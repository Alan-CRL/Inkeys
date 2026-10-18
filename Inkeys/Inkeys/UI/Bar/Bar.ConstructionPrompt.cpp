module;
#include "../../../IdtMain.h"
#include "Bar.BottomDock.h"
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include <d2d1_1helper.h>
#include <algorithm>
#include <cmath>
module Inkeys.UI.Bar;
import :Main;
import :Scene;
import :Layout;
import :Theme;
using namespace Inkeys::UI::Bar;
using namespace Inkeys::Drawing::Draw3;
using namespace draw3::shapes;
namespace
{
	std::wstring PromptLabel(const ConstructionStatus& status)
	{
		const auto& keys=I18nKey.UI.Bar.ShapeConstruction;
		if(GetToolCapabilities(status.preset).points)return I18n::getWOr(keys.NextVertex,L"落笔确定下一顶点");
		const bool first=status.confirmedSteps==0;
		if(status.preset==PresetKind::Arc||status.preset==PresetKind::Sector)
			return first?I18n::getWOr(keys.RadiusStep,L"确定圆心与半径"):I18n::getWOr(keys.SweepStep,L"确定圆弧范围");
		if(status.preset==PresetKind::Cuboid||status.preset==PresetKind::Cube)
			return first?I18n::getWOr(keys.FrontStep,L"确定正面"):I18n::getWOr(keys.DepthStep,L"确定深度");
		return first?I18n::getWOr(keys.BaseStep,L"确定底面"):I18n::getWOr(keys.HeightStep,L"确定高度");
	}
	RECT PromptPixels(EraserAttributeRect r,double z,int pad=0)
	{return {static_cast<LONG>(std::floor(r.left*z))-pad,static_cast<LONG>(std::floor(r.top*z))-pad,static_cast<LONG>(std::ceil(r.right*z))+pad,static_cast<LONG>(std::ceil(r.bottom*z))+pad};}
}
void BarConstructionPrompt::Initialize()
{
	if(initialized_)return;
	surface_.Initialization(0,0,1,1,BarBottomDockIndicatorCornerRadiusDip,BarBottomDockIndicatorCornerRadiusDip,BarButtonFrameThicknessDip,GetThemeColor(BarThemeColorEnum::Surface),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	surface_.enable.Initialization(true);surface_.pct.SetDirect(BarMainBarFillOpacity);surface_.framePct.emplace(BarMainBarFrameOpacity);surface_.frameLightPct.emplace(BarMainBarFrameOpacity);
	surface_.frameRendering=BarUiFrameRenderingEnum::PointLight;surface_.frameLightColor=BarUiFrameLightColorEnum::PenWhenDrawing;
	label_.Initialization(0,0,200,BarButtonOneSideDip,L"",12,GetThemeColor(BarThemeColorEnum::TextPrimary));label_.enable.Initialization(true);
	std::vector<std::shared_ptr<BarButtonClass>> list;
	for(auto& p:buttons_)
	{
		p=std::make_shared<BarButtonClass>();p->hide=false;p->size=BarButtonSizeEnum::twoOne;
		p->button.Initialization(0,0,70,32.5,4,4,1,GetThemeColor(BarThemeColorEnum::PressedFill),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		p->button.enable.Initialization(true);p->button.pct.SetDirect(0);p->button.framePct.emplace(0);p->button.frameLightPct.emplace(0);p->button.frameRendering=BarUiFrameRenderingEnum::PointLight;
		p->icon.enable.Initialization(false);p->name.Initialization(0,0,70,32.5,L"",12,GetThemeColor(BarThemeColorEnum::TextPrimary));p->name.enable.Initialization(true);
		ConfigureBarButtonVisualTargets(*p,BarButtonVisualLayoutKind::StandardTwoOne,0,1,true);p->name.x.SetDirect(0);p->name.w.SetDirect(70);list.push_back(p);
	}list_.Replace(std::move(list));initialized_=true;
}
bool BarConstructionPrompt::Advance(BarUISetClass& owner,std::shared_ptr<const ShapeUiSnapshot> snapshot,double dt,double speed,double zoom,RECT workArea,POINT origin,double rigidX,double rigidY)
{
	Initialize();changed_=false;const bool open=snapshot&&snapshot->construction.active&&!owner.barState.fold;
	opacity_.SetTar(open?1:0);const BarUiAnimationAdvanceContextClass anim{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
	bool active=false;const auto advance=[&](auto& v){const auto r=BarUiAdvanceAnimation(v,anim);changed_|=r.changed;active|=r.active;};advance(opacity_);
	if(!open&&opacity_.val<=.000001){changed_|=frame_.visible;frame_.visible=frame_.input=false;visible_=false;return changed_;}
	const auto root=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	const auto main=owner.shapeMap[BarUISetShapeEnum::MainBar];
	if(!root||!main)return false;
	root->UpInh({root->x.val-root->w.val/2,root->y.val-root->h.val/2});main->Inherit(BarUiInheritEnum::Center,*root);
	const auto status=snapshot?snapshot->construction:ConstructionStatus{};
	if(open)label_.TransitionToString(PromptLabel(status));
	const auto& keys=I18nKey.UI.Bar.ShapeConstruction;
	const std::array<std::wstring,3> labels{I18n::getWOr(keys.Complete,L"完成"),I18n::getWOr(keys.Back,L"回退"),I18n::getWOr(keys.CancelAction,L"取消")};
	const bool points=GetToolCapabilities(status.preset).points;
	buttons_[0]->hide=!points;buttons_[1]->hide=!status.canBack;buttons_[2]->hide=false;
	const auto layout=ResolveBarButtonListLayout(list_.Snapshot(),BarButtonListLayoutKind::SingleRow);
	const double labelWidth=owner.spec.MeasureText(label_.TargetContent(),12,DWRITE_FONT_WEIGHT_NORMAL).width;
	const double workLeft=(workArea.left-origin.x)/zoom-rigidX,workRight=(workArea.right-origin.x)/zoom-rigidX;
	const double panelWidth=(std::min)(labelWidth+layout.width+15,(std::max)(80.,workRight-workLeft-10));
	const bool stacked = labelWidth + layout.width + 15 > panelWidth;
	const int columns = (std::max)(1, (std::min)(3, static_cast<int>((panelWidth-5)/75)));
	const int actionCount = 1 + (points?1:0) + (status.canBack?1:0);
	const double targetHeight = stacked ? 5 + BarButtonOneSideDip + 5 + std::ceil(static_cast<double>(actionCount)/columns)*37.5 : BarButtonOneSideDip+10;
	height_.SetTar(targetHeight); advance(height_); const double panelHeight = height_.val;
	const auto anchor=ResolveBarBottomDockIndicatorGeometry({root->inhX,root->inhY,root->inhX+root->w.val,root->inhY+root->h.val},{main->inhX,main->inhY,main->inhX+main->w.val,main->inhY+main->h.val},panelWidth,12,main->inhY);
	const double targetLeft=(std::clamp)((anchor.leftDip+anchor.rightDip-panelWidth)/2,workLeft+5,(std::max)(workLeft+5,workRight-panelWidth-5));
	const double workTop=(workArea.top-origin.y)/zoom-rigidY;
	// 旧提示只提供水平锚点；完整操作面板按自身高度留在主栏上方，不能沿用跨越主栏top的小pill高度。
	const double above=main->inhY-targetHeight-5;
	const double candidateTop=above>=workTop+5?above:main->inhY+main->h.val+5;
	const double workBottom=(workArea.bottom-origin.y)/zoom-rigidY;
	const double targetTop=(std::clamp)(candidateTop,workTop+5,(std::max)(workTop+5,workBottom-targetHeight-5));
	left_.SetTar(targetLeft);top_.SetTar(targetTop);width_.SetTar(panelWidth);advance(left_);advance(top_);advance(width_);
	Frame next;next.identity=snapshot?snapshot->identity:frame_.identity;next.zoom=zoom;next.opacity=opacity_.val;next.visible=opacity_.val>.000001;
	next.panel={left_.val,top_.val,left_.val+width_.val,top_.val+panelHeight};next.input=open&&opacity_.IsSame()&&left_.IsSame()&&top_.IsSame()&&width_.IsSame()&&height_.IsSame();
	surface_.x.SetDirect(left_.val);surface_.y.SetDirect(top_.val);surface_.w.SetDirect(width_.val);surface_.h.SetDirect(panelHeight);surface_.UpInh({left_.val,top_.val});
	surface_.fill->SetTar(GetThemeColor(BarThemeColorEnum::Surface));surface_.frame->SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	const auto sr=AdvanceBarShapeVisual(surface_,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)});changed_|=sr.changed;active|=sr.active;
	const double textWidth=(std::max)(0.,stacked?width_.val-10:width_.val-layout.width-10);label_.x.SetDirect(5+textWidth/2);label_.y.SetDirect(stacked?5+BarButtonOneSideDip/2:panelHeight/2);label_.w.SetDirect(textWidth);label_.color.SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
	const auto lr=AdvanceBarWordVisual(label_,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)});changed_|=lr.changed;active|=lr.active;
	int visibleIndex=0;
	for(int i=0;i<3;++i)
	{
		auto& b=*buttons_[i];const bool wanted=i==0?points:i==1?status.canBack:true;const bool enabled=i==0?status.canComplete:i==1?status.canBack:true;
		b.name.TransitionToString(labels[i]);const auto& p=layout.placements[i];
		if(wanted)
		{
			x_[i].SetTar(stacked?5+35+(visibleIndex%columns)*75:textWidth+p.x+5);
			y_[i].SetTar(stacked?10+BarButtonOneSideDip+(visibleIndex/columns)*37.5+BarButtonOneSideDip/2:5+BarButtonOneSideDip/2);
			++visibleIndex;
		}
		advance(x_[i]); advance(y_[i]);
		RetargetBarButtonInteractionVisual(b,wanted,enabled,false,BarButtonHoverTransitionDuration,GetThemeColor(BarThemeColorEnum::TextPrimary),false);
		if(hovered_==i&&appliedHover_!=i)StartBarButtonHoverVisual(b);if(appliedHover_==i&&hovered_!=i)StopBarButtonHoverVisual(b,false);
		SetBarButtonPressedVisual(b,pressed_==i);const auto r=AdvanceBarButtonVisual(b,{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false,wanted||b.name.pct.val>.000001,true});changed_|=r.changed;active|=r.active;
		b.hide=!wanted&&b.name.pct.val<=.000001;b.button.x.SetDirect(left_.val+x_[i].val);b.button.y.SetDirect(top_.val+y_[i].val);b.button.UpInh({b.button.x.val-35,b.button.y.val-BarButtonOneSideDip/2});
		next.buttons[i]=ResolveBarButtonHitSnapshot(b,{b.button.inhX,b.button.inhY});next.buttons[i].enabled&=next.input&&enabled&&wanted&&x_[i].IsSame()&&y_[i].IsSame()&&b.name.pct.IsSame();next.buttons[i].visible=next.visible&&!b.hide.load();
	}appliedHover_=hovered_;
	changed_|=frame_.panel!=next.panel||frame_.opacity!=next.opacity||frame_.identity!=next.identity;frame_=next;visible_=next.visible;return changed_||active;
}
void BarConstructionPrompt::Draw(BarUIRendering& renderer,ID2D1DeviceContext* dc)
{
	if(!frame_.visible)return;renderer.PushPanelOpacity(dc,{static_cast<float>(frame_.panel.left*frame_.zoom),static_cast<float>(frame_.panel.top*frame_.zoom),static_cast<float>(frame_.panel.right*frame_.zoom),static_cast<float>(frame_.panel.bottom*frame_.zoom)},frame_.opacity);
	DrawBarBackgroundVisual(renderer,dc,surface_,{surface_.inhX,surface_.inhY});dc->PushAxisAlignedClip({static_cast<float>(frame_.panel.left*frame_.zoom),static_cast<float>(frame_.panel.top*frame_.zoom),static_cast<float>(frame_.panel.right*frame_.zoom),static_cast<float>(frame_.panel.bottom*frame_.zoom)},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);renderer.Word(dc,label_,label_.Inherit(BarUiInheritEnum::CenterFromTopLeft,surface_),DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_LEADING);
	for(auto& b:buttons_)if(!b->hide)DrawBarButtonVisual(renderer,dc,*b,{b->button.inhX,b->button.inhY});dc->PopAxisAlignedClip();dc->PopLayer();
}
void BarConstructionPrompt::CommitPresented(){std::scoped_lock lock(mutex_);presented_=frame_;}
RECT BarConstructionPrompt::Bounds()const{return frame_.visible?PromptPixels(frame_.panel,frame_.zoom,8):RECT{};}
std::array<RECT,1> BarConstructionPrompt::PresentedRegions()const{std::scoped_lock lock(mutex_);return {presented_.visible?PromptPixels(presented_.panel,presented_.zoom):RECT{}};}
bool BarConstructionPrompt::Pointer(BarUISetClass& owner,const ExMessage& message,bool cancelled)
{
	Frame f;{std::scoped_lock lock(mutex_);f=presented_;}if(!f.visible)return false;
	if(cancelled){hovered_=-1;pressed_=-1;owner.UpdateRendering(false);return true;}
	const double px=message.x/f.zoom,py=message.y/f.zoom;const bool inside=px>=f.panel.left&&px<f.panel.right&&py>=f.panel.top&&py<f.panel.bottom;
	int hit=-1;for(int i=0;i<3;++i)if(HitTestBarButton(f.buttons[i],px,py)){hit=i;break;}
	if(message.message==WM_LBUTTONDOWN){if(!inside)return false;pressed_=hit;pressedIdentity_=f.identity;hovered_=-1;}
	else if(message.message==WM_MOUSEMOVE){if(pressed_>=0&&pressed_!=hit)pressed_=-1;hovered_=hit;}
	else if(message.message==WM_LBUTTONUP)
	{
		const int pressed=pressed_;pressed_=-1;hovered_=-1;
		if(pressed>=0&&pressed==hit&&pressedIdentity_==f.identity){ShapeUiAction action;action.identity=pressedIdentity_;action.kind=hit==0?ShapeUiActionKind::ConstructionComplete:hit==1?ShapeUiActionKind::ConstructionBack:ShapeUiActionKind::ConstructionCancel;(void)SubmitShapeUiIntent(action);}
	}
	owner.UpdateRendering(false);return inside||pressed_>=0;
}
bool BarConstructionPrompt::Keyboard(BarUISetClass& owner,BYTE key,bool down)
{
	Frame f;{std::scoped_lock lock(mutex_);f=presented_;}if(!f.visible)return false;
	if(key==VK_ESCAPE){if(down){ShapeUiAction a;a.identity=f.identity;a.kind=ShapeUiActionKind::ConstructionCancel;(void)SubmitShapeUiIntent(a);}return true;}
	if(key==VK_TAB||key==VK_RIGHT||key==VK_LEFT){if(down){for(int k=0;k<3;++k){focused_=(focused_+3+(key==VK_LEFT?-1:1))%3;if(f.buttons[focused_].enabled)break;}}owner.UpdateRendering(false);return true;}
	if(key==VK_RETURN||key==VK_SPACE){if(down&&focused_>=0&&f.buttons[focused_].enabled){ShapeUiAction a;a.identity=f.identity;a.kind=focused_==0?ShapeUiActionKind::ConstructionComplete:focused_==1?ShapeUiActionKind::ConstructionBack:ShapeUiActionKind::ConstructionCancel;(void)SubmitShapeUiIntent(a);}return true;}return false;
}
