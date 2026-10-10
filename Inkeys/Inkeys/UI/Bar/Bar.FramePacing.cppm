module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>

export module Inkeys.UI.Bar.FramePacing;

export namespace Inkeys::UI::Bar
{
	enum class FramePacingWaitStrategy
	{
		FixedSpinTail,
		AdaptiveSpinTail,
		SleepOnly,
		WaitableTimer,
	};

	enum class FramePacingWaitBackend
	{
		None,
		Legacy,
		SleepOnly,
		HighResolutionWaitableTimer,
		CompatibleWaitableTimer,
	};

	enum class FramePacingWaitableTimerResult
	{
		NotAttempted,
		Completed,
		Unavailable,
		SetFailed,
		WaitFailed,
	};

	struct FramePacingWaitPolicy
	{
		FramePacingWaitStrategy strategy = FramePacingWaitStrategy::FixedSpinTail;
		double fixedSpinTailMs = 1.5;
		double adaptiveMinimumSpinTailMs = 0.25;
		double adaptiveMaximumSpinTailMs = 1.5;

		static constexpr FramePacingWaitPolicy Default() noexcept;
		static constexpr FramePacingWaitPolicy Legacy() noexcept;
		static constexpr FramePacingWaitPolicy FixedSpinTail(double spinTailMs) noexcept;
		static constexpr FramePacingWaitPolicy AdaptiveSpinTail(
			double minimumSpinTailMs = 0.25,
			double maximumSpinTailMs = 1.5) noexcept;
		static constexpr FramePacingWaitPolicy SleepOnly() noexcept;
		static constexpr FramePacingWaitPolicy WaitableTimer(
			double spinTailMs = 0.25) noexcept;
	};

	struct FramePacingAdaptiveState
	{
		double currentSpinTailMs = 1.5;
		bool initialized = false;
	};

	struct FramePacingMeasurement
	{
		double requestedWaitMs = 0.0;
		double sleepMs = 0.0;
		double waitMs = 0.0;
		double spinMs = 0.0;
		double elapsedMs = 0.0;
		double signedOvershootMs = 0.0;
		std::uint64_t threadCycles = 0;
		bool threadCyclesAvailable = false;
		FramePacingWaitBackend waitBackend = FramePacingWaitBackend::None;
		FramePacingWaitBackend attemptedWaitableTimerBackend =
			FramePacingWaitBackend::None;
		FramePacingWaitableTimerResult waitableTimerResult =
			FramePacingWaitableTimerResult::NotAttempted;
		bool waitableTimerFallback = false;
	};

	// 动画时钟可在真正 idle 唤醒时重置，避免把休眠时间计入首帧动画。
	class FrameAnimationClock
	{
	public:
		using Clock = std::chrono::steady_clock;

		explicit FrameAnimationClock(
			Clock::time_point now = Clock::now()) noexcept
			: reckon_(now)
		{
		}

		double Tick(Clock::time_point now = Clock::now()) noexcept
		{
			double elapsedSeconds =
				std::chrono::duration<double>(now - reckon_).count();
			reckon_ = now;
			if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0)
				return 0.0;
			return std::clamp(elapsedSeconds, 0.0, 0.05);
		}

		void Rebase(Clock::time_point now = Clock::now()) noexcept
		{
			reckon_ = now;
		}

