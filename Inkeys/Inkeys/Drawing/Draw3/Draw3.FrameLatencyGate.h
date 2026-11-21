#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace Inkeys::Drawing::Draw3
{
	// waitable 交换链帧延迟句柄的应用侧状态机：不依赖 D3D/DXGI，等待与时钟由调用方注入。
	// 句柄是计数信号：等到一次即占用一个名额，只有成功 Present1 之后才由系统回补。
	enum class FrameLatencySignal : std::uint8_t { Signaled, TimedOut, Failed };
	enum class FrameLatencyWaitOutcome : std::uint8_t { Disabled, AlreadyHeld, Signaled, TimedOut, Failed };
	// QueueOverflow：GPU 忙期间欠账超过 kFrameLatencyMaxBusyDebt，多压的帧无法再记账补还，只能重建交换链清空队列。
	enum class FrameLatencyTripReason : std::uint8_t { None, SignalLost, WaitFailed, QueueOverflow };
	// Count：可计数状态，超时参与熔断；Exclude：隐藏/交接等排除状态，超时打断连续计数；
	// Neutral：帧循环外的补等，既不计数也不打断帧循环已有的连续计数。
	// GpuBusy：超时那一刻上一帧 GPU 工作仍未完成（被其他程序抢占 GPU），与 Neutral 一样不计数也不打断；
	// 此后无令牌 Present 的帧确实压在队列里、GPU 恢复后会逐个交还信号，欠账按更高上限如实记账并逐帧还清。
	enum class FrameLatencyTimeoutPolicy : std::uint8_t { Count, Exclude, Neutral, GpuBusy };

	inline constexpr double kFrameLatencyTimeoutScale = 1.5;
	inline constexpr double kFrameLatencyMinimumTimeoutMs = 4.0;
	inline constexpr double kFrameLatencyMaximumTimeoutMs = 200.0;
	inline constexpr double kFrameLatencyDefaultRefreshMs = 1000.0 / 60.0;
	inline constexpr double kFrameLatencyIntervalEmaAlpha = 0.25;
	inline constexpr std::uint32_t kFrameLatencyTripTimeouts = 8;
	inline constexpr double kFrameLatencyTripMinimumMs = 1000.0;
	// 欠账上限：信号真的不来（隐藏、交接等）时欠账不无限增长，补等最多拖慢一帧。
	inline constexpr std::uint32_t kFrameLatencyMaxDebt = 2;
	// Count/GpuBusy 超时后的欠账上限：饥饿期间多压的帧若按 2 封顶，超出部分永不补还，GPU 恢复后一直留在合成队列里，
	// 可见延迟持续偏高（实测 3 秒饥饿后 45 ms → 130–250 ms 不恢复；上限 16 时 7 秒饥饿仍溢出 5 帧，REALTIME 下约 +200 ms）。
	// 连续 GPU 忙超过 5 秒（kGpuBusyNeutralLimitMs）后超时重新计数并熔断重建、队列清空；5 秒内约 25 次超时 Present，取 32 留余量。
	inline constexpr std::uint32_t kFrameLatencyMaxBusyDebt = 32;

	struct FrameLatencyWaitResult
	{
		FrameLatencyWaitOutcome outcome = FrameLatencyWaitOutcome::Disabled;
		double waitedMs = 0.0; // 含补等耗时
		double timeoutMs = 0.0;
		bool debtRepaid = false;        // 本次额外等到一次信号，还掉一笔欠账
		bool debtRepayTimedOut = false; // 补等超时，欠账保留
		FrameLatencyTimeoutPolicy timeoutPolicy = FrameLatencyTimeoutPolicy::Count; // 仅主等待超时时有效：本次超时实际采用的策略
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
			busyTimeoutPending_ = false;
			largeDebtCapPending_ = false;
			busyQueueOverflow_ = false;
			debt_ = 0;
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
		std::uint32_t Debt() const noexcept { return debt_; }
		std::uint64_t TotalDebtOverflows() const noexcept { return totalDebtOverflows_; }

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
		// policy 可以是 FrameLatencyTimeoutPolicy，也可以是返回它的可调用对象：后者只在主等待超时后调用一次，
		// 用于按“超时那一刻”的状态（如上一帧 GPU 工作是否仍未完成）决定本次超时是否计入熔断。
		template<class Policy, class Clock, class Wait>
		FrameLatencyWaitResult Acquire(double targetFrameMs, double refreshMs,
			Policy&& policy, Clock&& nowMs, Wait&& wait)
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
				if (debt_ > 0)
				{
					// 还账：此前有帧没拿到名额就 Present，队列多压了一帧。拿到本帧名额后再多等一次信号，
					// 每帧最多还一笔，逐帧把在途帧数收回到上限。空闲后对应信号已积在句柄里，补等立即返回。
					// 补等成功不进入信号间隔估计、不计入超时与熔断；补等超时同样抬高估计，只保留欠账，本帧照常 Present。
					const double repayStartMs = nowMs();
					const FrameLatencySignal repay = wait(static_cast<std::uint32_t>(std::ceil(result.timeoutMs)));
					const double repayWaitedMs = nowMs() - repayStartMs;
					if (std::isfinite(repayWaitedMs) && repayWaitedMs > 0.0) result.waitedMs += repayWaitedMs;
					if (repay == FrameLatencySignal::Signaled)
					{
						--debt_;
						result.debtRepaid = true;
					}
					else if (repay == FrameLatencySignal::Failed)
					{
						// 句柄失效与主等待失败同样处理：调用方随即熔断，本帧跳过并按熔断重建为不带 waitable 的交换链。
						waitFailed_ = true;
					}
					else
					{
						result.debtRepayTimedOut = true;
						RaiseIntervalAfterTimeout(result.timeoutMs);
					}
				}
				break;
			case FrameLatencySignal::TimedOut:
				result.outcome = FrameLatencyWaitOutcome::TimedOut;
				++totalTimeouts_;
				RaiseIntervalAfterTimeout(result.timeoutMs);
				result.timeoutPolicy = ResolveTimeoutPolicy(std::forward<Policy>(policy));
				// Count（GPU 已空闲而信号未到）与 GpuBusy 之后无令牌 Present 的帧都确实压在队列里，按大上限如实记账；
				// 只有排除状态与帧外补等（Exclude/Neutral）沿用小上限。溢出即熔断只针对 GPU 忙。
				largeDebtCapPending_ = result.timeoutPolicy == FrameLatencyTimeoutPolicy::GpuBusy ||
					result.timeoutPolicy == FrameLatencyTimeoutPolicy::Count;
				busyTimeoutPending_ = result.timeoutPolicy == FrameLatencyTimeoutPolicy::GpuBusy;
				if (result.timeoutPolicy == FrameLatencyTimeoutPolicy::Count)
				{
					if (consecutiveTimeouts_ == 0) streakStartMs_ = startMs;
					++consecutiveTimeouts_;
					lastTimeoutEndMs_ = endMs;
				}
				else if (result.timeoutPolicy == FrameLatencyTimeoutPolicy::Exclude) consecutiveTimeouts_ = 0;
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
			// waitable 交换链的 Present 不阻塞：没拿到名额（超时）也成功 Present，队列就多一帧，记为欠账。
			// 已到上限时只计溢出次数（会话累计），用于实测确认上限是否够用。
			if (succeeded && enabled_ && !tokenHeld_)
			{
				const std::uint32_t cap = largeDebtCapPending_ ? kFrameLatencyMaxBusyDebt : kFrameLatencyMaxDebt;
				if (debt_ < cap) ++debt_;
				else
				{
					++totalDebtOverflows_;
					// 长时间“半饥饿”（偶有信号，5 秒忙计时被清零）可让欠账顶到忙上限：此后溢出的帧会永久留在队列里，
					// 要求熔断重建（与改动前饥饿即熔断的行为一致）。排除状态下的溢出只计数，保持原语义。
					if (busyTimeoutPending_) busyQueueOverflow_ = true;
				}
			}
			if (succeeded)
			{
				tokenHeld_ = false;
				busyTimeoutPending_ = false;
				largeDebtCapPending_ = false;
			}
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
			if (waitFailed_ || busyQueueOverflow_) return true;
			return consecutiveTimeouts_ >= kFrameLatencyTripTimeouts &&
				lastTimeoutEndMs_ - streakStartMs_ >= kFrameLatencyTripMinimumMs;
		}

		// 熔断：本会话不再启用；调用方随后重建为不带 waitable 标志的交换链。
		void Trip() noexcept
		{
			tripReason_ = waitFailed_ ? FrameLatencyTripReason::WaitFailed :
				busyQueueOverflow_ ? FrameLatencyTripReason::QueueOverflow : FrameLatencyTripReason::SignalLost;
			tripped_ = true;
			enabled_ = false;
			tokenHeld_ = false;
			waitAttempted_ = false;
			busyTimeoutPending_ = false;
			largeDebtCapPending_ = false;
			debt_ = 0;
		}

	private:
		template<class Policy>
		static FrameLatencyTimeoutPolicy ResolveTimeoutPolicy(Policy&& policy)
		{
			if constexpr (std::is_invocable_r_v<FrameLatencyTimeoutPolicy, Policy&&>) return std::forward<Policy>(policy)();
			else return static_cast<FrameLatencyTimeoutPolicy>(policy);
		}

		// 超时说明信号间隔至少有这么长：把间隔估计抬到本次超时值，下一次超时随之放宽 1.5 倍。
		// 原先只在等到信号时才更新估计，GPU 慢时开头会连续超时几十次，欠账溢出上限后无法还清，队列持续积压。
		// 等到信号后照常按 EMA 回落，超时放宽只影响信号迟迟不来时的等待上限，不增加正常帧的等待。
		void RaiseIntervalAfterTimeout(double timeoutMs) noexcept
		{
			if (std::isfinite(timeoutMs) && timeoutMs > signalIntervalEmaMs_)
				signalIntervalEmaMs_ = (std::min)(timeoutMs, kFrameLatencyMaximumTimeoutMs);
		}

		bool enabled_ = false;
		bool tokenHeld_ = false;
		bool waitAttempted_ = false;
		bool waitFailed_ = false;
		bool hasLastAcquire_ = false;
		bool busyTimeoutPending_ = false; // 最近一次主等待因 GPU 忙超时、尚未 Present
		bool largeDebtCapPending_ = false; // 最近一次主等待以 Count/GpuBusy 超时、尚未 Present：欠账按大上限记账
		bool busyQueueOverflow_ = false;  // GPU 忙期间欠账溢出，需熔断重建
		bool tripped_ = false;
		FrameLatencyTripReason tripReason_ = FrameLatencyTripReason::None;
		std::uint32_t consecutiveTimeouts_ = 0;
		std::uint32_t debt_ = 0; // 没拿到名额就成功 Present 的次数（上限 kFrameLatencyMaxDebt，GPU 忙超时后 kFrameLatencyMaxBusyDebt）
		double streakStartMs_ = 0.0;
		double lastTimeoutEndMs_ = 0.0;
		double lastAcquireMs_ = 0.0;
		double signalIntervalEmaMs_ = 0.0;
		std::uint64_t totalAcquired_ = 0;
		std::uint64_t totalTimeouts_ = 0;
		std::uint64_t totalDebtOverflows_ = 0; // 欠账已满仍无名额 Present 的次数，不随 Reset 清零
	};
}
