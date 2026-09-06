#pragma once

#include "Setting.Layout.h"

namespace Inkeys::UI::Setting::Design
{
	inline constexpr float OpenPaneWidth = 248.0F;
	inline constexpr float CompactPaneWidth = 48.0F;
	inline constexpr float NavigationBreakpoint = 900.0F;
	inline constexpr float PageMaximumWidth = 1040.0F;
	inline constexpr float RowGap = 4.0F;
	inline constexpr float SectionGap = 28.0F;

	struct NavigationState
	{
		bool desktopCompact = false;
		bool narrow = false;
		bool overlayOpen = false;

		void Resize(float widthDip) noexcept
		{
			const bool nextNarrow = widthDip < NavigationBreakpoint;
			if (nextNarrow != narrow) overlayOpen = false;
			narrow = nextNarrow;
		}
		void Toggle() noexcept
		{
			if (narrow) overlayOpen = !overlayOpen;
			else desktopCompact = !desktopCompact;
		}
		void DismissOverlay() noexcept { overlayOpen = false; }
		[[nodiscard]] bool Expanded() const noexcept
		{
			return narrow ? overlayOpen : !desktopCompact;
		}
	};

	struct ShellGeometry
	{
		LayoutRect pane;
		LayoutRect content;
		float gutter = 24.0F;
		float pageWidth = 0.0F;
		float pageOffset = 0.0F;
	};

	// 输入和输出均为 DIP；只有最终绘制与命中坐标转换到物理像素。
	[[nodiscard]] inline ShellGeometry ResolveShellGeometry(
		float widthDip, float heightDip, const NavigationState& state) noexcept
	{
		const float width = std::isfinite(widthDip) ? (std::max)(0.0F, widthDip) : 0.0F;
		const float height = std::isfinite(heightDip) ? (std::max)(0.0F, heightDip) : 0.0F;
		const float reserved = (std::min)(width,
			state.narrow || state.desktopCompact ? CompactPaneWidth : OpenPaneWidth);
		ShellGeometry result;
		result.pane = { 0.0F, (std::min)(height, TitleBarHeightDip),
			(std::min)(width, state.Expanded() ? OpenPaneWidth : CompactPaneWidth), height };
		result.content = { reserved, (std::min)(height, TitleBarHeightDip), width, height };
		result.gutter = (std::min)(width >= 1100.0F ? 40.0F : 24.0F,
			result.content.Width() * 0.1F);
		result.pageWidth = (std::min)(PageMaximumWidth,
			(std::max)(0.0F, result.content.Width() - result.gutter * 2.0F));
		result.pageOffset = (result.content.Width() - result.pageWidth) * 0.5F;
		return result;
	}

	struct RowMeasure
	{
		float width = 0.0F;
		float padding = 16.0F;
		float textLeft = 0.0F;
		float textWidth = 0.0F;
		float actionWidth = 0.0F;
		float actionHeight = 0.0F;
		bool hasIcon = true;
		bool stacked = false;
	};

	[[nodiscard]] inline RowMeasure MeasureRow(float width, float actionWidth,
		float actionHeight, bool hasIcon = true) noexcept
	{
		RowMeasure result;
		result.width = std::isfinite(width) ? (std::max)(0.0F, width) : 0.0F;
		result.padding = (std::min)(16.0F, result.width * 0.1F);
		result.hasIcon = hasIcon && result.width >= 112.0F;
		result.textLeft = result.padding + (result.hasIcon ? 40.0F : 0.0F);
		const float available = (std::max)(0.0F, result.width - result.padding - result.textLeft);
		result.actionWidth = std::isfinite(actionWidth)
			? std::clamp(actionWidth, 0.0F, available) : 0.0F;
		result.actionHeight = std::isfinite(actionHeight) ? (std::max)(0.0F, actionHeight) : 0.0F;
		result.stacked = result.actionWidth > 0.0F && available - result.actionWidth - 16.0F < 160.0F;
		result.textWidth = result.stacked || result.actionWidth == 0.0F
			? available : (std::max)(0.0F, available - result.actionWidth - 16.0F);
		return result;
	}

	struct RowGeometry
	{
		LayoutRect text;
		LayoutRect icon;
		LayoutRect action;
		float height = 72.0F;
		bool stacked = false;
	};

	// 先测文案，再排列；绘制控件不会参与测量，也不会重复触发业务。
	[[nodiscard]] inline RowGeometry ResolveRowLayout(const RowMeasure& measure,
		float textHeight) noexcept
	{
		textHeight = std::isfinite(textHeight) ? (std::max)(0.0F, textHeight) : 0.0F;
		RowGeometry result;
		result.stacked = measure.stacked;
		const float actionGap = measure.actionWidth > 0.0F ? 12.0F : 0.0F;
		const float bodyHeight = measure.stacked
			? textHeight + actionGap + measure.actionHeight
			: (std::max)(textHeight, measure.actionHeight);
		result.height = (std::max)(72.0F, bodyHeight + measure.padding * 2.0F);
		const float textY = measure.stacked ? measure.padding : (result.height - textHeight) * 0.5F;
		result.text = { measure.textLeft, textY, measure.textLeft + measure.textWidth, textY + textHeight };
		if (measure.hasIcon)
		{
			const float iconY = measure.stacked ? textY + (textHeight - 24.0F) * 0.5F
				: (result.height - 24.0F) * 0.5F;
			result.icon = { measure.padding, iconY, measure.padding + 24.0F, iconY + 24.0F };
		}
		const float actionY = measure.stacked ? result.text.bottom + actionGap
			: (result.height - measure.actionHeight) * 0.5F;
		const float actionRight = measure.width - measure.padding;
		result.action = { actionRight - measure.actionWidth, actionY,
			actionRight, actionY + measure.actionHeight };
		return result;
	}
}
