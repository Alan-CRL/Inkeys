#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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
		static_assert(RecallChromeIdleSeconds == 3.0);
		RecallChromeState state;
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, false });
		auto view = ResolveRecallChrome(state);
		expect(view.undoExpanded && view.mainUndoVisible && !view.mainRedoVisible && !view.popupOpen
			&& view.undoEnabled && !view.redoEnabled, "only undo rests as a 2x2 undo");
		const auto rejected = ApplyRecallChromeCommandResult(state, true, state.availability, true);
		expect(rejected.hold == state.hold && rejected.idleResetSerial == state.idleResetSerial,
			"disabled commands do not open or restart the layout");
		const auto notAccepted = ApplyRecallChromeCommandResult(state, false, state.availability, false);
		expect(notAccepted.hold == state.hold && !notAccepted.settlePending,
			"a rejected publish leaves the UI untouched");

		state = ApplyRecallChromeCommandResult(state, false, state.availability, true);
		view = ResolveRecallChrome(state);
		expect(state.hold == RecallChromeHold::Undo && view.popupOpen && view.popupShowsRedo
			&& view.undoExpanded && !view.mainRedoVisible,
			"undo click keeps a 2x2 undo and puts redo in the popup");
		expect(RecallChromeIdleElapsed(state, 3.0) && !RecallChromeIdleElapsed(state, 2.99),
			"the pending layout waits a full 3 seconds");
		const auto beforeAvailability = state;
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, true });
		expect(!RecallChromeNeedsIdleRestart(beforeAvailability, state) && ResolveRecallChrome(state).undoExpanded,
			"availability changes color without restarting or changing a clicked layout");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		view = ResolveRecallChrome(state);
		expect(state.hold == RecallChromeHold::None && !state.settlePending && !view.popupOpen
			&& view.mainRedoVisible && view.mainUndoVisible && !view.undoExpanded && !view.redoExpanded,
			"after 3 seconds both commands rest as redo over undo");

		// 模拟真实末步顺序：点击前可用，Publish 后耗尽，互斥关闭再应用已接受的点击。
		const auto clickAvailability = state.availability;
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, false });
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Dismiss);
		state = ApplyRecallChromeCommandResult(state, true, clickAvailability, true);
		view = ResolveRecallChrome(state);
		expect(state.hold == RecallChromeHold::Redo && view.popupOpen && !view.popupShowsRedo
			&& view.redoExpanded && !view.mainUndoVisible && !view.redoEnabled && view.undoEnabled,
			"the last 2x1 redo still expands redo and opens undo even after asynchronous exhaustion");
		const auto beforePopupUndo = state;
		state = ApplyRecallChromeCommandResult(state, false, state.availability, true);
		view = ResolveRecallChrome(state);
		expect(RecallChromeNeedsIdleRestart(beforePopupUndo, state) && state.hold == RecallChromeHold::Redo
			&& view.redoExpanded && !view.popupShowsRedo && view.popupOpen,
			"popup undo keeps main redo and popup undo identities while restarting 3 seconds");
		const auto beforeRepeatedUndo = state;
		state = ApplyRecallChromeCommandResult(state, false, state.availability, true);
		expect(RecallChromeNeedsIdleRestart(beforeRepeatedUndo, state),
			"every accepted repeated command postpones the idle deadline");
		for (const auto event : { RecallChromeEvent::Dismiss, RecallChromeEvent::SuppressPopup,
			RecallChromeEvent::ReturnToDesktop })
		{
			const auto closed = ApplyRecallChromeEvent(state, event);
			const auto closedView = ResolveRecallChrome(closed);
			expect(!closedView.popupOpen && closedView.redoExpanded && closed.hold == RecallChromeHold::Redo
				&& closed.settlePending && !RecallChromeNeedsIdleRestart(state, closed),
				"close, drawing and workspace changes preserve the clicked deadline and identity");
			const auto reopened = ApplyRecallChromeCommandResult(closed, false, closed.availability, true);
			expect(reopened.hold == RecallChromeHold::Redo && ResolveRecallChrome(reopened).redoExpanded,
				"a suppressed popup cannot let its opposite command swap the main identity early");
		}
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, {});
		expect(!ResolveRecallChrome(state).redoEnabled && !ResolveRecallChrome(state).undoEnabled
			&& ResolveRecallChrome(state).redoExpanded, "both disabled colors keep the protected geometry");
		const auto grayClicked = ApplyRecallChromeCommandResult(state, true, state.availability, false);
		expect(RecallChromeNeedsIdleRestart(state, grayClicked) && grayClicked.hold == state.hold
			&& ResolveRecallChrome(grayClicked).redoExpanded,
			"clicking the exhausted command delays the existing protection without moving its identity");
		const auto grayClosed = ApplyRecallChromeEvent(grayClicked, RecallChromeEvent::Dismiss);
		const auto grayClosedClicked = ApplyRecallChromeCommandResult(grayClosed, true, grayClosed.availability, false);
		expect(RecallChromeNeedsIdleRestart(grayClosed, grayClosedClicked) && !ResolveRecallChrome(grayClosedClicked).popupOpen,
			"gray repeated clicks postpone a suppressed layout without reopening the popup");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		view = ResolveRecallChrome(state);
		expect(view.undoExpanded && !view.popupOpen && !view.undoEnabled && !view.redoEnabled,
			"exhaustion settles into a gray 2x2 undo only after idle");
	}
	{
		RecallChromeState state;
		const auto before = state;
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, true });
		expect(RecallChromeNeedsIdleRestart(before, state) && state.settlePending && state.hold == RecallChromeHold::None
			&& ResolveRecallChrome(state).undoExpanded,
			"availability without a click starts a pending timer and keeps the old geometry");
		const auto unchanged = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, true });
		expect(!RecallChromeNeedsIdleRestart(state, unchanged), "per-frame availability polling does not extend idle");
		const auto retargeted = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { false, true });
		expect(RecallChromeNeedsIdleRestart(state, retargeted) && ResolveRecallChrome(retargeted).undoExpanded,
			"a different unclicked pending target starts its own 3 second wait");
		const auto cancelled = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, false });
		expect(!cancelled.settlePending && ResolveRecallChrome(cancelled).undoExpanded,
			"returning to the displayed target cancels an unclicked pending change");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, false });
		expect(state.settlePending && ResolveRecallChrome(state).mainRedoVisible
			&& !ResolveRecallChrome(state).redoEnabled && !ResolveRecallChrome(state).undoExpanded,
			"redo exhaustion in idle 2x1 grays redo immediately and postpones 2x2 undo");
		const auto grayPendingClick = ApplyRecallChromeCommandResult(state, true, state.availability, false);
		expect(RecallChromeNeedsIdleRestart(state, grayPendingClick) && !ResolveRecallChrome(grayPendingClick).popupOpen,
			"gray clicks also delay an availability-only pending layout without opening a popup");
		const auto closed = ApplyRecallChromeEvent(state, RecallChromeEvent::Dismiss);
		expect(closed.settlePending && !RecallChromeNeedsIdleRestart(state, closed)
			&& !ResolveRecallChrome(closed).undoExpanded,
			"dismiss also preserves an availability-only pending timer");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		expect(ResolveRecallChrome(state).undoExpanded && !state.settlePending,
			"availability-only transitions settle on their timer");

		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { false, true });
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		expect(ResolveRecallChrome(state).redoExpanded && !ResolveRecallChrome(state).mainUndoVisible,
			"only redo rests as a 2x2 redo");
		state = ApplyRecallChromeCommandResult(state, true, state.availability, true);
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, true });
		const auto afterPopup = ApplyRecallChromeCommandResult(state, false, state.availability, true);
		expect(afterPopup.hold == RecallChromeHold::Redo && !ResolveRecallChrome(afterPopup).popupShowsRedo,
			"redo-only main and popup undo remain in place during opposite commands");

		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		state = ApplyRecallChromeCommandResult(state, false, state.availability, true);
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { false, true });
		const auto lastUndo = ApplyRecallChromeCommandResult(state, false, { true, true }, true);
		expect(ResolveRecallChrome(lastUndo).undoExpanded && !ResolveRecallChrome(lastUndo).undoEnabled,
			"last undo keeps its expanded feedback even if availability already changed");
		const auto popupRedo = ApplyRecallChromeCommandResult(lastUndo, true, lastUndo.availability, true);
		expect(popupRedo.hold == RecallChromeHold::Undo && ResolveRecallChrome(popupRedo).popupShowsRedo
			&& ResolveRecallChrome(popupRedo).undoExpanded,
			"popup redo is symmetric and never moves under a repeat click");
	}
	{
		RecallChromeState state;
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { false, true });
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		const auto checkHeldRedoColumn = [&](const char* message)
		{
			const auto view = ResolveRecallChrome(state);
			expect(view.mainRedoVisible && view.redoExpanded && !view.mainUndoVisible && !view.undoExpanded, message);
			// 生产网格中一枚 2×2 或上下两枚 2×1 均只占原来的 75 DIP 列。
			const unsigned halfColumns = (view.mainRedoVisible ? (view.redoExpanded ? 2u : 1u) : 0u)
				+ (view.mainUndoVisible ? (view.undoExpanded ? 2u : 1u) : 0u);
			expect(halfColumns == 2u, "the held redo group never adds a second main-bar column");
		};
		checkHeldRedoColumn("redo-only 2x2 starts in one main-bar column");
		state = ApplyRecallChromeCommandResult(state, true, state.availability, true);
		checkHeldRedoColumn("clicking 2x2 redo keeps its main-bar visible set and sizes");
		expect(ResolveRecallChrome(state).popupOpen && !ResolveRecallChrome(state).popupShowsRedo,
			"the fixed redo column opens popup undo");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, true });
		checkHeldRedoColumn("restoring undo availability does not insert undo into the held main bar");
		state = ApplyRecallChromeCommandResult(state, false, state.availability, true);
		checkHeldRedoColumn("popup undo never changes main-bar column occupancy");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Dismiss);
		checkHeldRedoColumn("closing popup undo leaves the held redo column unchanged");
		state = ApplyRecallChromeCommandResult(state, true, state.availability, true);
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, false });
		checkHeldRedoColumn("the last redo and its asynchronous exhaustion keep one column");
		state = ApplyRecallChromeCommandResult(state, true, state.availability, false);
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::ReturnToDesktop);
		checkHeldRedoColumn("gray repeat clicks and workspace closing cannot add a temporary column");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::Availability, { true, true });
		expect(!RecallChromeIdleElapsed(state, 2.99) && RecallChromeIdleElapsed(state, 3.0),
			"the redo column retains its existing 3 second protection");
		state = ApplyRecallChromeEvent(state, RecallChromeEvent::IdleElapsed);
		const auto compact = ResolveRecallChrome(state);
		expect(compact.mainRedoVisible && compact.mainUndoVisible && !compact.redoExpanded && !compact.undoExpanded
			&& !compact.popupOpen, "after idle the same column legitimately becomes redo over undo");
	}
	{
		// 主栏和弹窗共用生产颜色解析，再用真实颜色动画检查中间帧、反向与终态。
		constexpr unsigned normal = 0x00302010;
		BarUiColorClass color(normal);
		const auto advance = [&] { return BarUiAdvanceAnimation(color, { 0.05, 1, true, false }); };
		const auto target = [&](bool enabled) { color.SetTar(ResolveRecallCommandContentColor(enabled, normal)); };
		target(false);
		advance();
		expect(color.tar == RecallCommandUnavailableColor && color.val != normal
			&& color.val != RecallCommandUnavailableColor, "gray animation has a real intermediate color");
		const auto middle = color.val.load();
		target(true);
		expect(color.val == middle && color.startColor == middle && color.tar == normal,
			"re-enabling reverses color continuously from the displayed color");
		for (int frame = 0; frame < 10; ++frame) { target(true); advance(); }
		expect(color.val == normal && color.IsSame(), "repeated enabled targets converge to normal");
		for (int frame = 0; frame < 10; ++frame) { target(false); advance(); }
		expect(color.val == RecallCommandUnavailableColor && color.IsSame(),
			"repeated disabled targets converge to the brush gray without animation resets");
	}

	{
		// Headless 不编译 D2D 面板，源级检查真实 Advance 没有绕过可测状态策略。
		namespace fs = std::filesystem;
		const fs::path relative = "Inkeys/Inkeys/UI/Bar/Bar.RecallAttribute.cpp";
		fs::path source = fs::path(__FILE__).parent_path().parent_path() / relative;
		if (!fs::exists(source))
		{
			for (auto root = fs::current_path(); !root.empty(); root = root.parent_path())
			{
				if (fs::exists(root / relative)) { source = root / relative; break; }
				if (root == root.parent_path()) break;
			}
		}
		std::ifstream file(source, std::ios::binary);
		const std::string code((std::istreambuf_iterator<char>(file)), {});
		const auto begin = code.find("bool BarRecallAttributePanel::Advance(");
		const auto end = code.find("void BarRecallAttributePanel::Draw(", begin);
		expect(begin != std::string::npos && end != std::string::npos,
			"production Recall panel source is available for its wiring contract");
		if (begin != std::string::npos && end != std::string::npos)
		{
			const auto advance = code.substr(begin, end - begin);
			const auto snapshot = advance.find("owner.RecallChromeView()");
			expect(snapshot != std::string::npos && advance.find("owner.RecallChromeView()", snapshot + 1) == std::string::npos,
				"production panel uses one chrome snapshot for gate, anchor and popup identity");
			expect(advance.find("chrome.redoExpanded ? BarButtonPresetEnum::Redo : BarButtonPresetEnum::Recall") != std::string::npos
				&& advance.find("anchor && !anchor->IsVisible()") != std::string::npos
				&& advance.find("anchor->button.Inherit(") != std::string::npos
				&& advance.find("currentInput.anchor = { anchor->button.inhX") != std::string::npos,
				"production panel tests visibility and positions geometry from the selected undo or redo anchor");
			expect(advance.find("recallPreset") == std::string::npos
				&& advance.find("chrome.popupOpen && !shouldClose") != std::string::npos,
				"hiding main undo during redo expansion cannot close popup undo");
			expect(advance.find("commandPreset = showsRedo ? BarButtonPresetEnum::Redo : BarButtonPresetEnum::Recall") != std::string::npos
				&& advance.find("iconResource = showsRedo ? L\"barRedo\" : L\"barRecall\"") != std::string::npos
				&& advance.find("label = showsRedo ? RedoButtonLabel() : UndoButtonLabel()") != std::string::npos
				&& advance.find("b.preset = commandPreset") != std::string::npos
				&& advance.find("b.icon.InitializationFromResource(L\"UI\", iconResource)") != std::string::npos
				&& advance.find("b.name.SetStringImmediate(label)") != std::string::npos,
				"popup command identity commits its matching SVG and label together without showing the previous action");
			expect(advance.find("b.icon.AdvanceContentTransition(dt, speed)") != std::string::npos
				&& advance.find("b.name.AdvanceContentTransition(dt, speed)") != std::string::npos
				&& advance.find("active_ |= iconContentAdvanced || nameContentAdvanced") != std::string::npos,
				"production popup advances SVG and Word targets and keeps rendering until their content is committed");
		}
		// 宽度扫描与状态刷新并行，检查生产归位入口不会临时把隐藏的 Undo 插回主栏。
		std::ifstream buttonFile(source.parent_path() / "Bar.Button.cpp", std::ios::binary);
		const std::string buttons((std::istreambuf_iterator<char>(buttonFile)), {});
		const auto homingBegin = buttons.find("void BarButtonSetClass::PresetHoming(");
		const auto homingEnd = buttons.find("void BarButtonSetClass::CalcState(", homingBegin);
		expect(homingBegin != std::string::npos && homingEnd != std::string::npos,
			"production button homing source is available for the width contract");
		if (homingBegin != std::string::npos && homingEnd != std::string::npos)
		{
			const auto homing = buttons.substr(homingBegin, homingEnd - homingBegin);
			const auto chromeProjection = homing.find("barUISet.ApplyRecallChromeVisuals()");
			expect(chromeProjection != std::string::npos,
				"production homing publishes the final recall chrome projection");
			if (chromeProjection != std::string::npos)
			{
				const auto beforeProjection = homing.substr(0, chromeProjection);
				expect(beforeProjection.find("BarButtonPresetEnum::Recall]->hide") == std::string::npos
					&& beforeProjection.find("BarButtonPresetEnum::Redo]->hide") == std::string::npos,
					"production homing cannot expose an extra undo or redo column before its final projection");
			}
		}
		// 验证实际内容入口的提交/缓存链，防止目标正确但绘制仍消费旧 val。
		std::ifstream uiFile(source.parent_path() / "Bar.UI.cpp", std::ios::binary);
		const std::string ui((std::istreambuf_iterator<char>(uiFile)), {});
		expect(!ui.empty(), "production SVG and Word source is available for its commit contract");
		const auto section = [&](const char* start, const char* next)
		{
			const auto first = ui.find(start);
			if (first == std::string::npos) return std::string{};
			const auto last = ui.find(next, first);
			return last == std::string::npos ? std::string{} : ui.substr(first, last - first);
		};
		const auto svgImmediate = section("void BarUiSVGClass::InitializationFromString(", "void BarUiSVGClass::InitializationFromResource(");
		const auto svgCommit = section("void BarUiSVGClass::ApplyContentDirect(", "bool BarUiSVGClass::CacheBitmap(");
		const auto wordImmediate = section("bool BarUiWordClass::SetStringImmediate(", "bool BarUiWordClass::AdvanceContentTransition(");
		expect(svgImmediate.find("CancelContentTransition()") != std::string::npos
			&& svgImmediate.find("ApplyContentDirect(valT)") != std::string::npos
			&& svgCommit.find("svg.Initialization(valT)") != std::string::npos
			&& svgCommit.find("ResetCache()") != std::string::npos,
			"production immediate SVG cancels stale keyframes, commits the rendered value and invalidates its old bitmap");
		expect(wordImmediate.find("ApplyBarImmediateContentUpdate(") != std::string::npos
			&& wordImmediate.find("content.Initialization(contentT)") != std::string::npos
			&& wordImmediate.find("transitionContent.Initialization(contentT)") != std::string::npos,
			"production immediate Word shares the tested transaction and commits both rendered and pending strings");
	}

	std::cout << "[RecallAttribute] failures=" << failures << '\n';
	return failures;
}
