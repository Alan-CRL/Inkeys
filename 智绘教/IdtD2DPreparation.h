#pragma once

#include "IdtMain.h"

#include <d2d1_1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

extern CComPtr<ID2D1Factory> D2DFactory;
extern D2D1_RENDER_TARGET_PROPERTIES D2DProperty;

extern CComPtr<IDWriteFactory> D2DTextFactory;
extern CComPtr<IDWriteFontCollection> D2DFontCollection;

template <class T> void DxObjectSafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}
void D2DStarup();
void D2DShutdown();

D2D1::ColorF ConvertToD2DColor(COLORREF Color, bool ReserveAlpha = true);
void SetAlpha(COLORREF& Color, int Alpha);