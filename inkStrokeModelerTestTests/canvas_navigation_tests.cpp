#include <cmath>
#include <iostream>
#include <vector>

import draw3.canvas_navigation;

namespace
{
	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED canvas navigation line " << line << ": "
			<< expression << std::endl;
	}

#define CANVAS_NAVIGATION_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)
}

int RunCanvasNavigationTests()
{
	int failures = 0;
	draw3::CanvasViewportState viewport{ 10.0f, 20.0f };
	draw3::ApplyCanvasContentTranslation(viewport, { 64.0f, -32.0f });
	CANVAS_NAVIGATION_CHECK(viewport.x == -54.0f && viewport.y == 52.0f);
	const draw3::CanvasVector canvas = draw3::ScreenToCanvas({ 100.0f, 200.0f }, viewport);
	const draw3::CanvasVector screen = draw3::CanvasToScreen(canvas, viewport);
	CANVAS_NAVIGATION_CHECK(screen.x == 100.0f && screen.y == 200.0f);
	draw3::ApplyCanvasContentTranslation(viewport, { -3000000.0f, 3000000.0f });
	CANVAS_NAVIGATION_CHECK(viewport.x == draw3::kCanvasViewportLimitDip);
	CANVAS_NAVIGATION_CHECK(viewport.y == -draw3::kCanvasViewportLimitDip);

	draw3::CanvasTouchGestureState gesture;
	auto first = gesture.OnTouchDown(1, 1000, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(first.disposition == draw3::CanvasTouchDisposition::Draw);
	auto boundarySecond = gesture.OnTouchDown(2, 1180, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(boundarySecond.beginPan);
	CANVAS_NAVIGATION_CHECK(boundarySecond.cancelExistingTouchDrawing);
	CANVAS_NAVIGATION_CHECK(gesture.GestureContactCount() == 2);
	auto extra = gesture.OnTouchDown(3, 1500, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(extra.joinedExistingPan);
	gesture.OnTouchUp(1);
	CANVAS_NAVIGATION_CHECK(gesture.PanActive());
	gesture.OnTouchUp(2);
	gesture.OnTouchUp(3);
	CANVAS_NAVIGATION_CHECK(!gesture.PanActive());
	CANVAS_NAVIGATION_CHECK(gesture.BatchAllowsPan());

	first = gesture.OnTouchDown(11, 2000, 1000, false, false);
	gesture.Update(2181, 1000);
	auto late = gesture.OnTouchDown(12, 2182, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(!late.beginPan);
	CANVAS_NAVIGATION_CHECK(late.disposition == draw3::CanvasTouchDisposition::Draw);
	gesture.Reset();
	first = gesture.OnTouchDown(13, 2200, 1000, false, false);
	gesture.Update(2381, 1000);
	auto queuedWithinWindow = gesture.OnTouchDown(14, 2379, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(queuedWithinWindow.beginPan);
	CANVAS_NAVIGATION_CHECK(queuedWithinWindow.cancelExistingTouchDrawing);
	gesture.Reset();

	first = gesture.OnTouchDown(21, 3000, 1000, true, false);
	CANVAS_NAVIGATION_CHECK(first.disposition == draw3::CanvasTouchDisposition::PanCandidate);
	gesture.Update(3181, 1000);
	CANVAS_NAVIGATION_CHECK(gesture.Disposition(21) ==
		draw3::CanvasTouchDisposition::Suppressed);
	CANVAS_NAVIGATION_CHECK(gesture.InertiaBrakeRequested());
	late = gesture.OnTouchDown(22, 3200, 1000, true, false);
	CANVAS_NAVIGATION_CHECK(late.disposition == draw3::CanvasTouchDisposition::Draw);
	gesture.Reset();
	first = gesture.OnTouchDown(23, 3300, 1000, true, false);
	gesture.Update(3481, 1000);
	queuedWithinWindow = gesture.OnTouchDown(24, 3479, 1000, true, false);
	CANVAS_NAVIGATION_CHECK(queuedWithinWindow.beginPan);
	CANVAS_NAVIGATION_CHECK(!queuedWithinWindow.cancelExistingTouchDrawing);
	gesture.Reset();

	first = gesture.OnTouchDown(31, 4000, 1000, true, false);
	auto resumed = gesture.OnTouchDown(32, 4179, 1000, true, false);
	CANVAS_NAVIGATION_CHECK(resumed.beginPan);
	CANVAS_NAVIGATION_CHECK(!resumed.cancelExistingTouchDrawing);
	gesture.Reset();
	first = gesture.OnTouchDown(41, 5000, 1000, false, true);
	CANVAS_NAVIGATION_CHECK(first.disposition == draw3::CanvasTouchDisposition::Draw);
	auto blockedSecond = gesture.OnTouchDown(42, 5100, 1000, false, true);
	CANVAS_NAVIGATION_CHECK(!blockedSecond.beginPan);
	CANVAS_NAVIGATION_CHECK(blockedSecond.disposition ==
		draw3::CanvasTouchDisposition::Draw);
	gesture.InterruptForPenOrMouse();
	CANVAS_NAVIGATION_CHECK(!gesture.PanActive());
	CANVAS_NAVIGATION_CHECK(!gesture.BatchAllowsPan());
	gesture.Reset();
	gesture.OnTouchDown(51, 6000, 1000, false, false);
	gesture.OnTouchDown(52, 6100, 1000, false, false);
	gesture.InterruptForPenOrMouse();
	CANVAS_NAVIGATION_CHECK(gesture.Disposition(51) ==
		draw3::CanvasTouchDisposition::Suppressed);
	CANVAS_NAVIGATION_CHECK(gesture.Disposition(52) ==
		draw3::CanvasTouchDisposition::Suppressed);

	draw3::CanvasPanMotionState motion;
	motion.velocity = { 1000.0f, 0.0f };
	motion.inertiaActive = true;
	draw3::BeginCanvasPan(motion, true);
	CANVAS_NAVIGATION_CHECK(!motion.inertiaActive);
	CANVAS_NAVIGATION_CHECK(motion.inheritedVelocity.x == 1000.0f);
	const draw3::CanvasVector blended = draw3::UpdateCanvasPan(
		motion, { 10.0f, 0.0f }, 0.01);
	CANVAS_NAVIGATION_CHECK(blended.x > 10.0f);
	CANVAS_NAVIGATION_CHECK(draw3::CanvasPanSpeed(motion) <=
		draw3::kCanvasPanMaximumSpeedDipPerSecond);
	draw3::StopCanvasPan(motion);
	motion.velocity = { 1000.0f, 0.0f };
	motion.inertiaActive = true;
	draw3::BeginCanvasPan(motion, true);
	const draw3::CanvasVector reversed = draw3::UpdateCanvasPan(
		motion, { -4.0f, 0.0f }, 0.01);
	CANVAS_NAVIGATION_CHECK(reversed.x > -4.0f);
	CANVAS_NAVIGATION_CHECK(motion.velocity.x > 0.0f);
	draw3::EndCanvasPan(motion);
	CANVAS_NAVIGATION_CHECK(motion.inertiaActive);
	draw3::CanvasPanMotionState lowResidualMotion;
	lowResidualMotion.velocity = { 120.0f, 0.0f };
	lowResidualMotion.inertiaActive = true;
	draw3::BeginCanvasPan(lowResidualMotion, true);
	const draw3::CanvasVector lowResidualBlend = draw3::UpdateCanvasPan(
		lowResidualMotion, { 3.0f, 0.0f }, 0.01);
	CANVAS_NAVIGATION_CHECK(lowResidualBlend.x > 3.0f);
	CANVAS_NAVIGATION_CHECK(lowResidualMotion.velocity.x > 300.0f);
	draw3::EndCanvasPan(lowResidualMotion, 0.0);
	CANVAS_NAVIGATION_CHECK(lowResidualMotion.inertiaActive);
	draw3::CanvasPanMotionState staleReleaseMotion;
	staleReleaseMotion.velocity = { 3200.0f, 0.0f };
	draw3::EndCanvasPan(staleReleaseMotion,
		draw3::kCanvasPanReleaseVelocityHorizonSeconds + 0.001);
	CANVAS_NAVIGATION_CHECK(!staleReleaseMotion.inertiaActive);
	CANVAS_NAVIGATION_CHECK(draw3::CanvasPanSpeed(staleReleaseMotion) == 0.0f);
	CANVAS_NAVIGATION_CHECK(std::abs(draw3::CanvasPanReleaseAgeSeconds(
		1099, 1000, 1000, false) - 0.099) < 0.000001);
	CANVAS_NAVIGATION_CHECK(std::abs(draw3::CanvasPanReleaseAgeSeconds(
		1100, 1000, 1000, false) -
		draw3::kCanvasPanReleaseVelocityHorizonSeconds) < 0.000001);
	CANVAS_NAVIGATION_CHECK(!std::isfinite(draw3::CanvasPanReleaseAgeSeconds(
		1099, 1000, 1000, true)));
	CANVAS_NAVIGATION_CHECK(!std::isfinite(draw3::CanvasPanReleaseAgeSeconds(
		900, 1000, 1000, false)));
	CANVAS_NAVIGATION_CHECK(
		draw3::kCanvasPanInertiaDecelerationDipPerSecondSquared == 4000.0f);
	CANVAS_NAVIGATION_CHECK(
		draw3::kCanvasPanPenBrakeDecelerationDipPerSecondSquared == 12000.0f);
	draw3::CanvasPanMotionState travelMotion;
	travelMotion.velocity = { 3200.0f, 0.0f };
	travelMotion.inertiaActive = true;
	float normalTravel = 0.0f;
	for (size_t step = 0; step < 1000 && travelMotion.inertiaActive; ++step)
		normalTravel += draw3::StepCanvasPanInertia(
			travelMotion, 0.01, false).x;
	const float expectedTravel = 3200.0f * 3200.0f /
		(2.0f * draw3::kCanvasPanInertiaDecelerationDipPerSecondSquared);
	CANVAS_NAVIGATION_CHECK(std::abs(normalTravel - expectedTravel) < 1.0f);
	motion.velocity = { 1000.0f, 0.0f };
	motion.inertiaActive = true;
	const float beforeHover = draw3::CanvasPanSpeed(motion);
	draw3::StepCanvasPanInertia(motion, 0.01, true);
	CANVAS_NAVIGATION_CHECK(draw3::CanvasPanSpeed(motion) < beforeHover);
	gesture.Reset();
	gesture.OnTouchDown(61, 7000, 1000, false, false);
	gesture.OnTouchDown(62, 7100, 1000, false, false);
	draw3::InterruptCanvasPanForDrawing(motion, gesture);
	CANVAS_NAVIGATION_CHECK(!motion.inertiaActive);
	CANVAS_NAVIGATION_CHECK(draw3::CanvasPanSpeed(motion) == 0.0f);
	CANVAS_NAVIGATION_CHECK(!gesture.PanActive());
	CANVAS_NAVIGATION_CHECK(gesture.Disposition(61) ==
		draw3::CanvasTouchDisposition::Suppressed);
	CANVAS_NAVIGATION_CHECK(gesture.Disposition(62) ==
		draw3::CanvasTouchDisposition::Suppressed);
	CANVAS_NAVIGATION_CHECK(draw3::ShouldBeginSuppressingPenContactDuringTouchPan(
		true, true));
	CANVAS_NAVIGATION_CHECK(!draw3::ShouldBeginSuppressingPenContactDuringTouchPan(
		false, true));
	CANVAS_NAVIGATION_CHECK(!draw3::ShouldBeginSuppressingPenContactDuringTouchPan(
		true, false));
	CANVAS_NAVIGATION_CHECK(draw3::ShouldSuppressPenContactForTouchPan(
		true, 1200, 0, false));
	CANVAS_NAVIGATION_CHECK(draw3::ShouldSuppressPenContactForTouchPan(
		false, 1200, 1200, false));
	CANVAS_NAVIGATION_CHECK(!draw3::ShouldSuppressPenContactForTouchPan(
		false, 1201, 1200, false));
	CANVAS_NAVIGATION_CHECK(draw3::ShouldSuppressPenContactForTouchPan(
		false, 1201, 1200, true));
	CANVAS_NAVIGATION_CHECK(!draw3::IsPenContactSampleFresh(
		true, 1200, 1200));
	CANVAS_NAVIGATION_CHECK(draw3::IsPenContactSampleFresh(
		true, 1201, 1200));
	CANVAS_NAVIGATION_CHECK(!draw3::IsPenContactSampleFresh(
		false, 1201, 1200));
	CANVAS_NAVIGATION_CHECK(draw3::IsPenContactSampleFresh(
		true, 1200, 0));
	CANVAS_NAVIGATION_CHECK(draw3::ShouldPrioritizeDrawingContact(
		true, true, false));
	CANVAS_NAVIGATION_CHECK(draw3::ShouldPrioritizeDrawingContact(
		true, false, true));
	CANVAS_NAVIGATION_CHECK(!draw3::ShouldPrioritizeDrawingContact(
		false, true, false));
	CANVAS_NAVIGATION_CHECK(!draw3::ShouldPrioritizeDrawingContact(
		true, false, false));
	draw3::StopCanvasPan(motion);
	CANVAS_NAVIGATION_CHECK(!motion.inertiaActive);

	CANVAS_NAVIGATION_CHECK(draw3::CanvasPanFallbackBlurDip(300.0f) == 0.0f);
	CANVAS_NAVIGATION_CHECK(draw3::CanvasPanFallbackBlurDip(100000.0f) ==
		draw3::kCanvasPanMaximumFallbackBlurDip);
	const draw3::CanvasRenderBudget budget = draw3::ComputeCanvasRenderBudget({
		8.333, 2.0, 1.0, 0.5 });
	CANVAS_NAVIGATION_CHECK(budget.milliseconds == 4.0);
	CANVAS_NAVIGATION_CHECK(budget.maximumTiles == 8);
	const draw3::CanvasRenderBudget exhausted = draw3::ComputeCanvasRenderBudget({
		8.333, 8.0, 1.0, 0.5 });
	CANVAS_NAVIGATION_CHECK(exhausted.maximumTiles == 0);
	CANVAS_NAVIGATION_CHECK(draw3::CanvasVisibleClarityAfterAuthoritativeWrite(
		true, true));
	CANVAS_NAVIGATION_CHECK(!draw3::CanvasVisibleClarityAfterAuthoritativeWrite(
		true, false));
	CANVAS_NAVIGATION_CHECK(!draw3::CanvasVisibleClarityAfterAuthoritativeWrite(
		false, true));
	const draw3::CanvasVector prediction = draw3::ComputeCanvasPredictionOffset(
		{ 1000.0f, 0.0f }, 1000.0f, 500.0f);
	CANVAS_NAVIGATION_CHECK(std::abs(prediction.x - 150.0f) < 0.001f);
	const draw3::CanvasVector cappedPrediction = draw3::ComputeCanvasPredictionOffset(
		{ 24000.0f, 0.0f }, 100.0f, 100.0f);
	CANVAS_NAVIGATION_CHECK(std::abs(cappedPrediction.x -
		1.5f * std::hypot(100.0f, 100.0f)) < 0.001f);
	const auto recoveryCoverage = draw3::ComputeCanvasRenderCoverageBounds(
		{ 0.0f, 0.0f }, 256.0f, 256.0f, { -2000.0f, 0.0f }, 256);
	CANVAS_NAVIGATION_CHECK(recoveryCoverage.has_value());
	CANVAS_NAVIGATION_CHECK(recoveryCoverage->left <= -256.0f);
	CANVAS_NAVIGATION_CHECK(recoveryCoverage->right >= 812.0f);

	const std::vector<draw3::CanvasTileCoordinate> contentTiles = {
		{ -2, 0 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }
	};
	const draw3::CanvasRenderTilePlan tilePlan = draw3::PlanCanvasRenderTiles(
		contentTiles, { 0.0f, 0.0f }, 256.0f, 256.0f,
		{ -2000.0f, 0.0f }, 256);
	CANVAS_NAVIGATION_CHECK(!tilePlan.tiles.empty());
	CANVAS_NAVIGATION_CHECK(tilePlan.visibleTileCount == 1);
	const draw3::CanvasTileCoordinate originTile{ 0, 0 };
	CANVAS_NAVIGATION_CHECK(tilePlan.tiles.front().tile == originTile);
	CANVAS_NAVIGATION_CHECK(tilePlan.tiles[1].priority ==
		draw3::CanvasTilePriority::LeadingEdge);
	CANVAS_NAVIGATION_CHECK(tilePlan.tiles[1].tile.x > 0);
	const draw3::CanvasRenderTilePlan oppositeTilePlan = draw3::PlanCanvasRenderTiles(
		contentTiles, { 0.0f, 0.0f }, 256.0f, 256.0f,
		{ 2000.0f, 0.0f }, 256);
	CANVAS_NAVIGATION_CHECK(oppositeTilePlan.visibleTileCount == 1);
	CANVAS_NAVIGATION_CHECK(oppositeTilePlan.tiles[1].priority ==
		draw3::CanvasTilePriority::LeadingEdge);
	CANVAS_NAVIGATION_CHECK(oppositeTilePlan.tiles[1].tile.x < 0);

	const auto snapshotIntersection = draw3::ComputeCanvasSnapshotScreenIntersection(
		{ -128.0f, -64.0f }, { 0.0f, 0.0f }, 256.0f, 256.0f);
	CANVAS_NAVIGATION_CHECK(snapshotIntersection.has_value());
	CANVAS_NAVIGATION_CHECK(snapshotIntersection->left == 0.0f);
	CANVAS_NAVIGATION_CHECK(snapshotIntersection->right == 128.0f);
	CANVAS_NAVIGATION_CHECK(snapshotIntersection->bottom == 192.0f);
	CANVAS_NAVIGATION_CHECK(!draw3::ComputeCanvasSnapshotScreenIntersection(
		{ -512.0f, 0.0f }, { 0.0f, 0.0f }, 256.0f, 256.0f).has_value());

	return failures;
}
