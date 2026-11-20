module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <utility>
#include <windows.h>
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include <wrl/client.h>
#include "../../../PptWaitingDiagnostics.h"
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include "../Whiteboard/Whiteboard.Theme.h"

#include <cstdint>
#include <vector>
#include <chrono>
#include <d2d1_1.h>

module Inkeys.UI.Bar;
import :Scene;
import :Main;
import :Button;
import :State;
import :Theme;
import Inkeys.UI.UI3;
import Inkeys.UI.UI3.Animation;
import Inkeys.UI.Bar.EraserAttributeMotion;
import Inkeys.UI.UI3.FramePacing;
import Inkeys.Other.Config;
import Inkeys.UI.UI3.DirtyRegion;
import Inkeys.UI.UI3.Material;
import Inkeys.UI.UI3.LoadingIndicator;
import Inkeys.UI.UI3.FrameRing;
import Inkeys.UI.UI3.Interaction;

using namespace Inkeys::UI::UI3;

namespace Inkeys::UI::Bar
{
	namespace
	{
		constexpr double DamageOutsetDip = ButtonFrameThicknessDip
			+ RenderingAttribute::pointLightDiffuseExtraWidth
			+ RenderingAttribute::dirtyAntialiasPadding;
		// 大尺寸白板菜单基准（以大尺寸 More 按钮中心 265 对齐）；小尺寸由表面按 More 按钮实际位置平移。
		constexpr EraserAttributeRect LargeMenuBounds{190,-122.5,340,-5};
		constexpr EraserAttributeRect LargeThemeBounds{345,-301,575,-5};
		constexpr EraserAttributeRect LargeThemeAnchor{195,-42.5,335,-10};
		constexpr auto& ThemeColors=WhiteboardVisual::ThemeColors;

		bool Empty(const RECT& r) noexcept { return r.right<=r.left || r.bottom<=r.top; }
		RECT Pixels(EraserAttributeRect r,double scale) noexcept
		{
			return {static_cast<LONG>(std::floor(r.left*scale)),static_cast<LONG>(std::floor(r.top*scale)),
				static_cast<LONG>(std::ceil(r.right*scale)),static_cast<LONG>(std::ceil(r.bottom*scale))};
		}
		EraserAttributeRect Bounds(UiShapeClass& shape) noexcept
		{
			return {shape.x.val-shape.w.val/2,shape.y.val-shape.h.val/2,
				shape.x.val+shape.w.val/2,shape.y.val+shape.h.val/2};
		}
		UiInheritClass Inherit(UiShapeClass& shape) noexcept
		{ const auto b=Bounds(shape); return {b.left,b.top}; }
		void InitializeShape(UiShapeClass& shape,EraserAttributeRect r,COLORREF fill,
			double radius,double alpha,double frameAlpha=0)
		{
			shape.Initialization(EraserRectCenterX(r),EraserRectCenterY(r),r.Width(),r.Height(),
				radius,radius,1.0,fill,GetThemeColor(ThemeColorEnum::SurfaceFrame));
			shape.enable.Initialization(true); shape.pct.SetDirect(alpha);
			shape.framePct.emplace(frameAlpha); shape.frameLightPct.emplace(frameAlpha);
			shape.frameRendering=UiFrameRenderingEnum::PointLight;
			shape.frameLightColor=UiFrameLightColorEnum::PenWhenDrawing;
		}
		void SetShapeBounds(UiShapeClass& shape,EraserAttributeRect r,double duration,bool direct)
		{
			const auto set=[&](auto& value,double target){ if(direct)value.SetDirect(target);else value.SetTar(target,duration); };
			set(shape.x,EraserRectCenterX(r));set(shape.y,EraserRectCenterY(r));
			set(shape.w,r.Width());set(shape.h,r.Height());
		}

	}




