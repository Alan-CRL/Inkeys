module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <vector>

export module Inkeys.Drawing.Draw3.graphics_initialization;

export namespace Inkeys::Drawing::Draw3
{
	// 硬件加速可用时的适配器偏好，数值与配置 AdapterPreference 的 0/1/2 一一对应。
	enum class GraphicsAdapterPreference : std::uint8_t { Automatic, Integrated, Discrete };
	enum class GraphicsAdapterKind : std::uint8_t { Integrated, Discrete };
	// 混合显卡内核标志：未知（Win7 或非混合系统）、核显、独显。
	enum class GraphicsAdapterHybridFlag : std::uint8_t { Unknown, Integrated, Discrete };

	// 优先采用系统的混合显卡标志；没有时按独占显存判断（≤ 512 MiB 视为核显）。
	GraphicsAdapterKind ClassifyGraphicsAdapter(std::uint64_t dedicatedVideoMemory,
		GraphicsAdapterHybridFlag hybridFlag) noexcept;
	// 返回尝试顺序（输入下标）：自动只含 0 号（生产路径自动档不枚举，直接用默认适配器）；优先核显/独显时先尝试该类，再尝试另一类，类内保持枚举顺序。
	std::vector<std::size_t> OrderGraphicsAdapters(const std::vector<GraphicsAdapterKind>& kinds,
		GraphicsAdapterPreference preference);

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

	// 关闭硬件加速时直接使用 WARP；开启时按 adapterPreference 依次尝试硬件适配器（需 11_0），
	// 全部失败后按 allowWarpFallback 决定是否回退 WARP。
	bool InitializeGraphicsDevice(GraphicsDeviceResources& resources, bool enableHardwareAcceleration = true,
		bool allowWarpFallback = true, GraphicsAdapterPreference adapterPreference = GraphicsAdapterPreference::Automatic);
	// 仅验证生产设备选择与回退顺序，不创建窗口或实际 D3D 设备。
	int RunGraphicsDeviceSelectionTest() noexcept;
	// 统一识别必须销毁并重建 D3D 设备的 DXGI 返回值。
	bool IsGraphicsDeviceLostError(HRESULT result) noexcept;
}
