#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../Inkeys/Inkeys/Helper/Ui3PresentationFixtureAuth.h"

#include <array>
#include <iostream>

static_assert(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS == 0);

int RunUi3HarnessDisabledTests()
{
	using Inkeys::UI::Bar::TryRunUi3PresentationFixtureEarly;
	int failures = 0;
	// 实际调用禁用分支：完整、错位、大小写和坏引用均不得落入产品初始化。
	constexpr std::array rejected{
		L"Inkeys.exe --inkeys-internal-ui3-fixture-child-v1 1 2 3 4 C:\\private C:\\parent.exe",
		L"Inkeys.exe --ui3-presentation-benchmark --scene main-fold --round 1 --capture off --capacity 0",
		L"Inkeys.exe --ui3-fixture-auth-tests",
		L"Inkeys.exe --ordinary --ui3-fixture-auth-tests",
		L"Inkeys.exe --UI3-PRESENTATION-BENCHMARK",
		L"Inkeys.exe \"bad --ui3-presentation-benchmark tail\"",
		L"Inkeys.exe \"bad --ui3-fixture-auth-tests tail\"",
		L"Inkeys.exe \"bad --inkeys-internal-ui3-fixture-child-v1 tail\"",
	};
	for (const auto* command : rejected)
	{
		int exitCode = 0;
		if (!TryRunUi3PresentationFixtureEarly(command, exitCode) || exitCode != 81)
		{
			std::wcerr << L"[Ui3HarnessDisabled] internal mode was not rejected: " << command << L'\n';
			++failures;
		}
	}
	constexpr std::array ordinary{
		L"Inkeys.exe", L"Inkeys.exe --ordinary", L"Inkeys.exe --ui3-fixture-auth-tests-backup",
		L"Inkeys.exe \"C:\\folder\\ui3-presentation-benchmark.txt\"",
	};
	for (const auto* command : ordinary)
	{
		int exitCode = 123;
		if (TryRunUi3PresentationFixtureEarly(command, exitCode) || exitCode != 123)
		{
			std::wcerr << L"[Ui3HarnessDisabled] ordinary arguments changed: " << command << L'\n';
			++failures;
		}
	}
	int exitCode = 123;
	if (TryRunUi3PresentationFixtureEarly(nullptr, exitCode) || exitCode != 123) ++failures;
	return failures;
}
