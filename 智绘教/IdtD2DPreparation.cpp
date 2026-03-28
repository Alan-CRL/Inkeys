#include "IdtD2DPreparation.h"

ComPtr<ID2D1Factory1> d2dFactory1;

ComPtr<IDWriteFactory1> dWriteFactory1;
ComPtr<IDWriteFontCollection> dWriteFontCollection;

ComPtr<ID3D11Device> d3dDevice_WARP;
ComPtr<ID2D1Device> d2dDevice_WARP;

void D2DStarup()
{
	// 创建 D2D1.1 工厂
	D2D1CreateFactory(
		D2D1_FACTORY_TYPE_MULTI_THREADED,
		__uuidof(ID2D1Factory1),
		NULL,
		reinterpret_cast<void**>(d2dFactory1.ReleaseAndGetAddressOf())
	);

	DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory1),
		reinterpret_cast<IUnknown**>(dWriteFactory1.ReleaseAndGetAddressOf())
	);

	// 初始化 DC
	{
		// 创建 WARP 设备

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		D3D11CreateDevice(
			nullptr,                    // 指定 nullptr 使用默认适配器
			D3D_DRIVER_TYPE_WARP,       // **关键：使用 WARP 软件渲染器**
			nullptr,                    // 没有软件模块
			creationFlags,              // 设置支持 BGRA 格式
			featureLevels,              // 功能级别数组
			ARRAYSIZE(featureLevels),   // 数组大小
			D3D11_SDK_VERSION,          // SDK 版本
			d3dDevice_WARP.ReleaseAndGetAddressOf(),            // 返回创建的设备
			nullptr,                    // 返回实际的功能级别
			nullptr                     // 返回设备上下文 (我们不需要)
		);

		ComPtr<IDXGIDevice> dxgiDevice;
		d3dDevice_WARP.As(&dxgiDevice);

		d2dFactory1->CreateDevice(dxgiDevice.Get(), d2dDevice_WARP.ReleaseAndGetAddressOf());
	}
}
void D2DShutdown()
{
	dWriteFontCollection.Reset();
	dWriteFactory1.Reset();
	d2dFactory1.Reset();
}
