module;

#include "../../../IdtMain.h"
#include "../../../IdtState.h"

#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>
#include <optional>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cmath>
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include <deque>
#include <map>
#include <condition_variable>
#include <coroutine>
#include <exception>
#include <functional>
#include <utility>
#include "../../Input/UiTouchRouting.h"
#include "../../Drawing/Draw3/Draw3.Bridge.h"
#include "../../Drawing/Draw3/Draw3.SpeedEraser.h"

#include <atomic>
#include <cstddef>

export module Inkeys.UI.Bar:Main;

export import Inkeys.UI.UI3;
import :State;
import Inkeys.UI.Bar.EraserAttributeMotion;
import Inkeys.UI.Bar.RecallAttributeMotion;
export import Inkeys.UI.Bar.RecallChrome;
export import :Button;
export import :PenPreviewRendering;
export import Inkeys.UI.Bar.Metrics;

import Inkeys.UI.UI3.Animation;
export import Inkeys.UI.Bar.ToggleClickCoalescer;

import Inkeys.Conv.Color;
import Inkeys.Helper.Thread;
import Inkeys.Message;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
export import Inkeys.UI.UI3.FrameRing;
export import Inkeys.UI.UI3.PresentationProbe;
export import Inkeys.UI.Bar.BottomDock;
export import Inkeys.UI.StartupPlacement;
export import Inkeys.UI.Bar.StatusIndicatorModel;
import Inkeys.UI.Bar.LogoAppearance;
export import Inkeys.UI.UI3.Interaction;
import Inkeys.UI.UI3.Progress;

using namespace Inkeys::UI::UI3;

// ====================
// 动画

IdtAtomic<bool> BarUiEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDynamicEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDebugModeEnabled = false;
IdtAtomic<bool> BarUiDebugFrameRateEnabled = true;

// ====================
// 窗口

LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 窗口模态信息
class BarWindowPosClass
{
public:
	IdtAtomic<int> x = 0, y = 0;
	IdtAtomic<unsigned int> w = 0, h = 0;
	IdtAtomic<unsigned int> pct = 255; // 透明度
};

// ====================
// 媒体

// 媒体操控类
class BarMediaClass
{
public:
	void LoadFormat();

public:
	unique_ptr<FormatCache> formatCache;

protected:
};

// ====================
// 界面

// 前向声明
class BarUISetClass;
class BarRenderLoopCoordinator;

// 渲染线程唯一持有模型；交互线程只读已呈现几何并发布命令。
class BarStatusIndicatorPanel
{
public:
	// dragLocked：主栏拖动中锁存倒转方向，松手后再以弹簧换边。
	bool Advance(BarUISetClass&, double dt, double speed, double zoom, bool drawing, bool dragLocked);
	void Layout(Inkeys::UI::Bar::StatusIndicatorLayoutInput, double zoom);
	void Draw(UIRendering&, ID2D1DeviceContext*, const UiSuperellipseClass&, double lightMaterial);
	void CommitPresented();
	bool Pointer(BarUISetClass&, const ExMessage&, bool cancelled, bool contactPointer, bool* accepted);
	bool ResetPointerFeedback();
	std::chrono::steady_clock::time_point PollDeadline(std::chrono::steady_clock::time_point);
	RECT Bounds() const;
	RECT OverflowBounds() const;
	bool Active() const noexcept { return model_.NeedsFrames() || feedbackActive_; }
	bool Changed() const noexcept { return changed_; }
	int RunOffscreenTest(BarUISetClass&);
private:
	Inkeys::UI::Bar::StatusIndicatorModel model_;
	Inkeys::UI::Bar::StatusIndicatorLayout layout_;
	Inkeys::UI::Bar::StatusIndicatorLayoutInput input_;
	struct Presented
	{
	 std::vector<Inkeys::UI::Bar::StatusIndicatorEntryLayout> entries; // 仅可命中项
	 Inkeys::UI::Bar::StatusIndicatorLayoutInput main; // 同帧主按钮几何：主按钮内部让给主按钮命中
	 double zoom=1;
	};
	mutable std::mutex presentedMutex_;
	Presented presented_;
	Inkeys::UI::UI3::ButtonPointerState pointer_;
	std::atomic<std::int64_t> deadlineTicks_{0};
	std::uint64_t hovered_=0,pressed_=0;
	struct HoverVisual
	{
		UiPctClass pct{0};UiColorClass fill{RGB(127,127,127)};
		IdtAtomic<ButtonHoverStageEnum> stage=ButtonHoverStageEnum::None;
		bool entered=false;
	};
	std::map<std::uint64_t,HoverVisual> hover_;
	std::map<std::uint64_t,UiValueClass> press_;
	std::array<UiSVGClass,7> icons_;
	std::map<std::uint64_t,UiWordClass> words_;
	std::vector<std::pair<std::uint64_t,double>> simulated_;
	Microsoft::WRL::ComPtr<ID2D1PathGeometry> ringGeometry_;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
	std::array<double,3> ringKey_{};
	unsigned long long deviceGeneration_=0;
	double zoom_=1,simulatedSampleAt_=0;
	Inkeys::UI::Bar::StatusIndicatorSpring orientation_;
	// 新版本轮询只在交互 worker 上推进（PollDeadline），下载中 10Hz、其余 1Hz。
	Inkeys::UI::Bar::StatusIndicatorUpdateTracker updateTracker_;
	std::chrono::steady_clock::time_point nextUpdatePoll_{};
	std::chrono::steady_clock::time_point PollUpdateSource(std::chrono::steady_clock::time_point now);
	bool belowTarget_=false,orientationInitialized_=false;
	bool initialized_=false,feedbackActive_=false,changed_=false;
	void DrawRing(UIRendering&, ID2D1DeviceContext*, const UiSuperellipseClass&, double material, double content);
	void DrawStroke(UIRendering&, ID2D1DeviceContext*, ID2D1Geometry*, COLORREF, double opacity, double material, int variant);
};

// 面板向 UI3 自动脏区登记成员时由宿主提供：当帧 zoom 与局部像素 → 宿主追踪坐标的映射（属性面板为 Rigid）。
struct BarPanelVisualContext
{
	std::function<double()> zoom;
	std::function<RECT(const RECT&)> map;
	// 写入映射的浮点参数（Rigid 平移、缩放）：整数包络相同的亚像素平移也要判定为变化。各面板根组采样时调用。
	std::function<void(Inkeys::UI::UI3::VisualStateWriter&)> mappingState;
};

class BarEraserAttributePanel
{
	friend class BarUISetClass; // 仅 render owner 在候选命中事务中读取当前 frame_。
public:
	bool Advance(BarUISetClass& owner, double dt, double speed, double zoom, UINT dpi,
		RECT workArea, POINT origin, double rigidX, double rigidY,
		const UiTimelineClass* parentTimeline = nullptr,
		bool dragPlacementLocked = false,
		std::optional<bool> eraserSelected = std::nullopt);
	void Draw(UIRendering& renderer, ID2D1DeviceContext* context);
	// UI3 自动脏区：准备阶段按绘制同一算法写好成员继承位置（幂等）；登记成员到面板登记表。
	void PrepareVisuals();
	bool RegisterVisuals(Inkeys::UI::UI3::VisualDirtyRegistry& registry, const BarPanelVisualContext& context);
	void CommitPresented();
	bool Pointer(BarUISetClass& owner, const ExMessage& message, bool cancelled = false, bool contactPointer = false, bool* accepted = nullptr);
	bool ResetPointerFeedback();
	void Close(BarUISetClass& owner);
	RECT Bounds() const;
	std::array<RECT,3> PresentedRegions() const;
	Inkeys::UI::Bar::EraserAttributePresentation PresentationSnapshot() const;
	bool Changed() const noexcept { return changed_; }
	bool Active() const noexcept { return active_; }
	bool IdleFadeProtected() const noexcept { return panelMotion_.Visible() || menuMotion_.Visible(); }
	void RememberEntryFromSelection(bool value) noexcept { returnToSelectionOnClear_ = value; }
private:
	void Initialize();
	bool Execute(BarUISetClass& owner, int item);
	void DrawPreview(UIRendering& renderer, ID2D1DeviceContext* context, size_t index);
	void UpdatePanelFrame(bool open, bool menuOpen);
	Panel panel_{"EraserAttribute"}, menuPanel_{"EraserSensitivity"};
	void ConfigureSurface(UiShapeClass& surface, Inkeys::UI::Bar::EraserAttributeRect rect, double scale = 1.0);
	mutable std::mutex presentationMutex_;
	Inkeys::UI::Bar::EraserAttributeLayout layout_;
	Inkeys::UI::Bar::EraserAttributePresentation frame_, presented_;
	Inkeys::UI::Bar::EraserAttributeLayoutInput stableLayoutInput_, dragLayoutInput_;
	double zoom_ = 1;
	bool initialized_ = false, changed_ = false, active_ = false;
	bool hasStableLayoutInput_ = false, dragPlacementLocked_ = false;
	bool releaseSwitchLayoutLocked_ = false, sideSwitchActive_ = false;
	double sideSwitchExpandDuration_ = 0;
	bool previousBelow_ = false, previousReversed_ = false;
	int menuSide_ = -1;
	int appliedHover_ = -1; // 仅渲染线程消费输入反馈并操作按钮动画。
	IdtAtomic<int> hovered_ = -1, pressed_ = -1;
	bool returnToSelectionOnClear_ = false;
	Inkeys::UI::Bar::EraserSurfaceMotion panelMotion_, menuMotion_;
	std::array<BarButtonClass,11> buttons_;
	std::array<UiShapeClass,2> dividers_;
	UiShapeClass automaticDivider_;
	UiSVGClass automaticArrow_;
	std::array<UiShapeClass,3> circles_, circleSelectionRings_;
	std::array<UiValueClass,3> circleSelection_, circleHover_, circlePress_;
	UiShapeClass surface_, menu_;
	UiWordClass title_;
	std::array<Microsoft::WRL::ComPtr<ID2D1Bitmap1>,3> previewBitmaps_;
	std::array<bool,3> previewBitmapAttempted_{};
	std::array<float,3> previewDiametersPx_{}, previewRasterHalfWidths_{};
	float previewDpiScale_ = 1;
	Inkeys::Drawing::Draw3::EraserShape previewShape_ = Inkeys::Drawing::Draw3::EraserShape::Circle;
	Inkeys::Drawing::Draw3::EraserCursorPattern previewPattern_ = Inkeys::Drawing::Draw3::EraserCursorPattern::Grips;
	Inkeys::Drawing::Draw3::EraserCursorTheme previewTheme_ = Inkeys::Drawing::Draw3::EraserCursorTheme::Light;
	unsigned long long deviceGeneration_ = 0;
	int selectedSize_ = 32, sensitivity_ = 1;
	bool palmEnabled_ = false;
	Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState automatic_ =
		Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::On;
};

