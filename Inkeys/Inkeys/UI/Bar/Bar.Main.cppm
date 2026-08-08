module;

#include "../../../IdtMain.h"

#include "../../../IdtD2DPreparation.h"
#include <array>

export module Inkeys.UI.Bar:Main;

import :UI;
import :State;
import :Bottom;
import :Format;
import :RenderingAttribute;

import Inkeys.Conv.Color;
import Inkeys.Helper.Thread;

// ====================
// 动画

IdtAtomic<double> BarUiDefaultDes = 600.0; // 全局默认速度 px/s
IdtAtomic<double> BarUiDefaultOperationDur = 0.4; // 默认操作过程时长 s
IdtAtomic<bool> BarUiAnimationEnabled = true;
IdtAtomic<double> BarUiAnimationSpeedRate = 1.00; // 有效速度倍率；关闭动画时由配置接口切换为即时完成倍率
IdtAtomic<bool> BarUiEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDynamicEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDebugModeEnabled = false;

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
	void LoadExImage();
	void LoadFormat();

public:
	enum class BarExImageEnum : int
	{};

	// 似乎被废弃了，新版较多地使用的是 svg

public:
	IMAGE Image[10];
	unique_ptr<BarFormatCache> formatCache;

protected:
};

// ====================
// 界面

// 前向声明
class BarUISetClass;
// 前向声明

void HighPrecisionWait(double frameTimeSpentMs, double targetFPS);

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

enum class BarBorderLightSourceEnum : int
{
	Primary,
	Cursor,
};
enum class BarBorderPrimaryAnchorEnum : int
{
	MainButton,
	Select,
	Draw,
	Eraser,
	Geometry,
};
enum class BarBorderCursorTrackingStateEnum : int
{
	Dormant,
	Inside,
	Grace,
};

// 具体渲染
class BarUIRendering
{
public:
	BarUIRendering() {};
	BarUIRendering(BarUISetClass* barUISetClassT);

public:
	bool Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Svg(ID2D1DeviceContext* deviceContext, BarUiSVGClass& svg, const BarUiInheritClass& inh);
	bool Png(ID2D1DeviceContext* deviceContext, BarUiPNGClass& png, const BarUiInheritClass& inh);
	bool Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_CENTER);
	D2D1_SIZE_F MeasureText(const wstring& content, double fontSize,
		DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL);
	bool PrepareFrameLighting(double animationDtSeconds);
	void DiscardDeviceResources();
	void PushFrameDirtyClip(
		ID2D1DeviceContext* deviceContext, const D2D1_RECT_F& dirtyRect);
	void PopFrameDirtyClip(ID2D1DeviceContext* deviceContext);
	void HandleFrameEndDrawResult(HRESULT endDrawResult);
	void SetFrameDiffuseMaskGeometryScale(double scale)
	{
		frameDiffuseMaskGeometryScale = scale > 0.0 ? scale : 1.0;
	}

public:
	BarUISetClass* barUISetClass = nullptr;

