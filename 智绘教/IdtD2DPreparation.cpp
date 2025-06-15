#include "IdtD2DPreparation.h"

ID2D1Factory* D2DFactory = nullptr;
D2D1_RENDER_TARGET_PROPERTIES D2DProperty;

IDWriteFactory* D2DTextFactory = nullptr;
IDWriteFontCollection* D2DFontCollection = nullptr;

void D2DStarup()
{
	// 创建 D2D 工厂
	D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &D2DFactory);

	// 创建 DC Render 并指定软件加速（因为比硬件加速快，不知道为啥，现在知道了qaq）
	D2DProperty = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE::D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		D2D1::PixelFormat(
			DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED
		), 0.0, 0.0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
	);

	// 创建 D2D 文字工厂
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&D2DTextFactory));
}
void D2DShutdown()
{
	DxObjectSafeRelease(&D2DFontCollection);
	DxObjectSafeRelease(&D2DTextFactory);
	DxObjectSafeRelease(&D2DFactory);
}

D2D1::ColorF ConvertToD2DColor(COLORREF Color, bool ReserveAlpha)
{
	return D2D1::ColorF(
		GetRValue(Color) / 255.0f,
		GetGValue(Color) / 255.0f,
		GetBValue(Color) / 255.0f,
		(ReserveAlpha ? GetAValue(Color) : 255) / 255.0f
	);
}
void SetAlpha(COLORREF& Color, int Alpha)
{
	Color = (COLORREF)(((Color) & 0xFFFFFF) | ((Alpha) << 24));
}