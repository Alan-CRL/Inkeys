#pragma once

#include <Windows.h>
#include <string>

namespace Inkeys::Shutdown
{
	enum class Intent : unsigned char
	{
		Close,
		Restart,
		CrashRestart
	};

	enum class ArmResult : unsigned char
	{
		Armed,
		AlreadyArmed,
		Failed,
		FallbackArmed
	};

	// 必须在任何可能阻塞的窗口、保存或线程清理之前调用。失败时 GetLastError 保留原因。
	ArmResult ArmShutdownSupervisor(Intent intent, DWORD deadlineMilliseconds = 15000) noexcept;

	// 仅首次普通退出owner的合法Arm已结束为双失败(state3)时调用；当前线程按原截止退场。
	// 不进入业务清理、不创建线程/helper；Restart退场不代表新实例已建立。
	[[noreturn]] void EnforceFailedShutdownDeadline(Intent intent) noexcept;

	// 只读原退出绝对tick；失败清理接管只取更早截止，不在此补建监督。
	ULONGLONG PublishedShutdownDeadlineTick() noexcept;
	// 已不可恢复清理的静态前奏，仅CAS/原子关门/唤醒；调用者自己承担noreturn截止。
	ULONGLONG PublishFatalFailedCleanupNoWait() noexcept;

	// 隔离诊断只复用参数转义和镜像身份；各purpose仍独立核自己的目录与三个继承句柄。
	namespace DiagnosticsProcess
	{
		std::wstring QuoteArgument(const std::wstring& argument);
		bool CurrentImage(std::wstring& image);
		bool ProcessImage(HANDLE process, std::wstring& image);
		bool SameImageFile(const std::wstring& left, const std::wstring& right) noexcept;
		bool StartInherited(const std::wstring& image, const std::wstring& quotedArguments,
			STARTUPINFOEXW& startup, PROCESS_INFORMATION& process) noexcept;
	}

	// UEF 手动确认/自动模式共用的退出意图仲裁；只在实际获准重启时占用 0→3。
	bool TryClaimCrashRestartIntent(LONG* intentSlot, int crashMode,
		bool userAccepted) noexcept;

	// 在 wWinMain 最早处调用；识别内部子进程或显式无窗口测试后直接返回 exitCode。
	bool TryRunShutdownSupervisorEarly(LPCWSTR fullCommandLine, int& exitCode) noexcept;

	// 仅经复制EXE、父进程句柄和私有映射全部鉴权的启动故障child可命中。
	enum class StartupFailureSite : DWORD { None, Com, PptCom, Font, BarState };
	bool IsAuthorizedStartupFailureChild() noexcept;
	bool IsAuthorizedStartupFailure(StartupFailureSite site) noexcept;
	void PublishAuthorizedStartupFailure(StartupFailureSite site,
		DWORD failureCode, HRESULT actualResult) noexcept;
	void HoldAuthorizedStartupBoundary(DWORD failureCode, DWORD boundary) noexcept;
}
