module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../Drawing/Draw3/Assets/EraserGripVisual.h"
#include <algorithm>
#include <array>
#include <cmath>

module Inkeys.UI.Bar;
import :Main;
import :Theme;

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
		double duration,const BarUiTimelineClass* parent,double dt,double speed) noexcept
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
	void Place(BarUiShapeClass& shape,EraserAttributeRect r)
	{
		shape.x.SetDirect(r.left);shape.y.SetDirect(r.top);shape.w.SetDirect(r.Width());shape.h.SetDirect(r.Height());
		shape.UpInh(BarUiInheritClass(r.left,r.top));
	}
	D2D1_COLOR_F ThemeBrushColor(BarThemeColorEnum role,float opacity=1)
	{
		const auto c=GetThemeColor(role);
		return D2D1::ColorF(GetRValue(c)/255.0f,GetGValue(c)/255.0f,GetBValue(c)/255.0f,opacity);
	}
	COLORREF EraserOutlineColor() noexcept
	{ const auto c=static_cast<BYTE>(std::lround(ERASER_GRIP_OUTLINE_CHANNEL*255));return RGB(c,c,c); }
	void PushOpacity(ID2D1DeviceContext* context,double opacity)
	{
		context->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),nullptr,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
			D2D1::IdentityMatrix(),static_cast<float>((std::clamp)(opacity,0.0,1.0))),nullptr);
	}
	bool IsSize(int item) noexcept { return item>=1 && item<=3; }
	bool IsAutomatic(int item) noexcept { return item==4 || item==5; }
	int ButtonVisual(int item) noexcept { return item==5?4:IsSize(item)?-1:item; }
	constexpr std::array<int,6> buttonVisuals{0,4,6,7,8,9};
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
		default:return {};
		}
	}
}

