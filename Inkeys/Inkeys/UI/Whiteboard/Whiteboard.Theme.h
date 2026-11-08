#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cmath>

namespace Inkeys::UI::WhiteboardVisual
{
	// 显示和点击使用同一顺序，避免颜色重排后请求到旧色值。
	inline constexpr std::array<std::uint32_t,6> ThemeColors{
		0x123B32,0x172A46,0x000000,0xFFFFFF,0xE5E7EB,0xF5F1E6};

	// 主栏主题预览绘制纹理（预览的底色、圆角和边框由标准 UI3 Shape 绘制），同时是画布背景纹理的几何基准：
	// 24 DIP 间距、1 DIP 点半径/线宽、18% 黑/白、以 bounds 中心为相位。画布背景见 DrawThemePatternPhase。
	inline void DrawThemePattern(ID2D1DeviceContext* context,D2D1_RECT_F bounds,
		std::uint32_t rgb,int pattern,float scale=1,ID2D1SolidColorBrush* sharedBrush=nullptr)
	{
		if(!context || !pattern || bounds.right<=bounds.left || bounds.bottom<=bounds.top)return;
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> local;
		const unsigned luminance=((rgb>>16)&255)*299+((rgb>>8)&255)*587+(rgb&255)*114;
		const auto color=D2D1::ColorF(luminance>128000?0x000000:0xffffff,0.18F);
		if(!sharedBrush){if(FAILED(context->CreateSolidColorBrush(color,&local)))return;sharedBrush=local.Get();}
		const auto previous=sharedBrush->GetColor();sharedBrush->SetColor(color);
		context->PushAxisAlignedClip(bounds,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		const float step=24*scale,centerX=(bounds.left+bounds.right)/2,centerY=(bounds.top+bounds.bottom)/2;
		const float startX=centerX-std::floor((centerX-bounds.left)/step)*step;
		const float startY=centerY-std::floor((centerY-bounds.top)/step)*step;
		for(float x=startX;x<bounds.right;x+=step){
			if(pattern==2)context->DrawLine({x,bounds.top},{x,bounds.bottom},sharedBrush,scale);
			else for(float y=startY;y<bounds.bottom;y+=step)
				context->FillEllipse(D2D1::Ellipse({x,y},scale,scale),sharedBrush);
		}
		if(pattern==2)for(float y=startY;y<bounds.bottom;y+=step)
			context->DrawLine({bounds.left,y},{bounds.right,y},sharedBrush,scale);
		context->PopAxisAlignedClip();sharedBrush->SetColor(previous);
	}
	// 带视口相位的画布背景纹理（DIP 空间，scale 恒为 1）。phaseX/Y 是 bounds 左上角起首个格点的偏移，范围 [0,24)；
	// 视口为 (0,0) 时相位等于 DrawThemePattern 的 startX/startY，内部格点位置逐位相同。格点按序号直接计算，
	// 不做浮点累加，并多画一圈落在边缘外 <半宽+AA 的点/线，避免平移时边缘点突然出现或消失
	// （旧实现只画“≥left 且 <right”的格点，右/下边缘上的半个点缺失）。
	inline void DrawThemePatternPhase(ID2D1DeviceContext* context,D2D1_RECT_F bounds,
		std::uint32_t rgb,int pattern,double phaseX,double phaseY,ID2D1SolidColorBrush* sharedBrush=nullptr)
	{
		if(!context || !pattern || bounds.right<=bounds.left || bounds.bottom<=bounds.top
			|| !std::isfinite(phaseX) || !std::isfinite(phaseY))return;
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> local;
		const unsigned luminance=((rgb>>16)&255)*299+((rgb>>8)&255)*587+(rgb&255)*114;
		const auto color=D2D1::ColorF(luminance>128000?0x000000:0xffffff,0.18F);
		if(!sharedBrush){if(FAILED(context->CreateSolidColorBrush(color,&local)))return;sharedBrush=local.Get();}
		const auto previous=sharedBrush->GetColor();sharedBrush->SetColor(color);
		context->PushAxisAlignedClip(bounds,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		constexpr double step=24.0,margin=2.0; // 点半径/线半宽 1 DIP 加 AA 余量
		const double left=bounds.left,top=bounds.top;
		const long long firstX=static_cast<long long>(std::ceil((-margin-phaseX)/step));
		const long long lastX=static_cast<long long>(std::floor((bounds.right+margin-left-phaseX)/step));
		const long long firstY=static_cast<long long>(std::ceil((-margin-phaseY)/step));
		const long long lastY=static_cast<long long>(std::floor((bounds.bottom+margin-top-phaseY)/step));
		for(long long ix=firstX;ix<=lastX;++ix){
			const float x=static_cast<float>(left+phaseX+static_cast<double>(ix)*step);
			if(pattern==2)context->DrawLine({x,bounds.top},{x,bounds.bottom},sharedBrush,1.0F);
			else for(long long iy=firstY;iy<=lastY;++iy)
				context->FillEllipse(D2D1::Ellipse({x,static_cast<float>(top+phaseY+static_cast<double>(iy)*step)},1.0F,1.0F),sharedBrush);
		}
		if(pattern==2)for(long long iy=firstY;iy<=lastY;++iy){
			const float y=static_cast<float>(top+phaseY+static_cast<double>(iy)*step);
			context->DrawLine({bounds.left,y},{bounds.right,y},sharedBrush,1.0F);
		}
		context->PopAxisAlignedClip();sharedBrush->SetColor(previous);
	}
	// 视口 (0,0) 的旧 Freeze 整幅背景：纯色底加 DrawThemePattern。生产路径用 DrawThemePatternPhase，
	// 本函数保留为像素对照基准（--page-control-hidden-test / --draw3-whiteboard-pattern-test）。
	inline void DrawTheme(ID2D1DeviceContext* context,D2D1_RECT_F bounds,
		std::uint32_t rgb,int pattern)
	{
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
		if(!context || FAILED(context->CreateSolidColorBrush(D2D1::ColorF(rgb),&brush)))return;
		context->FillRectangle(bounds,brush.Get());
		DrawThemePattern(context,bounds,rgb,pattern,1,brush.Get());
	}
	// 纯色整幅背景：Draw3 归属后备方案下 Freeze 只画底色；纹理由 Draw3 画布按视口绘制。
	inline void DrawThemeSolid(ID2D1DeviceContext* context,D2D1_RECT_F bounds,std::uint32_t rgb)
	{
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
		if(!context || FAILED(context->CreateSolidColorBrush(D2D1::ColorF(rgb),&brush)))return;
		context->FillRectangle(bounds,brush.Get());
	}
}
