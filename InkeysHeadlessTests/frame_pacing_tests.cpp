#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

import Inkeys.UI.Bar.FramePacing;

namespace
{
	using namespace Inkeys::UI::Bar;

	constexpr std::array RemainingWaitBucketsMs{
		0.25, 0.5, 1.0, 1.5, 1.9, 2.0, 2.1, 4.0, 8.0, 12.0, 16.0,
	};
	constexpr std::array BenchmarkWaitBucketsMs{ 2.0, 8.0, 12.0, 16.0 };
	constexpr double EarlyWakeToleranceMs = 0.05;

	int failureCount = 0;

	void Check(bool condition, std::string_view name, double bucketMs = -1.0)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL frame_pacing " << name;
		if (bucketMs >= 0.0) std::cerr << " bucket_ms=" << bucketMs;
		std::cerr << '\n';
	}

	bool Near(double lhs, double rhs, double epsilon = 0.000001)
	{
		return std::abs(lhs - rhs) <= epsilon;
	}

	void TestAnimationFrameClockRebase()
	{
		using Clock = FrameAnimationClock::Clock;
		const auto start = Clock::time_point{};
		FrameAnimationClock clock(start);
		Check(Near(clock.Tick(start + std::chrono::milliseconds(16)), 0.016),
			"animation clock reports active frame delta");

		const auto idleWake = start + std::chrono::hours(2);
		clock.Rebase(idleWake);
		Check(Near(clock.Tick(idleWake + std::chrono::milliseconds(2)), 0.002),
			"idle rebase excludes sleep from first animation frame");
	}

	void TestPoliciesAndMeasurements()
	{
		constexpr FramePacingWaitPolicy defaultPolicy = FramePacingWaitPolicy::Default();
		Check(defaultPolicy.strategy == FramePacingWaitStrategy::WaitableTimer,
			"default strategy is waitable timer");
		Check(Near(defaultPolicy.fixedSpinTailMs, 0.25),
			"default waitable timer spin tail is 0.25 ms");
		constexpr FramePacingWaitPolicy legacy = FramePacingWaitPolicy::Legacy();
		Check(legacy.strategy == FramePacingWaitStrategy::FixedSpinTail,
			"legacy strategy is fixed spin tail");
		Check(Near(legacy.fixedSpinTailMs, 1.5), "legacy spin tail is 1.5 ms");
		constexpr FramePacingWaitPolicy waitableTimer =
			FramePacingWaitPolicy::WaitableTimer(0.25);
		Check(waitableTimer.strategy == FramePacingWaitStrategy::WaitableTimer,
			"waitable timer policy strategy");
		Check(Near(waitableTimer.fixedSpinTailMs, 0.25),
			"waitable timer spin tail");

		constexpr std::array fixedTails{ 0.5, 0.75, 1.0, 1.5 };
		for (double tailMs : fixedTails)
		{
			auto policy = FramePacingWaitPolicy::FixedSpinTail(tailMs);
			Check(policy.strategy == FramePacingWaitStrategy::FixedSpinTail,
				"fixed policy strategy");
			Check(Near(policy.fixedSpinTailMs, tailMs), "fixed policy tail");
		}

		// 普通测试逐桶验证旧 2 ms 门槛，同时确认显式测量字段保持自洽。
		for (double bucketMs : RemainingWaitBucketsMs)
		{
			FramePacingMeasurement measurement{};
			WaitForRemainingFrameTime(bucketMs, legacy, nullptr, &measurement);
			Check(Near(measurement.requestedWaitMs, bucketMs),
				"requested wait is recorded", bucketMs);
			Check(measurement.elapsedMs >= 0.0 && std::isfinite(measurement.elapsedMs),
				"elapsed wait is finite", bucketMs);
			Check(measurement.elapsedMs + EarlyWakeToleranceMs >= bucketMs,
				"elapsed wait reaches requested lower bound", bucketMs);
			Check(measurement.spinMs >= 0.0 && std::isfinite(measurement.spinMs),
				"legacy path records finite spin time", bucketMs);
			Check(bucketMs <= 2.0 ? measurement.sleepMs == 0.0 : measurement.sleepMs > 0.0,
				"legacy 2 ms sleep gate", bucketMs);
			Check(Near(
				measurement.signedOvershootMs,
				measurement.elapsedMs - measurement.requestedWaitMs),
				"signed overshoot is elapsed minus requested", bucketMs);
			if (measurement.threadCyclesAvailable)
				Check(measurement.threadCycles > 0,
					"available thread cycle sample is non-zero", bucketMs);
			Check(measurement.sleepMs + measurement.waitMs + measurement.spinMs
				<= measurement.elapsedMs + EarlyWakeToleranceMs,
				"phase times fit inside elapsed wait", bucketMs);
		}

		FramePacingMeasurement noSpinMeasurement{};
		WaitForRemainingFrameTime(
			0.25,
			FramePacingWaitPolicy::SleepOnly(),
			nullptr,
			&noSpinMeasurement);
		Check(noSpinMeasurement.sleepMs > 0.0, "sleep-only policy records sleep");
		Check(noSpinMeasurement.spinMs == 0.0, "sleep-only policy does not spin");
		Check(noSpinMeasurement.waitBackend == FramePacingWaitBackend::SleepOnly,
			"sleep-only policy records backend");

		FramePacingMeasurement waitableMeasurement{};
		WaitForRemainingFrameTime(8.0, waitableTimer, nullptr, &waitableMeasurement);
		Check(std::isfinite(waitableMeasurement.waitMs)
			&& waitableMeasurement.waitMs >= 0.0,
			"waitable timer records finite wait phase");
		Check(waitableMeasurement.elapsedMs + EarlyWakeToleranceMs >= 8.0,
			"waitable timer reaches requested lower bound");
		Check(waitableMeasurement.sleepMs + waitableMeasurement.waitMs
			+ waitableMeasurement.spinMs
			<= waitableMeasurement.elapsedMs + EarlyWakeToleranceMs,
			"waitable timer phases fit inside elapsed wait");
		const bool timerCompleted = waitableMeasurement.waitableTimerResult
			== FramePacingWaitableTimerResult::Completed;
		Check(timerCompleted
			? waitableMeasurement.waitBackend
				== FramePacingWaitBackend::HighResolutionWaitableTimer
				|| waitableMeasurement.waitBackend
					== FramePacingWaitBackend::CompatibleWaitableTimer
			: waitableMeasurement.waitBackend == FramePacingWaitBackend::Legacy
				&& waitableMeasurement.waitableTimerFallback,
			"waitable timer completes or explicitly falls back");

		FramePacingAdaptiveState adaptiveState{};
		constexpr FramePacingWaitPolicy adaptivePolicy =
			FramePacingWaitPolicy::AdaptiveSpinTail(0.25, 1.5);
		FramePacingMeasurement adaptiveMeasurement{};
		WaitForRemainingFrameTime(
			4.0, adaptivePolicy, &adaptiveState, &adaptiveMeasurement);
		Check(adaptiveState.initialized, "adaptive state is updated after sleep");
		Check(adaptiveState.currentSpinTailMs >= 0.25
			&& adaptiveState.currentSpinTailMs <= 1.5,
			"adaptive tail stays inside clamp");

		FramePacingAdaptiveState invalidAdaptiveState{
			(std::numeric_limits<double>::quiet_NaN)(), true };
		WaitForRemainingFrameTime(
			4.0, adaptivePolicy, &invalidAdaptiveState, nullptr);
		Check(std::isfinite(invalidAdaptiveState.currentSpinTailMs)
			&& invalidAdaptiveState.currentSpinTailMs >= 0.25
			&& invalidAdaptiveState.currentSpinTailMs <= 1.5,
			"adaptive state sanitizes non-finite external value");

		FramePacingMeasurement emptyMeasurement{};
		emptyMeasurement.elapsedMs = 1.0;
		WaitForRemainingFrameTime(0.0, legacy, nullptr, &emptyMeasurement);
		Check(emptyMeasurement.requestedWaitMs == 0.0
			&& emptyMeasurement.elapsedMs == 0.0
			&& !emptyMeasurement.threadCyclesAvailable,
			"non-positive wait returns a cleared measurement");

		auto defaultStart = std::chrono::steady_clock::now();
		WaitForRemainingFrameTime(0.5);
		double defaultElapsedMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - defaultStart).count();
		Check(std::isfinite(defaultElapsedMs)
			&& defaultElapsedMs + EarlyWakeToleranceMs >= 0.5,
			"default null-measurement overload waits");

		auto highPrecisionStart = std::chrono::steady_clock::now();
		HighPrecisionWait(0.5, 500.0);
		double highPrecisionElapsedMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - highPrecisionStart).count();
		Check(std::isfinite(highPrecisionElapsedMs)
			&& highPrecisionElapsedMs + EarlyWakeToleranceMs >= 1.5,
			"HighPrecisionWait uses the default remaining-time path");
	}

	template <typename Value>
	Value Percentile(std::vector<Value> values, double percentile)
	{
		std::sort(values.begin(), values.end());
		std::size_t index = static_cast<std::size_t>(
			std::ceil(percentile * static_cast<double>(values.size()))) - 1;
		return values[std::min(index, values.size() - 1)];
	}

	struct FramePacingBenchmarkStats
	{
		double p50SleepMs = 0.0;
		double p50WaitMs = 0.0;
		double p50SpinMs = 0.0;
		double p50ElapsedMs = 0.0;
		double p95ElapsedMs = 0.0;
		double p99ElapsedMs = 0.0;
		double maxElapsedMs = 0.0;
		double p50OvershootMs = 0.0;
		double p95OvershootMs = 0.0;
		double p99OvershootMs = 0.0;
		double maxOvershootMs = 0.0;
		double late025RatePercent = 0.0;
		double late100RatePercent = 0.0;
		std::uint64_t p50ThreadCycles = 0;
		std::size_t threadCycleSamples = 0;
		std::size_t highResolutionTimerSamples = 0;
		std::size_t compatibleTimerSamples = 0;
		std::size_t legacySamples = 0;
		std::size_t timerFallbackSamples = 0;
		std::size_t highResolutionTimerFailureSamples = 0;
		std::size_t compatibleTimerFailureSamples = 0;
		std::size_t timerUnavailableSamples = 0;
	};

	FramePacingBenchmarkStats Summarize(
		const std::vector<FramePacingMeasurement>& measurements)
	{
		std::vector<double> sleep;
		std::vector<double> wait;
		std::vector<double> spin;
		std::vector<double> elapsed;
		std::vector<double> overshoot;
		std::vector<std::uint64_t> cycles;
		sleep.reserve(measurements.size());
		wait.reserve(measurements.size());
		spin.reserve(measurements.size());
		elapsed.reserve(measurements.size());
		overshoot.reserve(measurements.size());
		cycles.reserve(measurements.size());
		std::size_t late025Count = 0;
		std::size_t late100Count = 0;
		FramePacingBenchmarkStats result{};
		for (const auto& measurement : measurements)
		{
			sleep.push_back(measurement.sleepMs);
			wait.push_back(measurement.waitMs);
			spin.push_back(measurement.spinMs);
			elapsed.push_back(measurement.elapsedMs);
			overshoot.push_back(measurement.signedOvershootMs);
			if (measurement.signedOvershootMs > 0.25) ++late025Count;
			if (measurement.signedOvershootMs > 1.0) ++late100Count;
			if (measurement.threadCyclesAvailable) cycles.push_back(measurement.threadCycles);
			switch (measurement.waitBackend)
			{
			case FramePacingWaitBackend::HighResolutionWaitableTimer:
				++result.highResolutionTimerSamples;
				break;
			case FramePacingWaitBackend::CompatibleWaitableTimer:
				++result.compatibleTimerSamples;
				break;
			case FramePacingWaitBackend::Legacy:
				++result.legacySamples;
				break;
			default:
				break;
			}
			if (measurement.waitableTimerFallback) ++result.timerFallbackSamples;
			if (measurement.waitableTimerResult
				== FramePacingWaitableTimerResult::SetFailed
				|| measurement.waitableTimerResult
					== FramePacingWaitableTimerResult::WaitFailed)
			{
				if (measurement.attemptedWaitableTimerBackend
					== FramePacingWaitBackend::HighResolutionWaitableTimer)
					++result.highResolutionTimerFailureSamples;
				else if (measurement.attemptedWaitableTimerBackend
					== FramePacingWaitBackend::CompatibleWaitableTimer)
					++result.compatibleTimerFailureSamples;
			}
			else if (measurement.waitableTimerResult
				== FramePacingWaitableTimerResult::Unavailable)
				++result.timerUnavailableSamples;
		}

		result.p50SleepMs = Percentile(sleep, 0.50);
		result.p50WaitMs = Percentile(wait, 0.50);
		result.p50SpinMs = Percentile(spin, 0.50);
		result.p50ElapsedMs = Percentile(elapsed, 0.50);
		result.p95ElapsedMs = Percentile(elapsed, 0.95);
		result.p99ElapsedMs = Percentile(elapsed, 0.99);
		result.maxElapsedMs = *std::max_element(elapsed.begin(), elapsed.end());
		result.p50OvershootMs = Percentile(overshoot, 0.50);
		result.p95OvershootMs = Percentile(overshoot, 0.95);
		result.p99OvershootMs = Percentile(overshoot, 0.99);
		result.maxOvershootMs = *std::max_element(overshoot.begin(), overshoot.end());
		result.late025RatePercent = static_cast<double>(late025Count) * 100.0
			/ static_cast<double>(measurements.size());
		result.late100RatePercent = static_cast<double>(late100Count) * 100.0
			/ static_cast<double>(measurements.size());
		result.threadCycleSamples = cycles.size();
		if (!cycles.empty()) result.p50ThreadCycles = Percentile(cycles, 0.50);
		return result;
	}

	struct FramePacingBenchmarkStrategy
	{
		std::string_view name;
		FramePacingWaitPolicy policy;
		bool adaptive = false;
	};

	void PrintBenchmarkResult(
		std::string_view strategy,
		double bucketMs,
		std::size_t sampleCount,
		const FramePacingBenchmarkStats& result)
	{
		std::cout << "FRAME_PACING"
			<< " strategy=" << strategy
			<< " bucket_ms=" << bucketMs
			<< " samples=" << sampleCount
			<< " p50_sleep_ms=" << result.p50SleepMs
			<< " p50_wait_ms=" << result.p50WaitMs
			<< " p50_spin_ms=" << result.p50SpinMs
			<< " p50_elapsed_ms=" << result.p50ElapsedMs
			<< " p95_elapsed_ms=" << result.p95ElapsedMs
			<< " p99_elapsed_ms=" << result.p99ElapsedMs
			<< " max_elapsed_ms=" << result.maxElapsedMs
			<< " p50_overshoot_ms=" << result.p50OvershootMs
			<< " p95_overshoot_ms=" << result.p95OvershootMs
			<< " p99_overshoot_ms=" << result.p99OvershootMs
			<< " max_overshoot_ms=" << result.maxOvershootMs
			<< " late_0_25ms_rate_pct=" << result.late025RatePercent
			<< " late_1_00ms_rate_pct=" << result.late100RatePercent
			<< " p50_thread_cycles=" << result.p50ThreadCycles
			<< " thread_cycle_samples=" << result.threadCycleSamples
			<< " high_resolution_timer_samples=" << result.highResolutionTimerSamples
			<< " compatible_timer_samples=" << result.compatibleTimerSamples
			<< " legacy_samples=" << result.legacySamples
			<< " timer_fallback_samples=" << result.timerFallbackSamples
			<< " high_resolution_timer_failure_samples="
			<< result.highResolutionTimerFailureSamples
			<< " compatible_timer_failure_samples="
			<< result.compatibleTimerFailureSamples
			<< " timer_unavailable_samples=" << result.timerUnavailableSamples
			<< '\n';
	}

	void RunFramePacingBenchmarks()
	{
		constexpr int warmupRounds = 3;
		// 101 个样本让 P95/P99 与 max 分离，并保留可控的整轮耗时。
		constexpr int measuredRounds = 101;
		constexpr std::array strategies{
			FramePacingBenchmarkStrategy{
				"fixed_0.50ms", FramePacingWaitPolicy::FixedSpinTail(0.5), false },
			FramePacingBenchmarkStrategy{
				"fixed_0.75ms", FramePacingWaitPolicy::FixedSpinTail(0.75), false },
			FramePacingBenchmarkStrategy{
				"fixed_1.00ms", FramePacingWaitPolicy::FixedSpinTail(1.0), false },
			FramePacingBenchmarkStrategy{
				"fixed_1.50ms_legacy", FramePacingWaitPolicy::Legacy(), false },
			FramePacingBenchmarkStrategy{
				"adaptive_0.25_1.50ms",
				FramePacingWaitPolicy::AdaptiveSpinTail(0.25, 1.5), true },
			FramePacingBenchmarkStrategy{
				"waitable_timer_0.00ms",
				FramePacingWaitPolicy::WaitableTimer(0.0), false },
			FramePacingBenchmarkStrategy{
				"waitable_timer_0.25ms",
				FramePacingWaitPolicy::WaitableTimer(0.25), false },
			FramePacingBenchmarkStrategy{
				"sleep_only", FramePacingWaitPolicy::SleepOnly(), false },
		};

		auto oldFlags = std::cout.flags();
		auto oldPrecision = std::cout.precision();
		std::cout << std::fixed << std::setprecision(4);
		for (std::size_t bucketIndex = 0;
			bucketIndex < BenchmarkWaitBucketsMs.size(); ++bucketIndex)
		{
			const double bucketMs = BenchmarkWaitBucketsMs[bucketIndex];
			// 每个 bucket 独立状态，并逐轮轮换策略顺序，降低热状态/时间漂移偏差。
			std::array<FramePacingAdaptiveState, strategies.size()> adaptiveStates{};
			std::array<std::vector<FramePacingMeasurement>, strategies.size()>
				measurementsByStrategy;
			for (auto& measurements : measurementsByStrategy)
				measurements.reserve(measuredRounds);

			for (int round = 0; round < warmupRounds; ++round)
			{
				for (std::size_t offset = 0; offset < strategies.size(); ++offset)
				{
					const std::size_t strategyIndex =
						(bucketIndex + static_cast<std::size_t>(round) + offset)
						% strategies.size();
					const auto& strategy = strategies[strategyIndex];
					FramePacingMeasurement measurement{};
					WaitForRemainingFrameTime(
						bucketMs,
						strategy.policy,
						strategy.adaptive ? &adaptiveStates[strategyIndex] : nullptr,
						&measurement);
				}
			}

			for (int round = 0; round < measuredRounds; ++round)
			{
				for (std::size_t offset = 0; offset < strategies.size(); ++offset)
				{
					const std::size_t strategyIndex =
						(bucketIndex + static_cast<std::size_t>(warmupRounds + round)
							+ offset)
						% strategies.size();
					const auto& strategy = strategies[strategyIndex];
					FramePacingMeasurement measurement{};
					WaitForRemainingFrameTime(
						bucketMs,
						strategy.policy,
						strategy.adaptive ? &adaptiveStates[strategyIndex] : nullptr,
						&measurement);
					measurementsByStrategy[strategyIndex].push_back(measurement);
				}
			}

			for (std::size_t strategyIndex = 0;
				strategyIndex < strategies.size(); ++strategyIndex)
			{
				const auto& strategy = strategies[strategyIndex];
				const auto& measurements = measurementsByStrategy[strategyIndex];
				PrintBenchmarkResult(
					strategy.name,
					bucketMs,
					measurements.size(),
					Summarize(measurements));
			}
		}
		std::cout.flags(oldFlags);
		std::cout.precision(oldPrecision);
	}
}

int RunFramePacingTests(bool benchmark)
{
	failureCount = 0;
	TestAnimationFrameClockRebase();
	TestPoliciesAndMeasurements();
	if (benchmark) RunFramePacingBenchmarks();
	return failureCount;
}
