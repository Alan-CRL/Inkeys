#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <iostream>
#include <string_view>

#include "../Inkeys/Inkeys/UI/Bar/Bar.DisplayTransition.h"

import Inkeys.UI.Bar.Animation;

namespace
{
	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}
}

int RunBarDisplayTransitionTests()
{
	using namespace Inkeys::UI::Bar;
	const RECT oldMonitor{ 100, 50, 2020, 1130 };
	const RECT sameSize{ -1920, 0, 0, 1080 };
	const auto preserved = ResolveBarDisplayPlacement(
		oldMonitor, sameSize, 1000.0, 500.0, 50.0, 50.0);
	Check(preserved.startLocalCenterX == 3020.0 &&
		preserved.targetLocalCenterX == 1000.0,
		"origin change starts at the same screen coordinate");
	Check(sameSize.left + preserved.startLocalCenterX ==
		oldMonitor.left + 1000.0 && sameSize.top + preserved.startLocalCenterY ==
		oldMonitor.top + 500.0,
		"negative monitor origin keeps the first animated frame continuous");

	const RECT smaller{ 0, 0, 800, 600 };
	const auto clamped = ResolveBarDisplayPlacement(
		RECT{ 0, 0, 1920, 1080 }, smaller, 1800.0, 1000.0, 60.0, 60.0);
	Check(clamped.targetLocalCenterX == 740.0 &&
		clamped.targetLocalCenterY == 540.0,
		"resolution shrink clamps button to nearest visible center");
	const auto dpiExpanded = ResolveBarDisplayPlacement(
		smaller, smaller, 760.0, 560.0, 90.0, 90.0);
	Check(dpiExpanded.targetLocalCenterX == 710.0 &&
		dpiExpanded.targetLocalCenterY == 510.0,
		"DPI growth clamps the enlarged button to the nearest visible center");
	Check(std::abs(EaseBarDisplayTransition(0.25) - 0.0625) < 0.000001 &&
		std::abs(EaseBarDisplayTransition(0.5) - 0.5) < 0.000001,
		"display transition uses ease-in-out cubic");
	for (bool docked : { false, true })
		for (bool dragging : { false, true })
			for (bool recovering : { false, true })
				for (bool desktop : { false, true })
					for (bool pending : { false, true })
						for (bool scene : { false, true })
							Check(CanAnimateBarTaskbarDockY(docked, dragging, recovering, desktop, pending, scene)
								== (docked && !dragging && !recovering && desktop && !pending && !scene),
								"taskbar animation respects drag/recovery/floating/PPT/whiteboard ownership");
	BarUiValueClass x(617.0), y(1034.5);
	Check(RetargetBarTaskbarDockY(y, 991.5, 1034.5) && y.val == 1034.5,
		"taskbar rise starts at successful screen Y");
	(void)BarUiAdvanceAnimation(y, BarUiAnimationAdvanceContextClass{ .1, 1.0, true, false });
	const double before = y.val;
	Check(!RetargetBarTaskbarDockY(y, 991.5, 1034.5) && y.val == before,
		"repeated taskbar sample neither resets Y nor restarts animation");
	(void)BarUiAdvanceAnimation(y, BarUiAnimationAdvanceContextClass{ .1, 1.0, true, false });
	Check(y.val < before, "same target continues its existing animation");
	const double lastPresented = before + 2.0;
	Check(RetargetBarTaskbarDockY(y, 1034.5, lastPresented) && y.val == lastPresented,
		"rapid reverse starts from last presented Y rather than uncommitted candidate");
	(void)BarUiAdvanceAnimation(y, BarUiAnimationAdvanceContextClass{ .1, 1.0, false, false });
	Check(y.IsSame() && y.val == 1034.5 && x.val == 617.0 && x.tar == 617.0,
		"animation off settles immediately and taskbar transition changes only Y");
	return failureCount;
}
