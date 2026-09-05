module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tpcshrd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <mutex>
#include <tchar.h> // Tablet Pen Service 属性宏仍使用 _T。

module Inkeys.Drawing.Draw3.window_control;

#if defined(DRAW3_RTS_DIAGNOSTICS)
import Inkeys.Drawing.Draw3.diagnostics;
#endif

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr UINT kApplySystemCursorMessage = WM_APP + 1;
		constexpr LONG_PTR kPromotedPointerSignatureMask = 0xFFFFFF00;
		constexpr LONG_PTR kPromotedPointerSignature = 0xFF515700;
		constexpr uint64_t kTouchInputBarrierKnownMask = uint64_t{ 1 } << 32u;

		constexpr DWORD kTabletInputFlags =
			TABLET_ENABLE_MULTITOUCHDATA |
			TABLET_DISABLE_PRESSANDHOLD |
			TABLET_DISABLE_PENTAPFEEDBACK |
			TABLET_DISABLE_PENBARRELFEEDBACK |
			TABLET_DISABLE_FLICKS;

		using GetPointerPenInfoFunction = BOOL(WINAPI*)(
			UINT32 pointerId, POINTER_PEN_INFO* penInfo);
		using GetPointerInfoFunction = BOOL(WINAPI*)(
			UINT32 pointerId, POINTER_INFO* pointerInfo);
		using GetPointerTypeFunction = BOOL(WINAPI*)(
			UINT32 pointerId, POINTER_INPUT_TYPE* pointerType);

		GetPointerTypeFunction ResolveGetPointerType() noexcept
		{
			static const GetPointerTypeFunction getPointerType = []() noexcept
				{
					const HMODULE user32 = GetModuleHandleW(L"user32.dll");
					return user32 ? reinterpret_cast<GetPointerTypeFunction>(
						GetProcAddress(user32, "GetPointerType")) : nullptr;
				}();
			return getPointerType;
		}

		struct PointerDetails
		{
			bool typeKnown = false;
			POINTER_INPUT_TYPE type = PT_POINTER;
			bool penInfoKnown = false;
			bool positionKnown = false;
			POINT screenPosition = {};
			bool eraserHint = false;
			bool inContact = false;
		};

		PointerDetails QueryPointerDetails(uint32_t pointerId) noexcept
		{
			PointerDetails details;
			// Win7 没有 Pointer Pen API，必须动态解析，缺失时继续使用当前工具。
			static const GetPointerPenInfoFunction getPointerPenInfo = []() noexcept
				{
					const HMODULE user32 = GetModuleHandleW(L"user32.dll");
					return user32 ? reinterpret_cast<GetPointerPenInfoFunction>(
						GetProcAddress(user32, "GetPointerPenInfo")) : nullptr;
				}();
			const GetPointerTypeFunction getPointerType = ResolveGetPointerType();
			if (!getPointerType || !getPointerType(pointerId, &details.type)) return details;
			details.typeKnown = true;
			static const GetPointerInfoFunction getPointerInfo = []() noexcept
				{
					const HMODULE user32 = GetModuleHandleW(L"user32.dll");
					return user32 ? reinterpret_cast<GetPointerInfoFunction>(
						GetProcAddress(user32, "GetPointerInfo")) : nullptr;
				}();
			if (getPointerInfo)
			{
				POINTER_INFO pointerInfo = {};
				if (getPointerInfo(pointerId, &pointerInfo) &&
					pointerInfo.pointerType == details.type)
				{
					details.positionKnown = true;
					details.screenPosition = pointerInfo.ptPixelLocation;
				}
			}
			if (details.type != PT_PEN || !getPointerPenInfo) return details;

			POINTER_PEN_INFO penInfo = {};
			if (!getPointerPenInfo(pointerId, &penInfo) ||
				penInfo.pointerInfo.pointerType != PT_PEN) return details;
			details.penInfoKnown = true;
			details.positionKnown = true;
			details.screenPosition = penInfo.pointerInfo.ptPixelLocation;
			details.eraserHint =
				(penInfo.penFlags & (PEN_FLAG_INVERTED | PEN_FLAG_ERASER)) != 0;
			details.inContact =
				(penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
			return details;
		}

		bool IsPromotedPointerMouseMessage(ULONG_PTR extraInfo) noexcept
		{
			return (extraInfo & kPromotedPointerSignatureMask) ==
				kPromotedPointerSignature;
		}

		DrawingCursorPointerAuthority AuthorityForPointerType(
			POINTER_INPUT_TYPE pointerType) noexcept
		{
			switch (pointerType)
			{
			case PT_PEN: return DrawingCursorPointerAuthority::Pen;
			case PT_MOUSE: return DrawingCursorPointerAuthority::Mouse;
			case PT_TOUCH: return DrawingCursorPointerAuthority::Touch;
			default: return DrawingCursorPointerAuthority::Unknown;
			}
		}

#if defined(DRAW3_RTS_DIAGNOSTICS)
		const char* MouseMessageName(UINT message) noexcept
		{
			switch (message)
			{
			case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
			case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
			case WM_LBUTTONUP: return "WM_LBUTTONUP";
			case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
			case WM_RBUTTONUP: return "WM_RBUTTONUP";
			case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
			case WM_MBUTTONUP: return "WM_MBUTTONUP";
			case WM_MOUSELEAVE: return "WM_MOUSELEAVE";
			case WM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
			default: return "WM_MOUSE_UNKNOWN";
			}
		}

		const char* PointerTypeName(POINTER_INPUT_TYPE pointerType,
			bool typeKnown) noexcept
		{
			if (!typeKnown) return "unknown";
			switch (pointerType)
			{
			case PT_PEN: return "pen";
			case PT_MOUSE: return "mouse";
			case PT_TOUCH: return "touch";
			case PT_TOUCHPAD: return "touchpad";
			default: return "pointer";
			}
		}

		const char* SystemCursorDecisionReason(
			DrawingCursorPointerAuthority authority, DrawingTool tool,
			bool penSampleValid, bool mouseSampleValid,
			bool mouseUsesSystemCursor, bool touchPanActive,
			bool realMouseTakeover, bool hide) noexcept
		{
			if (!hide) return "default-arrow";
			if (touchPanActive && !realMouseTakeover) return "touch-pan";
			if (authority == DrawingCursorPointerAuthority::Pen) return "pen-authority";
			if (tool == DrawingTool::Eraser) return "eraser-tool";
			if (tool == DrawingTool::Laser) return "laser-tool";
			if (!mouseUsesSystemCursor && mouseSampleValid) return "mouse-app-cursor";
			if (penSampleValid) return "pen-sample-fallback";
			return "hide-policy";
		}
#endif

	}

#if defined(DRAW3_RTS_DIAGNOSTICS)
	void WindowController::SetRtsTraceEnabled(bool enabled) noexcept
	{
		ConfigureRtsTrace(enabled);
		ConfigureDrawingCursorTrace(enabled);
		{
			std::lock_guard<std::mutex> lock(cursorTraceMutex_);
			lastCursorTraceStateKnown_ = false;
			lastSystemCursorDecisionKnown_ = false;
		}
		cursorTraceEnabled_.store(enabled, std::memory_order_release);
	}
