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
#include "Bar.DirtyRegion.h"
#include "Bar.InteractionWorker.h"
#include "../Whiteboard/Whiteboard.Theme.h"

module Inkeys.UI.Bar;
import :Scene;
import :Main;
import :Rendering;
import :Button;
import :State;
import :Theme;
import :UI;
import Inkeys.UI.Bar.Animation;
import Inkeys.UI.Bar.EraserAttributeMotion;
import Inkeys.UI.Bar.FramePacing;
import Inkeys.Other.Config;

namespace Inkeys::UI::Bar
{
	namespace
	{
		constexpr double DamageOutsetDip = BarButtonFrameThicknessDip
			+ BarRenderingAttribute::pointLightDiffuseExtraWidth
			+ BarRenderingAttribute::dirtyAntialiasPadding;
		constexpr EraserAttributeRect MenuBounds{190,-122.5,340,-5};
		constexpr EraserAttributeRect ThemeBounds{345,-301,575,-5};
		constexpr EraserAttributeRect ThemeAnchor{195,-42.5,335,-10};
		constexpr auto& ThemeColors=WhiteboardVisual::ThemeColors;
		std::mutex lightingMutex;
		BarSurfaceSharedLighting sharedLighting;
		std::uint64_t lightingRevision=1;
		std::vector<BarPageSurface*> lightingSubscribers;

		bool Empty(const RECT& r) noexcept { return r.right<=r.left || r.bottom<=r.top; }
		RECT Pixels(EraserAttributeRect r,double scale) noexcept
		{
			return {static_cast<LONG>(std::floor(r.left*scale)),static_cast<LONG>(std::floor(r.top*scale)),
				static_cast<LONG>(std::ceil(r.right*scale)),static_cast<LONG>(std::ceil(r.bottom*scale))};
		}
		EraserAttributeRect Bounds(BarUiShapeClass& shape) noexcept
		{
			return {shape.x.val-shape.w.val/2,shape.y.val-shape.h.val/2,
				shape.x.val+shape.w.val/2,shape.y.val+shape.h.val/2};
		}
		BarUiInheritClass Inherit(BarUiShapeClass& shape) noexcept
		{ const auto b=Bounds(shape); return {b.left,b.top}; }
		bool RoundedContains(EraserAttributeRect r,double radius,double x,double y) noexcept
		{
			if(x<r.left || x>=r.right || y<r.top || y>=r.bottom) return false;
			radius=(std::min)({radius,r.Width()/2,r.Height()/2});
			const double cx=(std::clamp)(x,r.left+radius,r.right-radius);
			const double cy=(std::clamp)(y,r.top+radius,r.bottom-radius);
			return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius;
		}
		void InitializeShape(BarUiShapeClass& shape,EraserAttributeRect r,COLORREF fill,
			double radius,double alpha,double frameAlpha=0)
		{
			shape.Initialization(EraserRectCenterX(r),EraserRectCenterY(r),r.Width(),r.Height(),
				radius,radius,1.0,fill,GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			shape.enable.Initialization(true); shape.pct.SetDirect(alpha);
			shape.framePct.emplace(frameAlpha); shape.frameLightPct.emplace(frameAlpha);
			shape.frameRendering=BarUiFrameRenderingEnum::PointLight;
			shape.frameLightColor=BarUiFrameLightColorEnum::PenWhenDrawing;
		}
		void SetShapeBounds(BarUiShapeClass& shape,EraserAttributeRect r,double duration,bool direct)
		{
			const auto set=[&](auto& value,double target){ if(direct)value.SetDirect(target);else value.SetTar(target,duration); };
			set(shape.x,EraserRectCenterX(r));set(shape.y,EraserRectCenterY(r));
			set(shape.w,r.Width());set(shape.h,r.Height());
		}
		bool SameLighting(const BarSurfaceSharedLighting& a,const BarSurfaceSharedLighting& b) noexcept
		{
			return a.primaryScreenX==b.primaryScreenX && a.primaryScreenY==b.primaryScreenY
				&& a.primaryRadiusPixels==b.primaryRadiusPixels && a.cursorScreenX==b.cursorScreenX
				&& a.cursorScreenY==b.cursorScreenY && a.cursorRadiusPixels==b.cursorRadiusPixels
				&& a.cursorIntensity==b.cursorIntensity && a.drawingPenColor==b.drawingPenColor
				&& a.drawingPenColorBlend==b.drawingPenColorBlend && a.drawingLightOpacity==b.drawingLightOpacity
				&& a.primaryVisible==b.primaryVisible && a.cursorVisible==b.cursorVisible
				&& a.edgeLightingEnabled==b.edgeLightingEnabled;
		}
	}

	BarUiFrameLightingSnapshot ResolveBarSurfaceFrameLightingSnapshot(
		const BarSurfaceSharedLighting& lighting,const RECT& bounds,LONG outset) noexcept
	{
		BarUiFrameLightingSnapshot result;
		result.primaryLight=D2D1::Point2F(static_cast<FLOAT>(lighting.primaryScreenX-bounds.left+outset),
			static_cast<FLOAT>(lighting.primaryScreenY-bounds.top+outset));
		result.primaryRadius=static_cast<FLOAT>(lighting.primaryRadiusPixels);
		result.cursorLight=D2D1::Point2F(static_cast<FLOAT>(lighting.cursorScreenX-bounds.left+outset),
			static_cast<FLOAT>(lighting.cursorScreenY-bounds.top+outset));
		result.cursorScreenLight=D2D1::Point2F(static_cast<FLOAT>(lighting.cursorScreenX),static_cast<FLOAT>(lighting.cursorScreenY));
		result.cursorRadius=static_cast<FLOAT>(lighting.cursorRadiusPixels);
		result.cursorIntensity=static_cast<FLOAT>(lighting.cursorIntensity);
		result.drawingPenColor=lighting.drawingPenColor;result.drawingPenColorBlend=lighting.drawingPenColorBlend;
		result.drawingLightOpacity=lighting.drawingLightOpacity;result.primaryLightVisible=lighting.primaryVisible;
		result.cursorLightVisible=lighting.cursorVisible;result.edgeLightingEnabled=lighting.edgeLightingEnabled;
		return result;
	}

