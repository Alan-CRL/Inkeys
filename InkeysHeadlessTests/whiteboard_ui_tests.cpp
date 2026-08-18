#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <iostream>
#include <string_view>

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
			&& !first.previousEnabled && first.nextEnabled,
			"first page disables previous and keeps append enabled");

		const auto switching = ResolvePageState(3, 5, true);
		Check(!switching.previousEnabled && !switching.nextEnabled,
			"switching disables both page buttons");
		const auto clamped = ResolvePageState(9, 4, false);
		Check(clamped.currentPage == 4 && clamped.totalPage == 4
			&& clamped.previousEnabled && clamped.nextEnabled,
			"page state clamps invalid runtime values");
	}

	void TestControlLayout()
	{
		using Inkeys::UI::Whiteboard::ResolveControlLayout;
		constexpr RECT monitor{ -1920, 0, 0, 1080 };
		const auto left = ResolveControlLayout(monitor, 1.0F, true);
		const auto right = ResolveControlLayout(monitor, 1.0F, false);
		Check(left.bounds.left == monitor.left + 5
			&& left.bounds.bottom == monitor.bottom - 5
			&& left.bounds.right - left.bounds.left == 195
			&& left.bounds.bottom - left.bounds.top == 60,
			"left control uses fixed 195x60 DIP and five DIP margin");
		Check(right.bounds.right == monitor.right - 5
			&& right.bounds.top == left.bounds.top,
			"right control mirrors against the monitor edge");
		Check(left.currentPage.top < left.totalPage.top
			&& left.currentPage.bottom <= left.totalPage.bottom,
			"current page occupies the stronger upper text tier");

		const auto scaled = ResolveControlLayout(monitor, 1.5F, true);
		Check(scaled.bounds.right - scaled.bounds.left == 293
			&& scaled.bounds.bottom - scaled.bounds.top == 90,
			"layout converts DIP to physical pixels at monitor DPI");
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
		Check(service.OverlayTopmost(), "overlay defaults to topmost mode");
		(void)service.SetOverlayTopmost(false);
		Check(!service.OverlayTopmost(), "notopmost mode persists without a refresh target");
		(void)service.SetOverlayTopmost(true);
		Check(service.OverlayTopmost(), "topmost mode restores persistently");
	}
}

int RunWhiteboardUiTests()
{
	TestPageState();
	TestControlLayout();
	TestWindowContracts();
	return failureCount;
}
