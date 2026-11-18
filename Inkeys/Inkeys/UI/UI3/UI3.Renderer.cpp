module;

#include <windows.h>
#include "../../Log/Log.h"
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <cstdio>
#include <d2d1effects.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <bit>
#include <array>
#include <cwchar>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#pragma comment(lib, "dxguid.lib")

#include <cstdint>
#include <cstddef>

module Inkeys.UI.UI3;
import :Renderer;
import :Primitive;
import :Format;
import :Envelope;
import :Theme;

import Inkeys.UI.UI3.Animation;
import Inkeys.UI.UI3.Metrics;
import Inkeys.UI.RenderPipeline;
import Inkeys.Conv.Color;
import Inkeys.UI.UI3.Material;
import Inkeys.UI.UI3.PresentationProbe;
import Inkeys.UI.UI3.Superellipse;

using namespace std;
using Microsoft::WRL::ComPtr;

namespace Inkeys::UI::UI3
{


namespace
{
	bool TryMapDrawBoundsToDevice(ID2D1DeviceContext* context,
		const D2D1_RECT_F& bounds, D2D1_RECT_F& mapped) noexcept
	{
		if (!context || !std::isfinite(bounds.left) || !std::isfinite(bounds.top)
			|| !std::isfinite(bounds.right) || !std::isfinite(bounds.bottom)
			|| bounds.left >= bounds.right || bounds.top >= bounds.bottom) return false;
		D2D1_MATRIX_3X2_F matrix{}; context->GetTransform(&matrix);
		for (const FLOAT value : { matrix._11, matrix._12, matrix._21,
			matrix._22, matrix._31, matrix._32 }) if (!std::isfinite(value)) return false;
		FLOAT dpiX = 96.0F, dpiY = 96.0F;
		if (context->GetUnitMode() != D2D1_UNIT_MODE_PIXELS) context->GetDpi(&dpiX, &dpiY);
		if (!std::isfinite(dpiX) || !std::isfinite(dpiY) || dpiX <= 0 || dpiY <= 0) return false;
		const double scaleX = dpiX / 96.0, scaleY = dpiY / 96.0;
		bool first = true;
		for (const FLOAT x : { bounds.left, bounds.right })
			for (const FLOAT y : { bounds.top, bounds.bottom })
			{
				const FLOAT deviceX = static_cast<FLOAT>((x * static_cast<double>(matrix._11)
					+ y * static_cast<double>(matrix._21) + matrix._31) * scaleX);
				const FLOAT deviceY = static_cast<FLOAT>((x * static_cast<double>(matrix._12)
					+ y * static_cast<double>(matrix._22) + matrix._32) * scaleY);
				if (!std::isfinite(deviceX) || !std::isfinite(deviceY)) return false;
				if (first) { mapped = { deviceX, deviceY, deviceX, deviceY }; first = false; }
				else { mapped.left = (std::min)(mapped.left, deviceX); mapped.top = (std::min)(mapped.top, deviceY);
					mapped.right = (std::max)(mapped.right, deviceX); mapped.bottom = (std::max)(mapped.bottom, deviceY); }
			}
		return true;
	}

	double FrameBorderDrawOutset(double thickness, UiFrameRenderingEnum rendering,
		double zoom, double referenceZoom, double geometryScale) noexcept
	{
		if (!std::isfinite(thickness) || !std::isfinite(geometryScale))
			return std::numeric_limits<double>::infinity();
		double stroke = std::abs(thickness * zoom);
		if (rendering == UiFrameRenderingEnum::PointLight)
		{
			// 与现有 dirty 外扩同源，额外保留稳定 blur 倍率和动画遮罩的完整描边。
			stroke = (std::max)(stroke, stroke * std::abs(geometryScale));
			stroke += RenderingAttribute::pointLightDiffuseExtraWidth
				* (std::max)(zoom, referenceZoom);
		}
		return stroke + RenderingAttribute::dirtyAntialiasPadding;
	}

	void ObserveNonSvgRectWrite(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, FLOAT outset = 0.0F) noexcept
	{
		auto* scope = Inkeys::UI::UI3::CurrentUi3SvgScope();
		if (!scope || !scope->probe) return;
		D2D1_MATRIX_3X2_F matrix; context->GetTransform(&matrix);
		const std::uint32_t rectBits[4]{ std::bit_cast<std::uint32_t>(rect.left - outset), std::bit_cast<std::uint32_t>(rect.top - outset),
			std::bit_cast<std::uint32_t>(rect.right + outset), std::bit_cast<std::uint32_t>(rect.bottom + outset) };
		const std::uint32_t matrixBits[6]{ std::bit_cast<std::uint32_t>(matrix._11), std::bit_cast<std::uint32_t>(matrix._12),
			std::bit_cast<std::uint32_t>(matrix._21), std::bit_cast<std::uint32_t>(matrix._22),
			std::bit_cast<std::uint32_t>(matrix._31), std::bit_cast<std::uint32_t>(matrix._32) };
		scope->probe->ObserveNonSvgWrite(context, rectBits, matrixBits);
	}
	void ObserveNonSvgGeometryWrite(ID2D1DeviceContext* context, ID2D1Geometry* geometry, FLOAT stroke = 0.0F) noexcept
	{
		if (!Inkeys::UI::UI3::CurrentUi3SvgScope()) return;
		D2D1_RECT_F bounds{};
		const HRESULT result = stroke > 0.0F
			? geometry->GetWidenedBounds(stroke, nullptr, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &bounds)
			: geometry->GetBounds(nullptr, &bounds);
		if (FAILED(result)) { Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(context); return; }
		ObserveNonSvgRectWrite(context, bounds, D2D1_DEFAULT_FLATTENING_TOLERANCE);
	}
	[[nodiscard]] auto SharedD2DFactory()
	{
		return Inkeys::UI::RenderPipeline::D2DFactory();
	}

	[[nodiscard]] auto SharedDWriteFactory()
	{
		return Inkeys::UI::RenderPipeline::DWriteFactory();
	}

	[[nodiscard]] auto SharedFontCollection()
	{
		return Inkeys::UI::RenderPipeline::FontCollection();
	}

	// 只在遮罩未命中时测量创建尝试，稳态缓存与分片热路径不读取时钟。
	class ScopedMaskCreationTimer
	{
	public:
		explicit ScopedMaskCreationTimer(double* elapsedMs) noexcept
			: elapsedMs_(elapsedMs)
		{
			if (elapsedMs_) started_ = std::chrono::steady_clock::now();
		}
		~ScopedMaskCreationTimer() noexcept
		{
			if (elapsedMs_)
				*elapsedMs_ += std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - started_).count();
		}

	private:
		double* elapsedMs_ = nullptr;
		std::chrono::steady_clock::time_point started_{};
	};


}

FormatCache* UIRendering::GetTypographyCache()
{
	if (services_.typography)
		if (auto* cache = services_.typography()) return cache;
	if (!ownedTypography_)
		if (auto factory = SharedDWriteFactory())
			ownedTypography_ = std::make_unique<FormatCache>(factory.Get());
	return ownedTypography_.get();
}

bool UIRendering::UpdateFontRenderingPolicy(bool allowFractional) noexcept
{
	if (allowFractionalFontSize == allowFractional) return false;
	allowFractionalFontSize = allowFractional;
	return true;
}

void UIRendering::BeginFontFrame(std::chrono::steady_clock::time_point time,
	const UiAnimationAdvanceContextClass& context, double targetZoom)
{
	const auto serial = static_cast<std::uint64_t>(time.time_since_epoch().count());
	if (!fontFrameReady || serial != fontFrameSerial) fontAnimationsActive = false;
	fontFrameReady = true; fontFrameSerial = serial; fontFrameContext = context;
	fontTargetZoom = targetZoom; fontTargetTransformReady = false; fontDuration = 0.0;
}

FLOAT UIRendering::ResolveFontSize(ID2D1DeviceContext* context, double fontSize, double nominalTarget)
{
	return ResolveAnimatedFontSize(context, fontSize, nominalTarget >= 0.0 ? nominalTarget : fontSize / frameZoom, directFontCorrection);
}

FLOAT UIRendering::ResolveWordFontSize(ID2D1DeviceContext* context, const UiWordClass& word,
	double fontSize, double nominalTarget)
{
	if (nominalTarget < 0.0) nominalTarget = word.fontTargetSize >= 0.0
		? static_cast<double>(word.fontTargetSize) : static_cast<double>(word.size.tar);
	const double duration = (word.size.val == word.size.tar && !word.size.hasMiddleV) ? 0.0
		: static_cast<double>(word.size.dur) * (1.0 - static_cast<double>(word.size.progress));
	return ResolveAnimatedFontSize(context, fontSize, nominalTarget, word.fontCorrection, duration);
}

FLOAT UIRendering::ResolveAnimatedFontSize(ID2D1DeviceContext* context, double fontSize,
	double nominalTarget, UiFontCorrectionClass& correction, double duration)
{
	if (!context || !std::isfinite(fontSize) || fontSize <= 0.0) return 0.0F;
	D2D1_MATRIX_3X2_F transform{};
	context->GetTransform(&transform);
	FLOAT dpiX = 96.0F, dpiY = 96.0F;
	if (context->GetUnitMode() != D2D1_UNIT_MODE_PIXELS) context->GetDpi(&dpiX, &dpiY);
	if (!std::isfinite(dpiX) || !std::isfinite(dpiY) || dpiX <= 0.0F || dpiY <= 0.0F) return 0.0F;
	// frameZoom 已乘进字号；此处只合并 DC 额外变换与单位到设备像素的比例。
	const double pixelsPerUnit = std::hypot(
		static_cast<double>(transform._21) * dpiX / 96.0,
		static_cast<double>(transform._22) * dpiY / 96.0);
	if (!std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0.0) return 0.0F;
	const double targetZoom = fontTargetZoom > 0.0 ? fontTargetZoom : frameZoom;
	const double targetPixelsPerUnit = fontTargetTransformReady
		? std::hypot(fontTargetY[0]*dpiX/96.0,fontTargetY[1]*dpiY/96.0) : pixelsPerUnit;
	const double targetPixels = nominalTarget * targetZoom * targetPixelsPerUnit;
	const UiAnimationAdvanceContextClass immediate{0.0, 1.0, false, true};
	const double coefficient = correction.Resolve(targetPixels, allowFractionalFontSize, fontFrameSerial,
		fontFrameReady ? fontFrameContext : immediate, duration > 0.0 ? duration : fontDuration);
	fontAnimationsActive |= correction.Active();
	const double resolved = fontSize * coefficient;
	return std::isfinite(resolved) && resolved > 0.0
		&& resolved <= (std::numeric_limits<FLOAT>::max)() ? static_cast<FLOAT>(resolved) : 0.0F;
}

void UIRendering::ConfigureLocalizedTypography()
{
	// 字体族与 locale 使用同一语言快照，日/韩不再走简中字形。
	// 先捕获版本；并发提交最多导致下一帧再刷新，不能将旧字族标成新版本。
	const auto typography = services_.localizedTypography
		? services_.localizedTypography() : LocalizedTypography{};
	localeRevision = typography.revision;
	fontFamily = typography.fontFamily;
	textLocale = typography.textLocale;
	buttonLabelFitCache.clear();
}

bool UIRendering::RefreshLocalizedTypography()
{
	if (!services_.localizedTypographyRevision
		|| localeRevision == services_.localizedTypographyRevision()) return false;
	ConfigureLocalizedTypography();
	return true;
}

void UIRendering::NotifyFrameTargetReady(ID2D1DeviceContext* context, unsigned long long generation) noexcept
{
	deviceContext = context;
	deviceGeneration = generation;
}

void UIRendering::NotifyFrameTargetDiscarded()
{
	// 回调期间仍保持旧上下文与代次可见，随后才放下引用。
	DiscardDeviceDependentCaches();
	deviceContext.Reset();
	deviceGeneration = 0;
}

void UIRendering::DiscardDeviceDependentCaches()
{
	if (services_.resetDeviceCaches) services_.resetDeviceCaches();
	frameGradientBrushCache.clear();
	frameDiffuseMaskCache.clear();
	frameGeometryDiffuseMaskCache.clear();
	superellipseGeometryCache = {};
	frameSolidColorBrush.Reset();
	surfaceHighlightBrush.Reset();
	surfaceHighlightUnavailable = false;
	colorPickerHueGradientBrush.Reset();
	colorPickerLightGradientBrush.Reset();
	colorPickerDarkGradientBrush.Reset();
	roundStrokeStyle.Reset();
	frameGaussianBlurEffect.Reset();
	frameMaskDeviceContext.Reset();
	frameDiffuseExactMaskBrush.Reset();
	frameGradientFailureLogged = false;
	frameGradientUnavailable = false;
	roundStrokeStyleFailureLogged = false;
	roundStrokeStyleUnavailable = false;
	colorPickerGradientFailureLogged = false;
	colorPickerGradientUnavailable = false;
	frameDiffuseEffectFailureLogged = false;
	frameDiffuseMaskFailureLogged = false;
	frameDiffuseMaskUnavailable = false;
	frameDiffuseMaskCreatedThisFrame = false;
	frameDiffuseExactMaskUnavailable = false;
	frameDiffuseMaskGeometryScale = 1.0;
	frameDiffuseMaskFrameSerial = 0;
	frameDiffuseExactMaskUseSerial = 0;
	frameDiffuseExactMaskPromotionFrameSerial = 0;
	frameDirtyClipActive = false;
	frameDirtyClipDeviceRect = {};
	frameDirtyClipContext = nullptr;
	frameDirtyClipDeviceValid = false;
	frameCulledDrawCount = 0;
}

double UIRendering::GetFrameShapeDrawOutset(const UiShapeClass& shape) const noexcept
{
	if (!shape.frame) return RenderingAttribute::dirtyAntialiasPadding;
	return FrameBorderDrawOutset(shape.ft ? static_cast<double>(shape.ft->val) : 4.0,
		shape.frameRendering, frameZoom, frameDiffuseReferenceZoom, frameDiffuseMaskGeometryScale);
}

bool UIRendering::ShouldDrawFrameBounds(ID2D1DeviceContext* context,
	const D2D1_RECT_F& bounds, double outset) noexcept
{
	if (!frameDirtyClipActive || !frameDirtyClipDeviceValid || context != frameDirtyClipContext
		|| !std::isfinite(outset) || outset < 0.0) return true;
	// 有限提交的 SVG 证明必须保留原观察顺序；跳过整颗按钮不能伪造资源/覆盖成功。
	const auto* observation = Inkeys::UI::UI3::CurrentUi3SvgScope();
	if (observation && observation->probe) return true;
	const auto envelope = D2D1::RectF(static_cast<FLOAT>(bounds.left - outset),
		static_cast<FLOAT>(bounds.top - outset), static_cast<FLOAT>(bounds.right + outset),
		static_cast<FLOAT>(bounds.bottom + outset));
	D2D1_RECT_F deviceBounds{};
	if (!TryMapDrawBoundsToDevice(context, envelope, deviceBounds)) return true;
	// clip 和物体各留设备像素余量；旋转、按压和 popup 缩放都经同一当前矩阵。
	constexpr FLOAT padding = static_cast<FLOAT>(RenderingAttribute::dirtyAntialiasPadding);
	if (deviceBounds.right + padding >= frameDirtyClipDeviceRect.left - padding
		&& deviceBounds.bottom + padding >= frameDirtyClipDeviceRect.top - padding
		&& deviceBounds.left - padding <= frameDirtyClipDeviceRect.right + padding
		&& deviceBounds.top - padding <= frameDirtyClipDeviceRect.bottom + padding) return true;
	++frameCulledDrawCount;
	return false;
}

