module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
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

namespace
{
	D2D1_RECT_F PixelRect(EraserAttributeRect r, double zoom)
	{ return D2D1::RectF(static_cast<float>(r.left*zoom),static_cast<float>(r.top*zoom),
		static_cast<float>(r.right*zoom),static_cast<float>(r.bottom*zoom)); }
	RECT IntegerRect(EraserAttributeRect r,double zoom,int outset=0)
	{
		if(r.Width()<=0 || r.Height()<=0)return {};
		return {static_cast<LONG>(floor(r.left*zoom))-outset,static_cast<LONG>(floor(r.top*zoom))-outset,
			static_cast<LONG>(ceil(r.right*zoom))+outset,static_cast<LONG>(ceil(r.bottom*zoom))+outset};
	}
	void Place(BarUiShapeClass& shape,EraserAttributeRect r)
	{
		shape.x.SetDirect(r.left);shape.y.SetDirect(r.top);
		shape.w.SetDirect(r.Width());shape.h.SetDirect(r.Height());
		shape.UpInh(BarUiInheritClass(r.left,r.top));
	}
	D2D1_COLOR_F ThemeBrushColor(BarThemeColorEnum role,float opacity=1)
	{
		const auto c=GetThemeColor(role);
		return D2D1::ColorF(GetRValue(c)/255.0f,GetGValue(c)/255.0f,GetBValue(c)/255.0f,opacity);
	}
	void PushOpacity(ID2D1DeviceContext* context,float opacity)
	{
		context->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),nullptr,
			D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,D2D1::IdentityMatrix(),opacity),nullptr);
	}
}

