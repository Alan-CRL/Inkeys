module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

export module Inkeys.Drawing.Draw3.graphics_initialization;

export namespace Inkeys::Drawing::Draw3
{
	// 保存应用运行期间使用的 D3D11 和 DXGI 核心对象。
	struct GraphicsDeviceResources
	{
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		Microsoft::WRL::ComPtr<IDXGIDevice1> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
		D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_UNKNOWN;
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	};

	// 初始化 D3D11 设备，硬件设备失败时自动回退到 WARP。
	bool InitializeGraphicsDevice(GraphicsDeviceResources& resources);
	// 统一识别必须销毁并重建 D3D 设备的 DXGI 返回值。
	bool IsGraphicsDeviceLostError(HRESULT result) noexcept;
}
