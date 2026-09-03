module;

#include <algorithm>
#include <chrono>
#include <cmath>

module Inkeys.UI.StartupPreview.State;

namespace Inkeys::UI::StartupPreview
{
	StateTransition StateMachine::Start(bool enabled, CacheState cacheState,
		bool presentationAvailable) noexcept
	{
		if (state_ != LifecycleState::Disabled) return Result(false);
		cacheState_ = cacheState;
		if (!enabled)
		{
			state_ = LifecycleState::Stopped;
			return Result(true);
		}
		if (!presentationAvailable)
		{
			state_ = LifecycleState::Bypassed;
			return Result(true);
		}
		state_ = LifecycleState::Preparing;
		switch (cacheState_)
		{
		case CacheState::Valid:
			state_ = LifecycleState::ShowingValidCache;
			handoff_ = HandoffPath::ValidCacheProxySwap;
			break;
		case CacheState::Corrupt:
			state_ = LifecycleState::ShowingCorruptFallback;
			handoff_ = HandoffPath::CorruptFadeThroughTransparent;
			break;
		case CacheState::Incompatible:
			state_ = LifecycleState::ShowingIncompatibleFallback;
			handoff_ = HandoffPath::EmbeddedProxyDeblur;
			break;
		case CacheState::Missing:
		default:
			state_ = LifecycleState::ShowingEmbedded;
			handoff_ = HandoffPath::EmbeddedProxyDeblur;
			break;
		}
		return Result(true);
	}

	StateTransition StateMachine::PreviewFrameCommitted() noexcept
	{
		const bool showing = state_ == LifecycleState::ShowingEmbedded
			|| state_ == LifecycleState::ShowingValidCache
			|| state_ == LifecycleState::ShowingCorruptFallback
			|| state_ == LifecycleState::ShowingIncompatibleFallback;
		if (!showing) return Result(false);
		state_ = LifecycleState::WaitingForBar;
		return Result(true);
	}

	StateTransition StateMachine::BarFrameCommitted() noexcept
	{
		if (state_ != LifecycleState::WaitingForBar) return Result(false);
		state_ = LifecycleState::Handoff;
		return Result(true);
	}

	StateTransition StateMachine::FatalFailure() noexcept
	{
		if (state_ == LifecycleState::Stopped || state_ == LifecycleState::Stopping
			|| state_ == LifecycleState::Bypassed
			|| state_ == LifecycleState::Disabled) return Result(false);
		state_ = LifecycleState::FailurePending;
		handoff_ = HandoffPath::None;
		return Result(true);
	}

	StateTransition StateMachine::RequestFailureFrame() noexcept
	{
		if (state_ != LifecycleState::FailurePending) return Result(false);
		state_ = LifecycleState::FailureRedRequested;
		return Result(true);
	}

	StateTransition StateMachine::FailureFrameCommitted() noexcept
	{
		if (state_ != LifecycleState::FailureRedRequested) return Result(false);
		state_ = LifecycleState::FailureRedCommitted;
		return Result(true);
	}

	StateTransition StateMachine::Bypass() noexcept
	{
		if (state_ == LifecycleState::Stopped || state_ == LifecycleState::Stopping)
			return Result(false);
		state_ = LifecycleState::Bypassed;
		handoff_ = HandoffPath::None;
		return Result(true);
	}

	StateTransition StateMachine::BeginStop() noexcept
	{
		if (state_ == LifecycleState::Stopped || state_ == LifecycleState::Stopping)
			return Result(false);
		state_ = LifecycleState::Stopping;
		return Result(true);
	}

	StateTransition StateMachine::FinishStop() noexcept
	{
		if (state_ != LifecycleState::Stopping
			&& state_ != LifecycleState::Bypassed) return Result(false);
		state_ = LifecycleState::Stopped;
		return Result(true);
	}

	void ProgressVisualReducer::MarkPreviewShown(
		std::chrono::steady_clock::time_point now) noexcept
	{
		if (previewShownTime_ == std::chrono::steady_clock::time_point{})
			previewShownTime_ = now;
	}

