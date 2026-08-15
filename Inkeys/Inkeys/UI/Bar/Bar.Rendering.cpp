module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"
#include <cstdio>
#include <d2d1effects.h>
#include <limits>

#pragma comment(lib, "dxguid.lib")

module Inkeys.UI.Bar;
import :Main;
import :Rendering;
import :UI;
import :State;
import :Button;
import :Format;
import :RenderingAttribute;

import Inkeys.UI.Bar.Animation;
import Inkeys.UI.RenderPipeline;

namespace
{
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

	constexpr double BarThicknessFineDialLabelFontSizeDip = 10.0;

	double ApplyBorderLightSmoothstep(double progress)
	{
		progress = clamp(progress, 0.0, 1.0);
		return progress * progress * (3.0 - 2.0 * progress);
	}
}

// 具体渲染
BarUIRendering::BarUIRendering(BarUISetClass* barUISetClassT) { barUISetClass = barUISetClassT; }

HRESULT BarUIRendering::EnsureDeviceResources(
	const Ui3RenderDeviceEpoch& epoch, UINT32 targetWidth, UINT32 targetHeight)
{
	if (targetWidth == 0 || targetHeight == 0) return E_INVALIDARG;
	if (epoch.generation == deviceGeneration && deviceContext
		&& targetBitmap && gdiInteropRenderTarget
		&& targetBitmapWidth == targetWidth
		&& targetBitmapHeight == targetHeight)
		return S_OK;
	return RecreateDeviceResources(epoch, targetWidth, targetHeight);
}

HRESULT BarUIRendering::RecreateDeviceResources(
	const Ui3RenderDeviceEpoch& epoch, UINT32 targetWidth, UINT32 targetHeight)
{
	if (!epoch.d2dDevice) return E_POINTER;

	ComPtr<ID2D1DeviceContext> nextDeviceContext;
	ComPtr<ID2D1Bitmap1> nextTargetBitmap;
	ComPtr<ID2D1GdiInteropRenderTarget> nextGdiInteropRenderTarget;
	HRESULT hr = epoch.d2dDevice->CreateDeviceContext(
		D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &nextDeviceContext);
	if (FAILED(hr)) return hr;

	const D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED));
	hr = nextDeviceContext->CreateBitmap(
		D2D1::SizeU(targetWidth, targetHeight), nullptr, 0,
		&bitmapProperties, &nextTargetBitmap);
	if (FAILED(hr)) return hr;

	hr = nextDeviceContext.As(&nextGdiInteropRenderTarget);
	if (FAILED(hr)) return hr;
	nextDeviceContext->SetTarget(nextTargetBitmap.Get());
	nextDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	// 新 epoch 三项资源全部成功后才替换旧资源，失败时保持当前帧域完整。
	DiscardDeviceResources();
	deviceContext = move(nextDeviceContext);
	targetBitmap = move(nextTargetBitmap);
	gdiInteropRenderTarget = move(nextGdiInteropRenderTarget);
	deviceGeneration = epoch.generation;
	targetBitmapWidth = targetWidth;
	targetBitmapHeight = targetHeight;
	return S_OK;
}

void BarUIRendering::DiscardDeviceResources()
{
	DiscardDeviceDependentCaches();
	if (deviceContext) deviceContext->SetTarget(nullptr);
	gdiInteropRenderTarget.Reset();
	targetBitmap.Reset();
	deviceContext.Reset();
	deviceGeneration = 0;
	targetBitmapWidth = 0;
	targetBitmapHeight = 0;
}

void BarUIRendering::DiscardDeviceDependentCaches()
{
	if (barUISetClass)
	{
		// D2D 位图属于当前 device generation；保留 SVG 文本和 PNG 解码像素，仅丢上传缓存。
		for (const auto& [key, svg] : barUISetClass->svgMap)
			if (svg) svg->ResetCache();
		for (const auto& [key, png] : barUISetClass->pngMap)
			if (png) png->ResetCache();
		barUISetClass->barButtonSet.ResetIconCaches();
	}
	frameGradientBrushCache.clear();
	frameDiffuseMaskCache.clear();
	frameGeometryDiffuseMaskCache.clear();
	superellipseGeometryCache = {};
	frameSolidColorBrush.Reset();
	for (auto& cached : thicknessPreviewGradientBrushCache) cached = {};
	thicknessPreviewGradientUseSerial = 0;
	colorPickerHueGradientBrush.Reset();
	colorPickerLightGradientBrush.Reset();
	colorPickerDarkGradientBrush.Reset();
	thicknessPreviewPath.Reset();
	thicknessPreviewStrokeStyle.Reset();
	roundStrokeStyle.Reset();
	thicknessFineDialSelectorGeometry.Reset();
	for (auto& cached : thicknessFineDialLabelCache) cached = {};
	thicknessFineDialLabelUseSerial = 0;
	frameGaussianBlurEffect.Reset();
	frameMaskDeviceContext.Reset();
	frameDiffuseExactMaskBrush.Reset();
	frameGradientFailureLogged = false;
	frameGradientUnavailable = false;
	thicknessPreviewGradientFailureLogged = false;
	thicknessPreviewGradientUnavailable = false;
	thicknessPreviewPathFailureLogged = false;
	thicknessPreviewPathUnavailable = false;
	roundStrokeStyleFailureLogged = false;
	roundStrokeStyleUnavailable = false;
	thicknessFineDialSelectorUnavailable = false;
	thicknessFineDialSelectorFailureLogged = false;
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
	frameDirtyClipRect = {};
}

void BarUIRendering::PushFrameDirtyClip(
	ID2D1DeviceContext* deviceContext, const D2D1_RECT_F& dirtyRect)
{
	if (!deviceContext || frameDirtyClipActive) return;
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
	frameDirtyClipRect = dirtyRect;
	deviceContext->PushAxisAlignedClip(
		dirtyRect, D2D1_ANTIALIAS_MODE_ALIASED);
	frameDirtyClipActive = true;
}

void BarUIRendering::PopFrameDirtyClip(ID2D1DeviceContext* deviceContext)
{
	if (!deviceContext || !frameDirtyClipActive) return;
	deviceContext->PopAxisAlignedClip();
	frameDirtyClipActive = false;
}

void BarUIRendering::HandleFrameEndDrawResult(HRESULT endDrawResult)
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
		if (IDTLogger) IDTLogger->error(
			"[BarUIRendering::HandleFrameEndDrawResult] A8 预模糊遮罩提交失败，本设备停用柔光遮罩, hr=0x{:08X}",
			static_cast<unsigned int>(endDrawResult));
	}
}

RECT BarUIRendering::GetFramePrimaryLightDamageBounds() const noexcept
{
	if (!frameEdgeLightingEnabled || !framePrimaryLightAnchorInitialized)
		return {};
	const double padding = BarRenderingAttribute::pointLightDiffuseExtraWidth
		* frameZoom + BarRenderingAttribute::dirtyAntialiasPadding;
	const double radius = max(0.0, static_cast<double>(frameLightRadius)) + padding;
	return RECT(
		static_cast<LONG>(floor(static_cast<double>(framePrimaryLight.x) - radius)),
		static_cast<LONG>(floor(static_cast<double>(framePrimaryLight.y) - radius)),
		static_cast<LONG>(ceil(static_cast<double>(framePrimaryLight.x) + radius)),
		static_cast<LONG>(ceil(static_cast<double>(framePrimaryLight.y) + radius)));
}

RECT BarUIRendering::GetFrameCursorLightDamageBounds() const noexcept
{
	const bool cursorLightCanContribute = frameCursorLightIntensity > 0.0001F
		|| (frameCursorLightAnimating && frameCursorLightIntensityTarget > 0.0001F);
	if (!frameEdgeLightingEnabled || !cursorLightCanContribute)
		return {};
	const double padding = BarRenderingAttribute::pointLightDiffuseExtraWidth
		* frameZoom + BarRenderingAttribute::dirtyAntialiasPadding;
	const double radius = max(0.0, static_cast<double>(frameCursorLightRadius)) + padding;
	return RECT(
		static_cast<LONG>(floor(static_cast<double>(frameCursorLight.x) - radius)),
		static_cast<LONG>(floor(static_cast<double>(frameCursorLight.y) - radius)),
		static_cast<LONG>(ceil(static_cast<double>(frameCursorLight.x) + radius)),
		static_cast<LONG>(ceil(static_cast<double>(frameCursorLight.y) + radius)));
}

