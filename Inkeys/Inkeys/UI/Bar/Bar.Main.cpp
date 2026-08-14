module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include <dwrite_1.h>
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../Business/LegacyDrawState.hpp"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"
#include "Bar.PresentDecision.h"
#include <limits>

#pragma comment(lib, "dxguid.lib")

module Inkeys.UI.Bar;
import :Main;
import :Rendering;
import :Layout;
import :Atomic;
import :Zoom;
import :Theme;

import Inkeys.UI.Bar.FramePacing;
import Inkeys.UI.RenderPipeline;

import <ranges>;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;

// Interaction 实现单元独占窗口消息状态；协调器仅通过窄接口读取或投递。
bool ReadColorPickerEntryPressed();
void RequestBarBorderCursorSuspend();
extern constexpr double BarButtonPressScale = 0.95;
extern constexpr double BarButtonHoverFadeDur = 5.0;
// Rendering 与 topology 共享同一组 module-linkage 常量，拆分后不复制数值。
extern constexpr double BarButtonCursorLightIntensity = 0.30;
extern constexpr double BarButtonPressedLightOpacity = 0.5;
extern constexpr double BarDrawAttributeExpandedHeight = 185.0;
extern constexpr double BarDrawAttributeCompactWidth = 60.0;
extern constexpr double BarDrawAttributeCompactScale =
	BarDrawAttributeCompactWidth / BarDrawAttributeExpandedWidth;
extern constexpr double BarDrawAttributeCompactHeight =
	BarDrawAttributeExpandedHeight * BarDrawAttributeCompactScale;
extern constexpr double BarDrawAttributeThicknessHeight = 105.0;
extern constexpr double BarDrawAttributeThicknessControlHeight = 30.0;
extern constexpr double BarDrawAttributeSurfaceOpacity = 0.95;
extern constexpr double BarDrawAttributeThicknessContentInset =
	BarDrawAttributeGap * 2.0;
// 几何属性与绘制属性共用同一组分割线参数，保证光影和圆角一致。
extern constexpr double BarUiDividerRadius = 0.5;
extern constexpr double BarUiDividerCursorLightIntensity = 0.30;
extern constexpr double BarDrawAttributePenTypeButtonWidth = 115.0;
extern constexpr double BarDrawAttributePenTypeButtonHeight = 30.0;
extern constexpr double BarDrawAttributePenTypeLeft =
	BarDrawAttributeExpandedWidth - BarDrawAttributeGap
	- BarDrawAttributePenTypeButtonWidth;
extern constexpr double BarDrawAttributeThicknessDividerLeft =
	BarDrawAttributeGap;
extern constexpr double BarDrawAttributeThicknessDividerRight =
	BarDrawAttributePenTypeLeft - BarDrawAttributeGap;
extern constexpr double BarDrawAttributeThicknessDividerWidth =
	BarDrawAttributeThicknessDividerRight
	- BarDrawAttributeThicknessDividerLeft;
extern constexpr double BarDrawAttributeThicknessAdjustX =
	BarDrawAttributeThicknessDividerRight
	- BarDrawAttributeThicknessControlHeight;
extern constexpr double BarDrawAttributeThicknessPresetStartX =
	BarDrawAttributeThicknessAdjustX
	- (BarDrawAttributeThicknessControlHeight + BarDrawAttributeGap) * 3.0;
extern constexpr double BarDrawAttributePenTypeExtensionDividerX = 85.0;
extern constexpr double BarDrawAttributePenTypeExtensionWidth =
	BarDrawAttributePenTypeButtonWidth
	- BarDrawAttributePenTypeExtensionDividerX;
extern constexpr double BarDrawAttributePenTypeMenuRowHeight = 30.0;
extern constexpr double BarDrawAttributePenTypeMenuPadding = 5.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckAreaWidth = 26.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckSize = 12.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckInset =
	(BarDrawAttributePenTypeMenuCheckAreaWidth
		- BarDrawAttributePenTypeMenuCheckSize) / 2.0;
extern constexpr double BarDrawAttributePenTypeMenuHeight =
	BarDrawAttributePenTypeMenuPadding * 2.0
	+ BarDrawAttributePenTypeMenuRowHeight * 2.0;
extern constexpr double BarGeometryAttributeExpandedWidth = 335.0;
extern constexpr double BarGeometryAttributeExpandedHeight = 100.0;
extern constexpr double BarGeometryAttributeCompactWidth = 60.0;
extern constexpr double BarGeometryAttributeCompactScale =
	BarGeometryAttributeCompactWidth / BarGeometryAttributeExpandedWidth;
