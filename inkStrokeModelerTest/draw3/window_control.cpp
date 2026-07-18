module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dwmapi.h>
#include <tpcshrd.h>
#include "../HiEasyX.h"

#include <iostream>
#include <tchar.h>

module draw3.window_control;

namespace draw3
{
	WindowController* WindowController::activeController_ = nullptr;

	namespace
	{
		constexpr DWORD kTabletInputFlags =
			TABLET_ENABLE_MULTITOUCHDATA |
			TABLET_DISABLE_PRESSANDHOLD |
			TABLET_DISABLE_PENTAPFEEDBACK |
			TABLET_DISABLE_PENBARRELFEEDBACK |
			TABLET_DISABLE_FLICKS;

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

	bool WindowController::Initialize(bool preconfigureNoRedirectionBitmap)
	{
		const RECT monitorRect = GetPrimaryMonitorRectangle(); // 以主显示器区域作为初始全屏画布。
		size_.width = static_cast<int>(monitorRect.right - monitorRect.left);
		size_.height = static_cast<int>(monitorRect.bottom - monitorRect.top);
		activeController_ = this; // HiEasyX 只能接静态回调，这里转回当前实例。

		hiex::PreSetWindowStyle(WS_POPUP);
		hiex::PreSetWindowPos(monitorRect.left, monitorRect.top);
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
		case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
			// 显式接收 RTS 多点数据，并禁止长按/轻拂抢占普通笔输入。
			return static_cast<LRESULT>(kTabletInputFlags);

		case WM_DESTROY:
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
				return 0;
			case '2':
			case VK_NUMPAD2:
				activeTool_.store(DrawingTool::Highlighter, std::memory_order_relaxed);
				return 0;
			case '3':
			case VK_NUMPAD3:
				activeTool_.store(DrawingTool::Eraser, std::memory_order_relaxed);
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
