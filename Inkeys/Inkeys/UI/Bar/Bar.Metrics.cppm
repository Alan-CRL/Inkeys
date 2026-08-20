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
	inline constexpr double BarMainBarWidthDip = 80.0;
	inline constexpr double BarMainBarHeightDip = 80.0;
	inline constexpr double BarMainBarCornerRadiusDip = 8.0;
	inline constexpr double BarMainBarFillOpacity = 0.8;
	inline constexpr double BarMainBarFrameOpacity = 0.18;
}