// 撤回属性：单 70x70 Redo 按钮，开合/换边/命中语义与橡皮属性一致，无子菜单。
class BarRecallAttributePanel
{
	friend class BarUISetClass; // 与橡皮面板使用同一成功呈现边界。
public:
	bool Advance(BarUISetClass& owner, const Inkeys::UI::Bar::RecallChromeView& chrome,
		bool anchorVisible, double dt, double speed, double zoom, UINT dpi,
		RECT workArea, POINT origin, double rigidX, double rigidY,
		const UiTimelineClass* parentTimeline = nullptr,
		bool dragPlacementLocked = false);
	void Draw(UIRendering& renderer, ID2D1DeviceContext* context);
	// UI3 自动脏区：准备阶段按绘制同一算法写好成员继承位置（幂等）；登记成员到面板登记表。
	void PrepareVisuals();
	bool RegisterVisuals(Inkeys::UI::UI3::VisualDirtyRegistry& registry, const BarPanelVisualContext& context);
	void CommitPresented();
	bool Pointer(BarUISetClass& owner, const ExMessage& message, bool cancelled = false, bool contactPointer = false, bool* accepted = nullptr);
	bool ResetPointerFeedback();
	void Close(BarUISetClass& owner);
	RECT Bounds() const;
	std::array<RECT,3> PresentedRegions() const;
	Inkeys::UI::Bar::RecallAttributePresentation PresentationSnapshot() const;
	bool Changed() const noexcept { return changed_; }
	bool Active() const noexcept { return active_; }
	bool IdleFadeProtected() const noexcept { return panelMotion_.Visible(); }
private:
	void Initialize();
	bool Execute(BarUISetClass& owner, int item);
	void ConfigureSurface(UiShapeClass& surface, Inkeys::UI::Bar::RecallAttributeRect rect, double scale = 1.0);
	void UpdatePanelFrame(bool open);
	Panel panel_{"RecallAttribute"};
	mutable std::mutex presentationMutex_;
	Inkeys::UI::Bar::RecallAttributeLayout layout_;
	Inkeys::UI::Bar::RecallAttributePresentation frame_, presented_;
	Inkeys::UI::Bar::RecallAttributeLayoutInput stableLayoutInput_, dragLayoutInput_;
	double zoom_ = 1;
	bool initialized_ = false, changed_ = false, active_ = false;
	bool hasStableLayoutInput_ = false, dragPlacementLocked_ = false;
	bool releaseSwitchLayoutLocked_ = false, sideSwitchActive_ = false;
	double sideSwitchExpandDuration_ = 0;
	bool previousBelow_ = false;
	int appliedHover_ = -1; // 仅渲染线程消费输入反馈并操作按钮动画。
	IdtAtomic<int> hovered_ = -1, pressed_ = -1;
	Inkeys::UI::Bar::RecallSurfaceMotion panelMotion_;
	BarButtonClass redoButton_;
	UiShapeClass surface_;
	unsigned long long deviceGeneration_ = 0;
};


namespace Inkeys::UI::Bar
{
	// 隐藏按钮继续留在运行时列表，位置跟随来源按钮；渲染与离屏回归共用。
	void AnchorHiddenBarButton(BarButtonSetClass& buttons, bool folded,
		BarButtonPresetEnum hiddenPreset, BarButtonPresetEnum anchorPreset,
		const std::function<void(UiValueClass&, double)>& setPositionTarget);

	enum class BarButtonListLayoutKind { Main, GroupedGrid, SingleRow, GeometryGrid };
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
	[[nodiscard]] BarButtonListLayout ResolveBarButtonListLayout(
		const std::vector<std::shared_ptr<BarButtonClass>>& buttons,
		BarButtonListLayoutKind kind,
		const std::vector<std::shared_ptr<BarButtonClass>>& secondGroup = {});


	// PageControl 只登记窗口事实；第三光源仍由 Main Bar 的唯一状态机拥有。
	export void NotifyBorderCursorSurfacePointerEntered() noexcept;
	export void NotifyBorderCursorSurfacePointerLeft() noexcept;
	export void PublishBorderCursorSurfaceBounds(
		unsigned int index, const RECT& bounds, bool visible) noexcept;
}
namespace Inkeys::UI::Bar
{
	wstring ShapeToolIconResource(const draw3::shapes::ToolSpec& tool,
		std::optional<unsigned> carouselSides = std::nullopt);
	wstring ShapeToolLabel(draw3::shapes::PresetKind preset);
	wstring ShapeAuxiliaryLabel(draw3::shapes::Auxiliary auxiliary, draw3::shapes::PresetKind preset = draw3::shapes::PresetKind::Rectangle);
	wstring ShapePlanarConstraintLabel(draw3::shapes::PlanarConstraint constraint);
	bool ApplyBarThicknessPresetIfRevision(int preset, std::uint64_t revision, bool save = true);
	// 目录属于UI层；没有真实preset的未来工具只占位，不能进入业务状态或文档。
	inline constexpr int GeometryCategoryCount = 35, GeometryImplementedCategoryCount = 20;
	inline constexpr int GeometryCircleCenter = GeometryCategoryCount, GeometryCircleBox = GeometryCircleCenter + 1;
	inline constexpr int GeometrySidesBegin = GeometryCircleBox + 1, GeometrySidesCount = 12;
	inline constexpr int GeometryAuxBegin = GeometrySidesBegin + GeometrySidesCount;
	inline constexpr int GeometryTriangleModeBegin = GeometryAuxBegin + 3, GeometryTrapezoidMode = GeometryTriangleModeBegin + 3;
	inline constexpr int GeometryStyleBegin = GeometryTrapezoidMode + 1;
	inline constexpr int GeometryPatternBegin = GeometryStyleBegin + 3, GeometryThicknessBegin = GeometryPatternBegin + 4;
	inline constexpr int GeometryThicknessCount = 3, GeometryCloseButton = GeometryThicknessBegin + GeometryThicknessCount;
	inline constexpr int GeometrySegmentBegin = GeometryCloseButton + 1, GeometrySegmentCount = 3;
	inline constexpr int GeometryControlCount = GeometrySegmentBegin + GeometrySegmentCount;
	enum class ShapeToolbarCategory { Regular, Solid, CoordinateFunction };
	struct ShapeCategoryDescriptor
	{
		const wchar_t* key;
		const wchar_t* resource;
		const char* labelKey;
		const wchar_t* fallbackLabel;
		std::optional<draw3::shapes::PresetKind> preset;
		ShapeToolbarCategory category = ShapeToolbarCategory::Regular;
	};
	const std::array<ShapeCategoryDescriptor, GeometryCategoryCount>& ShapeCategoryCatalog();
	wstring ShapeCategoryLabel(size_t index);

	struct GeometryAttributePresentation
	{
		EraserAttributeRect panel, parameterClip, categoryClip, scrollTrack, scrollThumb, workArea;
		std::array<EraserAttributeRect,GeometryControlCount> items{};
		std::array<ButtonHitSnapshot,GeometryControlCount> hits{};
		EraserSurfacePose pose;
		double zoom = 1, opacity = 0;
		bool visible = false, acceptsInput = false, categoriesAcceptInput = false, below = false;
		double scrollOffset = 0, scrollMaximum = 0, categoryViewportHeight = 0, categoryContentHeight = 0;
		draw3::shapes::ToolSpec tool{};
		ShapeToolbarCategory category = ShapeToolbarCategory::Regular;
		std::array<wstring,GeometryControlCount> accessibleNames{};
	};
}

// 跨笔构造提示独占状态；沿用居中提示锚点和公共按钮，不覆盖原dock提示状态。
class BarConstructionPrompt
{
	friend class BarGeometryAttributeTestAccess;
	friend class BarUISetClass;
public:
	bool Advance(BarUISetClass&, std::shared_ptr<const Inkeys::Drawing::Draw3::ShapeUiSnapshot>, double dt, double speed,
		double zoom, RECT workArea, POINT origin, double rigidX, double rigidY, const UiTimelineClass* parentTimeline = nullptr);
	void Draw(UIRendering&, ID2D1DeviceContext*);
	void CommitPresented();
	bool Pointer(BarUISetClass&, const ExMessage&, bool cancelled = false, bool* accepted = nullptr);
	bool Visible() const noexcept { return visible_.load(); }
	bool Changed() const noexcept { return changed_; }
	RECT Bounds() const;
	std::array<RECT,1> PresentedRegions() const;
private:
	void Initialize();
	struct Frame
	{
		Inkeys::Drawing::Draw3::ShapeUiIdentity identity;
		Inkeys::UI::Bar::EraserAttributeRect panel;
		std::array<Inkeys::UI::UI3::ButtonHitSnapshot,4> buttons;
		std::array<std::wstring,4> accessibleNames, tooltips;
		Inkeys::UI::Bar::EraserAttributeRect visualPanel;
		double zoom = 1, opacity = 0, scale = 0;
		bool visible = false, input = false, ring = false;
	};
	Frame frame_, presented_;
	void UpdatePanelFrame(bool open);
	Panel panel_{"ConstructionPrompt"};
	// 离屏 fixture 可替换意图出口，以核对一次触摸只提交一次；产品走 SubmitShapeUiIntent。
	std::function<bool(const Inkeys::Drawing::Draw3::ShapeUiAction&)> submit_;
	// 指针意图出口，测试替换 submit_ 时可截获。
	bool Submit(const Inkeys::Drawing::Draw3::ShapeUiAction& action) const;
	mutable std::mutex mutex_;
	BarButtonListClass list_;
	std::array<std::shared_ptr<BarButtonClass>,4> buttons_;
	std::shared_ptr<BarButtonClass> divider_;
	std::array<UiValueClass,4> x_, y_;
	UiShapeClass surface_;
	UiWordClass label_, ringLabel_;
	UiValueClass opacity_{0}, ringProgress_{0}, textWidth_{0};
	UiValueClass left_, top_, width_, height_;
	UiTimelineClass layoutTimeline_;
	Inkeys::UI::Bar::BarBottomDockIndicatorContentPhase contentPhase_ = Inkeys::UI::Bar::BarBottomDockIndicatorContentPhase::Stable;
	std::wstring desiredLabel_, desiredRingLabel_;
	unsigned buttonState_ = ~0u;
	Inkeys::Drawing::Draw3::ShapeUiIdentity pressedIdentity_;
	IdtAtomic<int> hovered_ = -1, pressed_ = -1;
	int appliedHover_ = -1;
	IdtAtomic<bool> visible_ = false;
	draw3::shapes::ConstructionStatus status_;
	bool initialized_ = false, changed_ = false, textTransition_ = false;
};