void UIRendering::PushPanelOpacity(ID2D1DeviceContext* context,
	const D2D1_RECT_F& bounds, double opacity)
{
	if (!context) return;
	// 一个面板只合成一次透明度；明确包络，避免高水位位图成为每层分配尺寸。
	const FLOAT zoom=static_cast<FLOAT>(frameDiffuseReferenceZoom>0.0?frameDiffuseReferenceZoom:frameZoom);
	const FLOAT outset=static_cast<FLOAT>((RenderingAttribute::pointLightDiffuseExtraWidth
		+ButtonFrameThicknessDip)*zoom+RenderingAttribute::dirtyAntialiasPadding);
	const auto envelope=D2D1::RectF(bounds.left-outset,bounds.top-outset,bounds.right+outset,bounds.bottom+outset);
	context->PushLayer(D2D1::LayerParameters(envelope, nullptr,
		D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
		static_cast<FLOAT>((std::clamp)(opacity, 0.0, 1.0))), nullptr);
}

void UIRendering::AdvanceFrameDiffuseMaskSerial()
{
	// 仅实际进入 BeginDraw 的帧参与“连续 8 帧”晋升，空闲唤醒不会打断候选。
	if (++frameDiffuseMaskFrameSerial == 0)
	{
		frameDiffuseMaskFrameSerial = 1;
		for (auto& cache : frameDiffuseMaskCache)
		{
			cache.exactCandidates = {};
			cache.exactMasks.clear();
		}
	}
}

void UIRendering::NotifyFrameDirtyClipPushed(
	ID2D1DeviceContext* context, const D2D1_RECT_F& dirtyRect)
{
	AdvanceFrameDiffuseMaskSerial();
	frameCulledDrawCount = 0;
	frameDirtyClipContext = context;
	frameDirtyClipDeviceValid = TryMapDrawBoundsToDevice(context, dirtyRect, frameDirtyClipDeviceRect);
	frameDirtyClipActive = true;
}

void UIRendering::NotifyFrameDirtyClipPopped() noexcept
{
	frameDirtyClipActive = false;
	frameDirtyClipContext = nullptr;
	frameDirtyClipDeviceValid = false;
}

void UIRendering::NotifyFrameBegunWithoutClip()
{
	AdvanceFrameDiffuseMaskSerial();
	frameCulledDrawCount = 0;
}

void UIRendering::NotifyFrameEnded(HRESULT endDrawResult)
{
	HandleFrameEndDrawResult(endDrawResult);
	if (ownedTypography_) ownedTypography_->Clean();
}

void UIRendering::HandleFrameEndDrawResult(HRESULT endDrawResult)
{
	if (SUCCEEDED(endDrawResult))
	{
		frameDiffuseMaskCreatedThisFrame = false;
		return;
	}
	// 主目标提交失败后丢弃整图候选与成品，避免跨失败帧沿用未验证的热度和资源。
	for (auto& cache : frameDiffuseMaskCache)
	{
		cache.exactCandidates = {};
		cache.exactMasks.clear();
	}
	frameDiffuseExactMaskPromotionFrameSerial = 0;
	if (!frameDiffuseMaskCreatedThisFrame) return;

	// A8/Effect 错误可能只在 EndDraw 暴露；本会话立即降级，不能进入逐帧重试。
	frameDiffuseMaskCache.clear();
	frameGeometryDiffuseMaskCache.clear();
	frameGaussianBlurEffect.Reset();
	frameDiffuseMaskUnavailable = true;
	frameDiffuseMaskCreatedThisFrame = false;
	if (!frameDiffuseMaskFailureLogged)
	{
		frameDiffuseMaskFailureLogged = true;
		Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
			"A8 预模糊遮罩提交失败，本设备停用柔光遮罩, hr=0x{:08X}",
			static_cast<unsigned int>(endDrawResult));
	}
}
UiFrameLightingSnapshot UIRendering::SnapshotFrameLighting() const noexcept
{
	UiFrameLightingSnapshot snapshot;
	snapshot.primaryLight = framePrimaryLight;
	snapshot.primaryRadius = frameLightRadius;
	snapshot.cursorLight = frameCursorLight;
	snapshot.cursorScreenLight = frameCursorScreenLight;
	snapshot.cursorRadius = frameCursorLightRadius;
	snapshot.cursorIntensity = frameCursorLightIntensity;
	snapshot.drawingPenColor = frameDrawingPenColor;
	snapshot.drawingPenColorBlend = frameDrawingPenColorBlend;
	snapshot.drawingLightOpacity = frameDrawingLightOpacity;
	snapshot.primaryLightVisible = framePrimaryLightAnchorInitialized
		&& frameLightRadius > 0.0F;
	snapshot.cursorLightVisible = frameCursorLightVisible
		&& frameCursorLightRadius > 0.0F;
	snapshot.edgeLightingEnabled = frameEdgeLightingEnabled;
	return snapshot;
}

void UIRendering::SetFrameLightingSnapshot(
	const UiFrameLightingSnapshot& snapshot, bool edgeLightingEnabled) noexcept
{
	framePrimaryLight = snapshot.primaryLight;
	frameLightRadius = std::isfinite(snapshot.primaryRadius)
		&& snapshot.primaryRadius > 0.0F ? snapshot.primaryRadius : 0.0F;
	framePrimaryLightAnchorInitialized = snapshot.primaryLightVisible
		&& frameLightRadius > 0.0F;
	frameCursorLight = snapshot.cursorLight;
	frameCursorScreenLight = snapshot.cursorScreenLight;
	frameCursorLightRadius = std::isfinite(snapshot.cursorRadius)
		&& snapshot.cursorRadius > 0.0F ? snapshot.cursorRadius : 0.0F;
	frameCursorLightIntensity = std::isfinite(snapshot.cursorIntensity)
		? std::clamp(snapshot.cursorIntensity, 0.0F, 1.0F) : 0.0F;
	frameCursorLightVisible = snapshot.cursorLightVisible
		&& frameCursorLightRadius > 0.0F
		&& frameCursorLightIntensity > 0.0001F;
	SetFrameCursorLightLocalGeometry(frameCursorLight,
		D2D1::SizeF(frameCursorLightRadius, frameCursorLightRadius));
	frameEdgeLightingEnabled = snapshot.edgeLightingEnabled;
	frameEdgeLightingAllowed = edgeLightingEnabled;
	frameDrawingPenColor = snapshot.drawingPenColor;
	frameDrawingPenColorBlend = std::clamp(
		snapshot.drawingPenColorBlend, 0.0, 1.0);
	frameDrawingLightOpacity = std::clamp(
		snapshot.drawingLightOpacity, 0.0, 1.0);
}

RECT UIRendering::GetFramePrimaryLightDamageBounds() const noexcept
{
	if (!frameEdgeLightingEnabled || !framePrimaryLightAnchorInitialized)
		return {};
	const double padding = RenderingAttribute::pointLightDiffuseExtraWidth
		* frameZoom + RenderingAttribute::dirtyAntialiasPadding;
	const double radius = (std::max)(0.0, static_cast<double>(frameLightRadius)) + padding;
	return RECT(
		static_cast<LONG>(floor(static_cast<double>(framePrimaryLight.x) - radius)),
		static_cast<LONG>(floor(static_cast<double>(framePrimaryLight.y) - radius)),
		static_cast<LONG>(ceil(static_cast<double>(framePrimaryLight.x) + radius)),
		static_cast<LONG>(ceil(static_cast<double>(framePrimaryLight.y) + radius)));
}

RECT UIRendering::GetFrameCursorLightDamageBounds() const noexcept
{
	const bool cursorLightCanContribute = frameCursorLightIntensity > 0.0001F;
	if (!frameEdgeLightingEnabled || !cursorLightCanContribute)
		return {};
	const double padding = RenderingAttribute::pointLightDiffuseExtraWidth
		* frameZoom + RenderingAttribute::dirtyAntialiasPadding;
	const double radius = (std::max)(0.0, static_cast<double>(frameCursorLightRadius)) + padding;
	return RECT(
		static_cast<LONG>(floor(static_cast<double>(frameCursorLight.x) - radius)),
		static_cast<LONG>(floor(static_cast<double>(frameCursorLight.y) - radius)),
		static_cast<LONG>(ceil(static_cast<double>(frameCursorLight.x) + radius)),
		static_cast<LONG>(ceil(static_cast<double>(frameCursorLight.y) + radius)));
}

ComPtr<ID2D1RadialGradientBrush> UIRendering::GetFrameGradientBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color,
	BorderLightSourceEnum lightSource, FLOAT radiusScale)
{
	COLORREF rgb = color & 0x00FFFFFF;
	D2D1_POINT_2F center = framePrimaryLight;
	radiusScale = std::isfinite(radiusScale)
		? clamp(radiusScale, 0.0F, 1.0F) : 1.0F;
	D2D1_SIZE_F radius = D2D1::SizeF(
		frameLightRadius * radiusScale, frameLightRadius * radiusScale);
	if (lightSource == BorderLightSourceEnum::Cursor)
	{
		center = frameLocalCursorLight;
		radius = D2D1::SizeF(
			frameLocalCursorLightRadiusX * radiusScale,
			frameLocalCursorLightRadiusY * radiusScale);
	}
	if (radius.width <= 0.0F || radius.height <= 0.0F) return nullptr;

	for (auto& cache : frameGradientBrushCache)
	{
		if (cache.color == rgb && cache.lightSource == lightSource)
		{
			// 颜色停靠点长期复用，动态光位置与半径只更新画刷的轻量属性。
			cache.brush->SetCenter(center);
			cache.brush->SetGradientOriginOffset(D2D1::Point2F());
			cache.brush->SetRadiusX(radius.width);
			cache.brush->SetRadiusY(radius.height);
			return cache.brush;
		}
	}
	if (frameGradientUnavailable) return nullptr;

	D2D1_GRADIENT_STOP gradientStops[] =
	{
		{ 0.00F, Inkeys::Color::ConvertToD2dColor(rgb, 1.00) },
		{ 0.25F, Inkeys::Color::ConvertToD2dColor(rgb, 0.72) },
		{ 0.65F, Inkeys::Color::ConvertToD2dColor(rgb, 0.20) },
		{ 1.00F, Inkeys::Color::ConvertToD2dColor(rgb, 0.00) },
	};

	ComPtr<ID2D1GradientStopCollection> stopCollection;
	HRESULT hr = deviceContext->CreateGradientStopCollection(
		gradientStops, ARRAYSIZE(gradientStops), D2D1_GAMMA_2_2,
		D2D1_EXTEND_MODE_CLAMP, &stopCollection);
	if (SUCCEEDED(hr))
	{
		FrameGradientBrushCacheClass cache;
		cache.color = rgb;
		cache.lightSource = lightSource;
		hr = deviceContext->CreateRadialGradientBrush(
			D2D1::RadialGradientBrushProperties(
				center, D2D1::Point2F(), radius.width, radius.height),
			stopCollection.Get(), &cache.brush);
		if (SUCCEEDED(hr))
		{
			// 动画混色会产生短期颜色，限制缓存容量避免长期运行后无界增长。
			if (frameGradientBrushCache.size() >= 32)
				frameGradientBrushCache.erase(frameGradientBrushCache.begin());
			frameGradientBrushCache.emplace_back(move(cache));
			return frameGradientBrushCache.back().brush;
		}
	}

	frameGradientUnavailable = true;
	if (!frameGradientFailureLogged)
	{
		frameGradientFailureLogged = true;
		Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
			"创建边框点光渐变失败, hr=0x{:08X}",
			static_cast<unsigned int>(hr));
	}
	return nullptr;
}

ID2D1SolidColorBrush* UIRendering::GetFrameSolidColorBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color, double opacity)
{
	if (!deviceContext) return nullptr;
	D2D1_COLOR_F d2dColor = Inkeys::Color::ConvertToD2dColor(
		color, clamp(opacity, 0.0, 1.0));
	if (!frameSolidColorBrush)
	{
		if (FAILED(deviceContext->CreateSolidColorBrush(
			d2dColor, &frameSolidColorBrush)))
			return nullptr;
	}
	else
	{
		frameSolidColorBrush->SetColor(d2dColor);
		frameSolidColorBrush->SetOpacity(1.0F);
	}
	return frameSolidColorBrush.Get();
}

ID2D1LinearGradientBrush* UIRendering::GetColorPickerHueGradientBrush(
	ID2D1DeviceContext* deviceContext,
	D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint)
{
	if (!deviceContext || colorPickerGradientUnavailable) return nullptr;
	if (!colorPickerHueGradientBrush)
	{
		// 色相停靠点只在当前 device generation 创建一次，帧内只更新端点。
		D2D1_GRADIENT_STOP stops[] =
		{
			{ 0.0F, D2D1::ColorF(D2D1::ColorF::Red) },
			{ 1.0F / 6.0F, D2D1::ColorF(D2D1::ColorF::Yellow) },
			{ 2.0F / 6.0F, D2D1::ColorF(D2D1::ColorF::Lime) },
			{ 3.0F / 6.0F, D2D1::ColorF(D2D1::ColorF::Cyan) },
			{ 4.0F / 6.0F, D2D1::ColorF(D2D1::ColorF::Blue) },
			{ 5.0F / 6.0F, D2D1::ColorF(D2D1::ColorF::Magenta) },
			{ 1.0F, D2D1::ColorF(D2D1::ColorF::Red) },
		};
		ComPtr<ID2D1GradientStopCollection> collection;
		HRESULT hr = deviceContext->CreateGradientStopCollection(
			stops, ARRAYSIZE(stops), D2D1_GAMMA_1_0,
			D2D1_EXTEND_MODE_CLAMP, &collection);
		if (SUCCEEDED(hr))
			hr = deviceContext->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(startPoint, endPoint),
				collection.Get(), &colorPickerHueGradientBrush);
		if (FAILED(hr))
		{
			colorPickerGradientUnavailable = true;
			if (!colorPickerGradientFailureLogged)
			{
				colorPickerGradientFailureLogged = true;
				Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
					"创建颜色选择器渐变失败, hr=0x{:08X}",
					static_cast<unsigned int>(hr));
			}
			return nullptr;
		}
	}
	colorPickerHueGradientBrush->SetStartPoint(startPoint);
	colorPickerHueGradientBrush->SetEndPoint(endPoint);
	colorPickerHueGradientBrush->SetOpacity(1.0F);
	return colorPickerHueGradientBrush.Get();
}

