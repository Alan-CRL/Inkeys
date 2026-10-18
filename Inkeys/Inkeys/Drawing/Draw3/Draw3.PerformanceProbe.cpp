#include "Draw3.PerformanceProbe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <vector>

import Inkeys.Drawing.Draw3.renderer;
import Inkeys.Drawing.Draw3.ink_prediction;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr std::size_t kWarmupBlocks = 3;
		constexpr std::size_t kMeasuredBlocks = 11;

		struct Scenario
		{
			const char* name;
			std::size_t pointCount;
			std::size_t repetitions;
			bool changingProtectedDuration;
		};

		constexpr std::array<Scenario, 4> kScenarios = { {
			{ "short_steady", 96, 128, false },
			{ "short_changing", 96, 128, true },
			{ "long_steady", 4096, 2, false },
			{ "long_changing", 4096, 2, true }
		} };

		bool Require(bool condition, const char* caseName, const char* invariant) noexcept
		{
			if (condition) return true;
			std::fprintf(stderr, "Draw3GeometryBenchmark failed: %s: %s\n",
				caseName, invariant);
			return false;
		}

		bool CheckRanges(const char* caseName, std::size_t pointCount,
			const LaserIncrementalStrokeState& prior,
			const LaserIncrementalRanges& ranges) noexcept
		{
			if (pointCount == 0)
				return Require(ranges.stableFirstIndex == 0 && ranges.stablePointCount == 0 &&
					ranges.liveFirstIndex == 0 && ranges.livePointCount == 0 &&
					ranges.nextStableCommittedIndex == 0, caseName, "empty ranges");

			const std::size_t committed = prior.rebuildRequired ? 0 :
				std::min(prior.stableCommittedIndex, pointCount - 1);
			if (!Require(ranges.nextStableCommittedIndex >= committed &&
				ranges.nextStableCommittedIndex < pointCount,
				caseName, "stable cursor bounds and monotonicity")) return false;
			if (!Require(ranges.liveFirstIndex < pointCount,
				caseName, "live range bounds")) return false;
			bool valid = true;
			valid &= Require(ranges.liveFirstIndex == ranges.nextStableCommittedIndex &&
				ranges.livePointCount == pointCount - ranges.liveFirstIndex,
				caseName, "live range begins at L1 connection point");
			if (ranges.stablePointCount > 0)
			{
				valid &= Require(ranges.stableFirstIndex == committed &&
					ranges.stablePointCount ==
						ranges.nextStableCommittedIndex - committed + 1,
					caseName, "stable delta includes prior connection point");
			}
			else
			{
				valid &= Require(ranges.nextStableCommittedIndex == committed,
					caseName, "no stable delta leaves cursor unchanged");
			}
			return valid;
		}

		bool CheckBoundaryCases() noexcept
		{
			constexpr std::array<InkPoint, 4> points = { {
				{ 0.0f, 0.0f, 2.5f, 0.0f },
				{ 1.0f, 0.0f, 2.5f, 1.0f },
				{ 2.0f, 0.0f, 2.5f, 2.0f },
				{ 3.0f, 0.0f, 2.5f, 3.0f }
			} };
			const std::span<const InkPoint> allPoints(points);
			LaserIncrementalStrokeState state;
			bool valid = true;

			const auto empty = PlanLaserIncrementalRanges({}, state, 1.5);
			valid &= CheckRanges("empty", 0, state, empty);
			const auto single = PlanLaserIncrementalRanges(allPoints.first(1), state, 1.5);
			valid &= CheckRanges("single", 1, state, single);
			valid &= Require(single.livePointCount == 1 && single.stablePointCount == 0,
				"single", "single point remains live");

			// L1 提交端与 L0 实时端共用边界点；保护时长变大不能回退已提交游标。
			const auto first = PlanLaserIncrementalRanges(allPoints, state, 1.5);
			valid &= CheckRanges("first boundary", points.size(), state, first);
			valid &= Require(first.stableFirstIndex == 0 && first.stablePointCount == 2 &&
				first.liveFirstIndex == 1 && first.livePointCount == 3,
				"first boundary", "L1 and L0 share point 1");
			state.stableCommittedIndex = first.nextStableCommittedIndex;
			state.rebuildRequired = false;
			const auto next = PlanLaserIncrementalRanges(allPoints, state, 0.5);
			valid &= CheckRanges("next boundary", points.size(), state, next);
			valid &= Require(next.stableFirstIndex == 1 && next.stablePointCount == 2 &&
				next.liveFirstIndex == 2, "next boundary", "delta shares point 1");
			state.stableCommittedIndex = next.nextStableCommittedIndex;
			const auto backward = PlanLaserIncrementalRanges(allPoints, state, 10.0);
			valid &= CheckRanges("backward protection", points.size(), state, backward);
			valid &= Require(backward.stablePointCount == 0 &&
				backward.nextStableCommittedIndex == 2,
				"backward protection", "committed L1 cursor does not retreat");

			const LaserIncrementalStrokeState rebuild{ 3, true };
			const auto rebuilt = PlanLaserIncrementalRanges(allPoints, rebuild, 1.5);
			valid &= CheckRanges("rebuild", points.size(), rebuild, rebuilt);
			valid &= Require(rebuilt.stableFirstIndex == 0 && rebuilt.liveFirstIndex == 1,
				"rebuild", "rebuild restarts from first point");
			const LaserIncrementalStrokeState fresh;
			const auto negative = PlanLaserIncrementalRanges(allPoints, fresh, -1.0);
			valid &= CheckRanges("negative duration", points.size(), fresh, negative);
			valid &= Require(negative.liveFirstIndex == 2 && negative.livePointCount == 2,
				"negative duration", "negative protection clamps to zero");
			return valid;
		}

		std::uint64_t MixChecksum(std::uint64_t checksum, std::size_t value) noexcept
		{
			return checksum ^ (static_cast<std::uint64_t>(value) +
				0x9e3779b97f4a7c15ull + (checksum << 6) + (checksum >> 2));
		}

		bool RunScenario(const Scenario& scenario)
		{
			std::vector<InkPoint> points(scenario.pointCount);
			std::vector<double> durations(scenario.pointCount);
			std::vector<LaserIncrementalStrokeState> states(scenario.repetitions);
			for (std::size_t index = 0; index < points.size(); ++index)
			{
				points[index] = {
					24.0f + static_cast<float>(index) * 0.5f,
					32.0f + static_cast<float>((index * 37) % 101) * 0.25f,
					2.5f + static_cast<float>((index * 11) % 7) * 0.1f,
					static_cast<float>(index) / 120.0f
				};
				constexpr std::array<double, 4> changing = { 0.025, 0.15, 0.045, 0.12 };
				durations[index] = scenario.changingProtectedDuration
					? changing[index % changing.size()] : 0.075;
			}

			LaserIncrementalStrokeState validationState;
			std::uint64_t trajectoryChecksum = 0;
			for (std::size_t pointCount = 1; pointCount <= points.size(); ++pointCount)
			{
				const auto ranges = PlanLaserIncrementalRanges(
					std::span<const InkPoint>(points.data(), pointCount),
					validationState, durations[pointCount - 1]);
				if (!CheckRanges(scenario.name, pointCount, validationState, ranges))
					return false;
				trajectoryChecksum = MixChecksum(trajectoryChecksum, ranges.stableFirstIndex);
				trajectoryChecksum = MixChecksum(trajectoryChecksum, ranges.stablePointCount);
				trajectoryChecksum = MixChecksum(trajectoryChecksum, ranges.liveFirstIndex);
				trajectoryChecksum = MixChecksum(trajectoryChecksum, ranges.livePointCount);
				trajectoryChecksum = MixChecksum(trajectoryChecksum, ranges.nextStableCommittedIndex);
				validationState.stableCommittedIndex = ranges.nextStableCommittedIndex;
				validationState.rebuildRequired = false;
			}

			std::uint64_t checksum = 0;
			std::uint64_t measuredNanoseconds = 0;
			for (std::size_t block = 0; block < kWarmupBlocks + kMeasuredBlocks; ++block)
			{
				std::fill(states.begin(), states.end(), LaserIncrementalStrokeState{});
				LaserIncrementalRanges lastRanges;
				const auto started = std::chrono::steady_clock::now();
				for (auto& state : states)
				{
					for (std::size_t pointCount = 1; pointCount <= points.size(); ++pointCount)
					{
						lastRanges = PlanLaserIncrementalRanges(
							std::span<const InkPoint>(points.data(), pointCount),
							state, durations[pointCount - 1]);
						state.stableCommittedIndex = lastRanges.nextStableCommittedIndex;
						state.rebuildRequired = false;
					}
				}
				const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count();

				// 防止测量调用被消去；校验和与输出均在计时区外。
				for (const auto& state : states)
					checksum = MixChecksum(checksum, state.stableCommittedIndex);
				checksum = MixChecksum(checksum, lastRanges.stablePointCount);
				checksum = MixChecksum(checksum, lastRanges.livePointCount);
				const bool warmup = block < kWarmupBlocks;
				if (!warmup) measuredNanoseconds += static_cast<std::uint64_t>(elapsed);
				std::fprintf(stdout,
					"Draw3GeometryBenchmark scenario=%s phase=%s block=%zu elapsed_ns=%lld calls=%zu\n",
					scenario.name, warmup ? "warmup" : "measured",
					warmup ? block + 1 : block - kWarmupBlocks + 1,
					static_cast<long long>(elapsed),
					scenario.pointCount * scenario.repetitions);
			}
			const std::size_t callsPerBlock = scenario.pointCount * scenario.repetitions;
			std::fprintf(stdout,
				"Draw3GeometryBenchmark scenario=%s summary samples=%zu calls_per_block=%zu total_calls=%zu mean_ns_per_call=%.3f checksum=%llu trajectory_checksum=%llu\n",
				scenario.name, kMeasuredBlocks, callsPerBlock,
				callsPerBlock * kMeasuredBlocks,
				static_cast<double>(measuredNanoseconds) /
					static_cast<double>(callsPerBlock * kMeasuredBlocks),
				static_cast<unsigned long long>(checksum),
				static_cast<unsigned long long>(trajectoryChecksum));
			std::fflush(stdout);
			return true;
		}
	}

	int RunDraw3GeometryBenchmark() noexcept
	{
		try
		{
			if (!CheckBoundaryCases()) return 1;
			std::fprintf(stdout,
				"Draw3GeometryBenchmark metric=PlanLaserIncrementalRanges_plus_cursor_update warmup_blocks=%zu measured_blocks=%zu\n",
				kWarmupBlocks, kMeasuredBlocks);
			for (const auto& scenario : kScenarios)
				if (!RunScenario(scenario)) return 1;
			return 0;
		}
		catch (const std::exception& error)
		{
			std::fprintf(stderr, "Draw3GeometryBenchmark exception: %s\n", error.what());
			return 2;
		}
		catch (...)
		{
			std::fputs("Draw3GeometryBenchmark unknown exception\n", stderr);
			return 2;
		}
	}
}
