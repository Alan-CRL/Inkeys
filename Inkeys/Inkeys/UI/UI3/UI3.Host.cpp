module;

#include <windows.h>
#include "../../Log/Log.h"
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

module Inkeys.UI.UI3;
import :Host;
import :Renderer;

import Inkeys.UI.RenderPipeline;
import Inkeys.UI.UI3.PresentationProbe;

using Microsoft::WRL::ComPtr;

namespace Inkeys::UI::UI3
{

namespace
{
	[[nodiscard]] const char* FrameTargetKindName(FrameTargetKind kind) noexcept
	{
		switch (kind)
		{
		case FrameTargetKind::PlainBitmap: return "plain-bitmap";
		case FrameTargetKind::ExternalContext: return "external-context";
		default: return "GDI-compatible";
		}
	}

	void ObserveSvgRectClip(ID2D1DeviceContext* context, const D2D1_RECT_F& rect) noexcept
	{
		if (!Inkeys::UI::UI3::CurrentUi3SvgScope()) return;
		D2D1_MATRIX_3X2_F matrix; context->GetTransform(&matrix);
		const std::uint32_t rectBits[4]{ std::bit_cast<std::uint32_t>(rect.left), std::bit_cast<std::uint32_t>(rect.top),
			std::bit_cast<std::uint32_t>(rect.right), std::bit_cast<std::uint32_t>(rect.bottom) };
		const std::uint32_t matrixBits[6]{ std::bit_cast<std::uint32_t>(matrix._11), std::bit_cast<std::uint32_t>(matrix._12),
			std::bit_cast<std::uint32_t>(matrix._21), std::bit_cast<std::uint32_t>(matrix._22),
			std::bit_cast<std::uint32_t>(matrix._31), std::bit_cast<std::uint32_t>(matrix._32) };
		Inkeys::UI::UI3::ObserveUi3SvgClipPush(context, rectBits, matrixBits);
	}

	[[nodiscard]] double ValidLayoutScale(const FrameCoordinateMapping& mapping) noexcept
	{
		return std::isfinite(mapping.layoutScale) && mapping.layoutScale > 0.0 ? mapping.layoutScale : 1.0;
	}
}

void FrameTarget::BindRenderer(UIRendering* renderer)
{
	if (renderer_ == renderer) return;
	if (renderer_) renderer_->NotifyFrameTargetDiscarded();
	renderer_ = renderer;
	if (renderer_ && deviceContext_)
	{
		// 传入的绘制器若还带着别的上下文或设备代次的缓存，先按「目标已丢弃」清理，再通告现有资源。
		if (renderer_->GetDeviceContext() && (renderer_->GetDeviceContext() != deviceContext_.Get()
			|| renderer_->GetDeviceGeneration() != deviceGeneration_))
			renderer_->NotifyFrameTargetDiscarded();
		renderer_->NotifyFrameTargetReady(deviceContext_.Get(), deviceGeneration_);
	}
}

HRESULT FrameTarget::Ensure(
	const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch, UINT32 targetWidth, UINT32 targetHeight)
{
	if (kind_ == FrameTargetKind::ExternalContext) return E_UNEXPECTED;
	if (targetWidth == 0 || targetHeight == 0) return E_INVALIDARG;
	if (epoch.generation == deviceGeneration_ && deviceContext_
		&& targetBitmap_
		&& (kind_ != FrameTargetKind::GdiCompatibleBitmap || gdiInteropRenderTarget_)
		&& pixelWidth_ == targetWidth
		&& pixelHeight_ == targetHeight)
		return S_OK;
	return Recreate(epoch, targetWidth, targetHeight);
}

HRESULT FrameTarget::Recreate(
	const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch, UINT32 targetWidth, UINT32 targetHeight)
{
	const auto fail = [&](unsigned int apiBit, const char* api, HRESULT result)
	{
		// 每设备代次、每 API 只记一次；资源丢弃不清闩锁，避免失败重试刷屏。
		if (failureGeneration_ != epoch.generation)
		{
			failureGeneration_ = epoch.generation;
			failureMask_ = 0;
		}
		if (!(failureMask_ & apiBit))
		{
			failureMask_ |= apiBit;
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"{} frame target recreate failed: api={}, size={}x{}, generation={}, hr={:#010x}",
				FrameTargetKindName(kind_), api, targetWidth, targetHeight, epoch.generation,
				static_cast<unsigned long>(result));
		}
		return result;
	};
	if (!epoch.d2dDevice) return fail(1u, "D2DDevice", E_POINTER);

