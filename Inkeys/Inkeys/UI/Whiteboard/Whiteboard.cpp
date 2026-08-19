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
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <windows.h>
#include <windowsx.h>

#include "../../../IdtFreezeFrame.h"
#include "../../../IdtImage.h"

module Inkeys.UI.Whiteboard;

import Inkeys.UI.Bar;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
import Inkeys.Window;

namespace Inkeys::UI::Whiteboard
{
	namespace
	{
		using BarHitTarget = Inkeys::UI::Bar::WhiteboardControlHitTarget;
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

		std::atomic_bool initialized = false;
		std::atomic_bool active = false;
		std::atomic_bool committedBackgroundActive = false;
		std::atomic_int currentPage = 1;
		std::atomic_int totalPage = 1;
		std::atomic_bool switching = false;
		std::array<std::atomic<BarHitTarget>, 2> hover{};
		std::array<std::atomic<BarHitTarget>, 2> pressed{};
		std::array<std::atomic<LONG>, 2> pointerX{};
		std::array<std::atomic<LONG>, 2> pointerY{};
		std::array<std::atomic_bool, 2> pointerKnown{};
		std::array<std::atomic_bool, 2> resetVisuals{};

		struct ControlTarget
		{
			Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
			Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
			Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> gdi;
			Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> debugBrush;
			std::uint64_t generation = 0;
			UINT width = 0;
			UINT height = 0;

			void Reset() noexcept
			{
				if (context) context->SetTarget(nullptr);
				debugBrush.Reset();
				gdi.Reset();
				bitmap.Reset();
				context.Reset();
				generation = 0;
				width = 0;
				height = 0;
			}
		};

		struct ControlPresentationState
		{
			RECT committedBounds{};
			BarHitTarget committedHover = BarHitTarget::None;
			BarHitTarget committedPressed = BarHitTarget::None;
			int committedCurrentPage = -1;
			int committedTotalPage = -1;
			bool committedPreviousEnabled = false;
			bool committedPageEnabled = false;
			bool committedNextEnabled = false;
			bool committedDebug = false;
			bool finalDebugFramePending = false;
			bool shown = false;
			std::uint64_t committedGeneration = 0;

			void ResetCommitted() noexcept
			{
				committedBounds = {};
				committedHover = BarHitTarget::None;
				committedPressed = BarHitTarget::None;
				committedCurrentPage = -1;
				committedTotalPage = -1;
				committedPreviousEnabled = false;
				committedPageEnabled = false;
				committedNextEnabled = false;
				committedDebug = false;
				finalDebugFramePending = false;
				committedGeneration = 0;
			}
		};