	struct BarPageSurface::Impl
	{
		// 白板菜单与主题面板的当前位置：内容尺寸不变，只随 More 按钮水平平移（遮蔽同名大尺寸基准）。
		EraserAttributeRect MenuBounds=LargeMenuBounds,ThemeBounds=LargeThemeBounds,ThemeAnchor=LargeThemeAnchor;
		// 列表只管理分页；两个属性面板仍是固定数组，没有第二份控件描述树。
		BarButtonListClass pagination;
		std::array<std::shared_ptr<BarButtonClass>,5> pageButtons;
		std::vector<std::shared_ptr<BarButtonClass>> paginationMembers,buttonRow;
		std::array<BarSurfaceButtonId,4> paginationDrawOrder{2,3,4,5};
		std::uint64_t layoutRevision=0;
		std::array<BarButtonClass,3> actions;
		std::array<BarButtonClass,9> previews;
		std::array<UiShapeClass,9> previewBackgrounds;
		std::array<std::wstring,17> iconNames;
		UiWordClass pageTotal,unsaved;
		std::array<UiWordClass,2> headings;
		UiShapeClass root,menu,theme,divider,dragHandle;
		BarAttributePanelMotion menuMotion,themeMotion;
		std::array<Panel,3> panels{Panel{"Pagination"},Panel{"PageMore"},Panel{"PageTheme"}};
		// 绘制由后到前；输入优先级独立，根栏的留白仍挡住菜单。
		PanelStack panelStack{{0,&panels[0],2,2},{1,&panels[1],1,1},{2,&panels[2],0,0}};
		BarAttributePanelFrame menuFrame,themeFrame;
		EraserSurfacePose menuPose,themePose;
		UIRendering renderer{MakeBarLocalizedTypographyServices()};
		// 宿主持有的帧目标（不拥有）；renderer 经 BindFrameTarget 绑定上去，宿主驱动帧生命周期。
		FrameTarget* boundTarget=nullptr;
		UiFrameLightingSnapshot drawingLighting;
		COLORREF appliedSurfaceColor=CLR_INVALID;
		double appliedResidentFillOpacity=-1,appliedResidentContentOpacity=-1;
		float pagePrimaryWidth=0,pageSecondaryWidth=0;
		UiPctClass opacity{1},idleFade{0};
		BarPageSurfaceContent content;
		PageLoadingIndicator indicator;
		bool indicatorAllowed=true,indicatorInputDirty=true,indicatorAnimations=true,ringFailed=false;
		// 环路径按页码按钮内缩圆角矩形缓存；尺寸/圆角变化时重建，设备丢失时随资源一起丢弃。
		ButtonFrameRing ring;
		RECT ringDamage{};
		// 当前页码槽实际显示的文字；门控到点后才换成状态文字。
		std::wstring shownCurrent,shownTotal;bool shownStatus=false;
		BarSurfaceBackgroundSpec background;
		BarSurfaceHooks hooks;
		RECT logical{0,0,1,1},presentation{0,0,1,1},damage{};
		float scale=1;
		double fontTargetScale=0;
		bool initialized=false,backgroundOnly=false,active=false,invalidated=true,subscribed=false;
		LightingSubscriberToken lightingSubscription=0;
		std::chrono::steady_clock::time_point previousFrame{};
		FrameAnimationClock animationClock;
		bool frameSeen=false;
		std::uint64_t appliedLightingRevision=0;
		RECT appliedLightingPresentation{};
		SurfaceSharedLighting appliedLighting;
		RECT previousCursorDamage{};
		struct FrameHit
		{
			std::array<ButtonHitSnapshot,17> buttons{};
			RECT logical{},presentation{};
			float scale=1;
			bool enabled=false;
			std::uint64_t generation=0;
		};
		int FrontPanel(double x,double y) const noexcept
		{
			const auto front=panelStack.FrontPanel(x,y);
			return front?static_cast<int>(*front):-1;
		}
		BarSurfaceButtonId HitButton(const FrameHit& frame,POINT point) const noexcept
		{
			if(!frame.enabled)return 0;
			const double x=point.x/frame.scale,y=point.y/frame.scale;
			const int panel=FrontPanel(x,y);if(panel<0)return 0;
			const BarSurfaceButtonId first=panel==0?1:panel==1?6:9,last=panel==0?5:panel==1?8:17;
			for(auto id=first;id<=last;++id)if(HitTestButton(frame.buttons[id-1],x,y))return id;
			return 0;
		}
		FrameHit candidate,committed,capturedCandidate;
		std::uint64_t damageRevision=0,capturedDamageRevision=0,capturedSerial=0,committedSerial=0;
		mutable std::mutex inputMutex;
		ButtonPointerState pointer;
		BarSurfaceButtonId hover=0,pressed=0,appliedHover=0,appliedPressed=0;
		bool feedbackDirty=false;
		// ---- UI3 自动脏区（V7）：每个分页场景（每个目标窗口）一个登记表，坐标为本场景 presentation 客户区像素。
		// 按钮、浮层、文字、加载环的像素变化由登记表按条目比较「本帧绘制状态」与「已确认呈现状态」得出；
		// 布局/缩放/原点/字号策略/常驻透明度/背景模式/设备与生命周期仍走整幅脏区。
		VisualDirtyRegistry visuals;
		bool visualsRegistered=false;
		// 最近一次准备的候选与随画面捕获的候选：回执确认时只提交被捕获（实际绘制）的那一次。
		std::uint64_t preparedVisualSerial=0,capturedVisualSerial=0;
		// 与 DrawingPose 同一平移：逻辑原点 − presentation 原点 + 姿态位移（亚像素向外取整）。
		RECT MapPose(const RECT& local,EraserSurfacePose pose) const noexcept
		{
			const double tx=logical.left-presentation.left+pose.x*scale,ty=logical.top-presentation.top+pose.y*scale;
			return {local.left+static_cast<LONG>(std::floor(tx)),local.top+static_cast<LONG>(std::floor(ty)),
				local.right+static_cast<LONG>(std::ceil(tx)),local.bottom+static_cast<LONG>(std::ceil(ty))};
		}
		// 局部 DIP 矩形 → 姿态缩放下的客户区像素，外扩与旧 Include 相同的边框/点光/抗锯齿余量。
		RECT LocalPixels(EraserAttributeRect r,EraserSurfacePose pose) const noexcept
		{
			const double zoom=scale*pose.scale;RECT p=Pixels(r,zoom);
			const LONG pad=static_cast<LONG>(std::ceil(DamageOutsetDip*zoom));InflateRect(&p,pad,pad);return MapPose(p,pose);
		}
		// 文字在 contentScale>1 时围绕中心放大绘制，包络同步放大。
		static EraserAttributeRect WordLocal(const UiWordClass& word,double x,double y) noexcept
		{
			EraserAttributeRect r{x,y,x+word.w.val,y+word.h.val};const double cs=word.contentScale;
			if(std::isfinite(cs)&&cs>1.0){const double gx=r.Width()*(cs-1)/2,gy=r.Height()*(cs-1)/2;r={r.left-gx,r.top-gy,r.right+gx,r.bottom+gy};}
			return r;
		}
		EraserSurfacePose PoseOf(int group) const noexcept{return group==1?menuPose:group==2?themePose:EraserSurfacePose{};}
		EraserAttributeRect DragHandleRect() noexcept
		{
			const auto r=Bounds(pageButtons[0]->button);const bool vertical=r.Width()<=r.Height();
			return {EraserRectCenterX(r)-(vertical?1:10),EraserRectCenterY(r)-(vertical?10:1),
				EraserRectCenterX(r)+(vertical?1:10),EraserRectCenterY(r)+(vertical?10:1)};
		}
		void RegisterVisuals()
		{
			visualsRegistered=true;
			// 顶层：整体透明度层、像素映射（缩放与原点）、字号目标、常驻内容透明度（分页按钮与未保存提示）。
			const auto top=visuals.AddGroup([this](VisualGroupSample& out){
				out.visible=true;auto& w=out.state;
				w.Real(opacity.val);w.Real(scale);w.Real(fontTargetScale);
				w.Int(logical.left-presentation.left);w.Int(logical.top-presentation.top);
				w.Real(GetThemeResidentContentOpacity());
			});
			// 分页根栏：PPT 闲置淡化只作用于根栏与分页按钮（浮层先于它绘制）。
			const auto rootGroup=visuals.AddGroup([this](VisualGroupSample& out){out.visible=true;out.state.Real(idleFade.val);},top);
			// 菜单 / 主题浮层：姿态、浮层透明度层、字号目标姿态；与 DrawGroup 一致，不可见时整组不画。
			auto popup=[this,top](bool child){
				return visuals.AddGroup([this,child](VisualGroupSample& out){
					const auto& frame=child?themeFrame:menuFrame;const double alpha=panels[child?2:1].Opacity();
					out.visible=frame.visible&&alpha>0.0001;if(!out.visible)return;
					const auto pose=child?themePose:menuPose;auto& w=out.state;
					w.Real(pose.x);w.Real(pose.y);w.Real(pose.scale);w.Real(alpha);w.Real(frame.targetPose.scale);
				},top);};
			const auto menuGroup=popup(false),themeGroup=popup(true);
			auto shapeEntry=[this](VisualGroupId group,UiShapeClass* shape,int poseGroup,std::function<bool()> gate,const char* name){
				(void)visuals.Add(group,[this,shape,poseGroup,gate=std::move(gate)](VisualSample& out){
					if((gate&&!gate())||!IsVisuallyDrawn(*shape))return;
					const auto b=Bounds(*shape);out.visible=true;out.bounds=LocalPixels(b,PoseOf(poseGroup));
					out.state.Real(b.left);out.state.Real(b.top);WriteVisualState(out.state,*shape,false);
				},name);};
			auto buttonEntry=[this](VisualGroupId group,BarSurfaceButtonId id,int poseGroup,const char* name){
				VisualSamplerContext context;
				context.zoom=[this,poseGroup]{return scale*PoseOf(poseGroup).scale;};
				context.map=[this,poseGroup](const RECT& local){return MapPose(local,PoseOf(poseGroup));};
				if(id>=9){
					// 主题预览：底色块（underlay）与底纹在按钮基础部件之外独立绘制——按钮底板常态透明、
					// 图标与文字都很小，底色块须自己贡献可见性与包络（在同一按压变换内）。
					const std::size_t i=id-9;
					auto inner=MakeButtonVisualSampler(Button(id),context);
					(void)visuals.Add(group,[this,i,id,poseGroup,inner=std::move(inner)](VisualSample& out){
						inner(out);
						auto& underlay=previewBackgrounds[i];
						const bool underlayDrawn=IsVisuallyDrawn(underlay),patternDrawn=i>=6;
						if(!underlayDrawn&&!patternDrawn)return;
						auto& button=Button(id);const auto b=Bounds(button.button);
						// 底色块按自身宽高画在按钮继承位置；底纹画满按钮矩形。
						EraserAttributeRect local=b;
						if(underlayDrawn){local.right=(std::max)(local.right,b.left+underlay.w.val);local.bottom=(std::max)(local.bottom,b.top+underlay.h.val);}
						RECT r=LocalPixels(local,PoseOf(poseGroup));
						Inkeys::UI::UI3::Detail::GrowForPress(r,button.pressScale.val);
						if(out.visible)RenderingAttribute::UnionRectInPlace(out.bounds,r);else out.bounds=r;
						out.visible=true;
						auto& w=out.state;w.Real(button.pressScale.val);w.Real(b.left);w.Real(b.top);w.Real(b.right);w.Real(b.bottom);
						w.Bool(underlayDrawn);if(underlayDrawn)WriteVisualState(w,underlay,false);
						if(i>=6){w.Color(static_cast<COLORREF>(content.backgroundRgb));w.Int(static_cast<int>(i-6));}
					},name);
					return;
				}
				(void)visuals.Add(group,MakeButtonVisualSampler(Button(id),std::move(context),id==3?&pageTotal:nullptr),name);
			};
			shapeEntry(rootGroup,&root,0,{},"page.root");
			shapeEntry(rootGroup,&dragHandle,0,[this]{return !content.whiteboard;},"page.drag-handle");
			// 绘制顺序（paginationDrawOrder）只含 2..5；1 号是拖柄的命中区，拖柄单独绘制。
			for(BarSurfaceButtonId id=2;id<=5;++id)buttonEntry(rootGroup,id,0,"page.button");
			(void)visuals.Add(rootGroup,[this](VisualSample& out){
				if(!(content.whiteboard&&content.surface==0&&content.unsaved)||!IsVisuallyDrawn(unsaved))return;
				out.visible=true;out.bounds=LocalPixels(WordLocal(unsaved,5,-32),{});WriteVisualState(out.state,unsaved,false);
			},"page.unsaved");
			// 加载环：页码按钮内侧的弧，按相位推导；包络与旧 ringDamage 相同（按钮 ± 抗锯齿外扩）。
			(void)visuals.Add(rootGroup,[this](VisualSample& out){
				if(!(indicator.Ring()>0)||ringFailed)return;
				auto& button=pageButtons[2]->button;const auto r=Bounds(button);constexpr float reach=PageLoadingStyle::AntialiasOutset;
				out.visible=true;out.bounds=LocalPixels({r.left-reach,r.top-reach,r.right+reach,r.bottom+reach},{});
				const auto arc=PageLoadingArcAt(indicator.phase,indicatorAnimations);auto& w=out.state;
				w.Real(r.left);w.Real(r.top);w.Real(r.Width());w.Real(r.Height());
				w.Real(button.rw?static_cast<double>(button.rw->val):static_cast<double>(ButtonCornerRadiusDip));
				w.Real(static_cast<double>(arc.start));w.Real(static_cast<double>(arc.length));w.Real(static_cast<double>(indicator.Ring()));
				w.Color(GetThemeColor(ThemeColorEnum::IconPrimary));
			},"page.ring");
			shapeEntry(menuGroup,&menu,1,{},"page.menu");
			for(BarSurfaceButtonId id=6;id<=8;++id)buttonEntry(menuGroup,id,1,"page.menu-button");
			shapeEntry(themeGroup,&theme,2,{},"page.theme");
			shapeEntry(themeGroup,&divider,2,{},"page.divider");
			for(std::size_t i=0;i<headings.size();++i)
				(void)visuals.Add(themeGroup,[this,i](VisualSample& out){
					auto& word=headings[i];if(!IsVisuallyDrawn(word))return;
					out.visible=true;out.bounds=LocalPixels(WordLocal(word,ThemeBounds.left+5,ThemeBounds.top+(i?191.0:5.0)),themePose);
					WriteVisualState(out.state,word,false);
				},"page.heading");
			for(BarSurfaceButtonId id=9;id<=17;++id)buttonEntry(themeGroup,id,2,"page.preview");
		}
		// 准备本帧登记表：必须在最后一次布局/内容/反馈更新之后、宿主读取 PendingDamage 与判断旧候选之前。
		void PrepareVisuals()
		{
			if(backgroundOnly||!initialized)return;
			if(!visualsRegistered)RegisterVisuals();
			// 绘制时才解析的派生几何先按同一公式写入（绘制时以相同输入重写同值）。
			// 页码按钮的总页数槽是次文字，须随按钮一起解析（与 DrawButton 传入的 options.secondary 一致）。
			for(BarSurfaceButtonId id=2;id<=17;++id){auto& button=Button(id);(void)PrepareButtonVisualInheritance(button,Inherit(button.button),id==3?&pageTotal:nullptr);}
			if(!content.whiteboard)SetShapeBounds(dragHandle,DragHandleRect(),0,true);
			const auto attempt=visuals.Prepare();preparedVisualSerial=attempt.serial;
			bool revise=attempt.changedSincePrevious;
			if(attempt.HasDamage()){
				// damage 相对已确认快照：每次都并入（重复并入不扩大）。例如已确认 A、B 待回执时又回到 A，
				// B 的回执清空 damage 后，下一次准备相对 B 得出的范围仍会补进来。
				const RECT client{0,0,presentation.right-presentation.left,presentation.bottom-presentation.top};
				const RECT r=IntersectDirtyRect(attempt.damage,client);
				if(!Empty(r)){const RECT before=damage;DirtyRegionTracker::UnionInPlace(damage,r);revise|=!EqualRect(&before,&damage);}
			}
			// 状态有任何变化（即使 damage 为空或落在已有范围内）也递增修订号：等待回执的旧候选不再代表当前画面。
			if(revise){invalidated=true;++damageRevision;}
		}