	const bool gdiCompatible = kind_ == FrameTargetKind::GdiCompatibleBitmap;
	ComPtr<ID2D1DeviceContext> nextDeviceContext;
	ComPtr<ID2D1Bitmap1> nextTargetBitmap;
	ComPtr<ID2D1GdiInteropRenderTarget> nextGdiInteropRenderTarget;
	HRESULT hr = epoch.d2dDevice->CreateDeviceContext(
		D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &nextDeviceContext);
	if (FAILED(hr)) return fail(2u, "CreateDeviceContext", hr);

	const D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		gdiCompatible ? D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE
			: D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED));
	hr = nextDeviceContext->CreateBitmap(
		D2D1::SizeU(targetWidth, targetHeight), nullptr, 0,
		&bitmapProperties, &nextTargetBitmap);
	if (FAILED(hr)) return fail(4u, "CreateBitmap", hr);

	if (gdiCompatible)
	{
		hr = nextDeviceContext.As(&nextGdiInteropRenderTarget);
		if (FAILED(hr)) return fail(8u, "QueryGdiInteropRenderTarget", hr);
	}
	nextDeviceContext->SetTarget(nextTargetBitmap.Get());
	nextDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	// 新 epoch 全部资源成功后才替换旧资源，失败时保持当前帧域完整。
	Discard();
	deviceContext_ = std::move(nextDeviceContext);
	targetBitmap_ = std::move(nextTargetBitmap);
	gdiInteropRenderTarget_ = std::move(nextGdiInteropRenderTarget);
	deviceGeneration_ = epoch.generation;
	pixelWidth_ = targetWidth;
	pixelHeight_ = targetHeight;
	if (renderer_) renderer_->NotifyFrameTargetReady(deviceContext_.Get(), deviceGeneration_);
	Inkeys::Log::TryFormat(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3,
		"{} frame target recreated: size={}x{}, generation={}",
		FrameTargetKindName(kind_), pixelWidth_, pixelHeight_, deviceGeneration_);
	return S_OK;
}

HRESULT FrameTarget::AttachExternalContext(ID2D1DeviceContext* context,
	unsigned long long generation, UINT32 targetWidth, UINT32 targetHeight)
{
	if (kind_ != FrameTargetKind::ExternalContext) return E_UNEXPECTED;
	if (!context) return E_POINTER;
	if (deviceContext_.Get() != context || deviceGeneration_ != generation)
	{
		// 与位图形态的重建同序：先丢弃（绘制器清设备缓存），再通告新上下文。
		Discard();
		deviceContext_ = context;
		deviceGeneration_ = generation;
		if (renderer_) renderer_->NotifyFrameTargetReady(deviceContext_.Get(), deviceGeneration_);
		Inkeys::Log::TryFormat(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3,
			"{} frame target attached: size={}x{}, generation={}",
			FrameTargetKindName(kind_), targetWidth, targetHeight, deviceGeneration_);
	}
	pixelWidth_ = targetWidth;
	pixelHeight_ = targetHeight;
	return S_OK;
}

void FrameTarget::Discard()
{
	if (deviceContext_ || targetBitmap_ || gdiInteropRenderTarget_)
		Inkeys::Log::TryFormat(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3,
			"{} frame target discarded: size={}x{}, generation={}",
			FrameTargetKindName(kind_), pixelWidth_, pixelHeight_, deviceGeneration_);
	// 绘制器先清设备相关缓存并放下上下文引用，再释放目标资源。
	if (renderer_) renderer_->NotifyFrameTargetDiscarded();
	dirtyClipActive_ = false;
	dirtyClipRect_ = {};
	// 外部上下文的目标归调用方所有，不能替它解除。
	if (deviceContext_ && kind_ != FrameTargetKind::ExternalContext) deviceContext_->SetTarget(nullptr);
	gdiInteropRenderTarget_.Reset();
	targetBitmap_.Reset();
	deviceContext_.Reset();
	deviceGeneration_ = 0;
	pixelWidth_ = 0;
	pixelHeight_ = 0;
}

