#include <iostream>
#include <string_view>

import Inkeys.UI.Freeze;

namespace
{
	int failures = 0;
	Inkeys::UI::Freeze::StateSnapshot observed{};
	int observerCalls = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAIL " << name << '\n';
	}

	void Observe(Inkeys::UI::Freeze::StateSnapshot snapshot) noexcept
	{
		observed = snapshot;
		++observerCalls;
	}
}

int RunFreezeStateTests()
{
	using namespace Inkeys::UI::Freeze;
	SetPresentationActive(false);
	SetWhiteboardActive(false);
	Check(IsAvailable() && !IsActive(), "freeze starts available and inactive");
	SetStateObserver(&Observe);
	const StateSnapshot initial = Snapshot();
	Check(observerCalls == 1 && observed.revision == initial.revision,
		"freeze observer receives current version on registration");
	Toggle();
	Check(IsActive(), "toggle enables freeze");
	const StateSnapshot enabled = Snapshot();
	Check(enabled.revision > initial.revision && observed.revision == enabled.revision,
		"freeze toggle publishes a newer atomic version");
	Check(!DeactivateIfRevision(initial.revision) && IsActive(),
		"freeze stale failure cannot deactivate newer request");
	Check(DeactivateIfRevision(enabled.revision) && !IsActive(),
		"freeze matching failure deactivates only its request");
	Toggle();
	SetPresentationActive(true);
	Check(!IsActive() && !IsAvailable(), "presentation entry disables and closes freeze");
	SetPresentationActive(false);
	Check(IsAvailable() && !IsActive(), "presentation exit does not restore freeze");
	Toggle();
	SetWhiteboardActive(true);
	Check(!IsActive() && !IsAvailable(), "whiteboard entry disables and closes freeze");
	SetWhiteboardActive(false);
	Check(IsAvailable() && !IsActive(), "whiteboard exit does not restore freeze");
	SetStateObserver(nullptr);
	return failures;
}
