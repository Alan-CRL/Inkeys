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
		&& StartupPreviewEnabledByDefault
		&& CalculateStartupBarTotalWidthDip(80.0, 380.0) == 470.0,
		"config defaults preview on and derives 380/470 DIP geometry")) ++failures;
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
		const double support = (std::max)(160.0,
			static_cast<double>(pixels.width) / 2.0);
		const ShimmerGradient gradient{ 0.0, 0.0, support, 0.0, 0.0, 1.0 };
		const auto travel = ResolveShimmerHorizontalTravel(bounds, gradient, 8.0);
		if (!Expect(travel.valid && travel.startTranslationX < travel.endTranslationX
			&& travel.startTranslationX + gradient.endX <= bounds.left
			&& travel.endTranslationX >= bounds.right
			&& IsShimmerSupportOutsideMask(bounds, gradient,
				travel.startTranslationX)
			&& IsShimmerSupportOutsideMask(bounds, gradient,
				travel.endTranslationX),
			"shimmer soft-tail support is fully offscreen at both endpoints")) ++failures;
	}
	const auto shimmerEpoch = std::chrono::steady_clock::time_point(100h);
	const auto halfCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 875ms, shimmerEpoch, 1.75s);
	const auto wrappedCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 1750ms, shimmerEpoch, 1.75s);
	if (!Expect(std::abs(halfCycle - 0.5) < 0.000001
		&& wrappedCycle == 0.0 && EaseShimmerPhase(0.25) < 0.25
		&& std::abs(EaseShimmerPhase(0.5) - 0.5) < 0.000001
		&& EaseShimmerPhase(0.75) > 0.75,
		"shimmer uses local epoch and eased endpoints")) ++failures;

	std::uint8_t previous = 0;
	for (int elapsed = 0; elapsed <= 180; ++elapsed)
	{
		const auto alpha = ResolveFadeInAlpha(
			std::chrono::milliseconds(elapsed), 180ms);
		if (!Expect(alpha >= previous, "preview fade-in alpha is monotonic")) ++failures;
		previous = alpha;
	}
	if (!Expect(ResolveFadeInAlpha(0ms, 180ms) == 0
		&& ResolveFadeInAlpha(180ms, 180ms) == 255
		&& ResolveFadeOutAlpha(0ms, 160ms) == 255
		&& ResolveFadeOutAlpha(160ms, 160ms) == 0
		&& ResolveFadeOutAlphaFrom(96, 0ms, 160ms) == 96
		&& ResolveFadeOutAlphaFrom(96, 160ms, 160ms) == 0,
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

	if (!Expect(IsBarStartupReady(BarStartupState::FirstFrameCommitted)
		&& !IsBarStartupReady(BarStartupState::RenderClientRegistered)
		&& IsBarStartupFailure(BarStartupState::WindowMissing)
		&& IsBarStartupFailure(BarStartupState::StoppedBeforeReady),
		"Bar readiness distinguishes committed and terminal states")) ++failures;

	const auto start = std::chrono::steady_clock::time_point(10s);
	ProgressVisualReducer progress;
	progress.MarkPreviewShown(start);
	auto visual = progress.Update(start + 2999ms, 0.4, false, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden
		&& visual.opacity == 0.0 && visual.displayedRatio <= 0.4,
		"progress stays hidden for 2999 ms")) ++failures;
	visual = progress.Update(start + 3s, 0.4, false, false);
	if (!Expect(visual.state == ProgressVisualState::FadingIn
		&& visual.displayedRatio <= 0.4,
		"three-second gate starts progress fade")) ++failures;
	visual = progress.Update(start + 3180ms, 0.4, false, false);
	if (!Expect(visual.state == ProgressVisualState::Visible
		&& visual.opacity == 1.0 && visual.displayedRatio <= 0.4,
		"progress becomes visible without time-based progress")) ++failures;
	visual = progress.Update(start + 3181ms, 1.0, true, false);
	if (!Expect(visual.state == ProgressVisualState::Visible
		&& visual.displayedRatio == 1.0 && visual.opacity == 1.0,
		"success reaches real 100 percent without hold")) ++failures;
	ProgressVisualReducer quick;
	quick.MarkPreviewShown(start);
	visual = quick.Update(start + 2900ms, 1.0, true, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden && visual.opacity == 0.0,
		"quick startup never shows progress")) ++failures;
	ProgressVisualReducer retry;
	retry.MarkPreviewShown(start);
	visual = retry.Update(start + 4s, 0.5, false, false);
	if (!Expect(!visual.red, "automatic retry remains normal-colored")) ++failures;
	visual = retry.Update(start + 4001ms, 0.5, false, true);
	if (!Expect(visual.state == ProgressVisualState::Failure && visual.red
		&& visual.opacity == 1.0 && visual.displayedRatio == 0.5,
		"only final fatal failure turns progress red")) ++failures;

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
