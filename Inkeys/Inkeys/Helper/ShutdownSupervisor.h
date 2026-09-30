#pragma once

#include <Windows.h>

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

	// UEF 手动确认/自动模式共用的退出意图仲裁；只在实际获准重启时占用 0→3。
	bool TryClaimCrashRestartIntent(LONG* intentSlot, int crashMode,
		bool userAccepted) noexcept;

	// 在 wWinMain 最早处调用；识别内部子进程或显式无窗口测试后直接返回 exitCode。
	bool TryRunShutdownSupervisorEarly(LPCWSTR fullCommandLine, int& exitCode) noexcept;
}
