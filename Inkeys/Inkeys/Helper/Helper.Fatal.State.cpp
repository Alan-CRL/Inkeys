#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "Helper.Fatal.h"

#include <atomic>

// 热路径状态：只用原子量，不分配、不加锁、不写日志，渲染线程和内存不足时都可调用。
// 产品与无窗口测试共用本文件；看门狗线程与触发逻辑在 Helper.Fatal.cpp（仅产品）。
namespace Inkeys::Fatal
{
	namespace
	{
		using Detail::kFailureSlotCount;
		using Detail::kSlotCount;

		// 槽状态编码为一个 64 位值：(lastBeatMs << 1) | busy。Beat/Idle 只由所属线程调用，单次 store 即可。
		std::atomic<std::uint64_t> g_slots[kSlotCount]{};
		std::atomic<std::uint64_t> g_firstFailure[kFailureSlotCount]{};
		std::atomic<std::uint64_t> g_lastFailure[kFailureSlotCount]{};
		// 待处理报告：低 32 位原因、高 32 位 detail，单原子发布整对，只保留第一次。
		std::atomic<std::uint64_t> g_pendingReport{ 0 };
		std::atomic<bool> g_draw3DebugFault{ false };

		constexpr std::size_t Index(Slot slot) noexcept { return static_cast<std::size_t>(slot); }
		constexpr std::size_t Index(FailureSlot slot) noexcept { return static_cast<std::size_t>(slot); }
	}

	// 看门狗唤醒事件；由 Helper.Fatal.cpp 创建，测试工程中保持为空。
	std::atomic<HANDLE> g_watchdogWake{ nullptr };

	namespace Detail
	{
		std::uint64_t NowMs() noexcept
		{
			ULONGLONG unbiased = 0;
			// 100ns 单位、不含睡眠/休眠；指针有效时不会失败，始终使用同一计时基准。
			// 时刻 0 保留给“无失败”，因此至少返回 1。
			(void)QueryUnbiasedInterruptTime(&unbiased);
			const std::uint64_t ms = unbiased / 10000ULL;
			return ms ? ms : 1;
		}

		WatchdogInput Sample(std::uint64_t nowMs) noexcept
		{
			WatchdogInput input;
			input.nowMs = nowMs;
			const std::uint64_t report = g_pendingReport.load(std::memory_order_acquire);
			input.pendingReason = static_cast<Reason>(static_cast<std::uint32_t>(report));
			input.pendingDetail = static_cast<std::uint32_t>(report >> 32);
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				const std::uint64_t value = g_slots[i].load(std::memory_order_acquire);
				input.slots[i].busy = (value & 1ULL) != 0;
				input.slots[i].lastBeatMs = value >> 1;
			}
			for (std::size_t i = 0; i < kFailureSlotCount; ++i)
			{
				// 首末时间分两个原子：前后各读一次首次时间，期间被清零或开始新一轮就跳过本轮，避免旧首次 + 新末次的错配。
				const std::uint64_t first = g_firstFailure[i].load(std::memory_order_acquire);
				const std::uint64_t last = g_lastFailure[i].load(std::memory_order_acquire);
				const bool consistent = g_firstFailure[i].load(std::memory_order_acquire) == first;
				input.firstFailureMs[i] = consistent ? first : 0;
				input.lastFailureMs[i] = consistent ? last : 0;
			}
			return input;
		}

		void ResetForTest() noexcept
		{
			for (auto& slot : g_slots) slot.store(0, std::memory_order_release);
			for (auto& slot : g_firstFailure) slot.store(0, std::memory_order_release);
			for (auto& slot : g_lastFailure) slot.store(0, std::memory_order_release);
			g_pendingReport.store(0, std::memory_order_release);
		}
	}

	void Report(Reason reason, std::uint32_t detail) noexcept
	{
		if (reason == Reason::None) return;
		std::uint64_t expected = 0;
		// 只保留第一次报告，原因与 detail 一起发布。
		(void)g_pendingReport.compare_exchange_strong(expected,
			(static_cast<std::uint64_t>(detail) << 32) | static_cast<std::uint32_t>(reason),
			std::memory_order_acq_rel);
		if (const HANDLE wake = g_watchdogWake.load(std::memory_order_acquire)) SetEvent(wake);
	}

	void RequestDraw3DebugFault() noexcept
	{
		g_draw3DebugFault.store(true, std::memory_order_release);
	}

	bool ConsumeDraw3DebugFault() noexcept
	{
		// 热路径（每帧）只做一次 relaxed 读，置位时才交换。
		return g_draw3DebugFault.load(std::memory_order_relaxed)
			&& g_draw3DebugFault.exchange(false, std::memory_order_acq_rel);
	}

	void Beat(Slot slot) noexcept
	{
		g_slots[Index(slot)].store((Detail::NowMs() << 1) | 1ULL, std::memory_order_release);
	}

	void Idle(Slot slot) noexcept
	{
		g_slots[Index(slot)].store(Detail::NowMs() << 1, std::memory_order_release);
	}

	void RecordFailure(FailureSlot slot) noexcept
	{
		auto& first = g_firstFailure[Index(slot)];
		auto& last = g_lastFailure[Index(slot)];
		const std::uint64_t now = Detail::NowMs();
		const std::uint64_t previous = last.load(std::memory_order_relaxed);
		// 退避期间不清零：计时延续到第一次失败；但距上次失败已超过陈旧窗口（客户端曾停止尝试）时重新计时。
		if (first.load(std::memory_order_relaxed) == 0 || previous == 0
			|| now < previous || now - previous > Detail::kFailureStaleMs)
			first.store(now, std::memory_order_release);
		last.store(now, std::memory_order_release);
	}

	void RecordSuccess(FailureSlot slot) noexcept
	{
		auto& first = g_firstFailure[Index(slot)];
		// 每帧成功都会调用；只在确有失败计时时才写，避免热路径反复写共享缓存行。
		if (first.load(std::memory_order_relaxed) != 0) first.store(0, std::memory_order_release);
	}

	void ClearFailure(FailureSlot slot) noexcept
	{
		RecordSuccess(slot);
	}
}