void BarEraserAttributePanel::Initialize()
{
	if(initialized_)return;
	for(auto* shape:{&surface_,&menu_})
	{
		shape->Initialization(0,0,0,0,BarMainBarCornerRadiusDip,BarMainBarCornerRadiusDip,BarButtonFrameThicknessDip,
			GetThemeColor(BarThemeColorEnum::Surface),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		shape->enable.Initialization(true);shape->pct.SetDirect(BarDrawAttributeSurfaceOpacity);
		shape->framePct.emplace(BarMainBarFrameOpacity);shape->frameLightPct.emplace(BarMainBarFrameOpacity);
		shape->frameRendering=BarUiFrameRenderingEnum::PointLight;shape->frameLightColor=BarUiFrameLightColorEnum::PenWhenDrawing;
	}
	const auto metrics=ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
	for(int i:buttonVisuals)
	{
		auto& b=buttons_[i];b.hide=false;b.size=BarButtonSizeEnum::twoTwo;
		b.button.Initialization(0,0,0,0,BarButtonCornerRadiusDip,BarButtonCornerRadiusDip,BarButtonFrameThicknessDip,
			GetThemeColor(BarThemeColorEnum::PressedFill),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		b.button.enable.Initialization(true);b.button.pct.SetDirect(0);
		b.button.framePct.emplace(0);b.button.frameLightPct.emplace(0);
		b.button.frameRendering=BarUiFrameRenderingEnum::PointLight;b.button.frameLightColor=BarUiFrameLightColorEnum::Frame;
		b.button.framePrimaryLightEnabled=false;b.button.frameCursorLightIntensityScale=BarButtonCursorLightIntensity;
		const bool large=i==0 || i==4;const auto label=ButtonLabel(i);
		b.name.Initialization(0,0,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip,label,metrics.primaryFontSizeDip,
			GetThemeColor(BarThemeColorEnum::TextPrimary));
		b.name.enable.Initialization(!label.empty());b.name.pct.SetDirect(1);
		b.icon.Initialization(0,0,GetThemeColor(BarThemeColorEnum::TextPrimary),std::nullopt);
		b.icon.enable.Initialization(large || i==9);b.icon.pct.SetDirect(1);
		if(large || i==9)b.icon.InitializationFromResource(L"UI",i==0?L"barClean":i==4?L"barAutoEraser":L"barSetting");
		b.icon.SetWH(large?metrics.iconSizeDip:20,large?metrics.iconSizeDip:20);
		b.icon.w.SetDirect(b.icon.w.tar);b.icon.h.SetDirect(b.icon.h.tar);
	}
	for(size_t i=0;i<circles_.size();++i)
	{
		auto& circle=circles_[i];
		circle.Initialization(0,0,1,1,0.5,0.5,1,RGB(255,255,255),EraserOutlineColor());
		circle.enable.Initialization(true);circle.pct.SetDirect(1);circle.framePct.emplace(1);circle.frameLightPct.emplace(0);
		circle.frameRendering=BarUiFrameRenderingEnum::PointLight;circle.frameLightColor=BarUiFrameLightColorEnum::Frame;
		circle.frameCursorLightIntensityScale=BarButtonCursorLightIntensity;
		auto& ring=circleSelectionRings_[i];
		ring.Initialization(0,0,1,1,0.5,0.5,EraserSizeSelectionThicknessDip,std::nullopt,
			GetThemeColor(BarThemeColorEnum::Accent));
		ring.enable.Initialization(true);ring.pct.SetDirect(0);ring.framePct.emplace(0);ring.frameLightPct.emplace(0);
		ring.frameRendering=BarUiFrameRenderingEnum::PointLight;ring.frameLightColor=BarUiFrameLightColorEnum::Frame;
		ring.framePrimaryLightEnabled=false;ring.frameCursorLightIntensityScale=BarButtonCursorLightIntensity;
		circleSelection_[i].Initialization(0);circleHover_[i].Initialization(0);circlePress_[i].Initialization(1);
	}
	for(auto& d:dividers_)
	{
		d.Initialization(0,0,1,1,BarUiDividerRadius,BarUiDividerRadius,BarButtonFrameThicknessDip,
			GetThemeColor(BarThemeColorEnum::SurfaceFrame),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		d.enable.Initialization(true);d.pct.SetDirect(0.30);d.framePct.emplace(0);d.frameLightPct.emplace(1);
		d.frameRendering=BarUiFrameRenderingEnum::PointLight;d.framePrimaryLightEnabled=false;d.frameCursorLightIntensityScale=BarUiDividerCursorLightIntensity;
	}
	automaticDivider_.Initialization(0,0,1,1,BarUiDividerRadius,BarUiDividerRadius,BarButtonFrameThicknessDip,
		GetThemeColor(BarThemeColorEnum::SurfaceFrame),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	automaticDivider_.enable.Initialization(true);automaticDivider_.pct.SetDirect(0.30);
	automaticDivider_.framePct.emplace(0);automaticDivider_.frameLightPct.emplace(0);
	automaticDivider_.frameRendering=BarUiFrameRenderingEnum::PointLight;
	automaticDivider_.frameLightColor=BarUiFrameLightColorEnum::Frame;
	automaticDivider_.framePrimaryLightEnabled=false;
	automaticDivider_.frameCursorLightIntensityScale=BarUiDividerCursorLightIntensity;
	automaticArrow_.Initialization(0,0,GetThemeColor(BarThemeColorEnum::TextPrimary),std::nullopt);
	automaticArrow_.InitializationFromResource(L"UI",L"barThicknessAdjust");
	automaticArrow_.SetWH(EraserAutomaticArrowSizeDip,EraserAutomaticArrowSizeDip);
	automaticArrow_.enable.Initialization(true);automaticArrow_.pct.SetDirect(1);
	title_.Initialization(0,0,0,BarButtonOneSideDip,L"灵敏度",BarButtonTwoTwoLabelFontSizeDip,GetThemeColor(BarThemeColorEnum::TextPrimary));
	title_.enable.Initialization(true);title_.pct.SetDirect(1);
	initialized_=true;
}

void BarEraserAttributePanel::ConfigureSurface(BarUiShapeClass& surface,EraserAttributeRect rect,double scale)
{
	Place(surface,rect);surface.rw->SetDirect(BarMainBarCornerRadiusDip*scale);surface.rh->SetDirect(BarMainBarCornerRadiusDip*scale);
	surface.ft->SetDirect(BarButtonFrameThicknessDip*scale);
	surface.fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));surface.frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
}

bool BarEraserAttributePanel::Advance(BarUISetClass& owner,double dt,double speed,double zoom,UINT dpi,
	RECT workArea,POINT origin,double rigidX,double rigidY,const BarUiTimelineClass* parentTimeline,
	bool dragPlacementLocked)
{
	Initialize();changed_=false;active_=false;
	const bool shouldClose=owner.barState.fold || stateMode.StateModeSelect!=StateModeSelectEnum::IdtEraser ||
		owner.barState.drawAttribute || owner.barState.geometryAttribute || owner.barState.moreExpanded;
	if(shouldClose)Close(owner);
	const bool open=owner.barState.eraserAttribute;if(!open)owner.barState.eraserSensitivityOpen=false;
	const bool menuOpen=owner.barState.eraserSensitivityOpen;visible_=open;
	const BarUiAnimationAdvanceContextClass context{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
	auto advance=[&](auto& value){const auto result=BarUiAdvanceAnimation(value,context);changed_|=result.changed;active_|=result.active;};
	auto root=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	root->UpInh(BarUiInheritClass(root->x.val-root->w.val/2,root->y.val-root->h.val/2));
	auto main=owner.shapeMap[BarUISetShapeEnum::MainBar];main->Inherit(BarUiInheritEnum::Center,*root);
	auto anchor=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];anchor->button.Inherit(BarUiInheritEnum::CenterFromTopLeft,*main);
	EraserAttributeLayoutInput currentInput;
	currentInput.main={main->inhX,main->inhY,main->inhX+main->w.val,main->inhY+main->h.val};
	currentInput.anchor={anchor->button.inhX,anchor->button.inhY,anchor->button.inhX+anchor->button.w.val,anchor->button.inhY+anchor->button.h.val};
	currentInput.zoom=zoom;currentInput.dpiScale=dpi/96.0;
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
	double panelDuration=static_cast<double>(BarUiDefaultOperationDur);
	if(beginSideSwitch)
	{
		const auto durations=ResolveEraserSideSwitchDurations(BarUiDefaultOperationDur,parentTimeline,dt,speed);
		panelDuration=durations.collapse;sideSwitchExpandDuration_=durations.expand;
		sideSwitchActive_=true;
	}
	else if(!switching)sideSwitchActive_=false;
	const BarUiTimelineClass* panelParent=switching?nullptr:parentTimeline;
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
	if(!holdDragPlacement && !holdReleaseSwitchPlacement)
	{
		input.below=previousBelow_;input.reversed=previousReversed_;input.lockedMenuSide=menuSide_;
		// 收拢到紧凑态之前继续显示原侧，避免 release 首帧被工作区避让直接闪到另一边。
		input.lockPanelSide=switching;
		stableLayoutInput_=input;hasStableLayoutInput_=true;
	}
	layout_=ResolveEraserAttributeLayout(input);if(menuOpen && menuSide_<0)menuSide_=layout_.menuBelow?1:0;
	EraserAttributePresentation next;next.zoom=zoom;
	next.panelPose=FitEraserSurfacePose(ResolveEraserSurfacePose(layout_.panel,input.anchor,panelMotion_.Geometry(),BarDrawAttributeCompactWidth),layout_.panel,input.work);
	const auto child=ResolveEraserSurfacePose(layout_.menu,layout_.automatic,menuMotion_.Geometry(),BarDrawAttributeCompactWidth);
	next.menuPose=FitEraserSurfacePose(ComposeEraserPose(next.panelPose,child),layout_.menu,input.work);
	// pose统一正向解析到当前Bar坐标，再交给原Renderer；不额外乘D2D父缩放或重置光源动画。
	next.geometry=TransformEraserAttributeLayout(layout_,next.panelPose,next.menuPose);
	next.panelOpacity=panelMotion_.Opacity();next.menuOpacity=next.panelOpacity*menuMotion_.Opacity();
	next.panelVisible=panelMotion_.Visible() && next.panelOpacity>0.000001;
	next.menuVisible=next.panelVisible && menuMotion_.Visible() && next.menuOpacity>0.000001;
	changed_|=frame_.geometry!=next.geometry || frame_.zoom!=next.zoom || frame_.panelOpacity!=next.panelOpacity ||
		frame_.menuOpacity!=next.menuOpacity || frame_.panelVisible!=next.panelVisible || frame_.menuVisible!=next.menuVisible ||
		frame_.panelPose.scale!=next.panelPose.scale || frame_.menuPose.scale!=next.menuPose.scale;
	frame_=next;zoom_=zoom;
	const auto& g=frame_.geometry;
	const auto preferences=EraserPreferencesSnapshot();const auto automatic=GetAutomaticState(preferences);
	changed_|=automatic!=automatic_ || selectedSize_!=static_cast<int>(preferences.baseSize) || sensitivity_!=static_cast<int>(preferences.sensitivity);
	automatic_=automatic;selectedSize_=static_cast<int>(preferences.baseSize);sensitivity_=static_cast<int>(preferences.sensitivity);
	changed_|=surface_.fill->val!=GetThemeColor(BarThemeColorEnum::Surface);
	ConfigureSurface(surface_,g.panel,frame_.panelPose.scale);ConfigureSurface(menu_,g.menu,frame_.menuPose.scale);
	const auto metrics=ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
	for(int i:buttonVisuals)
	{
		auto& b=buttons_[i];changed_|=b.name.SetStringImmediate(ButtonLabel(i));
		const bool enabled=i!=9;
		const bool selected=(i==4 && automatic_==AutomaticState::On) || (i>=6 && i<=8 && sensitivity_==i-6);
		const bool presented=i<6?frame_.panelVisible:frame_.menuVisible;
		const double scale=i<6?frame_.panelPose.scale:frame_.menuPose.scale;
		b.state->state=!enabled?BarWidgetState::Disable:selected?BarWidgetState::Selected:BarWidgetState::None;
		SetBarButtonPressedVisual(b,enabled && ButtonVisual(pressed_)==i);
		UpdateBarButtonHoverVisual(b,presented,enabled && !selected,BarButtonHoverFadeDurationSeconds);
		RetargetBarButtonInteractionVisual(b,presented,enabled,selected,BarButtonHoverTransitionDuration);
		b.button.frame->SetTar(GetThemeColor(selected?BarThemeColorEnum::Accent:BarThemeColorEnum::SurfaceFrame));
		Place(b.button,i==4?g.automatic:g.items[i]);
		b.button.rw->SetDirect(BarButtonCornerRadiusDip*scale);b.button.rh->SetDirect(BarButtonCornerRadiusDip*scale);b.button.ft->SetDirect(BarButtonFrameThicknessDip*scale);
		const bool large=i==0 || i==4;
		const double offset=i==4?-EraserAttributeAutomaticArrowWidthDip/2:0;
		b.icon.x.SetDirect(offset*scale);b.icon.y.SetDirect(large?metrics.iconOffsetYDip*scale:0);b.icon.contentScale=scale;
		b.name.x.SetDirect(offset*scale);b.name.y.SetDirect(large?metrics.primaryOffsetYDip*scale:0);
		b.name.w.SetDirect(large?metrics.primarySlotWidthDip*scale:static_cast<double>(b.button.w.val));b.name.h.SetDirect(metrics.primarySlotHeightDip*scale);b.name.size.SetDirect(metrics.primaryFontSizeDip*scale);
		advance(b.button.pct);advance(*b.button.fill);advance(*b.button.frame);advance(*b.button.frameLightPct);
		advance(b.icon.pct);advance(b.name.pct);advance(*b.icon.color1);advance(b.name.color);advance(b.pressScale);
	}
	for(size_t i=0;i<3;++i)
	{
		const bool selected=selectedSize_==static_cast<int>(BaseSizePresets[i]);
		circleSelection_[i].SetTar(selected?1:0,BarButtonHoverTransitionDuration);
		circleHover_[i].SetTar(hovered_==static_cast<int>(i+1)?1:0,BarButtonHoverTransitionDuration);
		const bool pressed=pressed_==static_cast<int>(i+1);
		circlePress_[i].SetTar(pressed?BarButtonPressScale:1.0,BarUiDefaultOperationDur,
			std::nullopt,false,pressed?BarButtonPressCurve():BarButtonReleaseCurve());
		advance(circleSelection_[i]);advance(circleHover_[i]);advance(circlePress_[i]);
		auto& circle=circles_[i];const auto rect=g.previews[i];
		const double thickness=rect.Width()*ERASER_GRIP_OUTLINE_RATIO;
		// 橡皮本体保持原直径；选中态由外侧独立圆环承载。
		Place(circle,{rect.left+thickness/2,rect.top+thickness/2,rect.right-thickness/2,rect.bottom-thickness/2});
		circle.rw->SetDirect(circle.w.val/2);circle.rh->SetDirect(circle.h.val/2);circle.ft->SetDirect(thickness);
		circle.frame->SetDirect(EraserOutlineColor());circle.frameLightPct->SetDirect(circleHover_[i].val*0.5);
		auto& ring=circleSelectionRings_[i];const double ringInset=EraserSizeSelectionGapDip*frame_.panelPose.scale+
			EraserSizeSelectionThicknessDip*frame_.panelPose.scale/2;
		Place(ring,{rect.left-ringInset,rect.top-ringInset,rect.right+ringInset,rect.bottom+ringInset});
		ring.rw->SetDirect(ring.w.val/2);ring.rh->SetDirect(ring.h.val/2);
		ring.ft->SetDirect(EraserSizeSelectionThicknessDip*frame_.panelPose.scale);
		ring.frame->SetDirect(GetThemeColor(BarThemeColorEnum::Accent));
		ring.pct.SetDirect(circleSelection_[i].val);ring.framePct->SetDirect(circleSelection_[i].val);
		ring.frameLightPct->SetDirect(circleSelection_[i].val);
	}
	for(size_t i=0;i<dividers_.size();++i)
	{
		auto& d=dividers_[i];Place(d,g.dividers[i]);d.ft->SetDirect(BarButtonFrameThicknessDip*frame_.panelPose.scale);
		d.rw->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);d.rh->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);
		d.fill->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));d.frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	}
	Place(automaticDivider_,g.automaticDivider);
	automaticDivider_.ft->SetDirect(BarButtonFrameThicknessDip*frame_.panelPose.scale);
	automaticDivider_.rw->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);
	automaticDivider_.rh->SetDirect(BarUiDividerRadius*frame_.panelPose.scale);
	const auto automaticFrame=static_cast<COLORREF>(buttons_[4].button.frame->val);
	automaticDivider_.fill->SetDirect(automaticFrame);automaticDivider_.frame->SetDirect(automaticFrame);
	automaticDivider_.frameLightPct->SetDirect(buttons_[4].button.frameLightPct->val);
	const auto arrowRect=g.items[5];const double arrowSize=EraserAutomaticArrowSizeDip*frame_.panelPose.scale;
	automaticArrow_.x.SetDirect(EraserRectCenterX(arrowRect)-arrowSize/2);
	automaticArrow_.y.SetDirect(EraserRectCenterY(arrowRect)-arrowSize/2);
	automaticArrow_.w.SetDirect(arrowSize);automaticArrow_.h.SetDirect(arrowSize);
	automaticArrow_.color1->SetTar(GetThemeColor(automatic_==AutomaticState::On?BarThemeColorEnum::Accent:BarThemeColorEnum::TextPrimary));
	const double collapsedAngle=g.menuBelow?180.0:0.0;
	automaticArrow_.angle.SetTar(menuOpen?180.0-collapsedAngle:collapsedAngle,BarUiDefaultOperationDur);
	automaticArrow_.pct.SetTar(frame_.panelVisible?1.0:0.0,BarButtonHoverTransitionDuration);
	advance(*automaticArrow_.color1);advance(automaticArrow_.angle);advance(automaticArrow_.pct);
	const auto& texts=I18nKey.UI.Bar.EraserAttributes;
	changed_|=title_.SetStringImmediate(I18n::getWOr(texts.SensitivityLabel,L"灵敏度"));
	title_.w.SetDirect(g.menuTitle.Width());title_.h.SetDirect(g.menuTitle.Height());title_.size.SetDirect(BarButtonTwoTwoLabelFontSizeDip*frame_.menuPose.scale);
	title_.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
	return changed_ || active_;
}

