#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <iostream>
#include <string_view>

#include "../Inkeys/Inkeys/UI/Bar/Bar.A2.h"

import Inkeys.UI.PageControl;

namespace
{
	using namespace Inkeys::UI::PageControl;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL page-control " << name << '\n';
	}

	LONG Width(const RECT& bounds) noexcept
	{
		return bounds.right - bounds.left;
	}

	LONG Height(const RECT& bounds) noexcept
	{
		return bounds.bottom - bounds.top;
	}

	void TestCompactWidgetContracts()
	{
		const auto left = ResolvePptWidgetContracts(Surface::BottomLeft);
		const auto right = ResolvePptWidgetContracts(Surface::BottomRight);
		const auto side = ResolvePptWidgetContracts(Surface::MiddleLeft);
		Check(left[0].role == PptWidgetRole::DragHandle
			&& left[0].dragSource && !left[0].hoverVisual
			&& !left[0].pressVisual && !left[0].invokesBusinessAction,
			"drag handle is the only visual-free drag source");
		Check(!left[1].dragSource && !left[2].dragSource && !left[3].dragSource,
			"buttons never act as drag sources");
		Check(!left[1].displaysText && !left[3].displaysText
			&& left[1].invokesBusinessAction && left[3].invokesBusinessAction,
			"compact arrows contain no labels and keep page actions");
		Check(left[2].displaysText && !left[2].invokesBusinessAction
			&& left[2].primaryTextBold && left[2].secondaryTextNormal,
			"page button is a visual no-op with mixed page-number weights");
		Check(left[0].bounds.left == 5.0 && left[3].bounds.right == 160.0
			&& right[1].bounds.left == 5.0 && right[0].bounds.right == 160.0,
			"horizontal drag slot stays on the outer edge");
		Check(side[0].bounds.top == 5.0 && side[3].bounds.bottom == 160.0,
			"side layout keeps the drag slot above three compact buttons");
	}

	void TestWorkspaceAndDpiLayouts()
	{
		constexpr RECT monitor{ 0, 0, 1920, 1080 };
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = true;
		WhiteboardState whiteboard;

		const auto compact = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.0F, ppt, whiteboard);
		Check(compact.mode == WorkspaceMode::PptCompact && compact.visible
			&& Width(compact.logicalBounds) == 165
			&& Height(compact.logicalBounds) == 43,
			"bottom PPT control resolves compact dimensions");
		const auto scaled = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.5F, ppt, whiteboard);
		Check(Width(scaled.logicalBounds) == 248
			&& Height(scaled.logicalBounds) == 64,
			"compact dimensions scale with DPI");
		const auto sideVisible = ResolveSurfaceLayout(Surface::MiddleLeft,
			monitor, 1.0F, ppt, whiteboard);
		const auto sideHidden = ResolveHiddenSurfaceBounds(
			Surface::MiddleLeft, monitor, sideVisible.logicalBounds);
		Check(sideHidden.right == monitor.left
			&& Height(sideHidden) == Height(sideVisible.logicalBounds),
			"side controls preserve their size at the off-screen entrance");

		whiteboard.expandedLayoutTarget = true;
		whiteboard.active = true;
		const auto expanded = ResolveSurfaceLayout(Surface::BottomLeft,
			monitor, 1.0F, ppt, whiteboard);
		const auto hiddenSide = ResolveSurfaceLayout(Surface::MiddleLeft,
			monitor, 1.0F, ppt, whiteboard);
		Check(expanded.mode == WorkspaceMode::WhiteboardExpanded
			&& Width(expanded.logicalBounds) == 230
			&& Height(expanded.logicalBounds) == 80,
			"PPT bottom host resolves the expanded whiteboard shape");
		Check(hiddenSide.mode == WorkspaceMode::Hidden && !hiddenSide.visible,
			"whiteboard workspace hides PPT side controls");

		ppt.presentationVisible = false;
		whiteboard.expandedLayoutTarget = false;
		whiteboard.active = false;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
			== WorkspaceMode::Hidden,
			"inactive PPT and whiteboard resolve hidden");
	}

	void TestWorkspaceTransitionAndFlashPolicy()
	{
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = false;
		WhiteboardState whiteboard;

		Check(!ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
			WorkspaceMode::PptCompact, WorkspaceMode::WhiteboardExpanded, false),
			"hidden PPT bottom bar enters whiteboard at final geometry");
		Check(ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
			WorkspaceMode::PptCompact, WorkspaceMode::WhiteboardExpanded, true),
			"visible PPT bottom bar morphs into whiteboard");
		Check(ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
			WorkspaceMode::WhiteboardExpanded, WorkspaceMode::PptCompact, true),
			"whiteboard reverse transition morphs from the current frame");
		Check(ShouldPreserveSurfaceBoundsWhileHiding(Surface::BottomLeft)
			&& ShouldPreserveSurfaceBoundsWhileHiding(Surface::BottomRight)
			&& !ShouldPreserveSurfaceBoundsWhileHiding(Surface::MiddleLeft),
			"hidden bottom hosts fade at the current frame while side hosts slide out");
		Check(ShouldKeepPageControlWindowVisible(true, false)
			&& ShouldKeepPageControlWindowVisible(false, true)
			&& !ShouldKeepPageControlWindowVisible(false, false),
			"hidden HWND survives only for its bounded exit transition");
		Check(ShouldLockSurfaceInput(false, false, false)
			&& ShouldLockSurfaceInput(true, true, false)
			&& ShouldLockSurfaceInput(true, false, true)
			&& !ShouldLockSurfaceInput(true, false, false),
			"surface input stays locked while hidden or transitioning");

		Check(ShouldFlashPptSurface(Surface::BottomLeft, ppt, whiteboard)
			&& !ShouldFlashPptSurface(Surface::MiddleLeft, ppt, whiteboard),
			"keyboard feedback targets only visible PPT surfaces");
		whiteboard.expandedLayoutTarget = true;
		whiteboard.active = true;
		Check(!ShouldFlashPptSurface(Surface::BottomLeft, ppt, whiteboard),
			"keyboard PPT feedback stays disabled in whiteboard");
		whiteboard.expandedLayoutTarget = false;
		whiteboard.active = false;
		ppt.presentationVisible = false;
		Check(!ShouldFlashPptSurface(Surface::BottomLeft, ppt, whiteboard),
			"keyboard PPT feedback stays disabled outside presentation");
	}

	void TestWhiteboardLayoutTargetAndReadiness()
	{
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		WhiteboardState whiteboard;

		whiteboard.expandedLayoutTarget = true;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
				== WorkspaceMode::WhiteboardExpanded
			&& !whiteboard.active
			&& WhiteboardWorkspaceSwitching(whiteboard)
			&& !ShouldFlashPptSurface(Surface::BottomLeft, ppt, whiteboard)
			&& ShouldLockSurfaceInput(true, false,
				WhiteboardWorkspaceSwitching(whiteboard)),
			"expanded target starts before background readiness and locks input");

		whiteboard.active = true;
		Check(!WhiteboardWorkspaceSwitching(whiteboard),
			"Draw3-ready background completes the expanded workspace gate");

		whiteboard.expandedLayoutTarget = false;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
				== WorkspaceMode::PptCompact
			&& WhiteboardWorkspaceSwitching(whiteboard)
			&& !ShouldFlashPptSurface(Surface::BottomLeft, ppt, whiteboard),
			"exit retargets compact before the background becomes inactive");
		whiteboard.active = false;
		Check(!WhiteboardWorkspaceSwitching(whiteboard),
			"inactive background completes the compact workspace gate");

		// Exiting 期间反向重入只改变目标，背景仍保持未就绪。
		whiteboard.expandedLayoutTarget = true;
		Check(ResolveWorkspaceMode(Surface::BottomLeft, ppt, whiteboard)
				== WorkspaceMode::WhiteboardExpanded
			&& !whiteboard.active
			&& WhiteboardWorkspaceSwitching(whiteboard)
			&& ShouldAnimateWorkspaceLayout(Surface::BottomLeft,
				WorkspaceMode::PptCompact,
				WorkspaceMode::WhiteboardExpanded, true),
			"reverse re-entry immediately retargets expanded from the current frame");
	}

	void TestRuntimeCollisionFallback()
	{
		constexpr RECT monitor{ 0, 0, 800, 600 };
		PptState ppt;
		ppt.presentationVisible = true;
		ppt.layout.showBottomPair = true;
		ppt.layout.showMiddlePair = true;
		ppt.layout.middlePairHeight = -212.5F;
		const PptState saved = ppt;
		WhiteboardState whiteboard;
		const RECT mainBar{ 0, 520, 800, 560 };

		const auto resolved = ResolveRuntimePageControlLayout(
			monitor, 1.0F, ppt, whiteboard, &mainBar);
		Check(!PageControlGroupOverlapsObstacle(Surface::BottomLeft,
			Surface::BottomRight, monitor, 1.0F, resolved, whiteboard,
			mainBar, 5), "main bar is the highest-priority obstacle");
		Check(!PageControlGroupOverlapsObstacle(Surface::MiddleLeft,
			Surface::MiddleRight, monitor, 1.0F, resolved, whiteboard,
			mainBar, 5), "side pair also avoids the main bar");
		Check(!PageControlGroupsOverlap(monitor, 1.0F,
			resolved, whiteboard, 5), "side pair avoids the resolved bottom pair");
		Check(ppt.layout.bottomPairHeight == saved.layout.bottomPairHeight
			&& ppt.layout.middlePairHeight == saved.layout.middlePairHeight,
			"runtime collision fallback never mutates saved positions");

		constexpr RECT tinyMonitor{ -180, -60, 0, 60 };
		PptState extreme;
		extreme.presentationVisible = true;
		extreme.layout.showBottomPair = true;
		extreme.layout.showMiddlePair = true;
		extreme.layout.bottomPairWidth = 10000.0F;
		extreme.layout.bottomPairHeight = 10000.0F;
		extreme.layout.middlePairWidth = 10000.0F;
		extreme.layout.middlePairHeight = 10000.0F;
		extreme.layout.bottomPairScale = 3.0F;
		extreme.layout.middlePairScale = 3.0F;
		const PptState original = extreme;
		const auto fitted = ResolveRuntimePageControlLayout(
			tinyMonitor, 1.5F, extreme, {});
		for (const auto surface : { Surface::BottomLeft, Surface::BottomRight,
			Surface::MiddleLeft, Surface::MiddleRight })
		{
			const auto layout = ResolveSurfaceLayout(surface, tinyMonitor,
				1.5F, fitted, {});
			Check(layout.logicalBounds.left >= tinyMonitor.left
				&& layout.logicalBounds.top >= tinyMonitor.top
				&& layout.logicalBounds.right <= tinyMonitor.right
				&& layout.logicalBounds.bottom <= tinyMonitor.bottom,
				"tiny monitor keeps every compact surface on screen");
		}
		Check(!PageControlGroupsOverlap(tinyMonitor, 1.5F, fitted, {}, 8),
			"tiny monitor fitting keeps bottom and side groups separated");
		Check(extreme.layout.bottomPairWidth == original.layout.bottomPairWidth
			&& extreme.layout.middlePairHeight == original.layout.middlePairHeight
			&& extreme.layout.bottomPairScale == original.layout.bottomPairScale
			&& extreme.layout.middlePairScale == original.layout.middlePairScale,
			"tiny monitor fitting does not mutate the published PPT snapshot");

		whiteboard.expandedLayoutTarget = true;
		whiteboard.active = true;
		PptLayoutState dragged = saved.layout;
		dragged.bottomPairWidth = 10000.0F;
		dragged.bottomPairHeight = 10000.0F;
		const auto clamped = ClampPageControlLayout(Surface::BottomLeft,
			monitor, 1.0F, whiteboard, dragged);
		Check(clamped.bottomPairWidth == 165.0F
			&& clamped.bottomPairHeight == 510.0F,
			"whiteboard drag clamp uses expanded 230x80 dimensions");
	}

	void TestA2ProjectionAndMigration()
	{
		using namespace Inkeys::UI::Bar;
		const auto desktop = ResolveBarA2Projection(false, false);
		const auto presentation = ResolveBarA2Projection(true, false);
		const auto whiteboard = ResolveBarA2Projection(true, true);
		Check(!desktop.whiteboardTwoTwo && desktop.freezeVisible
			&& !desktop.endShowVisible,
			"desktop A2 shows two compact buttons");
		Check(presentation.whiteboardTwoTwo && !presentation.freezeVisible
			&& presentation.endShowVisible,
			"PPT A2 shows Whiteboard and EndShow");
		Check(whiteboard.whiteboardTwoTwo && !whiteboard.freezeVisible
			&& !whiteboard.endShowVisible,
			"whiteboard A2 shows only Close Whiteboard");
		Check(IsLegacyBarA2Pair("Inkeys.Bar.Whiteboard", "Inkeys.Bar.Freeze")
			&& IsLegacyBarA2Pair("Inkeys.Bar.Freeze", "Inkeys.Bar.Whiteboard"),
			"both legal legacy A2 orders are accepted");
		Check(!IsLegacyBarA2Pair("Inkeys.Bar.Whiteboard", "Inkeys.Bar.EndShow"),
			"invalid legacy A2 pair returns to defaults");

		BarA2CallbackDispatcher dispatcher;
		int dispatchCount = 0;
		dispatcher.Set([&dispatchCount]() { ++dispatchCount; });
		Check(dispatcher.Dispatch() && dispatchCount == 1,
			"one EndShow request dispatches exactly one business callback");
		Check(!dispatcher.Dispatch() && dispatchCount == 1,
			"duplicate EndShow request is suppressed while one is outstanding");
		dispatcher.Complete();
		Check(dispatcher.Dispatch() && dispatchCount == 2,
			"completed EndShow request permits a later retry");
		dispatcher.Set({});
		Check(!dispatcher.Dispatch() && dispatchCount == 2,
			"cleared EndShow callback cannot enqueue a stale request");
	}
}

int RunPageControlTests()
{
	TestCompactWidgetContracts();
	TestWorkspaceAndDpiLayouts();
	TestWorkspaceTransitionAndFlashPolicy();
	TestWhiteboardLayoutTargetAndReadiness();
	TestRuntimeCollisionFallback();
	TestA2ProjectionAndMigration();
	return failureCount;
}
