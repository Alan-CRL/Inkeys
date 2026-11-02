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
		bool complete=false,expanded=false;
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

bool BarStatusIndicatorPanel::Advance(BarUISetClass& owner,double dt,double speed,double zoom,bool drawing)
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
			if(c.expanded)model_.ClickExpanded(c.key,now);
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
	model_.SetMainButtonWidth(owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]->w.val);
	changed_|=model_.SetDrawingActive(drawing);
	changed_|=model_.Advance(now,dt,speed,BarUiAnimationEnabled);
	feedbackActive_=false;
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

void BarStatusIndicatorPanel::Layout(const StatusIndicatorLayoutInput& input,double zoom)
{
	const auto sameRect=[](const StatusIndicatorRect& a,const StatusIndicatorRect& b)
	{return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;};
	changed_|=input_.mainCenterX!=input.mainCenterX||input_.mainCenterY!=input.mainCenterY
		||input_.mainWidth!=input.mainWidth||input_.mainHeight!=input.mainHeight||zoom_!=zoom
		||!sameRect(input_.baseBounds,input.baseBounds)||!sameRect(input_.work,input.work);
	input_=input;zoom_=zoom;layout_=ResolveStatusIndicatorLayout(model_,input);
}
RECT BarStatusIndicatorPanel::Bounds() const
{
	RECT result=BoundsPixels(UnionStatusIndicatorRect(layout_.normalBounds,layout_.overflowBounds),zoom_);
	if(layout_.ring.opacity>0||layout_.progress.opacity>0)
		Union(result,BoundsPixels({input_.mainCenterX-input_.mainWidth/2,input_.mainCenterY-input_.mainHeight/2,
			input_.mainCenterX+input_.mainWidth/2,input_.mainCenterY+input_.mainHeight/2},zoom_,12*zoom_+3));
	return result;
}
RECT BarStatusIndicatorPanel::OverflowBounds() const
{
	if(!model_.FanOpen()&&model_.FanProgress()<=0)return {};
	// 从展开首帧即预留完整Back峰值与选中胶囊，避免边播放边扩窗裁切。
	const std::size_t count=model_.DockedCount();StatusIndicatorRect reserve;
	for(std::size_t i=0;i<count;++i)
	{
		const auto place=ResolveStatusIndicatorFanPlacement(i,count,input_);
		const double dockX=input_.mainCenterX+input_.mainWidth/2-StatusIndicatorStyle::DockedCornerInset;
		const double dockY=input_.mainCenterY-input_.mainHeight/2+StatusIndicatorStyle::DockedCornerInset;
		const double padX=StatusIndicatorStyle::FanCircle/2+std::abs(place.x-dockX)*.08+3;
		const double padY=StatusIndicatorStyle::FanCircle/2+std::abs(place.y-dockY)*.08+3;
		const double text=StatusIndicatorCapsuleMaxWidth(input_.mainWidth);
		reserve=UnionStatusIndicatorRect(reserve,{place.x-padX-text,place.y-padY,place.x+padX,place.y+padY});
	}
	if(!input_.work.Empty())
	{
		reserve.left=(std::max)(reserve.left,input_.work.left);reserve.top=(std::max)(reserve.top,input_.work.top);
		reserve.right=(std::min)(reserve.right,input_.work.right);reserve.bottom=(std::min)(reserve.bottom,input_.work.bottom);
	}
	return BoundsPixels(UnionStatusIndicatorRect(reserve,layout_.overflowBounds),zoom_,3);
}
void BarStatusIndicatorPanel::CommitPresented()
{
	Presented next;next.zoom=zoom_;next.entries.reserve(layout_.entries.size());
	for(const auto& item:layout_.entries)
	{
		const auto* entry=model_.Find(item.key);
		if(!entry||!entry->visible)continue;
		// 退场/收起尾段仅参与绘制；业务角色取模型阶段，不能从当前胶囊外形猜测。
		if(entry->phase==StatusIndicatorPhase::Expanded||entry->phase==StatusIndicatorPhase::Docked)
			next.entries.push_back({item,entry->phase==StatusIndicatorPhase::Expanded});
	}
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
	std::uint64_t hit=0;bool expanded=false;
	for(;;)
	{
		// 与主栏的成功呈现事务共用serial，避免将旧条目几何与新抓手映射拼成命中。
		const auto before=owner.BottomDockPresentedSnapshot();
		Presented presented;{std::lock_guard lock(presentedMutex_);presented=presented_;}
		const auto point=ResolveBarBottomDockGripHitTestPointFromRigid(message.x,message.y,
			before.horizontalMapping,before.mapping,before.zoom);
		const double x=point.logicalX/presented.zoom,y=point.logicalY/presented.zoom;
		hit=0;expanded=false;
		for(auto it=presented.entries.rbegin();it!=presented.entries.rend();++it)
		{
			if(it->geometry.opacity<.05)continue;
			const auto& r=it->geometry.bounds;const double radius=(std::min)(r.Width(),r.Height())/2;
			const double centerX=std::clamp(x,r.left+radius,r.right-radius),centerY=(r.top+r.bottom)/2;
			if(x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom
				&&(x-centerX)*(x-centerX)+(y-centerY)*(y-centerY)<=radius*radius)
			{hit=it->geometry.key;expanded=it->expanded;break;}
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
	if(result.previousHover!=result.hover||result.previousPressed!=result.pressed)
	{ Command c;c.kind=CommandKind::Feedback;c.hover=result.hover;c.pressed=result.pressed;Queue(std::move(c)); }
	if(event==ButtonPointerEvent::Down&&!hit)
	{ Command c;c.kind=CommandKind::Outside;Queue(std::move(c)); }
	if(result.activate)
	{
		Command c;c.kind=CommandKind::Click;c.key=result.activate;c.expanded=expanded;Queue(std::move(c));
		if(accepted)*accepted=true;
	}
	if(contactPointer&&event==ButtonPointerEvent::Up)ResetPointerFeedback();
	return hit||result.previousPressed||result.pressed;
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
	const auto color=[&](COLORREF c,double opacity)
	{brush_->SetColor(D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f,static_cast<FLOAT>(std::clamp(opacity,0.0,1.0))));};
	const double material=BarThemeMaterial::ClampWeight(lightMaterial);
	const COLORREF background=BarThemeMaterial::MixColor(RGB(24,24,24),RGB(255,255,255),material);
	const COLORREF foreground=BarThemeMaterial::MixColor(RGB(255,255,255),RGB(0,0,0),material);
	const double contentOpacity=GetThemeResidentContentOpacity();
	const double surfaceOpacity=BarThemeMaterial::ResidentBaseFillOpacity*BarThemeMaterial::ResidentFillOpacityScale(
		material,BarThemeMaterial::LightResidentSurfaceOpacityScale,GetThemeResidentBackgroundOpacityOverride());
	const double frameOpacity=BarThemeMaterial::FrameOpacity(material,1);
	// 环的路径独立缓存，不干扰主按钮单条超椭圆缓存。
	if(layout_.ring.opacity>0||layout_.progress.opacity>0)
	{
		const double width=main.w.val*zoom_,height=main.h.val*zoom_,n=main.n->val;
		const std::array<double,3> key{width,height,n};
		ComPtr<ID2D1Factory> factory;context->GetFactory(&factory);
		if(!ringGeometry_||ringKey_!=key)
		{
			ringGeometry_.Reset();const auto created=Inkeys::UI::CreateSuperellipsePath(factory.Get(),static_cast<FLOAT>(width),static_cast<FLOAT>(height),static_cast<FLOAT>(n),128,&ringGeometry_);
			if(FAILED(created))ringGeometry_.Reset();ringKey_=key;
		}
		if(ringGeometry_)
		{
			const FLOAT left=static_cast<FLOAT>((main.x.val-main.w.val/2)*zoom_),top=static_cast<FLOAT>((main.y.val-main.h.val/2)*zoom_);
			ComPtr<ID2D1TransformedGeometry> positioned;
			factory->CreateTransformedGeometry(ringGeometry_.Get(),D2D1::Matrix3x2F::Translation(left,top),&positioned);
			if(positioned&&layout_.ring.opacity>0&&layout_.progress.opacity<=0)
			{
				const COLORREF accent=Accent(layout_.ring.argb);const double opacity=layout_.ring.opacity*frameOpacity;
				const auto theme=BarThemeMaterial::Resolve(material,accent,accent);
				if(!renderer.DrawPointLightFrame(context,accent,theme,BarUiFrameLightColorEnum::Frame,true,1,1,opacity,opacity,
						static_cast<FLOAT>(layout_.ring.width*zoom_),nullptr,positioned.Get(),static_cast<int>(lround(n*4))))
				{color(accent,opacity);context->DrawGeometry(positioned.Get(),brush_.Get(),static_cast<FLOAT>(layout_.ring.width*zoom_));}
			}
			if(layout_.progress.opacity>0&&layout_.progress.value>0)
			{
				FLOAT length=0;
				if(SUCCEEDED(ringGeometry_->ComputeLength(nullptr,&length)))
				{
					ComPtr<ID2D1PathGeometry> progress;ComPtr<ID2D1GeometrySink> sink;
					if(SUCCEEDED(factory->CreatePathGeometry(&progress))&&SUCCEEDED(progress->Open(&sink)))
					{
						const int segments=(std::max)(1,static_cast<int>(ceil(192*layout_.progress.value)));
						for(int i=0;i<=segments;++i)
						{
							D2D1_POINT_2F point{};ringGeometry_->ComputePointAtLength(static_cast<FLOAT>(length*layout_.progress.value*i/segments),nullptr,&point,nullptr);
							point.x+=left;point.y+=top;if(i==0)sink->BeginFigure(point,D2D1_FIGURE_BEGIN_HOLLOW);else sink->AddLine(point);
						}
						sink->EndFigure(D2D1_FIGURE_END_OPEN);
						if(SUCCEEDED(sink->Close()))
						{
							const COLORREF accent=Accent(layout_.progress.argb);
							const double opacity=layout_.progress.opacity*frameOpacity;
							const auto theme=BarThemeMaterial::Resolve(material,accent,accent);
							// 进度段也有彩色漫反射；分段长度纳入缓存身份，不能复用完整边框或旧进度的遮罩。
							const int variant=0x40000000|(static_cast<int>(lround(n*4))&255)
								|(static_cast<int>(lround(layout_.progress.value*1024))<<8);
							if(!renderer.DrawPointLightFrame(context,accent,theme,BarUiFrameLightColorEnum::Frame,true,1,1,
								opacity,opacity,static_cast<FLOAT>(layout_.progress.width*zoom_),nullptr,progress.Get(),variant))
							{
								color(accent,opacity);context->DrawGeometry(progress.Get(),brush_.Get(),static_cast<FLOAT>(layout_.progress.width*zoom_));
							}
						}
					}
				}
			}
		}
	}
	std::uint64_t newestDocked=0,newestDockSerial=0;
	for(const auto& e:model_.Entries())
		if(e.visible&&e.phase==StatusIndicatorPhase::Docked&&e.dockSerial>=newestDockSerial)
		{newestDocked=e.spec.key;newestDockSerial=e.dockSerial;}
	for(const auto& item:layout_.entries)
	{
		const auto* entry=model_.Find(item.key);if(!entry)continue;
		auto original=D2D1::Matrix3x2F::Identity();context->GetTransform(&original);
		const auto bounds=Pixels(item.bounds,zoom_);const FLOAT centerX=(bounds.left+bounds.right)/2,centerY=(bounds.top+bounds.bottom)/2;
		const auto press=press_.find(item.key);const double scale=press==press_.end()?1.0:static_cast<double>(press->second.val);
		context->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<FLOAT>(scale),static_cast<FLOAT>(scale),D2D1::Point2F(centerX,centerY))*original);
		const COLORREF accent=Accent(item.accentArgb);const float radius=(bounds.bottom-bounds.top)/2;
		if(item.shape==StatusIndicatorShape::Capsule)
		{
			color(background,item.opacity*surfaceOpacity);context->FillRoundedRectangle(D2D1::RoundedRect(bounds,radius,radius),brush_.Get());
			color(accent,item.opacity*frameOpacity);context->DrawRoundedRectangle(D2D1::RoundedRect(bounds,radius,radius),brush_.Get(),static_cast<FLOAT>(zoom_));
		}
		const auto circle=D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(item.iconCenterX*zoom_),static_cast<FLOAT>(item.iconCenterY*zoom_)),static_cast<FLOAT>(item.iconRadius*zoom_),static_cast<FLOAT>(item.iconRadius*zoom_));
		color(accent,item.opacity*surfaceOpacity);context->FillEllipse(circle,brush_.Get());
		const auto hover=hover_.find(item.key);const double hoverOpacity=hover==hover_.end()?0.0:static_cast<double>(hover->second.pct.val);
		if(hoverOpacity>0){color(foreground,item.opacity*hoverOpacity*2.5);context->DrawEllipse(circle,brush_.Get(),static_cast<FLOAT>(1.5*zoom_));}
		auto& icon=icons_[static_cast<std::size_t>(item.icon)];const double iconSize=item.iconRadius*2*StatusIndicatorStyle::CapsuleIcon/StatusIndicatorStyle::CapsuleCircle;
		icon.w.SetDirect(iconSize);icon.h.SetDirect(iconSize);icon.color1->SetDirect(foreground);
		// 后排胶囊只露强调色；文字和图标均不穿过前排可读区域。
		const double frontness=entry->phase==StatusIndicatorPhase::Expanded?StatusIndicatorCurve::Clamp01(1-entry->stack.position):1;
		// 半透明堆叠不能靠上层填充遮住旧图标；Fan展开后再逐渐显示后排图标。
		const double dockIcon=(entry->phase==StatusIndicatorPhase::Docked&&entry->spec.key!=newestDocked)
			?StatusIndicatorCurve::InOutSine(model_.FanProgress()):1.0;
		icon.pct.SetDirect(item.opacity*frontness*dockIcon);
		BarUIRendering::FontTargetScope fontTarget(renderer,press==press_.end()?1.0:static_cast<double>(press->second.tar));
		renderer.SetResidentContentOpacity(contentOpacity);
		renderer.Svg(context,icon,BarUiInheritClass(item.iconCenterX-iconSize/2,item.iconCenterY-iconSize/2));
		if(item.textOpacity>0&&!item.textClip.Empty())
		{
			auto [wordIt,created]=words_.try_emplace(item.key);auto& word=wordIt->second;
			if(created){word.Initialization(0,0,1,StatusIndicatorStyle::CapsuleHeight,entry->spec.text,StatusIndicatorStyle::TextSize,accent);word.enable.Initialization(true);}
			word.SetStringImmediate(entry->spec.text);word.color.SetDirect(accent);word.pct.SetDirect(item.opacity*item.textOpacity);
			word.w.SetDirect((std::max)(entry->textWidth+4.0,item.textClip.Width()));word.h.SetDirect(item.bounds.Height());
			word.size.SetDirect(StatusIndicatorStyle::TextSize*item.scale);word.fontTargetSize=StatusIndicatorStyle::TextSize;
			context->PushAxisAlignedClip(Pixels(item.textClip,zoom_),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			renderer.Word(context,word,BarUiInheritClass(item.textX,item.bounds.top),DWRITE_FONT_WEIGHT_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING);
			context->PopAxisAlignedClip();
		}
		renderer.SetResidentContentOpacity(1);context->SetTransform(original);
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
	Advance(owner,1,1,1,false);
	StatusIndicatorLayoutInput input;input.mainCenterX=root->x.val;input.mainCenterY=root->y.val;
	input.mainWidth=root->w.val;input.mainHeight=root->h.val;input.baseBounds={0,0,800,800};input.work=input.baseBounds;
	const auto layout=[&](){Layout(input,1);};layout();
	check(model_.Count()==2&&model_.FrontKey()==first.key);
	check(model_.Find(first.key)&&model_.Find(first.key)->textWidth>0);
	check(!StatusIndicatorTextOverflow(model_.Find(first.key)->textWidth,input.mainWidth));
	auto front=layout_.entries.back();ExMessage message{};message.x=static_cast<short>((front.bounds.left+front.bounds.right)/2);message.y=static_cast<short>((front.bounds.top+front.bounds.bottom)/2);
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
	check(!click(front,true));Advance(owner,1,1,1,false);layout();CommitPresented();
	check(model_.Find(first.key)->phase==StatusIndicatorPhase::Expanded);
	check(click(layout_.entries.back()));Advance(owner,1,1,1,false);layout();CommitPresented();
	check(model_.DockedCount()==1&&model_.FrontKey()==second.key);
	check(click(layout_.entries.back()));Advance(owner,1,1,1,false);layout();CommitPresented();
	check(model_.DockedCount()==2&&!model_.FanOpen());
	check(click(layout_.entries.back()));Advance(owner,1,1,1,false);layout();CommitPresented();
	check(model_.FanOpen()&&model_.FanProgress()==1&&!layout_.entries.empty());
	const auto overflow=OverflowBounds();check(!IsRectEmpty(&overflow));
	check(click(layout_.entries.back()));Advance(owner,1,1,1,false);layout();CommitPresented();
	check(model_.Selected()!=0);
	{std::lock_guard lock(presentedMutex_);check(!presented_.entries.empty()&&!presented_.entries.back().expanded);}

	PublishStatusIndicatorProgress(first.key,.6f,false);Advance(owner,1,1,1,false);layout();
	check(std::abs(model_.Progress().display-.6)<.001&&layout_.progress.opacity>0);
	Advance(owner,1,1,1,true);layout();check(layout_.progress.opacity==0&&layout_.ring.opacity==0);
	Advance(owner,1,1,1,false);layout();CommitPresented();
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
		check(icons_[static_cast<std::size_t>(StatusIndicatorIcon::Slideshow)].pct.val==0);
		check(icons_[static_cast<std::size_t>(StatusIndicatorIcon::NewVersion)].pct.val>0);
		model_.ClickDocked(second.key,Now());model_.Advance(Now(),1,1,false);
		model_.ClickDocked(second.key,Now());model_.Advance(Now(),1,1,false);layout();draw();
		check(std::all_of(icons_.begin(),icons_.begin()+3,[](auto& icon){return !icon.svg.GetVal().empty();}));
		check(icons_[static_cast<std::size_t>(StatusIndicatorIcon::Slideshow)].cacheBitmap!=nullptr);
		check(icons_[static_cast<std::size_t>(StatusIndicatorIcon::Slideshow)].pct.val>0);
	}
	model_.Retire(first.key,Now());CommitPresented();
	{std::lock_guard lock(presentedMutex_);check(std::none_of(presented_.entries.begin(),presented_.entries.end(),[&](const auto& hit){return hit.geometry.key==first.key;}));}
	// 清理测试发布，后续离屏用例沿用原始主栏状态和动画偏好。
	model_=StatusIndicatorModel{};layout_={};simulated_.clear();hover_.clear();press_.clear();words_.clear();pointer_.Reset();
	{std::lock_guard lock(presentedMutex_);presented_={};}
	{std::lock_guard lock(commandsMutex);commands.clear();}
	countSnapshot.store(0);debugCountSnapshot.store(0);deadlineTicks_.store(0);
	BarUiAnimationEnabled=previousAnimation;
	return failures;
}