void BarEraserAttributePanel::DrawPreview(BarUIRendering& renderer,ID2D1DeviceContext* context,size_t index)
{
	const auto& g=frame_.geometry;const auto rect=g.previews[index];
	context->PushAxisAlignedClip(PixelRect(g.previewClips[index],zoom_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	const float d=static_cast<float>(g.previewDiameters[index]*zoom_);
	const auto center=D2D1::Point2F(static_cast<float>(EraserRectCenterX(rect)*zoom_),static_cast<float>(EraserRectCenterY(rect)*zoom_));
	D2D1_MATRIX_3X2_F originalTransform{};context->GetTransform(&originalTransform);
	const float press=static_cast<float>(circlePress_[index].val);
	if(std::abs(press-1.0f)>0.000001f)
		context->SetTransform(D2D1::Matrix3x2F::Scale(press,press,center)*originalTransform);
	auto& ring=circleSelectionRings_[index];renderer.Shape(context,ring,BarUiInheritClass(ring.inhX,ring.inhY));
	auto& circle=circles_[index];renderer.Shape(context,circle,BarUiInheritClass(circle.inhX,circle.inhY));
	// Contact实体alpha=1；不修改共享Hover的ERASER_GRIP_OPACITY。
	brush_->SetColor(D2D1::ColorF(ERASER_GRIP_OUTLINE_CHANNEL,ERASER_GRIP_OUTLINE_CHANNEL,ERASER_GRIP_OUTLINE_CHANNEL,1));
	for(float sign:{-1.0f,1.0f})
	{
		const float x=center.x+sign*d*ERASER_GRIP_STRIPE_OFFSET_RATIO;
		const float r=d*ERASER_GRIP_STRIPE_RADIUS_RATIO,h=d*ERASER_GRIP_STRIPE_HALF_HEIGHT_RATIO;
		context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x-r,center.y-h,x+r,center.y+h),r,r),brush_.Get());
	}
	if(focused_==static_cast<int>(index+1))
	{
		// 白色圆上的焦点使用深色内虚线，与青色选中轮廓独立。
		brush_->SetColor(D2D1::ColorF(0.15f,0.15f,0.15f,1));
		const float radius=(std::max)(0.5f,d/2-static_cast<float>(circle.ft->val*zoom_)-2.0f);
		context->DrawEllipse(D2D1::Ellipse(center,radius,radius),brush_.Get(),1,focusStroke_.Get());
	}
	if(std::abs(press-1.0f)>0.000001f)context->SetTransform(originalTransform);
	context->PopAxisAlignedClip();
}