bool BarUIRendering::PrepareFrameLighting(double animationDtSeconds,
	int drawingMode, int penMode, COLORREF brush1Color,
	COLORREF highlighterColor, bool penetrateSelected)
{
	const bool previousEdgeLightingEnabled = frameEdgeLightingEnabled;
	const bool previousPrimaryLightAvailable = framePrimaryLightAnchorInitialized;
	const D2D1_POINT_2F previousPrimaryLight = framePrimaryLight;
	const D2D1_POINT_2F previousCursorLight = frameCursorLight;
	const FLOAT previousPrimaryRadius = frameLightRadius;
	const FLOAT previousCursorRadius = frameCursorLightRadius;
	const FLOAT previousCursorIntensity = frameCursorLightIntensity;
	const COLORREF previousDrawingPenColor = frameDrawingPenColor;
	const double previousDrawingPenColorBlend = frameDrawingPenColorBlend;
	const double previousDrawingLightOpacity = frameDrawingLightOpacity;
	framePrimaryLightChanged = false;
	frameCursorLightChanged = false;
	auto FinishLightingFrame = [&](bool sustainPrimary, bool sustainCursor)
		{
			auto PointChanged = [](D2D1_POINT_2F left, D2D1_POINT_2F right)
				{
					return abs(left.x - right.x) > 0.01F
						|| abs(left.y - right.y) > 0.01F;
				};
			const bool sharedVisualChanged =
				(previousDrawingPenColor & 0x00FFFFFF)
					!= (frameDrawingPenColor & 0x00FFFFFF)
				|| abs(previousDrawingPenColorBlend
					- frameDrawingPenColorBlend) > 0.000001
				|| abs(previousDrawingLightOpacity
					- frameDrawingLightOpacity) > 0.000001;
			const bool previousPrimaryVisible = previousEdgeLightingEnabled
				&& previousPrimaryLightAvailable && previousPrimaryRadius > 0.0F;
			const bool currentPrimaryVisible = frameEdgeLightingEnabled
				&& framePrimaryLightAnchorInitialized && frameLightRadius > 0.0F;
			const bool previousCursorVisible = previousEdgeLightingEnabled
				&& previousCursorIntensity > 0.0001F && previousCursorRadius > 0.0F;
			const bool currentCursorVisible = frameEdgeLightingEnabled
				&& frameCursorLightIntensity > 0.0001F
				&& frameCursorLightRadius > 0.0F;

			framePrimaryLightChanged = sustainPrimary
				|| previousPrimaryVisible != currentPrimaryVisible
				|| ((previousPrimaryVisible || currentPrimaryVisible)
					&& (PointChanged(previousPrimaryLight, framePrimaryLight)
						|| abs(previousPrimaryRadius - frameLightRadius) > 0.01F
						|| sharedVisualChanged));
			frameCursorLightChanged = sustainCursor
				|| previousCursorVisible != currentCursorVisible
				|| ((previousCursorVisible || currentCursorVisible)
					&& (PointChanged(previousCursorLight, frameCursorLight)
						|| abs(previousCursorRadius - frameCursorLightRadius) > 0.01F
						|| abs(previousCursorIntensity
							- frameCursorLightIntensity) > 0.0001F
						|| sharedVisualChanged));
			return framePrimaryLightChanged || frameCursorLightChanged;
		};

	frameCursorLightVisible = false;
	frameDrawingUsesPenColor = false;
	auto SnapshotPenColor = [&]()
		{
			return (penMode
				== static_cast<int>(PenModeSelectEnum::IdtPenHighlighter1)
				? highlighterColor : brush1Color)
				& 0x00FFFFFF;
		};
	COLORREF desiredDrawingPenColor = frameDrawingPenColorInitialized
		? frameDrawingPenColorTarget : SnapshotPenColor();

	double zoom = barUISetClass ? frameZoom : 0.0;
	if (!isfinite(zoom) || zoom <= 0.0) zoom = 0.0;
	frameLightRadius = static_cast<FLOAT>(BarBorderLightRadius * zoom);
	frameCursorLightRadius = static_cast<FLOAT>(BarBorderCursorLightRadius * zoom);
	SetFrameCursorLightLocalGeometry(frameCursorLight,
		D2D1::SizeF(frameCursorLightRadius, frameCursorLightRadius));

	bool edgeLightingEnabled = BarUiEdgeLightingEnabled;
	bool dynamicEdgeLightingEnabled = edgeLightingEnabled && BarUiDynamicEdgeLightingEnabled;
	if (!edgeLightingEnabled)
	{
		// 总开关关闭时停止点光计算，基础灰边仍由绘制阶段保留。
		bool needSettlingFrame = frameEdgeLightingEnabled || frameLightingWasAnimating
			|| frameCursorLightIntensity > 0.0001F;
		frameEdgeLightingEnabled = false;
		framePrimaryLightAnchorInitialized = false;
		framePrimaryLightAnimating = false;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget = 0.0F;
		frameCursorLightAnimating = false;
		frameCursorInputAvailable = false;
		frameAnimationStateInitialized = false;
		frameDrawingPenColorAnimating = false;
		frameDrawingPenColorInitialized = false;
		frameDrawingModeTransitionAnimating = false;
		frameDrawingModeInitialized = false;
		frameDrawingPenColorSourceInitialized = false;
		frameDrawingPenColorCarriesHighlighterHistory = false;
		const bool sustainPrimary = framePrimaryLightWasAnimating;
		const bool sustainCursor = frameCursorLightWasAnimating;
		framePrimaryLightWasAnimating = false;
		frameCursorLightWasAnimating = false;
		frameLightingWasAnimating = false;
		return FinishLightingFrame(
			needSettlingFrame && sustainPrimary,
			needSettlingFrame && sustainCursor);
	}
	bool edgeLightingStateChanged = !frameEdgeLightingEnabled;
	frameEdgeLightingEnabled = true;

	BarBorderPrimaryAnchorEnum desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::MainButton;
	D2D1_POINT_2F desiredPrimaryLight = framePrimaryLight;
	bool primaryTargetAvailable = false;
	if (barUISetClass)
	{
		// Geometry 始终使用持久 Brush1 色，不受当前 Pen 子模式影响。
		if (drawingMode == static_cast<int>(StateModeSelectEnum::IdtPen))
			desiredDrawingPenColor = SnapshotPenColor();
		else if (drawingMode == static_cast<int>(StateModeSelectEnum::IdtShape))
			desiredDrawingPenColor = brush1Color & 0x00FFFFFF;
		switch (static_cast<StateModeSelectEnum>(drawingMode))
		{
		case StateModeSelectEnum::IdtPen:
			desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::Draw;
			break;
		case StateModeSelectEnum::IdtEraser:
			desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::Eraser;
			break;
		case StateModeSelectEnum::IdtShape:
			desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::Geometry;
			break;
		case StateModeSelectEnum::IdtSelection:
			desiredPrimaryAnchor = BarBorderPrimaryAnchorEnum::Select;
			break;
		default:
			break;
		}

		auto mainButtonIt = barUISetClass->superellipseMap.find(
			BarUISetSuperellipseEnum::MainButton);
		if (mainButtonIt != barUISetClass->superellipseMap.end() && mainButtonIt->second)
		{
			auto mainButton = mainButtonIt->second;
			desiredPrimaryLight = D2D1::Point2F(
				static_cast<FLOAT>(mainButton->x.val * zoom),
				static_cast<FLOAT>((mainButton->y.val + mainButton->h.val / 2.0) * zoom));
			primaryTargetAvailable = true;

			BarButtonPresetEnum anchorPreset = BarButtonPresetEnum::None;
			switch (desiredPrimaryAnchor)
			{
			case BarBorderPrimaryAnchorEnum::Select: anchorPreset = BarButtonPresetEnum::Select; break;
			case BarBorderPrimaryAnchorEnum::Draw: anchorPreset = BarButtonPresetEnum::Draw; break;
			case BarBorderPrimaryAnchorEnum::Eraser: anchorPreset = BarButtonPresetEnum::Eraser; break;
			case BarBorderPrimaryAnchorEnum::Geometry: anchorPreset = BarButtonPresetEnum::Geometry; break;
			default: break;
			}

			auto mainBarIt = barUISetClass->shapeMap.find(BarUISetShapeEnum::MainBar);
			BarButtonClass* anchorButton = anchorPreset == BarButtonPresetEnum::None
				? nullptr : barUISetClass->barButtonSet.preset[static_cast<int>(anchorPreset)];
			if (mainBarIt != barUISetClass->shapeMap.end() && mainBarIt->second && anchorButton)
			{
				auto mainBar = mainBarIt->second;
				double mainBarLeft = mainButton->x.val + mainBar->x.val - mainBar->w.val / 2.0;
				double mainBarTop = mainButton->y.val + mainBar->y.val - mainBar->h.val / 2.0;
				// 主光落在当前模式按钮的下边缘中心，控件布局动画时目标也随之更新。
				desiredPrimaryLight = D2D1::Point2F(
					static_cast<FLOAT>((mainBarLeft + anchorButton->button.x.val) * zoom),
					static_cast<FLOAT>((mainBarTop + anchorButton->button.y.val
						+ anchorButton->button.h.val / 2.0) * zoom));
			}
		}

		frameDrawingUsesPenColor =
			(drawingMode == static_cast<int>(StateModeSelectEnum::IdtPen)
				&& !penetrateSelected)
			|| drawingMode == static_cast<int>(StateModeSelectEnum::IdtShape);
	}

	unsigned long long cursorSerial = 0;
	bool cursorInputAvailable = false;
	if (barUISetClass)
	{
		lock_guard lock(barUISetClass->borderCursorLightMutex);
		frameCursorLight = barUISetClass->borderCursorLightPoint;
		cursorSerial = barUISetClass->borderCursorLightSerial;
		cursorInputAvailable = barUISetClass->borderCursorInputAvailable
			&& barUISetClass->borderCursorLightReady;
	}

	bool animationEnabled = BarUiAnimationEnabled;
	double animationSpeedRate = BarUiAnimationSpeedRate;
	if (!isfinite(animationSpeedRate)) animationSpeedRate = 1.0;
	animationSpeedRate = clamp(animationSpeedRate, 0.1, 5.0);
	if (!isfinite(animationDtSeconds) || animationDtSeconds < 0.0) animationDtSeconds = 0.0;
	animationDtSeconds = clamp(animationDtSeconds, 0.0, 0.05);
	double scaledDtSeconds = animationDtSeconds * animationSpeedRate;

	bool drawingModeTransitionStarted = false;
	double desiredPenColorBlend = frameDrawingUsesPenColor ? 1.0 : 0.0;
	if (!frameDrawingModeInitialized)
	{
		frameDrawingModeInitialized = true;
		frameDrawingMode = drawingMode;
		frameDrawingPenColorBlend = desiredPenColorBlend;
		frameDrawingPenColorBlendStart = desiredPenColorBlend;
		frameDrawingPenColorBlendTarget = desiredPenColorBlend;
		frameDrawingLightOpacity = 1.0;
		frameDrawingLightOpacityStart = 1.0;
	}
	else
	{
		int desiredDrawingMode = drawingMode;
		if (frameDrawingMode != desiredDrawingMode)
			frameDrawingMode = desiredDrawingMode;
		if (frameDrawingPenColorBlendTarget != desiredPenColorBlend)
		{
			// 只有颜色角色改变才淡出换色；同色模式切换仅移动第一光源锚点。
			frameDrawingPenColorBlendStart = frameDrawingPenColorBlend;
			frameDrawingPenColorBlendTarget = desiredPenColorBlend;
			frameDrawingLightOpacityStart = frameDrawingLightOpacity;
			frameDrawingModeTransitionElapsed = 0.0;
			frameDrawingModeTransitionAnimating = animationEnabled;
			drawingModeTransitionStarted = true;
		}
	}

	if (!animationEnabled)
	{
		frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
		frameDrawingLightOpacity = 1.0;
		frameDrawingModeTransitionAnimating = false;
		frameDrawingModeTransitionElapsed = 0.0;
	}
	else if (frameDrawingModeTransitionAnimating)
	{
		double transitionDuration = BarUiDefaultOperationDur;
		if (!isfinite(transitionDuration) || transitionDuration <= 0.0)
		{
			frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
			frameDrawingLightOpacity = 1.0;
			frameDrawingModeTransitionAnimating = false;
		}
		else
		{
			frameDrawingModeTransitionElapsed += scaledDtSeconds;
			double progress = clamp(
				frameDrawingModeTransitionElapsed / transitionDuration, 0.0, 1.0);
			// 颜色变化压缩在中间 30% 时段，光影在正中完全透明，避免看见颜色互相覆盖。
			double colorProgress = ApplyBorderLightSmoothstep(
				clamp((progress - 0.35) / 0.30, 0.0, 1.0));
			frameDrawingPenColorBlend = frameDrawingPenColorBlendStart
				+ (frameDrawingPenColorBlendTarget - frameDrawingPenColorBlendStart)
				* colorProgress;
			if (progress < 0.5)
			{
				double fadeOutProgress = ApplyBorderLightSmoothstep(progress * 2.0);
				frameDrawingLightOpacity =
					frameDrawingLightOpacityStart * (1.0 - fadeOutProgress);
			}
			else
			{
				frameDrawingLightOpacity = ApplyBorderLightSmoothstep(
					(progress - 0.5) * 2.0);
			}
			if (progress >= 1.0)
			{
				frameDrawingPenColorBlend = frameDrawingPenColorBlendTarget;
				frameDrawingLightOpacity = 1.0;
				frameDrawingModeTransitionAnimating = false;
			}
		}
	}

	bool penLightColorChanged = false;
	int desiredColorSource = drawingMode
		== static_cast<int>(StateModeSelectEnum::IdtShape)
		? static_cast<int>(PenModeSelectEnum::IdtPenBrush1) : penMode;
	if (frameDrawingUsesPenColor)
	{
		bool desiredSourceIsHighlighter = desiredColorSource
			== static_cast<int>(PenModeSelectEnum::IdtPenHighlighter1);
		bool enteringGeometryFromHighlighter = drawingMode
			== static_cast<int>(StateModeSelectEnum::IdtShape)
			&& frameDrawingPenColorCarriesHighlighterHistory;
		if (!frameDrawingPenColorSourceInitialized)
		{
			frameDrawingPenColorSourceInitialized = true;
			frameDrawingPenColorSource = desiredColorSource;
			frameDrawingPenColorCarriesHighlighterHistory =
				desiredSourceIsHighlighter;
		}
		else if (enteringGeometryFromHighlighter)
		{
			frameDrawingPenColorSource = desiredColorSource;
			// Geometry 不继承未完成的 Highlighter -> Brush1 插值，直接接管 Brush1 快照。
			frameDrawingPenColorStart = desiredDrawingPenColor;
			frameDrawingPenColorTarget = desiredDrawingPenColor;
			frameDrawingPenColor = desiredDrawingPenColor;
			frameDrawingPenColorElapsed = 0.0;
			frameDrawingPenColorAnimating = false;
			frameDrawingPenColorInitialized = true;
			frameDrawingPenColorCarriesHighlighterHistory = false;
			penLightColorChanged = true;
		}
		else if (frameDrawingPenColorSource != desiredColorSource)
		{
			frameDrawingPenColorSource = desiredColorSource;
			if (desiredSourceIsHighlighter)
				frameDrawingPenColorCarriesHighlighterHistory = true;
		}
		else if (desiredSourceIsHighlighter)
			frameDrawingPenColorCarriesHighlighterHistory = true;
	}
	if (!frameDrawingPenColorInitialized)
	{
		frameDrawingPenColorInitialized = true;
		frameDrawingPenColorStart = desiredDrawingPenColor;
		frameDrawingPenColorTarget = desiredDrawingPenColor;
		frameDrawingPenColor = desiredDrawingPenColor;
	}
	else if ((frameDrawingPenColorTarget & 0x00FFFFFF) != desiredDrawingPenColor)
	{
		// 连续换色从当前视觉颜色重新起步，避免快速点击色块时发生跳变。
		frameDrawingPenColorStart = frameDrawingPenColor;
		frameDrawingPenColorTarget = desiredDrawingPenColor;
		frameDrawingPenColorElapsed = 0.0;
		frameDrawingPenColorAnimating = animationEnabled && frameDrawingUsesPenColor;
		penLightColorChanged = true;
	}

	if (!animationEnabled || !frameDrawingUsesPenColor)
	{
		frameDrawingPenColor = frameDrawingPenColorTarget;
		frameDrawingPenColorAnimating = false;
		frameDrawingPenColorElapsed = 0.0;
	}
	else if (frameDrawingPenColorAnimating)
	{
		double colorDuration = BarUiDefaultOperationDur;
		if (!isfinite(colorDuration) || colorDuration <= 0.0)
		{
			frameDrawingPenColor = frameDrawingPenColorTarget;
			frameDrawingPenColorAnimating = false;
		}
		else
		{
			frameDrawingPenColorElapsed += scaledDtSeconds;
			double progress = clamp(frameDrawingPenColorElapsed / colorDuration, 0.0, 1.0);
			double curvedProgress = BarUiApplyCurve(BarUiCurveEnum::EaseInOutCubic, progress);
			frameDrawingPenColor = MixBarUiColor(
				frameDrawingPenColorStart, frameDrawingPenColorTarget, curvedProgress);
			if (progress >= 1.0)
			{
				frameDrawingPenColor = frameDrawingPenColorTarget;
				frameDrawingPenColorAnimating = false;
			}
		}
	}
	if (frameDrawingPenColorCarriesHighlighterHistory
		&& frameDrawingPenColorSource
			== static_cast<int>(PenModeSelectEnum::IdtPenBrush1)
		&& !frameDrawingPenColorAnimating
		&& (frameDrawingPenColor & 0x00FFFFFF)
			== (desiredDrawingPenColor & 0x00FFFFFF))
	{
		// Brush1 真正稳定后才丢弃 Highlighter 历史；中途进入 Geometry 仍会直接接管。
		frameDrawingPenColorCarriesHighlighterHistory = false;
	}

	bool primaryLightMoved = false;
	bool primaryStateChanged = false;
	if (primaryTargetAvailable)
	{
		auto PointsDiffer = [](D2D1_POINT_2F left, D2D1_POINT_2F right)
			{
				return abs(left.x - right.x) > 0.01F || abs(left.y - right.y) > 0.01F;
			};
		D2D1_POINT_2F previousPrimaryLight = framePrimaryLight;
		if (!framePrimaryLightAnchorInitialized)
		{
			framePrimaryLightAnchorInitialized = true;
			framePrimaryLightAnchor = desiredPrimaryAnchor;
			framePrimaryLightStart = desiredPrimaryLight;
			framePrimaryLightTarget = desiredPrimaryLight;
			framePrimaryLight = desiredPrimaryLight;
		}
		else
		{
			bool anchorChanged = framePrimaryLightAnchor != desiredPrimaryAnchor;
			if (anchorChanged)
			{
				framePrimaryLightAnchor = desiredPrimaryAnchor;
				framePrimaryLightStart = framePrimaryLight;
				framePrimaryLightMoveElapsed = 0.0;
				framePrimaryLightAnimating = animationEnabled;
				primaryStateChanged = true;
			}
			framePrimaryLightTarget = desiredPrimaryLight;

			if (!animationEnabled)
			{
				framePrimaryLight = desiredPrimaryLight;
				framePrimaryLightAnimating = false;
				framePrimaryLightMoveElapsed = 0.0;
			}
			else if (framePrimaryLightAnimating)
			{
				double moveDuration = BarUiDefaultOperationDur;
				if (!isfinite(moveDuration) || moveDuration <= 0.0)
				{
					framePrimaryLight = framePrimaryLightTarget;
					framePrimaryLightAnimating = false;
				}
				else
				{
					framePrimaryLightMoveElapsed += scaledDtSeconds;
					double progress = clamp(framePrimaryLightMoveElapsed / moveDuration, 0.0, 1.0);
					double curvedProgress = BarUiApplyCurve(
						BarUiCurveEnum::EaseInOutCubic, progress);
					framePrimaryLight = D2D1::Point2F(
						static_cast<FLOAT>(framePrimaryLightStart.x
							+ (framePrimaryLightTarget.x - framePrimaryLightStart.x) * curvedProgress),
						static_cast<FLOAT>(framePrimaryLightStart.y
							+ (framePrimaryLightTarget.y - framePrimaryLightStart.y) * curvedProgress));
					if (progress >= 1.0)
					{
						framePrimaryLight = framePrimaryLightTarget;
						framePrimaryLightAnimating = false;
					}
				}
			}
			else framePrimaryLight = desiredPrimaryLight;
		}
		primaryLightMoved = PointsDiffer(previousPrimaryLight, framePrimaryLight);
	}

	bool stateChanged = edgeLightingStateChanged || primaryStateChanged || penLightColorChanged
		|| drawingModeTransitionStarted;
	bool cursorFadeRestarted = false;
	bool cursorMoved = false;
	bool desiredCursorLightVisible = animationEnabled
		&& dynamicEdgeLightingEnabled && cursorInputAvailable;
	if (!frameAnimationStateInitialized)
	{
		frameAnimationStateInitialized = true;
		frameLastAnimationEnabled = animationEnabled;
		frameCursorInputAvailable = desiredCursorLightVisible;
		handledBorderCursorLightSerial = cursorSerial;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget =
			desiredCursorLightVisible ? static_cast<FLOAT>(BarBorderLightIntensity) : 0.0F;
		if (desiredCursorLightVisible)
		{
			frameCursorLightFadeElapsed = 0.0;
			frameCursorLightAnimating = true;
			cursorFadeRestarted = true;
		}
	}
	else
	{
		if (animationEnabled != frameLastAnimationEnabled)
		{
			stateChanged = true;
			frameLastAnimationEnabled = animationEnabled;
		}
		if (desiredCursorLightVisible != frameCursorInputAvailable)
		{
			// 显隐切换从当前强度续接，靠近、超时和重新进入时不会发生亮度跳变。
			frameCursorInputAvailable = desiredCursorLightVisible;
			stateChanged = true;
			handledBorderCursorLightSerial = cursorSerial;
			frameCursorLightIntensityStart = frameCursorLightIntensity;
			frameCursorLightIntensityTarget = desiredCursorLightVisible
				? static_cast<FLOAT>(BarBorderLightIntensity) : 0.0F;
			frameCursorLightFadeElapsed = 0.0;
			frameCursorLightAnimating = animationEnabled
				&& abs(frameCursorLightIntensityTarget - frameCursorLightIntensityStart) > 0.0001F;
			cursorFadeRestarted = frameCursorLightAnimating;
		}
	}

	if (!animationEnabled)
	{
		// 关闭动画后立即隐藏鼠标光，基础灰边和第一主光保持稳定。
		handledBorderCursorLightSerial = cursorSerial;
		frameCursorInputAvailable = false;
		frameCursorLightIntensity = 0.0F;
		frameCursorLightIntensityStart = 0.0F;
		frameCursorLightIntensityTarget = 0.0F;
		frameCursorLightAnimating = false;
		bool needSettlingFrame = frameLightingWasAnimating;
		const bool sustainPrimary = framePrimaryLightWasAnimating;
		const bool sustainCursor = frameCursorLightWasAnimating;
		framePrimaryLightWasAnimating = false;
		frameCursorLightWasAnimating = false;
		frameLightingWasAnimating = false;
		return FinishLightingFrame(
			(primaryLightMoved || stateChanged)
				|| (needSettlingFrame && sustainPrimary),
			(stateChanged || (needSettlingFrame && sustainCursor)));
	}

	if (cursorSerial != handledBorderCursorLightSerial)
	{
		handledBorderCursorLightSerial = cursorSerial;
		cursorMoved = desiredCursorLightVisible;
	}

	if (frameCursorLightAnimating)
	{
		if (!cursorFadeRestarted) frameCursorLightFadeElapsed += scaledDtSeconds;
		double progress = frameCursorLightFadeElapsed / BarBorderCursorFadeInDur;
		double curvedProgress = ApplyBorderLightSmoothstep(progress);
		frameCursorLightIntensity = static_cast<FLOAT>(
			frameCursorLightIntensityStart
			+ (frameCursorLightIntensityTarget - frameCursorLightIntensityStart)
			* curvedProgress);
		if (frameCursorLightFadeElapsed >= BarBorderCursorFadeInDur)
		{
			frameCursorLightIntensity = frameCursorLightIntensityTarget;
			frameCursorLightAnimating = false;
		}
	}
	else frameCursorLightIntensity = frameCursorLightIntensityTarget;
	frameCursorLightVisible = frameCursorLightIntensity > 0.0001F;

	// 时间过程结束后再绘制一帧最终状态，随后恢复原有静止等待。
	const bool sharedLightingAnimating = frameDrawingPenColorAnimating
		|| frameDrawingModeTransitionAnimating;
	const bool primaryLightingAnimating = framePrimaryLightAnimating
		|| sharedLightingAnimating;
	const bool cursorLightingAnimating = frameCursorLightAnimating
		|| sharedLightingAnimating;
	const bool primarySettlingFrame = framePrimaryLightWasAnimating
		&& !primaryLightingAnimating;
	const bool cursorSettlingFrame = frameCursorLightWasAnimating
		&& !cursorLightingAnimating;
	framePrimaryLightWasAnimating = primaryLightingAnimating;
	frameCursorLightWasAnimating = cursorLightingAnimating;
	bool lightingAnimating = primaryLightingAnimating || cursorLightingAnimating;
	frameLightingWasAnimating = lightingAnimating;
	return FinishLightingFrame(
		primaryLightingAnimating || primarySettlingFrame
			|| primaryStateChanged || primaryLightMoved
			|| penLightColorChanged || drawingModeTransitionStarted
			|| edgeLightingStateChanged,
		cursorLightingAnimating || cursorSettlingFrame || cursorMoved
			|| penLightColorChanged || drawingModeTransitionStarted
			|| edgeLightingStateChanged);
}

