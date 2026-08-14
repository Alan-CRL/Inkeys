#include "../Inkeys/Inkeys/UI/Setting/Setting.SessionState.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Layout.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Theme.h"

#include <cmath>
#include <iostream>

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[SettingSessionState] failed: " << name << '\n';
		return condition;
	}
}

int RunSettingSessionStateTests()
{
	using Inkeys::UI::Setting::SessionState;
	using Inkeys::UI::Setting::NavigationLayout;
	int failures = 0;

	if (!Expect(Inkeys::UI::Setting::NormalizeUserScale(0.5F) == 1.0F
		&& Inkeys::UI::Setting::NormalizeUserScale(2.5F) == 2.0F
		&& Inkeys::UI::Setting::NormalizeUserScale(1.35F) == 1.35F,
		"user scale clamps to the 1.0-2.0 contract")) ++failures;
	if (!Expect(std::abs(Inkeys::UI::Setting::EffectiveScale(144U, 1.25F)
		- 1.875F) < 0.0001F,
		"effective scale multiplies system DPI and user scale")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolveWindowExtent(960.0F, 1.5F, 1920)
		== 1440
		&& Inkeys::UI::Setting::ResolveWindowExtent(700.0F, 3.0F, 1776)
		== 1776
		&& Inkeys::UI::Setting::ResolveWindowExtent(700.0F, 3.0F, 0)
		== 2100,
		"default setting extent is capped by the monitor work area")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolveNavigationLayout(1800.0F, 2.0F)
		== NavigationLayout::Open
		&& Inkeys::UI::Setting::ResolveNavigationLayout(1799.0F, 2.0F)
		== NavigationLayout::Compact
		&& Inkeys::UI::Setting::ResolveNavigationLayout(1520.0F, 2.0F)
		== NavigationLayout::Compact
		&& Inkeys::UI::Setting::ResolveNavigationLayout(1519.0F, 2.0F)
		== NavigationLayout::Overlay,
		"responsive breakpoints use logical DIP width")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolvePageWidth(1400.0F, 1.0F) == 920.0F
		&& Inkeys::UI::Setting::ResolvePageWidth(1400.0F, 1.5F) == 1380.0F
		&& Inkeys::UI::Setting::ResolvePageWidth(800.0F, 2.0F) == 800.0F,
		"page width caps at 920 DIP without expanding narrow content")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolvePageTransitionProgress(-0.01F) == 0.0F
		&& std::abs(Inkeys::UI::Setting::ResolvePageTransitionProgress(0.08F) - 0.5F)
		< 0.0001F
		&& Inkeys::UI::Setting::ResolvePageTransitionProgress(0.16F) == 1.0F
		&& Inkeys::UI::Setting::ResolvePageTransitionProgress(1.0F) == 1.0F,
		"page transition stays within the 160ms bounds")) ++failures;
	{
		const auto titleBar = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			960.0F, 1.0F, 46.0F, 56.0F, 80.0F);
		if (!Expect(titleBar.height == 32.0F
			&& titleBar.icon.Width() == 16.0F
			&& titleBar.minimize.left == 822.0F
			&& titleBar.maximize.left == 868.0F
			&& titleBar.close.left == 914.0F
			&& titleBar.versionVisible
			&& titleBar.drag.Width() >= 96.0F,
			"titlebar geometry keeps 32 DIP identity, drag, right header and captions"))
			++failures;
		if (!Expect(titleBar.close.Contains(959.0F, 16.0F)
			&& !titleBar.close.Contains(914.0F, 32.0F)
			&& titleBar.version.right < titleBar.minimize.left,
			"titlebar hit rectangles stay disjoint and use half-open bounds"))
			++failures;

		const auto narrowTitleBar = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			360.0F, 1.0F, 46.0F, 56.0F, 80.0F);
		if (!Expect(!narrowTitleBar.versionVisible
			&& narrowTitleBar.drag.Width() >= 96.0F
			&& narrowTitleBar.close.right == 360.0F,
			"narrow titlebar hides version before sacrificing drag or captions"))
			++failures;

		const auto scaledTitleBar = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			1920.0F, 2.0F, 92.0F, 112.0F, 160.0F);
		if (!Expect(scaledTitleBar.height == titleBar.height * 2.0F
			&& scaledTitleBar.icon.Width() == titleBar.icon.Width() * 2.0F
			&& scaledTitleBar.close.left == titleBar.close.left * 2.0F
			&& scaledTitleBar.versionVisible,
			"titlebar geometry scales logical DIP coordinates exactly once"))
			++failures;
	}
	if (!Expect(Inkeys::UI::Setting::ResolveThemeMode(false, true)
		== Inkeys::UI::Setting::ThemeMode::Light
		&& Inkeys::UI::Setting::ResolveThemeMode(false, false)
		== Inkeys::UI::Setting::ThemeMode::Dark
		&& Inkeys::UI::Setting::ResolveThemeMode(true, true)
		== Inkeys::UI::Setting::ThemeMode::HighContrast,
		"high contrast overrides the Windows app theme")) ++failures;

	SessionState state;

	auto decision = state.Resolve(1, false, false);
	if (!Expect(decision.initializeResident && !decision.createPresentation
		&& !decision.render, "hidden initialization builds resident resources"))
		++failures;
	state.CommitEpoch(1);

	state.SetVisible(true);
	decision = state.Resolve(1, true, false);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& decision.createPresentation
		&& decision.render, "first visible frame creates presentation")) ++failures;

	decision = state.Resolve(1, true, true);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& !decision.releasePresentation && decision.render,
		"stable visible session renders")) ++failures;

	state.QueueResize(960, 700);
	decision = state.Resolve(1, true, true);
	const auto firstResize = state.Resize();
	if (!Expect(decision.resize && firstResize.width == 960
		&& firstResize.height == 700, "resize is queued once")) ++failures;
	state.ConsumeResize(firstResize.serial);
	if (!Expect(!state.Resolve(1, true, true).resize, "resize clears after commit"))
		++failures;
	state.QueueResize(1000, 720);
	const auto staleResize = state.Resize();
	state.QueueResize(1024, 768);
	state.ConsumeResize(staleResize.serial);
	if (!Expect(state.Resolve(1, true, true).resize
		&& state.Resize().width == 1024,
		"new resize survives stale completion")) ++failures;
	state.ConsumeResize(state.Resize().serial);

	state.QueueFontRebuild();
	decision = state.Resolve(1, true, true);
	const auto fontSerial = state.FontRebuildSerial();
	if (!Expect(decision.rebuildFonts,
		"DPI or user scale change requests a font rebuild")) ++failures;
	state.ConsumeFontRebuild(fontSerial);
	if (!Expect(!state.Resolve(1, true, true).rebuildFonts,
		"font rebuild is consumed once")) ++failures;

	state.SetOccluded(true);
	decision = state.Resolve(1, true, true);
	if (!Expect(decision.probeOcclusion && !decision.render,
		"occlusion probes without rendering")) ++failures;
	state.SetOccluded(false);

	state.PublishBusinessCompletion(1, false);
	decision = state.Resolve(1, true, true);
	const auto completion = state.BusinessCompletion();
	if (!Expect(decision.consumeBusinessCompletion && completion.serial == 1
		&& !completion.succeeded,
		"business completion requests state consumption")) ++failures;
	state.ConsumeBusinessCompletion(completion.serial);
	if (!Expect(!state.Resolve(1, true, true).consumeBusinessCompletion,
		"business completion is consumed once")) ++failures;

	decision = state.Resolve(2, true, true);
	if (!Expect(!decision.initializeResident && decision.rebuildDeviceResources
		&& decision.releasePresentation && decision.createPresentation
		&& decision.render, "generation change rebuilds both layers")) ++failures;
	state.ReleaseResident();
	state.CommitEpoch(2);

	state.SetVisible(false);
	decision = state.Resolve(2, true, true);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& decision.releasePresentation && !decision.createPresentation
		&& !decision.render, "hide keeps resident resources and becomes idle"))
		++failures;
	state.ReleasePresentation();
	decision = state.Resolve(2, true, false);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& !decision.createPresentation
		&& !decision.render, "hidden resident session stays idle")) ++failures;

	decision = state.Resolve(3, true, false);
	if (!Expect(!decision.initializeResident && decision.rebuildDeviceResources
		&& !decision.createPresentation && !decision.render,
		"hidden generation change rebuilds resident resources")) ++failures;

	using Inkeys::UI::Setting::IsSharedDeviceLoss;
	if (!Expect(IsSharedDeviceLoss(DXGI_ERROR_DEVICE_REMOVED)
		&& IsSharedDeviceLoss(DXGI_ERROR_DEVICE_RESET)
		&& IsSharedDeviceLoss(DXGI_ERROR_DRIVER_INTERNAL_ERROR)
		&& !IsSharedDeviceLoss(DXGI_STATUS_OCCLUDED)
		&& !IsSharedDeviceLoss(E_FAIL),
		"DXGI failures distinguish shared device loss")) ++failures;

	return failures;
}
