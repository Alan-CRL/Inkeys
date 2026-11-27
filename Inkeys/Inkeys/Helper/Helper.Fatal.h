#pragma once

// 教学安全模式：软件内致命故障、卡死与持续失败的统一兜底（C1.6a）。
// 所有触发最终都交给 CrashHandler 的未处理异常处理核心，按“崩溃处理方式”三档处理。
// 本头文件供传统 .cpp 与 module 的全局模块片段直接 include。

#include <array>
#include <cstddef>
#include <cstdint>

// 不 include <Windows.h>：本头文件被许多 TU 在不同的 NOMINMAX 状态下引入，不能提前定义 min/max 宏。
struct _EXCEPTION_POINTERS;

namespace Inkeys::Fatal
{
	enum class Reason : std::uint32_t
	{
		None = 0,
		Draw3Stopped = 1,
		Draw3PresentFailures,
		UiDeviceRecovery,
		UiClientFailures,
		FreezeFailures,
		ThreadHang,
		CrtAbort,
		CrtInvalidParameter,
		CrtPureCall,
		DebugInjected,
		// 运行期已知失败的清理超时：由看门狗接手走 UEF；不能等用户，提示档按静默重启处理。
		KnownFailureCleanup,
	};

	// 与现有 0xE14300xx 退出码同前缀；崩溃报告据此区分真崩溃与兜底触发。
	constexpr unsigned long kExceptionCode = 0xE1430100; // DWORD

	const char* ReasonName(Reason reason) noexcept;

	// 任意线程：只写原子量并唤醒看门狗，由看门狗线程触发。看门狗未启动时延后到启动后处理。
	void Report(Reason reason, std::uint32_t detail = 0) noexcept;
	// 当前线程立即交给崩溃处理核心，处理完结束进程。处理器未安装时直接结束进程；
	// 正常退出已受理（意图槽为 Close/Restart）或另一线程已在处理时本线程挂起，由对方结束进程。
	[[noreturn]] void RaiseNow(Reason reason, std::uint32_t detail = 0) noexcept;

	// 卡死检测：每开始处理一个工作单元调用 Beat，进入真正的空闲等待前调用 Idle。
	// 判定的是“当前工作单元已执行多久”，不是“连续忙碌多久”。
	enum class Slot : std::uint8_t { UiScheduler, Draw3Loop, Count };
	void Beat(Slot slot) noexcept;
	void Idle(Slot slot) noexcept;

	// 持续失败检测：只在客户端真实 API 失败的分支记录，调度器的 Retry（延期、退避、等回执）不算失败。
	enum class FailureSlot : std::uint8_t
	{
		UiDeviceRecovery, UiBar, UiSetting, UiPageControl, UiPageControl2, UiPageControl3, UiPageControl4,
		UiWhiteboard, UiSelection,
		UiStartupPreview, Draw3Present, Freeze, Count
	};
	void RecordFailure(FailureSlot slot) noexcept;  // 真实故障：首次失败开始计时
	void RecordSuccess(FailureSlot slot) noexcept;  // 一次成功提交：清除计时
	void ClearFailure(FailureSlot slot) noexcept;   // 隐藏、注销、换代：清除计时，不算成功也不算失败

	// 调试页“模拟绘制致命错误”：设置标志后由 Draw3 绘制循环消费并抛出异常，验证 Draw3 停止 → 教学安全模式。
	void RequestDraw3DebugFault() noexcept;
	bool ConsumeDraw3DebugFault() noexcept;

	// 看门狗在启动门禁完成后启动；Stop 用于正常退出。
	bool StartWatchdog() noexcept;
	void StopWatchdog() noexcept;
	bool RuntimeArmed() noexcept;

	// 由 CrashHandler 在安装/撤销 UEF 时调用（调用方串行化）。处理核心为空表示未安装或已撤销。
	using CrashCore = long(__stdcall*)(_EXCEPTION_POINTERS*); // 与 LPTOP_LEVEL_EXCEPTION_FILTER 相同
	void RegisterCrashCore(CrashCore core) noexcept;
	// UEF 与 RaiseNow 共用的一次性接管（携带线程 ID）。First：本线程负责处理；OtherThread：另一线程已在处理，
	// 本线程应挂起；SameThread：处理中本线程再次出错（递归），不能再处理。接管后看门狗不再触发。
	enum class CrashClaim : std::uint8_t { First, OtherThread, SameThread };
	CrashClaim ClaimCrashHandling() noexcept;
	// CRT 入口：SIGABRT（覆盖 std::terminate）、invalid parameter、purecall。只在首次成功安装时保存旧值。
	void InstallCrtHandlers() noexcept;
	void RestoreCrtHandlers() noexcept;