	private:
		Clock::time_point reckon_;
	};

	struct FrameRateAverages
	{
		double actualFramesPerSecond = 0.0;
		double unlimitedFramesPerSecond = 0.0;
		bool updated = false;
	};

	// 每个完整窗口才锁存一次；无限制帧率只累计排除 pacing 等待后的工作时间。
	class OneSecondFrameRate
	{
	public:
		using Clock = std::chrono::steady_clock;

		explicit OneSecondFrameRate(
			Clock::duration window = std::chrono::seconds(1),
			Clock::time_point now = Clock::now()) noexcept
			: window_(window > Clock::duration::zero()
				? window : std::chrono::seconds(1)),
			bucketStart_(now),
			lastTick_(now)
		{
		}

		FrameRateAverages Tick(
			Clock::duration activeFrameTime,
			Clock::time_point now = Clock::now()) noexcept
		{
			if (now <= lastTick_)
			{
				// 时钟回拨或测试重放时重建统计桶，禁止输出负间隔。
				Reset(now);
			}
			lastTick_ = now;
			++frameCount_;

			const double activeSeconds =
				std::chrono::duration<double>(activeFrameTime).count();
			if (activeSeconds > 0.0 && std::isfinite(activeSeconds))
			{
				activeSeconds_ += activeSeconds;
				++activeFrameCount_;
			}

			FrameRateAverages result{
				publishedActual_, publishedUnlimited_, false };
			if (now - bucketStart_ < window_) return result;

			const double elapsedSeconds =
				std::chrono::duration<double>(now - bucketStart_).count();
			if (elapsedSeconds > 0.0 && std::isfinite(elapsedSeconds))
				publishedActual_ = static_cast<double>(frameCount_) / elapsedSeconds;
			else
				publishedActual_ = 0.0;
			publishedUnlimited_ = activeSeconds_ > 0.0
				? static_cast<double>(activeFrameCount_) / activeSeconds_
				: 0.0;
			result = { publishedActual_, publishedUnlimited_, true };

			bucketStart_ = now;
			frameCount_ = 0;
			activeFrameCount_ = 0;
			activeSeconds_ = 0.0;
			return result;
		}

		void Reset(Clock::time_point now = Clock::now()) noexcept
		{
			bucketStart_ = now;
			lastTick_ = now;
			frameCount_ = 0;
			activeFrameCount_ = 0;
			activeSeconds_ = 0.0;
			publishedActual_ = 0.0;
			publishedUnlimited_ = 0.0;
		}

	private:
		Clock::duration window_;
		Clock::time_point bucketStart_;
		Clock::time_point lastTick_;
		std::uint64_t frameCount_ = 0;
		std::uint64_t activeFrameCount_ = 0;
		double activeSeconds_ = 0.0;
		double publishedActual_ = 0.0;
		double publishedUnlimited_ = 0.0;
	};

	// FPS 文字只在真实渲染结束后补一帧休眠标记，不能反向维持渲染循环。
	class DebugFrameSleepLatch
	{
	public:
		[[nodiscard]] bool Update(
			bool enabled,
			bool hasActiveRendering) noexcept
		{
			if (!enabled || hasActiveRendering)
			{
				pending_ = false;
				presented_ = false;
				return false;
			}
			if (!presented_) pending_ = true;
			return pending_;
		}

		[[nodiscard]] bool IsPending() const noexcept
		{
			return pending_;
		}

		[[nodiscard]] bool IsPresented() const noexcept
		{
			return presented_;
		}

		bool CommitPresented() noexcept
		{
			if (!pending_) return false;
			pending_ = false;
			presented_ = true;
			return true;
		}

	private:
		bool pending_ = false;
		bool presented_ = false;
	};

	// 默认使用 waitable timer；Win7/创建或等待失败时在内部有界回退。
	void WaitForRemainingFrameTime(double requestedWaitMs);

	// measurement 非空才采集分段 QPC 与线程周期；默认产品路径不会进入测量分支。
	void WaitForRemainingFrameTime(
		double requestedWaitMs,
		const FramePacingWaitPolicy& policy,
		FramePacingAdaptiveState* adaptiveState,
		FramePacingMeasurement* measurement);

	void HighPrecisionWait(double frameTimeSpentMs, double targetFramesPerSecond);
}

namespace
{
	constexpr double FramePacingSleepGateMs = 2.0;
	constexpr double LegacySpinTailMs = 1.5;
	constexpr double AdaptiveTailDecay = 0.125;

