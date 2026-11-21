#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.FrameLatencyGate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>

namespace
{
	using namespace Inkeys::Drawing::Draw3;

	constexpr double kTarget120 = 1000.0 / 120.0;
	constexpr double kRefresh60 = 1000.0 / 60.0;

	// 模拟帧延迟句柄（上限 1 的计数信号）：每次 Present 在 GPU 处理完该帧后回补一个名额。
	struct LatencySemaphore
	{
		double nowMs = 0.0;
		double gpuFrameMs = 1.0;
		double gpuFreeAtMs = 0.0;
		int available = 1;
		bool signalLost = false;
		bool waitFails = false;
		int failFromWait = 0; // >0：从第 N 次等待起句柄失效，用于模拟补等时失效
		std::uint32_t lastTimeoutMs = 0;
		int waits = 0;
		std::deque<double> releases;

		void Present()
		{
			if (signalLost) return;
			gpuFreeAtMs = (std::max)(gpuFreeAtMs, nowMs) + gpuFrameMs;
			releases.push_back(gpuFreeAtMs);
		}

		FrameLatencySignal Wait(std::uint32_t timeoutMs)
		{
			++waits;
			lastTimeoutMs = timeoutMs;
			if (waitFails || (failFromWait > 0 && waits >= failFromWait)) return FrameLatencySignal::Failed;
			while (!releases.empty() && releases.front() <= nowMs) { releases.pop_front(); ++available; }
			if (available > 0) { --available; return FrameLatencySignal::Signaled; }
			if (!releases.empty() && releases.front() <= nowMs + timeoutMs)
			{
				nowMs = releases.front();
				releases.pop_front();
				return FrameLatencySignal::Signaled;
			}
			nowMs += timeoutMs;
			return FrameLatencySignal::TimedOut;
		}

		template<class Policy = FrameLatencyTimeoutPolicy>
		FrameLatencyWaitResult Acquire(FrameLatencyGate& gate,
			Policy policy = FrameLatencyTimeoutPolicy::Count,
			double targetFrameMs = kTarget120, double refreshMs = kRefresh60)
		{
			return gate.Acquire(targetFrameMs, refreshMs, policy,
				[this] { return nowMs; },
				[this](std::uint32_t timeoutMs) { return Wait(timeoutMs); });
		}

		// 已提交但 GPU 尚未完成的帧数（在途帧）。
		int InFlight() const
		{
			int count = 0;
			for (const double release : releases) if (release > nowMs) ++count;
			return count;
		}
	};

	bool Near(double actual, double expected, double tolerance = 1e-6)
	{
		return std::abs(actual - expected) <= tolerance;
	}
}

