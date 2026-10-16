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
#include "../Bar/Bar.InteractionWorker.h"
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
#include <memory>
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
		using Scene = Inkeys::UI::Bar::BarPageSurface;
		using PageContent = Inkeys::UI::Bar::BarPageSurfaceContent;
		using WidgetId = Inkeys::UI::Bar::BarSurfaceButtonId;
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
			std::array<BarUiValueClass,5> values;
			std::chrono::steady_clock::time_point frameTime{};
			double left = 0.0;
			double top = 0.0;
			double right = 1.0;
			double bottom = 1.0;
			double scale = 1.0;
			RECT target{ 0, 0, 1, 1 };
			double targetScale = 1.0;
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
			POINT dragStartScreen{};
			PptLayoutState dragStartLayout{};
			PptLayoutState feasibleLayout{};
			std::chrono::steady_clock::time_point lastDragCandidateTrace{};
			std::uint64_t suppressedDirectCandidateTraces = 0;
			std::uint64_t suppressedPresentationBusyTraces = 0;
			std::chrono::steady_clock::time_point pressStarted{};
			std::chrono::steady_clock::time_point lastRepeat{};
			std::uint64_t pressedCaptureTicket=0;
			PptKeyboardRepeatTiming repeatTiming{};
			std::uint64_t observedRevision = 0;
			WhiteboardState renderedWhiteboard;
			WhiteboardState pressedWhiteboard;
			WorkspaceMode pressedMode=WorkspaceMode::Hidden;
			std::uint64_t pressedPptSession=0;
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
		// 触摸批次只属于 HWND owner，不随渲染对象销毁而从另一线程重置。
		struct NativeTouchState {DWORD id=0;bool active=false,primary=false;POINT lastClient{};};
		std::array<NativeTouchState,4> nativeTouches;
		std::array<std::uint64_t,4> nativeCaptureTickets{};
		std::atomic_uint64_t nextNativeCaptureTicket=1;
		constexpr bool ShouldReleasePageCapture(std::uint64_t captured,std::uint64_t requested) noexcept
		{return captured!=0&&captured==requested;}
		struct InputPresentation
		{
			WorkspaceMode mode=WorkspaceMode::Hidden;
			WhiteboardState whiteboard;
			PageControlSurfaceBudget budget;
			bool visible=false,locked=true;
			std::uint64_t pptSession=0,semanticRevision=0;
		};
		std::mutex inputSnapshotMutex;
		std::array<InputPresentation,4> inputPresentations;
		std::atomic_uint64_t inputLifecycle=1;
		struct InputRegistrationContext {std::size_t index=0;std::uint64_t generation=0;};
		// 旧 registration 的取消回调可能晚到，代次和内存均保留到共享 worker 排空。
		std::vector<std::unique_ptr<InputRegistrationContext>> inputContexts;
		std::uint64_t inputSemanticRevision=0;
		void CommitInputPresentationLocked(std::size_t index,WorkspaceMode mode,const WhiteboardState& whiteboard,
			const PageControlSurfaceBudget& budget,bool visible,bool locked,std::uint64_t pptSession)
		{
			const auto& previous=inputPresentations[index];
			const bool same=previous.mode==mode
				&&(mode!=WorkspaceMode::WhiteboardExpanded||(previous.whiteboard.generation==whiteboard.generation&&previous.whiteboard.currentPage==whiteboard.currentPage))
				&&(mode!=WorkspaceMode::PptCompact||previous.pptSession==pptSession);
			const auto identity=same&&previous.semanticRevision?previous.semanticRevision:++inputSemanticRevision;
			inputPresentations[index]={mode,whiteboard,budget,visible,locked,pptSession,identity};
		}
		InputPresentation InputSnapshot(std::size_t index)
		{std::scoped_lock lock(inputSnapshotMutex);return inputPresentations[index];}
		std::pair<InputPresentation,Inkeys::UI::Bar::BarSurfaceInputSample> CaptureInputSnapshot(std::size_t index,POINT local)
		{
			std::scoped_lock lock(inputSnapshotMutex);const auto input=inputPresentations[index];
			return {input,input.visible?surfaces[index].scene.SamplePointer(local):Inkeys::UI::Bar::BarSurfaceInputSample{}};
		}


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
		thread_local bool releasingPageCapture = false;
		constexpr UINT ReleasePageCaptureMessage=WM_APP+0x247;

		[[nodiscard]] constexpr std::size_t Index(Client client) noexcept
		{
			return static_cast<std::size_t>(client)
				- static_cast<std::size_t>(Client::PptBottomLeft);
		}

		[[nodiscard]] std::size_t Index(HWND hwnd) noexcept
		{
			if(!hwnd)return Roles.size();
			auto& service = Inkeys::Window::GetService();
			for (std::size_t index = 0; index < Roles.size(); ++index)
				if (service.Handle(Roles[index]) == hwnd) return index;
			return Roles.size();
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

		void InvokeDirection(std::size_t surfaceIndex,bool next,WorkspaceMode expectedMode=WorkspaceMode::Hidden)
		{
			if (surfaceIndex >= surfaces.size()) return;
			std::function<void()> callback;
			const auto [ppt, whiteboard] = Snapshot();
			const auto liveMode=ResolveWorkspaceMode(SurfaceFor(surfaceIndex),ppt,whiteboard);
			if((expectedMode!=WorkspaceMode::Hidden&&expectedMode!=liveMode)
				||WhiteboardWorkspaceSwitching(whiteboard)
				||(whiteboard.active&&Inkeys::Window::GetService().WhiteboardWindowGroupMinimized()))return;
			CloseWhiteboardMenu();
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
			constexpr auto& colors=WhiteboardVisual::ThemeColors;
			if(id>=9 && id<=17 && callbacks.changeTheme)
				callbacks.changeTheme(id<15 ? colors[id-9] : pressed.backgroundRgb,
					id<15 ? pressed.pattern : static_cast<int>(id-15), pressed.generation, pressed.currentPage);
		}

		[[nodiscard]] PageContent BuildPageContent(std::size_t index,WorkspaceMode mode,
			const PptState& ppt,const WhiteboardState& whiteboard)
		{
			PageContent content;
			content.surface=static_cast<unsigned>(index);
			content.whiteboard=mode==WorkspaceMode::WhiteboardExpanded;
			content.switching=whiteboard.switching;content.pptActive=ppt.presentationVisible;
			content.nextIsAdd=whiteboard.nextIsAdd;content.nextIsEndShow=IsPptEndPage(ppt.currentPage,ppt.totalPage);
			content.unsaved=whiteboard.unsaved;content.menu=whiteboard.menu;
			if(content.whiteboard){
				content.currentPage=std::to_wstring(whiteboard.currentPage);content.totalPage=L"/"+std::to_wstring(whiteboard.totalPage);
				content.enabled={whiteboard.previousEnabled,whiteboard.pageEnabled,whiteboard.nextEnabled};
				content.interactive={whiteboard.previousInteractive,whiteboard.pageInteractive,whiteboard.nextInteractive};
			}else{
				const int maximum=index>=2?999:9999;
				content.currentPage=PageNumber(ppt.currentPage,maximum);content.totalPage=L"/"+PageNumber(ppt.totalPage,maximum);
			}
			content.previousLabel=IW(I18nKey.UI.Bar.MainButtons.WhiteboardPreviousLabel);
			content.nextLabel=IW(whiteboard.nextIsAdd?I18nKey.UI.Bar.MainButtons.WhiteboardAddPageLabel:I18nKey.UI.Bar.MainButtons.WhiteboardNextLabel);
			content.moreLabel=IW(I18nKey.UI.Bar.MainButtons.MoreLabel);
			content.unsavedLabel=IW(I18nKey.UI.Bar.MainButtons.WhiteboardUnsavedLabel);
			content.actionLabels={IW(I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel),IW(I18nKey.UI.Bar.MainButtons.MinimizeWhiteboardLabel),IW(I18nKey.UI.Bar.MainButtons.WhiteboardThemeLabel)};
			content.patternLabels={IW(I18nKey.UI.Bar.MainButtons.WhiteboardBlankLabel),IW(I18nKey.UI.Bar.MainButtons.WhiteboardDotsLabel),IW(I18nKey.UI.Bar.MainButtons.WhiteboardGridLabel)};
			content.sectionLabels={IW(I18nKey.UI.Bar.MainButtons.WhiteboardBackgroundColorLabel),IW(I18nKey.UI.Bar.MainButtons.WhiteboardBackgroundStyleLabel)};
			content.backgroundRgb=whiteboard.backgroundRgb;content.pattern=whiteboard.pattern;
			return content;
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

		void AdvanceBounds(AnimatedBounds& animation,std::chrono::steady_clock::time_point now) noexcept
		{
			if(animation.frameTime==now)return;
			const double dt=animation.frameTime.time_since_epoch().count()
				?(std::clamp)(std::chrono::duration<double>(now-animation.frameTime).count(),0.0,0.05):0;
			animation.frameTime=now;
			if(!animation.active || (dt<=0&&BarUiAnimationEnabled))return;
			const BarUiAnimationAdvanceContextClass frame{
				dt,static_cast<double>(BarUiAnimationSpeedRate),static_cast<bool>(BarUiAnimationEnabled),false};
			animation.active=false;
			for(auto& value:animation.values)animation.active|=BarUiAdvanceAnimation(value,frame).active;
			animation.left=animation.values[0].val;animation.top=animation.values[1].val;
			animation.right=animation.values[2].val;animation.bottom=animation.values[3].val;animation.scale=animation.values[4].val;
		}

		void RetargetBounds(AnimatedBounds& animation,const RECT& target,double scale,bool animate,
			std::chrono::steady_clock::time_point now) noexcept
		{
			AdvanceBounds(animation,now);
			if(animation.initialized&&SameRect(animation.target,target)&&animation.targetScale==scale)return;
			const bool direct=!animation.initialized||!animate;
			const double targets[]={static_cast<double>(target.left),static_cast<double>(target.top),static_cast<double>(target.right),static_cast<double>(target.bottom),scale};
			for(std::size_t i=0;i<animation.values.size();++i)
				if(direct)animation.values[i].SetDirect(targets[i]);else animation.values[i].SetTar(targets[i],LayoutTransitionDurationSeconds());
			animation.target=target;animation.targetScale=scale;animation.initialized=true;animation.active=!direct;
			animation.left=animation.values[0].val;animation.top=animation.values[1].val;
			animation.right=animation.values[2].val;animation.bottom=animation.values[3].val;animation.scale=animation.values[4].val;
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

		[[nodiscard]] bool ConfigureSurface(std::size_t index,WorkspaceMode desiredMode,
			const PptState& ppt,const WhiteboardState& whiteboard,
			const RECT& monitor,const ResolvedSurfaceLayout& target,const WhiteboardState&,
			std::uint64_t revision,std::chrono::steady_clock::time_point now,const char*& failedStage)
		{
			auto& state=surfaces[index];
			const auto visualMode=desiredMode==WorkspaceMode::Hidden
				?(state.sceneConfigured?state.configuredMode:WorkspaceMode::PptCompact):desiredMode;
			const bool modeChanged=state.sceneConfigured&&state.configuredMode!=visualMode;
			if(!state.sceneConfigured || modeChanged || state.observedRevision!=revision){
				const bool animate=modeChanged&&ShouldAnimateWorkspaceLayout(SurfaceFor(index),state.configuredMode,visualMode,state.targetVisible);
				if(!state.scene.ConfigurePagination(BuildPageContent(index,visualMode,ppt,whiteboard),animate?LayoutTransitionDurationSeconds():0)){
					failedStage="pagination-configure";return false;
				}
				// 相同文字的新业务修订仍须经过成功提交，不能仅更新输入身份就提前确认页面。
				if(!state.scene.IsInvalidated())state.forceFullPresentation=true;
				if(!state.sceneConfigured){state.scene.SetHooks({{},[index]{RequestSurface(index);}});state.scene.SetOpacity(0,0);}
			}
			const bool wasVisible=state.targetVisible;state.targetVisible=desiredMode!=WorkspaceMode::Hidden;
			if(modeChanged||(wasVisible&&!state.targetVisible)||(whiteboard.switching&&!state.renderedWhiteboard.switching)){
				// 工作区交接只投递取消；指针和长按仍由唯一输入 worker 清理。
				Inkeys::UI::Bar::SurfaceInputEvent cancel;cancel.window=Inkeys::Window::GetService().Handle(Roles[index]);
				cancel.generation=inputLifecycle.load(std::memory_order_acquire);cancel.message=WM_CANCELMODE;
				if(cancel.window)(void)Inkeys::UI::Bar::SubmitSurfaceInput(cancel);
			}
			const bool entering=!wasVisible&&state.targetVisible;
			if(!state.bounds.initialized&&entering&&index>=2)
				RetargetBounds(state.bounds,ResolveHiddenSurfaceBounds(SurfaceFor(index),monitor,target.logicalBounds),target.scale,false,now);
			if(state.targetVisible!=wasVisible){state.scene.SetOpacity(state.targetVisible?1:0,LayoutTransitionDurationSeconds());}
			const bool animateBounds=state.bounds.initialized&&(wasVisible||state.targetVisible)&&(!entering||index>=2);
			if(!state.targetVisible&&state.bounds.initialized&&ShouldPreserveSurfaceBoundsWhileHiding(SurfaceFor(index))){
				AdvanceBounds(state.bounds,now);RetargetBounds(state.bounds,CurrentBounds(state.bounds),state.bounds.scale,false,now);
			}else RetargetBounds(state.bounds,target.logicalBounds,target.scale,animateBounds,now);
			AdvanceBounds(state.bounds,now);
			state.inputLocked=ShouldLockSurfaceInput(state.targetVisible,state.bounds.active,WhiteboardWorkspaceSwitching(whiteboard));
			if(!ApplySceneBounds(state,CurrentBounds(state.bounds),static_cast<float>(state.bounds.scale))){failedStage="pagination-bounds";return false;}
			state.sceneConfigured=true;state.configuredMode=visualMode;state.observedRevision=revision;state.renderedWhiteboard=whiteboard;
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
			bool pixelsSubmitted = false;
		};

		[[nodiscard]] PresentSceneResult PresentScene(std::size_t index,
			SurfaceState& state, HWND hwnd,
			const FrameContext& frameContext, const RECT& presentationBounds,
			SIZE backingCapacity, bool forceFullReplacement,
			bool showDebugFrames, bool lifecycleRendering,
			const std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)>* offscreenSink=nullptr) noexcept
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
			if ((!hwnd && !offscreenSink) || width == 0 || height == 0)
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
			RECT businessDamage=forceFullReplacement
				?RECT{0,0,static_cast<LONG>(width),static_cast<LONG>(height)}:state.scene.PendingDamage();
			const bool businessDamagePending =
				!Inkeys::UI::Bar::BarDirtyRegionTracker::IsEmpty(businessDamage);
			if(!businessDamagePending&&!forceFullReplacement&&!lifecycleRendering
				&&!state.scene.AnimationActive()&&!state.debugFrameSleepLatch.IsPending()&&!state.debugOverlayRefreshPending){
				result.status=PresentStatus::Success;return result;
			}
			const bool retryingNonSleepVisual =
				ShouldTreatPageControlDamageAsActiveDebugFrame(
					showDebugFrames, businessDamagePending, forceFullReplacement,
					state.debugFrameSleepLatch.IsPending());
			const bool hasActiveRendering = lifecycleRendering
				|| state.scene.AnimationActive() || retryingNonSleepVisual;
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
			// 与主栏相同：先裁剪，再清理/绘制；保留的大位图不扩大实际工作量。
			FrameStageTimer drawTimer(diagnostics,FrameStage::Draw);
			context->SetTransform(D2D1::Matrix3x2F::Identity());
			state.scene.Renderer().BeginFrameDraw(context.Get(),D2D1::RectF(
				static_cast<FLOAT>(presentDirty.left),static_cast<FLOAT>(presentDirty.top),
				static_cast<FLOAT>(presentDirty.right),static_cast<FLOAT>(presentDirty.bottom)));
			(void)state.scene.Render(context.Get(),frameContext.frameTime);
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
			state.scene.Renderer().FinishFrameDrawCommands(context.Get());
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
					presented = offscreenSink ? (*offscreenSink)(info)!=FALSE
						: UpdateLayeredWindowIndirect(hwnd, &info) != FALSE;
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
			const HRESULT endDrawHr = state.scene.Renderer().EndFrameDraw(context.Get());
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
			result.status = PresentStatus::Success;result.pixelsSubmitted=true;
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
			bounds.left = target.left;
			bounds.top = target.top;
			bounds.right = target.right;
			bounds.bottom = target.bottom;
			bounds.scale = scale;
			bounds.target = target;
			bounds.targetScale = scale;
			bounds.initialized = true;
			bounds.active = false;
			const double values[]={static_cast<double>(target.left),static_cast<double>(target.top),static_cast<double>(target.right),static_cast<double>(target.bottom),scale};
			for(std::size_t i=0;i<bounds.values.size();++i)bounds.values[i].SetDirect(values[i]);
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

		// 输入只尝试直移锁；忙时发布绝对候选，由渲染线程提交，不能等 GetDC。
		void UpdateDrag(std::size_t index, POINT screen)
		{
			std::unique_lock renderLock(renderTransactionMutex,std::try_to_lock);
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
			const auto budget = renderLock.owns_lock()?ResolveGroupBudgetLocked(snapshot,monitor,
				dpiScale,directMoveRevision.load(std::memory_order_acquire),deviceGeneration,true):InputSnapshot(index).budget;
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
			if(!renderLock.owns_lock()){RequestDragPair(pairIndex);return;}
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
			const auto& envelopeState=whiteboard;
			auto target = ResolveSurfaceLayout(surface, monitor,
				dpiScale, ppt, envelopeState);
			if (mode == WorkspaceMode::Hidden && index >= 2)
				target.logicalBounds = ResolveHiddenSurfaceBounds(
					surface, monitor, target.logicalBounds);

			PresentStatus presentStatus = PresentStatus::Success;
			PresentSceneResult completedPixels;
			SIZE completedPresentation{},completedBacking{};
			bool pixelsSubmitted=false;
			RECT presentation{};
			bool keepAnimating = false;
			bool shouldShow = false;
			bool pptFrame = false;
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
						state.bounds.active || state.scene.AnimationActive());
				if (state.lightingSubscribed != shouldSubscribeLighting)
				{
					state.scene.SetSharedLightingSubscribed(
						shouldSubscribeLighting);
					state.lightingSubscribed = shouldSubscribeLighting;
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
					state.bounds.active || state.scene.AnimationActive();
				const bool exitTransitionActive = !state.targetVisible
					&& transitionDeadlineActive;
				keepAnimating = ShouldContinuePageControlFrame(
					state.targetVisible, transitionDeadlineActive,
					state.bounds.active, state.scene.AnimationActive(),
					false);
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
						completedPixels=presentResult;completedPresentation=presentationSize;completedBacking=backing.size;
						pixelsSubmitted=presentResult.pixelsSubmitted;
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
				state.forceFullPresentation=true;
				if(diagnostics){diagnostics->presentCommitted=false;diagnostics->presentFailed=true;}
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
			// 命中和 workspace/锁定策略作为一份已呈现状态发布，WndProc 不得读到跨代组合。
			std::unique_lock inputCommitLock(inputSnapshotMutex);
			if(pixelsSubmitted){
				// 像素、位置和可见性全部提交成功后才清除 dirty；失败保留旧命中快照。
				(void)state.scene.ConsumeDamage();state.committedPresentationSize=completedPresentation;
				state.committedBackingCapacity=completedBacking;state.committedDeviceGeneration=frameContext.epoch.generation;
				state.committedPresentationReady=true;state.forceFullPresentation=false;
				state.lastPresentedDebugFrameBounds=completedPixels.debugFrameBounds;state.lastPresentedDebugWindowBounds=completedPixels.debugWindowBounds;
				state.debugOverlayRefreshPending=false;(void)state.debugFrameSleepLatch.CommitPresented();
				state.scene.CommitPresented(shouldShow&&!state.inputLocked);
			}else if(!shouldShow)state.scene.CommitPresented(false);
			CommitInputPresentationLocked(index,state.configuredMode,whiteboard,groupBudget,state.targetVisible,state.inputLocked,ppt.layout.session);
			inputCommitLock.unlock();
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
			if(!keepAnimating)state.scene.SuspendForIdle(frameContext.frameTime);
			return keepAnimating ? FrameResult::Continue : FrameResult::Idle;
		}

		void PageControlInputEvent(const Inkeys::UI::Bar::SurfaceInputEvent& event,void* context)
		{
			const HWND hwnd=event.window;const auto message=event.message;
			const auto wParam=event.wParam;const auto lParam=event.lParam;
			const auto* registration=static_cast<InputRegistrationContext*>(context);
			if(registration&&(!initialized.load(std::memory_order_acquire)
				||registration->generation!=inputLifecycle.load(std::memory_order_acquire)))return;
			const std::size_t index=registration?registration->index:Index(hwnd);
			if(index>=surfaces.size())return;auto& state=surfaces[index];
			const auto committed=InputSnapshot(index);
			if(message==WM_LBUTTONDOWN&&event.captureTicket)state.pressedCaptureTicket=event.captureTicket;
			if((message==WM_KEYDOWN&&wParam==VK_ESCAPE)||message==WM_CANCELMODE||message==WM_CAPTURECHANGED)CloseWhiteboardMenu();
			if (message == WM_MOUSEMOVE)
			{
				const POINT screen=event.screenPoint;

				if (!committed.visible || committed.locked) return;
				if (ShouldNotifyPageControlCursorEntered(
					state.borderCursorPointerInside, event.touch,
					Inkeys::Message::IsPointerGeneratedMouseMessage(
						message, event.extraInfo)))
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
					UpdateDrag(index, screen);

					FlushCommittedDrag(DragPairIndex(index));
					return;
				}
				if (state.dragPending) return;
				const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const auto logical = (event.logicalPointValid?std::optional<POINT>(event.logicalPoint):std::nullopt);
				if (state.pressStarted.time_since_epoch().count() != 0)
				{
					const WidgetId expected = state.pressedNext
						? NextWidget : PreviousWidget;
					const bool sameDirectionHit = logical.has_value()
						&& static_cast<WidgetId>(event.hitButtonId) == expected;
					if (!ShouldKeepPptLongPressTracking(true,
						event.captureOwned, sameDirectionHit))
					{
						// 拖出箭头后本次按压彻底取消，移回也不能重新启动长按。
						state.pressStarted = {};
						state.lastRepeat = {};
						state.repeatTiming = {};
					}
				}
				if (logical) state.scene.ApplyPointer(Inkeys::UI::Bar::BarSurfacePointerEventKind::Move,static_cast<WidgetId>(event.hitButtonId));
				else state.scene.PointerLeave();
				return;
			}
			if (message == WM_MOUSELEAVE)
			{
				bool notifyBorderCursor = false;
				{

					notifyBorderCursor = state.borderCursorPointerInside;
					state.borderCursorPointerInside = false;
					if (!state.dragging) state.scene.PointerLeave();
					state.pressStarted = {};
					state.lastRepeat = {};
					state.repeatTiming = {};
				}
				if (notifyBorderCursor)
					Inkeys::UI::Bar::NotifyBorderCursorSurfacePointerLeft();
				return;
			}
			if (message == WM_LBUTTONDOWN)
			{
				// 动画帧可以更新命中几何，但排队的 Down 不能跨业务页面或放映会话重新解释。
				if(event.semanticRevision&&event.semanticRevision!=committed.semanticRevision)return;
				const auto [livePpt,liveWhiteboard]=Snapshot();
				if(ResolveWorkspaceMode(SurfaceFor(index),livePpt,liveWhiteboard)!=committed.mode
					||WhiteboardWorkspaceSwitching(liveWhiteboard)
					||(committed.mode==WorkspaceMode::PptCompact&&committed.pptSession!=livePpt.layout.session)
					||(committed.mode==WorkspaceMode::WhiteboardExpanded
						&&(committed.whiteboard.generation!=liveWhiteboard.generation||committed.whiteboard.currentPage!=liveWhiteboard.currentPage)))return;
				bool promotePpt = false;
				bool invokeDirectionOnDown = false;
				bool invokeNext = false;
				{

					if (!committed.visible || committed.locked) return;
					const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					const auto logical = (event.logicalPointValid?std::optional<POINT>(event.logicalPoint):std::nullopt);
					if (!logical.has_value()) return;
					const auto hit = static_cast<WidgetId>(event.hitButtonId);
					const auto region = PointerRegionForWidget(hit);
					const auto inputPolicy = ResolveWorkspaceInputPolicy(
						committed.mode);
					const bool dragCandidate = inputPolicy.drag
						&& CanStartPptDrag(region)
						&&(region!=PptPointerRegion::Background||event.backgroundHit);
					const auto result = hit == Inkeys::UI::Bar::BarSurfaceNoButton
						? Inkeys::UI::Bar::BarSurfacePointerResult{}
						: state.scene.ApplyPointer(Inkeys::UI::Bar::BarSurfacePointerEventKind::Down,static_cast<WidgetId>(event.hitButtonId));
					if (!result.consumed && !dragCandidate) return;
					state.pressedWhiteboard = committed.whiteboard;state.pressedMode=committed.mode;
					state.pressedPptSession=livePpt.layout.session;

					promotePpt = committed.mode == WorkspaceMode::PptCompact;
					auto [ppt, whiteboard] = Snapshot();
					(void)whiteboard;
					if (dragCandidate)
					{
						state.dragging = StartsPptDragImmediately(region);
						state.dragPending = !state.dragging;
						state.dragStartScreen = event.screenPoint;
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
							committed.mode, ppt.longPressEnabled, repeatable);
						invokeDirectionOnDown = pressPolicy.invokeOnPointerDown;
						if (ShouldKeepPptLongPressTracking(
							pressPolicy.trackLongPress, event.captureOwned, true))
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
				if (invokeDirectionOnDown) InvokeDirection(index,invokeNext,committed.mode);
				return;
			}
			if (message == WM_LBUTTONUP)
			{
				state.pressedCaptureTicket=0;
				const auto [livePpt,liveWhiteboard]=Snapshot();
				bool dragEnded = false;
				bool hadDragTracking = false;
				std::size_t dragPairIndex = DragPairIndex(index);
				std::uint64_t suppressedDirectCandidateTraces = 0;
				std::uint64_t suppressedPresentationBusyTraces = 0;
				bool shouldInvokeClick = false;
				WorkspaceMode releasedMode = WorkspaceMode::Hidden;
				WhiteboardState pressedWhiteboard;
				Inkeys::UI::Bar::BarSurfaceButtonId clicked =
					Inkeys::UI::Bar::BarSurfaceNoButton;
				{

					const POINT local{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					const bool acceptInput = !committed.locked&&committed.visible&&state.pressedMode==committed.mode
						&&ResolveWorkspaceMode(SurfaceFor(index),livePpt,liveWhiteboard)==state.pressedMode
						&&!WhiteboardWorkspaceSwitching(liveWhiteboard)
						&&(state.pressedMode!=WorkspaceMode::PptCompact||state.pressedPptSession==livePpt.layout.session)
						&&(state.pressedMode!=WorkspaceMode::WhiteboardExpanded
							||(state.pressedWhiteboard.generation==liveWhiteboard.generation&&state.pressedWhiteboard.currentPage==liveWhiteboard.currentPage));
					shouldInvokeClick = acceptInput;
					releasedMode = committed.mode;
					pressedWhiteboard = state.pressedWhiteboard;
					if (acceptInput)
					{
						if (const auto logical = (event.logicalPointValid?std::optional<POINT>(event.logicalPoint):std::nullopt))
							clicked = state.scene.ApplyPointer(Inkeys::UI::Bar::BarSurfacePointerEventKind::Up,static_cast<WidgetId>(event.hitButtonId)).clicked;
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
					InvokeDirection(index,clicked==NextWidget,releasedMode);
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
				return;
			}
			if (message == WM_MOUSEWHEEL)
			{
				bool invoke = false;
				{

					invoke = committed.visible && !committed.locked
						&& ResolveWorkspaceInputPolicy(
							committed.mode).wheel;
				}
				if (invoke) InvokeDirection(
					index,GET_WHEEL_DELTA_WPARAM(wParam)<0,WorkspaceMode::PptCompact);
				return;
			}
			if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
			{
				state.pressedCaptureTicket=0;
				bool rollbackDrag = false;
				const std::size_t dragPairIndex = DragPairIndex(index);
				{

					rollbackDrag = state.dragging || state.dragPending;
					if(state.borderCursorPointerInside){state.borderCursorPointerInside=false;Inkeys::UI::Bar::NotifyBorderCursorSurfacePointerLeft();}
					state.scene.CancelPointer();
					state.dragging = false;
					state.dragPending = false;
					state.lastDragCandidateTrace = {};
					state.suppressedDirectCandidateTraces = 0;
					state.suppressedPresentationBusyTraces = 0;
					state.pressStarted = {};
					state.lastRepeat = {};
					state.repeatTiming = {};




				}
				if (rollbackDrag)
				{
					const auto rollback = RollbackDragTracking(dragPairIndex);
					if (rollback.tracked)
					{
						if (rollback.discardedPending)
							(void)Inkeys::UI::RenderPipeline::PostControl([dragPairIndex,layout=rollback.layout]{ApplyDragPairBoundsDirect(dragPairIndex,layout);});
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

				return;
			}
		}

		std::chrono::steady_clock::time_point PageControlInputTick(std::chrono::steady_clock::time_point now,void* context)
		{
			const auto* registration=static_cast<InputRegistrationContext*>(context);
			if(!registration||!initialized.load(std::memory_order_acquire)
				||registration->generation!=inputLifecycle.load(std::memory_order_acquire))return std::chrono::steady_clock::time_point::max();
			const auto index=registration->index;
			if(index>=surfaces.size())return std::chrono::steady_clock::time_point::max();
			auto& state=surfaces[index];const auto input=InputSnapshot(index);
			const auto [ppt,whiteboard]=Snapshot();
			const bool enabled=input.visible&&!input.locked&&ResolvePptDirectionPressPolicy(input.mode,ppt.longPressEnabled).trackLongPress
				&&!whiteboard.active&&!whiteboard.expandedLayoutTarget&&!WhiteboardWorkspaceSwitching(whiteboard)
				&&state.pressedPptSession==ppt.layout.session;
			if(!enabled){state.pressStarted={};state.lastRepeat={};return std::chrono::steady_clock::time_point::max();}
			if(state.pressStarted.time_since_epoch().count()==0)return std::chrono::steady_clock::time_point::max();
			const bool repeated=state.lastRepeat.time_since_epoch().count()!=0;
			if(ShouldTriggerPptLongPressRepeat(true,now-state.pressStarted,repeated,repeated?now-state.lastRepeat:std::chrono::steady_clock::duration::zero(),state.repeatTiming)){
				state.lastRepeat=ResolvePptLongPressRepeatAnchor(state.lastRepeat,now,repeated,state.repeatTiming);
				const bool next=state.pressedNext;
				if(!IsPptDirectionActionRepeatable(ResolvePptDirectionAction(next,ppt.currentPage,ppt.totalPage))){state.pressStarted={};state.lastRepeat={};}
				InvokeDirection(index,next,WorkspaceMode::PptCompact);
			}
			if(state.pressStarted.time_since_epoch().count()==0)return std::chrono::steady_clock::time_point::max();
			return state.lastRepeat.time_since_epoch().count()?state.lastRepeat+state.repeatTiming.repeatInterval:state.pressStarted+state.repeatTiming.initialDelay;
		}
		void PageControlInputCancel(const Inkeys::UI::Bar::SurfaceInputEvent& cancellation,void* context)
		{
			const auto hwnd=cancellation.window;
			const auto* registration=static_cast<InputRegistrationContext*>(context);
			if(!registration||registration->generation!=inputLifecycle.load(std::memory_order_acquire))return;
			const auto ticket=cancellation.captureTicket?cancellation.captureTicket:
				(registration->index<surfaces.size()?surfaces[registration->index].pressedCaptureTicket:0);
			auto event=cancellation;event.message=WM_CANCELMODE;
			PageControlInputEvent(event,context);
			// 捕获只能由窗口 owner 释放；投递不等待该窗口或渲染线程。
			if(hwnd&&ticket)PostMessageW(hwnd,ReleasePageCaptureMessage,static_cast<WPARAM>(ticket),0);
		}

		LRESULT CALLBACK PageControlWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			const std::size_t index = Index(hwnd);
			if(index>=surfaces.size())return message==WM_NCHITTEST?HTTRANSPARENT:DefWindowProcW(hwnd,message,wParam,lParam);
			auto& touch = nativeTouches[index];
			if(message==ReleasePageCaptureMessage){
				// 旧代的释放仍需清理它拥有的捕获，但不得释放新 Down 已取得的捕获。
				if(!ShouldReleasePageCapture(nativeCaptureTickets[index],wParam))return 0;
				nativeCaptureTickets[index]=0;
				touch={};
				releasingPageCapture=true;if(GetCapture()==hwnd)ReleaseCapture();releasingPageCapture=false;return 0;
			}
			if(message==WM_CAPTURECHANGED){nativeCaptureTickets[index]=0;if(releasingPageCapture)return 0;}
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

							current = { touch.id, touch.active,
								touch.primary };
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

							touch.id = decision.state.id;
							touch.active = decision.state.active;
							touch.primary = decision.state.primary;
							if (decision.message != PageControlTouchMessage::None)
								touch.lastClient = decision.state.active
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

			if(message==WM_NCHITTEST){
				RECT window{};GetWindowRect(hwnd,&window);
				const auto [input,sample]=CaptureInputSnapshot(index,{GET_X_LPARAM(lParam)-window.left,GET_Y_LPARAM(lParam)-window.top});
				if(!input.visible)return HTTRANSPARENT;
				return ShouldAcceptPageControlClientHit(input.mode,sample.background,sample.hit!=0,input.locked,
					index==0&&(input.whiteboard.menu!=0||sample.popupBackground))?HTCLIENT:HTTRANSPARENT;
			}
			if(message==WM_MOUSEMOVE||message==WM_MOUSELEAVE||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP
				||message==WM_MOUSEWHEEL||message==WM_CANCELMODE||message==WM_CAPTURECHANGED||message==WM_KEYDOWN){
				Inkeys::UI::Bar::SurfaceInputEvent event;event.window=hwnd;event.message=(message==WM_KEYDOWN&&wParam==VK_ESCAPE)?WM_CANCELMODE:message;event.wParam=wParam;event.lParam=lParam;
				event.extraInfo=static_cast<ULONG_PTR>(GetMessageExtraInfo());event.generation=inputLifecycle.load(std::memory_order_acquire);event.touch=translatingTouch;event.contactId=translatingTouch?touch.id:0;
				POINT local{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
				if(message==WM_MOUSEWHEEL)ScreenToClient(hwnd,&local);
				event.screenPoint=local;ClientToScreen(hwnd,&event.screenPoint);
				const auto [input,sample]=CaptureInputSnapshot(index,local);event.logicalPoint=sample.logical;event.logicalPointValid=sample.valid;
				// PPT 只可从已呈现主矩形的背景拖动；退场中的属性面板仍遮挡，但不是拖动区。
				event.backgroundHit=sample.primaryBackground;event.hitButtonId=sample.hit;event.presentedRevision=sample.revision;event.semanticRevision=input.semanticRevision;
				if(message==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
				if(message==WM_LBUTTONDOWN&&sample.background){SetCapture(hwnd);
					if(GetCapture()==hwnd)nativeCaptureTickets[index]=nextNativeCaptureTicket.fetch_add(1,std::memory_order_relaxed);}
				event.captureOwned=GetCapture()==hwnd;
				event.captureTicket=nativeCaptureTickets[index];
				// owner 已完成或马上完成原生释放，worker 只撤销逻辑手势，不能再次释放后续接触。
				event.nativeCaptureReleased=event.message==WM_CANCELMODE||event.message==WM_CAPTURECHANGED;
				const bool accepted=Inkeys::UI::Bar::SubmitSurfaceInput(event);
				if(!accepted&&ShouldReleasePageCapture(nativeCaptureTickets[index],event.captureTicket)&&GetCapture()==hwnd){
					nativeCaptureTickets[index]=0;releasingPageCapture=true;ReleaseCapture();releasingPageCapture=false;
				}
				// 先排入 Up，再释放原生捕获；重入 CaptureChanged 只能排在 Up 之后。
				if((message==WM_LBUTTONUP||event.message==WM_CANCELMODE)&&GetCapture()==hwnd){
					nativeCaptureTickets[index]=0;
					releasingPageCapture=true;ReleaseCapture();releasingPageCapture=false;
				}
				return 0;
			}
			if (message == WM_ERASEBKGND) return 1;
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}

	std::function<FrameResult(const FrameContext&)> CreatePageControlOffscreenRenderCallback(
		unsigned index,std::function<OffscreenSnapshot()> provider,RECT logicalBounds,float scale,
		std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)> sink,HWND inputIdentity)
	{
		struct Fixture
		{
			SurfaceState surface;
			OffscreenSnapshot last;
			bool configured=false;
			HWND identity=nullptr;
		};
		const auto destroy=[](Fixture* fixture){
			if(fixture->identity){
				Inkeys::UI::Bar::UnregisterInteractionSurface(fixture->identity);
				if(!Inkeys::UI::Bar::DrainInteractionSurfaceCallbacks()){
					// 即便调用方在共享 worker 释放最后引用，也要等当前 callback 返回后再销毁。
					if(!QueueUserWorkItem(+[](void* context)->DWORD{
						if(Inkeys::UI::Bar::DrainInteractionSurfaceCallbacks())delete static_cast<Fixture*>(context);
						return 0;},fixture,WT_EXECUTEDEFAULT))OutputDebugStringA("[PageControlUI3] deferred fixture cleanup could not be queued\n");
					return;
				}
			}
			delete fixture;
		};
		auto fixture=std::shared_ptr<Fixture>(new Fixture,destroy);fixture->identity=inputIdentity;
		if(inputIdentity){
			Inkeys::UI::Bar::RegisterInteractionSurface(inputIdentity,1,+[](const Inkeys::UI::Bar::SurfaceInputEvent& event,void* context){
				auto& scene=static_cast<Fixture*>(context)->surface.scene;
				using Kind=Inkeys::UI::Bar::BarSurfacePointerEventKind;
				Kind kind;switch(event.message){
				case WM_MOUSEMOVE:kind=Kind::Move;break;case WM_LBUTTONDOWN:kind=Kind::Down;break;
				case WM_LBUTTONUP:kind=Kind::Up;break;case WM_MOUSELEAVE:kind=Kind::Leave;break;
				case WM_CANCELMODE:case WM_CAPTURECHANGED:kind=Kind::Cancel;break;default:return;
				}
				// 无 HWND 提交端约定 screenPoint 是呈现位图内坐标，仍读取最近成功的真实命中帧。
				const auto sample=scene.SamplePointer(event.screenPoint);(void)scene.ApplyPointer(kind,sample.hit);
			},nullptr,+[](const Inkeys::UI::Bar::SurfaceInputEvent&,void* context){(void)static_cast<Fixture*>(context)->surface.scene.CancelPointer();},fixture.get(),false);
		}
		return [fixture,index,provider=std::move(provider),logicalBounds,scale,sink=std::move(sink)](const FrameContext& frame) mutable
		{
			if(index>=4||!provider||!sink)return FrameResult::Idle;
			const auto snapshot=provider();
			const auto mode=ResolveWorkspaceMode(SurfaceFor(index),snapshot.ppt,snapshot.whiteboard);
			if(mode==WorkspaceMode::Hidden){fixture->surface.scene.SuspendForIdle(frame.frameTime);return FrameResult::Idle;}
			auto& state=fixture->surface;
			if(!fixture->configured||!ArePptStatesEquivalent(fixture->last.ppt,snapshot.ppt)
				||!AreWhiteboardStatesEquivalent(fixture->last.whiteboard,snapshot.whiteboard)){
				const bool modeChanged=fixture->configured&&state.configuredMode!=mode;
				state.scene.ConfigurePagination(BuildPageContent(index,mode,snapshot.ppt,snapshot.whiteboard),modeChanged?LayoutTransitionDurationSeconds():0);
				state.configuredMode=mode;state.bounds.scale=scale;
				state.scene.SetBounds(logicalBounds,scale);state.scene.SetOpacity(1,0);state.scene.SetSharedLightingSubscribed(true);
				if(!state.scene.IsInvalidated())state.forceFullPresentation=true;
				fixture->last=snapshot;fixture->configured=true;
			}
			(void)state.scene.Advance(frame.frameTime);
			const RECT presentation=state.scene.PresentationBounds();
			const SIZE size{presentation.right-presentation.left,presentation.bottom-presentation.top};
			const SIZE capacity{(std::max)(state.backingCapacity.cx,size.cx),(std::max)(state.backingCapacity.cy,size.cy)};
			const bool full=state.forceFullPresentation||state.committedDeviceGeneration!=frame.epoch.generation
				||state.committedPresentationSize.cx!=size.cx||state.committedPresentationSize.cy!=size.cy
				||state.backingCapacity.cx!=capacity.cx||state.backingCapacity.cy!=capacity.cy;
			state.backingCapacity=capacity;
			if(!full&&!state.scene.IsInvalidated()&&!state.scene.AnimationActive()){
				state.scene.SuspendForIdle(frame.frameTime);return FrameResult::Idle;
			}
			const auto result=PresentScene(index,state,nullptr,frame,presentation,capacity,full,false,state.scene.AnimationActive(),&sink);
			if(result.status!=PresentStatus::Success){state.forceFullPresentation=true;
				return result.status==PresentStatus::DeviceLost?FrameResult::DeviceLost:FrameResult::Retry;}
			state.scene.CommitPresented(!snapshot.whiteboard.switching);(void)state.scene.ConsumeDamage();
			state.committedPresentationSize=size;state.committedBackingCapacity=capacity;
			state.committedDeviceGeneration=frame.epoch.generation;state.committedPresentationReady=true;state.forceFullPresentation=false;
			if(!result.continueRendering)state.scene.SuspendForIdle(frame.frameTime);
			return result.continueRendering?FrameResult::Continue:FrameResult::Idle;
		};
	}

	int RunOffscreenTests()
	{
		int failures=0;
		const auto Check=[&](bool valid,const char* why){if(!valid){++failures;std::fprintf(stderr,"[PageControlUI3] FAIL %s\n",why);}};
		constexpr std::array<std::uint32_t,6> expectedColors{0x123B32,0x172A46,0x000000,0xFFFFFF,0xE5E7EB,0xF5F1E6};
		Check(WhiteboardVisual::ThemeColors==expectedColors,"palette row order is green, navy, black then white, light gray, ivory");
		const auto epoch=Inkeys::UI::RenderPipeline::GetDeviceEpoch();
		PptState ppt;ppt.presentationVisible=true;ppt.currentPage=7;ppt.totalPage=28;ppt.layout.showMiddlePair=true;
		WhiteboardState wb;wb.expandedLayoutTarget=wb.active=true;wb.currentPage=2;wb.totalPage=4;
		wb.previousEnabled=wb.previousInteractive=wb.nextEnabled=wb.nextInteractive=true;
		const auto origin=std::chrono::steady_clock::now()+1s;
		Check(ShouldReleasePageCapture(7,7)&&!ShouldReleasePageCapture(8,7)&&!ShouldReleasePageCapture(0,7),
			"retired release clears only the native capture it originally owned");
		for(const auto mode:{WorkspaceMode::PptCompact,WorkspaceMode::WhiteboardExpanded})
		for(std::size_t index=0;index<(mode==WorkspaceMode::PptCompact?4U:2U);++index)
		for(const int menu:{0,1,2})
		for(const float scale:{0.25F,1.0F,1.5F,2.0F}){
			if(menu&&(index!=0||mode!=WorkspaceMode::WhiteboardExpanded))continue;
			wb.menu=menu;Scene surface;
			const auto content=BuildPageContent(index,mode,ppt,wb);
			Check(surface.ConfigurePagination(content),"real button list and fixed properties configure");
			const double width=mode==WorkspaceMode::WhiteboardExpanded?(index==0?305.0:230.0):(index>=2?42.5:165.0);
			const double height=mode==WorkspaceMode::WhiteboardExpanded?80.0:(index>=2?165.0:42.5);
			Check(surface.SetBounds({200,100,200+static_cast<LONG>(std::ceil(width*scale)),100+static_cast<LONG>(std::ceil(height*scale))},scale),"base geometry accepts effective scale");
			for(int frame=0;frame<=45;++frame)(void)surface.Advance(origin+frame*20ms);
			Check(!surface.AnimationActive(),"shared button and panel animations settle");
			const auto bounds=surface.PresentationBounds();
			const UINT w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
			const HRESULT setup=surface.EnsureDeviceResources(epoch,w+23,h+19);
			Check(SUCCEEDED(setup),"independent target owns device resources without a main Bar owner");if(FAILED(setup))continue;
			auto* dc=surface.DeviceContext();auto& renderer=surface.Renderer();
			renderer.BeginFrameDraw(dc,D2D1::RectF(0,0,static_cast<FLOAT>(w),static_cast<FLOAT>(h)));
			(void)surface.Render(dc,origin+900ms);renderer.FinishFrameDrawCommands(dc);
			Check(SUCCEEDED(renderer.EndFrameDraw(dc)),"real shared draw transaction succeeds");
			surface.CommitPresented();(void)surface.ConsumeDamage();
			Check(!surface.HitTestBackground({0,0}),"rounded transparent corner passes through");
			Check(surface.HitTestBackground({static_cast<LONG>(width*scale/2),static_cast<LONG>(height*scale/2)}),"rendered root and hit geometry agree");
			for(const auto& button:surface.Layout().buttons){if(!button.visible||!button.interactive||button.id==1)continue;
				const POINT point{(button.localPixels.left+button.localPixels.right)/2,(button.localPixels.top+button.localPixels.bottom)/2};
				Check(surface.HitTest(point)==button.id,"shared hit snapshot matches each rendered button");
			}
			ComPtr<ID2D1Image> image;dc->GetTarget(&image);ComPtr<ID2D1Bitmap1> bitmap;
			Check(image&&SUCCEEDED(image.As(&bitmap)),"target supports pixel readback");if(!bitmap)continue;
			ComPtr<ID2D1Bitmap1> cpu;const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
			if(FAILED(dc->CreateBitmap(bitmap->GetPixelSize(),nullptr,0,&props,&cpu))||FAILED(cpu->CopyFromBitmap(nullptr,bitmap.Get(),nullptr))){Check(false,"readback copy succeeds");continue;}
			D2D1_MAPPED_RECT mapped{};if(FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ,&mapped))){Check(false,"pixel map succeeds");continue;}
			const auto Pixel=[&](double x,double y){const LONG px=static_cast<LONG>(std::lround(x*scale))+200-bounds.left;
				const LONG py=static_cast<LONG>(std::lround(y*scale))+100-bounds.top;
				return mapped.bits+py*mapped.pitch+px*4;};
			const bool provePageMorph=mode==WorkspaceMode::PptCompact&&index==0&&scale==1;
			const auto PageSlotPixels=[&]{std::vector<BYTE> bytes;
				for(int y=5;y<38;++y)for(int x=53;x<123;++x){const auto* pixel=Pixel(x,y);bytes.insert(bytes.end(),pixel,pixel+4);}return bytes;};
			const auto originalPagePixels=provePageMorph?PageSlotPixels():std::vector<BYTE>{};
			if(scale>=1){
				const auto page=surface.Layout().buttons[2].localPixels;unsigned bright=0;
				for(LONG y=page.top+2;y<page.bottom-2;++y)for(LONG x=page.left+2;x<page.right-2;++x){
					const auto* pixel=mapped.bits+(y+100-bounds.top)*mapped.pitch+(x+200-bounds.left)*4;
					if(pixel[0]>120&&pixel[1]>120&&pixel[2]>120)++bright;}
				Check(bright>10,"current and total page text are actually drawn");
				if(menu==2){const auto* white=Pixel(360,-181);Check(white[0]>230&&white[1]>230&&white[2]>230,"theme white square is filled");
					for(std::size_t color=0;color<WhiteboardVisual::ThemeColors.size();++color){
						const auto rgb=WhiteboardVisual::ThemeColors[color];const auto* swatch=Pixel(358+static_cast<double>(color%3)*75,-258+static_cast<double>(color/3)*75);
						Check(std::abs(int(swatch[2])-int((rgb>>16)&255))<3&&std::abs(int(swatch[1])-int((rgb>>8)&255))<3
							&&std::abs(int(swatch[0])-int(rgb&255))<3,"all six rendered swatches match the shared click palette in row order");
					}
					for(const double y:{-69.0,-45.0,-21.0})for(const double x:{436.0,460.0,484.0}){
						const auto* dot=Pixel(x,y);Check(dot[1]>70,"full button preview contains its centered three by three dots, including the label row");
					}
					for(const double y:{-69.0,-45.0,-21.0}){
						const auto* line=Pixel(505,y);Check(line[1]>70,"grid horizontal lines span the full preview width behind the label row");
						for(const double x:{511.0,535.0,559.0})Check(Pixel(x,y)[1]>70,"grid preview has nine centered intersections");
					}
					const auto* a=Pixel(436,-45);const auto* b=Pixel(484,-45);Check(std::abs(int(a[2])-int(b[2]))<3,"dot texture is symmetric about content center");
						const auto* green=Pixel(377,-231);Check(green[1]>160&&green[2]<60,"selection uses the original green SVG at double swatch size");
						const auto* fill=Pixel(356,-260);Check(std::abs(int(fill[2])-18)<3&&std::abs(int(fill[1])-59)<3,"selected swatch has no extra outer highlight");
				}
			}
			Check(mapped.bits[3]==0,"presentation margin remains transparent");cpu->Unmap();
			if(provePageMorph){
				auto matching=wb;matching.menu=0;matching.currentPage=ppt.currentPage;matching.totalPage=ppt.totalPage;
				surface.ConfigurePagination(BuildPageContent(0,WorkspaceMode::WhiteboardExpanded,ppt,matching),LayoutTransitionDurationSeconds());
				(void)surface.Advance(origin+900ms);renderer.BeginFrameDraw(dc,D2D1::RectF(0,0,static_cast<FLOAT>(w),static_cast<FLOAT>(h)));
				(void)surface.Render(dc,origin+900ms);renderer.FinishFrameDrawCommands(dc);
				Check(SUCCEEDED(renderer.EndFrameDraw(dc)),"page morph reversal frame draws through common renderer");
				if(SUCCEEDED(cpu->CopyFromBitmap(nullptr,bitmap.Get(),nullptr))&&SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&mapped))){
					Check(PageSlotPixels()==originalPagePixels,"same-frame PPT to whiteboard retarget preserves both page text slots exactly");cpu->Unmap();
				}else Check(false,"page morph readback succeeds");
			}
			const auto before=surface.PresentedGeneration();
			if(mode==WorkspaceMode::WhiteboardExpanded&&index==0){
				auto changed=content;changed.menu=menu?0:2;surface.ConfigurePagination(changed);
				(void)surface.Advance(origin+920ms);
				Check(surface.PresentedGeneration()==before,"uncommitted candidate never replaces input snapshot");
				const auto midway=surface.PresentationBounds();changed.menu=menu;surface.ConfigurePagination(changed);
				(void)surface.Advance(origin+920ms);const auto reversed=surface.PresentationBounds();
				Check(EqualRect(&midway,&reversed),"same-frame reversal continues the current UI3 pose");
			}
			surface.ReleaseDeviceResources();Check(SUCCEEDED(surface.EnsureDeviceResources(epoch,w+23,h+19)),"device resources rebuild with stable button instances");
		}
		{
			// 真实展开帧中检查前层按钮遮挡后层，不只验证静止时彼此分离的矩形。
			Scene surface;auto content=BuildPageContent(0,WorkspaceMode::WhiteboardExpanded,ppt,wb);content.menu=0;
			surface.ConfigurePagination(content);surface.SetBounds({0,0,305,80},1);(void)surface.Advance(origin);
			content.menu=2;surface.ConfigurePagination(content);unsigned rootOverlaps=0,menuOverlaps=0,tripleOverlaps=0;
			// 固定分页面板的独立圆角区域判据；不为测试导出 Bar 内部命中类型。
			const auto rootPanelContains=[](POINT point){
				if(point.x<0||point.x>=305||point.y<0||point.y>=80)return false;
				const LONG dx=point.x-(std::clamp)(point.x,8L,297L),dy=point.y-(std::clamp)(point.y,8L,72L);
				return dx*dx+dy*dy<=64;
			};
			for(int frame=1;frame<=45;++frame){
				const auto time=origin+frame*10ms;(void)surface.Advance(time);const auto bounds=surface.PresentationBounds();
				if(FAILED(surface.EnsureDeviceResources(epoch,bounds.right-bounds.left,bounds.bottom-bounds.top))){Check(false,"overlap frame resources");break;}
				auto* dc=surface.DeviceContext();auto& renderer=surface.Renderer();renderer.BeginFrameDraw(dc,D2D1::RectF(0,0,
					static_cast<FLOAT>(bounds.right-bounds.left),static_cast<FLOAT>(bounds.bottom-bounds.top)));
				surface.Render(dc,time);renderer.FinishFrameDrawCommands(dc);Check(SUCCEEDED(renderer.EndFrameDraw(dc)),"overlapping panels draw through shared renderer");
				surface.CommitPresented();const auto layout=surface.Layout();
				for(const auto& front:layout.buttons){if(!front.visible||!front.interactive||front.id>8)continue;
					for(const auto& back:layout.buttons){if(!back.visible||back.id<(front.id<=5?6U:9U))continue;
						RECT overlap{};if(!IntersectRect(&overlap,&front.localPixels,&back.localPixels)||overlap.right-overlap.left<8||overlap.bottom-overlap.top<8)continue;
						const POINT point{(overlap.left+overlap.right)/2,(overlap.top+overlap.bottom)/2};
						// 菜单和主题重叠时，最前的分页栏仍可能覆盖二者；其空白处同样遮挡。
						if(front.id>5&&rootPanelContains(point)){
							const auto hit=surface.HitTest(point);const POINT presentation{point.x-bounds.left,point.y-bounds.top};
							const auto sample=surface.SamplePointer(presentation);
							Check(hit<=5&&sample.hit==hit&&sample.primaryBackground&&!sample.popupBackground
								&&surface.PointerMove(point).hover==hit,"pagination occludes both menu and theme during three-layer overlap");
							++tripleOverlaps;continue;
						}
						Check(surface.HitTest(point)==front.id,"front pagination/menu button wins animated overlap hit");
						const POINT presentation{point.x-bounds.left,point.y-bounds.top};
						Check(surface.SamplePointer(presentation).hit==front.id&&surface.PointerMove(point).hover==front.id,
							"sample and worker pointer entry use the same front-to-back hit order");
						if(front.id<=5)++rootOverlaps;else ++menuOverlaps;
					}
				}
			}
			Check(rootOverlaps>0&&menuOverlaps>0&&tripleOverlaps>0,"real opening motion exercises pagination-over-menu, menu-over-theme and three-layer overlaps");
		}
		// 无 HWND 的故障注入走实际 GetDC/ReleaseDC/EndDraw 事务，不以数学动画冒充呈现。
		OffscreenSnapshot snapshot{ppt,wb};snapshot.whiteboard.menu=2;
		unsigned submitted=0;bool reject=true;
		auto callback=CreatePageControlOffscreenRenderCallback(0,[&]{return snapshot;},{0,0,305,80},1,
			[&](const UPDATELAYEREDWINDOWINFO& info){++submitted;Check(info.hdcSrc!=nullptr&&info.psize->cx>0,"headless sink receives real GetDC");return reject?FALSE:TRUE;});
		Inkeys::UI::RenderPipeline::FrameContext frame;frame.epoch=epoch;frame.frameTime=origin;
		Check(callback(frame)==FrameResult::Retry,"failed final sink keeps a pending transaction");
		reject=false;frame.frameTime+=20ms;Check(callback(frame)!=FrameResult::Retry&&submitted>=2,"successful retry uses production transaction");
		for(int i=0;i<45;++i){frame.frameTime+=20ms;(void)callback(frame);}
		const auto beforeIdentityOnly=submitted;++snapshot.ppt.targetRevision;reject=true;frame.frameTime+=20ms;
		Check(callback(frame)==FrameResult::Retry&&submitted==beforeIdentityOnly+1,
			"same-text business revision still waits for an actual successful presentation");
		reject=false;frame.frameTime+=20ms;Check(callback(frame)!=FrameResult::Retry,"identity-only presentation retries safely");
		{
			SurfaceState state;auto content=BuildPageContent(0,WorkspaceMode::WhiteboardExpanded,ppt,wb);content.menu=0;
			state.scene.ConfigurePagination(content);state.scene.SetBounds({0,0,305,80},1);state.scene.SetOpacity(1,0);
			auto time=origin+2s;for(int i=0;i<=45;++i)(void)state.scene.Advance(time+i*20ms);time+=900ms;
			bool fail=false;RECT actualDirty{};SIZE actualViewport{};auto testEpoch=epoch;
			std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)> sink=[&](const auto& info){
				actualDirty=info.prcDirty?*info.prcDirty:RECT{0,0,info.psize->cx,info.psize->cy};actualViewport=*info.psize;return fail?FALSE:TRUE;};
			const auto present=[&](bool full){
				FrameContext context;context.epoch=testEpoch;context.frameTime=time;
				(void)state.scene.Advance(time);const auto bounds=state.scene.PresentationBounds();
				const SIZE size{bounds.right-bounds.left,bounds.bottom-bounds.top};
				state.backingCapacity={(std::max)(state.backingCapacity.cx,size.cx),(std::max)(state.backingCapacity.cy,size.cy)};
				const auto result=PresentScene(0,state,nullptr,context,bounds,state.backingCapacity,full,false,state.scene.AnimationActive(),&sink);
				if(result.status==PresentStatus::Success&&result.pixelsSubmitted){state.scene.CommitPresented();(void)state.scene.ConsumeDamage();}
				return result;
			};
			struct TargetPixels {UINT width=0,height=0;std::vector<BYTE> bytes;};
			const auto readPixels=[&]{
				TargetPixels result;auto* dc=state.scene.DeviceContext();ComPtr<ID2D1Image> image;dc->GetTarget(&image);ComPtr<ID2D1Bitmap1> bitmap,cpu;
				if(!image||FAILED(image.As(&bitmap)))return result;
				const auto size=bitmap->GetPixelSize();const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
					D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
				if(FAILED(dc->CreateBitmap(size,nullptr,0,&props,&cpu))||FAILED(cpu->CopyFromBitmap(nullptr,bitmap.Get(),nullptr)))return result;
				D2D1_MAPPED_RECT mapped{};if(FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return result;
				result.width=size.width;result.height=size.height;result.bytes.resize(static_cast<std::size_t>(size.width)*size.height*4);
				for(UINT y=0;y<size.height;++y)std::copy_n(mapped.bits+y*mapped.pitch,size.width*4,result.bytes.data()+static_cast<std::size_t>(y)*size.width*4);
				cpu->Unmap();return result;
			};
			Check(present(true).status==PresentStatus::Success,"initial real transaction commits");
			const auto committed=state.scene.PresentedGeneration();Check(state.scene.HitTest({265,40})==5,"committed More hit is available");
			content.menu=2;state.scene.ConfigurePagination(content);time+=20ms;(void)state.scene.Advance(time);
			const auto pose=state.scene.PresentationBounds();fail=true;
			Check(present(true).status==PresentStatus::Retry,"actual final-submit failure is reported");
			const auto afterFailure=state.scene.PresentationBounds();
			Check(state.scene.PresentedGeneration()==committed&&state.scene.HitTest({265,40})==5,"failed present preserves the committed generation and hit");
			Check(!Inkeys::UI::Bar::BarDirtyRegionTracker::IsEmpty(state.scene.PendingDamage()),"failed present retains dirty work");
			Check(EqualRect(&pose,&afterFailure),"Render after Advance does not advance the same batch twice");
			hiddenTestBlockedResourceSurface.store(0,std::memory_order_release);time+=20ms;
			Check(present(true).resourcesFailed&&state.scene.PresentedGeneration()==committed,"resource failure also preserves committed input");
			hiddenTestBlockedResourceSurface.store(-1,std::memory_order_release);fail=false;time+=20ms;
			Check(present(true).status==PresentStatus::Success&&state.scene.PresentedGeneration()>committed,"successful retry publishes the new input frame");
			for(int i=1;i<=45;++i)(void)state.scene.Advance(time+i*20ms);time+=900ms;(void)present(true);
			const auto beforePage=readPixels();
			content.currentPage=L"3";state.scene.ConfigurePagination(content);state.scene.SetOpacity(1,0);
			const auto pageDamage=state.scene.PendingDamage();const auto openBounds=state.scene.PresentationBounds();
			Check(pageDamage.right-pageDamage.left<(openBounds.right-openBounds.left)/3
				&&pageDamage.bottom-pageDamage.top<(openBounds.bottom-openBounds.top)/2,
				"page-only update with both panels open keeps damage bounded to the page button");
			time+=20ms;Check(present(false).status==PresentStatus::Success,"open panel page update uses a real partial transaction");
			const auto afterPage=readPixels();std::size_t changedInside=0,changedOutside=0;
			if(beforePage.width==afterPage.width&&beforePage.height==afterPage.height&&beforePage.bytes.size()==afterPage.bytes.size()){
				for(UINT y=0;y<beforePage.height;++y)for(UINT x=0;x<beforePage.width;++x){
					const auto offset=(static_cast<std::size_t>(y)*beforePage.width+x)*4;
					if(!std::equal(beforePage.bytes.begin()+offset,beforePage.bytes.begin()+offset+4,afterPage.bytes.begin()+offset)){
						if(x>=static_cast<UINT>(actualDirty.left)&&x<static_cast<UINT>(actualDirty.right)&&y>=static_cast<UINT>(actualDirty.top)&&y<static_cast<UINT>(actualDirty.bottom))++changedInside;else ++changedOutside;
					}
				}
			}
			Check(changedInside>0&&changedOutside==0,"real partial page text pixels change while the open menu, theme and backing remainder stay intact");
			state.scene.ConfigurePagination(content);state.scene.SetOpacity(1,0);
			Check(!state.scene.IsInvalidated(),"identical content and settled opacity do not request another frame");
			const auto gapAlpha=[&](const TargetPixels& pixels){
				const auto bounds=state.scene.PresentationBounds();const LONG x=77-bounds.left,y=40-bounds.top;
				return x>=0&&y>=0&&static_cast<UINT>(x)<pixels.width&&static_cast<UINT>(y)<pixels.height
					?static_cast<unsigned>(pixels.bytes[(static_cast<std::size_t>(y)*pixels.width+x)*4+3]):0U;
			};
			const unsigned fullAlpha=gapAlpha(afterPage);
			state.scene.SetOpacity(0.5,0.3);time+=20ms;(void)present(false);
			state.scene.SetOpacity(0.5,0);Check(present(false).status==PresentStatus::Success,"zero-duration same-target opacity snap submits the current frame");
			const unsigned snappedAlpha=gapAlpha(readPixels());
			Check(fullAlpha>100&&std::abs(static_cast<int>(snappedAlpha*2)-static_cast<int>(fullAlpha))<=2,
				"explicit zero-duration opacity snap finishes an unsettled same-target animation");
			state.scene.SetOpacity(1,0);(void)present(false);
			const SIZE largeBacking=state.backingCapacity;
			content.menu=0;state.scene.ConfigurePagination(content);
			for(int i=1;i<=45;++i)(void)state.scene.Advance(time+i*20ms);time+=900ms;(void)present(true);
			Check(state.backingCapacity.cx==largeBacking.cx&&state.backingCapacity.cy==largeBacking.cy,"closing keeps only backing capacity");
			state.scene.PointerMove({40,40});time+=20ms;
			Check(present(false).status==PresentStatus::Success,"closed toolbar hover uses real partial transaction");
			Check(actualDirty.right-actualDirty.left<actualViewport.cx/2&&actualViewport.cy<largeBacking.cy,
				"closed hover clips work before drawing instead of clearing the high-water bitmap");
			const auto pagePixels=[&] {
				auto* dc=state.scene.DeviceContext();ComPtr<ID2D1Image> image;dc->GetTarget(&image);ComPtr<ID2D1Bitmap1> bitmap,cpu;
				if(!image||FAILED(image.As(&bitmap)))return 0U;
				const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
					D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
				if(FAILED(dc->CreateBitmap(bitmap->GetPixelSize(),nullptr,0,&props,&cpu))||FAILED(cpu->CopyFromBitmap(nullptr,bitmap.Get(),nullptr)))return 0U;
				D2D1_MAPPED_RECT mapped{};if(FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return 0U;
				const auto bounds=state.scene.PresentationBounds();unsigned count=0;
				for(int y=7;y<73;++y)for(int x=82;x<148;++x){const auto* p=mapped.bits+(y-bounds.top)*mapped.pitch+(x-bounds.left)*4;
					if(p[0]>120&&p[1]>120&&p[2]>120)++count;}
				cpu->Unmap();return count;
			};
			for(const bool epochChange:{false,true}){
				state.scene.PointerMove({190,40});time+=20ms;(void)state.scene.Advance(time);
				Check(!Inkeys::UI::Bar::BarDirtyRegionTracker::IsEmpty(state.scene.PendingDamage()),"rebuild starts with only partial hover damage");
				if(epochChange)++testEpoch.generation;else state.backingCapacity.cx+=23;
				Check(present(true).status==PresentStatus::Success&&pagePixels()>10,
					"new target or epoch redraws the entire viewport despite partial preexisting damage");
				state.scene.PointerLeave();time+=20ms;(void)present(false);
			}
			for(int i=1;i<=280;++i)(void)state.scene.Advance(time+i*20ms);time+=5600ms;
			{
				// 真实调度回调提供诊断 TLS；两次热态呈现继续使用同一 epoch、几何和高水位位图。
				Inkeys::UI::RenderPipeline::Scheduler scheduler;std::atomic_bool completed=false;
				bool transactionOk=false,reused=false,diagnosticsAvailable=false;
				const bool capture=scheduler.ConfigureRawCapture(16);
				const bool started=capture&&scheduler.Start([&](auto now){FrameContext context;context.epoch=testEpoch;context.frameTime=now;return context;});
				const bool registered=started&&scheduler.Register(Client::PptBottomLeft,[&](const FrameContext&){
					BarUiFrameLightingSnapshot light;light.edgeLightingEnabled=light.cursorLightVisible=true;
					light.cursorLight=D2D1::Point2F(40,40);light.cursorRadius=120;light.cursorIntensity=1;
					state.scene.Renderer().SetFrameLightingSnapshot(light);
					auto* diagnostics=Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics();diagnosticsAvailable=diagnostics!=nullptr;
					transactionOk=true;
					for(int warm=0;warm<3;++warm){state.scene.Invalidate();transactionOk&=present(false).status==PresentStatus::Success;}
					auto* context=state.scene.DeviceContext();auto* gdi=state.scene.GdiInteropRenderTarget();
					const auto before=diagnostics?diagnostics->light:Inkeys::UI::RenderPipeline::LightingDiagnostics{};
					for(int sample=0;sample<2;++sample){state.scene.Invalidate();transactionOk&=present(false).status==PresentStatus::Success;}
					reused=diagnostics&&state.scene.DeviceContext()==context&&state.scene.GdiInteropRenderTarget()==gdi
						&&diagnostics->light.roundedParentCreate==before.roundedParentCreate
						&&diagnostics->light.geometryParentCreate==before.geometryParentCreate
						&&diagnostics->light.roundedParentHit>before.roundedParentHit;
					completed.store(true,std::memory_order_release);return FrameResult::Idle;
				});
				if(registered){const auto deadline=std::chrono::steady_clock::now()+5s;
					while(!completed.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);}
				scheduler.Stop();
				Check(registered&&completed.load(std::memory_order_acquire)&&transactionOk&&diagnosticsAvailable,
					"hot reuse probe executes real scheduled GetDC transactions with production diagnostics");
				Check(reused,"closed high-water target reuses context and rounded masks across repeated hot presentations");
			}
			state.scene.SuspendForIdle(time);const auto sleeping=state.scene.PresentationBounds();
			content.menu=2;state.scene.ConfigurePagination(content);time+=10s;(void)state.scene.Advance(time);
			const auto resumed=state.scene.PresentationBounds();Check(EqualRect(&sleeping,&resumed),"first frame after long idle resumes at zero UI3 dt");
			time+=20ms;(void)state.scene.Advance(time);const auto moving=state.scene.PresentationBounds();
			Check(!EqualRect(&resumed,&moving),"shared clock advances on the following batch");
			for(int i=1;i<=45;++i)(void)state.scene.Advance(time+i*20ms);time+=900ms;(void)present(true);
			auto compact=BuildPageContent(0,WorkspaceMode::PptCompact,ppt,{});state.scene.ConfigurePagination(compact,LayoutTransitionDurationSeconds());
			time+=20ms;(void)state.scene.Advance(time);const auto middle=state.scene.Layout();
			Check(middle.buttons[4].visible&&!middle.buttons[4].enabled,"outgoing More remains drawable while the new PPT layout cannot activate it");
			Check(present(true).status==PresentStatus::Success&&state.scene.HitTest({265,40})!=5,"intermediate transition commits the matching non-actionable hit frame");
			unsigned retired=0;const auto retiredBounds=state.scene.PresentationBounds();
			for(const auto& button:middle.buttons)if(button.id>=6&&button.visible){
				++retired;Check(!button.enabled,"outgoing popup buttons remain visible but cannot activate under PPT");
				const POINT center{(button.localPixels.left+button.localPixels.right)/2,(button.localPixels.top+button.localPixels.bottom)/2};
				const auto sample=state.scene.SamplePointer({center.x-retiredBounds.left,center.y-retiredBounds.top});
				Check(sample.background&&sample.popupBackground&&!sample.primaryBackground&&!sample.hit,
					"committed retiring popup occlusion is distinct from the draggable root background");
				Check(ShouldAcceptPageControlClientHit(WorkspaceMode::WhiteboardExpanded,sample.background,false,false,sample.popupBackground),
					"visible retiring popup cannot pass a click through to whiteboard ink");
			}
			Check(retired>0,"real shared panel closing frame is exercised before retirement completes");
			const auto rootSample=state.scene.SamplePointer({77-retiredBounds.left,25-retiredBounds.top});
			Check(rootSample.background&&rootSample.primaryBackground&&!rootSample.popupBackground,
				"same committed frame identifies the original pagination root independently");
			const auto inFlight=state.scene.Layout();const auto inFlightBounds=state.scene.PresentationBounds();
			compact.currentPage=L"8";state.scene.ConfigurePagination(compact);
			compact.backgroundRgb=0xFFFFFF;compact.pattern=2;state.scene.ConfigurePagination(compact);
			const auto retained=state.scene.Layout();const auto retainedBounds=state.scene.PresentationBounds();
			bool sameGeometry=EqualRect(&inFlightBounds,&retainedBounds)!=FALSE;
			for(std::size_t i=0;i<inFlight.buttons.size();++i)sameGeometry&=EqualRect(&inFlight.buttons[i].localPixels,&retained.buttons[i].localPixels)!=FALSE;
			Check(sameGeometry,"page and unrelated theme updates preserve all current geometry during an in-flight workspace morph");
			Check(present(false).status==PresentStatus::Success,"content update during a morph uses the current shared render pose");
			// Cubic起步阶段小于一个像素，逐批推进后验证连续运动，避免把取整平台误判为动画停止。
			for(int i=0;i<5;++i){time+=20ms;(void)state.scene.Advance(time);}const auto continued=state.scene.Layout();
			Check(state.scene.AnimationActive()&&!EqualRect(&retained.buttons[1].localPixels,&continued.buttons[1].localPixels),"the original layout morph continues across following shared frames without snapping");
		}
		{
			Scene surface;surface.ConfigurePagination(BuildPageContent(0,WorkspaceMode::PptCompact,ppt,{}));surface.SetBounds({200,100,365,143},1);
			for(int i=0;i<=45;++i)(void)surface.Advance(origin+20s+i*20ms);surface.CommitPresented();
			unsigned wakes=0;surface.SetHooks({{},[&]{++wakes;}});surface.SetSharedLightingSubscribed(true);
			Inkeys::UI::Bar::BarSurfaceSharedLighting light;light.edgeLightingEnabled=light.cursorVisible=true;light.cursorIntensity=1;light.cursorRadiusPixels=30;
			light.cursorScreenX=-10000;light.cursorScreenY=-10000;Scene::PublishSharedLighting(light);wakes=0;
			light.cursorScreenX=-9990;Scene::PublishSharedLighting(light);Check(wakes==0,"far cursor changes do not wake unrelated targets");
			light.cursorScreenX=205;light.cursorScreenY=120;Scene::PublishSharedLighting(light);Check(wakes>0,"cursor entering a committed border wakes it");wakes=0;
			light.cursorScreenX=-9000;light.cursorScreenY=-9000;Scene::PublishSharedLighting(light);Check(wakes>0,"cursor leaving wakes the previous lit border");wakes=0;
			light.cursorScreenX=-8990;Scene::PublishSharedLighting(light);Check(wakes==0,"subsequent far cursor movement remains idle");
			surface.Reset();wakes=0;light.cursorScreenX=205;light.cursorScreenY=120;Scene::PublishSharedLighting(light);
			Check(wakes==0,"reset removes the old target lighting subscription and hooks");
		}
		{
			// 直接驱动与共享 worker 注册的同一个 handler/tick；不创建 HWND 或调用真实 PPT。
			const bool wasInitialized=initialized.load(std::memory_order_acquire);
			const auto oldPpt=publishedPpt;const auto oldWhiteboard=publishedWhiteboard;
			const auto oldInput=InputSnapshot(0);auto oldSurface=std::move(surfaces[0]);surfaces[0]=SurfaceState{};
			const auto oldPptCallbacks=pptCallbacks;const auto oldWhiteboardCallbacks=whiteboardCallbacks;
			const auto oldRevision=publishedRevision.load(std::memory_order_acquire);
			std::array<PptDragCommitTracker,2> oldDragTrackers;{std::scoped_lock lock(dragCommitMutex);oldDragTrackers=dragCommitTrackers;}
			unsigned pptNext=0,whiteboardNext=0;
			pptCallbacks.nextPage=[&]{++pptNext;};whiteboardCallbacks.nextPage=[&]{++whiteboardNext;};
			publishedPpt=ppt;publishedPpt.layout.session=101;publishedPpt.longPressEnabled=true;publishedWhiteboard={};
			{std::scoped_lock lock(inputSnapshotMutex);CommitInputPresentationLocked(0,WorkspaceMode::PptCompact,{}, {},true,false,publishedPpt.layout.session);}
			initialized.store(true,std::memory_order_release);
			Inkeys::UI::Bar::SurfaceInputEvent event;event.message=WM_LBUTTONDOWN;event.hitButtonId=4;
			event.logicalPointValid=event.captureOwned=true;event.logicalPoint={140,20};
			event.semanticRevision=InputSnapshot(0).semanticRevision;
			InputRegistrationContext registration{0,inputLifecycle.load(std::memory_order_acquire)};
			void* const context=&registration;
			event.hitButtonId=0;event.logicalPoint={400,-60};event.backgroundHit=false;
			PageControlInputEvent(event,context);
			Check(!surfaces[0].dragPending&&!surfaces[0].dragging,"retiring popup background cannot begin PPT pair drag on the real worker handler");
			event.logicalPoint={50,20};event.backgroundHit=true;PageControlInputEvent(event,context);
			Check(surfaces[0].dragPending,"committed PPT root background still begins ordinary pair drag");
			event.message=WM_LBUTTONUP;PageControlInputEvent(event,context);
			event.message=WM_LBUTTONDOWN;event.hitButtonId=3;event.backgroundHit=false;PageControlInputEvent(event,context);
			Check(surfaces[0].dragPending,"explicit page button retains its PPT drag behavior");
			event.message=WM_CANCELMODE;PageControlInputEvent(event,context);
			event.message=WM_LBUTTONDOWN;event.hitButtonId=4;event.logicalPoint={140,20};
			PageControlInputEvent(event,context);Check(pptNext==1,"PPT pointer Down invokes its own workspace action");
			publishedPpt.currentPage=8;++publishedPpt.targetRevision;
			{std::scoped_lock lock(inputSnapshotMutex);CommitInputPresentationLocked(0,WorkspaceMode::PptCompact,{}, {},true,false,publishedPpt.layout.session);}
			Check(InputSnapshot(0).semanticRevision==event.semanticRevision,"PPT page revisions preserve the session input identity");
			const auto repeatTime=surfaces[0].pressStarted+surfaces[0].repeatTiming.initialDelay;
			(void)PageControlInputTick(repeatTime,context);Check(pptNext==2,"PPT repeat survives normal page revisions");
			publishedWhiteboard=wb;publishedWhiteboard.menu=1;
			(void)PageControlInputTick(repeatTime+100ms,context);
			Check(pptNext==2&&whiteboardNext==0&&publishedWhiteboard.menu==1,"old PPT repeat neither advances whiteboard nor dismisses its menu");
			std::vector<std::uint32_t> selectedColors;
			whiteboardCallbacks.changeTheme=[&](std::uint32_t rgb,int,std::uint64_t,int){selectedColors.push_back(rgb);};
			for(WidgetId id=9;id<=14;++id)InvokeWhiteboardMenu(id,publishedWhiteboard);
			Check(selectedColors==std::vector<std::uint32_t>(expectedColors.begin(),expectedColors.end()),
				"six color actions dispatch the same reordered RGB values as the rendered swatches");
			publishedWhiteboard.generation=7;publishedWhiteboard.currentPage=2;
			{std::scoped_lock lock(inputSnapshotMutex);CommitInputPresentationLocked(0,WorkspaceMode::WhiteboardExpanded,publishedWhiteboard,{},true,false,publishedPpt.layout.session);}
			PageControlInputEvent(event,context);
			Check(pptNext==2&&whiteboardNext==0,"queued PPT Down cannot be reinterpreted after a whiteboard frame commits");
			event.semanticRevision=InputSnapshot(0).semanticRevision;
			event.message=WM_LBUTTONDOWN;PageControlInputEvent(event,context);
			publishedWhiteboard.generation=8;publishedWhiteboard.currentPage=3;
			event.message=WM_LBUTTONUP;PageControlInputEvent(event,context);
			Check(whiteboardNext==0,"whiteboard Up cannot act on a different loaded page generation");
			{std::scoped_lock lock(inputSnapshotMutex);CommitInputPresentationLocked(0,WorkspaceMode::WhiteboardExpanded,publishedWhiteboard,{},true,false,publishedPpt.layout.session);}
			event.semanticRevision=InputSnapshot(0).semanticRevision;
			event.message=WM_LBUTTONDOWN;PageControlInputEvent(event,context);event.message=WM_LBUTTONUP;PageControlInputEvent(event,context);
			Check(whiteboardNext==1,"valid whiteboard click uses the same button state machine");
			++publishedWhiteboard.generation;++publishedWhiteboard.currentPage;
			{std::scoped_lock lock(inputSnapshotMutex);CommitInputPresentationLocked(0,WorkspaceMode::WhiteboardExpanded,publishedWhiteboard,{},true,false,publishedPpt.layout.session);}
			event.message=WM_LBUTTONDOWN;PageControlInputEvent(event,context);event.message=WM_LBUTTONUP;PageControlInputEvent(event,context);
			Check(whiteboardNext==1,"queued whiteboard Down cannot act after another page frame commits");
			InputRegistrationContext retired{0,registration.generation-1};publishedWhiteboard.menu=1;
			event.message=WM_LBUTTONDOWN;PageControlInputEvent(event,&retired);PageControlInputCancel({},&retired);
			Check(whiteboardNext==1&&publishedWhiteboard.menu==1,"retired lifecycle callbacks cannot cancel or act on the new workspace");
			publishedWhiteboard={};++publishedPpt.layout.session;
			{std::scoped_lock lock(inputSnapshotMutex);CommitInputPresentationLocked(0,WorkspaceMode::PptCompact,{}, {},true,false,publishedPpt.layout.session);}
			event.semanticRevision=InputSnapshot(0).semanticRevision;++publishedPpt.layout.session;
			PageControlInputEvent(event,context);Check(pptNext==2,"queued Down is rejected when the live PPT session already changed");
			{std::scoped_lock lock(inputSnapshotMutex);CommitInputPresentationLocked(0,WorkspaceMode::PptCompact,{}, {},true,false,publishedPpt.layout.session);}
			PageControlInputEvent(event,context);Check(pptNext==2,"queued Down is rejected after the replacement PPT frame commits");
			publishedPpt=oldPpt;publishedWhiteboard=oldWhiteboard;pptCallbacks=oldPptCallbacks;whiteboardCallbacks=oldWhiteboardCallbacks;
			publishedRevision.store(oldRevision,std::memory_order_release);whiteboardMenuOpen.store(oldWhiteboard.menu!=0,std::memory_order_release);
			{std::scoped_lock lock(dragCommitMutex);dragCommitTrackers=oldDragTrackers;}
			initialized.store(wasInitialized,std::memory_order_release);
			{std::scoped_lock lock(inputSnapshotMutex);inputPresentations[0]=oldInput;surfaces[0]=std::move(oldSurface);}
		}
		std::fprintf(stderr,"[PageControlUI3] failures=%d\n",failures);return failures?1:0;
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
							|| state.scene.AnimationActive()
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
		// Release 可由 HWND owner 发起异步排空；禁止新代复用仍被旧 worker 引用的对象。
		if(!inputContexts.empty()){
			if(!Inkeys::UI::Bar::DrainInteractionSurfaceCallbacks())return false;
			// 上一代异步清理可能尚未调度成功；排空后由新 Acquire 补齐，不能继承旧手势或资源。
			{std::unique_lock renderLock(renderTransactionMutex);std::scoped_lock inputLock(inputSnapshotMutex);
				inputPresentations={};for(auto& surface:surfaces)surface=SurfaceState{};groupLayoutBudget={};}
			inputContexts.clear();
		}
		initialized.store(true, std::memory_order_release);
		const auto lifecycle=inputLifecycle.fetch_add(1,std::memory_order_acq_rel)+1;
		for(std::size_t index=0;index<Roles.size();++index){
			const HWND hwnd=Inkeys::Window::GetService().Handle(Roles[index]);
			if(hwnd){
				auto context=std::make_unique<InputRegistrationContext>(InputRegistrationContext{index,lifecycle});
				Inkeys::UI::Bar::RegisterInteractionSurface(hwnd,lifecycle,PageControlInputEvent,PageControlInputTick,PageControlInputCancel,context.get());
				inputContexts.push_back(std::move(context));
			}
		}
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
				for(const auto role:Roles)Inkeys::UI::Bar::UnregisterInteractionSurface(Inkeys::Window::GetService().Handle(role));
				(void)Inkeys::UI::Bar::DrainInteractionSurfaceCallbacks();
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
		for(const auto role:Roles)Inkeys::UI::Bar::UnregisterInteractionSurface(Inkeys::Window::GetService().Handle(role));
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
		const auto reset=[] {
			std::unique_lock renderLock(renderTransactionMutex);
			std::scoped_lock inputLock(inputSnapshotMutex);inputPresentations={};
			for(auto& surface:surfaces)surface=SurfaceState{};
			groupLayoutBudget={};
		};
		if(Inkeys::UI::Bar::DrainInteractionSurfaceCallbacks())reset();
		else{
			// 窗口 owner 不能等待可能正在向它提交窗口效果的 worker；排空后再销毁内部对象。
			(void)QueueUserWorkItem(+[](void*)->DWORD{
				std::scoped_lock lifecycleLock(lifecycleMutex);
				if(referenceCount.load(std::memory_order_acquire)!=0)return 0;
				if(!Inkeys::UI::Bar::DrainInteractionSurfaceCallbacks())return 0;
				std::unique_lock renderLock(renderTransactionMutex);
				std::scoped_lock inputLock(inputSnapshotMutex);inputPresentations={};
				for(auto& surface:surfaces)surface=SurfaceState{};groupLayoutBudget={};return 0;
			},nullptr,WT_EXECUTEDEFAULT);
		}
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
		InvokeDirection(0,delta<0,WorkspaceMode::PptCompact);
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
		for(const auto role:Roles){
			Inkeys::UI::Bar::SurfaceInputEvent event;event.window=Inkeys::Window::GetService().Handle(role);
			event.generation=inputLifecycle.load(std::memory_order_acquire);event.message=WM_CANCELMODE;
			if(event.window)(void)Inkeys::UI::Bar::SubmitSurfaceInput(event);
		}
	}
}