ID2D1LinearGradientBrush* UIRendering::GetColorPickerToneGradientBrush(
	ID2D1DeviceContext* deviceContext, bool darkTone,
	D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint, FLOAT opacity)
{
	if (!deviceContext || colorPickerGradientUnavailable) return nullptr;
	auto& targetBrush = darkTone
		? colorPickerDarkGradientBrush : colorPickerLightGradientBrush;
	if (!targetBrush)
	{
		D2D1_GRADIENT_STOP stops[2]{};
		if (darkTone)
		{
			stops[0] = { 0.0F, D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F) };
			stops[1] = { 1.0F, D2D1::ColorF(0.0F, 0.0F, 0.0F, 1.0F) };
		}
		else
		{
			stops[0] = { 0.0F, D2D1::ColorF(1.0F, 1.0F, 1.0F, 1.0F) };
			stops[1] = { 1.0F, D2D1::ColorF(1.0F, 1.0F, 1.0F, 0.0F) };
		}
		ComPtr<ID2D1GradientStopCollection> collection;
		HRESULT hr = deviceContext->CreateGradientStopCollection(
			stops, ARRAYSIZE(stops), D2D1_GAMMA_1_0,
			D2D1_EXTEND_MODE_CLAMP, &collection);
		if (SUCCEEDED(hr))
			hr = deviceContext->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(startPoint, endPoint),
				collection.Get(), &targetBrush);
		if (FAILED(hr))
		{
			colorPickerGradientUnavailable = true;
			if (!colorPickerGradientFailureLogged)
			{
				colorPickerGradientFailureLogged = true;
				Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
					"创建颜色选择器明暗覆盖失败, hr=0x{:08X}",
					static_cast<unsigned int>(hr));
			}
			return nullptr;
		}
	}
	targetBrush->SetStartPoint(startPoint);
	targetBrush->SetEndPoint(endPoint);
	targetBrush->SetOpacity(clamp(opacity, 0.0F, 1.0F));
	return targetBrush.Get();
}

bool UIRendering::DrawProgressRing(ID2D1DeviceContext* deviceContext,
	D2D1_POINT_2F center, FLOAT radius, FLOAT strokeWidth,
	FLOAT progress, COLORREF trackColor, COLORREF progressColor,
	FLOAT trackOpacity, FLOAT progressOpacity, bool roundCaps)
{
	if (!deviceContext || radius <= 0.0F || strokeWidth <= 0.0F) return true;
	progress = clamp(progress, 0.0F, 1.0F);
	D2D1_ELLIPSE ellipse = D2D1::Ellipse(center, radius, radius);
	if (auto trackBrush = GetFrameSolidColorBrush(
		deviceContext, trackColor, trackOpacity))
		(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(deviceContext), deviceContext)->DrawEllipse(&ellipse, trackBrush, strokeWidth);
	else return false;
	if (progress <= 0.000001F) return true;
	if (auto progressBrush = GetFrameSolidColorBrush(
		deviceContext, progressColor, progressOpacity))
	{
		if (progress >= 0.999F)
		{
			(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(deviceContext), deviceContext)->DrawEllipse(&ellipse, progressBrush, strokeWidth);
			return true;
		}

		FLOAT sweep = 360.0F * progress;
		D2D1_POINT_2F start = D2D1::Point2F(center.x, center.y - radius);
		FLOAT angle = (-90.0F + sweep) * 3.14159265F / 180.0F;
		D2D1_POINT_2F end = D2D1::Point2F(
			center.x + radius * cosf(angle),
			center.y + radius * sinf(angle));
		ComPtr<ID2D1PathGeometry> path;
		ComPtr<ID2D1GeometrySink> sink;
		auto factory = SharedD2DFactory();
		if (!factory || FAILED(factory->CreatePathGeometry(&path))
			|| FAILED(path->Open(&sink)))
			return false;
		sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
		D2D1_ARC_SEGMENT arc{};
		arc.point = end;
		arc.size = D2D1::SizeF(radius, radius);
		arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
		arc.arcSize = sweep >= 180.0F
			? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
		sink->AddArc(arc);
		sink->EndFigure(D2D1_FIGURE_END_OPEN);
		if (FAILED(sink->Close())) return false;
			(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(deviceContext), deviceContext)->DrawGeometry(path.Get(), progressBrush, strokeWidth, roundCaps ? GetRoundStrokeStyle() : nullptr);
	} else return false;
	return true;
}

ID2D1StrokeStyle* UIRendering::GetRoundStrokeStyle()
{
	if (roundStrokeStyle) return roundStrokeStyle.Get();
	auto factory = SharedD2DFactory();
	if (!factory || roundStrokeStyleUnavailable) return nullptr;
	D2D1_STROKE_STYLE_PROPERTIES properties = D2D1::StrokeStyleProperties(
		D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
		D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND,
		10.0F, D2D1_DASH_STYLE_SOLID, 0.0F);
	HRESULT hr = factory->CreateStrokeStyle(
		properties, nullptr, 0, &roundStrokeStyle);
	if (FAILED(hr))
	{
		roundStrokeStyleUnavailable = true;
		if (!roundStrokeStyleFailureLogged)
		{
			roundStrokeStyleFailureLogged = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建圆头描边样式失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}
	return roundStrokeStyle.Get();
}

UIRendering::FrameDiffuseMaskCacheClass* UIRendering::GetRoundedRectDiffuseMask(
	ID2D1DeviceContext* deviceContext,
	const D2D1_ROUNDED_RECT& roundedRect, FLOAT strokeWidth)
{
	if (!deviceContext || strokeWidth <= 0.0F
		|| frameDiffuseMaskUnavailable) return nullptr;
	FLOAT standardDeviation = static_cast<FLOAT>(
		RenderingAttribute::pointLightDiffuseExtraWidth / 6.0
		* (frameDiffuseReferenceZoom > 0.0 ? frameDiffuseReferenceZoom : frameZoom));
	if (standardDeviation <= 0.0F) return nullptr;

	auto QuantizeQuarter = [](FLOAT value) -> int
		{
			return (std::max)(0, static_cast<int>(lround(static_cast<double>(value) * 4.0)));
		};
	int radiusXQuarter = QuantizeQuarter(roundedRect.radiusX);
	int radiusYQuarter = QuantizeQuarter(roundedRect.radiusY);
	int strokeWidthQuarter = (std::max)(1, QuantizeQuarter(strokeWidth));
	int standardDeviationQuarter = (std::max)(1, QuantizeQuarter(standardDeviation));
	auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics();
	for (auto& cache : frameDiffuseMaskCache)
	{
		if (cache.radiusXQuarter == radiusXQuarter
			&& cache.radiusYQuarter == radiusYQuarter
			&& cache.strokeWidthQuarter == strokeWidthQuarter
			&& cache.standardDeviationQuarter == standardDeviationQuarter)
		{
			if (diagnostics) ++diagnostics->light.roundedParentHit;
			return &cache;
		}
	}

	if (diagnostics) ++diagnostics->light.roundedParentMiss;
	ScopedMaskCreationTimer creationTimer(
		diagnostics ? &diagnostics->light.roundedParentCreateMs : nullptr);

	if (!frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		ComPtr<ID2D1Device> owningDevice;
		deviceContext->GetDevice(&owningDevice);
		HRESULT hr = owningDevice
			? owningDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &frameMaskDeviceContext)
			: E_POINTER;
		if (FAILED(hr))
		{
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建遮罩缓存 DeviceContext 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameGaussianBlurEffect
		&& frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		HRESULT hr = frameMaskDeviceContext->CreateEffect(
			CLSID_D2D1GaussianBlur, &frameGaussianBlurEffect);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
				D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
		if (FAILED(hr))
		{
			frameGaussianBlurEffect.Reset();
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建 Gaussian Blur Effect 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameMaskDeviceContext || !frameGaussianBlurEffect)
	{
		if (diagnostics) ++diagnostics->light.roundedParentFailure;
		return nullptr;
	}

	FrameDiffuseMaskCacheClass cache;
	cache.radiusXQuarter = radiusXQuarter;
	cache.radiusYQuarter = radiusYQuarter;
	cache.strokeWidthQuarter = strokeWidthQuarter;
	cache.standardDeviationQuarter = standardDeviationQuarter;
	cache.radiusX = static_cast<FLOAT>(radiusXQuarter) / 4.0F;
	cache.radiusY = static_cast<FLOAT>(radiusYQuarter) / 4.0F;
	FLOAT cachedStrokeWidth = static_cast<FLOAT>(strokeWidthQuarter) / 4.0F;
	FLOAT cachedStandardDeviation =
		static_cast<FLOAT>(standardDeviationQuarter) / 4.0F;
	cache.padding = ceilf(cachedStandardDeviation * 3.0F
		+ cachedStrokeWidth * 0.5F + 1.0F);
	cache.size = D2D1::SizeF(
		cache.padding * 2.0F + cache.radiusX * 2.0F + 1.0F,
		cache.padding * 2.0F + cache.radiusY * 2.0F + 1.0F);

	D2D1_SIZE_U pixelSize = D2D1::SizeU(
		(std::max<UINT32>)(1, static_cast<UINT32>(ceilf(cache.size.width))),
		(std::max<UINT32>)(1, static_cast<UINT32>(ceilf(cache.size.height))));
	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(
			DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0F, 96.0F);
	ComPtr<ID2D1Bitmap1> sourceBitmap;
	ComPtr<ID2D1Bitmap1> outputBitmap;
	HRESULT hr = frameMaskDeviceContext->CreateBitmap(
		pixelSize, nullptr, 0, bitmapProperties, &sourceBitmap);
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateBitmap(
			pixelSize, nullptr, 0, bitmapProperties, &outputBitmap);
	// 所有父遮罩与精确遮罩共用恒白画刷，资源重建时随遮罩上下文统一释放。
	if (SUCCEEDED(hr) && !frameDiffuseExactMaskBrush)
		hr = frameMaskDeviceContext->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White), &frameDiffuseExactMaskBrush);
	if (SUCCEEDED(hr))
		hr = frameGaussianBlurEffect->SetValue(
			D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
			cachedStandardDeviation);

	if (SUCCEEDED(hr))
	{
		D2D1_COLOR_F transparent = D2D1::ColorF(0.0F, 0.0F);
		D2D1_ROUNDED_RECT localRoundedRect = D2D1::RoundedRect(
			D2D1::RectF(
				cache.padding, cache.padding,
				cache.padding + cache.radiusX * 2.0F + 1.0F,
				cache.padding + cache.radiusY * 2.0F + 1.0F),
			cache.radiusX, cache.radiusY);

		// 缓存上下文分两次提交，避免同一 BeginDraw 内把刚写完的 Target 当作 Effect 输入。
		frameMaskDeviceContext->SetTarget(sourceBitmap.Get());
		frameMaskDeviceContext->BeginDraw();
		frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
		frameMaskDeviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
		frameMaskDeviceContext->Clear(&transparent);
		frameMaskDeviceContext->DrawRoundedRectangle(
			&localRoundedRect, frameDiffuseExactMaskBrush.Get(), cachedStrokeWidth);
		hr = frameMaskDeviceContext->EndDraw();
		frameMaskDeviceContext->SetTarget(nullptr);

		if (SUCCEEDED(hr))
		{
			frameGaussianBlurEffect->SetInput(0, sourceBitmap.Get());
			frameMaskDeviceContext->SetTarget(outputBitmap.Get());
			frameMaskDeviceContext->BeginDraw();
			frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
			frameMaskDeviceContext->Clear(&transparent);
			frameMaskDeviceContext->DrawImage(frameGaussianBlurEffect.Get());
			hr = frameMaskDeviceContext->EndDraw();
			frameMaskDeviceContext->SetTarget(nullptr);
			frameGaussianBlurEffect->SetInput(0, nullptr);
		}
	}

	if (FAILED(hr))
	{
		if (diagnostics) ++diagnostics->light.roundedParentFailure;
		frameDiffuseMaskUnavailable = true;
		if (!frameDiffuseMaskFailureLogged)
		{
			frameDiffuseMaskFailureLogged = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建预模糊遮罩失败，本设备停用柔光遮罩, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	cache.bitmap = move(outputBitmap);
	if (frameDiffuseMaskCache.size() >= 24)
	{
		frameDiffuseMaskCache.erase(frameDiffuseMaskCache.begin());
	}
	frameDiffuseMaskCache.emplace_back(move(cache));
	frameDiffuseMaskCreatedThisFrame = true;
	if (diagnostics) ++diagnostics->light.roundedParentCreate;
	return &frameDiffuseMaskCache.back();
}

unsigned int UIRendering::FillRoundedRectDiffuseMaskSlices(
	ID2D1DeviceContext* deviceContext, ID2D1Bitmap* bitmap,
	ID2D1Brush* brush, const FLOAT* sourceX, const FLOAT* sourceY,
	const FLOAT* destinationX, const FLOAT* destinationY,
	int segmentCount)
{
	if (!deviceContext || !bitmap || !brush || !sourceX || !sourceY
		|| !destinationX || !destinationY || segmentCount <= 0) return 0;
	// 此私有入口只使用本renderer由几何生成的A8遮罩，不能给任意旧SVG bitmap同样资格。
	unsigned int fillCount = 0;
	for (int y = 0; y < segmentCount; y++)
	{
		for (int x = 0; x < segmentCount; x++)
		{
			if (destinationX[x + 1] <= destinationX[x]
				|| destinationY[y + 1] <= destinationY[y]) continue;
			D2D1_RECT_F destinationRect = D2D1::RectF(
				destinationX[x], destinationY[y],
				destinationX[x + 1], destinationY[y + 1]);
			D2D1_RECT_F sourceRect = D2D1::RectF(
				sourceX[x], sourceY[y],
				sourceX[x + 1], sourceY[y + 1]);
			(ObserveNonSvgRectWrite(deviceContext, destinationRect), deviceContext)->FillOpacityMask(
				bitmap, brush, &destinationRect, &sourceRect);
			++fillCount;
		}
	}
	// 聚合实际提交数，包含整图烘焙；零面积跳片不计入，也不逐片访问采样槽。
	if (auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics())
		diagnostics->light.slices += fillCount;
	return fillCount;
}

HRESULT UIRendering::CreateRoundedRectExactMask(
	const FrameDiffuseMaskCacheClass& mask,
	const FrameDiffuseExactMaskKeyClass& key,
	ComPtr<ID2D1Bitmap1>& outputBitmap)
{
	outputBitmap.Reset();
	if (!frameMaskDeviceContext || !mask.bitmap
		|| key.width == 0 || key.height == 0) return E_POINTER;

	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(
			DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0F, 96.0F);
	HRESULT hr = frameMaskDeviceContext->CreateBitmap(
		D2D1::SizeU(key.width, key.height), nullptr, 0,
		bitmapProperties, &outputBitmap);
	if (SUCCEEDED(hr) && !frameDiffuseExactMaskBrush)
		hr = frameMaskDeviceContext->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White),
			&frameDiffuseExactMaskBrush);
	if (FAILED(hr))
	{
		outputBitmap.Reset();
		return hr;
	}

	ComPtr<ID2D1Image> originalTarget;
	frameMaskDeviceContext->GetTarget(&originalTarget);
	D2D1_MATRIX_3X2_F originalTransform{};
	frameMaskDeviceContext->GetTransform(&originalTransform);
	D2D1_ANTIALIAS_MODE originalAntialiasMode =
		frameMaskDeviceContext->GetAntialiasMode();
	D2D1_PRIMITIVE_BLEND originalPrimitiveBlend =
		frameMaskDeviceContext->GetPrimitiveBlend();

	const FLOAT width = static_cast<FLOAT>(key.width);
	const FLOAT height = static_cast<FLOAT>(key.height);
	const FLOAT rectLeft = mask.padding;
	const FLOAT rectTop = mask.padding;
	const FLOAT rectRight = width - mask.padding;
	const FLOAT rectBottom = height - mask.padding;
	const FLOAT middleLeft = (std::min)(
		rectLeft + key.radiusX, (rectLeft + rectRight) * 0.5F);
	const FLOAT middleRight = (std::max)(
		rectRight - key.radiusX, middleLeft);
	const FLOAT middleTop = (std::min)(
		rectTop + key.radiusY, (rectTop + rectBottom) * 0.5F);
	const FLOAT middleBottom = (std::max)(
		rectBottom - key.radiusY, middleTop);
	const FLOAT sourceX[] = { 0.0F,
		mask.padding + mask.radiusX,
		mask.padding + mask.radiusX + 1.0F, mask.size.width };
	const FLOAT sourceY[] = { 0.0F,
		mask.padding + mask.radiusY,
		mask.padding + mask.radiusY + 1.0F, mask.size.height };
	const FLOAT destinationX[] = {
		0.0F, middleLeft, middleRight, width };
	const FLOAT destinationY[] = {
		0.0F, middleTop, middleBottom, height };

	// 专用上下文烘焙完整 A8；无论提交结果如何，都恢复调用前的 target 与绘制状态。
	frameMaskDeviceContext->SetTarget(outputBitmap.Get());
	frameMaskDeviceContext->BeginDraw();
	frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
	frameMaskDeviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	frameMaskDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	D2D1_COLOR_F transparent = D2D1::ColorF(0.0F, 0.0F);
	frameMaskDeviceContext->Clear(&transparent);
	FillRoundedRectDiffuseMaskSlices(
		frameMaskDeviceContext.Get(), mask.bitmap.Get(),
		frameDiffuseExactMaskBrush.Get(), sourceX, sourceY,
		destinationX, destinationY, 3);
	hr = frameMaskDeviceContext->EndDraw();
	frameMaskDeviceContext->SetTarget(originalTarget.Get());
	frameMaskDeviceContext->SetTransform(originalTransform);
	frameMaskDeviceContext->SetPrimitiveBlend(originalPrimitiveBlend);
	frameMaskDeviceContext->SetAntialiasMode(originalAntialiasMode);
	if (FAILED(hr)) outputBitmap.Reset();
	return hr;
}