	struct BarPageSurface::Impl
	{
		// 列表只管理分页；两个属性面板仍是固定数组，没有第二份控件描述树。
		BarButtonListClass pagination;
		std::array<std::shared_ptr<BarButtonClass>,5> pageButtons;
		std::vector<std::shared_ptr<BarButtonClass>> paginationMembers,buttonRow;
		std::array<BarSurfaceButtonId,4> paginationDrawOrder{2,3,4,5};
		std::uint64_t layoutRevision=0;
		std::array<BarButtonClass,3> actions;
		std::array<BarButtonClass,9> previews;
		std::array<BarUiShapeClass,9> previewBackgrounds;
		std::array<std::wstring,17> iconNames;
		BarUiWordClass pageTotal,unsaved;
		std::array<BarUiWordClass,2> headings;
		BarUiShapeClass root,menu,theme,divider,dragHandle;
		BarAttributePanelMotion menuMotion,themeMotion;
		BarAttributePanelFrame menuFrame,themeFrame;
		EraserSurfacePose menuPose,themePose;
		BarUIRendering renderer;
		BarUiFrameLightingSnapshot drawingLighting;
		COLORREF appliedSurfaceColor=CLR_INVALID;
		float pagePrimaryWidth=0,pageSecondaryWidth=0;
		BarUiPctClass opacity{1};
		BarPageSurfaceContent content;
		BarSurfaceBackgroundSpec background;
		BarSurfaceHooks hooks;
		RECT logical{0,0,1,1},presentation{0,0,1,1},damage{};
		float scale=1;
		double fontTargetScale=0;
		bool initialized=false,backgroundOnly=false,active=false,invalidated=true,subscribed=false;
		std::chrono::steady_clock::time_point previousFrame{};
		FrameAnimationClock animationClock;
		bool frameSeen=false;
		std::uint64_t appliedLightingRevision=0;
		RECT appliedLightingPresentation{};
		BarSurfaceSharedLighting appliedLighting;
		RECT previousCursorDamage{};
		struct FrameHit
		{
			std::array<BarButtonHitSnapshot,17> buttons{};
			std::array<EraserAttributeRect,3> panels{};
			std::array<double,3> panelRadii{};
			std::array<bool,3> panelVisible{};
			RECT logical{},presentation{};
			float scale=1;
			bool enabled=false;
			std::uint64_t generation=0;
		};
		// 命中与绘制的前后层级一致；前景面板的空白区域也不能点到后层按钮。
		static int FrontPanel(const FrameHit& frame,double x,double y) noexcept
		{
			for(std::size_t i=0;i<frame.panels.size();++i)
				if(frame.panelVisible[i]&&RoundedContains(frame.panels[i],frame.panelRadii[i],x,y))return static_cast<int>(i);
			return -1;
		}
		static BarSurfaceButtonId HitButton(const FrameHit& frame,POINT point) noexcept
		{
			if(!frame.enabled)return 0;
			const double x=point.x/frame.scale,y=point.y/frame.scale;
			const int panel=FrontPanel(frame,x,y);if(panel<0)return 0;
			const BarSurfaceButtonId first=panel==0?1:panel==1?6:9,last=panel==0?5:panel==1?8:17;
			for(auto id=first;id<=last;++id)if(HitTestBarButton(frame.buttons[id-1],x,y))return id;
			return 0;
		}
		FrameHit candidate,committed,capturedCandidate;
		std::uint64_t damageRevision=0,capturedDamageRevision=0,capturedSerial=0,committedSerial=0;
		mutable std::mutex inputMutex;
		ButtonPointerState pointer;
		BarSurfaceButtonId hover=0,pressed=0,appliedHover=0,appliedPressed=0;
		bool feedbackDirty=false;

