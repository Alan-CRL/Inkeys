#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

#include <json/json.h>

import Inkeys.Other.Config;
import Inkeys.UI.StartupPreview;

// Config 生产模块通过 IdtMain.h 引用该路径；headless fixture 将其限定到临时目录。
std::wstring globalPath;
std::wstring editionVersion;
std::wstring editionDate;
std::wstring userId;
std::wstring programArchitecture;
std::wstring targetArchitecture;
std::wstring windowsEdition;

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition)
			std::cerr << "[StartupPreviewState] failed: " << name << '\n';
		return condition;
	}

	struct LegacyConfigResult final
	{
		bool read = false;
		bool enabled = true;
		double widthDip = 0.0;
		double zoom = 0.0;
	};

	LegacyConfigResult ReadLegacyStartupPreviewFixture()
	{
		namespace fs = std::filesystem;
		const fs::path root = fs::temp_directory_path()
			/ (L"inkeys-startup-preview-config-"
				+ std::to_wstring(GetCurrentProcessId()) + L"-"
				+ std::to_wstring(GetTickCount64()));
		struct Cleanup final
		{
			fs::path root;
			std::wstring previousGlobalPath;
			~Cleanup()
			{
				globalPath = previousGlobalPath;
				std::error_code ignored;
				fs::remove_all(root, ignored);
			}
		} cleanup{ root, globalPath };

		std::error_code error;
		const fs::path configPath = root / L"Inkeys" / L"Config" / L"main.json";
		fs::create_directories(configPath.parent_path(), error);
		if (error) return {};

		Json::Value document(Json::objectValue);
		auto& preview = document["Experimental"]["Inkeys3"]["UI3"]
			["StartupPreview"];
		preview["Enable"] = false;
		// 模拟旧图片方案遗留字段；新 schema 应忽略它且补上缺失宽度默认值。
		preview["RetiredField"] = "ignored";
		document["UI"]["Bar"]["Zoom"] = 1.25;
		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		{
			std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
			if (!output) return {};
			output << Json::writeString(writer, document);
			if (!output) return {};
		}

		globalPath = root.wstring() + L"\\";
		Inkeys::Config config;
		const bool read = config.ReadMini({
			"Experimental.Inkeys3.UI3.StartupPreview.Enable",
			"Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip",
			"UI.Bar.Zoom" });
		return {
			read,
			config.Experimental.Inkeys3.UI3.StartupPreview.Enable.load(),
			config.Experimental.Inkeys3.UI3.StartupPreview.
				CachedStartupBarWidthDip.load(),
			config.UI.Bar.Zoom.load(),
		};
	}
}

