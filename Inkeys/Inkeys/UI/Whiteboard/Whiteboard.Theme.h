#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include <cstdint>

namespace Inkeys::UI::WhiteboardVisual
{
	inline void DrawTheme(ID2D1DeviceContext* context, D2D1_RECT_F bounds,
		std::uint32_t rgb, int pattern, bool preview = false, bool selected = false)
	{
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
		if (FAILED(context->CreateSolidColorBrush(D2D1::ColorF(rgb), &brush))) return;
		if (preview)
		{
			bounds.left += 7;
			bounds.top += 7;
			bounds.right -= 7;
			bounds.bottom -= 7;
		}
		context->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		context->FillRectangle(bounds, brush.Get());
		const unsigned luminance = ((rgb >> 16) & 255) * 299
			+ ((rgb >> 8) & 255) * 587 + (rgb & 255) * 114;
		brush->SetColor(D2D1::ColorF(luminance > 128000 ? 0x000000 : 0xffffff, 0.18F));
		// 背景和缩略预览使用同一纹理标尺；不缓存设备相关资源。
		for (float x = bounds.left + 12; pattern && x < bounds.right; x += 24)
		{
			if (pattern == 2)
				context->DrawLine({ x, bounds.top }, { x, bounds.bottom }, brush.Get(), 1);
			else
				for (float y = bounds.top + 12; y < bounds.bottom; y += 24)
					context->FillEllipse(D2D1::Ellipse({ x, y }, 1, 1), brush.Get());
		}
		if (pattern == 2)
			for (float y = bounds.top + 12; y < bounds.bottom; y += 24)
				context->DrawLine({ bounds.left, y }, { bounds.right, y }, brush.Get(), 1);
		context->PopAxisAlignedClip();
		if (preview)
		{
			brush->SetColor(D2D1::ColorF(selected ? 0x78bfff : 0x909090, 1));
			context->DrawRoundedRectangle(D2D1::RoundedRect(bounds, 4, 4),
				brush.Get(), selected ? 3.0F : 1.0F);
		}
	}
}
