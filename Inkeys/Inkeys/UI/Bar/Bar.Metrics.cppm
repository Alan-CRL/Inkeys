export module Inkeys.UI.Bar.Metrics;

export
{
	// 标准按钮网格与 MainBar 外框共用的 UI3 单一尺寸来源。
	inline constexpr double BarButtonGapDip = 5.0;
	inline constexpr double BarButtonOneSideDip = 32.5;
	inline constexpr double BarButtonTwoSideDip =
		BarButtonOneSideDip * 2.0 + BarButtonGapDip;
	inline constexpr double BarButtonCornerRadiusDip = 4.0;
	inline constexpr double BarButtonFrameThicknessDip = 1.0;
	inline constexpr double BarButtonTwoTwoIconSizeDip = 28.0;
	inline constexpr double BarButtonTwoTwoIconOffsetYDip = -10.0;
	inline constexpr double BarButtonTwoTwoLabelOffsetYDip = 20.0;
	inline constexpr double BarButtonTwoTwoLabelHeightDip = 25.0;
	inline constexpr double BarButtonTwoTwoLabelFontSizeDip = 13.0;
	inline constexpr double BarButtonPressScale = 0.95;
	inline constexpr double BarButtonPressedOpacity = 0.10;
	inline constexpr double BarButtonHoverOpacity = 0.18;
	inline constexpr double BarButtonDisabledContentOpacity = 0.30;
	inline constexpr double BarButtonPressedLightOpacity = 0.5;
	inline constexpr double BarButtonCursorLightIntensity = 0.30;
	inline constexpr double BarButtonHoverTransitionDuration = 0.24;
	inline constexpr double BarButtonHoverFadeDurationSeconds = 5.0;
	inline constexpr double BarButtonDefaultOperationDurationSeconds = 0.4;
	inline constexpr double BarMainBarWidthDip = 80.0;
	inline constexpr double BarMainBarHeightDip = 80.0;
	inline constexpr double BarMainBarCornerRadiusDip = 8.0;
	inline constexpr double BarMainBarFillOpacity = 0.8;
	inline constexpr double BarMainBarFrameOpacity = 0.18;

	// Startup Preview 只依赖这些无副作用的几何常量，不提前初始化完整 Bar。
	inline constexpr double BarMainButtonWidthDip = 80.0;
	inline constexpr double BarMainButtonHeightDip = 80.0;
	inline constexpr double BarMainButtonToMainBarGapDip = 10.0;
	inline constexpr double BarDefaultButtonColumnStepDip =
		BarButtonTwoSideDip + BarButtonGapDip;
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
		BarButtonGapDip
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

	static_assert(BarButtonTwoSideDip == 70.0);
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

	// 标准按钮内部几何只在这里解析，主栏与分页不得各写一份偏移。
	enum class BarButtonVisualLayoutKind : unsigned char
	{
		Custom,
		StandardOneOne,
		StandardTwoOne,
		StandardTwoTwo,
		PageHorizontal,
		PageVertical,
		PageTwoTwo,
	};

	struct BarButtonVisualMetrics
	{
		double buttonWidthDip = 0.0;
		double buttonHeightDip = 0.0;
		double iconSizeDip = 0.0;
		double iconOffsetXDip = 0.0;
		double iconOffsetYDip = 0.0;
		double primaryFontSizeDip = 0.0;
		double primaryOffsetXDip = 0.0;
		double primaryOffsetYDip = 0.0;
		double primarySlotWidthDip = 0.0;
		double primarySlotHeightDip = 0.0;
		double secondaryFontSizeDip = 0.0;
		double secondaryOffsetXDip = 0.0;
		double secondaryOffsetYDip = 0.0;
		double secondarySlotWidthDip = 0.0;
		double secondarySlotHeightDip = 0.0;
	};

	struct BarButtonVisualPoint
	{
		double x = 0.0;
		double y = 0.0;
	};

	// 子内容始终以调用方给出的按钮左上角为父级，不能读取历史 inh 缓存。
	[[nodiscard]] constexpr BarButtonVisualPoint
		ResolveBarButtonChildTopLeft(double parentX, double parentY,
			double parentWidth, double parentHeight,
			double childOffsetX, double childOffsetY,
			double childWidth, double childHeight) noexcept
	{
		return {
			parentX + parentWidth / 2.0 + childOffsetX - childWidth / 2.0,
			parentY + parentHeight / 2.0 + childOffsetY - childHeight / 2.0,
		};
	}

	[[nodiscard]] inline BarButtonVisualMetrics ResolveBarButtonVisualMetrics(
		BarButtonVisualLayoutKind layoutKind) noexcept
	{
		BarButtonVisualMetrics result;
		switch (layoutKind)
		{
		case BarButtonVisualLayoutKind::StandardOneOne:
			result.buttonWidthDip = BarButtonOneSideDip;
			result.buttonHeightDip = BarButtonOneSideDip;
			result.iconSizeDip = 20.0;
			break;
		case BarButtonVisualLayoutKind::StandardTwoOne:
			result.buttonWidthDip = BarButtonTwoSideDip;
			result.buttonHeightDip = BarButtonOneSideDip;
			result.iconSizeDip = 18.0;
			result.iconOffsetXDip = -21.0;
			result.primaryFontSizeDip = 12.0;
			result.primaryOffsetXDip = 11.5;
			result.primarySlotWidthDip = 37.0;
			result.primarySlotHeightDip = BarButtonOneSideDip;
			break;
		case BarButtonVisualLayoutKind::StandardTwoTwo:
			result.buttonWidthDip = BarButtonTwoSideDip;
			result.buttonHeightDip = BarButtonTwoSideDip;
			result.iconSizeDip = BarButtonTwoTwoIconSizeDip;
			result.iconOffsetYDip = BarButtonTwoTwoIconOffsetYDip;
			result.primaryFontSizeDip = BarButtonTwoTwoLabelFontSizeDip;
			result.primaryOffsetYDip = BarButtonTwoTwoLabelOffsetYDip;
			result.primarySlotWidthDip = BarButtonTwoSideDip;
			result.primarySlotHeightDip = BarButtonTwoTwoLabelHeightDip;
			break;
		case BarButtonVisualLayoutKind::PageHorizontal:
			result.buttonWidthDip = BarButtonTwoSideDip;
			result.buttonHeightDip = BarButtonOneSideDip;
			result.primaryFontSizeDip = 15.0;
			result.primarySlotHeightDip = 18.0;
			result.secondaryFontSizeDip = 13.0;
			result.secondarySlotHeightDip = 18.0;
			break;
		case BarButtonVisualLayoutKind::PageVertical:
			result.buttonWidthDip = BarButtonOneSideDip;
			result.buttonHeightDip = BarButtonTwoSideDip;
			result.primaryFontSizeDip = 15.0;
			result.primaryOffsetYDip = -10.0;
			result.primarySlotWidthDip = BarButtonOneSideDip;
			result.primarySlotHeightDip = 18.0;
			result.secondaryFontSizeDip = 13.0;
			result.secondaryOffsetYDip = 10.0;
			result.secondarySlotWidthDip = BarButtonOneSideDip;
			result.secondarySlotHeightDip = 18.0;
			break;
		case BarButtonVisualLayoutKind::PageTwoTwo:
			result.buttonWidthDip = BarButtonTwoSideDip;
			result.buttonHeightDip = BarButtonTwoSideDip;
			result.primaryFontSizeDip = BarButtonTwoTwoIconSizeDip;
			result.primaryOffsetYDip = BarButtonTwoTwoIconOffsetYDip;
			result.primarySlotWidthDip = BarButtonTwoSideDip;
			result.primarySlotHeightDip = BarButtonTwoTwoIconSizeDip;
			result.secondaryFontSizeDip = BarButtonTwoTwoLabelFontSizeDip;
			result.secondaryOffsetYDip = BarButtonTwoTwoLabelOffsetYDip;
			result.secondarySlotWidthDip = BarButtonTwoSideDip;
			result.secondarySlotHeightDip = BarButtonTwoTwoLabelHeightDip;
			break;
		case BarButtonVisualLayoutKind::Custom:
		default:
			break;
		}
		return result;
	}
}
