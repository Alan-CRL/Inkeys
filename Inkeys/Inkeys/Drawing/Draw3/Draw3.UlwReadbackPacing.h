#pragma once

#include <cmath>
#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	// ULW 异步读回的即时/流水自适应状态机：纯逻辑，不依赖 D3D，时间单位为 QPC 毫秒。
	// 即时模式：本帧帧尾阻塞完成读回并提交（延迟同同步路径）；
	// 流水模式：本帧的读回留到下一帧开头探测或提交背压时完成，CPU 最多领先 GPU 一帧。
	enum class UlwReadbackMode : std::uint8_t { Immediate, Pipelined };

	// 阈值是初始值，实机后可调。
	inline constexpr std::uint32_t kUlwPipelineEnterFrames = 3;
	inline constexpr double kUlwPipelineEnterWaitRatio = 0.25;
	inline constexpr double kUlwPipelineTrialIntervalMs = 500.0;
	inline constexpr double kUlwPipelineExitFrameRatio = 0.90;

	enum class UlwReadbackTransition : std::uint8_t { None, EnteredPipelined, ReturnedImmediate };

	class UlwReadbackPacing
	{
	public:
		void Reset() noexcept
		{
			mode_ = UlwReadbackMode::Immediate;
			slowStreak_ = 0;
			lastTrialMs_ = 0.0;
			trialFrame_ = false;
		}

		UlwReadbackMode Mode() const noexcept { return mode_; }
		bool TrialFrame() const noexcept { return trialFrame_; }

		// C3 帧尾决策（本帧有待完成读回时调用）：接下来进入空闲等待必须立即完成（最后一帧必须上屏）；
		// 固定帧截止等待时即时模式立即完成，流水模式只在试验帧（每 500 ms 一次）立即完成。
		bool ShouldCompleteAtFrameEnd(bool idleNext, double nowMs) noexcept
		{
			trialFrame_ = false;
			if (idleNext || mode_ == UlwReadbackMode::Immediate) return true;
			if (!(nowMs - lastTrialMs_ < kUlwPipelineTrialIntervalMs))
			{
				trialFrame_ = true;
				lastTrialMs_ = nowMs;
				return true;
			}
			return false;
		}

		// 每个有读回提交的帧结束后报告：frameMs 为本帧总耗时（含读回阻塞），budgetMs 为预设帧间隔，
		// mapWaitMs 为本帧帧尾阻塞完成的等待时长。没有读回的帧不调用，不改变状态。
		UlwReadbackTransition OnFrameMeasured(double frameMs, double budgetMs, double mapWaitMs, double nowMs) noexcept
		{
			if (!(budgetMs > 0.0) || !std::isfinite(frameMs) || !std::isfinite(mapWaitMs)) return UlwReadbackTransition::None;
			if (mode_ == UlwReadbackMode::Immediate)
			{
				// GPU 拖慢了这一帧：总耗时超预算，且阻塞等待占帧间隔的 25% 以上。
				const bool gpuBound = frameMs > budgetMs && mapWaitMs >= budgetMs * kUlwPipelineEnterWaitRatio;
				slowStreak_ = gpuBound ? slowStreak_ + 1 : 0;
				if (slowStreak_ < kUlwPipelineEnterFrames) return UlwReadbackTransition::None;
				mode_ = UlwReadbackMode::Pipelined;
				slowStreak_ = 0;
				lastTrialMs_ = nowMs;
				return UlwReadbackTransition::EnteredPipelined;
			}
			if (!trialFrame_) return UlwReadbackTransition::None;
			trialFrame_ = false;
			if (frameMs > budgetMs * kUlwPipelineExitFrameRatio) return UlwReadbackTransition::None;
			mode_ = UlwReadbackMode::Immediate;
			slowStreak_ = 0;
			return UlwReadbackTransition::ReturnedImmediate;
		}

	private:
		UlwReadbackMode mode_ = UlwReadbackMode::Immediate;
		std::uint32_t slowStreak_ = 0;
		double lastTrialMs_ = 0.0;
		bool trialFrame_ = false;
	};
}
