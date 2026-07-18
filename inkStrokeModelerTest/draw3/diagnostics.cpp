module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <iomanip>
#include <iostream>
#include <thread>
#include <windows.h>

module draw3.diagnostics;

namespace draw3
{
	namespace
	{
#if defined(_DEBUG)
		void WriteFastConsoleLine(const char* text, DWORD length)
		{
			static HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE); // 缓存控制台句柄，减少每帧日志开销。
			if (!consoleHandle || consoleHandle == INVALID_HANDLE_VALUE || length == 0) return;
			DWORD written = 0;
			if (!WriteConsoleA(consoleHandle, text, length, &written, nullptr))
			{
				WriteFile(consoleHandle, text, length, &written, nullptr); // 输出被重定向时 WriteConsoleA 会失败，改用 WriteFile。
			}
		}
#endif

		const char* DxgiAlphaModeName(DXGI_ALPHA_MODE mode)
		{
			switch (mode)
			{
			case DXGI_ALPHA_MODE_UNSPECIFIED: return "UNSPECIFIED";
			case DXGI_ALPHA_MODE_PREMULTIPLIED: return "PREMULTIPLIED";
			case DXGI_ALPHA_MODE_STRAIGHT: return "STRAIGHT";
			case DXGI_ALPHA_MODE_IGNORE: return "IGNORE";
			default: return "UNKNOWN";
			}
		}

		const char* DxgiSwapEffectName(DXGI_SWAP_EFFECT effect)
		{
			switch (effect)
			{
			case DXGI_SWAP_EFFECT_DISCARD: return "DISCARD";
			case DXGI_SWAP_EFFECT_SEQUENTIAL: return "SEQUENTIAL";
			case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "FLIP_SEQUENTIAL";
			case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "FLIP_DISCARD";
			default: return "UNKNOWN";
			}
		}

