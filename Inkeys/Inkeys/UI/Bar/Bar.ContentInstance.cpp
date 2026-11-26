module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include <d2d1_1helper.h>
#include <dwrite_1.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

module Inkeys.UI.Bar;
import :ContentInstance;
import :Main;
import :MainContent;
import Inkeys.UI.Bar.LayoutModel;
import Inkeys.UI.Bar.MainLayoutPolicy;
import Inkeys.UI.Bar.A2;
import Inkeys.UI.UI3.FramePacing;
import Inkeys.UI.UI3.Material;
import Inkeys.Other.Config;

using namespace Inkeys::UI::UI3;
extern const double BarDrawAttributeSurfaceOpacity;
extern const double BarGeometryAttributeDividerCursorLightIntensity;
extern const double BarMorePanelGap;
extern const double BarMorePanelAnchorGap;
extern const double BarMorePanelPadding;
extern const double BarMorePanelCloseSideWidth;
extern const double BarMorePanelSeparatorGap;
extern const double BarMorePanelCompactScale;
extern const double BarMorePanelCompactWidth;
extern const double BarMorePanelCompactHeight;

namespace Inkeys::UI::Bar
{
	namespace
	{
		struct MoreGridSize { double width, height, gridHeight; };

		MoreGridSize MeasureMoreGrid(const BarButtonListLayout& packed, double moreGap)
		{
			const int subColumns = packed.subColumns, totalRows = packed.totalRows;
			const bool showDivider = packed.showDivider;
			constexpr double one = ButtonOneSideDip;
			double contentWidth = subColumns * one
				+ max(0, subColumns - 1) * moreGap;
			double gridHeight = totalRows * one
				+ max(0, totalRows - 1) * moreGap
				+ (showDivider ? BarMorePanelSeparatorGap : 0.0);
			double panelWidth = contentWidth + BarMorePanelPadding * 2.0
				+ BarMorePanelCloseSideWidth;
			double panelHeight = BarMorePanelPadding * 2.0 + gridHeight;
			return {panelWidth, panelHeight, gridHeight};
		}
	}

