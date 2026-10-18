module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <windows.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

module Inkeys.Drawing.Draw3.graphics_initialization;

import Inkeys.Drawing.Draw3.diagnostics;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		template <typename CreateDevice>
		HRESULT CreateSelectedDevice(bool enableHardwareAcceleration, D3D_DRIVER_TYPE& actualDriver,
			CreateDevice&& createDevice)
		{
			// 显式关闭时跳过硬件尝试；失败回退只适用于已请求的硬件路径。
			actualDriver = enableHardwareAcceleration ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
			HRESULT result = createDevice(actualDriver);
			if (enableHardwareAcceleration && FAILED(result))
			{
				actualDriver = D3D_DRIVER_TYPE_WARP;
				result = createDevice(actualDriver);
			}
			if (FAILED(result)) actualDriver = D3D_DRIVER_TYPE_UNKNOWN;
			return result;
		}

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

		void LogDeviceAttempt(D3D_DRIVER_TYPE driverType, const char* levelSet,
			const char* attempt, HRESULT result)
		{
			// 成功路径仅在显式 Draw3 诊断开启时输出；失败阶段/HRESULT 始终保留，便于 Win7 现场归因。
			const bool compatibilityRetry = std::strcmp(attempt, "fallback") == 0;
			if (SUCCEEDED(result) && !StartupEnvironmentDiagnosticsEnabled() &&
				driverType == D3D_DRIVER_TYPE_HARDWARE && !compatibilityRetry) return;
			DiagnosticOutput::Line() << "[Draw3Diag][device] driver=" << DriverTypeName(driverType)
				<< " levels=" << levelSet << " attempt=" << attempt
				<< " hresult=0x" << std::hex
				<< static_cast<unsigned long>(result) << std::dec << std::endl;
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
			LogDeviceAttempt(driverType, "11_1,11_0", "preferred", result);
			if (result == E_INVALIDARG)
			{
				// Windows 7 不识别 11_1 枚举，使用只含 11_0 的列表重试。
				device.Reset();
				context.Reset();
				result = D3D11CreateDevice(nullptr, driverType, nullptr, creationFlags,
					fallbackLevels, ARRAYSIZE(fallbackLevels), D3D11_SDK_VERSION,
					device.GetAddressOf(), &actualFeatureLevel, context.GetAddressOf());
				LogDeviceAttempt(driverType, "11_0", "fallback", result);
			}
			return result;
		}
	}

	bool InitializeGraphicsDevice(GraphicsDeviceResources& resources, bool enableHardwareAcceleration)
	{
		// 重建时不能让新 device 与旧 adapter/factory 混用。
		resources = {};
		const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // 透明窗口路径需要 BGRA backbuffer。
		HRESULT hardwareResult = S_OK;
		HRESULT result = CreateSelectedDevice(enableHardwareAcceleration, resources.driverType,
			[&](D3D_DRIVER_TYPE driverType)
			{
				if (driverType == D3D_DRIVER_TYPE_WARP && enableHardwareAcceleration)
					DiagnosticOutput::Line() << "Hardware device initialization failed. Falling back to WARP." << std::endl;
				const HRESULT attemptResult = CreateCompatibleDevice(driverType, creationFlags,
					resources.device, resources.featureLevel, resources.context);
				if (driverType == D3D_DRIVER_TYPE_HARDWARE) hardwareResult = attemptResult;
				return attemptResult;
			});
		if (FAILED(result))
		{
			// 跳过硬件时不输出虚构的硬件尝试结果。
			DiagnosticOutput::Line line;
			line << "[Draw3Diag][device] final=failed hardware_attempted=" << enableHardwareAcceleration;
			if (enableHardwareAcceleration)
				line << " hardware_hresult=0x" << std::hex << static_cast<unsigned long>(hardwareResult) << std::dec;
			line << " warp_hresult=0x" << std::hex << static_cast<unsigned long>(result) << std::dec << std::endl;
			return false;
		}

		if (StartupEnvironmentDiagnosticsEnabled())
		{
			// 恢复启动时的设备环境输出，但不影响始终保留的错误诊断。
			DiagnosticOutput::Line() << "Current D3D device: " << DriverTypeName(resources.driverType) << std::endl;
			DiagnosticOutput::Line() << "D3D feature level: " << FeatureLevelName(resources.featureLevel) << std::endl;
		}

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
		if (StartupEnvironmentDiagnosticsEnabled())
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

	int RunGraphicsDeviceSelectionTest() noexcept
	{
		struct Case
		{
			bool enabled;
			HRESULT hardwareResult, warpResult;
			D3D_DRIVER_TYPE expectedDriver;
			unsigned expectedAttempts;
		};
		constexpr Case cases[] = {
			{ true, S_OK, E_NOTIMPL, D3D_DRIVER_TYPE_HARDWARE, 1 },
			{ true, E_FAIL, S_OK, D3D_DRIVER_TYPE_WARP, 2 },
			{ true, E_FAIL, E_NOTIMPL, D3D_DRIVER_TYPE_UNKNOWN, 2 },
			{ false, E_FAIL, S_OK, D3D_DRIVER_TYPE_WARP, 1 },
			{ false, S_OK, E_NOTIMPL, D3D_DRIVER_TYPE_UNKNOWN, 1 },
		};
		for (const auto& test : cases)
		{
			unsigned attempts = 0;
			bool orderValid = true;
			D3D_DRIVER_TYPE actual = D3D_DRIVER_TYPE_UNKNOWN;
			const HRESULT result = CreateSelectedDevice(test.enabled, actual,
				[&](D3D_DRIVER_TYPE requested)
				{
					const auto expected = test.enabled && attempts == 0
						? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
					orderValid = orderValid && requested == expected;
					++attempts;
					return requested == D3D_DRIVER_TYPE_HARDWARE ? test.hardwareResult : test.warpResult;
				});
			if (!orderValid || attempts != test.expectedAttempts || actual != test.expectedDriver ||
				result != (test.enabled && SUCCEEDED(test.hardwareResult) ? test.hardwareResult : test.warpResult))
			{
				std::fputs("[Draw3DevicePolicy] FAIL: hardware/WARP selection or fallback order\n", stderr);
				return 1;
			}
		}
		std::fputs("[Draw3DevicePolicy] PASS: direct WARP, hardware preference and failure fallback\n", stderr);
		return 0;
	}

	bool IsGraphicsDeviceLostError(HRESULT result) noexcept
	{
		return result == DXGI_ERROR_DEVICE_REMOVED ||
			result == DXGI_ERROR_DEVICE_RESET ||
			result == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
			result == DXGI_ERROR_DEVICE_HUNG;
	}
}
