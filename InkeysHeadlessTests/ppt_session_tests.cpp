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
