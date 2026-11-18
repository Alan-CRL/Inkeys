#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>

import Inkeys.UI.UI3.PanelStack;

using namespace Inkeys::UI::UI3;

namespace
{
	int failures = 0;
	void Check(bool condition, std::string_view name)
	{
		if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; }
	}
	PanelFrame Frame(double left = 0, bool visible = true)
	{
		return {{left - 8, -8, left + 108, 88}, {{left, 0, left + 100, 80}}, visible};
	}
	void TestOrderingAndWhitespace()
	{
		PanelState main, more, hidden;
		main.SetCandidate(Frame()); more.SetCandidate(Frame(20)); hidden.SetCandidate(Frame());
		PanelStack stack{{1, &main, 2, 1}, {2, &more, 1, 2}, {3, &hidden, 0, 3}};
		Check(!stack.FrontPanel(30, 30), "unpresented candidates never own a point");
		main.CommitPresented(); more.CommitPresented();
		const auto& order = stack.DrawOrder();
		Check(order.size() == 3 && order[0].id == 3 && order[1].id == 2 && order[2].id == 1,
			"draw traversal orders back to front");
		Check(stack.FrontPanel(30, 30) == 2 && stack.FrontPanel(10, 30) == 1,
			"more input precedes main despite its lower draw order");
		Check(more.Presented().BlocksWhitespace(30, 30, false)
			&& !more.Presented().BlocksWhitespace(30, 30, true), "panel consumes only member-free whitespace");
		Check(!stack.FrontPanel(-5, 30) && !stack.FrontPanel(120, 30),
			"visual shadow and half-open right edge are outside interaction");
		hidden.SetCandidate(Frame(0, false)); hidden.CommitPresented();
		Check(stack.FrontPanel(30, 30) == 2, "invisible presented panel cannot steal input");
		auto disabled = Frame(); disabled.blocksInput = false; hidden.SetCandidate(disabled); hidden.CommitPresented();
		Check(stack.FrontPanel(30, 30) == 2, "noninteractive panel cannot own whitespace");
		PanelStack empty{}; Check(!empty.FrontPanel(0, 0), "empty stack has no owner");
	}
	void TestCandidateAndSequence()
	{
		PanelState panel; PanelStack stack{{7, &panel, 0, 0}};
		panel.SetCandidate(Frame()); panel.CommitPresented();
		panel.SetCandidate(Frame(200));
		Check(stack.FrontPanel(10, 10) == 7 && !stack.FrontPanel(210, 10),
			"failed or unconfirmed frame retains old successful range");
		Check(!panel.CaptureCandidate(0) && panel.CaptureCandidate(1), "capture requires a positive increasing serial");
		panel.SetCandidate(Frame(400));
		Check(panel.ConfirmPresented(1) && stack.FrontPanel(210, 10) == 7 && !stack.FrontPanel(410, 10),
			"acknowledgement confirms the captured range rather than a later candidate");
		Check(!panel.ConfirmPresented(1) && !panel.CaptureCandidate(1), "duplicate and committed serials are rejected");
		Check(panel.CaptureCandidate(2), "next serial can capture a fresh candidate");
		panel.SetCandidate(Frame(600)); Check(panel.CaptureCandidate(3), "new capture supersedes the pending capture");
		Check(!panel.CanConfirm(2) && !panel.ConfirmPresented(2) && !panel.ConfirmPresented(4)
			&& stack.FrontPanel(210, 10) == 7, "out-of-order and unknown receipts preserve presented state");
		Check(!panel.CaptureCandidate(2) && panel.ConfirmPresented(3) && stack.FrontPanel(610, 10) == 7,
			"latest exact receipt succeeds after stale receipts");
		panel.SetCandidate(Frame(800)); Check(panel.CaptureCandidate(4), "capture before direct commit");
		panel.CommitPresented();
		Check(!panel.ConfirmPresented(4) && stack.FrontPanel(810, 10) == 7,
			"direct successful commit retires an earlier pending receipt");
		panel.SetCandidate(Frame(0, false));
		Check(stack.FrontPanel(810, 10) == 7, "unconfirmed hide cannot remove old input");
		panel.CommitPresented(); Check(!stack.FrontPanel(810, 10), "confirmed hide retires input");
	}
	void TestGeometryAndCopiedSnapshot()
	{
		PanelRegion rounded{{0, 0, 100, 80}, 8, 8, PanelHitGeometry::Rounded};
		Check(!rounded.Contains(0, 0) && rounded.Contains(8, 0) && rounded.Contains(4, 4)
			&& !rounded.Contains(100, 40), "page rounded geometry retains half-open edges and corners");
		PanelRegion tolerant{{10, 20, 110, 100}, 8, 8, PanelHitGeometry::RoundedWithPixelTolerance, 1.5};
		Check(tolerant.Contains(165, 90) && tolerant.Contains(165.0000005, 90)
			&& !tolerant.Contains(165.000002, 90) && !tolerant.Contains(15, 30),
			"attribute rounded geometry uses original pixel tolerance at fractional zoom");
		PanelRegion empty{{2, 2, 2, 4}}; Check(!empty.Contains(2, 3), "empty region owns no point");
		PanelRegion floatBounds{{-100000000, 0, 1, 80}, 0, 0, PanelHitGeometry::RoundedWithPixelTolerance, 1, true};
		Check(floatBounds.Contains(0, 40) && !floatBounds.Contains(.5, 40),
			"main shape hit preserves FLOAT extent rounding before DIP tolerance");
		PanelState panel; panel.SetCandidate(Frame()); panel.CommitPresented();
		const std::array<PanelFrame, 1> saved{panel.Presented()};
		panel.SetCandidate(Frame(200)); panel.CommitPresented();
		PanelStack stack{{0, &panel, 0, 0}};
		Check(stack.FrontPanel(10, 10, [&](const auto& entry) -> const PanelFrame& { return saved[entry.id]; }) == 0
			&& !stack.FrontPanel(10, 10), "copied member snapshot queries its own matching range");
	}
	void TestOwnerCoordinatesAndEligibility()
	{
		PanelState main, more, indicator;
		main.SetCandidate(Frame()); more.SetCandidate(Frame(200)); indicator.SetCandidate(Frame(400));
		main.CommitPresented(); more.CommitPresented(); indicator.CommitPresented();
		PanelStack stack{{1, &main, 2, 1}, {2, &more, 1, 2}, {3, &indicator, 3, 3}};
		// 主体、刚性叠层与原生直移提示沿用宿主各自的逆映射，输入优先级不依赖绘制顺序。
		const auto query = [&](bool eligible) {
			return stack.FrontPanelIf([&](const PanelStackEntry& entry) {
				if (entry.id == 3 && !eligible) return false;
				const double localX = entry.id == 1 ? 10 : entry.id == 2 ? 210 : 410;
				return entry.panel->Presented().OwnsPoint(localX, 10);
			});
		};
		Check(query(false) == 2 && query(true) == 3, "owner eligibility and coordinate mapping preserve input priority");
		more.SetCandidate(Frame(600));
		Check(query(false) == 2, "owner-specific query still uses successfully presented range");
		more.CommitPresented();
		Check(query(false) == 1, "confirmed mapped range change reveals the lower panel");
		PanelState moved; PanelStack movedStack{{4, &moved, 0, 0}};
		moved.SetCandidate(Frame()); moved.CommitPresented();
		moved.SetCandidate(Frame(200)); Check(moved.CaptureCandidate(1), "capture before native relocation");
		const auto relocated = Frame(400);
		moved.RebasePresentedBounds(relocated.visualBounds, relocated.interaction.bounds);
		Check(movedStack.FrontPanel(410, 10) == 4 && !movedStack.FrontPanel(10, 10)
			&& moved.Candidate().interaction.bounds.left == 200, "native relocation moves only the presented frame");
		Check(moved.ConfirmPresented(1) && movedStack.FrontPanel(210, 10) == 4,
			"native relocation leaves captured candidate acknowledgement intact");
	}
}

int RunUi3PanelStackTests()
{
	TestOrderingAndWhitespace(); TestCandidateAndSequence(); TestGeometryAndCopiedSnapshot();
	TestOwnerCoordinatesAndEligibility();
	return failures;
}
