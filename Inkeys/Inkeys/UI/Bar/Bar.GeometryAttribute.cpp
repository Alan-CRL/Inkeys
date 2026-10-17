module;
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "Bar.I18nFormat.h"
#include <d2d1_1helper.h>
#include <algorithm>
#include <array>
#include <cmath>

module Inkeys.UI.Bar;
import :Main;
import :Layout;
import :Theme;

using namespace Inkeys::UI::Bar;
using namespace draw3::shapes;
extern const double BarUiDividerRadius;
extern const double BarUiDividerCursorLightIntensity;
extern const double BarDrawAttributeSurfaceOpacity;

namespace
{
	constexpr int CategoryCount = 11, CircleCenter = 11, CircleBox = 12;
	constexpr int SidesBegin = 13, StyleBegin = 23, PatternBegin = 26;
	constexpr int ThicknessBegin = 30, FineAdjustment = 33, CloseButton = 34, ColorButton = 35;
	constexpr double ShapeButtonHeight = 50, ShapeLabelSize = 11;
	constexpr std::array<PresetKind, CategoryCount> Categories{
		PresetKind::Line, PresetKind::Arrow, PresetKind::Rectangle, PresetKind::RoundedRectangle,
		PresetKind::Circle, PresetKind::Ellipse, PresetKind::Triangle, PresetKind::Parallelogram,
		PresetKind::Rhombus, PresetKind::Trapezoid, PresetKind::RegularPolygon };
	D2D1_RECT_F PixelRect(EraserAttributeRect r, double zoom)
	{
		return D2D1::RectF(static_cast<float>(r.left * zoom), static_cast<float>(r.top * zoom),
			static_cast<float>(r.right * zoom), static_cast<float>(r.bottom * zoom));
	}
	RECT IntegerRect(EraserAttributeRect r, double zoom, int padding = 0)
	{
		if (r.Width() <= 0 || r.Height() <= 0) return {};
		return { static_cast<LONG>(floor(r.left * zoom)) - padding,
			static_cast<LONG>(floor(r.top * zoom)) - padding,
			static_cast<LONG>(ceil(r.right * zoom)) + padding,
			static_cast<LONG>(ceil(r.bottom * zoom)) + padding };
	}
	void Place(BarUiShapeClass& shape, EraserAttributeRect r)
	{
		shape.x.SetDirect(r.left); shape.y.SetDirect(r.top);
		shape.w.SetDirect(r.Width()); shape.h.SetDirect(r.Height());
		shape.UpInh(BarUiInheritClass(r.left, r.top));
	}
	bool OpenPreset(PresetKind kind) noexcept { return kind == PresetKind::Line || kind == PresetKind::Arrow; }
	wstring ItemLabel(int item)
	{
		const auto& t = I18nKey.UI.Bar.GeometryAttributes;
		if (item < CategoryCount) return ShapeToolLabel(Categories[item]);
		if (item == CircleCenter) return I18n::getWOr(t.CircleCenterLabel, L"圆心绘制");
		if (item == CircleBox) return I18n::getWOr(t.CircleBoundsLabel, L"外接绘制");
		if (item >= SidesBegin && item < StyleBegin) return std::to_wstring(item - SidesBegin + 3);
		return {};
	}
	wstring ItemResource(int item)
	{
		if (item == CircleCenter) return L"barShapeCircleCenter";
		if (item == CircleBox) return L"barShapeCircleBounds";
		if (item >= StyleBegin && item < PatternBegin)
			return std::array<const wchar_t*, 3>{L"barShapeOutline", L"barShapeFill", L"barShapeOutlineFill"}[item - StyleBegin];
		if (item >= PatternBegin && item < ThicknessBegin)
			return std::array<const wchar_t*, 4>{L"barShapeSolid", L"barShapeDashed", L"barShapeDotted", L"barShapeDashDot"}[item - PatternBegin];
		if (item == FineAdjustment) return L"barThicknessAdjust";
		if (item == CloseButton) return L"barCloseSmall";
		if (item == ColorButton) return L"barShapeColor";
		return {};
	}
}

