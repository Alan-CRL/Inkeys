module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <dxgi.h>
#include <tpcshrd.h>
#include <wrl/client.h>

#include "../Bar/Bar.DirtyRegion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Inkeys.UI.PageControl;

import Inkeys.UI.Bar;
import Inkeys.UI.Bar.FramePacing;
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
		[[nodiscard]] constexpr PptPointerRegion PointerRegionForWidget(
			WidgetId widget) noexcept
		{
			if (widget == DragWidget) return PptPointerRegion::DragHandle;
			if (widget == PreviousWidget) return PptPointerRegion::Previous;
			if (widget == PageWidget) return PptPointerRegion::Page;
			if (widget == NextWidget) return PptPointerRegion::Next;
			return PptPointerRegion::Background;
		}
		[[nodiscard]] double LayoutTransitionDurationSeconds() noexcept
		{
			return BarButtonDefaultOperationDurationSeconds;
		}

		[[nodiscard]] std::chrono::milliseconds LayoutTransitionWallDuration() noexcept
		{
			return std::chrono::milliseconds(static_cast<long long>(std::lround(
				LayoutTransitionDurationSeconds() * 1000.0)));
		}

		[[nodiscard]] PptKeyboardRepeatTiming QueryPptKeyboardRepeatTiming() noexcept
		{
			UINT keyboardDelay = PptKeyboardDelayFallback;
			UINT keyboardSpeed = PptKeyboardSpeedFallback;
			if (!SystemParametersInfoW(
				SPI_GETKEYBOARDDELAY, 0, &keyboardDelay, 0))
				keyboardDelay = PptKeyboardDelayFallback;
			if (!SystemParametersInfoW(
				SPI_GETKEYBOARDSPEED, 0, &keyboardSpeed, 0))
				keyboardSpeed = PptKeyboardSpeedFallback;
			return ResolvePptKeyboardRepeatTiming(keyboardDelay, keyboardSpeed);
		}
		constexpr auto DragCandidateTraceInterval = 100ms;

		[[nodiscard]] const char* SurfaceName(std::size_t index) noexcept
		{
			constexpr std::array names{
				"bottom-left", "bottom-right", "middle-left", "middle-right" };
			return index < names.size() ? names[index] : "unknown";
		}

		[[nodiscard]] const char* PointerRegionName(
			PptPointerRegion region) noexcept
		{
			switch (region)
			{
			case PptPointerRegion::Background: return "background";
			case PptPointerRegion::DragHandle: return "drag-handle";
			case PptPointerRegion::Previous: return "previous";
			case PptPointerRegion::Page: return "page";
			case PptPointerRegion::Next: return "next";
			}
			return "unknown";
		}

		void TraceDrag(const char* format, ...) noexcept
		{
			char detail[768]{};
			va_list arguments;
			va_start(arguments, format);
			(void)_vsnprintf_s(detail, sizeof(detail), _TRUNCATE,
				format, arguments);
			va_end(arguments);
			char line[896]{};
			(void)sprintf_s(line, "[PageControlDrag][tid=%lu] %s\n",
				static_cast<unsigned long>(GetCurrentThreadId()), detail);
			(void)std::fputs(line, stdout);
			OutputDebugStringA(line);
		}

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
			RECT appliedSceneBounds{};
			float appliedSceneScale = 1.0F;
			bool appliedSceneBoundsReady = false;
			bool sceneConfigured = false;
			bool lightingSubscribed = false;
			bool targetVisible = false;
			bool inputLocked = true;
			bool borderCursorPointerInside = false;
			bool dragging = false;
			bool dragPending = false;
			bool pressedNext = false;
			DWORD touchId = 0;
			bool touchActive = false;
			bool touchPrimary = false;
			POINT touchLastClient{};
			POINT dragStartScreen{};
			PptLayoutState dragStartLayout{};
			PptLayoutState feasibleLayout{};
			std::chrono::steady_clock::time_point lastDragCandidateTrace{};
			std::uint64_t suppressedDirectCandidateTraces = 0;
			std::uint64_t suppressedPresentationBusyTraces = 0;
			std::chrono::steady_clock::time_point pressStarted{};
			std::chrono::steady_clock::time_point lastRepeat{};
			PptKeyboardRepeatTiming repeatTiming{};
			std::chrono::steady_clock::time_point layoutTransitionUntil{};
			std::uint64_t observedRevision = 0;
			SIZE backingCapacity{ 1, 1 };
			SIZE committedPresentationSize{};
			SIZE committedBackingCapacity{};
			std::uint64_t committedDeviceGeneration = 0;
			bool committedPresentationReady = false;
			bool forceFullPresentation = true;
			bool windowCommitFailureActive = false;
			RECT lastPresentedDebugFrameBounds{};
			RECT lastPresentedDebugWindowBounds{};
			Inkeys::UI::Bar::DebugFrameSleepLatch debugFrameSleepLatch;
			bool debugOverlayRefreshPending = false;
		};

		// 成功/锁忙属于鼠标热路径；有界采样，并在下一条日志累计省略数量。
		[[nodiscard]] bool ShouldTraceDragCandidate(
			SurfaceState& state,
			std::chrono::steady_clock::time_point now) noexcept
		{
			if (state.lastDragCandidateTrace.time_since_epoch().count() != 0
				&& now - state.lastDragCandidateTrace
					< DragCandidateTraceInterval)
				return false;
			state.lastDragCandidateTrace = now;
			return true;
		}

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
		std::mutex dragCommitMutex;
		std::array<PptDragCommitTracker, 2> dragCommitTrackers;
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

		[[nodiscard]] constexpr std::size_t DragPairIndex(
			std::size_t surfaceIndex) noexcept
		{
			return surfaceIndex < 2 ? 0 : 1;
		}

		[[nodiscard]] constexpr std::array<std::size_t, 2> DragPair(
			std::size_t pairIndex) noexcept
		{
			return pairIndex == 0
				? std::array<std::size_t, 2>{ 0, 1 }
				: std::array<std::size_t, 2>{ 2, 3 };
		}

		[[nodiscard]] constexpr std::uint8_t DragSurfaceCommitMask(
			std::size_t surfaceIndex) noexcept
		{
			return static_cast<std::uint8_t>(1U << (surfaceIndex % 2));
		}

		void RequestDragPair(std::size_t pairIndex) noexcept
		{
			const auto pair = DragPair(pairIndex);
			Inkeys::UI::RenderPipeline::Request(
				Inkeys::UI::RenderPipeline::Mask(Clients[pair[0]])
				| Inkeys::UI::RenderPipeline::Mask(Clients[pair[1]]));
		}

		void CopyDragPairPosition(std::size_t pairIndex,
			const PptLayoutState& source, PptLayoutState& target) noexcept
		{
			if (pairIndex == 0)
			{
				target.bottomPairWidth = source.bottomPairWidth;
				target.bottomPairHeight = source.bottomPairHeight;
			}
			else
			{
				target.middlePairWidth = source.middlePairWidth;
				target.middlePairHeight = source.middlePairHeight;
			}
		}

		[[nodiscard]] bool SameDragPairPosition(std::size_t pairIndex,
			const PptLayoutState& left, const PptLayoutState& right) noexcept
		{
			return pairIndex == 0
				? left.bottomPairWidth == right.bottomPairWidth
					&& left.bottomPairHeight == right.bottomPairHeight
				: left.middlePairWidth == right.middlePairWidth
					&& left.middlePairHeight == right.middlePairHeight;
		}

		void BeginDragTracking(std::size_t pairIndex,
			const PptLayoutState& layout) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			BeginPptDragTracking(dragCommitTrackers[pairIndex], layout);
		}

		struct DragPublicationSnapshot
		{
			PptLayoutState layout{};
			std::uint64_t revision = 0;
			bool replacedPending = false;
			std::uint64_t replacedRevision = 0;
		};

		[[nodiscard]] DragPublicationSnapshot PublishDragCandidate(
			std::size_t pairIndex, const PptLayoutState& candidate) noexcept
		{
			DragPublicationSnapshot result;
			{
				std::scoped_lock dragLock(dragCommitMutex);
				std::scoped_lock snapshotLock(snapshotMutex);
				result.revision = directMoveRevision.load(
					std::memory_order_relaxed) + 1;
				result.layout = publishedPpt.layout;
				CopyDragPairPosition(pairIndex, candidate, result.layout);
				auto& tracker = dragCommitTrackers[pairIndex];
				if (!tracker.ownsLayout)
					BeginPptDragTracking(tracker, publishedPpt.layout);
				const auto publication = PublishPptDragCandidate(
					tracker, result.layout, result.revision);
				result.replacedPending = publication.replacedPending;
				result.replacedRevision = publication.replacedRevision;
				publishedPpt.layout = result.layout;
				// 先写完整 mailbox/layout，再以 release revision 对渲染线程发布。
				directMoveRevision.store(
					result.revision, std::memory_order_release);
			}
			return result;
		}

		struct DragCommitSnapshot
		{
			PptLayoutState layout{};
			std::uint64_t revision = 0;
			std::uint8_t committedSurfaceMask = 0;
			bool matched = false;
			bool completed = false;
			bool released = false;
			std::uint64_t directRevision = 0;
		};

		[[nodiscard]] DragCommitSnapshot ObservePendingDrag(
			std::size_t surfaceIndex, std::uint64_t revision) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			const auto& tracker = dragCommitTrackers[
				DragPairIndex(surfaceIndex)];
			if (!tracker.pending || tracker.revision != revision) return {};
			return { tracker.layout, tracker.revision,
				tracker.committedSurfaceMask, true, false, tracker.released };
		}

		[[nodiscard]] DragCommitSnapshot CommitDragSurface(
			std::size_t surfaceIndex, std::uint64_t revision) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			auto& tracker = dragCommitTrackers[DragPairIndex(surfaceIndex)];
			if (!tracker.pending || tracker.revision != revision) return {};
			DragCommitSnapshot result{ tracker.layout, tracker.revision,
				tracker.committedSurfaceMask, true, false, tracker.released };
			result.completed = MarkPptDragSurfaceCommitted(tracker, revision,
				DragSurfaceCommitMask(surfaceIndex));
			result.committedSurfaceMask = tracker.committedSurfaceMask;
			return result;
		}

		[[nodiscard]] DragCommitSnapshot CommitDragPairDirect(
			std::size_t pairIndex, std::uint64_t revision) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			auto& tracker = dragCommitTrackers[pairIndex];
			if (!tracker.pending || tracker.revision != revision) return {};
			DragCommitSnapshot result{ tracker.layout, tracker.revision,
				tracker.committedSurfaceMask, true, false, tracker.released };
			result.completed = MarkPptDragSurfaceCommitted(tracker, revision,
				PptDragCommittedSurfaceMask);
			result.committedSurfaceMask = tracker.committedSurfaceMask;
			if (result.completed)
			{
				result.directRevision = directMoveRevision.load(
					std::memory_order_relaxed) + 1;
				// SurfaceState.bounds 已在 renderTransactionMutex 内写完，再发布提交 revision。
				directMoveRevision.store(
					result.directRevision, std::memory_order_release);
			}
			return result;
		}

		[[nodiscard]] PptDragReleaseResult EndDragTracking(
			std::size_t pairIndex, bool persist) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			return ReleasePptDragTracking(
				dragCommitTrackers[pairIndex], persist);
		}

		void FinishDragPersistence(std::size_t pairIndex,
			std::uint64_t revision) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			(void)CompletePptDragPersistence(
				dragCommitTrackers[pairIndex], revision);
		}

		[[nodiscard]] PptDragRollbackResult RollbackDragTracking(
			std::size_t pairIndex) noexcept
		{
			PptDragRollbackResult result;
			{
				std::scoped_lock dragLock(dragCommitMutex);
				auto& tracker = dragCommitTrackers[pairIndex];
				result = RollbackPptDragTracking(tracker);
				if (!result.tracked) return result;
				std::scoped_lock snapshotLock(snapshotMutex);
				CopyDragPairPosition(pairIndex, result.layout,
					publishedPpt.layout);
				if (result.discardedPending)
				{
					const auto rollbackRevision = directMoveRevision.load(
						std::memory_order_relaxed) + 1;
					directMoveRevision.store(
						rollbackRevision, std::memory_order_release);
				}
			}
			return result;
		}

		void PreserveOwnedDragLayouts(PptState& state) noexcept
		{
			for (std::size_t pairIndex = 0;
				pairIndex < dragCommitTrackers.size(); ++pairIndex)
			{
				const auto& tracker = dragCommitTrackers[pairIndex];
				if (tracker.ownsLayout)
					CopyDragPairPosition(pairIndex, tracker.layout, state.layout);
			}
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
				{
					// 结束页 Next 复用 A2 的确认/退出队列，普通页仍保持翻页回调。
					switch (ResolvePptDirectionAction(
						next, ppt.currentPage, ppt.totalPage))
					{
					case PptDirectionAction::PreviousPage:
						callback = pptCallbacks.previousPage;
						break;
					case PptDirectionAction::NextPage:
						callback = pptCallbacks.nextPage;
						break;
					case PptDirectionAction::EndShow:
						callback = pptCallbacks.endShow;
						break;
					}
				}
			}
			if (callback) callback();
		}

		void InvokePptPreview()
		{
			std::function<void()> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = pptCallbacks.viewShow;
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
			background.cornerRadiusDip = BarMainBarCornerRadiusDip;
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
			const auto inputPolicy = ResolveWorkspaceInputPolicy(mode);
			auto& drag = widgets[0];
			drag.id = DragWidget;
			drag.kind = Inkeys::UI::Bar::BarSurfaceWidgetKind::DragHandle;
			drag.visible = mode != WorkspaceMode::Hidden;
			drag.enabled = inputPolicy.drag;
			drag.interactive = inputPolicy.drag;
			drag.dragOpacity = mode == WorkspaceMode::PptCompact ? 0.72 : 0.0;
			ApplyDarkTheme(drag);

			auto& previous = widgets[1];
			previous.id = PreviousWidget;
			previous.layoutKind = mode == WorkspaceMode::WhiteboardExpanded
				? Inkeys::UI::Bar::BarButtonVisualLayoutKind::StandardTwoTwo
				: Inkeys::UI::Bar::BarButtonVisualLayoutKind::StandardOneOne;
			const auto previousContent = ResolveDirectionContentPolicy(
				mode, false, false);
			previous.iconResource = previousContent.icon;
			previous.secondaryText = previousContent.label;
			previous.iconAngle = ResolveDirectionIconAngle(
				SurfaceFor(index), mode, false, false);
			previous.onClick = [index] { InvokeDirection(index, false); };
			ApplyDarkTheme(previous);

			auto& page = widgets[2];
			page.id = PageWidget;
			page.textUpdateMode =
				Inkeys::UI::Bar::BarSurfaceTextUpdateMode::Immediate;
			page.layoutKind = mode == WorkspaceMode::WhiteboardExpanded
				? Inkeys::UI::Bar::BarButtonVisualLayoutKind::PageTwoTwo
				: (index >= 2
					? Inkeys::UI::Bar::BarButtonVisualLayoutKind::PageVertical
					: Inkeys::UI::Bar::BarButtonVisualLayoutKind::PageHorizontal);
			ApplyDarkTheme(page);

			auto& next = widgets[3];
			next.id = NextWidget;
			next.layoutKind = mode == WorkspaceMode::WhiteboardExpanded
				? Inkeys::UI::Bar::BarButtonVisualLayoutKind::StandardTwoTwo
				: Inkeys::UI::Bar::BarButtonVisualLayoutKind::StandardOneOne;
			const bool nextIsEndShow = IsPptEndPage(
				ppt.currentPage, ppt.totalPage);
			const auto nextContent = ResolveDirectionContentPolicy(
				mode, true, whiteboard.nextIsAdd, nextIsEndShow);
			next.iconResource = nextContent.icon;
			next.secondaryText = nextContent.label;
			next.iconAngle = ResolveDirectionIconAngle(
				SurfaceFor(index), mode, true, whiteboard.nextIsAdd,
				nextIsEndShow);
			next.onClick = [index] { InvokeDirection(index, true); };
			ApplyDarkTheme(next);

			if (mode == WorkspaceMode::WhiteboardExpanded)
			{
				// Whiteboard 固定三枚真实 2x2 按钮；PPT-only Drag 槽向外侧收拢。
				drag.bounds = index == 0
					? Inkeys::UI::Bar::BarSurfaceDipRect{ 5.0, 5.0, 5.0, 75.0 }
					: Inkeys::UI::Bar::BarSurfaceDipRect{ 225.0, 5.0, 225.0, 75.0 };
				previous.bounds = { 5.0, 5.0, 75.0, 75.0 };
				page.bounds = { 80.0, 5.0, 150.0, 75.0 };
				next.bounds = { 155.0, 5.0, 225.0, 75.0 };
				previous.enabled = whiteboard.previousEnabled;
				previous.interactive = whiteboard.previousInteractive;
				page.enabled = whiteboard.pageEnabled;
				page.interactive = whiteboard.pageInteractive;
				page.primaryText = std::to_wstring(whiteboard.currentPage);
				page.secondaryText = L"/" + std::to_wstring(whiteboard.totalPage);
				next.enabled = whiteboard.nextEnabled;
				next.interactive = whiteboard.nextInteractive;
			}
			else
			{
				const bool vertical = index >= 2;
				const auto contracts = ResolvePptWidgetContracts(SurfaceFor(index));
				drag.bounds = { contracts[0].bounds.left, contracts[0].bounds.top,
					contracts[0].bounds.right, contracts[0].bounds.bottom };
				previous.bounds = { contracts[1].bounds.left, contracts[1].bounds.top,
					contracts[1].bounds.right, contracts[1].bounds.bottom };
				page.bounds = { contracts[2].bounds.left, contracts[2].bounds.top,
					contracts[2].bounds.right, contracts[2].bounds.bottom };
				next.bounds = { contracts[3].bounds.left, contracts[3].bounds.top,
					contracts[3].bounds.right, contracts[3].bounds.bottom };
				previous.enabled = previous.interactive = true;
				page.enabled = page.interactive = true;
				next.enabled = next.interactive = true;
				const int maximum = vertical ? 999 : 9999;
				page.primaryText = PageNumber(ppt.currentPage, maximum);
				page.secondaryText = L"/" + PageNumber(ppt.totalPage, maximum);
			}
			return widgets;
		}

		[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept
		{
			return EqualRect(&left, &right) != FALSE;
		}

		bool ApplySceneBounds(SurfaceState& state, const RECT& bounds,
			float scale) noexcept
		{
			const float normalizedScale = NormalizeScale(scale);
			if (!ShouldApplyPageControlSceneBounds(
				state.appliedSceneBoundsReady, state.appliedSceneBounds,
				state.appliedSceneScale, bounds, normalizedScale)) return true;
			if (!state.scene.SetBounds(bounds, normalizedScale)) return false;
			// Scene::SetBounds 即使输入不变也会唤醒；仅在成功应用后推进缓存。
			state.appliedSceneBounds = bounds;
			state.appliedSceneScale = normalizedScale;
			state.appliedSceneBoundsReady = true;
			return true;
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
				/ static_cast<double>(LayoutTransitionWallDuration().count()),
				0.0, 1.0);
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
				state.scene.SetHooks({ {}, [index] { RequestSurface(index); } });
				const bool fadeAtTarget = desiredMode != WorkspaceMode::Hidden
					&& index < 2;
				state.scene.SetOpacity(fadeAtTarget || desiredMode == WorkspaceMode::Hidden
					? 0.0 : 1.0, 0.0);
				if (fadeAtTarget)
				{
					state.scene.SetOpacity(1.0,
						LayoutTransitionDurationSeconds());
					state.layoutTransitionUntil = now
						+ LayoutTransitionWallDuration();
				}
				state.sceneConfigured = true;
				state.configuredMode = visualMode;
				state.observedRevision = revision;
			}
			else if (modeChanged)
			{
				const auto background = BuildBackground(index, visualMode);
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard);
				const bool animateLayout = ShouldAnimateWorkspaceLayout(SurfaceFor(index),
						state.configuredMode, visualMode, state.targetVisible);
				(void)state.scene.TransitionLayout(background, widgets,
					animateLayout ? LayoutTransitionDurationSeconds() : 0.0);
				if (modeChanged)
					state.layoutTransitionUntil = now
						+ LayoutTransitionWallDuration();
				state.configuredMode = visualMode;
				state.observedRevision = revision;
			}
			else if (contentChanged)
			{
				// 相同布局只更新稳定 widget；TransitionLayout 会无条件扩大为整窗 damage。
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard);
				for (const auto& widget : widgets)
				{
					(void)state.scene.SetWidgetState(widget.id, widget.visible,
						widget.enabled, widget.primaryText, widget.secondaryText,
						widget.iconResource, widget.iconAngle);
					(void)state.scene.SetWidgetInteractive(
						widget.id, widget.interactive);
				}
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
					LayoutTransitionDurationSeconds());
				state.layoutTransitionUntil = now
					+ LayoutTransitionWallDuration();
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
			if (state.inputLocked && state.borderCursorPointerInside)
			{
				state.borderCursorPointerInside = false;
				Inkeys::UI::Bar::NotifyBorderCursorSurfacePointerLeft();
			}
			(void)ApplySceneBounds(state, CurrentBounds(state.bounds),
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

		struct PresentSceneResult
		{
			PresentStatus status = PresentStatus::Retry;
			bool continueRendering = false;
			RECT debugFrameBounds{};
			RECT debugWindowBounds{};
		};

		[[nodiscard]] PresentSceneResult PresentScene(SurfaceState& state, HWND hwnd,
			const FrameContext& frameContext, const RECT& presentationBounds,
			SIZE backingCapacity, bool forceFullReplacement,
			bool showDebugFrames, bool lifecycleRendering) noexcept
		{
			PresentSceneResult result;
			const UINT width = static_cast<UINT>(presentationBounds.right
				- presentationBounds.left);
			const UINT height = static_cast<UINT>(presentationBounds.bottom
				- presentationBounds.top);
			if (!hwnd || width == 0 || height == 0) return result;
			const HRESULT resourceHr = state.scene.EnsureDeviceResources(
				frameContext.epoch,
				static_cast<UINT>((std::max)(1L, backingCapacity.cx)),
				static_cast<UINT>((std::max)(1L, backingCapacity.cy)));
			if (FAILED(resourceHr))
			{
				result.status = IsDeviceLost(resourceHr)
					? PresentStatus::DeviceLost : PresentStatus::Retry;
				return result;
			}
			auto* rawContext = state.scene.DeviceContext();
			auto* rawGdi = state.scene.GdiInteropRenderTarget();
			if (!rawContext || !rawGdi) return result;
			ComPtr<ID2D1DeviceContext> context(rawContext);
			ComPtr<ID2D1GdiInteropRenderTarget> gdi(rawGdi);
			context->BeginDraw();
			context->SetTransform(D2D1::Matrix3x2F::Identity());
			context->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
			const auto renderResult = state.scene.Render(
				context.Get(), frameContext.frameTime);
			RECT businessDamage = state.scene.PendingDamage();
			const bool businessDamagePending =
				!Inkeys::UI::Bar::BarDirtyRegionTracker::IsEmpty(businessDamage);
			const bool retryingNonSleepVisual =
				ShouldTreatPageControlDamageAsActiveDebugFrame(
					showDebugFrames, businessDamagePending, forceFullReplacement,
					state.debugFrameSleepLatch.IsPending());
			const bool hasActiveRendering = lifecycleRendering
				|| renderResult.animationActive || retryingNonSleepVisual;
			const bool finalIdleFrame = state.debugFrameSleepLatch.Update(
				showDebugFrames, hasActiveRendering);
			// 最终绿框就在当前帧提交，成功后应直接休眠，不能再唤醒一帧红框。
			result.continueRendering = hasActiveRendering;
			if (!businessDamagePending && !finalIdleFrame
				&& !state.debugOverlayRefreshPending)
			{
				// 非调试请求缺少分类 damage 时仍安全回退整窗。
				businessDamage = RECT{ 0, 0, static_cast<LONG>(width),
					static_cast<LONG>(height) };
			}
			const auto debugDamage = Inkeys::UI::Bar::ResolveBarDebugDamage(
				businessDamage, {}, state.lastPresentedDebugFrameBounds, {},
				showDebugFrames, finalIdleFrame);
			RECT frameTarget = debugDamage.frameTarget;
			RECT presentDirty = debugDamage.presentDamage;
			const RECT windowTarget{ 0, 0, static_cast<LONG>(width),
				static_cast<LONG>(height) };
			const auto debugWindowDamage =
				ResolvePageControlDebugWindowDamagePolicy(forceFullReplacement,
					state.debugOverlayRefreshPending, showDebugFrames);
			if (debugWindowDamage.includePreviousWindow)
				Inkeys::UI::Bar::BarDirtyRegionTracker::UnionInPlace(
					presentDirty, state.lastPresentedDebugWindowBounds);
			if (debugWindowDamage.includeCurrentWindow)
				Inkeys::UI::Bar::BarDirtyRegionTracker::UnionInPlace(
					presentDirty, windowTarget);
			presentDirty = Inkeys::UI::Bar::IntersectBarDirtyRect(
				presentDirty, windowTarget);
			if (Inkeys::UI::Bar::BarDirtyRegionTracker::IsEmpty(presentDirty))
				presentDirty = windowTarget;
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
					static_cast<FLOAT>((std::max)(0L, frameTarget.left)) + dirtyInset,
					static_cast<FLOAT>((std::max)(0L, frameTarget.top)) + dirtyInset,
					static_cast<FLOAT>((std::min)(static_cast<LONG>(width), frameTarget.right))
						- dirtyInset,
					static_cast<FLOAT>((std::min)(static_cast<LONG>(height), frameTarget.bottom))
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
				info.prcDirty = forceFullReplacement ? nullptr : &presentDirty;
				info.dwFlags = ULW_ALPHA;
				presented = UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
				releaseDcHr = gdi->ReleaseDC(nullptr);
			}
			else if (SUCCEEDED(getDcHr)) getDcHr = E_FAIL;
			const HRESULT endDrawHr = context->EndDraw();
			state.scene.HandleFrameEndDrawResult(endDrawHr);
			if (IsDeviceLost(getDcHr) || IsDeviceLost(releaseDcHr)
				|| IsDeviceLost(endDrawHr))
			{
				state.scene.ReleaseDeviceResources();
				result.status = PresentStatus::DeviceLost;
				return result;
			}
			if (FAILED(getDcHr) || FAILED(releaseDcHr)
				|| FAILED(endDrawHr) || !presented)
			{
				state.scene.ReleaseDeviceResources();
				return result;
			}
			result.status = PresentStatus::Success;
			result.debugFrameBounds = showDebugFrames ? frameTarget : RECT{};
			result.debugWindowBounds = showDebugFrames ? windowTarget : RECT{};
			return result;
		}

		[[nodiscard]] bool DragLayoutCollides(
			const RECT& monitor, float dpiScale, const PptState& source,
			const PptLayoutState& layout) noexcept
		{
			PptState candidate = source;
			candidate.layout = layout;
			const LONG gap = static_cast<LONG>(std::lround(
				PageControlGapDip * NormalizeScale(dpiScale)));
			return PageControlGroupsOverlap(
				monitor, dpiScale, candidate, gap);
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

		void ApplyDragPairBoundsDirect(std::size_t pairIndex,
			const PptLayoutState& layout) noexcept
		{
			auto [ppt, whiteboard] = Snapshot();
			(void)whiteboard;
			ppt.presentationVisible = true;
			CopyDragPairPosition(pairIndex, layout, ppt.layout);
			const RECT monitor = PrimaryBounds();
			const float dpiScale = DpiScale(nullptr);
			const auto pair = DragPair(pairIndex);
			std::unique_lock renderLock(renderTransactionMutex);
			for (const std::size_t surfaceIndex : pair)
			{
				const auto target = ResolveSurfaceLayout(
					SurfaceFor(surfaceIndex), monitor, dpiScale, ppt, {});
				auto& pairState = surfaces[surfaceIndex];
				SetBoundsDirect(pairState.bounds, target.logicalBounds,
					target.scale);
				(void)ApplySceneBounds(pairState, target.logicalBounds,
					target.scale);
			}
		}

		[[nodiscard]] bool MovePairWindowsDirect(
			const std::array<std::size_t, 2>& indices,
			const std::array<RECT, 2>& targets,
			std::uint64_t revision) noexcept
		{
			auto& service = Inkeys::Window::GetService();
			std::array<HWND, 2> handles{};
			std::array<RECT, 2> original{};
			const std::size_t pairIndex = DragPairIndex(indices[0]);
			for (std::size_t item = 0; item < indices.size(); ++item)
			{
				handles[item] = service.Handle(Roles[indices[item]]);
				if (!handles[item])
				{
					TraceDrag("move-pair pair=%s revision=%llu result=failed "
						"stage=%s-handle surface=%s error=0",
						pairIndex == 0 ? "bottom" : "middle",
						static_cast<unsigned long long>(revision),
						item == 0 ? "first" : "second",
						SurfaceName(indices[item]));
					return false;
				}
				if (!GetWindowRect(handles[item], &original[item]))
				{
					const DWORD error = GetLastError();
					TraceDrag("move-pair pair=%s revision=%llu result=failed "
						"stage=%s-get-window-rect surface=%s error=%lu",
						pairIndex == 0 ? "bottom" : "middle",
						static_cast<unsigned long long>(revision),
						item == 0 ? "first" : "second",
						SurfaceName(indices[item]),
						static_cast<unsigned long>(error));
					return false;
				}
			}

			constexpr UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
				| SWP_NOOWNERZORDER | SWP_NOSENDCHANGING;
			SetLastError(ERROR_SUCCESS);
			if (!SetWindowPos(handles[0], nullptr,
				targets[0].left, targets[0].top, 0, 0, flags))
			{
				const DWORD error = GetLastError();
				TraceDrag("move-pair pair=%s revision=%llu result=failed "
					"stage=first-set-window-pos surface=%s error=%lu",
					pairIndex == 0 ? "bottom" : "middle",
					static_cast<unsigned long long>(revision),
					SurfaceName(indices[0]),
					static_cast<unsigned long>(error));
				return false;
			}

			SetLastError(ERROR_SUCCESS);
			if (SetWindowPos(handles[1], nullptr,
				targets[1].left, targets[1].top, 0, 0, flags)) return true;

			const DWORD secondError = GetLastError();
			// 第二窗失败时恢复已移动的第一窗，pair 由渲染 fallback 再整体收敛。
			SetLastError(ERROR_SUCCESS);
			const bool rollbackSucceeded = SetWindowPos(handles[0], nullptr,
				original[0].left, original[0].top, 0, 0, flags) != FALSE;
			const DWORD rollbackError = rollbackSucceeded
				? ERROR_SUCCESS : GetLastError();
			TraceDrag("move-pair pair=%s revision=%llu result=failed "
				"stage=second-set-window-pos surface=%s error=%lu "
				"rollback_stage=first-set-window-pos rollback=%d rollback_error=%lu",
				pairIndex == 0 ? "bottom" : "middle",
				static_cast<unsigned long long>(revision),
				SurfaceName(indices[1]),
				static_cast<unsigned long>(secondError),
				rollbackSucceeded ? 1 : 0,
				static_cast<unsigned long>(rollbackError));
			return false;
		}

		// 调用方必须持有 renderTransactionMutex；直移和 bounds 提交共享同一事务。
		void UpdateDragLocked(std::size_t index, POINT screen)
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
			{
				std::scoped_lock lock(snapshotMutex);
				ppt = publishedPpt;
			}
			candidate = ClampPageControlLayout(moved, monitor,
				dpiScale, candidate);
			const std::size_t pairIndex = bottom ? 0 : 1;
			PptLayoutState mergedCandidate = ppt.layout;
			CopyDragPairPosition(pairIndex, candidate, mergedCandidate);
			if (DragLayoutCollides(monitor, dpiScale, ppt, mergedCandidate))
				CopyDragPairPosition(pairIndex,
					state.feasibleLayout, mergedCandidate);
			candidate = mergedCandidate;
			if (SameDragPairPosition(pairIndex,
				candidate, state.feasibleLayout)) return;
			const PptLayoutState previousFeasible = state.feasibleLayout;
			state.feasibleLayout = candidate;
			const auto publication = PublishDragCandidate(pairIndex, candidate);
			PptState previousPpt = ppt;
			CopyDragPairPosition(pairIndex, previousFeasible, previousPpt.layout);
			PptState candidatePpt = ppt;
			candidatePpt.layout = publication.layout;
			const auto pair = DragPair(pairIndex);
			std::array<ResolvedSurfaceLayout, 2> candidateLayouts{};
			std::array<PptDragPresentationTarget, 2> directTargets{};
			std::array<RECT, 2> presentationTargets{};
			for (std::size_t item = 0; item < pair.size(); ++item)
			{
				const Surface pairSurface = SurfaceFor(pair[item]);
				const auto previousLayout = ResolveSurfaceLayout(pairSurface,
					monitor, dpiScale, previousPpt, {});
				candidateLayouts[item] = ResolveSurfaceLayout(pairSurface, monitor,
					dpiScale, candidatePpt, {});
				auto& pairState = surfaces[pair[item]];
				directTargets[item] = ResolvePptDragPresentationTarget(
					previousLayout, candidateLayouts[item],
					pairState.scene.PresentationOutsetPixels());
				presentationTargets[item] = directTargets[item].bounds;
			}
			if (!directTargets[0].pureTranslation
				|| !directTargets[1].pureTranslation)
			{
				TraceDrag("candidate pair=%s revision=%llu result=fallback "
					"reason=non-translation pending=1 request_pair=1",
					pairIndex == 0 ? "bottom" : "middle",
					static_cast<unsigned long long>(publication.revision));
				RequestDragPair(pairIndex);
				return;
			}

			// 渲染线程可能持锁同步等待当前 owner；WndProc 只能尝试，不能形成等待环。
			std::unique_lock presentationLock(
				presentationMutex, std::try_to_lock);
			if (!presentationLock.owns_lock())
			{
				const auto traceNow = std::chrono::steady_clock::now();
				if (ShouldTraceDragCandidate(state, traceNow))
				{
					TraceDrag("candidate pair=%s revision=%llu result=fallback "
						"reason=presentation-lock-busy replaced=%d old_revision=%llu "
						"pending=1 request_pair=1 suppressed_direct=%llu "
						"suppressed_lock_busy=%llu",
						pairIndex == 0 ? "bottom" : "middle",
						static_cast<unsigned long long>(publication.revision),
						publication.replacedPending ? 1 : 0,
						static_cast<unsigned long long>(publication.replacedRevision),
						static_cast<unsigned long long>(
							state.suppressedDirectCandidateTraces),
						static_cast<unsigned long long>(
							state.suppressedPresentationBusyTraces));
					state.suppressedDirectCandidateTraces = 0;
					state.suppressedPresentationBusyTraces = 0;
				}
				else ++state.suppressedPresentationBusyTraces;
				RequestDragPair(pairIndex);
				return;
			}
			if (!MovePairWindowsDirect(
				pair, presentationTargets, publication.revision))
			{
				TraceDrag("candidate pair=%s revision=%llu result=fallback "
					"reason=window-move-failed pending=1 request_pair=1",
					pairIndex == 0 ? "bottom" : "middle",
					static_cast<unsigned long long>(publication.revision));
				RequestDragPair(pairIndex);
				return;
			}
			// 纯平移不触碰 Scene；松手后的唯一 RequestAll 再吸收最终布局。
			for (std::size_t item = 0; item < pair.size(); ++item)
				SetBoundsDirect(surfaces[pair[item]].bounds,
					candidateLayouts[item].logicalBounds,
					candidateLayouts[item].scale);
			const auto committed = CommitDragPairDirect(
				pairIndex, publication.revision);
			if (committed.completed)
			{
				for (std::size_t item = 0; item < pair.size(); ++item)
					Inkeys::UI::Bar::PublishBorderCursorSurfaceBounds(
						static_cast<unsigned int>(pair[item]),
						presentationTargets[item], true);
				const auto traceNow = std::chrono::steady_clock::now();
				if (ShouldTraceDragCandidate(state, traceNow))
				{
					TraceDrag("candidate pair=%s revision=%llu result=direct "
						"replaced=%d old_revision=%llu position=(%.1f,%.1f) "
						"delta=(%ld,%ld),(%ld,%ld) targets=(%ld,%ld),(%ld,%ld) "
						"direct_move_revision=%llu pending=0 request_pair=0 "
						"suppressed_direct=%llu suppressed_lock_busy=%llu",
						pairIndex == 0 ? "bottom" : "middle",
						static_cast<unsigned long long>(publication.revision),
						publication.replacedPending ? 1 : 0,
						static_cast<unsigned long long>(publication.replacedRevision),
						pairIndex == 0 ? publication.layout.bottomPairWidth
							: publication.layout.middlePairWidth,
						pairIndex == 0 ? publication.layout.bottomPairHeight
							: publication.layout.middlePairHeight,
						directTargets[0].deltaX, directTargets[0].deltaY,
						directTargets[1].deltaX, directTargets[1].deltaY,
						presentationTargets[0].left, presentationTargets[0].top,
						presentationTargets[1].left, presentationTargets[1].top,
						static_cast<unsigned long long>(committed.directRevision),
						static_cast<unsigned long long>(
							state.suppressedDirectCandidateTraces),
						static_cast<unsigned long long>(
							state.suppressedPresentationBusyTraces));
					state.suppressedDirectCandidateTraces = 0;
					state.suppressedPresentationBusyTraces = 0;
				}
				else ++state.suppressedDirectCandidateTraces;
			}
			else
			{
				TraceDrag("candidate pair=%s revision=%llu result=fallback "
					"reason=superseded pending=1 request_pair=1",
					pairIndex == 0 ? "bottom" : "middle",
					static_cast<unsigned long long>(publication.revision));
				RequestDragPair(pairIndex);
			}
		}

		[[nodiscard]] bool PersistDragPosition(const PptLayoutState& layout)
		{
			std::function<void(PptLayoutState)> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = pptCallbacks.persistPosition;
			}
			if (!callback) return false;
			callback(layout);
			return true;
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
			ppt = ResolveRuntimePageControlLayout(monitor, dpiScale, ppt);
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
				// 直移可在 frame 取快照后完成；旧帧不得先以 ULW 覆盖新 HWND 坐标。
				if (!IsPageControlFrameRevisionCurrent(frameDirectMoveRevision,
					directMoveRevision.load(std::memory_order_acquire)))
					return FrameResult::Retry;
				auto& state = surfaces[index];
				ConfigureSurface(index, mode, ppt, whiteboard, monitor, target,
					renderSnapshot.revision,
					frameContext.frameTime);
				const bool shouldSubscribeLighting =
					ShouldSubscribePageControlLighting(state.targetVisible,
						frameContext.frameTime < state.layoutTransitionUntil);
				if (state.lightingSubscribed != shouldSubscribeLighting)
				{
					state.scene.SetSharedLightingSubscribed(
						shouldSubscribeLighting);
					state.lightingSubscribed = shouldSubscribeLighting;
				}
				const auto directionPressPolicy =
					ResolvePptDirectionPressPolicy(mode, ppt.longPressEnabled);
				if (!directionPressPolicy.trackLongPress)
				{
					state.pressStarted = {};
					state.lastRepeat = {};
					state.repeatTiming = {};
				}
				const bool hasRepeated =
					state.lastRepeat.time_since_epoch().count() != 0;
				if (state.pressStarted.time_since_epoch().count() != 0
					&& ShouldTriggerPptLongPressRepeat(
						directionPressPolicy.trackLongPress,
						frameContext.frameTime - state.pressStarted,
						hasRepeated,
						hasRepeated
							? frameContext.frameTime - state.lastRepeat
							: std::chrono::steady_clock::duration::zero(),
						state.repeatTiming))
				{
					state.lastRepeat = ResolvePptLongPressRepeatAnchor(
						state.lastRepeat, frameContext.frameTime,
						hasRepeated, state.repeatTiming);
					repeatDirection = true;
					repeatNext = state.pressedNext;
					if (!IsPptDirectionActionRepeatable(ResolvePptDirectionAction(
						repeatNext, ppt.currentPage, ppt.totalPage)))
					{
						// 按住 Next 进入结束页时只投递一次 EndShow，随后终止重复。
						state.pressStarted = {};
						state.lastRepeat = {};
						state.repeatTiming = {};
					}
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
				const bool transitionDeadlineActive =
					frameContext.frameTime < state.layoutTransitionUntil;
				const bool exitTransitionActive = !state.targetVisible
					&& transitionDeadlineActive;
				keepAnimating = ShouldContinuePageControlFrame(
					state.targetVisible, transitionDeadlineActive,
					state.bounds.active, state.scene.AnimationActive(),
					state.pressStarted.time_since_epoch().count() != 0);
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
					const auto presentResult = PresentScene(state, hwnd,
						frameContext, presentation, backing.size,
						forceFullReplacement,
						debugEnabled.load(std::memory_order_acquire),
						keepAnimating);
					presentStatus = presentResult.status;
					keepAnimating = keepAnimating
						|| presentResult.continueRendering;
					if (presentStatus == PresentStatus::Success)
					{
						(void)state.scene.ConsumeDamage();
						state.committedPresentationSize = presentationSize;
						state.committedBackingCapacity = backing.size;
						state.committedDeviceGeneration = frameContext.epoch.generation;
						state.committedPresentationReady = true;
						state.forceFullPresentation = false;
						// 调试快照也属于呈现事务，失败帧不能提前推进绿框锁存。
						state.lastPresentedDebugFrameBounds =
							presentResult.debugFrameBounds;
						state.lastPresentedDebugWindowBounds =
							presentResult.debugWindowBounds;
						state.debugOverlayRefreshPending = false;
						(void)state.debugFrameSleepLatch.CommitPresented();
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
			if (!IsPageControlFrameRevisionCurrent(frameDirectMoveRevision,
				directMoveRevision.load(std::memory_order_acquire)))
				return FrameResult::Retry;
			const auto pendingDrag = ObservePendingDrag(
				index, frameDirectMoveRevision);
			if (pendingDrag.matched)
				TraceDrag("consume consumer=render surface=%s revision=%llu "
					"stage=attempt committed_mask=%u released=%d",
					SurfaceName(index),
					static_cast<unsigned long long>(pendingDrag.revision),
					static_cast<unsigned>(pendingDrag.committedSurfaceMask),
					pendingDrag.released ? 1 : 0);
			if (pendingDrag.matched && !shouldShow)
			{
				TraceDrag("consume consumer=render surface=%s revision=%llu "
					"result=deferred reason=surface-hidden pending=1",
					SurfaceName(index),
					static_cast<unsigned long long>(pendingDrag.revision));
				RequestDragPair(DragPairIndex(index));
				return FrameResult::Retry;
			}
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
				if (pendingDrag.matched)
					TraceDrag("consume consumer=render surface=%s revision=%llu "
						"result=failed set_bounds=%d visibility=%d pending=1",
						SurfaceName(index),
						static_cast<unsigned long long>(pendingDrag.revision),
						boundsApplied ? 1 : 0, visibilityApplied ? 1 : 0);
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
			// owner 可能在同步窗口提交期间发布了更新候选；旧提交必须继续重试。
			if (!IsPageControlFrameRevisionCurrent(frameDirectMoveRevision,
				directMoveRevision.load(std::memory_order_acquire)))
			{
				if (pendingDrag.matched)
					TraceDrag("consume consumer=render surface=%s revision=%llu "
						"result=stale-after-window-commit pending=1",
						SurfaceName(index),
						static_cast<unsigned long long>(pendingDrag.revision));
				return FrameResult::Retry;
			}
			const auto dragCommit = CommitDragSurface(
				index, frameDirectMoveRevision);
			if (dragCommit.matched)
				TraceDrag("consume consumer=render surface=%s revision=%llu "
					"result=committed committed_mask=%u pending=%d",
					SurfaceName(index),
					static_cast<unsigned long long>(dragCommit.revision),
					static_cast<unsigned>(dragCommit.committedSurfaceMask),
					dragCommit.completed ? 0 : 1);
			Inkeys::UI::Bar::PublishBorderCursorSurfaceBounds(
				static_cast<unsigned int>(index), presentation, shouldShow);
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
			if (message == WM_TABLET_QUERYSYSTEMGESTURESTATUS)
				return static_cast<LRESULT>(PageControlTabletGestureStatusFlags);
			if (message == WM_TOUCH)
			{
				const UINT count = LOWORD(wParam);
				std::vector<TOUCHINPUT> inputs(count);
				if (count != 0 && GetTouchInputInfo(
					reinterpret_cast<HTOUCHINPUT>(lParam), count,
					inputs.data(), sizeof(TOUCHINPUT)))
				{
					const bool hasPrimaryTouch = std::any_of(
						inputs.begin(), inputs.end(),
						[](const TOUCHINPUT& input) noexcept
						{
							return (input.dwFlags & TOUCHEVENTF_PRIMARY) != 0;
						});
					// 同批 primary 必须先接管，避免旧 fallback 先 Up 误触发 click。
					std::stable_partition(inputs.begin(), inputs.end(),
						[](const TOUCHINPUT& input) noexcept
						{
							return (input.dwFlags & TOUCHEVENTF_PRIMARY) != 0;
						});
					bool fallbackTouchLocked = false;
					for (const auto& input : inputs)
					{
						POINT local{ TOUCH_COORD_TO_PIXEL(input.x),
							TOUCH_COORD_TO_PIXEL(input.y) };
						ScreenToClient(hwnd, &local);
						PageControlTouchLockState current;
						{
							std::unique_lock renderLock(renderTransactionMutex);
							current = { state.touchId, state.touchActive,
								state.touchPrimary };
						}
						const auto decision = ResolvePageControlTouchSample(
							current, input.dwID,
							(input.dwFlags & TOUCHEVENTF_DOWN) != 0,
							(input.dwFlags & TOUCHEVENTF_MOVE) != 0,
							(input.dwFlags & TOUCHEVENTF_UP) != 0,
							(input.dwFlags & TOUCHEVENTF_PRIMARY) != 0,
							hasPrimaryTouch, fallbackTouchLocked);
						fallbackTouchLocked = decision.fallbackLocked;
						if (decision.cancelPrevious)
						{
							translatingTouch = true;
							(void)PageControlWindowProc(
								hwnd, WM_CANCELMODE, 0, 0);
							translatingTouch = false;
						}
						{
							std::unique_lock renderLock(renderTransactionMutex);
							state.touchId = decision.state.id;
							state.touchActive = decision.state.active;
							state.touchPrimary = decision.state.primary;
							if (decision.message != PageControlTouchMessage::None)
								state.touchLastClient = decision.state.active
									? local : POINT{};
						}
						UINT translated = 0;
						if (decision.message == PageControlTouchMessage::Down)
							translated = WM_LBUTTONDOWN;
						else if (decision.message == PageControlTouchMessage::Move)
							translated = WM_MOUSEMOVE;
						else if (decision.message == PageControlTouchMessage::Up)
							translated = WM_LBUTTONUP;
						if (translated == 0) continue;
						translatingTouch = true;
						(void)PageControlWindowProc(hwnd, translated,
							translated == WM_LBUTTONUP ? 0 : MK_LBUTTON,
							MAKELPARAM(local.x, local.y));
						translatingTouch = false;
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
				const auto logical = state.scene.PresentationToLogical(local);
				if (!logical.has_value()) return HTTRANSPARENT;
				const bool backgroundHit = state.scene.HitTestBackground(*logical);
				const bool widgetHit = state.scene.HitTest(*logical)
					!= Inkeys::UI::Bar::BarSurfaceNoWidget;
				// PPT 外框空白也属于拖动面；Whiteboard 仍只接收真实按钮。
				return ShouldAcceptPageControlClientHit(state.configuredMode,
					backgroundHit, widgetHit) ? HTCLIENT : HTTRANSPARENT;
			}
			if (message == WM_MOUSEMOVE)
			{
				TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&tracking);
				POINT screen{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ClientToScreen(hwnd, &screen);
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible || state.inputLocked) return 0;
				if (ShouldNotifyPageControlCursorEntered(
					state.borderCursorPointerInside, translatingTouch,
					Inkeys::Message::IsPointerGeneratedMouseMessage(
						message, static_cast<ULONG_PTR>(GetMessageExtraInfo()))))
				{
					state.borderCursorPointerInside = true;
					Inkeys::UI::Bar::NotifyBorderCursorSurfacePointerEntered();
				}
				if (state.dragPending && HasExceededPptDragThreshold(
					state.dragStartScreen, screen, GetSystemMetrics(SM_CXDRAG),
					GetSystemMetrics(SM_CYDRAG)))
				{
					state.dragPending = false;
					state.dragging = true;
					TraceDrag("threshold surface=%s start=(%ld,%ld) current=(%ld,%ld) "
						"result=dragging",
						SurfaceName(index), state.dragStartScreen.x,
						state.dragStartScreen.y, screen.x, screen.y);
					// Page 的标准 press 到阈值为止；转拖动时必须撤销而不是触发点击。
					state.scene.CancelPointer();
				}
				if (state.dragging)
				{
					UpdateDragLocked(index, screen);
					return 0;
				}
				if (state.dragPending) return 0;
				const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const auto logical = state.scene.PresentationToLogical(local);
				if (state.pressStarted.time_since_epoch().count() != 0)
				{
					const WidgetId expected = state.pressedNext
						? NextWidget : PreviousWidget;
					const bool sameDirectionHit = logical.has_value()
						&& state.scene.HitTest(*logical) == expected;
					if (!ShouldKeepPptLongPressTracking(true,
						GetCapture() == hwnd, sameDirectionHit))
					{
						// 拖出箭头后本次按压彻底取消，移回也不能重新启动长按。
						state.pressStarted = {};
						state.lastRepeat = {};
						state.repeatTiming = {};
					}
				}
				if (logical) state.scene.PointerMove(*logical);
				else state.scene.PointerLeave();
				return 0;
			}
			if (message == WM_MOUSELEAVE)
			{
				bool notifyBorderCursor = false;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					notifyBorderCursor = state.borderCursorPointerInside;
					state.borderCursorPointerInside = false;
					if (!state.dragging) state.scene.PointerLeave();
					state.pressStarted = {};
					state.lastRepeat = {};
					state.repeatTiming = {};
				}
				if (notifyBorderCursor)
					Inkeys::UI::Bar::NotifyBorderCursorSurfacePointerLeft();
				return 0;
			}
			if (message == WM_LBUTTONDOWN)
			{
				bool promotePpt = false;
				bool invokeDirectionOnDown = false;
				bool invokeNext = false;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					if (!state.targetVisible || state.inputLocked) return 0;
					const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					const auto logical = state.scene.PresentationToLogical(local);
					if (!logical.has_value()) return 0;
					const auto hit = state.scene.HitTest(*logical);
					const auto region = PointerRegionForWidget(hit);
					const auto inputPolicy = ResolveWorkspaceInputPolicy(
						state.configuredMode);
					const bool dragCandidate = inputPolicy.drag
						&& CanStartPptDrag(region);
					const auto result = hit == Inkeys::UI::Bar::BarSurfaceNoWidget
						? Inkeys::UI::Bar::BarSurfacePointerResult{}
						: state.scene.PointerDown(*logical);
					if (!result.consumed && !dragCandidate) return 0;
					SetCapture(hwnd);
					promotePpt = state.configuredMode == WorkspaceMode::PptCompact;
					auto [ppt, whiteboard] = Snapshot();
					(void)whiteboard;
					if (dragCandidate)
					{
						state.dragging = StartsPptDragImmediately(region);
						state.dragPending = !state.dragging;
						state.dragStartScreen = local;
						ClientToScreen(hwnd, &state.dragStartScreen);
						const RECT monitor = PrimaryBounds();
						ppt = ResolveRuntimePageControlLayout(
							monitor, DpiScale(hwnd), ppt);
						state.dragStartLayout = ppt.layout;
						state.feasibleLayout = ppt.layout;
						state.lastDragCandidateTrace = {};
						state.suppressedDirectCandidateTraces = 0;
						state.suppressedPresentationBusyTraces = 0;
						BeginDragTracking(DragPairIndex(index), ppt.layout);
						TraceDrag("down surface=%s region=%s immediate=%d "
							"start=(%ld,%ld) position=(%.1f,%.1f)",
							SurfaceName(index), PointerRegionName(region),
							state.dragging ? 1 : 0,
							state.dragStartScreen.x, state.dragStartScreen.y,
							DragPairIndex(index) == 0 ? ppt.layout.bottomPairWidth
								: ppt.layout.middlePairWidth,
							DragPairIndex(index) == 0 ? ppt.layout.bottomPairHeight
								: ppt.layout.middlePairHeight);
					}
					else if (result.pressed == PreviousWidget
						|| result.pressed == NextWidget)
					{
						invokeNext = result.pressed == NextWidget;
						const bool repeatable = IsPptDirectionActionRepeatable(
							ResolvePptDirectionAction(
								invokeNext, ppt.currentPage, ppt.totalPage));
						const auto pressPolicy = ResolvePptDirectionPressPolicy(
							state.configuredMode, ppt.longPressEnabled, repeatable);
						invokeDirectionOnDown = pressPolicy.invokeOnPointerDown;
						if (ShouldKeepPptLongPressTracking(
							pressPolicy.trackLongPress, GetCapture() == hwnd, true))
						{
							state.pressedNext = invokeNext;
							state.pressStarted = std::chrono::steady_clock::now();
							state.lastRepeat = {};
							state.repeatTiming = QueryPptKeyboardRepeatTiming();
							RequestSurface(index);
						}
					}
				}
				// Z 序和业务回调都可能同步进入 Window Service，必须位于 Scene 锁外。
				if (promotePpt)
					(void)Inkeys::Window::GetService().PromotePptWindow(Roles[index]);
				if (invokeDirectionOnDown) InvokeDirection(index, invokeNext);
				return 0;
			}
			if (message == WM_LBUTTONUP)
			{
				bool dragEnded = false;
				bool hadDragTracking = false;
				std::size_t dragPairIndex = DragPairIndex(index);
				std::uint64_t suppressedDirectCandidateTraces = 0;
				std::uint64_t suppressedPresentationBusyTraces = 0;
				bool shouldInvokeClick = false;
				WorkspaceMode releasedMode = WorkspaceMode::Hidden;
				Inkeys::UI::Bar::BarSurfaceWidgetId clicked =
					Inkeys::UI::Bar::BarSurfaceNoWidget;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					const bool acceptInput = !state.inputLocked && state.targetVisible;
					shouldInvokeClick = acceptInput;
					releasedMode = state.configuredMode;
					if (acceptInput)
					{
						if (const auto logical = state.scene.PresentationToLogical(local))
							clicked = state.scene.PointerUp(*logical, false).clicked;
						else state.scene.CancelPointer();
					}
					else state.scene.CancelPointer();
					hadDragTracking = state.dragging || state.dragPending;
					dragEnded = acceptInput && state.dragging;
					state.dragging = false;
					state.dragPending = false;
					suppressedDirectCandidateTraces =
						state.suppressedDirectCandidateTraces;
					suppressedPresentationBusyTraces =
						state.suppressedPresentationBusyTraces;
					state.lastDragCandidateTrace = {};
					state.suppressedDirectCandidateTraces = 0;
					state.suppressedPresentationBusyTraces = 0;
					state.pressStarted = {};
					state.lastRepeat = {};
					state.repeatTiming = {};
				}
				const auto dragRelease = hadDragTracking
					? EndDragTracking(dragPairIndex, dragEnded)
					: PptDragReleaseResult{};
				if (GetCapture() == hwnd) ReleaseCapture();
				if (dragEnded)
				{
					std::scoped_lock lock(snapshotMutex);
					publishedRevision.fetch_add(1, std::memory_order_relaxed);
				}
				if (shouldInvokeClick && releasedMode == WorkspaceMode::PptCompact
					&& clicked == PageWidget)
					InvokePptPreview();
				else if (shouldInvokeClick
					&& releasedMode != WorkspaceMode::PptCompact
					&& (clicked == PreviousWidget || clicked == NextWidget))
					InvokeDirection(index, clicked == NextWidget);
				if (dragRelease.tracked)
				{
					TraceDrag("up surface=%s pair=%s revision=%llu dragged=%d "
						"pending=%d persist=%d position=(%.1f,%.1f) "
						"suppressed_direct=%llu suppressed_lock_busy=%llu",
						SurfaceName(index),
						dragPairIndex == 0 ? "bottom" : "middle",
						static_cast<unsigned long long>(dragRelease.revision),
						dragEnded ? 1 : 0, dragRelease.pending ? 1 : 0,
						dragRelease.persist ? 1 : 0,
						dragPairIndex == 0
							? dragRelease.layout.bottomPairWidth
							: dragRelease.layout.middlePairWidth,
						dragPairIndex == 0
							? dragRelease.layout.bottomPairHeight
							: dragRelease.layout.middlePairHeight,
						static_cast<unsigned long long>(
							suppressedDirectCandidateTraces),
						static_cast<unsigned long long>(
							suppressedPresentationBusyTraces));
					if (dragRelease.pending) RequestDragPair(dragPairIndex);
				}
				if (dragEnded) RequestAll();
				if (dragRelease.persist)
				{
					const bool dispatched = PersistDragPosition(dragRelease.layout);
					TraceDrag("persist pair=%s revision=%llu dispatched=%d "
						"position=(%.1f,%.1f) pending=%d",
						dragPairIndex == 0 ? "bottom" : "middle",
						static_cast<unsigned long long>(dragRelease.revision),
						dispatched ? 1 : 0,
						dragPairIndex == 0
							? dragRelease.layout.bottomPairWidth
							: dragRelease.layout.middlePairWidth,
						dragPairIndex == 0
							? dragRelease.layout.bottomPairHeight
							: dragRelease.layout.middlePairHeight,
						dragRelease.pending ? 1 : 0);
					FinishDragPersistence(
						dragPairIndex, dragRelease.revision);
				}
				return 0;
			}
			if (message == WM_MOUSEWHEEL)
			{
				bool invoke = false;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					invoke = state.targetVisible && !state.inputLocked
						&& ResolveWorkspaceInputPolicy(
							state.configuredMode).wheel;
				}
				if (invoke) InvokeDirection(
					index, GET_WHEEL_DELTA_WPARAM(wParam) < 0);
				return 0;
			}
			if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
			{
				bool rollbackDrag = false;
				const std::size_t dragPairIndex = DragPairIndex(index);
				{
					std::unique_lock renderLock(renderTransactionMutex);
					rollbackDrag = state.dragging || state.dragPending;
					state.scene.CancelPointer();
					state.dragging = false;
					state.dragPending = false;
					state.lastDragCandidateTrace = {};
					state.suppressedDirectCandidateTraces = 0;
					state.suppressedPresentationBusyTraces = 0;
					state.pressStarted = {};
					state.lastRepeat = {};
					state.repeatTiming = {};
					state.touchActive = false;
					state.touchId = 0;
					state.touchPrimary = false;
					state.touchLastClient = {};
				}
				if (rollbackDrag)
				{
					const auto rollback = RollbackDragTracking(dragPairIndex);
					if (rollback.tracked)
					{
						if (rollback.discardedPending)
							ApplyDragPairBoundsDirect(
								dragPairIndex, rollback.layout);
						TraceDrag("cancel surface=%s pair=%s revision=%llu "
							"discarded_pending=%d rollback_position=(%.1f,%.1f)",
							SurfaceName(index),
							dragPairIndex == 0 ? "bottom" : "middle",
							static_cast<unsigned long long>(rollback.revision),
							rollback.discardedPending ? 1 : 0,
							dragPairIndex == 0 ? rollback.layout.bottomPairWidth
								: rollback.layout.middlePairWidth,
							dragPairIndex == 0 ? rollback.layout.bottomPairHeight
								: rollback.layout.middlePairHeight);
						RequestDragPair(dragPairIndex);
					}
				}
				// ReleaseCapture 会同步重入 WM_CAPTURECHANGED，必须在呈现锁外执行。
				if (message == WM_CANCELMODE && GetCapture() == hwnd) ReleaseCapture();
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
		for (std::size_t index = 0; index < Roles.size(); ++index)
		{
			if (service.Hide(Roles[index]))
				Inkeys::UI::Bar::PublishBorderCursorSurfaceBounds(
					static_cast<unsigned int>(index), {}, false);
		}
		{
			std::scoped_lock dragLock(dragCommitMutex);
			dragCommitTrackers = {};
		}
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
		PptState merged = state;
		{
			std::scoped_lock dragLock(dragCommitMutex);
			PreserveOwnedDragLayouts(merged);
			std::scoped_lock snapshotLock(snapshotMutex);
			if (ArePptStatesEquivalent(publishedPpt, merged)) return;
			publishedPpt = merged;
			publishedRevision.fetch_add(1, std::memory_order_release);
		}
		RequestAll();
	}

	void PublishWhiteboardState(const WhiteboardState& state) noexcept
	{
		{
			std::scoped_lock lock(snapshotMutex);
			if (AreWhiteboardStatesEquivalent(publishedWhiteboard, state)) return;
			publishedWhiteboard = state;
			publishedRevision.fetch_add(1, std::memory_order_release);
		}
		RequestAll();
	}

	void QueuePptWheel(short delta) noexcept
	{
		const auto [ppt, whiteboard] = Snapshot();
		if (!ppt.presentationVisible || whiteboard.expandedLayoutTarget
			|| whiteboard.active || WhiteboardWorkspaceSwitching(whiteboard)
			|| delta == 0) return;
		InvokeDirection(0, delta < 0);
	}

	void SetDebugEnabled(bool enabled) noexcept
	{
		if (debugEnabled.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		{
			std::unique_lock renderLock(renderTransactionMutex);
			// 与主栏一致，只在开关边界刷新整层覆盖；稳定蓝框不扩大后续 dirty。
			for (auto& surface : surfaces)
				surface.debugOverlayRefreshPending = true;
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
		std::array<bool, 2> rollbackPairs{};
		{
			std::unique_lock renderLock(renderTransactionMutex);
			for (std::size_t index = 0; index < surfaces.size(); ++index)
			{
				auto& surface = surfaces[index];
				rollbackPairs[DragPairIndex(index)] =
					rollbackPairs[DragPairIndex(index)]
					|| surface.dragging || surface.dragPending;
				surface.scene.CancelPointer();
				surface.dragging = false;
				surface.dragPending = false;
				surface.lastDragCandidateTrace = {};
				surface.suppressedDirectCandidateTraces = 0;
				surface.suppressedPresentationBusyTraces = 0;
				surface.pressStarted = {};
				surface.lastRepeat = {};
				surface.repeatTiming = {};
				surface.touchActive = false;
				surface.touchId = 0;
				surface.touchPrimary = false;
				surface.touchLastClient = {};
			}
		}
		for (std::size_t pairIndex = 0;
			pairIndex < rollbackPairs.size(); ++pairIndex)
		{
			if (!rollbackPairs[pairIndex]) continue;
			const auto rollback = RollbackDragTracking(pairIndex);
			if (!rollback.tracked) continue;
			if (rollback.discardedPending)
				ApplyDragPairBoundsDirect(pairIndex, rollback.layout);
			TraceDrag("cancel-all pair=%s revision=%llu discarded_pending=%d "
				"rollback_position=(%.1f,%.1f)",
				pairIndex == 0 ? "bottom" : "middle",
				static_cast<unsigned long long>(rollback.revision),
				rollback.discardedPending ? 1 : 0,
				pairIndex == 0 ? rollback.layout.bottomPairWidth
					: rollback.layout.middlePairWidth,
				pairIndex == 0 ? rollback.layout.bottomPairHeight
					: rollback.layout.middlePairHeight);
			RequestDragPair(pairIndex);
		}
		RequestAll();
	}
}
