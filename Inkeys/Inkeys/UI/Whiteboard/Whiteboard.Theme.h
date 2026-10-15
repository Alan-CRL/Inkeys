#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include <cstdint>
#include <cmath>

namespace Inkeys::UI::WhiteboardVisual
{
	struct ThemeDrawingResources
	{
		Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
		Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> clip;
		D2D1_RECT_F clipBounds{};
	};
	inline void DrawTheme(ID2D1DeviceContext* context, D2D1_RECT_F bounds,
		std::uint32_t rgb, int pattern, bool preview = false, bool selected = false, ThemeDrawingResources* cached = nullptr, bool labelled = false)
	{
		(void)selected; // 选中由共享 Bar SVG 呈现，背景绘制不再生成另一套勾。
		ThemeDrawingResources local;
		auto& resources=cached?*cached:local;
		if (resources.context.Get()!=context) {
			resources.context=context; resources.brush.Reset(); resources.clip.Reset();
		}
		if (!resources.brush && FAILED(context->CreateSolidColorBrush(D2D1::ColorF(rgb),&resources.brush))) return;
		auto* brush=resources.brush.Get(); brush->SetColor(D2D1::ColorF(rgb));
		if (preview) {
			const auto& old=resources.clipBounds;
			if (!resources.clip || old.left!=bounds.left || old.top!=bounds.top || old.right!=bounds.right || old.bottom!=bounds.bottom) {
				Microsoft::WRL::ComPtr<ID2D1Factory> factory; context->GetFactory(&factory);
				resources.clip.Reset(); resources.clipBounds=bounds;
				(void)factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(bounds,4,4),&resources.clip);
			}
			if (resources.clip) context->PushLayer(D2D1::LayerParameters(bounds,resources.clip.Get()),nullptr);
		}
		context->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		context->FillRectangle(bounds, brush);
		const unsigned luminance = ((rgb >> 16) & 255) * 299
			+ ((rgb >> 8) & 255) * 587 + (rgb & 255) * 114;
		brush->SetColor(D2D1::ColorF(luminance > 128000 ? 0x000000 : 0xffffff, 0.18F));
		// 纹理以内容区中心为原点，两侧同距；标签沿用主栏下方文字槽。
		auto texture=bounds;
		if (preview && labelled) { texture.left+=5; texture.right-=5; texture.top+=5; texture.bottom=texture.top+40; }
		const float centerX=(texture.left+texture.right)/2, centerY=(texture.top+texture.bottom)/2;
		const float startX=centerX-std::floor((centerX-texture.left)/24)*24;
		const float startY=centerY-std::floor((centerY-texture.top)/24)*24;
		for (float x=startX; pattern && x<texture.right; x+=24) {
			if (pattern==2) context->DrawLine({x,texture.top},{x,texture.bottom},brush,1);
			else for(float y=startY;y<texture.bottom;y+=24)
				context->FillEllipse(D2D1::Ellipse({x,y},1,1),brush);
		}
		if (pattern==2) for(float y=startY;y<texture.bottom;y+=24)
			context->DrawLine({texture.left,y},{texture.right,y},brush,1);
		context->PopAxisAlignedClip();
		if (preview)
		{
			if (resources.clip) context->PopLayer();
			brush->SetColor(D2D1::ColorF(0x909090, 1));
			context->DrawRoundedRectangle(D2D1::RoundedRect(bounds, 4, 4), brush, 1);
		}
	}
}
