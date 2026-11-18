module;

#include <windows.h>
#include "../../../IdtAtomic.h"
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <functional>
#include <optional>
#include <string>

export module Inkeys.UI.UI3:Button;

import :Primitive;
import :Renderer;
export import Inkeys.UI.UI3.Metrics;

using namespace std;

export namespace Inkeys::UI::UI3
{

enum class ButtonSizeEnum : int
{
	twoTwo, // 2*2 -> 70 * 70（= 两枚 2*1 + 间隙 5，或四枚 1*1）
	twoOne, // 2*1 -> 70 * 32.5（= 两枚 1*1 + 间隙 5）
	oneTwo, // 1*2 -> 10 * 70 仅限分割线（偏窄）
	oneOne // 1*1 -> 32.5 * 32.5（正方形）
};
enum class WidgetState : int
{
	None,
	Disable, // 禁用
	Selected // 选中
};
enum class WidgetEmphasize : int
{
	None,
	Hover, // 悬停（废弃）
	Pressed, // 按下
};

class ButtonStateClass
{
public:
	ButtonStateClass() {}
public:
	IdtAtomic<WidgetState> state = WidgetState::None;
	IdtAtomic<WidgetEmphasize> emph = WidgetEmphasize::None;
};

enum class ButtonHoverStageEnum : int
{
	None,
	Showing,
	Fading,
};

enum class ButtonIconKindEnum : int
{
	Svg,
	Png,
};

// 按钮角标：相对按钮外框固定，独立显隐动画；默认不挂载，其他按钮不受影响。
class ButtonBadgeClass
{
public:
	UiSVGClass svg;
	// 可见度驱动以角标中心为原点的缩放与旋转；below 为 0 右上、1 右下的换边过渡。
	UiValueClass progress = UiValueClass(0.0);
	UiValueClass below = UiValueClass(0.0);
	// 独立旋转角：进场 -45→0，离场 →+45；中途反向从当前角度继续，避免按目标重算产生跳变。
	UiValueClass angle = UiValueClass(45.0);
	IdtAtomic<bool> shown = false;
	IdtAtomic<bool> belowTarget = false;
	// 所在面板的姿态缩放；主栏为 1。
	IdtAtomic<double> scale = 1.0;
};

class ButtonClass
{
public:
	ButtonClass()
		: state(&localState)
	{
		// 按钮背景只承载选中、按下和悬停效果，首次显示前必须保持完全透明。
		button.pct.Initialization(0.0);
	}
	// 拷贝本地状态时重绑到自身；绘制记录沿用原拷贝构造的默认零值。
	ButtonClass(const ButtonClass& other)
		: size(other.size),
		button(other.button),
		hoverStage(other.hoverStage),
		pressScale(other.pressScale),
		name(other.name),
		icon(other.icon),
		pngIcon(other.pngIcon),
		iconKind(other.iconKind),
		clickFunc(other.clickFunc),
		badge(other.badge),
		localState(other.localState),
		state(other.state == &other.localState ? &localState : other.state)
	{}

public:
	IdtAtomic<ButtonSizeEnum> size;
	// 绘制记录
	IdtAtomic<double> lastDrawX = 0.0;
	IdtAtomic<double> lastDrawY = 0.0;

	// 按钮控件
	UiShapeClass button;
	// 悬停、按下和选中共用按钮原背景层，避免多层透明色叠加时发生闪变。
	IdtAtomic<ButtonHoverStageEnum> hoverStage = ButtonHoverStageEnum::None;
	// 背景、图标和文字共用的绘制倍率，不参与布局及命中区域计算。
	UiValueClass pressScale = UiValueClass(1.0);
	UiWordClass name;
	// SVG 继续承载统一的布局、透明度和动画状态；PNG 仅替换最终绘制载荷。
	UiSVGClass icon;
	UiPNGClass pngIcon;
	IdtAtomic<ButtonIconKindEnum> iconKind = ButtonIconKindEnum::Svg;

	function<void()> clickFunc;
	// 可选角标须在按钮发布给渲染线程前挂载，运行期只改其目标值。
	std::optional<ButtonBadgeClass> badge;

	ButtonStateClass localState;
	ButtonStateClass* state = nullptr;
};

