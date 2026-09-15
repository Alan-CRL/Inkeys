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
		EraserAttributeRect panel, menu, previewRegion;
		std::array<EraserAttributeRect, 10> items{};
		std::array<EraserAttributeRect, 4> dividers{};
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
		std::array<float, 3> diametersDip{16, 32, 64};
	};
	inline EraserAttributeRect OffsetEraserRect(EraserAttributeRect r, double x, double y) noexcept
	{ return {r.left+x, r.top+y, r.right+x, r.bottom+y}; }
	inline EraserAttributeRect IntersectEraserRect(EraserAttributeRect a, EraserAttributeRect b) noexcept
	{
		return { (std::max)(a.left,b.left), (std::max)(a.top,b.top),
			(std::max)((std::max)(a.left,b.left),(std::min)(a.right,b.right)),
			(std::max)((std::max)(a.top,b.top),(std::min)(a.bottom,b.bottom)) };
	}
	inline EraserAttributeLayout ResolveEraserAttributeLayout(const EraserAttributeLayoutInput& input) noexcept
	{
		using namespace Inkeys::Drawing::Draw3::SpeedEraser;
		EraserAttributeLayout result;
		const double zoom = std::isfinite(input.zoom) && input.zoom > 0 ? input.zoom : 1;
		const double dpi = std::isfinite(input.dpiScale) && input.dpiScale > 0 ? input.dpiScale : 1;
		const double height = input.main.Height() > 0 ? input.main.Height() : BarMainBarHeightDip;
		double gap = BarButtonGapDip;
		const double button = BarButtonTwoSideDip, arrow = BarButtonOneSideDip;
		std::array<double,3> slots{};
		DisplayScale display; display.dipPerPixelX = display.dipPerPixelY = static_cast<float>(1 / dpi);
		for (size_t i=0;i<slots.size();++i)
		{
			// 先转换实际像素再除父级zoom：自定义UI缩放不能缩放真实DIP光标。
			result.previewDiameters[i] = DiameterToCanvasPx(input.diametersDip[i],display) / zoom;
			slots[i] = (std::max)(button,result.previewDiameters[i] + 2*gap);
		}
		const double naturalCenter = slots[0]+slots[1]+slots[2];
		const double available = (std::max)(1.0,input.work.Width());
		const double fixed = button*2+arrow+BarButtonFrameThicknessDip*2;
		const double naturalWidth = fixed + naturalCenter + gap*9;
		// 先收弹性留白，仍不足只裁预览槽位，端部控件和圆的直径均不缩放。
		if (naturalWidth > available) gap = (std::max)(0.0,(available-fixed-naturalCenter)/9);
		const double center = (std::max)(0.0,(std::min)(naturalCenter,available-fixed-gap*9));
		result.horizontalOverflow = center < naturalCenter;
		const double width = fixed + center + gap*9;
		double x = input.reversed ? input.anchor.right-width : input.anchor.left;
		x = (std::clamp)(x,input.work.left,(std::max)(input.work.left,input.work.right-width));
		const double panelGap = BarMainButtonToMainBarGapDip;
		const double lower = input.work.bottom-input.main.bottom-panelGap;
		const double upper = input.main.top-input.work.top-panelGap;
		result.below = input.below;
		if ((result.below ? lower : upper) < height && (result.below ? upper : lower) > (result.below ? lower : upper))
			result.below = !result.below;
		double y = result.below ? input.main.bottom+panelGap : input.main.top-panelGap-height;
		y = (std::clamp)(y,input.work.top,(std::max)(input.work.top,input.work.bottom-height));
		result.panel = {x,y,x+width,y+height}; result.reversed=input.reversed;
		const double bh=(std::min)(button,(std::max)(1.0,height-2*BarButtonGapDip));
		const double by=(height-bh)/2;
		double cursor=gap;
		result.items[0]={cursor,by,cursor+button,by+bh}; cursor+=button+gap;
		const double dividerHeight = button-BarButtonGapDip*4;
		auto divider=[&](size_t i)
		{
			cursor+=gap;
			result.dividers[i]={cursor,(height-dividerHeight)/2,cursor+BarButtonFrameThicknessDip,(height+dividerHeight)/2};
			cursor+=BarButtonFrameThicknessDip+gap*2;
		};
		divider(0);
		const double previewLeft=cursor;
		for(size_t i=0;i<slots.size();++i)
		{
			const double w=slots[i]*center/naturalCenter;
			result.items[i+1]={cursor,by,cursor+w,by+bh}; cursor+=w;
		}
		result.previewRegion={previewLeft,0,cursor,height};
		divider(1);
		result.items[4]={cursor,by,cursor+button,by+bh};cursor+=button;
		result.items[5]={cursor,by,cursor+arrow,by+bh};
		result.dividers[2]={cursor,(height-dividerHeight)/2,cursor+BarButtonFrameThicknessDip,(height+dividerHeight)/2};
		auto place=[&](EraserAttributeRect r)
		{
			if(result.reversed) r={width-r.right,r.top,width-r.left,r.bottom};
			return OffsetEraserRect(r,x,y);
		};
		for(size_t i=0;i<6;++i)result.items[i]=place(result.items[i]);
		for(size_t i=0;i<3;++i)result.dividers[i]=place(result.dividers[i]);
		result.previewRegion=place(result.previewRegion);
		const double menuWidth=(std::min)(available,button*3+BarButtonGapDip*4);
		const double row=BarButtonOneSideDip, mg=BarButtonGapDip;
		const double menuHeight=row*3+mg*5+BarButtonFrameThicknessDip;
		result.menuBelow=input.lockedMenuSide<0 ? result.below : input.lockedMenuSide!=0;
		if(input.lockedMenuSide<0)
		{
			const double down=input.work.bottom-result.panel.bottom-mg;
			const double up=result.panel.top-input.work.top-mg;
			if((result.menuBelow?down:up)<menuHeight && (result.menuBelow?up:down)>(result.menuBelow?down:up))
				result.menuBelow=!result.menuBelow;
		}
		const double mx=(std::clamp)(result.items[5].right-menuWidth,input.work.left,
			(std::max)(input.work.left,input.work.right-menuWidth));
		const double my=(std::clamp)(result.menuBelow?result.panel.bottom+mg:result.panel.top-mg-menuHeight,
			input.work.top,(std::max)(input.work.top,input.work.bottom-menuHeight));
		result.menu={mx,my,mx+menuWidth,my+menuHeight};
		const double segment=(menuWidth-4*mg)/3;
		for(size_t i=0;i<3;++i)result.items[6+i]={mx+mg+i*(segment+mg),my+mg+row,
			mx+mg+i*(segment+mg)+segment,my+mg+row*2};
		const double dividerY=my+mg*2+row*2;
		result.dividers[3]={mx+mg,dividerY,mx+menuWidth-mg,dividerY+BarButtonFrameThicknessDip};
		result.items[9]={mx+mg,dividerY+BarButtonFrameThicknessDip+mg,mx+menuWidth-mg,my+menuHeight-mg};
		return result;
	}
}
