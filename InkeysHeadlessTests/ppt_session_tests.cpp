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
