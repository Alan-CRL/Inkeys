module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

module Inkeys.UI.StartupPreview.State;

namespace Inkeys::UI::StartupPreview
{
	namespace
	{
		constexpr double DefaultDpi = 96.0;
		constexpr double MinimumBarZoom = 0.50;
		constexpr double MaximumBarZoom = 2.00;
		constexpr double ProjectionEpsilon = 0.000001;
		constexpr double Pi = 3.14159265358979323846;

		[[nodiscard]] bool IsFiniteRect(const GeometryRect& rect) noexcept
		{
			return std::isfinite(rect.left) && std::isfinite(rect.top)
				&& std::isfinite(rect.right) && std::isfinite(rect.bottom)
				&& rect.right > rect.left && rect.bottom > rect.top;
		}

		[[nodiscard]] bool TryResolveProjectionBounds(
			const GeometryRect& maskBounds, const ShimmerGradient& gradient,
			double& minimum, double& maximum,
			double& vectorX, double& lengthSquared) noexcept
		{
			if (!IsFiniteRect(maskBounds)
				|| !std::isfinite(gradient.startX)
				|| !std::isfinite(gradient.startY)
				|| !std::isfinite(gradient.endX)
				|| !std::isfinite(gradient.endY)
				|| !std::isfinite(gradient.supportStart)
				|| !std::isfinite(gradient.supportEnd)
				|| gradient.supportStart < 0.0
				|| gradient.supportEnd > 1.0
				|| gradient.supportStart >= gradient.supportEnd)
				return false;

			vectorX = gradient.endX - gradient.startX;
			const double vectorY = gradient.endY - gradient.startY;
			lengthSquared = vectorX * vectorX + vectorY * vectorY;
			if (!std::isfinite(lengthSquared) || lengthSquared <= ProjectionEpsilon
				|| std::abs(vectorX) <= ProjectionEpsilon)
				return false;

			const double minimumX = vectorX >= 0.0
				? maskBounds.left : maskBounds.right;
			const double maximumX = vectorX >= 0.0
				? maskBounds.right : maskBounds.left;
			const double minimumY = vectorY >= 0.0
				? maskBounds.top : maskBounds.bottom;
			const double maximumY = vectorY >= 0.0
				? maskBounds.bottom : maskBounds.top;
			minimum = (minimumX - gradient.startX) * vectorX
				+ (minimumY - gradient.startY) * vectorY;
			maximum = (maximumX - gradient.startX) * vectorX
				+ (maximumY - gradient.startY) * vectorY;
			return std::isfinite(minimum) && std::isfinite(maximum);
		}
	}

	bool IsValidCachedStartupBarWidthDip(double widthDip) noexcept
	{
		return std::isfinite(widthDip)
			&& widthDip >= CachedStartupBarWidthMinimumDip
			&& widthDip <= CachedStartupBarWidthMaximumDip;
	}

	double ResolveCachedStartupBarWidthDip(double widthDip) noexcept
	{
		return IsValidCachedStartupBarWidthDip(widthDip)
			? widthDip : DefaultStartupBarWidthDip;
	}

	bool ShouldWriteCachedStartupBarWidthDip(
		double currentWidthDip, double candidateWidthDip) noexcept
	{
		if (!IsValidCachedStartupBarWidthDip(candidateWidthDip)) return false;
		if (!IsValidCachedStartupBarWidthDip(currentWidthDip)) return true;
		return std::abs(candidateWidthDip - currentWidthDip)
			>= CachedStartupBarWidthWriteEpsilonDip;
	}

	double CalculateStartupBarTotalWidthDip(double mainButtonTargetWidthDip,
		double layoutTotalWidthDip, bool expanded) noexcept
	{
		if (!std::isfinite(mainButtonTargetWidthDip)
			|| mainButtonTargetWidthDip < CachedStartupBarWidthMinimumDip
			|| mainButtonTargetWidthDip > CachedStartupBarWidthMaximumDip)
			return DefaultStartupBarWidthDip;
		if (!expanded) return mainButtonTargetWidthDip;
		if (!std::isfinite(layoutTotalWidthDip) || layoutTotalWidthDip <= 0.0)
			return DefaultStartupBarWidthDip;
		return ResolveCachedStartupBarWidthDip(
			Inkeys::UI::Bar::CalculateExpandedTotalWidthDip(
				mainButtonTargetWidthDip, layoutTotalWidthDip));
	}

