#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.DirtyRegion.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h"

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

	void TestHorizontalCapturePresentedSequence()
	{
		using namespace Inkeys::UI::Bar;
		for (bool opensRight : { true, false })
			for (double zoom : { 1.0, 1.5, 1.875 })
				for (double width : { 200.0, 560.0 })
				{
					const BarBottomDockEnvironment environment{
						RECT{ 100, 50, 2020, 1130 }, RECT{ 100, 50, 2020, 1082 }, zoom };
					const double monitorCenter = ResolveBarBottomDockMonitorCenterScreenX(environment.monitorBounds);
					const double bodyToMain = (opensRight ? 1.0 : -1.0) * (width / 2.0 + 10.0) * zoom;
					const double baseMain = monitorCenter - 60.0 * zoom - bodyToMain - 37.0;
					const double baseLeft = baseMain / zoom + (opensRight ? 50.0 : -50.0 - width);
					const double baseRight = baseLeft + width;
					const double baseFar = opensRight ? baseRight : baseLeft;
					BarBottomDockCenterDragTracker tracker;
					tracker.Begin(BarBottomDockCenterMode::Free, monitorCenter - 60.0 * zoom,
						true, true, environment);
					BarBottomDockCenterMode presentedMode = BarBottomDockCenterMode::Free;
					LONG presentedTranslation = 37;
					double presentedFar = baseFar;
					BarBottomDockSpringState farSpring;
					int sampleIndex = 0;
					bool recapturedDuringRecovery = false;
					constexpr int recoveryFrameCounts[]{ 180, 4, 1 };
					for (double offset : { -45.0, -40.0, -39.0, -25.0, 0.0, 25.0, 39.0, 41.0, 39.0, -41.0, -39.0 })
					{
						const double rawBody = monitorCenter + offset * zoom;
						const double rawMain = rawBody - bodyToMain;
						const auto update = tracker.Update(rawBody, true, true, environment);
						const LONG translation = static_cast<LONG>(std::lround(
							update.constrainedCenterScreenX - bodyToMain - baseMain));
						const bool centered = update.mode == BarBottomDockCenterMode::Centered;
						const double grip = centered ? (rawMain - baseMain - translation) / zoom : 0.0;
						const bool transition = update.mode != presentedMode;
						const double previousFarScreen = presentedFar * zoom + presentedTranslation;
						if (transition)
						{
							recapturedDuringRecovery |= centered
								&& std::abs(farSpring.positionDip) > BarBottomDockSettleDistanceDip;
							farSpring = { ResolveBarBottomDockRebasedFarEdgeOffsetDip(
								presentedFar, presentedTranslation, translation, baseFar, zoom), 0.0 };
						}
						const auto mapping = ResolveBarBottomDockHorizontalMapping(
							baseLeft, baseRight, opensRight, grip, farSpring.positionDip);
						const double currentFar = opensRight ? mapping.visualRightDip : mapping.visualLeftDip;
						Check(std::abs(baseMain + translation + mapping.rigidGripTranslationXDip * zoom - rawMain) <= 0.5,
							"capturing between 24 and 40 DIP, crossing and detaching preserve the held main button");
						if (transition)
							Check(Near(currentFar * zoom + translation, previousFarScreen),
								"capture, detach and recapture seed the far edge from exactly the last presented pixels");
						const double nearBase = opensRight ? baseLeft : baseRight;
						Check(Near(mapping.MapX(nearBase), nearBase + grip)
							&& Near(mapping.UnmapX(mapping.MapX(baseLeft + width / 3.0)), baseLeft + width / 3.0),
							"near edge shares rigid grip motion and body hit mapping round trips");
						const RECT baseBounds{ static_cast<LONG>(std::floor(baseLeft * zoom)), 900,
							static_cast<LONG>(std::ceil(baseRight * zoom)), 1000 };
						const auto envelope = ResolveBarBottomDockCapacityEnvelope(baseBounds, zoom,
							std::max({ BarBottomDockCenterThresholdDip, std::abs(grip), std::abs(farSpring.positionDip) }));
						Check(envelope.left <= mapping.visualLeftDip * zoom
							&& envelope.right >= mapping.visualRightDip * zoom,
							"horizontal capacity includes exact grip and recovery seed at every scale");
						// 很短的下一帧应连续衰减，不能把 39/41 DIP 的真实像素瞬间截到 24。
						const double initialFarOffset = farSpring.positionDip;
						(void)AdvanceBarBottomDockSpring(farSpring, 0.0, 0.0001, true, true);
						Check(std::abs(farSpring.positionDip - initialFarOffset) < 0.1,
							"far edge recovery has no first-frame clamp at the old visual limit");
						// 交错完整恢复与仅 1～4 帧的快速反向，重捕获必须接住仍在运动的远端。
						const int recoveryFrames = recoveryFrameCounts[sampleIndex++ % 3];
						for (int frame = 0; frame < recoveryFrames; ++frame)
							(void)AdvanceBarBottomDockSpring(farSpring, 0.0, 1.0 / 60.0, true, true);
						if (recoveryFrames == 180)
							Check(Near(farSpring.positionDip, 0.0), "far edge still settles with the original spring timing");
						presentedMode = update.mode;
						presentedTranslation = translation;
						const auto presentedMapping = ResolveBarBottomDockHorizontalMapping(
							baseLeft, baseRight, opensRight, grip, farSpring.positionDip);
						presentedFar = opensRight ? presentedMapping.visualRightDip : presentedMapping.visualLeftDip;
					}
					Check(recapturedDuringRecovery, "sequence recaptures before the preceding far-edge recovery settles");
					for (int frame = 0; frame < 180; ++frame)
						(void)AdvanceBarBottomDockSpring(farSpring, 0.0, 1.0 / 60.0, true, true);
					Check(Near(farSpring.positionDip, 0.0), "rapid recapture recovery still converges");
				}
	}

	void TestFirstCapturePresentationAcrossNewSamples()
	{
		using namespace Inkeys::UI::Bar;
		for (bool opensRight : { true, false })
			for (double zoom : { 1.0, 1.5, 1.875 })
			{
				const POINT origin{ 100, 50 };
				const POINT previousTranslation{ 13, -7 };
				const POINT captureTranslation{
					previousTranslation.x - static_cast<LONG>(std::lround((opensRight ? 39.0 : -39.0) * zoom)),
					previousTranslation.y - static_cast<LONG>(std::lround(16.0 * zoom)) };
				const double gripX = (previousTranslation.x - captureTranslation.x) / zoom;
				const double gripY = (previousTranslation.y - captureTranslation.y) / zoom;
				const double mainX = 400.0;
				const double mainY = 500.0;
				const double left = mainX + (opensRight ? 50.0 : -250.0);
				const double right = left + 200.0;
				const double baseFarEdge = opensRight ? right : left;
				const double captureFar = ResolveBarBottomDockRebasedFarEdgeOffsetDip(
					baseFarEdge, previousTranslation.x, captureTranslation.x, baseFarEdge, zoom);
				const auto horizontal = ResolveBarBottomDockHorizontalMapping(
					left, right, opensRight, gripX, captureFar);
				const auto vertical = ResolveBarBottomDockVerticalMapping(
					mainY - 40.5, mainY + 40.5, gripY, gripY);
				// 首次捕获等待 ULW 时，普通采样仍可连续推进 serial，但不改变此屏障。
				for (unsigned long long serial : { 2ULL, 4ULL, 12ULL, 40ULL })
				{
					const auto decision = ResolveBarBottomDockFramePresentation(
						2, captureTranslation, serial, serial, 2, 0,
						captureTranslation, previousTranslation);
					Check(!decision.deferred, "ordinary samples cannot starve the first capture presentation");
					const double screenX = origin.x + (mainX + horizontal.rigidGripTranslationXDip) * zoom
						+ decision.translation.x;
					const double screenY = origin.y + vertical.MapY(mainY) * zoom + decision.translation.y;
					Check(Near(screenX, origin.x + mainX * zoom + previousTranslation.x)
						&& Near(screenY, origin.y + mainY * zoom + previousTranslation.y),
						"first capture bitmap and destination preserve both held screen coordinates when serial advances");
				}
				const POINT staleFallback = ResolveBarBottomDockFrameTranslation(
					2, 4, 4, captureTranslation, previousTranslation);
				Check(std::abs(horizontal.rigidGripTranslationXDip * zoom + staleFallback.x
					- previousTranslation.x) > 24.0 * zoom
					&& std::abs(vertical.MapY(mainY) * zoom + staleFallback.y
						- mainY * zoom - previousTranslation.y) > 10.0 * zoom,
					"old stale-destination fallback reproduces the X/Y capture jump");

				BarPresentDecision present;
				present.AddDemand({ true, false, false });
				const auto failed = present.CompleteAttempt(BarPresentAttemptResult::Acquired(
					S_OK, TRUE, S_OK, E_FAIL, {}));
				const auto retry = ResolveBarBottomDockFramePresentation(
					4, captureTranslation, 6, 6, 2, 0, captureTranslation, previousTranslation);
				Check(!failed.IsCommitted() && present.ShouldPresent() && present.NeedsFullDirty()
					&& !retry.deferred && retry.translation.x == captureTranslation.x
					&& retry.translation.y == captureTranslation.y,
					"failed first capture retains its barrier and retries with matching bitmap translation");
				Check(present.CompleteAttempt(BarPresentAttemptResult::Acquired(
					S_OK, TRUE, S_OK, S_OK, {})).IsCommitted(), "retry establishes the first capture snapshot");

				const auto next = ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 6, 6, 2, 4, captureTranslation, captureTranslation);
				const auto nextHorizontal = ResolveBarBottomDockHorizontalMapping(
					left, right, opensRight, gripX + 2.0 / zoom, captureFar);
				const auto nextVertical = ResolveBarBottomDockVerticalMapping(
					mainY - 40.5, mainY + 40.5, gripY + 2.0 / zoom, gripY);
				Check(!next.deferred
					&& Near((mainX + nextHorizontal.rigidGripTranslationXDip) * zoom + next.translation.x,
						mainX * zoom + previousTranslation.x + 2.0)
					&& Near(nextVertical.MapY(mainY - 40.5) * zoom + next.translation.y,
						(mainY - 40.5) * zoom + previousTranslation.y + 2.0),
					"next current sample follows the new pointer without an extra capture offset");
				const POINT movedTranslation{ captureTranslation.x + 3, captureTranslation.y + 5 };
				const auto staleMove = ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 8, 8, 2, 4, movedTranslation, movedTranslation);
				Check(!staleMove.deferred && staleMove.translation.x == movedTranslation.x
					&& staleMove.translation.y == movedTranslation.y,
					"stale ordinary frame preserves a direct move after capture is presented");
				present.AddDemand({ true, false, false });
				const auto obsolete = ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 8, 8, 8, 4, movedTranslation, captureTranslation);
				if (obsolete.deferred) present.RequireFullDirtyRetry();
				Check(obsolete.deferred && present.ShouldPresent() && present.NeedsFullDirty()
					&& !present.HasFailureBackoff(),
					"new shape/display barrier skips obsolete ULW without consuming demand or reporting failure");
				Check(ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 7, 7, 8, 4, movedTranslation, captureTranslation).deferred
					&& ResolveBarBottomDockFramePresentation(
						6, captureTranslation, 6, 8, 8, 4, movedTranslation, captureTranslation).deferred,
					"publishing or changing transition tuple cannot submit mixed barrier state");
			}
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
	TestHorizontalCapturePresentedSequence();
	TestFirstCapturePresentationAcrossNewSamples();
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
		Check(ResolveBarBottomDockLine(
			monitor, RECT{ 100, 50, 2020, 1082 }, 5.0, 1.0) == 1125.0,
			"whiteboard inset uses monitor bottom minus five DIP");
		Check(ResolveBarBottomDockLine(
			monitor, RECT{ 100, 50, 2020, 1082 }, 5.0, 1.5) == 1122.0,
			"whiteboard inset converts DIP with monitor DPI only");
		Check(Near(ResolveBarBottomDockInsetPixels(5.0, 1.0), 5.0)
			&& Near(ResolveBarBottomDockInsetPixels(5.0, 1.5), 8.0),
			"five DIP inset matches page-bar physical margin");

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
	Check(!ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Centered, 100.0, 1000.0, false)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Centered, 900.0, 1000.0, true)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 900.0, 0.0, true)
		&& !ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 100.0,
			std::numeric_limits<double>::quiet_NaN(), false)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 900.0,
			std::numeric_limits<double>::infinity(), true)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 100.0, 1000.0, false)
		&& !ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 900.0, 1000.0, true),
		"centered and invalid-width position updates preserve the current layout side");
	const BarBottomDockFeedbackGeometry dockMainButtonBounds{
		-40.0, 608.0, 40.0, 688.0 };
	const BarBottomDockFeedbackGeometry dockMainBarBounds{
		-40.0, 608.0, 320.0, 688.0 };
	const auto dockIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 52.0, 14.0, 608.0);
	Check(Near(dockIndicator.leftDip, 106.0)
		&& Near(dockIndicator.topDip, 593.0)
		&& Near(dockIndicator.rightDip, 174.0)
		&& Near(dockIndicator.bottomDip, 623.0),
		"dock indicator uses equal text padding and centers on main bar top");
	const auto normalizedDockIndicator = ResolveBarBottomDockIndicatorGeometry(
		BarBottomDockFeedbackGeometry{
			std::numeric_limits<double>::quiet_NaN(), 30.0, -20.0, 10.0 },
		BarBottomDockFeedbackGeometry{ 40.0, 60.0, 10.0, 20.0 },
		52.0, 14.0, std::numeric_limits<double>::quiet_NaN());
	Check(Near(normalizedDockIndicator.leftDip, -24.0)
		&& Near(normalizedDockIndicator.topDip, 5.0)
		&& Near(normalizedDockIndicator.rightDip, 44.0)
		&& Near(normalizedDockIndicator.bottomDip, 35.0),
		"dock indicator normalizes non-finite and reversed visible bounds");
	for (double zoom : { 1.0, 1.5 })
	{
		const double centerPx = (dockIndicator.leftDip
			+ dockIndicator.rightDip) * zoom / 2.0;
		const double topPx = dockIndicator.topDip * zoom;
		const double widthPx = (dockIndicator.rightDip
			- dockIndicator.leftDip) * zoom;
		const double heightPx = (dockIndicator.bottomDip
			- dockIndicator.topDip) * zoom;
		Check(Near(centerPx, 140.0 * zoom)
			&& Near(topPx, 593.0 * zoom)
			&& Near(widthPx, 68.0 * zoom)
			&& Near(heightPx, 30.0 * zoom),
			"dock indicator center top width and height scale exactly once");
	}
	const auto actualWiderIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 120.0, 14.0, 608.0);
	Check(Near(actualWiderIndicator.rightDip - actualWiderIndicator.leftDip,
		136.0), "dock indicator expands only for the current language text");
	const auto invalidMetricsIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds,
		std::numeric_limits<double>::quiet_NaN(), -1.0, 608.0);
	const auto tallTextIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 52.0, 42.0, 608.0);
	Check(Near(invalidMetricsIndicator.rightDip
		- invalidMetricsIndicator.leftDip, 30.0)
		&& Near(tallTextIndicator.rightDip - tallTextIndicator.leftDip, 52.0),
		"dock indicator clamps invalid metrics and never uses negative padding");
	const auto scaledIndicator = ResolveBarBottomDockIndicatorScaledGeometry(
		dockIndicator, 1.1);
	const auto hiddenIndicator = ResolveBarBottomDockIndicatorScaledGeometry(
		dockIndicator, std::numeric_limits<double>::quiet_NaN());
	Check(Near(scaledIndicator.leftDip, 102.6)
		&& Near(scaledIndicator.topDip, 591.5)
		&& Near(scaledIndicator.rightDip, 177.4)
		&& Near(scaledIndicator.bottomDip, 624.5)
		&& Near(hiddenIndicator.leftDip, 140.0)
		&& Near(hiddenIndicator.topDip, 608.0)
		&& Near(hiddenIndicator.rightDip, 140.0)
		&& Near(hiddenIndicator.bottomDip, 608.0),
		"dock indicator scales equally around its center and hides at zero");
	const RECT indicatorVisualEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			dockIndicator, 1.1, 1.0, 6.0, 1.5, 3);
	const RECT indicatorBackPeakEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			dockIndicator, 1.2, 1.0, 6.0, 1.5, 3);
	Check(RectEquals(indicatorVisualEnvelope, RECT{ 139, 873, 281, 951 })
		&& RectEquals(indicatorBackPeakEnvelope,
			RECT{ 134, 871, 286, 953 })
		&& indicatorBackPeakEnvelope.top < indicatorVisualEnvelope.top,
		"indicator envelope covers Back overshoot stroke Gaussian and AA top edge");
	Check(RectEquals(ResolveBarBottomDockIndicatorVisualEnvelope(
		dockIndicator, std::numeric_limits<double>::quiet_NaN(),
		1.0, 6.0, 1.5, 3), RECT{}),
		"invalid indicator scale produces no visual envelope");
	const auto pulledVerticalMapping = ResolveBarBottomDockVerticalMapping(
		608.0, 688.0, -20.0);
	const auto pulledDockIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 52.0, 14.0,
		pulledVerticalMapping.MapY(dockMainBarBounds.topDip));
	const RECT pulledIndicatorEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			pulledDockIndicator, 1.0, 1.0, 6.0, 1.0, 3);
	const RECT stableIndicatorEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			dockIndicator, 1.0, 1.0, 6.0, 1.0, 3);
	BarDirtyRegionTracker indicatorDirtyTracker;
	constexpr std::uint64_t indicatorDirtyKey = 1;
	const RECT indicatorWindow{ 0, 0, 1920, 1080 };
	indicatorDirtyTracker.BeginFrame(indicatorWindow);
	indicatorDirtyTracker.Observe(indicatorDirtyKey, stableIndicatorEnvelope);
	indicatorDirtyTracker.CommitPresented();
	indicatorDirtyTracker.BeginFrame(indicatorWindow);
	indicatorDirtyTracker.MarkChanged(indicatorDirtyKey);
	indicatorDirtyTracker.Observe(indicatorDirtyKey, pulledIndicatorEnvelope);
	const RECT pulledIndicatorDamage =
		indicatorDirtyTracker.ResolveDamage(false);
	Check(pulledIndicatorEnvelope.top < stableIndicatorEnvelope.top
		&& pulledIndicatorDamage.top <= pulledIndicatorEnvelope.top
		&& pulledIndicatorDamage.bottom >= stableIndicatorEnvelope.bottom,
		"vertical dock jelly damages old and new indicator light envelopes");
	Check(IsBarBottomDockIndicatorHit(true, RECT{ 106, 593, 174, 623 }, 106, 593)
		&& !IsBarBottomDockIndicatorHit(
			true, RECT{ 106, 593, 174, 623 }, 174, 610)
		&& !IsBarBottomDockIndicatorHit(
			false, RECT{ 106, 593, 174, 623 }, 120, 610),
		"indicator hit test follows visible half-open presented bounds");
	Check(ResolveBarBottomDockIndicatorTarget(
		BarBottomDockMode::BottomDocked, true, true)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::BottomDocked, false, true)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::Floating, true, true)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::BottomDocked, true, false)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::BottomDocked, true, true, false),
		"dock indicator additionally requires a gesture eligibility latch");
	const auto floatingEntry =
		ResolveBarBottomDockIndicatorGestureEligibility(
			false, true, false, true, true);
	const auto centeredOriginDetach =
		ResolveBarBottomDockIndicatorGestureEligibility(
			false, false, true, true, true);
	const auto sameGestureRecapture =
		ResolveBarBottomDockIndicatorGestureEligibility(
			centeredOriginDetach.gestureActive, false, true, true, true);
	const auto sameGestureUnchanged =
		ResolveBarBottomDockIndicatorGestureEligibility(
			sameGestureRecapture.gestureActive, false, false, true, true);
	const auto leftBottomDock =
		ResolveBarBottomDockIndicatorGestureEligibility(
			sameGestureRecapture.gestureActive, false, false, false, true);
	const auto foldedGesture =
		ResolveBarBottomDockIndicatorGestureEligibility(
			true, false, false, true, false);
	const auto unchangedBottomGesture =
		ResolveBarBottomDockIndicatorGestureEligibility(
			false, false, false, true, true);
	Check(floatingEntry.gestureActive && floatingEntry.eligible
		&& centeredOriginDetach.gestureActive && centeredOriginDetach.eligible
		&& sameGestureRecapture.gestureActive && sameGestureRecapture.eligible
		&& sameGestureUnchanged.gestureActive && sameGestureUnchanged.eligible
		&& !leftBottomDock.gestureActive && !leftBottomDock.eligible
		&& !foldedGesture.gestureActive && !foldedGesture.eligible
		&& !unchangedBottomGesture.gestureActive
		&& !unchangedBottomGesture.eligible,
		"indicator latches on bottom entry or either center-mode transition only");
	Check(ResolveBarBottomDockFeedbackAction(false, true)
		== BarBottomDockFeedbackAction::FadeIn
		&& ResolveBarBottomDockFeedbackAction(true, false)
			== BarBottomDockFeedbackAction::FadeOut
		&& ResolveBarBottomDockFeedbackAction(true, true)
			== BarBottomDockFeedbackAction::None,
		"feedback animations reverse from their current visibility target");
	Check(ResolveBarBottomDockIndicatorContentAction(48.0, 96.0, false)
		== BarBottomDockIndicatorContentAction::ExpandFrame
		&& ResolveBarBottomDockIndicatorContentAction(96.0, 48.0, false)
			== BarBottomDockIndicatorContentAction::SwapText
		&& ResolveBarBottomDockIndicatorContentAction(96.0, 48.0, true)
			== BarBottomDockIndicatorContentAction::ShrinkFrame
		&& ResolveBarBottomDockIndicatorContentAction(48.0, 48.0, true)
			== BarBottomDockIndicatorContentAction::None,
		"indicator expands before swapping wider text and shrinks only after swap");

	BarBottomDockEnvironment environment{
		RECT{ 0, 0, 1920, 1080 }, RECT{ 0, 0, 1920, 1032 }, 1.5 };
	BarBottomDockDragTracker tracker;
	tracker.Begin(BarBottomDockMode::Floating,
		951.0, 1002.0, 981.0, environment);
	auto capturedAbove = tracker.Update(966.0, 1017.0, environment);
	Check(capturedAbove.captured
		&& capturedAbove.mode == BarBottomDockMode::BottomDocked
		&& capturedAbove.phase == BarBottomDockPhase::Capturing
		&& Near(capturedAbove.elasticOffsetDip, -10.0),
		"approach from above captures with stretch offset");
	auto draggingAfterCapture = tracker.Update(966.0, 1017.0, environment);
	Check(draggingAfterCapture.phase == BarBottomDockPhase::Dragging,
		"capturing advances to dragging on the next sample");
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

	BarBottomDockCenterDragTracker centerTracker;
	centerTracker.Begin(BarBottomDockCenterMode::Free, 880.0,
		true, true, environment);
	auto exactCenterCapture = centerTracker.Update(1020.0,
		true, true, environment);
	Check(exactCenterCapture.captured
		&& exactCenterCapture.mode == BarBottomDockCenterMode::Centered
		&& Near(exactCenterCapture.constrainedCenterScreenX, 960.0)
		&& Near(exactCenterCapture.elasticOffsetDip, 40.0),
		"horizontal center capture includes the exact forty DIP boundary");
	auto exactCenterDetach = centerTracker.Update(1020.0,
		true, true, environment);
	Check(!exactCenterDetach.detached
		&& exactCenterDetach.mode == BarBottomDockCenterMode::Centered,
		"horizontal center remains captured at the inclusive boundary");
	auto outsideCenterDetach = centerTracker.Update(1020.01,
		true, true, environment);
	Check(outsideCenterDetach.detached
		&& outsideCenterDetach.mode == BarBottomDockCenterMode::Free,
		"horizontal center detaches strictly outside forty DIP");
	centerTracker.Begin(BarBottomDockCenterMode::Free, 880.0,
		true, true, environment);
	const auto stableCapture = centerTracker.Update(950.0,
		true, true, environment);
	const auto repeatedInside = centerTracker.Update(950.0,
		true, true, environment);
	const auto pushedInside = centerTracker.Update(970.0,
		true, true, environment);
	const auto stableDetach = centerTracker.Update(1021.0,
		true, true, environment);
	const auto stableRecapture = centerTracker.Update(1019.0,
		true, true, environment);
	Check(stableCapture.mode == BarBottomDockCenterMode::Centered
		&& repeatedInside.mode == BarBottomDockCenterMode::Centered
		&& !repeatedInside.modeChanged
		&& pushedInside.mode == BarBottomDockCenterMode::Centered
		&& stableDetach.mode == BarBottomDockCenterMode::Free
		&& stableRecapture.mode == BarBottomDockCenterMode::Centered,
		"raw pointer samples do not oscillate after a presented barrier");
	BarBottomDockCenterDragTracker foldedCenterTracker;
	foldedCenterTracker.Begin(BarBottomDockCenterMode::Free, 960.0,
		true, false, environment);
	const auto foldedCenter = foldedCenterTracker.Update(960.0,
		true, false, environment);
	Check(foldedCenter.mode == BarBottomDockCenterMode::Free
		&& !foldedCenter.captured
		&& !ShouldCaptureBarBottomDockCenter(
			true, false, 960.0, environment.monitorBounds, environment.zoom),
		"folded main bar cannot enter centered mode");
	Check(ShouldCaptureBarBottomDockCenter(
		true, true, 900.0, environment.monitorBounds, environment.zoom)
		&& ShouldCaptureBarBottomDockCenter(
			true, true, 960.0, environment.monitorBounds, environment.zoom)
		&& ShouldCaptureBarBottomDockCenter(
		true, true, 1020.0, environment.monitorBounds, environment.zoom)
		&& !ShouldCaptureBarBottomDockCenter(
			true, true, 899.99, environment.monitorBounds, environment.zoom)
		&& !ShouldCaptureBarBottomDockCenter(
			true, true, 1020.01, environment.monitorBounds, environment.zoom)
		&& !ShouldCaptureBarBottomDockCenter(
			false, true, 960.0, environment.monitorBounds, environment.zoom),
		"horizontal capture uses the forty DIP monitor-center band and dock gate");

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
	const auto rightExpandedStretch = ResolveBarBottomDockHorizontalMapping(
		180.0, 500.0, true, -20.0);
	const auto rightExpandedCompress = ResolveBarBottomDockHorizontalMapping(
		180.0, 500.0, true, 20.0);
	const auto leftExpandedStretch = ResolveBarBottomDockHorizontalMapping(
		100.0, 420.0, false, 20.0);
	Check(Near(rightExpandedStretch.visualLeftDip, 160.0)
		&& Near(rightExpandedStretch.visualRightDip, 500.0)
		&& Near(rightExpandedCompress.visualLeftDip, 200.0)
		&& Near(rightExpandedCompress.visualRightDip, 500.0)
		&& Near(leftExpandedStretch.visualLeftDip, 100.0)
		&& Near(leftExpandedStretch.visualRightDip, 440.0)
		&& Near(rightExpandedStretch.rigidGripTranslationXDip, -20.0),
		"horizontal jelly moves the near edge with the rigid grip and fixes the far edge");
	Check(ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			false, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Free,
			BarBottomDockPhase::Stable, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Capturing, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, true, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, false,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			true, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, true, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, false, true),
		"only a stable idle centered bottom dock derives the root position");
	const auto centeredRight = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 80.0, 250.0, 400.0);
	const auto centeredLeft = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 80.0, -250.0, 400.0);
	const auto centeredStroke = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 82.0, 251.0, 402.0);
	const auto invalidCenteredRoot = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 0.0, 250.0, 400.0);
	const auto centeredRootRange = ResolveBarBottomDockCenteredRootRange(
		960.0, 82.0, 82.0, 150.0, 251.0, 202.0, 402.0);
	const auto invalidCenteredRootRange = ResolveBarBottomDockCenteredRootRange(
		960.0, 82.0, 82.0, 251.0, 150.0, 202.0, 402.0);
	Check(centeredRight.valid && Near(centeredRight.mainCenterDip, 755.0)
		&& Near(centeredRight.bodyLeftDip, 715.0)
		&& Near(centeredRight.bodyRightDip, 1205.0)
		&& Near((centeredRight.bodyLeftDip + centeredRight.bodyRightDip)
			/ 2.0, 960.0)
		&& centeredLeft.valid && Near(centeredLeft.mainCenterDip, 1165.0)
		&& Near(centeredLeft.bodyLeftDip, 715.0)
		&& Near(centeredLeft.bodyRightDip, 1205.0),
		"centered root preserves the visible union center on either side");
	Check(centeredStroke.valid
		&& Near(centeredStroke.mainCenterDip, 754.5)
		&& Near((centeredStroke.bodyLeftDip + centeredStroke.bodyRightDip)
			/ 2.0, 960.0)
		&& !invalidCenteredRoot.valid,
		"centered root includes visible strokes and rejects invalid geometry");
	Check(centeredRootRange.valid
		&& Near(centeredRootRange.minimumDip, 754.5)
		&& Near(centeredRootRange.maximumDip, 860.0)
		&& centeredStroke.mainCenterDip >= centeredRootRange.minimumDip
		&& centeredStroke.mainCenterDip <= centeredRootRange.maximumDip
		&& !invalidCenteredRootRange.valid,
		"centered root range conservatively reserves the full animated parent travel");
	Check(ResolveBarBottomDockInitialMainBarSide(false, false)
		&& ResolveBarBottomDockInitialMainBarSide(false, true)
		&& !ResolveBarBottomDockInitialMainBarSide(true, false)
		&& ResolveBarBottomDockInitialMainBarSide(true, true),
		"desktop initial placement opens right while whiteboard preserves its side");
	const auto horizontalCaptureStart = ResolveBarBottomDockHorizontalMapping(
		180.0, 500.0, true, -20.0, -20.0);
	const double detachedFarOffset = ResolveBarBottomDockRebasedFarEdgeOffsetDip(
		rightExpandedCompress.visualRightDip, 0.0, 20.0, 500.0, 1.0);
	const auto horizontalDetachStart =
		ResolveBarBottomDockRecoveringHorizontalMapping(
			180.0, 500.0, true, detachedFarOffset);
	Check(Near(horizontalCaptureStart.visualLeftDip, 160.0)
		&& Near(horizontalCaptureStart.visualRightDip, 480.0)
		&& Near(horizontalCaptureStart.rigidGripTranslationXDip, -20.0)
		&& Near(horizontalDetachStart.visualLeftDip + 20.0,
			rightExpandedCompress.visualLeftDip)
		&& Near(horizontalDetachStart.visualRightDip + 20.0,
			rightExpandedCompress.visualRightDip),
		"horizontal capture and detach preserve the previous screen-space pixels");

	const RECT logicalBody{ 10, 0, 110, 80 };
	Check(RectEquals(TransformBarBottomDockBodyRect(
		logicalBody, compressed, 1.5), RECT{ 10, 30, 110, 90 }),
		"body dirty bounds use the same endpoint mapping as drawing");
	Check(RectEquals(TransformBarBottomDockBodyRect(
		RECT{ 15, 0, 165, 120 },
		ResolveBarBottomDockHorizontalMapping(10.0, 110.0, true, 20.0),
		compressed, 1.5), RECT{ 45, 30, 165, 120 }),
		"two-axis dirty bounds use the same composed mapping as drawing");
	Check(RectEquals(TransformBarBottomDockGripRect(
		logicalBody,
		ResolveBarBottomDockHorizontalMapping(10.0, 110.0, true, 20.0),
		compressed, 1.5), RECT{ 40, 30, 140, 90 }),
		"main grip keeps its horizontal size while composing vertical jelly");
	Check(Near(UnmapBarBottomDockGripPixelX(
		90.0, ResolveBarBottomDockHorizontalMapping(
			10.0, 110.0, true, 20.0), 1.5), 60.0),
		"main grip hit testing removes only the rigid horizontal translation");
	Check(RectEquals(TranslateBarBottomDockRigidRect(
		logicalBody, 20.0, 1.5), RECT{ 10, 30, 110, 110 }),
		"rigid panel dirty bounds only translate");
	Check(RectEquals(TranslateBarBottomDockRigidRect(
		logicalBody, -20.0, 20.0, 1.5), RECT{ -20, 30, 80, 110 }),
		"rigid panel dirty bounds compose horizontal and vertical translation");
	Check(RectEquals(ResolveBarBottomDockVisualEnvelope(
		RECT{ 10, 20, 110, 120 }, 1.5), RECT{ -26, -16, 146, 156 }),
		"elastic capacity reserves the full two-axis twenty-four DIP envelope once");
	Check(RectEquals(ResolveBarBottomDockCapacityEnvelope(
		RECT{ 10, 50, 110, 150 }, 1.5),
		RECT{ -26, 14, 146, 186 }),
		"capacity envelope is derived from the untransformed baseline");
	Check(ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
		true, 1040.0, 1039.25, 1039.0)
		&& !ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
			true, 1040.0, 1039.25, 1000.0)
		&& !ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
			false, 1040.0, 1039.25, 1039.0),
		"downward release re-docks only when the visible constraint blocks it");
	const POINT latestTranslation{ 30, 40 };
	const POINT presentedTranslation{ 17, 23 };
	const POINT currentFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 4, 4, latestTranslation, presentedTranslation);
	Check(currentFrameTranslation.x == 30 && currentFrameTranslation.y == 40,
		"current transition frame may consume the latest drag translation");
	const POINT staleFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 6, 6, latestTranslation, presentedTranslation);
	const POINT publishingFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 5, 5, latestTranslation, presentedTranslation);
	Check(staleFrameTranslation.x == 17 && staleFrameTranslation.y == 23
		&& publishingFrameTranslation.x == 17
		&& publishingFrameTranslation.y == 23,
		"stale or in-flight transition frames retain the actual HWND translation");
	Check(ShouldDeferBarBottomDockReleaseHandoff(false, true, true)
		&& !ShouldDeferBarBottomDockReleaseHandoff(true, true, true)
		&& !ShouldDeferBarBottomDockReleaseHandoff(false, false, true)
		&& !ShouldDeferBarBottomDockReleaseHandoff(false, true, false),
		"release waits only while pending direct translation still owns the window");
	const double mappedHorizontalSample = MapBarBottomDockBodyPixelX(
		315.0, rightExpandedStretch, 1.5);
	const double mappedSample = MapBarBottomDockBodyPixelY(
		67.5, stretched, 1.5);
	Check(Near(UnmapBarBottomDockBodyPixelX(
			mappedHorizontalSample, rightExpandedStretch, 1.5), 315.0)
		&& Near(UnmapBarBottomDockBodyPixelY(
			mappedSample, stretched, 1.5), 67.5),
		"body hit testing inverts both non-identity drawing axes");
	const BarBottomDockHorizontalMapping hitHorizontal{
		10.0, 110.0, 30.0, 90.0, 0.6, 12.0, -8.0 };
	const BarBottomDockVerticalMapping hitVertical{
		20.0, 100.0, 35.0, 95.0, 0.75, 0.0, 6.0 };
	const auto rigidHit = ResolveBarBottomDockRigidHitTestPoint(
		180.0, 150.0, hitHorizontal, hitVertical, 1.5);
	const auto bodyHit = ResolveBarBottomDockBodyHitTestPointFromRigid(
		rigidHit.logicalX, rigidHit.logicalY,
		hitHorizontal, hitVertical, 1.5);
	const auto gripHit = ResolveBarBottomDockGripHitTestPointFromRigid(
		rigidHit.logicalX, rigidHit.logicalY,
		hitHorizontal, hitVertical, 1.5);
	Check(Near(rigidHit.logicalX, 192.0)
		&& Near(rigidHit.logicalY, 141.0)
		&& Near(bodyHit.visualX, 180.0)
		&& Near(bodyHit.visualY, 150.0)
		&& Near(bodyHit.logicalX,
			UnmapBarBottomDockBodyPixelX(180.0, hitHorizontal, 1.5))
		&& Near(bodyHit.logicalY,
			UnmapBarBottomDockBodyPixelY(150.0, hitVertical, 1.5))
		&& Near(gripHit.logicalX,
			UnmapBarBottomDockGripPixelX(180.0, hitHorizontal, 1.5))
		&& Near(gripHit.logicalY, bodyHit.logicalY),
		"combined body and grip hit tests map both axes from one tuple");
	const auto bodyLight = ResolveBarBottomDockBodyLocalLight(
		240.0, mappedSample, 90.0, stretched, 1.5);
	Check(Near(MapBarBottomDockBodyPixelY(
		bodyLight.centerY, stretched, 1.5), mappedSample)
		&& Near(bodyLight.centerX, 240.0)
		&& Near(bodyLight.radiusY * stretched.scaleY, 90.0),
		"body cursor light inverse mapping preserves its visual center and radius");
	const auto rigidLight = ResolveBarBottomDockRigidLocalLight(
		240.0, 315.0, 90.0, -20.0, 20.0, 1.5);
	Check(Near(rigidLight.centerY + 20.0 * 1.5, 315.0)
		&& Near(rigidLight.centerX - 20.0 * 1.5, 240.0)
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
