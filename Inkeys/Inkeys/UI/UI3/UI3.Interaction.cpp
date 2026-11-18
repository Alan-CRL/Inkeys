module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <cmath>
#include <optional>

module Inkeys.UI.UI3.Interaction;

namespace Inkeys::UI::UI3
{
	void PointerGestureRecognizer::Emit(GestureUpdate& result, GestureKind kind, Time time) const noexcept
	{
		result.events[result.count++] = {kind, target_, button_, time};
	}
	void PointerGestureRecognizer::EndContact() noexcept
	{
		contact_ = clickAllowed_ = holdActive_ = holdStarted_ = holdCompleted_ = false;
		secondClick_ = nativeContinuation_ = false; target_ = 0;
	}
	void PointerGestureRecognizer::Reset() noexcept
	{
		EndContact(); lastClicked_ = 0; lastClickDown_ = {};
	}
	void PointerGestureRecognizer::CancelContact() noexcept
	{
		// Main 保留最后一次成功按钮；原生双击续击不受中途失败点击/Leave 清空历史影响。
		if (config_.doubleClick == GestureDoubleClick::Native) EndContact();
		else Reset();
	}
	std::optional<PointerGestureRecognizer::Time> PointerGestureRecognizer::NextWake() const noexcept
	{
		// 双击历史在下次 Down 惰性检查，不为过期历史额外唤醒。
		if (!contact_ || !holdActive_ || holdCompleted_) return std::nullopt;
		return stableTime_ + config_.holdDelay + (holdStarted_ ? config_.holdDuration : std::chrono::milliseconds{0});
	}
	void PointerGestureRecognizer::AdvanceHold(GestureUpdate& result, Time now) noexcept
	{
		if (!contact_ || !holdActive_ || holdCompleted_) return;
		const auto begin = stableTime_ + config_.holdDelay;
		if (!holdStarted_ && now >= begin)
		{
			holdStarted_ = true;
			if (config_.consumeClickOnHoldStart) clickAllowed_ = false;
			Emit(result, GestureKind::LongPressStarted, begin);
		}
		const auto complete = begin + config_.holdDuration;
		if (holdStarted_ && now >= complete)
		{
			holdCompleted_ = true;
			Emit(result, GestureKind::LongPressCompleted, complete);
		}
	}
	void PointerGestureRecognizer::Activate(GestureUpdate& result, Time now) noexcept
	{
		if (button_ == GestureButton::Secondary) Emit(result, GestureKind::SecondaryClick, now);
		else
		{
			Emit(result, secondClick_ ? GestureKind::DoubleClick : GestureKind::Click, now);
			lastClicked_ = secondClick_ && config_.doubleClick != GestureDoubleClick::Native ? 0 : target_;
			lastClickDown_ = downTime_;
		}
	}
	GestureUpdate PointerGestureRecognizer::Advance(Time now) noexcept
	{
		GestureUpdate result; AdvanceHold(result, now); result.nextWake = NextWake(); return result;
	}
	GestureUpdate PointerGestureRecognizer::Update(const GesturePointerInput& input) noexcept
	{
		GestureUpdate result;
		if (input.kind == ButtonPointerEvent::Cancel || input.kind == ButtonPointerEvent::Leave)
		{
			if (contact_) Emit(result, GestureKind::Cancelled, input.time);
			CancelContact(); return result;
		}
		if (input.kind == ButtonPointerEvent::Down)
		{
			if (contact_) { Emit(result, GestureKind::Cancelled, input.time); CancelContact(); }
			const bool nativeSecond = config_.doubleClick == GestureDoubleClick::Native
				&& input.button == GestureButton::Primary && input.nativeDoubleClick && lastClicked_ != 0;
			if (!input.hit && !nativeSecond) return result;
			contact_ = clickAllowed_ = true; target_ = nativeSecond ? lastClicked_ : input.hit; button_ = input.button;
			downTime_ = stableTime_ = input.time; stableX_ = input.x; stableY_ = input.y;
			holdActive_ = config_.holdEnabled && button_ == GestureButton::Primary;
			const bool same = button_ == GestureButton::Primary && lastClicked_ == target_;
			nativeContinuation_ = same && config_.doubleClick == GestureDoubleClick::Native && input.nativeDoubleClick;
			secondClick_ = nativeContinuation_ || (same && config_.doubleClick == GestureDoubleClick::Interval
				&& input.time >= lastClickDown_ && input.time - lastClickDown_ <= config_.doubleClickInterval);
			const auto activation = button_ == GestureButton::Primary ? config_.activation : config_.secondaryActivation;
			if (activation == GestureActivation::OnPress) Activate(result, input.time);
			AdvanceHold(result, input.time);
		}
		else if (contact_ && (input.kind != ButtonPointerEvent::Up || input.button == button_))
		{
			const bool exited = config_.cancelOnHitExit && !nativeContinuation_ && input.hit != target_;
			if (exited && !(button_ == GestureButton::Primary && config_.activation == GestureActivation::OnPress))
			{
				Emit(result, GestureKind::Cancelled, input.time); CancelContact(); return result;
			}
			if (input.kind == ButtonPointerEvent::Move)
			{
				if (button_ == GestureButton::Primary && config_.activation == GestureActivation::OnPress && input.hit && input.hit != target_)
				{
					// 预设色块跨项即激活；穿过留白保留最后色块，回到同项不重复选择。
					if (holdActive_) Emit(result, GestureKind::Cancelled, input.time);
					target_ = input.hit; stableTime_ = input.time; stableX_ = input.x; stableY_ = input.y;
					holdStarted_ = holdCompleted_ = secondClick_ = false;
					holdActive_ = config_.holdEnabled && target_ != 0;
					if (target_) Activate(result, input.time);
				}
				const double dx = input.x - stableX_, dy = input.y - stableY_;
				const bool moved = config_.distance == GestureDistance::Axes
					? std::abs(dx) > config_.moveThreshold || std::abs(dy) > config_.moveThreshold
					: std::hypot(dx, dy) > config_.moveThreshold;
				if (holdActive_ && !holdCompleted_ && moved)
				{
					// 移动先于到期判定：picker 在该采样重置稳定点，不能先锁色再忽略移动。
					if (config_.consumeClickOnHoldStart && input.time >= stableTime_ + config_.holdDelay) clickAllowed_ = false;
					Emit(result, GestureKind::Cancelled, input.time);
					holdStarted_ = false;
					if (config_.movePolicy == GestureMovePolicy::RestartHold)
					{ stableTime_ = input.time; stableX_ = input.x; stableY_ = input.y; }
					else holdActive_ = false;
				}
				AdvanceHold(result, input.time);
			}
			else if (input.kind == ButtonPointerEvent::Up)
			{
				if (config_.advanceHoldOnRelease) AdvanceHold(result, input.time);
				const auto activation = button_ == GestureButton::Primary ? config_.activation : config_.secondaryActivation;
				if (clickAllowed_ && activation == GestureActivation::OnRelease) Activate(result, input.time);
				EndContact();
			}
		}
		result.nextWake = NextWake(); return result;
	}
}