	double ResolveStartupPreviewScale(double dpi, double barZoom) noexcept
	{
		if (!std::isfinite(dpi) || dpi <= 0.0) dpi = DefaultDpi;
		if (!std::isfinite(barZoom)) barZoom = 1.0;
		barZoom = std::clamp(barZoom, MinimumBarZoom, MaximumBarZoom);
		barZoom = std::round(barZoom * 100.0) / 100.0;
		return (dpi / DefaultDpi) * barZoom;
	}

	std::int32_t RoundStartupPreviewDipToPixels(
		double dip, double dpi, double barZoom) noexcept
	{
		if (!std::isfinite(dip) || dip <= 0.0) return 0;
		const double pixels = dip * ResolveStartupPreviewScale(dpi, barZoom);
		const double maximum = static_cast<double>(
			(std::numeric_limits<std::int32_t>::max)());
		if (!std::isfinite(pixels) || pixels >= maximum)
			return (std::numeric_limits<std::int32_t>::max)();
		return static_cast<std::int32_t>(std::lround(pixels));
	}

	StartupPreviewPixelSize ResolveStartupPreviewPixelSize(
		double widthDip, double dpi, double barZoom) noexcept
	{
		widthDip = ResolveCachedStartupBarWidthDip(widthDip);
		return {
			RoundStartupPreviewDipToPixels(widthDip, dpi, barZoom),
			RoundStartupPreviewDipToPixels(DefaultStartupBarHeightDip,
				dpi, barZoom),
		};
	}

	ShimmerHorizontalTravel ResolveShimmerHorizontalTravel(
		const GeometryRect& maskBounds, const ShimmerGradient& gradient,
		double outsideMargin) noexcept
	{
		if (!std::isfinite(outsideMargin) || outsideMargin < 0.0) return {};
		double minimum = 0.0;
		double maximum = 0.0;
		double vectorX = 0.0;
		double lengthSquared = 0.0;
		if (!TryResolveProjectionBounds(maskBounds, gradient, minimum, maximum,
			vectorX, lengthSquared)) return {};
		const double leftOutside = (minimum
			- lengthSquared * gradient.supportEnd) / vectorX;
		const double rightOutside = (maximum
			- lengthSquared * gradient.supportStart) / vectorX;
		if (!std::isfinite(leftOutside) || !std::isfinite(rightOutside)) return {};
		return {
			(std::min)(leftOutside, rightOutside) - outsideMargin,
			(std::max)(leftOutside, rightOutside) + outsideMargin,
			true,
		};
	}

	bool IsShimmerSupportOutsideMask(const GeometryRect& maskBounds,
		const ShimmerGradient& gradient, double translationX) noexcept
	{
		if (!std::isfinite(translationX)) return false;
		double minimum = 0.0;
		double maximum = 0.0;
		double vectorX = 0.0;
		double lengthSquared = 0.0;
		if (!TryResolveProjectionBounds(maskBounds, gradient, minimum, maximum,
			vectorX, lengthSquared)) return false;
		minimum -= translationX * vectorX;
		maximum -= translationX * vectorX;
		const double supportMinimum = lengthSquared * gradient.supportStart;
		const double supportMaximum = lengthSquared * gradient.supportEnd;
		return maximum <= supportMinimum || minimum >= supportMaximum;
	}

	double ResolveShimmerCycleRatio(std::chrono::steady_clock::time_point now,
		std::chrono::steady_clock::time_point localEpoch,
		std::chrono::duration<double> period) noexcept
	{
		if (!std::isfinite(period.count()) || period.count() <= 0.0
			|| now <= localEpoch) return 0.0;
		const double elapsed = std::chrono::duration<double>(now - localEpoch).count();
		return std::fmod(elapsed, period.count()) / period.count();
	}

	double EaseShimmerPhase(double cycleRatio) noexcept
	{
		cycleRatio = std::clamp(cycleRatio, 0.0, 1.0);
		return (1.0 - std::cos(Pi * cycleRatio)) * 0.5;
	}

	double ResolveShimmerTranslationX(
		const ShimmerHorizontalTravel& travel, double easedPhase) noexcept
	{
		if (!travel.valid) return 0.0;
		easedPhase = std::clamp(easedPhase, 0.0, 1.0);
		return travel.startTranslationX
			+ (travel.endTranslationX - travel.startTranslationX) * easedPhase;
	}

