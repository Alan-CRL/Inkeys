#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

#include "../Inkeys/Inkeys/UI/Bar/Bar.A2.h"
#include "../Inkeys/Inkeys/Input/UiPointerInput.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"

import Inkeys.UI.PageControl;
import Inkeys.UI.Bar.Metrics;
import Inkeys.UI.Bar.SurfaceLayout;
import Inkeys.UI.Ppt;

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

	LONG VisibleBodyBottom(const ResolvedSurfaceLayout& layout, double heightDip) noexcept
	{
		return layout.logicalBounds.top + static_cast<LONG>(std::ceil(
			(heightDip + BarButtonFrameThicknessDip / 2.0) * layout.scale));
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

	void TestNativeMouseAndDragSupplement()
	{
		namespace Pointer = Inkeys::Input::UiPointer;
		const auto NativeDown = [](std::uint64_t generation, DWORD time)
		{
			Pointer::Sample sample;
			sample.kind = Pointer::Kind::Down;
			sample.screen = { 100, 100 };
			sample.generation = generation;
			sample.messageTime = time;
			sample.tick = time;
			return sample;
		};
		// Page 低于阈值仍是点击，Raw 注册失败或超时都不能阻止原生 Up。
		for (const bool rawAvailable : { false, true })
		{
			Pointer::Testing::ResetProvider(rawAvailable);
			Pointer::Testing::SetClock(1000);
			Pointer::MouseChannel channel;
			Pointer::MouseGesture gesture;
			const auto down = NativeDown(1, 1000);
			Check(gesture.Begin(down), "PPT native press starts before optional raw claim");
			Check(channel.BeginMouse(down.screen, down.messageTime) == rawAvailable,
				"PPT optional drag claim reflects availability without owning the click");
			Pointer::Testing::SetClock(2001);
			Pointer::Testing::PollPendingMouse();
			Pointer::Sample packet;
			while (channel.TryPop(packet))
				Check(!gesture.AcceptRaw(packet),
					"PPT pending raw cancellation never completes a page click");
			auto up = down;
			up.kind = Pointer::Kind::Up;
			up.screen = { 101, 100 };
			up.messageTime = 2002;
			Check(!HasExceededPptDragThreshold(down.screen, up.screen, 8, 6)
				&& gesture.AcceptNative(up) && !gesture.Active(),
				"PPT page candidate completes native click despite unavailable raw");
		}
		// 三类拖动入口在没有 Raw Down 时，越过补充启动期限仍继续消费原生轨迹。
		for (const auto region : { PptPointerRegion::Background,
			PptPointerRegion::Page, PptPointerRegion::DragHandle })
		{
			Pointer::Testing::ResetProvider();
			Pointer::Testing::SetClock(10000);
			Pointer::MouseChannel channel;
			Pointer::MouseGesture gesture;
			Pointer::Session session;
			const auto down = NativeDown(5, 10000);
			Check(CanStartPptDrag(region) && gesture.Begin(down)
				&& session.Begin(down) == Pointer::BeginResult::Started
				&& channel.BeginMouse(down.screen, down.messageTime),
				"PPT native drag starts while optional Raw remains unbound");
			if (StartsPptDragImmediately(region)) (void)gesture.BeginDrag();
			auto move = down;
			move.kind = Pointer::Kind::Move;
			move.screen.x += 80;
			move.tick = move.messageTime = 10001;
			Check(gesture.AcceptNative(move) && session.Accept(gesture.Latest())
				&& HasExceededPptDragThreshold(down.screen, move.screen, 8, 6)
				&& gesture.BeginDrag(), "PPT enters actual drag before the one-second deadline");
			int cancelCount = 0;
			for (DWORD offset : { 999u, 1000u, 1500u, 5000u })
			{
				Pointer::Testing::SetClock(10000 + offset);
				Pointer::Testing::PollPendingMouse();
				Pointer::Sample packet;
				while (channel.TryPop(packet))
				{
					++cancelCount;
					Check(packet.kind == Pointer::Kind::Cancel && packet.generation == 0
						&& !gesture.AcceptRaw(packet) && gesture.Active() && session.Active(),
						"PPT unbound Raw timeout does not cancel actual native pair drag");
					channel.FinishMouse();
				}
				Check(cancelCount == (offset < 1000 ? 0 : 1),
					"PPT optional startup deadline expires once without terminating drag");
				move.screen.x += 10;
				move.tick = move.messageTime = 10000 + offset;
				Check(gesture.AcceptNative(move) && session.Accept(gesture.Latest())
					&& session.Active() && session.Latest().screen.x == move.screen.x,
					"PPT native pair movement stays effective for multiple seconds");
			}
			move.kind = Pointer::Kind::Up;
			move.tick = move.messageTime = 16000;
			Check(gesture.AcceptNative(move) && session.Accept(gesture.Latest())
				&& !session.Active() && !gesture.AcceptNative(move),
				"PPT native drag ends exactly once on the user's real release");
		}
		Pointer::Testing::ResetProvider();
		for (const auto region : { PptPointerRegion::Background,
			PptPointerRegion::Page, PptPointerRegion::DragHandle })
		for (const bool rawEndsFirst : { false, true })
		{
			Pointer::MouseGesture gesture;
			Pointer::Session session;
			const auto down = NativeDown(2, 3000);
			Check(CanStartPptDrag(region) && gesture.Begin(down)
				&& session.Begin(down) == Pointer::BeginResult::Started,
				"PPT native hit establishes candidate before any raw packet");
			if (StartsPptDragImmediately(region)) (void)gesture.BeginDrag();
			auto raw = down;
			raw.generation = 90;
			raw.device = 7;
			Check(!gesture.AcceptRaw(raw), "PPT raw Down cannot dispatch a second press");
			if (!StartsPptDragImmediately(region))
			{
				raw.kind = Pointer::Kind::Move;
				raw.screen = { 103, 100 };
				raw.messageTime = 3001;
				Check(gesture.AcceptRaw(raw) && session.Accept(gesture.Latest())
					&& !HasExceededPptDragThreshold(down.screen, raw.screen, 8, 6),
					"PPT candidate accepts supplemental movement below drag threshold");
			}
			raw.kind = Pointer::Kind::Move;
			raw.screen = { 180, 110 };
			raw.messageTime = 3002;
			Check(gesture.AcceptRaw(raw) && session.Accept(gesture.Latest())
				&& HasExceededPptDragThreshold(down.screen, raw.screen, 8, 6)
				&& gesture.BeginDrag(),
				"PPT raw movement can cross original threshold outside the window");
			raw.kind = Pointer::Kind::Up;
			raw.messageTime = 3003;
			auto nativeUp = down;
			nativeUp.kind = Pointer::Kind::Up;
			nativeUp.screen = raw.screen;
			nativeUp.messageTime = raw.messageTime;
			const bool ended = rawEndsFirst
				? gesture.AcceptRaw(raw) : gesture.AcceptNative(nativeUp);
			Check(ended && session.Accept(gesture.Latest()) && !session.Active(),
				"PPT first valid drag Up retires original native session");
			Check(!(rawEndsFirst ? gesture.AcceptNative(nativeUp) : gesture.AcceptRaw(raw)),
				"PPT alternate late Up cannot finish the same pair twice");
			const auto nextDown = NativeDown(3, 3010);
			Check(gesture.Begin(nextDown)
				&& session.Begin(nextDown) == Pointer::BeginResult::Started
				&& !gesture.AcceptNative(nativeUp) && !gesture.AcceptRaw(raw)
				&& session.Active(),
				"PPT old terminal cannot retire the next native press");
		}
		{
			Pointer::MouseGesture gesture;
			const auto down = NativeDown(4, 4000);
			Check(gesture.Begin(down), "PPT page tap starts on native Down");
			auto raw = down;
			raw.generation = 99;
			raw.device = 8;
			(void)gesture.AcceptRaw(raw);
			raw.kind = Pointer::Kind::Up;
			raw.messageTime = 4001;
			Check(!gesture.AcceptRaw(raw) && gesture.Active(),
				"PPT raw Up without a real drag never opens page preview");
			auto up = down;
			up.kind = Pointer::Kind::Up;
			up.messageTime = 4002;
			Check(gesture.AcceptNative(up), "PPT native Up still completes the page tap");
		}
		Pointer::Testing::ResetProvider();
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
		Check(ShouldAcceptPageControlClientHit(WorkspaceMode::WhiteboardExpanded, true, false, true)
			&& ShouldAcceptPageControlClientHit(WorkspaceMode::WhiteboardExpanded, true, false, false, true)
			&& !ShouldAcceptPageControlClientHit(WorkspaceMode::WhiteboardExpanded, false, false, true, true)
			&& !ShouldAcceptPageControlClientHit(WorkspaceMode::PptCompact, true, true, true),
			"locked whiteboard panel consumes clicks without accepting transparent corners or PPT input");
		constexpr POINT dragStart{ 100, 100 };
		Check(!HasExceededPptDragThreshold(
			dragStart, POINT{ 103, 102 }, 8, 6)
			&& HasExceededPptDragThreshold(
				dragStart, POINT{ 104, 102 }, 8, 6)
			&& HasExceededPptDragThreshold(
				dragStart, POINT{ 100, 103 }, 8, 6),
			"page press converts to drag only after the centered system threshold");
		Check(left[1].displaysText && left[3].displaysText
			&& left[1].invokesBusinessAction && left[3].invokesBusinessAction,
			"standard PPT arrows expose labels and retain page actions");
		Check(left[2].displaysText && left[2].invokesBusinessAction
			&& left[2].primaryTextBold && left[2].secondaryTextNormal
			&& left[2].immediateTextUpdate,
			"PPT page button opens preview with mixed page-number weights");
		Check(left[0].bounds.left == 5.0 && left[3].bounds.right == PptControlLongSideDip-PageControlGapDip
			&& right[1].bounds.left == 5.0 && right[0].bounds.right == PptControlLongSideDip-PageControlGapDip,
			"horizontal drag slot stays on the outer edge");
		Check(left[0].bounds.right == left[1].bounds.left
			&& right[3].bounds.right == right[0].bounds.left
			&& side[0].bounds.bottom == side[1].bounds.top,
			"drag divider lane touches the adjacent arrow without a third gap");
		Check(side[0].bounds.top == 5.0 && side[3].bounds.bottom == PptControlLongSideDip-PageControlGapDip,
			"side layout keeps the ten DIP drag slot above three standard buttons");
		Check(!BarPptCompactPaginationEnabled&&PptCompactLongSideDip==165.0&&PptCompactShortSideDip==42.5
			&&PptControlLongSideDip==240.0&&PptControlShortSideDip==80.0,
			"legacy geometry remains available while the production compact path is disabled");
		for(const auto surface:{Surface::BottomLeft,Surface::BottomRight,Surface::MiddleLeft,Surface::MiddleRight}){
			const auto buttons=ResolvePptWidgetContracts(surface);
			for(std::size_t id=1;id<buttons.size();++id)Check(buttons[id].bounds.right-buttons[id].bounds.left==BarButtonTwoSideDip
				&&buttons[id].bounds.bottom-buttons[id].bounds.top==BarButtonTwoSideDip,
				"every PPT direction and page button occupies the standard two by two square");
		}
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
		Check(ResolveDirectionIconAngle(Surface::MiddleLeft,
			WorkspaceMode::PptCompact, false, false) == 0.0
			&& ResolveDirectionIconAngle(Surface::MiddleLeft,
				WorkspaceMode::PptCompact, true, false) == 180.0
			&& ResolveDirectionIconAngle(Surface::MiddleRight,
				WorkspaceMode::PptCompact, false, false) == 0.0
			&& ResolveDirectionIconAngle(Surface::MiddleRight,
				WorkspaceMode::PptCompact, true, false) == 180.0,
			"both vertical PPT controls point Previous up and Next down");
		Check(ResolveDirectionIconAngle(Surface::BottomLeft,
			WorkspaceMode::PptCompact, false, false) == -90.0
			&& ResolveDirectionIconAngle(Surface::BottomRight,
				WorkspaceMode::PptCompact, true, false) == 90.0
			&& ResolveDirectionIconAngle(Surface::BottomLeft,
				WorkspaceMode::WhiteboardExpanded, false, false) == -90.0
			&& ResolveDirectionIconAngle(Surface::BottomRight,
				WorkspaceMode::WhiteboardExpanded, true, false) == 90.0
			&& ResolveDirectionIconAngle(Surface::BottomRight,
				WorkspaceMode::WhiteboardExpanded, true, true) == 0.0,
			"bottom PPT and Whiteboard direction angles remain unchanged");
	}

	void TestEndShowContentTargets()
	{
		const auto ready = Inkeys::UI::Ppt::ResolvePageStateForPublication(
			12, 12, 12, 12);
		const auto endPage = Inkeys::UI::Ppt::ResolvePageStateForPublication(
			-1, -1, -1, 12);
		const auto recoveryNotReady =
			Inkeys::UI::Ppt::ResolvePageStateForPublication(-1, -1, 12, 12);
		Check(IsPptEndPage(endPage.currentPage, endPage.totalPage)
			&& !IsPptEndPage(ready.currentPage, ready.totalPage)
			&& !IsPptEndPage(-1, -1)
			&& !IsPptEndPage(-1, 0),
			"PPT end page requires a valid total and a missing current page");
		Check(ResolvePptDirectionAction(false,
			ready.currentPage, ready.totalPage)
				== PptDirectionAction::PreviousPage
			&& ResolvePptDirectionAction(true,
				ready.currentPage, ready.totalPage)
				== PptDirectionAction::NextPage
			&& ResolvePptDirectionAction(true,
				endPage.currentPage, endPage.totalPage)
				== PptDirectionAction::EndShow
			&& ResolvePptDirectionAction(true, -1, -1)
				== PptDirectionAction::NextPage
			&& IsPptDirectionActionRepeatable(PptDirectionAction::PreviousPage)
			&& IsPptDirectionActionRepeatable(PptDirectionAction::NextPage)
			&& !IsPptDirectionActionRepeatable(PptDirectionAction::EndShow),
			"only the published PPT end page routes Next to EndShow");

		constexpr std::array surfaces{
			Surface::BottomLeft, Surface::BottomRight,
			Surface::MiddleLeft, Surface::MiddleRight,
		};
		for (const auto surface : surfaces)
		{
			const auto readyTarget = ResolveDirectionContentPolicy(
				WorkspaceMode::PptCompact, true, false,
				IsPptEndPage(ready.currentPage, ready.totalPage));
			const auto endTarget = ResolveDirectionContentPolicy(
				WorkspaceMode::PptCompact, true, false,
				IsPptEndPage(endPage.currentPage, endPage.totalPage));
			Check(readyTarget.icon == L"barMore"
				&& endTarget.icon == L"barEndShow" && endTarget.label.empty()
				&& ResolveDirectionIconAngle(surface,
					WorkspaceMode::PptCompact, true, false,
					IsPptEndPage(endPage.currentPage, endPage.totalPage)) == 0.0,
				"production end-page projection retargets every Next to EndShow");

			const bool vertical = surface == Surface::MiddleLeft
				|| surface == Surface::MiddleRight;
			const auto recoveredTarget = ResolveDirectionContentPolicy(
				WorkspaceMode::PptCompact, true, false,
				IsPptEndPage(recoveryNotReady.currentPage,
					recoveryNotReady.totalPage));
			const auto contracts = ResolvePptWidgetContracts(surface);
			Check(recoveredTarget.icon == L"barMore"
				&& ResolveDirectionIconAngle(surface,
					WorkspaceMode::PptCompact, true, false,
					IsPptEndPage(recoveryNotReady.currentPage,
						recoveryNotReady.totalPage))
					== (vertical ? 180.0 : 90.0)
				&& contracts[3].role == PptWidgetRole::Next
				&& contracts[3].invokesBusinessAction,
				"valid recovery restores arrows and the normal Next action");
		}
	}

	void TestNavigationWaitingLifecycle()
	{
		PptNavigationWaitingState waiting;
		Check(waiting.Publish(7, 11, PptNavigationDirection::Next)
			&& waiting.current.waiting && waiting.current.session == 7
			&& waiting.current.epoch == 11,
			"successful native enqueue publishes a session and epoch wait value");
		Check(!waiting.Publish(7, 11, PptNavigationDirection::Previous)
			&& waiting.current.direction == PptNavigationDirection::Next,
			"duplicate click keeps the original direction and epoch");
		PptState frame;
		frame.layout.session = 7;
		frame.currentPage = 2;
		frame.totalPage = 3;
		frame.navigationWaiting = waiting.current;
		Check(IsPptNavigationWaiting(frame, true) && !IsPptNavigationWaiting(frame, false)
			&& frame.currentPage == 2 && frame.totalPage == 3,
			"waiting direction preserves the real old page and does not fabricate navigation");
		frame.targetRevision = 44;
		frame.pageAckEpoch = 3;
		auto readyFrame = frame;
		readyFrame.navigationWaiting = {};
		PptPageCommitState commit;
		commit.Publish(readyFrame, 0x3);
		Check(commit.Commit(readyFrame, 0x3) && commit.acknowledged,
			"page ready is acknowledged before independent command feedback changes");
		commit.Publish(frame, 0x3);
		Check(commit.acknowledged && commit.committed == 0x3 && !commit.Commit(frame, 0x3),
			"navigation waiting neither resets nor newly completes Draw3/PageControl ready");
		auto changed = frame;
		++changed.navigationWaiting.epoch;
		Check(!ArePptStatesEquivalent(frame, changed),
			"waiting publication participates in PageControl visual invalidation");
		Check(!waiting.Complete(7, 10) && !waiting.Complete(8, 11)
			&& waiting.current.waiting,
			"stale epoch and wrong session cannot clear an active wait");
		Check(waiting.Complete(7, 11) && !waiting.current.waiting,
			"command completion clears waiting even when the page did not change");
		frame.navigationWaiting = waiting.current;
		Check(!IsPptNavigationWaiting(frame, true),
			"completed unchanged page is immediately retryable without a permanent marker");

		Check(waiting.Publish(7, 12, PptNavigationDirection::Previous)
			&& waiting.Publish(8, 1, PptNavigationDirection::Next)
			&& waiting.current.session == 8,
			"a new show replaces the previous session wait even with a restarted epoch");
		Check(!waiting.Complete(7, 12) && waiting.current.session == 8
			&& waiting.current.waiting,
			"late completion from the old show cannot clear the new show");
		Check(!waiting.Clear(7) && waiting.current.waiting,
			"wrong-session exit leaves the current session untouched");
		Check(waiting.Clear(8) && !waiting.current.waiting,
			"session exit clears its navigation feedback");
		Check(waiting.Publish(9, 2, PptNavigationDirection::Next)
			&& waiting.Clear() && !waiting.current.waiting,
			"shutdown clears any remaining navigation feedback");
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

		// 这里只验证 mailbox，不把它当作呈现锁竞争或真实几何提交的证据。
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

	void TestVersionedDragPublication()
	{
		for (const bool remember : { false, true })
		{
			PptState published;
			published.layout.session = 1;
			published.layout.epoch = 2;
			published.publicationRevision = 1;
			published.layout.rememberPosition = remember;
			const auto stale = published;
			PptDragCommitTracker bottom;
			BeginPptDragTracking(bottom, published.layout);
			auto moved = published.layout;
			moved.bottomPairHeight = 200;
			moved.pairVersions[0] = 10;
			(void)PublishPptDragCandidate(bottom, moved, 10);
			const auto released = ReleasePptDragTracking(bottom, true);
			Check(released.persist && bottom.ownsLayout,
				"memory-off release still owns the runtime handoff");
			Check(MarkPptDragSurfaceCommitted(bottom, 10, 3), "both surfaces commit memory-off drag");
			published.layout = bottom.committedLayout;
			Check(CompletePptDragPersistence(bottom, 10) && !bottom.ownsLayout,
				"runtime handoff releases ownership without a disk notification");
			auto incoming = stale;
			incoming.publicationRevision = 2;
			incoming.currentPage = 2;
			Check(MergePptPublication(published, incoming) && incoming.currentPage == 2
				&& incoming.layout.bottomPairHeight == 200 && incoming.layout.pairVersions[0] == 10,
				"late captured snapshot updates page while preserving a released drag");
			published = incoming;
			published.layout.middlePairHeight = 80;
			published.layout.pairVersions[1] = 11;
			incoming = stale;
			incoming.publicationRevision = 3;
			incoming.layout.bottomPairScale = 2.5F;
			Check(MergePptPublication(published, incoming)
				&& incoming.layout.bottomPairHeight == 200 && incoming.layout.middlePairHeight == 80
				&& incoming.layout.bottomPairScale == 2.5F,
				"settings merge preserves both interleaved pair positions while changing scale");
			published = incoming;
			incoming = stale;
			Check(!MergePptPublication(published, incoming), "late old publication cannot roll back page or visibility");
			incoming = published;
			++incoming.layout.epoch;
			++incoming.publicationRevision;
			incoming.layout.pairVersions = {};
			incoming.layout.bottomPairHeight = 0;
			Check(MergePptPublication(published, incoming) && incoming.layout.bottomPairHeight == 0,
				"explicit reset epoch replaces old positions");
			Check(!MergePptPublication(incoming, published), "pre-reset snapshot cannot reactivate old positions");
			PptDragCommitTracker untouched;
			BeginPptDragTracking(untouched, incoming.layout);
			Check(!ReleasePptDragTracking(untouched, true).persist && !untouched.ownsLayout,
				"drag-handle tap without a movement never leaves permanent ownership");
		}
	}

	void TestInterleavedPairFrameCommit()
	{
		PptState frame;
		frame.layout.session = 4;
		frame.layout.epoch = 8;
		PptDragCommitTracker bottom, middle;
		BeginPptDragTracking(bottom, frame.layout);
		BeginPptDragTracking(middle, frame.layout);
		frame.layout.pairVersions[0] = 10;
		frame.layout.bottomPairHeight = 200;
		(void)PublishPptDragCandidate(bottom, frame.layout, 10);
		frame.layout.pairVersions[1] = 11;
		frame.layout.middlePairHeight = 100;
		(void)PublishPptDragCandidate(middle, frame.layout, 11);
		frame.publicationRevision = 99;
		Check(!MarkPptDragFrameCommitted(bottom, frame, 0)
			&& MarkPptDragFrameCommitted(bottom, frame, 1),
			"bottom commit uses its candidate revision after a newer middle publication");
		Check(!MarkPptDragFrameCommitted(middle, frame, 2)
			&& MarkPptDragFrameCommitted(middle, frame, 3)
			&& !bottom.pending && !middle.pending,
			"interleaved pair frames both converge without sharing candidate revisions");
		auto newSession = frame;
		newSession.layout.session = 5;
		newSession.layout.epoch = 1;
		newSession.publicationRevision = 1;
		Check(MergePptPublication(frame, newSession),
			"new native session accepts a restarted facade with smaller local revisions");
	}

	void TestCanvasRecoveryPresentationCommit()
	{
		PptState frame;
		frame.layout.session = 7; frame.targetRevision = 9; frame.currentPage = 1; frame.totalPage = 2;
		frame.pageAckEpoch = 1;
		frame.recovery.kind = decltype(frame.recovery.kind)::Conflict;
		frame.recovery.session = 7; frame.recovery.targetRevision = 9;
		frame.recovery.revision = 11; frame.recovery.retryAvailable = true;
		PptPageCommitState commit;
		commit.Publish(frame, 1);
		Check(!commit.Commit(frame, 1), "fault labels cannot emit a normal ink UI-ready ack");
		auto ready = frame; ready.recovery = {};
		commit.Publish(ready, 1);
		Check(!commit.Commit(frame, 1) && commit.Commit(ready, 1) && !commit.Commit(ready, 1),
			"clearing fault requires one real normal frame and rejects stale fault frame");
		Check(!ArePptStatesEquivalent(frame, ready), "recovery publication invalidates page rendering equality");
	}

	void TestPagePresentationCommit()
	{
		PptState page;
		page.layout.session = 7;
		page.targetRevision = 100;
		page.currentPage = 2;
		page.totalPage = 9;
		PptPageCommitState gate;
		gate.Publish(page, 15);
		Check(!gate.Commit(page, 3), "bottom pair alone cannot acknowledge visible middle controls");
		auto late = page;
		--late.targetRevision;
		Check(!gate.Commit(late, 12), "late frame cannot satisfy current page presentation");
		Check(gate.Commit(page, 12) && !gate.Commit(page, 15), "exact page is acknowledged once after all visible surfaces");
		++page.targetRevision;
		++page.currentPage;
		gate.Publish(page, 15);
		Check(!gate.Commit(page, 1), "next target starts a new commit transaction");
		gate.Publish(page, 1);
		Check(gate.Commit(page, 0), "hiding uncommitted surfaces releases their presentation requirement");
		++page.targetRevision;
		gate.Publish(page, 0);
		Check(gate.Commit(page, 0), "all hidden controls acknowledge without waiting for nonexistent frames");
		++page.layout.session;
		gate.Publish(page, 3);
		Check(!gate.Commit(late, 3) && gate.Commit(page, 3), "new session rejects old success callbacks");
		PptPageCommitState endGate;
		auto end = page;
		end.currentPage = -1;
		++end.targetRevision;
		endGate.Publish(end, 3);
		Check(!endGate.Commit(page, 3) && endGate.Commit(end, 3),
			"typed end target commits its minus-one display only after the visible surface");
		++end.targetRevision;
		endGate.Publish(end, 0);
		Check(endGate.Commit(end, 0),
			"hidden end controls cannot hold the matching target indefinitely");

		PptPageCommitState recovery;
		auto beforePause = page;
		beforePause.pageAckEpoch = 10;
		recovery.Publish(beforePause, 3);
		Check(recovery.Commit(beforePause, 3), "initial page epoch commits normally");
		auto pending = beforePause;
		pending.targetRevision = 0;
		++pending.pageAckEpoch;
		recovery.Publish(pending, 3);
		Check(!recovery.Commit(beforePause, 3), "pause rejects previously captured surface commits");
		auto resumed = beforePause;
		resumed.pageAckEpoch += 2;
		recovery.Publish(resumed, 3);
		Check(!ArePptStatesEquivalent(beforePause, resumed)
			&& !recovery.Commit(beforePause, 3)
			&& !recovery.Commit(resumed, 1) && recovery.Commit(resumed, 2),
			"same numbers and target require new epoch surfaces before acknowledging recovery");
		recovery.Publish(resumed, 3);
		Check(!recovery.Commit(resumed, 3), "same publication epoch remains idempotent after acknowledgment");
	}

	void TestHideAndPageCommitRace()
	{
		PptState oldPage;
		oldPage.layout.session = 10;
		oldPage.targetRevision = 20;
		oldPage.currentPage = 1;
		oldPage.totalPage = 3;
		PptPageCommitState gate;
		gate.Publish(oldPage, 3);
		gate.BeginSurface(3);
		gate.FinishSurface(3, true);
		Check(gate.Commit(oldPage, 3), "initial visible page is committed");
		auto next = oldPage;
		++next.targetRevision;
		++next.currentPage;
		gate.Publish(next, 0);
		Check(!gate.Commit(next, 0) && gate.required == 3,
			"target hide cannot acknowledge new page while old numbers are still visible");
		gate.BeginSurface(1);
		gate.FinishSurface(1, true);
		Check(!gate.Commit(next, 1), "one fading surface with new numbers still waits for its visible peer");
		gate.FinishSurface(2, false);
		gate.Publish(next, 0);
		Check(gate.Commit(next, 0), "new number on one peer plus committed hide on the other completes handoff");

		PptPageCommitState inFlight;
		inFlight.Publish(oldPage, 3);
		inFlight.BeginSurface(1);
		inFlight.Publish(next, 0);
		Check(!inFlight.Commit(next, 0), "hidden fast path waits for an older frame already in presentation");
		inFlight.FinishSurface(1, true);
		inFlight.Publish(next, 0);
		Check(!inFlight.Commit(oldPage, 1) && !inFlight.Commit(next, 0),
			"late successful old frame remains an actual visibility requirement without accepting stale digits");
		inFlight.FinishSurface(1, false);
		inFlight.Publish(next, 0);
		Check(inFlight.Commit(next, 0), "successful hide releases the last older in-flight presentation");
	}

	void TestEffectiveScaleAndResources()
	{
		constexpr RECT monitor{ 0, 0, 7680, 4320 };
		PptState ppt;
		ppt.presentationActive = true;
		ppt.presentationVisible = true;
		ppt.layout.showMiddlePair = true;
		for (const float dpi : { 1.0F, 1.5F, 2.0F, 3.0F, 4.0F })
			for (const float user : { 0.5F, 1.0F, 2.5F, 3.0F })
			{
				ppt.layout.bottomPairScale = ppt.layout.middlePairScale = user;
				const auto fitted = ResolveRuntimePageControlLayout(monitor, dpi, ppt);
				for (const auto surface : { Surface::BottomLeft, Surface::BottomRight,
					Surface::MiddleLeft, Surface::MiddleRight })
				{
					const auto layout = ResolveSurfaceLayout(surface, monitor, dpi, fitted, {});
					Check(layout.scale == dpi * user && NormalizeScale(layout.scale) == layout.scale,
						"resolved pixel scale survives Scene-bound and drag translation normalization");
					Check(!ShouldApplyPageControlSceneBounds(true, layout.logicalBounds,
						layout.scale, layout.logicalBounds, layout.scale),
						"effective scale above four does not cause a perpetually dirty frame");
				}
			}
		const auto oversizedDpi = ResolveRuntimePageControlLayout(monitor, 6.0F, ppt);
		const auto oversizedLayout = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 6.0F, oversizedDpi, {});
		Check(oversizedLayout.scale == 12.0F && NormalizeScale(oversizedLayout.scale) == 12.0F,
			"out-of-range DPI is normalized once consistently through fitting and final layout");
		Check(NormalizeRuntimeControlScale(0.2F) == 0.2F
			&& NormalizeDpiScale(0.2F) == 0.5F && NormalizeScale(5.0F) == 5.0F,
			"DPI input, fitted user scale and effective scale have distinct contracts");
		const auto tiny = ResolveRuntimePageControlLayout({ 0, 0, 100, 80 }, 4, ppt, 10, 64);
		const auto tinyLayout = ResolveSurfaceLayout(Surface::BottomLeft, { 0, 0, 100, 80 }, 4, tiny, {});
		Check(tinyLayout.scale < 0.5F && NormalizeScale(tinyLayout.scale) == tinyLayout.scale
			&& Width(tinyLayout.logicalBounds) + 2 * static_cast<LONG>(std::ceil(10 * tinyLayout.scale)) <= 64,
			"tiny screen and bitmap limits fit complete presentation below half scale");
		WhiteboardState whiteboard;
		whiteboard.expandedLayoutTarget = whiteboard.active = true;
		const auto expanded = ResolveSurfaceLayout(Surface::BottomLeft, monitor, 2, ppt, whiteboard);
		const auto expandedRight = ResolveSurfaceLayout(Surface::BottomRight, monitor, 2, ppt, whiteboard);
		Check(expanded.scale == 2 && Width(expanded.logicalBounds) == 610
			&& Height(expanded.logicalBounds) == 160
			&& expanded.logicalBounds.left == monitor.left + 10
			&& VisibleBodyBottom(expanded, WhiteboardHeightDip) == monitor.bottom - 10
			&& expandedRight.scale == 2 && Width(expandedRight.logicalBounds) == 460
			&& Height(expandedRight.logicalBounds) == 160
			&& expandedRight.logicalBounds.right == monitor.right - 10
			&& VisibleBodyBottom(expandedRight, WhiteboardHeightDip) == monitor.bottom - 10,
			"whiteboard uses independent 305/230 DIP widths and fixed bottom corners without inheriting PPT scale or position");
	}

	void TestTaskbarUiScale()
	{
		constexpr RECT monitor{ -3840, 0, 3840, 4320 };
		PptState ppt;
		ppt.presentationActive = true;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = ppt.layout.showMiddlePair = true;
		ppt.layout.bottomPairScale = 1.5F;
		ppt.layout.middlePairScale = 2.0F;
		for (const float dpi : { 1.0F, 1.5F, 2.0F })
			for (const float base : { 0.3F, 0.6F, 0.9F, 1.2F })
				for (const float user : { 0.5F, 1.0F, 1.5F, 2.0F })
				{
					ppt.uiScale = base * user;
					const auto fitted = ResolveRuntimePageControlLayout(monitor, dpi, ppt);
					for (const auto surface : { Surface::BottomLeft, Surface::BottomRight,
						Surface::MiddleLeft, Surface::MiddleRight })
					{
						const auto layout = ResolveSurfaceLayout(surface, monitor, dpi, fitted, {});
						const bool middle = surface == Surface::MiddleLeft || surface == Surface::MiddleRight;
						Check(std::abs(layout.scale - base * user * (middle ? 2.0F : 1.5F)) < 1e-5F,
							"all four PPT surfaces multiply UI base, global zoom and local zoom exactly once");
						if (!middle) Check(VisibleBodyBottom(layout, PptControlShortSideDip) == monitor.bottom - static_cast<LONG>(std::lround(5 * dpi)),
							"PPT bottom inset stays five screen DIP across global and local UI scales");
					}
					WhiteboardState whiteboard;
					whiteboard.active = whiteboard.expandedLayoutTarget = true;
					const auto expanded=ResolveSurfaceLayout(Surface::BottomLeft,monitor,dpi,fitted,whiteboard);
					Check(VisibleBodyBottom(expanded, WhiteboardHeightDip) == monitor.bottom - static_cast<LONG>(std::lround(5 * dpi)),
						"whiteboard fill and stroke retain five screen DIP instead of five UI-scaled DIP");
					auto defaultPpt = fitted;defaultPpt.layout.bottomPairScale = 1.0F;
					const auto defaultLayout = ResolveSurfaceLayout(Surface::BottomLeft, monitor, dpi, defaultPpt, {});
					Check(defaultLayout.logicalBounds.top == expanded.logicalBounds.top
						&& defaultLayout.logicalBounds.bottom == expanded.logicalBounds.bottom,
						"default PPT and whiteboard keep the same body Y anchor at every global scale");
					Check(std::abs(expanded.scale-base*user)<1e-5F,
						"whiteboard follows the global UI base and zoom without PPT local zoom or a second DPI multiplier");
					PptState noShow;noShow.uiScale=base*user;noShow.layout.bottomPairScale=3;
					noShow.layout.bottomPairWidth=noShow.layout.bottomPairHeight=300;
					const auto standalone=ResolveSurfaceLayout(Surface::BottomLeft,monitor,dpi,noShow,whiteboard);
					Check(standalone.scale==expanded.scale&&EqualRect(&standalone.logicalBounds,&expanded.logicalBounds),
						"standalone whiteboard uses the same global scale and corner anchor without an active PPT session");
					auto candidate = fitted.layout;
					candidate.middlePairHeight = 21.0F * dpi;
					Check(!ResolvePptDragCandidate(Surface::MiddleLeft, monitor, dpi, fitted,
						candidate, fitted.layout, 20.0).centerSnapped,
						"middle snap threshold remains screen DIP regardless of taskbar/global UI scale");
				}
		ppt.uiScale = 0.3F;
		Check(Inkeys::UI::Bar::ResolveBarBottomDockInteractionZoom(192, 0.5, 0.3) == 0.15,
			"Bar interaction uses taskbar base without DPI double multiplication or half-scale clamp");
		const auto tiny = ResolveRuntimePageControlLayout({ 0, 0, 180, 100 }, 2, ppt, 10, 64);
		Check(tiny.layout.bottomPairScale <= ppt.layout.bottomPairScale
			&& ppt.layout.bottomPairScale == 1.5F,
			"runtime fit changes only frame local multipliers and preserves saved preferences");
	}

	void TestIndependentSurfaceFitRegression()
	{
		constexpr RECT monitor{ 0, 0, 7680, 4320 };
		PptState ppt;
		ppt.presentationActive = true;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.bottomPairScale = 3.0F;
		const auto left = ResolveRuntimePageControlLayout(
			monitor, 2.5F, ppt, 10.0F, 16384);
		const auto right = ResolveRuntimePageControlLayout(
			monitor, 2.5F, ppt, 35.0F, 512);
		Check(left.layout.bottomPairScale != right.layout.bottomPairScale,
			"regression setup distinguishes old per-surface fit inputs");
		const std::array<PageControlSurfaceBudget, 4> budgets{
			PageControlSurfaceBudget{ 10.0F, 16384 },
			PageControlSurfaceBudget{ 35.0F, 512 },
			PageControlSurfaceBudget{ 10.0F, 16384 },
			PageControlSurfaceBudget{ 10.0F, 16384 },
		};
		const auto group = ResolvePageControlGroupBudget(budgets, 3);
		Check(group.presentationOutsetDip == 35.0F
			&& group.bitmapLimit == 512,
			"all visible peers use the same conservative resource budget");
		for (const float dpi : { 1.0F, 1.5F, 2.0F, 2.5F })
		{
			const auto fitted = ResolveRuntimePageControlLayout(monitor, dpi,
				ppt, group.presentationOutsetDip, group.bitmapLimit);
			const auto resolvedLeft = ResolveSurfaceLayout(
				Surface::BottomLeft, monitor, dpi, fitted, {});
			const auto resolvedRight = ResolveSurfaceLayout(
				Surface::BottomRight, monitor, dpi, fitted, {});
			Check(resolvedLeft.scale == resolvedRight.scale
				&& Width(resolvedLeft.logicalBounds) == Width(resolvedRight.logicalBounds)
				&& resolvedLeft.logicalBounds.left - monitor.left
					== monitor.right - resolvedRight.logicalBounds.right
				&& resolvedLeft.logicalBounds.left >= monitor.left
				&& resolvedRight.logicalBounds.right <= monitor.right,
				"shared fit keeps both large controls symmetric and on screen");
		}
		const auto bounded = ResolveStableBackingSize({ 800, 600 },
			{ 300, 150 }, { 260, 110 }, 30, 3.0, 3.0, 512);
		Check(bounded.fits && bounded.size.cx == 512
			&& bounded.size.cy == 512,
			"old backing capacity is reduced to the current bitmap limit");
		const auto impossible = ResolveStableBackingSize({ 800, 600 },
			{ 513, 150 }, { 260, 110 }, 30, 3.0, 3.0, 512);
		Check(!impossible.fits,
			"a presentation beyond the resource limit is not silently clipped");
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

	void TestMiddlePairCenterSnap()
	{
		using Inkeys::UI::Bar::BarBottomDockThresholdDip;
		constexpr std::array monitors{
			RECT{ 0, 0, 3840, 2160 },
			RECT{ -2200, -1300, 1641, 861 },
		};
		for (const auto& monitor : monitors)
		for (const float dpi : { 1.0F, 1.5F, 2.0F })
		for (const float controlScale : { 0.5F, 1.0F, 2.5F, 3.0F })
		for (const auto surface : { Surface::MiddleLeft, Surface::MiddleRight })
		for (const float sign : { -1.0F, 1.0F })
		for (const float distanceDip : { 19.9F, 20.0F, 20.1F })
		for (const float edgeDistanceDip : { 19.9F, 20.0F, 20.1F, 120.0F })
		{
			const int failuresBefore = failureCount;
			PptState ppt;
			ppt.presentationActive = true;
			ppt.presentationVisible = true;
			ppt.layout.showBottomPair = false;
			ppt.layout.showMiddlePair = true;
			ppt.layout.middlePairScale = controlScale;
			ppt.layout.middlePairWidth = 20.0F;
			PptLayoutState candidate = ppt.layout;
			candidate.middlePairWidth = edgeDistanceDip * dpi;
			candidate.middlePairHeight = sign * distanceDip * dpi;
			const auto resolved = ResolvePptDragCandidate(surface, monitor,
				dpi, ppt, candidate, ppt.layout, BarBottomDockThresholdDip);
			const bool inside = distanceDip <= 20.0F && edgeDistanceDip <= 20.0F;
			Check(resolved.centerSnapped == inside && !resolved.centerBlocked
				&& !resolved.collisionRejected && !resolved.clamped
				&& resolved.layout.middlePairHeight
					== (inside ? 0.0F : candidate.middlePairHeight)
				&& resolved.layout.middlePairWidth == candidate.middlePairWidth
				&& resolved.layout.middlePairScale == controlScale,
				"middle snap requires both inclusive 20 screen DIP bands independent of control scale");
			PptState projected = ppt;
			projected.layout = resolved.layout;
			const auto left = ResolveSurfaceLayout(Surface::MiddleLeft,
				monitor, dpi, projected, {});
			const auto right = ResolveSurfaceLayout(Surface::MiddleRight,
				monitor, dpi, projected, {});
			Check(left.logicalBounds.top == right.logicalBounds.top
				&& left.logicalBounds.bottom == right.logicalBounds.bottom
				&& left.logicalBounds.left - monitor.left
					== monitor.right - right.logicalBounds.right,
				"snapping keeps both side controls symmetric and at the same height");
			const auto scaledGap = static_cast<LONG>(std::lround(
				PageControlGapDip * left.scale));
			const auto extraDistance = static_cast<LONG>(std::lround(
				candidate.middlePairWidth));
			Check(left.logicalBounds.left - monitor.left - scaledGap == extraDistance
				&& monitor.right - right.logicalBounds.right - scaledGap == extraDistance,
				"edge eligibility measures extra distance after the existing scaled gap");
			if (inside)
			{
				// 主体中心允许整数除法带来的半像素误差，不补边缘或阴影偏移。
				Check(std::abs(static_cast<double>(left.logicalBounds.top)
					+ left.logicalBounds.bottom - monitor.top - monitor.bottom) <= 1.0,
					"snapped body center follows the selected monitor with odd pixel rounding");
			}
			if (failureCount != failuresBefore)
				std::cerr << "  snap case origin=" << monitor.left << ',' << monitor.top
					<< " dpi=" << dpi << " scale=" << controlScale
					<< " surface=" << static_cast<int>(surface)
					<< " distanceDip=" << sign * distanceDip
					<< " edgeDistanceDip=" << edgeDistanceDip << '\n';
		}

		// 先裁剪再尝试吸附；矮屏能把远候选裁剪进中轴吸附带。
		constexpr RECT shortMonitor{ -600, -100, 600, 100 };
		PptState ppt;
		ppt.presentationActive = true;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = false;
		ppt.layout.showMiddlePair = true;
		auto candidate = ppt.layout;
		candidate.middlePairHeight = 10000.0F;
		const auto clamped = ResolvePptDragCandidate(Surface::MiddleLeft,
			shortMonitor, 1.0F, ppt, candidate, ppt.layout,
			BarBottomDockThresholdDip);
		Check(clamped.clamped && clamped.centerSnapped
			&& clamped.layout.middlePairHeight == 0.0F,
			"middle candidate is clamped before the center snap decision");

		// 拖动起点的旧倍率不能限制本帧已经重新拟合的可移动范围。
		ppt.layout.middlePairScale = 0.5F;
		candidate = ppt.layout;
		candidate.middlePairScale = 3.0F;
		candidate.middlePairHeight = 10000.0F;
		const auto currentScale = ResolvePptDragCandidate(Surface::MiddleLeft,
			RECT{ 0, 0, 1920, 1080 }, 1.0F, ppt, candidate, ppt.layout,
			BarBottomDockThresholdDip);
		Check(currentScale.clamped && !currentScale.centerSnapped
			&& currentScale.layout.middlePairHeight == 477.5F
			&& currentScale.layout.middlePairScale == 0.5F,
			"candidate clamp uses the current fitted scale instead of the old drag-start scale");
		candidate = ppt.layout;
		candidate.bottomPairHeight = 19.0F;
		const auto bottom = ResolvePptDragCandidate(Surface::BottomRight,
			RECT{ 0, 0, 1920, 1080 }, 1.0F, ppt, candidate, ppt.layout,
			BarBottomDockThresholdDip);
		Check(!bottom.centerSnapped && !bottom.centerBlocked
			&& bottom.layout.bottomPairHeight == 19.0F,
			"middle center snapping does not alter bottom pair drag offsets");
	}

	void TestMiddleSnapCollisionFallback()
	{
		using Inkeys::UI::Bar::BarBottomDockThresholdDip;
		constexpr RECT monitor{ 0, 0, 1920, 1080 };
		for (const float sign : { -1.0F, 1.0F })
		for (const auto surface : { Surface::MiddleLeft, Surface::MiddleRight })
		{
			PptState ppt;
			ppt.presentationActive = true;
			ppt.presentationVisible = true;
			ppt.layout.showBottomPair = true;
			ppt.layout.showMiddlePair = true;
			ppt.layout.middlePairScale = 0.5F;
			ppt.layout.middlePairWidth = 20.0F;
			ppt.layout.middlePairHeight = sign * 150.0F;
			auto centerFrame = ppt;
			centerFrame.layout.middlePairHeight = 0.0F;
			const auto centerBounds = ResolveSurfaceLayout(surface, monitor, 1.0F, centerFrame, {}).logicalBounds;
			const auto bottomBounds = ResolveSurfaceLayout(Surface::BottomLeft, monitor, 1.0F, ppt, {}).logicalBounds;
			// 障碍与当前主体中心重叠 5px，19px 自由候选仍须保留组间隙，不依赖旧 165/42.5 尺寸。
			ppt.layout.bottomPairHeight = static_cast<float>(sign > 0.0F
				? bottomBounds.top - centerBounds.bottom + PageControlGapDip
				: bottomBounds.bottom - centerBounds.top - PageControlGapDip);
			auto candidate = ppt.layout;
			candidate.middlePairWidth = 19.0F;
			candidate.middlePairHeight = sign * 19.0F;
			PptState free = ppt;
			free.layout = candidate;
			PptState centered = free;
			centered.layout.middlePairHeight = 0.0F;
			Check(!PageControlGroupsOverlap(monitor, 1.0F, free, 5)
				&& PageControlGroupsOverlap(monitor, 1.0F, centered, 5),
				"collision fixture has a feasible free candidate and an obstructed center");
			const auto resolved = ResolvePptDragCandidate(surface, monitor,
				1.0F, ppt, candidate, ppt.layout, BarBottomDockThresholdDip);
			Check(resolved.centerBlocked && !resolved.centerSnapped
				&& !resolved.collisionRejected
				&& resolved.layout.middlePairWidth == candidate.middlePairWidth
				&& resolved.layout.middlePairHeight == candidate.middlePairHeight
				&& resolved.layout.bottomPairHeight == ppt.layout.bottomPairHeight,
				"blocked center retains a feasible free movement without pushing the bottom pair");
		}

		PptState ppt;
		ppt.presentationActive = true;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = true;
		ppt.layout.middlePairScale = 0.5F;
		ppt.layout.middlePairWidth = 20.0F;
		ppt.layout.middlePairHeight = 150.0F;
		auto centerFrame = ppt;
		centerFrame.layout.middlePairHeight = 0.0F;
		const auto centerBounds = ResolveSurfaceLayout(Surface::MiddleLeft, monitor, 1.0F, centerFrame, {}).logicalBounds;
		const auto bottomBounds = ResolveSurfaceLayout(Surface::BottomLeft, monitor, 1.0F, ppt, {}).logicalBounds;
		ppt.layout.bottomPairHeight = static_cast<float>(bottomBounds.top - centerBounds.bottom + PageControlGapDip);
		auto candidate = ppt.layout;
		candidate.middlePairWidth = 19.0F;
		candidate.middlePairHeight = -80.0F;
		const auto rejected = ResolvePptDragCandidate(Surface::MiddleLeft,
			monitor, 1.0F, ppt, candidate, ppt.layout,
			BarBottomDockThresholdDip);
		Check(rejected.collisionRejected && !rejected.centerSnapped
			&& rejected.layout.middlePairWidth == ppt.layout.middlePairWidth
			&& rejected.layout.middlePairHeight == ppt.layout.middlePairHeight,
			"a colliding diagonal candidate rolls back the whole moved pair");
		candidate.middlePairHeight = 120.0F;
		const auto recovered = ResolvePptDragCandidate(Surface::MiddleLeft,
			monitor, 1.0F, ppt, candidate, rejected.layout,
			BarBottomDockThresholdDip);
		Check(!recovered.collisionRejected
			&& recovered.layout.middlePairWidth == candidate.middlePairWidth
			&& recovered.layout.middlePairHeight == candidate.middlePairHeight,
			"a later feasible candidate still moves after a collision rejection");

		// 候选可能来自旧拖动起点，另一组的新位置与身份必须采用当前发布。
		candidate.middlePairHeight = 19.0F;
		candidate.session = 1;
		candidate.epoch = 1;
		candidate.pairVersions = { 1, 2 };
		ppt.layout.session = 7;
		ppt.layout.epoch = 5;
		ppt.layout.pairVersions = { 21, 22 };
		// 新发布把障碍放到中心上方：只阻挡旧采样，自由中心仍保持两倍标准间隙。
		ppt.layout.bottomPairHeight = static_cast<float>(bottomBounds.bottom - centerBounds.top + PageControlGapDip * 2);
		PptState obstructedFree = ppt;
		obstructedFree.layout.middlePairWidth = candidate.middlePairWidth;
		obstructedFree.layout.middlePairHeight = candidate.middlePairHeight;
		auto freeCenter = obstructedFree;
		freeCenter.layout.middlePairHeight = 0.0F;
		Check(PageControlGroupsOverlap(monitor, 1.0F, obstructedFree, 5)
			&& !PageControlGroupsOverlap(monitor, 1.0F, freeCenter, 5),
			"new bottom publication obstructs the raw candidate but leaves its center feasible");
		const auto current = ResolvePptDragCandidate(Surface::MiddleLeft,
			monitor, 1.0F, ppt, candidate, rejected.layout,
			BarBottomDockThresholdDip);
		Check(current.centerSnapped && !current.collisionRejected
			&& current.layout.middlePairHeight == 0.0F
			&& current.layout.bottomPairHeight == ppt.layout.bottomPairHeight
			&& current.layout.session == ppt.layout.session
			&& current.layout.epoch == ppt.layout.epoch
			&& current.layout.pairVersions == ppt.layout.pairVersions,
			"current pair geometry and identity win over a stale drag-start snapshot");
	}

	void TestPptReleaseDragDecision()
	{
		constexpr POINT start{ -100, -200 };
		for (const auto region : { PptPointerRegion::Page, PptPointerRegion::Background })
		{
			Check(!ShouldStartPptDragOnRelease(WorkspaceMode::PptCompact,
				region, false, true, start, POINT{ -97, -198 }, 8, 6),
				"pending release inside the system threshold preserves click or preview");
			for (const POINT end : { POINT{ -104, -200 }, POINT{ -96, -200 },
				POINT{ -100, -203 }, POINT{ -100, -197 } })
			{
				Check(ShouldStartPptDragOnRelease(WorkspaceMode::PptCompact,
					region, false, true, start, end, 8, 6),
					"Up crossing the same centered drag threshold starts a final drag sample");
			}
			Check(ShouldStartPptDragOnRelease(WorkspaceMode::PptCompact,
				region, true, false, start, start, 8, 6)
				&& !ShouldStartPptDragOnRelease(WorkspaceMode::PptCompact,
					region, false, false, start, POINT{ 100, 100 }, 8, 6),
				"an active drag samples Up while a cleared gesture cannot restart");
		}
		Check(ShouldStartPptDragOnRelease(WorkspaceMode::PptCompact,
			PptPointerRegion::DragHandle, true, false, start, start, 8, 6),
			"an immediate drag handle also retains its final release sample");
		for (const auto region : { PptPointerRegion::Previous, PptPointerRegion::Next })
			Check(!ShouldStartPptDragOnRelease(WorkspaceMode::PptCompact,
				region, false, true, start, POINT{ 100, 100 }, 8, 6),
				"arrow release never converts a page action into a drag");
		for (const auto mode : { WorkspaceMode::Hidden, WorkspaceMode::WhiteboardExpanded })
			Check(!ShouldStartPptDragOnRelease(mode, PptPointerRegion::Page,
				false, true, start, POINT{ 100, 100 }, 8, 6)
				&& !ShouldStartPptDragOnRelease(mode, PptPointerRegion::DragHandle,
					true, false, start, POINT{ 100, 100 }, 8, 6),
				"hidden or Whiteboard release cannot publish a stale PPT drag");
	}

	void TestDragFrameOwnershipAndGeometry()
	{
		PptState frame;
		frame.presentationActive = true;
		frame.presentationVisible = true;
		frame.layout.session = 4;
		frame.layout.epoch = 8;
		frame.layout.pairVersions = { 10, 11 };
		PptDragCommitTracker tracker;
		BeginPptDragTracking(tracker, frame.layout);
		frame.layout.bottomPairWidth = 100.0F;
		frame.layout.bottomPairHeight = 75.0F;
		(void)PublishPptDragCandidate(tracker, frame.layout, 10);
		PptDragCommitTracker idleMiddle;
		Check(IsPptDragFrameOwned(tracker, frame, 0)
			&& IsPptDragFrameOwned(tracker, frame, 1)
			&& !IsPptDragFrameOwned(idleMiddle, frame, 2)
			&& !IsPptDragFrameOwned(tracker, frame, 4),
			"drag ownership covers the complete pair rather than only the pressed surface");
		(void)ReleasePptDragTracking(tracker, true);
		Check(IsPptDragFrameOwned(tracker, frame, 0)
			&& IsPptDragFrameOwned(tracker, frame, 1),
			"released pending drag keeps final-geometry ownership on both peers");
		auto otherPairUpdated = frame;
		++otherPairUpdated.layout.pairVersions[1];
		otherPairUpdated.layout.middlePairHeight = 80.0F;
		++otherPairUpdated.publicationRevision;
		Check(IsPptDragFrameOwned(tracker, otherPairUpdated, 0),
			"another pair publication does not interrupt current final-geometry ownership");
		for (int staleKind = 0; staleKind < 5; ++staleKind)
		{
			auto stale = frame;
			if (staleKind == 0) ++stale.layout.session;
			if (staleKind == 1) ++stale.layout.epoch;
			if (staleKind == 2) --stale.layout.pairVersions[0];
			if (staleKind == 3) --stale.layout.bottomPairHeight;
			if (staleKind == 4) stale.layout.bottomPairScale = 1.5F;
			Check(!IsPptDragFrameOwned(tracker, stale, 0),
				"another session epoch candidate or geometry cannot borrow drag ownership");
		}
		constexpr RECT target{ -320, 175, -119, 254 };
		Check(IsPptDragGeometryAtTarget(target, 1.0F, target, 1.0F)
			&& IsPptDragGeometryAtTarget(target, 1.00005F, target, 1.0F)
			&& !IsPptDragGeometryAtTarget(target, 1.0002F, target, 1.0F),
			"actual geometry requires stable pixel bounds and the existing scale tolerance");
		for (const float invalidScale : { (std::numeric_limits<float>::infinity)(),
			(std::numeric_limits<float>::quiet_NaN)() })
			Check(!IsPptDragGeometryAtTarget(target, invalidScale, target, 1.0F)
				&& !IsPptDragGeometryAtTarget(target, 1.0F, target, invalidScale),
				"non-finite actual or target scale cannot complete final drag geometry");
		for (int edge = 0; edge < 4; ++edge)
		{
			auto intermediate = target;
			if (edge == 0) --intermediate.left;
			if (edge == 1) --intermediate.top;
			if (edge == 2) ++intermediate.right;
			if (edge == 3) ++intermediate.bottom;
			Check(!IsPptDragGeometryAtTarget(intermediate, 1.0F, target, 1.0F),
				"one pixel of intermediate motion cannot count as final drag geometry");
		}
		Check(!MarkPptDragFrameCommitted(tracker, frame, 0) && tracker.pending
			&& MarkPptDragFrameCommitted(tracker, frame, 1)
			&& CompletePptDragPersistence(tracker, 10)
			&& !IsPptDragFrameOwned(tracker, frame, 0),
			"pair completion and runtime handoff release final-geometry ownership");
	}

	void TestWorkspaceAndDpiLayouts()
	{
		constexpr RECT monitor{ 0, 0, 1920, 1080 };
		PptState ppt;
		ppt.presentationActive = true;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = true;
		WhiteboardState whiteboard;

		const auto compact = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.0F, ppt, whiteboard);
		Check(compact.mode == WorkspaceMode::PptCompact && compact.visible
			&& Width(compact.logicalBounds) == 240
			&& Height(compact.logicalBounds) == 80,
			"bottom PPT control resolves standard two by two dimensions");
		const auto scaled = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.5F, ppt, whiteboard);
		Check(Width(scaled.logicalBounds) == 360
			&& Height(scaled.logicalBounds) == 120,
			"standard dimensions scale with DPI fallback");
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
			&& Width(expanded.logicalBounds) == 305
			&& Height(expanded.logicalBounds) == 80
			&& expanded.logicalBounds.left == 5
			&& VisibleBodyBottom(expanded, WhiteboardHeightDip) == monitor.bottom - 5
			&& expandedRight.logicalBounds.right == monitor.right - 5,
			"whiteboard ignores PPT offsets and stays at fixed bottom corners");
		Check(hiddenSide.mode == WorkspaceMode::Hidden && !hiddenSide.visible,
			"whiteboard workspace hides PPT side controls");

		ppt.presentationActive = false;
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
		ppt.presentationActive = true;
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
		const auto endShowPress = ResolvePptDirectionPressPolicy(
			WorkspaceMode::PptCompact, true, false);
		Check(disabledPress.invokeOnPointerDown && !disabledPress.trackLongPress
			&& enabledPress.invokeOnPointerDown && enabledPress.trackLongPress
			&& !whiteboardPress.invokeOnPointerDown
			&& !whiteboardPress.trackLongPress
			&& endShowPress.invokeOnPointerDown
			&& !endShowPress.trackLongPress,
			"PPT arrows invoke on down while EndShow and disabled presses do not repeat");
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

		state = ResolvePageControlTouchSample({}, 40,
			true, false, false, true, true, false, 100).state;
		decision = ResolvePageControlTouchSample(state, 40,
			false, true, false, true, true, false, 200);
		Check(decision.message == PageControlTouchMessage::None
			&& decision.state.active && decision.state.source == 100,
			"same contact id from another touch device cannot move the active pointer");
		decision = ResolvePageControlTouchSample(state, 40,
			false, false, true, true, true, false, 200);
		Check(decision.message == PageControlTouchMessage::None
			&& decision.state.active && decision.state.source == 100,
			"same contact id from another touch device cannot release the active pointer");
		decision = ResolvePageControlTouchSample(state, 40,
			true, false, false, false, false, false, 200);
		Check(decision.message == PageControlTouchMessage::None
			&& !decision.cancelPrevious && decision.state.source == 100,
			"fallback from another device cannot replace an active primary pointer");
		decision = ResolvePageControlTouchSample(state, 40,
			true, false, false, true, true, false, 200);
		Check(decision.message == PageControlTouchMessage::Down
			&& decision.cancelPrevious && decision.state.active
			&& decision.state.id == 40 && decision.state.source == 200,
			"primary same-id Down from a new device cancels before replacing ownership");
	}

	void TestWhiteboardLayoutTargetAndReadiness()
	{
		PptState ppt;
		ppt.presentationActive = true;
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

	void TestSharedBottomWorkspaceVisibility()
	{
		PptState ppt;ppt.presentationActive=true;ppt.presentationVisible=true;
		ppt.layout.showBottomPair=ppt.layout.showMiddlePair=true;
		ppt.uiScale=1.5F;ppt.layout.bottomPairScale=3.0F;
		ppt.layout.bottomPairWidth=ppt.layout.bottomPairHeight=10000;
		constexpr RECT monitor{0,0,800,600};
		const auto visible=ResolveRuntimePageControlLayout(monitor,1,ppt);
		ppt.presentationVisible=false;
		const auto covered=ResolveRuntimePageControlLayout(monitor,1,ppt);
		Check(ResolveWorkspaceMode(Surface::BottomLeft,ppt,{})==WorkspaceMode::PptCompact
			&&ResolveWorkspaceMode(Surface::BottomRight,ppt,{})==WorkspaceMode::PptCompact
			&&ResolveWorkspaceMode(Surface::MiddleLeft,ppt,{})==WorkspaceMode::Hidden,
			"live bottom survives the staged whiteboard visibility gap while middle controls hide");
		Check(covered.layout.bottomPairScale==visible.layout.bottomPairScale
			&&covered.layout.bottomPairWidth==visible.layout.bottomPairWidth
			&&covered.layout.bottomPairHeight==visible.layout.bottomPairHeight,
			"covered live bottom still fits and clamps before presentationVisible is restored");
		Check(covered.layout.middlePairScale==ppt.layout.middlePairScale
			&&covered.layout.middlePairWidth==ppt.layout.middlePairWidth
			&&covered.layout.middlePairHeight==ppt.layout.middlePairHeight,
			"covered middle pair does not join fitting or collision correction");
		ppt.layout.showBottomPair=false;
		Check(ResolveWorkspaceMode(Surface::BottomLeft,ppt,{})==WorkspaceMode::Hidden,
			"live session never overrides the user's bottom pair visibility setting");
		WhiteboardState board;board.active=board.expandedLayoutTarget=true;
		Check(ResolveWorkspaceMode(Surface::BottomLeft,ppt,board)==WorkspaceMode::WhiteboardExpanded
			&&ResolveWorkspaceMode(Surface::BottomLeft,ppt,board,true)==WorkspaceMode::Hidden,
			"whiteboard list wins the workspace projection while explicit minimization hides it");
		ppt.presentationActive=false;ppt.presentationVisible=true;ppt.layout.showBottomPair=true;
		Check(ResolveWorkspaceMode(Surface::BottomLeft,ppt,{})==WorkspaceMode::Hidden,
			"a truly ended session cannot revive a bottom pair through stale presentationVisible");
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
		changedPpt.presentationActive = true;
		Check(!ArePptStatesEquivalent(ppt, changedPpt),
			"live session changes publish while PPT controls remain hidden by whiteboard");
		changedPpt = ppt;
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
		ppt.presentationActive = true;
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
		extreme.presentationActive = true;
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
		for (const float dpi : { 1.0F, 1.5F, 2.0F })
		{
			PptState moved;
			moved.presentationActive = true;
			moved.presentationVisible = true;
			moved.uiScale = 0.6F;
			moved.layout.bottomPairScale = 1.5F;
			moved.layout.bottomPairWidth = 17;
			moved.layout.bottomPairHeight = 37;
			const auto movedLayout = ResolveSurfaceLayout(Surface::BottomLeft, monitor, dpi, moved, {});
			Check(VisibleBodyBottom(movedLayout, PptControlShortSideDip) == monitor.bottom - static_cast<LONG>(std::lround(5 * dpi)) - 37
				&& moved.layout.bottomPairWidth == 17 && moved.layout.bottomPairHeight == 37,
				"screen-DIP bottom inset preserves saved PPT drag offsets");
			auto maxDrag = moved.layout;
			maxDrag.bottomPairHeight = 10000;
			moved.layout = ClampPageControlLayout(Surface::BottomLeft, monitor, dpi, maxDrag, moved.uiScale);
			const auto clampedBounds = ResolveSurfaceLayout(Surface::BottomLeft, monitor, dpi, moved, {}).logicalBounds;
			Check(clampedBounds.top >= monitor.top && clampedBounds.top <= monitor.top + 5,
				"PPT drag clamp uses the same fixed bottom inset at the opposite screen edge");
		}
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
		Check(clamped.bottomPairWidth == 155.0F
			&& clamped.bottomPairHeight == 509.0F,
			"PPT drag clamp uses its active standard geometry without inheriting whiteboard width");
	}

	void TestA2ProjectionAndMigration()
	{
		using namespace Inkeys::UI::Bar;
		const auto desktop = ResolveBarA2Projection(false, false);
		const auto presentation = ResolveBarA2Projection(true, false);
		const auto whiteboard = ResolveBarA2Projection(true, true);
		const auto disabledDesktop = ResolveBarA2Projection(false, false, false);
		const auto disabledPresentation = ResolveBarA2Projection(true, false, false);
		Check(desktop.whiteboardTwoTwo && desktop.freezeVisible
			&& !desktop.endShowVisible,
			"desktop A2 keeps a standard 2x2 whiteboard entry");
		Check(presentation.whiteboardTwoTwo && !presentation.freezeVisible
			&& presentation.endShowVisible,
			"PPT A2 hides unavailable Freeze and keeps EndShow");
		Check(whiteboard.whiteboardTwoTwo && !whiteboard.freezeVisible
			&& !whiteboard.endShowVisible,
			"whiteboard A2 shows only Close Whiteboard");
		Check(!disabledDesktop.whiteboardTwoTwo && disabledDesktop.freezeVisible
			&& !disabledDesktop.endShowVisible && !disabledPresentation.whiteboardTwoTwo
			&& !disabledPresentation.freezeVisible && disabledPresentation.endShowVisible,
			"temporarily disabled whiteboard still hides PPT Freeze");
		Check(IsLegacyBarA2Pair("Inkeys.Bar.Whiteboard", "Inkeys.Bar.Freeze")
			&& IsLegacyBarA2Pair("Inkeys.Bar.Freeze", "Inkeys.Bar.Whiteboard"),
			"both legal legacy A2 orders are accepted");
		Check(!IsLegacyBarA2Pair("Inkeys.Bar.Whiteboard", "Inkeys.Bar.EndShow"),
			"invalid legacy A2 pair returns to defaults");

		for (const bool presentationActive : { false, true })
		for (const bool separate : { false, true })
		for (const bool selectionEmpty : { false, true })
		for (const bool available : { false, true })
		{
			const auto view = ResolveBarA2Projection(presentationActive, false, false,
				separate, selectionEmpty, available);
			Check(view.freezeVisible == (available && !presentationActive),
				"Freeze hides whenever unavailable in either operation mode");
			Check(view.pierceVisible == (separate && !selectionEmpty),
				"Pierce only appears in separate mode outside empty Selection");
			Check(!view.pierceVisible || view.pierceTwoTwo == !view.freezeVisible,
				"Pierce takes full slot precisely when Freeze is hidden");
			Check(!view.freezeVisible || view.freezeTwoTwo == !view.pierceVisible,
				"Freeze takes full slot when Pierce is hidden");
			const auto board = ResolveBarA2Projection(presentationActive, true, true,
				separate, selectionEmpty, available);
			Check(board.whiteboardTwoTwo && !board.freezeVisible && !board.pierceVisible,
				"Whiteboard never gains Click-through in either operation mode");
		}

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
	TestNativeMouseAndDragSupplement();
	TestEndShowContentTargets();
	TestNavigationWaitingLifecycle();
	TestDragCommitHandoff();
	TestVersionedDragPublication();
	TestInterleavedPairFrameCommit();
	TestPagePresentationCommit();
	TestCanvasRecoveryPresentationCommit();
	TestHideAndPageCommitRace();
	TestEffectiveScaleAndResources();
	TestTaskbarUiScale();
	TestIndependentSurfaceFitRegression();
	TestDragPureTranslationAndRevisionGate();
	TestMiddlePairCenterSnap();
	TestMiddleSnapCollisionFallback();
	TestPptReleaseDragDecision();
	TestDragFrameOwnershipAndGeometry();
	TestWorkspaceAndDpiLayouts();
	TestWorkspaceTransitionAndInputPolicy();
	TestTouchTranslationPolicy();
	TestWhiteboardLayoutTargetAndReadiness();
	TestSharedBottomWorkspaceVisibility();
	TestDirtyInvalidationDeduplication();
	TestRuntimeCollisionFallback();
	TestA2ProjectionAndMigration();
	return failureCount;
}