	std::int64_t FramePacingQpcFrequency() noexcept
	{
		static const std::int64_t frequency = []
			{
				LARGE_INTEGER result{};
				QueryPerformanceFrequency(&result);
				return static_cast<std::int64_t>(result.QuadPart);
			}();
		return frequency;
	}

	void FramePacingSleepForMilliseconds(double waitMs)
	{
		constexpr DWORD sleepChunkMs = 60'000;
		constexpr double sleepChunkMsDouble = static_cast<double>(sleepChunkMs);
		if (waitMs <= sleepChunkMsDouble)
		{
			std::this_thread::sleep_for(
				std::chrono::duration<double, std::milli>(waitMs));
			return;
		}

		// 极大 finite 输入分块交给 Win32，避免 chrono 转为整数 duration 时溢出。
		double remainingMs = waitMs;
		while (remainingMs > sleepChunkMsDouble)
		{
			Sleep(sleepChunkMs);
			remainingMs -= sleepChunkMsDouble;
		}
		if (remainingMs > 0.0)
			std::this_thread::sleep_for(
				std::chrono::duration<double, std::milli>(remainingMs));
	}

	double FramePacingTicksToMilliseconds(
		std::int64_t ticks,
		std::int64_t frequency) noexcept
	{
		return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency);
	}

	std::int64_t FramePacingTargetEndTick(
		std::int64_t startTick,
		double requestedWaitMs,
		std::int64_t frequency) noexcept
	{
		const long double requestedTicks = static_cast<long double>(requestedWaitMs)
			* static_cast<long double>(frequency) / 1000.0L;
		const long double maximumTicks = static_cast<long double>(
			(std::numeric_limits<std::int64_t>::max)());
		if (requestedTicks >= maximumTicks)
			return (std::numeric_limits<std::int64_t>::max)();
		const std::int64_t waitTicks = static_cast<std::int64_t>(requestedTicks);
		if (startTick > (std::numeric_limits<std::int64_t>::max)() - waitTicks)
			return (std::numeric_limits<std::int64_t>::max)();
		return startTick + waitTicks;
	}

	std::int64_t FramePacingRelativeHundredNanoseconds(double waitMs) noexcept
	{
		const long double requestedTicks = std::ceil(
			static_cast<long double>(waitMs) * 10000.0L);
		const long double maximumTicks = static_cast<long double>(
			(std::numeric_limits<std::int64_t>::max)());
		if (requestedTicks >= maximumTicks)
			return (std::numeric_limits<std::int64_t>::max)();
		return (std::max)(std::int64_t{ 1 }, static_cast<std::int64_t>(requestedTicks));
	}

	struct FramePacingTimerWaitOutcome
	{
		Inkeys::UI::Bar::FramePacingWaitableTimerResult result =
			Inkeys::UI::Bar::FramePacingWaitableTimerResult::Unavailable;
		Inkeys::UI::Bar::FramePacingWaitBackend backend =
			Inkeys::UI::Bar::FramePacingWaitBackend::None;
		bool compatibilityFallback = false;
	};

	class FramePacingThreadTimer
	{
	public:
		FramePacingThreadTimer() noexcept
		{
			if (!Create(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
				Inkeys::UI::Bar::FramePacingWaitBackend::HighResolutionWaitableTimer))
			{
				// 旧系统不识别高分辨率 flag 时，退回兼容 waitable timer。
				compatibilityFallback_ = true;
				Create(0, Inkeys::UI::Bar::FramePacingWaitBackend::CompatibleWaitableTimer);
			}
		}

		~FramePacingThreadTimer() noexcept
		{
			Reset();
		}

		FramePacingThreadTimer(const FramePacingThreadTimer&) = delete;
		FramePacingThreadTimer& operator=(const FramePacingThreadTimer&) = delete;

		FramePacingTimerWaitOutcome Wait(double waitMs) noexcept
		{
			FramePacingTimerWaitOutcome outcome{};
			outcome.backend = backend_;
			outcome.compatibilityFallback = compatibilityFallback_;
			if (!timer_) return outcome;

			LARGE_INTEGER dueTime{};
			dueTime.QuadPart = -FramePacingRelativeHundredNanoseconds(waitMs);
			if (SetWaitableTimer(timer_, &dueTime, 0, nullptr, nullptr, FALSE) == FALSE)
			{
				outcome.result = Inkeys::UI::Bar::FramePacingWaitableTimerResult::SetFailed;
				HandleOperationFailure();
				return outcome;
			}

			const DWORD waitResult = WaitForSingleObject(timer_, INFINITE);
			if (waitResult != WAIT_OBJECT_0)
			{
				outcome.result = Inkeys::UI::Bar::FramePacingWaitableTimerResult::WaitFailed;
				HandleOperationFailure();
				return outcome;
			}

			outcome.result = Inkeys::UI::Bar::FramePacingWaitableTimerResult::Completed;
			return outcome;
		}

	private:
		bool Create(
			DWORD flags,
			Inkeys::UI::Bar::FramePacingWaitBackend backend) noexcept
		{
			timer_ = CreateWaitableTimerExW(
				nullptr,
				nullptr,
				flags,
				TIMER_MODIFY_STATE | SYNCHRONIZE);
			if (!timer_) return false;
			backend_ = backend;
			return true;
		}

		void Reset() noexcept
		{
			if (timer_)
			{
				CloseHandle(timer_);
				timer_ = nullptr;
			}
			backend_ = Inkeys::UI::Bar::FramePacingWaitBackend::None;
		}

		void HandleOperationFailure() noexcept
		{
			const bool retryCompatible = backend_
				== Inkeys::UI::Bar::FramePacingWaitBackend::HighResolutionWaitableTimer;
			Reset();
			if (retryCompatible)
			{
				// 当前帧由 legacy 补足；后续帧直接使用兼容 timer，避免重复失败。
				compatibilityFallback_ = true;
				Create(0, Inkeys::UI::Bar::FramePacingWaitBackend::CompatibleWaitableTimer);
			}
		}

		HANDLE timer_ = nullptr;
		Inkeys::UI::Bar::FramePacingWaitBackend backend_ =
			Inkeys::UI::Bar::FramePacingWaitBackend::None;
		bool compatibilityFallback_ = false;
	};

	FramePacingThreadTimer& FramePacingGetThreadTimer() noexcept
	{
		thread_local FramePacingThreadTimer timer;
		return timer;
	}

	double FramePacingFixedTail(const Inkeys::UI::Bar::FramePacingWaitPolicy& policy) noexcept
	{
		double tail = std::isfinite(policy.fixedSpinTailMs)
			? policy.fixedSpinTailMs : LegacySpinTailMs;
		return std::clamp(tail, 0.0, FramePacingSleepGateMs);
	}

	void FramePacingAdaptiveBounds(
		const Inkeys::UI::Bar::FramePacingWaitPolicy& policy,
		double& minimumTailMs,
		double& maximumTailMs) noexcept
	{
		double first = std::isfinite(policy.adaptiveMinimumSpinTailMs)
			? policy.adaptiveMinimumSpinTailMs : 0.25;
		double second = std::isfinite(policy.adaptiveMaximumSpinTailMs)
			? policy.adaptiveMaximumSpinTailMs : LegacySpinTailMs;
		first = std::clamp(first, 0.0, FramePacingSleepGateMs);
		second = std::clamp(second, 0.0, FramePacingSleepGateMs);
		minimumTailMs = std::min(first, second);
		maximumTailMs = std::max(first, second);
	}

	double FramePacingAdaptiveTail(
		const Inkeys::UI::Bar::FramePacingWaitPolicy& policy,
		Inkeys::UI::Bar::FramePacingAdaptiveState* state) noexcept
	{
		double minimumTailMs = 0.0;
		double maximumTailMs = 0.0;
		FramePacingAdaptiveBounds(policy, minimumTailMs, maximumTailMs);
		if (!state) return maximumTailMs;
		if (!state->initialized || !std::isfinite(state->currentSpinTailMs))
		{
			// 外部状态损坏时回到冷启动上界，禁止 NaN 进入 sleep_for。
			state->currentSpinTailMs = maximumTailMs;
			state->initialized = false;
			return maximumTailMs;
		}
		return std::clamp(state->currentSpinTailMs, minimumTailMs, maximumTailMs);
	}

	void FramePacingUpdateAdaptiveTail(
		const Inkeys::UI::Bar::FramePacingWaitPolicy& policy,
		Inkeys::UI::Bar::FramePacingAdaptiveState* state,
		double sleepRequestedMs,
		double sleepElapsedMs) noexcept
	{
		if (!state) return;
		double minimumTailMs = 0.0;
		double maximumTailMs = 0.0;
		FramePacingAdaptiveBounds(policy, minimumTailMs, maximumTailMs);

		// 超时增大时立即抬升尾部，恢复时缓慢下降，避免相邻帧来回振荡。
		double observedOvershootMs = std::max(0.0, sleepElapsedMs - sleepRequestedMs);
		double desiredTailMs = std::clamp(
			observedOvershootMs + minimumTailMs,
			minimumTailMs,
			maximumTailMs);
		if (!state->initialized || !std::isfinite(state->currentSpinTailMs)
			|| desiredTailMs > state->currentSpinTailMs)
			state->currentSpinTailMs = desiredTailMs;
		else
			state->currentSpinTailMs +=
				(desiredTailMs - state->currentSpinTailMs) * AdaptiveTailDecay;
		state->currentSpinTailMs = std::clamp(
			state->currentSpinTailMs, minimumTailMs, maximumTailMs);
		state->initialized = true;
	}

	void FramePacingFinishCycleMeasurement(
		Inkeys::UI::Bar::FramePacingMeasurement* measurement,
		bool cycleStartAvailable,
		ULONG64 cycleStart) noexcept
	{
		if (!measurement || !cycleStartAvailable) return;
		ULONG64 cycleEnd = 0;
		if (QueryThreadCycleTime(GetCurrentThread(), &cycleEnd) && cycleEnd >= cycleStart)
		{
			measurement->threadCycles = static_cast<std::uint64_t>(cycleEnd - cycleStart);
			measurement->threadCyclesAvailable = true;
		}
	}
}