UIRendering::FrameDiffuseExactMaskSelectionClass
UIRendering::ResolveRoundedRectExactMask(
	ID2D1DeviceContext* deviceContext,
	FrameDiffuseMaskCacheClass& mask,
	const D2D1_ROUNDED_RECT& roundedRect)
{
	FrameDiffuseExactMaskSelectionClass selection;
	using ExactFallback = Inkeys::UI::RenderPipeline::ExactFallback;
	auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics();
	auto Fallback = [&](ExactFallback reason)
		{
			if (diagnostics)
				++diagnostics->light.exactFallback[static_cast<std::size_t>(reason)];
			return selection;
		};
	if (!deviceContext || !mask.bitmap || !frameMaskDeviceContext)
		return Fallback(ExactFallback::Other);
	if (frameDiffuseExactMaskUnavailable)
		return Fallback(ExactFallback::Unavailable);
	if (!isfinite(frameDiffuseMaskGeometryScale)
		|| abs(frameDiffuseMaskGeometryScale - 1.0) > 0.001)
		return Fallback(ExactFallback::GeometryScale);

	const FLOAT radiusX = (std::max)(0.0F, roundedRect.radiusX);
	const FLOAT radiusY = (std::max)(0.0F, roundedRect.radiusY);
	const bool geometryScaled = abs(radiusX - mask.radiusX) > 0.001F
		|| abs(radiusY - mask.radiusY) > 0.001F;
	if (geometryScaled) return Fallback(ExactFallback::QuantizedRadius);
	if (!isfinite(radiusX) || !isfinite(radiusY)
		|| roundedRect.rect.right <= roundedRect.rect.left
		|| roundedRect.rect.bottom <= roundedRect.rect.top)
		return Fallback(ExactFallback::SizeOrBudget);

	FLOAT dpiX = 0.0F;
	FLOAT dpiY = 0.0F;
	deviceContext->GetDpi(&dpiX, &dpiY);
	if (!isfinite(dpiX) || !isfinite(dpiY)
		|| dpiX != 96.0F || dpiY != 96.0F) return Fallback(ExactFallback::Dpi);

	D2D1_MATRIX_3X2_F transform{};
	deviceContext->GetTransform(&transform);
	if (transform._11 != 1.0F || transform._12 != 0.0F
		|| transform._21 != 0.0F || transform._22 != 1.0F
		|| transform._31 != 0.0F || transform._32 != 0.0F)
		return Fallback(ExactFallback::Transform);

	const FLOAT destinationLeft = roundedRect.rect.left - mask.padding;
	const FLOAT destinationTop = roundedRect.rect.top - mask.padding;
	const FLOAT destinationRight = roundedRect.rect.right + mask.padding;
	const FLOAT destinationBottom = roundedRect.rect.bottom + mask.padding;
	auto PixelAligned = [](FLOAT value)
		{
			return isfinite(value) && value == roundf(value);
		};
	if (!PixelAligned(destinationLeft) || !PixelAligned(destinationTop)
		|| !PixelAligned(destinationRight) || !PixelAligned(destinationBottom))
		return Fallback(ExactFallback::Alignment);

	selection.destination = D2D1::RectF(
		roundf(destinationLeft), roundf(destinationTop),
		roundf(destinationRight), roundf(destinationBottom));
	const FLOAT width = selection.destination.right - selection.destination.left;
	const FLOAT height = selection.destination.bottom - selection.destination.top;
	constexpr FLOAT MaximumExactMaskDimension = 512.0F * 1024.0F;
	if (!isfinite(width) || !isfinite(height)
		|| width <= 0.0F || height <= 0.0F
		|| width > MaximumExactMaskDimension
		|| height > MaximumExactMaskDimension)
		return Fallback(ExactFallback::SizeOrBudget);

	FrameDiffuseExactMaskKeyClass key;
	key.width = static_cast<UINT32>(width);
	key.height = static_cast<UINT32>(height);
	key.radiusX = radiusX;
	key.radiusY = radiusY;
	constexpr std::uint64_t MaximumItemBytes = 512ULL * 1024ULL;
	const std::uint64_t logicalBytes =
		static_cast<std::uint64_t>(key.width) * key.height;
	const UINT32 maximumBitmapSize =
		frameMaskDeviceContext->GetMaximumBitmapSize();
	if (logicalBytes == 0 || logicalBytes > MaximumItemBytes
		|| key.width > maximumBitmapSize || key.height > maximumBitmapSize)
		return Fallback(ExactFallback::SizeOrBudget);

	for (auto& ready : mask.exactMasks)
	{
		if (!ready.bitmap || !ready.key.Matches(key)) continue;
		ready.lastUse = ++frameDiffuseExactMaskUseSerial;
		selection.cache = &ready;
		if (diagnostics) ++diagnostics->light.exactHit;
		return selection;
	}
	if (frameDiffuseMaskFrameSerial == 0) return Fallback(ExactFallback::Warming);

	FrameDiffuseExactMaskCandidateClass* candidate = nullptr;
	for (auto& current : mask.exactCandidates)
	{
		if (current.valid && current.key.Matches(key))
		{
			candidate = &current;
			break;
		}
	}
	if (!candidate)
	{
		for (auto& current : mask.exactCandidates)
		{
			if (!current.valid)
			{
				candidate = &current;
				break;
			}
		}
	}
	if (!candidate)
	{
		candidate = &mask.exactCandidates.front();
		for (auto& current : mask.exactCandidates)
			if (current.lastUse < candidate->lastUse) candidate = &current;
	}

	const unsigned long long useSerial = ++frameDiffuseExactMaskUseSerial;
	if (!candidate->valid || !candidate->key.Matches(key))
	{
		*candidate = {};
		candidate->key = key;
		candidate->valid = true;
		candidate->consecutiveFrames = 1;
		candidate->lastSeenFrame = frameDiffuseMaskFrameSerial;
	}
	else if (candidate->lastSeenFrame != frameDiffuseMaskFrameSerial)
	{
		if (candidate->lastSeenFrame + 1 == frameDiffuseMaskFrameSerial)
		{
			if (candidate->consecutiveFrames < 8)
				++candidate->consecutiveFrames;
		}
		else candidate->consecutiveFrames = 1;
		candidate->lastSeenFrame = frameDiffuseMaskFrameSerial;
	}
	candidate->lastUse = useSerial;
	if (candidate->consecutiveFrames < 8
		|| frameDiffuseMaskCreatedThisFrame
		|| frameDiffuseExactMaskPromotionFrameSerial
			== frameDiffuseMaskFrameSerial)
		return Fallback(ExactFallback::Warming);

	ComPtr<ID2D1Bitmap1> exactBitmap;
	HRESULT hr = CreateRoundedRectExactMask(mask, key, exactBitmap);
	if (FAILED(hr) || !exactBitmap)
	{
		// 可选整图失败仅关闭整图路径，父级预模糊九宫格继续承担本会话渲染。
		frameDiffuseExactMaskUnavailable = true;
		Inkeys::Log::TryFormat(Inkeys::Log::Level::Warn, Inkeys::Log::Module::UI3,
			"创建精确 A8 遮罩失败，回退预模糊九宫格, hr=0x{:08X}",
			static_cast<unsigned int>(hr));
		return Fallback(ExactFallback::CreateFailure);
	}

	auto EvictOldestFromParent = [&](FrameDiffuseMaskCacheClass& parent)
		{
			if (parent.exactMasks.empty()) return false;
			std::size_t oldestIndex = 0;
			for (std::size_t index = 1; index < parent.exactMasks.size(); ++index)
				if (parent.exactMasks[index].lastUse
					< parent.exactMasks[oldestIndex].lastUse)
					oldestIndex = index;
			parent.exactMasks.erase(parent.exactMasks.begin() + oldestIndex);
			return true;
		};
	while (mask.exactMasks.size() >= 6) EvictOldestFromParent(mask);

	constexpr std::size_t MaximumReadyMasks = 48;
	constexpr std::uint64_t MaximumReadyMaskBytes = 4ULL * 1024ULL * 1024ULL;
	for (;;)
	{
		std::size_t readyCount = 0;
		std::uint64_t readyBytes = 0;
		FrameDiffuseMaskCacheClass* oldestParent = nullptr;
		std::size_t oldestIndex = 0;
		for (auto& parent : frameDiffuseMaskCache)
		{
			for (std::size_t index = 0; index < parent.exactMasks.size(); ++index)
			{
				auto& ready = parent.exactMasks[index];
				++readyCount;
				readyBytes += ready.logicalBytes;
				if (!oldestParent
					|| ready.lastUse
						< oldestParent->exactMasks[oldestIndex].lastUse)
				{
					oldestParent = &parent;
					oldestIndex = index;
				}
			}
		}
		const bool countFits = readyCount < MaximumReadyMasks;
		const bool bytesFit = readyBytes
			<= MaximumReadyMaskBytes - logicalBytes;
		if (countFits && bytesFit) break;
		if (!oldestParent) return Fallback(ExactFallback::SizeOrBudget);
		oldestParent->exactMasks.erase(
			oldestParent->exactMasks.begin() + oldestIndex);
	}

	FrameDiffuseExactMaskCacheClass ready;
	ready.key = key;
	ready.lastUse = ++frameDiffuseExactMaskUseSerial;
	ready.logicalBytes = logicalBytes;
	ready.bitmap = move(exactBitmap);
	mask.exactMasks.emplace_back(move(ready));
	candidate->valid = false;
	frameDiffuseExactMaskPromotionFrameSerial = frameDiffuseMaskFrameSerial;
	frameDiffuseMaskCreatedThisFrame = true;
	selection.cache = &mask.exactMasks.back();
	return selection;
}

void UIRendering::DrawRoundedRectDiffuseMask(ID2D1DeviceContext* deviceContext,
	const FrameDiffuseMaskCacheClass& mask,
	const FrameDiffuseExactMaskSelectionClass& exactMask,
	const D2D1_ROUNDED_RECT& roundedRect,
	ID2D1RadialGradientBrush* brush, FLOAT opacity)
{
	if (!deviceContext || !mask.bitmap || !brush || opacity <= 0.0F) return;
	brush->SetOpacity(clamp(opacity, 0.0F, 1.0F));

	FLOAT destinationLeft = roundedRect.rect.left - mask.padding;
	FLOAT destinationTop = roundedRect.rect.top - mask.padding;
	FLOAT destinationRight = roundedRect.rect.right + mask.padding;
	FLOAT destinationBottom = roundedRect.rect.bottom + mask.padding;
	FLOAT destinationRadiusX = (std::max)(0.0F, roundedRect.radiusX);
	FLOAT destinationRadiusY = (std::max)(0.0F, roundedRect.radiusY);
	FLOAT destinationMiddleLeft = (std::min)(
		roundedRect.rect.left + destinationRadiusX,
		(roundedRect.rect.left + roundedRect.rect.right) * 0.5F);
	FLOAT destinationMiddleRight = (std::max)(
		roundedRect.rect.right - destinationRadiusX, destinationMiddleLeft);
	FLOAT destinationMiddleTop = (std::min)(
		roundedRect.rect.top + destinationRadiusY,
		(roundedRect.rect.top + roundedRect.rect.bottom) * 0.5F);
	FLOAT destinationMiddleBottom = (std::max)(
		roundedRect.rect.bottom - destinationRadiusY, destinationMiddleTop);

	D2D1_ANTIALIAS_MODE originalAntialiasMode = deviceContext->GetAntialiasMode();
	deviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	if (exactMask.cache && exactMask.cache->bitmap)
	{
		D2D1_RECT_F sourceRect = D2D1::RectF(
			0.0F, 0.0F,
			static_cast<FLOAT>(exactMask.cache->key.width),
			static_cast<FLOAT>(exactMask.cache->key.height));
		(ObserveNonSvgRectWrite(deviceContext, exactMask.destination), deviceContext)->FillOpacityMask(
			exactMask.cache->bitmap.Get(), brush,
			&exactMask.destination, &sourceRect);
		if (auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics())
			++diagnostics->light.slices;
		deviceContext->SetAntialiasMode(originalAntialiasMode);
		return;
	}
	bool geometryScaled = abs(destinationRadiusX - mask.radiusX) > 0.001F
		|| abs(destinationRadiusY - mask.radiusY) > 0.001F;
	if (!geometryScaled)
	{
		const FLOAT sourceX[] = { 0.0F,
			mask.padding + mask.radiusX,
			mask.padding + mask.radiusX + 1.0F, mask.size.width };
		const FLOAT sourceY[] = { 0.0F,
			mask.padding + mask.radiusY,
			mask.padding + mask.radiusY + 1.0F, mask.size.height };
		const FLOAT destinationX[] = { destinationLeft,
			destinationMiddleLeft, destinationMiddleRight, destinationRight };
		const FLOAT destinationY[] = { destinationTop,
			destinationMiddleTop, destinationMiddleBottom, destinationBottom };
		FillRoundedRectDiffuseMaskSlices(
			deviceContext, mask.bitmap.Get(), brush,
			sourceX, sourceY, destinationX, destinationY, 3);
	}
	else
	{
		// 动画缩放时单独保留 Gaussian 外扩段，避免柔光宽度随圆角一起压扁。
		const FLOAT sourceX[] = { 0.0F, mask.padding,
			mask.padding + mask.radiusX,
			mask.padding + mask.radiusX + 1.0F,
			mask.padding + mask.radiusX * 2.0F + 1.0F, mask.size.width };
		const FLOAT sourceY[] = { 0.0F, mask.padding,
			mask.padding + mask.radiusY,
			mask.padding + mask.radiusY + 1.0F,
			mask.padding + mask.radiusY * 2.0F + 1.0F, mask.size.height };
		const FLOAT destinationX[] = { destinationLeft, roundedRect.rect.left,
			destinationMiddleLeft, destinationMiddleRight,
			roundedRect.rect.right, destinationRight };
		const FLOAT destinationY[] = { destinationTop, roundedRect.rect.top,
			destinationMiddleTop, destinationMiddleBottom,
			roundedRect.rect.bottom, destinationBottom };
		FillRoundedRectDiffuseMaskSlices(
			deviceContext, mask.bitmap.Get(), brush,
			sourceX, sourceY, destinationX, destinationY, 5);
	}
	deviceContext->SetAntialiasMode(originalAntialiasMode);
}

