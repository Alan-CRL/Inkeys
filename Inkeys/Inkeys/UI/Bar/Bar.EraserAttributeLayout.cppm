module;
#include <algorithm>
#include <array>
#include <cmath>
#include "../../Drawing/Draw3/Draw3.SpeedEraser.h"

export module Inkeys.UI.Bar.EraserAttributeLayout;
export import Inkeys.UI.Bar.Metrics;

export namespace Inkeys::UI::Bar
{
	struct EraserAttributeRect
	{
		double left = 0, top = 0, right = 0, bottom = 0;
		double Width() const noexcept { return right - left; }
		double Height() const noexcept { return bottom - top; }
		bool Contains(double x, double y) const noexcept
		{ return x >= left && x < right && y >= top && y < bottom; }
		friend bool operator==(const EraserAttributeRect&, const EraserAttributeRect&) = default;
	};
	enum class EraserAttributeItem : int
	{
		Clear, Small, Medium, Large, Automatic, Extension, Low, Normal, High, Settings,
		Count, None = -1
	};
	struct EraserAttributeLayout
	{
		EraserAttributeRect panel, menu, previewRegion, automatic, automaticDivider, menuTitle, anchor, work;
		std::array<EraserAttributeRect,3> previews{}, previewClips{};
		std::array<EraserAttributeRect, 10> items{};
		std::array<EraserAttributeRect, 2> dividers{};
		std::array<double, 3> previewDiameters{};
		bool below = false, reversed = false, menuBelow = false, horizontalOverflow = false;
		friend bool operator==(const EraserAttributeLayout&, const EraserAttributeLayout&) = default;
	};
	struct EraserAttributeLayoutInput
	{
		EraserAttributeRect main, anchor, work;
		double dpiScale = 1, zoom = 1;
		bool below = false, reversed = false;
		int lockedMenuSide = -1;
		// 直拖或方向过渡沿用已呈现侧，不能因临时工作区坐标立即翻边。
		bool lockPanelSide = false;
		// 测试可以注入大于面板高度的直径，生产始终传三档基础尺寸。
		std::array<float, 3> diametersDip{
			static_cast<float>(Inkeys::Drawing::Draw3::SpeedEraser::BaseSizePresets[0]),
			static_cast<float>(Inkeys::Drawing::Draw3::SpeedEraser::BaseSizePresets[1]),
			static_cast<float>(Inkeys::Drawing::Draw3::SpeedEraser::BaseSizePresets[2])};
	};
	inline EraserAttributeRect OffsetEraserRect(EraserAttributeRect r, double x, double y) noexcept
	{ return {r.left+x, r.top+y, r.right+x, r.bottom+y}; }
	inline EraserAttributeRect IntersectEraserRect(EraserAttributeRect a, EraserAttributeRect b) noexcept
	{
		return { (std::max)(a.left,b.left), (std::max)(a.top,b.top),
			(std::max)((std::max)(a.left,b.left),(std::min)(a.right,b.right)),
			(std::max)((std::max)(a.top,b.top),(std::min)(a.bottom,b.bottom)) };
	}
	inline int ResolveEraserAttributeRelease(int pressed,int hit,bool inside,bool cancelled) noexcept
	{
		if(cancelled || !inside || pressed<0)return -1;
		const bool automatic=(pressed==4 || pressed==5) && (hit==4 || hit==5);
		return hit==pressed || automatic?pressed:-1;
	}
	inline double EraserRectCenterX(EraserAttributeRect r) noexcept { return (r.left+r.right)/2; }
	inline double EraserRectCenterY(EraserAttributeRect r) noexcept { return (r.top+r.bottom)/2; }
	inline bool EraserAttributeCircleContains(EraserAttributeRect circle,double expansion,double x,double y) noexcept
	{
		const double radius=(std::max)(0.0,circle.Width()/2+expansion);
		const double dx=x-EraserRectCenterX(circle),dy=y-EraserRectCenterY(circle);
		return dx*dx+dy*dy<=radius*radius;
	}
	inline constexpr double EraserAttributeCircleGapDip=16.0;
	inline constexpr double EraserAttributeStandardGapDip=5.0;
	inline constexpr double EraserAttributeAutomaticArrowWidthDip=20.0;
	inline EraserAttributeLayout ResolveEraserAttributeLayout(const EraserAttributeLayoutInput& input) noexcept
	{
		using namespace Inkeys::Drawing::Draw3::SpeedEraser;
		EraserAttributeLayout r;r.anchor=input.anchor;r.work=input.work;
		const double zoom=std::isfinite(input.zoom) && input.zoom>0?input.zoom:1;
		const double dpi=std::isfinite(input.dpiScale) && input.dpiScale>0?input.dpiScale:1;
		const double height=input.main.Height()>0?input.main.Height():BarMainBarHeightDip;
		const double button=BarButtonTwoSideDip,arrow=EraserAttributeAutomaticArrowWidthDip;
		const double autoWidth=button+arrow,available=(std::max)(1.0,input.work.Width());
		DisplayScale display;display.dipPerPixelX=display.dipPerPixelY=static_cast<float>(1/dpi);
		double diameters=0;
		for(size_t i=0;i<3;++i)
		{
			// 工具DIP先到画布像素，再除UI父倍率；稳定展开恢复为同一个实际直径。
			r.previewDiameters[i]=DiameterToCanvasPx(input.diametersDip[i],display)/zoom;
			diameters+=r.previewDiameters[i];
		}
		const double stroke=BarButtonFrameThicknessDip;
		const double fixedContent=diameters+button+autoWidth+stroke*2;
		double gap=EraserAttributeStandardGapDip,circleGap=EraserAttributeCircleGapDip;
		// 极窄时先等量压缩四段5 DIP，再压缩圆组四段16 DIP；按钮和圆始终保持真实尺寸。
		if(fixedContent+circleGap*4+gap*4>available)
		{
			gap=(std::clamp)((available-fixedContent-circleGap*4)/4,0.0,EraserAttributeStandardGapDip);
			if(gap<=0)circleGap=(std::clamp)((available-fixedContent)/4,0.0,EraserAttributeCircleGapDip);
		}
		const double circleSide=diameters+circleGap*4;
		const double automaticSide=gap+autoWidth+gap;
		const double leftSide=input.reversed?automaticSide:circleSide;
		const double rightSide=input.reversed?circleSide:automaticSide;
		const double naturalWidth=leftSide+stroke+gap+button+gap+stroke+rightSide;
		const double halfClear=(std::min)(button/2,available/2);
		const double clearCenter=(std::clamp)(EraserRectCenterX(input.anchor),input.work.left+halfClear,input.work.right-halfClear);
		const double idealX=clearCenter-(leftSide+stroke+gap+button/2);
		r.horizontalOverflow=naturalWidth>available || idealX<input.work.left || idealX+naturalWidth>input.work.right;
		const double panelGap=BarMainButtonToMainBarGapDip;
		const double down=input.work.bottom-input.main.bottom-panelGap,up=input.main.top-input.work.top-panelGap;
		r.below=input.below;r.reversed=input.reversed;
		if(!input.lockPanelSide && (r.below?down:up)<height && (r.below?up:down)>(r.below?down:up))r.below=!r.below;
		const double y=(std::clamp)(r.below?input.main.bottom+panelGap:input.main.top-panelGap-height,
			input.work.top,(std::max)(input.work.top,input.work.bottom-height));
		r.panel={(std::max)(idealX,input.work.left),y,(std::min)(idealX+naturalWidth,input.work.right),y+height};
		const double cy=y+height/2,bh=button;
		const double clearLeft=idealX+leftSide+stroke+gap;
		r.items[0]={clearLeft,cy-bh/2,clearLeft+button,cy+bh/2};
		const double dh=button-BarButtonGapDip*4;
		r.dividers[0]={idealX+leftSide,cy-dh/2,idealX+leftSide+stroke,cy+dh/2};
		r.dividers[1]={r.items[0].right+gap,cy-dh/2,r.items[0].right+gap+stroke,cy+dh/2};
		const double leftStart=idealX,rightStart=r.dividers[1].right;
		const double circleStart=r.reversed?rightStart:leftStart,autoStart=r.reversed?leftStart:rightStart;
		const double circleEnd=circleStart+circleSide;
		r.previewRegion=IntersectEraserRect({circleStart,y,circleEnd,y+height},r.panel);
		double cursor=circleStart+circleGap;
		for(size_t i=0;i<3;++i)
		{
			const double d=r.previewDiameters[i],center=cursor+d/2;
			r.previews[i]={center-d/2,cy-d/2,center+d/2,cy+d/2};cursor+=d+circleGap;
		}
		for(size_t i=0;i<3;++i)
		{
			const auto circle=r.previews[i];
			const double lower=i==0?circleStart:(r.previews[i-1].right+circle.left)/2;
			const double upper=i==2?circleEnd:(circle.right+r.previews[i+1].left)/2;
			r.previewClips[i]=IntersectEraserRect({lower,y,upper,y+height},r.panel);
			const double hitRadius=circle.Width()/2+BarButtonGapDip;
			const double center=EraserRectCenterX(circle);
			r.items[i+1]=IntersectEraserRect({center-hitRadius,cy-hitRadius,center+hitRadius,cy+hitRadius},r.previewClips[i]);
		}
		const double ax=autoStart+gap;
		r.automatic={ax,cy-bh/2,ax+autoWidth,cy+bh/2};
		// 倒转只移动整个分组，A和右侧箭头不镜像，两个动作区保持原语义。
		r.items[4]={ax,cy-bh/2,ax+button,cy+bh/2};r.items[5]={ax+button,cy-bh/2,ax+autoWidth,cy+bh/2};
		// 内部分割线与主栏分割线使用同一高度并保持垂直居中。
		r.automaticDivider={ax+button-stroke/2,r.dividers[0].top,
			ax+button+stroke/2,r.dividers[0].bottom};
		const double menuWidth=(std::min)(available,button*2+BarButtonOneSideDip+BarButtonGapDip*2);
		const double row=BarButtonOneSideDip,padding=BarButtonGapDip*2,mg=BarButtonGapDip;
		const double menuHeight=row*2+padding*2+mg;
		r.menuBelow=input.lockedMenuSide<0?r.below:input.lockedMenuSide!=0;
		if(input.lockedMenuSide<0)
		{
			const double lower=input.work.bottom-r.panel.bottom-mg,upper=r.panel.top-input.work.top-mg;
			if((r.menuBelow?lower:upper)<menuHeight && (r.menuBelow?upper:lower)>(r.menuBelow?lower:upper))r.menuBelow=!r.menuBelow;
		}
		const double mx=(std::clamp)(EraserRectCenterX(r.automatic)-menuWidth/2,input.work.left,(std::max)(input.work.left,input.work.right-menuWidth));
		const double my=(std::clamp)(r.menuBelow?r.panel.bottom+mg:r.panel.top-mg-menuHeight,
			input.work.top,(std::max)(input.work.top,input.work.bottom-menuHeight));
		r.menu={mx,my,mx+menuWidth,my+menuHeight};
		r.items[9]={r.menu.right-padding-row,my+padding,r.menu.right-padding,my+padding+row};
		// 标题行在浮窗顶端与灵敏度按钮上沿之间等距，水平范围保持原有左对齐。
		const double sensitivityButtonTop=my+padding+row+mg;
		const double titleTop=my+(sensitivityButtonTop-my-row)/2;
		r.menuTitle={mx+padding,titleTop,r.items[9].left-mg,titleTop+row};
		const double segment=(menuWidth-padding*2-mg*2)/3;
		for(size_t i=0;i<3;++i)r.items[6+i]={mx+padding+i*(segment+mg),sensitivityButtonTop,
			mx+padding+i*(segment+mg)+segment,my+padding+row*2+mg};
		return r;
	}
}