	ProgressVisualSnapshot ProgressVisualReducer::Update(
		std::chrono::steady_clock::time_point now, double actualRatio,
		bool completed, bool failed) noexcept
	{
		actualRatio = std::clamp(actualRatio, 0.0, 1.0);
		// 显示值只追赶真实里程碑，不允许时间或动画虚构进度。
		displayedRatio_ = (std::min)(actualRatio,
			displayedRatio_ + (actualRatio - displayedRatio_) * 0.24);
		if (actualRatio < displayedRatio_) displayedRatio_ = actualRatio;

		if (failed)
		{
			state_ = ProgressVisualState::Failure;
			displayedRatio_ = actualRatio;
			opacity_ = 1.0;
			return { state_, displayedRatio_, opacity_, true };
		}

		constexpr auto ShowDelay = std::chrono::seconds(3);
		constexpr auto FadeIn = std::chrono::milliseconds(180);
		constexpr auto CompletionHold = std::chrono::milliseconds(300);
		constexpr auto FadeOut = std::chrono::milliseconds(140);
		if (completed)
		{
			if (state_ == ProgressVisualState::Hidden)
			{
				opacity_ = 0.0;
				return { state_, displayedRatio_, opacity_, false };
			}
			displayedRatio_ = 1.0;
			if (state_ != ProgressVisualState::Completing
				&& state_ != ProgressVisualState::FadingOut)
			{
				state_ = ProgressVisualState::Completing;
				transitionStart_ = now;
				opacity_ = 1.0;
			}
			if (state_ == ProgressVisualState::Completing)
			{
				if (now - transitionStart_ < CompletionHold)
					return { state_, displayedRatio_, opacity_, false };
				state_ = ProgressVisualState::FadingOut;
				transitionStart_ = now;
				fadeStartOpacity_ = opacity_;
			}
			const auto elapsed = now - transitionStart_;
			const double phase = std::clamp(
				std::chrono::duration<double>(elapsed).count() /
				std::chrono::duration<double>(FadeOut).count(), 0.0, 1.0);
			opacity_ = fadeStartOpacity_ * (1.0 - phase);
			if (phase >= 1.0) state_ = ProgressVisualState::Hidden;
			return { state_, displayedRatio_, opacity_, false };
		}

		if (previewShownTime_ == std::chrono::steady_clock::time_point{}
			|| now - previewShownTime_ < ShowDelay)
		{
			opacity_ = 0.0;
			return { ProgressVisualState::Hidden, displayedRatio_, opacity_, false };
		}
		if (state_ == ProgressVisualState::Hidden)
		{
			state_ = ProgressVisualState::FadingIn;
			transitionStart_ = now;
		}
		if (state_ == ProgressVisualState::FadingIn)
		{
			const double phase = std::clamp(
				std::chrono::duration<double>(now - transitionStart_).count() /
				std::chrono::duration<double>(FadeIn).count(), 0.0, 1.0);
			if (phase >= 1.0) state_ = ProgressVisualState::Visible;
			opacity_ = phase;
			return { state_, displayedRatio_, opacity_, false };
		}
		opacity_ = 1.0;
		return { state_, displayedRatio_, opacity_, false };
	}

	bool CanBeginSuccessfulHandoff(bool barCommitted, bool trackerComplete,
		bool trackerFailed, ProgressVisualState progressState,
		bool proxyCurrentGeneration, bool ownerBoundsApplied) noexcept
	{
		return barCommitted && trackerComplete && !trackerFailed
			&& progressState == ProgressVisualState::Hidden
			&& proxyCurrentGeneration && ownerBoundsApplied;
	}

	HandoffFrameDecision ResolveHandoffFrame(HandoffPath path,
		std::chrono::milliseconds elapsed, std::uint8_t committedBarAlpha) noexcept
	{
		const double milliseconds = (std::max)(0.0,
			static_cast<double>(elapsed.count()));
		HandoffFrameDecision decision;
		switch (path)
		{
		case HandoffPath::ValidCacheProxySwap:
			// 清晰 cache 与 live proxy 只在单帧边界切换，避免两次 SOURCE_OVER 增厚 alpha。
			decision.blurRatio = 0.0;
			decision.useLiveProxy = milliseconds >= 140.0;
			decision.requestBarAlpha = decision.useLiveProxy;
			decision.requestedBarAlpha = 255;
			break;
		case HandoffPath::CorruptFadeThroughTransparent:
			decision.previewAlpha = 1.0
				- std::clamp(milliseconds / 160.0, 0.0, 1.0);
			if (milliseconds >= 200.0)
			{
				decision.requestBarAlpha = true;
				decision.requestedBarAlpha = static_cast<std::uint8_t>(std::lround(
					255.0 * std::clamp((milliseconds - 200.0) / 160.0, 0.0, 1.0)));
			}
			break;
		case HandoffPath::EmbeddedProxyDeblur:
			decision.useLiveProxy = milliseconds >= 80.0;
			if (decision.useLiveProxy)
				decision.blurRatio = 1.0
					- std::clamp((milliseconds - 80.0) / 380.0, 0.0, 1.0);
			if (milliseconds >= 460.0)
			{
				decision.requestBarAlpha = true;
				decision.requestedBarAlpha = 255;
			}
			break;
		case HandoffPath::None:
		default:
			break;
		}
		decision.stopPreview = decision.requestBarAlpha
			&& decision.requestedBarAlpha == 255 && committedBarAlpha == 255;
		return decision;
	}

	void HandoffFailureReducer::ObserveFailure(
		std::chrono::steady_clock::time_point now, bool revealEligible) noexcept
	{
		if (!revealEligible || revealRequested_) return;
		if (firstFailure_ == std::chrono::steady_clock::time_point{})
			firstFailure_ = now;
	}

	void HandoffFailureReducer::ObserveSuccess() noexcept
	{
		if (!revealRequested_)
			firstFailure_ = std::chrono::steady_clock::time_point{};
	}

	HandoffRecoveryDecision HandoffFailureReducer::Poll(
		std::chrono::steady_clock::time_point now, bool revealEligible,
		bool ownerAvailable, std::uint8_t committedBarAlpha) noexcept
	{
		if (!revealEligible)
		{
			Reset();
			return {};
		}
		constexpr auto FailureLimit = std::chrono::milliseconds(750);
		if (!ownerAvailable)
			revealRequested_ = true;
		else if (firstFailure_ != std::chrono::steady_clock::time_point{}
			&& now - firstFailure_ >= FailureLimit)
			revealRequested_ = true;
		return { revealRequested_, revealRequested_ && committedBarAlpha == 255 };
	}

	void HandoffFailureReducer::Reset() noexcept
	{
		firstFailure_ = std::chrono::steady_clock::time_point{};
		revealRequested_ = false;
	}
}