	void PositionContentMore(MoreChrome chrome, BarButtonClass* moreButton, UiShapeClass& body,
		const BarMoreButtonSnapshotClass& snapshot, BarSizeMode sizeMode,
		UiValueClass& progress, UiValueClass& opacity, bool open, bool side,
		double windowWidth, double windowHeight, double frameZoom, std::optional<MoreSideLimit> limit,
		bool dark, bool closePressed, IdtAtomic<ButtonHoverStageEnum>& closeHover, UIRendering& labelRenderer, std::optional<double> viewportMargin)
	{
		auto [panel, divider, close, closeSvg, pressScale] = chrome;
		const auto& sizeMetrics = ResolveBarSizeMetrics(sizeMode);
		const auto packed = Inkeys::UI::Bar::ResolveBarButtonListLayout(snapshot.explicitMore,
			Inkeys::UI::Bar::BarButtonListLayoutKind::GroupedGrid, snapshot.forcedOverflow, sizeMode);
		const auto& placements = packed.placements;
		const int explicitRows = packed.explicitRows;
		const bool showDivider = packed.showDivider;

		constexpr double one = ButtonOneSideDip;
		// 小尺寸网格间距取档位参数（现与大尺寸同为 5），面板内边距与关闭栏保持原样。
		const bool compactMore = sizeMode == BarSizeMode::Compact;
		const double moreGap = compactMore ? sizeMetrics.gap : BarMorePanelGap;
		const double step = ButtonOneSideDip + (compactMore ? sizeMetrics.gap : ButtonGapDip);
		const auto [panelWidth, panelHeight, gridHeight] = MeasureMoreGrid(packed,moreGap);
		double compactWidth = BarMorePanelCompactWidth;
		double compactHeight = BarMorePanelCompactHeight;
		double rawProgress = static_cast<double>(progress.val);
		double opacityProgress = clamp(
			static_cast<double>(opacity.val), 0.0, 1.0);
		bool settleHiddenButtonVisuals = !open
			&& progress.IsSame() && opacity.IsSame()
			&& rawProgress <= 0.000001;
		// 根面板从 More 按钮中心的紧凑态展开，和绘制属性/几何面板共享锚点语义。
		double scale = max(0.01, BarMorePanelCompactScale
			+ (1.0 - BarMorePanelCompactScale) * rawProgress);
		// 字号取整只看面板展开态，不追逐每帧 SetDirect 的当前尺寸，也不取收起端紧凑倍率：
		// 否则约 2px 的收起目标取整误差会以另一条曲线插值，造成展开到位后放大、收起时偏小。
		// 后续用户展开倍率落地时，scale 公式的展开端与此处同取该倍率。
		const double fontTargetScale = 1.0;
		double logicalWindowWidth = frameZoom > 0.0
			? static_cast<double>(windowWidth) / frameZoom : panelWidth;
		double logicalWindowHeight = frameZoom > 0.0
			? static_cast<double>(windowHeight) / frameZoom : panelHeight;
		double anchorX = moreButton
			? moreButton->button.inhX + moreButton->button.w.val / 2.0
			: logicalWindowWidth / 2.0;
		double anchorY = moreButton
			? moreButton->button.inhY + moreButton->button.h.val / 2.0
			: logicalWindowHeight / 2.0;
		// 非居中底栏：面板不越过主按钮外侧边，未越过时仍以 More 按钮居中；窗口夹取在其后。
		double expandedAnchorX = anchorX;
		if (limit)
			expandedAnchorX = ClampBarAttributePanelLeft(anchorX - panelWidth / 2.0, panelWidth,
				limit->left, limit->right, limit->side) + panelWidth / 2.0;
		double expandedCenterX = clamp(expandedAnchorX,
			BarMorePanelPadding + panelWidth / 2.0,
			max(BarMorePanelPadding + panelWidth / 2.0,
				logicalWindowWidth - BarMorePanelPadding - panelWidth / 2.0));
		double direction = side ? 1.0 : -1.0;
		// 大尺寸相对 More 按钮边缘留 12；小尺寸与其他属性窗一致，相对主栏边缘留同一间隙。
		double expandedCenterY = anchorY + direction * (compactMore
			? body.h.val / 2.0 + sizeMetrics.attachedGap + panelHeight / 2.0
			: (moreButton ? moreButton->button.h.val / 2.0 : 35.0)
				+ BarMorePanelAnchorGap + panelHeight / 2.0);
		expandedCenterY = clamp(expandedCenterY,
			BarMorePanelPadding + panelHeight / 2.0,
			max(BarMorePanelPadding + panelHeight / 2.0,
				logicalWindowHeight - BarMorePanelPadding - panelHeight / 2.0));
		double panelCenterX = anchorX
			+ (expandedCenterX - anchorX) * rawProgress;
		double panelCenterY = anchorY
			+ (expandedCenterY - anchorY) * rawProgress;
		double displayWidth = max(1.0,
			compactWidth + (panelWidth - compactWidth) * rawProgress);
		double displayHeight = max(1.0,
			compactHeight + (panelHeight - compactHeight) * rawProgress);
		if (viewportMargin)
		{
			// Back 曲线可能越过终点，按本帧实际尺寸给预览阴影留足空间。
			const double left = *viewportMargin + displayWidth/2, top = *viewportMargin + displayHeight/2;
			panelCenterX = std::clamp(panelCenterX,left,(std::max)(left,logicalWindowWidth-left));
			panelCenterY = std::clamp(panelCenterY,top,(std::max)(top,logicalWindowHeight-top));
		}
		panel->x.SetDirect(panelCenterX);
		panel->y.SetDirect(panelCenterY);
		panel->w.SetDirect(displayWidth);
		panel->h.SetDirect(displayHeight);
		panel->rw->SetDirect(8.0 * scale);
		panel->rh->SetDirect(8.0 * scale);
		panel->ft->SetDirect(scale);
		panel->fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));
		panel->frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
		panel->pct.SetDirect(BarDrawAttributeSurfaceOpacity * opacityProgress);
		panel->framePct->SetDirect(0.18 * opacityProgress);
		panel->UpInh(UiInheritClass(
			panelCenterX - displayWidth / 2.0,
			panelCenterY - displayHeight / 2.0));
		// 子内容围绕完整面板中心统一缩放，避免紧凑态被非等比的根面板宽高拉偏。
		auto ScalePanelContentX = [&](double logicalX)
			{
				return panelCenterX + (logicalX - panelWidth / 2.0) * scale;
			};
		auto ScalePanelContentY = [&](double logicalY)
			{
				return panelCenterY + (logicalY - panelHeight / 2.0) * scale;
			};
		double panelCenterInMainBarX = moreButton
			? moreButton->button.x.val + panelCenterX - anchorX
			: panelCenterX
				- body.inhX;
		double panelCenterInMainBarY = moreButton
			? moreButton->button.y.val + panelCenterY - anchorY
			: panelCenterY
				- body.inhY;

		close->w.SetDirect(30.0 * scale);
		close->h.SetDirect(30.0 * scale);
		close->rw->SetDirect(4.0 * scale);
		close->rh->SetDirect(4.0 * scale);
		// X 位于按钮区域右侧窄栏的右上角，关闭时不增加面板高度。
		close->x.SetDirect(ScalePanelContentX(
			panelWidth - BarMorePanelCloseSideWidth / 2.0));
		close->y.SetDirect(ScalePanelContentY(
			BarMorePanelPadding + 15.0));
		// 图标透明度继续跟随面板进度，按钮入口只接管背景反馈。
		Inkeys::UI::UI3::RetargetFixedButtonVisual(dark,
			{*close, nullptr, nullptr, &pressScale, &closeHover},
			open, true, false, closePressed, UiDefaultOperationDur);
		close->UpInh(UiInheritClass(
			close->x.val - close->w.val / 2.0,
			close->y.val - close->h.val / 2.0));
		closeSvg->x.SetDirect(close->x.val);
		closeSvg->y.SetDirect(close->y.val);
		closeSvg->SetWH(18.0 * scale, 18.0 * scale);
		closeSvg->color1->SetDirect(GetThemeColor(ThemeColorEnum::IconPrimary));
		closeSvg->pct.SetDirect(opacityProgress);
		closeSvg->UpInh(UiInheritClass(
			closeSvg->x.val - closeSvg->w.val / 2.0,
			closeSvg->y.val - closeSvg->h.val / 2.0));

		double explicitBlockHeight = explicitRows > 0
			? explicitRows * one + max(0, explicitRows - 1) * moreGap
			: 0.0;
		double dividerLogicalY = explicitBlockHeight
			+ (moreGap + BarMorePanelSeparatorGap) / 2.0;
		double dividerPhysicalY = side
			? gridHeight - dividerLogicalY : dividerLogicalY;
		// 分割线忽略 X 侧栏，直接延伸到面板两侧的等距内边距。
		divider->w.SetDirect(
			(panelWidth - BarMorePanelPadding * 2.0) * scale);
		divider->h.SetDirect(scale);
		divider->x.SetDirect(ScalePanelContentX(panelWidth / 2.0));
		divider->y.SetDirect(ScalePanelContentY(
			BarMorePanelPadding + dividerPhysicalY));
		divider->pct.SetDirect(showDivider ? 0.30 * opacityProgress : 0.0);
		divider->UpInh(UiInheritClass(
			divider->x.val - divider->w.val / 2.0,
			divider->y.val - divider->h.val / 2.0));

		for (const auto& placement : placements)
		{
			BarButtonClass* button = placement.button.get();
			if (!button) continue;
			double width = (placement.columnSpan == 2 ? ButtonTwoSideDip : one);
			double height = (placement.rowSpan == 2 ? ButtonTwoSideDip : one);
			if (button->size == ButtonSizeEnum::oneTwo) width = 10.0;
			double logicalX = BarMorePanelPadding
				+ placement.column * step + width / 2.0;
			double logicalY = placement.row * step;
			if (showDivider && placement.forced)
				logicalY += BarMorePanelSeparatorGap;
			double physicalY = side
				? gridHeight - logicalY - height : logicalY;
			double localCenterY = BarMorePanelPadding
				+ physicalY + height / 2.0;
			// 始终保存主栏局部坐标：收起后按钮缩在 More 入口下方，补位时不会从远端飘入。
			button->button.x.SetDirect(panelCenterInMainBarX
				+ (logicalX - panelWidth / 2.0) * scale);
			button->button.y.SetDirect(panelCenterInMainBarY
				+ (localCenterY - panelHeight / 2.0) * scale);
			if (button->size == ButtonSizeEnum::oneTwo)
			{
				button->button.w.SetDirect(width * scale);
				button->button.h.SetDirect(height * scale);
				button->icon.pct.SetDirect(0.0); button->name.pct.SetDirect(0.0);
				continue;
			}
			const auto kind = button->size == ButtonSizeEnum::oneOne
				? ButtonVisualLayoutKind::StandardOneOne
				: button->size == ButtonSizeEnum::twoOne
					? ButtonVisualLayoutKind::StandardTwoOne : ButtonVisualLayoutKind::StandardTwoTwo;
			const auto visualMetrics = ResolveButtonVisualMetrics(kind);
			const double labelSize = button->LabelFontSize(visualMetrics.primaryFontSizeDip);
			UI3::ConfigureButtonVisualTargets(*button, kind, UiDefaultOperationDur, scale, true,
				nullopt, fontTargetScale, labelSize, button->preset == BarButtonPresetEnum::Geometry
					&& visualMetrics.primarySlotWidthDip > 4 && labelSize > 0 ? &labelRenderer : nullptr);
			const bool enabled = button->state->state != WidgetState::Disable;
			const bool selected = button->state->state == WidgetState::Selected;
			UI3::RetargetButtonInteractionVisual(dark, *button, open,
				enabled, selected, UiDefaultOperationDur);
			if (settleHiddenButtonVisuals)
			{
				button->button.fill->SetDirect(GetThemeColor(selected
					? ThemeColorEnum::SelectedFill : ThemeColorEnum::PressedFill));
				const COLORREF color = GetThemeColor(selected
					? ThemeColorEnum::Accent : ThemeColorEnum::TextPrimary);
				const COLORREF iconColor = GetThemeColor(selected
					? ThemeColorEnum::Accent : ThemeColorEnum::IconPrimary);
				button->button.frame->SetDirect(color);
				if (button->icon.color1) button->icon.color1->SetDirect(iconColor);
				if (button->icon.color2) button->icon.color2->SetDirect(iconColor);
				button->name.color.SetDirect(color);
			}
			const double contentOpacity = opacityProgress * (enabled ? 1.0 : GetThemeDisabledContentOpacity());
			button->icon.pct.SetDirect(contentOpacity);
			button->name.pct.SetDirect(button->size == ButtonSizeEnum::oneOne ? 0.0 : contentOpacity);
			button->lastDrawX = button->button.x.val;
			button->lastDrawY = button->button.y.val;
		}
	}

	void DrawContentBody(UIRendering& renderer, ID2D1DeviceContext* dc, Panel& panel, UiShapeClass& body,
		const std::vector<std::shared_ptr<BarButtonClass>>& buttons, const std::vector<BarMainButtonLayoutEntry>& layout,
		RECT* bounds, double idleFade)
	{
		renderer.SetResidentIdleFade(idleFade);
		panel.BindFrame(body);
		panel.DrawFrame(renderer, dc, UiInheritClass(
				body.inhX, body.inhY),
			bounds, true);

		// 退场按钮先画，收拢时由仍可见的前驱覆盖，不改变列表与命中顺序。
		// 主栏图标/文字属于常驻内容，受用户内容透明度控制；选中底板等形状不受影响。
		renderer.SetResidentContentOpacity(GetThemeResidentContentOpacity());
		for (const bool visible : { false, true })
		{
			size_t layoutIndex = 0;
			for (const auto& button : buttons)
			{
				if (!button) continue;
				const bool frameVisible = layout.at(layoutIndex++).visible;
				if (frameVisible != visible) continue;
				const auto inherit = button->button.Inherit(UiInheritEnum::CenterFromTopLeft,
					body);
				DrawButtonVisual(renderer, dc, *button, inherit);
			}
		}
		renderer.SetResidentContentOpacity(1.0);
		renderer.SetResidentIdleFade(0);
	}

	void DrawContentMore(UIRendering& renderer, ID2D1DeviceContext* dc, Panel& panel, MoreChrome chrome,
		UiShapeClass& body, const BarMoreButtonSnapshotClass& buttons, RECT* bounds)
	{
		auto [morePanel, moreDivider, moreClose, moreCloseSvg, press] = chrome;
		panel.BindFrame(*morePanel);
		panel.DrawFrame(renderer, dc,
			UiInheritClass(morePanel->inhX, morePanel->inhY),
			bounds, true);
		if (moreDivider && moreDivider->pct.val > 0.000001)
			renderer.Shape(dc, *moreDivider,
				UiInheritClass(moreDivider->inhX,
					moreDivider->inhY));

		auto DrawMoreButton = [&](BarButtonClass* button)
			{
				if (!button || button->icon.pct.val <= 0.000001) return;
				UiInheritClass buttonInherit = button->button.Inherit(
					UiInheritEnum::CenterFromTopLeft,
					body);
				DrawButtonVisual(renderer, dc,
					*button, buttonInherit);
			};
		for (const shared_ptr<BarButtonClass>& button :
			buttons.explicitMore)
			DrawMoreButton(button.get());
		for (const shared_ptr<BarButtonClass>& button :
			buttons.forcedOverflow)
			DrawMoreButton(button.get());

		if (moreClose && moreCloseSvg)
			Inkeys::UI::UI3::DrawFixedButtonVisual(renderer, dc,
				{*moreClose, moreCloseSvg, nullptr, &press},
				{UiInheritClass(moreClose->inhX, moreClose->inhY),
					UiInheritClass(moreCloseSvg->inhX, moreCloseSvg->inhY), UiInheritClass(0.0, 0.0), std::nullopt});
	}

	void DrawContentHint(UIRendering& renderer, ID2D1DeviceContext* dc, const BarHoverHintRect& r,
		const std::wstring& text, double frameZoom, double light, std::optional<UiShapeClass>& shape,
		FormatCache& cache, IDWriteFontCollection* fonts)
	{
		const double radius = BarCompactSizeMetrics.barCornerRadius;
		auto& surface = shape.emplace(0.0, 0.0, (r.right - r.left) / frameZoom, (r.bottom - r.top) / frameZoom,
			radius, radius, ButtonFrameThicknessDip,
			GetThemeColor(ThemeColorEnum::Surface), GetThemeColor(ThemeColorEnum::SurfaceFrame));
		surface.enable.Initialization(true);
		surface.pct.Initialization(BarMainBarFillOpacity);
		surface.framePct = UiPctClass(GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity));
		surface.themeSurface = true;
		surface.lightMaterial = light;
		renderer.Shape(dc, surface, UiInheritClass(r.left / frameZoom, r.top / frameZoom));
		IDWriteTextFormat* hintFormat = cache.GetFormat(
			renderer.GetFontFamily(), renderer.ResolveFontSize(dc,
				Inkeys::UI::Bar::BarHoverHintFontSizeDip * frameZoom, Inkeys::UI::Bar::BarHoverHintFontSizeDip),
			fonts, DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, renderer.GetTextLocale(),
			DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		if (auto hintBrush = renderer.GetFrameSolidColorBrush(dc,
			GetThemeColor(ThemeColorEnum::TextPrimary), 1.0); hintBrush && hintFormat)
			(Inkeys::UI::UI3::ObserveUi3SvgUnknownWrite(dc), dc)->DrawTextW(
				text.c_str(), static_cast<UINT32>(text.size()), hintFormat,
				D2D1::RectF(static_cast<FLOAT>(r.left), static_cast<FLOAT>(r.top),
					static_cast<FLOAT>(r.right), static_cast<FLOAT>(r.bottom)),
				hintBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

	namespace
	{
		using ButtonPtr = std::shared_ptr<BarButtonClass>;
		struct PreviewPrototypes : BarButtonSetClass
		{
			using BarButtonSetClass::ToRuntimeSize;
		};
		bool Contains(D2D1_RECT_F r, D2D1_POINT_2F p)
		{
			return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
		}
		bool SamePosition(Position a, Position b) { return a.segment == b.segment && a.index == b.index; }
		ButtonPtr Clone(const BarButtonClass& prototype)
		{
			auto b = std::make_shared<BarButtonClass>(prototype);
			b->state = &b->localState;
			b->clickFunc = {};
			b->hoverStage = ButtonHoverStageEnum::None;
			b->pressScale.SetDirect(1);
			return b;
		}
		void RetargetPressScale(UiValueClass& scale, bool pressed)
		{
			scale.SetTar(pressed ? ButtonPressScale : 1.0, UiDefaultOperationDur, std::nullopt, false,
				pressed ? ButtonPressCurve() : ButtonReleaseCurve());
		}
		D2D1_RECT_F Pixels(const UiShapeClass& shape, double scale)
		{
			return D2D1::RectF(static_cast<float>(shape.inhX * scale), static_cast<float>(shape.inhY * scale),
				static_cast<float>((shape.inhX + shape.w.val) * scale), static_cast<float>((shape.inhY + shape.h.val) * scale));
		}
	}

	struct BarContentInstance::State
	{
		BarLayoutModel model;
		std::uint64_t revision = 0, serial = 0, drawn = 0;
		std::optional<Position> lifted;
		bool expanded = false, initialized = false, styleDirty = false;
		// 抓起期间锁住相机适配宽度（抓起前的目标宽），避免窄宿主里底板越缩、按钮越大。
		double editWidth = 0, targetWidth = 0;
		// 沙盒状态：默认绘制模式（软笔）；样式键变化时才重新解析按钮内容。
		StateModeSelectEnum mode = StateModeSelectEnum::IdtPen;
		std::optional<long long> styleKey;
		std::unique_ptr<PreviewPrototypes> prototypes;
		std::array<std::vector<ButtonPtr>, 4> entries;
		std::unordered_map<std::string, ButtonPtr> singletons;
		ButtonPtr boundary[2], more, redo, pierce, ghost;
		std::string ghostId;
		// main 与真实主栏 buttonList 同序且保留隐藏实例；visible 与 main 逐项对应。
		std::vector<ButtonPtr> main;
		std::vector<BarMainButtonLayoutEntry> visible;
		std::unordered_map<BarButtonClass*, PreviewHit> identities;
		BarMoreButtonSnapshotClass panelButtons;
		Microsoft::WRL::ComPtr<ID2D1DrawingStateBlock1> drawingState;
		UiShapeClass body, panel, divider, close;
		UiSVGClass closeSvg;
		UiValueClass progress{0}, opacity{0}, press{1};
		// 相机只读目标布局：缩放与主栏上方预留随目标变化平滑过渡，从不读取动画中的宽度或 More 进度。
		UiValueClass cameraZoom{1}, cameraTop{0};
		MainBarBatchState batch;
		RecallChromeView recall;
		IdtAtomic<ButtonHoverStageEnum> closeHover = ButtonHoverStageEnum::None;
		Panel mainPanel{"main"}, morePanel{"more"};
		LightingStateMachine lighting;
		FrameAnimationClock clock;
		PreviewGeometry pending, presented;
		PreviewInputs inputs;
		PreviewFrame frame;
		std::optional<UiShapeClass> hintShape;
		BarHoverHintRect hintRect;
		MainBarHoverState hint;
		MainBarHoverSource hintSource;
		std::uint64_t hintSerial = 0;
		ButtonPtr hovered;
		// 按压中的实例（或 X）；松开后悬停暂停到下一次指针移动，与真实主栏一致。
		ButtonPtr pressed;
		bool closePressed = false;
		std::optional<std::uint64_t> hoverSuppressedSerial;
		std::unique_ptr<FormatCache> formats;
		UIRendering renderer{ UIRenderingServices{
			[this] { return formats.get(); }, [this] { ResetCaches(); },
			[this] { return inputs.typography.revision; }, [this] { return inputs.typography; } } };

		State()
		{
			body.Initialization(0,0,80,80,8,8,ButtonFrameThicknessDip,GetThemeColor(ThemeColorEnum::Surface),GetThemeColor(ThemeColorEnum::SurfaceFrame));
			panel = body;
			for (auto* shape : {&body,&panel})
			{
				shape->enable.Initialization(true);
				shape->frameRendering = UiFrameRenderingEnum::PointLight;
				shape->frameLightColor = UiFrameLightColorEnum::PenWhenDrawing;
				shape->themeSurface = true;
				shape->framePct = UiPctClass(BarMainBarFrameOpacity);
			}
			body.pct.SetDirect(BarMainBarFillOpacity);
			body.residentSurface = true;
			body.lightFillOpacityScale = ThemeMaterial::LightResidentSurfaceOpacityScale;
			divider.Initialization(0,0,1,1,0.5,0.5,1,GetThemeColor(ThemeColorEnum::SurfaceFrame),nullopt);
			close.Initialization(0,0,30,30,4,4,1,GetThemeColor(ThemeColorEnum::PressedFill),nullopt);
			close.framePct = UiPctClass(0);
			divider.enable.Initialization(true);
			close.enable.Initialization(true);
			closeSvg.Initialization(0,0,GetThemeColor(ThemeColorEnum::IconPrimary),nullopt);
			closeSvg.InitializationFromResource(L"UI", L"barCloseSmall");
			closeSvg.enable.Initialization(true);
		}

		MoreChrome Chrome() { return {&panel,&divider,&close,&closeSvg,press}; }
		void ResetCaches()
		{
			drawingState.Reset();
			if (formats) formats->CleanAll();
			auto reset = [](const ButtonPtr& b)
			{
				if (!b) return;
				b->icon.ResetCache();
				b->pngIcon.ResetCache();
				if (b->badge) b->badge->svg.ResetCache();
			};
			for (auto& zone : entries) for (auto& b : zone) reset(b);
			for (const auto& entry : singletons) reset(entry.second);
			for (auto& b : {boundary[0],boundary[1],more,redo,pierce}) reset(b);
			reset(ghost);
			closeSvg.ResetCache();
		}
		void PreparePrototypes()
		{
			ghost.reset();
			prototypes = std::make_unique<PreviewPrototypes>();
			prototypes->PresetInitialization();
			prototypes->RegisterBuiltInComponents();
			auto refresh = [](ButtonPtr& slot, const BarButtonClass* prototype)
			{
				if (!prototype) return;
				if (slot) slot->name.SetStringImmediate(prototype->name.content.GetTar());
				else slot = Clone(*prototype);
			};
			const auto* line = prototypes->preset[static_cast<int>(BarButtonPresetEnum::Divider)];
			for (auto& b : boundary) refresh(b,line);
			refresh(redo,prototypes->preset[static_cast<int>(BarButtonPresetEnum::Redo)]);
			refresh(pierce,prototypes->preset[static_cast<int>(BarButtonPresetEnum::Pierce)]);
			refresh(more,prototypes->GetMoreButton());
			// 原型刷新后按当前语言与沙盒状态重新解析样式。
			styleKey.reset();
		}
		// 与真实主栏 Update*ButtonStyle 共用解析；沙盒没有画布内容、白板与放映，绘制为软笔。
		void ApplyStyle(BarButtonClass& b, bool immediate, const StateModeClass& snapshot, bool separate)
		{
			std::optional<MainBarContentStyle> style;
			switch (b.preset.load())
			{
			case BarButtonPresetEnum::Select:
				style = ResolveSelectStyle(false, separate, mode == StateModeSelectEnum::IdtSelection);
				break;
			case BarButtonPresetEnum::Draw:
				style = ResolveDrawStyle(mode == StateModeSelectEnum::IdtPen, false, false, false);
				break;
			case BarButtonPresetEnum::Eraser:
				style = ResolveEraserStyle(mode == StateModeSelectEnum::IdtEraser);
				break;
			case BarButtonPresetEnum::Geometry:
			{
				const bool selected = mode == StateModeSelectEnum::IdtShape;
				style = ResolveGeometryStyle(selected, selected && IsShapeToolPinned(snapshot), GetShapeToolSpec(snapshot));
				if (b.badge) b.badge->shown = style->badge;
				break;
			}
			case BarButtonPresetEnum::Whiteboard: style = ResolveWhiteboardStyle(false, false, false); break;
			case BarButtonPresetEnum::Pierce: style = ResolvePierceStyle(); break;
			default: break;
			}
			if (style) b.TransitionContent(style->icon, style->LocalizedLabel(I18n::getW, ShapeToolLabel), immediate);
		}
		long long StyleKey(const StateModeClass& snapshot, bool separate) const
		{
			const auto tool = GetShapeToolSpec(snapshot);
			return static_cast<long long>(mode) + 16LL * static_cast<int>(tool.preset)
				+ 4096LL * static_cast<int>(tool.creationMode) + 65536LL * static_cast<int>(tool.sides)
				+ (separate ? 1LL << 40 : 0) + (IsShapeToolPinned(snapshot) ? 1LL << 41 : 0);
		}
		void RefreshLabel(const std::string& id, BarButtonClass& button) const
		{
			BarButtonRegistrationClass registration;
			if (prototypes->TryGetRegistration(id, registration) && registration.button)
				button.name.SetStringImmediate(registration.button->name.content.GetTar());
		}
		ButtonPtr Create(const BarButtonClass& prototype)
		{
			auto button = Clone(prototype);
			ApplyStyle(*button, true, GetStateModeSnapshot(), GetSelectAndClickThroughMode() == 1);
			return button;
		}
		ButtonPtr Make(const std::string& id, bool forLayout = false)
		{
			BarButtonRegistrationClass registration;
			if (!prototypes->TryGetRegistration(id, registration) || !registration.button) return {};
			if (!forLayout || registration.allowMultiple) return Create(*registration.button);
			auto& button = singletons[id];
			if (!button) button = Create(*registration.button);
			return button;
		}
		bool HasVisibleMoreItems(const BarLayoutModel& layout) const
		{
			const auto& items = layout[Segment::More];
			const auto& buttons = entries[static_cast<size_t>(Segment::More)];
			for (size_t i = 0; i < items.size(); ++i)
				if (buttons[i] && items[i].Visible) return true;
			return false;
		}
		void ClearHint()
		{
			// 布局替换后，静止指针不能沿用旧实例的提示；下次移动再登记。
			hint = {};
			hintSource = {};
			hintSerial = inputs.pointerSerial;
		}
		void Rebuild(const BarLayoutModel& next)
		{
			if (!prototypes) PreparePrototypes();
			for (size_t zone = 0; zone < entries.size(); ++zone)
			{
				const auto& previous = model.segments[zone];
				const auto& items = next.segments[zone];
				std::unordered_map<std::string, std::vector<size_t>> oldGroups, newGroups;
				std::vector<ButtonPtr> ordered(items.size());
				for (size_t index = 0; index < entries[zone].size(); ++index)
					oldGroups[previous[index].Id].push_back(index);
				for (size_t index = 0; index < items.size(); ++index)
				{
					const auto& id = items[index].Id;
					BarButtonRegistrationClass registration;
					if (!prototypes->TryGetRegistration(id, registration) || !registration.button) continue;
					if (registration.allowMultiple) newGroups[id].push_back(index);
					else ordered[index] = Make(id, true);
				}
				for (const auto& [id, group] : newGroups)
				{
					const auto old = oldGroups.find(id);
					// 与实栏一致：同段单份可换位置；多份只有位置和可见性均对应才复用。
					const bool reuse = old != oldGroups.end() && old->second.size() == group.size()
						&& (group.size() == 1 || std::equal(group.begin(), group.end(), old->second.begin(),
							[&](size_t current, size_t prior) { return current == prior && items[current].Visible == previous[prior].Visible; }));
					for (size_t index = 0; index < group.size(); ++index)
						ordered[group[index]] = reuse ? entries[zone][old->second[index]] : Make(id, true);
				}
				entries[zone] = std::move(ordered);
			}
			if (!HasVisibleMoreItems(next)) expanded = false;
			// 新加入或重新启用的按钮须使用当前沙盒样式，不取离开布局前的样式。
			styleDirty = true;
		}
		void Invalidate()
		{
			drawn = 0;
			frame.frameId = 0;
			presented = {};
		}
		BarButtonClass* Preset(BarButtonPresetEnum id) const
		{
			for (const auto& b : main)
				if (b && b->preset == id) return b.get();
			return nullptr;
		}
		// 命中项映射回按钮实例（主栏与 More 面板）；只有可交互的按钮返回实例。
		ButtonPtr ButtonAt(const PreviewHit& hit) const
		{
			if (hit.kind == PreviewHitKind::More) return more;
			if (hit.kind != PreviewHitKind::Item && hit.kind != PreviewHitKind::Companion) return {};
			if (hit.position && hit.position->segment == Segment::More && !expanded) return {};
			for (const auto* list : {&main, &panelButtons.explicitMore})
				for (const auto& b : *list)
				{
					const auto found = identities.find(b.get());
					if (found == identities.end() || found->second.kind != hit.kind || !b->IsVisible()) continue;
					if (found->second.position && hit.position && SamePosition(*found->second.position, *hit.position)) return b;
				}
			return {};
		}
		// 与真实主栏 Load 同序：A1（Redo 插在撤回前）、交界线、B1、More、交界线、A2（穿透插在定格前）。
		void Project()
		{
			main.clear(); visible.clear(); panelButtons.explicitMore.clear(); identities.clear();
			const bool compact = inputs.sizeMode == BarSizeMode::Compact;
			const bool selection = mode == StateModeSelectEnum::IdtSelection;
			const bool separate = GetSelectAndClickThroughMode() == 1;
			const auto a2 = ResolveBarA2Projection(inputs.presentation, false, inputs.whiteboardEnabled, separate, selection, inputs.freezeAvailable);
			recall = ResolveRecallChrome({}, selection, compact);
			const MainBarPresentationInput presentation{ mode, false, false, inputs.whiteboardEnabled,
				inputs.freezeAvailable, false, false, false, inputs.settingsVisible, expanded, compact, a2, recall };
			const auto snapshot = GetStateModeSnapshot();
			if (const auto key = StyleKey(snapshot, separate); styleDirty || styleKey != key)
			{
				const bool immediate = !initialized || !styleKey;
				for (auto& zone : entries) for (auto& b : zone) if (b) ApplyStyle(*b, immediate, snapshot, separate);
				if (pierce) ApplyStyle(*pierce, immediate, snapshot, separate);
				styleKey = key;
				styleDirty = false;
			}
			auto apply = [&](const ButtonPtr& b, bool hidden)
			{
				const auto [size, hide, state] = ResolveMainBarPresentation(b->preset.load(), b->baseSize.load(), presentation);
				b->size = size;
				b->userVisible = true;
				// 抓起项保留在列表中隐藏，沿用真实主栏的锚定收拢与退场淡出。
				b->hide = hide || hidden;
				b->state->state = state;
			};
			auto append = [&](const ButtonPtr& b, PreviewHitKind kind, std::optional<Position> pos, bool hidden = false)
			{
				if (!b) return;
				apply(b, hidden);
				identities.emplace(b.get(), PreviewHit{kind,pos});
				if (pos && pos->segment == Segment::More) panelButtons.explicitMore.push_back(b);
				else { main.push_back(b); visible.push_back({b.get(),b->size.load(),b->IsVisible()}); }
			};
			auto appendZone = [&](Segment zone)
			{
				auto& buttons = entries[static_cast<size_t>(zone)];
				for (size_t i = 0; i < buttons.size(); ++i)
				{
					const Position pos{zone,i};
					const auto& b = buttons[i];
					// 与真实主栏一致：用户隐藏的条目不进入运行时列表。
					if (!b || !model[zone][i].Visible) continue;
					b->baseSize = PreviewPrototypes::ToRuntimeSize(model[zone][i].Size);
					const bool hidden = lifted && SamePosition(*lifted,pos);
					const auto key = b->preset.load();
					if (key == BarButtonPresetEnum::Recall) append(redo, PreviewHitKind::Companion, pos, hidden);
					if (key == BarButtonPresetEnum::Freeze) append(pierce, PreviewHitKind::Companion, pos, hidden);
					append(b, key == BarButtonPresetEnum::Divider ? PreviewHitKind::UserDivider : PreviewHitKind::Item, pos, hidden);
				}
			};
			appendZone(Segment::A1);
			append(boundary[0], PreviewHitKind::Boundary, {});
			appendZone(Segment::B1);
			append(more, PreviewHitKind::More, {});
			append(boundary[1], PreviewHitKind::Boundary, {});
			appendZone(Segment::A2);
			appendZone(Segment::More);
		}
		void BuildGeometry();
	};

	D2D1_RECT_F PreviewTestAccess::HintBounds(const BarContentInstance& instance)
	{
		if (!instance.state_->hint.shown) return {};
		const auto& r = instance.state_->hintRect;
		return D2D1::RectF(float(r.left),float(r.top),float(r.right),float(r.bottom));
	}
	void PreviewTestAccess::ShowGeometryBadge(BarContentInstance& instance)
	{
		for (const auto& b : instance.state_->main)
			if (b->preset == BarButtonPresetEnum::Geometry && b->badge) b->badge->shown = true;
	}
	void PreviewTestAccess::AlignMainButtonPixelPhase(BarContentInstance& instance, BarButtonPresetEnum preset, D2D1_POINT_2F reference)
	{
		auto& state = *instance.state_;
		const auto* button = state.Preset(preset);
		if (!button) return;
		const auto phase = [](double coordinate) { return coordinate - std::floor(coordinate); };
		const auto zoom = state.frame.scale;
		const double deltaX = (phase(reference.x) - phase(button->button.inhX * zoom)) / zoom;
		const double deltaY = (phase(reference.y) - phase(button->button.inhY * zoom)) / zoom;
		state.body.UpInh(UiInheritClass(state.body.inhX + deltaX, state.body.inhY + deltaY));
		state.BuildGeometry();
	}
	double PreviewTestAccess::PressTarget(const BarContentInstance& instance, const PreviewHit& hit)
	{
		const auto& state = *instance.state_;
		if (hit.kind == PreviewHitKind::Close) return state.press.tar;
		const auto button = state.ButtonAt(hit);
		return button ? static_cast<double>(button->pressScale.tar) : 0.0;
	}
	double PreviewTestAccess::PressScale(const BarContentInstance& instance, const PreviewHit& hit)
	{
		const auto& state = *instance.state_;
		if (hit.kind == PreviewHitKind::Close) return state.press.val;
		const auto button = state.ButtonAt(hit);
		return button ? static_cast<double>(button->pressScale.val) : 0.0;
	}
	std::optional<PreviewTestAccess::CollapseSample> PreviewTestAccess::SampleCollapse(
		const BarContentInstance& instance, BarButtonPresetEnum preset)
	{
		const auto* button = instance.state_->Preset(preset);
		if (!button) return std::nullopt;
		return CollapseSample{button->button.x.val, button->button.x.tar, button->icon.pct.val};
	}
	bool PreviewTestAccess::IsCollapsedTo(const BarContentInstance& instance, BarButtonPresetEnum hidden, BarButtonPresetEnum anchor)
	{
		const auto* button = instance.state_->Preset(hidden);
		const auto* target = instance.state_->Preset(anchor);
		return button && target && !button->IsVisible() && target->IsVisible()
			&& std::abs(button->button.x.val - target->button.x.val) < 0.001
			&& std::abs(button->button.y.val - target->button.y.val) < 0.001
			&& button->icon.pct.val == 0.0 && button->name.pct.val == 0.0;
	}
	bool PreviewTestAccess::KeepMoreOpenAfterAction(BarContentInstance& instance, const PreviewHit& hit)
	{
		const auto button = instance.state_->ButtonAt(hit);
		if (!button) return false;
		button->closeMoreAfterAction = false;
		return true;
	}
	bool PreviewTestAccess::OwnsInactiveActions(const BarContentInstance& instance)
	{
		const auto& s = *instance.state_;
		std::unordered_set<const BarButtonClass*> seen;
		auto owned = [&](const ButtonPtr& b)
		{
			return !b || (!b->clickFunc && b->state == &b->localState && seen.insert(b.get()).second);
		};
		for (const auto& segment : s.entries)
			for (const auto& b : segment)
				if (!owned(b)) return false;
		return owned(s.more) && owned(s.redo) && owned(s.boundary[0]) && owned(s.boundary[1]);
	}
	std::vector<double> PreviewTestAccess::ButtonState(const BarContentInstance& instance)
	{
		const auto& s = *instance.state_;
		std::vector<double> values;
		for (const auto& b : s.main)
		{
			values.push_back(b->button.x.val);
			values.push_back(b->button.pct.val);
			values.push_back(b->pressScale.val);
			values.push_back(static_cast<int>(b->hoverStage.load()));
		}
		return values;
	}
	std::wstring PreviewTestAccess::ButtonLabel(const BarContentInstance& instance, BarButtonPresetEnum preset)
	{
		const auto* button = instance.state_->Preset(preset);
		return button ? button->name.content.GetTar() : std::wstring{};
	}

	BarContentInstance::BarContentInstance() : state_(std::make_unique<State>()) {}
	BarContentInstance::~BarContentInstance() = default;
	void BarContentInstance::SetLayout(const BarLayoutModel& model, std::uint64_t revision)
	{
		auto& s = *state_;
		const bool changed = !s.prototypes || !(s.model == model);
		if (!changed && revision == s.revision) return;
		ReleasePress();
		if (changed)
		{
			s.Rebuild(model);
			s.model = model;
			s.editWidth = 0;
			s.lifted.reset();
		}
		if (revision != s.revision) s.lifted.reset();
		s.revision = revision;
		s.ClearHint();
		s.Invalidate();
	}
	bool BarContentInstance::Lift(Position pos, std::uint64_t revision)
	{
		auto& s = *state_;
		if (s.lifted || revision != s.revision || static_cast<size_t>(pos.segment) >= s.entries.size()
			|| pos.index >= s.model[pos.segment].size()) return false;
		if (std::none_of(s.presented.items.begin(),s.presented.items.end(),[&](const PreviewHit& item)
			{ return item.position && SamePosition(*item.position,pos); })) return false;
		// 越过拖动阈值：先让按压回弹，再抓起（隐藏并收拢）。
		ReleasePress();
		s.editWidth = s.targetWidth;
		s.lifted = pos;
		s.Invalidate();
		return true;
	}
	void BarContentInstance::CancelLift()
	{
		if (!state_->lifted) return;
		state_->lifted.reset();
		state_->ClearHint();
		state_->Invalidate();
	}
	bool BarContentInstance::Place(const BarLayoutModel& accepted, Position gap, std::uint64_t revision)
	{
		auto& s = *state_;
		if (!s.prototypes || revision != s.revision || static_cast<size_t>(gap.segment) >= s.entries.size()
			|| gap.index > s.model[gap.segment].size()) return false;
		auto catalog = s.prototypes->GetLayoutCatalog();
		auto& target = s.entries[static_cast<size_t>(gap.segment)];
		ButtonPtr placed;
		if (s.lifted)
		{
			const Position from = *s.lifted;
			const auto expected = Move(s.model,from,gap,catalog);
			if (!expected || !(*expected == accepted)) return false;
			auto& source = s.entries[static_cast<size_t>(from.segment)];
			placed = source[from.index];
			source.erase(source.begin() + from.index);
			// C1 返回删除前的缝；向右移动要抵消源项一次。
			if (gap.segment == from.segment && gap.index > from.index) --gap.index;
		}
		else
		{
			if (gap.index >= accepted[gap.segment].size()) return false;
			const auto& id = accepted[gap.segment][gap.index].Id;
			const auto expected = Insert(s.model,id,gap,catalog);
			if (!expected || !(*expected == accepted)) return false;
			placed = s.Make(id, true);
			s.styleDirty = true;
		}
		target.insert(target.begin() + gap.index,placed);
		s.model = accepted;
		s.lifted.reset();
		if (!s.HasVisibleMoreItems(s.model)) s.expanded = false;
		s.ClearHint();
		s.Invalidate();
		return true;
	}
	void BarContentInstance::SetMoreExpanded(bool value)
	{
		if (state_->expanded == value || (value && !state_->HasVisibleMoreItems(state_->model))) return;
		state_->expanded = value;
		state_->Invalidate();
	}
	void BarContentInstance::SetToolMode(StateModeSelectEnum mode)
	{
		if (state_->mode == mode) return;
		state_->mode = mode;
		state_->Invalidate();
	}
	StateModeSelectEnum BarContentInstance::ToolMode() const { return state_->mode; }
	bool BarContentInstance::MoreExpanded() const { return state_->expanded; }
	bool BarContentInstance::Press(const PreviewHit& hit)
	{
		auto& s = *state_;
		ReleasePress();
		if (hit.kind == PreviewHitKind::Close && s.expanded && s.panelButtons.HasButtons())
			SetClosePressed(true);
		else if (auto b = s.ButtonAt(hit))
		{
			UI3::StopButtonHoverVisual(*b,true);
			UI3::SetButtonPressedVisual(*b,true);
			RetargetPressScale(b->pressScale, true);
			s.pressed = std::move(b);
		}
		else return false;
		if (s.hovered && s.hovered != s.pressed) UI3::StopButtonHoverVisual(*s.hovered,true);
		s.hovered.reset();
		// 按下不是“未按键的鼠标移动”：作废提示来源，抬起后须重新移动才计时。
		s.hintSource = {};
		return true;
	}
	bool BarContentInstance::SetClosePressed(bool pressed)
	{
		auto& s = *state_;
		if (pressed == s.closePressed) return false;
		s.closePressed = pressed;
		RetargetPressScale(s.press, pressed);
		return true;
	}
	void BarContentInstance::ReleasePress()
	{
		auto& s = *state_;
		if (!s.pressed && !s.closePressed) return;
		if (s.pressed)
		{
			UI3::SetButtonPressedVisual(*s.pressed,false);
			RetargetPressScale(s.pressed->pressScale, false);
			s.pressed.reset();
		}
		SetClosePressed(false);
		s.hoverSuppressedSerial = s.inputs.pointerSerial;
	}
	bool BarContentInstance::Activate(const PreviewHit& hit)
	{
		auto& s = *state_;
		const bool wasExpanded = s.expanded;
		const auto wasMode = s.mode;
		if (hit.kind == PreviewHitKind::Close) s.expanded = false;
		else if (hit.kind == PreviewHitKind::More)
		{
			if (!s.HasVisibleMoreItems(s.model)) return false;
			s.expanded = !s.expanded;
		}
		else if (auto b = s.ButtonAt(hit))
		{
			if (hit.position && hit.position->segment == Segment::More)
			{
				// 与真实主栏一致：面板项默认先收起 More 再执行动作（预览不执行功能）。
				if (b->closeMoreAfterAction) s.expanded = false;
			}
			else switch (b->preset.load())
			{
			case BarButtonPresetEnum::Select: s.mode = StateModeSelectEnum::IdtSelection; break;
			case BarButtonPresetEnum::Draw: s.mode = StateModeSelectEnum::IdtPen; break;
			case BarButtonPresetEnum::Eraser: s.mode = StateModeSelectEnum::IdtEraser; break;
			case BarButtonPresetEnum::Geometry: s.mode = StateModeSelectEnum::IdtShape; break;
			default: break;
			}
		}
		const bool changed = wasExpanded != s.expanded || wasMode != s.mode;
		if (changed) s.Invalidate();
		return changed;
	}
	void BarContentInstance::Suspend()
	{
		auto& s = *state_;
		s.clock.SuspendForIdle(s.inputs.time);
		ReleasePress();
		s.lifted.reset();
		if (s.hovered) UI3::StopButtonHoverVisual(*s.hovered,true);
		s.hovered.reset();
		s.ClearHint();
		s.Invalidate();
	}
	UIRendering& BarContentInstance::Renderer() { return state_->renderer; }
	const PreviewGeometry& BarContentInstance::Geometry() const { return state_->presented; }

	void BarContentInstance::State::BuildGeometry()
	{
		pending = {frame.frameId, revision, Pixels(body, frame.scale)};
		pending.dividerReach = static_cast<float>(ButtonGapDip * frame.scale / 2);
		auto add = [&](const ButtonPtr& b)
		{
			b->button.Inherit(UiInheritEnum::CenterFromTopLeft,body);
			// 隐藏实例（投影隐藏、抓起项）仍在列表里做收拢与淡出，但不参与命中与插入缝。
			if (!b->IsVisible()) return;
			auto hit = identities.at(b.get());
			hit.rect = Pixels(b->button,frame.scale);
			pending.items.push_back(hit);
		};
		if (expanded && panelButtons.HasButtons() && panel.pct.val > 0.000001)
		{
			pending.items.push_back({PreviewHitKind::Panel,{},Pixels(panel,frame.scale)});
			for (auto& b : panelButtons.explicitMore) add(b);
			pending.items.push_back({PreviewHitKind::Close,{},Pixels(close,frame.scale)});
		}
		for (auto& b : main) add(b);
		const float reach = static_cast<float>(frame.scale * ButtonOneSideDip / 2);
		for (auto zone : {Segment::A1, Segment::B1, Segment::More, Segment::A2})
		{
			std::vector<PreviewHit> items;
			for (const auto& h : pending.items)
				if (h.position && h.position->segment == zone && h.kind != PreviewHitKind::Companion)
				{
					auto item = h;
					// 小尺寸的 Redo 与 Recall 共用一个编辑位置，缝隙越过整个组合。
					if (redo && redo->IsVisible() && zone == Segment::A1 && inputs.sizeMode == BarSizeMode::Compact
						&& model[zone][h.position->index].Id == BarButtonId::Recall)
					{
						const auto companion = Pixels(redo->button,frame.scale);
						item.rect.left = (std::min)(item.rect.left,companion.left);
						item.rect.right = (std::max)(item.rect.right,companion.right);
					}
					items.push_back(item);
				}
			const bool rtl = inputs.iconOrder == 3 && zone != Segment::More;
			// 两个可见项间可能隔着隐藏项；原位放下应保留整个模型的次序。
			size_t leftGap = 0;
			auto originalGap = [&](size_t rightGap) {
				return lifted && lifted->segment == zone && leftGap <= lifted->index && lifted->index <= rightGap
					? lifted->index : rightGap;
			};
			auto vertical = [&](size_t index, float x, D2D1_RECT_F row)
			{
				index = originalGap(index);
				pending.gaps.push_back({{zone,index},{x,row.top},{x,row.bottom},
					{x-reach,row.top,x+reach,row.bottom}});
			};
			if (items.empty())
			{
				if (zone == Segment::B1 || zone == Segment::More)
				{
					auto r = Pixels(more->button,frame.scale);
					vertical(lifted && lifted->segment == zone ? lifted->index : zone == Segment::More ? model[zone].size() : 0,zone == Segment::More ? (r.left+r.right)/2 : rtl ? r.right : r.left,r);
					if (zone == Segment::B1)
					{
						// 空 B1 锚在 More 的边缘。只让缝隙伸入按钮半个间距，More 中心仍是纯收纳区。
						auto& area = pending.gaps.back().area;
						if (rtl) area.left = r.right - pending.dividerReach;
						else area.right = r.left + pending.dividerReach;
					}
				}
				continue;
			}
			for (size_t i = 0; i < items.size(); ++i)
			{
				const auto& hit = items[i];
				const auto r = hit.rect;
				if (i == 0) vertical(hit.position->index,rtl ? r.right : r.left,r);
				leftGap = hit.position->index + 1;
				if (i + 1 == items.size())
				{
					vertical(model[zone].size(),rtl ? r.left : r.right,r);
					continue;
				}
				const auto& next = items[i+1];
				const auto q = next.rect;
				if ((std::min)(r.right,q.right) > (std::max)(r.left,q.left)
					&& (r.bottom <= q.top || q.bottom <= r.top))
				{
					const float y = r.top < q.top ? (r.bottom+q.top)/2 : (q.bottom+r.top)/2;
					const float l = (std::max)(r.left,q.left), h = (std::min)(r.right,q.right);
					auto gap = *next.position;
					gap.index = originalGap(gap.index);
					pending.gaps.push_back({gap,{l,y},{h,y},{l,y-reach,h,y+reach}});
				}
				else if (zone == Segment::More && q.left < r.left)
				{
					vertical(next.position->index,r.right,r);
					vertical(next.position->index,q.left,q);
				}
				else vertical(next.position->index,rtl ? (r.left+q.right)/2 : (r.right+q.left)/2,q);
			}
		}
	}

	static PreviewHit HitGeometry(const PreviewGeometry& g, D2D1_POINT_2F p)
	{
		for (auto it = g.items.rbegin(); it != g.items.rend(); ++it)
		{
			const bool inPanel = it->kind == PreviewHitKind::Panel || it->kind == PreviewHitKind::Close
				|| (it->position && it->position->segment == Segment::More);
			if (inPanel && Contains(g.body,p)) continue;
			if (Contains(it->rect,p)) return *it;
		}
		// 先保留实体的直接命中，再将一像素的用户线扩到左右间距中点。
		for (const auto& item : g.items)
		{
			if (item.kind != PreviewHitKind::UserDivider) continue;
			auto area = item.rect;
			const float extra = g.dividerReach;
			area.left -= extra;
			area.right += extra;
			if (Contains(area,p)) return item;
		}
		return {};
	}
	PreviewHit BarContentInstance::HitTest(D2D1_POINT_2F point) const
	{
		return HitGeometry(Geometry(),point);
	}
	bool BarContentInstance::ShouldDismissMoreOnPress(bool overPreview, D2D1_POINT_2F point) const
	{
		// 展开或重排后可能仍显示旧像素。命中失效期间跳过外按，等宿主确认新帧。
		if (!MoreExpanded() || !Geometry().frameId) return false;
		if (!overPreview) return true;
		const auto hit = HitTest(point);
		return hit.kind != PreviewHitKind::More && hit.kind != PreviewHitKind::Panel
			&& hit.kind != PreviewHitKind::Close && (!hit.position || hit.position->segment != Segment::More);
	}
	std::vector<PreviewGap> BarContentInstance::InsertionGaps(D2D1_POINT_2F p) const
	{
		std::vector<PreviewGap> result;
		const auto hit = HitTest(p);
		for (const auto& gap : Geometry().gaps)
		{
			if (!Contains(gap.area,p)) continue;
			if (gap.position.segment == Segment::More && Contains(Geometry().body,p)
				&& hit.kind != PreviewHitKind::More) continue;
			result.push_back(gap);
		}
		auto distance = [&](const PreviewGap& gap)
		{
			return std::hypot(p.x - std::clamp(p.x,gap.start.x,gap.end.x),
				p.y - std::clamp(p.y,gap.start.y,gap.end.y));
		};
		std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b)
		{
			const auto da = distance(a), db = distance(b);
			if (da != db) return da < db;
			const bool sameA = hit.position && a.position.segment == hit.position->segment;
			const bool sameB = hit.position && b.position.segment == hit.position->segment;
			if (sameA != sameB) return sameA;
			return a.position.index < b.position.index;
		});
		return result;
	}

	PreviewFrame BarContentInstance::Advance(const PreviewInputs& inputs)
	{
		auto& s = *state_;
		const bool languageChanged = s.initialized && inputs.typography.revision != s.inputs.typography.revision;
		if (!s.prototypes) s.Rebuild(s.model);
		else if (languageChanged)
		{
			s.PreparePrototypes();
			// 新语言只更新原型文字，保留正在拖动的 occurrence 与其在途动画。
			for (size_t z = 0; z < s.entries.size(); ++z)
				for (size_t i = 0; i < s.entries[z].size(); ++i)
					if (auto& b = s.entries[z][i])
						s.RefreshLabel(s.model.segments[z][i].Id, *b);
			for (auto& [id, button] : s.singletons) s.RefreshLabel(id, *button);
		}
		if (!s.formats || inputs.writeFactory != s.inputs.writeFactory || inputs.fonts != s.inputs.fonts || languageChanged)
			s.formats = std::make_unique<FormatCache>(inputs.writeFactory);
		else s.formats->Clean();
		s.inputs = inputs;
		s.drawn = 0;
		s.frame = {};
		if (!(inputs.width > 0 && inputs.height > 0 && inputs.targetHeight > 0))
		{
			Suspend();
			return s.frame;
		}
		s.Project();
		auto& frame = s.frame;
		frame.frameId = ++s.serial;
		frame.revision = s.revision;
		const bool compact = inputs.sizeMode == BarSizeMode::Compact;
		const auto& metrics = ResolveBarSizeMetrics(inputs.sizeMode);
		const auto packed = ResolveBarButtonListLayout(s.main,BarButtonListLayoutKind::Main,{},inputs.sizeMode);
		const auto order = inputs.iconOrder == 3 ? BarIconOrder::RightToLeft : BarIconOrder::LeftToRight;
		const double dt = s.clock.Tick(inputs.time);
		const bool animate = UiAnimationEnabled && s.initialized;
		const double rate = UiAnimationSpeedRate;
		const UiAnimationAdvanceContextClass tick{dt,rate,animate,!animate};
		ButtonAnimationContext motion;
		motion.dtSeconds = dt; motion.speedRate = rate;
		motion.animationEnabled = animate; motion.forceReplace = !animate;
		const double light = GetThemeLightWeight();
		const bool dark = light < 0.5;
		s.body.lightMaterial = s.panel.lightMaterial = light;
		const bool open = s.expanded && s.panelButtons.HasButtons();

		// 相机（R3）只由目标包络决定：主栏上方常驻一行 More 面板（含 Back 峰值与阴影）的预留，
		// More 达到两行及以上时预留随之增高（预览区向下扩展），主栏下方对称预留一行；
		// 缩放取高度适配、可用宽度与预览区高度三者最小，与 More 开合进度和动画中的宽度无关。
		const double shadow = ThemeMaterial::SurfaceShadowOutsetDip + RenderingAttribute::dirtyAntialiasPadding;
		const auto moreGrid = ResolveBarButtonListLayout(s.panelButtons.explicitMore,BarButtonListLayoutKind::GroupedGrid,{},inputs.sizeMode);
		const auto moreSize = MeasureMoreGrid(moreGrid,compact ? metrics.gap : BarMorePanelGap);
		const double oneRow = BarMorePanelPadding * 2 + ButtonOneSideDip;
		const double peak = UiGetCurveExtrema(UiCurveEnum::EaseOutBack).maximum;
		auto reserveFor = [&](double panelHeight)
		{
			// 与 PositionContentMore 同一套展开几何：中心离 More 锚点 D·p，高度由紧凑态线性插值到面板高度。
			const double anchorHalf = compact ? metrics.barHeight / 2 + metrics.attachedGap
				: (s.more && s.more->size == ButtonSizeEnum::twoTwo ? ButtonTwoSideDip : ButtonOneSideDip) / 2 + BarMorePanelAnchorGap;
			const double distance = (anchorHalf + panelHeight / 2) * peak;
			const double height = BarMorePanelCompactHeight + (panelHeight - BarMorePanelCompactHeight) * peak;
			return (std::max)(0.0, distance + height / 2 - metrics.barHeight / 2) + shadow;
		};
		const double topTarget = reserveFor((std::max)(oneRow, s.panelButtons.HasButtons() ? moreSize.height : 0.0));
		const double bottomReserve = reserveFor(oneRow);
		if (!s.lifted) s.editWidth = 0;
		s.targetWidth = packed.width;
		const double morePeakWidth = BarMorePanelCompactWidth + (moreSize.width - BarMorePanelCompactWidth) * peak;
		const double moreEnvelope = (std::max)(moreSize.width, morePeakWidth) + 2 * shadow;
		const double mainEnvelope = (std::max)(packed.width, s.editWidth) + 2 * shadow;
		const double envelope = s.panelButtons.HasButtons() ? (std::max)(mainEnvelope, moreEnvelope) : mainEnvelope;
		const double targetExtent = topTarget + metrics.barHeight + bottomReserve;
		const double zoomTarget = (std::min)({inputs.targetHeight / metrics.barHeight, inputs.width / envelope,
			inputs.height / targetExtent});
		if (animate)
		{
			const UiCurveSpecClass cameraCurve{UiCurveEnum::EaseOutCubic,UiCurveEnum::EaseOutCubic,0,false};
			s.cameraZoom.SetTar(zoomTarget,UiDefaultOperationDur,nullopt,false,cameraCurve);
			s.cameraTop.SetTar(topTarget,UiDefaultOperationDur,nullopt,false,cameraCurve);
		}
		else
		{
			s.cameraZoom.SetDirect(zoomTarget);
			s.cameraTop.SetDirect(topTarget);
		}
		frame.active = UiAdvanceAnimation(s.cameraZoom,tick).active;
		frame.active |= UiAdvanceAnimation(s.cameraTop,tick).active;
		double zoom = s.cameraZoom.val;
		// 网格尺寸立即采用新布局，相机仍在过渡。先给当前网格预留峰值包络，避免面板在缩放落定前越界。
		if (s.panelButtons.HasButtons())
			zoom = (std::min)({zoom, inputs.width / moreEnvelope, inputs.height / targetExtent});
		frame.scale = zoom;
		const double extentDip = (std::max)(static_cast<double>(s.cameraTop.val), topTarget) + metrics.barHeight + bottomReserve;
		const double cappedHeight = (std::min)(std::ceil(extentDip * zoom), inputs.height);
		float extentPixels = static_cast<float>(cappedHeight);
		if (extentPixels > cappedHeight) extentPixels = std::nextafter(extentPixels, 0.0f);
		if (!s.initialized) s.renderer.ConfigureLocalizedTypography();
		else s.renderer.RefreshLocalizedTypography();
		s.renderer.UpdateFontRenderingPolicy(inputs.fractionalFont);
		s.renderer.SetFrameZoom(zoom);
		frame.active |= s.renderer.FontAnimationsActive();
		s.renderer.BeginFontFrame(inputs.time,tick,zoomTarget);
		s.renderer.SetFontTargetScale(1.0);

		// 与真实主栏同一编排：批次决策 → 按钮目标（含隐藏锚定）→ 底板目标；预览无主按钮、无折叠、无换边，底板居中。
		const auto batch = ResolveMainBarBatch(s.batch, {s.visible, packed.width, UiDefaultOperationDur, order,
			false, false, false, false});
		const MainBarThemeColor themeColor = [](ThemeColorEnum role) { return GetThemeColor(role); };
		ApplyMainBarButtonTargets(s.main, [&](BarButtonPresetEnum id) { return s.Preset(id); }, packed, batch,
			{false, true, s.mode == StateModeSelectEnum::IdtShape, !s.initialized, dark, metrics.barHeight / 2,
				UiDefaultOperationDur, order, s.recall, ButtonPressCurve(), ButtonReleaseCurve(), themeColor}, s.renderer);
		BarButtonClass* lifted = s.lifted ? s.entries[static_cast<size_t>(s.lifted->segment)][s.lifted->index].get() : nullptr;
		const auto retargetLiftPosition = [&](UiValueClass& value, double target)
		{
			UiSetLayoutPositionTarget(value, target, batch.duration, batch.forceRestart || batch.layoutChanged, batch.valueCurve);
		};
		if (lifted && s.lifted->segment != Segment::More)
		{
			// 共享层只为投影隐藏的固定工具选锚点，其他被抓起的主栏项就近收拢。
			switch (lifted->preset.load())
			{
			case BarButtonPresetEnum::Eraser: case BarButtonPresetEnum::Geometry:
			case BarButtonPresetEnum::Recall: case BarButtonPresetEnum::Redo:
			case BarButtonPresetEnum::Freeze: case BarButtonPresetEnum::Pierce:
			case BarButtonPresetEnum::CanvasDebug: case BarButtonPresetEnum::EndShow:
				break;
			default:
				AnchorHiddenBarButton(s.main, lifted, nullptr, retargetLiftPosition);
				break;
			}
		}
		ApplyMainBarBodyTargets(s.body, batch, {packed.width, metrics.barHeight, std::nullopt, metrics,
			false, true, animate, UiDefaultOperationDur, themeColor});
		frame.active |= AdvanceShapeVisual(s.body,motion).active;
		s.body.UpInh(UiInheritClass(inputs.width / zoom / 2 + s.body.x.val - s.body.w.val / 2, s.cameraTop.val));
		auto advanceButton = [&](BarButtonClass& b, bool moreItem)
		{
			b.button.lightMaterial = light;
			ButtonAnimationContext context = motion;
			context.visible = b.IsVisible() && (!moreItem || open);
			context.hoverAllowed = b.state->state != WidgetState::Disable && b.state->state != WidgetState::Selected;
			frame.active |= Bar::AdvanceButtonVisual(b,context).active;
		};
		for (const auto& b : s.main)
		{
			advanceButton(*b,false);
			b->button.Inherit(UiInheritEnum::CenterFromTopLeft,s.body);
		}
		// 禁用动画时，先同步进度再定位，避免重复准备同一时刻的画面时补画上一帧。
		ApplyMainBarMoreTargets(s.progress,s.opacity,open,UiDefaultOperationDur);
		auto advanceMore = [&]
		{
			for (auto* value : {&s.progress, &s.opacity})
			{
				const auto result = UiAdvanceAnimation(*value,tick);
				frame.active |= result.changed || result.active;
			}
		};
		if (!animate) advanceMore();
		PositionContentMore(s.Chrome(),s.more.get(),s.body,s.panelButtons,inputs.sizeMode,s.progress,s.opacity,
			open,false,inputs.width,extentPixels,zoom,{},dark,s.closePressed,s.closeHover,s.renderer,shadow);
		if (animate) advanceMore();
		if (lifted && s.lifted->segment == Segment::More)
		{
			// 网格不会定位隐藏项；预览保留这个实例，向相邻项（或空面板的入口）收拢并淡出。
			if (std::any_of(s.panelButtons.explicitMore.begin(), s.panelButtons.explicitMore.end(),
				[](const auto& button) { return button && button->IsVisible(); }))
				AnchorHiddenBarButton(s.panelButtons.explicitMore, lifted, nullptr, retargetLiftPosition);
			else
			{
				retargetLiftPosition(lifted->button.x, s.more->button.x.tar);
				retargetLiftPosition(lifted->button.y, s.more->button.y.tar);
				lifted->lastDrawX = s.more->button.x.tar;
				lifted->lastDrawY = s.more->button.y.tar;
			}
			UI3::RetargetButtonInteractionVisual(dark, *lifted, false, true,
				lifted->state->state == WidgetState::Selected, UiDefaultOperationDur);
		}
		frame.active |= UiAdvanceAnimation(s.press,tick).active;
		for (auto& b : s.panelButtons.explicitMore) advanceButton(*b,true);
		frame.active |= AdvanceShapeVisual(s.close,motion).active;
		frame.active |= AdvanceSvgVisual(s.closeSvg,motion).active;
		// 定位函数写的是宽高目标；推进后再按本帧尺寸居中，避免第一次画 X 时沿用上一帧尺寸。
		s.closeSvg.UpInh(UiInheritClass(s.closeSvg.x.val - s.closeSvg.w.val / 2.0,
			s.closeSvg.y.val - s.closeSvg.h.val / 2.0));
		// 批次时间轴与实栏一样在推进末尾前进，提示门禁读取推进后的状态。
		s.batch.timeline.Advance(dt,rate);
		frame.active |= s.batch.timeline.IsActive();

		s.BuildGeometry();
		ButtonPtr hovered;
		if (s.hoverSuppressedSerial && *s.hoverSuppressedSerial != inputs.pointerSerial) s.hoverSuppressedSerial.reset();
		const bool canHover = inputs.pointer && inputs.mouse && !inputs.editing && !s.lifted
			&& !s.pressed && !s.closePressed && !s.hoverSuppressedSerial;
		if (canHover)
			hovered = s.ButtonAt(HitGeometry(s.pending,*inputs.pointer));
		if (s.hovered != hovered)
		{
			if (s.hovered) UI3::StopButtonHoverVisual(*s.hovered,false);
			if (hovered) UI3::StartButtonHoverVisual(*hovered);
			s.hovered = hovered;
			frame.active = true;
		}
		// 提示来源只随新的指针移动登记（与真实主栏输入线程一致）；门禁与延时由共享层判定，只在小尺寸显示。
		if (inputs.pointerSerial != s.hintSerial)
		{
			s.hintSerial = inputs.pointerSerial;
			s.hintSource.mouse = canHover;
			s.hintSource.screen = inputs.pointer ? POINT{std::lround(inputs.pointer->x), std::lround(inputs.pointer->y)} : POINT{};
			s.hintSource.main = hovered;
			const auto identity = hovered ? s.identities.find(hovered.get()) : s.identities.end();
			s.hintSource.more = identity != s.identities.end() && identity->second.position
				&& identity->second.position->segment == Segment::More;
		}
		if (!canHover) s.hintSource = {};
		const auto hint = UpdateMainBarHoverHint(s.hint, s.hintSource, {inputs.sizeMode, false,
			!canHover, s.batch.timeline.IsActive(), false, !s.progress.IsSame() || !s.opacity.IsSame(),
			[] { return true; }, [] { return std::wstring{}; }, [open] { return open; }, inputs.time});
		frame.active |= hint.changed;
		frame.hintDeadline = hint.deadline;
		if (s.hint.shown)
		{
			const LONG inset = static_cast<LONG>(std::ceil(shadow * zoom));
			s.hintRect = PositionMainBarHoverHint(s.renderer, s.hint.text, zoom, s.hintSource.screen, POINT{},
				RECT{inset, inset, static_cast<LONG>(inputs.width) - inset, static_cast<LONG>(extentPixels) - inset},
				inputs.width, extentPixels);
		}
		UiFrameLightingInput lights;
		for (auto& b : s.main)
		{
			if (b->preset == BarButtonPresetEnum::Divider || !b->IsVisible()) continue;
			lights.primaryAnchor = D2D1::Point2F(static_cast<float>(b->button.inhX+b->button.w.val/2),
				static_cast<float>(b->button.inhY+b->button.h.val/2));
			lights.primaryAnchorAvailable = true;
			lights.primaryAnchorId = static_cast<int>(b->preset.load());
			break;
		}
		lights.edgeLightingEnabled = inputs.edgeLighting;
		lights.dynamicEdgeLightingEnabled = inputs.dynamicLighting;
		lights.cursorAvailable = inputs.pointer.has_value();
		lights.cursorScreenPoint = inputs.pointer.value_or(D2D1_POINT_2F{});
		lights.cursorSerial = inputs.pointerSerial;
		lights.initialAccentColor = lights.accentColor = GetThemeColor(ThemeColorEnum::Accent);
		s.lighting.SetFrameZoom(zoom);
		frame.active |= s.lighting.PrepareFrameLighting(dt,{},lights);
		s.renderer.SetFrameLightingSnapshot(s.lighting.SnapshotFrameLighting(),inputs.edgeLighting);
		// 预览区是稳定矩形（上沿紧贴宿主原点），宿主裁框不随 More 开合、提示或动画中间值变化。
		frame.bounds = D2D1::RectF(0, 0, static_cast<float>(inputs.width), extentPixels);
		frame.extent = D2D1::SizeF(frame.bounds.right-frame.bounds.left,frame.bounds.bottom-frame.bounds.top);
		s.initialized = true;
		if (!frame.active) s.clock.SuspendForIdle(inputs.time);
		return frame;
	}
	HRESULT BarContentInstance::Draw(FrameTarget& target, const PreviewFrame& frame)
	{
		auto& s = *state_;
		auto* dc = target.DeviceContext();
		s.drawn = 0;
		if (!frame.frameId || frame.frameId != s.frame.frameId || !dc || target.BoundRenderer() != &s.renderer
			|| dc != s.renderer.GetDeviceContext()) return E_INVALIDARG;
		if (!s.drawingState)
		{
			Microsoft::WRL::ComPtr<ID2D1Factory> factory;
			Microsoft::WRL::ComPtr<ID2D1Factory1> factory1;
			dc->GetFactory(&factory);
			auto hr = factory.As(&factory1);
			if (FAILED(hr)) return hr;
			hr = factory1->CreateDrawingStateBlock(nullptr,nullptr,&s.drawingState);
			if (FAILED(hr)) return hr;
		}
		// 宿主可能使用非 96 DPI 或带变换的外部 context，返回时恢复其绘图状态。
		struct Restore
		{
			ID2D1DeviceContext* dc;
			ID2D1DrawingStateBlock1* block;
			float dpiX, dpiY;
			~Restore() { dc->RestoreDrawingState(block); dc->SetDpi(dpiX,dpiY); }
		} restore{dc,s.drawingState.Get()};
		dc->GetDpi(&restore.dpiX,&restore.dpiY);
		dc->SaveDrawingState(restore.block);
		dc->SetDpi(96,96);
		dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);
		dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
		dc->SetTransform(D2D1::Matrix3x2F::Identity());
		if (s.panel.pct.val > 0.000001)
			DrawContentMore(s.renderer,dc,s.morePanel,s.Chrome(),s.body,s.panelButtons,nullptr);
		DrawContentBody(s.renderer,dc,s.mainPanel,s.body,s.main,s.visible,nullptr,0);
		if (s.hint.shown)
			DrawContentHint(s.renderer,dc,s.hintRect,s.hint.text,s.frame.scale,s.body.lightMaterial,
				s.hintShape,*s.formats,s.inputs.fonts);

		s.drawn = frame.frameId;
		return S_OK;
	}
	LocalizedTypography BarContentInstance::CurrentTypography()
	{
		return MakeBarLocalizedTypographyServices().localizedTypography();
	}
	HRESULT BarContentInstance::DrawGhost(FrameTarget& target, const std::variant<Position,std::string>& source,
		D2D1_POINT_2F point, const std::wstring& hint, std::optional<COLORREF> dangerOutline)
	{
		auto& s = *state_;
		auto* dc = target.DeviceContext();
		if (!dc || dc != s.renderer.GetDeviceContext() || !s.initialized) return E_INVALIDARG;
		const auto* pos = std::get_if<Position>(&source);
		if (pos && (size_t(pos->segment)>=s.entries.size() || pos->index>=s.entries[size_t(pos->segment)].size())) return E_INVALIDARG;
		const std::string& id = pos ? s.model[pos->segment][pos->index].Id : std::get<std::string>(source);
		if (!s.ghost || s.ghostId != id)
		{
			s.ghost = s.Make(id);
			s.ghostId = id;
		}
		if (!s.ghost) return E_INVALIDARG;
		auto& button = *s.ghost;
		if (pos)
		{
			const auto& original = s.entries[size_t(pos->segment)][pos->index];
			if (!original) return E_INVALIDARG;
			button.size = original->size.load();
			button.state->state = original->state->state;
		}
		else
		{
			const auto size = PreviewPrototypes::ToRuntimeSize(DefaultSizeForBarButtonId(id));
			button.size = s.inputs.sizeMode == BarSizeMode::Compact && size != ButtonSizeEnum::oneTwo ? ButtonSizeEnum::oneOne : size;
			button.state->state = WidgetState::None;
		}
		const double zoom = s.frame.scale;
		const auto kind = button.size == ButtonSizeEnum::oneOne ? ButtonVisualLayoutKind::StandardOneOne :
			button.size == ButtonSizeEnum::twoOne ? ButtonVisualLayoutKind::StandardTwoOne : ButtonVisualLayoutKind::StandardTwoTwo;
		if (button.preset == BarButtonPresetEnum::Divider)
		{
			button.button.w.SetDirect(1);
			button.button.h.SetDirect(s.inputs.sizeMode == BarSizeMode::Compact ? 28.5 : 50);
			button.button.fill->SetDirect(GetThemeColor(ThemeColorEnum::Divider));
		}
		else
		{
			UI3::ConfigureButtonVisualTargets(button,kind,0,1,true,nullopt,nullopt,
				button.LabelFontSize(ResolveButtonVisualMetrics(kind).primaryFontSizeDip),&s.renderer);
			UI3::RetargetButtonInteractionVisual(GetThemeLightWeight()<0.5,button,true,true,button.state->state==WidgetState::Selected,0);
			if (button.size == ButtonSizeEnum::oneOne) button.name.pct.SetDirect(0);
		}
		(void)Bar::AdvanceButtonVisual(button,{0,1,false,true});
		const double ghostZoom = zoom * 1.05;
		const double x = point.x / ghostZoom - button.button.w.val / 2;
		const double y = point.y / ghostZoom - button.button.h.val / 2;
		// 使用已推进的主栏材质参数（含主题、半径、边框及阴影），仅改几何。
		auto background = s.body;
		background.w.SetDirect(button.button.w.val + 2 * ButtonGapDip);
		background.h.SetDirect(button.button.h.val + 2 * ButtonGapDip);
		background.UpInh(UiInheritClass(x - ButtonGapDip, y - ButtonGapDip));

		s.renderer.SetFrameZoom(ghostZoom);
		button.button.lightMaterial = background.lightMaterial;
		button.button.themeSurface = button.preset != BarButtonPresetEnum::Divider;
		button.button.pct.SetDirect(1);
		if (dangerOutline)
		{
			// 底板、阴影和按钮先合成，再整体降低透明度；描边和跟随浮窗保持清晰。
			const auto pixels = target.PixelSize();
			dc->PushLayer(D2D1::LayerParameters1(D2D1::RectF(0, 0, float(pixels.width), float(pixels.height)),
				nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 0.45f, nullptr,
				D2D1_LAYER_OPTIONS1_NONE), nullptr);
		}
		s.renderer.Shape(dc, background, UiInheritClass(background.inhX, background.inhY));
		DrawButtonVisual(s.renderer, dc, button, UiInheritClass(x, y));
		if (dangerOutline)
		{
			dc->PopLayer();
			const auto rect = Pixels(background, ghostZoom);
			const float rx = static_cast<float>(background.rw->val * ghostZoom);
			const float ry = static_cast<float>(background.rh->val * ghostZoom);
			if (auto* brush = s.renderer.ContentSolidBrush(*dangerOutline))
				dc->DrawRoundedRectangle(D2D1::RoundedRect(rect, rx, ry), brush, static_cast<float>(ghostZoom));
		}
		s.renderer.SetFrameZoom(zoom);
		return DrawFollowingHint(target, point, hint,
			(button.button.h.val / 2 + ButtonGapDip) * 1.05 + BarHoverHintCursorOffsetDip);
	}
	HRESULT BarContentInstance::DrawFollowingHint(FrameTarget& target, D2D1_POINT_2F point, const std::wstring& hint, double offsetDip)
	{
		auto& s = *state_;
		auto* dc = target.DeviceContext();
		if (!dc || dc != s.renderer.GetDeviceContext() || !s.initialized) return E_INVALIDARG;
		const double zoom = s.frame.scale;
		if (!hint.empty())
		{
			const auto size = s.renderer.MeasureText(hint,BarHoverHintFontSizeDip);
			const auto pixels = target.PixelSize();
			const auto rect = ResolveBarHoverHintRect(point.x,point.y,
				(std::ceil(size.width)+2*BarHoverHintPaddingXDip)*zoom,(std::ceil(size.height)+2*BarHoverHintPaddingYDip)*zoom,
				offsetDip*zoom,{0,0,double(pixels.width),double(pixels.height)});
			DrawContentHint(s.renderer,dc,rect,hint,zoom,GetThemeLightWeight(),s.hintShape,*s.formats,s.inputs.fonts);
		}
		return S_OK;
	}
	void BarContentInstance::ConfirmFrame(std::uint64_t id, bool succeeded)
	{
		auto& s = *state_;
		if (succeeded && id && id == s.drawn && id == s.frame.frameId) s.presented = s.pending;
		if (id == s.drawn) s.drawn = 0;
	}

}
