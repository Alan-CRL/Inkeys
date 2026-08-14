module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tpcshrd.h>

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <limits>
#include <mutex>
#include <process.h>
#include <tchar.h> // Tablet Pen Service 属性宏仍使用 _T。

module draw3.window_control;

#if defined(DRAW3_RTS_DIAGNOSTICS)
import draw3.diagnostics;
#endif

namespace draw3
{
	namespace
	{
		constexpr wchar_t kWindowClassName[] = L"InkeysDraw3Window";
		constexpr UINT kApplySystemCursorMessage = WM_APP + 1;
		constexpr LONG_PTR kPromotedPointerSignatureMask = 0xFFFFFF00;
		constexpr LONG_PTR kPromotedPointerSignature = 0xFF515700;
		constexpr LPARAM kPreviousKeyStateMask = static_cast<LPARAM>(1) << 30;

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

		bool IsPromotedPointerMouseMessage() noexcept
		{
			return (GetMessageExtraInfo() & kPromotedPointerSignatureMask) ==
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

		RECT GetPrimaryMonitorRectangle()
		{
			const POINT origin = { 0, 0 };
			const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
			MONITORINFO monitorInfo = {};
			monitorInfo.cbSize = sizeof(monitorInfo);
			if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) return monitorInfo.rcMonitor;
			return RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		}

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
		const HWND window = window_.load(std::memory_order_acquire);
		if (window && IsWindow(window) && !PostMessageW(window, WM_CLOSE, 0, 0) && windowThreadId_)
			PostThreadMessageW(windowThreadId_, WM_QUIT, 0, 0); // HWND 关闭投递失败时仍保证线程可退出。
		if (windowThread_)
		{
			WaitForSingleObject(windowThread_, INFINITE); // 等待窗口过程停止使用当前控制器实例。
			CloseHandle(windowThread_);
			windowThread_ = nullptr;
			windowThreadId_ = 0;
		}
		if (windowReadyEvent_)
		{
			CloseHandle(windowReadyEvent_);
			windowReadyEvent_ = nullptr;
		}
	}

	bool WindowController::Initialize(bool preconfigureNoRedirectionBitmap)
	{
		defaultCursor_ = LoadCursorW(nullptr, IDC_ARROW);
		initialExtendedStyle_ = WS_EX_TOPMOST;
		if (preconfigureNoRedirectionBitmap)
			initialExtendedStyle_ |= WS_EX_NOREDIRECTIONBITMAP;
		windowReadyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!windowReadyEvent_)
		{
			std::cout << "Create window-ready event failed. GetLastError=" << GetLastError() << std::endl;
			return false;
		}

		unsigned threadId = 0;
		const uintptr_t threadHandle = _beginthreadex(
			nullptr, 0, WindowThreadEntry, this, 0, &threadId);
		windowThread_ = reinterpret_cast<HANDLE>(threadHandle);
		windowThreadId_ = static_cast<DWORD>(threadId);
		if (!threadHandle)
		{
			std::cout << "Create window thread failed. errno=" << errno << std::endl;
			CloseHandle(windowReadyEvent_);
			windowReadyEvent_ = nullptr;
			return false;
		}

		const DWORD waitResult = WaitForSingleObject(windowReadyEvent_, INFINITE);
		CloseHandle(windowReadyEvent_);
		windowReadyEvent_ = nullptr;
		const HWND window = window_.load(std::memory_order_acquire);
		if (waitResult != WAIT_OBJECT_0 || !window)
		{
			if (waitResult != WAIT_OBJECT_0)
				std::cout << "Wait for drawing window failed. GetLastError=" << GetLastError() << std::endl;
			WaitForSingleObject(windowThread_, INFINITE);
			CloseHandle(windowThread_);
			windowThread_ = nullptr;
			windowThreadId_ = 0;
			return false;
		}

