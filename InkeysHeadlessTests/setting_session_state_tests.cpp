#include "../Inkeys/Inkeys/UI/Setting/Setting.SessionState.h"

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
	using Inkeys::UI::Setting::StartupPreviewPreference;
	int failures = 0;
	SessionState state;
	StartupPreviewPreference startupPreview;
	if (!Expect(startupPreview.StartupEnabled()
		&& startupPreview.ConfiguredEnabled(),
		"startup preview defaults enabled")) ++failures;
	if (!Expect(startupPreview.SetConfigured(false)
		&& startupPreview.StartupEnabled()
		&& !startupPreview.ConfiguredEnabled()
		&& startupPreview.ConsumeWritePending()
		&& !startupPreview.ConsumeWritePending(),
		"setting changes persistence but not current startup snapshot")) ++failures;

	auto decision = state.Resolve(1, false);
	if (!Expect(!decision.release && !decision.rebuild && !decision.render,
		"hidden state stays idle")) ++failures;

	state.SetVisible(true);
	decision = state.Resolve(1, false);
	if (!Expect(decision.rebuild && decision.render,
		"first visible frame rebuilds and renders")) ++failures;
	state.CommitEpoch(1);

	decision = state.Resolve(1, true);
	if (!Expect(!decision.release && !decision.rebuild && decision.render,
		"stable visible session renders")) ++failures;

	state.QueueResize(960, 700);
	decision = state.Resolve(1, true);
	const auto firstResize = state.Resize();
	if (!Expect(decision.resize && firstResize.width == 960
		&& firstResize.height == 700, "resize is queued once")) ++failures;
	state.ConsumeResize(firstResize.serial);
	if (!Expect(!state.Resolve(1, true).resize, "resize clears after commit"))
		++failures;
	state.QueueResize(1000, 720);
	const auto staleResize = state.Resize();
	state.QueueResize(1024, 768);
	state.ConsumeResize(staleResize.serial);
	if (!Expect(state.Resolve(1, true).resize
		&& state.Resize().width == 1024,
		"new resize survives stale completion")) ++failures;
	state.ConsumeResize(state.Resize().serial);

	state.SetOccluded(true);
	decision = state.Resolve(1, true);
	if (!Expect(decision.probeOcclusion && !decision.render,
		"occlusion probes without rendering")) ++failures;
	state.SetOccluded(false);

	state.PublishBusinessCompletion(1, false);
	decision = state.Resolve(1, true);
	const auto completion = state.BusinessCompletion();
	if (!Expect(decision.consumeBusinessCompletion && completion.serial == 1
		&& !completion.succeeded,
		"business completion requests state consumption")) ++failures;
	state.ConsumeBusinessCompletion(completion.serial);
	if (!Expect(!state.Resolve(1, true).consumeBusinessCompletion,
		"business completion is consumed once")) ++failures;

	decision = state.Resolve(2, true);
	if (!Expect(decision.release && decision.rebuild && decision.render,
		"generation change releases and rebuilds")) ++failures;
	state.Release();
	state.CommitEpoch(2);

	state.SetVisible(false);
	decision = state.Resolve(2, true);
	if (!Expect(decision.release && !decision.rebuild && !decision.render,
		"hide releases session and becomes idle")) ++failures;

	using Inkeys::UI::Setting::IsSharedDeviceLoss;
	if (!Expect(IsSharedDeviceLoss(DXGI_ERROR_DEVICE_REMOVED)
		&& IsSharedDeviceLoss(DXGI_ERROR_DEVICE_RESET)
		&& IsSharedDeviceLoss(DXGI_ERROR_DRIVER_INTERNAL_ERROR)
		&& !IsSharedDeviceLoss(DXGI_STATUS_OCCLUDED)
		&& !IsSharedDeviceLoss(E_FAIL),
		"DXGI failures distinguish shared device loss")) ++failures;

	return failures;
}
