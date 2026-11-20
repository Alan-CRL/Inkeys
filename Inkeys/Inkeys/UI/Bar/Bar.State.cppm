module;

#include "../../../IdtMain.h"

export module Inkeys.UI.Bar:State;

export import Inkeys.UI.UI3;

enum class ThicknessViewMode : int
{
	Preview,
	Slider,
	FineDial,
};

class BarStateClass
{
public:
	IdtAtomic<bool> fold = false;
	IdtAtomic<bool> drawAttribute = false;
	IdtAtomic<bool> geometryAttribute = false;
	IdtAtomic<bool> eraserAttribute = false;
	IdtAtomic<bool> eraserSensitivityOpen = false;
	// 撤回属性：点击主栏撤回后展开，内置单 70x70 Redo；继续点 Undo/Redo 保持展开。
	IdtAtomic<bool> recallAttribute = false;
	// 更多浮层状态同步到主栏入口的 Selected 视觉；折叠主栏时强制关闭。
	IdtAtomic<bool> moreExpanded = false;
	IdtAtomic<bool> moreClosePress = false;

	struct
	{
		IdtAtomic<bool> brush1Press = false;
		IdtAtomic<bool> softPenPress = false;
		IdtAtomic<bool> laserPress = false;
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
		IdtAtomic<ThicknessViewMode> thicknessViewMode =
			ThicknessViewMode::Preview;
		IdtAtomic<bool> thicknessSliderHover = false;
		IdtAtomic<bool> thicknessSliderPinned = false;
		IdtAtomic<bool> thicknessSliderDragging = false;
		// 直接触摸 Preview 时只更新候选预览，不激活 Slider 视觉。
		IdtAtomic<bool> thicknessPreviewDragging = false;
		IdtAtomic<bool> thicknessSliderPressed = false;
		IdtAtomic<bool> thicknessSliderCapture = false;
		IdtAtomic<double> thicknessSliderCandidateWidth = 0.0;
		// FineDial 只共享渲染所需快照；运动阶段、速度和采样环留在交互线程。
		IdtAtomic<double> thicknessFineDialVisualWidth = 0.0;
		IdtAtomic<bool> thicknessFineDialCandidateActive = false;
		IdtAtomic<bool> thicknessFineDialDragging = false;
		IdtAtomic<bool> thicknessFineDialPhysicsActive = false;
		// 量程视觉过渡期间旧 tick 只用于退场，交互线程仅消费按下。
		IdtAtomic<bool> thicknessFineDialRangeTransitionActive = false;
		// Slider 外移识别只发布独立预览，不提前改变 FineDial ViewMode。
		IdtAtomic<bool> thicknessFineDialActivationPreviewActive = false;
		IdtAtomic<bool> thicknessFineDialActivationDwellActive = false;
		IdtAtomic<float> thicknessFineDialActivationPreviewProgress = 0.0f;
		IdtAtomic<double> thicknessFineDialActivationPreviewVisualWidth = 0.0;
		// 仅正常 FineDial -> Preview 退出请求渲染侧锁存 Popup 当前位置。
		IdtAtomic<bool> thicknessFineDialPopupExitLatchRequested = false;
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
		// 记录 Hint 是否已经进入视觉状态，Slider 会据此锁存本次会话。
		IdtAtomic<bool> thicknessOverflowHintPresent = false;
		// 简易颜色选择器：输入线程串行改色，渲染线程只读取这些轻量状态。
		IdtAtomic<bool> colorPickerOpen = false;
		IdtAtomic<bool> colorPickerPointerCapture = false;
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
	void ThicknessDisplayUpdate(double penWidth);
};
class BarStyleClass : public Inkeys::UI::UI3::ThemeStyleState
{
public:
	// 窗口线程只发布目标，实际主题由串行渲染线程在帧首应用。
	std::atomic<bool> requestedDarkStyle = true;
	// 常驻表面透明度百分比：背景 0 表示跟随主题默认；同样由渲染线程在帧首应用。
	std::atomic<int> requestedBackgroundOpacity = 0;
	std::atomic<int> requestedContentOpacity = 100;
	// 界面尺寸：设置线程只写 requested；渲染线程在帧首（无主栏拖动时）落地为 applied。
	// 按钮投影、输入线程与翻页控件只读 applied，保证同一帧不混用两套尺寸。
	std::atomic<int> requestedSizeMode = 0;
	std::atomic<int> appliedSizeMode = 0;
	IdtAtomic<double> dpiZoom = 1.0;
	// UI 基准独立于真实 DPI，画笔尺寸仍只使用 dpiZoom。
	IdtAtomic<double> uiBaseZoom = 1.0;
	IdtAtomic<double> configZoom = 1.0;
	IdtAtomic<double> zoom = 1.0;
	IdtAtomic<bool> initialZoomFitPending = false;
};

// ---

