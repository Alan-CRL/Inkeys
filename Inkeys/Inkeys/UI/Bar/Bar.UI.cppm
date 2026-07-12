module;

#include "../../../IdtMain.h"

#include "../../../IdtD2DPreparation.h"
#include <wrl/client.h>

export module Inkeys.UI.Bar:UI;

import :State;

// 动画全局默认参数在 Bar.Main.cppm 中统一定义。
extern IdtAtomic<double> BarUiDefaultDes;
extern IdtAtomic<double> BarUiDefaultOperationDur;
extern IdtAtomic<double> BarUiAnimationSpeedRate;

// 动效类型
enum class BarUiValueModeEnum : int
{
	Once = 0, // 无动画
	Linear = 1, // 线性
	Variable = 2 // 回弹动效
};

// 动画曲线只负责将线性时间进度映射为数值进度；后续非线性在此扩充。
enum class BarUiCurveEnum : int
{
	Linear = 0,
};

// 一组关联动画共用的线性时间轴。中途修改目标时读取剩余时长，不延后原完成时刻。
class BarUiTimelineClass
{
public:
	void Restart(double durationT)
	{
		duration = isfinite(durationT) && durationT > 0.0 ? durationT : 0.0;
		progress = duration > 0.0 ? 0.0 : 1.0;
	}
	void Advance(double dt, double speedRate)
	{
		if (!IsActive() || !isfinite(dt) || dt <= 0.0
			|| !isfinite(speedRate) || speedRate <= 0.0) return;

		progress = clamp(progress + dt * speedRate / duration, 0.0, 1.0);
	}
	bool IsActive() const
	{
		return isfinite(duration) && duration > 0.0
			&& isfinite(progress) && progress < 1.0;
	}
	double GetRemainingDuration() const
	{
		if (!IsActive()) return 0.0;
		return duration * (1.0 - progress);
	}
	double GetProgress() const
	{
		return clamp(progress, 0.0, 1.0);
	}

private:
	double duration = 0.0; // 基础总时长，不包含全局速度倍率
	double progress = 1.0; // 始终按真实时间线性推进，曲线不能反向修改它
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
	BarUiValueClass(double valT, BarUiValueModeEnum modT = BarUiValueModeEnum::Variable, optional<double> desT = nullopt)
	{
		mod = modT;
		if (desT.has_value()) des = desT.value();
		SetDirect(valT);
	}

	bool IsSame() { return val == tar && !hasMiddleV; }
	bool SetTar(double tarT, optional<double> durT = nullopt, optional<double> middleVT = nullopt, bool forceRestart = false)
	{
		if (!forceRestart && tar == tarT)
		{
			// UI 每帧会重复提交普通目标，此时保留已经启动的中间关键帧过程。
			if (!middleVT.has_value()) return false;
			if (hasMiddleV && middleV == middleVT.value()) return false;
		}

		startV = val;
		progress = 0.0;
		tar = tarT;
		hasMiddleV = middleVT.has_value();
		if (middleVT.has_value()) middleV = middleVT.value();

		double distance = abs(static_cast<double>(tar) - static_cast<double>(startV));
		if (middleVT.has_value())
		{
			distance = abs(middleVT.value() - static_cast<double>(startV))
				+ abs(static_cast<double>(tar) - middleVT.value());
		}
		double defaultSpeed = des;
		if (durT.has_value()) dur = durT.value();
		else if (isfinite(distance) && isfinite(defaultSpeed) && defaultSpeed > 0.0) dur = distance / defaultSpeed;
		else dur = 0.0;
		return true;
	}
	void SetDirect(double valueT)
	{
		val = valueT;
		tar = valueT;
		startV = valueT;
		progress = 0.0;
		dur = 0.0;
		hasMiddleV = false;
	}
	void Initialization(double valT, BarUiValueModeEnum modT = BarUiValueModeEnum::Variable, optional<double> desT = nullopt)
	{
		mod = modT;
		if (desT.has_value()) des = desT.value();
		SetDirect(valT);
	}

public:
	IdtAtomic<BarUiValueModeEnum> mod = BarUiValueModeEnum::Linear;
	IdtAtomic<BarUiCurveEnum> curve = BarUiCurveEnum::Linear;

