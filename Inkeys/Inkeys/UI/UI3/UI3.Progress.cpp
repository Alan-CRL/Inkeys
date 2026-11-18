module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

module Inkeys.UI.UI3.Progress;

namespace Inkeys::UI::UI3
{
	void HoldProgressState::Start(Time now, HoldProgressConfig config) noexcept
	{
		config_ = config; start_ = lastTime_ = now;
		holding_ = true; completed_ = resourceFailed_ = pendingCompletion_ = false; progress_ = 0;
		// 不抹掉上一轮退场透明度；再次开始时仍从当前值重定向。
	}
	void HoldProgressState::Cancel(Time now) noexcept
	{
		holding_ = false; pendingCompletion_ = false;
		if (config_.resetProgressOnCancel) progress_ = 0;
		(void)Advance(now, {0.0});
	}
	void HoldProgressState::Complete(Time now, bool accepted) noexcept
	{
		if (completed_ || !holding_ || resourceFailed_) return;
		completed_ = true; pendingCompletion_ = true;
		if (accepted) progress_ = 1;
		// 拒绝与图形完成均退场；picker 完成后保持文字，环单独退场。
		if (!accepted || config_.completion == HoldProgressCompletion::FadeOut) holding_ = false;
		lastTime_ = now;
	}
	void HoldProgressState::Step(Track& track, bool target, std::chrono::milliseconds duration,
		double dt, const HoldProgressAnimation& animation) noexcept
	{
		const bool retarget = track.target != target;
		if (retarget) { track.target = target; track.start = track.value; track.phase = 0; }
		const double seconds = std::chrono::duration<double>(duration).count();
		// Pct 的微小差值会直接归位；主栏 picker 的 Value（三次曲线）保留这些小数继续插值。
		const bool pctSettled = config_.curve == HoldProgressCurve::EaseOutSine
			&& std::abs((target ? 1.0 : 0.0) - track.start) <= .000001;
		if (!animation.enabled || seconds <= 0 || !std::isfinite(animation.speedRate) || animation.speedRate <= 0 || pctSettled)
		{ track.value = target ? 1 : 0; track.phase = 1; return; }
		if (track.phase >= 1) return;
		if (!retarget || animation.dtSeconds.has_value())
			track.phase = (std::clamp)(track.phase + (std::max)(0.0, dt) * animation.speedRate / seconds, 0.0, 1.0);
		const double t = track.phase;
		double eased;
		if (config_.curve == HoldProgressCurve::EaseOutSine) eased = std::sin(t * 3.14159265358979323846 / 2);
		else if (t < .5) eased = 4 * t * t * t;
		else { const double inverse = -2 * t + 2; eased = 1 - inverse * inverse * inverse / 2; }
		track.value = track.phase >= 1 ? (target ? 1 : 0)
			: (std::clamp)(track.start + ((target ? 1.0 : 0.0) - track.start) * eased, 0.0, 1.0);
	}
	HoldProgressFrame HoldProgressState::Frame(bool justCompleted) const noexcept
	{
		return {resourceFailed_ ? 0.0 : hint_.value, resourceFailed_ ? 0.0 : ring_.value,
			progress_, resourceFailed_ ? 0.0 : mix_.value, justCompleted};
	}
	HoldProgressFrame HoldProgressState::Advance(Time now, HoldProgressAnimation animation) noexcept
	{
		if (resourceFailed_) return Frame(false);
		const double dt = animation.dtSeconds.value_or(std::chrono::duration<double>(now - lastTime_).count());
		lastTime_ = now;
		bool visible = false;
		if (holding_ && !completed_)
		{
			const auto begin = start_ + config_.delay;
			visible = now >= begin;
			if (visible)
			{
				const double duration = std::chrono::duration<double>(config_.duration).count();
				progress_ = duration <= 0 ? 1 : (std::clamp)(std::chrono::duration<double>(now - begin).count() / duration, 0.0, 1.0);
				if (now >= begin + config_.duration) Complete(now);
			}
		}
		if (completed_) visible = holding_ && config_.completion == HoldProgressCompletion::KeepHint;
		Step(hint_, visible, visible ? config_.fadeIn : config_.fadeOut, dt, animation);
		const bool showRing = visible && !completed_;
		Step(ring_, showRing, showRing ? config_.fadeIn : config_.fadeOut, dt, animation);
		// 锁定文字在完成后才变亮，取消后仍按同一曲线回退。
		Step(mix_, visible && completed_, config_.fadeIn, dt, animation);
		const bool justCompleted = pendingCompletion_; pendingCompletion_ = false;
		return Frame(justCompleted);
	}
	HoldProgressSample HoldProgressState::Sample(Time now) const noexcept
	{
		if (!holding_ || completed_) return {};
		const auto begin = start_ + config_.delay, end = begin + config_.duration;
		const bool visible = now >= begin, due = now >= end;
		const double duration = std::chrono::duration<double>(config_.duration).count();
		const double progress = !visible ? 0 : duration <= 0 ? 1
			: (std::clamp)(std::chrono::duration<double>(now - begin).count() / duration, 0.0, 1.0);
		// 资源失败只屏蔽绘制，不能阻断业务侧的固定期限。
		return {visible, due, progress, due ? std::nullopt : std::optional<Time>{visible ? end : begin}};
	}
	HoldProgressFrame HoldProgressState::AdvanceFeedback(Time now, bool visible, bool ringVisible, bool completed,
		HoldProgressAnimation animation) noexcept
	{
		const double oldHint = hint_.value, oldRing = ring_.value, oldMix = mix_.value;
		const double dt = animation.dtSeconds.value_or(std::chrono::duration<double>(now - lastTime_).count());
		lastTime_ = now;
		Step(hint_, visible, visible ? config_.fadeIn : config_.fadeOut, dt, animation);
		Step(ring_, ringVisible, ringVisible ? config_.fadeIn : config_.fadeOut, dt, animation);
		Step(mix_, completed, config_.fadeIn, dt, animation);
		auto frame = Frame(false);
		frame.changed = oldHint != hint_.value || oldRing != ring_.value || oldMix != mix_.value;
		frame.active = hint_.phase < 1 || ring_.phase < 1 || mix_.phase < 1;
		return frame;
	}
	std::optional<HoldProgressState::Time> HoldProgressState::NextWake() const noexcept
	{
		if (resourceFailed_) return std::nullopt;
		if (pendingCompletion_) return lastTime_;
		const auto begin = start_ + config_.delay;
		const auto complete = begin + config_.duration;
		const bool fading = hint_.phase < 1 || ring_.phase < 1 || mix_.phase < 1;
		if (holding_ && !completed_ && lastTime_ < begin)
			return fading ? (std::min)(begin, lastTime_ + std::chrono::milliseconds(1)) : begin;
		// 可见进度/淡入淡出需要连续帧；以 1ms 表达最小推进量，宿主可合并到自身帧节拍。
		if (fading || (holding_ && !completed_))
		{
			const auto frame = lastTime_ + std::chrono::milliseconds(1);
			return holding_ && !completed_ ? (std::min)(frame, complete) : frame;
		}
		return std::nullopt;
	}
	void HoldProgressState::MarkResourceFailed() noexcept
	{
		// 本次反馈停止采样及唤醒；识别器/业务提交独立推进，不受绘制失败影响。
		resourceFailed_ = true;
	}
	bool HoldProgressState::SetDeviceGeneration(std::uint64_t generation) noexcept
	{
		if (deviceGeneration_ == generation) return false;
		deviceGeneration_ = generation; resourceFailed_ = false; return true;
	}
}