extern constexpr double BarGeometryAttributeCompactHeight =
	BarGeometryAttributeExpandedHeight * BarGeometryAttributeCompactScale;
extern constexpr double BarGeometryAttributeGap = 5.0;
extern constexpr double BarGeometryAttributeThicknessButtonSize = 30.0;
extern constexpr double BarGeometryAttributeDividerCursorLightIntensity =
	BarUiDividerCursorLightIntensity;
extern constexpr double BarThicknessSliderTrackHeight = 4.0;
extern constexpr double BarThicknessSliderThumbCenterDiameter = 12.0;
extern constexpr double BarThicknessSliderThumbHoverCenterDiameter = 14.0;
extern constexpr double BarThicknessSliderThumbPressedCenterDiameter = 10.0;
extern constexpr double BarThicknessSliderThumbAnimationDur = 0.28;
	extern constexpr double BarThicknessSliderPressAnimationDur = 0.12;
	extern constexpr double BarThicknessPreviewPopupAnimationDur = 0.40;
	extern constexpr double BarThicknessPreviewNumberAnimationDur = 0.18;
	extern constexpr double BarThicknessPreviewPopupPadding = 8.0;
	extern constexpr double BarThicknessPreviewPopupThumbGap = 10.0;
	extern constexpr double BarThicknessPreviewAvoidGap = 5.0;
	extern constexpr double BarThicknessPreviewNumberGap = 5.0;
	extern constexpr double BarThicknessPreviewNumberInset = 5.0;
	extern constexpr double BarThicknessPreviewNumberFontSize = 13.0;
	// FineDial 激活区沿 Preview 展开方向排列，数值均为未缩放 DIP。
	extern constexpr double BarThicknessFineDialActivationPreviewBaseOpacity = 0.5;
	extern constexpr double BarThicknessFineDialActivationPreviewEnterDur = 0.18;
	extern constexpr double BarThicknessFineDialActivationPreviewFadeOutDur = 0.30;
	extern constexpr double BarThicknessFineDialSelectionTransitionDur = 0.18;
	extern constexpr double BarThicknessFineDialPopupPanelGapDip = 8.0;
	extern constexpr double BarThicknessFineDialTransitionDur = 0.28;
	extern constexpr double BarThicknessFineDialThetaLimit = 1.20;
	extern constexpr double BarThicknessFineDialDepthLiftDip = 4.0;
	extern constexpr double BarThicknessFineDialEdgeFadeStart = 0.68;
	extern constexpr double BarThicknessFineDialTickLengthDip = 7.0;
	extern constexpr double BarThicknessFineDialMajorTickLengthDip = 12.0;
	extern constexpr double BarThicknessFineDialSelectorWidthDip = 7.0;
	extern constexpr double BarThicknessFineDialSelectorHeightDip = 5.0;
	extern constexpr double BarThicknessSliderThumbMorphExitOpacity = 0.04;
	// 拖动改值后静止 0.5s 出提示，再 1.5s（合计 2.0s）进度走满并锁定粗细。
	extern constexpr double BarThicknessHoldHintAnimDur = 0.18;
	extern constexpr double BarThicknessHoldExchangeAnimDur = 0.12;
	// 圆环相对文字行高为 3/5，并比默认 5px 间隙更贴近文字。
	extern constexpr double BarThicknessHoldRingSizeScale = 3.0 / 5.0;
	extern constexpr double BarThicknessHoldRingTextGap = 2.0;
	extern constexpr double BarThicknessTooltipBadgeHeight = 24.0;
extern constexpr double BarThicknessTooltipIconSize = 14.0;
	extern constexpr double BarThicknessTooltipCloseButtonSize = 20.0;
extern constexpr double BarThicknessTooltipHitPadding = 2.0;
extern constexpr double BarThicknessTooltipPadding = 8.0;
extern constexpr double BarThicknessTooltipCloseReserve = 25.0;
extern constexpr double BarThicknessTooltipTitleFontSize = 12.0;
extern constexpr double BarThicknessTooltipBodyFontSize = 10.0;
extern constexpr double BarThicknessTooltipLineGap = 3.0;
extern constexpr double BarThicknessTooltipPopupGap =
	BarDrawAttributeThicknessContentInset;
extern constexpr double BarThicknessTooltipFillOpacity =
	BarDrawAttributeSurfaceOpacity;
