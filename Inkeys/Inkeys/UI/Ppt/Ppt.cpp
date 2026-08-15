module;

#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../../../IdtConfiguration.h"
#include "../../../IdtDraw.h"
#include "../../../IdtImage.h"

module Inkeys.UI.Ppt;

import Inkeys.UI.RenderPipeline;
import Inkeys.Window;
import Inkeys.Other.Config;
import Inkeys.Display;

using Microsoft::WRL::ComPtr;

namespace Inkeys::UI::Ppt
{
	namespace
	{
		using namespace std::chrono_literals;
		using RenderClient = Inkeys::UI::RenderPipeline::Client;
		using FrameResult = Inkeys::UI::RenderPipeline::FrameResult;
		using FrameContext = Inkeys::UI::RenderPipeline::FrameContext;
		using WindowRole = Inkeys::Window::WindowRole;

		constexpr std::array<RenderClient, 5> Clients{
			RenderClient::PptBottomLeft,
			RenderClient::PptBottomRight,
			RenderClient::PptMiddleLeft,
			RenderClient::PptMiddleRight,
			RenderClient::PptExitShow,
		};

		constexpr std::array<WindowRole, 5> Roles{
			WindowRole::PptBottomLeft,
			WindowRole::PptBottomRight,
			WindowRole::PptMiddleLeft,
			WindowRole::PptMiddleRight,
			WindowRole::PptExitShow,
		};

		enum class PointerAction : std::uint8_t
		{
			Move,
			Down,
			Up,
			Leave,
			Wheel,
			Cancel,
		};

		struct PointerEvent
		{
			PointerAction action = PointerAction::Move;
			POINT local{};
			POINT screen{};
			short wheel = 0;
		};

		enum class HitTarget : std::uint8_t
		{
			None,
			Previous,
			Page,
			Next,
			EndShow,
			Drag,
		};

		struct TargetResources
		{
			ComPtr<ID2D1DeviceContext> context;
			ComPtr<ID2D1Bitmap1> bitmap;
			ComPtr<ID2D1GdiInteropRenderTarget> gdi;
			ComPtr<ID2D1SolidColorBrush> brush;
			ComPtr<ID2D1StrokeStyle> dragHandleStyle;
			ComPtr<IDWriteTextFormat> pageFormat;
			ComPtr<IDWriteTextFormat> totalFormat;
			std::array<ComPtr<ID2D1Bitmap1>, 6> icons;
			std::uint64_t generation = 0;
			UINT width = 0;
			UINT height = 0;

			void Reset() noexcept
			{
				if (context) context->SetTarget(nullptr);
				for (auto& icon : icons) icon.Reset();
				totalFormat.Reset();
				pageFormat.Reset();
				dragHandleStyle.Reset();
				brush.Reset();
				gdi.Reset();
				bitmap.Reset();
				context.Reset();
				generation = 0;
				width = height = 0;
			}
		};

		struct ClientState
		{
			std::mutex inputMutex;
			std::deque<PointerEvent> input;
			TargetResources target;
			HitTarget hover = HitTarget::None;
			HitTarget pressed = HitTarget::None;
			bool dragging = false;
			bool shown = false;
			bool finalDebugFrame = false;
			bool animationInitialized = false;
			float currentLeft = 0.0F;
			float currentTop = 0.0F;
			float currentWidth = 1.0F;
			float currentHeight = 1.0F;
			float currentScale = 1.0F;
			float currentOpacity = 0.0F;
			float startLeft = 0.0F;
			float startTop = 0.0F;
			float startWidth = 1.0F;
			float startHeight = 1.0F;
			float startScale = 1.0F;
			float targetLeft = 0.0F;
			float targetTop = 0.0F;
			float targetWidth = 1.0F;
			float targetHeight = 1.0F;
			float targetScale = 1.0F;
			std::chrono::steady_clock::time_point geometryStarted{};
			RECT observedMonitor{};
			float observedDpiScale = 1.0F;
			bool displayGeometryInitialized = false;
			bool displayTransitionActive = false;
			POINT dragStart{};
			RECT dragMonitor{};
			float dragDpiScale = 1.0F;
			float dragStartWidth = 0.0F;
			float dragStartHeight = 0.0F;
			float feasibleWidth = 0.0F;
			float feasibleHeight = 0.0F;
			RECT committedScreen{};
			RECT committedVisual{};
			HitTarget committedHover = HitTarget::None;
			HitTarget committedPressed = HitTarget::None;
			int committedCurrentPage = -1;
			int committedTotalPage = -1;
			bool committedDebug = false;
			bool committedDebugActive = false;
			float committedOpacity = -1.0F;
			std::uint64_t committedGeneration = 0;
			UINT committedWidth = 0;
			UINT committedHeight = 0;
			std::chrono::steady_clock::time_point pressStarted{};
			std::chrono::steady_clock::time_point lastRepeat{};
		};

		struct TouchState
		{
			DWORD id = 0;
			bool active = false;
			bool primary = false;
			POINT last{};
		};

		struct Layout
		{
			RECT screen{};
			UINT width = 1;
			UINT height = 1;
			UINT capacityWidth = 1;
			UINT capacityHeight = 1;
			float scale = 1.0F;
			float opacity = 0.0F;
			bool visible = false;
			bool animationActive = false;
			bool targetVisible = false;
		};

		std::array<ClientState, 5> states;
		std::array<TouchState, 5> touches;
		std::array<std::atomic<UINT>, 5> dpiTokens{};
		std::array<std::atomic_bool, 3> groupDragging{};
		Inkeys::Display::Subscription displaySubscription;
		std::mutex callbackMutex;
		std::mutex configurationMutex;
		BusinessCallbacks business;
		LayoutConfiguration configuration;
		std::array<Inkeys::Graphics::DibSurface, 6> iconSurfaces;
		bool iconsReady = false;
		std::atomic_bool initialized = false;
		std::atomic_bool presentationVisible = false;
		std::atomic_bool debugEnabled = false;
		std::atomic_int currentPage = -1;
		std::atomic_int totalPage = -1;
		std::atomic_bool keyboardFlashNext = false;
		std::atomic<long long> keyboardFlashUntil = 0;

		[[nodiscard]] constexpr std::size_t Index(RenderClient client) noexcept
		{
			return static_cast<std::size_t>(client) -
				static_cast<std::size_t>(RenderClient::PptBottomLeft);
		}

		[[nodiscard]] constexpr std::size_t Index(WindowRole role) noexcept
		{
			return static_cast<std::size_t>(role) -
				static_cast<std::size_t>(WindowRole::PptBottomLeft);
		}

		[[nodiscard]] constexpr bool IsPptRole(WindowRole role) noexcept
		{
			return role >= WindowRole::PptBottomLeft && role <= WindowRole::PptExitShow;
		}

		[[nodiscard]] constexpr RenderClient ClientFor(WindowRole role) noexcept
		{
			return static_cast<RenderClient>(
				static_cast<unsigned>(RenderClient::PptBottomLeft) + Index(role));
		}

		[[nodiscard]] constexpr WindowRole RoleFor(RenderClient client) noexcept
		{
			return Roles[Index(client)];
		}

		[[nodiscard]] constexpr Control ControlFor(RenderClient client) noexcept
		{
			return static_cast<Control>(Index(client));
		}

		[[nodiscard]] constexpr bool IsBottom(RenderClient client) noexcept
		{
			return client == RenderClient::PptBottomLeft ||
				client == RenderClient::PptBottomRight;
		}

		[[nodiscard]] constexpr bool IsMiddle(RenderClient client) noexcept
		{
			return client == RenderClient::PptMiddleLeft ||
				client == RenderClient::PptMiddleRight;
		}

		[[nodiscard]] constexpr bool IsLeft(RenderClient client) noexcept
		{
			return client == RenderClient::PptBottomLeft ||
				client == RenderClient::PptMiddleLeft;
		}

		[[nodiscard]] constexpr std::size_t DragGroup(RenderClient client) noexcept
		{
			return IsBottom(client) ? 0U : IsMiddle(client) ? 1U : 2U;
		}

