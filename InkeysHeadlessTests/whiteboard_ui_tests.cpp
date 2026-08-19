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
			&& !first.previousEnabled && first.pageEnabled && first.nextEnabled,
			"first page disables previous while page and append buttons remain enabled");

		const auto switching = ResolvePageState(3, 5, true);
		Check(!switching.previousEnabled && !switching.pageEnabled
			&& !switching.nextEnabled,
			"switching disables all three page buttons");
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
			&& left.bounds.right - left.bounds.left == 230
			&& left.bounds.bottom - left.bounds.top == 80,
			"left control uses the MainBar 80 DIP height and twoTwo grid margin");
		Check(right.bounds.right == monitor.right - 5
			&& right.bounds.top == left.bounds.top,
			"right control mirrors against the monitor edge");
		Check(left.previous.left == 5 && left.previous.right == 75
			&& left.currentPage.left == 80 && left.currentPage.right == 150
			&& EqualRect(&left.currentPage, &left.totalPage)
			&& left.next.left == 155 && left.next.right == 225
			&& left.previous.top == 5 && left.currentPage.top == 5
			&& left.next.bottom == 75,
			"previous page and next share three equal twoTwo button bounds");

		const auto scaled = ResolveControlLayout(monitor, 1.5F, true);
		Check(scaled.bounds.right - scaled.bounds.left == 345
			&& scaled.bounds.bottom - scaled.bounds.top == 120,
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
			Check(!service.OverlayFullscreen(), "overlay defaults to non-fullscreen");
			(void)service.SetOverlayTopmost(false);
			Check(!service.OverlayTopmost(), "notopmost mode persists without a refresh target");
			(void)service.SetOverlayFullscreen(true);
			Check(service.OverlayFullscreen() && !service.OverlayTopmost(),
				"fullscreen mark persists independently of topmost");
			(void)service.SetOverlayFullscreen(false);
			(void)service.SetOverlayTopmost(true);
			Check(service.OverlayTopmost() && !service.OverlayFullscreen(),
				"topmost restore keeps fullscreen independently cleared");
	}
}

int RunWhiteboardUiTests()
{
	TestPageState();
	TestControlLayout();
	TestWindowContracts();
	return failureCount;
}
