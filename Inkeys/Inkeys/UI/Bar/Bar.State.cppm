module;

#include "../../../IdtMain.h"

export module Inkeys.UI.Bar:State;

class BarStateClass
{
public:
	IdtAtomic<bool> fold = true;
	IdtAtomic<bool> drawAttribute = false;
	IdtAtomic<bool> geometryAttribute = false;
	// 更多浮层状态同步到主栏入口的 Selected 视觉；折叠主栏时强制关闭。
	IdtAtomic<bool> moreExpanded = false;
	IdtAtomic<bool> moreClosePress = false;

	struct
	{
		IdtAtomic<bool> brush1Press = false;
		IdtAtomic<bool> highlight1Press = false;
		IdtAtomic<bool> penTypeMenuOpen = false;
		IdtAtomic<bool> penTypeMenuDirectionLocked = false;
		IdtAtomic<bool> penTypeMenuOpenBelow = true;
		IdtAtomic<int> penTypeMenuAnchorMode = 0;
		IdtAtomic<bool> penTypeExtensionPress = false;
		IdtAtomic<bool> penTypeFreeLinePress = false;
		IdtAtomic<bool> thicknessFinePress = false;
		IdtAtomic<bool> thicknessMediumPress = false;
		IdtAtomic<bool> thicknessCoarsePress = false;
		IdtAtomic<bool> thicknessAdjustPress = false;
IdtAtomic<bool> thicknessSliderHover = false;
			IdtAtomic<bool> thicknessSliderPinned = false;
			IdtAtomic<bool> thicknessSliderDragging = false;
			IdtAtomic<bool> thicknessSliderPressed = false;
			IdtAtomic<bool> thicknessSliderCapture = false;
			IdtAtomic<float> thicknessSliderCandidateWidth = 0.0f;
			// 拖动改值后静止保持：提示/进度/锁定，跨交互与渲染线程共享。
			IdtAtomic<bool> thicknessSliderHoldHintActive = false;
			IdtAtomic<bool> thicknessSliderHoldLocked = false;
			IdtAtomic<float> thicknessSliderHoldProgress = 0.0f;
		IdtAtomic<bool> thicknessAnnotationHover = false;
		IdtAtomic<bool> thicknessAnnotationHoverGrace = false;
		IdtAtomic<bool> thicknessAnnotationPinned = false;
		IdtAtomic<bool> thicknessAnnotationClosePress = false;
		IdtAtomic<bool> thicknessOverflowHover = false;
		IdtAtomic<bool> thicknessOverflowHoverGrace = false;
		IdtAtomic<bool> thicknessOverflowPinned = false;
		IdtAtomic<bool> thicknessOverflowClosePress = false;
		IdtAtomic<bool> thicknessPreviewOverflow = false;
		// 简易颜色选择器：输入线程串行改色，渲染线程只读取这些轻量状态。
		IdtAtomic<bool> colorPickerOpen = false;
		IdtAtomic<bool> colorPickerDarkTone = true; // 默认暗色系
		IdtAtomic<bool> colorPickerTonePress = false;
		IdtAtomic<bool> colorPickerClosePress = false;
		IdtAtomic<bool> colorPickerMarkerVisible = false;
		IdtAtomic<float> colorPickerMarkerX = 0.0f;
		IdtAtomic<float> colorPickerMarkerY = 0.0f;
		IdtAtomic<bool> colorPickerPointerPressed = false;
		IdtAtomic<bool> colorPickerPointerCapture = false;
		IdtAtomic<bool> colorPickerHoldHintActive = false;
		IdtAtomic<bool> colorPickerHoldLocked = false;
		IdtAtomic<float> colorPickerHoldProgress = 0.0f;
		IdtAtomic<float> colorPickerPointerY = 0.0f;
		IdtAtomic<unsigned int> colorPickerKeyboardDownMask = 0;
	}drawAttributeBar;

	struct
	{
		IdtAtomic<bool> straightLinePress = false;
		IdtAtomic<bool> rectanglePress = false;
		IdtAtomic<bool> thicknessFinePress = false;
		IdtAtomic<bool> thicknessMediumPress = false;
		IdtAtomic<bool> thicknessCoarsePress = false;
		IdtAtomic<bool> closePress = false;
	}geometryAttributeBar;

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
