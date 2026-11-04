module;
#include "../../../IdtMain.h"
#include "Bar.StatusIndicator.h"
#include "Bar.InteractionWorker.h"
#include "Bar.BottomDock.h"
#include <cstdio>
#include "Bar.ThemeMaterial.h"
#include "../Superellipse.Geometry.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <random>

module Inkeys.UI.Bar;
import :Main;
import :Theme;
import Inkeys.UI.RenderPipeline;

using namespace Inkeys::UI::Bar;
using Microsoft::WRL::ComPtr;
namespace
{
	enum class CommandKind { Publish,Retire,Progress,Options,Add,Remove,Click,Outside,Feedback };
	struct Command
	{
		CommandKind kind=CommandKind::Publish;
		StatusIndicatorSpec spec;
		StatusIndicatorOptions options;
		std::uint64_t key=0,hover=0,pressed=0;
		double progress=-1;
		bool complete=false;
		StatusIndicatorRole role=StatusIndicatorRole::None;
	};
	std::mutex commandsMutex;
	std::deque<Command> commands;
	std::atomic_size_t countSnapshot{0},debugCountSnapshot{0};
	double Now() noexcept
	{ return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	void Queue(Command command)
	{
		{ std::lock_guard lock(commandsMutex);commands.push_back(std::move(command)); }
		barUISet.UpdateRendering(false);
	}
	COLORREF Accent(std::uint32_t argb) noexcept
	{ return RGB((argb>>16)&255,(argb>>8)&255,argb&255); }
	D2D1_RECT_F Pixels(StatusIndicatorRect r,double zoom)
	{ return D2D1::RectF(static_cast<FLOAT>(r.left*zoom),static_cast<FLOAT>(r.top*zoom),static_cast<FLOAT>(r.right*zoom),static_cast<FLOAT>(r.bottom*zoom)); }
	RECT BoundsPixels(StatusIndicatorRect r,double zoom,double outset=2)
	{
		if(r.Empty())return {};
		return {static_cast<LONG>(floor(r.left*zoom-outset)),static_cast<LONG>(floor(r.top*zoom-outset)),
			static_cast<LONG>(ceil(r.right*zoom+outset)),static_cast<LONG>(ceil(r.bottom*zoom+outset))};
	}
	void Union(RECT& target,RECT add)
	{
		if(IsRectEmpty(&add))return;
		if(IsRectEmpty(&target)){target=add;return;}
		target.left=(std::min)(target.left,add.left);target.top=(std::min)(target.top,add.top);
		target.right=(std::max)(target.right,add.right);target.bottom=(std::max)(target.bottom,add.bottom);
	}
	// 光晕、点光漫反射与描边抗锯齿的外扩（DIP）。
	double GlowOutsetDip() noexcept { return BarRenderingAttribute::pointLightDiffuseExtraWidth+2; }
	bool HitRoundedRect(const StatusIndicatorRect& r,double radius,double x,double y,double pad) noexcept
	{
		const StatusIndicatorRect hit=OutsetStatusIndicatorRect(r,pad);
		if(x<hit.left||x>hit.right||y<hit.top||y>hit.bottom)return false;
		radius=(std::min)({radius+pad,hit.Width()/2,hit.Height()/2});
		const double cx=std::clamp(x,hit.left+radius,hit.right-radius),cy=std::clamp(y,hit.top+radius,hit.bottom-radius);
		return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius;
	}
}
namespace Inkeys::UI::Bar
{
	void PublishStatusIndicator(const StatusIndicatorSpec& spec) { Command c;c.spec=spec;Queue(std::move(c)); }
	void RetireStatusIndicator(std::uint64_t key) { Command c;c.kind=CommandKind::Retire;c.key=key;Queue(std::move(c)); }
	void PublishStatusIndicatorProgress(std::uint64_t key,float progress,bool complete)
	{ Command c;c.kind=CommandKind::Progress;c.key=key;c.progress=progress;c.complete=complete;Queue(std::move(c)); }
	void SetStatusIndicatorOptions(bool enable,bool ppt,bool superTop,bool newVersion)
	{ Command c;c.kind=CommandKind::Options;c.options={enable,ppt,superTop,newVersion};Queue(std::move(c)); }
	void DebugAddRandomStatusIndicator() { Command c;c.kind=CommandKind::Add;Queue(std::move(c)); }
	void DebugRemoveStatusIndicator() { Command c;c.kind=CommandKind::Remove;Queue(std::move(c)); }
	std::size_t StatusIndicatorCount() noexcept { return countSnapshot.load(std::memory_order_acquire); }
	std::size_t DebugStatusIndicatorCount() noexcept { return debugCountSnapshot.load(std::memory_order_acquire); }
}

bool BarStatusIndicatorPanel::Advance(BarUISetClass& owner,double dt,double speed,double zoom,bool drawing,bool dragLocked)
{
	const double now=Now();zoom_=zoom;changed_=false;
	std::deque<Command> pending;
	{ std::lock_guard lock(commandsMutex);pending.swap(commands); }
	for(auto& c:pending)
	{
		changed_=true;
		switch(c.kind)
		{
		case CommandKind::Publish:model_.Publish(c.spec,now);break;
		case CommandKind::Retire:model_.Retire(c.key,now);break;
		case CommandKind::Progress:model_.SetProgress(c.key,c.progress,c.complete,now);break;
		case CommandKind::Options:model_.SetOptions(c.options,now);break;
		case CommandKind::Feedback:hovered_=c.hover;pressed_=c.pressed;break;
		case CommandKind::Outside:model_.ClickOutside(now);break;
		case CommandKind::Click:
			if(c.role==StatusIndicatorRole::Capsule)model_.ClickExpanded(c.key,now);
			else (void)model_.ClickDocked(c.key,now); // 正式动作在后续状态源阶段接入。
			break;
		case CommandKind::Add:
			{
				if(model_.Count()>=StatusIndicatorStyle::Capacity)break;
				static std::mt19937 random{std::random_device{}()};static std::uint64_t serial=0;
				const auto bits=random();StatusIndicatorSpec spec;spec.key=0xDB00000000000000ULL|++serial;
				spec.kind=StatusIndicatorKind::Debug;spec.lifetime=bits%4?StatusIndicatorLifetime::Persistent:StatusIndicatorLifetime::Notification;
				spec.icon=static_cast<StatusIndicatorIcon>((bits>>4)%7);
				static constexpr std::array<std::uint32_t,8> colors{0xFFC43E1C,0xFFE8352B,0xFF1F4FB8,0xFF2EA043,0xFF9B59B6,0xFFE09D12,0xFF008FAD,0xFFCC4780};
				spec.accentArgb=colors[(bits>>8)%colors.size()];
				static constexpr std::array<const wchar_t*,6> texts{L"OK",L"放映模式",L"超级置顶",L"Download",L"这是一条用于检查三秒内完整滚动的超长状态提示",L"A very long status message to exercise scrolling"};
				spec.text=texts[(bits>>12)%texts.size()];
				if(model_.Publish(spec,now)==StatusIndicatorPublishResult::Added
					&&spec.lifetime==StatusIndicatorLifetime::Persistent&&(bits>>16)%3==0)
					simulated_.emplace_back(spec.key,now);
			}
			break;
		case CommandKind::Remove:
			if(const auto key=model_.NewestKey(StatusIndicatorKind::Debug,StatusIndicatorLifetime::Persistent))model_.Retire(*key,now);
			break;
		}
	}
	// 测量与正式绘制共用同一字体、粗体和DIP；模型无需依赖DWrite。
	for(const auto& entry:model_.Entries())
		model_.SetTextWidth(entry.spec.key,owner.spec.MeasureText(entry.spec.text,StatusIndicatorStyle::TextSize,DWRITE_FONT_WEIGHT_BOLD).width);
	if(!simulated_.empty()&&now>=simulatedSampleAt_)
	{
		simulatedSampleAt_=now+.1;
		const auto [key,start]=simulated_.back();
		const double progress=std::clamp((now-start)/6.0,0.0,1.0);
		changed_|=model_.SetProgress(key,progress,progress>=1,now);
		std::erase_if(simulated_,[&](const auto& p){return now>=p.second+6;});
	}
	std::erase_if(simulated_,[&](const auto& p){return !model_.Find(p.first);});
	changed_|=model_.SetDrawingActive(drawing);
	changed_|=model_.Advance(now,dt,speed,BarUiAnimationEnabled);
	// 倒转跟随主栏所在半屏；拖动中锁存，松手后锚点以弹簧换边。
	if(!dragLocked)belowTarget_=static_cast<bool>(owner.barState.widgetPosition.primaryBar);
	if(!orientationInitialized_){orientation_.Snap(belowTarget_?1.0:0.0);orientationInitialized_=true;changed_=true;}
	const bool orientationChanged=orientation_.Advance(belowTarget_?1.0:0.0,dt*speed,BarUiAnimationEnabled,StatusIndicatorSprings::Orientation);
	changed_|=orientationChanged;
	feedbackActive_=orientationChanged;
	for(const auto& entry:model_.Entries())
	{
		auto [h,inserted]=hover_.try_emplace(entry.spec.key);
		auto [p,insertedPress]=press_.try_emplace(entry.spec.key,1.0);
		auto& hover=h->second;
		const bool over=hovered_==entry.spec.key,pressed=pressed_==entry.spec.key;
		if(hover.entered!=over)
		{
			if(over)StartBarButtonHoverVisual(&hover.pct,&hover.fill,&hover.stage);
			else StopBarButtonHoverVisual(&hover.pct,&hover.fill,&hover.stage,pressed,false);
			hover.entered=over;changed_=true;
		}
		if(hover.stage!=BarButtonHoverStageEnum::None)changed_|=UpdateBarButtonHoverVisual(hover.pct,&hover.fill,hover.stage,true,!pressed,BarButtonHoverFadeDurationSeconds);
		p->second.SetTar(pressed?BarButtonPressScale:1,BarUiDefaultOperationDur,std::nullopt,false,pressed?BarButtonPressCurve():BarButtonReleaseCurve());
		const BarUiAnimationAdvanceContextClass context{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
		const auto hAdvance=BarUiAdvanceAnimation(hover.pct,context),pAdvance=BarUiAdvanceAnimation(p->second,context);
		const auto fillAdvance=BarUiAdvanceAnimation(hover.fill,context);
		changed_|=hAdvance.changed||pAdvance.changed||fillAdvance.changed;
		feedbackActive_|=hAdvance.active||pAdvance.active||fillAdvance.active;
	}
	std::erase_if(hover_,[&](const auto& p){return !model_.Find(p.first);});
	std::erase_if(press_,[&](const auto& p){return !model_.Find(p.first);});
	std::erase_if(words_,[&](const auto& p){return !model_.Find(p.first);});
	countSnapshot.store(model_.Count(),std::memory_order_release);
	std::size_t debug=0;
	for(const auto& e:model_.Entries())if(e.spec.kind==StatusIndicatorKind::Debug&&e.spec.lifetime==StatusIndicatorLifetime::Persistent&&!e.Leaving())++debug;
	debugCountSnapshot.store(debug,std::memory_order_release);
	double deadline=model_.WakeDeadline();
	if(!simulated_.empty())deadline=(std::min)(deadline,simulatedSampleAt_);
	const auto next=std::isfinite(deadline)?std::chrono::steady_clock::time_point{
		std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(deadline))}.time_since_epoch().count():0;
	if(deadlineTicks_.exchange(next,std::memory_order_acq_rel)!=next)WakeInteractionWorker();
	return changed_||Active();
}

void BarStatusIndicatorPanel::Layout(StatusIndicatorLayoutInput input,double zoom)
{
	input.below=orientation_.position;
	const auto sameRect=[](const StatusIndicatorRect& a,const StatusIndicatorRect& b)
	{return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;};
	changed_|=input_.mainCenterX!=input.mainCenterX||input_.mainCenterY!=input.mainCenterY
		||input_.mainWidth!=input.mainWidth||input_.mainHeight!=input.mainHeight||input_.mainN!=input.mainN
		||input_.below!=input.below||zoom_!=zoom
		||!sameRect(input_.baseBounds,input.baseBounds)||!sameRect(input_.work,input.work);
	input_=input;zoom_=zoom;layout_=ResolveStatusIndicatorLayout(model_,input);
}
RECT BarStatusIndicatorPanel::Bounds() const
{
	const double outset=GlowOutsetDip()*zoom_;
	RECT result=BoundsPixels(UnionStatusIndicatorRect(layout_.normalBounds,layout_.overflowBounds),zoom_,outset);
	if(layout_.ring.opacity>0||layout_.progress.opacity>0)
		Union(result,BoundsPixels({input_.mainCenterX-input_.mainWidth/2,input_.mainCenterY-input_.mainHeight/2,
			input_.mainCenterX+input_.mainWidth/2,input_.mainCenterY+input_.mainHeight/2},zoom_,outset));
	return result;
}
RECT BarStatusIndicatorPanel::OverflowBounds() const
{
	// 胶囊出现、飞行与同心弧从首帧即预留完整回弹包络，避免边播放边扩窗裁切。
	const auto reserve=UnionStatusIndicatorRect(layout_.reserveBounds,layout_.overflowBounds);
	return BoundsPixels(reserve,zoom_,GlowOutsetDip()*zoom_);
}
void BarStatusIndicatorPanel::CommitPresented()
{
	Presented next;next.zoom=zoom_;next.entries.reserve(layout_.entries.size());
	for(const auto& item:layout_.entries)
		if(item.role!=StatusIndicatorRole::None)next.entries.push_back(item);
	std::lock_guard lock(presentedMutex_);presented_=std::move(next);
}
std::chrono::steady_clock::time_point BarStatusIndicatorPanel::PollDeadline(std::chrono::steady_clock::time_point now)
{
	auto ticks=deadlineTicks_.load(std::memory_order_acquire);
	if(!ticks)return std::chrono::steady_clock::time_point::max();
	const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::duration(ticks)};
	if(now<deadline)return deadline;
	if(deadlineTicks_.compare_exchange_strong(ticks,0,std::memory_order_acq_rel))barUISet.UpdateRendering(false);
	return std::chrono::steady_clock::time_point::max();
}
bool BarStatusIndicatorPanel::ResetPointerFeedback()
{
	{std::lock_guard lock(presentedMutex_);pointer_.Reset();}Command command;command.kind=CommandKind::Feedback;Queue(std::move(command));return false;
}
bool BarStatusIndicatorPanel::Pointer(BarUISetClass& owner,const ExMessage& message,bool cancelled,bool contactPointer,bool* accepted)
{
	if(accepted)*accepted=false;
	std::uint64_t hit=0;StatusIndicatorRole role=StatusIndicatorRole::None;
	for(;;)
	{
		// 与主栏的成功呈现事务共用serial，避免将旧条目几何与新抓手映射拼成命中。
		const auto before=owner.BottomDockPresentedSnapshot();
		Presented presented;{std::lock_guard lock(presentedMutex_);presented=presented_;}
		const auto point=ResolveBarBottomDockGripHitTestPointFromRigid(message.x,message.y,
			before.horizontalMapping,before.mapping,before.zoom);
		const double x=point.logicalX/presented.zoom,y=point.logicalY/presented.zoom;
		hit=0;role=StatusIndicatorRole::None;
		for(auto it=presented.entries.rbegin();it!=presented.entries.rend();++it)
		{
			if(it->opacity<.05)continue;
			// 停靠方块很小，命中区外扩到 DockHitMin，但只在方块自身周围。
			const double pad=it->role==StatusIndicatorRole::Dock
				?(std::max)(0.0,(StatusIndicatorStyle::DockHitMin-it->bounds.Width())/2):0.0;
			if(HitRoundedRect(it->bounds,it->radius,x,y,pad)){hit=it->key;role=it->role;break;}
		}
		if(owner.BottomDockPresentedSnapshot().serial==before.serial)break;
	}
	ButtonPointerEvent event;
	if(cancelled)event=ButtonPointerEvent::Cancel;
	else if(message.message==WM_MOUSEMOVE)event=ButtonPointerEvent::Move;
	else if(message.message==WM_LBUTTONDOWN||message.message==WM_LBUTTONDBLCLK)event=ButtonPointerEvent::Down;
	else if(message.message==WM_LBUTTONUP)event=ButtonPointerEvent::Up;
	else return false;
	ButtonPointerResult result;
	{std::lock_guard lock(presentedMutex_);result=pointer_.Update(event,hit,POINT{message.x,message.y});}
	// 悬停/按压只用于方块；胶囊没有按钮式反馈。
	const auto feedback=[&](std::uint64_t key){ return key&&key==hit&&role==StatusIndicatorRole::Capsule?0:key; };
	if(result.previousHover!=result.hover||result.previousPressed!=result.pressed)
	{ Command c;c.kind=CommandKind::Feedback;c.hover=feedback(result.hover);c.pressed=feedback(result.pressed);Queue(std::move(c)); }
	if(event==ButtonPointerEvent::Down&&!hit)
	{ Command c;c.kind=CommandKind::Outside;Queue(std::move(c)); }
	if(result.activate)
	{
		Command c;c.kind=CommandKind::Click;c.key=result.activate;c.role=result.activate==hit?role:StatusIndicatorRole::Dock;Queue(std::move(c));
		if(accepted)*accepted=true;
	}
	if(contactPointer&&event==ButtonPointerEvent::Up)ResetPointerFeedback();
	return hit||result.previousPressed||result.pressed;
}

