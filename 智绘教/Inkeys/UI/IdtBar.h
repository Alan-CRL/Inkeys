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
	BarUiStateClass(optional<bool> valT, optional<bool> tarT = nullopt)
	{
		if (valT.has_value()) val = valT.value();
		else val = false;

		if (tarT.has_value()) tar = tarT.value();
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

	void initialization(double valT, BarUiValueModeEnum modT = BarUiValueModeEnum::Once) { mod = modT, val = tar = valT, startV = valT; }

public:
	IdtAtomic<BarUiValueModeEnum> mod = BarUiValueModeEnum::Once;

	IdtAtomic<double> val = 0.0; // 直接值（当前位置）
	IdtAtomic<double> tar = 0.0; // 目标值（目标位置）
	IdtAtomic<double> ary = 1.0; // 变换精度（差值绝对值小于等于精度则认为已经动画完成，则直接赋值等于）

	// 适用于 回弹动效模式
	IdtAtomic<double> spe = 1.0; // 基准速度 px/s
	IdtAtomic<double> startV = 0.0; // 起始位置（用于计算百分比，在界面设被设置时）
};
// 颜色 UI 值（忽略透明度）
class BarUiColorClass
{
public:
	BarUiColorClass() {}

public:
	IdtAtomic<COLORREF> val = RGB(0, 0, 0); // 直接值（当前位置）
	IdtAtomic<COLORREF> tar = RGB(0, 0, 0); // 目标值（目标位置）

	IdtAtomic<double> spe = 0.0; // RGB基准速度 1/s
};
// 透明度 UI 值
class BarUiPctClass
{
public:
	BarUiPctClass() {}

	void initialization(double valT) { val = tar = valT; }

public:
	IdtAtomic<double> val = 1.0; // 透明度直接值
	IdtAtomic<double> tar = 1.0; // 颜色目标值

	IdtAtomic<double> spe = 1.0; // 透明度基准速度 1/s
};
// 文字 UI 值
class BarUiWordClass
{
public:
	BarUiWordClass() {}

	void initialization(string valT)
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

// 位置继承
enum class BarUiInheritEnum
{
	TopLeft = 0, // 左上继承
	Left = 4, // 左中继承
	Center = 5, // 居中继承

	// 拓展

	LeftToRight = 13, // 父左中，子右中
	RightToLeft = 15, // 父右中，子左中
};
class BarUiInheritClass
{
public:
	BarUiInheritClass(double xT, double yT) { x = xT, y = yT; }
	BarUiInheritClass(BarUiInheritEnum typeT, double w, double h, double xT, double yT, double wT, double hT);

public:
	BarUiInheritEnum type = BarUiInheritEnum::Center;
	IdtAtomic<double> x = 0.0; // 继承坐标原点 x 坐标
	IdtAtomic<double> y = 0.0; // 继承坐标原点 y 坐标
};
// 颜色透明度继承
class BarUiPctInheritClass
{
public:
	BarUiPctInheritClass() {}
	BarUiPctInheritClass(double pctT) { pct = pctT; }

public:
	IdtAtomic<double> pct = 1.0; // 继承的透明度
};

// 单个形状控件
class BarUiShapeClass
{
public:
	BarUiShapeClass() {}

public:
	BarUiInheritClass Inherit() { return UpInh(BarUiInheritClass(x.val, y.val)); } // 继承自己
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiShapeClass& shape) { return UpInh(BarUiInheritClass(typeT, w.val, h.val, shape.x.val, shape.y.val, shape.w.val, shape.h.val)); }
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiSVGClass& svg) { return UpInh(BarUiInheritClass(typeT, w.val, h.val, svg.x.val, svg.y.val, svg.w.val, svg.h.val)); }

	BarUiPctInheritClass InheritPct(const BarUiShapeClass& shape) { return UpInhPct(BarUiPctInheritClass(shape.pct.val)); }
	BarUiPctInheritClass InheritPct(const BarUiSVGClass& svg) { return UpInhPct(BarUiPctInheritClass(svg.pct.val)); }

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

	// 颜色

	optional<BarUiColorClass> fill; // 控件填充颜色
	optional<BarUiColorClass> frame; // 控件圆角直径

	// 透明度
	BarUiPctClass pct;

public:
	// 继承值 -> 也就是实际绘制的位置
	double inhX; // 控件左上角 x 坐标
	double inhY; // 控件左上角 y 坐标
	const BarUiInheritClass& UpInh(const BarUiInheritClass& inh)
	{
		inhX = inh.x, inhY = inh.y;
		return inh;
	}

	double inhPct;
	const BarUiPctInheritClass& UpInhPct(const BarUiPctInheritClass& inh)
	{
		inhPct = inh.pct;
		return inh;
	}
};
// 单个 SVG 控件
class BarUiSVGClass
{
public:
	BarUiSVGClass(double xT, double yT, BarUiValueModeEnum type = BarUiValueModeEnum::Variable)
	{
		x.initialization(xT, type);
		y.initialization(xT, type);
	}

public:
	BarUiInheritClass Inherit() { return UpInh(BarUiInheritClass(x.val, y.val)); } // 继承自己
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiShapeClass& shape) { return UpInh(BarUiInheritClass(typeT, w.val, h.val, shape.x.val, shape.y.val, shape.w.val, shape.h.val)); }
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiSVGClass& svg) { return UpInh(BarUiInheritClass(typeT, w.val, h.val, svg.x.val, svg.y.val, svg.w.val, svg.h.val)); }

	BarUiPctInheritClass InheritPct(const BarUiShapeClass& shape) { return UpInhPct(BarUiPctInheritClass(shape.pct.val)); }
	BarUiPctInheritClass InheritPct(const BarUiSVGClass& svg) { return UpInhPct(BarUiPctInheritClass(svg.pct.val)); }

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
	double inhX; // 控件左上角 x 坐标
	double inhY; // 控件左上角 y 坐标
	const BarUiInheritClass& UpInh(const BarUiInheritClass& inh)
	{
		inhX = inh.x, inhY = inh.y;
		return inh;
	}

	double inhPct;
	const BarUiPctInheritClass& UpInhPct(const BarUiPctInheritClass& inh)
	{
		inhPct = inh.pct;
		return inh;
	}

public:
	bool SetWH(optional<double> wT, optional<double> hT, BarUiValueModeEnum type = BarUiValueModeEnum::Variable)
	{
		int tarW, tarH;

		if (wT.has_value() && hT.has_value()) { tarW = wT.value(), tarH = hT.value(); }
		else
		{
			// 解析SVG
			unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg.GetVal());
			if (!document) return false; // 解析失败

			if (wT.has_value() && !hT.has_value())
			{
				// 高度自动
				tarW = wT.value();
				tarH = document->height() * (wT.value() / document->width());
			}
			else if (!wT.has_value() && hT.has_value())
			{
				// 宽度自动
				tarW = document->width() * (hT.value() / document->height());
				tarH = hT.value();
			}
			else
			{
				// 原尺寸
				tarW = document->width();
				tarH = document->height();
			}
		}

		w.initialization(tarW, type);
		h.initialization(tarW, type);

		return true;
	}
};

// 具体渲染
class BarUIRendering
{
private:
	BarUIRendering() = delete;
public:
	static string SvgReplaceColor(const string& input, const optional<BarUiColorClass>& color1, const optional<BarUiColorClass>& color2);

	static bool Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg, const BarUiInheritClass& inh, const BarUiPctInheritClass& pct);
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

enum class BarUISetSvgEnum : size_t
{
	logo
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