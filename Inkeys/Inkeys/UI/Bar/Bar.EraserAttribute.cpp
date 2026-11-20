module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../Drawing/Draw3/Assets/EraserGripVisual.h"
#include "../../Drawing/Draw3/Draw3.EraserCursor.h"
#include <algorithm>
#include <array>
#include <cmath>

module Inkeys.UI.Bar;
import :Main;
import :Theme;
import Inkeys.UI.Setting;

using namespace Inkeys::UI::UI3;

using namespace Inkeys::UI::Bar;
using namespace Inkeys::Drawing::Draw3::SpeedEraser;
extern const double BarUiDividerRadius;
extern const double BarUiDividerCursorLightIntensity;
extern const double BarDrawAttributeSurfaceOpacity;
extern const double BarDrawAttributeCompactWidth;

namespace
{
	constexpr double EraserSizeSelectionGapDip=3.0;
	constexpr double EraserSizeSelectionThicknessDip=1.0;
	constexpr double EraserAutomaticArrowSizeDip=18.0;
	struct EraserSideSwitchDurations
	{
		double collapse=0,expand=0;
	};
	EraserSideSwitchDurations ResolveEraserSideSwitchDurations(
		double duration,const UiTimelineClass* parent,double dt,double speed) noexcept
	{
		duration=std::isfinite(duration) && duration>0?duration:0;
		if(parent)
		{
			const double observedProgress=parent->GetProgress();
			const double remaining=parent->GetRemainingDuration();
			const double full=observedProgress<1?remaining/(1-observedProgress):duration;
			double progress=observedProgress;
			// RenderLoop在调用子面板前已推进父时间线一帧；回退该帧后再加入，避免子面板抢跑。
			if(full>0 && std::isfinite(dt) && dt>0 && std::isfinite(speed) && speed>0)
				progress=(std::max)(0.0,progress-dt*speed/full);
			if(full>0 && progress<=0.5)
			{
				// 与绘制属性的0.5关键帧一致：只补父批次尚未走完的前半程，后半程保持完整。
				return {(std::max)(0.0,(0.5-progress)*full),full/2};
			}
		}
		return {duration/2,duration/2};
	}
	D2D1_RECT_F PixelRect(EraserAttributeRect r,double zoom)
	{ return D2D1::RectF(static_cast<float>(r.left*zoom),static_cast<float>(r.top*zoom),static_cast<float>(r.right*zoom),static_cast<float>(r.bottom*zoom)); }
	RECT IntegerRect(EraserAttributeRect r,double zoom,int outset=0)
	{
		if(r.Width()<=0 || r.Height()<=0)return {};
		return {static_cast<LONG>(floor(r.left*zoom))-outset,static_cast<LONG>(floor(r.top*zoom))-outset,
			static_cast<LONG>(ceil(r.right*zoom))+outset,static_cast<LONG>(ceil(r.bottom*zoom))+outset};
	}
	void Place(UiShapeClass& shape,EraserAttributeRect r)
	{
		shape.x.SetDirect(r.left);shape.y.SetDirect(r.top);shape.w.SetDirect(r.Width());shape.h.SetDirect(r.Height());
		shape.UpInh(UiInheritClass(r.left,r.top));
	}
	COLORREF EraserOutlineColor() noexcept
	{ const auto c=static_cast<BYTE>(std::lround(ERASER_GRIP_OUTLINE_CHANNEL*255));return RGB(c,c,c); }

	bool IsSize(int item) noexcept { return item>=1 && item<=3; }
	bool IsPanelButton(int item) noexcept { return item<6 || item==10; }
	int ButtonVisual(int item) noexcept { return item==5?4:IsSize(item)?-1:item; }
	constexpr std::array<int,7> buttonVisuals{0,4,6,7,8,9,10};
	wstring ButtonLabel(int item)
	{
		const auto& t=I18nKey.UI.Bar.EraserAttributes;
		switch(item)
		{
		case 0:return I18n::getWOr(t.ClearCanvasLabel,L"清空");
		case 4:return I18n::getWOr(t.AutomaticLabel,L"自动粗细");
		case 6:return I18n::getWOr(t.LowLabel,L"低");
		case 7:return I18n::getWOr(t.MediumLabel,L"中");
		case 8:return I18n::getWOr(t.HighLabel,L"高");
		case 10:return I18n::getWOr(I18nKey.SettingsUI.Draw.Eraser.PalmEraser.N,L"手掌橡皮");
		default:return {};
		}
	}
}