int RunDraw3FrameLatencyGateTests()
{
	int failures = 0;
	const auto expect = [&](bool condition, const char* name)
	{
		if (condition) return;
		++failures;
		std::cerr << "[FrameLatencyGate] failed: " << name << '\n';
	};

	// 未启用：不等待，也不要求补等。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		const auto result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::Disabled && semaphore.waits == 0, "disabled gate never waits");
		expect(!gate.NeedsWaitBeforePresent() && !gate.ShouldTrip(), "disabled gate needs no catch-up wait");
	}

	// 首次获取、已持有不重复等待、空帧保留令牌、Present1 失败不消耗。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		expect(gate.Enable() && gate.Enabled() && !gate.TokenHeld(), "enable starts without token");
		expect(gate.NeedsWaitBeforePresent(), "present before any wait needs catch-up");
		auto result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::Signaled && gate.TokenHeld() && semaphore.waits == 1, "first acquire takes token");
		expect(!gate.NeedsWaitBeforePresent(), "held token needs no catch-up");
		// 空帧（本帧不 Present）：下一帧不再等待，否则名额被耗尽。
		result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::AlreadyHeld && semaphore.waits == 1 && result.waitedMs == 0.0, "held token is not waited again");
		gate.OnPresentCalled(false);
		expect(gate.TokenHeld(), "failed Present1 keeps token");
		result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::AlreadyHeld && semaphore.waits == 1, "token survives failed present");
		semaphore.Present();
		gate.OnPresentCalled(true);
		expect(!gate.TokenHeld() && gate.NeedsWaitBeforePresent(), "successful Present1 consumes token");
		result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::Signaled && semaphore.waits == 2, "next frame waits again");
	}

	// 超时取值公式：上下限、刷新间隔下界、信号间隔放宽。
	{
		expect(Near(FrameLatencyGate::ComputeTimeoutMs(kTarget120, kRefresh60, 0.0), 25.0), "timeout uses refresh interval floor");
		expect(Near(FrameLatencyGate::ComputeTimeoutMs(1000.0 / 30.0, kRefresh60, 0.0), 50.0), "timeout uses target frame interval");
		expect(Near(FrameLatencyGate::ComputeTimeoutMs(kTarget120, kRefresh60, 80.0), 120.0), "timeout follows signal interval");
		expect(Near(FrameLatencyGate::ComputeTimeoutMs(1000.0 / 240.0, 1.0, 0.0), 6.25), "timeout scales small interval");
		expect(Near(FrameLatencyGate::ComputeTimeoutMs(1.0, 1.0, 0.0), kFrameLatencyMinimumTimeoutMs), "timeout lower bound");
		expect(Near(FrameLatencyGate::ComputeTimeoutMs(kTarget120, kRefresh60, 500.0), kFrameLatencyMaximumTimeoutMs), "timeout upper bound");
		expect(Near(FrameLatencyGate::ComputeTimeoutMs(std::nan(""), -1.0, 0.0), kFrameLatencyMinimumTimeoutMs), "invalid inputs fall to lower bound");
	}

	// 超时后仍可继续：不持有令牌，本帧 Present 前不再补等，下一帧重新等待。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		auto result = semaphore.Acquire(gate, FrameLatencyTimeoutPolicy::Count, kTarget120, 16.0);
		expect(result.outcome == FrameLatencyWaitOutcome::TimedOut && !gate.TokenHeld(), "timeout leaves no token");
		expect(semaphore.lastTimeoutMs == 24 && Near(result.waitedMs, 24.0) && Near(result.timeoutMs, 24.0), "timeout waits one bounded interval");
		expect(!gate.NeedsWaitBeforePresent(), "timed-out frame presents without a second wait");
		semaphore.Present();
		gate.OnPresentCalled(true);
		expect(gate.NeedsWaitBeforePresent(), "catch-up is required again after present");
		result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::Signaled && gate.ConsecutiveTimeouts() == 0, "acquire clears timeout streak");
		expect(gate.TotalTimeouts() == 1 && gate.TotalAcquired() == 1, "totals count waits");
	}

	// GPU 慢：每帧 80 ms 才回补信号，超时自适应放宽后等待成功，全程不熔断。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		semaphore.gpuFrameMs = 80.0;
		gate.Enable();
		bool tripped = false; int lateTimeouts = 0;
		for (int frame = 0; frame < 200; ++frame)
		{
			const auto result = semaphore.Acquire(gate);
			if (frame >= 20 && result.outcome != FrameLatencyWaitOutcome::Signaled) ++lateTimeouts;
			tripped |= gate.ShouldTrip();
			semaphore.nowMs += 2.0; // 本帧工作耗时；超时帧仍然 Present。
			semaphore.Present();
			gate.OnPresentCalled(true);
		}
		expect(!tripped, "slow GPU never trips");
		expect(gate.SignalIntervalEmaMs() > 70.0 && gate.SignalIntervalEmaMs() < 90.0, "signal interval estimate follows slow GPU");
		expect(Near(gate.TimeoutMs(kTarget120, kRefresh60), gate.SignalIntervalEmaMs() * 1.5), "timeout relaxed by signal interval");
		expect(lateTimeouts == 0, "slow GPU waits succeed after adapting");
	}

	// 空闲间隔不进入信号间隔估计。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		(void)semaphore.Acquire(gate);
		semaphore.Present(); gate.OnPresentCalled(true);
		semaphore.nowMs += 5000.0;
		(void)semaphore.Acquire(gate);
		expect(gate.SignalIntervalEmaMs() == 0.0 && Near(gate.TimeoutMs(kTarget120, kRefresh60), 25.0), "idle gap does not inflate timeout");
	}

	// 信号丢失：连续超时 ≥ 8 次且持续 ≥ 1000 ms 才熔断；熔断后 Reset/Enable 不恢复。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		(void)semaphore.Acquire(gate);
		semaphore.Present(); gate.OnPresentCalled(true);
		semaphore.signalLost = true;
		semaphore.releases.clear();
		bool trippedEarly = false; int timeouts = 0; double firstTimeoutStartMs = semaphore.nowMs;
		while (!gate.ShouldTrip() && timeouts < 1000)
		{
			(void)semaphore.Acquire(gate);
			++timeouts;
			if (timeouts <= 8) trippedEarly |= gate.ShouldTrip();
			semaphore.Present(); gate.OnPresentCalled(true);
			semaphore.nowMs += 2.0;
		}
		expect(!trippedEarly, "eight short timeouts alone do not trip");
		expect(gate.ShouldTrip() && gate.ConsecutiveTimeouts() >= kFrameLatencyTripTimeouts &&
			semaphore.nowMs - firstTimeoutStartMs >= kFrameLatencyTripMinimumMs, "lost signal trips after count and duration");
		gate.Trip();
		expect(gate.Tripped() && !gate.Enabled() && gate.TripReason() == FrameLatencyTripReason::SignalLost, "trip disables gate");
		gate.Reset();
		expect(gate.Tripped() && !gate.Enable() && !gate.Enabled(), "reset keeps trip for the session");
		const int waitsBefore = semaphore.waits;
		expect(semaphore.Acquire(gate).outcome == FrameLatencyWaitOutcome::Disabled && semaphore.waits == waitsBefore, "tripped gate never waits");
	}

	// 持续足够久但次数不足：不熔断。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		for (int index = 0; index < 4; ++index)
		{
			(void)semaphore.Acquire(gate);
			semaphore.nowMs += 600.0;
		}
		expect(gate.ConsecutiveTimeouts() == 4 && !gate.ShouldTrip(), "long duration with few timeouts does not trip");
	}

	// 不可计数状态（隐藏/交接等）：超时不熔断，并打断此前的连续计数。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		for (int index = 0; index < 100; ++index)
		{
			(void)semaphore.Acquire(gate, FrameLatencyTimeoutPolicy::Exclude);
			semaphore.nowMs += 20.0;
		}
		expect(gate.ConsecutiveTimeouts() == 0 && !gate.ShouldTrip(), "excluded timeouts never trip");
		for (int index = 0; index < 7; ++index) { (void)semaphore.Acquire(gate); semaphore.nowMs += 200.0; }
		(void)semaphore.Acquire(gate, FrameLatencyTimeoutPolicy::Exclude);
		expect(gate.ConsecutiveTimeouts() == 0, "excluded timeout breaks the streak");
		for (int index = 0; index < 7; ++index) { (void)semaphore.Acquire(gate); semaphore.nowMs += 200.0; }
		expect(gate.ConsecutiveTimeouts() == 7 && !gate.ShouldTrip(), "streak restarts after excluded state");
		// 帧循环外的补等不计数也不打断。
		(void)semaphore.Acquire(gate, FrameLatencyTimeoutPolicy::Neutral);
		expect(gate.ConsecutiveTimeouts() == 7 && !gate.ShouldTrip(), "neutral timeout leaves the streak");
		(void)semaphore.Acquire(gate);
		expect(gate.ConsecutiveTimeouts() == 8 && gate.ShouldTrip(), "counted timeouts trip after the streak resumes");
	}

	// 超时后才解析策略（R5）：GPU 仍忙的超时记为 Neutral，不熔断也不打断空闲超时的连续计数；
	// 解析器只在主等待超时时调用，成功获取不调用。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		int resolverCalls = 0;
		bool gpuBusy = false;
		const auto resolver = [&]
		{
			++resolverCalls;
			return gpuBusy ? FrameLatencyTimeoutPolicy::Neutral : FrameLatencyTimeoutPolicy::Count;
		};
		auto result = semaphore.Acquire(gate, resolver);
		expect(result.outcome == FrameLatencyWaitOutcome::Signaled && resolverCalls == 0, "resolver is not called on signal");
		semaphore.Present(); gate.OnPresentCalled(true);
		semaphore.available = 0; semaphore.releases.clear();
		gpuBusy = true;
		for (int index = 0; index < 30; ++index)
		{
			result = semaphore.Acquire(gate, resolver);
			expect(result.outcome == FrameLatencyWaitOutcome::TimedOut && result.timeoutPolicy == FrameLatencyTimeoutPolicy::Neutral,
				"busy timeout resolves to neutral");
			semaphore.nowMs += 200.0;
		}
		expect(resolverCalls == 30 && gate.ConsecutiveTimeouts() == 0 && !gate.ShouldTrip(), "gpu-busy timeouts never trip");
		// 忙与空闲交替：只累计空闲超时，忙超时不打断。
		for (int index = 0; index < 14; ++index)
		{
			gpuBusy = (index % 2) == 1;
			(void)semaphore.Acquire(gate, resolver);
			semaphore.nowMs += 200.0;
		}
		expect(gate.ConsecutiveTimeouts() == 7 && !gate.ShouldTrip(), "alternating busy timeouts count only idle ones");
		gpuBusy = false;
		result = semaphore.Acquire(gate, resolver);
		expect(result.timeoutPolicy == FrameLatencyTimeoutPolicy::Count && gate.ConsecutiveTimeouts() == 8 && gate.ShouldTrip(),
			"idle timeouts still trip");
	}

	// 解析器在主等待返回之后调用（读取超时那一刻的状态）；还账补等无论成败都不调用解析器。
	{
		FrameLatencyGate gate;
		gate.Enable();
		double clockMs = 0.0;
		bool waitReturned = false;
		bool resolverSawReturnedWait = false;
		const auto timedOut = gate.Acquire(kTarget120, kRefresh60,
			[&] { resolverSawReturnedWait = waitReturned; return FrameLatencyTimeoutPolicy::Neutral; },
			[&] { return clockMs; },
			[&](std::uint32_t timeoutMs) { clockMs += timeoutMs; waitReturned = true; return FrameLatencySignal::TimedOut; });
		expect(timedOut.outcome == FrameLatencyWaitOutcome::TimedOut && resolverSawReturnedWait, "resolver runs after the wait returns");
		gate.OnPresentCalled(true); // 无令牌 Present：记一笔欠账
		int resolverCalls = 0;
		int waits = 0;
		const auto repaid = gate.Acquire(kTarget120, kRefresh60,
			[&] { ++resolverCalls; return FrameLatencyTimeoutPolicy::Count; },
			[&] { return clockMs; },
			[&](std::uint32_t timeoutMs)
			{
				++waits;
				if (waits == 1) return FrameLatencySignal::Signaled;
				clockMs += timeoutMs;
				return FrameLatencySignal::TimedOut; // 补等超时
			});
		expect(repaid.outcome == FrameLatencyWaitOutcome::Signaled && repaid.debtRepayTimedOut && waits == 2 && resolverCalls == 0,
			"debt repay path never calls resolver");
	}

	// GPU 忙超时：欠账按 kFrameLatencyMaxBusyDebt 如实记账（饥饿期间多压的帧都在队列里），GPU 恢复后逐帧还清；
	// 排除状态（Exclude）仍按 kFrameLatencyMaxDebt 封顶。不计入熔断。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		(void)semaphore.Acquire(gate);
		semaphore.Present(); gate.OnPresentCalled(true);
		const double starvedGpuFreeAt = semaphore.nowMs + 3000.0; // 3 秒饥饿：期间提交的帧都在 GPU 恢复后完成
		semaphore.available = 0;
		semaphore.releases.clear();
		semaphore.releases.push_back(starvedGpuFreeAt); // 饥饿前提交的那一帧同样在 GPU 恢复时才完成
		semaphore.gpuFreeAtMs = starvedGpuFreeAt;
		int busyFrames = 0;
		while (semaphore.nowMs < starvedGpuFreeAt - 250.0)
		{
			const auto result = semaphore.Acquire(gate, [] { return FrameLatencyTimeoutPolicy::GpuBusy; });
			expect(result.outcome == FrameLatencyWaitOutcome::TimedOut, "starved frame times out");
			semaphore.Present(); gate.OnPresentCalled(true);
			++busyFrames;
		}
		expect(busyFrames > static_cast<int>(kFrameLatencyMaxDebt) && !gate.ShouldTrip(), "gpu-busy starvation never trips");
		expect(gate.Debt() == (std::min)(static_cast<std::uint32_t>(busyFrames), kFrameLatencyMaxBusyDebt) &&
			gate.TotalDebtOverflows() == static_cast<std::uint64_t>((std::max)(0, busyFrames - static_cast<int>(kFrameLatencyMaxBusyDebt))),
			"gpu-busy presents are tracked up to the busy debt cap");
		// GPU 恢复：每帧拿到名额后多等一次信号，欠账逐帧还清，在途帧回到上限。
		semaphore.nowMs = starvedGpuFreeAt + 1.0;
		semaphore.gpuFrameMs = 1.0;
		for (int frame = 0; frame < 40 && gate.Debt() > 0; ++frame)
		{
			(void)semaphore.Acquire(gate, [] { return FrameLatencyTimeoutPolicy::Count; });
			semaphore.Present(); gate.OnPresentCalled(true);
			semaphore.nowMs += 2.0;
		}
		expect(gate.Debt() == 0, "busy debt is repaid after the gpu recovers");
		expect(semaphore.InFlight() <= 1, "queue returns to one frame in flight after starvation");
	}
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		semaphore.signalLost = true;
		for (int index = 0; index < 10; ++index)
		{
			(void)semaphore.Acquire(gate, FrameLatencyTimeoutPolicy::Exclude);
			semaphore.Present(); gate.OnPresentCalled(true);
		}
		expect(gate.Debt() == kFrameLatencyMaxDebt, "excluded timeouts keep the small debt cap");
		expect(!gate.ShouldTrip(), "excluded debt overflow only counts and never trips");
	}
	// 忙超时累计欠账后改判 Count（GPU 刚空闲）：该帧仍按大上限记账，不按小上限溢出，也不熔断。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		semaphore.signalLost = true;
		for (int index = 0; index < 3; ++index)
		{
			(void)semaphore.Acquire(gate, [] { return FrameLatencyTimeoutPolicy::GpuBusy; });
			semaphore.Present(); gate.OnPresentCalled(true);
		}
		(void)semaphore.Acquire(gate, [] { return FrameLatencyTimeoutPolicy::Count; });
		semaphore.Present(); gate.OnPresentCalled(true);
		expect(gate.Debt() == 4 && gate.TotalDebtOverflows() == 0 && !gate.ShouldTrip(), "count timeout after busy debt keeps tracking");
		(void)semaphore.Acquire(gate, FrameLatencyTimeoutPolicy::Exclude);
		semaphore.Present(); gate.OnPresentCalled(true);
		expect(gate.Debt() == 4 && gate.TotalDebtOverflows() == 1 && !gate.ShouldTrip(), "excluded present above the small cap only counts overflow");
	}
	// GPU 忙期间欠账超过忙上限：溢出的帧无法补还，要求熔断重建。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		semaphore.signalLost = true;
		for (std::uint32_t index = 0; index < kFrameLatencyMaxBusyDebt; ++index)
		{
			(void)semaphore.Acquire(gate, [] { return FrameLatencyTimeoutPolicy::GpuBusy; });
			semaphore.Present(); gate.OnPresentCalled(true);
		}
		expect(gate.Debt() == kFrameLatencyMaxBusyDebt && !gate.ShouldTrip(), "busy debt up to the cap does not trip");
		(void)semaphore.Acquire(gate, [] { return FrameLatencyTimeoutPolicy::GpuBusy; });
		semaphore.Present(); gate.OnPresentCalled(true);
		expect(gate.ShouldTrip(), "busy debt overflow requests a rebuild");
		gate.Trip();
		expect(gate.TripReason() == FrameLatencyTripReason::QueueOverflow, "busy overflow trip reason");
	}

	// 解析器也可返回 Exclude；句柄失效时不调用解析器且照常熔断。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		for (int index = 0; index < 7; ++index) { (void)semaphore.Acquire(gate); semaphore.nowMs += 200.0; }
		(void)semaphore.Acquire(gate, [] { return FrameLatencyTimeoutPolicy::Exclude; });
		expect(gate.ConsecutiveTimeouts() == 0, "resolver exclude breaks the streak");
		int resolverCalls = 0;
		semaphore.waitFails = true;
		const auto result = semaphore.Acquire(gate, [&] { ++resolverCalls; return FrameLatencyTimeoutPolicy::Neutral; });
		expect(result.outcome == FrameLatencyWaitOutcome::Failed && resolverCalls == 0 && gate.ShouldTrip(), "failed wait trips without resolver");
	}

	// 句柄等待失败：立即要求熔断。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.waitFails = true;
		const auto result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::Failed && !gate.TokenHeld() && gate.ShouldTrip(), "failed wait requests trip");
		gate.Trip();
		expect(gate.TripReason() == FrameLatencyTripReason::WaitFailed, "failed wait trip reason");
	}

	// 主等待成功、还账补等时句柄失效：与主等待失败同样熔断，不当作超时保留欠账。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		(void)semaphore.Acquire(gate);
		semaphore.Present(); gate.OnPresentCalled(true);
		semaphore.available = 0; semaphore.releases.clear();
		(void)semaphore.Acquire(gate);
		semaphore.Present(); gate.OnPresentCalled(true);
		expect(gate.Debt() == 1, "repay failure setup leaves one debt");
		semaphore.available = 1;
		semaphore.failFromWait = semaphore.waits + 2;
		const auto result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::Signaled && gate.TokenHeld() && !result.debtRepayTimedOut,
			"main wait keeps its token when the repay wait fails");
		expect(gate.ShouldTrip(), "failed repay wait requests trip");
		gate.Trip();
		expect(gate.TripReason() == FrameLatencyTripReason::WaitFailed, "failed repay wait trip reason");
	}

	// 交换链重建：Reset 清令牌与连续计数，重新 Enable 后从新句柄开始。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		(void)semaphore.Acquire(gate);
		gate.Reset();
		expect(!gate.Enabled() && !gate.TokenHeld() && !gate.Tripped(), "reset clears token without tripping");
		expect(gate.Enable() && gate.NeedsWaitBeforePresent(), "re-enabled gate waits on the new handle");
	}

	// 欠账记账：只有没拿到名额且 Present1 成功才记一笔。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		(void)semaphore.Acquire(gate);
		semaphore.Present(); gate.OnPresentCalled(true);
		expect(gate.Debt() == 0, "present with token adds no debt");
		semaphore.available = 0; semaphore.releases.clear();
		const auto result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::TimedOut, "debt setup times out");
		gate.OnPresentCalled(false);
		expect(gate.Debt() == 0, "failed present without token adds no debt");
		gate.OnPresentCalled(true);
		expect(gate.Debt() == 1, "successful present without token adds debt");
	}

	// GPU 抖动导致超时：之后逐帧补等还清欠账，在途帧回到 1，不再永久多压一帧。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		semaphore.gpuFrameMs = 30.0; // 慢于初始超时 25 ms，开头必然超时
		gate.Enable();
		int maxLateInFlight = 0; bool repaid = false;
		for (int frame = 0; frame < 100; ++frame)
		{
			const auto result = semaphore.Acquire(gate);
			repaid |= result.debtRepaid;
			semaphore.nowMs += 2.0;
			semaphore.Present();
			gate.OnPresentCalled(true);
			if (frame >= 20) maxLateInFlight = (std::max)(maxLateInFlight, semaphore.InFlight());
		}
		expect(gate.TotalTimeouts() > 0 && repaid, "jitter timeout creates debt that is repaid");
		expect(gate.Debt() == 0, "debt is fully repaid");
		expect(gate.TotalDebtOverflows() == 0, "jitter debt stays within the cap");
		expect(maxLateInFlight <= 1, "repaid debt keeps one frame in flight");
	}

	// 空闲后：残留欠账对应的信号已积在句柄里，下一笔第一帧补等立即返回，不额外等待也不突发排队。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		semaphore.gpuFrameMs = 30.0;
		gate.Enable();
		(void)semaphore.Acquire(gate);
		semaphore.Present(); gate.OnPresentCalled(true);
		expect(semaphore.Acquire(gate).outcome == FrameLatencyWaitOutcome::TimedOut, "idle setup times out");
		semaphore.Present(); gate.OnPresentCalled(true);
		expect(gate.Debt() == 1, "idle setup leaves one debt");
		semaphore.nowMs += 5000.0;
		const auto result = semaphore.Acquire(gate);
		expect(result.outcome == FrameLatencyWaitOutcome::Signaled && result.debtRepaid && result.waitedMs == 0.0,
			"debt after idle is repaid without waiting");
		expect(gate.Debt() == 0, "stroke after idle starts without debt");
	}

	// 开画布时 GPU 已很慢（每帧 60 ms，初始超时只有 25 ms）：超时后放宽，几次内适应，欠账不溢出、最终还清。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		semaphore.gpuFrameMs = 60.0;
		gate.Enable();
		int maxLateInFlight = 0;
		for (int frame = 0; frame < 60; ++frame)
		{
			(void)semaphore.Acquire(gate);
			semaphore.nowMs += 2.0;
			semaphore.Present();
			gate.OnPresentCalled(true);
			if (frame >= 20) maxLateInFlight = (std::max)(maxLateInFlight, semaphore.InFlight());
		}
		expect(gate.TotalTimeouts() > 0 && gate.TotalTimeouts() <= 3, "slow start adapts after a few timeouts");
		expect(gate.TotalDebtOverflows() == 0 && gate.Debt() == 0, "slow start debt stays within cap and is repaid");
		expect(maxLateInFlight <= 1, "slow start returns to one frame in flight");
	}

	// 信号丢失：欠账封顶，补等不会发生（没等到本帧名额就不还账）；Reset 清零。
	{
		FrameLatencyGate gate; LatencySemaphore semaphore;
		gate.Enable();
		semaphore.available = 0;
		semaphore.signalLost = true;
		bool repayAttempted = false;
		for (int index = 0; index < 10; ++index)
		{
			const auto result = semaphore.Acquire(gate, FrameLatencyTimeoutPolicy::Exclude);
			repayAttempted |= result.debtRepaid || result.debtRepayTimedOut;
			semaphore.Present(); gate.OnPresentCalled(true);
			semaphore.nowMs += 2.0;
		}
		expect(gate.Debt() == kFrameLatencyMaxDebt && !repayAttempted, "lost signal caps debt without repay waits");
		expect(gate.TotalDebtOverflows() == 10 - kFrameLatencyMaxDebt, "presents beyond the debt cap are counted as overflow");
		gate.Reset();
		expect(gate.Debt() == 0 && gate.TotalDebtOverflows() == 10 - kFrameLatencyMaxDebt, "reset clears debt but keeps overflow total");
	}

	std::cout << "[FrameLatencyGate] failures=" << failures << '\n';
	return failures;
}