namespace Inkeys::UI::Bar
{
	wstring ShapeToolIconResource(const ToolSpec& tool, std::optional<unsigned> carouselSides)
	{
		switch (tool.preset)
		{
		case PresetKind::Line: return L"barShapeStraightLine";
		case PresetKind::Arrow: return L"barShapeArrow";
		case PresetKind::Rectangle: return L"barShapeRectangle";
		case PresetKind::RoundedRectangle: return L"barShapeRoundedRectangle";
		case PresetKind::Circle: return tool.creationMode == CreationMode::Drag ? L"barShapeCircleBounds" : L"barShapeCircleCenter";
		case PresetKind::Ellipse: return L"barShapeEllipse";
		case PresetKind::Triangle: return L"barShapeTriangle";
		case PresetKind::Parallelogram: return L"barShapeParallelogram";
		case PresetKind::Rhombus: return L"barShapeRhombus";
		case PresetKind::Trapezoid: return L"barShapeTrapezoid";
		case PresetKind::RegularPolygon:
			return L"barShapePolygon" + std::to_wstring((std::clamp)(carouselSides.value_or(tool.sides), 3u, 12u));
		default: return L"barGeometry";
		}
	}
	wstring ShapeToolLabel(PresetKind preset)
	{
		const auto& t = I18nKey.UI.Bar.GeometryAttributes;
		switch (preset)
		{
		case PresetKind::Line: return I18n::getWOr(t.StraightLineLabel, L"直线");
		case PresetKind::Arrow: return I18n::getWOr(t.ArrowLabel, L"箭头");
		case PresetKind::Rectangle: return I18n::getWOr(t.RectangleLabel, L"矩形");
		case PresetKind::RoundedRectangle: return I18n::getWOr(t.RoundedRectangleLabel, L"圆角矩形");
		case PresetKind::Circle: return I18n::getWOr(t.CircleLabel, L"圆");
		case PresetKind::Ellipse: return I18n::getWOr(t.EllipseLabel, L"椭圆");
		case PresetKind::Triangle: return I18n::getWOr(t.TriangleLabel, L"三角形");
		case PresetKind::Parallelogram: return I18n::getWOr(t.ParallelogramLabel, L"平行四边形");
		case PresetKind::Rhombus: return I18n::getWOr(t.RhombusLabel, L"菱形");
		case PresetKind::Trapezoid: return I18n::getWOr(t.TrapezoidLabel, L"梯形");
		case PresetKind::RegularPolygon: return I18n::getWOr(t.RegularPolygonLabel, L"正多边形");
		default: return {};
		}
	}
}

