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
	draw3::CanvasViewportState distantViewport{ 150000.0f, -275000.0f };
	const draw3::CanvasContentTranslationResult distantTranslation =
		draw3::ApplyCanvasContentTranslationChecked(
			distantViewport, { -14.135f, -16.706f });
	CANVAS_NAVIGATION_CHECK(!distantTranslation.xClamped);
	CANVAS_NAVIGATION_CHECK(!distantTranslation.yClamped);
	const draw3::CanvasContentTranslationResult hardLimitTranslation =
		draw3::ApplyCanvasContentTranslationChecked(
			distantViewport, { -3000000.0f, 0.0f });
	CANVAS_NAVIGATION_CHECK(hardLimitTranslation.xClamped);
	CANVAS_NAVIGATION_CHECK(!hardLimitTranslation.yClamped);

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
	CANVAS_NAVIGATION_CHECK(gesture.HasContact(21));
	CANVAS_NAVIGATION_CHECK(gesture.Disposition(21) ==
		draw3::CanvasTouchDisposition::Suppressed);
	CANVAS_NAVIGATION_CHECK(gesture.InertiaBrakeRequested());
	late = gesture.OnTouchDown(22, 3200, 1000, true, false);
	CANVAS_NAVIGATION_CHECK(late.disposition == draw3::CanvasTouchDisposition::Draw);
	CANVAS_NAVIGATION_CHECK(!gesture.HasContact(999));
	gesture.OnTouchUp(21);
	CANVAS_NAVIGATION_CHECK(!gesture.HasContact(21));
	gesture.Reset();
	first = gesture.OnTouchDown(23, 3300, 1000, true, false);
	gesture.Update(3481, 1000);
	queuedWithinWindow = gesture.OnTouchDown(24, 3479, 1000, true, false);
	CANVAS_NAVIGATION_CHECK(queuedWithinWindow.beginPan);
	CANVAS_NAVIGATION_CHECK(!queuedWithinWindow.cancelExistingTouchDrawing);
	gesture.Reset();
	first = gesture.OnTouchDown(25, 3600, 1000, false, false);
	auto reversedTimestamp = gesture.OnTouchDown(26, 3599, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(!reversedTimestamp.beginPan);
	CANVAS_NAVIGATION_CHECK(reversedTimestamp.disposition ==
		draw3::CanvasTouchDisposition::Draw);
	gesture.Reset();
	first = gesture.OnTouchDown(27, 3700, 0, false, false);
	auto invalidFrequency = gesture.OnTouchDown(28, 3701, 0, false, false);
	CANVAS_NAVIGATION_CHECK(!invalidFrequency.beginPan);
	CANVAS_NAVIGATION_CHECK(invalidFrequency.disposition ==
		draw3::CanvasTouchDisposition::Draw);
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
	gesture.OnTouchUp(51);
	gesture.OnTouchUp(52);
	first = gesture.OnTouchDown(53, 6200, 1000, false, false);
	resumed = gesture.OnTouchDown(54, 6201, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(first.disposition == draw3::CanvasTouchDisposition::Draw);
	CANVAS_NAVIGATION_CHECK(resumed.beginPan);
	gesture.Reset();

	// 第二指 Down 排队后即使 latest 已前进，Pan anchor 仍从 Down 补齐全部位移。
	const draw3::CanvasPanContactAnchor secondDownAnchor{
		{ 10.0f, 20.0f }, 1, false };
	const draw3::CanvasPanContactAnchor delayedSecondLatest{
		{ 55.0f, 80.0f }, 4, false };
	const draw3::CanvasPanContactAnchor selectedSecondAnchor =
		draw3::ResolveCanvasPanContactAnchor(secondDownAnchor,
			delayedSecondLatest, draw3::CanvasPanContactAnchorMode::Down, false);
	CANVAS_NAVIGATION_CHECK(selectedSecondAnchor.sequence == 1);
	CANVAS_NAVIGATION_CHECK(delayedSecondLatest.position.x -
		selectedSecondAnchor.position.x == 45.0f);
	CANVAS_NAVIGATION_CHECK(delayedSecondLatest.position.y -
		selectedSecondAnchor.position.y == 60.0f);
	const draw3::CanvasPanContactAnchor terminalFirstAnchor =
		draw3::ResolveCanvasPanContactAnchor(
			{ { 0.0f, 0.0f }, 1, false },
			{ { 25.0f, 30.0f }, 3, false },
			draw3::CanvasPanContactAnchorMode::Current, true);
	CANVAS_NAVIGATION_CHECK(terminalFirstAnchor.sequence == 3);
	CANVAS_NAVIGATION_CHECK(terminalFirstAnchor.terminalPending);
	CANVAS_NAVIGATION_CHECK(draw3::ShouldConsumeCanvasPanContactSnapshot(
		3, 3, terminalFirstAnchor.terminalPending));
	draw3::CanvasTouchGestureState handoffGesture;
	handoffGesture.OnTouchDown(71, 7000, 1000, false, false);
	handoffGesture.OnTouchDown(72, 7050, 1000, false, false);
	CANVAS_NAVIGATION_CHECK(draw3::IsCanvasPanLifecycleOwnershipConsistent(
		true, handoffGesture.PanContactCount(), 2, 1));
	handoffGesture.OnTouchUp(71); // terminal 首指同步退休后，剩余一指仍持有 Pan。
	CANVAS_NAVIGATION_CHECK(handoffGesture.PanActive());
	CANVAS_NAVIGATION_CHECK(draw3::IsCanvasPanLifecycleOwnershipConsistent(
		true, handoffGesture.PanContactCount(), 1, 0));
	handoffGesture.OnTouchUp(72);
	CANVAS_NAVIGATION_CHECK(!handoffGesture.PanActive());
	CANVAS_NAVIGATION_CHECK(draw3::IsCanvasPanLifecycleOwnershipConsistent(
		false, handoffGesture.PanContactCount(), 0, 0));

	draw3::CanvasPanMotionState motion;
	motion.velocity = { 3000.0f, 0.0f };
	motion.inertiaActive = true;
	draw3::BeginCanvasPan(motion, true, 1000);
	CANVAS_NAVIGATION_CHECK(!motion.inertiaActive);
	CANVAS_NAVIGATION_CHECK(motion.releaseVelocityCandidate.x == 3000.0f);
	CANVAS_NAVIGATION_CHECK(motion.releaseVelocityCandidateSource ==
		draw3::CanvasPanReleaseCandidateSource::Residual);
	const draw3::CanvasVector catchUp = draw3::UpdateCanvasPan(
		motion, { 2.0f, -1.0f }, { 2.0f, -1.0f }, 1010, 1000);
	CANVAS_NAVIGATION_CHECK(catchUp.x == 2.0f && catchUp.y == -1.0f);
	CANVAS_NAVIGATION_CHECK(motion.hasNewMove);
	CANVAS_NAVIGATION_CHECK(motion.releaseVelocityCandidateSource ==
		draw3::CanvasPanReleaseCandidateSource::None);
	const draw3::CanvasVector smallDirect = motion.directVelocity;
	CANVAS_NAVIGATION_CHECK(motion.velocity.x == smallDirect.x &&
		motion.velocity.y == smallDirect.y);
	draw3::EndCanvasPan(motion, 0.0);
	CANVAS_NAVIGATION_CHECK(motion.releaseSource ==
		draw3::CanvasPanReleaseSource::NewDirect);
	CANVAS_NAVIGATION_CHECK(motion.selectedReleaseVelocity.x == smallDirect.x &&
		motion.selectedReleaseVelocity.y == smallDirect.y);
	CANVAS_NAVIGATION_CHECK(motion.velocity.x == smallDirect.x &&
		motion.velocity.y == smallDirect.y);
	CANVAS_NAVIGATION_CHECK(std::abs(motion.velocity.x - 3200.0f) > 1.0f);

	draw3::CanvasPanMotionState residualFallback;
	residualFallback.velocity = { 3000.0f, -400.0f };
	residualFallback.inertiaActive = true;
	draw3::BeginCanvasPan(residualFallback, true, 2000);
	draw3::EndCanvasPan(residualFallback, 0.01);
	CANVAS_NAVIGATION_CHECK(residualFallback.selectedReleaseVelocity.x == 3000.0f &&
		residualFallback.selectedReleaseVelocity.y == -400.0f);
	CANVAS_NAVIGATION_CHECK(residualFallback.velocity.x == 3000.0f &&
		residualFallback.velocity.y == -400.0f);
	CANVAS_NAVIGATION_CHECK(residualFallback.releaseSource ==
		draw3::CanvasPanReleaseSource::NoNewMoveResidual);

	for (float moveDelta : { 5.0f, -5.0f })
	{
		draw3::CanvasPanMotionState directionMotion;
		directionMotion.velocity = { 3000.0f, 0.0f };
		directionMotion.inertiaActive = true;
		draw3::BeginCanvasPan(directionMotion, true, 3000);
		draw3::UpdateCanvasPan(directionMotion,
			{ moveDelta, 0.0f }, { moveDelta, 0.0f }, 3010, 1000);
		const float directVelocity = directionMotion.directVelocity.x;
		CANVAS_NAVIGATION_CHECK(directionMotion.velocity.x == directVelocity);
		draw3::EndCanvasPan(directionMotion, 0.0);
		CANVAS_NAVIGATION_CHECK(directionMotion.velocity.x == directVelocity);
		CANVAS_NAVIGATION_CHECK(directionMotion.releaseSource ==
			draw3::CanvasPanReleaseSource::NewDirect);
	}
	draw3::CanvasPanMotionState sampledMotion;
	draw3::BeginCanvasPan(sampledMotion, false, 4000);
	draw3::UpdateCanvasPan(sampledMotion,
		{ 10.0f, 0.0f }, { 10.0f, 0.0f }, 4010, 1000);
	draw3::UpdateCanvasPan(sampledMotion,
		{ 20.0f, 0.0f }, { 20.0f, 0.0f }, 4030, 1000);
	draw3::UpdateCanvasPan(sampledMotion,
		{ 20.0f, 0.0f }, { 20.0f, 0.0f }, 4050, 1000);
	const float stableVelocity = sampledMotion.velocity.x;
	CANVAS_NAVIGATION_CHECK(std::abs(stableVelocity - 1000.0f) < 0.1f);
	// 重复位置包和 Up 的终态跳变都不能覆盖已拟合的释放速度。
	draw3::UpdateCanvasPan(sampledMotion, {}, {}, 4060, 1000, true);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocity.x == stableVelocity);
	const draw3::CanvasVector terminalDelta = draw3::UpdateCanvasPan(sampledMotion,
		{ 500.0f, -600.0f }, {}, 4061, 1000, false);
	CANVAS_NAVIGATION_CHECK(terminalDelta.x == 500.0f && terminalDelta.y == -600.0f);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocity.x == stableVelocity);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocity.y == 0.0f);
	const int64_t stableUpdateQpc = sampledMotion.lastUpdateQpc;
	draw3::ResetCanvasPanVelocitySamples(sampledMotion, stableUpdateQpc - 1);
	CANVAS_NAVIGATION_CHECK(sampledMotion.lastUpdateQpc == stableUpdateQpc);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocitySamples[0].qpc == stableUpdateQpc);
	CANVAS_NAVIGATION_CHECK(sampledMotion.lastVelocitySampleQpc == 0);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocity.x == 0.0f);
	CANVAS_NAVIGATION_CHECK(sampledMotion.releaseVelocityCandidate.x == stableVelocity);
	CANVAS_NAVIGATION_CHECK(sampledMotion.releaseVelocityCandidateSource ==
		draw3::CanvasPanReleaseCandidateSource::Topology);
	draw3::CanvasPanMotionState immediateTopologyRelease = sampledMotion;
	draw3::EndCanvasPan(immediateTopologyRelease, 0.01);
	CANVAS_NAVIGATION_CHECK(immediateTopologyRelease.inertiaActive);
	CANVAS_NAVIGATION_CHECK(immediateTopologyRelease.selectedReleaseVelocity.x ==
		stableVelocity);
	CANVAS_NAVIGATION_CHECK(immediateTopologyRelease.velocity.x == stableVelocity);
	CANVAS_NAVIGATION_CHECK(immediateTopologyRelease.releaseSource ==
		draw3::CanvasPanReleaseSource::TopologyNoNewMove);
	draw3::ResetCanvasPanVelocitySamples(sampledMotion, 4070);
	CANVAS_NAVIGATION_CHECK(sampledMotion.lastVelocitySampleQpc == 0);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocity.x == 0.0f);
	CANVAS_NAVIGATION_CHECK(sampledMotion.releaseVelocityCandidate.x == stableVelocity);
	draw3::UpdateCanvasPan(sampledMotion,
		{ -10.0f, 0.0f }, { -10.0f, 0.0f }, 4080, 1000);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocity.x < 0.0f);
	CANVAS_NAVIGATION_CHECK(sampledMotion.releaseVelocityCandidateSource ==
		draw3::CanvasPanReleaseCandidateSource::None);
	const float topologyDirectVelocity = sampledMotion.directVelocity.x;
	draw3::CanvasPanMotionState topologyMovedRelease = sampledMotion;
	draw3::EndCanvasPan(topologyMovedRelease, 0.0);
	CANVAS_NAVIGATION_CHECK(topologyMovedRelease.velocity.x == topologyDirectVelocity);
	CANVAS_NAVIGATION_CHECK(topologyMovedRelease.releaseSource ==
		draw3::CanvasPanReleaseSource::NewDirect);
	draw3::CanvasPanMotionState staleDirectRelease;
	draw3::BeginCanvasPan(staleDirectRelease, false, 5000);
	draw3::UpdateCanvasPan(staleDirectRelease,
		{ 10.0f, 0.0f }, { 10.0f, 0.0f }, 5010, 1000);
	const float staleSelectedDirect = staleDirectRelease.directVelocity.x;
	draw3::EndCanvasPan(staleDirectRelease,
		draw3::kCanvasPanReleaseVelocityHorizonSeconds + 0.001);
	CANVAS_NAVIGATION_CHECK(staleDirectRelease.releaseSource ==
		draw3::CanvasPanReleaseSource::NewDirect);
	CANVAS_NAVIGATION_CHECK(staleDirectRelease.selectedReleaseVelocity.x ==
		staleSelectedDirect);
	CANVAS_NAVIGATION_CHECK(staleDirectRelease.velocity.x == 0.0f);
	CANVAS_NAVIGATION_CHECK(!staleDirectRelease.inertiaActive);
	draw3::UpdateCanvasPan(sampledMotion,
		{ -10.0f, 0.0f }, { -10.0f, 0.0f }, 4300, 1000);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocitySampleCount == 1);
	CANVAS_NAVIGATION_CHECK(sampledMotion.velocity.x == 0.0f);
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
		draw3::kCanvasPanInertiaDecelerationDipPerSecondSquared == 6000.0f);
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
