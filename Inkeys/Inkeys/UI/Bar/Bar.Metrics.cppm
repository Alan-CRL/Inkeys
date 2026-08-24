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
}

export namespace Inkeys::UI::Bar
{
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
