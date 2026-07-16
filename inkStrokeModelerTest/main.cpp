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
	if (!presentation.Initialize(window.Handle(), graphics, renderer, // 初始化透明呈现链，同时会初始化 renderer 的尺寸资源。
		static_cast<UINT>(initialSize.width), static_cast<UINT>(initialSize.height)))
	{
		std::cout << "Failed to initialize any transparent present pipeline." << std::endl;
		return -1;
	}
	window.SetGpuTransparentComposition(presentation.IsGpuTransparentComposition()); // 让窗口过程按当前透明模式处理背景和重绘。

	// 绘制控制器封装单笔帧循环，入口只保留应用级消息编排。
	draw3::DrawingController drawing(
		window,
		renderer,
		presentation,
		draw3::CreateStrokeModelConfiguration(GetPrimaryDpiX()));
	drawing.ClearCanvas();

	while (!window.ExitRequested())
	{
		if (window.ConsumeClearCanvasRequest()) drawing.ClearCanvas(); // 键盘清屏请求在主绘制线程执行。
		if (window.ConsumeCompositionChangedRequest())
		{
			presentation.RefreshAfterCompositionChanged(); // DWM 状态变化后重新启用玻璃/扩展帧。
			window.RequestFullPresent(); // 透明模式刷新后补一帧完整画布。
		}
		drawing.ProcessPendingResize(true); // Resize 请求会重建交换链和图层资源。
		if (window.ConsumeFullPresentRequest()) drawing.PresentFullCanvas(); // 处理窗口暴露或移动后的全量刷新。

		draw3::MouseMessage message = {};
		if (!window.TryGetMouseMessage(message))
		{
			Sleep(1); // 没有输入时让出时间片，避免主循环空转。
			continue;
		}
		if (message.message == WM_LBUTTONDOWN)
		{
			drawing.DrawMouseStroke(message);
		}
	}
	return 0;
}
