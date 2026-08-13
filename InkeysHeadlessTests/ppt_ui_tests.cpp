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

	void TestVisualGeometryAndPageText()
	{
		const auto bottomLeft = ResolveControlVisualGeometry(Control::BottomLeft);
		const auto bottomRight = ResolveControlVisualGeometry(Control::BottomRight);
		const auto middleLeft = ResolveControlVisualGeometry(Control::MiddleLeft);
		const auto exit = ResolveControlVisualGeometry(Control::ExitShow);
		Check(bottomLeft.dragHandle.x1 == 8.0F && bottomLeft.dragHandle.x2 == 8.0F &&
			bottomLeft.dragHandle.y1 == 15.0F && bottomLeft.dragHandle.y2 == 45.0F,
			"bottom-left restores vertical drag handle");
		Check(bottomRight.dragHandle.x1 == 187.0F &&
			bottomRight.dragHandle.x2 == 187.0F,
			"bottom-right drag handle mirrors at trailing edge");
		Check(middleLeft.dragHandle.y1 == 8.0F && middleLeft.dragHandle.y2 == 8.0F &&
			middleLeft.dragHandle.x1 == 15.0F && middleLeft.dragHandle.x2 == 45.0F,
			"middle controls restore horizontal drag handle");
		Check(exit.dragHandle.x1 == 8.0F && exit.dragHandle.x2 == 8.0F,
			"exit control restores vertical drag handle");

		Check(middleLeft.previous.top == 15.0F && middleLeft.previous.bottom == 65.0F &&
			middleLeft.next.top == 130.0F && middleLeft.next.bottom == 180.0F,
			"middle buttons keep balanced outer spacing");
		Check(middleLeft.currentPage.top == 70.0F &&
			middleLeft.currentPage.bottom == 110.0F &&
			middleLeft.totalPage.top == 100.0F &&
			middleLeft.totalPage.bottom == 125.0F,
			"middle page text uses legacy centered bounds");
		Check(IsInPageHitArea(Control::BottomLeft, 70.0F, 0.0F) &&
			IsInPageHitArea(Control::BottomLeft, 120.0F, 60.0F) &&
			!IsInPageHitArea(Control::BottomLeft, 69.0F, 30.0F),
			"bottom page hit area keeps full control height");
		Check(IsInPageHitArea(Control::MiddleLeft, 0.0F, 70.0F) &&
			IsInPageHitArea(Control::MiddleLeft, 60.0F, 125.0F) &&
			!IsInPageHitArea(Control::MiddleLeft, 30.0F, 126.0F),
			"middle page hit area keeps full control width");

		const auto unknown = ResolvePageText(Control::BottomLeft, -1, -1);
		Check(unknown.current == L"-" && unknown.total == L"/-",
			"unknown page values use placeholders");
		const auto bottomLimit = ResolvePageText(Control::BottomRight, 12345, 56789);
		Check(bottomLimit.current == L"9999" && bottomLimit.total == L"/9999",
			"bottom page values retain four-digit cap");
		const auto middleLimit = ResolvePageText(Control::MiddleRight, 1234, 5678);
		Check(middleLimit.current == L"999" && middleLimit.total == L"/999",
			"middle page values retain three-digit cap");
	}
}

int RunPptUiTests()
{
	TestLayoutAndDpi();
	TestAnimationAndDrag();
	TestDamageTransactions();
	TestVisualGeometryAndPageText();
	return failureCount;
}
