#include <iostream>
#include <string_view>

#include "../Inkeys/Inkeys/Helper/Helper.Fatal.h"

namespace
{
	using namespace Inkeys::Fatal;
	using Detail::EvaluateWatchdog;
	using Detail::WatchdogInput;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	constexpr std::uint64_t kBase = 1'000'000;

	WatchdogInput BusySlot(Slot slot, std::uint64_t lastBeat, std::uint64_t now)
	{
		WatchdogInput input;
		input.nowMs = now;
		input.slots[static_cast<std::size_t>(slot)] = { true, lastBeat };
		return input;
	}

	void TestHangThreshold()
	{
		auto input = BusySlot(Slot::Draw3Loop, kBase, kBase + 14'999);
		Check(!EvaluateWatchdog(input).trigger, "busy slot 14.999 s after beat does not trigger");
		input.nowMs = kBase + 15'000;
		const auto decision = EvaluateWatchdog(input);
		Check(decision.trigger && decision.reason == Reason::ThreadHang
			&& decision.detail == static_cast<std::uint32_t>(Slot::Draw3Loop),
			"busy slot 15 s after beat triggers ThreadHang with slot detail");

		WatchdogInput idle;
		idle.nowMs = kBase + 3'600'000;
		idle.slots[0] = { false, kBase };
		Check(!EvaluateWatchdog(idle).trigger, "idle slot never triggers however long");

		// 持续 Beat：每个工作单元都刷新时间戳，判定只看当前工作单元。
		auto beating = BusySlot(Slot::UiScheduler, kBase + 59'990, kBase + 60'000);
		Check(!EvaluateWatchdog(beating).trigger, "continuously beating busy slot does not trigger");

		// 时钟回退（不应发生）不触发。
		auto backwards = BusySlot(Slot::UiScheduler, kBase + 20'000, kBase);
		Check(!EvaluateWatchdog(backwards).trigger, "beat in the future does not trigger");
	}

	void TestFailureThreshold()
	{
		WatchdogInput input;
		input.firstFailureMs[static_cast<std::size_t>(FailureSlot::UiBar)] = kBase;
		input.lastFailureMs[static_cast<std::size_t>(FailureSlot::UiBar)] = kBase + 10'000;
		input.nowMs = kBase + 9'999;
		Check(!EvaluateWatchdog(input).trigger, "failure 9.999 s does not trigger");
		input.nowMs = kBase + 10'000;
		auto decision = EvaluateWatchdog(input);
		Check(decision.trigger && decision.reason == Reason::UiClientFailures
			&& decision.detail == static_cast<std::uint32_t>(FailureSlot::UiBar),
			"bar failure for 10 s triggers UiClientFailures");
		// 客户端已停止尝试（隐藏、空闲）：最近一次失败超过 5 秒即不触发。
		input.nowMs = kBase + 15'000;
		Check(EvaluateWatchdog(input).trigger, "failure last seen exactly 5 s ago still triggers");
		input.nowMs = kBase + 15'001;
		Check(!EvaluateWatchdog(input).trigger, "stale failure older than 5 s does not trigger");

		WatchdogInput device;
		device.nowMs = kBase + 10'000;
		device.firstFailureMs[static_cast<std::size_t>(FailureSlot::UiDeviceRecovery)] = kBase;
		device.lastFailureMs[static_cast<std::size_t>(FailureSlot::UiDeviceRecovery)] = kBase + 10'000;
		Check(EvaluateWatchdog(device).reason == Reason::UiDeviceRecovery, "device recovery maps to its own reason");
		WatchdogInput present;
		present.nowMs = kBase + 10'000;
		present.firstFailureMs[static_cast<std::size_t>(FailureSlot::Draw3Present)] = kBase;
		present.lastFailureMs[static_cast<std::size_t>(FailureSlot::Draw3Present)] = kBase + 10'000;
		Check(EvaluateWatchdog(present).reason == Reason::Draw3PresentFailures, "draw3 present maps to its own reason");
		WatchdogInput freeze;
		freeze.nowMs = kBase + 10'000;
		freeze.firstFailureMs[static_cast<std::size_t>(FailureSlot::Freeze)] = kBase;
		freeze.lastFailureMs[static_cast<std::size_t>(FailureSlot::Freeze)] = kBase + 10'000;
		Check(EvaluateWatchdog(freeze).reason == Reason::FreezeFailures, "freeze maps to its own reason");
	}

	void TestSuppression()
	{
		auto input = BusySlot(Slot::UiScheduler, kBase, kBase + 60'000);
		input.pendingReason = Reason::DebugInjected;
		input.exitIntentClaimed = true;
		Check(!EvaluateWatchdog(input).trigger, "claimed exit intent suppresses every trigger");
		input.exitIntentClaimed = false;
		input.crashHandling = true;
		Check(!EvaluateWatchdog(input).trigger, "active crash handling suppresses every trigger");
		input.crashHandling = false;
		input.debuggerPresent = true;
		Check(!EvaluateWatchdog(input).trigger, "attached debugger suppresses every trigger");
		input.debuggerPresent = false;
		const auto decision = EvaluateWatchdog(input);
		Check(decision.trigger && decision.reason == Reason::DebugInjected, "pending report wins over hang");
	}

	void TestAtomicState()
	{
		Detail::ResetForTest();
		Beat(Slot::UiScheduler);
		auto sample = Detail::Sample(Detail::NowMs());
		Check(sample.slots[0].busy && sample.slots[0].lastBeatMs != 0, "Beat marks slot busy with timestamp");
		Idle(Slot::UiScheduler);
		sample = Detail::Sample(Detail::NowMs());
		Check(!sample.slots[0].busy, "Idle marks slot idle");

		RecordFailure(FailureSlot::UiSetting);
		const auto first = Detail::Sample(Detail::NowMs()).firstFailureMs[static_cast<std::size_t>(FailureSlot::UiSetting)];
		Check(first != 0, "RecordFailure starts timing");
		RecordFailure(FailureSlot::UiSetting);
		Check(Detail::Sample(Detail::NowMs()).firstFailureMs[static_cast<std::size_t>(FailureSlot::UiSetting)] == first,
			"repeated failure keeps first failure time");
		RecordSuccess(FailureSlot::UiSetting);
		Check(Detail::Sample(Detail::NowMs()).firstFailureMs[static_cast<std::size_t>(FailureSlot::UiSetting)] == 0,
			"RecordSuccess clears timing");
		RecordFailure(FailureSlot::Freeze);
		ClearFailure(FailureSlot::Freeze);
		Check(Detail::Sample(Detail::NowMs()).firstFailureMs[static_cast<std::size_t>(FailureSlot::Freeze)] == 0,
			"ClearFailure clears timing");

		Report(Reason::DebugInjected, 7);
		Report(Reason::CrtAbort, 9);
		sample = Detail::Sample(Detail::NowMs());
		Check(sample.pendingReason == Reason::DebugInjected && sample.pendingDetail == 7, "first report is retained");
		Detail::ResetForTest();
		Check(Detail::Sample(Detail::NowMs()).pendingReason == Reason::None, "reset clears pending report");
	}
}

int RunFatalWatchdogTests()
{
	TestHangThreshold();
	TestFailureThreshold();
	TestSuppression();
	TestAtomicState();
	return failureCount;
}