ID2D1RadialGradientBrush* BarUIRendering::GetFrameGradientBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color, BarBorderLightSourceEnum lightSource)
{
	COLORREF rgb = color & 0x00FFFFFF;
	D2D1_POINT_2F center = framePrimaryLight;
	D2D1_SIZE_F radius = D2D1::SizeF(frameLightRadius, frameLightRadius);
	if (lightSource == BarBorderLightSourceEnum::Cursor)
	{
		center = frameLocalCursorLight;
		radius = D2D1::SizeF(
			frameLocalCursorLightRadiusX, frameLocalCursorLightRadiusY);
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
			return cache.brush.Get();
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
			return frameGradientBrushCache.back().brush.Get();
		}
	}

	frameGradientUnavailable = true;
	if (!frameGradientFailureLogged)
	{
		frameGradientFailureLogged = true;
		if (IDTLogger) IDTLogger->error(
			"[BarUIRendering::GetFrameGradientBrush] 创建边框点光渐变失败, hr=0x{:08X}",
			static_cast<unsigned int>(hr));
	}
	return nullptr;
}

ID2D1SolidColorBrush* BarUIRendering::GetFrameSolidColorBrush(
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

ID2D1LinearGradientBrush* BarUIRendering::GetThicknessPreviewGradientBrush(
	ID2D1DeviceContext* deviceContext, COLORREF color,
	D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
	FLOAT leftOpacity, FLOAT opacity)
{
	if (!deviceContext || !isfinite(leftOpacity) || !isfinite(opacity))
		return nullptr;
	const COLORREF rgb = color & 0x00FFFFFF;
	const std::uint8_t leftOpacityByte = static_cast<std::uint8_t>(round(
		clamp(leftOpacity, 0.0F, 1.0F) * 255.0F));
	opacity = clamp(opacity, 0.0F, 1.0F);
	for (auto& cached : thicknessPreviewGradientBrushCache)
	{
		if (cached.valid && cached.color == rgb
			&& cached.leftOpacity == leftOpacityByte && cached.brush)
		{
			cached.lastUse = ++thicknessPreviewGradientUseSerial;
			cached.brush->SetStartPoint(startPoint);
			cached.brush->SetEndPoint(endPoint);
			cached.brush->SetOpacity(opacity);
			return cached.brush.Get();
		}
	}
	if (thicknessPreviewGradientUnavailable) return nullptr;

	const FLOAT quantizedLeftOpacity =
		static_cast<FLOAT>(leftOpacityByte) / 255.0F;
	D2D1_GRADIENT_STOP gradientStops[] =
	{
		{ 0.0F, Inkeys::Color::ConvertToD2dColor(rgb, quantizedLeftOpacity) },
		{ 1.0F, Inkeys::Color::ConvertToD2dColor(rgb, 1.00) },
	};
	ComPtr<ID2D1GradientStopCollection> stopCollection;
	HRESULT hr = deviceContext->CreateGradientStopCollection(
		gradientStops, ARRAYSIZE(gradientStops), D2D1_GAMMA_2_2,
		D2D1_EXTEND_MODE_CLAMP, &stopCollection);
	ComPtr<ID2D1LinearGradientBrush> brush;
	if (SUCCEEDED(hr))
		hr = deviceContext->CreateLinearGradientBrush(
			D2D1::LinearGradientBrushProperties(startPoint, endPoint),
			stopCollection.Get(), &brush);
	if (FAILED(hr))
	{
		// 创建失败不破坏已有 LRU；旧键仍可命中，新键回退到实色画刷。
		thicknessPreviewGradientUnavailable = true;
		if (!thicknessPreviewGradientFailureLogged)
		{
			thicknessPreviewGradientFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetThicknessPreviewGradientBrush] 创建粗细预览渐变失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	ThicknessPreviewGradientBrushCacheClass created;
	created.color = rgb;
	created.leftOpacity = leftOpacityByte;
	created.lastUse = ++thicknessPreviewGradientUseSerial;
	created.valid = true;
	created.brush = move(brush);
	created.brush->SetOpacity(opacity);
	auto* target = &thicknessPreviewGradientBrushCache.front();
	for (auto& cached : thicknessPreviewGradientBrushCache)
	{
		if (!cached.valid)
		{
			target = &cached;
			break;
		}
		if (cached.lastUse < target->lastUse) target = &cached;
	}
	*target = move(created);
	return target->brush.Get();
}

ID2D1LinearGradientBrush* BarUIRendering::GetColorPickerHueGradientBrush(
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
				if (IDTLogger) IDTLogger->error(
					"[BarUIRendering::GetColorPickerHueGradientBrush] 创建颜色选择器渐变失败, hr=0x{:08X}",
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

ID2D1LinearGradientBrush* BarUIRendering::GetColorPickerToneGradientBrush(
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
				if (IDTLogger) IDTLogger->error(
					"[BarUIRendering::GetColorPickerToneGradientBrush] 创建颜色选择器明暗覆盖失败, hr=0x{:08X}",
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

void BarUIRendering::DrawProgressRing(ID2D1DeviceContext* deviceContext,
	D2D1_POINT_2F center, FLOAT radius, FLOAT strokeWidth,
	FLOAT progress, COLORREF trackColor, COLORREF progressColor,
	FLOAT trackOpacity, FLOAT progressOpacity)
{
	if (!deviceContext || radius <= 0.0F || strokeWidth <= 0.0F) return;
	progress = clamp(progress, 0.0F, 1.0F);
	D2D1_ELLIPSE ellipse = D2D1::Ellipse(center, radius, radius);
	if (auto trackBrush = GetFrameSolidColorBrush(
		deviceContext, trackColor, trackOpacity))
		deviceContext->DrawEllipse(&ellipse, trackBrush, strokeWidth);
	if (progress <= 0.000001F) return;
	if (auto progressBrush = GetFrameSolidColorBrush(
		deviceContext, progressColor, progressOpacity))
	{
		if (progress >= 0.999F)
		{
			deviceContext->DrawEllipse(&ellipse, progressBrush, strokeWidth);
			return;
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
			return;
		sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
		D2D1_ARC_SEGMENT arc{};
		arc.point = end;
		arc.size = D2D1::SizeF(radius, radius);
		arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
		arc.arcSize = sweep >= 180.0F
			? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
		sink->AddArc(arc);
		sink->EndFigure(D2D1_FIGURE_END_OPEN);
		if (SUCCEEDED(sink->Close()))
			deviceContext->DrawGeometry(path.Get(), progressBrush, strokeWidth);
	}
}

ID2D1PathGeometry* BarUIRendering::GetThicknessPreviewPath()
{
	auto factory = SharedD2DFactory();
	if (!factory || thicknessPreviewPathUnavailable) return nullptr;
	if (thicknessPreviewPath)
	{
		return thicknessPreviewPath.Get();
	}

	ComPtr<ID2D1PathGeometry> path;
	HRESULT hr = factory->CreatePathGeometry(&path);
	if (SUCCEEDED(hr))
	{
		ComPtr<ID2D1GeometrySink> sink;
		hr = path->Open(&sink);
		if (SUCCEEDED(hr))
		{
			// 单位路径只在当前 device generation 创建一次，帧内仅变换 world matrix。
			sink->BeginFigure(D2D1::Point2F(0.0F, 0.0F),
				D2D1_FIGURE_BEGIN_HOLLOW);
			D2D1_BEZIER_SEGMENT firstSegment{
				D2D1::Point2F(0.30F, 1.0F),
				D2D1::Point2F(0.40F, 1.0F),
				D2D1::Point2F(0.50F, 0.0F) };
			D2D1_BEZIER_SEGMENT secondSegment{
				D2D1::Point2F(0.60F, -1.0F),
				D2D1::Point2F(0.70F, -1.0F),
				D2D1::Point2F(1.0F, 0.0F) };
			sink->AddBezier(firstSegment);
			sink->AddBezier(secondSegment);
			sink->EndFigure(D2D1_FIGURE_END_OPEN);
			hr = sink->Close();
		}
	}
	if (FAILED(hr))
	{
		thicknessPreviewPathUnavailable = true;
		if (!thicknessPreviewPathFailureLogged)
		{
			thicknessPreviewPathFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetThicknessPreviewPath] 创建粗细预览路径失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	thicknessPreviewPath = move(path);
	return thicknessPreviewPath.Get();
}

ID2D1StrokeStyle1* BarUIRendering::GetThicknessPreviewStrokeStyle()
{
	if (thicknessPreviewStrokeStyle) return thicknessPreviewStrokeStyle.Get();
	auto factory = SharedD2DFactory();
	if (!factory || thicknessPreviewPathUnavailable) return nullptr;
	// 非均匀单位变换只拉伸路径，FIXED 保持真实笔宽与圆头不变形。
	D2D1_STROKE_STYLE_PROPERTIES1 properties{
		D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
		D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND,
		10.0F, D2D1_DASH_STYLE_SOLID, 0.0F,
		D2D1_STROKE_TRANSFORM_TYPE_FIXED };
	HRESULT hr = factory->CreateStrokeStyle(
		&properties, nullptr, 0, &thicknessPreviewStrokeStyle);
	if (FAILED(hr))
	{
		thicknessPreviewPathUnavailable = true;
		if (!thicknessPreviewPathFailureLogged)
		{
			thicknessPreviewPathFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetThicknessPreviewStrokeStyle] 创建圆头描边样式失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}
	return thicknessPreviewStrokeStyle.Get();
}

ID2D1StrokeStyle* BarUIRendering::GetRoundStrokeStyle()
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
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetRoundStrokeStyle] 创建圆头描边样式失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}
	return roundStrokeStyle.Get();
}

ID2D1PathGeometry* BarUIRendering::GetThicknessFineDialSelectorGeometry()
{
	if (thicknessFineDialSelectorGeometry)
		return thicknessFineDialSelectorGeometry.Get();
	auto factory = SharedD2DFactory();
	if (!factory || thicknessFineDialSelectorUnavailable) return nullptr;

	ComPtr<ID2D1PathGeometry> geometry;
	HRESULT hr = factory->CreatePathGeometry(&geometry);
	if (SUCCEEDED(hr))
	{
		ComPtr<ID2D1GeometrySink> sink;
		hr = geometry->Open(&sink);
		if (SUCCEEDED(hr))
		{
			// 单位三角只创建一次，绘制时通过矩阵镜像到选择轴上下两侧。
			sink->BeginFigure(
				D2D1::Point2F(-0.5F, 0.0F), D2D1_FIGURE_BEGIN_FILLED);
			sink->AddLine(D2D1::Point2F(0.5F, 0.0F));
			sink->AddLine(D2D1::Point2F(0.0F, 1.0F));
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			hr = sink->Close();
		}
	}
	if (FAILED(hr))
	{
		thicknessFineDialSelectorUnavailable = true;
		if (!thicknessFineDialSelectorFailureLogged)
		{
			thicknessFineDialSelectorFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetThicknessFineDialSelectorGeometry] 创建 FineDial selector 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}
	thicknessFineDialSelectorGeometry = move(geometry);
	return thicknessFineDialSelectorGeometry.Get();
}

BarUIRendering::ThicknessFineDialLabelCacheClass*
BarUIRendering::GetThicknessFineDialLabelLayout(int value, FLOAT zoom)
{
	auto dwriteFactory = SharedDWriteFactory();
	auto fontCollection = SharedFontCollection();
	if (!dwriteFactory || !barUISetClass
		|| !barUISetClass->barMedia.formatCache
		|| !isfinite(zoom) || zoom <= 0.0F)
		return nullptr;
	++thicknessFineDialLabelUseSerial;
	for (auto& cached : thicknessFineDialLabelCache)
	{
		if (cached.valid && cached.value == value
			&& abs(cached.zoom - zoom) <= 0.0001F && cached.layout)
		{
			cached.lastUse = thicknessFineDialLabelUseSerial;
			return &cached;
		}
	}

	auto* target = &thicknessFineDialLabelCache.front();
	for (auto& cached : thicknessFineDialLabelCache)
	{
		if (!cached.valid)
		{
			target = &cached;
			break;
		}
		if (cached.lastUse < target->lastUse) target = &cached;
	}
	FLOAT fontSize = static_cast<FLOAT>(
		BarThicknessFineDialLabelFontSizeDip) * zoom;
	IDWriteTextFormat* format =
		barUISetClass->barMedia.formatCache->GetFormat(
			L"HarmonyOS Sans SC", fontSize,
			fontCollection.Get(), DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			L"zh-cn", DWRITE_TEXT_ALIGNMENT_CENTER,
			DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
	if (!format) return nullptr;

	wchar_t text[16]{};
	int length = _snwprintf_s(text, _countof(text), _TRUNCATE, L"%d", value);
	if (length <= 0) return nullptr;
	ComPtr<IDWriteTextLayout> layout;
	HRESULT hr = dwriteFactory->CreateTextLayout(
		text, static_cast<UINT32>(length), format,
		64.0F * zoom, 20.0F * zoom, &layout);
	if (FAILED(hr)) return nullptr;
	layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	DWRITE_TEXT_METRICS metrics{};
	if (FAILED(layout->GetMetrics(&metrics))) return nullptr;

	*target = {};
	target->value = value;
	target->zoom = zoom;
	target->size = D2D1::SizeF(
		ceil(metrics.widthIncludingTrailingWhitespace),
		ceil(metrics.height));
	// CENTER 对齐作用于完整 layout box，绘制原点必须按同一宽度居中。
	target->layoutWidth = metrics.layoutWidth;
	target->lastUse = thicknessFineDialLabelUseSerial;
	target->valid = true;
	target->layout = move(layout);
	return target;
}

BarUIRendering::FrameDiffuseMaskCacheClass* BarUIRendering::GetRoundedRectDiffuseMask(
	ID2D1DeviceContext* deviceContext,
	const D2D1_ROUNDED_RECT& roundedRect, FLOAT strokeWidth)
{
	if (!deviceContext || strokeWidth <= 0.0F || !barUISetClass
		|| frameDiffuseMaskUnavailable) return nullptr;
	FLOAT standardDeviation = static_cast<FLOAT>(
		BarRenderingAttribute::pointLightDiffuseExtraWidth / 6.0
		* frameZoom);
	if (standardDeviation <= 0.0F) return nullptr;

	auto QuantizeQuarter = [](FLOAT value) -> int
		{
			return max(0, static_cast<int>(lround(static_cast<double>(value) * 4.0)));
		};
	int radiusXQuarter = QuantizeQuarter(roundedRect.radiusX);
	int radiusYQuarter = QuantizeQuarter(roundedRect.radiusY);
	int strokeWidthQuarter = max(1, QuantizeQuarter(strokeWidth));
	int standardDeviationQuarter = max(1, QuantizeQuarter(standardDeviation));
	for (auto& cache : frameDiffuseMaskCache)
	{
		if (cache.radiusXQuarter == radiusXQuarter
			&& cache.radiusYQuarter == radiusYQuarter
			&& cache.strokeWidthQuarter == strokeWidthQuarter
			&& cache.standardDeviationQuarter == standardDeviationQuarter)
		{
			return &cache;
		}
	}

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
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetRoundedRectDiffuseMask] 创建遮罩缓存 DeviceContext 失败, hr=0x{:08X}",
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
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetRoundedRectDiffuseMask] 创建 Gaussian Blur Effect 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameMaskDeviceContext || !frameGaussianBlurEffect) return nullptr;

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
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.width))),
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.height))));
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
	ComPtr<ID2D1SolidColorBrush> sourceBrush;
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White), &sourceBrush);
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
			&localRoundedRect, sourceBrush.Get(), cachedStrokeWidth);
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
		frameDiffuseMaskUnavailable = true;
		if (!frameDiffuseMaskFailureLogged)
		{
			frameDiffuseMaskFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetRoundedRectDiffuseMask] 创建预模糊遮罩失败，本设备停用柔光遮罩, hr=0x{:08X}",
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
	return &frameDiffuseMaskCache.back();
}

