#pragma once

#include "../../IdtMain.h"
#include "../../IdtD2DPreparation.h"

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

/// 单个 UI 值
//// 状态 UI 值
class BarUiStateClass
{
public:
	BarUiStateClass() {}
	BarUiStateClass(optional<bool> valT, optional<bool> tarT = nullopt)
	{
		if (valT.has_value()) val = valT.value();
		else val = false;

		if (tarT.has_value()) tar = tarT.value();
		else tar = val;
	}

	void Initialization(bool valT) { val = tar = valT; }

public:
	IdtAtomic<bool> val = false;
	IdtAtomic<bool> tar = false;
};
//// 模态 UI 值
class BarUiValueClass
{
public:
	BarUiValueClass() {}

	void Initialization(double valT, BarUiValueModeEnum modT = BarUiValueModeEnum::Once) { mod = modT, val = tar = valT, startV = valT; }

public:
	IdtAtomic<BarUiValueModeEnum> mod = BarUiValueModeEnum::Once;

	IdtAtomic<double> val = 0.0; // 直接值（当前位置）
	IdtAtomic<double> tar = 0.0; // 目标值（目标位置）
	IdtAtomic<double> ary = 1.0; // 变换精度（差值绝对值小于等于精度则认为已经动画完成，则直接赋值等于）

	// 适用于 回弹动效模式
	IdtAtomic<double> spe = 1.0; // 基准速度 px/s
	IdtAtomic<double> startV = 0.0; // 起始位置（用于计算百分比，在界面设被设置时）
};
//// 颜色 UI 值（忽略透明度）
class BarUiColorClass
{
public:
	BarUiColorClass() {}

	void Initialization(COLORREF valT) { val = tar = valT; }

public:
	IdtAtomic<COLORREF> val = RGB(0, 0, 0); // 直接值（当前位置）
	IdtAtomic<COLORREF> tar = RGB(0, 0, 0); // 目标值（目标位置）

	IdtAtomic<double> spe = 0.0; // RGB基准速度 1/s
};
//// 透明度 UI 值
class BarUiPctClass
{
public:
	BarUiPctClass() {}

	void Initialization(double valT) { val = tar = valT; }

public:
	IdtAtomic<double> val = 1.0; // 透明度直接值
	IdtAtomic<double> tar = 1.0; // 颜色目标值

	IdtAtomic<double> spe = 1.0; // 透明度基准速度 1/s
};
//// 文字 UI 值
class BarUiWordClass
{
public:
	BarUiWordClass() {}

	void Initialization(string valT)
	{
		unique_lock lockValmt(valmt);
		val = valT;
		lockValmt.unlock();

		unique_lock lockTarmt(tarmt);
		tar = valT;
		lockTarmt.unlock();
	}

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
		if (first > second) swap(first, second);

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

// 前向声明
class BarUiShapeClass;
class BarUiSVGClass;
// 前向声明

/// 继承
//// 位置继承
enum class BarUiInheritEnum
{
	// 相对内部继承

	TopLeft = 0, // 左上继承
	Left = 4, // 左中继承
	Center = 5, // 居中继承

	// 相对外部继承

	ToRight = 13, // 父左中，子右中
	ToLeft = 15, // 父右中，子左中
};
class BarUiInheritClass
{
public:
	BarUiInheritClass(double xT, double yT) { x = xT, y = yT; }
	BarUiInheritClass(BarUiInheritEnum typeT, double w, double h, double xT, double yT, double wT, double hT);

public:
	BarUiInheritEnum type = BarUiInheritEnum::Center;
	IdtAtomic<double> x = 0.0; // 继承坐标左上角 x 坐标
	IdtAtomic<double> y = 0.0; // 继承坐标左上角 y 坐标
};
//// 颜色透明度继承
class BarUiPctInheritClass
{
public:
	BarUiPctInheritClass() {}
	BarUiPctInheritClass(double pctT) { pct = pctT; }

public:
	IdtAtomic<double> pct = 1.0; // 继承的透明度
};

///
//// 单个形状控件
class BarUiShapeClass
{
public:
	BarUiShapeClass() {}
	BarUiShapeClass(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type = BarUiValueModeEnum::Variable)
	{
		x.Initialization(xT, type);
		y.Initialization(yT, type);
		w.Initialization(wT, type);
		h.Initialization(hT, type);
		if (rwT.has_value()) { rw = BarUiValueClass(); rw.value().Initialization(rwT.value(), type); }
		if (rhT.has_value()) { rh = BarUiValueClass(); rh.value().Initialization(rhT.value(), type); }
		if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
		if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
		if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
	}

public:
	BarUiInheritClass Inherit(); // 继承自己
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiShapeClass& shape);
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiSVGClass& svg);

	BarUiPctInheritClass InheritPct(); // 继承自己
	BarUiPctInheritClass InheritPct(const BarUiShapeClass& shape);
	BarUiPctInheritClass InheritPct(const BarUiSVGClass& svg);

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 模态

	BarUiValueClass x; // 控件中心 x 坐标
	BarUiValueClass y; // 控件中心 y 坐标
	BarUiValueClass w; // 控件宽度
	BarUiValueClass h; // 控件高度
	optional<BarUiValueClass> rw; // 控件圆角直径
	optional<BarUiValueClass> rh; // 控件圆角直径
	optional<BarUiValueClass> ft; // 控件边框宽度

	// 颜色

	optional<BarUiColorClass> fill; // 控件填充颜色
	optional<BarUiColorClass> frame; // 控件边框颜色

	// 透明度
	BarUiPctClass pct;