	IdtAtomic<double> val = 0.0; // 直接值（当前位置）
	IdtAtomic<double> tar = 0.0; // 目标值（目标位置）

	IdtAtomic<double> des = BarUiDefaultDes; // 默认速度 px/s；未提交过程时长时用于推导 dur
	IdtAtomic<double> dur = 0.0; // 当前动画段的基础时长 s，不包含全局速度倍率
	IdtAtomic<double> startV = 0.0; // 起始位置（用于计算百分比，在界面设被设置时）
	IdtAtomic<double> progress = 0.0; // 当前动画段的线性进度（曲线 x，0->1）
	IdtAtomic<bool> hasMiddleV = false; // 是否经过位于时间 0.5 的单个中间关键帧
	IdtAtomic<double> middleV = 0.0; // 中间关键帧值；tar 始终保留最终目标
};
//// 颜色 UI 值（忽略透明度）
class BarUiColorClass
{
public:
	BarUiColorClass() {}
	BarUiColorClass(COLORREF valT, optional<double> desT = nullopt)
	{
		if (desT.has_value()) des = desT.value();
		SetDirect(valT);
	}

	bool IsSame() { return val == tar; }
	bool SetTar(COLORREF tarT, optional<double> durT = nullopt)
	{
		if (tar == tarT) return false;

		startColor = val;
		progress = 0.0;
		tar = tarT;

		double defaultSpeed = des;
		if (durT.has_value()) dur = durT.value();
		else if (isfinite(defaultSpeed) && defaultSpeed > 0.0) dur = 1.0 / defaultSpeed;
		else dur = 0.0;
		return true;
	}
	void SetDirect(COLORREF valueT)
	{
		val = valueT;
		tar = valueT;
		startColor = valueT;
		progress = 0.0;
		dur = 0.0;
	}
	void Initialization(COLORREF valT, optional<double> desT = nullopt)
	{
		if (desT.has_value()) des = desT.value();
		SetDirect(valT);
	}

public:
	IdtAtomic<BarUiCurveEnum> curve = BarUiCurveEnum::Linear;
	IdtAtomic<COLORREF> val = RGB(0, 0, 0); // 直接值（当前位置）
	IdtAtomic<COLORREF> tar = RGB(0, 0, 0); // 目标值（目标位置）
	IdtAtomic<COLORREF> startColor = RGB(0, 0, 0); // 起始颜色（用于计算百分比，在界面被设置时）
	IdtAtomic<double> progress = 0.0; // 当前动画段的线性进度（曲线 x，0->1）

	IdtAtomic<double> des = 0.60; // 默认速度 1/s；未提交过程时长时用于推导 dur
	IdtAtomic<double> dur = 0.0; // 当前动画段的基础时长 s，不包含全局速度倍率
};
//// 透明度 UI 值
class BarUiPctClass
{
public:
	BarUiPctClass() {}
	BarUiPctClass(double valT, optional<double> desT = nullopt)
	{
		if (desT.has_value()) des = desT.value();
		SetDirect(valT);
	}

	bool IsSame() { return val == tar && !hasMiddleV; }
	bool SetTar(double tarT, optional<double> durT = nullopt, optional<double> middleVT = nullopt, bool forceRestart = false)
	{
		if (!forceRestart && tar == tarT)
		{
			// UI 每帧会重复提交普通目标，此时保留已经启动的中间关键帧过程。
			if (!middleVT.has_value()) return false;
			if (hasMiddleV && middleV == middleVT.value()) return false;
		}

		startV = val;
		progress = 0.0;
		tar = tarT;
		hasMiddleV = middleVT.has_value();
		if (middleVT.has_value()) middleV = middleVT.value();

		double defaultSpeed = des;
		if (durT.has_value()) dur = durT.value();
		else if (isfinite(defaultSpeed) && defaultSpeed > 0.0) dur = 1.0 / defaultSpeed;
		else dur = 0.0;
		return true;
	}
	void SetDirect(double valueT)
	{
		val = valueT;
		tar = valueT;
		startV = valueT;
		progress = 0.0;
		dur = 0.0;
		hasMiddleV = false;
	}
	void Initialization(double valT, optional<double> desT = nullopt)
	{
		if (desT.has_value()) des = desT.value();
		SetDirect(valT);
	}

public:
	IdtAtomic<BarUiCurveEnum> curve = BarUiCurveEnum::Linear;
	IdtAtomic<double> val = 1.0; // 透明度直接值
	IdtAtomic<double> tar = 1.0; // 颜色目标值
	IdtAtomic<double> startV = 1.0; // 起始透明度（用于计算百分比，在界面被设置时）
	IdtAtomic<double> progress = 0.0; // 当前动画段的线性进度（曲线 x，0->1）