void FrameTarget::BeginFrame(ID2D1DeviceContext* context,
	const D2D1_RECT_F& dirtyRect)
{
	if (!context) return;
	if (kind_ == FrameTargetKind::ExternalContext)
	{
		// 调用方已 BeginDraw 并自行决定清空；这里只建立绘制器的帧状态与可选裁剪。
		if (externalDirtyClipEnabled_) PushDirtyClip(context, dirtyRect);
		else if (renderer_) renderer_->NotifyFrameBegunWithoutClip();
		return;
	}
	context->BeginDraw();
	PushDirtyClip(context, dirtyRect);
	const auto transparent = D2D1::ColorF(0, 0.0F);
	Inkeys::UI::UI3::ObserveUi3SvgClear(context);
	context->Clear(&transparent);
}

void FrameTarget::FinishDrawCommands(ID2D1DeviceContext* context)
{
	PopDirtyClip(context);
}

HRESULT FrameTarget::EndFrame(ID2D1DeviceContext* context)
{
	if (!context) return E_POINTER;
	// 外部上下文由调用方 EndDraw，其结果经 NotifyExternalEndDraw 交回。
	if (kind_ == FrameTargetKind::ExternalContext) return S_OK;
	const HRESULT result = context->EndDraw();
	if (renderer_) renderer_->NotifyFrameEnded(result);
	return result;
}

void FrameTarget::NotifyExternalEndDraw(HRESULT endDrawResult)
{
	if (kind_ == FrameTargetKind::ExternalContext && renderer_) renderer_->NotifyFrameEnded(endDrawResult);
}

void FrameTarget::PushDirtyClip(
	ID2D1DeviceContext* context, const D2D1_RECT_F& dirtyRect)
{
	if (!context || dirtyClipActive_) return;
	dirtyClipRect_ = dirtyRect;
	// 绘制器在裁剪推入前按当前变换记录设备范围，供内容剔除判定。
	if (renderer_) renderer_->NotifyFrameDirtyClipPushed(context, dirtyRect);
	context->PushAxisAlignedClip(
		dirtyRect, D2D1_ANTIALIAS_MODE_ALIASED);
	ObserveSvgRectClip(context, dirtyRect);
	dirtyClipActive_ = true;
}

void FrameTarget::PopDirtyClip(ID2D1DeviceContext* context)
{
	if (!context || !dirtyClipActive_) return;
	context->PopAxisAlignedClip();
	Inkeys::UI::UI3::ObserveUi3SvgClipPop(context);
	dirtyClipActive_ = false;
	if (renderer_) renderer_->NotifyFrameDirtyClipPopped();
}

FrameTransaction::FrameTransaction(FrameTarget& target, const D2D1_RECT_F& dirtyRect)
	: target_(target), context_(target.DeviceContext()), gdiInterop_(target.GdiInteropRenderTarget())
{
	// 事务持有本帧借用的 D2D/GDI 对象直到结束，目标中途重建也不会使其失效。
	if (context_) target_.BeginFrame(context_.Get(), dirtyRect);
}

FrameTransaction::~FrameTransaction()
{
	if (Active()) (void)EndWithoutPresent();
}

void FrameTransaction::FinishDrawCommands()
{
	if (!context_ || ended_ || commandsFinished_) return;
	commandsFinished_ = true;
	target_.FinishDrawCommands(context_.Get());
}