		Impl()
		{
			renderer.ConfigureLocalizedTypography();
			std::vector<std::shared_ptr<BarButtonClass>> list;
			for(auto& button:pageButtons){button=std::make_shared<BarButtonClass>();list.push_back(button);}
			pagination.Replace(std::move(list));
			paginationMembers=pagination.Snapshot();
			buttonRow.assign(paginationMembers.begin()+1,paginationMembers.end());
			const COLORREF surface=GetThemeColor(ThemeColorEnum::Surface);appliedSurfaceColor=surface;
			InitializeShape(root,{0,0,BarPptPaginationLongSideDip,BarPptPaginationShortSideDip},surface,BarMainBarCornerRadiusDip,
				GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity),GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity));
			root.residentSurface=true;
			InitializeShape(menu,MenuBounds,surface,BarMainBarCornerRadiusDip,0.95,GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity));
			InitializeShape(theme,ThemeBounds,surface,BarMainBarCornerRadiusDip,0.95,GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity));
			panels[0].BindFrame(root);panels[1].BindFrame(menu);panels[2].BindFrame(theme);
			InitializeShape(divider,{350,-116,570,-115},GetThemeColor(ThemeColorEnum::SurfaceFrame),0.5,0.30);
			InitializeShape(dragHandle,{5,5,7,25},GetThemeColor(ThemeColorEnum::SurfaceFrame),1,0.72);
			for(auto& button:pageButtons) InitializeButton(*button);
			for(auto& button:actions) InitializeButton(button);
			for(auto& button:previews) InitializeButton(button);
			const auto text=GetThemeColor(ThemeColorEnum::TextPrimary);
			pageTotal.Initialization(0,20,70,25,L"",13,text);
			unsaved.Initialization(5,-32,295,27,L"",14,text);
			// 白板主题分区标题与主栏标题同为 13 DIP 粗体。
			for(auto& word:headings){word.Initialization(0,0,220,25,L"",ButtonTwoTwoLabelFontSizeDip,text);word.enable.Initialization(true);}
			unsaved.enable.Initialization(true);
			for(auto& shape:previewBackgrounds){InitializeShape(shape,{0,0,70,70},RGB(18,59,50),4,1,1);shape.frameRendering=UiFrameRenderingEnum::Solid;}
		}
		void InitializeButton(BarButtonClass& button)
		{
			const auto text=GetThemeColor(ThemeColorEnum::TextPrimary);
			button.size=ButtonSizeEnum::twoTwo;button.hide=false;
			InitializeShape(button.button,{0,0,70,70},GetThemeColor(ThemeColorEnum::PressedFill),4,0);
			button.button.framePrimaryLightEnabled=false;
			button.button.frameCursorLightIntensityScale=ButtonCursorLightIntensity;
			button.name.Initialization(0,20,70,25,L"",13,text);
			button.icon.Initialization(0,-10,GetThemeColor(ThemeColorEnum::IconPrimary),std::nullopt);
			button.icon.enable.Initialization(false);
		}
		BarButtonClass& Button(BarSurfaceButtonId id)
		{
			if(id<=5)return *pageButtons[id-1];
			if(id<=8)return actions[id-6];
			return previews[id-9];
		}
		void UpdatePaginationDrawOrder() noexcept
		{
			std::size_t count=0;
			// 拓扑改变时缓存隐藏优先顺序；热帧与绘制不再复制固定列表。
			for(const bool visible:{false,true})for(std::size_t i=1;i<paginationMembers.size();++i)
				if(paginationMembers[i]->IsVisible()==visible)paginationDrawOrder[count++]=static_cast<BarSurfaceButtonId>(i+1);
		}
		// 500ms门控到点后，哪些按钮视觉上变浅：加载/终态失败时整窗口；可重试失败时只有不可点的上/下一页。
		bool LoadingDimmed(BarSurfaceButtonId id) const noexcept
		{
			if(!indicator.reached||content.loadState==BarPageLoadState::None)return false;
			if(content.loadState==BarPageLoadState::RetryableFailed)return id>=2&&id<=4&&!content.interactive[id-2];
			return true;
		}
		bool PropertyButtonEnabled(BarSurfaceButtonId id) const noexcept
		{
			if(id<6)return true;
			return content.whiteboard&&content.surface==0&&(id<9?content.menu!=0:content.menu==2);
		}
		LightingSubscriptionTarget SnapshotLightingTarget() const
		{
			std::scoped_lock lock(inputMutex);
			const auto& frame=committed;
			LightingSubscriptionTarget target;target.enabled=frame.enabled;
			target.damageOutsetPixels=DamageOutsetDip*frame.scale;
			const LONG outset=static_cast<LONG>(std::ceil(DamageOutsetDip*frame.scale));
			for(const auto& panel:panels)if(panel.Presented().visible){
				const auto& r=panel.Presented().interaction.bounds;
				RECT inner=Pixels({r.left,r.top,r.right,r.bottom},frame.scale);OffsetRect(&inner,frame.logical.left,frame.logical.top);
				RECT outer=inner;InflateRect(&outer,outset,outset);target.borders.push_back({outer,inner});
			}
			// hooks 按值带出注册表锁，Reset/销毁后也不访问 Impl。
			target.notify=[callbacks=hooks]{if(callbacks.invalidate)callbacks.invalidate();if(callbacks.wake)callbacks.wake();};
			return target;
		}
		BarSurfaceHooks SnapshotHooks() const
		{std::scoped_lock lock(inputMutex);return hooks;}
		void Wake() noexcept
		{
			// 输入可与首帧 SetHooks 并发，复制后在锁外唤醒，避免读写同一 std::function。
			const auto callbacks=SnapshotHooks();
			if(callbacks.invalidate)callbacks.invalidate();if(callbacks.wake)callbacks.wake();
		}
		void FullDamage() noexcept
		{
			damage={0,0,presentation.right-presentation.left,presentation.bottom-presentation.top};invalidated=true;++damageRevision;
		}
		void Include(RECT r) noexcept
		{
			const LONG pad=Outset();InflateRect(&r,pad,pad);
			OffsetRect(&r,logical.left-presentation.left,logical.top-presentation.top);
			r=IntersectDirtyRect(r,{0,0,presentation.right-presentation.left,presentation.bottom-presentation.top});
			if(!Empty(r)){DirtyRegionTracker::UnionInPlace(damage,r);invalidated=true;++damageRevision;}
		}
		LONG Outset() const noexcept { return backgroundOnly?0:static_cast<LONG>(std::ceil(DamageOutsetDip*scale)); }
		void ConfigureButtonContent(BarButtonClass& button,const std::wstring& icon,
			const std::wstring& label,double angle,double duration,bool direct)
		{
			std::size_t slot=0;for(;slot<17;++slot)if(&Button(static_cast<BarSurfaceButtonId>(slot+1))==&button)break;
			if(slot<17&&iconNames[slot]!=icon){
				if(!icon.empty()){
					if(iconNames[slot].empty()||direct)button.icon.InitializationFromResource(L"UI",icon);
					else button.icon.TransitionToResource(L"UI",icon);
				}
				iconNames[slot]=icon;
			}
			button.icon.enable.Initialization(!icon.empty());
			if(direct)button.name.SetStringImmediate(label);else button.name.TransitionToString(label);
			// 空目标也先完成共享文字退场，中点替换为空以后 Word 自然不再绘制。
			button.name.enable.Initialization(true);
			if(direct)button.icon.angle.SetDirect(angle);else button.icon.angle.SetTar(angle,duration);
		}
		void ConfigureButton(BarButtonClass& button,ButtonVisualLayoutKind kind,EraserAttributeRect rect,
			const std::wstring& icon,const std::wstring& label,double angle,double duration,bool direct)
		{
			ConfigureButtonContent(button,icon,label,angle,duration,direct);
			ConfigureButtonVisualTargets(button,kind,duration,1,direct);
			SetShapeBounds(button.button,rect,duration,direct);
		}
		void UpdateHorizontalPageWidths(const BarPageSurfaceContent& next,bool currentChanged,bool totalChanged,bool direct,double duration)
		{
			const auto metrics=ResolveButtonVisualMetrics(ButtonVisualLayoutKind::PageHorizontal);
			if(currentChanged)pagePrimaryWidth=renderer.MeasureText(next.currentPage,metrics.primaryFontSizeDip,DWRITE_FONT_WEIGHT_BOLD).width;
			if(totalChanged)pageSecondaryWidth=renderer.MeasureText(next.totalPage,metrics.secondaryFontSizeDip,DWRITE_FONT_WEIGHT_NORMAL).width;
			const auto slot=[&](auto& value,double target){if(direct)value.SetDirect(target);else value.SetTar(target,duration);};
			slot(pageButtons[2]->name.w,(std::max)(1.0F,pagePrimaryWidth));slot(pageTotal.w,(std::max)(1.0F,pageSecondaryWidth));
			slot(pageButtons[2]->name.x,-pageSecondaryWidth/2);slot(pageTotal.x,pagePrimaryWidth/2);
		}
		Inkeys::Business::PptWaitTrace::Fields ContentObservationBefore(long oldKind,const BarPageSurfaceContent& next,
			Inkeys::Business::PptWaitTrace::ContentPolicy policy,unsigned changes)
		{
			namespace Trace=Inkeys::Business::PptWaitTrace;Trace::Fields value;
			if(!Trace::contentSample)return value;
			value=Trace::contentSample->fields;value.oldKind=oldKind;value.newKind=static_cast<long>(next.kind);
			value.contentPolicy=static_cast<unsigned>(policy);value.contentChanges=changes;
			value.flags&=~Trace::AnimationEnabled;
			if(static_cast<bool>(UiAnimationEnabled))value.flags|=Trace::AnimationEnabled;
			// 公开原子数值仅作setter前/后观察；不读取可能复制字符串的val/target，也不猜中点。
			value.primaryScaleBefore=Trace::ContentMilli(static_cast<double>(pageButtons[2]->name.contentScale));
			value.primaryPctBefore=Trace::ContentMilli(static_cast<double>(pageButtons[2]->name.contentPct));
			value.secondaryScaleBefore=Trace::ContentMilli(static_cast<double>(pageTotal.contentScale));
			value.secondaryPctBefore=Trace::ContentMilli(static_cast<double>(pageTotal.contentPct));return value;
		}
		void CompleteContentObservation(Inkeys::Business::PptWaitTrace::Fields value,bool primary,bool secondary)
		{
			namespace Trace=Inkeys::Business::PptWaitTrace;if(!Trace::contentSample)return;
			value.setterChanges=(primary?1U:0U)|(secondary?2U:0U);
			value.primaryScaleAfter=Trace::ContentMilli(static_cast<double>(pageButtons[2]->name.contentScale));
			value.primaryPctAfter=Trace::ContentMilli(static_cast<double>(pageButtons[2]->name.contentPct));
			value.secondaryScaleAfter=Trace::ContentMilli(static_cast<double>(pageTotal.contentScale));
			value.secondaryPctAfter=Trace::ContentMilli(static_cast<double>(pageTotal.contentPct));Trace::CaptureContentPolicy(value);
		}
		void ResetRingResources() noexcept
		{
			ring.Reset();ringFailed=false;
		}
		// 状态文字只在500ms门控到点后替换常态页码；空串表示该槽不替换。
		std::pair<std::wstring,std::wstring> DisplayedPages(const BarPageSurfaceContent& value) const
		{
			const bool status=indicator.reached&&value.loadState!=BarPageLoadState::None;
			return {status&&!value.statusPrimary.empty()?value.statusPrimary:value.currentPage,
				status&&!value.statusSecondary.empty()?value.statusSecondary:value.totalPage};
		}
		// 以当前 content 与门控状态重算页码槽文字；返回是否改变。
		bool RefreshDisplayedText()
		{
			const auto [current,total]=DisplayedPages(content);
			const bool status=indicator.reached&&content.loadState!=BarPageLoadState::None;
			const bool currentChanged=current!=shownCurrent,totalChanged=total!=shownTotal;
			if(!currentChanged&&!totalChanged&&status==shownStatus)return false;
			// 纯页码之间的跳变立即生效；进出状态文字走既有的文字过渡。
			// 小尺寸不显示状态词，槽内始终是页码：一律立即换字，避免门控期间数字走中点过渡。
			const bool immediate=content.compact||(!status&&!shownStatus);
			const auto observation=ContentObservationBefore(static_cast<long>(shownStatus),content,
				immediate?Inkeys::Business::PptWaitTrace::ContentPolicy::Immediate:Inkeys::Business::PptWaitTrace::ContentPolicy::Animated,
				(currentChanged?1U:0U)|(totalChanged?2U:0U)|(status!=shownStatus?4U:0U));
			bool primaryChanged,secondaryChanged;
			if(immediate){
				primaryChanged=pageButtons[2]->name.SetStringImmediate(current);secondaryChanged=pageTotal.SetStringImmediate(total);
			}else{
				primaryChanged=pageButtons[2]->name.TransitionToString(current);secondaryChanged=pageTotal.TransitionToString(total);
			}
			CompleteContentObservation(observation,primaryChanged,secondaryChanged);
			if(content.compact&&content.surface<2){
				auto shown=content;shown.currentPage=current;shown.totalPage=total;
				auto& name=pageButtons[2]->name;
				const bool moving=!name.x.IsSame()||!name.w.IsSame()||!name.size.IsSame()
					||!pageTotal.x.IsSame()||!pageTotal.w.IsSame()||!pageTotal.y.IsSame()||!pageTotal.size.IsSame();
				UpdateHorizontalPageWidths(shown,currentChanged,totalChanged,!moving,UiDefaultOperationDur);
			}
			shownCurrent=current;shownTotal=total;shownStatus=status;
			return true;
		}
		void UpdateContent(const BarPageSurfaceContent& next)
		{
			// 同帧false→true也必须退休旧等待；只清截止事实，当前显隐值留给下一Advance连续退场。
			if(content.loadState!=BarPageLoadState::None&&next.loadState==BarPageLoadState::None){
				indicator.requested=false;indicator.deadline=PageLoadingIndicator::Time::max();
			}
			indicatorInputDirty|=content.loadingSession!=next.loadingSession||content.loadState!=next.loadState;
			// 内容更新不得重新提交布局目标，否则普通翻页会截断正在进行的 PPT/白板转换。
			const auto text=[&](BarSurfaceButtonId id,const std::wstring& icon,const std::wstring& label,double angle=0){
				ConfigureButtonContent(Button(id),icon,label,angle,UiDefaultOperationDur,false);
			};
			const bool wb=next.whiteboard,vertical=next.surface>=2,standard=!next.compact;
			if(content.previousLabel!=next.previousLabel)text(2,L"barMore",standard?next.previousLabel:L"",vertical&&!wb?0:-90);
			if(content.nextLabel!=next.nextLabel||content.nextIsAdd!=next.nextIsAdd||content.nextIsEndShow!=next.nextIsEndShow)
				text(4,wb&&next.nextIsAdd?L"barAdd":(!wb&&next.nextIsEndShow?L"barEndShow":L"barMore"),standard?next.nextLabel:L"",
					(wb&&next.nextIsAdd)||(!wb&&next.nextIsEndShow)?0:(vertical&&!wb?180:90));
			if(content.moreLabel!=next.moreLabel)text(5,L"barMenu",standard?next.moreLabel:L"");
			const wchar_t* icons[]={L"barCloseWhiteboard",L"barMinimize",L"barPalette"};
			for(std::size_t i=0;i<actions.size();++i)if(content.actionLabels[i]!=next.actionLabels[i])text(static_cast<BarSurfaceButtonId>(6+i),icons[i],next.actionLabels[i]);
			for(std::size_t i=0;i<next.patternLabels.size();++i)if(content.patternLabels[i]!=next.patternLabels[i])text(static_cast<BarSurfaceButtonId>(15+i),L"colorSelect",next.patternLabels[i]);
			for(std::size_t i=0;i<headings.size();++i)if(content.sectionLabels[i]!=next.sectionLabels[i]){
				headings[i].SetStringImmediate(next.sectionLabels[i]);
			}
			if(content.unsavedLabel!=next.unsavedLabel||content.unsaved!=next.unsaved){
				unsaved.SetStringImmediate(next.unsavedLabel);
			}
			if(content.backgroundRgb!=next.backgroundRgb||content.pattern!=next.pattern){
				const auto rgb=next.backgroundRgb;
				for(std::size_t i=6;i<previewBackgrounds.size();++i)
					previewBackgrounds[i].fill->SetDirect(RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255));
			}
			// 标题、未保存提示、预览底色、启用态、切换/加载、放映态的像素变化都由登记表按条目得出。
			if(content.pptActive!=next.pptActive){
				// 放映恢复时同步撤销 Minimize 的旧反馈，禁用态不能残留按压底色或释放点击。
				if(next.pptActive){
					std::scoped_lock lock(inputMutex);
					if(hover==7||pressed==7){pointer.Reset();hover=pressed=0;feedbackDirty=true;}
				}
			}
			content=next;(void)RefreshDisplayedText();
			const bool menuChanged=menuMotion.Retarget(wb&&next.surface==0&&next.menu!=0,UiDefaultOperationDur);
			const bool themeChanged=themeMotion.Retarget(wb&&next.surface==0&&next.menu==2,UiDefaultOperationDur,&menuMotion.Timeline());
			if(menuChanged||themeChanged)FullDamage();
		}
		void Configure(const BarPageSurfaceContent& next,double duration)
		{
			if(initialized&&!backgroundOnly&&content.surface==next.surface&&content.whiteboard==next.whiteboard
				&&content.compact==next.compact){UpdateContent(next);return;}
			indicatorInputDirty=true;root.residentSurface=true;
			const bool direct=!initialized || duration<=0;
			const bool wb=next.whiteboard,vertical=next.surface>=2,left=next.surface==0,standard=!next.compact;
			const auto sizeMode=next.compact?BarSizeMode::Compact:BarSizeMode::Large;
			const auto& sizeMetrics=ResolveBarSizeMetrics(sizeMode);
			for(std::size_t i=0;i<pageButtons.size();++i){
				pageButtons[i]->hide=(i==0&&wb)||(i==4&&(!wb||!left));
				// 小尺寸：箭头/加页/更多 1×1，页码 2×1（竖向由下方转置为 1×2）。
				pageButtons[i]->size=standard?ButtonSizeEnum::twoTwo:(i==2?ButtonSizeEnum::twoOne:ButtonSizeEnum::oneOne);
			}
			UpdatePaginationDrawOrder();++layoutRevision;
			std::array<EraserAttributeRect,5> rects{};
			EraserAttributeRect rootRect;
			{
				// 可见标准按钮决定位置和背景尺寸；Drag 仍是无额外间距的自有 lane。大小尺寸共用此路径。
				const auto row=ResolveBarButtonListLayout(buttonRow,BarButtonListLayoutKind::SingleRow,{},sizeMode);
				const double lane=pageButtons[0]->IsVisible()?BarPaginationDragLaneDip(sizeMode):0;
				const bool leading=next.surface!=1;
				const double offset=leading?lane:0;
				rootRect=vertical?EraserAttributeRect{0,0,row.height,row.width+lane}:EraserAttributeRect{0,0,row.width+lane,row.height};
				for(std::size_t i=0;i<row.placements.size();++i){
					const auto& item=row.placements[i];
					EraserAttributeRect rect{item.x-item.width/2+offset,item.y-item.height/2,item.x+item.width/2+offset,item.y+item.height/2};
					if(!item.button->IsVisible()){
						std::size_t anchor=i;while(anchor>0&&!row.placements[anchor-1].button->IsVisible())--anchor;
						if(anchor>0){const auto& target=rects[anchor];
							const double x=vertical?EraserRectCenterY(target):EraserRectCenterX(target);
							rect.left=x-item.width/2;rect.right=x+item.width/2;}
					}
					rects[i+1]=vertical?EraserAttributeRect{rect.top,rect.left,rect.bottom,rect.right}:rect;
				}
				const double gap=sizeMetrics.gap;
				const double start=leading?gap:row.width-gap;
				const EraserAttributeRect drag{start,gap,start+lane,row.height-gap};
				rects[0]=vertical?EraserAttributeRect{drag.top,drag.left,drag.bottom,drag.right}:drag;
			}
			// 白板菜单与主题面板随 More 按钮中心水平平移；大尺寸偏移为 0，保持原坐标。
			{
				const double dx=standard?0.0:EraserRectCenterX(rects[4])-EraserRectCenterX(LargeMenuBounds);
				MenuBounds={LargeMenuBounds.left+dx,LargeMenuBounds.top,LargeMenuBounds.right+dx,LargeMenuBounds.bottom};
				ThemeBounds={LargeThemeBounds.left+dx,LargeThemeBounds.top,LargeThemeBounds.right+dx,LargeThemeBounds.bottom};
				ThemeAnchor={LargeThemeAnchor.left+dx,LargeThemeAnchor.top,LargeThemeAnchor.right+dx,LargeThemeAnchor.bottom};
				// 弹层底板与主题分割线与内容同步平移（大尺寸目标与原值相同，不产生变化）。
				SetShapeBounds(menu,MenuBounds,0,true);SetShapeBounds(theme,ThemeBounds,0,true);
				SetShapeBounds(divider,{ThemeBounds.left+5,-116,ThemeBounds.right-5,-115},0,true);
			}
			SetShapeBounds(root,rootRect,duration,direct);
			if(!backgroundOnly){
				const double radius=standard?BarMainBarCornerRadiusDip:sizeMetrics.barCornerRadius;
				if(root.rw)root.rw->SetDirect(radius);
				if(root.rh)root.rh->SetDirect(radius);
			}
			ConfigureButton(*pageButtons[0],ButtonVisualLayoutKind::Custom,rects[0],L"",L"",0,duration,direct);
			ConfigureButton(*pageButtons[1],standard?ButtonVisualLayoutKind::StandardTwoTwo:ButtonVisualLayoutKind::StandardOneOne,
				rects[1],L"barMore",standard?next.previousLabel:L"",vertical&&!wb?0:-90,duration,direct);
			ConfigureButton(*pageButtons[2],standard?ButtonVisualLayoutKind::PageTwoTwo:
				(vertical?ButtonVisualLayoutKind::PageVertical:ButtonVisualLayoutKind::PageHorizontal),rects[2],L"",L"",0,duration,direct);
			// 页码属于状态输出：不沿文字切换动画延迟页身份。
			const auto observation=ContentObservationBefore(-1,next,Inkeys::Business::PptWaitTrace::ContentPolicy::Initial,0);
			const auto [shownNow,shownTotalNow]=DisplayedPages(next);
			const bool primaryChanged=pageButtons[2]->name.SetStringImmediate(shownNow);
			pageButtons[2]->name.enable.Initialization(true);
			const bool secondaryChanged=pageTotal.SetStringImmediate(shownTotalNow);
			shownCurrent=shownNow;shownTotal=shownTotalNow;shownStatus=indicator.reached&&next.loadState!=BarPageLoadState::None;
			CompleteContentObservation(observation,primaryChanged,secondaryChanged);
			const auto metrics=ResolveButtonVisualMetrics(standard?ButtonVisualLayoutKind::PageTwoTwo:
				(vertical?ButtonVisualLayoutKind::PageVertical:ButtonVisualLayoutKind::PageHorizontal));
			const auto slot=[&](auto& value,double target){if(direct)value.SetDirect(target);else value.SetTar(target,duration);};
			// 页身份立即更新，字体及双行槽位仍从当前呈现值连续变换。
			slot(pageTotal.size,metrics.secondaryFontSizeDip);
			slot(pageTotal.x,metrics.secondaryOffsetXDip);slot(pageTotal.y,metrics.secondaryOffsetYDip);
			slot(pageTotal.w,metrics.secondarySlotWidthDip?metrics.secondarySlotWidthDip:ButtonTwoSideDip);
			slot(pageTotal.h,metrics.secondarySlotHeightDip);pageTotal.enable.Initialization(true);
			if(!standard&&!vertical){
				auto shown=next;shown.currentPage=shownNow;shown.totalPage=shownTotalNow;
				UpdateHorizontalPageWidths(shown,true,true,direct,duration);
			}
			ConfigureButton(*pageButtons[3],standard?ButtonVisualLayoutKind::StandardTwoTwo:ButtonVisualLayoutKind::StandardOneOne,rects[3],
				wb&&next.nextIsAdd?L"barAdd":(!wb&&next.nextIsEndShow?L"barEndShow":L"barMore"),standard?next.nextLabel:L"",
				(wb&&next.nextIsAdd)||(!wb&&next.nextIsEndShow)?0:(vertical&&!wb?180:90),duration,direct);
			ConfigureButton(*pageButtons[4],standard?ButtonVisualLayoutKind::StandardTwoTwo:ButtonVisualLayoutKind::StandardOneOne,
				rects[4],L"barMenu",standard?next.moreLabel:L"",0,duration,direct);
			if(direct)for(auto& button:pageButtons)if(!button->IsVisible()){
				// 首帧没有动画 dt；直接配置的隐藏成员从透明开始，普通退场仍保留当前值渐隐。
				button->icon.pct.SetDirect(0);button->name.pct.SetDirect(0);button->button.pct.SetDirect(0);
				if(button->button.framePct)button->button.framePct->SetDirect(0);
				if(button->button.frameLightPct)button->button.frameLightPct->SetDirect(0);
			}
			const wchar_t* icons[]={L"barCloseWhiteboard",L"barMinimize",L"barPalette"};
			for(std::size_t i=0;i<actions.size();++i){
				const double y=MenuBounds.top+5+i*(ButtonOneSideDip+5);
				ConfigureButton(actions[i],ButtonVisualLayoutKind::StandardFourOne,{MenuBounds.left+5,y,MenuBounds.right-5,y+32.5},icons[i],next.actionLabels[i],0,0,true);
			}
			for(std::size_t i=0;i<previews.size();++i){
				const double x=ThemeBounds.left+5+(i%3)*75;
				const double y=ThemeBounds.top+(i<6?35.0+(i/3)*75:221.0);
				ConfigureButton(previews[i],ButtonVisualLayoutKind::StandardTwoTwo,{x,y,x+70,y+70},L"colorSelect",i<6?L"":next.patternLabels[i-6],0,0,true);
				previews[i].icon.color1.reset();previews[i].icon.w.SetDirect(30);previews[i].icon.h.SetDirect(30);
				previews[i].icon.y.SetDirect(i<6?0:ButtonTwoTwoIconOffsetYDip);
				previewBackgrounds[i].fill->SetDirect(i<6?RGB((ThemeColors[i]>>16)&255,(ThemeColors[i]>>8)&255,ThemeColors[i]&255):
					RGB((next.backgroundRgb>>16)&255,(next.backgroundRgb>>8)&255,next.backgroundRgb&255));
				// 浅色用色块边框角色（约 18% 黑）；深色保持原 #909090 实线。
				const bool lightPreview=GetThemeLightWeight()>0.5;
				previewBackgrounds[i].frame->SetDirect(lightPreview?GetThemeColor(ThemeColorEnum::SwatchFrame):RGB(144,144,144));
				previewBackgrounds[i].framePct->SetDirect(lightPreview?0.18:1.0);
				SetShapeBounds(previewBackgrounds[i],Bounds(previews[i].button),0,true);
			}
			for(std::size_t i=0;i<headings.size();++i){headings[i].content.SetVal(next.sectionLabels[i]);headings[i].content.SetTar(next.sectionLabels[i]);}
			unsaved.content.SetVal(next.unsavedLabel);unsaved.content.SetTar(next.unsavedLabel);
			content=next;initialized=true;backgroundOnly=false;
			menuMotion.Retarget(wb&&left&&next.menu!=0,UiDefaultOperationDur);
			themeMotion.Retarget(wb&&left&&next.menu==2,UiDefaultOperationDur,&menuMotion.Timeline());
			FullDamage();
		}
		void RefreshGeometry()
		{
			const auto old=presentation;
			menuFrame=ResolveBarAttributePanelFrame(menuMotion,MenuBounds,Bounds(pageButtons[4]->button),60);
			themeFrame=ResolveBarAttributePanelFrame(themeMotion,ThemeBounds,ThemeAnchor,60,&menuFrame);
			menuPose=menuFrame.pose;themePose=themeFrame.pose;
			RECT extent=Pixels(Bounds(root),scale);
			// 小尺寸长边为非整数 DIP（如 192.5）：根外框按 PageControl 布局同一 lround 取整，
			// 否则呈现外框比目标多 1 px，拖动到位判定与保存交接永远不成立；多出的小数像素落在外扩内。
			if(content.compact){const auto r=Bounds(root);
				extent={static_cast<LONG>(std::lround(r.left*scale)),static_cast<LONG>(std::lround(r.top*scale)),
					static_cast<LONG>(std::lround(r.right*scale)),static_cast<LONG>(std::lround(r.bottom*scale))};}
			if(menuFrame.visible&&menuFrame.opacity>0.0001)DirtyRegionTracker::UnionInPlace(extent,Pixels(menuPose.Apply(MenuBounds),scale));
			if(themeFrame.visible&&themeFrame.opacity>0.0001)DirtyRegionTracker::UnionInPlace(extent,Pixels(themePose.Apply(ThemeBounds),scale));
			if(content.whiteboard&&content.surface==0&&content.unsaved)DirtyRegionTracker::UnionInPlace(extent,Pixels({5,-32,300,0},scale));
			presentation=extent;OffsetRect(&presentation,logical.left,logical.top);InflateRect(&presentation,Outset(),Outset());
			candidate={};candidate.logical=logical;candidate.presentation=presentation;candidate.scale=scale;candidate.enabled=opacity.val>0.0001;
			const std::array<EraserAttributeRect,3> panelRects{Bounds(root),menuPose.Apply(MenuBounds),themePose.Apply(ThemeBounds)};
			// 小尺寸翻页背景与主栏同为 5.25 DIP 同心圆角；大尺寸保持 8。
			const double rootRadius=content.compact?ResolveBarSizeMetrics(BarSizeMode::Compact).barCornerRadius:BarMainBarCornerRadiusDip;
			const std::array<double,3> panelRadii{backgroundOnly?background.cornerRadiusDip:rootRadius,
				BarMainBarCornerRadiusDip*menuPose.scale,BarMainBarCornerRadiusDip*themePose.scale};
			const std::array<bool,3> panelVisible{candidate.enabled,menuFrame.visible&&menuFrame.opacity>0.0001,themeFrame.visible&&themeFrame.opacity>0.0001};
			panels[0].SetExternalState(static_cast<double>(opacity.tar)>0.0001,1,opacity.val);
			panels[1].SetExternalState(!backgroundOnly&&content.whiteboard&&content.surface==0&&content.menu!=0,menuMotion.Geometry(),menuFrame.opacity);
			panels[2].SetExternalState(!backgroundOnly&&content.whiteboard&&content.surface==0&&content.menu==2,themeMotion.Geometry(),themeFrame.opacity);
			for(std::size_t i=0;i<panels.size();++i){
				const auto& r=panelRects[i];const double radius=panelRadii[i];
				RECT visual=Pixels(r,scale);InflateRect(&visual,Outset(),Outset());
				panels[i].SetCandidate(visual,{{r.left,r.top,r.right,r.bottom},radius,radius,PanelHitGeometry::Rounded},panelVisible[i]);
			}
			if(!backgroundOnly)for(BarSurfaceButtonId id=1;id<=17;++id){
				auto& button=Button(id);auto hit=ResolveButtonHitSnapshot(button,Inherit(button.button));
				const auto pose=id>=9?themePose:id>=6?menuPose:EraserSurfacePose{};
				auto r=pose.Apply({hit.boundsDip.left,hit.boundsDip.top,hit.boundsDip.right,hit.boundsDip.bottom});
				hit.boundsDip=D2D1::RectF(static_cast<FLOAT>(r.left),static_cast<FLOAT>(r.top),static_cast<FLOAT>(r.right),static_cast<FLOAT>(r.bottom));
				hit.radiusX*=pose.scale;hit.radiusY*=pose.scale;
				hit.visible=id<=4 || (id==5&&((content.whiteboard&&content.surface==0)||button.icon.pct.val>0.0001||button.name.pct.val>0.0001))
					|| (id>=6&&id<=8&&panelVisible[1]) || (id>=9&&panelVisible[2]);
				hit.enabled=hit.visible&&!content.switching&&PropertyButtonEnabled(id);
				if(id>=2&&id<=4)hit.enabled=hit.enabled&&content.enabled[id-2]&&content.interactive[id-2];
				// 重试按钮在“重试”文字出现之前保持不可点，避免点到仍显示页码的按钮。
				if(id==3&&content.loadState==BarPageLoadState::RetryableFailed&&!indicator.reached)hit.enabled=false;
				if(id==1)hit.enabled=!content.whiteboard;
				if(id==7)hit.enabled=hit.enabled&&!content.pptActive;
				if(id==5)hit.enabled=hit.enabled&&content.whiteboard&&content.surface==0;
				candidate.buttons[id-1]=hit;
			}
			if(!EqualRect(&old,&presentation))FullDamage();
		}
		void RefreshLighting()
		{
			if(!subscribed)return;
			SurfaceSharedLighting light;std::uint64_t revision;
			{const auto snapshot=ReadSharedLighting();light=snapshot.lighting;revision=snapshot.revision;}
			if(subscribed&&(appliedLightingRevision!=revision||!EqualRect(&appliedLightingPresentation,&presentation))){
				const bool mappingChanged=!EqualRect(&appliedLightingPresentation,&presentation);
				const bool primaryChanged=mappingChanged||light.primaryScreenX!=appliedLighting.primaryScreenX
					||light.primaryScreenY!=appliedLighting.primaryScreenY||light.primaryRadiusPixels!=appliedLighting.primaryRadiusPixels
					||light.primaryVisible!=appliedLighting.primaryVisible||light.edgeLightingEnabled!=appliedLighting.edgeLightingEnabled
					||light.drawingPenColor!=appliedLighting.drawingPenColor||light.drawingPenColorBlend!=appliedLighting.drawingPenColorBlend
					||light.drawingLightOpacity!=appliedLighting.drawingLightOpacity;
				renderer.SetFrameLightingSnapshot(ResolveSurfaceFrameLightingSnapshot(light,presentation,0), BarUiEdgeLightingEnabled);
				RECT cursorDamage{};
				if(light.edgeLightingEnabled&&light.cursorVisible&&light.cursorIntensity>0.0001){
					const auto influence=renderer.GetFrameCursorLightDamageBounds();
					const auto includeBorder=[&](EraserAttributeRect rect){
						RECT contentPixels=Pixels(rect,scale);OffsetRect(&contentPixels,logical.left-presentation.left,logical.top-presentation.top);
						RECT outer=contentPixels;InflateRect(&outer,Outset(),Outset());
						DirtyRegionTracker::UnionInPlace(cursorDamage,ResolveLightBorderDamage(outer,contentPixels,influence));
					};
					for(const auto& panel:panels)if(panel.Candidate().visible){
						const auto& r=panel.Candidate().interaction.bounds;includeBorder({r.left,r.top,r.right,r.bottom});
					}
				}
				if(primaryChanged)FullDamage();
				else{
					RECT changedLight=previousCursorDamage;DirtyRegionTracker::UnionInPlace(changedLight,cursorDamage);
					changedLight=IntersectDirtyRect(changedLight,{0,0,presentation.right-presentation.left,presentation.bottom-presentation.top});
					if(!Empty(changedLight)){DirtyRegionTracker::UnionInPlace(damage,changedLight);invalidated=true;++damageRevision;}
				}
				previousCursorDamage=cursorDamage;appliedLighting=light;
				appliedLightingRevision=revision;appliedLightingPresentation=presentation;
			}
		}
		void UpdateFeedback()
		{
			BarSurfaceButtonId newHover,newPressed;
			{std::scoped_lock lock(inputMutex);if(!feedbackDirty)return;newHover=hover;newPressed=pressed;feedbackDirty=false;}
			if(appliedHover&&appliedHover!=newHover)StopButtonHoverVisual(Button(appliedHover),false);
			if(newHover&&newHover!=appliedHover)StartButtonHoverVisual(Button(newHover));
			if(appliedPressed&&appliedPressed!=newPressed)SetButtonPressedVisual(Button(appliedPressed),false);
			if(newPressed) {StopButtonHoverVisual(Button(newPressed),true,true);SetButtonPressedVisual(Button(newPressed),true);}
			appliedHover=newHover;appliedPressed=newPressed;
		}
		// prepareVisuals=false 仅供 Render：裁剪已按此前的准备确定，绘制期间不得再产生新的候选。
		bool Advance(std::chrono::steady_clock::time_point now,bool prepareVisuals=true)
		{
			// 相同 frameTime 或隐藏后恢复也先处理字号策略，不能被 idle 早退吞掉。
			if(renderer.UpdateFontRenderingPolicy(Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize.load()))FullDamage();
			if(frameSeen&&now==previousFrame&&!indicatorInputDirty&&indicatorAnimations==static_cast<bool>(UiAnimationEnabled)){
				// 同一 frameTime 之间仍可能有内容/布局更新：登记表照常准备，不能被时间短路吞掉。
				if(prepareVisuals)PrepareVisuals();return active||renderer.FontAnimationsActive();
			}
			const bool fontPending=renderer.FontAnimationsActive();
			// 字号修正系数在绘制时才推进（被裁剪拒绘的按钮不推进），准备阶段的采样无法代表本帧结果：
			// 修正动画进行中保持整幅，待字号解析前移到裁剪之前再交给登记表。
			if(fontPending)FullDamage();
			Inkeys::UI::RenderPipeline::FrameStageTimer timer(Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics(),
				Inkeys::UI::RenderPipeline::FrameStage::AdvanceAnimationsAndDeriveLayout);
			if(!frameSeen){animationClock.Rebase(now);frameSeen=true;}
			const double dt=animationClock.Tick(now);previousFrame=now;
			if(auto* diagnostics=Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics()){
				diagnostics->rawDtSeconds=animationClock.LastRawElapsedSeconds();diagnostics->animationDtSeconds=dt;
			}
			const double speed=(std::max)(0.01,static_cast<double>(UiAnimationSpeedRate));
			const UiAnimationAdvanceContextClass frame{dt,speed,static_cast<bool>(UiAnimationEnabled),false};
			renderer.BeginFontFrame(now,frame,fontTargetScale>0?fontTargetScale:scale);
            indicatorInputDirty=false;indicatorAnimations=frame.animationEnabled;
            const auto episode=indicator.episode,indicatorSession=indicator.session;
            const bool indicatorChanged=indicator.Advance(now,content.loadingSession,
                indicatorAllowed&&!backgroundOnly,static_cast<PageLoadState>(content.loadState),dt,speed,frame.animationEnabled,
                [](bool entering,double t){return UiApplyCurve(entering?UiCurveEnum::EaseOutCubic:UiCurveEnum::EaseInCubic,t);});
            if((episode!=indicator.episode||indicatorSession!=indicator.session)&&ringFailed)ResetRingResources();
            // 门控到点/撤销时才切换状态文字，短暂的跳变不会出现任何变化。
            if(indicatorChanged)(void)RefreshDisplayedText();
            if(indicatorChanged){
                // 环与页码按钮的像素变化由登记表得出；这里只记录环包络，供隐藏指示器时立即擦除。
                const auto r=Bounds(pageButtons[2]->button);
                constexpr float reach=PageLoadingStyle::AntialiasOutset;
                const auto current=Pixels({r.left-reach,r.top-reach,r.right+reach,r.bottom+reach},scale);
                ringDamage=indicator.Ring()>0?current:RECT{};
            }

			active=fontPending;bool changed=false,panelsChanged=false;
			const auto surface=GetThemeColor(ThemeColorEnum::Surface);
			if(appliedSurfaceColor!=surface){
				// 普通表面跟随主栏样式，预览内部颜色仍由页面主题独立控制。
				const auto text=GetThemeColor(ThemeColorEnum::TextPrimary),border=GetThemeColor(ThemeColorEnum::SurfaceFrame);
				const double frameOpacity=GetThemeSurfaceFrameOpacity(BarMainBarFrameOpacity);
				for(auto* shape:{&root,&menu,&theme})if(shape!=&root||!backgroundOnly||background.useThemeColors){
					shape->fill->SetTar(surface);shape->frame->SetTar(border);
					if(shape!=&root||!backgroundOnly)shape->framePct->SetTar(frameOpacity);
				}
				// 分页常驻底板与主栏同样随主题调整透明度；仅背景模式沿用调用方给定值。
				if(!backgroundOnly)root.pct.SetTar(GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity));
				divider.fill->SetTar(border);dragHandle.fill->SetTar(border);
				pageTotal.color.SetTar(text);unsaved.color.SetTar(text);for(auto& word:headings)word.color.SetTar(text);
				const bool lightPreview=GetThemeLightWeight()>0.5;
				for(auto& shape:previewBackgrounds){
					shape.frame->SetTar(lightPreview?GetThemeColor(ThemeColorEnum::SwatchFrame):RGB(144,144,144));
					shape.framePct->SetTar(lightPreview?0.18:1.0);
				}
				appliedSurfaceColor=surface;
			}
			{
				// 分页不经主栏材质动画，直接取主题端点，使边缘光强度与中性光色和主栏一致。
				const double lightMaterial=GetThemeLightWeight();
				for(auto* shape:{&root,&menu,&theme,&divider,&dragHandle})shape->lightMaterial=lightMaterial;
				for(auto& shape:previewBackgrounds)shape.lightMaterial=lightMaterial;
				for(BarSurfaceButtonId id=1;id<=17;++id)Button(id).button.lightMaterial=lightMaterial;
			}
			{
				// 用户常驻透明度变化：分页底板跟随主栏绝对值，图标/文字倍率在绘制时读取，均需整幅重画。
				const double residentFill=GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity);
				const double residentContent=GetThemeResidentContentOpacity();
				if(residentFill!=appliedResidentFillOpacity||residentContent!=appliedResidentContentOpacity){
					if(appliedResidentFillOpacity>=0&&!backgroundOnly)root.pct.SetTar(residentFill);
					if(appliedResidentContentOpacity>=0)FullDamage();
					appliedResidentFillOpacity=residentFill;appliedResidentContentOpacity=residentContent;
				}
			}
			const auto advance=[&](auto& value){
				if(frame.animationEnabled&&dt<=0&&!value.IsSame()){active=true;return;}
				auto r=UiAdvanceAnimation(value,frame);active|=r.active;changed|=r.changed;};
			advance(opacity);advance(idleFade);
			const ButtonAnimationContext common{dt,speed,static_cast<bool>(UiAnimationEnabled)};
			const auto join=[&](const auto& result){active|=result.active;changed|=result.changed;};
			join(AdvanceShapeVisual(root,common));
			if(!backgroundOnly){
				for(auto* shape:{&menu,&theme,&divider,&dragHandle})join(AdvanceShapeVisual(*shape,common));
				for(auto& word:headings)join(AdvanceWordVisual(word,common));
				join(AdvanceWordVisual(unsaved,common));
				panelsChanged=menuMotion.Advance(frame);panelsChanged|=themeMotion.Advance(frame);
				active|=menuMotion.Active()||themeMotion.Active();
				UpdateFeedback();
				for(BarSurfaceButtonId id=1;id<=17;++id){
					auto& button=Button(id);const bool visible=(id<=5&&button.IsVisible())
						|| (id>=6&&id<=8&&menuMotion.Visible()) || (id>=9&&themeMotion.Visible());
					bool enabled=!LoadingDimmed(id)&&PropertyButtonEnabled(id);if(id>=2&&id<=4)enabled&=content.enabled[id-2];if(id==7)enabled&=!content.pptActive;
					// 与主栏一致：图标用 IconPrimary、文字用 TextPrimary；只有压在白板底色上的样式预览按背景亮度覆盖。
					std::optional<COLORREF> text;
					if(id>=15){const auto rgb=content.backgroundRgb;const unsigned lum=((rgb>>16)&255)*299+((rgb>>8)&255)*587+(rgb&255)*114;
						text=GetThemeColor(lum>128000?ThemeModeEnum::Light:ThemeModeEnum::Dark,ThemeColorEnum::TextPrimary);}
					const bool selected=id>=9&&(id<15?ThemeColors[id-9]==content.backgroundRgb:static_cast<int>(id-15)==content.pattern);
					RetargetButtonInteractionVisual(button,visible,enabled,false,ButtonHoverTransitionDuration,text,
						id>=9?selected:(id!=1&&id!=3),
						GetThemeColor(ThemeColorEnum::PressedFill),GetThemeColor(ThemeColorEnum::TextPrimary));
					if(id>=9)button.icon.color1.reset();
					ButtonAnimationContext animation{dt,speed,static_cast<bool>(UiAnimationEnabled)};
					animation.visible=visible;animation.hoverAllowed=enabled&&id!=1;
					// 通用禁用变浅只覆盖图标与主文字；总页码槽由 Scene 独立持有，需同口径补上。
					if(id==3)pageTotal.pct.SetTar(visible?(enabled?1.0:GetThemeDisabledContentOpacity()):0.0,ButtonHoverTransitionDuration);
					const auto result=AdvanceButtonVisual(button,animation,id==3?&pageTotal:nullptr);
					active|=result.active;
				}
			}
			const auto oldMenu=menuPose,oldTheme=themePose;
			RefreshGeometry();
			// 背景模式由调用方回调绘制、没有内容版本，视觉变化仍整幅；分页模式由登记表按条目得出。
			if(backgroundOnly&&(changed || panelsChanged || oldMenu!=menuPose || oldTheme!=themePose))FullDamage();
			if(prepareVisuals)PrepareVisuals();
			RefreshLighting();
			return active||fontPending;
		}
		BarSurfacePointerResult Pointer(ButtonPointerEvent event,POINT logicalPoint)
		{
			std::scoped_lock lock(inputMutex);
			const auto hit=HitButton(committed,logicalPoint);
			const auto result=pointer.Update(event,hit);
			hover=static_cast<BarSurfaceButtonId>(result.hover);pressed=static_cast<BarSurfaceButtonId>(result.pressed);
			feedbackDirty=feedbackDirty||result.previousHover!=result.hover||result.previousPressed!=result.pressed;
			return {hover,pressed,static_cast<BarSurfaceButtonId>(result.activate),result.previousHover!=result.hover,
				result.previousPressed!=result.pressed,hit!=0||result.previousPressed!=0};
		}
		void DrawShape(ID2D1DeviceContext* dc,UiShapeClass& shape)
		{DrawBackgroundVisual(renderer,dc,shape,Inherit(shape));}
		PanelPose DrawingPose(EraserSurfacePose pose,EraserSurfacePose targetPose={}) const
		{
			PanelPose result;result.zoom=scale*pose.scale;result.fontTargetZoom=(fontTargetScale>0?fontTargetScale:scale)*targetPose.scale;
			result.diffuseReferenceZoom=scale;result.diffuseGeometryScale=1.0/(std::max)(0.001,pose.scale);
			const auto x=static_cast<FLOAT>(logical.left-presentation.left+pose.x*scale),y=static_cast<FLOAT>(logical.top-presentation.top+pose.y*scale);
			auto light=drawingLighting;light.primaryLight.x-=x;light.primaryLight.y-=y;light.cursorLight.x-=x;light.cursorLight.y-=y;
			result.lighting=light;result.edgeLighting=BarUiEdgeLightingEnabled;result.transform=D2D1::Matrix3x2F::Translation(x,y);return result;
		}
		void DrawButton(ID2D1DeviceContext* dc,BarSurfaceButtonId id)
		{
			auto& button=Button(id);ButtonDrawOptions options;
			if(id==3)options.secondary=&pageTotal;
			if(id>=6&&id<=8)options.primaryAlignment=DWRITE_TEXT_ALIGNMENT_LEADING;
			if(id>=9){
				const std::size_t i=id-9;
				options.underlay=[&,i](auto& target,auto* context,auto& item,const auto& inherit){
					auto& shape=previewBackgrounds[i];
					return target.Shape(context,shape,inherit.button);
				};
				if(i>=6)options.content=[&,i](auto& target,auto* context,auto& item,const auto&){
					const float z=static_cast<float>(target.GetFrameZoom());const auto r=Bounds(item.button);
					const D2D1_RECT_F texture=D2D1::RectF(static_cast<FLOAT>(r.left*z),static_cast<FLOAT>(r.top*z),
						static_cast<FLOAT>(r.right*z),static_cast<FLOAT>(r.bottom*z));
					WhiteboardVisual::DrawThemePattern(context,texture,content.backgroundRgb,static_cast<int>(i-6),z,target.ContentSolidBrush(RGB(255,255,255),1));
					return true;
				};
			}
            if(id==3&&indicator.Ring()>0&&!ringFailed)options.content=[&](auto& target,auto* context,auto& item,const auto&){
                // 沿页码按钮边框内侧的圆角矩形画一段弧：无轨道、圆角端帽，弧长与起点由相位推导。
                const auto r=Bounds(item.button);
                const float corner=item.button.rw?static_cast<float>(item.button.rw->val):static_cast<float>(ButtonCornerRadiusDip);
                const auto arc=PageLoadingArcAt(indicator.phase,indicatorAnimations);
                // 装饰失败不改变数字提交或业务恢复状态。
                if(!ring.Draw(context,r.left,r.top,r.Width(),r.Height(),corner,static_cast<float>(ButtonFrameThicknessDip),1.0F,
                    static_cast<float>(target.GetFrameZoom()),{arc.start,arc.length,false},GetThemeColor(ThemeColorEnum::IconPrimary),
                    static_cast<FLOAT>(indicator.Ring())))ringFailed=true;
                return true;
            };
			DrawButtonVisual(renderer,dc,button,Inherit(button.button),options);
		}
		void DrawGroup(ID2D1DeviceContext* dc,bool child)
		{
			const auto pose=child?themePose:menuPose;
			const double alpha=panels[child?2:1].Opacity();
			if(alpha<=0.0001)return;
			PanelPoseScope poseScope(renderer,dc,DrawingPose(pose,child?themeFrame.targetPose:menuFrame.targetPose));
			const auto bounds=child?ThemeBounds:MenuBounds;
			const auto pixels=Pixels(bounds,scale*pose.scale);
			const bool layer=alpha<0.9999;
			if(layer)renderer.PushPanelOpacity(dc,D2D1::RectF(static_cast<FLOAT>(pixels.left),static_cast<FLOAT>(pixels.top),
				static_cast<FLOAT>(pixels.right),static_cast<FLOAT>(pixels.bottom)),alpha);
			auto& surface=child?theme:menu;panels[child?2:1].DrawFrame(renderer,dc,Inherit(surface));
			if(child){
				DrawShape(dc,divider);
				for(std::size_t i=0;i<headings.size();++i)
					renderer.Word(dc,headings[i],{ThemeBounds.left+5,ThemeBounds.top+(i?191.0:5.0)},DWRITE_FONT_WEIGHT_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING);
				for(BarSurfaceButtonId id=9;id<=17;++id)DrawButton(dc,id);
			}else{
				for(BarSurfaceButtonId id=6;id<=8;++id)DrawButton(dc,id);
			}
			if(layer)dc->PopLayer();
		}
	};

	BarPageSurface::BarPageSurface():impl_(std::make_unique<Impl>()){}
	BarPageSurface::~BarPageSurface(){SetSharedLightingSubscribed(false);}
	// 订阅捕获稳定的 Impl 地址，移动 unique_ptr 不改变注册目标。
	BarPageSurface::BarPageSurface(BarPageSurface&& other) noexcept:impl_(std::move(other.impl_)){}
	BarPageSurface& BarPageSurface::operator=(BarPageSurface&& other) noexcept
	{
		if(this!=&other){SetSharedLightingSubscribed(false);
			// 旧 Impl 即将销毁：先让仍存活的帧目标放下对旧绘制器的引用。
			if(impl_&&impl_->boundTarget)impl_->boundTarget->BindRenderer(nullptr);
			impl_=std::move(other.impl_);}
		return *this;
	}
	bool BarPageSurface::ConfigurePagination(const BarPageSurfaceContent& content,double duration)
	{
		Inkeys::UI::RenderPipeline::FrameStageTimer timer(Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics(),
			Inkeys::UI::RenderPipeline::FrameStage::SubmitTargetsAndLayout);
		if (impl_->renderer.RefreshLocalizedTypography()) impl_->FullDamage(); impl_->Configure(content,duration);impl_->RefreshGeometry();
		impl_->PrepareVisuals();return true;
	}
	bool BarPageSurface::ConfigureBackground(const BarSurfaceBackgroundSpec& background)
	{
		auto& p=*impl_;p.background=background;p.backgroundOnly=true;p.initialized=true;
		p.root.residentSurface=false;
		InitializeShape(p.root,{background.bounds.left,background.bounds.top,background.bounds.right,background.bounds.bottom},
			background.useThemeColors?GetThemeColor(ThemeColorEnum::Surface):background.fill,
			background.cornerRadiusDip,background.fillOpacity,background.frameOpacity);
		p.root.frame->SetDirect(background.useThemeColors?GetThemeColor(ThemeColorEnum::SurfaceFrame):background.frame);
		p.root.ft->SetDirect(background.frameThicknessDip);p.root.frameLightPct->SetDirect(0);
		p.RefreshGeometry();p.FullDamage();return true;
	}
	bool BarPageSurface::SetBounds(RECT bounds,float scale) noexcept
	{
		if(Empty(bounds)||!std::isfinite(scale)||scale<=0)return false;
		auto& p=*impl_;if(EqualRect(&p.logical,&bounds)&&p.scale==scale)return true;
		Inkeys::UI::RenderPipeline::FrameStageTimer timer(Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics(),
			Inkeys::UI::RenderPipeline::FrameStage::SubmitTargetsAndLayout);
		// 几何可在同一 frameTime 的 Advance 后落定；只重映射光源，不再推进动画时钟。
		p.logical=bounds;p.scale=scale;p.renderer.SetFrameZoom(scale);p.RefreshGeometry();p.RefreshLighting();p.FullDamage();p.PrepareVisuals();return true;
	}
	void BarPageSurface::SetFontTargetScale(double scale) noexcept
	{
		if(!std::isfinite(scale)||scale<=0||impl_->fontTargetScale==scale)return;
		impl_->fontTargetScale=scale;impl_->renderer.SetFontTargetZoom(scale);impl_->FullDamage();
	}
	void BarPageSurface::SetOpacity(double value,double duration) noexcept
	{
		value=std::isfinite(value)?(std::clamp)(value,0.0,1.0):0.0;
		if(duration<=0){if(impl_->opacity.IsSame()&&impl_->opacity.val==value)return;impl_->opacity.SetDirect(value);}
		else if(!impl_->opacity.SetTar(value,duration))return;
		impl_->FullDamage();
	}
	void BarPageSurface::SetIdleFade(bool dimmed,double duration) noexcept
	{
		const double value=dimmed?1.0:0.0;
		if(duration<=0){if(impl_->idleFade.IsSame()&&impl_->idleFade.val==value)return;impl_->idleFade.SetDirect(value);}
		else if(!impl_->idleFade.SetTar(value,duration))return;
		impl_->FullDamage();
	}
	void BarPageSurface::SetHooks(BarSurfaceHooks hooks)
	{std::scoped_lock lock(impl_->inputMutex);impl_->hooks=std::move(hooks);}
	void BarPageSurface::SetSharedLightingSubscribed(bool value) noexcept
	{
		if(!impl_||impl_->subscribed==value)return;
		impl_->subscribed=value;
		if(value)impl_->lightingSubscription=AddSharedLightingSubscriber(
			[owner=impl_.get()]{return owner->SnapshotLightingTarget();});
		else{RemoveSharedLightingSubscriber(impl_->lightingSubscription);impl_->lightingSubscription=0;}
		// 订阅可晚于本帧 Advance/SetBounds；首帧不等待下一次光源发布。
		if(value){impl_->appliedLightingRevision=0;impl_->RefreshLighting();}
	}
	void BarPageSurface::Reset() noexcept
	{
		SetSharedLightingSubscribed(false);
		// 帧目标归宿主持有、跨 Reset 存活：与原来同序先丢弃目标资源，再解除它对旧绘制器的引用；
		// 整体重建 Impl 后重新绑定，帧目标不会指向已销毁的绘制器。
		FrameTarget* const target=impl_->boundTarget;
		if(target){target->Discard();target->BindRenderer(nullptr);}
		impl_=std::make_unique<Impl>();
		if(target)BindFrameTarget(target);
	}
	void BarPageSurface::BindFrameTarget(FrameTarget* target)
	{
		if(!impl_||impl_->boundTarget==target)return;
		if(impl_->boundTarget)impl_->boundTarget->BindRenderer(nullptr);
		impl_->boundTarget=target;
		if(target)target->BindRenderer(&impl_->renderer);
	}
	void BarPageSurface::PrepareDeviceGeneration(unsigned long long generation) noexcept
	{
		// 先清场景自有设备资源，宿主随后才 Ensure 帧目标（与原 EnsureDeviceResources 同序）。
		const unsigned long long current=impl_->boundTarget?impl_->boundTarget->DeviceGeneration():0;
		if(current!=generation){impl_->ResetRingResources();for(BarSurfaceButtonId id=1;id<=17;++id)impl_->Button(id).icon.ResetCache();}
	}
	void BarPageSurface::ReleaseDeviceResources() noexcept
	{
		impl_->ResetRingResources();
		// 帧目标由宿主持有，但其丢弃必须夹在环资源与图标缓存清理之间，保持原有先后。
		if(impl_->boundTarget)impl_->boundTarget->Discard();
		for(BarSurfaceButtonId id=1;id<=17;++id){impl_->Button(id).icon.ResetCache();impl_->Button(id).pngIcon.ResetCache();}
		impl_->FullDamage();
	}
	void BarPageSurface::Invalidate() noexcept{impl_->FullDamage();impl_->Wake();}
	bool BarPageSurface::IsInvalidated() const noexcept{return impl_->invalidated;}
	RECT BarPageSurface::PendingDamage() const noexcept{return impl_->damage;}
	RECT BarPageSurface::ConsumeDamage() noexcept{const auto d=impl_->damage;impl_->damage={};impl_->invalidated=false;return d;}
	BarPageSurfaceLayout BarPageSurface::Layout() const
	{
		BarPageSurfaceLayout result{impl_->logical,impl_->presentation,impl_->scale};
		const auto& members=impl_->paginationMembers;const auto& order=impl_->paginationDrawOrder;
		for(std::size_t i=0;i<impl_->candidate.buttons.size();++i){const auto& hit=impl_->candidate.buttons[i];
			const auto id=static_cast<BarSurfaceButtonId>(i+1);const auto found=std::find(order.begin(),order.end(),id);
			const void* identity=i<impl_->pageButtons.size()?(i<members.size()?members[i].get():nullptr):&impl_->Button(id);
			result.buttons.push_back({id,Pixels({hit.boundsDip.left,hit.boundsDip.top,hit.boundsDip.right,hit.boundsDip.bottom},impl_->scale),
				hit.visible,hit.enabled,hit.enabled,identity,found!=order.end()?static_cast<unsigned>(found-order.begin()+1):0,
				(std::max)(impl_->Button(id).icon.pct.val,impl_->Button(id).name.pct.val)});}
		return result;
	}
	BarPageSurfaceGeometry BarPageSurface::Geometry() const noexcept
	{
		auto& p=*impl_;const auto body=Bounds(p.root);
		return {{body.left,body.top,body.right,body.bottom},p.opacity.val,ThemeMaterial::ResolveIdleOpacity(p.root.pct.val,ThemeMaterial::IdleBackgroundOpacity,p.backgroundOnly?0.0:static_cast<double>(p.idleFade.val)),p.layoutRevision,
			!p.root.x.IsSame()||!p.root.y.IsSame()||!p.root.w.IsSame()||!p.root.h.IsSame()};
	}
	RECT BarPageSurface::LogicalBounds() const noexcept{return impl_->logical;}
	RECT BarPageSurface::PresentationBounds() const noexcept{return impl_->presentation;}
	LONG BarPageSurface::PresentationOutsetPixels() const noexcept{return impl_->Outset();}
	void BarPageSurface::CommitPresented(bool inputEnabled) noexcept
	{
		std::scoped_lock lock(impl_->inputMutex);const auto generation=impl_->committed.generation+1;
		impl_->committed=impl_->candidate;impl_->committed.generation=generation;
		for(auto& button:impl_->committed.buttons)button.enabled&=inputEnabled;
		impl_->committedSerial=impl_->capturedSerial;
		for(auto& panel:impl_->panels)panel.CommitPresented();
		if(impl_->preparedVisualSerial)(void)impl_->visuals.Commit(impl_->preparedVisualSerial);
	}
	std::uint64_t BarPageSurface::CapturePresentationCandidate() noexcept
	{
		impl_->capturedCandidate=impl_->candidate;
		impl_->capturedDamageRevision=impl_->damageRevision;
		impl_->capturedVisualSerial=impl_->preparedVisualSerial;
		const auto serial=++impl_->capturedSerial;
		for(auto& panel:impl_->panels)panel.CaptureCandidate(serial);
		return serial;
	}
	bool BarPageSurface::IsPresentationCandidateCurrent(std::uint64_t serial) const noexcept
	{
		return std::all_of(impl_->panels.begin(),impl_->panels.end(),[serial](const Panel& panel){return panel.CanConfirm(serial);})
			&&impl_->capturedDamageRevision==impl_->damageRevision;
	}
	bool BarPageSurface::CommitPresentationCandidate(std::uint64_t serial,bool inputEnabled,bool pixelsSubmitted) noexcept
	{
		if(!serial||serial!=impl_->capturedSerial||serial<=impl_->committedSerial)return false;
		std::scoped_lock lock(impl_->inputMutex);
		for(const auto& panel:impl_->panels)if(!panel.CanConfirm(serial))return false;
		const auto generation=impl_->committed.generation+1;
		impl_->committed=impl_->capturedCandidate;impl_->committed.generation=generation;
		for(auto& button:impl_->committed.buttons)button.enabled&=inputEnabled;
		impl_->committedSerial=serial;
		for(auto& panel:impl_->panels)panel.ConfirmPresented(serial);
		// 登记表只推进到被捕获（实际绘制）的那一次准备；之后的变化仍与它比较。
		// 隐藏或无像素候选没有改变窗口像素，不推进登记表快照（下一次准备仍与上次上屏状态比较）。
		if(pixelsSubmitted&&impl_->capturedVisualSerial)(void)impl_->visuals.Commit(impl_->capturedVisualSerial);
		// 捕获后的反馈/光照失效仍归下一帧，旧回执只消费它实际覆盖的 damage。
		if(impl_->capturedDamageRevision==impl_->damageRevision){impl_->damage={};impl_->invalidated=false;}
		return true;
	}
	std::uint64_t BarPageSurface::PresentedGeneration() const noexcept
	{std::scoped_lock lock(impl_->inputMutex);return impl_->committed.generation;}
	std::optional<POINT> BarPageSurface::PresentationToLogical(POINT point) const noexcept
	{
		std::scoped_lock lock(impl_->inputMutex);const auto& c=impl_->committed;
		if(!c.enabled||point.x<0||point.y<0||point.x>=c.presentation.right-c.presentation.left||point.y>=c.presentation.bottom-c.presentation.top)return std::nullopt;
		return POINT{point.x+c.presentation.left-c.logical.left,point.y+c.presentation.top-c.logical.top};
	}
	bool BarPageSurface::HitTestBackground(POINT point) const noexcept
	{
		std::scoped_lock lock(impl_->inputMutex);const auto& c=impl_->committed;if(!c.enabled)return false;
		return impl_->FrontPanel(point.x/c.scale,point.y/c.scale)>=0;
	}
	BarSurfaceButtonId BarPageSurface::HitTest(POINT point) const noexcept
	{
		std::scoped_lock lock(impl_->inputMutex);const auto& c=impl_->committed;if(!c.enabled)return 0;
		return impl_->HitButton(c,point);
	}
	BarSurfaceButtonId BarPageSurface::HitTestPresentation(POINT point) const noexcept
	{const auto logical=PresentationToLogical(point);return logical?HitTest(*logical):0;}
	BarSurfaceInputSample BarPageSurface::SamplePointer(POINT point) const noexcept
	{
		std::scoped_lock lock(impl_->inputMutex);const auto& c=impl_->committed;
		BarSurfaceInputSample result;result.revision=c.generation;
		if(!c.enabled)return result;
		result.logical={point.x+c.presentation.left-c.logical.left,point.y+c.presentation.top-c.logical.top};
		result.valid=true;
		const int panel=impl_->FrontPanel(result.logical.x/c.scale,result.logical.y/c.scale);
		result.background=panel>=0;result.primaryBackground=panel==0;result.popupBackground=panel>0;
		result.hit=impl_->HitButton(c,result.logical);
		return result;
	}
	BarSurfacePointerResult BarPageSurface::ApplyPointer(BarSurfacePointerEventKind kind,BarSurfaceButtonId hit,
		std::optional<POINT> screenPoint) noexcept
	{
		BarSurfacePointerResult result;
		{std::scoped_lock lock(impl_->inputMutex);
			const auto state=impl_->pointer.Update(static_cast<ButtonPointerEvent>(kind),hit,screenPoint);
			impl_->hover=static_cast<BarSurfaceButtonId>(state.hover);impl_->pressed=static_cast<BarSurfaceButtonId>(state.pressed);
			impl_->feedbackDirty|=state.previousHover!=state.hover||state.previousPressed!=state.pressed;
			result={impl_->hover,impl_->pressed,static_cast<BarSurfaceButtonId>(state.activate),state.previousHover!=state.hover,state.previousPressed!=state.pressed,
				hit!=0||state.previousPressed!=0};}
		if(result.hoverChanged||result.pressedChanged)impl_->Wake();return result;
	}
	BarSurfacePointerResult BarPageSurface::PointerMove(POINT p) noexcept{auto r=impl_->Pointer(ButtonPointerEvent::Move,p);if(r.hoverChanged||r.pressedChanged)impl_->Wake();return r;}
	BarSurfacePointerResult BarPageSurface::PointerLeave() noexcept{auto r=impl_->Pointer(ButtonPointerEvent::Leave,{});impl_->Wake();return r;}
	BarSurfacePointerResult BarPageSurface::PointerDown(POINT p) noexcept{auto r=impl_->Pointer(ButtonPointerEvent::Down,p);impl_->Wake();return r;}
	BarSurfacePointerResult BarPageSurface::PointerUp(POINT p,bool) noexcept{auto r=impl_->Pointer(ButtonPointerEvent::Up,p);impl_->Wake();return r;}
	BarSurfacePointerResult BarPageSurface::CancelPointer() noexcept{auto r=impl_->Pointer(ButtonPointerEvent::Cancel,{});impl_->Wake();return r;}
	bool BarPageSurface::Advance(std::chrono::steady_clock::time_point now) noexcept{return impl_->Advance(now);}
	void BarPageSurface::SuspendForIdle(std::chrono::steady_clock::time_point now) noexcept{impl_->animationClock.SuspendForIdle(now);}
	bool BarPageSurface::AnimationActive() const noexcept{return impl_->active||impl_->renderer.FontAnimationsActive();}
	bool BarPageSurface::FontAnimationsActive() const noexcept{return impl_->renderer.FontAnimationsActive();}
    void BarPageSurface::SetLoadingIndicatorVisible(bool visible) noexcept
    {
        if(impl_->indicatorAllowed!=visible)impl_->indicatorInputDirty=true;
        impl_->indicatorAllowed=visible;
        if(!visible){if(impl_->indicator.Ring()>0)impl_->Include(impl_->ringDamage);impl_->ringDamage={};impl_->indicator.Reset();(void)impl_->RefreshDisplayedText();}
    }
    bool BarPageSurface::LoadingIndicatorAnimationActive() const noexcept
    {return impl_->indicatorAllowed&&!impl_->ringFailed&&impl_->indicator.NeedsFrames(static_cast<bool>(UiAnimationEnabled));}
    std::chrono::steady_clock::time_point BarPageSurface::LoadingIndicatorDeadline() const noexcept
    {return impl_->indicatorAllowed?impl_->indicator.WakeDeadline():std::chrono::steady_clock::time_point::max();}
	bool BarPageSurface::PopupAnimationActive() const noexcept{return impl_->menuMotion.Active()||impl_->themeMotion.Active();}
	BarSurfaceRenderResult BarPageSurface::Render(ID2D1DeviceContext* dc,std::chrono::steady_clock::time_point now)
	{
		if(!dc)return {};auto& p=*impl_;p.Advance(now,false);
		if(auto* diagnostics=Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics()){
			const auto light=p.renderer.SnapshotFrameLighting();
			diagnostics->lightFlags=(light.edgeLightingEnabled?1U:0U)|(light.primaryLightVisible?2U:0U)
				|(light.cursorLightVisible?4U:0U)|(light.cursorIntensity>0.0001F?8U:0U);
		}
		p.drawingLighting=p.renderer.SnapshotFrameLighting();
		D2D1_MATRIX_3X2_F original;dc->GetTransform(&original);
		{
		PanelPoseScope rootPose(p.renderer,dc,p.DrawingPose({}));
		const float alpha=static_cast<FLOAT>(p.opacity.val);const bool layer=alpha<0.9999F;
		if(layer)p.renderer.PushPanelOpacity(dc,D2D1::RectF(static_cast<FLOAT>(p.presentation.left-p.logical.left),static_cast<FLOAT>(p.presentation.top-p.logical.top),
			static_cast<FLOAT>(p.presentation.right-p.logical.left),static_cast<FLOAT>(p.presentation.bottom-p.logical.top)),alpha);
		// 后层先画：主题位于菜单下，菜单位于分页栏下；沿用共享面板的当前姿态。
		if(!p.backgroundOnly){
			for(const auto& entry:p.panelStack.DrawOrder()){
				if(entry.id==2&&p.themeFrame.visible)p.DrawGroup(dc,true);
				if(entry.id==1&&p.menuFrame.visible)p.DrawGroup(dc,false);
			}
		}
		p.renderer.SetResidentIdleFade(p.backgroundOnly?0.0:static_cast<double>(p.idleFade.val));
		p.panels[0].DrawFrame(p.renderer,dc,Inherit(p.root));
		if(p.backgroundOnly&&p.background.drawContent){
			dc->SetTransform(D2D1::Matrix3x2F::Scale(p.scale,p.scale)*D2D1::Matrix3x2F::Translation(static_cast<FLOAT>(p.logical.left-p.presentation.left),static_cast<FLOAT>(p.logical.top-p.presentation.top)));
			p.background.drawContent(dc,D2D1::RectF(static_cast<FLOAT>(p.background.bounds.left),static_cast<FLOAT>(p.background.bounds.top),static_cast<FLOAT>(p.background.bounds.right),static_cast<FLOAT>(p.background.bounds.bottom)));dc->SetTransform(p.DrawingPose({}).transform);
		}
		if(!p.backgroundOnly){
			if(!p.content.whiteboard){
				SetShapeBounds(p.dragHandle,p.DragHandleRect(),0,true);p.DrawShape(dc,p.dragHandle);
			}
			// 分页按钮图标/文字属于常驻内容；菜单与主题浮层不受影响。
			p.renderer.SetResidentContentOpacity(GetThemeResidentContentOpacity());
			for(const auto id:p.paginationDrawOrder)p.DrawButton(dc,id);
			if(p.content.whiteboard&&p.content.surface==0&&p.content.unsaved)p.renderer.Word(dc,p.unsaved,{5,-32},DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_LEADING);
			p.renderer.SetResidentContentOpacity(1.0);
		}
		p.renderer.SetResidentIdleFade(0);
		if(layer)dc->PopLayer();
		}
		// 退出作用域后仍保持原 Render 对外留下的根字号/倍率，供下一次布局测量使用。
		dc->SetTransform(original);p.renderer.SetFrameZoom(p.scale);p.renderer.SetFrameDiffuseMaskGeometryScale(1);
		p.renderer.SetFontTargetScale(1.0);p.renderer.SetFontTargetZoom(p.fontTargetScale>0?p.fontTargetScale:p.scale);p.renderer.SetFrameDiffuseReferenceZoom(p.scale);
		p.renderer.SetFrameLightingSnapshot(p.drawingLighting, BarUiEdgeLightingEnabled);
		return {true,p.active||p.renderer.FontAnimationsActive(),p.invalidated,p.damage};
	}
}
