#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "Helper.Fatal.h"

#include "../Log/Log.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <intrin.h>

LONG* GetOffSignalInteropPointer();

namespace Inkeys::Fatal
{
	extern std::atomic<HANDLE> g_watchdogWake;

	namespace
	{
		std::atomic<CrashCore> g_crashCore{ nullptr };
		// 正在执行 UEF 处理的线程；0 表示尚未开始。
		std::atomic<DWORD> g_crashThread{ 0 };

		std::atomic<bool> g_watchdogArmed{ false };
		std::atomic<bool> g_watchdogStop{ false };
		HANDLE g_watchdogThread = nullptr;

		LONG ExitIntent() noexcept
		{
			LONG* slot = GetOffSignalInteropPointer();
			return slot ? InterlockedCompareExchange(slot, 0, 0) : 0;
		}

		bool ExitIntentClaimed() noexcept { return ExitIntent() != 0; }

		[[noreturn]] void ParkForever() noexcept
		{
			// 另一线程正在处理崩溃，由它结束进程；本线程不得再触发第二次处理或提前结束进程。
			for (;;) Sleep(INFINITE);
		}

		Detail::WatchdogInput SampleForWatchdog() noexcept
		{
			Detail::WatchdogInput input = Detail::Sample(Detail::NowMs());
			input.exitIntentClaimed = ExitIntentClaimed();
			input.crashHandling = g_crashThread.load(std::memory_order_acquire) != 0;
			input.debuggerPresent = IsDebuggerPresent() != FALSE;
			return input;
		}

		DWORD WINAPI WatchdogThread(void*) noexcept
		{
			bool debuggerLogged = false;
			while (!g_watchdogStop.load(std::memory_order_acquire))
			{
				const HANDLE wake = g_watchdogWake.load(std::memory_order_acquire);
				if (wake) (void)WaitForSingleObject(wake, 500);
				else Sleep(500);
				if (g_watchdogStop.load(std::memory_order_acquire)) break;

				const Detail::WatchdogInput input = SampleForWatchdog();
				if (input.debuggerPresent && !debuggerLogged)
				{
					debuggerLogged = true;
					Inkeys::Log::TryFormat(Inkeys::Log::Level::Warn, Inkeys::Log::Module::Main,
						"已附加调试器，教学安全模式看门狗不会触发");
				}
				const Detail::WatchdogDecision decision = Detail::EvaluateWatchdog(input);
				if (!decision.trigger) continue;
				// 触发前重新取样复验：期间完成的 Beat、成功提交或已受理的退出都会撤销本次判定
				// （退出已受理时交给退出流程自己的 15 秒监督）。
				if (g_watchdogStop.load(std::memory_order_acquire)) break;
				Inkeys::Log::TryFormat(Inkeys::Log::Level::Critical, Inkeys::Log::Module::Main,
					"教学安全模式兜底判定（复验通过即触发）：原因={}，detail={}", ReasonName(decision.reason), decision.detail);
				// 以最后一次复验为判定点，复验与触发之间不再有其他工作。
				const Detail::WatchdogDecision confirm = Detail::EvaluateWatchdog(SampleForWatchdog());
				if (!confirm.trigger || confirm.reason != decision.reason || confirm.detail != decision.detail) continue;
				RaiseNow(decision.reason, decision.detail);
			}
			return 0;
		}

		// CRT 入口：首次成功安装时保存旧值；部分安装只还原已装上的项。
		struct CrtHandlerState
		{
			bool installed = false;
			bool abortInstalled = false;
			_crt_signal_t previousAbort = SIG_DFL;
			_invalid_parameter_handler previousInvalidParameter = nullptr;
			_purecall_handler previousPureCall = nullptr;
			unsigned int previousAbortBehavior = 0;
		};
		CrtHandlerState g_crt;

		void __cdecl OnAbortSignal(int)
		{
			// CRT 进入处理器前已把 SIGABRT 复位为默认；立即重装，避免另一线程并发 abort 走默认 _exit 抢先结束进程。
			(void)signal(SIGABRT, OnAbortSignal);
			RaiseNow(Reason::CrtAbort);
		}
		void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
		{
			RaiseNow(Reason::CrtInvalidParameter);
		}
		void __cdecl OnPureCall() { RaiseNow(Reason::CrtPureCall); }
	}

	const char* ReasonName(Reason reason) noexcept
	{
		switch (reason)
		{
		case Reason::Draw3Stopped: return "Draw3Stopped";
		case Reason::Draw3PresentFailures: return "Draw3PresentFailures";
		case Reason::UiDeviceRecovery: return "UiDeviceRecovery";
		case Reason::UiClientFailures: return "UiClientFailures";
		case Reason::FreezeFailures: return "FreezeFailures";
		case Reason::ThreadHang: return "ThreadHang";
		case Reason::CrtAbort: return "CrtAbort";
		case Reason::CrtInvalidParameter: return "CrtInvalidParameter";
		case Reason::CrtPureCall: return "CrtPureCall";
		case Reason::DebugInjected: return "DebugInjected";
		case Reason::KnownFailureCleanup: return "KnownFailureCleanup";
		default: return "Unknown";
		}
	}