void BarGeometryAttributePanel::Initialize()
{
	if (initialized_) return;
	surface_.Initialization(0, 0, 0, 0, BarMainBarCornerRadiusDip, BarMainBarCornerRadiusDip,
		BarButtonFrameThicknessDip, GetThemeColor(BarThemeColorEnum::Surface), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	surface_.enable.Initialization(true); surface_.pct.SetDirect(BarDrawAttributeSurfaceOpacity);
	surface_.framePct.emplace(BarMainBarFrameOpacity); surface_.frameLightPct.emplace(BarMainBarFrameOpacity);
	surface_.frameRendering = BarUiFrameRenderingEnum::PointLight;
	surface_.frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
	for (int i = 0; i < static_cast<int>(buttons_.size()); ++i)
	{
		auto& b = buttons_[i]; b.hide = false; b.size = BarButtonSizeEnum::oneOne;
		b.button.Initialization(0, 0, 0, 0, BarButtonCornerRadiusDip, BarButtonCornerRadiusDip,
			BarButtonFrameThicknessDip, GetThemeColor(BarThemeColorEnum::PressedFill), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		b.button.enable.Initialization(true); b.button.pct.SetDirect(0);
		b.button.framePct.emplace(0); b.button.frameLightPct.emplace(0);
		b.button.frameRendering = BarUiFrameRenderingEnum::PointLight; b.button.framePrimaryLightEnabled = false;
		b.button.frameLightColor = BarUiFrameLightColorEnum::Frame;
		b.button.frameCursorLightIntensityScale = BarButtonCursorLightIntensity;
		b.name.Initialization(0, 0, 70, 15, ItemLabel(i), ShapeLabelSize, GetThemeColor(BarThemeColorEnum::TextPrimary));
		b.name.enable.Initialization(true); b.name.pct.SetDirect(1);
		b.icon.Initialization(0, 0, GetThemeColor(BarThemeColorEnum::TextPrimary), GetThemeColor(BarThemeColorEnum::TextPrimary));
		const auto resource = i < CategoryCount ? ShapeToolIconResource(ToolSpec{Categories[i]}) : ItemResource(i);
		b.icon.enable.Initialization(!resource.empty()); b.icon.pct.SetDirect(1);
		if (!resource.empty()) b.icon.InitializationFromResource(L"UI", resource);
		resourceTargets_[i] = resource; labelTargets_[i] = ItemLabel(i);
		b.icon.SetWH(i < CategoryCount ? 28 : 20, i < CategoryCount ? 28 : 20);
		b.icon.w.SetDirect(b.icon.w.tar); b.icon.h.SetDirect(b.icon.h.tar);
	}
	for (auto& d : dividers_)
	{
		d.Initialization(0, 0, 1, 1, BarUiDividerRadius, BarUiDividerRadius, BarButtonFrameThicknessDip,
			GetThemeColor(BarThemeColorEnum::SurfaceFrame), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		d.enable.Initialization(true); d.pct.SetDirect(0.30); d.framePct.emplace(0); d.frameLightPct.emplace(1);
		d.frameRendering = BarUiFrameRenderingEnum::PointLight; d.framePrimaryLightEnabled = false;
		d.frameCursorLightIntensityScale = BarUiDividerCursorLightIntensity;
	}
	initialized_ = true;
}

void BarGeometryAttributePanel::UpdateCarousel(bool enabled)
{
	std::lock_guard lock(carouselMutex_);
	carouselEnabled_ = enabled;
	if (!enabled)
	{
		if (carouselArmed_ && floating_window) KillTimer(floating_window, GeometryCarouselTimerId);
		carouselArmed_ = false; carouselDeadline_ = {}; return;
	}
	if (carouselArmed_) return;
	carouselDeadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(1800);
	carouselArmed_ = true;
	if (floating_window) SetTimer(floating_window, GeometryCarouselTimerId, 1800, nullptr);
}
void BarGeometryAttributePanel::StopCarousel() noexcept { UpdateCarousel(false); }
bool BarGeometryAttributePanel::PollCarouselTimer(unsigned& retryMs, std::chrono::steady_clock::time_point now)
{
	std::lock_guard lock(carouselMutex_); retryMs = 0;
	if (!carouselEnabled_ || !carouselArmed_) return false;
	if (now < carouselDeadline_)
	{
		retryMs = static_cast<unsigned>((std::max)(1LL,
			std::chrono::duration_cast<std::chrono::milliseconds>(carouselDeadline_ - now).count()));
		return false;
	}
	carouselSides_ = carouselSides_ == 12 ? 3 : carouselSides_ + 1;
	carouselArmed_ = false; return true;
}

bool BarGeometryAttributePanel::Advance(BarUISetClass& owner, const StateModeClass& state,
	double dt, double speed, double zoom, RECT workArea, POINT origin, double rigidX, double rigidY,
	const BarUiTimelineClass* parentTimeline, bool dragPlacementLocked)
{
	Initialize(); changed_ = false; active_ = false;
	const bool shouldClose = owner.barState.fold || state.StateModeSelect != StateModeSelectEnum::IdtShape
		|| owner.barState.drawAttribute || owner.barState.eraserAttribute || owner.barState.recallAttribute || owner.barState.moreExpanded;
	if (shouldClose) Close(owner);
	const bool open = owner.barState.geometryAttribute && !shouldClose; visible_ = open;
	const auto anchor = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)];
	const auto root = owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	const auto main = owner.shapeMap[BarUISetShapeEnum::MainBar];
	if (!anchor || !root || !main) { StopCarousel(); frame_.visible = false; return false; }
	root->UpInh(BarUiInheritClass(root->x.val - root->w.val / 2, root->y.val - root->h.val / 2));
	main->Inherit(BarUiInheritEnum::Center, *root); anchor->button.Inherit(BarUiInheritEnum::CenterFromTopLeft, *main);
	EraserAttributeRect trigger{anchor->button.inhX, anchor->button.inhY,
		anchor->button.inhX + anchor->button.w.val, anchor->button.inhY + anchor->button.h.val};
	const EraserAttributeRect work{(workArea.left - origin.x) / zoom - rigidX, (workArea.top - origin.y) / zoom - rigidY,
		(workArea.right - origin.x) / zoom - rigidX, (workArea.bottom - origin.y) / zoom - rigidY};
	const auto tool = GetShapeToolSpec(state); const auto style = GetShapeStyle(state);
	const bool showStyle = !OpenPreset(tool.preset), showStroke = !showStyle || style.stroke;
	const bool circle = tool.preset == PresetKind::Circle, polygon = tool.preset == PresetKind::RegularPolygon;

	// 所有语言先测量最长标签，再确定同宽列，避免平行四边形等名称被裁掉。
	wstring labels = owner.spec.GetFontFamily() + owner.spec.GetTextLocale();
	for (int i = 0; i < CategoryCount; ++i) labels += ItemLabel(i) + L"|";
	if (labels != measuredLabels_)
	{
		measuredLabels_ = labels; measuredColumnWidth_ = 70;
		for (int i = 0; i < CategoryCount; ++i)
			measuredColumnWidth_ = (std::max)(measuredColumnWidth_, ceil(static_cast<double>(owner.spec.MeasureText(ItemLabel(i), ShapeLabelSize).width)) + 10);
	}
	const double columnWidth = measuredColumnWidth_;
	const double targetWidth = columnWidth * 6 + BarButtonGapDip * 7;
	const double parameterHeight = circle || polygon ? BarButtonOneSideDip + BarButtonGapDip * 2 + 1 : 0;
	const double baseHeight = 2 * ShapeButtonHeight + BarButtonGapDip * 5 + 1 + BarButtonOneSideDip;
	const double targetHeight = baseHeight + parameterHeight;
	const int wantedParameterGroup = circle ? 1 : polygon ? 2 : 0;
	if (wantedParameterGroup != 0) parameterGroup_ = wantedParameterGroup;
	const BarUiAnimationAdvanceContextClass context{dt, speed, static_cast<bool>(BarUiAnimationEnabled), false};
	if (!hadOpen_) { width_.SetDirect(targetWidth); height_.SetDirect(targetHeight); }
	else { width_.SetTar(targetWidth, BarUiDefaultOperationDur); height_.SetTar(targetHeight, BarUiDefaultOperationDur); }
	for (auto* value : {&width_, &height_})
	{
		if (context.animationEnabled && dt <= 0 && !value->IsSame()) { active_ = true; continue; }
		const auto result = BarUiAdvanceAnimation(*value, context); changed_ |= result.changed; active_ |= result.active;
	}
	const bool below = owner.barState.widgetPosition.primaryBar;
	if (open && hadOpen_ && below != previousBelow_ && !dragPlacementLocked) sideSwitch_ = true;
	if (!hadOpen_ || !open) previousBelow_ = below;
	changed_ |= motion_.Retarget(open && !sideSwitch_, sideSwitch_ ? static_cast<double>(BarUiDefaultOperationDur) / 2 : static_cast<double>(BarUiDefaultOperationDur),
		sideSwitch_ ? nullptr : parentTimeline);
	changed_ |= motion_.Advance(context);
	if (sideSwitch_ && !motion_.Visible())
	{
		previousBelow_ = below; sideSwitch_ = false;
		changed_ |= motion_.Retarget(open, BarUiDefaultOperationDur / 2);
		if (!context.animationEnabled) changed_ |= motion_.Advance(context);
	}
	active_ |= motion_.Active(); hadOpen_ = open || motion_.Visible();
	const double width = (std::max)(1.0, static_cast<double>(width_.val));
	const double height = (std::max)(1.0, static_cast<double>(height_.val));
	const double parameterSpace = (std::clamp)(height - baseHeight, 0.0, BarButtonOneSideDip + BarButtonGapDip * 2 + 1);
	parameterOpacity_ = parameterSpace / (BarButtonOneSideDip + BarButtonGapDip * 2 + 1);
	if (wantedParameterGroup == 0 && parameterSpace <= 0.000001) parameterGroup_ = 0;
	double left = EraserRectCenterX(trigger) - width / 2;
	const double gap = 10;
	double top = previousBelow_ ? main->inhY + main->h.val + gap : main->inhY - height - gap;
	if (width <= work.Width()) left = (std::clamp)(left, work.left, work.right - width);
	if (height <= work.Height()) top = (std::clamp)(top, work.top, work.bottom - height);
	EraserAttributeRect panel{left, top, left + width, top + height};
	if (dragPlacementLocked && open && previousPanel_.Width() > 0)
	{
		panel = OffsetEraserRect(previousPanel_, EraserRectCenterX(trigger) - EraserRectCenterX(previousAnchor_),
			EraserRectCenterY(trigger) - EraserRectCenterY(previousAnchor_));
	}
	previousPanel_ = panel; previousAnchor_ = trigger;
	auto pf = ResolveBarAttributePanelFrame(motion_, panel, trigger, 60);
	// 小工作区只缩放本次运行的面板，不改用户倍率或按钮尺寸偏好。
	const double fit = (std::min)({1.0, work.Width() / width, work.Height() / height});
	if (fit > 0 && fit < 1)
	{
		const double cx = EraserRectCenterX(panel), cy = EraserRectCenterY(panel);
		pf.pose.x = cx + (pf.pose.x - cx) * fit; pf.pose.y = cy + (pf.pose.y - cy) * fit; pf.pose.scale *= fit;
	}
	pf.pose = FitEraserSurfacePose(pf.pose, panel, work);
	GeometryAttributePresentation next; next.zoom = zoom; next.pose = pf.pose;
	next.opacity = pf.opacity; next.visible = pf.visible; next.tool = tool;
	next.acceptsInput = open && !sideSwitch_ && !motion_.Active() && width_.IsSame() && height_.IsSame();
	next.panel = pf.pose.Apply(panel);
	std::array<EraserAttributeRect,36> slots{}; std::array<EraserAttributeRect,4> lines{};
	const double g = BarButtonGapDip, smallSide = BarButtonOneSideDip;
	// 第一行末端保留关闭入口；其余两行仍至多六列。
	for (int i = 0; i < CategoryCount; ++i)
	{
		const int row = i < 5 ? 0 : 1, column = i < 5 ? i : i - 5;
		const double x = left + g + column * (columnWidth + g), y = top + g + row * (ShapeButtonHeight + g);
		slots[i] = {x, y, x + columnWidth, y + ShapeButtonHeight};
	}
	slots[CloseButton] = {left + width - g - smallSide, top + g, left + width - g, top + g + smallSide};
	slots[ColorButton] = {left + width - 2 * g - 2 * smallSide, top + g, left + width - 2 * g - smallSide, top + g + smallSide};
	double y = top + 2 * ShapeButtonHeight + g * 3;
	lines[0] = {left + g, y, left + width - g, y + 1}; y += 1 + g;
	if (parameterGroup_ == 1)
	{
		const double w = (width - g * 3) / 2;
		for (int i = 0; i < 2; ++i) slots[CircleCenter + i] = {left + g + i * (w + g), y, left + g + i * (w + g) + w, y + smallSide};
	}
	if (parameterGroup_ == 2)
	{
		const double startX = left + (width - (smallSide * 10 + g * 9)) / 2;
		for (int i = 0; i < 10; ++i) slots[SidesBegin + i] = {startX + i * (smallSide + g), y, startX + i * (smallSide + g) + smallSide, y + smallSide};
	}
	// 参数退场时保留原组，底部属性跟随同一尺寸动画连续平移，命中等待布局稳定。
	if (parameterGroup_ != 0) lines[1] = {left + g, y + parameterSpace - g - 1, left + width - g, y + parameterSpace - g};
	y += parameterSpace;
	double x = left + g;
	auto group = [&](int begin, int count)
	{
		for (int i = 0; i < count; ++i) { slots[begin + i] = {x, y, x + smallSide, y + smallSide}; x += smallSide + g; }
	};
	auto separator = [&](int i) { lines[i] = {x, y + 2, x + 1, y + smallSide - 2}; x += 1 + g * 2; };
	if (showStyle) { group(StyleBegin, 3); if (showStroke) separator(2); }
	if (showStroke) { group(PatternBegin, 4); separator(3); group(ThicknessBegin, 4); }
	const int requestedHover = hovered_;
	if (requestedHover != appliedHover_)
	{
		if (appliedHover_ >= 0) StopBarButtonHoverVisual(buttons_[appliedHover_], false);
		if (requestedHover >= 0) StartBarButtonHoverVisual(buttons_[requestedHover]);
		appliedHover_ = requestedHover; changed_ = true;
	}
	unsigned carouselSides;
	{ std::lock_guard lock(carouselMutex_); carouselSides = carouselSides_; }
	for (int i = 0; i < static_cast<int>(buttons_.size()); ++i)
	{
		auto& b = buttons_[i]; const bool shown = slots[i].Width() > 0;
		const bool selected = i < CategoryCount ? tool.preset == Categories[i]
			: i == CircleCenter ? tool.creationMode == CreationMode::CenterRadius
			: i == CircleBox ? tool.creationMode == CreationMode::Drag
			: i >= SidesBegin && i < StyleBegin ? tool.sides == static_cast<unsigned>(i - SidesBegin + 3)
			: i >= StyleBegin && i < PatternBegin ? static_cast<int>(GetShapeFillMode(state)) == i - StyleBegin
			: i >= PatternBegin && i < ThicknessBegin ? static_cast<int>(style.linePattern) == i - PatternBegin
			: i >= ThicknessBegin && i < FineAdjustment ? IsBarThicknessPresetSelected(PenModeSelectEnum::IdtPenSoftPen, i - ThicknessBegin, owner.barStyle.dpiZoom, state) : false;
		wstring resource = ItemResource(i), label = ItemLabel(i);
		if (i < CategoryCount)
		{
			auto itemTool = tool; itemTool.preset = Categories[i];
			if (itemTool.preset == PresetKind::Circle) itemTool.creationMode = state.Shape.circleMode;
			resource = ShapeToolIconResource(itemTool, i == 10 && tool.preset != PresetKind::RegularPolygon ? std::optional<unsigned>(carouselSides) : std::nullopt);
		}
		if (i >= ThicknessBegin && i < FineAdjustment)
		{
			const auto metrics = GetBarThicknessPresetMetrics(PenModeSelectEnum::IdtPenSoftPen, i - ThicknessBegin, state);
			label = Inkeys::UI::Bar::Detail::FormatWidthDipNumber(metrics.resolvedWidthDip);
			thicknessPreviewWidths_[i - ThicknessBegin] = metrics.appliedCanvasWidthPx;
		}
		if (resourceTargets_[i] != resource || labelTargets_[i] != label)
		{
			// 同一资源不重复读取/解码；只有真实语义切换才进入共享内容转换。
			changed_ |= b.TransitionContent(resource, label);
			resourceTargets_[i] = resource; labelTargets_[i] = label;
		}
		b.hide = !shown; b.state->state = selected ? BarWidgetState::Selected : BarWidgetState::None;
		SetBarButtonPressedVisual(b, pressed_ == i && !pressCancelled_);
		if (i == ColorButton)
		{
			// 色块的填充与描边有不同语义，仍借用共享背景/按压，只分开发布SVG双色目标。
			RetargetBarFixedButtonVisual({b.button, &b.icon, &b.name, &b.pressScale, &b.hoverStage},
				shown && pf.visible, true, false, pressed_ == i && !pressCancelled_, BarButtonHoverTransitionDuration);
			b.icon.color2->SetTar(GetPenColor(state));
		}
		else RetargetBarButtonInteractionVisual(b, shown && pf.visible, true, selected, BarButtonHoverTransitionDuration);
		const auto item = pf.pose.Apply(slots[i]); next.items[i] = shown ? item : EraserAttributeRect{};
		Place(b.button, next.items[i]); const double scale = pf.pose.scale;
		b.button.rw->SetDirect(BarButtonCornerRadiusDip * scale); b.button.rh->SetDirect(BarButtonCornerRadiusDip * scale);
		b.button.ft->SetDirect(BarButtonFrameThicknessDip * scale);
		const bool category = i < CategoryCount, circleOption = i == CircleCenter || i == CircleBox;
		const double parameterTextWidth = circleOption ? ceil(owner.spec.MeasureText(label, 12).width) + 4 : smallSide;
		b.icon.x.SetDirect(circleOption ? -(parameterTextWidth + 8) * scale / 2 : 0); b.icon.y.SetDirect(category ? -9 * scale : 0);
		// contentScale由共享SVG内容转换独占，面板姿态只改布局尺寸，避免覆盖缩放换图动画。
		b.icon.SetWH((category ? 28 : 20) * scale, (category ? 28 : 20) * scale);
		b.icon.w.SetDirect(b.icon.w.tar); b.icon.h.SetDirect(b.icon.h.tar);
		b.name.x.SetDirect(circleOption ? 14 * scale : 0); b.name.y.SetDirect(category ? 17 * scale : 0);
		b.name.w.SetDirect((category ? columnWidth - 4 : parameterTextWidth) * scale);
		b.name.h.SetDirect((category ? 15 : smallSide) * scale); b.name.size.SetDirect((category ? ShapeLabelSize : 12) * scale);
		b.name.enable.Initialization(!label.empty()); b.icon.enable.Initialization(!resource.empty());
		const BarButtonAnimationContext buttonContext{dt, speed, context.animationEnabled, false, shown && pf.visible, !selected};
		const auto result = AdvanceBarButtonVisual(b, buttonContext); changed_ |= result.changed; active_ |= result.active;
		next.hits[i] = ResolveBarButtonHitSnapshot(b, BarUiInheritClass(b.button.inhX, b.button.inhY));
		next.hits[i].visible = next.visible && shown; next.hits[i].enabled = next.acceptsInput;
	}
	Place(surface_, next.panel); const double scale = pf.pose.scale;
	surface_.rw->SetDirect(BarMainBarCornerRadiusDip * scale); surface_.rh->SetDirect(BarMainBarCornerRadiusDip * scale);
	surface_.ft->SetDirect(BarButtonFrameThicknessDip * scale);
	changed_ |= surface_.fill->val != GetThemeColor(BarThemeColorEnum::Surface);
	surface_.fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface)); surface_.frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	for (size_t i = 0; i < lines.size(); ++i)
	{
		auto& d = dividers_[i]; const bool shown = lines[i].Width() > 0;
		Place(d, shown ? pf.pose.Apply(lines[i]) : EraserAttributeRect{}); d.enable.Initialization(shown);
		d.ft->SetDirect(BarButtonFrameThicknessDip * scale); d.rw->SetDirect(BarUiDividerRadius * scale); d.rh->SetDirect(BarUiDividerRadius * scale);
		d.fill->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame)); d.frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	}
	changed_ |= frame_.panel != next.panel || frame_.items != next.items || frame_.opacity != next.opacity || frame_.visible != next.visible || frame_.zoom != next.zoom;
	frame_ = next;
	UpdateCarousel(open && next.acceptsInput && static_cast<bool>(BarUiAnimationEnabled) && tool.preset != PresetKind::RegularPolygon && pressed_ < 0);
	return changed_ || active_;
}