	std::uint8_t ResolveFadeInAlpha(std::chrono::milliseconds elapsed,
		std::chrono::milliseconds duration) noexcept
	{
		if (duration.count() <= 0) return 255;
		const double phase = std::clamp(static_cast<double>(elapsed.count())
			/ static_cast<double>(duration.count()), 0.0, 1.0);
		return static_cast<std::uint8_t>(std::lround(phase * 255.0));
	}

	std::uint8_t ResolveFadeOutAlpha(std::chrono::milliseconds elapsed,
		std::chrono::milliseconds duration) noexcept
	{
		return static_cast<std::uint8_t>(255u
			- ResolveFadeInAlpha(elapsed, duration));
	}

	std::uint8_t ResolveFadeOutAlphaFrom(std::uint8_t startAlpha,
		std::chrono::milliseconds elapsed,
		std::chrono::milliseconds duration) noexcept
	{
		const double remaining = static_cast<double>(
			ResolveFadeOutAlpha(elapsed, duration)) / 255.0;
		return static_cast<std::uint8_t>(std::lround(
			static_cast<double>(startAlpha) * remaining));
	}

	HandoffSnapshot OrderedHandoffReducer::PreviewFirstAlpha0Committed() noexcept
	{
		if (state_ != HandoffState::Preparing) return Result();
		state_ = HandoffState::PreviewVisible;
		return Result();
	}

	HandoffSnapshot OrderedHandoffReducer::BarAlpha0Committed(
		bool startupComplete) noexcept
	{
		if (state_ == HandoffState::PreviewVisible)
			state_ = HandoffState::WaitingForBar;
		if (state_ != HandoffState::WaitingForBar || !startupComplete)
			return Result();
		state_ = HandoffState::PreviewFadeOut;
		return Result(true);
	}

	HandoffSnapshot OrderedHandoffReducer::PreviewFadeOutCommitted() noexcept
	{
		if (state_ != HandoffState::PreviewFadeOut) return Result();
		state_ = HandoffState::WaitingForBarOpaque;
		return Result(false, true);
	}

	HandoffSnapshot OrderedHandoffReducer::BarAlpha255Committed() noexcept
	{
		if (state_ != HandoffState::WaitingForBarOpaque
			&& state_ != HandoffState::Bypassed) return Result();
		state_ = HandoffState::Stopped;
		return Result(false, false, true);
	}

	HandoffSnapshot OrderedHandoffReducer::Bypass() noexcept
	{
		if (state_ == HandoffState::Stopped) return Result();
		state_ = HandoffState::Bypassed;
		// Preview 已不可用时请求正式 Bar 恢复 255，确认 committed 后再清理。
		return Result(false, true, false);
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
		displayedRatio_ = (std::min)(actualRatio,
			displayedRatio_ + (actualRatio - displayedRatio_) * 0.24);
		if (actualRatio < displayedRatio_) displayedRatio_ = actualRatio;
		if (failed)
		{
			state_ = ProgressVisualState::Failure;
			displayedRatio_ = actualRatio;
			return { state_, displayedRatio_, 1.0, true };
		}
		if (completed)
		{
			displayedRatio_ = 1.0;
			const bool wasVisible = state_ != ProgressVisualState::Hidden;
			return { wasVisible ? ProgressVisualState::Visible
				: ProgressVisualState::Hidden, displayedRatio_,
				wasVisible ? 1.0 : 0.0, false };
		}

		constexpr auto ShowDelay = std::chrono::seconds(3);
		constexpr auto FadeIn = std::chrono::milliseconds(180);
		if (previewShownTime_ == std::chrono::steady_clock::time_point{}
			|| now - previewShownTime_ < ShowDelay)
			return { ProgressVisualState::Hidden, displayedRatio_, 0.0, false };
		if (state_ == ProgressVisualState::Hidden)
		{
			state_ = ProgressVisualState::FadingIn;
			transitionStart_ = now;
		}
		if (state_ == ProgressVisualState::FadingIn)
		{
			const double phase = std::clamp(std::chrono::duration<double>(
				now - transitionStart_).count()
				/ std::chrono::duration<double>(FadeIn).count(), 0.0, 1.0);
			if (phase >= 1.0) state_ = ProgressVisualState::Visible;
			return { state_, displayedRatio_, phase, false };
		}
		return { state_, displayedRatio_, 1.0, false };
	}
}
