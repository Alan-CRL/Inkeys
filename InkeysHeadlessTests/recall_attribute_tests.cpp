#include <algorithm>
#include <cmath>
#include <iostream>

import Inkeys.UI.Bar.RecallAttributeMotion;
import Inkeys.UI.Bar.RecallChrome;
import Inkeys.UI.Bar.Animation;

int RunRecallAttributeTests()
{
	using namespace Inkeys::UI::Bar;
	int failures = 0;
	auto expect = [&](bool value, const char* message)
	{
		if (!value) { ++failures; std::cerr << "[RecallAttribute] " << message << '\n'; }
	};
	auto centerX = [](RecallAttributeRect r) { return (r.left + r.right) / 2; };
	auto centerY = [](RecallAttributeRect r) { return (r.top + r.bottom) / 2; };

	{
		RecallAttributeLayoutInput input;
		input.work = {0, 0, 1400, 900};
		input.main = {500, 420, 880, 500};
		input.anchor = {650, 425, 720, 495};
		input.below = true;
		const auto layout = ResolveRecallAttributeLayout(input);
		const double width = RecallAttributeStandardGapDip + BarButtonTwoSideDip + RecallAttributeStandardGapDip;
		expect(std::abs(layout.panel.Width() - width) < 0.001, "single redo panel is 5+70+5 DIP wide");
		expect(std::abs(layout.panel.Height() - input.main.Height()) < 0.001, "panel height matches the main bar");
		expect(std::abs(layout.items[0].Width() - BarButtonTwoSideDip) < 0.001
			&& std::abs(layout.items[0].Height() - BarButtonTwoSideDip) < 0.001,
			"redo slot is the shared 70x70 button");
		expect(std::abs(centerX(layout.items[0]) - centerX(input.anchor)) < 0.001,
			"redo stays anchored to the recall entry");
		expect(std::abs(layout.panel.top - (input.main.bottom + BarMainButtonToMainBarGapDip)) < 0.001 && layout.below,
			"preferred side keeps the 10 DIP gap below the main bar");
		expect(std::abs(layout.items[0].left - (layout.panel.left + RecallAttributeStandardGapDip)) < 0.001,
			"redo keeps the standard 5 DIP inset");
	}
	{
		RecallAttributeLayoutInput input;
		input.work = {0, 0, 400, 200};
		input.main = {100, 110, 300, 190};
		input.anchor = {160, 120, 230, 190};
		input.below = true;
		const auto flipped = ResolveRecallAttributeLayout(input);
		expect(!flipped.below && flipped.panel.bottom <= input.main.top,
			"insufficient space below flips the panel above the main bar");
		input.lockPanelSide = true;
		const auto locked = ResolveRecallAttributeLayout(input);
		expect(locked.below, "drag and side-switch lock the presented side");
	}
	{
		RecallAttributeLayoutInput input;
		input.work = {0, 0, 50, 400};
		input.main = {0, 80, 50, 160};
		input.anchor = {-20, 85, 50, 155};
		input.below = true;
		const auto clipped = ResolveRecallAttributeLayout(input);
		expect(clipped.horizontalOverflow && clipped.panel.left >= input.work.left
			&& clipped.panel.right <= input.work.right,
			"narrow work area clips the shell without a compact layout");
	}
	expect(ResolveRecallAttributeRelease(0, 0, true, false) == 0, "redo release stays on the pressed slot");
	expect(ResolveRecallAttributeRelease(-1, 0, true, false) == -1, "opening release cannot trigger redo");
	expect(ResolveRecallAttributeRelease(0, 0, true, true) == -1
		&& ResolveRecallAttributeRelease(0, -1, false, false) == -1,
		"cancel and external release produce no redo");

	{
		RecallSurfaceMotion motion;
		BarUiAnimationAdvanceContextClass frame{1.0 / 60, 1, true, false};
		motion.Retarget(true, BarUiDefaultOperationDur);
		double openingPeak = 0;
		for (int i = 0; i < 24; ++i)
		{
			motion.Advance(frame);
			openingPeak = (std::max)(openingPeak, motion.Geometry());
			expect(motion.Opacity() >= 0 && motion.Opacity() <= 1, "alpha stays bounded during Back overshoot");
		}
		expect(openingPeak > 1.01 && motion.Geometry() == 1 && !motion.Active(),
			"opening uses the shared 0.4s Back curve and settles on the frame boundary");
		motion.Retarget(false, BarUiDefaultOperationDur);
		double closingPeak = 0;
		for (int i = 0; i < 24; ++i)
		{
			motion.Advance(frame);
			closingPeak = (std::max)(closingPeak, motion.Geometry());
			if (i < 8) expect(motion.Visible(), "EaseInBack close stays visible through the early rebound");
		}
		expect(closingPeak > 1.01 && !motion.Visible() && !motion.Active(),
			"closing rebounds then retires inside the same 0.4s batch");
		motion.Retarget(true, BarUiDefaultOperationDur);
		for (int i = 0; i < 7; ++i) motion.Advance(frame);
		const double before = motion.Geometry();
		motion.Retarget(false, BarUiDefaultOperationDur);
		motion.Advance({0, 1, true, false});
		expect(motion.Geometry() == before, "mid-animation reversal keeps the current pose");
		motion.Retarget(true, BarUiDefaultOperationDur);
		motion.Advance({0, 1, false, false});
		expect(motion.Geometry() == 1 && motion.Opacity() == 1 && !motion.Active(),
			"disabled animation settles immediately");
		BarUiTimelineClass parent;
		parent.Restart(BarUiDefaultOperationDur);
		parent.Advance(0.1, 1);
		motion.Retarget(false, 0);
		motion.Advance({0, 1, false, false});
		motion.Retarget(true, BarUiDefaultOperationDur, &parent);
		expect(std::abs(motion.Timeline().GetRemainingDuration() - 0.3) < 0.001,
			"popup joins the parent timeline during its first half");
		const RecallAttributeRect panel{200, 300, 280, 380};
		const RecallAttributeRect anchor{205, 390, 275, 460};
		const auto compact = ResolveRecallSurfacePose(panel, anchor, 0, 60);
		expect(std::abs(compact.Apply(panel).Width() - 60) < 0.001
			&& std::abs(centerX(compact.Apply(panel)) - centerX(anchor)) < 0.001,
			"closed popup compresses to the shared 60 DIP anchor");
		const auto overshoot = ResolveRecallSurfacePose(panel, anchor, openingPeak, 60);
		expect(overshoot.Apply(panel).Width() > panel.Width(),
			"dirty bounds follow the actual Back overshoot");
		std::cout << "[RecallMotion] openingPeak=" << openingPeak << " closingPeak=" << closingPeak << '\n';
	}
	{
		using Availability = RecallHistoryAvailability;
		RecallChromeState state;
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, false });
		auto view = ResolveRecallChrome(state);
		expect(view.undoExpanded && view.mainUndoVisible && !view.mainRedoVisible && !view.popupOpen
			&& view.undoEnabled && !view.redoEnabled,
			"only undo rests as a 2x2 undo");
		const auto rejectedRedo = ApplyRecallChromeEvent(state, RecallChromeEvent::RedoClick);
		expect(rejectedRedo.hold == state.hold, "gray redo does not take the 2x2");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::UndoClick);
		view = ResolveRecallChrome(state);
		expect(state.hold == RecallChromeHold::Undo && view.popupOpen && view.popupShowsRedo
			&& view.undoExpanded && !view.mainRedoVisible,
			"undo click keeps a 2x2 undo and puts redo in the popup");
		expect(RecallChromeIdleElapsed(state.hold, RecallChromeIdleSeconds)
			&& !RecallChromeIdleElapsed(state.hold, RecallChromeIdleSeconds - 0.01),
			"hold lasts 5 seconds");
		state.availability = { true, true };
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, true });
		expect(state.hold == RecallChromeHold::Undo && ResolveRecallChrome(state).undoExpanded,
			"availability changes color immediately and does not change the held layout");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		view = ResolveRecallChrome(state);
		expect(state.hold == RecallChromeHold::None && !view.popupOpen
			&& view.mainRedoVisible && view.mainUndoVisible && !view.undoExpanded && !view.redoExpanded,
			"after 5 seconds both commands rest as redo over undo");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::RedoClick);
		view = ResolveRecallChrome(state);
		expect(state.hold == RecallChromeHold::Redo && view.popupOpen && !view.popupShowsRedo
			&& view.redoExpanded && !view.mainUndoVisible,
			"a 2x1 redo click makes redo 2x2 and moves undo into the popup");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { false, true });
		view = ResolveRecallChrome(state);
		expect(!view.popupOpen && view.redoExpanded && view.mainRedoVisible && !view.mainUndoVisible
			&& !view.undoEnabled && view.redoEnabled,
			"only redo rests as a 2x2 redo");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::RedoClick);
		expect(ResolveRecallChrome(state).popupOpen && !ResolveRecallChrome(state).popupShowsRedo,
			"clicking the lone redo opens undo in the popup");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::SuppressPopup);
		expect(!ResolveRecallChrome(state).popupOpen && ResolveRecallChrome(state).redoExpanded
			&& state.hold == RecallChromeHold::Redo,
			"drawing hides the popup but keeps the held 2x2 until the timer");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::ReturnToDesktop);
		view = ResolveRecallChrome(state);
		expect(state.hold == RecallChromeHold::None && !view.popupOpen && view.redoExpanded,
			"returning to the desktop settles without reopening the popup");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, {});
		view = ResolveRecallChrome(state);
		expect(view.undoExpanded && view.mainUndoVisible && !view.mainRedoVisible
			&& !view.undoEnabled && !view.redoEnabled,
			"neither command leaves a gray 2x2 undo");
	}
	std::cout << "[RecallAttribute] failures=" << failures << '\n';
	return failures;
}
