module;

#include "../../../IdtMain.h"

#include "../../../IdtD2DPreparation.h"
#include <array>
#include <cstdint>

export module Inkeys.UI.Bar:Main;

import :UI;
import :State;
import :Button;
import :Format;
import :Rendering;
import :RenderingAttribute;

import Inkeys.UI.Bar.Animation;
export import Inkeys.UI.Bar.ToggleClickCoalescer;

import Inkeys.Conv.Color;
import Inkeys.Helper.Thread;

// ====================
// 动画

IdtAtomic<bool> BarUiEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDynamicEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDebugModeEnabled = false;
IdtAtomic<bool> BarUiDebugFrameRateEnabled = true;

// ====================
// 窗口

LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 窗口模态信息
class BarWindowPosClass
{
public:
	IdtAtomic<int> x = 0, y = 0;
	IdtAtomic<unsigned int> w = 0, h = 0;
	IdtAtomic<unsigned int> pct = 255; // 透明度
};

// ====================
// 媒体

// 媒体操控类
class BarMediaClass
{
public:
	void LoadFormat();

public:
	unique_ptr<BarFormatCache> formatCache;

protected:
};

// ====================
// 界面

// 前向声明
class BarUISetClass;
class BarRenderLoopCoordinator;
// 控件枚举
enum class BarUISetShapeEnum : int
{
	MainBar,
	MorePanel,
	MorePanelDivider,
	MorePanelCloseHit,

	DrawAttributeBar,
	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,
	DrawAttributeBar_ColorSelect12,
	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_PenTypeExtensionHit,
	DrawAttributeBar_PenTypeExtensionDivider,
	DrawAttributeBar_ThicknessSelect,
	DrawAttributeBar_ThicknessDivider,
	DrawAttributeBar_ThicknessFine,
	DrawAttributeBar_ThicknessMedium,
	DrawAttributeBar_ThicknessCoarse,
	DrawAttributeBar_ThicknessAdjust,
	DrawAttributeBar_ThicknessSliderHit,
	DrawAttributeBar_ThicknessSliderThumb,
	DrawAttributeBar_ThicknessPreviewPopupSurface,
	DrawAttributeBar_ThicknessPreviewPopupCircle,
	DrawAttributeBar_ThicknessAnnotationInfoHit,
	DrawAttributeBar_ThicknessOverflowBadge,
	DrawAttributeBar_ThicknessOverflowInfoHit,
	DrawAttributeBar_ThicknessAnnotationPopup,
	DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
	DrawAttributeBar_ThicknessOverflowPopup,
	DrawAttributeBar_ThicknessOverflowPopupCloseHit,
	DrawAttributeBar_PenTypeMenu,
	DrawAttributeBar_PenTypeMenuFreeLine,
	DrawAttributeBar_PenTypeMenuAnnotationLine,
	DrawAttributeBar_ColorPickerPanel,
	DrawAttributeBar_ColorPickerPalette,
	DrawAttributeBar_ColorPickerToneToggle,
	DrawAttributeBar_ColorPickerCloseHit,
	DrawAttributeBar_ColorPickerPreviewBubble,
	DrawAttributeBar_ColorPickerHoldHint,
	DrawAttributeBar_ColorSelect12Inner,

	GeometryAttributeBar,
	GeometryAttributeBar_Divider,
	GeometryAttributeBar_StraightLine,
	GeometryAttributeBar_Rectangle,
	GeometryAttributeBar_ThicknessFine,
	GeometryAttributeBar_ThicknessMedium,
	GeometryAttributeBar_ThicknessCoarse,
	GeometryAttributeBar_Close,
};
enum class BarUISetSuperellipseEnum : int
{
	MainButton,
};
enum class BarUISetSvgEnum : int
{
	logo1,
	logoInk,
	MorePanelClose,

	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_PenTypeExtensionArrow,
	DrawAttributeBar_ThicknessAdjust,
	DrawAttributeBar_PenTypeMenuCheck,
	DrawAttributeBar_ThicknessAnnotationInfo,
	DrawAttributeBar_ThicknessOverflowInfo,
	DrawAttributeBar_ThicknessAnnotationPopupClose,
	DrawAttributeBar_ThicknessOverflowPopupClose,
	DrawAttributeBar_ColorSelect12Check,
	DrawAttributeBar_ColorPickerToneSun,
	DrawAttributeBar_ColorPickerToneMoon,
	GeometryAttributeBar_StraightLine,
	GeometryAttributeBar_Rectangle,
	GeometryAttributeBar_Close,
};
enum class BarUISetPngEnum : int
{
	DrawAttributeBar_ColorSelect12Wheel,
};
enum class BarUISetWordEnum : int
{
	BackgroundWarning,

	MainButton,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_ThicknessDisplay,
	DrawAttributeBar_ThicknessFineNumber,
	DrawAttributeBar_ThicknessMediumNumber,
	DrawAttributeBar_ThicknessCoarseNumber,
	DrawAttributeBar_PenTypeMenuFreeLine,
	DrawAttributeBar_ThicknessAnnotationLabel,
		DrawAttributeBar_ThicknessHoldLockLabel,
		DrawAttributeBar_ThicknessAnnotationPopupText,
		DrawAttributeBar_ThicknessAnnotationPopupBody,
		DrawAttributeBar_ThicknessOverflowPopupText,
		DrawAttributeBar_ThicknessOverflowPopupBody,
		DrawAttributeBar_ColorPickerRgb,
		DrawAttributeBar_ColorPickerG,
		DrawAttributeBar_ColorPickerB,
		DrawAttributeBar_ColorPickerOpacity,
		DrawAttributeBar_ColorPickerRgbValue,
		DrawAttributeBar_ColorPickerGValue,
		DrawAttributeBar_ColorPickerBValue,
		DrawAttributeBar_ColorPickerOpacityValue,
		DrawAttributeBar_ColorPickerHoldLabel,
		DrawAttributeBar_ThicknessPreviewPopupNumber,