void BarEraserAttributePanel::Initialize()
{
	if(initialized_)return;
	panel_.BindFrame(surface_);menuPanel_.BindFrame(menu_);
	for(auto* shape:{&surface_,&menu_})
	{
		shape->Initialization(0,0,0,0,BarMainBarCornerRadiusDip,BarMainBarCornerRadiusDip,ButtonFrameThicknessDip,
			GetThemeColor(ThemeColorEnum::Surface),GetThemeColor(ThemeColorEnum::SurfaceFrame));
		shape->enable.Initialization(true);shape->pct.SetDirect(BarDrawAttributeSurfaceOpacity);
		shape->framePct.emplace(GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity));shape->frameLightPct.emplace(BarMainBarFrameOpacity);
		shape->frameRendering=UiFrameRenderingEnum::PointLight;shape->frameLightColor=UiFrameLightColorEnum::PenWhenDrawing;
	}
	const auto metrics=ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoTwo);
	for(int i:buttonVisuals)
	{
		auto& b=buttons_[i];b.hide=false;b.size=ButtonSizeEnum::twoTwo;
		b.button.Initialization(0,0,0,0,ButtonCornerRadiusDip,ButtonCornerRadiusDip,ButtonFrameThicknessDip,
			GetThemeColor(ThemeColorEnum::PressedFill),GetThemeColor(ThemeColorEnum::SurfaceFrame));
		b.button.enable.Initialization(true);b.button.pct.SetDirect(0);
		b.button.framePct.emplace(0);b.button.frameLightPct.emplace(0);
		b.button.frameRendering=UiFrameRenderingEnum::PointLight;b.button.frameLightColor=UiFrameLightColorEnum::Frame;
		b.button.framePrimaryLightEnabled=false;b.button.frameCursorLightIntensityScale=ButtonCursorLightIntensity;
		const bool large=i==0 || i==4 || i==10;const auto label=ButtonLabel(i);
		b.name.Initialization(0,0,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip,label,metrics.primaryFontSizeDip,
			GetThemeColor(ThemeColorEnum::TextPrimary));
		b.name.enable.Initialization(!label.empty());b.name.pct.SetDirect(1);
		b.icon.Initialization(0,0,GetThemeColor(ThemeColorEnum::TextPrimary),std::nullopt);
		b.icon.enable.Initialization(large || i==9);b.icon.pct.SetDirect(1);
		// 面板图标显式映射：清空画布沿用主栏垃圾桶，自动粗细、手掌橡皮、笔速橡皮设置用专属资源。
		const wchar_t* iconResource=i==0?L"barClean":i==4?L"barAutoEraser":i==10?L"barPalmEraser":i==9?L"barEraserSpeedSetting":nullptr;
		if(iconResource)b.icon.InitializationFromResource(L"UI",iconResource);
		b.icon.SetWH(large?metrics.iconSizeDip:20,large?metrics.iconSizeDip:20);
		b.icon.w.SetDirect(b.icon.w.tar);b.icon.h.SetDirect(b.icon.h.tar);
	}
	for(size_t i=0;i<circles_.size();++i)
	{
		auto& circle=circles_[i];
		circle.Initialization(0,0,1,1,0.5,0.5,1,std::nullopt,EraserOutlineColor());
		circle.enable.Initialization(true);circle.pct.SetDirect(0);circle.framePct.emplace(0);circle.frameLightPct.emplace(0);
		circle.frameRendering=UiFrameRenderingEnum::PointLight;circle.frameLightColor=UiFrameLightColorEnum::Frame;
		circle.frameCursorLightIntensityScale=ButtonCursorLightIntensity;
		auto& ring=circleSelectionRings_[i];
		ring.Initialization(0,0,1,1,0.5,0.5,EraserSizeSelectionThicknessDip,std::nullopt,
			GetThemeColor(ThemeColorEnum::Accent));
		ring.enable.Initialization(true);ring.pct.SetDirect(0);ring.framePct.emplace(0);ring.frameLightPct.emplace(0);
		ring.frameRendering=UiFrameRenderingEnum::PointLight;ring.frameLightColor=UiFrameLightColorEnum::Frame;
		ring.framePrimaryLightEnabled=false;ring.frameCursorLightIntensityScale=ButtonCursorLightIntensity;
		circleSelection_[i].Initialization(0);circleHover_[i].Initialization(0);circlePress_[i].Initialization(1);
	}
	for(auto& d:dividers_)
	{
		d.Initialization(0,0,1,1,BarUiDividerRadius,BarUiDividerRadius,ButtonFrameThicknessDip,
			GetThemeColor(ThemeColorEnum::SurfaceFrame),GetThemeColor(ThemeColorEnum::SurfaceFrame));
		d.enable.Initialization(true);d.pct.SetDirect(0.30);d.framePct.emplace(0);d.frameLightPct.emplace(1);
		d.frameRendering=UiFrameRenderingEnum::PointLight;d.framePrimaryLightEnabled=false;d.frameCursorLightIntensityScale=BarUiDividerCursorLightIntensity;
	}
	automaticDivider_.Initialization(0,0,1,1,BarUiDividerRadius,BarUiDividerRadius,ButtonFrameThicknessDip,
		GetThemeColor(ThemeColorEnum::SurfaceFrame),GetThemeColor(ThemeColorEnum::SurfaceFrame));
	automaticDivider_.enable.Initialization(true);automaticDivider_.pct.SetDirect(0.30);
	automaticDivider_.framePct.emplace(0);automaticDivider_.frameLightPct.emplace(0);
	automaticDivider_.frameRendering=UiFrameRenderingEnum::PointLight;
	automaticDivider_.frameLightColor=UiFrameLightColorEnum::Frame;
	automaticDivider_.framePrimaryLightEnabled=false;
	automaticDivider_.frameCursorLightIntensityScale=BarUiDividerCursorLightIntensity;
	automaticArrow_.Initialization(0,0,GetThemeColor(ThemeColorEnum::TextPrimary),std::nullopt);
	automaticArrow_.InitializationFromResource(L"UI",L"barThicknessAdjust");
	automaticArrow_.SetWH(EraserAutomaticArrowSizeDip,EraserAutomaticArrowSizeDip);
	automaticArrow_.enable.Initialization(true);automaticArrow_.pct.SetDirect(1);
	title_.Initialization(0,0,0,ButtonOneSideDip,L"灵敏度",ButtonTwoTwoLabelFontSizeDip,GetThemeColor(ThemeColorEnum::TextPrimary));
	title_.enable.Initialization(true);title_.pct.SetDirect(1);
	initialized_=true;
}

void BarEraserAttributePanel::ConfigureSurface(UiShapeClass& surface,EraserAttributeRect rect,double scale)
{
	Place(surface,rect);surface.rw->SetDirect(BarMainBarCornerRadiusDip*scale);surface.rh->SetDirect(BarMainBarCornerRadiusDip*scale);
	surface.ft->SetDirect(ButtonFrameThicknessDip*scale);
	surface.fill->SetDirect(GetThemeColor(ThemeColorEnum::Surface));surface.frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
	surface.framePct->SetDirect(GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity));
}

