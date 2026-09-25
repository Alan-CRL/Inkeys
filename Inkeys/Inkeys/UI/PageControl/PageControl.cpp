module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tpcshrd.h>
#include <wrl/client.h>

#include "../Bar/Bar.DirtyRegion.h"
#include "../../Drawing/Draw3/Draw3.PptTiming.h"

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
#include <thread>
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
			const char* lastFailureStage = nullptr;
			HRESULT lastFailureHr = S_OK;
			DWORD lastFailureError = ERROR_SUCCESS;
			std::chrono::steady_clock::time_point lastFailureTrace{};
			std::chrono::steady_clock::time_point retryAfter{};
			std::uint64_t retryPublicationRevision = 0;
			std::uint64_t retryDeviceGeneration = 0;
			unsigned retryBitmapLimit = 0;
			unsigned repeatedFailureCount = 0;
		};

		struct GroupLayoutBudgetCache
		{
			RECT monitor{};
			float dpiScale = 0.0F;
			std::uint64_t publicationRevision = 0;
			std::uint64_t directRevision = 0;
			std::uint64_t deviceGeneration = 0;
			PageControlSurfaceBudget budget{};
			bool valid = false;
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
		GroupLayoutBudgetCache groupLayoutBudget;
		std::atomic_int hiddenTestBlockedUlwSurface = -1;
		std::atomic_int hiddenTestBlockedResourceSurface = -1;
		std::atomic_bool hiddenTestMonitorEnabled = false;
		std::atomic_uint hiddenTestDpi = USER_DEFAULT_SCREEN_DPI;
		std::atomic_bool hiddenTestBudgetOverrideEnabled = false;
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
		PptPageCommitState pageCommit;
		std::array<std::uint64_t, 2> handedOffDragVersions{};
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
				if (candidate.session != publishedPpt.layout.session
					|| candidate.epoch != publishedPpt.layout.epoch) return {};
				result.layout = publishedPpt.layout;
				result.layout.pairVersions[pairIndex] = result.revision;
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
			std::size_t surfaceIndex, const PptState& frame) noexcept
		{
			const auto revision = frame.layout.pairVersions[DragPairIndex(surfaceIndex)];
			std::scoped_lock lock(dragCommitMutex);
			auto& tracker = dragCommitTrackers[DragPairIndex(surfaceIndex)];
			if (!tracker.pending || tracker.revision != revision) return {};
			DragCommitSnapshot result{ tracker.layout, tracker.revision,
				tracker.committedSurfaceMask, true, false, tracker.released };
			result.completed = MarkPptDragFrameCommitted(tracker, frame, surfaceIndex);
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
				publishedPpt.layout.pairVersions[pairIndex] = result.layout.pairVersions[pairIndex];
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
				{
					CopyDragPairPosition(pairIndex, tracker.layout, state.layout);
					state.layout.pairVersions[pairIndex] = tracker.layout.pairVersions[pairIndex];
				}
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

		[[nodiscard]] std::pair<RECT, float> LayoutMonitor() noexcept
		{
			if (hiddenTestMonitorEnabled.load(std::memory_order_acquire))
				return { { -30000, -30000, -28080, -28920 },
					static_cast<float>(hiddenTestDpi.load(std::memory_order_relaxed))
						/ USER_DEFAULT_SCREEN_DPI };
			const auto snapshot = Inkeys::Display::GetSnapshot();
			if (const auto* monitor = snapshot ? snapshot->Primary() : nullptr)
			{
				// 分页仍按主屏布局，DPI 必须来自同一显示快照；退场 HWND 可能已移到邻屏。
				const UINT dpi = monitor->effectiveDpiX
					? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI;
				return { monitor->bounds, static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI };
			}
			return { { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) }, 1.0F };
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

		std::uint8_t VisiblePptMask(const PptState& ppt,
			const WhiteboardState& whiteboard) noexcept;

		// 拖动与渲染共用同一代的保守预算；纯位置直移可复用原预算。
		[[nodiscard]] PageControlSurfaceBudget ResolveGroupBudgetLocked(
			const RenderSnapshot& snapshot, const RECT& monitor, float dpiScale,
			std::uint64_t directRevision, std::uint64_t deviceGeneration,
			bool reuseAfterDirectMove) noexcept
		{
			if (groupLayoutBudget.valid
				&& groupLayoutBudget.publicationRevision == snapshot.revision
				&& groupLayoutBudget.deviceGeneration == deviceGeneration
				&& groupLayoutBudget.dpiScale == dpiScale
				&& SameRect(groupLayoutBudget.monitor, monitor)
				&& (reuseAfterDirectMove
					|| groupLayoutBudget.directRevision == directRevision))
				return groupLayoutBudget.budget;

			std::array<PageControlSurfaceBudget, 4> local{};
			for (std::size_t item = 0; item < local.size(); ++item)
			{
				const auto& state = surfaces[item];
				const float oldScale = NormalizeScale(state.appliedSceneScale);
				local[item].presentationOutsetDip =
					static_cast<float>(state.scene.PresentationOutsetPixels()) / oldScale;
				if (auto* context = state.scene.DeviceContext())
					local[item].bitmapLimit = (std::min)(
						local[item].bitmapLimit, context->GetMaximumBitmapSize());
			}
			if (hiddenTestBudgetOverrideEnabled.load(std::memory_order_relaxed))
			{
				local[0] = { 10.0F, 16384 };
				local[1] = { 35.0F, 512 };
			}
			groupLayoutBudget = { monitor, dpiScale, snapshot.revision,
				directRevision, deviceGeneration,
				ResolvePageControlGroupBudget(local,
					VisiblePptMask(snapshot.ppt, snapshot.whiteboard)), true };
			return groupLayoutBudget.budget;
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

		[[nodiscard]] bool ConfigureSurface(std::size_t index, WorkspaceMode desiredMode,
			const PptState& ppt, const WhiteboardState& whiteboard,
			const RECT& monitor, const ResolvedSurfaceLayout& target,
			std::uint64_t revision,
			std::chrono::steady_clock::time_point now,
			const char*& failedStage)
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
				if (!state.scene.Configure(background, widgets))
				{
					failedStage = "scene-configure";
					return false;
				}
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
			}
			else if (modeChanged)
			{
				const auto background = BuildBackground(index, visualMode);
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard);
				const bool animateLayout = ShouldAnimateWorkspaceLayout(SurfaceFor(index),
						state.configuredMode, visualMode, state.targetVisible);
				if (!state.scene.TransitionLayout(background, widgets,
					animateLayout ? LayoutTransitionDurationSeconds() : 0.0))
				{
					failedStage = "scene-transition";
					return false;
				}
				if (modeChanged)
					state.layoutTransitionUntil = now
						+ LayoutTransitionWallDuration();
			}
			else if (contentChanged)
			{
				// 相同布局只更新稳定 widget；TransitionLayout 会无条件扩大为整窗 damage。
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard);
				for (const auto& widget : widgets)
				{
					if (!state.scene.SetWidgetState(widget.id, widget.visible,
						widget.enabled, widget.primaryText, widget.secondaryText,
						widget.iconResource, widget.iconAngle)
						|| !state.scene.SetWidgetInteractive(
							widget.id, widget.interactive))
					{
						failedStage = "scene-widget";
						return false;
					}
				}
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
			if (!ApplySceneBounds(state, CurrentBounds(state.bounds),
				static_cast<float>(state.bounds.scale)))
			{
				failedStage = "scene-bounds";
				return false;
			}
			// 场景和几何都成功后才承认该发布代，失败仍可重试。
			state.sceneConfigured = true;
			state.configuredMode = visualMode;
			state.observedRevision = revision;
			return true;
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
			const char* failureStage = "unknown";
			HRESULT failureHr = S_OK;
			DWORD failureError = ERROR_SUCCESS;
			bool resourcesFailed = false;
		};

		[[nodiscard]] PresentSceneResult PresentScene(std::size_t index,
			SurfaceState& state, HWND hwnd,
			const FrameContext& frameContext, const RECT& presentationBounds,
			SIZE backingCapacity, bool forceFullReplacement,
			bool showDebugFrames, bool lifecycleRendering) noexcept
		{
			PresentSceneResult result;
			const UINT width = static_cast<UINT>(presentationBounds.right
				- presentationBounds.left);
			const UINT height = static_cast<UINT>(presentationBounds.bottom
				- presentationBounds.top);
			if (!hwnd || width == 0 || height == 0)
			{
				result.failureStage = "invalid-presentation";
				result.failureError = ERROR_INVALID_WINDOW_HANDLE;
				return result;
			}
			const HRESULT resourceHr = hiddenTestBlockedResourceSurface.load(
				std::memory_order_relaxed) == static_cast<int>(index)
				? E_OUTOFMEMORY : state.scene.EnsureDeviceResources(
					frameContext.epoch,
					static_cast<UINT>((std::max)(1L, backingCapacity.cx)),
					static_cast<UINT>((std::max)(1L, backingCapacity.cy)));
			if (FAILED(resourceHr))
			{
				result.failureStage = "resources";
				result.failureHr = resourceHr;
				result.resourcesFailed = true;
				result.status = IsDeviceLost(resourceHr)
					? PresentStatus::DeviceLost : PresentStatus::Retry;
				return result;
			}
			auto* rawContext = state.scene.DeviceContext();
			auto* rawGdi = state.scene.GdiInteropRenderTarget();
			if (!rawContext || !rawGdi)
			{
				result.failureStage = "context";
				result.failureHr = E_POINTER;
				return result;
			}
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
			DWORD ulwError = ERROR_SUCCESS;
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
				if (hiddenTestBlockedUlwSurface.load(std::memory_order_relaxed)
					== static_cast<int>(index))
					ulwError = ERROR_GEN_FAILURE;
				else
				{
					SetLastError(ERROR_SUCCESS);
					presented = UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
					if (!presented) ulwError = GetLastError();
				}
				releaseDcHr = gdi->ReleaseDC(nullptr);
			}
			else if (SUCCEEDED(getDcHr)) getDcHr = E_FAIL;
			const HRESULT endDrawHr = context->EndDraw();
			state.scene.HandleFrameEndDrawResult(endDrawHr);
			if (IsDeviceLost(getDcHr) || IsDeviceLost(releaseDcHr)
				|| IsDeviceLost(endDrawHr))
			{
				result.failureStage = IsDeviceLost(getDcHr) ? "getdc"
					: IsDeviceLost(releaseDcHr) ? "releasedc" : "enddraw";
				result.failureHr = IsDeviceLost(getDcHr) ? getDcHr
					: IsDeviceLost(releaseDcHr) ? releaseDcHr : endDrawHr;
				result.failureError = ulwError;
				state.scene.ReleaseDeviceResources();
				result.status = PresentStatus::DeviceLost;
				return result;
			}
			if (FAILED(getDcHr) || FAILED(releaseDcHr)
				|| FAILED(endDrawHr) || !presented)
			{
				result.failureStage = FAILED(getDcHr) ? "getdc"
					: FAILED(releaseDcHr) ? "releasedc"
					: FAILED(endDrawHr) ? "enddraw" : "ulw";
				result.failureHr = FAILED(getDcHr) ? getDcHr
					: FAILED(releaseDcHr) ? releaseDcHr : endDrawHr;
				result.failureError = ulwError;
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
				PageControlGapDip * NormalizeDpiScale(dpiScale)));
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
			auto snapshot = SnapshotForRender();
			auto& ppt = snapshot.ppt;
			ppt.presentationVisible = true;
			CopyDragPairPosition(pairIndex, layout, ppt.layout);
			const auto [monitor, dpiScale] = LayoutMonitor();
			const auto deviceGeneration =
				Inkeys::UI::RenderPipeline::GetDeviceEpoch().generation;
			const auto pair = DragPair(pairIndex);
			std::unique_lock renderLock(renderTransactionMutex);
			const auto budget = ResolveGroupBudgetLocked(snapshot, monitor,
				dpiScale, directMoveRevision.load(std::memory_order_acquire),
				deviceGeneration, true);
			ppt = ResolveRuntimePageControlLayout(monitor, dpiScale, ppt,
				budget.presentationOutsetDip, budget.bitmapLimit);
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
			const auto [monitor, dpiScale] = LayoutMonitor();
			const auto snapshot = SnapshotForRender();
			PptState ppt = snapshot.ppt;
			if (candidate.session != ppt.layout.session || candidate.epoch != ppt.layout.epoch)
			{
				state.dragging = state.dragPending = false;
				return;
			}
			const auto deviceGeneration =
				Inkeys::UI::RenderPipeline::GetDeviceEpoch().generation;
			const auto budget = ResolveGroupBudgetLocked(snapshot, monitor,
				dpiScale, directMoveRevision.load(std::memory_order_acquire),
				deviceGeneration, true);
			ppt = ResolveRuntimePageControlLayout(monitor, dpiScale, ppt,
				budget.presentationOutsetDip, budget.bitmapLimit);
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
			if (publication.revision == 0) return;
			PptState previousPpt = ppt;
			CopyDragPairPosition(pairIndex, previousFeasible, previousPpt.layout);
			PptState candidatePpt = ppt;
			candidatePpt.layout = publication.layout;
			candidatePpt = ResolveRuntimePageControlLayout(monitor, dpiScale,
				candidatePpt, budget.presentationOutsetDip, budget.bitmapLimit);
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
				// 旧 Scene 的实际倍率不同于候选时不能做只移动 HWND 的快速路径。
				if (!pairState.appliedSceneBoundsReady
					|| std::abs(pairState.appliedSceneScale
						- candidateLayouts[item].scale) > 0.0001F)
					directTargets[item].pureTranslation = false;
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

		void FlushCommittedDrag(std::size_t pair)
		{
			PptLayoutState layout;
			{
				std::scoped_lock lock(dragCommitMutex);
				const auto& tracker = dragCommitTrackers[pair];
				layout = tracker.committedLayout;
				if (layout.pairVersions[pair] == 0) return;
				// 已提交位置可在拖动中被开关保存；未提交候选不进入 facade。
				if (layout.pairVersions[pair] <= handedOffDragVersions[pair])
				{
					(void)CompletePptDragPersistence(dragCommitTrackers[pair], tracker.revision);
					return;
				}
			}
			std::function<void(std::size_t, PptLayoutState)> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = pptCallbacks.commitPosition;
			}
			if (callback) callback(pair, layout);
			{
				std::scoped_lock lock(dragCommitMutex);
				const auto& tracker = dragCommitTrackers[pair];
				if (tracker.layout.session != layout.session || tracker.layout.epoch != layout.epoch) return;
				handedOffDragVersions[pair] = (std::max)(handedOffDragVersions[pair], layout.pairVersions[pair]);
				(void)CompletePptDragPersistence(dragCommitTrackers[pair], layout.pairVersions[pair]);
			}
		}

		std::uint8_t VisiblePptMask(const PptState& ppt, const WhiteboardState& whiteboard) noexcept
		{
			std::uint8_t result = 0;
			for (std::size_t index = 0; index < 4; ++index)
				if (ResolveWorkspaceMode(SurfaceFor(index), ppt, whiteboard) == WorkspaceMode::PptCompact)
					result |= static_cast<std::uint8_t>(1U << index);
			return result;
		}

		[[nodiscard]] PageControlSurfaceBudget GroupBudgetForFrame(
			const RenderSnapshot& snapshot, const RECT& monitor, float dpiScale,
			std::uint64_t directRevision,
			std::uint64_t deviceGeneration) noexcept
		{
			std::scoped_lock renderLock(renderTransactionMutex);
			return ResolveGroupBudgetLocked(snapshot, monitor, dpiScale,
				directRevision, deviceGeneration, false);
		}

		[[nodiscard]] bool TracePageControlPresentEnabled() noexcept
		{
			static const bool enabled = []
			{
				wchar_t value[2]{};
				return GetEnvironmentVariableW(L"INKEYS_PAGECONTROL_PRESENT_TRACE",
					value, 2) != 0 && value[0] == L'1';
			}();
			return enabled;
		}

		void TraceSurfaceStage(std::size_t index, const char* stage,
			HRESULT hr, DWORD error, const RenderSnapshot& snapshot,
			const RECT& monitor, float dpiScale,
			const PageControlSurfaceBudget& budget, const RECT& target,
			const RECT& presentation, bool recovered = false) noexcept
		{
			auto& state = surfaces[index];
			const auto now = std::chrono::steady_clock::now();
			const bool sameFailure = !recovered && state.lastFailureStage == stage
				&& state.lastFailureHr == hr && state.lastFailureError == error
				&& state.retryPublicationRevision == snapshot.revision
				&& state.retryDeviceGeneration == groupLayoutBudget.deviceGeneration
				&& state.retryBitmapLimit == budget.bitmapLimit;
			if (recovered)
			{
				state.repeatedFailureCount = 0;
				state.retryAfter = {};
			}
			else
			{
				state.repeatedFailureCount = sameFailure
					? (std::min)(state.repeatedFailureCount + 1U, 1000U) : 1U;
				state.retryPublicationRevision = snapshot.revision;
				state.retryDeviceGeneration = groupLayoutBudget.deviceGeneration;
				state.retryBitmapLimit = budget.bitmapLimit;
				// 相同确定性失败不再每个 60 FPS 节拍重做资源/ULW 事务。
				state.retryAfter = state.repeatedFailureCount < 3 ? now
					: now + std::chrono::milliseconds((std::min)(
						500U, 100U << (std::min)(state.repeatedFailureCount - 3U, 2U)));
			}
			const bool trace = TracePageControlPresentEnabled()
				&& (recovered || !sameFailure || now - state.lastFailureTrace >= 2s);
			state.lastFailureStage = recovered ? nullptr : stage;
			state.lastFailureHr = hr;
			state.lastFailureError = error;
			if (!trace) return;
			state.lastFailureTrace = now;
			char line[1536]{};
			(void)sprintf_s(line, "[PageControlPresent] %s surface=%s stage=%s "
				"session=%llu publication=%llu direct=%llu device=%llu "
				"monitor=(%ld,%ld,%ld,%ld) dpi=%.2f outset=%.2f limit=%u "
				"target=(%ld,%ld,%ld,%ld) presentation=(%ld,%ld,%ld,%ld) "
				"backing=(%ld,%ld) targetVisible=%d animated=%d hr=0x%08lX error=%lu\n",
				recovered ? "recovered" : "retry", SurfaceName(index),
				stage ? stage : "none",
				static_cast<unsigned long long>(snapshot.ppt.layout.session),
				static_cast<unsigned long long>(snapshot.revision),
				static_cast<unsigned long long>(directMoveRevision.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(groupLayoutBudget.deviceGeneration),
				monitor.left, monitor.top, monitor.right, monitor.bottom,
				dpiScale, budget.presentationOutsetDip, budget.bitmapLimit,
				target.left, target.top, target.right, target.bottom,
				presentation.left, presentation.top, presentation.right,
				presentation.bottom, state.backingCapacity.cx,
				state.backingCapacity.cy, state.targetVisible ? 1 : 0,
				state.bounds.active ? 1 : 0,
				static_cast<unsigned long>(hr),
				static_cast<unsigned long>(error));
			(void)std::fputs(line, stdout);
			OutputDebugStringA(line);
			// 同一次失败记录四个真实 HWND，避免单侧成功遮住另一侧的阶段。
			for (std::size_t item = 0; item < Roles.size(); ++item)
			{
				const HWND hwnd = Inkeys::Window::GetService().Handle(Roles[item]);
				RECT actual{};
				if (hwnd) (void)GetWindowRect(hwnd, &actual);
				(void)sprintf_s(line, "[PageControlPresent] peer=%s hwnd=0x%llX "
					"visible=%d enabled=%d owner=0x%llX exStyle=0x%llX "
					"rect=(%ld,%ld,%ld,%ld) committed=%d\n",
					SurfaceName(item),
					static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(hwnd)),
					hwnd && IsWindowVisible(hwnd) ? 1 : 0,
					hwnd && IsWindowEnabled(hwnd) ? 1 : 0,
					static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(
						hwnd ? GetWindow(hwnd, GW_OWNER) : nullptr)),
					static_cast<unsigned long long>(hwnd
						? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0),
					actual.left, actual.top, actual.right, actual.bottom,
					surfaces[item].committedPresentationReady ? 1 : 0);
				(void)std::fputs(line, stdout);
				OutputDebugStringA(line);
			}
		}

		void AcknowledgePage(const PptState& frame, std::uint8_t committedMask)
		{
			bool visible = false;
			{
				std::scoped_lock lock(snapshotMutex);
				pageCommit.Publish(publishedPpt, VisiblePptMask(publishedPpt, publishedWhiteboard));
				if (!pageCommit.Commit(frame, committedMask)) return;
				visible = pageCommit.required != 0;
			}
			std::function<void(std::uint64_t, std::uint64_t, int, int)> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = pptCallbacks.pagePresented;
			}
			Inkeys::Drawing::Draw3::TracePptTiming(visible ? "page-ui-commit" : "page-ui-hidden",
				frame.layout.session, frame.targetRevision);
			if (callback) callback(frame.layout.session, frame.targetRevision,
				frame.currentPage, frame.totalPage);
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
			const auto [monitor, dpiScale] = LayoutMonitor();
			auto renderSnapshot = SnapshotForRender();
			auto& ppt = renderSnapshot.ppt;
			const auto& whiteboard = renderSnapshot.whiteboard;
			const auto groupBudget = GroupBudgetForFrame(renderSnapshot,
				monitor, dpiScale, frameDirectMoveRevision,
				frameContext.epoch.generation);
			ppt = ResolveRuntimePageControlLayout(monitor, dpiScale, ppt,
				groupBudget.presentationOutsetDip, groupBudget.bitmapLimit);
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
			bool pptFrame = false;
			bool repeatDirection = false;
			bool repeatNext = false;
			{
				std::unique_lock renderLock(renderTransactionMutex);
				// 直移可在 frame 取快照后完成；旧帧不得先以 ULW 覆盖新 HWND 坐标。
				if (!IsPageControlFrameRevisionCurrent(frameDirectMoveRevision,
					directMoveRevision.load(std::memory_order_acquire))
					|| renderSnapshot.revision != publishedRevision.load(std::memory_order_acquire))
					return FrameResult::Retry;
				auto& state = surfaces[index];
				if (state.lastFailureStage
					&& state.retryPublicationRevision == renderSnapshot.revision
					&& state.retryDeviceGeneration == frameContext.epoch.generation
					&& state.retryBitmapLimit == groupBudget.bitmapLimit
					&& std::chrono::steady_clock::now() < state.retryAfter)
					return FrameResult::Retry;
				const char* configureFailure = nullptr;
				if (!ConfigureSurface(index, mode, ppt, whiteboard, monitor,
					target, renderSnapshot.revision, frameContext.frameTime,
					configureFailure))
				{
					state.forceFullPresentation = true;
					TraceSurfaceStage(index, configureFailure, E_FAIL,
						ERROR_SUCCESS, renderSnapshot, monitor, dpiScale,
						groupBudget, target.logicalBounds, {});
					return FrameResult::Retry;
				}
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
				SIZE presentationSize{
					presentation.right - presentation.left,
					presentation.bottom - presentation.top };
				const SIZE targetContentSize{
					target.logicalBounds.right - target.logicalBounds.left,
					target.logicalBounds.bottom - target.logicalBounds.top };
				auto backing = ResolveStableBackingSize(
					state.backingCapacity, presentationSize, targetContentSize,
					state.scene.PresentationOutsetPixels(), state.bounds.scale,
					target.scale, groupBudget.bitmapLimit);
				if (!backing.fits)
				{
					// 动画中间帧若超过新设备容量，直接落到已适配的目标再提交。
					if (mode != WorkspaceMode::PptCompact)
					{
						TraceSurfaceStage(index, "backing-limit", E_INVALIDARG,
							ERROR_SUCCESS, renderSnapshot, monitor, dpiScale,
							groupBudget, target.logicalBounds, presentation);
						return FrameResult::Retry;
					}
					RetargetBounds(state.bounds, target.logicalBounds,
						target.scale, false, frameContext.frameTime);
					if (!ApplySceneBounds(state, target.logicalBounds, target.scale))
					{
						TraceSurfaceStage(index, "scene-bounds", E_FAIL,
							ERROR_SUCCESS, renderSnapshot, monitor, dpiScale,
							groupBudget, target.logicalBounds, presentation);
						return FrameResult::Retry;
					}
					presentation = state.scene.PresentationBounds();
					presentationSize = SIZE{ presentation.right - presentation.left,
						presentation.bottom - presentation.top };
					backing = ResolveStableBackingSize(state.backingCapacity,
						presentationSize, targetContentSize,
						state.scene.PresentationOutsetPixels(), state.bounds.scale,
						target.scale, groupBudget.bitmapLimit);
					if (!backing.fits)
					{
						TraceSurfaceStage(index, "target-backing-limit", E_INVALIDARG,
							ERROR_SUCCESS, renderSnapshot, monitor, dpiScale,
							groupBudget, target.logicalBounds, presentation);
						return FrameResult::Retry;
					}
				}
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
				pptFrame = shouldShow && state.configuredMode == WorkspaceMode::PptCompact;
				if (shouldShow)
				{
					// 与发布共享短锁：新隐藏目标不能越过即将显示旧数字的在途帧。
					{
						std::scoped_lock snapshotLock(snapshotMutex);
						if (renderSnapshot.revision != publishedRevision.load(std::memory_order_acquire))
							return FrameResult::Retry;
						if (pptFrame) pageCommit.BeginSurface(static_cast<std::uint8_t>(1U << index));
					}

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
					const auto presentResult = PresentScene(index, state, hwnd,
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
					{
						state.forceFullPresentation = true;
						if (presentResult.resourcesFailed)
							state.backingCapacity = { 1, 1 };
						TraceSurfaceStage(index, presentResult.failureStage,
							presentResult.failureHr, presentResult.failureError,
							renderSnapshot, monitor, dpiScale, groupBudget,
							target.logicalBounds, presentation);
					}
				}
			}
			// 业务回调可能反向发布 UI 状态，不能在 Scene/present 事务锁内调用。
			if (repeatDirection) InvokeDirection(index, repeatNext);
			if (presentStatus != PresentStatus::Success)
				return presentStatus == PresentStatus::DeviceLost
					? FrameResult::DeviceLost : FrameResult::Retry;
			std::unique_lock presentationLock(presentationMutex);
			// 拖动期间产生的过期帧不能把已经直移的 HWND 拉回旧坐标。
			if (!IsPageControlFrameRevisionCurrent(frameDirectMoveRevision,
				directMoveRevision.load(std::memory_order_acquire))
				|| renderSnapshot.revision != publishedRevision.load(std::memory_order_acquire))
				return FrameResult::Retry;
			const auto pendingDrag = ObservePendingDrag(
				index, ppt.layout.pairVersions[DragPairIndex(index)]);
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
				(void)RollbackDragTracking(DragPairIndex(index));
				presentationLock.unlock();
				FlushCommittedDrag(DragPairIndex(index));
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
				presentationLock.unlock();
				std::unique_lock renderLock(renderTransactionMutex);
				TraceSurfaceStage(index, !boundsApplied ? "window-bounds"
					: "window-visibility", E_FAIL, ERROR_GEN_FAILURE,
					renderSnapshot, monitor, dpiScale, groupBudget,
					target.logicalBounds, presentation);
				return FrameResult::Retry;
			}
			if (state.windowCommitFailureActive)
			{
				state.windowCommitFailureActive = false;
				LogWindowCommitState(index, hwnd, shouldShow,
					boundsApplied, visibilityApplied, true);
			}
			{
				std::scoped_lock snapshotLock(snapshotMutex);
				// 即使发布在同步提交中途变化，也必须登记实际仍可见的旧 PPT 帧。
				pageCommit.FinishSurface(static_cast<std::uint8_t>(1U << index), pptFrame);
			}
			// owner 可能在同步窗口提交期间发布了更新候选；旧提交必须继续重试。
			if (!IsPageControlFrameRevisionCurrent(frameDirectMoveRevision,
				directMoveRevision.load(std::memory_order_acquire))
				|| renderSnapshot.revision != publishedRevision.load(std::memory_order_acquire))
			{
				if (pendingDrag.matched)
					TraceDrag("consume consumer=render surface=%s revision=%llu "
						"result=stale-after-window-commit pending=1",
						SurfaceName(index),
						static_cast<unsigned long long>(pendingDrag.revision));
				return FrameResult::Retry;
			}
			const auto dragCommit = CommitDragSurface(index, ppt);
			if (dragCommit.matched)
				TraceDrag("consume consumer=render surface=%s revision=%llu "
					"result=committed committed_mask=%u pending=%d",
					SurfaceName(index),
					static_cast<unsigned long long>(dragCommit.revision),
					static_cast<unsigned>(dragCommit.committedSurfaceMask),
					dragCommit.completed ? 0 : 1);
			Inkeys::UI::Bar::PublishBorderCursorSurfaceBounds(
				static_cast<unsigned int>(index), presentation, shouldShow);
			presentationLock.unlock();
			FlushCommittedDrag(DragPairIndex(index));
			AcknowledgePage(ppt, pptFrame ? static_cast<std::uint8_t>(1U << index) : 0);
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (state.lastFailureStage)
					TraceSurfaceStage(index, state.lastFailureStage, S_OK,
						ERROR_SUCCESS, renderSnapshot, monitor, dpiScale,
						groupBudget, target.logicalBounds, presentation, true);
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
					renderLock.unlock();
					FlushCommittedDrag(DragPairIndex(index));
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
						const auto [monitor, dpiScale] = LayoutMonitor();
						ppt = ResolveRuntimePageControlLayout(
							monitor, dpiScale, ppt);
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
				if (dragRelease.tracked) FlushCommittedDrag(dragPairIndex);
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
						FlushCommittedDrag(dragPairIndex);
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

	int RunOffscreenTests()
	{
		int failures = 0;
		auto Check = [&](bool valid, const char* message)
		{
			if (!valid) { ++failures; std::fprintf(stderr, "[PageControlScene] FAIL %s\n", message); }
		};
		const auto epoch = Inkeys::UI::RenderPipeline::GetDeviceEpoch();
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.currentPage = 7;
		ppt.totalPage = 28;
		ppt.layout.showMiddlePair = true;
		WhiteboardState whiteboard;
		whiteboard.currentPage = 2;
		whiteboard.totalPage = 4;
		whiteboard.previousEnabled = whiteboard.nextEnabled = true;
		whiteboard.previousInteractive = whiteboard.nextInteractive = true;
		whiteboard.expandedLayoutTarget = whiteboard.active = true;
		const auto now = std::chrono::steady_clock::now() + 1s;
		for (const auto mode : { WorkspaceMode::PptCompact, WorkspaceMode::WhiteboardExpanded })
			for (std::size_t index = 0; index < (mode == WorkspaceMode::PptCompact ? 4U : 2U); ++index)
				for (const float scale : { 0.25F, 1.0F, 5.0F, 12.0F })
				{
					Scene scene;
					const auto background = BuildBackground(index, mode);
					const auto widgets = BuildWidgets(index, mode, ppt, whiteboard);
					Check(scene.Configure(background, widgets), "product widgets configure");
					const LONG width = static_cast<LONG>(std::lround(background.bounds.right * scale));
					const LONG height = static_cast<LONG>(std::lround(background.bounds.bottom * scale));
					Check(scene.SetBounds({ 200, 100, 200 + width, 100 + height }, scale), "effective bounds accepted");
					scene.SetOpacity(1.0, 0.0);
					const LONG outset = scene.PresentationOutsetPixels();
					const auto bounds = scene.PresentationBounds();
					Check(bounds.right - bounds.left == width + 2 * outset
						&& bounds.bottom - bounds.top == height + 2 * outset,
						"presentation adds equal shadow extents on all sides");
					const UINT targetWidth = static_cast<UINT>(width + 2 * outset + 23);
					const UINT targetHeight = static_cast<UINT>(height + 2 * outset + 19);
					const HRESULT setup = scene.EnsureDeviceResources(epoch, targetWidth, targetHeight);
					Check(SUCCEEDED(setup), "actual Scene backing allocates");
					if (FAILED(setup)) continue;
					auto* context = scene.DeviceContext();
					context->BeginDraw();
					context->SetTransform(D2D1::Matrix3x2F::Identity());
					context->Clear(D2D1::ColorF(0, 0, 0, 0));
					(void)scene.Render(context, now);
					Check(SUCCEEDED(context->EndDraw()), "actual Scene renders at final scale");
					const POINT right{ width - (std::max)(1L, static_cast<LONG>(5 * scale)), height / 2 };
					const POINT bottom{ width / 2, height - (std::max)(1L, static_cast<LONG>(5 * scale)) };
					Check(scene.HitTestBackground(right) && scene.HitTestBackground(bottom),
						"right and bottom content use the same scale as layout");
					Check(!scene.PresentationToLogical({ 0, 0 }).has_value(), "transparent margin remains click through");
					for (const auto& widget : widgets)
					{
						if (!widget.visible || widget.kind == Inkeys::UI::Bar::BarSurfaceWidgetKind::DragHandle) continue;
						const auto& rectangle = widget.bounds;
						const POINT center{ static_cast<LONG>((rectangle.left + rectangle.right) * scale / 2),
							static_cast<LONG>((rectangle.top + rectangle.bottom) * scale / 2) };
						Check(scene.HitTest(center) == widget.id, "actual button hit matches rendered position");
					}
					ComPtr<ID2D1Image> target;
					context->GetTarget(&target);
					ComPtr<ID2D1Bitmap1> bitmap;
					Check(target && SUCCEEDED(target.As(&bitmap)), "render target supports bitmap readback");
					if (!bitmap) continue;
					ComPtr<ID2D1Bitmap1> readable;
					const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ
						| D2D1_BITMAP_OPTIONS_CANNOT_DRAW, bitmap->GetPixelFormat());
					const HRESULT copied = context->CreateBitmap(bitmap->GetPixelSize(), nullptr, 0, &properties, &readable);
					if (FAILED(copied) || FAILED(readable->CopyFromBitmap(nullptr, bitmap.Get(), nullptr)))
					{ Check(false, "rendered pixels read back"); continue; }
					D2D1_MAPPED_RECT mapped{};
					if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
					{ Check(false, "readback maps"); continue; }
					auto Alpha = [&](LONG x, LONG y) { return mapped.bits[y * mapped.pitch + x * 4 + 3]; };
					Check(Alpha(right.x + outset, right.y + outset) > 16
						&& Alpha(bottom.x + outset, bottom.y + outset) > 16,
						"right and bottom body pixels contain visible content instead of one-sided gaps");
					Check(Alpha(targetWidth - 1, targetHeight - 1) == 0,
						"extra backing capacity remains outside presentation content");
					readable->Unmap();
				}
		std::fprintf(stderr, "[PageControlScene] failures=%d\n", failures);
		return failures;
	}

	int RunHiddenWindowTests()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		int failures = 0;
		auto Check = [&](bool valid, const char* why)
		{
			if (!valid)
			{
				++failures;
				std::fprintf(stderr, "[PageControlHidden] FAIL %s\n", why);
			}
		};
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(com)) return 2;
		const HRESULT initializedPipeline = Inkeys::UI::RenderPipeline::Initialize();
		if (FAILED(initializedPipeline)) { CoUninitialize(); return 3; }
		auto& service = Inkeys::Window::GetService();
		const std::wstring suffix = L".PageControl.Hidden."
			+ std::to_wstring(GetCurrentProcessId());
		auto Make = [&](WindowRole role, const wchar_t* name, WNDPROC proc)
		{
			Inkeys::Window::WindowSpec spec;
			spec.role = role;
			spec.className = std::wstring(L"Inkeys") + name + suffix;
			spec.title = L"Inkeys PageControl hidden test";
			spec.x = -30000;
			spec.y = -30000;
			spec.width = 64;
			spec.height = 64;
			spec.style = WS_POPUP | WS_CLIPCHILDREN;
			spec.exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
			spec.windowProc = proc;
			spec.visible = false;
			spec.bindMessages = false;
			return spec;
		};
		std::vector<Inkeys::Window::WindowSpec> specs;
		specs.push_back(Make(WindowRole::MagnifierHost, L"Magnifier", DefWindowProcW));
		specs.push_back(Make(WindowRole::Freeze, L"Freeze", DefWindowProcW));
		specs.push_back(Make(WindowRole::DrawpadPresentation, L"Presentation", DefWindowProcW));
		specs.push_back(Make(WindowRole::Drawpad, L"Drawpad", DefWindowProcW));
		for (std::size_t index = 0; index < Roles.size(); ++index)
			specs.push_back(Make(Roles[index],
				index == 0 ? L"BottomLeft" : index == 1 ? L"BottomRight"
					: index == 2 ? L"MiddleLeft" : L"MiddleRight",
				PageControlWindowProc));
		specs.push_back(Make(WindowRole::Bar, L"Bar", DefWindowProcW));
		if (!service.Start(std::move(specs)))
		{
			Inkeys::UI::RenderPipeline::Shutdown();
			CoUninitialize();
			return 4;
		}
		hiddenTestMonitorEnabled.store(true, std::memory_order_release);
		bool acquired = Acquire();
		Check(acquired, "four production render clients register");
		if (acquired)
		{
			std::atomic_int acknowledged = 0;
			SetPptCallbacks({ .pagePresented = [&](std::uint64_t,
				std::uint64_t, int, int) { acknowledged.fetch_add(1); } });
			auto WaitFor = [&](auto&& predicate)
			{
				const auto deadline = std::chrono::steady_clock::now() + 5s;
				while (std::chrono::steady_clock::now() < deadline)
				{
					if (predicate()) return true;
					std::this_thread::sleep_for(20ms);
				}
				return predicate();
			};
			auto VisibleMask = [&]
			{
				std::uint8_t mask = 0;
				for (std::size_t index = 0; index < Roles.size(); ++index)
					if (IsWindowVisible(service.Handle(Roles[index])))
						mask |= static_cast<std::uint8_t>(1U << index);
				return mask;
			};
			PptState ppt;
			ppt.layout.session = 1;
			ppt.layout.epoch = 1;
			ppt.publicationRevision = 1;
			ppt.targetRevision = 1;
			ppt.presentationVisible = true;
			ppt.currentPage = 1;
			ppt.totalPage = 3;
			ppt.layout.bottomPairScale = 3.0F;
			ppt.layout.middlePairScale = 3.0F;
			// 左窗 ULW 一直失败，右窗可先成功，但绝不能提前满足页码回执。
			hiddenTestBlockedUlwSurface.store(0, std::memory_order_release);
			PublishPptState(ppt);
			Check(WaitFor([&] { return VisibleMask() == 2; })
				&& acknowledged.load() == 0,
				"one-sided ULW failure leaves peer visible without page ack");
			hiddenTestBlockedUlwSurface.store(-1, std::memory_order_release);
			Check(WaitFor([&] { return VisibleMask() == 3
				&& acknowledged.load() == 1; }),
				"failed peer retries and completes without new pointer or publication");
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				surfaces[0].backingCapacity = { 1024, 1024 };
				surfaces[0].forceFullPresentation = true;
			}
			hiddenTestBlockedResourceSurface.store(0, std::memory_order_release);
			ppt.currentPage = 2;
			++ppt.targetRevision;
			++ppt.publicationRevision;
			PublishPptState(ppt);
			Check(WaitFor([&]
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				return surfaces[0].backingCapacity.cx == 1
					&& surfaces[0].backingCapacity.cy == 1;
			}) && acknowledged.load() == 1,
				"resource failure drops only the failed side's stale backing high-water mark");
			hiddenTestBlockedResourceSurface.store(-1, std::memory_order_release);
			Check(WaitFor([&] { return VisibleMask() == 3
				&& acknowledged.load() == 2; }),
				"resource retry restores the current page without a new input event");
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				Check(surfaces[0].committedBackingCapacity.cx < 1024
					&& surfaces[0].committedBackingCapacity.cy < 1024,
					"recovery allocation fits current presentation instead of stale capacity");
			}
			ppt.layout.showBottomPair = false;
			ppt.layout.showMiddlePair = true;
			++ppt.publicationRevision;
			PublishPptState(ppt);
			Check(WaitFor([&] { return VisibleMask() == 12; }),
				"side-only pair enters from legal offscreen starts and settles");

			constexpr RECT monitor{ -30000, -30000, -28080, -28920 };
			for (const UINT dpi : { 96U, 144U, 192U, 240U })
			{
				hiddenTestBudgetOverrideEnabled.store(dpi == 240U,
					std::memory_order_release);
				hiddenTestDpi.store(dpi, std::memory_order_release);
				ppt.layout.showBottomPair = true;
				ppt.layout.showMiddlePair = true;
				++ppt.publicationRevision;
				PublishPptState(ppt);
				// 测试 DPI 原子不是 Display 事件；显式走产品布局唤醒入口。
				NotifyLayoutChanged();
				const auto expectedRevision = publishedRevision.load(
					std::memory_order_acquire);
				const bool allSettled = WaitFor([&]
				{
					if (VisibleMask() != 15) return false;
					std::scoped_lock renderLock(renderTransactionMutex);
					const auto now = std::chrono::steady_clock::now();
					for (const auto& state : surfaces)
						if (state.observedRevision != expectedRevision
							|| state.bounds.active || state.scene.AnimationActive()
							|| now < state.layoutTransitionUntil
							|| !state.committedPresentationReady)
							return false;
					return true;
				});
				Check(allSettled, "all four ULW/HWND surfaces settle at large scale");
				if (!allSettled) continue;
				std::scoped_lock renderLock(renderTransactionMutex);
				const auto budget = groupLayoutBudget.budget;
				if (dpi == 240U)
					Check(budget.bitmapLimit == 512
						&& budget.presentationOutsetDip >= 35.0F,
						"different old peer budgets resolve once for the four real windows");
				const auto fitted = ResolveRuntimePageControlLayout(monitor,
					static_cast<float>(dpi) / 96.0F, ppt,
					budget.presentationOutsetDip, budget.bitmapLimit);
				for (std::size_t index = 0; index < Roles.size(); ++index)
				{
					const auto body = surfaces[index].scene.LogicalBounds();
					const auto expected = ResolveSurfaceLayout(SurfaceFor(index),
						monitor, static_cast<float>(dpi) / 96.0F, fitted, {});
					RECT actual{};
					const HWND hwnd = service.Handle(Roles[index]);
					const RECT expectedPresentation =
						surfaces[index].scene.PresentationBounds();
					Check(hwnd && GetWindowRect(hwnd, &actual)
						&& EqualRect(&actual, &expectedPresentation),
						"real PPT HWND matches successful Scene presentation bounds");
					const bool bodyInside = body.left >= monitor.left
						&& body.right <= monitor.right && body.top >= monitor.top
						&& body.bottom <= monitor.bottom;
					const bool bodyMatches = EqualRect(&body,
						&expected.logicalBounds) != FALSE;
					if (!bodyInside || !bodyMatches)
						std::fprintf(stderr, "[PageControlHidden] dpi=%u side=%s "
							"body=(%ld,%ld,%ld,%ld) expected=(%ld,%ld,%ld,%ld) "
							"inside=%d match=%d\n", dpi, SurfaceName(index),
							body.left, body.top, body.right, body.bottom,
							expected.logicalBounds.left, expected.logicalBounds.top,
							expected.logicalBounds.right, expected.logicalBounds.bottom,
							bodyInside ? 1 : 0, bodyMatches ? 1 : 0);
					Check(bodyInside && bodyMatches,
						"each final visible body is inside the offscreen target monitor");
					const POINT center{ (body.right - body.left) / 2,
						(body.bottom - body.top) / 2 };
					Check(surfaces[index].scene.HitTestBackground(center),
						"visible body keeps its input hit geometry");
					ComPtr<ID2D1Image> target;
					auto* context = surfaces[index].scene.DeviceContext();
					if (context) context->GetTarget(&target);
					ComPtr<ID2D1Bitmap1> bitmap;
					if (!target || FAILED(target.As(&bitmap)))
					{ Check(false, "visible surface has a readable bitmap"); continue; }
					ComPtr<ID2D1Bitmap1> readable;
					const auto properties = D2D1::BitmapProperties1(
						D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
						bitmap->GetPixelFormat());
					if (FAILED(context->CreateBitmap(bitmap->GetPixelSize(),
						nullptr, 0, &properties, &readable))
						|| FAILED(readable->CopyFromBitmap(nullptr, bitmap.Get(), nullptr)))
					{ Check(false, "visible surface pixels copy back"); continue; }
					D2D1_MAPPED_RECT mapped{};
					if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
					{ Check(false, "visible surface pixels map"); continue; }
					const LONG outset = surfaces[index].scene.PresentationOutsetPixels();
					const LONG sampleX = center.x + outset;
					const LONG sampleY = center.y + outset;
					const auto pixelSize = bitmap->GetPixelSize();
					const bool sampleInside = sampleX >= 0 && sampleY >= 0
						&& static_cast<UINT>(sampleX) < pixelSize.width
						&& static_cast<UINT>(sampleY) < pixelSize.height;
					Check(sampleInside, "body readback sample fits backing bitmap");
					if (sampleInside)
						Check(mapped.bits[sampleY * mapped.pitch + sampleX * 4 + 3] > 16,
							"both sides carry nontransparent current body pixels");
					readable->Unmap();
				}
			}
			hiddenTestBudgetOverrideEnabled.store(false,
				std::memory_order_release);
			ppt.layout.bottomPairScale = ppt.layout.middlePairScale = 1.0F;
			++ppt.publicationRevision;
			PublishPptState(ppt);
			const auto smallerRevision = publishedRevision.load(
				std::memory_order_acquire);
			Check(WaitFor([&]
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				for (const auto& state : surfaces)
					if (state.observedRevision != smallerRevision || state.bounds.active)
						return false;
				return true;
			}), "all four controls settle after large-to-small setting change");
			ppt.layout.bottomPairScale = ppt.layout.middlePairScale = 3.0F;
			++ppt.publicationRevision;
			PublishPptState(ppt);
			const auto largerRevision = publishedRevision.load(
				std::memory_order_acquire);
			Check(WaitFor([&]
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				for (const auto& state : surfaces)
					if (state.observedRevision != largerRevision || state.bounds.active)
						return false;
				return true;
			}), "all four controls settle after small-to-large setting change");
			WhiteboardState whiteboard;
			whiteboard.active = whiteboard.expandedLayoutTarget = true;
			PublishWhiteboardState(whiteboard);
			Check(WaitFor([&] { return VisibleMask() == 3; }),
				"whiteboard uses only the shared bottom pair");
			PublishWhiteboardState({});
			Check(WaitFor([&] { return VisibleMask() == 15; }),
				"PPT four-surface layout returns after whiteboard coverage");
			SetPptCallbacks({});
			Release();
		}
		hiddenTestBlockedUlwSurface.store(-1, std::memory_order_release);
		hiddenTestBlockedResourceSurface.store(-1, std::memory_order_release);
		hiddenTestMonitorEnabled.store(false, std::memory_order_release);
		hiddenTestBudgetOverrideEnabled.store(false, std::memory_order_release);
		service.StopAndJoin();
		Inkeys::UI::RenderPipeline::Shutdown();
		CoUninitialize();
		std::fprintf(stderr, "[PageControlHidden] failures=%d\n", failures);
		return failures ? 1 : 0;
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
			{
				{
					std::scoped_lock lock(snapshotMutex);
					pageCommit.FinishSurface(static_cast<std::uint8_t>(1U << index), false);
				}
				Inkeys::UI::Bar::PublishBorderCursorSurfaceBounds(
					static_cast<unsigned int>(index), {}, false);
			}
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
		groupLayoutBudget = {};
	}

	WNDPROC WindowProc() noexcept { return PageControlWindowProc; }

	void FlushPositionCommits()
	{
		// 保存边沿汇入已经成功提交但回调尚未运行的几何，不等待窗口或候选。
		for (std::size_t pair = 0; pair < dragCommitTrackers.size(); ++pair)
			FlushCommittedDrag(pair);
	}

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
		bool changed = false;
		{
			std::scoped_lock dragLock(dragCommitMutex);
			std::scoped_lock snapshotLock(snapshotMutex);
			if (!MergePptPublication(publishedPpt, merged)) return;
			if (merged.layout.session != publishedPpt.layout.session
				|| merged.layout.epoch != publishedPpt.layout.epoch)
			{
				dragCommitTrackers = {};
				handedOffDragVersions = {};
				directMoveRevision.fetch_add(1, std::memory_order_release);
			}
			PreserveOwnedDragLayouts(merged);
			changed = !ArePptStatesEquivalent(publishedPpt, merged);
			publishedPpt = merged;
			if (changed) publishedRevision.fetch_add(1, std::memory_order_release);
		}
		AcknowledgePage(merged, 0);
		if (changed) RequestAll();
	}

	void PublishWhiteboardState(const WhiteboardState& state) noexcept
	{
		{
			std::scoped_lock lock(snapshotMutex);
			if (AreWhiteboardStatesEquivalent(publishedWhiteboard, state)) return;
			publishedWhiteboard = state;
			publishedRevision.fetch_add(1, std::memory_order_release);
		}
		AcknowledgePage(Snapshot().first, 0);
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
			FlushCommittedDrag(pairIndex);
		}
		RequestAll();
	}
}
