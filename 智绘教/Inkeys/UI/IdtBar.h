#pragma once

#include "../../IdtMain.h"

// ====================
// 窗口

// 窗口模态信息
class BarWindowPosClass
{
public:
	BarWindowPosClass()
	{
		x = y = 0;
		w = h = 0;
		pct = 0;
	}

public:
	IdtAtomic<unsigned int> x, y;
	IdtAtomic<unsigned int> w, h;
	IdtAtomic<unsigned int> pct; // 透明度
};
extern BarWindowPosClass barWindowPos;

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
enum class BarUiValueModeClass : int
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
		mod = BarUiValueModeClass::Once;

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
			obj.mod = BarUiValueModeClass::Once;

			obj.val = obj.tar = valT;
			obj.ary = aryT;
		}
		return obj;
	}
	static BarUiValueClass BarUiValueClassModeVariable(double valT, double aryT, double speT)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeClass::Variable;

			obj.val = obj.tar = valT;
			obj.ary = aryT;

			obj.spe = speT;
			obj.startV = valT;
		}
		return obj;
	}

public:
	IdtAtomic<BarUiValueModeClass> mod;

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
	IdtAtomic<bool> ValXEnable;
	BarUiValueClass x;

	// 控件左上角 y 坐标
	IdtAtomic<bool> ValYEnable;
	BarUiValueClass y;

	// 控件宽度
	IdtAtomic<bool> ValWEnable;
	BarUiValueClass w;

	// 控件高度
	IdtAtomic<bool> ValHEnable;
	BarUiValueClass h;

	// 控件圆角直径
	IdtAtomic<bool> ValRWEnable;
	BarUiValueClass rw;

	// 控件圆角直径
	IdtAtomic<bool> ValRHEnable;
	BarUiValueClass rh;

	//

	// 控件圆角直径
	IdtAtomic<bool> ColFillEnable;
	BarUiColorClass fill;

	// 控件圆角直径
	IdtAtomic<bool> ColFrameEnable;
	BarUiColorClass frame;
};

class BarUISetClass
{
public:
	void Rendering();

	constexpr size_t MakeKey(int a, int b) noexcept
	{
		assert(0 <= a && a <= 65535); assert(0 <= b && b <= 99);
		return (static_cast<size_t>(a) << 8) | static_cast<size_t>(b);
	}

public:
	BarMediaClass barMedia;

	ankerl::unordered_dense::map<size_t, BarUiWidgetClass> widgetMap;
};

// ====================
// 交互

// 初始化

class BarInitializationClass
{
public:
	void Initialization();

protected:
	void InitializeWindow();
	void InitializeMedia();
	void InitializeUI();

public:
	BarUISetClass barUISet;
};
extern BarInitializationClass barInitialization;