// 图形面板仅保存拓扑和资源，按钮行为与动画交给共享 BarButton 运行时。
class BarGeometryAttributePanel
{
public:
	bool Advance(BarUISetClass& owner, const StateModeClass& state, double dt, double speed,
		double zoom, RECT workArea, POINT origin, double rigidX, double rigidY,
		const UiTimelineClass* parentTimeline = nullptr, bool dragPlacementLocked = false,
		std::chrono::steady_clock::time_point frameTime = std::chrono::steady_clock::now());
	void Draw(UIRendering& renderer, ID2D1DeviceContext* context);
	// UI3 自动脏区：准备阶段按绘制同一算法写好成员继承位置（幂等）；登记成员到面板登记表。
	void PrepareVisuals();
	bool RegisterVisuals(Inkeys::UI::UI3::VisualDirtyRegistry& registry, const BarPanelVisualContext& context);
	void CommitPresented();
	bool Pointer(BarUISetClass& owner, const ExMessage& message, bool cancelled = false, bool contactPointer = false, bool* accepted = nullptr);
	bool ResetPointerFeedback();
	void Close(BarUISetClass& owner);
	void StopCarousel() noexcept;
	std::chrono::steady_clock::time_point ConsumeCarouselWake(
		std::chrono::steady_clock::time_point now, bool& due);
	// 长按固定期限由交互 owner 消费：200ms 请求进度环帧，1000ms 提交选中并固定，不依赖帧率。
	std::chrono::steady_clock::time_point ConsumeHoldWake(BarUISetClass& owner,
		std::chrono::steady_clock::time_point now);
	RECT Bounds() const;
	std::array<RECT,1> PresentedRegions() const;
	Inkeys::UI::Bar::GeometryAttributePresentation PresentationSnapshot() const;
	bool IdleFadeProtected() const noexcept { return motion_.Visible(); }
	bool Changed() const noexcept { return changed_; }
	bool Active() const noexcept { return active_; }
private:
	friend class BarUISetClass;
	friend class BarGeometryAttributeTestAccess;
	void Initialize();
	void Execute(BarUISetClass& owner, int item, std::uint64_t revision, bool* accepted = nullptr);
	void UpdateCarousel(bool enabled, std::chrono::steady_clock::time_point now);
	void UpdatePanelFrame(bool open);
	Panel panel_{"GeometryAttribute"};
	bool CancelHold();
	mutable std::mutex presentationMutex_, carouselMutex_, holdMutex_;
	Inkeys::UI::Bar::GeometryAttributePresentation frame_, presented_;
	Inkeys::UI::Bar::EraserAttributeRect previousPanel_, previousAnchor_;
	Inkeys::UI::Bar::BarAttributePanelMotion motion_;
	std::array<std::shared_ptr<BarButtonClass>,Inkeys::UI::Bar::GeometryControlCount> buttons_;
	BarButtonListClass categoryList_, attributeList_;
	Inkeys::UI::Bar::BarButtonListLayout categoryLayout_, attributeLayout_;
	IdtAtomic<int> categoryRequest_ = 0;
	Inkeys::UI::Bar::ShapeToolbarCategory category_ = Inkeys::UI::Bar::ShapeToolbarCategory::Regular;
	std::array<UiValueClass,Inkeys::UI::Bar::GeometryCategoryCount> categoryX_, categoryY_;
	std::array<Inkeys::UI::Bar::EraserAttributeRect,Inkeys::UI::Bar::GeometryCategoryCount> categoryAnchors_{};
	std::array<Inkeys::UI::Bar::EraserAttributeRect,Inkeys::UI::Bar::GeometryStyleBegin-Inkeys::UI::Bar::GeometryCircleCenter> parameterAnchors_{};
	UiTimelineClass layoutTimeline_;
	std::array<std::shared_ptr<BarButtonClass>,2> attributeDividers_;
	std::array<UiValueClass,11> attributeX_;
	std::uint32_t attributeVisibility_ = ~0u;
	double attributeMaxWidth_ = 0;
	std::array<wstring,Inkeys::UI::Bar::GeometryControlCount> resourceTargets_, labelTargets_;
	std::array<UiShapeClass,3> dividers_;
	UiShapeClass auxiliaryHintSurface_;
	UiWordClass auxiliaryHint_;
	bool auxiliaryHintVisible_ = false;
	// 长按：输入 worker 写入，render 只读取进度；holdMutex_ 保护。
	int holdItem_ = -1;
	HoldProgressState holdProgress_;
	POINT holdOrigin_{};
	bool holdRingWake_ = false, holdCommitted_ = false;
	// 提交是否被工具状态接受；被拒（旧 revision 等）时进度环不定格满格。
	bool holdAccepted_ = false;
	// 调用方持有 holdMutex_：结束本次长按的全部状态。
	void ResetHoldLocked() noexcept { holdItem_ = -1; holdRingWake_ = holdCommitted_ = holdAccepted_ = false; }
	// 进度环由 render owner 推进与绘制；绘制失败后本次长按内不再尝试，下一次长按开始时重试。
	int ringItem_ = -1;
	double ringProgress_ = 0;
	double ringOpacity_ = 0;
	Inkeys::UI::UI3::ButtonFrameRing ring_;
	bool ringFailed_ = false, ringShown_ = false;
	UiShapeClass surface_, scrollRail_, scrollThumb_;
	UiValueClass width_{0}, height_{0}, viewportHeight_{0}, scroll_{0};
	// worker只发布滚动意图；共享动画值由唯一render owner推进。
	IdtAtomic<double> scrollRequest_ = 0;
	IdtAtomic<bool> scrollDragging_ = false;
	Inkeys::UI::Bar::GeometryAttributePresentation scrollGesture_;
	double scrollDragY_ = 0, scrollDragOffset_ = 0;
	std::uint64_t scrollRevision_ = 0;
	bool initialized_ = false, changed_ = false, active_ = false, previousBelow_ = false;
	bool sideSwitch_ = false, hadOpen_ = false, requestedOpen_ = false;
	int appliedHover_ = -1;
	double parameterOpacity_ = 0;
	IdtAtomic<int> hovered_ = -1, pressed_ = -1;
	std::uint64_t pressRevision_ = 0;
	IdtAtomic<bool> pressCancelled_ = false;
	std::chrono::steady_clock::time_point carouselDeadline_{};
	bool carouselEnabled_ = false, carouselArmed_ = false, carouselWakeRequested_ = false;
	unsigned carouselSides_ = 3;
	std::array<double,3> thicknessPreviewWidths_{};
	wstring measuredFontFamily_, measuredLocale_;
	unsigned long long measuredDeviceGeneration_ = 0;
	std::array<double,Inkeys::UI::Bar::GeometryCategoryCount> categoryFontSizes_{};
	std::array<double,2> circleTextWidths_{};
	std::array<double,10> optionTextWidths_{};
	unsigned long long deviceGeneration_ = 0;
};

// 控件枚举
enum class BarUISetShapeEnum : int
{
	MainBar,
	MorePanel,
	MorePanelDivider,
	MorePanelCloseHit,

	DrawAttributeBar,
	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,
	DrawAttributeBar_ColorSelect12,
	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_PenTypeExtensionHit,
	DrawAttributeBar_PenTypeExtensionDivider,
	DrawAttributeBar_ThicknessSelect,
	DrawAttributeBar_ThicknessDivider,
	DrawAttributeBar_ThicknessFine,
	DrawAttributeBar_ThicknessMedium,
	DrawAttributeBar_ThicknessCoarse,
	DrawAttributeBar_ThicknessAdjust,
	DrawAttributeBar_ThicknessSliderHit,
	DrawAttributeBar_ThicknessSliderThumb,
	DrawAttributeBar_ThicknessPreviewPopupSurface,
	DrawAttributeBar_ThicknessPreviewPopupCircle,
	DrawAttributeBar_ThicknessAnnotationInfoHit,
	DrawAttributeBar_ThicknessOverflowBadge,
	DrawAttributeBar_ThicknessOverflowInfoHit,
	DrawAttributeBar_ThicknessAnnotationPopup,
	DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
	DrawAttributeBar_ThicknessOverflowPopup,
	DrawAttributeBar_ThicknessOverflowPopupCloseHit,
	DrawAttributeBar_PenTypeMenu,
	DrawAttributeBar_PenTypeMenuFreeLine,
	DrawAttributeBar_PenTypeMenuAnnotationLine,
	// 内容色与反馈分层；连续编号供预设色块按原顺序借用。
	DrawAttributeBar_ColorSelect1Feedback,
	DrawAttributeBar_ColorSelect2Feedback,
	DrawAttributeBar_ColorSelect3Feedback,
	DrawAttributeBar_ColorSelect4Feedback,
	DrawAttributeBar_ColorSelect5Feedback,
	DrawAttributeBar_ColorSelect6Feedback,
	DrawAttributeBar_ColorSelect7Feedback,
	DrawAttributeBar_ColorSelect8Feedback,
	DrawAttributeBar_ColorSelect9Feedback,
	DrawAttributeBar_ColorSelect10Feedback,
	DrawAttributeBar_ColorSelect11Feedback,
	DrawAttributeBar_ColorSelect12Feedback,
	DrawAttributeBar_ThicknessAnnotationInfoFeedback,
	DrawAttributeBar_ThicknessOverflowInfoFeedback,
	DrawAttributeBar_ColorSelect12Inner,

	GeometryAttributeBar,
	GeometryAttributeBar_Divider,
	GeometryAttributeBar_StraightLine,
	GeometryAttributeBar_Rectangle,
	GeometryAttributeBar_ThicknessFine,
	GeometryAttributeBar_ThicknessMedium,
	GeometryAttributeBar_ThicknessCoarse,
	GeometryAttributeBar_Close,
};
enum class BarUISetSuperellipseEnum : int
{
	MainButton,
};
enum class BarUISetSvgEnum : int
{
	logo1,
	// 着色层已并入 logo1 外观；保留序号供 UI3 SVG 诊断探针稳定映射，不再创建对象。
	logoInk,
	MorePanelClose,

	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_PenTypeExtensionArrow,
	DrawAttributeBar_ThicknessAdjust,
	DrawAttributeBar_PenTypeMenuCheck,
	DrawAttributeBar_ThicknessAnnotationInfo,
	DrawAttributeBar_ThicknessOverflowInfo,
	DrawAttributeBar_ThicknessAnnotationPopupClose,
	DrawAttributeBar_ThicknessOverflowPopupClose,
	DrawAttributeBar_ColorSelect12Check,
	// 取色器亮暗图标已归 UI3 取色器组件自持；SVG 有限探针以枚举序号作标签且上限固定，
	// 这两个序号保留为占位，后续项的序号不得移动。
	ReservedSvgOrdinal18,
	ReservedSvgOrdinal19,
	GeometryAttributeBar_StraightLine,
	GeometryAttributeBar_Rectangle,
	GeometryAttributeBar_Close,
	StatusSlideshow,
	StatusPin,
	StatusNewVersion,
	StatusStar,
	StatusBell,
	StatusInfo,
	StatusBolt,
};
enum class BarUISetPngEnum : int
{
	DrawAttributeBar_ColorSelect12Wheel,
};
enum class BarUISetWordEnum : int
{
	BackgroundWarning,

	MainButton,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_ThicknessDisplay,
	DrawAttributeBar_ThicknessFineNumber,
	DrawAttributeBar_ThicknessMediumNumber,
	DrawAttributeBar_ThicknessCoarseNumber,
	DrawAttributeBar_PenTypeMenuFreeLine,
	DrawAttributeBar_ThicknessAnnotationLabel,
		DrawAttributeBar_ThicknessHoldLockLabel,
		DrawAttributeBar_ThicknessAnnotationPopupText,
		DrawAttributeBar_ThicknessAnnotationPopupBody,
		DrawAttributeBar_ThicknessOverflowPopupText,
		DrawAttributeBar_ThicknessOverflowPopupBody,
		DrawAttributeBar_ThicknessPreviewPopupNumber,

		GeometryAttributeBar_StraightLine,
		GeometryAttributeBar_Rectangle,
		GeometryAttributeBar_ThicknessFineNumber,
		GeometryAttributeBar_ThicknessMediumNumber,
		GeometryAttributeBar_ThicknessCoarseNumber,
	};

enum class BarBorderCursorTrackingStateEnum : int
{
	Dormant,
	Inside,
	Grace,
};

enum class BarDirectWindowDragPhase : std::uint8_t
{
	Idle,
	Dragging,
	Absorbing,
};

struct BarSeekResult
{
	double rawPathLength = 0.0;
	bool moved = false;
	bool captured = false;
	bool detached = false;
	bool modeChanged = false;
	bool allowClick = true;
};