void BarEraserAttributePanel::Initialize()
{
	if(initialized_)return;
	for(auto* shape:{&surface_,&menu_,&tooltip_})
	{
		shape->Initialization(0,0,0,0,BarMainBarCornerRadiusDip,BarMainBarCornerRadiusDip,
			BarButtonFrameThicknessDip,GetThemeColor(BarThemeColorEnum::Surface),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		shape->enable.Initialization(true);shape->pct.SetDirect(BarDrawAttributeSurfaceOpacity);
		shape->framePct.emplace(BarMainBarFrameOpacity);
		shape->frameLightPct.emplace(BarMainBarFrameOpacity);
		shape->frameRendering=BarUiFrameRenderingEnum::PointLight;
		shape->frameLightColor=BarUiFrameLightColorEnum::PenWhenDrawing;
	}
	const wchar_t* names[]={L"清空画布",L"",L"",L"",L"自动粗细",L"",L"低",L"中",L"高",L"自动粗细设置"};
	const auto metrics=ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
	for(size_t i=0;i<buttons_.size();++i)
	{
		auto& b=buttons_[i]; b.hide=false;b.size=BarButtonSizeEnum::twoTwo;
		b.button.Initialization(0,0,0,0,BarButtonCornerRadiusDip,BarButtonCornerRadiusDip,
			BarButtonFrameThicknessDip,GetThemeColor(BarThemeColorEnum::PressedFill),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		b.button.enable.Initialization(true);b.button.pct.SetDirect(0);
		b.button.framePct.emplace(0);b.button.frameLightPct.emplace(0);
		b.button.frameRendering=BarUiFrameRenderingEnum::PointLight;
		b.button.frameLightColor=BarUiFrameLightColorEnum::PenWhenDrawing;
		b.button.framePrimaryLightEnabled=false;b.button.frameCursorLightIntensityScale=BarButtonCursorLightIntensity;
		if(i==4 || i==5){b.button.rw->SetDirect(0);b.button.rh->SetDirect(0);}
		const bool large=i==0 || i==4;
		b.name.Initialization(0,large?metrics.primaryOffsetYDip:0,
			large?metrics.primarySlotWidthDip:BarButtonTwoSideDip,metrics.primarySlotHeightDip,
			names[i],metrics.primaryFontSizeDip,GetThemeColor(BarThemeColorEnum::TextPrimary));
		b.name.enable.Initialization(names[i][0]!=0);b.name.pct.SetDirect(1);
		b.icon.Initialization(0,large?metrics.iconOffsetYDip:0,GetThemeColor(BarThemeColorEnum::TextPrimary),std::nullopt);
		b.icon.enable.Initialization(large || i==9);b.icon.pct.SetDirect(1);
		if(large || i==9)b.icon.InitializationFromResource(L"UI",i==0?L"barClean":i==4?L"barEraser":L"barSetting");
		b.icon.SetWH(large?metrics.iconSizeDip:20,large?metrics.iconSizeDip:20);
		b.icon.w.SetDirect(b.icon.w.tar);b.icon.h.SetDirect(b.icon.h.tar);
		if(i==9){b.icon.x.SetDirect(-BarButtonTwoSideDip);b.name.x.SetDirect(12);b.name.w.SetDirect(BarButtonTwoSideDip*2);}
	}
	for(auto& d:dividers_)
	{
		d.Initialization(0,0,1,1,BarUiDividerRadius,BarUiDividerRadius,BarButtonFrameThicknessDip,
			GetThemeColor(BarThemeColorEnum::SurfaceFrame),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		d.enable.Initialization(true);d.pct.SetDirect(0.30);d.framePct.emplace(0);d.frameLightPct.emplace(1);
		d.frameRendering=BarUiFrameRenderingEnum::PointLight;d.framePrimaryLightEnabled=false;
		d.frameCursorLightIntensityScale=BarUiDividerCursorLightIntensity;
	}
	title_.Initialization(0,0,0,BarButtonOneSideDip,L"灵敏度",BarButtonTwoTwoLabelFontSizeDip,GetThemeColor(BarThemeColorEnum::TextPrimary));
	title_.enable.Initialization(true);title_.pct.SetDirect(1);
	tooltipText_.Initialization(0,0,0,0,L"",BarButtonTwoTwoLabelFontSizeDip,GetThemeColor(BarThemeColorEnum::TextPrimary));
	tooltipText_.enable.Initialization(true);tooltipText_.pct.SetDirect(1);
	initialized_=true;
}

void BarEraserAttributePanel::ConfigureSurface(BarUiShapeClass& surface,EraserAttributeRect rect)
{
	Place(surface,rect);
	surface.fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));
	surface.frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
}

bool BarEraserAttributePanel::Advance(BarUISetClass& owner,double dt,double speed,double zoom,UINT dpi,
	RECT workArea,POINT origin,double rigidX,double rigidY)
{
	Initialize();changed_=false;active_=false;
	if(owner.barState.fold || stateMode.StateModeSelect!=StateModeSelectEnum::IdtEraser ||
		owner.barState.drawAttribute || owner.barState.geometryAttribute || owner.barState.moreExpanded) Close(owner);
	const bool open=owner.barState.eraserAttribute;
	if(!open)owner.barState.eraserSensitivityOpen=false;
	visible_=open;
	const BarUiAnimationAdvanceContextClass context{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
	auto advance=[&](auto& value)
	{
		const auto result=BarUiAdvanceAnimation(value,context);
		changed_|=result.changed;active_|=result.active;
	};
	const BarUiCurveSpecClass fade{BarUiCurveEnum::EaseOutSine,BarUiCurveEnum::EaseOutSine,0,false};
	const bool targetBelow=owner.barState.widgetPosition.primaryBar;
	const bool targetReversed=!owner.barState.widgetPosition.mainBar;
	const bool switching=targetBelow!=previousBelow_ || targetReversed!=previousReversed_;
	// 组倒转在全透明的中点换向，沿用UI3淡出/淡入节奏，避免文字与圆瞬间换位。
	progress_.SetTar(open && !switching?1:0,switching?static_cast<double>(BarUiDefaultOperationDur)/2:static_cast<double>(BarUiDefaultOperationDur),std::nullopt,false,fade);
	menuProgress_.SetTar(owner.barState.eraserSensitivityOpen?1:0,BarUiDefaultOperationDur,std::nullopt,false,fade);
	advance(progress_);advance(menuProgress_);
	const bool menuOpen=owner.barState.eraserSensitivityOpen;
	if(!menuOpen)menuSide_=-1;
	if(switching && progress_.val==0)
	{
		previousBelow_=targetBelow;previousReversed_=targetReversed;menuSide_=-1;
		if(open){progress_.SetTar(1,static_cast<double>(BarUiDefaultOperationDur)/2,std::nullopt,false,fade);active_=true;}
	}
	const bool below=previousBelow_,reversed=previousReversed_;
	auto root=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	root->UpInh(BarUiInheritClass(root->x.val-root->w.val/2,root->y.val-root->h.val/2));
	auto main=owner.shapeMap[BarUISetShapeEnum::MainBar];main->Inherit(BarUiInheritEnum::Center,*root);
	auto anchor=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
	anchor->button.Inherit(BarUiInheritEnum::CenterFromTopLeft,*main);
	EraserAttributeLayoutInput input;
	input.main={main->inhX,main->inhY,main->inhX+main->w.val,main->inhY+main->h.val};
	input.anchor={anchor->button.inhX,anchor->button.inhY,
		anchor->button.inhX+anchor->button.w.val,anchor->button.inhY+anchor->button.h.val};
	input.zoom=zoom;input.dpiScale=dpi/96.0;input.below=below;input.reversed=reversed;input.lockedMenuSide=menuSide_;
	input.work={(workArea.left-origin.x)/zoom-rigidX,(workArea.top-origin.y)/zoom-rigidY,
		(workArea.right-origin.x)/zoom-rigidX,(workArea.bottom-origin.y)/zoom-rigidY};
	auto next=ResolveEraserAttributeLayout(input);
	if(menuOpen && menuSide_<0)menuSide_=next.menuBelow?1:0;
	// 换边沿位置过渡；开合只淡入和靠近主栏，不给真实圆施加缩放。
	const double offset=next.panel.top-input.main.top;
	if(progress_.val==0)sideOffset_.SetDirect(offset);
	else sideOffset_.SetTar(offset,BarUiDefaultOperationDur);
	advance(sideOffset_);
	const double shift=sideOffset_.val-offset+(next.below?-1:1)*BarMainButtonToMainBarGapDip*(1-progress_.val);
	const double boundedShift=(std::clamp)(shift,input.work.top-next.panel.top,input.work.bottom-next.panel.bottom);
	next.panel=OffsetEraserRect(next.panel,0,boundedShift);next.previewRegion=OffsetEraserRect(next.previewRegion,0,boundedShift);
	for(size_t i=0;i<6;++i)next.items[i]=OffsetEraserRect(next.items[i],0,boundedShift);
	for(size_t i=0;i<3;++i)next.dividers[i]=OffsetEraserRect(next.dividers[i],0,boundedShift);
	changed_|=layout_!=next || zoom_!=zoom;layout_=next;zoom_=zoom;
	const auto preferences=EraserPreferencesSnapshot();
	const auto automatic=GetAutomaticState(preferences);
	const bool clear=Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent;
	changed_|=clear!=clearEnabled_ || automatic!=automatic_ || selectedSize_!=static_cast<int>(preferences.baseSize) || sensitivity_!=static_cast<int>(preferences.sensitivity);
	clearEnabled_=clear;automatic_=automatic;selectedSize_=static_cast<int>(preferences.baseSize);sensitivity_=static_cast<int>(preferences.sensitivity);
	changed_|=surface_.fill->val!=GetThemeColor(BarThemeColorEnum::Surface);
	ConfigureSurface(surface_,layout_.panel);ConfigureSurface(menu_,layout_.menu);
	for(size_t i=0;i<buttons_.size();++i)
	{
		auto& b=buttons_[i];const bool enabled=i!=9 && (i!=0 || clearEnabled_);
		const bool selected=(i>=1 && i<=3 && selectedSize_==(16<<(i-1))) ||
			(i==4 && automatic_==AutomaticState::On) || (i==5 && menuOpen) ||
			(i>=6 && i<=8 && sensitivity_==static_cast<int>(i-6));
		b.state->state=!enabled?BarWidgetState::Disable:selected?BarWidgetState::Selected:BarWidgetState::None;
		SetBarButtonPressedVisual(b,enabled && pressed_==static_cast<int>(i));
		UpdateBarButtonHoverVisual(b,i<6?open:menuOpen,enabled && !selected,BarButtonHoverFadeDurationSeconds);
		RetargetBarButtonInteractionVisual(b,i<6?open:menuOpen,enabled,selected,BarButtonHoverTransitionDuration);
		Place(b.button,layout_.items[i]);
		advance(b.button.pct);advance(*b.button.fill);advance(*b.button.frameLightPct);
		advance(b.icon.pct);advance(b.name.pct);advance(*b.icon.color1);advance(b.name.color);advance(b.pressScale);
		if(i>=1 && i<=3)
		{
			// 槽位的选中标记独立于真实橡皮，按下反馈只作用于外围背景。
			selection_[i-1].SetTar(selected?1:0,BarButtonHoverTransitionDuration);advance(selection_[i-1]);
		}
	}
	for(size_t i=0;i<dividers_.size();++i)
	{
		Place(dividers_[i],layout_.dividers[i]);
		dividers_[i].fill->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		dividers_[i].frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
	}
	title_.w.SetDirect(layout_.menu.Width());title_.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
	const int hint=focused_>=0?focused_.load():hovered_.load();
	wstring text;
	if(hint>=1 && hint<=3)
	{
		const wchar_t* sizes[]={L"小号 · 16 DIP",L"中号 · 32 DIP",L"大号 · 64 DIP"};
		text=wstring(sizes[hint-1])+L"\n自动模式时作为基础大小";
	}
	else if(hint==9)text=L"自动粗细设置 · 暂未开放";
	else if(hint==4 && automatic_==AutomaticState::Mixed)text=L"各设备设置不同\n点击统一开启自动粗细";
	const bool hintVisible=open && !text.empty() && (hint<6 || menuOpen);
	tooltipProgress_.SetTar(hintVisible?1:0,BarButtonHoverTransitionDuration);advance(tooltipProgress_);
	if(hintVisible)
	{
		if(tooltipText_.content.GetVal()!=text){tooltipText_.content.Initialization(text);changed_=true;}
		const auto ref=hint>=6?layout_.menu:layout_.panel;
		const double w=(std::min)(input.work.Width(),BarButtonTwoSideDip*4),h=BarButtonOneSideDip*2;
		const double x=(std::clamp)(layout_.items[hint].left,input.work.left,(std::max)(input.work.left,input.work.right-w));
		double y=layout_.below?ref.bottom+BarButtonGapDip:ref.top-BarButtonGapDip-h;
		if(y+h>input.work.bottom)y=ref.top-BarButtonGapDip-h;
		else if(y<input.work.top)y=ref.bottom+BarButtonGapDip;
		y=(std::clamp)(y,input.work.top,(std::max)(input.work.top,input.work.bottom-h));
		EraserAttributeRect r{x,y,x+w,y+h};changed_|=tooltipRect_!=r;tooltipRect_=r;
	}
	ConfigureSurface(tooltip_,tooltipRect_);tooltipText_.w.SetDirect(tooltipRect_.Width());tooltipText_.h.SetDirect(tooltipRect_.Height());
	tooltipText_.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
	return changed_ || active_;
}

void BarEraserAttributePanel::DrawPreview(ID2D1DeviceContext* context,size_t index)
{
	const auto slot=layout_.items[index+1];
	const auto clip=IntersectEraserRect({slot.left,layout_.panel.top,slot.right,layout_.panel.bottom},layout_.previewRegion);
	context->PushAxisAlignedClip(PixelRect(clip,zoom_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	const float d=static_cast<float>(layout_.previewDiameters[index]*zoom_);
	const auto center=D2D1::Point2F(static_cast<float>((slot.left+slot.right)*0.5*zoom_),
		static_cast<float>((layout_.panel.top+layout_.panel.bottom)*0.5*zoom_));
	PushOpacity(context,ERASER_GRIP_OPACITY);
	brush_->SetColor(D2D1::ColorF(ERASER_GRIP_OUTLINE_CHANNEL,ERASER_GRIP_OUTLINE_CHANNEL,ERASER_GRIP_OUTLINE_CHANNEL,1));
	context->FillEllipse(D2D1::Ellipse(center,d/2,d/2),brush_.Get());
	const float inner=d*(0.5f-ERASER_GRIP_OUTLINE_RATIO);
	brush_->SetColor(D2D1::ColorF(1,1,1,1));context->FillEllipse(D2D1::Ellipse(center,inner,inner),brush_.Get());
	brush_->SetColor(D2D1::ColorF(ERASER_GRIP_OUTLINE_CHANNEL,ERASER_GRIP_OUTLINE_CHANNEL,ERASER_GRIP_OUTLINE_CHANNEL,1));
	for(float sign:{-1.0f,1.0f})
	{
		const float x=center.x+sign*d*ERASER_GRIP_STRIPE_OFFSET_RATIO;
		const float r=d*ERASER_GRIP_STRIPE_RADIUS_RATIO,h=d*ERASER_GRIP_STRIPE_HALF_HEIGHT_RATIO;
		context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x-r,center.y-h,x+r,center.y+h),r,r),brush_.Get());
	}
	context->PopLayer();context->PopAxisAlignedClip();
	const float selection=static_cast<float>(selection_[index].val);
	if(selection>0)
	{
		brush_->SetColor(ThemeBrushColor(BarThemeColorEnum::Accent,selection));
		const float width=static_cast<float>((std::min)(BarButtonOneSideDip/2,slot.Width())*zoom_);
		const float h=static_cast<float>(BarButtonGapDip*0.6*zoom_);
		const float bottom=static_cast<float>((layout_.panel.bottom-BarButtonGapDip)*zoom_);
		context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(center.x-width/2,bottom-h,center.x+width/2,bottom),h/2,h/2),brush_.Get());
	}
}

