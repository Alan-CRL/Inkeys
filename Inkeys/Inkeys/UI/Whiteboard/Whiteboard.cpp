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
#include <optional>
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
#include "../Bar/Bar.DirtyRegion.h"

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
		using Inkeys::UI::Bar::BarDirtyRegionTracker;
		using Inkeys::UI::Bar::ResolveBarDebugDamage;
		using Inkeys::UI::Bar::BarDebugFrameWidth;
		using Inkeys::UI::Bar::BarDebugDirtyFrameInset;
		using Inkeys::UI::Bar::BarDebugWindowFrameInset;
		using Inkeys::UI::Bar::BarDebugDirtyColor;
		using Inkeys::UI::Bar::BarDebugPresentedColor;

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
		std::mutex pageStateMutex;
		PageStateTransaction pageTransaction;
		// Present 事务覆盖 BeginDraw -> Render -> GetDC -> EndDraw，退出 reset 必须等待它。
		std::mutex renderTransactionMutex;
		std::array<Scene, 2> controlScenes{};
		Scene backgroundScene;
		std::array<RECT, 2> configuredControlScreens{};
		std::array<float, 2> configuredControlScales{ 0.0F, 0.0F };
		std::array<RECT, 2> previousDebugDirtyFrames{};
		std::array<RECT, 2> previousDebugPresentedFrames{};
		std::array<bool, 2> observedDebugModes{ false, false };
		// owner thread 在渲染事务锁被占用时不能同步清理 Scene，先记录待处理取消。
		std::array<std::atomic_bool, 2> pendingPointerCancel{};
		RECT configuredBackgroundScreen{};
		float configuredBackgroundScale = 0.0F;
		Inkeys::Display::Subscription displaySubscription;
		std::mutex callbackMutex;
		BusinessCallbacks business;

		[[nodiscard]] PageState PublishedPageState() noexcept
		{
			std::scoped_lock lock(pageStateMutex);
			return pageTransaction.Snapshot();
		}

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

		void ConsumePendingPointerCancel(std::size_t index, Scene& scene) noexcept
		{
			if (pendingPointerCancel[index].exchange(false,
				std::memory_order_acq_rel))
				scene.CancelPointer();
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

		void ApplyPageState(Scene& scene, const PageState& page)
		{
			(void)scene.SetWidgetState(WidgetIds[0], true,
				page.previousEnabled, {}, L"左翻页");
			(void)scene.SetWidgetInteractive(
				WidgetIds[0], page.previousInteractive);
			(void)scene.SetWidgetState(WidgetIds[1], true,
				page.pageEnabled, std::to_wstring(page.currentPage),
				L"/" + std::to_wstring(page.totalPage));
			(void)scene.SetWidgetInteractive(
				WidgetIds[1], page.pageInteractive);
			(void)scene.SetWidgetState(WidgetIds[2], true,
				page.nextEnabled, {}, page.nextIsAdd ? L"加页" : L"右翻页",
				page.nextIsAdd ? L"barAdd" : L"barMore",
				page.nextIsAdd ? 0.0 : 90.0);
			(void)scene.SetWidgetInteractive(
				WidgetIds[2], page.nextInteractive);
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
			ApplyPageState(controlScenes[index], PublishedPageState());
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

		void DrawDebugFrame(ID2D1DeviceContext* context, const RECT& bounds,
			COLORREF color, FLOAT inset) noexcept
		{
			if (!context || BarDirtyRegionTracker::IsEmpty(bounds)) return;
			const RECT clipped = bounds;
			const D2D1_ROUNDED_RECT frame = D2D1::RoundedRect(D2D1::RectF(
				static_cast<FLOAT>(clipped.left) + inset,
				static_cast<FLOAT>(clipped.top) + inset,
				static_cast<FLOAT>(clipped.right) - inset,
				static_cast<FLOAT>(clipped.bottom) - inset), 0.0F, 0.0F);
			if (frame.rect.right <= frame.rect.left || frame.rect.bottom <= frame.rect.top)
				return;
			ComPtr<ID2D1SolidColorBrush> brush;
			const D2D1_COLOR_F brushColor = D2D1::ColorF(
				static_cast<FLOAT>(GetRValue(color)) / 255.0F,
				static_cast<FLOAT>(GetGValue(color)) / 255.0F,
				static_cast<FLOAT>(GetBValue(color)) / 255.0F, 1.0F);
			if (FAILED(context->CreateSolidColorBrush(brushColor, &brush)) || !brush)
				return;
			context->DrawRoundedRectangle(&frame, brush.Get(), BarDebugFrameWidth);
		}

		[[nodiscard]] PresentStatus PresentScene(Scene& scene, HWND hwnd,
			const FrameContext& frameContext, const RECT& presentationBounds,
			bool freezeOwner, std::optional<std::size_t> debugIndex = std::nullopt) noexcept
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
			// Scene 接口返回的是借用指针；present 事务内额外持有强引用，
			// 即使设备 epoch 切换触发缓存清理，也不会让当前 COM 对象提前析构。
			ComPtr<ID2D1DeviceContext> contextLease(context);
			ComPtr<ID2D1GdiInteropRenderTarget> gdiLease(gdi);
			context = contextLease.Get();
			gdi = gdiLease.Get();
			context->BeginDraw();
			context->SetTransform(D2D1::Matrix3x2F::Identity());
			context->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
			const auto drawResult = scene.Render(context, frameContext.frameTime);
			const bool debugMode = Inkeys::UI::Bar::DebugModeEnabled();
			RECT businessDamage = drawResult.damage;
			RECT debugPresentDamage{};
			if (debugIndex.has_value())
			{
				const std::size_t index = *debugIndex;
				RECT previousFrame = previousDebugDirtyFrames[index];
				BarDirtyRegionTracker::UnionInPlace(
					previousFrame, previousDebugPresentedFrames[index]);
				const auto debugDamage = ResolveBarDebugDamage(
					businessDamage, {}, previousFrame, {}, debugMode, false);
				debugPresentDamage = debugDamage.presentDamage;
				if (debugMode)
				{
					// 红框标记业务 dirty，绿框标记本次实际 present 覆盖区。
					DrawDebugFrame(context, businessDamage,
						BarDebugDirtyColor, BarDebugDirtyFrameInset);
					DrawDebugFrame(context, debugDamage.presentDamage,
						BarDebugPresentedColor, BarDebugWindowFrameInset);
				}
				else
				{
					// Debug 关闭时仍提交旧框所在区域，确保上一帧残留被擦除。
					businessDamage = debugPresentDamage;
				}
			}
			HDC source = nullptr;
			bool presented = false;
			HRESULT getDcHr = gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &source);
			HRESULT releaseDcHr = S_OK;
			if (SUCCEEDED(getDcHr) && source)
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
				if (debugIndex.has_value())
				{
					// 覆盖层不进入业务 damage，但旧/新框都必须进入提交擦除区。
					BarDirtyRegionTracker::UnionInPlace(dirty, debugPresentDamage);
				}
				if (dirty.right <= dirty.left || dirty.bottom <= dirty.top)
					dirty = RECT{ 0, 0, static_cast<LONG>(width),
						static_cast<LONG>(height) };
				info.prcDirty = &dirty;
				info.dwFlags = ULW_ALPHA;
				presented = freezeOwner
					? SubmitFreezeSurface(hwnd, &info, true)
					: UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
				if (destinationDc) ReleaseDC(nullptr, destinationDc);
				releaseDcHr = gdi->ReleaseDC(nullptr);
			}
			else if (SUCCEEDED(getDcHr)) getDcHr = E_FAIL;
			const HRESULT endDrawHr = context->EndDraw();
			scene.HandleFrameEndDrawResult(endDrawHr);
		(void)drawResult;
		if (IsDeviceLost(getDcHr) || IsDeviceLost(releaseDcHr)
			|| IsDeviceLost(endDrawHr))
		{
			scene.ReleaseDeviceResources();
			return PresentStatus::DeviceLost;
		}
		if (FAILED(getDcHr) || FAILED(releaseDcHr) || FAILED(endDrawHr))
		{
			// GDI interop 与 EndDraw 同属一次 present；任一步失败都重建本窗 target。
			scene.ReleaseDeviceResources();
			return PresentStatus::Retry;
		}
		if (!presented) return PresentStatus::Retry;
		if (debugIndex.has_value())
		{
			const std::size_t index = *debugIndex;
			if (debugMode)
			{
				// 记录业务 dirty 区和实际绿色 present 框，下一帧才能准确擦除旧框。
				previousDebugDirtyFrames[index] = drawResult.damage;
				previousDebugPresentedFrames[index] = debugPresentDamage;
			}
			else
			{
				previousDebugDirtyFrames[index] = {};
				previousDebugPresentedFrames[index] = {};
			}
		}
		return PresentStatus::Success;
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
			PresentStatus presentStatus = PresentStatus::Retry;
			RECT presentation{};
			bool animationActive = false;
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (!EqualRect(&configuredBackgroundScreen, &bounds)
					|| configuredBackgroundScale != scale)
					ConfigureBackgroundScene(bounds, scale);
				presentation = backgroundScene.PresentationBounds();
				presentStatus = PresentScene(backgroundScene, hwnd,
					frameContext, presentation, true);
				if (presentStatus == PresentStatus::Success)
				{
					animationActive = backgroundScene.AnimationActive();
					(void)backgroundScene.ConsumeDamage();
				}
			}
			if (presentStatus != PresentStatus::Success)
				return presentStatus == PresentStatus::DeviceLost
					? FrameResult::DeviceLost : FrameResult::Retry;
			// Exit 可能在 present 期间提交；不要在已取消后重新显示 Freeze。
			if (!active.load(std::memory_order_acquire))
			{
				(void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
				committedBackgroundActive.store(false, std::memory_order_release);
				return FrameResult::Idle;
			}
			(void)Inkeys::Window::GetService().SetBounds(WindowRole::Freeze, presentation);
			(void)Inkeys::Window::GetService().Show(WindowRole::Freeze);
			committedBackgroundActive.store(true, std::memory_order_release);
			return animationActive ? FrameResult::Continue
				: FrameResult::Idle;
		}

		FrameResult RenderControl(std::size_t index, const FrameContext& frameContext)
		{
			auto& service = Inkeys::Window::GetService();
			const HWND hwnd = service.Handle(ControlRoles[index]);
			if (!hwnd) return FrameResult::Retry;
			PresentStatus presentStatus = PresentStatus::Retry;
			RECT presentation{};
			bool animationActive = false;
			bool inactive = false;
			{
				std::unique_lock renderLock(renderTransactionMutex);
				ConsumePendingPointerCancel(index, controlScenes[index]);
				if (!active.load(std::memory_order_acquire))
				{
					controlScenes[index].Reset();
					previousDebugDirtyFrames[index] = {};
					previousDebugPresentedFrames[index] = {};
					observedDebugModes[index] = Inkeys::UI::Bar::DebugModeEnabled();
					inactive = true;
				}
				else
				{
					const bool debugMode = Inkeys::UI::Bar::DebugModeEnabled();
					if (observedDebugModes[index] != debugMode)
					{
						observedDebugModes[index] = debugMode;
						// Debug 开关只触发一次完整重绘，关闭后可擦除上一帧框。
						controlScenes[index].Invalidate();
					}
					const RECT screen = PrimaryBounds();
					const float scale = DpiScale(hwnd);
					if (!EqualRect(&configuredControlScreens[index], &screen)
						|| configuredControlScales[index] != scale)
						ConfigureControlScene(index);
					presentation = controlScenes[index].PresentationBounds();
					presentStatus = PresentScene(controlScenes[index], hwnd,
						frameContext, presentation, false, index);
					if (presentStatus == PresentStatus::Success)
					{
						animationActive = controlScenes[index].AnimationActive();
						(void)controlScenes[index].ConsumeDamage();
					}
				}
			}
			if (inactive)
			{
				(void)service.Hide(ControlRoles[index]);
				return FrameResult::Idle;
			}
			if (presentStatus != PresentStatus::Success)
				return presentStatus == PresentStatus::DeviceLost
					? FrameResult::DeviceLost : FrameResult::Retry;
			if (!active.load(std::memory_order_acquire))
			{
				(void)service.Hide(ControlRoles[index]);
				return FrameResult::Idle;
			}
			(void)service.SetBounds(ControlRoles[index], presentation);
			(void)service.Show(ControlRoles[index]);
			return animationActive ? FrameResult::Continue
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
			const bool pointerMessage = message == WM_MOUSEMOVE
				|| message == WM_MOUSELEAVE || message == WM_LBUTTONDOWN
				|| message == WM_LBUTTONUP;
			// 关闭/切换期间丢弃排队的鼠标消息，避免迟到 Up 触发翻页回调。
			if (pointerMessage && !active.load(std::memory_order_acquire)) return 0;
			if (message == WM_MOUSEMOVE)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				ConsumePendingPointerCancel(index, scene);
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
				std::unique_lock renderLock(renderTransactionMutex);
				ConsumePendingPointerCancel(index, scene);
				scene.PointerLeave();
				return 0;
			}
			if (message == WM_LBUTTONDOWN)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				ConsumePendingPointerCancel(index, scene);
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
				std::unique_lock renderLock(renderTransactionMutex);
				ConsumePendingPointerCancel(index, scene);
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
				std::unique_lock renderLock(renderTransactionMutex, std::try_to_lock);
				if (!renderLock.owns_lock())
				{
					pendingPointerCancel[index].store(true,
						std::memory_order_release);
					return 0;
				}
				scene.CancelPointer();
				pendingPointerCancel[index].store(false,
					std::memory_order_release);
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
		{
			std::scoped_lock lock(pageStateMutex);
			pageTransaction.Reset();
			currentPage.store(1, std::memory_order_relaxed);
			totalPage.store(1, std::memory_order_relaxed);
			switching.store(false, std::memory_order_relaxed);
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
		active.store(false, std::memory_order_release);
		displaySubscription.Reset();
		for (const Client client : Clients)
			Inkeys::UI::RenderPipeline::Unregister(client);
		// Service 操作必须在事务锁外执行，避免 owner thread 的输入消息反向等待渲染锁。
		for (const WindowRole role : ControlRoles)
			(void)Inkeys::Window::GetService().Hide(role);
		(void)Inkeys::Window::GetService().Hide(WindowRole::Freeze);
		{
			std::unique_lock renderLock(renderTransactionMutex);
			for (auto& scene : controlScenes)
			{
				scene.Reset();
				scene.ReleaseDeviceResources();
			}
			for (auto& pending : pendingPointerCancel)
				pending.store(false, std::memory_order_release);
			backgroundScene.Reset();
			backgroundScene.ReleaseDeviceResources();
		}
		committedBackgroundActive.store(false, std::memory_order_release);
		{
			std::scoped_lock pageLock(pageStateMutex);
			pageTransaction.Reset();
			currentPage.store(1, std::memory_order_relaxed);
			totalPage.store(1, std::memory_order_relaxed);
			switching.store(false, std::memory_order_relaxed);
		}
		std::scoped_lock lock(callbackMutex);
		business = {};
	}

	WNDPROC WindowProc() noexcept { return WhiteboardWindowProc; }

	void PublishActive(bool value) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		std::unique_lock renderLock(renderTransactionMutex);
		if (!initialized.load(std::memory_order_acquire)) return;
		if (active.exchange(value, std::memory_order_acq_rel) == value) return;
		if (!value)
		{
			for (std::size_t index = 0; index < controlScenes.size(); ++index)
			{
				pendingPointerCancel[index].store(false,
					std::memory_order_release);
				controlScenes[index].Reset();
			}
		}
		RequestControls();
		Inkeys::UI::RenderPipeline::Request(Client::WhiteboardFreeze);
	}

	void CancelPointerCapture() noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		// 先让 owner thread 释放系统捕获；窗口过程拿不到事务锁时由 pending 标志补偿清理。
		(void)Inkeys::Window::GetService().CancelPointerCapture();
		std::unique_lock renderLock(renderTransactionMutex);
		if (!initialized.load(std::memory_order_acquire)) return;
		for (std::size_t index = 0; index < controlScenes.size(); ++index)
		{
			pendingPointerCancel[index].store(false,
				std::memory_order_release);
			controlScenes[index].CancelPointer();
		}
		RequestControls();
	}

	void PublishPageState(int current, int total, bool changing) noexcept
	{
		if (!initialized.load(std::memory_order_acquire)) return;
		bool currentChanged = false;
		bool totalChanged = false;
		bool switchingChanged = false;
		PageState state;
		{
			std::scoped_lock lock(pageStateMutex);
			if (!initialized.load(std::memory_order_acquire)) return;
			const PageState previous = pageTransaction.Snapshot();
			state = pageTransaction.Publish(current, total, changing);
			currentChanged = previous.currentPage != state.currentPage;
			totalChanged = previous.totalPage != state.totalPage;
			switchingChanged = previous.switching != state.switching;
			currentPage.store(state.currentPage, std::memory_order_relaxed);
			totalPage.store(state.totalPage, std::memory_order_relaxed);
			switching.store(state.switching, std::memory_order_release);
		}
		if (!(currentChanged || totalChanged || switchingChanged)) return;
		// 页码发布来自状态线程，必须与 RenderFrame 共用 present 事务锁。
		std::unique_lock renderLock(renderTransactionMutex);
		for (auto& scene : controlScenes)
			ApplyPageState(scene, state);
		RequestControls();
	}

	bool Active() noexcept { return active.load(std::memory_order_acquire); }

	bool BackgroundMatchesActive(bool value) noexcept
	{
		return committedBackgroundActive.load(std::memory_order_acquire) == value;
	}
}