		Impl()
		{
			renderer.ConfigureLocalizedTypography();
			std::vector<std::shared_ptr<BarButtonClass>> list;
			for(auto& button:pageButtons){button=std::make_shared<BarButtonClass>();list.push_back(button);}
			pagination.Replace(std::move(list));
			paginationMembers=pagination.Snapshot();
			buttonRow.assign(paginationMembers.begin()+1,paginationMembers.end());
			const COLORREF surface=GetThemeColor(BarThemeColorEnum::Surface);appliedSurfaceColor=surface;
			InitializeShape(root,{0,0,BarPptPaginationLongSideDip,BarPptPaginationShortSideDip},surface,BarMainBarCornerRadiusDip,BarMainBarFillOpacity,BarMainBarFrameOpacity);
			InitializeShape(menu,MenuBounds,surface,BarMainBarCornerRadiusDip,0.95,BarMainBarFrameOpacity);
			InitializeShape(theme,ThemeBounds,surface,BarMainBarCornerRadiusDip,0.95,BarMainBarFrameOpacity);
			InitializeShape(divider,{350,-116,570,-115},GetThemeColor(BarThemeColorEnum::SurfaceFrame),0.5,0.30);
			InitializeShape(dragHandle,{5,5,7,25},GetThemeColor(BarThemeColorEnum::SurfaceFrame),1,0.72);
			for(auto& button:pageButtons) InitializeButton(*button);
			for(auto& button:actions) InitializeButton(button);
			for(auto& button:previews) InitializeButton(button);
			const auto text=GetThemeColor(BarThemeColorEnum::TextPrimary);
			pageTotal.Initialization(0,20,70,25,L"",13,text);
			unsaved.Initialization(5,-32,295,27,L"",14,text);
			for(auto& word:headings){word.Initialization(0,0,220,25,L"",12,text);word.enable.Initialization(true);}
			unsaved.enable.Initialization(true);
			for(auto& shape:previewBackgrounds){InitializeShape(shape,{0,0,70,70},RGB(18,59,50),4,1,1);shape.frameRendering=BarUiFrameRenderingEnum::Solid;}
		}
		void InitializeButton(BarButtonClass& button)
		{
			const auto text=GetThemeColor(BarThemeColorEnum::TextPrimary);
			button.size=BarButtonSizeEnum::twoTwo;button.hide=false;
			InitializeShape(button.button,{0,0,70,70},GetThemeColor(BarThemeColorEnum::PressedFill),4,0);
			button.button.framePrimaryLightEnabled=false;
			button.button.frameCursorLightIntensityScale=BarButtonCursorLightIntensity;
			button.name.Initialization(0,20,70,25,L"",13,text);
			button.icon.Initialization(0,-10,text,std::nullopt);
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
		bool PropertyButtonEnabled(BarSurfaceButtonId id) const noexcept
		{
			if(id<6)return true;
			return content.whiteboard&&content.surface==0&&(id<9?content.menu!=0:content.menu==2);
		}
		bool NeedsLightingUpdate(const BarSurfaceSharedLighting& before,
			const BarSurfaceSharedLighting& after) const noexcept
		{
			FrameHit frame;
			{std::scoped_lock lock(inputMutex);frame=committed;}
			if(!frame.enabled)return false;
			const bool colorsChanged=before.drawingPenColor!=after.drawingPenColor
				||before.drawingPenColorBlend!=after.drawingPenColorBlend
				||before.drawingLightOpacity!=after.drawingLightOpacity;
			const bool primaryChanged=colorsChanged||before.primaryScreenX!=after.primaryScreenX
				||before.primaryScreenY!=after.primaryScreenY||before.primaryRadiusPixels!=after.primaryRadiusPixels
				||before.primaryVisible!=after.primaryVisible||before.edgeLightingEnabled!=after.edgeLightingEnabled;
			const bool cursorChanged=colorsChanged||before.cursorScreenX!=after.cursorScreenX
				||before.cursorScreenY!=after.cursorScreenY||before.cursorRadiusPixels!=after.cursorRadiusPixels
				||before.cursorVisible!=after.cursorVisible||before.cursorIntensity!=after.cursorIntensity
				||before.edgeLightingEnabled!=after.edgeLightingEnabled;
			const auto influence=[&](const BarSurfaceSharedLighting& light,bool cursor){
				if(!light.edgeLightingEnabled||(cursor?(!light.cursorVisible||light.cursorIntensity<=0.0001):!light.primaryVisible))return RECT{};
				const double x=cursor?light.cursorScreenX:light.primaryScreenX;
				const double y=cursor?light.cursorScreenY:light.primaryScreenY;
				const double radius=cursor?light.cursorRadiusPixels:light.primaryRadiusPixels;
				if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(radius)||radius<=0)return RECT{};
				const double reach=radius+DamageOutsetDip*frame.scale;
				return Pixels({x-reach,y-reach,x+reach,y+reach},1);
			};
			const RECT oldPrimary=primaryChanged?influence(before,false):RECT{};
			const RECT newPrimary=primaryChanged?influence(after,false):RECT{};
			const RECT oldCursor=cursorChanged?influence(before,true):RECT{};
			const RECT newCursor=cursorChanged?influence(after,true):RECT{};
			const LONG outset=static_cast<LONG>(std::ceil(DamageOutsetDip*frame.scale));
			for(std::size_t i=0;i<frame.panels.size();++i)if(frame.panelVisible[i]){
				RECT inner=Pixels(frame.panels[i],frame.scale);OffsetRect(&inner,frame.logical.left,frame.logical.top);
				RECT outer=inner;InflateRect(&outer,outset,outset);
				for(const RECT& light:{oldPrimary,newPrimary,oldCursor,newCursor})
					if(!Empty(ResolveBarLightBorderDamage(outer,inner,light)))return true;
			}
			return false;
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
			r=IntersectBarDirtyRect(r,{0,0,presentation.right-presentation.left,presentation.bottom-presentation.top});
			if(!Empty(r)){BarDirtyRegionTracker::UnionInPlace(damage,r);invalidated=true;++damageRevision;}
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
		void ConfigureButton(BarButtonClass& button,BarButtonVisualLayoutKind kind,EraserAttributeRect rect,
			const std::wstring& icon,const std::wstring& label,double angle,double duration,bool direct)
		{
			ConfigureButtonContent(button,icon,label,angle,duration,direct);
			ConfigureBarButtonVisualTargets(button,kind,duration,1,direct);
			SetShapeBounds(button.button,rect,duration,direct);
		}
		void IncludeButton(BarSurfaceButtonId id) noexcept
		{
			const auto& hit=candidate.buttons[id-1];if(!hit.visible)return;
			Include(Pixels({hit.boundsDip.left,hit.boundsDip.top,hit.boundsDip.right,hit.boundsDip.bottom},scale));
		}
		void UpdateHorizontalPageWidths(const BarPageSurfaceContent& next,bool currentChanged,bool totalChanged,bool direct,double duration)
		{
			const auto metrics=ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::PageHorizontal);
			if(currentChanged)pagePrimaryWidth=renderer.MeasureText(next.currentPage,metrics.primaryFontSizeDip,DWRITE_FONT_WEIGHT_BOLD).width;
			if(totalChanged)pageSecondaryWidth=renderer.MeasureText(next.totalPage,metrics.secondaryFontSizeDip,DWRITE_FONT_WEIGHT_NORMAL).width;
			const auto slot=[&](auto& value,double target){if(direct)value.SetDirect(target);else value.SetTar(target,duration);};
			slot(pageButtons[2]->name.w,(std::max)(1.0F,pagePrimaryWidth));slot(pageTotal.w,(std::max)(1.0F,pageSecondaryWidth));
			slot(pageButtons[2]->name.x,-pageSecondaryWidth/2);slot(pageTotal.x,pagePrimaryWidth/2);
		}
		void UpdateContent(const BarPageSurfaceContent& next)
		{
			// 内容更新不得重新提交布局目标，否则普通翻页会截断正在进行的 PPT/白板转换。
			const bool currentChanged=content.currentPage!=next.currentPage,totalChanged=content.totalPage!=next.totalPage;
			const bool kindChanged=content.kind!=next.kind;
			if(currentChanged||totalChanged||kindChanged){
				IncludeButton(3);
				if(content.kind==BarPageContentKind::Numeric&&next.kind==BarPageContentKind::Numeric){
					pageButtons[2]->name.SetStringImmediate(next.currentPage);pageTotal.SetStringImmediate(next.totalPage);
				}else{
					pageButtons[2]->name.TransitionToString(next.currentPage);pageTotal.TransitionToString(next.totalPage);
				}
				if(BarPptCompactPaginationEnabled&&!next.whiteboard&&next.surface<2){
					auto& name=pageButtons[2]->name;
					const bool moving=!name.x.IsSame()||!name.w.IsSame()||!name.size.IsSame()
						||!pageTotal.x.IsSame()||!pageTotal.w.IsSame()||!pageTotal.y.IsSame()||!pageTotal.size.IsSame();
					UpdateHorizontalPageWidths(next,currentChanged,totalChanged,!moving,BarUiDefaultOperationDur);
				}
			}
			const auto text=[&](BarSurfaceButtonId id,const std::wstring& icon,const std::wstring& label,double angle=0){
				ConfigureButtonContent(Button(id),icon,label,angle,BarUiDefaultOperationDur,false);IncludeButton(id);
			};
			const bool wb=next.whiteboard,vertical=next.surface>=2,standard=wb||!BarPptCompactPaginationEnabled;
			if(content.previousLabel!=next.previousLabel)text(2,L"barMore",standard?next.previousLabel:L"",vertical&&!wb?0:-90);
			if(content.nextLabel!=next.nextLabel||content.nextIsAdd!=next.nextIsAdd||content.nextIsEndShow!=next.nextIsEndShow)
				text(4,wb&&next.nextIsAdd?L"barAdd":(!wb&&next.nextIsEndShow?L"barEndShow":L"barMore"),standard?next.nextLabel:L"",
					(wb&&next.nextIsAdd)||(!wb&&next.nextIsEndShow)?0:(vertical&&!wb?180:90));
			if(content.moreLabel!=next.moreLabel)text(5,L"barMenu",next.moreLabel);
			const wchar_t* icons[]={L"barCloseSmall",L"barMinimize",L"barPalette"};
			for(std::size_t i=0;i<actions.size();++i)if(content.actionLabels[i]!=next.actionLabels[i])text(static_cast<BarSurfaceButtonId>(6+i),icons[i],next.actionLabels[i]);
			for(std::size_t i=0;i<next.patternLabels.size();++i)if(content.patternLabels[i]!=next.patternLabels[i])text(static_cast<BarSurfaceButtonId>(15+i),L"colorSelect",next.patternLabels[i]);
			for(std::size_t i=0;i<headings.size();++i)if(content.sectionLabels[i]!=next.sectionLabels[i]){
				headings[i].SetStringImmediate(next.sectionLabels[i]);if(themeFrame.visible)Include(Pixels(themePose.Apply(ThemeBounds),scale));
			}
			if(content.unsavedLabel!=next.unsavedLabel||content.unsaved!=next.unsaved){
				unsaved.SetStringImmediate(next.unsavedLabel);if(wb&&next.surface==0)Include(Pixels({5,-32,300,0},scale));
			}
			if(content.backgroundRgb!=next.backgroundRgb||content.pattern!=next.pattern){
				const auto rgb=next.backgroundRgb;
				for(std::size_t i=6;i<previewBackgrounds.size();++i){
					previewBackgrounds[i].fill->SetDirect(RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255));IncludeButton(static_cast<BarSurfaceButtonId>(9+i));
				}
				if(content.backgroundRgb!=next.backgroundRgb)for(std::size_t i=0;i<ThemeColors.size();++i)
					if(ThemeColors[i]==content.backgroundRgb||ThemeColors[i]==next.backgroundRgb)IncludeButton(static_cast<BarSurfaceButtonId>(9+i));
			}
			for(std::size_t i=0;i<next.enabled.size();++i)if(content.enabled[i]!=next.enabled[i]||content.interactive[i]!=next.interactive[i])IncludeButton(static_cast<BarSurfaceButtonId>(2+i));
			if(content.switching!=next.switching)for(BarSurfaceButtonId id=1;id<=17;++id)IncludeButton(id);
			if(content.navigationWaitingPrevious!=next.navigationWaitingPrevious)IncludeButton(2);
			if(content.navigationWaitingNext!=next.navigationWaitingNext)IncludeButton(4);
			if(content.pptActive!=next.pptActive){
				IncludeButton(7);
				// 放映恢复时同步撤销 Minimize 的旧反馈，禁用态不能残留按压底色或释放点击。
				if(next.pptActive){
					std::scoped_lock lock(inputMutex);
					if(hover==7||pressed==7){pointer.Reset();hover=pressed=0;feedbackDirty=true;}
				}
			}
			content=next;
			const bool menuChanged=menuMotion.Retarget(wb&&next.surface==0&&next.menu!=0,BarUiDefaultOperationDur);
			const bool themeChanged=themeMotion.Retarget(wb&&next.surface==0&&next.menu==2,BarUiDefaultOperationDur,&menuMotion.Timeline());
			if(menuChanged||themeChanged)FullDamage();
		}
		void Configure(const BarPageSurfaceContent& next,double duration)
		{
			if(initialized&&!backgroundOnly&&content.surface==next.surface&&content.whiteboard==next.whiteboard){UpdateContent(next);return;}
			const bool direct=!initialized || duration<=0;
			const bool wb=next.whiteboard,vertical=next.surface>=2,left=next.surface==0,standard=wb||!BarPptCompactPaginationEnabled;
			for(std::size_t i=0;i<pageButtons.size();++i){
				pageButtons[i]->hide=(i==0&&wb)||(i==4&&(!wb||!left));
				pageButtons[i]->size=BarButtonSizeEnum::twoTwo;
			}
			UpdatePaginationDrawOrder();++layoutRevision;
			std::array<EraserAttributeRect,5> rects{};
			EraserAttributeRect rootRect;
			if(standard){
				// 可见标准按钮决定位置和背景尺寸；Drag 仍是无额外间距的自有 lane。
				const auto row=ResolveBarButtonListLayout(buttonRow,BarButtonListLayoutKind::SingleRow);
				const double lane=pageButtons[0]->IsVisible()?BarPaginationDragHandleSlotDip:0;
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
				const double start=leading?BarButtonGapDip:row.width-BarButtonGapDip;
				const EraserAttributeRect drag{start,BarButtonGapDip,start+lane,row.height-BarButtonGapDip};
				rects[0]=vertical?EraserAttributeRect{drag.top,drag.left,drag.bottom,drag.right}:drag;
			}
			else{
				// 旧紧凑分支继续保留；当前编译期开关统一使用上方的标准列表布局。
				rootRect=vertical?EraserAttributeRect{0,0,BarPptPaginationShortSideDip,BarPptPaginationLongSideDip}:
					EraserAttributeRect{0,0,BarPptPaginationLongSideDip,BarPptPaginationShortSideDip};
				if(vertical)rects={EraserAttributeRect{5,5,37.5,15},{5,15,37.5,47.5},{5,52.5,37.5,122.5},{5,127.5,37.5,160},{37.5,127.5,37.5,160}};
				else if(next.surface==1)rects={EraserAttributeRect{150,5,160,37.5},{5,5,37.5,37.5},{42.5,5,112.5,37.5},{117.5,5,150,37.5},{160,5,160,37.5}};
				else rects={EraserAttributeRect{5,5,15,37.5},{15,5,47.5,37.5},{52.5,5,122.5,37.5},{127.5,5,160,37.5},{160,5,160,37.5}};
				const double half=BarButtonTwoSideDip/2,x=EraserRectCenterX(rects[3]),y=EraserRectCenterY(rects[3]);
				rects[4]={x-half,y-half,x+half,y+half};
			}
			SetShapeBounds(root,rootRect,duration,direct);
			ConfigureButton(*pageButtons[0],BarButtonVisualLayoutKind::Custom,rects[0],L"",L"",0,duration,direct);
			ConfigureButton(*pageButtons[1],standard?BarButtonVisualLayoutKind::StandardTwoTwo:BarButtonVisualLayoutKind::StandardOneOne,
				rects[1],L"barMore",standard?next.previousLabel:L"",vertical&&!wb?0:-90,duration,direct);
			ConfigureButton(*pageButtons[2],standard?BarButtonVisualLayoutKind::PageTwoTwo:
				(vertical?BarButtonVisualLayoutKind::PageVertical:BarButtonVisualLayoutKind::PageHorizontal),rects[2],L"",L"",0,duration,direct);
			// 页码属于状态输出：不沿文字切换动画延迟页身份。
			pageButtons[2]->name.SetStringImmediate(next.currentPage);
			pageButtons[2]->name.enable.Initialization(true);
			pageTotal.SetStringImmediate(next.totalPage);
			const auto metrics=ResolveBarButtonVisualMetrics(standard?BarButtonVisualLayoutKind::PageTwoTwo:
				(vertical?BarButtonVisualLayoutKind::PageVertical:BarButtonVisualLayoutKind::PageHorizontal));
			const auto slot=[&](auto& value,double target){if(direct)value.SetDirect(target);else value.SetTar(target,duration);};
			// 页身份立即更新，字体及双行槽位仍从当前呈现值连续变换。
			slot(pageTotal.size,metrics.secondaryFontSizeDip);
			slot(pageTotal.x,metrics.secondaryOffsetXDip);slot(pageTotal.y,metrics.secondaryOffsetYDip);
			slot(pageTotal.w,metrics.secondarySlotWidthDip?metrics.secondarySlotWidthDip:BarButtonTwoSideDip);
			slot(pageTotal.h,metrics.secondarySlotHeightDip);pageTotal.enable.Initialization(true);
			if(!standard&&!vertical){
				UpdateHorizontalPageWidths(next,true,true,direct,duration);
			}
			ConfigureButton(*pageButtons[3],standard?BarButtonVisualLayoutKind::StandardTwoTwo:BarButtonVisualLayoutKind::StandardOneOne,rects[3],
				wb&&next.nextIsAdd?L"barAdd":(!wb&&next.nextIsEndShow?L"barEndShow":L"barMore"),standard?next.nextLabel:L"",
				(wb&&next.nextIsAdd)||(!wb&&next.nextIsEndShow)?0:(vertical&&!wb?180:90),duration,direct);
			ConfigureButton(*pageButtons[4],BarButtonVisualLayoutKind::StandardTwoTwo,rects[4],L"barMenu",next.moreLabel,0,duration,direct);
			if(direct)for(auto& button:pageButtons)if(!button->IsVisible()){
				// 首帧没有动画 dt；直接配置的隐藏成员从透明开始，普通退场仍保留当前值渐隐。
				button->icon.pct.SetDirect(0);button->name.pct.SetDirect(0);button->button.pct.SetDirect(0);
				if(button->button.framePct)button->button.framePct->SetDirect(0);
				if(button->button.frameLightPct)button->button.frameLightPct->SetDirect(0);
			}
			const wchar_t* icons[]={L"barCloseSmall",L"barMinimize",L"barPalette"};
			for(std::size_t i=0;i<actions.size();++i){
				const double y=MenuBounds.top+5+i*(BarButtonOneSideDip+5);
				ConfigureButton(actions[i],BarButtonVisualLayoutKind::StandardFourOne,{195,y,335,y+32.5},icons[i],next.actionLabels[i],0,0,true);
			}
			for(std::size_t i=0;i<previews.size();++i){
				const double x=ThemeBounds.left+5+(i%3)*75;
				const double y=ThemeBounds.top+(i<6?35.0+(i/3)*75:221.0);
				ConfigureButton(previews[i],BarButtonVisualLayoutKind::StandardTwoTwo,{x,y,x+70,y+70},L"colorSelect",i<6?L"":next.patternLabels[i-6],0,0,true);
				previews[i].icon.color1.reset();previews[i].icon.w.SetDirect(30);previews[i].icon.h.SetDirect(30);
				previews[i].icon.y.SetDirect(i<6?0:BarButtonTwoTwoIconOffsetYDip);
				previewBackgrounds[i].fill->SetDirect(i<6?RGB((ThemeColors[i]>>16)&255,(ThemeColors[i]>>8)&255,ThemeColors[i]&255):
					RGB((next.backgroundRgb>>16)&255,(next.backgroundRgb>>8)&255,next.backgroundRgb&255));
				previewBackgrounds[i].frame->SetDirect(RGB(144,144,144));
				SetShapeBounds(previewBackgrounds[i],Bounds(previews[i].button),0,true);
			}
			for(std::size_t i=0;i<headings.size();++i){headings[i].content.SetVal(next.sectionLabels[i]);headings[i].content.SetTar(next.sectionLabels[i]);}
			unsaved.content.SetVal(next.unsavedLabel);unsaved.content.SetTar(next.unsavedLabel);
			content=next;initialized=true;backgroundOnly=false;
			menuMotion.Retarget(wb&&left&&next.menu!=0,BarUiDefaultOperationDur);
			themeMotion.Retarget(wb&&left&&next.menu==2,BarUiDefaultOperationDur,&menuMotion.Timeline());
			FullDamage();
		}
		void RefreshGeometry()
		{
			const auto old=presentation;
			menuFrame=ResolveBarAttributePanelFrame(menuMotion,MenuBounds,Bounds(pageButtons[4]->button),60);
			themeFrame=ResolveBarAttributePanelFrame(themeMotion,ThemeBounds,ThemeAnchor,60,&menuFrame);
			menuPose=menuFrame.pose;themePose=themeFrame.pose;
			RECT extent=Pixels(Bounds(root),scale);
			if(menuFrame.visible&&menuFrame.opacity>0.0001)BarDirtyRegionTracker::UnionInPlace(extent,Pixels(menuPose.Apply(MenuBounds),scale));
			if(themeFrame.visible&&themeFrame.opacity>0.0001)BarDirtyRegionTracker::UnionInPlace(extent,Pixels(themePose.Apply(ThemeBounds),scale));
			if(content.whiteboard&&content.surface==0&&content.unsaved)BarDirtyRegionTracker::UnionInPlace(extent,Pixels({5,-32,300,0},scale));
			presentation=extent;OffsetRect(&presentation,logical.left,logical.top);InflateRect(&presentation,Outset(),Outset());
			candidate={};candidate.logical=logical;candidate.presentation=presentation;candidate.scale=scale;candidate.enabled=opacity.val>0.0001;
			candidate.panels[0]=Bounds(root);candidate.panelRadii[0]=backgroundOnly?background.cornerRadiusDip:BarMainBarCornerRadiusDip;
			candidate.panelVisible[0]=candidate.enabled;
			candidate.panels[1]=menuPose.Apply(MenuBounds);candidate.panelRadii[1]=BarMainBarCornerRadiusDip*menuPose.scale;
			candidate.panelVisible[1]=menuFrame.visible&&menuFrame.opacity>0.0001;
			candidate.panels[2]=themePose.Apply(ThemeBounds);candidate.panelRadii[2]=BarMainBarCornerRadiusDip*themePose.scale;
			candidate.panelVisible[2]=themeFrame.visible&&themeFrame.opacity>0.0001;
			if(!backgroundOnly)for(BarSurfaceButtonId id=1;id<=17;++id){
				auto& button=Button(id);auto hit=ResolveBarButtonHitSnapshot(button,Inherit(button.button));
				const auto pose=id>=9?themePose:id>=6?menuPose:EraserSurfacePose{};
				auto r=pose.Apply({hit.boundsDip.left,hit.boundsDip.top,hit.boundsDip.right,hit.boundsDip.bottom});
				hit.boundsDip=D2D1::RectF(static_cast<FLOAT>(r.left),static_cast<FLOAT>(r.top),static_cast<FLOAT>(r.right),static_cast<FLOAT>(r.bottom));
				hit.radiusX*=pose.scale;hit.radiusY*=pose.scale;
				hit.visible=id<=4 || (id==5&&((content.whiteboard&&content.surface==0)||button.icon.pct.val>0.0001||button.name.pct.val>0.0001))
					|| (id>=6&&id<=8&&candidate.panelVisible[1]) || (id>=9&&candidate.panelVisible[2]);
				hit.enabled=hit.visible&&!content.switching&&PropertyButtonEnabled(id);
				if(id>=2&&id<=4)hit.enabled=hit.enabled&&content.enabled[id-2]&&content.interactive[id-2];
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
			BarSurfaceSharedLighting light;std::uint64_t revision;
			{std::scoped_lock lock(lightingMutex);light=sharedLighting;revision=lightingRevision;}
			if(subscribed&&(appliedLightingRevision!=revision||!EqualRect(&appliedLightingPresentation,&presentation))){
				const bool mappingChanged=!EqualRect(&appliedLightingPresentation,&presentation);
				const bool primaryChanged=mappingChanged||light.primaryScreenX!=appliedLighting.primaryScreenX
					||light.primaryScreenY!=appliedLighting.primaryScreenY||light.primaryRadiusPixels!=appliedLighting.primaryRadiusPixels
					||light.primaryVisible!=appliedLighting.primaryVisible||light.edgeLightingEnabled!=appliedLighting.edgeLightingEnabled
					||light.drawingPenColor!=appliedLighting.drawingPenColor||light.drawingPenColorBlend!=appliedLighting.drawingPenColorBlend
					||light.drawingLightOpacity!=appliedLighting.drawingLightOpacity;
				renderer.SetFrameLightingSnapshot(ResolveBarSurfaceFrameLightingSnapshot(light,presentation,0));
				RECT cursorDamage{};
				if(light.edgeLightingEnabled&&light.cursorVisible&&light.cursorIntensity>0.0001){
					const auto influence=renderer.GetFrameCursorLightDamageBounds();
					const auto includeBorder=[&](EraserAttributeRect rect){
						RECT contentPixels=Pixels(rect,scale);OffsetRect(&contentPixels,logical.left-presentation.left,logical.top-presentation.top);
						RECT outer=contentPixels;InflateRect(&outer,Outset(),Outset());
						BarDirtyRegionTracker::UnionInPlace(cursorDamage,ResolveBarLightBorderDamage(outer,contentPixels,influence));
					};
					for(std::size_t i=0;i<candidate.panels.size();++i)if(candidate.panelVisible[i])includeBorder(candidate.panels[i]);
				}
				if(primaryChanged)FullDamage();
				else{
					RECT changedLight=previousCursorDamage;BarDirtyRegionTracker::UnionInPlace(changedLight,cursorDamage);
					changedLight=IntersectBarDirtyRect(changedLight,{0,0,presentation.right-presentation.left,presentation.bottom-presentation.top});
					if(!Empty(changedLight)){BarDirtyRegionTracker::UnionInPlace(damage,changedLight);invalidated=true;++damageRevision;}
				}
				previousCursorDamage=cursorDamage;appliedLighting=light;
				appliedLightingRevision=revision;appliedLightingPresentation=presentation;
			}
		}
		void UpdateFeedback()
		{
			BarSurfaceButtonId newHover,newPressed;
			{std::scoped_lock lock(inputMutex);if(!feedbackDirty)return;newHover=hover;newPressed=pressed;feedbackDirty=false;}
			if(appliedHover&&appliedHover!=newHover)StopBarButtonHoverVisual(Button(appliedHover),false);
			if(newHover&&newHover!=appliedHover)StartBarButtonHoverVisual(Button(newHover));
			if(appliedPressed&&appliedPressed!=newPressed)SetBarButtonPressedVisual(Button(appliedPressed),false);
			if(newPressed) {StopBarButtonHoverVisual(Button(newPressed),true,true);SetBarButtonPressedVisual(Button(newPressed),true);}
			if(appliedHover)Include(Pixels({candidate.buttons[appliedHover-1].boundsDip.left,candidate.buttons[appliedHover-1].boundsDip.top,
				candidate.buttons[appliedHover-1].boundsDip.right,candidate.buttons[appliedHover-1].boundsDip.bottom},scale));
			appliedHover=newHover;appliedPressed=newPressed;
		}
		bool Advance(std::chrono::steady_clock::time_point now)
		{
			// 相同 frameTime 或隐藏后恢复也先处理字号策略，不能被 idle 早退吞掉。
			if(renderer.UpdateFontRenderingPolicy(Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize.load()))FullDamage();
			if(frameSeen&&now==previousFrame)return active||renderer.FontAnimationsActive();
			const bool fontPending=renderer.FontAnimationsActive();
			if(fontPending)FullDamage();
			Inkeys::UI::RenderPipeline::FrameStageTimer timer(Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics(),
				Inkeys::UI::RenderPipeline::FrameStage::AdvanceAnimationsAndDeriveLayout);
			if(!frameSeen){animationClock.Rebase(now);frameSeen=true;}
			const double dt=animationClock.Tick(now);previousFrame=now;
			if(auto* diagnostics=Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics()){
				diagnostics->rawDtSeconds=animationClock.LastRawElapsedSeconds();diagnostics->animationDtSeconds=dt;
			}
			const double speed=(std::max)(0.01,static_cast<double>(BarUiAnimationSpeedRate));
			const BarUiAnimationAdvanceContextClass frame{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
			renderer.BeginFontFrame(now,frame,fontTargetScale>0?fontTargetScale:scale);
			active=fontPending;bool changed=false,panelsChanged=false;
			const auto surface=GetThemeColor(BarThemeColorEnum::Surface);
			if(appliedSurfaceColor!=surface){
				// 普通表面跟随主栏样式，预览内部颜色仍由页面主题独立控制。
				const auto text=GetThemeColor(BarThemeColorEnum::TextPrimary),border=GetThemeColor(BarThemeColorEnum::SurfaceFrame);
				for(auto* shape:{&root,&menu,&theme})if(shape!=&root||!backgroundOnly||background.useThemeColors){
					shape->fill->SetTar(surface);shape->frame->SetTar(border);
				}
				divider.fill->SetTar(border);dragHandle.fill->SetTar(border);
				pageTotal.color.SetTar(text);unsaved.color.SetTar(text);for(auto& word:headings)word.color.SetTar(text);
				appliedSurfaceColor=surface;
			}
			const auto advance=[&](auto& value){
				if(frame.animationEnabled&&dt<=0&&!value.IsSame()){active=true;return;}
				auto r=BarUiAdvanceAnimation(value,frame);active|=r.active;changed|=r.changed;};
			advance(opacity);
			const BarButtonAnimationContext common{dt,speed,static_cast<bool>(BarUiAnimationEnabled)};
			const auto join=[&](const auto& result){active|=result.active;changed|=result.changed;};
			join(AdvanceBarShapeVisual(root,common));
			if(!backgroundOnly){
				for(auto* shape:{&menu,&theme,&divider,&dragHandle})join(AdvanceBarShapeVisual(*shape,common));
				for(auto& word:headings)join(AdvanceBarWordVisual(word,common));
				join(AdvanceBarWordVisual(unsaved,common));
				panelsChanged=menuMotion.Advance(frame);panelsChanged|=themeMotion.Advance(frame);
				active|=menuMotion.Active()||themeMotion.Active();
				UpdateFeedback();
				for(BarSurfaceButtonId id=1;id<=17;++id){
					auto& button=Button(id);const bool visible=(id<=5&&button.IsVisible())
						|| (id>=6&&id<=8&&menuMotion.Visible()) || (id>=9&&themeMotion.Visible());
					bool enabled=!content.switching&&PropertyButtonEnabled(id);if(id>=2&&id<=4)enabled&=content.enabled[id-2];if(id==7)enabled&=!content.pptActive;
					COLORREF text=GetThemeColor(BarThemeColorEnum::TextPrimary);
					if(id>=15){const auto rgb=content.backgroundRgb;const unsigned lum=((rgb>>16)&255)*299+((rgb>>8)&255)*587+(rgb&255)*114;
						text=GetThemeColor(lum>128000?BarThemeModeEnum::Light:BarThemeModeEnum::Dark,BarThemeColorEnum::TextPrimary);}
					const bool selected=id>=9&&(id<15?ThemeColors[id-9]==content.backgroundRgb:static_cast<int>(id-15)==content.pattern);
					RetargetBarButtonInteractionVisual(button,visible,enabled,false,BarButtonHoverTransitionDuration,text,
						id>=9?selected:(id!=1&&id!=3),
						GetThemeColor(BarThemeColorEnum::PressedFill),GetThemeColor(BarThemeColorEnum::TextPrimary));
					if(id>=9)button.icon.color1.reset();
					BarButtonAnimationContext animation{dt,speed,static_cast<bool>(BarUiAnimationEnabled)};
					animation.visible=visible;animation.hoverAllowed=enabled&&id!=1;
					const auto result=AdvanceBarButtonVisual(button,animation,id==3?&pageTotal:nullptr);
					active|=result.active;
					if(result.changed)Include(Pixels({candidate.buttons[id-1].boundsDip.left,candidate.buttons[id-1].boundsDip.top,
						candidate.buttons[id-1].boundsDip.right,candidate.buttons[id-1].boundsDip.bottom},scale));
				}
			}
			const auto oldMenu=menuPose,oldTheme=themePose;
			RefreshGeometry();
			if(changed || panelsChanged || oldMenu!=menuPose || oldTheme!=themePose)FullDamage();
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
		void DrawShape(ID2D1DeviceContext* dc,BarUiShapeClass& shape)
		{DrawBarBackgroundVisual(renderer,dc,shape,Inherit(shape));}
		void SetPose(ID2D1DeviceContext* dc,EraserSurfacePose pose,EraserSurfacePose targetPose={})
		{
			renderer.SetFrameZoom(scale*pose.scale);renderer.SetFontTargetScale(1.0);renderer.SetFontTargetZoom((fontTargetScale>0?fontTargetScale:scale)*targetPose.scale);renderer.SetFrameDiffuseReferenceZoom(scale);renderer.SetFrameDiffuseMaskGeometryScale(1.0/(std::max)(0.001,pose.scale));
			const auto x=static_cast<FLOAT>(logical.left-presentation.left+pose.x*scale),y=static_cast<FLOAT>(logical.top-presentation.top+pose.y*scale);
			auto light=drawingLighting;light.primaryLight.x-=x;light.primaryLight.y-=y;light.cursorLight.x-=x;light.cursorLight.y-=y;
			renderer.SetFrameLightingSnapshot(light);dc->SetTransform(D2D1::Matrix3x2F::Translation(x,y));
		}
		void DrawButton(ID2D1DeviceContext* dc,BarSurfaceButtonId id)
		{
			auto& button=Button(id);BarButtonDrawOptions options;
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
			const bool navigationWaiting=(id==2&&content.navigationWaitingPrevious)
				||(id==4&&content.navigationWaitingNext);
			if(navigationWaiting)options.content=[&](auto& target,auto* context,auto& item,const auto&){
				auto* brush=target.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::TextPrimary),0.9);
				if(!brush)return false;
				const float z=static_cast<float>(target.GetFrameZoom());const auto r=Bounds(item.button);
				const float radius=1.6F*z,y=static_cast<float>((r.top+11)*z);
				// 三个无进度点仅表示命令仍在等待，不替换箭头、旧页码或 ready 状态。
				for(int dot=0;dot<3;++dot){
					const float x=static_cast<float>((r.right-20+dot*5)*z);
					const auto ellipse=D2D1::Ellipse(D2D1::Point2F(x,y),radius,radius);
					context->FillEllipse(&ellipse,brush);
				}
				return true;
			};
			DrawBarButtonVisual(renderer,dc,button,Inherit(button.button),options);
		}
		void DrawGroup(ID2D1DeviceContext* dc,bool child)
		{
			const auto pose=child?themePose:menuPose;
			const double alpha=child?themeFrame.opacity:menuFrame.opacity;
			if(alpha<=0.0001)return;
			SetPose(dc,pose,child?themeFrame.targetPose:menuFrame.targetPose);
			const auto bounds=child?ThemeBounds:MenuBounds;
			const auto pixels=Pixels(bounds,scale*pose.scale);
			const bool layer=alpha<0.9999;
			if(layer)renderer.PushPanelOpacity(dc,D2D1::RectF(static_cast<FLOAT>(pixels.left),static_cast<FLOAT>(pixels.top),
				static_cast<FLOAT>(pixels.right),static_cast<FLOAT>(pixels.bottom)),alpha);
			DrawShape(dc,child?theme:menu);
			if(child){
				DrawShape(dc,divider);
				for(std::size_t i=0;i<headings.size();++i)
					renderer.Word(dc,headings[i],{350,ThemeBounds.top+(i?191.0:5.0)},DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_LEADING);
				for(BarSurfaceButtonId id=9;id<=17;++id)DrawButton(dc,id);
			}else{
				for(BarSurfaceButtonId id=6;id<=8;++id)DrawButton(dc,id);
			}
			if(layer)dc->PopLayer();
		}
	};