void BarEraserAttributePanel::Draw(BarUIRendering& renderer,ID2D1DeviceContext* context)
{
	if(!initialized_ || !frame_.panelVisible)return;
	if(deviceGeneration_!=renderer.GetDeviceGeneration())
	{
		brush_.Reset();focusStroke_.Reset();automaticArrow_.ResetCache();for(int i:buttonVisuals)buttons_[i].icon.ResetCache();deviceGeneration_=renderer.GetDeviceGeneration();
	}
	if(!brush_ && FAILED(context->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&brush_)))return;
	if(!focusStroke_){ComPtr<ID2D1Factory> factory;context->GetFactory(&factory);auto props=D2D1::StrokeStyleProperties();props.dashStyle=D2D1_DASH_STYLE_DOT;factory->CreateStrokeStyle(props,nullptr,0,&focusStroke_);}
	const auto& g=frame_.geometry;
	auto drawSurface=[&](BarUiShapeClass& s){DrawBarBackgroundVisual(renderer,context,s,BarUiInheritClass(s.inhX,s.inhY));};
	auto drawButton=[&](int i){auto& b=buttons_[i];DrawBarButtonVisual(renderer,context,b,BarUiInheritClass(b.button.inhX,b.button.inhY));};
	auto focusButton=[&](int i)
	{
		const auto& b=buttons_[i];brush_->SetColor(ThemeBrushColor(BarThemeColorEnum::TextPrimary));
		auto rect=PixelRect(i==4?g.automatic:g.items[i],zoom_);rect.left+=2;rect.right-=2;rect.top+=2;rect.bottom-=2;
		context->DrawRoundedRectangle(D2D1::RoundedRect(rect,4,4),brush_.Get(),1,focusStroke_.Get());
	};
	PushOpacity(context,frame_.panelOpacity);renderer.SetFrameDiffuseMaskGeometryScale(1/frame_.panelPose.scale);drawSurface(surface_);
	context->PushAxisAlignedClip(PixelRect(g.panel,zoom_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	drawButton(0);drawButton(4);
	for(size_t i=0;i<3;++i)DrawPreview(renderer,context,i);
	for(auto& d:dividers_)renderer.Shape(context,d,BarUiInheritClass(d.inhX,d.inhY));
	// 内部分割线和箭头跟随整个90×70按钮围绕同一中心缩放。
	const auto whole=PixelRect(g.automatic,zoom_);
	const float press=static_cast<float>(buttons_[4].pressScale.val);
	const float ax=(whole.left+whole.right)/2,ay=(whole.top+whole.bottom)/2;
	D2D1_MATRIX_3X2_F originalTransform{};context->GetTransform(&originalTransform);
	if(std::abs(press-1.0f)>0.000001f)
		context->SetTransform(D2D1::Matrix3x2F::Scale(press,press,D2D1::Point2F(ax,ay))*originalTransform);
	renderer.Shape(context,automaticDivider_,BarUiInheritClass(automaticDivider_.inhX,automaticDivider_.inhY));
	renderer.Svg(context,automaticArrow_,BarUiInheritClass(automaticArrow_.x.val,automaticArrow_.y.val));
	if(std::abs(press-1.0f)>0.000001f)context->SetTransform(originalTransform);
	const int focus=focused_;if(focus==0 || IsAutomatic(focus))focusButton(ButtonVisual(focus));
	context->PopAxisAlignedClip();context->PopLayer();renderer.SetFrameDiffuseMaskGeometryScale(1);
	if(frame_.menuVisible)
	{
		PushOpacity(context,frame_.menuOpacity);renderer.SetFrameDiffuseMaskGeometryScale(1/frame_.menuPose.scale);drawSurface(menu_);
		context->PushAxisAlignedClip(PixelRect(g.menu,zoom_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		renderer.Word(context,title_,BarUiInheritClass(g.menuTitle.left,g.menuTitle.top),DWRITE_FONT_WEIGHT_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING);
		for(int i:{6,7,8,9})drawButton(i);if(focus>=6 && focus<=8)focusButton(focus);
		context->PopAxisAlignedClip();context->PopLayer();renderer.SetFrameDiffuseMaskGeometryScale(1);
	}
}

RECT BarEraserAttributePanel::Bounds() const
{
	RECT result{};if(!frame_.panelVisible)return result;
	auto bounds=[&](EraserAttributeRect rect,double scale)
	{
		const int padding=static_cast<int>(ceil((BarButtonFrameThicknessDip*scale+BarRenderingAttribute::pointLightDiffuseExtraWidth)*zoom_))+BarRenderingAttribute::dirtyAntialiasPadding;
		return IntegerRect(rect,zoom_,padding);
	};
	result=bounds(frame_.geometry.panel,frame_.panelPose.scale);
	if(frame_.menuVisible)BarRenderingAttribute::UnionRectInPlace(result,bounds(frame_.geometry.menu,frame_.menuPose.scale));
	return result;
}
void BarEraserAttributePanel::CommitPresented()
{
	std::scoped_lock lock(presentationMutex_);presented_=frame_;
}
EraserAttributePresentation BarEraserAttributePanel::PresentationSnapshot() const
{
	std::scoped_lock lock(presentationMutex_);return presented_;
}
std::array<RECT,3> BarEraserAttributePanel::PresentedRegions() const
{
	std::scoped_lock lock(presentationMutex_);const auto& f=presented_;
	return {f.panelVisible?IntegerRect(f.geometry.panel,f.zoom):RECT{},f.menuVisible?IntegerRect(f.geometry.menu,f.zoom):RECT{},RECT{}};
}
void BarEraserAttributePanel::Close(BarUISetClass& owner)
{
	owner.barState.eraserAttribute=false;owner.barState.eraserSensitivityOpen=false;pressed_=-1;hovered_=-1;focused_=-1;visible_=false;
}
void BarEraserAttributePanel::Execute(BarUISetClass& owner,int item)
{
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
			owner.UpdateRendering();return;
		}
	}
	else if(IsSize(item))SetGlobalEraserPreference(static_cast<int>(BaseSizePresets[item-1]));
	else if(item==4)SetGlobalEraserPreference(-1,-1,GetAutomaticState(EraserPreferencesSnapshot())!=AutomaticState::On);
	else if(item==5){if(owner.TryBeginToggle(BarToggleChannel::EraserSensitivity))owner.barState.eraserSensitivityOpen=!owner.barState.eraserSensitivityOpen;}
	else if(item>=6 && item<=8)SetGlobalEraserPreference(-1,item-6);
	owner.UpdateRendering(false);
}
bool BarEraserAttributePanel::ResetPointerFeedback()
{
	const int old=ButtonVisual(hovered_);const bool changed=hovered_>=0 || pressed_>=0;
	hovered_=-1;pressed_=-1;
	if(old>=0)StopBarButtonHoverVisual(buttons_[old],false);
	return changed;
}
bool BarEraserAttributePanel::Pointer(BarUISetClass& owner,const ExMessage& message,bool cancelled,bool contactPointer)
{
	const auto f=PresentationSnapshot();
	if(!f.panelVisible && pressed_<0)return false;
	const auto& geometry=f.geometry;const double x=message.x/f.zoom,y=message.y/f.zoom;
	auto contains=[&](EraserAttributeRect rect,double scale)
	{
		return rect.Width()>0 && rect.Height()>0 && BarUiRoundedRectContainsPoint(message.x,message.y,f.zoom,rect.left,rect.top,
			rect.Width(),rect.Height(),BarMainBarCornerRadiusDip*scale,BarMainBarCornerRadiusDip*scale);
	};
	const bool overMenu=f.menuVisible && contains(geometry.menu,f.menuPose.scale);
	const bool inside=overMenu || (f.panelVisible && contains(geometry.panel,f.panelPose.scale));
	int hit=-1;
	if(inside)
	{
		const auto clip=overMenu?geometry.menu:geometry.panel;
		for(int i=overMenu?6:0;i<(overMenu?10:6);++i)
		{
			if(!IntersectEraserRect(geometry.items[i],clip).Contains(x,y))continue;
			if(IsSize(i))
			{
				const auto circle=geometry.previews[i-1];
				if(!EraserAttributeCircleContains(circle,BarButtonGapDip*f.panelPose.scale,x,y))continue;
			}
			hit=i;break;
		}
	}
	if(message.message==WM_MOUSEMOVE)
	{
		if(hovered_!=hit)
		{
			const int old=ButtonVisual(hovered_);hovered_=hit;focused_=-1;const int visual=ButtonVisual(hit);
			if(old!=visual)
			{
				if(old>=0)StopBarButtonHoverVisual(buttons_[old],false);
				if(visual>=0 && visual!=9)StartBarButtonHoverVisual(buttons_[visual]);
			}
			owner.UpdateRendering(false);
		}
		if(pressed_>=0 && cancelled){pressed_=-1;owner.UpdateRendering(false);}return inside;
	}
	const bool down=message.message==WM_LBUTTONDOWN || message.message==WM_LBUTTONDBLCLK;
	if(down)
	{
		if(owner.barState.eraserSensitivityOpen && !overMenu && hit!=5){owner.barState.eraserSensitivityOpen=false;owner.UpdateRendering(false);}
		const bool enabled=hit!=9;
		// 必须是面板收到的新Down才能建立动作票据；主栏开栏的那次Up不能落到中央Clear。
		pressed_=inside && enabled && owner.barState.eraserAttribute && !cancelled?hit:-1;
		focused_=-1;owner.UpdateRendering(false);return inside;
	}
	if(message.message==WM_LBUTTONUP)
	{
		const int pressed=pressed_;
		pressed_=-1;
		// 同一整体按钮内跨区释放，仍执行Down时的动作，不改成另一命令。
		const int action=ResolveEraserAttributeRelease(pressed,hit,inside,cancelled);
		if(action>=0 && owner.barState.eraserAttribute)Execute(owner,action);
		if(contactPointer)ResetPointerFeedback();
		if(pressed>=0 || contactPointer)owner.UpdateRendering(false);return inside || pressed>=0;
	}
	return inside;
}
bool BarEraserAttributePanel::Keyboard(BarUISetClass& owner,BYTE key,bool down)
{
	if(!owner.barState.eraserAttribute)return false;
	if(key==VK_ESCAPE)
	{
		if(down){if(owner.barState.eraserSensitivityOpen)owner.barState.eraserSensitivityOpen=false;else Close(owner);owner.UpdateRendering(false);}return true;
	}
	if(key!=VK_TAB && key!=VK_LEFT && key!=VK_RIGHT && key!=VK_RETURN && key!=VK_SPACE)return false;
	if(!down)return true;
	if(key==VK_RETURN || key==VK_SPACE){if(focused_>=0)Execute(owner,focused_);return true;}
	const int first=owner.barState.eraserSensitivityOpen?6:0,last=owner.barState.eraserSensitivityOpen?8:5;
	const int delta=key==VK_LEFT || (key==VK_TAB && (GetKeyState(VK_SHIFT)&0x8000))?-1:1;
	int focus=focused_;if(focus<first || focus>last)focus=first;else focus=first+(focus-first+delta+last-first+1)%(last-first+1);
	focused_=focus;hovered_=-1;owner.UpdateRendering(false);return true;
}
