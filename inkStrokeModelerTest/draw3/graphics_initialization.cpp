module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <iostream>
#include <windows.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

module draw3.graphics_initialization;

import draw3.diagnostics;

namespace draw3
{
	namespace
	{
		const char* DriverTypeName(D3D_DRIVER_TYPE driverType)
		{
			switch (driverType)
			{
			case D3D_DRIVER_TYPE_HARDWARE: return "Hardware";
			case D3D_DRIVER_TYPE_WARP: return "WARP";
			default: return "Unknown";
			}
		}

		const char* FeatureLevelName(D3D_FEATURE_LEVEL featureLevel)
		{
			switch (featureLevel)
			{
			case D3D_FEATURE_LEVEL_11_1: return "11_1";
			case D3D_FEATURE_LEVEL_11_0: return "11_0";
			case D3D_FEATURE_LEVEL_10_1: return "10_1";
			case D3D_FEATURE_LEVEL_10_0: return "10_0";
			case D3D_FEATURE_LEVEL_9_3: return "9_3";
			case D3D_FEATURE_LEVEL_9_2: return "9_2";
			case D3D_FEATURE_LEVEL_9_1: return "9_1";
			default: return "Unknown";
			}
		}

		HRESULT CreateCompatibleDevice(D3D_DRIVER_TYPE driverType, UINT creationFlags,
			Microsoft::WRL::ComPtr<ID3D11Device>& device, D3D_FEATURE_LEVEL& actualFeatureLevel,
			Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context)
		{
			static constexpr D3D_FEATURE_LEVEL preferredLevels[] = {
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0
			};
			static constexpr D3D_FEATURE_LEVEL fallbackLevels[] = { D3D_FEATURE_LEVEL_11_0 }; // 旧系统只接受 11_0 列表。

			device.Reset();
			context.Reset();
			HRESULT result = D3D11CreateDevice(nullptr, driverType, nullptr, creationFlags,
				preferredLevels, ARRAYSIZE(preferredLevels), D3D11_SDK_VERSION,
				device.GetAddressOf(), &actualFeatureLevel, context.GetAddressOf());
			if (result == E_INVALIDARG)
			{
				// Windows 7 不识别 11_1 枚举，使用只含 11_0 的列表重试。
				device.Reset();
				context.Reset();
				result = D3D11CreateDevice(nullptr, driverType, nullptr, creationFlags,
					fallbackLevels, ARRAYSIZE(fallbackLevels), D3D11_SDK_VERSION,
					device.GetAddressOf(), &actualFeatureLevel, context.GetAddressOf());
			}
			return result;
		}
	}

	bool InitializeGraphicsDevice(GraphicsDeviceResources& resources)
	{
		const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // 透明窗口路径需要 BGRA backbuffer。
		HRESULT result = CreateCompatibleDevice(D3D_DRIVER_TYPE_HARDWARE, creationFlags,
			resources.device, resources.featureLevel, resources.context);
		if (FAILED(result))
		{
			std::cout << "Hardware device initialization failed. Falling back to WARP." << std::endl;
			result = CreateCompatibleDevice(D3D_DRIVER_TYPE_WARP, creationFlags, // 硬件失败时退到软件光栅，方便诊断和兼容。
				resources.device, resources.featureLevel, resources.context);
			if (FAILED(result))
			{
				std::cout << "Failed to initialize a D3D11 device with both Hardware and WARP." << std::endl;
				return false;
			}
			resources.driverType = D3D_DRIVER_TYPE_WARP;
		}
		else
		{
			resources.driverType = D3D_DRIVER_TYPE_HARDWARE;
		}

		std::cout << "Current D3D device: " << DriverTypeName(resources.driverType) << std::endl;
		std::cout << "D3D feature level: " << FeatureLevelName(resources.featureLevel) << std::endl;

		resources.dxgiDevice.Reset();
		result = resources.device.As(&resources.dxgiDevice); // 使用类型安全的 QueryInterface 获取 DXGI 设备。
		if (FAILED(result))
		{
			LogHResult("ID3D11Device::QueryInterface(IDXGIDevice1)", result);
			return false;
		}
		resources.dxgiDevice->SetMaximumFrameLatency(1); // 限制排队帧数，降低笔迹显示延迟。

		result = resources.dxgiDevice->GetAdapter(resources.adapter.ReleaseAndGetAddressOf()); // 取得适配器用于日志和 factory 查询。
		if (FAILED(result) || !resources.adapter)
		{
			LogHResult("IDXGIDevice1::GetAdapter", result);
			return false;
		}
		LogAdapterDiagnostics(resources.adapter.Get());

		result = resources.adapter->GetParent(__uuidof(IDXGIFactory2),
			reinterpret_cast<void**>(resources.factory.ReleaseAndGetAddressOf())); // 交换链必须由同一适配器的 factory 创建。
		if (FAILED(result) || !resources.factory)
		{
			LogHResult("IDXGIAdapter::GetParent(IDXGIFactory2)", result);
			return false;
		}
		return true;
	}
}
