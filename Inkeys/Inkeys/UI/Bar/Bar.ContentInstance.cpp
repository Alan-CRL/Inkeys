module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../../IdtMain.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include <d2d1_1helper.h>
#include <dwrite_1.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_set>

module Inkeys.UI.Bar;
import :ContentInstance;
import :Main;
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
		// 字号取整只看面板最终姿态，不能追逐每帧 SetDirect 的当前展开尺寸。
		const double fontTargetScale = max(0.01, BarMorePanelCompactScale
			+ (1.0 - BarMorePanelCompactScale) * progress.tar);
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
		bool expanded = false, initialized = false;
		double editWidth = 0;
		std::optional<std::uint64_t> blockedHintSerial;
		std::unique_ptr<PreviewPrototypes> prototypes;
		std::array<std::vector<ButtonPtr>, 4> entries;
		ButtonPtr boundary[2], more, redo, ghost;
		std::string ghostId;
		std::vector<ButtonPtr> main;
		std::vector<BarMainButtonLayoutEntry> visible;
		std::unordered_map<BarButtonClass*, PreviewHit> identities;
		BarMoreButtonSnapshotClass panelButtons;
		Microsoft::WRL::ComPtr<ID2D1DrawingStateBlock1> drawingState;
		UiShapeClass body, panel, divider, close;
		UiSVGClass closeSvg;
		UiValueClass progress{0}, opacity{0}, press{1};
		IdtAtomic<ButtonHoverStageEnum> closeHover = ButtonHoverStageEnum::None;
		Panel mainPanel{"main"}, morePanel{"more"};
		LightingStateMachine lighting;
		FrameAnimationClock clock;
		PreviewGeometry pending, presented;
		PreviewInputs inputs;
		PreviewFrame frame;
		std::optional<UiShapeClass> hintShape;
		BarHoverHintRect hintRect;
		std::wstring hintText;
		std::chrono::steady_clock::time_point entered;
		bool hintShown = false;
		ButtonPtr hovered;
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
			for (auto& b : {boundary[0],boundary[1],more,redo}) reset(b);
			reset(ghost);
			closeSvg.ResetCache();
		}
		void PreparePrototypes()
		{
			ghost.reset();
			prototypes = std::make_unique<PreviewPrototypes>();
			prototypes->PresetInitialization();
			prototypes->RegisterBuiltInComponents();
			auto refresh = [](ButtonPtr& slot, const BarButtonClass& prototype)
			{
				if (slot) slot->name.SetStringImmediate(prototype.name.content.GetTar());
				else slot = Clone(prototype);
			};
			const auto* line = prototypes->preset[static_cast<int>(BarButtonPresetEnum::Divider)];
			for (auto& b : boundary) refresh(b,*line);
			refresh(redo,*prototypes->preset[static_cast<int>(BarButtonPresetEnum::Redo)]);
			refresh(more,*prototypes->GetMoreButton());
		}
		ButtonPtr Make(const std::string& id)
		{
			BarButtonRegistrationClass registration;
			if (!prototypes->TryGetRegistration(id, registration) || !registration.button) return {};
			auto b = Clone(*registration.button);
			if (b->preset == BarButtonPresetEnum::Draw)
				b->TransitionContent(L"barBrush1", IW(I18nKey.UI.Bar.MainButtons.HardPenLabel), true);
			if (b->preset == BarButtonPresetEnum::Whiteboard)
				b->TransitionContent(L"barWhiteboard", IW(I18nKey.UI.Bar.MainButtons.WhiteboardLabel), true);
			return b;
		}
		void Rebuild()
		{
			if (!prototypes) PreparePrototypes();
			for (std::size_t z = 0; z < entries.size(); ++z)
			{
				entries[z].clear();
				for (const auto& item : model.segments[z]) entries[z].push_back(Make(item.Id));
			}
		}
		void Invalidate()
		{
			drawn = 0;
			frame.frameId = 0;
			presented = {};
		}
		void Project()
		{
			main.clear(); visible.clear(); panelButtons.explicitMore.clear(); identities.clear();
			const bool compact = inputs.sizeMode == BarSizeMode::Compact;
			const auto a2 = ResolveBarA2Projection(inputs.presentation, false, inputs.whiteboardEnabled, false, false, inputs.freezeAvailable);
			const auto recall = ResolveRecallChrome({}, false, compact);
			auto append = [&](ButtonPtr b, PreviewHitKind kind, std::optional<Position> pos)
			{
				identities.emplace(b.get(), PreviewHit{kind,pos});
				if (pos && pos->segment == Segment::More) panelButtons.explicitMore.push_back(b);
				else { main.push_back(b); visible.push_back({b.get(),b->size.load(),true}); }
			};
			auto appendZone = [&](Segment zone)
			{
				auto& buttons = entries[static_cast<size_t>(zone)];
				for (size_t i = 0; i < buttons.size(); ++i)
				{
					Position pos{zone,i};
					auto b = buttons[i];
					if (!b || !model[zone][i].Visible || (lifted && SamePosition(*lifted,pos))) continue;
					const auto key = b->preset.load();
					if ((key == BarButtonPresetEnum::Whiteboard && !inputs.whiteboardEnabled)
						|| (key == BarButtonPresetEnum::Freeze && !a2.freezeVisible)
						|| (key == BarButtonPresetEnum::EndShow && !a2.endShowVisible)) continue;
					auto size = PreviewPrototypes::ToRuntimeSize(model[zone][i].Size);
					if (key == BarButtonPresetEnum::Whiteboard) size = a2.whiteboardTwoTwo ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
					if (key == BarButtonPresetEnum::Freeze) size = a2.freezeTwoTwo ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
					if (key == BarButtonPresetEnum::Recall)
					{
						size = recall.undoExpanded ? ButtonSizeEnum::twoTwo : ButtonSizeEnum::twoOne;
						if (recall.mainRedoVisible)
						{
							redo->hide = false;
							redo->size = ButtonSizeEnum::oneOne;
							redo->state->state = WidgetState::Disable;
							append(redo, PreviewHitKind::Companion, pos);
						}
					}
					b->size = compact && size != ButtonSizeEnum::oneTwo ? ButtonSizeEnum::oneOne : size;
					b->hide = false;
					b->state->state = key == BarButtonPresetEnum::Recall ? WidgetState::Disable :
						key == BarButtonPresetEnum::Draw || (inputs.settingsVisible && key == BarButtonPresetEnum::Setting)
							? WidgetState::Selected : WidgetState::None;
					append(b, key == BarButtonPresetEnum::Divider ? PreviewHitKind::UserDivider : PreviewHitKind::Item, pos);
				}
			};
			appendZone(Segment::A1);
			append(boundary[0], PreviewHitKind::Boundary, {});
			appendZone(Segment::B1);
			more->size = compact ? ButtonSizeEnum::oneOne : ButtonSizeEnum::twoTwo;
			append(more, PreviewHitKind::More, {});
			append(boundary[1], PreviewHitKind::Boundary, {});
			appendZone(Segment::A2);
			appendZone(Segment::More);
			more->state->state = expanded && panelButtons.HasButtons() ? WidgetState::Selected : WidgetState::None;
		}
		void BuildGeometry();
	};

	D2D1_RECT_F PreviewTestAccess::HintBounds(const BarContentInstance& instance)
	{
		const auto& r = instance.state_->hintRect;
		return D2D1::RectF(float(r.left),float(r.top),float(r.right),float(r.bottom));
	}
	void PreviewTestAccess::ShowGeometryBadge(BarContentInstance& instance)
	{
		for (const auto& b : instance.state_->main)
			if (b->preset == BarButtonPresetEnum::Geometry && b->badge) b->badge->shown = true;
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

	BarContentInstance::BarContentInstance() : state_(std::make_unique<State>()) {}
	BarContentInstance::~BarContentInstance() = default;
	void BarContentInstance::SetLayout(const BarLayoutModel& model, std::uint64_t revision)
	{
		auto& s = *state_;
		const bool changed = !s.prototypes || !(s.model == model);
		if (!changed && revision == s.revision) return;
		if (changed)
		{
			s.model = model;
			s.editWidth = 0;
			s.Rebuild();
			s.lifted.reset();
		}
		if (revision != s.revision) s.lifted.reset();
		s.revision = revision;
		s.Invalidate();
	}
	bool BarContentInstance::Lift(Position pos, std::uint64_t revision)
	{
		auto& s = *state_;
		if (s.lifted || revision != s.revision || static_cast<size_t>(pos.segment) >= s.entries.size()
			|| pos.index >= s.model[pos.segment].size()) return false;
		if (std::none_of(s.presented.items.begin(),s.presented.items.end(),[&](const PreviewHit& item)
			{ return item.position && SamePosition(*item.position,pos); })) return false;
		s.editWidth = (std::max)(static_cast<double>(s.body.w.val), static_cast<double>(s.body.w.tar));
		s.lifted = pos;
		s.Invalidate();
		return true;
	}
	void BarContentInstance::CancelLift()
	{
		if (!state_->lifted) return;
		state_->lifted.reset();
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
			placed = s.Make(id);
		}
		target.insert(target.begin() + gap.index,placed);
		s.model = accepted;
		s.lifted.reset();
		s.Invalidate();
		return true;
	}
	void BarContentInstance::SetMoreExpanded(bool value)
	{
		if (state_->expanded == value) return;
		state_->expanded = value;
		state_->Invalidate();
	}
	void BarContentInstance::Suspend()
	{
		auto& s = *state_;
		s.clock.SuspendForIdle(s.inputs.time);
		s.lifted.reset();
		if (s.hovered) UI3::StopButtonHoverVisual(*s.hovered,true);
		s.hovered.reset();
		s.hintText.clear();
		s.hintShown = false;
		s.blockedHintSerial = s.inputs.pointerSerial;
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
			auto hit = identities.at(b.get());
			hit.rect = Pixels(b->button,frame.scale);
			pending.items.push_back(hit);
		};
		if (expanded && panelButtons.HasButtons() && opacity.val > 0.000001)
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
					if (redo && zone == Segment::A1 && inputs.sizeMode == BarSizeMode::Compact
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
		if (!s.prototypes)
		{
			s.PreparePrototypes();
			s.Rebuild();
		}
		else if (languageChanged)
		{
			s.PreparePrototypes();
			// 新语言只更新原型文字，保留正在拖动的 occurrence 与其在途动画。
			for (size_t z = 0; z < s.entries.size(); ++z)
				for (size_t i = 0; i < s.entries[z].size(); ++i)
					if (auto& b = s.entries[z][i])
						if (auto fresh = s.Make(s.model.segments[z][i].Id))
							b->name.SetStringImmediate(fresh->name.content.GetTar());
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
		const auto& metrics = ResolveBarSizeMetrics(inputs.sizeMode);
		const auto packed = ResolveBarButtonListLayout(s.main,BarButtonListLayoutKind::Main,{},inputs.sizeMode);
		const auto order = inputs.iconOrder == 3 ? BarIconOrder::RightToLeft : BarIconOrder::LeftToRight;
		const double dt = s.clock.Tick(inputs.time);
		const bool animate = UiAnimationEnabled && s.initialized;
		const double rate = UiAnimationSpeedRate;
		UiAnimationAdvanceContextClass tick{dt,rate,animate,!animate};
		ButtonAnimationContext motion{dt,rate,animate,!animate};
		const double light = GetThemeLightWeight();
		const bool dark = light < 0.5;
		s.body.lightMaterial = s.panel.lightMaterial = light;
		const bool grows = packed.width > s.body.w.val;
		const auto curve = grows ? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseOutCubic;
		s.body.w.SetTar(packed.width,UiDefaultOperationDur,nullopt,false,{curve,curve,0,false});
		frame.active = UiAdvanceAnimation(s.body.w,tick).active;
		s.body.fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));
		s.body.frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
		s.body.h.SetDirect(metrics.barHeight);
		const bool open = s.expanded && s.panelButtons.HasButtons();
		auto advancePanel = [&](UiValueClass& value, UiCurveEnum ease)
		{
			value.SetTar(open ? 1.0 : 0.0,UiDefaultOperationDur,nullopt,false,{ease,ease,0,false});
			frame.active |= UiAdvanceAnimation(value,tick).active;
		};
		advancePanel(s.progress,open ? UiCurveEnum::EaseOutBack : UiCurveEnum::EaseInBack);
		advancePanel(s.opacity,open ? UiCurveEnum::EaseOutSine : UiCurveEnum::EaseInSine);
		const double shadow = ThemeMaterial::SurfaceShadowOutsetDip + RenderingAttribute::dirtyAntialiasPadding;
		if (!s.lifted && s.body.w.IsSame()) s.editWidth = 0;
		// 抓起期间锁住适配上限，否则在窄宿主里底板越缩短，按钮反而越大。
		double envelope = (std::max)({packed.width,static_cast<double>(s.body.w.val),s.editWidth});
		double panelSpace = 0, targetPanelSpace = 0;
		double targetWidth = (std::max)(packed.width,s.editWidth);
		if (open || s.opacity.val > 0)
		{
			const auto grid = ResolveBarButtonListLayout(s.panelButtons.explicitMore,BarButtonListLayoutKind::GroupedGrid,{},inputs.sizeMode);
			const auto size = MeasureMoreGrid(grid,metrics.gap);
			// 以当前姿态预约空间，反向关闭时从同一位置释放。
			const double overshoot = (std::max)(0.0,static_cast<double>(s.progress.val));
			const double height = size.height + BarMorePanelAnchorGap + metrics.barHeight/2;
			panelSpace = height * overshoot;
			if (open)
			{
				targetWidth = (std::max)(targetWidth,size.width);
				targetPanelSpace = height;
			}
			envelope = (std::max)(envelope, size.width * overshoot);
		}
		frame.scale = (std::min)(inputs.targetHeight/metrics.barHeight,inputs.width/(envelope+2*shadow));
		frame.scale = (std::min)(frame.scale, inputs.height/(metrics.barHeight+panelSpace+2*shadow));
		const double zoom = frame.scale;
		const double bodyTop = (std::max)((inputs.height/zoom-metrics.barHeight)/2, panelSpace+shadow);
		s.body.UpInh(UiInheritClass((inputs.width/zoom-s.body.w.val)/2,bodyTop));
		if (!s.initialized) s.renderer.ConfigureLocalizedTypography();
		else s.renderer.RefreshLocalizedTypography();
		s.renderer.UpdateFontRenderingPolicy(inputs.fractionalFont);
		s.renderer.SetFrameZoom(zoom);
		const double targetScale = (std::min)({inputs.targetHeight/metrics.barHeight,inputs.width/(targetWidth+2*shadow),
			inputs.height/(metrics.barHeight+targetPanelSpace+2*shadow)});
		frame.active |= s.renderer.FontAnimationsActive();
		s.renderer.BeginFontFrame(inputs.time,tick,targetScale);
		s.renderer.SetFontTargetScale(1.0);
		for (const auto& p : packed.placements)
		{
			auto& b = *p.button;
			b.button.x.SetTar(ResolveBarMainIconX(p.x,packed.width,order,true),UiDefaultOperationDur);
			b.button.y.SetTar(p.y,UiDefaultOperationDur);
			const bool separator = b.preset == BarButtonPresetEnum::Divider;
			if (separator)
			{
				b.button.w.SetTar(p.width,UiDefaultOperationDur);
				b.button.h.SetTar(p.height,UiDefaultOperationDur);
				b.button.fill->SetDirect(GetThemeColor(ThemeColorEnum::Divider));
				b.button.frame->SetDirect(GetThemeColor(ThemeColorEnum::Divider));
				b.button.frameLightPct->SetDirect(1);
				b.button.frameCursorLightIntensityScale = BarGeometryAttributeDividerCursorLightIntensity;
			}
			else
			{
				const auto kind = b.size == ButtonSizeEnum::oneOne ? ButtonVisualLayoutKind::StandardOneOne
					: b.size == ButtonSizeEnum::twoOne ? ButtonVisualLayoutKind::StandardTwoOne : ButtonVisualLayoutKind::StandardTwoTwo;
				UI3::ConfigureButtonVisualTargets(b,kind,UiDefaultOperationDur,1,false,
					b.preset == BarButtonPresetEnum::More && b.size == ButtonSizeEnum::twoTwo ? std::optional<double>{34} : nullopt,
					nullopt,b.LabelFontSize(ResolveButtonVisualMetrics(kind).primaryFontSizeDip),
					b.preset == BarButtonPresetEnum::Geometry ? &s.renderer : nullptr);
				UI3::RetargetButtonInteractionVisual(dark,b,true,b.state->state != WidgetState::Disable,
					b.state->state == WidgetState::Selected,UiDefaultOperationDur);
				if (b.size == ButtonSizeEnum::oneOne) b.name.pct.SetTar(0,UiDefaultOperationDur);
			}
			frame.active |= Bar::AdvanceButtonVisual(b,motion).active;
			b.button.Inherit(UiInheritEnum::CenterFromTopLeft,s.body);
		}
		PositionContentMore(s.Chrome(),s.more.get(),s.body,s.panelButtons,inputs.sizeMode,s.progress,s.opacity,
			open,false,inputs.width,inputs.height,zoom,{},dark,false,s.closeHover,s.renderer,shadow);
		for (auto& b : s.panelButtons.explicitMore)
			frame.active |= Bar::AdvanceButtonVisual(*b,motion).active;
		frame.active |= UI3::AdvanceShapeVisual(s.close,motion).active;
		s.BuildGeometry();
		ButtonPtr hovered;
		if (inputs.pointer && inputs.mouse && !inputs.editing && !s.lifted)
		{
			const auto hit = HitGeometry(s.pending,*inputs.pointer);
			if (hit.kind == PreviewHitKind::More) hovered = s.more;
			else if (hit.kind == PreviewHitKind::Companion) hovered = s.redo;
			else if (hit.kind == PreviewHitKind::Item)
				hovered = s.entries[static_cast<size_t>(hit.position->segment)][hit.position->index];
		}
		if (s.hovered != hovered)
		{
			if (s.hovered) UI3::StopButtonHoverVisual(*s.hovered,false);
			if (hovered) UI3::StartButtonHoverVisual(*hovered);
			s.hovered = hovered;
			frame.active = true;
		}
		const bool movingLayout = !s.body.w.IsSame() || !s.progress.IsSame()
			|| std::any_of(s.main.begin(),s.main.end(),[](const auto& b){return !b->button.x.IsSame() || !b->button.y.IsSame();});
		if (movingLayout || inputs.editing || s.lifted || !inputs.mouse) s.blockedHintSerial = inputs.pointerSerial;
		const bool hintAllowed = !s.blockedHintSerial || *s.blockedHintSerial != inputs.pointerSerial;
		auto text = hovered && hintAllowed ? hovered->name.content.GetTar() : std::wstring{};
		std::replace(text.begin(),text.end(),L'\n',L' ');
		if (text != s.hintText && (!s.hintShown || text.empty())) { s.entered = inputs.time; s.hintShown = false; }
		s.hintText = text;
		if (!text.empty())
		{
			const auto due = s.entered + std::chrono::milliseconds(500);
			if (inputs.time >= due) s.hintShown = true;
			else frame.hintDeadline = due;
			if (s.hintShown)
			{
				const auto size = s.renderer.MeasureText(text,BarHoverHintFontSizeDip);
				s.hintRect = ResolveBarHoverHintRect(inputs.pointer->x,inputs.pointer->y,
					((std::min)(std::ceil(double(size.width)),BarHoverHintMaxTextWidthDip)+2*BarHoverHintPaddingXDip)*zoom,
					(std::ceil(size.height)+2*BarHoverHintPaddingYDip)*zoom,BarHoverHintCursorOffsetDip*zoom,
					{shadow*zoom,shadow*zoom,inputs.width-shadow*zoom,inputs.height-shadow*zoom});
			}
		}
		UiFrameLightingInput lights;
		for (auto& b : s.main)
		{
			if (b->preset == BarButtonPresetEnum::Divider) continue;
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
		// 提供实际内容包络；阴影、过冲与最上层提示都参与宿主的纹理预算。
		auto bounds = s.pending.body;
		auto include = [&](D2D1_RECT_F r)
		{
			bounds.left = (std::min)(bounds.left,r.left); bounds.right = (std::max)(bounds.right,r.right);
			bounds.top = (std::min)(bounds.top,r.top); bounds.bottom = (std::max)(bounds.bottom,r.bottom);
		};
		for (const auto& item : s.pending.items) include(item.rect);
		if (s.panel.pct.val > 0.000001) include(Pixels(s.panel,zoom));
		if (inputs.reserveHintSpace)
		{
			// 宿主按包络裁图时为提示预留上沿，避免 500ms 后按钮从静止指针下移走。
			bounds.top = (std::max)(float(shadow*zoom),bounds.top-float((BarHoverHintMaxHeightDip+BarHoverHintCursorOffsetDip)*zoom));
		}
		if (s.hintShown) include(D2D1::RectF(static_cast<float>(s.hintRect.left),static_cast<float>(s.hintRect.top),
			static_cast<float>(s.hintRect.right),static_cast<float>(s.hintRect.bottom)));
		const auto outset = static_cast<float>(std::ceil(shadow*zoom));
		frame.bounds = D2D1::RectF(bounds.left-outset,bounds.top-outset,bounds.right+outset,bounds.bottom+outset);
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
		if (s.hintShown)
			DrawContentHint(s.renderer,dc,s.hintRect,s.hintText,s.frame.scale,s.body.lightMaterial,
				s.hintShape,*s.formats,s.inputs.fonts);

		s.drawn = frame.frameId;
		return S_OK;
	}
	LocalizedTypography BarContentInstance::CurrentTypography()
	{
		return MakeBarLocalizedTypographyServices().localizedTypography();
	}
	HRESULT BarContentInstance::DrawGhost(FrameTarget& target, const std::variant<Position,std::string>& source,
		D2D1_POINT_2F point, const std::wstring& hint, bool danger)
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
		s.renderer.SetFrameZoom(zoom*1.05);
		const double x = point.x/(zoom*1.05)-button.button.w.val/2;
		const double y = point.y/(zoom*1.05)-button.button.h.val/2;
		button.button.lightMaterial = GetThemeLightWeight();
		button.button.themeSurface = button.preset != BarButtonPresetEnum::Divider;
		button.button.pct.SetDirect(danger ? 0.4 : 0.85);
		s.renderer.SetResidentContentOpacity(danger ? 0.45 : 1);
		DrawButtonVisual(s.renderer,dc,button,UiInheritClass(x,y));
		s.renderer.SetResidentContentOpacity(1);
		s.renderer.SetFrameZoom(zoom);
		return DrawFollowingHint(target,point,hint,button.button.h.val/2+BarHoverHintCursorOffsetDip);
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