		std::array<ControlTarget, 2> controlTargets{};
		std::array<ControlPresentationState, 2> controlPresentation{};
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
				const UINT dpi = monitor->effectiveDpiX ? monitor->effectiveDpiX
					: USER_DEFAULT_SCREEN_DPI;
				return static_cast<float>(dpi)
					/ static_cast<float>(USER_DEFAULT_SCREEN_DPI);
			}
			using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
			static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			const UINT dpi = getDpiForWindow && hwnd ? getDpiForWindow(hwnd) : 0;
			return static_cast<float>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI)
				/ static_cast<float>(USER_DEFAULT_SCREEN_DPI);
		}

		[[nodiscard]] std::size_t ControlIndex(HWND hwnd) noexcept
		{
			return Inkeys::Window::GetService().Handle(WindowRole::WhiteboardRight) == hwnd
				? 1u : 0u;
		}

		[[nodiscard]] BarHitTarget HitTest(HWND hwnd, POINT point) noexcept
		{
			const auto layout = ResolveControlLayout({}, DpiScale(hwnd), true);
			if (PtInRect(&layout.previous, point)) return BarHitTarget::Previous;
			if (PtInRect(&layout.currentPage, point)) return BarHitTarget::Page;
			if (PtInRect(&layout.next, point)) return BarHitTarget::Next;
			return BarHitTarget::None;
		}

		[[nodiscard]] bool TargetEnabled(BarHitTarget target) noexcept
		{
			const PageState page = ResolvePageState(
				currentPage.load(std::memory_order_acquire),
				totalPage.load(std::memory_order_acquire),
				switching.load(std::memory_order_acquire));
			return target == BarHitTarget::Previous ? page.previousEnabled
				: target == BarHitTarget::Page ? page.pageEnabled
				: target == BarHitTarget::Next ? page.nextEnabled : false;
		}

		void RequestControls() noexcept
		{
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Mask(Client::WhiteboardLeft)
				| Inkeys::UI::RenderPipeline::Mask(Client::WhiteboardRight));
		}

		void Invoke(BarHitTarget target)
		{
			std::function<void()> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = target == BarHitTarget::Previous
					? business.previousPage
					: target == BarHitTarget::Next ? business.nextPage : nullptr;
			}
			// Page 按钮保留完整交互视觉，当前点击语义明确为 no-op。
			if (callback && TargetEnabled(target)) callback();
		}

		[[nodiscard]] HRESULT EnsureControlTarget(ControlTarget& target,
			const FrameContext& context, UINT width, UINT height)
		{
			if (!context.epoch.d2dDevice || width == 0 || height == 0) return E_INVALIDARG;
			if (target.generation == context.epoch.generation && target.context
				&& target.bitmap && target.gdi && target.debugBrush && target.width == width
				&& target.height == height)
				return S_OK;
			Microsoft::WRL::ComPtr<ID2D1DeviceContext> nextContext;
			Microsoft::WRL::ComPtr<ID2D1Bitmap1> nextBitmap;
			Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> nextGdi;
			Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> nextDebugBrush;
			HRESULT hr = context.epoch.d2dDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &nextContext);
			if (FAILED(hr)) return hr;
			const auto properties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED));
			hr = nextContext->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
				&properties, &nextBitmap);
			if (FAILED(hr)) return hr;
			hr = nextContext.As(&nextGdi);
			if (FAILED(hr)) return hr;
			nextContext->SetTarget(nextBitmap.Get());
			hr = nextContext->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Blue), &nextDebugBrush);
			if (FAILED(hr)) return hr;
			nextContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			target.Reset();
			target.context = std::move(nextContext);
			target.bitmap = std::move(nextBitmap);
			target.gdi = std::move(nextGdi);
			target.debugBrush = std::move(nextDebugBrush);
			target.generation = context.epoch.generation;
			target.width = width;
			target.height = height;
			return S_OK;
		}

		[[nodiscard]] bool IsDeviceLost(HRESULT hr) noexcept
		{
			return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET
				|| hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		[[nodiscard]] bool PresentControl(HWND hwnd, ControlTarget& target,
			POINT destination, const RECT& dirty) noexcept
		{
			if (!hwnd || !target.gdi) return false;
			HDC source = nullptr;
			if (FAILED(target.gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &source)) || !source)
				return false;
			POINT sourcePoint{};
			SIZE size{ static_cast<LONG>(target.width), static_cast<LONG>(target.height) };
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			UPDATELAYEREDWINDOWINFO info{};
			info.cbSize = sizeof(info);
			info.pptDst = &destination;
			info.psize = &size;
			info.pptSrc = &sourcePoint;
			info.hdcSrc = source;
			info.pblend = &blend;
			info.prcDirty = &dirty;
			info.dwFlags = ULW_ALPHA;
			const BOOL presented = UpdateLayeredWindowIndirect(hwnd, &info);
			const HRESULT releaseHr = target.gdi->ReleaseDC(nullptr);
			return presented != FALSE && SUCCEEDED(releaseHr);
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
			if (show) surface.clear(0xff123b32u);
			else surface.clear();
			HDC screen = GetDC(nullptr);
			if (!screen) return FrameResult::Retry;
			POINT destination{ show ? bounds.left : 0, show ? bounds.top : 0 };
			POINT sourcePoint{};
			SIZE size{ surface.width(), surface.height() };
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			UPDATELAYEREDWINDOWINFO info{};
			info.cbSize = sizeof(info);
			info.hdcDst = screen;
			info.pptDst = &destination;
			info.psize = &size;
			info.pptSrc = &sourcePoint;
			info.hdcSrc = surface.dc();
			info.pblend = &blend;
			info.dwFlags = ULW_ALPHA;
			const bool presented = SubmitFreezeSurface(hwnd, &info, true);
			ReleaseDC(nullptr, screen);
			if (!presented) return FrameResult::Retry;
			if (show) (void)Inkeys::Window::GetService().SetBounds(WindowRole::Freeze, bounds);
			if (show) (void)Inkeys::Window::GetService().Show(WindowRole::Freeze);
			else (void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
			committedBackgroundActive.store(show, std::memory_order_release);
			return FrameResult::Idle;
		}

		FrameResult RenderControl(bool left, const FrameContext& frameContext)
		{
			const std::size_t index = left ? 0u : 1u;
			auto& service = Inkeys::Window::GetService();
			const HWND hwnd = service.Handle(ControlRoles[index]);
			if (!hwnd) return FrameResult::Retry;
			auto& presentation = controlPresentation[index];
			if (!active.load(std::memory_order_acquire))
			{
				(void)service.Hide(ControlRoles[index]);
				presentation.shown = false;
				presentation.ResetCommitted();
				return FrameResult::Idle;
			}
			const float scale = DpiScale(hwnd);
			const auto layout = ResolveControlLayout(PrimaryBounds(), scale, left);
			const UINT width = static_cast<UINT>(layout.bounds.right - layout.bounds.left);
			const UINT height = static_cast<UINT>(layout.bounds.bottom - layout.bounds.top);
			ControlTarget& target = controlTargets[index];
			const HRESULT resourceHr = EnsureControlTarget(target, frameContext, width, height);
			if (FAILED(resourceHr))
				return IsDeviceLost(resourceHr) ? FrameResult::DeviceLost : FrameResult::Retry;

			const PageState page = ResolvePageState(
				currentPage.load(std::memory_order_acquire),
				totalPage.load(std::memory_order_acquire),
				switching.load(std::memory_order_acquire));
			const BarHitTarget hoverNow = hover[index].load(std::memory_order_acquire);
			const BarHitTarget pressedNow = pressed[index].load(std::memory_order_acquire);
			const bool resetNow = resetVisuals[index].exchange(
				false, std::memory_order_acq_rel);
			Inkeys::UI::Bar::WhiteboardControlRenderState renderState{
				page.currentPage, page.totalPage, page.previousEnabled,
				page.pageEnabled, page.nextEnabled,
				hoverNow, pressedNow,
				POINT{ pointerX[index].load(std::memory_order_acquire),
					pointerY[index].load(std::memory_order_acquire) },
				pointerKnown[index].load(std::memory_order_acquire), resetNow };
			auto* context = target.context.Get();
			context->BeginDraw();
			context->SetTransform(D2D1::Matrix3x2F::Identity());
			context->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
			const auto drawResult = Inkeys::UI::Bar::RenderWhiteboardControl(
				context, renderState, scale, left,
				POINT{ layout.bounds.left, layout.bounds.top },
				frameContext.frameTime);
			const bool debug = Inkeys::UI::Bar::DebugModeEnabled();
			const bool businessChanged = resetNow
				|| presentation.committedGeneration != frameContext.epoch.generation
				|| !EqualRect(&presentation.committedBounds, &layout.bounds)
				|| presentation.committedHover != hoverNow
				|| presentation.committedPressed != pressedNow
				|| presentation.committedCurrentPage != page.currentPage
				|| presentation.committedTotalPage != page.totalPage
				|| presentation.committedPreviousEnabled != page.previousEnabled
				|| presentation.committedPageEnabled != page.pageEnabled
				|| presentation.committedNextEnabled != page.nextEnabled
				|| presentation.committedDebug != debug;
			const bool renderingActive = drawResult.animationActive || businessChanged;
			const bool drawFinalDebug = debug && !renderingActive
				&& presentation.finalDebugFramePending;
			if (debug)
			{
				// 蓝框标识真实 HWND，红/绿框分别标识活动 damage 与最终休眠帧。
				target.debugBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Blue));
				context->DrawRectangle(D2D1::RectF(0.5F, 0.5F,
					static_cast<FLOAT>(width) - 0.5F,
					static_cast<FLOAT>(height) - 0.5F), target.debugBrush.Get(), 1.0F);
				if (renderingActive || drawFinalDebug)
				{
					target.debugBrush->SetColor(D2D1::ColorF(renderingActive
						? D2D1::ColorF::Red : D2D1::ColorF::LimeGreen));
					context->DrawRectangle(D2D1::RectF(2.0F, 2.0F,
						static_cast<FLOAT>(width) - 2.0F,
						static_cast<FLOAT>(height) - 2.0F), target.debugBrush.Get(), 2.0F);
				}
			}
			const RECT dirty{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			const bool presented = PresentControl(hwnd, target,
				{ layout.bounds.left, layout.bounds.top }, dirty);
			const HRESULT endDrawHr = context->EndDraw();
			::barUISet.spec.HandleFrameEndDrawResult(endDrawHr);
			if (FAILED(endDrawHr) || !presented)
			{
				presentation.committedGeneration = 0;
				if (IsDeviceLost(endDrawHr)) target.Reset();
				return IsDeviceLost(endDrawHr) ? FrameResult::DeviceLost : FrameResult::Retry;
			}
			(void)service.SetBounds(ControlRoles[index], layout.bounds);
			(void)service.Show(ControlRoles[index]);
			presentation.shown = true;
			presentation.committedBounds = layout.bounds;
			presentation.committedHover = hoverNow;
			presentation.committedPressed = pressedNow;
			presentation.committedCurrentPage = page.currentPage;
			presentation.committedTotalPage = page.totalPage;
			presentation.committedPreviousEnabled = page.previousEnabled;
			presentation.committedPageEnabled = page.pageEnabled;
			presentation.committedNextEnabled = page.nextEnabled;
			presentation.committedDebug = debug;
			presentation.committedGeneration = frameContext.epoch.generation;
			if (!debug) presentation.finalDebugFramePending = false;
			else if (renderingActive) presentation.finalDebugFramePending = true;
			else if (drawFinalDebug) presentation.finalDebugFramePending = false;
			return drawResult.animationActive || presentation.finalDebugFramePending
				? FrameResult::Continue : FrameResult::Idle;
		}

		FrameResult RenderFrame(Client client, const FrameContext& context)
		{
			if (!initialized.load(std::memory_order_acquire)) return FrameResult::Idle;
			if (client == Client::WhiteboardFreeze) return RenderBackground();
			return RenderControl(client == Client::WhiteboardLeft, context);
		}

		LRESULT CALLBACK WhiteboardWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			const std::size_t index = ControlIndex(hwnd);
			if (message == WM_MOUSEMOVE)
			{
				TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&tracking);
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				pointerX[index].store(point.x, std::memory_order_release);
				pointerY[index].store(point.y, std::memory_order_release);
				pointerKnown[index].store(true, std::memory_order_release);
				hover[index].store(HitTest(hwnd, point), std::memory_order_release);
				Inkeys::UI::Bar::NotifyWhiteboardControlPointerActivity(true);
				RequestControls();
				return 0;
			}
			if (message == WM_MOUSELEAVE)
			{
				hover[index].store(BarHitTarget::None, std::memory_order_release);
				Inkeys::UI::Bar::NotifyWhiteboardControlPointerActivity(false);
				RequestControls();
				return 0;
			}
			if (message == WM_LBUTTONDOWN)
			{
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const BarHitTarget target = HitTest(hwnd, point);
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
				const BarHitTarget down = pressed[index].exchange(
					BarHitTarget::None, std::memory_order_acq_rel);
				if (GetCapture() == hwnd) ReleaseCapture();
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (down != BarHitTarget::None && down == HitTest(hwnd, point)) Invoke(down);
				RequestControls();
				return 0;
			}
			if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
			{
				pressed[index].store(BarHitTarget::None, std::memory_order_release);
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
		for (const Client client : Clients)
			Inkeys::UI::RenderPipeline::Unregister(client);
		for (const WindowRole role : ControlRoles)
			(void)Inkeys::Window::GetService().Hide(role);
		for (auto& target : controlTargets) target.Reset();
		for (auto& state : controlPresentation)
		{
			state.ResetCommitted();
			state.shown = false;
		}
		for (auto& value : hover) value.store(BarHitTarget::None, std::memory_order_release);
		for (auto& value : pressed) value.store(BarHitTarget::None, std::memory_order_release);
		for (auto& value : resetVisuals)
			value.store(true, std::memory_order_release);
		std::scoped_lock lock(callbackMutex);
		business = {};
	}

	WNDPROC WindowProc() noexcept { return WhiteboardWindowProc; }

	void PublishActive(bool value) noexcept
	{
		if (active.exchange(value, std::memory_order_acq_rel) == value) return;
		if (!value)
		{
			for (std::size_t index = 0; index < hover.size(); ++index)
			{
				hover[index].store(BarHitTarget::None, std::memory_order_release);
				pressed[index].store(BarHitTarget::None, std::memory_order_release);
				pointerKnown[index].store(false, std::memory_order_release);
			}
			for (auto& value : resetVisuals)
				value.store(true, std::memory_order_release);
		}
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
		if (currentChanged || totalChanged || switchingChanged) RequestControls();
	}

	bool Active() noexcept { return active.load(std::memory_order_acquire); }

	bool BackgroundMatchesActive(bool value) noexcept
	{
		return committedBackgroundActive.load(std::memory_order_acquire) == value;
	}
}
