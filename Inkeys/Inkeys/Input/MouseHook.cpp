module;

#include "../../IdtMain.h"
#include "MouseHook.Transport.h"

#include <atomic>
#include <future>
#include <mutex>
#include <thread>

module Inkeys.Input.MouseHook;
import Inkeys.Other.Inputs;
import Inkeys.UI.PageControl;

namespace
{
	namespace Transport = Inkeys::Input::HookTransport;
	std::mutex operationMutex;
	std::mutex wakeMutex;
	std::jthread hookThread;
	HANDLE wakeEvent = nullptr;
	HHOOK hookHandle = nullptr;
	std::atomic_bool running = false;
	std::atomic_uint64_t lifecycleGeneration = 0;
	std::atomic<DWORD> routeHeartbeat = 0;
	Transport::Bridge bridge;
	Transport::Snapshot ownerRoute;

	void WakeHookThread() noexcept
	{
		std::scoped_lock lock(wakeMutex);
		if (wakeEvent) (void)SetEvent(wakeEvent);
	}

	LRESULT CALLBACK HookProc(int code, WPARAM message, LPARAM lParam)
	{
		const auto lifecycle = lifecycleGeneration.load(std::memory_order_acquire);
		if (Transport::BypassHook(code, running.load(std::memory_order_acquire), message == WM_MOUSEMOVE))
			return CallNextHookEx(hookHandle, code, message, lParam);

		unsigned button = 0;
		bool down = false;
		switch (message)
		{
		case WM_LBUTTONDOWN: button = VK_LBUTTON; down = true; break;
		case WM_MBUTTONDOWN: button = VK_MBUTTON; down = true; break;
		case WM_RBUTTONDOWN: button = VK_RBUTTON; down = true; break;
		case WM_LBUTTONUP: button = VK_LBUTTON; break;
		case WM_MBUTTONUP: button = VK_MBUTTON; break;
		case WM_RBUTTONUP: button = VK_RBUTTON; break;
		}
		if (button) Inkeys::Inputs::SetKeyBoardDown(static_cast<BYTE>(button), down);
		if (!lParam || (message != WM_MOUSEWHEEL && !down))
			return CallNextHookEx(hookHandle, code, message, lParam);

		const auto& mouse = *reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
		Transport::Event event;
		event.kind = message == WM_MOUSEWHEEL ? Transport::Kind::Wheel : Transport::Kind::OutsideCandidate;
		event.screen = { mouse.pt.x, mouse.pt.y };
		event.time = mouse.time;
		event.button = button;
		event.delta = message == WM_MOUSEWHEEL ? static_cast<short>(HIWORD(mouse.mouseData)) : 0;
		event.flags = mouse.flags;
		event.extraInfo = mouse.dwExtraInfo;
		const auto admission = bridge.TryCommit(event, GetTickCount(),
			routeHeartbeat.load(std::memory_order_acquire), lifecycle);
		if (admission == Transport::Admission::Suppress) return 1;
		return CallNextHookEx(hookHandle, code, message, lParam);
	}

	void Run(std::stop_token stopToken, std::promise<bool> startup)
	{
		MSG message{};
		(void)PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
		HANDLE localWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!localWakeEvent) { startup.set_value(false); return; }
		{
			std::scoped_lock lock(wakeMutex);
			wakeEvent = localWakeEvent;
		}
		std::stop_callback stopCallback(stopToken, [] { WakeHookThread(); });
		hookHandle = SetWindowsHookExW(WH_MOUSE_LL, HookProc, nullptr, 0);
		if (!hookHandle)
		{
			std::scoped_lock lock(wakeMutex);
			wakeEvent = nullptr;
			CloseHandle(localWakeEvent);
			startup.set_value(false);
			return;
		}
		running.store(true, std::memory_order_release);
		startup.set_value(true);
		bool quit = false;
		while (!stopToken.stop_requested() && !offSignal && !quit)
		{
			const DWORD result = MsgWaitForMultipleObjectsEx(
				1, &localWakeEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			if (result == WAIT_OBJECT_0) continue;
			if (result != WAIT_OBJECT_0 + 1) break;
			while (!stopToken.stop_requested() && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_QUIT) { quit = true; break; }
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		running.store(false, std::memory_order_release);
		UnhookWindowsHookEx(hookHandle); // 由安装线程卸载，不在业务线程持锁调用。
		hookHandle = nullptr;
		{
			std::scoped_lock lock(wakeMutex);
			wakeEvent = nullptr;
		}
		CloseHandle(localWakeEvent);
	}

	void StopLocked() noexcept
	{
		running.store(false, std::memory_order_release);
		lifecycleGeneration.fetch_add(1, std::memory_order_acq_rel);
		if (!hookThread.joinable()) return;
		hookThread.request_stop();
		WakeHookThread();
		hookThread.join();
	}
}

namespace Inkeys::Input::MouseHook
{
	bool Start()
	{
		std::scoped_lock lock(operationMutex);
		StopLocked();
		std::promise<bool> startup;
		auto result = startup.get_future();
		try
		{
			hookThread = std::jthread(Run, std::move(startup));
			if (result.get()) return true;
		}
		catch (...) { /* 线程或启动通信失败同安装失败一样回退，不影响原生输入。 */ }
		StopLocked();
		return false;
	}
	void Stop() noexcept
	{
		std::scoped_lock lock(operationMutex);
		StopLocked();
	}
	void PublishRoute(const Route& requested) noexcept
	{
		auto next = requested;
		const auto token = Business::CapturePptInputSession();
		next.wheelEnabled = next.wheelEnabled && token.active
			&& UI::PageControl::CanAcceptPptWheel(token);
		if (ownerRoute.route.wheelEnabled != next.wheelEnabled
			|| ownerRoute.route.outsideClickEnabled != next.outsideClickEnabled
			|| ownerRoute.route.contextRevision != next.contextRevision
			|| (!Business::MatchesPptSession(ownerRoute.session, token)
				&& (ownerRoute.session.active || token.active)))
		{
			ownerRoute.route = next;
			ownerRoute.session = token;
			++ownerRoute.generation;
			bridge.Publish(ownerRoute);
			UI::PageControl::ResetWheelInput();
		}
		routeHeartbeat.store(GetTickCount(), std::memory_order_release);
	}
	bool TryPop(Event& event) noexcept
	{
		for (unsigned retired = 0; retired < 16 && bridge.TryPop(event); ++retired)
		{
			if (event.lifecycleGeneration == lifecycleGeneration.load(std::memory_order_acquire)
				&& event.routeGeneration == ownerRoute.generation) return true;
		}
		return false;
	}
	void DispatchWheel(const Event& event) noexcept
	{
		if (event.kind != Kind::Wheel || event.routeGeneration != ownerRoute.generation
			|| event.lifecycleGeneration != lifecycleGeneration.load(std::memory_order_acquire)
			|| !ownerRoute.route.wheelEnabled) return;
		UI::PageControl::ConsumePptWheel(event.session, event.routeGeneration, event.delta);
	}
	void PollPptWheel() noexcept { UI::PageControl::DrainLocalWheel(); }
}