UIRendering::FrameGeometryDiffuseMaskCacheClass* UIRendering::GetGeometryDiffuseMask(
	ID2D1DeviceContext* deviceContext, ID2D1Geometry* geometry,
	FLOAT strokeWidth, int geometryVariantQuarter)
{
	if (!deviceContext || !geometry || strokeWidth <= 0.0F)
		return nullptr;
	if (frameDiffuseMaskUnavailable) return nullptr;
	D2D1_RECT_F geometryBounds{};
	HRESULT hr = geometry->GetBounds(nullptr, &geometryBounds);
	if (FAILED(hr)) return nullptr;
	FLOAT width = geometryBounds.right - geometryBounds.left;
	FLOAT height = geometryBounds.bottom - geometryBounds.top;
	if (width <= 0.0F || height <= 0.0F) return nullptr;

	FLOAT standardDeviation = static_cast<FLOAT>(
		RenderingAttribute::pointLightDiffuseExtraWidth / 6.0
		* (frameDiffuseReferenceZoom > 0.0 ? frameDiffuseReferenceZoom : frameZoom));
	auto QuantizeQuarter = [](FLOAT value) -> int
		{
			return (std::max)(0, static_cast<int>(lround(static_cast<double>(value) * 4.0)));
		};
	int widthQuarter = (std::max)(1, QuantizeQuarter(width));
	int heightQuarter = (std::max)(1, QuantizeQuarter(height));
	int strokeWidthQuarter = (std::max)(1, QuantizeQuarter(strokeWidth));
	int standardDeviationQuarter = (std::max)(1, QuantizeQuarter(standardDeviation));
	auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics();
	for (auto& cache : frameGeometryDiffuseMaskCache)
	{
		if (cache.widthQuarter == widthQuarter
			&& cache.heightQuarter == heightQuarter
			&& cache.geometryVariantQuarter == geometryVariantQuarter
			&& cache.strokeWidthQuarter == strokeWidthQuarter
			&& cache.standardDeviationQuarter == standardDeviationQuarter)
		{
			if (diagnostics) ++diagnostics->light.geometryParentHit;
			return &cache;
		}
	}

	if (diagnostics) ++diagnostics->light.geometryParentMiss;
	ScopedMaskCreationTimer creationTimer(
		diagnostics ? &diagnostics->light.geometryParentCreateMs : nullptr);

	if (!frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		ComPtr<ID2D1Device> owningDevice;
		deviceContext->GetDevice(&owningDevice);
		hr = owningDevice
			? owningDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &frameMaskDeviceContext)
			: E_POINTER;
		if (FAILED(hr))
		{
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建遮罩缓存 DeviceContext 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameGaussianBlurEffect
		&& frameMaskDeviceContext && !frameDiffuseEffectFailureLogged)
	{
		hr = frameMaskDeviceContext->CreateEffect(
			CLSID_D2D1GaussianBlur, &frameGaussianBlurEffect);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
				D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
		if (SUCCEEDED(hr))
			hr = frameGaussianBlurEffect->SetValue(
				D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
		if (FAILED(hr))
		{
			frameGaussianBlurEffect.Reset();
			frameDiffuseEffectFailureLogged = true;
			frameDiffuseMaskUnavailable = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建 Gaussian Blur Effect 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameMaskDeviceContext || !frameGaussianBlurEffect)
	{
		if (diagnostics) ++diagnostics->light.geometryParentFailure;
		return nullptr;
	}

	FrameGeometryDiffuseMaskCacheClass cache;
	cache.widthQuarter = widthQuarter;
	cache.heightQuarter = heightQuarter;
	cache.geometryVariantQuarter = geometryVariantQuarter;
	cache.strokeWidthQuarter = strokeWidthQuarter;
	cache.standardDeviationQuarter = standardDeviationQuarter;
	FLOAT cachedStrokeWidth = static_cast<FLOAT>(strokeWidthQuarter) / 4.0F;
	FLOAT cachedStandardDeviation =
		static_cast<FLOAT>(standardDeviationQuarter) / 4.0F;
	cache.padding = ceilf(cachedStandardDeviation * 3.0F
		+ cachedStrokeWidth * 0.5F + 1.0F);
	cache.size = D2D1::SizeF(
		width + cache.padding * 2.0F,
		height + cache.padding * 2.0F);

	D2D1_SIZE_U pixelSize = D2D1::SizeU(
		(std::max<UINT32>)(1, static_cast<UINT32>(ceilf(cache.size.width))),
		(std::max<UINT32>)(1, static_cast<UINT32>(ceilf(cache.size.height))));
	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(
			DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		96.0F, 96.0F);
	ComPtr<ID2D1Bitmap1> sourceBitmap;
	ComPtr<ID2D1Bitmap1> outputBitmap;
	hr = frameMaskDeviceContext->CreateBitmap(
		pixelSize, nullptr, 0, bitmapProperties, &sourceBitmap);
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateBitmap(
			pixelSize, nullptr, 0, bitmapProperties, &outputBitmap);
	// 与精确遮罩烘焙共用恒白画刷，随当前设备资源统一释放，避免每次 miss 重建。
	if (SUCCEEDED(hr) && !frameDiffuseExactMaskBrush)
		hr = frameMaskDeviceContext->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White), &frameDiffuseExactMaskBrush);
	if (SUCCEEDED(hr))
		hr = frameGaussianBlurEffect->SetValue(
			D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
			cachedStandardDeviation);

	if (SUCCEEDED(hr))
	{
		D2D1_COLOR_F transparent = D2D1::ColorF(0.0F, 0.0F);

		// 缓存上下文分两次提交，避免同一 BeginDraw 内把刚写完的 Target 当作 Effect 输入。
		frameMaskDeviceContext->SetTarget(sourceBitmap.Get());
		frameMaskDeviceContext->BeginDraw();
		frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Translation(
			cache.padding - geometryBounds.left,
			cache.padding - geometryBounds.top));
		frameMaskDeviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
		frameMaskDeviceContext->Clear(&transparent);
		frameMaskDeviceContext->DrawGeometry(
			geometry, frameDiffuseExactMaskBrush.Get(), cachedStrokeWidth);
		hr = frameMaskDeviceContext->EndDraw();
		frameMaskDeviceContext->SetTarget(nullptr);

		if (SUCCEEDED(hr))
		{
			frameGaussianBlurEffect->SetInput(0, sourceBitmap.Get());
			frameMaskDeviceContext->SetTarget(outputBitmap.Get());
			frameMaskDeviceContext->BeginDraw();
			frameMaskDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
			frameMaskDeviceContext->Clear(&transparent);
			frameMaskDeviceContext->DrawImage(frameGaussianBlurEffect.Get());
			hr = frameMaskDeviceContext->EndDraw();
			frameMaskDeviceContext->SetTarget(nullptr);
			frameGaussianBlurEffect->SetInput(0, nullptr);
		}
	}
	if (FAILED(hr))
	{
		if (diagnostics) ++diagnostics->light.geometryParentFailure;
		frameDiffuseMaskUnavailable = true;
		if (!frameDiffuseMaskFailureLogged)
		{
			frameDiffuseMaskFailureLogged = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建几何预模糊遮罩失败，本设备停用柔光遮罩, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	cache.bitmap = move(outputBitmap);
	if (frameGeometryDiffuseMaskCache.size() >= 24)
		frameGeometryDiffuseMaskCache.erase(frameGeometryDiffuseMaskCache.begin());
	frameGeometryDiffuseMaskCache.emplace_back(move(cache));
	frameDiffuseMaskCreatedThisFrame = true;
	if (diagnostics) ++diagnostics->light.geometryParentCreate;
	return &frameGeometryDiffuseMaskCache.back();
}

void UIRendering::DrawGeometryDiffuseMask(ID2D1DeviceContext* deviceContext,
	const FrameGeometryDiffuseMaskCacheClass& mask,
	const D2D1_RECT_F& geometryBounds,
	ID2D1RadialGradientBrush* brush, FLOAT opacity)
{
	if (!deviceContext || !mask.bitmap || !brush || opacity <= 0.0F) return;
	D2D1_RECT_F destinationRect = D2D1::RectF(
		geometryBounds.left - mask.padding,
		geometryBounds.top - mask.padding,
		geometryBounds.right + mask.padding,
		geometryBounds.bottom + mask.padding);
	D2D1_RECT_F sourceRect = D2D1::RectF(
		0.0F, 0.0F, mask.size.width, mask.size.height);
	brush->SetOpacity(clamp(opacity, 0.0F, 1.0F));
	D2D1_ANTIALIAS_MODE originalAntialiasMode = deviceContext->GetAntialiasMode();
	deviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	(ObserveNonSvgRectWrite(deviceContext, destinationRect), deviceContext)->FillOpacityMask(
		mask.bitmap.Get(), brush, &destinationRect, &sourceRect);
	if (auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics())
		++diagnostics->light.slices;
	deviceContext->SetAntialiasMode(originalAntialiasMode);
}

bool UIRendering::DrawPointLightFrame(ID2D1DeviceContext* deviceContext, COLORREF color,
	const ThemeMaterial::Material& material,
	UiFrameLightColorEnum frameLightColor,
	bool primaryLightEnabled, double cursorLightIntensityScale,
	double lightCursorLightIntensityScale,
	double baseFramePct, double lightPct, FLOAT strokeWidth,
	const D2D1_ROUNDED_RECT* roundedRect,
	ID2D1Geometry* geometry, int geometryVariantQuarter)
{
	if (!deviceContext || (!roundedRect && !geometry) || strokeWidth <= 0.0F)
		return false;

	FLOAT baseOpacity = static_cast<FLOAT>(clamp(baseFramePct, 0.0, 1.0));
	const FLOAT sourceLightOpacity = static_cast<FLOAT>(
		clamp(lightPct, 0.0, 1.0));
	if (baseOpacity <= 0.0F && sourceLightOpacity <= 0.0F) return true;

	bool useDrawingLightTransition =
		frameLightColor == UiFrameLightColorEnum::PenWhenDrawing;
	double penColorBlend = useDrawingLightTransition
		? clamp(frameDrawingPenColorBlend, 0.0, 1.0) : 0.0;
	// 边缘光基色取本对象实际绘制的边框色：选中按钮为主题青；中性边框在浅色端
	// 过渡到 EdgeLight 深灰（8% 黑的边框本身作光色不可见），绘制时再向笔色过渡。
	auto lightingMaterial = material;
	lightingMaterial.edgeLight = ThemeMaterial::ObjectEdgeLight(material.lightWeight, color);
	const auto primaryLighting = ThemeMaterial::ResolveLighting(
		lightingMaterial, frameDrawingPenColor, penColorBlend);
	const auto cursorLighting = ThemeMaterial::ResolveCursorLighting(
		lightingMaterial, frameDrawingPenColor, penColorBlend);
	FLOAT primaryLightOpacity = sourceLightOpacity;
	FLOAT cursorLightOpacity = sourceLightOpacity;
	if (useDrawingLightTransition)
	{
		const double drawingOpacity = clamp(frameDrawingLightOpacity, 0.0, 1.0);
		primaryLightOpacity *= static_cast<FLOAT>(drawingOpacity);
		cursorLightOpacity *= static_cast<FLOAT>(drawingOpacity);
	}
	primaryLightOpacity *= static_cast<FLOAT>(primaryLighting.intensity);
	cursorLightOpacity *= static_cast<FLOAT>(cursorLighting.intensity);
	ComPtr<ID2D1RadialGradientBrush> primaryBrush;
	ComPtr<ID2D1RadialGradientBrush> cursorBrush;
	const double lightCursorScale = lightCursorLightIntensityScale >= 0.0
		? clamp(lightCursorLightIntensityScale, 0.0, 1.0)
		: clamp(cursorLightIntensityScale, 0.0, 1.0);
	const double objectCursorScale = ThemeMaterial::Mix(
		clamp(cursorLightIntensityScale, 0.0, 1.0), lightCursorScale,
		material.lightWeight);
	FLOAT cursorLightIntensity = frameCursorLightIntensity
		* static_cast<FLOAT>(objectCursorScale);
	bool edgeLightingEnabled = frameEdgeLightingAllowed && frameEdgeLightingEnabled;
	D2D1_RECT_F lightBounds{};
	if (roundedRect) lightBounds = roundedRect->rect;
	else if (geometry && FAILED(geometry->GetBounds(nullptr, &lightBounds)))
		return false;
	FLOAT lightBoundsOutset = strokeWidth
		+ static_cast<FLOAT>(
			RenderingAttribute::pointLightDiffuseExtraWidth
			* frameZoom);
	lightBounds.left -= lightBoundsOutset;
	lightBounds.top -= lightBoundsOutset;
	lightBounds.right += lightBoundsOutset;
	lightBounds.bottom += lightBoundsOutset;
	auto LightIntersectsBounds = [&](D2D1_POINT_2F point,
		FLOAT radiusX, FLOAT radiusY) -> bool
		{
			if (radiusX <= 0.0F || radiusY <= 0.0F) return false;
			FLOAT nearestX = clamp(point.x, lightBounds.left, lightBounds.right);
			FLOAT nearestY = clamp(point.y, lightBounds.top, lightBounds.bottom);
			FLOAT deltaX = (point.x - nearestX) / radiusX;
			FLOAT deltaY = (point.y - nearestY) / radiusY;
			return deltaX * deltaX + deltaY * deltaY <= 1.0F;
		};
	// 半径可以为游标光和基础边框保留，主光可见性必须独立遵守共享快照。
	bool drawPrimaryLight = edgeLightingEnabled && framePrimaryLightAnchorInitialized
		&& primaryLightOpacity > 0.0F && primaryLightEnabled
		&& LightIntersectsBounds(
			framePrimaryLight,
			frameLightRadius * static_cast<FLOAT>(material.primaryLightRadiusScale),
			frameLightRadius * static_cast<FLOAT>(material.primaryLightRadiusScale));
	bool drawCursorLight = edgeLightingEnabled
		&& cursorLightOpacity > 0.0F && frameCursorLightVisible
		&& cursorLightIntensity > 0.0F
		&& LightIntersectsBounds(frameLocalCursorLight,
			frameLocalCursorLightRadiusX
				* static_cast<FLOAT>(material.cursorLightRadiusScale),
			frameLocalCursorLightRadiusY
				* static_cast<FLOAT>(material.cursorLightRadiusScale));
	if (drawPrimaryLight)
		primaryBrush = GetFrameGradientBrush(
			deviceContext, primaryLighting.color,
			BorderLightSourceEnum::Primary,
			static_cast<FLOAT>(material.primaryLightRadiusScale));
	if (drawCursorLight)
		cursorBrush = GetFrameGradientBrush(
			deviceContext, cursorLighting.color,
			BorderLightSourceEnum::Cursor,
			static_cast<FLOAT>(material.cursorLightRadiusScale));
	if ((drawPrimaryLight && !primaryBrush) || (drawCursorLight && !cursorBrush)) return false;

	ID2D1SolidColorBrush* baseFrameBrush = nullptr;
	if (baseOpacity > 0.0F)
	{
		baseFrameBrush = GetFrameSolidColorBrush(deviceContext, color, baseOpacity);
		if (!baseFrameBrush) return false;
	}

	auto DrawLightPass = [&](ID2D1RadialGradientBrush* brush,
		FLOAT opacity, FLOAT intensity, FLOAT width)
		{
			if (!brush || intensity <= 0.0F) return;
			brush->SetOpacity(clamp(opacity * intensity, 0.0F, 1.0F));
			if (roundedRect) (ObserveNonSvgRectWrite(deviceContext, roundedRect->rect, width * 0.5F), deviceContext)->DrawRoundedRectangle(roundedRect, brush, width);
			else (ObserveNonSvgGeometryWrite(deviceContext, geometry, width), deviceContext)->DrawGeometry(geometry, brush, width);
		};
	deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	// 点光范围之外仍完整保留原边框，光源只在基础灰边上增加强调。
	if (baseFrameBrush)
	{
		if (roundedRect) (ObserveNonSvgRectWrite(deviceContext, roundedRect->rect, strokeWidth * 0.5F), deviceContext)->DrawRoundedRectangle(roundedRect, baseFrameBrush, strokeWidth);
		else (ObserveNonSvgGeometryWrite(deviceContext, geometry, strokeWidth), deviceContext)->DrawGeometry(geometry, baseFrameBrush, strokeWidth);
	}

	if (drawPrimaryLight || drawCursorLight)
	{
		const FLOAT primaryDiffuseSourceOpacity = static_cast<FLOAT>(clamp(
			primaryLighting.diffuseOpacity / BorderGaussianCenterCoverage,
			0.0, 1.0));
		const FLOAT cursorDiffuseSourceOpacity = static_cast<FLOAT>(clamp(
			cursorLighting.diffuseOpacity / BorderGaussianCenterCoverage,
			0.0, 1.0));
		auto CompositeOpacity = [](FLOAT opacity) -> FLOAT
			{
				opacity = clamp(opacity, 0.0F, 1.0F);
				const FLOAT inverseOpacity = 1.0F - opacity;
				return 1.0F - inverseOpacity * inverseOpacity;
			};
		if (roundedRect)
		{
			D2D1_ROUNDED_RECT maskRoundedRect = *roundedRect;
			FLOAT maskStrokeWidth = strokeWidth;
			if (isfinite(frameDiffuseMaskGeometryScale)
				&& frameDiffuseMaskGeometryScale > 0.0)
			{
				// 等比动画只改变落点九宫格，缓存仍使用稳定的完整圆角和描边。
				maskRoundedRect.radiusX *=
					static_cast<FLOAT>(frameDiffuseMaskGeometryScale);
				maskRoundedRect.radiusY *=
					static_cast<FLOAT>(frameDiffuseMaskGeometryScale);
				maskStrokeWidth *=
					static_cast<FLOAT>(frameDiffuseMaskGeometryScale);
			}
			FrameDiffuseMaskCacheClass* diffuseMask =
				GetRoundedRectDiffuseMask(
					deviceContext, maskRoundedRect, maskStrokeWidth);
			if (diffuseMask)
			{
				FrameDiffuseExactMaskSelectionClass exactMask =
					ResolveRoundedRectExactMask(
						deviceContext, *diffuseMask, *roundedRect);
				// 模糊后的几何遮罩跨帧复用，光源颜色和位置仍由实时径向画刷决定。
				if (drawPrimaryLight)
				{
					DrawRoundedRectDiffuseMask(deviceContext, *diffuseMask,
						exactMask, *roundedRect, primaryBrush.Get(),
						CompositeOpacity(primaryLightOpacity
							* primaryDiffuseSourceOpacity));
				}
				if (drawCursorLight)
				{
					DrawRoundedRectDiffuseMask(deviceContext, *diffuseMask,
						exactMask, *roundedRect, cursorBrush.Get(),
						CompositeOpacity(cursorLightOpacity * cursorLightIntensity
							* cursorDiffuseSourceOpacity));
				}
			}
		}
		else if (geometry)
		{
			D2D1_RECT_F geometryBounds{};
			if (SUCCEEDED(geometry->GetBounds(nullptr, &geometryBounds)))
			{
				FrameGeometryDiffuseMaskCacheClass* diffuseMask =
					GetGeometryDiffuseMask(deviceContext, geometry,
						strokeWidth, geometryVariantQuarter);
				if (diffuseMask)
				{
					if (drawPrimaryLight)
					{
						DrawGeometryDiffuseMask(deviceContext, *diffuseMask,
							geometryBounds, primaryBrush.Get(),
							CompositeOpacity(primaryLightOpacity
								* primaryDiffuseSourceOpacity));
					}
					if (drawCursorLight)
					{
						DrawGeometryDiffuseMask(deviceContext, *diffuseMask,
							geometryBounds, cursorBrush.Get(),
							CompositeOpacity(cursorLightOpacity * cursorLightIntensity
								* cursorDiffuseSourceOpacity));
					}
				}

			}
		}

	}
	DrawLightPass(primaryBrush.Get(), primaryLightOpacity,
		static_cast<FLOAT>(BorderLightIntensity), strokeWidth);
	DrawLightPass(cursorBrush.Get(), cursorLightOpacity,
		cursorLightIntensity, strokeWidth);
	return true;
}

void UIRendering::DrawSurfaceMaterialShadows(ID2D1DeviceContext* deviceContext,
	const ThemeMaterial::Material& material, double opacity,
	const D2D1_ROUNDED_RECT* roundedRect, ID2D1Geometry* geometry)
{
	if (!deviceContext || (!roundedRect && !geometry) || opacity <= 0.0)
		return;
	if (material.keyShadowOpacity <= 0.0 && material.ambientShadowOpacity <= 0.0)
		return;
	D2D1_MATRIX_3X2_F previousTransform;
	deviceContext->GetTransform(&previousTransform);
	deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	auto DrawShadow = [&](COLORREF color, double shadowOpacity,
		double radiusDip, double offsetYDip)
		{
			if (shadowOpacity <= 0.0 || radiusDip <= 0.0) return;
			const double alpha = ThemeMaterial::ClampWeight(opacity * shadowOpacity);
			const FLOAT offsetY = static_cast<FLOAT>(offsetYDip * frameZoom);
			deviceContext->SetTransform(
				D2D1::Matrix3x2F::Translation(0.0F, offsetY) * previousTransform);
			// 有限的轮廓带近似柔和衰减，不缓存动画帧，也不创建逐帧 Gaussian。
			constexpr int samples = 8;
			double previousCoverage = 0.0;
			for (int sample = samples; sample > 0; --sample)
			{
				const double falloff = 1.0 - static_cast<double>(sample - 1) / samples;
				const double coverage = alpha * falloff * falloff;
				const double bandOpacity = (coverage - previousCoverage)
					/ (1.0 - previousCoverage);
				auto brush = GetFrameSolidColorBrush(deviceContext, color, bandOpacity);
				if (!brush) break;
				const FLOAT width = static_cast<FLOAT>(
					2.0 * radiusDip * frameZoom * sample / samples);
				if (roundedRect) deviceContext->DrawRoundedRectangle(roundedRect, brush, width);
				else deviceContext->DrawGeometry(geometry, brush, width);
				previousCoverage = coverage;
			}
		};
	DrawShadow(material.ambientShadowColor, material.ambientShadowOpacity,
		material.ambientShadowRadiusDip, material.ambientShadowOffsetYDip);
	DrawShadow(material.keyShadowColor, material.keyShadowOpacity,
		material.keyShadowRadiusDip, material.keyShadowOffsetYDip);
	deviceContext->SetTransform(previousTransform);
}

void UIRendering::DrawSurfaceMaterialHighlight(ID2D1DeviceContext* deviceContext,
	const ThemeMaterial::Material& material, double opacity,
	const D2D1_ROUNDED_RECT* roundedRect, ID2D1Geometry* geometry)
{
	if (!deviceContext || (!roundedRect && !geometry)
		|| material.highlightOpacity <= 0.0 || opacity <= 0.0
		|| surfaceHighlightUnavailable)
		return;
	D2D1_RECT_F bounds{};
	if (roundedRect) bounds = roundedRect->rect;
	else if (FAILED(geometry->GetBounds(nullptr, &bounds))) return;
	if (!surfaceHighlightBrush)
	{
		const D2D1_GRADIENT_STOP stops[] =
		{
			{ 0.0F, Inkeys::Color::ConvertToD2dColor(material.highlightColor, 1.0) },
			{ 1.0F, Inkeys::Color::ConvertToD2dColor(material.highlightColor, 0.0) },
		};
		ComPtr<ID2D1GradientStopCollection> collection;
		HRESULT hr = deviceContext->CreateGradientStopCollection(
			stops, ARRAYSIZE(stops), D2D1_GAMMA_2_2,
			D2D1_EXTEND_MODE_CLAMP, &collection);
		if (SUCCEEDED(hr))
			hr = deviceContext->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(
					D2D1::Point2F(), D2D1::Point2F(0.0F, 1.0F)),
				collection.Get(), &surfaceHighlightBrush);
		if (FAILED(hr))
		{
			surfaceHighlightUnavailable = true;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"创建顶部高光失败，本设备停用高光, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
			return;
		}
	}
	// 只有靠近顶部的轮廓接收高光，底部不会形成一圈白色 glow。
	surfaceHighlightBrush->SetStartPoint(D2D1::Point2F(bounds.left, bounds.top));
	surfaceHighlightBrush->SetEndPoint(D2D1::Point2F(bounds.left,
		bounds.top + (std::min)(static_cast<FLOAT>(24.0 * frameZoom),
			(bounds.bottom - bounds.top) * 0.5F)));
	surfaceHighlightBrush->SetOpacity(static_cast<FLOAT>(
		ThemeMaterial::ClampWeight(opacity * material.highlightOpacity)));
	const FLOAT width = static_cast<FLOAT>(frameZoom);
	if (roundedRect) deviceContext->DrawRoundedRectangle(roundedRect, surfaceHighlightBrush.Get(), width);
	else deviceContext->DrawGeometry(geometry, surfaceHighlightBrush.Get(), width);
}

