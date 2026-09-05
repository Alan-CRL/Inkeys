#include <iostream>
#include <string_view>

import Inkeys.UI.Freeze;

namespace
{
	int failures = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAIL " << name << '\n';
	}
}

int RunFreezeStateTests()
{
	using namespace Inkeys::UI::Freeze;
	SetPresentationActive(false);
	SetWhiteboardActive(false);
	Check(IsAvailable() && !IsActive(), "freeze starts available and inactive");
	Toggle();
	Check(IsActive(), "toggle enables freeze");
	SetPresentationActive(true);
	Check(!IsActive() && !IsAvailable(), "presentation entry disables and closes freeze");
	SetPresentationActive(false);
	Check(IsAvailable() && !IsActive(), "presentation exit does not restore freeze");
	Toggle();
	SetWhiteboardActive(true);
	Check(!IsActive() && !IsAvailable(), "whiteboard entry disables and closes freeze");
	SetWhiteboardActive(false);
	Check(IsAvailable() && !IsActive(), "whiteboard exit does not restore freeze");
	return failures;
}