public:
	// 继承值 -> 也就是实际绘制的位置
	double inhX = 0.0; // 控件左上角 x 坐标
	double inhY = 0.0; // 控件左上角 y 坐标
	const BarUiInheritClass& UpInh(const BarUiInheritClass& inh)
	{
		inhX = inh.x, inhY = inh.y;
		return inh;
	}

	double inhPct = 1.0;
	const BarUiPctInheritClass& UpInhPct(const BarUiPctInheritClass& inh)
	{
		inhPct = inh.pct;
		return inh;
	}
};
//// 单个 SVG 控件
class BarUiSVGClass
{
public:
	BarUiSVGClass(double xT, double yT, BarUiValueModeEnum type = BarUiValueModeEnum::Variable)
	{
		x.Initialization(xT, type);
		y.Initialization(yT, type);
	}

	void InitializationFromString(string valT) { svg.Initialization(valT); }
	void InitializationFromResource(const wstring& resType, const wstring& resName);

public:
	BarUiInheritClass Inherit(); // 继承自己
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiShapeClass& shape);
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiSVGClass& svg);

	BarUiPctInheritClass InheritPct(); // 继承自己
	BarUiPctInheritClass InheritPct(const BarUiShapeClass& shape);
	BarUiPctInheritClass InheritPct(const BarUiSVGClass& svg);

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 模态

	BarUiValueClass x; // 控件中心 x 坐标
	BarUiValueClass y; // 控件中心 y 坐标
	BarUiValueClass w; // 控件宽度
	BarUiValueClass h; // 控件高度

	// 颜色

	optional<BarUiColorClass> color1; // 控件强调颜色1（忽略透明度)
	optional<BarUiColorClass> color2; // 控件强调颜色2（忽略透明度)

	// 透明度
	BarUiPctClass pct;

	// SVG 内容

	BarUiWordClass svg;

public:
	// 继承值 -> 也就是实际绘制的位置
	double inhX = 0.0; // 控件左上角 x 坐标
	double inhY = 0.0; // 控件左上角 y 坐标
	const BarUiInheritClass& UpInh(const BarUiInheritClass& inh)
	{
		inhX = inh.x, inhY = inh.y;
		return inh;
	}

	double inhPct = 1.0;
	const BarUiPctInheritClass& UpInhPct(const BarUiPctInheritClass& inh)
	{
		inhPct = inh.pct;
		return inh;
	}

public:
	bool SetWH(optional<double> wT, optional<double> hT, BarUiValueModeEnum type = BarUiValueModeEnum::Variable);
};

// Svg 控件枚举
enum class BarUISetShapeEnum
{
	MainButton
};
enum class BarUISetSvgEnum
{
	logo1
};

// 具体渲染
class BarUIRendering
{
private:
	BarUIRendering() = delete;

public:
	static bool Shape(ID2D1DCRenderTarget* DCRenderTarget, const BarUiShapeClass& shape, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct);
	static bool Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct);

private:
	static string SvgReplaceColor(const string& input, const optional<BarUiColorClass>& color1, const optional<BarUiColorClass>& color2);
};

// UI 总集
class BarUISetClass
{
public:
	void Rendering();

public:
	BarMediaClass barMedia;

	ankerl::unordered_dense::map<BarUISetShapeEnum, shared_ptr<BarUiShapeClass>> shapeMap;
	ankerl::unordered_dense::map<BarUISetSvgEnum, shared_ptr<BarUiSVGClass>> svgMap;
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
	static void InitializeMedia(BarUISetClass& barUISet);
	static void InitializeUI(BarUISetClass& barUISet);
};