struct BarPendingDisplaySnapshot
{
	RECT bounds{ 0, 0, 1, 1 };
	RECT workArea{ 0, 0, 1, 1 };
	UINT dpi = USER_DEFAULT_SCREEN_DPI;
	unsigned long long serial = 0;
	double uiBaseScale = 1.0;
	double desktopDockLine = 1.0;
	bool protectBottomEdge = false;
};

struct BarBottomDockPresentedSnapshot
{
	Inkeys::UI::Bar::BarBottomDockMode mode =
		Inkeys::UI::Bar::BarBottomDockMode::BottomDocked;
	Inkeys::UI::Bar::BarBottomDockPhase phase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	bool recoveryActive = false;
	Inkeys::UI::Bar::BarBottomDockVerticalMapping mapping{};
	Inkeys::UI::Bar::BarBottomDockCenterMode centerMode =
		Inkeys::UI::Bar::BarBottomDockCenterMode::Centered;
	Inkeys::UI::Bar::BarBottomDockPhase centerPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	Inkeys::UI::Bar::BarBottomDockHorizontalMapping horizontalMapping{};
	double centerElasticOffsetDip = 0.0;
	double elasticOffsetDip = 0.0;
	double rigidTranslationDip = 0.0;
	double mainButtonVisibleHeightDip = 81.0;
	bool dragActive = false; // 上次完整成功帧是否仍按住，用于松手形状交接。
	unsigned long long dragSession = 0;
	double zoom = 1.0;
		POINT monitorOrigin{};
		RECT monitorBounds{ 0, 0, 1, 1 };
		RECT workArea{ 0, 0, 1, 1 };
		UINT dpi = USER_DEFAULT_SCREEN_DPI;
		unsigned long long displaySerial = 0;
	double desktopDockLine = 1.0;
	bool protectBottomEdge = false;
	POINT directTranslation{};
	double mainCenterScreenX = 0.0;
	double mainCenterScreenY = 0.0;
	double rawMainCenterScreenX = 0.0;
	double bodyCenterScreenX = 0.0;
	double visualBodyCenterScreenX = 0.0;
	double rawBodyCenterScreenX = 0.0;
	bool indicatorVisible = false;
	bool indicatorOccluding = false;
	RECT indicatorBounds{};
	unsigned long long transitionSerial = 0;
	unsigned long long serial = 0;
};

enum class BarBorderPrimaryAnchorEnum : int
{
	MainButton,
	Select,
	Draw,
	Eraser,
	Geometry,
	Pierce,
};

// 主栏只发布一次当前帧的光源输入；独立目标不需要构造主栏 owner。
export struct BarUiMainLightingInput
{
	std::array<D2D1_POINT_2F, 6> anchors{};
	std::array<bool, 6> anchorAvailable{};
	D2D1_POINT_2F cursorScreenPoint{};
	unsigned long long cursorSerial = 0;
	bool cursorAvailable = false;
};
export class BarUISetClass;
UIRenderingServices MakeBarLocalizedTypographyServices();
UIRenderingServices MakeMainBarRenderingServices(BarUISetClass* owner);
BarUiMainLightingInput ReadMainBarLightingInput(BarUISetClass* owner);
UiFrameLightingInput ResolveMainBarFrameLightingInput(BarUISetClass* owner,
	int drawingMode, int penMode, COLORREF brush1Color, COLORREF highlighterColor);

// 编号只标识产品面板；绘制层级与输入优先级由 panelStack 分别登记。
enum class BarPanelId : PanelId
{
	Main, More, ColorPicker, AnnotationTooltip, OverflowTooltip, BottomDockIndicator,
	DrawAttribute, GeometryAttribute, EraserAttribute, EraserMenu, RecallAttribute, ConstructionPrompt
};
constexpr std::uint32_t BarPanelMask(BarPanelId id) noexcept
{ return 1u << static_cast<PanelId>(id); }

namespace Inkeys::UI::Bar
{
	// --ui3-input-worker-test：共享输入邮箱/派发/续接/期限/取消的生产回归，需访问主栏内部状态。
	export int RunSharedInteractionWorkerTests() noexcept;
}

// UI 总集
export class BarUISetClass
{
	friend int Inkeys::UI::Bar::RunSharedInteractionWorkerTests() noexcept;
public:
	BarUISetClass() : spec(MakeMainBarRenderingServices(this))
	{
		colorPicker.SetCloseRequest([this] { CloseColorPicker(false); });
	};

	// 渲染
	[[nodiscard]] bool Rendering();
	void StopRendering();
	// 鼠标交互
	void Interact();
	// 仅交互 owner 调用，顶层与嵌套等待共享有限批量的 Hook/策略消费。
	void PollGlobalMouseInput();
	// 仅合并已经判定为展开/收起的动作，不拦截状态切换和原始输入。
	bool TryBeginToggle(Inkeys::UI::Bar::BarToggleChannel channel)
	{
		return toggleClickCoalescer.TryBegin(channel);
	}
	void RequestBottomDockCenterAfterExpand() noexcept
	{
		bottomDockCenterAutoCaptureRequested.store(true, memory_order_release);
	}
	void ClearBottomDockCenterForFold() noexcept
	{
		BeginBottomDockTransition();
		bottomDockCenterMode.store(
			Inkeys::UI::Bar::BarBottomDockCenterMode::Free,
			memory_order_relaxed);
		bottomDockCenterPhase.store(
			Inkeys::UI::Bar::BarBottomDockPhase::Stable,
			memory_order_relaxed);
		bottomDockCenterElasticOffsetDip.store(0.0, memory_order_relaxed);
		(void)FinishBottomDockTransition(true);
	}

public:
	BarWindowPosClass barWindow;
	BarMediaClass barMedia;
	BarButtonSetClass barButtonSet;
	UIRendering spec;
	// 主栏帧目标：拥有设备上下文、GDI 兼容目标位图与帧生命周期，绑定 spec。
	FrameTarget specTarget{spec};
	LightingStateMachine lightingStateMachine;
	bool PrepareFrameLighting(double animationDtSeconds, POINT layoutScreenOrigin,
		const UiFrameLightingInput& input);
	BarPenPreviewRenderer penPreviewRendering;
	std::function<BarUiMainLightingInput()> mainLightingInput = [this] { return ReadMainBarLightingInput(this); };
	// 字体派生缓存仍在原策略/语言清理点同步失效。
	void ConfigureLocalizedTypography()
	{ spec.ConfigureLocalizedTypography(); penPreviewRendering.ResetTypographyCaches(); }
	bool RefreshLocalizedTypography()
	{
		if (!spec.RefreshLocalizedTypography()) return false;
		penPreviewRendering.ResetTypographyCaches(); return true;
	}
	bool UpdateFontRenderingPolicy(bool allowFractional) noexcept
	{
		if (!spec.UpdateFontRenderingPolicy(allowFractional)) return false;
		penPreviewRendering.ResetTypographyCaches(); return true;
	}
	bool HitTestPresentedButton(const BarButtonClass& button,
		double layoutXDip, double layoutYDip) const;
	bool HitTestPresentedShape(const UiShapeClass& shape,
		double layoutXDip, double layoutYDip) const;
	bool HitTestPresentedShape(const UiSuperellipseClass& shape,
		double layoutXDip, double layoutYDip) const;
	const Panel* PanelForShape(const UiShapeClass& shape) const;

	bool InitializeStartupPlacement(Inkeys::UI::StartupPlacement placement) noexcept;
	// 启动前写入后只读，渲染线程仅在首次完整布局时应用一次。
	Inkeys::UI::StartupPlacement startupPlacement =
		Inkeys::UI::StartupPlacement::CenteredBottomBar;
	BarStateClass barState;
	BarStyleClass barStyle;
	BarGeometryAttributePanel geometryAttribute;
	BarConstructionPrompt constructionPrompt;
	BarEraserAttributePanel eraserAttribute;
	BarStatusIndicatorPanel statusIndicator;
	BarRecallAttributePanel recallAttribute;
	Panel mainPanel{"Main"}, morePanel{"More"}, colorPickerPanel{"MainColorPicker"};
	Panel annotationTooltipPanel{"AnnotationTooltip"}, overflowTooltipPanel{"OverflowTooltip"};
	Panel bottomDockIndicatorPanel{"BottomDockIndicator"}, drawAttributePanel{"DrawAttribute"};
	PanelStack panelStack{
		{static_cast<PanelId>(BarPanelId::DrawAttribute), &drawAttributePanel, 0, 0},
		{static_cast<PanelId>(BarPanelId::GeometryAttribute), &geometryAttribute.panel_, 1, 80},
		{static_cast<PanelId>(BarPanelId::EraserAttribute), &eraserAttribute.panel_, 2, 70},
		{static_cast<PanelId>(BarPanelId::EraserMenu), &eraserAttribute.menuPanel_, 3, 71},
		{static_cast<PanelId>(BarPanelId::RecallAttribute), &recallAttribute.panel_, 4, 60},
		{static_cast<PanelId>(BarPanelId::More), &morePanel, 5, 20},
		{static_cast<PanelId>(BarPanelId::Main), &mainPanel, 6, 10},
		{static_cast<PanelId>(BarPanelId::AnnotationTooltip), &annotationTooltipPanel, 8, 40},
		{static_cast<PanelId>(BarPanelId::OverflowTooltip), &overflowTooltipPanel, 7, 30},
		{static_cast<PanelId>(BarPanelId::ColorPicker), &colorPickerPanel, 9, 50},
		{static_cast<PanelId>(BarPanelId::ConstructionPrompt), &constructionPrompt.panel_, 10, 90},
		{static_cast<PanelId>(BarPanelId::BottomDockIndicator), &bottomDockIndicatorPanel, 11, 100}
	};
	// mask 由原 PointerStage 的可用性门控产生；几何统一查询已呈现帧。
	std::optional<PanelId> FrontPresentedPanel(double x, double y, std::uint32_t mask) const;
	bool IsPresentedPanelAt(BarPanelId id, double x, double y) const
	{ return FrontPresentedPanel(x, y, BarPanelMask(id)).has_value(); }
	// 已复制的成员与面板范围来自同一把快照锁；捕获中的手势继续沿用这份范围。
	bool PanelOwnsPoint(BarPanelId id, const PanelFrame& frame, double x, double y) const
	{
		return panelStack.FrontPanelIf([&](const PanelStackEntry& entry) {
			return entry.id == static_cast<PanelId>(id) && frame.OwnsPoint(x, y);
		}).has_value();
	}
	// 锁存布局和保护期；命令可用性读取 Draw3 真值，不写入按钮配置。
	std::mutex recallChromeMutex;
	Inkeys::UI::Bar::RecallChromeState recallChrome{};
	// 本次统一投影发布的view供布局颜色和弹窗共用，不在getter再次拼实时输入。
	Inkeys::UI::Bar::RecallChromeView recallChromePresentation{};
	// 用墙钟期限，不累加动画 dt。主栏休眠时动画时钟会停，并且恢复后的第一帧 dt 为 0。
	std::chrono::steady_clock::time_point recallChromeIdleDeadline{};
	bool recallChromeIdleArmed = false;

