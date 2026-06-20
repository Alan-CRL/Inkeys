module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atlbase.h>
#include <d3d11.h>
#include <dxgi1_2.h>

export module draw3.graphics_initialization;

export namespace draw3
{
	// 保存应用运行期间使用的 D3D11 和 DXGI 核心对象。
	struct GraphicsDeviceResources
	{
		CComPtr<ID3D11Device> device;
		CComPtr<ID3D11DeviceContext> context;
		CComPtr<IDXGIDevice1> dxgiDevice;
		CComPtr<IDXGIAdapter> adapter;
		CComPtr<IDXGIFactory2> factory;
		D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_UNKNOWN;
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	};

	// 初始化 D3D11 设备，硬件设备失败时自动回退到 WARP。
	bool InitializeGraphicsDevice(GraphicsDeviceResources& resources);
}