	// 分隔项由调用方显式门控，通用入口不读取产品 preset。
	bool StartButtonHoverVisual(UiPctClass* hoverPct, UiColorClass* hoverFill,
		IdtAtomic<ButtonHoverStageEnum>* hoverStage);
	bool StopButtonHoverVisual(UiPctClass* hoverPct, UiColorClass* hoverFill,
		IdtAtomic<ButtonHoverStageEnum>* hoverStage, bool immediate,
		bool preserveVisual = false);
	bool StartButtonHoverVisual(ButtonClass& button, bool separator = false) noexcept;
	bool StopButtonHoverVisual(ButtonClass& button, bool immediate,
		bool preserveVisual = false, bool separator = false) noexcept;
	bool UpdateButtonHoverVisual(UiPctClass& pct, UiColorClass* fill,
		IdtAtomic<ButtonHoverStageEnum>& stage, bool visible,
		bool hoverAllowed, double fadeDurationSeconds) noexcept;
	bool UpdateButtonHoverVisual(ButtonClass& button, bool visible,
		bool hoverAllowed, double fadeDurationSeconds) noexcept;
	void SetButtonPressedVisual(ButtonClass& button, bool pressed) noexcept;
	// 显式深浅值放在首参，避免配色参数隐式转 bool 时产生重载二义。
	void RetargetButtonInteractionVisual(bool dark, ButtonClass& button,
		bool visible, bool enabled, bool selected,
		double durationSeconds, std::optional<COLORREF> contentColor = std::nullopt,
		bool iconVisible = true, std::optional<COLORREF> fillColorOverride = std::nullopt,
		std::optional<COLORREF> frameColorOverride = std::nullopt) noexcept;

	// 角标：按钮外框右上（倒转时右下）内收 4DIP、直径 10DIP；缩放与旋转以角标中心为原点。
	inline constexpr double ButtonBadgeDiameterDip = 10.0, ButtonBadgeInsetDip = 4.0;
	struct ButtonBadgeGeometry { double centerX = 0, centerY = 0, diameter = 0, scale = 0, angle = 0; };
	void InitializeButtonBadge(ButtonBadgeClass& badge, const wchar_t* resource = L"barStatusPin");
	[[nodiscard]] ButtonBadgeGeometry ResolveButtonBadgeGeometry(ButtonClass& button,
		const UiInheritClass& buttonInherit) noexcept;
	// 旋转角标的外接圆包络（像素）；无角标或已完全隐藏时为空。
	[[nodiscard]] RECT ResolveButtonBadgeBounds(ButtonClass& button,
		const UiInheritClass& buttonInherit, double zoom) noexcept;

	// 固定属性借用已有字段，不复制控件，不进入注册表。
	struct ButtonVisualParts
	{
		UiShapeClass& background;
		UiSVGClass* icon = nullptr;
		UiWordClass* primary = nullptr;
		UiValueClass* pressScale = nullptr;
		IdtAtomic<ButtonHoverStageEnum>* hoverStage = nullptr;
	};
	void RetargetFixedButtonVisual(bool dark, const ButtonVisualParts& parts,
		bool visible, bool enabled, bool selected, bool pressed, double duration,
		std::optional<COLORREF> contentColor = std::nullopt, bool iconVisible = true,
		std::optional<COLORREF> fillColor = std::nullopt,
		std::optional<COLORREF> frameColor = std::nullopt) noexcept;

	struct ButtonVisualInheritance
	{
		UiInheritClass button;
		UiInheritClass icon;
		UiInheritClass primary;
		std::optional<UiInheritClass> secondary;
	};

	struct ContentExtent
	{
		D2D1_RECT_F boundsDip{};
		bool animationActive = false;
	};

	struct ButtonDrawOptions
	{
		UiWordClass* secondary = nullptr;
		DWRITE_FONT_WEIGHT primaryWeight = DWRITE_FONT_WEIGHT_BOLD;
		DWRITE_FONT_WEIGHT secondaryWeight = DWRITE_FONT_WEIGHT_NORMAL;
		DWRITE_TEXT_ALIGNMENT primaryAlignment = DWRITE_TEXT_ALIGNMENT_CENTER;
		bool drawIcon = true;
		using Content = std::function<bool(UIRendering&, ID2D1DeviceContext*,
			ButtonClass&, const ButtonVisualInheritance&)>;
		Content underlay;
		Content content;
		// 自绘内容需要参与拒绘/脏区时，声明两个回调的联合可视包络及动画活跃状态。
		// 坐标为按钮左上角原点的 DIP，位于按压缩放和 DC 变换之前；须覆盖全部扩展像素。
		// 未声明且有回调时保守地不提前拒绘；声明不扩大命中区，调用方自行并入脏区/续帧。
		std::optional<ContentExtent> contentExtent;
	};
	struct FixedButtonDrawOptions
	{
		using Content = std::function<bool(UIRendering&, ID2D1DeviceContext*,
			const ButtonVisualParts&, const ButtonVisualInheritance&)>;
		Content underlay;
		Content content;
		// 范围/活跃声明沿用普通按钮合同；未声明的回调保守绘制。
		std::optional<ContentExtent> contentExtent;
		// 分批绘制专有内容时复用按压变换，避免重复叠加背景。
		bool drawBackground = true;
	};
	std::optional<D2D1_RECT_F> ResolveFixedButtonVisualBounds(UIRendering& renderer,
		const ButtonVisualParts& parts, const ButtonVisualInheritance& inheritance,
		const FixedButtonDrawOptions& options = {});
	// 返回标准内容（含角标）与声明内容的联合像素包络，尚未应用按压/DC 变换。
	// 有回调但未声明范围、或元数据无效时返回空，调用方须沿用保守脏区策略。
	std::optional<D2D1_RECT_F> ResolveButtonVisualBounds(UIRendering& renderer, ButtonClass& button,
		const ButtonVisualInheritance& inheritance, const ButtonDrawOptions& options = {});