void BarEraserAttributePanel::Draw(BarUIRendering& renderer,ID2D1DeviceContext* context)
{
	if(!initialized_ || progress_.val<=0)return;
	if(deviceGeneration_!=renderer.GetDeviceGeneration()){brush_.Reset();splitClip_.Reset();for(auto& b:buttons_)b.icon.ResetCache();deviceGeneration_=renderer.GetDeviceGeneration();}
	if(!brush_ && FAILED(context->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&brush_)))return;
	auto drawSurface=[&](BarUiShapeClass& s)
	{ DrawBarBackgroundVisual(renderer,context,s,BarUiInheritClass(s.inhX,s.inhY)); };
	auto drawButton=[&](size_t i)
	{
		auto& b=buttons_[i];
		DrawBarButtonVisual(renderer,context,b,BarUiInheritClass(layout_.items[i].left,layout_.items[i].top));
	};
	PushOpacity(context,static_cast<float>(progress_.val));drawSurface(surface_);
	context->PushAxisAlignedClip(PixelRect(layout_.panel,zoom_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	for(size_t i=0;i<4;++i)drawButton(i);
	const auto body=PixelRect(layout_.items[4],zoom_),extra=PixelRect(layout_.items[5],zoom_);
	const auto split=D2D1::RectF((std::min)(body.left,extra.left),body.top,(std::max)(body.right,extra.right),body.bottom);
	const D2D1_SIZE_F clipSize{split.right-split.left,split.bottom-split.top};
	if(!splitClip_ || splitClipSize_.width!=clipSize.width || splitClipSize_.height!=clipSize.height)
	{
		ComPtr<ID2D1Factory> factory;context->GetFactory(&factory);splitClip_.Reset();
		const float radius=static_cast<float>(BarButtonCornerRadiusDip*zoom_);
		factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0,0,clipSize.width,clipSize.height),radius,radius),&splitClip_);
		splitClipSize_=clipSize;
	}
	context->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),splitClip_.Get(),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
		D2D1::Matrix3x2F::Translation(split.left,split.top)),nullptr);
	drawButton(4);drawButton(5);context->PopLayer();
	for(size_t i=0;i<3;++i){renderer.Shape(context,dividers_[i],BarUiInheritClass(dividers_[i].inhX,dividers_[i].inhY));DrawPreview(context,i);}
	const auto extension=PixelRect(layout_.items[5],zoom_);
	const float cx=(extension.left+extension.right)/2,cy=(extension.top+extension.bottom)/2;
	const float half=static_cast<float>(BarButtonGapDip*0.7*zoom_);
	const float direction=menuSide_==1?1.0f:-1.0f;
	brush_->SetColor(ThemeBrushColor(BarThemeColorEnum::TextPrimary));
	context->DrawLine({cx-half,cy-direction*half/2},{cx,cy+direction*half/2},brush_.Get(),static_cast<float>(BarButtonFrameThicknessDip*zoom_));
	context->DrawLine({cx,cy+direction*half/2},{cx+half,cy-direction*half/2},brush_.Get(),static_cast<float>(BarButtonFrameThicknessDip*zoom_));
	if(automatic_==AutomaticState::Mixed)
	{
		const auto r=PixelRect(layout_.items[4],zoom_);
		brush_->SetColor(ThemeBrushColor(BarThemeColorEnum::Accent));
		const float x=(r.left+r.right)/2,y=static_cast<float>((layout_.panel.bottom-BarButtonGapDip*2)*zoom_);
		context->DrawLine({x-half,y},{x+half,y},brush_.Get(),static_cast<float>(BarButtonFrameThicknessDip*2*zoom_));
	}
	const int focus=focused_;
	if(focus>=0 && focus<6)
	{
		brush_->SetColor(ThemeBrushColor(BarThemeColorEnum::TextPrimary));
		auto r=PixelRect(layout_.items[focus],zoom_);r.left+=2;r.right-=2;r.top+=2;r.bottom-=2;
		context->DrawRoundedRectangle(D2D1::RoundedRect(r,4,4),brush_.Get());
	}
	context->PopAxisAlignedClip();context->PopLayer();
	if(menuProgress_.val>0)
	{
		PushOpacity(context,static_cast<float>(progress_.val*menuProgress_.val));drawSurface(menu_);
		renderer.Word(context,title_,BarUiInheritClass(layout_.menu.left,layout_.menu.top));
		for(size_t i=6;i<10;++i)drawButton(i);
		renderer.Shape(context,dividers_[3],BarUiInheritClass(dividers_[3].inhX,dividers_[3].inhY));
		if(focus>=6 && focus<9)
		{
			brush_->SetColor(ThemeBrushColor(BarThemeColorEnum::TextPrimary));
			context->DrawRoundedRectangle(D2D1::RoundedRect(PixelRect(layout_.items[focus],zoom_),4,4),brush_.Get());
		}
		context->PopLayer();
	}
	if(tooltipProgress_.val>0)
	{
		PushOpacity(context,static_cast<float>(progress_.val*tooltipProgress_.val));drawSurface(tooltip_);
		renderer.Word(context,tooltipText_,BarUiInheritClass(tooltipRect_.left,tooltipRect_.top),DWRITE_FONT_WEIGHT_NORMAL);
		context->PopLayer();
	}
}

