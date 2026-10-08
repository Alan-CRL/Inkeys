#pragma once

#include "Setting.Layout.h"
#include <array>
#include <vector>
#include <utility>

namespace Inkeys::UI::Setting::Design
{
	inline constexpr float OpenPaneWidth = 248.0F;
	inline constexpr float CompactPaneWidth = 48.0F;
	inline constexpr float NavigationBreakpoint = 900.0F;
	inline constexpr float PageMaximumWidth = 1040.0F;
	inline constexpr float RowGap = 4.0F;
	inline constexpr float SectionGap = 28.0F;
	inline constexpr float NavigationGlyphSize = 16.0F;
	inline constexpr float RowGlyphSize = 20.0F;
	inline constexpr float ControlHeight = 32.0F;
	inline constexpr float ControlSpacing = 8.0F;
	inline constexpr float ButtonGroupMaximumWidth = 320.0F;
	inline constexpr float ScrollbarTrackSize = 8.0F;
	inline constexpr float ScrollbarPadding = 2.0F;

	struct ButtonGroupGeometry
	{
		float width = 0.0F;
		float height = 0.0F;
		std::vector<LayoutRect> items;
	};

	// 先按自然宽度换行，再把每行尾部对齐；测量不提交 ImGui item。
	[[nodiscard]] inline ButtonGroupGeometry ResolveButtonGroup(
		const std::vector<float>& widths, float maximumWidth)
	{
		ButtonGroupGeometry result;
		maximumWidth = std::isfinite(maximumWidth) ? (std::max)(1.0F, maximumWidth) : 1.0F;
		std::vector<std::pair<size_t, float>> lines;
		size_t first = 0;
		float x = 0.0F;
		float y = 0.0F;
		for (const float requestedWidth : widths)
		{
			const float width = std::isfinite(requestedWidth)
				? std::clamp(requestedWidth, 1.0F, maximumWidth) : 1.0F;
			if (x > 0.0F && x + ControlSpacing + width > maximumWidth)
			{
				lines.emplace_back(first, x);
				result.width = (std::max)(result.width, x);
				first = result.items.size();
				x = 0.0F;
				y += ControlHeight + ControlSpacing;
			}
			if (x > 0.0F) x += ControlSpacing;
			result.items.push_back({ x, y, x + width, y + ControlHeight });
			x += width;
		}
		if (result.items.empty()) return result;
		lines.emplace_back(first, x);
		result.width = (std::max)(result.width, x);
		result.height = y + ControlHeight;
		for (size_t line = 0; line < lines.size(); ++line)
		{
			const size_t end = line + 1 < lines.size() ? lines[line + 1].first : result.items.size();
			const float offset = result.width - lines[line].second;
			for (size_t index = lines[line].first; index < end; ++index)
			{
				result.items[index].left += offset;
				result.items[index].right += offset;
			}
		}
		return result;
	}

	enum class GeneralSection : int { Basic, Appearance, Interaction, Storage };

	struct NavigationTween
	{
		float value = 0.0F;
		float target = 0.0F;
		float start = 0.0F;
		float elapsed = 0.0F;
		float progress = 0.0F;

		explicit NavigationTween(float initial = 0.0F) noexcept
			: value(initial), target(initial), start(initial) {}
		void Reset(float next) noexcept { value = target = start = next; elapsed = progress = 0.0F; }
		void Target(float next) noexcept
		{
			if (target == next) return;
			// 快速反向从当前可见值起步，不重播已经走过的区间。
			start = value; target = next; elapsed = progress = 0.0F;
		}
		void Advance(float delta, float duration, bool enabled) noexcept
		{
			if (!enabled || duration <= 0.0F) { Reset(target); return; }
			// 使用归一化进度，中途调整速率只改变后续速度，不会倒退已显示的画面。
			progress = (std::min)(1.0F, progress + delta / duration);
			elapsed = progress * duration;
			const float remaining = 1.0F - progress;
			value = start + (target - start) * (1.0F - remaining * remaining * remaining);
		}
		[[nodiscard]] bool Active() const noexcept { return std::abs(value - target) > 0.0001F; }
	};

