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
	const auto stableEnd = ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", endTopology));
	check(stableEnd.snapshot && Inkeys::Drawing::Draw3::ResolveEndScreenTarget(stableEnd.snapshot->descriptor)
		&& ResolvePptControlPage(*stableEnd.snapshot, -1, 3).known
		&& !ResolvePptControlPage(*stableEnd.snapshot, 3, 3).known
		&& !ResolvePptControlPage(*stableEnd.snapshot, -1, 4).known,
		"stable end topology still requires matching raw end state and total before writable admission");
	check(ParsePptSessionSnapshot(envelope(L"Unknown", L"Unknown", unavailable)).snapshot.has_value(),
		"read failures stay unknown instead of exiting the workspace");
	check(!ParsePptSessionSnapshot(envelope(L"Active", L"Valid", unavailable)).snapshot,
		"unavailable descriptor cannot open input through valid status");
	const std::wstring scalarOnly = LR"({"schemaVersion":1,"provider":"PowerPoint","status":"Unavailable","fullName":"","presentationName":"","applicationProcessId":0,"slideShowHwnd":0,"currentPage":2,"totalPage":3,"currentSlideId":null,"slideIds":[],"bindingRevision":9})";
	const auto parsedBasic = ParsePptSessionSnapshot(envelope(L"Active", L"Unknown", scalarOnly));
	check(parsedBasic.snapshot && ResolvePptControlPage(*parsedBasic.snapshot, 2, 3).known
		&& !Inkeys::Drawing::Draw3::ResolvePresentationTarget(parsedBasic.snapshot->descriptor),
		"real wire parser accepts fresh scalar controls while refusing an ink identity");
	if (valid.snapshot)
	{
		auto windowless = *valid.snapshot;
		windowless.descriptor.slideShowHwnd = 0;
		windowless.descriptor.applicationProcessId = 0;
		const auto basic = ResolvePptControlPage(windowless, 2, 3);
		check(CanStartPptControlSession(windowless) && basic.known
			&& basic.currentPage == 2 && basic.totalPage == 3,
			"fresh owner page keeps basic controls without a typed HWND");
		check(!ResolvePptControlPage(windowless, 1, 3).known
			&& !ResolvePptControlPage(windowless, 2, 4).known,
			"raw and owner page disagreement cannot publish fresh basic numbers");
		windowless.descriptor.status =
			Inkeys::Drawing::Draw3::PresentationDescriptorStatus::PageIndexFallback;
		windowless.descriptor.slideIds.clear();
		windowless.descriptor.currentSlideId.reset();
		windowless.descriptor.fullName.clear();
		windowless.descriptor.presentationName.clear();
		check(ResolvePptControlPage(windowless, 2, 3).known,
			"missing metadata and stable IDs do not block basic page controls");
		windowless.pageStatus = PptPageStatus::Unknown;
		windowless.descriptor.status =
			Inkeys::Drawing::Draw3::PresentationDescriptorStatus::Unavailable;
		check(ResolvePptControlPage(windowless, 2, 3).known,
			"fresh scalar page evidence survives a metadata-unavailable descriptor");
		windowless.descriptor.status =
			Inkeys::Drawing::Draw3::PresentationDescriptorStatus::TransientBusy;
		check(CanStartPptControlSession(windowless)
			&& !ResolvePptControlPage(windowless, 2, 3).known,
			"busy preserves an active scene without making an old raw page fresh");
		windowless.lifecycle = PptLifecycle::Unknown;
		check(!CanStartPptControlSession(windowless),
			"a failed activity probe cannot create a basic control session");
	}
	const auto basicEnd = ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", unavailable));
	check(basicEnd.snapshot && ResolvePptControlPage(*basicEnd.snapshot, -1, 3).known
		&& !ResolvePptControlPage(*basicEnd.snapshot, 3, 3).known
		&& !Inkeys::Drawing::Draw3::ResolveEndScreenTarget(basicEnd.snapshot->descriptor),
		"explicit end state can publish basic controls without fabricating topology");
	// Done/Count 是本次证据；缺拓扑和 HWND 时也只发布基础结束控制，不生成墨迹目标。
	const std::wstring scalarEnd = LR"({"schemaVersion":1,"provider":"Wps","status":"Unavailable","fullName":"C:\\Lessons\\Deck.pptx","presentationName":"Deck.pptx","applicationProcessId":0,"slideShowHwnd":0,"currentPage":0,"totalPage":3,"currentSlideId":null,"slideIds":[],"bindingRevision":9})";
	const auto parsedBasicEnd = ParsePptSessionSnapshot(envelope(L"Active", L"EndScreen", scalarEnd));
	check(parsedBasicEnd.snapshot
		&& ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 3).known
		&& ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 3).currentPage == -1
		&& ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 3).totalPage == 3
		&& !ResolvePptControlPage(*parsedBasicEnd.snapshot, -1, 4).known
		&& !Inkeys::Drawing::Draw3::ResolvePresentationTarget(parsedBasicEnd.snapshot->descriptor)
		&& !Inkeys::Drawing::Draw3::ResolveEndScreenTarget(parsedBasicEnd.snapshot->descriptor),
		"fresh windowless Done wire keeps basic end controls while refusing all ink targets");

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
	auto noWindow = original;
	noWindow.showWindow = 0; noWindow.processId = 0;
	check(!IsPptWindowConfirmedEnded(noWindow, false, 0)
		&& MatchesPptSession(noWindow, noWindow),
		"HWND zero is an unknown window, not proof of COM show exit");
	check(IsPptWindowConfirmedEnded(original, false, 0)
		&& IsPptWindowConfirmedEnded(original, true, original.processId + 1)
		&& !IsPptWindowConfirmedEnded(original, true, 0),
		"only a destroyed window or successfully observed different owner ends the show");
	original.processStartTime = 100;
	check(IsPptWindowConfirmedEnded(original, true, original.processId, 200)
		&& !IsPptWindowConfirmedEnded(original, true, original.processId, 0),
		"known process creation identity detects PID reuse without guessing on query failure");
	if (valid.snapshot)
	{
		auto sameShow = *valid.snapshot;
		auto matching = noWindow;
		matching.showSessionRevision = sameShow.showSessionRevision;
		matching.bindingRevision = sameShow.bindingRevision;
		check(MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"basic scene identity is independent of a window and drawing readiness");
		sameShow.lifecycle = PptLifecycle::Unknown;
		check(MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"same show read failure may retain the previous suspended display");
		++sameShow.bindingRevision;
		check(!MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"new document cannot borrow an old scene or ink after busy");
		--sameShow.bindingRevision; ++sameShow.showSessionRevision;
		check(!MatchesPptShowSession(matching, matching.serviceGeneration, sameShow),
			"windowless reentry still requires a new control session");
	}

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
	check(ShouldPlaceBarWorkspaceDock(false, BarBottomDockMode::BottomDocked),
		"expanded dock follows scene bottom without changing centering mode");
	check(!ShouldPlaceBarWorkspaceDock(true, BarBottomDockMode::BottomDocked) &&
		!ShouldPlaceBarWorkspaceDock(false, BarBottomDockMode::Floating) &&
		!ShouldPlaceBarWorkspaceDock(true, BarBottomDockMode::Floating),
		"folded and floating bars preserve their geometry on scene entry");
	check(ResolveBarWorkspaceBottomInsetDip(false, true, false, false) == 5.0
		&& ResolveBarWorkspaceBottomInsetDip(false, true, false, true) == 0.0
		&& ResolveBarWorkspaceBottomInsetDip(false, false, true, false) == 5.0
		&& ResolveBarWorkspaceBottomInsetDip(true, true, false, false) == 0.0,
		"whiteboard minimize switches to work-area dock without ending scene");
	BarWorkspaceSceneState workspace;
	check(workspace.Update(false, true) == BarWorkspaceTransition::Enter,
		"desktop to presentation captures baseline");
	check(workspace.Update(true, true) == BarWorkspaceTransition::Switch,
		"presentation to whiteboard preserves baseline");
	check(workspace.Update(true, false) == BarWorkspaceTransition::Keep,
		"background presentation exit cannot restore desktop under whiteboard");
	check(workspace.Update(false, false) == BarWorkspaceTransition::Leave,
		"last workspace exit restores desktop baseline");
	check(workspace.Update(true, false) == BarWorkspaceTransition::Enter,
		"desktop to whiteboard captures a fresh baseline");
	check(workspace.Update(false, true) == BarWorkspaceTransition::Switch,
		"return to a live show preserves baseline");
	check(workspace.Update(false, true) == BarWorkspaceTransition::Keep,
		"heartbeat and minimizing do not replay entry");
	check(workspace.Update(false, false) == BarWorkspaceTransition::Leave &&
		workspace.Update(false, false) == BarWorkspaceTransition::Keep,
		"desktop restoration occurs exactly once");
	return failures;
}
