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
#include "../Whiteboard/Whiteboard.Theme.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
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
import Inkeys.UI.Bar.Animation;
import Inkeys.UI.Bar.EraserAttributeMotion;
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
		using FrameStage = Inkeys::UI::RenderPipeline::FrameStage;
		using FrameStageTimer = Inkeys::UI::RenderPipeline::FrameStageTimer;
		using Scene = Inkeys::UI::Bar::BarSurfaceScene;
		using WidgetSpec = Inkeys::UI::Bar::BarSurfaceWidgetSpec;
		using WidgetId = Inkeys::UI::Bar::BarSurfaceWidgetId;
		using WindowRole = Inkeys::Window::WindowRole;

		constexpr WidgetId DragWidget = 1;
		constexpr WidgetId PreviousWidget = 2;
		constexpr WidgetId PageWidget = 3;
		constexpr WidgetId NextWidget = 4;
		constexpr WidgetId MoreWidget = 5;
		void CloseWhiteboardMenu() noexcept;
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
			WhiteboardState renderedWhiteboard;
			double envelopeOffset = 0.0;
			int envelopeMenu = 0;
			WhiteboardState pressedWhiteboard;
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
		std::atomic_bool whiteboardMenuOpen = false;
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
			CloseWhiteboardMenu();
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
			std::size_t index, WorkspaceMode mode, int menu = 0, bool unsaved = false, double envelopeOffset = -1.0)
		{
			Inkeys::UI::Bar::BarSurfaceBackgroundSpec background;
			const bool whiteboard = mode == WorkspaceMode::WhiteboardExpanded;
			const bool vertical = index >= 2;
			const double offset = whiteboard && index == 0 ? (envelopeOffset >= 0 ? envelopeOffset : (menu ? std::ceil((std::max)(0.0,-WhiteboardPopupEnvelope(menu).top))+5.0 : 0.0) + (unsaved ? 32.0 : 0.0)) : 0.0;
			background.bounds = { 0.0, offset,
				whiteboard ? (index == 0 ? 305.0 : WhiteboardWidthDip)
					: vertical ? PptCompactShortSideDip : PptCompactLongSideDip,
				offset + (whiteboard ? WhiteboardHeightDip
					: vertical ? PptCompactLongSideDip : PptCompactShortSideDip) };
			background.contentEnvelope=whiteboard && index==0;
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

		void CloseWhiteboardMenu() noexcept
		{
			{ std::scoped_lock lock(snapshotMutex);
			  if(!publishedWhiteboard.menu) return;
			  publishedWhiteboard.menu=0; whiteboardMenuOpen.store(false,std::memory_order_release); publishedRevision.fetch_add(1,std::memory_order_release); }
			RequestAll();
		}

		void InvokeWhiteboardMenu(WidgetId id, const WhiteboardState& pressed)
		{
			WhiteboardCallbacks callbacks;
			{ std::scoped_lock lock(callbackMutex); callbacks=whiteboardCallbacks; }
			const auto state=Snapshot().second;
			if(!state.active || state.switching) return;
			if(id==5 || id==8) {
				{ std::scoped_lock lock(snapshotMutex);
				  publishedWhiteboard.menu=id==5 ? (state.menu ? 0:1) : (state.menu==2 ? 1:2);
				  whiteboardMenuOpen.store(publishedWhiteboard.menu!=0,std::memory_order_release);
				  publishedRevision.fetch_add(1,std::memory_order_release); }
				RequestAll(); return;
			}
			if(id==6 || id==7) {
				CloseWhiteboardMenu();
				if(id==6 && callbacks.closeWhiteboard) callbacks.closeWhiteboard();
				if(id==7 && !Snapshot().first.presentationVisible) (void)Inkeys::Window::GetService().MinimizeWhiteboardWindowGroup();
			}
			constexpr std::uint32_t colors[]={0x123B32,0xFFFFFF,0xF5F1E6,0xE5E7EB,0x25282D,0x172A46};
			if(id>=9 && id<=17 && callbacks.changeTheme)
				callbacks.changeTheme(id<15 ? colors[id-9] : pressed.backgroundRgb,
					id<15 ? pressed.pattern : static_cast<int>(id-15), pressed.generation, pressed.currentPage);
		}

		[[nodiscard]] std::vector<WidgetSpec> BuildWidgets(
			std::size_t index, WorkspaceMode mode,
			const PptState& ppt, const WhiteboardState& whiteboard, double envelopeOffset = -1.0)
		{
			std::vector<WidgetSpec> widgets(23);
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
			previous.primaryText = mode == WorkspaceMode::WhiteboardExpanded ? IW(I18nKey.UI.Bar.MainButtons.WhiteboardPreviousLabel) : std::wstring(previousContent.label);
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
			next.primaryText = mode == WorkspaceMode::WhiteboardExpanded ? IW(whiteboard.nextIsAdd ? I18nKey.UI.Bar.MainButtons.WhiteboardAddPageLabel : I18nKey.UI.Bar.MainButtons.WhiteboardNextLabel) : std::wstring(nextContent.label);
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
			// 所有布局保留同一组实例，PPT/白板切换和菜单反向动画不重建按钮。
			for (std::size_t i=4;i<widgets.size();++i) {
				auto& w=widgets[i]; w.id=static_cast<WidgetId>(i+1);
				w.visible=false; w.interactive=false; w.bounds={230,5,300,75}; ApplyDarkTheme(w);
			}
			if (index==0 && mode==WorkspaceMode::WhiteboardExpanded) {
				using namespace Inkeys::UI::Bar;
				constexpr double gap=BarButtonGapDip, one=BarButtonOneSideDip, two=BarButtonTwoSideDip;
				constexpr double menuLeft=190.0, menuTop=-122.5, themeLeft=345.0, themeTop=-301.0;
				auto& more=widgets[4]; more.visible=true; more.interactive=!whiteboard.switching;
				more.layoutKind=BarButtonVisualLayoutKind::StandardTwoTwo;
				more.iconResource=L"barMenu"; more.primaryText=IW(I18nKey.UI.Bar.MainButtons.MoreLabel);
				const auto row=[&](std::size_t i, const std::wstring& text, const wchar_t* icon) {
					auto& w=widgets[i]; w.visible=whiteboard.menu!=0; w.interactive=w.visible&&!whiteboard.switching;
					w.parentPanel=19; w.layoutKind=BarButtonVisualLayoutKind::StandardFourOne; w.primaryLeftAligned=true;
					const double top=menuTop+gap+(i-5)*(one+gap);
					w.bounds={menuLeft+gap,top,menuLeft+gap+140,top+one}; w.primaryText=text; w.iconResource=icon;
				};
				row(5,IW(I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel),L"barCloseSmall");
				row(6,IW(I18nKey.UI.Bar.MainButtons.MinimizeWhiteboardLabel),L"barMinimize");
				row(7,IW(I18nKey.UI.Bar.MainButtons.WhiteboardThemeLabel),L"barPalette");
				widgets[6].enabled=widgets[6].interactive=widgets[6].interactive&&!ppt.presentationVisible;
				constexpr std::uint32_t colors[]={0x123B32,0xFFFFFF,0xF5F1E6,0xE5E7EB,0x25282D,0x172A46};
				const std::wstring labels[]={IW(I18nKey.UI.Bar.MainButtons.WhiteboardBlankLabel),
					IW(I18nKey.UI.Bar.MainButtons.WhiteboardDotsLabel),IW(I18nKey.UI.Bar.MainButtons.WhiteboardGridLabel)};
				for (std::size_t i=0;i<9;++i) {
					auto& w=widgets[8+i]; w.visible=whiteboard.menu==2; w.interactive=w.visible&&!whiteboard.switching;
					w.parentPanel=20; w.layoutKind=BarButtonVisualLayoutKind::StandardTwoTwo;
					const auto rgb=i<6?colors[i]:whiteboard.backgroundRgb;
					const int pattern=i<6?0:static_cast<int>(i-6);
					w.selectionIndicator=true; w.selectionVisible=i<6?rgb==whiteboard.backgroundRgb:pattern==whiteboard.pattern;
					w.iconResource=L"colorSelect";
					const double x=themeLeft+gap+(i%3)*(two+gap);
					const double y=themeTop+(i<6?35.0+(i/3)*(two+gap):221.0);
					w.bounds={x,y,x+two,y+two};
					if(i>=6) {
						w.primaryText=labels[i-6];
						const unsigned luminance=((rgb>>16)&255)*299+((rgb>>8)&255)*587+(rgb&255)*114;
						w.content=GetThemeColor(luminance>128000?BarThemeModeEnum::Light:BarThemeModeEnum::Dark,
							BarThemeColorEnum::TextPrimary);
					}
					w.contentRevision=static_cast<std::uint64_t>(rgb)*4+pattern;
					auto resources=std::make_shared<WhiteboardVisual::ThemeDrawingResources>();
					w.releaseContentResources=[resources] { *resources={}; };
					w.drawContent=[rgb,pattern,resources,labelled=i>=6](auto* dc,auto rect) {
						WhiteboardVisual::DrawTheme(dc,rect,rgb,pattern,true,false,resources.get(),labelled);
					};
				}
				auto& menu=widgets[18]; menu.panel=true; menu.visible=whiteboard.menu!=0;
				menu.popupAnchor=MoreWidget; menu.interactive=false; menu.bounds={menuLeft,menuTop,340,-5};
				auto& theme=widgets[19]; theme.panel=true; theme.visible=whiteboard.menu==2;
				theme.popupAnchor=8; theme.parentPanel=19; theme.interactive=false; theme.bounds={themeLeft,themeTop,575,-5};
				const auto header=[&](std::size_t i,double y,const std::wstring& title) {
					auto& w=widgets[i]; w.visible=whiteboard.menu==2; w.parentPanel=20; w.header=true; w.interactive=false;
					w.bounds={themeLeft+gap,y,570,y+25}; w.primaryText=title;
					w.primaryFontSizeDip=12; w.primaryOffsetYDip=0; w.primarySlotHeightDip=25;
					w.primaryLeftAligned=true;
				};
				header(20,themeTop+gap,IW(I18nKey.UI.Bar.MainButtons.WhiteboardBackgroundColorLabel));
				header(21,themeTop+191,IW(I18nKey.UI.Bar.MainButtons.WhiteboardBackgroundStyleLabel));
				auto& divider=widgets[22]; divider.visible=whiteboard.menu==2; divider.parentPanel=20;
				divider.divider=true; divider.interactive=false; divider.bounds={themeLeft+gap,themeTop+185,570,themeTop+186};
				const double offset=envelopeOffset>=0?envelopeOffset:WhiteboardMenuHeight(whiteboard);
				for(auto& widget:widgets) { widget.bounds.top+=offset; widget.bounds.bottom+=offset; }
				auto& status=widgets[17]; status.visible=whiteboard.unsaved;
				status.interactive=false; status.bounds={5,5,300,32};
				status.primaryText=IW(I18nKey.UI.Bar.MainButtons.WhiteboardUnsavedLabel);
				status.primaryOffsetYDip=0; status.primaryFontSizeDip=14; status.primarySlotHeightDip=20;
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
			const RECT& monitor, const ResolvedSurfaceLayout& target, const WhiteboardState& envelopeState,
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
			const double offset=index==0 && visualMode==WorkspaceMode::WhiteboardExpanded?WhiteboardMenuHeight(envelopeState):0.0;
			if (state.sceneConfigured && !modeChanged && offset!=state.envelopeOffset)
				state.scene.RebaseLocalOrigin(offset-state.envelopeOffset);
			const bool popupEnvelopeChanged=offset!=state.envelopeOffset;
			state.envelopeOffset=offset; state.envelopeMenu=envelopeState.menu;

			if (!state.sceneConfigured)
			{
				const auto background = BuildBackground(index, visualMode, whiteboard.menu, whiteboard.unsaved, offset);
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard, offset);
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
			else if (modeChanged || ((state.renderedWhiteboard.menu!=whiteboard.menu || popupEnvelopeChanged) && visualMode == WorkspaceMode::WhiteboardExpanded))
			{
				const auto background = BuildBackground(index, visualMode, whiteboard.menu, whiteboard.unsaved, offset);
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard, offset);
				const bool animateLayout = ShouldAnimateWorkspaceLayout(SurfaceFor(index),
						state.configuredMode, visualMode, state.targetVisible);
				if (!state.scene.TransitionLayout(background, widgets,
					(animateLayout || whiteboard.menu != 0 || visualMode == WorkspaceMode::WhiteboardExpanded) ? LayoutTransitionDurationSeconds() : 0.0))
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
				const auto widgets = BuildWidgets(index, visualMode, ppt, whiteboard, offset);
				if (visualMode==WorkspaceMode::WhiteboardExpanded) {
					if(!state.scene.UpdateContent(widgets)) { failedStage="scene-content"; return false; }
				}
				else for (const auto& widget : widgets)
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
			const bool animateBounds = !popupEnvelopeChanged && state.bounds.initialized
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
			state.renderedWhiteboard = whiteboard;
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
			auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics();
			if (diagnostics)
			{
				diagnostics->capacitySize = backingCapacity;
				diagnostics->viewport = presentationBounds;
				diagnostics->source = {};
				diagnostics->zoom = state.bounds.scale;
			}
			const UINT width = static_cast<UINT>(presentationBounds.right
				- presentationBounds.left);
			const UINT height = static_cast<UINT>(presentationBounds.bottom
				- presentationBounds.top);
			if (!hwnd || width == 0 || height == 0)
			{
				result.failureStage = "invalid-presentation";
				result.failureError = ERROR_INVALID_WINDOW_HANDLE;
				if (diagnostics) diagnostics->presentFailed = true;
				return result;
			}
			const HRESULT resourceHr = hiddenTestBlockedResourceSurface.load(
				std::memory_order_relaxed) == static_cast<int>(index)
				? E_OUTOFMEMORY : state.scene.EnsureDeviceResources(
					frameContext.epoch,
					static_cast<UINT>((std::max)(1L, backingCapacity.cx)),
					static_cast<UINT>((std::max)(1L, backingCapacity.cy)));
			if (diagnostics)
			{
				diagnostics->resourceResult = resourceHr;
				diagnostics->presentFailed = FAILED(resourceHr);
				// Ensure 成功时实际 target 尺寸就是这次申请的 backing 容量。
				if (SUCCEEDED(resourceHr))
					diagnostics->targetSize = { (std::max)(1L, backingCapacity.cx),
						(std::max)(1L, backingCapacity.cy) };
			}
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
				if (diagnostics)
				{
					diagnostics->resourceResult = E_POINTER;
					diagnostics->presentFailed = true;
				}
				result.failureStage = "context";
				result.failureHr = E_POINTER;
				return result;
			}
			ComPtr<ID2D1DeviceContext> context(rawContext);
			ComPtr<ID2D1GdiInteropRenderTarget> gdi(rawGdi);
			FrameStageTimer drawTimer(diagnostics, FrameStage::Draw);
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
			drawTimer.Stop();
			HDC source = nullptr;
			bool presented = false;
			DWORD ulwError = ERROR_SUCCESS;
			if (diagnostics) diagnostics->presentAttempted = true;
			FrameStageTimer getDcTimer(diagnostics, FrameStage::GetDC);
			HRESULT getDcHr = gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &source);
			getDcTimer.Stop();
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
				if (diagnostics) diagnostics->ulwAttempted = true;
				FrameStageTimer ulwTimer(diagnostics, FrameStage::ULW);
				if (hiddenTestBlockedUlwSurface.load(std::memory_order_relaxed)
					== static_cast<int>(index))
					ulwError = ERROR_GEN_FAILURE;
				else
				{
					SetLastError(ERROR_SUCCESS);
					presented = UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
					if (!presented) ulwError = GetLastError();
				}
				ulwTimer.Stop();
				// ULW 只读取源 DC；这里没有通过 GDI 修改像素。
				const RECT gdiModifiedRect{};
				FrameStageTimer releaseDcTimer(diagnostics, FrameStage::ReleaseDC);
				releaseDcHr = gdi->ReleaseDC(&gdiModifiedRect);
				releaseDcTimer.Stop();
			}
			else if (SUCCEEDED(getDcHr)) getDcHr = E_FAIL;
			FrameStageTimer endDrawTimer(diagnostics, FrameStage::EndDraw);
			const HRESULT endDrawHr = context->EndDraw();
			endDrawTimer.Stop();
			if (diagnostics)
			{
				diagnostics->getDcResult = getDcHr;
				diagnostics->releaseDcResult = releaseDcHr;
				diagnostics->endDrawResult = endDrawHr;
				diagnostics->ulwError = ulwError;
				diagnostics->ulwSucceeded = presented;
				diagnostics->presentFailed = FAILED(getDcHr) || FAILED(releaseDcHr)
					|| FAILED(endDrawHr) || !presented;
				diagnostics->presentCommitted = !diagnostics->presentFailed;
			}
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
			auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics();
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
			const WorkspaceMode mode = whiteboard.active && Inkeys::Window::GetService().WhiteboardWindowGroupMinimized()
				? WorkspaceMode::Hidden : ResolveWorkspaceMode(surface, ppt, whiteboard);
			auto envelopeState=whiteboard;
			if (index==0 && whiteboard.active && surfaces[index].scene.PopupAnimationActive())
				envelopeState.menu=(std::max)(whiteboard.menu,surfaces[index].envelopeMenu);
			auto target = ResolveSurfaceLayout(surface, monitor,
				dpiScale, ppt, envelopeState);
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
				FrameStageTimer renderLockTimer(diagnostics, FrameStage::PresentLockWait);
				std::unique_lock renderLock(renderTransactionMutex);
				renderLockTimer.Stop();
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
					target, envelopeState, renderSnapshot.revision, frameContext.frameTime,
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
				(void)state.scene.Advance(frameContext.frameTime);
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
			FrameStageTimer presentationLockTimer(diagnostics, FrameStage::PresentLockWait);
			std::unique_lock presentationLock(presentationMutex);
			presentationLockTimer.Stop();
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
			if ((message == WM_KEYDOWN && wParam == VK_ESCAPE) || message == WM_CANCELMODE)
				CloseWhiteboardMenu();
			if (message == WM_NCHITTEST)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible) return HTTRANSPARENT;
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
					backgroundHit, widgetHit, state.inputLocked,
					index == 0 && Snapshot().second.menu != 0) ? HTCLIENT : HTTRANSPARENT;
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
					state.pressedWhiteboard = state.renderedWhiteboard;
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
				WhiteboardState pressedWhiteboard;
				Inkeys::UI::Bar::BarSurfaceWidgetId clicked =
					Inkeys::UI::Bar::BarSurfaceNoWidget;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					const bool acceptInput = !state.inputLocked && state.targetVisible;
					shouldInvokeClick = acceptInput;
					releasedMode = state.configuredMode;
					pressedWhiteboard = state.pressedWhiteboard;
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
				else if(shouldInvokeClick && releasedMode==WorkspaceMode::WhiteboardExpanded && clicked>=MoreWidget)
					InvokeWhiteboardMenu(clicked, pressedWhiteboard);
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
		for (const int menu : {0,1,2})
		for (const auto mode : { WorkspaceMode::PptCompact, WorkspaceMode::WhiteboardExpanded })
			for (std::size_t index = 0; index < (mode == WorkspaceMode::PptCompact ? 4U : 2U); ++index)
				for (const float scale : { 0.25F, 1.0F, 5.0F, 12.0F })
				{
					if(menu && (index!=0 || mode!=WorkspaceMode::WhiteboardExpanded)) continue;
					whiteboard.menu=menu;
					Scene scene;
					auto background = BuildBackground(index, mode, menu);
					background.contentEnvelope=false;
					const auto widgets = BuildWidgets(index, mode, ppt, whiteboard);
					Check(scene.Configure(background, widgets), "product widgets configure");
					const LONG width = static_cast<LONG>(std::lround((menu ? std::ceil((std::max)(305.0,WhiteboardPopupEnvelope(menu).right))+5.0 : background.bounds.right) * scale));
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
					for(int frame=0;frame<=40;++frame) (void)scene.Advance(now-std::chrono::milliseconds(2000-frame*50));
					Check(!scene.AnimationActive(),"settled popup shares finite attribute timeline without repeated target changes");
					context->BeginDraw();
					context->SetTransform(D2D1::Matrix3x2F::Identity());
					context->Clear(D2D1::ColorF(0, 0, 0, 0));
					(void)scene.Render(context, now);
					Check(SUCCEEDED(context->EndDraw()), "actual Scene renders at final scale");
					const POINT right{ static_cast<LONG>(background.bounds.right*scale) - (std::max)(1L, static_cast<LONG>(5 * scale)), static_cast<LONG>((background.bounds.top+background.bounds.bottom)*scale/2) };
					const POINT bottom{ static_cast<LONG>(background.bounds.right*scale/2), height - (std::max)(1L, static_cast<LONG>(5 * scale)) };
					Check(scene.HitTestBackground(right) && scene.HitTestBackground(bottom),
						"right and bottom content use the same scale as layout");
					Check(!scene.PresentationToLogical({ 0, 0 }).has_value(), "transparent margin remains click through");
					for (const auto& widget : widgets)
					{
						if (!widget.visible || !widget.enabled || !widget.interactive || widget.panel || widget.kind == Inkeys::UI::Bar::BarSurfaceWidgetKind::DragHandle) continue;
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
					if (mode == WorkspaceMode::WhiteboardExpanded && scale >= 1.0F) {
						for(const auto slot : {1,3,4}) {
							if(slot==4 && index!=0) continue;
							const auto& rect=widgets[slot].bounds; int bright=0;
							for(LONG y=static_cast<LONG>((rect.top+45)*scale);y<static_cast<LONG>((rect.top+65)*scale);++y)
								for(LONG x=static_cast<LONG>((rect.left+4)*scale);x<static_cast<LONG>((rect.right-4)*scale);++x) {
									const auto* p=mapped.bits+(y+outset)*mapped.pitch+(x+outset)*4;
									if(p[0]>180 && p[1]>180 && p[2]>180) ++bright;
								}
							Check(bright>0,"standard 2x2 direction and More label pixels are visible");
						}
					}
					if (menu == 2)
					{
						constexpr std::uint32_t colors[] = { 0x123B32, 0xFFFFFF, 0xF5F1E6, 0xE5E7EB, 0x25282D, 0x172A46 };
						for (std::size_t color = 0; color < 6; ++color)
						{
							const auto& rect = widgets[8 + color].bounds;
							const LONG x = static_cast<LONG>((rect.left + 10) * scale) + outset;
							const LONG y = static_cast<LONG>((rect.top + 10) * scale) + outset;
							const auto* pixel = mapped.bits + y * mapped.pitch + x * 4;
							Check(pixel[0] == (colors[color] & 255)
								&& pixel[1] == ((colors[color] >> 8) & 255)
								&& pixel[2] == ((colors[color] >> 16) & 255),
								"swatch center pixels reproduce the six product background colors");
						}
						Check(widgets[5].bounds.Width()==140 && widgets[5].bounds.Height()==32.5,
							"menu operation rows use exact standard 140x32.5 DIP");
						Check((widgets[18].bounds.left+widgets[18].bounds.right)/2==(widgets[4].bounds.left+widgets[4].bounds.right)/2,
							"menu center inherits More button center");
						Check(widgets[19].bounds.left-widgets[18].bounds.right==5 && background.bounds.top-widgets[18].bounds.bottom==5,
							"popup gaps use the shared 5 DIP metric");
						Check(widgets[20].header && widgets[21].header && widgets[22].divider,
							"theme grouping uses shared UI text and divider shape");
						Check(widgets[8].selectionIndicator && widgets[8].iconResource==L"colorSelect",
							"theme selection uses existing pen-color SVG resource");
						if (scale >= 1.0F)
						{
							// 直接检查真实 Scene 的预览像素，不能仅断言 painter 回调存在。
							auto PreviewRed = [&](std::size_t style, double x, double y)
							{
								const auto& rect = widgets[14 + style].bounds;
								const LONG px = static_cast<LONG>((rect.left + x) * scale) + outset;
								const LONG py = static_cast<LONG>((rect.top + y) * scale) + outset;
								return mapped.bits[py * mapped.pitch + px * 4 + 2];
							};
							Check(PreviewRed(1, 11, 25) > PreviewRed(0, 11, 25),
								"dot preview contains a visible dot over the blank background");
							Check(PreviewRed(2, 11, 34) > PreviewRed(1, 11, 34),
								"grid preview contains a continuous line outside the dot footprint");
						}
					}
					if(menu==2 && scale==1.0F) {
						// 原 SVG 的绿圆可见直径约 22 DIP，对应 30 DIP 盒；旧 15 DIP 图标不能通过。
						const auto& selected=widgets[8].bounds; int minX=70,maxX=-1,minY=70,maxY=-1;
						for(int y=0;y<70;++y) for(int x=0;x<70;++x) {
							const auto* pixel=mapped.bits+(static_cast<LONG>(selected.top)+y+outset)*mapped.pitch+(static_cast<LONG>(selected.left)+x+outset)*4;
							if(pixel[0]>=100 && pixel[0]<=155 && pixel[1]>180 && pixel[2]<30) {
								minX=(std::min)(minX,x); maxX=(std::max)(maxX,x); minY=(std::min)(minY,y); maxY=(std::max)(maxY,y);
							}
						}
						Check(maxX-minX+1>=21 && maxX-minX+1<=24 && maxY-minY+1>=21 && maxY-minY+1<=24,
							"selected SVG retains original green fill at twice pen-color selector size");
						for(const int slot:{14,15,16}) {
							const auto& rect=widgets[slot].bounds; int bright=0;
							for(int y=45;y<65;++y) for(int x=4;x<66;++x) {
								const auto* pixel=mapped.bits+(static_cast<LONG>(rect.top)+y+outset)*mapped.pitch+(static_cast<LONG>(rect.left)+x+outset)*4;
								if(pixel[0]>180 && pixel[1]>180 && pixel[2]>180) ++bright;
							}
							Check(bright>0,"blank dots and grid labels render inside standard 2x2 primary label slot");
						}
						auto PatternPixel=[&](int slot,int x,int y) {
							const auto& rect=widgets[slot].bounds;
							return mapped.bits[(static_cast<LONG>(rect.top)+y+outset)*mapped.pitch+(static_cast<LONG>(rect.left)+x+outset)*4+2];
						};
						Check(std::abs(int(PatternPixel(15,10,24))-int(PatternPixel(15,59,25)))<=2
							&& std::abs(int(PatternPixel(16,10,9))-int(PatternPixel(16,59,40)))<=2,
							"dot and grid pixels are centrally symmetric about preview content center");
					}
					if(menu==2 && scale==1.0F) {
						CreateDirectoryW(L"TestResults",nullptr);
						FILE* output=nullptr; _wfopen_s(&output,L"TestResults/whiteboard-menu-revision.bmp",L"wb");
						if(output) {
							BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
							file.bfSize=file.bfOffBits+targetWidth*targetHeight*4;
							BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=targetWidth;
							info.biHeight=-static_cast<LONG>(targetHeight); info.biPlanes=1; info.biBitCount=32;
							fwrite(&file,sizeof(file),1,output); fwrite(&info,sizeof(info),1,output);
							for(UINT y=0;y<targetHeight;++y) fwrite(mapped.bits+y*mapped.pitch,targetWidth*4,1,output);
							fclose(output);
						}
					}
					readable->Unmap();
					if(menu==2) {
						Check(widgets[4].iconResource==L"barMenu" && widgets[4].visible,
							"left fourth standard button survives expanded theme layout");
						Check(widgets[8].drawContent && widgets[16].drawContent,
							"real swatches and grid previews use shared painter");
						whiteboard.menu=0;
						Check(scene.TransitionLayout(BuildBackground(index,mode,0,false,background.bounds.top),
							BuildWidgets(index,mode,ppt,whiteboard,background.bounds.top),0.2),"menu close uses existing Scene transition");
						// 实际提交关闭中间帧，覆盖 Back 越零和半途反向，而非只连续设置 target。
						for (int step=0;step<5;++step) {
							context->BeginDraw(); context->Clear(D2D1::ColorF(0,0,0,0));
							(void)scene.Render(context,now+std::chrono::milliseconds(30*(step+1)));
							Check(SUCCEEDED(context->EndDraw()),"closing Back frame renders valid geometry");
						}
						whiteboard.menu=1;
						Check(scene.TransitionLayout(BuildBackground(index,mode,1,false,background.bounds.top),
							BuildWidgets(index,mode,ppt,whiteboard,background.bounds.top),0.2),"menu reversal reuses widget instances");
						for (int step=0;step<8;++step) {
							context->BeginDraw(); context->Clear(D2D1::ColorF(0,0,0,0));
							(void)scene.Render(context,now+std::chrono::milliseconds(150+30*(step+1)));
							Check(SUCCEEDED(context->EndDraw()),"reversed Back frame renders valid geometry");
						}
						scene.ReleaseDeviceResources();
						Check(SUCCEEDED(scene.EnsureDeviceResources(epoch,targetWidth,targetHeight)),
							"menu resources rebuild after device release");
					}
				}
		// 真实 PPT 稳定实例进入白板，逐帧对照共享属性 motion，不复制独立 easing。
		{
			using namespace Inkeys::UI::Bar;
			const double savedSpeed=BarUiAnimationSpeedRate; const bool savedEnabled=BarUiAnimationEnabled;
			for(const double speed:{0.5,1.0,2.0}) for(const float scale:{0.75F,1.5F}) {
				BarUiAnimationSpeedRate=speed; BarUiAnimationEnabled=false;
				WhiteboardState state=whiteboard; state.menu=2;
				const double offset=WhiteboardMenuHeight(state);
				const auto openBackground=BuildBackground(0,WorkspaceMode::WhiteboardExpanded,2,false,offset);
				const auto openWidgets=BuildWidgets(0,WorkspaceMode::WhiteboardExpanded,ppt,state,offset);
				const LONG width=static_cast<LONG>(std::ceil((WhiteboardPopupEnvelope(2).right+5)*scale));
				const LONG height=static_cast<LONG>(std::ceil((offset+80)*scale));
				Scene scene;
				Check(scene.Configure(BuildBackground(0,WorkspaceMode::PptCompact),BuildWidgets(0,WorkspaceMode::PptCompact,ppt,state)),"cold PPT configures stable popup slots");
				state.menu=0;
				Check(scene.TransitionLayout(BuildBackground(0,WorkspaceMode::WhiteboardExpanded,0,false,offset),
					BuildWidgets(0,WorkspaceMode::WhiteboardExpanded,ppt,state,offset),0),"cold PPT transitions to whiteboard without replacing slots");
				Check(scene.SetBounds({-1800,-320,-1800+width,-320+height},scale),"negative-screen origin accepted");
				const auto start=now+10s; (void)scene.Advance(start-16ms); (void)scene.Advance(start);
				Check(!scene.AnimationActive(),"cold PPT content transitions settle before idle popup start");
				const LONG outset=scene.PresentationOutsetPixels();
				const HRESULT setup=scene.EnsureDeviceResources(epoch,width+2*outset+4,height+2*outset+4);
				Check(SUCCEEDED(setup),"animated production scene resources allocate"); if(FAILED(setup)) continue;
				const auto bodyPresentation=scene.PresentationBounds();
				BarUiAnimationEnabled=true; state.menu=2;
				Check(scene.TransitionLayout(openBackground,openWidgets,0.4),"joint popup open reuses PPT instances");
				EraserSurfaceMotion parent,child; parent.Retarget(true,0.4); child.Retarget(true,0.4,&parent.Timeline());
				FrameAnimationClock referenceClock; (void)referenceClock.Tick(start); referenceClock.SuspendForIdle(start);
				auto RectFor=[&](WidgetId id) { for(const auto& item:scene.Layout().widgets) if(item.id==id) return item.localPixels; return RECT{}; };
				auto Expected=[&](const WidgetSpec& item,EraserSurfacePose pose) {
					const auto r=pose.Apply({item.bounds.left,item.bounds.top,item.bounds.right,item.bounds.bottom});
					return RECT{static_cast<LONG>(std::floor(r.left*scale)),static_cast<LONG>(std::floor(r.top*scale)),
						static_cast<LONG>(std::ceil(r.right*scale)),static_cast<LONG>(std::ceil(r.bottom*scale))};
				};
				for(int frame=1;frame<=60;++frame) {
					const auto time=start+std::chrono::milliseconds(frame*16);
					const double popupDt=referenceClock.Tick(time);
					(void)parent.Advance({popupDt,speed,true,false}); (void)child.Advance({popupDt,speed,true,false}); (void)scene.Advance(time);
					const auto& p=openWidgets[18].bounds; const auto& a=openWidgets[4].bounds;
					const auto pp=ResolveEraserSurfacePose({p.left,p.top,p.right,p.bottom},{a.left,a.top,a.right,a.bottom},parent.Geometry(),60);
					const auto& c=openWidgets[19].bounds; const auto& ca=openWidgets[7].bounds;
					const auto cp=ComposeEraserPose(pp,ResolveEraserSurfacePose({c.left,c.top,c.right,c.bottom},
						{ca.left,ca.top,ca.right,ca.bottom},child.Geometry(),60));
					const auto ep=Expected(openWidgets[18],pp),ec=Expected(openWidgets[19],cp),ap=RectFor(19),ac=RectFor(20);
					if(frame==1 && (!EqualRect(&ep,&ap) || !EqualRect(&ec,&ac)))
						std::fprintf(stderr,"[PageControlScene] pose speed=%.1f dpi=%.2f dt=%.3f expected=(%ld,%ld,%ld,%ld)/(%ld,%ld,%ld,%ld) actual=(%ld,%ld,%ld,%ld)/(%ld,%ld,%ld,%ld)\n",
							speed,scale,popupDt,ep.left,ep.top,ep.right,ep.bottom,ec.left,ec.top,ec.right,ec.bottom,
							ap.left,ap.top,ap.right,ap.bottom,ac.left,ac.top,ac.right,ac.bottom);
					Check(EqualRect(&ep,&ap) && EqualRect(&ec,&ac),"intermediate parent and child exactly match shared attribute motion at each speed");
					auto* dc=scene.DeviceContext(); dc->BeginDraw(); dc->Clear(D2D1::ColorF(0,0,0,0)); (void)scene.Render(dc,time);
					Check(SUCCEEDED(dc->EndDraw()),"shared popup intermediate frame draws successfully");
					const auto afterRender=RectFor(20); Check(EqualRect(&ac,&afterRender),"Advance and Render at one frameTime do not double advance");
					const auto presented=scene.PresentationBounds(),logical=scene.LogicalBounds();
					Check(presented.left<=logical.left+ac.left && presented.top<=logical.top+ac.top
						&& presented.right>=logical.left+ac.right && presented.bottom>=logical.top+ac.bottom,
						"actual presented crop contains composed child at every sampled pose");
					const auto color=RectFor(9); const POINT center{(color.left+color.right)/2,(color.top+color.bottom)/2};
					const POINT client{logical.left+center.x-presented.left,logical.top+center.y-presented.top};
					const auto mapped=scene.PresentationToLogical(client);
					Check(mapped && mapped->x==center.x && mapped->y==center.y,"cropped client maps to logical pixels across DPI and negative origin");
					if(frame==10) {
						state.backgroundRgb=0xFFFFFF;
						Check(scene.UpdateContent(BuildWidgets(0,WorkspaceMode::WhiteboardExpanded,ppt,state,offset)),"theme selection uses content-only update");
						(void)scene.Advance(time); const auto afterContent=RectFor(20);
						Check(EqualRect(&ac,&afterContent),"content update preserves pose and same-frame clock");
					}
				}
				// 真实 PPT 初始化后的面板填充与 More 分割线必须有像素，不能只验证其 spec。
				{
					auto* dc=scene.DeviceContext(); ComPtr<ID2D1Image> image; dc->GetTarget(&image);
					ComPtr<ID2D1Bitmap1> bitmap,readable; if(image) (void)image.As(&bitmap);
					if(bitmap) {
						const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,bitmap->GetPixelFormat());
						Check(SUCCEEDED(dc->CreateBitmap(bitmap->GetPixelSize(),nullptr,0,&properties,&readable)),"cold PPT chrome readback allocates");
						if(readable && SUCCEEDED(readable->CopyFromBitmap(nullptr,bitmap.Get(),nullptr))) {
							D2D1_MAPPED_RECT pixels{};
							if(SUCCEEDED(readable->Map(D2D1_MAP_OPTIONS_READ,&pixels))) {
								const auto presented=scene.PresentationBounds(),logical=scene.LogicalBounds();
								auto Pixel=[&](double x,double y) {
									const LONG px=static_cast<LONG>(std::floor(x*scale))+logical.left-presented.left;
									const LONG py=static_cast<LONG>(std::floor(y*scale))+logical.top-presented.top;
									return pixels.bits+py*pixels.pitch+px*4;
								};
								// frame 10 已切换纯白背景，标签须沿用同一文字槽并转为可读的深色。
								for(const int slot:{14,15,16}) {
									const auto& rect=openWidgets[slot].bounds; int dark=0;
									for(int y=45;y<65;++y) for(int x=4;x<66;++x) {
										const auto* pixel=Pixel(rect.left+x,rect.top+y);
										if(pixel[0]<100 && pixel[1]<100 && pixel[2]<100 && pixel[3]>220) ++dark;
									}
									Check(dark>0,"light-background style labels retain readable dark text inside standard 2x2 slot");
								}
								Check(Pixel(193,offset-80)[3]>120,"cold PPT to whiteboard applies opaque menu panel chrome");
								Check(Pixel(360,offset-115.5)[2]>35,"cold PPT to whiteboard applies shared More divider fill");
								readable->Unmap();
							} else Check(false,"cold PPT chrome maps");
						} else Check(false,"cold PPT chrome copies");
					} else Check(false,"cold PPT chrome target supports readback");
				}
				Check(!scene.PopupAnimationActive(),"popup finishes independent of other button feedback");
				// 动画禁用即刻回到主体，不等待按钮 hover 的五秒退场。
				BarUiAnimationEnabled=false; state.menu=0;
				Check(scene.TransitionLayout(BuildBackground(0,WorkspaceMode::WhiteboardExpanded,0,false,offset),
					BuildWidgets(0,WorkspaceMode::WhiteboardExpanded,ppt,state,offset),0.4),"disabled close accepted");
				(void)scene.Advance(start+2s); Check(!scene.PopupAnimationActive(),"disabled animation closes parent and child immediately");
				const auto closed=scene.PresentationBounds(); Check(EqualRect(&closed,&bodyPresentation),"HWND envelope returns to body after popup close");
			}
			BarUiAnimationSpeedRate=savedSpeed; BarUiAnimationEnabled=savedEnabled;
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
			auto next=state;
			// 页/工作区交接收起菜单；同页主题回执不打断用户继续选色。
			if(state.active && !state.switching && state.currentPage==publishedWhiteboard.currentPage
				&& state.generation==publishedWhiteboard.generation)
				next.menu=publishedWhiteboard.menu;
			if (AreWhiteboardStatesEquivalent(publishedWhiteboard, next)) return;
			publishedWhiteboard = next;
			whiteboardMenuOpen.store(next.menu!=0,std::memory_order_release);
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

	bool RequestWhiteboardMenuDismiss() noexcept
	{
		// 低级输入钩子只投递取消，不等待窗口线程或渲染事务。
		if (!whiteboardMenuOpen.load(std::memory_order_acquire)) return false;
		const HWND hwnd = Inkeys::Window::GetService().Handle(WindowRole::PptBottomLeft);
		return hwnd && PostMessageW(hwnd, WM_CANCELMODE, 0, 0) != FALSE;
	}

	void CancelPointerCapture() noexcept
	{
		CloseWhiteboardMenu();
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