protected:
	struct FrameGradientBrushCacheClass
	{
		COLORREF color = RGB(0, 0, 0);
		BarBorderLightSourceEnum lightSource = BarBorderLightSourceEnum::Primary;
		ComPtr<ID2D1RadialGradientBrush> brush;
	};
	struct FrameDiffuseMaskCacheClass
	{
		int radiusXQuarter = 0;
		int radiusYQuarter = 0;
		int strokeWidthQuarter = 0;
		int standardDeviationQuarter = 0;
		FLOAT padding = 0.0F;
		FLOAT radiusX = 0.0F;
		FLOAT radiusY = 0.0F;
		D2D1_SIZE_F size{};
		ComPtr<ID2D1Bitmap1> bitmap;
	};
	struct FrameGeometryDiffuseMaskCacheClass
	{
		int widthQuarter = 0;
		int heightQuarter = 0;
		int geometryVariantQuarter = 0;
		int strokeWidthQuarter = 0;
		int standardDeviationQuarter = 0;
		FLOAT padding = 0.0F;
		D2D1_SIZE_F size{};
		ComPtr<ID2D1Bitmap1> bitmap;
	};
	struct SuperellipseGeometryCacheClass
	{
		FLOAT width = 0.0F;
		FLOAT height = 0.0F;
		FLOAT n = 0.0F;
		int segments = 0;
		FLOAT translatedX = 0.0F;
		FLOAT translatedY = 0.0F;
		ComPtr<ID2D1PathGeometry> localGeometry;
		ComPtr<ID2D1TransformedGeometry> translatedGeometry;
	};

	ID2D1RadialGradientBrush* GetFrameGradientBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color, BarBorderLightSourceEnum lightSource);
	ID2D1SolidColorBrush* GetFrameSolidColorBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color, double opacity);
	ID2D1LinearGradientBrush* GetThicknessPreviewGradientBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
		FLOAT leftOpacity);
	ID2D1LinearGradientBrush* GetColorPickerHueGradientBrush(
		ID2D1DeviceContext* deviceContext,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint);
	ID2D1LinearGradientBrush* GetColorPickerToneGradientBrush(
		ID2D1DeviceContext* deviceContext, bool darkTone,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
		FLOAT opacity);
	void DrawProgressRing(ID2D1DeviceContext* deviceContext,
		D2D1_POINT_2F center, FLOAT radius, FLOAT strokeWidth,
		FLOAT progress, COLORREF trackColor, COLORREF progressColor,
		FLOAT trackOpacity, FLOAT progressOpacity);
	ID2D1PathGeometry* GetThicknessPreviewPath(
		const array<D2D1_POINT_2F, 7>& points);
	ID2D1StrokeStyle* GetThicknessPreviewStrokeStyle();
	ID2D1Geometry* GetSuperellipseGeometry(
		FLOAT x, FLOAT y, FLOAT width, FLOAT height, FLOAT n, int segments);
	FrameDiffuseMaskCacheClass* GetRoundedRectDiffuseMask(
		ID2D1DeviceContext* deviceContext,
		const D2D1_ROUNDED_RECT& roundedRect, FLOAT strokeWidth);
	void DrawRoundedRectDiffuseMask(ID2D1DeviceContext* deviceContext,
		const FrameDiffuseMaskCacheClass& mask,
		const D2D1_ROUNDED_RECT& roundedRect,
		ID2D1RadialGradientBrush* brush, FLOAT opacity);
	FrameGeometryDiffuseMaskCacheClass* GetGeometryDiffuseMask(
		ID2D1DeviceContext* deviceContext, ID2D1Geometry* geometry,
		FLOAT strokeWidth, int geometryVariantQuarter);
	void DrawGeometryDiffuseMask(ID2D1DeviceContext* deviceContext,
		const FrameGeometryDiffuseMaskCacheClass& mask,
		const D2D1_RECT_F& geometryBounds,
		ID2D1RadialGradientBrush* brush, FLOAT opacity);
	bool DrawPointLightFrame(ID2D1DeviceContext* deviceContext, COLORREF color,
		BarUiFrameLightColorEnum frameLightColor,
		bool primaryLightEnabled, double cursorLightIntensityScale,
		double baseFramePct, double lightPct, FLOAT strokeWidth,
		const D2D1_ROUNDED_RECT* roundedRect,
		ID2D1Geometry* geometry, int geometryVariantQuarter = 0);

	D2D1_POINT_2F framePrimaryLight = D2D1::Point2F();
	D2D1_POINT_2F framePrimaryLightStart = D2D1::Point2F();
	D2D1_POINT_2F framePrimaryLightTarget = D2D1::Point2F();
	D2D1_POINT_2F frameCursorLight = D2D1::Point2F();
	FLOAT frameCursorLightIntensity = 0.0F;
	FLOAT frameCursorLightIntensityStart = 0.0F;
	FLOAT frameCursorLightIntensityTarget = 0.0F;
	FLOAT frameLightRadius = 0.0F;
	FLOAT frameCursorLightRadius = 0.0F;
	BarBorderPrimaryAnchorEnum framePrimaryLightAnchor = BarBorderPrimaryAnchorEnum::MainButton;
	bool framePrimaryLightAnchorInitialized = false;
	bool framePrimaryLightAnimating = false;
	bool frameCursorLightVisible = false;
	bool frameCursorLightAnimating = false;
	bool frameAnimationStateInitialized = false;
	bool frameLastAnimationEnabled = false;
	bool frameCursorInputAvailable = false;
	bool frameLightingWasAnimating = false;
	bool frameEdgeLightingEnabled = false;
	bool frameGradientFailureLogged = false;
	bool thicknessPreviewGradientFailureLogged = false;
	bool thicknessPreviewGradientUnavailable = false;
	bool thicknessPreviewGradientColorInitialized = false;
	bool thicknessPreviewPathFailureLogged = false;
	bool thicknessPreviewPathUnavailable = false;
	bool thicknessPreviewPathInitialized = false;
	bool colorPickerGradientFailureLogged = false;
	bool colorPickerGradientUnavailable = false;
	bool frameDiffuseEffectFailureLogged = false;
	bool frameDiffuseMaskFailureLogged = false;
	bool frameDiffuseMaskUnavailable = false;
	bool frameDiffuseMaskCreatedThisFrame = false;
	double frameDiffuseMaskGeometryScale = 1.0;
	bool frameDirtyClipActive = false;
	D2D1_RECT_F frameDirtyClipRect{};
	double framePrimaryLightMoveElapsed = 0.0;
	double frameCursorLightFadeElapsed = 0.0;
	double frameDrawingPenColorElapsed = 0.0;
	double frameDrawingModeTransitionElapsed = 0.0;
	double frameDrawingPenColorBlend = 0.0;
	double frameDrawingPenColorBlendStart = 0.0;
	double frameDrawingPenColorBlendTarget = 0.0;
	double frameDrawingLightOpacity = 1.0;
	double frameDrawingLightOpacityStart = 1.0;
	unsigned long long handledBorderCursorLightSerial = 0;
	COLORREF frameDrawingPenColor = RGB(0, 0, 0);
	COLORREF frameDrawingPenColorStart = RGB(0, 0, 0);
	COLORREF frameDrawingPenColorTarget = RGB(0, 0, 0);
	bool frameDrawingUsesPenColor = false;
	bool frameDrawingPenColorInitialized = false;
	bool frameDrawingPenColorAnimating = false;
	bool frameDrawingModeInitialized = false;
	bool frameDrawingModeTransitionAnimating = false;
	vector<FrameGradientBrushCacheClass> frameGradientBrushCache;
	vector<FrameDiffuseMaskCacheClass> frameDiffuseMaskCache;
	vector<FrameGeometryDiffuseMaskCacheClass> frameGeometryDiffuseMaskCache;
	ComPtr<ID2D1SolidColorBrush> frameSolidColorBrush;
	ComPtr<ID2D1LinearGradientBrush> thicknessPreviewGradientBrush;
	ComPtr<ID2D1LinearGradientBrush> colorPickerHueGradientBrush;
	ComPtr<ID2D1LinearGradientBrush> colorPickerLightGradientBrush;
	ComPtr<ID2D1LinearGradientBrush> colorPickerDarkGradientBrush;
	ComPtr<ID2D1PathGeometry> thicknessPreviewPath;
	ComPtr<ID2D1StrokeStyle> thicknessPreviewStrokeStyle;
	SuperellipseGeometryCacheClass superellipseGeometryCache;
	COLORREF thicknessPreviewGradientColor = RGB(0, 0, 0);
	FLOAT thicknessPreviewGradientLeftOpacity = -1.0F;
	array<D2D1_POINT_2F, 7> thicknessPreviewPathPoints{};
	ComPtr<ID2D1DeviceContext> frameMaskDeviceContext;
	ComPtr<ID2D1Effect> frameGaussianBlurEffect;

	friend class BarUISetClass;
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

