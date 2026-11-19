module;

#include <windows.h>
#include "../../../IdtAtomic.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include <cstdint>
#include <type_traits>

export module Inkeys.UI.UI3:Primitive;

export import Inkeys.UI.UI3.Animation;
import Inkeys.UI.UI3.Material;
export import Inkeys.UI.UI3.PresentationProbe;

using namespace std;

export namespace Inkeys::UI::UI3
{

//// 文字 UI 值
class UiStringClass
{
public:
	UiStringClass() {}
	UiStringClass(wstring valT)
	{
		unique_lock lockValmt(valmt);
		val = valT;
		BumpRevision();
		lockValmt.unlock();

		unique_lock lockTarmt(tarmt);
		tar = valT;
		lockTarmt.unlock();
	}
	// 拷贝构造函数，只拷贝值，mutex新建
	UiStringClass(const UiStringClass& other)
	{
		// 与 ApplyTar/IsSame 保持 val -> tar 的锁顺序，避免读取并发写入中的 wstring。
		shared_lock lockVal(other.valmt);
		shared_lock lockTar(other.tarmt);
		val = other.val;
		tar = other.tar;
		valRevision_.store(other.valRevision_.load(std::memory_order_relaxed), std::memory_order_relaxed);
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
		BumpRevision();
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
		if (val == tar) return;
		val = tar;
		BumpRevision();
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
		// 版本号只在显示值真正改变时递增，重复设置相同文字不能让视觉脏区每帧判定变化。
		if (val != valT) { val = valT; BumpRevision(); }
		lockValmt.unlock();

		unique_lock lockTarmt(tarmt);
		tar = valT;
		lockTarmt.unlock();
	}

public:
	friend bool operator==(const UiStringClass& lhs, const UiStringClass& rhs)
	{
		if (&lhs == &rhs) return true; // 同一个对象

		// std::less 为无关对象指针提供稳定全序，随后统一按 valmt -> tarmt 加锁。
		const UiStringClass* first = &lhs;
		const UiStringClass* second = &rhs;
		if (std::less<const UiStringClass*>{}(second, first)) swap(first, second);

		// 为了防止死锁，分别锁两个对象的valmt和tarmt
		// 总是先valmt，再tarmt（重要！避免死锁）
		shared_lock vlock1(first->valmt);
		shared_lock tlock1(first->tarmt);

		shared_lock vlock2(second->valmt);
		shared_lock tlock2(second->tarmt);

		return lhs.val == rhs.val && lhs.tar == rhs.tar;
	}
	friend bool operator!=(const UiStringClass& lhs, const UiStringClass& rhs)
	{
		return !(lhs == rhs);
	}