	struct NavigationState
	{
		bool desktopCompact = false;
		bool narrow = false;
		bool overlayOpen = false;
		bool generalExpanded = true;
		GeneralSection lastGeneralSection = GeneralSection::Basic;
		bool flyoutOpen = false;
		bool restoreParentFocus = false;
		bool mouseReleaseBarrier = false;
		bool scrollSelectedGeneral = false;
		bool animationsInitialized = false;
		bool animationsEnabled = true;
		float animationRate = 1.0F;
		NavigationTween paneWidth{ OpenPaneWidth };
		NavigationTween groupReveal{ 1.0F };
		NavigationTween flyoutReveal;
		NavigationTween contentReveal{ 1.0F };
		NavigationTween indicatorX;
		NavigationTween indicatorY;
		bool indicatorInitialized = false;
		int contentPage = 0;
		GeneralSection contentSection = GeneralSection::Basic;
		// 真实控件提交后的屏幕像素框，同时供连续帧测试观测，不另写一套命中公式。
		LayoutRect parentBounds, chevronBounds, flyoutBounds;
		std::array<LayoutRect, 4> childBounds{}, flyoutChildBounds{}, footerBounds{};
		std::array<LayoutRect, 6> middleBounds{};
		std::uint32_t parentItemId = 0;
		std::uint32_t childFocusId = 0;
		std::uint32_t childWindowItemId = 0;
		bool inlineChildFocused = false;

		void CloseFlyout() noexcept
		{
			if (IsFlyoutOpen()) restoreParentFocus = true;
			flyoutOpen = false;
		}
		void Resize(float widthDip) noexcept
		{
			const bool nextNarrow = widthDip < NavigationBreakpoint;
			if (nextNarrow != narrow) { overlayOpen = false; CloseFlyout(); scrollSelectedGeneral = true; }
			narrow = nextNarrow;
		}
		void Toggle() noexcept
		{
			CloseFlyout();
			scrollSelectedGeneral = true;
			if (narrow) overlayOpen = !overlayOpen;
			else desktopCompact = !desktopCompact;
		}
		void DismissOverlay() noexcept { overlayOpen = false; }
		[[nodiscard]] bool Expanded() const noexcept
		{
			return narrow ? overlayOpen : !desktopCompact;
		}
		[[nodiscard]] bool IsFlyoutOpen() const noexcept
		{
			return flyoutOpen || flyoutReveal.value > 0.001F;
		}
		[[nodiscard]] bool BlockContentInput() const noexcept
		{
			return overlayOpen || (narrow && animationsInitialized && paneWidth.value > CompactPaneWidth + 0.01F)
				|| IsFlyoutOpen() || mouseReleaseBarrier;
		}
	};

	// 产品与无窗口测试共用推进入口；渲染线程每帧调用一次，不跨线程保存 UI 指针。
	inline void AdvanceNavigationAnimations(NavigationState& state, float delta,
		bool enabled, float rate) noexcept
	{
		// 慢帧仍推进有限时长；隐藏恢复是否跳过首帧由真实会话显隐信号决定。
		delta = std::isfinite(delta) ? std::clamp(delta, 0.0F, 0.25F) : 0.0F;
		state.animationsEnabled = enabled;
		state.animationRate = std::isfinite(rate) ? std::clamp(rate, 0.1F, 5.0F) : 1.0F;
		const float paneTarget = state.Expanded() ? OpenPaneWidth : CompactPaneWidth;
		const float groupTarget = state.Expanded() && state.generalExpanded ? 1.0F : 0.0F;
		if (!state.animationsInitialized)
		{
			state.paneWidth.Reset(paneTarget);
			state.groupReveal.Reset(groupTarget);
			state.animationsInitialized = true;
		}
		state.paneWidth.Target(paneTarget);
		state.groupReveal.Target(groupTarget);
		state.flyoutReveal.Target(state.flyoutOpen ? 1.0F : 0.0F);
		state.paneWidth.Advance(delta, 0.200F / state.animationRate, enabled);
		state.groupReveal.Advance(delta, 0.160F / state.animationRate, enabled);
		state.flyoutReveal.Advance(delta, 0.140F / state.animationRate, enabled);
		state.contentReveal.Advance(delta, 0.120F / state.animationRate, enabled);
		state.indicatorX.Advance(delta, 0.160F / state.animationRate, enabled);
		state.indicatorY.Advance(delta, 0.160F / state.animationRate, enabled);
	}

