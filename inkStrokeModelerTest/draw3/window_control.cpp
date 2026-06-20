module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dwmapi.h>
#include "../HiEasyX.h"

#include <tchar.h>

module draw3.window_control;

namespace draw3
{
	WindowController* WindowController::activeController_ = nullptr;

	namespace
	{
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
		const RECT monitorRect = GetPrimaryMonitorRectangle();
		size_.width = static_cast<int>(monitorRect.right - monitorRect.left);
		size_.height = static_cast<int>(monitorRect.bottom - monitorRect.top);
		activeController_ = this;

		hiex::PreSetWindowStyle(WS_POPUP);
		hiex::PreSetWindowPos(monitorRect.left, monitorRect.top);
		if (preconfigureNoRedirectionBitmap)
		{
			// Win7 不支持该扩展样式，因此只在确认 DComp API 存在时预置。
			hiex::PreSetWindowStyleEx(WS_EX_NOREDIRECTIONBITMAP);
		}
		window_ = hiex::initgraph_win32(size_.width, size_.height, EW_SHOWCONSOLE, _T(""), WindowProcedure);
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
		if (!resizeRequested_.exchange(false, std::memory_order_acquire)) return false;
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
	}

	void WindowController::SetGpuTransparentComposition(bool enabled)
	{
		gpuTransparentComposition_.store(enabled, std::memory_order_release);
	}

	int WindowController::BrushShapeType() const
	{
		return brushShapeType_.load(std::memory_order_relaxed);
	}

	bool WindowController::ExitRequested() const
	{
		return exitRequested_.load(std::memory_order_acquire);
	}

	LRESULT CALLBACK WindowController::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!activeController_) return HIWINDOW_DEFAULT_PROC;
		return activeController_->HandleWindowMessage(window, message, wParam, lParam);
	}

	LRESULT WindowController::HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		const bool gpuTransparent = gpuTransparentComposition_.load(std::memory_order_acquire);
		switch (message)
		{
		case WM_DESTROY:
			exitRequested_.store(true, std::memory_order_release);
			break;

		case WM_DWMCOMPOSITIONCHANGED:
			compositionChangedRequested_.store(true, std::memory_order_release);
			return 0;

		case WM_ERASEBKGND:
			if (gpuTransparent) return 1;
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
			if (gpuTransparent) RequestFullPresent();
			break;

		case WM_WINDOWPOSCHANGED:
			if (gpuTransparent)
			{
				const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam);
				if (!position || !(position->flags & SWP_NOMOVE) || !(position->flags & SWP_NOSIZE)) RequestFullPresent();
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
				}
				if (gpuTransparent) RequestFullPresent();
			}
			break;

		case WM_KEYDOWN:
			switch (wParam)
			{
			case '0':
			case VK_NUMPAD0:
				clearCanvasRequested_.store(true, std::memory_order_relaxed);
				return 0;
			case '1':
			case VK_NUMPAD1:
				brushShapeType_.store(0, std::memory_order_relaxed);
				return 0;
			case '9':
			case VK_NUMPAD9:
				exitRequested_.store(true, std::memory_order_release);
				return 0;
			}
			break;
		}
		return HIWINDOW_DEFAULT_PROC;
	}
}
