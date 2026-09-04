module;

#include <chrono>
#include <cstdint>

export module Inkeys.UI.StartupPreview.State;

import Inkeys.UI.Bar.Metrics;

export namespace Inkeys::UI::StartupPreview
{
	inline constexpr double DefaultMainBarBodyWidthDip =
		BarDefaultMainBarLayoutWidthDip;
	inline constexpr double DefaultStartupBarWidthDip =
		BarDefaultStartupPreviewTotalWidthDip;
	inline constexpr double DefaultCachedStartupBarWidthDip =
		DefaultStartupBarWidthDip;
	inline constexpr bool StartupPreviewEnabledByDefault =
		StartupPreviewEnabledDefault;
	inline constexpr double CachedStartupBarWidthMinimumDip =
		StartupPreviewCachedWidthMinimumDip;
	inline constexpr double CachedStartupBarWidthMaximumDip =
		StartupPreviewCachedWidthMaximumDip;
	inline constexpr double CachedStartupBarWidthWriteEpsilonDip = 0.01;
	inline constexpr double StartupMainButtonToMainBarGapDip =
		BarMainButtonToMainBarGapDip;
	inline constexpr double DefaultStartupBarHeightDip = BarMainBarHeightDip;
	inline constexpr double DefaultStartupBarCornerRadiusDip =
		BarMainBarCornerRadiusDip;

	struct StartupPreviewPixelSize final
	{
		std::int32_t width = 0;
		std::int32_t height = 0;
	};

	[[nodiscard]] bool IsValidCachedStartupBarWidthDip(double widthDip) noexcept;
	[[nodiscard]] double ResolveCachedStartupBarWidthDip(double widthDip) noexcept;
	[[nodiscard]] bool ShouldWriteCachedStartupBarWidthDip(
		double currentWidthDip, double candidateWidthDip) noexcept;
	[[nodiscard]] double CalculateStartupBarTotalWidthDip(
		double mainButtonTargetWidthDip, double layoutTotalWidthDip,
		bool expanded = true) noexcept;
	[[nodiscard]] double ResolveStartupPreviewScale(double dpi,
		double barZoom) noexcept;
	[[nodiscard]] std::int32_t RoundStartupPreviewDipToPixels(double dip,
		double dpi, double barZoom) noexcept;
	[[nodiscard]] StartupPreviewPixelSize ResolveStartupPreviewPixelSize(
		double widthDip, double dpi, double barZoom) noexcept;

	struct GeometryRect final
	{
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;
	};

	struct ShimmerGradient final
	{
		double startX = 0.0;
		double startY = 0.0;
		double endX = 0.0;
		double endY = 0.0;
		double supportStart = 0.0;
		double supportEnd = 1.0;
	};

	struct ShimmerHorizontalTravel final
	{
		double startTranslationX = 0.0;
		double endTranslationX = 0.0;
		bool valid = false;
	};

	// 按完整非零尾光投影求左右离屏边界，不能用固定像素行程。
	[[nodiscard]] ShimmerHorizontalTravel ResolveShimmerHorizontalTravel(
		const GeometryRect& maskBounds, const ShimmerGradient& gradient,
		double outsideMargin = 1.0) noexcept;
	[[nodiscard]] bool IsShimmerSupportOutsideMask(
		const GeometryRect& maskBounds, const ShimmerGradient& gradient,
		double translationX) noexcept;
	[[nodiscard]] double ResolveShimmerCycleRatio(
		std::chrono::steady_clock::time_point now,
		std::chrono::steady_clock::time_point localEpoch,
		std::chrono::duration<double> period) noexcept;
	[[nodiscard]] double EaseShimmerPhase(double cycleRatio) noexcept;
	[[nodiscard]] double ResolveShimmerTranslationX(
		const ShimmerHorizontalTravel& travel, double easedPhase) noexcept;
	[[nodiscard]] std::uint8_t ResolveFadeInAlpha(
		std::chrono::milliseconds elapsed,
		std::chrono::milliseconds duration) noexcept;
	[[nodiscard]] std::uint8_t ResolveFadeOutAlpha(
		std::chrono::milliseconds elapsed,
		std::chrono::milliseconds duration) noexcept;
	[[nodiscard]] std::uint8_t ResolveFadeOutAlphaFrom(
		std::uint8_t startAlpha, std::chrono::milliseconds elapsed,
		std::chrono::milliseconds duration) noexcept;

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

	enum class HandoffState : std::uint8_t
	{
		Preparing,
		PreviewVisible,
		WaitingForBar,
		PreviewFadeOut,
		WaitingForBarOpaque,
		Stopped,
		Bypassed,
	};

	struct HandoffSnapshot final
	{
		HandoffState state = HandoffState::Preparing;
		bool requestPreviewFadeOut = false;
		bool requestBarFadeIn = false;
		bool destroyPreview = false;
	};

	class OrderedHandoffReducer final
	{
	public:
		[[nodiscard]] HandoffSnapshot PreviewFirstAlpha0Committed() noexcept;
		[[nodiscard]] HandoffSnapshot BarAlpha0Committed(bool startupComplete) noexcept;
		[[nodiscard]] HandoffSnapshot PreviewFadeOutCommitted() noexcept;
		[[nodiscard]] HandoffSnapshot BarAlpha255Committed() noexcept;
		[[nodiscard]] HandoffSnapshot Bypass() noexcept;
		[[nodiscard]] HandoffState State() const noexcept { return state_; }

	private:
		[[nodiscard]] HandoffSnapshot Result(bool fadePreview = false,
			bool fadeBar = false, bool destroy = false) const noexcept
		{
			return { state_, fadePreview, fadeBar, destroy };
		}

		HandoffState state_ = HandoffState::Preparing;
	};

	enum class ProgressVisualState : std::uint8_t
	{
		Hidden,
		FadingIn,
		Visible,
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
		void MarkPreviewShown(std::chrono::steady_clock::time_point now) noexcept;
		[[nodiscard]] ProgressVisualSnapshot Update(
			std::chrono::steady_clock::time_point now, double actualRatio,
			bool completed, bool failed) noexcept;

	private:
		std::chrono::steady_clock::time_point previewShownTime_{};
		std::chrono::steady_clock::time_point transitionStart_{};
		double displayedRatio_ = 0.0;
		ProgressVisualState state_ = ProgressVisualState::Hidden;
	};
}