		if (window)
		{
			// 多点属性需要在第一根手指按下前写入；窗口消息中也返回相同标志。
			const ATOM tabletPropertyAtom = GlobalAddAtom(MICROSOFT_TABLETPENSERVICE_PROPERTY);
			if (!SetProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY,
				reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kTabletInputFlags))))
			{
				std::cout << "Set Tablet Pen Service window property failed. GetLastError="
					<< GetLastError() << std::endl;
			}
			if (tabletPropertyAtom) GlobalDeleteAtom(tabletPropertyAtom); // 与微软示例一致，属性本身仍由 HWND 持有。
		}
		return window != nullptr;
	}

	unsigned __stdcall WindowController::WindowThreadEntry(void* context)
	{
		auto* controller = static_cast<WindowController*>(context);
		if (controller) controller->RunWindowThread();
		return 0;
	}

	void WindowController::RunWindowThread()
	{
		const HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSEXW windowClass = {};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.style = CS_DBLCLKS;
		windowClass.lpfnWndProc = WindowProcedure;
		windowClass.hInstance = instance;
		windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
		windowClass.hIconSm = windowClass.hIcon;
		windowClass.lpszClassName = kWindowClassName;
		if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		{
			std::cout << "Register drawing window class failed. GetLastError=" << GetLastError() << std::endl;
			SetEvent(windowReadyEvent_);
			return;
		}

		const RECT monitorRect = GetPrimaryMonitorRectangle();
		size_.width = static_cast<int>(monitorRect.right - monitorRect.left);
		// 保留显示器最底部一行，避免覆盖系统边缘交互区。
		size_.height = static_cast<int>((std::max)(1L,
			monitorRect.bottom - monitorRect.top - 1L));
		const HWND window = CreateWindowExW(
			initialExtendedStyle_, kWindowClassName, L"Inkeys Draw3", WS_POPUP,
			monitorRect.left, monitorRect.top, size_.width, size_.height,
			nullptr, nullptr, instance, this);
		window_.store(window, std::memory_order_release);
		if (!window)
			std::cout << "Create drawing window failed. GetLastError=" << GetLastError() << std::endl;
		else if (!CreatePerformanceHudWindow(window))
			std::cout << "Create performance HUD window failed. GetLastError=" <<
				GetLastError() << std::endl;
		SetEvent(windowReadyEvent_); // HWND 和尺寸在事件发出前完成发布。
		if (!window) return;

		MSG message = {};
		BOOL messageResult = 0;
		while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		if (messageResult == -1)
			std::cout << "Drawing window message loop failed. GetLastError="
				<< GetLastError() << std::endl;
		// WM_QUIT 兜底不能遗留仍绑定当前控制器的 HWND。
		DestroyPerformanceHudWindow();
		if (IsWindow(window)) DestroyWindow(window);
		window_.store(nullptr, std::memory_order_release);
	}

	void WindowController::Show()
	{
		if (const HWND window = window_.load(std::memory_order_acquire))
		{
			// 独立测试宿主需要取得焦点，以便方向键验证画布平移。
			ShowWindow(window, SW_SHOWNORMAL);
			PostPerformanceHudRefresh();
		}
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

	bool WindowController::ShouldIgnoreMouseCursorMessage() const noexcept
	{
		DrawingCursorSample penSample;
		const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
		return draw3::ShouldIgnoreMouseCursorMessage(
			IsPromotedPointerMouseMessage(), ResolveGetPointerType() != nullptr,
			penSampleValid);
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
		char line[896] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[CURSOR_TRACE][state] event=%s pointerId=%u pointerEventType=%s(%u) "
			"cursorOwner=%u touchPanActive=%u realMouseTakeover=%u suppression=%u suppressedPointerId=%u "
			"haptic={pointerId=%u,pending=%u,leave=%u} "
			"penSample={valid=%u,contact=%u,inverted=%u} "
			"mouseSample={valid=%u,contact=%u} appCursor=%s appReason=%s "
			"ShouldHideSystemDrawingCursor=%u systemCursor=%s reason=%s\r\n",
			eventName ? eventName : "unknown", pointerId,
			PointerTypeName(pointerType, pointerTypeKnown),
			static_cast<unsigned>(pointerType), static_cast<unsigned>(authority),
			touchPanActive ? 1u : 0u, realMouseTakeover ? 1u : 0u,
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

	LRESULT CALLBACK WindowController::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* controller = reinterpret_cast<WindowController*>(
			GetWindowLongPtrW(window, GWLP_USERDATA));
		if (message == WM_NCCREATE)
		{
			const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
			controller = create ? static_cast<WindowController*>(create->lpCreateParams) : nullptr;
			if (!controller) return FALSE;
			SetLastError(ERROR_SUCCESS);
			if (!SetWindowLongPtrW(window, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(controller)) && GetLastError() != ERROR_SUCCESS)
			{
				std::cout << "Bind drawing window controller failed. GetLastError="
					<< GetLastError() << std::endl;
				return FALSE;
			}
		}
		if (!controller) return DefWindowProcW(window, message, wParam, lParam);

		const LRESULT result = controller->HandleWindowMessage(window, message, wParam, lParam);
		if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
		return result;
	}

	LRESULT WindowController::HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		const bool gpuTransparent = gpuTransparentComposition_.load(std::memory_order_acquire); // GPU 透明模式下由 backbuffer 负责背景。
		switch (message)
		{
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
			PostQuitMessage(0);
			return 0;

		case WM_DWMCOMPOSITIONCHANGED:
			compositionChangedRequested_.store(true, std::memory_order_release); // DWM 状态变化交给主循环刷新 presenter。
			RequestControlWake();
			return 0;

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
			if (buttonUp && penCompatibilityMouseContactSuppressed_.exchange(
				false, std::memory_order_acq_rel))
			{
				if (suppressedPenPointerId_.load(std::memory_order_acquire) == 0)
					SetPenContactSuppressedForTouchPan(false);
				break;
			}
			if (penCompatibilityMouseContactSuppressed_.load(
				std::memory_order_acquire)) break;
			if (buttonUp && ShouldSuppressMouseButtonUpCursorSample(CursorOwner()))
				break; // Pointer 终态后的兼容 Mouse Up 不能把已隐藏光标重新发布为 Hover。
			if (ShouldIgnoreMouseCursorMessage()) break;
			const bool buttonDown = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
				message == WM_MBUTTONDOWN ||
				(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;
			LARGE_INTEGER qpc = {};
			LARGE_INTEGER qpcFrequency = {};
			QueryPerformanceCounter(&qpc);
			QueryPerformanceFrequency(&qpcFrequency);
			DrawingCursorSample penSample;
			const bool penSampleValid = penCursorSample_.Read(penSample) && penSample.valid;
			const float mouseX = static_cast<float>(GET_X_LPARAM(lParam));
			const float mouseY = static_cast<float>(GET_Y_LPARAM(lParam));
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
			break;
		}

		case WM_MOUSELEAVE:
			trackingMouseLeave_ = false;
			ClearMouseCursorSample();
			if (CursorOwner() ==
				DrawingCursorPointerAuthority::Mouse)
				SetDrawingCursorOwner(DrawingCursorPointerAuthority::Unknown);
			break;

		case WM_MOUSEWHEEL:
			if (!ShouldIgnoreMouseCursorMessage())
			{
				if (touchPanActive_.load(std::memory_order_acquire))
					realMouseTakeoverDuringTouchPan_.store(true, std::memory_order_release);
				SetDrawingCursorOwner(DrawingCursorPointerAuthority::Mouse);
			}
			break;

		case WM_KEYDOWN:
			switch (wParam)
			{
			case '0':
			case VK_NUMPAD0:
				if ((lParam & kPreviousKeyStateMask) == 0)
					QueueCanvasCommand({ CanvasCommandType::NextPage });
				return 0;
			case '1':
			case VK_NUMPAD1:
				activeTool_.store(DrawingTool::Pen, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case '2':
			case VK_NUMPAD2:
				activeTool_.store(DrawingTool::Highlighter, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case '3':
			case VK_NUMPAD3:
				activeTool_.store(DrawingTool::Eraser, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case '4':
			case VK_NUMPAD4:
				activeTool_.store(DrawingTool::Laser, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case 'Q':
				activeTool_.store(DrawingTool::SolidLine, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case 'W':
				activeTool_.store(DrawingTool::DashedLine, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case 'E':
				activeTool_.store(DrawingTool::OutlineRectangle, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case 'R':
				activeTool_.store(DrawingTool::FilledRectangle, std::memory_order_relaxed);
				RequestDrawingCursorRender();
				QueueSystemCursorRefresh();
				return 0;
			case '5':
			case VK_NUMPAD5:
				if ((lParam & kPreviousKeyStateMask) == 0)
					QueueCanvasCommand({ CanvasCommandType::Undo });
				return 0;
			case '8':
			case VK_NUMPAD8:
				if ((lParam & kPreviousKeyStateMask) == 0)
					QueueCanvasCommand({ CanvasCommandType::PreviousPage });
				return 0;
			case VK_LEFT:
				if constexpr (kCanvasNavigationProductIntegrationEnabled)
				{
					if ((lParam & kPreviousKeyStateMask) == 0)
						QueueCanvasCommand({ CanvasCommandType::TranslateViewport, -64.0f, 0.0f });
				}
				return 0;
			case VK_RIGHT:
				if constexpr (kCanvasNavigationProductIntegrationEnabled)
				{
					if ((lParam & kPreviousKeyStateMask) == 0)
						QueueCanvasCommand({ CanvasCommandType::TranslateViewport, 64.0f, 0.0f });
				}
				return 0;
			case VK_UP:
				if constexpr (kCanvasNavigationProductIntegrationEnabled)
				{
					if ((lParam & kPreviousKeyStateMask) == 0)
						QueueCanvasCommand({ CanvasCommandType::TranslateViewport, 0.0f, -64.0f });
				}
				return 0;
			case VK_DOWN:
				if constexpr (kCanvasNavigationProductIntegrationEnabled)
				{
					if ((lParam & kPreviousKeyStateMask) == 0)
						QueueCanvasCommand({ CanvasCommandType::TranslateViewport, 0.0f, 64.0f });
				}
				return 0;
			case '9':
			case VK_NUMPAD9:
				exitRequested_.store(true, std::memory_order_release);
				RequestControlWake();
				return 0;
			}
			break;
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}
}