	BarPageSurface::BarPageSurface():impl_(std::make_unique<Impl>()){}
	BarPageSurface::~BarPageSurface(){SetSharedLightingSubscribed(false);}
	BarPageSurface::BarPageSurface(BarPageSurface&& other) noexcept
	{
		// 注册表中的对象和 impl 必须在同一边界转移，发布线程不能看到已移空的 owner。
		std::scoped_lock lock(lightingMutex);impl_=std::move(other.impl_);
		for(auto*& item:lightingSubscribers)if(item==&other)item=this;
	}
	BarPageSurface& BarPageSurface::operator=(BarPageSurface&& other) noexcept
	{
		if(this!=&other){
			std::scoped_lock lock(lightingMutex);std::erase(lightingSubscribers,this);
			impl_=std::move(other.impl_);
			for(auto*& item:lightingSubscribers)if(item==&other)item=this;
		}
		return *this;
	}
	bool BarPageSurface::ConfigurePagination(const BarPageSurfaceContent& content,double duration)
	{
		Inkeys::UI::RenderPipeline::FrameStageTimer timer(Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics(),
			Inkeys::UI::RenderPipeline::FrameStage::SubmitTargetsAndLayout);
		impl_->Configure(content,duration);impl_->RefreshGeometry();return true;
	}
	bool BarPageSurface::ConfigureBackground(const BarSurfaceBackgroundSpec& background)
	{
		auto& p=*impl_;p.background=background;p.backgroundOnly=true;p.initialized=true;
		InitializeShape(p.root,{background.bounds.left,background.bounds.top,background.bounds.right,background.bounds.bottom},
			background.useThemeColors?GetThemeColor(BarThemeColorEnum::Surface):background.fill,
			background.cornerRadiusDip,background.fillOpacity,background.frameOpacity);
		p.root.frame->SetDirect(background.useThemeColors?GetThemeColor(BarThemeColorEnum::SurfaceFrame):background.frame);
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
		p.logical=bounds;p.scale=scale;p.renderer.SetFrameZoom(scale);p.RefreshGeometry();p.RefreshLighting();p.FullDamage();return true;
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
	void BarPageSurface::SetHooks(BarSurfaceHooks hooks)
	{std::scoped_lock lock(impl_->inputMutex);impl_->hooks=std::move(hooks);}
	void BarPageSurface::SetSharedLightingSubscribed(bool value) noexcept
	{
		if(!impl_)return;
		{
			std::scoped_lock lock(lightingMutex);if(impl_->subscribed==value)return;
			impl_->subscribed=value;
			if(value)lightingSubscribers.push_back(this);else std::erase(lightingSubscribers,this);
		}
		// 订阅可晚于本帧 Advance/SetBounds；锁外重映射，首帧不等待下一次动画或光源发布。
		if(value){impl_->appliedLightingRevision=0;impl_->RefreshLighting();}
	}
	void BarPageSurface::PublishSharedLighting(const BarSurfaceSharedLighting& value) noexcept
	{
		std::vector<BarSurfaceHooks> targets;
		{
			std::scoped_lock lock(lightingMutex);if(SameLighting(sharedLighting,value))return;
			const auto previous=sharedLighting;sharedLighting=value;++lightingRevision;
			// 只在锁内读取 owner；锁外只保留值回调，Reset/移动/销毁不能留下悬空 impl。
			for(auto* surface:lightingSubscribers)
				if(surface->impl_->NeedsLightingUpdate(previous,value))targets.push_back(surface->impl_->SnapshotHooks());
		}
		for(const auto& hooks:targets){if(hooks.invalidate)hooks.invalidate();if(hooks.wake)hooks.wake();}
	}
	void BarPageSurface::Reset() noexcept
	{
		SetSharedLightingSubscribed(false);impl_->renderer.DiscardDeviceResources();impl_=std::make_unique<Impl>();
	}
	void BarPageSurface::ReleaseDeviceResources() noexcept
	{
		impl_->renderer.DiscardDeviceResources();for(BarSurfaceButtonId id=1;id<=17;++id){impl_->Button(id).icon.ResetCache();impl_->Button(id).pngIcon.ResetCache();}
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
		return {{body.left,body.top,body.right,body.bottom},p.opacity.val,p.root.pct.val,p.layoutRevision,
			!p.root.x.IsSame()||!p.root.y.IsSame()||!p.root.w.IsSame()||!p.root.h.IsSame()};
	}
	RECT BarPageSurface::LogicalBounds() const noexcept{return impl_->logical;}
	RECT BarPageSurface::PresentationBounds() const noexcept{return impl_->presentation;}
	LONG BarPageSurface::PresentationOutsetPixels() const noexcept{return impl_->Outset();}
	HRESULT BarPageSurface::EnsureDeviceResources(const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch,UINT w,UINT h)
	{
		if(impl_->renderer.GetDeviceGeneration()!=epoch.generation){for(BarSurfaceButtonId id=1;id<=17;++id)impl_->Button(id).icon.ResetCache();}
		return impl_->renderer.EnsureDeviceResources(epoch,w,h);
	}
	ID2D1DeviceContext* BarPageSurface::DeviceContext() const noexcept{return impl_->renderer.GetDeviceContext();}
	ID2D1GdiInteropRenderTarget* BarPageSurface::GdiInteropRenderTarget() const noexcept{return impl_->renderer.GetGdiInteropRenderTarget();}
	BarUIRendering& BarPageSurface::Renderer() noexcept{return impl_->renderer;}
	void BarPageSurface::HandleFrameEndDrawResult(HRESULT value){impl_->renderer.HandleFrameEndDrawResult(value);}
	void BarPageSurface::CommitPresented(bool inputEnabled) noexcept
	{
		std::scoped_lock lock(impl_->inputMutex);const auto generation=impl_->committed.generation+1;
		impl_->committed=impl_->candidate;impl_->committed.generation=generation;
		for(auto& button:impl_->committed.buttons)button.enabled&=inputEnabled;
		impl_->committedSerial=impl_->capturedSerial;
	}
	std::uint64_t BarPageSurface::CapturePresentationCandidate() noexcept
	{
		impl_->capturedCandidate=impl_->candidate;
		impl_->capturedDamageRevision=impl_->damageRevision;
		return ++impl_->capturedSerial;
	}
	bool BarPageSurface::IsPresentationCandidateCurrent(std::uint64_t serial) const noexcept
	{
		return serial&&serial==impl_->capturedSerial&&serial>impl_->committedSerial
			&&impl_->capturedDamageRevision==impl_->damageRevision;
	}
	bool BarPageSurface::CommitPresentationCandidate(std::uint64_t serial,bool inputEnabled) noexcept
	{
		if(!serial||serial!=impl_->capturedSerial||serial<=impl_->committedSerial)return false;
		std::scoped_lock lock(impl_->inputMutex);
		const auto generation=impl_->committed.generation+1;
		impl_->committed=impl_->capturedCandidate;impl_->committed.generation=generation;
		for(auto& button:impl_->committed.buttons)button.enabled&=inputEnabled;
		impl_->committedSerial=serial;
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
		for(std::size_t i=0;i<c.panels.size();++i)if(c.panelVisible[i]&&RoundedContains(c.panels[i],c.panelRadii[i],point.x/c.scale,point.y/c.scale))return true;
		return false;
	}
	BarSurfaceButtonId BarPageSurface::HitTest(POINT point) const noexcept
	{
		std::scoped_lock lock(impl_->inputMutex);const auto& c=impl_->committed;if(!c.enabled)return 0;
		return Impl::HitButton(c,point);
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
		const int panel=Impl::FrontPanel(c,result.logical.x/c.scale,result.logical.y/c.scale);
		result.background=panel>=0;result.primaryBackground=panel==0;result.popupBackground=panel>0;
		result.hit=Impl::HitButton(c,result.logical);
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
	bool BarPageSurface::PopupAnimationActive() const noexcept{return impl_->menuMotion.Active()||impl_->themeMotion.Active();}
	BarSurfaceRenderResult BarPageSurface::Render(ID2D1DeviceContext* dc,std::chrono::steady_clock::time_point now)
	{
		if(!dc)return {};auto& p=*impl_;p.Advance(now);
		if(auto* diagnostics=Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics()){
			const auto light=p.renderer.SnapshotFrameLighting();
			diagnostics->lightFlags=(light.edgeLightingEnabled?1U:0U)|(light.primaryLightVisible?2U:0U)
				|(light.cursorLightVisible?4U:0U)|(light.cursorIntensity>0.0001F?8U:0U);
		}
		p.drawingLighting=p.renderer.SnapshotFrameLighting();
		D2D1_MATRIX_3X2_F original;dc->GetTransform(&original);p.SetPose(dc,{});
		const float alpha=static_cast<FLOAT>(p.opacity.val);const bool layer=alpha<0.9999F;
		if(layer)p.renderer.PushPanelOpacity(dc,D2D1::RectF(static_cast<FLOAT>(p.presentation.left-p.logical.left),static_cast<FLOAT>(p.presentation.top-p.logical.top),
			static_cast<FLOAT>(p.presentation.right-p.logical.left),static_cast<FLOAT>(p.presentation.bottom-p.logical.top)),alpha);
		// 后层先画：主题位于菜单下，菜单位于分页栏下；沿用共享面板的当前姿态。
		if(!p.backgroundOnly){
			if(p.themeFrame.visible)p.DrawGroup(dc,true);
			if(p.menuFrame.visible)p.DrawGroup(dc,false);
			p.SetPose(dc,{});
		}
		p.DrawShape(dc,p.root);
		if(p.backgroundOnly&&p.background.drawContent){
			dc->SetTransform(D2D1::Matrix3x2F::Scale(p.scale,p.scale)*D2D1::Matrix3x2F::Translation(static_cast<FLOAT>(p.logical.left-p.presentation.left),static_cast<FLOAT>(p.logical.top-p.presentation.top)));
			p.background.drawContent(dc,D2D1::RectF(static_cast<FLOAT>(p.background.bounds.left),static_cast<FLOAT>(p.background.bounds.top),static_cast<FLOAT>(p.background.bounds.right),static_cast<FLOAT>(p.background.bounds.bottom)));p.SetPose(dc,{});
		}
		if(!p.backgroundOnly){
			if(!p.content.whiteboard){
				const auto r=Bounds(p.pageButtons[0]->button);const bool vertical=r.Width()<=r.Height();
				SetShapeBounds(p.dragHandle,{EraserRectCenterX(r)-(vertical?1:10),EraserRectCenterY(r)-(vertical?10:1),
					EraserRectCenterX(r)+(vertical?1:10),EraserRectCenterY(r)+(vertical?10:1)},0,true);p.DrawShape(dc,p.dragHandle);
			}
			for(const auto id:p.paginationDrawOrder)p.DrawButton(dc,id);
			if(p.content.whiteboard&&p.content.surface==0&&p.content.unsaved)p.renderer.Word(dc,p.unsaved,{5,-32},DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_LEADING);
		}
		if(layer)dc->PopLayer();dc->SetTransform(original);p.renderer.SetFrameZoom(p.scale);p.renderer.SetFrameDiffuseMaskGeometryScale(1);
		p.renderer.SetFrameLightingSnapshot(p.drawingLighting);
		return {true,p.active||p.renderer.FontAnimationsActive(),p.invalidated,p.damage};
	}
}
