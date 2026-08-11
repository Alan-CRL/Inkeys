module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtD2DPreparation.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtFloating.h"
#include "../../../IdtState.h"
#include "../../../IdtWindow.h"
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

import <ranges>;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;

// Rendering 只读 Main 的 module-linkage 常量，公式保持单一定义。
bool ReadColorPickerEntryPressed();
void RequestBarBorderCursorSuspend();
extern const double BarButtonPressScale;
extern const double BarButtonHoverFadeDur;
extern const double BarButtonCursorLightIntensity;
extern const double BarButtonPressedLightOpacity;
extern const double BarDrawAttributeExpandedHeight;
extern const double BarDrawAttributeCompactWidth;
extern const double BarDrawAttributeCompactScale;
extern const double BarDrawAttributeCompactHeight;
extern const double BarDrawAttributeThicknessHeight;
extern const double BarDrawAttributeThicknessControlHeight;
extern const double BarDrawAttributeSurfaceOpacity;
extern const double BarDrawAttributeThicknessContentInset;
extern const double BarUiDividerRadius;
extern const double BarUiDividerCursorLightIntensity;
extern const double BarDrawAttributePenTypeButtonWidth;
extern const double BarDrawAttributePenTypeButtonHeight;
extern const double BarDrawAttributePenTypeLeft;
extern const double BarDrawAttributeThicknessDividerLeft;
extern const double BarDrawAttributeThicknessDividerRight;
extern const double BarDrawAttributeThicknessDividerWidth;
extern const double BarDrawAttributeThicknessAdjustX;
extern const double BarDrawAttributeThicknessPresetStartX;
extern const double BarDrawAttributePenTypeExtensionDividerX;
extern const double BarDrawAttributePenTypeExtensionWidth;
extern const double BarDrawAttributePenTypeMenuRowHeight;
extern const double BarDrawAttributePenTypeMenuPadding;
extern const double BarDrawAttributePenTypeMenuCheckAreaWidth;
extern const double BarDrawAttributePenTypeMenuCheckSize;
extern const double BarDrawAttributePenTypeMenuCheckInset;
extern const double BarDrawAttributePenTypeMenuHeight;
extern const double BarGeometryAttributeExpandedWidth;
extern const double BarGeometryAttributeExpandedHeight;
extern const double BarGeometryAttributeCompactWidth;
extern const double BarGeometryAttributeCompactScale;
extern const double BarGeometryAttributeCompactHeight;
extern const double BarGeometryAttributeGap;
extern const double BarGeometryAttributeThicknessButtonSize;
extern const double BarGeometryAttributeDividerCursorLightIntensity;
extern const double BarThicknessSliderTrackHeight;
extern const double BarThicknessSliderThumbCenterDiameter;
extern const double BarThicknessSliderThumbHoverCenterDiameter;
extern const double BarThicknessSliderThumbPressedCenterDiameter;
extern const double BarThicknessSliderThumbAnimationDur;
extern const double BarThicknessSliderPressAnimationDur;
extern const double BarThicknessPreviewPopupAnimationDur;
extern const double BarThicknessPreviewNumberAnimationDur;
extern const double BarThicknessPreviewPopupPadding;
extern const double BarThicknessPreviewPopupThumbGap;
extern const double BarThicknessPreviewAvoidGap;
extern const double BarThicknessPreviewNumberGap;
extern const double BarThicknessPreviewNumberInset;
extern const double BarThicknessPreviewNumberFontSize;
extern const double BarThicknessFineDialActivationPreviewBaseOpacity;
extern const double BarThicknessFineDialActivationPreviewEnterDur;
extern const double BarThicknessFineDialActivationPreviewFadeOutDur;
extern const double BarThicknessFineDialSelectionTransitionDur;
extern const double BarThicknessFineDialPopupPanelGapDip;
extern const double BarThicknessFineDialTransitionDur;
extern const double BarThicknessFineDialThetaLimit;
extern const double BarThicknessFineDialDepthLiftDip;
extern const double BarThicknessFineDialEdgeFadeStart;
extern const double BarThicknessFineDialTickLengthDip;
extern const double BarThicknessFineDialMajorTickLengthDip;
extern const double BarThicknessFineDialSelectorWidthDip;
extern const double BarThicknessFineDialSelectorHeightDip;
extern const double BarThicknessSliderThumbMorphExitOpacity;
extern const double BarThicknessHoldHintAnimDur;
extern const double BarThicknessHoldExchangeAnimDur;
extern const double BarThicknessHoldRingSizeScale;
extern const double BarThicknessHoldRingTextGap;
extern const double BarThicknessTooltipBadgeHeight;
extern const double BarThicknessTooltipIconSize;
extern const double BarThicknessTooltipCloseButtonSize;
extern const double BarThicknessTooltipHitPadding;
extern const double BarThicknessTooltipPadding;
extern const double BarThicknessTooltipCloseReserve;
extern const double BarThicknessTooltipTitleFontSize;
extern const double BarThicknessTooltipBodyFontSize;
extern const double BarThicknessTooltipLineGap;
extern const double BarThicknessTooltipPopupGap;
extern const double BarThicknessTooltipFillOpacity;
extern const double BarThicknessTooltipFrameOpacity;
extern const double BarColorSwatchFrameOpacity;
extern const double BarColorPickerPanelWidth;
extern const double BarColorPickerPanelHeight;
extern const double BarColorPickerPaletteInset;
extern const double BarColorPickerPaletteTop;
extern const double BarColorPickerPaletteWidth;
extern const double BarColorPickerPaletteHeight;
extern const double BarColorPickerChromeHeight;
extern const double BarColorPickerChromeTop;
extern const double BarColorPickerPanelGap;
extern const double BarColorPickerPanelAnimationDur;
extern const double BarColorPickerCompactScale;
extern const double BarColorPickerCompactWidth;
extern const double BarColorPickerCompactHeight;
extern const double BarColorPickerHoldHintAnimationDur;
extern const double BarMorePanelGap;
extern const double BarMorePanelAnchorGap;
extern const double BarMorePanelPadding;
extern const double BarMorePanelCloseSideWidth;
extern const double BarMorePanelSeparatorGap;
extern const double BarMorePanelCompactScale;
extern const double BarMorePanelCompactWidth;
extern const double BarMorePanelCompactHeight;
constexpr int BarThicknessFineDialVisibleTickLimit = 64;

enum class ThicknessFineDialRangeTransitionPhase
{
	Idle,
	RevealNewRange,
	MoveValue,
	RetireOldRange,
};

struct BarRenderFrameSnapshot
{
	StateModeSelectEnum stateMode = StateModeSelectEnum::IdtSelection;
	PenModeSelectEnum penMode = PenModeSelectEnum::IdtPenBrush1;
	COLORREF brush1Color = RGB(0, 0, 0);
	COLORREF highlighterColor = RGB(0, 0, 0);
	bool penetrate = false;
	unsigned long long demandGeneration = 0;
	double zoom = 1.0;
	double animationDtSeconds = 0.0;
	double animationSpeedRate = 1.0;
	int ordinal = 2;
};

enum class BarRenderLoopStageResult
{
	Proceed,
	Continue,
	Stop,
};

// 只保存渲染线程跨帧使用的状态；控件拓扑仍由 BarUISetClass 持有。
struct BarRenderLoopState
{
	explicit BarRenderLoopState(
		BarUISetClass& owner,
		std::atomic<unsigned long long>& mainButtonPulseSerial)
		: barWindow(owner.barWindow),
		barMedia(owner.barMedia),
		barButtonSet(owner.barButtonSet),
		spec(owner.spec),
		barState(owner.barState),
		barStyle(owner.barStyle),
		shapeMap(owner.shapeMap),
		superellipseMap(owner.superellipseMap),
		svgMap(owner.svgMap),
		pngMap(owner.pngMap),
		wordMap(owner.wordMap),
		drawAttributeBrushHoverStage(owner.drawAttributeBrushHoverStage),
		drawAttributeHighlightHoverStage(owner.drawAttributeHighlightHoverStage),
		drawAttributePenTypeExtensionHoverStage(
			owner.drawAttributePenTypeExtensionHoverStage),
		drawAttributePenTypeFreeLineHoverStage(
			owner.drawAttributePenTypeFreeLineHoverStage),
		drawAttributeThicknessFineHoverStage(
			owner.drawAttributeThicknessFineHoverStage),
		drawAttributeThicknessMediumHoverStage(
			owner.drawAttributeThicknessMediumHoverStage),
		drawAttributeThicknessCoarseHoverStage(
			owner.drawAttributeThicknessCoarseHoverStage),
		drawAttributeThicknessAdjustHoverStage(
			owner.drawAttributeThicknessAdjustHoverStage),
		drawAttributeAnnotationCloseHoverStage(
			owner.drawAttributeAnnotationCloseHoverStage),
		drawAttributeOverflowCloseHoverStage(
			owner.drawAttributeOverflowCloseHoverStage),
		drawAttributeColorPickerToneHoverStage(
			owner.drawAttributeColorPickerToneHoverStage),
		drawAttributeColorPickerCloseHoverStage(
			owner.drawAttributeColorPickerCloseHoverStage),
		moreCloseHoverStage(owner.moreCloseHoverStage),
		geometryStraightLineHoverStage(owner.geometryStraightLineHoverStage),
		geometryRectangleHoverStage(owner.geometryRectangleHoverStage),
		geometryThicknessFineHoverStage(owner.geometryThicknessFineHoverStage),
		geometryThicknessMediumHoverStage(owner.geometryThicknessMediumHoverStage),
		geometryThicknessCoarseHoverStage(owner.geometryThicknessCoarseHoverStage),
		geometryCloseHoverStage(owner.geometryCloseHoverStage),
		mainButtonClickPulseSerial(mainButtonPulseSerial),
		presentDecision(RECT(0, 0, barWindow.w, barWindow.h))
	{
		auto range = GetBarThicknessSliderRange(
			stateMode.Pen.ModeSelect, barStyle.dpiZoom);
		if (range.supported && range.max > range.min)
		{
			drawAttributeThicknessSliderNormalized.SetDirect(clamp(
				(static_cast<double>(GetPenWidth()) - range.min)
				/ static_cast<double>(range.max - range.min),
				0.0, 1.0));
			drawAttributeThicknessSliderNormalizedInitialized = true;
		}
	}

	BarWindowPosClass& barWindow;
	BarMediaClass& barMedia;
	BarButtonSetClass& barButtonSet;
	BarUIRendering& spec;
	BarStateClass& barState;
	BarStyleClass& barStyle;
	decltype(BarUISetClass::shapeMap)& shapeMap;
	decltype(BarUISetClass::superellipseMap)& superellipseMap;
	decltype(BarUISetClass::svgMap)& svgMap;
	decltype(BarUISetClass::pngMap)& pngMap;
	decltype(BarUISetClass::wordMap)& wordMap;
	decltype(BarUISetClass::drawAttributeBrushHoverStage)&
		drawAttributeBrushHoverStage;
	decltype(BarUISetClass::drawAttributeHighlightHoverStage)&
		drawAttributeHighlightHoverStage;
	decltype(BarUISetClass::drawAttributePenTypeExtensionHoverStage)&
		drawAttributePenTypeExtensionHoverStage;
	decltype(BarUISetClass::drawAttributePenTypeFreeLineHoverStage)&
		drawAttributePenTypeFreeLineHoverStage;
	decltype(BarUISetClass::drawAttributeThicknessFineHoverStage)&
		drawAttributeThicknessFineHoverStage;
	decltype(BarUISetClass::drawAttributeThicknessMediumHoverStage)&
		drawAttributeThicknessMediumHoverStage;
	decltype(BarUISetClass::drawAttributeThicknessCoarseHoverStage)&
		drawAttributeThicknessCoarseHoverStage;
	decltype(BarUISetClass::drawAttributeThicknessAdjustHoverStage)&
		drawAttributeThicknessAdjustHoverStage;
	decltype(BarUISetClass::drawAttributeAnnotationCloseHoverStage)&
		drawAttributeAnnotationCloseHoverStage;
	decltype(BarUISetClass::drawAttributeOverflowCloseHoverStage)&
		drawAttributeOverflowCloseHoverStage;
	decltype(BarUISetClass::drawAttributeColorPickerToneHoverStage)&
		drawAttributeColorPickerToneHoverStage;
	decltype(BarUISetClass::drawAttributeColorPickerCloseHoverStage)&
		drawAttributeColorPickerCloseHoverStage;
	decltype(BarUISetClass::moreCloseHoverStage)& moreCloseHoverStage;
	decltype(BarUISetClass::geometryStraightLineHoverStage)&
		geometryStraightLineHoverStage;
	decltype(BarUISetClass::geometryRectangleHoverStage)& geometryRectangleHoverStage;
	decltype(BarUISetClass::geometryThicknessFineHoverStage)&
		geometryThicknessFineHoverStage;
	decltype(BarUISetClass::geometryThicknessMediumHoverStage)&
		geometryThicknessMediumHoverStage;
	decltype(BarUISetClass::geometryThicknessCoarseHoverStage)&
		geometryThicknessCoarseHoverStage;
	decltype(BarUISetClass::geometryCloseHoverStage)& geometryCloseHoverStage;
	std::atomic<unsigned long long>& mainButtonClickPulseSerial;

	unsigned long long barDeviceResourceFailureGeneration = 0;
	bool barPresentFailureLogged = false;
	chrono::high_resolution_clock::time_point reckon =
		chrono::high_resolution_clock::now();
	Inkeys::UI::Bar::FrameAnimationClock animationClock;
	RECT current = RECT(0, 0, 0, 0);
	Inkeys::UI::Bar::BarPresentDecision presentDecision;
	unsigned long long presentAttemptFrameSerial = 0;
	bool mainBarLayoutSide = barState.widgetPosition.mainBar;
	bool drawAttributeLayoutSide = barState.widgetPosition.primaryBar;
	bool drawAttributeLayoutOpen = barState.drawAttribute;
	bool geometryAttributeLayoutSide = barState.widgetPosition.primaryBar;
	bool geometryAttributeLayoutOpen = barState.geometryAttribute;
	BarUiTimelineClass mainBarTimeline;
	BarUiTimelineClass drawAttributeTimeline;
	BarUiTimelineClass geometryAttributeTimeline;
	BarUiValueClass morePanelProgress{ 0.0 };
	BarUiValueClass morePanelOpacity{ 0.0 };
	int mainLogoInkColorSource = -1;
	bool mainLogoInkCarriesHighlighterHistory = false;
	BarUiCurveEnum mainBarBatchCurve = BarUiCurveEnum::EaseInOutCubic;
	const BarUiCurveSpecClass buttonPressCurve{
		BarUiCurveEnum::EaseOutCubic, BarUiCurveEnum::EaseOutCubic, 0.0, false };
	const BarUiCurveSpecClass buttonReleaseCurve{
		BarUiCurveEnum::EaseOutBack, BarUiCurveEnum::EaseOutBack, 0.0, false };
	optional<double> mainBarLayoutWidth;
	BarUiValueClass drawAttributePenThickness{ max(0.0f, GetPenWidth()) };
	bool drawAttributePenThicknessInitialized =
		stateMode.StateModeSelect == StateModeSelectEnum::IdtPen;
	BarUiValueClass drawAttributePenPreviewMorph{
		stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1 ? 1.0 : 0.0 };
	bool drawAttributePenPreviewMorphInitialized =
		stateMode.StateModeSelect == StateModeSelectEnum::IdtPen;
	BarUiValueClass drawAttributeThicknessSliderProgress{ 0.0 };
	BarUiValueClass drawAttributeThicknessSliderTrackOpacity{ 0.0 };
	BarUiValueClass drawAttributeThicknessFineDialProgress{ 0.0 };
	BarUiValueClass drawAttributeThicknessFineDialRecognitionVisibility{ 0.0 };
	BarUiValueClass drawAttributeThicknessFineDialDwellProgress{ 0.0 };
	BarUiValueClass drawAttributeThicknessFineDialSelectionProgress{ 0.0 };
	bool drawAttributeThicknessFineDialActivationGeometryTransition = false;
	ThicknessFineDialRangeTransitionPhase thicknessFineDialRangeTransitionPhase =
		ThicknessFineDialRangeTransitionPhase::Idle;
	BarUiValueClass thicknessFineDialOldRangeOpacity{ 0.0 };
	BarUiValueClass thicknessFineDialNewRangeOpacity{ 1.0 };
	BarThicknessSliderRange thicknessFineDialOldRenderRange{};
	BarThicknessSliderRange thicknessFineDialNewRenderRange{};
	BarThicknessSliderRange thicknessFineDialLastLogicalRange =
		GetBarThicknessSliderRange(stateMode.Pen.ModeSelect, barStyle.dpiZoom);
	PenModeSelectEnum thicknessFineDialLastPenMode = stateMode.Pen.ModeSelect;
	BarUiValueClass drawAttributeThicknessSliderThumbOpacity{ 0.0 };
	BarUiValueClass drawAttributeThicknessSliderThumbScale{ 0.75 };
	BarUiValueClass drawAttributeThicknessSliderAccentOpacity{ 1.0 };
	BarUiValueClass drawAttributeThicknessSliderCenterDiameter{
		BarThicknessSliderThumbCenterDiameter };
	bool drawAttributeThicknessSliderTargetActive = false;
	bool drawAttributeThicknessSliderPositionLocked = false;
	bool drawAttributeOverflowSliderSessionAllowsHint = false;
	BarUiValueClass drawAttributeThicknessSliderNormalized{ 0.0 };
	bool drawAttributeThicknessSliderNormalizedInitialized = false;
	BarUiValueClass drawAttributeThicknessPreviewPopupProgress{ 0.0 };
	BarUiValueClass drawAttributeThicknessPreviewPopupRetargetProgress{ 1.0 };
	BarUiValueClass drawAttributeThicknessPreviewNumberInsideProgress{ 0.0 };
	BarUiValueClass drawAttributeThicknessHoldExchangeProgress{ 0.0 };
	BarUiValueClass drawAttributeThicknessHoldGroupScale{ 0.82 };
	D2D1_RECT_F drawAttributeThicknessPreviewNumberRect{};
	D2D1_POINT_2F drawAttributeThicknessPreviewPopupAnchor{};
	double drawAttributeThicknessPreviewPopupScale = 0.0;
	bool drawAttributeThicknessPreviewPopupGeometryValid = false;
	bool drawAttributeThicknessPreviewPopupTargetVisible = false;
	bool drawAttributeThicknessPreviewPopupTargetFineDial = false;
	bool drawAttributeThicknessPreviewPopupExitPositionLatched = false;
	bool drawAttributeThicknessPreviewPopupRenderedCenterValid = false;
	D2D1_POINT_2F drawAttributeThicknessPreviewPopupRenderedCenter{};
	D2D1_POINT_2F drawAttributeThicknessPreviewPopupExitCenter{};
	int drawAttributeThicknessPreviewMeasuredValue = -1;
	wstring drawAttributeThicknessPreviewMeasuredText;
	D2D1_SIZE_F drawAttributeThicknessPreviewMeasuredSize{};
	BarUiValueClass drawAttributeAnnotationPopupProgress{ 0.0 };
	BarUiValueClass drawAttributeOverflowPopupProgress{ 0.0 };
	BarUiValueClass drawAttributeOverflowBadgeProgress{ 0.0 };
	BarUiValueClass drawAttributePenTypeMenuProgress{ 0.0 };
	BarUiValueClass drawAttributeColorPickerProgress{ 0.0 };
	BarUiValueClass drawAttributeColorPickerEntryPressScale{ 1.0 };
	BarUiValueClass drawAttributeColorPickerToneMix{ 0.0 };
	BarUiValueClass drawAttributeColorPickerHoldOpacity{ 0.0 };
	BarUiValueClass drawAttributeColorPickerHoldRingOpacity{ 0.0 };
	BarUiValueClass drawAttributeColorPickerHoldTextMix{ 0.0 };
	bool drawAttributeColorPickerHoldTargetActive = false;
	bool drawAttributeColorPickerHoldOnTop = true;
	BarUiValueClass drawAttributeColorPickerDisplayR{ 0.0 };
	BarUiValueClass drawAttributeColorPickerDisplayG{ 0.0 };
	BarUiValueClass drawAttributeColorPickerDisplayB{ 0.0 };
	BarUiValueClass drawAttributeColorPickerDisplayOpacity{ 100.0 };
	bool drawAttributeColorPickerDisplayInitialized = false;
	BarUiValueClass drawAttributeThicknessHoldRingLockOpacity{ 1.0 };
	BarUiValueClass drawAttributeThicknessHoldTextMix{ 0.0 };
	D2D1_SIZE_F holdLockLabelTextSize =
		spec.MeasureText(L"保持并固定粗细", 13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerHoldTextSize =
		spec.MeasureText(L"保持并固定颜色", 12.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerFooterTextSize =
		spec.MeasureText(L"Ag", 13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerFooterRgbValueSize =
		spec.MeasureText(L"255", 13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerFooterOpacityValueSize =
		spec.MeasureText(L"100%", 13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerFooterRLabelSize = spec.MeasureText(
		wordMap[BarUISetWordEnum::DrawAttributeBar_ColorPickerRgb]->content.GetVal(),
		13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerFooterGLabelSize = spec.MeasureText(
		wordMap[BarUISetWordEnum::DrawAttributeBar_ColorPickerG]->content.GetVal(),
		13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerFooterBLabelSize = spec.MeasureText(
		wordMap[BarUISetWordEnum::DrawAttributeBar_ColorPickerB]->content.GetVal(),
		13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F colorPickerFooterOpacityLabelSize = spec.MeasureText(
		wordMap[BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacity]
			->content.GetVal(),
		13.0, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F annotationPopupTitleSize = spec.MeasureText(
		L"启用标注线（暂不可用）",
		BarThicknessTooltipTitleFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
	D2D1_SIZE_F annotationPopupBodySize = spec.MeasureText(
		L"锁定绘制方向仅为水平、竖直或斜45°",
		BarThicknessTooltipBodyFontSize, DWRITE_FONT_WEIGHT_NORMAL);
	D2D1_SIZE_F overflowPopupTitleSize = spec.MeasureText(
		L"墨迹粗细超出预览范围",
		BarThicknessTooltipTitleFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
	D2D1_SIZE_F overflowPopupBodySize = spec.MeasureText(
		L"预览中的粗细可能与绘制粗细不一致。",
		BarThicknessTooltipBodyFontSize, DWRITE_FONT_WEIGHT_NORMAL);
	double annotationPopupWidth = ceil(max(
		annotationPopupTitleSize.width, annotationPopupBodySize.width))
		+ BarThicknessTooltipPadding * 2.0 + BarThicknessTooltipCloseReserve;
	double annotationPopupHeight = ceil(annotationPopupTitleSize.height
		+ BarThicknessTooltipLineGap + annotationPopupBodySize.height)
		+ BarThicknessTooltipPadding * 2.0;
	double overflowPopupWidth = ceil(max(
		overflowPopupTitleSize.width, overflowPopupBodySize.width))
		+ BarThicknessTooltipPadding * 2.0 + BarThicknessTooltipCloseReserve;
	double overflowPopupHeight = ceil(overflowPopupTitleSize.height
		+ BarThicknessTooltipLineGap + overflowPopupBodySize.height)
		+ BarThicknessTooltipPadding * 2.0;
	BarUiValueClass drawAttributeBrushPressScale{ 1.0 };
	BarUiValueClass drawAttributeHighlightPressScale{ 1.0 };
	BarUiValueClass drawAttributePenTypeExtensionPressScale{ 1.0 };
	BarUiValueClass drawAttributePenTypeFreeLinePressScale{ 1.0 };
	BarUiValueClass drawAttributeThicknessFinePressScale{ 1.0 };
	BarUiValueClass drawAttributeThicknessMediumPressScale{ 1.0 };
	BarUiValueClass drawAttributeThicknessCoarsePressScale{ 1.0 };
	BarUiValueClass drawAttributeThicknessAdjustPressScale{ 1.0 };
	BarUiValueClass drawAttributeAnnotationClosePressScale{ 1.0 };
	BarUiValueClass drawAttributeOverflowClosePressScale{ 1.0 };
	BarUiValueClass drawAttributeColorPickerTonePressScale{ 1.0 };
	BarUiValueClass drawAttributeColorPickerClosePressScale{ 1.0 };
	BarUiValueClass moreClosePressScale{ 1.0 };
	BarUiValueClass geometryStraightLinePressScale{ 1.0 };
	BarUiValueClass geometryRectanglePressScale{ 1.0 };
	BarUiValueClass geometryThicknessFinePressScale{ 1.0 };
	BarUiValueClass geometryThicknessMediumPressScale{ 1.0 };
	BarUiValueClass geometryThicknessCoarsePressScale{ 1.0 };
	BarUiValueClass geometryClosePressScale{ 1.0 };
	bool drawAttributeAnnotationCloseVisible = false;
	bool drawAttributeOverflowCloseVisible = false;
	static constexpr double mainButtonScale = 1.05;
	static constexpr double mainButtonBaseSize = 80.0;
	shared_ptr<BarUiSVGClass> mainButtonLogo = svgMap[BarUISetSvgEnum::logo1];
	double mainButtonLogoBaseW = mainButtonLogo->w.tar;
	double mainButtonLogoBaseH = mainButtonLogo->h.tar;
	unsigned long long handledMainButtonPulseSerial = 0;
	Inkeys::UI::Bar::RollingFrameRate rollingFrameRate;
	wstring fps = L"-- FPS";
};

// 渲染线程的阶段协调器仅在当前 module 内可见，不扩大 BarUISetClass 的公开接口。
class BarRenderLoopCoordinator
{
public:
	explicit BarRenderLoopCoordinator(BarUISetClass& owner) : owner_(owner) {}

	void Run();

private:
	BarRenderLoopStageResult WakeAndSnapshot(
		BarRenderLoopState& state, BarRenderFrameSnapshot& frame);
	void SubmitTargetsAndLayout(
		BarRenderLoopState& state, const BarRenderFrameSnapshot& frame);
	bool AdvanceAnimationsAndDeriveLayout(
		BarRenderLoopState& state, const BarRenderFrameSnapshot& frame);
	void PrepareLightingAndDemand(
		BarRenderLoopState& state, const BarRenderFrameSnapshot& frame,
		bool needRendering);
	BarRenderLoopStageResult CalculateDirtyAndDrawPresent(
		BarRenderLoopState& state, const BarRenderFrameSnapshot& frame,
		UPDATELAYEREDWINDOWINFO& ulwi);
	void PaceFrame(BarRenderLoopState& state, int frameOrdinal);

	void CloseAnnotationTooltip() { owner_.CloseAnnotationTooltip(); }
	void CloseThicknessOverflowTooltip() { owner_.CloseThicknessOverflowTooltip(); }
	void ClosePenTypeMenu() { owner_.ClosePenTypeMenu(); }
	void CloseThicknessSlider(bool cancelCapture)
	{
		owner_.CloseThicknessSlider(cancelCapture);
	}
	void CloseColorPicker(bool cancelCapture)
	{
		owner_.CloseColorPicker(cancelCapture);
	}
	void RefreshBorderCursorVisibleRegions(double frameZoom)
	{
		owner_.RefreshBorderCursorVisibleRegions(frameZoom);
	}
	double ResolveThicknessSliderCenterY(
		const BarRenderLoopState& state,
		const BarThicknessPreviewGeometry& geometry) const
	{
		double centerY = geometry.sliderCenterY;
		if (!state.drawAttributeThicknessSliderPositionLocked) return centerY;

		// Slider 会话统一使用 Position B，并始终避开当前侧的 Overflow Hint。
		double halfThumb = BarThicknessSliderThumbDiameter
			* geometry.panelScale / 2.0;
		double positionB = centerY
			- geometry.previewSide
				* (BarThicknessTooltipBadgeHeight + BarDrawAttributeGap)
				* geometry.panelScale;
		return clamp(positionB,
			geometry.previewTop + halfThumb,
			geometry.previewBottom - halfThumb);
	}

	BarUISetClass& owner_;
};

// 渲染
void BarUISetClass::Rendering()
{
	BarRenderLoopCoordinator(*this).Run();
}

BarRenderLoopStageResult BarRenderLoopCoordinator::WakeAndSnapshot(
	BarRenderLoopState& state, BarRenderFrameSnapshot& frame)
{
	if (++state.presentAttemptFrameSerial == 0)
	{
		state.presentAttemptFrameSerial = 1;
		state.presentDecision.ResetFailureRecovery();
	}
	frame.demandGeneration = BarAtomic::wait.CurrentGeneration();
	state.presentDecision.ObserveDemandGeneration(frame.demandGeneration);
	if (state.presentDecision.HasFailureBackoff())
	{
		const auto retryDeadline = chrono::steady_clock::now()
			+ chrono::duration_cast<chrono::steady_clock::duration>(
				chrono::duration<double>(
					state.presentDecision.RetryDelayFrames() / 60.0));
		// 失败退避只阻塞到新请求或截止时间，不再按 60 Hz 轮询整条渲染循环。
		const auto wakeGeneration = BarAtomic::wait.WaitUntilGenerationChange(
			frame.demandGeneration, retryDeadline);
		if (offSignal) return BarRenderLoopStageResult::Stop;
		if (wakeGeneration != frame.demandGeneration)
		{
			frame.demandGeneration = wakeGeneration;
			state.presentDecision.ObserveDemandGeneration(frame.demandGeneration);
		}
		else state.presentAttemptFrameSerial = state.presentDecision.NextRetryFrame();
	}

	frame.zoom = static_cast<double>(state.barStyle.zoom);
	if (!isfinite(frame.zoom) || frame.zoom <= 0.0) frame.zoom = 1.0;
	state.spec.SetFrameZoom(frame.zoom);
	// 绘制状态和第一光源共用同一帧快照，避免模式和颜色跨阶段混读。
	frame.stateMode = stateMode.StateModeSelect;
	frame.penMode = stateMode.Pen.ModeSelect;
	frame.brush1Color = stateMode.Pen.Brush1.color;
	frame.highlighterColor = stateMode.Pen.Highlighter1.color;
	frame.penetrate = static_cast<bool>(penetrate.select);

	frame.animationDtSeconds = state.animationClock.Tick();
	frame.animationSpeedRate = static_cast<double>(BarUiAnimationSpeedRate);
	return BarRenderLoopStageResult::Proceed;
}

void BarRenderLoopCoordinator::SubmitTargetsAndLayout(
	BarRenderLoopState& state, const BarRenderFrameSnapshot& frame)
{
	const auto& frameDrawingState = frame;
	const double frameZoom = frame.zoom;
	const double animationDtSeconds = frame.animationDtSeconds;
	const double currentAnimationSpeedRate = frame.animationSpeedRate;
	const int forNum = frame.ordinal;
	// 主按钮
	{
		double operationDur = BarUiDefaultOperationDur;
		auto mainButton = state.superellipseMap[BarUISetSuperellipseEnum::MainButton];
		auto mainButtonInk = state.svgMap[BarUISetSvgEnum::logoInk];
		unsigned long long mainButtonPulseSerial = state.mainButtonClickPulseSerial.load(std::memory_order_relaxed);
		bool mainButtonPulse = mainButtonPulseSerial != state.handledMainButtonPulseSerial;
		if (mainButtonPulse) state.handledMainButtonPulseSerial = mainButtonPulseSerial;

		const BarUiCurveSpecClass mainButtonPulseCurve{
			BarUiCurveEnum::EaseOutBack, BarUiCurveEnum::EaseInBack, 0.0, false };
		if (mainButtonPulse)
		{
			// 有效点击只在松手后触发一次放大关键帧，主图标与超椭圆同步回到原尺寸。
			mainButton->w.SetTar(state.mainButtonBaseSize, operationDur,
				state.mainButtonBaseSize * state.mainButtonScale, true, mainButtonPulseCurve);
			mainButton->h.SetTar(state.mainButtonBaseSize, operationDur,
				state.mainButtonBaseSize * state.mainButtonScale, true, mainButtonPulseCurve);
			state.mainButtonLogo->w.SetTar(state.mainButtonLogoBaseW, operationDur,
				state.mainButtonLogoBaseW * state.mainButtonScale, true, mainButtonPulseCurve);
			state.mainButtonLogo->h.SetTar(state.mainButtonLogoBaseH, operationDur,
				state.mainButtonLogoBaseH * state.mainButtonScale, true, mainButtonPulseCurve);
			mainButtonInk->w.SetTar(state.mainButtonLogoBaseW, operationDur,
				state.mainButtonLogoBaseW * state.mainButtonScale, true, mainButtonPulseCurve);
			mainButtonInk->h.SetTar(state.mainButtonLogoBaseH, operationDur,
				state.mainButtonLogoBaseH * state.mainButtonScale, true, mainButtonPulseCurve);
		}
		else
		{
			mainButton->w.SetTar(state.mainButtonBaseSize, operationDur);
			mainButton->h.SetTar(state.mainButtonBaseSize, operationDur);
			state.mainButtonLogo->w.SetTar(state.mainButtonLogoBaseW, operationDur);
			state.mainButtonLogo->h.SetTar(state.mainButtonLogoBaseH, operationDur);
			mainButtonInk->w.SetTar(state.mainButtonLogoBaseW, operationDur);
			mainButtonInk->h.SetTar(state.mainButtonLogoBaseH, operationDur);
		}

		BarUiCurveEnum mainButtonPctCurve = state.barState.fold
			? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine;
		BarUiCurveSpecClass mainButtonPctCurveSpec{
			mainButtonPctCurve, mainButtonPctCurve, 0.0, false };
		if (state.barState.fold)
		{
			mainButton->n.value().SetTar(3.0, operationDur);

			mainButton->pct.SetTar(
				0.6, operationDur, nullopt, false, mainButtonPctCurveSpec);
		}
		else
		{
			mainButton->n.value().SetTar(10.0, operationDur);

			mainButton->pct.SetTar(
				0.8, operationDur, nullopt, false, mainButtonPctCurveSpec);
		}
		state.superellipseMap[BarUISetSuperellipseEnum::MainButton]->fill.value().SetTar(
			GetThemeColor(BarThemeColorEnum::Surface), operationDur);
		state.superellipseMap[BarUISetSuperellipseEnum::MainButton]->frame.value().SetTar(
			GetThemeColor(BarThemeColorEnum::SurfaceFrame), operationDur);

		// 主按钮底图随深浅色切换，着色层跟随当前画笔颜色。
		{
			static optional<bool> lastMainLogoDarkStyle;
			bool currentMainLogoDarkStyle = state.barStyle.darkStyle;
			if (!lastMainLogoDarkStyle.has_value() || lastMainLogoDarkStyle.value() != currentMainLogoDarkStyle)
			{
				state.svgMap[BarUISetSvgEnum::logo1]->SetTarFromResource(L"UI", currentMainLogoDarkStyle ? L"logo1" : L"logo2");
				lastMainLogoDarkStyle = currentMainLogoDarkStyle;
			}
			// 着色层和底图同尺寸，贴合修正交给 SVG 路径本身处理。
			bool showLogoInk = frameDrawingState.stateMode
				== StateModeSelectEnum::IdtPen
				|| frameDrawingState.stateMode == StateModeSelectEnum::IdtShape;
			COLORREF logoInkColor = frameDrawingState.stateMode
				== StateModeSelectEnum::IdtShape
				? frameDrawingState.brush1Color
				: (frameDrawingState.penMode
					== PenModeSelectEnum::IdtPenHighlighter1
					? frameDrawingState.highlighterColor
					: frameDrawingState.brush1Color);
			int logoInkColorSource = frameDrawingState.stateMode
				== StateModeSelectEnum::IdtShape
				? static_cast<int>(PenModeSelectEnum::IdtPenBrush1)
				: static_cast<int>(frameDrawingState.penMode);
			bool logoInkUsesHighlighter = logoInkColorSource
				== static_cast<int>(PenModeSelectEnum::IdtPenHighlighter1);
			bool geometryTakingOverHighlighter = frameDrawingState.stateMode
				== StateModeSelectEnum::IdtShape
				&& state.mainLogoInkCarriesHighlighterHistory;
			if (state.mainLogoInkColorSource < 0 || geometryTakingOverHighlighter)
				mainButtonInk->color1.value().SetDirect(logoInkColor);
			else mainButtonInk->color1.value().SetTar(
				logoInkColor, operationDur);
			state.mainLogoInkColorSource = logoInkColorSource;
			if (geometryTakingOverHighlighter)
				state.mainLogoInkCarriesHighlighterHistory = false;
			else if (logoInkUsesHighlighter)
				state.mainLogoInkCarriesHighlighterHistory = true;
			else if (state.mainLogoInkCarriesHighlighterHistory
				&& mainButtonInk->color1.value().IsSame())
				state.mainLogoInkCarriesHighlighterHistory = false;
			// 显隐继续共用 UI3 动画时钟；颜色源切换不允许污染 Geometry。
			mainButtonInk->pct.SetTar(showLogoInk ? 1.0 : 0.0, operationDur);
		}
	}
	// 主栏
	{
		double operationDur = BarUiDefaultOperationDur;
		auto mainBar = state.shapeMap[BarUISetShapeEnum::MainBar];
		BarUiCurveSpecClass syncedValueCurve;
		bool syncValueCurveFromBatch = false;
		BarUiCurveSpecClass syncedPctCurve{
			BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine, 0.0, false };
		const BarUiCurveSpecClass keyframeValueCurve{
			BarUiCurveEnum::EaseInCubic, BarUiCurveEnum::EaseOutBack, 0.0, false };
		const BarUiCurveSpecClass keyframePctCurve{
			BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine, 0.0, false };
		auto SyncValueDuration = [&](BarUiValueClass& value)
			{
				// 只在新目标刚建立时提交批次剩余时长，不能每帧改写正在推进的动画段。
				if (!value.IsSame() && value.progress == 0.0)
				{
					value.dur = operationDur;
					// 关键帧已经携带前后两段曲线；普通值仅在批次中覆盖自身默认曲线。
					if (!value.hasMiddleV)
					{
						BarUiCurveSpecClass curveSpec = syncValueCurveFromBatch
							? syncedValueCurve
							: BarUiCurveSpecClass{ value.curve, value.curve, 0.0, false };
						value.activeCurve = curveSpec.first;
						value.activeMiddleCurve = curveSpec.second;
						value.timelineStartProgress = curveSpec.timelineStartProgress;
						value.continueTimelinePhase = curveSpec.continueTimelinePhase;
					}
				}
			};
		auto SyncPctDuration = [&](BarUiPctClass& pct)
			{
				// 独立悬停已经提交自己的显现/淡出时长，不能再被属性栏批次时长覆盖。
				if (!pct.animateWhenDisabled && !pct.IsSame() && pct.progress == 0.0)
				{
					pct.dur = operationDur;
					pct.activeCurve = syncedPctCurve.first;
					pct.activeMiddleCurve = syncedPctCurve.second;
					pct.timelineStartProgress = syncedPctCurve.timelineStartProgress;
					pct.continueTimelinePhase = syncedPctCurve.continueTimelinePhase;
				}
			};
		bool currentMainBarSide = state.barState.widgetPosition.mainBar;
		bool mainBarSideSwitch = !state.barState.fold && currentMainBarSide != state.mainBarLayoutSide;
		// 浮层展开状态直接映射到硬编码入口的选中态，复用普通按钮颜色。
		if (auto moreButton = state.barButtonSet.GetMoreButton())
			moreButton->localState.state = (!state.barState.fold && state.barState.moreExpanded)
				? BarWidgetState::Selected : BarWidgetState::None;
		// 换边动画被打断时，新一侧仍会在下一帧与这里记录的旧侧产生一次明确变化。
		state.mainBarLayoutSide = currentMainBarSide;
		bool currentDrawAttributeSide = state.barState.widgetPosition.primaryBar;
		bool drawAttributeSideSwitch = state.barState.drawAttribute
			&& currentDrawAttributeSide != state.drawAttributeLayoutSide;
		state.drawAttributeLayoutSide = currentDrawAttributeSide;
		if (drawAttributeSideSwitch)
		{
			// 换边期间沿用已锁存方向退场，归零后再接受新方向。
			ClosePenTypeMenu();
		}
		if (drawAttributeSideSwitch
			&& state.barState.drawAttributeBar.colorPickerOpen
			&& state.barState.drawAttributeBar.colorPickerMarkerVisible)
		{
			// 色板纵向语义随展开方向翻转，选点同步镜像以保持当前颜色不变。
			state.barState.drawAttributeBar.colorPickerMarkerY = 1.0f
				- static_cast<float>(state.barState.drawAttributeBar.colorPickerMarkerY);
		}
		bool currentDrawAttributeOpen = state.barState.drawAttribute;
		bool drawAttributeVisibilityChange = currentDrawAttributeOpen != state.drawAttributeLayoutOpen;
		state.drawAttributeLayoutOpen = currentDrawAttributeOpen;
		bool currentGeometryAttributeSide = state.barState.widgetPosition.primaryBar;
		bool geometryAttributeSideSwitch = state.barState.geometryAttribute
			&& currentGeometryAttributeSide != state.geometryAttributeLayoutSide;
		state.geometryAttributeLayoutSide = currentGeometryAttributeSide;
		bool currentGeometryAttributeOpen = state.barState.geometryAttribute;
		bool geometryAttributeVisibilityChange =
			currentGeometryAttributeOpen != state.geometryAttributeLayoutOpen;
		state.geometryAttributeLayoutOpen = currentGeometryAttributeOpen;
		auto thicknessSliderRange = GetBarThicknessSliderRange(
			frameDrawingState.penMode, state.barStyle.dpiZoom);
		bool thicknessSliderAvailable =
			stateMode.StateModeSelect == StateModeSelectEnum::IdtPen
				&& thicknessSliderRange.supported
				&& state.barState.drawAttribute && !state.barState.fold;
		bool colorPickerAvailable =
			stateMode.StateModeSelect == StateModeSelectEnum::IdtPen
				&& state.barState.drawAttribute && !state.barState.fold;
		if (!colorPickerAvailable
			&& (state.barState.drawAttributeBar.colorPickerOpen
				|| state.barState.drawAttributeBar.colorPickerPointerPressed
				|| state.barState.drawAttributeBar.colorPickerPointerCapture))
		{
			// 属性栏折叠或工具失效时立即撤销命中，承载面板仍按进度完成退场。
			CloseColorPicker(true);
		}
		// 展开/收起都保留 Popup 的 Back 回弹，几何值可短暂越过终点。
		const BarUiCurveSpecClass colorPickerPanelCurve{
			state.barState.drawAttributeBar.colorPickerOpen
				? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack,
			state.barState.drawAttributeBar.colorPickerOpen
				? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack,
			0.0, false };
		state.drawAttributeColorPickerProgress.SetTar(
			colorPickerAvailable
				&& state.barState.drawAttributeBar.colorPickerOpen ? 1.0 : 0.0,
			BarColorPickerPanelAnimationDur, nullopt, false,
			colorPickerPanelCurve);
		state.drawAttributeColorPickerToneMix.SetTar(
			state.barState.drawAttributeBar.colorPickerDarkTone ? 1.0 : 0.0,
			BarColorPickerPanelAnimationDur);
		bool colorPickerHoldHintTarget =
			state.barState.drawAttributeBar.colorPickerHoldHintActive
			|| state.barState.drawAttributeBar.colorPickerHoldLocked;
		bool colorPickerHoldRingTarget =
			state.barState.drawAttributeBar.colorPickerHoldHintActive
			&& !state.barState.drawAttributeBar.colorPickerHoldLocked;
		state.drawAttributeColorPickerHoldOpacity.SetTar(
			colorPickerHoldHintTarget ? 1.0 : 0.0,
			BarColorPickerHoldHintAnimationDur);
		state.drawAttributeColorPickerHoldRingOpacity.SetTar(
			colorPickerHoldRingTarget ? 1.0 : 0.0,
			BarColorPickerHoldHintAnimationDur);
		state.drawAttributeColorPickerHoldTextMix.SetTar(
			state.barState.drawAttributeBar.colorPickerHoldLocked ? 1.0 : 0.0,
			BarColorPickerHoldHintAnimationDur);
		{
			// Draw2 绘制源色时忽略通道 alpha，最终透明度只由 stroke 层的 130/255 决定。
			COLORREF penColor = GetPenColor();
			double displayR = GetRValue(penColor);
			double displayG = GetGValue(penColor);
			double displayB = GetBValue(penColor);
			double strokeAlpha =
				stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
					? (130.0 / 255.0) : 1.0;
			double displayOpacity = clamp(
				strokeAlpha * 100.0, 0.0, 100.0);
			bool pickerDragging =
				state.barState.drawAttributeBar.colorPickerPointerPressed;
			if (!state.drawAttributeColorPickerDisplayInitialized)
			{
				state.drawAttributeColorPickerDisplayR.SetDirect(displayR);
				state.drawAttributeColorPickerDisplayG.SetDirect(displayG);
				state.drawAttributeColorPickerDisplayB.SetDirect(displayB);
				state.drawAttributeColorPickerDisplayOpacity.SetDirect(displayOpacity);
				state.drawAttributeColorPickerDisplayInitialized = true;
			}
			else if (pickerDragging)
			{
				state.drawAttributeColorPickerDisplayR.SetDirect(displayR);
				state.drawAttributeColorPickerDisplayG.SetDirect(displayG);
				state.drawAttributeColorPickerDisplayB.SetDirect(displayB);
				state.drawAttributeColorPickerDisplayOpacity.SetDirect(displayOpacity);
			}
			else
			{
				state.drawAttributeColorPickerDisplayR.SetTar(displayR, operationDur);
				state.drawAttributeColorPickerDisplayG.SetTar(displayG, operationDur);
				state.drawAttributeColorPickerDisplayB.SetTar(displayB, operationDur);
				state.drawAttributeColorPickerDisplayOpacity.SetTar(
					displayOpacity, operationDur);
			}
		}
		if (!thicknessSliderAvailable
			&& (state.barState.drawAttributeBar.thicknessSliderHover
				|| state.barState.drawAttributeBar.thicknessSliderPinned
				|| state.barState.drawAttributeBar.thicknessSliderPressed
				|| state.barState.drawAttributeBar.thicknessSliderDragging
				|| state.barState.drawAttributeBar.thicknessPreviewDragging
				|| state.barState.drawAttributeBar.thicknessFineDialDragging
				|| state.barState.drawAttributeBar.thicknessFineDialPhysicsActive
				|| state.barState.drawAttributeBar
					.thicknessFineDialActivationPreviewActive
				|| state.barState.drawAttributeBar
					.thicknessFineDialActivationDwellActive
				|| static_cast<float>(state.barState.drawAttributeBar
					.thicknessFineDialActivationPreviewProgress) > 0.0f
				|| state.barState.drawAttributeBar.thicknessViewMode
					!= ThicknessViewMode::Preview
				|| state.barState.drawAttributeBar.thicknessSliderCapture))
		{
			// 属性栏失效时主动结束捕获，嵌套输入循环会收到一次合成抬起。
			CloseThicknessSlider(true);
		}
		ThicknessViewMode thicknessViewMode = thicknessSliderAvailable
			? static_cast<ThicknessViewMode>(
				state.barState.drawAttributeBar.thicknessViewMode)
			: ThicknessViewMode::Preview;
		bool thicknessSliderActive = thicknessSliderAvailable
			&& thicknessViewMode == ThicknessViewMode::Slider;
		bool thicknessFineDialActive = thicknessSliderAvailable
			&& thicknessViewMode == ThicknessViewMode::FineDial;
		auto CancelFineDialRangeTransition = [&]()
			{
				state.thicknessFineDialRangeTransitionPhase =
					ThicknessFineDialRangeTransitionPhase::Idle;
				state.thicknessFineDialOldRangeOpacity.SetDirect(0.0);
				state.thicknessFineDialNewRangeOpacity.SetDirect(1.0);
				state.barState.drawAttributeBar
					.thicknessFineDialRangeTransitionActive = false;
			};
		bool fineDialPenModeChanged = frameDrawingState.penMode
			!= state.thicknessFineDialLastPenMode;
		if (!thicknessFineDialActive
			|| state.barState.drawAttributeBar.thicknessFineDialCandidateActive
			|| !thicknessSliderRange.supported)
		{
			CancelFineDialRangeTransition();
		}
		else if (fineDialPenModeChanged)
		{
			BarThicknessSliderRange previousRenderRange =
				state.thicknessFineDialLastLogicalRange;
			if (state.thicknessFineDialRangeTransitionPhase
				!= ThicknessFineDialRangeTransitionPhase::Idle)
			{
				previousRenderRange.min = min(
					state.thicknessFineDialOldRenderRange.min,
					state.thicknessFineDialNewRenderRange.min);
				previousRenderRange.max = max(
					state.thicknessFineDialOldRenderRange.max,
					state.thicknessFineDialNewRenderRange.max);
				previousRenderRange.supported = true;
			}
			double transitionVisual = static_cast<double>(
				state.drawAttributePenThickness.val);
			bool outsideNewRange = transitionVisual
				< thicknessSliderRange.min - 0.000001
				|| transitionVisual
					> thicknessSliderRange.max + 0.000001;
			if (previousRenderRange.supported && outsideNewRange)
			{
				// 先保留旧视觉并补齐新区间，随后才启动已有粗细动画。
				state.thicknessFineDialOldRenderRange = previousRenderRange;
				state.thicknessFineDialNewRenderRange = thicknessSliderRange;
				state.thicknessFineDialRangeTransitionPhase =
					ThicknessFineDialRangeTransitionPhase::RevealNewRange;
				state.thicknessFineDialOldRangeOpacity.SetDirect(1.0);
				state.thicknessFineDialNewRangeOpacity.SetDirect(0.0);
				state.drawAttributePenThickness.SetDirect(transitionVisual);
				state.barState.drawAttributeBar
					.thicknessFineDialRangeTransitionActive = true;
			}
			else CancelFineDialRangeTransition();
		}
		state.thicknessFineDialLastPenMode = frameDrawingState.penMode;
		state.thicknessFineDialLastLogicalRange = thicknessSliderRange;
		if (state.thicknessFineDialRangeTransitionPhase
			== ThicknessFineDialRangeTransitionPhase::RevealNewRange)
		{
			state.thicknessFineDialOldRangeOpacity.SetTar(1.0);
			state.thicknessFineDialNewRangeOpacity.SetTar(
				1.0, BarThicknessFineDialTransitionDur);
			if (state.thicknessFineDialNewRangeOpacity.IsSame())
				state.thicknessFineDialRangeTransitionPhase =
					ThicknessFineDialRangeTransitionPhase::MoveValue;
		}
		else if (state.thicknessFineDialRangeTransitionPhase
			== ThicknessFineDialRangeTransitionPhase::RetireOldRange)
		{
			state.thicknessFineDialOldRangeOpacity.SetTar(
				0.0, BarThicknessFineDialTransitionDur);
			if (state.thicknessFineDialOldRangeOpacity.IsSame())
				CancelFineDialRangeTransition();
		}
		bool thicknessExpandedActive = thicknessSliderActive
			|| thicknessFineDialActive;
		if (thicknessExpandedActive
			!= state.drawAttributeThicknessSliderTargetActive)
		{
			if (thicknessExpandedActive
				&& !state.drawAttributeThicknessSliderPositionLocked)
			{
				// Preview -> Slider 只在入口锁存一次，恢复 Preview 前保持同一会话。
				bool hintDisplayed =
					state.barState.drawAttributeBar.thicknessOverflowHintPresent
					&& state.drawAttributeOverflowBadgeProgress.val > 0.000001;
				state.drawAttributeThicknessSliderPositionLocked = true;
				state.drawAttributeOverflowSliderSessionAllowsHint = hintDisplayed;
			}
			state.drawAttributeThicknessSliderTargetActive =
				thicknessExpandedActive;
		}
		auto SetPopupProgress = [&](BarUiValueClass& progress,
			bool visible, bool closeRebound = false)
		{
			BarUiCurveEnum curve = visible
				? BarUiCurveEnum::EaseOutBack
				: (closeRebound ? BarUiCurveEnum::EaseInBack
					: BarUiCurveEnum::EaseInCubic);
			BarUiCurveSpecClass curveSpec{
				curve, curve, 0.0, false };
			progress.SetTar(visible ? 1.0 : 0.0,
				operationDur, nullopt, false, curveSpec);
		};
		// 笔型扩展菜单与面板共享退场动画；方向在打开时锁存。
		bool penTypeMenuEligible = state.barState.drawAttribute && !state.barState.fold
			&& PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
		bool penTypeMenuAnchorMatches =
			state.barState.drawAttributeBar.penTypeMenuAnchorMode
			== static_cast<int>(stateMode.Pen.ModeSelect);
		bool penTypeMenuDirectionMatches =
			!state.barState.drawAttributeBar.penTypeMenuDirectionLocked
			|| static_cast<bool>(
				state.barState.drawAttributeBar.penTypeMenuOpenBelow)
				== static_cast<bool>(state.barState.widgetPosition.primaryBar);
		if (!penTypeMenuEligible)
		{
			// 资格失效时当帧撤销入口按压、悬停和菜单命中。
			ClosePenTypeMenu();
		}
		else if ((!penTypeMenuAnchorMatches
			|| !penTypeMenuDirectionMatches)
			&& state.barState.drawAttributeBar.penTypeMenuOpen)
			ClosePenTypeMenu();
		if (!static_cast<bool>(state.barState.drawAttributeBar.penTypeMenuOpen)
			&& static_cast<bool>(state.barState.drawAttributeBar.penTypeMenuDirectionLocked)
			&& state.drawAttributePenTypeMenuProgress.val <= 0.000001
			&& state.drawAttributePenTypeMenuProgress.tar <= 0.000001)
			state.barState.drawAttributeBar.penTypeMenuDirectionLocked = false;
		// 菜单展开复用 Overflow Popup 的回弹，收起使用对应的反向回弹。
		SetPopupProgress(state.drawAttributePenTypeMenuProgress,
			state.barState.drawAttributeBar.penTypeMenuOpen, true);
		const BarUiCurveSpecClass thicknessSliderProgressCurve{
			BarUiCurveEnum::EaseInOutCubic,
				BarUiCurveEnum::EaseInOutCubic, 0.0, false };
		bool holdSliderMorphForThumbExit = !thicknessExpandedActive
			&& state.drawAttributeThicknessSliderProgress.val > 0.000001
			&& state.drawAttributeThicknessSliderThumbOpacity.val
				> BarThicknessSliderThumbMorphExitOpacity;
		state.drawAttributeThicknessSliderProgress.SetTar(
			(thicknessExpandedActive || holdSliderMorphForThumbExit)
				? 1.0 : 0.0,
			operationDur, nullopt, false,
			thicknessSliderProgressCurve);
		state.drawAttributeThicknessSliderTrackOpacity.SetTar(
			(thicknessSliderActive || holdSliderMorphForThumbExit)
				? 1.0 : 0.0,
			BarThicknessFineDialTransitionDur, nullopt, false,
			thicknessSliderProgressCurve);
		state.drawAttributeThicknessFineDialProgress.SetTar(
			thicknessFineDialActive ? 1.0 : 0.0,
			BarThicknessFineDialTransitionDur, nullopt, false,
			thicknessSliderProgressCurve);
		state.drawAttributeThicknessFineDialSelectionProgress.SetTar(
			thicknessFineDialActive ? 1.0 : 0.0,
			BarThicknessFineDialSelectionTransitionDur, nullopt, false,
			thicknessSliderProgressCurve);
		bool recognitionPreviewSharedActive = state.barState.drawAttributeBar
			.thicknessFineDialActivationPreviewActive;
		bool dwellPreviewSharedActive = state.barState.drawAttributeBar
			.thicknessFineDialActivationDwellActive;
		double activationDwellProgress = clamp(static_cast<double>(
			state.barState.drawAttributeBar
				.thicknessFineDialActivationPreviewProgress),
			0.0, 1.0);
		bool recognitionPreviewActive = thicknessSliderActive
			&& recognitionPreviewSharedActive;
		bool activationPreviewHandoffPending =
			(thicknessSliderActive || thicknessFineDialActive)
			&& !recognitionPreviewSharedActive
			&& !dwellPreviewSharedActive
			&& activationDwellProgress > 0.000001;
		if (recognitionPreviewActive)
		{
			state.drawAttributeThicknessFineDialActivationGeometryTransition = true;
			state.drawAttributeThicknessFineDialRecognitionVisibility.SetTar(
				1.0, BarThicknessFineDialActivationPreviewEnterDur,
				nullopt, false, thicknessSliderProgressCurve);
			if (dwellPreviewSharedActive)
			{
				// 计时进度直接跟随 1 秒时钟；离区后再由本地动画平滑回暗态。
				state.drawAttributeThicknessFineDialDwellProgress.SetDirect(
					activationDwellProgress);
			}
			else
				state.drawAttributeThicknessFineDialDwellProgress.SetTar(
					0.0, BarThicknessFineDialActivationPreviewFadeOutDur,
					nullopt, false, thicknessSliderProgressCurve);
		}
		else if (activationPreviewHandoffPending)
		{
			// 正式激活接住完整预览，直到 FineDial 主进度追上，避免透明度回落。
			state.drawAttributeThicknessFineDialActivationGeometryTransition = true;
			state.drawAttributeThicknessFineDialRecognitionVisibility.SetDirect(1.0);
			state.drawAttributeThicknessFineDialDwellProgress.SetDirect(max(
				static_cast<double>(
					state.drawAttributeThicknessFineDialDwellProgress.val),
				activationDwellProgress));
			if (thicknessFineDialActive)
			{
				state.barState.drawAttributeBar
					.thicknessFineDialActivationPreviewProgress = 0.0f;
				state.barState.drawAttributeBar
					.thicknessFineDialActivationDwellActive = false;
			}
		}
		else
		{
			double retainedPreviewOpacity =
				BarThicknessFineDialActivationPreviewBaseOpacity
					* static_cast<double>(
						state.drawAttributeThicknessFineDialRecognitionVisibility.val)
				+ (1.0 - BarThicknessFineDialActivationPreviewBaseOpacity)
					* static_cast<double>(
						state.drawAttributeThicknessFineDialDwellProgress.val);
			if (!thicknessFineDialActive
				|| state.drawAttributeThicknessFineDialProgress.val + 0.000001
					>= retainedPreviewOpacity)
			{
				state.drawAttributeThicknessFineDialRecognitionVisibility.SetTar(
					0.0, BarThicknessFineDialActivationPreviewFadeOutDur,
					nullopt, false, thicknessSliderProgressCurve);
				state.drawAttributeThicknessFineDialDwellProgress.SetTar(
					0.0, BarThicknessFineDialActivationPreviewFadeOutDur,
					nullopt, false, thicknessSliderProgressCurve);
			}
		}
		if ((!thicknessFineDialActive
				|| state.drawAttributeThicknessFineDialProgress.val >= 0.999999)
			&& !recognitionPreviewActive
			&& !activationPreviewHandoffPending
			&& state.drawAttributeThicknessFineDialRecognitionVisibility.val
				<= 0.000001
			&& state.drawAttributeThicknessFineDialDwellProgress.val <= 0.000001)
		{
			state.drawAttributeThicknessFineDialActivationGeometryTransition = false;
		}
// 圆点只在轨道完全拉直后出现；退出时先完全消失，再恢复预览。
			// 锁定只冻结本轮粗细，圆点和浮窗保持到真实抬手。
			bool thicknessSliderHoldLocked =
				state.barState.drawAttributeBar.thicknessSliderHoldLocked;
			bool thicknessSliderThumbVisible = thicknessSliderActive
				&& state.drawAttributeThicknessSliderProgress.val >= 0.999999;
			bool thicknessHoldHintTarget =
				state.barState.drawAttributeBar.thicknessSliderHoldHintActive
				|| thicknessSliderHoldLocked;
			// 额外透明度只负责锁定后的圆环退场；普通显隐统一交给 Hold 组进度。
			if (thicknessSliderHoldLocked)
			{
				state.drawAttributeThicknessHoldRingLockOpacity.SetTar(
					0.0, BarThicknessHoldHintAnimDur);
			}
			else if (thicknessHoldHintTarget)
			{
				state.drawAttributeThicknessHoldRingLockOpacity.SetTar(
					1.0, BarThicknessHoldHintAnimDur);
			}
			else if (state.drawAttributeThicknessHoldExchangeProgress.val <= 0.000001
				&& state.drawAttributeThicknessHoldExchangeProgress.tar <= 0.000001)
			{
				// 锁定会话完全退场后再复位，下一次圆环可与文字同时弹入。
				state.drawAttributeThicknessHoldRingLockOpacity.SetDirect(1.0);
			}
			state.drawAttributeThicknessHoldTextMix.SetTar(
				thicknessSliderHoldLocked ? 1.0 : 0.0,
				BarThicknessHoldHintAnimDur);
			// 内容立即切换；组透明度和组缩放均从当前视觉状态平滑重定向。
			const BarUiCurveSpecClass holdExchangeCurve{
				thicknessHoldHintTarget
					? BarUiCurveEnum::EaseOutCubic
					: BarUiCurveEnum::EaseInCubic,
				thicknessHoldHintTarget
					? BarUiCurveEnum::EaseOutCubic
					: BarUiCurveEnum::EaseInCubic,
				0.0, false };
			state.drawAttributeThicknessHoldExchangeProgress.SetTar(
				thicknessHoldHintTarget ? 1.0 : 0.0,
				BarThicknessHoldExchangeAnimDur,
				nullopt, false, holdExchangeCurve);
			const BarUiCurveSpecClass holdGroupScaleCurve{
				thicknessHoldHintTarget
					? BarUiCurveEnum::EaseOutBack
					: BarUiCurveEnum::EaseInBack,
				thicknessHoldHintTarget
					? BarUiCurveEnum::EaseOutBack
					: BarUiCurveEnum::EaseInBack,
				0.0, false };
			state.drawAttributeThicknessHoldGroupScale.SetTar(
				thicknessHoldHintTarget ? 1.0 : 0.82,
				thicknessHoldHintTarget
					? static_cast<double>(BarUiDefaultOperationDur)
					: BarThicknessHoldExchangeAnimDur,
				nullopt, false, holdGroupScaleCurve);
			bool thicknessPreviewPopupVisible =
				thicknessSliderThumbVisible || thicknessFineDialActive;
			bool popupHideStarted =
				state.drawAttributeThicknessPreviewPopupTargetVisible
				&& !thicknessPreviewPopupVisible;
			if (popupHideStarted)
			{
				bool normalFineDialPreviewExit =
					state.drawAttributeThicknessPreviewPopupTargetFineDial
					&& state.barState.drawAttributeBar
						.thicknessFineDialPopupExitLatchRequested
					&& thicknessSliderAvailable
					&& state.barState.drawAttribute && !state.barState.fold
					&& thicknessViewMode == ThicknessViewMode::Preview
					&& state.drawAttributeThicknessPreviewPopupRenderedCenterValid;
				if (normalFineDialPreviewExit)
				{
					// 正常返回 Preview 时只缩放/淡出，面板生命周期退出仍保留原追随几何。
					state.drawAttributeThicknessPreviewPopupExitCenter =
						state.drawAttributeThicknessPreviewPopupRenderedCenter;
					state.drawAttributeThicknessPreviewPopupExitPositionLatched = true;
					state.drawAttributeThicknessPreviewPopupRetargetProgress.SetDirect(0.0);
				}
				else
				{
					state.drawAttributeThicknessPreviewPopupExitPositionLatched = false;
					state.drawAttributeThicknessPreviewPopupRetargetProgress.SetDirect(1.0);
				}
				state.barState.drawAttributeBar
					.thicknessFineDialPopupExitLatchRequested = false;
			}
			if (state.drawAttributeThicknessPreviewPopupExitPositionLatched
				&& (!thicknessSliderAvailable
					|| !state.barState.drawAttribute || state.barState.fold))
			{
				// 面板或主栏收起继续沿用原几何追随，不保留普通 Preview 退出锁存。
				state.drawAttributeThicknessPreviewPopupExitPositionLatched = false;
				state.drawAttributeThicknessPreviewPopupRetargetProgress.SetDirect(1.0);
			}
			if (state.drawAttributeThicknessPreviewPopupExitPositionLatched)
			{
				if (thicknessPreviewPopupVisible)
					state.drawAttributeThicknessPreviewPopupRetargetProgress.SetTar(
						1.0, BarThicknessPreviewPopupAnimationDur,
						nullopt, false, thicknessSliderProgressCurve);
				else
					state.drawAttributeThicknessPreviewPopupRetargetProgress.SetDirect(0.0);
			}
			const BarUiCurveSpecClass thicknessPreviewPopupCurve{
				thicknessPreviewPopupVisible
					? BarUiCurveEnum::EaseOutBack
					: BarUiCurveEnum::EaseInBack,
				thicknessPreviewPopupVisible
					? BarUiCurveEnum::EaseOutBack
					: BarUiCurveEnum::EaseInBack,
				0.0, false };
			state.drawAttributeThicknessPreviewPopupProgress.SetTar(
				thicknessPreviewPopupVisible ? 1.0 : 0.0,
				BarThicknessPreviewPopupAnimationDur,
				nullopt, false, thicknessPreviewPopupCurve);
			if (state.drawAttributeThicknessPreviewPopupExitPositionLatched
				&& ((!thicknessPreviewPopupVisible
					&& state.drawAttributeThicknessPreviewPopupProgress.val <= 0.000001
					&& state.drawAttributeThicknessPreviewPopupProgress.tar <= 0.000001)
					|| (thicknessPreviewPopupVisible
						&& state.drawAttributeThicknessPreviewPopupProgress.val >= 0.999999
						&& state.drawAttributeThicknessPreviewPopupRetargetProgress.val
							>= 0.999999)))
			{
				state.drawAttributeThicknessPreviewPopupExitPositionLatched = false;
				state.drawAttributeThicknessPreviewPopupRetargetProgress.SetDirect(1.0);
			}
			state.drawAttributeThicknessPreviewPopupTargetVisible =
				thicknessPreviewPopupVisible;
			state.drawAttributeThicknessPreviewPopupTargetFineDial =
				thicknessFineDialActive;
		const BarUiCurveSpecClass thumbOpacityCurve{
			thicknessSliderThumbVisible
				? BarUiCurveEnum::EaseOutCubic
				: BarUiCurveEnum::EaseInCubic,
			thicknessSliderThumbVisible
				? BarUiCurveEnum::EaseOutCubic
				: BarUiCurveEnum::EaseInCubic,
			0.0, false };
		const BarUiCurveSpecClass thumbScaleCurve{
			thicknessSliderThumbVisible
				? BarUiCurveEnum::EaseOutBack
				: BarUiCurveEnum::EaseInCubic,
			thicknessSliderThumbVisible
				? BarUiCurveEnum::EaseOutBack
				: BarUiCurveEnum::EaseInCubic,
			0.0, false };
		state.drawAttributeThicknessSliderThumbOpacity.SetTar(
			thicknessSliderThumbVisible ? 1.0 : 0.0,
			BarThicknessSliderThumbAnimationDur,
			nullopt, false, thumbOpacityCurve);
		state.drawAttributeThicknessSliderThumbScale.SetTar(
			thicknessSliderThumbVisible ? 1.0 : 0.75,
			BarThicknessSliderThumbAnimationDur,
			nullopt, false, thumbScaleCurve);
		bool thicknessPreviewRestored = !thicknessExpandedActive
			&& state.drawAttributeThicknessSliderProgress.val <= 0.000001
			&& state.drawAttributeThicknessSliderProgress.tar <= 0.000001
			&& state.drawAttributeThicknessFineDialProgress.val <= 0.000001
			&& state.drawAttributeThicknessFineDialProgress.tar <= 0.000001
			&& state.drawAttributeThicknessSliderThumbOpacity.val <= 0.000001
			&& state.drawAttributeThicknessSliderThumbOpacity.tar <= 0.000001;
		if (thicknessPreviewRestored
			&& state.drawAttributeThicknessSliderPositionLocked)
		{
			// Preview 完整恢复后才释放本次 Slider session 的位置和 Hint 历史。
			state.drawAttributeThicknessSliderPositionLocked = false;
			state.drawAttributeOverflowSliderSessionAllowsHint = false;
		}
		const BarUiCurveSpecClass thicknessSliderStateCurve{
			BarUiCurveEnum::EaseOutCubic,
			BarUiCurveEnum::EaseOutCubic, 0.0, false };
		double thicknessSliderAccentOpacity = 1.0;
		double thicknessSliderCenterDiameter =
			BarThicknessSliderThumbCenterDiameter;
		if (thicknessSliderActive
			&& state.barState.drawAttributeBar.thicknessSliderPressed)
		{
			thicknessSliderAccentOpacity = 0.80;
			thicknessSliderCenterDiameter =
				BarThicknessSliderThumbPressedCenterDiameter;
		}
		else if (thicknessSliderActive
			&& state.barState.drawAttributeBar.thicknessSliderHover)
		{
			thicknessSliderAccentOpacity = 0.90;
			thicknessSliderCenterDiameter =
				BarThicknessSliderThumbHoverCenterDiameter;
		}
		// 对齐 WinUI：外圈不缩放，只让强调色强度和中心圆尺寸平滑切换。
		state.drawAttributeThicknessSliderAccentOpacity.SetTar(
			thicknessSliderAccentOpacity,
			BarThicknessSliderPressAnimationDur,
			nullopt, false, thicknessSliderStateCurve);
		state.drawAttributeThicknessSliderCenterDiameter.SetTar(
			thicknessSliderCenterDiameter,
			BarThicknessSliderPressAnimationDur,
			nullopt, false, thicknessSliderStateCurve);
if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
			{
				// 真实粗细仍只在抬起提交；拖动中数字即时显示候选值，抬手后恢复普通动画。
				double penThickness = max(0.0f, GetPenWidth());
				bool thicknessCandidateDragging =
					state.barState.drawAttributeBar.thicknessSliderDragging
						|| state.barState.drawAttributeBar.thicknessPreviewDragging
						|| state.barState.drawAttributeBar
							.thicknessFineDialCandidateActive;
				if (thicknessCandidateDragging)
				{
					penThickness = max(0.0f,
						static_cast<float>(state.barState.drawAttributeBar
							.thicknessSliderCandidateWidth));
				}
				double penPreviewMorph =
					PenModeUsesCurvedThicknessPreview(stateMode.Pen.ModeSelect)
					? 0.0
					: (stateMode.Pen.ModeSelect
						== PenModeSelectEnum::IdtPenHighlighter1 ? 1.0 : 0.0);
				if (!state.drawAttributePenThicknessInitialized)
				{
					// 首次进入绘制模式时先同步真实粗细，避免稍后展开属性栏仍显示 0 → 默认值。
					state.drawAttributePenThickness.SetDirect(penThickness);
					state.drawAttributePenThicknessInitialized = true;
				}
				else if (thicknessCandidateDragging)
					// 拖动中数字跟手，不走过渡动画。
					state.drawAttributePenThickness.SetDirect(penThickness);
				else if (state.thicknessFineDialRangeTransitionPhase
					== ThicknessFineDialRangeTransitionPhase::MoveValue)
				{
					bool moveStarted = state.drawAttributePenThickness.SetTar(
						penThickness, operationDur);
					if (!moveStarted && state.drawAttributePenThickness.IsSame())
						state.thicknessFineDialRangeTransitionPhase =
							ThicknessFineDialRangeTransitionPhase::RetireOldRange;
				}
				else if (state.thicknessFineDialRangeTransitionPhase
					== ThicknessFineDialRangeTransitionPhase::Idle)
					state.drawAttributePenThickness.SetTar(penThickness, operationDur);
				if (!state.drawAttributePenPreviewMorphInitialized)
				{
					state.drawAttributePenPreviewMorph.SetDirect(penPreviewMorph);
					state.drawAttributePenPreviewMorphInitialized = true;
				}
				else state.drawAttributePenPreviewMorph.SetTar(
					penPreviewMorph, operationDur);

				// 圆点位置按当前笔形量程归一化；笔形切换时对 0–1 做动画，不直接用旧宽度/新量程瞬算。
				auto thicknessSliderRange = GetBarThicknessSliderRange(
					stateMode.Pen.ModeSelect, state.barStyle.dpiZoom);
				if (thicknessSliderRange.supported
					&& thicknessSliderRange.max > thicknessSliderRange.min)
				{
					double targetNormalized = clamp(
						(penThickness - thicknessSliderRange.min)
						/ static_cast<double>(
							thicknessSliderRange.max
								- thicknessSliderRange.min),
						0.0, 1.0);
					if (!state.drawAttributeThicknessSliderNormalizedInitialized)
					{
						state.drawAttributeThicknessSliderNormalized.SetDirect(
							targetNormalized);
						state.drawAttributeThicknessSliderNormalizedInitialized =
							true;
					}
					else if (thicknessCandidateDragging)
						state.drawAttributeThicknessSliderNormalized.SetDirect(
							targetNormalized);
					else state.drawAttributeThicknessSliderNormalized.SetTar(
						targetNormalized, operationDur);
				}
			}
			else
			{
				state.drawAttributePenThicknessInitialized = false;
				state.drawAttributePenPreviewMorphInitialized = false;
				state.drawAttributeThicknessSliderNormalizedInitialized = false;
			}
		bool mainBarFoldChange = (state.barState.fold && mainBar->x.tar != 0.0)
			|| (!state.barState.fold && mainBar->x.tar == 0.0);
// 与下方布局共用：间隙 5，1*1 边长 32.5，使
			// 2*1 = 两枚 1*1 + 间隙，2*2 = 两枚 2*1 + 间隙 = 四枚 1*1，且各处间隙一致。
			constexpr double barBtnGap = 5.0;
			constexpr double barBtnOne = 32.5; // (70 - gap) / 2，保持正方形
			constexpr double barBtnTwo = barBtnOne * 2.0 + barBtnGap; // 70
			constexpr double barBtnOneStep = barBtnOne + barBtnGap; // 37.5
			constexpr double barBtnTwoStep = barBtnTwo + barBtnGap; // 75
			auto CalculateButtonLayoutWidth = [&]()
				{
					double width = barBtnGap, xO = barBtnGap, yO = barBtnGap;
					for (int id = 0; id < state.barButtonSet.tot; id++)
					{
						BarButtonClass* temp = state.barButtonSet.buttonList.Get(id);
						if (!temp) continue;
						if (temp->size == BarButtonSizeEnum::oneOne)
						{
							if (!temp->IsVisible()) continue;
							if (yO <= barBtnGap) yO += barBtnOneStep, width += barBtnOneStep;
							else if (xO + barBtnOneStep >= width) xO += barBtnOneStep, yO = barBtnGap;
							else xO += barBtnOneStep;
						}
						else if (temp->size == BarButtonSizeEnum::twoOne)
						{
							if (yO > barBtnGap && xO + barBtnTwoStep > width) xO = width, yO = barBtnGap;
							if (!temp->IsVisible()) continue;
							if (yO <= barBtnGap) yO += barBtnOneStep, width += barBtnTwoStep;
							else xO += barBtnTwoStep, yO = barBtnGap;
						}
						else if (temp->size == BarButtonSizeEnum::twoTwo)
						{
							if (yO > barBtnGap) yO = barBtnGap, xO = width;
							if (temp->IsVisible()) xO += barBtnTwoStep, width += barBtnTwoStep;
						}
						else if (temp->size == BarButtonSizeEnum::oneTwo)
						{
							if (!temp->IsVisible()) continue;
							// Divider 只结束未填满的列，不占用已有 5 DIP 间隙之外的宽度。
							xO = width;
							yO = barBtnGap;
						}
					}
					return width;
				};
		double layoutTotalWidth = CalculateButtonLayoutWidth();
		bool mainBarLayoutChange = state.mainBarLayoutWidth.has_value()
			&& abs(layoutTotalWidth - state.mainBarLayoutWidth.value()) > 0.000001;
		bool mainBarLayoutExpands = mainBarLayoutChange
			&& layoutTotalWidth > state.mainBarLayoutWidth.value();
		// 布局变化会取代仍在运行的换边关键帧；即使某个控件目标没变，也必须从当前值重建。
		bool interruptingMainBarSideSwitch = mainBarLayoutChange && state.mainBarTimeline.IsActive()
			&& (mainBar->x.hasMiddleV || mainBar->w.hasMiddleV);
		// 新操作创建完整批次；批次进入后半程后，新布局不再压缩到旧截止时间。
		bool lateMainBarLayoutChange = !state.barState.fold && state.mainBarTimeline.IsActive()
			&& mainBarLayoutChange && !state.mainBarTimeline.CanJoin();
		// 后半程布局变化会重开完整批次；目标未变的在途布局值也要从当前值同步重启。
		bool forceRestartMainBarLayout = mainBarFoldChange || lateMainBarLayoutChange;
		// 超过加入阈值后会创建新批次，此时旧换边中点已经失效，不能在新批次中再次收窄。
		bool continueMainBarSideSwitchKeyframe = interruptingMainBarSideSwitch
			&& !lateMainBarLayoutChange;
		bool restartMainBarTimeline = mainBarFoldChange || mainBarSideSwitch
			|| lateMainBarLayoutChange
			|| (!state.barState.fold && !state.mainBarTimeline.IsActive() && mainBarLayoutChange);
		if (restartMainBarTimeline)
		{
			if (mainBarSideSwitch) state.mainBarBatchCurve = BarUiCurveEnum::EaseInOutCubic;
			else if (mainBarFoldChange)
				state.mainBarBatchCurve = state.barState.fold
				? BarUiCurveEnum::EaseInBack : BarUiCurveEnum::EaseOutBack;
			else state.mainBarBatchCurve = mainBarLayoutExpands
				// 展开保留回弹活力；收起立即响应并在末端平稳减速到零。
				? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseOutCubic;
			state.mainBarTimeline.Restart(operationDur);
		}
		else if (state.mainBarTimeline.IsActive() && mainBarLayoutChange)
		{
			// 换边中途加入布局时用完整平滑曲线覆盖剩余时间，避免压缩重播 Back 造成突发加速。
			state.mainBarBatchCurve = continueMainBarSideSwitchKeyframe
				? BarUiCurveEnum::EaseInOutCubic
				: (mainBarLayoutExpands ? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseOutCubic);
		}
		state.mainBarLayoutWidth = layoutTotalWidth;
		if (state.mainBarTimeline.IsActive()) operationDur = state.mainBarTimeline.GetRemainingDuration();
		double mainBarPhase = state.mainBarTimeline.IsActive() ? state.mainBarTimeline.GetProgress() : 0.0;
		bool continueMainBarPhase = state.mainBarTimeline.IsActive() && mainBarPhase > 0.0
			&& !continueMainBarSideSwitchKeyframe;
		syncValueCurveFromBatch = state.mainBarTimeline.IsActive();
		BarUiCurveEnum syncedMainBarCurve = state.mainBarTimeline.IsActive()
			? state.mainBarBatchCurve : BarUiCurveEnum::EaseInOutCubic;
		BarUiCurveEnum syncedMainBarPctCurve = mainBarLayoutChange
			? (mainBarLayoutExpands ? BarUiCurveEnum::EaseOutSine : BarUiCurveEnum::EaseOutCubic)
			: (state.mainBarTimeline.IsActive() && state.mainBarBatchCurve == BarUiCurveEnum::EaseInBack
				? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine);
		syncedValueCurve = { syncedMainBarCurve, syncedMainBarCurve,
			mainBarPhase, continueMainBarPhase };
		syncedPctCurve = { syncedMainBarPctCurve, syncedMainBarPctCurve,
			mainBarPhase, continueMainBarPhase };
		const BarUiCurveSpecClass continuedKeyframeValueCurve{
			BarUiCurveEnum::EaseInCubic, BarUiCurveEnum::EaseOutBack,
			mainBarPhase, true };
		const BarUiCurveSpecClass continuedKeyframePctCurve{
			BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine,
			mainBarPhase, true };
		auto SetButtonPositionTar = [&](BarUiValueClass& value, double target, double middle, bool mirrorX = false)
			{
				// 左侧展开仍按正序布局，只将最终横坐标按主栏宽度镜像。
				if (mirrorX && !state.barState.widgetPosition.mainBar) target = layoutTotalWidth - target;
				if (mainBarSideSwitch)
				{
					value.SetTar(target, operationDur, middle, true, keyframeValueCurve);
				}
				else if (continueMainBarSideSwitchKeyframe)
				{
					// 父栏和按钮必须继续共享换边中点，否则继承坐标会叠加出先后错位。
					value.SetTar(target, operationDur, middle, true, continuedKeyframeValueCurve);
				}
				else value.SetTar(target, operationDur, nullopt,
					forceRestartMainBarLayout, syncedValueCurve);
			};

		// 按钮位置计算（特别操作）
		double totalWidth = barBtnGap;
		{
			double xO = barBtnGap, yO = barBtnGap;
			// 控件计算的 xO 和 yO 包含自身和 右侧、下册 的空隙值 5px
			// 统一间隙 5：1*1=32.5（正方形），两枚 1*1+间隙=2*1(70x32.5)，
			// 两枚 2*1+间隙=2*2(70x70)，四枚 1*1 合成 2*2；两行时上/中/下间距均为 5。
			constexpr double barBtnOneHalf = barBtnOne / 2.0; // 16.25
			constexpr double barBtnTwoHalf = barBtnTwo / 2.0; // 35

			// 两侧始终按正序计算；向左展开时由横坐标镜像实现从右向左填充。
			auto baseRange = views::iota(0, state.barButtonSet.tot);
			variant<decltype(baseRange), decltype(baseRange | views::reverse)> viewVariant;
			viewVariant = baseRange;

			visit([&](auto&& forRange)
				{
					for (int id : forRange)
					{
						BarButtonClass* temp = state.barButtonSet.buttonList.Get(id);
						if (temp == nullptr) continue;
						if (temp->icon.color1.has_value())
						{
							COLORREF iconColor = temp->state->state == BarWidgetState::Selected
								? GetThemeColor(BarThemeColorEnum::Accent)
								: GetThemeColor(BarThemeColorEnum::TextPrimary);
							// 第一次计算或不可见时直接同步，避免 SVG 显示后才从黑色过渡。
							if (forNum == 1 || state.barState.fold || !temp->IsVisible())
								temp->icon.color1.value().SetDirect(iconColor);
							else temp->icon.color1.value().SetTar(iconColor);
							if (temp->icon.color2.has_value())
							{
								if (forNum == 1 || state.barState.fold || !temp->IsVisible())
									temp->icon.color2.value().SetDirect(iconColor);
								else temp->icon.color2.value().SetTar(iconColor);
							}
						}
						if (temp->preset.load() == BarButtonPresetEnum::Divider)
						{
							temp->button.frameRendering = BarUiFrameRenderingEnum::PointLight;
							temp->button.frameLightColor = BarUiFrameLightColorEnum::Frame;
							temp->button.framePrimaryLightEnabled = false;
							temp->button.frameCursorLightIntensityScale =
								BarGeometryAttributeDividerCursorLightIntensity;
						}
						else
						{
							COLORREF buttonLightColor = temp->state->state == BarWidgetState::Selected
								? GetThemeColor(BarThemeColorEnum::Accent)
								: GetThemeColor(BarThemeColorEnum::TextPrimary);
							if (!temp->button.frame.has_value())
								temp->button.frame = BarUiColorClass(buttonLightColor);
							if (!temp->button.framePct.has_value())
								temp->button.framePct = BarUiPctClass(0.0);
							if (!temp->button.frameLightPct.has_value())
								temp->button.frameLightPct = BarUiPctClass(0.0);
							if (!temp->button.ft.has_value())
								temp->button.ft = BarUiValueClass(1.0);
							temp->button.frameRendering = BarUiFrameRenderingEnum::PointLight;
							temp->button.framePrimaryLightEnabled = false;
							temp->button.frameCursorLightIntensityScale = BarButtonCursorLightIntensity;
							if (forNum == 1 || state.barState.fold || temp->hide)
								temp->button.frame.value().SetDirect(buttonLightColor);
							else temp->button.frame.value().SetTar(buttonLightColor);
							// 主栏仅让选中按钮响应第三光源，未选中按钮保持无光影。
							bool buttonLightVisible = !state.barState.fold && !temp->hide
								&& temp->button.enable.tar
								&& temp->state->state == BarWidgetState::Selected;
							double buttonLightOpacity = buttonLightVisible
								? (temp->state->emph == BarWidgetEmphasize::Pressed
									? BarButtonPressedLightOpacity : 1.0) : 0.0;
							temp->button.frameLightPct.value().SetTar(buttonLightOpacity, operationDur);
						}

						if (temp->size == BarButtonSizeEnum::oneOne)
						{
							// 特殊设定：是否是颜色选择器
							bool isColorSelector = (temp->name.enable.tar && temp->name.content.GetTar().substr(0, 7) == L"__color");

							if (temp->button.enable.tar)
							{
								if (state.barState.fold || !temp->IsVisible())
								{
									if (state.barState.fold)
									{
										SetButtonPositionTar(temp->button.x, 40.0, 40.0);
										SetButtonPositionTar(temp->button.y, 40.0, 40.0);
									}

									temp->button.pct.SetTar(0.0, operationDur);
								}
								else
								{
SetButtonPositionTar(temp->button.x, xO + barBtnOneHalf, 40.0, true);
										// 1*1=32.5：两行时 top/gap/bottom 均为 5，且与 2*2 上下端对齐。
										SetButtonPositionTar(temp->button.y, yO + barBtnOneHalf, 40.0);

											if (isColorSelector) temp->button.pct.SetTar(1.0, operationDur); // 只有颜色选择器使用
										else
										{
											if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->button.pct.SetTar(0.1, operationDur);
											else if (temp->state->state == BarWidgetState::Selected) temp->button.pct.SetTar(0.2, operationDur);
											else if (temp->hoverStage == BarButtonHoverStageEnum::None)
												temp->button.pct.SetTar(0.0, operationDur);
										}
									}
								temp->button.w.SetTar(barBtnOne, operationDur);
								temp->button.h.SetTar(barBtnOne, operationDur);

								if (!isColorSelector)
								{
									if (temp->state->state == BarWidgetState::Selected)
										temp->button.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
									else temp->button.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
								}
							}
							if (temp->icon.enable.tar)
							{
								if (isColorSelector) temp->icon.SetWH(nullopt, 10.0); // 颜色选择器中的图标即为标识选中该颜色，所以需要较小尺寸
								else temp->icon.SetWH(nullopt, 20.0);

								temp->icon.x.SetTar(0.0);
								temp->icon.y.SetTar(0.0);
								if (state.barState.fold || !temp->IsVisible())
								{
									temp->icon.pct.SetTar(0.0, operationDur);
								}
								else
								{
									temp->icon.pct.SetTar(1.0, operationDur);
									if (temp->state->state == BarWidgetState::Selected)
										temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
									else temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
								}
							}
							if (temp->name.enable.tar)
							{
								// 无法容下文字的位置
								temp->name.pct.SetTar(0.0, operationDur);
							}

							// 记录目标绘制位置
							temp->lastDrawX = temp->button.x.tar;
							temp->lastDrawY = temp->button.y.tar;

							if (!temp->IsVisible())
							{
								temp->button.pct.SetTar(0.0, operationDur);
								temp->icon.pct.SetTar(0.0, operationDur);
								temp->name.pct.SetTar(0.0, operationDur);
							}
							else
							{
								// 位于第一行
								if (yO <= barBtnGap)
								{
									yO += barBtnOneStep;
									totalWidth += barBtnOneStep;
									// 只有在第一行时才增加总宽度，因为第二行没有再加的必要
									// 如果第二行是 twoOne 或 twoTwo 的按钮，则会自动换行到更右侧
								}
								// 位于第二行
								else
								{
									// 如果第一行是 twoOne，现在是第二行应该存在塞下第二个 1*1 的按钮的情况

									if (xO + barBtnOneStep >= totalWidth)
									{
										// 如果当前 xO + step 超过了总宽度，则换行到更右侧
										xO += barBtnOneStep;
										yO = barBtnGap;
									}
									else
									{
										// 否则继续在当前行
										xO += barBtnOneStep;
									}
								}
							}
						}
						if (temp->size == BarButtonSizeEnum::twoOne)
						{
							if (yO > barBtnGap)
							{
								// 如果当前位置处于第二行，且容不下一个 2*1 的按钮，则换行到更右侧
								if (xO + barBtnTwoStep > totalWidth)
								{
									xO = totalWidth;
									yO = barBtnGap;
								}
							}

							if (temp->button.enable.tar)
							{
								if (state.barState.fold || !temp->IsVisible())
								{
									if (state.barState.fold)
									{
										SetButtonPositionTar(temp->button.x, 40.0, 40.0);
										SetButtonPositionTar(temp->button.y, 40.0, 40.0);
									}

									temp->button.pct.SetTar(0.0, operationDur);
								}
								else
								{
SetButtonPositionTar(temp->button.x, xO + barBtnTwoHalf, 40.0, true);
										// 2*1=70x32.5：与 oneOne 同网格，两行贴齐 2*2 且间隙均为 5。
										SetButtonPositionTar(temp->button.y, yO + barBtnOneHalf, 40.0);

											if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->button.pct.SetTar(0.1, operationDur);
											else if (temp->state->state == BarWidgetState::Selected) temp->button.pct.SetTar(0.2, operationDur);
										else if (temp->hoverStage == BarButtonHoverStageEnum::None)
											temp->button.pct.SetTar(0.0, operationDur);
										}
									temp->button.w.SetTar(barBtnTwo, operationDur);
									temp->button.h.SetTar(barBtnOne, operationDur);

							if (temp->state->state == BarWidgetState::Selected)
								temp->button.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
							else temp->button.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
							}
							if (temp->icon.enable.tar)
							{
								temp->icon.SetWH(nullopt, 18.0);

								temp->icon.x.SetTar(-21.0); // 靠左对齐（70 宽内：左 5 + icon 18 + 间隙，右侧留给文字）
								temp->icon.y.SetTar(0.0);
								if (state.barState.fold || !temp->IsVisible()) temp->icon.pct.SetTar(0.0, operationDur);
								else
								{
									temp->icon.pct.SetTar(1.0, operationDur);
									if (temp->state->state == BarWidgetState::Selected)
										temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
									else temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
								}
							}
							if (temp->name.enable.tar)
							{
temp->name.x.SetTar(11.5); // 右对齐
									temp->name.y.SetTar(0.0);
									temp->name.w.SetTar(37); // 70px 宽度中除去左侧 icon 占用的 18px + 5px * 2 的空隙,考虑自身右侧还有 5px 的间隙
									temp->name.h.SetTar(barBtnOne);
								if (state.barState.fold || !temp->IsVisible()) temp->name.pct.SetTar(0.0, operationDur);
								else temp->name.pct.SetTar(1.0, operationDur);

								if (temp->state->state == BarWidgetState::Selected)
									temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::Accent));
								else temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
								temp->name.size.SetTar(12.0);
							}

							// 记录目标绘制位置
							temp->lastDrawX = temp->button.x.tar;
							temp->lastDrawY = temp->button.y.tar;

							if (!temp->IsVisible())
							{
								temp->button.pct.SetTar(0.0, operationDur);
								temp->icon.pct.SetTar(0.0, operationDur);
								temp->name.pct.SetTar(0.0, operationDur);
							}
							else
							{
								// 位于第一行
								if (yO <= barBtnGap)
								{
									yO += barBtnOneStep;
									totalWidth += barBtnTwoStep;
									// 只在第一行中增加总宽度，因为第二行没有再加的必要
									// 第二行如果是 oneOne 的按钮，那么在超过宽度时也会自动换行到更右侧
								}
								// 位于第二行
								else
								{
									xO += barBtnTwoStep;
									yO = barBtnGap;
								}
							}
						}
						if (temp->size == BarButtonSizeEnum::twoTwo)
						{
							if (yO > barBtnGap)
							{
								yO = barBtnGap;
								xO = totalWidth;
							}

							if (temp->button.enable.tar)
							{
								if (state.barState.fold || !temp->IsVisible())
								{
									if (state.barState.fold)
									{
										SetButtonPositionTar(temp->button.x, 40.0, 40.0);
										SetButtonPositionTar(temp->button.y, 40.0, 40.0);
									}

									temp->button.pct.SetTar(0.0, operationDur);
								}
								else
								{
SetButtonPositionTar(temp->button.x, xO + barBtnTwoHalf, 40.0, true);
								SetButtonPositionTar(temp->button.y, yO + barBtnTwoHalf, 40.0);

										if (temp->state->emph == BarWidgetEmphasize::Pressed) temp->button.pct.SetTar(0.1, operationDur);
										else if (temp->state->state == BarWidgetState::Selected) temp->button.pct.SetTar(0.2, operationDur);
									else if (temp->hoverStage == BarButtonHoverStageEnum::None)
										temp->button.pct.SetTar(0.0, operationDur);
									}
								temp->button.w.SetTar(barBtnTwo, operationDur);
								temp->button.h.SetTar(barBtnTwo, operationDur);

							if (temp->state->state == BarWidgetState::Selected)
								temp->button.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
							else temp->button.fill.value().SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
							}
							if (temp->icon.enable.tar)
							{
								bool enlargedGeometryIcon =
									temp->preset == BarButtonPresetEnum::Geometry
									&& stateMode.StateModeSelect == StateModeSelectEnum::IdtShape;
								bool enlargedMoreIcon =
									temp->preset == BarButtonPresetEnum::More;
								temp->icon.SetWH(nullopt,
									enlargedMoreIcon ? 34.0
									: (enlargedGeometryIcon ? 34.0 : 28.0));
								temp->icon.x.SetTar(0.0);
								temp->icon.y.SetTar(-10.0);
								if (state.barState.fold || !temp->IsVisible())
								{
									temp->icon.pct.SetTar(0.0, operationDur);
								}
								else
								{
									temp->icon.pct.SetTar(1.0, operationDur);
									if (temp->state->state == BarWidgetState::Selected)
										temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::Accent));
									else temp->icon.color1.value().SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
								}
							}
							if (temp->name.enable.tar)
							{
temp->name.x.SetTar(0.0);
									temp->name.y.SetTar(20.0);
									temp->name.w.SetTar(barBtnTwo);
									temp->name.h.SetTar(25.0);
								if (state.barState.fold || !temp->IsVisible()) temp->name.pct.SetTar(0.0, operationDur);
								else temp->name.pct.SetTar(1.0, operationDur);

								if (temp->state->state == BarWidgetState::Selected)
									temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::Accent));
								else temp->name.color.SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));

								temp->name.size.SetTar(13.0);
							}

							// 记录目标绘制位置
							temp->lastDrawX = temp->button.x.tar;
							temp->lastDrawY = temp->button.y.tar;

							if (!temp->IsVisible())
							{
								temp->button.pct.SetTar(0.0, operationDur);
								temp->icon.pct.SetTar(0.0, operationDur);
								temp->name.pct.SetTar(0.0, operationDur);
							}
							else
							{
xO += barBtnTwoStep, yO = barBtnGap;
									totalWidth += barBtnTwoStep;
								}
							}

							// 特殊体质 - 分隔栏
							if (temp->size == BarButtonSizeEnum::oneTwo)
							{
								if (temp->IsVisible())
								{
									// 小按钮留下半列时先封列；下一组从边界后的新列开始。
									xO = totalWidth;
									yO = barBtnGap;
								}

							if (temp->button.enable.tar)
							{
								if (state.barState.fold || !temp->IsVisible())
								{
									if (state.barState.fold)
									{
										SetButtonPositionTar(temp->button.x, 40.0, 40.0);
										SetButtonPositionTar(temp->button.y, 40.0, 40.0);
									}

									temp->button.pct.SetTar(0.0, operationDur);
									if (temp->button.frameLightPct.has_value())
										temp->button.frameLightPct.value().SetTar(0.0, operationDur);
								}
								else
								{
									// 细线居中落在上一组末尾已有的 5 DIP 间隙内。
SetButtonPositionTar(temp->button.x, xO - barBtnGap / 2.0, 40.0, true);
								SetButtonPositionTar(temp->button.y, yO + barBtnTwoHalf, 40.0);
									temp->button.pct.SetTar(0.30, operationDur);
									if (temp->button.frameLightPct.has_value())
									{
										if (forNum == 1)
											temp->button.frameLightPct.value().SetDirect(1.0);
										else temp->button.frameLightPct.value().SetTar(1.0, operationDur);
									}
								}
								temp->button.w.SetTar(1.0, operationDur);
								// 布局仍按 oneTwo 封满两行，实际线长保留两端 5 DIP 留白和圆角半径。
								temp->button.h.SetTar(50.0, operationDur);
								if (temp->button.rw.has_value()) temp->button.rw.value().SetTar(0.5, operationDur);
								if (temp->button.rh.has_value()) temp->button.rh.value().SetTar(0.5, operationDur);
								if (temp->button.ft.has_value()) temp->button.ft.value().SetTar(1.0, operationDur);
								if (temp->button.framePct.has_value()) temp->button.framePct.value().SetTar(0.0, operationDur);

								const COLORREF dividerColor = GetThemeColor(BarThemeColorEnum::SurfaceFrame);
								temp->button.fill.value().SetTar(dividerColor);
								if (temp->button.frame.has_value()) temp->button.frame.value().SetTar(dividerColor);
							}
							temp->icon.pct.SetDirect(0.0);

							// 记录目标绘制位置
							temp->lastDrawX = temp->button.x.tar;
							temp->lastDrawY = temp->button.y.tar;

							if (!temp->IsVisible())
							{
								temp->button.pct.SetTar(0.0, operationDur);
								if (temp->button.frameLightPct.has_value())
									temp->button.frameLightPct.value().SetTar(0.0, operationDur);
								temp->icon.pct.SetTar(0.0, operationDur);
								temp->name.pct.SetTar(0.0, operationDur);
							}
						}

						// 按压倍率独立于布局批次，松手或拖出时从当前值回弹到标准大小。
						if (temp->preset == BarButtonPresetEnum::Divider)
						{
							// Divider 不参与按钮状态机，但保留独立 frameLightPct 的第三光。
							temp->hoverStage = BarButtonHoverStageEnum::None;
							temp->state->emph = BarWidgetEmphasize::None;
							temp->pressScale.SetDirect(1.0);
							temp->button.pct.animateWhenDisabled = false;
							if (temp->button.fill.has_value())
								temp->button.fill.value().animateWhenDisabled = false;
						}
						else if (temp->state->emph == BarWidgetEmphasize::Pressed)
							temp->pressScale.SetTar(BarButtonPressScale, BarUiDefaultOperationDur,
								nullopt, false, state.buttonPressCurve);
						else temp->pressScale.SetTar(1.0, BarUiDefaultOperationDur,
							nullopt, false, state.buttonReleaseCurve);

						// 尺寸枚举只负责选择布局，按钮及其内容统一在同一过程时间内到达新布局。
						SyncValueDuration(temp->button.x);
						SyncValueDuration(temp->button.y);
						SyncValueDuration(temp->button.w);
						SyncValueDuration(temp->button.h);
						SyncPctDuration(temp->button.pct);
						SyncValueDuration(temp->icon.x);
						SyncValueDuration(temp->icon.y);
						SyncValueDuration(temp->icon.w);
						SyncValueDuration(temp->icon.h);
						SyncValueDuration(temp->icon.angle);
						SyncPctDuration(temp->icon.pct);
						SyncValueDuration(temp->name.x);
						SyncValueDuration(temp->name.y);
						SyncValueDuration(temp->name.w);
						SyncValueDuration(temp->name.h);
						SyncValueDuration(temp->name.size);
						SyncPctDuration(temp->name.pct);

						if (mainBarSideSwitch || continueMainBarSideSwitchKeyframe)
						{
							// 换边中点将整个按钮组合隐藏，再从主按钮下方展开到新位置。
							const BarUiCurveSpecClass& pctCurve = mainBarSideSwitch
								? keyframePctCurve : continuedKeyframePctCurve;
							temp->button.pct.SetTar(temp->button.pct.tar, operationDur, 0.0, true, pctCurve);
							if (temp->button.frameLightPct.has_value())
								temp->button.frameLightPct.value().SetTar(
									temp->button.frameLightPct.value().tar, operationDur, 0.0, true, pctCurve);
							temp->icon.pct.SetTar(temp->icon.pct.tar, operationDur, 0.0, true, pctCurve);
							temp->name.pct.SetTar(temp->name.pct.tar, operationDur, 0.0, true, pctCurve);
						}
					}
				}, viewVariant);

			auto FindVisibleAnchor = [&](BarButtonClass* hidden, BarButtonClass* preferred)
				{
					const int buttonCount = state.barButtonSet.tot.load();
					int hiddenIndex = -1;
					for (int index = 0; index < buttonCount; index++)
					{
						BarButtonClass* candidate = state.barButtonSet.buttonList.Get(index);
						if (candidate == hidden) hiddenIndex = index;
						if (candidate && candidate == preferred && candidate->IsVisible()) return candidate;
					}
					if (hiddenIndex < 0) return static_cast<BarButtonClass*>(nullptr);

					// 首选锚点不可见时，按布局距离寻找最近的有效按钮。
					for (int distance = 1; distance < buttonCount; distance++)
					{
						const int previousIndex = hiddenIndex - distance;
						if (previousIndex >= 0)
						{
							BarButtonClass* candidate = state.barButtonSet.buttonList.Get(previousIndex);
							if (candidate && candidate->IsVisible()) return candidate;
						}

						const int nextIndex = hiddenIndex + distance;
						if (nextIndex < buttonCount)
						{
							BarButtonClass* candidate = state.barButtonSet.buttonList.Get(nextIndex);
							if (candidate && candidate->IsVisible()) return candidate;
						}
					}
					return static_cast<BarButtonClass*>(nullptr);
				};
			auto AnchorHiddenButton = [&](BarButtonPresetEnum hiddenPreset, BarButtonPresetEnum anchorPreset)
				{
					BarButtonClass* hidden = state.barButtonSet.preset[static_cast<int>(hiddenPreset)];
					if (state.barState.fold || !hidden || hidden->IsVisible()) return;

					BarButtonClass* anchor = FindVisibleAnchor(
						hidden, state.barButtonSet.preset[static_cast<int>(anchorPreset)]);
					if (!anchor) return;

					// 隐藏控件停在来源按钮中心，显示时从该位置展开。
					SetButtonPositionTar(hidden->button.x, anchor->button.x.tar, 40.0);
					SetButtonPositionTar(hidden->button.y, anchor->button.y.tar, 40.0);
					hidden->lastDrawX = anchor->button.x.tar;
					hidden->lastDrawY = anchor->button.y.tar;
				};
			AnchorHiddenButton(BarButtonPresetEnum::Eraser, BarButtonPresetEnum::Draw);
			AnchorHiddenButton(BarButtonPresetEnum::Geometry, BarButtonPresetEnum::Draw);
			AnchorHiddenButton(BarButtonPresetEnum::Recall, BarButtonPresetEnum::Draw);
			AnchorHiddenButton(BarButtonPresetEnum::Pierce, BarButtonPresetEnum::Freeze);
		}
		totalWidth = layoutTotalWidth;
		Inkeys::UI::Bar::Zoom::FitInitialAfterMainBarLayout(owner_, totalWidth);
		{ /**/ }

		// 主栏
		{
			if (state.barState.fold)
			{
				mainBar->x.SetTar(0.0, operationDur, nullopt, forceRestartMainBarLayout, syncedValueCurve);
				mainBar->w.SetTar(80.0, operationDur, nullopt, forceRestartMainBarLayout, syncedValueCurve);

				state.shapeMap[BarUISetShapeEnum::MainBar]->pct.SetTar(0.0, operationDur, nullopt, false, syncedPctCurve);
				state.shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().SetTar(0.0, operationDur, nullopt, false, syncedPctCurve);
			}
			else
			{
				if (mainBarSideSwitch)
					mainBar->w.SetTar(totalWidth, operationDur, 80.0, true, keyframeValueCurve);
				else if (continueMainBarSideSwitchKeyframe)
					// 布局目标变化不能丢掉换边中点，继续在原批次 0.5 时刻收窄到主按钮宽度。
					mainBar->w.SetTar(totalWidth, operationDur, 80.0, true, continuedKeyframeValueCurve);
				else mainBar->w.SetTar(totalWidth, operationDur, nullopt,
					forceRestartMainBarLayout, syncedValueCurve);

				double targetX = 0.0;
				if (state.barState.widgetPosition.mainBar)
					targetX = state.superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + mainBar->w.tar / 2.0 + 10.0;
				else
					targetX = -(state.superellipseMap[BarUISetSuperellipseEnum::MainButton]->GetW() / 2.0 + mainBar->w.tar / 2.0 + 10.0);

				if (mainBarSideSwitch)
					mainBar->x.SetTar(targetX, operationDur, 0.0, true, keyframeValueCurve);
				else if (continueMainBarSideSwitchKeyframe)
					mainBar->x.SetTar(targetX, operationDur, 0.0, true, continuedKeyframeValueCurve);
				else mainBar->x.SetTar(targetX, operationDur, nullopt,
					forceRestartMainBarLayout, syncedValueCurve);

				state.shapeMap[BarUISetShapeEnum::MainBar]->pct.SetTar(
					0.8, operationDur, nullopt, false, syncedPctCurve);
				state.shapeMap[BarUISetShapeEnum::MainBar]->framePct.value().SetTar(
					0.18, operationDur, nullopt, false, syncedPctCurve);
			}
			if (mainBarSideSwitch || continueMainBarSideSwitchKeyframe)
			{
				// 主栏填充和边框在换边关键帧同步变为全透明。
				const BarUiCurveSpecClass& pctCurve = mainBarSideSwitch
					? keyframePctCurve : continuedKeyframePctCurve;
				mainBar->pct.SetTar(mainBar->pct.tar, operationDur, 0.0, true, pctCurve);
				mainBar->framePct.value().SetTar(
					mainBar->framePct.value().tar, operationDur, 0.0, true, pctCurve);
			}
			state.shapeMap[BarUISetShapeEnum::MainBar]->fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Surface));
			state.shapeMap[BarUISetShapeEnum::MainBar]->frame.value().SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));

			// 绘制属性
			{
				bool drawAttributeBatchChange = drawAttributeVisibilityChange || drawAttributeSideSwitch;
				operationDur = BarUiDefaultOperationDur;
				double drawAttributePhase = 0.0;
				bool continueDrawAttributePhase = false;
				if (drawAttributeBatchChange)
				{
					// 只在主栏批次前 50% 内加入；进入后半程则使用完整时长创建独立新批次。
					if (state.mainBarTimeline.CanJoin())
					{
						operationDur = state.mainBarTimeline.GetRemainingDuration();
						drawAttributePhase = state.mainBarTimeline.GetProgress();
						continueDrawAttributePhase = drawAttributePhase > 0.0;
					}
					state.drawAttributeTimeline.Restart(operationDur);
				}
				else if (state.drawAttributeTimeline.IsActive())
				{
					operationDur = state.drawAttributeTimeline.GetRemainingDuration();
					drawAttributePhase = state.drawAttributeTimeline.GetProgress();
					continueDrawAttributePhase = drawAttributePhase > 0.0;
				}
				syncValueCurveFromBatch = state.drawAttributeTimeline.IsActive();
				BarUiCurveEnum drawAttributeCurve = state.drawAttributeTimeline.IsActive()
					? (state.barState.drawAttribute ? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack)
					: BarUiCurveEnum::EaseInOutCubic;
				BarUiCurveEnum drawAttributePctCurve = state.drawAttributeTimeline.IsActive()
					&& !state.barState.drawAttribute
					? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine;
				syncedValueCurve = {
					drawAttributeCurve, drawAttributeCurve,
					drawAttributePhase, continueDrawAttributePhase };
				syncedPctCurve = { drawAttributePctCurve, drawAttributePctCurve,
					drawAttributePhase, continueDrawAttributePhase };
				const BarUiCurveSpecClass drawAttributeKeyframeValueCurve{
					BarUiCurveEnum::EaseInCubic, BarUiCurveEnum::EaseOutBack,
					drawAttributePhase, continueDrawAttributePhase };
				const BarUiCurveSpecClass drawAttributeKeyframePctCurve{
					BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine,
					drawAttributePhase, continueDrawAttributePhase };
				auto CompactDrawAttributeX = [&](double expandedX) { return expandedX * BarDrawAttributeCompactScale; };
				auto CompactDrawAttributeY = [&](double expandedY)
					{
						return expandedY * BarDrawAttributeCompactScale;
					};
				auto CompactDrawAttributeSize = [&](double expandedSize)
					{
						return expandedSize * BarDrawAttributeCompactScale;
					};
				double drawAttributeLayoutScale =
					state.barState.drawAttribute ? 1.0 : BarDrawAttributeCompactScale;
				auto drawAttributeBar =
					state.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
				if (!state.barState.drawAttribute)
				{
					// 收起面板保持与展开面板相同宽高比，并居中藏在绘制按钮下方。
					drawAttributeBar->x.SetTar(0.0);
					drawAttributeBar->y.SetTar(0.0);
					drawAttributeBar->w.SetTar(BarDrawAttributeCompactWidth);
					drawAttributeBar->h.SetTar(BarDrawAttributeCompactHeight);

					drawAttributeBar->pct.SetTar(0.0);
					drawAttributeBar->framePct.value().SetTar(0.0);
				}
				else
				{
					drawAttributeBar->w.SetTar(BarDrawAttributeExpandedWidth);
					drawAttributeBar->h.SetTar(BarDrawAttributeExpandedHeight);

					drawAttributeBar->x.SetTar(0);
					if (state.barState.widgetPosition.primaryBar)
						drawAttributeBar->y.SetTar((state.shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + drawAttributeBar->GetH() / 2.0 + 10.0));
					else
						drawAttributeBar->y.SetTar(-(state.shapeMap[BarUISetShapeEnum::MainBar]->GetH() / 2.0 + drawAttributeBar->GetH() / 2.0 + 10.0));

					drawAttributeBar->pct.SetTar(
						BarDrawAttributeSurfaceOpacity);
					drawAttributeBar->framePct.value().SetTar(0.18);
				}
				drawAttributeBar->rw.value().SetTar(8.0 * drawAttributeLayoutScale);
				drawAttributeBar->rh.value().SetTar(8.0 * drawAttributeLayoutScale);
				drawAttributeBar->ft.value().SetTar(drawAttributeLayoutScale);
				drawAttributeBar->fill.value().SetTar(GetThemeColor(BarThemeColorEnum::Surface));
				drawAttributeBar->frame.value().SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));

				// Color 区域
				{
					// 上下布局直接提交最终坐标，避免同一帧多次 SetTar 重启动画。
					double colorTopY = state.barState.widgetPosition.primaryBar ? 5.0 : 115.0;
					double colorBottomY = state.barState.widgetPosition.primaryBar ? 40.0 : 150.0;
					// Color 1
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.SetTar(CompactDrawAttributeX(5.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(
								CompactDrawAttributeY(colorTopY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->x.SetTar(5.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->y.SetTar(colorTopY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->fill.value().tar))
						{
							// 说明当前选中的是当前的颜色
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1]->ft.value().SetTar(1.0);
						}
					}
					// Color 2
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->x.SetTar(CompactDrawAttributeX(5.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(
								CompactDrawAttributeY(colorBottomY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->x.SetTar(5.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->y.SetTar(colorBottomY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2]->ft.value().SetTar(1.0);
						}
					}
					// Color 3
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->x.SetTar(CompactDrawAttributeX(40.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(
								CompactDrawAttributeY(colorTopY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->x.SetTar(40.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->y.SetTar(colorTopY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3]->ft.value().SetTar(1.0);
						}
					}
					// Color 4
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->x.SetTar(CompactDrawAttributeX(40.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(
								CompactDrawAttributeY(colorBottomY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->x.SetTar(40.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->y.SetTar(colorBottomY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4]->ft.value().SetTar(1.0);
						}
					}
					// Color 5
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->x.SetTar(CompactDrawAttributeX(75.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(
								CompactDrawAttributeY(colorTopY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->x.SetTar(75.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->y.SetTar(colorTopY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5]->ft.value().SetTar(1.0);
						}
					}
					// Color 6
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->x.SetTar(CompactDrawAttributeX(75.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(
								CompactDrawAttributeY(colorBottomY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->x.SetTar(75.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->y.SetTar(colorBottomY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6]->ft.value().SetTar(1.0);
						}
					}
					// Color 7
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->x.SetTar(CompactDrawAttributeX(110.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(
								CompactDrawAttributeY(colorTopY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->x.SetTar(110.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->y.SetTar(colorTopY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7]->ft.value().SetTar(1.0);
						}
					}
					// Color 8
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->x.SetTar(CompactDrawAttributeX(110.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(
								CompactDrawAttributeY(colorBottomY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->x.SetTar(110.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->y.SetTar(colorBottomY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8]->ft.value().SetTar(1.0);
						}
					}
					// Color 9
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->x.SetTar(CompactDrawAttributeX(145.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(
								CompactDrawAttributeY(colorTopY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->x.SetTar(145.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->y.SetTar(colorTopY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9]->ft.value().SetTar(1.0);
						}
					}
					// Color 10
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->x.SetTar(CompactDrawAttributeX(145.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(
								CompactDrawAttributeY(colorBottomY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->x.SetTar(145.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->y.SetTar(colorBottomY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10]->ft.value().SetTar(1.0);
						}
					}
					// Color 11
					{
						if (!state.barState.drawAttribute)
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->x.SetTar(CompactDrawAttributeX(180.0));
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(
								CompactDrawAttributeY(colorTopY));

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(0.0);
						}
						else
						{
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->x.SetTar(180.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->y.SetTar(colorTopY);

							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(1.0);
						}

						if (state.barState.drawAttribute && Inkeys::Color::CompereColorRef(GetPenColor(), state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->fill.value().tar))
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(1.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->ft.value().SetTar(1.0);
						}
						else
						{
							state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11]->pct.SetTar(0.0);
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11]->ft.value().SetTar(1.0);
						}
					}
					// Color 12：圆盘始终存在，色芯和右下角绿勾只在自定义色模式中淡入。
					{
						auto customSwatch = state.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ColorSelect12];
						auto customInner = state.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner];
						auto customWheel = state.pngMap[
							BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel];
						auto customCheck = state.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check];
						COLORREF currentColor = GetPenColor() & 0x00FFFFFF;
						bool customSelected = !IsBarPresetColor(currentColor);
						bool customPressed = ReadColorPickerEntryPressed()
							&& state.barState.drawAttribute;
						state.drawAttributeColorPickerEntryPressScale.SetTar(
							customPressed ? BarButtonPressScale : 1.0,
							BarUiDefaultOperationDur, nullopt, false,
							customPressed ? state.buttonPressCurve : state.buttonReleaseCurve);
						if (!state.barState.drawAttribute)
						{
							customSwatch->x.SetTar(CompactDrawAttributeX(180.0));
							customSwatch->y.SetTar(
								CompactDrawAttributeY(colorBottomY));
							customSwatch->pct.SetTar(0.0);
							customWheel->pct.SetTar(0.0);
							customInner->pct.SetTar(0.0);
							customCheck->pct.SetTar(0.0);
						}
						else
						{
							customSwatch->x.SetTar(180.0);
							customSwatch->y.SetTar(colorBottomY);
							customSwatch->pct.SetTar(1.0);
							customWheel->pct.SetTar(1.0);
							customInner->pct.SetTar(customSelected ? 1.0 : 0.0,
								operationDur);
							customCheck->pct.SetTar(customSelected ? 1.0 : 0.0,
								operationDur);
						}
						double customScale = drawAttributeLayoutScale;
						// 色盘铺满入口，随后绘制的 1 DIP 外框覆盖其边缘，避免出现内圈空隙。
						customWheel->x.SetTar(0.0);
						customWheel->y.SetTar(0.0);
						// 色芯尺寸改为 20×20 后，(30-20)/2=5 保持居中。
						customInner->x.SetTar(5.0 * customScale);
						customInner->y.SetTar(5.0 * customScale);
						customCheck->x.SetTar(15.0 * customScale);
						customCheck->y.SetTar(15.0 * customScale);
						// 色芯只动画透明度，拖动色板时纯色必须即时跟手。
						customInner->fill->SetDirect(currentColor);
						customInner->frame->SetTar(
							GetThemeColor(BarThemeColorEnum::SurfaceFrame), operationDur);
						customInner->framePct->SetTar(customSelected
							&& state.barState.drawAttribute ? BarColorSwatchFrameOpacity : 0.0);
						customSwatch->framePct->SetTar(
							state.barState.drawAttribute ? BarColorSwatchFrameOpacity : 0.0);
						if (customSwatch->frameLightPct.has_value())
							customSwatch->frameLightPct->SetTar(
								state.barState.drawAttribute
									? (customPressed
										? BarButtonPressedLightOpacity : 1.0)
									: 0.0);
						if (customSwatch->frame.has_value())
							customSwatch->frame->SetTar(
								GetThemeColor(BarThemeColorEnum::SurfaceFrame), operationDur);
					}

					// 面板本体和文字显式跟随 UI3 主题，避免运行时换色后停留在旧主题。
					auto pickerPanel = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel];
					pickerPanel->fill->SetTar(
						GetThemeColor(BarThemeColorEnum::Surface), operationDur);
					pickerPanel->frame->SetTar(
						GetThemeColor(BarThemeColorEnum::SurfaceFrame), operationDur);
					{
						auto toneHit = state.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle];
						auto closeHit = state.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit];
						// 与笔类型按钮一致：背景层用 PressedFill，不启用边框光影动画。
						toneHit->fill->SetTar(
							GetThemeColor(BarThemeColorEnum::PressedFill), operationDur);
						closeHit->fill->SetTar(
							GetThemeColor(BarThemeColorEnum::PressedFill), operationDur);
						if (!toneHit->framePct.has_value()) toneHit->framePct = BarUiPctClass(0.0);
						if (!closeHit->framePct.has_value()) closeHit->framePct = BarUiPctClass(0.0);
						if (!toneHit->frameLightPct.has_value())
							toneHit->frameLightPct = BarUiPctClass(0.0);
						if (!closeHit->frameLightPct.has_value())
							closeHit->frameLightPct = BarUiPctClass(0.0);
						toneHit->framePct->SetTar(0.0);
						closeHit->framePct->SetTar(0.0);
						toneHit->frameLightPct->SetTar(0.0);
						closeHit->frameLightPct->SetTar(0.0);
					}
					state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerHoldHint]
						->fill->SetTar(
							GetThemeColor(BarThemeColorEnum::Surface), operationDur);
					const BarUISetWordEnum pickerThemeWords[] =
					{
						BarUISetWordEnum::DrawAttributeBar_ColorPickerRgb,
						BarUISetWordEnum::DrawAttributeBar_ColorPickerG,
						BarUISetWordEnum::DrawAttributeBar_ColorPickerB,
						BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacity,
						BarUISetWordEnum::DrawAttributeBar_ColorPickerRgbValue,
						BarUISetWordEnum::DrawAttributeBar_ColorPickerGValue,
						BarUISetWordEnum::DrawAttributeBar_ColorPickerBValue,
						BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacityValue,
					};
					for (auto wordType : pickerThemeWords)
						state.wordMap[wordType]->color.SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary), operationDur);
					// 太阳/月亮图标随主题文字色变化。
					COLORREF pickerToneIconColor =
						GetThemeColor(BarThemeColorEnum::TextPrimary);
					if (forNum == 1 || !state.barState.drawAttribute)
					{
						state.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneSun]
							->color1.value().SetDirect(pickerToneIconColor);
						state.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneMoon]
							->color1.value().SetDirect(pickerToneIconColor);
					}
					else
					{
						state.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneSun]
							->color1.value().SetTar(pickerToneIconColor);
						state.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneMoon]
							->color1.value().SetTar(pickerToneIconColor);
					}
				}
				{ /**/ }
				// 画笔样式区域
				{
					auto SetDrawAttributeSvgColor = [&](BarUISetSvgEnum type, COLORREF color)
						{
							auto& svgColor = state.svgMap[type]->color1.value();
							// 属性栏隐藏时预先完成设色，再次展开不会出现黑色到主题色的过程。
							if (forNum == 1 || !state.barState.drawAttribute) svgColor.SetDirect(color);
							else svgColor.SetTar(color);
						};
					struct PenTypeButtonLayout
					{
						BarUISetShapeEnum shape;
						BarUISetSvgEnum svg;
						BarUISetWordEnum word;
						double y;
						bool enabled;
						bool selected;
						bool pressed;
						IdtAtomic<BarButtonHoverStageEnum>* hoverStage;
						BarUiValueClass* pressScale;
					};
					const PenTypeButtonLayout penTypeButtons[] =
					{
						{ BarUISetShapeEnum::DrawAttributeBar_Brush2,
							BarUISetSvgEnum::DrawAttributeBar_Brush2,
							BarUISetWordEnum::DrawAttributeBar_Brush2,
							5.0, false, false, false, nullptr, nullptr },
						{ BarUISetShapeEnum::DrawAttributeBar_Laser,
							BarUISetSvgEnum::DrawAttributeBar_Laser,
							BarUISetWordEnum::DrawAttributeBar_Laser,
							40.0, false, false, false, nullptr, nullptr },
						{ BarUISetShapeEnum::DrawAttributeBar_Highlight1,
							BarUISetSvgEnum::DrawAttributeBar_Highlight1,
							BarUISetWordEnum::DrawAttributeBar_Highlight1,
							75.0, true,
							stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1,
							state.barState.drawAttributeBar.highlight1Press,
							&state.drawAttributeHighlightHoverStage, &state.drawAttributeHighlightPressScale },
						{ BarUISetShapeEnum::DrawAttributeBar_Brush1,
							BarUISetSvgEnum::DrawAttributeBar_Brush1,
							BarUISetWordEnum::DrawAttributeBar_Brush1,
							110.0, true,
							stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1,
							state.barState.drawAttributeBar.brush1Press,
							&state.drawAttributeBrushHoverStage, &state.drawAttributeBrushPressScale },
						{ BarUISetShapeEnum::DrawAttributeBar_SoftPen,
							BarUISetSvgEnum::DrawAttributeBar_SoftPen,
							BarUISetWordEnum::DrawAttributeBar_SoftPen,
							145.0, false, false, false, nullptr, nullptr },
					};
					for (const auto& button : penTypeButtons)
					{
						auto shape = state.shapeMap[button.shape];
						auto svg = state.svgMap[button.svg];
						auto word = state.wordMap[button.word];
						double layoutScale = drawAttributeLayoutScale;

						bool buttonShowsExtension = button.selected
							&& PenModeSupportsAnnotationLine(
								stateMode.Pen.ModeSelect);
						shape->x.SetTar(BarDrawAttributePenTypeLeft * layoutScale);
						shape->y.SetTar(button.y * layoutScale);
						shape->w.SetTar(BarDrawAttributePenTypeButtonWidth * layoutScale);
						shape->h.SetTar(BarDrawAttributePenTypeButtonHeight * layoutScale);
						shape->rw.value().SetTar(4.0 * layoutScale);
						shape->rh.value().SetTar(4.0 * layoutScale);
						shape->ft.value().SetTar(layoutScale);
						svg->x.SetTar(6.0 * layoutScale);
						svg->y.SetTar(0.0);
						svg->SetWH(18.0 * layoutScale, 18.0 * layoutScale);
						word->x.SetTar(30.0 * layoutScale);
						word->y.SetTar(0.0);
						word->w.SetTar((buttonShowsExtension
							? BarDrawAttributePenTypeExtensionDividerX - 35.0
							: BarDrawAttributePenTypeButtonWidth - 35.0)
							* layoutScale);
						word->h.SetTar(BarDrawAttributePenTypeButtonHeight * layoutScale);
						word->size.SetTar(12.0 * layoutScale);
						if (button.enabled)
						{
							if (!shape->frame.has_value()) shape->frame = BarUiColorClass(GetThemeColor(BarThemeColorEnum::TextPrimary));
							if (!shape->framePct.has_value()) shape->framePct = BarUiPctClass(0.0);
							if (!shape->frameLightPct.has_value()) shape->frameLightPct = BarUiPctClass(0.0);
							if (!shape->ft.has_value()) shape->ft = BarUiValueClass(1.0);
							shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
							shape->framePrimaryLightEnabled = false;
							shape->frameCursorLightIntensityScale = BarButtonCursorLightIntensity;
						}

						if (!state.barState.drawAttribute)
						{
							shape->pct.SetTar(0.0);
							svg->pct.SetTar(0.0);
							word->pct.SetTar(0.0);
							if (shape->frameLightPct.has_value()) shape->frameLightPct->SetTar(0.0);
						}
						else
						{
							// 预留笔型只禁用交互与光影，图标和文字仍正常显示。
							svg->pct.SetTar(1.0);
							word->pct.SetTar(1.0);
							if (!button.enabled) shape->pct.SetTar(0.0);
							else if (button.pressed) shape->pct.SetTar(0.1);
							else if (button.selected) shape->pct.SetTar(0.2);
							else if (button.hoverStage
								&& *button.hoverStage == BarButtonHoverStageEnum::None)
								shape->pct.SetTar(0.0);
							if (shape->frameLightPct.has_value())
								shape->frameLightPct->SetTar(button.enabled && button.selected
									? (button.pressed ? BarButtonPressedLightOpacity : 1.0) : 0.0);
						}

						COLORREF contentColor = button.enabled
							? (button.selected
								? GetThemeColor(BarThemeColorEnum::Accent)
								: GetThemeColor(BarThemeColorEnum::TextPrimary))
							: RGB(200, 200, 200);
						word->color.SetTar(contentColor);
						SetDrawAttributeSvgColor(button.svg, contentColor);
						if (button.enabled) shape->frame.value().SetTar(contentColor);
						shape->fill.value().SetTar(button.selected
							? GetThemeColor(BarThemeColorEnum::Accent)
							: GetThemeColor(BarThemeColorEnum::PressedFill));

						if (button.pressScale)
						{
							button.pressScale->SetTar(
								button.pressed ? BarButtonPressScale : 1.0,
								BarUiDefaultOperationDur, nullopt, false,
								button.pressed ? state.buttonPressCurve : state.buttonReleaseCurve);
						}
				}

				double layoutScale = drawAttributeLayoutScale;
				bool extensionVisible = state.barState.drawAttribute && !state.barState.fold
					&& PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
					double extensionY = stateMode.Pen.ModeSelect
						== PenModeSelectEnum::IdtPenHighlighter1 ? 75.0 : 110.0;
					auto extensionHit = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit];
					auto extensionDivider = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionDivider];
					double extensionX = BarDrawAttributePenTypeLeft
						+ BarDrawAttributePenTypeExtensionDividerX;
					extensionHit->x.SetDirect(extensionX * layoutScale);
					extensionHit->y.SetDirect(extensionY * layoutScale);
					extensionHit->w.SetDirect(extensionVisible
						? BarDrawAttributePenTypeExtensionWidth * layoutScale : 0.0);
					extensionHit->h.SetDirect(extensionVisible
						? BarDrawAttributePenTypeButtonHeight * layoutScale : 0.0);
					extensionHit->rw->SetDirect(4.0 * layoutScale);
					extensionHit->rh->SetDirect(4.0 * layoutScale);
					extensionHit->ft->SetDirect(layoutScale);
					extensionHit->fill->SetTar(
						GetThemeColor(BarThemeColorEnum::PressedFill), operationDur);
					if (!extensionVisible) extensionHit->pct.SetTar(0.0);
					else if (state.barState.drawAttributeBar.penTypeExtensionPress)
						extensionHit->pct.SetTar(0.10);
					else if (state.drawAttributePenTypeExtensionHoverStage
						== BarButtonHoverStageEnum::None)
						extensionHit->pct.SetTar(0.0);
					state.drawAttributePenTypeExtensionPressScale.SetTar(
						state.barState.drawAttributeBar.penTypeExtensionPress
							? BarButtonPressScale : 1.0,
						BarUiDefaultOperationDur, nullopt, false,
						state.barState.drawAttributeBar.penTypeExtensionPress
							? state.buttonPressCurve : state.buttonReleaseCurve);

					extensionDivider->x.SetDirect(extensionX * layoutScale);
					extensionDivider->y.SetDirect(
						(extensionY + BarDrawAttributeGap) * layoutScale);
					extensionDivider->w.SetDirect(BarUiDividerWidth * layoutScale);
					extensionDivider->h.SetDirect(
						(BarDrawAttributePenTypeButtonHeight
							- BarDrawAttributeGap * 2.0) * layoutScale);
					extensionDivider->rw->SetDirect(BarUiDividerRadius * layoutScale);
					extensionDivider->rh->SetDirect(BarUiDividerRadius * layoutScale);
					extensionDivider->ft->SetDirect(layoutScale);
					extensionDivider->fill->SetTar(
						GetThemeColor(BarThemeColorEnum::Accent), operationDur);
					extensionDivider->frame->SetTar(
						GetThemeColor(BarThemeColorEnum::Accent), operationDur);
					extensionDivider->pct.SetTar(extensionVisible ? 0.30 : 0.0);
					extensionDivider->frameLightPct->SetTar(
						extensionVisible ? 1.0 : 0.0);

					auto extensionArrow = state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_PenTypeExtensionArrow];
					bool arrowOpenBelow = state.barState.drawAttributeBar
						.penTypeMenuDirectionLocked
						? static_cast<bool>(state.barState.drawAttributeBar.penTypeMenuOpenBelow)
						: static_cast<bool>(state.barState.widgetPosition.primaryBar);
					double extensionCollapsedAngle = arrowOpenBelow ? 180.0 : 0.0;
					double extensionTargetAngle =
						state.barState.drawAttributeBar.penTypeMenuOpen
							? 180.0 - extensionCollapsedAngle
							: extensionCollapsedAngle;
					if (forNum == 1 || !extensionVisible)
						extensionArrow->angle.SetDirect(extensionTargetAngle);
					else extensionArrow->angle.SetTar(
						extensionTargetAngle, operationDur);
					SetDrawAttributeSvgColor(
						BarUISetSvgEnum::DrawAttributeBar_PenTypeExtensionArrow,
						// 笔型三角按下仍保持 Accent，缩放和入口背景负责按压反馈。
						GetThemeColor(BarThemeColorEnum::Accent));
				}
				{ /**/ }
				// 粗细调节区域
				{
					double layoutScale = drawAttributeLayoutScale;
					double thicknessY = state.barState.widgetPosition.primaryBar ? 75.0 : 5.0;
					bool thicknessControlsOnTop =
						state.barState.widgetPosition.primaryBar;
					double thicknessDividerOffsetY = thicknessControlsOnTop
						? 0.0
						: BarDrawAttributeThicknessHeight - BarUiDividerWidth;
					double thicknessControlOffsetY = thicknessControlsOnTop
						? BarUiDividerWidth + BarDrawAttributeGap
						: thicknessDividerOffsetY - BarDrawAttributeGap
							- BarDrawAttributeThicknessControlHeight;
					auto thicknessRegion =
						state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
					thicknessRegion->x.SetTar(
						BarDrawAttributeGap * layoutScale);
					thicknessRegion->y.SetTar(thicknessY * layoutScale);
					thicknessRegion->w.SetTar(240.0 * layoutScale);
					thicknessRegion->h.SetTar(
						BarDrawAttributeThicknessHeight * layoutScale);
					thicknessRegion->rw.value().SetTar(4.0 * layoutScale);
					thicknessRegion->rh.value().SetTar(4.0 * layoutScale);
					thicknessRegion->ft.value().SetTar(layoutScale);
					// 旧外框只保留为布局/裁剪几何，不再绘制或参与第三光。
					thicknessRegion->pct.SetTar(0.0);
					thicknessRegion->framePct.value().SetTar(0.0);
					thicknessRegion->frameLightPct.value().SetTar(0.0);
					thicknessRegion->frame.value().SetTar(
						GetThemeColor(BarThemeColorEnum::SurfaceFrame), operationDur);

					auto thicknessDivider = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ThicknessDivider];
					thicknessDivider->x.SetTar(BarDrawAttributeGap * layoutScale);
					thicknessDivider->y.SetTar(
						(thicknessY + thicknessDividerOffsetY) * layoutScale);
					thicknessDivider->w.SetTar(
						BarDrawAttributeThicknessDividerWidth * layoutScale);
					thicknessDivider->h.SetTar(BarUiDividerWidth * layoutScale);
					thicknessDivider->rw->SetTar(BarUiDividerRadius * layoutScale);
					thicknessDivider->rh->SetTar(BarUiDividerRadius * layoutScale);
					thicknessDivider->ft->SetTar(layoutScale);
					thicknessDivider->fill->SetTar(
						GetThemeColor(BarThemeColorEnum::SurfaceFrame));
					thicknessDivider->frame->SetTar(
						GetThemeColor(BarThemeColorEnum::SurfaceFrame));
					thicknessDivider->pct.SetTar(state.barState.drawAttribute ? 0.30 : 0.0);
					thicknessDivider->framePct->SetTar(0.0);
					thicknessDivider->frameLightPct->SetTar(
						state.barState.drawAttribute ? 1.0 : 0.0);

					auto thicknessDisplay =
						state.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay];
					thicknessDisplay->x.SetTar(
						(BarDrawAttributeGap
							+ BarDrawAttributeThicknessContentInset)
						* layoutScale);
					thicknessDisplay->y.SetTar(
						(thicknessY + thicknessControlOffsetY) * layoutScale);
					thicknessDisplay->w.SetTar(90.0 * layoutScale);
					thicknessDisplay->h.SetTar(30.0 * layoutScale);
					thicknessDisplay->size.SetTar(13.0 * layoutScale);
					thicknessDisplay->pct.SetTar(state.barState.drawAttribute ? 1.0 : 0.0);
					thicknessDisplay->color.SetTar(
						GetThemeColor(BarThemeColorEnum::TextPrimary));

			double thicknessControlOpacity = clamp(
				1.0 - static_cast<double>(
					state.drawAttributeThicknessHoldExchangeProgress.val),
				0.0, 1.0);
			bool thicknessControlsExchangeDirect =
				state.drawAttributeThicknessHoldExchangeProgress.val > 0.000001
				|| state.drawAttributeThicknessHoldExchangeProgress.tar > 0.000001;
bool thicknessPresetMode =
							PenModeUsesThicknessPresets(stateMode.Pen.ModeSelect);
						// 预设选中只看真实粗细；左下角数字由动画值驱动。
						int actualThickness = static_cast<int>(lround(clamp(
							static_cast<double>(max(0.0f, GetPenWidth())),
							0.0, 999.0)));
						auto ConfigureThicknessButton = [&](BarUISetShapeEnum shapeType,
							shared_ptr<BarUiWordClass> numberWord, double x, bool visible,
							bool selected, bool pressed,
							IdtAtomic<BarButtonHoverStageEnum>& hoverStage,
							BarUiValueClass& pressScale)
							{
							auto shape = state.shapeMap[shapeType];
							shape->x.SetTar(x * layoutScale);
							shape->y.SetTar(
								(thicknessY + thicknessControlOffsetY) * layoutScale);
							shape->w.SetTar(
								BarDrawAttributeThicknessControlHeight * layoutScale);
							shape->h.SetTar(
								BarDrawAttributeThicknessControlHeight * layoutScale);
							shape->rw.value().SetTar(4.0 * layoutScale);
							shape->rh.value().SetTar(4.0 * layoutScale);
							shape->fill.value().SetTar(selected
								? GetThemeColor(BarThemeColorEnum::Accent)
								: GetThemeColor(BarThemeColorEnum::PressedFill));
							shape->frame.value().SetTar(selected
								? GetThemeColor(BarThemeColorEnum::Accent)
								: GetThemeColor(BarThemeColorEnum::TextPrimary));

							if (!visible)
							{
								if (thicknessControlsExchangeDirect)
								{
									shape->pct.SetDirect(0.0);
									shape->frameLightPct.value().SetDirect(0.0);
									if (numberWord) numberWord->pct.SetDirect(0.0);
								}
								else
								{
									shape->pct.SetTar(0.0);
									shape->frameLightPct.value().SetTar(0.0);
									if (numberWord) numberWord->pct.SetTar(0.0);
								}
							}
							else
							{
								double shapeOpacity = pressed ? 0.10
									: (selected ? 0.20 : 0.0);
								if (thicknessControlsExchangeDirect)
									shape->pct.SetDirect(
										shapeOpacity * thicknessControlOpacity);
								else if (pressed) shape->pct.SetTar(0.10);
								else if (selected) shape->pct.SetTar(0.20);
								else if (hoverStage == BarButtonHoverStageEnum::None
									|| thicknessControlOpacity < 0.999999)
									shape->pct.SetTar(0.0);
								double frameLightOpacity = selected
									? (pressed ? BarButtonPressedLightOpacity : 1.0)
										* thicknessControlOpacity
									: 0.0;
								if (thicknessControlsExchangeDirect)
									shape->frameLightPct.value().SetDirect(
										frameLightOpacity);
								else shape->frameLightPct.value().SetTar(
									frameLightOpacity);
								if (numberWord)
								{
									if (thicknessControlsExchangeDirect)
										numberWord->pct.SetDirect(
											thicknessControlOpacity);
									else numberWord->pct.SetTar(1.0);
								}
							}

							if (numberWord)
							{
								numberWord->x.SetTar(x * layoutScale);
								numberWord->y.SetTar(
									(thicknessY + thicknessControlOffsetY) * layoutScale);
								numberWord->w.SetTar(
									BarDrawAttributeThicknessControlHeight * layoutScale);
								numberWord->h.SetTar(
									BarDrawAttributeThicknessControlHeight * layoutScale);
								numberWord->size.SetTar(10.0 * layoutScale);
							}
							pressScale.SetTar(pressed ? BarButtonPressScale : 1.0,
								BarUiDefaultOperationDur, nullopt, false,
								pressed ? state.buttonPressCurve : state.buttonReleaseCurve);
						};

					const BarUISetShapeEnum presetShapes[] =
					{
						BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
						BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
						BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
					};
					const BarUISetWordEnum presetWords[] =
					{
						BarUISetWordEnum::DrawAttributeBar_ThicknessFineNumber,
						BarUISetWordEnum::DrawAttributeBar_ThicknessMediumNumber,
						BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber,
					};
					IdtAtomic<bool>* presetPresses[] =
					{
						&state.barState.drawAttributeBar.thicknessFinePress,
						&state.barState.drawAttributeBar.thicknessMediumPress,
						&state.barState.drawAttributeBar.thicknessCoarsePress,
					};
					IdtAtomic<BarButtonHoverStageEnum>* presetHoverStages[] =
					{
						&state.drawAttributeThicknessFineHoverStage,
						&state.drawAttributeThicknessMediumHoverStage,
						&state.drawAttributeThicknessCoarseHoverStage,
					};
					BarUiValueClass* presetPressScales[] =
					{
						&state.drawAttributeThicknessFinePressScale,
						&state.drawAttributeThicknessMediumPressScale,
						&state.drawAttributeThicknessCoarsePressScale,
					};
for (size_t i = 0; i < 3; ++i)
						{
							int presetPx = GetBarThicknessPresetPx(
								stateMode.Pen.ModeSelect, i, state.barStyle.dpiZoom);
							auto numberWord = state.wordMap[presetWords[i]];
							wstring numberText = to_wstring(presetPx);
							numberWord->content.SetTar(numberText);
							ConfigureThicknessButton(presetShapes[i], numberWord,
								BarDrawAttributeThicknessPresetStartX
									+ static_cast<double>(i)
										* (BarDrawAttributeThicknessControlHeight
											+ BarDrawAttributeGap),
								state.barState.drawAttribute && thicknessPresetMode,
								actualThickness == presetPx, *presetPresses[i],
								*presetHoverStages[i], *presetPressScales[i]);
						}
						bool adjustVisible = state.barState.drawAttribute
							&& thicknessPresetMode;
					ConfigureThicknessButton(
						BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust, nullptr,
						BarDrawAttributeThicknessAdjustX, adjustVisible,
						state.barState.drawAttributeBar.thicknessViewMode
							!= ThicknessViewMode::Preview,
						state.barState.drawAttributeBar.thicknessAdjustPress,
						state.drawAttributeThicknessAdjustHoverStage,
						state.drawAttributeThicknessAdjustPressScale);
					auto thicknessAdjustSvg =
						state.svgMap[BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust];
					thicknessAdjustSvg->x.SetTar(0.0);
					thicknessAdjustSvg->y.SetTar(0.0);
					thicknessAdjustSvg->w.SetTar(18.0 * layoutScale);
					thicknessAdjustSvg->h.SetTar(18.0 * layoutScale);
					if (thicknessControlsExchangeDirect)
						thicknessAdjustSvg->pct.SetDirect(
							adjustVisible ? thicknessControlOpacity : 0.0);
					else thicknessAdjustSvg->pct.SetTar(
						adjustVisible ? 1.0 : 0.0);
					auto& thicknessAdjustColor =
						thicknessAdjustSvg->color1.value();
							// 三角按下仍保留选中态基色，按压反馈由背景和光影透明度表达。
							COLORREF thicknessAdjustTargetColor =
								state.barState.drawAttributeBar.thicknessViewMode
									!= ThicknessViewMode::Preview
									? GetThemeColor(BarThemeColorEnum::Accent)
									: GetThemeColor(BarThemeColorEnum::TextPrimary);
					if (forNum == 1 || !state.barState.drawAttribute)
						thicknessAdjustColor.SetDirect(thicknessAdjustTargetColor);
					else thicknessAdjustColor.SetTar(thicknessAdjustTargetColor);

					bool tooltipBaseVisible =
						state.barState.drawAttribute && !state.barState.fold;
					bool annotationSupported = tooltipBaseVisible
						&& state.barState.drawAttributeBar.penTypeMenuOpen
						&& PenModeSupportsAnnotationLine(
							stateMode.Pen.ModeSelect);
					double expandedPreviewCapacity =
						(BarDrawAttributeThicknessHeight
							- BarDrawAttributeThicknessControlHeight
							- BarUiDividerWidth
							- BarDrawAttributeGap * 2.0)
						* max(0.0, static_cast<double>(frameZoom));
					bool previewOverflow = tooltipBaseVisible
						&& static_cast<double>(GetPenWidth())
							> expandedPreviewCapacity + 0.001;
					state.barState.drawAttributeBar.thicknessPreviewOverflow =
						previewOverflow;
					bool fineDialVisualPresent =
						state.barState.drawAttributeBar.thicknessViewMode
							== ThicknessViewMode::FineDial
						|| state.drawAttributeThicknessFineDialProgress.val > 0.000001
						|| state.drawAttributeThicknessFineDialProgress.tar > 0.000001;
					if (!previewOverflow)
					{
						// overflowPossible=false 时任何阶段都立即撤销 Hint 与命中。
						state.barState.drawAttributeBar.thicknessOverflowHintPresent = false;
						state.drawAttributeOverflowSliderSessionAllowsHint = false;
						CloseThicknessOverflowTooltip();
					}
					else if (fineDialVisualPresent)
					{
						// Dial 退场完成前不重建 Overflow；业务 overflow 标记保持不变。
						state.barState.drawAttributeBar.thicknessOverflowHintPresent = false;
						CloseThicknessOverflowTooltip();
					}
					else if (!state.drawAttributeThicknessSliderTargetActive)
					{
						// Preview 与 Slider -> Preview 恢复阶段允许按当前 overflow 创建 Hint。
						state.barState.drawAttributeBar.thicknessOverflowHintPresent = true;
						// 恢复阶段产生的 Hint 要允许快速反转时沿用，
						// 但同一 Slider 会话中消失后仍不得重新创建。
						if (state.drawAttributeThicknessSliderPositionLocked)
							state.drawAttributeOverflowSliderSessionAllowsHint = true;
					}
					else if (!state.drawAttributeOverflowSliderSessionAllowsHint)
					{
						// Slider session 内未带入 Hint 时，新产生的 overflow 不能创建 Hint。
						state.barState.drawAttributeBar.thicknessOverflowHintPresent = false;
						CloseThicknessOverflowTooltip();
					}

					if (!tooltipBaseVisible)
					{
						ClosePenTypeMenu();
						CloseThicknessOverflowTooltip();
					}
					if (!annotationSupported) CloseAnnotationTooltip();
					SetPopupProgress(state.drawAttributeAnnotationPopupProgress,
						annotationSupported
						&& (state.barState.drawAttributeBar.thicknessAnnotationHover
							|| state.barState.drawAttributeBar.thicknessAnnotationPinned));
					SetPopupProgress(state.drawAttributeOverflowPopupProgress,
						state.barState.drawAttributeBar.thicknessOverflowHintPresent
						&& (state.barState.drawAttributeBar.thicknessOverflowHover
							|| state.barState.drawAttributeBar.thicknessOverflowPinned));
					SetPopupProgress(state.drawAttributeOverflowBadgeProgress,
						state.barState.drawAttributeBar.thicknessOverflowHintPresent);

					auto freeLineRow = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine];
					freeLineRow->fill->SetTar(
						GetThemeColor(BarThemeColorEnum::PressedFill), operationDur);
					if (!state.barState.drawAttributeBar.penTypeMenuOpen)
						freeLineRow->pct.SetTar(0.0);
					else if (state.barState.drawAttributeBar.penTypeFreeLinePress)
						freeLineRow->pct.SetTar(0.10);
					else if (state.drawAttributePenTypeFreeLineHoverStage
						== BarButtonHoverStageEnum::None)
						freeLineRow->pct.SetTar(0.0);
					state.drawAttributePenTypeFreeLinePressScale.SetTar(
						state.barState.drawAttributeBar.penTypeFreeLinePress
							? BarButtonPressScale : 1.0,
						BarUiDefaultOperationDur, nullopt, false,
						state.barState.drawAttributeBar.penTypeFreeLinePress
							? state.buttonPressCurve : state.buttonReleaseCurve);
					state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuAnnotationLine]
						->pct.SetTar(0.0);

					auto ConfigureTooltipCloseButton =
						[&](BarUISetShapeEnum shapeType, bool pinned,
							bool pressed,
							IdtAtomic<BarButtonHoverStageEnum>& hoverStage,
							BarUiValueClass& pressScale)
						{
							auto closeButton = state.shapeMap[shapeType];
							closeButton->fill.value().SetTar(
								GetThemeColor(BarThemeColorEnum::PressedFill),
								operationDur);
							if (!pinned) closeButton->pct.SetTar(0.0);
							else if (pressed) closeButton->pct.SetTar(0.10);
							else if (hoverStage == BarButtonHoverStageEnum::None)
								closeButton->pct.SetTar(0.0);
							pressScale.SetTar(
								pressed ? BarButtonPressScale : 1.0,
								BarUiDefaultOperationDur, nullopt, false,
								pressed ? state.buttonPressCurve : state.buttonReleaseCurve);
						};
					ConfigureTooltipCloseButton(
						BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
						state.barState.drawAttributeBar.thicknessAnnotationPinned,
						state.barState.drawAttributeBar.thicknessAnnotationClosePress,
						state.drawAttributeAnnotationCloseHoverStage,
						state.drawAttributeAnnotationClosePressScale);
					ConfigureTooltipCloseButton(
						BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
						state.barState.drawAttributeBar.thicknessOverflowPinned,
						state.barState.drawAttributeBar.thicknessOverflowClosePress,
						state.drawAttributeOverflowCloseHoverStage,
						state.drawAttributeOverflowClosePressScale);

					const BarUISetShapeEnum tooltipSurfaces[] =
					{
						BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge,
						BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
						BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
						BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu,
					};
					for (auto shapeType : tooltipSurfaces)
					{
						auto surface = state.shapeMap[shapeType];
						surface->fill.value().SetTar(
							GetThemeColor(BarThemeColorEnum::Surface),
							operationDur);
						surface->frame.value().SetTar(
							GetThemeColor(BarThemeColorEnum::SurfaceFrame),
							operationDur);
					}
					state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_PenTypeMenuFreeLine]
						->color.SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary), operationDur);
					state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText]
						->color.SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							operationDur);
					COLORREF popupBodyColor = MixBarUiColor(
						GetThemeColor(BarThemeColorEnum::TextPrimary),
						GetThemeColor(BarThemeColorEnum::Surface), 0.45);
					state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody]
						->color.SetTar(popupBodyColor, operationDur);
					state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText]
						->color.SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							operationDur);
					state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody]
						->color.SetTar(popupBodyColor, operationDur);
					state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_PenTypeMenuCheck]
						->color1.value().SetTar(
							GetThemeColor(BarThemeColorEnum::Accent), operationDur);
					state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo]
						->color1.value().SetTar(
							RGB(200, 200, 200), operationDur);
					state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo]
						->color1.value().SetTar(
							RGB(255, 255, 255), operationDur);
					state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose]
						->color1.value().SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							operationDur);
					state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose]
						->color1.value().SetTar(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							operationDur);
				}

				// 颜色块在收起状态保留缩小后的相对排布，展开时同时恢复坐标和尺寸。
				for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
					i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect11); i++)
				{
					auto shape = state.shapeMap[static_cast<BarUISetShapeEnum>(i)];
					double size = state.barState.drawAttribute ? 30.0 : CompactDrawAttributeSize(30.0);
					shape->w.SetTar(size);
					shape->h.SetTar(size);
					shape->rw.value().SetTar(4.0 * drawAttributeLayoutScale);
					shape->rh.value().SetTar(4.0 * drawAttributeLayoutScale);
					shape->ft.value().SetTar(drawAttributeLayoutScale);
					// 填充先显现，灰边只随同一批次淡入到 18%。
					shape->framePct.value().SetTar(
						state.barState.drawAttribute ? BarColorSwatchFrameOpacity : 0.0);

					auto svg = state.svgMap[static_cast<BarUISetSvgEnum>(
						static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1)
						+ i - static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1))];
					double svgSize = state.barState.drawAttribute ? 15.0 : CompactDrawAttributeSize(15.0);
					svg->w.SetTar(svgSize);
					svg->h.SetTar(svgSize);
				}
				{
					double customScale = state.barState.drawAttribute
						? 1.0 : BarDrawAttributeCompactScale;
					auto customSwatch = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorSelect12];
					customSwatch->w.SetTar(30.0 * customScale);
					customSwatch->h.SetTar(30.0 * customScale);
					customSwatch->rw->SetTar(15.0 * customScale);
					customSwatch->rh->SetTar(15.0 * customScale);
					customSwatch->ft->SetTar(customScale);

					auto customInner = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner];
					// 色芯为外面彩色圆盘的 2/3 大（30×30 → 20×20）。
					customInner->w.SetTar(20.0 * customScale);
					customInner->h.SetTar(20.0 * customScale);
					customInner->rw->SetTar(10.0 * customScale);
					customInner->rh->SetTar(10.0 * customScale);
					customInner->ft->SetTar(customScale);

					auto customWheel = state.pngMap[
						BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel];
					customWheel->w.SetTar(30.0 * customScale);
					customWheel->h.SetTar(30.0 * customScale);
					auto customCheck = state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check];
					customCheck->w.SetTar(15.0 * customScale);
					customCheck->h.SetTar(15.0 * customScale);
				}

				// 展开、收起时，属性栏及全部内部控件共用同一个完成时刻。
				for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar);
					i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust); i++)
				{
					auto obj = state.shapeMap[static_cast<BarUISetShapeEnum>(i)];
					if (!obj) continue;
					SyncValueDuration(obj->x);
					SyncValueDuration(obj->y);
					SyncValueDuration(obj->w);
					SyncValueDuration(obj->h);
					if (obj->rw.has_value()) SyncValueDuration(obj->rw.value());
					if (obj->rh.has_value()) SyncValueDuration(obj->rh.value());
					if (obj->ft.has_value()) SyncValueDuration(obj->ft.value());
					SyncPctDuration(obj->pct);
					if (obj->framePct.has_value()) SyncPctDuration(obj->framePct.value());
					if (obj->frameLightPct.has_value()) SyncPctDuration(obj->frameLightPct.value());
				}
				for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
					i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust); i++)
				{
					auto obj = state.svgMap[static_cast<BarUISetSvgEnum>(i)];
					if (!obj) continue;
					SyncValueDuration(obj->x);
					SyncValueDuration(obj->y);
					SyncValueDuration(obj->w);
					SyncValueDuration(obj->h);
					SyncPctDuration(obj->pct);
				}
				{
					auto obj = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner];
					SyncValueDuration(obj->x);
					SyncValueDuration(obj->y);
					SyncValueDuration(obj->w);
					SyncValueDuration(obj->h);
					SyncValueDuration(obj->rw.value());
					SyncValueDuration(obj->rh.value());
					SyncValueDuration(obj->ft.value());
					SyncPctDuration(obj->pct);
					SyncPctDuration(obj->framePct.value());
					auto png = state.pngMap[
						BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel];
					SyncValueDuration(png->x);
					SyncValueDuration(png->y);
					SyncValueDuration(png->w);
					SyncValueDuration(png->h);
					SyncValueDuration(png->angle);
					SyncPctDuration(png->pct);
					auto svg = state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check];
					SyncValueDuration(svg->x);
					SyncValueDuration(svg->y);
					SyncValueDuration(svg->w);
					SyncValueDuration(svg->h);
					SyncValueDuration(svg->angle);
					SyncPctDuration(svg->pct);
				}
				for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
					i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber); i++)
				{
					auto obj = state.wordMap[static_cast<BarUISetWordEnum>(i)];
					if (!obj) continue;
					SyncValueDuration(obj->x);
					SyncValueDuration(obj->y);
					SyncValueDuration(obj->w);
					SyncValueDuration(obj->h);
					SyncValueDuration(obj->size);
					SyncPctDuration(obj->pct);
				}

				if (drawAttributeVisibilityChange)
				{
					// 每次展开或收起都从当前值重建，所有子控件共用本批次的同一结束时刻。
					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar);
						i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust); i++)
					{
						auto obj = state.shapeMap[static_cast<BarUISetShapeEnum>(i)];
						if (!obj) continue;
						obj->x.SetTar(obj->x.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur, nullopt, true, syncedValueCurve);
						if (obj->rw.has_value()) obj->rw.value().SetTar(obj->rw.value().tar, operationDur, nullopt, true, syncedValueCurve);
						if (obj->rh.has_value()) obj->rh.value().SetTar(obj->rh.value().tar, operationDur, nullopt, true, syncedValueCurve);
						if (obj->ft.has_value()) obj->ft.value().SetTar(obj->ft.value().tar, operationDur, nullopt, true, syncedValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, nullopt, true, syncedPctCurve);
						if (obj->framePct.has_value())
							obj->framePct.value().SetTar(obj->framePct.value().tar, operationDur, nullopt, true, syncedPctCurve);
						if (obj->frameLightPct.has_value())
							obj->frameLightPct.value().SetTar(obj->frameLightPct.value().tar, operationDur, nullopt, true, syncedPctCurve);
					}
					for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust); i++)
					{
						auto obj = state.svgMap[static_cast<BarUISetSvgEnum>(i)];
						if (!obj) continue;
						obj->x.SetTar(obj->x.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, nullopt, true, syncedPctCurve);
					}
					{
						auto obj = state.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner];
						obj->x.SetTar(obj->x.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->rw->SetTar(obj->rw->tar, operationDur, nullopt, true, syncedValueCurve);
						obj->rh->SetTar(obj->rh->tar, operationDur, nullopt, true, syncedValueCurve);
						obj->ft->SetTar(obj->ft->tar, operationDur, nullopt, true, syncedValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, nullopt, true, syncedPctCurve);
						obj->framePct->SetTar(obj->framePct->tar, operationDur,
							nullopt, true, syncedPctCurve);
						auto png = state.pngMap[
							BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel];
						png->x.SetTar(png->x.tar, operationDur, nullopt, true, syncedValueCurve);
						png->y.SetTar(png->y.tar, operationDur, nullopt, true, syncedValueCurve);
						png->w.SetTar(png->w.tar, operationDur, nullopt, true, syncedValueCurve);
						png->h.SetTar(png->h.tar, operationDur, nullopt, true, syncedValueCurve);
						png->angle.SetTar(png->angle.tar, operationDur, nullopt, true, syncedValueCurve);
						png->pct.SetTar(png->pct.tar, operationDur, nullopt, true, syncedPctCurve);
						auto svg = state.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check];
						svg->x.SetTar(svg->x.tar, operationDur, nullopt, true, syncedValueCurve);
						svg->y.SetTar(svg->y.tar, operationDur, nullopt, true, syncedValueCurve);
						svg->w.SetTar(svg->w.tar, operationDur, nullopt, true, syncedValueCurve);
						svg->h.SetTar(svg->h.tar, operationDur, nullopt, true, syncedValueCurve);
						svg->angle.SetTar(svg->angle.tar, operationDur, nullopt, true, syncedValueCurve);
						svg->pct.SetTar(svg->pct.tar, operationDur, nullopt, true, syncedPctCurve);
					}
					for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
						i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber); i++)
					{
						auto obj = state.wordMap[static_cast<BarUISetWordEnum>(i)];
						if (!obj) continue;
						obj->x.SetTar(obj->x.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->size.SetTar(obj->size.tar, operationDur, nullopt, true, syncedValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, nullopt, true, syncedPctCurve);
					}
				}

				if (drawAttributeSideSwitch)
				{
					// 所有隐藏几何都约束在 60×30 面板中心，不读取仍在动画中的 val。
					drawAttributeBar->x.SetTar(
						drawAttributeBar->x.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
					drawAttributeBar->y.SetTar(
						drawAttributeBar->y.tar, operationDur, 0.0, true, drawAttributeKeyframeValueCurve);
					drawAttributeBar->w.SetTar(
						drawAttributeBar->w.tar, operationDur, BarDrawAttributeCompactWidth, true,
						drawAttributeKeyframeValueCurve);
					drawAttributeBar->h.SetTar(
						drawAttributeBar->h.tar, operationDur, BarDrawAttributeCompactHeight, true,
						drawAttributeKeyframeValueCurve);
					drawAttributeBar->rw.value().SetTar(
						drawAttributeBar->rw.value().tar, operationDur,
						CompactDrawAttributeSize(drawAttributeBar->rw.value().tar),
						true, drawAttributeKeyframeValueCurve);
					drawAttributeBar->rh.value().SetTar(
						drawAttributeBar->rh.value().tar, operationDur,
						CompactDrawAttributeSize(drawAttributeBar->rh.value().tar),
						true, drawAttributeKeyframeValueCurve);
					drawAttributeBar->ft.value().SetTar(
						drawAttributeBar->ft.value().tar, operationDur,
						CompactDrawAttributeSize(drawAttributeBar->ft.value().tar),
						true, drawAttributeKeyframeValueCurve);
					drawAttributeBar->pct.SetTar(
						drawAttributeBar->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
					drawAttributeBar->framePct.value().SetTar(
						drawAttributeBar->framePct.value().tar, operationDur, 0.0, true,
						drawAttributeKeyframePctCurve);

					for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust); i++)
					{
						auto obj = state.shapeMap[static_cast<BarUISetShapeEnum>(i)];
						if (!obj) continue;
						double middleW = max(1.0, CompactDrawAttributeSize(obj->w.tar));
						double middleH = max(1.0, CompactDrawAttributeSize(obj->h.tar));
						// 根面板负责对齐绘制按钮；子控件保留完整布局的等比微缩坐标。
						double middleX = CompactDrawAttributeX(obj->x.tar);
						double middleY = CompactDrawAttributeY(obj->y.tar);
						obj->x.SetTar(obj->x.tar, operationDur, middleX, true, drawAttributeKeyframeValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur, middleY, true, drawAttributeKeyframeValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur, middleW, true, drawAttributeKeyframeValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur, middleH, true, drawAttributeKeyframeValueCurve);
						if (obj->rw.has_value())
							obj->rw.value().SetTar(obj->rw.value().tar, operationDur,
								CompactDrawAttributeSize(obj->rw.value().tar), true,
								drawAttributeKeyframeValueCurve);
						if (obj->rh.has_value())
							obj->rh.value().SetTar(obj->rh.value().tar, operationDur,
								CompactDrawAttributeSize(obj->rh.value().tar), true,
								drawAttributeKeyframeValueCurve);
						if (obj->ft.has_value())
							obj->ft.value().SetTar(obj->ft.value().tar, operationDur,
								CompactDrawAttributeSize(obj->ft.value().tar), true,
								drawAttributeKeyframeValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
						if (obj->framePct.has_value())
							obj->framePct.value().SetTar(obj->framePct.value().tar, operationDur, 0.0, true,
								drawAttributeKeyframePctCurve);
						if (obj->frameLightPct.has_value())
							obj->frameLightPct.value().SetTar(obj->frameLightPct.value().tar, operationDur, 0.0, true,
								drawAttributeKeyframePctCurve);
					}
					for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
						i <= static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust); i++)
					{
						auto obj = state.svgMap[static_cast<BarUISetSvgEnum>(i)];
						if (!obj) continue;
						obj->x.SetTar(obj->x.tar, operationDur,
							CompactDrawAttributeX(obj->x.tar), true, drawAttributeKeyframeValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur,
							CompactDrawAttributeY(obj->y.tar), true, drawAttributeKeyframeValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur,
							max(1.0, CompactDrawAttributeSize(obj->w.tar)), true, drawAttributeKeyframeValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur,
							max(1.0, CompactDrawAttributeSize(obj->h.tar)), true, drawAttributeKeyframeValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
					}
					{
						auto obj = state.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner];
						obj->x.SetTar(obj->x.tar, operationDur,
							obj->x.tar * BarDrawAttributeCompactScale,
							true, drawAttributeKeyframeValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur,
							obj->y.tar * BarDrawAttributeCompactScale,
							true, drawAttributeKeyframeValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur,
							max(1.0, obj->w.tar * BarDrawAttributeCompactScale),
							true, drawAttributeKeyframeValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur,
							max(1.0, obj->h.tar * BarDrawAttributeCompactScale),
							true, drawAttributeKeyframeValueCurve);
						obj->rw->SetTar(obj->rw->tar, operationDur,
							obj->rw->tar * BarDrawAttributeCompactScale,
							true, drawAttributeKeyframeValueCurve);
						obj->rh->SetTar(obj->rh->tar, operationDur,
							obj->rh->tar * BarDrawAttributeCompactScale,
							true, drawAttributeKeyframeValueCurve);
						obj->ft->SetTar(obj->ft->tar, operationDur,
							obj->ft->tar * BarDrawAttributeCompactScale,
							true, drawAttributeKeyframeValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, 0.0,
							true, drawAttributeKeyframePctCurve);
						obj->framePct->SetTar(obj->framePct->tar, operationDur, 0.0,
							true, drawAttributeKeyframePctCurve);
						auto SetCustomContentKeyframe = [&](auto& content)
							{
								content->x.SetTar(content->x.tar, operationDur,
									content->x.tar * BarDrawAttributeCompactScale,
									true, drawAttributeKeyframeValueCurve);
								content->y.SetTar(content->y.tar, operationDur,
									content->y.tar * BarDrawAttributeCompactScale,
									true, drawAttributeKeyframeValueCurve);
								content->w.SetTar(content->w.tar, operationDur,
									max(1.0, content->w.tar * BarDrawAttributeCompactScale),
									true, drawAttributeKeyframeValueCurve);
								content->h.SetTar(content->h.tar, operationDur,
									max(1.0, content->h.tar * BarDrawAttributeCompactScale),
									true, drawAttributeKeyframeValueCurve);
								content->pct.SetTar(content->pct.tar, operationDur, 0.0,
									true, drawAttributeKeyframePctCurve);
							};
						SetCustomContentKeyframe(state.pngMap[
							BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel]);
						SetCustomContentKeyframe(state.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check]);
					}
					for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
						i <= static_cast<int>(BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber); i++)
					{
						auto obj = state.wordMap[static_cast<BarUISetWordEnum>(i)];
						if (!obj) continue;
						obj->x.SetTar(obj->x.tar, operationDur,
							CompactDrawAttributeX(obj->x.tar), true, drawAttributeKeyframeValueCurve);
						obj->y.SetTar(obj->y.tar, operationDur,
							CompactDrawAttributeY(obj->y.tar), true, drawAttributeKeyframeValueCurve);
						obj->w.SetTar(obj->w.tar, operationDur,
							max(1.0, CompactDrawAttributeSize(obj->w.tar)), true, drawAttributeKeyframeValueCurve);
						obj->h.SetTar(obj->h.tar, operationDur,
							max(1.0, CompactDrawAttributeSize(obj->h.tar)), true, drawAttributeKeyframeValueCurve);
						obj->size.SetTar(obj->size.tar, operationDur,
							max(1.0, CompactDrawAttributeSize(obj->size.tar)), true, drawAttributeKeyframeValueCurve);
						obj->pct.SetTar(obj->pct.tar, operationDur, 0.0, true, drawAttributeKeyframePctCurve);
					}
					state.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay]->color.SetTar(
						GetThemeColor(BarThemeColorEnum::TextPrimary), operationDur);
				}
			}

			// 几何属性使用独立批次，但允许在主栏动画前半段加入同一截止时间。
			{
				bool batchChange = geometryAttributeVisibilityChange
					|| geometryAttributeSideSwitch;
				operationDur = BarUiDefaultOperationDur;
				double phase = 0.0;
				bool continuePhase = false;
				if (batchChange)
				{
					if (state.mainBarTimeline.CanJoin())
					{
						operationDur = state.mainBarTimeline.GetRemainingDuration();
						phase = state.mainBarTimeline.GetProgress();
						continuePhase = phase > 0.0;
					}
					state.geometryAttributeTimeline.Restart(operationDur);
				}
				else if (state.geometryAttributeTimeline.IsActive())
				{
					operationDur = state.geometryAttributeTimeline.GetRemainingDuration();
					phase = state.geometryAttributeTimeline.GetProgress();
					continuePhase = phase > 0.0;
				}
				syncValueCurveFromBatch = state.geometryAttributeTimeline.IsActive();
				BarUiCurveEnum valueCurve = state.geometryAttributeTimeline.IsActive()
					? (state.barState.geometryAttribute
						? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack)
					: BarUiCurveEnum::EaseInOutCubic;
				BarUiCurveEnum pctCurve = state.geometryAttributeTimeline.IsActive()
					&& !state.barState.geometryAttribute
					? BarUiCurveEnum::EaseInSine : BarUiCurveEnum::EaseOutSine;
				syncedValueCurve = { valueCurve, valueCurve, phase, continuePhase };
				syncedPctCurve = { pctCurve, pctCurve, phase, continuePhase };
				const BarUiCurveSpecClass sideValueCurve{
					BarUiCurveEnum::EaseInCubic, BarUiCurveEnum::EaseOutBack,
					phase, continuePhase };
				const BarUiCurveSpecClass sidePctCurve{
					BarUiCurveEnum::EaseOutSine, BarUiCurveEnum::EaseOutSine,
					phase, continuePhase };
				auto Compact = [](double value)
					{
						return value * BarGeometryAttributeCompactScale;
					};
				double layoutScale = state.barState.geometryAttribute
					? 1.0 : BarGeometryAttributeCompactScale;
				auto panel = state.shapeMap[BarUISetShapeEnum::GeometryAttributeBar];
				panel->x.SetTar(0.0);
				panel->y.SetTar(state.barState.geometryAttribute
					? (state.barState.widgetPosition.primaryBar
						? mainBar->GetH() / 2.0
							+ BarGeometryAttributeExpandedHeight / 2.0 + 10.0
						: -(mainBar->GetH() / 2.0
							+ BarGeometryAttributeExpandedHeight / 2.0 + 10.0))
					: 0.0);
				panel->w.SetTar(state.barState.geometryAttribute
					? BarGeometryAttributeExpandedWidth
					: BarGeometryAttributeCompactWidth);
				panel->h.SetTar(state.barState.geometryAttribute
					? BarGeometryAttributeExpandedHeight
					: BarGeometryAttributeCompactHeight);
				panel->rw->SetTar(8.0 * layoutScale);
				panel->rh->SetTar(8.0 * layoutScale);
				panel->ft->SetTar(layoutScale);
				panel->pct.SetTar(state.barState.geometryAttribute
					? BarDrawAttributeSurfaceOpacity : 0.0);
				panel->framePct->SetTar(state.barState.geometryAttribute ? 0.18 : 0.0);
				panel->fill->SetTar(GetThemeColor(BarThemeColorEnum::Surface));
				panel->frame->SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));

				struct GeometryButtonLayout
				{
					BarUISetShapeEnum shape;
					BarUISetWordEnum word;
					double x;
					double y;
					double size;
					bool label;
					bool selected;
					bool pressed;
					IdtAtomic<BarButtonHoverStageEnum>* hoverStage;
					BarUiValueClass* pressScale;
				};
				int brushWidth = static_cast<int>(lround(max(
					0.0f, stateMode.Pen.Brush1.width)));
				const GeometryButtonLayout buttons[] =
				{
					{ BarUISetShapeEnum::GeometryAttributeBar_StraightLine,
						BarUISetWordEnum::GeometryAttributeBar_StraightLine,
						5.0, 5.0, 50.0, true,
						stateMode.Shape.ModeSelect
							== ShapeModeSelectEnum::IdtShapeStraightLine1,
						state.barState.geometryAttributeBar.straightLinePress,
						&state.geometryStraightLineHoverStage,
						&state.geometryStraightLinePressScale },
					{ BarUISetShapeEnum::GeometryAttributeBar_Rectangle,
						BarUISetWordEnum::GeometryAttributeBar_Rectangle,
						60.0, 5.0, 50.0, true,
						stateMode.Shape.ModeSelect
							== ShapeModeSelectEnum::IdtShapeRectangle1,
						state.barState.geometryAttributeBar.rectanglePress,
						&state.geometryRectangleHoverStage,
						&state.geometryRectanglePressScale },
					{ BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine,
						BarUISetWordEnum::GeometryAttributeBar_ThicknessFineNumber,
						230.0, 65.0, 30.0, false,
						brushWidth == GetBarThicknessPresetPx(
							PenModeSelectEnum::IdtPenBrush1, 0, state.barStyle.dpiZoom),
						state.barState.geometryAttributeBar.thicknessFinePress,
						&state.geometryThicknessFineHoverStage,
						&state.geometryThicknessFinePressScale },
					{ BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium,
						BarUISetWordEnum::GeometryAttributeBar_ThicknessMediumNumber,
						265.0, 65.0, 30.0, false,
						brushWidth == GetBarThicknessPresetPx(
							PenModeSelectEnum::IdtPenBrush1, 1, state.barStyle.dpiZoom),
						state.barState.geometryAttributeBar.thicknessMediumPress,
						&state.geometryThicknessMediumHoverStage,
						&state.geometryThicknessMediumPressScale },
					{ BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse,
						BarUISetWordEnum::GeometryAttributeBar_ThicknessCoarseNumber,
						300.0, 65.0, 30.0, false,
						brushWidth == GetBarThicknessPresetPx(
							PenModeSelectEnum::IdtPenBrush1, 2, state.barStyle.dpiZoom),
						state.barState.geometryAttributeBar.thicknessCoarsePress,
						&state.geometryThicknessCoarseHoverStage,
						&state.geometryThicknessCoarsePressScale },
				};
				for (size_t index = 0; index < size(buttons); ++index)
				{
					const auto& button = buttons[index];
					auto shape = state.shapeMap[button.shape];
					auto word = state.wordMap[button.word];
					shape->x.SetTar(button.x * layoutScale);
					shape->y.SetTar(button.y * layoutScale);
					shape->w.SetTar(button.size * layoutScale);
					shape->h.SetTar(button.size * layoutScale);
					shape->rw->SetTar(4.0 * layoutScale);
					shape->rh->SetTar(4.0 * layoutScale);
					shape->ft->SetTar(layoutScale);
					shape->fill->SetTar(button.selected
						? GetThemeColor(BarThemeColorEnum::Accent)
						: GetThemeColor(BarThemeColorEnum::PressedFill));
					shape->frame->SetTar(button.selected
						? GetThemeColor(BarThemeColorEnum::Accent)
						: GetThemeColor(BarThemeColorEnum::TextPrimary));
					if (!state.barState.geometryAttribute)
					{
						shape->pct.SetTar(0.0);
						shape->frameLightPct->SetTar(0.0);
					}
					else
					{
						if (button.pressed) shape->pct.SetTar(0.10);
						else if (button.selected) shape->pct.SetTar(0.20);
						else if (*button.hoverStage == BarButtonHoverStageEnum::None)
							shape->pct.SetTar(0.0);
						shape->frameLightPct->SetTar(button.selected
							? (button.pressed ? BarButtonPressedLightOpacity : 1.0)
							: 0.0);
					}
					word->x.SetTar(button.x * layoutScale);
					word->y.SetTar((button.label ? 35.0 : button.y) * layoutScale);
					word->w.SetTar(button.size * layoutScale);
					word->h.SetTar((button.label ? 15.0 : button.size) * layoutScale);
					word->size.SetTar((button.label ? 11.0 : 10.0) * layoutScale);
					word->color.SetTar(button.selected
						? GetThemeColor(BarThemeColorEnum::Accent)
						: GetThemeColor(BarThemeColorEnum::TextPrimary));
					if (button.label)
						word->pct.SetTar(state.barState.geometryAttribute ? 1.0 : 0.0);
					else
					{
						int presetPx = GetBarThicknessPresetPx(
							PenModeSelectEnum::IdtPenBrush1, index - 2,
							state.barStyle.dpiZoom);
						word->content.SetTar(to_wstring(presetPx));
						double availableDiameter =
							(BarGeometryAttributeThicknessButtonSize - 8.0)
							* layoutScale * static_cast<double>(frameZoom);
						word->pct.SetTar(state.barState.geometryAttribute
							&& presetPx * layoutScale > availableDiameter ? 1.0 : 0.0);
					}
					button.pressScale->SetTar(
						button.pressed ? BarButtonPressScale : 1.0,
						BarUiDefaultOperationDur, nullopt, false,
						button.pressed ? state.buttonPressCurve : state.buttonReleaseCurve);
				}

				// 面板与主栏复用同一组双色 SVG，避免两套图形风格逐渐分叉。
				for (size_t index = 0; index < 2; ++index)
				{
					auto icon = state.svgMap[static_cast<BarUISetSvgEnum>(
						static_cast<int>(BarUISetSvgEnum::GeometryAttributeBar_StraightLine)
						+ static_cast<int>(index))];
					const auto& button = buttons[index];
					icon->x.SetTar((button.x + 11.0) * layoutScale);
					icon->y.SetTar((button.y + 3.0) * layoutScale);
					icon->SetWH(28.0 * layoutScale, 28.0 * layoutScale);
					COLORREF iconColor = button.selected
						? GetThemeColor(BarThemeColorEnum::Accent)
						: GetThemeColor(BarThemeColorEnum::TextPrimary);
					icon->color1->SetTar(iconColor);
					icon->color2->SetTar(iconColor);
					icon->pct.SetTar(state.barState.geometryAttribute
						? (button.pressed ? 0.70 : 1.0) : 0.0);
				}

				auto close = state.shapeMap[
					BarUISetShapeEnum::GeometryAttributeBar_Close];
				close->x.SetTar(300.0 * layoutScale);
				close->y.SetTar(5.0 * layoutScale);
				close->w.SetTar(30.0 * layoutScale);
				close->h.SetTar(30.0 * layoutScale);
				close->rw->SetTar(4.0 * layoutScale);
				close->rh->SetTar(4.0 * layoutScale);
				close->ft->SetTar(layoutScale);
				close->fill->SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
				if (!state.barState.geometryAttribute) close->pct.SetTar(0.0);
				else if (state.barState.geometryAttributeBar.closePress)
					close->pct.SetTar(0.10);
				else if (state.geometryCloseHoverStage == BarButtonHoverStageEnum::None)
					close->pct.SetTar(0.0);
				state.geometryClosePressScale.SetTar(
					state.barState.geometryAttributeBar.closePress
						? BarButtonPressScale : 1.0,
					BarUiDefaultOperationDur, nullopt, false,
					state.barState.geometryAttributeBar.closePress
						? state.buttonPressCurve : state.buttonReleaseCurve);
				auto closeSvg = state.svgMap[
					BarUISetSvgEnum::GeometryAttributeBar_Close];
				closeSvg->x.SetTar(306.0 * layoutScale);
				closeSvg->y.SetTar(11.0 * layoutScale);
				closeSvg->SetWH(18.0 * layoutScale, 18.0 * layoutScale);
				closeSvg->color1->SetTar(
					GetThemeColor(BarThemeColorEnum::TextPrimary));
				closeSvg->pct.SetTar(state.barState.geometryAttribute ? 1.0 : 0.0);

				auto divider = state.shapeMap[
					BarUISetShapeEnum::GeometryAttributeBar_Divider];
				divider->x.SetTar(5.0 * layoutScale);
				divider->y.SetTar(60.0 * layoutScale);
				divider->w.SetTar(325.0 * layoutScale);
				divider->h.SetTar(BarUiDividerWidth * layoutScale);
				divider->rw->SetTar(BarUiDividerRadius * layoutScale);
				divider->rh->SetTar(BarUiDividerRadius * layoutScale);
				divider->ft->SetTar(layoutScale);
				divider->fill->SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				divider->frame->SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				divider->pct.SetTar(state.barState.geometryAttribute ? 0.30 : 0.0);
				divider->framePct->SetTar(0.0);
				divider->frameLightPct->SetTar(state.barState.geometryAttribute ? 1.0 : 0.0);

				auto SyncGeometryShape = [&](const shared_ptr<BarUiShapeClass>& shape)
					{
						SyncValueDuration(shape->x); SyncValueDuration(shape->y);
						SyncValueDuration(shape->w); SyncValueDuration(shape->h);
						if (shape->rw) SyncValueDuration(*shape->rw);
						if (shape->rh) SyncValueDuration(*shape->rh);
						if (shape->ft) SyncValueDuration(*shape->ft);
						SyncPctDuration(shape->pct);
						if (shape->framePct) SyncPctDuration(*shape->framePct);
						if (shape->frameLightPct) SyncPctDuration(*shape->frameLightPct);
					};
				for (int value = static_cast<int>(BarUISetShapeEnum::GeometryAttributeBar);
					value <= static_cast<int>(
						BarUISetShapeEnum::GeometryAttributeBar_Close); ++value)
					SyncGeometryShape(state.shapeMap[static_cast<BarUISetShapeEnum>(value)]);
				for (int value = static_cast<int>(
					BarUISetSvgEnum::GeometryAttributeBar_StraightLine);
					value <= static_cast<int>(BarUISetSvgEnum::GeometryAttributeBar_Close);
					++value)
				{
					auto svg = state.svgMap[static_cast<BarUISetSvgEnum>(value)];
					SyncValueDuration(svg->x); SyncValueDuration(svg->y);
					SyncValueDuration(svg->w); SyncValueDuration(svg->h);
					SyncValueDuration(svg->angle);
					SyncPctDuration(svg->pct);
				}
				for (int value = static_cast<int>(
					BarUISetWordEnum::GeometryAttributeBar_StraightLine);
					value <= static_cast<int>(
						BarUISetWordEnum::GeometryAttributeBar_ThicknessCoarseNumber); ++value)
				{
					auto word = state.wordMap[static_cast<BarUISetWordEnum>(value)];
					SyncValueDuration(word->x); SyncValueDuration(word->y);
					SyncValueDuration(word->w); SyncValueDuration(word->h);
					SyncValueDuration(word->size); SyncPctDuration(word->pct);
				}

				auto RestartShape = [&](const shared_ptr<BarUiShapeClass>& shape,
					bool sideSwitch, bool root)
					{
						auto middle = [&](double value, bool keepAtZero = false)
							{
								return keepAtZero ? 0.0 : Compact(value);
							};
						const auto& curve = sideSwitch ? sideValueCurve : syncedValueCurve;
						const auto& opacityCurve = sideSwitch ? sidePctCurve : syncedPctCurve;
						optional<double> middleX = sideSwitch
							? optional<double>(root ? 0.0 : middle(shape->x.tar)) : nullopt;
						optional<double> middleY = sideSwitch
							? optional<double>(root ? 0.0 : middle(shape->y.tar)) : nullopt;
						optional<double> middleW = sideSwitch ? optional<double>(root
							? BarGeometryAttributeCompactWidth
							: max(1.0, middle(shape->w.tar))) : nullopt;
						optional<double> middleH = sideSwitch ? optional<double>(root
							? BarGeometryAttributeCompactHeight
							: max(1.0, middle(shape->h.tar))) : nullopt;
						shape->x.SetTar(shape->x.tar, operationDur, middleX, true, curve);
						shape->y.SetTar(shape->y.tar, operationDur, middleY, true, curve);
						shape->w.SetTar(shape->w.tar, operationDur, middleW, true, curve);
						shape->h.SetTar(shape->h.tar, operationDur, middleH, true, curve);
						if (shape->rw) shape->rw->SetTar(shape->rw->tar, operationDur,
							sideSwitch ? optional<double>(middle(shape->rw->tar)) : nullopt,
							true, curve);
						if (shape->rh) shape->rh->SetTar(shape->rh->tar, operationDur,
							sideSwitch ? optional<double>(middle(shape->rh->tar)) : nullopt,
							true, curve);
						if (shape->ft) shape->ft->SetTar(shape->ft->tar, operationDur,
							sideSwitch ? optional<double>(middle(shape->ft->tar)) : nullopt,
							true, curve);
						shape->pct.SetTar(shape->pct.tar, operationDur,
							sideSwitch ? optional<double>(0.0) : nullopt, true, opacityCurve);
						if (shape->framePct) shape->framePct->SetTar(shape->framePct->tar,
							operationDur, sideSwitch ? optional<double>(0.0) : nullopt,
							true, opacityCurve);
						if (shape->frameLightPct) shape->frameLightPct->SetTar(
							shape->frameLightPct->tar, operationDur,
							sideSwitch ? optional<double>(0.0) : nullopt, true, opacityCurve);
					};
				auto RestartSvg = [&](const shared_ptr<BarUiSVGClass>& svg)
					{
						const auto& curve = geometryAttributeSideSwitch
							? sideValueCurve : syncedValueCurve;
						const auto& opacityCurve = geometryAttributeSideSwitch
							? sidePctCurve : syncedPctCurve;
						svg->x.SetTar(svg->x.tar, operationDur,
							geometryAttributeSideSwitch
								? optional<double>(Compact(svg->x.tar)) : nullopt,
							true, curve);
						svg->y.SetTar(svg->y.tar, operationDur,
							geometryAttributeSideSwitch
								? optional<double>(Compact(svg->y.tar)) : nullopt,
							true, curve);
						svg->w.SetTar(svg->w.tar, operationDur,
							geometryAttributeSideSwitch
								? optional<double>(max(1.0, Compact(svg->w.tar))) : nullopt,
							true, curve);
					svg->h.SetTar(svg->h.tar, operationDur,
						geometryAttributeSideSwitch
							? optional<double>(max(1.0, Compact(svg->h.tar))) : nullopt,
						true, curve);
					svg->angle.SetTar(svg->angle.tar, operationDur,
						nullopt, true, curve);
					svg->pct.SetTar(svg->pct.tar, operationDur,
							geometryAttributeSideSwitch ? optional<double>(0.0) : nullopt,
							true, opacityCurve);
					};
				if (geometryAttributeVisibilityChange || geometryAttributeSideSwitch)
				{
					for (int value = static_cast<int>(BarUISetShapeEnum::GeometryAttributeBar);
						value <= static_cast<int>(
							BarUISetShapeEnum::GeometryAttributeBar_Close); ++value)
						RestartShape(state.shapeMap[static_cast<BarUISetShapeEnum>(value)],
							geometryAttributeSideSwitch,
							value == static_cast<int>(BarUISetShapeEnum::GeometryAttributeBar));
					for (int value = static_cast<int>(
						BarUISetWordEnum::GeometryAttributeBar_StraightLine);
						value <= static_cast<int>(
							BarUISetWordEnum::GeometryAttributeBar_ThicknessCoarseNumber); ++value)
					{
						auto word = state.wordMap[static_cast<BarUISetWordEnum>(value)];
						const auto& curve = geometryAttributeSideSwitch
							? sideValueCurve : syncedValueCurve;
						const auto& opacityCurve = geometryAttributeSideSwitch
							? sidePctCurve : syncedPctCurve;
						auto Middle = [&](double target) -> optional<double>
							{
								return geometryAttributeSideSwitch
									? optional<double>(max(1.0, Compact(target))) : nullopt;
							};
						word->x.SetTar(word->x.tar, operationDur,
							geometryAttributeSideSwitch
								? optional<double>(Compact(word->x.tar)) : nullopt, true, curve);
						word->y.SetTar(word->y.tar, operationDur,
							geometryAttributeSideSwitch
								? optional<double>(Compact(word->y.tar)) : nullopt, true, curve);
						word->w.SetTar(word->w.tar, operationDur, Middle(word->w.tar), true, curve);
						word->h.SetTar(word->h.tar, operationDur, Middle(word->h.tar), true, curve);
						word->size.SetTar(word->size.tar, operationDur, Middle(word->size.tar), true, curve);
						word->pct.SetTar(word->pct.tar, operationDur,
							geometryAttributeSideSwitch ? optional<double>(0.0) : nullopt,
							true, opacityCurve);
					}
					for (int value = static_cast<int>(
						BarUISetSvgEnum::GeometryAttributeBar_StraightLine);
						value <= static_cast<int>(BarUISetSvgEnum::GeometryAttributeBar_Close);
						++value)
						RestartSvg(state.svgMap[static_cast<BarUISetSvgEnum>(value)]);
				}
			}
		}
	}

	// 更多浮层使用标准 2x2 子网格，显式更多在远端，强制溢出靠近主栏。
	{
		auto moreButton = state.barButtonSet.GetMoreButton();
		auto panel = state.shapeMap[BarUISetShapeEnum::MorePanel];
		auto divider = state.shapeMap[BarUISetShapeEnum::MorePanelDivider];
		auto close = state.shapeMap[BarUISetShapeEnum::MorePanelCloseHit];
		auto closeSvg = state.svgMap[BarUISetSvgEnum::MorePanelClose];
		BarMoreButtonSnapshotClass snapshot = state.barButtonSet.GetMoreButtonSnapshot();
		bool hasButtons = snapshot.HasButtons();
		if (!hasButtons)
		{
			state.barState.moreExpanded = false;
			if (moreButton) moreButton->localState.state = BarWidgetState::None;
		}
		bool open = hasButtons && state.barState.moreExpanded && !state.barState.fold;
		bool side = state.barState.widgetPosition.primaryBar;
		// 三角保持固定朝向，展开态改由入口 Selected 与青色高亮表达。
		if (moreButton) moreButton->icon.angle.SetDirect(0.0);
		state.morePanelProgress.SetTar(open ? 1.0 : 0.0,
			BarUiDefaultOperationDur, nullopt, false,
			{ open ? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack,
				open ? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInBack,
				0.0, false });
		state.morePanelOpacity.SetTar(open ? 1.0 : 0.0,
			BarUiDefaultOperationDur, nullopt, false,
			{ open ? BarUiCurveEnum::EaseOutSine : BarUiCurveEnum::EaseInSine,
				open ? BarUiCurveEnum::EaseOutSine : BarUiCurveEnum::EaseInSine,
				0.0, false });
		struct MorePlacement
		{
			shared_ptr<BarButtonClass> button;
			int column = 0;
			int row = 0;
			int columnSpan = 1;
			int rowSpan = 1;
			bool forced = false;
		};
		const size_t itemCount = snapshot.explicitMore.size()
			+ snapshot.forcedOverflow.size();
		const double packedItemCount = static_cast<double>(
			max<size_t>(1, itemCount));
		int columns = min(5, max(1,
			static_cast<int>(ceil(sqrt(packedItemCount)))));
		int subColumns = columns * 2;
		vector<vector<bool>> occupied;
		vector<MorePlacement> placements;
		auto SpanFor = [](BarButtonSizeEnum size) -> pair<int, int>
			{
				switch (size)
				{
				case BarButtonSizeEnum::twoTwo: return { 2, 2 };
				case BarButtonSizeEnum::twoOne: return { 2, 1 };
				case BarButtonSizeEnum::oneTwo: return { 1, 2 };
				case BarButtonSizeEnum::oneOne: return { 1, 1 };
				}
				return { 2, 2 };
			};
		auto EnsureRows = [&](int rowCount)
			{
				while (static_cast<int>(occupied.size()) < rowCount)
					occupied.push_back(vector<bool>(subColumns, false));
			};
		auto PackGroup = [&](const vector<shared_ptr<BarButtonClass>>& buttons,
			int startRow, bool forced) -> int
			{
				int usedRows = startRow;
				for (const shared_ptr<BarButtonClass>& button : buttons)
				{
					if (!button) continue;
					auto [columnSpan, rowSpan] = SpanFor(button->size);
					bool placed = false;
					for (int row = startRow; !placed; ++row)
					{
						EnsureRows(row + rowSpan);
						for (int column = 0;
							column + columnSpan <= subColumns; ++column)
						{
							bool free = true;
							for (int y = 0; free && y < rowSpan; ++y)
								for (int x = 0; x < columnSpan; ++x)
									if (occupied[row + y][column + x])
									{
										free = false;
										break;
									}
							if (!free) continue;
							for (int y = 0; y < rowSpan; ++y)
								for (int x = 0; x < columnSpan; ++x)
									occupied[row + y][column + x] = true;
							placements.push_back({ button, column, row,
								columnSpan, rowSpan, forced });
							usedRows = max(usedRows, row + rowSpan);
							placed = true;
							break;
						}
					}
				}
				return usedRows;
			};
		int explicitRows = PackGroup(snapshot.explicitMore, 0, false);
		if (explicitRows % 2 != 0) ++explicitRows;
		bool showDivider = !snapshot.explicitMore.empty()
			&& !snapshot.forcedOverflow.empty();
		int forcedStartRow = showDivider ? explicitRows : 0;
		int forcedRows = PackGroup(snapshot.forcedOverflow,
			forcedStartRow, true);
		int totalRows = max(1, max(explicitRows, forcedRows));

		constexpr double one = 32.5;
		constexpr double step = 37.5;
		double contentWidth = subColumns * one
			+ max(0, subColumns - 1) * BarMorePanelGap;
		double gridHeight = totalRows * one
			+ max(0, totalRows - 1) * BarMorePanelGap
			+ (showDivider ? BarMorePanelSeparatorGap : 0.0);
		double panelWidth = contentWidth + BarMorePanelPadding * 2.0
			+ BarMorePanelCloseSideWidth;
		double panelHeight = BarMorePanelPadding * 2.0 + gridHeight;
		double compactWidth = BarMorePanelCompactWidth;
		double compactHeight = BarMorePanelCompactHeight;
		double rawProgress = static_cast<double>(state.morePanelProgress.val);
		double opacityProgress = clamp(
			static_cast<double>(state.morePanelOpacity.val), 0.0, 1.0);
		bool settleHiddenButtonVisuals = !open
			&& state.morePanelProgress.IsSame() && state.morePanelOpacity.IsSame()
			&& rawProgress <= 0.000001;
		// 根面板从 More 按钮中心的紧凑态展开，和绘制属性/几何面板共享锚点语义。
		double scale = max(0.01, BarMorePanelCompactScale
			+ (1.0 - BarMorePanelCompactScale) * rawProgress);
		double logicalWindowWidth = frameZoom > 0.0
			? static_cast<double>(state.barWindow.w) / frameZoom : panelWidth;
		double logicalWindowHeight = frameZoom > 0.0
			? static_cast<double>(state.barWindow.h) / frameZoom : panelHeight;
		double anchorX = moreButton
			? moreButton->button.inhX + moreButton->button.w.val / 2.0
			: logicalWindowWidth / 2.0;
		double anchorY = moreButton
			? moreButton->button.inhY + moreButton->button.h.val / 2.0
			: logicalWindowHeight / 2.0;
		double expandedCenterX = clamp(anchorX,
			BarMorePanelPadding + panelWidth / 2.0,
			max(BarMorePanelPadding + panelWidth / 2.0,
				logicalWindowWidth - BarMorePanelPadding - panelWidth / 2.0));
		double direction = side ? 1.0 : -1.0;
		double expandedCenterY = anchorY + direction * (
			(moreButton ? moreButton->button.h.val / 2.0 : 35.0)
			+ BarMorePanelAnchorGap + panelHeight / 2.0);
		expandedCenterY = clamp(expandedCenterY,
			BarMorePanelPadding + panelHeight / 2.0,
			max(BarMorePanelPadding + panelHeight / 2.0,
				logicalWindowHeight - BarMorePanelPadding - panelHeight / 2.0));
		double panelCenterX = anchorX
			+ (expandedCenterX - anchorX) * rawProgress;
		double panelCenterY = anchorY
			+ (expandedCenterY - anchorY) * rawProgress;
		double displayWidth = max(1.0,
			compactWidth + (panelWidth - compactWidth) * rawProgress);
		double displayHeight = max(1.0,
			compactHeight + (panelHeight - compactHeight) * rawProgress);
		panel->x.SetDirect(panelCenterX);
		panel->y.SetDirect(panelCenterY);
		panel->w.SetDirect(displayWidth);
		panel->h.SetDirect(displayHeight);
		panel->rw->SetDirect(8.0 * scale);
		panel->rh->SetDirect(8.0 * scale);
		panel->ft->SetDirect(scale);
		panel->fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));
		panel->frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		panel->pct.SetDirect(BarDrawAttributeSurfaceOpacity * opacityProgress);
		panel->framePct->SetDirect(0.18 * opacityProgress);
		panel->UpInh(BarUiInheritClass(
			panelCenterX - displayWidth / 2.0,
			panelCenterY - displayHeight / 2.0));
		// 子内容围绕完整面板中心统一缩放，避免紧凑态被非等比的根面板宽高拉偏。
		auto ScalePanelContentX = [&](double logicalX)
			{
				return panelCenterX + (logicalX - panelWidth / 2.0) * scale;
			};
		auto ScalePanelContentY = [&](double logicalY)
			{
				return panelCenterY + (logicalY - panelHeight / 2.0) * scale;
			};
		double panelCenterInMainBarX = moreButton
			? moreButton->button.x.val + panelCenterX - anchorX
			: panelCenterX
				- state.shapeMap[BarUISetShapeEnum::MainBar]->inhX;
		double panelCenterInMainBarY = moreButton
			? moreButton->button.y.val + panelCenterY - anchorY
			: panelCenterY
				- state.shapeMap[BarUISetShapeEnum::MainBar]->inhY;

		close->w.SetDirect(30.0 * scale);
		close->h.SetDirect(30.0 * scale);
		close->rw->SetDirect(4.0 * scale);
		close->rh->SetDirect(4.0 * scale);
		// X 位于按钮区域右侧窄栏的右上角，关闭时不增加面板高度。
		close->x.SetDirect(ScalePanelContentX(
			panelWidth - BarMorePanelCloseSideWidth / 2.0));
		close->y.SetDirect(ScalePanelContentY(
			BarMorePanelPadding + 15.0));
		if (!open) close->pct.SetTar(0.0, BarUiDefaultOperationDur);
		else if (state.barState.moreClosePress) close->pct.SetTar(0.10);
		else if (state.moreCloseHoverStage == BarButtonHoverStageEnum::None)
			close->pct.SetTar(0.0);
		close->fill->SetTar(GetThemeColor(BarThemeColorEnum::PressedFill));
		state.moreClosePressScale.SetTar(
			state.barState.moreClosePress ? BarButtonPressScale : 1.0,
			BarUiDefaultOperationDur, nullopt, false,
			state.barState.moreClosePress ? state.buttonPressCurve : state.buttonReleaseCurve);
		close->UpInh(BarUiInheritClass(
			close->x.val - close->w.val / 2.0,
			close->y.val - close->h.val / 2.0));
		closeSvg->x.SetDirect(close->x.val);
		closeSvg->y.SetDirect(close->y.val);
		closeSvg->SetWH(18.0 * scale, 18.0 * scale);
		closeSvg->color1->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
		closeSvg->pct.SetDirect(opacityProgress);
		closeSvg->UpInh(BarUiInheritClass(
			closeSvg->x.val - closeSvg->w.val / 2.0,
			closeSvg->y.val - closeSvg->h.val / 2.0));

		double explicitBlockHeight = explicitRows > 0
			? explicitRows * one + max(0, explicitRows - 1) * BarMorePanelGap
			: 0.0;
		double dividerLogicalY = explicitBlockHeight
			+ (BarMorePanelGap + BarMorePanelSeparatorGap) / 2.0;
		double dividerPhysicalY = side
			? gridHeight - dividerLogicalY : dividerLogicalY;
		// 分割线忽略 X 侧栏，直接延伸到面板两侧的等距内边距。
		divider->w.SetDirect(
			(panelWidth - BarMorePanelPadding * 2.0) * scale);
		divider->h.SetDirect(scale);
		divider->x.SetDirect(ScalePanelContentX(panelWidth / 2.0));
		divider->y.SetDirect(ScalePanelContentY(
			BarMorePanelPadding + dividerPhysicalY));
		divider->pct.SetDirect(showDivider ? 0.30 * opacityProgress : 0.0);
		divider->UpInh(BarUiInheritClass(
			divider->x.val - divider->w.val / 2.0,
			divider->y.val - divider->h.val / 2.0));

		for (const MorePlacement& placement : placements)
		{
			BarButtonClass* button = placement.button.get();
			if (!button) continue;
			double width = (placement.columnSpan == 2 ? 70.0 : one);
			double height = (placement.rowSpan == 2 ? 70.0 : one);
			if (button->size == BarButtonSizeEnum::oneTwo) width = 10.0;
			double logicalX = BarMorePanelPadding
				+ placement.column * step + width / 2.0;
			double logicalY = placement.row * step;
			if (showDivider && placement.forced)
				logicalY += BarMorePanelSeparatorGap;
			double physicalY = side
				? gridHeight - logicalY - height : logicalY;
			double localCenterY = BarMorePanelPadding
				+ physicalY + height / 2.0;
			// 始终保存主栏局部坐标：收起后按钮缩在 More 入口下方，补位时不会从远端飘入。
			button->button.x.SetDirect(panelCenterInMainBarX
				+ (logicalX - panelWidth / 2.0) * scale);
			button->button.y.SetDirect(panelCenterInMainBarY
				+ (localCenterY - panelHeight / 2.0) * scale);
			button->button.w.SetDirect(width * scale);
			button->button.h.SetDirect(height * scale);
			if (button->button.rw) button->button.rw->SetDirect(4.0 * scale);
			if (button->button.rh) button->button.rh->SetDirect(4.0 * scale);
			if (!button->button.frame)
				button->button.frame = BarUiColorClass(
					GetThemeColor(BarThemeColorEnum::TextPrimary));
			if (!button->button.framePct) button->button.framePct = BarUiPctClass(0.0);
			if (!button->button.frameLightPct)
				button->button.frameLightPct = BarUiPctClass(0.0);
			if (!button->button.ft) button->button.ft = BarUiValueClass(scale);
			button->button.ft->SetDirect(scale);
			button->button.frameRendering = BarUiFrameRenderingEnum::PointLight;
			button->button.framePrimaryLightEnabled = false;
			button->button.frameCursorLightIntensityScale =
				BarButtonCursorLightIntensity;
			COLORREF buttonFill = button->state->state == BarWidgetState::Selected
				? GetThemeColor(BarThemeColorEnum::Accent)
				: GetThemeColor(BarThemeColorEnum::PressedFill);
			COLORREF buttonFrame = button->state->state == BarWidgetState::Selected
				? GetThemeColor(BarThemeColorEnum::Accent)
				: GetThemeColor(BarThemeColorEnum::TextPrimary);
			if (settleHiddenButtonVisuals)
			{
				// 隐藏时先落稳选中颜色，避免展开后才从旧颜色渐变为青色。
				button->button.fill->SetDirect(buttonFill);
				button->button.frame->SetDirect(buttonFrame);
			}
			else
			{
				button->button.fill->SetTar(buttonFill);
				button->button.frame->SetTar(buttonFrame);
			}
			button->button.frameLightPct->SetTar(
				open && button->state->state == BarWidgetState::Selected
					? (button->state->emph == BarWidgetEmphasize::Pressed
						? BarButtonPressedLightOpacity : 1.0) : 0.0);
			if (!open)
				button->button.pct.SetTar(0.0, BarUiDefaultOperationDur);
			else if (button->state->emph == BarWidgetEmphasize::Pressed)
				button->button.pct.SetTar(0.10, BarUiDefaultOperationDur);
			else if (button->state->state == BarWidgetState::Selected)
				button->button.pct.SetTar(0.20, BarUiDefaultOperationDur);
			else if (button->hoverStage == BarButtonHoverStageEnum::None)
				button->button.pct.SetTar(0.0, BarUiDefaultOperationDur);
			COLORREF contentColor =
				button->state->state == BarWidgetState::Selected
					? GetThemeColor(BarThemeColorEnum::Accent)
					: GetThemeColor(BarThemeColorEnum::TextPrimary);
			if (settleHiddenButtonVisuals)
			{
				button->icon.color1->SetDirect(contentColor);
				if (button->icon.color2)
					button->icon.color2->SetDirect(contentColor);
				button->name.color.SetDirect(contentColor);
			}
			else
			{
				button->icon.color1->SetTar(contentColor);
				if (button->icon.color2)
					button->icon.color2->SetTar(contentColor);
				button->name.color.SetTar(contentColor);
			}
			if (button->size == BarButtonSizeEnum::oneOne)
			{
				button->icon.SetWH(20.0 * scale, 20.0 * scale);
				button->icon.x.SetDirect(0.0);
				button->icon.y.SetDirect(0.0);
				button->name.pct.SetDirect(0.0);
			}
			else if (button->size == BarButtonSizeEnum::twoOne)
			{
				button->icon.SetWH(18.0 * scale, 18.0 * scale);
				button->icon.x.SetDirect(-21.0 * scale);
				button->icon.y.SetDirect(0.0);
				button->name.x.SetDirect(11.5 * scale);
				button->name.y.SetDirect(0.0);
				button->name.w.SetDirect(37.0 * scale);
				button->name.h.SetDirect(one * scale);
				button->name.size.SetDirect(12.0 * scale);
				button->name.pct.SetDirect(opacityProgress);
			}
			else if (button->size == BarButtonSizeEnum::twoTwo)
			{
				button->icon.SetWH(28.0 * scale, 28.0 * scale);
				button->icon.x.SetDirect(0.0);
				button->icon.y.SetDirect(-10.0 * scale);
				button->name.x.SetDirect(0.0);
				button->name.y.SetDirect(20.0 * scale);
				button->name.w.SetDirect(70.0 * scale);
				button->name.h.SetDirect(25.0 * scale);
				button->name.size.SetDirect(13.0 * scale);
				button->name.pct.SetDirect(opacityProgress);
			}
			else
			{
				button->icon.pct.SetDirect(0.0);
				button->name.pct.SetDirect(0.0);
				continue;
			}
			button->icon.pct.SetDirect(opacityProgress);
			button->lastDrawX = button->button.x.val;
			button->lastDrawY = button->button.y.val;
		}
	}

}

bool BarRenderLoopCoordinator::AdvanceAnimationsAndDeriveLayout(
	BarRenderLoopState& state, const BarRenderFrameSnapshot& frame)
{
	const auto& frameDrawingState = frame;
	const double frameZoom = frame.zoom;
	const double animationDtSeconds = frame.animationDtSeconds;
	const double currentAnimationSpeedRate = frame.animationSpeedRate;
	const int forNum = frame.ordinal;
	bool needRendering = false;

	auto AdvanceAnimation = [&](auto& animation, bool forceReplace) -> void
		{
			const BarUiAnimationAdvanceContextClass context{
				animationDtSeconds,
				currentAnimationSpeedRate,
				static_cast<bool>(BarUiAnimationEnabled),
				forceReplace,
			};
			const auto result = BarUiAdvanceAnimation(animation, context);
			// active 保证量化后未变色的中间帧继续推进，changed 保证最后一帧提交。
			needRendering = needRendering || result.changed || result.active;
		};
	auto ChangeState = [&](BarUiStateClass& state, bool forceReplace)
		{ AdvanceAnimation(state, forceReplace); };
	auto ChangeValue = [&](BarUiValueClass& value, bool forceReplace)
		{ AdvanceAnimation(value, forceReplace); };
	auto ChangeColor = [&](BarUiColorClass& color, bool forceReplace)
		{ AdvanceAnimation(color, forceReplace); };
	auto ChangePct = [&](BarUiPctClass& pct, bool forceReplace)
		{ AdvanceAnimation(pct, forceReplace); };
	auto ChangeString = [&](BarUiStringClass& stringO, bool) -> void
		{
			needRendering = true;
			stringO.ApplyTar();
		};
// 关闭动画时拖动不会改变 val/tar，仍需每帧重绘圆点位置。
		if (state.barState.drawAttributeBar.thicknessSliderDragging
			|| state.barState.drawAttributeBar.thicknessSliderPressed
			|| state.barState.drawAttributeBar.thicknessFineDialDragging
			|| state.barState.drawAttributeBar.thicknessFineDialPhysicsActive)
		{
			needRendering = true;
		}
		// 独立的粗细值也进入统一动画时钟，方便后续直接替换为非线性或回弹曲线。
		if (!state.drawAttributePenThickness.IsSame()) ChangeValue(state.drawAttributePenThickness, false);
		if (!state.drawAttributePenPreviewMorph.IsSame())
			ChangeValue(state.drawAttributePenPreviewMorph, false);
		if (!state.drawAttributeThicknessSliderNormalized.IsSame())
			ChangeValue(state.drawAttributeThicknessSliderNormalized, false);
		if (!state.drawAttributeThicknessHoldRingLockOpacity.IsSame())
			ChangeValue(state.drawAttributeThicknessHoldRingLockOpacity, false);
		if (!state.drawAttributeThicknessHoldTextMix.IsSame())
			ChangeValue(state.drawAttributeThicknessHoldTextMix, false);
		if (!state.drawAttributeThicknessHoldExchangeProgress.IsSame())
			ChangeValue(state.drawAttributeThicknessHoldExchangeProgress, false);
		if (!state.drawAttributeThicknessHoldGroupScale.IsSame())
			ChangeValue(state.drawAttributeThicknessHoldGroupScale, false);
		if (!state.drawAttributeThicknessPreviewPopupProgress.IsSame())
			ChangeValue(state.drawAttributeThicknessPreviewPopupProgress, false);
		if (!state.drawAttributeThicknessPreviewPopupRetargetProgress.IsSame())
			ChangeValue(
				state.drawAttributeThicknessPreviewPopupRetargetProgress, false);
		if (!state.drawAttributeThicknessSliderProgress.IsSame())
			ChangeValue(state.drawAttributeThicknessSliderProgress, false);
		if (!state.drawAttributeThicknessSliderTrackOpacity.IsSame())
			ChangeValue(state.drawAttributeThicknessSliderTrackOpacity, false);
		if (!state.drawAttributeThicknessFineDialProgress.IsSame())
			ChangeValue(state.drawAttributeThicknessFineDialProgress, false);
		if (!state.drawAttributeThicknessFineDialRecognitionVisibility.IsSame())
			ChangeValue(
				state.drawAttributeThicknessFineDialRecognitionVisibility, false);
		if (!state.drawAttributeThicknessFineDialDwellProgress.IsSame())
			ChangeValue(state.drawAttributeThicknessFineDialDwellProgress, false);
		if (!state.drawAttributeThicknessFineDialSelectionProgress.IsSame())
			ChangeValue(state.drawAttributeThicknessFineDialSelectionProgress, false);
		if (!state.thicknessFineDialOldRangeOpacity.IsSame())
			ChangeValue(state.thicknessFineDialOldRangeOpacity, false);
		if (!state.thicknessFineDialNewRangeOpacity.IsSame())
			ChangeValue(state.thicknessFineDialNewRangeOpacity, false);
		// 静止保持进度由交互线程写入，渲染侧每帧重绘环形进度。
		if (state.barState.drawAttributeBar.thicknessSliderHoldHintActive
			|| state.barState.drawAttributeBar.thicknessSliderHoldLocked)
		{
			needRendering = true;
		}
	if (!state.drawAttributeThicknessSliderThumbOpacity.IsSame())
		ChangeValue(state.drawAttributeThicknessSliderThumbOpacity, false);
	if (!state.drawAttributeThicknessSliderThumbScale.IsSame())
		ChangeValue(state.drawAttributeThicknessSliderThumbScale, false);
	if (!state.drawAttributeThicknessSliderAccentOpacity.IsSame())
		ChangeValue(state.drawAttributeThicknessSliderAccentOpacity, false);
	if (!state.drawAttributeThicknessSliderCenterDiameter.IsSame())
		ChangeValue(state.drawAttributeThicknessSliderCenterDiameter, false);
	if (!state.drawAttributeAnnotationPopupProgress.IsSame())
		ChangeValue(state.drawAttributeAnnotationPopupProgress, false);
	if (!state.drawAttributeOverflowPopupProgress.IsSame())
		ChangeValue(state.drawAttributeOverflowPopupProgress, false);
	if (!state.drawAttributeOverflowBadgeProgress.IsSame())
		ChangeValue(state.drawAttributeOverflowBadgeProgress, false);
	if (!state.drawAttributePenTypeMenuProgress.IsSame())
		ChangeValue(state.drawAttributePenTypeMenuProgress, false);
	if (!state.drawAttributeColorPickerProgress.IsSame())
		ChangeValue(state.drawAttributeColorPickerProgress, false);
	if (!state.drawAttributeColorPickerEntryPressScale.IsSame())
		ChangeValue(state.drawAttributeColorPickerEntryPressScale, false);
	if (!state.drawAttributeColorPickerToneMix.IsSame())
		ChangeValue(state.drawAttributeColorPickerToneMix, false);
	if (!state.drawAttributeColorPickerHoldOpacity.IsSame())
		ChangeValue(state.drawAttributeColorPickerHoldOpacity, false);
	if (!state.drawAttributeColorPickerHoldRingOpacity.IsSame())
		ChangeValue(state.drawAttributeColorPickerHoldRingOpacity, false);
	if (!state.drawAttributeColorPickerHoldTextMix.IsSame())
		ChangeValue(state.drawAttributeColorPickerHoldTextMix, false);
	if (!state.drawAttributeColorPickerDisplayR.IsSame())
		ChangeValue(state.drawAttributeColorPickerDisplayR, false);
	if (!state.drawAttributeColorPickerDisplayG.IsSame())
		ChangeValue(state.drawAttributeColorPickerDisplayG, false);
	if (!state.drawAttributeColorPickerDisplayB.IsSame())
		ChangeValue(state.drawAttributeColorPickerDisplayB, false);
	if (!state.drawAttributeColorPickerDisplayOpacity.IsSame())
		ChangeValue(state.drawAttributeColorPickerDisplayOpacity, false);
	if (!state.morePanelProgress.IsSame())
		ChangeValue(state.morePanelProgress, false);
	if (!state.morePanelOpacity.IsSame())
		ChangeValue(state.morePanelOpacity, false);
	// 保持进度仅在按压期间推进；静止打开的面板不会维持渲染唤醒。
	if (state.barState.drawAttributeBar.colorPickerPointerPressed
		&& (state.barState.drawAttributeBar.colorPickerHoldHintActive
			|| state.barState.drawAttributeBar.colorPickerHoldLocked))
	{
		needRendering = true;
	}
	if (!state.drawAttributeBrushPressScale.IsSame()) ChangeValue(state.drawAttributeBrushPressScale, false);
	if (!state.drawAttributeHighlightPressScale.IsSame()) ChangeValue(state.drawAttributeHighlightPressScale, false);
	if (!state.drawAttributePenTypeExtensionPressScale.IsSame())
		ChangeValue(state.drawAttributePenTypeExtensionPressScale, false);
	if (!state.drawAttributePenTypeFreeLinePressScale.IsSame())
		ChangeValue(state.drawAttributePenTypeFreeLinePressScale, false);
	if (!state.drawAttributeThicknessFinePressScale.IsSame()) ChangeValue(state.drawAttributeThicknessFinePressScale, false);
	if (!state.drawAttributeThicknessMediumPressScale.IsSame()) ChangeValue(state.drawAttributeThicknessMediumPressScale, false);
	if (!state.drawAttributeThicknessCoarsePressScale.IsSame()) ChangeValue(state.drawAttributeThicknessCoarsePressScale, false);
	if (!state.drawAttributeThicknessAdjustPressScale.IsSame()) ChangeValue(state.drawAttributeThicknessAdjustPressScale, false);
	if (!state.drawAttributeAnnotationClosePressScale.IsSame())
		ChangeValue(state.drawAttributeAnnotationClosePressScale, false);
	if (!state.drawAttributeOverflowClosePressScale.IsSame())
		ChangeValue(state.drawAttributeOverflowClosePressScale, false);
	if (!state.drawAttributeColorPickerTonePressScale.IsSame())
		ChangeValue(state.drawAttributeColorPickerTonePressScale, false);
	if (!state.drawAttributeColorPickerClosePressScale.IsSame())
		ChangeValue(state.drawAttributeColorPickerClosePressScale, false);
	if (!state.moreClosePressScale.IsSame())
		ChangeValue(state.moreClosePressScale, false);
	if (!state.geometryStraightLinePressScale.IsSame())
		ChangeValue(state.geometryStraightLinePressScale, false);
	if (!state.geometryRectanglePressScale.IsSame())
		ChangeValue(state.geometryRectanglePressScale, false);
	if (!state.geometryThicknessFinePressScale.IsSame())
		ChangeValue(state.geometryThicknessFinePressScale, false);
	if (!state.geometryThicknessMediumPressScale.IsSame())
		ChangeValue(state.geometryThicknessMediumPressScale, false);
	if (!state.geometryThicknessCoarsePressScale.IsSame())
		ChangeValue(state.geometryThicknessCoarsePressScale, false);
	if (!state.geometryClosePressScale.IsSame())
		ChangeValue(state.geometryClosePressScale, false);

	for (const auto& [key, val] : state.shapeMap)
	{
		bool forceReplace = false, change = false;
		if (val->forceReplace) val->forceReplace = false, forceReplace = true;

		if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
		if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
		if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
		if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
		if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
		if (val->rw.has_value() && !val->rw->IsSame()) ChangeValue(val->rw.value(), forceReplace), change = true;
		if (val->rh.has_value() && !val->rh->IsSame()) ChangeValue(val->rh.value(), forceReplace), change = true;
		if (val->ft.has_value() && !val->ft->IsSame()) ChangeValue(val->ft.value(), forceReplace), change = true;
		if (val->fill.has_value() && !val->fill->IsSame()) ChangeColor(val->fill.value(), forceReplace), change = true;
		if (val->frame.has_value() && !val->frame->IsSame()) ChangeColor(val->frame.value(), forceReplace), change = true;
		if (val->framePct.has_value() && !val->framePct->IsSame()) ChangePct(val->framePct.value(), forceReplace), change = true;
		if (val->frameLightPct.has_value() && !val->frameLightPct->IsSame()) ChangePct(val->frameLightPct.value(), forceReplace), change = true;
		if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
	}
	for (const auto& [key, val] : state.superellipseMap)
	{
		bool forceReplace = false, change = false;
		if (val->forceReplace) val->forceReplace = false, forceReplace = true;

		if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
		if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
		if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
		if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
		if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
		if (val->n.has_value() && !val->n->IsSame()) ChangeValue(val->n.value(), forceReplace), change = true;
		if (val->ft.has_value() && !val->ft->IsSame()) ChangeValue(val->ft.value(), forceReplace), change = true;
		if (val->fill.has_value() && !val->fill->IsSame()) ChangeColor(val->fill.value(), forceReplace), change = true;
		if (val->frame.has_value() && !val->frame->IsSame()) ChangeColor(val->frame.value(), forceReplace), change = true;
		if (val->framePct.has_value() && !val->framePct->IsSame()) ChangePct(val->framePct.value(), forceReplace), change = true;
		if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
	}
	for (const auto& [key, val] : state.svgMap)
	{
		bool forceReplace = false, change = false;;
		if (val->forceReplace) val->forceReplace = false, forceReplace = true;
		if (val->AdvanceContentTransition(animationDtSeconds, currentAnimationSpeedRate))
		{
			needRendering = true;
		}

		if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
		if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
		if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
		if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
		if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
		if (!val->angle.IsSame()) ChangeValue(val->angle, forceReplace), change = true;
		if (!val->svg.IsSame()) ChangeString(val->svg, forceReplace), change = true;
		if (val->color1.has_value() && !val->color1->IsSame()) ChangeColor(val->color1.value(), forceReplace), change = true;
		if (val->color2.has_value() && !val->color2->IsSame()) ChangeColor(val->color2.value(), forceReplace), change = true;
		if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
	}
	for (const auto& [key, val] : state.pngMap)
	{
		bool forceReplace = false, change = false;
		if (val->forceReplace) val->forceReplace = false, forceReplace = true;

		if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
		if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
		if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
		if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
		if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
		if (!val->angle.IsSame()) ChangeValue(val->angle, forceReplace), change = true;
		if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
	}
	for (const auto& [key, val] : state.wordMap)
	{
		bool forceReplace = false, change = false;;
		if (val->forceReplace) val->forceReplace = false, forceReplace = true;
		if (val->AdvanceContentTransition(
			animationDtSeconds, currentAnimationSpeedRate))
		{
			needRendering = true;
		}

		if (!val->enable.IsSame()) ChangeState(val->enable, forceReplace), change = true;
		if (!val->x.IsSame()) ChangeValue(val->x, forceReplace), change = true;
		if (!val->y.IsSame()) ChangeValue(val->y, forceReplace), change = true;
		if (!val->w.IsSame()) ChangeValue(val->w, forceReplace), change = true;
		if (!val->h.IsSame()) ChangeValue(val->h, forceReplace), change = true;
		if (!val->size.IsSame()) ChangeValue(val->size, forceReplace), change = true;
		if (!val->content.IsSame()) ChangeString(val->content, forceReplace), change = true;
		if (!val->color.IsSame()) ChangeColor(val->color, forceReplace), change = true;
		if (!val->pct.IsSame()) ChangePct(val->pct, forceReplace), change = true;
	}
	auto UpdateHoverAnimation = [&](BarUiPctClass& hoverPct, BarUiColorClass* hoverFill,
		IdtAtomic<BarButtonHoverStageEnum>& hoverStage, bool visible, bool hoverAllowed)
		{
			auto FinishHover = [&]()
				{
					hoverStage = BarButtonHoverStageEnum::None;
					hoverPct.animateWhenDisabled = false;
					if (hoverFill) hoverFill->animateWhenDisabled = false;
				};
			if (!visible)
			{
				// 隐藏时清除仍在运行的独立悬停过程，避免下次显示继承旧的渐隐灰色。
				if (hoverStage != BarButtonHoverStageEnum::None) hoverPct.SetDirect(0.0);
				FinishHover();
			}
			else if (!hoverAllowed)
			{
				// 选中状态继续复用同一背景层，透明度由选中动画接管。
				FinishHover();
			}
			else if (hoverStage == BarButtonHoverStageEnum::Showing
				&& hoverPct.IsSame())
			{
				// 快速显现完成后立即进入独立渐隐阶段，同一次进入不会重新计时。
				hoverStage = BarButtonHoverStageEnum::Fading;
				const BarUiCurveSpecClass hoverFadeCurve{
					BarUiCurveEnum::EaseInSine, BarUiCurveEnum::EaseInSine, 0.0, false };
				hoverPct.SetTar(0.0, BarButtonHoverFadeDur, nullopt, true, hoverFadeCurve);
			}
			else if (hoverStage == BarButtonHoverStageEnum::Fading
				&& hoverPct.IsSame())
			{
				FinishHover();
			}
		};

	auto drawAttributeBrush = state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1];
	UpdateHoverAnimation(drawAttributeBrush->pct, &drawAttributeBrush->fill.value(),
		state.drawAttributeBrushHoverStage, state.barState.drawAttribute,
		stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenBrush1);
	auto drawAttributeHighlight = state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_Highlight1];
	UpdateHoverAnimation(drawAttributeHighlight->pct, &drawAttributeHighlight->fill.value(),
		state.drawAttributeHighlightHoverStage, state.barState.drawAttribute,
		stateMode.Pen.ModeSelect != PenModeSelectEnum::IdtPenHighlighter1);
	auto penTypeExtension = state.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit];
	bool penTypeExtensionVisible = state.barState.drawAttribute && !state.barState.fold
		&& PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
	UpdateHoverAnimation(penTypeExtension->pct,
		&penTypeExtension->fill.value(),
		state.drawAttributePenTypeExtensionHoverStage,
		penTypeExtensionVisible,
		!state.barState.drawAttributeBar.penTypeExtensionPress);
	auto penTypeFreeLine = state.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine];
	bool penTypeFreeLineVisible = penTypeExtensionVisible
		&& state.barState.drawAttributeBar.penTypeMenuOpen;
	UpdateHoverAnimation(penTypeFreeLine->pct,
		&penTypeFreeLine->fill.value(),
		state.drawAttributePenTypeFreeLineHoverStage,
		penTypeFreeLineVisible,
		!state.barState.drawAttributeBar.penTypeFreeLinePress);
	const BarUISetShapeEnum thicknessPresetShapes[] =
	{
		BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
		BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
		BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
	};
	IdtAtomic<BarButtonHoverStageEnum>* thicknessPresetHoverStages[] =
	{
		&state.drawAttributeThicknessFineHoverStage,
		&state.drawAttributeThicknessMediumHoverStage,
		&state.drawAttributeThicknessCoarseHoverStage,
	};
		double thicknessControlOpacity = clamp(
			1.0 - static_cast<double>(
				state.drawAttributeThicknessHoldExchangeProgress.val),
			0.0, 1.0);
		bool thicknessPresetMode =
			PenModeUsesThicknessPresets(stateMode.Pen.ModeSelect);
		// 悬停动画同样只按真实粗细判断预设选中，避免拖动候选值误亮按钮。
		int actualThickness = static_cast<int>(lround(clamp(
			static_cast<double>(max(0.0f, GetPenWidth())), 0.0, 999.0)));
		for (size_t i = 0; i < 3; ++i)
		{
			auto shape = state.shapeMap[thicknessPresetShapes[i]];
			bool selected = actualThickness
				== GetBarThicknessPresetPx(
					stateMode.Pen.ModeSelect, i, state.barStyle.dpiZoom);
			UpdateHoverAnimation(shape->pct, &shape->fill.value(),
				*thicknessPresetHoverStages[i],
				state.barState.drawAttribute && thicknessPresetMode,
				thicknessControlOpacity >= 0.999999 && !selected);
		}
		auto thicknessAdjust =
			state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
		bool thicknessAdjustVisible =
			state.barState.drawAttribute && thicknessPresetMode;
	UpdateHoverAnimation(thicknessAdjust->pct, &thicknessAdjust->fill.value(),
		state.drawAttributeThicknessAdjustHoverStage, thicknessAdjustVisible,
		thicknessControlOpacity >= 0.999999
			&& state.barState.drawAttributeBar.thicknessViewMode
				== ThicknessViewMode::Preview);
	auto annotationClose = state.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit];
	UpdateHoverAnimation(annotationClose->pct,
		&annotationClose->fill.value(),
		state.drawAttributeAnnotationCloseHoverStage,
		state.barState.drawAttributeBar.thicknessAnnotationPinned, true);
	auto overflowClose = state.shapeMap[
		BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit];
	UpdateHoverAnimation(overflowClose->pct,
		&overflowClose->fill.value(),
		state.drawAttributeOverflowCloseHoverStage,
		state.barState.drawAttributeBar.thicknessOverflowPinned, true);
	auto colorPickerTone =
		state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle];
	UpdateHoverAnimation(colorPickerTone->pct,
		&colorPickerTone->fill.value(),
		state.drawAttributeColorPickerToneHoverStage,
		state.barState.drawAttributeBar.colorPickerOpen,
		!state.barState.drawAttributeBar.colorPickerTonePress);
	auto colorPickerClose =
		state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit];
	UpdateHoverAnimation(colorPickerClose->pct,
		&colorPickerClose->fill.value(),
		state.drawAttributeColorPickerCloseHoverStage,
		state.barState.drawAttributeBar.colorPickerOpen,
		!state.barState.drawAttributeBar.colorPickerClosePress);
	auto moreClose = state.shapeMap[BarUISetShapeEnum::MorePanelCloseHit];
	UpdateHoverAnimation(moreClose->pct, &moreClose->fill.value(),
		state.moreCloseHoverStage, state.barState.moreExpanded && !state.barState.fold,
		!state.barState.moreClosePress);

	auto geometryStraightLine =
		state.shapeMap[BarUISetShapeEnum::GeometryAttributeBar_StraightLine];
	UpdateHoverAnimation(geometryStraightLine->pct,
		&geometryStraightLine->fill.value(), state.geometryStraightLineHoverStage,
		state.barState.geometryAttribute,
		stateMode.Shape.ModeSelect
			!= ShapeModeSelectEnum::IdtShapeStraightLine1);
	auto geometryRectangle =
		state.shapeMap[BarUISetShapeEnum::GeometryAttributeBar_Rectangle];
	UpdateHoverAnimation(geometryRectangle->pct,
		&geometryRectangle->fill.value(), state.geometryRectangleHoverStage,
		state.barState.geometryAttribute,
		stateMode.Shape.ModeSelect
			!= ShapeModeSelectEnum::IdtShapeRectangle1);
	const BarUISetShapeEnum geometryThicknessShapes[] =
	{
		BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine,
		BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium,
		BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse,
	};
	IdtAtomic<BarButtonHoverStageEnum>* geometryThicknessHoverStages[] =
	{
		&state.geometryThicknessFineHoverStage,
		&state.geometryThicknessMediumHoverStage,
		&state.geometryThicknessCoarseHoverStage,
	};
	int geometryThickness = static_cast<int>(lround(max(
		0.0f, stateMode.Pen.Brush1.width)));
	for (size_t index = 0; index < 3; ++index)
	{
		auto shape = state.shapeMap[geometryThicknessShapes[index]];
		bool selected = geometryThickness == GetBarThicknessPresetPx(
			PenModeSelectEnum::IdtPenBrush1, index, state.barStyle.dpiZoom);
		UpdateHoverAnimation(shape->pct, &shape->fill.value(),
			*geometryThicknessHoverStages[index],
			state.barState.geometryAttribute, !selected);
	}
	auto geometryClose =
		state.shapeMap[BarUISetShapeEnum::GeometryAttributeBar_Close];
	UpdateHoverAnimation(geometryClose->pct,
		&geometryClose->fill.value(), state.geometryCloseHoverStage,
		state.barState.geometryAttribute, true);

	// 主栏与更多浮层共享同一套按钮动画推进，实体不会同时出现在两处。
	auto UpdateRegisteredButtonAnimation = [&](BarButtonClass* temp,
		bool moreItem)
	{
		if (temp == nullptr) return;
		BarUiColorClass* hoverFill = temp->button.fill.has_value()
			? &temp->button.fill.value() : nullptr;
		bool isDivider = temp->preset == BarButtonPresetEnum::Divider;
		if (isDivider)
		{
			// 分隔线只推进几何/光影属性，清除旧按钮状态且不改写第三光透明度。
			temp->hoverStage = BarButtonHoverStageEnum::None;
			temp->state->emph = BarWidgetEmphasize::None;
			temp->pressScale.SetDirect(1.0);
			temp->button.pct.animateWhenDisabled = false;
			if (hoverFill) hoverFill->animateWhenDisabled = false;
		}
		else
		{
			UpdateHoverAnimation(temp->button.pct, hoverFill, temp->hoverStage,
				!state.barState.fold && temp->IsVisible()
					&& (!moreItem || state.barState.moreExpanded),
				temp->state->state != BarWidgetState::Selected);
			if (moreItem)
			{
				bool pressed = temp->state->emph == BarWidgetEmphasize::Pressed;
				temp->pressScale.SetTar(pressed ? BarButtonPressScale : 1.0,
					BarUiDefaultOperationDur, nullopt, false,
					pressed ? state.buttonPressCurve : state.buttonReleaseCurve);
			}
		}
		if (!temp->pressScale.IsSame()) ChangeValue(temp->pressScale, false);

		{
			bool forceReplace = false, change = false;;
			if (temp->button.forceReplace) temp->button.forceReplace = false, forceReplace = true;

			if (!temp->button.enable.IsSame()) ChangeState(temp->button.enable, forceReplace), change = true;
			if (!temp->button.x.IsSame()) ChangeValue(temp->button.x, forceReplace), change = true;
			if (!temp->button.y.IsSame()) ChangeValue(temp->button.y, forceReplace), change = true;
			if (!temp->button.w.IsSame()) ChangeValue(temp->button.w, forceReplace), change = true;
			if (!temp->button.h.IsSame()) ChangeValue(temp->button.h, forceReplace), change = true;
			if (temp->button.rw.has_value() && !temp->button.rw->IsSame()) ChangeValue(temp->button.rw.value(), forceReplace), change = true;
			if (temp->button.rh.has_value() && !temp->button.rh->IsSame()) ChangeValue(temp->button.rh.value(), forceReplace), change = true;
			if (temp->button.ft.has_value() && !temp->button.ft->IsSame()) ChangeValue(temp->button.ft.value(), forceReplace), change = true;
			if (temp->button.fill.has_value() && !temp->button.fill->IsSame()) ChangeColor(temp->button.fill.value(), forceReplace), change = true;
			if (temp->button.frame.has_value() && !temp->button.frame->IsSame()) ChangeColor(temp->button.frame.value(), forceReplace), change = true;
			if (temp->button.framePct.has_value() && !temp->button.framePct->IsSame()) ChangePct(temp->button.framePct.value(), forceReplace), change = true;
			if (temp->button.frameLightPct.has_value() && !temp->button.frameLightPct->IsSame()) ChangePct(temp->button.frameLightPct.value(), forceReplace), change = true;
			if (!temp->button.pct.IsSame()) ChangePct(temp->button.pct, forceReplace), change = true;
		}
		{
			bool forceReplace = false, change = false;;
			if (temp->icon.forceReplace) temp->icon.forceReplace = false, forceReplace = true;
			if (temp->icon.AdvanceContentTransition(animationDtSeconds, currentAnimationSpeedRate))
			{
				needRendering = true;
			}

			if (!temp->icon.enable.IsSame()) ChangeState(temp->icon.enable, forceReplace), change = true;
			if (!temp->icon.x.IsSame()) ChangeValue(temp->icon.x, forceReplace), change = true;
			if (!temp->icon.y.IsSame()) ChangeValue(temp->icon.y, forceReplace), change = true;
			if (!temp->icon.w.IsSame()) ChangeValue(temp->icon.w, forceReplace), change = true;
			if (!temp->icon.h.IsSame()) ChangeValue(temp->icon.h, forceReplace), change = true;
			if (!temp->icon.angle.IsSame()) ChangeValue(temp->icon.angle, forceReplace), change = true;
			if (!temp->icon.svg.IsSame()) ChangeString(temp->icon.svg, forceReplace), change = true;
			if (temp->icon.color1.has_value() && !temp->icon.color1->IsSame()) ChangeColor(temp->icon.color1.value(), forceReplace), change = true;
			if (temp->icon.color2.has_value() && !temp->icon.color2->IsSame()) ChangeColor(temp->icon.color2.value(), forceReplace), change = true;
			if (!temp->icon.pct.IsSame()) ChangePct(temp->icon.pct, forceReplace), change = true;
		}

		{
			bool forceReplace = false, change = false;;
			if (temp->name.forceReplace) temp->name.forceReplace = false, forceReplace = true;
			if (temp->name.AdvanceContentTransition(
				animationDtSeconds, currentAnimationSpeedRate))
			{
				needRendering = true;
			}

			if (!temp->name.enable.IsSame()) ChangeState(temp->name.enable, forceReplace), change = true;
			if (!temp->name.x.IsSame()) ChangeValue(temp->name.x, forceReplace), change = true;
			if (!temp->name.y.IsSame()) ChangeValue(temp->name.y, forceReplace), change = true;
			if (!temp->name.w.IsSame()) ChangeValue(temp->name.w, forceReplace), change = true;
			if (!temp->name.h.IsSame()) ChangeValue(temp->name.h, forceReplace), change = true;
			if (!temp->name.size.IsSame()) ChangeValue(temp->name.size, forceReplace), change = true;
			if (!temp->name.content.IsSame()) ChangeString(temp->name.content, forceReplace), change = true;
			if (!temp->name.color.IsSame()) ChangeColor(temp->name.color, forceReplace), change = true;
			if (!temp->name.pct.IsSame()) ChangePct(temp->name.pct, forceReplace), change = true;
		}
	};
	for (int id = 0; id < state.barButtonSet.tot; id++)
		UpdateRegisteredButtonAnimation(
			state.barButtonSet.buttonList.Get(id), false);
	BarMoreButtonSnapshotClass animatedMoreSnapshot =
		state.barButtonSet.GetMoreButtonSnapshot();
	for (const shared_ptr<BarButtonClass>& button :
		animatedMoreSnapshot.explicitMore)
		UpdateRegisteredButtonAnimation(button.get(), true);
	for (const shared_ptr<BarButtonClass>& button :
		animatedMoreSnapshot.forcedOverflow)
		UpdateRegisteredButtonAnimation(button.get(), true);

	// 提示控件全部从动画中的粗细区域派生，换边时随面板收拢到叹号锚点。
	{
		auto mainButton =
			state.superellipseMap[BarUISetSuperellipseEnum::MainButton];
		mainButton->UpInh(BarUiInheritClass(
			mainButton->x.val - mainButton->w.val / 2.0,
			mainButton->y.val - mainButton->h.val / 2.0));
		auto mainBar = state.shapeMap[BarUISetShapeEnum::MainBar];
		mainBar->Inherit(BarUiInheritEnum::Center, *mainButton);
		auto drawButton =
			state.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Draw)];
		drawButton->button.Inherit(
			BarUiInheritEnum::CenterFromTopLeft, *mainBar);
		auto panel = state.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
		panel->Inherit(BarUiInheritEnum::Center, drawButton->button);
		auto region =
			state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
		BarUiInheritClass regionInherit =
			region->Inherit(BarUiInheritEnum::TopLeft, *panel);
		auto adjust =
			state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
		BarUiInheritClass adjustInherit =
			adjust->Inherit(BarUiInheritEnum::TopLeft, *panel);
		auto thicknessDisplay =
			state.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay];

		auto previewGeometry = CalculateBarThicknessPreviewGeometry(
			*panel, *region, regionInherit, *adjust, adjustInherit);
		double panelScale = previewGeometry.panelScale;
		double panelExpandedProgress = clamp(
			(panelScale - BarDrawAttributeCompactScale)
			/ (1.0 - BarDrawAttributeCompactScale), 0.0, 1.0);
		double previewSide = previewGeometry.previewSide;
		auto thicknessAdjustArrow = state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust];
		bool thicknessOpensBelow = previewSide > 0.000001
			|| (abs(previewSide) <= 0.000001
				&& static_cast<bool>(state.barState.widgetPosition.primaryBar));
		double thicknessCollapsedAngle = thicknessOpensBelow ? 180.0 : 0.0;
		bool thicknessViewExpanded = state.barState.drawAttributeBar
			.thicknessViewMode != ThicknessViewMode::Preview;
		double thicknessTargetAngle =
			thicknessViewExpanded
				? 180.0 - thicknessCollapsedAngle
				: thicknessCollapsedAngle;
		if (forNum == 1 || !state.barState.drawAttribute)
			thicknessAdjustArrow->angle.SetDirect(thicknessTargetAngle);
		else thicknessAdjustArrow->angle.SetTar(
			thicknessTargetAngle, BarUiDefaultOperationDur);
		double previewAreaHeight = max(0.0,
			previewGeometry.previewBottom
				- previewGeometry.previewTop);
		double previewTop = previewGeometry.previewTop;
		double contentOpacity = clamp(
			static_cast<double>(thicknessDisplay->pct.val), 0.0, 1.0);
		auto sliderHit = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderHit];
		auto sliderThumb = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderThumb];
		if (previewGeometry.valid)
		{
			// 命中框只覆盖预览区，明确排除粗细文字与按钮控制行。
			sliderHit->x.SetDirect(
				previewGeometry.previewLeft - panel->inhX);
			sliderHit->y.SetDirect(
				previewGeometry.previewTop - panel->inhY);
			sliderHit->w.SetDirect(
				previewGeometry.previewRight
					- previewGeometry.previewLeft);
			sliderHit->h.SetDirect(previewAreaHeight);
			sliderHit->pct.SetDirect(0.0);
			sliderHit->Inherit(BarUiInheritEnum::TopLeft, *panel);

double baseThumbDiameter =
					BarThicknessSliderThumbDiameter * panelScale;
				double thumbTravel = max(0.0,
					previewGeometry.trackRight
						- previewGeometry.trackLeft
						- baseThumbDiameter);
				// 使用独立归一化动画值，笔形切换与快捷粗细都会平滑移动圆点。
				double thumbNormalized = clamp(
					static_cast<double>(
						state.drawAttributeThicknessSliderNormalized.val),
					0.0, 1.0);
				double thumbCenterX =
					previewGeometry.trackLeft
					+ baseThumbDiameter / 2.0
					+ thumbTravel * thumbNormalized;
			double thumbScale = max(0.0, static_cast<double>(
				state.drawAttributeThicknessSliderThumbScale.val));
			double thumbDiameter =
				baseThumbDiameter * thumbScale;
			sliderThumb->x.SetDirect(
				thumbCenterX - thumbDiameter / 2.0
					- panel->inhX);
			sliderThumb->y.SetDirect(
				ResolveThicknessSliderCenterY(state, previewGeometry)
					- thumbDiameter / 2.0 - panel->inhY);
			sliderThumb->w.SetDirect(thumbDiameter);
			sliderThumb->h.SetDirect(thumbDiameter);
			sliderThumb->pct.SetDirect(clamp(
				static_cast<double>(
					state.drawAttributeThicknessSliderThumbOpacity.val)
					* contentOpacity, 0.0, 1.0));
			sliderThumb->Inherit(
				BarUiInheritEnum::TopLeft, *panel);
		}
		else
		{
			sliderHit->w.SetDirect(0.0);
			sliderHit->h.SetDirect(0.0);
			sliderHit->pct.SetDirect(0.0);
			sliderThumb->w.SetDirect(0.0);
			sliderThumb->h.SetDirect(0.0);
			sliderThumb->pct.SetDirect(0.0);
		}
		// 扩展入口与菜单都使用当前选中笔型的动画几何，资格失效时立即清除命中。
		bool annotationCapability =
			PenModeSupportsAnnotationLine(stateMode.Pen.ModeSelect);
		bool extensionInteractive = state.barState.drawAttribute && !state.barState.fold
			&& annotationCapability;
		// 面板收拢时视觉继续跟随当前透明度；命中仍由目标态立即关闭。
		bool extensionVisualVisible = annotationCapability
			&& contentOpacity > 0.000001;
		auto GetPenTypeShape = [&](PenModeSelectEnum mode)
			-> shared_ptr<BarUiShapeClass>
		{
			if (mode == PenModeSelectEnum::IdtPenHighlighter1)
				return state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_Highlight1];
			if (mode == PenModeSelectEnum::IdtPenBrush1)
				return state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_Brush1];
			return nullptr;
		};
		shared_ptr<BarUiShapeClass> selectedPenTypeShape =
			GetPenTypeShape(stateMode.Pen.ModeSelect);
		double triggerX = selectedPenTypeShape
			? selectedPenTypeShape->x.val
				+ BarDrawAttributePenTypeExtensionDividerX * panelScale
			: (BarDrawAttributePenTypeLeft
				+ BarDrawAttributePenTypeExtensionDividerX) * panelScale;
					double triggerY = selectedPenTypeShape
						? static_cast<double>(selectedPenTypeShape->y.val) : 0.0;
		double triggerWidth = BarDrawAttributePenTypeExtensionWidth * panelScale;
		double triggerHeight = BarDrawAttributePenTypeButtonHeight * panelScale;
		auto extensionHit = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit];
		auto extensionDivider = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionDivider];
		auto extensionArrow = state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_PenTypeExtensionArrow];
		auto SetLocalGeometry = [&](const shared_ptr<BarUiShapeClass>& shape,
			double x, double y, double width, double height)
		{
			shape->x.SetDirect(x);
			shape->y.SetDirect(y);
			shape->w.SetDirect(max(0.0, width));
			shape->h.SetDirect(max(0.0, height));
			if (shape->rw.has_value()) shape->rw->SetDirect(4.0 * panelScale);
			if (shape->rh.has_value()) shape->rh->SetDirect(4.0 * panelScale);
			if (shape->ft.has_value()) shape->ft->SetDirect(panelScale);
		};
		SetLocalGeometry(extensionHit, triggerX, triggerY,
			extensionInteractive ? triggerWidth : 0.0,
			extensionInteractive ? triggerHeight : 0.0);
		extensionHit->Inherit(BarUiInheritEnum::TopLeft, *panel);
		SetLocalGeometry(extensionDivider, triggerX, triggerY + BarDrawAttributeGap * panelScale,
			extensionVisualVisible ? BarUiDividerWidth * panelScale : 0.0,
			extensionVisualVisible
				? max(0.0, triggerHeight - BarDrawAttributeGap * 2.0 * panelScale)
				: 0.0);
		extensionDivider->rw->SetDirect(BarUiDividerRadius * panelScale);
		extensionDivider->rh->SetDirect(BarUiDividerRadius * panelScale);
		extensionDivider->Inherit(BarUiInheritEnum::TopLeft, *panel);
		extensionArrow->x.SetDirect(
		triggerX + (triggerWidth - 18.0 * panelScale) / 2.0);
		extensionArrow->y.SetDirect(
		triggerY + (triggerHeight - 18.0 * panelScale) / 2.0);
		extensionArrow->w.SetDirect(18.0 * panelScale);
		extensionArrow->h.SetDirect(18.0 * panelScale);
		extensionArrow->pct.SetDirect(
			extensionVisualVisible ? contentOpacity : 0.0);
		extensionArrow->Inherit(BarUiInheritEnum::TopLeft, *panel);

		// 浮窗始终从 Thumb 锚点等比展开；完整布局独立计算，保证圆和文字不被裁切。
		state.drawAttributeThicknessPreviewPopupGeometryValid = false;
		double popupScale = max(0.0, static_cast<double>(
			state.drawAttributeThicknessPreviewPopupProgress.val));
		double fineDialProgress = clamp(static_cast<double>(
			state.drawAttributeThicknessFineDialProgress.val), 0.0, 1.0);
		auto popupSurface = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessPreviewPopupSurface];
		auto popupCircle = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessPreviewPopupCircle];
		auto popupNumber = state.wordMap[
			BarUISetWordEnum::DrawAttributeBar_ThicknessPreviewPopupNumber];
		if (previewGeometry.valid && popupScale > 0.000001
			&& sliderThumb->w.val > 0.0 && sliderThumb->h.val > 0.0)
		{
			int previewThickness = static_cast<int>(lround(clamp(
				static_cast<double>(state.drawAttributePenThickness.val),
				0.0, 999.0)));
			// 整数未变化时复用 DWrite 测量，稳定帧不重复创建 TextLayout。
			if (previewThickness != state.drawAttributeThicknessPreviewMeasuredValue)
			{
				state.drawAttributeThicknessPreviewMeasuredValue = previewThickness;
				state.drawAttributeThicknessPreviewMeasuredText =
					to_wstring(previewThickness);
				state.drawAttributeThicknessPreviewMeasuredSize = state.spec.MeasureText(
					state.drawAttributeThicknessPreviewMeasuredText,
					BarThicknessPreviewNumberFontSize,
					DWRITE_FONT_WEIGHT_BOLD);
			}
			const wstring& previewText =
				state.drawAttributeThicknessPreviewMeasuredText;
			const D2D1_SIZE_F& previewTextSize =
				state.drawAttributeThicknessPreviewMeasuredSize;
			double textWidth = max(1.0,
				static_cast<double>(previewTextSize.width));
			double textHeight = max(1.0,
				static_cast<double>(previewTextSize.height));
			double circleDiameter = max(0.0,
				static_cast<double>(state.drawAttributePenThickness.val)
					/ max(0.000001,
						static_cast<double>(frameZoom)));
			bool numberFitsInside = circleDiameter
				>= max(textWidth, textHeight)
					+ BarThicknessPreviewNumberInset * 2.0;
			state.drawAttributeThicknessPreviewNumberInsideProgress.SetTar(
				numberFitsInside ? 1.0 : 0.0,
				BarThicknessPreviewNumberAnimationDur);
			if (!state.drawAttributeThicknessPreviewNumberInsideProgress.IsSame())
				ChangeValue(
					state.drawAttributeThicknessPreviewNumberInsideProgress, false);
			double numberInsideProgress = clamp(static_cast<double>(
				state.drawAttributeThicknessPreviewNumberInsideProgress.val),
				0.0, 1.0);
			double contentHeight = max(circleDiameter, textHeight);
			double circleTop = (contentHeight - circleDiameter) / 2.0;
			double outsideNumberCenterX = circleDiameter
				+ BarThicknessPreviewNumberGap + textWidth / 2.0;
			double insideNumberCenterX = circleDiameter / 2.0;
			double numberCenterX = outsideNumberCenterX
				+ (insideNumberCenterX - outsideNumberCenterX)
					* numberInsideProgress;
			double numberLeft = numberCenterX - textWidth / 2.0;
			double contentLeft = min(0.0, numberLeft);
			double contentRight = max(circleDiameter,
				numberLeft + textWidth);
			double popupWidth = contentRight - contentLeft
				+ BarThicknessPreviewPopupPadding * 2.0;
			double popupHeight = contentHeight
				+ BarThicknessPreviewPopupPadding * 2.0;

			BarUiInheritClass thumbInherit = sliderThumb->Inherit(
				BarUiInheritEnum::TopLeft, *panel);
			double anchorX = thumbInherit.x + sliderThumb->w.val / 2.0;
			double anchorY = thumbInherit.y + sliderThumb->h.val / 2.0;
			double thumbRadius = min(
				sliderThumb->w.val, sliderThumb->h.val) / 2.0;
			// 浮窗贴着 Thumb 外侧留 10 DIP，可与属性面板局部重叠。
			double sliderTargetCenterY = anchorY + previewGeometry.previewSide
				* (thumbRadius + BarThicknessPreviewPopupThumbGap
					+ popupHeight / 2.0);

			// Slider 端继续使用既有 pen-type safe bound；FineDial 端不读取该边界。
			double sliderTargetCenterX = anchorX;
			if (fineDialProgress < 0.999999)
			{
				double penTypeSafeRight = numeric_limits<double>::infinity();
				auto IncludePenTypeLeft = [&](const auto& widget)
					{
						if (!widget || widget->w.val <= 0.0
							|| widget->h.val <= 0.0)
							return;
						BarUiInheritClass inherit = widget->Inherit(
							BarUiInheritEnum::TopLeft, *panel);
						penTypeSafeRight = min(penTypeSafeRight,
							static_cast<double>(inherit.x)
								- BarThicknessPreviewAvoidGap);
					};
				IncludePenTypeLeft(state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_Brush1]);
				IncludePenTypeLeft(state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_Highlight1]);
				IncludePenTypeLeft(state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu]);
				if (!isfinite(penTypeSafeRight))
					penTypeSafeRight = panel->inhX
						+ BarDrawAttributePenTypeLeft * panelScale
						- BarThicknessPreviewAvoidGap;
				sliderTargetCenterX = min(anchorX,
					penTypeSafeRight - popupWidth / 2.0);
				double safePopupScale = max(0.000001, popupScale);
				double maxRenderedCenterX = anchorX
					+ (penTypeSafeRight - anchorX) / safePopupScale
					- popupWidth / 2.0;
				sliderTargetCenterX = min(
					sliderTargetCenterX, maxRenderedCenterX);
			}

			double fineTargetCenterX =
				(previewGeometry.trackLeft + previewGeometry.trackRight) / 2.0;
			// 以连续 previewSide 穿过换边中点，避免 Popup 在上下端点间跳跃。
			double panelCenterY = panel->inhY + panel->h.val / 2.0;
			double fineTargetCenterY = panelCenterY
				+ previewGeometry.previewSide
					* (panel->h.val / 2.0
						+ BarThicknessFineDialPopupPanelGapDip * panelScale
						+ popupHeight / 2.0);
			double targetCenterX = sliderTargetCenterX
				+ (fineTargetCenterX - sliderTargetCenterX)
					* fineDialProgress;
			double targetCenterY = sliderTargetCenterY
				+ (fineTargetCenterY - sliderTargetCenterY)
					* fineDialProgress;
			// FineDial 端仅保留窗口边界保护，外侧间隙不再受笔型区域挤压。
			double logicalWindowWidth = static_cast<double>(state.barWindow.w)
				/ max(0.000001, static_cast<double>(frameZoom));
			double logicalWindowHeight = static_cast<double>(state.barWindow.h)
				/ max(0.000001, static_cast<double>(frameZoom));
			targetCenterX = clamp(targetCenterX,
				popupWidth / 2.0,
				max(popupWidth / 2.0,
					logicalWindowWidth - popupWidth / 2.0));
			targetCenterY = clamp(targetCenterY,
				popupHeight / 2.0,
				max(popupHeight / 2.0,
					logicalWindowHeight - popupHeight / 2.0));
			double popupPivotX = anchorX;
			double popupPivotY = anchorY;
			if (state.drawAttributeThicknessPreviewPopupExitPositionLatched)
			{
				popupPivotX = state.drawAttributeThicknessPreviewPopupExitCenter.x;
				popupPivotY = state.drawAttributeThicknessPreviewPopupExitCenter.y;
				double retargetProgress =
					state.drawAttributeThicknessPreviewPopupTargetVisible
					? clamp(static_cast<double>(
						state.drawAttributeThicknessPreviewPopupRetargetProgress.val),
						0.0, 1.0)
					: 0.0;
				// 退出固定完整 Popup 中心；快速反转再从该中心连续移向新目标。
				targetCenterX = popupPivotX
					+ (targetCenterX - popupPivotX) * retargetProgress;
				targetCenterY = popupPivotY
					+ (targetCenterY - popupPivotY) * retargetProgress;
			}
			double targetLeft = targetCenterX - popupWidth / 2.0;
			double targetRight = targetCenterX + popupWidth / 2.0;
			double targetTop = targetCenterY - popupHeight / 2.0;
			double targetBottom = targetCenterY + popupHeight / 2.0;
			double animatedLeft = popupPivotX
				+ (targetLeft - popupPivotX) * popupScale;
			double animatedTop = popupPivotY
				+ (targetTop - popupPivotY) * popupScale;
			double animatedRight = popupPivotX
				+ (targetRight - popupPivotX) * popupScale;
			double animatedBottom = popupPivotY
				+ (targetBottom - popupPivotY) * popupScale;
			double popupOpacity = clamp(popupScale, 0.0, 1.0);

			popupSurface->x.SetDirect(animatedLeft - panel->inhX);
			popupSurface->y.SetDirect(animatedTop - panel->inhY);
			popupSurface->w.SetDirect(max(0.0, animatedRight - animatedLeft));
			popupSurface->h.SetDirect(max(0.0, animatedBottom - animatedTop));
			popupSurface->rw->SetDirect(4.0 * popupScale);
			popupSurface->rh->SetDirect(4.0 * popupScale);
			popupSurface->ft->SetDirect(popupScale);
			popupSurface->fill->SetDirect(
				GetThemeColor(BarThemeColorEnum::Surface));
			popupSurface->frame->SetDirect(
				GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			popupSurface->pct.SetDirect(
				BarDrawAttributeSurfaceOpacity * popupOpacity);
			popupSurface->framePct->SetDirect(
				BarThicknessTooltipFrameOpacity * popupOpacity);
			popupSurface->frameLightPct->SetDirect(popupOpacity);
			popupSurface->Inherit(BarUiInheritEnum::TopLeft, *panel);

			double circleTargetLeft = targetLeft
				+ BarThicknessPreviewPopupPadding - contentLeft;
			double circleTargetTop = targetTop
				+ BarThicknessPreviewPopupPadding + circleTop;
			double circleAnimatedLeft = popupPivotX
				+ (circleTargetLeft - popupPivotX) * popupScale;
			double circleAnimatedTop = popupPivotY
				+ (circleTargetTop - popupPivotY) * popupScale;
			popupCircle->x.SetDirect(circleAnimatedLeft - panel->inhX);
			popupCircle->y.SetDirect(circleAnimatedTop - panel->inhY);
			popupCircle->w.SetDirect(circleDiameter * popupScale);
			popupCircle->h.SetDirect(circleDiameter * popupScale);
			popupCircle->rw->SetDirect(circleDiameter * popupScale / 2.0);
			popupCircle->rh->SetDirect(circleDiameter * popupScale / 2.0);
			popupCircle->fill->SetDirect(RGB(255, 255, 255));
			popupCircle->pct.SetDirect(popupOpacity);
			popupCircle->Inherit(BarUiInheritEnum::TopLeft, *panel);

			double numberTargetLeft = targetLeft
				+ BarThicknessPreviewPopupPadding
				+ numberLeft - contentLeft;
			double numberTargetTop = targetTop
				+ BarThicknessPreviewPopupPadding
				+ (contentHeight - textHeight) / 2.0;
			popupNumber->w.SetDirect(textWidth);
			popupNumber->h.SetDirect(textHeight);
			popupNumber->size.SetDirect(
				BarThicknessPreviewNumberFontSize);
			popupNumber->content.SetVal(previewText);
			popupNumber->content.SetTar(previewText);
			popupNumber->color.SetDirect(MixBarUiColor(
				GetThemeColor(BarThemeColorEnum::TextPrimary),
				RGB(0, 0, 0), numberInsideProgress));
			popupNumber->pct.SetDirect(popupOpacity);

			state.drawAttributeThicknessPreviewNumberRect = D2D1::RectF(
				static_cast<FLOAT>(numberTargetLeft),
				static_cast<FLOAT>(numberTargetTop),
				static_cast<FLOAT>(numberTargetLeft + textWidth),
				static_cast<FLOAT>(numberTargetTop + textHeight));
			state.drawAttributeThicknessPreviewPopupAnchor = D2D1::Point2F(
				static_cast<FLOAT>(popupPivotX),
				static_cast<FLOAT>(popupPivotY));
			state.drawAttributeThicknessPreviewPopupScale = popupScale;
			state.drawAttributeThicknessPreviewPopupGeometryValid = true;
			state.drawAttributeThicknessPreviewPopupRenderedCenter = D2D1::Point2F(
				static_cast<FLOAT>((animatedLeft + animatedRight) / 2.0),
				static_cast<FLOAT>((animatedTop + animatedBottom) / 2.0));
			state.drawAttributeThicknessPreviewPopupRenderedCenterValid = true;
		}
		else
		{
			state.drawAttributeThicknessPreviewPopupRenderedCenterValid = false;
			popupSurface->pct.SetDirect(0.0);
			popupSurface->framePct->SetDirect(0.0);
			popupSurface->frameLightPct->SetDirect(0.0);
			popupSurface->w.SetDirect(0.0);
			popupSurface->h.SetDirect(0.0);
			popupCircle->pct.SetDirect(0.0);
			popupCircle->w.SetDirect(0.0);
			popupCircle->h.SetDirect(0.0);
			popupNumber->pct.SetDirect(0.0);
		}

		// 菜单方向在打开时锁存；进度从触发器中心向远离主栏一侧移动。
		bool menuOpenBelow = state.barState.drawAttributeBar.penTypeMenuDirectionLocked
			? static_cast<bool>(state.barState.drawAttributeBar.penTypeMenuOpenBelow)
			: static_cast<bool>(state.barState.widgetPosition.primaryBar);
		PenModeSelectEnum menuAnchorMode =
			state.barState.drawAttributeBar.penTypeMenuDirectionLocked
			? static_cast<PenModeSelectEnum>(static_cast<int>(
				state.barState.drawAttributeBar.penTypeMenuAnchorMode))
			: stateMode.Pen.ModeSelect;
		auto menuAnchorShape = GetPenTypeShape(menuAnchorMode);
		double menuTriggerX = menuAnchorShape
			? menuAnchorShape->x.val
				+ BarDrawAttributePenTypeExtensionDividerX * panelScale
			: triggerX;
			double menuTriggerY = menuAnchorShape
			? static_cast<double>(menuAnchorShape->y.val) : triggerY;
		// 保留 Back 的上溢出，只对透明度做单独裁剪，避免吞掉回弹。
		double menuProgress = max(0.0, static_cast<double>(
			state.drawAttributePenTypeMenuProgress.val))
			* panelExpandedProgress;
		double menuOpacity = clamp(menuProgress, 0.0, 1.0);
		double menuWidth = BarDrawAttributePenTypeButtonWidth * panelScale;
		double menuHeight = BarDrawAttributePenTypeMenuHeight * panelScale;
		double menuTargetLeft = menuTriggerX + triggerWidth - menuWidth;
		double menuAnchorX = menuTriggerX + triggerWidth / 2.0;
		double menuAnchorY = menuTriggerY + triggerHeight / 2.0;
		double menuTargetCenterY = menuOpenBelow
			? menuTriggerY + triggerHeight + BarDrawAttributeGap * panelScale
				+ menuHeight / 2.0
			: menuTriggerY - BarDrawAttributeGap * panelScale
				- menuHeight / 2.0;
		double menuCenterX = menuAnchorX
			+ (menuTargetLeft + menuWidth / 2.0 - menuAnchorX)
				* menuProgress;
		double menuCenterY = menuAnchorY
			+ (menuTargetCenterY - menuAnchorY) * menuProgress;
		double menuDrawWidth = menuWidth * menuProgress;
		double menuDrawHeight = menuHeight * menuProgress;
		double menuLeft = menuCenterX - menuDrawWidth / 2.0;
		double menuTop = menuCenterY - menuDrawHeight / 2.0;

		auto SetSurfaceDerived = [&](const shared_ptr<BarUiShapeClass>& shape,
			double x, double y, double width, double height, double opacity)
			{
				shape->x.SetDirect(x);
				shape->y.SetDirect(y);
				shape->w.SetDirect(max(0.0, width));
				shape->h.SetDirect(max(0.0, height));
				if (shape->rw.has_value())
					shape->rw->SetDirect(4.0 * panelScale);
				if (shape->rh.has_value())
					shape->rh->SetDirect(4.0 * panelScale);
				if (shape->ft.has_value())
					shape->ft->SetDirect(panelScale);
				shape->pct.SetDirect(
					BarThicknessTooltipFillOpacity * opacity);
				if (shape->framePct.has_value())
					shape->framePct->SetDirect(
						BarThicknessTooltipFrameOpacity * opacity);
				if (shape->frameLightPct.has_value())
					shape->frameLightPct->SetDirect(opacity);
			};
		auto SetHitDerived = [&](const shared_ptr<BarUiShapeClass>& shape,
			double x, double y, double size, bool clearVisual = true)
			{
				shape->x.SetDirect(x);
				shape->y.SetDirect(y);
				shape->w.SetDirect(max(0.0, size));
				shape->h.SetDirect(max(0.0, size));
				if (clearVisual) shape->pct.SetDirect(0.0);
			};

		bool menuVisualVisible = menuProgress > 0.000001;
		bool menuInteractive = extensionInteractive
			&& state.barState.drawAttributeBar.penTypeMenuOpen
			&& menuVisualVisible;
		auto menuSurface = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu];
		SetSurfaceDerived(menuSurface, menuLeft, menuTop,
			menuDrawWidth, menuDrawHeight,
			menuVisualVisible ? contentOpacity * menuOpacity : 0.0);
		if (menuSurface->rw.has_value())
			menuSurface->rw->SetDirect(4.0 * panelScale * menuProgress);
		if (menuSurface->rh.has_value())
			menuSurface->rh->SetDirect(4.0 * panelScale * menuProgress);
		if (menuSurface->ft.has_value())
			menuSurface->ft->SetDirect(panelScale * menuProgress);
		menuSurface->Inherit(BarUiInheritEnum::TopLeft, *panel);
		double menuPadding = BarDrawAttributePenTypeMenuPadding * panelScale
			* menuProgress;
		double menuRowHeight = BarDrawAttributePenTypeMenuRowHeight * panelScale
			* menuProgress;
		double menuRowWidth = max(0.0, menuDrawWidth - menuPadding * 2.0);
		auto freeLineRow = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine];
		auto annotationRow = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuAnnotationLine];
		SetLocalGeometry(freeLineRow, menuLeft + menuPadding,
			menuTop + menuPadding, menuRowWidth, menuRowHeight);
		SetLocalGeometry(annotationRow, menuLeft + menuPadding,
			menuTop + menuPadding + menuRowHeight, menuRowWidth, menuRowHeight);
		freeLineRow->Inherit(BarUiInheritEnum::TopLeft, *panel);
		annotationRow->Inherit(BarUiInheritEnum::TopLeft, *panel);
		if (!menuVisualVisible)
		{
			freeLineRow->w.SetDirect(0.0);
			freeLineRow->h.SetDirect(0.0);
			annotationRow->w.SetDirect(0.0);
			annotationRow->h.SetDirect(0.0);
		}
		auto menuFreeWord = state.wordMap[
			BarUISetWordEnum::DrawAttributeBar_PenTypeMenuFreeLine];
		menuFreeWord->x.SetDirect(menuLeft + menuPadding
			+ BarDrawAttributePenTypeMenuCheckAreaWidth
				* panelScale * menuProgress);
		menuFreeWord->y.SetDirect(menuTop + menuPadding);
		menuFreeWord->w.SetDirect(max(0.0, menuRowWidth
			- (BarDrawAttributePenTypeMenuCheckAreaWidth + 4.0)
				* panelScale));
		menuFreeWord->h.SetDirect(menuRowHeight);
		menuFreeWord->size.SetDirect(12.0 * panelScale);
		menuFreeWord->pct.SetDirect(menuVisualVisible ? contentOpacity * menuOpacity : 0.0);
		menuFreeWord->Inherit(BarUiInheritEnum::TopLeft, *panel);
		auto menuCheck = state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_PenTypeMenuCheck];
		menuCheck->x.SetDirect(menuLeft + menuPadding
			+ BarDrawAttributePenTypeMenuCheckInset
				* panelScale * menuProgress);
		menuCheck->y.SetDirect(menuTop + menuPadding
			+ (menuRowHeight - BarDrawAttributePenTypeMenuCheckSize
				* panelScale * menuProgress) / 2.0);
		menuCheck->w.SetDirect(BarDrawAttributePenTypeMenuCheckSize
			* panelScale * menuProgress);
		menuCheck->h.SetDirect(BarDrawAttributePenTypeMenuCheckSize
			* panelScale * menuProgress);
		menuCheck->pct.SetDirect(menuVisualVisible ? contentOpacity * menuOpacity : 0.0);
		menuCheck->Inherit(BarUiInheritEnum::TopLeft, *panel);

		// 标注线行禁用，但问号仍复用原帮助入口和原浮窗状态。
		double annotationOpacity = menuVisualVisible
			? contentOpacity * menuOpacity : 0.0;
		auto annotationLabel = state.wordMap[
			BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel];
		annotationLabel->x.SetDirect(
			// 两行共用固定 Checkmark 保留位，文字始终从同一列左对齐。
			menuLeft + menuPadding
			+ BarDrawAttributePenTypeMenuCheckAreaWidth
				* panelScale * menuProgress);
		annotationLabel->y.SetDirect(
			menuTop + menuPadding + menuRowHeight);
		annotationLabel->w.SetDirect(max(0.0,
			menuRowWidth - (12.0 + 4.0 + BarThicknessTooltipIconSize)
				* panelScale * menuProgress));
		annotationLabel->h.SetDirect(menuRowHeight);
		annotationLabel->size.SetDirect(13.0 * panelScale);
		annotationLabel->pct.SetDirect(annotationOpacity);
		annotationLabel->color.SetDirect(RGB(160, 160, 160));
		annotationLabel->Inherit(BarUiInheritEnum::TopLeft, *panel);

		double annotationInfoX = menuLeft + menuDrawWidth - menuPadding
			- (BarThicknessTooltipIconSize + 4.0 * panelScale) * menuProgress;
		double annotationInfoY = menuTop + menuPadding + menuRowHeight
			+ (menuRowHeight - BarThicknessTooltipIconSize * panelScale * menuProgress)
				/ 2.0;
		auto annotationInfo = state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo];
		annotationInfo->x.SetDirect(annotationInfoX);
		annotationInfo->y.SetDirect(annotationInfoY);
		annotationInfo->w.SetDirect(
			BarThicknessTooltipIconSize * panelScale * menuProgress);
		annotationInfo->h.SetDirect(
			BarThicknessTooltipIconSize * panelScale * menuProgress);
		annotationInfo->pct.SetDirect(annotationOpacity);
		annotationInfo->Inherit(BarUiInheritEnum::TopLeft, *panel);
		auto annotationInfoHit = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit];
			double tooltipHitPadding =
				BarThicknessTooltipHitPadding * panelScale * menuProgress;
			SetHitDerived(annotationInfoHit,
				annotationInfoX - tooltipHitPadding,
				annotationInfoY - tooltipHitPadding,
				(BarThicknessTooltipIconSize
					+ BarThicknessTooltipHitPadding * 2.0)
					* panelScale * menuProgress);
			if (!menuInteractive)
			{
				annotationInfoHit->x.SetDirect(annotationInfoX
					+ BarThicknessTooltipIconSize * panelScale
						* menuProgress / 2.0);
				annotationInfoHit->y.SetDirect(annotationInfoY
					+ BarThicknessTooltipIconSize * panelScale
						* menuProgress / 2.0);
				annotationInfoHit->w.SetDirect(0.0);
				annotationInfoHit->h.SetDirect(0.0);
			}
			annotationInfoHit->Inherit(BarUiInheritEnum::TopLeft, *panel);

		double badgeLowerProgress =
			clamp((previewSide + 1.0) / 2.0, 0.0, 1.0);
		double badgeHeight = BarThicknessTooltipBadgeHeight * panelScale;
		double badgeTopAtUpperEdge = previewTop
			+ BarDrawAttributeGap * panelScale;
		double badgeTopAtLowerEdge = previewTop + previewAreaHeight
			- (BarDrawAttributeGap * panelScale + badgeHeight);
		double badgeTop = badgeTopAtUpperEdge
			+ (badgeTopAtLowerEdge - badgeTopAtUpperEdge)
				* badgeLowerProgress;

		// Hold 组在控制行右对齐；状态先更新，再用短促透明度与回弹稳定视觉。
		double holdStageOpacity = clamp(static_cast<double>(
			state.drawAttributeThicknessHoldExchangeProgress.val), 0.0, 1.0);
		double holdHintOpacity = clamp(
			contentOpacity * holdStageOpacity, 0.0, 1.0);
		double holdLabelHeight = max(0.0,
			static_cast<double>(thicknessAdjust->h.val));
		double holdLabelW = max(0.0,
			static_cast<double>(state.holdLockLabelTextSize.width) * panelScale);
		double holdRingSize = holdLabelHeight
			* BarThicknessHoldRingSizeScale;
		double holdRingGap = BarThicknessHoldRingTextGap
			* (holdLabelHeight
				/ max(1.0, BarThicknessTooltipBadgeHeight));
		// 文字右端固定对齐，圆环在文字左侧紧邻显示。
		double holdLabelX =
			BarDrawAttributeThicknessDividerRight * panelScale - holdLabelW;
		BarUiInheritClass thicknessAdjustInherit = thicknessAdjust->Inherit(
			BarUiInheritEnum::TopLeft, *panel);
		double holdLabelY = thicknessAdjustInherit.y - panel->inhY;
			auto holdLockLabel = state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessHoldLockLabel];
			holdLockLabel->x.SetDirect(holdLabelX);
		holdLockLabel->y.SetDirect(holdLabelY);
			holdLockLabel->w.SetDirect(holdLabelW);
			holdLockLabel->h.SetDirect(holdLabelHeight);
			holdLockLabel->size.SetDirect(13.0 * panelScale);
			holdLockLabel->pct.SetDirect(holdHintOpacity);
			holdLockLabel->contentScale = max(0.0, static_cast<double>(
				state.drawAttributeThicknessHoldGroupScale.val));
			holdLockLabel->contentPct = 1.0;
			COLORREF holdGrayColor = MixBarUiColor(
				GetThemeColor(BarThemeColorEnum::TextPrimary),
				GetThemeColor(BarThemeColorEnum::Surface), 0.45);
			COLORREF holdTextColor = MixBarUiColor(
				holdGrayColor,
				GetThemeColor(BarThemeColorEnum::TextPrimary),
				clamp(static_cast<double>(
					state.drawAttributeThicknessHoldTextMix.val), 0.0, 1.0));
			holdLockLabel->color.SetDirect(holdTextColor);
			holdLockLabel->content.SetVal(L"保持并固定粗细");
			holdLockLabel->content.SetTar(L"保持并固定粗细");

		bool overflowInteractive =
			state.barState.drawAttributeBar.thicknessOverflowHintPresent;
		double badgeMargin =
		BarDrawAttributeThicknessContentInset * panelScale;
		double overflowOpacity = contentOpacity
			* clamp(static_cast<double>(
				state.drawAttributeOverflowBadgeProgress.val), 0.0, 1.0);
		double overflowBadgeW =
			BarThicknessTooltipBadgeHeight * panelScale;
		double overflowBadgeX = region->x.val + region->w.val
			- badgeMargin - overflowBadgeW;
		double overflowBadgeY = badgeTop - panel->inhY;
		auto overflowBadge = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge];
		SetSurfaceDerived(overflowBadge,
			overflowBadgeX, overflowBadgeY,
			overflowBadgeW, overflowBadgeW, overflowOpacity);
		overflowBadge->Inherit(BarUiInheritEnum::TopLeft, *panel);

		double overflowInfoX = overflowBadgeX
			+ (BarThicknessTooltipBadgeHeight
				- BarThicknessTooltipIconSize) / 2.0 * panelScale;
		double overflowInfoY = overflowBadgeY
			+ (BarThicknessTooltipBadgeHeight
				- BarThicknessTooltipIconSize) / 2.0 * panelScale;
		auto overflowInfo = state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo];
		overflowInfo->x.SetDirect(overflowInfoX);
		overflowInfo->y.SetDirect(overflowInfoY);
		overflowInfo->w.SetDirect(
			BarThicknessTooltipIconSize * panelScale);
		overflowInfo->h.SetDirect(
			BarThicknessTooltipIconSize * panelScale);
		overflowInfo->pct.SetDirect(overflowOpacity);
		auto overflowInfoHit = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit];
		double overflowTooltipHitPadding =
			BarThicknessTooltipHitPadding * panelScale;
		SetHitDerived(overflowInfoHit,
			overflowInfoX - overflowTooltipHitPadding,
			overflowInfoY - overflowTooltipHitPadding,
			(BarThicknessTooltipIconSize
				+ BarThicknessTooltipHitPadding * 2.0) * panelScale);
		// Slider 目标建立后立即撤销命中，视觉仍按现有曲线完成退场。
		if (!overflowInteractive || overflowOpacity <= 0.000001)
		{
			overflowInfoHit->x.SetDirect(overflowInfoX
				+ BarThicknessTooltipIconSize * panelScale / 2.0);
			overflowInfoHit->y.SetDirect(overflowInfoY
				+ BarThicknessTooltipIconSize * panelScale / 2.0);
			overflowInfoHit->w.SetDirect(0.0);
			overflowInfoHit->h.SetDirect(0.0);
		}
		overflowInfoHit->Inherit(BarUiInheritEnum::TopLeft, *panel);

		struct PopupDerivedLayout
		{
			double anchorX = 0.0;
			double anchorY = 0.0;
			double targetLeft = 0.0;
			double targetTop = 0.0;
			double baseWidth = 0.0;
			double baseHeight = 0.0;
			double titleHeight = 0.0;
			double bodyHeight = 0.0;
			double progress = 0.0;
			double opacity = 0.0;
		};
		double logicalWindowWidth = frameZoom > 0.0
			? static_cast<double>(state.barWindow.w) / frameZoom : 0.0;
		double logicalWindowHeight = frameZoom > 0.0
			? static_cast<double>(state.barWindow.h) / frameZoom : 0.0;
		auto BuildPopupLayout = [&](double anchorX, double anchorY,
			double width, double height, double titleHeight,
			double bodyHeight, double progress,
			double outwardDirection)
			{
				PopupDerivedLayout layout;
				layout.anchorX = anchorX;
				layout.anchorY = anchorY;
				layout.baseWidth = width;
				layout.baseHeight = height;
				layout.titleHeight = titleHeight;
				layout.bodyHeight = bodyHeight;
				layout.progress = max(0.0, progress)
					* panelExpandedProgress;
				layout.opacity = clamp(layout.progress, 0.0, 1.0);
				layout.targetLeft = clamp(
					anchorX - width / 2.0, BarDrawAttributeGap,
					max(BarDrawAttributeGap,
						logicalWindowWidth - BarDrawAttributeGap - width));
				// 浮窗完整落在徽标外侧，避免从叹号中心展开后压住当前按钮。
				double targetCenterY = anchorY + outwardDirection
					* (BarThicknessTooltipBadgeHeight / 2.0 * panelScale
						+ BarThicknessTooltipPopupGap + height / 2.0);
				layout.targetTop = clamp(
					targetCenterY - height / 2.0,
					BarDrawAttributeGap,
					max(BarDrawAttributeGap,
						logicalWindowHeight - BarDrawAttributeGap - height));
				return layout;
			};

		double annotationAnchorX = annotationInfo->inhX
			+ annotationInfo->w.val / 2.0;
		double annotationAnchorY = annotationInfo->inhY
			+ annotationInfo->h.val / 2.0;
		double overflowAnchorX = overflowInfoHit->inhX
			+ overflowInfoHit->w.val / 2.0;
		double overflowAnchorY = overflowInfoHit->inhY
			+ overflowInfoHit->h.val / 2.0;
		PopupDerivedLayout annotationPopupLayout = BuildPopupLayout(
			annotationAnchorX, annotationAnchorY,
			state.annotationPopupWidth, state.annotationPopupHeight,
			state.annotationPopupTitleSize.height,
			state.annotationPopupBodySize.height,
			state.drawAttributeAnnotationPopupProgress.val,
			menuOpenBelow ? 1.0 : -1.0);
		PopupDerivedLayout overflowPopupLayout = BuildPopupLayout(
			overflowAnchorX, overflowAnchorY,
			state.overflowPopupWidth, state.overflowPopupHeight,
			state.overflowPopupTitleSize.height,
			state.overflowPopupBodySize.height,
			state.drawAttributeOverflowPopupProgress.val,
			previewSide);

		auto ApplyPopupLayout = [&](const PopupDerivedLayout& layout,
			BarUISetShapeEnum popupShapeType,
			BarUISetWordEnum popupTitleType,
			BarUISetWordEnum popupBodyType,
			BarUISetShapeEnum closeHitType,
			BarUISetSvgEnum closeSvgType,
			bool closeVisible)
			{
				double scale = max(0.0, layout.progress);
				double width = layout.baseWidth * scale;
				double height = layout.baseHeight * scale;
				double absoluteX = layout.anchorX
					+ (layout.targetLeft - layout.anchorX) * scale;
				double absoluteY = layout.anchorY
					+ (layout.targetTop - layout.anchorY) * scale;
				double localX = absoluteX - panel->inhX;
				double localY = absoluteY - panel->inhY;
				auto popup = state.shapeMap[popupShapeType];
				SetSurfaceDerived(popup, localX, localY,
					width, height, layout.opacity);
				// 浮窗从叹号弹性展开时，圆角和边框也按自身比例等比生长。
				if (popup->rw.has_value())
					popup->rw->SetDirect(4.0 * scale);
				if (popup->rh.has_value())
					popup->rh->SetDirect(4.0 * scale);
				if (popup->ft.has_value())
					popup->ft->SetDirect(scale);
				popup->Inherit(BarUiInheritEnum::TopLeft, *panel);

				double padding =
					BarThicknessTooltipPadding * scale;
				double contentWidth = max(0.0,
					width - (BarThicknessTooltipPadding * 2.0
						+ BarThicknessTooltipCloseReserve) * scale);
				auto title = state.wordMap[popupTitleType];
				title->x.SetDirect(localX + padding);
				title->y.SetDirect(localY + padding);
				title->w.SetDirect(contentWidth);
				title->h.SetDirect(layout.titleHeight * scale);
				title->size.SetDirect(
					BarThicknessTooltipTitleFontSize * scale);
				title->pct.SetDirect(layout.opacity);
				title->Inherit(BarUiInheritEnum::TopLeft, *panel);

				auto body = state.wordMap[popupBodyType];
				body->x.SetDirect(localX + padding);
				body->y.SetDirect(localY + padding
					+ (layout.titleHeight
						+ BarThicknessTooltipLineGap) * scale);
				body->w.SetDirect(contentWidth);
				body->h.SetDirect(layout.bodyHeight * scale);
				body->size.SetDirect(
					BarThicknessTooltipBodyFontSize * scale);
				body->pct.SetDirect(layout.opacity);
				body->Inherit(BarUiInheritEnum::TopLeft, *panel);

double closeButtonSize =
						BarThicknessTooltipCloseButtonSize * scale;
					double closeIconSize =
						BarThicknessTooltipIconSize * scale;
				double closeX =
					localX + width - padding - closeButtonSize;
				double closeY = localY + padding;
				auto closeHit = state.shapeMap[closeHitType];
				SetHitDerived(
					closeHit, closeX, closeY, closeButtonSize, false);
				if (closeHit->rw.has_value())
					closeHit->rw->SetDirect(4.0 * scale);
				if (closeHit->rh.has_value())
					closeHit->rh->SetDirect(4.0 * scale);
				closeHit->Inherit(BarUiInheritEnum::TopLeft, *panel);
				auto closeSvg = state.svgMap[closeSvgType];
				closeSvg->x.SetDirect(closeX
					+ (closeButtonSize - closeIconSize) / 2.0);
				closeSvg->y.SetDirect(closeY
					+ (closeButtonSize - closeIconSize) / 2.0);
				closeSvg->w.SetDirect(closeIconSize);
				closeSvg->h.SetDirect(closeIconSize);
				closeSvg->pct.SetDirect(
					closeVisible ? layout.opacity : 0.0);
				closeSvg->Inherit(BarUiInheritEnum::TopLeft, *panel);
			};
		auto KeepCloseVisibleThroughCollapse =
			[](bool pinned, const PopupDerivedLayout& layout,
				bool& closeVisible)
			{
				if (pinned) closeVisible = true;
				else if (layout.progress <= 0.000001)
					closeVisible = false;
				return closeVisible;
			};
		ApplyPopupLayout(annotationPopupLayout,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
			BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText,
			BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
			BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose,
			KeepCloseVisibleThroughCollapse(
				state.barState.drawAttributeBar.thicknessAnnotationPinned,
				annotationPopupLayout,
				state.drawAttributeAnnotationCloseVisible));
		ApplyPopupLayout(overflowPopupLayout,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
			BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText,
			BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
			BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose,
			KeepCloseVisibleThroughCollapse(
				state.barState.drawAttributeBar.thicknessOverflowPinned,
				overflowPopupLayout,
				state.drawAttributeOverflowCloseVisible));

		// 颜色面板直接使用同一全屏 floating_window 的逻辑坐标，不创建或移动额外 HWND。
		auto customSwatch = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorSelect12];
		auto pickerPanel = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel];
		auto pickerPalette = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette];
		auto pickerToneHit = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle];
		auto pickerCloseHit = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit];
		auto pickerPreview = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerPreviewBubble];
		auto pickerHold = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerHoldHint];
		double pickerProgress = static_cast<double>(
			state.drawAttributeColorPickerProgress.val);
		// 从色块锚点的紧凑态展开，放大几何变化以完整复用绘制属性窗口的回弹。
		double pickerScale = max(0.01, BarColorPickerCompactScale
			+ (1.0 - BarColorPickerCompactScale) * pickerProgress);
		double pickerWidth = max(1.0,
			BarColorPickerCompactWidth
			+ (BarColorPickerPanelWidth - BarColorPickerCompactWidth)
				* pickerProgress);
		double pickerHeight = max(1.0,
			BarColorPickerCompactHeight
			+ (BarColorPickerPanelHeight - BarColorPickerCompactHeight)
				* pickerProgress);
		double swatchCenterX = customSwatch->inhX
			+ customSwatch->w.val / 2.0;
		// 与绘制属性同向：primaryBar 时向下，倒转后向上；不按屏幕边夹紧，可越出可视区。
		bool openBelowSwatch = state.barState.widgetPosition.primaryBar;
		double targetPickerLeft =
			swatchCenterX - BarColorPickerPanelWidth / 2.0;
		double targetPickerTop = openBelowSwatch
			? customSwatch->inhY + customSwatch->h.val + BarColorPickerPanelGap
			: customSwatch->inhY - BarColorPickerPanelGap
				- BarColorPickerPanelHeight;
		double targetPickerCenterX = targetPickerLeft
			+ BarColorPickerPanelWidth / 2.0;
		double targetPickerCenterY = targetPickerTop
			+ BarColorPickerPanelHeight / 2.0;
		// 紧凑态贴在入口外侧，下方展开时直接从圆形入口下缘出现。
		double compactPickerCenterY = openBelowSwatch
			? customSwatch->inhY + customSwatch->h.val
				+ BarColorPickerPanelGap + BarColorPickerCompactHeight / 2.0
			: customSwatch->inhY - BarColorPickerPanelGap
				- BarColorPickerCompactHeight / 2.0;
		double pickerCenterX = swatchCenterX
			+ (targetPickerCenterX - swatchCenterX) * pickerProgress;
		double pickerCenterY = compactPickerCenterY
			+ (targetPickerCenterY - compactPickerCenterY) * pickerProgress;
		double pickerLeft = pickerCenterX - pickerWidth / 2.0;
		double pickerTop = pickerCenterY - pickerHeight / 2.0;
		double pickerOpacity = clamp(pickerProgress, 0.0, 1.0);
	// 面板圆角/底色/边框透明度与绘制属性栏保持同一套 Surface 规范。
	double pickerPanelRadius = 8.0 * pickerScale;
	double pickerControlRadius = 4.0 * pickerScale;
	pickerPanel->w.SetDirect(pickerWidth);
	pickerPanel->h.SetDirect(pickerHeight);
	pickerPanel->rw->SetDirect(pickerPanelRadius);
	pickerPanel->rh->SetDirect(pickerPanelRadius);
	pickerPanel->pct.SetDirect(
		BarDrawAttributeSurfaceOpacity * pickerOpacity);
	pickerPanel->framePct->SetDirect(0.18 * pickerOpacity);
	pickerPanel->frameLightPct->SetDirect(pickerOpacity);
	pickerPanel->UpInh(BarUiInheritClass(pickerLeft, pickerTop));

	auto SetAbsoluteHit = [&](const shared_ptr<BarUiShapeClass>& hit,
		double left, double top, double width, double height)
		{
			hit->w.SetDirect(max(0.0, width));
			hit->h.SetDirect(max(0.0, height));
			hit->pct.SetDirect(pickerOpacity);
			hit->UpInh(BarUiInheritClass(left, top));
		};
	double paletteLeft = pickerLeft
		+ BarColorPickerPaletteInset * pickerScale;
	double paletteTop = pickerTop
		+ BarColorPickerPaletteTop * pickerScale;
	double paletteWidth = BarColorPickerPaletteWidth * pickerScale;
	double paletteHeight = BarColorPickerPaletteHeight * pickerScale;
	SetAbsoluteHit(pickerPalette, paletteLeft, paletteTop,
		paletteWidth, paletteHeight);
	pickerPalette->rw->SetDirect(pickerControlRadius);
	pickerPalette->rh->SetDirect(pickerControlRadius);
	// 顶部色系/预览/关闭与粗细快速调节按钮同高（30px）。
	double chromeHeight = BarColorPickerChromeHeight * pickerScale;
	double chromeTop = pickerTop + BarColorPickerChromeTop * pickerScale;
	// 色系/关闭按钮只更新几何，pct 交给悬停与按压状态机，避免每帧 SetDirect 冲掉动画。
	// 色系按钮改为纯图标 30×30，与关闭按钮同尺寸；间隙统一为 5px。
	pickerToneHit->w.SetDirect(chromeHeight);
	pickerToneHit->h.SetDirect(chromeHeight);
	pickerToneHit->rw->SetDirect(pickerControlRadius);
	pickerToneHit->rh->SetDirect(pickerControlRadius);
	pickerToneHit->UpInh(BarUiInheritClass(
		pickerLeft + BarColorPickerPaletteInset * pickerScale, chromeTop));
	if (!state.barState.drawAttributeBar.colorPickerOpen)
		pickerToneHit->pct.SetTar(0.0);
	else if (state.barState.drawAttributeBar.colorPickerTonePress)
		pickerToneHit->pct.SetTar(0.10);
	else if (state.drawAttributeColorPickerToneHoverStage
		== BarButtonHoverStageEnum::None)
		pickerToneHit->pct.SetTar(0.0); // 无常驻背景，悬停时才由动画显现
	state.drawAttributeColorPickerTonePressScale.SetTar(
		state.barState.drawAttributeBar.colorPickerTonePress
			? BarButtonPressScale : 1.0,
		BarUiDefaultOperationDur, nullopt, false,
		state.barState.drawAttributeBar.colorPickerTonePress
			? state.buttonPressCurve : state.buttonReleaseCurve);
	pickerCloseHit->w.SetDirect(chromeHeight);
	pickerCloseHit->h.SetDirect(chromeHeight);
	pickerCloseHit->rw->SetDirect(pickerControlRadius);
	pickerCloseHit->rh->SetDirect(pickerControlRadius);
	pickerCloseHit->UpInh(BarUiInheritClass(
		pickerLeft + pickerWidth - BarColorPickerPaletteInset * pickerScale - chromeHeight,
		chromeTop));
	if (!state.barState.drawAttributeBar.colorPickerOpen)
		pickerCloseHit->pct.SetTar(0.0);
	else if (state.barState.drawAttributeBar.colorPickerClosePress)
		pickerCloseHit->pct.SetTar(0.10);
	else if (state.drawAttributeColorPickerCloseHoverStage
		== BarButtonHoverStageEnum::None)
		pickerCloseHit->pct.SetTar(0.0);
	state.drawAttributeColorPickerClosePressScale.SetTar(
		state.barState.drawAttributeBar.colorPickerClosePress
			? BarButtonPressScale : 1.0,
		BarUiDefaultOperationDur, nullopt, false,
		state.barState.drawAttributeBar.colorPickerClosePress
			? state.buttonPressCurve : state.buttonReleaseCurve);

	// 色系按钮改纯图标：亮=太阳、暗=月亮，仅显示当前色系对应图标。
	bool pickerDarkTone = state.barState.drawAttributeBar.colorPickerDarkTone;
	double toneIconSize = 20.0 * pickerScale;
	auto pickerToneSun = state.svgMap[
		BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneSun];
	auto pickerToneMoon = state.svgMap[
		BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneMoon];
	auto LayoutToneIcon = [&](const shared_ptr<BarUiSVGClass>& icon,
		bool active)
		{
			icon->w.SetDirect(toneIconSize);
			icon->h.SetDirect(toneIconSize);
			icon->pct.SetDirect(active ? pickerOpacity : 0.0);
			icon->Inherit(BarUiInheritEnum::Center, *pickerToneHit);
		};
	LayoutToneIcon(pickerToneSun, !pickerDarkTone);
	LayoutToneIcon(pickerToneMoon, pickerDarkTone);

	int displayR = static_cast<int>(lround(clamp(
		static_cast<double>(state.drawAttributeColorPickerDisplayR.val), 0.0, 255.0)));
	int displayG = static_cast<int>(lround(clamp(
		static_cast<double>(state.drawAttributeColorPickerDisplayG.val), 0.0, 255.0)));
	int displayB = static_cast<int>(lround(clamp(
		static_cast<double>(state.drawAttributeColorPickerDisplayB.val), 0.0, 255.0)));
	int displayOpacity = static_cast<int>(lround(clamp(
		static_cast<double>(state.drawAttributeColorPickerDisplayOpacity.val),
		0.0, 100.0)));
	double footerTop = paletteTop + paletteHeight;
	// 文字区域覆盖色板下沿到面板下沿，连同上下留白一起做竖直居中。
	double footerBottom = pickerTop + pickerHeight;
	double footerHeight = max(22.0 * pickerScale, footerBottom - footerTop);
	auto pickerRgbWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerRgb];
	auto pickerGWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerG];
	auto pickerBWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerB];
	auto pickerOpacityWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacity];
	auto pickerRgbValueWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerRgbValue];
	auto pickerGValueWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerGValue];
	auto pickerBValueWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerBValue];
	auto pickerOpacityValueWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacityValue];
	// 用页脚实际竖向留白推导横向边距，避免与色板 5 DIP 内缩绑定。
	double footerOuterPadding = clamp(
		(footerHeight - state.colorPickerFooterTextSize.height * pickerScale) / 2.0,
		0.0, footerHeight / 2.0);
	double footerColumnGap = 6.0 * pickerScale;
	double footerLabelValueGap = 3.0 * pickerScale;
	double footerRgbValueW = state.colorPickerFooterRgbValueSize.width * pickerScale;
	double footerOpacityValueW =
		state.colorPickerFooterOpacityValueSize.width * pickerScale;
	double footerRgbLabelW = max(
		state.colorPickerFooterRLabelSize.width,
		max(state.colorPickerFooterGLabelSize.width,
			state.colorPickerFooterBLabelSize.width)) * pickerScale;
	double footerOpacityLabelW =
		state.colorPickerFooterOpacityLabelSize.width * pickerScale;
	double footerRgbColW = footerRgbLabelW
		+ footerLabelValueGap + footerRgbValueW;
	double footerOpacityColW = footerOpacityLabelW
		+ footerLabelValueGap + footerOpacityValueW;
	double footerRX = pickerLeft + footerOuterPadding;
	double footerGX = footerRX + footerRgbColW + footerColumnGap;
	double footerBX = footerGX + footerRgbColW + footerColumnGap;
	double footerOpacityX = pickerLeft + pickerWidth
		- footerOuterPadding - footerOpacityColW;
	pickerRgbValueWord->content.SetVal(to_wstring(displayR));
	pickerRgbValueWord->content.SetTar(to_wstring(displayR));
	pickerGValueWord->content.SetVal(to_wstring(displayG));
	pickerGValueWord->content.SetTar(to_wstring(displayG));
	pickerBValueWord->content.SetVal(to_wstring(displayB));
	pickerBValueWord->content.SetTar(to_wstring(displayB));
	wstring opacityValue = to_wstring(displayOpacity) + L"%";
	pickerOpacityValueWord->content.SetVal(opacityValue);
	pickerOpacityValueWord->content.SetTar(opacityValue);
	auto LayoutPickerFooterWord = [&](BarUiWordClass& word,
		double wordX, double wordW)
		{
			word.w.SetDirect(wordW);
			word.h.SetDirect(footerHeight);
			word.size.SetDirect(13.0 * pickerScale);
			word.pct.SetDirect(pickerOpacity);
			word.UpInh(BarUiInheritClass(wordX, footerTop));
		};
	LayoutPickerFooterWord(*pickerRgbWord, footerRX, footerRgbLabelW);
	LayoutPickerFooterWord(*pickerGWord, footerGX, footerRgbLabelW);
	LayoutPickerFooterWord(*pickerBWord, footerBX, footerRgbLabelW);
	LayoutPickerFooterWord(*pickerOpacityWord, footerOpacityX,
		footerOpacityLabelW);
	LayoutPickerFooterWord(*pickerRgbValueWord,
		footerRX + footerRgbLabelW + footerLabelValueGap,
		footerRgbValueW);
	LayoutPickerFooterWord(*pickerGValueWord,
		footerGX + footerRgbLabelW + footerLabelValueGap,
		footerRgbValueW);
	LayoutPickerFooterWord(*pickerBValueWord,
		footerBX + footerRgbLabelW + footerLabelValueGap,
		footerRgbValueW);
	LayoutPickerFooterWord(*pickerOpacityValueWord,
		footerOpacityX + footerOpacityLabelW + footerLabelValueGap,
		footerOpacityValueW);

	// 保持提示只在本次显现开始时决定侧边；隐藏途中不再跟手换边。
	double holdOpacity = clamp(static_cast<double>(
		state.drawAttributeColorPickerHoldOpacity.val) * pickerOpacity, 0.0, 1.0);
	// 宽度只装文字与环形进度条：5 左距 + 文字 + 5 间隔 + 环 14 + 5 右距。
	double holdWidth = min(paletteWidth - 10.0 * pickerScale,
		static_cast<double>(state.colorPickerHoldTextSize.width)
			+ 29.0 * pickerScale);
	double holdHeight = 28.0 * pickerScale;
	bool holdTargetActive =
		state.barState.drawAttributeBar.colorPickerHoldHintActive
		|| state.barState.drawAttributeBar.colorPickerHoldLocked;
	if (holdTargetActive && !state.drawAttributeColorPickerHoldTargetActive)
	{
		bool pointerOnTopHalf = static_cast<double>(
			state.barState.drawAttributeBar.colorPickerPointerY)
			< paletteTop + paletteHeight / 2.0;
		state.drawAttributeColorPickerHoldOnTop = !pointerOnTopHalf;
	}
	state.drawAttributeColorPickerHoldTargetActive = holdTargetActive;
	double holdLeft = paletteLeft + (paletteWidth - holdWidth) / 2.0;
	double holdTop = state.drawAttributeColorPickerHoldOnTop
		? paletteTop + BarColorPickerPaletteInset * pickerScale
		: paletteTop + paletteHeight - holdHeight
			- BarColorPickerPaletteInset * pickerScale;
	SetAbsoluteHit(pickerHold, holdLeft, holdTop, holdWidth, holdHeight);
	pickerHold->pct.SetDirect(0.82 * holdOpacity);
	pickerHold->rw->SetDirect(pickerControlRadius);
	pickerHold->rh->SetDirect(pickerControlRadius);
	auto pickerHoldWord = state.wordMap[
		BarUISetWordEnum::DrawAttributeBar_ColorPickerHoldLabel];
	// 文字与右侧环之间留 5px，紧靠但不相撞。
	pickerHoldWord->w.SetDirect(holdWidth - 24.0 * pickerScale);
	pickerHoldWord->h.SetDirect(holdHeight);
	pickerHoldWord->size.SetDirect(12.0 * pickerScale);
	pickerHoldWord->pct.SetDirect(holdOpacity);
	COLORREF colorPickerHoldGrayColor = MixBarUiColor(
		GetThemeColor(BarThemeColorEnum::TextPrimary),
		GetThemeColor(BarThemeColorEnum::Surface), 0.45);
	pickerHoldWord->color.SetDirect(MixBarUiColor(
		colorPickerHoldGrayColor,
		GetThemeColor(BarThemeColorEnum::TextPrimary),
		clamp(static_cast<double>(
			state.drawAttributeColorPickerHoldTextMix.val), 0.0, 1.0)));
	pickerHoldWord->UpInh(BarUiInheritClass(
		holdLeft + BarColorPickerPaletteInset * pickerScale, holdTop));

	// 顶部色预览是面板固定控件：随面板展开常驻，无独立入退场，不显示文字。
	double previewSlotGap = BarColorPickerPaletteInset * pickerScale;
	double previewSlotLeft = pickerToneHit->inhX
		+ pickerToneHit->w.val + previewSlotGap;
	double previewSlotRight = pickerCloseHit->inhX - previewSlotGap;
	double previewSlotTop = pickerToneHit->inhY;
	double previewSlotHeight = pickerToneHit->h.val;
	double previewSlotWidth = max(0.0,
		previewSlotRight - previewSlotLeft);
SetAbsoluteHit(pickerPreview, previewSlotLeft, previewSlotTop,
			previewSlotWidth, previewSlotHeight);
		pickerPreview->pct.SetDirect(pickerOpacity);
		pickerPreview->rw->SetDirect(pickerControlRadius);
		pickerPreview->rh->SetDirect(pickerControlRadius);
		// 顶部预览色块只反映 RGB；透明度由底部读数单独展示。
		pickerPreview->fill->SetDirect(RGB(displayR, displayG, displayB));
		}

	// 时间轴与属性值在同一帧末尾推进，避免批次剩余时间和实际动画相差一帧。
	state.mainBarTimeline.Advance(animationDtSeconds, currentAnimationSpeedRate);
	state.drawAttributeTimeline.Advance(animationDtSeconds, currentAnimationSpeedRate);
	state.geometryAttributeTimeline.Advance(
		animationDtSeconds, currentAnimationSpeedRate);


	return needRendering;
}

void BarRenderLoopCoordinator::PrepareLightingAndDemand(
	BarRenderLoopState& state, const BarRenderFrameSnapshot& frame,
	bool needRendering)
{
	const auto& frameDrawingState = frame;
	const double animationDtSeconds = frame.animationDtSeconds;
	bool needRenderOnce = BarAtomic::renderOnceFlag.exchange(false);
	bool needBorderLightingRendering = false;
	{
		needBorderLightingRendering = state.spec.PrepareFrameLighting(
			animationDtSeconds,
			static_cast<int>(frameDrawingState.stateMode),
			static_cast<int>(frameDrawingState.penMode),
			frameDrawingState.brush1Color,
			frameDrawingState.highlighterColor,
			frameDrawingState.penetrate);
	}
	bool sustainRendering = true == BarAtomic::sustainFlag;
	bool debugRendering = true == BarUiDebugModeEnabled;
	state.presentDecision.AddDemand({
		needRendering || sustainRendering || debugRendering,
		needBorderLightingRendering,
		needRenderOnce,
		});
}

BarRenderLoopStageResult BarRenderLoopCoordinator::CalculateDirtyAndDrawPresent(
	BarRenderLoopState& state, const BarRenderFrameSnapshot& frame,
	UPDATELAYEREDWINDOWINFO& ulwi)
{
	const unsigned long long frameDemandGeneration = frame.demandGeneration;
	const double frameZoom = frame.zoom;
	const auto& frameDrawingState = frame;
	if (state.presentDecision.ShouldPresent())
	{

		bool interactiveFrame = state.presentDecision.NeedsInteractivePass();
		auto renderPass = AcquireUi3RenderPass(interactiveFrame
			? Ui3RenderPriority::Interactive : Ui3RenderPriority::Cosmetic);
		if (!renderPass)
		{
			// 租约结果也交给同一状态机，装饰跳帧不得吞掉最终 lighting 状态。
			(void)state.presentDecision.CompleteAttempt(
				Inkeys::UI::Bar::BarPresentAttemptResult::CosmeticLeaseSkipped());
			Inkeys::UI::Bar::HighPrecisionWait(chrono::duration<double, milli>(
				chrono::high_resolution_clock::now() - state.reckon).count(), 60.0);
			state.reckon = chrono::high_resolution_clock::now();
			return BarRenderLoopStageResult::Continue;
		}

		Ui3RenderDeviceEpoch epoch = GetUi3RenderDeviceEpoch();
		state.presentDecision.ObserveDeviceGeneration(epoch.generation);
		const bool deviceGenerationChanged =
			epoch.generation != state.spec.GetDeviceGeneration();
		HRESULT ensureDeviceResourcesHr = state.spec.EnsureDeviceResources(epoch,
			static_cast<UINT32>(state.barWindow.w), static_cast<UINT32>(state.barWindow.h));
		if (FAILED(ensureDeviceResourcesHr))
		{
			state.presentDecision.RequireFullDirtyRetry();
			state.presentDecision.RecordFailure(
				Inkeys::UI::Bar::BarPresentFailureClass::DeviceResources,
				epoch.generation, frameDemandGeneration,
				state.presentAttemptFrameSerial);
			if (state.barDeviceResourceFailureGeneration != epoch.generation)
			{
				state.barDeviceResourceFailureGeneration = epoch.generation;
				if (IDTLogger) IDTLogger->error(
					"[BarUISetClass::Rendering] 切换 UI3 epoch 后重建 Bar 资源失败, hr=0x{:08X}",
					static_cast<unsigned int>(ensureDeviceResourcesHr));
			}
			return BarRenderLoopStageResult::Continue;
		}
		if (deviceGenerationChanged)
		{
			state.barDeviceResourceFailureGeneration = 0;
			state.presentDecision.ResetFailureRecovery();
			state.presentDecision.RequireFullDirtyRetry();
		}
		ID2D1DeviceContext* barDeviceContext = state.spec.GetDeviceContext();
		ID2D1GdiInteropRenderTarget* barGdiInterop =
			state.spec.GetGdiInteropRenderTarget();

		// BeginDraw 前计算三个根控件的保守边界，用同一 dirty rect 约束清除、D2D 和 ULW。
		auto mainButton = state.superellipseMap[BarUISetSuperellipseEnum::MainButton];
		mainButton->UpInh(BarUiInheritClass(
			mainButton->x.val - mainButton->w.val / 2.0,
			mainButton->y.val - mainButton->h.val / 2.0));
		auto mainBar = state.shapeMap[BarUISetShapeEnum::MainBar];
		mainBar->Inherit(BarUiInheritEnum::Center, *mainButton);
		auto drawButton =
			state.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Draw)];
		drawButton->button.Inherit(
			BarUiInheritEnum::CenterFromTopLeft, *mainBar);
		auto drawAttribute = state.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
		drawAttribute->Inherit(BarUiInheritEnum::Center, drawButton->button);
		auto geometryButton =
			state.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)];
		geometryButton->button.Inherit(
			BarUiInheritEnum::CenterFromTopLeft, *mainBar);
		auto geometryAttribute =
			state.shapeMap[BarUISetShapeEnum::GeometryAttributeBar];
		geometryAttribute->Inherit(
			BarUiInheritEnum::Center, geometryButton->button);

		RECT predicted = RECT(0, 0, 0, 0);
		auto UnionShapeBounds = [&](RECT& bounds, const BarUiShapeClass* shape)
			{
				if (!shape) return;
				// 命中专用 Shape 没有填充或边框，实际不会进入 D2D，不能污染 dirty。
				if ((!shape->fill.has_value() && !shape->frame.has_value())
					|| shape->w.val <= 0.0 || shape->h.val <= 0.0)
					return;
				double lightPct = shape->frameLightPct.has_value()
					? static_cast<double>(shape->frameLightPct.value().val) : 0.0;
				if (shape->enable.val
					&& (shape->pct.val > 0.0 || lightPct > 0.0))
					BarRenderingAttribute::UnionRectInPlace(bounds,
						BarRenderingAttribute::GetWeigetRect(
							*shape, static_cast<double>(frameZoom)));
			};
		auto UnionSvgBounds = [&](RECT& bounds, const BarUiSVGClass* svg)
			{
				if (!svg) return;
				if (svg->enable.val && svg->pct.val > 0.0)
					BarRenderingAttribute::UnionRectInPlace(bounds,
						BarRenderingAttribute::GetWeigetRect(
							*svg, static_cast<double>(frameZoom)));
			};
		auto UnionPngBounds = [&](RECT& bounds, const BarUiPNGClass* png)
			{
				if (!png) return;
				if (png->enable.val && png->pct.val > 0.0)
					BarRenderingAttribute::UnionRectInPlace(bounds,
						BarRenderingAttribute::GetWeigetRect(
							*png, static_cast<double>(frameZoom)));
			};
		auto UnionWordBounds = [&](RECT& bounds, const BarUiWordClass* word)
			{
				if (!word) return;
				if (word->enable.val && word->pct.val > 0.0)
					BarRenderingAttribute::UnionRectInPlace(bounds,
						BarRenderingAttribute::GetWeigetRect(
							*word, static_cast<double>(frameZoom)));
			};
		// 预测边界与成功提交后保存的实际边界必须共用同一可见性判定。
		auto IncludeShapeBounds = [&](const shared_ptr<BarUiShapeClass>& shape)
			{ UnionShapeBounds(predicted, shape.get()); };
		auto IncludeSvgBounds = [&](const shared_ptr<BarUiSVGClass>& svg)
			{ UnionSvgBounds(predicted, svg.get()); };
		auto IncludePngBounds = [&](const shared_ptr<BarUiPNGClass>& png)
			{ UnionPngBounds(predicted, png.get()); };
		auto IncludeWordBounds = [&](const shared_ptr<BarUiWordClass>& word)
			{ UnionWordBounds(predicted, word.get()); };
		IncludeShapeBounds(mainBar);
		IncludeShapeBounds(drawAttribute);
		IncludeShapeBounds(geometryAttribute);
		IncludeShapeBounds(state.shapeMap[BarUISetShapeEnum::MorePanel]);
		IncludeShapeBounds(state.shapeMap[BarUISetShapeEnum::MorePanelDivider]);
		IncludeShapeBounds(state.shapeMap[BarUISetShapeEnum::MorePanelCloseHit]);
		IncludeSvgBounds(state.svgMap[BarUISetSvgEnum::MorePanelClose]);
		BarMoreButtonSnapshotClass predictedMoreSnapshot =
			state.barButtonSet.GetMoreButtonSnapshot();
		auto IncludeMoreButtonBounds = [&](const shared_ptr<BarButtonClass>& button)
			{
				if (!button) return;
				IncludeShapeBounds(shared_ptr<BarUiShapeClass>(
					button, &button->button));
				if (button->iconKind == BarButtonIconKindEnum::Png)
					IncludePngBounds(shared_ptr<BarUiPNGClass>(
						button, &button->pngIcon));
				else IncludeSvgBounds(shared_ptr<BarUiSVGClass>(
					button, &button->icon));
				IncludeWordBounds(shared_ptr<BarUiWordClass>(
					button, &button->name));
			};
		for (const shared_ptr<BarButtonClass>& button :
			predictedMoreSnapshot.explicitMore)
			IncludeMoreButtonBounds(button);
		for (const shared_ptr<BarButtonClass>& button :
			predictedMoreSnapshot.forcedOverflow)
			IncludeMoreButtonBounds(button);
		auto thicknessSliderHit = state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderHit];
		if (thicknessSliderHit
			&& state.drawAttributeThicknessSliderProgress.val > 0.0)
			BarRenderingAttribute::UnionRectInPlace(predicted,
				BarRenderingAttribute::GetWeigetRect(
					*thicknessSliderHit,
					static_cast<double>(frameZoom)));
IncludeShapeBounds(state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderThumb]);
			IncludeShapeBounds(state.shapeMap[
				BarUISetShapeEnum::
					DrawAttributeBar_ThicknessPreviewPopupSurface]);
			IncludeShapeBounds(state.shapeMap[
				BarUISetShapeEnum::
					DrawAttributeBar_ThicknessPreviewPopupCircle]);
			// 数值迁移始终被自适应 Surface 包住，Surface 边界同时覆盖其 predicted 脏区。
			// 静止保持提示文字与环形进度（环由文字位置推导）。
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessHoldLockLabel]);
			// 浮窗可越过绘制属性边框，BeginDraw 前必须显式纳入新帧脏区。
			IncludeShapeBounds(state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup]);
		// 菜单可越过绘制属性面板，必须在 BeginDraw 前纳入预测脏区。
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine]);
		IncludeWordBounds(state.wordMap[
			BarUISetWordEnum::DrawAttributeBar_PenTypeMenuFreeLine]);
		IncludeWordBounds(state.wordMap[
			BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel]);
		IncludeSvgBounds(state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_PenTypeMenuCheck]);
		IncludeSvgBounds(state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody]);
		IncludeSvgBounds(state.svgMap[
			BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose]);
			IncludeSvgBounds(state.svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerPreviewBubble]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorPickerHoldHint]);
		IncludeShapeBounds(state.shapeMap[
			BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner]);
		IncludePngBounds(state.pngMap[
			BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel]);
			IncludeSvgBounds(state.svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check]);
			IncludeSvgBounds(state.svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneSun]);
			IncludeSvgBounds(state.svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneMoon]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerRgb]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerG]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerB]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacity]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerRgbValue]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerGValue]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerBValue]);
			IncludeWordBounds(state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacityValue]);
		IncludeWordBounds(state.wordMap[
			BarUISetWordEnum::DrawAttributeBar_ColorPickerHoldLabel]);
		if (mainButton->enable.val && mainButton->pct.val > 0.0)
			BarRenderingAttribute::UnionRectInPlace(predicted,
				BarRenderingAttribute::GetWeigetRect(
					*mainButton, static_cast<double>(frameZoom)));
		RECT frameDirty = state.presentDecision.LastPresentedBounds();
		BarRenderingAttribute::UnionRectInPlace(frameDirty, predicted);
		if (state.presentDecision.NeedsFullDirty() || BarUiDebugModeEnabled)
			frameDirty = RECT(0, 0, state.barWindow.w, state.barWindow.h);
		frameDirty.left = clamp<LONG>(
			frameDirty.left, 0, static_cast<LONG>(state.barWindow.w));
		frameDirty.top = clamp<LONG>(
			frameDirty.top, 0, static_cast<LONG>(state.barWindow.h));
		frameDirty.right = clamp<LONG>(
			frameDirty.right, 0, static_cast<LONG>(state.barWindow.w));
		frameDirty.bottom = clamp<LONG>(
			frameDirty.bottom, 0, static_cast<LONG>(state.barWindow.h));
		if (frameDirty.right <= frameDirty.left
			|| frameDirty.bottom <= frameDirty.top)
			frameDirty = RECT(0, 0, state.barWindow.w, state.barWindow.h);
		D2D1_RECT_F frameDirtyRect = D2D1::RectF(
			static_cast<FLOAT>(frameDirty.left),
			static_cast<FLOAT>(frameDirty.top),
			static_cast<FLOAT>(frameDirty.right),
			static_cast<FLOAT>(frameDirty.bottom));
		state.current = RECT(0, 0, 0, 0);
		barDeviceContext->BeginDraw();
		state.spec.PushFrameDirtyClip(barDeviceContext, frameDirtyRect);

		// 清除背景
		{
			D2D1_COLOR_F clearColor = Inkeys::Color::ConvertToD2dColor(RGBA(0, 0, 0, 0));
			// 全局 dirty clip 已经同时覆盖旧、新边界，Clear 不再触碰其余全屏位图。
			barDeviceContext->Clear(&clearColor);

			// TODO 绘制纯白全透明警告用户开启 aero
			auto obj = BarUISetWordEnum::BackgroundWarning;
			state.spec.Word(barDeviceContext, *state.wordMap[obj], state.wordMap[obj]->Inherit(), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
		}

		using enum BarUiInheritEnum;
		{
			// 主栏
			{
				// 提前计算依赖
				{
					auto mainButton = state.superellipseMap[BarUISetSuperellipseEnum::MainButton];
					// 使用动画中的实际宽高计算左上角，保证超椭圆与内部 SVG 始终围绕中心缩放。
					mainButton->UpInh(BarUiInheritClass(
						mainButton->x.val - mainButton->w.val / 2.0,
						mainButton->y.val - mainButton->h.val / 2.0));
					state.shapeMap[BarUISetShapeEnum::MainBar]->Inherit(Center, *mainButton);
					state.barButtonSet.preset[(int)BarButtonPresetEnum::Draw]->button.Inherit(CenterFromTopLeft, *state.shapeMap[BarUISetShapeEnum::MainBar]);
				}

				// 绘制属性
				{
					auto obj = BarUISetShapeEnum::DrawAttributeBar;
					auto drawAttributePanel = state.shapeMap[obj];
					double panelGeometryScale = drawAttributePanel->w.val
						/ BarDrawAttributeExpandedWidth;
					if (!isfinite(panelGeometryScale)
						|| panelGeometryScale <= 0.000001)
						panelGeometryScale = 1.0;
					// 等比收拢时复用完整尺寸的柔光遮罩，第三光源亮度全程连续。
					state.spec.SetFrameDiffuseMaskGeometryScale(
						1.0 / panelGeometryScale);
					state.spec.Shape(barDeviceContext, *state.shapeMap[obj], state.shapeMap[obj]->Inherit(Center, state.barButtonSet.preset[(int)BarButtonPresetEnum::Draw]->button), &state.current, true);
					// 只发布三个外层可见区域，Raw Input 高频路径无需遍历全部子控件。
					RefreshBorderCursorVisibleRegions(frameZoom);

					// Color 区域
					{
						// Color 1
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect1;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect1;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 2
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect2;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect2;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 3
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect3;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect3;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 4
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect4;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect4;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 5
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect5;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect5;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 6
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect6;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect6;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 7
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect7;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect7;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 8
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect8;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect8;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 9
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect9;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect9;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 10
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect10;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect10;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 11
						{
							auto obj1 = BarUISetShapeEnum::DrawAttributeBar_ColorSelect11;
							state.spec.Shape(barDeviceContext, *state.shapeMap[obj1], state.shapeMap[obj1]->Inherit(TopLeft, *state.shapeMap[BarUISetShapeEnum::DrawAttributeBar]));

							auto obj2 = BarUISetSvgEnum::DrawAttributeBar_ColorSelect11;
							state.spec.Svg(barDeviceContext, *state.svgMap[obj2], state.svgMap[obj2]->Inherit(Center, *state.shapeMap[obj1]));
						}
						// Color 12：圆盘、色芯和绿勾都继承入口的当前几何，换边时不直接跳到目标方向。
						{
							auto customSwatch = state.shapeMap[
								BarUISetShapeEnum::DrawAttributeBar_ColorSelect12];
							BarUiInheritClass customInherit = customSwatch->Inherit(
								TopLeft, *state.shapeMap[
									BarUISetShapeEnum::DrawAttributeBar]);
							double customPressScale = static_cast<double>(
								state.drawAttributeColorPickerEntryPressScale.val);
							if (!isfinite(customPressScale) || customPressScale <= 0.0)
								customPressScale = 1.0;
							D2D1_MATRIX_3X2_F customOriginalTransform;
							barDeviceContext->GetTransform(&customOriginalTransform);
							bool customTransformChanged =
								abs(customPressScale - 1.0) > 0.000001;
							if (customTransformChanged)
							{
								// 只缩放整组视觉，Shape 的原始几何继续承担命中。
								FLOAT centerX = static_cast<FLOAT>((customInherit.x
									+ customSwatch->w.val / 2.0) * frameZoom);
								FLOAT centerY = static_cast<FLOAT>((customInherit.y
									+ customSwatch->h.val / 2.0) * frameZoom);
								D2D1_MATRIX_3X2_F scaleTransform = D2D1::Matrix3x2F::Scale(
									static_cast<FLOAT>(customPressScale),
									static_cast<FLOAT>(customPressScale),
									D2D1::Point2F(centerX, centerY));
								barDeviceContext->SetTransform(
									scaleTransform * customOriginalTransform);
							}
							auto customWheel = state.pngMap[
								BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel];
							state.spec.Png(barDeviceContext, *customWheel,
								customWheel->Inherit(TopLeft, *customSwatch));
							// 外框后绘制，用与其他色块一致的 1 DIP 边框压住色盘边缘。
							state.spec.Shape(barDeviceContext, *customSwatch,
								customInherit);
							auto customInner = state.shapeMap[
								BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner];
							state.spec.Shape(barDeviceContext, *customInner,
								customInner->Inherit(TopLeft, *customSwatch));
							auto customCheck = state.svgMap[
								BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check];
							state.spec.Svg(barDeviceContext, *customCheck,
								customCheck->Inherit(TopLeft, *customSwatch));
							if (customTransformChanged)
								barDeviceContext->SetTransform(customOriginalTransform);
						}
					}
					// 画笔样式区域
					{
						struct PenTypeButtonRender
						{
							BarUISetShapeEnum shape;
							BarUISetSvgEnum svg;
							BarUISetWordEnum word;
							BarUiValueClass* pressScale;
						};
					const PenTypeButtonRender penTypeButtons[] =
						{
							{ BarUISetShapeEnum::DrawAttributeBar_Brush2,
								BarUISetSvgEnum::DrawAttributeBar_Brush2,
								BarUISetWordEnum::DrawAttributeBar_Brush2, nullptr },
							{ BarUISetShapeEnum::DrawAttributeBar_Laser,
								BarUISetSvgEnum::DrawAttributeBar_Laser,
								BarUISetWordEnum::DrawAttributeBar_Laser, nullptr },
							{ BarUISetShapeEnum::DrawAttributeBar_Highlight1,
								BarUISetSvgEnum::DrawAttributeBar_Highlight1,
								BarUISetWordEnum::DrawAttributeBar_Highlight1,
								&state.drawAttributeHighlightPressScale },
							{ BarUISetShapeEnum::DrawAttributeBar_Brush1,
								BarUISetSvgEnum::DrawAttributeBar_Brush1,
								BarUISetWordEnum::DrawAttributeBar_Brush1,
								&state.drawAttributeBrushPressScale },
							{ BarUISetShapeEnum::DrawAttributeBar_SoftPen,
								BarUISetSvgEnum::DrawAttributeBar_SoftPen,
								BarUISetWordEnum::DrawAttributeBar_SoftPen, nullptr },
					};
					auto panel = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar];
					for (const auto& button : penTypeButtons)
					{
						auto shape = state.shapeMap[button.shape];
						BarUiInheritClass shapeInherit =
							shape->Inherit(TopLeft, *panel);
							double pressScale = button.pressScale
								? static_cast<double>(button.pressScale->val) : 1.0;
							if (!isfinite(pressScale) || pressScale <= 0.0) pressScale = 1.0;

							D2D1_MATRIX_3X2_F originalTransform;
							barDeviceContext->GetTransform(&originalTransform);
							bool transformChanged = abs(pressScale - 1.0) > 0.000001;
							if (transformChanged)
							{
								// 与主栏一致，整个图文按钮围绕背景中心缩放。
								FLOAT centerX = static_cast<FLOAT>(
									(shapeInherit.x + shape->w.val / 2.0) * frameZoom);
								FLOAT centerY = static_cast<FLOAT>(
									(shapeInherit.y + shape->h.val / 2.0) * frameZoom);
								D2D1_MATRIX_3X2_F scaleTransform = D2D1::Matrix3x2F::Scale(
									static_cast<FLOAT>(pressScale), static_cast<FLOAT>(pressScale),
									D2D1::Point2F(centerX, centerY));
								barDeviceContext->SetTransform(scaleTransform * originalTransform);
							}

							state.spec.Shape(barDeviceContext, *shape, shapeInherit);
							state.spec.Svg(barDeviceContext, *state.svgMap[button.svg],
								state.svgMap[button.svg]->Inherit(Left, *shape));
						state.spec.Word(barDeviceContext, *state.wordMap[button.word],
							state.wordMap[button.word]->Inherit(TopLeft, *shape),
							DWRITE_FONT_WEIGHT_NORMAL,
							DWRITE_TEXT_ALIGNMENT_LEADING);
						if (transformChanged) barDeviceContext->SetTransform(originalTransform);
					}

					// 选中且具备能力的笔型在右侧显示独立扩展入口。
					auto extensionHit = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit];
					auto extensionDivider = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionDivider];
					auto extensionArrow = state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_PenTypeExtensionArrow];
					double extensionScale = max(0.0, static_cast<double>(
						state.drawAttributePenTypeExtensionPressScale.val));
					if (!isfinite(extensionScale) || extensionScale <= 0.0)
						extensionScale = 1.0;
					D2D1_MATRIX_3X2_F extensionTransform;
					barDeviceContext->GetTransform(&extensionTransform);
					if (abs(extensionScale - 1.0) > 0.000001)
					{
						BarUiInheritClass extensionInherit =
							extensionHit->Inherit(TopLeft, *panel);
						FLOAT centerX = static_cast<FLOAT>((extensionInherit.x
							+ extensionHit->w.val / 2.0) * frameZoom);
						FLOAT centerY = static_cast<FLOAT>((extensionInherit.y
							+ extensionHit->h.val / 2.0) * frameZoom);
						barDeviceContext->SetTransform(
							D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(extensionScale),
								static_cast<FLOAT>(extensionScale),
								D2D1::Point2F(centerX, centerY))
							* extensionTransform);
					}
					state.spec.Shape(barDeviceContext, *extensionHit,
						extensionHit->Inherit(TopLeft, *panel));
					state.spec.Svg(barDeviceContext, *extensionArrow,
						extensionArrow->Inherit(TopLeft, *panel));
					barDeviceContext->SetTransform(extensionTransform);
					// 分割线不继承入口按压缩放，避免按下时产生位移或闪烁。
					state.spec.Shape(barDeviceContext, *extensionDivider,
						extensionDivider->Inherit(TopLeft, *panel));
				}
					// 粗细调节区域
					{
						auto panel = state.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
						auto thicknessRegion =
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
						auto thicknessDisplay =
							state.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay];
						auto thicknessAdjust =
							state.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust];
						BarUiInheritClass thicknessRegionInherit =
							thicknessRegion->Inherit(TopLeft, *panel);
						state.spec.Shape(barDeviceContext, *thicknessRegion,
							thicknessRegionInherit);
						auto thicknessDivider = state.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ThicknessDivider];
						state.spec.Shape(barDeviceContext, *thicknessDivider,
							thicknessDivider->Inherit(TopLeft, *panel));
						BarUiInheritClass thicknessDisplayInherit =
							thicknessDisplay->Inherit(TopLeft, *panel);
						BarUiInheritClass thicknessAdjustInherit =
							thicknessAdjust->Inherit(TopLeft, *panel);
						auto previewGeometry =
							CalculateBarThicknessPreviewGeometry(
								*panel, *thicknessRegion,
								thicknessRegionInherit,
								*thicknessAdjust,
								thicknessAdjustInherit);
						double panelAnimationScale =
							previewGeometry.panelScale;

						double contentOpacity = thicknessDisplay->pct.val;
						FLOAT uiZoom = static_cast<FLOAT>(frameZoom);
						COLORREF contentColor = thicknessDisplay->color.val;
						if (contentOpacity > 0.000001 && uiZoom > 0.0f
							&& previewGeometry.valid)
						{
							// 展开静止后保持真实设备 px；面板动画时只补上同一几何缩放倍率。
							FLOAT requestedThickness = max(0.0f,
								static_cast<FLOAT>(state.drawAttributePenThickness.val
									* panelAnimationScale));
							double previewAreaHeight =
								previewGeometry.previewBottom
									- previewGeometry.previewTop;
							FLOAT maxPreviewThickness = max(1.0f,
								static_cast<FLOAT>(
									previewAreaHeight * uiZoom));
							FLOAT normalPreviewThickness =
								min(requestedThickness, maxPreviewThickness);
							double sliderProgress = clamp(
								static_cast<double>(
									state.drawAttributeThicknessSliderProgress.val),
								0.0, 1.0);
							double sliderTrackOpacity = clamp(
								static_cast<double>(
									state.drawAttributeThicknessSliderTrackOpacity.val),
								0.0, 1.0);
							double baseThicknessOpacity = contentOpacity
								* ((1.0 - sliderProgress)
									+ sliderProgress * sliderTrackOpacity);
							FLOAT trackThickness = min(
								maxPreviewThickness,
								static_cast<FLOAT>(
									BarThicknessSliderTrackHeight
									* panelAnimationScale * uiZoom));
							FLOAT previewThickness = static_cast<FLOAT>(
								normalPreviewThickness
								+ (trackThickness
									- normalPreviewThickness)
									* sliderProgress);
							// Preview 使用完整左右边界，进入 Slider 时再平滑收进内容内边距。
							FLOAT left = static_cast<FLOAT>((
								previewGeometry.previewLeft
								+ (previewGeometry.trackLeft
									- previewGeometry.previewLeft) * sliderProgress)
								* uiZoom);
							FLOAT right = static_cast<FLOAT>((
								previewGeometry.previewRight
								+ (previewGeometry.trackRight
									- previewGeometry.previewRight) * sliderProgress)
								* uiZoom);
							double previewSide =
								previewGeometry.previewSide;
							double previewMorph = clamp(
								static_cast<double>(
									state.drawAttributePenPreviewMorph.val),
								0.0, 1.0);
							double hardCurveProgress =
								clamp(1.0 - previewMorph * 2.0, 0.0, 1.0);
							double highlighterProgress =
								clamp((previewMorph - 0.5) * 2.0, 0.0, 1.0);
							double panelExpandedProgress = clamp(
								(panelAnimationScale
									- BarDrawAttributeCompactScale)
								/ (1.0 - BarDrawAttributeCompactScale),
								0.0, 1.0);
							FLOAT radius = previewThickness / 2.0F;
							FLOAT availableAmplitude = max(0.0F,
								(maxPreviewThickness - previewThickness)
									/ 2.0F);
							FLOAT amplitude = min(
								maxPreviewThickness * 0.34F,
								availableAmplitude)
								* static_cast<FLOAT>(
									hardCurveProgress
									* panelExpandedProgress
									* (1.0 - sliderProgress));
							FLOAT curveDirection =
								static_cast<FLOAT>(-previewSide);
								double sliderCenterY =
								ResolveThicknessSliderCenterY(state, previewGeometry);
								FLOAT centerY = static_cast<FLOAT>(
									(previewGeometry.previewCenterY
										+ (sliderCenterY
											- previewGeometry.previewCenterY)
											* sliderProgress) * uiZoom);
							D2D1_RECT_F previewRect = D2D1::RectF(left,
								centerY - previewThickness / 2.0f, right,
								centerY + previewThickness / 2.0f);
							COLORREF trackColor = MixBarUiColor(
								GetThemeColor(
									BarThemeColorEnum::Surface),
								GetThemeColor(
									BarThemeColorEnum::Accent),
								state.drawAttributeThicknessSliderAccentOpacity.val);
							COLORREF previewColor = MixBarUiColor(
								contentColor, trackColor,
								sliderProgress);
							ID2D1SolidColorBrush* solidBrush =
								state.spec.GetFrameSolidColorBrush(
									barDeviceContext, previewColor,
									baseThicknessOpacity);
							D2D1_RECT_F previewClip = D2D1::RectF(
								static_cast<FLOAT>(
									previewGeometry.previewLeft * uiZoom),
								static_cast<FLOAT>(
									previewGeometry.previewTop * uiZoom),
								static_cast<FLOAT>(
									previewGeometry.previewRight * uiZoom),
								static_cast<FLOAT>(
									previewGeometry.previewBottom * uiZoom));
							bool previewClipPushed =
								previewClip.right > previewClip.left
								&& previewClip.bottom > previewClip.top;
							if (previewClipPushed)
								barDeviceContext->PushAxisAlignedClip(
									previewClip,
									D2D1_ANTIALIAS_MODE_ALIASED);

							if (previewMorph <= 0.5 && solidBrush
								&& previewThickness > 0.0F)
							{
								FLOAT startX = min(previewRect.right,
									previewRect.left + radius);
								FLOAT endX = max(startX,
									previewRect.right - radius);
								FLOAT span = max(0.0F, endX - startX);
								FLOAT signedAmplitude = curveDirection * amplitude;
								D2D1_MATRIX_3X2_F originalTransform{};
								barDeviceContext->GetTransform(&originalTransform);
								auto DrawFallback = [&]()
								{
									D2D1_ROUNDED_RECT fallback{
										previewRect, radius, radius };
									barDeviceContext->FillRoundedRectangle(
										&fallback, solidBrush);
								};
								if (span <= 0.001F)
								{
									// 可用长度退化时保留原圆头胶囊，避免奇异矩阵。
									DrawFallback();
								}
								else
								{
									auto strokeStyle =
										state.spec.GetThicknessPreviewStrokeStyle();
									if (abs(signedAmplitude) <= 0.001F && strokeStyle)
										barDeviceContext->DrawLine(
											D2D1::Point2F(startX, centerY),
											D2D1::Point2F(endX, centerY),
											solidBrush, previewThickness, strokeStyle);
									else
									{
										auto path = state.spec.GetThicknessPreviewPath();
										if (path && strokeStyle)
										{
											D2D1_MATRIX_3X2_F unitTransform =
												D2D1::Matrix3x2F::Scale(
													span, signedAmplitude)
												* D2D1::Matrix3x2F::Translation(
													startX, centerY);
											barDeviceContext->SetTransform(
												unitTransform * originalTransform);
											barDeviceContext->DrawGeometry(
												path, solidBrush, previewThickness,
												strokeStyle);
										}
										else DrawFallback();
									}
								}
								// clip 出栈前无条件恢复调用方 transform。
								barDeviceContext->SetTransform(originalTransform);
							}
							else if (previewThickness > 0.0F)
							{
								FLOAT normalPreviewRadius =
									previewThickness / 2.0F
									* static_cast<FLOAT>(
										1.0 - highlighterProgress);
								FLOAT previewRadius = static_cast<FLOAT>(
									normalPreviewRadius
									+ (previewThickness / 2.0F
										- normalPreviewRadius)
										* sliderProgress);
								D2D1_ROUNDED_RECT roundedPreview{
									previewRect,
									previewRadius, previewRadius };
								FLOAT normalLeftOpacity = static_cast<FLOAT>(
									1.0 - 0.65 * highlighterProgress);
								FLOAT leftOpacity = static_cast<FLOAT>(
									normalLeftOpacity
									+ (1.0F - normalLeftOpacity)
										* sliderProgress);
								ID2D1LinearGradientBrush* gradientBrush =
									state.spec.GetThicknessPreviewGradientBrush(
										barDeviceContext, previewColor,
										D2D1::Point2F(
											previewRect.left, centerY),
										D2D1::Point2F(
											previewRect.right, centerY),
										leftOpacity,
										static_cast<FLOAT>(baseThicknessOpacity));
								ID2D1Brush* previewBrush =
									gradientBrush
									? static_cast<ID2D1Brush*>(gradientBrush)
									: static_cast<ID2D1Brush*>(solidBrush);
								if (previewBrush)
								{
									barDeviceContext->FillRoundedRectangle(
										&roundedPreview, previewBrush);
								}
							}
							if (previewClipPushed)
								barDeviceContext->PopAxisAlignedClip();

							// Dial 不可见时在这里直接跳过投影、tick 和文字缓存查询。
							double activationPreviewOpacity = clamp(
								BarThicknessFineDialActivationPreviewBaseOpacity
									* static_cast<double>(
										state.drawAttributeThicknessFineDialRecognitionVisibility.val)
								+ (1.0 - BarThicknessFineDialActivationPreviewBaseOpacity)
									* static_cast<double>(
										state.drawAttributeThicknessFineDialDwellProgress.val),
								0.0, 1.0);
							double fineDialOpacity = clamp(max(
								static_cast<double>(
									state.drawAttributeThicknessFineDialProgress.val),
								activationPreviewOpacity),
								0.0, 1.0)
								* contentOpacity * panelExpandedProgress;
							if (fineDialOpacity > 0.000001)
							{
							auto logicalRange = GetBarThicknessSliderRange(
								frameDrawingState.penMode, state.barStyle.dpiZoom);
							bool rangeTransitionActive =
								state.thicknessFineDialRangeTransitionPhase
									!= ThicknessFineDialRangeTransitionPhase::Idle;
							BarThicknessSliderRange renderRange = logicalRange;
							if (rangeTransitionActive)
							{
								renderRange.min = min(
									state.thicknessFineDialOldRenderRange.min,
									state.thicknessFineDialNewRenderRange.min);
								renderRange.max = max(
									state.thicknessFineDialOldRenderRange.max,
									state.thicknessFineDialNewRenderRange.max);
								renderRange.supported = true;
							}
							auto fineGeometry =
									CalculateBarThicknessFineDialGeometry(
										previewGeometry, sliderCenterY,
										panel->inhY, panel->inhY + panel->h.val);
							double rangeSpan = static_cast<double>(
								renderRange.max - renderRange.min);
								double trackTravelLogical = max(0.0,
									previewGeometry.trackRight
										- previewGeometry.trackLeft
										- BarThicknessSliderThumbDiameter
											* panelAnimationScale);
								double unitTravelLogical =
									ResolveThicknessFineDialUnitTravel(
										trackTravelLogical, state.barStyle.dpiZoom);
								double availableHalfWidth = max(0.0,
									fineGeometry.availableHalfWidth
										- 6.0 * panelAnimationScale);
								double radius = availableHalfWidth
									/ sin(BarThicknessFineDialThetaLimit);
								double angularStep = radius > 0.000001
									? unitTravelLogical / radius : 0.0;
							if (logicalRange.supported && renderRange.supported
								&& rangeSpan > 0.0
									&& fineGeometry.valid && radius > 0.000001
									&& angularStep > 0.000001)
							{
								bool candidateActive = state.barState.drawAttributeBar
									.thicknessFineDialCandidateActive;
								double liveVisualValue = candidateActive
									? static_cast<double>(state.barState.drawAttributeBar
										.thicknessFineDialVisualWidth)
									: static_cast<double>(state.drawAttributePenThickness.val);
								// 发布程序化动画的当帧视觉值，后续抓取可从当前角度接管。
								if (!candidateActive && isfinite(liveVisualValue))
									state.barState.drawAttributeBar.thicknessFineDialVisualWidth =
										static_cast<float>(liveVisualValue);
								if (!isfinite(liveVisualValue))
									liveVisualValue = clamp(
										static_cast<double>(GetPenWidth()),
										static_cast<double>(logicalRange.min),
										static_cast<double>(logicalRange.max));
								double visualValue = liveVisualValue;
								double previewAnchor = static_cast<double>(
									state.barState.drawAttributeBar
										.thicknessFineDialActivationPreviewVisualWidth);
								if (state.drawAttributeThicknessFineDialActivationGeometryTransition
									&& isfinite(previewAnchor) && previewAnchor > 0.0)
								{
									// 正式层从固定预览锚点接管 live value，激活边界不滚动或跳格。
									double formalProgress = clamp(static_cast<double>(
										state.drawAttributeThicknessFineDialProgress.val), 0.0, 1.0);
									visualValue = previewAnchor
										+ (liveVisualValue - previewAnchor) * formalProgress;
								}
									double visibleValueRadius =
										BarThicknessFineDialThetaLimit / angularStep;
								int firstTick = max(renderRange.min,
										static_cast<int>(ceil(
											visualValue - visibleValueRadius)));
								int lastTick = min(renderRange.max,
										static_cast<int>(floor(
											visualValue + visibleValueRadius)));
									if (lastTick - firstTick + 1
										> BarThicknessFineDialVisibleTickLimit)
									{
										int centerTick = static_cast<int>(
											lround(visualValue));
									firstTick = max(renderRange.min,
											centerTick
												- BarThicknessFineDialVisibleTickLimit / 2);
									lastTick = min(renderRange.max,
											firstTick
												+ BarThicknessFineDialVisibleTickLimit - 1);
									firstTick = max(renderRange.min,
											lastTick
												- BarThicknessFineDialVisibleTickLimit + 1);
									}

									double dialCenterX = fineGeometry.centerX;
									double dialCenterY = fineGeometry.centerY;
									if (state.drawAttributeThicknessFineDialActivationGeometryTransition)
									{
										double recognitionCenterY =
											(static_cast<double>(fineGeometry.clickZone.top)
												+ fineGeometry.clickZone.bottom) / 2.0;
										double geometryProgress = clamp(static_cast<double>(
											state.drawAttributeThicknessFineDialProgress.val),
											0.0, 1.0);
										dialCenterY = recognitionCenterY
											+ (fineGeometry.centerY - recognitionCenterY)
												* geometryProgress;
									}
									double outwardDirection =
										fineGeometry.outwardDirection;
								double selectionProgress = clamp(
									static_cast<double>(
										state.drawAttributeThicknessFineDialSelectionProgress.val),
									0.0, 1.0);
								if (state.drawAttributeThicknessFineDialActivationGeometryTransition
									&& state.barState.drawAttributeBar.thicknessViewMode
										!= ThicknessViewMode::FineDial)
									selectionProgress = 0.0;
									COLORREF tickColor = MixBarUiColor(
										GetThemeColor(BarThemeColorEnum::TextPrimary),
										GetThemeColor(BarThemeColorEnum::Surface), 0.52);
									COLORREF centerColor = RGB(255, 255, 255);

									// 两条固定分段 envelope 只暗示圆柱外缘，不引入 effect 或逐帧资源。
									if (auto envelopeBrush =
										state.spec.GetFrameSolidColorBrush(
											barDeviceContext, tickColor,
											fineDialOpacity * 0.12))
									{
										constexpr int envelopeSegments = 12;
										D2D1_POINT_2F previousTop{};
										D2D1_POINT_2F previousBottom{};
										for (int segment = 0;
											segment <= envelopeSegments; ++segment)
										{
											double theta = -BarThicknessFineDialThetaLimit
												+ BarThicknessFineDialThetaLimit * 2.0
													* static_cast<double>(segment)
													/ envelopeSegments;
											double depth = max(0.0, cos(theta));
											double x = dialCenterX
												+ radius * sin(theta);
											double y = dialCenterY - outwardDirection
												* (1.0 - depth)
												* BarThicknessFineDialDepthLiftDip
												* panelAnimationScale;
											double halfEnvelope =
												(BarThicknessFineDialMajorTickLengthDip / 2.0
													+ 2.0) * panelAnimationScale
												* (0.70 + 0.30 * depth);
											D2D1_POINT_2F nextTop = D2D1::Point2F(
												static_cast<FLOAT>(x * uiZoom),
												static_cast<FLOAT>((y - halfEnvelope)
													* uiZoom));
											D2D1_POINT_2F nextBottom = D2D1::Point2F(
												static_cast<FLOAT>(x * uiZoom),
												static_cast<FLOAT>((y + halfEnvelope)
													* uiZoom));
											if (segment > 0)
											{
												barDeviceContext->DrawLine(
													previousTop, nextTop,
													envelopeBrush, max(0.5F, 0.7F * uiZoom));
												barDeviceContext->DrawLine(
													previousBottom, nextBottom,
													envelopeBrush, max(0.5F, 0.7F * uiZoom));
											}
											previousTop = nextTop;
											previousBottom = nextBottom;
										}
									}

									struct FineDialLabelCandidate
									{
										IDWriteTextLayout* layout = nullptr;
										D2D1_POINT_2F origin{};
										double opacity = 0.0;
									};
									array<FineDialLabelCandidate,
										BarThicknessFineDialVisibleTickLimit>
										labelCandidates{};
									size_t labelCandidateCount = 0;
									for (int tick = firstTick; tick <= lastTick; ++tick)
									{
										double rangeMembershipOpacity = 1.0;
										bool oldRangeMember = false;
										bool newRangeMember = false;
										if (rangeTransitionActive)
										{
											oldRangeMember = tick
												>= state.thicknessFineDialOldRenderRange.min
												&& tick <= state.thicknessFineDialOldRenderRange.max;
											newRangeMember = tick
												>= state.thicknessFineDialNewRenderRange.min
												&& tick <= state.thicknessFineDialNewRenderRange.max;
											rangeMembershipOpacity = max(
												oldRangeMember
													? static_cast<double>(
														state.thicknessFineDialOldRangeOpacity.val)
													: 0.0,
												newRangeMember
													? static_cast<double>(
														state.thicknessFineDialNewRangeOpacity.val)
													: 0.0);
											if (rangeMembershipOpacity <= 0.000001)
												continue;
										}
										double theta = (static_cast<double>(tick)
											- visualValue) * angularStep;
										if (abs(theta) > BarThicknessFineDialThetaLimit)
											continue;
										double depth = max(0.0, cos(theta));
										double tickX = dialCenterX
											+ radius * sin(theta);
										double edgeRatio = availableHalfWidth > 0.0
											? abs(tickX - dialCenterX)
												/ availableHalfWidth : 1.0;
										double fadeT = clamp(
											(edgeRatio
												- BarThicknessFineDialEdgeFadeStart)
											/ (1.0
												- BarThicknessFineDialEdgeFadeStart),
											0.0, 1.0);
										double edgeFade = 1.0
											- fadeT * fadeT * (3.0 - 2.0 * fadeT);
										double tickOpacity = fineDialOpacity * edgeFade
											* (0.30 + 0.70 * depth);
										tickOpacity *= rangeMembershipOpacity;
										bool logicalEndpoint = tick == logicalRange.min
											|| tick == logicalRange.max;
										bool retiringEndpoint = rangeTransitionActive
											&& oldRangeMember
											&& (tick == state.thicknessFineDialOldRenderRange.min
												|| tick == state.thicknessFineDialOldRenderRange.max);
										bool endpoint = logicalEndpoint || retiringEndpoint;
										bool major = tick % 5 == 0 || endpoint;
										// 预激活统一为短刻度，正式层再引入 major 长度和线宽。
										double majorProgress = major ? selectionProgress : 0.0;
										double tickLength = (BarThicknessFineDialTickLengthDip
											+ (BarThicknessFineDialMajorTickLengthDip
												- BarThicknessFineDialTickLengthDip)
												* majorProgress)
											* panelAnimationScale
											* (0.72 + 0.28 * depth);
										double tickCenterY = dialCenterY - outwardDirection
											* (1.0 - depth)
											* BarThicknessFineDialDepthLiftDip
											* panelAnimationScale;
										if (auto tickBrush =
											state.spec.GetFrameSolidColorBrush(
												barDeviceContext, tickColor,
												tickOpacity))
											barDeviceContext->DrawLine(
												D2D1::Point2F(
													static_cast<FLOAT>(tickX * uiZoom),
													static_cast<FLOAT>((tickCenterY
														- tickLength / 2.0) * uiZoom)),
												D2D1::Point2F(
													static_cast<FLOAT>(tickX * uiZoom),
													static_cast<FLOAT>((tickCenterY
														+ tickLength / 2.0) * uiZoom)),
												tickBrush,
												max(0.7F, static_cast<FLOAT>(
													(1.0 + 0.4 * majorProgress)
													* panelAnimationScale * uiZoom)));

										if (major && tickOpacity > 0.000001
											&& selectionProgress > 0.000001)
										{
											auto* label = state.spec.GetThicknessFineDialLabelLayout(
												tick, uiZoom);
											if (label && label->layout
												&& labelCandidateCount
													< labelCandidates.size())
											{
												double labelCenterY = tickCenterY
													- outwardDirection
														* (tickLength / 2.0
															+ 3.0 * panelAnimationScale
															+ label->size.height
																/ (2.0 * uiZoom));
												auto& candidate =
													labelCandidates[labelCandidateCount++];
												candidate.layout = label->layout.Get();
												candidate.origin = D2D1::Point2F(
													static_cast<FLOAT>(tickX * uiZoom
														- label->layoutWidth / 2.0F),
													static_cast<FLOAT>(labelCenterY * uiZoom
														- label->size.height / 2.0F));
												candidate.opacity = tickOpacity * 0.92
													* selectionProgress;
											}
										}
									}

									// 所有可见 5 倍数和端点都绘制，避免投影移动时交错淘汰数字。
									for (size_t index = 0;
										index < labelCandidateCount; ++index)
									{
										const auto& candidate = labelCandidates[index];
										if (!candidate.layout
											|| candidate.opacity <= 0.000001)
											continue;
										if (auto labelBrush = state.spec.GetFrameSolidColorBrush(
											barDeviceContext, tickColor,
											candidate.opacity))
										{
											barDeviceContext->DrawTextLayout(
												candidate.origin, candidate.layout,
												labelBrush,
												D2D1_DRAW_TEXT_OPTIONS_CLIP);
										}
									}

									if (selectionProgress > 0.000001)
									{
										if (auto centerBrush =
											state.spec.GetFrameSolidColorBrush(
												barDeviceContext, centerColor,
												fineDialOpacity * selectionProgress))
										{
											double centerLength =
											(BarThicknessFineDialMajorTickLengthDip + 3.0)
											* panelAnimationScale;
										barDeviceContext->DrawLine(
											D2D1::Point2F(
												static_cast<FLOAT>(dialCenterX * uiZoom),
												static_cast<FLOAT>((dialCenterY
													- centerLength / 2.0) * uiZoom)),
											D2D1::Point2F(
												static_cast<FLOAT>(dialCenterX * uiZoom),
												static_cast<FLOAT>((dialCenterY
													+ centerLength / 2.0) * uiZoom)),
											centerBrush, max(1.0F,
												static_cast<FLOAT>(1.6
													* panelAnimationScale * uiZoom)));

											if (auto selector =
											state.spec.GetThicknessFineDialSelectorGeometry())
										{
											D2D1_MATRIX_3X2_F originalTransform;
											barDeviceContext->GetTransform(
												&originalTransform);
											FLOAT selectorIntroScale = static_cast<FLOAT>(
												0.86 + 0.14 * selectionProgress);
											FLOAT selectorWidth = static_cast<FLOAT>(
												BarThicknessFineDialSelectorWidthDip
												* panelAnimationScale * uiZoom)
												* selectorIntroScale;
											FLOAT selectorHeight = static_cast<FLOAT>(
												BarThicknessFineDialSelectorHeightDip
												* panelAnimationScale * uiZoom)
												* selectorIntroScale;
											FLOAT selectorGap = static_cast<FLOAT>(
												(centerLength / 2.0 + 2.0
													* panelAnimationScale) * uiZoom);
											FLOAT centerXPixel = static_cast<FLOAT>(
												dialCenterX * uiZoom);
											FLOAT centerYPixel = static_cast<FLOAT>(
												dialCenterY * uiZoom);
											barDeviceContext->SetTransform(
												D2D1::Matrix3x2F::Scale(
													selectorWidth, selectorHeight)
												* D2D1::Matrix3x2F::Translation(
													centerXPixel,
													centerYPixel - selectorGap
														- selectorHeight)
												* originalTransform);
											barDeviceContext->FillGeometry(
												selector, centerBrush);
											barDeviceContext->SetTransform(
												D2D1::Matrix3x2F::Scale(
													selectorWidth, -selectorHeight)
												* D2D1::Matrix3x2F::Translation(
													centerXPixel,
													centerYPixel + selectorGap
														+ selectorHeight)
												* originalTransform);
											barDeviceContext->FillGeometry(
												selector, centerBrush);
											barDeviceContext->SetTransform(
												originalTransform);
											}
										}
									}
								}
							}
						}

						struct ThicknessButtonRender
						{
							BarUISetShapeEnum shape;
							BarUISetWordEnum numberWord;
							BarUiValueClass* pressScale;
							int presetIndex;
						};
						const ThicknessButtonRender thicknessButtons[] =
						{
							{ BarUISetShapeEnum::DrawAttributeBar_ThicknessFine,
								BarUISetWordEnum::DrawAttributeBar_ThicknessFineNumber,
								&state.drawAttributeThicknessFinePressScale, 0 },
							{ BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium,
								BarUISetWordEnum::DrawAttributeBar_ThicknessMediumNumber,
								&state.drawAttributeThicknessMediumPressScale, 1 },
							{ BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse,
								BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber,
								&state.drawAttributeThicknessCoarsePressScale, 2 },
							{ BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust,
								BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay,
								&state.drawAttributeThicknessAdjustPressScale, -1 },
						};
						int displayedThickness = static_cast<int>(lround(clamp(
							static_cast<double>(state.drawAttributePenThickness.val),
							0.0, 999.0)));
						for (const auto& button : thicknessButtons)
						{
							auto shape = state.shapeMap[button.shape];
							BarUiInheritClass shapeInherit =
								shape->Inherit(TopLeft, *panel);
bool presetButton = button.presetIndex >= 0;
								auto numberWord = presetButton
									? state.wordMap[button.numberWord] : nullptr;
								bool adjustVisible =
									PenModeUsesThicknessPresets(
										stateMode.Pen.ModeSelect);
								bool highlighterPreset =
									presetButton
									&& stateMode.Pen.ModeSelect
										== PenModeSelectEnum::IdtPenHighlighter1;
								double buttonOpacity = presetButton
									? static_cast<double>(numberWord->pct.val)
									: (adjustVisible ? contentOpacity : 0.0);
							// 圆点读取 Shape 的当前边框色，跟随白色到青色的已有颜色动画。
							COLORREF buttonColor = shape->frame.value().val;

							double pressScale = button.pressScale->val;
							if (!isfinite(pressScale) || pressScale <= 0.0)
								pressScale = 1.0;
							D2D1_MATRIX_3X2_F originalTransform;
							barDeviceContext->GetTransform(&originalTransform);
							bool transformChanged =
								abs(pressScale - 1.0) > 0.000001;
							if (transformChanged)
							{
								FLOAT centerX = static_cast<FLOAT>(
									(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
								FLOAT centerY = static_cast<FLOAT>(
									(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
								barDeviceContext->SetTransform(
									D2D1::Matrix3x2F::Scale(
										static_cast<FLOAT>(pressScale),
										static_cast<FLOAT>(pressScale),
										D2D1::Point2F(centerX, centerY))
									* originalTransform);
							}

							state.spec.Shape(barDeviceContext, *shape, shapeInherit);
							if (!presetButton)
							{
								auto adjustSvg = state.svgMap[
									BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust];
								state.spec.Svg(barDeviceContext, *adjustSvg,
									adjustSvg->Inherit(Center, *shape));
							}
else
								{
									// 数字可能只作为圆点透明度来源，也要刷新继承坐标供 dirty 计算使用。
									BarUiInheritClass numberInherit =
										numberWord->Inherit(TopLeft, *panel);
									int actualPx = GetBarThicknessPresetPx(
										stateMode.Pen.ModeSelect,
										button.presetIndex, state.barStyle.dpiZoom);
									if (highlighterPreset)
									{
										// 荧光笔预设始终显示数字，不再画圆点。
										if (buttonOpacity > 0.000001 && numberWord)
										{
											wstring numberText = to_wstring(actualPx);
											numberWord->content.SetVal(numberText);
											numberWord->content.SetTar(numberText);
											numberWord->color.SetDirect(buttonColor);
											numberWord->pct.SetDirect(buttonOpacity);
											state.spec.Word(barDeviceContext, *numberWord,
												numberInherit,
												DWRITE_FONT_WEIGHT_BOLD,
												DWRITE_TEXT_ALIGNMENT_CENTER);
										}
									}
									else
									{
										ID2D1SolidColorBrush* buttonBrush =
											state.spec.GetFrameSolidColorBrush(
												barDeviceContext, buttonColor,
												buttonOpacity);
										if (buttonOpacity > 0.000001 && buttonBrush
											&& uiZoom > 0.0f)
										{
											FLOAT centerX = static_cast<FLOAT>(
												(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
											FLOAT centerY = static_cast<FLOAT>(
												(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
											FLOAT innerDiameter = max(1.0f,
												static_cast<FLOAT>(min(
													shape->w.val, shape->h.val) * uiZoom)
												- 8.0f * uiZoom);
											FLOAT diameter = min(
												static_cast<FLOAT>(actualPx
													* panelAnimationScale), innerDiameter);
											D2D1_ELLIPSE ellipse = D2D1::Ellipse(
												D2D1::Point2F(centerX, centerY),
												diameter / 2.0f, diameter / 2.0f);
											barDeviceContext->FillEllipse(
												&ellipse, buttonBrush);
											if (static_cast<FLOAT>(actualPx
												* panelAnimationScale) > innerDiameter)
											{
												// 填满时用黑白高对比数字保留真实设备像素值。
												wstring numberText = to_wstring(actualPx);
												numberWord->content.SetVal(numberText);
												numberWord->content.SetTar(numberText);
												numberWord->color.SetDirect(
													GetBarReadableTextColor(buttonColor));
												state.spec.Word(barDeviceContext, *numberWord,
													numberInherit,
													DWRITE_FONT_WEIGHT_BOLD,
													DWRITE_TEXT_ALIGNMENT_CENTER);
											}
										}
									}
								}
							if (transformChanged)
								barDeviceContext->SetTransform(originalTransform);
						}

						wstring thicknessText =
							L"粗细 " + to_wstring(displayedThickness);
						thicknessDisplay->content.SetVal(thicknessText);
						thicknessDisplay->content.SetTar(thicknessText);
						state.spec.Word(barDeviceContext, *thicknessDisplay,
							thicknessDisplayInherit,
							DWRITE_FONT_WEIGHT_NORMAL,
							DWRITE_TEXT_ALIGNMENT_LEADING);
					}
					state.spec.SetFrameDiffuseMaskGeometryScale(1.0);
				}

				// 几何属性
				{
					auto panel =
						state.shapeMap[BarUISetShapeEnum::GeometryAttributeBar];
					double panelScale = panel->w.val
						/ BarGeometryAttributeExpandedWidth;
					if (!isfinite(panelScale) || panelScale <= 0.000001)
						panelScale = 1.0;
					state.spec.SetFrameDiffuseMaskGeometryScale(1.0 / panelScale);
					state.spec.Shape(barDeviceContext, *panel,
						panel->Inherit(Center,
							state.barButtonSet.preset[static_cast<int>(
								BarButtonPresetEnum::Geometry)]->button),
						&state.current, true);

					auto divider = state.shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_Divider];
					state.spec.Shape(barDeviceContext, *divider,
						divider->Inherit(TopLeft, *panel));

					struct GeometryShapeButtonRender
					{
						BarUISetShapeEnum shape;
						BarUISetSvgEnum icon;
						BarUISetWordEnum word;
						BarUiValueClass* pressScale;
					};
					const GeometryShapeButtonRender shapeButtons[] =
					{
						{ BarUISetShapeEnum::GeometryAttributeBar_StraightLine,
							BarUISetSvgEnum::GeometryAttributeBar_StraightLine,
							BarUISetWordEnum::GeometryAttributeBar_StraightLine,
							&state.geometryStraightLinePressScale },
						{ BarUISetShapeEnum::GeometryAttributeBar_Rectangle,
							BarUISetSvgEnum::GeometryAttributeBar_Rectangle,
							BarUISetWordEnum::GeometryAttributeBar_Rectangle,
							&state.geometryRectanglePressScale },
					};
					FLOAT uiZoom = static_cast<FLOAT>(frameZoom);
					ID2D1StrokeStyle* roundStrokeStyle =
						state.spec.GetRoundStrokeStyle();
					for (const auto& button : shapeButtons)
					{
						auto shape = state.shapeMap[button.shape];
						auto word = state.wordMap[button.word];
						BarUiInheritClass shapeInherit =
							shape->Inherit(TopLeft, *panel);
						double pressScale = button.pressScale->val;
						if (!isfinite(pressScale) || pressScale <= 0.0)
							pressScale = 1.0;
						D2D1_MATRIX_3X2_F originalTransform;
						barDeviceContext->GetTransform(&originalTransform);
						bool transformChanged = abs(pressScale - 1.0) > 0.000001;
						if (transformChanged)
						{
							FLOAT centerX = static_cast<FLOAT>(
								(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
							FLOAT centerY = static_cast<FLOAT>(
								(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
							barDeviceContext->SetTransform(
								D2D1::Matrix3x2F::Scale(
									static_cast<FLOAT>(pressScale),
									static_cast<FLOAT>(pressScale),
									D2D1::Point2F(centerX, centerY))
								* originalTransform);
						}

						state.spec.Shape(barDeviceContext, *shape, shapeInherit);
						auto icon = state.svgMap[button.icon];
						state.spec.Svg(barDeviceContext, *icon,
							icon->Inherit(TopLeft, *panel));
						state.spec.Word(barDeviceContext, *word,
							word->Inherit(TopLeft, *panel),
							DWRITE_FONT_WEIGHT_NORMAL,
							DWRITE_TEXT_ALIGNMENT_CENTER);
						if (transformChanged)
							barDeviceContext->SetTransform(originalTransform);
					}

					const BarUISetShapeEnum thicknessShapes[] =
					{
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine,
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium,
						BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse,
					};
					const BarUISetWordEnum thicknessWords[] =
					{
						BarUISetWordEnum::GeometryAttributeBar_ThicknessFineNumber,
						BarUISetWordEnum::GeometryAttributeBar_ThicknessMediumNumber,
						BarUISetWordEnum::GeometryAttributeBar_ThicknessCoarseNumber,
					};
					BarUiValueClass* thicknessPressScales[] =
					{
						&state.geometryThicknessFinePressScale,
						&state.geometryThicknessMediumPressScale,
						&state.geometryThicknessCoarsePressScale,
					};
					const bool thicknessPressed[] =
					{
						state.barState.geometryAttributeBar.thicknessFinePress,
						state.barState.geometryAttributeBar.thicknessMediumPress,
						state.barState.geometryAttributeBar.thicknessCoarsePress,
					};
					for (size_t index = 0; index < 3; ++index)
					{
						auto shape = state.shapeMap[thicknessShapes[index]];
						auto numberWord = state.wordMap[thicknessWords[index]];
						BarUiInheritClass shapeInherit =
							shape->Inherit(TopLeft, *panel);
						double pressScale = thicknessPressScales[index]->val;
						if (!isfinite(pressScale) || pressScale <= 0.0)
							pressScale = 1.0;
						D2D1_MATRIX_3X2_F originalTransform;
						barDeviceContext->GetTransform(&originalTransform);
						bool transformChanged = abs(pressScale - 1.0) > 0.000001;
						if (transformChanged)
						{
							FLOAT centerX = static_cast<FLOAT>(
								(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
							FLOAT centerY = static_cast<FLOAT>(
								(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
							barDeviceContext->SetTransform(
								D2D1::Matrix3x2F::Scale(
									static_cast<FLOAT>(pressScale),
									static_cast<FLOAT>(pressScale),
									D2D1::Point2F(centerX, centerY))
								* originalTransform);
						}

						state.spec.Shape(barDeviceContext, *shape, shapeInherit);
						double panelOpacity = clamp(
							static_cast<double>(panel->pct.val)
								/ BarDrawAttributeSurfaceOpacity,
							0.0, 1.0);
						FLOAT contentOpacity = static_cast<FLOAT>(panelOpacity
							* (thicknessPressed[index] ? 0.70 : 1.0));
						COLORREF contentColor = shape->frame.value().val;
						ID2D1SolidColorBrush* contentBrush =
							state.spec.GetFrameSolidColorBrush(barDeviceContext,
								contentColor, contentOpacity);
						int presetPx = GetBarThicknessPresetPx(
							PenModeSelectEnum::IdtPenBrush1, index,
							state.barStyle.dpiZoom);
						if (contentBrush && contentOpacity > 0.000001F
							&& uiZoom > 0.0F)
						{
							FLOAT centerX = static_cast<FLOAT>(
								(shapeInherit.x + shape->w.val / 2.0) * uiZoom);
							FLOAT centerY = static_cast<FLOAT>(
								(shapeInherit.y + shape->h.val / 2.0) * uiZoom);
							FLOAT diameter = max(1.0F,
								static_cast<FLOAT>(min(shape->w.val, shape->h.val)
									* uiZoom - 8.0 * panelScale * uiZoom));
							FLOAT requestedThickness = max(1.0F,
								static_cast<FLOAT>(presetPx * panelScale));
							FLOAT previewThickness = min(
								requestedThickness, diameter);
							FLOAT halfDiagonal = max(0.0F,
								(diameter - previewThickness) * 0.5F)
								/ static_cast<FLOAT>(sqrt(2.0));
							if (halfDiagonal <= 0.25F)
							{
								D2D1_ELLIPSE ellipse = D2D1::Ellipse(
									D2D1::Point2F(centerX, centerY),
									previewThickness / 2.0F,
									previewThickness / 2.0F);
								barDeviceContext->FillEllipse(&ellipse, contentBrush);
							}
							else
							{
								barDeviceContext->DrawLine(
									D2D1::Point2F(centerX - halfDiagonal,
										centerY + halfDiagonal),
									D2D1::Point2F(centerX + halfDiagonal,
										centerY - halfDiagonal),
									contentBrush, previewThickness,
									roundStrokeStyle);
							}
						}
						if (numberWord->pct.val > 0.000001)
						{
							numberWord->color.SetDirect(
								GetBarReadableTextColor(contentColor));
							state.spec.Word(barDeviceContext, *numberWord,
								numberWord->Inherit(TopLeft, *panel),
								DWRITE_FONT_WEIGHT_BOLD,
								DWRITE_TEXT_ALIGNMENT_CENTER);
						}
						if (transformChanged)
							barDeviceContext->SetTransform(originalTransform);
					}

					auto close = state.shapeMap[
						BarUISetShapeEnum::GeometryAttributeBar_Close];
					BarUiInheritClass closeInherit =
						close->Inherit(TopLeft, *panel);
					double closePressScale = state.geometryClosePressScale.val;
					if (!isfinite(closePressScale) || closePressScale <= 0.0)
						closePressScale = 1.0;
					D2D1_MATRIX_3X2_F closeOriginalTransform;
					barDeviceContext->GetTransform(&closeOriginalTransform);
					bool closeTransformChanged =
						abs(closePressScale - 1.0) > 0.000001;
					if (closeTransformChanged)
					{
						FLOAT centerX = static_cast<FLOAT>(
							(closeInherit.x + close->w.val / 2.0) * uiZoom);
						FLOAT centerY = static_cast<FLOAT>(
							(closeInherit.y + close->h.val / 2.0) * uiZoom);
						barDeviceContext->SetTransform(
							D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(closePressScale),
								static_cast<FLOAT>(closePressScale),
								D2D1::Point2F(centerX, centerY))
							* closeOriginalTransform);
					}
					state.spec.Shape(barDeviceContext, *close, closeInherit);
					auto closeSvg = state.svgMap[
						BarUISetSvgEnum::GeometryAttributeBar_Close];
					state.spec.Svg(barDeviceContext, *closeSvg,
						closeSvg->Inherit(TopLeft, *panel));
					if (closeTransformChanged)
						barDeviceContext->SetTransform(closeOriginalTransform);
					state.spec.SetFrameDiffuseMaskGeometryScale(1.0);
					RefreshBorderCursorVisibleRegions(frameZoom);
				}

				// More 必须先画、主栏后画，收拢部分才会从主栏下层自然出现。
				auto DrawMainBar = [&]()
				{
					auto obj = BarUISetShapeEnum::MainBar;
					state.spec.Shape(barDeviceContext, *state.shapeMap[obj], BarUiInheritClass(state.shapeMap[obj]->inhX, state.shapeMap[obj]->inhY), &state.current, true);

					for (int id = 0; id < state.barButtonSet.tot; id++)
					{
						BarButtonClass* temp = state.barButtonSet.buttonList.Get(id);
						if (temp == nullptr) continue;

						BarUiInheritClass buttonInherit = temp->button.Inherit(
							CenterFromTopLeft, *state.shapeMap[BarUISetShapeEnum::MainBar]);
						double pressScale = temp->pressScale.val;
						if (!isfinite(pressScale) || pressScale <= 0.0) pressScale = 1.0;
						D2D1_MATRIX_3X2_F originalTransform;
						barDeviceContext->GetTransform(&originalTransform);
						bool transformChanged = abs(pressScale - 1.0) > 0.000001;
						if (transformChanged)
						{
							// 整个按钮组合围绕背景中心缩放，组件自身的布局值和命中区域保持不变。
							FLOAT centerX = static_cast<FLOAT>(
								(buttonInherit.x + temp->button.w.val / 2.0) * frameZoom);
							FLOAT centerY = static_cast<FLOAT>(
								(buttonInherit.y + temp->button.h.val / 2.0) * frameZoom);
							D2D1_MATRIX_3X2_F scaleTransform = D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(pressScale), static_cast<FLOAT>(pressScale),
								D2D1::Point2F(centerX, centerY));
							barDeviceContext->SetTransform(scaleTransform * originalTransform);
						}

						state.spec.Shape(barDeviceContext, temp->button, buttonInherit);
						if (temp->preset == BarButtonPresetEnum::Divider)
						{
							// Divider 是纯 Shape 视觉，不绘制占位 SVG 或文字层。
							if (transformChanged) barDeviceContext->SetTransform(originalTransform);
							continue;
						}
						BarUiInheritClass iconInherit = temp->icon.Inherit(Center, temp->button);
						if (temp->iconKind == BarButtonIconKindEnum::Png)
						{
							// PNG 复用 SVG 图标控制器的布局与透明度动画，仅替换最终绘制载荷。
							temp->pngIcon.x.SetDirect(temp->icon.x.val);
							temp->pngIcon.y.SetDirect(temp->icon.y.val);
							temp->pngIcon.w.SetDirect(temp->icon.w.val);
							temp->pngIcon.h.SetDirect(temp->icon.h.val);
							temp->pngIcon.angle.SetDirect(temp->icon.angle.val);
							temp->pngIcon.pct.SetDirect(temp->icon.pct.val);
							temp->pngIcon.enable.val = temp->icon.enable.val;
							temp->pngIcon.enable.tar = temp->icon.enable.tar;
							state.spec.Png(barDeviceContext, temp->pngIcon, temp->pngIcon.UpInh(iconInherit));
						}
						else
						{
							state.spec.Svg(barDeviceContext, temp->icon, iconInherit);
						}
						state.spec.Word(barDeviceContext, temp->name, temp->name.Inherit(Center, temp->button));
						if (transformChanged) barDeviceContext->SetTransform(originalTransform);
					}
				};

				// 更多按钮保留主栏局部坐标，浮层自身仍只按上下展开方向翻转物理行。
				auto morePanel = state.shapeMap[BarUISetShapeEnum::MorePanel];
				if (morePanel && morePanel->pct.val > 0.000001)
				{
					state.spec.Shape(barDeviceContext, *morePanel,
						BarUiInheritClass(morePanel->inhX, morePanel->inhY),
						&state.current, true);
					auto moreDivider = state.shapeMap[
						BarUISetShapeEnum::MorePanelDivider];
					if (moreDivider && moreDivider->pct.val > 0.000001)
						state.spec.Shape(barDeviceContext, *moreDivider,
							BarUiInheritClass(moreDivider->inhX,
								moreDivider->inhY));

					auto DrawMoreButton = [&](BarButtonClass* button)
						{
							if (!button || button->icon.pct.val <= 0.000001) return;
							BarUiInheritClass buttonInherit = button->button.Inherit(
								CenterFromTopLeft,
								*state.shapeMap[BarUISetShapeEnum::MainBar]);
							double pressScale = button->pressScale.val;
							if (!isfinite(pressScale) || pressScale <= 0.0)
								pressScale = 1.0;
							D2D1_MATRIX_3X2_F originalTransform;
							barDeviceContext->GetTransform(&originalTransform);
							bool transformChanged = abs(pressScale - 1.0) > 0.000001;
							if (transformChanged)
							{
								FLOAT centerX = static_cast<FLOAT>(
									(buttonInherit.x + button->button.w.val / 2.0)
									* frameZoom);
								FLOAT centerY = static_cast<FLOAT>(
									(buttonInherit.y + button->button.h.val / 2.0)
									* frameZoom);
								barDeviceContext->SetTransform(
									D2D1::Matrix3x2F::Scale(
										static_cast<FLOAT>(pressScale),
										static_cast<FLOAT>(pressScale),
										D2D1::Point2F(centerX, centerY))
									* originalTransform);
							}
							state.spec.Shape(barDeviceContext, button->button,
								buttonInherit);
							BarUiInheritClass iconInherit = button->icon.Inherit(
								Center, button->button);
							if (button->iconKind == BarButtonIconKindEnum::Png)
							{
								button->pngIcon.x.SetDirect(button->icon.x.val);
								button->pngIcon.y.SetDirect(button->icon.y.val);
								button->pngIcon.w.SetDirect(button->icon.w.val);
								button->pngIcon.h.SetDirect(button->icon.h.val);
								button->pngIcon.angle.SetDirect(button->icon.angle.val);
								button->pngIcon.pct.SetDirect(button->icon.pct.val);
								button->pngIcon.enable.val = button->icon.enable.val;
								button->pngIcon.enable.tar = button->icon.enable.tar;
								state.spec.Png(barDeviceContext, button->pngIcon,
									button->pngIcon.UpInh(iconInherit));
							}
							else state.spec.Svg(barDeviceContext, button->icon,
								iconInherit);
							state.spec.Word(barDeviceContext, button->name,
								button->name.Inherit(Center, button->button));
							if (transformChanged)
								barDeviceContext->SetTransform(originalTransform);
						};
					BarMoreButtonSnapshotClass moreSnapshot =
						state.barButtonSet.GetMoreButtonSnapshot();
					for (const shared_ptr<BarButtonClass>& button :
						moreSnapshot.explicitMore)
						DrawMoreButton(button.get());
					for (const shared_ptr<BarButtonClass>& button :
						moreSnapshot.forcedOverflow)
						DrawMoreButton(button.get());

					auto moreClose = state.shapeMap[
						BarUISetShapeEnum::MorePanelCloseHit];
					double closePressScale = state.moreClosePressScale.val;
					if (!isfinite(closePressScale) || closePressScale <= 0.0)
						closePressScale = 1.0;
					D2D1_MATRIX_3X2_F closeOriginalTransform;
					barDeviceContext->GetTransform(&closeOriginalTransform);
					bool closeTransformChanged = moreClose
						&& abs(closePressScale - 1.0) > 0.000001;
					if (closeTransformChanged)
					{
						// X 的按压缩放只作用于绘制，不改变侧栏命中区域。
						FLOAT centerX = static_cast<FLOAT>(
							(moreClose->inhX + moreClose->w.val / 2.0)
							* frameZoom);
						FLOAT centerY = static_cast<FLOAT>(
							(moreClose->inhY + moreClose->h.val / 2.0)
							* frameZoom);
						barDeviceContext->SetTransform(D2D1::Matrix3x2F::Scale(
							static_cast<FLOAT>(closePressScale),
							static_cast<FLOAT>(closePressScale),
							D2D1::Point2F(centerX, centerY)) * closeOriginalTransform);
					}
					if (moreClose)
						state.spec.Shape(barDeviceContext, *moreClose,
							BarUiInheritClass(moreClose->inhX, moreClose->inhY));
					auto moreCloseSvg = state.svgMap[
						BarUISetSvgEnum::MorePanelClose];
					if (moreCloseSvg)
						state.spec.Svg(barDeviceContext, *moreCloseSvg,
							BarUiInheritClass(moreCloseSvg->inhX,
								moreCloseSvg->inhY));
					if (closeTransformChanged)
						barDeviceContext->SetTransform(closeOriginalTransform);
				}
				DrawMainBar();
			}
			{ /**/ }

			// 主按钮
				{
					auto obj = BarUISetSuperellipseEnum::MainButton;
					state.spec.Superellipse(barDeviceContext, *state.superellipseMap[obj], BarUiInheritClass(state.superellipseMap[obj]->inhX, state.superellipseMap[obj]->inhY), &state.current, true);

				{
					auto obj = BarUISetSvgEnum::logo1;
						state.spec.Svg(barDeviceContext, *state.svgMap[obj], state.svgMap[obj]->Inherit(Center, *state.superellipseMap[BarUISetSuperellipseEnum::MainButton]));
					}
				}
				// 动画中的子控件可能暂时超出父级边界，脏区必须包含其真实新旧范围以清除残影。
				for (BarUISetShapeEnum moreShape : {
					BarUISetShapeEnum::MorePanel,
					BarUISetShapeEnum::MorePanelDivider,
					BarUISetShapeEnum::MorePanelCloseHit })
				{
					auto shape = state.shapeMap[moreShape];
					UnionShapeBounds(state.current, shape.get());
				}
				if (auto moreCloseSvg = state.svgMap[BarUISetSvgEnum::MorePanelClose])
					UnionSvgBounds(state.current, moreCloseSvg.get());
				BarMoreButtonSnapshotClass dirtyMoreSnapshot =
					state.barButtonSet.GetMoreButtonSnapshot();
				auto IncludeMoreButtonDirty = [&](const shared_ptr<BarButtonClass>& button)
					{
						if (!button) return;
						UnionShapeBounds(state.current, &button->button);
						if (button->iconKind == BarButtonIconKindEnum::Png)
							UnionPngBounds(state.current, &button->pngIcon);
						else UnionSvgBounds(state.current, &button->icon);
						UnionWordBounds(state.current, &button->name);
					};
				for (const shared_ptr<BarButtonClass>& button :
					dirtyMoreSnapshot.explicitMore)
					IncludeMoreButtonDirty(button);
				for (const shared_ptr<BarButtonClass>& button :
					dirtyMoreSnapshot.forcedOverflow)
					IncludeMoreButtonDirty(button);
				for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar);
					i <= static_cast<int>(
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerHoldHint);
					i++)
				{
					auto obj = state.shapeMap[static_cast<BarUISetShapeEnum>(i)];
					UnionShapeBounds(state.current, obj.get());
				}
				for (int i = static_cast<int>(BarUISetSvgEnum::DrawAttributeBar_ColorSelect1);
					i <= static_cast<int>(
						BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose);
					i++)
				{
					auto obj = state.svgMap[static_cast<BarUISetSvgEnum>(i)];
					UnionSvgBounds(state.current, obj.get());
				}
				for (int i = static_cast<int>(BarUISetWordEnum::DrawAttributeBar_Brush1);
					i <= static_cast<int>(
						BarUISetWordEnum::DrawAttributeBar_ColorPickerHoldLabel);
					i++)
				{
					auto obj = state.wordMap[static_cast<BarUISetWordEnum>(i)];
					UnionWordBounds(state.current, obj.get());
				}
				UnionShapeBounds(state.current, state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner].get());
				UnionPngBounds(state.current, state.pngMap[
					BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel].get());
				UnionSvgBounds(state.current, state.svgMap[
					BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check].get());
				for (int i = static_cast<int>(
					BarUISetShapeEnum::GeometryAttributeBar);
					i <= static_cast<int>(
						BarUISetShapeEnum::GeometryAttributeBar_Close);
					++i)
				{
					auto geometryShape =
						state.shapeMap[static_cast<BarUISetShapeEnum>(i)];
					UnionShapeBounds(state.current, geometryShape.get());
				}
				for (int i = static_cast<int>(
					BarUISetWordEnum::GeometryAttributeBar_StraightLine);
					i <= static_cast<int>(
						BarUISetWordEnum::GeometryAttributeBar_ThicknessCoarseNumber);
					++i)
				{
					auto geometryWord =
						state.wordMap[static_cast<BarUISetWordEnum>(i)];
					UnionWordBounds(state.current, geometryWord.get());
				}
				for (int i = static_cast<int>(
					BarUISetSvgEnum::GeometryAttributeBar_StraightLine);
					i <= static_cast<int>(BarUISetSvgEnum::GeometryAttributeBar_Close);
					++i)
				{
					auto geometrySvg = state.svgMap[static_cast<BarUISetSvgEnum>(i)];
					UnionSvgBounds(state.current, geometrySvg.get());
				}
				for (int id = 0; id < state.barButtonSet.tot; id++)
				{
					BarButtonClass* temp = state.barButtonSet.buttonList.Get(id);
					if (!temp) continue;
					UnionShapeBounds(state.current, &temp->button);
					if (temp->iconKind == BarButtonIconKindEnum::Png)
						UnionPngBounds(state.current, &temp->pngIcon);
					else UnionSvgBounds(state.current, &temp->icon);
					UnionWordBounds(state.current, &temp->name);
				}
				{
					auto obj = BarUISetSvgEnum::logoInk;
					state.spec.Svg(barDeviceContext, *state.svgMap[obj], state.svgMap[obj]->Inherit(Center, *state.superellipseMap[BarUISetSuperellipseEnum::MainButton]));
				}
			}
		{ /**/ }

		// 浮窗不擦除已经绘制的背景；徽标随后覆盖，形成从按钮下层展开的层级。
		{
			auto panel = state.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
			if (panel->pct.val > 0.0 && state.barMedia.formatCache)
			{
				// 属性栏可见期间保留两档完整字号，浮窗首帧只做缓存查询。
				state.barMedia.formatCache->GetFormat(
					L"HarmonyOS Sans SC",
					static_cast<FLOAT>(
						BarThicknessTooltipTitleFontSize * frameZoom),
					dWriteFontCollection.Get(),
					DWRITE_FONT_WEIGHT_SEMI_BOLD,
					DWRITE_FONT_STYLE_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL,
					L"zh-cn",
					DWRITE_TEXT_ALIGNMENT_LEADING,
					DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				state.barMedia.formatCache->GetFormat(
					L"HarmonyOS Sans SC",
					static_cast<FLOAT>(
						BarThicknessTooltipBodyFontSize * frameZoom),
					dWriteFontCollection.Get(),
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STYLE_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL,
					L"zh-cn",
					DWRITE_TEXT_ALIGNMENT_LEADING,
					DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			}
			auto DrawThicknessPopup =
				[&](BarUISetShapeEnum popupShapeType,
					BarUISetWordEnum popupTitleType,
					BarUISetWordEnum popupBodyType,
					BarUISetShapeEnum closeHitType,
					BarUISetSvgEnum closeSvgType,
					BarUISetShapeEnum anchorBadgeType,
					BarUiValueClass& closePressScale)
				{
					auto popup = state.shapeMap[popupShapeType];
					auto popupTitle = state.wordMap[popupTitleType];
					auto popupBody = state.wordMap[popupBodyType];
					auto closeHit = state.shapeMap[closeHitType];
					auto closeSvg = state.svgMap[closeSvgType];
					BarUiInheritClass popupInherit =
						popup->Inherit(TopLeft, *panel);
					double popupGeometryScale = popup->rw.has_value()
						? popup->rw->val / 4.0 : 1.0;
					if (!isfinite(popupGeometryScale)
						|| popupGeometryScale <= 0.000001)
						popupGeometryScale = 1.0;
					state.spec.SetFrameDiffuseMaskGeometryScale(
						1.0 / popupGeometryScale);
					state.spec.Shape(barDeviceContext, *popup,
						popupInherit, &state.current, false);
					state.spec.SetFrameDiffuseMaskGeometryScale(1.0);

					BarUiInheritClass titleInherit =
						popupTitle->Inherit(TopLeft, *panel);
					BarUiInheritClass bodyInherit =
						popupBody->Inherit(TopLeft, *panel);
					BarUiInheritClass closeHitInherit =
						closeHit->Inherit(TopLeft, *panel);
					BarUiInheritClass closeInherit =
						closeSvg->Inherit(TopLeft, *panel);
					if (popup->pct.val <= 0.0
						|| popupGeometryScale <= 0.000001)
						return;

					auto anchorBadge = state.shapeMap[anchorBadgeType];
					double anchorX = anchorBadge->inhX
						+ anchorBadge->w.val / 2.0;
					double anchorY = anchorBadge->inhY
						+ anchorBadge->h.val / 2.0;
					auto UnscaleCoordinate =
						[&](double value, double anchor)
						{
							return anchor
								+ (value - anchor) / popupGeometryScale;
						};

					double titleW = popupTitle->w.val;
					double titleH = popupTitle->h.val;
					double titleSize = popupTitle->size.val;
					double bodyW = popupBody->w.val;
					double bodyH = popupBody->h.val;
					double bodySize = popupBody->size.val;
					double closeHitW = closeHit->w.val;
					double closeHitH = closeHit->h.val;
					double closeHitRw = closeHit->rw.has_value()
						? static_cast<double>(closeHit->rw->val) : 0.0;
					double closeHitRh = closeHit->rh.has_value()
						? static_cast<double>(closeHit->rh->val) : 0.0;
					double closeW = closeSvg->w.val;
					double closeH = closeSvg->h.val;
					popupTitle->w.SetDirect(titleW / popupGeometryScale);
					popupTitle->h.SetDirect(titleH / popupGeometryScale);
					popupTitle->size.SetDirect(
						BarThicknessTooltipTitleFontSize);
					popupBody->w.SetDirect(bodyW / popupGeometryScale);
					popupBody->h.SetDirect(bodyH / popupGeometryScale);
					popupBody->size.SetDirect(
						BarThicknessTooltipBodyFontSize);
					closeHit->w.SetDirect(closeHitW / popupGeometryScale);
					closeHit->h.SetDirect(closeHitH / popupGeometryScale);
					if (closeHit->rw.has_value())
						closeHit->rw->SetDirect(
							closeHitRw / popupGeometryScale);
					if (closeHit->rh.has_value())
						closeHit->rh->SetDirect(
							closeHitRh / popupGeometryScale);
					closeSvg->w.SetDirect(closeW / popupGeometryScale);
					closeSvg->h.SetDirect(closeH / popupGeometryScale);

					D2D1_MATRIX_3X2_F originalTransform;
					barDeviceContext->GetTransform(&originalTransform);
					barDeviceContext->SetTransform(
						D2D1::Matrix3x2F::Scale(
							static_cast<FLOAT>(popupGeometryScale),
							static_cast<FLOAT>(popupGeometryScale),
							D2D1::Point2F(
								static_cast<FLOAT>(anchorX * frameZoom),
								static_cast<FLOAT>(anchorY * frameZoom)))
						* originalTransform);
					// 文字和 SVG 使用完整尺寸资源再整体缩放，避免动画每帧创建新字号格式。
					state.spec.Word(barDeviceContext, *popupTitle,
						BarUiInheritClass(
							UnscaleCoordinate(titleInherit.x, anchorX),
							UnscaleCoordinate(titleInherit.y, anchorY)),
						DWRITE_FONT_WEIGHT_SEMI_BOLD,
						DWRITE_TEXT_ALIGNMENT_LEADING);
					state.spec.Word(barDeviceContext, *popupBody,
						BarUiInheritClass(
							UnscaleCoordinate(bodyInherit.x, anchorX),
							UnscaleCoordinate(bodyInherit.y, anchorY)),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_LEADING);
					double unscaledCloseX =
						UnscaleCoordinate(closeHitInherit.x, anchorX);
					double unscaledCloseY =
						UnscaleCoordinate(closeHitInherit.y, anchorY);
					double closeScale = closePressScale.val;
					D2D1_MATRIX_3X2_F popupTransform;
					barDeviceContext->GetTransform(&popupTransform);
					if (abs(closeScale - 1.0) > 0.000001)
					{
						// 背景与叉号围绕 20px 按钮中心同步缩放，命中区域保持原尺寸。
						double closeCenterX =
							unscaledCloseX + closeHit->w.val / 2.0;
						double closeCenterY =
							unscaledCloseY + closeHit->h.val / 2.0;
						barDeviceContext->SetTransform(
							D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(closeScale),
								static_cast<FLOAT>(closeScale),
								D2D1::Point2F(
									static_cast<FLOAT>(
										closeCenterX * frameZoom),
									static_cast<FLOAT>(
										closeCenterY * frameZoom)))
							* popupTransform);
					}
					state.spec.Shape(barDeviceContext, *closeHit,
						BarUiInheritClass(unscaledCloseX, unscaledCloseY));
					state.spec.Svg(barDeviceContext, *closeSvg,
						BarUiInheritClass(
							UnscaleCoordinate(closeInherit.x, anchorX),
							UnscaleCoordinate(closeInherit.y, anchorY)));
					barDeviceContext->SetTransform(originalTransform);

					popupTitle->w.SetDirect(titleW);
					popupTitle->h.SetDirect(titleH);
					popupTitle->size.SetDirect(titleSize);
					popupBody->w.SetDirect(bodyW);
					popupBody->h.SetDirect(bodyH);
					popupBody->size.SetDirect(bodySize);
					closeHit->w.SetDirect(closeHitW);
					closeHit->h.SetDirect(closeHitH);
					if (closeHit->rw.has_value())
						closeHit->rw->SetDirect(closeHitRw);
					if (closeHit->rh.has_value())
						closeHit->rh->SetDirect(closeHitRh);
					closeSvg->w.SetDirect(closeW);
					closeSvg->h.SetDirect(closeH);
				};

			auto DrawPenTypeOverlay = [&]()
			{
			// Overflow Hint 属于属性固有内容，放在菜单下层；问号 Tooltip 随后覆盖菜单。
			auto overflowBadge = state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge];
			state.spec.Shape(barDeviceContext, *overflowBadge,
				overflowBadge->Inherit(TopLeft, *panel));
			auto overflowInfo = state.svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo];
			state.spec.Svg(barDeviceContext, *overflowInfo,
				overflowInfo->Inherit(TopLeft, *panel));

			// 笔型扩展菜单位于属性内容之上，帮助浮窗随后再覆盖菜单。
			auto penTypeMenu = state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu];
			double menuGeometryScale = penTypeMenu->rw.has_value()
				? penTypeMenu->rw->val / 4.0 : 1.0;
			if (!isfinite(menuGeometryScale)
				|| menuGeometryScale <= 0.000001)
				menuGeometryScale = 1.0;
			state.spec.SetFrameDiffuseMaskGeometryScale(
				1.0 / menuGeometryScale);
			state.spec.Shape(barDeviceContext, *penTypeMenu,
				penTypeMenu->Inherit(TopLeft, *panel), &state.current, false);
			state.spec.SetFrameDiffuseMaskGeometryScale(1.0);

			auto freeLineRow = state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine];
			auto freeLineWord = state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_PenTypeMenuFreeLine];
			auto menuCheck = state.svgMap[
				BarUISetSvgEnum::DrawAttributeBar_PenTypeMenuCheck];
			double freeLineScale = max(0.0, static_cast<double>(
				state.drawAttributePenTypeFreeLinePressScale.val));
			if (!isfinite(freeLineScale) || freeLineScale <= 0.0)
				freeLineScale = 1.0;
			D2D1_MATRIX_3X2_F menuTransform;
			barDeviceContext->GetTransform(&menuTransform);
			if (abs(freeLineScale - 1.0) > 0.000001)
			{
				BarUiInheritClass rowInherit =
					freeLineRow->Inherit(TopLeft, *panel);
				FLOAT centerX = static_cast<FLOAT>((rowInherit.x
					+ freeLineRow->w.val / 2.0) * frameZoom);
				FLOAT centerY = static_cast<FLOAT>((rowInherit.y
					+ freeLineRow->h.val / 2.0) * frameZoom);
				barDeviceContext->SetTransform(
					D2D1::Matrix3x2F::Scale(
						static_cast<FLOAT>(freeLineScale),
						static_cast<FLOAT>(freeLineScale),
						D2D1::Point2F(centerX, centerY))
					* menuTransform);
			}
			state.spec.Shape(barDeviceContext, *freeLineRow,
				freeLineRow->Inherit(TopLeft, *panel));
			state.spec.Svg(barDeviceContext, *menuCheck,
				menuCheck->Inherit(TopLeft, *panel));
			state.spec.Word(barDeviceContext, *freeLineWord,
				freeLineWord->Inherit(TopLeft, *panel),
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_TEXT_ALIGNMENT_LEADING);
			barDeviceContext->SetTransform(menuTransform);

			auto annotationLabel = state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel];
			state.spec.Word(barDeviceContext, *annotationLabel,
				annotationLabel->Inherit(TopLeft, *panel),
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_TEXT_ALIGNMENT_LEADING);
			auto annotationInfo = state.svgMap[
				BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo];
			state.spec.Svg(barDeviceContext, *annotationInfo,
				annotationInfo->Inherit(TopLeft, *panel));

			DrawThicknessPopup(
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText,
				BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit,
				BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge,
				state.drawAttributeOverflowClosePressScale);
			// 菜单内 [?] 的帮助 Tooltip 始终位于其他绘制属性浮层之上。
			DrawThicknessPopup(
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText,
				BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
				BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose,
				BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit,
				state.drawAttributeAnnotationClosePressScale);
			};

			// 静止保持提示：圆环在左、文字在右；锁定后圆环淡出，文字可保留并变白。
				auto holdLockLabel = state.wordMap[
					BarUISetWordEnum::DrawAttributeBar_ThicknessHoldLockLabel];
				double holdHintOpacity = clamp(
					static_cast<double>(holdLockLabel->pct.val), 0.0, 1.0);
				// 圆环复用整组显隐；额外因子只表达锁定后的圆环淡出。
				double holdRingOpacity = holdHintOpacity * clamp(
					static_cast<double>(
						state.drawAttributeThicknessHoldRingLockOpacity.val), 0.0, 1.0);
				if (holdHintOpacity > 0.000001
					|| holdRingOpacity > 0.000001)
				{
					FLOAT holdUiZoom = static_cast<FLOAT>(frameZoom);
					BarUiInheritClass holdLabelInherit =
						holdLockLabel->Inherit(TopLeft, *panel);
					double holdLabelHeight = holdLockLabel->h.val;
					double holdContentScale = max(0.0, static_cast<double>(
						state.drawAttributeThicknessHoldGroupScale.val));
					double holdRingSize = holdLabelHeight
						* BarThicknessHoldRingSizeScale * holdContentScale;
					double holdRingGap = BarThicknessHoldRingTextGap
						* (holdLabelHeight
							/ max(1.0, BarThicknessTooltipBadgeHeight));
					// 环在文字左侧，直径为文字行高的 3/5，垂直居中。
					double holdRingCenterX =
						(holdLabelInherit.x - holdRingGap
							- holdRingSize / 2.0)
						* holdUiZoom;
					double holdRingCenterY =
						(holdLabelInherit.y + holdLabelHeight / 2.0) * holdUiZoom;
					if (holdRingOpacity > 0.000001)
					{
						float holdProgress = clamp(
							static_cast<float>(
								state.barState.drawAttributeBar
									.thicknessSliderHoldProgress),
							0.0f, 1.0f);
						float ringStroke = max(1.0f,
							static_cast<float>(2.0 * holdUiZoom
								* (holdRingSize
									/ max(1.0, BarThicknessTooltipBadgeHeight))));
						float ringRadius = max(ringStroke,
							static_cast<float>(holdRingSize * holdUiZoom / 2.0)
								- ringStroke);
						// 圆环始终用灰色进度，锁定时直接淡出，不变白。
						COLORREF holdRingTrackColor = MixBarUiColor(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							GetThemeColor(BarThemeColorEnum::Surface), 0.70);
						COLORREF holdRingFillColor = MixBarUiColor(
							GetThemeColor(BarThemeColorEnum::TextPrimary),
							GetThemeColor(BarThemeColorEnum::Surface), 0.35);
						state.spec.DrawProgressRing(
							barDeviceContext,
							D2D1::Point2F(
								static_cast<FLOAT>(holdRingCenterX),
								static_cast<FLOAT>(holdRingCenterY)),
							ringRadius, ringStroke, holdProgress,
							holdRingTrackColor, holdRingFillColor,
							static_cast<FLOAT>(holdRingOpacity * 0.45),
							static_cast<FLOAT>(holdRingOpacity));
					}
					if (holdHintOpacity > 0.000001)
					{
						state.spec.Word(barDeviceContext, *holdLockLabel,
							holdLabelInherit,
							DWRITE_FONT_WEIGHT_NORMAL,
							DWRITE_TEXT_ALIGNMENT_LEADING);
					}
				}

			DrawPenTypeOverlay();
			state.spec.SetFrameDiffuseMaskGeometryScale(1.0);
			}

			// 简易颜色选择器作为同窗顶层内容绘制，面板静止时不请求 sustain 帧。
			{
				auto pickerPanel = state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel];
				double pickerOpacity = clamp(
					static_cast<double>(pickerPanel->pct.val)
						/ BarDrawAttributeSurfaceOpacity, 0.0, 1.0);
				double pickerGeometryScale =
					pickerPanel->w.val / BarColorPickerPanelWidth;
				if (pickerOpacity > 0.000001)
				{
					// 点光 diffuse mask 以完整面板几何归一，避免回弹缩放时边缘光变弱。
					double pickerMaskScale = max(0.01,
						pickerGeometryScale);
					state.spec.SetFrameDiffuseMaskGeometryScale(
						1.0 / pickerMaskScale);
					state.spec.Shape(barDeviceContext, *pickerPanel,
						BarUiInheritClass(pickerPanel->inhX, pickerPanel->inhY),
						&state.current, false);

					auto toneHit = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle];
					auto toneSun = state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneSun];
					auto toneMoon = state.svgMap[
						BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneMoon];
					double tonePressScale = clamp(static_cast<double>(
						state.drawAttributeColorPickerTonePressScale.val), 0.0, 1.0);
					if (!isfinite(tonePressScale) || tonePressScale <= 0.0)
						tonePressScale = 1.0;
					D2D1_MATRIX_3X2_F pickerButtonTransform;
					barDeviceContext->GetTransform(&pickerButtonTransform);
					bool toneTransformChanged = abs(tonePressScale - 1.0) > 0.000001;
					if (toneTransformChanged)
					{
						FLOAT centerX = static_cast<FLOAT>(
							(toneHit->inhX + toneHit->w.val / 2.0) * frameZoom);
						FLOAT centerY = static_cast<FLOAT>(
							(toneHit->inhY + toneHit->h.val / 2.0) * frameZoom);
						barDeviceContext->SetTransform(
							D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(tonePressScale),
								static_cast<FLOAT>(tonePressScale),
								D2D1::Point2F(centerX, centerY))
							* pickerButtonTransform);
					}
					state.spec.Shape(barDeviceContext, *toneHit,
						BarUiInheritClass(toneHit->inhX, toneHit->inhY));
					// 色系按钮显示当前色系图标：亮=太阳，暗=月亮。
					state.spec.Svg(barDeviceContext, *toneSun,
						BarUiInheritClass(toneSun->inhX, toneSun->inhY));
					state.spec.Svg(barDeviceContext, *toneMoon,
						BarUiInheritClass(toneMoon->inhX, toneMoon->inhY));
					if (toneTransformChanged)
						barDeviceContext->SetTransform(pickerButtonTransform);

					auto palette = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette];
					FLOAT uiZoom = static_cast<FLOAT>(frameZoom);
					D2D1_RECT_F paletteRect = D2D1::RectF(
						static_cast<FLOAT>(palette->inhX * uiZoom),
						static_cast<FLOAT>(palette->inhY * uiZoom),
						static_cast<FLOAT>((palette->inhX + palette->w.val) * uiZoom),
						static_cast<FLOAT>((palette->inhY + palette->h.val) * uiZoom));
					D2D1_ROUNDED_RECT roundedPalette{
						paletteRect,
						static_cast<FLOAT>(palette->rw->val * uiZoom),
						static_cast<FLOAT>(palette->rh->val * uiZoom) };
					auto hueBrush = state.spec.GetColorPickerHueGradientBrush(
						barDeviceContext,
						D2D1::Point2F(paletteRect.left, paletteRect.top),
						D2D1::Point2F(paletteRect.right, paletteRect.top));
					if (hueBrush)
					{
						hueBrush->SetOpacity(static_cast<FLOAT>(pickerOpacity));
						barDeviceContext->FillRoundedRectangle(
							&roundedPalette, hueBrush);
						double darkMix = clamp(static_cast<double>(
							state.drawAttributeColorPickerToneMix.val), 0.0, 1.0);
						bool openBelowSwatch =
							state.barState.widgetPosition.primaryBar;
						D2D1_POINT_2F nearPoint = D2D1::Point2F(
							paletteRect.left,
							openBelowSwatch ? paletteRect.top : paletteRect.bottom);
						D2D1_POINT_2F farPoint = D2D1::Point2F(
							paletteRect.left,
							openBelowSwatch ? paletteRect.bottom : paletteRect.top);
						auto lightBrush = state.spec.GetColorPickerToneGradientBrush(
							barDeviceContext, false,
							farPoint, nearPoint,
							static_cast<FLOAT>(pickerOpacity * (1.0 - darkMix)));
						if (lightBrush) barDeviceContext->FillRoundedRectangle(
							&roundedPalette, lightBrush);
						auto darkBrush = state.spec.GetColorPickerToneGradientBrush(
							barDeviceContext, true,
							nearPoint, farPoint,
							static_cast<FLOAT>(pickerOpacity * darkMix));
						if (darkBrush) barDeviceContext->FillRoundedRectangle(
							&roundedPalette, darkBrush);
					}

					if (state.barState.drawAttributeBar.colorPickerMarkerVisible)
					{
						double markerX = clamp(static_cast<double>(
							state.barState.drawAttributeBar.colorPickerMarkerX), 0.0, 1.0);
						double markerY = clamp(static_cast<double>(
							state.barState.drawAttributeBar.colorPickerMarkerY), 0.0, 1.0);
						D2D1_POINT_2F markerCenter = D2D1::Point2F(
							static_cast<FLOAT>(paletteRect.left
								+ markerX * (paletteRect.right - paletteRect.left)),
							static_cast<FLOAT>(paletteRect.top
								+ markerY * (paletteRect.bottom - paletteRect.top)));
						D2D1_ELLIPSE outer = D2D1::Ellipse(markerCenter,
							static_cast<FLOAT>(5.5 * pickerGeometryScale) * uiZoom,
							static_cast<FLOAT>(5.5 * pickerGeometryScale) * uiZoom);
						if (auto markerShadow = state.spec.GetFrameSolidColorBrush(
							barDeviceContext, RGB(0, 0, 0),
							pickerOpacity * 0.72))
							barDeviceContext->DrawEllipse(&outer, markerShadow,
								static_cast<FLOAT>(3.0 * pickerGeometryScale) * uiZoom);
						if (auto markerBrush = state.spec.GetFrameSolidColorBrush(
							barDeviceContext, RGB(255, 255, 255), pickerOpacity))
							barDeviceContext->DrawEllipse(&outer, markerBrush,
								static_cast<FLOAT>(1.5 * pickerGeometryScale) * uiZoom);
					}

					auto rgbWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerRgb];
					state.spec.Word(barDeviceContext, *rgbWord,
						BarUiInheritClass(rgbWord->inhX, rgbWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_LEADING);
					auto gWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerG];
					state.spec.Word(barDeviceContext, *gWord,
						BarUiInheritClass(gWord->inhX, gWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_LEADING);
					auto bWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerB];
					state.spec.Word(barDeviceContext, *bWord,
						BarUiInheritClass(bWord->inhX, bWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_LEADING);
					auto opacityWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacity];
					state.spec.Word(barDeviceContext, *opacityWord,
						BarUiInheritClass(opacityWord->inhX, opacityWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_LEADING);
					auto rgbValueWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerRgbValue];
					state.spec.Word(barDeviceContext, *rgbValueWord,
						BarUiInheritClass(rgbValueWord->inhX, rgbValueWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_TRAILING);
					auto gValueWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerGValue];
					state.spec.Word(barDeviceContext, *gValueWord,
						BarUiInheritClass(gValueWord->inhX, gValueWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_TRAILING);
					auto bValueWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerBValue];
					state.spec.Word(barDeviceContext, *bValueWord,
						BarUiInheritClass(bValueWord->inhX, bValueWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_TRAILING);
					auto opacityValueWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacityValue];
					state.spec.Word(barDeviceContext, *opacityValueWord,
						BarUiInheritClass(
							opacityValueWord->inhX, opacityValueWord->inhY),
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_TEXT_ALIGNMENT_TRAILING);

					// 关闭按钮与顶部控件同为 30px 高，X 视觉为命中区的 1/3；按压缩放不影响命中。
					auto closeHit = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit];
					double closePressScale = clamp(static_cast<double>(
						state.drawAttributeColorPickerClosePressScale.val), 0.0, 1.0);
					if (!isfinite(closePressScale) || closePressScale <= 0.0)
						closePressScale = 1.0;
					D2D1_MATRIX_3X2_F closeTransform;
					barDeviceContext->GetTransform(&closeTransform);
					bool closeTransformChanged = abs(closePressScale - 1.0) > 0.000001;
					if (closeTransformChanged)
					{
						FLOAT centerX = static_cast<FLOAT>(
							(closeHit->inhX + closeHit->w.val / 2.0) * frameZoom);
						FLOAT centerY = static_cast<FLOAT>(
							(closeHit->inhY + closeHit->h.val / 2.0) * frameZoom);
						barDeviceContext->SetTransform(
							D2D1::Matrix3x2F::Scale(
								static_cast<FLOAT>(closePressScale),
								static_cast<FLOAT>(closePressScale),
								D2D1::Point2F(centerX, centerY))
							* closeTransform);
					}
					state.spec.Shape(barDeviceContext, *closeHit,
						BarUiInheritClass(closeHit->inhX, closeHit->inhY));
					if (auto closeBrush = state.spec.GetFrameSolidColorBrush(
						barDeviceContext,
						GetThemeColor(BarThemeColorEnum::TextPrimary),
						pickerOpacity))
					{
// 相对此前 2/3 再减半为 1/3；去掉固定内缩与下限，避免小尺寸被夹回原观感。
							FLOAT glyphScale = 1.0F / 3.0F;
							FLOAT centerX = static_cast<FLOAT>(
								(closeHit->inhX + closeHit->w.val / 2.0) * uiZoom);
							FLOAT centerY = static_cast<FLOAT>(
								(closeHit->inhY + closeHit->h.val / 2.0) * uiZoom);
							FLOAT half = max(1.0F * uiZoom, static_cast<FLOAT>(
								closeHit->w.val * uiZoom * glyphScale * 0.5F));
						barDeviceContext->DrawLine(
							D2D1::Point2F(centerX - half, centerY - half),
							D2D1::Point2F(centerX + half, centerY + half),
							closeBrush, max(1.0F, 1.6F * uiZoom));
						barDeviceContext->DrawLine(
							D2D1::Point2F(centerX + half, centerY - half),
							D2D1::Point2F(centerX - half, centerY + half),
							closeBrush, max(1.0F, 1.6F * uiZoom));
					}
					if (closeTransformChanged)
						barDeviceContext->SetTransform(closeTransform);

					auto holdHint = state.shapeMap[
						BarUISetShapeEnum::DrawAttributeBar_ColorPickerHoldHint];
					auto holdWord = state.wordMap[
						BarUISetWordEnum::DrawAttributeBar_ColorPickerHoldLabel];
					double holdRingOpacity = clamp(static_cast<double>(
						state.drawAttributeColorPickerHoldRingOpacity.val)
						* pickerOpacity, 0.0, 1.0);
					if (holdHint->pct.val > 0.000001
						|| holdWord->pct.val > 0.000001
						|| holdRingOpacity > 0.000001)
					{
						if (holdHint->pct.val > 0.000001)
							state.spec.Shape(barDeviceContext, *holdHint,
								BarUiInheritClass(holdHint->inhX, holdHint->inhY));
						if (holdWord->pct.val > 0.000001)
							state.spec.Word(barDeviceContext, *holdWord,
								BarUiInheritClass(holdWord->inhX, holdWord->inhY),
								DWRITE_FONT_WEIGHT_NORMAL,
								DWRITE_TEXT_ALIGNMENT_LEADING);
							if (holdRingOpacity > 0.000001)
							{
								FLOAT ringRadius = static_cast<FLOAT>(
									7.0 * pickerGeometryScale) * uiZoom;
								D2D1_POINT_2F ringCenter = D2D1::Point2F(
									static_cast<FLOAT>((holdHint->inhX
										+ holdHint->w.val
										- 12.0 * pickerGeometryScale) * uiZoom),
									static_cast<FLOAT>((holdHint->inhY
										+ holdHint->h.val / 2.0) * uiZoom));
								float progress = clamp(static_cast<float>(
									state.barState.drawAttributeBar.colorPickerHoldProgress),
									0.0F, 1.0F);
							COLORREF holdRingTrackColor = MixBarUiColor(
								GetThemeColor(BarThemeColorEnum::TextPrimary),
								GetThemeColor(BarThemeColorEnum::Surface), 0.70);
							COLORREF holdRingFillColor = MixBarUiColor(
								GetThemeColor(BarThemeColorEnum::TextPrimary),
								GetThemeColor(BarThemeColorEnum::Surface), 0.35);
							state.spec.DrawProgressRing(
								barDeviceContext, ringCenter, ringRadius,
								static_cast<FLOAT>(2.0 * pickerGeometryScale) * uiZoom,
								progress,
								holdRingTrackColor, holdRingFillColor,
								static_cast<FLOAT>(holdRingOpacity * 0.45),
								static_cast<FLOAT>(holdRingOpacity));
						}
					}
				}

				auto preview = state.shapeMap[
					BarUISetShapeEnum::DrawAttributeBar_ColorPickerPreviewBubble];
				if (preview->pct.val > 0.000001)
				{
					state.spec.Shape(barDeviceContext, *preview,
						BarUiInheritClass(preview->inhX, preview->inhY),
						&state.current, false);
				}
				state.spec.SetFrameDiffuseMaskGeometryScale(1.0);
			}

		// 预览浮窗覆盖全部普通绘制属性内容，Thumb 在最后一层单独补画。
		{
			auto panel = state.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
			auto popupSurface = state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessPreviewPopupSurface];
			auto popupCircle = state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessPreviewPopupCircle];
			auto popupNumber = state.wordMap[
				BarUISetWordEnum::DrawAttributeBar_ThicknessPreviewPopupNumber];
			if (state.drawAttributeThicknessPreviewPopupGeometryValid)
			{
				double popupScale = max(0.000001,
					state.drawAttributeThicknessPreviewPopupScale);
				// 以完整 Surface 几何归一 PointLight mask，回弹期间第三光亮度保持连续。
				state.spec.SetFrameDiffuseMaskGeometryScale(1.0 / popupScale);
				state.spec.Shape(barDeviceContext, *popupSurface,
					popupSurface->Inherit(TopLeft, *panel), &state.current, false);
				state.spec.SetFrameDiffuseMaskGeometryScale(1.0);
				state.spec.Shape(barDeviceContext, *popupCircle,
					popupCircle->Inherit(TopLeft, *panel), &state.current, false);

				D2D1_MATRIX_3X2_F originalTransform;
				barDeviceContext->GetTransform(&originalTransform);
				barDeviceContext->SetTransform(
					D2D1::Matrix3x2F::Scale(
						static_cast<FLOAT>(popupScale),
						static_cast<FLOAT>(popupScale),
						D2D1::Point2F(
							state.drawAttributeThicknessPreviewPopupAnchor.x
								* static_cast<FLOAT>(frameZoom),
							state.drawAttributeThicknessPreviewPopupAnchor.y
								* static_cast<FLOAT>(frameZoom)))
					* originalTransform);
				// 数字始终使用完整字号格式，仅通过整体变换完成展开和位置迁移。
				state.spec.Word(barDeviceContext, *popupNumber,
					BarUiInheritClass(
						state.drawAttributeThicknessPreviewNumberRect.left,
						state.drawAttributeThicknessPreviewNumberRect.top),
					DWRITE_FONT_WEIGHT_BOLD,
					DWRITE_TEXT_ALIGNMENT_CENTER);
				barDeviceContext->SetTransform(originalTransform);
			}

			auto sliderThumb = state.shapeMap[
				BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderThumb];
			double thumbOpacity = clamp(
				static_cast<double>(sliderThumb->pct.val), 0.0, 1.0);
			if (thumbOpacity > 0.000001
				&& sliderThumb->w.val > 0.0 && sliderThumb->h.val > 0.0)
			{
				BarUiInheritClass thumbInherit = sliderThumb->Inherit(
					TopLeft, *panel);
				FLOAT uiZoom = static_cast<FLOAT>(frameZoom);
				double panelAnimationScale = panel->w.val
					/ BarDrawAttributeExpandedWidth;
				FLOAT thumbDiameter = static_cast<FLOAT>(min(
					sliderThumb->w.val, sliderThumb->h.val) * uiZoom);
				FLOAT thumbRadius = thumbDiameter / 2.0F;
				D2D1_POINT_2F thumbCenter = D2D1::Point2F(
					static_cast<FLOAT>((thumbInherit.x
						+ sliderThumb->w.val / 2.0) * uiZoom),
					static_cast<FLOAT>((thumbInherit.y
						+ sliderThumb->h.val / 2.0) * uiZoom));
				auto FillThumbCircle = [&](COLORREF color, FLOAT radius)
					{
						if (radius <= 0.0F) return;
						auto brush = state.spec.GetFrameSolidColorBrush(
							barDeviceContext, color, thumbOpacity);
						if (!brush) return;
						D2D1_ELLIPSE ellipse = D2D1::Ellipse(
							thumbCenter, radius, radius);
						barDeviceContext->FillEllipse(&ellipse, brush);
					};
				COLORREF surfaceColor = panel->fill.has_value()
					? static_cast<COLORREF>(panel->fill.value().val)
					: GetThemeColor(BarThemeColorEnum::Surface);
				COLORREF textColor = GetThemeColor(
					BarThemeColorEnum::TextPrimary);
				COLORREF accentColor = GetThemeColor(
					BarThemeColorEnum::Accent);
				double accentOpacity = clamp(static_cast<double>(
					state.drawAttributeThicknessSliderAccentOpacity.val),
					0.0, 1.0);
				COLORREF centerColor = MixBarUiColor(
					surfaceColor, accentColor, accentOpacity);
				COLORREF outerFillColor = state.barStyle.darkStyle
					? MixBarUiColor(surfaceColor, textColor, 0.20)
					: surfaceColor;
				COLORREF outerFrameColor = MixBarUiColor(
					outerFillColor, textColor,
					state.barStyle.darkStyle ? 0.12 : 0.16);
				FillThumbCircle(outerFrameColor, thumbRadius);
				FillThumbCircle(outerFillColor,
					max(0.0F, thumbRadius - static_cast<FLOAT>(
						panelAnimationScale * uiZoom)));
				FLOAT centerDiameter = static_cast<FLOAT>(
					state.drawAttributeThicknessSliderCenterDiameter.val)
					* static_cast<FLOAT>(panelAnimationScale * max(0.0,
						static_cast<double>(
							state.drawAttributeThicknessSliderThumbScale.val)))
					* uiZoom;
				FillThumbCircle(centerColor,
					min(thumbRadius * 0.70F, centerDiameter / 2.0F));
			}
		}

		// 调试模式持续显示实时 FPS，并把文本范围加入脏区。
		if (BarUiDebugModeEnabled)
		{
			FLOAT tarZoom = static_cast<FLOAT>(frameZoom);
			wstring content = L"开发版本 " + editionDate + L" | 不代表最终品质 | " + state.fps;

			const bool alignToLeft = state.mainBarLayoutSide;
			ComPtr<IDWriteTextFormat> pTextFormat;
			pTextFormat = state.barMedia.formatCache->GetFormat(
				L"HarmonyOS Sans SC",
				12.0F * tarZoom,
				dWriteFontCollection.Get(),
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				L"zh-cn",
				alignToLeft
					? DWRITE_TEXT_ALIGNMENT_LEADING
					: DWRITE_TEXT_ALIGNMENT_TRAILING,
				DWRITE_PARAGRAPH_ALIGNMENT_NEAR // 指定段落顶部对齐
			);

			// 3. 创建画刷
			ID2D1SolidColorBrush* pBrush =
				state.spec.GetFrameSolidColorBrush(
					barDeviceContext, RGB(255, 255, 255), 0.5);

			auto mainButton = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton];
			double tarX = mainButton->inhX;
			double tarY = mainButton->inhY + mainButton->GetH();
			// 主按钮位于右侧时向左排版，避免调试文字越过屏幕边缘。
			double layoutLeft = alignToLeft ? tarX : tarX + mainButton->GetW() - 300.0;
			double layoutRight = alignToLeft ? tarX + 300.0 : tarX + mainButton->GetW();

			// 4. 设定绘制区域
			D2D1_RECT_F layoutRect = D2D1::RectF(
				static_cast<FLOAT>(layoutLeft * tarZoom), static_cast<FLOAT>(tarY * tarZoom),
				static_cast<FLOAT>(layoutRight * tarZoom),
				static_cast<FLOAT>((tarY + 20) * tarZoom));

			RECT tmp = RECT((LONG)(layoutRect.left), (LONG)(layoutRect.top), (LONG)(layoutRect.right), (LONG)(layoutRect.bottom));
			BarRenderingAttribute::UnionRectInPlace(state.current, tmp);

			// 5. 绘制文本
			if (pBrush && pTextFormat) barDeviceContext->DrawTextW(
				content.c_str(),           // text
				(UINT32)content.length(),  // text length
				pTextFormat.Get(),         // format
				layoutRect,                // layout rect
				pBrush,                    // brush
				D2D1_DRAW_TEXT_OPTIONS_NONE
			);
		}

		if (BarUiDebugModeEnabled)
		{
			// 红框只标记本帧即将提交的实际脏区，不改变正常更新区域。
			RECT debugTarget = frameDirty;
			BarRenderingAttribute::UnionRectInPlace(debugTarget, state.current);
			{
				if (debugTarget.left < 0) debugTarget.left = 0;
				if (debugTarget.top < 0) debugTarget.top = 0;
				LONG debugWindowWidth = static_cast<LONG>(state.barWindow.w);
				LONG debugWindowHeight = static_cast<LONG>(state.barWindow.h);
				if (debugTarget.right > debugWindowWidth) debugTarget.right = debugWindowWidth;
				if (debugTarget.bottom > debugWindowHeight) debugTarget.bottom = debugWindowHeight;
			}

			COLORREF frame = RGB(255, 0, 0);
			D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
				static_cast<FLOAT>(debugTarget.left), static_cast<FLOAT>(debugTarget.top),
				static_cast<FLOAT>(debugTarget.right - 1),
				static_cast<FLOAT>(debugTarget.bottom - 1)), 0, 0);

			ID2D1SolidColorBrush* borderBrush =
				state.spec.GetFrameSolidColorBrush(
					barDeviceContext, frame, 1.0);

			if (borderBrush)
				barDeviceContext->DrawRoundedRectangle(
					&roundedRect, borderBrush, 1.0f);
		}

		// Windows 7 Platform Update 要求 GetDC 时 Clip/Layer 栈为空。
		state.spec.PopFrameDirtyClip(barDeviceContext);
		HRESULT getDcHr = E_POINTER;
		BOOL updateLayeredWindowSucceeded = FALSE;
		DWORD updateLayeredWindowError = ERROR_SUCCESS;
		HRESULT releaseDcHr = E_FAIL;
		{
			// 脏区更新
			RECT target = frameDirty;
			{
				// 脏区更新限制
				if (target.left < 0) target.left = 0;
				if (target.top < 0) target.top = 0;
				if (target.right > state.barWindow.w) target.right = state.barWindow.w;
				if (target.bottom > state.barWindow.h) target.bottom = state.barWindow.h;
			}

			// psize 指定窗口本次更新“新内容”宽高
			// pptDst 指定新内容贴到屏幕上的位置（左上角）
			// pptSrc 从源内存 DC 的哪个位置起贴内容

			// 设置窗口位置
			POINT ptDst = { 0, 0 };
			if (barGdiInterop)
			{
				// GetDC 自带必要的 D2D 提交，避免在此之前再做一次重复 Flush。
				HDC hdc = nullptr;
				getDcHr = barGdiInterop->GetDC(
					D2D1_DC_INITIALIZE_MODE_COPY, &hdc);
				if (SUCCEEDED(getDcHr) && hdc)
				{
					ulwi.pptDst = &ptDst;
					ulwi.hdcSrc = hdc;
					ulwi.prcDirty = &target;
					updateLayeredWindowSucceeded =
						UpdateLayeredWindowIndirect(floating_window, &ulwi);
					if (!updateLayeredWindowSucceeded)
						updateLayeredWindowError = GetLastError();
					releaseDcHr = barGdiInterop->ReleaseDC(nullptr);
				}
				else if (SUCCEEDED(getDcHr)) getDcHr = E_POINTER;
			}
		}

		HRESULT endDrawHr = barDeviceContext->EndDraw();
		state.spec.HandleFrameEndDrawResult(endDrawHr);
		const auto presentCompletion = state.presentDecision.CompleteAttempt(
			Inkeys::UI::Bar::BarPresentAttemptResult::Acquired(
				getDcHr,
				updateLayeredWindowSucceeded,
				releaseDcHr,
				endDrawHr,
				state.current),
			epoch.generation, frameDemandGeneration,
			state.presentAttemptFrameSerial);
		if (presentCompletion.IsCommitted())
		{
			state.barPresentFailureLogged = false;
		}
		else
		{
			if (!state.barPresentFailureLogged && IDTLogger)
				IDTLogger->error(
					"[BarUISetClass::Rendering] 提交事务失败，将全脏重试: GetDC=0x{:08X}, ULW={}, ULWError={}, ReleaseDC=0x{:08X}, EndDraw=0x{:08X}",
					static_cast<unsigned int>(getDcHr),
					updateLayeredWindowSucceeded != FALSE,
					static_cast<unsigned long>(updateLayeredWindowError),
					static_cast<unsigned int>(releaseDcHr),
					static_cast<unsigned int>(endDrawHr));
			state.barPresentFailureLogged = true;
		}

		if (presentCompletion.NeedsTargetRecreation())
		{
			// 任一 D2D/GDI 互操作阶段报告设备丢失，都在下一帧重建本地资源。
			state.spec.DiscardDeviceResources();
		}
		state.barMedia.formatCache->Clean();
		if (!presentCompletion.IsCommitted()) return BarRenderLoopStageResult::Continue;

	}
	else
	{
		(void)BarAtomic::wait.WaitAndConsume();
		if (offSignal) return BarRenderLoopStageResult::Stop;
		// 只重置真正 idle 的休眠时间；提交失败退避仍需继续推进待完成动画。
		state.animationClock.Rebase();
	}

	return BarRenderLoopStageResult::Proceed;
}

void BarRenderLoopCoordinator::PaceFrame(
	BarRenderLoopState& state, int frameOrdinal)
{
	if (frameOrdinal == 1)
	{
		IdtWindowsIsVisible.floatingWindow = true;
	}
	// 帧率锁
	{
		Inkeys::UI::Bar::HighPrecisionWait(chrono::duration<double, milli>(chrono::high_resolution_clock::now() - state.reckon).count(), 60.0);

		//double delay = 1000.0 / 60.0 - chrono::duration<double, milli>(chrono::high_resolution_clock::now() - state.reckon).count();
		//if (delay >= 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
	}

	if (BarUiDebugModeEnabled)
	{
		const double averageFps = state.rollingFrameRate.Tick();
		state.fps = averageFps > 0.0
			? format(L"{:.2f} FPS", averageFps)
			: L"-- FPS";
	}
	state.reckon = chrono::high_resolution_clock::now();
}

void BarRenderLoopCoordinator::Run()
{
	auto& barWindow = owner_.barWindow;
	auto& spec = owner_.spec;

	Inkeys::Thread::StatusGuard guard("BarUISetClass::Rendering");

	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;
	}
	SIZE sizeWnd = { static_cast<LONG>(barWindow.w), static_cast<LONG>(barWindow.h) };
	POINT ptSrc = { 0,0 };
	POINT ptDst = { 0,0 };
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	{
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = NULL;
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;
	}

	// 退出期间不再让窗口样式重试阻塞渲染线程收尾。
	while (!offSignal && !(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE,
			GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;
		this_thread::sleep_for(chrono::milliseconds(10));
	}
	if (offSignal) return;
	while (!offSignal && !(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE,
			GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
		this_thread::sleep_for(chrono::milliseconds(10));
	}
	if (offSignal) return;

	{
		auto renderPass = AcquireUi3RenderPass(Ui3RenderPriority::Interactive);
		Ui3RenderDeviceEpoch epoch = GetUi3RenderDeviceEpoch();
		HRESULT hr = spec.EnsureDeviceResources(epoch,
			static_cast<UINT32>(barWindow.w), static_cast<UINT32>(barWindow.h));
		if (FAILED(hr))
		{
			if (IDTLogger) IDTLogger->error(
				"[BarUISetClass::Rendering] 创建 UI3 Bar 设备资源失败, hr=0x{:08X}",
				static_cast<unsigned int>(hr));
			return;
		}
	}

	BarRenderLoopState state(owner_, owner_.mainButtonClickPulseSerial);
	for (int forNum = 1; !offSignal; forNum = 2)
	{
		BarRenderFrameSnapshot frame;
		frame.ordinal = forNum;
		if (WakeAndSnapshot(state, frame) == BarRenderLoopStageResult::Stop) break;
		SubmitTargetsAndLayout(state, frame);
		const bool needRendering = AdvanceAnimationsAndDeriveLayout(state, frame);
		PrepareLightingAndDemand(state, frame, needRendering);
		const auto presentResult = CalculateDirtyAndDrawPresent(state, frame, ulwi);
		if (presentResult == BarRenderLoopStageResult::Stop) break;
		if (presentResult == BarRenderLoopStageResult::Continue) continue;
		PaceFrame(state, forNum);
	}

	return;
}
// 渲染更新：状态更新 + 通知计算并渲染
