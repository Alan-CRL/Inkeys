#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

import Inkeys.Startup.Progress;

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[StartupProgress] failed: " << name << '\n';
		return condition;
	}
}

int RunStartupProgressTests()
{
	using namespace Inkeys::Startup;
	int failures = 0;
	const auto enabledPlan = Plan::ForStartup(true);
	const auto disabledPlan = Plan::ForStartup(false);
	if (!Expect(enabledPlan.totalUnits == 1000,
		"enabled plan has nominal 1000 units")) ++failures;
	if (!Expect(enabledPlan.Contains(Milestone::PreviewGeometryReady)
		&& enabledPlan.Weight(Milestone::PreviewGeometryReady) == 20,
		"preview geometry contributes the former 20-unit slot")) ++failures;
	if (!Expect(disabledPlan.totalUnits == 940
		&& !disabledPlan.Contains(Milestone::PreviewGeometryReady)
		&& !disabledPlan.Contains(Milestone::PreviewOwnerReady)
		&& disabledPlan.Contains(Milestone::RenderDeviceReady),
		"disabled plan removes only preview work")) ++failures;

	ProgressTracker tracker(enabledPlan,
		std::chrono::steady_clock::time_point(std::chrono::seconds(7)));
	if (!Expect(tracker.Complete(Milestone::DisplayReady)
		&& !tracker.Complete(Milestone::DisplayReady),
		"duplicate completion increments once")) ++failures;
	if (!Expect(tracker.Complete(Milestone::SuperTopCrossed),
		"out of order completion is accepted")) ++failures;
	const auto beforeTime = tracker.GetSnapshot();
	std::this_thread::sleep_for(std::chrono::milliseconds(2));
	const auto afterTime = tracker.GetSnapshot();
	if (!Expect(beforeTime.completedUnits == afterTime.completedUnits
		&& beforeTime.actualRatio == afterTime.actualRatio
		&& afterTime.startTime == std::chrono::steady_clock::time_point(
			std::chrono::seconds(7)),
		"elapsed time never advances actual progress")) ++failures;

	ProgressTracker concurrent(enabledPlan);
	std::vector<std::jthread> reporters;
	for (int index = 0; index < 12; ++index)
		reporters.emplace_back([&]
			{
				for (int repeat = 0; repeat < 500; ++repeat)
				{
					(void)concurrent.Complete(Milestone::LoggingReady);
					(void)concurrent.Complete(Milestone::ComReady);
				}
			});
	reporters.clear();
	const auto concurrentSnapshot = concurrent.GetSnapshot();
	if (!Expect(concurrentSnapshot.completedUnits == 45
		&& concurrentSnapshot.revision == 2,
		"concurrent duplicate reporters merge exactly once")) ++failures;

	ProgressTracker completion(enabledPlan);
	for (unsigned index = 0;
		index < static_cast<unsigned>(Milestone::Count); ++index)
	{
		const auto milestone = static_cast<Milestone>(index);
		if (milestone != Milestone::BarFirstFrameCommitted)
			(void)completion.Complete(milestone);
	}
	const auto gated = completion.GetSnapshot();
	if (!Expect(gated.completedUnits == 960 && gated.actualRatio < 1.0,
		"bar committed milestone is the only 100 percent gate")) ++failures;
	(void)completion.Complete(Milestone::BarFirstFrameCommitted);
	if (!Expect(completion.GetSnapshot().actualRatio == 1.0,
		"committed Bar frame completes the plan")) ++failures;

	ProgressTracker earlyBar(enabledPlan);
	(void)earlyBar.Complete(Milestone::BarFirstFrameCommitted);
	if (!Expect(earlyBar.GetSnapshot().actualRatio < 1.0,
		"out-of-order Bar commit cannot complete unfinished work")) ++failures;
	for (unsigned index = 0;
		index < static_cast<unsigned>(Milestone::Count); ++index)
		(void)earlyBar.Complete(static_cast<Milestone>(index));
	if (!Expect(earlyBar.GetSnapshot().actualRatio == 1.0,
		"early Bar gate reaches 100 only after every real milestone")) ++failures;

	ProgressTracker failed(enabledPlan);
	(void)failed.Complete(Milestone::SuperTopCrossed);
	if (!Expect(failed.Fail(0x1234u) && !failed.Fail(0x5678u),
		"first failure freezes failure code")) ++failures;
	const auto frozen = failed.GetSnapshot();
	if (!Expect(!failed.Complete(Milestone::BarFirstFrameCommitted),
		"milestones are rejected after failure")) ++failures;
	const auto stillFrozen = failed.GetSnapshot();
	if (!Expect(stillFrozen.failed && stillFrozen.failureCode == 0x1234u
		&& stillFrozen.completedUnits == frozen.completedUnits
		&& stillFrozen.revision == frozen.revision,
		"failure freezes ratio and revision")) ++failures;

	ProgressTracker disabled(disabledPlan);
	if (!Expect(!disabled.Complete(Milestone::PreviewOwnerReady),
		"unplanned milestone cannot be marked complete")) ++failures;
	SetActiveTracker(&disabled);
	if (!Expect(Report(Milestone::LoggingReady)
		&& ActiveSnapshot().completedUnits == disabledPlan.Weight(
			Milestone::LoggingReady),
		"global producer bridge reports to the active tracker")) ++failures;
	ClearActiveTracker(&disabled);
	if (!Expect(!Report(Milestone::ComReady),
		"cleared producer bridge rejects late reports")) ++failures;
	return failures;
}
