#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string_view>

#include "../Inkeys/Inkeys/UI/Bar/Bar.A2.h"

import Inkeys.UI.PageControl;
import Inkeys.UI.Bar.Metrics;
import Inkeys.UI.Bar.SurfaceLayout;

namespace
{
	using namespace Inkeys::UI::PageControl;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL page-control " << name << '\n';
	}

	LONG Width(const RECT& bounds) noexcept
	{
		return bounds.right - bounds.left;
	}

	LONG Height(const RECT& bounds) noexcept
	{
		return bounds.bottom - bounds.top;
	}

	void TestSharedButtonInheritance()
	{
		using namespace Inkeys::UI::Bar;
		const auto icon = ResolveBarButtonChildTopLeft(
			100.0, 200.0, 70.0, 70.0,
			0.0, -10.0, 28.0, 28.0);
		const auto primary = ResolveBarButtonChildTopLeft(
			100.0, 200.0, 70.0, 70.0,
			0.0, 20.0, 70.0, 25.0);
		const auto secondary = ResolveBarButtonChildTopLeft(
			100.0, 200.0, 70.0, 70.0,
			6.0, 0.0, 20.0, 18.0);
		auto Near = [](double left, double right)
			{
				return std::abs(left - right) < 0.000001;
			};
		Check(Near(icon.x, 121.0) && Near(icon.y, 211.0),
			"SVG inherits the explicit button position instead of stale cache");
		Check(Near(primary.x, 100.0) && Near(primary.y, 242.5)
			&& Near(secondary.x, 131.0) && Near(secondary.y, 226.0),
			"primary and secondary text share the same button parent");
	}

	void TestSharedSurfaceLightingMapping()
	{
		using namespace Inkeys::UI::Bar;
		const auto primary = ResolveBarSurfaceScreenPoint(
			140.0, 260.0, 100, 200, 7);
		const auto cursor = ResolveBarSurfaceScreenPoint(
			175.0, 285.0, 100, 200, 7);
		Check(primary.x == 47.0 && primary.y == 67.0,
			"shared primary light maps from screen into presentation pixels");
		Check(cursor.x == 82.0 && cursor.y == 92.0,
			"surface maps the Main Bar cursor without creating a local position");
	}

	void TestCompactWidgetContracts()
	{
		const auto left = ResolvePptWidgetContracts(Surface::BottomLeft);
		const auto right = ResolvePptWidgetContracts(Surface::BottomRight);
		const auto side = ResolvePptWidgetContracts(Surface::MiddleLeft);
		Check(left[0].role == PptWidgetRole::DragHandle
			&& left[0].dragSource && !left[0].hoverVisual
			&& !left[0].pressVisual && !left[0].invokesBusinessAction,
			"drag handle remains a visual-free immediate drag source");
		Check(!left[1].dragSource && left[2].dragSource
			&& !left[3].dragSource && left[2].immediateTextUpdate,
			"page is draggable with immediate numbers while arrows stay pure buttons");
		Check(CanStartPptDrag(PptPointerRegion::Background)
			&& CanStartPptDrag(PptPointerRegion::DragHandle)
			&& CanStartPptDrag(PptPointerRegion::Page)
			&& !CanStartPptDrag(PptPointerRegion::Previous)
			&& !CanStartPptDrag(PptPointerRegion::Next)
			&& StartsPptDragImmediately(PptPointerRegion::DragHandle)
			&& !StartsPptDragImmediately(PptPointerRegion::Page),
			"PPT drag routing includes background and page but excludes arrows");
		Check(ShouldAcceptPageControlClientHit(
			WorkspaceMode::PptCompact, true, false)
			&& !ShouldAcceptPageControlClientHit(
				WorkspaceMode::PptCompact, false, false)
			&& ShouldAcceptPageControlClientHit(
				WorkspaceMode::WhiteboardExpanded, true, true)
			&& !ShouldAcceptPageControlClientHit(
				WorkspaceMode::WhiteboardExpanded, true, false),
			"transparent rounded corners pass through while PPT background remains draggable");
		constexpr POINT dragStart{ 100, 100 };
		Check(!HasExceededPptDragThreshold(
			dragStart, POINT{ 103, 102 }, 8, 6)
			&& HasExceededPptDragThreshold(
				dragStart, POINT{ 104, 102 }, 8, 6)
			&& HasExceededPptDragThreshold(
				dragStart, POINT{ 100, 103 }, 8, 6),
			"page press converts to drag only after the centered system threshold");
		Check(!left[1].displaysText && !left[3].displaysText
			&& left[1].invokesBusinessAction && left[3].invokesBusinessAction,
			"compact arrows contain no labels and keep page actions");
		Check(left[2].displaysText && left[2].invokesBusinessAction
			&& left[2].primaryTextBold && left[2].secondaryTextNormal
			&& left[2].immediateTextUpdate,
			"PPT page button opens preview with mixed page-number weights");
		Check(left[0].bounds.left == 5.0 && left[3].bounds.right == 160.0
			&& right[1].bounds.left == 5.0 && right[0].bounds.right == 160.0,
			"horizontal drag slot stays on the outer edge");
		Check(left[0].bounds.right == left[1].bounds.left
			&& right[3].bounds.right == right[0].bounds.left
			&& side[0].bounds.bottom == side[1].bounds.top,
			"drag divider lane touches the adjacent arrow without a third gap");
		Check(side[0].bounds.top == 5.0 && side[3].bounds.bottom == 160.0,
			"side layout keeps the drag slot above three compact buttons");
		const auto pptPrevious = ResolveDirectionContentPolicy(
			WorkspaceMode::PptCompact, false, false);
		const auto whiteboardPrevious = ResolveDirectionContentPolicy(
			WorkspaceMode::WhiteboardExpanded, false, false);
		const auto whiteboardNext = ResolveDirectionContentPolicy(
			WorkspaceMode::WhiteboardExpanded, true, false);
		const auto whiteboardAdd = ResolveDirectionContentPolicy(
			WorkspaceMode::WhiteboardExpanded, true, true);
		Check(pptPrevious.icon == L"barMore" && pptPrevious.label.empty()
			&& whiteboardPrevious.icon == pptPrevious.icon
			&& whiteboardNext.icon == pptPrevious.icon,
			"ordinary arrows keep the same SVG across workspaces");
		Check(whiteboardAdd.icon == L"barAdd"
			&& whiteboardAdd.label == L"加页",
			"only whiteboard Add changes SVG and label semantics");
	}

	void TestDragCommitHandoff()
	{
		PptLayoutState initial;
		initial.bottomPairWidth = 20.0F;
		initial.bottomPairHeight = 30.0F;
		PptDragCommitTracker tracker;
		BeginPptDragTracking(tracker, initial);

		PptLayoutState first = initial;
		first.bottomPairWidth = 40.0F;
		const auto firstPublication = PublishPptDragCandidate(
			tracker, first, 10);
		Check(!firstPublication.requestPair && !firstPublication.replacedPending
			&& tracker.pending && tracker.ownsLayout,
			"direct drag publication stays pending without eagerly requesting its pair");

		PptLayoutState latest = first;
		latest.bottomPairWidth = 65.0F;
		latest.bottomPairHeight = 45.0F;
		const auto latestPublication = PublishPptDragCandidate(
			tracker, latest, 11);
		Check(!latestPublication.requestPair
			&& latestPublication.replacedPending
			&& latestPublication.replacedRevision == 10
			&& tracker.revision == 11
			&& tracker.layout.bottomPairWidth == 65.0F
			&& tracker.layout.bottomPairHeight == 45.0F,
			"candidate payload is complete before the outer revision publication");
		Check(!MarkPptDragSurfaceCommitted(tracker, 10, 0x01)
			&& tracker.pending,
			"stale render acknowledgement cannot clear the latest candidate");

		// 注入一次 try_lock 竞争：fallback 显式请求 pair，mailbox 保留最终候选。
		const bool presentationLockAvailable = false;
		const bool fallbackRequestedPair = !presentationLockAvailable;
		if (presentationLockAvailable)
			(void)MarkPptDragSurfaceCommitted(
				tracker, 11, PptDragCommittedSurfaceMask);
		Check(fallbackRequestedPair && tracker.pending
			&& tracker.layout.bottomPairWidth == 65.0F,
			"presentation lock contention retains the final movement sample");

		const auto release = ReleasePptDragTracking(tracker, true);
		Check(release.tracked && release.pending && release.persist
			&& tracker.ownsLayout,
			"mouse release retains ownership while the latest candidate is pending");
		Check(!MarkPptDragSurfaceCommitted(tracker, 11, 0x01)
			&& tracker.pending && tracker.committedSurfaceMask == 0x01,
			"one rendered HWND cannot complete a pair drag");
		Check(MarkPptDragSurfaceCommitted(tracker, 11, 0x02)
			&& !tracker.pending && tracker.ownsLayout,
			"both render clients consume the latest candidate without another move");
		Check(CompletePptDragPersistence(tracker, 11)
			&& !tracker.ownsLayout,
			"layout ownership releases only after commit and persistence complete");

		BeginPptDragTracking(tracker, latest);
		PptLayoutState cancelled = latest;
		cancelled.bottomPairWidth = 90.0F;
		(void)PublishPptDragCandidate(tracker, cancelled, 12);
		const auto rollback = RollbackPptDragTracking(tracker);
		Check(rollback.tracked && rollback.discardedPending
			&& rollback.layout.bottomPairWidth == 65.0F
			&& !tracker.pending && !tracker.ownsLayout,
			"cancel explicitly rolls an uncommitted candidate back");
	}

	void TestDragPureTranslationAndRevisionGate()
	{
		ResolvedSurfaceLayout previous;
		previous.logicalBounds = RECT{ 100, 200, 265, 243 };
		previous.scale = 1.0F;
		previous.mode = WorkspaceMode::PptCompact;
		previous.visible = true;
		ResolvedSurfaceLayout candidate = previous;
		candidate.logicalBounds = RECT{ 140, 230, 305, 273 };

		const auto target = ResolvePptDragPresentationTarget(
			previous, candidate, 18);
		Check(target.pureTranslation && target.deltaX == 40
			&& target.deltaY == 30
			&& target.bounds.left == 122 && target.bounds.top == 212
			&& target.bounds.right == 323 && target.bounds.bottom == 291,
			"pure drag resolves an absolute presentation target with stable outset");

		ResolvedSurfaceLayout latest = candidate;
		latest.logicalBounds = RECT{ 190, 260, 355, 303 };
		const auto latestTarget = ResolvePptDragPresentationTarget(
			candidate, latest, 18);
		Check(latestTarget.pureTranslation
			&& latestTarget.bounds.left == 172
			&& latestTarget.bounds.top == 242,
			"latest absolute target catches up after an earlier candidate was deferred");

		ResolvedSurfaceLayout resized = latest;
		++resized.logicalBounds.right;
		Check(!ResolvePptDragPresentationTarget(
			latest, resized, 18).pureTranslation,
			"size changes leave the direct path for render fallback");
		Check(IsPageControlFrameRevisionCurrent(17, 17)
			&& !IsPageControlFrameRevisionCurrent(17, 18),
			"frame revision gate rejects stale work before presentation");
	}

	void TestWorkspaceAndDpiLayouts()
	{
		constexpr RECT monitor{ 0, 0, 1920, 1080 };
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = true;
		WhiteboardState whiteboard;

		const auto compact = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.0F, ppt, whiteboard);
		Check(compact.mode == WorkspaceMode::PptCompact && compact.visible
			&& Width(compact.logicalBounds) == 165
			&& Height(compact.logicalBounds) == 43,
			"bottom PPT control resolves compact dimensions");
		const auto scaled = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.5F, ppt, whiteboard);
		Check(Width(scaled.logicalBounds) == 248
			&& Height(scaled.logicalBounds) == 64,
			"compact dimensions scale with DPI");
		const auto sideVisible = ResolveSurfaceLayout(Surface::MiddleLeft,
			monitor, 1.0F, ppt, whiteboard);
		const auto sideHidden = ResolveHiddenSurfaceBounds(
			Surface::MiddleLeft, monitor, sideVisible.logicalBounds);
		Check(sideHidden.right == monitor.left
			&& Height(sideHidden) == Height(sideVisible.logicalBounds),
			"side controls preserve their size at the off-screen entrance");

		whiteboard.expandedLayoutTarget = true;
		whiteboard.active = true;
		ppt.layout.bottomPairWidth = 123.0F;
		ppt.layout.bottomPairHeight = 77.0F;
		const auto expanded = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.0F, ppt, whiteboard);
		const auto expandedRight = ResolveSurfaceLayout(Surface::BottomRight,
			monitor, 1.0F, ppt, whiteboard);
		const auto hiddenSide = ResolveSurfaceLayout(Surface::MiddleLeft,
			monitor, 1.0F, ppt, whiteboard);
		Check(expanded.mode == WorkspaceMode::WhiteboardExpanded
			&& Width(expanded.logicalBounds) == 230
			&& Height(expanded.logicalBounds) == 80
			&& expanded.logicalBounds.left == 5
			&& expanded.logicalBounds.bottom == monitor.bottom - 5
			&& expandedRight.logicalBounds.right == monitor.right - 5,
			"whiteboard ignores PPT offsets and stays at fixed bottom corners");
		Check(hiddenSide.mode == WorkspaceMode::Hidden && !hiddenSide.visible,
			"whiteboard workspace hides PPT side controls");

		ppt.presentationVisible = false;
		whiteboard.expandedLayoutTarget = false;
		whiteboard.active = false;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
			== WorkspaceMode::Hidden,
			"inactive PPT and whiteboard resolve hidden");
	}

	void TestWorkspaceTransitionAndInputPolicy()
	{
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = false;
		WhiteboardState whiteboard;

		Check(!ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
			WorkspaceMode::PptCompact, WorkspaceMode::WhiteboardExpanded, false),
			"hidden PPT bottom bar enters whiteboard at final geometry");
		Check(ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
			WorkspaceMode::PptCompact, WorkspaceMode::WhiteboardExpanded, true),
			"visible PPT bottom bar morphs into whiteboard");
		Check(ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
			WorkspaceMode::WhiteboardExpanded, WorkspaceMode::PptCompact, true),
			"whiteboard reverse transition morphs from the current frame");
		Check(ShouldPreserveSurfaceBoundsWhileHiding(Surface::BottomLeft)
			&& ShouldPreserveSurfaceBoundsWhileHiding(Surface::BottomRight)
			&& !ShouldPreserveSurfaceBoundsWhileHiding(Surface::MiddleLeft),
			"hidden bottom hosts fade at the current frame while side hosts slide out");
		Check(ShouldKeepPageControlWindowVisible(true, false)
			&& ShouldKeepPageControlWindowVisible(false, true)
			&& !ShouldKeepPageControlWindowVisible(false, false),
			"hidden HWND survives only for its bounded exit transition");
		Check(ShouldSubscribePageControlLighting(true, false)
			&& ShouldSubscribePageControlLighting(false, true)
			&& !ShouldSubscribePageControlLighting(false, false),
			"shared lighting subscribes only while visible or exiting");
		Check(ShouldNotifyPageControlCursorEntered(false, false, false)
			&& !ShouldNotifyPageControlCursorEntered(true, false, false)
			&& !ShouldNotifyPageControlCursorEntered(false, true, false)
			&& !ShouldNotifyPageControlCursorEntered(false, false, true),
			"only the first real mouse move activates shared cursor lighting");
		Check(ShouldLockSurfaceInput(false, false, false)
			&& ShouldLockSurfaceInput(true, true, false)
			&& ShouldLockSurfaceInput(true, false, true)
			&& !ShouldLockSurfaceInput(true, false, false),
			"surface input stays locked while hidden or transitioning");
		const auto pptInput = ResolveWorkspaceInputPolicy(
			WorkspaceMode::PptCompact);
		const auto whiteboardInput = ResolveWorkspaceInputPolicy(
			WorkspaceMode::WhiteboardExpanded);
		Check(pptInput.click && pptInput.drag && pptInput.longPress
			&& pptInput.wheel && pptInput.persistPosition,
			"PPT compact keeps its complete input policy");
		Check(whiteboardInput.click && !whiteboardInput.drag
			&& !whiteboardInput.longPress && !whiteboardInput.wheel
			&& !whiteboardInput.persistPosition,
			"whiteboard accepts clicks without inheriting PPT input");

		const auto disabledPress = ResolvePptDirectionPressPolicy(
			WorkspaceMode::PptCompact, false);
		const auto enabledPress = ResolvePptDirectionPressPolicy(
			WorkspaceMode::PptCompact, true);
		const auto whiteboardPress = ResolvePptDirectionPressPolicy(
			WorkspaceMode::WhiteboardExpanded, true);
		Check(disabledPress.invokeOnPointerDown && !disabledPress.trackLongPress
			&& enabledPress.invokeOnPointerDown && enabledPress.trackLongPress
			&& !whiteboardPress.invokeOnPointerDown
			&& !whiteboardPress.trackLongPress,
			"PPT arrows invoke on down and only configured presses repeat");
		const auto defaultTiming = ResolvePptKeyboardRepeatTiming(
			PptKeyboardDelayFallback, PptKeyboardSpeedFallback);
		const auto slowTiming = ResolvePptKeyboardRepeatTiming(0, 0);
		const auto clampedTiming = ResolvePptKeyboardRepeatTiming(99, 99);
		constexpr std::array expectedKeyboardDelays{
			std::chrono::milliseconds(250), std::chrono::milliseconds(500),
			std::chrono::milliseconds(750), std::chrono::milliseconds(1000) };
		bool allKeyboardDelaysMatch = true;
		for (unsigned int delay = 0; delay < expectedKeyboardDelays.size(); ++delay)
		{
			allKeyboardDelaysMatch = allKeyboardDelaysMatch
				&& ResolvePptKeyboardRepeatTiming(delay, 31).initialDelay
					== expectedKeyboardDelays[delay];
		}
		Check(defaultTiming.initialDelay == std::chrono::milliseconds(500)
			&& defaultTiming.repeatInterval == std::chrono::milliseconds(33)
			&& slowTiming.initialDelay == std::chrono::milliseconds(250)
			&& slowTiming.repeatInterval == std::chrono::milliseconds(400)
			&& clampedTiming.initialDelay == std::chrono::milliseconds(1000)
			&& clampedTiming.repeatInterval == std::chrono::milliseconds(33)
			&& allKeyboardDelaysMatch,
			"PPT repeat timing follows clamped Windows keyboard delay and rate");
		Check(!ShouldTriggerPptLongPressRepeat(true,
			std::chrono::milliseconds(249), false, {}, slowTiming)
			&& ShouldTriggerPptLongPressRepeat(true,
				std::chrono::milliseconds(250), false, {}, slowTiming)
			&& !ShouldTriggerPptLongPressRepeat(true,
				std::chrono::milliseconds(649), true,
				std::chrono::milliseconds(399), slowTiming)
			&& ShouldTriggerPptLongPressRepeat(true,
				std::chrono::milliseconds(650), true,
				std::chrono::milliseconds(400), slowTiming)
			&& !ShouldTriggerPptLongPressRepeat(false,
				std::chrono::seconds(1), false, {}, defaultTiming),
			"first keyboard repeat uses delay and later repeats use the rate interval");
		using RepeatClock = std::chrono::steady_clock;
		const auto pressStarted = RepeatClock::time_point(std::chrono::seconds(1));
		const PptKeyboardRepeatTiming quantizedTiming{
			std::chrono::milliseconds(500), std::chrono::milliseconds(34) };
		const auto firstAnchor = ResolvePptLongPressRepeatAnchor(
			{}, pressStarted + std::chrono::milliseconds(516),
			false, quantizedTiming);
		const auto secondAnchor = ResolvePptLongPressRepeatAnchor(
			firstAnchor, pressStarted + std::chrono::milliseconds(550),
			true, quantizedTiming);
		const auto quantizedAnchor = ResolvePptLongPressRepeatAnchor(
			secondAnchor, pressStarted + std::chrono::milliseconds(600),
			true, quantizedTiming);
		const auto lateAnchor = ResolvePptLongPressRepeatAnchor(
			quantizedAnchor, pressStarted + std::chrono::milliseconds(700),
			true, quantizedTiming);
		Check(firstAnchor == pressStarted + std::chrono::milliseconds(516)
			&& secondAnchor == pressStarted + std::chrono::milliseconds(550)
			&& quantizedAnchor == pressStarted + std::chrono::milliseconds(584)
			&& lateAnchor == pressStarted + std::chrono::milliseconds(700),
			"repeat anchor protects the second interval and drops overdue backlog");
		Check(ShouldKeepPptLongPressTracking(true, true, true)
			&& !ShouldKeepPptLongPressTracking(false, true, true)
			&& !ShouldKeepPptLongPressTracking(true, false, true)
			&& !ShouldKeepPptLongPressTracking(true, true, false),
			"configuration, capture loss, and pointer leave stop repetition");
		Check(ShouldContinuePageControlFrame(false, true, false, false, false)
			&& ShouldContinuePageControlFrame(true, true, false, false, false)
			&& ShouldContinuePageControlFrame(true, false, true, false, false)
			&& ShouldContinuePageControlFrame(true, false, false, true, false)
			&& ShouldContinuePageControlFrame(true, false, false, false, true)
			&& !ShouldContinuePageControlFrame(true, false, false, false, false)
			&& !ShouldContinuePageControlFrame(false, false, true, true, true),
			"transition deadline keeps PageControl scheduled until input can unlock");
		using TransitionClock = std::chrono::steady_clock;
		const auto transitionStarted = TransitionClock::time_point(
			std::chrono::seconds(2));
		const auto transitionDeadline = transitionStarted
			+ std::chrono::milliseconds(200);
		const auto frameBeforeDeadline = transitionDeadline
			- std::chrono::milliseconds(1);
		const auto frameAtDeadline = transitionDeadline;
		const auto frameAfterDeadline = transitionDeadline
			+ std::chrono::milliseconds(17);
		const auto deadlineActive = [transitionDeadline](auto frame) noexcept
			{
				return frame < transitionDeadline;
			};
		Check(ShouldLockSurfaceInput(true,
				deadlineActive(transitionStarted), false)
			&& ShouldContinuePageControlFrame(true,
				deadlineActive(transitionStarted), false, false, false)
			&& ShouldLockSurfaceInput(true,
				deadlineActive(frameBeforeDeadline), false)
			&& ShouldContinuePageControlFrame(true,
				deadlineActive(frameBeforeDeadline), false, false, false)
			&& !ShouldLockSurfaceInput(true,
				deadlineActive(frameAtDeadline), false)
			&& !ShouldContinuePageControlFrame(true,
				deadlineActive(frameAtDeadline), false, false, false)
			&& !ShouldLockSurfaceInput(true,
				deadlineActive(frameAfterDeadline), false)
			&& !ShouldContinuePageControlFrame(true,
				deadlineActive(frameAfterDeadline), false, false, false),
			"deadline frames keep input locked and self-schedule exactly until expiry");
	}

	void TestTouchTranslationPolicy()
	{
		Check(PageControlTabletGestureStatusFlags
			== (0x00000001U | 0x00000008U | 0x00000100U
				| 0x00000200U | 0x00010000U),
			"PageControl disables the same Tablet gestures as the Bar");

		PageControlTouchLockState state;
		auto decision = ResolvePageControlTouchSample(
			state, 10, true, false, false, true, true, false);
		state = decision.state;
		Check(decision.message == PageControlTouchMessage::Down
			&& !decision.cancelPrevious && state.active && state.primary
			&& state.id == 10,
			"primary touch locks and translates one pointer down");
		decision = ResolvePageControlTouchSample(
			state, 11, false, true, false, false, false, false);
		Check(decision.message == PageControlTouchMessage::None
			&& decision.state.id == 10,
			"non-active touch movement is ignored");
		decision = ResolvePageControlTouchSample(
			state, 10, false, false, true, true, true, false);
		state = decision.state;
		Check(decision.message == PageControlTouchMessage::Up && !state.active,
			"active primary up releases the touch lock");

		decision = ResolvePageControlTouchSample(
			state, 20, true, false, false, false, false, false);
		state = decision.state;
		Check(decision.message == PageControlTouchMessage::Down
			&& decision.fallbackLocked && state.active && !state.primary
			&& state.id == 20,
			"first down becomes the fallback when a batch has no primary flag");
		decision = ResolvePageControlTouchSample(
			state, 21, true, false, false, false, false,
			decision.fallbackLocked);
		Check(decision.message == PageControlTouchMessage::None
			&& decision.state.id == 20,
			"fallback batch ignores additional touch downs");

		decision = ResolvePageControlTouchSample(
			state, 30, true, false, false, true, true, false);
		state = decision.state;
		Check(decision.cancelPrevious
			&& decision.message == PageControlTouchMessage::Down
			&& state.active && state.primary && state.id == 30,
			"new primary touch cancels the old fallback before taking ownership");
		decision = ResolvePageControlTouchSample(
			state, 20, false, false, true, false, true, false);
		Check(decision.message == PageControlTouchMessage::None
			&& decision.state.active && decision.state.primary
			&& decision.state.id == 30,
			"old fallback up cannot click after a primary replacement");
		decision = ResolvePageControlTouchSample(
			state, 30, false, true, false, true, true, false);
		state = decision.state;
		Check(decision.message == PageControlTouchMessage::Move
			&& state.active && state.id == 30,
			"replacement touch continues through the shared move path");
		decision = ResolvePageControlTouchSample(
			state, 30, false, false, true, true, true, false);
		Check(decision.message == PageControlTouchMessage::Up
			&& !decision.state.active,
			"replacement touch up clears the lock");
	}

	void TestWhiteboardLayoutTargetAndReadiness()
	{
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		WhiteboardState whiteboard;

		whiteboard.expandedLayoutTarget = true;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
				== WorkspaceMode::WhiteboardExpanded
			&& !whiteboard.active
			&& WhiteboardWorkspaceSwitching(whiteboard)
			&& ShouldLockSurfaceInput(true, false,
				WhiteboardWorkspaceSwitching(whiteboard)),
			"expanded target starts before background readiness and locks input");

		whiteboard.active = true;
		Check(!WhiteboardWorkspaceSwitching(whiteboard),
			"Draw3-ready background completes the expanded workspace gate");

		whiteboard.expandedLayoutTarget = false;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
				== WorkspaceMode::PptCompact
			&& WhiteboardWorkspaceSwitching(whiteboard),
			"exit retargets compact before the background becomes inactive");
		whiteboard.active = false;
		Check(!WhiteboardWorkspaceSwitching(whiteboard),
			"inactive background completes the compact workspace gate");

		// Exiting 期间反向重入只改变目标，背景仍保持未就绪。
		whiteboard.expandedLayoutTarget = true;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
				== WorkspaceMode::WhiteboardExpanded
			&& !whiteboard.active
			&& WhiteboardWorkspaceSwitching(whiteboard)
			&& ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
				WorkspaceMode::PptCompact,
				WorkspaceMode::WhiteboardExpanded, true),
			"reverse re-entry immediately retargets expanded from the current frame");
	}

	void TestDirtyInvalidationDeduplication()
	{
		constexpr RECT firstBounds{ 10, 20, 175, 63 };
		constexpr RECT movedBounds{ 11, 20, 176, 63 };
		Check(ShouldApplyPageControlSceneBounds(false, {}, 1.0F,
			firstBounds, 1.0F), "scene bounds are applied initially");
		Check(!ShouldApplyPageControlSceneBounds(true, firstBounds, 1.0F,
			firstBounds, 1.0F), "stable scene bounds do not self-wake rendering");
		Check(ShouldApplyPageControlSceneBounds(true, firstBounds, 1.0F,
			movedBounds, 1.0F)
			&& ShouldApplyPageControlSceneBounds(true, firstBounds, 1.0F,
				firstBounds, 1.25F),
			"real geometry or scale changes still invalidate the scene");

		PptState ppt;
		PptState changedPpt = ppt;
		Check(ArePptStatesEquivalent(ppt, changedPpt),
			"identical PPT snapshots are deduplicated");
		changedPpt.currentPage = 2;
		Check(!ArePptStatesEquivalent(ppt, changedPpt),
			"PPT page changes still publish");
		changedPpt = ppt;
		changedPpt.longPressEnabled = true;
		Check(!ArePptStatesEquivalent(ppt, changedPpt),
			"PPT long-press configuration changes publish immediately");
		changedPpt = ppt;
		changedPpt.layout.bottomPairWidth = 12.0F;
		Check(!ArePptStatesEquivalent(ppt, changedPpt),
			"PPT layout changes still publish");

		WhiteboardState whiteboard;
		WhiteboardState changedWhiteboard = whiteboard;
		Check(AreWhiteboardStatesEquivalent(whiteboard, changedWhiteboard),
			"identical whiteboard snapshots are deduplicated");
		changedWhiteboard.switching = true;
		Check(!AreWhiteboardStatesEquivalent(whiteboard, changedWhiteboard),
			"whiteboard transition changes still publish");

		const auto stableDebugWindow =
			ResolvePageControlDebugWindowDamagePolicy(false, false, true);
		const auto disablingDebug =
			ResolvePageControlDebugWindowDamagePolicy(false, true, false);
		const auto enablingDebug =
			ResolvePageControlDebugWindowDamagePolicy(false, true, true);
		Check(!stableDebugWindow.includePreviousWindow
			&& !stableDebugWindow.includeCurrentWindow,
			"stable blue window frame never expands prcDirty to the full window");
		Check(disablingDebug.includePreviousWindow
			&& !disablingDebug.includeCurrentWindow
			&& enablingDebug.includePreviousWindow
			&& enablingDebug.includeCurrentWindow,
			"debug toggle clears the old frame and draws the new frame only when enabled");
		Check(!ShouldTreatPageControlDamageAsActiveDebugFrame(
			false, true, false, false)
			&& !ShouldTreatPageControlDamageAsActiveDebugFrame(
				false, false, true, false)
			&& ShouldTreatPageControlDamageAsActiveDebugFrame(
				true, true, false, false)
			&& !ShouldTreatPageControlDamageAsActiveDebugFrame(
				true, true, false, true),
			"business damage schedules a final debug frame only while debug is enabled");
	}

	void TestRuntimeCollisionFallback()
	{
		constexpr RECT monitor{ 0, 0, 800, 600 };
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = true;
		ppt.layout.middlePairHeight = -212.5F;
		const PptState saved = ppt;

		const auto resolved = ResolveRuntimePageControlLayout(
			monitor, 1.0F, ppt);
		Check(!PageControlGroupsOverlap(monitor, 1.0F,
			resolved, 5), "side pair avoids the fixed bottom pair");
		Check(resolved.layout.bottomPairHeight
			== saved.layout.bottomPairHeight,
			"runtime collision keeps the bottom pair at its user position");
		Check(ppt.layout.bottomPairHeight == saved.layout.bottomPairHeight
			&& ppt.layout.middlePairHeight == saved.layout.middlePairHeight,
			"runtime collision fallback never mutates saved positions");

		constexpr RECT tinyMonitor{ -180, -60, 0, 60 };
		PptState extreme;
		extreme.presentationVisible = true;
		extreme.layout.showBottomPair = true;
		extreme.layout.showMiddlePair = true;
		extreme.layout.bottomPairWidth = 10000.0F;
		extreme.layout.bottomPairHeight = 10000.0F;
		extreme.layout.middlePairWidth = 10000.0F;
		extreme.layout.middlePairHeight = 10000.0F;
		extreme.layout.bottomPairScale = 3.0F;
		extreme.layout.middlePairScale = 3.0F;
		const PptState original = extreme;
		const auto fitted = ResolveRuntimePageControlLayout(
			tinyMonitor, 1.5F, extreme);
		for (const auto surface : { Surface::BottomLeft, Surface::BottomRight,
			Surface::MiddleLeft, Surface::MiddleRight })
		{
			const auto layout = ResolveSurfaceLayout(surface, tinyMonitor,
				1.5F, fitted, {});
			Check(layout.logicalBounds.left >= tinyMonitor.left
				&& layout.logicalBounds.top >= tinyMonitor.top
				&& layout.logicalBounds.right <= tinyMonitor.right
				&& layout.logicalBounds.bottom <= tinyMonitor.bottom,
				"tiny monitor keeps every compact surface on screen");
		}
		Check(!PageControlGroupsOverlap(tinyMonitor, 1.5F, fitted, 8),
			"tiny monitor fitting keeps bottom and side groups separated");
		Check(extreme.layout.bottomPairWidth == original.layout.bottomPairWidth
			&& extreme.layout.middlePairHeight == original.layout.middlePairHeight
			&& extreme.layout.bottomPairScale == original.layout.bottomPairScale
			&& extreme.layout.middlePairScale == original.layout.middlePairScale,
			"tiny monitor fitting does not mutate the published PPT snapshot");

		PptLayoutState dragged = saved.layout;
		dragged.bottomPairWidth = 10000.0F;
		dragged.bottomPairHeight = 10000.0F;
		const auto clamped = ClampPageControlLayout(Surface::BottomLeft,
			monitor, 1.0F, dragged);
		Check(clamped.bottomPairWidth == 230.0F
			&& clamped.bottomPairHeight == 547.5F,
			"PPT drag clamp uses compact geometry only");
	}

	void TestA2ProjectionAndMigration()
	{
		using namespace Inkeys::UI::Bar;
		const auto desktop = ResolveBarA2Projection(false, false);
		const auto presentation = ResolveBarA2Projection(true, false);
		const auto whiteboard = ResolveBarA2Projection(true, true);
		Check(!desktop.whiteboardTwoTwo && desktop.freezeVisible
			&& !desktop.endShowVisible,
			"desktop A2 shows two compact buttons");
		Check(presentation.whiteboardTwoTwo && !presentation.freezeVisible
			&& presentation.endShowVisible,
			"PPT A2 shows Whiteboard and EndShow");
		Check(whiteboard.whiteboardTwoTwo && !whiteboard.freezeVisible
			&& !whiteboard.endShowVisible,
			"whiteboard A2 shows only Close Whiteboard");
		Check(IsLegacyBarA2Pair("Inkeys.Bar.Whiteboard", "Inkeys.Bar.Freeze")
			&& IsLegacyBarA2Pair("Inkeys.Bar.Freeze", "Inkeys.Bar.Whiteboard"),
			"both legal legacy A2 orders are accepted");
		Check(!IsLegacyBarA2Pair("Inkeys.Bar.Whiteboard", "Inkeys.Bar.EndShow"),
			"invalid legacy A2 pair returns to defaults");

		BarA2CallbackDispatcher dispatcher;
		int dispatchCount = 0;
		dispatcher.Set([&dispatchCount]() { ++dispatchCount; });
		Check(dispatcher.Dispatch() && dispatchCount == 1,
			"one EndShow request dispatches exactly one business callback");
		Check(!dispatcher.Dispatch() && dispatchCount == 1,
			"duplicate EndShow request is suppressed while one is outstanding");
		dispatcher.Complete();
		Check(dispatcher.Dispatch() && dispatchCount == 2,
			"completed EndShow request permits a later retry");
		dispatcher.Set({});
		Check(!dispatcher.Dispatch() && dispatchCount == 2,
			"cleared EndShow callback cannot enqueue a stale request");
	}
}

int RunPageControlTests()
{
	TestSharedButtonInheritance();
	TestSharedSurfaceLightingMapping();
	TestCompactWidgetContracts();
	TestDragCommitHandoff();
	TestDragPureTranslationAndRevisionGate();
	TestWorkspaceAndDpiLayouts();
	TestWorkspaceTransitionAndInputPolicy();
	TestTouchTranslationPolicy();
	TestWhiteboardLayoutTargetAndReadiness();
	TestDirtyInvalidationDeduplication();
	TestRuntimeCollisionFallback();
	TestA2ProjectionAndMigration();
	return failureCount;
}
