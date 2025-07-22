#include "IdtBar.h"

#include "../../IdtD2DPreparation.h"
#include "../../IdtDisplayManagement.h"
#include "../../IdtWindow.h"
#include "../CONV/IdtColor.h"

//#undef max
//#undef min
//#include "libcuckoo/cuckoohash_map.h"

#define LUNASVG_BUILD_STATIC
#include <lunasvg/lunasvg.h>
#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

// ====================
// 临时
void FloatingInstallHook();

// ====================
// 窗口

// ====================
// 媒体
void BarMediaClass::LoadExImage()
{
}

// ====================
// 界面

// 具体渲染
bool BarUIRendering::Svg(ID2D1DCRenderTarget* DCRenderTarget, const BarUiSVGClass& svg)
{
	int targetWidth = svg.h->val;
	int targetHeight = svg.w->val;

	unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg.svg->GetVal());
	//unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromFile("D:\\Downloads\\Inkeys.svg");

	lunasvg::Bitmap bitmap = document->renderToBitmap(targetWidth, targetHeight);

	CComPtr<ID2D1Bitmap> d2dBitmap;
	D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

	// lunasvg 文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
	DCRenderTarget->CreateBitmap(
		D2D1::SizeU(bitmap.width(), bitmap.height()),
		bitmap.data(),
		bitmap.width() * 4, // stride
		props,
		&d2dBitmap);

	D2D1_RECT_F destRect = D2D1::RectF(0, 0, (float)targetWidth, (float)targetHeight);
	DCRenderTarget->DrawBitmap(
		d2dBitmap,
		destRect,        // 目标矩形
		1.0f,            // 不透明度
		D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
		nullptr          // 源rect, null表示全部
	);
}

// UI 总集
void BarUISetClass::Rendering()
{
	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;
	}
	SIZE sizeWnd = { static_cast<LONG>(BarWindowPosClass::w), static_cast<LONG>(BarWindowPosClass::h) };
	POINT ptSrc = { 0,0 };
	POINT ptDst = { 0,0 };
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	{
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = GetDC(NULL);
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;
	}

	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}

	// 画布
	IMAGE barBackground(BarWindowPosClass::w, BarWindowPosClass::h);

	// 初始化 D2D DC
	CComPtr<ID2D1DCRenderTarget> DCRenderTarget;
	{
		// 创建位图兼容的 DC Render Target
		ID2D1DCRenderTarget* pDCRenderTarget = nullptr;
		HRESULT hr = D2DFactory->CreateDCRenderTarget(&D2DProperty, &pDCRenderTarget);
		DCRenderTarget.Attach(pDCRenderTarget);

		RECT PptBackgroundWindowRect = { 0, 0, barBackground.getwidth(),barBackground.getheight() };
		DCRenderTarget->BindDC(GetImageHDC(&barBackground), &PptBackgroundWindowRect);

		// 设置抗锯齿
		DCRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE::D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	}

	chrono::high_resolution_clock::time_point reckon = chrono::high_resolution_clock::now();
	for (int forNum = 1; !offSignal; forNum = 2)
	{
		DCRenderTarget->BeginDraw();

		{
			D2D1_COLOR_F clearColor = ConvertToD2DColor(RGBA(0, 0, 0, 128));
			DCRenderTarget->Clear(&clearColor);
		}
		{
			int targetWidth = 500;
			int targetHeight = 500;

			//unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svgText);
			unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromFile("D:\\Downloads\\Inkeys.svg");

			lunasvg::Bitmap bitmap = document->renderToBitmap(targetWidth, targetHeight);

			CComPtr<ID2D1Bitmap> d2dBitmap;
			D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

			// lunasvg文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
			DCRenderTarget->CreateBitmap(
				D2D1::SizeU(bitmap.width(), bitmap.height()),
				bitmap.data(),
				bitmap.width() * 4, // stride
				props,
				&d2dBitmap);

			D2D1_RECT_F destRect = D2D1::RectF(0, 0, (float)targetWidth, (float)targetHeight);
			DCRenderTarget->DrawBitmap(
				d2dBitmap,
				destRect,        // 目标矩形
				1.0f,            // 不透明度
				D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
				nullptr          // 源rect, null表示全部
			);
		}

		DCRenderTarget->EndDraw();

		{
			// 设置窗口位置
			POINT ptDst = { 0, 0 };

			ulwi.pptDst = &ptDst;
			ulwi.hdcSrc = GetImageHDC(&barBackground);
			UpdateLayeredWindowIndirect(floating_window, &ulwi);
		}

		if (forNum == 1)
		{
			IdtWindowsIsVisible.floatingWindow = true;
		}
		// 帧率锁
		{
			double delay = 1000.0 / 24.0 - chrono::duration<double, std::milli>(chrono::high_resolution_clock::now() - reckon).count();
			if (delay >= 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay)));
		}
		reckon = chrono::high_resolution_clock::now();
	}

	return;
}

// ====================
// 交互

// 初始化
void BarInitializationClass::Initialization()
{
	threadStatus[L"BarInitialization"] = true;

	// 初始化
	InitializeWindow();
	InitializeMedia();
	InitializeUI();

	// 线程

	thread FloatingInstallHookThread(FloatingInstallHook);
	FloatingInstallHookThread.detach();

	thread(BarUISetClass::Rendering).detach();

	// 等待

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

	unsigned int waitTimes = 1;
	for (; waitTimes <= 10; waitTimes++)
	{
		if (!threadStatus[L"BarUI"] &&
			!threadStatus[L"BarDraw"]) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}

	threadStatus[L"BarInitialization"] = false;
	return;
}
void BarInitializationClass::InitializeWindow()
{
	DisableResizing(floating_window, true); // hiex 禁止窗口拉伸

	SetWindowLong(floating_window, GWL_STYLE, GetWindowLong(floating_window, GWL_STYLE) & ~WS_CAPTION); // 隐藏窗口标题栏
	SetWindowLong(floating_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW); // 隐藏窗口任务栏图标

	BarWindowPosClass::x = 0;
	BarWindowPosClass::y = 0;
	BarWindowPosClass::w = 1000;// MainMonitor.MonitorWidth;
	BarWindowPosClass::h = 1000;// MainMonitor.MonitorHeight;
	BarWindowPosClass::pct = 255;
	SetWindowPos(floating_window, NULL, BarWindowPosClass::x, BarWindowPosClass::y, BarWindowPosClass::w, BarWindowPosClass::h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_DRAWFRAME); // 设置窗口位置尺寸
}
void BarInitializationClass::InitializeMedia()
{
	BarUISetClass::barMedia.LoadExImage();
}
void BarInitializationClass::InitializeUI()
{
	//
}