	// 显示值（val）每真正改变一次递增一次：视觉脏区采样比较版本号，避免每帧复制长文本（SVG 内容可达数 KB）。
	[[nodiscard]] std::uint64_t ValRevision() const noexcept { return valRevision_.load(std::memory_order_relaxed); }

protected:
	void BumpRevision() noexcept { valRevision_.fetch_add(1, std::memory_order_relaxed); }
	mutable shared_mutex valmt;
	mutable shared_mutex tarmt;
	wstring val = L"";
	wstring tar = L"";
	std::atomic<std::uint64_t> valRevision_{ 1 };
};

// 前向声明
class UiShapeClass;
class UiSuperellipseClass;
class UiSVGClass;
class UiWordClass;
// 前向声明

/// 继承
//// 位置继承
enum class UiInheritEnum
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
class UiInheritClass
{
public:
	UiInheritClass(double xT, double yT);
	UiInheritClass(UiInheritEnum typeT, double xO, double yO, double wO, double hO, double xT, double yT, double wT, double hT);

public:
	UiInheritEnum type = UiInheritEnum::Center;
	double x = 0.0; // 继承坐标左上角 x 坐标
	double y = 0.0; // 继承坐标左上角 y 坐标
};
//// 继承基类
class UiInnheritBaseClass
{
protected:
	UiInnheritBaseClass() = default;

public:
	UiValueClass x; // 控件中心 x 坐标
	UiValueClass y; // 控件中心 y 坐标
	UiValueClass w; // 控件宽度
	UiValueClass h; // 控件高度
	UiPctClass pct; // 透明度

public:
	UiInheritClass Inherit() { return UpInh(UiInheritClass(x.val - w.tar / 2.0, y.val - h.tar / 2.0)); }
	UiInheritClass Inherit(UiInheritEnum typeT, const UiInnheritBaseClass& obj) { return UpInh(UiInheritClass(typeT, x.val, y.val, w.val, h.val, obj.inhX, obj.inhY, obj.w.val, obj.h.val)); }

public:
	// 继承值 -> 也就是实际绘制的位置
	IdtAtomic<double> inhX = 0.0; // 控件左上角 x 坐标
	IdtAtomic<double> inhY = 0.0; // 控件左上角 y 坐标
	const UiInheritClass& UpInh(const UiInheritClass& inh)
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
enum class UiFrameRenderingEnum : int
{
	Solid = 0,
	PointLight = 1,
};
enum class UiFrameLightColorEnum : int
{
	Frame = 0,
	PenWhenDrawing = 1,
};
enum class UiFrameLightOpacitySourceEnum : int
{
	FramePct = 0,
	ObjectPct = 1,
};

//// 单个形状控件
// Shape 与独立 surface 共用同一圆角命中，避免只在绘制上看起来一致。
bool UiSuperellipseContainsPoint(double mx, double my, double zoom,
	double x, double y, double width, double height, double exponent, double epsilon = 1e-6);
bool UiRoundedRectContainsPoint(double mx, double my, double zoom,
	double leftDip, double topDip, double widthDip, double heightDip,
	double radiusXDip, double radiusYDip, double epsilon = 1e-6) noexcept;

class UiShapeClass : public UiInnheritBaseClass
{
public:
	UiShapeClass() {}
	UiShapeClass(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, UiValueModeEnum type = UiValueModeEnum::Linear);

	void Initialization(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, UiValueModeEnum type = UiValueModeEnum::Linear);

public:
	bool IsClick(int mx, int my, double zoom, double epsilon = 1e-6);

public:
	// 整体该控件是否显示
	UiStateClass enable;

	// 模态
	optional<UiValueClass> rw; // 控件圆角半径
	optional<UiValueClass> rh; // 控件圆角半径
	optional<UiValueClass> ft; // 控件边框宽度

	// 颜色
	optional<UiColorClass> fill; // 控件填充颜色
	optional<UiColorClass> frame; // 控件边框颜色

	// 透明度
	optional<UiPctClass> framePct; // 控件边框透明度
	optional<UiPctClass> frameLightPct; // 仅点光边框使用的独立透明度
	// 渲染线程逐对象发布当前材质权重，主按钮收展不能改变其他浮层。
	double lightMaterial = 0.0; // 0=原深色，1=浅色
	bool themeSurface = false; // 只有表面启用材质填充、静态阴影和顶部高光
	double lightFillOpacityScale = 1.0; // 浅色端填充透明度比例，仅常驻表面不为 1
	bool residentSurface = false; // 常驻表面（主栏、主按钮、吸附指示）：填充受用户背景透明度控制
	UiFrameRenderingEnum frameRendering = UiFrameRenderingEnum::Solid; // 默认保留原纯色边框
	UiFrameLightColorEnum frameLightColor = UiFrameLightColorEnum::Frame;
	UiFrameLightOpacitySourceEnum frameLightOpacitySource = UiFrameLightOpacitySourceEnum::FramePct;
	bool framePrimaryLightEnabled = true; // PointLight 默认接受主光源，可按控件关闭
	double frameCursorLightIntensityScale = 1.0; // 鼠标光默认与主光同强度
	double frameLightCursorLightIntensityScale = -1.0; // >=0 时作为浅色端点，保持 Dark 原倍率
};
//// 单个超椭圆控件
class UiSuperellipseClass : public UiInnheritBaseClass
{
public:
	UiSuperellipseClass() {}
	UiSuperellipseClass(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, UiValueModeEnum type = UiValueModeEnum::Linear);

	void Initialization(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, UiValueModeEnum type = UiValueModeEnum::Linear);

public:
	bool IsClick(int mx, int my, double zoom, double epsilon = 1e-6);

public:
	// 整体该控件是否显示
	UiStateClass enable;

	// 模态