	[[noreturn]] void RaiseNow(Reason reason, std::uint32_t detail) noexcept
	{
		const DWORD self = GetCurrentThreadId();
		const DWORD handling = g_crashThread.load(std::memory_order_acquire);
		if (handling != 0 && handling != self) ParkForever();
		// 正常关闭或重启已受理：由退出 owner 的 15 秒监督结束进程，保留其意图（重启不被提前结束打断）。
		const LONG intent = ExitIntent();
		if (intent == 1 || intent == 2) ParkForever();

		// 合成异常记录后直接调用已注册的 UEF 处理核心，再自行结束进程。
		// 不依赖 RaiseException 穿过各层 C++ 帧（noexcept 帧可能转去 terminate），各入口行为一致。
		EXCEPTION_RECORD record{};
		record.ExceptionCode = kExceptionCode;
		record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
		record.ExceptionAddress = _ReturnAddress();
		record.NumberParameters = 2;
		record.ExceptionInformation[0] = static_cast<ULONG_PTR>(reason);
		record.ExceptionInformation[1] = static_cast<ULONG_PTR>(detail);
		CONTEXT context{};
		RtlCaptureContext(&context);
		EXCEPTION_POINTERS pointers{ &record, &context };

		if (const CrashCore core = g_crashCore.load(std::memory_order_acquire))
		{
			(void)core(&pointers);
			// 另一线程已接管处理（本次进入核心时接管失败）：交由它结束进程。
			const DWORD owner = g_crashThread.load(std::memory_order_acquire);
			if (owner != 0 && owner != self) ParkForever();
		}
		// 处理期间正常关闭或重启刚被受理（其监督可能尚未建立）：同样交给退出 owner 结束进程。
		const LONG intentAfter = ExitIntent();
		if (intentAfter == 1 || intentAfter == 2) ParkForever();
		// 处理器未安装或已撤销（正常退出已受理）时直接结束进程，不再进入重启。
		TerminateProcess(GetCurrentProcess(), kExceptionCode);
		ParkForever();
	}

	bool StartWatchdog() noexcept
	{
		if (g_watchdogArmed.load(std::memory_order_acquire)) return true;
		HANDLE wake = g_watchdogWake.load(std::memory_order_acquire);
		if (!wake)
		{
			wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (!wake) return false;
			g_watchdogWake.store(wake, std::memory_order_release);
		}
		g_watchdogStop.store(false, std::memory_order_release);
		g_watchdogThread = CreateThread(nullptr, 64 * 1024, WatchdogThread, nullptr,
			STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
		if (!g_watchdogThread) return false;
		g_watchdogArmed.store(true, std::memory_order_release);
		// 启动前已有的报告立即处理。
		SetEvent(wake);
		return true;
	}

	void StopWatchdog() noexcept
	{
		if (!g_watchdogArmed.exchange(false, std::memory_order_acq_rel)) return;
		g_watchdogStop.store(true, std::memory_order_release);
		if (const HANDLE wake = g_watchdogWake.load(std::memory_order_acquire)) SetEvent(wake);
		if (g_watchdogThread)
		{
			(void)WaitForSingleObject(g_watchdogThread, 2000);
			CloseHandle(g_watchdogThread);
			g_watchdogThread = nullptr;
		}
		// 唤醒事件保留到进程结束：其他线程的 Report 可能仍在使用。
	}

	bool RuntimeArmed() noexcept
	{
		return g_watchdogArmed.load(std::memory_order_acquire);
	}

	void RegisterCrashCore(CrashCore core) noexcept
	{
		g_crashCore.store(core, std::memory_order_release);
	}

	CrashClaim ClaimCrashHandling() noexcept
	{
		const DWORD self = GetCurrentThreadId();
		DWORD expected = 0;
		if (g_crashThread.compare_exchange_strong(expected, self, std::memory_order_acq_rel))
			return CrashClaim::First;
		return expected == self ? CrashClaim::SameThread : CrashClaim::OtherThread;
	}

	void InstallCrtHandlers() noexcept
	{
		if (g_crt.installed) return;
		// 先关掉 Debug 弹窗与 fastfail：handler 万一返回也不绕过处理核心。
		g_crt.previousAbortBehavior = _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		const _crt_signal_t previous = signal(SIGABRT, OnAbortSignal);
		if (previous != SIG_ERR)
		{
			g_crt.previousAbort = previous;
			g_crt.abortInstalled = true;
		}
		else Inkeys::Log::TryFormat(Inkeys::Log::Level::Warn, Inkeys::Log::Module::Main,
			"SIGABRT 处理器安装失败，abort 与未捕获异常不会自动重启");
		g_crt.previousInvalidParameter = _set_invalid_parameter_handler(OnInvalidParameter);
		g_crt.previousPureCall = _set_purecall_handler(OnPureCall);
		g_crt.installed = true;
	}

	void RestoreCrtHandlers() noexcept
	{
		if (!g_crt.installed) return;
		(void)_set_purecall_handler(g_crt.previousPureCall);
		(void)_set_invalid_parameter_handler(g_crt.previousInvalidParameter);
		if (g_crt.abortInstalled) (void)signal(SIGABRT, g_crt.previousAbort);
		(void)_set_abort_behavior(g_crt.previousAbortBehavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		g_crt = {};
	}
}