void BarGeometryAttributePanel::Draw(BarUIRendering& renderer, ID2D1DeviceContext* context)
{
	if (!initialized_ || !frame_.visible) return;
	if (deviceGeneration_ != renderer.GetDeviceGeneration())
	{
		brush_.Reset(); for (auto& b : buttons_) b.icon.ResetCache(); deviceGeneration_ = renderer.GetDeviceGeneration();
	}
	renderer.PushPanelOpacity(context, PixelRect(frame_.panel, frame_.zoom), frame_.opacity);
	renderer.SetFrameDiffuseMaskGeometryScale(1 / frame_.pose.scale);
	DrawBarBackgroundVisual(renderer, context, surface_, BarUiInheritClass(surface_.inhX, surface_.inhY));
	context->PushAxisAlignedClip(PixelRect(frame_.panel, frame_.zoom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	for (size_t i = 0; i < dividers_.size(); ++i)
		if (i != 1 && dividers_[i].enable.val) renderer.Shape(context, dividers_[i], BarUiInheritClass(dividers_[i].inhX, dividers_[i].inhY));
	auto drawButton = [&](size_t i)
	{
		auto& b = buttons_[i]; if (!b.IsVisible()) return;
		BarButtonDrawOptions options; options.primaryWeight = DWRITE_FONT_WEIGHT_NORMAL;
		if (i >= ThicknessBegin && i < FineAdjustment)
		{
			// 预设仍显示真实笔宽的小线段；数字保留，供细于一像素或超出预览时辨认。
			options.underlay = [&](BarUIRendering& r, ID2D1DeviceContext* dc, BarButtonClass& button, const BarButtonVisualInheritance& inh)
			{
				const auto color = static_cast<COLORREF>(button.name.color.val);
				auto* brush = r.ContentSolidBrush(color, 0.50f);
				if (!brush) return false;
				const float z = static_cast<float>(frame_.zoom), scale = static_cast<float>(frame_.pose.scale);
				const float cx = static_cast<float>((inh.button.x + button.button.w.val / 2) * z);
				const float y = static_cast<float>((inh.button.y + button.button.h.val - 5 * scale) * z);
				const float width = (std::max)(1.0f, static_cast<float>((std::min)(8.0 * z, thicknessPreviewWidths_[i - ThicknessBegin])) * scale);
				dc->DrawLine(D2D1::Point2F(cx - 7 * scale * z, y), D2D1::Point2F(cx + 7 * scale * z, y), brush, width);
				dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 7 * scale * z, y), width / 2, width / 2), brush);
				dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 7 * scale * z, y), width / 2, width / 2), brush); return true;
			};
		}
		DrawBarButtonVisual(renderer, context, b, BarUiInheritClass(b.button.inhX, b.button.inhY), options);
		if (focused_ == static_cast<int>(i))
		{
			auto* brush = renderer.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::TextPrimary), 0.8f);
			auto rect = PixelRect(frame_.items[i], frame_.zoom); rect.left += 2; rect.right -= 2; rect.top += 2; rect.bottom -= 2;
			if (brush) context->DrawRoundedRectangle(D2D1::RoundedRect(rect, 3, 3), brush, 1);
		}
	};
	for (size_t i = 0; i < buttons_.size(); ++i) if (i < CircleCenter || i >= StyleBegin) drawButton(i);
	if (parameterGroup_ != 0 && parameterOpacity_ > 0.000001)
	{
		const double top = frame_.items[parameterGroup_ == 1 ? CircleCenter : SidesBegin].top;
		const double bottom = dividers_[1].inhY + dividers_[1].h.val;
		if (bottom > top)
		{
			renderer.PushPanelOpacity(context, PixelRect(frame_.panel, frame_.zoom), parameterOpacity_);
			context->PushAxisAlignedClip(PixelRect({frame_.panel.left, top, frame_.panel.right, bottom}, frame_.zoom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for (size_t i = CircleCenter; i < StyleBegin; ++i) drawButton(i);
			auto& divider = dividers_[1]; if (divider.enable.val) renderer.Shape(context, divider, BarUiInheritClass(divider.inhX, divider.inhY));
			context->PopAxisAlignedClip(); context->PopLayer();
		}
	}
	context->PopAxisAlignedClip(); context->PopLayer(); renderer.SetFrameDiffuseMaskGeometryScale(1);
}

RECT BarGeometryAttributePanel::Bounds() const
{
	if (!frame_.visible) return {};
	const int padding = static_cast<int>(ceil((BarButtonFrameThicknessDip * frame_.pose.scale + BarRenderingAttribute::pointLightDiffuseExtraWidth) * frame_.zoom)) + BarRenderingAttribute::dirtyAntialiasPadding;
	return IntegerRect(frame_.panel, frame_.zoom, padding);
}
void BarGeometryAttributePanel::CommitPresented() { std::lock_guard lock(presentationMutex_); presented_ = frame_; }
GeometryAttributePresentation BarGeometryAttributePanel::PresentationSnapshot() const { std::lock_guard lock(presentationMutex_); return presented_; }
std::array<RECT,1> BarGeometryAttributePanel::PresentedRegions() const
{
	const auto f = PresentationSnapshot(); return {f.visible ? IntegerRect(f.panel, f.zoom) : RECT{}};
}
void BarGeometryAttributePanel::Close(BarUISetClass& owner)
{
	owner.barState.geometryAttribute = false; visible_ = false; ResetPointerFeedback(); focused_ = -1; StopCarousel();
}
bool BarGeometryAttributePanel::ResetPointerFeedback()
{
	const bool changed = hovered_ >= 0 || pressed_ >= 0;
	hovered_ = -1; pressed_ = -1; pressCancelled_ = true; return changed;
}
void BarGeometryAttributePanel::Execute(BarUISetClass& owner, int item, std::uint64_t revision)
{
	if (StateModeTransitionRevision() != revision) return;
	if (item < CategoryCount) SetShapeToolPresetIfRevision(Categories[item], revision);
	else if (item == CircleCenter || item == CircleBox) SetShapeCircleModeIfRevision(item == CircleCenter ? CreationMode::CenterRadius : CreationMode::Drag, revision);
	else if (item >= SidesBegin && item < StyleBegin) SetShapePolygonSidesIfRevision(item - SidesBegin + 3, revision);
	else if (item >= StyleBegin && item < PatternBegin) SetShapeFillModeIfRevision(static_cast<draw3::shapes::FillMode>(item - StyleBegin), revision);
	else if (item >= PatternBegin && item < ThicknessBegin) SetShapeLinePatternIfRevision(static_cast<LinePattern>(item - PatternBegin), revision);
	else if (item >= ThicknessBegin && item < FineAdjustment) ApplyBarThicknessPresetIfRevision(item - ThicknessBegin, revision);
	else if (item == FineAdjustment)
	{
		owner.BeginGeometryAttributeEdit(true); StopCarousel();
	}
	else if (item == ColorButton)
	{
		owner.BeginGeometryAttributeEdit(false); StopCarousel();
	}
	else if (item == CloseButton) Close(owner);
	owner.UpdateRendering();
}
bool BarGeometryAttributePanel::Pointer(BarUISetClass& owner, const ExMessage& message, bool cancelled, bool contactPointer)
{
	if (cancelled) { const bool owned = ResetPointerFeedback(); if (owned) owner.UpdateRendering(false); return owned; }
	const auto f = PresentationSnapshot(); if (!f.visible && pressed_ < 0) return false;
	const double x = message.x / f.zoom, y = message.y / f.zoom;
	const bool inside = f.visible && BarUiRoundedRectContainsPoint(message.x, message.y, f.zoom,
		f.panel.left, f.panel.top, f.panel.Width(), f.panel.Height(), BarMainBarCornerRadiusDip * f.pose.scale, BarMainBarCornerRadiusDip * f.pose.scale);
	int hit = -1;
	if (inside && f.acceptsInput) for (size_t i = 0; i < f.hits.size(); ++i) if (HitTestBarButton(f.hits[i], x, y)) { hit = static_cast<int>(i); break; }
	if (message.message == WM_MOUSEMOVE)
	{
		if (pressed_ >= 0 && hit != pressed_) pressCancelled_ = true;
		const int hover = pressed_ >= 0 ? -1 : hit;
		if (hovered_ != hover || pressed_ >= 0) { hovered_ = hover; focused_ = -1; owner.UpdateRendering(false); }
		return inside || pressed_ >= 0;
	}
	if (message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK)
	{
		const auto state = GetStateModeVersionedSnapshot();
		pressed_ = owner.barState.geometryAttribute && state.state.StateModeSelect == StateModeSelectEnum::IdtShape ? hit : -1;
		pressRevision_ = state.revision; pressCancelled_ = false; hovered_ = -1; focused_ = -1;
		if (pressed_ >= 0) StopCarousel(); owner.UpdateRendering(false); return inside;
	}
	if (message.message == WM_LBUTTONUP)
	{
		const int pressed = pressed_; pressed_ = -1;
		if (pressed >= 0 && hit == pressed && !pressCancelled_ && owner.barState.geometryAttribute) Execute(owner, pressed, pressRevision_);
		// 接触输入和鼠标都等待新的 Move 恢复悬停，释放回弹不被 hover 抢占。
		hovered_ = -1; if (contactPointer) focused_ = -1;
		if (pressed >= 0) owner.UpdateRendering(false); return inside || pressed >= 0;
	}
	return inside;
}
bool BarGeometryAttributePanel::Keyboard(BarUISetClass& owner, BYTE key, bool down)
{
	if (!owner.barState.geometryAttribute) return false;
	if (key == VK_ESCAPE) { if (down) { Close(owner); owner.UpdateRendering(false); } return true; }
	if (key != VK_TAB && key != VK_LEFT && key != VK_RIGHT && key != VK_RETURN && key != VK_SPACE) return false;
	if (!down) return true;
	const auto f = PresentationSnapshot(); if (!f.acceptsInput) return true;
	if (key == VK_RETURN || key == VK_SPACE)
	{
		if (focused_ >= 0) Execute(owner, focused_, GetStateModeVersionedSnapshot().revision); return true;
	}
	const int delta = key == VK_LEFT || (key == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) ? -1 : 1;
	int focus = focused_; for (size_t attempts = 0; attempts < buttons_.size(); ++attempts)
	{
		focus = (focus + delta + static_cast<int>(buttons_.size())) % static_cast<int>(buttons_.size());
		if (f.hits[focus].visible) break;
	}
	focused_ = focus; hovered_ = -1; owner.UpdateRendering(false); return true;
}
