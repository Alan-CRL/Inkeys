module;

#include "../../../IdtMain.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <functional>

export module Inkeys.UI.Bar:UI;

import :State;

export import Inkeys.UI.Bar.Animation;

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
	{
		// 与 ApplyTar/IsSame 保持 val -> tar 的锁顺序，避免读取并发写入中的 wstring。
		shared_lock lockVal(other.valmt);
		shared_lock lockTar(other.tarmt);
		val = other.val;
		tar = other.tar;
	}

public:
	wstring GetVal() const
	{
		shared_lock lock(valmt);
		return val;
	}
	void SetVal(const wstring& v)
	{
		unique_lock lock(valmt);
		// 高频布局会重复提交同一文字，持锁短路避免无效字符串复制。
		if (val == v) return;
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
		if (tar == t) return;
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

		// std::less 为无关对象指针提供稳定全序，随后统一按 valmt -> tarmt 加锁。
		const BarUiStringClass* first = &lhs;
		const BarUiStringClass* second = &rhs;
		if (std::less<const BarUiStringClass*>{}(second, first)) swap(first, second);

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
enum class BarUiFrameRenderingEnum : int
{
	Solid = 0,
	PointLight = 1,
};
enum class BarUiFrameLightColorEnum : int
{
	Frame = 0,
	PenWhenDrawing = 1,
};
enum class BarUiFrameLightOpacitySourceEnum : int
{
	FramePct = 0,
	ObjectPct = 1,
};

//// 单个形状控件
// Shape 与独立 surface 共用同一圆角命中，避免只在绘制上看起来一致。
bool BarUiRoundedRectContainsPoint(int mx, int my, double zoom,
	double leftDip, double topDip, double widthDip, double heightDip,
	double radiusXDip, double radiusYDip, double epsilon = 1e-6) noexcept;

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
	optional<BarUiPctClass> frameLightPct; // 仅点光边框使用的独立透明度
	BarUiFrameRenderingEnum frameRendering = BarUiFrameRenderingEnum::Solid; // 默认保留原纯色边框
	BarUiFrameLightColorEnum frameLightColor = BarUiFrameLightColorEnum::Frame;
	BarUiFrameLightOpacitySourceEnum frameLightOpacitySource = BarUiFrameLightOpacitySourceEnum::FramePct;
	bool framePrimaryLightEnabled = true; // PointLight 默认接受主光源，可按控件关闭
	double frameCursorLightIntensityScale = 1.0; // 鼠标光默认与主光同强度
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
	BarUiFrameRenderingEnum frameRendering = BarUiFrameRenderingEnum::Solid; // 默认保留原纯色边框
	BarUiFrameLightColorEnum frameLightColor = BarUiFrameLightColorEnum::Frame;
	BarUiFrameLightOpacitySourceEnum frameLightOpacitySource = BarUiFrameLightOpacitySourceEnum::FramePct;
	bool framePrimaryLightEnabled = true; // PointLight 默认接受主光源，可按控件关闭
	double frameCursorLightIntensityScale = 1.0; // 鼠标光默认与主光同强度
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
	bool TransitionToString(const wstring& valT, optional<double> durT = nullopt,
		double keyframeProgressT = 0.5, double middleScaleT = 0.8);
	bool TransitionToResource(const wstring& resType, const wstring& resName,
		optional<double> durT = nullopt, double keyframeProgressT = 0.5, double middleScaleT = 0.8);
	bool AdvanceContentTransition(double dt, double speedRate);
	void CancelContentTransition();
	// SVG 位图属于当前 D2D device，设备 epoch 切换时必须主动释放。
	void ResetCache();

public:
	// 整体该控件是否显示
	BarUiStateClass enable;
	// 绕目标矩形中心旋转，不改变控件本身的宽高。
	BarUiValueClass angle;

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
	// 内容切换倍率不参与 SVG 栅格化尺寸，只在最终绘制时叠乘。
	IdtAtomic<double> contentScale = 1.0;
	IdtAtomic<double> contentPct = 1.0;

	double cW = 0.0, cH = 0.0; // 缓存宽度、高度
	COLORREF cColor1 = RGB(0, 0, 0), cColor2 = RGB(0, 0, 0);
	bool CacheBitmap(ID2D1DeviceContext* deviceContext, double tarW, double tarH);

public:
	bool SetWH(optional<double> wT, optional<double> hT);
protected:
	void ApplyContentDirect(const wstring& valT);
	pair<double, double> CalcWH();
	BarUiStringClass transitionSvg;
	BarUiKeyframeTimelineClass contentTransitionTimeline;
	// 以下载荷只允许在 contentTransitionTimeline 的事务回调内读写。
	double contentTransitionStartScale = 1.0;
	double contentTransitionStartPct = 1.0;
	double contentTransitionMiddleScale = 0.8;
	double contentTransitionKeyframeProgress = 0.5;

public:
	double rW = 0.0; // 实际宽度
	double rH = 0.0; // 实际高度
};
static_assert(std::is_default_constructible_v<BarUiSVGClass>,
	"BarUiSVGClass must remain default constructible");
