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
			double& vectorX, double& vectorY,
			double& lengthSquared) noexcept
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
			vectorY = gradient.endY - gradient.startY;
			lengthSquared = vectorX * vectorX + vectorY * vectorY;
			if (!std::isfinite(lengthSquared) || lengthSquared <= ProjectionEpsilon
				|| !std::isfinite(vectorX) || !std::isfinite(vectorY))
				return false;

			const double cornersX[]{ maskBounds.left, maskBounds.right,
				maskBounds.right, maskBounds.left };
			const double cornersY[]{ maskBounds.top, maskBounds.top,
				maskBounds.bottom, maskBounds.bottom };
			minimum = (std::numeric_limits<double>::max)();
			maximum = -(std::numeric_limits<double>::max)();
			for (int index = 0; index < 4; ++index)
			{
				const double projection = ((cornersX[index] - gradient.startX)
					* vectorX + (cornersY[index] - gradient.startY) * vectorY)
					/ lengthSquared;
				minimum = (std::min)(minimum, projection);
				maximum = (std::max)(maximum, projection);
			}
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

	ShimmerGradient ResolveStartupPreviewShimmerGradient(
		double width, double height) noexcept
	{
		if (!std::isfinite(width) || !std::isfinite(height)
			|| width <= 0.0 || height <= 0.0) return {};
		const double supportX = (std::max)(160.0, width * 0.5);
		// Y 方向跨度来自占位高度，让高光核心保持旧版左上到右下的斜向质感。
		return { 0.0, height * -0.5, supportX, height * 1.5, 0.0, 1.0 };
	}

	ShimmerTravel ResolveShimmerTravel(
		const GeometryRect& maskBounds, const ShimmerGradient& gradient,
		double outsideMargin) noexcept
	{
		if (!std::isfinite(outsideMargin) || outsideMargin < 0.0) return {};
		double minimum = 0.0;
		double maximum = 0.0;
		double vectorX = 0.0;
		double vectorY = 0.0;
		double lengthSquared = 0.0;
		if (!TryResolveProjectionBounds(maskBounds, gradient, minimum, maximum,
			vectorX, vectorY, lengthSquared)) return {};
		const double length = std::sqrt(lengthSquared);
		if (!std::isfinite(length) || length <= ProjectionEpsilon) return {};
		const double projectedMargin = outsideMargin / length;
		const double startOffset = minimum - gradient.supportEnd - projectedMargin;
		const double endOffset = maximum - gradient.supportStart + projectedMargin;
		if (!std::isfinite(startOffset) || !std::isfinite(endOffset)
			|| startOffset >= endOffset) return {};
		return {
			vectorX * startOffset,
			vectorY * startOffset,
			vectorX * endOffset,
			vectorY * endOffset,
			true,
		};
	}

	bool IsShimmerSupportOutsideMask(const GeometryRect& maskBounds,
		const ShimmerGradient& gradient,
		double translationX, double translationY) noexcept
	{
		if (!std::isfinite(translationX) || !std::isfinite(translationY))
			return false;
		double minimum = 0.0;
		double maximum = 0.0;
		double vectorX = 0.0;
		double vectorY = 0.0;
		double lengthSquared = 0.0;
		if (!TryResolveProjectionBounds(maskBounds, gradient, minimum, maximum,
			vectorX, vectorY, lengthSquared)) return false;
		const double translationProjection = (translationX * vectorX
			+ translationY * vectorY) / lengthSquared;
		minimum -= translationProjection;
		maximum -= translationProjection;
		const double supportMinimum = gradient.supportStart;
		const double supportMaximum = gradient.supportEnd;
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
		cycleRatio = std::clamp(cycleRatio, 0.0,
			StartupPreviewShimmerSweepFraction);
		const double sweepRatio = cycleRatio
			/ StartupPreviewShimmerSweepFraction;
		return (1.0 - std::cos(Pi * sweepRatio)) * 0.5;
	}

	ShimmerTranslation ResolveShimmerTranslation(
		const ShimmerTravel& travel, double easedPhase) noexcept
	{
		if (!travel.valid) return {};
		easedPhase = std::clamp(easedPhase, 0.0, 1.0);
		return {
			travel.startTranslationX
				+ (travel.endTranslationX - travel.startTranslationX) * easedPhase,
			travel.startTranslationY
				+ (travel.endTranslationY - travel.startTranslationY) * easedPhase,
		};
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

	void HandoffTimingReducer::PreviewFrameCommitted(bool progressVisible,
		std::uint8_t previewAlpha) noexcept
	{
		if (!frozen_ && progressVisible && previewAlpha > 0)
			progressEverCommittedVisible_ = true;
	}

	std::chrono::milliseconds HandoffTimingReducer::FreezeDuration() noexcept
	{
		if (!frozen_)
		{
			frozen_ = true;
			frozenDuration_ = progressEverCommittedVisible_
				? StartupPreviewProgressHandoffDuration
				: StartupPreviewQuickHandoffDuration;
		}
		return frozenDuration_;
	}

	void ProgressVisualReducer::MarkPreviewShown(
		std::chrono::steady_clock::time_point now) noexcept
	{
		if (previewShownTime_ == std::chrono::steady_clock::time_point{})
			previewShownTime_ = now;
	}

	double ProgressVisualReducer::SampleDisplayedRatio(
		std::chrono::steady_clock::time_point now) noexcept
	{
		if (!ratioAnimating_ || now <= ratioStartTime_)
			return displayedRatio_;
		const double phase = std::clamp(std::chrono::duration<double>(
			now - ratioStartTime_).count()
			/ std::chrono::duration<double>(
				StartupPreviewProgressAnimationDuration).count(), 0.0, 1.0);
		const double eased = phase * phase * (3.0 - 2.0 * phase);
		displayedRatio_ = ratioStart_
			+ (ratioTarget_ - ratioStart_) * eased;
		if (phase >= 1.0)
		{
			displayedRatio_ = ratioTarget_;
			ratioStart_ = ratioTarget_;
			ratioAnimating_ = false;
		}
		return displayedRatio_;
	}

	double ProgressVisualReducer::SampleFailureColorProgress(
		std::chrono::steady_clock::time_point now) const noexcept
	{
		(void)now;
		// 首次 fatal 帧直接使用红色，避免快速失败时先短暂显示蓝色。
		return failureTargetFrozen_ ? 1.0 : 0.0;
	}

	void ProgressVisualReducer::RetargetRatio(
		std::chrono::steady_clock::time_point now, double targetRatio) noexcept
	{
		targetRatio = std::clamp(targetRatio, 0.0, 1.0);
		(void)SampleDisplayedRatio(now);
		if (targetRatio < displayedRatio_)
		{
			// 真实进度下降时立即收紧，不能让动画短暂伪报更高进度。
			displayedRatio_ = targetRatio;
			ratioStart_ = targetRatio;
			ratioTarget_ = targetRatio;
			ratioAnimating_ = false;
			return;
		}
		if (targetRatio == ratioTarget_) return;
		ratioStart_ = displayedRatio_;
		ratioTarget_ = targetRatio;
		ratioStartTime_ = now;
		ratioAnimating_ = targetRatio != displayedRatio_;
	}

	ProgressVisualSnapshot ProgressVisualReducer::Update(
		std::chrono::steady_clock::time_point now, double actualRatio,
		bool completed, bool failed) noexcept
	{
		if (!std::isfinite(actualRatio)) actualRatio = 0.0;
		actualRatio = std::clamp(actualRatio, 0.0, 1.0);
		if (failed || failureTargetFrozen_)
		{
			if (!failureTargetFrozen_)
			{
				failureTarget_ = actualRatio;
				failureColorStartTime_ = now;
				failureTargetFrozen_ = true;
			}
			RetargetRatio(now, failureTarget_);
			state_ = ProgressVisualState::Failure;
			return { state_, displayedRatio_, 1.0,
				SampleFailureColorProgress(now) };
		}
		// completed 只改变可见状态；长度仍不得超过调用方提供的真实比例。
		RetargetRatio(now, actualRatio);
		if (completed)
		{
			if (state_ == ProgressVisualState::Hidden)
				return { state_, displayedRatio_, 0.0, 0.0 };
			if (state_ == ProgressVisualState::FadingIn)
			{
				constexpr auto FadeIn = std::chrono::milliseconds(180);
				const double phase = std::clamp(std::chrono::duration<double>(
					now - transitionStart_).count()
					/ std::chrono::duration<double>(FadeIn).count(), 0.0, 1.0);
				if (phase >= 1.0) state_ = ProgressVisualState::Visible;
				return { state_, displayedRatio_, phase, 0.0 };
			}
			return { state_, displayedRatio_, 1.0, 0.0 };
		}

		constexpr auto ShowDelay = std::chrono::seconds(3);
		constexpr auto FadeIn = std::chrono::milliseconds(180);
		if (previewShownTime_ == std::chrono::steady_clock::time_point{}
			|| now - previewShownTime_ < ShowDelay)
			return { ProgressVisualState::Hidden, displayedRatio_, 0.0, 0.0 };
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
			return { state_, displayedRatio_, phase, 0.0 };
		}
		return { state_, displayedRatio_, 1.0, 0.0 };
	}
}
