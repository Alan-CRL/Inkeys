module;

#include "../../../IdtMain.h"

#include "../../../IdtD2DPreparation.h"
#include <wrl/client.h>

export module Inkeys.UI.Bar:UI;

import :State;

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

	bool IsSame() { return val == tar; }
	bool SetTar(bool tarT)
	{
		if (tar == tarT) return false;

		tar = tarT;
		return true;
	}
	void Initialization(optional<bool> valT, optional<bool> tarT = nullopt)
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
//// 模态 UI 值
class BarUiValueClass
{
public:
	BarUiValueClass() {}
	BarUiValueClass(double valT, BarUiValueModeEnum modT = BarUiValueModeEnum::Variable) { mod = modT, SetDirect(valT); }

	bool IsSame() { return val == tar; }
	bool SetTar(double tarT)
	{
		if (tar == tarT) return false;

		startV = val;
		progress = 0.0;
		tar = tarT;
		return true;
	}
	void SetDirect(double valueT)
	{
		val = valueT;
		tar = valueT;
		startV = valueT;
		progress = 0.0;
	}
	void Initialization(double valT, BarUiValueModeEnum modT = BarUiValueModeEnum::Variable) { mod = modT, SetDirect(valT); }

public:
	IdtAtomic<BarUiValueModeEnum> mod = BarUiValueModeEnum::Linear;

	IdtAtomic<double> val = 0.0; // 直接值（当前位置）
	IdtAtomic<double> tar = 0.0; // 目标值（目标位置）
	IdtAtomic<double> ary = 1.0; // 变换精度（差值绝对值小于等于精度则认为已经动画完成，则直接赋值等于）

	// 适用于 回弹动效模式
	IdtAtomic<double> spe = 120.0; // 基准速度 px/s（此时用于动画细节的测试，故意调慢速度）
	IdtAtomic<double> startV = 0.0; // 起始位置（用于计算百分比，在界面设被设置时）
	IdtAtomic<double> progress = 0.0; // 当前动画段的线性进度（曲线 x，0->1）
};
//// 颜色 UI 值（忽略透明度）
class BarUiColorClass
{
public:
	BarUiColorClass() {}
	BarUiColorClass(COLORREF valT) { SetDirect(valT); }

	bool IsSame() { return val == tar; }
	bool SetTar(COLORREF tarT)
	{
		if (tar == tarT) return false;

		startColor = val;
		progress = 0.0;
		tar = tarT;
		return true;
	}
	void SetDirect(COLORREF valueT)
	{
		val = valueT;
		tar = valueT;
		startColor = valueT;
		progress = 0.0;
	}
	void Initialization(COLORREF valT) { SetDirect(valT); }

public:
	IdtAtomic<COLORREF> val = RGB(0, 0, 0); // 直接值（当前位置）
	IdtAtomic<COLORREF> tar = RGB(0, 0, 0); // 目标值（目标位置）
	IdtAtomic<COLORREF> startColor = RGB(0, 0, 0); // 起始颜色（用于计算百分比，在界面被设置时）
	IdtAtomic<double> progress = 0.0; // 当前动画段的线性进度（曲线 x，0->1）

	IdtAtomic<double> spe = 0.60; // RGB基准速度 1/s（此时用于动画细节的测试，故意调慢速度）
	// 如果 spe <= 0 则表示直接变化
};
//// 透明度 UI 值
class BarUiPctClass
{
public:
	BarUiPctClass() {}
	BarUiPctClass(double valT) { SetDirect(valT); }

	bool IsSame() { return val == tar; }
	bool SetTar(double tarT)
	{
		if (tar == tarT) return false;

		startV = val;
		progress = 0.0;
		tar = tarT;
		return true;
	}
	void SetDirect(double valueT)
	{
		val = valueT;
		tar = valueT;
		startV = valueT;
		progress = 0.0;
	}
	void Initialization(double valT) { SetDirect(valT); }

public:
	IdtAtomic<double> val = 1.0; // 透明度直接值
	IdtAtomic<double> tar = 1.0; // 颜色目标值
	IdtAtomic<double> startV = 1.0; // 起始透明度（用于计算百分比，在界面被设置时）
	IdtAtomic<double> progress = 0.0; // 当前动画段的线性进度（曲线 x，0->1）

