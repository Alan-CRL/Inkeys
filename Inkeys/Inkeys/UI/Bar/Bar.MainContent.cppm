module;

#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include <chrono>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

export module Inkeys.UI.Bar:MainContent;
import :Button;
import Inkeys.UI.UI3;
import Inkeys.UI.Bar.A2;
import Inkeys.UI.Bar.MainLayoutPolicy;
import Inkeys.UI.Bar.Metrics;
import Inkeys.UI.Bar.RecallChrome;

using namespace Inkeys::UI::UI3;

export namespace Inkeys::UI::Bar
{
	struct MainBarPresentationInput
	{
		StateModeSelectEnum mode;
		bool whiteboard, hasContent, featureEnabled;
		bool freezeAvailable, freezeActive, canvasDebugEnabled, canvasDebugVisual;
		bool settingVisible, moreOpen, compactSize;
		BarA2Projection a2;
		RecallChromeView recall;
	};
	struct MainBarPresentation
	{
		ButtonSizeEnum size;
		bool hide;
		WidgetState state;
	};
	MainBarPresentation ResolveMainBarPresentation(BarButtonPresetEnum preset,
		ButtonSizeEnum baseSize, const MainBarPresentationInput& input);

	// 样式只选资源和文案身份；宿主在原内容切换点解析当前语言（图形保留自己的回退文案）。
	struct MainBarContentStyle
	{
		std::wstring icon;
		std::variant<const char*, draw3::shapes::PresetKind> label;
		bool badge = false;
		std::wstring LocalizedLabel(std::wstring (*lookup)(std::string),
			std::wstring (*shapeLabel)(draw3::shapes::PresetKind)) const;
	};
	MainBarContentStyle ResolveSelectStyle(bool whiteboard, bool separate, bool selection);
	MainBarContentStyle ResolveDrawStyle(bool selected, bool laser, bool highlighter, bool hardPen);
	MainBarContentStyle ResolveEraserStyle(bool selected);
	MainBarContentStyle ResolveGeometryStyle(bool selected, bool pinned, const draw3::shapes::ToolSpec& tool);
	MainBarContentStyle ResolveWhiteboardStyle(bool active, bool resume, bool returnToShow);
	MainBarContentStyle ResolvePierceStyle();

	struct MainBarBatchState
	{
		UiTimelineClass timeline;
		UiCurveEnum curve = UiCurveEnum::EaseInOutCubic;
		std::optional<double> width;
		std::optional<std::vector<BarMainButtonLayoutEntry>> layout;
		std::optional<BarIconOrder> iconOrder;
	};
	struct MainBarBatchInput
	{
		const std::vector<BarMainButtonLayoutEntry>& layout;
		double width, duration;
		BarIconOrder iconOrder;
		bool fold, foldChanged, sideSwitch, hasSideKeyframe;
	};
	struct MainBarBatch
	{
		bool restarted, layoutChanged, forceRestart, sideKeyframe, syncValues;
		double duration;
		UiCurveSpecClass valueCurve, pctCurve, keyframeValueCurve, keyframePctCurve;
	};
	MainBarBatch ResolveMainBarBatch(MainBarBatchState& state, const MainBarBatchInput& input);
	void SyncValueDuration(UiValueClass& value, double duration, bool syncValues, const UiCurveSpecClass& curve);
	void SyncPctDuration(UiPctClass& pct, double duration, const UiCurveSpecClass& curve);

	struct BarButtonListPlacement
	{
		std::shared_ptr<BarButtonClass> button;
		double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
		int column = 0, row = 0, columnSpan = 1, rowSpan = 1;
		bool forced = false;
	};
	struct BarButtonListLayout
	{
		std::vector<BarButtonListPlacement> placements;
		double width = 0.0, height = 0.0;
		int subColumns = 0, explicitRows = 0, totalRows = 0;
		bool showDivider = false;
	};

	// 主题查询可通知订阅者，按原目标写入顺序向宿主取色。
	using MainBarThemeColor = COLORREF (*)(ThemeColorEnum);
	struct MainBarButtonTargetInput
	{
		bool fold, opensRight, shapeSelected, firstFrame, dark;
		double foldAnchor, pressDuration;
		BarIconOrder iconOrder;
		RecallChromeView recall;
		UiCurveSpecClass pressCurve, releaseCurve;
		MainBarThemeColor themeColor;
	};
	// 首选 preset 由宿主按实例解析；同一预设出现多次时仍按按钮身份匹配。
	void AnchorHiddenBarButton(const std::vector<std::shared_ptr<BarButtonClass>>& buttons,
		BarButtonClass* hidden, const BarButtonClass* preferredPreset,
		const std::function<void(UiValueClass&, double)>& setPositionTarget);
	void ApplyMainBarButtonTargets(const std::vector<std::shared_ptr<BarButtonClass>>& buttons,
		const std::function<BarButtonClass*(BarButtonPresetEnum)>& preset, const BarButtonListLayout& layout,
		const MainBarBatch& batch, const MainBarButtonTargetInput& input, UIRendering& renderer);

	struct MainBarBodyTargetInput
	{
		double width, foldedWidth;
		// 无主按钮时底板局部中心为 0，由宿主放置整个主栏。
		std::optional<double> mainButtonWidth;
		const BarSizeMetrics& metrics;
		bool fold, opensRight, animateSize;
		double sizeDuration;
		MainBarThemeColor themeColor;
	};
	void ApplyMainBarBodyTargets(UiShapeClass& body, const MainBarBatch& batch,
		const MainBarBodyTargetInput& input);
	void ApplyMainBarMoreTargets(UiValueClass& progress, UiValueClass& opacity, bool open, double duration);

	struct MainBarHoverSource
	{
		bool mouse = false, more = false;
		POINT screen{};
		std::shared_ptr<BarButtonClass> main;
	};
	struct MainBarHoverState
	{
		std::wstring text;
		std::chrono::steady_clock::time_point enteredAt{};
		bool shown = false;
	};
	struct MainBarHoverInput
	{
		BarSizeMode sizeMode;
		bool fold, dragging, layoutActive, geometryActive, moreMoving;
		// 面板与呈现状态只在主栏门禁通过后读取，面板文案按橡皮、图形的顺序查询。
		std::function<bool()> panelsAndPresentationReady;
		std::function<std::wstring()> panelText;
		std::function<bool()> moreExpanded;
		std::chrono::steady_clock::time_point now;
	};
	struct MainBarHoverUpdate
	{
		bool changed = false;
		std::optional<std::chrono::steady_clock::time_point> deadline;
	};
	MainBarHoverUpdate UpdateMainBarHoverHint(MainBarHoverState& state,
		MainBarHoverSource& source, const MainBarHoverInput& input);
	BarHoverHintRect PositionMainBarHoverHint(UIRendering& renderer, const std::wstring& text,
		double zoom, POINT screen, POINT origin, RECT workArea, double windowWidth, double windowHeight);
}
