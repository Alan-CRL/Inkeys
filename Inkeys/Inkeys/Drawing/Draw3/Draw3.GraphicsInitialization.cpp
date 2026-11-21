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
#include <sstream>
#include <string>
#include <vector>
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
		HRESULT CreateSelectedDevice(bool enableHardwareAcceleration, bool allowWarpFallback, D3D_DRIVER_TYPE& actualDriver,
			CreateDevice&& createDevice)
		{
			// 显式关闭时跳过硬件尝试；失败回退只适用于已请求且允许回退的硬件路径（“硬件加速”档不回退）。
			actualDriver = enableHardwareAcceleration ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
			HRESULT result = createDevice(actualDriver);
			if (enableHardwareAcceleration && allowWarpFallback && FAILED(result))
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
			const char* attempt, HRESULT result, const char* adapterText = nullptr)
		{
			// 成功路径仅在显式 Draw3 诊断开启时输出；失败阶段/HRESULT 始终保留，便于 Win7 现场归因。
			const bool compatibilityRetry = std::strcmp(attempt, "fallback") == 0;
			if (SUCCEEDED(result) && !StartupEnvironmentDiagnosticsEnabled() &&
				driverType == D3D_DRIVER_TYPE_HARDWARE && !compatibilityRetry) return;
			// 失败记 Warn；Draw3 调试开关开启时成功写入 Draw3 通道，否则（WARP/兼容重试成功）记 Info。
			const DiagnosticOutput::Target target = FAILED(result) ? DiagnosticOutput::OutWarn
				: StartupEnvironmentDiagnosticsEnabled() ? DiagnosticOutput::Target{ DiagnosticOutput::Stream::Output, Inkeys::Log::Channel::Draw3 }
				: DiagnosticOutput::OutInfo;
			DiagnosticOutput::Line line(target);
			line << "[Draw3Diag][device] driver=" << DriverTypeName(driverType);
			if (adapterText) line << " adapter=" << adapterText;
			line << " levels=" << levelSet << " attempt=" << attempt
				<< " hresult=0x" << std::hex
				<< static_cast<unsigned long>(result) << std::dec << std::endl;
		}

		HRESULT CreateCompatibleDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType, UINT creationFlags,
			Microsoft::WRL::ComPtr<ID3D11Device>& device, D3D_FEATURE_LEVEL& actualFeatureLevel,
			Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context, const char* adapterText = nullptr)
		{
			static constexpr D3D_FEATURE_LEVEL preferredLevels[] = {
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0
			};
			static constexpr D3D_FEATURE_LEVEL fallbackLevels[] = { D3D_FEATURE_LEVEL_11_0 }; // 旧系统只接受 11_0 列表。
			// 指定适配器时驱动类型必须为 UNKNOWN；日志仍按“硬件”归类。
			const D3D_DRIVER_TYPE createType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : driverType;

			device.Reset();
			context.Reset();
			HRESULT result = D3D11CreateDevice(adapter, createType, nullptr, creationFlags,
				preferredLevels, ARRAYSIZE(preferredLevels), D3D11_SDK_VERSION,
				device.GetAddressOf(), &actualFeatureLevel, context.GetAddressOf());
			LogDeviceAttempt(driverType, "11_1,11_0", "preferred", result, adapterText);
			if (result == E_INVALIDARG)
			{
				// Windows 7 不识别 11_1 枚举，使用只含 11_0 的列表重试。
				device.Reset();
				context.Reset();
				result = D3D11CreateDevice(adapter, createType, nullptr, creationFlags,
					fallbackLevels, ARRAYSIZE(fallbackLevels), D3D11_SDK_VERSION,
					device.GetAddressOf(), &actualFeatureLevel, context.GetAddressOf());
				LogDeviceAttempt(driverType, "11_0", "fallback", result, adapterText);
			}
			return result;
		}

		// 按 d3dkmthk.h 布局自行声明（Win7 SDK 头不可见），接口全部动态获取。
		struct KmtOpenAdapterFromLuid { LUID AdapterLuid; UINT hAdapter; };
		struct KmtQueryAdapterInfo { UINT hAdapter; int Type; void* pPrivateDriverData; UINT PrivateDriverDataSize; };
		struct KmtCloseAdapter { UINT hAdapter; };
		using KmtOpenFromLuidFn = LONG(APIENTRY*)(KmtOpenAdapterFromLuid*);
		using KmtQueryFn = LONG(APIENTRY*)(const KmtQueryAdapterInfo*);
		using KmtCloseFn = LONG(APIENTRY*)(const KmtCloseAdapter*);
		constexpr int kKmtQueryAdapterType = 15; // KMTQAITYPE_ADAPTERTYPE（Win8+）
		constexpr UINT kKmtHybridDiscrete = 1u << 4; // D3DKMT_ADAPTERTYPE::HybridDiscrete（WDDM 1.3+）
		constexpr UINT kKmtHybridIntegrated = 1u << 5; // D3DKMT_ADAPTERTYPE::HybridIntegrated（WDDM 1.3+）

		GraphicsAdapterHybridFlag QueryHybridFlag(const LUID& luid) noexcept
		{
			// Win7 没有 D3DKMTOpenAdapterFromLuid，非混合系统也不设置这两个位；两者都退回显存判断。
			const HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
			if (!gdi32) return GraphicsAdapterHybridFlag::Unknown;
			const auto open = reinterpret_cast<KmtOpenFromLuidFn>(GetProcAddress(gdi32, "D3DKMTOpenAdapterFromLuid"));
			const auto query = reinterpret_cast<KmtQueryFn>(GetProcAddress(gdi32, "D3DKMTQueryAdapterInfo"));
			const auto close = reinterpret_cast<KmtCloseFn>(GetProcAddress(gdi32, "D3DKMTCloseAdapter"));
			if (!open || !query || !close) return GraphicsAdapterHybridFlag::Unknown;
			KmtOpenAdapterFromLuid openInfo{ luid, 0 };
			if (open(&openInfo) != 0 || !openInfo.hAdapter) return GraphicsAdapterHybridFlag::Unknown;
			UINT adapterType = 0;
			const KmtQueryAdapterInfo queryInfo{ openInfo.hAdapter, kKmtQueryAdapterType, &adapterType, sizeof(adapterType) };
			const LONG status = query(&queryInfo);
			const KmtCloseAdapter closeInfo{ openInfo.hAdapter };
			(void)close(&closeInfo);
			if (status != 0) return GraphicsAdapterHybridFlag::Unknown;
			if (adapterType & kKmtHybridIntegrated) return GraphicsAdapterHybridFlag::Integrated;
			if (adapterType & kKmtHybridDiscrete) return GraphicsAdapterHybridFlag::Discrete;
			return GraphicsAdapterHybridFlag::Unknown;
		}

		struct AdapterCandidate
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
			UINT index = 0;
			GraphicsAdapterKind kind = GraphicsAdapterKind::Discrete;
			GraphicsAdapterHybridFlag hybridFlag = GraphicsAdapterHybridFlag::Unknown;
			std::string text;
		};

		const char* AdapterKindName(GraphicsAdapterKind kind) noexcept
		{
			return kind == GraphicsAdapterKind::Integrated ? "integrated" : "discrete";
		}

		const char* AdapterPreferenceName(GraphicsAdapterPreference preference) noexcept
		{
			switch (preference)
			{
			case GraphicsAdapterPreference::Integrated: return "integrated-first";
			case GraphicsAdapterPreference::Discrete: return "discrete-first";
			default: return "automatic";
			}
		}

		std::string DescribeAdapter(UINT index, const DXGI_ADAPTER_DESC1& description,
			GraphicsAdapterKind kind, GraphicsAdapterHybridFlag hybridFlag)
		{
			char name[256]{};
			if (!WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr))
				std::strcpy(name, "?");
			std::ostringstream text;
			text << "#" << index << " \"" << name << "\" vendor=0x" << std::hex << std::uppercase
				<< description.VendorId << " device=0x" << description.DeviceId << std::dec << std::nouppercase
				<< " dedicatedMiB=" << (static_cast<unsigned long long>(description.DedicatedVideoMemory) >> 20)
				<< " kind=" << AdapterKindName(kind)
				<< (hybridFlag == GraphicsAdapterHybridFlag::Unknown ? "(memory)" : "(hybrid)");
			return text.str();
		}

		// 每次（含设备丢失重建）都新建 factory 重新枚举，避免沿用已失效的适配器。
		std::vector<AdapterCandidate> EnumerateHardwareAdapters()
		{
			std::vector<AdapterCandidate> candidates;
			Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return candidates;
			for (UINT index = 0;; ++index)
			{
				Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
				const HRESULT result = factory->EnumAdapters1(index, adapter.GetAddressOf());
				if (result == DXGI_ERROR_NOT_FOUND || FAILED(result) || !adapter) break;
				DXGI_ADAPTER_DESC1 description{};
				if (FAILED(adapter->GetDesc1(&description))) continue;
				// 软件适配器（Microsoft Basic Render Driver，部分系统不带软件标志）只作为最后的 WARP 回退。
				if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
					(description.VendorId == 0x1414 && description.DeviceId == 0x8C)) continue;
				AdapterCandidate candidate;
				candidate.adapter = adapter;
				candidate.index = index;
				candidate.hybridFlag = QueryHybridFlag(description.AdapterLuid);
				candidate.kind = ClassifyGraphicsAdapter(description.DedicatedVideoMemory, candidate.hybridFlag);
				candidate.text = DescribeAdapter(index, description, candidate.kind, candidate.hybridFlag);
				candidates.push_back(std::move(candidate));
			}
			return candidates;
		}

		// 按偏好依次尝试硬件适配器，第一个能建出 11_0 设备的即采用；返回最后一次失败的 HRESULT。
		HRESULT CreateHardwareDevice(GraphicsAdapterPreference preference, UINT creationFlags,
			GraphicsDeviceResources& resources)
		{
			const auto candidates = EnumerateHardwareAdapters();
			if (candidates.empty())
			{
				// 枚举不可用时保留原行为：交给运行时选择默认适配器。
				DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "[Draw3Diag][adapter] preference="
					<< AdapterPreferenceName(preference) << " candidates=0 action=default-adapter" << std::endl;
				return CreateCompatibleDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, creationFlags,
					resources.device, resources.featureLevel, resources.context);
			}
			std::vector<GraphicsAdapterKind> kinds;
			kinds.reserve(candidates.size());
			for (const auto& candidate : candidates) kinds.push_back(candidate.kind);
			HRESULT result = DXGI_ERROR_NOT_FOUND;
			for (const std::size_t position : OrderGraphicsAdapters(kinds, preference))
			{
				const auto& candidate = candidates[position];
				result = CreateCompatibleDevice(candidate.adapter.Get(), D3D_DRIVER_TYPE_HARDWARE, creationFlags,
					resources.device, resources.featureLevel, resources.context, candidate.text.c_str());
				if (SUCCEEDED(result))
				{
					// 选卡结果始终记一行，便于现场判断用的是哪块显卡。
					DiagnosticOutput::Line(DiagnosticOutput::OutInfo) << "[Draw3Diag][adapter] preference="
						<< AdapterPreferenceName(preference) << " candidates=" << candidates.size()
						<< " selected=" << candidate.text << " featureLevel=" << FeatureLevelName(resources.featureLevel)
						<< std::endl;
					return result;
				}
			}
			return result;
		}
	}

	GraphicsAdapterKind ClassifyGraphicsAdapter(std::uint64_t dedicatedVideoMemory,
		GraphicsAdapterHybridFlag hybridFlag) noexcept
	{
		if (hybridFlag == GraphicsAdapterHybridFlag::Integrated) return GraphicsAdapterKind::Integrated;
		if (hybridFlag == GraphicsAdapterHybridFlag::Discrete) return GraphicsAdapterKind::Discrete;
		// 核显只有 BIOS 划出的少量独占显存（常见 32–512 MiB），独显通常更多。
		constexpr std::uint64_t integratedMaximum = 512ull << 20;
		return dedicatedVideoMemory <= integratedMaximum ? GraphicsAdapterKind::Integrated : GraphicsAdapterKind::Discrete;
	}

	std::vector<std::size_t> OrderGraphicsAdapters(const std::vector<GraphicsAdapterKind>& kinds,
		GraphicsAdapterPreference preference)
	{
		std::vector<std::size_t> order;
		order.reserve(kinds.size());
		if (preference == GraphicsAdapterPreference::Automatic)
		{
			// 0 号适配器连接主显示器，先试它；不满足再按枚举顺序继续。
			for (std::size_t i = 0; i < kinds.size(); ++i) order.push_back(i);
			return order;
		}
		const GraphicsAdapterKind first = preference == GraphicsAdapterPreference::Integrated
			? GraphicsAdapterKind::Integrated : GraphicsAdapterKind::Discrete;
		for (std::size_t i = 0; i < kinds.size(); ++i) if (kinds[i] == first) order.push_back(i);
		for (std::size_t i = 0; i < kinds.size(); ++i) if (kinds[i] != first) order.push_back(i);
		return order;
	}

	bool InitializeGraphicsDevice(GraphicsDeviceResources& resources, bool enableHardwareAcceleration, bool allowWarpFallback,
		GraphicsAdapterPreference adapterPreference)
	{
		// 重建时不能让新 device 与旧 adapter/factory 混用。
		resources = {};
		const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // 透明窗口路径需要 BGRA backbuffer。
		HRESULT hardwareResult = S_OK;
		HRESULT result = CreateSelectedDevice(enableHardwareAcceleration, allowWarpFallback, resources.driverType,
			[&](D3D_DRIVER_TYPE driverType)
			{
				if (driverType == D3D_DRIVER_TYPE_WARP && enableHardwareAcceleration)
					DiagnosticOutput::Line(DiagnosticOutput::OutWarn) << "Hardware device initialization failed. Falling back to WARP." << std::endl;
				const HRESULT attemptResult = driverType == D3D_DRIVER_TYPE_HARDWARE
					? CreateHardwareDevice(adapterPreference, creationFlags, resources)
					: CreateCompatibleDevice(nullptr, driverType, creationFlags,
						resources.device, resources.featureLevel, resources.context);
				if (driverType == D3D_DRIVER_TYPE_HARDWARE) hardwareResult = attemptResult;
				return attemptResult;
			});
		if (FAILED(result))
		{
			// 跳过硬件时不输出虚构的硬件尝试结果。
			DiagnosticOutput::Line line(DiagnosticOutput::OutError);
			line << "[Draw3Diag][device] final=failed hardware_attempted=" << enableHardwareAcceleration
				<< " warp_fallback=" << allowWarpFallback;
			if (enableHardwareAcceleration)
				line << " hardware_hresult=0x" << std::hex << static_cast<unsigned long>(hardwareResult) << std::dec;
			// 禁止回退时未尝试 WARP，不输出虚构的 WARP 结果。
			if (!enableHardwareAcceleration || allowWarpFallback)
				line << " warp_hresult=0x" << std::hex << static_cast<unsigned long>(result) << std::dec;
			line << std::endl;
			return false;
		}

		if (StartupEnvironmentDiagnosticsEnabled())
		{
			// 恢复启动时的设备环境输出，但不影响始终保留的错误诊断。
			DiagnosticOutput::Line(DiagnosticOutput::Target{ DiagnosticOutput::Stream::Output, Inkeys::Log::Channel::Draw3 }) << "Current D3D device: " << DriverTypeName(resources.driverType) << std::endl;
			DiagnosticOutput::Line(DiagnosticOutput::Target{ DiagnosticOutput::Stream::Output, Inkeys::Log::Channel::Draw3 }) << "D3D feature level: " << FeatureLevelName(resources.featureLevel) << std::endl;
		}

		resources.dxgiDevice.Reset();
		result = resources.device.As(&resources.dxgiDevice); // 使用类型安全的 QueryInterface 获取 DXGI 设备。
		if (FAILED(result))
		{
			LogHResult("ID3D11Device::QueryInterface(IDXGIDevice1)", result);
			return false;
		}
		// Win7 也支持设备级排队上限。设备已移除时不能继续使用这代资源。
		result = resources.dxgiDevice->SetMaximumFrameLatency(1);
		if (FAILED(result))
		{
			LogHResult("IDXGIDevice1::SetMaximumFrameLatency(1)", result);
			if (IsGraphicsDeviceLostError(result)) return false;
		}

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
			bool enabled, fallback;
			HRESULT hardwareResult, warpResult;
			D3D_DRIVER_TYPE expectedDriver;
			unsigned expectedAttempts;
		};
		constexpr Case cases[] = {
			{ true, true, S_OK, E_NOTIMPL, D3D_DRIVER_TYPE_HARDWARE, 1 },
			{ true, true, E_FAIL, S_OK, D3D_DRIVER_TYPE_WARP, 2 },
			{ true, true, E_FAIL, E_NOTIMPL, D3D_DRIVER_TYPE_UNKNOWN, 2 },
			{ false, true, E_FAIL, S_OK, D3D_DRIVER_TYPE_WARP, 1 },
			{ false, true, S_OK, E_NOTIMPL, D3D_DRIVER_TYPE_UNKNOWN, 1 },
			// “硬件加速”档：硬件成功照常使用，失败不回退 WARP。
			{ true, false, S_OK, S_OK, D3D_DRIVER_TYPE_HARDWARE, 1 },
			{ true, false, E_FAIL, S_OK, D3D_DRIVER_TYPE_UNKNOWN, 1 },
			// 软件档不受回退开关影响。
			{ false, false, E_FAIL, S_OK, D3D_DRIVER_TYPE_WARP, 1 },
		};
		for (const auto& test : cases)
		{
			unsigned attempts = 0;
			bool orderValid = true;
			D3D_DRIVER_TYPE actual = D3D_DRIVER_TYPE_UNKNOWN;
			const HRESULT result = CreateSelectedDevice(test.enabled, test.fallback, actual,
				[&](D3D_DRIVER_TYPE requested)
				{
					const auto expected = test.enabled && attempts == 0
						? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
					orderValid = orderValid && requested == expected;
					++attempts;
					return requested == D3D_DRIVER_TYPE_HARDWARE ? test.hardwareResult : test.warpResult;
				});
			if (!orderValid || attempts != test.expectedAttempts || actual != test.expectedDriver ||
				result != (test.enabled && (SUCCEEDED(test.hardwareResult) || !test.fallback) ? test.hardwareResult : test.warpResult))
			{
				std::fputs("[Draw3DevicePolicy] FAIL: hardware/WARP selection or fallback order\n", stderr);
				return 1;
			}
		}
		// 适配器分类：混合显卡标志优先，其次按独占显存（≤ 512 MiB 为核显）。
		using Kind = GraphicsAdapterKind;
		using Hybrid = GraphicsAdapterHybridFlag;
		using Preference = GraphicsAdapterPreference;
		constexpr std::uint64_t MiB = 1ull << 20;
		if (ClassifyGraphicsAdapter(128 * MiB, Hybrid::Unknown) != Kind::Integrated ||
			ClassifyGraphicsAdapter(512 * MiB, Hybrid::Unknown) != Kind::Integrated ||
			ClassifyGraphicsAdapter(977 * MiB, Hybrid::Unknown) != Kind::Discrete ||
			ClassifyGraphicsAdapter(2048 * MiB, Hybrid::Integrated) != Kind::Integrated ||
			ClassifyGraphicsAdapter(128 * MiB, Hybrid::Discrete) != Kind::Discrete)
		{
			std::fputs("[Draw3DevicePolicy] FAIL: adapter classification\n", stderr);
			return 1;
		}
		// 尝试顺序：自动保持枚举顺序；优先某类时先该类、后另一类，类内保持枚举顺序。
		const std::vector<Kind> kinds{ Kind::Integrated, Kind::Discrete, Kind::Integrated, Kind::Discrete };
		const std::vector<std::size_t> automaticOrder{ 0, 1, 2, 3 };
		const std::vector<std::size_t> integratedOrder{ 0, 2, 1, 3 };
		const std::vector<std::size_t> discreteOrder{ 1, 3, 0, 2 };
		const std::vector<Kind> onlyDiscrete{ Kind::Discrete };
		if (OrderGraphicsAdapters(kinds, Preference::Automatic) != automaticOrder ||
			OrderGraphicsAdapters(kinds, Preference::Integrated) != integratedOrder ||
			OrderGraphicsAdapters(kinds, Preference::Discrete) != discreteOrder ||
			OrderGraphicsAdapters(onlyDiscrete, Preference::Integrated) != std::vector<std::size_t>{ 0 } ||
			!OrderGraphicsAdapters({}, Preference::Discrete).empty())
		{
			std::fputs("[Draw3DevicePolicy] FAIL: adapter preference order\n", stderr);
			return 1;
		}
		if (RunDiagnosticFormattingProductionTest() != 0) return 1;
		std::fputs("[Draw3DevicePolicy] PASS: direct WARP, hardware preference, failure fallback, hardware-only and adapter order\n", stderr);
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