bool UIRendering::Shape(ID2D1DeviceContext* deviceContext, const UiShapeClass& shape, const UiInheritClass& inh, RECT* targetRect, bool clip)
{
	// 判断是否启用
	if (shape.enable.val == false) return false;
	if (!shape.fill.has_value() && !shape.frame.has_value()) return false;
	if (frameZoom <= 0.0) return false;
	if (shape.w.val <= 0 || shape.h.val <= 0) return false;
	double frameLightPct = shape.frameLightPct.has_value()
		? clamp(static_cast<double>(shape.frameLightPct.value().val), 0.0, 1.0) : 0.0;
	if (shape.pct.val <= 0.0 && frameLightPct <= 0.0) return false;

	// 初始化绘制量
	double tarX = inh.x; // 绘制左上角 x
	double tarY = inh.y; // 绘制左上角 y
	double tarW = shape.w.val;
	double tarH = shape.h.val;
	double tarPct = shape.pct.val; // 透明度
	const auto material = ThemeMaterial::Resolve(shape.lightMaterial,
		shape.fill.has_value() ? static_cast<COLORREF>(shape.fill.value().val) : RGB(0, 0, 0),
		shape.frame.has_value() ? static_cast<COLORREF>(shape.frame.value().val) : RGB(255, 255, 255));

	double tarRw = 0.0;
	double tarRh = 0.0;
	if (shape.rw.has_value()) tarRw = shape.rw.value().val;
	if (shape.rh.has_value()) tarRh = shape.rh.value().val;

	FLOAT tarZoom = static_cast<FLOAT>(frameZoom);
	D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(static_cast<FLOAT>(tarX) * tarZoom, static_cast<FLOAT>(tarY) * tarZoom, static_cast<FLOAT>(tarX + tarW) * tarZoom, static_cast<FLOAT>(tarY + tarH) * tarZoom), static_cast<FLOAT>(tarRw) * tarZoom, static_cast<FLOAT>(tarRh) * tarZoom);

	if (!ShouldDrawFrameBounds(deviceContext, roundedRect.rect, GetFrameShapeDrawOutset(shape)))
	{
		if (targetRect) RenderingAttribute::UnionRectInPlace(*targetRect, RenderingAttribute::GetWeigetRect(shape, tarZoom));
		return false;
	}

	// Clip
	if (clip)
	{
		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, RGB(0, 0, 0), 0.0);
		if (!fillBrush) return false;
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		(ObserveNonSvgRectWrite(deviceContext, roundedRect.rect), deviceContext)->FillRoundedRectangle(&roundedRect, fillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}
	// 渲染到 DC
	{
		if (shape.themeSurface)
			DrawSurfaceMaterialShadows(deviceContext, material, tarPct, &roundedRect, nullptr);
		// 渲染填充
		if (shape.fill.has_value() && tarPct > 0.0)
		{
			COLORREF fill = shape.themeSurface ? material.surface
				: static_cast<COLORREF>(shape.fill.value().val);
			double fillPct = tarPct * (shape.themeSurface ? material.fillOpacityScale
				* (shape.residentSurface
					? ThemeMaterial::ResidentFillOpacityScale(material.lightWeight,
						shape.lightFillOpacityScale, GetThemeResidentBackgroundOpacityOverride())
					: ThemeMaterial::Mix(1.0, shape.lightFillOpacityScale, material.lightWeight)) : 1.0);
			if (shape.residentSurface)
			{
				// 主栏 pct 的 0↔0.8 是收展显隐，先限制正常材质再组合，不能把淡出前半段钳在30%。
				const double visibility = shape.themeSurface ? tarPct / ThemeMaterial::ResidentBaseFillOpacity : 1.0;
				const double normal = shape.themeSurface ? ThemeMaterial::ResidentBaseFillOpacity
					* material.fillOpacityScale * ThemeMaterial::ResidentFillOpacityScale(material.lightWeight,
						shape.lightFillOpacityScale, GetThemeResidentBackgroundOpacityOverride()) : fillPct;
				fillPct = ThemeMaterial::ResolveIdleOpacity(normal,
					ThemeMaterial::IdleBackgroundOpacity, residentIdleFade) * visibility;
			}
			ID2D1SolidColorBrush* fillBrush =
				GetFrameSolidColorBrush(deviceContext, fill, fillPct);
			if (!fillBrush) return false;
			(ObserveNonSvgRectWrite(deviceContext, roundedRect.rect), deviceContext)->FillRoundedRectangle(&roundedRect, fillBrush);
		}
		// 渲染边框
		if (shape.frame.has_value())
		{
			COLORREF frame = shape.themeSurface ? material.surfaceFrame
				: static_cast<COLORREF>(shape.frame.value().val);
			double tarFramePct = tarPct;
			if (shape.framePct.has_value()) tarFramePct = shape.framePct.value().val;
			double tarFrameLightPct = shape.frameLightPct.has_value()
				? frameLightPct : tarFramePct;
			if (!shape.frameLightPct.has_value()
				&& shape.frameLightOpacitySource == UiFrameLightOpacitySourceEnum::ObjectPct)
				tarFrameLightPct = tarPct;
			if (shape.themeSurface) tarFramePct *= material.frameOpacityScale;

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			bool shouldDraw = true;
			if (shape.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(shape.ft.value().val * tarZoom);
				shouldDraw = strokeWidth > 0.0F;
			}
			if (shouldDraw)
			{
				bool pointLightDrawn = shape.frameRendering == UiFrameRenderingEnum::PointLight
					&& DrawPointLightFrame(deviceContext, frame, material, shape.frameLightColor,
						shape.framePrimaryLightEnabled, shape.frameCursorLightIntensityScale,
						shape.frameLightCursorLightIntensityScale,
						tarFramePct, tarFrameLightPct,
						strokeWidth, &roundedRect, nullptr);
				if (!pointLightDrawn)
				{
					ID2D1SolidColorBrush* borderBrush =
						GetFrameSolidColorBrush(deviceContext, frame, tarFramePct);
					if (!borderBrush) return false;
					(ObserveNonSvgRectWrite(deviceContext, roundedRect.rect, strokeWidth * 0.5F), deviceContext)->DrawRoundedRectangle(
						&roundedRect, borderBrush, strokeWidth);
				}
			}
		}
		if (shape.themeSurface)
			DrawSurfaceMaterialHighlight(deviceContext, material, tarPct, &roundedRect, nullptr);
	}

	if (targetRect) RenderingAttribute::UnionRectInPlace(*targetRect, RenderingAttribute::GetWeigetRect(shape, tarZoom));
	return true;
}
ID2D1Geometry* UIRendering::GetSuperellipseGeometry(
	FLOAT x, FLOAT y, FLOAT width, FLOAT height, FLOAT n, int segments)
{
	auto factory = SharedD2DFactory();
	if (!factory || width <= 0.0F || height <= 0.0F || n <= 0.0F)
		return nullptr;

	bool pathChanged = !superellipseGeometryCache.localGeometry
		|| superellipseGeometryCache.width != width
		|| superellipseGeometryCache.height != height
		|| superellipseGeometryCache.n != n
		|| superellipseGeometryCache.segments != segments;
	if (pathChanged)
	{
		ComPtr<ID2D1PathGeometry> nextLocalGeometry;
		const HRESULT hr = Inkeys::UI::UI3::CreateSuperellipsePath(factory.Get(),
			width, height, n, segments, &nextLocalGeometry);
		if (FAILED(hr) || !nextLocalGeometry) return nullptr;

		superellipseGeometryCache.width = width;
		superellipseGeometryCache.height = height;
		superellipseGeometryCache.n = n;
		superellipseGeometryCache.segments = segments;
		superellipseGeometryCache.localGeometry = move(nextLocalGeometry);
		superellipseGeometryCache.translatedGeometry.Reset();
	}

	bool translationChanged = !superellipseGeometryCache.translatedGeometry
		|| superellipseGeometryCache.translatedX != x
		|| superellipseGeometryCache.translatedY != y;
	if (translationChanged)
	{
		D2D1_MATRIX_3X2_F translation = D2D1::Matrix3x2F::Translation(x, y);
		ComPtr<ID2D1TransformedGeometry> nextTranslatedGeometry;
		HRESULT hr = factory->CreateTransformedGeometry(
			superellipseGeometryCache.localGeometry.Get(), &translation,
			&nextTranslatedGeometry);
		if (FAILED(hr) || !nextTranslatedGeometry) return nullptr;
		superellipseGeometryCache.translatedX = x;
		superellipseGeometryCache.translatedY = y;
		superellipseGeometryCache.translatedGeometry = move(nextTranslatedGeometry);
	}

	return superellipseGeometryCache.translatedGeometry.Get();
}
bool UIRendering::Superellipse(ID2D1DeviceContext* deviceContext, const UiSuperellipseClass& superellipse, const UiInheritClass& inh, RECT* targetRect, bool clip)
{
	// 判断是否启用
	if (superellipse.enable.val == false) return false;
	if (!superellipse.fill.has_value() && !superellipse.frame.has_value()) return false;
	if (frameZoom <= 0.0) return false;
	if (superellipse.w.val <= 0 || superellipse.h.val <= 0) return false;
	if (superellipse.pct.val <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = frameZoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = superellipse.w.val * tarZoom;
	double tarH = superellipse.h.val * tarZoom;
	double tarPct = superellipse.pct.val; // 透明度
	const auto material = ThemeMaterial::Resolve(superellipse.lightMaterial,
		superellipse.fill.has_value() ? static_cast<COLORREF>(superellipse.fill.value().val) : RGB(0, 0, 0),
		superellipse.frame.has_value() ? static_cast<COLORREF>(superellipse.frame.value().val) : RGB(255, 255, 255));

	double tarN = 4.0;
	if (superellipse.n.has_value()) tarN = superellipse.n.value().val;

	const auto drawBounds = D2D1::RectF(static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY),
		static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH));
	const double outset = superellipse.frame ? FrameBorderDrawOutset(
		superellipse.ft ? static_cast<double>(superellipse.ft->val) : 4.0,
		superellipse.frameRendering, frameZoom, frameDiffuseReferenceZoom, frameDiffuseMaskGeometryScale)
		: RenderingAttribute::dirtyAntialiasPadding;
	if (!ShouldDrawFrameBounds(deviceContext, drawBounds, outset))
	{
		if (targetRect) RenderingAttribute::UnionRectInPlace(*targetRect, RenderingAttribute::GetWeigetRect(superellipse, tarZoom));
		return false;
	}

	// 路径只由尺寸、n 与采样精度决定，面板移动时复用局部路径。
	int segs = clamp(static_cast<int>((tarW + tarH) / 8.0), 24, 128);
	ID2D1Geometry* geometry = GetSuperellipseGeometry(
		static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY),
		static_cast<FLOAT>(tarW), static_cast<FLOAT>(tarH),
		static_cast<FLOAT>(tarN), segs);
	if (!geometry) return false;

	// Clip
	if (clip)
	{
		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, RGB(0, 0, 0), 0.0);
		if (!fillBrush) return false;
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		(ObserveNonSvgGeometryWrite(deviceContext, geometry), deviceContext)->FillGeometry(geometry, fillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}

	// 渲染到 DC
	{
		if (superellipse.themeSurface)
			DrawSurfaceMaterialShadows(deviceContext, material, tarPct, nullptr, geometry);
		// 渲染填充
		if (superellipse.fill.has_value())
		{
			COLORREF fill = superellipse.themeSurface ? material.surface
				: static_cast<COLORREF>(superellipse.fill.value().val);
			double fillPct = tarPct * (superellipse.themeSurface ? material.fillOpacityScale
				* (superellipse.residentSurface
					? ThemeMaterial::ResidentFillOpacityScale(material.lightWeight,
						superellipse.lightFillOpacityScale, GetThemeResidentBackgroundOpacityOverride())
					: ThemeMaterial::Mix(1.0, superellipse.lightFillOpacityScale, material.lightWeight)) : 1.0);
			if (superellipse.residentSurface)
				fillPct = ThemeMaterial::ResolveIdleOpacity(fillPct,
					ThemeMaterial::IdleBackgroundOpacity, residentIdleFade);
			ID2D1SolidColorBrush* fillBrush =
				GetFrameSolidColorBrush(deviceContext, fill, fillPct);
			if (!fillBrush) return false;
			(ObserveNonSvgGeometryWrite(deviceContext, geometry), deviceContext)->FillGeometry(geometry, fillBrush);
		}
		// 渲染边框
		if (superellipse.frame.has_value())
		{
			COLORREF frame = superellipse.themeSurface ? material.surfaceFrame
				: static_cast<COLORREF>(superellipse.frame.value().val);
			double tarFramePct = tarPct;
			if (superellipse.framePct.has_value()) tarFramePct = superellipse.framePct.value().val;
			double tarFrameLightPct = tarFramePct;
			if (superellipse.frameLightOpacitySource == UiFrameLightOpacitySourceEnum::ObjectPct)
				tarFrameLightPct = tarPct;
			if (superellipse.themeSurface) tarFramePct *= material.frameOpacityScale;

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			bool shouldDraw = true;
			if (superellipse.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(superellipse.ft.value().val * tarZoom);
				shouldDraw = strokeWidth > 0.0F;
			}
			if (shouldDraw)
			{
				bool pointLightDrawn = superellipse.frameRendering == UiFrameRenderingEnum::PointLight
					&& DrawPointLightFrame(deviceContext, frame, material, superellipse.frameLightColor,
						superellipse.framePrimaryLightEnabled,
						superellipse.frameCursorLightIntensityScale,
						superellipse.frameLightCursorLightIntensityScale,
						tarFramePct, tarFrameLightPct,
						strokeWidth, nullptr, geometry,
						static_cast<int>(lround(tarN * 4.0)));
				if (!pointLightDrawn)
				{
					ID2D1SolidColorBrush* borderBrush =
						GetFrameSolidColorBrush(deviceContext, frame, tarFramePct);
					if (!borderBrush) return false;
					(ObserveNonSvgGeometryWrite(deviceContext, geometry, strokeWidth), deviceContext)->DrawGeometry(geometry, borderBrush, strokeWidth);
				}
			}
		}
		if (superellipse.themeSurface)
			DrawSurfaceMaterialHighlight(deviceContext, material, tarPct, nullptr, geometry);
	}

	if (targetRect) RenderingAttribute::UnionRectInPlace(*targetRect, RenderingAttribute::GetWeigetRect(superellipse, tarZoom));
	return true;
}
bool UIRendering::Svg(ID2D1DeviceContext* deviceContext, UiSVGClass& svg, const UiInheritClass& inh)
{
	auto* observationScope = Inkeys::UI::UI3::CurrentUi3SvgScope();
	auto RejectObserved = [&](Inkeys::UI::UI3::Ui3SvgFailure failure = Inkeys::UI::UI3::Ui3SvgFailure::DrawRejected)
	{
		if (observationScope && observationScope->probe) observationScope->probe->ObserveRejected(svg.ObservationState(), failure);
		return false;
	};
	// 判断是否启用
	if (!deviceContext || svg.enable.val == false) return RejectObserved();
	if (frameZoom <= 0.0) return RejectObserved();
	if (svg.w.val <= 0 || svg.h.val <= 0) return RejectObserved();
	double contentScale = svg.contentScale;
	double contentPct = svg.contentPct;
	if (!isfinite(contentScale) || contentScale <= 0.0) return RejectObserved();
	if (!isfinite(contentPct) || svg.pct.val * contentPct <= 0.0) return RejectObserved();

	// 初始化绘制量
	double tarZoom = frameZoom;
	double baseX = inh.x * tarZoom;
	double baseY = inh.y * tarZoom;
	double baseW = svg.w.val * tarZoom;
	double baseH = svg.h.val * tarZoom;
	// 内容倍率只改变目标矩形，并围绕原中心缩放；缓存继续使用基础尺寸。
	double tarW = baseW * contentScale;
	double tarH = baseH * contentScale;
	double tarX = baseX + (baseW - tarW) / 2.0;
	double tarY = baseY + (baseH - tarH) / 2.0;
	double tarPct = clamp(static_cast<double>(svg.pct.val) * contentPct
		* ThemeMaterial::ResolveIdleOpacity(residentContentOpacity,
			ThemeMaterial::IdleContentOpacity, residentIdleFade), 0.0, 1.0);

	// 尺寸与内容缩放动画只改变目标矩形，SVG 位图尽量复用到稳定帧。
	ComPtr<ID2D1Bitmap> d2dBitmap;
	bool observedQualityFallback = false;
	{
		// 旧设备位图不能作为质量刷新失败时的回退；正常路径仍由外部重置链先清理。
		if (svg.cacheBitmap && svg.cacheDeviceGeneration != deviceGeneration) svg.ResetCache();
		bool colorChanged =
			(svg.color1.has_value() && svg.cColor1 != svg.color1.value().val)
			|| (svg.color2.has_value() && svg.cColor2 != svg.color2.value().val);
		bool sizeChanged = abs(svg.cW - baseW) > SvgRasterSizeEpsilon
			|| abs(svg.cH - baseH) > SvgRasterSizeEpsilon;
		bool transformAnimating = !svg.w.IsSame() || !svg.h.IsSame()
			|| abs(contentScale - 1.0) > SvgRasterSizeEpsilon
			|| abs(contentPct - 1.0) > SvgRasterSizeEpsilon;
		bool materiallyUpscaled = svg.cacheBitmap
			&& (tarW > svg.cW * SvgRasterUpscaleThreshold
				|| tarH > svg.cH * SvgRasterUpscaleThreshold);

		const bool appearanceChanged = svg.appearanceKey != svg.cAppearanceKey;
		bool needUpdate = !svg.cacheBitmap || colorChanged || appearanceChanged
			|| (!transformAnimating && sizeChanged)
			|| (transformAnimating && materiallyUpscaled);
		if (observationScope && observationScope->probe)
			observationScope->probe->NoteLookup(needUpdate, svg.ObservedBitmapProof().ready && svg.ObservedBitmapProof().semanticKnown);
		if (needUpdate)
		{
			double rasterW = baseW;
			double rasterH = baseH;
			if (transformAnimating && materiallyUpscaled)
			{
				// 尺寸动画可直接按终点预建，避免后续中间帧再次跨过质量阈值。
				double targetW = static_cast<double>(svg.w.tar) * tarZoom;
				double targetH = static_cast<double>(svg.h.tar) * tarZoom;
				if (isfinite(targetW) && targetW > 0.0) rasterW = (std::max)(tarW, targetW);
				else rasterW = tarW;
				if (isfinite(targetH) && targetH > 0.0) rasterH = (std::max)(tarH, targetH);
				else rasterH = tarH;
			}

			if (!svg.CacheBitmap(deviceContext, rasterW, rasterH, deviceGeneration))
			{
				// 质量刷新失败时保留已有内容；内容/颜色失效则不能显示旧语义。
				if (!svg.cacheBitmap || colorChanged || appearanceChanged) return RejectObserved(svg.ObservationState().lastFailure);
				observedQualityFallback = true;
			}
		}
		d2dBitmap = svg.cacheBitmap.Get();
	}
	if (!d2dBitmap) return RejectObserved();

	// 渲染到 DC
	{
		D2D1_RECT_F destRect = D2D1::RectF(static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY), static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH));
		double tarAngle = svg.angle.val;
		if (!isfinite(tarAngle)) tarAngle = 0.0;
		D2D1_MATRIX_3X2_F originalTransform;
		deviceContext->GetTransform(&originalTransform);
		bool transformChanged = abs(fmod(tarAngle, 360.0)) > 0.000001;
		if (transformChanged)
		{
			// 与 PNG 一致：仅旋转最终内容，布局宽高和缓存尺寸都保持不变。
			deviceContext->SetTransform(
				D2D1::Matrix3x2F::Rotation(
					static_cast<FLOAT>(tarAngle),
					D2D1::Point2F(
						static_cast<FLOAT>(tarX + tarW / 2.0),
						static_cast<FLOAT>(tarY + tarH / 2.0)))
				* originalTransform);
		}
		if (observationScope && observationScope->probe)
		{
			Inkeys::UI::UI3::Ui3SvgDrawObservation observed;
			observed.used = svg.ObservedBitmapProof(); observed.qualityMatches = !observedQualityFallback;
			observed.use = observedQualityFallback ? Inkeys::UI::UI3::Ui3SvgUse::QualityFallback : Inkeys::UI::UI3::Ui3SvgUse::Unverified;
			observed.failure = observedQualityFallback ? svg.ObservationState().lastFailure : Inkeys::UI::UI3::Ui3SvgFailure::None;
			const float dest[4]{ destRect.left, destRect.top, destRect.right, destRect.bottom };
			D2D1_MATRIX_3X2_F effective; deviceContext->GetTransform(&effective);
			const float matrix[6]{ effective._11, effective._12, effective._21, effective._22, effective._31, effective._32 };
			for (unsigned i = 0; i < 4; ++i) observed.destBits[i] = std::bit_cast<std::uint32_t>(dest[i]);
			for (unsigned i = 0; i < 6; ++i) observed.transformBits[i] = std::bit_cast<std::uint32_t>(matrix[i]);
			observed.finalOpacityBits = std::bit_cast<std::uint32_t>(static_cast<FLOAT>(tarPct));
			observationScope->probe->ObserveDraw(deviceContext, svg.ObservationState(), observed);
		}
		{
		Inkeys::UI::UI3::Ui3SvgStageTimer timer(Inkeys::UI::UI3::Ui3SvgStage::Draw);
		Inkeys::UI::UI3::ObserveUi3SvgOperation(Inkeys::UI::UI3::Ui3SvgStage::Draw, Inkeys::UI::UI3::Ui3SvgOperation::Begin);
		deviceContext->DrawBitmap(
			d2dBitmap.Get(),
			destRect,								// 目标矩形
			static_cast<FLOAT>(tarPct),				// 不透明度
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
			nullptr									// 源rect, null表示全部
		);
		}
		if (transformChanged) deviceContext->SetTransform(originalTransform);
	}

	return true;
}
bool UIRendering::Png(ID2D1DeviceContext* deviceContext, UiPNGClass& png, const UiInheritClass& inh)
{
	if (!deviceContext || !png.enable.val) return false;
	if (frameZoom <= 0.0) return false;
	if (png.w.val <= 0.0 || png.h.val <= 0.0 || png.pct.val <= 0.0) return false;
	if (png.cacheBitmap && png.cacheDeviceGeneration != deviceGeneration) png.ResetCache();
	bool pngCacheMiss = !png.cacheBitmap;
	if (pngCacheMiss && !png.CacheBitmap(deviceContext, deviceGeneration)) return false;

	double tarZoom = frameZoom;
	double tarX = inh.x * tarZoom;
	double tarY = inh.y * tarZoom;
	double tarW = png.w.val * tarZoom;
	double tarH = png.h.val * tarZoom;
	double tarPct = clamp(static_cast<double>(png.pct.val)
		* ThemeMaterial::ResolveIdleOpacity(residentContentOpacity,
			ThemeMaterial::IdleContentOpacity, residentIdleFade), 0.0, 1.0);
	double tarAngle = png.angle.val;
	if (!isfinite(tarAngle)) tarAngle = 0.0;

	D2D1_MATRIX_3X2_F originalTransform;
	deviceContext->GetTransform(&originalTransform);
	bool transformChanged = abs(fmod(tarAngle, 360.0)) > 0.000001;
	if (transformChanged)
	{
		// 只旋转绘制内容，目标宽高和布局坐标保持不变。
		deviceContext->SetTransform(
			D2D1::Matrix3x2F::Rotation(
				static_cast<FLOAT>(tarAngle),
				D2D1::Point2F(
					static_cast<FLOAT>(tarX + tarW / 2.0),
					static_cast<FLOAT>(tarY + tarH / 2.0)))
			* originalTransform);
	}

	(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(deviceContext), deviceContext)->DrawBitmap(
		png.cacheBitmap.Get(),
		D2D1::RectF(
			static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY),
			static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH)),
		static_cast<FLOAT>(tarPct),
		D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
		nullptr);
	if (transformChanged) deviceContext->SetTransform(originalTransform);
	return true;
}
bool UIRendering::Word(ID2D1DeviceContext* deviceContext, const UiWordClass& word, const UiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_ALIGNMENT textAlign, bool fitButtonLabel)
{
	// 判断是否启用
	if (!deviceContext || word.enable.val == false) return false;
	if (frameZoom <= 0.0) return false;
	if (word.size.val <= 0) return false;
	if (word.w.val <= 0 || word.h.val <= 0) return false;
	double contentPct = clamp(static_cast<double>(word.contentPct), 0.0, 1.0);
	if (word.pct.val <= 0.0 || contentPct <= 0.0) return false;
	const wstring tarContent = word.content.GetVal();

	// 初始化绘制量
	double tarZoom = frameZoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = word.w.val * tarZoom;
	double tarH = word.h.val * tarZoom;
	double tarSize = word.size.val * tarZoom;
	double contentScale = (std::max)(0.0, static_cast<double>(word.contentScale));
	double centerX = tarX + tarW / 2.0;
	double centerY = tarY + tarH / 2.0;
	tarW *= contentScale;
	tarH *= contentScale;
	// 只对按钮主标签拟合；按最终几何计算比例，避免动画逐帧改变名义字号目标。
	double labelFit = 1.0, fittedTarget = -1.0;
	if (fitButtonLabel)
	{
		const double nominal = word.fontTargetSize >= 0.0 ? static_cast<double>(word.fontTargetSize) : static_cast<double>(word.size.tar);
		const double sourceTarget = static_cast<double>(word.size.tar);
		if (nominal > 0.0 && sourceTarget > 0.0)
		{
			const double targetScale = nominal / sourceTarget;
			fittedTarget = FitButtonLabelFontSize(tarContent,nominal,
				static_cast<double>(word.w.tar)*targetScale,static_cast<double>(word.h.tar)*targetScale,fontWeight);
			labelFit = fittedTarget / nominal;
		}
	}
	tarSize = ResolveWordFontSize(deviceContext, word, tarSize * contentScale * labelFit, fittedTarget);
	if (tarSize <= 0.0) return false;
	tarX = centerX - tarW / 2.0;
	tarY = centerY - tarH / 2.0;
	// 布局透明度、内容切换透明度与常驻内容透明度相乘
	double tarPct = word.pct.val * contentPct * ThemeMaterial::ResolveIdleOpacity(
		residentContentOpacity, ThemeMaterial::IdleContentOpacity, residentIdleFade);
	const auto layoutRect = D2D1::RectF(static_cast<FLOAT>(tarX), static_cast<FLOAT>(tarY),
		static_cast<FLOAT>(tarX + tarW), static_cast<FLOAT>(tarY + tarH));
	if (!ShouldDrawFrameBounds(deviceContext, layoutRect, RenderingAttribute::dirtyAntialiasPadding)) return false;
	if (!GetTypographyCache()) return false;

	// 获取样式
	IDWriteTextFormat* textFormat = nullptr;
	{
		/*IDWriteTextFormat* tmpTextFormat;
		SharedDWriteFactory()->CreateTextFormat(
			fontFamily,
			SharedFontCollection().Get(),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			static_cast<FLOAT>(tarSize),
			textLocale,
			&tmpTextFormat
		);
		tmpTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		tmpTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

		textFormat.Attach(tmpTextFormat);*/

		textFormat = GetTypographyCache()->GetFormat(
			fontFamily,
			tarSize,
			SharedFontCollection().Get(),
			fontWeight,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			textLocale,
			textAlign,
			DWRITE_PARAGRAPH_ALIGNMENT_CENTER   // 指定段落居中
		);
	}
	if (!textFormat) return false;
	// 渲染到 DC
	{
		COLORREF color = word.color.val;

		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, color, tarPct);
		if (!fillBrush) return false;

		(ObserveNonSvgRectWrite(deviceContext, layoutRect), deviceContext)->DrawTextW(
			tarContent.c_str(),
			wcslen(tarContent.c_str()),
			textFormat,
			layoutRect,
			fillBrush,
			D2D1_DRAW_TEXT_OPTIONS_CLIP
		);
	}

	return true;
}

