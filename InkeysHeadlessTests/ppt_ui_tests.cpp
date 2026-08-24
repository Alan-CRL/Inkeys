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
		Check(bottomLeft.backing.cx == 165 && bottomLeft.backing.cy == 43,
			"bottom backing uses compact bar dimensions");

		const auto middleLeft = ResolveControlLayout(
			Control::MiddleLeft, monitor, config, false);
		const auto middleRight = ResolveControlLayout(
			Control::MiddleRight, monitor, config, false);
		Check(middleLeft.hidden.right < monitor.left &&
			middleRight.hidden.left > monitor.right,
			"middle controls hide on their respective sides");
		Check(SameRect(bottomLeft.expanded, bottomLeft.hidden),
			"bottom controls fade at their target position");

		const auto scaled = ResolveControlLayout(
			Control::BottomLeft, monitor, config, true, 1.5F);
		Check(scaled.backing.cx == 248 && scaled.backing.cy == 64,
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
		Check(clamped.bottomPairWidth == 330.0F && clamped.bottomPairHeight == 0.0F,
			"bottom drag clamps to monitor boundaries");

		config.showBottomPair = true;
		config.showMiddlePair = true;
		config.bottomPairWidth = 0.0F;
		config.bottomPairHeight = 0.0F;
		config.middlePairHeight = 0.0F;
		Check(!PptDragCollides(Control::BottomLeft, monitor, config),
			"separated visible groups do not collide");
		config.middlePairHeight = -312.5F;
		Check(PptDragCollides(Control::BottomLeft, monitor, config),
			"overlapping visible groups collide");
		config.showMiddlePair = false;
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
		Check(bottomLeft.dragHandle.x1 == 10.0F && bottomLeft.dragHandle.x2 == 10.0F &&
			bottomLeft.dragHandle.y1 == 11.25F && bottomLeft.dragHandle.y2 == 31.25F,
			"bottom-left uses compact vertical drag handle");
		Check(bottomRight.dragHandle.x1 == 155.0F &&
			bottomRight.dragHandle.x2 == 155.0F,
			"bottom-right drag handle mirrors at trailing edge");
		Check(middleLeft.dragHandle.y1 == 10.0F && middleLeft.dragHandle.y2 == 10.0F &&
			middleLeft.dragHandle.x1 == 11.25F && middleLeft.dragHandle.x2 == 31.25F,
			"middle controls use compact horizontal drag handle");

		Check(middleLeft.previous.top == 15.0F && middleLeft.previous.bottom == 47.5F &&
			middleLeft.next.top == 127.5F && middleLeft.next.bottom == 160.0F,
			"middle buttons keep balanced outer spacing");
		Check(middleLeft.currentPage.top == 52.5F &&
			middleLeft.currentPage.bottom == 87.5F &&
			middleLeft.totalPage.top == 87.5F &&
			middleLeft.totalPage.bottom == 122.5F,
			"middle page text uses compact centered bounds");
		Check(IsInPageHitArea(Control::BottomLeft, 52.5F, 0.0F) &&
			IsInPageHitArea(Control::BottomLeft, 122.5F, 42.5F) &&
			!IsInPageHitArea(Control::BottomLeft, 52.0F, 30.0F),
			"bottom page hit area keeps full control height");
		Check(IsInPageHitArea(Control::MiddleLeft, 0.0F, 52.5F) &&
			IsInPageHitArea(Control::MiddleLeft, 42.5F, 122.5F) &&
			!IsInPageHitArea(Control::MiddleLeft, 30.0F, 123.0F),
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

	void TestDisplayReflowAndTransition()
	{
		const RECT monitor{ -800, 0, 0, 600 };
		LayoutConfiguration config;
		config.bottomPairWidth = 10000.0F;
		config.bottomPairHeight = -100.0F;
		config.middlePairWidth = 10000.0F;
		config.middlePairHeight = 10000.0F;
		config.showMiddlePair = true;
		config.bottomPairScale = 2.0F;
		config.middlePairScale = 2.0F;
		const auto runtime = ResolveRuntimeLayoutConfiguration(
			monitor, config, 1.5F);
		for (const auto control : { Control::BottomLeft, Control::BottomRight,
			Control::MiddleLeft, Control::MiddleRight })
		{
			const auto layout = ResolveControlLayout(control, monitor,
				runtime.configuration, true, runtime.dpiScale);
			Check(layout.expanded.left >= monitor.left &&
				layout.expanded.top >= monitor.top &&
				layout.expanded.right <= monitor.right &&
				layout.expanded.bottom <= monitor.bottom,
				"display reflow keeps enabled PPT control on screen");
		}
		Check(EasePptDisplayTransition(0.25F) == 0.0625F &&
			EasePptDisplayTransition(0.5F) == 0.5F,
			"PPT display transition uses ease-in-out cubic");
		const float midpoint = InterpolatePptDisplayValue(100.0F, 300.0F, 0.5F);
		Check(midpoint == 200.0F &&
			InterpolatePptDisplayValue(midpoint, 400.0F, 0.0F) == midpoint,
			"PPT display retarget starts from current rendered value");

		const RECT tinyMonitor{ -180, -60, 0, 60 };
		LayoutConfiguration extreme;
		extreme.bottomPairWidth = 10000.0F;
		extreme.bottomPairHeight = 10000.0F;
		extreme.middlePairWidth = 10000.0F;
		extreme.middlePairHeight = 10000.0F;
		extreme.bottomPairScale = 3.0F;
		extreme.middlePairScale = 3.0F;
		extreme.showMiddlePair = true;
		const auto original = extreme;
		const auto fitted = ResolveRuntimeLayoutConfiguration(
			tinyMonitor, extreme, 1.5F);
		for (const auto control : { Control::BottomLeft, Control::BottomRight,
			Control::MiddleLeft, Control::MiddleRight })
		{
			const auto layout = ResolveControlLayout(control, tinyMonitor,
				fitted.configuration, true, fitted.dpiScale);
			Check(layout.expanded.left >= tinyMonitor.left &&
				layout.expanded.top >= tinyMonitor.top &&
				layout.expanded.right <= tinyMonitor.right &&
				layout.expanded.bottom <= tinyMonitor.bottom,
				"extreme monitor uses runtime-only group fitting");
		}
		Check(!PptDragCollides(Control::MiddleLeft, tinyMonitor,
				fitted.configuration, fitted.dpiScale),
			"lower-priority side group is corrected after collisions");
		Check(extreme.bottomPairWidth == original.bottomPairWidth &&
			extreme.middlePairScale == original.middlePairScale,
			"runtime correction does not mutate persisted configuration input");
	}
}

int RunPptUiTests()
{
	TestLayoutAndDpi();
	TestAnimationAndDrag();
	TestDamageTransactions();
	TestVisualGeometryAndPageText();
	TestDisplayReflowAndTransition();
	return failureCount;
}