void BarStatusIndicatorPanel::DrawStroke(BarUIRendering& renderer,ID2D1DeviceContext* context,ID2D1Geometry* geometry,
	COLORREF color,double opacity,double material,int variant)
{
	if(!geometry||opacity<=0)return;
	const FLOAT width=static_cast<FLOAT>(StatusIndicatorStyle::RingWidth*zoom_);
	// 光晕只在开启光影时出现：整圈预模糊遮罩用强调色实色画刷填充，再叠细线与点光。
	if(BarUiEdgeLightingEnabled)
	{
		D2D1_RECT_F bounds{};
		if(auto* mask=renderer.GetGeometryDiffuseMask(context,geometry,width,variant);mask&&mask->bitmap&&SUCCEEDED(geometry->GetBounds(nullptr,&bounds)))
		{
			const auto destination=D2D1::RectF(bounds.left-mask->padding,bounds.top-mask->padding,bounds.right+mask->padding,bounds.bottom+mask->padding);
			const auto source=D2D1::RectF(0,0,mask->size.width,mask->size.height);
			const double glow=StatusIndicatorCurve::Lerp(.65,.35,material)*opacity;
			brush_->SetColor(D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f,static_cast<FLOAT>(std::clamp(glow,0.0,1.0))));
			const auto antialias=context->GetAntialiasMode();
			context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			context->FillOpacityMask(mask->bitmap.Get(),brush_.Get(),&destination,&source);
			context->SetAntialiasMode(antialias);
		}
	}
	const auto theme=BarThemeMaterial::Resolve(material,color,color);
	if(!renderer.DrawPointLightFrame(context,color,theme,BarUiFrameLightColorEnum::Frame,true,1,-1,opacity,opacity,width,nullptr,geometry,variant))
	{
		brush_->SetColor(D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f,static_cast<FLOAT>(std::clamp(opacity,0.0,1.0))));
		context->DrawGeometry(geometry,brush_.Get(),width,renderer.GetRoundStrokeStyle());
	}
}

