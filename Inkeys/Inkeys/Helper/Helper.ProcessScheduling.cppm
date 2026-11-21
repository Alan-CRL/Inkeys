module;

#include "../../IdtMain.h"

#include <array>
#include <cstdio>
#include <mutex>
#include <string>

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
		int requestedClass = -1;
		int appliedClass = -1;
		// 当前应生效的类：本次有成功请求时取它，本次全部失败时沿用上一次的（系统保持原类不变），仅用于读回失败时显示。
		int currentClass = -1;
		int attemptCount = 0;
		bool disablePowerThrottlingRequested = false;
		bool valid = false;
	};

	struct SchedulingAttemptResult
	{
		int appliedClass = -1;
		int attempts = 0;
		long statuses[3]{};
		int classes[3]{};
		long lastStatus = 0;
	};

	GpuSchedulingTier NormalizeGpuSchedulingTier(int value) noexcept;
	// 档位到 D3DKMT_SCHEDULINGPRIORITYCLASS 值：NORMAL=2、HIGH=4、REALTIME=5。
	int GpuSchedulingClassForTier(GpuSchedulingTier tier) noexcept;
	int SchedulingFallbackRoute(GpuSchedulingTier tier, int (&out)[3]) noexcept;
	// 本次全部请求失败时系统保持原类，诊断沿用上一次的当前类；有成功请求时取成功的类。
	constexpr int NextCurrentSchedulingClass(int appliedClass, int previousCurrentClass) noexcept
	{
		return appliedClass >= 0 ? appliedClass : previousCurrentClass;
	}

	template<class SetFn>
	SchedulingAttemptResult RunSchedulingFallback(GpuSchedulingTier tier, SetFn&& set) noexcept
	{
		int route[3]{};
		const int count = SchedulingFallbackRoute(tier, route);
		SchedulingAttemptResult result;
		for (int i = 0; i < count; ++i)
		{
			const int schedulingClass = route[i];
			const long status = set(schedulingClass);
			result.classes[i] = schedulingClass;
			result.statuses[i] = status;
			result.lastStatus = status;
			++result.attempts;
			// NTSTATUS 非负即成功；读回只用于展示，不能触发降档。
			if (status >= 0)
			{
				result.appliedClass = schedulingClass;
				break;
			}
		}
		return result;
	}

	// 把档位与电源节流应用到当前进程。线程安全、不抛异常；参数与上次相同时跳过系统调用并返回上次结果。
	// disablePowerThrottling=true 显式关闭执行速度节流；false 交还系统默认（不是强制节流）。
	ProcessSchedulingResult ApplyProcessScheduling(GpuSchedulingTier tier, bool disablePowerThrottling) noexcept;
	ProcessSchedulingResult LastProcessSchedulingResult() noexcept;
	std::string FormatProcessSchedulingInformation(const ProcessSchedulingResult& result) noexcept;
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
	Inkeys::Helper::GpuSchedulingTier lastTier = Inkeys::Helper::GpuSchedulingTier::Balanced;
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

	int SchedulingFallbackRoute(GpuSchedulingTier tier, int (&out)[3]) noexcept
	{
		const int requested = GpuSchedulingClassForTier(tier);
		int count = 0;
		out[count++] = requested;
		if (requested == kSchedulingClassRealtime) out[count++] = kSchedulingClassHigh;
		if (requested != kSchedulingClassNormal) out[count++] = kSchedulingClassNormal;
		return count;
	}

	ProcessSchedulingResult LastProcessSchedulingResult() noexcept
	{
		std::lock_guard lock(applyMutex);
		return lastResult;
	}

	std::string FormatProcessSchedulingInformation(const ProcessSchedulingResult& result) noexcept
	{
		try
		{
			if (!result.valid) return "GPU 调度类：尚未应用\n电源节流：尚未应用\n";

			// 优先显示系统读回值；读回失败时标明显示的是最近一次成功请求。
			const int schedulingClass = result.priorityReadBack >= 0 ? result.priorityReadBack : result.currentClass;
			std::string text = "GPU 调度类：";
			if (schedulingClass >= 0)
			{
				constexpr std::array names{ "IDLE", "BELOW_NORMAL", "NORMAL", "ABOVE_NORMAL", "HIGH", "REALTIME" };
				const char* name = schedulingClass < static_cast<int>(names.size()) ? names[schedulingClass] : "未知";
				text += fmt::format("{} ({})", name, schedulingClass);
				if (result.priorityReadBack < 0) text += "（未读回）";
			}
			else
				text += "不可用";

			text += "\n电源节流：";
			if (!result.throttlingApiAvailable ||
				(!result.throttlingApplied && result.throttlingError == ERROR_INVALID_PARAMETER))
				text += "不支持";
			else if (result.throttlingApplied)
				text += result.disablePowerThrottlingRequested ? "已关闭执行速度节流" : "系统默认";
			else
				text += fmt::format("设置失败（错误码 {}）", result.throttlingError);
			text += '\n';
			return text;
		}
		catch (...)
		{
			return {};
		}
	}

	ProcessSchedulingResult ApplyProcessScheduling(GpuSchedulingTier tier, bool disablePowerThrottling) noexcept
	{
		tier = NormalizeGpuSchedulingTier(static_cast<int>(tier));
		std::lock_guard lock(applyMutex);
		if (lastResult.valid && tier == lastTier && disablePowerThrottling == lastResult.disablePowerThrottlingRequested)
			return lastResult;

		ProcessSchedulingResult result;
		result.requestedClass = GpuSchedulingClassForTier(tier);
		result.disablePowerThrottlingRequested = disablePowerThrottling;
		SchedulingAttemptResult attempts;
		const HANDLE process = GetCurrentProcess();

		// GPU 调度类：Win11 实测 NORMAL/HIGH 在节流状态下实际优先级都是 1，REALTIME 为 23（与 DWM 同级）。
		if (const HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll"))
		{
			const auto setClass = reinterpret_cast<SetSchedulingClassFn>(GetProcAddress(gdi32, "D3DKMTSetProcessSchedulingPriorityClass"));
			const auto getClass = reinterpret_cast<GetSchedulingClassFn>(GetProcAddress(gdi32, "D3DKMTGetProcessSchedulingPriorityClass"));
			if (setClass)
			{
				result.priorityApiAvailable = true;
				attempts = RunSchedulingFallback(tier, [&](int schedulingClass) noexcept
				{
					return setClass(process, schedulingClass);
				});
				result.priorityStatus = attempts.lastStatus;
				result.appliedClass = attempts.appliedClass;
				result.attemptCount = attempts.attempts;
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

		// 至多三次尝试，每项为单数字类值、冒号及 32 位 NTSTATUS。
		char attemptLog[40]{};
		int offset = 0;
		for (int i = 0; i < attempts.attempts; ++i)
		{
			offset += std::snprintf(attemptLog + offset, sizeof(attemptLog) - offset, "%s%d:0x%08lx",
				i == 0 ? "" : ",", attempts.classes[i], static_cast<unsigned long>(attempts.statuses[i]));
		}

		const OsVersion os = QueryOsVersion();
		Inkeys::Log::Info(Inkeys::Log::Module::Draw3,
			"进程调度 tier={} requested={} attempts={} applied={} readBack={} disableThrottling={} priorityApi={} status={:#010x} throttlingApi={} throttlingApplied={} throttlingError={} os={}.{}.{}",
			TierName(tier), result.requestedClass, result.attemptCount ? attemptLog : "none",
			result.appliedClass, result.priorityReadBack, disablePowerThrottling, result.priorityApiAvailable,
			static_cast<unsigned long>(result.priorityStatus),
			result.throttlingApiAvailable, result.throttlingApplied, result.throttlingError, os.major, os.minor, os.build);

		result.currentClass = NextCurrentSchedulingClass(result.appliedClass, lastResult.currentClass);
		result.valid = true;
		lastTier = tier;
		lastResult = result;
		return result;
	}
}
