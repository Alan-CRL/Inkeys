module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>

#include "../../../IdtFreezeFrame.h"
#include "../../../IdtImage.h"

module Inkeys.UI.Whiteboard;

import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
import Inkeys.Window;

namespace Inkeys::UI::Whiteboard
{
	namespace
	{
		using Client = Inkeys::UI::RenderPipeline::Client;
		using FrameContext = Inkeys::UI::RenderPipeline::FrameContext;
		using FrameResult = Inkeys::UI::RenderPipeline::FrameResult;
		using WindowRole = Inkeys::Window::WindowRole;

		constexpr std::array<Client, 3> Clients{
			Client::WhiteboardFreeze,
			Client::WhiteboardLeft,
			Client::WhiteboardRight,
		};
		constexpr std::array<WindowRole, 2> ControlRoles{
			WindowRole::WhiteboardLeft,
			WindowRole::WhiteboardRight,
		};

		enum class HitTarget : std::uint8_t { None, Previous, Next };
		std::atomic_bool initialized = false;
		std::atomic_bool active = false;
		std::atomic_bool committedBackgroundActive = false;
		std::atomic_int currentPage = 1;
		std::atomic_int totalPage = 1;
		std::atomic_bool switching = false;
		std::array<std::atomic<HitTarget>, 2> hover{};
		std::array<std::atomic<HitTarget>, 2> pressed{};
		Inkeys::Display::Subscription displaySubscription;
		std::mutex callbackMutex;
		BusinessCallbacks business;

		[[nodiscard]] RECT PrimaryBounds() noexcept
		{
			const auto snapshot = Inkeys::Display::GetSnapshot();
			if (const auto* monitor = snapshot ? snapshot->Primary() : nullptr)
				return monitor->bounds;
			return { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		}

		[[nodiscard]] float DpiScale(HWND hwnd) noexcept
		{
			const auto snapshot = Inkeys::Display::GetSnapshot();
			if (const auto* monitor = snapshot ? snapshot->Primary() : nullptr)
			{
				const UINT dpi = monitor->effectiveDpiX ? monitor->effectiveDpiX :
					USER_DEFAULT_SCREEN_DPI;
				return static_cast<float>(dpi) /
					static_cast<float>(USER_DEFAULT_SCREEN_DPI);
			}
			// Display 尚未发布首个快照时动态解析 DPI API，避免引入 Win10 静态导入。
			using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
			static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			const UINT dpi = getDpiForWindow && hwnd ? getDpiForWindow(hwnd) : 0;
			return static_cast<float>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) /
				static_cast<float>(USER_DEFAULT_SCREEN_DPI);
		}

		[[nodiscard]] std::size_t ControlIndex(HWND hwnd) noexcept
		{
			return Inkeys::Window::GetService().Handle(WindowRole::WhiteboardRight) == hwnd ? 1u : 0u;
		}

		[[nodiscard]] HitTarget HitTest(HWND hwnd, POINT point) noexcept
		{
			const auto layout = ResolveControlLayout({}, DpiScale(hwnd), true);
			if (PtInRect(&layout.previous, point)) return HitTarget::Previous;
			if (PtInRect(&layout.next, point)) return HitTarget::Next;
			return HitTarget::None;
		}

		[[nodiscard]] bool TargetEnabled(HitTarget target) noexcept
		{
			const PageState page = ResolvePageState(currentPage.load(std::memory_order_acquire),
				totalPage.load(std::memory_order_acquire),
				switching.load(std::memory_order_acquire));
			return target == HitTarget::Previous ? page.previousEnabled :
				target == HitTarget::Next ? page.nextEnabled : false;
		}

		void RequestControls() noexcept
		{
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Mask(Client::WhiteboardLeft) |
				Inkeys::UI::RenderPipeline::Mask(Client::WhiteboardRight));
		}