	optional<UiValueClass> n;
	optional<UiValueClass> ft; // 控件边框宽度

	// 颜色

	optional<UiColorClass> fill; // 控件填充颜色
	optional<UiColorClass> frame; // 控件边框颜色

	// 透明度
	optional<UiPctClass> framePct; // 控件边框透明度
	// 颜色、透明度、静态及动态光效共用同一个连续权重。
	double lightMaterial = 0.0; // 0=原深色，1=浅色
	bool themeSurface = false;
	double lightFillOpacityScale = 1.0; // 浅色端填充透明度比例，仅常驻表面不为 1
	bool residentSurface = false; // 常驻表面（主栏、主按钮、吸附指示）：填充受用户背景透明度控制
	UiFrameRenderingEnum frameRendering = UiFrameRenderingEnum::Solid; // 默认保留原纯色边框
	UiFrameLightColorEnum frameLightColor = UiFrameLightColorEnum::Frame;
	UiFrameLightOpacitySourceEnum frameLightOpacitySource = UiFrameLightOpacitySourceEnum::FramePct;
	bool framePrimaryLightEnabled = true; // PointLight 默认接受主光源，可按控件关闭
	double frameCursorLightIntensityScale = 1.0; // 鼠标光默认与主光同强度
	double frameLightCursorLightIntensityScale = -1.0; // >=0 时作为浅色端点，保持 Dark 原倍率
};
//// 单个 SVG 控件
class UiSVGClass : public UiInnheritBaseClass
{
public:
	UiSVGClass() {}
	UiSVGClass(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, UiValueModeEnum type = UiValueModeEnum::Linear);

	void Initialization(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, UiValueModeEnum type = UiValueModeEnum::Linear);

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
	[[nodiscard]] bool IsContentTransitionActive();
	[[nodiscard]] bool BindObservationTag(std::uint32_t tag) noexcept;
	void NotifyObservedContentValueWrite() noexcept;
	[[nodiscard]] Ui3SvgBitmapProof ObservedBitmapProof() const noexcept;
	[[nodiscard]] const Ui3SvgObjectObservation& ObservationState() const noexcept { return observed_; }
	void ObserveFiniteRequirement(bool domainRelevant) noexcept;
	// SVG 位图属于当前 D2D device，设备 epoch 切换时必须主动释放。
	void ResetCache();

public:
	// 整体该控件是否显示
	UiStateClass enable;
	// 绕目标矩形中心旋转，不改变控件本身的宽高。
	UiValueClass angle;

	// 颜色

	optional<UiColorClass> color1; // 控件强调颜色1（忽略透明度)
	optional<UiColorClass> color2; // 控件强调颜色2（忽略透明度)
	// 替换标志为
	// color1 -> rgba(10,0,7,0)
	// color2 -> rgba(9,0,2,0)

public:
	// 外观键非空时，在栅格化前执行宿主提供的节点加工；设备重建保留输入。
	std::function<bool(const std::function<bool(const char* id, const char* name, const std::string& value)>& setAttribute)> documentPreprocess;
	std::optional<std::uint64_t> appearanceKey, cAppearanceKey;
	// SVG 内容
	UiStringClass svg;
	Microsoft::WRL::ComPtr<ID2D1Bitmap> cacheBitmap;
	unsigned long long cacheDeviceGeneration = 0; // 成功上传时记录设备代次，外部重置链之外的兜底。
	// 生成或上传位图失败的日志闩锁：每个图元每设备代次只记一次。
	std::optional<unsigned long long> failureLoggedGeneration;
	// 内容切换倍率不参与 SVG 栅格化尺寸，只在最终绘制时叠乘。
	IdtAtomic<double> contentScale = 1.0;
	IdtAtomic<double> contentPct = 1.0;

