module;

export module Inkeys.UI.UI3.Metrics;

export namespace Inkeys::UI::UI3
{
	inline constexpr unsigned char DarkSurfaceColorChannel = 24;
	inline constexpr unsigned char DarkSurfaceFrameColorChannel = 255;

	// 标准按钮网格与 MainBar 外框共用的 UI3 单一尺寸来源。
	inline constexpr double ButtonGapDip = 5.0;
	inline constexpr double ButtonOneSideDip = 32.5;
	inline constexpr double ButtonTwoSideDip =
		ButtonOneSideDip * 2.0 + ButtonGapDip;
	inline constexpr double ButtonCornerRadiusDip = 4.0;
	inline constexpr double ButtonFrameThicknessDip = 1.0;
	inline constexpr double ButtonTwoTwoIconSizeDip = 28.0;
	inline constexpr double ButtonTwoTwoIconOffsetYDip = -10.0;
	inline constexpr double ButtonTwoTwoLabelOffsetYDip = 20.0;
	inline constexpr double ButtonTwoTwoLabelHeightDip = 25.0;
	inline constexpr double ButtonTwoTwoLabelFontSizeDip = 13.0;
	inline constexpr double ButtonPressScale = 0.95;
	inline constexpr double ButtonPressedOpacity = 0.10;
	inline constexpr double ButtonHoverOpacity = 0.18;
	inline constexpr double ButtonDisabledContentOpacity = 0.30;
	inline constexpr double ButtonPressedLightOpacity = 0.5;
	inline constexpr double ButtonCursorLightIntensity = 0.30;
	inline constexpr double ButtonLightCursorLightIntensity = 0.40;
	inline constexpr double ButtonHoverTransitionDuration = 0.24;
	inline constexpr double ButtonHoverFadeDurationSeconds = 5.0;
	inline constexpr double ButtonDefaultOperationDurationSeconds = 0.4;

	static_assert(ButtonTwoSideDip == 70.0);

	// 标准按钮内部几何只在这里解析，主栏与分页不得各写一份偏移。
	enum class ButtonVisualLayoutKind : unsigned char
	{
		Custom,
		StandardOneOne,
		StandardTwoOne,
		StandardFourOne,
		StandardTwoTwo,
		PageHorizontal,
		PageVertical,
		PageTwoTwo,
	};

	struct ButtonVisualMetrics
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

	struct ButtonVisualPoint
	{
		double x = 0.0;
		double y = 0.0;
	};

	// 子内容始终以调用方给出的按钮左上角为父级，不能读取历史 inh 缓存。
	[[nodiscard]] constexpr ButtonVisualPoint
		ResolveButtonChildTopLeft(double parentX, double parentY,
			double parentWidth, double parentHeight,
			double childOffsetX, double childOffsetY,
			double childWidth, double childHeight) noexcept
	{
		return {
			parentX + parentWidth / 2.0 + childOffsetX - childWidth / 2.0,
			parentY + parentHeight / 2.0 + childOffsetY - childHeight / 2.0,
		};
	}

	// 含翻页/白板操作行布局分支，属产品布局档位，S4 按钮模型拆分时再处理
	[[nodiscard]] inline ButtonVisualMetrics ResolveButtonVisualMetrics(
		ButtonVisualLayoutKind layoutKind) noexcept
	{
		ButtonVisualMetrics result;
		switch (layoutKind)
		{
		case ButtonVisualLayoutKind::StandardOneOne:
			result.buttonWidthDip = ButtonOneSideDip;
			result.buttonHeightDip = ButtonOneSideDip;
			result.iconSizeDip = 20.0;
			break;
		case ButtonVisualLayoutKind::StandardTwoOne:
			result.buttonWidthDip = ButtonTwoSideDip;
			result.buttonHeightDip = ButtonOneSideDip;
			result.iconSizeDip = 18.0;
			result.iconOffsetXDip = -21.0;
			result.primaryFontSizeDip = 12.0;
			result.primaryOffsetXDip = 11.5;
			result.primarySlotWidthDip = 37.0;
			result.primarySlotHeightDip = ButtonOneSideDip;
			break;
		case ButtonVisualLayoutKind::StandardFourOne:
			// 白板操作行按产品规范固定 140 DIP，沿用单行按钮的字号和图标。
			result.buttonWidthDip = 140.0;
			result.buttonHeightDip = ButtonOneSideDip;
			result.iconSizeDip = 18.0;
			result.iconOffsetXDip = -56.0;
			result.primaryFontSizeDip = 12.0;
			result.primaryOffsetXDip = 11.5;
			result.primarySlotWidthDip = 107.0;
			result.primarySlotHeightDip = ButtonOneSideDip;
			break;
		case ButtonVisualLayoutKind::StandardTwoTwo:
			result.buttonWidthDip = ButtonTwoSideDip;
			result.buttonHeightDip = ButtonTwoSideDip;
			result.iconSizeDip = ButtonTwoTwoIconSizeDip;
			result.iconOffsetYDip = ButtonTwoTwoIconOffsetYDip;
			result.primaryFontSizeDip = ButtonTwoTwoLabelFontSizeDip;
			result.primaryOffsetYDip = ButtonTwoTwoLabelOffsetYDip;
			result.primarySlotWidthDip = ButtonTwoSideDip;
			result.primarySlotHeightDip = ButtonTwoTwoLabelHeightDip;
			break;
		case ButtonVisualLayoutKind::PageHorizontal:
			result.buttonWidthDip = ButtonTwoSideDip;
			result.buttonHeightDip = ButtonOneSideDip;
			result.primaryFontSizeDip = 15.0;
			result.primarySlotHeightDip = 18.0;
			result.secondaryFontSizeDip = 13.0;
			result.secondarySlotHeightDip = 18.0;
			break;
		case ButtonVisualLayoutKind::PageVertical:
			result.buttonWidthDip = ButtonOneSideDip;
			result.buttonHeightDip = ButtonTwoSideDip;
			result.primaryFontSizeDip = 15.0;
			result.primaryOffsetYDip = -10.0;
			result.primarySlotWidthDip = ButtonOneSideDip;
			result.primarySlotHeightDip = 18.0;
			result.secondaryFontSizeDip = 13.0;
			result.secondaryOffsetYDip = 10.0;
			result.secondarySlotWidthDip = ButtonOneSideDip;
			result.secondarySlotHeightDip = 18.0;
			break;
		case ButtonVisualLayoutKind::PageTwoTwo:
			result.buttonWidthDip = ButtonTwoSideDip;
			result.buttonHeightDip = ButtonTwoSideDip;
			result.primaryFontSizeDip = ButtonTwoTwoIconSizeDip;
			result.primaryOffsetYDip = ButtonTwoTwoIconOffsetYDip;
			result.primarySlotWidthDip = ButtonTwoSideDip;
			result.primarySlotHeightDip = ButtonTwoTwoIconSizeDip;
			result.secondaryFontSizeDip = ButtonTwoTwoLabelFontSizeDip;
			result.secondaryOffsetYDip = ButtonTwoTwoLabelOffsetYDip;
			result.secondarySlotWidthDip = ButtonTwoSideDip;
			result.secondarySlotHeightDip = ButtonTwoTwoLabelHeightDip;
			break;
		case ButtonVisualLayoutKind::Custom:
		default:
			break;
		}
		return result;
	}
}