#endif

	WindowController::~WindowController()
	{
		// HWND 生命周期由 Window Service 管理；这里只解绑外部附着状态。
		DetachExternal();
	}

	bool WindowController::AttachExternal(HWND window, ExternalWindowCallbacks callbacks)
	{
		(void)callbacks; // 产品样式由 HostStyleCallbacks/Window Service 负责。
		if (!window || !IsWindow(window)) return false;
		if (externalWindow_.load(std::memory_order_acquire)) return false;
		{
			// Host generation 之间不得继承上一代异步 completion 或画布命令。
			const std::scoped_lock lock(canvasCommandMutex_);
			canvasCommands_.clear();
		}
		RECT client = {};
		if (!GetClientRect(window, &client)) return false;
		exitRequested_.store(false, std::memory_order_release);
		resizeRequested_.store(false, std::memory_order_release);
		fullPresentRequested_.store(false, std::memory_order_release);
		compositionChangedRequested_.store(false, std::memory_order_release);
		drawingCursorRenderRequested_.store(false, std::memory_order_release);
		activationAllowed_.store(false, std::memory_order_release);
		window_.store(window, std::memory_order_release);
		size_.width = (std::max)(0L, client.right - client.left);
		size_.height = (std::max)(0L, client.bottom - client.top);
		defaultCursor_ = LoadCursorW(nullptr, IDC_ARROW);
		// RTS 多点属性必须在第一个 contact 前发布到外部 HWND。
		const ATOM tabletPropertyAtom = GlobalAddAtom(MICROSOFT_TABLETPENSERVICE_PROPERTY);
		(void)SetProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY,
			reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kTabletInputFlags)));
		if (tabletPropertyAtom) GlobalDeleteAtom(tabletPropertyAtom);
		// 其余附着状态就绪后再开放 WndProc 转发，避免观察到半初始化 HWND。
		externalWindow_.store(true, std::memory_order_release);
		return true;
	}

	void WindowController::DetachExternal() noexcept
	{
		// 先关闭新消息入口，再清理 HWND 属性和输入状态。
		const bool wasAttached = externalWindow_.exchange(false, std::memory_order_acq_rel);
		{
			const std::scoped_lock lock(canvasCommandMutex_);
			canvasCommands_.clear();
		}
		if (!wasAttached) return;
		if (const HWND window = window_.load(std::memory_order_acquire))
		{
			// 外部 HWND 由 Window Service 销毁；这里只撤销 Draw3 自己写入的属性。
			RemovePropW(window, MICROSOFT_TABLETPENSERVICE_PROPERTY);
		}
		inputCoordinator_.store(nullptr, std::memory_order_release);
		activationAllowed_.store(false, std::memory_order_release);
		penCursorSample_.Clear();
		mouseCursorSample_.Clear();
		cursorOwner_.store(DrawingCursorPointerAuthority::Unknown,
			std::memory_order_release);
		window_.store(nullptr, std::memory_order_release);
	}

	LRESULT WindowController::HandleExternalMessage(
		HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!externalWindow_.load(std::memory_order_acquire) ||
			window_.load(std::memory_order_acquire) != window)
			return DefWindowProcW(window, message, wParam, lParam);
		return HandleWindowMessage(window, message, wParam, lParam);
	}

	HWND WindowController::Handle() const
	{
		return window_.load(std::memory_order_acquire);
	}

	WindowSize WindowController::Size() const
	{
		return size_;
	}

	void WindowController::CommitSize(int width, int height)
	{
		size_.width = width;
		size_.height = height;
	}

	bool WindowController::TryDequeueCanvasCommand(CanvasCommand& command)
	{
		const std::scoped_lock lock(canvasCommandMutex_);
		if (canvasCommands_.empty()) return false;
		command = canvasCommands_.front();
		canvasCommands_.pop_front();
		return true;
	}

	bool WindowController::ConsumeResizeRequest(WindowSize& size)
	{
		if (!resizeRequested_.exchange(false, std::memory_order_acquire)) return false; // 消费一次跨线程尺寸请求。
		size.width = pendingResizeWidth_.load(std::memory_order_relaxed);
		size.height = pendingResizeHeight_.load(std::memory_order_relaxed);
		return true;
	}

	bool WindowController::ConsumeFullPresentRequest()
	{
		return fullPresentRequested_.exchange(false, std::memory_order_acquire);
	}

	bool WindowController::ConsumeCompositionChangedRequest()
	{
		return compositionChangedRequested_.exchange(false, std::memory_order_acquire);
	}

	bool WindowController::ConsumeDrawingCursorRenderRequest()
	{
		return drawingCursorRenderRequested_.exchange(false, std::memory_order_acquire);
	}

	void WindowController::RequestFullPresent()
	{
		fullPresentRequested_.store(true, std::memory_order_release);
		RequestControlWake();
	}

	void WindowController::SetInputCoordinator(ContactInputCoordinator* coordinator)
	{
		inputCoordinator_.store(coordinator, std::memory_order_release);
		if (coordinator) coordinator->PublishControlWake(); // 接线前已产生的窗口请求也不能漏唤醒。
	}

	void WindowController::RequestControlWake()
	{
		if (ContactInputCoordinator* coordinator = inputCoordinator_.load(std::memory_order_acquire))
			coordinator->PublishControlWake();
	}

	void WindowController::QueueCanvasCommand(CanvasCommand command)
	{
		{
			const std::scoped_lock lock(canvasCommandMutex_);
			canvasCommands_.push_back(command);
		}
		RequestControlWake();
	}

	void WindowController::SetGpuTransparentComposition(bool enabled)
	{
		gpuTransparentComposition_.store(enabled, std::memory_order_release);
	}

	DrawingTool WindowController::ActiveTool() const
	{
		return activeTool_.load(std::memory_order_relaxed);
	}

	void WindowController::SetActiveTool(DrawingTool tool) noexcept
	{
		if (static_cast<uint32_t>(tool) > static_cast<uint32_t>(DrawingTool::FilledRectangle))
			return;
		activeTool_.store(tool, std::memory_order_release);
		RequestDrawingCursorRender();
		RequestControlWake();
	}

	bool WindowController::SelectionMode() const noexcept
	{
		return selectionMode_.load(std::memory_order_acquire);
	}

	void WindowController::SetSelectionMode(bool enabled) noexcept
	{
		if (selectionMode_.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		RequestControlWake();
	}

	bool WindowController::AutoSaveEnabled() const noexcept
	{
		return autoSaveEnabled_.load(std::memory_order_acquire);
	}

	void WindowController::SetAutoSaveEnabled(bool enabled) noexcept
	{
		autoSaveEnabled_.store(enabled, std::memory_order_release);
	}

	void WindowController::SetActivationAllowed(bool enabled) noexcept
	{
		activationAllowed_.store(enabled, std::memory_order_release);
	}

	EraserWidthMode WindowController::ActiveEraserWidthMode() const noexcept
	{
		return EraserWidthModeForRevision(
			eraserWidthModeRevision_.load(std::memory_order_acquire));
	}

	uint32_t WindowController::ActiveEraserWidthModeRevision() const noexcept
	{
		return eraserWidthModeRevision_.load(std::memory_order_acquire);
	}

	void WindowController::SetEraserWidthMode(EraserWidthMode mode) noexcept
	{
		uint32_t revision = eraserWidthModeRevision_.load(std::memory_order_relaxed);
		const EraserWidthMode current = EraserWidthModeForRevision(revision);
		if (current == mode) return;
		++revision;
		if (EraserWidthModeForRevision(revision) != mode) ++revision;
		eraserWidthModeRevision_.store(revision, std::memory_order_release);
		RequestControlWake();
	}

	void WindowController::SetProductVisualStyle(
		uint32_t colorRgba, float widthDip) noexcept
	{
		if (!std::isfinite(widthDip) || widthDip <= 0.0f)
			widthDip = 2.0f;
		productColorRgba_.store(colorRgba, std::memory_order_release);
		productWidthDip_.store(widthDip, std::memory_order_release);
		RequestDrawingCursorRender();
		RequestControlWake();
	}

	ProductVisualStyle WindowController::ProductVisualStyleSnapshot() const noexcept
	{
		ProductVisualStyle style;
		style.colorRgba = productColorRgba_.load(std::memory_order_acquire);
		style.widthDip = productWidthDip_.load(std::memory_order_acquire);
		return style;
	}

	void WindowController::EnqueueCanvasCommand(CanvasCommand command)
	{
		QueueCanvasCommand(command);
	}

	void WindowController::RequestExit() noexcept
	{
		exitRequested_.store(true, std::memory_order_release);
		RequestControlWake();
	}

	bool WindowController::ConfigureDrawingCursor(
		DrawingTool tool, const DrawingCursorAppearance& appearance)
	{
		if (!IsValidDrawingCursorAppearance(appearance))
		{
			std::cout << "Invalid drawing cursor appearance for tool="
				<< static_cast<uint32_t>(tool) << std::endl;
			return false;
		}
		if (tool == DrawingTool::Pen) penCursorAppearance_ = appearance;
		else if (tool == DrawingTool::Highlighter) highlighterCursorAppearance_ = appearance;
		else if (tool == DrawingTool::Eraser) eraserCursorAppearance_ = appearance;
		else if (tool == DrawingTool::Laser) laserCursorAppearance_ = appearance;
		else penCursorAppearance_ = appearance;
		RequestDrawingCursorRender();
		return true;
	}

	DrawingCursorAppearance WindowController::CursorAppearanceForTool(
		DrawingTool tool) const noexcept
	{
		if (tool == DrawingTool::Pen) return penCursorAppearance_;
		if (tool == DrawingTool::Highlighter) return highlighterCursorAppearance_;
		if (tool == DrawingTool::Eraser) return eraserCursorAppearance_;
		if (tool == DrawingTool::Laser) return laserCursorAppearance_;
		return penCursorAppearance_;
	}

	void WindowController::SetMouseUsesSystemCursor(bool enabled) noexcept
	{
		if (mouseUsesSystemCursor_.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		RequestDrawingCursorRender(); // 同时清理旧应用光标脏区并切换系统箭头。
		QueueSystemCursorRefresh();
	}

	bool WindowController::GetMouseUsesSystemCursor() const noexcept
	{
		return mouseUsesSystemCursor_.load(std::memory_order_acquire);
	}

	void WindowController::SetActiveDrawingCursorTool(DrawingTool tool) noexcept
	{
		const int32_t encoded = static_cast<int32_t>(tool);
		if (activeDrawingCursorTool_.exchange(encoded, std::memory_order_acq_rel) != encoded)
		{
			RequestDrawingCursorRender();
			QueueSystemCursorRefresh();
		}
	}

	void WindowController::ClearActiveDrawingCursorTool() noexcept
	{
		if (activeDrawingCursorTool_.exchange(-1, std::memory_order_acq_rel) != -1)
		{
			RequestDrawingCursorRender();
			QueueSystemCursorRefresh();
		}
	}

	DrawingTool WindowController::EffectiveDrawingCursorTool() const noexcept
	{
		const int32_t activeTool = activeDrawingCursorTool_.load(std::memory_order_acquire);
		if (activeTool >= static_cast<int32_t>(DrawingTool::Pen) &&
			activeTool <= static_cast<int32_t>(DrawingTool::FilledRectangle))
			return static_cast<DrawingTool>(activeTool);
		return ActiveTool();
	}

	DrawingCursorPointerAuthority WindowController::CursorOwner() const noexcept
	{
		if (touchPanActive_.load(std::memory_order_acquire) &&
			realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire))
			return DrawingCursorPointerAuthority::Mouse;
		return cursorOwner_.load(std::memory_order_acquire);
	}

	bool WindowController::ReadPenCursorSample(DrawingCursorSample& sample) const noexcept
	{
		return penCursorSample_.Read(sample);
	}

	bool WindowController::ReadMouseCursorSample(DrawingCursorSample& sample) const noexcept
	{
		return mouseCursorSample_.Read(sample);
	}

	void WindowController::SetTouchPanActive(bool active) noexcept
	{
		if (touchPanActive_.exchange(active, std::memory_order_acq_rel) == active) return;
		if (active)
			realMouseTakeoverDuringTouchPan_.store(false, std::memory_order_release);
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState(active ? "touch-pan-start" : "touch-pan-stop",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
		DrawingCursorSample penSample;
		DrawingCursorSample mouseSample;
		penCursorSample_.Read(penSample);
		mouseCursorSample_.Read(mouseSample);
		if (active)
		{
			// Hover 可能已预启动触觉；Pan 期间保留 Pen presence，但隐藏应用光标。
			hapticPointerIdRequested_.store(false, std::memory_order_release);
			hapticPointerLeaveRequested_.store(true, std::memory_order_release);
			pendingHapticPointerId_.store(0, std::memory_order_relaxed);
			pendingHapticPointerEraser_.store(false, std::memory_order_relaxed);
			if (penSample.valid && penSample.inContact)
				SetPenContactSuppressedForTouchPan(true);
		}
		else
		{
			SetDrawingCursorOwner(ResolveDrawingCursorOwnerAfterTouchPan(
				realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire),
				penSample.valid, mouseSample.valid));
		}
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
		RequestControlWake();
	}

	bool WindowController::TouchPanActive() const noexcept
	{
		return touchPanActive_.load(std::memory_order_acquire);
	}

	void WindowController::SuppressPenContactForTouchPan() noexcept
	{
		SetPenContactSuppressedForTouchPan(true);
		hapticPointerIdRequested_.store(false, std::memory_order_release);
		hapticPointerLeaveRequested_.store(true, std::memory_order_release);
		pendingHapticPointerId_.store(0, std::memory_order_relaxed);
		pendingHapticPointerEraser_.store(false, std::memory_order_relaxed);
		// Pen presence 独立保留，供兼容 Mouse 判断和 Pan 结束后的 owner 恢复。
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
		RequestControlWake();
	}

	bool WindowController::PenContactSuppressedForTouchPan() const noexcept
	{
		return penContactSuppressedForTouchPan_.load(std::memory_order_acquire);
	}

	void WindowController::NotifyTouchContactBegin() noexcept
	{
		NotifyTouchContactBegin(true, GetTickCount());
	}

	void WindowController::NotifyTouchContactBegin(bool trackActiveContact,
		uint32_t touchBarrierTick) noexcept
	{
		const uint64_t barrierState = kTouchInputBarrierKnownMask |
			static_cast<uint64_t>(touchBarrierTick);
		latestTouchInputBarrierTick_.store(barrierState, std::memory_order_release);
		if (trackActiveContact)
			activeTouchContactCount_.fetch_add(1u, std::memory_order_acq_rel);
		ClearMouseCursorSample();
		// Mailbox 已经无效时 Clear 会早退；Touch Down 仍必须重新应用专用工具策略。
		QueueSystemCursorRefresh();
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState(trackActiveContact ? "touch-contact-begin" : "touch-pointer-barrier",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
	}

	void WindowController::NotifyTouchContactEnd() noexcept
	{
		uint32_t count = activeTouchContactCount_.load(std::memory_order_acquire);
		while (count > 0 && !activeTouchContactCount_.compare_exchange_weak(
			count, count - 1u, std::memory_order_acq_rel, std::memory_order_acquire))
		{
		}
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("touch-contact-end",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
	}

	void WindowController::SetPenContactSuppressedForTouchPan(bool suppressed) noexcept
	{
		const bool changed = penContactSuppressedForTouchPan_.exchange(
			suppressed, std::memory_order_acq_rel) != suppressed;
		if (!changed) return;
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState(suppressed ? "suppression-latched" : "suppression-cleared",
			lastCursorTracePointerId_.load(std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
		if (suppressed)
		{
			hapticPointerIdRequested_.store(false, std::memory_order_release);
			hapticPointerLeaveRequested_.store(true, std::memory_order_release);
			RequestControlWake();
		}
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	void WindowController::PublishPenCursorSample(
		const DrawingCursorSample& sample) noexcept
	{
		const bool touchPanActive = touchPanActive_.load(std::memory_order_acquire);
		const bool realMouseTakeover = realMouseTakeoverDuringTouchPan_.load(
			std::memory_order_acquire);
		if (sample.valid && (!touchPanActive || !realMouseTakeover))
			SetDrawingCursorOwner(DrawingCursorPointerAuthority::Pen);
		const bool suppressForTouchPan = ShouldSuppressPenFeedbackForTouchPan(
			touchPanActive,
			penContactSuppressedForTouchPan_.load(std::memory_order_acquire) ||
				penCompatibilityMouseContactSuppressed_.load(std::memory_order_acquire),
			true, sample.inContact);
		if (suppressForTouchPan)
		{
			SetPenContactSuppressedForTouchPan(true);
			penCursorSample_.Publish(sample);
			return;
		}
		DrawingCursorSample previous;
		penCursorSample_.Read(previous);
		if (!penCursorSample_.Publish(sample)) return;
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("pen-sample", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		// 接触期间由活动绘制帧读取 mailbox，避免每个 Pen 包额外唤醒。
		if (!sample.inContact) RequestDrawingCursorRender();
		if (previous.valid != sample.valid || previous.inContact != sample.inContact ||
			previous.inverted != sample.inverted) QueueSystemCursorRefresh();
	}

	void WindowController::ClearPenCursorSample() noexcept
	{
		// 没有 WM_POINTER 生命周期的设备以 RTS Clear 作为本次抑制终态。
		if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0)
			SetPenContactSuppressedForTouchPan(false);
		if (!penCursorSample_.Clear()) return;
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("pen-sample-clear", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	void WindowController::PublishMouseCursorSample(
		const DrawingCursorSample& sample) noexcept
	{
		DrawingCursorSample previous;
		mouseCursorSample_.Read(previous);
		if (!mouseCursorSample_.Publish(sample)) return;
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("mouse-sample", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		RequestDrawingCursorRender();
		if (previous.valid != sample.valid || previous.inContact != sample.inContact)
			QueueSystemCursorRefresh();
	}

	void WindowController::ClearMouseCursorSample() noexcept
	{
		if (!mouseCursorSample_.Clear()) return;
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("mouse-sample-clear", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire));
		#endif
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	bool WindowController::ShouldIgnoreMouseCursorMessage(bool promotedPointerMessage,
		bool penSampleValid, bool touchBarrierKnown,
		uint32_t mouseMessageTick, uint32_t touchBarrierTick) const noexcept
	{
		return Inkeys::Drawing::Draw3::ShouldIgnoreMouseCursorMessage(
			promotedPointerMessage, ResolveGetPointerType() != nullptr,
			penSampleValid, touchBarrierKnown, mouseMessageTick, touchBarrierTick);
	}

	void WindowController::RequestDrawingCursorRender() noexcept
	{
		drawingCursorRenderRequested_.store(true, std::memory_order_release);
		RequestControlWake();
	}

	void WindowController::QueueSystemCursorRefresh() noexcept
	{
		const HWND window = window_.load(std::memory_order_acquire);
		if (!window || systemCursorRefreshPosted_.exchange(true, std::memory_order_acq_rel)) return;
		if (!PostMessageW(window, kApplySystemCursorMessage, 0, 0))
			systemCursorRefreshPosted_.store(false, std::memory_order_release);
	}

	void WindowController::SetDrawingCursorOwner(
		DrawingCursorPointerAuthority owner) noexcept
	{
		if (cursorOwner_.exchange(owner,
			std::memory_order_acq_rel) == owner) return;
		#if defined(DRAW3_RTS_DIAGNOSTICS)
		TraceCursorState("cursor-owner-change", lastCursorTracePointerId_.load(
			std::memory_order_acquire),
			static_cast<POINTER_INPUT_TYPE>(lastCursorTracePointerType_.load(
				std::memory_order_acquire)),
			lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire), true);
		#endif
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

#if defined(DRAW3_RTS_DIAGNOSTICS)
	void WindowController::TraceCursorState(const char* eventName, uint32_t pointerId,
		POINTER_INPUT_TYPE pointerType, bool pointerTypeKnown,
		bool forceLifecycle) noexcept
	{
		if (!cursorTraceEnabled_.load(std::memory_order_acquire)) return;
		DrawingCursorSample penSample;
		DrawingCursorSample mouseSample;
		penCursorSample_.Read(penSample);
		mouseCursorSample_.Read(mouseSample);
		const DrawingCursorPointerAuthority authority = CursorOwner();
		const DrawingTool tool = EffectiveDrawingCursorTool();
		const bool touchPanActive = touchPanActive_.load(std::memory_order_acquire);
		const uint32_t activeTouchContactCount = activeTouchContactCount_.load(
			std::memory_order_acquire);
		const bool realMouseTakeover = realMouseTakeoverDuringTouchPan_.load(
			std::memory_order_acquire);
		const bool suppression = penContactSuppressedForTouchPan_.load(
			std::memory_order_acquire);
		const uint32_t suppressedPointerId = suppressedPenPointerId_.load(
			std::memory_order_acquire);
		const uint32_t pendingHapticPointerId = pendingHapticPointerId_.load(
			std::memory_order_acquire);
		const bool hapticPointerIdRequested = hapticPointerIdRequested_.load(
			std::memory_order_acquire);
		const bool hapticPointerLeaveRequested = hapticPointerLeaveRequested_.load(
			std::memory_order_acquire);
		const bool mouseUsesSystemCursor = mouseUsesSystemCursor_.load(
			std::memory_order_acquire);
		const bool hide = ShouldHideSystemDrawingCursor(authority,
			tool == DrawingTool::Eraser, tool == DrawingTool::Laser,
			penSample.valid, mouseSample.valid, mouseUsesSystemCursor,
			touchPanActive, realMouseTakeover);
		DrawingCursorDiagnosticVisualState appVisual;
		const bool appVisualKnown = ReadDrawingCursorDiagnosticVisualState(appVisual);

		// 坐标和 sequence 不进入 key，Pointer Update 只有状态变化才输出。
		uint64_t stateKey = pointerId;
		stateKey = stateKey * 17u + static_cast<uint64_t>(pointerType);
		stateKey = stateKey * 17u + (pointerTypeKnown ? 1u : 0u);
		stateKey = stateKey * 17u + static_cast<uint64_t>(authority);
		stateKey = stateKey * 17u + (touchPanActive ? 1u : 0u);
		stateKey = stateKey * 17u + activeTouchContactCount;
		stateKey = stateKey * 17u + (realMouseTakeover ? 1u : 0u);
		stateKey = stateKey * 17u + (suppression ? 1u : 0u);
		stateKey = stateKey * 17u + suppressedPointerId;
		stateKey = stateKey * 17u + pendingHapticPointerId;
		stateKey = stateKey * 17u + (hapticPointerIdRequested ? 1u : 0u);
		stateKey = stateKey * 17u + (hapticPointerLeaveRequested ? 1u : 0u);
		stateKey = stateKey * 17u + (penSample.valid ? 1u : 0u);
		stateKey = stateKey * 17u + (penSample.inContact ? 1u : 0u);
		stateKey = stateKey * 17u + (penSample.inverted ? 1u : 0u);
		stateKey = stateKey * 17u + (mouseSample.valid ? 1u : 0u);
		stateKey = stateKey * 17u + (mouseSample.inContact ? 1u : 0u);
		stateKey = stateKey * 17u + (appVisualKnown ? 1u : 0u);
		stateKey = stateKey * 17u + (appVisual.visible ? 1u : 0u);
		stateKey = stateKey * 17u + static_cast<uint64_t>(appVisual.reason);
		stateKey = stateKey * 17u + (hide ? 1u : 0u);

		std::lock_guard<std::mutex> lock(cursorTraceMutex_);
		if (!forceLifecycle && lastCursorTraceStateKnown_ &&
			lastCursorTraceStateKey_ == stateKey) return;
		lastCursorTraceStateKnown_ = true;
		lastCursorTraceStateKey_ = stateKey;
		char line[960] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[CURSOR_TRACE][state] event=%s pointerId=%u pointerEventType=%s(%u) "
			"cursorOwner=%u touchPanActive=%u activeTouchContacts=%u "
			"realMouseTakeover=%u suppression=%u suppressedPointerId=%u "
			"haptic={pointerId=%u,pending=%u,leave=%u} "
			"penSample={valid=%u,contact=%u,inverted=%u} "
			"mouseSample={valid=%u,contact=%u} appCursor=%s appReason=%s "
			"ShouldHideSystemDrawingCursor=%u systemCursor=%s reason=%s\r\n",
			eventName ? eventName : "unknown", pointerId,
			PointerTypeName(pointerType, pointerTypeKnown),
			static_cast<unsigned>(pointerType), static_cast<unsigned>(authority),
			touchPanActive ? 1u : 0u, activeTouchContactCount,
			realMouseTakeover ? 1u : 0u,
			suppression ? 1u : 0u,
			suppressedPointerId, pendingHapticPointerId,
			hapticPointerIdRequested ? 1u : 0u,
			hapticPointerLeaveRequested ? 1u : 0u,
			penSample.valid ? 1u : 0u,
			penSample.inContact ? 1u : 0u, penSample.inverted ? 1u : 0u,
			mouseSample.valid ? 1u : 0u, mouseSample.inContact ? 1u : 0u,
			appVisualKnown ? appVisual.visible ? "visible" : "hidden" : "unknown",
			appVisualKnown ? DrawingCursorDiagnosticVisualReasonName(
				appVisual.reason) : "unknown",
			hide ? 1u : 0u, hide ? "hidden" : "arrow",
			SystemCursorDecisionReason(authority, tool, penSample.valid,
				mouseSample.valid, mouseUsesSystemCursor, touchPanActive,
				realMouseTakeover, hide));
		if (length > 0)
		{
			std::cout.write(line, (std::min)(
				static_cast<size_t>(length), sizeof(line) - 1));
			std::cout.flush();
		}
	}

	void WindowController::TraceTouchMouseMessage(UINT message, uint32_t messageTick,
		uint32_t touchBarrierTick, bool touchBarrierKnown,
		ULONG_PTR extraInfo, bool promotedPointerMessage,
		int x, int y, uint32_t activeTouchContactCount,
		bool accepted, const char* reason) noexcept
	{
		if (activeTouchContactCount == 0 ||
			!cursorTraceEnabled_.load(std::memory_order_acquire)) return;
		std::lock_guard<std::mutex> lock(cursorTraceMutex_);
		char line[640] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[CURSOR_TRACE][touch-mouse] message=%s(0x%04x) messageTime=%u "
			"touchBarrier=%u barrierKnown=%u extraInfo=0x%llx promoted=%u "
			"x=%d y=%d activeTouchContacts=%u accepted=%u reason=%s\r\n",
			MouseMessageName(message), static_cast<unsigned>(message), messageTick,
			touchBarrierTick, touchBarrierKnown ? 1u : 0u,
			static_cast<unsigned long long>(extraInfo),
			promotedPointerMessage ? 1u : 0u, x, y, activeTouchContactCount,
			accepted ? 1u : 0u, reason ? reason : "unknown");
		if (length > 0)
		{
			std::cout.write(line, (std::min)(
				static_cast<size_t>(length), sizeof(line) - 1));
			std::cout.flush();
		}
	}
#endif

	void WindowController::ApplyWindowCursor(const char* trigger) noexcept
	{
		POINT cursorPosition = {};
		const HWND window = window_.load(std::memory_order_acquire);
		if (!window || !GetCursorPos(&cursorPosition)) return;
		const HWND cursorWindow = WindowFromPoint(cursorPosition);
		if (cursorWindow != window && (!cursorWindow || !IsChild(window, cursorWindow)))
			return; // 私有刷新消息不能改变其他窗口当前拥有的系统光标。
		DrawingCursorSample penSample;
		DrawingCursorSample mouseSample;
		penCursorSample_.Read(penSample);
		mouseCursorSample_.Read(mouseSample);
		const DrawingTool tool = EffectiveDrawingCursorTool();
		const DrawingCursorPointerAuthority authority = CursorOwner();
		const bool mouseUsesSystemCursor =
			mouseUsesSystemCursor_.load(std::memory_order_acquire);
		const bool touchPanActive = touchPanActive_.load(std::memory_order_acquire);
		const bool realMouseTakeover = realMouseTakeoverDuringTouchPan_.load(
			std::memory_order_acquire);
		const bool hide = ShouldHideSystemDrawingCursor(
			authority,
			tool == DrawingTool::Eraser, tool == DrawingTool::Laser,
			penSample.valid, mouseSample.valid,
			mouseUsesSystemCursor, touchPanActive, realMouseTakeover);
		SetCursor(hide ? nullptr : defaultCursor_); // 仅影响当前 HWND，不使用全局计数式 ShowCursor。
#if defined(DRAW3_RTS_DIAGNOSTICS)
		if (cursorTraceEnabled_.load(std::memory_order_acquire))
		{
			DrawingCursorDiagnosticVisualState appVisual;
			const bool appVisualKnown = ReadDrawingCursorDiagnosticVisualState(appVisual);
			uint64_t decisionKey = static_cast<uint64_t>(authority);
			decisionKey = decisionKey * 17u + static_cast<uint64_t>(tool);
			decisionKey = decisionKey * 17u + (penSample.valid ? 1u : 0u);
			decisionKey = decisionKey * 17u + (penSample.inContact ? 1u : 0u);
			decisionKey = decisionKey * 17u + (mouseSample.valid ? 1u : 0u);
			decisionKey = decisionKey * 17u + (mouseSample.inContact ? 1u : 0u);
			decisionKey = decisionKey * 17u + (mouseUsesSystemCursor ? 1u : 0u);
			decisionKey = decisionKey * 17u + (touchPanActive ? 1u : 0u);
			decisionKey = decisionKey * 17u + (realMouseTakeover ? 1u : 0u);
			decisionKey = decisionKey * 17u + pendingHapticPointerId_.load(
				std::memory_order_acquire);
			decisionKey = decisionKey * 17u + (hapticPointerIdRequested_.load(
				std::memory_order_acquire) ? 1u : 0u);
			decisionKey = decisionKey * 17u + (hapticPointerLeaveRequested_.load(
				std::memory_order_acquire) ? 1u : 0u);
			decisionKey = decisionKey * 17u + (hide ? 1u : 0u);
			decisionKey = decisionKey * 17u + (appVisualKnown ? 1u : 0u);
			decisionKey = decisionKey * 17u + (appVisual.visible ? 1u : 0u);
			decisionKey = decisionKey * 17u + static_cast<uint64_t>(appVisual.reason);
			std::lock_guard<std::mutex> lock(cursorTraceMutex_);
			if (!lastSystemCursorDecisionKnown_ ||
				lastSystemCursorDecisionKey_ != decisionKey)
			{
				lastSystemCursorDecisionKnown_ = true;
				lastSystemCursorDecisionKey_ = decisionKey;
				char line[768] = {};
				const int length = std::snprintf(line, sizeof(line),
					"[CURSOR_TRACE][system] trigger=%s pointerId=%u pointerEventType=%s(%u) "
					"cursorOwner=%u touchPanActive=%u realMouseTakeover=%u "
					"suppression=%u suppressedPointerId=%u "
					"haptic={pointerId=%u,pending=%u,leave=%u} "
					"penSample={valid=%u,contact=%u,inverted=%u} "
					"mouseSample={valid=%u,contact=%u} appCursor=%s "
					"appReason=%s ShouldHideSystemDrawingCursor=%u "
					"systemCursor=%s reason=%s\r\n",
					trigger ? trigger : "unknown",
					lastCursorTracePointerId_.load(std::memory_order_acquire),
					PointerTypeName(static_cast<POINTER_INPUT_TYPE>(
						lastCursorTracePointerType_.load(std::memory_order_acquire)),
						lastCursorTracePointerTypeKnown_.load(std::memory_order_acquire)),
					lastCursorTracePointerType_.load(std::memory_order_acquire),
					static_cast<unsigned>(authority),
					touchPanActive ? 1u : 0u, realMouseTakeover ? 1u : 0u,
					penContactSuppressedForTouchPan_.load(
						std::memory_order_acquire) ? 1u : 0u,
					suppressedPenPointerId_.load(std::memory_order_acquire),
					pendingHapticPointerId_.load(std::memory_order_acquire),
					hapticPointerIdRequested_.load(std::memory_order_acquire) ? 1u : 0u,
					hapticPointerLeaveRequested_.load(std::memory_order_acquire) ? 1u : 0u,
					penSample.valid ? 1u : 0u, penSample.inContact ? 1u : 0u,
					penSample.inverted ? 1u : 0u,
					mouseSample.valid ? 1u : 0u, mouseSample.inContact ? 1u : 0u,
					appVisualKnown ? appVisual.visible ? "visible" : "hidden" : "unknown",
					appVisualKnown ? DrawingCursorDiagnosticVisualReasonName(
						appVisual.reason) : "unknown",
					hide ? 1u : 0u, hide ? "hidden" : "arrow",
					SystemCursorDecisionReason(authority, tool, penSample.valid,
						mouseSample.valid, mouseUsesSystemCursor, touchPanActive,
						realMouseTakeover, hide));
				if (length > 0)
				{
					std::cout.write(line, (std::min)(
						static_cast<size_t>(length), sizeof(line) - 1));
					std::cout.flush();
				}
			}
		}
#else
		(void)trigger;
#endif
	}

	bool WindowController::ConsumeHapticPointerId(uint32_t& pointerId, bool& eraserHint)
	{
		if (!hapticPointerIdRequested_.exchange(false, std::memory_order_acquire))
			return false;
		pointerId = pendingHapticPointerId_.load(std::memory_order_relaxed);
		eraserHint = pendingHapticPointerEraser_.load(std::memory_order_relaxed);
		return pointerId != 0;
	}

	bool WindowController::ConsumeHapticPointerLeave()
	{
		return hapticPointerLeaveRequested_.exchange(false, std::memory_order_acquire);
	}

	bool WindowController::ExitRequested() const
	{
		return exitRequested_.load(std::memory_order_acquire);
	}

	LRESULT WindowController::HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		const bool gpuTransparent = gpuTransparentComposition_.load(std::memory_order_acquire); // GPU 透明模式下由 backbuffer 负责背景。
		switch (message)
		{
		case WM_CLOSE:
			// 外部 HWND 由 Window Service 销毁；Draw3 只请求绘制线程退出。
			exitRequested_.store(true, std::memory_order_release);
			RequestControlWake();
			return 0;

		case kApplySystemCursorMessage:
			systemCursorRefreshPosted_.store(false, std::memory_order_release);
			ApplyWindowCursor("posted-refresh");
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				ApplyWindowCursor("WM_SETCURSOR");
				return TRUE;
			}
			break;

		case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
			// 显式接收 RTS 多点数据，并禁止长按/轻拂抢占普通笔输入。
			return static_cast<LRESULT>(kTabletInputFlags);

		case WM_POINTERLEAVE:
		{
			const uint32_t pointerId = static_cast<uint32_t>(LOWORD(wParam));
			const PointerDetails details = QueryPointerDetails(pointerId);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			lastCursorTracePointerId_.store(pointerId, std::memory_order_release);
			lastCursorTracePointerType_.store(
				static_cast<uint32_t>(details.type), std::memory_order_release);
			lastCursorTracePointerTypeKnown_.store(
				details.typeKnown, std::memory_order_release);
#endif
			const DrawingCursorPointerAuthority previousAuthority = CursorOwner();
			DrawingCursorSample penSample;
			const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
			const bool suppressedPenPointer =
				suppressedPenPointerId_.load(std::memory_order_acquire) == pointerId;
			const DrawingCursorPointerAuthority pointerEventType = details.typeKnown
				? AuthorityForPointerType(details.type)
				: suppressedPenPointer ||
					previousAuthority == DrawingCursorPointerAuthority::Pen || penSampleValid
					? DrawingCursorPointerAuthority::Pen : previousAuthority;
			const bool penPointer = pointerEventType == DrawingCursorPointerAuthority::Pen;
			if (penPointer) ClearPenCursorSample();
			if (penPointer &&
				(suppressedPenPointerId_.load(std::memory_order_acquire) == 0 ||
					suppressedPenPointerId_.load(std::memory_order_relaxed) == pointerId))
			{
				suppressedPenPointerId_.store(0, std::memory_order_release);
				SetPenContactSuppressedForTouchPan(false);
			}
			if (penPointer)
				penCompatibilityMouseContactSuppressed_.store(false,
					std::memory_order_release);
			SetDrawingCursorOwner(ResolveDrawingCursorOwnerForPointerEvent(
				previousAuthority, pointerEventType,
				touchPanActive_.load(std::memory_order_acquire),
				realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire)));
			if (penPointer)
			{
				lastHapticPenInfoPointerId_ = 0;
				lastHapticPenInfoKnown_ = false;
				lastHapticPenInfoEraser_ = false;
				hapticPointerLeaveRequested_.store(true, std::memory_order_release);
				RequestControlWake();
			}
#if defined(DRAW3_RTS_DIAGNOSTICS)
			TraceCursorState("WM_POINTERLEAVE", pointerId, details.type,
				details.typeKnown, true);
#endif
			break;
		}
		case WM_POINTERENTER:
		case WM_POINTERUPDATE:
		case WM_POINTERDOWN:
		case WM_POINTERUP:
		{
			const uint32_t pointerId = static_cast<uint32_t>(LOWORD(wParam));
			const PointerDetails details = QueryPointerDetails(pointerId);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			lastCursorTracePointerId_.store(pointerId, std::memory_order_release);
			lastCursorTracePointerType_.store(
				static_cast<uint32_t>(details.type), std::memory_order_release);
			lastCursorTracePointerTypeKnown_.store(
				details.typeKnown, std::memory_order_release);
#endif
			const DrawingCursorPointerAuthority previousAuthority = CursorOwner();
			const DrawingCursorPointerAuthority pointerEventType = details.typeKnown
				? AuthorityForPointerType(details.type) : DrawingCursorPointerAuthority::Unknown;
			if (ShouldClearMouseCursorSampleForPointerEvent(
				pointerEventType, message == WM_POINTERDOWN))
			{
				// Pointer 仅作 message-time barrier fallback；活跃计数由 RTS 权威生命周期维护。
				NotifyTouchContactBegin(false, static_cast<uint32_t>(GetMessageTime()));
			}
			const bool penPointer = pointerEventType == DrawingCursorPointerAuthority::Pen ||
				suppressedPenPointerId_.load(std::memory_order_acquire) == pointerId;
			const bool penInContact = details.inContact || message == WM_POINTERDOWN;
			if (details.typeKnown)
			{
				if (pointerEventType == DrawingCursorPointerAuthority::Mouse &&
					touchPanActive_.load(std::memory_order_acquire))
					realMouseTakeoverDuringTouchPan_.store(true, std::memory_order_release);
				SetDrawingCursorOwner(ResolveDrawingCursorOwnerForPointerEvent(
					previousAuthority, pointerEventType,
					touchPanActive_.load(std::memory_order_acquire),
					realMouseTakeoverDuringTouchPan_.load(std::memory_order_acquire)));
			}
			if (message == WM_POINTERUP && penPointer)
			{
				penCompatibilityMouseContactSuppressed_.store(false,
					std::memory_order_release);
				ClearPenCursorSample(); // 终态坐标不能冒充 Hover；新 Update/InAir 会重新发布。
				if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0 ||
					suppressedPenPointerId_.load(std::memory_order_relaxed) == pointerId)
				{
					suppressedPenPointerId_.store(0, std::memory_order_release);
					SetPenContactSuppressedForTouchPan(false);
				}
			}
			else if (details.type == PT_PEN && details.positionKnown)
			{
				if (ShouldSuppressPenFeedbackForTouchPan(
					touchPanActive_.load(std::memory_order_acquire),
					penContactSuppressedForTouchPan_.load(std::memory_order_acquire),
					true, penInContact))
				{
					suppressedPenPointerId_.store(pointerId, std::memory_order_release);
					SetPenContactSuppressedForTouchPan(true);
				}
				POINT clientPosition = details.screenPosition;
				if (ScreenToClient(window, &clientPosition))
				{
					DrawingCursorSample sample;
					sample.x = static_cast<float>(clientPosition.x);
					sample.y = static_cast<float>(clientPosition.y);
					sample.valid = true;
					sample.inverted = details.eraserHint;
					sample.inContact = penInContact;
					LARGE_INTEGER qpc = {};
					QueryPerformanceCounter(&qpc);
					sample.qpc = qpc.QuadPart;
					PublishPenCursorSample(sample);
				}
			}
			if ((message == WM_POINTERENTER || message == WM_POINTERDOWN) &&
				pointerId != 0 && penPointer &&
				!touchPanActive_.load(std::memory_order_acquire) &&
				!penContactSuppressedForTouchPan_.load(std::memory_order_acquire))
			{
				if (pointerId != lastHapticPenInfoPointerId_ || !lastHapticPenInfoKnown_)
				{
					lastHapticPenInfoPointerId_ = pointerId;
					lastHapticPenInfoKnown_ = details.penInfoKnown;
					lastHapticPenInfoEraser_ = details.penInfoKnown && details.eraserHint;
				}
				// Pointer 只作为触觉设备绑定/笔尾线索；RTS 仍是唯一绘制输入来源。
				hapticPointerLeaveRequested_.store(false, std::memory_order_relaxed);
				pendingHapticPointerId_.store(pointerId, std::memory_order_relaxed);
				pendingHapticPointerEraser_.store(
					lastHapticPenInfoKnown_ && lastHapticPenInfoEraser_,
					std::memory_order_relaxed);
				hapticPointerIdRequested_.store(true, std::memory_order_release);
				RequestControlWake();
			}
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const char* pointerEventName = message == WM_POINTERDOWN
				? "WM_POINTERDOWN" : message == WM_POINTERUP
				? "WM_POINTERUP" : message == WM_POINTERENTER
				? "WM_POINTERENTER" : "WM_POINTERUPDATE";
			TraceCursorState(pointerEventName, pointerId, details.type,
				details.typeKnown,
				message == WM_POINTERDOWN || message == WM_POINTERUP);
#endif
			break;
		}

		case WM_DESTROY:
			penCursorSample_.Clear();
			mouseCursorSample_.Clear();
			cursorOwner_.store(
				DrawingCursorPointerAuthority::Unknown, std::memory_order_release);
			touchPanActive_.store(false, std::memory_order_release);
			realMouseTakeoverDuringTouchPan_.store(false, std::memory_order_release);
			penContactSuppressedForTouchPan_.store(false, std::memory_order_release);
			penCompatibilityMouseContactSuppressed_.store(false,
				std::memory_order_release);
			suppressedPenPointerId_.store(0, std::memory_order_release);
			SetCursor(defaultCursor_);
			RemoveProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY);
			exitRequested_.store(true, std::memory_order_release); // 通知主循环退出。
			RequestControlWake();
			if (!externalWindow_.load(std::memory_order_acquire))
			{
				PostQuitMessage(0);
				return 0;
			}
			// Window Service 所在线程还负责其它 overlay HWND，不能被 Draw3
			// 的外部窗口过程投递 WM_QUIT；交回宿主默认处理即可。
			return DefWindowProcW(window, message, wParam, lParam);

		case WM_DWMCOMPOSITIONCHANGED:
			compositionChangedRequested_.store(true, std::memory_order_release); // DWM 状态变化交给主循环刷新 presenter。
			RequestControlWake();
			return 0;

		case WM_MOUSEACTIVATE:
			// 只由 Enter/Exit 事务切换，避免每帧抢焦点或在 Presentation 中激活。
			return activationAllowed_.load(std::memory_order_acquire)
				? MA_ACTIVATE : MA_NOACTIVATE;

		case WM_ERASEBKGND:
			if (gpuTransparent) return 1; // 阻止 GDI 擦背景，避免透明区域闪烁。
			break;

		case WM_PAINT:
			if (gpuTransparent)
			{
				// 新暴露区域由主绘制线程重新提交整张画布。
				RequestFullPresent();
				ValidateRect(window, nullptr);
				return 0;
			}
			break;

		case WM_SHOWWINDOW:
		case WM_ACTIVATE:
			if (gpuTransparent) RequestFullPresent(); // 窗口可见性变化后重新提交完整画布。
			break;

		case WM_WINDOWPOSCHANGED:
			if (gpuTransparent)
			{
				const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam);
				if (!position || !(position->flags & SWP_NOMOVE) || !(position->flags & SWP_NOSIZE)) RequestFullPresent(); // 移动或尺寸变化后刷新 DWM 读取内容。
			}
			break;

		case WM_SIZE:
			if (wParam != SIZE_MINIMIZED)
			{
				const int width = static_cast<int>(LOWORD(lParam));
				const int height = static_cast<int>(HIWORD(lParam));
				if (width > 0 && height > 0)
				{
					// 窗口过程只记录尺寸，D3D 资源仍由主绘制线程重建。
					pendingResizeWidth_.store(width, std::memory_order_relaxed);
					pendingResizeHeight_.store(height, std::memory_order_relaxed);
					resizeRequested_.store(true, std::memory_order_release);
					RequestControlWake();
				}
				if (gpuTransparent) RequestFullPresent();
			}
			break;

		case WM_DPICHANGED:
			// DPI 变化不直接改资源，先请求一帧完整重建。
			compositionChangedRequested_.store(true, std::memory_order_release);
			RequestControlWake();
			return 0;

		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		{
			const bool buttonUp = message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
				message == WM_MBUTTONUP;
			const bool buttonDown = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
				message == WM_MBUTTONDOWN ||
				(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;
			const uint32_t messageTick = static_cast<uint32_t>(GetMessageTime());
			const ULONG_PTR extraInfo = GetMessageExtraInfo();
			const bool promotedPointerMessage = IsPromotedPointerMouseMessage(extraInfo);
			const uint64_t barrierState = latestTouchInputBarrierTick_.load(
				std::memory_order_acquire);
			const bool touchBarrierKnown =
				(barrierState & kTouchInputBarrierKnownMask) != 0;
			const uint32_t touchBarrierTick = static_cast<uint32_t>(barrierState);
			const int mouseClientX = GET_X_LPARAM(lParam);
			const int mouseClientY = GET_Y_LPARAM(lParam);
			DrawingCursorSample penSample;
			const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const uint32_t activeTouchContactCount = activeTouchContactCount_.load(
				std::memory_order_acquire);
			const auto traceMouseDecision = [&](bool accepted, const char* reason) noexcept
			{
				TraceTouchMouseMessage(message, messageTick, touchBarrierTick,
					touchBarrierKnown, extraInfo, promotedPointerMessage,
					mouseClientX, mouseClientY, activeTouchContactCount,
					accepted, reason);
			};
#endif
			if (ShouldIgnoreMouseCursorMessage(promotedPointerMessage, penSampleValid,
				touchBarrierKnown, messageTick, touchBarrierTick))
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				const bool staleQueuedMouse = touchBarrierKnown &&
					static_cast<LONG>(messageTick - touchBarrierTick) <= 0;
				traceMouseDecision(false, promotedPointerMessage ? "promoted-pointer" :
					staleQueuedMouse ? "stale-touch-barrier" : "pen-compatibility-filter");
#endif
				break;
			}
			if (buttonUp && penCompatibilityMouseContactSuppressed_.exchange(
				false, std::memory_order_acq_rel))
			{
				if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0)
					SetPenContactSuppressedForTouchPan(false);
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pen-compatibility-terminal");
#endif
				break;
			}
			if (penCompatibilityMouseContactSuppressed_.load(
				std::memory_order_acquire))
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pen-compatibility-latched");
#endif
				break;
			}
			if (buttonUp && ShouldSuppressMouseButtonUpCursorSample(CursorOwner()))
			{
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pointer-terminal-owner");
#endif
				break; // Pointer 终态后的兼容 Mouse Up 不能把已隐藏光标重新发布为 Hover。
			}
			LARGE_INTEGER qpc = {};
			LARGE_INTEGER qpcFrequency = {};
			QueryPerformanceCounter(&qpc);
			QueryPerformanceFrequency(&qpcFrequency);
			const float mouseX = static_cast<float>(mouseClientX);
			const float mouseY = static_cast<float>(mouseClientY);
			const double penSampleAgeSeconds = penSampleValid && qpcFrequency.QuadPart > 0 &&
				qpc.QuadPart >= penSample.qpc
				? static_cast<double>(qpc.QuadPart - penSample.qpc) /
					static_cast<double>(qpcFrequency.QuadPart)
				: (std::numeric_limits<double>::infinity)();
			if (ShouldTreatMouseContactAsPenCompatibilityMessage(
				touchPanActive_.load(std::memory_order_acquire),
				penSampleValid, buttonDown, mouseX - penSample.x, mouseY - penSample.y,
				penSampleAgeSeconds))
			{
				// 没有 Pointer Down 的设备仍会提升兼容鼠标消息；只抑制反馈，不改变平移 authority。
				penCompatibilityMouseContactSuppressed_.store(true,
					std::memory_order_release);
				SuppressPenContactForTouchPan();
#if defined(DRAW3_RTS_DIAGNOSTICS)
				traceMouseDecision(false, "pen-compatibility-position");
#endif
				break;
			}
			if (touchPanActive_.load(std::memory_order_acquire))
				realMouseTakeoverDuringTouchPan_.store(true, std::memory_order_release);
			SetDrawingCursorOwner(DrawingCursorPointerAuthority::Mouse);
			if (message == WM_MOUSEMOVE && !trackingMouseLeave_)
			{
				TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
				trackingMouseLeave_ = TrackMouseEvent(&tracking) != FALSE;
			}
			DrawingCursorSample sample;
			sample.x = mouseX;
			sample.y = mouseY;
			sample.valid = true;
			sample.inContact = buttonDown;
			sample.qpc = qpc.QuadPart;
			PublishMouseCursorSample(sample);
#if defined(DRAW3_RTS_DIAGNOSTICS)
			traceMouseDecision(true, "accepted-real-mouse");
#endif
			break;
		}

		case WM_MOUSELEAVE:
		{
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const uint32_t messageTick = static_cast<uint32_t>(GetMessageTime());
			const ULONG_PTR extraInfo = GetMessageExtraInfo();
			const uint64_t barrierState = latestTouchInputBarrierTick_.load(
				std::memory_order_acquire);
			TraceTouchMouseMessage(message, messageTick,
				static_cast<uint32_t>(barrierState),
				(barrierState & kTouchInputBarrierKnownMask) != 0,
				extraInfo, IsPromotedPointerMouseMessage(extraInfo), 0, 0,
				activeTouchContactCount_.load(std::memory_order_acquire),
				true, "accepted-mouse-leave-clear");
#endif
			trackingMouseLeave_ = false;
			ClearMouseCursorSample();
			if (CursorOwner() ==
				DrawingCursorPointerAuthority::Mouse)
				SetDrawingCursorOwner(DrawingCursorPointerAuthority::Unknown);
			break;
		}

		case WM_MOUSEWHEEL:
		{
			const uint32_t messageTick = static_cast<uint32_t>(GetMessageTime());
			const ULONG_PTR extraInfo = GetMessageExtraInfo();
			const bool promotedPointerMessage = IsPromotedPointerMouseMessage(extraInfo);
			const uint64_t barrierState = latestTouchInputBarrierTick_.load(
				std::memory_order_acquire);
			const bool touchBarrierKnown =
				(barrierState & kTouchInputBarrierKnownMask) != 0;
			const uint32_t touchBarrierTick = static_cast<uint32_t>(barrierState);
			DrawingCursorSample penSample;
			const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
			const bool accepted = !ShouldIgnoreMouseCursorMessage(promotedPointerMessage,
				penSampleValid, touchBarrierKnown, messageTick, touchBarrierTick);
			if (accepted)
			{
				if (touchPanActive_.load(std::memory_order_acquire))
					realMouseTakeoverDuringTouchPan_.store(true, std::memory_order_release);
				SetDrawingCursorOwner(DrawingCursorPointerAuthority::Mouse);
			}
#if defined(DRAW3_RTS_DIAGNOSTICS)
			const bool staleQueuedMouse = touchBarrierKnown &&
				static_cast<LONG>(messageTick - touchBarrierTick) <= 0;
			TraceTouchMouseMessage(message, messageTick, touchBarrierTick,
				touchBarrierKnown, extraInfo, promotedPointerMessage,
				GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
				activeTouchContactCount_.load(std::memory_order_acquire), accepted,
				accepted ? "accepted-real-mouse-wheel" :
				promotedPointerMessage ? "promoted-pointer" :
				staleQueuedMouse ? "stale-touch-barrier" : "pen-compatibility-filter");
#endif
			break;
		}

		}
		return DefWindowProcW(window, message, wParam, lParam);
	}
}
