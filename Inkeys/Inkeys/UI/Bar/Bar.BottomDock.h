#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace Inkeys::UI::Bar
{
	inline constexpr double BarBottomDockThresholdDip = 20.0;
	inline constexpr double BarBottomDockCenterThresholdDip = 40.0;
		inline constexpr double BarWhiteboardBottomInsetDip = 5.0;
	inline constexpr double BarBottomDockVisualLimitDip = 24.0;
	inline constexpr double BarBottomDockSpringOmega = 18.0;
	inline constexpr double BarBottomDockSpringDampingRatio = 0.72;
	inline constexpr double BarBottomDockSpringMaxDtSeconds = 0.032;
	inline constexpr double BarBottomDockSpringStepSeconds = 1.0 / 120.0;
	inline constexpr double BarBottomDockSettleDistanceDip = 0.15;
	inline constexpr double BarBottomDockSettleVelocityDipPerSecond = 4.0;
	inline constexpr double BarBottomDockIndicatorHeightDip = 30.0;
	inline constexpr double BarBottomDockIndicatorCornerRadiusDip = 6.0;

	enum class BarBottomDockMode
	{
		Floating,
		BottomDocked,
	};

	enum class BarBottomDockCenterMode
	{
		Free,
		Centered,
	};

	enum class BarBottomDockPhase
	{
		Stable,
		Capturing,
		Dragging,
		Detaching,
		Recovering,
	};

	struct BarBottomDockFeedbackGeometry
	{
		double leftDip = 0.0;
		double topDip = 0.0;
		double rightDip = 0.0;
		double bottomDip = 0.0;
	};

	enum class BarBottomDockFeedbackAction
	{
		None,
		FadeIn,
		FadeOut,
	};

	enum class BarBottomDockIndicatorContentAction
	{
		None,
		ExpandFrame,
		SwapText,
		ShrinkFrame,
	};

	[[nodiscard]] inline BarBottomDockFeedbackAction
		ResolveBarBottomDockFeedbackAction(
			bool wasVisibleTarget, bool isVisibleTarget) noexcept
	{
		if (!wasVisibleTarget && isVisibleTarget)
			return BarBottomDockFeedbackAction::FadeIn;
		if (wasVisibleTarget && !isVisibleTarget)
			return BarBottomDockFeedbackAction::FadeOut;
		return BarBottomDockFeedbackAction::None;
	}

	[[nodiscard]] inline BarBottomDockIndicatorContentAction
		ResolveBarBottomDockIndicatorContentAction(
			double currentTextWidthDip, double targetTextWidthDip,
			bool textMatchesTarget) noexcept
	{
		currentTextWidthDip = std::max(0.0,
			std::isfinite(currentTextWidthDip) ? currentTextWidthDip : 0.0);
		targetTextWidthDip = std::max(0.0,
			std::isfinite(targetTextWidthDip) ? targetTextWidthDip : 0.0);
		if (!textMatchesTarget)
			return currentTextWidthDip + 0.000001 < targetTextWidthDip
				? BarBottomDockIndicatorContentAction::ExpandFrame
				: BarBottomDockIndicatorContentAction::SwapText;
		if (currentTextWidthDip > targetTextWidthDip + 0.000001)
			return BarBottomDockIndicatorContentAction::ShrinkFrame;
		if (currentTextWidthDip + 0.000001 < targetTextWidthDip)
			return BarBottomDockIndicatorContentAction::ExpandFrame;
		return BarBottomDockIndicatorContentAction::None;
	}

	[[nodiscard]] inline bool ResolveBarBottomDockIndicatorTarget(
		BarBottomDockMode currentMode, bool dragActive,
		bool mainBarExpanded, bool gestureEligible) noexcept
	{
		return currentMode == BarBottomDockMode::BottomDocked
			&& dragActive && mainBarExpanded && gestureEligible;
	}

	[[nodiscard]] inline bool ResolveBarBottomDockIndicatorTarget(
		BarBottomDockMode currentMode, bool dragActive,
		bool mainBarExpanded) noexcept
	{
		return ResolveBarBottomDockIndicatorTarget(
			currentMode, dragActive, mainBarExpanded, true);
	}

	struct BarBottomDockIndicatorGestureEligibility
	{
		bool gestureActive = false;
		bool eligible = false;
	};

	[[nodiscard]] inline BarBottomDockIndicatorGestureEligibility
		ResolveBarBottomDockIndicatorGestureEligibility(
			bool previousGestureActive, bool enteredBottomDock,
			bool centerModeChanged, bool verticallyDocked,
			bool mainBarExpanded) noexcept
	{
		bool gestureActive = previousGestureActive;
		if (!verticallyDocked || !mainBarExpanded)
			gestureActive = false;
		else if (enteredBottomDock || centerModeChanged)
			gestureActive = true;
		return { gestureActive, gestureActive };
	}

	[[nodiscard]] inline BarBottomDockFeedbackGeometry
		ResolveBarBottomDockIndicatorGeometry(
			const BarBottomDockFeedbackGeometry& mainButton,
			const BarBottomDockFeedbackGeometry& mainBar,
			double actualLabelWidthDip,
			double actualLabelHeightDip,
			double visibleMainBarTopDip) noexcept
	{
		auto FiniteOrZero = [](double value) noexcept
			{ return std::isfinite(value) ? value : 0.0; };
		auto Normalize = [&](const BarBottomDockFeedbackGeometry& input) noexcept
			{
				BarBottomDockFeedbackGeometry result{
					FiniteOrZero(input.leftDip), FiniteOrZero(input.topDip),
					FiniteOrZero(input.rightDip), FiniteOrZero(input.bottomDip) };
				if (result.rightDip < result.leftDip)
					std::swap(result.leftDip, result.rightDip);
				if (result.bottomDip < result.topDip)
					std::swap(result.topDip, result.bottomDip);
				return result;
			};
		const auto button = Normalize(mainButton);
		const auto bar = Normalize(mainBar);
		actualLabelWidthDip = std::max(0.0, std::isfinite(actualLabelWidthDip)
			? actualLabelWidthDip : 0.0);
		actualLabelHeightDip = std::max(0.0,
			std::isfinite(actualLabelHeightDip) ? actualLabelHeightDip : 0.0);
		visibleMainBarTopDip = std::isfinite(visibleMainBarTopDip)
			? visibleMainBarTopDip : bar.topDip;
		// 高度扣除实际文字高度后的单侧余量同时作为水平内边距。
		const double padding = std::max(0.0,
			(BarBottomDockIndicatorHeightDip - actualLabelHeightDip) / 2.0);
		const double width = actualLabelWidthDip + padding * 2.0;
		const double center = (std::min(button.leftDip, bar.leftDip)
			+ std::max(button.rightDip, bar.rightDip)) / 2.0;
		const double top = visibleMainBarTopDip
			- BarBottomDockIndicatorHeightDip / 2.0;
		return { center - width / 2.0, top, center + width / 2.0,
			top + BarBottomDockIndicatorHeightDip };
	}

	[[nodiscard]] inline BarBottomDockFeedbackGeometry
		ResolveBarBottomDockIndicatorScaledGeometry(
			const BarBottomDockFeedbackGeometry& geometry,
			double scale) noexcept
	{
		auto FiniteOrZero = [](double value) noexcept
			{ return std::isfinite(value) ? value : 0.0; };
		double left = FiniteOrZero(geometry.leftDip);
		double top = FiniteOrZero(geometry.topDip);
		double right = FiniteOrZero(geometry.rightDip);
		double bottom = FiniteOrZero(geometry.bottomDip);
		if (right < left) std::swap(left, right);
		if (bottom < top) std::swap(top, bottom);
		scale = std::max(0.0, std::isfinite(scale) ? scale : 0.0);
		const double centerX = (left + right) / 2.0;
		const double centerY = (top + bottom) / 2.0;
		const double halfWidth = (right - left) * scale / 2.0;
		const double halfHeight = (bottom - top) * scale / 2.0;
		return { centerX - halfWidth, centerY - halfHeight,
			centerX + halfWidth, centerY + halfHeight };
	}

	[[nodiscard]] inline bool IsBarBottomDockIndicatorHit(
		bool visible, const RECT& bounds, LONG x, LONG y) noexcept
	{
		return visible && bounds.right > bounds.left && bounds.bottom > bounds.top
			&& x >= bounds.left && x < bounds.right
			&& y >= bounds.top && y < bounds.bottom;
	}

		struct BarBottomDockEnvironment
		{
			RECT monitorBounds{};
			RECT workArea{};
			double zoom = 1.0;
			double insetDip = 0.0;
			double dpiScale = 1.0;
		};

	[[nodiscard]] inline double NormalizeBarBottomDockZoom(double zoom) noexcept
	{
		return std::isfinite(zoom) && zoom > 0.0 ? zoom : 1.0;
	}

	[[nodiscard]] inline RECT ResolveBarBottomDockIndicatorVisualEnvelope(
		const BarBottomDockFeedbackGeometry& geometry,
		double scale, double strokeWidthDip, double gaussianOutsetDip,
		double zoom, LONG antialiasPaddingPx) noexcept
	{
		scale = std::max(0.0, std::isfinite(scale) ? scale : 0.0);
		if (scale <= 0.0) return {};
		strokeWidthDip = std::max(0.0,
			std::isfinite(strokeWidthDip) ? strokeWidthDip : 0.0);
		gaussianOutsetDip = std::max(0.0,
			std::isfinite(gaussianOutsetDip) ? gaussianOutsetDip : 0.0);
		zoom = NormalizeBarBottomDockZoom(zoom);
		antialiasPaddingPx = std::max(0L, antialiasPaddingPx);

		const auto scaled = ResolveBarBottomDockIndicatorScaledGeometry(
			geometry, scale);
		if (scaled.rightDip <= scaled.leftDip
			|| scaled.bottomDip <= scaled.topDip)
			return {};
		// Back 超调、随比例描边和固定 Gaussian 外扩共用同一个权威包络。
		const LONG visualPadding = static_cast<LONG>(std::ceil(
			(strokeWidthDip * scale + gaussianOutsetDip) * zoom))
			+ antialiasPaddingPx;
		return RECT{
			static_cast<LONG>(std::floor(scaled.leftDip * zoom))
				- visualPadding,
			static_cast<LONG>(std::floor(scaled.topDip * zoom))
				- visualPadding,
			static_cast<LONG>(std::ceil(scaled.rightDip * zoom))
				+ visualPadding,
			static_cast<LONG>(std::ceil(scaled.bottomDip * zoom))
				+ visualPadding,
		};
	}

	[[nodiscard]] inline double ResolveBarBottomDockInteractionZoom(
		UINT dpi, double configZoom) noexcept
	{
		const double dpiScale = std::clamp(
			static_cast<double>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI)
				/ static_cast<double>(USER_DEFAULT_SCREEN_DPI),
			0.5, 4.0);
		if (!std::isfinite(configZoom) || configZoom <= 0.0)
			configZoom = 1.0;
		// 交互阈值只消费一次最终 zoom，不能再叠乘窗口 DPI。
		return NormalizeBarBottomDockZoom(dpiScale * configZoom);
	}

		[[nodiscard]] inline double ResolveBarBottomDockInsetPixels(
			double insetDip, double dpiScale) noexcept
		{
			insetDip = std::max(0.0, std::isfinite(insetDip) ? insetDip : 0.0);
			dpiScale = NormalizeBarBottomDockZoom(
				std::isfinite(dpiScale) && dpiScale > 0.0 ? dpiScale : 1.0);
			return std::round(insetDip * dpiScale);
		}

		[[nodiscard]] inline double ResolveBarBottomDockLine(
			const RECT& monitorBounds, const RECT& workArea,
			double insetDip = 0.0, double dpiScale = 1.0) noexcept
		{
			const bool monitorValid = monitorBounds.right > monitorBounds.left
				&& monitorBounds.bottom > monitorBounds.top;
			if (!monitorValid) return static_cast<double>(monitorBounds.bottom);
			const double insetPx = ResolveBarBottomDockInsetPixels(insetDip, dpiScale);
			if (insetPx > 0.0)
			{
				// 白板铺满显示器后，底栏贴屏幕底边再上移统一边距，不再跟任务栏工作区。
				return static_cast<double>(monitorBounds.bottom) - insetPx;
			}
			const bool workAreaValid = workArea.right > workArea.left
				&& workArea.bottom > workArea.top
				&& workArea.left >= monitorBounds.left
				&& workArea.top >= monitorBounds.top
				&& workArea.right <= monitorBounds.right
				&& workArea.bottom <= monitorBounds.bottom;
			return static_cast<double>(workAreaValid
				&& workArea.bottom < monitorBounds.bottom
				? workArea.bottom : monitorBounds.bottom);
		}

	[[nodiscard]] inline double ResolveBarVisibleBorderBottomScreen(
		double centerScreenY, double bodyHeightDip,
		double strokeWidthDip, double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		bodyHeightDip = std::max(0.0, std::isfinite(bodyHeightDip)
			? bodyHeightDip : 0.0);
		strokeWidthDip = std::max(0.0, std::isfinite(strokeWidthDip)
			? strokeWidthDip : 0.0);
		return centerScreenY + (bodyHeightDip + strokeWidthDip) * zoom / 2.0;
	}

	[[nodiscard]] inline double ResolveBarBottomDockCenterScreenY(
		double dockLineScreenY, double bodyHeightDip,
		double strokeWidthDip, double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		bodyHeightDip = std::max(0.0, std::isfinite(bodyHeightDip)
			? bodyHeightDip : 0.0);
		strokeWidthDip = std::max(0.0, std::isfinite(strokeWidthDip)
			? strokeWidthDip : 0.0);
		return dockLineScreenY - (bodyHeightDip + strokeWidthDip) * zoom / 2.0;
	}

	[[nodiscard]] inline double ResolveBarBottomDockElasticOffsetForScreenGrip(
		double rigidGripScreenY, double dockCenterScreenY,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		if (!std::isfinite(dockCenterScreenY)) dockCenterScreenY = 0.0;
		if (!std::isfinite(rigidGripScreenY))
			rigidGripScreenY = dockCenterScreenY;
		return std::clamp((rigidGripScreenY - dockCenterScreenY) / zoom,
			-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
	}

	[[nodiscard]] inline bool ShouldAllowBarBottomDockClick(
		bool directMoveFailed, bool gestureCancelled, bool modeChanged,
		double rawPathLengthScreen, double maximumElasticTravelScreen,
		double maximumInteractionZoom) noexcept
	{
		const double elasticClickSlopScreen = std::max(
			2.0, 3.0 * NormalizeBarBottomDockZoom(maximumInteractionZoom));
		return !directMoveFailed && !gestureCancelled && !modeChanged
			&& std::isfinite(rawPathLengthScreen)
			&& rawPathLengthScreen <= 20.0
			&& std::isfinite(maximumElasticTravelScreen)
			&& maximumElasticTravelScreen <= elasticClickSlopScreen;
	}

	[[nodiscard]] inline double ClampBarBottomDockMainCenterScreenX(
		double centerScreenX, const RECT& monitorBounds,
		double mainVisibleHalfWidthDip, double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		mainVisibleHalfWidthDip = std::max(0.0,
			std::isfinite(mainVisibleHalfWidthDip) ? mainVisibleHalfWidthDip : 0.0);
		const double halfWidth = mainVisibleHalfWidthDip * zoom;
		const double minimum = static_cast<double>(monitorBounds.left) + halfWidth;
		const double maximum = std::max(minimum,
			static_cast<double>(monitorBounds.right) - halfWidth);
		return std::clamp(centerScreenX, minimum, maximum);
	}

	[[nodiscard]] inline bool ResolveBarMainBarRightSide(
		double mainCenterX, double monitorWidth) noexcept
	{
		if (!std::isfinite(mainCenterX) || !std::isfinite(monitorWidth)
			|| monitorWidth <= 0.0)
			return true;
		// 中轴线上保持右展；只有抬手后越过中轴才切换到左展。
		return mainCenterX <= monitorWidth / 2.0;
	}

	[[nodiscard]] inline bool ResolveBarBottomDockPositionMainBarSide(
		BarBottomDockCenterMode presentedCenterMode,
		double mainCenterX, double windowWidth, bool currentSide) noexcept
	{
		// 居中态保持当前布局方向；未初始化窗口也不能参与重新分类。
		if (presentedCenterMode == BarBottomDockCenterMode::Centered
			|| !std::isfinite(windowWidth) || windowWidth <= 0.0)
			return currentSide;
		return ResolveBarMainBarRightSide(mainCenterX, windowWidth);
	}

	[[nodiscard]] inline double ResolveBarBottomDockInitialMainCenterScreenX(
		const RECT& monitorBounds, double bodyLeftFromMainCenterDip,
		double bodyRightFromMainCenterDip, double mainVisibleHalfWidthDip,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		if (!std::isfinite(bodyLeftFromMainCenterDip))
			bodyLeftFromMainCenterDip = -mainVisibleHalfWidthDip;
		if (!std::isfinite(bodyRightFromMainCenterDip))
			bodyRightFromMainCenterDip = mainVisibleHalfWidthDip;
		if (bodyRightFromMainCenterDip < bodyLeftFromMainCenterDip)
			std::swap(bodyLeftFromMainCenterDip, bodyRightFromMainCenterDip);
		const double monitorCenter = (static_cast<double>(monitorBounds.left)
			+ static_cast<double>(monitorBounds.right)) / 2.0;
		const double bodyCenterOffset = (bodyLeftFromMainCenterDip
			+ bodyRightFromMainCenterDip) * zoom / 2.0;
		return ClampBarBottomDockMainCenterScreenX(
			monitorCenter - bodyCenterOffset, monitorBounds,
			mainVisibleHalfWidthDip, zoom);
	}

	[[nodiscard]] inline double ResolveBarBottomDockMonitorCenterScreenX(
		const RECT& monitorBounds) noexcept
	{
		return (static_cast<double>(monitorBounds.left)
			+ static_cast<double>(monitorBounds.right)) / 2.0;
	}

	[[nodiscard]] inline bool ShouldCaptureBarBottomDockCenter(
		bool verticallyDocked, bool mainBarExpanded,
		double bodyCenterScreenX, const RECT& monitorBounds,
		double zoom) noexcept
	{
		if (!verticallyDocked || !mainBarExpanded
			|| monitorBounds.right <= monitorBounds.left
			|| !std::isfinite(bodyCenterScreenX))
			return false;
		zoom = NormalizeBarBottomDockZoom(zoom);
		return std::abs(bodyCenterScreenX
			- ResolveBarBottomDockMonitorCenterScreenX(monitorBounds))
			<= BarBottomDockCenterThresholdDip * zoom;
	}

	struct BarBottomDockCenterDragUpdate
	{
		BarBottomDockCenterMode mode = BarBottomDockCenterMode::Free;
		BarBottomDockPhase phase = BarBottomDockPhase::Stable;
		double monitorCenterScreenX = 0.0;
		double stableCenterScreenX = 0.0;
		double constrainedCenterScreenX = 0.0;
		double elasticOffsetDip = 0.0;
		bool captured = false;
		bool detached = false;
		bool modeChanged = false;
	};

	class BarBottomDockCenterDragTracker
	{
	public:
		void Begin(BarBottomDockCenterMode mode,
			double rawBodyCenterScreenX,
			bool verticallyDocked, bool mainBarExpanded,
			const BarBottomDockEnvironment& environment) noexcept
		{
			const bool enabled = verticallyDocked && mainBarExpanded
				&& environment.monitorBounds.right
					> environment.monitorBounds.left;
			mode_ = enabled ? mode : BarBottomDockCenterMode::Free;
			phase_ = BarBottomDockPhase::Dragging;
			stableCenterScreenX_ = ResolveBarBottomDockMonitorCenterScreenX(
				environment.monitorBounds);
			previousRawBodyCenterScreenX_ = std::isfinite(rawBodyCenterScreenX)
				? rawBodyCenterScreenX : stableCenterScreenX_;
			const double zoom = NormalizeBarBottomDockZoom(environment.zoom);
			elasticOffsetDip_ = mode_ == BarBottomDockCenterMode::Centered
				? std::clamp((previousRawBodyCenterScreenX_
					- stableCenterScreenX_) / zoom,
					-BarBottomDockCenterThresholdDip,
					BarBottomDockCenterThresholdDip)
				: 0.0;
		}

		[[nodiscard]] BarBottomDockCenterDragUpdate Update(
			double rawBodyCenterScreenX,
			bool verticallyDocked, bool mainBarExpanded,
			const BarBottomDockEnvironment& environment) noexcept
		{
			const double zoom = NormalizeBarBottomDockZoom(environment.zoom);
			const double thresholdScreen = BarBottomDockCenterThresholdDip * zoom;
			const bool enabled = verticallyDocked && mainBarExpanded
				&& environment.monitorBounds.right
					> environment.monitorBounds.left;
			const double monitorCenter = ResolveBarBottomDockMonitorCenterScreenX(
				environment.monitorBounds);
			if (!std::isfinite(rawBodyCenterScreenX))
				rawBodyCenterScreenX = previousRawBodyCenterScreenX_;

			BarBottomDockCenterDragUpdate result;
			if (mode_ == BarBottomDockCenterMode::Free
				&& phase_ == BarBottomDockPhase::Detaching)
				phase_ = BarBottomDockPhase::Recovering;

			if (!enabled)
			{
				if (mode_ == BarBottomDockCenterMode::Centered)
				{
					mode_ = BarBottomDockCenterMode::Free;
					phase_ = BarBottomDockPhase::Detaching;
					result.detached = true;
					result.modeChanged = true;
				}
				else if (phase_ != BarBottomDockPhase::Recovering)
					phase_ = BarBottomDockPhase::Dragging;
			}
			else if (mode_ == BarBottomDockCenterMode::Free)
			{
				const double currentDistance = rawBodyCenterScreenX - monitorCenter;
				const double previousDistance =
					previousRawBodyCenterScreenX_ - monitorCenter;
				const bool inCaptureBand = std::isfinite(currentDistance)
					&& std::abs(currentDistance) <= thresholdScreen;
				const bool segmentTouchesCaptureBand =
					std::isfinite(currentDistance)
					&& std::isfinite(previousDistance)
					&& std::min(previousDistance, currentDistance) <= thresholdScreen
					&& std::max(previousDistance, currentDistance) >= -thresholdScreen;
				if (inCaptureBand)
				{
					stableCenterScreenX_ = monitorCenter;
					elasticOffsetDip_ = std::clamp(currentDistance / zoom,
						-BarBottomDockCenterThresholdDip,
						BarBottomDockCenterThresholdDip);
					mode_ = BarBottomDockCenterMode::Centered;
					phase_ = BarBottomDockPhase::Capturing;
					result.captured = true;
					result.modeChanged = true;
				}
				else if (segmentTouchesCaptureBand)
				{
					// 高速横穿捕获带时消费完整线段，最终保持 Free 并连续恢复。
					stableCenterScreenX_ = monitorCenter;
					elasticOffsetDip_ = std::copysign(
						BarBottomDockCenterThresholdDip, currentDistance);
					phase_ = BarBottomDockPhase::Detaching;
					result.captured = true;
					result.detached = true;
					result.modeChanged = true;
				}
			}
			else
			{
				stableCenterScreenX_ = monitorCenter;
				const double offsetScreen = rawBodyCenterScreenX - monitorCenter;
				elasticOffsetDip_ = std::clamp(offsetScreen / zoom,
					-BarBottomDockCenterThresholdDip, BarBottomDockCenterThresholdDip);
				if (std::abs(offsetScreen) > thresholdScreen)
				{
					mode_ = BarBottomDockCenterMode::Free;
					phase_ = BarBottomDockPhase::Detaching;
					result.detached = true;
					result.modeChanged = true;
				}
				else phase_ = BarBottomDockPhase::Dragging;
			}

			previousRawBodyCenterScreenX_ = rawBodyCenterScreenX;
			result.mode = mode_;
			result.phase = phase_;
			result.monitorCenterScreenX = monitorCenter;
			result.stableCenterScreenX = stableCenterScreenX_;
			result.constrainedCenterScreenX =
				mode_ == BarBottomDockCenterMode::Centered
				? stableCenterScreenX_ : rawBodyCenterScreenX;
			result.elasticOffsetDip = elasticOffsetDip_;
			return result;
		}

		void Rebase(double rawBodyCenterScreenX,
			const RECT& monitorBounds) noexcept
		{
			stableCenterScreenX_ = ResolveBarBottomDockMonitorCenterScreenX(
				monitorBounds);
			previousRawBodyCenterScreenX_ = std::isfinite(rawBodyCenterScreenX)
				? rawBodyCenterScreenX : stableCenterScreenX_;
		}

		void End() noexcept
		{
			phase_ = std::abs(elasticOffsetDip_) > BarBottomDockSettleDistanceDip
				? BarBottomDockPhase::Recovering : BarBottomDockPhase::Stable;
		}

		void SetSettled() noexcept
		{
			phase_ = BarBottomDockPhase::Stable;
			elasticOffsetDip_ = 0.0;
		}

		[[nodiscard]] BarBottomDockCenterMode Mode() const noexcept
		{
			return mode_;
		}
		[[nodiscard]] BarBottomDockPhase Phase() const noexcept { return phase_; }
		[[nodiscard]] double ElasticOffsetDip() const noexcept
		{
			return elasticOffsetDip_;
		}

	private:
		BarBottomDockCenterMode mode_ = BarBottomDockCenterMode::Free;
		BarBottomDockPhase phase_ = BarBottomDockPhase::Stable;
		double previousRawBodyCenterScreenX_ = 0.0;
		double stableCenterScreenX_ = 0.0;
		double elasticOffsetDip_ = 0.0;
	};

	struct BarBottomDockDragUpdate
	{
		BarBottomDockMode mode = BarBottomDockMode::Floating;
		BarBottomDockPhase phase = BarBottomDockPhase::Stable;
		double dockLineScreenY = 0.0;
		double stableGripScreenY = 0.0;
		double constrainedGripScreenY = 0.0;
		double elasticOffsetDip = 0.0;
		bool captured = false;
		bool detached = false;
		bool modeChanged = false;
	};

	class BarBottomDockDragTracker
	{
	public:
		void Begin(BarBottomDockMode mode, double rawGripScreenY,
			double floatingVisibleBottomScreenY, double stableDockGripScreenY,
			const BarBottomDockEnvironment& environment) noexcept
		{
			mode_ = mode;
			phase_ = BarBottomDockPhase::Dragging;
			previousFloatingVisibleBottomScreenY_ = floatingVisibleBottomScreenY;
			stableGripScreenY_ = stableDockGripScreenY;
			const double zoom = NormalizeBarBottomDockZoom(environment.zoom);
			elasticOffsetDip_ = mode == BarBottomDockMode::BottomDocked
				? std::clamp((rawGripScreenY - stableGripScreenY_) / zoom,
					-BarBottomDockThresholdDip, BarBottomDockThresholdDip)
				: 0.0;
		}

		[[nodiscard]] BarBottomDockDragUpdate Update(
			double rawGripScreenY, double floatingVisibleBottomScreenY,
			const BarBottomDockEnvironment& environment) noexcept
		{
			const double zoom = NormalizeBarBottomDockZoom(environment.zoom);
				const double dockLine = ResolveBarBottomDockLine(
					environment.monitorBounds, environment.workArea,
					environment.insetDip, environment.dpiScale);
			const double thresholdScreen = BarBottomDockThresholdDip * zoom;
			BarBottomDockDragUpdate result;
			result.mode = mode_;
			result.phase = phase_;
			result.dockLineScreenY = dockLine;
			if (mode_ == BarBottomDockMode::Floating
				&& phase_ == BarBottomDockPhase::Detaching)
				phase_ = BarBottomDockPhase::Recovering;

			if (mode_ == BarBottomDockMode::Floating)
			{
				const double currentDistance = floatingVisibleBottomScreenY - dockLine;
				const double previousDistance =
					previousFloatingVisibleBottomScreenY_ - dockLine;
				const bool inCaptureBand = std::isfinite(currentDistance)
					&& std::abs(currentDistance) <= thresholdScreen;
				const bool segmentTouchesCaptureBand =
					std::isfinite(currentDistance)
					&& std::isfinite(previousDistance)
					&& std::min(previousDistance, currentDistance) <= thresholdScreen
					&& std::max(previousDistance, currentDistance) >= -thresholdScreen;
				if (inCaptureBand)
				{
					// 抓住时用同一采样反推出稳定抓取点，避免 HWND 改 y 后跳变。
					stableGripScreenY_ = rawGripScreenY - currentDistance;
					elasticOffsetDip_ = std::clamp(currentDistance / zoom,
						-BarBottomDockThresholdDip, BarBottomDockThresholdDip);
					mode_ = BarBottomDockMode::BottomDocked;
					phase_ = BarBottomDockPhase::Capturing;
					result.captured = true;
					result.modeChanged = true;
				}
				else if (segmentTouchesCaptureBand)
				{
					// 单个高速采样先经过捕获区、再越过脱离阈值：消费完整线段，
					// 最终保持悬浮并从阈值形变量连续恢复，不能在 dock 上停一帧。
					stableGripScreenY_ = rawGripScreenY - currentDistance;
					elasticOffsetDip_ = std::copysign(
						BarBottomDockThresholdDip, currentDistance);
					phase_ = BarBottomDockPhase::Detaching;
					result.captured = true;
					result.detached = true;
					result.modeChanged = true;
				}
			}
			else
			{
				const double offsetScreen = rawGripScreenY - stableGripScreenY_;
				elasticOffsetDip_ = std::clamp(offsetScreen / zoom,
					-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
				if (std::abs(offsetScreen) > thresholdScreen)
				{
					mode_ = BarBottomDockMode::Floating;
					phase_ = BarBottomDockPhase::Detaching;
					result.detached = true;
					result.modeChanged = true;
				}
				else phase_ = BarBottomDockPhase::Dragging;
			}

			previousFloatingVisibleBottomScreenY_ = floatingVisibleBottomScreenY;
			result.mode = mode_;
			result.phase = phase_;
			result.stableGripScreenY = stableGripScreenY_;
			result.constrainedGripScreenY = mode_ == BarBottomDockMode::BottomDocked
				? stableGripScreenY_ : rawGripScreenY;
			result.elasticOffsetDip = elasticOffsetDip_;
			return result;
		}

		void RebaseDockGrip(double stableDockGripScreenY,
			double floatingVisibleBottomScreenY) noexcept
		{
			stableGripScreenY_ = stableDockGripScreenY;
			previousFloatingVisibleBottomScreenY_ = floatingVisibleBottomScreenY;
		}

		void End() noexcept
		{
			phase_ = std::abs(elasticOffsetDip_) > BarBottomDockSettleDistanceDip
				? BarBottomDockPhase::Recovering : BarBottomDockPhase::Stable;
		}

		void SetSettled() noexcept
		{
			phase_ = BarBottomDockPhase::Stable;
			elasticOffsetDip_ = 0.0;
		}

		[[nodiscard]] BarBottomDockMode Mode() const noexcept { return mode_; }
		[[nodiscard]] BarBottomDockPhase Phase() const noexcept { return phase_; }
		[[nodiscard]] double ElasticOffsetDip() const noexcept
		{
			return elasticOffsetDip_;
		}

	private:
		BarBottomDockMode mode_ = BarBottomDockMode::BottomDocked;
		BarBottomDockPhase phase_ = BarBottomDockPhase::Stable;
		double previousFloatingVisibleBottomScreenY_ = 0.0;
		double stableGripScreenY_ = 0.0;
		double elasticOffsetDip_ = 0.0;
	};

	struct BarBottomDockVerticalMapping
	{
		double baseTopDip = 0.0;
		double baseBottomDip = 0.0;
		double visualTopDip = 0.0;
		double visualBottomDip = 0.0;
		double scaleY = 1.0;
		double rigidGripYDip = 0.0;
		double rigidOverlayTranslationYDip = 0.0;

		[[nodiscard]] double MapY(double valueDip) const noexcept
		{
			return visualTopDip + (valueDip - baseTopDip) * scaleY;
		}

		[[nodiscard]] double UnmapY(double valueDip) const noexcept
		{
			return baseTopDip + (valueDip - visualTopDip)
				/ std::max(0.000001, scaleY);
		}
	};

	struct BarBottomDockHorizontalMapping
	{
		double baseLeftDip = 0.0;
		double baseRightDip = 0.0;
		double visualLeftDip = 0.0;
		double visualRightDip = 0.0;
		double scaleX = 1.0;
		double rigidGripTranslationXDip = 0.0;
		double rigidOverlayTranslationXDip = 0.0;

		[[nodiscard]] double MapX(double valueDip) const noexcept
		{
			return visualLeftDip + (valueDip - baseLeftDip) * scaleX;
		}

		[[nodiscard]] double UnmapX(double valueDip) const noexcept
		{
			return baseLeftDip + (valueDip - visualLeftDip)
				/ std::max(0.000001, scaleX);
		}
	};

	struct BarBottomDockCenteredRootPlacement
	{
		double mainCenterDip = 0.0;
		double bodyLeftDip = 0.0;
		double bodyRightDip = 0.0;
		bool valid = false;
	};

	struct BarBottomDockCenteredRootRange
	{
		double minimumDip = 0.0;
		double maximumDip = 0.0;
		bool valid = false;
	};

	[[nodiscard]] inline bool ShouldDeriveBarBottomDockCenteredRoot(
		bool verticallyDocked, BarBottomDockCenterMode centerMode,
		BarBottomDockPhase centerPhase, bool dragActive,
		bool mainBarExpanded, bool centerSpringActive,
		bool farEdgeSpringActive, bool displayTransitionActive) noexcept
	{
		return verticallyDocked
			&& centerMode == BarBottomDockCenterMode::Centered
			&& centerPhase == BarBottomDockPhase::Stable
			&& !dragActive && mainBarExpanded
			&& !centerSpringActive && !farEdgeSpringActive
			&& !displayTransitionActive;
	}

	[[nodiscard]] inline BarBottomDockCenteredRootPlacement
		ResolveBarBottomDockCenteredRootPlacement(
			double monitorCenterDip, double mainButtonVisibleWidthDip,
			double mainBarCenterOffsetDip,
			double mainBarVisibleWidthDip) noexcept
	{
		if (!std::isfinite(monitorCenterDip)
			|| !std::isfinite(mainButtonVisibleWidthDip)
			|| !std::isfinite(mainBarCenterOffsetDip)
			|| !std::isfinite(mainBarVisibleWidthDip)
			|| mainButtonVisibleWidthDip <= 0.0
			|| mainBarVisibleWidthDip <= 0.0)
			return {};

		const double relativeLeftDip = std::min(
			-mainButtonVisibleWidthDip / 2.0,
			mainBarCenterOffsetDip - mainBarVisibleWidthDip / 2.0);
		const double relativeRightDip = std::max(
			mainButtonVisibleWidthDip / 2.0,
			mainBarCenterOffsetDip + mainBarVisibleWidthDip / 2.0);
		const double mainCenterDip = monitorCenterDip
			- (relativeLeftDip + relativeRightDip) / 2.0;
		return {
			mainCenterDip,
			mainCenterDip + relativeLeftDip,
			mainCenterDip + relativeRightDip,
			true,
		};
	}

	[[nodiscard]] inline BarBottomDockCenteredRootRange
		ResolveBarBottomDockCenteredRootRange(
			double monitorCenterDip,
			double mainButtonVisibleWidthMinimumDip,
			double mainButtonVisibleWidthMaximumDip,
			double mainBarCenterOffsetMinimumDip,
			double mainBarCenterOffsetMaximumDip,
			double mainBarVisibleWidthMinimumDip,
			double mainBarVisibleWidthMaximumDip) noexcept
	{
		if (!std::isfinite(monitorCenterDip)
			|| !std::isfinite(mainButtonVisibleWidthMinimumDip)
			|| !std::isfinite(mainButtonVisibleWidthMaximumDip)
			|| !std::isfinite(mainBarCenterOffsetMinimumDip)
			|| !std::isfinite(mainBarCenterOffsetMaximumDip)
			|| !std::isfinite(mainBarVisibleWidthMinimumDip)
			|| !std::isfinite(mainBarVisibleWidthMaximumDip)
			|| mainButtonVisibleWidthMinimumDip < 0.0
			|| mainButtonVisibleWidthMaximumDip <= 0.0
			|| mainButtonVisibleWidthMinimumDip
				> mainButtonVisibleWidthMaximumDip
			|| mainBarCenterOffsetMinimumDip
				> mainBarCenterOffsetMaximumDip
			|| mainBarVisibleWidthMinimumDip < 0.0
			|| mainBarVisibleWidthMaximumDip <= 0.0
			|| mainBarVisibleWidthMinimumDip
				> mainBarVisibleWidthMaximumDip)
			return {};

		const double buttonHalfMinimum =
			mainButtonVisibleWidthMinimumDip / 2.0;
		const double buttonHalfMaximum =
			mainButtonVisibleWidthMaximumDip / 2.0;
		const double barHalfMinimum = mainBarVisibleWidthMinimumDip / 2.0;
		const double barHalfMaximum = mainBarVisibleWidthMaximumDip / 2.0;
		const double barLeftMinimum =
			mainBarCenterOffsetMinimumDip - barHalfMaximum;
		const double barLeftMaximum =
			mainBarCenterOffsetMaximumDip - barHalfMinimum;
		const double barRightMinimum =
			mainBarCenterOffsetMinimumDip + barHalfMinimum;
		const double barRightMaximum =
			mainBarCenterOffsetMaximumDip + barHalfMaximum;
		const double relativeLeftMinimum = std::min(
			-buttonHalfMaximum, barLeftMinimum);
		const double relativeLeftMaximum = std::min(
			-buttonHalfMinimum, barLeftMaximum);
		const double relativeRightMinimum = std::max(
			buttonHalfMinimum, barRightMinimum);
		const double relativeRightMaximum = std::max(
			buttonHalfMaximum, barRightMaximum);
		const double bodyCenterMinimum =
			(relativeLeftMinimum + relativeRightMinimum) / 2.0;
		const double bodyCenterMaximum =
			(relativeLeftMaximum + relativeRightMaximum) / 2.0;
		return {
			monitorCenterDip - bodyCenterMaximum,
			monitorCenterDip - bodyCenterMinimum,
			true,
		};
	}

	[[nodiscard]] inline bool ResolveBarBottomDockInitialMainBarSide(
		bool whiteboardPlacement, bool currentSide) noexcept
	{
		// 桌面首次放置固定为主按钮居左；白板入口继续沿用自己的方向。
		return whiteboardPlacement ? currentSide : true;
	}

	[[nodiscard]] inline BarBottomDockHorizontalMapping
		ResolveBarBottomDockHorizontalMapping(double baseLeftDip,
			double baseRightDip, bool opensRight,
			double rigidGripOffsetDip,
			double farEdgeOffsetDip = 0.0) noexcept
	{
		if (!std::isfinite(baseLeftDip)) baseLeftDip = 0.0;
		if (!std::isfinite(baseRightDip) || baseRightDip <= baseLeftDip)
			baseRightDip = baseLeftDip + 1.0;
		// 水平捕获带为 40 DIP；抓手和成功像素的坐标差不能被 24 DIP 弹簧保护截断。
		rigidGripOffsetDip = std::isfinite(rigidGripOffsetDip) ? rigidGripOffsetDip : 0.0;
		farEdgeOffsetDip = std::isfinite(farEdgeOffsetDip) ? farEdgeOffsetDip : 0.0;
		// 主栏近端跟随抓手，远端独立弹向稳定居中边界；主按钮本身不参与缩放。
		double visualLeft = baseLeftDip
			+ (opensRight ? rigidGripOffsetDip : farEdgeOffsetDip);
		double visualRight = baseRightDip
			+ (opensRight ? farEdgeOffsetDip : rigidGripOffsetDip);
		visualRight = std::max(visualLeft + 0.000001, visualRight);
		const double width = visualRight - visualLeft;
		return { baseLeftDip, baseRightDip, visualLeft, visualRight,
			width / (baseRightDip - baseLeftDip), rigidGripOffsetDip, 0.0 };
	}

	[[nodiscard]] inline BarBottomDockHorizontalMapping
		ResolveBarBottomDockRecoveringHorizontalMapping(double baseLeftDip,
			double baseRightDip, bool opensRight,
			double farEdgeOffsetDip) noexcept
	{
		return ResolveBarBottomDockHorizontalMapping(
			baseLeftDip, baseRightDip, opensRight, 0.0, farEdgeOffsetDip);
	}

	[[nodiscard]] inline double ResolveBarBottomDockRebasedFarEdgeOffsetDip(
		double presentedVisualFarEdgeDip, double presentedDirectTranslationPx,
		double nextDirectTranslationPx, double nextBaseFarEdgeDip,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		if (!std::isfinite(presentedVisualFarEdgeDip))
			presentedVisualFarEdgeDip = nextBaseFarEdgeDip;
		const double rebased = presentedVisualFarEdgeDip
			+ (presentedDirectTranslationPx - nextDirectTranslationPx) / zoom
			- nextBaseFarEdgeDip;
		return std::isfinite(rebased) ? rebased : 0.0;
	}

	[[nodiscard]] inline double MapBarBottomDockBodyPixelX(
		double valuePx, const BarBottomDockHorizontalMapping& mapping,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		return mapping.MapX(valuePx / zoom) * zoom;
	}

	[[nodiscard]] inline double UnmapBarBottomDockBodyPixelX(
		double valuePx, const BarBottomDockHorizontalMapping& mapping,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		return mapping.UnmapX(valuePx / zoom) * zoom;
	}

	[[nodiscard]] inline double UnmapBarBottomDockGripPixelX(
		double valuePx, const BarBottomDockHorizontalMapping& mapping,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		return valuePx - mapping.rigidGripTranslationXDip * zoom;
	}

	struct BarBottomDockHitTestPoint
	{
		double logicalX = 0.0;
		double logicalY = 0.0;
		double visualX = 0.0;
		double visualY = 0.0;
	};

	[[nodiscard]] inline double MapBarBottomDockBodyPixelY(
		double valuePx, const BarBottomDockVerticalMapping& mapping,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		return mapping.MapY(valuePx / zoom) * zoom;
	}

	[[nodiscard]] inline double UnmapBarBottomDockBodyPixelY(
		double valuePx, const BarBottomDockVerticalMapping& mapping,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		return mapping.UnmapY(valuePx / zoom) * zoom;
	}

	[[nodiscard]] inline BarBottomDockHitTestPoint
		ResolveBarBottomDockRigidHitTestPoint(double visualX, double visualY,
			const BarBottomDockHorizontalMapping& horizontalMapping,
			const BarBottomDockVerticalMapping& verticalMapping,
			double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		return {
			visualX - horizontalMapping.rigidOverlayTranslationXDip * zoom,
			visualY - verticalMapping.rigidOverlayTranslationYDip * zoom,
			visualX,
			visualY,
		};
	}

	[[nodiscard]] inline BarBottomDockHitTestPoint
		ResolveBarBottomDockBodyHitTestPointFromRigid(
			double rigidX, double rigidY,
			const BarBottomDockHorizontalMapping& horizontalMapping,
			const BarBottomDockVerticalMapping& verticalMapping,
			double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		const double visualX = std::round(rigidX
			+ horizontalMapping.rigidOverlayTranslationXDip * zoom);
		const double visualY = std::round(rigidY
			+ verticalMapping.rigidOverlayTranslationYDip * zoom);
		return {
			UnmapBarBottomDockBodyPixelX(
				visualX, horizontalMapping, zoom),
			UnmapBarBottomDockBodyPixelY(
				visualY, verticalMapping, zoom),
			visualX,
			visualY,
		};
	}

	[[nodiscard]] inline BarBottomDockHitTestPoint
		ResolveBarBottomDockGripHitTestPointFromRigid(
			double rigidX, double rigidY,
			const BarBottomDockHorizontalMapping& horizontalMapping,
			const BarBottomDockVerticalMapping& verticalMapping,
			double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		const double visualX = std::round(rigidX
			+ horizontalMapping.rigidOverlayTranslationXDip * zoom);
		const double visualY = std::round(rigidY
			+ verticalMapping.rigidOverlayTranslationYDip * zoom);
		return {
			UnmapBarBottomDockGripPixelX(
				visualX, horizontalMapping, zoom),
			UnmapBarBottomDockBodyPixelY(
				visualY, verticalMapping, zoom),
			visualX,
			visualY,
		};
	}

	struct BarBottomDockLocalLightGeometry
	{
		double centerX = 0.0;
		double centerY = 0.0;
		double radiusX = 0.0;
		double radiusY = 0.0;
	};

	[[nodiscard]] inline BarBottomDockLocalLightGeometry
		ResolveBarBottomDockBodyLocalLight(double centerXPx,
			double centerYPx, double radiusPx,
			const BarBottomDockHorizontalMapping& horizontalMapping,
			const BarBottomDockVerticalMapping& mapping,
			double zoom) noexcept
	{
		if (!std::isfinite(centerXPx)) centerXPx = 0.0;
		if (!std::isfinite(centerYPx)) centerYPx = 0.0;
		radiusPx = std::max(0.0,
			std::isfinite(radiusPx) ? radiusPx : 0.0);
		const double scaleX = std::max(0.000001,
			std::isfinite(horizontalMapping.scaleX)
				? horizontalMapping.scaleX : 1.0);
		const double scaleY = std::max(0.000001,
			std::isfinite(mapping.scaleY) ? mapping.scaleY : 1.0);
		// 光标点已经位于视觉坐标；绘制到形变组前先逆映射，避免再变换一次。
		return {
			UnmapBarBottomDockBodyPixelX(centerXPx, horizontalMapping, zoom),
			UnmapBarBottomDockBodyPixelY(centerYPx, mapping, zoom),
			radiusPx / scaleX,
			radiusPx / scaleY,
		};
	}

	[[nodiscard]] inline BarBottomDockLocalLightGeometry
		ResolveBarBottomDockRigidLocalLight(double centerXPx,
			double centerYPx, double radiusPx,
			double translationXDip, double translationYDip,
			double zoom) noexcept
	{
		if (!std::isfinite(centerXPx)) centerXPx = 0.0;
		if (!std::isfinite(centerYPx)) centerYPx = 0.0;
		radiusPx = std::max(0.0,
			std::isfinite(radiusPx) ? radiusPx : 0.0);
		zoom = NormalizeBarBottomDockZoom(zoom);
		translationXDip = std::isfinite(translationXDip)
			? translationXDip : 0.0;
		translationYDip = std::isfinite(translationYDip)
			? translationYDip : 0.0;
		return {
			centerXPx - translationXDip * zoom,
			centerYPx - translationYDip * zoom,
			radiusPx,
			radiusPx,
		};
	}

	[[nodiscard]] inline RECT TransformBarBottomDockBodyRect(
		const RECT& bounds, const BarBottomDockVerticalMapping& mapping,
		double zoom) noexcept
	{
		if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
			return {};
		const double mappedTop = MapBarBottomDockBodyPixelY(
			static_cast<double>(bounds.top), mapping, zoom);
		const double mappedBottom = MapBarBottomDockBodyPixelY(
			static_cast<double>(bounds.bottom), mapping, zoom);
		return RECT{
			bounds.left,
			static_cast<LONG>(std::floor(std::min(mappedTop, mappedBottom))),
			bounds.right,
			static_cast<LONG>(std::ceil(std::max(mappedTop, mappedBottom))),
		};
	}

	[[nodiscard]] inline RECT TransformBarBottomDockGripRect(
		RECT bounds, const BarBottomDockHorizontalMapping& horizontalMapping,
		const BarBottomDockVerticalMapping& verticalMapping,
		double zoom) noexcept
	{
		zoom = NormalizeBarBottomDockZoom(zoom);
		const double xOffset = horizontalMapping.rigidGripTranslationXDip * zoom;
		const double top = MapBarBottomDockBodyPixelY(
			bounds.top, verticalMapping, zoom);
		const double bottom = MapBarBottomDockBodyPixelY(
			bounds.bottom, verticalMapping, zoom);
		return RECT{
			static_cast<LONG>(std::floor(bounds.left + xOffset)),
			static_cast<LONG>(std::floor(std::min(top, bottom))),
			static_cast<LONG>(std::ceil(bounds.right + xOffset)),
			static_cast<LONG>(std::ceil(std::max(top, bottom))) };
	}

	[[nodiscard]] inline BarBottomDockLocalLightGeometry
		ResolveBarBottomDockRigidLocalLight(double centerXPx,
			double centerYPx, double radiusPx,
			double translationDip, double zoom) noexcept
	{
		return ResolveBarBottomDockRigidLocalLight(centerXPx, centerYPx,
			radiusPx, 0.0, translationDip, zoom);
	}

	[[nodiscard]] inline BarBottomDockLocalLightGeometry
		ResolveBarBottomDockBodyLocalLight(double centerXPx,
			double centerYPx, double radiusPx,
			const BarBottomDockVerticalMapping& mapping,
			double zoom) noexcept
	{
		return ResolveBarBottomDockBodyLocalLight(centerXPx, centerYPx,
			radiusPx, BarBottomDockHorizontalMapping{}, mapping, zoom);
	}

	[[nodiscard]] inline RECT TransformBarBottomDockBodyRect(
		const RECT& bounds, const BarBottomDockHorizontalMapping& horizontal,
		const BarBottomDockVerticalMapping& vertical, double zoom) noexcept
	{
		if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
			return {};
		const double mappedLeft = MapBarBottomDockBodyPixelX(
			static_cast<double>(bounds.left), horizontal, zoom);
		const double mappedRight = MapBarBottomDockBodyPixelX(
			static_cast<double>(bounds.right), horizontal, zoom);
		const RECT verticalBounds = TransformBarBottomDockBodyRect(
			bounds, vertical, zoom);
		return RECT{
			static_cast<LONG>(std::floor(std::min(mappedLeft, mappedRight))),
			verticalBounds.top,
			static_cast<LONG>(std::ceil(std::max(mappedLeft, mappedRight))),
			verticalBounds.bottom,
		};
	}

	[[nodiscard]] inline RECT TranslateBarBottomDockRigidRect(
		const RECT& bounds, double translationDip, double zoom) noexcept
	{
		if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
			return {};
		zoom = NormalizeBarBottomDockZoom(zoom);
		const double translationPx = (std::isfinite(translationDip)
			? translationDip : 0.0) * zoom;
		return RECT{
			bounds.left,
			static_cast<LONG>(std::floor(bounds.top + translationPx)),
			bounds.right,
			static_cast<LONG>(std::ceil(bounds.bottom + translationPx)),
		};
	}

	[[nodiscard]] inline RECT TranslateBarBottomDockRigidRect(
		const RECT& bounds, double translationXDip,
		double translationYDip, double zoom) noexcept
	{
		RECT translated = TranslateBarBottomDockRigidRect(
			bounds, translationYDip, zoom);
		if (translated.right <= translated.left) return {};
		zoom = NormalizeBarBottomDockZoom(zoom);
		const double translationPx = (std::isfinite(translationXDip)
			? translationXDip : 0.0) * zoom;
		translated.left = static_cast<LONG>(std::floor(
			translated.left + translationPx));
		translated.right = static_cast<LONG>(std::ceil(
			translated.right + translationPx));
		return translated;
	}

	[[nodiscard]] inline RECT ResolveBarBottomDockVisualEnvelope(
		const RECT& bounds, double zoom,
		double horizontalOutsetDip = BarBottomDockVisualLimitDip,
		double verticalOutsetDip = BarBottomDockVisualLimitDip) noexcept
	{
		if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
			return {};
		zoom = NormalizeBarBottomDockZoom(zoom);
		const LONG horizontalPadding = static_cast<LONG>(
			std::ceil(std::max(BarBottomDockVisualLimitDip,
				std::isfinite(horizontalOutsetDip) ? horizontalOutsetDip : 0.0) * zoom));
		const LONG verticalPadding = static_cast<LONG>(
			std::ceil(std::max(BarBottomDockVisualLimitDip,
				std::isfinite(verticalOutsetDip) ? verticalOutsetDip : 0.0) * zoom));
		return RECT{ bounds.left - horizontalPadding,
			bounds.top - verticalPadding, bounds.right + horizontalPadding,
			bounds.bottom + verticalPadding };
	}

	[[nodiscard]] inline RECT ResolveBarBottomDockCapacityEnvelope(
		const RECT& elasticBaseBounds, double zoom,
		double horizontalOutsetDip = BarBottomDockVisualLimitDip,
		double verticalOutsetDip = BarBottomDockVisualLimitDip) noexcept
	{
		if (elasticBaseBounds.right <= elasticBaseBounds.left
			|| elasticBaseBounds.bottom <= elasticBaseBounds.top)
			return {};
		// 始终从未形变基线扩完整包络，不能从端点映射后的 bounds 反推。
		return ResolveBarBottomDockVisualEnvelope(
			elasticBaseBounds, zoom, horizontalOutsetDip, verticalOutsetDip);
	}

	[[nodiscard]] inline bool
		ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
			bool downwardDetachSeen, double rawMainCenterScreenY,
			double maximumVisibleCenterScreenY, double dockCenterScreenY,
			double toleranceScreenPx = 0.5) noexcept
	{
		if (!downwardDetachSeen || !std::isfinite(rawMainCenterScreenY)
			|| !std::isfinite(maximumVisibleCenterScreenY)
			|| !std::isfinite(dockCenterScreenY))
			return false;
		toleranceScreenPx = std::isfinite(toleranceScreenPx)
			? std::max(0.0, toleranceScreenPx) : 0.5;
		return rawMainCenterScreenY > maximumVisibleCenterScreenY
			&& maximumVisibleCenterScreenY
				<= dockCenterScreenY + toleranceScreenPx;
	}

	// 所有写者先独占奇数 serial，避免两个 fetch_add 把写入中的 tuple 误标为稳定。
	inline void BeginBarBottomDockTransition(
		std::atomic<unsigned long long>& serial) noexcept
	{
		auto expected = serial.load(std::memory_order_acquire);
		for (;;)
		{
			if ((expected & 1ULL) == 0 && serial.compare_exchange_weak(
				expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire))
				return;
			expected = serial.load(std::memory_order_acquire);
		}
	}

	[[nodiscard]] inline bool TryBeginBarBottomDockFrameTransition(
		std::atomic<unsigned long long>& serial,
		unsigned long long consumedSerial, bool dragActive) noexcept
	{
		// 渲染帧只能归位自己消费过的非拖动状态；不能替更新的抓取或捕获确认屏障。
		if (dragActive || (consumedSerial & 1ULL) != 0) return false;
		return serial.compare_exchange_strong(consumedSerial, consumedSerial + 1,
			std::memory_order_acq_rel, std::memory_order_acquire);
	}

	inline unsigned long long FinishBarBottomDockTransition(
		std::atomic<unsigned long long>& serial,
		std::atomic<unsigned long long>& deferredSerial, bool deferWindowMove) noexcept
	{
		const auto next = serial.load(std::memory_order_relaxed) + 1;
		if (deferWindowMove) deferredSerial.store(next, std::memory_order_relaxed);
		serial.store(next, std::memory_order_release);
		return next;
	}

	[[nodiscard]] constexpr bool ShouldDeferBarBottomDockReleaseHandoff(
		bool dragActive, bool directDragStillOwnsWindow,
		bool directTranslationPending) noexcept
	{
		return !dragActive && directDragStillOwnsWindow
			&& directTranslationPending;
	}

	[[nodiscard]] inline POINT ResolveBarBottomDockFrameTranslation(
		unsigned long long frameTransitionSerial,
		unsigned long long observedSerialBefore,
		unsigned long long observedSerialAfter,
		POINT latestTranslation, POINT presentedTranslation) noexcept
	{
		// serial 为偶数且提交前后未变化时，最新位移仍属于当前形态。
		const bool frameStillCurrent = (frameTransitionSerial & 1ULL) == 0
			&& observedSerialBefore == frameTransitionSerial
			&& observedSerialAfter == frameTransitionSerial;
		return frameStillCurrent ? latestTranslation : presentedTranslation;
	}

	struct BarBottomDockFramePresentationDecision
	{
		POINT translation{};
		bool deferred = false;
	};

	[[nodiscard]] inline BarBottomDockFramePresentationDecision
		ResolveBarBottomDockFramePresentation(
			unsigned long long frameTransitionSerial, POINT frameTranslation,
			unsigned long long observedSerialBefore,
			unsigned long long observedSerialAfter,
			unsigned long long deferredTransitionSerial,
			unsigned long long presentedTransitionSerial,
			POINT latestTranslation, POINT presentedTranslation) noexcept
	{
		// 尚未完成的发布无法区分普通采样与新形态，不能提交混合 tuple。
		if ((frameTransitionSerial & 1ULL) != 0
			|| (observedSerialBefore & 1ULL) != 0
			|| observedSerialBefore != observedSerialAfter
			|| deferredTransitionSerial > frameTransitionSerial)
			return { presentedTranslation, true };
		// 首张转换位图已包含此屏障：使用配套位移，后续普通采样不能使其饥饿。
		if (deferredTransitionSerial > presentedTransitionSerial)
			return { frameTranslation, false };
		return { ResolveBarBottomDockFrameTranslation(frameTransitionSerial,
			observedSerialBefore, observedSerialAfter,
			latestTranslation, presentedTranslation), false };
	}

	[[nodiscard]] inline BarBottomDockVerticalMapping
		ResolveBarBottomDockVerticalMapping(double baseTopDip,
			double baseBottomDip, double elasticOffsetDip,
			double captureBottomOffsetDip = 0.0,
		bool preserveCaptureBottomOffset = false) noexcept
	{
		if (!std::isfinite(baseTopDip)) baseTopDip = 0.0;
		if (!std::isfinite(baseBottomDip) || baseBottomDip <= baseTopDip)
			baseBottomDip = baseTopDip + 1.0;
		elasticOffsetDip = std::clamp(
			std::isfinite(elasticOffsetDip) ? elasticOffsetDip : 0.0,
			-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
		captureBottomOffsetDip = std::isfinite(captureBottomOffsetDip) ? captureBottomOffsetDip : 0.0;
		// 仅成功像素的捕获初值可超出常规保护，不能在映射阶段再次截断。
		if (!preserveCaptureBottomOffset)
			captureBottomOffsetDip = std::clamp(captureBottomOffsetDip,
				-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
		// 捕获首帧下端点保留原屏幕位置，再独立弹向 dock 线。
		const double visualBottom = baseBottomDip + captureBottomOffsetDip;
		const double visualTop = std::min(visualBottom - 0.000001,
			baseTopDip + elasticOffsetDip);
		const double baseHeight = baseBottomDip - baseTopDip;
		return {
			baseTopDip,
			baseBottomDip,
			visualTop,
			visualBottom,
			(visualBottom - visualTop) / baseHeight,
			(baseTopDip + baseBottomDip) / 2.0 + elasticOffsetDip,
			elasticOffsetDip,
		};
	}

	[[nodiscard]] inline BarBottomDockVerticalMapping
		ResolveBarBottomDockRecoveringVerticalMapping(double baseTopDip,
			double baseBottomDip, double elasticOffsetDip,
			double captureBottomOffsetDip = 0.0,
		bool preserveCaptureBottomOffset = false) noexcept
	{
		if (!std::isfinite(baseTopDip)) baseTopDip = 0.0;
		if (!std::isfinite(baseBottomDip) || baseBottomDip <= baseTopDip)
			baseBottomDip = baseTopDip + 1.0;
		elasticOffsetDip = std::clamp(
			std::isfinite(elasticOffsetDip) ? elasticOffsetDip : 0.0,
			-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
		captureBottomOffsetDip = std::isfinite(captureBottomOffsetDip) ? captureBottomOffsetDip : 0.0;
		// 仅成功像素的捕获初值可超出常规保护，不能在映射阶段再次截断。
		if (!preserveCaptureBottomOffset)
			captureBottomOffsetDip = std::clamp(captureBottomOffsetDip,
				-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
		const double visualBottom = std::max(baseTopDip + 0.000001,
			baseBottomDip - elasticOffsetDip + captureBottomOffsetDip);
		const double baseHeight = baseBottomDip - baseTopDip;
		return {
			baseTopDip,
			baseBottomDip,
			baseTopDip,
			visualBottom,
			(visualBottom - baseTopDip) / baseHeight,
			(baseTopDip + baseBottomDip) / 2.0,
			0.0,
		};
	}

	struct BarBottomDockSpringState
	{
		double positionDip = 0.0;
		double velocityDipPerSecond = 0.0;
	};

	[[nodiscard]] inline bool SeedBarBottomDockCaptureBottom(
		BarBottomDockSpringState& spring,
		BarBottomDockMode presentedMode, BarBottomDockMode nextMode,
		double presentedBottomScreenY, double nextBaseBottomScreenY,
		double zoom, bool animationsEnabled) noexcept
	{
		if (presentedMode != BarBottomDockMode::Floating
			|| nextMode != BarBottomDockMode::BottomDocked) return false;
		// 捕获从成功像素播入；被跳过的候选帧不能消费这次交接。
		const double offset = (presentedBottomScreenY - nextBaseBottomScreenY)
			/ NormalizeBarBottomDockZoom(zoom);
		spring.positionDip = animationsEnabled && std::isfinite(offset) ? offset : 0.0;
		spring.velocityDipPerSecond = 0.0;
		return true;
	}

	struct BarBottomDockSpringResult
	{
		double positionDip = 0.0;
		double velocityDipPerSecond = 0.0;
		bool active = false;
	};

	[[nodiscard]] inline BarBottomDockSpringResult AdvanceBarBottomDockSpring(
		BarBottomDockSpringState& state, double targetDip, double dtSeconds,
		bool animationsEnabled = true, bool preservePresentedOffset = false) noexcept
	{
		if (!std::isfinite(targetDip)) targetDip = 0.0;
		targetDip = std::clamp(targetDip,
			-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
		if (!std::isfinite(state.positionDip)) state.positionDip = targetDip;
		if (!std::isfinite(state.velocityDipPerSecond))
			state.velocityDipPerSecond = 0.0;
		if (!animationsEnabled)
		{
			state.positionDip = targetDip;
			state.velocityDipPerSecond = 0.0;
			return { state.positionDip, state.velocityDipPerSecond, false };
		}

		// 水平交接允许从真实旧像素连续衰减；保护只阻止进一步向外发散。
		const double positionLimitDip = preservePresentedOffset
			? std::max(BarBottomDockVisualLimitDip, std::abs(state.positionDip))
			: BarBottomDockVisualLimitDip;
		dtSeconds = std::clamp(std::isfinite(dtSeconds) ? dtSeconds : 0.0,
			0.0, BarBottomDockSpringMaxDtSeconds);
		double remaining = dtSeconds;
		while (remaining > 0.0)
		{
			const double step = std::min(remaining, BarBottomDockSpringStepSeconds);
			const double acceleration = BarBottomDockSpringOmega
				* BarBottomDockSpringOmega * (targetDip - state.positionDip)
				- 2.0 * BarBottomDockSpringDampingRatio
				* BarBottomDockSpringOmega * state.velocityDipPerSecond;
			state.velocityDipPerSecond += acceleration * step;
			state.positionDip += state.velocityDipPerSecond * step;
			if (state.positionDip > positionLimitDip)
			{
				state.positionDip = positionLimitDip;
				if (state.velocityDipPerSecond > 0.0)
					state.velocityDipPerSecond = 0.0;
			}
			else if (state.positionDip < -positionLimitDip)
			{
				state.positionDip = -positionLimitDip;
				if (state.velocityDipPerSecond < 0.0)
					state.velocityDipPerSecond = 0.0;
			}
			remaining -= step;
		}

		const bool settled = std::abs(state.positionDip - targetDip)
			<= BarBottomDockSettleDistanceDip
			&& std::abs(state.velocityDipPerSecond)
			<= BarBottomDockSettleVelocityDipPerSecond;
		if (settled)
		{
			state.positionDip = targetDip;
			state.velocityDipPerSecond = 0.0;
		}
		return { state.positionDip, state.velocityDipPerSecond, !settled };
	}
	struct BarBottomDockGrabAnchor
	{
		double normalizedY = 0.5;
		bool valid = false;
	};

	[[nodiscard]] inline BarBottomDockGrabAnchor ResolveBarBottomDockGrabAnchor(
		const BarBottomDockVerticalMapping& mapping, double rootYDip, double actualHeightDip,
		double pointerScreenY, double screenOriginY, double translationY, double zoom) noexcept
	{
		if (!std::isfinite(actualHeightDip) || actualHeightDip <= 0.0
			|| !std::isfinite(rootYDip) || !std::isfinite(pointerScreenY)) return {};
		const double logical = mapping.UnmapY((pointerScreenY - screenOriginY - translationY)
			/ NormalizeBarBottomDockZoom(zoom));
		const double q = (logical - rootYDip) / actualHeightDip + 0.5;
		return { q, std::isfinite(q) };
	}

	struct BarBottomDockShiftedShape
	{
		double topDip = 0.0;
		double bottomDip = 0.0;
	};

	[[nodiscard]] inline BarBottomDockShiftedShape ResolveBarBottomDockShiftedShape(
		const BarBottomDockVerticalMapping& presented, double rootYDip,
		double heightDip, double normalizedGrabY,
		double previousZoom, double currentGrabDip, double currentZoom) noexcept
	{
		// 先把成功形状移到本次抓点；屏幕原点和直移在相减时抵消，不冻结旧底边。
		const double grab = presented.MapY(rootYDip + (normalizedGrabY - 0.5) * heightDip);
		const double ratio = NormalizeBarBottomDockZoom(previousZoom) / NormalizeBarBottomDockZoom(currentZoom);
		return { currentGrabDip + (presented.visualTopDip - grab) * ratio,
			currentGrabDip + (presented.visualBottomDip - grab) * ratio };
	}

	struct BarBottomDockAnchoredMappingResult
	{
		BarBottomDockVerticalMapping mapping{};
		double effectiveGrabDip = 0.0;
		bool constrained = false;
	};

	[[nodiscard]] inline BarBottomDockAnchoredMappingResult ResolveBarBottomDockAnchoredMapping(
		double baseTop, double baseBottom, double logicalGrab, double desiredGrab,
		double desiredBottom, double fallbackHeight, bool pinBottom,
		double minimumY, double maximumY) noexcept
	{
		constexpr double minimumScale = 0.000001;
		const double baseHeight = std::max(minimumScale, baseBottom - baseTop);
		const double minimumHeight = baseHeight * minimumScale;
		const double r = (logicalGrab - baseTop) / baseHeight;
		const double safeMin = std::isfinite(minimumY) ? minimumY : baseTop;
		const double safeMax = std::max(safeMin + minimumHeight,
			std::isfinite(maximumY) ? maximumY : baseBottom);
		const double point = std::clamp(std::isfinite(desiredGrab) ? desiredGrab : logicalGrab, safeMin, safeMax);
		double height = std::max(minimumHeight, std::isfinite(fallbackHeight) ? fallbackHeight : baseHeight);
		bool constrained = point != desiredGrab;
		if (pinBottom)
		{
			const double solved = r != 1.0 ? (desiredBottom - point) / (1.0 - r) : -1.0;
			if (std::isfinite(solved) && solved > 0.0) height = solved;
			else if (r != 1.0) { height = minimumHeight; constrained = true; }
			else constrained = constrained || desiredBottom != point;
		}
		// 抓点/底端约束不可兼得时保留抓点；只以真实屏幕空间和正高度限制形状。
		double available = safeMax - safeMin;
		if (r > 0.0) available = std::min(available, (point - safeMin) / r);
		if (r < 1.0) available = std::min(available, (safeMax - point) / (1.0 - r));
		const double bounded = std::clamp(height, minimumHeight, std::max(minimumHeight, available));
		constrained = constrained || bounded != height;
		const double top = point - r * bounded;
		const double bottom = top + bounded;
		return { { baseTop, baseBottom, top, bottom, bounded / baseHeight,
			point, top - baseTop }, point, constrained };
	}

	[[nodiscard]] inline bool ShouldRecoverBarBottomDockOnRelease(
		const BarBottomDockVerticalMapping& presented, bool recoveryActive,
		double presentedGripOffset, double inputOffset) noexcept
	{
		return recoveryActive || std::abs(presented.scaleY - 1.0) > 0.000001
			|| std::abs(presentedGripOffset) > BarBottomDockSettleDistanceDip
			|| std::abs(inputOffset) > BarBottomDockSettleDistanceDip;
	}

	struct BarBottomDockVerticalFrameInput
	{
		BarBottomDockMode mode = BarBottomDockMode::Floating;
		bool dragging = false, anchorValid = false, handoff = false, animationsEnabled = true;
		double baseTop = 0.0, baseBottom = 1.0, targetBottom = 1.0, logicalGrab = 0.5, effectiveGrab = 0.5;
		double minimumY = 0.0, maximumY = 1.0, dtSeconds = 0.0;
		BarBottomDockShiftedShape previousShape{};
	};
	struct BarBottomDockVerticalFrameResult
	{
		BarBottomDockAnchoredMappingResult geometry{};
		bool seeded = false, gripActive = false, shapeActive = false;
	};

	[[nodiscard]] inline BarBottomDockVerticalFrameResult AdvanceBarBottomDockVerticalFrame(
		BarBottomDockSpringState& grip, BarBottomDockSpringState& shape,
		const BarBottomDockVerticalFrameInput& input) noexcept
	{
		BarBottomDockVerticalFrameResult result;
		const double baseHeight = input.baseBottom - input.baseTop;
		if (!input.anchorValid)
		{
			grip = {}; shape = {};
			const double shift = input.mode == BarBottomDockMode::BottomDocked ? input.targetBottom - input.baseBottom : 0.0;
			result.geometry = { { input.baseTop, input.baseBottom, input.baseTop + shift, input.baseBottom + shift,
				1.0, input.logicalGrab + shift, shift }, input.logicalGrab + shift, false };
			return result;
		}
		const double restGripOffset = input.mode == BarBottomDockMode::BottomDocked ? input.targetBottom - input.baseBottom : 0.0;
		if (input.handoff)
		{
			// 新坐标所有权只接住成功形状，不继承另一坐标含义下的有限差分速度。
			grip = { input.effectiveGrab - input.logicalGrab, 0.0 };
			shape = { input.mode == BarBottomDockMode::BottomDocked
				? input.previousShape.bottomDip - input.targetBottom
				: input.previousShape.bottomDip - input.previousShape.topDip - baseHeight, 0.0 };
			result.seeded = true;
		}
		if (input.dragging) grip = { input.effectiveGrab - input.logicalGrab, 0.0 };
		else if (!result.seeded)
		{
			// 恢复目标是贴底后的正常形状，实际高度与根节点基准不同时也不留残差。
			grip.positionDip -= restGripOffset;
			result.gripActive = AdvanceBarBottomDockSpring(grip, 0.0, input.dtSeconds,
				input.animationsEnabled, true).active;
			grip.positionDip += restGripOffset;
		}
		if (!result.seeded)
			result.shapeActive = AdvanceBarBottomDockSpring(shape, 0.0, input.dtSeconds,
				input.animationsEnabled, true).active;
		if (!input.animationsEnabled)
		{
			shape = {};
			if (!input.dragging) grip = { restGripOffset, 0.0 };
		}
		result.gripActive = result.gripActive || (!input.dragging && std::abs(grip.positionDip - restGripOffset) > BarBottomDockSettleDistanceDip);
		result.shapeActive = result.shapeActive || std::abs(shape.positionDip) > BarBottomDockSettleDistanceDip;
		const double point = input.dragging ? input.effectiveGrab : input.logicalGrab + grip.positionDip;
		result.geometry = ResolveBarBottomDockAnchoredMapping(input.baseTop, input.baseBottom,
			input.logicalGrab, point, input.targetBottom + shape.positionDip,
			input.mode == BarBottomDockMode::Floating ? baseHeight + shape.positionDip : baseHeight,
			input.mode == BarBottomDockMode::BottomDocked, input.minimumY, input.maximumY);
		return result;
	}

	[[nodiscard]] inline double ResolveBarBottomDockVerticalOutset(
		const BarBottomDockVerticalFrameInput& input, const BarBottomDockSpringState& grip,
		const BarBottomDockSpringState& shape, const BarBottomDockVerticalMapping& current) noexcept
	{
		double outset = BarBottomDockVisualLimitDip;
		auto Include = [&](const BarBottomDockVerticalMapping& mapping)
			{
				outset = std::max({ outset, std::abs(mapping.visualTopDip - input.baseTop),
					std::abs(mapping.visualBottomDip - input.baseBottom) });
			};
		Include(current);
		if (!input.anchorValid) return outset;
		// 能量范围包住现有速度的恢复扫掠；抓住时还预留完整输入带的真实求解端点。
		const double gripRange = std::max(input.dragging ? BarBottomDockVisualLimitDip : 0.0,
			std::hypot(grip.positionDip, grip.velocityDipPerSecond / BarBottomDockSpringOmega) * 1.06);
		const double shapeRange = std::hypot(shape.positionDip, shape.velocityDipPerSecond / BarBottomDockSpringOmega) * 1.06;
		for (double e : { -gripRange, gripRange })
			for (double c : { -shapeRange, shapeRange })
				Include(ResolveBarBottomDockAnchoredMapping(input.baseTop, input.baseBottom, input.logicalGrab,
					input.logicalGrab + e, input.targetBottom + c,
					input.baseBottom - input.baseTop + (input.mode == BarBottomDockMode::Floating ? c : 0.0),
					input.mode == BarBottomDockMode::BottomDocked, input.minimumY, input.maximumY).mapping);
		return outset;
	}

	inline void RebaseBarBottomDockMapping(BarBottomDockVerticalMapping& vertical,
		BarBottomDockHorizontalMapping& horizontal, double dx, double dy) noexcept
	{
		vertical.baseTopDip += dy; vertical.baseBottomDip += dy;
		vertical.visualTopDip += dy; vertical.visualBottomDip += dy; vertical.rigidGripYDip += dy;
		horizontal.baseLeftDip += dx; horizontal.baseRightDip += dx;
		horizontal.visualLeftDip += dx; horizontal.visualRightDip += dx;
	}

}