namespace Inkeys::UI::Bar
{
	constexpr FramePacingWaitPolicy FramePacingWaitPolicy::Legacy() noexcept
	{
		return { FramePacingWaitStrategy::FixedSpinTail, 1.5, 0.25, 1.5 };
	}

	constexpr FramePacingWaitPolicy FramePacingWaitPolicy::FixedSpinTail(
		double spinTailMs) noexcept
	{
		return { FramePacingWaitStrategy::FixedSpinTail, spinTailMs, 0.25, 1.5 };
	}

	constexpr FramePacingWaitPolicy FramePacingWaitPolicy::AdaptiveSpinTail(
		double minimumSpinTailMs,
		double maximumSpinTailMs) noexcept
	{
		return {
			FramePacingWaitStrategy::AdaptiveSpinTail,
			1.5,
			minimumSpinTailMs,
			maximumSpinTailMs,
		};
	}

	constexpr FramePacingWaitPolicy FramePacingWaitPolicy::SleepOnly() noexcept
	{
		return { FramePacingWaitStrategy::SleepOnly, 0.0, 0.25, 1.5 };
	}

	constexpr FramePacingWaitPolicy FramePacingWaitPolicy::WaitableTimer(
		double spinTailMs) noexcept
	{
		return { FramePacingWaitStrategy::WaitableTimer, spinTailMs, 0.25, 1.5 };
	}