RECT BarEraserAttributePanel::Bounds() const
{
	RECT result{};
	if(progress_.val<=0)return result;
	const int padding=static_cast<int>(ceil((BarButtonFrameThicknessDip+BarRenderingAttribute::pointLightDiffuseExtraWidth)*zoom_))+BarRenderingAttribute::dirtyAntialiasPadding;
	result=IntegerRect(layout_.panel,zoom_,padding);
	if(menuProgress_.val>0)BarRenderingAttribute::UnionRectInPlace(result,IntegerRect(layout_.menu,zoom_,padding));
	if(tooltipProgress_.val>0)BarRenderingAttribute::UnionRectInPlace(result,IntegerRect(tooltipRect_,zoom_,padding));
	return result;
}
void BarEraserAttributePanel::CommitPresented()
{
	std::scoped_lock lock(presentationMutex_);
	presented_=layout_;presentedZoom_=zoom_;presentedPanelVisible_=progress_.val>0.001;
	presentedMenuVisible_=presentedPanelVisible_ && menuProgress_.val>0.001;
	presentedTooltip_=tooltipProgress_.val>0.001 && presentedPanelVisible_?tooltipRect_:EraserAttributeRect{};
}
std::array<RECT,3> BarEraserAttributePanel::PresentedRegions() const
{
	std::scoped_lock lock(presentationMutex_);
	return {presentedPanelVisible_?IntegerRect(presented_.panel,presentedZoom_):RECT{},
		presentedMenuVisible_?IntegerRect(presented_.menu,presentedZoom_):RECT{},IntegerRect(presentedTooltip_,presentedZoom_)};
}
void BarEraserAttributePanel::Close(BarUISetClass& owner)
{
	owner.barState.eraserAttribute=false;owner.barState.eraserSensitivityOpen=false;
	pressed_=-1;hovered_=-1;focused_=-1;visible_=false;
}
void BarEraserAttributePanel::Execute(BarUISetClass& owner,int item)
{
	if(item==0)
	{
		if(!Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent)return;
		const auto accepted=Inkeys::Drawing::Draw3::PublishProductCommand(Inkeys::Drawing::Draw3::Bridge::CommandType::Clear);
		if(accepted==Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted)Close(owner);
	}
	else if(item>=1 && item<=3)SetGlobalEraserPreference(16<<(item-1));
	else if(item==4)SetGlobalEraserPreference(-1,-1,GetAutomaticState(EraserPreferencesSnapshot())!=AutomaticState::On);
	else if(item==5)
	{
		if(owner.TryBeginToggle(BarToggleChannel::EraserSensitivity))owner.barState.eraserSensitivityOpen=!owner.barState.eraserSensitivityOpen;
	}
	else if(item>=6 && item<=8)SetGlobalEraserPreference(-1,item-6);
	owner.UpdateRendering(false);
}
bool BarEraserAttributePanel::Pointer(BarUISetClass& owner,const ExMessage& message,bool cancelled)
{
	EraserAttributeLayout geometry;EraserAttributeRect hint;double zoom;bool panel,menu;
	{std::scoped_lock lock(presentationMutex_);geometry=presented_;hint=presentedTooltip_;zoom=presentedZoom_;panel=presentedPanelVisible_;menu=presentedMenuVisible_;}
	if(!panel && pressed_<0)return false;
	const double x=message.x/zoom,y=message.y/zoom;
	auto contains=[&](EraserAttributeRect r)
	{
		return r.Width()>0 && r.Height()>0 && BarUiRoundedRectContainsPoint(message.x,message.y,zoom,
			r.left,r.top,r.Width(),r.Height(),BarMainBarCornerRadiusDip,BarMainBarCornerRadiusDip);
	};
	const bool overMenu=menu && contains(geometry.menu);
	const bool inside=overMenu || (panel && contains(geometry.panel)) || contains(hint);
	int hit=-1;
	if(inside)
	{
		const auto clip=overMenu?geometry.menu:geometry.panel;
		for(int i=overMenu?6:0;i<(overMenu?10:6);++i)
			if(IntersectEraserRect(geometry.items[i],clip).Contains(x,y)){hit=i;break;}
	}
	if(message.message==WM_MOUSEMOVE)
	{
		if(hovered_!=hit)
		{
			const int old=hovered_;hovered_=hit;focused_=-1;
			if(old>=0)StopBarButtonHoverVisual(buttons_[old],false);
			if(hit>=0 && hit!=9 && (hit!=0 || Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent))
				StartBarButtonHoverVisual(buttons_[hit]);
			owner.UpdateRendering(false);
		}
		if(pressed_>=0 && cancelled){pressed_=-1;owner.UpdateRendering(false);}
		return inside;
	}
	const bool down=message.message==WM_LBUTTONDOWN || message.message==WM_LBUTTONDBLCLK;
	if(down)
	{
		// 子菜单外先关闭，再最多执行当前命中的一个控件；扩展箭头自己负责开合。
		if(owner.barState.eraserSensitivityOpen && !overMenu && hit!=5)
		{owner.barState.eraserSensitivityOpen=false;owner.UpdateRendering(false);}
		const bool enabled=hit!=9 && (hit!=0 || Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent);
		pressed_=inside && enabled && owner.barState.eraserAttribute && !cancelled?hit:-1;
		focused_=-1;owner.UpdateRendering(false);return inside;
	}
	if(message.message==WM_LBUTTONUP)
	{
		const int pressed=pressed_;pressed_=-1;
		if(!cancelled && inside && hit==pressed && pressed>=0 && owner.barState.eraserAttribute)
			Execute(owner,pressed);
		if(pressed>=0)owner.UpdateRendering(false);
		return inside || pressed>=0;
	}
	return inside;
}
bool BarEraserAttributePanel::Keyboard(BarUISetClass& owner,BYTE key,bool down)
{
	if(!owner.barState.eraserAttribute)return false;
	if(key==VK_ESCAPE)
	{
		if(down){if(owner.barState.eraserSensitivityOpen)owner.barState.eraserSensitivityOpen=false;else Close(owner);owner.UpdateRendering(false);}
		return true;
	}
	if(key!=VK_TAB && key!=VK_LEFT && key!=VK_RIGHT && key!=VK_RETURN && key!=VK_SPACE)return false;
	if(!down)return true;
	if(key==VK_RETURN || key==VK_SPACE){if(focused_>=0)Execute(owner,focused_);return true;}
	const int first=owner.barState.eraserSensitivityOpen?6:0,last=owner.barState.eraserSensitivityOpen?8:5;
	const int delta=key==VK_LEFT || (key==VK_TAB && (GetKeyState(VK_SHIFT)&0x8000))?-1:1;
	int focus=focused_;if(focus<first || focus>last)focus=first;else focus=first+(focus-first+delta+last-first+1)%(last-first+1);
	if(focus==0 && !Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent)focus=delta>0?1:5;
	focused_=focus;hovered_=-1;owner.UpdateRendering(false);return true;
}