		const char* DxgiFormatName(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
			case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
			default: return "OTHER";
			}
		}
	}

	double GetQpcTimeMilliseconds()
	{
		static LARGE_INTEGER frequency = {};
		if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency); // 频率固定，初始化一次即可。
		LARGE_INTEGER counter = {};
		QueryPerformanceCounter(&counter);
		return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	}

	void HighPrecisionWait(double frameTimeSpentMs, double targetFPS)
	{
		const double waitTimeMs = 1000.0 / targetFPS - frameTimeSpentMs; // 扣除本帧工作耗时后再补足目标帧间隔。
		if (waitTimeMs <= 0.0) return;

		static LARGE_INTEGER frequency = {};
		if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
		LARGE_INTEGER startCounter = {};
		LARGE_INTEGER currentCounter = {};
		QueryPerformanceCounter(&startCounter);
		const long long waitTicks = static_cast<long long>(waitTimeMs * static_cast<double>(frequency.QuadPart) / 1000.0);
		const long long targetEndTick = startCounter.QuadPart + waitTicks; // 转成 QPC tick，最后用忙等对齐。

		// 先粗略睡眠，最后约 1.5ms 使用忙等待收紧帧间隔。
		if (waitTimeMs > 2.0)
		{
			std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(waitTimeMs - 1.5));
		}
		do
		{
			QueryPerformanceCounter(&currentCounter);
			YieldProcessor(); // 短忙等阶段降低自旋对 CPU 的压力。
		} while (currentCounter.QuadPart < targetEndTick);
	}

	void LogFrameTiming(size_t committedIndex, size_t realPointCount, size_t predictedPointCount,
		size_t l0PointCount, double workMs, double previousFrameMs, bool idleFrozen)
	{
#if defined(_DEBUG)
		static double nextLogTimeMs = 0.0;
		const double nowMs = GetQpcTimeMilliseconds();
		if (nowMs < nextLogTimeMs) return;
		nextLogTimeMs = nowMs + 250.0; // Debug 最多每秒输出四次，避免逐帧日志扰动建模。
		const int logicFps = workMs > 0.001 ? static_cast<int>(1000.0 / workMs) : 0; // 只看本帧代码实际工作耗时。
		const int realFps = previousFrameMs > 0.001 ? static_cast<int>(1000.0 / previousFrameMs) : 0; // 包含等待后的真实帧间隔。
		char buffer[256] = {};
		const int length = std::snprintf(buffer, sizeof(buffer),
			"commit:%zu work:%.3fms logic:%d FPS prev-real:%d FPS(%.3fms) realPts:%zu predPts:%zu l0Pts:%zu frozen:%d\r\n",
			committedIndex, workMs, logicFps, realFps, previousFrameMs, realPointCount,
			predictedPointCount, l0PointCount, idleFrozen ? 1 : 0);
		if (length <= 0) return;
		WriteFastConsoleLine(buffer, static_cast<DWORD>(std::min(length, static_cast<int>(sizeof(buffer) - 1))));
#else
		(void)committedIndex;
		(void)realPointCount;
		(void)predictedPointCount;
		(void)l0PointCount;
		(void)workMs;
		(void)previousFrameMs;
		(void)idleFrozen;
#endif
	}

	void LogHResult(const char* step, HRESULT result)
	{
		std::cout << step << " failed. HRESULT=0x" << std::hex
			<< static_cast<unsigned long>(result) << std::dec << std::endl;
	}

	void LogWin32Error(const char* step, DWORD error)
	{
		std::cout << step << " failed. GetLastError=" << error << std::endl;
	}

	const char* BoolText(BOOL value)
	{
		return value ? "true" : "false";
	}

	std::string WideToUtf8(const WCHAR* text)
	{
		if (!text || text[0] == L'\0') return {};
		const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr); // 先查询 UTF-8 缓冲区大小。
		if (requiredSize <= 1) return {};
		std::string result(static_cast<size_t>(requiredSize - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), requiredSize, nullptr, nullptr); // 去掉末尾 NUL 后返回 std::string。
		return result;
	}

	bool LogDwmColorizationState(const char* modeName, const char* stage, BOOL* outOpaqueBlend)
	{
		DWORD colorizationColor = 0;
		BOOL opaqueBlend = TRUE;
		const HRESULT result = DwmGetColorizationColor(&colorizationColor, &opaqueBlend);
		if (FAILED(result))
		{
			std::cout << "[" << modeName << "] " << stage
				<< " DwmGetColorizationColor failed. HRESULT=0x" << std::hex
				<< static_cast<unsigned long>(result) << std::dec << std::endl;
			return false;
		}
		std::cout << "[" << modeName << "] " << stage << " DwmColorizationColor=0x"
			<< std::hex << static_cast<unsigned long>(colorizationColor) << std::dec
			<< " opaqueBlend=" << BoolText(opaqueBlend) << std::endl;
		if (outOpaqueBlend) *outOpaqueBlend = opaqueBlend;
		return true;
	}

	void LogDwmModeDiagnostics(const char* modeName, HWND window, const char* stage)
	{
		if (!window) return;
		RECT windowRect = {};
		RECT clientRect = {};
		GetWindowRect(window, &windowRect); // 同时记录窗口和客户区，排查边框样式导致的尺寸偏差。
		GetClientRect(window, &clientRect);
		std::cout << "[" << modeName << "] " << stage
			<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window)
			<< " style=0x" << static_cast<unsigned long>(GetWindowLongPtr(window, GWL_STYLE))
			<< " exStyle=0x" << static_cast<unsigned long>(GetWindowLongPtr(window, GWL_EXSTYLE)) << std::dec
			<< " window=(" << windowRect.left << "," << windowRect.top << "," << windowRect.right << "," << windowRect.bottom << ")"
			<< " client=(" << clientRect.left << "," << clientRect.top << "," << clientRect.right << "," << clientRect.bottom << ")" << std::endl;

		BOOL compositionEnabled = FALSE;
		const HRESULT result = DwmIsCompositionEnabled(&compositionEnabled);
		if (SUCCEEDED(result))
		{
			std::cout << "[" << modeName << "] " << stage
				<< " DwmIsCompositionEnabled=" << BoolText(compositionEnabled) << std::endl;
		}
		else
		{
			LogHResult("DwmIsCompositionEnabled", result);
		}
		LogDwmColorizationState(modeName, stage);
	}

	void LogSwapChainDescription(const char* modeName, const char* stage, const DXGI_SWAP_CHAIN_DESC1& description)
	{
		std::cout << "[" << modeName << "] " << stage << " swapchain desc: size="
			<< description.Width << "x" << description.Height
			<< " format=" << DxgiFormatName(description.Format) << "(" << static_cast<unsigned int>(description.Format) << ")"
			<< " bufferCount=" << description.BufferCount
			<< " swapEffect=" << DxgiSwapEffectName(description.SwapEffect)
			<< " alphaMode=" << DxgiAlphaModeName(description.AlphaMode)
			<< " scaling=" << static_cast<unsigned int>(description.Scaling)
			<< " flags=0x" << std::hex << static_cast<unsigned int>(description.Flags) << std::dec << std::endl;
	}

	void LogSwapChainRuntimeDescription(const char* modeName, IDXGISwapChain1* swapChain, const char* stage)
	{
		if (!swapChain) return;
		DXGI_SWAP_CHAIN_DESC1 description = {};
		const HRESULT result = swapChain->GetDesc1(&description);
		if (FAILED(result))
		{
			LogHResult("IDXGISwapChain1::GetDesc1", result);
			return;
		}
		LogSwapChainDescription(modeName, stage, description);
	}

	void LogAdapterDiagnostics(IDXGIAdapter* adapter)
	{
		if (!adapter) return;
		DXGI_ADAPTER_DESC description = {};
		HRESULT result = adapter->GetDesc(&description);
		if (SUCCEEDED(result))
		{
			std::cout << "DXGI adapter: " << WideToUtf8(description.Description)
				<< " VendorId=0x" << std::hex << description.VendorId
				<< " DeviceId=0x" << description.DeviceId
				<< " SubSysId=0x" << description.SubSysId
				<< " Revision=0x" << description.Revision << std::dec
				<< " DedicatedVideoMemory=" << static_cast<unsigned long long>(description.DedicatedVideoMemory)
				<< " DedicatedSystemMemory=" << static_cast<unsigned long long>(description.DedicatedSystemMemory)
				<< " SharedSystemMemory=" << static_cast<unsigned long long>(description.SharedSystemMemory) << std::endl;
		}
		else
		{
			LogHResult("IDXGIAdapter::GetDesc", result);
		}

		LARGE_INTEGER driverVersion = {};
		result = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
		if (SUCCEEDED(result))
		{
			std::cout << "DXGI adapter UMD driver version raw: high=0x" << std::hex
				<< static_cast<unsigned long>(driverVersion.HighPart) << " low=0x"
				<< static_cast<unsigned long>(driverVersion.LowPart) << std::dec << std::endl;
		}
		else
		{
			LogHResult("IDXGIAdapter::CheckInterfaceSupport(IDXGIDevice)", result);
		}
	}
}