FrameTransactionResult FrameTransaction::Commit(const FramePresentSink& sink,
	const FrameCommitStepObserver& observer)
{
	FrameTransactionResult result;
	result.commitRequested = true;
	if (!Active()) return result;
	FinishDrawCommands();
	ended_ = true;
	// 提交端或分步观察回调抛出异常时仍要成对收尾：先归还 DC、再结束绘制，各执行一次，
	// 异常照常向外传播。正常路径上两项在离开前都已完成，这里不再重复。
	struct Unwind
	{
		FrameTransaction& transaction;
		bool dcHeld = false, drawEnded = false;
		~Unwind()
		{
			if (dcHeld)
			{
				const RECT gdiModifiedRect{};
				(void)transaction.gdiInterop_->ReleaseDC(&gdiModifiedRect);
			}
			if (!drawEnded) (void)transaction.target_.EndFrame(transaction.context_.Get());
		}
	} unwind{ *this };
	FramePresentSource source;
	source.pixelSize = target_.PixelSize();
	const auto present = [&]
	{
		result.presentAttempted = true;
		const FramePresentOutcome outcome = sink(source);
		result.presentSucceeded = outcome.succeeded;
		result.presentError = outcome.error;
	};
	const auto observe = [&](FrameCommitStep step, bool begin)
	{
		if (observer) observer(step, begin);
	};
	const auto endDraw = [&]
	{
		observe(FrameCommitStep::EndDraw, true);
		unwind.drawEnded = true;
		result.endDraw = target_.EndFrame(context_.Get());
		observe(FrameCommitStep::EndDraw, false);
	};
	if (target_.Kind() == FrameTargetKind::GdiCompatibleBitmap)
	{
		source.targetBitmap = target_.TargetBitmap();
		if (gdiInterop_ && sink)
		{
			// GetDC 自带必要的 D2D 提交；此前裁剪/图层栈已清空。
			HDC sourceDc = nullptr;
			observe(FrameCommitStep::GetDC, true);
			result.getDc = gdiInterop_->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &sourceDc);
			unwind.dcHeld = SUCCEEDED(result.getDc) && sourceDc;
			observe(FrameCommitStep::GetDC, false);
			if (SUCCEEDED(result.getDc) && sourceDc)
			{
				source.sourceDc = sourceDc;
				present();
				// 提交端只读取源 DC，没有经 GDI 修改像素。
				const RECT gdiModifiedRect{};
				result.releaseDcAttempted = true;
				observe(FrameCommitStep::ReleaseDC, true);
				unwind.dcHeld = false;
				result.releaseDc = gdiInterop_->ReleaseDC(&gdiModifiedRect);
				observe(FrameCommitStep::ReleaseDC, false);
			}
			else if (SUCCEEDED(result.getDc))
			{
				result.sourceDcMissing = true;
				result.getDc = E_POINTER;
			}
		}
		endDraw();
		return result;
	}
	// 没有 GDI interop 的形态：GDI 两步不适用，记为成功。
	result.getDc = S_OK;
	result.releaseDc = S_OK;
	endDraw();
	if (target_.Kind() == FrameTargetKind::PlainBitmap)
	{
		// 读回类提交端需要已结算的目标位图。
		if (FAILED(result.endDraw)) return result;
		source.targetBitmap = target_.TargetBitmap();
	}
	if (sink) present();
	else result.presentSucceeded = true;
	return result;
}

FrameTransactionResult FrameTransaction::EndWithoutPresent()
{
	FrameTransactionResult result;
	if (!Active()) return result;
	FinishDrawCommands();
	ended_ = true;
	result.endDraw = target_.EndFrame(context_.Get());
	return result;
}

void FrameTransactionLogState::Report(const char* role, const FrameTransactionResult& result) noexcept
{
	// 延后/放弃本帧没有尝试提交：只有 EndDraw 失败才算失败，成功也不能证明已从失败恢复。
	const bool failed = result.commitRequested ? !result.PixelsCommitted() : FAILED(result.endDraw);
	if (failed)
	{
		if (consecutiveFailures_ == 0)
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::UI3,
				"frame transaction failed: role={}, commit={}, getDc={:#010x}, present={}/{}, presentError={}, releaseDc={:#010x}, endDraw={:#010x}",
				role ? role : "unknown", result.commitRequested ? 1 : 0,
				static_cast<unsigned long>(result.getDc),
				result.presentSucceeded ? 1 : 0, result.presentAttempted ? 1 : 0,
				static_cast<unsigned long>(result.presentError),
				static_cast<unsigned long>(result.releaseDc),
				static_cast<unsigned long>(result.endDraw));
		if (consecutiveFailures_ != (std::numeric_limits<unsigned>::max)()) ++consecutiveFailures_;
		return;
	}
	if (!result.commitRequested || consecutiveFailures_ == 0) return;
	Inkeys::Log::TryFormat(Inkeys::Log::Level::Info, Inkeys::Log::Module::UI3,
		"frame transaction recovered: role={}, failures={}", role ? role : "unknown", consecutiveFailures_);
	consecutiveFailures_ = 0;
}