void BarStatusIndicatorPanel::DrawRing(BarUIRendering& renderer,ID2D1DeviceContext* context,const BarUiSuperellipseClass& main,double material,double content)
{
	if(layout_.ring.opacity<=0&&layout_.progress.opacity<=0)return;
	const double width=main.w.val*zoom_,height=main.h.val*zoom_,n=main.n->val;
	const std::array<double,3> key{width,height,n};
	ComPtr<ID2D1Factory> factory;context->GetFactory(&factory);
	if(!ringGeometry_||ringKey_!=key)
	{
		ringGeometry_.Reset();
		if(FAILED(Inkeys::UI::CreateSuperellipsePath(factory.Get(),static_cast<FLOAT>(width),static_cast<FLOAT>(height),static_cast<FLOAT>(n),128,&ringGeometry_)))ringGeometry_.Reset();
		ringKey_=key;
	}
	if(!ringGeometry_)return;
	const FLOAT left=static_cast<FLOAT>((main.x.val-main.w.val/2)*zoom_),top=static_cast<FLOAT>((main.y.val-main.h.val/2)*zoom_);
	ComPtr<ID2D1TransformedGeometry> positioned;
	factory->CreateTransformedGeometry(ringGeometry_.Get(),D2D1::Matrix3x2F::Translation(left,top),&positioned);
	const int shapeVariant=static_cast<int>(lround(n*4))&255;
	if(positioned&&layout_.ring.opacity>0&&layout_.progress.opacity<=0)
	{
		const auto accent=Accent(StatusIndicatorColor::Display(layout_.ring.argb,material));
		DrawStroke(renderer,context,positioned.Get(),accent,layout_.ring.opacity*content,material,0x20000000|shapeVariant);
	}
	if(layout_.progress.opacity<=0||layout_.progress.value<=0)return;
	// 进度段：从超椭圆顶部中点顺时针按路径长度截取，下方原边框为轨道。
	FLOAT length=0;
	if(FAILED(ringGeometry_->ComputeLength(nullptr,&length)))return;
	ComPtr<ID2D1PathGeometry> progress;ComPtr<ID2D1GeometrySink> sink;
	if(FAILED(factory->CreatePathGeometry(&progress))||FAILED(progress->Open(&sink)))return;
	const int segments=(std::max)(1,static_cast<int>(ceil(192*layout_.progress.value)));
	for(int i=0;i<=segments;++i)
	{
		D2D1_POINT_2F point{};ringGeometry_->ComputePointAtLength(static_cast<FLOAT>(length*layout_.progress.value*i/segments),nullptr,&point,nullptr);
		point.x+=left;point.y+=top;if(i==0)sink->BeginFigure(point,D2D1_FIGURE_BEGIN_HOLLOW);else sink->AddLine(point);
	}
	sink->EndFigure(D2D1_FIGURE_END_OPEN);
	if(FAILED(sink->Close()))return;
	// 分段长度纳入遮罩缓存身份，不能复用完整边框或旧进度的遮罩。
	const int variant=0x40000000|shapeVariant|(static_cast<int>(lround(layout_.progress.value*1024))<<8);
	DrawStroke(renderer,context,progress.Get(),Accent(StatusIndicatorColor::Display(layout_.progress.argb,material)),
		layout_.progress.opacity*content,material,variant);
}

