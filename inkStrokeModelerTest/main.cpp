#include <iostream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

import draw3.drawing_controller;
import draw3.graphics_initialization;
import draw3.ink_prediction;
import draw3.renderer;
import draw3.transparent_presentation;
import draw3.window_control;

namespace
{
	// 读取主显示器 DPI，用于把建模速度参数换算为物理尺度。
	int GetPrimaryDpiX()
	{
		HDC screen = GetDC(nullptr);
		if (!screen) return 96;
		const int dpiX = GetDeviceCaps(screen, LOGPIXELSX);
		ReleaseDC(nullptr, screen);
		return dpiX > 0 ? dpiX : 96;
	}
}

int main()
{
	timeBeginPeriod(1); // 保持原有 1ms 系统计时粒度。

	// 先创建窗口，以便透明呈现模块随后绑定 HWND 和交换链。
	draw3::WindowController window;
	if (!window.Initialize(draw3::ShouldPreconfigureNoRedirectionBitmap()))
	{
		std::cout << "Failed to initialize the drawing window." << std::endl;
		return -1;
	}

	// 初始化共享的 D3D11/DXGI 设备资源。
	draw3::GraphicsDeviceResources graphics;
	if (!draw3::InitializeGraphicsDevice(graphics)) return -1;

	draw3::InkRenderer renderer;
	draw3::TransparentPresentationController presentation;
	const draw3::WindowSize initialSize = window.Size();
	if (!presentation.Initialize(window.Handle(), graphics, renderer,
		static_cast<UINT>(initialSize.width), static_cast<UINT>(initialSize.height)))
	{
		std::cout << "Failed to initialize any transparent present pipeline." << std::endl;
		return -1;
	}
	window.SetGpuTransparentComposition(presentation.IsGpuTransparentComposition());

	// 绘制控制器封装单笔帧循环，入口只保留应用级消息编排。
	draw3::DrawingController drawing(
		window,
		renderer,
		presentation,
		draw3::CreateStrokeModelConfiguration(GetPrimaryDpiX()));
	drawing.ClearCanvas();

	while (!window.ExitRequested())
	{
		if (window.ConsumeClearCanvasRequest()) drawing.ClearCanvas();
		if (window.ConsumeCompositionChangedRequest())
		{
			presentation.RefreshAfterCompositionChanged();
			window.RequestFullPresent();
		}
		drawing.ProcessPendingResize(true);
		if (window.ConsumeFullPresentRequest()) drawing.PresentFullCanvas();

		draw3::MouseMessage message = {};
		if (!window.TryGetMouseMessage(message))
		{
			Sleep(1);
			continue;
		}
		if (message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN)
		{
			drawing.DrawMouseStroke(message);
		}
	}
	return 0;
}
