#include "IdtD2DPreparation.h"

CComPtr<ID2D1Factory1> D2DFactory;

CComPtr<IDWriteFactory1> D2DTextFactory;
CComPtr<IDWriteFontCollection> D2DFontCollection;

void D2DStarup()
{
	// 创建 D2D1.1 工厂
	ID2D1Factory1* tmpFactory = nullptr;
	D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (IID_PPV_ARGS(&tmpFactory)));
	D2DFactory.Attach(tmpFactory);

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