		[[nodiscard]] float DpiScale(RenderClient client) noexcept
		{
			const UINT dpi = dpiTokens[Index(client)].load(std::memory_order_acquire);
			return NormalizePptDpiScale(static_cast<float>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) /
				static_cast<float>(USER_DEFAULT_SCREEN_DPI));
		}

		[[nodiscard]] UINT QueryWindowDpi(HWND hwnd) noexcept
		{
			using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
			static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			const UINT dpi = getDpiForWindow && hwnd ? getDpiForWindow(hwnd) : 0;
			return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
		}

		[[nodiscard]] Inkeys::UI::RenderPipeline::ClientMask PairMask(
			RenderClient client) noexcept
		{
			using Inkeys::UI::RenderPipeline::Mask;
			if (IsBottom(client))
				return Mask(RenderClient::PptBottomLeft) |
					Mask(RenderClient::PptBottomRight);
			if (IsMiddle(client))
				return Mask(RenderClient::PptMiddleLeft) |
					Mask(RenderClient::PptMiddleRight);
			return Mask(RenderClient::PptExitShow);
		}

		struct PrimaryDisplayLayout
		{
			RECT bounds{};
			float dpiScale = 1.0F;
		};

		[[nodiscard]] PrimaryDisplayLayout ReadPrimaryDisplayLayout(
			RenderClient client) noexcept
		{
			const auto snapshot = Inkeys::Display::GetSnapshot();
			if (const auto* monitor = snapshot ? snapshot->Primary() : nullptr)
				return { monitor->bounds, NormalizePptDpiScale(
					static_cast<float>(monitor->effectiveDpiX
						? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI) /
					static_cast<float>(USER_DEFAULT_SCREEN_DPI)) };
			return {
				RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN),
					GetSystemMetrics(SM_CYSCREEN) },
				DpiScale(client),
			};
		}

		[[nodiscard]] LayoutConfiguration ReadConfiguration()
		{
			std::scoped_lock lock(configurationMutex);
			return configuration;
		}

		void WriteConfiguration(const LayoutConfiguration& next)
		{
			std::scoped_lock lock(configurationMutex);
			configuration = next;
		}

		[[nodiscard]] LayoutConfiguration SnapshotLegacyConfiguration()
		{
			LayoutConfiguration snapshot;
			snapshot.bottomPairWidth = pptComSetlist.bottomBothWidth;
			snapshot.bottomPairHeight = pptComSetlist.bottomBothHeight;
			snapshot.middlePairWidth = pptComSetlist.middleBothWidth;
			snapshot.middlePairHeight = pptComSetlist.middleBothHeight;
			snapshot.exitWidth = pptComSetlist.bottomMiddleWidth;
			snapshot.exitHeight = pptComSetlist.bottomMiddleHeight;
			snapshot.bottomPairScale = pptComSetlist.bottomSideBothWidgetScale;
			snapshot.middlePairScale = pptComSetlist.middleSideBothWidgetScale;
			snapshot.exitScale = pptComSetlist.bottomSideMiddleWidgetScale;
			snapshot.showBottomPair = pptComSetlist.showBottomBoth;
			snapshot.showMiddlePair = pptComSetlist.showMiddleBoth;
			snapshot.showExit = pptComSetlist.showBottomMiddle;
			snapshot.rememberPosition = pptComSetlist.memoryWidgetPosition;
			return snapshot;
		}

		[[nodiscard]] Layout CalculateLayout(RenderClient client, ClientState& state,
			std::chrono::steady_clock::time_point now)
		{
			const auto display = ReadPrimaryDisplayLayout(client);
			const RECT monitor = display.bounds;
			const auto runtime = ResolveRuntimeLayoutConfiguration(monitor,
				ReadConfiguration(), display.dpiScale);
			const auto resolved = ResolveControlLayout(ControlFor(client), monitor,
				runtime.configuration, presentationVisible.load(std::memory_order_acquire),
				runtime.dpiScale);
			Layout layout;
			layout.targetVisible = resolved.enabled;
			const RECT& target = resolved.enabled ? resolved.expanded : resolved.hidden;
			auto UpdateDisplayGeometry = [&]()
				{
					const float seconds = std::chrono::duration<float>(now -
						state.geometryStarted).count();
					const float progress = std::clamp(seconds / 0.4F, 0.0F, 1.0F);
					state.currentLeft = InterpolatePptDisplayValue(
						state.startLeft, state.targetLeft, progress);
					state.currentTop = InterpolatePptDisplayValue(
						state.startTop, state.targetTop, progress);
					state.currentWidth = InterpolatePptDisplayValue(
						state.startWidth, state.targetWidth, progress);
					state.currentHeight = InterpolatePptDisplayValue(
						state.startHeight, state.targetHeight, progress);
					state.currentScale = InterpolatePptDisplayValue(
						state.startScale, state.targetScale, progress);
					return progress;
				};
			auto DisplayMatches = [&]() noexcept
				{
					return state.displayGeometryInitialized &&
						EqualRect(&state.observedMonitor, &monitor) &&
						std::abs(state.observedDpiScale - runtime.dpiScale) <= 0.0001F;
				};
			auto TargetMatches = [&]() noexcept
				{
					return std::abs(state.targetLeft - target.left) <= 0.01F &&
						std::abs(state.targetTop - target.top) <= 0.01F &&
						std::abs(state.targetWidth - resolved.backing.cx) <= 0.01F &&
						std::abs(state.targetHeight - resolved.backing.cy) <= 0.01F &&
						std::abs(state.targetScale - resolved.scale) <= 0.0001F;
				};
			if (!state.animationInitialized)
			{
				state.currentLeft = static_cast<float>(target.left);
				state.currentTop = static_cast<float>(target.top);
				state.currentWidth = static_cast<float>(resolved.backing.cx);
				state.currentHeight = static_cast<float>(resolved.backing.cy);
				state.currentScale = resolved.scale;
				state.startLeft = state.targetLeft = state.currentLeft;
				state.startTop = state.targetTop = state.currentTop;
				state.startWidth = state.targetWidth = state.currentWidth;
				state.startHeight = state.targetHeight = state.currentHeight;
				state.startScale = state.targetScale = state.currentScale;
				state.geometryStarted = now;
				state.observedMonitor = monitor;
				state.observedDpiScale = runtime.dpiScale;
				state.displayGeometryInitialized = true;
				state.displayTransitionActive = false;
				state.currentOpacity = resolved.enabled ? 1.0F : 0.0F;
				state.animationInitialized = true;
			}
			else
			{
				const bool dragging = groupDragging[DragGroup(client)].load(
					std::memory_order_acquire);
				if (!dragging)
				{
					float progress = 0.0F;
					if (state.displayTransitionActive)
						progress = UpdateDisplayGeometry();
					if (!DisplayMatches())
					{
						// 只对显示范围或 DPI 变化启用固定 0.4 秒动画；显隐仍沿用旧推进逻辑。
						state.startLeft = state.currentLeft;
						state.startTop = state.currentTop;
						state.startWidth = state.currentWidth;
						state.startHeight = state.currentHeight;
						state.startScale = state.currentScale;
						state.targetLeft = static_cast<float>(target.left);
						state.targetTop = static_cast<float>(target.top);
						state.targetWidth = static_cast<float>(resolved.backing.cx);
						state.targetHeight = static_cast<float>(resolved.backing.cy);
						state.targetScale = resolved.scale;
						state.geometryStarted = now;
						state.observedMonitor = monitor;
						state.observedDpiScale = runtime.dpiScale;
						state.displayGeometryInitialized = true;
						state.displayTransitionActive = !TargetMatches() ||
							std::abs(state.currentLeft - state.targetLeft) > 0.01F ||
							std::abs(state.currentTop - state.targetTop) > 0.01F ||
							std::abs(state.currentWidth - state.targetWidth) > 0.01F ||
							std::abs(state.currentHeight - state.targetHeight) > 0.01F ||
							std::abs(state.currentScale - state.targetScale) > 0.0001F;
					}
					else if (state.displayTransitionActive && !TargetMatches())
					{
						// 配置或显隐变化不是显示事件，取消几何过渡并交回旧布局动画。
						state.displayTransitionActive = false;
					}
					else if (state.displayTransitionActive && progress >= 1.0F)
					{
						state.displayTransitionActive = false;
					}

					if (!state.displayTransitionActive)
					{
						state.currentLeft = AdvanceLegacyValue(state.currentLeft,
							static_cast<float>(target.left));
						state.currentTop = AdvanceLegacyValue(state.currentTop,
							static_cast<float>(target.top));
						state.currentWidth = static_cast<float>(resolved.backing.cx);
						state.currentHeight = static_cast<float>(resolved.backing.cy);
						state.currentScale = resolved.scale;
						state.startLeft = state.targetLeft = static_cast<float>(target.left);
						state.startTop = state.targetTop = static_cast<float>(target.top);
						state.startWidth = state.targetWidth = state.currentWidth;
						state.startHeight = state.targetHeight = state.currentHeight;
						state.startScale = state.targetScale = state.currentScale;
					}
				}
				state.currentOpacity = AdvanceLegacyValue(state.currentOpacity,
					resolved.enabled ? 1.0F : 0.0F, 15.0F, 1.0F / 255.0F);
			}
			layout.animationActive = state.displayTransitionActive ||
				std::abs(state.currentLeft - target.left) > 0.01F ||
				std::abs(state.currentTop - target.top) > 0.01F ||
				std::abs(state.currentWidth - state.targetWidth) > 0.01F ||
				std::abs(state.currentHeight - state.targetHeight) > 0.01F ||
				std::abs(state.currentScale - state.targetScale) > 0.0001F ||
				std::abs(state.currentOpacity - (resolved.enabled ? 1.0F : 0.0F)) > 0.001F;
			layout.scale = state.currentScale;
			layout.width = static_cast<UINT>((std::max)(1.0F,
				std::round(state.currentWidth)));
			layout.height = static_cast<UINT>((std::max)(1.0F,
				std::round(state.currentHeight)));
			layout.capacityWidth = static_cast<UINT>(std::ceil((std::max)({ 1.0F,
				state.currentWidth, state.targetWidth })));
			layout.capacityHeight = static_cast<UINT>(std::ceil((std::max)({ 1.0F,
				state.currentHeight, state.targetHeight })));
			layout.opacity = std::clamp(state.currentOpacity, 0.0F, 1.0F);
			layout.visible = resolved.enabled || layout.animationActive || layout.opacity > 0.0F;
			layout.screen.left = static_cast<LONG>(std::lround(state.currentLeft));
			layout.screen.top = static_cast<LONG>(std::lround(state.currentTop));
			layout.screen.right = layout.screen.left + static_cast<LONG>(layout.width);
			layout.screen.bottom = layout.screen.top + static_cast<LONG>(layout.height);
			return layout;
		}

		[[nodiscard]] HitTarget HitTest(RenderClient client, POINT point,
			const Layout& layout) noexcept
		{
			const float x = static_cast<float>(point.x) / layout.scale;
			const float y = static_cast<float>(point.y) / layout.scale;
			const auto geometry = ResolveControlVisualGeometry(ControlFor(client));
			auto Contains = [x, y](const VisualRect& rect)
				{
					return x >= rect.left && x <= rect.right &&
						y >= rect.top && y <= rect.bottom;
				};
			if (client == RenderClient::PptExitShow)
				return Contains(geometry.action)
					? HitTarget::EndShow : HitTarget::Drag;
			if (IsBottom(client))
			{
				if (Contains(geometry.previous))
					return HitTarget::Previous;
				if (IsInPageHitArea(ControlFor(client), x, y))
					return HitTarget::Page;
				if (Contains(geometry.next))
					return HitTarget::Next;
				return HitTarget::Drag;
			}
			if (Contains(geometry.previous))
				return HitTarget::Previous;
			if (IsInPageHitArea(ControlFor(client), x, y))
				return HitTarget::Page;
			if (Contains(geometry.next))
				return HitTarget::Next;
			return HitTarget::Drag;
		}

		void Invoke(HitTarget target)
		{
			BusinessCallbacks snapshot;
			{
				std::scoped_lock lock(callbackMutex);
				snapshot = business;
			}
			switch (target)
			{
			case HitTarget::Previous:
				if (snapshot.previousPage) snapshot.previousPage();
				break;
			case HitTarget::Next:
				if (snapshot.nextPage) snapshot.nextPage();
				break;
			case HitTarget::Page:
				if (snapshot.viewShow) snapshot.viewShow();
				break;
			case HitTarget::EndShow:
				if (snapshot.endShow) snapshot.endShow();
				break;
			default:
				break;
			}
		}

		void PersistPosition(LayoutConfiguration configurationSnapshot)
		{
			std::function<void(LayoutConfiguration)> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = business.persistPosition;
			}
			if (callback) callback(std::move(configurationSnapshot));
		}

		void UpdateDrag(RenderClient client, ClientState& state, POINT screen)
		{
			const float dx = static_cast<float>(screen.x - state.dragStart.x);
			const float dy = static_cast<float>(screen.y - state.dragStart.y);
			auto candidate = ReadConfiguration();
			if (IsBottom(client))
			{
				candidate.bottomPairWidth = state.dragStartWidth + (IsLeft(client) ? dx : -dx);
				candidate.bottomPairHeight = state.dragStartHeight - dy;
			}
			else if (IsMiddle(client))
			{
				candidate.middlePairWidth = state.dragStartWidth + (IsLeft(client) ? dx : -dx);
				candidate.middlePairHeight = state.dragStartHeight - dy;
			}
			else
			{
				candidate.exitWidth = state.dragStartWidth + dx;
				candidate.exitHeight = state.dragStartHeight - dy;
			}
			const RECT monitor = state.dragMonitor;
			candidate = ClampPptDrag(ControlFor(client), monitor, candidate,
				state.dragDpiScale);

			// 五个控件互相排斥；碰撞时回退最近可行采样，而不是整次手势起点。
			const auto movedMask = PairMask(client);
			const bool collision = PptDragCollides(ControlFor(client), monitor, candidate,
				state.dragDpiScale);
			if (collision)
			{
				if (IsBottom(client))
					candidate.bottomPairWidth = state.feasibleWidth,
					candidate.bottomPairHeight = state.feasibleHeight;
				else if (IsMiddle(client))
					candidate.middlePairWidth = state.feasibleWidth,
					candidate.middlePairHeight = state.feasibleHeight;
				else
					candidate.exitWidth = state.feasibleWidth,
					candidate.exitHeight = state.feasibleHeight;
			}
			else if (IsBottom(client))
				state.feasibleWidth = candidate.bottomPairWidth,
				state.feasibleHeight = candidate.bottomPairHeight;
			else if (IsMiddle(client))
				state.feasibleWidth = candidate.middlePairWidth,
				state.feasibleHeight = candidate.middlePairHeight;
			else
				state.feasibleWidth = candidate.exitWidth,
				state.feasibleHeight = candidate.exitHeight;
			WriteConfiguration(candidate);
			// 拖动期间固定 backing 仅直移 HWND，释放后才重新绘制该组。
			for (const auto moved : Clients)
			{
			if ((movedMask & Inkeys::UI::RenderPipeline::Mask(moved)) == 0) continue;
				const auto movedLayout = ResolveControlLayout(ControlFor(moved), monitor,
					candidate, presentationVisible.load(std::memory_order_acquire),
					state.dragDpiScale);
				(void)Inkeys::Window::GetService().SetBounds(RoleFor(moved),
					movedLayout.expanded);
				auto& movedState = states[Index(moved)];
				movedState.currentLeft = static_cast<float>(movedLayout.expanded.left);
				movedState.currentTop = static_cast<float>(movedLayout.expanded.top);
				movedState.startLeft = movedState.targetLeft = movedState.currentLeft;
				movedState.startTop = movedState.targetTop = movedState.currentTop;
				movedState.startWidth = movedState.currentWidth;
				movedState.startHeight = movedState.currentHeight;
				movedState.startScale = movedState.currentScale;
				movedState.geometryStarted = std::chrono::steady_clock::now();
			}
		}

		void Enqueue(RenderClient client, PointerEvent event)
		{
			auto& state = states[Index(client)];
			{
				std::scoped_lock lock(state.inputMutex);
				if (event.action == PointerAction::Move && !state.input.empty() &&
					state.input.back().action == PointerAction::Move)
					state.input.back() = event;
				else
					state.input.push_back(event);
			}
			Inkeys::UI::RenderPipeline::Request(client);
		}

		struct InputOutcome
		{
			bool changed = false;
			bool directMoved = false;
			bool dragReleased = false;
		};

		[[nodiscard]] InputOutcome DrainInput(RenderClient client, ClientState& state,
			const Layout& layout)
		{
			std::deque<PointerEvent> events;
			{
				std::scoped_lock lock(state.inputMutex);
				events.swap(state.input);
			}
			InputOutcome outcome;
			for (const auto& event : events)
			{
				outcome.changed = true;
				if (event.action == PointerAction::Wheel)
				{
					Invoke(event.wheel < 0 ? HitTarget::Next : HitTarget::Previous);
					continue;
				}

				const HitTarget hit = HitTest(client, event.local, layout);
				if (event.action == PointerAction::Move)
				{
					state.hover = hit;
					if ((state.pressed == HitTarget::Previous ||
						state.pressed == HitTarget::Next) && hit != state.pressed)
						state.pressed = HitTarget::None;
					if (state.dragging)
					{
						UpdateDrag(client, state, event.screen);
						outcome.directMoved = true;
					}
				}
				else if (event.action == PointerAction::Leave)
				{
					state.hover = HitTarget::None;
				}
				else if (event.action == PointerAction::Down)
				{
					(void)Inkeys::Window::GetService().PromotePptWindow(RoleFor(client));
					state.pressed = hit;
					state.pressStarted = state.lastRepeat = std::chrono::steady_clock::now();
					if (hit == HitTarget::Previous || hit == HitTarget::Next)
					{
						Invoke(hit);
					}
					else if (hit == HitTarget::Drag || hit == HitTarget::Page)
					{
						const auto snapshot = ReadConfiguration();
						state.dragging = true;
						groupDragging[DragGroup(client)].store(true,
							std::memory_order_release);
						const auto pausedAt = std::chrono::steady_clock::now();
						for (const auto moved : Clients)
						{
							if ((PairMask(client) & Inkeys::UI::RenderPipeline::Mask(moved)) == 0)
								continue;
							auto& movedState = states[Index(moved)];
							movedState.startLeft = movedState.currentLeft;
							movedState.startTop = movedState.currentTop;
							movedState.startWidth = movedState.currentWidth;
							movedState.startHeight = movedState.currentHeight;
							movedState.startScale = movedState.currentScale;
							movedState.geometryStarted = pausedAt;
						}
						state.dragStart = event.screen;
						const auto display = ReadPrimaryDisplayLayout(client);
						state.dragMonitor = display.bounds;
						state.dragDpiScale = display.dpiScale;
						state.dragStartWidth = IsBottom(client) ? snapshot.bottomPairWidth
							: IsMiddle(client) ? snapshot.middlePairWidth
							: snapshot.exitWidth;
						state.dragStartHeight = IsBottom(client) ? snapshot.bottomPairHeight
							: IsMiddle(client) ? snapshot.middlePairHeight
							: snapshot.exitHeight;
						state.feasibleWidth = state.dragStartWidth;
						state.feasibleHeight = state.dragStartHeight;
					}
					else
						groupDragging[DragGroup(client)].store(false,
							std::memory_order_release);
				}
				else if (event.action == PointerAction::Up ||
					event.action == PointerAction::Cancel)
				{
					const auto pressed = std::exchange(state.pressed, HitTarget::None);
					if (event.action == PointerAction::Up && pressed == HitTarget::EndShow &&
						hit == HitTarget::EndShow)
						Invoke(HitTarget::EndShow);
					if (state.dragging)
					{
						const LONG dx = event.screen.x - state.dragStart.x;
						const LONG dy = event.screen.y - state.dragStart.y;
						if (event.action == PointerAction::Up && pressed == HitTarget::Page &&
							dx * dx + dy * dy <= 400)
							Invoke(HitTarget::Page);
						state.dragging = false;
						const auto resumedAt = std::chrono::steady_clock::now();
						for (const auto moved : Clients)
						{
							if ((PairMask(client) & Inkeys::UI::RenderPipeline::Mask(moved)) == 0)
								continue;
							auto& movedState = states[Index(moved)];
							movedState.startLeft = movedState.currentLeft;
							movedState.startTop = movedState.currentTop;
							movedState.startWidth = movedState.currentWidth;
							movedState.startHeight = movedState.currentHeight;
							movedState.startScale = movedState.currentScale;
							movedState.geometryStarted = resumedAt;
						}
						groupDragging[DragGroup(client)].store(false,
							std::memory_order_release);
						outcome.dragReleased = true;
						Inkeys::UI::RenderPipeline::Request(PairMask(client));
						const auto configurationSnapshot = ReadConfiguration();
						if (configurationSnapshot.rememberPosition)
							PersistPosition(configurationSnapshot);
					}
					else
						groupDragging[DragGroup(client)].store(false,
							std::memory_order_release);
				}
			}

			if (state.pressed == HitTarget::Previous || state.pressed == HitTarget::Next)
			{
				const auto now = std::chrono::steady_clock::now();
				if (state.hover == state.pressed &&
					Inkeys::config.PlugIn.PPTHelper.Tentative.EnablePageButtonLongPress &&
					now - state.pressStarted >= 400ms && now - state.lastRepeat >= 15ms)
				{
					Invoke(state.pressed);
					state.lastRepeat = now;
					outcome.changed = true;
				}
			}
			return outcome;
		}

		[[nodiscard]] bool IsLocalTargetLost(HRESULT hr) noexcept
		{
			return hr == D2DERR_RECREATE_TARGET;
		}

		[[nodiscard]] bool IsSharedDeviceLost(HRESULT hr) noexcept
		{
			return hr == DXGI_ERROR_DEVICE_REMOVED ||
				hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		[[nodiscard]] HRESULT EnsureResources(TargetResources& target,
			const FrameContext& frameContext, const Layout& layout)
		{
			const auto& epoch = frameContext.epoch;
			const auto& assets = frameContext.assets;
			if (target.context && target.bitmap && target.gdi && target.brush &&
				target.dragHandleStyle &&
				target.generation == epoch.generation &&
				target.width >= layout.capacityWidth &&
				target.height >= layout.capacityHeight) return S_OK;
			if (!epoch.d2dDevice || !assets.d2dFactory || !assets.dwriteFactory)
				return E_POINTER;

			TargetResources next;
			HRESULT hr = epoch.d2dDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &next.context);
			if (FAILED(hr)) return hr;
			const auto properties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED));
			hr = next.context->CreateBitmap(D2D1::SizeU(layout.capacityWidth,
				layout.capacityHeight),
				nullptr, 0, &properties, &next.bitmap);
			if (FAILED(hr)) return hr;
			hr = next.context.As(&next.gdi);
			if (FAILED(hr)) return hr;
			next.context->SetTarget(next.bitmap.Get());
			next.context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			hr = next.context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black),
				&next.brush);
			if (FAILED(hr)) return hr;
			D2D1_STROKE_STYLE_PROPERTIES strokeProperties = D2D1::StrokeStyleProperties();
			strokeProperties.startCap = D2D1_CAP_STYLE_ROUND;
			strokeProperties.endCap = D2D1_CAP_STYLE_ROUND;
			hr = assets.d2dFactory->CreateStrokeStyle(&strokeProperties, nullptr, 0,
				&next.dragHandleStyle);
			if (FAILED(hr)) return hr;
			hr = assets.dwriteFactory->CreateTextFormat(L"HarmonyOS Sans SC",
				assets.fontCollection.Get(),
				DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL, 24.0F, L"zh-CN",
				&next.pageFormat);
			if (FAILED(hr)) return hr;
			hr = assets.dwriteFactory->CreateTextFormat(L"HarmonyOS Sans SC",
				assets.fontCollection.Get(),
				DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL, 16.0F, L"zh-CN",
				&next.totalFormat);
			if (FAILED(hr)) return hr;
			for (auto* format : { next.pageFormat.Get(), next.totalFormat.Get() })
			{
				format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			}
			if (!iconsReady) return E_UNEXPECTED;
			const D2D1_BITMAP_PROPERTIES1 iconProperties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_NONE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED));
			for (std::size_t index = 1; index < next.icons.size(); ++index)
			{
				const auto& surface = iconSurfaces[index];
				if (surface.empty()) return E_UNEXPECTED;
				const auto pixels = surface.pixels();
				hr = next.context->CreateBitmap(
					D2D1::SizeU(static_cast<UINT32>(surface.width()),
						static_cast<UINT32>(surface.height())), pixels.data(),
					static_cast<UINT32>(surface.width() * sizeof(Inkeys::Graphics::DibSurface::Pixel)),
					&iconProperties, &next.icons[index]);
				if (FAILED(hr)) return hr;
			}
			next.generation = epoch.generation;
			next.width = layout.capacityWidth;
			next.height = layout.capacityHeight;
			target.Reset();
			target = std::move(next);
			return S_OK;
		}

		void SetBrush(TargetResources& target, const D2D1_COLOR_F& color)
		{
			target.brush->SetColor(color);
		}

		void DrawButton(TargetResources& target, const D2D1_RECT_F& rect,
			HitTarget button, const ClientState& state, float radius)
		{
			D2D1_COLOR_F fill = D2D1::ColorF(250.0F / 255.0F, 250.0F / 255.0F,
				250.0F / 255.0F, 160.0F / 255.0F);
			const auto now = std::chrono::steady_clock::now().time_since_epoch();
			const auto nowTicks = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
			const bool keyboardPressed = (button == HitTarget::Previous || button == HitTarget::Next) &&
				keyboardFlashUntil.load(std::memory_order_acquire) > nowTicks &&
				keyboardFlashNext.load(std::memory_order_acquire) == (button == HitTarget::Next);
			if (state.pressed == button || keyboardPressed)
				fill = D2D1::ColorF(200.0F / 255.0F, 200.0F / 255.0F,
					200.0F / 255.0F, 1.0F);
			else if (state.hover == button)
				fill = D2D1::ColorF(225.0F / 255.0F, 225.0F / 255.0F,
					225.0F / 255.0F, 1.0F);
			const auto rounded = D2D1::RoundedRect(rect, radius, radius);
			SetBrush(target, fill);
			target.context->FillRoundedRectangle(rounded, target.brush.Get());
			SetBrush(target, D2D1::ColorF(200.0F / 255.0F, 200.0F / 255.0F,
				200.0F / 255.0F, 160.0F / 255.0F));
			target.context->DrawRoundedRectangle(rounded, target.brush.Get(), 1.0F);
		}

		void DrawChevron(TargetResources& target, D2D1_POINT_2F center,
			float size, bool next, bool vertical)
		{
			SetBrush(target, D2D1::ColorF(50.0F / 255.0F, 50.0F / 255.0F,
				50.0F / 255.0F, 1.0F));
			D2D1_POINT_2F a{}, b{}, c{};
			if (!vertical)
			{
				const float sign = next ? 1.0F : -1.0F;
				a = { center.x - sign * size * 0.35F, center.y - size * 0.45F };
				b = { center.x + sign * size * 0.35F, center.y };
				c = { center.x - sign * size * 0.35F, center.y + size * 0.45F };
			}
			else
			{
				const float sign = next ? 1.0F : -1.0F;
				a = { center.x - size * 0.45F, center.y - sign * size * 0.35F };
				b = { center.x, center.y + sign * size * 0.35F };
				c = { center.x + size * 0.45F, center.y - sign * size * 0.35F };
			}
			target.context->DrawLine(a, b, target.brush.Get(), 3.0F);
			target.context->DrawLine(b, c, target.brush.Get(), 3.0F);
		}

		void DrawIcon(TargetResources& target, std::size_t icon,
			const D2D1_RECT_F& bounds, float opacity = 1.0F)
		{
			if (icon >= target.icons.size() || !target.icons[icon]) return;
			target.context->DrawBitmap(target.icons[icon].Get(), bounds,
				std::clamp(opacity, 0.0F, 1.0F),
				D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		}

		void DrawLogicalText(TargetResources& target, const std::wstring& text,
			IDWriteTextFormat* format, const VisualRect& bounds, float scale)
		{
			if (!format || scale <= 0.0F) return;
			D2D1_MATRIX_3X2_F original{};
			target.context->GetTransform(&original);
			// 文本格式保持 96-DPI 逻辑字号，当前插值 scale 只作为绘制变换。
			target.context->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
			target.context->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()),
				format, D2D1::RectF(bounds.left, bounds.top, bounds.right, bounds.bottom),
				target.brush.Get());
			target.context->SetTransform(original);
		}

		[[nodiscard]] D2D1_RECT_F ScaleRect(const VisualRect& rect, float scale) noexcept
		{
			return D2D1::RectF(rect.left * scale, rect.top * scale,
				rect.right * scale, rect.bottom * scale);
		}

		void DrawDragHandle(TargetResources& target, const VisualLine& line,
			float scale)
		{
			// 拖动柄沿用旧版深灰色、2px 圆头线条。
			SetBrush(target, D2D1::ColorF(60.0F / 255.0F, 60.0F / 255.0F,
				60.0F / 255.0F, 250.0F / 255.0F));
			target.context->DrawLine(D2D1::Point2F(line.x1 * scale, line.y1 * scale),
				D2D1::Point2F(line.x2 * scale, line.y2 * scale), target.brush.Get(),
				2.0F * scale, target.dragHandleStyle.Get());
		}

		void DrawPageControl(RenderClient client, ClientState& state,
			TargetResources& target, const Layout& layout)
		{
			const float s = layout.scale;
			const auto geometry = ResolveControlVisualGeometry(ControlFor(client));
			const auto outer = D2D1::RoundedRect(D2D1::RectF(0.5F, 0.5F,
				static_cast<float>(layout.width) - 0.5F,
				static_cast<float>(layout.height) - 0.5F), 15.0F * s, 15.0F * s);
			SetBrush(target, D2D1::ColorF(225.0F / 255.0F, 225.0F / 255.0F,
				225.0F / 255.0F, 160.0F / 255.0F));
			target.context->FillRoundedRectangle(outer, target.brush.Get());
			SetBrush(target, D2D1::ColorF(200.0F / 255.0F, 200.0F / 255.0F,
				200.0F / 255.0F, 160.0F / 255.0F));
			target.context->DrawRoundedRectangle(outer, target.brush.Get(), s);
			DrawDragHandle(target, geometry.dragHandle, s);

			if (IsBottom(client))
			{
				const auto previous = ScaleRect(geometry.previous, s);
				const auto next = ScaleRect(geometry.next, s);
				DrawButton(target, previous, HitTarget::Previous, state, 17.5F * s);
				DrawButton(target, next, HitTarget::Next, state, 17.5F * s);
				DrawIcon(target, 1, D2D1::RectF(previous.left + 5.0F * s,
					previous.top + 5.0F * s, previous.right - 5.0F * s,
					previous.bottom - 5.0F * s));
				const std::size_t nextIcon = currentPage.load(std::memory_order_acquire) < 0
					? 3 : 2;
				DrawIcon(target, nextIcon, D2D1::RectF(next.left + 5.0F * s,
					next.top + 5.0F * s, next.right - 5.0F * s,
					next.bottom - 5.0F * s));
				const auto text = ResolvePageText(ControlFor(client),
					currentPage.load(std::memory_order_acquire),
					totalPage.load(std::memory_order_acquire));
				SetBrush(target, D2D1::ColorF(30.0F / 255.0F, 30.0F / 255.0F,
					30.0F / 255.0F, 1.0F));
				DrawLogicalText(target, text.current, target.pageFormat.Get(),
					geometry.currentPage, s);
				SetBrush(target, D2D1::ColorF(60.0F / 255.0F, 60.0F / 255.0F,
					60.0F / 255.0F, 1.0F));
				DrawLogicalText(target, text.total, target.totalFormat.Get(),
					geometry.totalPage, s);
			}
			else
			{
				const auto previous = ScaleRect(geometry.previous, s);
				const auto next = ScaleRect(geometry.next, s);
				DrawButton(target, previous, HitTarget::Previous, state, 17.5F * s);
				DrawButton(target, next, HitTarget::Next, state, 17.5F * s);
				DrawIcon(target, 4, D2D1::RectF(previous.left + 5.0F * s,
					previous.top + 5.0F * s, previous.right - 5.0F * s,
					previous.bottom - 5.0F * s));
				const std::size_t nextIcon = currentPage.load(std::memory_order_acquire) < 0
					? 3 : 5;
				DrawIcon(target, nextIcon, D2D1::RectF(next.left + 5.0F * s,
					next.top + 5.0F * s, next.right - 5.0F * s,
					next.bottom - 5.0F * s));
				const auto text = ResolvePageText(ControlFor(client),
					currentPage.load(std::memory_order_acquire),
					totalPage.load(std::memory_order_acquire));
				SetBrush(target, D2D1::ColorF(30.0F / 255.0F, 30.0F / 255.0F,
					30.0F / 255.0F, 1.0F));
				DrawLogicalText(target, text.current, target.pageFormat.Get(),
					geometry.currentPage, s);
				SetBrush(target, D2D1::ColorF(60.0F / 255.0F, 60.0F / 255.0F,
					60.0F / 255.0F, 1.0F));
				DrawLogicalText(target, text.total, target.totalFormat.Get(),
					geometry.totalPage, s);
			}
		}

		void DrawExitControl(ClientState& state, TargetResources& target,
			const Layout& layout)
		{
			const float s = layout.scale;
			const auto geometry = ResolveControlVisualGeometry(Control::ExitShow);
			const auto outer = D2D1::RoundedRect(D2D1::RectF(0.5F, 0.5F,
				static_cast<float>(layout.width) - 0.5F,
				static_cast<float>(layout.height) - 0.5F), 15.0F * s, 15.0F * s);
			SetBrush(target, D2D1::ColorF(225.0F / 255.0F, 225.0F / 255.0F,
				225.0F / 255.0F, 160.0F / 255.0F));
			target.context->FillRoundedRectangle(outer, target.brush.Get());
			DrawDragHandle(target, geometry.dragHandle, s);
			const auto button = ScaleRect(geometry.action, s);
			DrawButton(target, button, HitTarget::EndShow, state, 17.5F * s);
			DrawIcon(target, 3, D2D1::RectF(20.0F * s, 10.0F * s,
				60.0F * s, 50.0F * s));
		}

		[[nodiscard]] RECT VisualBounds(RenderClient client, HitTarget target,
			const Layout& layout) noexcept
		{
			const float s = layout.scale;
			if (client == RenderClient::PptExitShow)
				return target == HitTarget::EndShow
					? RECT{ static_cast<LONG>(10.0F * s), 0,
						static_cast<LONG>(70.0F * s), static_cast<LONG>(60.0F * s) }
					: RECT{};
			if (IsBottom(client))
			{
				const float inset = (IsLeft(client) ? 15.0F : 5.0F) * s;
				if (target == HitTarget::Previous)
					return RECT{ static_cast<LONG>(inset), 0,
						static_cast<LONG>(inset + 55.0F * s), static_cast<LONG>(60.0F * s) };
				if (target == HitTarget::Next)
					return RECT{ static_cast<LONG>(inset + 120.0F * s), 0,
						static_cast<LONG>(inset + 180.0F * s), static_cast<LONG>(60.0F * s) };
				if (target == HitTarget::Page)
					return RECT{ static_cast<LONG>(inset + 50.0F * s), 0,
						static_cast<LONG>(inset + 125.0F * s), static_cast<LONG>(60.0F * s) };
			}
			else
			{
				if (target == HitTarget::Previous)
					return RECT{ 0, static_cast<LONG>(10.0F * s),
						static_cast<LONG>(60.0F * s), static_cast<LONG>(70.0F * s) };
				if (target == HitTarget::Next)
					return RECT{ 0, static_cast<LONG>(120.0F * s),
						static_cast<LONG>(60.0F * s), static_cast<LONG>(180.0F * s) };
				if (target == HitTarget::Page)
					return RECT{ 0, static_cast<LONG>(65.0F * s),
						static_cast<LONG>(60.0F * s), static_cast<LONG>(125.0F * s) };
			}
			return RECT{};
		}

		void UnionDamage(RECT& damage, const RECT& next) noexcept
		{
			if (next.right <= next.left || next.bottom <= next.top) return;
			if (damage.right <= damage.left || damage.bottom <= damage.top)
				damage = next;
			else
			{
				damage.left = (std::min)(damage.left, next.left);
				damage.top = (std::min)(damage.top, next.top);
				damage.right = (std::max)(damage.right, next.right);
				damage.bottom = (std::max)(damage.bottom, next.bottom);
			}
		}

		[[nodiscard]] FrameResult RenderFrame(
			RenderClient client, const FrameContext& frameContext)
		{
			auto& state = states[Index(client)];
			const Layout layout = CalculateLayout(client, state, frameContext.frameTime);
			const auto input = DrainInput(client, state, layout);
			// 成对控件共享拖拽门闩，直移期间配对窗口也不得进入 D2D/ULW。
			if (groupDragging[DragGroup(client)].load(std::memory_order_acquire) &&
				!input.dragReleased)
				return FrameResult::Idle;
			const auto nowTicks = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			const bool keyboardFlash = client != RenderClient::PptExitShow &&
				keyboardFlashUntil.load(std::memory_order_acquire) > nowTicks;
			const bool active = input.changed || state.dragging || keyboardFlash ||
				layout.animationActive ||
				state.pressed == HitTarget::Previous || state.pressed == HitTarget::Next;

			const auto role = RoleFor(client);
			auto& service = Inkeys::Window::GetService();
			if (!layout.visible)
			{
				if (state.shown)
				{
					(void)service.Hide(role);
					state.shown = false;
				}
				return FrameResult::Idle;
			}
			if (!state.shown)
			{
				(void)service.Show(role);
				state.shown = true;
			}

			// 唯一管线线程串行覆盖目标重建、绘制和 ULW 提交。
			const auto& epoch = frameContext.epoch;
			const bool targetChanged = state.target.generation != epoch.generation ||
				state.target.width < layout.capacityWidth ||
				state.target.height < layout.capacityHeight;
			const HRESULT resourceHr = EnsureResources(state.target, frameContext, layout);
			if (FAILED(resourceHr))
			{
				state.committedGeneration = 0;
				if (IsLocalTargetLost(resourceHr)) state.target.Reset();
				return IsSharedDeviceLost(resourceHr)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}

			const bool debug = debugEnabled.load(std::memory_order_acquire);
			const int pageNow = currentPage.load(std::memory_order_acquire);
			const int totalNow = totalPage.load(std::memory_order_acquire);
			const bool windowMoved = !EqualRect(&state.committedScreen, &layout.screen);
			bool requireFull = targetChanged || state.committedGeneration == 0 ||
				windowMoved || state.committedDebug != debug ||
				std::abs(state.committedOpacity - layout.opacity) > 0.001F;
			RECT changedVisual{};
			if (state.committedHover != state.hover)
			{
				UnionDamage(changedVisual, VisualBounds(client, state.committedHover, layout));
				UnionDamage(changedVisual, VisualBounds(client, state.hover, layout));
			}
			if (state.committedPressed != state.pressed)
			{
				UnionDamage(changedVisual, VisualBounds(client, state.committedPressed, layout));
				UnionDamage(changedVisual, VisualBounds(client, state.pressed, layout));
			}
			if (client != RenderClient::PptExitShow &&
				(state.committedCurrentPage != pageNow || state.committedTotalPage != totalNow))
				UnionDamage(changedVisual, VisualBounds(client, HitTarget::Page, layout));
			if (keyboardFlash) requireFull = true;
			if (input.changed && changedVisual.right <= changedVisual.left)
				UnionDamage(changedVisual, VisualBounds(client, state.pressed, layout));
			const SIZE backing{ static_cast<LONG>(layout.width),
				static_cast<LONG>(layout.height) };
			const auto diagnostic = ResolvePptDiagnosticDamage(backing, changedVisual,
				state.committedVisual, requireFull, debug, active,
				state.finalDebugFrame);
			const RECT dirty = diagnostic.damage;

			auto* context = state.target.context.Get();
			context->BeginDraw();
			context->PushAxisAlignedClip(D2D1::RectF(
				static_cast<float>(dirty.left), static_cast<float>(dirty.top),
				static_cast<float>(dirty.right), static_cast<float>(dirty.bottom)),
				D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			context->Clear(D2D1::ColorF(0, 0.0F));
			context->PushLayer(D2D1::LayerParameters(
				D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
				D2D1::IdentityMatrix(), layout.opacity), nullptr);
			if (client == RenderClient::PptExitShow)
				DrawExitControl(state, state.target, layout);
			else
				DrawPageControl(client, state, state.target, layout);

			context->PopLayer();
			if (debug)
			{
				if (diagnostic.drawActive || diagnostic.drawFinal)
				{
					SetBrush(state.target, diagnostic.drawActive
					? D2D1::ColorF(D2D1::ColorF::Red)
					: D2D1::ColorF(D2D1::ColorF::LimeGreen));
					context->DrawRectangle(D2D1::RectF(
						static_cast<float>(dirty.left) + 1.0F,
						static_cast<float>(dirty.top) + 1.0F,
						static_cast<float>(dirty.right) - 1.0F,
						static_cast<float>(dirty.bottom) - 1.0F),
						state.target.brush.Get(), 2.0F);
				}
				SetBrush(state.target, D2D1::ColorF(D2D1::ColorF::Blue));
				context->DrawRectangle(D2D1::RectF(0.5F, 0.5F,
					static_cast<float>(layout.width) - 0.5F,
					static_cast<float>(layout.height) - 0.5F), state.target.brush.Get(), 1.0F);
			}
			context->PopAxisAlignedClip();

			HDC hdc = nullptr;
			HRESULT getDcHr = state.target.gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &hdc);
			BOOL presented = FALSE;
			DWORD presentError = ERROR_SUCCESS;
			HRESULT releaseDcHr = E_FAIL;
			if (SUCCEEDED(getDcHr) && hdc)
			{
				POINT destination{ layout.screen.left, layout.screen.top };
				POINT source{};
				SIZE size{ static_cast<LONG>(layout.width), static_cast<LONG>(layout.height) };
				BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
				UPDATELAYEREDWINDOWINFO info{};
				info.cbSize = sizeof(info);
				info.pptDst = &destination;
				info.psize = &size;
				info.pptSrc = &source;
				info.hdcSrc = hdc;
				info.pblend = &blend;
				info.dwFlags = ULW_ALPHA;
				info.prcDirty = &dirty;
				presented = UpdateLayeredWindowIndirect(service.Handle(role), &info);
				if (!presented) presentError = GetLastError();
				releaseDcHr = state.target.gdi->ReleaseDC(nullptr);
			}
			const HRESULT endDrawHr = context->EndDraw();
			if (FAILED(getDcHr) || FAILED(releaseDcHr) || FAILED(endDrawHr) || !presented)
			{
				state.committedGeneration = 0;
				if (IsLocalTargetLost(getDcHr) || IsLocalTargetLost(releaseDcHr) ||
					IsLocalTargetLost(endDrawHr))
				{
					state.target.Reset();
					return FrameResult::Retry;
				}
				if (IsSharedDeviceLost(getDcHr) || IsSharedDeviceLost(releaseDcHr) ||
					IsSharedDeviceLost(endDrawHr))
				{
					state.target.Reset();
					return FrameResult::DeviceLost;
				}
				(void)presentError;
				return FrameResult::Retry;
			}
			state.committedScreen = layout.screen;
			state.committedVisual = (diagnostic.drawActive || diagnostic.drawFinal)
				? dirty : RECT{};
			state.committedHover = state.hover;
			state.committedPressed = state.pressed;
			state.committedCurrentPage = pageNow;
			state.committedTotalPage = totalNow;
			state.committedDebug = debug;
			state.committedDebugActive = active;
			state.committedOpacity = layout.opacity;
			state.committedGeneration = epoch.generation;
			state.committedWidth = layout.width;
			state.committedHeight = layout.height;

			state.finalDebugFrame = diagnostic.keepFinalFrame;
			return active ? FrameResult::Continue : FrameResult::Idle;
		}

		void QueueMouse(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			const auto role = Inkeys::Window::RoleFromHandle(hwnd);
			if (!IsPptRole(role)) return;
			PointerEvent event;
			bool releaseAfterEnqueue = false;
			event.local = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			event.screen = event.local;
			switch (message)
			{
			case WM_MOUSEMOVE:
				event.action = PointerAction::Move;
				ClientToScreen(hwnd, &event.screen);
				break;
			case WM_LBUTTONDOWN:
				event.action = PointerAction::Down;
				ClientToScreen(hwnd, &event.screen);
				SetCapture(hwnd);
				break;
			case WM_LBUTTONUP:
				event.action = PointerAction::Up;
				ClientToScreen(hwnd, &event.screen);
				releaseAfterEnqueue = GetCapture() == hwnd;
				break;
			case WM_MOUSELEAVE: event.action = PointerAction::Leave; break;
			case WM_MOUSEWHEEL:
				event.action = PointerAction::Wheel;
				event.wheel = GET_WHEEL_DELTA_WPARAM(wParam);
				event.screen = event.local;
				ScreenToClient(hwnd, &event.local);
				break;
			case WM_CANCELMODE:
			case WM_CAPTURECHANGED:
				event.action = PointerAction::Cancel;
				if (message == WM_CANCELMODE && GetCapture() == hwnd)
					releaseAfterEnqueue = true;
				break;
			default: return;
			}
			Enqueue(ClientFor(role), event);
			if (releaseAfterEnqueue) ReleaseCapture();
		}

		LRESULT CALLBACK PptWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			const auto role = Inkeys::Window::RoleFromHandle(hwnd);
			if (message == WM_TABLET_QUERYSYSTEMGESTURESTATUS)
				return TABLET_DISABLE_PRESSANDHOLD | TABLET_DISABLE_PENTAPFEEDBACK |
					TABLET_DISABLE_PENBARRELFEEDBACK | TABLET_DISABLE_FLICKS;
			if (message == WM_TOUCH && IsPptRole(role))
			{
				const UINT count = LOWORD(wParam);
				std::vector<TOUCHINPUT> input(count);
				if (GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam), count,
					input.data(), sizeof(TOUCHINPUT)))
				{
					auto& touch = touches[Index(role)];
					for (const auto& item : input)
					{
						const bool primary = (item.dwFlags & TOUCHEVENTF_PRIMARY) != 0;
						POINT screen{ TOUCH_COORD_TO_PIXEL(item.x),
							TOUCH_COORD_TO_PIXEL(item.y) };
						POINT local = screen;
						ScreenToClient(hwnd, &local);
					if ((item.dwFlags & TOUCHEVENTF_DOWN) && !touch.active)
					{
						touch = { item.dwID, true, primary, local };
						SetCapture(hwnd);
							Enqueue(ClientFor(role), { PointerAction::Down, local, screen, 0 });
						}
						else if (touch.active && touch.id == item.dwID &&
							(item.dwFlags & TOUCHEVENTF_MOVE))
						{
							touch.last = local;
							Enqueue(ClientFor(role), { PointerAction::Move, local, screen, 0 });
						}
						else if (touch.active && touch.id == item.dwID &&
							(item.dwFlags & TOUCHEVENTF_UP))
						{
							touch = {};
							Enqueue(ClientFor(role), { PointerAction::Up, local, screen, 0 });
							if (GetCapture() == hwnd) ReleaseCapture();
						}
					}
				}
				CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
				return 0;
			}
			if (IsPptRole(role) && (message == WM_DPICHANGED ||
				message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE))
			{
				const auto client = ClientFor(role);
				if (message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE ||
					message == WM_DPICHANGED)
					(void)Inkeys::Display::Refresh(message == WM_DISPLAYCHANGE
						? Inkeys::Display::ChangeReason::Display
						: Inkeys::Display::ChangeReason::Settings);
				const UINT dpi = message == WM_DPICHANGED && LOWORD(wParam)
					? LOWORD(wParam) : QueryWindowDpi(hwnd);
				// 消息本身也是布局代次；即使 DPI 仍为 96，也需重算显示器位置。
				if (message == WM_DPICHANGED)
					for (auto& token : dpiTokens)
						token.store(dpi, std::memory_order_release);
				else
					dpiTokens[Index(client)].store(dpi, std::memory_order_release);
				Inkeys::UI::RenderPipeline::Request(
					message == WM_DPICHANGED
						? Inkeys::UI::RenderPipeline::PptMask()
						: Inkeys::UI::RenderPipeline::Mask(client));
				return 0;
			}
			if (message == WM_MOUSEMOVE)
			{
				TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&tracking);
			}
			if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN ||
				message == WM_LBUTTONUP || message == WM_MOUSELEAVE ||
				message == WM_MOUSEWHEEL || message == WM_CANCELMODE ||
				message == WM_CAPTURECHANGED)
			{
				// WM_TOUCH 已自行翻译，系统补发的兼容鼠标不能再次入队。
				if ((GetMessageExtraInfo() & 0xFFFFFF80ULL) == 0xFF515700ULL) return 0;
				QueueMouse(hwnd, message, wParam, lParam);
				return 0;
			}
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}

	bool Initialize(BusinessCallbacks callbacks)
	{
		if (initialized.exchange(true, std::memory_order_acq_rel)) return false;
		WriteConfiguration(SnapshotLegacyConfiguration());
		for (std::size_t index = 0; index < Roles.size(); ++index)
		{
			const HWND hwnd = Inkeys::Window::GetService().Handle(Roles[index]);
			dpiTokens[index].store(QueryWindowDpi(hwnd), std::memory_order_release);
		}
		iconsReady = true;
		for (std::size_t index = 1; index < iconSurfaces.size(); ++index)
		{
			const wchar_t* resource = index == 1 ? L"ppt1" : index == 2 ? L"ppt2" :
				index == 3 ? L"ppt3" : index == 4 ? L"ppt4" : L"ppt5";
			if (!LoadSurfaceFromResource(&iconSurfaces[index], L"PNG", resource))
			{
				iconsReady = false;
				break;
			}
			RecolorSurface(iconSurfaces[index], RGB(50, 50, 50));
		}
		if (!iconsReady)
		{
			Shutdown();
			return false;
		}
		{
			std::scoped_lock lock(callbackMutex);
			business = std::move(callbacks);
		}
		using namespace Inkeys::UI::RenderPipeline;
		for (const auto client : Clients)
		{
			if (!Register(client, [client](const FrameContext& context)
				{ return RenderFrame(client, context); }))
			{
				Shutdown();
				return false;
			}
		}
		displaySubscription = Inkeys::Display::Subscribe(
			[](Inkeys::Display::SnapshotPtr snapshot)
			{
				const auto* monitor = snapshot ? snapshot->Primary() : nullptr;
				const UINT dpi = monitor && monitor->effectiveDpiX
					? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI;
				for (auto& token : dpiTokens)
					token.store(dpi, std::memory_order_release);
				if (initialized.load(std::memory_order_acquire))
					Inkeys::UI::RenderPipeline::Request(
						Inkeys::UI::RenderPipeline::PptMask());
			});
		return true;
	}

	void Shutdown() noexcept
	{
		if (!initialized.exchange(false, std::memory_order_acq_rel)) return;
		displaySubscription.Reset();
		for (const auto role : Roles)
		{
			const HWND hwnd = Inkeys::Window::GetService().Handle(role);
			DWORD_PTR ignored = 0;
			// 有界同步取消确保窗口线程先释放 capture，退出不能无限等待消息泵。
			if (hwnd) (void)SendMessageTimeoutW(hwnd, WM_CANCELMODE, 0, 0,
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &ignored);
		}
		// Unregister 同步等待正在执行的回调退出，之后才可释放每窗 D2D target。
		for (const auto client : Clients)
			Inkeys::UI::RenderPipeline::Unregister(client);
		for (auto& state : states)
		{
			std::scoped_lock lock(state.inputMutex);
			state.input.clear();
			state.target.Reset();
			state.hover = HitTarget::None;
			state.pressed = HitTarget::None;
			state.dragging = false;
			state.shown = false;
			state.finalDebugFrame = false;
			state.animationInitialized = false;
			state.currentLeft = 0.0F;
			state.currentTop = 0.0F;
			state.currentWidth = 1.0F;
			state.currentHeight = 1.0F;
			state.currentScale = 1.0F;
			state.currentOpacity = 0.0F;
			state.startLeft = state.targetLeft = 0.0F;
			state.startTop = state.targetTop = 0.0F;
			state.startWidth = state.targetWidth = 1.0F;
			state.startHeight = state.targetHeight = 1.0F;
			state.startScale = state.targetScale = 1.0F;
			state.geometryStarted = {};
			state.observedMonitor = {};
			state.observedDpiScale = 1.0F;
			state.displayGeometryInitialized = false;
			state.displayTransitionActive = false;
			state.dragStart = {};
			state.dragMonitor = {};
			state.dragDpiScale = 1.0F;
			state.dragStartWidth = 0.0F;
			state.dragStartHeight = 0.0F;
			state.feasibleWidth = 0.0F;
			state.feasibleHeight = 0.0F;
			state.committedScreen = {};
			state.committedVisual = {};
			state.committedHover = HitTarget::None;
			state.committedPressed = HitTarget::None;
			state.committedCurrentPage = -1;
			state.committedTotalPage = -1;
			state.committedDebug = false;
			state.committedDebugActive = false;
			state.committedOpacity = -1.0F;
			state.committedGeneration = 0;
			state.committedWidth = 0;
			state.committedHeight = 0;
			state.pressStarted = {};
			state.lastRepeat = {};
		}
		for (auto& dragging : groupDragging)
			dragging.store(false, std::memory_order_release);
		for (auto& touch : touches) touch = {};
		std::scoped_lock lock(callbackMutex);
		business = {};
	}

	WNDPROC WindowProc() noexcept
	{
		return PptWindowProc;
	}

	void PublishPresentationVisible(bool visible) noexcept
	{
		if (presentationVisible.exchange(visible, std::memory_order_acq_rel) == visible)
			return;
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::PptMask());
	}

	void PublishPageState(int current, int total) noexcept
	{
		const bool currentChanged =
			currentPage.exchange(current, std::memory_order_acq_rel) != current;
		const bool totalChanged =
			totalPage.exchange(total, std::memory_order_acq_rel) != total;
		const bool changed = currentChanged || totalChanged;
		if (changed)
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::PptPageMask());
	}

	void FlashPageDirection(bool next) noexcept
	{
		keyboardFlashNext.store(next, std::memory_order_release);
		const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(
			(std::chrono::steady_clock::now() + 100ms).time_since_epoch()).count();
		keyboardFlashUntil.store(until, std::memory_order_release);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::PptPageMask());
	}

	void NotifyConfigurationChanged(ConfigGroup group) noexcept
	{
		WriteConfiguration(SnapshotLegacyConfiguration());
		using Inkeys::UI::RenderPipeline::Mask;
		switch (group)
		{
		case ConfigGroup::BottomPair:
			Inkeys::UI::RenderPipeline::Request(Mask(RenderClient::PptBottomLeft) |
				Mask(RenderClient::PptBottomRight));
			break;
		case ConfigGroup::MiddlePair:
			Inkeys::UI::RenderPipeline::Request(Mask(RenderClient::PptMiddleLeft) |
				Mask(RenderClient::PptMiddleRight));
			break;
		case ConfigGroup::ExitShow:
			Inkeys::UI::RenderPipeline::Request(RenderClient::PptExitShow);
			break;
		case ConfigGroup::All:
		default:
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::PptMask());
			break;
		}
	}

	void QueueGlobalWheel(short delta) noexcept
	{
		Enqueue(RenderClient::PptBottomLeft,
			{ PointerAction::Wheel, {}, {}, delta });
	}

	void SetDebugEnabled(bool enabled) noexcept
	{
		if (debugEnabled.exchange(enabled, std::memory_order_acq_rel) == enabled) return;
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::PptMask());
	}
}