unsigned int BarUIRendering::FillRoundedRectDiffuseMaskSlices(
	ID2D1DeviceContext* deviceContext, ID2D1Bitmap* bitmap,
	ID2D1Brush* brush, const FLOAT* sourceX, const FLOAT* sourceY,
	const FLOAT* destinationX, const FLOAT* destinationY,
	int segmentCount)
{
	if (!deviceContext || !bitmap || !brush || !sourceX || !sourceY
		|| !destinationX || !destinationY || segmentCount <= 0) return 0;
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
			deviceContext->FillOpacityMask(
				bitmap, brush, &destinationRect, &sourceRect);
			++fillCount;
		}
	}
	return fillCount;
}

HRESULT BarUIRendering::CreateRoundedRectExactMask(
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
	const FLOAT middleLeft = min(
		rectLeft + key.radiusX, (rectLeft + rectRight) * 0.5F);
	const FLOAT middleRight = max(
		rectRight - key.radiusX, middleLeft);
	const FLOAT middleTop = min(
		rectTop + key.radiusY, (rectTop + rectBottom) * 0.5F);
	const FLOAT middleBottom = max(
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

BarUIRendering::FrameDiffuseExactMaskSelectionClass
BarUIRendering::ResolveRoundedRectExactMask(
	ID2D1DeviceContext* deviceContext,
	FrameDiffuseMaskCacheClass& mask,
	const D2D1_ROUNDED_RECT& roundedRect)
{
	FrameDiffuseExactMaskSelectionClass selection;
	auto Fallback = [&]()
		{
			return selection;
		};
	if (!deviceContext || !mask.bitmap || !frameMaskDeviceContext
		|| frameDiffuseExactMaskUnavailable
		|| !isfinite(frameDiffuseMaskGeometryScale)
		|| abs(frameDiffuseMaskGeometryScale - 1.0) > 0.001)
		return Fallback();

	const FLOAT radiusX = max(0.0F, roundedRect.radiusX);
	const FLOAT radiusY = max(0.0F, roundedRect.radiusY);
	const bool geometryScaled = abs(radiusX - mask.radiusX) > 0.001F
		|| abs(radiusY - mask.radiusY) > 0.001F;
	if (geometryScaled || !isfinite(radiusX) || !isfinite(radiusY)
		|| roundedRect.rect.right <= roundedRect.rect.left
		|| roundedRect.rect.bottom <= roundedRect.rect.top)
		return Fallback();

	FLOAT dpiX = 0.0F;
	FLOAT dpiY = 0.0F;
	deviceContext->GetDpi(&dpiX, &dpiY);
	if (!isfinite(dpiX) || !isfinite(dpiY)
		|| dpiX != 96.0F || dpiY != 96.0F) return Fallback();

	D2D1_MATRIX_3X2_F transform{};
	deviceContext->GetTransform(&transform);
	if (transform._11 != 1.0F || transform._12 != 0.0F
		|| transform._21 != 0.0F || transform._22 != 1.0F
		|| transform._31 != 0.0F || transform._32 != 0.0F)
		return Fallback();

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
		return Fallback();

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
		return Fallback();

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
		return Fallback();

	for (auto& ready : mask.exactMasks)
	{
		if (!ready.bitmap || !ready.key.Matches(key)) continue;
		ready.lastUse = ++frameDiffuseExactMaskUseSerial;
		selection.cache = &ready;
		return selection;
	}
	if (frameDiffuseMaskFrameSerial == 0) return Fallback();

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
		return Fallback();

	ComPtr<ID2D1Bitmap1> exactBitmap;
	HRESULT hr = CreateRoundedRectExactMask(mask, key, exactBitmap);
	if (FAILED(hr) || !exactBitmap)
	{
		// 可选整图失败仅关闭整图路径，父级预模糊九宫格继续承担本会话渲染。
		frameDiffuseExactMaskUnavailable = true;
		if (IDTLogger) IDTLogger->warn(
			"[BarUIRendering::ResolveRoundedRectExactMask] 创建精确 A8 遮罩失败，回退预模糊九宫格, hr=0x{:08X}",
			static_cast<unsigned int>(hr));
		return Fallback();
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
		if (!oldestParent) return Fallback();
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

void BarUIRendering::DrawRoundedRectDiffuseMask(ID2D1DeviceContext* deviceContext,
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
	FLOAT destinationRadiusX = max(0.0F, roundedRect.radiusX);
	FLOAT destinationRadiusY = max(0.0F, roundedRect.radiusY);
	FLOAT destinationMiddleLeft = min(
		roundedRect.rect.left + destinationRadiusX,
		(roundedRect.rect.left + roundedRect.rect.right) * 0.5F);
	FLOAT destinationMiddleRight = max(
		roundedRect.rect.right - destinationRadiusX, destinationMiddleLeft);
	FLOAT destinationMiddleTop = min(
		roundedRect.rect.top + destinationRadiusY,
		(roundedRect.rect.top + roundedRect.rect.bottom) * 0.5F);
	FLOAT destinationMiddleBottom = max(
		roundedRect.rect.bottom - destinationRadiusY, destinationMiddleTop);

	D2D1_ANTIALIAS_MODE originalAntialiasMode = deviceContext->GetAntialiasMode();
	deviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	if (exactMask.cache && exactMask.cache->bitmap)
	{
		D2D1_RECT_F sourceRect = D2D1::RectF(
			0.0F, 0.0F,
			static_cast<FLOAT>(exactMask.cache->key.width),
			static_cast<FLOAT>(exactMask.cache->key.height));
		deviceContext->FillOpacityMask(
			exactMask.cache->bitmap.Get(), brush,
			&exactMask.destination, &sourceRect);
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

BarUIRendering::FrameGeometryDiffuseMaskCacheClass* BarUIRendering::GetGeometryDiffuseMask(
	ID2D1DeviceContext* deviceContext, ID2D1Geometry* geometry,
	FLOAT strokeWidth, int geometryVariantQuarter)
{
	if (!deviceContext || !geometry || strokeWidth <= 0.0F || !barUISetClass)
		return nullptr;
	if (frameDiffuseMaskUnavailable) return nullptr;
	D2D1_RECT_F geometryBounds{};
	HRESULT hr = geometry->GetBounds(nullptr, &geometryBounds);
	if (FAILED(hr)) return nullptr;
	FLOAT width = geometryBounds.right - geometryBounds.left;
	FLOAT height = geometryBounds.bottom - geometryBounds.top;
	if (width <= 0.0F || height <= 0.0F) return nullptr;

	FLOAT standardDeviation = static_cast<FLOAT>(
		BarRenderingAttribute::pointLightDiffuseExtraWidth / 6.0
		* frameZoom);
	auto QuantizeQuarter = [](FLOAT value) -> int
		{
			return max(0, static_cast<int>(lround(static_cast<double>(value) * 4.0)));
		};
	int widthQuarter = max(1, QuantizeQuarter(width));
	int heightQuarter = max(1, QuantizeQuarter(height));
	int strokeWidthQuarter = max(1, QuantizeQuarter(strokeWidth));
	int standardDeviationQuarter = max(1, QuantizeQuarter(standardDeviation));
	for (auto& cache : frameGeometryDiffuseMaskCache)
	{
		if (cache.widthQuarter == widthQuarter
			&& cache.heightQuarter == heightQuarter
			&& cache.geometryVariantQuarter == geometryVariantQuarter
			&& cache.strokeWidthQuarter == strokeWidthQuarter
			&& cache.standardDeviationQuarter == standardDeviationQuarter)
		{
			return &cache;
		}
	}

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
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetGeometryDiffuseMask] 创建遮罩缓存 DeviceContext 失败, hr=0x{:08X}",
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
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetGeometryDiffuseMask] 创建 Gaussian Blur Effect 失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
	}
	if (!frameMaskDeviceContext || !frameGaussianBlurEffect) return nullptr;

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
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.width))),
		max<UINT32>(1, static_cast<UINT32>(ceilf(cache.size.height))));
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
	ComPtr<ID2D1SolidColorBrush> sourceBrush;
	if (SUCCEEDED(hr))
		hr = frameMaskDeviceContext->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White), &sourceBrush);
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
			geometry, sourceBrush.Get(), cachedStrokeWidth);
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
		frameDiffuseMaskUnavailable = true;
		if (!frameDiffuseMaskFailureLogged)
		{
			frameDiffuseMaskFailureLogged = true;
			if (IDTLogger) IDTLogger->error(
				"[BarUIRendering::GetGeometryDiffuseMask] 创建几何预模糊遮罩失败，本设备停用柔光遮罩, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
		}
		return nullptr;
	}

	cache.bitmap = move(outputBitmap);
	if (frameGeometryDiffuseMaskCache.size() >= 24)
		frameGeometryDiffuseMaskCache.erase(frameGeometryDiffuseMaskCache.begin());
	frameGeometryDiffuseMaskCache.emplace_back(move(cache));
	frameDiffuseMaskCreatedThisFrame = true;
	return &frameGeometryDiffuseMaskCache.back();
}

