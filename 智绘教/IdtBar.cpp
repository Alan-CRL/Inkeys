#include "IdtBar.h"

#include "IdtWindow.h"
#include "IdtDisplayManagement.h"

#undef max
#undef min
#include "libcuckoo/cuckoohash_map.hh"

// 临时
void FloatingInstallHook();

// 窗口

BarWindowPosClass barWindowPos;

// 媒体
void BarMediaClass::LoadExImage()
{
}

// 界面

void BarUISetClass::Rendering()
{
	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255;
		blend.AlphaFormat = AC_SRC_ALPHA;
	}
	SIZE sizeWnd = { static_cast<LONG>(barWindowPos.w), static_cast<LONG>(barWindowPos.h) };
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

	IMAGE barBackground(barWindowPos.w, barWindowPos.h);
	Graphics barGraphics(GetImageHDC(&barBackground));
	barGraphics.SetSmoothingMode(SmoothingModeHighQuality);

	clock_t tRecord = clock();
	for (int for_num = 1; !offSignal; for_num = 2)
	{
		SetImageColor(barBackground, RGBA(0, 0, 0, 0), true);

		{
			// 设置窗口位置
			POINT ptDst = { 0, 0 };

			ulwi.pptDst = &ptDst;
			ulwi.hdcSrc = GetImageHDC(&barBackground);
			UpdateLayeredWindowIndirect(floating_window, &ulwi);
		}

		if (for_num == 1)
		{
			IdtWindowsIsVisible.floatingWindow = true;
		}
		if (tRecord)
		{
			int delay = 1000 / 24 - (clock() - tRecord);
			if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
		tRecord = clock();
	}

	return;
}

// 交互

// 初始化

BarInitializationClass barInitialization;
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

	thread([&]() { barUISet.Rendering(); }).detach();

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

	barWindowPos.x = 0;
	barWindowPos.y = 0;
	barWindowPos.w = MainMonitor.MonitorWidth;
	barWindowPos.h = MainMonitor.MonitorHeight;
	barWindowPos.pct = 255;
	SetWindowPos(floating_window, NULL, barWindowPos.x, barWindowPos.y, barWindowPos.w, barWindowPos.h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_DRAWFRAME); // 设置窗口位置尺寸
}
void BarInitializationClass::InitializeMedia()
{
	barUISet.barMedia.LoadExImage();
}
void BarInitializationClass::InitializeUI()
{
	//
}