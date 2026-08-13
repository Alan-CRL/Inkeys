module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtPlug-in.h"
#include "../../../IdtRts.h"
#include "../../../IdtState.h"
#include "../Window/Window.Legacy.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <functional>
#include <mutex>
#include <thread>

module Inkeys.Input.MouseHook;
import Inkeys.Other.Inputs;
import Inkeys.UI.Ppt;

namespace
{
	using Clock = std::chrono::steady_clock;

	std::mutex lifecycleMutex;
	std::jthread hookThread;
	HANDLE wakeEvent = nullptr;
	HHOOK hookHandle = nullptr;
	Inkeys::Input::MouseHook::CollapseCallback collapseCallback;
	std::atomic_bool running = false;
	std::atomic_bool clickCollapsePending = false;
	std::atomic_bool mouseUpPending = false;
	Clock::time_point clickCollapseDeadline;
	Clock::time_point mouseUpDeadline;
	UINT mouseUpMessage = 0;

	void WakeHookThread() noexcept
	{
		std::scoped_lock lock(lifecycleMutex);
		if (wakeEvent)
			(void)SetEvent(wakeEvent);
	}

	void ScheduleClickCollapse() noexcept
	{
		clickCollapseDeadline = Clock::now() + std::chrono::milliseconds(100);
		clickCollapsePending.store(true, std::memory_order_release);
		WakeHookThread();
	}

	void ScheduleMouseUp(UINT message) noexcept
	{
		mouseUpMessage = message;
		mouseUpDeadline = Clock::now() + std::chrono::milliseconds(500);
		mouseUpPending.store(true, std::memory_order_release);
		WakeHookThread();
	}

	void ProcessPendingActions()
	{
		const auto now = Clock::now();
		if (clickCollapsePending.load(std::memory_order_acquire)
			&& clickCollapseDeadline <= now
			&& clickCollapsePending.exchange(false, std::memory_order_acq_rel))
		{
			Inkeys::Input::MouseHook::CollapseCallback callback;
			{
				std::scoped_lock lock(lifecycleMutex);
				callback = collapseCallback;
			}
			if (callback)
				callback();
		}

		if (mouseUpPending.load(std::memory_order_acquire)
			&& mouseUpDeadline <= now
			&& mouseUpPending.exchange(false, std::memory_order_acq_rel))
		{
			if (!offSignal)
			{
				if (IDTLogger)
					IDTLogger->info("[鼠标钩子][MouseUp] 修正遗漏的抬起消息");
				HandleMouseInput(drawpad_window, mouseUpMessage, 0, 0);
			}
		}
	}