double UIRendering::FitButtonLabelFontSize(const wstring& content, double fontSize,
	double width, double height, DWRITE_FONT_WEIGHT weight)
{
	if (content.empty() || !std::isfinite(fontSize) || !std::isfinite(width) || !std::isfinite(height)
		|| fontSize <= 0.0 || width <= 0.0 || height <= 0.0) return fontSize;
	const ButtonLabelFitKey key{content,fontSize,width,height,static_cast<unsigned>(weight)};
	if (const auto found=buttonLabelFitCache.find(key);found!=buttonLabelFitCache.end()) return found->second;
	auto fits=[&](double size)
	{
		const auto measured=MeasureText(content,size,weight);
		return measured.width>0 && measured.height>0 && measured.width<=width && measured.height<=height;
	};
	double result=fontSize;
	if (!fits(fontSize))
	{
		// 保留可读性下限：12-DIP 单行至少 9.5，既有 9-DIP 双行至少 8.5；比例随缩放一致。
		const double minimum=fontSize*(content.find(L'\n')!=wstring::npos ? 8.5/9.0 : 9.5/12.0);
		result=minimum;
		if (fits(minimum))
		{
			double upper=fontSize;
			for (int step=0;step<12;++step)
			{
				const double middle=(result+upper)*0.5;
				if (fits(middle)) result=middle; else upper=middle;
			}
		}
	}
	// 只缓存有限的语言/文字/固定槽组合，正文和动态数字不进入此路径。
	if (buttonLabelFitCache.size()>=64) buttonLabelFitCache.clear();
	buttonLabelFitCache.emplace(key,result);
	return result;
}