	namespace Detail
	{
		constexpr std::uint64_t kHangThresholdMs = 15000;
		constexpr std::uint64_t kFailureThresholdMs = 10000;
		// 最近一次失败超过此时长视为客户端已停止尝试（隐藏、空闲、注销）；再失败时重新计时。
		constexpr std::uint64_t kFailureStaleMs = 5000;
		constexpr std::size_t kSlotCount = static_cast<std::size_t>(Slot::Count);
		constexpr std::size_t kFailureSlotCount = static_cast<std::size_t>(FailureSlot::Count);

		struct SlotSample
		{
			bool busy = false;
			std::uint64_t lastBeatMs = 0;
		};

		struct WatchdogInput
		{
			std::uint64_t nowMs = 0;
			Reason pendingReason = Reason::None;
			std::uint32_t pendingDetail = 0;
			std::array<SlotSample, kSlotCount> slots{};
			// 0 表示该槽当前没有持续失败；否则为第一次失败的时刻。
			std::array<std::uint64_t, kFailureSlotCount> firstFailureMs{};
			std::array<std::uint64_t, kFailureSlotCount> lastFailureMs{};
			bool exitIntentClaimed = false;  // 意图槽非 0：退出已受理，由退出流程自己的 15 秒监督兜底
			bool crashHandling = false;      // UEF 正在处理
			bool debuggerPresent = false;
		};

		struct WatchdogDecision
		{
			bool trigger = false;
			Reason reason = Reason::None;
			std::uint32_t detail = 0;
		};

		constexpr Reason ReasonForFailureSlot(FailureSlot slot) noexcept
		{
			switch (slot)
			{
			case FailureSlot::UiDeviceRecovery: return Reason::UiDeviceRecovery;
			case FailureSlot::Draw3Present: return Reason::Draw3PresentFailures;
			case FailureSlot::Freeze: return Reason::FreezeFailures;
			default: return Reason::UiClientFailures;
			}
		}

		// 纯函数：输入时刻与各槽状态，输出是否触发及原因。单元测试直接覆盖。
		constexpr WatchdogDecision EvaluateWatchdog(const WatchdogInput& input) noexcept
		{
			if (input.exitIntentClaimed || input.crashHandling || input.debuggerPresent) return {};
			if (input.pendingReason != Reason::None)
				return { true, input.pendingReason, input.pendingDetail };
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				const SlotSample& slot = input.slots[i];
				if (slot.busy && input.nowMs >= slot.lastBeatMs
					&& input.nowMs - slot.lastBeatMs >= kHangThresholdMs)
					return { true, Reason::ThreadHang, static_cast<std::uint32_t>(i) };
			}
			for (std::size_t i = 0; i < kFailureSlotCount; ++i)
			{
				const std::uint64_t first = input.firstFailureMs[i];
				const std::uint64_t last = input.lastFailureMs[i];
				// 失败须仍在持续：第一次失败至今 ≥ 10 秒，且最近一次失败在 5 秒内。
				if (first != 0 && input.nowMs >= first && input.nowMs - first >= kFailureThresholdMs
					&& input.nowMs >= last && input.nowMs - last <= kFailureStaleMs)
					return { true, ReasonForFailureSlot(static_cast<FailureSlot>(i)),
						static_cast<std::uint32_t>(i) };
			}
			return {};
		}

		// 不含系统睡眠/休眠的毫秒时钟，避免唤醒后把睡眠时长误判为卡死。
		std::uint64_t NowMs() noexcept;
		// 从原子状态取样；退出意图、UEF 与调试器状态由调用方补齐。
		WatchdogInput Sample(std::uint64_t nowMs) noexcept;
		// 仅测试：清空全部原子状态。
		void ResetForTest() noexcept;
	}
}