	[[nodiscard]] DWORD NextWaitTimeout() noexcept
	{
		const auto noDeadline = Clock::time_point((Clock::duration::max)());
		auto deadline = noDeadline;
		if (clickCollapsePending.load(std::memory_order_acquire))
			deadline = clickCollapseDeadline;
		if (mouseUpPending.load(std::memory_order_acquire))
			deadline = (std::min)(deadline, mouseUpDeadline);
		if (deadline == noDeadline)
			return INFINITE;
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - Clock::now());
		return remaining.count() <= 0
			? 0
			: static_cast<DWORD>((std::min)(remaining.count(),
				static_cast<long long>(MAXDWORD - 1)));
	}

	LRESULT CALLBACK HookProc(int code, WPARAM message, LPARAM lParam)
	{
		if (code < 0 || !running.load(std::memory_order_acquire)
			|| message == WM_MOUSEMOVE)
			return CallNextHookEx(hookHandle, code, message, lParam);

		if (message == WM_LBUTTONDOWN)
			Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, true);
		else if (message == WM_LBUTTONUP)
		{
			Inkeys::Inputs::SetKeyBoardDown(VK_LBUTTON, false);
			if (useMouseInput && leftButtonPid != 0)
				ScheduleMouseUp(WM_LBUTTONUP);
		}
		else if (message == WM_MBUTTONDOWN)
			Inkeys::Inputs::SetKeyBoardDown(VK_MBUTTON, true);
		else if (message == WM_MBUTTONUP)
			Inkeys::Inputs::SetKeyBoardDown(VK_MBUTTON, false);
		else if (message == WM_RBUTTONDOWN)
			Inkeys::Inputs::SetKeyBoardDown(VK_RBUTTON, true);
		else if (message == WM_RBUTTONUP)
		{
			Inkeys::Inputs::SetKeyBoardDown(VK_RBUTTON, false);
			if (useMouseInput && rightButtonPid != 0)
				ScheduleMouseUp(WM_RBUTTONUP);
		}

		if (message == WM_MOUSEWHEEL
			&& stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection
			&& !penetrate.select && PptInfoState.TotalPage != -1)
		{
			const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
			const short wheel = GET_WHEEL_DELTA_WPARAM(mouse->mouseData);
			// Hook 只写入 UI3 交互队列，具体命令由共享渲染线程串行处理。
			Inkeys::UI::Ppt::QueueGlobalWheel(wheel);
			return 1;
		}

		if ((message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN
			|| message == WM_RBUTTONDOWN)
			&& setlist.regularSetting.clickRecover
			&& (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection
				|| penetrate.select))
		{
			ScheduleClickCollapse();
		}

		return CallNextHookEx(hookHandle, code, message, lParam);
	}

	void Run(std::stop_token stopToken, std::promise<bool> startup)
	{
		MSG message{};
		(void)PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
		HANDLE localWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!localWakeEvent)
		{
			startup.set_value(false);
			return;
		}
		{
			std::scoped_lock lock(lifecycleMutex);
			wakeEvent = localWakeEvent;
		}

		std::stop_callback stopCallback(stopToken, [] { WakeHookThread(); });
		hookHandle = SetWindowsHookExW(WH_MOUSE_LL, HookProc, nullptr, 0);
		if (!hookHandle)
		{
			{
				std::scoped_lock lock(lifecycleMutex);
				wakeEvent = nullptr;
			}
			CloseHandle(localWakeEvent);
			startup.set_value(false);
			return;
		}

		running.store(true, std::memory_order_release);
		startup.set_value(true);
		while (!stopToken.stop_requested() && !offSignal)
		{
			const DWORD waitResult = MsgWaitForMultipleObjectsEx(
				1, &localWakeEvent, NextWaitTimeout(), QS_ALLINPUT,
				MWMO_INPUTAVAILABLE);
			if (waitResult == WAIT_TIMEOUT || waitResult == WAIT_OBJECT_0)
			{
				ProcessPendingActions();
				continue;
			}
			if (waitResult != WAIT_OBJECT_0 + 1)
				break;
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_QUIT)
					break;
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			ProcessPendingActions();
		}

		running.store(false, std::memory_order_release);
		clickCollapsePending.store(false, std::memory_order_release);
		mouseUpPending.store(false, std::memory_order_release);
		UnhookWindowsHookEx(hookHandle);
		hookHandle = nullptr;
		{
			std::scoped_lock lock(lifecycleMutex);
			wakeEvent = nullptr;
		}
		CloseHandle(localWakeEvent);
	}
}

namespace Inkeys::Input::MouseHook
{
	bool Start(CollapseCallback callback)
	{
		Stop();
		std::promise<bool> startup;
		auto result = startup.get_future();
		{
			std::scoped_lock lock(lifecycleMutex);
			collapseCallback = std::move(callback);
		}
		hookThread = std::jthread(Run, std::move(startup));
		if (result.get())
			return true;
		Stop();
		return false;
	}

	void Stop() noexcept
	{
		if (hookThread.joinable())
		{
			hookThread.request_stop();
			WakeHookThread();
			hookThread.join();
		}
		std::scoped_lock lock(lifecycleMutex);
		collapseCallback = {};
	}

	void CancelPendingCollapse() noexcept
	{
		clickCollapsePending.store(false, std::memory_order_release);
	}

	void CancelPendingMouseUp() noexcept
	{
		mouseUpPending.store(false, std::memory_order_release);
	}
}