	double cW = 0.0, cH = 0.0; // 缓存宽度、高度
	COLORREF cColor1 = RGB(0, 0, 0), cColor2 = RGB(0, 0, 0);
	bool CacheBitmap(ID2D1DeviceContext* deviceContext, double tarW, double tarH,
		unsigned long long deviceGeneration);

public:
	bool SetWH(optional<double> wT, optional<double> hT);
protected:
	Ui3SvgObjectObservation observed_;
	const void* observedInitializationObject_ = nullptr;
	const void* observedBitmapObject_ = nullptr;
	void ApplyContentDirect(const wstring& valT);
	pair<double, double> CalcWH();
	UiStringClass transitionSvg;
	UiKeyframeTimelineClass contentTransitionTimeline;
	// 以下载荷只允许在 contentTransitionTimeline 的事务回调内读写。
	double contentTransitionStartScale = 1.0;
	double contentTransitionStartPct = 1.0;
	double contentTransitionMiddleScale = 0.8;
	double contentTransitionKeyframeProgress = 0.5;

public:
	double rW = 0.0; // 实际宽度
	double rH = 0.0; // 实际高度
};
static_assert(std::is_default_constructible_v<UiSVGClass>,
	"UiSVGClass must remain default constructible");
//// 单个 PNG 控件
class UiPNGClass : public UiInnheritBaseClass
{
public:
	UiPNGClass() {}
	UiPNGClass(double xT, double yT, UiValueModeEnum type = UiValueModeEnum::Linear);

	void Initialization(double xT, double yT, UiValueModeEnum type = UiValueModeEnum::Linear);
	bool InitializationFromMemory(const void* data, size_t size);
	bool InitializationFromResource(const wstring& resType, const wstring& resName);
	bool SetWH(optional<double> wT, optional<double> hT);
	bool CacheBitmap(ID2D1DeviceContext* deviceContext, unsigned long long deviceGeneration);
	void ResetCache();

public:
	// 整体该控件是否显示
	UiStateClass enable;
	// 绕目标矩形中心旋转，不改变控件本身的宽高。
	UiValueClass angle;
	Microsoft::WRL::ComPtr<ID2D1Bitmap> cacheBitmap;
	unsigned long long cacheDeviceGeneration = 0; // 成功上传时记录设备代次，外部重置链之外的兜底。
	// 生成或上传位图失败的日志闩锁：每个图元每设备代次只记一次。
	std::optional<unsigned long long> failureLoggedGeneration;

protected:
	vector<unsigned char> bitmapPixels;
	UINT32 bitmapWidth = 0;
	UINT32 bitmapHeight = 0;

public:
	double rW = 0.0; // PNG 原始宽度
	double rH = 0.0; // PNG 原始高度
	// 位图载荷每次成功替换递增：视觉脏区以它代表 PNG 内容身份（缓存位图地址不能代表内容）。
	std::uint64_t contentRevision = 0;
};
//// 单个文字控件
class UiWordClass : public UiInnheritBaseClass
{
public:
	UiWordClass() {}
	UiWordClass(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT = RGB(0, 0, 0), UiValueModeEnum type = UiValueModeEnum::Linear);

	void Initialization(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT = RGB(0, 0, 0), UiValueModeEnum type = UiValueModeEnum::Linear);
	bool TransitionToString(const wstring& contentT, optional<double> durT = nullopt,
		double keyframeProgressT = 0.5, double middleScaleT = 0.8);
	bool SetStringImmediate(const wstring& contentT);
	// 布局读取待切换内容，不提前改动透明中点之前仍在显示的文字。
	wstring TargetContent();
	bool AdvanceContentTransition(double dt, double speedRate);
	void CancelContentTransition();

public:
	// 整体该控件是否显示
	UiStateClass enable;

	// 内容
	UiStringClass content;
	// 文字内容切换与 SVG 使用同一缩放、淡出和中点替换语义。
	IdtAtomic<double> contentScale = 1.0;
	IdtAtomic<double> contentPct = 1.0;

	// 字号
	UiValueClass size;
	// 派生姿态每帧 SetDirect 时显式保留静止字号目标；-1 使用原 size.tar。
	IdtAtomic<double> fontTargetSize = -1.0;
	mutable UiFontCorrectionClass fontCorrection;

	// 颜色
	UiColorClass color;

protected:
	UiStringClass transitionContent;
	UiKeyframeTimelineClass contentTransitionTimeline;
	// 以下载荷只允许在 contentTransitionTimeline 的事务回调内读写。
	double contentTransitionStartScale = 1.0;
	double contentTransitionStartPct = 1.0;
	double contentTransitionMiddleScale = 0.8;
	double contentTransitionKeyframeProgress = 0.5;
};
}
