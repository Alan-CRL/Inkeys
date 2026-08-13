#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <iostream>
#include <string_view>

import Inkeys.UI.Ppt;

namespace
{
	using namespace Inkeys::UI::Ppt;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	bool SameRect(const RECT& lhs, const RECT& rhs)
	{
		return lhs.left == rhs.left && lhs.top == rhs.top &&
			lhs.right == rhs.right && lhs.bottom == rhs.bottom;
	}

	void TestLayoutAndDpi()
	{
		constexpr RECT monitor{ -1920, 0, 0, 1080 };
		LayoutConfiguration config;
		config.bottomPairWidth = 100.0F;
		config.middlePairWidth = 80.0F;
		config.showMiddlePair = true;
		const auto bottomLeft = ResolveControlLayout(
			Control::BottomLeft, monitor, config, true);
		const auto bottomRight = ResolveControlLayout(
			Control::BottomRight, monitor, config, true);
		Check(bottomLeft.expanded.left - monitor.left ==
			monitor.right - bottomRight.expanded.right,
			"bottom pair mirrors around monitor center");
		Check(bottomLeft.backing.cx == 195 && bottomLeft.backing.cy == 60,
			"bottom backing uses legacy dimensions");

		const auto middleLeft = ResolveControlLayout(
			Control::MiddleLeft, monitor, config, false);
		const auto middleRight = ResolveControlLayout(
			Control::MiddleRight, monitor, config, false);
		const auto exit = ResolveControlLayout(Control::ExitShow, monitor, config, false);
		Check(middleLeft.hidden.right < monitor.left &&
			middleRight.hidden.left > monitor.right,
			"middle controls hide on their respective sides");
		Check(exit.hidden.top > monitor.bottom, "exit control hides below monitor");

		const auto scaled = ResolveControlLayout(
			Control::BottomLeft, monitor, config, true, 1.5F);
		Check(scaled.backing.cx == 293 && scaled.backing.cy == 90,
			"DPI changes physical backing size");
		auto movedConfig = config;
		movedConfig.bottomPairWidth += 40.0F;
		const auto moved = ResolveControlLayout(
			Control::BottomLeft, monitor, movedConfig, true);
		Check(moved.backing.cx == bottomLeft.backing.cx &&
			moved.backing.cy == bottomLeft.backing.cy &&
			moved.expanded.left != bottomLeft.expanded.left,
			"position change reuses backing size");
	}

	void TestAnimationAndDrag()
	{
		const float first = AdvanceLegacyValue(0.0F, 15.0F);
		Check(first > 0.0F && first < 15.0F,
			"legacy advancement converges monotonically");
		Check(AdvanceLegacyValue(14.95F, 15.0F) == 15.0F,
			"legacy advancement snaps near target");

		constexpr RECT monitor{ 0, 0, 1000, 800 };
		LayoutConfiguration config;
		config.bottomPairWidth = 10000.0F;
		config.bottomPairHeight = -10.0F;
		auto clamped = ClampPptDrag(Control::BottomLeft, monitor, config);
		Check(clamped.bottomPairWidth == 295.0F && clamped.bottomPairHeight == 0.0F,
			"bottom drag clamps to monitor boundaries");

		config.showBottomPair = true;
		config.showMiddlePair = false;
		config.showExit = true;
		config.bottomPairWidth = 0.0F;
		config.bottomPairHeight = 0.0F;
		config.exitWidth = 0.0F;
		config.exitHeight = 0.0F;
		Check(!PptDragCollides(Control::BottomLeft, monitor, config),
			"separated visible groups do not collide");
		config.exitWidth = -395.0F;
		Check(PptDragCollides(Control::BottomLeft, monitor, config),
			"overlapping visible groups collide");
		config.showExit = false;
		Check(!PptDragCollides(Control::BottomLeft, monitor, config),
			"hidden groups are ignored by collision checks");
	}

	void TestDamageTransactions()
	{
		constexpr SIZE backing{ 100, 60 };
		Check(SameRect(ResolvePptDamage(backing, {}, {}, true),
			RECT{ 0, 0, 100, 60 }), "full damage covers backing");
		Check(SameRect(ResolvePptDamage(backing,
			RECT{ -5, 4, 20, 30 }, RECT{ 15, 10, 120, 50 }, false),
			RECT{ 0, 4, 100, 50 }), "local damage unions and clips visuals");

		const auto active = ResolvePptDiagnosticDamage(backing,
			RECT{ 10, 10, 30, 30 }, {}, false, true, true, false);
		Check(active.drawActive && !active.drawFinal && active.keepFinalFrame &&
			SameRect(active.damage, RECT{ 10, 10, 30, 30 }),
			"active diagnostic draws red local damage");
		const auto final = ResolvePptDiagnosticDamage(backing,
			{}, active.damage, false, true, false, active.keepFinalFrame);
		Check(!final.drawActive && final.drawFinal && !final.keepFinalFrame &&
			SameRect(final.damage, active.damage),
			"idle transition draws one green final damage");
	}
}

int RunPptUiTests()
{
	TestLayoutAndDpi();
	TestAnimationAndDrag();
	TestDamageTransactions();
	return failureCount;
}
