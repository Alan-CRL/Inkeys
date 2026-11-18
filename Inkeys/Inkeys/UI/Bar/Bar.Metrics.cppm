module;

export module Inkeys.UI.Bar.Metrics;

export import Inkeys.UI.UI3.Metrics;

using namespace Inkeys::UI::UI3;

export
{
	// 旧 compact 视觉保留，当前统一使用标准 2×2；几何和绘制必须消费同一开关。
	inline constexpr bool BarPptCompactPaginationEnabled = false;
	inline constexpr double BarPaginationDragHandleSlotDip = ButtonGapDip * 2.0;
	inline constexpr double BarPptCompactLongSideDip =
		BarPaginationDragHandleSlotDip + ButtonOneSideDip * 2.0
		+ ButtonTwoSideDip + ButtonGapDip * 4.0;
	inline constexpr double BarPptCompactShortSideDip =
		ButtonOneSideDip + ButtonGapDip * 2.0;
	inline constexpr double BarPptPaginationLongSideDip =
		BarPptCompactPaginationEnabled ? BarPptCompactLongSideDip
		: BarPaginationDragHandleSlotDip + ButtonTwoSideDip * 3.0 + ButtonGapDip * 4.0;
	inline constexpr double BarPptPaginationShortSideDip =
		BarPptCompactPaginationEnabled ? BarPptCompactShortSideDip
		: ButtonTwoSideDip + ButtonGapDip * 2.0;
	inline constexpr double BarWhiteboardPaginationWidthDip =
		ButtonTwoSideDip * 3.0 + ButtonGapDip * 4.0;
	inline constexpr double BarWhiteboardPaginationLeftWidthDip =
		BarWhiteboardPaginationWidthDip + ButtonTwoSideDip + ButtonGapDip;
	inline constexpr double BarWhiteboardPaginationHeightDip =
		ButtonTwoSideDip + ButtonGapDip * 2.0;
	inline constexpr double BarMainBarWidthDip = 80.0;
	inline constexpr double BarMainBarHeightDip = 80.0;
	inline constexpr double BarMainBarCornerRadiusDip = 8.0;
	inline constexpr double BarMainBarFillOpacity = 0.8;
	inline constexpr double BarMainBarFrameOpacity = 0.18;

	// Startup Preview 只依赖这些无副作用的视觉常量，不提前初始化完整 Bar。
	inline constexpr double BarMainButtonWidthDip = 80.0;
	inline constexpr double BarMainButtonHeightDip = 80.0;
	inline constexpr double BarMainButtonCollapsedFillOpacity = 0.6;
	inline constexpr double BarMainButtonToMainBarGapDip = 10.0;
	inline constexpr double BarDefaultButtonColumnStepDip =
		ButtonTwoSideDip + ButtonGapDip;
	// 默认展开布局：A1 的 Select/Draw/Clean 三列，More 一列，A2 的
	// Whiteboard/Freeze 上下共用一列；Divider 只收束列，不增加宽度。
	inline constexpr unsigned BarDefaultA1ColumnCount = 3;
	inline constexpr unsigned BarDefaultMoreColumnCount = 1;
	inline constexpr unsigned BarDefaultA2ColumnCount = 1;
	inline constexpr unsigned BarDefaultMainBarColumnCount =
		BarDefaultA1ColumnCount + BarDefaultMoreColumnCount
		+ BarDefaultA2ColumnCount;
	// CalculateButtonLayoutWidth 从左侧 5 DIP 起步：5 + 5 * 75 = 380。
	inline constexpr double BarDefaultMainBarLayoutWidthDip =
		ButtonGapDip
		+ static_cast<double>(BarDefaultMainBarColumnCount)
			* BarDefaultButtonColumnStepDip;
	inline constexpr double BarDefaultStartupPreviewTotalWidthDip =
		BarMainButtonWidthDip + BarMainButtonToMainBarGapDip
		+ BarDefaultMainBarLayoutWidthDip;
	inline constexpr double StartupPreviewCachedWidthDefaultDip =
		BarDefaultStartupPreviewTotalWidthDip;
	inline constexpr bool StartupPreviewEnabledDefault = true;
	inline constexpr double StartupPreviewCachedWidthMinimumDip =
		BarMainButtonWidthDip;
	inline constexpr double StartupPreviewCachedWidthMaximumDip = 4096.0;

	static_assert(BarDefaultButtonColumnStepDip == 75.0);
	static_assert(BarDefaultMainBarColumnCount == 5);
	static_assert(BarDefaultMainBarLayoutWidthDip == 380.0);
	static_assert(BarDefaultStartupPreviewTotalWidthDip == 470.0);
}

export namespace Inkeys::UI::Bar
{
	[[nodiscard]] constexpr double CalculateExpandedTotalWidthDip(
		double mainButtonTargetWidthDip, double layoutTotalWidthDip) noexcept
	{
		return mainButtonTargetWidthDip
			+ BarMainButtonToMainBarGapDip + layoutTotalWidthDip;
	}

	struct BarPaginationDipRect
	{
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;
	};

	struct BarPptStandardPaginationGeometry
	{
		BarPaginationDipRect drag;
		BarPaginationDipRect previous;
		BarPaginationDipRect page;
		BarPaginationDipRect next;
	};

	[[nodiscard]] constexpr BarPptStandardPaginationGeometry
		ResolveBarPptStandardPaginationGeometry(bool vertical, bool right) noexcept
	{
		constexpr double gap = ButtonGapDip;
		constexpr double side = ButtonTwoSideDip;
		constexpr double slot = BarPaginationDragHandleSlotDip;
		if (vertical)
		{
			return {
				{ gap, gap, gap + side, gap + slot },
				{ gap, gap + slot, gap + side, gap + slot + side },
				{ gap, 2 * gap + slot + side, gap + side, 2 * gap + slot + 2 * side },
				{ gap, 3 * gap + slot + 2 * side, gap + side, 3 * gap + slot + 3 * side },
			};
		}
		if (right)
		{
			return {
				{ 3 * gap + 3 * side, gap, 3 * gap + 3 * side + slot, gap + side },
				{ gap, gap, gap + side, gap + side },
				{ 2 * gap + side, gap, 2 * gap + 2 * side, gap + side },
				{ 3 * gap + 2 * side, gap, 3 * gap + 3 * side, gap + side },
			};
		}
		return {
			{ gap, gap, gap + slot, gap + side },
			{ gap + slot, gap, gap + slot + side, gap + side },
			{ 2 * gap + slot + side, gap, 2 * gap + slot + 2 * side, gap + side },
			{ 3 * gap + slot + 2 * side, gap, 3 * gap + slot + 3 * side, gap + side },
		};
	}

}
