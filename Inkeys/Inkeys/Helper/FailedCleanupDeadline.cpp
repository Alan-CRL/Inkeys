#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "FailedCleanupDeadline.h"

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace Inkeys::Shutdown
{
	static_assert(std::is_trivial_v<FailedCleanupTestGates>);
	static_assert(std::is_standard_layout_v<FailedCleanupTestGates>);
	static_assert(sizeof(FailedCleanupTestGates) == (sizeof(HANDLE) == 4 ? 28 : 56));
	static_assert(offsetof(FailedCleanupTestGates, afterBeginClaimedEvent) == (sizeof(HANDLE) == 4 ? 4 : 8));
	static_assert(std::atomic<ULONGLONG>::is_always_lock_free);

	namespace Detail
	{
		struct FailedCleanupState
		{
			FailedCleanupState(FatalCleanupPublisher value,
				FailedCleanupTestGates gates) noexcept : publisher(value), testGates(gates) {}
			~FailedCleanupState() noexcept
			{
				// Begin 可能已赢 CAS、尚未 SetEvent；wake 只由最后一个强引用关闭。
				if (wake) CloseHandle(wake);
			}

			const FatalCleanupPublisher publisher;
			const FailedCleanupTestGates testGates;
			std::atomic<ULONGLONG> control{ 0 };
			std::atomic<ULONGLONG> fatalDeadline{ 0 };
			HANDLE wake = nullptr;
		};
	}

	namespace
	{
		constexpr ULONGLONG kDormant = 0;
		constexpr ULONGLONG kCancelled = 1;
		constexpr ULONGLONG kExpiredBit = 1ULL << 63;
		constexpr ULONGLONG kGraceMilliseconds = 15000;
		constexpr ULONGLONG kFatalMilliseconds = 15000;
		constexpr DWORD kJoinMilliseconds = 1000;
		constexpr DWORD kFatalRecheckMilliseconds = 50;
		constexpr DWORD kCleanupExpiredExitCode = 0xE143001A;
		constexpr DWORD kPrepareFailedExitCode = 0xE143001B;
		constexpr DWORD kCancelJoinFailedExitCode = 0xE143001C;
		constexpr DWORD kProducerStopUnprovenExitCode = 0xE143001D;

		ULONGLONG DeadlineAfter(ULONGLONG tick, ULONGLONG milliseconds) noexcept
		{
			// 1 是已经到期的安全退场值；不允许溢出或混入 control 的 Expired 高位。
			return tick < kExpiredBit - milliseconds ? tick + milliseconds : 1;
		}

		[[noreturn]] void ForceCurrentProcessAt(ULONGLONG absoluteTick, DWORD exitCode,
			const std::atomic<ULONGLONG>* sharedDeadline = nullptr) noexcept
		{
			for (;;)
			{
				if (sharedDeadline)
				{
					const ULONGLONG published = sharedDeadline->load(std::memory_order_acquire);
					if (published && published < absoluteTick) absoluteTick = published;
				}
				const ULONGLONG now = GetTickCount64();
				if (now < absoluteTick)
				{
					const ULONGLONG remaining = absoluteTick - now;
					// 仅 fatal owner 重查可能缩短的共同截止，正常取消不轮询。
					Sleep(static_cast<DWORD>(remaining < kFatalRecheckMilliseconds
						? remaining : kFatalRecheckMilliseconds));
					continue;
				}
				TerminateProcess(GetCurrentProcess(), exitCode);
				Sleep(1); // 自终止异常返回也不进入析构、业务清理或新一代初始化。
			}
		}

		void SaveEarlierDeadline(std::atomic<ULONGLONG>& destination, ULONGLONG deadline) noexcept
		{
			auto current = destination.load(std::memory_order_acquire);
			while ((current == 0 || deadline < current) && !destination.compare_exchange_weak(current, deadline,
				std::memory_order_acq_rel, std::memory_order_acquire)) {}
		}

		[[noreturn]] void FatalAt(Detail::FailedCleanupState* state,
			FatalCleanupPublisher publisher, ULONGLONG ownDeadline, DWORD exitCode) noexcept
		{
			if (!ownDeadline) ownDeadline = 1;
			if (state)
			{
				// 在无等待 publisher 前发布完整 tick；并发接管不延长首次截止。
				SaveEarlierDeadline(state->fatalDeadline, ownDeadline);
				const ULONGLONG existing = publisher ? publisher() : 0;
				if (existing) SaveEarlierDeadline(state->fatalDeadline, existing);
				ForceCurrentProcessAt(state->fatalDeadline.load(std::memory_order_acquire),
					exitCode, &state->fatalDeadline);
			}
			// OOM 时没有 State；当前线程先存自己的 tick，再取原普通/UEF 的更早截止。
			const ULONGLONG existing = publisher ? publisher() : 0;
			if (existing && existing < ownDeadline) ownDeadline = existing;
			ForceCurrentProcessAt(ownDeadline, exitCode);
		}

		[[noreturn]] void FatalNow(Detail::FailedCleanupState* state,
			FatalCleanupPublisher publisher, DWORD exitCode) noexcept
		{
			FatalAt(state, publisher, DeadlineAfter(GetTickCount64(), kFatalMilliseconds), exitCode);
		}

		[[noreturn]] void FatalExpired(Detail::FailedCleanupState& state,
			ULONGLONG graceDeadline) noexcept
		{
			const ULONGLONG deadline = graceDeadline > kCancelled && graceDeadline < kExpiredBit
				? DeadlineAfter(graceDeadline, kFatalMilliseconds) : 1;
			FatalAt(&state, state.publisher, deadline, kCleanupExpiredExitCode);
		}

		void WaitTestGate(Detail::FailedCleanupState& state,
			HANDLE reached, HANDLE proceed) noexcept
		{
			if (reached && !SetEvent(reached))
				FatalNow(&state, state.publisher, kCancelJoinFailedExitCode);
			if (proceed && WaitForSingleObject(proceed, INFINITE) != WAIT_OBJECT_0)
				FatalNow(&state, state.publisher, kCancelJoinFailedExitCode);
		}

		bool ClaimExpired(Detail::FailedCleanupState& state, ULONGLONG& control) noexcept
		{
			const ULONGLONG deadline = control;
			// 同一个 CAS 发布终态和原 tick，另一 owner 不等额外字段发布。
			return state.control.compare_exchange_strong(control, deadline | kExpiredBit,
				std::memory_order_acq_rel, std::memory_order_acquire);
		}

		struct MonitorEnvelope
		{
			std::shared_ptr<Detail::FailedCleanupState> state;
		};

		DWORD WINAPI MonitorThread(void* parameter) noexcept
		{
			auto* envelope = static_cast<MonitorEnvelope*>(parameter);
			auto state = std::move(envelope->state);
			delete envelope;
			for (;;)
			{
				auto control = state->control.load(std::memory_order_acquire);
				if (control == kCancelled)
				{
					WaitTestGate(*state, state->testGates.monitorCancelExitEnteredEvent,
						state->testGates.continueMonitorExitEvent);
					return ERROR_SUCCESS;
				}
				if (control & kExpiredBit) FatalExpired(*state, control & ~kExpiredBit);
				DWORD waitMilliseconds = INFINITE;
				if (control != kDormant)
				{
					const ULONGLONG now = GetTickCount64();
					if (now >= control)
					{
						const ULONGLONG deadline = control;
						// 此门只暂停 monitor；Complete 必须仍能独立竞争 Expired 后接管。
						WaitTestGate(*state, state->testGates.beforeExpiredClaimedEvent,
							state->testGates.continueExpiredEvent);
						if (ClaimExpired(*state, control)) FatalExpired(*state, deadline);
						continue;
					}
					const ULONGLONG remaining = control - now;
					waitMilliseconds = static_cast<DWORD>(remaining < INFINITE
						? remaining : static_cast<ULONGLONG>(INFINITE) - 1);
				}
				const DWORD result = WaitForSingleObject(state->wake, waitMilliseconds);
				if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT)
					FatalNow(state.get(), state->publisher, kCancelJoinFailedExitCode);
			}
		}
	}

	ULONGLONG FailedCleanupSignal::BeginKnownFailure(ULONGLONG inheritedGraceDeadline) const noexcept
	{
		const auto state = state_;
		if (!state) return 0;
		auto control = state->control.load(std::memory_order_acquire);
		for (;;)
		{
			if (control == kCancelled) return 0;
			if (control & kExpiredBit) FatalExpired(*state, control & ~kExpiredBit);
			if (control != kDormant) return control;
			if (inheritedGraceDeadline && (inheritedGraceDeadline <= kCancelled
				|| inheritedGraceDeadline >= kExpiredBit))
				FatalNow(state.get(), state->publisher, kCleanupExpiredExitCode);
			ULONGLONG deadline = DeadlineAfter(GetTickCount64(), kGraceMilliseconds);
			if (deadline <= kCancelled)
				FatalNow(state.get(), state->publisher, kCleanupExpiredExitCode);
			if (inheritedGraceDeadline && inheritedGraceDeadline < deadline) deadline = inheritedGraceDeadline;
			if (!state->control.compare_exchange_strong(control, deadline,
				std::memory_order_acq_rel, std::memory_order_acquire)) continue;
			if (!state->testGates.beginGateAfterWake)
				WaitTestGate(*state, state->testGates.afterBeginClaimedEvent,
					state->testGates.continueBeginEvent);
			if (!SetEvent(state->wake))
				FatalNow(state.get(), state->publisher, kCancelJoinFailedExitCode);
			// 验真真实模块夹具在此暂停 owner；monitor 已收到 wake，不能让测试本身使其永眠。
			if (state->testGates.beginGateAfterWake)
				WaitTestGate(*state, state->testGates.afterBeginClaimedEvent,
					state->testGates.continueBeginEvent);
			return deadline;
		}
	}

	bool FailedCleanupSignal::HasPublisher() const noexcept
	{
		return Publisher() != nullptr;
	}

	FatalCleanupPublisher FailedCleanupSignal::Publisher() const noexcept
	{
		return state_ ? state_->publisher : nullptr;
	}

	[[noreturn]] void FailedCleanupSignal::FailUnprovenProducerStop() const noexcept
	{
		const auto state = state_;
		// 停止证明失败不能释放 plugin/input/HWND；owning 旧 Signal 仍有安全静态 publisher。
		FatalNow(state.get(), state ? state->publisher : nullptr, kProducerStopUnprovenExitCode);
	}

	FailedCleanupDeadline::FailedCleanupDeadline(FatalCleanupPublisher publisher,
		FailedCleanupTestGates testGates) noexcept : publisher_(publisher), testGates_(testGates) {}

	void FailedCleanupDeadline::PrepareOrFatal() noexcept
	{
		if (prepared_ || completed_) return;
		if (!publisher_ || testGates_.forceStateAllocationFailure)
			FatalNow(nullptr, publisher_, kPrepareFailedExitCode);
		try
		{
			state_ = std::make_shared<Detail::FailedCleanupState>(publisher_, testGates_);
		}
		catch (...)
		{
			FatalNow(nullptr, publisher_, kPrepareFailedExitCode);
		}
		state_->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!state_->wake) FatalNow(state_.get(), publisher_, kPrepareFailedExitCode);
		auto* envelope = new (std::nothrow) MonitorEnvelope{ state_ };
		if (!envelope) FatalNow(state_.get(), publisher_, kPrepareFailedExitCode);
		monitor_ = testGates_.forceMonitorCreationFailure ? nullptr
			: CreateThread(nullptr, 0, &MonitorThread, envelope, 0, nullptr);
		// 创建失败后不再依赖堆释放或新线程；envelope/State/句柄保留到进程死亡。
		if (!monitor_) FatalNow(state_.get(), publisher_, kPrepareFailedExitCode);
		prepared_ = true;
	}

	FailedCleanupSignal FailedCleanupDeadline::Signal() const noexcept
	{
		FailedCleanupSignal signal;
		signal.state_ = state_;
		return signal;
	}

	void FailedCleanupDeadline::CompleteOrFatal() noexcept
	{
		if (!prepared_ || completed_) return;
		const auto state = state_;
		if (!state || !monitor_) FatalNow(state.get(), publisher_, kCancelJoinFailedExitCode);
		auto control = state->control.load(std::memory_order_acquire);
		for (;;)
		{
			if (control == kCancelled) break;
			if (control & kExpiredBit) FatalExpired(*state, control & ~kExpiredBit);
			if (control != kDormant && GetTickCount64() >= control)
			{
				const ULONGLONG deadline = control;
				if (ClaimExpired(*state, control)) FatalExpired(*state, deadline);
				continue;
			}
			if (state->control.compare_exchange_strong(control, kCancelled,
				std::memory_order_acq_rel, std::memory_order_acquire)) break;
		}
		if (!SetEvent(state->wake)) FatalNow(state.get(), publisher_, kCancelJoinFailedExitCode);
		const DWORD result = WaitForSingleObject(monitor_, kJoinMilliseconds);
		if (result != WAIT_OBJECT_0) FatalNow(state.get(), publisher_, kCancelJoinFailedExitCode);
		// 只有内核 thread HANDLE 真正 signaled 才能释放管理句柄和 scope 引用。
		if (!CloseHandle(monitor_)) FatalNow(state.get(), publisher_, kCancelJoinFailedExitCode);
		monitor_ = nullptr;
		completed_ = true;
		state_.reset();
	}

	FailedCleanupDeadline::~FailedCleanupDeadline() noexcept
	{
		if (prepared_ && !completed_) CompleteOrFatal();
	}
}
