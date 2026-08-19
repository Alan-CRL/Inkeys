module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <d2d1_1.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <windows.h>
#include <windowsx.h>

#include "../../../IdtFreezeFrame.h"

module Inkeys.UI.Whiteboard;

import Inkeys.UI.Bar;
import Inkeys.UI.Bar.Metrics;
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
		using Scene = Inkeys::UI::Bar::BarSurfaceScene;
		using WidgetSpec = Inkeys::UI::Bar::BarSurfaceWidgetSpec;
		using WidgetId = Inkeys::UI::Bar::BarSurfaceWidgetId;

		constexpr std::array<Client, 3> Clients{
			Client::WhiteboardFreeze,
			Client::WhiteboardLeft,
			Client::WhiteboardRight,
		};
		constexpr std::array<WindowRole, 2> ControlRoles{
			WindowRole::WhiteboardLeft,
			WindowRole::WhiteboardRight,
		};
		constexpr std::array<WidgetId, 3> WidgetIds{ 1, 2, 3 };

		std::atomic_bool initialized = false;
		std::atomic_bool active = false;
		std::atomic_bool committedBackgroundActive = false;
		std::atomic_int currentPage = 1;
		std::atomic_int totalPage = 1;
		std::atomic_bool switching = false;
		std::array<Scene, 2> controlScenes{};
		Scene backgroundScene;
		std::array<RECT, 2> configuredControlScreens{};
		std::array<float, 2> configuredControlScales{ 0.0F, 0.0F };
		RECT configuredBackgroundScreen{};
		float configuredBackgroundScale = 0.0F;
		Inkeys::Display::Subscription displaySubscription;
		std::mutex callbackMutex;
		BusinessCallbacks business;

		[[nodiscard]] RECT PrimaryBounds() noexcept
		{
			const auto snapshot = Inkeys::Display::GetSnapshot();
			if (const auto* monitor = snapshot ? snapshot->Primary() : nullptr)
				return monitor->bounds;
			return { 0, 0, GetSystemMetrics(SM_CXSCREEN),
				GetSystemMetrics(SM_CYSCREEN) };
		}

		[[nodiscard]] float DpiScale(HWND hwnd) noexcept
		{
			const auto snapshot = Inkeys::Display::GetSnapshot();
			HMONITOR monitorHandle = hwnd
				? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
			const auto* monitor = snapshot
				? (monitorHandle ? snapshot->Find(monitorHandle) : snapshot->Primary())
				: nullptr;
			if (monitor)
			{
				const UINT dpi = monitor->effectiveDpiX
					? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI;
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

		[[nodiscard]] std::size_t SurfaceIndex(HWND hwnd) noexcept
		{
			return Inkeys::Window::GetService().Handle(WindowRole::WhiteboardRight) == hwnd
				? 1u : 0u;
		}

		void RequestSurface(std::size_t index) noexcept
		{
			Inkeys::UI::RenderPipeline::Request(Inkeys::UI::RenderPipeline::Mask(
				index == 0 ? Client::WhiteboardLeft : Client::WhiteboardRight));
		}

		void RequestControls() noexcept
		{
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Mask(Client::WhiteboardLeft)
				| Inkeys::UI::RenderPipeline::Mask(Client::WhiteboardRight));
		}

		void InvokePageCallback(std::size_t index)
		{
			std::function<void()> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = index == 0 ? business.previousPage : business.nextPage;
			}
			if (callback) callback();
		}

		[[nodiscard]] std::vector<WidgetSpec> BuildWidgets()
		{
			std::vector<WidgetSpec> widgets;
			widgets.reserve(3);
			const COLORREF darkPressedFill = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::PressedFill);
			const COLORREF darkText = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::TextPrimary);
			auto ApplyDarkBarColors = [&](WidgetSpec& widget)
			{
				// 独立白板 surface 显式复用 Bar 深色主题，避免未绑定全局样式时回退到浅色。
				widget.useThemeColors = false;
				widget.fill = darkPressedFill;
				widget.content = darkText;
			};
			WidgetSpec previous;
			previous.id = WidgetIds[0];
			previous.enabled = false;
			previous.iconResource = L"barMore";
			previous.secondaryText = L"左翻页";
			previous.iconAngle = -90.0;
			previous.onClick = [] { InvokePageCallback(0); };
			ApplyDarkBarColors(previous);
			widgets.push_back(std::move(previous));

			WidgetSpec page;
			page.id = WidgetIds[1];
			page.primaryText = L"1";
			page.secondaryText = L"/1";
			page.primaryFontSizeDip = 28.0;
			page.primaryOffsetYDip = -10.0;
			page.primarySlotHeightDip = 28.0;
			page.secondaryFontSizeDip = 13.0;
			page.secondaryOffsetYDip = 20.0;
			page.secondarySlotHeightDip = 25.0;
			ApplyDarkBarColors(page);
			widgets.push_back(std::move(page));

			WidgetSpec next;
			next.id = WidgetIds[2];
			next.enabled = false;
			next.iconResource = L"barMore";
			next.secondaryText = L"右翻页";
			next.iconAngle = 90.0;
			next.onClick = [] { InvokePageCallback(1); };
			ApplyDarkBarColors(next);
			widgets.push_back(std::move(next));
			return widgets;
		}

		void ConfigureControlScene(std::size_t index)
		{
			const auto bounds = PrimaryBounds();
			const float scale = DpiScale(nullptr);
			Inkeys::UI::Bar::BarSurfaceBackgroundSpec background;
			// 独立 surface 不能依赖全局 BarStyle 指针，直接使用主栏深色颜色。
			background.useThemeColors = false;
			background.fill = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::Surface);
			background.frame = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::SurfaceFrame);
			background.cornerRadiusDip = BarMainBarCornerRadiusDip;
			background.frameThicknessDip = BarButtonFrameThicknessDip;
			background.fillOpacity = BarMainBarFillOpacity;
			background.frameOpacity = BarMainBarFrameOpacity;
			background.visible = true;
			const auto widgets = BuildWidgets();
			Inkeys::UI::Bar::BarSurfaceHorizontalGroupSpec group;
			group.screenBounds = bounds;
			group.dpiScale = scale;
			group.anchor = index == 0
				? Inkeys::UI::Bar::BarSurfaceHorizontalAnchor::Left
				: Inkeys::UI::Bar::BarSurfaceHorizontalAnchor::Right;
			(void)controlScenes[index].ConfigureHorizontalGroup(background, widgets, group);
			controlScenes[index].SetSharedPrimaryLightSubscribed(true);
			controlScenes[index].SetHooks({
				{}, [index] { RequestSurface(index); } });
			configuredControlScreens[index] = bounds;
			configuredControlScales[index] = scale;
			const PageState page = ResolvePageState(
				currentPage.load(std::memory_order_acquire),
				totalPage.load(std::memory_order_acquire),
				switching.load(std::memory_order_acquire));
			(void)controlScenes[index].SetWidgetState(WidgetIds[0], true,
				page.previousEnabled, {}, L"左翻页");
			(void)controlScenes[index].SetWidgetState(WidgetIds[1], true,
				page.pageEnabled, std::to_wstring(page.currentPage),
				L"/" + std::to_wstring(page.totalPage));
			(void)controlScenes[index].SetWidgetState(WidgetIds[2], true,
				page.nextEnabled, {}, L"右翻页");
		}

		void ConfigureBackgroundScene(const RECT& bounds, float scale)
		{
			const double widthDip = static_cast<double>(bounds.right - bounds.left) / scale;
			const double heightDip = static_cast<double>(bounds.bottom - bounds.top) / scale;
			Inkeys::UI::Bar::BarSurfaceBackgroundSpec background;
			background.bounds = { 0.0, 0.0, widthDip, heightDip };
			background.fill = RGB(18, 59, 50);
			background.frame = RGB(18, 59, 50);
			background.useThemeColors = false;
			background.cornerRadiusDip = 0.0;
			background.frameThicknessDip = 0.0;
			background.fillOpacity = 1.0;
			background.frameOpacity = 0.0;
			background.visible = true;
			(void)backgroundScene.Configure(background,
				std::span<const WidgetSpec>{});
			(void)backgroundScene.SetBounds(bounds, scale);
			configuredBackgroundScreen = bounds;
			configuredBackgroundScale = scale;
		}

		[[nodiscard]] bool IsDeviceLost(HRESULT hr) noexcept
		{
			return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET
				|| hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		enum class PresentStatus : std::uint8_t
		{
			Success,
			Retry,
			DeviceLost,
		};

		[[nodiscard]] PresentStatus PresentScene(Scene& scene, HWND hwnd,
			const FrameContext& frameContext, const RECT& presentationBounds,
			bool freezeOwner) noexcept
		{
			const UINT width = static_cast<UINT>(presentationBounds.right
				- presentationBounds.left);
			const UINT height = static_cast<UINT>(presentationBounds.bottom
				- presentationBounds.top);
			if (!hwnd || width == 0 || height == 0)
				return PresentStatus::Retry;
			const HRESULT resourceHr = scene.EnsureDeviceResources(
				frameContext.epoch, width, height);
			if (FAILED(resourceHr))
			{
				return IsDeviceLost(resourceHr)
					? PresentStatus::DeviceLost : PresentStatus::Retry;
			}
			auto* context = scene.DeviceContext();
			auto* gdi = scene.GdiInteropRenderTarget();
			if (!context || !gdi) return PresentStatus::Retry;
			context->BeginDraw();
			context->SetTransform(D2D1::Matrix3x2F::Identity());
			context->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
			const auto drawResult = scene.Render(context, frameContext.frameTime);
			HDC source = nullptr;
			bool presented = false;
			if (SUCCEEDED(gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &source)) && source)
			{
				POINT destination{ presentationBounds.left, presentationBounds.top };
				POINT sourcePoint{};
				SIZE size{ static_cast<LONG>(width), static_cast<LONG>(height) };
				BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
				UPDATELAYEREDWINDOWINFO info{};
				info.cbSize = sizeof(info);
				info.pptDst = &destination;
				info.psize = &size;
				info.pptSrc = &sourcePoint;
				info.hdcSrc = source;
				HDC destinationDc = nullptr;
				if (freezeOwner) destinationDc = GetDC(nullptr);
				info.hdcDst = destinationDc;
				info.pblend = &blend;
				RECT dirty = scene.PendingDamage();
				if (dirty.right <= dirty.left || dirty.bottom <= dirty.top)
					dirty = RECT{ 0, 0, static_cast<LONG>(width),
						static_cast<LONG>(height) };
				info.prcDirty = &dirty;
				info.dwFlags = ULW_ALPHA;
				presented = freezeOwner
					? SubmitFreezeSurface(hwnd, &info, true)
					: UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
				if (destinationDc) ReleaseDC(nullptr, destinationDc);
				(void)gdi->ReleaseDC(nullptr);
			}
			const HRESULT endDrawHr = context->EndDraw();
			scene.HandleFrameEndDrawResult(endDrawHr);
		(void)drawResult;
		if (IsDeviceLost(endDrawHr))
		{
			scene.ReleaseDeviceResources();
			return PresentStatus::DeviceLost;
		}
		if (FAILED(endDrawHr))
		{
			scene.ReleaseDeviceResources();
			return PresentStatus::Retry;
		}
		return presented ? PresentStatus::Success : PresentStatus::Retry;
		}

		FrameResult RenderBackground(const FrameContext& frameContext)
		{
			const HWND hwnd = Inkeys::Window::GetService().Handle(WindowRole::Freeze);
			if (!hwnd) return FrameResult::Retry;
			const bool show = active.load(std::memory_order_acquire);
			if (!show)
			{
				(void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
				committedBackgroundActive.store(false, std::memory_order_release);
				return FrameResult::Idle;
			}
			const RECT bounds = PrimaryBounds();
			const float scale = DpiScale(hwnd);
			if (!EqualRect(&configuredBackgroundScreen, &bounds)
				|| configuredBackgroundScale != scale)
				ConfigureBackgroundScene(bounds, scale);
			const RECT presentation = backgroundScene.PresentationBounds();
			const auto presentStatus = PresentScene(backgroundScene, hwnd,
				frameContext, presentation, true);
			if (presentStatus != PresentStatus::Success)
				return presentStatus == PresentStatus::DeviceLost
					? FrameResult::DeviceLost : FrameResult::Retry;
			(void)Inkeys::Window::GetService().SetBounds(WindowRole::Freeze, presentation);
			(void)Inkeys::Window::GetService().Show(WindowRole::Freeze);
			(void)backgroundScene.ConsumeDamage();
			committedBackgroundActive.store(true, std::memory_order_release);
			return backgroundScene.AnimationActive() ? FrameResult::Continue
				: FrameResult::Idle;
		}

		FrameResult RenderControl(std::size_t index, const FrameContext& frameContext)
		{
			auto& service = Inkeys::Window::GetService();
			const HWND hwnd = service.Handle(ControlRoles[index]);
			if (!hwnd) return FrameResult::Retry;
			if (!active.load(std::memory_order_acquire))
			{
				(void)service.Hide(ControlRoles[index]);
				controlScenes[index].Reset();
				return FrameResult::Idle;
			}
			const RECT screen = PrimaryBounds();
			const float scale = DpiScale(hwnd);
			if (!EqualRect(&configuredControlScreens[index], &screen)
				|| configuredControlScales[index] != scale)
				ConfigureControlScene(index);
			const RECT presentation = controlScenes[index].PresentationBounds();
			const auto presentStatus = PresentScene(controlScenes[index], hwnd,
				frameContext, presentation, false);
			if (presentStatus != PresentStatus::Success)
				return presentStatus == PresentStatus::DeviceLost
					? FrameResult::DeviceLost : FrameResult::Retry;
			(void)service.SetBounds(ControlRoles[index], presentation);
			(void)service.Show(ControlRoles[index]);
			(void)controlScenes[index].ConsumeDamage();
			return controlScenes[index].AnimationActive() ? FrameResult::Continue
				: FrameResult::Idle;
		}

		FrameResult RenderFrame(Client client, const FrameContext& context)
		{
			if (!initialized.load(std::memory_order_acquire)) return FrameResult::Idle;
			if (client == Client::WhiteboardFreeze) return RenderBackground(context);
			return RenderControl(client == Client::WhiteboardRight ? 1u : 0u, context);
		}

		LRESULT CALLBACK WhiteboardWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			const std::size_t index = SurfaceIndex(hwnd);
			Scene& scene = controlScenes[index];
			if (message == WM_MOUSEMOVE)
			{
				TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&tracking);
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (const auto logical = scene.PresentationToLogical(point))
					scene.PointerMove(*logical);
				else
					scene.PointerLeave();
				return 0;
			}
			if (message == WM_MOUSELEAVE)
			{
				scene.PointerLeave();
				return 0;
			}
			if (message == WM_LBUTTONDOWN)
			{
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (const auto logical = scene.PresentationToLogical(point))
				{
					const auto result = scene.PointerDown(*logical);
					if (result.consumed) SetCapture(hwnd);
				}
				return 0;
			}
			if (message == WM_LBUTTONUP)
			{
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (const auto logical = scene.PresentationToLogical(point))
					scene.PointerUp(*logical);
				else
					scene.CancelPointer();
				if (GetCapture() == hwnd) ReleaseCapture();
				return 0;
			}
			if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
			{
				scene.CancelPointer();
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
		ConfigureControlScene(0);
		ConfigureControlScene(1);
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
				if (!initialized.load(std::memory_order_acquire)) return;
				RequestControls();
				Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
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
		(void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
		for (auto& scene : controlScenes)
		{
			scene.Reset();
			scene.ReleaseDeviceResources();
		}
		backgroundScene.Reset();
		backgroundScene.ReleaseDeviceResources();
		committedBackgroundActive.store(false, std::memory_order_release);
		std::scoped_lock lock(callbackMutex);
		business = {};
	}

	WNDPROC WindowProc() noexcept { return WhiteboardWindowProc; }

	void PublishActive(bool value) noexcept
	{
		if (active.exchange(value, std::memory_order_acq_rel) == value) return;
		if (!value)
			for (auto& scene : controlScenes) scene.Reset();
		RequestControls();
		Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
	}

	void PublishPageState(int current, int total, bool changing) noexcept
	{
		const bool currentChanged = currentPage.exchange(
			current, std::memory_order_acq_rel) != current;
		const bool totalChanged = totalPage.exchange(
			total, std::memory_order_acq_rel) != total;
		const bool switchingChanged = switching.exchange(
			changing, std::memory_order_acq_rel) != changing;
		if (!(currentChanged || totalChanged || switchingChanged)) return;
		const PageState state = ResolvePageState(current, total, changing);
		for (auto& scene : controlScenes)
		{
			(void)scene.SetWidgetState(WidgetIds[0], true,
				state.previousEnabled, {}, L"左翻页");
			(void)scene.SetWidgetState(WidgetIds[1], true,
				state.pageEnabled, std::to_wstring(state.currentPage),
				L"/" + std::to_wstring(state.totalPage));
			(void)scene.SetWidgetState(WidgetIds[2], true,
				state.nextEnabled, {}, L"右翻页");
		}
		RequestControls();
	}

	bool Active() noexcept { return active.load(std::memory_order_acquire); }

	bool BackgroundMatchesActive(bool value) noexcept
	{
		return committedBackgroundActive.load(std::memory_order_acquire) == value;
	}
}