bool BarEraserAttributePanel::Advance(BarUISetClass& owner,double dt,double speed,double zoom,UINT dpi,
	RECT workArea,POINT origin,double rigidX,double rigidY,const UiTimelineClass* parentTimeline,
	bool dragPlacementLocked,std::optional<bool> eraserSelected)
{
	Initialize();changed_=false;active_=false;
	const bool selected = eraserSelected.has_value() ? *eraserSelected
		: GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtEraser;
	const bool shouldClose=owner.barState.fold || !selected ||
		owner.barState.drawAttribute || owner.barState.geometryAttribute || owner.barState.moreExpanded ||
		owner.barState.recallAttribute;
	if(shouldClose)Close(owner);
	const bool open=owner.barState.eraserAttribute;if(!open)owner.barState.eraserSensitivityOpen=false;
	const bool menuOpen=owner.barState.eraserSensitivityOpen;
	panel_.SetExternalState(open,panelMotion_.Geometry(),panelMotion_.Opacity());
	menuPanel_.SetExternalState(menuOpen,menuMotion_.Geometry(),menuMotion_.Opacity());
	const UiAnimationAdvanceContextClass context{dt,speed,static_cast<bool>(UiAnimationEnabled),false};
	auto advance=[&](auto& value){const auto result=UiAdvanceAnimation(value,context);changed_|=result.changed;active_|=result.active;};
	auto root=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	root->UpInh(UiInheritClass(root->x.val-root->w.val/2,root->y.val-root->h.val/2));
	auto main=owner.shapeMap[BarUISetShapeEnum::MainBar];main->Inherit(UiInheritEnum::Center,*root);
	auto anchor=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];anchor->button.Inherit(UiInheritEnum::CenterFromTopLeft,*main);
	const auto preferences=EraserPreferencesSnapshot();
	EraserAttributeLayoutInput currentInput;currentInput.shape=preferences.shape;
	currentInput.main={main->inhX,main->inhY,main->inhX+main->w.val,main->inhY+main->h.val};
	currentInput.anchor={anchor->button.inhX,anchor->button.inhY,anchor->button.inhX+anchor->button.w.val,anchor->button.inhY+anchor->button.h.val};
	currentInput.zoom=zoom;currentInput.dpiScale=dpi/96.0;
	currentInput.sizeMode=NormalizeBarSizeMode(owner.barStyle.appliedSizeMode.load(std::memory_order_acquire));
	{
		// 非居中底栏：面板不越过主按钮外侧边；按静止边长取主按钮矩形，避免按压脉冲带动面板。
		const double mainHalf=ResolveBarSizeMetrics(currentInput.sizeMode).mainButtonSide/2;
		currentInput.mainButton={root->x.val-mainHalf,root->y.val-mainHalf,root->x.val+mainHalf,root->y.val+mainHalf};
		currentInput.limitToMainButton=!owner.IsCenteredBottomDock();
	}
	currentInput.work={(workArea.left-origin.x)/zoom-rigidX,(workArea.top-origin.y)/zoom-rigidY,(workArea.right-origin.x)/zoom-rigidX,(workArea.bottom-origin.y)/zoom-rigidY};
	const bool holdDragPlacement=dragPlacementLocked && open;
	const bool releasedDragPlacement=!holdDragPlacement && dragPlacementLocked_;
	if(holdDragPlacement && !dragPlacementLocked_)
	{
		// HWND 直移期间沿用上一帧的局部几何；松手吸收后再由现有换边动画接管。
		dragLayoutInput_=hasStableLayoutInput_?stableLayoutInput_:currentInput;
		dragLayoutInput_.below=layout_.below;dragLayoutInput_.reversed=layout_.reversed;
		dragLayoutInput_.lockedMenuSide=menuSide_;dragLayoutInput_.lockPanelSide=true;
		dragPlacementLocked_=true;releaseSwitchLayoutLocked_=false;
	}
	else if(releasedDragPlacement)
	{
		// 松手吸收HWND位移时同步平移旧工作区，避免真实工作区夹取先把面板闪回屏内。
		dragLayoutInput_=RebaseEraserAttributeLayoutInput(dragLayoutInput_,currentInput.anchor);
		dragLayoutInput_.lockedMenuSide=menuSide_;dragLayoutInput_.lockPanelSide=true;
		dragPlacementLocked_=false;
	}
	const bool targetBelow=holdDragPlacement?previousBelow_:static_cast<bool>(owner.barState.widgetPosition.primaryBar);
	const bool targetReversed=holdDragPlacement?previousReversed_:!static_cast<bool>(owner.barState.widgetPosition.mainBar);
	bool switching=targetBelow!=previousBelow_ || targetReversed!=previousReversed_;
	if(releasedDragPlacement)releaseSwitchLayoutLocked_=switching;
	else if(!switching)releaseSwitchLayoutLocked_=false;
	const bool beginSideSwitch=switching && !sideSwitchActive_;
	double panelDuration=static_cast<double>(UiDefaultOperationDur);
	if(beginSideSwitch)
	{
		const auto durations=ResolveEraserSideSwitchDurations(UiDefaultOperationDur,parentTimeline,dt,speed);
		panelDuration=durations.collapse;sideSwitchExpandDuration_=durations.expand;
		sideSwitchActive_=true;
	}
	else if(!switching)sideSwitchActive_=false;
	const UiTimelineClass* panelParent=switching?nullptr:parentTimeline;
	changed_|=panelMotion_.Retarget(open && !switching,panelDuration,panelParent);
	changed_|=menuMotion_.Retarget(menuOpen && !switching,panelDuration,&panelMotion_.Timeline());
	double sideSwitchCarryDt=0;
	if(switching && context.animationEnabled && std::isfinite(dt) && dt>0 && std::isfinite(speed) && speed>0)
	{
		const double collapseSeconds=panelMotion_.Active()?panelMotion_.Timeline().GetRemainingDuration()/speed:0;
		sideSwitchCarryDt=(std::max)(0.0,dt-collapseSeconds);
	}
	changed_|=panelMotion_.Advance(context);changed_|=menuMotion_.Advance(context);
	// 与绘制属性一致，在一个默认时长的前后半段完成收拢与展开。
	if(switching && !panelMotion_.Visible())
	{
		previousBelow_=targetBelow;previousReversed_=targetReversed;menuSide_=-1;
		releaseSwitchLayoutLocked_=false;sideSwitchActive_=false;
		changed_|=panelMotion_.Retarget(open,sideSwitchExpandDuration_);
		changed_|=menuMotion_.Retarget(menuOpen,sideSwitchExpandDuration_,&panelMotion_.Timeline());
		if(!context.animationEnabled){changed_|=panelMotion_.Advance(context);changed_|=menuMotion_.Advance(context);}
		else if(sideSwitchCarryDt>0)
		{
			// 帧步跨过透明中点时把余量交给展开段，避免相较绘制属性慢一帧。
			auto carryContext=context;carryContext.dtSeconds=sideSwitchCarryDt;
			changed_|=panelMotion_.Advance(carryContext);changed_|=menuMotion_.Advance(carryContext);
		}
		switching=false;
	}
	active_|=panelMotion_.Active() || menuMotion_.Active();if(!menuOpen && !menuMotion_.Visible())menuSide_=-1;
	const bool holdReleaseSwitchPlacement=switching && releaseSwitchLayoutLocked_;
	if(holdReleaseSwitchPlacement)
		dragLayoutInput_=RebaseEraserAttributeLayoutInput(dragLayoutInput_,currentInput.anchor);
	EraserAttributeLayoutInput input=(holdDragPlacement || holdReleaseSwitchPlacement)?dragLayoutInput_:currentInput;
	input.shape=preferences.shape; // 直拖只锁定摆放，形状仍使用本帧完整偏好。
	if(!holdDragPlacement && !holdReleaseSwitchPlacement)
	{
		input.below=previousBelow_;input.reversed=previousReversed_;input.lockedMenuSide=menuSide_;
		// 收拢到紧凑态之前继续显示原侧，避免 release 首帧被工作区避让直接闪到另一边。
		input.lockPanelSide=switching;
		stableLayoutInput_=input;hasStableLayoutInput_=true;
	}
	layout_=ResolveEraserAttributeLayout(input);if(menuOpen && menuSide_<0)menuSide_=layout_.menuBelow?1:0;
	EraserAttributePresentation next;next.zoom=zoom;
	auto panelFrame=ResolveBarAttributePanelFrame(panelMotion_,layout_.panel,input.anchor,BarDrawAttributeCompactWidth);
	panelFrame.pose=FitEraserSurfacePose(panelFrame.pose,layout_.panel,input.work);
	auto menuFrame=ResolveBarAttributePanelFrame(menuMotion_,layout_.menu,layout_.automatic,
		BarDrawAttributeCompactWidth,&panelFrame);
	menuFrame.pose=FitEraserSurfacePose(menuFrame.pose,layout_.menu,input.work);
	next.panelPose=panelFrame.pose;next.menuPose=menuFrame.pose;
	next.geometry=TransformEraserAttributeLayout(layout_,next.panelPose,next.menuPose);
	next.panelOpacity=panelFrame.opacity;next.menuOpacity=menuFrame.opacity;
	next.panelVisible=panelFrame.visible;next.menuVisible=menuFrame.visible;
	changed_|=frame_.geometry!=next.geometry || frame_.zoom!=next.zoom || frame_.panelOpacity!=next.panelOpacity ||
		frame_.menuOpacity!=next.menuOpacity || frame_.panelVisible!=next.panelVisible || frame_.menuVisible!=next.menuVisible ||
		frame_.panelPose.scale!=next.panelPose.scale || frame_.menuPose.scale!=next.menuPose.scale;
	frame_=next;zoom_=zoom;
	const auto& g=frame_.geometry;
	const auto automatic=GetAutomaticState(preferences);
	std::array<float,3> diametersPx{};
	for(size_t i=0;i<3;++i)diametersPx[i]=input.diametersDip[i]*static_cast<float>(input.dpiScale);
	if(previewShape_!=preferences.shape || previewPattern_!=preferences.cursorPattern || previewTheme_!=preferences.cursorTheme ||
		previewDiametersPx_!=diametersPx || previewDpiScale_!=static_cast<float>(input.dpiScale))
	{
		changed_=true;for(auto& bitmap:previewBitmaps_)bitmap.Reset();previewBitmapAttempted_.fill(false);
		previewShape_=preferences.shape;previewPattern_=preferences.cursorPattern;previewTheme_=preferences.cursorTheme;
		previewDiametersPx_=diametersPx;previewDpiScale_=static_cast<float>(input.dpiScale);
	}
	const bool palmEnabled=PalmEraserPreferencesSnapshot().enabled;
	changed_|=palmEnabled!=palmEnabled_;palmEnabled_=palmEnabled;
	changed_|=automatic!=automatic_ || selectedSize_!=static_cast<int>(preferences.baseSize) || sensitivity_!=static_cast<int>(preferences.sensitivity);
	automatic_=automatic;selectedSize_=static_cast<int>(preferences.baseSize);sensitivity_=static_cast<int>(preferences.sensitivity);
	changed_|=surface_.fill->val!=GetThemeColor(ThemeColorEnum::Surface);
	ConfigureSurface(surface_,g.panel,frame_.panelPose.scale);ConfigureSurface(menu_,g.menu,frame_.menuPose.scale);
	const auto metrics=ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoTwo);
	const int requestedHover=ButtonVisual(hovered_.load());
	if(appliedHover_!=requestedHover){
		if(appliedHover_>=0)StopButtonHoverVisual(buttons_[appliedHover_],false);
		if(requestedHover>=0)StartButtonHoverVisual(buttons_[requestedHover]);
		appliedHover_=requestedHover;changed_=true;
	}
	for(int i:buttonVisuals)
	{
		auto& b=buttons_[i];changed_|=b.name.SetStringImmediate(ButtonLabel(i));
		// 小尺寸自动粗细与手掌橡皮只显示图标：关闭文字而不改写透明度（透明度由共享交互视觉统一驱动）；
		// 清空按钮在小尺寸下按不可见处理，图标与文字由共享交互视觉淡出。
		b.name.enable.Initialization(!ButtonLabel(i).empty() && !(g.compact && (i==4 || i==10)));
		const bool selected=(i==4 && automatic_==AutomaticState::On) || (i==10 && palmEnabled_) || (i>=6 && i<=8 && sensitivity_==i-6);
		// 小尺寸没有清空按钮；自动粗细与手掌橡皮改用 1×1 仅图标视觉。
		const bool compactPanel=g.compact;
		const bool presented=(IsPanelButton(i)?frame_.panelVisible:frame_.menuVisible) && !(compactPanel && i==0);
		const double scale=IsPanelButton(i)?frame_.panelPose.scale:frame_.menuPose.scale;
		b.state->state=selected?WidgetState::Selected:WidgetState::None;
		SetButtonPressedVisual(b,ButtonVisual(pressed_)==i);

		RetargetButtonInteractionVisual(b,presented,true,selected,ButtonHoverTransitionDuration,
			std::nullopt,true,std::nullopt,GetThemeColor(selected?ThemeColorEnum::Accent:ThemeColorEnum::SurfaceFrame));
		Place(b.button,i==4?g.automatic:g.items[i]);
		b.button.rw->SetDirect(ButtonCornerRadiusDip*scale);b.button.rh->SetDirect(ButtonCornerRadiusDip*scale);b.button.ft->SetDirect(ButtonFrameThicknessDip*scale);
		const bool large=(i==0 || i==4 || i==10) && !compactPanel;
		if(i==4 || i==10)
		{
			// 图标尺寸随档位双向切换：小尺寸 1×1 为 20，大尺寸恢复 2×2 的 28。
			const double iconSide=compactPanel?20.0:metrics.iconSizeDip;
			b.icon.SetWH(iconSide,iconSide);b.icon.w.SetDirect(b.icon.w.tar);b.icon.h.SetDirect(b.icon.h.tar);
		}
		const double offset=i==4?-EraserAttributeAutomaticArrowWidthDip/2:0;
		b.icon.x.SetDirect(offset*scale);b.icon.y.SetDirect(large?metrics.iconOffsetYDip*scale:0);b.icon.contentScale=scale;
		b.name.x.SetDirect(offset*scale);b.name.y.SetDirect(large?metrics.primaryOffsetYDip*scale:0);
		b.name.w.SetDirect(large?metrics.primarySlotWidthDip*scale:static_cast<double>(b.button.w.val));b.name.h.SetDirect(metrics.primarySlotHeightDip*scale);b.name.size.SetDirect(metrics.primaryFontSizeDip*scale);
		b.name.fontTargetSize=metrics.primaryFontSizeDip*(IsPanelButton(i)?panelFrame.targetPose.scale:menuFrame.targetPose.scale);
		ButtonAnimationContext buttonFrame;
		buttonFrame.dtSeconds=dt;buttonFrame.speedRate=speed;buttonFrame.animationEnabled=UiAnimationEnabled;
		buttonFrame.visible=presented;buttonFrame.hoverAllowed=!selected;
		const auto advanced=AdvanceButtonVisual(b,buttonFrame);
		changed_|=advanced.changed;active_|=advanced.active;
	}
	for(size_t i=0;i<3;++i)
	{
		const bool selected=selectedSize_==static_cast<int>(BaseSizePresets[i]);
		circleSelection_[i].SetTar(selected?1:0,ButtonHoverTransitionDuration);
		circleHover_[i].SetTar(hovered_==static_cast<int>(i+1)?1:0,ButtonHoverTransitionDuration);
		const bool pressed=pressed_==static_cast<int>(i+1);
		circlePress_[i].SetTar(pressed?ButtonPressScale:1.0,UiDefaultOperationDur,
			std::nullopt,false,pressed?ButtonPressCurve():ButtonReleaseCurve());
		advance(circleSelection_[i]);advance(circleHover_[i]);advance(circlePress_[i]);
		auto& circle=circles_[i];const auto rect=g.previews[i];
		const double thickness=rect.Width()*ERASER_GRIP_OUTLINE_RATIO;
		// 橡皮本体保持原直径；选中态由外侧独立圆环承载。
		Place(circle,{rect.left+thickness/2,rect.top+thickness/2,rect.right-thickness/2,rect.bottom-thickness/2});
		const double corner=g.shape==Inkeys::Drawing::Draw3::EraserShape::Circle
			?(std::min)(rect.Width(),rect.Height())/2:(std::min)(g.previewCornerRadius,(std::min)(rect.Width(),rect.Height())/2);
		circle.rw->SetDirect((std::max)(0.0,corner-thickness/2));circle.rh->SetDirect((std::max)(0.0,corner-thickness/2));circle.ft->SetDirect(thickness);
		circle.frame->SetDirect(previewTheme_==Inkeys::Drawing::Draw3::EraserCursorTheme::Dark?RGB(110,110,110):EraserOutlineColor());
		circle.frameLightPct->SetDirect(circleHover_[i].val*0.5);
		auto& ring=circleSelectionRings_[i];const double ringInset=EraserSizeSelectionGapDip*frame_.panelPose.scale+
			EraserSizeSelectionThicknessDip*frame_.panelPose.scale/2;
		Place(ring,EraserAttributeSelectionOutline(rect,frame_.panelPose.scale));
		const double ringCorner=g.shape==Inkeys::Drawing::Draw3::EraserShape::Circle?ring.w.val/2:corner+ringInset;
		ring.rw->SetDirect(ringCorner);ring.rh->SetDirect(ringCorner);
		ring.ft->SetDirect(EraserSizeSelectionThicknessDip*frame_.panelPose.scale);
		ring.frame->SetDirect(GetThemeColor(ThemeColorEnum::Accent));
		ring.pct.SetDirect(circleSelection_[i].val);ring.framePct->SetDirect(circleSelection_[i].val);
		ring.frameLightPct->SetDirect(circleSelection_[i].val);
	}
	for(size_t i=0;i<dividers_.size();++i)
	{
		auto& d=dividers_[i];Place(d,g.dividers[i]);d.ft->SetDirect(ButtonFrameThicknessDip*frame_.panelPose.scale);
		// 小尺寸只保留预览组与开关组之间的一条分割线。
		d.pct.SetDirect(g.compact && i==1?0.0:0.30);d.frameLightPct->SetDirect(g.compact && i==1?0.0:1.0);
		d.rw->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);d.rh->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);
		d.fill->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));d.frame->SetDirect(GetThemeColor(ThemeColorEnum::SurfaceFrame));
	}
	Place(automaticDivider_,g.automaticDivider);
	automaticDivider_.ft->SetDirect(ButtonFrameThicknessDip*frame_.panelPose.scale);
	automaticDivider_.rw->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);
	automaticDivider_.rh->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);
	const auto automaticFrame=static_cast<COLORREF>(buttons_[4].button.frame->val);
	automaticDivider_.fill->SetDirect(automaticFrame);automaticDivider_.frame->SetDirect(automaticFrame);
	automaticDivider_.frameLightPct->SetDirect(buttons_[4].button.frameLightPct->val);
	const auto arrowRect=g.items[5];const double arrowSize=EraserAutomaticArrowSizeDip*frame_.panelPose.scale;
	automaticArrow_.x.SetDirect(EraserRectCenterX(arrowRect)-arrowSize/2);
	automaticArrow_.y.SetDirect(EraserRectCenterY(arrowRect)-arrowSize/2);
	automaticArrow_.w.SetDirect(arrowSize);automaticArrow_.h.SetDirect(arrowSize);
	automaticArrow_.color1->SetTar(GetThemeColor(automatic_==AutomaticState::On?ThemeColorEnum::Accent:ThemeColorEnum::TextPrimary));
	const double collapsedAngle=g.menuBelow?180.0:0.0;
	automaticArrow_.angle.SetTar(menuOpen?180.0-collapsedAngle:collapsedAngle,UiDefaultOperationDur);
	automaticArrow_.pct.SetTar(frame_.panelVisible?1.0:0.0,ButtonHoverTransitionDuration);
	advance(*automaticArrow_.color1);advance(automaticArrow_.angle);advance(automaticArrow_.pct);
	const auto& texts=I18nKey.UI.Bar.EraserAttributes;
	changed_|=title_.SetStringImmediate(I18n::getWOr(texts.SensitivityLabel,L"灵敏度"));
	title_.w.SetDirect(g.menuTitle.Width());title_.h.SetDirect(g.menuTitle.Height());title_.size.SetDirect(ButtonTwoTwoLabelFontSizeDip*frame_.menuPose.scale);
	title_.fontTargetSize=ButtonTwoTwoLabelFontSizeDip*menuFrame.targetPose.scale;
	title_.color.SetDirect(GetThemeColor(ThemeColorEnum::TextPrimary));
	UpdatePanelFrame(open,menuOpen);
	return changed_ || active_;
}