	IdtAtomic<double> spe = 0.60; // 透明度基准速度 1/s（此时用于动画细节的测试，故意调慢速度）
	// 如果 spe <= 0 则表示直接变化
};
//// 文字 UI 值
class BarUiStringClass
{
public:
	BarUiStringClass() {}
	BarUiStringClass(wstring valT)
	{
		unique_lock lockValmt(valmt);
		val = valT;
		lockValmt.unlock();

		unique_lock lockTarmt(tarmt);
		tar = valT;
		lockTarmt.unlock();
	}
	// 拷贝构造函数，只拷贝值，mutex新建
	BarUiStringClass(const BarUiStringClass& other)
		: val(other.val), tar(other.tar), valmt(), tarmt() {}

public:
	wstring GetVal() const
	{
		shared_lock lock(valmt);
		return val;
	}
	void SetVal(const wstring& v)
	{
		unique_lock lock(valmt);
		val = v;
	}
	wstring GetTar() const
	{
		shared_lock lock(tarmt);
		return tar;
	}
	void SetTar(const wstring& t)
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
	bool IsSame()
	{
		shared_lock lock_val(valmt);
		shared_lock lock_tar(tarmt);
		return val == tar;
	}
	void Initialization(wstring valT)
	{
		unique_lock lockValmt(valmt);
		val = valT;
		lockValmt.unlock();

		unique_lock lockTarmt(tarmt);
		tar = valT;
		lockTarmt.unlock();
	}

public:
	friend bool operator==(const BarUiStringClass& lhs, const BarUiStringClass& rhs)
	{
		if (&lhs == &rhs) return true; // 同一个对象

		// 按指针大小先锁valmt，再锁tarmt，避免死锁
		const BarUiStringClass* first = &lhs;
		const BarUiStringClass* second = &rhs;
		if (first > second) swap(first, second);

		// 为了防止死锁，分别锁两个对象的valmt和tarmt
		// 总是先valmt，再tarmt（重要！避免死锁）
		shared_lock vlock1(first->valmt);
		shared_lock tlock1(first->tarmt);

		shared_lock vlock2(second->valmt);
		shared_lock tlock2(second->tarmt);

		return lhs.val == rhs.val && lhs.tar == rhs.tar;
	}
	friend bool operator!=(const BarUiStringClass& lhs, const BarUiStringClass& rhs)
	{
		return !(lhs == rhs);
	}

protected:
	mutable shared_mutex valmt;
	mutable shared_mutex tarmt;
	wstring val = L"";
	wstring tar = L"";
};

// 前向声明
class BarUiShapeClass;
class BarUiSuperellipseClass;
class BarUiSVGClass;
class BarUiWordClass;
// 前向声明

/// 继承
//// 位置继承
enum class BarUiInheritEnum
{
	// 相对内部继承

	TopLeft = 0, // 左上继承
	Top = 1, // 上中继承
	Left = 4, // 左中继承
	Center = 5, // 居中继承
	Right = 6, // 右中继承

	// 相对外部继承

	ToTop = 11, // 父下中，子上中
	ToRight = 13, // 父左中，子右中
	ToLeft = 15, // 父右中，子左中
	ToBottom = 17, // 父上中，子下中
};
class BarUiInheritClass
{
public:
	BarUiInheritClass(double xT, double yT);
	BarUiInheritClass(BarUiInheritEnum typeT, double xO, double yO, double wO, double hO, double xT, double yT, double wT, double hT);

public:
	BarUiInheritEnum type = BarUiInheritEnum::Center;
	double x = 0.0; // 继承坐标左上角 x 坐标
	double y = 0.0; // 继承坐标左上角 y 坐标
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
	BarUiInheritClass Inherit() { return UpInh(BarUiInheritClass(x.val - w.tar / 2.0, y.val - h.tar / 2.0)); }
	BarUiInheritClass Inherit(BarUiInheritEnum typeT, const BarUiInnheritBaseClass& obj) { return UpInh(BarUiInheritClass(typeT, x.val, y.val, w.val, h.val, obj.inhX, obj.inhY, obj.w.val, obj.h.val)); }

public:
	// 继承值 -> 也就是实际绘制的位置
	IdtAtomic<double> inhX = 0.0; // 控件左上角 x 坐标
	IdtAtomic<double> inhY = 0.0; // 控件左上角 y 坐标
	const BarUiInheritClass& UpInh(const BarUiInheritClass& inh)
	{
		inhX = inh.x, inhY = inh.y;
		return inh;
	}

	IdtAtomic<bool> forceReplace = true;

public:
	// 模态方位查询
	double GetX() { return x.tar; };
	double GetY() { return y.tar; };
	double GetW() { return w.tar; };
	double GetH() { return h.tar; };
	struct OrientationStruct { double x, y; };
	OrientationStruct GetCenterX() { return { x.tar,y.tar }; }
	OrientationStruct GetLeft() { return { x.tar - w.tar / 2.0, y.tar }; }
	OrientationStruct GetRight() { return { x.tar + w.tar / 2.0, y.tar }; }
};