	inline void SnapNavigationAnimations(NavigationState& state) noexcept
	{
		state.animationsEnabled = false;
		state.paneWidth.Reset(state.Expanded() ? OpenPaneWidth : CompactPaneWidth);
		state.groupReveal.Reset(state.Expanded() && state.generalExpanded ? 1.0F : 0.0F);
		state.flyoutReveal.Reset(state.flyoutOpen ? 1.0F : 0.0F);
		state.contentReveal.Reset(1.0F);
		state.indicatorX.Reset(state.indicatorX.target);
		state.indicatorY.Reset(state.indicatorY.target);
	}

	[[nodiscard]] inline float ContentAlpha(const NavigationState& state) noexcept
	{
		return state.animationsEnabled ? state.contentReveal.value : 1.0F;
	}

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
		const float visualWidth = state.animationsInitialized ? state.paneWidth.value
			: (state.Expanded() ? OpenPaneWidth : CompactPaneWidth);
		const float reserved = (std::min)(width, state.narrow ? CompactPaneWidth : visualWidth);
		ShellGeometry result;
		result.pane = { 0.0F, (std::min)(height, TitleBarHeightDip),
			(std::min)(width, visualWidth), height };
		result.content = { reserved, (std::min)(height, TitleBarHeightDip), width, height };
		result.gutter = (std::min)(width >= 1100.0F ? 40.0F : 24.0F,
			result.content.Width() * 0.1F);
		result.pageWidth = (std::min)(PageMaximumWidth,
			(std::max)(0.0F, result.content.Width() - result.gutter * 2.0F));
		result.pageOffset = (result.content.Width() - result.pageWidth) * 0.5F;
		return result;
	}

	inline constexpr float NavigationRowHeight = 36.0F;
	inline constexpr float NavigationDebugHeight = 28.0F;
	inline constexpr float NavigationSeparatorBand = 12.0F;
	inline constexpr size_t NavigationMiddleItemCount = 6;

	struct NavigationPaneGeometry
	{
		LayoutRect menu;
		LayoutRect top;
		LayoutRect middle;
		LayoutRect footer;
		std::array<LayoutRect, 2> topItems;
		std::array<LayoutRect, 4> footerItems; // 社区、赞助、重启、关闭。
		std::array<LayoutRect, 3> separators;
		LayoutRect debug;
		float middleContentHeight = 0.0F;
		float minimumHeight = 0.0F;
		bool debugVisible = false;
		bool fits = false;
	};

	// 先测固定行、分割线和调试按钮，再给中段剩余高度；所有坐标相对 pane，单位为 DIP。
	[[nodiscard]] inline NavigationPaneGeometry ResolveNavigationPaneGeometry(
		float widthDip, float heightDip, bool expanded,
		float rowHeightDip = NavigationRowHeight, float debugWidthDip = 72.0F,
		float expandedFraction = -1.0F) noexcept
	{
		const float width = std::isfinite(widthDip) ? (std::max)(0.0F, widthDip) : 0.0F;
		const float height = std::isfinite(heightDip) ? (std::max)(0.0F, heightDip) : 0.0F;
		const float rowHeight = std::isfinite(rowHeightDip) && rowHeightDip > 0.0F
			? rowHeightDip : NavigationRowHeight;
		const float inset = (std::min)(4.0F, width * 0.1F);
		const float right = width - inset;
		constexpr float headerHeight = 8.0F + NavigationRowHeight + 12.0F;
		const float pairHeight = rowHeight * 2.0F + RowGap;
		const float topHeight = headerHeight + pairHeight + NavigationSeparatorBand;
		const float debugFraction = expandedFraction >= 0.0F
			? std::clamp(expandedFraction, 0.0F, 1.0F) : (expanded ? 1.0F : 0.0F);
		const float footerHeight = pairHeight * 2.0F + NavigationSeparatorBand * 2.0F
			+ 8.0F + (8.0F + NavigationDebugHeight) * debugFraction;

		NavigationPaneGeometry result;
		result.minimumHeight = topHeight + footerHeight;
		result.fits = height >= result.minimumHeight;
		result.debugVisible = expanded && debugFraction > 0.001F;
		// 不够高的既有窗口由 HWND 最小尺寸合同扩展；此处仍保持固定区互不覆盖。
		const float footerTop = (std::max)(topHeight, height - footerHeight);
		result.menu = { inset, 8.0F, (std::min)(right, inset + 40.0F),
			8.0F + NavigationRowHeight };
		result.top = { 0.0F, 0.0F, width, topHeight };
		result.middle = { 0.0F, topHeight, width, footerTop };
		result.footer = { 0.0F, footerTop, width, footerTop + footerHeight };
		result.topItems[0] = { inset, headerHeight, right, headerHeight + rowHeight };
		result.topItems[1] = { inset, headerHeight + rowHeight + RowGap, right,
			headerHeight + pairHeight };
		const float dividerInset = (std::min)(16.0F, width * 0.4F);
		auto separator = [&](float bandTop) {
			const float y = bandTop + (NavigationSeparatorBand - 1.0F) * 0.5F;
			return LayoutRect{ dividerInset, y, width - dividerInset, y + 1.0F };
		};
		result.separators[0] = separator(headerHeight + pairHeight);
		result.separators[1] = separator(footerTop);
		const float supportTop = footerTop + NavigationSeparatorBand;
		result.footerItems[0] = { inset, supportTop, right, supportTop + rowHeight };
		result.footerItems[1] = { inset, supportTop + rowHeight + RowGap, right,
			supportTop + pairHeight };
		result.separators[2] = separator(supportTop + pairHeight);
		const float actionsTop = supportTop + pairHeight + NavigationSeparatorBand;
		result.footerItems[2] = { inset, actionsTop, right, actionsTop + rowHeight };
		result.footerItems[3] = { inset, actionsTop + rowHeight + RowGap, right,
			actionsTop + pairHeight };
		if (result.debugVisible)
		{
			const float debugWidth = std::isfinite(debugWidthDip)
				? std::clamp(debugWidthDip, 0.0F, right - inset) : 0.0F;
			const float debugLeft = (width - debugWidth) * 0.5F;
			const float debugTop = actionsTop + pairHeight + 8.0F;
			result.debug = { debugLeft, debugTop, debugLeft + debugWidth,
				debugTop + NavigationDebugHeight };
		}
		result.middleContentHeight = rowHeight * static_cast<float>(NavigationMiddleItemCount)
			+ RowGap * static_cast<float>(NavigationMiddleItemCount - 1);
		return result;
	}

	// 浮层优先下方，空间不足时翻到上方；极短客户区限制高度并交给真实窗口滚动。
	[[nodiscard]] inline LayoutRect ResolveNavigationFlyoutGeometry(const LayoutRect& anchor,
		const LayoutRect& client, float scale = 1.0F) noexcept
	{
		scale = std::isfinite(scale) && scale > 0.0F ? scale : 1.0F;
		const float margin = 8.0F * scale;
		const float width = (std::min)(OpenPaneWidth * scale, (std::max)(1.0F, client.Width() - margin * 2.0F));
		const float height = (std::min)((NavigationRowHeight * 4.0F + RowGap * 3.0F + 16.0F) * scale,
			(std::max)(1.0F, client.Height() - margin * 2.0F));
		const float left = std::clamp(anchor.left, client.left + margin,
			(std::max)(client.left + margin, client.right - margin - width));
		float top = anchor.bottom + margin;
		if (top + height > client.bottom - margin) top = anchor.top - margin - height;
		top = std::clamp(top, client.top + margin,
			(std::max)(client.top + margin, client.bottom - margin - height));
		return { left, top, left + width, top + height };
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
