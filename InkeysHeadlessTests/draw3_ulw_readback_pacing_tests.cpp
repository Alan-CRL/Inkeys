#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.UlwReadbackPacing.h"

#include <iostream>

namespace
{
	using namespace Inkeys::Drawing::Draw3;
	constexpr double kBudget = 1000.0 / 120.0;
}

int RunDraw3UlwReadbackPacingTests()
{
	int failures = 0;
	const auto expect = [&](bool condition, const char* name)
	{
		if (condition) return;
		++failures;
		std::cerr << "[UlwReadbackPacing] failed: " << name << '\n';
	};

	// 默认即时模式：固定帧截止等待与空闲等待都立即完成。
	{
		UlwReadbackPacing pacing;
		expect(pacing.Mode() == UlwReadbackMode::Immediate, "starts immediate");
		expect(pacing.ShouldCompleteAtFrameEnd(false, 0.0) && pacing.ShouldCompleteAtFrameEnd(true, 1.0),
			"immediate completes at every frame end");
	}

	// 连续 3 帧 GPU 拖慢才进入流水；中间一帧正常则重新计数；只超预算但等待很短不算。
	{
		UlwReadbackPacing pacing;
		const double slowWait = kBudget * 0.5;
		expect(pacing.OnFrameMeasured(kBudget * 1.5, kBudget, slowWait, 10.0) == UlwReadbackTransition::None &&
			pacing.OnFrameMeasured(kBudget * 1.5, kBudget, slowWait, 20.0) == UlwReadbackTransition::None,
			"two slow frames stay immediate");
		expect(pacing.OnFrameMeasured(kBudget * 0.8, kBudget, slowWait, 30.0) == UlwReadbackTransition::None,
			"frame within budget resets streak");
		expect(pacing.OnFrameMeasured(kBudget * 1.5, kBudget, kBudget * 0.1, 40.0) == UlwReadbackTransition::None &&
			pacing.OnFrameMeasured(kBudget * 1.5, kBudget, kBudget * 0.1, 50.0) == UlwReadbackTransition::None &&
			pacing.OnFrameMeasured(kBudget * 1.5, kBudget, kBudget * 0.1, 60.0) == UlwReadbackTransition::None,
			"CPU-bound slow frames never enter pipelined");
		pacing.OnFrameMeasured(kBudget * 1.5, kBudget, slowWait, 70.0);
		pacing.OnFrameMeasured(kBudget * 1.5, kBudget, slowWait, 80.0);
		expect(pacing.OnFrameMeasured(kBudget * 1.5, kBudget, slowWait, 90.0) == UlwReadbackTransition::EnteredPipelined &&
			pacing.Mode() == UlwReadbackMode::Pipelined, "three GPU-bound frames enter pipelined");
	}

	// 流水模式：固定帧截止等待时延后完成，空闲等待前必须完成，每 500 ms 试一帧即时。
	{
		UlwReadbackPacing pacing;
		for (int frame = 0; frame < 3; ++frame) pacing.OnFrameMeasured(kBudget * 2, kBudget, kBudget, 100.0);
		expect(pacing.Mode() == UlwReadbackMode::Pipelined, "setup pipelined at 100 ms");
		expect(!pacing.ShouldCompleteAtFrameEnd(false, 200.0) && !pacing.TrialFrame(), "pipelined defers deadline frame");
		expect(pacing.ShouldCompleteAtFrameEnd(true, 210.0) && !pacing.TrialFrame(), "idle wait always completes");
		expect(pacing.OnFrameMeasured(kBudget * 0.5, kBudget, 0.0, 210.0) == UlwReadbackTransition::None &&
			pacing.Mode() == UlwReadbackMode::Pipelined, "fast non-trial frame does not leave pipelined");
		expect(!pacing.ShouldCompleteAtFrameEnd(false, 599.0), "no trial before 500 ms");
		expect(pacing.ShouldCompleteAtFrameEnd(false, 600.0) && pacing.TrialFrame(), "trial frame after 500 ms");
		expect(pacing.OnFrameMeasured(kBudget * 0.95, kBudget, kBudget * 0.5, 600.0) == UlwReadbackTransition::None &&
			pacing.Mode() == UlwReadbackMode::Pipelined, "slow trial stays pipelined");
		expect(!pacing.ShouldCompleteAtFrameEnd(false, 1000.0), "next trial waits another 500 ms");
		expect(pacing.ShouldCompleteAtFrameEnd(false, 1100.0) && pacing.TrialFrame(), "second trial");
		expect(pacing.OnFrameMeasured(kBudget * 0.9, kBudget, 1.0, 1100.0) == UlwReadbackTransition::ReturnedImmediate &&
			pacing.Mode() == UlwReadbackMode::Immediate, "trial within 90% returns immediate");
		expect(pacing.ShouldCompleteAtFrameEnd(false, 1110.0), "immediate again completes deadline frames");
	}

	// 非法输入不改变状态；Reset 回到即时模式。
	{
		UlwReadbackPacing pacing;
		for (int frame = 0; frame < 5; ++frame) pacing.OnFrameMeasured(kBudget * 2, 0.0, kBudget, 0.0);
		expect(pacing.Mode() == UlwReadbackMode::Immediate, "zero budget ignored");
		for (int frame = 0; frame < 3; ++frame) pacing.OnFrameMeasured(kBudget * 2, kBudget, kBudget, 0.0);
		pacing.Reset();
		expect(pacing.Mode() == UlwReadbackMode::Immediate && !pacing.TrialFrame(), "reset returns immediate");
	}

	std::cout << "[UlwReadbackPacing] failures=" << failures << '\n';
	return failures;
}