void BarUIRendering::DrawGeometryDiffuseMask(ID2D1DeviceContext* deviceContext,
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
	deviceContext->FillOpacityMask(
		mask.bitmap.Get(), brush, &destinationRect, &sourceRect);
	deviceContext->SetAntialiasMode(originalAntialiasMode);
}

bool BarUIRendering::DrawPointLightFrame(ID2D1DeviceContext* deviceContext, COLORREF color,
	BarUiFrameLightColorEnum frameLightColor,
	bool primaryLightEnabled, double cursorLightIntensityScale,
	double baseFramePct, double lightPct, FLOAT strokeWidth,
	const D2D1_ROUNDED_RECT* roundedRect,
	ID2D1Geometry* geometry, int geometryVariantQuarter)
{
	if (!deviceContext || (!roundedRect && !geometry) || strokeWidth <= 0.0F || frameLightRadius <= 0.0F)
		return false;

	FLOAT baseOpacity = static_cast<FLOAT>(clamp(baseFramePct, 0.0, 1.0));
	FLOAT lightOpacity = static_cast<FLOAT>(clamp(lightPct, 0.0, 1.0));
	if (baseOpacity <= 0.0F && lightOpacity <= 0.0F) return true;

	bool useDrawingLightTransition =
		frameLightColor == BarUiFrameLightColorEnum::PenWhenDrawing;
	double penColorBlend = useDrawingLightTransition
		? clamp(frameDrawingPenColorBlend, 0.0, 1.0) : 0.0;
	if (useDrawingLightTransition)
		lightOpacity *= static_cast<FLOAT>(
			clamp(frameDrawingLightOpacity, 0.0, 1.0));
	COLORREF lightColor = color;
	if (penColorBlend > 0.0)
		lightColor = MixBarUiColor(color, frameDrawingPenColor, penColorBlend);
	FLOAT diffuseOpacity = static_cast<FLOAT>(
		BarBorderFrameDiffuseOpacity
		+ (BarBorderPenDiffuseOpacity - BarBorderFrameDiffuseOpacity) * penColorBlend);
	ID2D1RadialGradientBrush* primaryBrush = nullptr;
	ID2D1RadialGradientBrush* cursorBrush = nullptr;
	FLOAT cursorLightIntensity = frameCursorLightIntensity
		* static_cast<FLOAT>(clamp(cursorLightIntensityScale, 0.0, 1.0));
	bool edgeLightingEnabled = BarUiEdgeLightingEnabled;
	D2D1_RECT_F lightBounds{};
	if (roundedRect) lightBounds = roundedRect->rect;
	else if (geometry && FAILED(geometry->GetBounds(nullptr, &lightBounds)))
		return false;
	FLOAT lightBoundsOutset = strokeWidth
		+ static_cast<FLOAT>(
			BarRenderingAttribute::pointLightDiffuseExtraWidth
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
	bool drawPrimaryLight = edgeLightingEnabled
		&& lightOpacity > 0.0F && primaryLightEnabled
		&& LightIntersectsBounds(
			framePrimaryLight, frameLightRadius, frameLightRadius);
	bool drawCursorLight = edgeLightingEnabled
		&& lightOpacity > 0.0F && frameCursorLightVisible
		&& cursorLightIntensity > 0.0F
		&& LightIntersectsBounds(frameLocalCursorLight,
			frameLocalCursorLightRadiusX, frameLocalCursorLightRadiusY);
	if (drawPrimaryLight)
		primaryBrush = GetFrameGradientBrush(
			deviceContext, lightColor, BarBorderLightSourceEnum::Primary);
	if (drawCursorLight)
		cursorBrush = GetFrameGradientBrush(
			deviceContext, lightColor, BarBorderLightSourceEnum::Cursor);
	if ((drawPrimaryLight && !primaryBrush) || (drawCursorLight && !cursorBrush)) return false;

	ID2D1SolidColorBrush* baseFrameBrush = nullptr;
	if (baseOpacity > 0.0F)
	{
		baseFrameBrush = GetFrameSolidColorBrush(deviceContext, color, baseOpacity);
		if (!baseFrameBrush) return false;
	}

	auto DrawLightPass = [&](ID2D1RadialGradientBrush* brush, FLOAT intensity, FLOAT width)
		{
			if (!brush || intensity <= 0.0F) return;
			brush->SetOpacity(clamp(lightOpacity * intensity, 0.0F, 1.0F));
			if (roundedRect) deviceContext->DrawRoundedRectangle(roundedRect, brush, width);
			else deviceContext->DrawGeometry(geometry, brush, width);
		};
	deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	// 点光范围之外仍完整保留原边框，光源只在基础灰边上增加强调。
	if (baseFrameBrush)
	{
		if (roundedRect) deviceContext->DrawRoundedRectangle(roundedRect, baseFrameBrush, strokeWidth);
		else deviceContext->DrawGeometry(geometry, baseFrameBrush, strokeWidth);
	}

	if (drawPrimaryLight || drawCursorLight)
	{
		FLOAT diffuseSourceOpacity = static_cast<FLOAT>(clamp(
			diffuseOpacity / BarBorderGaussianCenterCoverage, 0.0, 1.0));
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
						exactMask, *roundedRect, primaryBrush,
						CompositeOpacity(lightOpacity * diffuseSourceOpacity));
				}
				if (drawCursorLight)
				{
					DrawRoundedRectDiffuseMask(deviceContext, *diffuseMask,
						exactMask, *roundedRect, cursorBrush,
						CompositeOpacity(lightOpacity * cursorLightIntensity
							* diffuseSourceOpacity));
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
							geometryBounds, primaryBrush,
							CompositeOpacity(lightOpacity * diffuseSourceOpacity));
					}
					if (drawCursorLight)
					{
						DrawGeometryDiffuseMask(deviceContext, *diffuseMask,
							geometryBounds, cursorBrush,
							CompositeOpacity(lightOpacity * cursorLightIntensity
								* diffuseSourceOpacity));
					}
				}

			}
		}

	}
	DrawLightPass(primaryBrush, static_cast<FLOAT>(BarBorderLightIntensity), strokeWidth);
	DrawLightPass(cursorBrush, cursorLightIntensity, strokeWidth);
	return true;
}