void BarEraserAttributePanel::DrawPreview(UIRendering& renderer,ID2D1DeviceContext* context,size_t index)
{
	using namespace Inkeys::Drawing::Draw3;
	const auto& g=frame_.geometry;const auto rect=g.previews[index];
	context->PushAxisAlignedClip(PixelRect(g.previewClips[index],zoom_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	const auto center=D2D1::Point2F(static_cast<float>(EraserRectCenterX(rect)*zoom_),static_cast<float>(EraserRectCenterY(rect)*zoom_));
	D2D1_MATRIX_3X2_F originalTransform{};context->GetTransform(&originalTransform);
	const float press=static_cast<float>(circlePress_[index].val);
	if(std::abs(press-1.0f)>0.000001f)
		context->SetTransform(D2D1::Matrix3x2F::Scale(press,press,center)*originalTransform);
	auto& ring=circleSelectionRings_[index];renderer.Shape(context,ring,UiInheritClass(ring.inhX,ring.inhY));
	const auto half=ResolveEraserHalfSize(g.shape,previewDiametersPx_[index]/2);
	if(!previewBitmapAttempted_[index])
	{
		previewBitmapAttempted_[index]=true;
		// 位图使用实际画布尺寸；开合和按压仅变换 quad，不逐帧重烘焙。
		const float intendedShortSide=(std::min)(half.x,half.y)*2;
		const auto bakedSide=static_cast<uint32_t>((std::max)(1.0f,std::round(intendedShortSide)));
		const float bakeScale=bakedSide/intendedShortSide;
		const auto raster=RasterizeEraserCursor({g.shape,previewPattern_,previewTheme_},bakedSide,6*previewDpiScale_*bakeScale);
		previewRasterHalfWidths_[index]=raster.halfWidthPx;
		if(!raster.levels.empty())
		{
			const auto& level=raster.levels.front();
			const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
			context->CreateBitmap(D2D1::SizeU(level.width,level.height),level.bgra.data(),level.width*4,&properties,previewBitmaps_[index].GetAddressOf());
		}
	}
	if(previewBitmaps_[index] && half.x>0)
	{
		const auto size=previewBitmaps_[index]->GetPixelSize();
		const float scale=static_cast<float>(rect.Width()*zoom_)/(previewRasterHalfWidths_[index]*2);
		const float width=size.width*scale,height=size.height*scale;
		const auto destination=D2D1::RectF(center.x-width/2,center.y-height/2,center.x+width/2,center.y+height/2);
		context->DrawBitmap(previewBitmaps_[index].Get(),destination,1,D2D1_INTERPOLATION_MODE_LINEAR);
	}
	// 仅叠加原有 PointLight 轮廓，填充和静态描边由缓存位图负责。
	auto& hover=circles_[index];renderer.Shape(context,hover,UiInheritClass(hover.inhX,hover.inhY));
	if(std::abs(press-1.0f)>0.000001f)context->SetTransform(originalTransform);
	context->PopAxisAlignedClip();
}

void BarEraserAttributePanel::Draw(UIRendering& renderer,ID2D1DeviceContext* context)
{
	if(!initialized_ || !frame_.panelVisible)return;
	if(deviceGeneration_!=renderer.GetDeviceGeneration())
	{
		for(auto& bitmap:previewBitmaps_)bitmap.Reset();previewBitmapAttempted_.fill(false);
		automaticArrow_.ResetCache();for(int i:buttonVisuals)buttons_[i].icon.ResetCache();deviceGeneration_=renderer.GetDeviceGeneration();
	}
	const auto& g=frame_.geometry;
	auto drawButton=[&](int i){auto& b=buttons_[i];DrawButtonVisual(renderer,context,b,UiInheritClass(b.button.inhX,b.button.inhY));};
	renderer.PushPanelOpacity(context,PixelRect(frame_.geometry.panel,zoom_),panel_.Opacity());renderer.SetFrameDiffuseMaskGeometryScale(1/frame_.panelPose.scale);
	panel_.DrawFrame(renderer,context,{surface_.inhX,surface_.inhY},nullptr,false,PixelRect(g.panel,zoom_));
	if(!g.compact)drawButton(0);
	drawButton(4);drawButton(10);
	for(size_t i=0;i<3;++i)DrawPreview(renderer,context,i);
	for(auto& d:dividers_)renderer.Shape(context,d,UiInheritClass(d.inhX,d.inhY));
	// 内部分割线和箭头跟随整个90×70按钮围绕同一中心缩放。
	const auto whole=PixelRect(g.automatic,zoom_);
	const float press=static_cast<float>(buttons_[4].pressScale.val);
	const float ax=(whole.left+whole.right)/2,ay=(whole.top+whole.bottom)/2;
	D2D1_MATRIX_3X2_F originalTransform{};context->GetTransform(&originalTransform);
	if(std::abs(press-1.0f)>0.000001f)
		context->SetTransform(D2D1::Matrix3x2F::Scale(press,press,D2D1::Point2F(ax,ay))*originalTransform);
	renderer.Shape(context,automaticDivider_,UiInheritClass(automaticDivider_.inhX,automaticDivider_.inhY));
	renderer.Svg(context,automaticArrow_,UiInheritClass(automaticArrow_.x.val,automaticArrow_.y.val));
	if(std::abs(press-1.0f)>0.000001f)context->SetTransform(originalTransform);
	context->PopAxisAlignedClip();context->PopLayer();renderer.SetFrameDiffuseMaskGeometryScale(1);
	if(frame_.menuVisible)
	{
		renderer.PushPanelOpacity(context,PixelRect(frame_.geometry.menu,zoom_),menuPanel_.Opacity());renderer.SetFrameDiffuseMaskGeometryScale(1/frame_.menuPose.scale);
		menuPanel_.DrawFrame(renderer,context,{menu_.inhX,menu_.inhY},nullptr,false,PixelRect(g.menu,zoom_));
		renderer.Word(context,title_,UiInheritClass(g.menuTitle.left,g.menuTitle.top),DWRITE_FONT_WEIGHT_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING);
		for(int i:{6,7,8,9})drawButton(i);
		context->PopAxisAlignedClip();context->PopLayer();renderer.SetFrameDiffuseMaskGeometryScale(1);
	}
}

void BarEraserAttributePanel::PrepareVisuals()
{
	if(!initialized_ || !frame_.panelVisible)return;
	for(int i:{0,4,10,6,7,8,9}){auto& b=buttons_[i];(void)PrepareButtonVisualInheritance(b,UiInheritClass(b.button.inhX,b.button.inhY));}
	const auto& g=frame_.geometry;
	title_.UpInh(UiInheritClass(g.menuTitle.left,g.menuTitle.top));
	automaticArrow_.UpInh(UiInheritClass(automaticArrow_.x.val,automaticArrow_.y.val));
}

bool BarEraserAttributePanel::RegisterVisuals(VisualDirtyRegistry& registry,const BarPanelVisualContext& context)
{
	if(!initialized_)return false;
	const VisualSamplerContext sampler{context.zoom,context.map,{}};
	const auto writeRect=[](VisualStateWriter& out,const EraserAttributeRect& r){out.Real(r.left);out.Real(r.top);out.Real(r.right);out.Real(r.bottom);};
	// 主面板与灵敏度菜单各自整体合成（透明度层 + 裁剪），作为两个组；Draw 在主面板不可见时整体跳过。
	const auto panel=registry.AddGroup([this,writeRect,context](VisualGroupSample& out)
		{
			out.visible=initialized_&&frame_.panelVisible;if(!out.visible)return;
			if(context.mappingState)context.mappingState(out.state);
			out.state.Real(panel_.Opacity());writeRect(out.state,frame_.geometry.panel);out.state.Real(frame_.panelPose.scale);
		});
	const auto menu=registry.AddGroup([this,writeRect,context](VisualGroupSample& out)
		{
			out.visible=initialized_&&frame_.panelVisible&&frame_.menuVisible;if(!out.visible)return;
			if(context.mappingState)context.mappingState(out.state);
			out.state.Real(menuPanel_.Opacity());writeRect(out.state,frame_.geometry.menu);out.state.Real(frame_.menuPose.scale);
		});
	(void)registry.Add(panel,MakeVisualSampler(surface_,sampler),"eraser.surface");
	for(int i:{0,4,10})(void)registry.Add(panel,MakeButtonVisualSampler(buttons_[i],sampler),"eraser.button"+std::to_string(i));
	for(auto& d:dividers_)(void)registry.Add(panel,MakeVisualSampler(d,sampler),"eraser.divider");
	// 尺寸预览：像素全部裁剪在 previewClips 内，包络直接取裁剪矩形；位图在绘制时懒创建，身份写入易变段。
	for(size_t i=0;i<3;++i)
		(void)registry.Add(panel,[this,i,context,writeRect](VisualSample& out)
			{
				const auto& g=frame_.geometry;const double zoom=context.zoom?context.zoom():1.0;
				const RECT clip=IntegerRect(g.previewClips[i],zoom,1);
				out.visible=clip.right>clip.left&&clip.bottom>clip.top;if(!out.visible)return;
				out.bounds=context.map?context.map(clip):clip;
				auto& state=out.state;state.Real(zoom);writeRect(state,g.previewClips[i]);writeRect(state,g.previews[i]);
				state.Real(circlePress_[i].val);state.Int(static_cast<int>(g.shape));
				state.Int(static_cast<int>(previewPattern_));state.Int(static_cast<int>(previewTheme_));
				state.Real(previewDiametersPx_[i]);state.Real(previewDpiScale_);
				WriteVisualState(state,circleSelectionRings_[i]);WriteVisualState(state,circles_[i]);
				state.BeginVolatile();
				state.UInt(reinterpret_cast<std::uintptr_t>(previewBitmaps_[i].Get()));state.Real(previewRasterHalfWidths_[i]);
				state.EndVolatile();
			},"eraser.preview"+std::to_string(i));
	// 自动粗细按钮内部的分割线与箭头：随按钮 4 的按压缩放绕整个按钮中心缩放（回弹峰值约 1.005，被外扩覆盖）。
	(void)registry.Add(panel,[this,context](VisualSample& out)
		{
			const double zoom=context.zoom?context.zoom():1.0;
			RECT bounds{};bool visible=false;
			if(IsVisuallyDrawn(automaticDivider_)){visible=true;RenderingAttribute::UnionRectInPlace(bounds,RenderingAttribute::GetWeigetRect(automaticDivider_,zoom));}
			if(IsVisuallyDrawn(automaticArrow_)){visible=true;RenderingAttribute::UnionRectInPlace(bounds,RenderingAttribute::GetWeigetRect(automaticArrow_,zoom));}
			out.visible=visible;if(!visible)return;
			Inkeys::UI::UI3::Detail::GrowForPress(bounds,buttons_[4].pressScale.val);
			out.bounds=context.map?context.map(bounds):bounds;
			out.state.Real(zoom);out.state.Real(buttons_[4].pressScale.val);
			WriteVisualState(out.state,automaticDivider_);WriteVisualState(out.state,automaticArrow_);
		},"eraser.automatic-decoration");
	(void)registry.Add(menu,MakeVisualSampler(menu_,sampler),"eraser.menu");
	(void)registry.Add(menu,MakeVisualSampler(title_,sampler),"eraser.menu-title");
	for(int i:{6,7,8,9})(void)registry.Add(menu,MakeButtonVisualSampler(buttons_[i],sampler),"eraser.button"+std::to_string(i));
	return true;
}

void BarEraserAttributePanel::UpdatePanelFrame(bool open,bool menuOpen)
{
	panel_.SetExternalState(open,panelMotion_.Geometry(),frame_.panelOpacity);
	menuPanel_.SetExternalState(menuOpen,menuMotion_.Geometry(),frame_.menuOpacity);
	auto bounds=[&](EraserAttributeRect rect,double scale)
	{
		const int padding=static_cast<int>(ceil((ButtonFrameThicknessDip*scale+RenderingAttribute::pointLightDiffuseExtraWidth)*zoom_))+RenderingAttribute::dirtyAntialiasPadding;
		return IntegerRect(rect,zoom_,padding);
	};
	const auto set=[&](Panel& panel,EraserAttributeRect r,double scale,bool visible){
		const double radius=BarMainBarCornerRadiusDip*scale;
		panel.SetCandidate(bounds(r,scale),{{r.left,r.top,r.right,r.bottom},radius,radius,PanelHitGeometry::RoundedWithPixelTolerance,zoom_},visible);
	};
	set(panel_,frame_.geometry.panel,frame_.panelPose.scale,frame_.panelVisible);
	set(menuPanel_,frame_.geometry.menu,frame_.menuPose.scale,frame_.menuVisible);
}
RECT BarEraserAttributePanel::Bounds() const
{
	RECT result=panel_.CandidateBounds();if(!frame_.panelVisible)return result;
	if(frame_.menuVisible)RenderingAttribute::UnionRectInPlace(result,menuPanel_.CandidateBounds());
	return result;
}
void BarEraserAttributePanel::CommitPresented()
{
	std::scoped_lock lock(presentationMutex_);presented_=frame_;panel_.CommitPresented();menuPanel_.CommitPresented();
}
EraserAttributePresentation BarEraserAttributePanel::PresentationSnapshot() const
{
	std::scoped_lock lock(presentationMutex_);return presented_;
}
std::array<RECT,3> BarEraserAttributePanel::PresentedRegions() const
{
	std::scoped_lock lock(presentationMutex_);
	return {panel_.PresentedInteractionBounds(),menuPanel_.PresentedInteractionBounds(),RECT{}};
}
void BarEraserAttributePanel::Close(BarUISetClass& owner)
{
	owner.barState.eraserAttribute=false;owner.barState.eraserSensitivityOpen=false;pressed_=-1;hovered_=-1;
}
bool BarEraserAttributePanel::Execute(BarUISetClass& owner,int item)
{
	bool performed=item>=1 && item<=10;
	if(item==0)
	{
		const auto snapshot=Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
		const auto returnMode=ResolveEraserClearReturnMode(snapshot.completedStrokeKind,returnToSelectionOnClear_);
		const auto accepted=Inkeys::Drawing::Draw3::PublishProductCommand(Inkeys::Drawing::Draw3::Bridge::CommandType::Clear);
		if(accepted==Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted)
		{
			// 从有内容的选择进入橡皮时，清空后优先恢复选择，不受期间橡皮笔迹影响。
			if(returnMode==BarEraserClearReturnMode::Selection)ChangeStateModeToSelection();
			else if(returnMode==BarEraserClearReturnMode::Drawing)ChangeStateModeToPen();
			else if(returnMode==BarEraserClearReturnMode::Shape)ChangeStateModeToShape();
			// 最近一次是橡皮时保持当前橡皮模式；只在命令被接受后收起面板。
			Close(owner);
			owner.UpdateRendering();return true;
		}
	}
	else if(IsSize(item))SetGlobalEraserPreference(static_cast<int>(BaseSizePresets[item-1]));
	else if(item==4)SetGlobalEraserPreference(-1,-1,GetAutomaticState(EraserPreferencesSnapshot())!=AutomaticState::On);
	else if(item==5){performed=owner.TryBeginToggle(BarToggleChannel::EraserSensitivity);if(performed)owner.barState.eraserSensitivityOpen=!owner.barState.eraserSensitivityOpen;}
	else if(item>=6 && item<=8)SetGlobalEraserPreference(-1,item-6);
	else if(item==9)Inkeys::UI::Setting::ShowSpeedEraser();
	else if(item==10)
	{
		// 与设置页的手掌橡皮总开关是同一份偏好；其余识别参数保持不变。
		auto palm=PalmEraserPreferencesSnapshot();palm.enabled=!palm.enabled;
		SetPalmEraserPreference(palm);
	}
	owner.UpdateRendering(false);
	return performed;
}
bool BarEraserAttributePanel::ResetPointerFeedback()
{
	const bool changed=hovered_>=0 || pressed_>=0;
	hovered_=-1;pressed_=-1;
	return changed;
}
bool BarEraserAttributePanel::Pointer(BarUISetClass& owner,const ExMessage& message,bool cancelled,bool contactPointer,bool* accepted)
{
	if(accepted)*accepted=false;
	EraserAttributePresentation f;PanelFrame panelFrame,menuFrame;
	{std::scoped_lock lock(presentationMutex_);f=presented_;panelFrame=panel_.Presented();menuFrame=menuPanel_.Presented();}
	if(cancelled){const bool owned=ResetPointerFeedback();if(owned)owner.UpdateRendering(false);return owned;}
	if(!f.panelVisible && pressed_<0)return false;
	const auto& geometry=f.geometry;const double x=message.x/f.zoom,y=message.y/f.zoom;
	const auto front=owner.panelStack.FrontPanelIf([&](const PanelStackEntry& entry){
		if(entry.id==static_cast<PanelId>(BarPanelId::EraserMenu))return menuFrame.OwnsPoint(message.x,message.y);
		return entry.id==static_cast<PanelId>(BarPanelId::EraserAttribute)&&panelFrame.OwnsPoint(message.x,message.y);
	});
	const bool overMenu=front==static_cast<PanelId>(BarPanelId::EraserMenu);
	const bool inside=front.has_value();
	int hit=-1;
	if(inside)
	{
		const auto clip=overMenu?geometry.menu:geometry.panel;
		for(int step=0;step<(overMenu?4:7);++step)
		{
			const int i=overMenu?6+step:(step<6?step:10);
			if(!IntersectEraserRect(geometry.items[i],clip).Contains(x,y))continue;
			if(IsSize(i))
			{
				const auto circle=geometry.previews[i-1];
				if(!EraserAttributePreviewContains(circle,geometry.shape,geometry.previewCornerRadius,ButtonGapDip*f.panelPose.scale,x,y))continue;
			}
			hit=i;break;
		}
	}
	if(message.message==WM_MOUSEMOVE)
	{
		if(hovered_!=hit)
		{
			// 输入只发布目标，悬停/按压动画统一由 Advance 在渲染线程消费。
			hovered_=hit;
			owner.UpdateRendering(false);
		}
		if(pressed_>=0 && cancelled){pressed_=-1;owner.UpdateRendering(false);}return inside;
	}
	const bool down=message.message==WM_LBUTTONDOWN || message.message==WM_LBUTTONDBLCLK;
	if(down)
	{
		if(owner.barState.eraserSensitivityOpen && !overMenu && hit!=5){owner.barState.eraserSensitivityOpen=false;owner.UpdateRendering(false);}
		// 必须是面板收到的新Down才能建立动作票据；主栏开栏的那次Up不能落到中央Clear。
		pressed_=inside && owner.barState.eraserAttribute && !cancelled?hit:-1;
		owner.UpdateRendering(false);return inside;
	}
	if(message.message==WM_LBUTTONUP)
	{
		const int pressed=pressed_;
		pressed_=-1;
		// 同一整体按钮内跨区释放，仍执行Down时的动作，不改成另一命令。
		const int action=ResolveEraserAttributeRelease(pressed,hit,inside,cancelled);
		if(action>=0 && owner.barState.eraserAttribute){const bool performed=Execute(owner,action);if(accepted)*accepted=performed;}
		if(contactPointer)ResetPointerFeedback();
		if(pressed>=0 || contactPointer)owner.UpdateRendering(false);return inside || pressed>=0;
	}
	return inside;
}
// 小尺寸悬停提示：自动粗细（含其展开箭头）与手掌橡皮显示名称；尺寸圆与菜单项（自带文字）不提示。
std::wstring BarEraserAttributePanel::HoverHintText() const
{
	// 面板开合动画期间不提示。
	if(!panelMotion_.Visible() || panelMotion_.Active())return {};
	const int item=hovered_.load();
	return item==4||item==5?ButtonLabel(4):item==10?ButtonLabel(10):std::wstring{};
}
