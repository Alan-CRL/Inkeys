#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	// waitable 交换链帧延迟句柄的应用侧状态机：不依赖 D3D/DXGI，等待与时钟由调用方注入。
	// 句柄是计数信号：等到一次即占用一个名额，只有成功 Present1 之后才由系统回补。
	enum class FrameLatencySignal : std::uint8_t { Signaled, TimedOut, Failed };
	enum class FrameLatencyWaitOutcome : std::uint8_t { Disabled, AlreadyHeld, Signaled, TimedOut, Failed };
	enum class FrameLatencyTripReason : std::uint8_t { None, SignalLost, WaitFailed };
	// Count：可计数状态，超时参与熔断；Exclude：隐藏/交接等排除状态，超时打断连续计数；
	// Neutral：帧循环外的补等，既不计数也不打断帧循环已有的连续计数。
	enum class FrameLatencyTimeoutPolicy : std::uint8_t { Count, Exclude, Neutral };

	inline constexpr double kFrameLatencyTimeoutScale = 1.5;
	inline constexpr double kFrameLatencyMinimumTimeoutMs = 4.0;
	inline constexpr double kFrameLatencyMaximumTimeoutMs = 200.0;
	inline constexpr double kFrameLatencyDefaultRefreshMs = 1000.0 / 60.0;
	inline constexpr double kFrameLatencyIntervalEmaAlpha = 0.25;
	inline constexpr std::uint32_t kFrameLatencyTripTimeouts = 8;
	inline constexpr double kFrameLatencyTripMinimumMs = 1000.0;

	struct FrameLatencyWaitResult
	{
		FrameLatencyWaitOutcome outcome = FrameLatencyWaitOutcome::Disabled;
		double waitedMs = 0.0;
		double timeoutMs = 0.0;
	};

	class FrameLatencyGate
	{
	public:
		// 新交换链取得句柄后启用；已熔断的会话不再启用。
		bool Enable() noexcept
		{
			Reset();
			if (tripped_) return false;
			enabled_ = true;
			return true;
		}

		// 交换链释放/重建：令牌、连续计数和节奏估计都属于旧句柄；熔断状态保留到会话结束。
		void Reset() noexcept
		{
			enabled_ = false;
			tokenHeld_ = false;
			waitAttempted_ = false;
			waitFailed_ = false;
			hasLastAcquire_ = false;
			consecutiveTimeouts_ = 0;
			streakStartMs_ = 0.0;
			lastTimeoutEndMs_ = 0.0;
			lastAcquireMs_ = 0.0;
			signalIntervalEmaMs_ = 0.0;
		}

		bool Enabled() const noexcept { return enabled_; }
		bool TokenHeld() const noexcept { return tokenHeld_; }
		bool Tripped() const noexcept { return tripped_; }
		FrameLatencyTripReason TripReason() const noexcept { return tripReason_; }
		std::uint32_t ConsecutiveTimeouts() const noexcept { return consecutiveTimeouts_; }
		double SignalIntervalEmaMs() const noexcept { return signalIntervalEmaMs_; }
		std::uint64_t TotalAcquired() const noexcept { return totalAcquired_; }
		std::uint64_t TotalTimeouts() const noexcept { return totalTimeouts_; }

		// clamp(1.5 × max(预设帧间隔, 刷新间隔, 信号间隔 EMA), 4, 200)。
		// GPU 慢时信号间隔变长，超时随之放宽，等待照常成功而不是越排越多。
		static double ComputeTimeoutMs(double targetFrameMs, double refreshMs, double signalIntervalEmaMs) noexcept
		{
			const auto usable = [](double value) noexcept { return std::isfinite(value) && value > 0.0 ? value : 0.0; };
			const double base = (std::max)({ usable(targetFrameMs), usable(refreshMs), usable(signalIntervalEmaMs) });
			return std::clamp(base * kFrameLatencyTimeoutScale,
				kFrameLatencyMinimumTimeoutMs, kFrameLatencyMaximumTimeoutMs);
		}

		double TimeoutMs(double targetFrameMs, double refreshMs) const noexcept
		{
			return ComputeTimeoutMs(targetFrameMs, refreshMs, signalIntervalEmaMs_);
		}

		// 未持有令牌时才等待一次。nowMs() 返回单调毫秒；wait(timeoutMs) 返回 FrameLatencySignal。
		template<class Clock, class Wait>
		FrameLatencyWaitResult Acquire(double targetFrameMs, double refreshMs,
			FrameLatencyTimeoutPolicy policy, Clock&& nowMs, Wait&& wait)
		{
			FrameLatencyWaitResult result;
			if (!enabled_) return result;
			if (tokenHeld_)
			{
				result.outcome = FrameLatencyWaitOutcome::AlreadyHeld;
				return result;
			}
			result.timeoutMs = TimeoutMs(targetFrameMs, refreshMs);
			const double startMs = nowMs();
			const FrameLatencySignal signal = wait(static_cast<std::uint32_t>(std::ceil(result.timeoutMs)));
			const double endMs = nowMs();
			const double waitedMs = endMs - startMs;
			result.waitedMs = std::isfinite(waitedMs) && waitedMs > 0.0 ? waitedMs : 0.0;
			waitAttempted_ = true;
			switch (signal)
			{
			case FrameLatencySignal::Signaled:
				result.outcome = FrameLatencyWaitOutcome::Signaled;
				tokenHeld_ = true;
				consecutiveTimeouts_ = 0;
				++totalAcquired_;
				if (hasLastAcquire_)
				{
					// 超过超时上限的间隔来自空闲（期间没有 Present），不是合成节奏，不进入估计。
					const double interval = endMs - lastAcquireMs_;
					if (std::isfinite(interval) && interval > 0.0 && interval <= kFrameLatencyMaximumTimeoutMs)
						signalIntervalEmaMs_ = signalIntervalEmaMs_ > 0.0
							? signalIntervalEmaMs_ + kFrameLatencyIntervalEmaAlpha * (interval - signalIntervalEmaMs_)
							: interval;
				}
				lastAcquireMs_ = endMs;
				hasLastAcquire_ = true;
				break;
			case FrameLatencySignal::TimedOut:
				result.outcome = FrameLatencyWaitOutcome::TimedOut;
				++totalTimeouts_;
				if (policy == FrameLatencyTimeoutPolicy::Count)
				{
					if (consecutiveTimeouts_ == 0) streakStartMs_ = startMs;
					++consecutiveTimeouts_;
					lastTimeoutEndMs_ = endMs;
				}
				else if (policy == FrameLatencyTimeoutPolicy::Exclude) consecutiveTimeouts_ = 0;
				break;
			default:
				// 句柄本身失效：不再等待，交由调用方熔断。
				result.outcome = FrameLatencyWaitOutcome::Failed;
				waitFailed_ = true;
				break;
			}
			return result;
		}

		// 只有实际调用了 Present1 才通知；成功才消耗令牌，失败或未调用时令牌保留。
		void OnPresentCalled(bool succeeded) noexcept
		{
			waitAttempted_ = false;
			if (succeeded) tokenHeld_ = false;
		}

		// 帧循环外的 Present：没有令牌且自上次 Present 以来没等过时，需要先补等一次。
		bool NeedsWaitBeforePresent() const noexcept
		{
			return enabled_ && !tokenHeld_ && !waitAttempted_;
		}

		// 信号迟到（GPU 慢）会被成功获取清零；只有可计数状态下连续超时且持续足够久才判定信号丢失。
		bool ShouldTrip() const noexcept
		{
			if (!enabled_) return false;
			if (waitFailed_) return true;
			return consecutiveTimeouts_ >= kFrameLatencyTripTimeouts &&
				lastTimeoutEndMs_ - streakStartMs_ >= kFrameLatencyTripMinimumMs;
		}

		// 熔断：本会话不再启用；调用方随后重建为不带 waitable 标志的交换链。
		void Trip() noexcept
		{
			tripReason_ = waitFailed_ ? FrameLatencyTripReason::WaitFailed : FrameLatencyTripReason::SignalLost;
			tripped_ = true;
			enabled_ = false;
			tokenHeld_ = false;
			waitAttempted_ = false;
		}

	private:
		bool enabled_ = false;
		bool tokenHeld_ = false;
		bool waitAttempted_ = false;
		bool waitFailed_ = false;
		bool hasLastAcquire_ = false;
		bool tripped_ = false;
		FrameLatencyTripReason tripReason_ = FrameLatencyTripReason::None;
		std::uint32_t consecutiveTimeouts_ = 0;
		double streakStartMs_ = 0.0;
		double lastTimeoutEndMs_ = 0.0;
		double lastAcquireMs_ = 0.0;
		double signalIntervalEmaMs_ = 0.0;
		std::uint64_t totalAcquired_ = 0;
		std::uint64_t totalTimeouts_ = 0;
	};
}
