module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <windows.h>

#include <vector>

export module Inkeys.UI.Bar.SurfaceLayout;

export import Inkeys.UI.Bar.Metrics;

export namespace Inkeys::UI::Bar
{
	struct BarSurfacePoint
	{
		double x = 0.0;
		double y = 0.0;
	};

	[[nodiscard]] constexpr BarSurfacePoint
		ResolveBarSurfaceScreenPoint(double screenX, double screenY,
			LONG logicalLeft, LONG logicalTop, LONG presentationOutset) noexcept
	{
		return {
			screenX - static_cast<double>(logicalLeft) + presentationOutset,
			screenY - static_cast<double>(logicalTop) + presentationOutset,
		};
	}

	// Surface 布局只保存 DIP 语义；每个 target 再独立转换为像素。
	struct BarSurfaceDipRect
	{
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;

		[[nodiscard]] double Width() const noexcept
		{
			return right - left;
		}
		[[nodiscard]] double Height() const noexcept
		{
			return bottom - top;
		}
	};

	enum class BarSurfaceHorizontalAnchor : std::uint8_t
	{
		Left,
		Right,
	};

	struct BarSurfaceHorizontalGroupSpec
	{
		RECT screenBounds{};
		float dpiScale = 1.0F;
		double edgeMarginDip = BarButtonGapDip;
		double bottomMarginDip = BarButtonGapDip;
		double itemSizeDip = BarButtonTwoSideDip;
		double gapDip = BarButtonGapDip;
		BarSurfaceHorizontalAnchor anchor = BarSurfaceHorizontalAnchor::Left;
	};

	struct BarSurfaceHorizontalGroupLayout
	{
		RECT logicalBounds{};
		float dpiScale = 1.0F;
		double widthDip = 0.0;
		double heightDip = 0.0;
		std::vector<BarSurfaceDipRect> widgets;
	};

	// 纯布局入口由 Scene 和 headless tests 共用，避免 Whiteboard 复制几何。
	[[nodiscard]] inline BarSurfaceHorizontalGroupLayout
		ResolveBarSurfaceHorizontalGroupLayout(
			const BarSurfaceHorizontalGroupSpec& group,
			std::size_t widgetCount) noexcept
	{
		BarSurfaceHorizontalGroupLayout result;
		if (widgetCount == 0) return result;
		const RECT screen{
			(std::min)(group.screenBounds.left, group.screenBounds.right),
			(std::min)(group.screenBounds.top, group.screenBounds.bottom),
			(std::max)(group.screenBounds.left, group.screenBounds.right),
			(std::max)(group.screenBounds.top, group.screenBounds.bottom) };
		if (screen.right <= screen.left || screen.bottom <= screen.top)
			return result;
		auto normalizeDip = [](double value, double fallback) noexcept
		{
			return std::isfinite(value) && value >= 0.0 ? value : fallback;
		};
		result.dpiScale = std::isfinite(group.dpiScale) && group.dpiScale > 0.0F
			? (std::clamp)(group.dpiScale, 0.5F, 4.0F) : 1.0F;
		const double edgeDip = normalizeDip(group.edgeMarginDip, BarButtonGapDip);
		const double bottomDip = normalizeDip(group.bottomMarginDip, BarButtonGapDip);
		const double itemDip = normalizeDip(group.itemSizeDip, BarButtonTwoSideDip);
		const double gapDip = normalizeDip(group.gapDip, BarButtonGapDip);
		result.widthDip = edgeDip * 2.0
			+ itemDip * static_cast<double>(widgetCount)
			+ gapDip * static_cast<double>(widgetCount - 1);
		result.heightDip = edgeDip * 2.0 + itemDip;
		const LONG widthPixels = (std::max)(1L, static_cast<LONG>(
			std::lround(result.widthDip * result.dpiScale)));
		const LONG heightPixels = (std::max)(1L, static_cast<LONG>(
			std::lround(result.heightDip * result.dpiScale)));
		const LONG edgePixels = static_cast<LONG>(std::lround(
			edgeDip * result.dpiScale));
		const LONG bottomPixels = static_cast<LONG>(std::lround(
			bottomDip * result.dpiScale));
		const LONG left = group.anchor == BarSurfaceHorizontalAnchor::Left
			? screen.left + edgePixels
			: screen.right - edgePixels - widthPixels;
		const LONG top = screen.bottom - bottomPixels - heightPixels;
		result.logicalBounds = RECT{ left, top,
			left + widthPixels, top + heightPixels };
		result.widgets.reserve(widgetCount);
		for (std::size_t index = 0; index < widgetCount; ++index)
		{
			const double x = edgeDip
				+ static_cast<double>(index) * (itemDip + gapDip);
			result.widgets.push_back(BarSurfaceDipRect{
				x, edgeDip, x + itemDip, edgeDip + itemDip });
		}
		return result;
	}
}