void BarStatusIndicatorPanel::Draw(BarUIRendering& renderer,ID2D1DeviceContext* context,const BarUiSuperellipseClass& main,double lightMaterial)
{
	if(!context)return;
	if(!initialized_)
	{
		constexpr std::array<const wchar_t*,7> resources{L"BarStatusSlideshow",L"BarStatusPin",L"BarStatusNewVersion",L"BarStatusStar",L"BarStatusBell",L"BarStatusInfo",L"BarStatusBolt"};
		for(std::size_t i=0;i<icons_.size();++i)
		{
			auto& icon=icons_[i];icon.Initialization(0,0,RGB(255,255,255),std::nullopt);
			icon.InitializationFromResource(L"UI",resources[i]);icon.enable.Initialization(true);icon.pct.SetDirect(1);
		}
		initialized_=true;
	}
	if(deviceGeneration_!=renderer.GetDeviceGeneration())
	{
		brush_.Reset();ringGeometry_.Reset();for(auto& icon:icons_)icon.ResetCache();deviceGeneration_=renderer.GetDeviceGeneration();
	}
	if(!brush_&&FAILED(context->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&brush_)))return;
	const double material=BarThemeMaterial::ClampWeight(lightMaterial);
	// 强调色边框/图标/文字均属常驻内容：跟随用户内容透明度与 PPT 闲置淡化。
	const double residentContent=GetThemeResidentContentOpacity();
	const double content=BarThemeMaterial::ResolveIdleOpacity(residentContent,BarThemeMaterial::IdleContentOpacity,renderer.residentIdleFade);
	DrawRing(renderer,context,main,material,content);
	if(layout_.entries.empty())return;

	ComPtr<ID2D1Factory> factory;context->GetFactory(&factory);
	const auto rounded=[&](const StatusIndicatorEntryLayout& item)
	{
		const auto r=Pixels(item.bounds,zoom_);const FLOAT radius=static_cast<FLOAT>(item.radius*zoom_);
		return D2D1::RoundedRect(r,radius,radius);
	};
	// 上层外形逐个遮挡下层：下层只在外露部分完整绘制（含图标/文字），不靠半透明叠加或省略绘制。
	std::vector<ComPtr<ID2D1RoundedRectangleGeometry>> shapes(layout_.entries.size());
	for(std::size_t i=0;i<layout_.entries.size();++i)factory->CreateRoundedRectangleGeometry(rounded(layout_.entries[i]),&shapes[i]);
	const FLOAT glow=static_cast<FLOAT>(GlowOutsetDip()*zoom_);
	const COLORREF surface=GetThemeColor(BarThemeModeEnum::Dark,BarThemeColorEnum::Surface);
	for(std::size_t i=0;i<layout_.entries.size();++i)
	{
		const auto& item=layout_.entries[i];
		const auto* entry=model_.Find(item.key);if(!entry)continue;
		const auto rr=rounded(item);
		const auto area=D2D1::RectF(rr.rect.left-glow,rr.rect.top-glow,rr.rect.right+glow,rr.rect.bottom+glow);
		ComPtr<ID2D1Geometry> mask;
		for(std::size_t j=i+1;j<layout_.entries.size();++j)
		{
			if(!shapes[j]||layout_.entries[j].opacity<.02||!OutsetStatusIndicatorRect(item.bounds,GlowOutsetDip()).Intersects(layout_.entries[j].bounds))continue;
			if(!mask)
			{
				ComPtr<ID2D1RectangleGeometry> full;factory->CreateRectangleGeometry(area,&full);mask=full;
			}
			ComPtr<ID2D1PathGeometry> next;ComPtr<ID2D1GeometrySink> sink;
			if(FAILED(factory->CreatePathGeometry(&next))||FAILED(next->Open(&sink)))continue;
			if(SUCCEEDED(mask->CombineWithGeometry(shapes[j].Get(),D2D1_COMBINE_MODE_EXCLUDE,nullptr,sink.Get()))&&SUCCEEDED(sink->Close()))mask=next;
		}
		const bool layered=mask||item.opacity<.999;
		// 整项透明度单次合成：停靠组 50%、出现/退场渐隐不会让边框、图标、文字分别叠加。
		if(layered)context->PushLayer(D2D1::LayerParameters1(area,mask.Get(),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
			D2D1::IdentityMatrix(),static_cast<FLOAT>(std::clamp(item.opacity,0.0,1.0)),nullptr,D2D1_LAYER_OPTIONS1_NONE),nullptr);
		D2D1_MATRIX_3X2_F original{};context->GetTransform(&original);
		const auto press=press_.find(item.key);const double scale=press==press_.end()?1.0:static_cast<double>(press->second.val);
		const D2D1_POINT_2F center{(rr.rect.left+rr.rect.right)/2,(rr.rect.top+rr.rect.bottom)/2};
		if(scale!=1)context->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<FLOAT>(scale),static_cast<FLOAT>(scale),center)*original);

		// 表面：主栏同源材质（背景透明度、闲置淡化、浅色缩放由常驻表面规则统一处理）。
		BarUiShapeClass shape(0,0,item.bounds.Width(),item.bounds.Height(),item.radius,item.radius,std::nullopt,surface,std::nullopt);
		shape.enable.Initialization(true);shape.pct.Initialization(BarMainBarFillOpacity);
		shape.themeSurface=true;shape.residentSurface=true;shape.lightMaterial=material;
		shape.lightFillOpacityScale=BarThemeMaterial::LightResidentSurfaceOpacityScale;
		renderer.Shape(context,shape,BarUiInheritClass(item.bounds.left,item.bounds.top),nullptr,false);
		// 悬停/按压同主栏按钮：表面内高亮填充，不用描边或光影。
		if(const auto hover=hover_.find(item.key);hover!=hover_.end()&&hover->second.pct.val>0)
		{
			const COLORREF fill=static_cast<COLORREF>(hover->second.fill.val);
			brush_->SetColor(D2D1::ColorF(GetRValue(fill)/255.f,GetGValue(fill)/255.f,GetBValue(fill)/255.f,static_cast<FLOAT>(std::clamp(static_cast<double>(hover->second.pct.val),0.0,1.0))));
			context->FillRoundedRectangle(rr,brush_.Get());
		}
		const auto accent=Accent(StatusIndicatorColor::Display(item.accentArgb,material));
		// 1 DIP 强调色边框，与主栏边框同粗细，点光规则同主栏。
		const auto frameMaterial=BarThemeMaterial::Resolve(material,surface,accent);
		const double frameOpacity=.9*content;
		if(!renderer.DrawPointLightFrame(context,accent,frameMaterial,BarUiFrameLightColorEnum::Frame,true,1,-1,
			frameOpacity,frameOpacity,static_cast<FLOAT>(StatusIndicatorStyle::FrameWidth*zoom_),&rr,nullptr))
		{
			brush_->SetColor(D2D1::ColorF(GetRValue(accent)/255.f,GetGValue(accent)/255.f,GetBValue(accent)/255.f,static_cast<FLOAT>(frameOpacity)));
			context->DrawRoundedRectangle(rr,brush_.Get(),static_cast<FLOAT>(StatusIndicatorStyle::FrameWidth*zoom_));
		}
		auto& icon=icons_[static_cast<std::size_t>(item.icon)];
		icon.w.SetDirect(item.iconSize);icon.h.SetDirect(item.iconSize);icon.color1->SetDirect(accent);icon.pct.SetDirect(1);
		BarUIRendering::FontTargetScope fontTarget(renderer,press==press_.end()?1.0:static_cast<double>(press->second.tar));
		renderer.SetResidentContentOpacity(residentContent);
		renderer.Svg(context,icon,BarUiInheritClass(item.iconCenterX-item.iconSize/2,item.iconCenterY-item.iconSize/2));
		if(item.shape==StatusIndicatorShape::Capsule&&item.textOpacity>.002&&item.textClip.Width()>.5)
		{
			// 文字裁入胶囊内侧圆角，滚动时两端线性渐隐，不留矩形切口。
			const auto clip=Pixels(item.textClip,zoom_);
			const FLOAT inset=static_cast<FLOAT>(zoom_);
			ComPtr<ID2D1RoundedRectangleGeometry> inner;
			factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(rr.rect.left+inset,rr.rect.top+inset,rr.rect.right-inset,rr.rect.bottom-inset),
				(std::max)(0.0F,rr.radiusX-inset),(std::max)(0.0F,rr.radiusY-inset)),&inner);
			ComPtr<ID2D1LinearGradientBrush> fade;
			if(item.fadeLeft||item.fadeRight)
			{
				const FLOAT span=(std::max)(1.0F,clip.right-clip.left),edge=(std::min)(.45F,static_cast<FLOAT>(StatusIndicatorStyle::TextFade*item.textScale*zoom_)/span);
				const std::array<D2D1_GRADIENT_STOP,4> stops{{{0,D2D1::ColorF(0,0,0,item.fadeLeft?0.f:1.f)},{edge,D2D1::ColorF(0,0,0,1)},
					{1-edge,D2D1::ColorF(0,0,0,1)},{1,D2D1::ColorF(0,0,0,item.fadeRight?0.f:1.f)}}};
				ComPtr<ID2D1GradientStopCollection> collection;
				if(SUCCEEDED(context->CreateGradientStopCollection(stops.data(),static_cast<UINT32>(stops.size()),&collection)))
					context->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(clip.left,0),D2D1::Point2F(clip.right,0)),collection.Get(),&fade);
			}
			context->PushLayer(D2D1::LayerParameters1(clip,inner.Get(),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,D2D1::IdentityMatrix(),
				1.0F,fade.Get(),D2D1_LAYER_OPTIONS1_NONE),nullptr);
			auto [wordIt,created]=words_.try_emplace(item.key);auto& word=wordIt->second;
			if(created){word.Initialization(0,0,1,StatusIndicatorStyle::CapsuleHeight,entry->spec.text,StatusIndicatorStyle::TextSize,accent);word.enable.Initialization(true);}
			word.SetStringImmediate(entry->spec.text);word.color.SetDirect(accent);word.pct.SetDirect(item.textOpacity);
			word.w.SetDirect((std::max)(entry->textWidth*item.textScale+4.0,item.textClip.Width()));word.h.SetDirect(item.bounds.Height());
			word.size.SetDirect(StatusIndicatorStyle::TextSize*item.textScale);word.fontTargetSize=StatusIndicatorStyle::TextSize;
			renderer.Word(context,word,BarUiInheritClass(item.textX,item.bounds.top),DWRITE_FONT_WEIGHT_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING);
			context->PopLayer();
		}
		renderer.SetResidentContentOpacity(1);
		context->SetTransform(original);
		if(layered)context->PopLayer();
	}
}


