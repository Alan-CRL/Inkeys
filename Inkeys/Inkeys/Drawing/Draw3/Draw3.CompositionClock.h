#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	// DWM 合成时钟模型与提交时机判定：纯逻辑，不调用 DWM/D3D，时间统一为 QPC 毫秒。
	// 时钟只回答“下一次垂直同步（DWM 合成）在何时”，与呈现后端无关，阶段 D/E 可复用。
	inline constexpr double kCompositionClockRereadMs = 250.0;
	inline constexpr double kCompositionClockMinPeriodMs = 1.0;
	inline constexpr double kCompositionClockMaxPeriodMs = 100.0;
	// 提交需在垂直同步前这么久完成，给 DWM 取内容留余量；初始值，实测调整。
	inline constexpr double kCompositionDeadlineMarginMs = 2.0;
	// 相位微调每帧最多移动帧间隔的 10%，避免帧节奏突变。
	inline constexpr double kCompositionPhaseStepRatio = 0.10;
	// 预设帧间隔与刷新周期整数倍的相对误差在 3% 内视为约数（含 59.94/60 这类）。
	inline constexpr double kCompositionDivisorTolerance = 0.03;
	// “下一帧来提交是否还赶得上”判定时为帧耗时抖动多留 25% 帧间隔。
	inline constexpr double kCompositionNextFrameSlackRatio = 0.25;
	inline constexpr double kCommitEstimateEmaAlpha = 0.25;
	inline constexpr std::size_t kCommitEstimateWindow = 16;
	// 还没有实测样本时宁可高估提交耗时。
	inline constexpr double kCommitEstimateDefaultMs = 4.0;

	struct CompositionTimingSample
	{
		bool valid = false;     // DWM 正在合成且读取成功
		double vblankMs = 0.0;  // 某一次垂直同步的时刻（可在过去或未来）
		double periodMs = 0.0;  // 刷新周期
	};

	class CompositionClock
	{
	public:
		// 启动、DWM 合成状态变化、Resize/显示器变化时调用，下一次查询立即重读。
		void Invalidate() noexcept { sampled_ = false; }

		// 约每 250 ms 重读一次校正漂移（也跟随动态刷新率），不逐帧调用 DWM。
		bool NeedsRefresh(double nowMs) const noexcept
		{
			return !sampled_ || !(nowMs - sampledAtMs_ >= 0.0 && nowMs - sampledAtMs_ < kCompositionClockRereadMs);
		}

		void Update(const CompositionTimingSample& sample, double nowMs) noexcept
		{
			sampled_ = true;
			sampledAtMs_ = nowMs;
			// 读取失败、DWM 未合成或周期不合理时时钟失效，调用方退回简单间隔节流。
			valid_ = sample.valid && std::isfinite(sample.vblankMs) && std::isfinite(sample.periodMs) &&
				sample.periodMs >= kCompositionClockMinPeriodMs && sample.periodMs <= kCompositionClockMaxPeriodMs;
			if (!valid_) return;
			vblankMs_ = sample.vblankMs;
			periodMs_ = sample.periodMs;
		}

		bool Valid() const noexcept { return valid_; }
		double PeriodMs() const noexcept { return valid_ ? periodMs_ : 0.0; }

		// 严格晚于 timeMs 的下一次垂直同步。
		double NextVBlankMs(double timeMs) const noexcept
		{
			if (!valid_) return timeMs;
			const double cycles = std::floor((timeMs - vblankMs_) / periodMs_) + 1.0;
			const double next = vblankMs_ + cycles * periodMs_;
			return next > timeMs ? next : next + periodMs_; // 浮点舍入时保证严格晚于
		}

		// 不早于 timeMs 的最早提交截止时刻（垂直同步减去余量）。
		double DeadlineAtOrAfter(double timeMs, double marginMs = kCompositionDeadlineMarginMs) const noexcept
		{
			if (!valid_) return timeMs;
			// 减去极小容差：timeMs 恰好落在截止上时浮点舍入不能把它推到下一个周期。
			const double cycles = std::ceil((timeMs + marginMs - vblankMs_) / periodMs_ - 1e-9);
			return vblankMs_ + cycles * periodMs_ - marginMs;
		}

	private:
		bool sampled_ = false;
		bool valid_ = false;
		double sampledAtMs_ = 0.0;
		double vblankMs_ = 0.0;
		double periodMs_ = 0.0;
	};

	// 近期提交耗时（拷贝入队 → ULW 返回）的估计：EMA 与最近 16 次最大值取较大者，宁可高估。
	class CommitDurationEstimator
	{
	public:
		void Reset() noexcept { *this = {}; }

		void Add(double milliseconds) noexcept
		{
			if (!std::isfinite(milliseconds) || milliseconds < 0.0) return;
			emaMs_ = count_ == 0 ? milliseconds : emaMs_ + (milliseconds - emaMs_) * kCommitEstimateEmaAlpha;
			window_[next_] = milliseconds;
			next_ = (next_ + 1) % window_.size();
			if (count_ < window_.size()) ++count_;
		}

		bool HasSamples() const noexcept { return count_ != 0; }

		double EstimateMs() const noexcept
		{
			if (count_ == 0) return kCommitEstimateDefaultMs;
			double recentMax = 0.0;
			for (std::size_t index = 0; index < count_; ++index) recentMax = (std::max)(recentMax, window_[index]);
			return (std::max)(emaMs_, recentMax);
		}

	private:
		std::array<double, kCommitEstimateWindow> window_{};
		std::size_t next_ = 0;
		std::size_t count_ = 0;
		double emaMs_ = 0.0;
	};

	// 预设帧率与刷新率的关系决定提交节奏。
	enum class CompositionCadence : std::uint8_t
	{
		CommitEveryFrame, // 预设低于刷新率且非约数：每帧立即提交，不调相位
		ThrottleToRefresh, // 预设高于刷新率：每个刷新周期最多提交一次
		PhaseLock,        // 预设等于刷新率或为其约数：每帧提交，可微调帧相位贴近截止
	};

	inline CompositionCadence ClassifyCompositionCadence(double frameIntervalMs, double refreshMs) noexcept
	{
		if (!(frameIntervalMs > 0.0) || !(refreshMs > 0.0) || !std::isfinite(frameIntervalMs) || !std::isfinite(refreshMs))
			return CompositionCadence::CommitEveryFrame;
		const double ratio = frameIntervalMs / refreshMs;
		if (ratio < 1.0 - kCompositionDivisorTolerance) return CompositionCadence::ThrottleToRefresh;
		const double multiple = std::round(ratio);
		return std::fabs(ratio - multiple) <= kCompositionDivisorTolerance
			? CompositionCadence::PhaseLock : CompositionCadence::CommitEveryFrame;
	}

	inline CompositionTimingSample DcompTimingSample(std::int64_t nextTicks, std::int64_t currentTicks,
		std::int64_t frequency, std::uint32_t numerator, std::uint32_t denominator) noexcept
	{
		if (frequency <= 0 || currentTicks <= 0 || nextTicks <= currentTicks || !numerator || !denominator) return {};
		const double periodMs = 1000.0 * denominator / numerator;
		if (periodMs < kCompositionClockMinPeriodMs || periodMs > kCompositionClockMaxPeriodMs) return {};
		return { true, static_cast<double>(nextTicks) * (1000.0 / frequency), periodMs };
	}

	inline double DcompPhaseAdjustMs(double plannedNextStartMs, double intervalMs, double workMs,
		const CompositionClock& clock) noexcept
	{
		if (!clock.Valid() || !std::isfinite(plannedNextStartMs) || !std::isfinite(intervalMs) ||
			!std::isfinite(workMs) || intervalMs <= 0.0 || workMs < 0.0 || workMs >= intervalMs) return 0.0;
		const double periodMs = clock.PeriodMs();
		double slotMs = periodMs;
		if (intervalMs < periodMs)
		{
			// 高帧率在一次刷新内保留多个等距子帧，只微调其相位。
			const double ratio = periodMs / intervalMs;
			const double slots = std::round(ratio);
			if (std::abs(ratio - slots) > kCompositionDivisorTolerance) return 0.0;
			slotMs /= slots;
		}
		else if (ClassifyCompositionCadence(intervalMs, periodMs) != CompositionCadence::PhaseLock) return 0.0;
		const double finishMs = plannedNextStartMs + workMs;
		const double shiftMs = std::remainder(clock.DeadlineAtOrAfter(finishMs) - finishMs, slotMs);
		const double limitMs = intervalMs * kCompositionPhaseStepRatio;
		return std::clamp(shiftMs, -limitMs, limitMs);
	}

	struct UlwCommitDecision
	{
		bool commit = true;
		double deadlineMs = 0.0; // 对齐时本次提交瞄准的截止时刻；未对齐为 0
	};

	// ULW 提交时机：每个刷新周期最多一次；DWM 合成且允许对齐时，选每个周期里最后一个赶得上截止的帧。
	class UlwCommitPlanner
	{
	public:
		void Reset() noexcept { *this = {}; }

		// 节流状态失效（Resize、输出切换、恢复、强制整帧提交后）：下一帧按“首次提交”处理。
		void ForgetLastCommit() noexcept
		{
			hasLastCommit_ = false;
			lastCommitMs_ = 0.0;
			lastDeadlineMs_ = 0.0;
			nextSlotMs_ = 0.0;
		}

		// Present 时判定本帧是否拷出提交。aligned=DWM 合成、对齐开启、即时模式且时钟有效。
		UlwCommitDecision Decide(double nowMs, double frameIntervalMs, double refreshMs,
			bool aligned, const CompositionClock& clock) const noexcept
		{
			UlwCommitDecision decision;
			if (ClassifyCompositionCadence(frameIntervalMs, refreshMs) != CompositionCadence::ThrottleToRefresh ||
				!hasLastCommit_)
			{
				if (aligned && clock.Valid())
					decision.deadlineMs = clock.DeadlineAtOrAfter(nowMs + estimator_.EstimateMs());
				return decision;
			}
			if (aligned && clock.Valid())
			{
				const double periodMs = clock.PeriodMs();
				const double estimateMs = estimator_.EstimateMs();
				// 本次提交能赶上的最早截止；上一次提交已占用的周期不再提交第二次。
				double deadlineMs = clock.DeadlineAtOrAfter(nowMs + estimateMs);
				if (deadlineMs < lastDeadlineMs_ + periodMs * 0.5)
					deadlineMs = clock.DeadlineAtOrAfter(lastDeadlineMs_ + periodMs * 0.5);
				decision.deadlineMs = deadlineMs;
				// 下一帧来提交也赶得上这个截止 → 本帧只合并脏区，让更新的那帧去提交。
				const double nextFrameFinishMs = nowMs + frameIntervalMs * (1.0 + kCompositionNextFrameSlackRatio) + estimateMs;
				// 安全网：时钟异常导致长时间不提交时强制提交。
				const bool overdue = !(nowMs - lastCommitMs_ < periodMs * 2.0);
				decision.commit = overdue || nextFrameFinishMs > deadlineMs;
				return decision;
			}
			// 简单间隔：提交槽位按刷新周期推进（保留余数，不逐次重新减容差），长期提交率不超过刷新率；
			// 半个帧间隔的容差只吸收帧间隔抖动，避免与帧间隔拍频。
			decision.commit = !(nowMs < nextSlotMs_ - frameIntervalMs * 0.5);
			return decision;
		}

		// refreshMs ≤ 0（空闲前收尾、输出切换等无帧时序的调用）时沿用上一次的刷新周期。
		// forced=true：绕过节流的强制提交（整帧、空闲前收尾、熔断收尾），总是从本次提交重新起算，不预支未来周期。
		void OnCommitIssued(double nowMs, double deadlineMs, double refreshMs = 0.0, bool forced = false) noexcept
		{
			if (std::isfinite(refreshMs) && refreshMs > 0.0) lastRefreshMs_ = refreshMs;
			// 普通提交保留槽位余数；落后超过一个周期（空闲、长帧之后）时从本次提交重新起算，不补发欠下的槽位。
			if (forced || !hasLastCommit_ || nowMs - nextSlotMs_ > lastRefreshMs_)
				nextSlotMs_ = nowMs;
			nextSlotMs_ += lastRefreshMs_;
			hasLastCommit_ = true;
			lastCommitMs_ = nowMs;
			lastDeadlineMs_ = deadlineMs;
		}

		// 本帧开始到拷贝入队的耗时、拷贝入队到 ULW 返回的耗时，分别用于相位估计。
		void OnFrameWorkMeasured(double milliseconds) noexcept
		{
			if (!std::isfinite(milliseconds) || milliseconds < 0.0) return;
			workEmaMs_ = hasWork_ ? workEmaMs_ + (milliseconds - workEmaMs_) * kCommitEstimateEmaAlpha : milliseconds;
			hasWork_ = true;
		}
		void OnCommitCompleted(double milliseconds) noexcept { estimator_.Add(milliseconds); }

		double CommitEstimateMs() const noexcept { return estimator_.EstimateMs(); }
		double WorkEstimateMs() const noexcept { return hasWork_ ? workEmaMs_ : 0.0; }

		// 预设等于刷新率或为其约数时，下一帧开始时间的建议调整量（正=多等，负=少等）：
		// 使“开始 + 工作耗时 + 提交耗时”落在最近的截止上，每帧最多移动帧间隔的 10%。
		double PhaseAdjustMs(double plannedNextStartMs, double frameIntervalMs, const CompositionClock& clock) const noexcept
		{
			if (!clock.Valid() || !hasWork_ || !(frameIntervalMs > 0.0) ||
				ClassifyCompositionCadence(frameIntervalMs, clock.PeriodMs()) != CompositionCadence::PhaseLock) return 0.0;
			const double finishMs = plannedNextStartMs + workEmaMs_ + estimator_.EstimateMs();
			const double nextDeadlineMs = clock.DeadlineAtOrAfter(finishMs);
			const double previousDeadlineMs = nextDeadlineMs - clock.PeriodMs();
			const double delayMs = nextDeadlineMs - finishMs;     // 推迟开始：输入读得更晚，上屏时刻不变
			const double advanceMs = finishMs - previousDeadlineMs; // 提前开始：赶上前一个截止
			const double shiftMs = delayMs <= advanceMs ? delayMs : -advanceMs;
			const double limitMs = frameIntervalMs * kCompositionPhaseStepRatio;
			return std::clamp(shiftMs, -limitMs, limitMs);
		}

	private:
		CommitDurationEstimator estimator_;
		bool hasLastCommit_ = false;
		double lastCommitMs_ = 0.0;
		double lastDeadlineMs_ = 0.0;
		double nextSlotMs_ = 0.0;
		double lastRefreshMs_ = 1000.0 / 60.0;
		bool hasWork_ = false;
		double workEmaMs_ = 0.0;
	};
}
