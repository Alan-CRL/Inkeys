module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tpcshrd.h>
#include "../HiEasyX.h"

#include <cstdint>
#include <iostream>
#include <tchar.h>

module draw3.window_control;

namespace draw3
{
	WindowController* WindowController::activeController_ = nullptr;

	namespace
	{
		constexpr UINT kApplySystemCursorMessage = WM_APP + 1;
		constexpr LONG_PTR kPromotedPointerSignatureMask = 0xFFFFFF00;
		constexpr LONG_PTR kPromotedPointerSignature = 0xFF515700;

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

	WindowController::~WindowController()
	{
		if (activeController_ == this) activeController_ = nullptr;
	}

	bool WindowController::Initialize(bool preconfigureNoRedirectionBitmap)
	{
		const RECT monitorRect = GetPrimaryMonitorRectangle(); // 以主显示器区域作为初始全屏画布。
		size_.width = static_cast<int>(monitorRect.right - monitorRect.left);
		size_.height = static_cast<int>(monitorRect.bottom - monitorRect.top);
		activeController_ = this; // HiEasyX 只能接静态回调，这里转回当前实例。
		defaultCursor_ = LoadCursorW(nullptr, IDC_ARROW);

		hiex::PreSetWindowStyle(WS_POPUP);
		hiex::PreSetWindowPos(monitorRect.left, monitorRect.top);
		hiex::PreSetWindowShowState(SW_HIDE); // 避免透明 presenter 首次提交前暴露 HiEasyX 白色类背景。
		if (preconfigureNoRedirectionBitmap)
		{
			// Win7 不支持该扩展样式，因此只在确认 DComp API 存在时预置。
			hiex::PreSetWindowStyleEx(WS_EX_NOREDIRECTIONBITMAP);
		}
		window_ = hiex::initgraph_win32(size_.width, size_.height, EW_SHOWCONSOLE, _T(""), WindowProcedure);
		if (window_)
		{
			// 多点属性需要在第一根手指按下前写入；窗口消息中也返回相同标志。
			const ATOM tabletPropertyAtom = GlobalAddAtom(MICROSOFT_TABLETPENSERVICE_PROPERTY);
			if (!SetProp(window_, MICROSOFT_TABLETPENSERVICE_PROPERTY,
				reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kTabletInputFlags))))
			{
				std::cout << "Set Tablet Pen Service window property failed. GetLastError="
					<< GetLastError() << std::endl;
			}
			if (tabletPropertyAtom) GlobalDeleteAtom(tabletPropertyAtom); // 与微软示例一致，属性本身仍由 HWND 持有。
		}
		return window_ != nullptr;
	}

	void WindowController::Show()
	{
		if (window_) ShowWindow(window_, SW_SHOWNORMAL);
	}

	HWND WindowController::Handle() const
	{
		return window_;
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

	bool WindowController::TryGetMouseMessage(MouseMessage& message) const
	{
		ExMessage nativeMessage = {};
		if (!hiex::peekmessage_win32(&nativeMessage, EM_MOUSE, true, window_)) return false;
		message.message = nativeMessage.message;
		message.x = nativeMessage.x;
		message.y = nativeMessage.y;
		return true;
	}

	void WindowController::FlushMouseMessages() const
	{
		hiex::flushmessage_win32(EM_MOUSE, window_);
	}

	bool WindowController::ConsumeClearCanvasRequest()
	{
		return clearCanvasRequested_.exchange(false, std::memory_order_relaxed);
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
		else eraserCursorAppearance_ = appearance;
		RequestDrawingCursorRender();
		return true;
	}

	DrawingCursorAppearance WindowController::CursorAppearanceForTool(
		DrawingTool tool) const noexcept
	{
		if (tool == DrawingTool::Pen) return penCursorAppearance_;
		if (tool == DrawingTool::Highlighter) return highlighterCursorAppearance_;
		return eraserCursorAppearance_;
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
			activeTool <= static_cast<int32_t>(DrawingTool::Eraser))
			return static_cast<DrawingTool>(activeTool);
		return ActiveTool();
	}

	DrawingCursorPointerAuthority WindowController::CursorPointerAuthority() const noexcept
	{
		return drawingCursorPointerAuthority_.load(std::memory_order_acquire);
	}

	bool WindowController::ReadPenCursorSample(DrawingCursorSample& sample) const noexcept
	{
		return penCursorSample_.Read(sample);
	}

	bool WindowController::ReadMouseCursorSample(DrawingCursorSample& sample) const noexcept
	{
		return mouseCursorSample_.Read(sample);
	}

	void WindowController::PublishPenCursorSample(
		const DrawingCursorSample& sample) noexcept
	{
		DrawingCursorSample previous;
		penCursorSample_.Read(previous);
		if (!penCursorSample_.Publish(sample)) return;
		if (!ResolveGetPointerType())
			drawingCursorPointerAuthority_.store(
				DrawingCursorPointerAuthority::Unknown, std::memory_order_release);
		RequestDrawingCursorRender();
		if (previous.valid != sample.valid || previous.inContact != sample.inContact ||
			previous.inverted != sample.inverted) QueueSystemCursorRefresh();
	}

	void WindowController::ClearPenCursorSample() noexcept
	{
		if (!penCursorSample_.Clear()) return;
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	void WindowController::PublishMouseCursorSample(
		const DrawingCursorSample& sample) noexcept
	{
		DrawingCursorSample previous;
		mouseCursorSample_.Read(previous);
		if (!mouseCursorSample_.Publish(sample)) return;
		RequestDrawingCursorRender();
		if (previous.valid != sample.valid || previous.inContact != sample.inContact)
			QueueSystemCursorRefresh();
	}

	void WindowController::ClearMouseCursorSample() noexcept
	{
		if (!mouseCursorSample_.Clear()) return;
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	bool WindowController::ShouldIgnoreMouseCursorMessage() const noexcept
	{
		if (IsPromotedPointerMouseMessage()) return true;
		const DrawingCursorPointerAuthority authority =
			drawingCursorPointerAuthority_.load(std::memory_order_acquire);
		DrawingCursorSample penSample;
		if (authority == DrawingCursorPointerAuthority::Pen)
			return penCursorSample_.Read(penSample) && penSample.valid;
		if (ResolveGetPointerType()) return false;

		// Windows 7 没有 Pointer API：Pen 仍在 RTS range 内时，低优先级鼠标消息不得抢占。
		return penCursorSample_.Read(penSample) && penSample.valid;
	}

	void WindowController::RequestDrawingCursorRender() noexcept
	{
		drawingCursorRenderRequested_.store(true, std::memory_order_release);
		RequestControlWake();
	}

	void WindowController::QueueSystemCursorRefresh() noexcept
	{
		if (!window_ || systemCursorRefreshPosted_.exchange(true, std::memory_order_acq_rel)) return;
		if (!PostMessageW(window_, kApplySystemCursorMessage, 0, 0))
			systemCursorRefreshPosted_.store(false, std::memory_order_release);
	}

	void WindowController::SetDrawingCursorPointerAuthority(
		DrawingCursorPointerAuthority authority) noexcept
	{
		if (drawingCursorPointerAuthority_.exchange(authority,
			std::memory_order_acq_rel) == authority) return;
		RequestDrawingCursorRender();
		QueueSystemCursorRefresh();
	}

	void WindowController::ApplyWindowCursor() noexcept
	{
		POINT cursorPosition = {};
		if (!window_ || !GetCursorPos(&cursorPosition)) return;
		const HWND cursorWindow = WindowFromPoint(cursorPosition);
		if (cursorWindow != window_ && (!cursorWindow || !IsChild(window_, cursorWindow)))
			return; // 私有刷新消息不能改变其他窗口当前拥有的系统光标。
		DrawingCursorSample penSample;
		DrawingCursorSample mouseSample;
		penCursorSample_.Read(penSample);
		mouseCursorSample_.Read(mouseSample);
		const DrawingTool tool = EffectiveDrawingCursorTool();
		const bool hide = ShouldHideSystemDrawingCursor(
			drawingCursorPointerAuthority_.load(std::memory_order_acquire),
			tool == DrawingTool::Eraser, penSample.valid, mouseSample.valid);
		SetCursor(hide ? nullptr : defaultCursor_); // 仅影响当前 HWND，不使用全局计数式 ShowCursor。
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
		if (!activeController_) return HIWINDOW_DEFAULT_PROC;
		return activeController_->HandleWindowMessage(window, message, wParam, lParam); // 静态窗口过程转发到当前控制器实例。
	}

	LRESULT WindowController::HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		const bool gpuTransparent = gpuTransparentComposition_.load(std::memory_order_acquire); // GPU 透明模式下由 backbuffer 负责背景。
		switch (message)
		{
		case kApplySystemCursorMessage:
			systemCursorRefreshPosted_.store(false, std::memory_order_release);
			ApplyWindowCursor();
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				ApplyWindowCursor();
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
			const DrawingCursorPointerAuthority previousAuthority =
				drawingCursorPointerAuthority_.load(std::memory_order_acquire);
			const DrawingCursorPointerAuthority leaveAuthority = details.typeKnown
				? AuthorityForPointerType(details.type) : previousAuthority;
			if (leaveAuthority == DrawingCursorPointerAuthority::Pen) ClearPenCursorSample();
			// 保留离开的设备 authority，防止陈旧 Mouse 样本在没有新鼠标移动时恢复。
			SetDrawingCursorPointerAuthority(leaveAuthority);
			lastHapticPenInfoPointerId_ = 0;
			lastHapticPenInfoKnown_ = false;
			lastHapticPenInfoEraser_ = false;
			hapticPointerLeaveRequested_.store(true, std::memory_order_release);
			RequestControlWake();
			break;
		}
		case WM_POINTERENTER:
		case WM_POINTERUPDATE:
		case WM_POINTERDOWN:
		case WM_POINTERUP:
		{
			const uint32_t pointerId = static_cast<uint32_t>(LOWORD(wParam));
			const PointerDetails details = QueryPointerDetails(pointerId);
			if (details.typeKnown)
				SetDrawingCursorPointerAuthority(AuthorityForPointerType(details.type));
			if (details.type == PT_PEN && details.positionKnown)
			{
				POINT clientPosition = details.screenPosition;
				if (ScreenToClient(window, &clientPosition))
				{
					DrawingCursorSample sample;
					sample.x = static_cast<float>(clientPosition.x);
					sample.y = static_cast<float>(clientPosition.y);
					sample.valid = true;
					sample.inverted = details.eraserHint;
					sample.inContact = details.inContact;
					LARGE_INTEGER qpc = {};
					QueryPerformanceCounter(&qpc);
					sample.qpc = qpc.QuadPart;
					PublishPenCursorSample(sample);
				}
			}
			if ((message == WM_POINTERENTER || message == WM_POINTERDOWN) && pointerId != 0)
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
			break;
		}

		case WM_DESTROY:
			penCursorSample_.Clear();
			mouseCursorSample_.Clear();
			drawingCursorPointerAuthority_.store(
				DrawingCursorPointerAuthority::Unknown, std::memory_order_release);
			SetCursor(defaultCursor_);
			RemoveProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY);
			exitRequested_.store(true, std::memory_order_release); // 通知主循环退出。
			RequestControlWake();
			break;

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
			if (ShouldIgnoreMouseCursorMessage()) break;
			SetDrawingCursorPointerAuthority(DrawingCursorPointerAuthority::Mouse);
			if (message == WM_MOUSEMOVE && !trackingMouseLeave_)
			{
				TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
				trackingMouseLeave_ = TrackMouseEvent(&tracking) != FALSE;
			}
			const bool buttonDown = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
				message == WM_MBUTTONDOWN ||
				(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;
			DrawingCursorSample sample;
			sample.x = static_cast<float>(GET_X_LPARAM(lParam));
			sample.y = static_cast<float>(GET_Y_LPARAM(lParam));
			sample.valid = true;
			sample.inContact = buttonDown;
			LARGE_INTEGER qpc = {};
			QueryPerformanceCounter(&qpc);
			sample.qpc = qpc.QuadPart;
			PublishMouseCursorSample(sample);
			break;
		}

		case WM_MOUSELEAVE:
			trackingMouseLeave_ = false;
			ClearMouseCursorSample();
			if (drawingCursorPointerAuthority_.load(std::memory_order_acquire) ==
				DrawingCursorPointerAuthority::Mouse)
				SetDrawingCursorPointerAuthority(DrawingCursorPointerAuthority::Unknown);
			break;

		case WM_MOUSEWHEEL:
			if (!ShouldIgnoreMouseCursorMessage())
				SetDrawingCursorPointerAuthority(DrawingCursorPointerAuthority::Mouse);
			break;

		case WM_KEYDOWN:
			switch (wParam)
			{
			case '0':
			case VK_NUMPAD0:
				clearCanvasRequested_.store(true, std::memory_order_release);
				RequestControlWake();
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
			case '9':
			case VK_NUMPAD9:
				exitRequested_.store(true, std::memory_order_release);
				RequestControlWake();
				return 0;
			}
			break;
		}
		return HIWINDOW_DEFAULT_PROC;
	}
}
