#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.CompositionClock.h"

#include <cmath>
#include <iostream>

namespace
{
	using namespace Inkeys::Drawing::Draw3;

	constexpr double kPeriod60 = 1000.0 / 60.0;

	bool Near(double actual, double expected, double tolerance = 1e-6)
	{
		return std::abs(actual - expected) <= tolerance;
	}

	CompositionClock MakeClock(double vblankMs, double periodMs, double nowMs = 0.0)
	{
		CompositionClock clock;
		clock.Update({ true, vblankMs, periodMs }, nowMs);
		return clock;
	}
}

int RunDraw3CompositionClockTests()
{
	int failures = 0;
	const auto expect = [&](bool condition, const char* name)
	{
		if (condition) return;
		++failures;
		std::cerr << "[CompositionClock] failed: " << name << '\n';
	};

	// 时钟：下一次垂直同步与截止时刻；垂直同步样本可在过去或未来。
	{
		const CompositionClock clock = MakeClock(100.0, kPeriod60);
		expect(clock.Valid() && Near(clock.PeriodMs(), kPeriod60), "valid sample");
		expect(Near(clock.NextVBlankMs(100.0), 100.0 + kPeriod60), "next vblank strictly after an exact vblank");
		expect(Near(clock.NextVBlankMs(110.0), 100.0 + kPeriod60), "next vblank inside the period");
		expect(Near(clock.NextVBlankMs(50.0), 100.0 - 2.0 * kPeriod60, 1e-3), "future sample projects backwards");
		expect(Near(clock.DeadlineAtOrAfter(105.0), 100.0 + kPeriod60 - kCompositionDeadlineMarginMs), "deadline is vblank minus margin");
		expect(Near(clock.DeadlineAtOrAfter(100.0 + kPeriod60 - 1.0), 100.0 + 2.0 * kPeriod60 - kCompositionDeadlineMarginMs),
			"inside the margin rolls to the next deadline");
		expect(Near(clock.DeadlineAtOrAfter(100.0 + kPeriod60 - kCompositionDeadlineMarginMs), 100.0 + kPeriod60 - kCompositionDeadlineMarginMs),
			"exact deadline is still reachable");
	}

	// 失效：读取失败、周期异常；约每 250 ms 重读，Invalidate 立即重读。
	{
		CompositionClock clock;
		expect(clock.NeedsRefresh(0.0) && !clock.Valid(), "fresh clock needs a sample");
		clock.Update({ false, 0.0, 0.0 }, 0.0);
		expect(!clock.Valid() && !clock.NeedsRefresh(10.0), "failed read invalidates and waits for reread interval");
		clock.Update({ true, 0.0, 0.5 }, 0.0);
		expect(!clock.Valid(), "period below 1 ms rejected");
		clock.Update({ true, 0.0, 150.0 }, 0.0);
		expect(!clock.Valid(), "period above 100 ms rejected");
		clock.Update({ true, 0.0, kPeriod60 }, 1000.0);
		expect(clock.Valid() && !clock.NeedsRefresh(1249.0) && clock.NeedsRefresh(1250.0), "reread every 250 ms");
		expect(clock.NeedsRefresh(900.0), "clock going backwards rereads");
		clock.Invalidate();
		expect(clock.NeedsRefresh(1001.0), "invalidate forces reread");
	}

	// 预设帧率与刷新率的关系。
	{
		expect(ClassifyCompositionCadence(1000.0 / 240.0, kPeriod60) == CompositionCadence::ThrottleToRefresh, "240/60 throttles");
		expect(ClassifyCompositionCadence(1000.0 / 120.0, kPeriod60) == CompositionCadence::ThrottleToRefresh, "120/60 throttles");
		expect(ClassifyCompositionCadence(1000.0 / 60.0, kPeriod60) == CompositionCadence::PhaseLock, "60/60 phase-locks");
		expect(ClassifyCompositionCadence(1000.0 / 60.0, 1000.0 / 59.94) == CompositionCadence::PhaseLock, "60/59.94 phase-locks");
		expect(ClassifyCompositionCadence(1000.0 / 30.0, kPeriod60) == CompositionCadence::PhaseLock, "30/60 divisor phase-locks");
		expect(ClassifyCompositionCadence(1000.0 / 60.0, 1000.0 / 120.0) == CompositionCadence::PhaseLock, "60/120 divisor phase-locks");
		expect(ClassifyCompositionCadence(1000.0 / 60.0, 1000.0 / 75.0) == CompositionCadence::CommitEveryFrame, "60/75 non-divisor");
		expect(ClassifyCompositionCadence(1000.0 / 120.0, 1000.0 / 144.0) == CompositionCadence::CommitEveryFrame, "120/144 non-divisor");
		expect(ClassifyCompositionCadence(0.0, kPeriod60) == CompositionCadence::CommitEveryFrame, "invalid interval commits every frame");
	}

	// 提交耗时估计：EMA 与最近 16 次最大值取较大者；无样本时用保守默认值。
	{
		CommitDurationEstimator estimator;
		expect(!estimator.HasSamples() && Near(estimator.EstimateMs(), kCommitEstimateDefaultMs), "default estimate");
		estimator.Add(1.0);
		estimator.Add(5.0);
		estimator.Add(1.0);
		expect(Near(estimator.EstimateMs(), 5.0), "recent max dominates");
		for (int index = 0; index < 16; ++index) estimator.Add(1.0);
		expect(estimator.EstimateMs() >= 1.0 && estimator.EstimateMs() < 1.05, "old spike leaves the window");
		estimator.Add(-1.0);
		estimator.Add(std::nan(""));
		expect(estimator.EstimateMs() < 1.05, "invalid samples ignored");
	}

	// 预设不高于刷新率：每帧都提交。
	{
		UlwCommitPlanner planner;
		const CompositionClock clock = MakeClock(0.0, kPeriod60);
		planner.OnCommitIssued(0.0, 0.0);
		expect(planner.Decide(1.0, kPeriod60, kPeriod60, false, clock).commit, "60/60 unaligned commits every frame");
		expect(planner.Decide(1.0, 1000.0 / 30.0, kPeriod60, true, clock).commit, "30/60 aligned commits every frame");
	}

	// 简单间隔节流（240/60）：不足一个刷新周期（留半帧容差）只合并；首帧总是提交。
	{
		UlwCommitPlanner planner;
		CompositionClock invalid;
		const double interval = 1000.0 / 240.0;
		expect(planner.Decide(0.0, interval, kPeriod60, false, invalid).commit, "first frame commits");
		planner.OnCommitIssued(0.0, 0.0);
		int commits = 1;
		double lastCommit = 0.0;
		bool spacingOk = true;
		for (int frame = 1; frame < 240; ++frame)
		{
			const double now = frame * interval;
			if (!planner.Decide(now, interval, kPeriod60, false, invalid).commit) continue;
			spacingOk = spacingOk && now - lastCommit >= kPeriod60 - interval * 0.5 - 1e-9;
			lastCommit = now;
			planner.OnCommitIssued(now, 0.0);
			++commits;
		}
		expect(spacingOk, "throttled commits are about one refresh apart");
		expect(commits >= 58 && commits <= 61, "about 60 commits per second at 240/60");
		planner.ForgetLastCommit();
		expect(planner.Decide(lastCommit + 1.0, interval, kPeriod60, false, invalid).commit, "forget commits immediately");
	}

	// 强制提交（整帧、空闲前收尾）不预支未来槽位：连续强制提交后，普通节流在一个周期后恢复。
	{
		UlwCommitPlanner planner;
		CompositionClock invalid;
		const double interval = 1000.0 / 240.0;
		for (int index = 0; index < 4; ++index) planner.OnCommitIssued(index * interval, 0.0, kPeriod60, true);
		const double lastForced = 3.0 * interval;
		expect(!planner.Decide(lastForced + interval, interval, kPeriod60, false, invalid).commit,
			"forced commit still occupies its period");
		expect(planner.Decide(lastForced + kPeriod60, interval, kPeriod60, false, invalid).commit,
			"normal throttle resumes one period after the last forced commit");
		planner.OnCommitIssued(lastForced + kPeriod60, 0.0, 0.0); // refresh=0 沿用上次周期
		expect(!planner.Decide(lastForced + kPeriod60 + interval, interval, kPeriod60, false, invalid).commit &&
			planner.Decide(lastForced + 2.0 * kPeriod60, interval, kPeriod60, false, invalid).commit,
			"refresh=0 keeps previous period");
		// 普通提交后不足半周期再强制提交：仍从强制提交起算，一个周期后恢复（不预支到下下个槽位）。
		UlwCommitPlanner mixed;
		mixed.OnCommitIssued(0.0, 0.0, kPeriod60);
		mixed.OnCommitIssued(12.5, 0.0, kPeriod60, true);
		expect(mixed.Decide(12.5 + kPeriod60, interval, kPeriod60, false, invalid).commit,
			"forced commit shortly before the slot resumes one period later");
	}

	// 简单间隔的长期提交率不超过刷新率（近刷新率组合、帧间隔抖动）：槽位推进保留余数。
	{
		struct RateCase { const char* name; double fps; double hz; double jitter; };
		constexpr RateCase cases[] = {
			{ "120/90", 120.0, 90.0, 0.0 }, { "240/165", 240.0, 165.0, 0.0 }, { "144/120", 144.0, 120.0, 0.0 },
			{ "240/60 jitter", 240.0, 60.0, 0.4 }, { "120/90 jitter", 120.0, 90.0, 0.3 },
		};
		for (const RateCase& rate : cases)
		{
			UlwCommitPlanner planner;
			CompositionClock invalid;
			const double interval = 1000.0 / rate.fps;
			const double refresh = 1000.0 / rate.hz;
			std::uint32_t jitterSeed = 12345u;
			int commits = 0;
			double now = 0.0;
			for (int frame = 0; frame < static_cast<int>(rate.fps) * 2; ++frame)
			{
				jitterSeed = jitterSeed * 1664525u + 1013904223u;
				const double jitter = ((jitterSeed >> 8) % 1000 / 999.0 - 0.5) * 2.0 * rate.jitter * interval;
				const double sample = now + jitter;
				if (planner.Decide(sample, interval, refresh, false, invalid).commit)
				{
					planner.OnCommitIssued(sample, 0.0, refresh);
					++commits;
				}
				now += interval;
			}
			// 两秒内最多 2×刷新率次（首帧另加 1），且不能退化到远低于刷新率。
			const int limit = static_cast<int>(rate.hz * 2.0) + 1;
			if (commits > limit || commits < static_cast<int>(rate.hz * 2.0 * 0.85))
			{
				++failures;
				std::cerr << "[CompositionClock] failed: long-term rate " << rate.name << " commits=" << commits
					<< " limit=" << limit << '\n';
			}
		}
	}

	// 对齐节流（240/60）：每个周期提交一次，且是赶得上截止的最后一帧。
	{
		UlwCommitPlanner planner;
		const CompositionClock clock = MakeClock(0.0, kPeriod60);
		const double interval = 1000.0 / 240.0;
		for (int sample = 0; sample < 16; ++sample) planner.OnCommitCompleted(1.0);
		planner.OnCommitIssued(-kPeriod60, -kCompositionDeadlineMarginMs);
		int commits = 0;
		bool oncePerPeriod = true;
		bool lastReachable = true;
		double lastDeadline = -1.0;
		for (int frame = 0; frame < 240; ++frame)
		{
			const double now = 0.3 + frame * interval;
			const UlwCommitDecision decision = planner.Decide(now, interval, kPeriod60, true, clock);
			if (!decision.commit) continue;
			oncePerPeriod = oncePerPeriod && decision.deadlineMs > lastDeadline + kPeriod60 * 0.5;
			// 本帧之后的下一帧（含抖动余量）已赶不上同一截止。
			lastReachable = lastReachable && now + 1.0 <= decision.deadlineMs &&
				now + interval * (1.0 + kCompositionNextFrameSlackRatio) + 1.0 > decision.deadlineMs;
			lastDeadline = decision.deadlineMs;
			planner.OnCommitIssued(now, decision.deadlineMs);
			++commits;
		}
		expect(oncePerPeriod, "aligned throttle commits at most once per period");
		expect(lastReachable, "aligned commit is the last frame that reaches the deadline");
		expect(commits >= 58 && commits <= 61, "about 60 aligned commits per second at 240/60");
	}

	// 对齐安全网：时钟异常导致两周期未提交时强制提交。
	{
		UlwCommitPlanner planner;
		const CompositionClock clock = MakeClock(0.0, kPeriod60);
		planner.OnCommitIssued(0.0, 1000.0); // 伪造远期截止
		expect(planner.Decide(2.0 * kPeriod60 + 0.1, 1000.0 / 240.0, kPeriod60, true, clock).commit, "overdue forces commit");
	}

	// 相位微调：每帧最多 10%，逐步收敛到“开始 + 工作 + 提交 ≈ 截止”；非约数与无工作样本不调。
	{
		UlwCommitPlanner planner;
		const CompositionClock clock = MakeClock(0.0, kPeriod60);
		const double interval = kPeriod60;
		expect(Near(planner.PhaseAdjustMs(0.0, interval, clock), 0.0), "no work sample no shift");
		planner.OnFrameWorkMeasured(3.0);
		for (int sample = 0; sample < 16; ++sample) planner.OnCommitCompleted(1.0);
		double start = 5.0; // 完成于 9 ms，距截止 14.67 约 5.67 ms
		bool bounded = true;
		for (int frame = 0; frame < 40; ++frame)
		{
			const double shift = planner.PhaseAdjustMs(start, interval, clock);
			bounded = bounded && std::abs(shift) <= interval * kCompositionPhaseStepRatio + 1e-9;
			start += interval + shift;
		}
		const double finish = start + 3.0 + 1.0;
		const double deadline = clock.DeadlineAtOrAfter(finish - 1e-6);
		expect(bounded, "phase step bounded to 10% per frame");
		expect(std::abs(finish - deadline) < 0.05 || std::abs(finish - (deadline - kPeriod60)) < 0.05, "phase converges onto a deadline");
		expect(Near(planner.PhaseAdjustMs(0.0, 1000.0 / 48.0, MakeClock(0.0, 1000.0 / 75.0)), 0.0),
			"non-divisor cadence never shifts");
		expect(Near(planner.PhaseAdjustMs(0.0, 1000.0 / 240.0, clock), 0.0), "throttle cadence never shifts");
		expect(Near(planner.PhaseAdjustMs(0.0, interval, CompositionClock{}), 0.0), "invalid clock never shifts");
	}

	std::cout << "[CompositionClock] failures=" << failures << '\n';
	return failures;
}
