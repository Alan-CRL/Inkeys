module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "../Bar/Bar.DirtyRegion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Inkeys.UI.PageControl;

import Inkeys.UI.Bar;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
import Inkeys.Message;
import Inkeys.Window;

namespace Inkeys::UI::PageControl
{
	namespace
	{
		using namespace std::chrono_literals;
		using Microsoft::WRL::ComPtr;
		using Client = Inkeys::UI::RenderPipeline::Client;
		using FrameContext = Inkeys::UI::RenderPipeline::FrameContext;
		using FrameResult = Inkeys::UI::RenderPipeline::FrameResult;
		using Scene = Inkeys::UI::Bar::BarSurfaceScene;
		using WidgetSpec = Inkeys::UI::Bar::BarSurfaceWidgetSpec;
		using WidgetId = Inkeys::UI::Bar::BarSurfaceWidgetId;
		using WindowRole = Inkeys::Window::WindowRole;

		constexpr WidgetId DragWidget = 1;
		constexpr WidgetId PreviousWidget = 2;
		constexpr WidgetId PageWidget = 3;
		constexpr WidgetId NextWidget = 4;
		constexpr auto LayoutTransitionDuration = 240ms;
		constexpr auto LongPressDelay = 500ms;
		constexpr auto LongPressInterval = 120ms;
		constexpr auto ExternalPressDuration = 110ms;

		constexpr std::array<Client, 4> Clients{
			Client::PptBottomLeft,
			Client::PptBottomRight,
			Client::PptMiddleLeft,
			Client::PptMiddleRight,
		};
		constexpr std::array<WindowRole, 4> Roles{
			WindowRole::PptBottomLeft,
			WindowRole::PptBottomRight,
			WindowRole::PptMiddleLeft,
			WindowRole::PptMiddleRight,
		};

		struct AnimatedBounds
		{
			double left = 0.0;
			double top = 0.0;
			double right = 1.0;
			double bottom = 1.0;
			double scale = 1.0;
			double startLeft = 0.0;
			double startTop = 0.0;
			double startRight = 1.0;
			double startBottom = 1.0;
			double startScale = 1.0;
			RECT target{ 0, 0, 1, 1 };
			double targetScale = 1.0;
			std::chrono::steady_clock::time_point started{};
			bool initialized = false;
			bool active = false;
		};

		struct SurfaceState
		{
			Scene scene;
			WorkspaceMode configuredMode = WorkspaceMode::Hidden;
			AnimatedBounds bounds;
			bool sceneConfigured = false;
			bool targetVisible = false;
			bool inputLocked = true;
			bool dragging = false;
			bool repeatTriggered = false;
			bool pressedNext = false;
			DWORD touchId = 0;
			bool touchActive = false;
			POINT dragStartScreen{};
			PptLayoutState dragStartLayout{};
			PptLayoutState feasibleLayout{};
			std::chrono::steady_clock::time_point pressStarted{};
			std::chrono::steady_clock::time_point lastRepeat{};
			std::chrono::steady_clock::time_point externalPressUntil{};
			bool externalPressNext = false;
			std::chrono::steady_clock::time_point layoutTransitionUntil{};
			std::uint64_t observedRevision = 0;
			SIZE backingCapacity{ 1, 1 };
			SIZE committedPresentationSize{};
			SIZE committedBackingCapacity{};
			std::uint64_t committedDeviceGeneration = 0;
			bool committedPresentationReady = false;
			bool forceFullPresentation = true;
			bool windowCommitFailureActive = false;
		};

		std::array<SurfaceState, 4> surfaces;
		std::atomic_uint referenceCount = 0;
		std::atomic_bool initialized = false;
		std::atomic_uint64_t publishedRevision = 1;
		std::atomic_uint64_t directMoveRevision = 1;
		std::atomic_bool debugEnabled = false;
		std::mutex lifecycleMutex;
		std::mutex snapshotMutex;
		std::mutex callbackMutex;
		std::mutex renderTransactionMutex;
		std::mutex presentationMutex;
		PptState publishedPpt;
		WhiteboardState publishedWhiteboard;
		PptCallbacks pptCallbacks;
		WhiteboardCallbacks whiteboardCallbacks;
		Inkeys::Display::Subscription displaySubscription;
		thread_local bool translatingTouch = false;

		[[nodiscard]] constexpr std::size_t Index(Client client) noexcept
		{
			return static_cast<std::size_t>(client)
				- static_cast<std::size_t>(Client::PptBottomLeft);
		}

		[[nodiscard]] std::size_t Index(HWND hwnd) noexcept
		{
			auto& service = Inkeys::Window::GetService();
			for (std::size_t index = 0; index < Roles.size(); ++index)
				if (service.Handle(Roles[index]) == hwnd) return index;
			return 0;
		}

		[[nodiscard]] Surface SurfaceFor(std::size_t index) noexcept
		{
			return static_cast<Surface>(index);
		}

		void RequestAll() noexcept
		{
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::PptPageMask());
		}

		void RequestSurface(std::size_t index) noexcept
		{
			Inkeys::UI::RenderPipeline::Request(Clients[index]);
		}

		[[nodiscard]] std::pair<PptState, WhiteboardState> Snapshot() noexcept
		{
			std::scoped_lock lock(snapshotMutex);
			return { publishedPpt, publishedWhiteboard };
		}

		struct RenderSnapshot
		{
			PptState ppt;
			WhiteboardState whiteboard;
			std::uint64_t revision = 0;
		};