int BarStatusIndicatorPanel::RunOffscreenTest(BarUISetClass& owner)
{
	// 使用真实命令队列、DWrite/D2D与呈现后命中；只替代窗口提交，不创建HWND。
	int failures=0;
	int assertion=0;
	const auto check=[&](bool condition){++assertion;if(!condition){++failures;std::fprintf(stderr,"[StatusIndicator] FAIL assertion %d\n",assertion);}};
	const bool previousAnimation=BarUiAnimationEnabled;BarUiAnimationEnabled=false;
	const auto root=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
	StatusIndicatorSpec first;first.key=0xDA00000000000001ULL;first.text=L"放映模式";first.accentArgb=0xFFC43E1C;first.icon=StatusIndicatorIcon::Slideshow;
	auto second=first;second.key++;second.text=L"A very long status message to exercise scrolling";second.accentArgb=0xFF2EA043;second.icon=StatusIndicatorIcon::NewVersion;
	PublishStatusIndicator(first);PublishStatusIndicator(second);
	Advance(owner,1,1,1,false,false);
	StatusIndicatorLayoutInput input;input.mainCenterX=root->x.val;input.mainCenterY=root->y.val;
	input.mainWidth=root->w.val;input.mainHeight=root->h.val;input.mainN=root->n->val;input.baseBounds={0,0,800,800};input.work=input.baseBounds;
	const auto layout=[&](){Layout(input,1);};layout();
	check(model_.Count()==2&&model_.FrontKey()==first.key);
	check(model_.Find(first.key)&&model_.Find(first.key)->textWidth>0);
	check(StatusIndicatorTextOverflow(model_.Find(first.key)->textWidth)==0);
	auto front=layout_.entries.back();
	check(front.key==first.key&&front.shape==StatusIndicatorShape::Capsule
		&&std::abs((front.bounds.top+front.bounds.bottom)/2-StatusIndicatorEdgeY(input_))<1e-6);
	ExMessage message{};message.x=static_cast<short>((front.bounds.left+front.bounds.right)/2);message.y=static_cast<short>((front.bounds.top+front.bounds.bottom)/2);
	message.message=WM_LBUTTONDOWN;
	check(!Pointer(owner,message,false,false,nullptr)); // 未提交的候选几何不能命中。
	CommitPresented();
	const auto click=[&](const StatusIndicatorEntryLayout& item,bool cancel=false)
	{
		ExMessage event{};event.x=static_cast<short>(item.iconCenterX);event.y=static_cast<short>(item.iconCenterY);
		event.message=WM_LBUTTONDOWN;event.lbutton=true;bool accepted=false;
		check(Pointer(owner,event,false,false,&accepted)&&!accepted);
		event.message=WM_LBUTTONUP;event.lbutton=false;Pointer(owner,event,cancel,false,&accepted);
		return accepted;
	};
	const auto settle=[&](){Advance(owner,1,1,1,false,false);layout();CommitPresented();};
	check(!click(front,true));settle();
	check(model_.Find(first.key)->phase==StatusIndicatorPhase::Expanded);
	check(click(layout_.entries.back()));settle();
	check(model_.DockedCount()==1&&model_.FrontKey()==second.key);
	// 单个方块点击就地重新展开，再点胶囊收回。
	const auto findLayout=[&](std::uint64_t key)->const StatusIndicatorEntryLayout*
	{ for(const auto& item:layout_.entries)if(item.key==key)return &item;return nullptr; };
	{
		model_.ClickDocked(first.key,Now());settle();
		const auto* reopened=findLayout(first.key);
		check(model_.Reexpanded()&&reopened&&reopened->shape==StatusIndicatorShape::Capsule&&reopened->role==StatusIndicatorRole::Capsule);
		if(reopened)check(click(*reopened));settle();
		check(!model_.Reexpanded()&&model_.Find(first.key)->phase==StatusIndicatorPhase::Docked);
	}
	if(const auto* capsule=findLayout(second.key))check(click(*capsule));
	settle();
	check(model_.DockedCount()==2&&!model_.FanOpen());
	check(click(layout_.entries.back()));settle();
	check(model_.FanOpen()&&model_.FanProgress()==1&&!layout_.entries.empty());
	const auto overflow=OverflowBounds();check(!IsRectEmpty(&overflow));
	check(click(layout_.entries.back()));settle();
	check(model_.Selected()!=0);
	{std::lock_guard lock(presentedMutex_);check(!presented_.entries.empty()&&presented_.entries.back().role==StatusIndicatorRole::Fan);}

	PublishStatusIndicatorProgress(first.key,.6f,false);Advance(owner,1,1,1,false,false);layout();
	check(std::abs(model_.Progress().display-.6)<.001&&layout_.progress.opacity>0);
	Advance(owner,1,1,1,true,false);layout();check(layout_.progress.opacity==0&&layout_.ring.opacity==0);
	Advance(owner,1,1,1,false,false);layout();CommitPresented();
	auto& renderer=owner.spec;renderer.SetFrameZoom(1);
	const auto ready=renderer.EnsureDeviceResources(Inkeys::UI::RenderPipeline::GetDeviceEpoch(),800,800);
	check(SUCCEEDED(ready));
	if(SUCCEEDED(ready))
	{
		auto* context=renderer.GetDeviceContext();
		const auto draw=[&]()
		{
			context->BeginDraw();context->SetTransform(D2D1::Matrix3x2F::Identity());context->Clear(D2D1::ColorF(0,0,0,0));
			renderer.Superellipse(context,*root,BarUiInheritClass(root->x.val-root->w.val/2,root->y.val-root->h.val/2));
			Draw(renderer,context,*root,0);
			const auto result=context->EndDraw();check(SUCCEEDED(result));renderer.HandleFrameEndDrawResult(result);
		};
		model_.ClickOutside(Now());model_.Advance(Now(),1,1,false);layout();draw();
		check(std::all_of(icons_.begin(),icons_.begin()+3,[](auto& icon){return !icon.svg.GetVal().empty();}));
		check(icons_[static_cast<std::size_t>(StatusIndicatorIcon::Slideshow)].cacheBitmap!=nullptr);
		check(icons_[static_cast<std::size_t>(StatusIndicatorIcon::NewVersion)].cacheBitmap!=nullptr);
		model_.ClickDocked(second.key,Now());model_.Advance(Now(),1,1,false);
		model_.ClickDocked(second.key,Now());model_.Advance(Now(),1,1,false);layout();draw();
		check(model_.Selected()==second.key&&words_.count(second.key)==1);
	}
	model_.Retire(first.key,Now());layout();CommitPresented();
	{std::lock_guard lock(presentedMutex_);check(std::none_of(presented_.entries.begin(),presented_.entries.end(),[&](const auto& hit){return hit.key==first.key&&hit.role!=StatusIndicatorRole::None;}));}
	// 清理测试发布，后续离屏用例沿用原始主栏状态和动画偏好。
	model_=StatusIndicatorModel{};layout_={};simulated_.clear();hover_.clear();press_.clear();words_.clear();pointer_.Reset();
	{std::lock_guard lock(presentedMutex_);presented_={};}
	{std::lock_guard lock(commandsMutex);commands.clear();}
	countSnapshot.store(0);debugCountSnapshot.store(0);deadlineTicks_.store(0);
	BarUiAnimationEnabled=previousAnimation;
	return failures;
}