	ankerl::unordered_dense::map<BarUISetShapeEnum, shared_ptr<UiShapeClass>> shapeMap;
	ankerl::unordered_dense::map<BarUISetSuperellipseEnum, shared_ptr<UiSuperellipseClass>> superellipseMap;
	ankerl::unordered_dense::map<BarUISetSvgEnum, shared_ptr<UiSVGClass>> svgMap;
	// 主 Logo 的产品快照留在主栏；图元只接收加工回调与递增外观键。
	std::optional<BarLogoAppearance::Snapshot> logoAppearanceSnapshot;
	std::uint64_t logoAppearanceRevision = 0;
	void ConfigureLogoAppearance(UiSVGClass& svg, const BarLogoAppearance::Snapshot& appearance)
	{
		logoAppearanceSnapshot = appearance;
		svg.documentPreprocess = [appearance](const std::function<bool(const char*, const char*, const std::string&)>& setAttribute)
		{
			bool complete = true;
			BarLogoAppearance::ApplyAttributes(appearance,
				[&](const char* id, const char* name, const std::string& value)
				{
					if (!setAttribute(id, name, value)) complete = false;
				});
			return complete;
		};
		svg.appearanceKey = ++logoAppearanceRevision;
	}
	ankerl::unordered_dense::map<BarUISetPngEnum, shared_ptr<UiPNGClass>> pngMap;
	ankerl::unordered_dense::map<BarUISetWordEnum, shared_ptr<UiWordClass>> wordMap;

	struct FixedButtonFeedback
	{
		IdtAtomic<bool> pressed = false;
		IdtAtomic<ButtonHoverStageEnum> hoverStage = ButtonHoverStageEnum::None;
		UiValueClass pressScale = UiValueClass(1.0);
	};
	std::array<FixedButtonFeedback, 11> drawAttributeSwatchFeedback;
	std::array<FixedButtonFeedback, 2> drawAttributeInfoFeedback;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeColorPickerEntryHoverStage = ButtonHoverStageEnum::None;

