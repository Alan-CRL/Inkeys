#include <chrono>
#include <iostream>
#include <string_view>

import Inkeys.UI.Bar.ToggleClickCoalescer;

namespace
{
	using namespace Inkeys::UI::Bar;
	using namespace std::chrono_literals;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL toggle_click_coalescer " << name << '\n';
	}

	void TestSameChannelMergesWithinWindow()
	{
		using Clock = BarToggleClickCoalescer::Clock;
		const auto start = Clock::time_point{};
		BarToggleClickCoalescer coalescer(300ms);

		Check(coalescer.TryBegin(BarToggleChannel::Main, start),
			"first toggle executes immediately");
		Check(!coalescer.TryBegin(BarToggleChannel::Main, start + 299ms),
			"same toggle is merged inside 300 ms");
		Check(coalescer.TryBegin(BarToggleChannel::Main, start + 300ms),
			"same toggle executes at the 300 ms boundary");
	}

	void TestChannelsAreIndependent()
	{
		using Clock = BarToggleClickCoalescer::Clock;
		const auto start = Clock::time_point{};
		BarToggleClickCoalescer coalescer(300ms);

		Check(coalescer.TryBegin(BarToggleChannel::DrawAttribute, start),
			"draw attribute toggle executes");
		Check(coalescer.TryBegin(BarToggleChannel::GeometryAttribute, start),
			"geometry attribute has an independent channel");
		Check(coalescer.TryBegin(BarToggleChannel::More, start),
			"more has an independent channel");
		Check(coalescer.TryBegin(BarToggleChannel::ThicknessAdjust, start),
			"thickness adjust has an independent channel");
		Check(coalescer.TryBegin(BarToggleChannel::PenTypeMenu, start),
			"pen type menu has an independent channel");
	}

	void TestNonMonotonicTimeStartsNewWindow()
	{
		using Clock = BarToggleClickCoalescer::Clock;
		const auto start = Clock::time_point{} + 1s;
		BarToggleClickCoalescer coalescer(300ms);

		Check(coalescer.TryBegin(BarToggleChannel::Main, start),
			"initial toggle executes before clock reset");
		Check(coalescer.TryBegin(BarToggleChannel::Main, start - 1ms),
			"non-monotonic injected time rebases the channel");
	}
}

int RunToggleClickCoalescerTests()
{
	failureCount = 0;
	TestSameChannelMergesWithinWindow();
	TestChannelsAreIndependent();
	TestNonMonotonicTimeStartsNewWindow();
	return failureCount;
}
