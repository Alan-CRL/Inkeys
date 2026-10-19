#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "KeyboardShortcuts.h"
#include "KeyboardShortcuts.Transport.h"

#include <atomic>
#include <cwchar>
#include <future>
#include <mutex>
#include <thread>

namespace
{
	namespace Shortcuts = Inkeys::Input::KeyboardShortcuts;
	std::mutex operationMutex, wakeMutex;
	std::jthread serviceThread;
	HANDLE wakeEvent = nullptr;
	HHOOK hookHandle = nullptr;
	std::atomic_bool running = false, hookInstalled = false, hotkeyRegistered = false;
	std::atomic_uint32_t hookError = 0, hotkeyError = 0;
	std::atomic_uint64_t lifecycle = 0;
	// mask 与代次在同一个原子值中发布，回调不会看到“新开关+旧代次”。
	std::atomic_uint64_t enabledState = (1ULL << 8) | Shortcuts::AllEnabled;
	Shortcuts::Transport::Bridge bridge;
	constexpr int HotkeyId = 0x4b58;

	void WakeService() noexcept
	{
		std::scoped_lock lock(wakeMutex);
		if (wakeEvent) (void)SetEvent(wakeEvent);
	}

	LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM lParam)
	{
		if (code < 0 || !running.load(std::memory_order_acquire) || !lParam
			|| (message != WM_KEYDOWN && message != WM_KEYUP && message != WM_SYSKEYDOWN && message != WM_SYSKEYUP))
			return CallNextHookEx(hookHandle, code, message, lParam);
		const auto& source = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
		Shortcuts::Transport::KeyEvent event;
		event.virtualKey = source.vkCode;
		event.scanCode = source.scanCode;
		event.flags = source.flags;
		event.time = source.time;
		event.extraInfo = source.dwExtraInfo;
		event.down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
		event.injected = (source.flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
		event.extended = (source.flags & LLKHF_EXTENDED) != 0;
		const auto enabled = enabledState.load(std::memory_order_acquire);
		const bool consumed = bridge.HandleKey(event,
			static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(GetForegroundWindow())), GetTickCount(),
			static_cast<std::uint32_t>(enabled & Shortcuts::AllEnabled), enabled >> 8,
			lifecycle.load(std::memory_order_acquire));
		if (consumed) return 1;
		return CallNextHookEx(hookHandle, code, message, lParam);
	}

	void ConfigureGlobalHotkey(bool& attempted, bool& requested, bool& registered,
		std::uint64_t& appliedConfiguration, DWORD& acceptAfter) noexcept
	{
		const auto configuration = enabledState.load(std::memory_order_acquire);
		if (configuration != appliedConfiguration)
		{
			appliedConfiguration = configuration;
			acceptAfter = GetTickCount();
			// 重新启用不复活旧注册消息；配置更新也退休已排队但未执行的 X。
			MSG retired{};
			for (unsigned count = 0; count < 256
				&& PeekMessageW(&retired, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE); ++count) {}
		}
		const bool next = (configuration & Shortcuts::Flag(Shortcuts::Command::ToggleAnnotation)) != 0;
		if (attempted && next == requested) return;
		attempted = true;
		requested = next;
		if (registered && !next)
		{
			if (!UnregisterHotKey(nullptr, HotkeyId))
			{
				hotkeyError.store(GetLastError(), std::memory_order_relaxed);
				return; // 注销失败仍报告实际保留状态，不假称该组合已经交还系统。
			}
			registered = false;
		}
		hotkeyError.store(0, std::memory_order_relaxed);
		if (next && !registered)
		{
			registered = RegisterHotKey(nullptr, HotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'X') != FALSE;
			if (!registered) hotkeyError.store(GetLastError(), std::memory_order_relaxed);
		}
		hotkeyRegistered.store(registered, std::memory_order_release);
	}

	bool ReadIntegrity(HANDLE process, DWORD& level) noexcept
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
		alignas(TOKEN_MANDATORY_LABEL) unsigned char storage[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE]{};
		DWORD required = 0;
		const bool read = GetTokenInformation(token, TokenIntegrityLevel, storage, sizeof(storage), &required) != FALSE;
		CloseHandle(token);
		if (!read) return false;
		const auto sid = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(storage)->Label.Sid;
		if (!IsValidSid(sid) || !*GetSidSubAuthorityCount(sid)) return false;
		level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
		return true;
	}

	bool SameInputDesktop() noexcept
	{
		const HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_HOOKCONTROL);
		if (!input) return false;
		wchar_t inputName[256]{}, threadName[256]{};
		DWORD needed = 0;
		const bool same = GetUserObjectInformationW(input, UOI_NAME, inputName, sizeof(inputName), &needed) &&
			GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
				threadName, sizeof(threadName), &needed) && std::wcscmp(inputName, threadName) == 0;
		CloseDesktop(input);
		return same;
	}

	bool BootstrapKeys() noexcept
	{
		// GetAsyncKeyState 仅用于启动校准；活跃桌面/访问权/前台完整性任一不可证实时，不把零当 Up。
		if (!SameInputDesktop()) return false;
		const HWND foreground = GetForegroundWindow();
		DWORD processId = 0;
		if (!foreground || !GetWindowThreadProcessId(foreground, &processId) || !processId) return false;
		DWORD ownIntegrity = 0, foregroundIntegrity = 0;
		if (!ReadIntegrity(GetCurrentProcess(), ownIntegrity)) return false;
		const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (!process) return false;
		const bool allowed = ReadIntegrity(process, foregroundIntegrity) && foregroundIntegrity <= ownIntegrity;
		CloseHandle(process);
		if (!allowed) return false;
		std::array<bool, 256> held{};
		for (const auto key : Shortcuts::Transport::BootstrapKeys) held[key] = GetAsyncKeyState(key) < 0;
		DWORD afterProcessId = 0;
		if (GetForegroundWindow() != foreground ||
			!GetWindowThreadProcessId(foreground, &afterProcessId) || afterProcessId != processId ||
			!SameInputDesktop()) return false;
		bridge.SeedInitialState(held);
		return true;
	}

	void Run(std::stop_token stop, std::promise<void> startup)
	{
		MSG message{};
		(void)PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
		HANDLE localWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!localWake)
		{
			const auto error = GetLastError();
			hookError.store(error, std::memory_order_relaxed);
			hotkeyError.store(error, std::memory_order_relaxed);
			startup.set_value();
			return;
		}
		{
			std::scoped_lock lock(wakeMutex);
			wakeEvent = localWake;
		}
		std::stop_callback onStop(stop, [] { WakeService(); });
		// 未取得可信启动快照前，未知按压整次 Forward；不从失败零值猜测 Up。
		bridge.ResetProducer();
		running.store(true, std::memory_order_release);
		hookHandle = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
		hookInstalled.store(hookHandle != nullptr, std::memory_order_release);
		hookError.store(hookHandle ? 0 : GetLastError(), std::memory_order_relaxed);
		bool baselineCaptured = BootstrapKeys();
		DWORD lastBootstrapAttempt = GetTickCount();
		bool attempted = false, requested = false, registered = false;
		std::uint64_t appliedConfiguration = 0;
		DWORD acceptAfter = 0;
		ConfigureGlobalHotkey(attempted, requested, registered, appliedConfiguration, acceptAfter);
		startup.set_value(); // 两个入口独立失败；一个失败不拆掉另一个。
		bool quit = false;
		while (!stop.stop_requested() && !quit)
		{
			ConfigureGlobalHotkey(attempted, requested, registered, appliedConfiguration, acceptAfter);
			const auto now = GetTickCount();
			if (!baselineCaptured && !bridge.Synchronized() && now - lastBootstrapAttempt >= 500)
			{
				lastBootstrapAttempt = now;
				baselineCaptured = BootstrapKeys();
			}
			const auto wait = MsgWaitForMultipleObjectsEx(1, &localWake, baselineCaptured || bridge.Synchronized() ? INFINITE : 500, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			if (wait == WAIT_OBJECT_0 || wait == WAIT_TIMEOUT) continue;
			if (wait != WAIT_OBJECT_0 + 1) break;
			while (!stop.stop_requested() && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_QUIT) { quit = true; break; }
				if (message.message == WM_HOTKEY && message.wParam == HotkeyId)
				{
					// LL 只观察 X；同一线程的 WM_HOTKEY 是往返意图的唯一 producer。
					const auto enabled = enabledState.load(std::memory_order_acquire);
					if (registered && enabled == appliedConfiguration
						&& Shortcuts::Transport::HotkeyAfterConfiguration(message.time, acceptAfter)) bridge.HandleHotkey(
						static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(GetForegroundWindow())), GetTickCount(),
						static_cast<std::uint32_t>(enabled & Shortcuts::AllEnabled), enabled >> 8,
						lifecycle.load(std::memory_order_acquire));
					continue;
				}
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		running.store(false, std::memory_order_release);
		if (registered) (void)UnregisterHotKey(nullptr, HotkeyId);
		hotkeyRegistered.store(false, std::memory_order_release);
		if (hookHandle) (void)UnhookWindowsHookEx(hookHandle);
		hookHandle = nullptr;
		hookInstalled.store(false, std::memory_order_release);
		{
			std::scoped_lock lock(wakeMutex);
			wakeEvent = nullptr;
		}
		CloseHandle(localWake);
	}

	void StopLocked() noexcept
	{
		running.store(false, std::memory_order_release);
		lifecycle.fetch_add(1, std::memory_order_acq_rel);
		if (!serviceThread.joinable()) return;
		serviceThread.request_stop();
		WakeService();
		serviceThread.join();
	}
}

