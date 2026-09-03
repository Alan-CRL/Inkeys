module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

export module Inkeys.UI.StartupPreview.State;

export namespace Inkeys::UI::StartupPreview
{
	enum class CacheState : std::uint8_t
	{
		Missing,
		Valid,
		Incompatible,
		Corrupt,
	};

	enum class BarStartupState : std::uint8_t
	{
		NotStarted,
		Initializing,
		RenderClientRegistered,
		FirstFrameCommitted,
		WindowMissing,
		ClientRegistrationFailed,
		StartupFailed,
		StoppedBeforeReady,
	};

	[[nodiscard]] constexpr bool IsBarStartupReady(BarStartupState state) noexcept
	{
		return state == BarStartupState::FirstFrameCommitted;
	}

	[[nodiscard]] constexpr bool IsBarStartupFailure(BarStartupState state) noexcept
	{
		return state >= BarStartupState::WindowMissing;
	}

	enum class LifecycleState : std::uint8_t
	{
		Disabled,
		Preparing,
		ShowingEmbedded,
		ShowingValidCache,
		ShowingCorruptFallback,
		ShowingIncompatibleFallback,
		WaitingForBar,
		Handoff,
		FailurePending,
		FailureRedRequested,
		FailureRedCommitted,
		Bypassed,
		Stopping,
		Stopped,
	};

	enum class HandoffPath : std::uint8_t
	{
		None,
		ValidCacheProxySwap,
		EmbeddedProxyDeblur,
		CorruptFadeThroughTransparent,
	};

	struct StateTransition final
	{
		LifecycleState state = LifecycleState::Disabled;
		HandoffPath handoff = HandoffPath::None;
		bool accepted = false;
	};

	class StateMachine final
	{
	public:
		[[nodiscard]] StateTransition Start(
			bool enabled, CacheState cacheState, bool presentationAvailable) noexcept;
		[[nodiscard]] StateTransition PreviewFrameCommitted() noexcept;
		[[nodiscard]] StateTransition BarFrameCommitted() noexcept;
		[[nodiscard]] StateTransition FatalFailure() noexcept;
		[[nodiscard]] StateTransition RequestFailureFrame() noexcept;
		[[nodiscard]] StateTransition FailureFrameCommitted() noexcept;
		[[nodiscard]] StateTransition Bypass() noexcept;
		[[nodiscard]] StateTransition BeginStop() noexcept;
		[[nodiscard]] StateTransition FinishStop() noexcept;

		[[nodiscard]] LifecycleState State() const noexcept { return state_; }
		[[nodiscard]] CacheState Cache() const noexcept { return cacheState_; }
		[[nodiscard]] HandoffPath Handoff() const noexcept { return handoff_; }

	private:
		[[nodiscard]] StateTransition Result(bool accepted) const noexcept
		{
			return { state_, handoff_, accepted };
		}

		LifecycleState state_ = LifecycleState::Disabled;
		CacheState cacheState_ = CacheState::Missing;
		HandoffPath handoff_ = HandoffPath::None;
	};

	enum class ProgressVisualState : std::uint8_t
	{
		Hidden,
		FadingIn,
		Visible,
		FadingOut,
		Failure,
	};

	struct ProgressVisualSnapshot final
	{
		ProgressVisualState state = ProgressVisualState::Hidden;
		double displayedRatio = 0.0;
		double opacity = 0.0;
		bool red = false;
	};

	class ProgressVisualReducer final
	{
	public:
		explicit ProgressVisualReducer(
			std::chrono::steady_clock::time_point startTime) noexcept
			: startTime_(startTime) {}

		[[nodiscard]] ProgressVisualSnapshot Update(
			std::chrono::steady_clock::time_point now,
			double actualRatio,
			bool completed,
			bool failed) noexcept;

	private:
		std::chrono::steady_clock::time_point startTime_{};
		std::chrono::steady_clock::time_point transitionStart_{};
		double displayedRatio_ = 0.0;
		double fadeStartOpacity_ = 0.0;
		double opacity_ = 0.0;
		ProgressVisualState state_ = ProgressVisualState::Hidden;
	};

	struct HandoffFrameDecision final
	{
		bool useLiveProxy = false;
		double blurRatio = 1.0;
		double previewAlpha = 1.0;
		bool requestBarAlpha = false;
		std::uint8_t requestedBarAlpha = 0;
		bool stopPreview = false;
	};

	[[nodiscard]] bool CanBeginSuccessfulHandoff(
		bool barCommitted, bool trackerComplete, bool trackerFailed,
		ProgressVisualState progressState, bool proxyCurrentGeneration,
		bool ownerBoundsApplied) noexcept;
	[[nodiscard]] HandoffFrameDecision ResolveHandoffFrame(
		HandoffPath path, std::chrono::milliseconds elapsed,
		std::uint8_t committedBarAlpha) noexcept;

	struct HandoffRecoveryDecision final
	{
		bool requestOpaqueBar = false;
		bool stopPreview = false;
	};

	class HandoffFailureReducer final
	{
	public:
		void ObserveFailure(std::chrono::steady_clock::time_point now,
			bool revealEligible) noexcept;
		void ObserveSuccess() noexcept;
		[[nodiscard]] HandoffRecoveryDecision Poll(
			std::chrono::steady_clock::time_point now, bool revealEligible,
			bool ownerAvailable, std::uint8_t committedBarAlpha) noexcept;
		void Reset() noexcept;

	private:
		std::chrono::steady_clock::time_point firstFailure_{};
		bool revealRequested_ = false;
	};
}