public:
	BarWindowPosClass barWindow;
	BarMediaClass barMedia;
	BarButtomSetClass barButtomSet;
	BarUIRendering spec;

	BarStateClass barState;
	BarStyleClass barStyle;

	ankerl::unordered_dense::map<BarUISetShapeEnum, shared_ptr<BarUiShapeClass>> shapeMap;
	ankerl::unordered_dense::map<BarUISetSuperellipseEnum, shared_ptr<BarUiSuperellipseClass>> superellipseMap;
	ankerl::unordered_dense::map<BarUISetSvgEnum, shared_ptr<BarUiSVGClass>> svgMap;
	ankerl::unordered_dense::map<BarUISetPngEnum, shared_ptr<BarUiPNGClass>> pngMap;
	ankerl::unordered_dense::map<BarUISetWordEnum, shared_ptr<BarUiWordClass>> wordMap;

	// 绘制属性按钮同样复用自身背景层，仅单独记录悬停动画阶段。
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeBrushHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeHighlightHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributePenTypeExtensionHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributePenTypeFreeLineHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeThicknessFineHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeThicknessMediumHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeThicknessCoarseHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeThicknessAdjustHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeAnnotationCloseHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeOverflowCloseHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeColorPickerToneHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeColorPickerCloseHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> moreCloseHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> geometryStraightLineHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> geometryRectangleHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> geometryThicknessFineHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> geometryThicknessMediumHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> geometryThicknessCoarseHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> geometryCloseHoverStage = BarButtomHoverStageEnum::None;

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
	void RefreshBorderCursorVisibleRegions();
	bool IsBorderCursorLightNearVisibleRegion(POINT screenPoint);
	std::atomic<unsigned long long> mainButtonClickPulseSerial = 0;

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
	friend LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
// 全局 Bar UI 集合
export extern BarUISetClass barUISet;

// ====================
// 环境

// 弃用
/*
// LOGO配色方案
enum class BarLogoColorSchemeEnum : int
{
	Default = 0, // 深色
	Slate = 1, // 浅色
};*/

// 初始化

namespace Inkeys::UI::Bar
{
	export void Initialization();
	export void SetAnimationOptions(bool enable, double speedRate);
	export void SetEdgeLightingOptions(bool enable, bool dynamic);
	export void SetDebugMode(bool enable);
	export void NotifyCanvasDrawingStarted();
	export void NotifyCanvasDrawingEnded();
	export bool TryQueueColorPickerKeyboardInput(BYTE vkCode, bool keyDown);

	void InitializeWindow(BarUISetClass& barUISet);
	void InitializeMedia(BarUISetClass& barUISet);
	void InitializeUI(BarUISetClass& barUISet);
};
