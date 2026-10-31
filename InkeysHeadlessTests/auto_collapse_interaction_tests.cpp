#include "../Inkeys/Inkeys/UI/Bar/Bar.AutoCollapseInteraction.h"

#include <iostream>

int RunAutoCollapseInteractionTests()
{
	using namespace Inkeys::UI::Bar;
	int failures = 0;
	auto check = [&](bool value, const char* label)
	{
		if (!value) { ++failures; std::cerr << "[AutoCollapseInteraction] " << label << '\n'; }
	};
	OutsideCollapsePending pending;
	check(!pending.Take(1000, 1, true), "idle does not collapse");
	pending.Arm(1000, 1);
	check(!pending.Take(1099, 1, true), "outside click waits 100ms");
	check(pending.Take(1100, 1, true), "deadline collapses once");
	check(!pending.Take(1101, 1, true), "deadline is consumed");
	pending.Arm(2000, 2);
	pending.Cancel();
	check(!pending.Take(2200, 2, true), "own-window down cancels pending click");
	pending.Arm(3000, 3);
	check(!pending.Take(3200, 4, true), "mode dock workspace policy or input revision rejects old candidate");
	check(!pending.Take(3201, 3, true), "ABA does not revive cancelled candidate");
	pending.Arm(4000, 5);
	check(!pending.Take(4100, 5, false), "disabled policy cancels pending click");
	check(!pending.Take(4200, 5, true), "reenable does not replay old click");
	pending.Arm(5000, 6);
	pending.Arm(5050, 6);
	check(!pending.Take(5100, 6, true) && pending.Take(5150, 6, true),
		"new outside click replaces previous deadline");

	for (const int policy : { 0, 1, 2 })
	{
		DragCollapseGate gate;
		check(!gate.Take(policy, BarBottomDockMode::Floating, false), "down and click slop never trigger drag collapse");
		const bool bottom = gate.Take(policy, BarBottomDockMode::BottomDocked, true);
		check(bottom == (policy == 2), "centered/free bottom use same policy");
		const bool floating = gate.Take(policy, BarBottomDockMode::Floating, true);
		check(floating == (policy == 1), "non-bottom policy triggers after real detachment");
		check(!gate.Take(policy, BarBottomDockMode::Floating, true), "one drag never folds twice");
	}
	DragCollapseGate changedPolicy;
	check(!changedPolicy.Take(0, BarBottomDockMode::Floating, true)
		&& changedPolicy.Take(2, BarBottomDockMode::Floating, true),
		"policy change applies to subsequent actual drag input");
	return failures;
}
