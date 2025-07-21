#pragma once

#include "../../IdtMain.h"

// ====================
// 窗口

// 窗口模态信息
class BarWindowPosClass
{
private:
	BarWindowPosClass() = delete;

public:
	inline static IdtAtomic<unsigned int> x = 0, y = 0;
	inline static IdtAtomic<unsigned int> w = 0, h = 0;
	inline static IdtAtomic<unsigned int> pct = 0; // 透明度
};

// ====================
// 媒体

// 媒体操控类
class BarMediaClass
{
public:
	void LoadExImage();

public:
	enum class BarExImageEnum : int
	{
	};

public:
	IMAGE Image[10];
};

// ====================
// 界面

// 动效类型
enum class BarUiValueModeEnum : int
{
	Once = 0, // 无动画
	Variable = 1 // 回弹动效模式
};

// 状态 UI 值
class BarUiStateClass
{
public:
	BarUiStateClass()
	{
		val = false;
		tar = false;
	}
public:
	IdtAtomic<bool> val;
	IdtAtomic<bool> tar;
};
// 模态 UI 值
class BarUiValueClass
{
public:
	BarUiValueClass()
	{
		mod = BarUiValueModeEnum::Once;

		val = 0.0;
		tar = 0.0;
		ary = 0.0;

		spe = 0.0;
		startV = 0.0;
	}
	static BarUiValueClass BarUiValueClassModeOnce(double valT, double aryT)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeEnum::Once;

			obj.val = obj.tar = valT;
			obj.ary = aryT;
		}
		return obj;
	}
	static BarUiValueClass BarUiValueClassModeVariable(double valT, double aryT, double speT)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeEnum::Variable;

			obj.val = obj.tar = valT;
			obj.ary = aryT;

			obj.spe = speT;
			obj.startV = valT;
		}
		return obj;
	}

public:
	IdtAtomic<BarUiValueModeEnum> mod;

	IdtAtomic<double> val; // 直接值（当前位置）
	IdtAtomic<double> tar; // 目标值（目标位置）
	IdtAtomic<double> ary; // 变换精度（差值绝对值小于精度则认为已经动画完成，则直接赋值等于）

	// 适用于 回弹动效模式
	IdtAtomic<double> spe; // 基准速度 px/s
	IdtAtomic<double> startV; // 起始位置（用于计算百分比，在界面设被设置时）
};
// 颜色 UI 值
class BarUiColorClass
{
public:
	BarUiColorClass()
	{
		val = RGBA(0, 0, 0, 0);
		tar = RGBA(0, 0, 0, 0);

		spe = 0.0;
		pctSpe = 0.0;
	}

public:
	IdtAtomic<COLORREF> val; // 直接值（当前位置）
	IdtAtomic<COLORREF> tar; // 目标值（目标位置）

	IdtAtomic<double> spe; // 基准速度 1/s
	IdtAtomic<double> pctSpe; // 基准速度 1/s
};
// 文字 UI 值
class BarUiWordClass
{
public:
	BarUiWordClass()
	{
		val = "";
		tar = "";
	}

protected:
	mutex mt;
	string val; // 直接值（当前位置）
	string tar; // 目标值（目标位置）
};

// 单个 UI 控件
class BarUiWidgetClass
{
public:
	BarUiWidgetClass() {}

public:
	// 设计思路：每个项单独加入哈希表，提高单个修改时的性能

	// 整体该控件是否显示
	BarUiStateClass enable;

	//

	// 控件左上角 x 坐标
	IdtAtomic<bool> valXEnable;
	BarUiValueClass x;

	// 控件左上角 y 坐标
	IdtAtomic<bool> valYEnable;
	BarUiValueClass y;

	// 控件宽度
	IdtAtomic<bool> valWEnable;
	BarUiValueClass w;

	// 控件高度
	IdtAtomic<bool> valHEnable;
	BarUiValueClass h;

	// 控件圆角直径
	IdtAtomic<bool> valRWEnable;
	BarUiValueClass rw;

	// 控件圆角直径
	IdtAtomic<bool> valRHEnable;
	BarUiValueClass rh;

	//

	// 控件圆角直径
	IdtAtomic<bool> ColFillEnable;
	BarUiColorClass fill;

	// 控件圆角直径
	IdtAtomic<bool> ColFrameEnable;
	BarUiColorClass frame;
};
// 单个 SVG 控件
class BarUiSVGClass
{
public:
	BarUiSVGClass() {}

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	//

	// 控件左上角 x 坐标
	IdtAtomic<bool> valXEnable;
	BarUiValueClass x;

	// 控件左上角 y 坐标
	IdtAtomic<bool> valYEnable;
	BarUiValueClass y;

	// 控件宽度
	IdtAtomic<bool> valWEnable;
	BarUiValueClass w;

	// 控件高度
	IdtAtomic<bool> valHEnable;
	BarUiValueClass h;

	// 控件圆角直径
	IdtAtomic<bool> col1Enable;
	BarUiColorClass color1;

	// 控件圆角直径
	IdtAtomic<bool> col2Enable;
	BarUiColorClass color2;

	//

	// SVG 内容
	BarUiWordClass svg;
};

class BarUISetClass
{
private:
	BarUISetClass() = delete;
public:
	static void Rendering();

public:
	inline static BarMediaClass barMedia;

	inline static ankerl::unordered_dense::map<size_t, BarUiWidgetClass*> widgetMap;
	inline static ankerl::unordered_dense::map<size_t, BarUiWidgetClass*> svgMap;
};

// ====================
// 交互

// 初始化

class BarInitializationClass
{
private:
	BarInitializationClass() = delete;
public:
	static void Initialization();

protected:
	static void InitializeWindow();
	static void InitializeMedia();
	static void InitializeUI();
};