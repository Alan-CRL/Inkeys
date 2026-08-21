#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace Inkeys::UI::Bar
{
		inline constexpr double BarBottomDockThresholdDip = 20.0;
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

	[[nodiscard]] inline bool ResolveBarBottomDockIndicatorTarget(
		BarBottomDockMode currentMode, bool dragActive,
		bool mainBarExpanded) noexcept
	{
		return currentMode == BarBottomDockMode::BottomDocked
			&& dragActive && mainBarExpanded;
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
			const BarBottomDockVerticalMapping& mapping,
			double zoom) noexcept
	{
		if (!std::isfinite(centerXPx)) centerXPx = 0.0;
		if (!std::isfinite(centerYPx)) centerYPx = 0.0;
		radiusPx = std::max(0.0,
			std::isfinite(radiusPx) ? radiusPx : 0.0);
		const double scaleY = std::max(0.000001,
			std::isfinite(mapping.scaleY) ? mapping.scaleY : 1.0);
		// 光标点已经位于视觉坐标；绘制到形变组前先逆映射，避免再变换一次。
		return {
			centerXPx,
			UnmapBarBottomDockBodyPixelY(centerYPx, mapping, zoom),
			radiusPx,
			radiusPx / scaleY,
		};
	}

	[[nodiscard]] inline BarBottomDockLocalLightGeometry
		ResolveBarBottomDockRigidLocalLight(double centerXPx,
			double centerYPx, double radiusPx,
			double translationDip, double zoom) noexcept
	{
		if (!std::isfinite(centerXPx)) centerXPx = 0.0;
		if (!std::isfinite(centerYPx)) centerYPx = 0.0;
		radiusPx = std::max(0.0,
			std::isfinite(radiusPx) ? radiusPx : 0.0);
		zoom = NormalizeBarBottomDockZoom(zoom);
		translationDip = std::isfinite(translationDip)
			? translationDip : 0.0;
		return {
			centerXPx,
			centerYPx - translationDip * zoom,
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

	[[nodiscard]] inline RECT ResolveBarBottomDockVisualEnvelope(
		const RECT& bounds, double zoom) noexcept
	{
		if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
			return {};
		zoom = NormalizeBarBottomDockZoom(zoom);
		const LONG verticalPadding = static_cast<LONG>(
			std::ceil(BarBottomDockVisualLimitDip * zoom));
		return RECT{ bounds.left, bounds.top - verticalPadding,
			bounds.right, bounds.bottom + verticalPadding };
	}

	[[nodiscard]] inline RECT ResolveBarBottomDockCapacityEnvelope(
		const RECT& elasticBaseBounds, double zoom) noexcept
	{
		if (elasticBaseBounds.right <= elasticBaseBounds.left
			|| elasticBaseBounds.bottom <= elasticBaseBounds.top)
			return {};
		// 始终从未形变基线扩完整包络，不能从端点映射后的 bounds 反推。
		return ResolveBarBottomDockVisualEnvelope(elasticBaseBounds, zoom);
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

	[[nodiscard]] inline POINT ResolveBarBottomDockFrameTranslation(
		unsigned long long frameTransitionSerial,
		unsigned long long observedSerialBefore,
		unsigned long long observedSerialAfter,
		POINT latestTranslation, POINT frameTranslation) noexcept
	{
		// serial 为偶数且提交前后未变化时，最新位移仍属于当前形态。
		const bool frameStillCurrent = (frameTransitionSerial & 1ULL) == 0
			&& observedSerialBefore == frameTransitionSerial
			&& observedSerialAfter == frameTransitionSerial;
		return frameStillCurrent ? latestTranslation : frameTranslation;
	}

	[[nodiscard]] inline BarBottomDockVerticalMapping
		ResolveBarBottomDockVerticalMapping(double baseTopDip,
			double baseBottomDip, double elasticOffsetDip,
			double captureBottomOffsetDip = 0.0) noexcept
	{
		if (!std::isfinite(baseTopDip)) baseTopDip = 0.0;
		if (!std::isfinite(baseBottomDip) || baseBottomDip <= baseTopDip)
			baseBottomDip = baseTopDip + 1.0;
		elasticOffsetDip = std::clamp(
			std::isfinite(elasticOffsetDip) ? elasticOffsetDip : 0.0,
			-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
		captureBottomOffsetDip = std::clamp(
			std::isfinite(captureBottomOffsetDip)
				? captureBottomOffsetDip : 0.0,
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
			double captureBottomOffsetDip = 0.0) noexcept
	{
		if (!std::isfinite(baseTopDip)) baseTopDip = 0.0;
		if (!std::isfinite(baseBottomDip) || baseBottomDip <= baseTopDip)
			baseBottomDip = baseTopDip + 1.0;
		elasticOffsetDip = std::clamp(
			std::isfinite(elasticOffsetDip) ? elasticOffsetDip : 0.0,
			-BarBottomDockVisualLimitDip, BarBottomDockVisualLimitDip);
		captureBottomOffsetDip = std::clamp(
			std::isfinite(captureBottomOffsetDip)
				? captureBottomOffsetDip : 0.0,
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

	struct BarBottomDockSpringResult
	{
		double positionDip = 0.0;
		double velocityDipPerSecond = 0.0;
		bool active = false;
	};

	[[nodiscard]] inline BarBottomDockSpringResult AdvanceBarBottomDockSpring(
		BarBottomDockSpringState& state, double targetDip, double dtSeconds,
		bool animationsEnabled = true) noexcept
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
			if (state.positionDip > BarBottomDockVisualLimitDip)
			{
				state.positionDip = BarBottomDockVisualLimitDip;
				if (state.velocityDipPerSecond > 0.0)
					state.velocityDipPerSecond = 0.0;
			}
			else if (state.positionDip < -BarBottomDockVisualLimitDip)
			{
				state.positionDip = -BarBottomDockVisualLimitDip;
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
}
