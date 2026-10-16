#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include <cstdint>
#include <cmath>

namespace Inkeys::UI::WhiteboardVisual
{
	// 预览和整幅背景共用纹理相位；预览的底色、圆角和边框由标准 UI3 Shape 绘制。
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
	inline void DrawTheme(ID2D1DeviceContext* context,D2D1_RECT_F bounds,
		std::uint32_t rgb,int pattern)
	{
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
		if(!context || FAILED(context->CreateSolidColorBrush(D2D1::ColorF(rgb),&brush)))return;
		context->FillRectangle(bounds,brush.Get());
		DrawThemePattern(context,bounds,rgb,pattern,1,brush.Get());
	}
}
