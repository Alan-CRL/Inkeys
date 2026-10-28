#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <iostream>
#include <string_view>

#include "../Inkeys/Inkeys/UI/Whiteboard/Whiteboard.BackgroundState.h"

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

	void TestBackgroundInvalidation()
	{
		using Inkeys::UI::WhiteboardDetail::BackgroundState;
		BackgroundState background;
		background.Reset();
		(void)background.Publish(true, 3);
		const BackgroundState::Frame frame{background.Desired(),
			reinterpret_cast<HWND>(1), 7, {-1920, -1080, 0, 0}};
		Check(background.Prepare(frame), "initial background pixels can be prepared");
		background.Invalidate();
		Check(!background.Prepare(frame) && !background.Prepared(3),
			"late native ACK cannot revive pixels retired by theme or display invalidation");
	}

	void TestBackgroundNativeReceipts()
	{
		using namespace Inkeys::UI::WhiteboardDetail;
		using namespace Inkeys::Window;
		BackgroundState background;
		background.Reset();
		(void)background.Publish(true, 11);
		const BackgroundState::Surface surface{reinterpret_cast<HWND>(1), 7};
		Check(background.Bind(background.Desired(), surface), "background candidate binds its native generation");
		const BackgroundState::Frame frame{background.Desired(), surface.hwnd, surface.generation,
			{-1920, -1080, 0, 0}, background.ContentRevision(), 9};
		WindowCandidateReceipt receipt;
		receipt.revision = 41;
		receipt.frameSerial = frame.frameSerial;
		receipt.workspaceRevision = frame.request.revision;
		receipt.count = 1;
		receipt.completedMask = 1;
		receipt.actual[0] = {WindowRole::Freeze, surface.hwnd, surface.generation,
			frame.bounds, false, false, true, 0};
		const auto matches = [&](const auto& candidate, bool committed = true, bool visible = false)
		{ return BackgroundFrameMatchesReceipt(frame, 41, candidate, committed, visible); };
		Check(!background.Prepared(11) && !matches(receipt, false),
			"pixels and queued native work do not publish BackgroundPrepared");
		Check(matches(receipt) && !matches(receipt, true, true),
			"hidden matching pixels may prepare but cannot acknowledge active visibility");
		for (int mismatch = 0; mismatch != 8; ++mismatch)
		{
			auto stale = receipt;
			switch (mismatch)
			{
			case 0: ++stale.revision; break;
			case 1: ++stale.frameSerial; break;
			case 2: ++stale.workspaceRevision; break;
			case 3: stale.completedMask = 0; break;
			case 4: stale.count = 2; break;
			case 5: ++stale.actual[0].generation; break;
			case 6: --stale.actual[0].bounds.left; break;
			case 7: stale.actual[0].known = false; break;
			}
			Check(!matches(stale) && !background.Prepared(11),
				"stale, partial, foreign-generation or unknown native receipts cannot prepare pixels");
		}
		Check(background.Prepare(frame) && background.FrameCurrent(frame),
			"fully matched native and pixel identities can publish preparation");
		auto pixelFrame = frame;
		pixelFrame.presentationRevision = 5;
		receipt.actual[0].presentationRevision = 6;
		Check(!BackgroundFrameMatchesReceipt(pixelFrame, 41, receipt, true, false),
			"native receipt for another pixel transaction cannot prepare background");
		receipt.actual[0].presentationRevision = 5;
		Check(BackgroundFrameMatchesReceipt(pixelFrame, 41, receipt, true, false),
			"native receipt binds exact successful pixel transaction");
		receipt.actual[0].visible = true;
		Check(matches(receipt, true, true), "owner complete visibility permits active acknowledgement");
		receipt.actual[0].iconic = true;
		Check(!matches(receipt, true, true), "minimized native background never acknowledges active");
		background.Invalidate();
		Check(!background.FrameCurrent(frame) && !background.Prepare(frame),
			"theme, DPI or device invalidation rejects an otherwise exact late native receipt");
		background.RetireHeld(surface);
		Check(background.Bind(background.Desired(), {surface.hwnd, 8}) && !background.FrameCurrent(frame),
			"same HWND recreated with new generation cannot reuse old pixel ACK");
		(void)background.Publish(false, 12);
		const auto closing = background.Desired();
		const BackgroundState::Surface closingSurface{surface.hwnd, 8};
		receipt.workspaceRevision = closing.revision;
		receipt.actual[0].generation = closingSurface.generation;
		receipt.actual[0].visible = false;
		Check(!BackgroundInactiveMatchesReceipt(closing, closingSurface, frame.frameSerial, 41, receipt, false)
			&& !background.InactiveCommitted(closing), "inactive ACK waits for committed native Hide");
		Check(BackgroundInactiveMatchesReceipt(closing, closingSurface, frame.frameSerial, 41, receipt, true),
			"actual Hide acknowledges inactive even while minimized");
		receipt.actual[0].visible = true;
		Check(!BackgroundInactiveMatchesReceipt(closing, closingSurface, frame.frameSerial, 41, receipt, true),
			"reported completion with visible native surface cannot acknowledge Hide");
		(void)background.Publish(true, 13);
		Check(!background.CommitInactive(closing) && !background.Prepare(frame),
			"late inactive or active ACK after reversal cannot confirm a newer request");
	}

	void TestPageState()
	{
		using Inkeys::UI::Whiteboard::PageStateTransaction;
		using Inkeys::UI::Whiteboard::ResolvePageState;
		const auto first = ResolvePageState(1, 1, false);
		Check(first.currentPage == 1 && first.totalPage == 1
			&& !first.previousEnabled && first.pageEnabled && first.nextEnabled
			&& first.nextIsAdd,
			"first page disables previous while page and append buttons remain enabled");
		const auto changing = ResolvePageState(3, 5, true);
		Check(changing.previousEnabled && changing.pageEnabled
			&& changing.nextEnabled && !changing.previousInteractive
			&& !changing.pageInteractive && !changing.nextInteractive
			&& !changing.nextIsAdd,
			"switching locks input without changing page button visuals");
		const auto appending = ResolvePageState(1, 1, true);
		Check(appending.nextEnabled && !appending.nextInteractive
			&& appending.nextIsAdd,
			"appending keeps the add-page content while the request is in flight");
		const auto latchedArrow = ResolvePageState(3, 3, true, false);
		Check(latchedArrow.nextIsAdd == false,
			"latched next visual stays an arrow during a page transaction");
		const auto latchedAdd = ResolvePageState(3, 4, true, true);
		Check(latchedAdd.nextIsAdd,
			"latched add visual ignores intermediate page totals");
		const auto clamped = ResolvePageState(9, 4, false);
		Check(clamped.currentPage == 4 && clamped.totalPage == 4
			&& clamped.previousEnabled && clamped.nextEnabled
			&& clamped.nextIsAdd,
			"page state clamps invalid runtime values");

		PageStateTransaction append;
		(void)append.Publish(3, 3, false);
		const auto appendStart = append.Publish(3, 4, true);
		const auto appendMoved = append.Publish(4, 4, true);
		const auto appendDone = append.Publish(4, 4, false);
		Check(appendStart.nextIsAdd && appendMoved.nextIsAdd && appendDone.nextIsAdd,
			"append visual stays stable through a page transaction");

		PageStateTransaction toLast;
		(void)toLast.Publish(2, 3, false);
		const auto arrowDuringSwitch = toLast.Publish(3, 3, true);
		const auto addAfterSwitch = toLast.Publish(3, 3, false);
		Check(!arrowDuringSwitch.nextIsAdd && addAfterSwitch.nextIsAdd,
			"last-page arrow changes to add only after switching settles");

		PageStateTransaction fromFirst;
		(void)fromFirst.Publish(1, 3, false);
		const auto disabledDuringSwitch = fromFirst.Publish(2, 3, true);
		const auto enabledAfterSwitch = fromFirst.Publish(2, 3, false);
		Check(!disabledDuringSwitch.previousEnabled &&
			enabledAfterSwitch.previousEnabled,
			"previous visual stays disabled while leaving the first page");

		PageStateTransaction initialSwitch;
		const auto firstSwitch = initialSwitch.Publish(2, 5, true);
		Check(firstSwitch.currentPage == 2 && firstSwitch.totalPage == 5 &&
			firstSwitch.previousEnabled && !firstSwitch.nextIsAdd &&
			!firstSwitch.previousInteractive && !firstSwitch.pageInteractive &&
			!firstSwitch.nextInteractive,
			"first switching publish uses the real page instead of 1/1");
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
		using Inkeys::Window::OverlayActivationMode;
		using Inkeys::Window::ResolveOverlayActivationStyle;
		Check(static_cast<unsigned>(WindowRole::PptBottomLeft)
			== static_cast<unsigned>(WindowRole::Drawpad) + 1
			&& static_cast<unsigned>(WindowRole::PptBottomRight)
			== static_cast<unsigned>(WindowRole::PptBottomLeft) + 1,
			"shared bottom page hosts follow drawpad without duplicate whiteboard roles");
		using Inkeys::Window::RequiresOverlayActivationStyleTransition;
		Check(!RequiresOverlayActivationStyleTransition(WindowRole::PptBottomLeft,
			OverlayActivationMode::Presentation,
			OverlayActivationMode::Whiteboard)
			&& !RequiresOverlayActivationStyleTransition(WindowRole::Bar,
				OverlayActivationMode::Presentation,
				OverlayActivationMode::Whiteboard),
			"PageControl hosts and Bar stay visible across Whiteboard style changes");
		Check(RequiresOverlayActivationStyleTransition(WindowRole::Freeze,
			OverlayActivationMode::Presentation,
			OverlayActivationMode::Whiteboard)
			&& RequiresOverlayActivationStyleTransition(WindowRole::Drawpad,
				OverlayActivationMode::Presentation,
				OverlayActivationMode::Whiteboard),
			"only activation-bearing Whiteboard windows require style transitions");
		Inkeys::Window::Service service;
		Check(service.OverlayTopmost() && !service.OverlayFullscreen(),
			"overlay defaults to topmost and non-fullscreen");
		(void)service.SetOverlayTopmost(false);
		(void)service.SetOverlayFullscreen(true);
		Check(service.OverlayFullscreen() && !service.OverlayTopmost(),
			"fullscreen mark persists independently of topmost");
		const auto whiteboardFreeze = ResolveOverlayActivationStyle(
			WindowRole::Freeze, OverlayActivationMode::Whiteboard);
		const auto whiteboardDrawpad = ResolveOverlayActivationStyle(
			WindowRole::Drawpad, OverlayActivationMode::Whiteboard);
		const auto presentationDrawpad = ResolveOverlayActivationStyle(
			WindowRole::Drawpad, OverlayActivationMode::Presentation);
		Check((whiteboardFreeze.setExStyle & WS_EX_APPWINDOW) != 0 &&
			(whiteboardFreeze.clearExStyle & WS_EX_NOACTIVATE) != 0 &&
			whiteboardFreeze.taskbarAnchor && whiteboardFreeze.acceptsActivation,
			"Freeze is the only Whiteboard taskbar and activation anchor");
		Check((whiteboardDrawpad.setExStyle & WS_EX_TOOLWINDOW) != 0 &&
			(whiteboardDrawpad.clearExStyle & WS_EX_NOACTIVATE) != 0 &&
			whiteboardDrawpad.acceptsActivation,
			"Whiteboard Drawpad accepts activation without a taskbar button");
		Check((presentationDrawpad.setExStyle & WS_EX_NOACTIVATE) != 0 &&
			(presentationDrawpad.clearExStyle & WS_EX_APPWINDOW) != 0 &&
			!presentationDrawpad.acceptsActivation,
			"Presentation overlays remain no-activate and tool-window only");
	}
}

int RunWhiteboardUiTests()
{
	TestBackgroundInvalidation();
	TestBackgroundNativeReceipts();
	TestPageState();
	TestSceneLayout();
	TestSurfaceLayoutContracts();
	TestWindowContracts();
	return failureCount;
}