	IdtAtomic<double> des = 0.60; // 默认速度 1/s；未提交过程时长时用于推导 dur
	IdtAtomic<double> dur = 0.0; // 当前动画段的基础时长 s，不包含全局速度倍率
	IdtAtomic<bool> hasMiddleV = false; // 是否经过位于时间 0.5 的单个透明度关键帧
	IdtAtomic<double> middleV = 0.0; // 中间透明度；tar 始终保留最终目标
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
	// 内部对齐：将子控件指定锚点对齐到父控件同名锚点，再叠加子控件的 x/y 偏移。

	TopLeft = 0, // 子左上角 = 父左上角 + (x, y)
	Top = 1, // 子上边中点 = 父上边中点 + (x, y)
	Left = 4, // 子左边中点 = 父左边中点 + (x, y)
	Center = 5, // 子中心 = 父中心 + (x, y)
	Right = 6, // 子右边中点 = 父右边中点 + (x, y)

	// 非对称
	CenterFromTopLeft = 10, // 子中心 = 父左上角 + (x, y)，适合使用中心坐标描述父级内部布局

	// 外部停靠：将子控件贴在父控件外侧，再叠加子控件的 x/y 偏移。

	ToTop = 11, // 子下边中点 = 父上边中点 + (x, y)，子控件位于父级上方
	ToRight = 13, // 子左边中点 = 父右边中点 + (x, y)，子控件位于父级右侧
	ToLeft = 15, // 子右边中点 = 父左边中点 + (x, y)，子控件位于父级左侧
	ToBottom = 17, // 子上边中点 = 父下边中点 + (x, y)，子控件位于父级下方
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
1. BarUiValueClass 已有 val/tar/mod/des/dur/startV，可表达“当前值、目标值、动画类型、默认速度、过程时长、动画段起点”。
2. Bar.Main.cpp 的 ChangeValue/ChangeColor/ChangePct 已接入匀速逐帧推进；Linear/Variable 暂时同为线性曲线。
3. MainBar、DrawAttributeBar 的部分 w/h 已设置为 Variable，framePct 已加入动效同步；后续还需要按实际视觉效果细化 x、pct、framePct 等模式。

建议模型：
1. 目标变化时通过 SetTar 记录动画段起点，而不是每帧重置：
   value.SetTar(newTar);
   SetTar 内部只有在 newTar != oldTar 时才会 startV = val、progress = 0.0，并更新 tar。
   因为计算 UI 阶段会每帧重复写相同目标，所以重复 SetTar 同一个目标不应重启动画。

2. progress 表示曲线横轴 x，始终按真实时间线性从 0 -> 1。
	调用方提交 dur 时直接使用；未提交时由距离和默认 des 推导：dur = abs(tar - startV) / des。
	每帧叠加全局速度倍率推进，但不修改动画段保存的基础 dur：
		progress += dt * speedRate / dur;

3. 每帧推进：
   double x = clamp(progress, 0.0, 1.0);
   double y = Curve(x);
   value.val = value.startV + (value.tar - value.startV) * y;

   Linear:   Curve(x) = x。
   Variable: 后续可使用 EaseOutBack 这类回弹曲线，y 允许超过 1 后回到 1。

4. 动画完成：
	当 progress >= 1.0 时收尾，保证同 dur 的组合属性在同一帧到达：
       value.val = value.tar;
       value.startV = value.tar;
       value.progress = 0.0;

注意：
1. 回弹曲线下 val 可能非单调，不能从 val 反推 progress；progress/startV/tar 必须作为动画状态保存。
2. 若之后希望打断时速度也完全连续，需要改成带 velocity 的弹簧积分模型；当前备忘先按曲线模型实现。
*/
