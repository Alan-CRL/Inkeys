#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <iostream>
#include <string_view>

import Inkeys.UI.Bar.SurfaceLayout;
import Inkeys.UI.Whiteboard;
import Inkeys.Window;

namespace
{
	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL whiteboard: " << name << '\n';
	}

	void TestPageState()
	{
		using Inkeys::UI::Whiteboard::ResolvePageState;
		const auto first = ResolvePageState(1, 1, false);
		Check(first.currentPage == 1 && first.totalPage == 1
			&& !first.previousEnabled && first.pageEnabled && first.nextEnabled
			&& first.nextIsAdd,
			"first page disables previous while page and append buttons remain enabled");
		const auto changing = ResolvePageState(3, 5, true);
		Check(!changing.previousEnabled && !changing.pageEnabled
			&& !changing.nextEnabled && !changing.nextIsAdd,
			"switching disables all page buttons");
		const auto clamped = ResolvePageState(9, 4, false);
		Check(clamped.currentPage == 4 && clamped.totalPage == 4
			&& clamped.previousEnabled && clamped.nextEnabled
			&& clamped.nextIsAdd,
			"page state clamps invalid runtime values");
	}

	void TestSceneLayout()
	{
		using namespace Inkeys::UI::Bar;
		constexpr RECT monitor{ -1920, 0, 0, 1080 };
		BarSurfaceHorizontalGroupSpec group;
		group.screenBounds = monitor;
		group.dpiScale = 1.0F;
		group.anchor = BarSurfaceHorizontalAnchor::Left;
		const auto left = ResolveBarSurfaceHorizontalGroupLayout(group, 3);
		group.anchor = BarSurfaceHorizontalAnchor::Right;
		const auto right = ResolveBarSurfaceHorizontalGroupLayout(group, 3);
		Check(left.logicalBounds.left == monitor.left + 5
			&& left.logicalBounds.bottom == monitor.bottom - 5
			&& left.logicalBounds.right - left.logicalBounds.left == 230
			&& left.logicalBounds.bottom - left.logicalBounds.top == 80,
			"UI3 group owns the standard twoTwo logical layout and anchoring");
		Check(right.logicalBounds.right == monitor.right - 5
			&& right.logicalBounds.top == left.logicalBounds.top,
			"right UI3 surface mirrors against the monitor edge");
		Check(left.widgets.size() == 3
			&& left.widgets[0].left == 5 && left.widgets[0].right == 75
			&& left.widgets[1].left == 80 && left.widgets[1].right == 150
			&& left.widgets[2].left == 155 && left.widgets[2].right == 225,
			"UI3 layout creates three equal twoTwo widget slots");
		group.dpiScale = 1.5F;
		const auto scaled = ResolveBarSurfaceHorizontalGroupLayout(group, 3);
		Check(scaled.logicalBounds.right - scaled.logicalBounds.left == 345
			&& scaled.logicalBounds.bottom - scaled.logicalBounds.top == 120,
			"shared DIP metrics resolve independently at target DPI");
	}

	void TestSurfaceLayoutContracts()
	{
		using namespace Inkeys::UI::Bar;
		BarSurfaceHorizontalGroupSpec group;
		Check(group.edgeMarginDip == BarButtonGapDip
			&& group.bottomMarginDip == BarButtonGapDip
			&& group.itemSizeDip == BarButtonTwoSideDip
			&& group.gapDip == BarButtonGapDip,
			"surface layout defaults to the shared Bar twoTwo metrics");
		Check(group.anchor == BarSurfaceHorizontalAnchor::Left,
			"surface anchor remains explicit instead of relying on Whiteboard-local geometry");
	}

	void TestWindowContracts()
	{
		using Inkeys::Window::WindowRole;
		Check(static_cast<unsigned>(WindowRole::WhiteboardLeft)
			== static_cast<unsigned>(WindowRole::Drawpad) + 1
			&& static_cast<unsigned>(WindowRole::WhiteboardRight)
			== static_cast<unsigned>(WindowRole::WhiteboardLeft) + 1
			&& static_cast<unsigned>(WindowRole::PptBottomLeft)
			== static_cast<unsigned>(WindowRole::WhiteboardRight) + 1,
			"whiteboard roles stay between drawpad and PPT roles");
		Inkeys::Window::Service service;
		Check(service.OverlayTopmost() && !service.OverlayFullscreen(),
			"overlay defaults to topmost and non-fullscreen");
		(void)service.SetOverlayTopmost(false);
		(void)service.SetOverlayFullscreen(true);
		Check(service.OverlayFullscreen() && !service.OverlayTopmost(),
			"fullscreen mark persists independently of topmost");
	}
}

int RunWhiteboardUiTests()
{
	TestPageState();
	TestSceneLayout();
	TestSurfaceLayoutContracts();
	TestWindowContracts();
	return failureCount;
}
