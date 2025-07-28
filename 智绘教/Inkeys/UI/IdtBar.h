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
class BarUiSuperellipseClass;
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
//// 继承基类
class BarUiInnheritBaseClass
{
protected:
	BarUiInnheritBaseClass() = default;

public:
	BarUiValueClass x; // 控件中心 x 坐标
	BarUiValueClass y; // 控件中心 y 坐标
	BarUiValueClass w; // 控件宽度
	BarUiValueClass h; // 控件高度
	BarUiPctClass pct; // 透明度

public:
	BarUiInheritClass Inherit() { return UpInh(BarUiInheritClass(x.val, y.val)); }
	BarUiPctInheritClass InheritPct() { return UpInhPct(BarUiPctInheritClass(pct.val)); }

	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiShapeClass& shape);
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiSuperellipseClass& superellipse);
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiSVGClass& svg);
	BarUiPctInheritClass InheritPct(const BarUiShapeClass& shape);
	BarUiPctInheritClass InheritPct(const BarUiSuperellipseClass& superellipse);
	BarUiPctInheritClass InheritPct(const BarUiSVGClass& svg);

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

/// 控件
//// 单个形状控件
class BarUiShapeClass : public BarUiInnheritBaseClass
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
	bool IsClick(int mx, int my, double epsilon = 1e-6)
	{
		// 保证有效参数
		if (w.val <= 0.0 || h.val <= 0.0 || (rw.has_value() && rw.value().val < 0.0) || (rh.has_value() && rh.value().val < 0.0)) return false;

		// 折算为半径
		double rx = 0.0;
		double ry = 0.0;
		if (rw.has_value()) rx = clamp(rw.value().val / 2.0, 0.0, w.val / 2.0);
		if (rh.has_value()) ry = clamp(rh.value().val / 2.0, 0.0, h.val / 2.0);

		// 矩形区域内才有可能
		if (static_cast<double>(mx) < x.val - epsilon || static_cast<double>(mx) > x.val + w.val + epsilon ||
			static_cast<double>(mx) < y.val - epsilon || static_cast<double>(mx) > y.val + h.val + epsilon)
			return false;

		// “内矩形”范围
		double ix0 = x.val + rx;         // 内部矩形左
		double ix1 = x.val + w.val - rx; // 内部矩形右
		double iy0 = y.val + ry;         // 上
		double iy1 = y.val + h.val - ry; // 下

		// 若点在内矩形，直接返回
		if (static_cast<double>(mx) >= ix0 - epsilon && static_cast<double>(mx) <= ix1 + epsilon &&
			static_cast<double>(mx) >= iy0 - epsilon && static_cast<double>(mx) <= iy1 + epsilon)
			return true;

		// 否则一定在四角矩形外或圆角四象限内，枚举距离最近的圆角中心
		// Clamp到最近的角
		double cx = (static_cast<double>(mx) < ix0) ? ix0 : ((static_cast<double>(mx) > ix1) ? ix1 : static_cast<double>(mx));
		double cy = (static_cast<double>(mx) < iy0) ? iy0 : ((static_cast<double>(mx) > iy1) ? iy1 : static_cast<double>(mx));

		// 对应的圆角中心
		// 只有在圆角四象限判定，否则前面矩形部分已经返回true
		double dx = static_cast<double>(mx) - cx;
		double dy = static_cast<double>(mx) - cy;

		// 椭圆(中心0,0, 半径rx,ry)上的判定
		// (dx/rx)^2 + (dy/ry)^2 <= 1

		if (rx > 0 && ry > 0)
		{
			double ellipseVal = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
			return ellipseVal <= 1.0 + epsilon;
		}
		// 若rx或ry为0，则为直角矩形，已在前面矩形段判断过
		return false;
	}

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 模态
	optional<BarUiValueClass> rw; // 控件圆角半径
	optional<BarUiValueClass> rh; // 控件圆角半径
	optional<BarUiValueClass> ft; // 控件边框宽度

	// 颜色

	optional<BarUiColorClass> fill; // 控件填充颜色
	optional<BarUiColorClass> frame; // 控件边框颜色
};
//// 单个超椭圆控件
class BarUiSuperellipseClass : public BarUiInnheritBaseClass
{
public:
	BarUiSuperellipseClass() {}
	BarUiSuperellipseClass(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type = BarUiValueModeEnum::Variable)
	{
		x.Initialization(xT, type);
		y.Initialization(yT, type);
		w.Initialization(wT, type);
		h.Initialization(hT, type);
		if (nT.has_value()) { n = BarUiValueClass(); n.value().Initialization(nT.value(), type); }
		if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
		if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
		if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
	}

public:
	bool IsClick(int mx, int my, double epsilon = 1e-6)
	{
		// 计算中心
		double cx = inhX + w.val / 2.0;
		double cy = inhY + h.val / 2.0;
		// 半轴
		double a = w.val / 2.0;
		double b = h.val / 2.0;

		// 映射到中心
		double normx = (static_cast<double>(mx) - cx) / a;
		double normy = (static_cast<double>(my) - cy) / b;

		// degenerate
		if (a <= 0 || b <= 0 || (!n.has_value() || n.value().val <= 0)) return false;

		// 超椭圆判定
		double val = pow(abs(normx), n.value().val) + pow(abs(normy), n.value().val);

		// 内/边判定
		if (epsilon > 0.0) return val <= 1.0 + epsilon;
		return val <= 1.0;
	}

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 模态

	optional<BarUiValueClass> n;
	optional<BarUiValueClass> ft; // 控件边框宽度

	// 颜色

	optional<BarUiColorClass> fill; // 控件填充颜色
	optional<BarUiColorClass> frame; // 控件边框颜色
};
//// 单个 SVG 控件
class BarUiSVGClass : public BarUiInnheritBaseClass
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
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 颜色

	optional<BarUiColorClass> color1; // 控件强调颜色1（忽略透明度)
	optional<BarUiColorClass> color2; // 控件强调颜色2（忽略透明度)

	// SVG 内容

	BarUiWordClass svg;

public:
	bool SetWH(optional<double> wT, optional<double> hT, BarUiValueModeEnum type = BarUiValueModeEnum::Variable);
};

// 控件枚举
enum class BarUISetShapeEnum
{
	MainButton
};
enum class BarUISetSuperellipseEnum
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
	static bool Superellipse(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct);
	static bool Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct);

private:
	static string SvgReplaceColor(const string& input, const optional<BarUiColorClass>& color1, const optional<BarUiColorClass>& color2);
};

// UI 总集
class BarUISetClass
{
public:
	void Rendering();
	void Interact();

public:
	BarMediaClass barMedia;

	ankerl::unordered_dense::map<BarUISetShapeEnum, shared_ptr<BarUiShapeClass>> shapeMap;
	ankerl::unordered_dense::map<BarUISetSuperellipseEnum, shared_ptr<BarUiSuperellipseClass>> superellipseMap;
	ankerl::unordered_dense::map<BarUISetSvgEnum, shared_ptr<BarUiSVGClass>> svgMap;

protected:
	double Seek(const ExMessage& msg);
};

// ====================
// 环境

// LOGO配色方案
enum class BarLogaColorSchemeEnum : int
{
	Default = 0, // 深色
	Slate = 1 // 浅色
};

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