extern constexpr double BarThicknessTooltipFrameOpacity = 0.18;
extern constexpr double BarColorSwatchFrameOpacity = 0.18;
extern constexpr double BarColorPickerPanelWidth = 300.0;
// 5 顶距 + 30 顶栏 + 5 间隔 + 132 色板 + 32 读数（含底部 10px 额外间隙）+ 5 底距 = 209。
extern constexpr double BarColorPickerPanelHeight = 209.0;
extern constexpr double BarColorPickerPaletteInset = 5.0;
extern constexpr double BarColorPickerPaletteTop = 40.0;
extern constexpr double BarColorPickerPaletteWidth = 290.0;
extern constexpr double BarColorPickerPaletteHeight = 132.0;
// 与粗细快速调节按钮同高（30px），顶部色系/预览/关闭共用。
extern constexpr double BarColorPickerChromeHeight = 30.0;
extern constexpr double BarColorPickerChromeTop = 5.0;
extern constexpr double BarColorPickerPanelGap = BarDrawAttributeGap;
// 颜色选择器与绘制属性窗口共用默认展开时长，保证回弹节奏一致。
extern constexpr double BarColorPickerPanelAnimationDur = 0.40;
extern constexpr double BarColorPickerCompactScale = BarDrawAttributeCompactScale;
extern constexpr double BarColorPickerCompactWidth =
	BarColorPickerPanelWidth * BarColorPickerCompactScale;
extern constexpr double BarColorPickerCompactHeight =
	BarColorPickerPanelHeight * BarColorPickerCompactScale;
extern constexpr double BarColorPickerHoldHintAnimationDur = 0.18;
extern constexpr double BarMorePanelGap = 5.0;
// 主栏与浮层之间留出更明显的净空；网格单元仍沿用标准 5 DIP 间距。
extern constexpr double BarMorePanelAnchorGap = 12.0;
extern constexpr double BarMorePanelPadding = 5.0;
// 关闭按钮放在网格右侧的窄栏，不再占用面板顶部高度。
extern constexpr double BarMorePanelCloseSideWidth = 35.0;
extern constexpr double BarMorePanelSeparatorGap = 10.0;
extern constexpr double BarMorePanelCompactScale = 0.16;
extern constexpr double BarMorePanelCompactWidth = 60.0;
extern constexpr double BarMorePanelCompactHeight = 30.0;
// ====================
// 媒体

// 媒体操控类
void BarMediaClass::LoadFormat()
{
	formatCache = make_unique<BarFormatCache>(
		Inkeys::UI::RenderPipeline::DWriteFactory().Get());
}

// ====================
// 界面

void BarUISetClass::UpdateRendering(bool updateState)
{
	static mutex mtx;
	lock_guard<mutex> lock(mtx);

	// 状态更新
	if (updateState)
	{
		barButtonSet.StateUpdate();
		// 非画笔模式的 GetPenWidth 为 0，收起过程中保留最后一次有效的粗细文字。
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
			barState.ThicknessDisplayUpdate();
	}

	// 通知计算并渲染
	BarAtomic::wait.Notify();
	Inkeys::UI::RenderPipeline::Request(
		Inkeys::UI::RenderPipeline::Client::Bar);
}

// 全局 Bar UI 集合
BarUISetClass barUISet;

// ====================
// 环境

// 初始化

namespace Inkeys::UI::Bar
{
	void SetAnimationOptions(bool enable, double speedRate)
	{
		speedRate = isfinite(speedRate) ? clamp(speedRate, 0.1, 5.0) : 1.0;
		// 使用足够大的有限倍率统一完成普通动画、批次和 SVG 关键帧，不能用 0 让时间轴停住。
		BarUiAnimationEnabled = enable;
		BarUiAnimationSpeedRate = enable ? speedRate : 1.0e12;
		if (!enable) RequestBarBorderCursorSuspend();
		barUISet.UpdateRendering(false);
	}

	void SetEdgeLightingOptions(bool enable, bool dynamic)
	{
		BarUiEdgeLightingEnabled = enable;
		BarUiDynamicEdgeLightingEnabled = dynamic;
		// 关闭任一级动态光门禁时，统一交给 Bar 窗口线程注销 Raw Input。
		if (!enable || !dynamic) RequestBarBorderCursorSuspend();
		barUISet.UpdateRendering(false);
	}

	void SetDebugOptions(bool enable, bool showFrameRate)
	{
		BarUiDebugModeEnabled = enable;
		BarUiDebugFrameRateEnabled = showFrameRate;
		// 渲染线程会比较新旧选项，只在需要时清除 FPS 文字或红框。
		barUISet.UpdateRendering(false);
	}


}
