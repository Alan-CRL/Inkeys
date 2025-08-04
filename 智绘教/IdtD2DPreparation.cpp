#include "IdtD2DPreparation.h"

CComPtr<ID2D1Factory1> D2DFactory;
D2D1_RENDER_TARGET_PROPERTIES D2DProperty;

CComPtr<IDWriteFactory1> D2DTextFactory;
CComPtr<IDWriteFontCollection> D2DFontCollection;

void D2DStarup()
{
	// 创建 D2D1.1 工厂
	ID2D1Factory1* tmpFactory = nullptr;
	D2D1CreateFactory(
		D2D1_FACTORY_TYPE_MULTI_THREADED,
		__uuidof(ID2D1Factory1),
		nullptr,
		reinterpret_cast<void**>(&tmpFactory)
	);
	D2DFactory.Attach(tmpFactory);

	// 创建 DC Render 相关属性，D2D1.1 仍兼容 RenderTargetProperties，但强烈建议后面用 Device/DeviceContext
	D2DProperty = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		D2D1::PixelFormat(
			DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED
		), 0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_10
	);

	IDWriteFactory1* tmpWriteFactory = nullptr;
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), reinterpret_cast<IUnknown**>(&tmpWriteFactory));
	D2DTextFactory.Attach(tmpWriteFactory);
}
void D2DShutdown()
{
	D2DFontCollection.Release();
	D2DTextFactory.Release();
	D2DFactory.Release();
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