namespace Inkeys::Input::KeyboardShortcuts
{
	Status Start()
	{
		std::scoped_lock lock(operationMutex);
		StopLocked();
		hookError.store(0, std::memory_order_relaxed);
		hotkeyError.store(0, std::memory_order_relaxed);
		std::promise<void> startup;
		auto completed = startup.get_future();
		try
		{
			serviceThread = std::jthread(Run, std::move(startup));
			completed.get();
		}
		catch (...)
		{
			StopLocked();
			hookError.store(ERROR_NOT_ENOUGH_MEMORY, std::memory_order_relaxed);
			hotkeyError.store(ERROR_NOT_ENOUGH_MEMORY, std::memory_order_relaxed);
		}
		return GetStatus();
	}
	void Stop() noexcept
	{
		std::scoped_lock lock(operationMutex);
		StopLocked();
	}
	void SetEnabled(std::uint32_t requested) noexcept
	{
		requested &= AllEnabled;
		auto previous = enabledState.load(std::memory_order_acquire);
		while ((previous & AllEnabled) != requested)
		{
			const auto next = (((previous >> 8) + 1) << 8) | requested;
			if (enabledState.compare_exchange_weak(previous, next, std::memory_order_acq_rel))
			{
				WakeService();
				return;
			}
		}
	}
	std::uint32_t GetEnabled() noexcept
	{
		return static_cast<std::uint32_t>(enabledState.load(std::memory_order_acquire) & AllEnabled);
	}
	Status GetStatus() noexcept
	{
		Status status;
		status.running = running.load(std::memory_order_acquire);
		status.hookInstalled = hookInstalled.load(std::memory_order_acquire);
		status.synchronizationPending = !bridge.Synchronized();
		status.hotkeyRegistered = hotkeyRegistered.load(std::memory_order_acquire);
		status.hookError = hookError.load(std::memory_order_relaxed);
		status.hotkeyError = hotkeyError.load(std::memory_order_relaxed);
		const auto enabled = enabledState.load(std::memory_order_acquire);
		status.enabledMask = static_cast<std::uint32_t>(enabled & AllEnabled);
		status.configGeneration = enabled >> 8;
		status.lifecycleGeneration = lifecycle.load(std::memory_order_acquire);
		bridge.ReadCounters(status);
		return status;
	}
	void PublishRoute(const RouteSnapshot& route) noexcept { bridge.PublishRoute(route); }
	bool TryPop(Intent& intent) noexcept
	{
		const auto currentService = lifecycle.load(std::memory_order_acquire);
		for (unsigned retired = 0; retired < 16 && bridge.TryPop(intent); ++retired)
		{
			if (running.load(std::memory_order_acquire) && intent.serviceGeneration == currentService) return true;
		}
		return false;
	}
	GestureState GetGestureState() noexcept { return bridge.GetGestureState(); }
}
