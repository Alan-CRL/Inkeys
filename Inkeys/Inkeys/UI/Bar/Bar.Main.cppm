module;

#include "../../../IdtMain.h"

#include "../../../IdtD2DPreparation.h"

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
	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_DrawSelect,
	DrawAttributeBar_DrawSelectGroove,
	DrawAttributeBar_ThicknessSelect,
};
enum class BarUISetSuperellipseEnum : int
{
	MainButton,
};
enum class BarUISetSvgEnum : int
{
	logo1,
	logoInk,

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
};
enum class BarUISetWordEnum : int
{
	BackgroundWarning,

	MainButton,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_ThicknessDisplay,
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
	bool Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_CENTER);
	bool PrepareFrameLighting(chrono::steady_clock::time_point now);

public:
	BarUISetClass* barUISetClass = nullptr;

protected:
	struct FrameGradientBrushCacheClass
	{
		COLORREF color = RGB(0, 0, 0);
		bool interaction = false;
		ComPtr<ID2D1RadialGradientBrush> brush;
	};

	ID2D1RadialGradientBrush* GetFrameGradientBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color, bool interaction);
	bool DrawPointLightFrame(ID2D1DeviceContext* deviceContext, COLORREF color,
		double framePct, FLOAT strokeWidth, const D2D1_ROUNDED_RECT* roundedRect,
		ID2D1Geometry* geometry);

	D2D1_POINT_2F framePrimaryLight = D2D1::Point2F();
	D2D1_POINT_2F frameInteractionLight = D2D1::Point2F();
	FLOAT framePrimaryLightIntensity = 1.0F;
	FLOAT frameInteractionLightIntensity = 0.0F;
	FLOAT frameLightRadius = 0.0F;
	bool frameInteractionLightVisible = false;
	bool frameLightingWasAnimating = false;
	bool frameGradientFailureLogged = false;
	vector<FrameGradientBrushCacheClass> frameGradientBrushCache;
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
	ankerl::unordered_dense::map<BarUISetWordEnum, shared_ptr<BarUiWordClass>> wordMap;

	// 绘制属性按钮同样复用自身背景层，仅单独记录悬停动画阶段。
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeBrushHoverStage = BarButtomHoverStageEnum::None;
	IdtAtomic<BarButtomHoverStageEnum> drawAttributeHighlightHoverStage = BarButtomHoverStageEnum::None;

public:
	// 渲染更新：状态更新 + 通知计算并渲染
	void UpdateRendering(bool updateState = true);
protected:
	// 拖动交互
	double Seek(const ExMessage& msg);
	void RegisterBorderInteractionLight(double clientX, double clientY, bool restartPrimaryPulse);
	std::atomic<unsigned long long> mainButtonClickPulseSerial = 0;

	mutex borderInteractionLightMutex;
	D2D1_POINT_2F borderInteractionLightPoint = D2D1::Point2F();
	chrono::steady_clock::time_point borderInteractionLightUpdatedAt{};
	chrono::steady_clock::time_point borderPrimaryLightPulseStartedAt{};
	bool hasBorderInteractionLight = false;

	friend class BarUIRendering;
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

	void InitializeWindow(BarUISetClass& barUISet);
	void InitializeMedia(BarUISetClass& barUISet);
	void InitializeUI(BarUISetClass& barUISet);
};