//// 单个 PNG 控件
class BarUiPNGClass : public BarUiInnheritBaseClass
{
public:
	BarUiPNGClass() {}
	BarUiPNGClass(double xT, double yT, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

	void Initialization(double xT, double yT, BarUiValueModeEnum type = BarUiValueModeEnum::Linear);
	bool InitializationFromMemory(const void* data, size_t size);
	bool InitializationFromResource(const wstring& resType, const wstring& resName);
	bool SetWH(optional<double> wT, optional<double> hT);
	bool CacheBitmap(ID2D1DeviceContext* deviceContext);
	void ResetCache();

public:
	// 整体该控件是否显示
	BarUiStateClass enable;
	// 绕目标矩形中心旋转，不改变控件本身的宽高。
	BarUiValueClass angle;
	Microsoft::WRL::ComPtr<ID2D1Bitmap> cacheBitmap;

protected:
	vector<unsigned char> bitmapPixels;
	UINT32 bitmapWidth = 0;
	UINT32 bitmapHeight = 0;

public:
	double rW = 0.0; // PNG 原始宽度
	double rH = 0.0; // PNG 原始高度
};
//// 单个文字控件
class BarUiWordClass : public BarUiInnheritBaseClass
{
public:
	BarUiWordClass() {}
	BarUiWordClass(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT = RGB(0, 0, 0), BarUiValueModeEnum type = BarUiValueModeEnum::Linear);

	void Initialization(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT = RGB(0, 0, 0), BarUiValueModeEnum type = BarUiValueModeEnum::Linear);
	bool TransitionToString(const wstring& contentT, optional<double> durT = nullopt,
		double keyframeProgressT = 0.5, double middleScaleT = 0.8);
	bool SetStringImmediate(const wstring& contentT);
	bool AdvanceContentTransition(double dt, double speedRate);
	void CancelContentTransition();

public:
	// 整体该控件是否显示
	BarUiStateClass enable;

	// 内容
	BarUiStringClass content;
	// 文字内容切换与 SVG 使用同一缩放、淡出和中点替换语义。
	IdtAtomic<double> contentScale = 1.0;
	IdtAtomic<double> contentPct = 1.0;

	// 字号
	BarUiValueClass size;

	// 颜色
	BarUiColorClass color;

protected:
	BarUiStringClass transitionContent;
	BarUiKeyframeTimelineClass contentTransitionTimeline;
	// 以下载荷只允许在 contentTransitionTimeline 的事务回调内读写。
	double contentTransitionStartScale = 1.0;
	double contentTransitionStartPct = 1.0;
	double contentTransitionMiddleScale = 0.8;
	double contentTransitionKeyframeProgress = 0.5;
};