	constexpr FramePacingWaitPolicy FramePacingWaitPolicy::Default() noexcept
	{
		return { FramePacingWaitStrategy::WaitableTimer, 0.25, 0.25, 1.5 };
	}

	void WaitForRemainingFrameTime(double requestedWaitMs)
	{
		constexpr FramePacingWaitPolicy policy = FramePacingWaitPolicy::Default();
		WaitForRemainingFrameTime(requestedWaitMs, policy, nullptr, nullptr);
	}

	void WaitForRemainingFrameTime(
		double requestedWaitMs,
		const FramePacingWaitPolicy& policy,
		FramePacingAdaptiveState* adaptiveState,
		FramePacingMeasurement* measurement)
	{
		if (measurement)
		{
			*measurement = {};
			measurement->requestedWaitMs = requestedWaitMs;
		}
		if (!(requestedWaitMs > 0.0) || !std::isfinite(requestedWaitMs)) return;

		if (policy.strategy == FramePacingWaitStrategy::SleepOnly && !measurement)
		{
			FramePacingSleepForMilliseconds(requestedWaitMs);
			return;
		}

		ULONG64 cycleStart = 0;
		bool cycleStartAvailable = measurement
			&& QueryThreadCycleTime(GetCurrentThread(), &cycleStart) != FALSE;
		const std::int64_t frequency = FramePacingQpcFrequency();
		LARGE_INTEGER startCounter{};
		QueryPerformanceCounter(&startCounter);

		if (policy.strategy == FramePacingWaitStrategy::SleepOnly)
		{
			FramePacingSleepForMilliseconds(requestedWaitMs);
			LARGE_INTEGER endCounter{};
			QueryPerformanceCounter(&endCounter);
			measurement->waitBackend = FramePacingWaitBackend::SleepOnly;
			measurement->sleepMs = FramePacingTicksToMilliseconds(
				endCounter.QuadPart - startCounter.QuadPart, frequency);
			measurement->elapsedMs = measurement->sleepMs;
			measurement->signedOvershootMs = measurement->elapsedMs - requestedWaitMs;
			FramePacingFinishCycleMeasurement(measurement, cycleStartAvailable, cycleStart);
			return;
		}

		const bool waitableTimer = policy.strategy == FramePacingWaitStrategy::WaitableTimer;
		const bool adaptive = policy.strategy == FramePacingWaitStrategy::AdaptiveSpinTail;
		double spinTailMs = adaptive
			? FramePacingAdaptiveTail(policy, adaptiveState)
			: FramePacingFixedTail(policy);
		const std::int64_t targetEndTick = FramePacingTargetEndTick(
			startCounter.QuadPart, requestedWaitMs, frequency);
		LARGE_INTEGER spinStartCounter = startCounter;
		double adaptiveSleepRequestedMs = 0.0;
		double adaptiveSleepElapsedMs = 0.0;
		bool adaptiveSleepObserved = false;

		if (waitableTimer)
		{
			const double timerRequestedMs = requestedWaitMs - spinTailMs;
			if (timerRequestedMs > 0.0)
			{
				LARGE_INTEGER waitStartCounter{};
				QueryPerformanceCounter(&waitStartCounter);
				const FramePacingTimerWaitOutcome outcome =
					FramePacingGetThreadTimer().Wait(timerRequestedMs);
				LARGE_INTEGER waitEndCounter{};
				QueryPerformanceCounter(&waitEndCounter);
				spinStartCounter = waitEndCounter;
				if (measurement)
				{
					measurement->waitMs = FramePacingTicksToMilliseconds(
						waitEndCounter.QuadPart - waitStartCounter.QuadPart, frequency);
					measurement->waitableTimerResult = outcome.result;
					measurement->waitBackend = outcome.backend;
					measurement->attemptedWaitableTimerBackend = outcome.backend;
					measurement->waitableTimerFallback = outcome.compatibilityFallback;
				}

				if (outcome.result != FramePacingWaitableTimerResult::Completed)
				{
					// timer 操作失败时只补足当前帧剩余时间，避免重复等待整段时长。
					const double remainingMs = (std::max)(0.0,
						FramePacingTicksToMilliseconds(
							targetEndTick - waitEndCounter.QuadPart, frequency));
					FramePacingMeasurement fallbackMeasurement{};
					WaitForRemainingFrameTime(
						remainingMs,
						FramePacingWaitPolicy::Legacy(),
						nullptr,
						measurement ? &fallbackMeasurement : nullptr);
					if (!measurement) return;

					LARGE_INTEGER endCounter{};
					QueryPerformanceCounter(&endCounter);
					measurement->sleepMs = fallbackMeasurement.sleepMs;
					measurement->spinMs = fallbackMeasurement.spinMs;
					measurement->elapsedMs = FramePacingTicksToMilliseconds(
						endCounter.QuadPart - startCounter.QuadPart, frequency);
					measurement->signedOvershootMs =
						measurement->elapsedMs - requestedWaitMs;
					measurement->waitBackend = FramePacingWaitBackend::Legacy;
					measurement->waitableTimerFallback = true;
					FramePacingFinishCycleMeasurement(
						measurement, cycleStartAvailable, cycleStart);
					return;
				}
			}
		}

		// 与旧实现一致：只有严格大于 2 ms 才先睡眠，等于 2 ms 仍全程自旋。
		if (!waitableTimer && requestedWaitMs > FramePacingSleepGateMs)
		{
			if (measurement) measurement->waitBackend = FramePacingWaitBackend::Legacy;
			double sleepRequestedMs = requestedWaitMs - spinTailMs;
			LARGE_INTEGER sleepStartCounter{};
			if (measurement || adaptive) QueryPerformanceCounter(&sleepStartCounter);
			FramePacingSleepForMilliseconds(sleepRequestedMs);
			if (measurement || adaptive)
			{
				QueryPerformanceCounter(&spinStartCounter);
				double sleepElapsedMs = FramePacingTicksToMilliseconds(
					spinStartCounter.QuadPart - sleepStartCounter.QuadPart, frequency);
				if (measurement) measurement->sleepMs = sleepElapsedMs;
				if (adaptive)
				{
					adaptiveSleepRequestedMs = sleepRequestedMs;
					adaptiveSleepElapsedMs = sleepElapsedMs;
					adaptiveSleepObserved = true;
				}
			}
		}

		// QPC 后执行一次处理器 yield，保留旧 ARM64/x86/x64 忙等待序列。
		LARGE_INTEGER currentCounter{};
		do
		{
			QueryPerformanceCounter(&currentCounter);
			YieldProcessor();
		} while (currentCounter.QuadPart < targetEndTick);

		LARGE_INTEGER endCounter = currentCounter;
		if (measurement) QueryPerformanceCounter(&endCounter);
		if (adaptiveSleepObserved)
			FramePacingUpdateAdaptiveTail(
				policy,
				adaptiveState,
				adaptiveSleepRequestedMs,
				adaptiveSleepElapsedMs);
		if (!measurement) return;
		if (!waitableTimer && measurement->waitBackend == FramePacingWaitBackend::None)
			measurement->waitBackend = FramePacingWaitBackend::Legacy;
		measurement->spinMs = FramePacingTicksToMilliseconds(
			endCounter.QuadPart - spinStartCounter.QuadPart, frequency);
		measurement->elapsedMs = FramePacingTicksToMilliseconds(
			endCounter.QuadPart - startCounter.QuadPart, frequency);
		measurement->signedOvershootMs = measurement->elapsedMs - requestedWaitMs;
		FramePacingFinishCycleMeasurement(measurement, cycleStartAvailable, cycleStart);
	}

	void HighPrecisionWait(double frameTimeSpentMs, double targetFramesPerSecond)
	{
		if (!(targetFramesPerSecond > 0.0)
			|| !std::isfinite(targetFramesPerSecond)
			|| !std::isfinite(frameTimeSpentMs))
			return;
		WaitForRemainingFrameTime(1000.0 / targetFramesPerSecond - frameTimeSpentMs);
	}
}
