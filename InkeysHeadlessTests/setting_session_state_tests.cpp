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
	using Inkeys::UI::Setting::BarButtonClickAction;
	using Inkeys::UI::Setting::ResolveBarButtonClickAction;
	using Inkeys::UI::Setting::SessionState;
	using Inkeys::UI::Setting::StartupPreviewPreference;
	int failures = 0;
	if (!Expect(ResolveBarButtonClickAction(false, false)
		== BarButtonClickAction::ShowAndActivate
		&& ResolveBarButtonClickAction(false, true)
		== BarButtonClickAction::ShowAndActivate,
		"hidden setting is shown and activated")) ++failures;
	if (!Expect(ResolveBarButtonClickAction(true, false)
		== BarButtonClickAction::Activate,
		"visible unfocused setting is activated")) ++failures;
	if (!Expect(ResolveBarButtonClickAction(true, true)
		== BarButtonClickAction::Hide,
		"visible focused setting is hidden")) ++failures;
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

	// 渲染观察前发生 Hide→Show 时，最终可见也必须销毁旧输入会话。
	state.SetVisible(true);
	decision = state.Resolve(2, true);
	if (!Expect(decision.release && decision.rebuild && decision.render,
		"hide then show retains old session invalidation")) ++failures;
	state.Release();
	state.CommitEpoch(2);
	if (!Expect(!state.Resolve(2, true).release,
		"drained session can rebuild once and remain stable")) ++failures;

	state.SetVisible(false);
	state.SetVisible(false);
	state.SetVisible(true);
	decision = state.Resolve(2, true);
	if (!Expect(decision.release && decision.rebuild,
		"repeated hide keeps one invalidation")) ++failures;
	state.Release();
	state.CommitEpoch(2);
	decision = state.Resolve(2, true);
	if (!Expect(!decision.release && !decision.rebuild && decision.render,
		"release consumes repeated hide invalidation")) ++failures;

	// 旧帧已取决策后才到达的 Hide，不能被该帧 CommitEpoch 擦掉。
	decision = state.Resolve(2, true);
	state.SetVisible(false);
	state.SetVisible(true);
	state.CommitEpoch(2);
	if (!Expect(!decision.release && state.Resolve(2, true).release,
		"stale frame commit preserves a later hide")) ++failures;
	state.Release();
	state.SetVisible(false);
	state.SetVisible(true);
	state.CommitEpoch(2);
	if (!Expect(state.Resolve(2, true).release,
		"hide after new session boundary survives its first commit")) ++failures;
	state.Release();
	state.CommitEpoch(2);

	// 逻辑 Hide 已被 render 消费并建好新 context，FIFO 的原生 Hide 才真正到达。
	SessionState fifoSession;
	fifoSession.SetVisible(true);
	fifoSession.CommitEpoch(1);
	fifoSession.SetVisible(false);
	fifoSession.SetVisible(true);
	if (!Expect(fifoSession.Resolve(1, true).release,
		"logical hide invalidates old session before native FIFO hide")) ++failures;
	fifoSession.Release();
	fifoSession.CommitEpoch(1);
	decision = fifoSession.Resolve(1, true);
	if (!Expect(!decision.release && decision.render,
		"new session is committed before delayed native hide")) ++failures;
	fifoSession.InvalidateSession();
	fifoSession.CommitEpoch(1);
	decision = fifoSession.Resolve(1, true);
	if (!Expect(fifoSession.IsVisible() && decision.release
		&& decision.rebuild && decision.render,
		"delayed native hide preserves show intent and invalidates new input")) ++failures;
	fifoSession.Release();
	fifoSession.CommitEpoch(1);
	if (!Expect(!fifoSession.Resolve(1, true).release,
		"delayed native hide is consumed once after draining")) ++failures;
	fifoSession.SetVisible(false);
	fifoSession.InvalidateSession();
	decision = fifoSession.Resolve(1, true);
	if (!Expect(!fifoSession.IsVisible() && decision.release
		&& !decision.rebuild && !decision.render,
		"native hide also preserves a final hidden intent")) ++failures;

	SessionState firstSession;
	firstSession.SetVisible(false);
	decision = firstSession.Resolve(1, false);
	if (!Expect(!decision.release && !decision.rebuild && !decision.render,
		"hide without a session remains idle")) ++failures;
	firstSession.SetVisible(true);
	decision = firstSession.Resolve(1, false);
	if (!Expect(!decision.release && decision.rebuild,
		"hide without an old session permits initial creation")) ++failures;
	// 与生产新建边界相同：确认没有旧 context 后才消费历史 Hide。
	firstSession.Release();
	decision = firstSession.Resolve(1, false);
	if (!Expect(!decision.release && decision.rebuild,
		"failed initial creation can retry")) ++failures;
	firstSession.Release();
	firstSession.CommitEpoch(1);
	decision = firstSession.Resolve(1, true);
	if (!Expect(!decision.release && !decision.rebuild && decision.render,
		"initial session does not rebuild indefinitely after hide")) ++failures;
	firstSession.SetVisible(false);
	firstSession.Release();
	firstSession.Release();
	firstSession.SetVisible(true);
	firstSession.Release();
	firstSession.CommitEpoch(1);
	if (!Expect(!firstSession.Resolve(1, true).release,
		"empty drain and reopen stay idempotent")) ++failures;

	using Inkeys::UI::Setting::IsSharedDeviceLoss;
	if (!Expect(IsSharedDeviceLoss(DXGI_ERROR_DEVICE_REMOVED)
		&& IsSharedDeviceLoss(DXGI_ERROR_DEVICE_RESET)
		&& IsSharedDeviceLoss(DXGI_ERROR_DRIVER_INTERNAL_ERROR)
		&& !IsSharedDeviceLoss(DXGI_STATUS_OCCLUDED)
		&& !IsSharedDeviceLoss(E_FAIL),
		"DXGI failures distinguish shared device loss")) ++failures;

	return failures;
}
