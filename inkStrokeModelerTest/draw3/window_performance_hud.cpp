module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <windows.h>

module draw3.window_control;

namespace draw3
{
	namespace
	{
		constexpr wchar_t kPerformanceHudWindowClassName[] =
			L"InkeysDraw3PerformanceHudWindow";
		constexpr UINT kRefreshPerformanceHudMessage = WM_APP + 40;
		constexpr BYTE kHudBackgroundAlpha = 176;
		constexpr BYTE kHudBorderAlpha = 208;
		constexpr BYTE kHudTextAlpha = 238;

		UINT PerformanceHudDpi(HWND window) noexcept
		{
			using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
			static const GetDpiForWindowFunction getDpiForWindow = []() noexcept
				{
					const HMODULE user32 = GetModuleHandleW(L"user32.dll");
					return user32 ? reinterpret_cast<GetDpiForWindowFunction>(
						GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
				}();
			if (getDpiForWindow)
			{
				const UINT dpi = getDpiForWindow(window);
				if (dpi != 0) return dpi;
			}
			HDC screen = GetDC(nullptr);
			const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
			if (screen) ReleaseDC(nullptr, screen);
			return static_cast<UINT>(std::max(dpi, 96));
		}

		RECT PrimaryMonitorWorkArea() noexcept
		{
			const HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
			MONITORINFO info = {};
			info.cbSize = sizeof(info);
			if (monitor && GetMonitorInfoW(monitor, &info)) return info.rcWork;
			return RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		}

		uint32_t StraightBgra(BYTE red, BYTE green, BYTE blue) noexcept
		{
			return static_cast<uint32_t>(blue) |
				(static_cast<uint32_t>(green) << 8) |
				(static_cast<uint32_t>(red) << 16);
		}

		BYTE Premultiply(BYTE color, BYTE alpha) noexcept
		{
			return static_cast<BYTE>((static_cast<uint32_t>(color) * alpha + 127) / 255);
		}

		void ConvertHudPixelsToPremultiplied(
			uint32_t* pixels, int width, int height) noexcept
		{
			if (!pixels || width <= 0 || height <= 0) return;
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					const bool border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
					const BYTE baseRed = border ? 52 : 20;
					const BYTE baseGreen = border ? 58 : 23;
					const BYTE baseBlue = border ? 66 : 29;
					const BYTE baseAlpha = border ? kHudBorderAlpha : kHudBackgroundAlpha;
					const uint32_t pixel = pixels[static_cast<size_t>(y) * width + x];
					const BYTE blue = static_cast<BYTE>(pixel & 0xFF);
					const BYTE green = static_cast<BYTE>((pixel >> 8) & 0xFF);
					const BYTE red = static_cast<BYTE>((pixel >> 16) & 0xFF);
					const double redCoverage = static_cast<double>(std::max<int>(red - baseRed, 0)) /
						std::max<int>(255 - baseRed, 1);
					const double greenCoverage = static_cast<double>(std::max<int>(green - baseGreen, 0)) /
						std::max<int>(255 - baseGreen, 1);
					const double blueCoverage = static_cast<double>(std::max<int>(blue - baseBlue, 0)) /
						std::max<int>(255 - baseBlue, 1);
					const double coverage = std::clamp(
						std::max({ redCoverage, greenCoverage, blueCoverage }), 0.0, 1.0);
					const BYTE alpha = static_cast<BYTE>(std::lround(
						baseAlpha + coverage * (kHudTextAlpha - baseAlpha)));
					pixels[static_cast<size_t>(y) * width + x] =
						static_cast<uint32_t>(Premultiply(blue, alpha)) |
						(static_cast<uint32_t>(Premultiply(green, alpha)) << 8) |
						(static_cast<uint32_t>(Premultiply(red, alpha)) << 16) |
						(static_cast<uint32_t>(alpha) << 24);
				}
			}
		}
	}

	bool WindowController::CreatePerformanceHudWindow(HWND owner)
	{
		if (!owner) return false;
		const HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSEXW windowClass = {};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = PerformanceHudWindowProcedure;
		windowClass.hInstance = instance;
		windowClass.lpszClassName = kPerformanceHudWindowClassName;
		if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return false;

		const DWORD extendedStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT |
			WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
		const HWND hudWindow = CreateWindowExW(
			extendedStyle, kPerformanceHudWindowClassName, L"", WS_POPUP,
			0, 0, 1, 1, owner, nullptr, instance, this);
		performanceHudWindow_.store(hudWindow, std::memory_order_release);
		return hudWindow != nullptr;
	}

	void WindowController::DestroyPerformanceHudWindow() noexcept
	{
		const HWND hudWindow = performanceHudWindow_.exchange(
			nullptr, std::memory_order_acq_rel);
		if (hudWindow && IsWindow(hudWindow)) DestroyWindow(hudWindow);
	}

	void WindowController::SetPerformanceHudEnabled(bool enabled) noexcept
	{
		performanceHudEnabled_.store(enabled, std::memory_order_release);
		PostPerformanceHudRefresh();
	}

	bool WindowController::GetPerformanceHudEnabled() const noexcept
	{
		return performanceHudEnabled_.load(std::memory_order_acquire);
	}

	void WindowController::UpdatePerformanceHudText(std::wstring_view text)
	{
		bool changed = false;
		{
			std::lock_guard lock(performanceHudMutex_);
			if (performanceHudText_ != text)
			{
				performanceHudText_.assign(text);
				changed = true;
			}
		}
		if (changed) PostPerformanceHudRefresh();
	}

	void WindowController::PostPerformanceHudRefresh() noexcept
	{
		const HWND hudWindow = performanceHudWindow_.load(std::memory_order_acquire);
		if (hudWindow) PostMessageW(hudWindow, kRefreshPerformanceHudMessage, 0, 0);
	}

	void WindowController::RefreshPerformanceHudWindow()
	{
		const HWND hudWindow = performanceHudWindow_.load(std::memory_order_acquire);
		const HWND owner = hudWindow ? GetWindow(hudWindow, GW_OWNER) : nullptr;
		if (!hudWindow || !performanceHudEnabled_.load(std::memory_order_acquire) ||
			!owner || !IsWindowVisible(owner))
		{
			if (hudWindow) ShowWindow(hudWindow, SW_HIDE);
			return;
		}

		std::wstring text;
		{
			std::lock_guard lock(performanceHudMutex_);
			text = performanceHudText_;
		}
		if (text.empty())
		{
			ShowWindow(hudWindow, SW_HIDE);
			return;
		}

		const UINT dpi = PerformanceHudDpi(hudWindow);
		const int padding = MulDiv(12, static_cast<int>(dpi), 96);
		const int minimumWidth = MulDiv(420, static_cast<int>(dpi), 96);
		const int fontHeight = -MulDiv(15, static_cast<int>(dpi), 96);
		HDC memoryDc = CreateCompatibleDC(nullptr);
		HFONT font = CreateFontW(fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
		if (!memoryDc || !font)
		{
			if (font) DeleteObject(font);
			if (memoryDc) DeleteDC(memoryDc);
			ShowWindow(hudWindow, SW_HIDE);
			return;
		}
		const HGDIOBJ oldFont = SelectObject(memoryDc, font);
		RECT measured = {};
		DrawTextW(memoryDc, text.c_str(), static_cast<int>(text.size()), &measured,
			DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX);
		const int measuredWidth = static_cast<int>(measured.right);
		const int measuredHeight = static_cast<int>(measured.bottom);
		const int width = std::clamp(
			std::max(minimumWidth, measuredWidth + padding * 2), minimumWidth, 960);
		const int height = std::clamp(measuredHeight + padding * 2,
			MulDiv(80, static_cast<int>(dpi), 96), MulDiv(220, static_cast<int>(dpi), 96));

		BITMAPINFO bitmapInfo = {};
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = width;
		bitmapInfo.bmiHeader.biHeight = -height;
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;
		void* bitmapBits = nullptr;
		HBITMAP bitmap = CreateDIBSection(
			memoryDc, &bitmapInfo, DIB_RGB_COLORS, &bitmapBits, nullptr, 0);
		if (!bitmap || !bitmapBits)
		{
			if (bitmap) DeleteObject(bitmap);
			SelectObject(memoryDc, oldFont);
			DeleteObject(font);
			DeleteDC(memoryDc);
			ShowWindow(hudWindow, SW_HIDE);
			return;
		}
		const HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
		auto* pixels = static_cast<uint32_t*>(bitmapBits);
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const bool border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
				pixels[static_cast<size_t>(y) * width + x] = border
					? StraightBgra(52, 58, 66) : StraightBgra(20, 23, 29);
			}
		}
		SetBkMode(memoryDc, TRANSPARENT);
		SetTextColor(memoryDc, RGB(245, 247, 250));
		RECT textRect = { padding, padding, width - padding, height - padding };
		DrawTextW(memoryDc, text.c_str(), static_cast<int>(text.size()), &textRect,
			DT_LEFT | DT_TOP | DT_NOPREFIX | DT_NOCLIP);
		ConvertHudPixelsToPremultiplied(pixels, width, height);

		const RECT workArea = PrimaryMonitorWorkArea();
		const int margin = MulDiv(12, static_cast<int>(dpi), 96);
		POINT destination = { workArea.left + margin, workArea.top + margin };
		SIZE size = { width, height };
		POINT source = {};
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		HDC screenDc = GetDC(nullptr);
		const BOOL updated = screenDc && UpdateLayeredWindow(hudWindow, screenDc,
			&destination, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);
		if (screenDc) ReleaseDC(nullptr, screenDc);

		SelectObject(memoryDc, oldBitmap);
		SelectObject(memoryDc, oldFont);
		DeleteObject(bitmap);
		DeleteObject(font);
		DeleteDC(memoryDc);
		if (updated)
		{
			ShowWindow(hudWindow, SW_SHOWNOACTIVATE);
			SetWindowPos(hudWindow, HWND_TOP, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		else
		{
			ShowWindow(hudWindow, SW_HIDE);
		}
	}

	LRESULT CALLBACK WindowController::PerformanceHudWindowProcedure(
		HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* controller = reinterpret_cast<WindowController*>(
			GetWindowLongPtrW(window, GWLP_USERDATA));
		if (message == WM_NCCREATE)
		{
			const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
			controller = create ? static_cast<WindowController*>(create->lpCreateParams) : nullptr;
			if (!controller) return FALSE;
			SetWindowLongPtrW(window, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(controller));
		}
		if (!controller) return DefWindowProcW(window, message, wParam, lParam);

		switch (message)
		{
		case kRefreshPerformanceHudMessage:
			controller->RefreshPerformanceHudWindow();
			return 0;
		case WM_NCHITTEST:
			return HTTRANSPARENT;
		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
		case WM_ERASEBKGND:
			return 1;
		case WM_NCDESTROY:
			controller->performanceHudWindow_.compare_exchange_strong(
				window, nullptr, std::memory_order_acq_rel);
			SetWindowLongPtrW(window, GWLP_USERDATA, 0);
			break;
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}
}
