#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"

namespace
{
	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	bool Near(double lhs, double rhs, double epsilon = 0.000001)
	{
		return std::abs(lhs - rhs) <= epsilon;
	}

	bool RectEquals(const RECT& value, const RECT& expected)
	{
		return value.left == expected.left && value.top == expected.top
			&& value.right == expected.right && value.bottom == expected.bottom;
	}
}

int RunBarBottomDockTests()
{
	using namespace Inkeys::UI::Bar;
	const RECT monitor{ 100, 50, 2020, 1130 };
	Check(ResolveBarBottomDockLine(monitor, RECT{ 100, 50, 2020, 1082 }) == 1082.0,
		"bottom taskbar uses work-area bottom");
	Check(ResolveBarBottomDockLine(monitor, monitor) == 1130.0,
		"no or auto-hidden taskbar uses monitor bottom");
	Check(ResolveBarBottomDockLine(monitor, RECT{ 148, 50, 2020, 1130 }) == 1130.0,
		"side taskbar does not move dock line");
	Check(ResolveBarBottomDockLine(monitor, RECT{ 100, 98, 2020, 1130 }) == 1130.0,
		"top taskbar does not move dock line");
	Check(ResolveBarBottomDockLine(monitor, RECT{}) == 1130.0,
		"invalid work area falls back to monitor bottom");
	Check(ResolveBarBottomDockLine(
		monitor, RECT{ 99, 50, 2020, 1082 }) == 1130.0,
		"work area outside monitor left falls back");
	Check(ResolveBarBottomDockLine(
		monitor, RECT{ 100, 49, 2020, 1082 }) == 1130.0,
		"work area outside monitor top falls back");
	Check(ResolveBarBottomDockLine(
		monitor, RECT{ 100, 50, 2021, 1082 }) == 1130.0,
		"work area outside monitor right falls back");

	for (double zoom : { 1.0, 1.5, 1.875 })
	{
		const double center = ResolveBarBottomDockCenterScreenY(
			1082.0, 80.0, 1.0, zoom);
		Check(Near(ResolveBarVisibleBorderBottomScreen(
			center, 80.0, 1.0, zoom), 1082.0),
			"visible stroke bottom aligns exactly at every zoom");
	}
	Check(Near(ResolveBarBottomDockInteractionZoom(96, 1.0), 1.0)
		&& Near(ResolveBarBottomDockInteractionZoom(144, 1.0), 1.5)
		&& Near(ResolveBarBottomDockInteractionZoom(144, 1.25), 1.875),
		"pending DPI and config zoom are combined exactly once");
	Check(Near(ResolveBarBottomDockInteractionZoom(0, 0.0), 1.0),
		"invalid interaction zoom inputs use the stable fallback");
	const double oldRigidGripScreenY = 1000.0 + 15.0 * 1.0;
	const double rebasedElasticDip =
		ResolveBarBottomDockElasticOffsetForScreenGrip(
			oldRigidGripScreenY, 980.0, 1.5);
	Check(Near(980.0 + rebasedElasticDip * 1.5,
		oldRigidGripScreenY),
		"DPI and work-area changes preserve the absolute rigid drag grip");
	Check(Near(ResolveBarBottomDockElasticOffsetForScreenGrip(
		std::numeric_limits<double>::quiet_NaN(), 980.0, 1.5), 0.0)
		&& Near(ResolveBarBottomDockElasticOffsetForScreenGrip(
			1000.0, std::numeric_limits<double>::quiet_NaN(), 1.5),
			BarBottomDockVisualLimitDip)
		&& Near(ResolveBarBottomDockElasticOffsetForScreenGrip(
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::quiet_NaN(), 1.5), 0.0),
		"non-finite physical grip inputs use finite fallbacks");
	Check(ShouldAllowBarBottomDockClick(
		false, false, false, 4.0, 2.5, 1.0)
		&& !ShouldAllowBarBottomDockClick(
			false, true, false, 0.0, 0.0, 1.0)
		&& !ShouldAllowBarBottomDockClick(
			false, false, true, 0.0, 0.0, 1.0),
		"cancelled and mode-changing elastic gestures never trigger clicks");

	const double centeredMainX = ResolveBarBottomDockInitialMainCenterScreenX(
		RECT{ 0, 0, 1000, 600 }, -40.5, 359.5, 40.5, 1.0);
	Check(Near(centeredMainX, 340.5),
		"expanded body centers while main button remains on the left");
	Check(Near(ClampBarBottomDockMainCenterScreenX(
		-100.0, RECT{ 0, 0, 1000, 600 }, 40.5, 1.5), 60.75),
		"horizontal clamp only keeps main button visible");
	Check(Near(ClampBarBottomDockMainCenterScreenX(
		800.0, RECT{ 0, 0, 1000, 600 }, 40.0, 1.0), 800.0)
		&& Near(ClampBarBottomDockMainCenterScreenX(
			100.0, RECT{ 0, 0, 1000, 600 }, 40.0, 1.0), 100.0)
		&& Near(ClampBarBottomDockMainCenterScreenX(
			650.0, RECT{ 0, 0, 1000, 600 }, 40.0, 1.0), 650.0),
		"horizontal reversal remains exact and independent");
	Check(ResolveBarMainBarRightSide(500.0, 1000.0)
		&& ResolveBarMainBarRightSide(499.99, 1000.0)
		&& !ResolveBarMainBarRightSide(500.01, 1000.0),
		"main bar reverses only after crossing the horizontal center line");
	const auto targetIndicator = ResolveBarBottomDockTargetIndicatorGeometry(
		-120.0, 360.0, 688.0);
	Check(Near(targetIndicator.leftDip, -123.0)
		&& Near(targetIndicator.topDip, 608.0)
		&& Near(targetIndicator.rightDip, 243.0)
		&& Near(targetIndicator.bottomDip, 688.0),
		"dock target indicator follows main bar x and keeps normal geometry");
	for (double zoom : { 1.0, 1.5 })
	{
		Check(Near((targetIndicator.rightDip - targetIndicator.leftDip) * zoom,
			(360.0 + 2.0 * BarBottomDockTargetIndicatorOutsetDip) * zoom)
			&& Near((targetIndicator.bottomDip - targetIndicator.topDip) * zoom,
				BarBottomDockTargetIndicatorHeightDip * zoom),
			"dock target indicator zoom is applied exactly once");
	}
	Check(IsBarBottomDockEntryCapture(
		BarBottomDockMode::Floating, BarBottomDockMode::BottomDocked,
		BarBottomDockPhase::Capturing)
		&& !IsBarBottomDockEntryCapture(
			BarBottomDockMode::BottomDocked, BarBottomDockMode::BottomDocked,
			BarBottomDockPhase::Dragging)
		&& !IsBarBottomDockEntryCapture(
			BarBottomDockMode::BottomDocked, BarBottomDockMode::Floating,
			BarBottomDockPhase::Detaching)
		&& !IsBarBottomDockEntryCapture(
			BarBottomDockMode::Floating, BarBottomDockMode::Floating,
			BarBottomDockPhase::Detaching),
		"dock target indicator starts only for a real entry capture");
	Check(ResolveBarBottomDockTargetIndicatorAction(
		BarBottomDockMode::Floating, BarBottomDockMode::BottomDocked,
		BarBottomDockPhase::Capturing, false, true)
			== BarBottomDockTargetIndicatorAction::FadeIn
		&& ResolveBarBottomDockTargetIndicatorAction(
			BarBottomDockMode::BottomDocked, BarBottomDockMode::BottomDocked,
			BarBottomDockPhase::Capturing, true, true)
			== BarBottomDockTargetIndicatorAction::None
		&& ResolveBarBottomDockTargetIndicatorAction(
			BarBottomDockMode::BottomDocked, BarBottomDockMode::BottomDocked,
			BarBottomDockPhase::Stable, true, false)
			== BarBottomDockTargetIndicatorAction::FadeOut,
		"dock target indicator fades in and out around capture settling");
	Check(ResolveBarBottomDockTargetIndicatorAction(
		BarBottomDockMode::BottomDocked, BarBottomDockMode::Floating,
		BarBottomDockPhase::Detaching, true, true)
			== BarBottomDockTargetIndicatorAction::HideImmediately
		&& ResolveBarBottomDockTargetIndicatorAction(
			BarBottomDockMode::Floating, BarBottomDockMode::Floating,
			BarBottomDockPhase::Detaching, false, false)
			== BarBottomDockTargetIndicatorAction::HideImmediately
		&& ResolveBarBottomDockTargetIndicatorAction(
			BarBottomDockMode::BottomDocked, BarBottomDockMode::BottomDocked,
			BarBottomDockPhase::Dragging, false, false)
			== BarBottomDockTargetIndicatorAction::None,
		"dock target indicator never starts during dock dragging or detaching");

	BarBottomDockEnvironment environment{
		RECT{ 0, 0, 1920, 1080 }, RECT{ 0, 0, 1920, 1032 }, 1.5 };
	BarBottomDockDragTracker tracker;
	tracker.Begin(BarBottomDockMode::Floating,
		951.0, 1002.0, 981.0, environment);
	auto capturedAbove = tracker.Update(966.0, 1017.0, environment);
	Check(capturedAbove.captured
		&& capturedAbove.mode == BarBottomDockMode::BottomDocked
		&& Near(capturedAbove.elasticOffsetDip, -10.0),
		"approach from above captures with stretch offset");
	auto exactDetachThreshold = tracker.Update(
		capturedAbove.stableGripScreenY + 30.0, 1047.0, environment);
	Check(!exactDetachThreshold.detached
		&& exactDetachThreshold.mode == BarBottomDockMode::BottomDocked,
		"exact twenty DIP remains docked");
	auto detached = tracker.Update(
		capturedAbove.stableGripScreenY + 30.01, 1047.01, environment);
	Check(detached.detached
		&& detached.mode == BarBottomDockMode::Floating
		&& detached.phase == BarBottomDockPhase::Detaching,
		"more than twenty DIP detaches");
	BarBottomDockDragTracker zoomRebasedTracker;
	zoomRebasedTracker.Begin(BarBottomDockMode::BottomDocked,
		1000.0, 1032.0, 1000.0,
		BarBottomDockEnvironment{ environment.monitorBounds,
			environment.workArea, 1.0 });
	zoomRebasedTracker.RebaseDockGrip(1000.0, 1032.0);
	auto zoomExactThreshold = zoomRebasedTracker.Update(
		1030.0, 1062.0, environment);
	Check(!zoomExactThreshold.detached
		&& zoomExactThreshold.mode == BarBottomDockMode::BottomDocked,
		"zoom rebase keeps the exact twenty DIP detach boundary");

	BarBottomDockDragTracker exactCaptureTracker;
	exactCaptureTracker.Begin(BarBottomDockMode::Floating,
		900.0, 1001.0, 981.0, environment);
	auto exactCapture = exactCaptureTracker.Update(901.0, 1002.0, environment);
	Check(exactCapture.captured
		&& Near(exactCapture.elasticOffsetDip, -20.0),
		"exact twenty DIP captures and zoom is applied once");
	BarBottomDockDragTracker outsideCaptureTracker;
	outsideCaptureTracker.Begin(BarBottomDockMode::Floating,
		900.0, 1000.0, 981.0, environment);
	auto outsideCapture = outsideCaptureTracker.Update(
		900.99, 1000.99, environment);
	Check(!outsideCapture.captured
		&& outsideCapture.mode == BarBottomDockMode::Floating,
		"outside the capture band remains floating");

	tracker.Begin(BarBottomDockMode::Floating,
		1042.0, 1062.0, 1012.0, environment);
	auto capturedBelow = tracker.Update(1032.0, 1052.0, environment);
	Check(capturedBelow.captured && capturedBelow.elasticOffsetDip > 0.0,
		"approach from below captures with compression offset");

	tracker.Begin(BarBottomDockMode::Floating,
		950.0, 980.0, 1000.0, environment);
	auto fastCross = tracker.Update(1035.0, 1065.0, environment);
	Check(fastCross.captured && fastCross.detached
		&& fastCross.mode == BarBottomDockMode::Floating
		&& fastCross.phase == BarBottomDockPhase::Detaching
		&& Near(fastCross.elasticOffsetDip, BarBottomDockThresholdDip),
		"fast segment consumes capture and detach without a docked stall");
	auto fastCrossRecovery = tracker.Update(1036.0, 1066.0, environment);
	Check(fastCrossRecovery.phase == BarBottomDockPhase::Recovering
		&& !fastCrossRecovery.modeChanged,
		"detaching advances to recovering on the next absolute sample");
	tracker.Begin(BarBottomDockMode::Floating,
		1080.0, 1080.0, 1000.0, environment);
	auto fastReverseCross = tracker.Update(950.0, 950.0, environment);
	Check(fastReverseCross.captured && fastReverseCross.detached
		&& fastReverseCross.mode == BarBottomDockMode::Floating
		&& Near(fastReverseCross.elasticOffsetDip,
			-BarBottomDockThresholdDip),
		"fast reverse segment consumes both thresholds symmetrically");

	const auto stretched = ResolveBarBottomDockVerticalMapping(0.0, 80.0, -20.0);
	Check(Near(stretched.visualTopDip, -20.0) && Near(stretched.scaleY, 1.25)
		&& Near(stretched.rigidGripYDip, 20.0)
		&& Near(stretched.MapY(40.0), 30.0)
		&& Near(stretched.UnmapY(30.0), 40.0),
		"upward grip stretches body while rigid grip follows full delta");
	const auto compressed = ResolveBarBottomDockVerticalMapping(0.0, 80.0, 20.0);
	Check(Near(compressed.visualTopDip, 20.0) && Near(compressed.scaleY, 0.75)
		&& Near(compressed.rigidOverlayTranslationYDip, 20.0),
		"downward grip compresses body and translates rigid overlays");
	const auto captureAboveStart = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, -12.0, -12.0);
	const auto captureAboveProgress = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, -12.0, -6.0);
	Check(Near(captureAboveStart.visualTopDip, -12.0)
		&& Near(captureAboveStart.visualBottomDip, 68.0)
		&& Near(captureAboveStart.scaleY, 1.0)
		&& Near(captureAboveProgress.visualTopDip, -12.0)
		&& Near(captureAboveProgress.visualBottomDip, 74.0),
		"capture from above keeps the first frame continuous then animates the bottom edge");
	const auto captureBelowStart = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, 12.0, 12.0);
	const auto captureBelowProgress = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, 12.0, 6.0);
	Check(Near(captureBelowStart.visualTopDip, 12.0)
		&& Near(captureBelowStart.visualBottomDip, 92.0)
		&& Near(captureBelowStart.scaleY, 1.0)
		&& Near(captureBelowProgress.visualTopDip, 12.0)
		&& Near(captureBelowProgress.visualBottomDip, 86.0),
		"capture from below keeps the first frame continuous then compresses toward dock");
	const auto detachedDown = ResolveBarBottomDockRecoveringVerticalMapping(
		20.0, 100.0, 20.0);
	Check(Near(detachedDown.visualTopDip, 20.0)
		&& Near(detachedDown.visualBottomDip, 80.0)
		&& Near(detachedDown.scaleY, 0.75)
		&& Near(detachedDown.rigidOverlayTranslationYDip, 0.0),
		"downward detach preserves the compressed frame while the window moves");
	const auto detachedUp = ResolveBarBottomDockRecoveringVerticalMapping(
		-20.0, 60.0, -20.0);
	Check(Near(detachedUp.visualTopDip, -20.0)
		&& Near(detachedUp.visualBottomDip, 80.0)
		&& Near(detachedUp.scaleY, 1.25),
		"upward detach preserves the stretched frame while the window moves");

	const RECT logicalBody{ 10, 0, 110, 80 };
	Check(RectEquals(TransformBarBottomDockBodyRect(
		logicalBody, compressed, 1.5), RECT{ 10, 30, 110, 90 }),
		"body dirty bounds use the same endpoint mapping as drawing");
	Check(RectEquals(TranslateBarBottomDockRigidRect(
		logicalBody, 20.0, 1.5), RECT{ 10, 30, 110, 110 }),
		"rigid panel dirty bounds only translate");
	Check(RectEquals(ResolveBarBottomDockVisualEnvelope(
		RECT{ 10, 20, 110, 120 }, 1.5), RECT{ 10, -16, 110, 156 }),
		"elastic capacity reserves the full twenty-four DIP envelope once");
	Check(RectEquals(ResolveBarBottomDockCapacityEnvelope(
		RECT{ 10, 50, 110, 150 }, 1.5),
		RECT{ 10, 14, 110, 186 }),
		"capacity envelope is derived from the untransformed baseline");
	Check(ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
		true, 1040.0, 1039.25, 1039.0)
		&& !ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
			true, 1040.0, 1039.25, 1000.0)
		&& !ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
			false, 1040.0, 1039.25, 1039.0),
		"downward release re-docks only when the visible constraint blocks it");
	const POINT latestTranslation{ 30, 40 };
	const POINT frameTranslation{ 10, 20 };
	const POINT currentFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 4, 4, latestTranslation, frameTranslation);
	Check(currentFrameTranslation.x == 30 && currentFrameTranslation.y == 40,
		"current transition frame may consume the latest drag translation");
	const POINT staleFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 6, 6, latestTranslation, frameTranslation);
	const POINT publishingFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 5, 5, latestTranslation, frameTranslation);
	Check(staleFrameTranslation.x == 10 && staleFrameTranslation.y == 20
		&& publishingFrameTranslation.x == 10
		&& publishingFrameTranslation.y == 20,
		"stale or in-flight transition frames retain their matching translation");
	const double mappedSample = MapBarBottomDockBodyPixelY(
		67.5, stretched, 1.5);
	Check(Near(UnmapBarBottomDockBodyPixelY(
		mappedSample, stretched, 1.5), 67.5),
		"body hit testing is the exact inverse of drawing");
	const auto bodyLight = ResolveBarBottomDockBodyLocalLight(
		240.0, mappedSample, 90.0, stretched, 1.5);
	Check(Near(MapBarBottomDockBodyPixelY(
		bodyLight.centerY, stretched, 1.5), mappedSample)
		&& Near(bodyLight.centerX, 240.0)
		&& Near(bodyLight.radiusY * stretched.scaleY, 90.0),
		"body cursor light inverse mapping preserves its visual center and radius");
	const auto rigidLight = ResolveBarBottomDockRigidLocalLight(
		240.0, 315.0, 90.0, 20.0, 1.5);
	Check(Near(rigidLight.centerY + 20.0 * 1.5, 315.0)
		&& Near(rigidLight.centerX, 240.0)
		&& Near(rigidLight.radiusX, 90.0)
		&& Near(rigidLight.radiusY, 90.0),
		"rigid cursor light inverse translation preserves screen geometry");

	auto CheckDetachContinuity = [&](double offsetDip,
		double captureBottomOffsetDip, std::string_view name)
		{
			const double zoom = 1.5;
			const auto dockedMapping = ResolveBarBottomDockVerticalMapping(
				100.0, 180.0, offsetDip, captureBottomOffsetDip);
			const auto recoveryMapping =
				ResolveBarBottomDockRecoveringVerticalMapping(
					100.0, 180.0, offsetDip, captureBottomOffsetDip);
			const double windowShiftPx = offsetDip * zoom;
			const double dockedTopPx = dockedMapping.visualTopDip * zoom;
			const double dockedBottomPx = dockedMapping.visualBottomDip * zoom;
			const double recoveryTopPx = recoveryMapping.visualTopDip * zoom
				+ windowShiftPx;
			const double recoveryBottomPx = recoveryMapping.visualBottomDip * zoom
				+ windowShiftPx;
			const double dockedGripPx = dockedMapping.rigidGripYDip * zoom;
			const double recoveryGripPx = recoveryMapping.rigidGripYDip * zoom
				+ windowShiftPx;
			Check(Near(dockedTopPx, recoveryTopPx)
				&& Near(dockedBottomPx, recoveryBottomPx)
				&& Near(dockedGripPx, recoveryGripPx), name);
		};
	CheckDetachContinuity(20.0, 0.0,
		"downward detach preserves body and rigid grip in screen space");
	CheckDetachContinuity(-20.0, 0.0,
		"upward detach preserves body and rigid grip in screen space");
	CheckDetachContinuity(-12.0, -6.0,
		"detach during capture preserves the animated bottom edge");

	BarBottomDockSpringState spring{ 20.0, 0.0 };
	bool sawOvershoot = false;
	bool active = true;
	for (int frame = 0; frame < 240 && active; ++frame)
	{
		const auto result = AdvanceBarBottomDockSpring(
			spring, 0.0, 1.0 / 60.0, true);
		active = result.active;
		sawOvershoot |= result.positionDip < 0.0;
	}
	Check(sawOvershoot, "underdamped spring has a gentle overshoot");
	Check(!active && Near(spring.positionDip, 0.0)
		&& Near(spring.velocityDipPerSecond, 0.0),
		"spring settles and stops requesting frames");
	BarBottomDockSpringState captureBottomSpring{ -12.0, 0.0 };
	const auto captureBottomFirstFrame = AdvanceBarBottomDockSpring(
		captureBottomSpring, 0.0, 1.0 / 60.0, true);
	Check(captureBottomFirstFrame.active
		&& captureBottomFirstFrame.positionDip > -12.0
		&& captureBottomFirstFrame.positionDip < 0.0,
		"captured bottom edge advances over time instead of snapping to dock");
	BarBottomDockSpringState disabledSpring{ -15.0, 100.0 };
	const auto disabled = AdvanceBarBottomDockSpring(
		disabledSpring, 0.0, 0.016, false);
	Check(!disabled.active && Near(disabled.positionDip, 0.0),
		"disabled animations settle immediately");
	BarBottomDockSpringState clampedDt{ 20.0, -15.0 };
	BarBottomDockSpringState exactDt = clampedDt;
	(void)AdvanceBarBottomDockSpring(clampedDt, 0.0, 0.5, true);
	(void)AdvanceBarBottomDockSpring(
		exactDt, 0.0, BarBottomDockSpringMaxDtSeconds, true);
	Check(Near(clampedDt.positionDip, exactDt.positionDip)
		&& Near(clampedDt.velocityDipPerSecond,
			exactDt.velocityDipPerSecond),
		"spring clamps queued frame time to thirty-two milliseconds");

	return failureCount;
}
