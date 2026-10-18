module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

#include "../../../IdtFreezeFrame.h"

module Inkeys.UI.Whiteboard;

import Inkeys.UI.Bar;
import Inkeys.UI.PageControl;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
import Inkeys.Window;

namespace Inkeys::UI::Whiteboard
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		using Client = Inkeys::UI::RenderPipeline::Client;
		using FrameContext = Inkeys::UI::RenderPipeline::FrameContext;
		using FrameResult = Inkeys::UI::RenderPipeline::FrameResult;
		using Scene = Inkeys::UI::Bar::BarSurfaceScene;
		using WindowRole = Inkeys::Window::WindowRole;

		std::atomic_bool initialized = false;
		std::atomic_bool expandedLayoutTarget = false;
		std::atomic_bool active = false;
		std::atomic_bool committedBackgroundActive = false;
		std::mutex pageStateMutex;
		PageStateTransaction pageTransaction;
		std::mutex backgroundRenderMutex;
		Scene backgroundScene;
		RECT configuredBackgroundScreen{};
		float configuredBackgroundScale = 0.0F;
		Inkeys::Display::Subscription displaySubscription;

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
			const HMONITOR handle = hwnd
				? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
			const auto* monitor = snapshot
				? (handle ? snapshot->Find(handle) : snapshot->Primary()) : nullptr;
			if (monitor)
			{
				const UINT dpi = monitor->effectiveDpiX
					? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI;
				return static_cast<float>(dpi)
					/ static_cast<float>(USER_DEFAULT_SCREEN_DPI);
			}
			return 1.0F;
		}

		[[nodiscard]] PageState PublishedPageState() noexcept
		{
			std::scoped_lock lock(pageStateMutex);
			return pageTransaction.Snapshot();
		}

		void PublishPageControlSnapshot() noexcept
		{
			const PageState page = PublishedPageState();
			Inkeys::UI::PageControl::WhiteboardState state;
			state.expandedLayoutTarget = expandedLayoutTarget.load(
				std::memory_order_acquire);
			state.active = active.load(std::memory_order_acquire);
			state.currentPage = page.currentPage;
			state.totalPage = page.totalPage;
			state.previousEnabled = page.previousEnabled;
			state.previousInteractive = page.previousInteractive;
			state.pageEnabled = page.pageEnabled;
			state.pageInteractive = page.pageInteractive;
			state.nextEnabled = page.nextEnabled;
			state.nextInteractive = page.nextInteractive;
			state.nextIsAdd = page.nextIsAdd;
			state.switching = page.switching;
			Inkeys::UI::PageControl::PublishWhiteboardState(state);
		}

		void ConfigureBackgroundScene(const RECT& bounds, float scale)
		{
			const double widthDip = static_cast<double>(
				bounds.right - bounds.left) / scale;
			const double heightDip = static_cast<double>(
				bounds.bottom - bounds.top) / scale;
			Inkeys::UI::Bar::BarSurfaceBackgroundSpec background;
			background.bounds = { 0.0, 0.0, widthDip, heightDip };
			background.fill = RGB(18, 59, 50);
			background.frame = RGB(18, 59, 50);
			background.useThemeColors = false;
			background.cornerRadiusDip = 0.0;
			background.frameThicknessDip = 0.0;
			background.fillOpacity = 1.0;
			background.frameOpacity = 0.0;
			(void)backgroundScene.Configure(background,
				std::span<const Inkeys::UI::Bar::BarSurfaceWidgetSpec>{});
			(void)backgroundScene.SetBounds(bounds, scale);
			configuredBackgroundScreen = bounds;
			configuredBackgroundScale = scale;
		}

		[[nodiscard]] bool IsDeviceLost(HRESULT hr) noexcept
		{
			return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET
				|| hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		FrameResult RenderBackground(const FrameContext& frameContext)
		{
			if (!initialized.load(std::memory_order_acquire))
				return FrameResult::Idle;
			auto& service = Inkeys::Window::GetService();
			const HWND hwnd = service.Handle(WindowRole::Freeze);
			if (!hwnd) return FrameResult::Retry;
			if (!active.load(std::memory_order_acquire))
			{
				(void)service.Hide(WindowRole::Freeze);
				committedBackgroundActive.store(false, std::memory_order_release);
				return FrameResult::Idle;
			}

			const RECT bounds = PrimaryBounds();
			const float scale = DpiScale(hwnd);
			RECT presentation{};
			bool animationActive = false;
			{
				std::unique_lock renderLock(backgroundRenderMutex);
				if (!EqualRect(&configuredBackgroundScreen, &bounds)
					|| configuredBackgroundScale != scale)
					ConfigureBackgroundScene(bounds, scale);
				presentation = backgroundScene.PresentationBounds();
				const UINT width = static_cast<UINT>(presentation.right
					- presentation.left);
				const UINT height = static_cast<UINT>(presentation.bottom
					- presentation.top);
				const HRESULT resourceHr = backgroundScene.EnsureDeviceResources(
					frameContext.epoch, width, height);
				if (FAILED(resourceHr)) return IsDeviceLost(resourceHr)
					? FrameResult::DeviceLost : FrameResult::Retry;
				auto* rawContext = backgroundScene.DeviceContext();
				auto* rawGdi = backgroundScene.GdiInteropRenderTarget();
				if (!rawContext || !rawGdi) return FrameResult::Retry;
				ComPtr<ID2D1DeviceContext> context(rawContext);
				ComPtr<ID2D1GdiInteropRenderTarget> gdi(rawGdi);
				context->BeginDraw();
				context->SetTransform(D2D1::Matrix3x2F::Identity());
				context->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
				(void)backgroundScene.Render(context.Get(), frameContext.frameTime);
				HDC source = nullptr;
				HRESULT getDcHr = gdi->GetDC(
					D2D1_DC_INITIALIZE_MODE_COPY, &source);
				HRESULT releaseDcHr = S_OK;
				bool presented = false;
				if (SUCCEEDED(getDcHr) && source)
				{
					POINT destination{ presentation.left, presentation.top };
					POINT sourcePoint{};
					SIZE size{ static_cast<LONG>(width), static_cast<LONG>(height) };
					BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
					UPDATELAYEREDWINDOWINFO info{};
					info.cbSize = sizeof(info);
					info.pptDst = &destination;
					info.psize = &size;
					info.pptSrc = &sourcePoint;
					info.hdcSrc = source;
					info.pblend = &blend;
					RECT dirty = backgroundScene.PendingDamage();
					if (dirty.right <= dirty.left || dirty.bottom <= dirty.top)
						dirty = RECT{ 0, 0, static_cast<LONG>(width),
							static_cast<LONG>(height) };
					info.prcDirty = &dirty;
					info.dwFlags = ULW_ALPHA;
					HDC destinationDc = GetDC(nullptr);
					info.hdcDst = destinationDc;
					presented = SubmitFreezeSurface(hwnd, &info, true);
					if (destinationDc) ReleaseDC(nullptr, destinationDc);
					releaseDcHr = gdi->ReleaseDC(nullptr);
				}
				else if (SUCCEEDED(getDcHr)) getDcHr = E_FAIL;
				const HRESULT endDrawHr = context->EndDraw();
				backgroundScene.HandleFrameEndDrawResult(endDrawHr);
				if (IsDeviceLost(getDcHr) || IsDeviceLost(releaseDcHr)
					|| IsDeviceLost(endDrawHr))
				{
					backgroundScene.ReleaseDeviceResources();
					return FrameResult::DeviceLost;
				}
				if (FAILED(getDcHr) || FAILED(releaseDcHr)
					|| FAILED(endDrawHr) || !presented)
				{
					backgroundScene.ReleaseDeviceResources();
					return FrameResult::Retry;
				}
				animationActive = backgroundScene.AnimationActive();
				(void)backgroundScene.ConsumeDamage();
			}
			if (!active.load(std::memory_order_acquire))
			{
				(void)service.Hide(WindowRole::Freeze);
				committedBackgroundActive.store(false, std::memory_order_release);
				return FrameResult::Idle;
			}
			(void)service.SetBounds(WindowRole::Freeze, presentation);
			(void)service.Show(WindowRole::Freeze);
			committedBackgroundActive.store(true, std::memory_order_release);
			return animationActive ? FrameResult::Continue : FrameResult::Idle;
		}
	}

	bool Initialize(BusinessCallbacks callbacks)
	{
		if (initialized.exchange(true, std::memory_order_acq_rel)) return false;
		expandedLayoutTarget.store(false, std::memory_order_release);
		active.store(false, std::memory_order_release);
		committedBackgroundActive.store(false, std::memory_order_release);
		{
			std::scoped_lock lock(pageStateMutex);
			pageTransaction.Reset();
		}
		if (!Inkeys::UI::PageControl::Acquire())
		{
			initialized.store(false, std::memory_order_release);
			return false;
		}
		Inkeys::UI::PageControl::SetWhiteboardCallbacks({
			std::move(callbacks.previousPage),
			std::move(callbacks.nextPage),
		});
		if (!Inkeys::UI::RenderPipeline::Register(
			Client::WhiteboardFreeze, RenderBackground))
		{
			Inkeys::UI::PageControl::SetWhiteboardCallbacks({});
			Inkeys::UI::PageControl::Release();
			initialized.store(false, std::memory_order_release);
			return false;
		}
		displaySubscription = Inkeys::Display::Subscribe(
			[](Inkeys::Display::SnapshotPtr)
			{
				if (!initialized.load(std::memory_order_acquire)) return;
				Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
				Inkeys::UI::PageControl::NotifyLayoutChanged();
			});
		PublishPageControlSnapshot();
		return true;
	}

	void Shutdown() noexcept
	{
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		expandedLayoutTarget.store(false, std::memory_order_release);
		active.store(false, std::memory_order_release);
		PublishPageControlSnapshot();
		displaySubscription.Reset();
		Inkeys::UI::RenderPipeline::Unregister(Client::WhiteboardFreeze);
		(void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
		{
			std::unique_lock renderLock(backgroundRenderMutex);
			backgroundScene.Reset();
			backgroundScene.ReleaseDeviceResources();
		}
		committedBackgroundActive.store(false, std::memory_order_release);
		Inkeys::UI::PageControl::SetWhiteboardCallbacks({});
		Inkeys::UI::PageControl::Release();
		std::scoped_lock pageLock(pageStateMutex);
		pageTransaction.Reset();
	}

	WNDPROC WindowProc() noexcept
	{
		return Inkeys::UI::PageControl::WindowProc();
	}

	void PublishExpandedLayoutTarget(bool value) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		if (expandedLayoutTarget.exchange(value, std::memory_order_acq_rel)
			== value) return;
		PublishPageControlSnapshot();
	}

	void PublishActive(bool value) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		if (active.exchange(value, std::memory_order_acq_rel) == value) return;
		PublishPageControlSnapshot();
		Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
	}

	void CancelPointerCapture() noexcept
	{
		if (initialized.load(std::memory_order_acquire))
			Inkeys::UI::PageControl::CancelPointerCapture();
	}

	void PublishPageState(int current, int total, bool changing) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		bool changed = false;
		{
			std::scoped_lock lock(pageStateMutex);
			const PageState previous = pageTransaction.Snapshot();
			const PageState next = pageTransaction.Publish(current, total, changing);
			changed = previous.currentPage != next.currentPage
				|| previous.totalPage != next.totalPage
				|| previous.switching != next.switching
				|| previous.nextIsAdd != next.nextIsAdd;
		}
		if (changed) PublishPageControlSnapshot();
	}

	bool Active() noexcept { return active.load(std::memory_order_acquire); }

	bool BackgroundMatchesActive(bool value) noexcept
	{
		return committedBackgroundActive.load(std::memory_order_acquire) == value;
	}
}
