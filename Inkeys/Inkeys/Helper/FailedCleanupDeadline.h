#pragma once

#include <Windows.h>

#include <memory>

namespace Inkeys::Shutdown
{
	// 只发布进程寿命的原子退场前奏；不得捕获业务对象或等待清理。
	using FatalCleanupPublisher = ULONGLONG (*)() noexcept;

	// 仅验真测试 child 传入；事件由测试持有到 join 或进程死亡，helper 不关闭借用句柄。
	struct FailedCleanupTestGates
	{
		bool forceStateAllocationFailure;
		bool forceMonitorCreationFailure;
		bool beginGateAfterWake; // 默认 gates{} 为 false；真实失败停滞夹具先唤醒再停 owner。
		HANDLE afterBeginClaimedEvent;
		HANDLE continueBeginEvent;
		HANDLE beforeExpiredClaimedEvent;
		HANDLE continueExpiredEvent;
		HANDLE monitorCancelExitEnteredEvent;
		HANDLE continueMonitorExitEvent;
	};

	namespace Detail { struct FailedCleanupState; }

	class FailedCleanupDeadline;

	class FailedCleanupSignal
	{
	public:
		FailedCleanupSignal() noexcept = default;
		// 第一次失败固定共同 grace tick；旧 Cancelled Signal 永远返回 0。
		ULONGLONG BeginKnownFailure(ULONGLONG inheritedGraceDeadline = 0) const noexcept;
		[[nodiscard]] bool HasPublisher() const noexcept;
		[[nodiscard]] FatalCleanupPublisher Publisher() const noexcept;
		[[noreturn]] void FailUnprovenProducerStop() const noexcept;

	private:
		friend class FailedCleanupDeadline;
		std::shared_ptr<Detail::FailedCleanupState> state_;
	};

	// scope 的准备/完成由同一管理 owner 串行调用；Signal 按值交给其它 owner。
	class FailedCleanupDeadline
	{
	public:
		explicit FailedCleanupDeadline(FatalCleanupPublisher publisher,
			FailedCleanupTestGates testGates = {}) noexcept;
		FailedCleanupDeadline(const FailedCleanupDeadline&) = delete;
		FailedCleanupDeadline& operator=(const FailedCleanupDeadline&) = delete;
		FailedCleanupDeadline(FailedCleanupDeadline&&) = delete;
		FailedCleanupDeadline& operator=(FailedCleanupDeadline&&) = delete;
		// 准备失败或真实 join 失败都不返回业务清理；未失败的 Dormant 没有启动时限。
		void PrepareOrFatal() noexcept;
		[[nodiscard]] FailedCleanupSignal Signal() const noexcept;
		void CompleteOrFatal() noexcept;
		~FailedCleanupDeadline() noexcept;

	private:
		FatalCleanupPublisher publisher_ = nullptr;
		FailedCleanupTestGates testGates_{};
		std::shared_ptr<Detail::FailedCleanupState> state_;
		HANDLE monitor_ = nullptr;
		bool prepared_ = false;
		bool completed_ = false;
	};
}
