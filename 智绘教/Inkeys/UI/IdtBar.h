#pragma once

#include "../../IdtMain.h"
#include "../../IdtD2DPreparation.h"
#include "IdtBarUI.h"
#include "IdtBarState.h"
#include "IdtBarBottom.h"
#include "IdtBarFormat.h"

// ====================
// 窗口

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
	{
	};

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

// 控件枚举
enum class BarUISetShapeEnum
{
	MainBar,

	DrawAttributeBar,
	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
};
enum class BarUISetSuperellipseEnum
{
	MainButton,

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
};
enum class BarUISetSvgEnum
{
	logo1,

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
enum class BarUISetWordEnum
{
	MainButton,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
};

// 具体渲染
class BarUIRendering
{
public:
	BarUIRendering() {};
	BarUIRendering(BarUISetClass* barUISetClassT) { barUISetClass = barUISetClassT; }

public:
	bool Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, bool clip = false);
	bool Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, bool clip = false);
	bool Svg(ID2D1DeviceContext* deviceContext, const BarUiSVGClass& svg, const BarUiInheritClass& inh);
	bool Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& shape, const BarUiInheritClass& inh);

public:
	BarUISetClass* barUISetClass = nullptr;
};

// UI 总集
class BarUISetClass
{
public:
	BarUISetClass() : spec(this) {};

	void Rendering(); // 渲染
	void Interact(); // 鼠标交互

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

public:
	// 渲染更新：状态更新 + 通知计算并渲染
	void UpdateRendering(bool updateButtom = true);
protected:
	// 拖动交互
	double Seek(const ExMessage& msg);
};
extern BarUISetClass barUISet; // 全局 Bar UI 集合

// ====================
// 环境

// 弃用
// LOGO配色方案
enum class BarLogaColorSchemeEnum : int
{
	Default = 0, // 深色
	Slate = 1, // 浅色
};

// 初始化

class BarInitializationClass
{
private:
	BarInitializationClass() = delete;
public:
	static void Initialization();

protected:
	static void InitializeWindow(BarUISetClass& barUISet);
	static void InitializeMedia(BarUISetClass& barUISet);
	static void InitializeUI(BarUISetClass& barUISet);
};