module;

#include "../../../IdtMain.h"

export module Inkeys.UI.Bar:State;

class BarStateClass
{
public:
	IdtAtomic<bool> fold = true;
	IdtAtomic<bool> drawAttribute = false;

	struct
	{
		IdtAtomic<bool> brush1Press = false;
		IdtAtomic<bool> highlight1Press = false;
		IdtAtomic<bool> thicknessFinePress = false;
		IdtAtomic<bool> thicknessMediumPress = false;
		IdtAtomic<bool> thicknessCoarsePress = false;
		IdtAtomic<bool> thicknessAdjustPress = false;
		IdtAtomic<bool> thicknessSliderHover = false;
		IdtAtomic<bool> thicknessSliderPinned = false;
		IdtAtomic<bool> thicknessSliderDragging = false;
		IdtAtomic<bool> thicknessSliderPressed = false;
		IdtAtomic<bool> thicknessSliderCapture = false;
		IdtAtomic<bool> thicknessAnnotationHover = false;
		IdtAtomic<bool> thicknessAnnotationPinned = false;
		IdtAtomic<bool> thicknessAnnotationClosePress = false;
		IdtAtomic<bool> thicknessOverflowHover = false;
		IdtAtomic<bool> thicknessOverflowPinned = false;
		IdtAtomic<bool> thicknessOverflowClosePress = false;
		IdtAtomic<bool> thicknessPreviewOverflow = false;
	}drawAttributeBar;

	struct
	{
		IdtAtomic<bool> mainBar; // false 左侧， true 右侧
		// 特别的，左侧和右侧上时，主栏上的控件块会反向

		IdtAtomic<bool> primaryBar; // false 上方， true 下方
		// 特别的，左侧和右侧上时，绘制属性上的控件块会上下反向
	}widgetPosition;

	void PositionUpdate(double tarZoom);
	void ThicknessDisplayUpdate();
};
class BarStyleClass
{
public:
	IdtAtomic<bool> darkStyle = true;
	IdtAtomic<double> dpiZoom = 1.0;
	IdtAtomic<double> configZoom = 1.0;
	IdtAtomic<double> zoom = 1.0;
	IdtAtomic<bool> initialZoomFitPending = false;
};

// ---

enum class BarWidgetState : int
{
	None,
	Disable, // 禁用
	Selected // 选中
};
enum class BarWidgetEmphasize : int
{
	None,
	Hover, // 悬停（废弃）
	Pressed, // 按下
};
