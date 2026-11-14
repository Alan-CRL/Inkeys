module;
#include "../../../IdtMain.h"
#include "Bar.BottomDock.h"
#include "Bar.ThemeMaterial.h"
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
import :RenderingAttribute;
using namespace Inkeys::UI::Bar;
using namespace Inkeys::Drawing::Draw3;
using namespace draw3::shapes;
extern const double BarUiDividerRadius;
extern const double BarUiDividerCursorLightIntensity;
namespace
{
	// 只调整视觉/键盘顺序，动作索引继续保持稳定。
	constexpr std::array<int,4> PromptOrder{0,1,3,2};
	constexpr std::array<ShapeUiActionKind,4> PromptActions{ShapeUiActionKind::ConstructionBack,
		ShapeUiActionKind::ConstructionForward,ShapeUiActionKind::ConstructionComplete,ShapeUiActionKind::ConstructionCancel};
	std::wstring PromptStepDescription(const ConstructionStatus& status)
	{
		const auto& keys=I18nKey.UI.Bar.ShapeConstruction;
		if(GetToolCapabilities(status.preset).points)return I18n::getWOr(keys.NextVertex,L"落笔确定下一顶点");
		const bool first=status.confirmedSteps==0;
		if(status.preset==PresetKind::Arc||status.preset==PresetKind::Sector)
			return first?I18n::getWOr(keys.RadiusStep,L"确定圆心与半径"):I18n::getWOr(keys.SweepStep,L"确定圆弧范围");
		return first?I18n::getWOr(keys.BaseStep,L"确定底面"):I18n::getWOr(keys.HeightStep,L"确定高度");
	}
	std::wstring PromptFormat(std::wstring format,uint32_t count,const std::wstring& description)
	{
		for(const auto& replacement:std::array<std::pair<std::wstring,std::wstring>,2>{{{L"{0}",std::to_wstring(count)},{L"{1}",description}}})
		{
			size_t at=0;
			while((at=format.find(replacement.first,at))!=std::wstring::npos)
			{format.replace(at,replacement.first.size(),replacement.second);at+=replacement.second.size();}
		}
		return format;
	}
	std::wstring PromptLabel(const ConstructionStatus& status)
	{
		const auto& keys=I18nKey.UI.Bar.ShapeConstruction;const auto description=PromptStepDescription(status);
		if(status.totalSteps)return PromptFormat(I18n::getWOr(keys.StepFormat,L"第 {0} 步 · {1}"),status.step,description);
		if(!status.confirmedSteps)return description;
		return PromptFormat(I18n::getWOr(keys.PointCountFormat,L"已确定 {0} 个点 · {1}"),status.confirmedSteps,description);
	}
	int PromptOutset(double zoom,double scale)
	{return static_cast<int>(std::ceil(BarThemeMaterial::SurfaceShadowOutsetDip*zoom*scale))+BarRenderingAttribute::dirtyAntialiasPadding;}
	RECT PromptPixels(EraserAttributeRect r,double z,int pad=0)
	{return {static_cast<LONG>(std::floor(r.left*z))-pad,static_cast<LONG>(std::floor(r.top*z))-pad,static_cast<LONG>(std::ceil(r.right*z))+pad,static_cast<LONG>(std::ceil(r.bottom*z))+pad};}
}
void BarConstructionPrompt::Initialize()
{
	if(initialized_)return;
	surface_.Initialization(0,0,1,1,6,6,BarButtonFrameThicknessDip,GetThemeColor(BarThemeModeEnum::Dark,BarThemeColorEnum::Surface),GetThemeColor(BarThemeModeEnum::Dark,BarThemeColorEnum::SurfaceFrame));
	surface_.enable.Initialization(true);surface_.pct.SetDirect(BarMainBarFillOpacity);surface_.framePct.emplace(BarMainBarFrameOpacity);surface_.frameLightPct.emplace(BarMainBarFrameOpacity);
	surface_.themeSurface=surface_.residentSurface=true;surface_.lightFillOpacityScale=BarThemeMaterial::LightResidentSurfaceOpacityScale;
	surface_.frameRendering=BarUiFrameRenderingEnum::PointLight;surface_.frameLightColor=BarUiFrameLightColorEnum::PenWhenDrawing;
	surface_.framePrimaryLightEnabled=true;surface_.frameCursorLightIntensityScale=1.0;
	label_.Initialization(0,0,200,BarButtonOneSideDip,L"",13,GetThemeColor(BarThemeColorEnum::TextPrimary));label_.enable.Initialization(true);
	ringLabel_.Initialization(0,0,24,24,L"",9.5,GetThemeColor(BarThemeColorEnum::TextPrimary));ringLabel_.enable.Initialization(true);
	divider_=std::make_shared<BarButtonClass>();divider_->preset=BarButtonPresetEnum::Divider;divider_->size=BarButtonSizeEnum::oneTwo;divider_->hide=false;
	divider_->button.Initialization(0,0,1,BarButtonOneSideDip-4,BarUiDividerRadius,BarUiDividerRadius,BarButtonFrameThicknessDip,GetThemeColor(BarThemeColorEnum::Divider),GetThemeColor(BarThemeColorEnum::Divider));
	divider_->button.enable.Initialization(true);divider_->button.pct.SetDirect(.30);divider_->button.framePct.emplace(0);divider_->button.frameLightPct.emplace(1);
	divider_->button.frameRendering=BarUiFrameRenderingEnum::PointLight;divider_->button.framePrimaryLightEnabled=false;divider_->button.frameCursorLightIntensityScale=BarUiDividerCursorLightIntensity;
	divider_->icon.enable.Initialization(false);divider_->name.enable.Initialization(false);
	std::vector<std::shared_ptr<BarButtonClass>> list{divider_};
	const std::array<const wchar_t*,4> resources{L"BARSTEPBACK",L"BARSTEPFORWARD",L"FLUENTCHECKMARK12FILLED",L"BARCLOSESMALL"};
	for(size_t i=0;i<buttons_.size();++i)
	{
		auto& p=buttons_[i];p=std::make_shared<BarButtonClass>();p->hide=false;p->size=BarButtonSizeEnum::oneOne;
		p->button.Initialization(0,0,BarButtonOneSideDip,BarButtonOneSideDip,4,4,1,GetThemeColor(BarThemeColorEnum::PressedFill),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		p->button.enable.Initialization(true);p->button.pct.SetDirect(0);p->button.framePct.emplace(0);p->button.frameLightPct.emplace(0);
		p->icon.Initialization(0,0,GetThemeColor(BarThemeColorEnum::TextPrimary),GetThemeColor(BarThemeColorEnum::TextPrimary));p->icon.enable.Initialization(true);p->icon.InitializationFromResource(L"UI",resources[i]);
		p->name.Initialization(0,0,0,0,L"",13,GetThemeColor(BarThemeColorEnum::TextPrimary));p->name.enable.Initialization(false);
		// SVG载荷准备后再解析OneOne槽，避免首次图标宽高为零。
		ConfigureBarButtonVisualTargets(*p,BarButtonVisualLayoutKind::StandardOneOne,0,1,true);
	}
	for(const auto i:PromptOrder)list.push_back(buttons_[i]);
	list_.Replace(std::move(list));initialized_=true;
}
bool BarConstructionPrompt::Advance(BarUISetClass& owner,std::shared_ptr<const ShapeUiSnapshot> snapshot,double dt,double speed,double zoom,RECT workArea,POINT origin,double rigidX,double rigidY,const BarUiTimelineClass* parentTimeline)
{
	Initialize();changed_=false;
	const bool open=snapshot&&snapshot->construction.active&&snapshot->construction.totalSteps!=1;
	const bool hidden=opacity_.val<=.000001;const bool opening=open&&opacity_.tar==0;
	const auto curve=open?BarUiCurveEnum::EaseOutBack:BarUiCurveEnum::EaseInBack;
	const BarUiCurveSpecClass visibilityCurve{curve,curve,0,false};
	opacity_.SetTar(open?1:0,.4,std::nullopt,false,visibilityCurve);
	const BarUiAnimationAdvanceContextClass anim{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
	bool active=false;const auto advance=[&](auto& v){const auto r=BarUiAdvanceAnimation(v,anim);changed_|=r.changed;active|=r.active;};
	if(!open&&hidden&&opacity_.IsSame())
	{
		changed_|=frame_.visible;frame_.visible=frame_.input=false;visible_=false;hovered_=pressed_=focused_=-1;return changed_;
	}
	const auto root=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];const auto main=owner.shapeMap[BarUISetShapeEnum::MainBar];
	if(!root||!main)return false;
	root->UpInh({root->x.val-root->w.val/2,root->y.val-root->h.val/2});main->Inherit(BarUiInheritEnum::Center,*root);
	if(open)
	{
		if(frame_.identity.session!=snapshot->identity.session){hovered_=pressed_=focused_=-1;}
		status_=snapshot->construction;
	}
	// 完成后快照会切到编辑会话；先按原构造身份接收完成沿，再锁存到退场结束。
	if(!open&&snapshot&&snapshot->construction.finished
		&&snapshot->completedConstructionSession==frame_.identity.session)status_.finished=true;
	const auto status=status_;
	const bool points=GetToolCapabilities(status.preset).points;
	if(open){desiredLabel_=PromptLabel(status);desiredRingLabel_=status.totalSteps?std::to_wstring(status.step)+L"/"+std::to_wstring(status.totalSteps):L"";}
	const double workLeft=(workArea.left-origin.x)/zoom-rigidX,workRight=(workArea.right-origin.x)/zoom-rigidX;
	const double available=(std::max)(80.,workRight-workLeft-10);
	const double textLeft=status.totalSteps?41.:12.;
	const double measured=owner.spec.MeasureText(desiredLabel_,13,DWRITE_FONT_WEIGHT_BOLD).width;
	const std::array<bool,4> wanted{true,true,points,true};
	const bool contact=snapshot&&snapshot->constructionContactActive;
	const std::array<bool,4> enabled{!contact&&status.canBack,!contact&&status.canForward,!contact&&status.canComplete,!contact};
	unsigned state=points?1u:0u;for(size_t i=0;i<4;++i){buttons_[i]->hide=!wanted[i];if(enabled[i])state|=2u<<i;}
	const auto layout=ResolveBarButtonListLayout(list_.Snapshot(),BarButtonListLayoutKind::SingleRow);
	const bool stacked=textLeft+measured+5+layout.width>available;
	const int capacity=(std::max)(1,(std::min)(4,static_cast<int>((available-5)/(BarButtonOneSideDip+5))));
	// 两个终结按钮作为一对换行，窄窗口也不能把勾移到叉的下一行。
	const int columns=points?(capacity>=4?4:2):capacity;
	const double desiredTextWidth=(std::min)(measured,(std::max)(0.,available-textLeft-(stacked?5.:5+layout.width)));
	const double targetChrome=stacked?available-desiredTextWidth:textLeft+5+layout.width;
	const double targetHeight=stacked?42.5+std::ceil((points?4.:3.)/columns)*37.5:42.5;
	if(hidden)
	{
		label_.SetStringImmediate(desiredLabel_);ringLabel_.SetStringImmediate(desiredRingLabel_);textWidth_.SetDirect(desiredTextWidth);
		textTransition_=false;contentPhase_=BarBottomDockIndicatorContentPhase::Stable;
	}
	else
	{
		if(textTransition_&&label_.content.GetVal()==desiredLabel_&&label_.contentPct==1.&&label_.contentScale==1.)textTransition_=false;
		const auto startWidth=[&](double target,BarBottomDockIndicatorContentPhase phase){textWidth_.SetTar(target,.4);contentPhase_=phase;};
		const auto swap=[&]()
		{
			if(label_.content.GetVal()==desiredLabel_&&!textTransition_)return;
			if(!BarUiAnimationEnabled){label_.SetStringImmediate(desiredLabel_);ringLabel_.SetStringImmediate(desiredRingLabel_);textWidth_.SetDirect(desiredTextWidth);textTransition_=false;contentPhase_=BarBottomDockIndicatorContentPhase::Stable;return;}
			textTransition_=label_.TransitionToString(desiredLabel_,.4,.5,.8)||textTransition_;
			ringLabel_.TransitionToString(desiredRingLabel_,.4,.5,.8);contentPhase_=BarBottomDockIndicatorContentPhase::SwappingText;
		};
		AdvanceBarIndicatorContent(contentPhase_,textWidth_,static_cast<double>(textWidth_.val),desiredTextWidth,
			label_.content.GetVal()==desiredLabel_&&!textTransition_,[&]{return label_.content.GetVal()==desiredLabel_;},startWidth,swap);
	}
	advance(textWidth_);
	// 显隐、禁用与面板留白加入同一0.4秒批次，中途变化可接续剩余时间。
	const bool layoutChanged=state!=buttonState_||width_.tar!=targetChrome||height_.tar!=targetHeight;
	if(layoutChanged&&!hidden)
	{
		if(parentTimeline&&parentTimeline->CanJoin())layoutTimeline_=*parentTimeline;
		else if(!layoutTimeline_.CanJoin())layoutTimeline_.Restart(.4);
	}
	const double duration=layoutTimeline_.IsActive()?layoutTimeline_.GetRemainingDuration():.4;
	const double phase=layoutTimeline_.IsActive()?layoutTimeline_.GetProgress():0.;
	const BarUiCurveSpecClass layoutCurve{BarUiCurveEnum::EaseInOutCubic,BarUiCurveEnum::EaseInOutCubic,phase,phase>0};
	if(hidden){width_.SetDirect(targetChrome);height_.SetDirect(targetHeight);}
	else{width_.SetTar(targetChrome,duration,std::nullopt,layoutChanged,layoutCurve);height_.SetTar(targetHeight,duration,std::nullopt,layoutChanged,layoutCurve);}
	advance(width_);advance(height_);advance(opacity_);
	const double panelWidth=width_.val+textWidth_.val,panelHeight=height_.val;
	// 手动收起只改变锚点，不能结束构造提示；展开时以主按钮和主栏可见并集定位。
	const double rootLeft=root->inhX,rootRight=rootLeft+root->w.val,rootTop=root->inhY,rootBottom=rootTop+root->h.val;
	const bool folded=owner.barState.fold;
	const double anchorLeft=folded?rootLeft:(std::min)(rootLeft,static_cast<double>(main->inhX));
	const double anchorRight=folded?rootRight:(std::max)(rootRight,main->inhX+main->w.val);
	const double anchorTop=folded?rootTop:(std::min)(rootTop,static_cast<double>(main->inhY));
	const double anchorBottom=folded?rootBottom:(std::max)(rootBottom,main->inhY+main->h.val);
	const double targetWidth=targetChrome+desiredTextWidth;
	const double targetLeft=(std::clamp)((anchorLeft+anchorRight-targetWidth)/2,workLeft+5,(std::max)(workLeft+5,workRight-targetWidth-5));
	const double workTop=(workArea.top-origin.y)/zoom-rigidY,workBottom=(workArea.bottom-origin.y)/zoom-rigidY;
	const double above=anchorTop-targetHeight-5;
	const double targetTop=(std::clamp)(above>=workTop+5?above:anchorBottom+5,workTop+5,(std::max)(workTop+5,workBottom-targetHeight-5));
	if(opening&&hidden){left_.SetDirect(targetLeft);top_.SetDirect(targetTop);}
	else{left_.SetTar(targetLeft,.4);top_.SetTar(targetTop,.4);}advance(left_);advance(top_);
	Frame next;next.identity=snapshot?snapshot->identity:frame_.identity;next.zoom=zoom;
	next.scale=(std::max)(0.,static_cast<double>(opacity_.val));next.opacity=(std::clamp)(next.scale,0.,1.);next.visible=next.opacity>.000001;
	next.ring=open?status.totalSteps>1:frame_.ring;
	next.panel={left_.val,top_.val,left_.val+panelWidth,top_.val+panelHeight};
	const double cx=(next.panel.left+next.panel.right)/2,cy=(next.panel.top+next.panel.bottom)/2;
	next.visualPanel={cx-panelWidth*next.scale/2,cy-panelHeight*next.scale/2,cx+panelWidth*next.scale/2,cy+panelHeight*next.scale/2};
	next.input=open&&opacity_.IsSame()&&left_.IsSame()&&top_.IsSame()&&width_.IsSame()&&height_.IsSame()&&textWidth_.IsSame();
	surface_.x.SetDirect(left_.val);surface_.y.SetDirect(top_.val);surface_.w.SetDirect(panelWidth);surface_.h.SetDirect(panelHeight);surface_.UpInh({left_.val,top_.val});surface_.lightMaterial=main->lightMaterial;
	const auto join=[&](const auto& r){changed_|=r.changed;active|=r.active;};
	join(AdvanceBarShapeVisual(surface_,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)}));
	label_.x.SetDirect(textLeft+textWidth_.val/2);label_.y.SetDirect(21.25);label_.w.SetDirect(textWidth_.val);label_.color.SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
	ringLabel_.x.SetDirect(21);ringLabel_.y.SetDirect(21.25);ringLabel_.color.SetTar(GetThemeColor(BarThemeColorEnum::TextPrimary));
	join(AdvanceBarWordVisual(label_,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)}));join(AdvanceBarWordVisual(ringLabel_,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)}));
	const double progress=status.totalSteps?static_cast<double>(status.step?status.step-1:0)/status.totalSteps:0.;
	const double ringTarget=!open&&status.finished?1.:progress;
	if(open||ringTarget!=ringProgress_.tar)
	{
		if(hidden&&open)ringProgress_.SetDirect(ringTarget);
		else ringProgress_.SetTar(ringTarget,!open&&status.finished?.24:.4,std::nullopt,false,{BarUiCurveEnum::EaseOutCubic,BarUiCurveEnum::EaseOutCubic,0,false});
	}
	advance(ringProgress_);
	const auto& keys=I18nKey.UI.Bar.ShapeConstruction;
	next.accessibleNames={IW(keys.BackName),IW(keys.ForwardName),IW(keys.CompleteName),IW(keys.CancelName)};
	next.tooltips={IW(keys.BackTooltip),IW(keys.ForwardTooltip),IW(keys.CompleteTooltip),IW(keys.CancelTooltip)};
	int visibleIndex=0;const int hoverTarget=hovered_>=0?static_cast<int>(hovered_):static_cast<int>(focused_);
	for(int order=0;order<4;++order)
	{
		const int i=PromptOrder[order];
		auto& b=*buttons_[i];b.name.SetStringImmediate(next.accessibleNames[i]);b.state->state=enabled[i]?BarWidgetState::None:BarWidgetState::Disable;
		const auto& placement=layout.placements[order+1];
		const double targetX=stacked?5+BarButtonOneSideDip/2+(visibleIndex%columns)*37.5:placement.x;
		const double targetY=stacked?42.5+BarButtonOneSideDip/2+(visibleIndex/columns)*37.5:21.25;
		if(wanted[i])++visibleIndex;
		if(hidden){x_[i].SetDirect(targetX);y_[i].SetDirect(targetY);}
		else{x_[i].SetTar(targetX,duration);y_[i].SetTar(targetY,duration);}advance(x_[i]);advance(y_[i]);
		const COLORREF contentColor=GetThemeColor(BarThemeColorEnum::TextPrimary);
		const bool retarget=layoutChanged||!b.icon.color1||b.icon.color1->tar!=contentColor;
		if(retarget)RetargetBarButtonInteractionVisual(b,wanted[i],enabled[i],false,duration,contentColor);
		// 本提示的设计合同固定禁用内容为0.30；用同一批次推进，不继承浅色端的0.45。
		const double targetOpacity=wanted[i]?(enabled[i]?1.:.30):0.;
		if(retarget)
		{
			if(hidden)b.icon.pct.SetDirect(targetOpacity);
			else b.icon.pct.SetTar(targetOpacity,duration,std::nullopt,layoutChanged,layoutCurve);
		}
		b.button.lightMaterial=surface_.lightMaterial;
		const bool hover=enabled[i]&&wanted[i]&&hoverTarget==i;
		if(hidden)StopBarButtonHoverVisual(b,true);
		if(hover&&appliedHover_!=i)StartBarButtonHoverVisual(b);
		if(appliedHover_==i&&!hover)StopBarButtonHoverVisual(b,!enabled[i]);
		SetBarButtonPressedVisual(b,enabled[i]&&pressed_==i);
		join(AdvanceBarButtonVisual(b,{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false,wanted[i]||b.icon.pct.val>.000001,enabled[i]}));
		b.hide=!wanted[i]&&b.icon.pct.val<=.000001;b.button.x.SetDirect(left_.val+x_[i].val+(stacked?0.:textLeft+textWidth_.val+5));b.button.y.SetDirect(top_.val+y_[i].val);
		b.button.UpInh({b.button.x.val-BarButtonOneSideDip/2,b.button.y.val-BarButtonOneSideDip/2});
		next.buttons[i]=ResolveBarButtonHitSnapshot(b,{b.button.inhX,b.button.inhY});
		next.buttons[i].enabled&=next.input&&enabled[i]&&wanted[i]&&x_[i].IsSame()&&y_[i].IsSame()&&b.icon.pct.IsSame();next.buttons[i].visible=next.visible&&!b.hide.load();
	}
	divider_->hide=stacked;divider_->button.fill->SetTar(GetThemeColor(BarThemeColorEnum::Divider));divider_->button.frame->SetTar(GetThemeColor(BarThemeColorEnum::Divider));divider_->button.lightMaterial=surface_.lightMaterial;
	divider_->button.UpInh({left_.val+textLeft+textWidth_.val+10,top_.val+(42.5-(BarButtonOneSideDip-4))/2});
	join(AdvanceBarButtonVisual(*divider_,{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false,!stacked,false}));
	appliedHover_=hoverTarget>=0&&enabled[hoverTarget]?hoverTarget:-1;
	buttonState_=state;if(!anim.animationEnabled)layoutTimeline_.Restart(0);else layoutTimeline_.Advance(dt,speed);
	changed_|=frame_.panel!=next.panel||frame_.scale!=next.scale||frame_.identity!=next.identity;frame_=next;visible_=next.visible;return changed_||active||textTransition_;
}
void BarConstructionPrompt::Draw(BarUIRendering& renderer,ID2D1DeviceContext* dc)
{
	if(!frame_.visible)return;
	const auto pixels=PromptPixels(frame_.visualPanel,frame_.zoom,PromptOutset(frame_.zoom,frame_.scale));
	renderer.PushPanelOpacity(dc,D2D1::RectF(static_cast<float>(pixels.left),static_cast<float>(pixels.top),static_cast<float>(pixels.right),static_cast<float>(pixels.bottom)),frame_.opacity);
	D2D1_MATRIX_3X2_F original;dc->GetTransform(&original);
	const auto center=D2D1::Point2F(static_cast<float>((frame_.panel.left+frame_.panel.right)*frame_.zoom/2),static_cast<float>((frame_.panel.top+frame_.panel.bottom)*frame_.zoom/2));
	dc->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<float>(frame_.scale),static_cast<float>(frame_.scale),center)*original);
	DrawBarBackgroundVisual(renderer,dc,surface_,{surface_.inhX,surface_.inhY});
	dc->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(frame_.panel.left*frame_.zoom),static_cast<float>(frame_.panel.top*frame_.zoom),static_cast<float>(frame_.panel.right*frame_.zoom),static_cast<float>(frame_.panel.bottom*frame_.zoom)),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	renderer.SetResidentContentOpacity(GetThemeResidentContentOpacity());
	renderer.Word(dc,label_,label_.Inherit(BarUiInheritEnum::CenterFromTopLeft,surface_),DWRITE_FONT_WEIGHT_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING);
	if(frame_.ring)
	{
		const auto ringCenter=D2D1::Point2F(static_cast<float>((frame_.panel.left+21)*frame_.zoom),static_cast<float>((frame_.panel.top+21.25)*frame_.zoom));
		const float contentOpacity=static_cast<float>(GetThemeResidentContentOpacity());
		renderer.DrawProgressRing(dc,ringCenter,static_cast<float>(10.75*frame_.zoom),static_cast<float>(2.5*frame_.zoom),static_cast<float>(ringProgress_.val),GetThemeColor(BarThemeColorEnum::SubtleFill),GetThemeColor(BarThemeColorEnum::Accent),contentOpacity,contentOpacity,true);
		renderer.Word(dc,ringLabel_,ringLabel_.Inherit(BarUiInheritEnum::CenterFromTopLeft,surface_),DWRITE_FONT_WEIGHT_BOLD);
	}
	for(auto& b:buttons_)if(!b->hide)DrawBarButtonVisual(renderer,dc,*b,{b->button.inhX,b->button.inhY});
	if(!divider_->hide)
	{
		const double opacity=divider_->button.pct.val,light=divider_->button.frameLightPct->val;
		divider_->button.pct.SetDirect(opacity*GetThemeResidentContentOpacity());divider_->button.frameLightPct->SetDirect(light*GetThemeResidentContentOpacity());
		DrawBarButtonVisual(renderer,dc,*divider_,{divider_->button.inhX,divider_->button.inhY});
		divider_->button.pct.SetDirect(opacity);divider_->button.frameLightPct->SetDirect(light);
	}
	renderer.SetResidentContentOpacity(1);dc->PopAxisAlignedClip();dc->SetTransform(original);dc->PopLayer();
}
void BarConstructionPrompt::CommitPresented(){std::scoped_lock lock(mutex_);presented_=frame_;}
RECT BarConstructionPrompt::Bounds()const{return frame_.visible?PromptPixels(frame_.visualPanel,frame_.zoom,PromptOutset(frame_.zoom,frame_.scale)):RECT{};}
std::array<RECT,1> BarConstructionPrompt::PresentedRegions()const{std::scoped_lock lock(mutex_);return {presented_.visible?PromptPixels(presented_.visualPanel,presented_.zoom):RECT{}};}
bool BarConstructionPrompt::Pointer(BarUISetClass& owner,const ExMessage& message,bool cancelled)
{
	Frame f;{std::scoped_lock lock(mutex_);f=presented_;}if(!f.visible)return false;
	const int previousHover=hovered_,previousPress=pressed_;
	if(cancelled){hovered_=pressed_=-1;if(previousHover>=0||previousPress>=0)owner.UpdateRendering(false);return previousHover>=0||previousPress>=0;}
	const double px=message.x/f.zoom,py=message.y/f.zoom;
	const bool inside=px>=f.visualPanel.left&&px<f.visualPanel.right&&py>=f.visualPanel.top&&py<f.visualPanel.bottom;
	int hit=-1;for(int i=0;i<4;++i)if(f.input&&HitTestBarButton(f.buttons[i],px,py)){hit=i;break;}
	if(message.message==WM_LBUTTONDOWN){if(!inside)return false;pressed_=hit;pressedIdentity_=f.identity;hovered_=focused_=-1;}
	else if(message.message==WM_MOUSEMOVE){if(pressed_>=0&&pressed_!=hit)pressed_=-1;hovered_=hit;}
	else if(message.message==WM_LBUTTONUP)
	{
		const int pressed=pressed_;pressed_=hovered_=-1;
		if(pressed>=0&&pressed==hit&&pressedIdentity_==f.identity){ShapeUiAction action;action.identity=pressedIdentity_;action.kind=PromptActions[hit];(void)SubmitShapeUiIntent(action);}
	}
	if(inside||previousHover!=hovered_||previousPress!=pressed_)owner.UpdateRendering(false);
	return inside||previousPress>=0;
}
bool BarConstructionPrompt::WantsKeyboard()const
{
	// 键盘准入与实际处理共同读取成功呈现状态，候选帧不能提前截获 PPT 按键。
	std::scoped_lock lock(mutex_);return presented_.visible;
}
bool BarConstructionPrompt::Keyboard(BarUISetClass& owner,const KeyboardMessage& message)
{
	const BYTE key=message.key;const bool down=message.down;
	Frame f;{std::scoped_lock lock(mutex_);f=presented_;}if(!f.visible)return false;
	if(key==VK_ESCAPE){if(down&&f.buttons[3].enabled){ShapeUiAction a;a.identity=f.identity;a.kind=ShapeUiActionKind::ConstructionCancel;(void)SubmitShapeUiIntent(a);}return true;}
	if(key==VK_TAB||key==VK_RIGHT||key==VK_LEFT)
	{
		if(down)
		{
			const int delta=key==VK_LEFT||(key==VK_TAB&&message.shift)?-1:1;
			int candidate=focused_<0?(delta>0?3:0):static_cast<int>(std::find(PromptOrder.begin(),PromptOrder.end(),focused_)-PromptOrder.begin());focused_=-1;
			for(int k=0;k<4;++k){candidate=(candidate+4+delta)%4;const int item=PromptOrder[candidate];if(f.buttons[item].visible&&f.buttons[item].enabled){focused_=item;break;}}
			owner.UpdateRendering(false);
		}return true;
	}
	if(key==VK_RETURN||key==VK_SPACE)
	{
		const int focused=focused_;
		if(down&&focused>=0&&f.buttons[focused].visible&&f.buttons[focused].enabled){ShapeUiAction a;a.identity=f.identity;a.kind=PromptActions[focused];(void)SubmitShapeUiIntent(a);}return true;
	}return false;
}