		void Invoke(HitTarget target)
		{
			std::function<void()> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = target == HitTarget::Previous
					? business.previousPage : business.nextPage;
			}
			if (callback && TargetEnabled(target)) callback();
		}

		void ConfigureUlw(UPDATELAYEREDWINDOWINFO& info, HDC screen,
			HDC source, POINT& destination, POINT& sourcePoint, SIZE& size,
			BLENDFUNCTION& blend) noexcept
		{
			blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			info = {};
			info.cbSize = sizeof(info);
			info.hdcDst = screen;
			info.pptDst = &destination;
			info.psize = &size;
			info.hdcSrc = source;
			info.pptSrc = &sourcePoint;
			info.pblend = &blend;
			info.dwFlags = ULW_ALPHA;
		}

		[[nodiscard]] bool SubmitLayered(HWND hwnd, Inkeys::Graphics::DibSurface& surface,
			POINT destination, bool freeze) noexcept
		{
			HDC screen = GetDC(nullptr);
			if (!screen) return false;
			POINT source{};
			SIZE size{ surface.width(), surface.height() };
			BLENDFUNCTION blend{};
			UPDATELAYEREDWINDOWINFO info{};
			ConfigureUlw(info, screen, surface.dc(), destination, source, size, blend);
			const bool result = freeze
				? SubmitFreezeSurface(hwnd, &info, true)
				: UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
			ReleaseDC(nullptr, screen);
			return result;
		}

		void DrawRoundedSurface(Gdiplus::Graphics& graphics, const RECT& rect,
			BYTE fillAlpha, BYTE frameAlpha)
		{
			const float x = static_cast<float>(rect.left);
			const float y = static_cast<float>(rect.top);
			const float width = static_cast<float>(rect.right - rect.left);
			const float height = static_cast<float>(rect.bottom - rect.top);
			const float radius = (std::min)(8.0F, height * 0.18F);
			Gdiplus::GraphicsPath path;
			path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
			path.AddArc(x + width - radius * 2, y, radius * 2, radius * 2, 270, 90);
			path.AddArc(x + width - radius * 2, y + height - radius * 2,
				radius * 2, radius * 2, 0, 90);
			path.AddArc(x, y + height - radius * 2, radius * 2, radius * 2, 90, 90);
			path.CloseFigure();
			Gdiplus::SolidBrush fill(Gdiplus::Color(fillAlpha, 250, 250, 250));
			Gdiplus::Pen border(Gdiplus::Color(frameAlpha, 120, 120, 120), 1.0F);
			graphics.FillPath(&fill, &path);
			graphics.DrawPath(&border, &path);
		}

		void DrawRoundedButton(Gdiplus::Graphics& graphics, const RECT& rect,
			bool hot, bool down, bool enabled)
		{
			const BYTE alpha = !enabled ? 20 : down ? 90 : hot ? 55 : 0;
			DrawRoundedSurface(graphics, rect, alpha, 0);
		}

		void DrawChevron(Gdiplus::Graphics& graphics, const RECT& rect,
			bool next, bool enabled)
		{
			const float cx = (rect.left + rect.right) * 0.5F;
			const float cy = (rect.top + rect.bottom) * 0.5F;
			const float sign = next ? 1.0F : -1.0F;
			const float size = static_cast<float>(rect.bottom - rect.top) * 0.22F;
			Gdiplus::PointF points[]{
				{ cx - sign * size * 0.55F, cy - size },
				{ cx + sign * size * 0.55F, cy },
				{ cx - sign * size * 0.55F, cy + size },
			};
			Gdiplus::Pen pen(Gdiplus::Color(enabled ? 255 : 85, 50, 50, 50), 3.0F);
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);
			pen.SetLineJoin(Gdiplus::LineJoinRound);
			graphics.DrawLines(&pen, points, 3);
		}

		void DrawCenteredText(Gdiplus::Graphics& graphics, const std::wstring& text,
			const RECT& rect, float size, bool bold, BYTE alpha)
		{
			Gdiplus::FontFamily family(L"Segoe UI");
			Gdiplus::Font font(&family, size, bold ? Gdiplus::FontStyleBold :
				Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::StringFormat format;
			format.SetAlignment(Gdiplus::StringAlignmentCenter);
			format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, 50, 50, 50));
			const Gdiplus::RectF bounds(static_cast<float>(rect.left), static_cast<float>(rect.top),
				static_cast<float>(rect.right - rect.left),
				static_cast<float>(rect.bottom - rect.top));
			graphics.DrawString(text.c_str(), -1, &font, bounds, &format, &brush);
		}

		FrameResult RenderBackground()
		{
			const HWND hwnd = Inkeys::Window::GetService().Handle(WindowRole::Freeze);
			if (!hwnd) return FrameResult::Retry;
			const bool show = active.load(std::memory_order_acquire);
			const RECT bounds = PrimaryBounds();
			const int width = show ? bounds.right - bounds.left : 1;
			const int height = show ? bounds.bottom - bounds.top : 1;
			Inkeys::Graphics::DibSurface surface;
			if (!surface.resize((std::max)(1, width), (std::max)(1, height)))
				return FrameResult::Retry;
			// DibSurface 是 BGRA 内存布局；0xff123b32 对应固定背景 #123B32。
			if (show) surface.clear(0xff123b32u);
			else surface.clear();
			const POINT destination{ show ? bounds.left : 0, show ? bounds.top : 0 };
			if (show)
				(void)Inkeys::Window::GetService().SetBounds(WindowRole::Freeze, bounds);
			if (!SubmitLayered(hwnd, surface, destination, true)) return FrameResult::Retry;
			if (show) (void)Inkeys::Window::GetService().Show(WindowRole::Freeze);
			else (void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
			committedBackgroundActive.store(show, std::memory_order_release);
			return FrameResult::Idle;
		}

		FrameResult RenderControl(bool left)
		{
			const std::size_t index = left ? 0u : 1u;
			auto& service = Inkeys::Window::GetService();
			const HWND hwnd = service.Handle(ControlRoles[index]);
			if (!hwnd) return FrameResult::Retry;
			if (!active.load(std::memory_order_acquire))
			{
				(void)service.Hide(ControlRoles[index]);
				return FrameResult::Idle;
			}
			const float scale = DpiScale(hwnd);
			const ControlLayout layout = ResolveControlLayout(PrimaryBounds(), scale, left);
			const int width = layout.bounds.right - layout.bounds.left;
			const int height = layout.bounds.bottom - layout.bounds.top;
			Inkeys::Graphics::DibSurface surface;
			if (!surface.resize(width, height)) return FrameResult::Retry;
			surface.clear();
			Gdiplus::Graphics graphics(surface.dc());
			graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
			DrawRoundedSurface(graphics, { 0, 0, width, height }, 242, 45);
			const HitTarget hot = hover[index].load(std::memory_order_acquire);
			const HitTarget down = pressed[index].load(std::memory_order_acquire);
			const PageState page = ResolvePageState(currentPage.load(std::memory_order_acquire),
				totalPage.load(std::memory_order_acquire),
				switching.load(std::memory_order_acquire));
			DrawRoundedButton(graphics, layout.previous,
				hot == HitTarget::Previous, down == HitTarget::Previous,
				page.previousEnabled);
			DrawRoundedButton(graphics, layout.next,
				hot == HitTarget::Next, down == HitTarget::Next,
				page.nextEnabled);
			DrawChevron(graphics, layout.previous, false, page.previousEnabled);
			DrawChevron(graphics, layout.next, true, page.nextEnabled);
			DrawCenteredText(graphics, std::to_wstring(page.currentPage), layout.currentPage,
				22.0F * scale, true, 255);
			DrawCenteredText(graphics, L"/" + std::to_wstring(page.totalPage), layout.totalPage,
				12.0F * scale, false, 150);
			(void)service.SetBounds(ControlRoles[index], layout.bounds);
			if (!SubmitLayered(hwnd, surface, { layout.bounds.left, layout.bounds.top }, false))
				return FrameResult::Retry;
			(void)service.Show(ControlRoles[index]);
			return FrameResult::Idle;
		}

		FrameResult RenderFrame(Client client, const FrameContext&)
		{
			if (!initialized.load(std::memory_order_acquire)) return FrameResult::Idle;
			if (client == Client::WhiteboardFreeze) return RenderBackground();
			return RenderControl(client == Client::WhiteboardLeft);
		}

		LRESULT CALLBACK WhiteboardWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			const std::size_t index = ControlIndex(hwnd);
			auto PointerPoint = [&](POINT& point) noexcept
				{
					POINTER_INFO info{};
					if (!GetPointerInfo(GET_POINTERID_WPARAM(wParam), &info)) return false;
					if (info.pointerType == PT_MOUSE) return false;
					point = info.ptPixelLocation;
					return ScreenToClient(hwnd, &point) != FALSE;
				};
			if (message == WM_POINTERUPDATE)
			{
				POINT point{};
				if (!PointerPoint(point)) return DefWindowProcW(hwnd, message, wParam, lParam);
				hover[index].store(HitTest(hwnd, point), std::memory_order_release);
				RequestControls();
				return 0;
			}
			if (message == WM_POINTERDOWN)
			{
				POINT point{};
				if (!PointerPoint(point)) return DefWindowProcW(hwnd, message, wParam, lParam);
				const HitTarget target = HitTest(hwnd, point);
				if (TargetEnabled(target))
				{
					pressed[index].store(target, std::memory_order_release);
					RequestControls();
				}
				return 0;
			}
			if (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED)
			{
				const HitTarget down = pressed[index].exchange(
					HitTarget::None, std::memory_order_acq_rel);
				POINT point{};
				const bool validPoint = message == WM_POINTERUP && PointerPoint(point);
				if (validPoint && down != HitTarget::None && down == HitTest(hwnd, point))
					Invoke(down);
				RequestControls();
				return 0;
			}
			if (message == WM_MOUSEMOVE)
			{
				TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&tracking);
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				hover[index].store(HitTest(hwnd, point), std::memory_order_release);
				RequestControls();
				return 0;
			}
			if (message == WM_MOUSELEAVE)
			{
				hover[index].store(HitTarget::None, std::memory_order_release);
				RequestControls();
				return 0;
			}
			if (message == WM_LBUTTONDOWN)
			{
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const HitTarget target = HitTest(hwnd, point);
				if (TargetEnabled(target))
				{
					pressed[index].store(target, std::memory_order_release);
					SetCapture(hwnd);
					RequestControls();
				}
				return 0;
			}
			if (message == WM_LBUTTONUP)
			{
				const HitTarget down = pressed[index].exchange(
					HitTarget::None, std::memory_order_acq_rel);
				if (GetCapture() == hwnd) ReleaseCapture();
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (down != HitTarget::None && down == HitTest(hwnd, point)) Invoke(down);
				RequestControls();
				return 0;
			}
			if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
			{
				pressed[index].store(HitTarget::None, std::memory_order_release);
				RequestControls();
				return 0;
			}
			if (message == WM_ERASEBKGND) return 1;
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}

	bool Initialize(BusinessCallbacks callbacks)
	{
		if (initialized.exchange(true, std::memory_order_acq_rel)) return false;
		{
			std::scoped_lock lock(callbackMutex);
			business = std::move(callbacks);
		}
		for (const Client client : Clients)
		{
			if (!Inkeys::UI::RenderPipeline::Register(client,
				[client](const FrameContext& context) { return RenderFrame(client, context); }))
			{
				Shutdown();
				return false;
			}
		}
		displaySubscription = Inkeys::Display::Subscribe(
			[](Inkeys::Display::SnapshotPtr)
			{
				if (initialized.load(std::memory_order_acquire))
					Inkeys::UI::RenderPipeline::Request(
						Inkeys::UI::RenderPipeline::WhiteboardMask());
			});
		return true;
	}

	void Shutdown() noexcept
	{
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		displaySubscription.Reset();
		for (const Client client : Clients) Inkeys::UI::RenderPipeline::Unregister(client);
		for (const WindowRole role : ControlRoles)
			(void)Inkeys::Window::GetService().Hide(role);
		std::scoped_lock lock(callbackMutex);
		business = {};
	}

	WNDPROC WindowProc() noexcept { return WhiteboardWindowProc; }

	void PublishActive(bool value) noexcept
	{
		if (active.exchange(value, std::memory_order_acq_rel) == value) return;
		Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::WhiteboardMask());
	}

	void PublishPageState(int current, int total, bool changing) noexcept
	{
		const bool currentChanged = currentPage.exchange(
			current, std::memory_order_acq_rel) != current;
		const bool totalChanged = totalPage.exchange(
			total, std::memory_order_acq_rel) != total;
		const bool switchingChanged = switching.exchange(
			changing, std::memory_order_acq_rel) != changing;
		const bool changed = currentChanged || totalChanged || switchingChanged;
		if (changed) RequestControls();
	}

	bool Active() noexcept { return active.load(std::memory_order_acquire); }

	bool BackgroundMatchesActive(bool value) noexcept
	{
		return committedBackgroundActive.load(std::memory_order_acquire) == value;
	}
}