bool BarUIRendering::Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, RECT* targetRect, bool clip)
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

	double tarRw = 0.0;
	double tarRh = 0.0;
	if (shape.rw.has_value()) tarRw = shape.rw.value().val;
	if (shape.rh.has_value()) tarRh = shape.rh.value().val;

	FLOAT tarZoom = static_cast<FLOAT>(frameZoom);
	D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(static_cast<FLOAT>(tarX) * tarZoom, static_cast<FLOAT>(tarY) * tarZoom, static_cast<FLOAT>(tarX + tarW) * tarZoom, static_cast<FLOAT>(tarY + tarH) * tarZoom), static_cast<FLOAT>(tarRw) * tarZoom, static_cast<FLOAT>(tarRh) * tarZoom);

	// Clip
	if (clip)
	{
		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, RGB(0, 0, 0), 0.0);
		if (!fillBrush) return false;
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		deviceContext->FillRoundedRectangle(&roundedRect, fillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}
	// 渲染到 DC
	{
		// 渲染填充
		if (shape.fill.has_value() && tarPct > 0.0)
		{
			COLORREF fill = shape.fill.value().val;
			ID2D1SolidColorBrush* fillBrush =
				GetFrameSolidColorBrush(deviceContext, fill, tarPct);
			if (!fillBrush) return false;
			deviceContext->FillRoundedRectangle(&roundedRect, fillBrush);
		}
		// 渲染边框
		if (shape.frame.has_value())
		{
			COLORREF frame = shape.frame.value().val;
			double tarFramePct = tarPct;
			if (shape.framePct.has_value()) tarFramePct = shape.framePct.value().val;
			double tarFrameLightPct = shape.frameLightPct.has_value()
				? frameLightPct : tarFramePct;
			if (!shape.frameLightPct.has_value()
				&& shape.frameLightOpacitySource == BarUiFrameLightOpacitySourceEnum::ObjectPct)
				tarFrameLightPct = tarPct;

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			bool shouldDraw = true;
			if (shape.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(shape.ft.value().val * tarZoom);
				shouldDraw = strokeWidth > 0.0F;
			}
			if (shouldDraw)
			{
				bool pointLightDrawn = shape.frameRendering == BarUiFrameRenderingEnum::PointLight
					&& DrawPointLightFrame(deviceContext, frame, shape.frameLightColor,
						shape.framePrimaryLightEnabled, shape.frameCursorLightIntensityScale,
						tarFramePct, tarFrameLightPct,
						strokeWidth, &roundedRect, nullptr);
				if (!pointLightDrawn)
				{
					ID2D1SolidColorBrush* borderBrush =
						GetFrameSolidColorBrush(deviceContext, frame, tarFramePct);
					if (!borderBrush) return false;
					deviceContext->DrawRoundedRectangle(
						&roundedRect, borderBrush, strokeWidth);
				}
			}
		}
	}

	if (targetRect) BarRenderingAttribute::UnionRectInPlace(*targetRect, BarRenderingAttribute::GetWeigetRect(shape, tarZoom));
	return true;
}
ID2D1Geometry* BarUIRendering::GetSuperellipseGeometry(
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
		constexpr FLOAT pi = 3.14159265359F;
		FLOAT radius = min(width, height) / 2.0F;
		int cornerSegments = max(6, segments / 4);
		vector<D2D1_POINT_2F> points;
		points.reserve(static_cast<size_t>(cornerSegments + 1) * 4 + 1);

		auto AppendCorner = [&](FLOAT centerX, FLOAT centerY, FLOAT begin, FLOAT end)
			{
				for (int i = 0; i <= cornerSegments; i++)
				{
					FLOAT theta = begin + (end - begin)
						* static_cast<FLOAT>(i) / static_cast<FLOAT>(cornerSegments);
					FLOAT cosTheta = cosf(theta);
					FLOAT sinTheta = sinf(theta);
					FLOAT localX = radius * copysignf(
						powf(abs(cosTheta), 2.0F / n), cosTheta);
					FLOAT localY = radius * copysignf(
						powf(abs(sinTheta), 2.0F / n), sinTheta);
					points.emplace_back(D2D1::Point2F(
						centerX + localX, centerY + localY));
				}
			};

		// 路径固定在局部原点，位置变化时只更新廉价的平移几何。
		AppendCorner(width - radius, radius, -pi / 2.0F, 0.0F);
		AppendCorner(width - radius, height - radius, 0.0F, pi / 2.0F);
		AppendCorner(radius, height - radius, pi / 2.0F, pi);
		AppendCorner(radius, radius, pi, pi * 3.0F / 2.0F);
		points.emplace_back(points.front());

		vector<D2D1_BEZIER_SEGMENT> beziers;
		int pointCount = static_cast<int>(points.size()) - 1;
		if (pointCount < 3) return nullptr;
		beziers.reserve(pointCount);
		for (int i = 0; i < pointCount; i++)
		{
			D2D1_POINT_2F p0 = points[(i - 1 + pointCount) % pointCount];
			D2D1_POINT_2F p1 = points[i];
			D2D1_POINT_2F p2 = points[(i + 1) % pointCount];
			D2D1_POINT_2F p3 = points[(i + 2) % pointCount];
			beziers.push_back({
				{ p1.x + (p2.x - p0.x) / 6.0F,
					p1.y + (p2.y - p0.y) / 6.0F },
				{ p2.x - (p3.x - p1.x) / 6.0F,
					p2.y - (p3.y - p1.y) / 6.0F },
				p2 });
		}

		ComPtr<ID2D1PathGeometry> nextLocalGeometry;
		HRESULT hr = factory->CreatePathGeometry(&nextLocalGeometry);
		if (FAILED(hr) || !nextLocalGeometry) return nullptr;
		ComPtr<ID2D1GeometrySink> sink;
		hr = nextLocalGeometry->Open(&sink);
		if (FAILED(hr) || !sink) return nullptr;
		sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_FILLED);
		sink->AddBeziers(beziers.data(), static_cast<UINT32>(beziers.size()));
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		hr = sink->Close();
		if (FAILED(hr)) return nullptr;

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
bool BarUIRendering::Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, RECT* targetRect, bool clip)
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

	double tarN = 4.0;
	if (superellipse.n.has_value()) tarN = superellipse.n.value().val;

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
		deviceContext->FillGeometry(geometry, fillBrush);
		deviceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
	}

	// 渲染到 DC
	{
		// 渲染填充
		if (superellipse.fill.has_value())
		{
			COLORREF fill = superellipse.fill.value().val;
			ID2D1SolidColorBrush* fillBrush =
				GetFrameSolidColorBrush(deviceContext, fill, tarPct);
			if (!fillBrush) return false;
			deviceContext->FillGeometry(geometry, fillBrush);
		}
		// 渲染边框
		if (superellipse.frame.has_value())
		{
			COLORREF frame = superellipse.frame.value().val;
			double tarFramePct = tarPct;
			if (superellipse.framePct.has_value()) tarFramePct = superellipse.framePct.value().val;
			double tarFrameLightPct = tarFramePct;
			if (superellipse.frameLightOpacitySource == BarUiFrameLightOpacitySourceEnum::ObjectPct)
				tarFrameLightPct = tarPct;

			FLOAT strokeWidth = 4.0f * static_cast<FLOAT>(tarZoom);
			bool shouldDraw = true;
			if (superellipse.ft.has_value())
			{
				strokeWidth = static_cast<FLOAT>(superellipse.ft.value().val * tarZoom);
				shouldDraw = strokeWidth > 0.0F;
			}
			if (shouldDraw)
			{
				bool pointLightDrawn = superellipse.frameRendering == BarUiFrameRenderingEnum::PointLight
					&& DrawPointLightFrame(deviceContext, frame, superellipse.frameLightColor,
						superellipse.framePrimaryLightEnabled,
						superellipse.frameCursorLightIntensityScale,
						tarFramePct, tarFrameLightPct,
						strokeWidth, nullptr, geometry,
						static_cast<int>(lround(tarN * 4.0)));
				if (!pointLightDrawn)
				{
					ID2D1SolidColorBrush* borderBrush =
						GetFrameSolidColorBrush(deviceContext, frame, tarFramePct);
					if (!borderBrush) return false;
					deviceContext->DrawGeometry(geometry, borderBrush, strokeWidth);
				}
			}
		}
	}

	if (targetRect) BarRenderingAttribute::UnionRectInPlace(*targetRect, BarRenderingAttribute::GetWeigetRect(superellipse, tarZoom));
	return true;
}
bool BarUIRendering::Svg(ID2D1DeviceContext* deviceContext, BarUiSVGClass& svg, const BarUiInheritClass& inh)
{
	// 判断是否启用
	if (!deviceContext || svg.enable.val == false) return false;
	if (frameZoom <= 0.0) return false;
	if (svg.w.val <= 0 || svg.h.val <= 0) return false;
	double contentScale = svg.contentScale;
	double contentPct = svg.contentPct;
	if (!isfinite(contentScale) || contentScale <= 0.0) return false;
	if (!isfinite(contentPct) || svg.pct.val * contentPct <= 0.0) return false;

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
	double tarPct = clamp(static_cast<double>(svg.pct.val) * contentPct, 0.0, 1.0);

	// 尺寸与内容缩放动画只改变目标矩形，SVG 位图尽量复用到稳定帧。
	ComPtr<ID2D1Bitmap> d2dBitmap;
	{
		bool colorChanged =
			(svg.color1.has_value() && svg.cColor1 != svg.color1.value().val)
			|| (svg.color2.has_value() && svg.cColor2 != svg.color2.value().val);
		bool sizeChanged = abs(svg.cW - baseW) > BarSvgRasterSizeEpsilon
			|| abs(svg.cH - baseH) > BarSvgRasterSizeEpsilon;
		bool transformAnimating = !svg.w.IsSame() || !svg.h.IsSame()
			|| abs(contentScale - 1.0) > BarSvgRasterSizeEpsilon
			|| abs(contentPct - 1.0) > BarSvgRasterSizeEpsilon;
		bool materiallyUpscaled = svg.cacheBitmap
			&& (tarW > svg.cW * BarSvgRasterUpscaleThreshold
				|| tarH > svg.cH * BarSvgRasterUpscaleThreshold);

		bool needUpdate = !svg.cacheBitmap || colorChanged
			|| (!transformAnimating && sizeChanged)
			|| (transformAnimating && materiallyUpscaled);
		if (needUpdate)
		{
			double rasterW = baseW;
			double rasterH = baseH;
			if (transformAnimating && materiallyUpscaled)
			{
				// 尺寸动画可直接按终点预建，避免后续中间帧再次跨过质量阈值。
				double targetW = static_cast<double>(svg.w.tar) * tarZoom;
				double targetH = static_cast<double>(svg.h.tar) * tarZoom;
				if (isfinite(targetW) && targetW > 0.0) rasterW = max(tarW, targetW);
				else rasterW = tarW;
				if (isfinite(targetH) && targetH > 0.0) rasterH = max(tarH, targetH);
				else rasterH = tarH;
			}

			if (!svg.CacheBitmap(deviceContext, rasterW, rasterH))
			{
				// 质量刷新失败时保留已有内容；内容/颜色失效则不能显示旧语义。
				if (!svg.cacheBitmap || colorChanged) return false;
			}
		}
		d2dBitmap = svg.cacheBitmap.Get();
	}
	if (!d2dBitmap) return false;

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
		deviceContext->DrawBitmap(
			d2dBitmap.Get(),
			destRect,								// 目标矩形
			static_cast<FLOAT>(tarPct),				// 不透明度
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
			nullptr									// 源rect, null表示全部
		);
		if (transformChanged) deviceContext->SetTransform(originalTransform);
	}

	return true;
}
bool BarUIRendering::Png(ID2D1DeviceContext* deviceContext, BarUiPNGClass& png, const BarUiInheritClass& inh)
{
	if (!deviceContext || !png.enable.val) return false;
	if (frameZoom <= 0.0) return false;
	if (png.w.val <= 0.0 || png.h.val <= 0.0 || png.pct.val <= 0.0) return false;
	bool pngCacheMiss = !png.cacheBitmap;
	if (pngCacheMiss && !png.CacheBitmap(deviceContext)) return false;

	double tarZoom = frameZoom;
	double tarX = inh.x * tarZoom;
	double tarY = inh.y * tarZoom;
	double tarW = png.w.val * tarZoom;
	double tarH = png.h.val * tarZoom;
	double tarPct = clamp(static_cast<double>(png.pct.val), 0.0, 1.0);
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

	deviceContext->DrawBitmap(
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
bool BarUIRendering::Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_ALIGNMENT textAlign)
{
	// 判断是否启用
	if (word.enable.val == false) return false;
	if (frameZoom <= 0.0) return false;
	if (word.size.val <= 0) return false;
	if (word.w.val <= 0 || word.h.val <= 0) return false;
	double contentPct = clamp(static_cast<double>(word.contentPct), 0.0, 1.0);
	if (word.pct.val <= 0.0 || contentPct <= 0.0) return false;

	// 初始化绘制量
	double tarZoom = frameZoom;
	double tarX = inh.x * tarZoom; // 绘制左上角 x
	double tarY = inh.y * tarZoom; // 绘制左上角 y
	double tarW = word.w.val * tarZoom;
	double tarH = word.h.val * tarZoom;
	double tarSize = word.size.val * tarZoom;
	double contentScale = max(0.0, static_cast<double>(word.contentScale));
	double centerX = tarX + tarW / 2.0;
	double centerY = tarY + tarH / 2.0;
	tarW *= contentScale;
	tarH *= contentScale;
	tarSize *= contentScale;
	tarX = centerX - tarW / 2.0;
	tarY = centerY - tarH / 2.0;
	double tarPct = word.pct.val * contentPct; // 布局透明度与内容切换透明度相乘

	// Word 控件改为存入 wstring
	wstring tarContent = word.content.GetVal();

	// 获取样式
	IDWriteTextFormat* textFormat = nullptr;
	{
		/*IDWriteTextFormat* tmpTextFormat;
		SharedDWriteFactory()->CreateTextFormat(
			L"HarmonyOS Sans SC",
			SharedFontCollection().Get(),
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			static_cast<FLOAT>(tarSize),
			L"zh-cn",
			&tmpTextFormat
		);
		tmpTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		tmpTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

		textFormat.Attach(tmpTextFormat);*/

		textFormat = barUISetClass->barMedia.formatCache->GetFormat(
			L"HarmonyOS Sans SC",
			tarSize,
			SharedFontCollection().Get(),
			fontWeight,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			L"zh-cn",
			textAlign,
			DWRITE_PARAGRAPH_ALIGNMENT_CENTER   // 指定段落居中
		);
	}
	if (!textFormat) return false;
	// 计算区域
	D2D1_RECT_F layoutRect;
	{
		layoutRect = D2D1::RectF(
			static_cast<FLOAT>(tarX),
			static_cast<FLOAT>(tarY),
			static_cast<FLOAT>(tarX + tarW),
			static_cast<FLOAT>(tarY + tarH)
		);
	}
	// 渲染到 DC
	{
		COLORREF color = word.color.val;

		ID2D1SolidColorBrush* fillBrush =
			GetFrameSolidColorBrush(deviceContext, color, tarPct);
		if (!fillBrush) return false;

		deviceContext->DrawTextW(
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

D2D1_SIZE_F BarUIRendering::MeasureText(
	const wstring& content, double fontSize, DWRITE_FONT_WEIGHT fontWeight)
{
	D2D1_SIZE_F result = D2D1::SizeF();
	auto dwriteFactory = SharedDWriteFactory();
	if (content.empty() || !dwriteFactory || !barUISetClass
		|| !barUISetClass->barMedia.formatCache || fontSize <= 0.0)
		return result;

	IDWriteTextFormat* textFormat =
		barUISetClass->barMedia.formatCache->GetFormat(
			L"HarmonyOS Sans SC", static_cast<FLOAT>(fontSize),
			SharedFontCollection().Get(),
			fontWeight, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, L"zh-cn",
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
			maxLineLength = max(maxLineLength, currentLineLength);
			currentLineLength = 0;
			++lineCount;
		}
		else ++currentLineLength;
	}
	maxLineLength = max(maxLineLength, currentLineLength);
	result.width = static_cast<FLOAT>(maxLineLength * fontSize);
	result.height = static_cast<FLOAT>(lineCount * fontSize * 1.4);
	return result;
}
