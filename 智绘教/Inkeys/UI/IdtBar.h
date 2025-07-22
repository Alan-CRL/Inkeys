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
	Linear = 1, // 线性
	Variable = 2 // 回弹动效
};

// 状态 UI 值
class BarUiStateClass
{
public:
	BarUiStateClass() {}
	BarUiStateClass(IdtOptional<bool> valT, IdtOptional<bool> tarT = nullopt)
	{
		if (valT.has_value()) val = valT;
		else val = false;

		if (tarT.has_value()) tar = tarT;
		else tar = val;
	}

public:
	IdtAtomic<bool> val = false;
	IdtAtomic<bool> tar = false;
};
// 模态 UI 值
class BarUiValueClass
{
public:
	BarUiValueClass() {}

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
	static BarUiValueClass BarUiValueClassModeLinear(double valT, double aryT, double speT)
	{
		BarUiValueClass obj;
		{
			obj.mod = BarUiValueModeEnum::Linear;

			obj.val = obj.tar = valT;
			obj.ary = aryT;

			obj.spe = speT;
			obj.startV = valT;
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
	IdtAtomic<BarUiValueModeEnum> mod = BarUiValueModeEnum::Once;

	IdtAtomic<double> val = 0.0; // 直接值（当前位置）
	IdtAtomic<double> tar = 0.0; // 目标值（目标位置）
	IdtAtomic<double> ary = 0.0; // 变换精度（差值绝对值小于精度则认为已经动画完成，则直接赋值等于）

	// 适用于 回弹动效模式
	IdtAtomic<double> spe = 0.0; // 基准速度 px/s
	IdtAtomic<double> startV = 0.0; // 起始位置（用于计算百分比，在界面设被设置时）
};
// 颜色 UI 值
class BarUiColorClass
{
public:
	BarUiColorClass() {}

public:
	IdtAtomic<COLORREF> val = RGBA(0, 0, 0, 0); // 直接值（当前位置）
	IdtAtomic<COLORREF> tar = RGBA(0, 0, 0, 0); // 目标值（目标位置）

	IdtAtomic<double> spe = 0.0; // 基准速度 1/s
	IdtAtomic<double> pctSpe = 0.0; // 基准速度 1/s
};
// 文字 UI 值
class BarUiWordClass
{
public:
	BarUiWordClass() {}

public:
	string GetVal() const
	{
		shared_lock lock(valmt);
		return val;
	}
	void SetVal(const string& v)
	{
		unique_lock lock(valmt);
		val = v;
	}
	string GetTar() const
	{
		shared_lock lock(tarmt);
		return tar;
	}
	void SetTar(const string& t)
	{
		unique_lock lock(tarmt);
		tar = t;
	}

	void ApplyTar()
	{
		unique_lock lock_val(valmt);
		shared_lock lock_tar(tarmt); // 可读锁，防止tar被其它线程突然写掉
		val = tar;
	}

public:
	friend bool operator==(const BarUiWordClass& lhs, const BarUiWordClass& rhs)
	{
		if (&lhs == &rhs) return true; // 同一个对象

		// 按指针大小先锁valmt，再锁tarmt，避免死锁
		const BarUiWordClass* first = &lhs;
		const BarUiWordClass* second = &rhs;
		if (first > second) std::swap(first, second);

		// 为了防止死锁，分别锁两个对象的valmt和tarmt
		// 总是先valmt，再tarmt（重要！避免死锁）
		shared_lock vlock1(first->valmt);
		shared_lock vlock2(second->valmt);

		shared_lock tlock1(first->tarmt);
		shared_lock tlock2(second->tarmt);

		return lhs.val == rhs.val && lhs.tar == rhs.tar;
	}
	friend bool operator!=(const BarUiWordClass& lhs, const BarUiWordClass& rhs)
	{
		return !(lhs == rhs);
	}

protected:
	mutable shared_mutex valmt;
	mutable shared_mutex tarmt;
	string val = "";
	string tar = "";
};

// 单个形状控件
class BarUiShapeClass
{
public:
	BarUiShapeClass() {}

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 模态

	IdtOptional<BarUiValueClass> x; // 控件中心 x 坐标
	IdtOptional<BarUiValueClass> y; // 控件中心 y 坐标
	IdtOptional<BarUiValueClass> w; // 控件宽度
	IdtOptional<BarUiValueClass> h; // 控件高度
	IdtOptional<BarUiValueClass> rw; // 控件圆角直径
	IdtOptional<BarUiValueClass> rh; // 控件圆角直径

	// 颜色

	IdtOptional<BarUiColorClass> fill; // 控件填充颜色
	IdtOptional<BarUiColorClass> frame; // 控件圆角直径

	// 继承
};
// 单个 SVG 控件
class BarUiSVGClass
{
public:
	BarUiSVGClass() {}

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 模态

	IdtOptional<BarUiValueClass> x; // 控件中心 x 坐标
	IdtOptional<BarUiValueClass> y; // 控件中心 y 坐标
	IdtOptional<BarUiValueClass> w; // 控件宽度
	IdtOptional<BarUiValueClass> h; // 控件高度

	// 颜色

	IdtOptional<BarUiColorClass> color1; // 控件强调颜色1
	IdtOptional<BarUiColorClass> color2; // 控件强调颜色2

	// SVG

	IdtOptional<BarUiWordClass> svg; // SVG 内容
};

// 具体渲染
class BarUIRendering
{
private:
	BarUIRendering() = delete;
public:
	static bool Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg);
};

// UI 总集
class BarUISetClass
{
private:
	BarUISetClass() = delete;
public:
	static void Rendering();

public:
	inline static BarMediaClass barMedia;

	inline static ankerl::unordered_dense::map<size_t, BarUiValueClass*> valuetMap;
	inline static ankerl::unordered_dense::map<size_t, BarUiSVGClass*> svgMap;
};

// ====================
// 环境

// LOGO配色方案
enum class BarLogaColorSchemeEnum : int
{
	Default = 0, // 深色
	Slate = 1 // 浅色
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