	// 绘制属性按钮同样复用自身背景层，仅单独记录悬停动画阶段。
	IdtAtomic<ButtonHoverStageEnum> drawAttributeBrushHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeSoftPenHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeLaserHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeHighlightHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributePenTypeExtensionHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributePenTypeFreeLineHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeThicknessFineHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeThicknessMediumHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeThicknessCoarseHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeThicknessAdjustHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeAnnotationCloseHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> drawAttributeOverflowCloseHoverStage = ButtonHoverStageEnum::None;
	ColorPicker colorPicker;
	IdtAtomic<ButtonHoverStageEnum> moreCloseHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> geometryStraightLineHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> geometryRectangleHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> geometryThicknessFineHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> geometryThicknessMediumHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> geometryThicknessCoarseHoverStage = ButtonHoverStageEnum::None;
	IdtAtomic<ButtonHoverStageEnum> geometryCloseHoverStage = ButtonHoverStageEnum::None;

public:
	// 渲染更新：状态更新 + 通知计算并渲染
	void UpdateRendering(bool updateState = true);
	Inkeys::UI::UI3::Ui3FiniteSignature ReadFiniteSignature(const StateModeVersionedSnapshot& tool);
	// 工具或白板工作区切换时收起所有属性浮层，主栏本体保持不变。
	void CollapseAuxiliaryPanels(bool cancelCapture = true);
	// 主栏点击展开对应 2×2；关闭只收面板，布局等 3 秒空闲后调整。
	void OpenRecallFromUndo();
	void OpenRecallFromRedo();
	void NoteRecallPopupRedo();
	bool ActivateRecallPopupCommand();
	void NoteRecallPopupDismissed();
	void ClearRecallForDrawing();
	void SettleRecallChromeForWorkspace();
	bool AdvanceRecallChromeIdle(double dtSeconds);
	Inkeys::UI::Bar::RecallChromeView RecallChromeView();
	Inkeys::UI::Bar::RecallChromeView RecallChromeView(bool compactSelection);
	void NoteRecallHistoryAvailability(bool canUndo, bool canRedo);
	void ArmRecallChromeIdleTimer() noexcept;
	void DisarmRecallChromeIdleTimer() noexcept;
	void ArmRecallChromeIdleDeadline();
	// 到期返回 true 并唤醒渲染；未到期时 retryMs 为剩余时间。
	bool PollRecallChromeIdleTimer(unsigned& retryMs);
	void StartDisplayTracking();
	void StopDisplayTracking() noexcept;
	void PublishDisplaySnapshot(Inkeys::Display::SnapshotPtr snapshot) noexcept;
	void PublishWindowDpi(UINT dpi) noexcept;
	BarPendingDisplaySnapshot PendingDisplaySnapshot() const noexcept
	{
		for (;;)
		{
			const auto serialBefore = pendingDisplaySerial.load(
				memory_order_acquire);
			if ((serialBefore & 1ULL) != 0) continue;
			BarPendingDisplaySnapshot snapshot{
				RECT{
					pendingDisplayLeft.load(memory_order_relaxed),
					pendingDisplayTop.load(memory_order_relaxed),
					pendingDisplayRight.load(memory_order_relaxed),
					pendingDisplayBottom.load(memory_order_relaxed) },
				RECT{
					pendingWorkAreaLeft.load(memory_order_relaxed),
					pendingWorkAreaTop.load(memory_order_relaxed),
					pendingWorkAreaRight.load(memory_order_relaxed),
					pendingWorkAreaBottom.load(memory_order_relaxed) },
				pendingDisplayDpi.load(memory_order_relaxed),
				serialBefore,
				pendingDisplayUiBaseScale.load(memory_order_relaxed),
				pendingDesktopDockLine.load(memory_order_relaxed),
				pendingProtectBottomEdge.load(memory_order_relaxed) };
			std::atomic_thread_fence(memory_order_acquire);
			if (pendingDisplaySerial.load(memory_order_acquire) == serialBefore)
				return snapshot;
		}
	}
	POINT PresentedMonitorOrigin() const noexcept
	{
		return BottomDockPresentedSnapshot().monitorOrigin;
	}
	POINT DirectWindowPresentedTranslation(bool ignoreWhileDragging = false) const noexcept
	{
		if (ignoreWhileDragging
			&& directWindowDragPhase.load(memory_order_acquire)
				== BarDirectWindowDragPhase::Dragging)
			return {};
		return BottomDockPresentedSnapshot().directTranslation;
	}
	bool IsBottomDockLayoutLocked() const noexcept
	{
		return bottomDockMode.load(memory_order_acquire)
			== Inkeys::UI::Bar::BarBottomDockMode::BottomDocked
			|| bottomDockRecoveryActive.load(memory_order_acquire);
	}
	BarBottomDockPresentedSnapshot BottomDockPresentedSnapshot() const noexcept
	{
		for (;;)
		{
			const auto serialBefore = bottomDockPresentedMappingSerial.load(
				memory_order_acquire);
			if ((serialBefore & 1ULL) != 0) continue;
			BarBottomDockPresentedSnapshot snapshot{};
			snapshot.mode = bottomDockPresentedMode.load(memory_order_relaxed);
			snapshot.phase = bottomDockPresentedPhase.load(memory_order_relaxed);
			snapshot.recoveryActive =
				bottomDockPresentedRecoveryActive.load(memory_order_relaxed);
			snapshot.mapping.baseTopDip = bottomDockPresentedBaseTopDip.load(
				memory_order_relaxed);
			snapshot.mapping.baseBottomDip =
				bottomDockPresentedBaseBottomDip.load(memory_order_relaxed);
			snapshot.mapping.visualTopDip = bottomDockPresentedVisualTopDip.load(
				memory_order_relaxed);
			snapshot.mapping.visualBottomDip =
				bottomDockPresentedVisualBottomDip.load(memory_order_relaxed);
			snapshot.mapping.scaleY = max(0.000001,
				bottomDockPresentedScaleY.load(memory_order_relaxed));
			snapshot.mapping.rigidGripYDip =
				bottomDockPresentedRigidGripYDip.load(memory_order_relaxed);
			snapshot.elasticOffsetDip = bottomDockPresentedElasticOffsetDip.load(
				memory_order_relaxed);
			snapshot.rigidTranslationDip =
				bottomDockPresentedRigidTranslationDip.load(memory_order_relaxed);
			snapshot.mainButtonVisibleHeightDip =
				bottomDockPresentedMainButtonVisibleHeightDip.load(memory_order_relaxed);
			snapshot.dragActive =
				bottomDockPresentedDragActive.load(memory_order_relaxed);
			snapshot.dragSession = bottomDockPresentedDragSession.load(memory_order_relaxed);
			snapshot.mapping.rigidOverlayTranslationYDip =
				snapshot.rigidTranslationDip;
			snapshot.centerMode = bottomDockPresentedCenterMode.load(
				memory_order_relaxed);
			snapshot.centerPhase = bottomDockPresentedCenterPhase.load(
				memory_order_relaxed);
			snapshot.horizontalMapping.baseLeftDip =
				bottomDockPresentedBaseLeftDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.baseRightDip =
				bottomDockPresentedBaseRightDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.visualLeftDip =
				bottomDockPresentedVisualLeftDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.visualRightDip =
				bottomDockPresentedVisualRightDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.scaleX = max(0.000001,
				bottomDockPresentedScaleX.load(memory_order_relaxed));
			snapshot.horizontalMapping.rigidGripTranslationXDip =
				bottomDockPresentedRigidGripTranslationXDip.load(
					memory_order_relaxed);
			snapshot.horizontalMapping.rigidOverlayTranslationXDip =
				bottomDockPresentedRigidTranslationXDip.load(
					memory_order_relaxed);
			snapshot.centerElasticOffsetDip =
				bottomDockPresentedCenterElasticOffsetDip.load(memory_order_relaxed);
			snapshot.zoom = Inkeys::UI::Bar::NormalizeBarBottomDockZoom(
				bottomDockPresentedZoom.load(memory_order_relaxed));
			snapshot.monitorOrigin = POINT{
				presentedMonitorOriginX.load(memory_order_relaxed),
				presentedMonitorOriginY.load(memory_order_relaxed) };
			snapshot.monitorBounds = RECT{
				bottomDockPresentedDisplayLeft.load(memory_order_relaxed),
				bottomDockPresentedDisplayTop.load(memory_order_relaxed),
				bottomDockPresentedDisplayRight.load(memory_order_relaxed),
				bottomDockPresentedDisplayBottom.load(memory_order_relaxed) };
				snapshot.workArea = RECT{
					bottomDockPresentedWorkAreaLeft.load(memory_order_relaxed),
					bottomDockPresentedWorkAreaTop.load(memory_order_relaxed),
					bottomDockPresentedWorkAreaRight.load(memory_order_relaxed),
					bottomDockPresentedWorkAreaBottom.load(memory_order_relaxed) };
				snapshot.dpi = bottomDockPresentedDisplayDpi.load(memory_order_relaxed);
				snapshot.displaySerial =
				bottomDockPresentedDisplaySerial.load(memory_order_relaxed);
			snapshot.desktopDockLine = bottomDockPresentedDesktopDockLine.load(memory_order_relaxed);
			snapshot.protectBottomEdge = bottomDockPresentedProtectBottomEdge.load(memory_order_relaxed);
			snapshot.directTranslation = POINT{
				bottomDockPresentedDirectTranslationX.load(memory_order_relaxed),
				bottomDockPresentedDirectTranslationY.load(memory_order_relaxed) };
			snapshot.mainCenterScreenX =
				bottomDockPresentedMainCenterScreenX.load(memory_order_relaxed);
			snapshot.mainCenterScreenY =
				bottomDockPresentedMainCenterScreenY.load(memory_order_relaxed);
			snapshot.rawMainCenterScreenX =
				bottomDockPresentedRawMainCenterScreenX.load(memory_order_relaxed);
			snapshot.bodyCenterScreenX =
				bottomDockPresentedBodyCenterScreenX.load(memory_order_relaxed);
			snapshot.visualBodyCenterScreenX =
				bottomDockPresentedVisualBodyCenterScreenX.load(
					memory_order_relaxed);
			snapshot.rawBodyCenterScreenX =
				bottomDockPresentedRawBodyCenterScreenX.load(memory_order_relaxed);
			{
				std::lock_guard lock(presentedButtonHitsMutex_);
				const auto& indicator = bottomDockIndicatorPanel.Presented();
				snapshot.indicatorVisible = indicator.visible;
				snapshot.indicatorOccluding = indicator.visible && indicator.blocksInput;
				const auto& bounds = indicator.interaction.bounds;
				snapshot.indicatorBounds = RECT{static_cast<LONG>(bounds.left), static_cast<LONG>(bounds.top),
					static_cast<LONG>(bounds.right), static_cast<LONG>(bounds.bottom)};
			}
			snapshot.transitionSerial =
				bottomDockPresentedTransitionSerial.load(memory_order_relaxed);
			snapshot.serial = serialBefore;
			std::atomic_thread_fence(std::memory_order_acquire);
			if (bottomDockPresentedMappingSerial.load(memory_order_acquire)
				== serialBefore) return snapshot;
		}
	}
	int BottomDockRigidHitTestY(int visualY) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(visualY)
			- snapshot.rigidTranslationDip * snapshot.zoom));
	}
	POINT BottomDockRigidHitTestPoint(POINT visualPoint) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const auto resolved = Inkeys::UI::Bar::
			ResolveBarBottomDockRigidHitTestPoint(
				visualPoint.x, visualPoint.y, snapshot.horizontalMapping,
				snapshot.mapping, snapshot.zoom);
		return POINT{ static_cast<LONG>(lround(resolved.logicalX)),
			static_cast<LONG>(lround(resolved.logicalY)) };
	}
	int BottomDockRigidVisualY(int rigidY) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(rigidY)
			+ snapshot.rigidTranslationDip * snapshot.zoom));
	}
	int BottomDockBodyHitTestY(int visualY) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelY(
				static_cast<double>(visualY), snapshot.mapping, snapshot.zoom)));
	}
	int BottomDockRigidHitTestX(int visualX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(visualX)
			- snapshot.horizontalMapping.rigidOverlayTranslationXDip
				* snapshot.zoom));
	}
	int BottomDockRigidVisualX(int rigidX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(rigidX)
			+ snapshot.horizontalMapping.rigidOverlayTranslationXDip
				* snapshot.zoom));
	}
	int BottomDockBodyHitTestX(int visualX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelX(
				static_cast<double>(visualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockGripHitTestX(int visualX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockGripPixelX(
				static_cast<double>(visualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockGripHitTestXFromRigid(int rigidX,
		int* visualX = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const int resolvedVisualX = static_cast<int>(lround(
			static_cast<double>(rigidX)
				+ snapshot.horizontalMapping.rigidOverlayTranslationXDip
					* snapshot.zoom));
		if (visualX) *visualX = resolvedVisualX;
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockGripPixelX(
				static_cast<double>(resolvedVisualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockBodyHitTestXFromRigid(int rigidX,
		int* visualX = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const int resolvedVisualX = static_cast<int>(lround(
			static_cast<double>(rigidX)
				+ snapshot.horizontalMapping.rigidOverlayTranslationXDip
					* snapshot.zoom));
		if (visualX) *visualX = resolvedVisualX;
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelX(
				static_cast<double>(resolvedVisualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockBodyHitTestYFromRigid(int rigidY,
		int* visualY = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const int resolvedVisualY = static_cast<int>(lround(
			static_cast<double>(rigidY)
				+ snapshot.rigidTranslationDip * snapshot.zoom));
		if (visualY) *visualY = resolvedVisualY;
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelY(
				static_cast<double>(resolvedVisualY), snapshot.mapping,
				snapshot.zoom)));
	}
	POINT BottomDockBodyHitTestPointFromRigid(POINT rigidPoint,
		POINT* visualPoint = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const auto resolved = Inkeys::UI::Bar::
			ResolveBarBottomDockBodyHitTestPointFromRigid(
				rigidPoint.x, rigidPoint.y, snapshot.horizontalMapping,
				snapshot.mapping, snapshot.zoom);
		if (visualPoint)
			*visualPoint = POINT{ static_cast<LONG>(lround(resolved.visualX)),
				static_cast<LONG>(lround(resolved.visualY)) };
		return POINT{ static_cast<LONG>(lround(resolved.logicalX)),
			static_cast<LONG>(lround(resolved.logicalY)) };
	}
	POINT BottomDockGripHitTestPointFromRigid(POINT rigidPoint,
		POINT* visualPoint = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const auto resolved = Inkeys::UI::Bar::
			ResolveBarBottomDockGripHitTestPointFromRigid(
				rigidPoint.x, rigidPoint.y, snapshot.horizontalMapping,
				snapshot.mapping, snapshot.zoom);
		if (visualPoint)
			*visualPoint = POINT{ static_cast<LONG>(lround(resolved.visualX)),
				static_cast<LONG>(lround(resolved.visualY)) };
		return POINT{ static_cast<LONG>(lround(resolved.logicalX)),
			static_cast<LONG>(lround(resolved.logicalY)) };
	}
	RECT BottomDockBodyVisualBounds(RECT bounds) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return Inkeys::UI::Bar::TransformBarBottomDockBodyRect(
			bounds, snapshot.horizontalMapping, snapshot.mapping, snapshot.zoom);
	}
	RECT BottomDockGripVisualBounds(RECT bounds) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return Inkeys::UI::Bar::TransformBarBottomDockGripRect(
			bounds, snapshot.horizontalMapping, snapshot.mapping, snapshot.zoom);
	}
	RECT BottomDockRigidVisualBounds(RECT bounds) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return Inkeys::UI::Bar::TranslateBarBottomDockRigidRect(bounds,
			snapshot.horizontalMapping.rigidOverlayTranslationXDip,
			snapshot.rigidTranslationDip, snapshot.zoom);
	}
	bool IsBottomDockIndicatorPresentedAt(LONG x, LONG y) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		PanelFrame frame;
		frame.visible = snapshot.indicatorVisible && snapshot.indicatorOccluding;
		frame.interaction.bounds = {static_cast<double>(snapshot.indicatorBounds.left), static_cast<double>(snapshot.indicatorBounds.top),
			static_cast<double>(snapshot.indicatorBounds.right), static_cast<double>(snapshot.indicatorBounds.bottom)};
		return PanelOwnsPoint(BarPanelId::BottomDockIndicator, frame, x, y);
	}

protected:
	void PreparePresentedButtonHits(const StateModeClass& stateMode, double frameZoom = 0);
	void PublishPresentedButtonHits();
	mutable std::mutex presentedButtonHitsMutex_;
	std::unordered_map<const BarButtonClass*, Inkeys::UI::UI3::ButtonHitSnapshot> presentedButtonHits_, candidateButtonHits_;
	std::unordered_map<const void*, Inkeys::UI::UI3::ButtonHitSnapshot> presentedVisualHits_, candidateVisualHits_;
	void BeginBottomDockTransition() noexcept
	{
		Inkeys::UI::Bar::BeginBarBottomDockTransition(bottomDockTransitionSerial);
	}
	unsigned long long FinishBottomDockTransition(bool deferWindowMove = false) noexcept
	{
		// 全部输入/显示/工作区写者共用此屏障，稳定偶数发布前退休旧位图的原生直移。
		if (deferWindowMove) CancelNativeDirectWindowMove(true);
		return Inkeys::UI::Bar::FinishBarBottomDockTransition(
			bottomDockTransitionSerial, bottomDockDeferredTransitionSerial, deferWindowMove);
	}
	// 调用方持有 directWindowDragMutex；screenDelta 是真实直移，layoutTranslation 是吸收的布局像素。
	void RebaseBottomDockPresentedWindow(POINT directTranslation,
		POINT screenDelta = {}, POINT layoutTranslation = {}) noexcept
	{
		directWindowPresentedTranslationX.store(
			directTranslation.x, memory_order_relaxed);
		directWindowPresentedTranslationY.store(
			directTranslation.y, memory_order_relaxed);
		bottomDockPresentedMappingSerial.fetch_add(1, memory_order_acq_rel);
		std::atomic_thread_fence(std::memory_order_release);
		bottomDockPresentedDirectTranslationX.store(
			directTranslation.x, memory_order_relaxed);
		bottomDockPresentedDirectTranslationY.store(
			directTranslation.y, memory_order_relaxed);
		bottomDockPresentedMainCenterScreenX.store(
			bottomDockPresentedMainCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		bottomDockPresentedMainCenterScreenY.store(
			bottomDockPresentedMainCenterScreenY.load(memory_order_relaxed)
				+ screenDelta.y,
			memory_order_relaxed);
		bottomDockPresentedRawMainCenterScreenX.store(
			bottomDockPresentedRawMainCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		bottomDockPresentedBodyCenterScreenX.store(
			bottomDockPresentedBodyCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		bottomDockPresentedRawBodyCenterScreenX.store(
			bottomDockPresentedRawBodyCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		const auto shift = [](auto& value, auto delta)
			{ value.store(value.load(memory_order_relaxed) + delta, memory_order_relaxed); };
		shift(bottomDockPresentedVisualBodyCenterScreenX, screenDelta.x);
		if (layoutTranslation.x != 0 || layoutTranslation.y != 0)
		{
			const double zoom = Inkeys::UI::Bar::NormalizeBarBottomDockZoom(
				bottomDockPresentedZoom.load(memory_order_relaxed));
			const double dx = layoutTranslation.x / zoom, dy = layoutTranslation.y / zoom;
			// 绝对 DIP 端点随布局平移；scale 和弹性/刚性相对偏移保持原值。
			shift(bottomDockPresentedBaseTopDip, dy);
			shift(bottomDockPresentedBaseBottomDip, dy);
			shift(bottomDockPresentedVisualTopDip, dy);
			shift(bottomDockPresentedVisualBottomDip, dy);
			shift(bottomDockPresentedRigidGripYDip, dy);
			shift(bottomDockPresentedBaseLeftDip, dx);
			shift(bottomDockPresentedBaseRightDip, dx);
			shift(bottomDockPresentedVisualLeftDip, dx);
			shift(bottomDockPresentedVisualRightDip, dx);
			// 提示命中是布局像素；按钮命中是绝对 DIP，两者都不是屏幕缓存。
			std::lock_guard hitLock(presentedButtonHitsMutex_);
			for (Panel* panel : {&mainPanel, &morePanel, &colorPickerPanel, &annotationTooltipPanel,
				&overflowTooltipPanel, &drawAttributePanel, &bottomDockIndicatorPanel})
			{
				auto visual = panel->Presented().visualBounds;
				auto interaction = panel->Presented().interaction.bounds;
				visual.left += layoutTranslation.x; visual.right += layoutTranslation.x;
				visual.top += layoutTranslation.y; visual.bottom += layoutTranslation.y;
				if (panel == &bottomDockIndicatorPanel)
				{
					interaction.left += layoutTranslation.x; interaction.right += layoutTranslation.x;
					interaction.top += layoutTranslation.y; interaction.bottom += layoutTranslation.y;
				}
				else
				{
					// 原命中表以 FLOAT 存 DIP；吸收直移时保持同样的舍入，不能改用 double 累加。
					interaction.left = static_cast<FLOAT>(interaction.left) + static_cast<FLOAT>(dx);
					interaction.right = static_cast<FLOAT>(interaction.right) + static_cast<FLOAT>(dx);
					interaction.top = static_cast<FLOAT>(interaction.top) + static_cast<FLOAT>(dy);
					interaction.bottom = static_cast<FLOAT>(interaction.bottom) + static_cast<FLOAT>(dy);
				}
				panel->RebasePresentedBounds(visual, interaction);
			}
			const auto shiftHits = [&](auto& hits)
			{
				for (auto& [identity, hit] : hits)
				{
					hit.boundsDip.left += static_cast<FLOAT>(dx); hit.boundsDip.right += static_cast<FLOAT>(dx);
					hit.boundsDip.top += static_cast<FLOAT>(dy); hit.boundsDip.bottom += static_cast<FLOAT>(dy);
				}
			};
			shiftHits(presentedButtonHits_); shiftHits(presentedVisualHits_);
		}
		bottomDockPresentedMappingSerial.fetch_add(1, memory_order_release);
	}
	// 拖动交互
	Inkeys::UI::UI3::InteractionTask<BarSeekResult> Seek(const ExMessage& msg);
	bool SetBorderCursorRawInputEnabled(HWND hWnd, bool enabled);
	void ActivateBorderCursorTracking(HWND hWnd);
	void RegisterBorderCursorLight(HWND hWnd);
	void HandleBorderCursorGraceTimeout(HWND hWnd);
	void SuspendBorderCursorTracking(HWND hWnd, bool waitForMouseLeave = false);
	bool ScheduleBorderCursorGraceTimer(HWND hWnd, UINT delayMs);
	void HandleCanvasDrawingActivity(HWND hWnd, bool started);
	void CloseAnnotationTooltip();
	void CloseThicknessOverflowTooltip();
	void CloseDrawAttributeTooltips();
	void ClosePenTypeMenu();
	void CloseThicknessSlider(bool cancelCapture);
	void CloseColorPicker(bool cancelCapture);
	void ShutdownWindowInput(HWND hWnd);
	void RefreshBorderCursorVisibleRegions();
	bool IsBorderCursorLightNearVisibleRegion(POINT screenPoint);
	bool IsAutoCollapsePointerHit(POINT screenPoint);
	std::atomic<unsigned long long> mainButtonClickPulseSerial = 0;
	Inkeys::UI::Bar::BarToggleClickCoalescer toggleClickCoalescer;

	mutex borderCursorLightMutex;
	D2D1_POINT_2F borderCursorLightPoint = D2D1::Point2F();
	D2D1_POINT_2F borderCursorLightScreenPoint = D2D1::Point2F();
	unsigned long long borderCursorLightSerial = 0;
	bool borderCursorInputAvailable = false;
	bool borderCursorLightReady = false;
	bool borderCursorLightNearVisibleRegion = false;
	bool borderCursorRawInputRegistered = false;
	bool borderCursorRegistrationFailureLogged = false;
	bool borderCursorRemovalFailureLogged = false;
	bool borderCursorTimerFailureLogged = false;
	bool borderCursorActivationBlockedUntilLeave = false;
	BarBorderCursorTrackingStateEnum borderCursorTrackingState =
		BarBorderCursorTrackingStateEnum::Dormant;
	ULONGLONG borderCursorGraceDeadlineTick = 0;
	array<RECT, 10> borderCursorVisibleRegions{};
	size_t borderCursorVisibleRegionCount = 0;

	// ULW 与主按钮直移共用几何锁；位移使用原子目标，交互线程不等待慢提交。
	mutex directWindowDragMutex;
	atomic<BarDirectWindowDragPhase> directWindowDragPhase =
		BarDirectWindowDragPhase::Idle;
	atomic<LONG> directWindowDragTranslationX = 0;
	atomic<LONG> directWindowDragTranslationY = 0;
	atomic<LONG> directWindowPresentedTranslationX = 0;
	atomic<LONG> directWindowPresentedTranslationY = 0;
	RECT committedWindowScreenBounds{};
	bool committedWindowScreenBoundsReady = false;
	std::uint64_t committedWindowPresentationRevision = 0;
	HWND committedWindowHandle = nullptr;
	std::uint64_t committedWindowGeneration = 0;
	struct NativeDirectMove
	{
		HWND hwnd = nullptr;
		std::uint64_t generation = 0, frameSerial = 0, session = 0, transitionSerial = 0, presentationRevision = 0;
		RECT bounds{};
		POINT translation{};
	} nativeDirectMove;
	atomic<std::uint64_t> nativeDirectMoveRevision = 0;
	std::uint64_t nativeDirectMoveSerial = 0;
	// 载荷沿既有几何锁发布；窗口原生执行和回滚由同一个 Service owner 持有。
	bool RequestNativeDirectWindowMoveLocked(POINT translation, unsigned long long transitionSerial,
		std::uint64_t leasedPresentationRevision = 0) noexcept;
	bool ConsumeNativeDirectWindowMoveLocked(std::uint64_t presentationRevision) noexcept;
	void CancelNativeDirectWindowMove(bool preserveCompleted = false) noexcept;
	void PollNativeDirectWindowMove() noexcept;
	Inkeys::Display::Subscription displaySubscription;
	atomic<unsigned long long> pendingDisplaySerial = 0;
	mutex pendingDisplayPublishMutex;
	atomic<LONG> pendingDisplayLeft = 0;
	atomic<LONG> pendingDisplayTop = 0;
	atomic<LONG> pendingDisplayRight = 1;
	atomic<LONG> pendingDisplayBottom = 1;
	atomic<LONG> pendingWorkAreaLeft = 0;
	atomic<LONG> pendingWorkAreaTop = 0;
	atomic<LONG> pendingWorkAreaRight = 1;
	atomic<LONG> pendingWorkAreaBottom = 1;
	atomic<UINT> pendingDisplayDpi = USER_DEFAULT_SCREEN_DPI;
	atomic<double> pendingDisplayUiBaseScale = 1.0;
	atomic<double> pendingDesktopDockLine = 1.0;
	atomic<bool> pendingProtectBottomEdge = false;
	atomic<LONG> presentedMonitorOriginX = 0;
	atomic<LONG> presentedMonitorOriginY = 0;
	// 交互线程发布目标，渲染线程只回写当前视觉偏移与恢复完成状态。
	atomic<Inkeys::UI::Bar::BarBottomDockMode> bottomDockMode =
		Inkeys::UI::Bar::BarBottomDockMode::BottomDocked;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<double> bottomDockElasticOffsetDip = 0.0;
	atomic<Inkeys::UI::Bar::BarBottomDockCenterMode> bottomDockCenterMode =
		Inkeys::UI::Bar::BarBottomDockCenterMode::Centered;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockCenterPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<double> bottomDockCenterElasticOffsetDip = 0.0;
	atomic<bool> bottomDockIndicatorGestureEligible = false;
	atomic<bool> bottomDockCenterAutoCaptureRequested = false;
	atomic<unsigned long long> bottomDockPresentedMappingSerial = 0;
	atomic<Inkeys::UI::Bar::BarBottomDockMode> bottomDockPresentedMode =
		Inkeys::UI::Bar::BarBottomDockMode::BottomDocked;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockPresentedPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<bool> bottomDockPresentedRecoveryActive = false;
	atomic<double> bottomDockPresentedElasticOffsetDip = 0.0;
	atomic<double> bottomDockPresentedBaseTopDip = 0.0;
	atomic<double> bottomDockPresentedBaseBottomDip = 0.0;
	atomic<double> bottomDockPresentedVisualTopDip = 0.0;
	atomic<double> bottomDockPresentedVisualBottomDip = 0.0;
	atomic<double> bottomDockPresentedScaleY = 1.0;
	atomic<double> bottomDockPresentedRigidGripYDip = 0.0;
	atomic<double> bottomDockPresentedRigidTranslationDip = 0.0;
	atomic<Inkeys::UI::Bar::BarBottomDockCenterMode>
		bottomDockPresentedCenterMode =
			Inkeys::UI::Bar::BarBottomDockCenterMode::Centered;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockPresentedCenterPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<double> bottomDockPresentedCenterElasticOffsetDip = 0.0;
	atomic<double> bottomDockPresentedBaseLeftDip = 0.0;
	atomic<double> bottomDockPresentedBaseRightDip = 0.0;
	atomic<double> bottomDockPresentedVisualLeftDip = 0.0;
	atomic<double> bottomDockPresentedVisualRightDip = 0.0;
	atomic<double> bottomDockPresentedScaleX = 1.0;
	atomic<double> bottomDockPresentedRigidGripTranslationXDip = 0.0;
	atomic<double> bottomDockPresentedRigidTranslationXDip = 0.0;
	atomic<double> bottomDockPresentedZoom = 1.0;
	atomic<LONG> bottomDockPresentedDisplayLeft = 0;
	atomic<LONG> bottomDockPresentedDisplayTop = 0;
	atomic<LONG> bottomDockPresentedDisplayRight = 1;
	atomic<LONG> bottomDockPresentedDisplayBottom = 1;
	atomic<LONG> bottomDockPresentedWorkAreaLeft = 0;
	atomic<LONG> bottomDockPresentedWorkAreaTop = 0;
	atomic<LONG> bottomDockPresentedWorkAreaRight = 1;
		atomic<LONG> bottomDockPresentedWorkAreaBottom = 1;
		atomic<UINT> bottomDockPresentedDisplayDpi = USER_DEFAULT_SCREEN_DPI;
		atomic<unsigned long long> bottomDockPresentedDisplaySerial = 0;
	atomic<double> bottomDockPresentedDesktopDockLine = 1.0;
	atomic<bool> bottomDockPresentedProtectBottomEdge = false;
	atomic<LONG> bottomDockPresentedDirectTranslationX = 0;
	atomic<LONG> bottomDockPresentedDirectTranslationY = 0;
	atomic<double> bottomDockPresentedMainCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedMainCenterScreenY = 0.0;
	atomic<double> bottomDockPresentedRawMainCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedBodyCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedVisualBodyCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedRawBodyCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedMainButtonVisibleHeightDip = 81.0;
	atomic<bool> bottomDockPresentedDragActive = false;
	atomic<unsigned long long> bottomDockPresentedDragSession = 0;
	atomic<bool> bottomDockDragActive = false;
	atomic<double> bottomDockDragRigidGripScreenX = 0.0;
	atomic<double> bottomDockDragRigidGripScreenY = 0.0;
	// 顶边目标独立于底端形变；与两轴状态、translation 共用发布事务。
	atomic<double> bottomDockDragTopScreenY = 0.0;
	atomic<bool> bottomDockDragTopAnchorValid = false;
	atomic<unsigned long long> bottomDockDragSession = 0;
	atomic<bool> bottomDockRecoveryActive = false;
	atomic<unsigned long long> bottomDockTransitionSerial = 0;
	atomic<unsigned long long> bottomDockDeferredTransitionSerial = 0;
	atomic<unsigned long long> bottomDockPresentedTransitionSerial = 0;

	friend class BarGeometryAttributeTestAccess;
	friend BarUiMainLightingInput ReadMainBarLightingInput(BarUISetClass* owner);
	friend class BarGoldenTestAccess;
	friend class BarRenderLoopCoordinator;
	friend LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
// 全局 Bar UI 集合
export extern BarUISetClass barUISet;

// 初始化

namespace Inkeys::UI::Bar
{
	export struct PresentationAlphaDiagnostics final
	{
		std::uint8_t requested = 255;
		std::uint8_t committed = 255;
		bool transparentCommitted = false;
		bool opaqueCommitted = false;
	};

	export WNDPROC WindowProc() noexcept;
	export void RegisterNativeTouchInput(HWND window);
	// 主栏内容重放（--ui3-host-offscreen-test 的第二宿主验证专用）：生产路径一帧的像素定稿并提交成功后，
	// 协调器把「同一帧内容」交给测试宿主，由它画进自己的帧目标与绘制器。各成员只在回调期间有效。
	export struct BarMainBarContentReplay
	{
		FrameCoordinateMapping mapping;   // 本帧生产路径使用的坐标映射
		RECT viewport{};                  // 本帧提交给窗口的范围（缩放后的布局像素）
		RECT contentBounds{};             // 本帧内容的视觉外框（缩放后的布局像素）
		D2D1_SIZE_U targetSize{};         // 生产路径帧目标的像素尺寸
		// 帧开始之前调用：把主栏绘制器的本帧输入同步给 renderer，并在 context 上建立基础变换。
		std::function<void(UIRendering& renderer, ID2D1DeviceContext* context,
			const FrameCoordinateMapping& mapping)> prepare;
		// 帧开始之后调用：经主栏内容绘制入口把本帧内容画进 renderer 与 context。
		std::function<void(UIRendering& renderer, ID2D1DeviceContext* context,
			const FrameCoordinateMapping& mapping)> draw;
	};
	export using BarMainBarContentReplayCallback = std::function<void(const BarMainBarContentReplay&)>;
	// contentReplay 为空（生产与其余测试）时协调器不执行任何重放逻辑。
	export std::function<Inkeys::UI::RenderPipeline::FrameResult(
		const Inkeys::UI::RenderPipeline::FrameContext&)> CreateBarOffscreenRenderCallback(
		BarUISetClass& owner, std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)> present,
		BarMainBarContentReplayCallback contentReplay = {});
	export int RunUi3MultiTargetOffscreenBenchmark();
	export int RunGeometryAttributeOffscreenTest();
	export int RunUi3GoldenDump(const wchar_t* outputDirectory);
	// 像素基线入口（Bar.Golden.Test.cpp）的主栏场景夹具，供第二宿主测试复用同一份搭建与确定性处理。
	struct BarGoldenMainSceneHost
	{
		// 交给协调器的内容重放回调；为空时不重放。
		BarMainBarContentReplayCallback replay;
		// 每次经基线读回端成功读回 ULW 源裁剪后调用（预乘 BGRA、自上而下，尺寸为 info.psize）。
		std::function<void(const UPDATELAYEREDWINDOWINFO& info, const std::vector<BYTE>& bytes)> captured;
		// 场景按基线流程推进到静止后调用；renderFrame 强制渲染一帧，返回是否收到新的有效提交。
		std::function<bool(const std::function<bool()>& renderFrame)> settled;
		// 可选：同时拿到自然帧入口（不强制整幅，生产按自己的脏区提交；返回是否有新的有效提交）。设置后代替 settled。
		std::function<bool(const std::function<bool()>& renderFrame, const std::function<bool()>& naturalFrame)> settledNatural;
	};
	// 主栏展开（drawAttribute 为 true 时再打开绘制属性面板）；返回值口径同基线：S_OK 成功、S_FALSE 未静止。
	HRESULT RunGoldenMainSceneForHost(bool drawAttribute, bool dark, double scale,
		const BarGoldenMainSceneHost& host);
	export int RunUi3HostOffscreenTest();
	// UI3 自动脏区的测试钩子：设置观察者后每个成功提交帧回调一次（生产路径为空）；
	// shadowDamage 与 panelDamage 均为生产登记表的本帧脏区（影子登记表已删除，字段保留兼容测试）。
	// 坐标均为本帧窗口客户区像素，已裁切到视口。
	export struct BarVisualShadowFrame
	{
		RECT shadowDamage{};
		RECT presentDirty{};
		RECT panelDamage{};
		RECT lightDamage{};
		SIZE viewport{};
		std::size_t changed = 0;
		std::size_t purityMismatches = 0;
		std::size_t purityGroupMismatches = 0;
		std::uint64_t serial = 0;
		std::vector<std::string> purityNames;
		std::vector<std::string> panelChangedNames;
		unsigned fullReason = 0;
		// 本帧开始时只有已登记的字号修正动画在进行（按压标签修正等，交给登记表而不整幅）。
		bool fontRegistryOnly = false;
	};
	export void SetBarVisualShadowTestHook(std::function<void(const BarVisualShadowFrame&)> observe);
	export int RunUi3AttributeDirtyTest();
	bool RunConstructionPromptTouchRouteTest(POINT screen);
	export int RunIdleBackingResourceLifecycleTest() noexcept;
	export int RunUi3WorkspaceCenteringTest();
	export int RunUi3BottomDockTransitionTest();
	export int RunBarNativeDirectMoveTests(const std::function<bool()>& gateOwner,
		const std::function<void()>& releaseOwner);
	bool RunUi3BottomDockInteractionSequence(BarUISetClass& owner,
		const std::function<bool(const char*, POINT)>& render,
		double downNormalizedY,
		const std::function<BOOL(HWND, int, int)>& directMove,
		const std::function<void(const char*)>& beforeMove = {},
		const std::function<bool(unsigned, const char*, POINT)>& repeatRelease = {},
		bool expandAfterClick = false);
	export int RunEraserAttributeOffscreenTest();
	export int RunThicknessFineDialProductionTest() noexcept;
	export int RunConstructionAutoCollapseProductionTest() noexcept;
	export int RunKeyboardShortcutBusinessProductionTest() noexcept;
	export int RunKeyboardShortcutWhiteboardProductionTest() noexcept;
	export Inkeys::Message::Reply QueueWindowMessageInLayoutSpace(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	// 主栏向共享输入 worker（Inkeys.UI.UI3:HostInput）登记主内容处理器的作用域：生产交互线程，
	// 以及直接驱动泵或建立离屏 worker 的无窗口回归，都在此之前持有。可嵌套，最后一个结束时注销。
	class MainInteractionHandlerScope final
	{
	public:
		MainInteractionHandlerScope();
		~MainInteractionHandlerScope();
		MainInteractionHandlerScope(const MainInteractionHandlerScope&) = delete;
		MainInteractionHandlerScope& operator=(const MainInteractionHandlerScope&) = delete;
	};
	export void Initialization(Inkeys::UI::StartupPlacement startupPlacement =
		Inkeys::UI::StartupPlacement::CenteredBottomBar);
	export void RequestPresentationAlpha(std::uint8_t alpha) noexcept;
	export std::uint8_t CommittedPresentationAlpha() noexcept;
	export std::uint8_t RequestedPresentationAlpha() noexcept;
	export PresentationAlphaDiagnostics SnapshotPresentationAlphaDiagnostics() noexcept;
	export void SetAnimationOptions(bool enable, double speedRate);
	export bool GetBarEdgeLightingEnabled() noexcept;
	export void SetEdgeLightingOptions(bool enable, bool dynamic);
	export void SetThemeMode(int themeMode) noexcept;
	// 背景百分比 0 表示跟随主题默认；调用方负责先按配置规则归一。
	export void SetResidentOpacity(int backgroundPercent, int contentPercent) noexcept;
	export using ::Inkeys::UI::Bar::StatusIndicatorKind;
	export using ::Inkeys::UI::Bar::StatusIndicatorLifetime;
	export using ::Inkeys::UI::Bar::StatusIndicatorAction;
	export using ::Inkeys::UI::Bar::StatusIndicatorIcon;
	export using ::Inkeys::UI::Bar::StatusIndicatorSpec;
	export void PublishStatusIndicator(const StatusIndicatorSpec&);
	// 超级置顶在启动时确定、运行中不变：启动成功后发布一次，常驻到退出。
	export void PublishSuperTopStatusIndicator();
	// PPT/WPS 放映场次的开始与结束边沿（PptInfo 线程）。
	export void PublishPptStatusIndicator(std::uint64_t session, bool wps);
	export void RetirePptStatusIndicator(std::uint64_t session);
	export void RetireStatusIndicator(std::uint64_t key);
	export void PublishStatusIndicatorProgress(std::uint64_t key,float progress,bool complete);
	export void SetStatusIndicatorOptions(bool enable,bool ppt,bool superTop,bool newVersion);
	export void DebugAddRandomStatusIndicator();
	export void DebugRemoveStatusIndicator();
	export std::size_t StatusIndicatorCount() noexcept;
	export std::size_t DebugStatusIndicatorCount() noexcept;
	export void SetDebugOptions(bool enable, bool showFrameRate);
	export bool DebugModeEnabled() noexcept;
	export void SetCurrentPageHasContent(bool hasContent) noexcept;
	export void NoteRecallHistoryAvailability(bool canUndo, bool canRedo);
	export bool CurrentPageHasContent() noexcept;
	export void RefreshToolState() noexcept;
	export struct PptIdleInputSnapshot
	{
		std::chrono::steady_clock::time_point activity{};
		bool hovered = false, pressed = false;
	};
	export PptIdleInputSnapshot SnapshotPptIdleInput() noexcept;
	export void ResetPptIdleInput() noexcept;
	export void PublishPptIdleDeadline(std::chrono::steady_clock::time_point) noexcept;
	export void SetPptPresentationActive(bool active) noexcept;
	export bool PptPresentationActive() noexcept;
	export void PublishPptSession(std::uint64_t session, bool active, HWND showWindow) noexcept;
	export std::optional<bool> ConsumePptSceneTransition() noexcept;
	export HWND PptPresentationWindow() noexcept;
	export HMONITOR PptSceneMonitor() noexcept;
	export void UpdateSceneMonitor(HMONITOR monitor) noexcept;
	export void UpdatePptSceneMonitor(HWND window) noexcept;
	export double SceneBottomDockInsetDip() noexcept;
	export void SetBusinessFocusCallback(std::function<void()> callback);
	export void NotifyPptBusinessAction();
	export bool PptBusinessFocusAllowed() noexcept;
	export void SetEndShowRequestCallback(std::function<void(std::uint64_t)> callback);
	export void CompleteEndShowRequest(std::uint64_t request) noexcept;
	export void SetEndShowCallback(std::function<void()> callback);
	export void RequestEndShow();
	export void CompleteEndShowRequest() noexcept;
	export void SetWhiteboardActive(bool active) noexcept;
	export void CollapseAuxiliaryPanels(bool cancelCapture = true) noexcept;
	export bool WhiteboardActive() noexcept;
	export void RequestWhiteboardBottomDock() noexcept;
	export bool ConsumeWhiteboardBottomDockRequest() noexcept;
	export bool WhiteboardDockLockActive() noexcept;
	export void ClearWhiteboardDockLock() noexcept;
	export bool HideWhiteboardSnapIndicator() noexcept;
	export void NotifyCanvasDrawingStarted();
	export void NotifyCanvasDrawingEnded();
	export void NotifyCanvasStrokeAccepted(
		const Inkeys::Drawing::Draw3::Bridge::AcceptedStrokeBegin& notice) noexcept;
	export std::uint64_t AutoCollapseSceneRevision() noexcept;

	bool InitializeWindow(BarUISetClass& barUISet);
	void InitializeUI(BarUISetClass& barUISet);
	void SetContentStateUpdatesReady(bool ready) noexcept;
};