/// 控件
//// 单个形状控件
class BarUiShapeClass : public BarUiInnheritBaseClass
{
public:
	BarUiShapeClass() {}
	BarUiShapeClass(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

	void Initialization(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

public:
	bool IsClick(int mx, int my, double zoom, double epsilon = 1e-6);

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

	// 透明度
	optional<BarUiPctClass> framePct; // 控件边框透明度
};
//// 单个超椭圆控件
class BarUiSuperellipseClass : public BarUiInnheritBaseClass
{
public:
	BarUiSuperellipseClass() {}
	BarUiSuperellipseClass(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

	void Initialization(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

public:
	bool IsClick(int mx, int my, double zoom, double epsilon = 1e-6);

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 模态

	optional<BarUiValueClass> n;
	optional<BarUiValueClass> ft; // 控件边框宽度

	// 颜色

	optional<BarUiColorClass> fill; // 控件填充颜色
	optional<BarUiColorClass> frame; // 控件边框颜色

	// 透明度
	optional<BarUiPctClass> framePct; // 控件边框透明度
};
//// 单个 SVG 控件
class BarUiSVGClass : public BarUiInnheritBaseClass
{
public:
	BarUiSVGClass() {}
	BarUiSVGClass(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

	void Initialization(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

	void InitializationFromString(wstring valT);
	void InitializationFromResource(const wstring& resType, const wstring& resName);
	void SetTarFromString(wstring valT);
	void SetTarFromResource(const wstring& resType, const wstring& resName);

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 颜色

	optional<BarUiColorClass> color1; // 控件强调颜色1（忽略透明度)
	optional<BarUiColorClass> color2; // 控件强调颜色2（忽略透明度)
	// 替换标志为
	// color1 -> rgba(10,0,7,0)
	// color2 -> rgba(9,0,2,0)

public:
	// SVG 内容
	BarUiStringClass svg;
	Microsoft::WRL::ComPtr<ID2D1Bitmap> cacheBitmap;

	double cW = 0.0, cH = 0.0; // 缓存宽度、高度
	COLORREF cColor1 = RGB(0, 0, 0), cColor2 = RGB(0, 0, 0);
	bool CacheBitmap(ID2D1DeviceContext* deviceContext, double tarW, double tarH);

public:
	bool SetWH(optional<double> wT, optional<double> hT);
protected:
	pair<double, double> CalcWH();

public:
	double rW; // 实际宽度
	double rH; // 实际高度
};
//// 单个文字控件
class BarUiWordClass : public BarUiInnheritBaseClass
{
public:
	BarUiWordClass() {}
	BarUiWordClass(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT = RGB(0, 0, 0), BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

	void Initialization(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT = RGB(0, 0, 0), BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 内容
	BarUiStringClass content;

	// 字号
	BarUiValueClass size;

	// 颜色
	BarUiColorClass color;
};

/*
动效实现备忘：

当前现状：
1. BarUiValueClass 已有 val/tar/mod/ary/spe/startV，可表达“当前值、目标值、动画类型、精度、速度、动画段起点”。
2. Bar.Main.cpp 的 ChangeValue/ChangeColor/ChangePct 已接入匀速逐帧推进；Linear/Variable 暂时同为线性曲线。
3. MainBar、DrawAttributeBar 的部分 w/h 已设置为 Variable，framePct 已加入动效同步；后续还需要按实际视觉效果细化 x、pct、framePct 等模式。

建议模型：
1. 目标变化时通过 SetTar 记录动画段起点，而不是每帧重置：
   value.SetTar(newTar);
   SetTar 内部只有在 newTar != oldTar 时才会 startV = val、progress = 0.0，并更新 tar。
   因为计算 UI 阶段会每帧重复写相同目标，所以重复 SetTar 同一个目标不应重启动画。

2. 额外状态建议优先增加 progress，而不是固定 duration：
   progress 表示曲线横轴 x，始终按真实时间线性从 0 -> 1。
   duration 可由距离和当前 spe 推导：duration = abs(tar - startV) / spe。
   如果 spe 动画中途变化，则每帧用当前 spe 推进：
       progress += dt * spe / abs(tar - startV);
   这样不会让 val 跳变，只会从下一帧开始加速或减速。

3. 每帧推进：
   double x = clamp(progress, 0.0, 1.0);
   double y = Curve(x);
   value.val = value.startV + (value.tar - value.startV) * y;

   Linear:   Curve(x) = x。
   Variable: 后续可使用 EaseOutBack 这类回弹曲线，y 允许超过 1 后回到 1。

4. 动画完成：
   当 progress >= 1.0，或 abs(value.tar - value.val) <= ary 时，收尾：
       value.val = value.tar;
       value.startV = value.tar;
       value.progress = 0.0;

注意：
1. 回弹曲线下 val 可能非单调，不能从 val 反推 progress；progress/startV/tar 必须作为动画状态保存。
2. 若之后希望打断时速度也完全连续，需要改成带 velocity 的弹簧积分模型；当前备忘先按曲线模型实现。
*/