int RunStartupPreviewStateTests()
{
	using namespace Inkeys::UI::StartupPreview;
	using namespace std::chrono_literals;
	int failures = 0;

	if (!Expect(DefaultMainBarBodyWidthDip == 380.0
		&& DefaultCachedStartupBarWidthDip == 470.0
		&& DefaultStartupBarHeightDip == 80.0
		&& DefaultStartupBarCornerRadiusDip == 8.0
		&& StartupPreviewSurfaceColorChannel == 24.0 / 255.0
		&& StartupPreviewSurfaceFrameColorChannel == 1.0
		&& StartupPreviewFillAlpha == 0.8
		&& StartupPreviewFrameAlpha == 0.18
		&& StartupPreviewEnabledByDefault
		&& CalculateStartupBarTotalWidthDip(80.0, 380.0) == 470.0,
		"preview uses MainBar dark surface visuals and derives 380/470 DIP geometry"))
		++failures;
	if (!Expect(CalculateStartupBarTotalWidthDip(80.0, 380.0, false) == 80.0,
		"collapsed geometry publishes only the main button width")) ++failures;
	for (const double invalid : { 0.0, -1.0, 79.99, 4096.01,
		(std::numeric_limits<double>::quiet_NaN)(),
		(std::numeric_limits<double>::infinity)(),
		-(std::numeric_limits<double>::infinity)() })
		if (!Expect(ResolveCachedStartupBarWidthDip(invalid)
			== DefaultCachedStartupBarWidthDip,
			"invalid cached width falls back to 470 DIP")) ++failures;
	if (!Expect(ResolveCachedStartupBarWidthDip(80.0) == 80.0
		&& ResolveCachedStartupBarWidthDip(612.5) == 612.5,
		"valid cached width remains an unscaled DIP value")) ++failures;
	const auto legacyConfig = ReadLegacyStartupPreviewFixture();
	if (!Expect(legacyConfig.read && !legacyConfig.enabled
		&& legacyConfig.widthDip == DefaultCachedStartupBarWidthDip
		&& legacyConfig.zoom == 1.25,
		"legacy main.json without cached width keeps the 470 DIP default"))
		++failures;
	if (!Expect(!ShouldWriteCachedStartupBarWidthDip(470.0, 470.0)
		&& !ShouldWriteCachedStartupBarWidthDip(470.0, 470.005)
		&& ShouldWriteCachedStartupBarWidthDip(470.0, 545.0)
		&& !ShouldWriteCachedStartupBarWidthDip(470.0,
			(std::numeric_limits<double>::quiet_NaN)()),
		"cached width write suppresses noise and rejects invalid candidates")) ++failures;

	const auto pixels96 = ResolveStartupPreviewPixelSize(470.0, 96.0, 1.0);
	const auto pixels120 = ResolveStartupPreviewPixelSize(470.0, 120.0, 1.25);
	const auto pixels144 = ResolveStartupPreviewPixelSize(470.0, 144.0, 1.5);
	if (!Expect(pixels96.width == 470 && pixels96.height == 80
		&& pixels120.width == 734 && pixels120.height == 125
		&& pixels144.width == 1058 && pixels144.height == 180,
		"DPI times Bar zoom uses consistent rounding")) ++failures;
	if (!Expect(ResolveStartupPreviewPixelSize(
		(std::numeric_limits<double>::quiet_NaN)(), 96.0, 1.0).width == 470,
		"invalid cached width still produces default preview pixels")) ++failures;

	const RECT negativeMonitor{ -1920, 0, 0, 1080 };
	const RECT negativeWorkArea{ -1920, 0, 0, 1040 };
	const RECT centered = ResolveStartupPreviewBounds(
		negativeMonitor, negativeWorkArea, 470.0, 80.0, 96, 1.0);
	if (!Expect(centered.left == -1195 && centered.right == -725
		&& centered.top == 960 && centered.bottom == 1040,
		"complete placeholder envelope stays centered in negative work area")) ++failures;
	const RECT sideTaskbarWorkArea{ -1870, 0, 0, 1080 };
	const RECT sideTaskbar = ResolveStartupPreviewBounds(
		negativeMonitor, sideTaskbarWorkArea, 470.0, 80.0, 96, 1.0);
	if (!Expect(sideTaskbar.left == -1195 && sideTaskbar.right == -725
		&& sideTaskbar.top == 1000 && sideTaskbar.bottom == 1080,
		"side taskbar keeps the envelope centered on the monitor")) ++failures;

	for (const auto pixels : { pixels96, pixels120, pixels144 })
	{
		const GeometryRect bounds{ 0.0, 0.0,
			static_cast<double>(pixels.width), static_cast<double>(pixels.height) };
		const auto gradient = ResolveStartupPreviewShimmerGradient(
			static_cast<double>(pixels.width), static_cast<double>(pixels.height));
		const double supportLength = std::hypot(gradient.endX - gradient.startX,
			gradient.endY - gradient.startY);
		const auto travel = ResolveShimmerTravel(bounds, gradient,
			(std::max)(8.0, supportLength * 0.10));
		const auto startTranslation = ResolveShimmerTranslation(travel, 0.0);
		const auto middleTranslation = ResolveShimmerTranslation(travel, 0.5);
		const auto endTranslation = ResolveShimmerTranslation(travel, 1.0);
		if (!Expect(travel.valid
			&& gradient.endX > gradient.startX
			&& gradient.endY > gradient.startY
			&& startTranslation.x < endTranslation.x
			&& startTranslation.y < endTranslation.y
			&& IsShimmerSupportOutsideMask(bounds, gradient,
				startTranslation.x, startTranslation.y)
			&& !IsShimmerSupportOutsideMask(bounds, gradient,
				middleTranslation.x, middleTranslation.y)
			&& IsShimmerSupportOutsideMask(bounds, gradient,
				endTranslation.x, endTranslation.y),
			"diagonal shimmer soft-tail support is offscreen at endpoints and crosses the mask")) ++failures;
	}
	const auto shimmerEpoch = std::chrono::steady_clock::time_point(100h);
	const auto shimmerPeriod =
		std::chrono::duration<double>(StartupPreviewShimmerCycleSeconds);
	const auto quarterSweepCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 700ms, shimmerEpoch, shimmerPeriod);
	const auto middleSweepCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 1400ms, shimmerEpoch, shimmerPeriod);
	const auto threeQuarterSweepCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 2100ms, shimmerEpoch, shimmerPeriod);
	const auto sweepEndCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 2800ms, shimmerEpoch, shimmerPeriod);
	const auto holdCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 3400ms, shimmerEpoch, shimmerPeriod);
	const auto beforeWrapCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 3999ms, shimmerEpoch, shimmerPeriod);
	const auto wrappedCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 4000ms, shimmerEpoch, shimmerPeriod);
	const GeometryRect cycleBounds{ 0.0, 0.0, 470.0, 80.0 };
	const auto cycleGradient = ResolveStartupPreviewShimmerGradient(470.0, 80.0);
	const double cycleSupportLength = std::hypot(
		cycleGradient.endX - cycleGradient.startX,
		cycleGradient.endY - cycleGradient.startY);
	const auto cycleTravel = ResolveShimmerTravel(cycleBounds, cycleGradient,
		(std::max)(8.0, cycleSupportLength * 0.10));
	const auto sweepEndTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(sweepEndCycle));
	const auto holdTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(holdCycle));
	const auto beforeWrapTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(beforeWrapCycle));
	const auto wrappedTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(wrappedCycle));
	if (!Expect(std::abs(sweepEndCycle - 0.70) < 0.000001
		&& std::abs(holdCycle - 0.85) < 0.000001
		&& beforeWrapCycle > 0.99 && wrappedCycle == 0.0
		&& EaseShimmerPhase(quarterSweepCycle) < 0.25
		&& std::abs(EaseShimmerPhase(middleSweepCycle) - 0.5) < 0.000001
		&& EaseShimmerPhase(threeQuarterSweepCycle) > 0.75
		&& sweepEndTranslation.x == holdTranslation.x
		&& sweepEndTranslation.y == holdTranslation.y
		&& IsShimmerSupportOutsideMask(cycleBounds, cycleGradient,
			sweepEndTranslation.x, sweepEndTranslation.y)
		&& IsShimmerSupportOutsideMask(cycleBounds, cycleGradient,
			beforeWrapTranslation.x, beforeWrapTranslation.y)
		&& IsShimmerSupportOutsideMask(cycleBounds, cycleGradient,
			wrappedTranslation.x, wrappedTranslation.y),
		"shimmer eases its sweep and holds fully offscreen between passes"))
		++failures;

	std::uint8_t previous = 0;
	for (int elapsed = 0; elapsed <= 300; ++elapsed)
	{
		const auto alpha = ResolveFadeInAlpha(
			std::chrono::milliseconds(elapsed), StartupPreviewFadeInDuration);
		if (!Expect(alpha >= previous, "preview fade-in alpha is monotonic")) ++failures;
		previous = alpha;
	}
	if (!Expect(ResolveFadeInAlpha(0ms, StartupPreviewFadeInDuration) == 0
		&& ResolveFadeInAlpha(300ms, StartupPreviewFadeInDuration) == 255
		&& ResolveFadeOutAlpha(0ms, StartupPreviewQuickHandoffDuration) == 255
		&& ResolveFadeOutAlpha(300ms, StartupPreviewQuickHandoffDuration) == 0
		&& ResolveFadeOutAlphaFrom(96, 0ms,
			StartupPreviewProgressHandoffDuration) == 96
		&& ResolveFadeOutAlphaFrom(96, 1000ms,
			StartupPreviewProgressHandoffDuration) == 0
		&& ResolveFadeOutAlpha(159ms,
			StartupPreviewExitFadeOutDuration) > 0
		&& ResolveFadeOutAlpha(160ms,
			StartupPreviewExitFadeOutDuration) == 0,
		"fade helpers have exact endpoints")) ++failures;

	OrderedHandoffReducer handoff;
	if (!Expect(handoff.PreviewFirstAlpha0Committed().state
		== HandoffState::PreviewVisible,
		"alpha-zero preview commit starts visible phase")) ++failures;
	if (!Expect(!handoff.BarAlpha0Committed(false).requestPreviewFadeOut
		&& handoff.State() == HandoffState::WaitingForBar,
		"Bar alpha zero waits for real startup completion")) ++failures;
	const auto fadePreview = handoff.BarAlpha0Committed(true);
	if (!Expect(fadePreview.state == HandoffState::PreviewFadeOut
		&& fadePreview.requestPreviewFadeOut && !fadePreview.requestBarFadeIn,
		"successful handoff fades Preview first")) ++failures;
	if (!Expect(!handoff.BarAlpha255Committed().destroyPreview
		&& handoff.State() == HandoffState::PreviewFadeOut,
		"Bar cannot skip Preview fade-out")) ++failures;
	const auto fadeBar = handoff.PreviewFadeOutCommitted();
	if (!Expect(fadeBar.state == HandoffState::WaitingForBarOpaque
		&& fadeBar.requestBarFadeIn && !fadeBar.destroyPreview,
		"Preview zero commit starts Bar fade-in")) ++failures;
	if (!Expect(handoff.BarAlpha255Committed().destroyPreview
		&& handoff.State() == HandoffState::Stopped,
		"Preview destruction waits for Bar alpha 255 commit")) ++failures;
	OrderedHandoffReducer recovery;
	const auto bypass = recovery.Bypass();
	if (!Expect(bypass.state == HandoffState::Bypassed
		&& bypass.requestBarFadeIn && !bypass.destroyPreview,
		"failure recovery first requests opaque Bar")) ++failures;
	if (!Expect(recovery.BarAlpha255Committed().destroyPreview
		&& recovery.State() == HandoffState::Stopped,
		"failure cleanup waits for Bar alpha 255")) ++failures;

	HandoffTimingReducer quickTiming;
	quickTiming.PreviewFrameCommitted(true, 0);
	quickTiming.PreviewFrameCommitted(false, 255);
	if (!Expect(quickTiming.FreezeDuration()
			== StartupPreviewQuickHandoffDuration
		&& !quickTiming.ProgressEverCommittedVisible(),
		"uncommitted or invisible progress keeps the 300 ms handoff")) ++failures;
	quickTiming.PreviewFrameCommitted(true, 255);
	if (!Expect(quickTiming.FreezeDuration()
			== StartupPreviewQuickHandoffDuration
		&& !quickTiming.ProgressEverCommittedVisible(),
		"handoff timing stays frozen after handoff starts")) ++failures;
	HandoffTimingReducer progressTiming;
	progressTiming.PreviewFrameCommitted(true, 1);
	if (!Expect(progressTiming.ProgressEverCommittedVisible()
			&& progressTiming.FreezeDuration()
				== StartupPreviewProgressHandoffDuration,
		"a committed visible progress frame selects the 1000 ms handoff"))
		++failures;

	if (!Expect(IsBarStartupReady(BarStartupState::FirstFrameCommitted)
		&& !IsBarStartupReady(BarStartupState::RenderClientRegistered)
		&& IsBarStartupFailure(BarStartupState::WindowMissing)
		&& IsBarStartupFailure(BarStartupState::StoppedBeforeReady),
		"Bar readiness distinguishes committed and terminal states")) ++failures;

	const auto start = std::chrono::steady_clock::time_point(10s);
	ProgressVisualReducer zeroEpochProgress;
	(void)zeroEpochProgress.Update({}, 0.4, false, false);
	auto zeroEpochVisual = zeroEpochProgress.Update(
		std::chrono::steady_clock::time_point(150ms), 0.4, false, false);
	if (!Expect(std::abs(zeroEpochVisual.displayedRatio - 0.2) < 0.000001,
		"progress animation supports a zero steady-clock epoch")) ++failures;
	ProgressVisualReducer progress;
	progress.MarkPreviewShown(start);
	auto visual = progress.Update(start, 0.4, false, false);
	if (!Expect(visual.displayedRatio == 0.0,
		"progress starts its own time-based animation without jumping")) ++failures;
	visual = progress.Update(start + 150ms, 0.4, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.2) < 0.000001,
		"progress ratio uses smoothstep at the animation midpoint")) ++failures;
	ProgressVisualReducer frameIndependent;
	frameIndependent.MarkPreviewShown(start);
	(void)frameIndependent.Update(start, 0.4, false, false);
	const auto quarterVisual = frameIndependent.Update(
		start + 75ms, 0.4, false, false);
	if (!Expect(quarterVisual.displayedRatio < 0.1,
		"progress smoothstep starts slower than linear interpolation")) ++failures;
	const auto independentVisual = frameIndependent.Update(
		start + 150ms, 0.4, false, false);
	if (!Expect(std::abs(independentVisual.displayedRatio
			- visual.displayedRatio) < 0.000001,
		"progress interpolation is frame-rate independent")) ++failures;
	visual = progress.Update(start + 150ms, 0.8, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.2) < 0.000001,
		"progress retarget keeps the current displayed ratio continuous")) ++failures;
	visual = progress.Update(start + 300ms, 0.8, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.5) < 0.000001,
		"retargeted progress continues on a fresh smooth timeline")) ++failures;
	visual = progress.Update(start + 2999ms, 0.8, false, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden
		&& visual.opacity == 0.0 && visual.displayedRatio <= 0.8,
		"progress stays hidden for 2999 ms")) ++failures;
	visual = progress.Update(start + 3s, 0.8, false, false);
	if (!Expect(visual.state == ProgressVisualState::FadingIn
		&& visual.displayedRatio <= 0.8,
		"three-second gate starts progress fade")) ++failures;
	visual = progress.Update(start + 3180ms, 0.8, false, false);
	if (!Expect(visual.state == ProgressVisualState::Visible
		&& visual.opacity == 1.0 && visual.displayedRatio <= 0.8,
		"progress becomes visible after its opacity gate")) ++failures;
	visual = progress.Update(start + 3181ms, 1.0, true, false);
	if (!Expect(visual.state == ProgressVisualState::Visible
		&& visual.displayedRatio == 0.8 && visual.opacity == 1.0,
		"completion retargets to 100 percent without jumping")) ++failures;
	const double fadeStartRatio = visual.displayedRatio;
	visual = progress.Update(start + 3331ms, 1.0, true, false);
	if (!Expect(visual.displayedRatio > fadeStartRatio
			&& visual.displayedRatio < 1.0
			&& progressTiming.FreezeDuration()
				== StartupPreviewProgressHandoffDuration,
		"progress keeps advancing during the frozen 1000 ms handoff fade"))
		++failures;
	visual = progress.Update(start + 3481ms, 1.0, true, false);
	if (!Expect(visual.displayedRatio == 1.0,
		"completed progress reaches 100 percent on its own timeline")) ++failures;
	ProgressVisualReducer boundedCompletion;
	(void)boundedCompletion.Update(start, 0.6, false, false);
	visual = boundedCompletion.Update(start + 300ms, 0.6, true, false);
	if (!Expect(visual.displayedRatio == 0.6,
		"completed progress never exceeds the supplied actual ratio")) ++failures;
	ProgressVisualReducer quick;
	quick.MarkPreviewShown(start);
	visual = quick.Update(start + 2900ms, 1.0, true, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden
		&& visual.opacity == 0.0 && visual.displayedRatio == 0.0,
		"quick startup never shows progress")) ++failures;
	ProgressVisualReducer retry;
	retry.MarkPreviewShown(start);
	(void)retry.Update(start, 0.2, false, false);
	visual = retry.Update(start + 300ms, 0.2, false, false);
	visual = retry.Update(start + 4s, 0.2, false, false);
	if (!Expect(visual.failureColorProgress == 0.0,
		"automatic retry remains normal-colored")) ++failures;
	visual = retry.Update(start + 4001ms, 0.5, false, true);
	if (!Expect(visual.state == ProgressVisualState::Failure
		&& visual.failureColorProgress == 0.0
		&& visual.opacity == 1.0
		&& std::abs(visual.displayedRatio - 0.2) < 0.000001,
		"fatal failure starts its color transition without jumping its length"))
		++failures;
	const auto failureQuarterVisual = retry.Update(
		start + 4076ms, 0.9, false, false);
	if (!Expect(failureQuarterVisual.failureColorProgress < 0.25,
		"failure color transition starts slower than linear interpolation"))
		++failures;
	visual = retry.Update(start + 4151ms, 0.9, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.35) < 0.000001
			&& std::abs(visual.failureColorProgress - 0.5) < 0.000001,
		"failed progress length and color keep animating on independent timelines"))
		++failures;
	visual = retry.Update(start + 4301ms, 0.9, false, false);
	if (!Expect(visual.displayedRatio == 0.5
			&& visual.failureColorProgress == 1.0,
		"failed progress reaches red and ignores later actual changes")) ++failures;
	visual = retry.Update(start + 4302ms, 0.3, false, false);
	if (!Expect(visual.state == ProgressVisualState::Failure
			&& visual.failureColorProgress == 1.0
			&& visual.displayedRatio == 0.5,
		"first failure keeps its red color and frozen actual target sticky"))
		++failures;

	NotifyBarFirstCommittedFrame(80.0, 380.0, true);
	double publishedWidth = 0.0;
	if (!Expect(TakeCommittedStartupBarWidthDip(publishedWidth)
		&& publishedWidth == 470.0 && !TakeCommittedStartupBarWidthDip(publishedWidth),
		"expanded committed target publishes total width once")) ++failures;
	NotifyBarFirstCommittedFrame(80.0, 380.0, false);
	if (!Expect(TakeCommittedStartupBarWidthDip(publishedWidth)
		&& publishedWidth == 80.0,
		"collapsed committed target publishes button width")) ++failures;
	NotifyBarFirstCommittedFrame(80.0,
		(std::numeric_limits<double>::quiet_NaN)(), true);
	if (!Expect(!TakeCommittedStartupBarWidthDip(publishedWidth),
		"invalid committed geometry is not published")) ++failures;
	NotifyBarFirstCommittedFrame(80.0, 5000.0, true);
	if (!Expect(!TakeCommittedStartupBarWidthDip(publishedWidth),
		"oversized committed geometry is not replaced by a persisted fallback"))
		++failures;
	return failures;
}