D2D1_SIZE_F UIRendering::MeasureText(
	const wstring& content, double fontSize, DWRITE_FONT_WEIGHT fontWeight)
{
	D2D1_SIZE_F result = D2D1::SizeF();
	auto dwriteFactory = SharedDWriteFactory();
	if (content.empty() || !dwriteFactory || !GetTypographyCache() || fontSize <= 0.0)
		return result;

	IDWriteTextFormat* textFormat =
		GetTypographyCache()->GetFormat(
			fontFamily, static_cast<FLOAT>(fontSize),
			SharedFontCollection().Get(),
			fontWeight, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, textLocale,
			DWRITE_TEXT_ALIGNMENT_LEADING,
			DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	if (!textFormat) return result;

	ComPtr<IDWriteTextLayout> textLayout;
	HRESULT hr = dwriteFactory->CreateTextLayout(
		content.c_str(), static_cast<UINT32>(content.size()), textFormat,
		4096.0F, 4096.0F, &textLayout);
	if (SUCCEEDED(hr))
	{
		textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		DWRITE_TEXT_METRICS metrics{};
		hr = textLayout->GetMetrics(&metrics);
		if (SUCCEEDED(hr))
		{
			result.width = ceil(metrics.widthIncludingTrailingWhitespace);
			result.height = ceil(metrics.height);
			return result;
		}
	}

	// DWrite 测量失败时保留可读区域，不能让提示框退化为零尺寸。
	size_t maxLineLength = 0;
	size_t currentLineLength = 0;
	size_t lineCount = 1;
	for (wchar_t ch : content)
	{
		if (ch == L'\n')
		{
			maxLineLength = (std::max)(maxLineLength, currentLineLength);
			currentLineLength = 0;
			++lineCount;
		}
		else ++currentLineLength;
	}
	maxLineLength = (std::max)(maxLineLength, currentLineLength);
	result.width = static_cast<FLOAT>(maxLineLength * fontSize);
	result.height = static_cast<FLOAT>(lineCount * fontSize * 1.4);
	return result;
}

void UIRendering::ColorPalette(ID2D1DeviceContext* dc, D2D1_RECT_F rect, bool darkTone, bool below, double opacity)
{
	if (!dc) return;
	const D2D1_ROUNDED_RECT rounded{rect, 4, 4};
	if (auto* hue = GetColorPickerHueGradientBrush(dc, {rect.left,rect.top}, {rect.right,rect.top}))
	{
		hue->SetOpacity(static_cast<float>(opacity)); dc->FillRoundedRectangle(rounded, hue);
		const D2D1_POINT_2F nearPoint{rect.left, below ? rect.top : rect.bottom};
		const D2D1_POINT_2F farPoint{rect.left, below ? rect.bottom : rect.top};
		if (auto* tone = GetColorPickerToneGradientBrush(dc, darkTone,
			darkTone ? nearPoint : farPoint, darkTone ? farPoint : nearPoint, static_cast<float>(opacity)))
			dc->FillRoundedRectangle(rounded, tone);
	}
}

void DrawSliderThumb(UIRendering& renderer, ID2D1DeviceContext* dc,
	const SliderThumbStyle& style, UiSVGClass* iconSvg, double iconScale)
{
	const auto centerPx = style.centerPx;
	const FLOAT outerDiameterPx = style.outerDiameterPx, centerDiameterPx = style.centerDiameterPx;
	const double opacity = style.opacity;
	if (!dc || outerDiameterPx <= 0.0F || opacity <= 0.0) return;
	const double lightWeight = ThemeMaterial::ClampWeight(style.lightWeight);
	const double accentOpacity = ThemeMaterial::ClampWeight(style.accentOpacity);
	const COLORREF darkFill = MixUiColor(
		GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::Surface),
		GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::TextPrimary), 0.20);
	const COLORREF outerFill = MixUiColor(darkFill, RGB(255, 255, 255), lightWeight);
	const COLORREF darkFrame = MixUiColor(darkFill,
		GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::TextPrimary), 0.12);
	const COLORREF lightFrame = MixUiColor(
		GetThemeColor(ThemeModeEnum::Light, ThemeColorEnum::Surface),
		GetThemeColor(ThemeModeEnum::Light, ThemeColorEnum::TextPrimary),
		ThemeMaterial::LightSliderThumbBorderOpacity);
	const FLOAT radius = outerDiameterPx / 2.0F;
	// 主栏旧公式逐圆合成；编辑器内容倍率则在层边界只乘一次。
	if (style.compositeOpacity) renderer.PushPanelOpacity(dc, {centerPx.x - radius, centerPx.y - radius,
		centerPx.x + radius, centerPx.y + radius}, opacity);
	const auto fill = [&](COLORREF color, FLOAT r)
	{
		// 帧内solid brush会被下一次取色复用，必须立即消费。
		if (r > 0.0F)
			if (auto* brush = renderer.ContentSolidBrush(color, style.compositeOpacity ? 1.0 : opacity))
			{
				Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(dc);
				dc->FillEllipse(D2D1::Ellipse(centerPx, r, r), brush);
			}
	};
	fill(MixUiColor(darkFrame, lightFrame, lightWeight), radius);
	fill(outerFill, (std::max)(0.0F, radius - style.frameThicknessPx));
	if (!iconSvg)
	{
		fill(MixUiColor(style.surfaceColor, style.accentColor, accentOpacity),
			(std::min)(radius * 0.70F, centerDiameterPx / 2.0F));
		if (style.compositeOpacity) dc->PopLayer();
		return;
	}
	const double zoom = renderer.GetFrameZoom();
	const double diameter = 16.0 * iconScale;
	iconSvg->w.SetDirect(diameter);
	iconSvg->h.SetDirect(diameter);
	iconSvg->pct.SetDirect(style.compositeOpacity ? 1.0 : opacity);
	const COLORREF iconColor = MixUiColor(outerFill,
		style.accentColor, accentOpacity);
	if (iconSvg->color1) iconSvg->color1->SetDirect(iconColor);
	if (iconSvg->color2) iconSvg->color2->SetDirect(iconColor);
	const auto inh = iconSvg->UpInh({centerPx.x / zoom - diameter / 2.0,
		centerPx.y / zoom - diameter / 2.0});
	renderer.Svg(dc, *iconSvg, inh);
	if (style.compositeOpacity) dc->PopLayer();
}

}