		[[nodiscard]] RenderSnapshot SnapshotForRender() noexcept
		{
			std::scoped_lock lock(snapshotMutex);
			// 状态和 revision 必须来自同一个临界区，否则并发发布可能被误判为已观察。
			return { publishedPpt, publishedWhiteboard,
				publishedRevision.load(std::memory_order_relaxed) };
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

		[[nodiscard]] std::wstring PageNumber(int value, int maximum)
		{
			return value < 0 ? L"-"
				: std::to_wstring((std::min)(maximum, value));
		}

		void InvokeDirection(std::size_t surfaceIndex, bool next)
		{
			if (surfaceIndex >= surfaces.size()) return;
			std::function<void()> callback;
			const auto [ppt, whiteboard] = Snapshot();
			{
				std::scoped_lock lock(callbackMutex);
				if (whiteboard.active && surfaceIndex < 2)
					callback = next ? whiteboardCallbacks.nextPage
						: whiteboardCallbacks.previousPage;
				else if (ppt.presentationVisible)
					callback = next ? pptCallbacks.nextPage
						: pptCallbacks.previousPage;
			}
			if (callback) callback();
		}

		void ApplyDarkTheme(WidgetSpec& widget)
		{
			widget.useThemeColors = false;
			widget.fill = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::PressedFill);
			widget.content = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::TextPrimary);
		}

		[[nodiscard]] Inkeys::UI::Bar::BarSurfaceBackgroundSpec BuildBackground(
			std::size_t index, WorkspaceMode mode)
		{
			Inkeys::UI::Bar::BarSurfaceBackgroundSpec background;
			const bool whiteboard = mode == WorkspaceMode::WhiteboardExpanded;
			const bool vertical = index >= 2;
			background.bounds = { 0.0, 0.0,
				whiteboard ? WhiteboardWidthDip
					: vertical ? PptCompactShortSideDip : PptCompactLongSideDip,
				whiteboard ? WhiteboardHeightDip
					: vertical ? PptCompactLongSideDip : PptCompactShortSideDip };
			background.useThemeColors = false;
			background.fill = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::Surface);
			background.frame = GetThemeColor(
				BarThemeModeEnum::Dark, BarThemeColorEnum::SurfaceFrame);
			background.cornerRadiusDip = whiteboard
				? BarMainBarCornerRadiusDip : BarButtonCornerRadiusDip;
			background.frameThicknessDip =
				BarButtonFrameThicknessDip;
			background.fillOpacity = BarMainBarFillOpacity;
			background.frameOpacity = BarMainBarFrameOpacity;
			return background;
		}

		[[nodiscard]] std::vector<WidgetSpec> BuildWidgets(
			std::size_t index, WorkspaceMode mode,
			const PptState& ppt, const WhiteboardState& whiteboard)
		{
			std::vector<WidgetSpec> widgets(4);
			auto& drag = widgets[0];
			drag.id = DragWidget;
			drag.kind = Inkeys::UI::Bar::BarSurfaceWidgetKind::DragHandle;
			drag.visible = mode != WorkspaceMode::Hidden;
			drag.enabled = drag.visible;
			drag.interactive = drag.visible;
			ApplyDarkTheme(drag);

			auto& previous = widgets[1];
			previous.id = PreviousWidget;
			previous.iconResource = L"barMore";
			previous.iconSizeDip = mode == WorkspaceMode::WhiteboardExpanded
				? BarButtonTwoTwoIconSizeDip : 18.0;
			previous.onClick = [index] { InvokeDirection(index, false); };
			ApplyDarkTheme(previous);

			auto& page = widgets[2];
			page.id = PageWidget;
			page.primaryFontSizeDip = mode == WorkspaceMode::WhiteboardExpanded
				? 28.0 : 15.0;
			page.secondaryFontSizeDip = mode == WorkspaceMode::WhiteboardExpanded
				? 13.0 : 13.0;
			ApplyDarkTheme(page);

			auto& next = widgets[3];
			next.id = NextWidget;
			next.iconSizeDip = mode == WorkspaceMode::WhiteboardExpanded
				? BarButtonTwoTwoIconSizeDip : 18.0;
			next.onClick = [index] { InvokeDirection(index, true); };
			ApplyDarkTheme(next);

			if (mode == WorkspaceMode::WhiteboardExpanded)
			{
				// 白板仍只呈现三枚按钮；页码按钮顶部 10 DIP 复用为专属拖动条。
				drag.bounds = { 80.0, 5.0, 150.0, 15.0 };
				previous.bounds = { 5.0, 5.0, 75.0, 75.0 };
				page.bounds = { 80.0, 5.0, 150.0, 75.0 };
				next.bounds = { 155.0, 5.0, 225.0, 75.0 };
				previous.enabled = whiteboard.previousEnabled;
				previous.interactive = whiteboard.previousInteractive;
				previous.secondaryText = L"左翻页";
				previous.iconAngle = -90.0;
				page.enabled = whiteboard.pageEnabled;
				page.interactive = whiteboard.pageInteractive;
				page.primaryText = std::to_wstring(whiteboard.currentPage);
				page.secondaryText = L"/" + std::to_wstring(whiteboard.totalPage);
				page.primaryOffsetYDip = -10.0;
				page.primarySlotHeightDip = 28.0;
				page.secondaryOffsetYDip = 20.0;
				page.secondarySlotHeightDip = 25.0;
				next.enabled = whiteboard.nextEnabled;
				next.interactive = whiteboard.nextInteractive;
				next.secondaryText = whiteboard.nextIsAdd ? L"加页" : L"右翻页";
				next.iconResource = whiteboard.nextIsAdd ? L"barAdd" : L"barMore";
				next.iconAngle = whiteboard.nextIsAdd ? 0.0 : 90.0;
			}
			else
			{
				const bool vertical = index >= 2;
				const bool right = index == 1;
				const auto contracts = ResolvePptWidgetContracts(SurfaceFor(index));
				drag.bounds = { contracts[0].bounds.left, contracts[0].bounds.top,
					contracts[0].bounds.right, contracts[0].bounds.bottom };
				previous.bounds = { contracts[1].bounds.left, contracts[1].bounds.top,
					contracts[1].bounds.right, contracts[1].bounds.bottom };
				page.bounds = { contracts[2].bounds.left, contracts[2].bounds.top,
					contracts[2].bounds.right, contracts[2].bounds.bottom };
				next.bounds = { contracts[3].bounds.left, contracts[3].bounds.top,
					contracts[3].bounds.right, contracts[3].bounds.bottom };
				if (vertical)
				{
					previous.iconAngle = 180.0;
					next.iconAngle = 0.0;
					page.primaryOffsetYDip = -10.0;
					page.secondaryOffsetYDip = 10.0;
				}
				else if (right)
				{
					previous.iconAngle = -90.0;
					next.iconAngle = 90.0;
					page.primaryOffsetXDip = -13.0;
					page.secondaryOffsetXDip = 13.0;
				}
				else
				{
					previous.iconAngle = -90.0;
					next.iconAngle = 90.0;
					page.primaryOffsetXDip = -13.0;
					page.secondaryOffsetXDip = 13.0;
				}
				previous.enabled = previous.interactive = true;
				page.enabled = page.interactive = true;
				next.enabled = next.interactive = true;
				const int maximum = vertical ? 999 : 9999;
				page.primaryText = PageNumber(ppt.currentPage, maximum);
				page.secondaryText = L"/" + PageNumber(ppt.totalPage, maximum);
				page.primarySlotHeightDip = 18.0;
				page.secondarySlotHeightDip = 18.0;
			}
			return widgets;
		}

		[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept
		{
			return EqualRect(&left, &right) != FALSE;
		}

		[[nodiscard]] double Ease(double progress) noexcept
		{
			progress = (std::clamp)(progress, 0.0, 1.0);
			return progress < 0.5
				? 4.0 * progress * progress * progress
				: 1.0 - std::pow(-2.0 * progress + 2.0, 3.0) / 2.0;
		}

		void AdvanceBounds(AnimatedBounds& animation,
			std::chrono::steady_clock::time_point now) noexcept
		{
			if (!animation.active) return;
			const double elapsed = std::chrono::duration<double, std::milli>(
				now - animation.started).count();
			const double progress = (std::clamp)(elapsed
				/ static_cast<double>(LayoutTransitionDuration.count()), 0.0, 1.0);
			const double value = Ease(progress);
			auto Mix = [value](double start, double target)
			{
				return start + (target - start) * value;
			};
			animation.left = Mix(animation.startLeft, animation.target.left);
			animation.top = Mix(animation.startTop, animation.target.top);
			animation.right = Mix(animation.startRight, animation.target.right);
			animation.bottom = Mix(animation.startBottom, animation.target.bottom);
			animation.scale = Mix(animation.startScale, animation.targetScale);
			if (progress >= 1.0)
			{
				animation.active = false;
				animation.left = animation.target.left;
				animation.top = animation.target.top;
				animation.right = animation.target.right;
				animation.bottom = animation.target.bottom;
				animation.scale = animation.targetScale;
			}
		}

		void RetargetBounds(AnimatedBounds& animation, const RECT& target,
			double scale, bool animate,
			std::chrono::steady_clock::time_point now) noexcept
		{
			AdvanceBounds(animation, now);
			if (!animation.initialized)
			{
				animation.left = target.left;
				animation.top = target.top;
				animation.right = target.right;
				animation.bottom = target.bottom;
				animation.scale = scale;
				animation.target = target;
				animation.targetScale = scale;
				animation.initialized = true;
				return;
			}
			if (SameRect(animation.target, target)
				&& std::abs(animation.targetScale - scale) < 0.0001) return;
			animation.startLeft = animation.left;
			animation.startTop = animation.top;
			animation.startRight = animation.right;
			animation.startBottom = animation.bottom;
			animation.startScale = animation.scale;
			animation.target = target;
			animation.targetScale = scale;
			animation.started = now;
			animation.active = animate;
			if (!animate)
			{
				animation.left = target.left;
				animation.top = target.top;
				animation.right = target.right;
				animation.bottom = target.bottom;
				animation.scale = scale;
			}
		}

		[[nodiscard]] RECT CurrentBounds(const AnimatedBounds& animation) noexcept
		{
			RECT result{
				static_cast<LONG>(std::lround(animation.left)),
				static_cast<LONG>(std::lround(animation.top)),
				static_cast<LONG>(std::lround(animation.right)),
				static_cast<LONG>(std::lround(animation.bottom)) };
			if (result.right <= result.left) result.right = result.left + 1;
			if (result.bottom <= result.top) result.bottom = result.top + 1;
			return result;
		}

		[[nodiscard]] std::optional<RECT> MainBarObstacle() noexcept
		{
			const HWND hwnd = Inkeys::Window::GetService().Handle(WindowRole::Bar);
			RECT bounds{};
			if (!hwnd || !IsWindowVisible(hwnd) || !GetWindowRect(hwnd, &bounds)
				|| bounds.right <= bounds.left || bounds.bottom <= bounds.top)
				return std::nullopt;
			return bounds;
		}

		void ConfigureSurface(std::size_t index, WorkspaceMode desiredMode,
			const PptState& ppt, const WhiteboardState& whiteboard,
			const RECT& monitor, const ResolvedSurfaceLayout& target,
			std::uint64_t revision,
			std::chrono::steady_clock::time_point now)
		{
			auto& state = surfaces[index];
			const WorkspaceMode visualMode = desiredMode == WorkspaceMode::Hidden
				? (state.sceneConfigured && state.configuredMode != WorkspaceMode::Hidden
					? state.configuredMode : WorkspaceMode::PptCompact)
				: desiredMode;
			const bool modeChanged = state.sceneConfigured
				&& state.configuredMode != visualMode;
			const bool contentChanged = state.observedRevision != revision;
			if (!state.sceneConfigured)
			{
				const auto background = BuildBackground(index, visualMode);
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard);
				(void)state.scene.Configure(background, widgets);
				state.scene.SetSharedPrimaryLightSubscribed(true);
				state.scene.SetHooks({ {}, [index] { RequestSurface(index); } });
				const bool fadeAtTarget = desiredMode != WorkspaceMode::Hidden
					&& index < 2;
				state.scene.SetOpacity(fadeAtTarget || desiredMode == WorkspaceMode::Hidden
					? 0.0 : 1.0, 0.0);
				if (fadeAtTarget)
				{
					state.scene.SetOpacity(1.0,
						static_cast<double>(LayoutTransitionDuration.count()));
					state.layoutTransitionUntil = now + LayoutTransitionDuration;
				}
				state.sceneConfigured = true;
				state.configuredMode = visualMode;
				state.observedRevision = revision;
			}
			else if (modeChanged || contentChanged)
			{
				const auto background = BuildBackground(index, visualMode);
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard);
				const bool animateLayout = !modeChanged
					|| ShouldAnimateWorkspaceLayout(SurfaceFor(index),
						state.configuredMode, visualMode, state.targetVisible);
				(void)state.scene.TransitionLayout(background, widgets,
					animateLayout
						? static_cast<double>(LayoutTransitionDuration.count()) : 0.0);
				if (modeChanged)
					state.layoutTransitionUntil = now + LayoutTransitionDuration;
				state.configuredMode = visualMode;
				state.observedRevision = revision;
			}

			const bool wasVisible = state.targetVisible;
			state.targetVisible = desiredMode != WorkspaceMode::Hidden;
			const bool entering = !wasVisible && state.targetVisible;
			if (!state.bounds.initialized && entering && index >= 2)
			{
				// 侧栏即使首次帧已可见，也先建立屏外起点以保留侧向出场。
				RetargetBounds(state.bounds, ResolveHiddenSurfaceBounds(
					SurfaceFor(index), monitor, target.logicalBounds),
					target.scale, false, now);
			}
			if (state.targetVisible != wasVisible)
			{
				state.scene.SetOpacity(state.targetVisible ? 1.0 : 0.0,
					static_cast<double>(LayoutTransitionDuration.count()));
				state.layoutTransitionUntil = now + LayoutTransitionDuration;
			}
			const bool animateBounds = state.bounds.initialized
				&& (wasVisible || state.targetVisible)
				&& (!entering || index >= 2);
			if (!state.targetVisible && state.bounds.initialized
				&& ShouldPreserveSurfaceBoundsWhileHiding(SurfaceFor(index)))
			{
				// 底栏隐藏时冻结当前 frame，只让透明度退场。
				AdvanceBounds(state.bounds, now);
				RetargetBounds(state.bounds, CurrentBounds(state.bounds),
					state.bounds.scale, false, now);
			}
			else
				RetargetBounds(state.bounds, target.logicalBounds, target.scale,
					animateBounds, now);
			AdvanceBounds(state.bounds, now);
			state.inputLocked = ShouldLockSurfaceInput(state.targetVisible,
				now < state.layoutTransitionUntil,
				WhiteboardWorkspaceSwitching(whiteboard));
			(void)state.scene.SetBounds(CurrentBounds(state.bounds),
				static_cast<float>(state.bounds.scale));
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
			SIZE backingCapacity, bool forceFullReplacement,
			bool showDebugFrames, bool finalIdleFrame) noexcept
		{
			const UINT width = static_cast<UINT>(presentationBounds.right
				- presentationBounds.left);
			const UINT height = static_cast<UINT>(presentationBounds.bottom
				- presentationBounds.top);
			if (!hwnd || width == 0 || height == 0) return PresentStatus::Retry;
			const HRESULT resourceHr = scene.EnsureDeviceResources(
				frameContext.epoch,
				static_cast<UINT>((std::max)(1L, backingCapacity.cx)),
				static_cast<UINT>((std::max)(1L, backingCapacity.cy)));
			if (FAILED(resourceHr)) return IsDeviceLost(resourceHr)
				? PresentStatus::DeviceLost : PresentStatus::Retry;
			auto* rawContext = scene.DeviceContext();
			auto* rawGdi = scene.GdiInteropRenderTarget();
			if (!rawContext || !rawGdi) return PresentStatus::Retry;
			ComPtr<ID2D1DeviceContext> context(rawContext);
			ComPtr<ID2D1GdiInteropRenderTarget> gdi(rawGdi);
			context->BeginDraw();
			context->SetTransform(D2D1::Matrix3x2F::Identity());
			context->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
			(void)scene.Render(context.Get(), frameContext.frameTime);
			RECT dirty = scene.PendingDamage();
			if (dirty.right <= dirty.left || dirty.bottom <= dirty.top)
				dirty = RECT{ 0, 0, static_cast<LONG>(width),
					static_cast<LONG>(height) };
			if (showDebugFrames)
			{
				// 红/绿框表示本帧 damage，蓝框直接表示当前 HWND 提交边界。
				ComPtr<ID2D1SolidColorBrush> damageBrush;
				ComPtr<ID2D1SolidColorBrush> windowBrush;
				const COLORREF damageColor =
					Inkeys::UI::Bar::ResolveBarDebugFrameColor(finalIdleFrame);
				(void)context->CreateSolidColorBrush(D2D1::ColorF(
					GetRValue(damageColor) / 255.0F,
					GetGValue(damageColor) / 255.0F,
					GetBValue(damageColor) / 255.0F, 1.0F), &damageBrush);
				(void)context->CreateSolidColorBrush(D2D1::ColorF(
					0.0F, 120.0F / 255.0F, 1.0F, 1.0F), &windowBrush);
				constexpr FLOAT frameWidth = Inkeys::UI::Bar::BarDebugFrameWidth;
				constexpr FLOAT dirtyInset =
					Inkeys::UI::Bar::BarDebugDirtyFrameInset;
				constexpr FLOAT windowInset =
					Inkeys::UI::Bar::BarDebugWindowFrameInset;
				const D2D1_RECT_F damageRect = D2D1::RectF(
					static_cast<FLOAT>((std::max)(0L, dirty.left)) + dirtyInset,
					static_cast<FLOAT>((std::max)(0L, dirty.top)) + dirtyInset,
					static_cast<FLOAT>((std::min)(static_cast<LONG>(width), dirty.right))
						- dirtyInset,
					static_cast<FLOAT>((std::min)(static_cast<LONG>(height), dirty.bottom))
						- dirtyInset);
				if (damageBrush && damageRect.right > damageRect.left
					&& damageRect.bottom > damageRect.top)
					context->DrawRectangle(&damageRect, damageBrush.Get(), frameWidth);
				const D2D1_RECT_F windowRect = D2D1::RectF(
					windowInset, windowInset,
					static_cast<FLOAT>(width) - windowInset,
					static_cast<FLOAT>(height) - windowInset);
				if (windowBrush)
					context->DrawRectangle(&windowRect, windowBrush.Get(), frameWidth);
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
				info.pblend = &blend;
				info.prcDirty = forceFullReplacement || showDebugFrames
					? nullptr : &dirty;
				info.dwFlags = ULW_ALPHA;
				presented = UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
				releaseDcHr = gdi->ReleaseDC(nullptr);
			}
			else if (SUCCEEDED(getDcHr)) getDcHr = E_FAIL;
			const HRESULT endDrawHr = context->EndDraw();
			scene.HandleFrameEndDrawResult(endDrawHr);
			if (IsDeviceLost(getDcHr) || IsDeviceLost(releaseDcHr)
				|| IsDeviceLost(endDrawHr))
			{
				scene.ReleaseDeviceResources();
				return PresentStatus::DeviceLost;
			}
			if (FAILED(getDcHr) || FAILED(releaseDcHr)
				|| FAILED(endDrawHr) || !presented)
			{
				scene.ReleaseDeviceResources();
				return PresentStatus::Retry;
			}
			return PresentStatus::Success;
		}

		[[nodiscard]] bool DragLayoutCollides(Surface moved,
			const RECT& monitor, float dpiScale, const PptState& source,
			const WhiteboardState& whiteboard,
			const PptLayoutState& layout) noexcept
		{
			PptState candidate = source;
			candidate.layout = layout;
			const LONG gap = static_cast<LONG>(std::lround(
				PageControlGapDip * NormalizeScale(dpiScale)));
			const bool bottom = moved == Surface::BottomLeft
				|| moved == Surface::BottomRight;
			const std::array<Surface, 2> group = bottom
				? std::array<Surface, 2>{ Surface::BottomLeft, Surface::BottomRight }
				: std::array<Surface, 2>{ Surface::MiddleLeft, Surface::MiddleRight };
			const std::array<Surface, 2> other = bottom
				? std::array<Surface, 2>{ Surface::MiddleLeft, Surface::MiddleRight }
				: std::array<Surface, 2>{ Surface::BottomLeft, Surface::BottomRight };
			const auto obstacle = MainBarObstacle();
			for (const Surface item : group)
			{
				const auto current = ResolveSurfaceLayout(item, monitor,
					dpiScale, candidate, whiteboard);
				if (obstacle.has_value()
					&& OverlapsWithGap(current.logicalBounds, *obstacle, gap))
					return true;
				for (const Surface otherItem : other)
				{
					const auto otherLayout = ResolveSurfaceLayout(otherItem,
						monitor, dpiScale, candidate, whiteboard);
					if (otherLayout.visible && OverlapsWithGap(current.logicalBounds,
						otherLayout.logicalBounds, gap)) return true;
				}
			}
			return false;
		}

		void SetBoundsDirect(AnimatedBounds& bounds, const RECT& target,
			double scale) noexcept
		{
			bounds.left = bounds.startLeft = target.left;
			bounds.top = bounds.startTop = target.top;
			bounds.right = bounds.startRight = target.right;
			bounds.bottom = bounds.startBottom = target.bottom;
			bounds.scale = bounds.startScale = scale;
			bounds.target = target;
			bounds.targetScale = scale;
			bounds.initialized = true;
			bounds.active = false;
		}

		[[nodiscard]] bool MovePairWindowsDirect(
			const std::array<std::size_t, 2>& indices,
			const std::array<POINT, 2>& translations) noexcept
		{
			auto& service = Inkeys::Window::GetService();
			std::array<HWND, 2> handles{};
			std::array<RECT, 2> original{};
			for (std::size_t item = 0; item < indices.size(); ++item)
			{
				handles[item] = service.Handle(Roles[indices[item]]);
				if (!handles[item] || !GetWindowRect(handles[item], &original[item]))
					return false;
			}

			HDWP batch = BeginDeferWindowPos(static_cast<int>(indices.size()));
			if (!batch) return false;
			constexpr UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
				| SWP_NOOWNERZORDER | SWP_NOSENDCHANGING;
			for (std::size_t item = 0; item < indices.size(); ++item)
			{
				batch = DeferWindowPos(batch, handles[item], nullptr,
					original[item].left + translations[item].x,
					original[item].top + translations[item].y,
					0, 0, flags);
				if (!batch) return false;
			}
			if (EndDeferWindowPos(batch)) return true;

			// 批量提交失败时恢复两个窗口，内部布局也不会前进。
			for (std::size_t item = 0; item < indices.size(); ++item)
				(void)SetWindowPos(handles[item], nullptr,
					original[item].left, original[item].top, 0, 0, flags);
			return false;
		}

		void UpdateDrag(std::size_t index, POINT screen)
		{
			auto& state = surfaces[index];
			const Surface moved = SurfaceFor(index);
			const bool left = moved == Surface::BottomLeft
				|| moved == Surface::MiddleLeft;
			const bool bottom = moved == Surface::BottomLeft
				|| moved == Surface::BottomRight;
			const float dx = static_cast<float>(screen.x - state.dragStartScreen.x);
			const float dy = static_cast<float>(screen.y - state.dragStartScreen.y);
			PptLayoutState candidate = state.dragStartLayout;
			if (bottom)
			{
				candidate.bottomPairWidth += left ? dx : -dx;
				candidate.bottomPairHeight -= dy;
			}
			else
			{
				candidate.middlePairWidth += left ? dx : -dx;
				candidate.middlePairHeight -= dy;
			}
			const RECT monitor = PrimaryBounds();
			const float dpiScale = DpiScale(nullptr);
			PptState ppt;
			WhiteboardState whiteboard;
			{
				std::scoped_lock lock(snapshotMutex);
				ppt = publishedPpt;
				whiteboard = publishedWhiteboard;
			}
			candidate = ClampPageControlLayout(moved, monitor,
				dpiScale, whiteboard, candidate);
			const PptLayoutState previousLayout = state.feasibleLayout;
			if (DragLayoutCollides(moved, monitor, dpiScale,
				ppt, whiteboard, candidate)) candidate = state.feasibleLayout;
			PptState previousPpt = ppt;
			PptState candidatePpt = ppt;
			previousPpt.layout = previousLayout;
			candidatePpt.layout = candidate;
			const std::array<std::size_t, 2> pair = bottom
				? std::array<std::size_t, 2>{ 0, 1 }
				: std::array<std::size_t, 2>{ 2, 3 };
			std::array<ResolvedSurfaceLayout, 2> layouts{};
			std::array<POINT, 2> translations{};
			for (std::size_t item = 0; item < pair.size(); ++item)
			{
				const Surface pairSurface = SurfaceFor(pair[item]);
				const auto previous = ResolveSurfaceLayout(pairSurface, monitor,
					dpiScale, previousPpt, whiteboard);
				layouts[item] = ResolveSurfaceLayout(pairSurface, monitor,
					dpiScale, candidatePpt, whiteboard);
				translations[item] = POINT{
					layouts[item].logicalBounds.left - previous.logicalBounds.left,
					layouts[item].logicalBounds.top - previous.logicalBounds.top };
			}
			if (translations[0].x == 0 && translations[0].y == 0
				&& translations[1].x == 0 && translations[1].y == 0) return;
			std::scoped_lock presentationLock(presentationMutex);
			if (!MovePairWindowsDirect(pair, translations)) return;
			state.feasibleLayout = candidate;
			{
				std::scoped_lock lock(snapshotMutex);
				publishedPpt.layout = candidate;
			}
			for (std::size_t item = 0; item < pair.size(); ++item)
			{
				auto& pairState = surfaces[pair[item]];
				SetBoundsDirect(pairState.bounds, layouts[item].logicalBounds,
					layouts[item].scale);
				(void)pairState.scene.SetBounds(layouts[item].logicalBounds,
					layouts[item].scale);
			}
			directMoveRevision.fetch_add(1, std::memory_order_release);
		}

		void PersistDragPosition(const PptLayoutState& layout)
		{
			std::function<void(PptLayoutState)> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = pptCallbacks.persistPosition;
			}
			if (callback) callback(layout);
		}

		void LogWindowCommitState(std::size_t index, HWND hwnd,
			bool shouldShow, bool boundsApplied, bool visibilityApplied,
			bool recovered) noexcept
		{
			RECT bounds{};
			if (hwnd) (void)GetWindowRect(hwnd, &bounds);
			const HWND owner = hwnd ? GetWindow(hwnd, GW_OWNER) : nullptr;
			const LONG_PTR exStyle = hwnd
				? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
			wchar_t message[640]{};
			swprintf_s(message,
				L"[PageControl] window commit %s: role=%u hwnd=0x%llX "
				L"owner=0x%llX visible=%d topmost=%d bounds=(%ld,%ld,%ld,%ld) "
				L"shouldShow=%d present=success setBounds=%d visibility=%d\n",
				recovered ? L"recovered" : L"failed",
				static_cast<unsigned>(Roles[index]),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(hwnd)),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(owner)),
				hwnd && IsWindowVisible(hwnd) ? 1 : 0,
				(exStyle & WS_EX_TOPMOST) != 0 ? 1 : 0,
				bounds.left, bounds.top, bounds.right, bounds.bottom,
				shouldShow ? 1 : 0, boundsApplied ? 1 : 0,
				visibilityApplied ? 1 : 0);
			OutputDebugStringW(message);
		}

		FrameResult RenderSurface(std::size_t index,
			const FrameContext& frameContext)
		{
			if (!initialized.load(std::memory_order_acquire))
				return FrameResult::Idle;
			auto& service = Inkeys::Window::GetService();
			const std::uint64_t frameDirectMoveRevision =
				directMoveRevision.load(std::memory_order_acquire);
			const HWND hwnd = service.Handle(Roles[index]);
			if (!hwnd) return FrameResult::Retry;
			const RECT monitor = PrimaryBounds();
			const float dpiScale = DpiScale(hwnd);
			auto renderSnapshot = SnapshotForRender();
			auto& ppt = renderSnapshot.ppt;
			const auto& whiteboard = renderSnapshot.whiteboard;
			const auto obstacle = MainBarObstacle();
			ppt = ResolveRuntimePageControlLayout(monitor, dpiScale,
				ppt, whiteboard, obstacle ? &*obstacle : nullptr);
			const Surface surface = SurfaceFor(index);
			const WorkspaceMode mode = ResolveWorkspaceMode(
				surface, ppt, whiteboard);
			auto target = ResolveSurfaceLayout(surface, monitor,
				dpiScale, ppt, whiteboard);
			if (mode == WorkspaceMode::Hidden && index >= 2)
				target.logicalBounds = ResolveHiddenSurfaceBounds(
					surface, monitor, target.logicalBounds);

			PresentStatus presentStatus = PresentStatus::Success;
			RECT presentation{};
			bool keepAnimating = false;
			bool shouldShow = false;
			bool repeatDirection = false;
			bool repeatNext = false;
			{
				std::unique_lock renderLock(renderTransactionMutex);
				auto& state = surfaces[index];
				ConfigureSurface(index, mode, ppt, whiteboard, monitor, target,
					renderSnapshot.revision,
					frameContext.frameTime);
				if (state.externalPressUntil.time_since_epoch().count() != 0)
				{
					const bool active = frameContext.frameTime < state.externalPressUntil;
					(void)state.scene.SetWidgetExternalPressed(
						state.externalPressNext ? NextWidget : PreviousWidget, active);
					if (!active) state.externalPressUntil = {};
				}
				if (state.pressStarted.time_since_epoch().count() != 0
					&& frameContext.frameTime - state.pressStarted >= LongPressDelay
					&& frameContext.frameTime - state.lastRepeat >= LongPressInterval)
				{
					state.lastRepeat = frameContext.frameTime;
					state.repeatTriggered = true;
					repeatDirection = true;
					repeatNext = state.pressedNext;
				}
				presentation = state.scene.PresentationBounds();
				const SIZE presentationSize{
					presentation.right - presentation.left,
					presentation.bottom - presentation.top };
				const SIZE targetContentSize{
					target.logicalBounds.right - target.logicalBounds.left,
					target.logicalBounds.bottom - target.logicalBounds.top };
				const auto backing = ResolveStableBackingSize(
					state.backingCapacity, presentationSize, targetContentSize,
					state.scene.PresentationOutsetPixels(), state.bounds.scale,
					target.scale);
				state.backingCapacity = backing.size;
				const bool exitTransitionActive = !state.targetVisible
					&& frameContext.frameTime < state.layoutTransitionUntil;
				const bool visibleAnimation = state.targetVisible
					&& (state.bounds.active || state.scene.AnimationActive()
						|| state.pressStarted.time_since_epoch().count() != 0
						|| state.externalPressUntil.time_since_epoch().count() != 0);
				keepAnimating = exitTransitionActive || visibleAnimation;
				shouldShow = ShouldKeepPageControlWindowVisible(
					state.targetVisible, exitTransitionActive);
				if (shouldShow)
				{
					const bool presentationSizeChanged =
						!state.committedPresentationReady
						|| state.committedPresentationSize.cx != presentationSize.cx
						|| state.committedPresentationSize.cy != presentationSize.cy;
					const bool backingChanged =
						!state.committedPresentationReady
						|| state.committedBackingCapacity.cx != backing.size.cx
						|| state.committedBackingCapacity.cy != backing.size.cy;
					const bool deviceChanged =
						state.committedDeviceGeneration
							!= frameContext.epoch.generation;
					const bool forceFullReplacement = state.forceFullPresentation
						|| presentationSizeChanged || backingChanged || deviceChanged;
					presentStatus = PresentScene(state.scene, hwnd,
						frameContext, presentation, backing.size,
						forceFullReplacement,
						debugEnabled.load(std::memory_order_acquire),
						!keepAnimating);
					if (presentStatus == PresentStatus::Success)
					{
						(void)state.scene.ConsumeDamage();
						state.committedPresentationSize = presentationSize;
						state.committedBackingCapacity = backing.size;
						state.committedDeviceGeneration = frameContext.epoch.generation;
						state.committedPresentationReady = true;
						state.forceFullPresentation = false;
					}
					else
						state.forceFullPresentation = true;
				}
			}
			// 业务回调可能反向发布 UI 状态，不能在 Scene/present 事务锁内调用。
			if (repeatDirection) InvokeDirection(index, repeatNext);
			if (presentStatus != PresentStatus::Success)
				return presentStatus == PresentStatus::DeviceLost
					? FrameResult::DeviceLost : FrameResult::Retry;
			std::scoped_lock presentationLock(presentationMutex);
			// 拖动期间产生的过期帧不能把已经直移的 HWND 拉回旧坐标。
			if (frameDirectMoveRevision
				!= directMoveRevision.load(std::memory_order_acquire))
				return FrameResult::Retry;
			bool boundsApplied = true;
			bool visibilityApplied = false;
			if (shouldShow)
			{
				boundsApplied = service.SetBounds(Roles[index], presentation);
				if (boundsApplied)
					visibilityApplied = service.Show(Roles[index]);
			}
			else
				visibilityApplied = service.Hide(Roles[index]);
			auto& state = surfaces[index];
			if (!visibilityApplied || (shouldShow && !boundsApplied))
			{
				if (!state.windowCommitFailureActive)
					LogWindowCommitState(index, hwnd, shouldShow,
						boundsApplied, visibilityApplied, false);
				state.windowCommitFailureActive = true;
				return FrameResult::Retry;
			}
			if (state.windowCommitFailureActive)
			{
				state.windowCommitFailureActive = false;
				LogWindowCommitState(index, hwnd, shouldShow,
					boundsApplied, visibilityApplied, true);
			}
			return keepAnimating ? FrameResult::Continue : FrameResult::Idle;
		}

		LRESULT CALLBACK PageControlWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			const std::size_t index = Index(hwnd);
			auto& state = surfaces[index];
			if ((message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN
				|| message == WM_LBUTTONUP || message == WM_MOUSEWHEEL)
				&& !translatingTouch
				&& Inkeys::Message::IsTouchGeneratedMouseMessage(
					message, GetMessageExtraInfo())) return 0;
			if (message == WM_TOUCH)
			{
				const UINT count = LOWORD(wParam);
				std::vector<TOUCHINPUT> inputs(count);
				if (count != 0 && GetTouchInputInfo(
					reinterpret_cast<HTOUCHINPUT>(lParam), count,
					inputs.data(), sizeof(TOUCHINPUT)))
				{
					for (const auto& input : inputs)
					{
						UINT translated = 0;
						{
							std::unique_lock renderLock(renderTransactionMutex);
							const bool primary = (input.dwFlags
								& TOUCHEVENTF_PRIMARY) != 0;
							if ((input.dwFlags & TOUCHEVENTF_DOWN)
								&& primary && !state.touchActive)
							{
								state.touchActive = true;
								state.touchId = input.dwID;
								translated = WM_LBUTTONDOWN;
							}
							else if (state.touchActive && state.touchId == input.dwID)
							{
								if (input.dwFlags & TOUCHEVENTF_MOVE)
									translated = WM_MOUSEMOVE;
								else if (input.dwFlags & TOUCHEVENTF_UP)
								{
									state.touchActive = false;
									state.touchId = 0;
									translated = WM_LBUTTONUP;
								}
							}
						}
						if (translated != 0)
						{
							POINT local{ TOUCH_COORD_TO_PIXEL(input.x),
								TOUCH_COORD_TO_PIXEL(input.y) };
							ScreenToClient(hwnd, &local);
							translatingTouch = true;
							(void)PageControlWindowProc(hwnd, translated,
								translated == WM_LBUTTONUP ? 0 : MK_LBUTTON,
								MAKELPARAM(local.x, local.y));
							translatingTouch = false;
						}
					}
				}
				CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
				return 0;
			}
			if (message == WM_NCHITTEST)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible || state.inputLocked) return HTTRANSPARENT;
				RECT window{};
				GetWindowRect(hwnd, &window);
				const POINT local{ GET_X_LPARAM(lParam) - window.left,
					GET_Y_LPARAM(lParam) - window.top };
				return state.scene.HitTestPresentation(local)
					== Inkeys::UI::Bar::BarSurfaceNoWidget
					? HTTRANSPARENT : HTCLIENT;
			}
			if (message == WM_MOUSEMOVE)
			{
				TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&tracking);
				POINT screen{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ClientToScreen(hwnd, &screen);
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible || state.inputLocked) return 0;
				if (state.dragging)
				{
					UpdateDrag(index, screen);
					return 0;
				}
				const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (const auto logical = state.scene.PresentationToLogical(local))
					state.scene.PointerMove(*logical);
				else state.scene.PointerLeave();
				return 0;
			}
			if (message == WM_MOUSELEAVE)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.dragging) state.scene.PointerLeave();
				return 0;
			}
			if (message == WM_LBUTTONDOWN)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible || state.inputLocked) return 0;
				const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const auto logical = state.scene.PresentationToLogical(local);
				if (!logical.has_value()) return 0;
				const auto result = state.scene.PointerDown(*logical);
				if (!result.consumed) return 0;
				SetCapture(hwnd);
				if (result.pressed == DragWidget)
				{
					state.dragging = true;
					state.dragStartScreen = local;
					ClientToScreen(hwnd, &state.dragStartScreen);
					auto [ppt, whiteboard] = Snapshot();
					const RECT monitor = PrimaryBounds();
					const auto obstacle = MainBarObstacle();
					ppt = ResolveRuntimePageControlLayout(monitor, DpiScale(hwnd),
						ppt, whiteboard, obstacle ? &*obstacle : nullptr);
					state.dragStartLayout = ppt.layout;
					state.feasibleLayout = ppt.layout;
				}
				else if (result.pressed == PreviousWidget
					|| result.pressed == NextWidget)
				{
					state.pressedNext = result.pressed == NextWidget;
					state.pressStarted = std::chrono::steady_clock::now();
					state.lastRepeat = state.pressStarted;
					state.repeatTriggered = false;
					RequestSurface(index);
				}
				return 0;
			}
			if (message == WM_LBUTTONUP)
			{
				PptLayoutState persisted{};
				bool persist = false;
				bool dragEnded = false;
				bool shouldInvokeClick = false;
				Inkeys::UI::Bar::BarSurfaceWidgetId clicked =
					Inkeys::UI::Bar::BarSurfaceNoWidget;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					const bool acceptInput = !state.inputLocked && state.targetVisible;
					shouldInvokeClick = acceptInput && !state.repeatTriggered;
					if (acceptInput)
					{
						if (const auto logical = state.scene.PresentationToLogical(local))
							clicked = state.scene.PointerUp(*logical, false).clicked;
						else state.scene.CancelPointer();
					}
					else state.scene.CancelPointer();
					if (acceptInput && state.dragging)
					{
						const auto [ppt, whiteboard] = Snapshot();
						(void)whiteboard;
						persisted = ppt.layout;
						persist = persisted.rememberPosition;
						dragEnded = true;
					}
					state.dragging = false;
					state.pressStarted = {};
					state.repeatTriggered = false;
				}
				if (GetCapture() == hwnd) ReleaseCapture();
				if (dragEnded)
				{
					std::scoped_lock lock(snapshotMutex);
					publishedRevision.fetch_add(1, std::memory_order_relaxed);
				}
				if (shouldInvokeClick
					&& (clicked == PreviousWidget || clicked == NextWidget))
					InvokeDirection(index, clicked == NextWidget);
				if (dragEnded) RequestAll();
				if (persist) PersistDragPosition(persisted);
				return 0;
			}
			if (message == WM_MOUSEWHEEL)
			{
				bool invoke = false;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					invoke = state.targetVisible && !state.inputLocked;
				}
				if (invoke) InvokeDirection(
					index, GET_WHEEL_DELTA_WPARAM(wParam) < 0);
				return 0;
			}
			if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				state.scene.CancelPointer();
				state.dragging = false;
				state.pressStarted = {};
				state.repeatTriggered = false;
				state.touchActive = false;
				state.touchId = 0;
				return 0;
			}
			if (message == WM_ERASEBKGND) return 1;
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}

	bool Acquire()
	{
		std::scoped_lock lifecycleLock(lifecycleMutex);
		const unsigned previous = referenceCount.load(std::memory_order_acquire);
		if (previous != 0)
		{
			referenceCount.store(previous + 1, std::memory_order_release);
			return true;
		}
		initialized.store(true, std::memory_order_release);
		for (std::size_t index = 0; index < Clients.size(); ++index)
		{
			if (!Inkeys::UI::RenderPipeline::Register(Clients[index],
				[index](const FrameContext& context)
				{
					return RenderSurface(index, context);
				}))
			{
				for (std::size_t registered = 0; registered < index; ++registered)
					Inkeys::UI::RenderPipeline::Unregister(Clients[registered]);
				initialized.store(false, std::memory_order_release);
				return false;
			}
		}
		displaySubscription = Inkeys::Display::Subscribe(
			[](Inkeys::Display::SnapshotPtr)
			{
				if (initialized.load(std::memory_order_acquire)) RequestAll();
			});
		referenceCount.store(1, std::memory_order_release);
		RequestAll();
		return true;
	}

	void Release() noexcept
	{
		std::scoped_lock lifecycleLock(lifecycleMutex);
		const unsigned count = referenceCount.load(std::memory_order_acquire);
		if (count == 0) return;
		if (count > 1)
		{
			referenceCount.store(count - 1, std::memory_order_release);
			return;
		}
		referenceCount.store(0, std::memory_order_release);
		initialized.store(false, std::memory_order_release);
		displaySubscription.Reset();
		for (const Client client : Clients)
			Inkeys::UI::RenderPipeline::Unregister(client);
		auto& service = Inkeys::Window::GetService();
		for (const WindowRole role : Roles) (void)service.Hide(role);
		std::unique_lock renderLock(renderTransactionMutex);
		for (auto& surface : surfaces)
		{
			surface.scene.Reset();
			surface.scene.ReleaseDeviceResources();
			surface = SurfaceState{};
		}
	}

	WNDPROC WindowProc() noexcept { return PageControlWindowProc; }

	void SetPptCallbacks(PptCallbacks callbacks)
	{
		std::scoped_lock lock(callbackMutex);
		pptCallbacks = std::move(callbacks);
	}

	void SetWhiteboardCallbacks(WhiteboardCallbacks callbacks)
	{
		std::scoped_lock lock(callbackMutex);
		whiteboardCallbacks = std::move(callbacks);
	}

	void PublishPptState(const PptState& state) noexcept
	{
		{
			std::scoped_lock lock(snapshotMutex);
			publishedPpt = state;
			publishedRevision.fetch_add(1, std::memory_order_release);
		}
		RequestAll();
	}

	void PublishWhiteboardState(const WhiteboardState& state) noexcept
	{
		{
			std::scoped_lock lock(snapshotMutex);
			publishedWhiteboard = state;
			publishedRevision.fetch_add(1, std::memory_order_release);
		}
		RequestAll();
	}

	void FlashPptDirection(bool next) noexcept
	{
		const auto until = std::chrono::steady_clock::now()
			+ ExternalPressDuration;
		const auto [ppt, whiteboard] = Snapshot();
		std::unique_lock renderLock(renderTransactionMutex);
		for (std::size_t index = 0; index < surfaces.size(); ++index)
		{
			if (!ShouldFlashPptSurface(SurfaceFor(index), ppt, whiteboard))
				continue;
			surfaces[index].externalPressNext = next;
			surfaces[index].externalPressUntil = until;
			if (surfaces[index].sceneConfigured)
				(void)surfaces[index].scene.SetWidgetExternalPressed(
					next ? NextWidget : PreviousWidget, true);
		}
		renderLock.unlock();
		RequestAll();
	}

	void QueuePptWheel(short delta) noexcept
	{
		const auto [ppt, whiteboard] = Snapshot();
		if (!ppt.presentationVisible || whiteboard.expandedLayoutTarget
			|| whiteboard.active || WhiteboardWorkspaceSwitching(whiteboard)
			|| delta == 0) return;
		InvokeDirection(0, delta < 0);
		FlashPptDirection(delta < 0);
	}

	void SetDebugEnabled(bool enabled) noexcept
	{
		if (debugEnabled.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		{
			std::unique_lock renderLock(renderTransactionMutex);
			// 开关调试框后全量替换一次，确保旧覆盖层不会留在 layered bitmap。
			for (auto& surface : surfaces) surface.forceFullPresentation = true;
		}
		RequestAll();
	}

	void NotifyLayoutChanged() noexcept
	{
		{
			std::scoped_lock lock(snapshotMutex);
			publishedRevision.fetch_add(1, std::memory_order_relaxed);
		}
		RequestAll();
	}

	void CancelPointerCapture() noexcept
	{
		(void)Inkeys::Window::GetService().CancelPointerCapture();
		std::unique_lock renderLock(renderTransactionMutex);
		for (auto& surface : surfaces)
		{
			surface.scene.CancelPointer();
			surface.dragging = false;
			surface.pressStarted = {};
			surface.repeatTriggered = false;
			surface.touchActive = false;
			surface.touchId = 0;
		}
		RequestAll();
	}
}
