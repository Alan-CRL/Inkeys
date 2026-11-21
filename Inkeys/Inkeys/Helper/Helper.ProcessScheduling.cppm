module;

#include "../../IdtMain.h"

#include <mutex>

export module Inkeys.Helper.ProcessScheduling;

export namespace Inkeys::Helper
{
	// 设置页“GPU 调度优先级”三档，配置值即枚举值；越界按均衡处理。
	enum class GpuSchedulingTier : int { Standard = 0, Balanced = 1, Performance = 2 };

	struct ProcessSchedulingResult
	{
		bool priorityApiAvailable = false;
		long priorityStatus = 0;      // D3DKMTSetProcessSchedulingPriorityClass 返回的 NTSTATUS
		int priorityReadBack = -1;    // D3DKMTGetProcessSchedulingPriorityClass 读回的类值，失败为 -1
		bool throttlingApiAvailable = false;
		bool throttlingApplied = false;
		unsigned long throttlingError = 0;
	};

	GpuSchedulingTier NormalizeGpuSchedulingTier(int value) noexcept;
	// 档位到 D3DKMT_SCHEDULINGPRIORITYCLASS 值：NORMAL=2、HIGH=4、REALTIME=5。
	int GpuSchedulingClassForTier(GpuSchedulingTier tier) noexcept;
	// 把档位与电源节流应用到当前进程。线程安全、不抛异常；参数与上次相同时跳过系统调用并返回上次结果。
	// disablePowerThrottling=true 显式关闭执行速度节流；false 交还系统默认（不是强制节流）。
	ProcessSchedulingResult ApplyProcessScheduling(GpuSchedulingTier tier, bool disablePowerThrottling) noexcept;
}

namespace
{
	// d3dkmthk.h 的 D3DKMT_SCHEDULINGPRIORITYCLASS 取值；不包含该头文件，按 SDK 10.0.26100 声明自行定义。
	constexpr int kSchedulingClassNormal = 2;
	constexpr int kSchedulingClassHigh = 4;
	constexpr int kSchedulingClassRealtime = 5;

	// gdi32 自 Win7 起导出；仍按名称动态获取，缺失时只记日志。
	using SetSchedulingClassFn = LONG(APIENTRY*)(HANDLE, int);
	using GetSchedulingClassFn = LONG(APIENTRY*)(HANDLE, int*);
	// kernel32 自 Win8 起才导出：静态调用会让 Win7 无法启动，必须动态获取。
	using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

	struct OsVersion { DWORD major = 0, minor = 0, build = 0; };

	OsVersion QueryOsVersion() noexcept
	{
		using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
		const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		const auto rtlGetVersion = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
		RTL_OSVERSIONINFOW version{};
		version.dwOSVersionInfoSize = sizeof(version);
		if (!rtlGetVersion || rtlGetVersion(&version) != 0) return {};
		return { version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber };
	}

	const char* TierName(Inkeys::Helper::GpuSchedulingTier tier) noexcept
	{
		switch (tier)
		{
		case Inkeys::Helper::GpuSchedulingTier::Standard: return "standard";
		case Inkeys::Helper::GpuSchedulingTier::Performance: return "performance";
		default: return "balanced";
		}
	}

	std::mutex applyMutex;
	bool hasApplied = false;
	Inkeys::Helper::GpuSchedulingTier lastTier = Inkeys::Helper::GpuSchedulingTier::Balanced;
	bool lastDisableThrottling = false;
	Inkeys::Helper::ProcessSchedulingResult lastResult;
}

namespace Inkeys::Helper
{
	GpuSchedulingTier NormalizeGpuSchedulingTier(int value) noexcept
	{
		return value >= static_cast<int>(GpuSchedulingTier::Standard) && value <= static_cast<int>(GpuSchedulingTier::Performance)
			? static_cast<GpuSchedulingTier>(value) : GpuSchedulingTier::Balanced;
	}

	int GpuSchedulingClassForTier(GpuSchedulingTier tier) noexcept
	{
		switch (tier)
		{
		case GpuSchedulingTier::Standard: return kSchedulingClassNormal;
		case GpuSchedulingTier::Performance: return kSchedulingClassRealtime;
		default: return kSchedulingClassHigh;
		}
	}

	ProcessSchedulingResult ApplyProcessScheduling(GpuSchedulingTier tier, bool disablePowerThrottling) noexcept
	{
		tier = NormalizeGpuSchedulingTier(static_cast<int>(tier));
		std::lock_guard lock(applyMutex);
		if (hasApplied && tier == lastTier && disablePowerThrottling == lastDisableThrottling) return lastResult;

		ProcessSchedulingResult result;
		const HANDLE process = GetCurrentProcess();
		const int schedulingClass = GpuSchedulingClassForTier(tier);

		// GPU 调度类：Win11 实测 NORMAL/HIGH 在节流状态下实际优先级都是 1，REALTIME 为 23（与 DWM 同级）。
		if (const HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll"))
		{
			const auto setClass = reinterpret_cast<SetSchedulingClassFn>(GetProcAddress(gdi32, "D3DKMTSetProcessSchedulingPriorityClass"));
			const auto getClass = reinterpret_cast<GetSchedulingClassFn>(GetProcAddress(gdi32, "D3DKMTGetProcessSchedulingPriorityClass"));
			if (setClass)
			{
				result.priorityApiAvailable = true;
				result.priorityStatus = setClass(process, schedulingClass);
			}
			int readBack = -1;
			if (getClass && getClass(process, &readBack) == 0) result.priorityReadBack = readBack;
		}

		// 电源节流（EcoQoS）：Windows 把“有可见窗口但不在前台”的进程降为后台 QoS，GPU 调度优先级随之降到 1。
		// 显式关闭执行速度节流后恢复为 16；关闭开关时 ControlMask=0 交还系统决定，而不是强制开启节流。
		// Win10 1709 之前没有该信息类，调用返回失败，只记录错误码。
		if (const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll"))
		{
			const auto setInformation = reinterpret_cast<SetProcessInformationFn>(GetProcAddress(kernel32, "SetProcessInformation"));
			if (setInformation)
			{
				result.throttlingApiAvailable = true;
				PROCESS_POWER_THROTTLING_STATE state{};
				state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
				state.ControlMask = disablePowerThrottling ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
				state.StateMask = 0;
				result.throttlingApplied = setInformation(process, ProcessPowerThrottling, &state, sizeof(state)) != FALSE;
				if (!result.throttlingApplied) result.throttlingError = GetLastError();
			}
		}

		const OsVersion os = QueryOsVersion();
		Inkeys::Log::Info(Inkeys::Log::Module::Draw3,
			"进程调度 tier={} class={} disableThrottling={} priorityApi={} status={:#010x} readBack={} throttlingApi={} throttlingApplied={} throttlingError={} os={}.{}.{}",
			TierName(tier), schedulingClass, disablePowerThrottling, result.priorityApiAvailable,
			static_cast<unsigned long>(result.priorityStatus), result.priorityReadBack,
			result.throttlingApiAvailable, result.throttlingApplied, result.throttlingError, os.major, os.minor, os.build);

		hasApplied = true;
		lastTier = tier;
		lastDisableThrottling = disablePowerThrottling;
		lastResult = result;
		return result;
	}
}