		GeometryAttributeBar_StraightLine,
		GeometryAttributeBar_Rectangle,
		GeometryAttributeBar_ThicknessFineNumber,
		GeometryAttributeBar_ThicknessMediumNumber,
		GeometryAttributeBar_ThicknessCoarseNumber,
	};

enum class BarBorderCursorTrackingStateEnum : int
{
	Dormant,
	Inside,
	Grace,
};

// UI 总集
export class BarUISetClass
{
public:
	BarUISetClass() : spec(this) {};

	// 渲染
	void Rendering();
	// 鼠标交互
	void Interact();
	// 仅合并已经判定为展开/收起的动作，不拦截状态切换和原始输入。
	bool TryBeginToggle(Inkeys::UI::Bar::BarToggleChannel channel)
	{
		return toggleClickCoalescer.TryBegin(channel);
	}

public:
	BarWindowPosClass barWindow;
	BarMediaClass barMedia;
	BarButtonSetClass barButtonSet;
	BarUIRendering spec;

	BarStateClass barState;
	BarStyleClass barStyle;

	ankerl::unordered_dense::map<BarUISetShapeEnum, shared_ptr<BarUiShapeClass>> shapeMap;
	ankerl::unordered_dense::map<BarUISetSuperellipseEnum, shared_ptr<BarUiSuperellipseClass>> superellipseMap;
	ankerl::unordered_dense::map<BarUISetSvgEnum, shared_ptr<BarUiSVGClass>> svgMap;
	ankerl::unordered_dense::map<BarUISetPngEnum, shared_ptr<BarUiPNGClass>> pngMap;
	ankerl::unordered_dense::map<BarUISetWordEnum, shared_ptr<BarUiWordClass>> wordMap;

	// 绘制属性按钮同样复用自身背景层，仅单独记录悬停动画阶段。
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeBrushHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeHighlightHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributePenTypeExtensionHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributePenTypeFreeLineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessFineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessMediumHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessCoarseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessAdjustHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeAnnotationCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeOverflowCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeColorPickerToneHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeColorPickerCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> moreCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryStraightLineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryRectangleHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryThicknessFineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryThicknessMediumHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryThicknessCoarseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryCloseHoverStage = BarButtonHoverStageEnum::None;

public:
	// 渲染更新：状态更新 + 通知计算并渲染
	void UpdateRendering(bool updateState = true);
protected:
	// 拖动交互
	double Seek(const ExMessage& msg);
	bool SetBorderCursorRawInputEnabled(HWND hWnd, bool enabled);
	void ActivateBorderCursorTracking(HWND hWnd);
	void RegisterBorderCursorLight(HWND hWnd);
	void HandleBorderCursorGraceTimeout(HWND hWnd);
	void SuspendBorderCursorTracking(HWND hWnd, bool waitForMouseLeave = false);
	bool ScheduleBorderCursorGraceTimer(HWND hWnd, UINT delayMs);
	void HandleCanvasDrawingActivity(HWND hWnd, bool started);
	void CloseAnnotationTooltip();
	void CloseThicknessOverflowTooltip();
	void CloseDrawAttributeTooltips();
	void ClosePenTypeMenu();
	void CloseThicknessSlider(bool cancelCapture);
	void CloseColorPicker(bool cancelCapture);
	void ShutdownWindowInput(HWND hWnd);
	void RefreshBorderCursorVisibleRegions(double frameZoom);
	bool IsBorderCursorLightNearVisibleRegion(POINT screenPoint);
	std::atomic<unsigned long long> mainButtonClickPulseSerial = 0;
	Inkeys::UI::Bar::BarToggleClickCoalescer toggleClickCoalescer;

	mutex borderCursorLightMutex;
	D2D1_POINT_2F borderCursorLightPoint = D2D1::Point2F();
	unsigned long long borderCursorLightSerial = 0;
	bool borderCursorInputAvailable = false;
	bool borderCursorLightReady = false;
	bool borderCursorLightNearVisibleRegion = false;
	bool borderCursorRawInputRegistered = false;
	bool borderCursorRegistrationFailureLogged = false;
	bool borderCursorRemovalFailureLogged = false;
	bool borderCursorTimerFailureLogged = false;
	bool borderCursorActivationBlockedUntilLeave = false;
	BarBorderCursorTrackingStateEnum borderCursorTrackingState =
		BarBorderCursorTrackingStateEnum::Dormant;
	ULONGLONG borderCursorGraceDeadlineTick = 0;
	array<RECT, 6> borderCursorVisibleRegions{};
	size_t borderCursorVisibleRegionCount = 0;

	friend class BarUIRendering;
	friend class BarRenderLoopCoordinator;
	friend LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
// 全局 Bar UI 集合
export extern BarUISetClass barUISet;

// 初始化

namespace Inkeys::UI::Bar
{
	export WNDPROC WindowProc() noexcept;
	export void Initialization();
	export void SetAnimationOptions(bool enable, double speedRate);
	export void SetEdgeLightingOptions(bool enable, bool dynamic);
	export void SetDebugOptions(bool enable, bool showFrameRate);
	export void NotifyCanvasDrawingStarted();
	export void NotifyCanvasDrawingEnded();
	export bool TryQueueColorPickerKeyboardInput(BYTE vkCode, bool keyDown);

	bool InitializeWindow(BarUISetClass& barUISet);
	void InitializeUI(BarUISetClass& barUISet);
};
