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
		EraserAttributeRect panel, menu, previewRegion, automatic, menuTitle, anchor, work;
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
	inline EraserAttributeLayout ResolveEraserAttributeLayout(const EraserAttributeLayoutInput& input) noexcept
	{
		using namespace Inkeys::Drawing::Draw3::SpeedEraser;
		EraserAttributeLayout r;r.anchor=input.anchor;r.work=input.work;
		const double zoom=std::isfinite(input.zoom) && input.zoom>0?input.zoom:1;
		const double dpi=std::isfinite(input.dpiScale) && input.dpiScale>0?input.dpiScale:1;
		const double height=input.main.Height()>0?input.main.Height():BarMainBarHeightDip;
		const double button=BarButtonTwoSideDip,arrow=BarButtonOneSideDip;
		const double autoWidth=button+arrow,available=(std::max)(1.0,input.work.Width());
		DisplayScale display;display.dipPerPixelX=display.dipPerPixelY=static_cast<float>(1/dpi);
		double diameters=0;
		for(size_t i=0;i<3;++i)
		{
			// 工具DIP先到画布像素，再除UI父倍率；稳定展开恢复为同一个实际直径。
			r.previewDiameters[i]=DiameterToCanvasPx(input.diametersDip[i],display)/zoom;
			diameters+=r.previewDiameters[i];
		}
		constexpr double circleEdgeGap=16.0; // 两个圆边缘之间的UI留白，不是圆心间距。
		double gap=BarButtonGapDip;
		const double naturalSide=(std::max)(diameters+circleEdgeGap*2,autoWidth);
		const double fixed=button+BarButtonFrameThicknessDip*2;
		if(fixed+naturalSide*2+gap*6>available)
			gap=(std::clamp)((available-fixed-autoWidth*2)/6,0.0,gap);
		const double side=(std::max)(0.0,(std::min)(naturalSide,(available-fixed-gap*6)/2));
		const double edgeGap=(std::clamp)((side-diameters)/2,0.0,circleEdgeGap);
		r.horizontalOverflow=diameters>side;
		const double width=(std::min)(available,fixed+side*2+gap*6);
		const double x=(std::clamp)(EraserRectCenterX(input.anchor)-width/2,input.work.left,(std::max)(input.work.left,input.work.right-width));
		const double panelGap=BarMainButtonToMainBarGapDip;
		const double down=input.work.bottom-input.main.bottom-panelGap,up=input.main.top-input.work.top-panelGap;
		r.below=input.below;r.reversed=input.reversed;
		if((r.below?down:up)<height && (r.below?up:down)>(r.below?down:up))r.below=!r.below;
		const double y=(std::clamp)(r.below?input.main.bottom+panelGap:input.main.top-panelGap-height,
			input.work.top,(std::max)(input.work.top,input.work.bottom-height));
		r.panel={x,y,x+width,y+height};const double cy=y+height/2,cx=x+width/2;
		const double bh=(std::min)(button,(std::max)(1.0,height-2*BarButtonGapDip));
		r.items[0]={cx-button/2,cy-bh/2,cx+button/2,cy+bh/2};
		const double dh=button-BarButtonGapDip*4,stroke=BarButtonFrameThicknessDip;
		r.dividers[0]={r.items[0].left-gap-stroke,cy-dh/2,r.items[0].left-gap,cy+dh/2};
		r.dividers[1]={r.items[0].right+gap,cy-dh/2,r.items[0].right+gap+stroke,cy+dh/2};
		const double leftStart=x+gap,rightStart=r.dividers[1].right+gap;
		const double circleStart=r.reversed?rightStart:leftStart,autoStart=r.reversed?leftStart:rightStart;
		r.previewRegion={circleStart,y,circleStart+side,y+height};
		const double naturalCircles=diameters+edgeGap*2;
		double cursor=circleStart+(side-naturalCircles)/2;
		for(size_t i=0;i<3;++i)
		{
			const double d=r.previewDiameters[i];double center=cursor+d/2;
			if(r.horizontalOverflow)
				center=circleStart+side*(i+0.5)/3; // 极窄区域只裁各圆的溢出，不改外直径。
			r.previews[i]={center-d/2,cy-d/2,center+d/2,cy+d/2};cursor+=d+edgeGap;
		}
		for(size_t i=0;i<3;++i)
		{
			const auto circle=r.previews[i];
			const double lower=i==0?circleStart-gap:(r.previews[i-1].right+circle.left)/2;
			const double upper=i==2?circleStart+side+gap:(circle.right+r.previews[i+1].left)/2;
			const double clipLeft=r.horizontalOverflow?circleStart+side*i/3:lower;
			const double clipRight=r.horizontalOverflow?circleStart+side*(i+1)/3:upper;
			r.previewClips[i]=IntersectEraserRect({clipLeft,y,clipRight,y+height},r.panel);
			const double hitRadius=(std::max)(BarButtonOneSideDip/2,circle.Width()/2+BarButtonGapDip);
			const double center=EraserRectCenterX(circle);
			r.items[i+1]=IntersectEraserRect({center-hitRadius,cy-hitRadius,center+hitRadius,cy+hitRadius},r.previewClips[i]);
		}
		const double ax=autoStart+(side-autoWidth)/2;
		r.automatic={ax,cy-bh/2,ax+autoWidth,cy+bh/2};
		// 倒转只移动整个分组，A和右侧箭头不镜像，两个动作区保持原语义。
		r.items[4]={ax,cy-bh/2,ax+button,cy+bh/2};r.items[5]={ax+button,cy-bh/2,ax+autoWidth,cy+bh/2};
		const double menuWidth=(std::min)(available,button*2+arrow+BarButtonGapDip*2);
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
		r.menuTitle={mx+padding,my+padding,r.items[9].left-mg,my+padding+row};
		const double segment=(menuWidth-padding*2-mg*2)/3;
		for(size_t i=0;i<3;++i)r.items[6+i]={mx+padding+i*(segment+mg),my+padding+row+mg,
			mx+padding+i*(segment+mg)+segment,my+padding+row*2+mg};
		return r;
	}
}