D2D1_POINT_2F LayoutToTarget(const FrameCoordinateMapping& mapping, D2D1_POINT_2F layout) noexcept
{
	const double scale = ValidLayoutScale(mapping);
	return D2D1::Point2F(static_cast<FLOAT>(layout.x * scale - mapping.targetOrigin.x),
		static_cast<FLOAT>(layout.y * scale - mapping.targetOrigin.y));
}

D2D1_POINT_2F TargetToLayout(const FrameCoordinateMapping& mapping, D2D1_POINT_2F target) noexcept
{
	const double scale = ValidLayoutScale(mapping);
	return D2D1::Point2F(static_cast<FLOAT>((target.x + mapping.targetOrigin.x) / scale),
		static_cast<FLOAT>((target.y + mapping.targetOrigin.y) / scale));
}

D2D1_POINT_2F LayoutToScreen(const FrameCoordinateMapping& mapping, D2D1_POINT_2F layout) noexcept
{
	const double scale = ValidLayoutScale(mapping);
	return D2D1::Point2F(static_cast<FLOAT>(layout.x * scale + mapping.screenOrigin.x),
		static_cast<FLOAT>(layout.y * scale + mapping.screenOrigin.y));
}

D2D1_POINT_2F ScreenToLayout(const FrameCoordinateMapping& mapping, D2D1_POINT_2F screen) noexcept
{
	const double scale = ValidLayoutScale(mapping);
	return D2D1::Point2F(static_cast<FLOAT>((screen.x - mapping.screenOrigin.x) / scale),
		static_cast<FLOAT>((screen.y - mapping.screenOrigin.y) / scale));
}

POINT TargetToScreen(const FrameCoordinateMapping& mapping, POINT target) noexcept
{
	return { target.x + mapping.targetOrigin.x + mapping.screenOrigin.x,
		target.y + mapping.targetOrigin.y + mapping.screenOrigin.y };
}

POINT ScreenToTarget(const FrameCoordinateMapping& mapping, POINT screen) noexcept
{
	return { screen.x - mapping.screenOrigin.x - mapping.targetOrigin.x,
		screen.y - mapping.screenOrigin.y - mapping.targetOrigin.y };
}

D2D1_RECT_F LayoutToTargetRect(const FrameCoordinateMapping& mapping, const D2D1_RECT_F& layout) noexcept
{
	const auto topLeft = LayoutToTarget(mapping, D2D1::Point2F(layout.left, layout.top));
	const auto bottomRight = LayoutToTarget(mapping, D2D1::Point2F(layout.right, layout.bottom));
	return D2D1::RectF(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
}

D2D1_MATRIX_3X2_F LayoutToTargetTransform(const FrameCoordinateMapping& mapping) noexcept
{
	const FLOAT scale = static_cast<FLOAT>(ValidLayoutScale(mapping));
	return D2D1::Matrix3x2F::Scale(scale, scale)
		* D2D1::Matrix3x2F::Translation(static_cast<FLOAT>(-mapping.targetOrigin.x),
			static_cast<FLOAT>(-mapping.targetOrigin.y));
}

RECT ExpandByLightingOutset(const FrameCoordinateMapping& mapping, const RECT& targetBounds) noexcept
{
	const LONG outset = mapping.lightingOutset > 0 ? mapping.lightingOutset : 0;
	return { targetBounds.left - outset, targetBounds.top - outset,
		targetBounds.right + outset, targetBounds.bottom + outset };
}

}
