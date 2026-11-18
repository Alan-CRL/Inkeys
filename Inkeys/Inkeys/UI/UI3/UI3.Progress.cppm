module;

#include <chrono>
#include <cstdint>
#include <optional>

export module Inkeys.UI.UI3.Progress;

export namespace Inkeys::UI::UI3
{
	enum class HoldProgressCurve { EaseOutSine, EaseInOutCubic };
	enum class HoldProgressCompletion { FadeOut, KeepHint };
	struct HoldProgressConfig
	{
		// 时长非负；完成时长从延时结束起计。图形为 200+800，picker 为 500+1500 ms。
		std::chrono::milliseconds delay{200}, duration{800}, fadeIn{180}, fadeOut{140};
		HoldProgressCurve curve = HoldProgressCurve::EaseOutSine;
		HoldProgressCompletion completion = HoldProgressCompletion::FadeOut;
		bool resetProgressOnCancel = false; // 图形淡出保留弧长，picker 松手立即归零。
	};
	struct HoldProgressAnimation
	{
		// 不提供 dt 时按传入时间推进，目标变化当帧从零相位开始。
		// 提供 dt 时连同目标变化当帧一起推进，复现现有 UiPct/Value 的逐帧算法。
		std::optional<double> dtSeconds;
		double speedRate = 1.0;
		bool enabled = true;
	};
	struct HoldProgressFrame
	{
		// visibility 为提示整体透明度；KeepHint 完成后文字保留，环单独淡出，completedMix 变亮。
		double visibility = 0, ringVisibility = 0, progress = 0, completedMix = 0;
		bool justCompleted = false;
		bool changed = false, active = false; // AdvanceFeedback 的透明度推进结果。
	};
	struct HoldProgressSample
	{
		bool visible = false, due = false;
		double progress = 0;
		std::optional<std::chrono::steady_clock::time_point> nextWake;
	};
	class HoldProgressState
	{
	public:
		using Time = std::chrono::steady_clock::time_point;
		void Start(Time now, HoldProgressConfig config = {}) noexcept;
		void Cancel(Time now) noexcept;
		// 外部提交拒绝时传 false：保留最后采样弧长退场，不能假装提交成功而画满。
		void Complete(Time now, bool accepted = true) noexcept;
		HoldProgressFrame Advance(Time now, HoldProgressAnimation animation = {}) noexcept;
		// 只采样期限，不推进动画或自动完成；worker 与 render 可按原来的采样点分别消费。
		HoldProgressSample Sample(Time now) const noexcept;
		// 外部固定时钟/时间映射整体平移时，保留已采样反馈相位。
		void ShiftTime(Time::duration offset) noexcept { start_ += offset; lastTime_ += offset; }
		// 业务提交与反馈分属不同 owner 时，由调用方发布目标，仍由唯一 render owner 推进透明度。
		HoldProgressFrame AdvanceFeedback(Time now, bool visible, bool ringVisible, bool completed,
			HoldProgressAnimation animation = {}) noexcept;
		std::optional<Time> NextWake() const noexcept;
		void MarkResourceFailed() noexcept;
		// 设备换代只重置资源失败屏障，手势的起点、完成与淡出相位不受影响。
		bool SetDeviceGeneration(std::uint64_t generation) noexcept;
		bool Completed() const noexcept { return completed_; }
		bool ResourceFailed() const noexcept { return resourceFailed_; }
	private:
		struct Track
		{
			double value = 0, start = 0, phase = 1;
			bool target = false;
		};
		void Step(Track& track, bool target, std::chrono::milliseconds duration,
			double dt, const HoldProgressAnimation& animation) noexcept;
		HoldProgressFrame Frame(bool justCompleted) const noexcept;
		HoldProgressConfig config_;
		Time start_{}, lastTime_{};
		Track hint_, ring_, mix_;
		double progress_ = 0;
		std::uint64_t deviceGeneration_ = 0;
		bool holding_ = false, completed_ = false, resourceFailed_ = false, pendingCompletion_ = false;
	};
}
