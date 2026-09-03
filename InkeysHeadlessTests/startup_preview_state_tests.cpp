#include "../Inkeys/Inkeys/UI/StartupPreview/StartupPreview.CacheWrite.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

import Inkeys.UI.StartupPreview;

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[StartupPreviewState] failed: " << name << '\n';
		return condition;
	}

	enum class FakeCacheFailure
	{
		None,
		Prepare,
		Directory,
		Create,
		Write,
		Flush,
		Latest,
		Replace,
	};

	struct FakeCacheOperations
	{
		FakeCacheFailure failure = FakeCacheFailure::None;
		bool closed = false;
		bool cleaned = false;

		bool Prepare() const { return failure != FakeCacheFailure::Prepare; }
		bool EnsureParent() const { return failure != FakeCacheFailure::Directory; }
		bool CreateTemporary() const { return failure != FakeCacheFailure::Create; }
		bool WriteAll() const { return failure != FakeCacheFailure::Write; }
		bool Flush() const { return failure != FakeCacheFailure::Flush; }
		bool IsLatest() const { return failure != FakeCacheFailure::Latest; }
		bool Replace() const { return failure != FakeCacheFailure::Replace; }
		void Close() { closed = true; }
		void Cleanup() { cleaned = true; }
	};
}

int RunStartupPreviewStateTests()
{
	using namespace Inkeys::UI::StartupPreview;
	using namespace std::chrono_literals;
	int failures = 0;

	StateMachine disabled;
	if (!Expect(IsBarStartupReady(BarStartupState::FirstFrameCommitted)
		&& !IsBarStartupReady(BarStartupState::RenderClientRegistered)
		&& IsBarStartupFailure(BarStartupState::WindowMissing)
		&& IsBarStartupFailure(BarStartupState::StoppedBeforeReady),
		"TopWindow distinguishes committed Bar from terminal failures")) ++failures;
	if (!Expect(disabled.Start(false, CacheState::Missing, true).state
		== LifecycleState::Stopped,
		"disabled preview stops without preparing")) ++failures;

	for (const auto cache : { CacheState::Valid, CacheState::Missing,
		CacheState::Incompatible, CacheState::Corrupt })
	{
		StateMachine machine;
		const auto started = machine.Start(true, cache, true);
		const auto expectedHandoff = cache == CacheState::Valid
			? HandoffPath::ValidCacheProxySwap
			: cache == CacheState::Corrupt
				? HandoffPath::CorruptFadeThroughTransparent
				: HandoffPath::EmbeddedProxyDeblur;
		if (!Expect(started.accepted && started.handoff == expectedHandoff,
			"cache classification selects the required handoff")) ++failures;
		if (!Expect(!machine.BarFrameCommitted().accepted,
			"Bar frame cannot skip preview first commit")) ++failures;
		if (!Expect(machine.PreviewFrameCommitted().state
			== LifecycleState::WaitingForBar
			&& machine.BarFrameCommitted().state == LifecycleState::Handoff,
			"preview and Bar committed frames advance handoff")) ++failures;
		if (!Expect(machine.BeginStop().accepted
			&& machine.FinishStop().state == LifecycleState::Stopped,
			"handoff has bounded stop states")) ++failures;
	}

	StateMachine unavailable;
	if (!Expect(unavailable.Start(true, CacheState::Valid, false).state
		== LifecycleState::Bypassed,
		"presentation failure bypasses preview")) ++failures;
	if (!Expect(unavailable.FinishStop().state == LifecycleState::Stopped,
		"bypassed preview can finish stop")) ++failures;

	StateMachine fatal;
	(void)fatal.Start(true, CacheState::Missing, true);
	if (!Expect(fatal.FatalFailure().state == LifecycleState::FailurePending
		&& fatal.RequestFailureFrame().state == LifecycleState::FailureRedRequested
		&& fatal.FailureFrameCommitted().state == LifecycleState::FailureRedCommitted,
		"fatal path requires an explicit red committed frame")) ++failures;

	const auto start = std::chrono::steady_clock::time_point(10s);
	const auto previewShown = start + 20s;
	ProgressVisualReducer progress;
	auto visual = progress.Update(previewShown - 1ms, 0.4, false, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden && visual.opacity == 0.0,
		"progress gate does not start before the Preview is shown")) ++failures;
	progress.MarkPreviewShown(previewShown);
	progress.MarkPreviewShown(previewShown + 2s);
	visual = progress.Update(previewShown + 2999ms, 0.4, false, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden && visual.opacity == 0.0,
		"progress stays hidden for three seconds after Preview show")) ++failures;
	visual = progress.Update(previewShown + 3s, 0.4, false, false);
	if (!Expect(visual.state == ProgressVisualState::FadingIn
		&& visual.displayedRatio <= 0.4,
		"three second gate starts real progress fade")) ++failures;
	visual = progress.Update(previewShown + 3180ms, 0.4, false, false);
	if (!Expect(visual.state == ProgressVisualState::Visible
		&& visual.displayedRatio <= 0.4,
		"fade reaches visible without exceeding actual")) ++failures;
	ProgressVisualReducer earlyFailure;
	visual = earlyFailure.Update(start, 0.25, false, true);
	if (!Expect(visual.state == ProgressVisualState::Failure
		&& visual.red && visual.opacity == 1.0 && visual.displayedRatio == 0.25,
		"fatal progress immediately uses red actual ratio before the show gate")) ++failures;

	ProgressVisualReducer quick;
	quick.MarkPreviewShown(start);
	visual = quick.Update(start + 2900ms, 1.0, true, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden,
		"startup completed before three seconds never shows progress")) ++failures;
	if (!Expect(CanBeginSuccessfulHandoff(true, true, false, visual.state,
		true, true), "hidden progress permits a complete handoff")) ++failures;
	if (!Expect(!CanBeginSuccessfulHandoff(true, false, false, visual.state,
		true, true), "early Bar commit waits for parallel milestones")) ++failures;

	ProgressVisualReducer ordered;
	ordered.MarkPreviewShown(start);
	(void)ordered.Update(start + 3s, 0.8, false, false);
	(void)ordered.Update(start + 3180ms, 0.8, false, false);
	auto completing = ordered.Update(start + 3200ms, 1.0, true, false);
	if (!Expect(completing.state == ProgressVisualState::Completing
		&& completing.displayedRatio == 1.0 && completing.opacity == 1.0
		&& !CanBeginSuccessfulHandoff(true, true, false, completing.state, true, true),
		"visible progress reaches full before handoff starts")) ++failures;
	completing = ordered.Update(start + 3499ms, 1.0, true, false);
	if (!Expect(completing.state == ProgressVisualState::Completing
		&& completing.displayedRatio == 1.0 && completing.opacity == 1.0,
		"full progress remains visible for the completion hold")) ++failures;
	auto fading = ordered.Update(start + 3500ms, 1.0, true, false);
	if (!Expect(fading.state == ProgressVisualState::FadingOut
		&& !CanBeginSuccessfulHandoff(true, true, false, fading.state, true, true),
		"full progress fades before handoff starts")) ++failures;
	const auto hidden = ordered.Update(start + 3640ms, 1.0, true, false);
	if (!Expect(hidden.state == ProgressVisualState::Hidden
		&& CanBeginSuccessfulHandoff(true, true, false, hidden.state, true, true),
		"handoff starts only after progress is hidden")) ++failures;

	const auto validBefore = ResolveHandoffFrame(
		HandoffPath::ValidCacheProxySwap, 139ms, 0);
	const auto validSwitch = ResolveHandoffFrame(
		HandoffPath::ValidCacheProxySwap, 140ms, 0);
	if (!Expect(!validBefore.useLiveProxy && !validBefore.requestBarAlpha
		&& validSwitch.useLiveProxy && validSwitch.requestBarAlpha
		&& validSwitch.requestedBarAlpha == 255,
		"valid cache uses one clear cache-to-proxy boundary")) ++failures;
	const auto embeddedStart = ResolveHandoffFrame(
		HandoffPath::EmbeddedProxyDeblur, 79ms, 0);
	const auto embeddedLive = ResolveHandoffFrame(
		HandoffPath::EmbeddedProxyDeblur, 80ms, 0);
	const auto embeddedDone = ResolveHandoffFrame(
		HandoffPath::EmbeddedProxyDeblur, 460ms, 255);
	if (!Expect(!embeddedStart.useLiveProxy && embeddedStart.blurRatio == 1.0
		&& embeddedLive.useLiveProxy && embeddedLive.blurRatio == 1.0
		&& embeddedDone.blurRatio == 0.0 && embeddedDone.stopPreview,
		"embedded path swaps under blur then deblurs before reveal")) ++failures;
	const auto corruptFade = ResolveHandoffFrame(
		HandoffPath::CorruptFadeThroughTransparent, 80ms, 0);
	const auto corruptHold = ResolveHandoffFrame(
		HandoffPath::CorruptFadeThroughTransparent, 180ms, 0);
	const auto corruptRise = ResolveHandoffFrame(
		HandoffPath::CorruptFadeThroughTransparent, 280ms, 0);
	const auto corruptDone = ResolveHandoffFrame(
		HandoffPath::CorruptFadeThroughTransparent, 360ms, 255);
	if (!Expect(corruptFade.previewAlpha == 0.5 && !corruptFade.requestBarAlpha
		&& corruptHold.previewAlpha == 0.0 && !corruptHold.requestBarAlpha
		&& corruptRise.previewAlpha == 0.0
		&& corruptRise.requestedBarAlpha > 0
		&& corruptRise.requestedBarAlpha < 255 && corruptDone.stopPreview,
		"corrupt path has a transparent hold before Bar fade")) ++failures;
	for (int elapsed = 0; elapsed <= 360; ++elapsed)
	{
		const auto decision = ResolveHandoffFrame(
			HandoffPath::CorruptFadeThroughTransparent,
			std::chrono::milliseconds(elapsed), 0);
		const bool barIntermediate = decision.requestBarAlpha
			&& decision.requestedBarAlpha > 0 && decision.requestedBarAlpha < 255;
		if (!Expect(!barIntermediate || decision.previewAlpha == 0.0,
			"two layered windows never carry intermediate alpha together"))
		{
			++failures;
			break;
		}
	}

	HandoffFailureReducer recovery;
	recovery.ObserveFailure(start, true);
	recovery.ObserveSuccess();
	if (!Expect(!recovery.Poll(start + 1s, true, true, 0).requestOpaqueBar,
		"one recovered preview failure does not bypass handoff")) ++failures;
	recovery.ObserveFailure(start, true);
	if (!Expect(!recovery.Poll(start + 749ms, true, true, 0).requestOpaqueBar
		&& recovery.Poll(start + 750ms, true, true, 0).requestOpaqueBar
		&& recovery.Poll(start + 751ms, true, true, 255).stopPreview,
		"persistent handoff failure reveals Bar after a bounded retry")) ++failures;
	recovery.Reset();
	if (!Expect(recovery.Poll(start, true, false, 0).requestOpaqueBar,
		"missing owner immediately reveals an already committed Bar")) ++failures;

	using namespace Inkeys::UI::StartupPreview::CacheWrite;
	LatestRevisionPolicy revisions;
	if (!Expect(revisions.Accept(4) && !revisions.Accept(3)
		&& revisions.Accept(5) && !revisions.IsLatest(4) && revisions.IsLatest(5),
		"cache writer retains only the latest revision")) ++failures;
	for (const auto failure : { FakeCacheFailure::Prepare, FakeCacheFailure::Directory,
		FakeCacheFailure::Create, FakeCacheFailure::Write, FakeCacheFailure::Flush,
		FakeCacheFailure::Latest, FakeCacheFailure::Replace })
	{
		FakeCacheOperations operations{ failure };
		if (!Expect(!ExecuteDurableTransaction(operations)
			&& operations.closed && operations.cleaned,
			"durable cache failure closes and cleans its temporary file")) ++failures;
	}
	FakeCacheOperations success;
	if (!Expect(ExecuteDurableTransaction(success) && success.closed
		&& !success.cleaned, "durable cache commits only after every stage")) ++failures;

	const auto testRoot = std::filesystem::current_path()
		/ (L"startup-preview-cache-test-" + std::to_wstring(GetCurrentProcessId()));
	std::error_code fileError;
	std::filesystem::remove_all(testRoot, fileError);
	const auto nestedTarget = testRoot / L"missing" / L"nested" / L"preview.bin";
	if (!Expect(EnsureParentDirectory(nestedTarget.wstring())
		&& std::filesystem::is_directory(nestedTarget.parent_path()),
		"cache writer creates a missing target directory")) ++failures;
	std::filesystem::remove_all(testRoot, fileError);

	const auto stopStart = std::chrono::steady_clock::now();
	ConfigureDeveloperCapture(L"startup-preview-unused.bin");
	Stop();
	if (!Expect(std::chrono::steady_clock::now() - stopStart < 1s
		&& !DeveloperCaptureRequested(),
		"idle cache writer stops within its bounded lifetime")) ++failures;
	return failures;
}
