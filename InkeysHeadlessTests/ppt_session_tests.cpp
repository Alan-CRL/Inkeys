#include "../Inkeys/Inkeys/Business/PptSession.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.A2.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"

#include <iostream>
#include <string>

int RunPptSessionTests()
{
	using namespace Inkeys::Business;
	using namespace Inkeys::UI::Bar;
	int failures = 0;
	auto check = [&](bool ok, const char* text)
	{
		if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; }
	};
	const std::wstring descriptor = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"StableSlideIds","fullName":"C:\\slides\\a.pptx","presentationName":"a.pptx","applicationProcessId":12,"slideShowHwnd":34,"currentPage":2,"totalPage":3,"currentSlideId":202,"slideIds":[101,202,303],"bindingRevision":9})";
	const auto envelope = [&](std::wstring lifecycle, std::wstring status,
		std::wstring nested = L"")
	{
		return L"{\"schemaVersion\":1,\"stateRevision\":10,\"showSessionRevision\":2,\"bindingRevision\":9,\"lifecycle\":\""
			+ lifecycle + L"\",\"pageStatus\":\"" + status + L"\",\"descriptor\":"
			+ (nested.empty() ? descriptor : nested) + L"}";
	};
	const auto valid = ParsePptSessionSnapshot(envelope(L"Active", L"Valid"));
	check(valid.snapshot && valid.snapshot->showSessionRevision == 2
		&& valid.snapshot->descriptor.currentSlideId == 202,
		"session parser delegates stable identity validation to real descriptor parser");
	check(!ParsePptSessionSnapshot(envelope(L"Inactive", L"Valid")).snapshot,
		"inactive snapshot cannot claim a writable valid page");
	check(!ParsePptSessionSnapshot(envelope(L"Active", L"GuessEnd")).snapshot,
		"unknown status cannot masquerade as an end screen");
	std::wstring mismatched = envelope(L"Active", L"Valid");
	mismatched.replace(mismatched.find(L"\"bindingRevision\":9"),
		std::wstring(L"\"bindingRevision\":9").size(), L"\"bindingRevision\":8");
	check(!ParsePptSessionSnapshot(mismatched).snapshot,
		"session and descriptor must belong to same binding");
	const std::wstring unavailable = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"Unavailable","fullName":"C:\\slides\\a.pptx","presentationName":"a.pptx","applicationProcessId":12,"slideShowHwnd":34,"currentPage":0,"totalPage":3,"currentSlideId":null,"slideIds":[],"bindingRevision":9})";
	check(ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", unavailable)).snapshot.has_value(),
		"explicit end screen preserves v1 descriptor without fabricating slide identity");
	const std::wstring endTopology = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"StableSlideIds","fullName":"C:\\slides\\a.pptx","presentationName":"a.pptx","applicationProcessId":12,"slideShowHwnd":34,"currentPage":0,"totalPage":3,"currentSlideId":null,"slideIds":[101,202,303],"bindingRevision":9})";
	check(ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", endTopology)).snapshot.has_value() &&
		!ParsePptSessionSnapshot(envelope(L"Active", L"Valid", endTopology)).snapshot,
		"only EndScreen envelope can authorize a zero-current-page stable topology");
	check(ParsePptSessionSnapshot(envelope(L"Unknown", L"Unknown", unavailable)).snapshot.has_value(),
		"read failures stay unknown instead of exiting the workspace");
	check(!ParsePptSessionSnapshot(envelope(L"Active", L"Valid", unavailable)).snapshot,
		"unavailable descriptor cannot open input through valid status");

	// 执行生产缓存/Bridge/回执谓词；不创建 HWND，也不模拟 Office 已经呈现。
	{
		namespace D3 = Inkeys::Drawing::Draw3;
		auto desc = valid.snapshot->descriptor;
		desc.currentPage = 1; desc.currentSlideId = 101;
		PptTargetCache cache;
		D3::Bridge::StateBridge bridge;
		check(cache.Update(true, &desc, true, false, 1) && cache.Current(), "initial target resolves");
		auto first = bridge.PublishPresentationTarget(*cache.target);
		cache.target->targetRevision = first.value_or(0);
		desc.currentPage = 2; desc.currentSlideId = 202;
		check(!cache.Update(true, &desc, false, false, 1) && !cache.Current(),
			"raw A with descriptor B retains pending application and rejects stale A");
		check(cache.Update(false, &desc, true, false, 1) && cache.Current()
			&& cache.target->pageIndex == 1 && cache.target->slideId == 202,
			"raw catches up without a new descriptor revision and resolves B");
		const auto second = bridge.PublishPresentationTarget(*cache.target);
		cache.target->targetRevision = second.value_or(0);
		check(second && first && *second != *first
			&& MatchesPptPageUi(*cache.target, 1, *second, 2, 3)
			&& !MatchesPptPageUi(*cache.target, 1, *first, 1, 3),
			"new page accepts only its own UI acknowledgement");
		for (int tick = 0; tick != 10000; ++tick)
			check(!cache.Update(false, &desc, true, false, 1) && cache.Current()
				&& bridge.PublishPresentationTarget(*cache.target) == second,
				"unchanged heartbeat preserves converged target identity");
		// raw 先到时仍等待 descriptor；随后多次 descriptor 变化只应用最后可信目标。
		check(!cache.Update(false, &desc, false, false, 1), "raw-first mismatch does not resolve");
		desc.currentPage = 3; desc.currentSlideId = 303;
		check(!cache.Update(true, &desc, false, false, 1), "rapid page C waits for raw");
		desc.currentPage = 1; desc.currentSlideId = 101;
		check(cache.Update(true, &desc, true, false, 1) && cache.target->pageIndex == 0,
			"rapid changes converge to latest trusted descriptor");
		desc.status = D3::PresentationDescriptorStatus::Unavailable;
		check(!cache.Update(true, &desc, true, false, 1) && !cache.Current(),
			"failed resolution cannot publish previous target");
		desc.status = D3::PresentationDescriptorStatus::StableSlideIds;
		check(cache.Update(false, &desc, true, false, 1), "failed resolution remains retryable");
		desc.currentPage = 0; desc.currentSlideId.reset();
		check(!cache.Update(true, &desc, false, false, 1) &&
			cache.Update(false, &desc, false, true, 1) && cache.target->pageIndex == 3,
			"end screen pending update converges without another envelope");
		cache.target->targetRevision = 9;
		check(MatchesPptPageUi(*cache.target, 1, 9, -1, 3)
			&& !MatchesPptPageUi(*cache.target, 1, 9, 3, 3), "end screen requires distinct UI identity");
		desc.currentPage = 2; desc.currentSlideId = 202;
		check(cache.Update(true, &desc, true, false, 2) && cache.target->sessionRevision == 2
			&& !MatchesPptPageUi(*cache.target, 1, 9, 2, 3), "new session rejects previous UI ack");
		desc.slideIds = {303,202,101};
		check(cache.Update(true, &desc, true, false, 2) && cache.target->slideIds == desc.slideIds,
			"same page topology change replaces full target identity");
		cache.target.reset();
		check(cache.Update(false, &desc, true, false, 2), "cleared session cache resolves even without notification");

		// UI 没看到暂停占位帧时，原目标曾经 ack 的状态不能吞掉恢复回执。
		const auto before = bridge.Snapshot().presentationTarget;
		const auto identity = D3::Bridge::ReadyIdentityFor(*before);
		for (int tick = 0; tick != 1000; ++tick)
			check(bridge.RequirePresentationTargetRefresh(identity)
				&& bridge.Snapshot().presentationTarget->targetRevision == identity.targetRevision,
				"repeated suspension is idempotent until trusted publication");
		const auto resumed = bridge.PublishPresentationTarget(*before);
		check(resumed && *resumed != identity.targetRevision,
			"same-page resume creates new identity for Present and UI ack");
		check(!bridge.RequirePresentationTargetRefresh(identity)
			&& bridge.PublishPresentationTarget(*before) == resumed,
			"late suspension cannot invalidate resumed page and heartbeat stays stable");
		const auto resumedTarget = bridge.Snapshot().presentationTarget;
		check(!MatchesPptPageUi(*resumedTarget, 1, identity.targetRevision, 2, 3)
			&& MatchesPptPageUi(*resumedTarget, 1, *resumed, 2, 3),
			"old same-page UI acknowledgement is rejected after resume");
		const auto resumedIdentity = D3::Bridge::ReadyIdentityFor(*resumedTarget);
		check(bridge.RequirePresentationTargetRefresh(resumedIdentity), "arm refresh before rejected publication");
		auto invalid = *resumedTarget;
		invalid.totalPages = 0;
		check(!bridge.PublishPresentationTarget(invalid)
			&& bridge.Snapshot().presentationTarget->targetRevision == *resumed,
			"invalid publication preserves current target");
		const auto afterRejected = bridge.PublishPresentationTarget(*resumedTarget);
		check(afterRejected && *afterRejected != *resumed,
			"rejected publication retains refresh obligation for valid retry");

		auto beforeClear = bridge.Snapshot().presentationTarget;
		const auto clearIdentity = D3::Bridge::ReadyIdentityFor(*beforeClear);
		check(bridge.RequirePresentationTargetRefresh(clearIdentity) && bridge.ClearPresentationTarget()
			&& !bridge.RequirePresentationTargetRefresh(clearIdentity), "cleared target rejects late refresh");
		const auto afterClear = bridge.PublishPresentationTarget(*beforeClear);
		check(afterClear && *afterClear != clearIdentity.targetRevision
			&& !bridge.RequirePresentationTargetRefresh(clearIdentity)
			&& bridge.PublishPresentationTarget(*beforeClear) == afterClear,
			"republication after clear has new stable identity unaffected by old refresh");

		auto beforeReset = bridge.Snapshot().presentationTarget;
		const auto resetIdentity = D3::Bridge::ReadyIdentityFor(*beforeReset);
		check(bridge.RequirePresentationTargetRefresh(resetIdentity), "arm refresh before Bridge reset");
		bridge.Reset();
		check(!bridge.RequirePresentationTargetRefresh(resetIdentity), "Bridge reset rejects old refresh identity");
		const auto afterReset = bridge.PublishPresentationTarget(*beforeReset);
		check(afterReset && *afterReset != resetIdentity.targetRevision
			&& !bridge.RequirePresentationTargetRefresh(resetIdentity)
			&& bridge.PublishPresentationTarget(*beforeReset) == afterReset,
			"Bridge reset preserves revision uniqueness and clears pending refresh");

		const auto previousSession = bridge.Snapshot().presentationTarget;
		const auto previousIdentity = D3::Bridge::ReadyIdentityFor(*previousSession);
		check(bridge.RequirePresentationTargetRefresh(previousIdentity), "arm refresh before new session");
		auto newSession = *previousSession;
		++newSession.sessionRevision;
		const auto newRevision = bridge.PublishPresentationTarget(newSession);
		check(newRevision && *newRevision != previousIdentity.targetRevision
			&& !bridge.RequirePresentationTargetRefresh(previousIdentity)
			&& bridge.PublishPresentationTarget(newSession) == newRevision,
			"new session rejects previous-session refresh and retains stable revision");
	}

	PptSessionToken original{ 1, 2, 3, 4, 5, 6, true, true };
	check(MatchesPptSession(original, original), "current request matches exact session");
	auto reopened = original;
	++reopened.showSessionRevision;
	check(!MatchesPptSession(original, reopened), "same HWND and binding reentry rejects old confirmation");
	reopened = original; ++reopened.serviceGeneration;
	check(!MatchesPptSession(original, reopened), "new service instance rejects old command");
	reopened = original; reopened.active = false;
	check(!MatchesPptSession(original, reopened), "ended show rejects queued command");

	BarA2CallbackDispatcher dispatcher;
	std::uint64_t first = 0, second = 0;
	dispatcher.SetStamped([&](std::uint64_t id) { first = id; });
	check(dispatcher.Dispatch() && !dispatcher.Dispatch(), "main exit single-flight spans pending dialog");
	dispatcher.Complete(first);
	dispatcher.SetStamped([&](std::uint64_t id) { second = id; });
	check(dispatcher.Dispatch() && second != first, "cancel allows a new request with different identity");
	dispatcher.Complete(first);
	check(!dispatcher.Dispatch(), "late completion cannot unlock a newer confirmation");
	dispatcher.Complete(second);
	dispatcher.SetStamped([](std::uint64_t) { throw 1; });
	check(!dispatcher.Dispatch(), "failed business dispatch releases its request");
	dispatcher.SetStamped([&](std::uint64_t id) { second = id; });
	check(dispatcher.Dispatch(), "dispatch works after failure");
	dispatcher.Complete(second);

	BarPptSceneState scene;
	check(scene.Publish(1, true) && scene.Take() == std::optional<bool>(true),
		"first live show creates exactly one enter transition");
	check(!scene.Publish(1, true) && !scene.Take(), "page heartbeat does not replay enter");
	check(scene.Publish(1, false) && scene.Take() == std::optional<bool>(false),
		"true exit creates exactly one exit transition");
	check(!scene.Publish(1, false), "duplicate exit is idempotent");
	BarPptSceneState rapidReentry;
	check(rapidReentry.Publish(1, true) && rapidReentry.Publish(1, false)
		&& rapidReentry.Publish(2, true), "native same-iteration exit and reentry are accepted");
	const auto reentryTransition = rapidReentry.Take();
	check(reentryTransition == std::optional<bool>(false) && rapidReentry.active
		&& rapidReentry.session == 2 && !rapidReentry.Take(),
		"pending exit obligation survives newer entry without replaying two animations");
	check(reentryTransition && ResolveBarPptSceneAction(*reentryTransition, false,
		BarBottomDockMode::Floating, false) == BarPptSceneAction::CenterDock,
		"expanded floating bar centers after exit even when next show already started");
	check(ResolveBarPptSceneAction(true, false, BarBottomDockMode::BottomDocked, false)
		== BarPptSceneAction::CenterDock, "expanded dock enters centered");
	check(ResolveBarPptSceneAction(true, false, BarBottomDockMode::Floating, false)
		== BarPptSceneAction::Keep, "expanded free bar stays free on entry");
	check(ResolveBarPptSceneAction(true, true, BarBottomDockMode::BottomDocked, false)
		== BarPptSceneAction::Detach, "folded dock releases constraints without expansion");
	check(ResolveBarPptSceneAction(false, false, BarBottomDockMode::Floating, false)
		== BarPptSceneAction::CenterDock, "expanded exit centers even a free bar");
	check(ResolveBarPptSceneAction(false, true, BarBottomDockMode::Floating, false)
		== BarPptSceneAction::Keep, "folded exit keeps position");
	check(ResolveBarPptSceneAction(false, false, BarBottomDockMode::BottomDocked, true)
		== BarPptSceneAction::Keep, "whiteboard owns geometry when underlying show ends");
	return failures;
}
