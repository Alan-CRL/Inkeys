#pragma once
#include "pch.h"
#include "Utils.h"

namespace DirectInkPresenter
{
	namespace UI
	{
		namespace Graphics
		{
			using namespace D2D1;
			// 获取窗口当前DPI缩放
			FLOAT GetDpiScailingForHwnd(HWND hWnd);
			// 获取窗口当前DPI
			void GetDpiForHwnd(HWND hWnd, float& fDpiX, float& fDpiY);
			// 将使用与设备无关的像素(DIP)表示的长度转换为使用物理像素表示的长度。
			LONG ConvertDipsToPixels(float dips, float dpi = -1.f);
			// 将使用物理像素表示的长度转换为使用与设备无关的像素(DIP)表示的长度。
			float ConvertPixelsToDips(UINT pixels, float dpi = -1.f);
			// 设置当前上下文的DPI
			void SetContextDpi(float dpi);
			void SetContextDpi(HWND hWnd);

			void CreateTargetForHwnd(HWND hWnd, ID2D1HwndRenderTarget** d2dHwndRenderTarget);
			void Initialize();

			inline ID2D1Factory* GetD2DFactory();
			inline IDWriteFactory* GetDwriteFactory();
			inline IWICImagingFactory* GetWicFactory();
		};
	}
}