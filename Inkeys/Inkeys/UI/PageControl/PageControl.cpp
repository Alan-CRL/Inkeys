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
#include "../Bar/Bar.BottomDock.h"
#include "../../Drawing/Draw3/Draw3.PptTiming.h"
#include "../../Input/UiPointerInput.h"
#include "../../Input/MouseHook.Transport.h"
#include "../../Window/Window.MessageDispatch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <future>
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
		struct LocalWheelRequest
		{
			Business::PptSessionToken session;
			short delta = 0;
			std::uint64_t generation = 0;
		};
		// 本地 HWND 与 Hook 是不同 producer；本地通道独立有界，不共享 Hook SPSC。
		std::mutex localWheelMutex;
		std::array<LocalWheelRequest, 64> localWheels{};
		std::size_t localWheelRead = 0, localWheelWrite = 0, localWheelCount = 0;
		std::atomic_uint64_t wheelInputGeneration = 1;
		Input::HookTransport::WheelAccumulator globalWheelAccumulator;
		Input::HookTransport::WheelAccumulator localWheelAccumulator;
		std::uint64_t localWheelSession = 0;

		void EnqueueLocalWheel(short delta) noexcept
		{
			if (!delta) return;
			const auto generation = wheelInputGeneration.load(std::memory_order_acquire);
			const auto token = Business::CapturePptInputSession();
			if (!CanAcceptPptWheel(token)) return;
			std::scoped_lock lock(localWheelMutex);
			if (localWheelCount == localWheels.size()) return;
			localWheels[localWheelWrite] = { token, delta, generation };
			localWheelWrite = (localWheelWrite + 1) % localWheels.size();
			++localWheelCount;
		}

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
		namespace PointerInput = Inkeys::Input::UiPointer;
		constexpr UINT PointerWakeMessage = WM_APP + 0x351;
		constexpr UINT PointerCancelMessage = WM_APP + 0x352;
		constexpr UINT_PTR PointerTimer = 0x353;

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
			bool pointerActive = false;
			std::uint64_t pointerGeneration = 0;
			PptPointerRegion dragRegion = PptPointerRegion::Background;
			bool pressedNext = false;
			std::uintptr_t pointerSerial = 0;
			std::uint64_t pointerPptSession = 0;
			std::uint64_t pointerLayoutEpoch = 0;
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
			std::chrono::steady_clock::time_point lastRejectedInputTrace{};
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

		struct PointerClient
		{
			PointerInput::MouseChannel mouse;
			PointerInput::Session session;
			PointerInput::MouseGesture mouseGesture;
			std::uint64_t mouseGeneration = 0;
			bool rawTracking = false;
			std::uintptr_t touchDevice = 0;
			std::uint64_t touchGeneration = 0;
			std::uintptr_t nextSerial = 0;
			bool draining = false;
		};

		// 会话仅在 HWND owner 上推进；渲染线程只读 SurfaceState 中的有效性。
		std::array<PointerClient, 4> pointerClients;
		struct MiddleSnapState
		{
			std::uint64_t session = 0;
			std::uint64_t epoch = 0;
			std::uint64_t revision = 0;
			std::chrono::steady_clock::time_point started{};
			std::chrono::steady_clock::time_point sampledAt{};
			std::array<LONG, 2> startTop{};
			std::array<LONG, 2> targetTop{};
			std::array<SIZE, 2> size{};
			std::array<float, 2> scale{};
			bool latched = false;
			bool active = false;
		};
		constexpr auto MiddleSnapDuration = 140ms;
		MiddleSnapState middleSnap; // 与 SurfaceState 相同，由 renderTransactionMutex 保护。
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
		thread_local const PointerInput::Sample* dispatchedPointer = nullptr;
		thread_local bool dispatchedMouseAccepted = false;

		struct PointerDispatchScope
		{
			const PointerInput::Sample* previous = dispatchedPointer;
			bool previousTouch = translatingTouch;
			bool previousMouseAccepted = dispatchedMouseAccepted;
			explicit PointerDispatchScope(const PointerInput::Sample& sample,
				bool mouseAccepted = false) noexcept
			{
				dispatchedPointer = &sample;
				dispatchedMouseAccepted = mouseAccepted;
				translatingTouch = sample.source == PointerInput::Source::Touch;
			}
			~PointerDispatchScope()
			{
				dispatchedPointer = previous;
				dispatchedMouseAccepted = previousMouseAccepted;
				translatingTouch = previousTouch;
			}
		};

		[[nodiscard]] POINT PointerScreen(HWND hwnd, LPARAM lParam) noexcept
		{
			if (dispatchedPointer) return dispatchedPointer->screen;
			POINT screen{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ClientToScreen(hwnd, &screen);
			return screen;
		}

		[[nodiscard]] POINT PointerLocal(HWND hwnd, LPARAM lParam) noexcept
		{
			POINT local = PointerScreen(hwnd, lParam);
			ScreenToClient(hwnd, &local);
			return local;
		}

		[[nodiscard]] PointerInput::Sample NativePointerSample(
			HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
		{
			PointerInput::Sample sample;
			sample.kind = message == WM_LBUTTONDOWN ? PointerInput::Kind::Down
				: message == WM_LBUTTONUP ? PointerInput::Kind::Up : PointerInput::Kind::Move;
			Inkeys::Window::MessageDispatch::Sample posted;
			if (Inkeys::Window::MessageDispatch::TryGet(hwnd, message, wParam, lParam, posted))
			{
				sample.screen = posted.screen;
				sample.messageTime = posted.time;
			}
			else
			{
				sample.screen = PointerScreen(hwnd, lParam);
				sample.messageTime = static_cast<DWORD>(GetMessageTime());
			}
			sample.tick = Inkeys::Window::MessageDispatch::PostedTick(sample.messageTime);
			return sample;
		}

		[[nodiscard]] constexpr std::size_t Index(Client client) noexcept
		{
			return static_cast<std::size_t>(client)
				- static_cast<std::size_t>(Client::PptBottomLeft);
		}

		[[nodiscard]] std::size_t Index(HWND hwnd) noexcept
		{
			if (!hwnd) return Roles.size();
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

		[[nodiscard]] bool BeginDragTracking(std::size_t pairIndex,
			const PptLayoutState& layout) noexcept
		{
			std::scoped_lock dragLock(dragCommitMutex);
			std::scoped_lock snapshotLock(snapshotMutex);
			if (layout.session != publishedPpt.layout.session
				|| layout.epoch != publishedPpt.layout.epoch) return false;
			// tracker 身份始终使用发布的原始布局；runtime fit 只属于手势/实际几何。
			// pending 尚未收敛时再次 Down，不能用 fit 倍率覆盖同 revision 的候选身份。
			BeginPptDragTracking(dragCommitTrackers[pairIndex], publishedPpt.layout);
			return true;
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
			std::size_t surfaceIndex, const PptState& frame,
			bool geometryAtTarget) noexcept
		{
			const auto revision = frame.layout.pairVersions[DragPairIndex(surfaceIndex)];
			std::scoped_lock lock(dragCommitMutex);
			auto& tracker = dragCommitTrackers[DragPairIndex(surfaceIndex)];
			if (!tracker.pending || tracker.revision != revision) return {};
			DragCommitSnapshot result{ tracker.layout, tracker.revision,
				tracker.committedSurfaceMask, true, false, tracker.released };
			if (geometryAtTarget)
				result.completed = MarkPptDragFrameCommitted(tracker, frame, surfaceIndex);
			result.committedSurfaceMask = tracker.committedSurfaceMask;
			return result;
		}

		[[nodiscard]] DragCommitSnapshot CommitDragPairDirect(
			std::size_t pairIndex, std::uint64_t revision, bool geometryAtTarget) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			auto& tracker = dragCommitTrackers[pairIndex];
			if (!tracker.pending || tracker.revision != revision) return {};
			DragCommitSnapshot result{ tracker.layout, tracker.revision,
				tracker.committedSurfaceMask, true, false, tracker.released };
			result.completed = geometryAtTarget && MarkPptDragSurfaceCommitted(tracker, revision,
				PptDragCommittedSurfaceMask);
			result.committedSurfaceMask = tracker.committedSurfaceMask;
			if (result.completed || !geometryAtTarget)
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
			std::size_t pairIndex, bool persist, const PptLayoutState& expected) noexcept
		{
			std::scoped_lock lock(dragCommitMutex);
			auto& tracker = dragCommitTrackers[pairIndex];
			if (tracker.layout.session != expected.session
				|| tracker.layout.epoch != expected.epoch) return {};
			return ReleasePptDragTracking(tracker, persist);
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
			std::array<bool, 2> dragPositionOwned{};
		};

		[[nodiscard]] RenderSnapshot SnapshotForRender() noexcept
		{
			// 与候选发布相同的 D→S 顺序，同时采集 pair 所有权；不能在 S 内反取 D。
			std::scoped_lock dragLock(dragCommitMutex);
			std::scoped_lock snapshotLock(snapshotMutex);
			RenderSnapshot result{ publishedPpt, publishedWhiteboard,
				publishedRevision.load(std::memory_order_relaxed) };
			for (std::size_t pair = 0; pair < result.dragPositionOwned.size(); ++pair)
				result.dragPositionOwned[pair] = IsPptDragFrameOwned(
					dragCommitTrackers[pair], publishedPpt, pair * 2);
			return result;
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
				&& std::abs(animation.targetScale - scale) < 0.0001
				&& (animate || !animation.active)) return;
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

		[[nodiscard]] bool MiddleSnapMatches(std::size_t index, const PptState& frame,
			const ResolvedSurfaceLayout& target) noexcept
		{
			if (index < 2 || !middleSnap.latched || !frame.presentationVisible
				|| frame.layout.session != middleSnap.session || frame.layout.epoch != middleSnap.epoch
				|| frame.layout.pairVersions[1] != middleSnap.revision
				|| frame.layout.middlePairHeight != 0.0F || target.mode != WorkspaceMode::PptCompact) return false;
			const std::size_t item = index - 2;
			return target.logicalBounds.top == middleSnap.targetTop[item]
				&& target.logicalBounds.right - target.logicalBounds.left == middleSnap.size[item].cx
				&& target.logicalBounds.bottom - target.logicalBounds.top == middleSnap.size[item].cy
				&& std::abs(target.scale - middleSnap.scale[item]) <= 0.0001F;
		}

		[[nodiscard]] RECT ResolveMiddleSnapBounds(std::size_t index,
			const ResolvedSurfaceLayout& target, std::chrono::steady_clock::time_point now) noexcept
		{
			RECT result = target.logicalBounds;
			if (!middleSnap.active) return result;
			// renderer 的整批 frameTime 可早于最新 owner 采样；两面共用单调时间，禁止回拉。
			middleSnap.sampledAt = (std::max)(now, middleSnap.sampledAt);
			const double progress = (std::clamp)(std::chrono::duration<double>(
				middleSnap.sampledAt - middleSnap.started).count() / std::chrono::duration<double>(MiddleSnapDuration).count(), 0.0, 1.0);
			const std::size_t item = index - 2;
			const LONG height = result.bottom - result.top;
			// 复用 UI3 PageControl 原有 cubic 曲线；仅 Y 修正，X 始终采用最新拖动目标。
			result.top = static_cast<LONG>(std::lround(middleSnap.startTop[item]
				+ (middleSnap.targetTop[item] - middleSnap.startTop[item]) * Ease(progress)));
			result.bottom = result.top + height;
			if (progress >= 1.0) middleSnap.active = false;
			return result;
		}

		void UpdateMiddleSnap(const PptState& frame,
			const std::array<ResolvedSurfaceLayout, 2>& targets,
			bool centerSnapped, std::chrono::steady_clock::time_point now) noexcept
		{
			if (!centerSnapped) { middleSnap = {}; return; }
			bool sameTarget = middleSnap.latched && middleSnap.session == frame.layout.session
				&& middleSnap.epoch == frame.layout.epoch;
			for (std::size_t item = 0; item < targets.size(); ++item)
			{
				const auto& target = targets[item];
				const auto& state = surfaces[item + 2];
				const auto current = CurrentBounds(state.bounds);
				if (!state.bounds.initialized || state.configuredMode != WorkspaceMode::PptCompact
					|| !state.targetVisible || std::abs(state.bounds.scale - target.scale) > 0.0001
					|| current.right - current.left != target.logicalBounds.right - target.logicalBounds.left
					|| current.bottom - current.top != target.logicalBounds.bottom - target.logicalBounds.top)
				{ middleSnap = {}; return; } // 真实布局/DPI变化继续由原工作区动画处理。
				sameTarget = sameTarget && middleSnap.targetTop[item] == target.logicalBounds.top
					&& std::abs(middleSnap.scale[item] - target.scale) <= 0.0001F;
			}
			if (!sameTarget)
			{
				middleSnap = {};
				middleSnap.session = frame.layout.session;
				middleSnap.epoch = frame.layout.epoch;
				middleSnap.started = now; // 整个 pair 只有一个起始时刻，带内 Move/页发布不重启。
				middleSnap.sampledAt = now;
				middleSnap.latched = true;
				for (std::size_t item = 0; item < targets.size(); ++item)
				{
					const auto current = CurrentBounds(surfaces[item + 2].bounds);
					const auto& target = targets[item];
					middleSnap.startTop[item] = current.top;
					middleSnap.targetTop[item] = target.logicalBounds.top;
					middleSnap.size[item] = { target.logicalBounds.right - target.logicalBounds.left,
						target.logicalBounds.bottom - target.logicalBounds.top };
					middleSnap.scale[item] = target.scale;
					middleSnap.active = middleSnap.active || current.top != target.logicalBounds.top;
				}
			}
			middleSnap.revision = frame.layout.pairVersions[1];
		}

		[[nodiscard]] bool ConfigureSurface(std::size_t index, WorkspaceMode desiredMode,
			const PptState& ppt, const WhiteboardState& whiteboard,
			const RECT& monitor, const ResolvedSurfaceLayout& target,
			std::uint64_t revision,
			std::chrono::steady_clock::time_point now,
			bool dragPositionOwned, const char*& failedStage)
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
			// 整个 pair（含松手后 pending）直接落定纯拖动位置；DPI/倍率/工作区动画保留。
			const bool settleDragPosition = dragPositionOwned && wasVisible
				&& state.targetVisible && !modeChanged
				&& desiredMode == WorkspaceMode::PptCompact
				&& state.bounds.initialized
				&& std::abs(state.bounds.scale - target.scale) <= 0.0001
				&& state.bounds.target.right - state.bounds.target.left
					== target.logicalBounds.right - target.logicalBounds.left
				&& state.bounds.target.bottom - state.bounds.target.top
					== target.logicalBounds.bottom - target.logicalBounds.top;
			const bool animateBounds = !settleDragPosition && state.bounds.initialized
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
			else if (settleDragPosition && MiddleSnapMatches(index, ppt, target))
				RetargetBounds(state.bounds, ResolveMiddleSnapBounds(index, target, now), target.scale, false, now);
			else
			{
				if (index >= 2) middleSnap = {};
				RetargetBounds(state.bounds, target.logicalBounds, target.scale, animateBounds, now);
			}
			AdvanceBounds(state.bounds, now);
			state.inputLocked = ShouldLockSurfaceInput(state.targetVisible,
				now < state.layoutTransitionUntil,
				WhiteboardWorkspaceSwitching(whiteboard));
			if (state.pointerActive && (state.inputLocked || modeChanged
				|| (state.configuredMode == WorkspaceMode::PptCompact
					&& (state.pointerPptSession != ppt.layout.session
						|| state.pointerLayoutEpoch != ppt.layout.epoch))))
			{
				// 渲染线程只关准入，取消和 pair 回退交回 owner，不能跨线程改会话。
				state.pointerActive = false;
				state.pressStarted = {};
				state.lastRepeat = {};
				state.repeatTiming = {};
				(void)PostMessageW(Inkeys::Window::GetService().Handle(Roles[index]),
					PointerCancelMessage, state.pointerSerial, 0);
			}
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

		template<typename ReadBounds, typename MoveWindow>
		[[nodiscard]] bool MovePairWindowsDirect(
			const std::array<std::size_t, 2>& indices,
			const std::array<RECT, 2>& targets, std::uint64_t revision,
			ReadBounds&& readBounds, MoveWindow&& moveWindow) noexcept
		{
			std::array<RECT, 2> original{};
			const std::size_t pairIndex = DragPairIndex(indices[0]);
			for (std::size_t item = 0; item < indices.size(); ++item)
			{
				if (!readBounds(item, original[item]))
				{
					TraceDrag("move-pair pair=%s revision=%llu result=failed "
						"stage=%s-get-window-rect surface=%s error=%lu",
						pairIndex == 0 ? "bottom" : "middle",
						static_cast<unsigned long long>(revision),
						item == 0 ? "first" : "second", SurfaceName(indices[item]),
						static_cast<unsigned long>(GetLastError()));
					return false;
				}
			}
			if (!moveWindow(0, targets[0]))
			{
				TraceDrag("move-pair pair=%s revision=%llu result=failed "
					"stage=first-set-window-pos surface=%s error=%lu",
					pairIndex == 0 ? "bottom" : "middle",
					static_cast<unsigned long long>(revision), SurfaceName(indices[0]),
					static_cast<unsigned long>(GetLastError()));
				return false;
			}
			if (moveWindow(1, targets[1])) return true;

			const DWORD secondError = GetLastError();
			// 第二窗失败时恢复已移动的第一窗，pair 由渲染 fallback 再整体收敛。
			const bool rollbackSucceeded = moveWindow(0, original[0]);
			const DWORD rollbackError = rollbackSucceeded ? ERROR_SUCCESS : GetLastError();
			TraceDrag("move-pair pair=%s revision=%llu result=failed "
				"stage=second-set-window-pos surface=%s error=%lu "
				"rollback_stage=first-set-window-pos rollback=%d rollback_error=%lu",
				pairIndex == 0 ? "bottom" : "middle",
				static_cast<unsigned long long>(revision), SurfaceName(indices[1]),
				static_cast<unsigned long>(secondError), rollbackSucceeded ? 1 : 0,
				static_cast<unsigned long>(rollbackError));
			return false;
		}

		[[nodiscard]] bool MovePairWindowsDirect(
			const std::array<std::size_t, 2>& indices,
			const std::array<RECT, 2>& targets, std::uint64_t revision) noexcept
		{
			auto& service = Inkeys::Window::GetService();
			const std::array<HWND, 2> handles{
				service.Handle(Roles[indices[0]]), service.Handle(Roles[indices[1]]) };
			constexpr UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
				| SWP_NOOWNERZORDER | SWP_NOSENDCHANGING;
			return MovePairWindowsDirect(indices, targets, revision,
				[&](std::size_t item, RECT& original)
				{
					SetLastError(handles[item] ? ERROR_SUCCESS : ERROR_INVALID_WINDOW_HANDLE);
					return handles[item] && GetWindowRect(handles[item], &original) != FALSE;
				},
				[&](std::size_t item, const RECT& target)
				{
					SetLastError(ERROR_SUCCESS);
					return SetWindowPos(handles[item], nullptr,
						target.left, target.top, 0, 0, flags) != FALSE;
				});
		}

		// 调用方必须持有 renderTransactionMutex；直移和 bounds 提交共享同一事务。
		template<typename MovePair>
		void UpdateDragLocked(std::size_t index, POINT screen, MovePair&& movePair,
			std::chrono::steady_clock::time_point sampleTime = std::chrono::steady_clock::now())
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
			const std::size_t pairIndex = bottom ? 0 : 1;
			const auto constrained = ResolvePptDragCandidate(moved, monitor,
				dpiScale, ppt, candidate, state.feasibleLayout,
				Inkeys::UI::Bar::BarBottomDockThresholdDip);
			const bool unchanged = SameDragPairPosition(pairIndex,
				constrained.layout, state.feasibleLayout);
			if ((constrained.collisionRejected || constrained.clamped
				|| constrained.centerBlocked) && ShouldTraceDragCandidate(
					state, std::chrono::steady_clock::now()))
			{
				const auto pending = ObservePendingDrag(index,
					ppt.layout.pairVersions[pairIndex]);
				TraceDrag("constraint surface=%s collision=%d clamped=%d center_blocked=%d "
					"unchanged=%d raw=(%.1f,%.1f) accepted=(%.1f,%.1f) pending=%d mask=%u",
					SurfaceName(index), constrained.collisionRejected ? 1 : 0,
					constrained.clamped ? 1 : 0, constrained.centerBlocked ? 1 : 0,
					unchanged ? 1 : 0,
					bottom ? candidate.bottomPairWidth : candidate.middlePairWidth,
					bottom ? candidate.bottomPairHeight : candidate.middlePairHeight,
					bottom ? constrained.layout.bottomPairWidth : constrained.layout.middlePairWidth,
					bottom ? constrained.layout.bottomPairHeight : constrained.layout.middlePairHeight,
					pending.matched ? 1 : 0, static_cast<unsigned>(pending.committedSurfaceMask));
			}
			candidate = constrained.layout;
			if (unchanged)
			{
				if (!bottom)
				{
					const bool wasActive = middleSnap.active;
					PptState unchangedPpt = ppt;
					unchangedPpt.layout = candidate;
					const std::array targets{
						ResolveSurfaceLayout(Surface::MiddleLeft, monitor, dpiScale, unchangedPpt, {}),
						ResolveSurfaceLayout(Surface::MiddleRight, monitor, dpiScale, unchangedPpt, {}) };
					UpdateMiddleSnap(unchangedPpt, targets, constrained.centerSnapped, sampleTime);
					const auto pending = ObservePendingDrag(index, ppt.layout.pairVersions[pairIndex]);
					if (!middleSnap.active || pending.matched)
					{
						if (middleSnap.active || wasActive) RequestDragPair(pairIndex);
						return;
					}
					// 已知目标仍为中心、实际几何尚未到位时，也须建立一次 pending 来持有动画。
				}
				else return;
			}
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
				candidateLayouts[item] = ResolveSurfaceLayout(SurfaceFor(pair[item]), monitor, dpiScale, candidatePpt, {});
			if (!bottom) UpdateMiddleSnap(candidatePpt, candidateLayouts, constrained.centerSnapped, sampleTime);
			const bool snapMoving = !bottom && middleSnap.active;
			std::array<RECT, 2> directLogicalTargets{};
			for (std::size_t item = 0; item < pair.size(); ++item)
			{
				const Surface pairSurface = SurfaceFor(pair[item]);
				const auto previousLayout = ResolveSurfaceLayout(pairSurface,
					monitor, dpiScale, previousPpt, {});
				auto& pairState = surfaces[pair[item]];
				directTargets[item] = ResolvePptDragPresentationTarget(
					previousLayout, candidateLayouts[item],
					pairState.scene.PresentationOutsetPixels());
				// 旧 Scene 的实际倍率不同于候选时不能做只移动 HWND 的快速路径。
				if (!pairState.appliedSceneBoundsReady
					|| std::abs(pairState.appliedSceneScale
						- candidateLayouts[item].scale) > 0.0001F)
					directTargets[item].pureTranslation = false;
				directLogicalTargets[item] = !bottom && MiddleSnapMatches(pair[item], candidatePpt, candidateLayouts[item])
					? ResolveMiddleSnapBounds(pair[item], candidateLayouts[item], sampleTime) : candidateLayouts[item].logicalBounds;
				presentationTargets[item] = directTargets[item].bounds;
				OffsetRect(&presentationTargets[item], 0,
					directLogicalTargets[item].top - candidateLayouts[item].logicalBounds.top);
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
			if (!movePair(pair, presentationTargets, publication.revision))
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
					directLogicalTargets[item], candidateLayouts[item].scale);
			const auto committed = CommitDragPairDirect(pairIndex, publication.revision, !snapMoving);
			if (committed.completed || (snapMoving && committed.matched))
			{
				for (std::size_t item = 0; item < pair.size(); ++item)
					Inkeys::UI::Bar::PublishBorderCursorSurfaceBounds(
						static_cast<unsigned int>(pair[item]),
						presentationTargets[item], true);
				if (snapMoving) RequestDragPair(pairIndex);
				const auto traceNow = std::chrono::steady_clock::now();
				if (ShouldTraceDragCandidate(state, traceNow))
				{
					TraceDrag("candidate pair=%s revision=%llu result=direct "
						"replaced=%d old_revision=%llu position=(%.1f,%.1f) "
						"delta=(%ld,%ld),(%ld,%ld) targets=(%ld,%ld),(%ld,%ld) "
						"direct_move_revision=%llu pending=%d request_pair=%d "
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
						static_cast<unsigned long long>(committed.directRevision), snapMoving ? 1 : 0, snapMoving ? 1 : 0,
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

		void UpdateDragLocked(std::size_t index, POINT screen)
		{
			UpdateDragLocked(index, screen, [](const auto& pair,
				const auto& targets, std::uint64_t revision)
			{
				return MovePairWindowsDirect(pair, targets, revision);
			});
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
			std::function<void(std::uint64_t, std::uint64_t, int, int, std::uint64_t)> callback;
			{
				std::scoped_lock lock(callbackMutex);
				callback = pptCallbacks.pagePresented;
			}
			Inkeys::Drawing::Draw3::TracePptTiming(visible ? "page-ui-commit" : "page-ui-hidden",
				frame.layout.session, frame.targetRevision);
			if (callback) callback(frame.layout.session, frame.targetRevision,
				frame.currentPage, frame.totalPage, frame.pageAckEpoch);
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
			bool dragGeometryAtTarget = false;
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
					target, renderSnapshot.revision, frameContext.frameTime,
					renderSnapshot.dragPositionOwned[DragPairIndex(index)], configureFailure))
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
				if (!directionPressPolicy.trackLongPress || !state.pointerActive)
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
				const LONG outset = state.scene.PresentationOutsetPixels();
				const RECT finalPresentation{ target.logicalBounds.left - outset,
					target.logicalBounds.top - outset, target.logicalBounds.right + outset,
					target.logicalBounds.bottom + outset };
				dragGeometryAtTarget = IsPptDragGeometryAtTarget(
					state.scene.LogicalBounds(), static_cast<float>(state.bounds.scale),
					target.logicalBounds, target.scale)
					&& SameRect(presentation, finalPresentation);
				const bool transitionDeadlineActive =
					frameContext.frameTime < state.layoutTransitionUntil;
				const bool exitTransitionActive = !state.targetVisible
					&& transitionDeadlineActive;
				keepAnimating = ShouldContinuePageControlFrame(
					state.targetVisible, transitionDeadlineActive,
					state.bounds.active || (MiddleSnapMatches(index, ppt, target) && middleSnap.active), state.scene.AnimationActive(),
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
			const auto dragCommit = CommitDragSurface(index, ppt, dragGeometryAtTarget);
			if (dragCommit.matched)
				TraceDrag("consume consumer=render surface=%s revision=%llu "
					"result=%s committed_mask=%u pending=%d geometry_at_target=%d",
					SurfaceName(index),
					static_cast<unsigned long long>(dragCommit.revision),
					dragGeometryAtTarget ? "committed" : "geometry-pending",
					static_cast<unsigned>(dragCommit.committedSurfaceMask),
					dragCommit.completed ? 0 : 1, dragGeometryAtTarget ? 1 : 0);
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
			return keepAnimating || (dragCommit.matched && !dragGeometryAtTarget)
				? FrameResult::Continue : FrameResult::Idle;
		}

		void TraceRejectedInput(std::size_t index, const char* source) noexcept
		{
			auto& state = surfaces[index];
			const auto now = std::chrono::steady_clock::now();
			if (state.lastRejectedInputTrace.time_since_epoch().count() != 0
				&& now - state.lastRejectedInputTrace < DragCandidateTraceInterval) return;
			state.lastRejectedInputTrace = now;
			const auto frame = SnapshotForRender();
			const auto pending = ObservePendingDrag(index,
				frame.ppt.layout.pairVersions[DragPairIndex(index)]);
			TraceDrag("input surface=%s source=%s visible=%d locked=%d mode=%u "
				"transition_ms=%lld workspace_switching=%d pointer=%d dragging=%d pending=%d revision=%llu mask=%u",
				SurfaceName(index), source, state.targetVisible ? 1 : 0,
				state.inputLocked ? 1 : 0, static_cast<unsigned>(state.configuredMode),
				(std::max)(0LL, static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
					state.layoutTransitionUntil - now).count())), WhiteboardWorkspaceSwitching(frame.whiteboard) ? 1 : 0,
				state.pointerActive ? 1 : 0,
				state.dragging ? 1 : 0, pending.matched ? 1 : 0,
				static_cast<unsigned long long>(pending.revision),
				static_cast<unsigned>(pending.committedSurfaceMask));
		}

		void CancelSurfacePointer(std::size_t index)
		{
			auto& state = surfaces[index];
			bool rollbackDrag = false;
			const std::size_t pairIndex = DragPairIndex(index);
			{
				std::unique_lock renderLock(renderTransactionMutex);
				rollbackDrag = state.dragging || state.dragPending;
				// Up 的同步旧 CaptureChanged 已无活动 pointer，必须保留 released pending 吸附。
				if (index >= 2 && (state.pointerActive || rollbackDrag)) middleSnap = {};
				state.scene.CancelPointer();
				state.dragging = false;
				state.dragPending = false;
				state.pointerActive = false;
				++state.pointerGeneration;
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
			if (!rollbackDrag) return;
			const auto rollback = RollbackDragTracking(pairIndex);
			if (!rollback.tracked) return;
			if (rollback.discardedPending) ApplyDragPairBoundsDirect(pairIndex, rollback.layout);
			TraceDrag("cancel surface=%s pair=%s revision=%llu "
				"discarded_pending=%d rollback_position=(%.1f,%.1f)",
				SurfaceName(index), pairIndex == 0 ? "bottom" : "middle",
				static_cast<unsigned long long>(rollback.revision),
				rollback.discardedPending ? 1 : 0,
				pairIndex == 0 ? rollback.layout.bottomPairWidth : rollback.layout.middlePairWidth,
				pairIndex == 0 ? rollback.layout.bottomPairHeight : rollback.layout.middlePairHeight);
			RequestDragPair(pairIndex);
			FlushCommittedDrag(pairIndex);
		}

		struct PointerDownResult
		{
			bool accepted = false;
			bool promotePpt = false;
			bool invokeDirection = false;
			bool invokeNext = false;
			bool rawDrag = false;
		};

		template<typename Capture, typename OwnsCapture, typename ToClient, typename Release>
		[[nodiscard]] PointerDownResult BeginSurfacePointer(
			std::size_t index, POINT screen, Capture&& capture,
			OwnsCapture&& ownsCapture, ToClient&& toClient, Release&& release,
			const PointerInput::Sample* pointer = nullptr)
		{
			auto& state = surfaces[index];
			const auto pressedAt = std::chrono::steady_clock::now();
			std::uint64_t request = 0;
			std::uint64_t session = 0;
			std::uint64_t epoch = 0;
			WorkspaceMode preparedMode = WorkspaceMode::Hidden;
			PptPointerRegion preparedRegion = PptPointerRegion::Background;
			const POINT initialLocal = toClient(screen);
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible || state.inputLocked)
				{
					TraceRejectedInput(index, "down");
					return {};
				}
				const auto logical = state.scene.PresentationToLogical(initialLocal);
				if (!logical || !state.scene.HitTestBackground(*logical)) return {};
				const auto hit = state.scene.HitTest(*logical);
				preparedMode = state.configuredMode;
				preparedRegion = PointerRegionForWidget(hit);
				if (hit == Inkeys::UI::Bar::BarSurfaceNoWidget
					&& !(ResolveWorkspaceInputPolicy(preparedMode).drag
						&& CanStartPptDrag(preparedRegion))) return {};
				const auto frame = SnapshotForRender();
				session = frame.ppt.layout.session;
				epoch = frame.ppt.layout.epoch;
				request = ++state.pointerGeneration;
			}

			// SetCapture 会同步取消旧窗口手势；先在锁外完成，之后才建立本次 tracker。
			capture();
			const bool captureOwned = ownsCapture();
			const POINT local = toClient(screen); // 旧手势取消可能改变窗口，重新映射事件屏幕点。
			PointerDownResult result;
			bool releaseAbandoned = false;
			{
				std::unique_lock renderLock(renderTransactionMutex);
				const auto frame = SnapshotForRender();
				const auto logical = state.scene.PresentationToLogical(local);
				const auto hit = logical ? state.scene.HitTest(*logical)
					: Inkeys::UI::Bar::BarSurfaceNoWidget;
				const bool current = request == state.pointerGeneration
					&& session == frame.ppt.layout.session && epoch == frame.ppt.layout.epoch
					&& state.targetVisible && !state.inputLocked
					&& state.configuredMode == preparedMode
					&& ResolveWorkspaceMode(SurfaceFor(index), frame.ppt, frame.whiteboard) == preparedMode
					&& !WhiteboardWorkspaceSwitching(frame.whiteboard)
					&& logical && state.scene.HitTestBackground(*logical)
					&& PointerRegionForWidget(hit) == preparedRegion;
				// Mouse/Touch 由已验证的来源会话归属；可选 capture 失败不能阻断原生点击和 Raw 拖动。
				if ((captureOwned || (pointer && pointer->source != PointerInput::Source::Pen)) && current)
				{
					const bool dragCandidate = ResolveWorkspaceInputPolicy(preparedMode).drag
						&& CanStartPptDrag(preparedRegion);
					const auto press = hit == Inkeys::UI::Bar::BarSurfaceNoWidget
						? Inkeys::UI::Bar::BarSurfacePointerResult{}
						: state.scene.PointerDown(*logical);
					result.accepted = press.consumed || dragCandidate;
					// capture 的同步取消完成后再建立来源归属，避免旧 Cancel 清掉新手势。
					if (result.accepted && pointer)
					{
						auto& client = pointerClients[index];
						result.accepted = client.session.Begin(*pointer) != PointerInput::BeginResult::Rejected
							&& (pointer->source != PointerInput::Source::Mouse || client.mouseGesture.Begin(*pointer));
					}
					if (result.accepted)
					{
						if (index >= 2)
						{
							if (middleSnap.active) RequestDragPair(1);
							middleSnap = {};
						}
						state.pointerActive = true;
						state.pointerSerial = ++pointerClients[index].nextSerial;
						state.pointerPptSession = session;
						state.pointerLayoutEpoch = epoch;
						result.rawDrag = dragCandidate;
						state.dragRegion = preparedRegion;
						state.dragging = state.dragPending = false;
						state.pressStarted = {};
						state.lastRepeat = {};
						state.repeatTiming = {};
						result.promotePpt = preparedMode == WorkspaceMode::PptCompact;
						auto ppt = frame.ppt;
						if (dragCandidate)
						{
							state.dragging = StartsPptDragImmediately(preparedRegion);
							state.dragPending = !state.dragging;
							if (state.dragging && pointer && pointer->source == PointerInput::Source::Mouse)
								(void)pointerClients[index].mouseGesture.BeginDrag();
							state.dragStartScreen = screen;
							const auto [monitor, dpiScale] = LayoutMonitor();
							const auto budget = ResolveGroupBudgetLocked(frame, monitor, dpiScale,
								directMoveRevision.load(std::memory_order_acquire),
								Inkeys::UI::RenderPipeline::GetDeviceEpoch().generation, true);
							ppt = ResolveRuntimePageControlLayout(monitor, dpiScale, ppt,
								budget.presentationOutsetDip, budget.bitmapLimit);
							state.dragStartLayout = state.feasibleLayout = ppt.layout;
							state.lastDragCandidateTrace = {};
							state.suppressedDirectCandidateTraces = 0;
							state.suppressedPresentationBusyTraces = 0;
							if (!BeginDragTracking(DragPairIndex(index), ppt.layout))
							{
								state.dragging = state.dragPending = state.pointerActive = false;
								state.scene.CancelPointer();
								result = {};
							}
							TraceDrag("down surface=%s region=%s immediate=%d "
								"start=(%ld,%ld) position=(%.1f,%.1f)", SurfaceName(index),
								PointerRegionName(preparedRegion), state.dragging ? 1 : 0,
								screen.x, screen.y,
								DragPairIndex(index) == 0 ? ppt.layout.bottomPairWidth : ppt.layout.middlePairWidth,
								DragPairIndex(index) == 0 ? ppt.layout.bottomPairHeight : ppt.layout.middlePairHeight);
						}
						else if (press.pressed == PreviousWidget || press.pressed == NextWidget)
						{
							result.invokeNext = press.pressed == NextWidget;
							const bool repeatable = IsPptDirectionActionRepeatable(ResolvePptDirectionAction(
								result.invokeNext, ppt.currentPage, ppt.totalPage));
							const auto policy = ResolvePptDirectionPressPolicy(
								preparedMode, ppt.longPressEnabled, repeatable);
							result.invokeDirection = policy.invokeOnPointerDown;
							if (ShouldKeepPptLongPressTracking(policy.trackLongPress, state.pointerActive, true))
							{
								state.pressedNext = result.invokeNext;
								state.pressStarted = pressedAt;
								state.repeatTiming = QueryPptKeyboardRepeatTiming();
								RequestSurface(index);
							}
						}
					}
				}
				if (!result.accepted)
				{
					releaseAbandoned = request == state.pointerGeneration || !state.pointerActive;
					if (request == state.pointerGeneration)
					{
						state.scene.CancelPointer();
						if (pointer)
						{
							pointerClients[index].session.Reset();
							pointerClients[index].mouseGesture.Reset();
						}
					}
				}
			}
			if (releaseAbandoned && ownsCapture()) release();
			return result;
		}

		struct PointerUpResult
		{
			PptDragReleaseResult drag;
			bool dragEnded = false;
			bool preview = false;
			bool invokeDirection = false;
			bool invokeNext = false;
		};

		template<typename UpdateDrag>
		[[nodiscard]] PointerUpResult FinishSurfacePointer(
			std::size_t index, POINT local, POINT screen, bool screenValid,
			UpdateDrag&& updateDrag)
		{
			auto& state = surfaces[index];
			PointerUpResult result;
			std::unique_lock renderLock(renderTransactionMutex);
			const bool tracked = state.dragging || state.dragPending;
			const auto frame = SnapshotForRender();
			const bool sameLayout = !tracked
				|| (state.dragStartLayout.session == frame.ppt.layout.session
					&& state.dragStartLayout.epoch == frame.ppt.layout.epoch);
			const bool acceptInput = state.pointerActive && !state.inputLocked
				&& state.targetVisible && sameLayout
				&& ResolveWorkspaceMode(SurfaceFor(index), frame.ppt, frame.whiteboard) == state.configuredMode
				&& !WhiteboardWorkspaceSwitching(frame.whiteboard);
			if (state.pointerActive && !acceptInput) TraceRejectedInput(index, "up");
			// Up 也可能是越过阈值的最后采样；在清理手势前统一采用其屏幕坐标。
			if (acceptInput && screenValid && ShouldStartPptDragOnRelease(
				state.configuredMode, state.dragRegion, state.dragging, state.dragPending,
				state.dragStartScreen, screen, GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG)))
			{
				state.scene.CancelPointer();
				state.dragPending = false;
				state.dragging = true;
				updateDrag(index, screen);
			}
			result.dragEnded = acceptInput && state.dragging;
			WidgetId clicked = Inkeys::UI::Bar::BarSurfaceNoWidget;
			if (acceptInput && !result.dragEnded)
			{
				if (const auto logical = state.scene.PresentationToLogical(local))
					clicked = state.scene.PointerUp(*logical, false).clicked;
				else state.scene.CancelPointer();
			}
			else state.scene.CancelPointer();
			if (tracked && sameLayout)
				result.drag = EndDragTracking(DragPairIndex(index), result.dragEnded, state.dragStartLayout);
			result.preview = acceptInput && !result.dragEnded
				&& state.configuredMode == WorkspaceMode::PptCompact && clicked == PageWidget;
			result.invokeDirection = acceptInput && state.configuredMode != WorkspaceMode::PptCompact
				&& (clicked == PreviousWidget || clicked == NextWidget);
			result.invokeNext = clicked == NextWidget;
			state.pointerActive = false;
			++state.pointerGeneration;
			state.dragging = state.dragPending = false;
			state.pressStarted = {};
			state.lastRepeat = {};
			state.repeatTiming = {};
			if (result.drag.tracked)
				TraceDrag("up surface=%s pair=%s revision=%llu dragged=%d pending=%d "
					"persist=%d position=(%.1f,%.1f) suppressed_direct=%llu suppressed_lock_busy=%llu",
					SurfaceName(index), DragPairIndex(index) == 0 ? "bottom" : "middle",
					static_cast<unsigned long long>(result.drag.revision), result.dragEnded ? 1 : 0,
					result.drag.pending ? 1 : 0, result.drag.persist ? 1 : 0,
					DragPairIndex(index) == 0 ? result.drag.layout.bottomPairWidth : result.drag.layout.middlePairWidth,
					DragPairIndex(index) == 0 ? result.drag.layout.bottomPairHeight : result.drag.layout.middlePairHeight,
					static_cast<unsigned long long>(state.suppressedDirectCandidateTraces),
					static_cast<unsigned long long>(state.suppressedPresentationBusyTraces));
			state.lastDragCandidateTrace = {};
			state.suppressedDirectCandidateTraces = 0;
			state.suppressedPresentationBusyTraces = 0;
			return result;
		}

		LRESULT CALLBACK PageControlWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam);

		// 调用者持有 renderTransactionMutex；发布先于下一呈现帧时也要拒绝旧手势。
		[[nodiscard]] bool PointerWorkspaceCurrentLocked(std::size_t index)
		{
			const auto& state = surfaces[index];
			const auto [ppt, whiteboard] = Snapshot();
			const auto mode = ResolveWorkspaceMode(SurfaceFor(index), ppt, whiteboard);
			return state.targetVisible && !state.inputLocked
				&& mode == state.configuredMode
				&& !WhiteboardWorkspaceSwitching(whiteboard)
				&& (mode != WorkspaceMode::PptCompact
					|| (state.pointerPptSession == ppt.layout.session
						&& state.pointerLayoutEpoch == ppt.layout.epoch));
		}

		void DrainPointerMouse(HWND hwnd, std::size_t index)
		{
			if (!hwnd) return;
			auto& client = pointerClients[index];
			if (client.draining) return;
			client.draining = true;
			if (client.session.Active()
				&& client.session.Latest().source == PointerInput::Source::Mouse
				&& client.mouseGesture.SourceReplaced())
				(void)PageControlWindowProc(hwnd, PointerCancelMessage, 0, 0);
			PointerInput::Sample sample;
			while (client.mouse.TryPop(sample))
			{
				if (!client.session.Active()
					|| client.session.Latest().source != PointerInput::Source::Mouse) continue;
				if (sample.kind == PointerInput::Kind::Cancel)
				{
					client.rawTracking = false;
					// 未绑定补充失败只停 Raw；是否取消业务由共享策略按实际 Raw 归属裁决。
					const bool cancel = client.mouseGesture.AcceptRaw(sample);
					client.mouse.FinishMouse();
					if (cancel) (void)PageControlWindowProc(hwnd, PointerCancelMessage, 0, 0);
					continue;
				}
				if (!client.mouseGesture.AcceptRaw(sample)) continue;
				const auto accepted = client.mouseGesture.Latest();
				const UINT translated = accepted.kind == PointerInput::Kind::Up
					? WM_LBUTTONUP : WM_MOUSEMOVE;
				PointerDispatchScope scope(accepted, true);
				(void)PageControlWindowProc(hwnd, translated,
					translated == WM_LBUTTONUP ? 0 : MK_LBUTTON, 0);
			}
			client.draining = false;
		}

		LRESULT CALLBACK PageControlWindowProc(HWND hwnd, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			if (message == WM_TABLET_QUERYSYSTEMGESTURESTATUS)
				return static_cast<LRESULT>(PageControlTabletGestureStatusFlags);
			const std::size_t index = Index(hwnd);
			// 创建早期 Service 尚未发布 HWND，不能误用第一窗的会话。
			if (index >= surfaces.size()) return DefWindowProcW(hwnd, message, wParam, lParam);
			auto& state = surfaces[index];
			auto& client = pointerClients[index];
			PointerInput::Sample normalizedMouseSample;
			std::optional<PointerDispatchScope> normalizedMouseScope;
			if (message == PointerWakeMessage)
			{
				DrainPointerMouse(hwnd, index);
				return 0;
			}
			if (message == PointerCancelMessage)
			{
				if (wParam != 0)
				{
					std::unique_lock renderLock(renderTransactionMutex);
					if (state.pointerSerial != wParam) return 0;
				}
				message = WM_CANCELMODE;
			}
			if (message == WM_TIMER && wParam == PointerTimer)
			{
				// 仅按住期间轮询桌面有效性并补 drain，处理安全桌面和通知投递失败。
				if (!PointerInput::CheckInputDesktop())
				{
					(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
					return 0;
				}
				DrainPointerMouse(hwnd, index);
				bool cancel = false;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					cancel = client.session.Active()
						&& (state.inputLocked || !state.targetVisible || !state.pointerActive
							|| !PointerWorkspaceCurrentLocked(index));
				}
				if (cancel) (void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
				else if (!client.session.Active())
					KillTimer(hwnd, PointerTimer);
				return 0;
			}
			if (!dispatchedPointer && !client.draining
				&& message != WM_CANCELMODE && message != WM_DESTROY
				&& message != WM_CAPTURECHANGED
				&& !(message == WM_SHOWWINDOW && !wParam)
				&& client.session.Active())
				DrainPointerMouse(hwnd, index);
			if (message == WM_SHOWWINDOW && !wParam)
				(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
			if (message == WM_DESTROY)
			{
				(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
				client.mouse.SetWakeWindow(nullptr, 0);
			}
			if (!dispatchedPointer
				&& (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE || message == WM_LBUTTONUP)
				&& Inkeys::Message::IsPenGeneratedMouseMessage(message, GetMessageExtraInfo()))
			{
				// 笔兼容消息没有物理 Raw Mouse；保留原有 capture 路径并隔离来源。
				auto sample = NativePointerSample(hwnd, message, wParam, lParam);
				sample.source = PointerInput::Source::Pen;
				if (sample.kind == PointerInput::Kind::Down)
				{
					if (client.session.Active()
						&& client.session.Latest().source != PointerInput::Source::Mouse) return 0;
					if (client.session.Active())
						(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
					sample.generation = ++client.touchGeneration;
					if (!client.mouse.BeginTouch(sample.generation)) return 0;
					if (!SetTimer(hwnd, PointerTimer, 50, nullptr))
					{
						(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
						return 0;
					}
					for (std::size_t other = 0; other < pointerClients.size(); ++other)
						if (other != index)
							DrainPointerMouse(Inkeys::Window::GetService().Handle(Roles[other]), other);
				}
				else
				{
					if (!client.session.Active()
						|| client.session.Latest().source != PointerInput::Source::Pen) return 0;
					sample.generation = client.session.Latest().generation;
				}
				PointerDispatchScope scope(sample);
				(void)PageControlWindowProc(hwnd, message, wParam, 0);
				if (sample.kind == PointerInput::Kind::Down && !client.session.Active())
					(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
				if (sample.kind == PointerInput::Kind::Up) client.mouse.EndTouch(sample.generation);
				return 0;
			}
			if ((message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN
				|| message == WM_LBUTTONUP || message == WM_MOUSEWHEEL)
				&& !dispatchedPointer
				&& Inkeys::Message::IsTouchGeneratedMouseMessage(
					message, GetMessageExtraInfo())) return 0;
			if (message == WM_LBUTTONDOWN && !dispatchedPointer)
			{
				// 原消息立即建立按压；Raw 注册与配对结果不能阻断点击或长按。
				auto down = NativePointerSample(hwnd, message, wParam, lParam);
				if (client.session.Active())
				{
					if (client.session.Latest().source != PointerInput::Source::Mouse
						|| static_cast<LONG>(down.messageTime
							- client.session.Latest().messageTime) < 0) return 0;
					// 无捕获时旧 Up 可能落在窗外；下一次真实 Down 取消旧按压再重新命中。
					(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
				}
				for (std::size_t other = 0; other < pointerClients.size(); ++other)
				{
					if (other == index) continue;
					const HWND otherWindow = Inkeys::Window::GetService().Handle(Roles[other]);
					DrainPointerMouse(otherWindow, other);
					if (!pointerClients[other].session.Active()) continue;
					if (pointerClients[other].session.Latest().source
						!= PointerInput::Source::Mouse) return 0;
					(void)PageControlWindowProc(otherWindow, WM_CANCELMODE, 0, 0);
				}
				down.generation = ++client.mouseGeneration;
				PointerDispatchScope scope(down);
				(void)PageControlWindowProc(hwnd, message, wParam, 0);
				return 0;
			}
			if (!dispatchedPointer
				&& (message == WM_MOUSEMOVE || message == WM_LBUTTONUP)
				&& client.session.Active())
			{
				if (client.session.Latest().source != PointerInput::Source::Mouse) return 0;
				if (message == WM_MOUSEMOVE && (wParam & MK_LBUTTON) == 0) return 0;
				auto sample = NativePointerSample(hwnd, message, wParam, lParam);
				sample.generation = client.session.Latest().generation;
				PointerDispatchScope scope(sample);
				(void)PageControlWindowProc(hwnd, message, wParam, 0);
				return 0;
			}

			if ((message == WM_MOUSEMOVE || message == WM_LBUTTONUP)
				&& dispatchedPointer)
			{
				bool invalid = false;
				{
					std::unique_lock renderLock(renderTransactionMutex);
					invalid = client.session.Active()
						&& (!state.pointerActive || !PointerWorkspaceCurrentLocked(index));
				}
				if (invalid)
				{
					(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
					return 0;
				}
				if (dispatchedPointer->source == PointerInput::Source::Mouse
					&& !dispatchedMouseAccepted)
				{
					if (!client.mouseGesture.AcceptNative(*dispatchedPointer))
					{
						if (client.mouseGesture.SourceReplaced())
							(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
						return 0;
					}
					if (client.mouseGesture.Latest().kind == PointerInput::Kind::Cancel)
					{
						(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
						return 0;
					}
					// 后续末段位移与命中使用归一化终态，不能再读取迟到消息的旧位置。
					normalizedMouseSample = client.mouseGesture.Latest();
					normalizedMouseScope.emplace(normalizedMouseSample, true);
				}
				// 来源校验先于最终位移；FinishSurfacePointer 统一采用合法 Up 的末段坐标。
				if (!client.session.Accept(*dispatchedPointer)) return 0;
			}
			else if (message == WM_LBUTTONUP) return 0;
			if (message == WM_CAPTURECHANGED && client.session.Active()
				&& client.session.Latest().source == PointerInput::Source::Mouse
				&& client.rawTracking && client.mouseGesture.Dragging())
				return 0; // 实际拖动允许 Raw 延续；普通按压仍沿用捕获丢失取消。

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
						const auto device = reinterpret_cast<std::uintptr_t>(input.hSource);
						const bool down = (input.dwFlags & TOUCHEVENTF_DOWN) != 0;
						PageControlTouchLockState current;
						{
							std::unique_lock renderLock(renderTransactionMutex);
							current = { state.touchId, state.touchActive,
								state.touchPrimary, client.touchDevice };
						}
						auto decision = ResolvePageControlTouchSample(
							current, input.dwID,
							down,
							(input.dwFlags & TOUCHEVENTF_MOVE) != 0,
							(input.dwFlags & TOUCHEVENTF_UP) != 0,
							(input.dwFlags & TOUCHEVENTF_PRIMARY) != 0,
							hasPrimaryTouch, fallbackTouchLocked, device);
						fallbackTouchLocked = decision.fallbackLocked;
						if (decision.message == PageControlTouchMessage::Down
							&& client.session.Active()
							&& client.session.Latest().source != PointerInput::Source::Touch)
							decision.cancelPrevious = true;
						if (decision.cancelPrevious)
							(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
						if (decision.message == PageControlTouchMessage::None) continue;
						PointerInput::Sample sample;
						sample.source = PointerInput::Source::Touch;
						sample.screen = { TOUCH_COORD_TO_PIXEL(input.x), TOUCH_COORD_TO_PIXEL(input.y) };
						sample.tick = GetTickCount64();
						sample.messageTime = input.dwTime;
						sample.device = device;
						sample.contact = input.dwID;
						if (decision.message == PageControlTouchMessage::Down)
						{
							++client.touchGeneration;
							if (!client.mouse.BeginTouch(client.touchGeneration)) continue;
							if (!SetTimer(hwnd, PointerTimer, 50, nullptr))
							{
								client.mouse.EndTouch(client.touchGeneration);
								continue;
							}
							// 四个分页窗共用 pair 状态；新触摸先在 owner 上收尾旧鼠标。
							for (std::size_t other = 0; other < pointerClients.size(); ++other)
								if (other != index)
									DrainPointerMouse(Inkeys::Window::GetService().Handle(Roles[other]), other);
							client.touchDevice = device;
							sample.kind = PointerInput::Kind::Down;
						}
						else sample.kind = decision.message == PageControlTouchMessage::Up
							? PointerInput::Kind::Up : PointerInput::Kind::Move;
						sample.generation = client.touchGeneration;
						POINT local = sample.screen;
						ScreenToClient(hwnd, &local);
						{
							std::unique_lock renderLock(renderTransactionMutex);
							state.touchId = decision.state.id;
							state.touchActive = decision.state.active;
							state.touchPrimary = decision.state.primary;
							state.touchLastClient = decision.state.active ? local : POINT{};
						}
						PointerDispatchScope scope(sample);
						const UINT translated = sample.kind == PointerInput::Kind::Down
							? WM_LBUTTONDOWN : sample.kind == PointerInput::Kind::Up
								? WM_LBUTTONUP : WM_MOUSEMOVE;
						(void)PageControlWindowProc(hwnd, translated,
							translated == WM_LBUTTONUP ? 0 : MK_LBUTTON, 0);
						if (sample.kind == PointerInput::Kind::Down && !client.session.Active())
							(void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
						if (sample.kind == PointerInput::Kind::Up)
							client.mouse.EndTouch(sample.generation);
					}
				}
				else (void)PageControlWindowProc(hwnd, WM_CANCELMODE, 0, 0);
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
				const POINT screen = PointerScreen(hwnd, lParam);
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible || state.inputLocked)
				{
					if (state.pointerActive || (wParam & MK_LBUTTON) != 0) TraceRejectedInput(index, "move");
					return 0;
				}
				if ((!dispatchedPointer || WindowFromPoint(screen) == hwnd)
					&& ShouldNotifyPageControlCursorEntered(
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
					if (client.session.Latest().source == PointerInput::Source::Mouse)
						(void)client.mouseGesture.BeginDrag();
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
				const POINT local = PointerLocal(hwnd, lParam);
				const auto logical = state.scene.PresentationToLogical(local);
				if (state.pressStarted.time_since_epoch().count() != 0)
				{
					const WidgetId expected = state.pressedNext
						? NextWidget : PreviousWidget;
					const bool sameDirectionHit = logical.has_value()
						&& state.scene.HitTest(*logical) == expected;
					if (!ShouldKeepPptLongPressTracking(true,
						state.pointerActive, sameDirectionHit))
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
					if (!state.pointerActive || (client.session.Active()
						&& client.session.Latest().source == PointerInput::Source::Mouse
						&& !state.dragging && !state.dragPending))
					{
						if (!state.dragging) state.scene.PointerLeave();
						state.pressStarted = {};
						state.lastRepeat = {};
						state.repeatTiming = {};
					}
				}
				if (notifyBorderCursor)
					Inkeys::UI::Bar::NotifyBorderCursorSurfacePointerLeft();
				return 0;
			}
			if (message == WM_LBUTTONDOWN)
			{
				const POINT screen = PointerScreen(hwnd, lParam);
				const auto result = BeginSurfacePointer(index, screen,
					[hwnd] { SetCapture(hwnd); }, [hwnd] { return GetCapture() == hwnd; },
					[hwnd](POINT point) { ScreenToClient(hwnd, &point); return point; },
					[] { ReleaseCapture(); }, dispatchedPointer);
				if (result.accepted && dispatchedPointer->source == PointerInput::Source::Mouse)
				{
					// 只有拖动候选申请 Raw 补充；普通点击与长按继续使用原生来源。
					if (result.rawDrag)
					{
						client.mouse.SetWakeWindow(hwnd, PointerWakeMessage);
						client.rawTracking = client.mouse.BeginMouse(screen, dispatchedPointer->messageTime);
					}
					(void)SetTimer(hwnd, PointerTimer, 50, nullptr);
				}
				// Z 序和业务回调都可能同步进入 Window Service，必须位于 Scene 锁外。
				if (result.promotePpt)
					(void)Inkeys::Window::GetService().PromotePptWindow(Roles[index]);
				if (result.invokeDirection) InvokeDirection(index, result.invokeNext);
				return 0;
			}
			if (message == WM_LBUTTONUP)
			{
				KillTimer(hwnd, PointerTimer);
				const POINT local = PointerLocal(hwnd, lParam);
				const POINT screen = PointerScreen(hwnd, lParam);
				const auto result = FinishSurfacePointer(index, local, screen, true,
					[](std::size_t surface, POINT point) { UpdateDragLocked(surface, point); });
				if (dispatchedPointer->source == PointerInput::Source::Mouse)
				{
					client.mouse.FinishMouse();
					client.mouseGesture.Reset();
					client.rawTracking = false;
				}
				// 手势和 tracker 已结束；同步的旧 CaptureChanged 不得回滚刚释放的候选。
				if (GetCapture() == hwnd) ReleaseCapture();
				if (result.dragEnded)
				{
					std::scoped_lock lock(snapshotMutex);
					publishedRevision.fetch_add(1, std::memory_order_relaxed);
				}
				if (result.preview) InvokePptPreview();
				else if (result.invokeDirection) InvokeDirection(index, result.invokeNext);
				if (result.drag.pending) RequestDragPair(DragPairIndex(index));
				if (result.dragEnded) RequestAll();
				if (result.drag.tracked) FlushCommittedDrag(DragPairIndex(index));
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
				if (invoke) EnqueueLocalWheel(GET_WHEEL_DELTA_WPARAM(wParam));
				return 0;
			}
			if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
			{
				KillTimer(hwnd, PointerTimer);
				client.session.Reset();
				client.mouseGesture.Reset();
				client.rawTracking = false;
				client.mouse.FinishMouse();
				client.mouse.EndTouch();
				client.touchDevice = 0;
				CancelSurfacePointer(index);
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
		// 直接使用生产候选、真实 AnimatedBounds/Scene 和 capture 小事务；窗口操作仅在接缝处注入。
		if (!initialized.load(std::memory_order_acquire))
		{
			const auto saved = SnapshotForRender();
			const auto savedTrackers = dragCommitTrackers;
			const auto savedHandOff = handedOffDragVersions;
			const auto savedPageCommit = pageCommit;
			const auto savedDirect = directMoveRevision.load();
			const bool savedMonitor = hiddenTestMonitorEnabled.exchange(true);
			const auto savedDpi = hiddenTestDpi.exchange(USER_DEFAULT_SCREEN_DPI);
			const RECT monitor{ -30000, -30000, -28080, -28920 };
			PptState baseline = ppt;
			baseline.layout.session = 501;
			baseline.layout.epoch = 601;
			baseline.longPressEnabled = true;
			auto Prepare = [&]
			{
				{
					std::scoped_lock renderLock(renderTransactionMutex);
					for (auto& state : surfaces)
					{
						state.scene.Reset();
						state = SurfaceState{};
					}
					groupLayoutBudget = {};
					middleSnap = {};
				}
				{
					std::scoped_lock dragLock(dragCommitMutex);
					std::scoped_lock snapshotLock(snapshotMutex);
					dragCommitTrackers = {};
					handedOffDragVersions = {};
					publishedPpt = baseline;
					publishedWhiteboard = {};
					publishedRevision.fetch_add(1);
					directMoveRevision.fetch_add(1);
				}
				const auto frame = SnapshotForRender();
				std::scoped_lock renderLock(renderTransactionMutex);
				for (std::size_t index = 0; index < surfaces.size(); ++index)
				{
					const auto target = ResolveSurfaceLayout(SurfaceFor(index), monitor, 1.0F, baseline, {});
					const char* failed = nullptr;
					Check(ConfigureSurface(index, WorkspaceMode::PptCompact, baseline, {}, monitor,
						target, frame.revision, now, false, failed), "drag fixture configures real Scene");
					Check(ConfigureSurface(index, WorkspaceMode::PptCompact, baseline, {}, monitor,
						target, frame.revision, now + 1s, false, failed), "drag fixture completes normal entry");
				}
			};
			auto ScreenForWidget = [&](std::size_t index, WidgetId widget)
			{
				const auto contracts = ResolvePptWidgetContracts(SurfaceFor(index));
				const auto& bounds = contracts[widget - 1].bounds;
				const auto& state = surfaces[index];
				const auto body = state.scene.LogicalBounds();
				return POINT{ body.left + static_cast<LONG>(std::lround(
					(bounds.left + bounds.right) * state.bounds.scale / 2.0)),
					body.top + static_cast<LONG>(std::lround(
						(bounds.top + bounds.bottom) * state.bounds.scale / 2.0)) };
			};
			auto ToClient = [&](std::size_t index, POINT screen)
			{
				const auto bounds = surfaces[index].scene.PresentationBounds();
				return POINT{ screen.x - bounds.left, screen.y - bounds.top };
			};
			auto Down = [&](std::size_t index, POINT screen)
			{
				return BeginSurfacePointer(index, screen, [] {}, [] { return true; },
					[&](POINT point) { return ToClient(index, point); }, [] {});
			};
			auto AbsorbSurface = [&](std::size_t index, const PptState& frame,
				std::chrono::steady_clock::time_point time, bool commit)
			{
				const auto ownership = SnapshotForRender();
				const auto target = ResolveSurfaceLayout(SurfaceFor(index), monitor, 1.0F, frame, {});
				bool atTarget = false;
				{
					std::scoped_lock renderLock(renderTransactionMutex);
					const char* failed = nullptr;
					Check(ConfigureSurface(index, WorkspaceMode::PptCompact, frame, {}, monitor,
						target, ownership.revision, time, ownership.dragPositionOwned[DragPairIndex(index)], failed),
						"released pair configures real Scene");
					atTarget = IsPptDragGeometryAtTarget(surfaces[index].scene.LogicalBounds(),
						static_cast<float>(surfaces[index].bounds.scale), target.logicalBounds, target.scale);
					const LONG outset = surfaces[index].scene.PresentationOutsetPixels();
					const RECT presentation{ target.logicalBounds.left - outset, target.logicalBounds.top - outset,
						target.logicalBounds.right + outset, target.logicalBounds.bottom + outset };
					Check(atTarget && !surfaces[index].bounds.active
						&& SameRect(surfaces[index].scene.PresentationBounds(), presentation),
						"drag Scene logical and presentation geometry have no remainder");
				}
				if (commit) (void)CommitDragSurface(index, frame, atTarget);
			};
			auto AbsorbPair = [&](std::size_t pairIndex, const PptState& frame,
				std::chrono::steady_clock::time_point time, bool commit)
			{
				for (const std::size_t index : DragPair(pairIndex)) AbsorbSurface(index, frame, time, commit);
			};
			// 两面分别由渲染吸收，最后一个 Up 没有 Move；原始屏幕起点不受窗口直移影响。
			for (const std::size_t index : { 0U, 1U, 2U, 3U })
			{
				Prepare();
				const POINT down = ScreenForWidget(index, PageWidget);
				Check(Down(index, down).accepted && surfaces[index].dragPending, "Page down waits for drag threshold");
				const POINT up{ down.x + (index % 2 == 0 ? 120L : -120L), down.y + 70 };
				std::array<RECT, 2> windows{};
				const auto pair = DragPair(DragPairIndex(index));
				for (std::size_t item = 0; item < pair.size(); ++item)
					windows[item] = surfaces[pair[item]].scene.PresentationBounds();
				auto move = [&](const auto& indices, const auto& targets, std::uint64_t revision)
				{
					return MovePairWindowsDirect(indices, targets, revision,
						[&](std::size_t item, RECT& value) { value = windows[item]; return true; },
						[&](std::size_t item, const RECT& value) { windows[item] = value; return true; });
				};
				const auto released = FinishSurfacePointer(index, ToClient(index, up), up, true,
					[&](std::size_t surface, POINT point) { UpdateDragLocked(surface, point, move); });
				Check(released.dragEnded && !released.preview && !released.drag.pending,
					"Up alone crosses threshold, directly commits both windows and suppresses preview");
				const auto finalFrame = SnapshotForRender().ppt;
				Check(DragPairIndex(index) == 0 ? finalFrame.layout.bottomPairWidth == 120.0F
					: finalFrame.layout.middlePairWidth == 120.0F, "Up final screen coordinate reaches pair");
				CancelSurfacePointer(index); // 模拟 ReleaseCapture 的同步旧 Cancel。
				Check(SameDragPairPosition(DragPairIndex(index), finalFrame.layout,
					SnapshotForRender().ppt.layout), "release capture cannot roll final candidate back");
				for (const auto elapsed : { 0ms, 16ms, 80ms, 200ms, 600ms })
					AbsorbPair(DragPairIndex(index), finalFrame, now + 2s + elapsed, false);
			}
			// 另一线程真实占有 P；最后采样和 Up 必须走生产 try_lock fallback，不调用窗口移动。
			Prepare();
			const std::size_t index = 2;
			const POINT down = ScreenForWidget(index, DragWidget);
			Check(Down(index, down).accepted, "busy fixture starts immediate drag");
			std::promise<void> locked;
			std::promise<void> unlock;
			auto unlockSignal = unlock.get_future();
			std::jthread holder([&]
			{
				std::unique_lock presentationLock(presentationMutex);
				locked.set_value();
				(void)unlockSignal.wait_for(5s);
			});
			Check(locked.get_future().wait_for(2s) == std::future_status::ready, "other thread holds presentation mutex");
			int busyMoves = 0;
			auto unexpectedMove = [&](const auto&, const auto&, std::uint64_t) { ++busyMoves; return true; };
			const POINT last{ down.x + 80, down.y + 90 };
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				UpdateDragLocked(index, last, unexpectedMove);
			}
			const auto oldFrame = SnapshotForRender();
			const POINT up{ down.x + 110, down.y + 105 };
			const auto released = FinishSurfacePointer(index, ToClient(index, up), up, true,
				[&](std::size_t surface, POINT point) { UpdateDragLocked(surface, point, unexpectedMove); });
			unlock.set_value();
			holder.join();
			const auto finalFrame = SnapshotForRender();
			Check(busyMoves == 0 && released.drag.pending && released.dragEnded,
				"real lock contention retains final released candidate without another Move");
			Check(!IsPageControlFrameRevisionCurrent(oldFrame.ppt.layout.pairVersions[1],
				finalFrame.ppt.layout.pairVersions[1]), "late final Up supersedes in-flight frame revision");
			(void)CommitDragSurface(2, oldFrame.ppt, true);
			(void)CommitDragSurface(3, oldFrame.ppt, true);
			Check(dragCommitTrackers[1].pending, "stale frame cannot complete new candidate");
			// 真实 bounds 已朝同一目标插值时，正确版本的两个中间帧也不能提前提交。
			for (const std::size_t surface : DragPair(1))
			{
				const auto target = ResolveSurfaceLayout(SurfaceFor(surface), monitor, 1.0F, finalFrame.ppt, {});
				bool atTarget = true;
				{
					std::scoped_lock renderLock(renderTransactionMutex);
					auto& state = surfaces[surface];
					RetargetBounds(state.bounds, target.logicalBounds, target.scale, true, now + 2s);
					AdvanceBounds(state.bounds, now + 2s + 40ms);
					Check(ApplySceneBounds(state, CurrentBounds(state.bounds), static_cast<float>(state.bounds.scale)),
						"actual intermediate Scene bounds apply");
					atTarget = IsPptDragGeometryAtTarget(state.scene.LogicalBounds(),
						static_cast<float>(state.bounds.scale), target.logicalBounds, target.scale);
					Check(state.bounds.active && !atTarget, "fixture has a real animated intermediate frame");
				}
				(void)CommitDragSurface(surface, finalFrame.ppt, atTarget);
			}
			Check(dragCommitTrackers[1].pending && dragCommitTrackers[1].committedSurfaceMask == 0,
				"candidate revision alone cannot commit intermediate geometry");
			AbsorbPair(1, finalFrame.ppt, now + 2s + 41ms, true);
			Check(!dragCommitTrackers[1].pending && dragCommitTrackers[1].released,
				"both real configured surfaces complete released pending");
			for (const auto elapsed : { 56ms, 80ms, 200ms, 600ms })
				AbsorbPair(1, finalFrame.ppt, now + 2s + elapsed, false);

			// raw 与 fit 不同时，released pending 期间再次零位移拖动不能破坏候选身份。
			const auto normalBaseline = baseline;
			baseline.layout.middlePairScale = 3.0F;
			const bool savedBudgetOverride = hiddenTestBudgetOverrideEnabled.exchange(true);
			Prepare();
			const auto fittedBaseline = ResolveRuntimePageControlLayout(monitor, 1.0F, baseline, 35.0F, 512);
			Check(fittedBaseline.layout.middlePairScale != baseline.layout.middlePairScale,
				"repeated gesture fixture really has distinct raw and fitted scales");
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				for (const std::size_t surface : DragPair(1))
				{
					const auto target = ResolveSurfaceLayout(SurfaceFor(surface), monitor, 1.0F, fittedBaseline, {});
					SetBoundsDirect(surfaces[surface].bounds, target.logicalBounds, target.scale);
					const char* failed = nullptr;
					Check(ConfigureSurface(surface, WorkspaceMode::PptCompact, fittedBaseline, {}, monitor,
						target, publishedRevision.load(), now + 1500ms, false, failed),
						"repeated gesture starts with actual fitted Scene geometry");
				}
			}
			const auto oldFittedBounds = surfaces[2].scene.LogicalBounds();
			const POINT firstFitDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, firstFitDown).accepted, "first fitted gesture starts");
			const POINT firstFitUp{ firstFitDown.x + 80, firstFitDown.y + 100 };
			int fitMoveAttempts = 0;
			auto failedFitMove = [&](const auto&, const auto&, std::uint64_t) { ++fitMoveAttempts; return false; };
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				UpdateDragLocked(2, firstFitUp, failedFitMove);
			}
			const auto firstFitRelease = FinishSurfacePointer(2, ToClient(2, firstFitUp), firstFitUp, true,
				[&](std::size_t surface, POINT point) { UpdateDragLocked(surface, point, failedFitMove); });
			const auto firstFitTracker = dragCommitTrackers[1];
			Check(firstFitRelease.drag.pending && firstFitTracker.released && fitMoveAttempts == 1,
				"first fitted gesture releases a pending candidate without moving actual bounds");
			const auto pendingFitFrame = SnapshotForRender();
			const auto finalFitted = ResolveRuntimePageControlLayout(monitor, 1.0F, pendingFitFrame.ppt, 35.0F, 512);
			const auto finalFittedTarget = ResolveSurfaceLayout(Surface::MiddleLeft, monitor, 1.0F, finalFitted, {});
			Check(!SameRect(oldFittedBounds, finalFittedTarget.logicalBounds)
				&& pendingFitFrame.dragPositionOwned[1], "fitted pair has a real pending translation and raw ownership");
			const POINT secondFitDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, secondFitDown).accepted, "second fitted gesture starts before pending render");
			Check(dragCommitTrackers[1].pending && dragCommitTrackers[1].revision == firstFitTracker.revision
				&& SameDragPairPosition(1, dragCommitTrackers[1].committedLayout, firstFitTracker.committedLayout)
				&& dragCommitTrackers[1].committedLayout.pairVersions == firstFitTracker.committedLayout.pairVersions
				&& dragCommitTrackers[1].committedLayout.middlePairScale == firstFitTracker.committedLayout.middlePairScale
				&& SnapshotForRender().dragPositionOwned[1],
				"second Down preserves canonical pending identity and last committed layout");
			const auto secondFitRelease = FinishSurfacePointer(2, ToClient(2, secondFitDown), secondFitDown, true,
				[&](std::size_t surface, POINT point) { UpdateDragLocked(surface, point, failedFitMove); });
			const auto repeatedFitFrame = SnapshotForRender();
			Check(secondFitRelease.drag.pending && fitMoveAttempts == 1
				&& repeatedFitFrame.ppt.layout.pairVersions[1] == firstFitTracker.revision
				&& repeatedFitFrame.dragPositionOwned[1]
				&& SameRect(surfaces[2].scene.LogicalBounds(), oldFittedBounds),
				"zero-Move Up keeps the same pending revision, ownership and old actual geometry");
			const auto repeatedFitted = ResolveRuntimePageControlLayout(monitor, 1.0F, repeatedFitFrame.ppt, 35.0F, 512);
			AbsorbPair(1, repeatedFitted, now + 2s, true);
			Check(!dragCommitTrackers[1].pending, "first fallback frame settles both fitted surfaces after repeated Up");
			for (const auto elapsed : { 16ms, 80ms, 600ms }) AbsorbPair(1, repeatedFitted, now + 2s + elapsed, false);
			hiddenTestBudgetOverrideEnabled.store(savedBudgetOverride);
			baseline = normalBaseline;

			baseline.layout.middlePairHeight = 120.0F;
			Prepare();
			const POINT snapDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, snapDown).accepted, "snap release fixture starts away from center");
			const POINT snapUp{ snapDown.x, snapDown.y + 100 };
			const auto snapped = FinishSurfacePointer(2, ToClient(2, snapUp), snapUp, true,
				[&](std::size_t surface, POINT point)
				{
					UpdateDragLocked(surface, point, [](const auto&, const auto&, std::uint64_t) { return true; });
				});
			const auto snappedFrame = SnapshotForRender().ppt;
			Check(snapped.dragEnded && snappedFrame.layout.middlePairHeight == 0.0F,
				"Up final sample enters 20 DIP center and side band using production candidate strategy");
			for (const auto elapsed : { 0ms, 80ms, 600ms }) AbsorbPair(1, snappedFrame, now + 2s + elapsed, false);
			baseline.layout.middlePairHeight = 0.0F;

			// 实际 UI3 吸附 Y：共享起点/时间，中间帧不能当最终提交，带内 X 仍即时跟随。
			baseline.layout.middlePairHeight = 30.0F;
			const auto snapTime = now + 2s;
			auto RenderSnapFrame = [&](std::chrono::steady_clock::time_point time, bool commit,
				std::uint8_t commitMask = PptDragCommittedSurfaceMask)
			{
				const auto snapshot = SnapshotForRender();
				std::array<RECT, 2> actual{};
				for (const std::size_t surface : DragPair(1))
				{
					const auto target = ResolveSurfaceLayout(SurfaceFor(surface), monitor, 1.0F, snapshot.ppt, {});
					bool atTarget = false;
					{
						std::scoped_lock renderLock(renderTransactionMutex);
						const char* failed = nullptr;
						Check(ConfigureSurface(surface, WorkspaceMode::PptCompact, snapshot.ppt, {}, monitor,
							target, snapshot.revision, time, snapshot.dragPositionOwned[1], failed),
							"snap frame configures actual production Scene");
						actual[surface - 2] = surfaces[surface].scene.LogicalBounds();
						atTarget = IsPptDragGeometryAtTarget(actual[surface - 2],
							static_cast<float>(surfaces[surface].bounds.scale), target.logicalBounds, target.scale);
						Check(!surfaces[surface].inputLocked, "snap animation does not lock input");
					}
					if (commit && (commitMask & (1U << (surface - 2))) != 0)
						(void)CommitDragSurface(surface, snapshot.ppt, atTarget);
				}
				return actual;
			};
			auto SuccessfulSnapMove = [](const auto&, const auto&, std::uint64_t) { return true; };
			auto SampleSnap = [&](POINT point, std::chrono::steady_clock::time_point time)
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				UpdateDragLocked(2, point, SuccessfulSnapMove, time);
			};
			auto ReleaseSnap = [&](POINT point, std::chrono::steady_clock::time_point time)
			{
				return FinishSurfacePointer(2, ToClient(2, point), point, true,
					[&](std::size_t surface, POINT screen) { UpdateDragLocked(surface, screen, SuccessfulSnapMove, time); });
			};
			Prepare();
			const POINT upOnlyDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, upOnlyDown).accepted, "Up-only snap fixture starts");
			const LONG upOnlyStart = surfaces[2].scene.LogicalBounds().top;
			const auto upOnlyRelease = ReleaseSnap({ upOnlyDown.x, upOnlyDown.y + 15 }, snapTime);
			CancelSurfacePointer(2);
			const auto upOnlyMiddle = RenderSnapFrame(snapTime + 70ms, true);
			Check(upOnlyRelease.dragEnded && upOnlyRelease.drag.pending && middleSnap.active
				&& upOnlyMiddle[0].top > upOnlyStart && dragCommitTrackers[1].pending,
				"Up without a final Move starts a real intermediate snap from actual geometry");
			(void)RenderSnapFrame(snapTime + MiddleSnapDuration, true);
			Check(!dragCommitTrackers[1].pending && !middleSnap.active, "Up-only snap settles at its own short deadline");
			Prepare();
			const POINT animatedDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, animatedDown).accepted, "animated snap starts outside the 20 DIP band");
			const LONG startTop = surfaces[2].scene.LogicalBounds().top;
			SampleSnap({ animatedDown.x, animatedDown.y + 15 }, snapTime);
			const auto snapStarted = middleSnap.started;
			Check(middleSnap.active && dragCommitTrackers[1].pending
				&& CurrentBounds(surfaces[2].bounds).top == startTop,
				"snap entry retains actual Y and starts one pending 140ms correction");
			const auto firstSnapFrame = SnapshotForRender();
			const auto targetSnap = ResolveSurfaceLayout(Surface::MiddleLeft, monitor, 1.0F, firstSnapFrame.ppt, {});
			const auto firstIntermediate = RenderSnapFrame(snapTime + 35ms, true);
			Check(firstIntermediate[0].top > startTop && firstIntermediate[0].top < targetSnap.logicalBounds.top
				&& firstIntermediate[0].top == firstIntermediate[1].top
				&& dragCommitTrackers[1].committedSurfaceMask == 0,
				"both actual Scenes share an intermediate Y without committing");
			SampleSnap({ animatedDown.x + 10, animatedDown.y + 14 }, snapTime + 50ms);
			const auto movedSnap = SnapshotForRender();
			const auto movedTarget = ResolveSurfaceLayout(Surface::MiddleLeft, monitor, 1.0F, movedSnap.ppt, {});
			Check(middleSnap.started == snapStarted && movedSnap.ppt.layout.middlePairWidth == 10.0F
				&& CurrentBounds(surfaces[2].bounds).left == movedTarget.logicalBounds.left,
				"continuous Move changes X immediately without restarting snap");
			auto pageUpdate = movedSnap.ppt;
			++pageUpdate.publicationRevision;
			++pageUpdate.currentPage;
			PublishPptState(pageUpdate);
			const auto afterPage = RenderSnapFrame(snapTime + 55ms, true);
			Check(middleSnap.started == snapStarted && afterPage[0].top >= firstIntermediate[0].top,
				"page publication preserves monotonic snap progress");
			const POINT animatedUp{ animatedDown.x + 12, animatedDown.y + 13 };
			SampleSnap(animatedUp, snapTime + 60ms);
			const auto owner60 = SnapshotForRender();
			const LONG owner60Top = CurrentBounds(surfaces[2].bounds).top;
			// 新候选由 owner 在 t60 直移，renderer 此后才取快照，但 batch 时钟仍为 t40。
			const auto lateBatch40 = RenderSnapFrame(snapTime + 40ms, true);
			const auto newestLeft = ResolveSurfaceLayout(Surface::MiddleLeft, monitor, 1.0F, owner60.ppt, {});
			const auto newestRight = ResolveSurfaceLayout(Surface::MiddleRight, monitor, 1.0F, owner60.ppt, {});
			Check(SnapshotForRender().ppt.layout.pairVersions[1] == owner60.ppt.layout.pairVersions[1]
				&& lateBatch40[0].top >= owner60Top && lateBatch40[1].top >= owner60Top
				&& lateBatch40[0].left == newestLeft.logicalBounds.left
				&& lateBatch40[1].left == newestRight.logicalBounds.left,
				"latest candidate with an older batch clock cannot reverse either Scene or stale X");
			const auto newer80 = RenderSnapFrame(snapTime + 80ms, true);
			Check(newer80[0].top >= lateBatch40[0].top && newer80[1].top >= lateBatch40[1].top
				&& middleSnap.started == snapStarted && middleSnap.sampledAt == snapTime + 80ms,
				"next t80 frame advances the shared clock without extending the original deadline");
			const auto animatedRelease = ReleaseSnap(animatedUp, snapTime + 85ms);
			CancelSurfacePointer(2); // 与 ReleaseCapture 的同步旧 CaptureChanged 相同入口。
			Check(animatedRelease.drag.pending && middleSnap.active && middleSnap.started == snapStarted,
				"Up and its old synchronous Cancel preserve the released snap and original deadline");
			(void)CommitDragSurface(2, firstSnapFrame.ppt, true);
			Check(dragCommitTrackers[1].committedSurfaceMask == 0,
				"older snap candidate cannot commit after a newer X sample");
			LONG previousTop = newer80[0].top;
			for (const auto elapsed : { 90ms, 100ms, 110ms })
			{
				const auto actual = RenderSnapFrame(snapTime + elapsed, true);
				Check(actual[0].top >= previousTop && actual[0].top <= targetSnap.logicalBounds.top
					&& actual[0].top == actual[1].top && dragCommitTrackers[1].pending,
					"released pair advances monotonically while retaining pending ownership");
				previousTop = actual[0].top;
			}
			(void)RenderSnapFrame(snapTime + MiddleSnapDuration, true, 0x01);
			Check(!middleSnap.active && dragCommitTrackers[1].pending
				&& dragCommitTrackers[1].committedSurfaceMask == 0x01,
				"one final surface with the other still retrying cannot complete the pair");
			(void)RenderSnapFrame(snapTime + 150ms, true, 0x02);
			Check(!dragCommitTrackers[1].pending, "other final surface success completes the released snap pair");
			const auto settledSnap = SnapshotForRender().ppt;
			for (const auto elapsed : { 160ms, 240ms, 600ms }) AbsorbPair(1, settledSnap, snapTime + elapsed, false);

			// 完成后仍在带内的 Move 不重播；离带、真正 Cancel 和新手势均清退旧状态。
			Prepare();
			const POINT heldDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, heldDown).accepted, "held snap fixture starts");
			SampleSnap({ heldDown.x, heldDown.y + 15 }, snapTime);
			(void)RenderSnapFrame(snapTime + MiddleSnapDuration, true);
			const auto completedStart = middleSnap.started;
			SampleSnap({ heldDown.x + 5, heldDown.y + 16 }, snapTime + 180ms);
			Check(!middleSnap.active && middleSnap.started == completedStart && !dragCommitTrackers[1].pending,
				"completed snap does not replay when X moves inside the capture band");
			SampleSnap({ heldDown.x + 30, heldDown.y + 16 }, snapTime + 190ms);
			const auto detached = SnapshotForRender().ppt;
			Check(!middleSnap.latched && detached.layout.middlePairHeight == 14.0F,
				"leaving the side-distance condition restores immediate free Y");
			AbsorbPair(1, detached, snapTime + 190ms, false);
			Prepare();
			const POINT cancelDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, cancelDown).accepted, "cancel snap fixture starts");
			SampleSnap({ cancelDown.x, cancelDown.y + 15 }, snapTime);
			(void)RenderSnapFrame(snapTime + 50ms, true);
			CancelSurfacePointer(2);
			Check(!middleSnap.latched && !dragCommitTrackers[1].pending
				&& SnapshotForRender().ppt.layout.middlePairHeight == 30.0F,
				"true active Cancel clears snap and restores last committed position");
			Prepare();
			const POINT gestureDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, gestureDown).accepted, "new gesture snap fixture starts");
			SampleSnap({ gestureDown.x, gestureDown.y + 15 }, snapTime);
			(void)ReleaseSnap({ gestureDown.x, gestureDown.y + 15 }, snapTime + 40ms);
			Check(Down(2, ScreenForWidget(2, DragWidget)).accepted && !middleSnap.latched
				&& SnapshotForRender().dragPositionOwned[1], "new gesture interrupts old snap without losing raw candidate identity");
			Prepare();
			const POINT sessionDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, sessionDown).accepted, "session snap fixture starts");
			SampleSnap({ sessionDown.x, sessionDown.y + 15 }, snapTime);
			(void)RenderSnapFrame(snapTime + 50ms, false);
			auto nextSession = baseline;
			++nextSession.layout.session;
			PublishPptState(nextSession);
			(void)RenderSnapFrame(snapTime + 60ms, false);
			Check(!middleSnap.latched && !SnapshotForRender().dragPositionOwned[1],
				"new session invalidates old snap before configuring its own layout");

			// 两种真实生产兜底：P 锁忙、第二窗失败回滚，均让 released snap 按原截止点完成。
			for (const bool lockBusy : { true, false })
			{
				Prepare();
				const POINT retryDown = ScreenForWidget(2, DragWidget);
				Check(Down(2, retryDown).accepted, "retry snap fixture starts");
				std::promise<void> acquired;
				std::promise<void> releaseLock;
				auto releaseSignal = releaseLock.get_future();
				std::jthread contender;
				if (lockBusy)
				{
					contender = std::jthread([&]
					{
						std::unique_lock presentationLock(presentationMutex);
						acquired.set_value();
						(void)releaseSignal.wait_for(5s);
					});
					Check(acquired.get_future().wait_for(2s) == std::future_status::ready,
						"snap retry actually contends with another thread");
				}
				std::array<RECT, 2> windows{ surfaces[2].scene.PresentationBounds(), surfaces[3].scene.PresentationBounds() };
				const auto originalWindows = windows;
				std::vector<std::size_t> calls;
				auto failSecond = [&](const auto& indices, const auto& targets, std::uint64_t revision)
				{
					return MovePairWindowsDirect(indices, targets, revision,
						[&](std::size_t item, RECT& value) { value = windows[item]; return true; },
						[&](std::size_t item, const RECT& value)
						{
							calls.push_back(item);
							if (item == 1) { SetLastError(ERROR_BUSY); return false; }
							windows[item] = value; return true;
						});
				};
				const POINT retryUp{ retryDown.x + 10, retryDown.y + 15 };
				{
					std::scoped_lock renderLock(renderTransactionMutex);
					UpdateDragLocked(2, retryUp, failSecond, snapTime);
				}
				const auto retryRelease = FinishSurfacePointer(2, ToClient(2, retryUp), retryUp, true,
					[&](std::size_t surface, POINT point) { UpdateDragLocked(surface, point, failSecond, snapTime + 40ms); });
				if (lockBusy) { releaseLock.set_value(); contender.join(); }
				Check(retryRelease.drag.pending && middleSnap.started == snapTime
					&& (lockBusy ? calls.empty() : calls == std::vector<std::size_t>{ 0, 1, 0 })
					&& SameRect(windows[0], originalWindows[0]) && SameRect(windows[1], originalWindows[1]),
					"retry preserves original snap timeline and pair rollback");
				CancelSurfacePointer(2);
				(void)RenderSnapFrame(snapTime + 70ms, true);
				Check(dragCommitTrackers[1].pending && middleSnap.active, "retry keeps a real intermediate released frame");
				(void)RenderSnapFrame(snapTime + MiddleSnapDuration, true);
				Check(!dragCommitTrackers[1].pending && !middleSnap.active, "retry converges at original deadline without new input");
			}
			baseline.layout.middlePairHeight = 0.0F;

			// 第二窗失败必须回滚第一窗；之后单侧完成也不能提前完成 pair。
			Prepare();
			const POINT failedDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, failedDown).accepted, "failed move fixture starts drag");
			const std::array<RECT, 2> original{ surfaces[2].scene.PresentationBounds(), surfaces[3].scene.PresentationBounds() };
			auto windows = original;
			std::vector<std::size_t> moveOrder;
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				UpdateDragLocked(2, { failedDown.x + 90, failedDown.y + 120 },
					[&](const auto& indices, const auto& targets, std::uint64_t revision)
					{
						return MovePairWindowsDirect(indices, targets, revision,
							[&](std::size_t item, RECT& value) { value = windows[item]; return true; },
							[&](std::size_t item, const RECT& value)
							{
								moveOrder.push_back(item);
								if (item == 1) { SetLastError(ERROR_BUSY); return false; }
								windows[item] = value;
								return true;
							});
					});
			}
			Check(moveOrder == std::vector<std::size_t>{ 0, 1, 0 }
				&& SameRect(windows[0], original[0]) && SameRect(windows[1], original[1])
				&& dragCommitTrackers[1].pending, "second failure rolls first window back and retains pending");
			const auto failedFrame = SnapshotForRender().ppt;
			AbsorbSurface(2, failedFrame, now + 2s, true);
			Check(dragCommitTrackers[1].pending && dragCommitTrackers[1].committedSurfaceMask == 1,
				"one recovered client cannot complete pair");
			AbsorbPair(1, failedFrame, now + 2s, true);
			Check(!dragCommitTrackers[1].pending, "both clients eventually recover without new input");

			// 无拖动所有权时，正常位置/倍率/工作区转场继续动画，不能整体关闭缓动。
			Prepare();
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				const char* failed = nullptr;
				auto changed = baseline;
				changed.layout.middlePairWidth = 150.0F;
				auto target = ResolveSurfaceLayout(Surface::MiddleLeft, monitor, 1.0F, changed, {});
				Check(ConfigureSurface(2, WorkspaceMode::PptCompact, changed, {}, monitor,
					target, publishedRevision.load(), now + 2s, false, failed)
					&& surfaces[2].bounds.active && !SameRect(CurrentBounds(surfaces[2].bounds), target.logicalBounds),
					"ordinary position changes retain bounds animation");
				changed.layout.middlePairScale = 1.5F;
				target = ResolveSurfaceLayout(Surface::MiddleLeft, monitor, 1.0F, changed, {});
				Check(ConfigureSurface(2, WorkspaceMode::PptCompact, changed, {}, monitor,
					target, publishedRevision.load(), now + 2s + 20ms, true, failed)
					&& surfaces[2].bounds.active, "owned pair does not suppress scale transition");
			}

			// SetCapture 注入同步旧 Cancel，执行与 WndProc 完全相同的小事务，禁止锁内 capture。
			Prepare();
			const POINT captureDown = ScreenForWidget(2, DragWidget);
			Check(Down(2, captureDown).accepted, "old capture gesture exists");
			{
				std::scoped_lock renderLock(renderTransactionMutex);
				UpdateDragLocked(2, { captureDown.x + 80, captureDown.y + 100 },
					[](const auto&, const auto&, std::uint64_t) { return false; });
			}
			Check(dragCommitTrackers[1].pending, "old capture owns an uncommitted candidate");
			bool captureCanEnterRenderLock = false;
			bool oldCancelledBeforeNewTracking = false;
			const auto transferred = BeginSurfacePointer(3, ScreenForWidget(3, DragWidget), [&]
			{
				std::jthread probe([&]
				{
					std::unique_lock probeLock(renderTransactionMutex, std::try_to_lock);
					captureCanEnterRenderLock = probeLock.owns_lock();
				});
				probe.join();
				if (!captureCanEnterRenderLock) return;
				CancelSurfacePointer(2);
				oldCancelledBeforeNewTracking = !surfaces[2].pointerActive && !dragCommitTrackers[1].ownsLayout;
			}, [] { return true; }, [&](POINT point) { return ToClient(3, point); }, [] {});
			Check(transferred.accepted && captureCanEnterRenderLock && oldCancelledBeforeNewTracking
				&& surfaces[3].pointerActive && dragCommitTrackers[1].ownsLayout,
				"synchronous capture cancellation finishes before new same-pair tracking");
			int abandonedReleases = 0;
			const auto abandoned = BeginSurfacePointer(3, ScreenForWidget(3, DragWidget),
				[&] { CancelSurfacePointer(3); }, [] { return true; },
				[&](POINT point) { return ToClient(3, point); }, [&] { ++abandonedReleases; });
			Check(!abandoned.accepted && abandonedReleases == 1 && !surfaces[3].pointerActive,
				"capture-time gesture invalidation leaves no new tracking");
			Prepare();
			const auto notCaptured = BeginSurfacePointer(2, ScreenForWidget(2, DragWidget),
				[] {}, [] { return false; }, [&](POINT point) { return ToClient(2, point); }, [] {});
			Check(!notCaptured.accepted && !surfaces[2].pointerActive && !dragCommitTrackers[1].ownsLayout,
				"failed capture does not establish tracking");
			int staleReleases = 0;
			const auto staleCapture = BeginSurfacePointer(2, ScreenForWidget(2, DragWidget), [&]
			{
				auto next = baseline;
				++next.layout.session;
				PublishPptState(next);
			}, [] { return true; }, [&](POINT point) { return ToClient(2, point); }, [&] { ++staleReleases; });
			Check(!staleCapture.accepted && staleReleases == 1 && !dragCommitTrackers[1].ownsLayout,
				"capture-time new session rejects stale Down and releases capture");
			Prepare();
			const auto arrow = Down(0, ScreenForWidget(0, PreviousWidget));
			Check(arrow.accepted && arrow.invokeDirection && !surfaces[0].dragPending
				&& surfaces[0].pressStarted.time_since_epoch().count() != 0,
				"capture precedes one immediate arrow action and long-press tracking");
			CancelSurfacePointer(0);
			const POINT click = ScreenForWidget(0, PageWidget);
			Check(Down(0, click).accepted, "Page click fixture starts");
			int clickMoves = 0;
			const auto clicked = FinishSurfacePointer(0, ToClient(0, click), click, true,
				[&](std::size_t, POINT) { ++clickMoves; });
			Check(clicked.preview && !clicked.dragEnded && clickMoves == 0,
				"Page Up within threshold preserves preview without final drag sample");

			// 合并后的真实 Down/Up helper 必须保留来源校验，Mouse/Touch 不依赖 capture 成功。
			for (const auto source : { PointerInput::Source::Mouse, PointerInput::Source::Touch,
				PointerInput::Source::Pen })
			{
				Prepare();
				auto& client = pointerClients[0];
				PointerInput::Sample down;
				down.kind = PointerInput::Kind::Down;
				down.source = source;
				down.screen = ScreenForWidget(0, PageWidget);
				down.generation = ++client.mouseGeneration;
				down.messageTime = 100;
				down.tick = 100;
				const auto pressed = BeginSurfacePointer(0, down.screen, [] {}, [] { return false; },
					[&](POINT point) { return ToClient(0, point); }, [] {}, &down);
				if (source == PointerInput::Source::Pen)
				{
					Check(!pressed.accepted && !client.session.Active() && !surfaces[0].pointerActive,
						"pen compatibility retains capture requirement without creating a stale source");
					continue;
				}
				Check(pressed.accepted && pressed.rawDrag && client.session.Active()
					&& surfaces[0].dragPending && surfaces[0].pointerPptSession == baseline.layout.session
					&& surfaces[0].pointerLayoutEpoch == baseline.layout.epoch,
					"native source binds after capture attempt with current PPT session and drag candidate");
				auto up = down;
				up.kind = PointerInput::Kind::Up;
				up.screen.x += 120;
				up.screen.y += 70;
				up.messageTime = 120;
				up.tick = 120;
				if (source == PointerInput::Source::Mouse)
					Check(client.mouseGesture.AcceptNative(up), "native Up remains valid without a Raw claim");
				Check(client.session.Accept(up), "source ownership validates final Up before geometry changes");
				int updates = 0;
				POINT applied{};
				const auto released = FinishSurfacePointer(0, ToClient(0, up.screen), up.screen, true,
					[&](std::size_t surface, POINT point)
					{
						++updates;
						applied = point;
						UpdateDragLocked(surface, point,
							[](const auto&, const auto&, std::uint64_t) { return false; });
					});
				Check(released.dragEnded && !released.preview && updates == 1
					&& applied.x == up.screen.x && applied.y == up.screen.y && !surfaces[0].pointerActive,
					"source-validated final Up crosses threshold once and never opens preview");
				client.mouseGesture.Reset();
				client.session.Reset();
				CancelSurfacePointer(0);
			}

			{
				std::scoped_lock renderLock(renderTransactionMutex);
				for (auto& state : surfaces) { state.scene.Reset(); state = SurfaceState{}; }
				groupLayoutBudget = {};
				middleSnap = {};
			}
			{
				std::scoped_lock dragLock(dragCommitMutex);
				std::scoped_lock snapshotLock(snapshotMutex);
				publishedPpt = saved.ppt;
				publishedWhiteboard = saved.whiteboard;
				publishedRevision.store(saved.revision);
				directMoveRevision.store(savedDirect);
				dragCommitTrackers = savedTrackers;
				handedOffDragVersions = savedHandOff;
				pageCommit = savedPageCommit;
			}
			hiddenTestDpi.store(savedDpi);
			hiddenTestMonitorEnabled.store(savedMonitor);
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
				std::uint64_t, int, int, std::uint64_t) { acknowledged.fetch_add(1); } });
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
		wheelInputGeneration.fetch_add(1, std::memory_order_acq_rel);
		{
			std::scoped_lock lock(localWheelMutex);
			localWheelRead = localWheelWrite = localWheelCount = 0;
		}
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
		middleSnap = {};
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

	bool CanAcceptPptWheel(const Business::PptSessionToken& session) noexcept
	{
		if (!initialized.load(std::memory_order_acquire) || !session.active) return false;
		const auto [ppt, whiteboard] = Snapshot();
		return ppt.presentationVisible && ppt.totalPage > 0
			&& ppt.layout.session == session.localSession
			&& !whiteboard.expandedLayoutTarget && !whiteboard.active
			&& !WhiteboardWorkspaceSwitching(whiteboard);
	}

	void ResetWheelInput() noexcept
	{
		// 仅 Bar owner 改累计器；原生线程只捕获原子代次和投递值。
		wheelInputGeneration.fetch_add(1, std::memory_order_acq_rel);
		globalWheelAccumulator.Reset();
		localWheelAccumulator.Reset();
		localWheelSession = 0;
	}

	void ConsumePptWheel(const Business::PptSessionToken& session,
		std::uint64_t routeGeneration, short delta) noexcept
	{
		if (!delta || !CanAcceptPptWheel(session)
			|| !Business::IsCurrentPptInputSession(session))
		{
			globalWheelAccumulator.Reset();
			return;
		}
		const auto [ppt, whiteboard] = Snapshot();
		if (ppt.layout.session != session.localSession || !ppt.presentationVisible
			|| whiteboard.expandedLayoutTarget || whiteboard.active
			|| WhiteboardWorkspaceSwitching(whiteboard)) return;
		const int steps = globalWheelAccumulator.Consume(routeGeneration, delta);
		// 完整刻度立即尝试既有业务门禁；被节流拒绝的不排队等待以后补翻。
		for (int remaining = std::abs(steps); remaining > 0; --remaining)
			Business::QueuePptInputDirection(session, steps < 0, ppt.currentPage, ppt.totalPage);
	}

	void DrainLocalWheel() noexcept
	{
		for (unsigned batch = 0; batch < 16; ++batch)
		{
			LocalWheelRequest request;
			{
				std::scoped_lock lock(localWheelMutex);
				if (!localWheelCount) break;
				request = localWheels[localWheelRead];
				localWheelRead = (localWheelRead + 1) % localWheels.size();
				--localWheelCount;
			}
			if (request.generation != wheelInputGeneration.load(std::memory_order_acquire)
				|| !CanAcceptPptWheel(request.session)
				|| !Business::IsCurrentPptInputSession(request.session))
			{
				localWheelAccumulator.Reset();
				continue;
			}
			if (localWheelSession != request.session.localSession)
			{
				localWheelSession = request.session.localSession;
				localWheelAccumulator.Reset();
			}
			const auto [ppt, whiteboard] = Snapshot();
			if (!ppt.presentationVisible || ppt.layout.session != request.session.localSession
				|| whiteboard.active || whiteboard.expandedLayoutTarget
				|| WhiteboardWorkspaceSwitching(whiteboard)) continue;
			const int steps = localWheelAccumulator.Consume(request.generation, request.delta);
			for (int remaining = std::abs(steps); remaining > 0; --remaining)
				Business::QueuePptInputDirection(request.session,
					steps < 0, ppt.currentPage, ppt.totalPage);
		}
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
				if (index >= 2 && (surface.pointerActive || surface.dragging || surface.dragPending)) middleSnap = {};
				surface.scene.CancelPointer();
				surface.pointerActive = false;
				surface.dragging = false;
				surface.dragPending = false;
				surface.pointerActive = false;
				++surface.pointerGeneration;
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
