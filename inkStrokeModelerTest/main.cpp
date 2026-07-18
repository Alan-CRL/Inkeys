#include <iostream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

import draw3.contact_input;
import draw3.drawing_controller;
import draw3.graphics_initialization;
import draw3.ink_prediction;
import draw3.realtime_stylus;
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
	// 先创建窗口，以便透明呈现模块随后绑定 HWND 和交换链。
	draw3::ContactInputCoordinator input;
	draw3::WindowController window;
	if (!window.Initialize(draw3::ShouldPreconfigureNoRedirectionBitmap()))
	{
		std::cout << "Failed to initialize the drawing window." << std::endl;
		return -1;
	}
	window.SetInputCoordinator(&input); // 窗口控制请求可唤醒完全空闲的绘制线程。

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

	draw3::RealTimeStylusInput stylus;
	if (!stylus.Initialize(window.Handle(), input))
	{
		std::cout << "Failed to initialize RealTimeStylus multi-contact input." << std::endl;
		return -1; // 本阶段不回退到旧鼠标轮询。
	}

	// 绘制控制器独占模型与 D3D；RTS 同步插件只发布最新一致快照。
	draw3::DrawingController drawing(
		input,
		window,
		renderer,
		presentation,
		draw3::CreateStrokeModelConfiguration(GetPrimaryDpiX()));
	drawing.ClearCanvas();
	drawing.Run();
	stylus.Shutdown(); // 停回调、移除插件后再释放协调器记录。
	window.SetInputCoordinator(nullptr); // 解除窗口回调中的非拥有指针，再进入局部对象析构。
	return 0;
}