	// 所有目标消费同一个渲染批次时间；观察者只接收实际推进结果，不再推进动画。
	enum class ButtonAnimationPart { Shape, Icon, Primary, Secondary, Press, Badge };
	enum class ButtonAnimationProperty { Geometry, Visibility, Color, Content };
	struct ButtonAnimationContext
	{
		double dtSeconds = 0.0;
		double speedRate = 1.0;
		bool animationEnabled = true;
		bool forceReplace = false;
		bool visible = true;
		bool hoverAllowed = true;
		bool finiteSvgProbe = false;
		std::function<void(const void*, ButtonAnimationPart, ButtonAnimationProperty,
			bool changed, bool active, bool same)> observe;
	};
	struct ButtonAnimationResult { bool changed = false, active = false; };
	[[nodiscard]] ButtonAnimationResult AdvanceButtonVisual(
		ButtonClass& button, const ButtonAnimationContext& frame,
		UiWordClass* secondary = nullptr, bool separator = false);
	[[nodiscard]] ButtonAnimationResult AdvanceShapeVisual(
		UiShapeClass&, const ButtonAnimationContext&);
	[[nodiscard]] ButtonAnimationResult AdvanceSvgVisual(
		UiSVGClass&, const ButtonAnimationContext&);
	[[nodiscard]] ButtonAnimationResult AdvanceWordVisual(
		UiWordClass&, const ButtonAnimationContext&,
		ButtonAnimationPart part = ButtonAnimationPart::Primary);

	void ConfigureButtonVisualTargets(ButtonClass& button,
		ButtonVisualLayoutKind layoutKind, double durationSeconds,
		double scale = 1.0, bool direct = false,
		std::optional<double> iconHeightOverride = std::nullopt,
		std::optional<double> fontTargetScale = std::nullopt,
		std::optional<double> labelFontSize = std::nullopt, UIRendering* labelRenderer = nullptr);

	struct ButtonHitSnapshot
	{
		D2D1_RECT_F boundsDip{};
		bool visible = false, enabled = false;
		double radiusX = 0.0, radiusY = 0.0, superellipseExponent = 0.0;
	};
	[[nodiscard]] ButtonHitSnapshot ResolveShapeHitSnapshot(
		const UiShapeClass& shape, const UiInheritClass& inherit,
		bool visible = true, bool enabled = true) noexcept;
	[[nodiscard]] ButtonHitSnapshot ResolveButtonHitSnapshot(
		ButtonClass& button, const UiInheritClass& inherit, bool visible, bool enabled) noexcept;
	[[nodiscard]] bool HitTestButton(const ButtonHitSnapshot& hit,
		double xDip, double yDip) noexcept;

	// 显式父坐标是唯一真值；绘制与 dirty 必须先建立同一条继承链。
	[[nodiscard]] ButtonVisualInheritance
		PrepareButtonVisualInheritance(ButtonClass& button,
			const UiInheritClass& inherit,
			UiWordClass* secondary = nullptr) noexcept;

	bool DrawButtonVisual(UIRendering& renderer,
		ID2D1DeviceContext* deviceContext, ButtonClass& button,
		const UiInheritClass& inherit,
		const ButtonDrawOptions& options = {}, bool separator = false);
	bool DrawFixedButtonVisual(UIRendering& renderer, ID2D1DeviceContext* context,
		const ButtonVisualParts& parts, const ButtonVisualInheritance& inheritance,
		DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
		// 默认保留固定控件原有文字裁排，需要标签拟合时由调用方显式启用。
		bool fitPrimaryLabel = false, const FixedButtonDrawOptions& options = {});
	bool DrawBackgroundVisual(UIRendering& renderer,
		ID2D1DeviceContext* deviceContext, UiShapeClass& background,
		const UiInheritClass& inherit, RECT* targetRect = nullptr,
		bool clip = false);

}
