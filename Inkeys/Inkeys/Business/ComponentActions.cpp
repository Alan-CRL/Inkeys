module;

#include <windows.h>
#include <exdisp.h>
#include <shldisp.h>

#include <atomic>
#include <mutex>
#include <thread>

module Inkeys.Business.ComponentActions;
import Inkeys.Helper.SecRandom;

namespace
{
	std::mutex quickDrawMutex;
	std::jthread quickDrawThread;
	std::atomic_bool quickDrawRunning = false;
	bool quickDrawStopping = false;

	void OpenComponentUri(const wchar_t* uri)
	{
		SHELLEXECUTEINFO info{ sizeof(info) };
		info.fMask = SEE_MASK_NOASYNC;
		info.lpVerb = L"open";
		info.lpFile = uri;
		info.nShow = SW_SHOWNORMAL;
		ShellExecuteExW(&info);
	}

	void OpenSecRandomQuickDrawAsync()
	{
		std::jthread retired;
		{
			std::scoped_lock lock(quickDrawMutex);
			if (quickDrawStopping || quickDrawRunning.load(std::memory_order_acquire)) return;
			retired = std::move(quickDrawThread);
			quickDrawRunning.store(true, std::memory_order_release);
			try
			{
				quickDrawThread = std::jthread([](std::stop_token token)
				{
					try { (void)Inkeys::SecRandom::OpenQuickDraw(nullptr, 5000, token); }
					catch (...) {} // 外部组件失败不能使业务线程终止进程。
					quickDrawRunning.store(false, std::memory_order_release);
				});
			}
			catch (...) { quickDrawRunning.store(false, std::memory_order_release); throw; }
		}
		// 已完成的旧线程也在状态锁外 join；停止入口可随时取得锁并请求取消新线程。
		if (retired.joinable()) retired.join();
	}

}

namespace Inkeys::Business
{
	void ExecuteBuiltInComponentAction(BuiltInComponentAction action)
	{
		switch (action)
		{
		case BuiltInComponentAction::Explorer:
			ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
			return;
		case BuiltInComponentAction::TaskManager:
			ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
			return;
		case BuiltInComponentAction::ControlPanel:
			ShellExecuteW(nullptr, L"open", L"control.exe", nullptr, nullptr, SW_SHOWNORMAL);
			return;
		case BuiltInComponentAction::ShowDesktop:
		{
			// Bar 交互线程不保证已初始化 COM，调用 Shell 前局部建立 STA。
			const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			IShellDispatch4* shell = nullptr;
			if (SUCCEEDED(CoCreateInstance(
				CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IShellDispatch4,
				reinterpret_cast<void**>(&shell))) && shell)
			{
				shell->ToggleDesktop();
				shell->Release();
			}
			if (SUCCEEDED(initialized))
				CoUninitialize();
			return;
		}
		case BuiltInComponentAction::LockWorkStation:
			LockWorkStation();
			return;
		case BuiltInComponentAction::Escape:
			keybd_event(VK_ESCAPE, 0, 0, 0);
			keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
			return;
		case BuiltInComponentAction::AltF4:
			keybd_event(VK_MENU, 0, 0, 0);
			keybd_event(VK_F4, 0, 0, 0);
			keybd_event(VK_F4, 0, KEYEVENTF_KEYUP, 0);
			keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
			return;
		case BuiltInComponentAction::IslandCaller:
			OpenComponentUri(L"classisland://plugins/IslandCaller/Run");
			return;
		case BuiltInComponentAction::IslandCallerSimple:
			OpenComponentUri(L"classisland://plugins/IslandCaller/Simple/1");
			return;
		case BuiltInComponentAction::SecRandomDirect:
			OpenComponentUri(L"secrandom://direct_extraction");
			return;
		case BuiltInComponentAction::SecRandomQuickDraw:
			OpenSecRandomQuickDrawAsync();
			return;
		case BuiltInComponentAction::SecRandomQuickDrawCompat:
			OpenComponentUri(L"secrandom://roll_call/quick_draw");
			return;
		case BuiltInComponentAction::NamePicker:
			OpenComponentUri(L"namepicker://");
			return;
		case BuiltInComponentAction::ClassIslandSettings:
			OpenComponentUri(L"classisland://app/settings/");
			return;
		case BuiltInComponentAction::ClassIslandProfile:
			OpenComponentUri(L"classisland://app/profile/");
			return;
		case BuiltInComponentAction::ClassIslandClassSwap:
			OpenComponentUri(L"classisland://app/class-swap");
			return;
		}
	}

	void ShutdownComponentActions()
	{
		std::jthread stopping;
		{
			std::scoped_lock lock(quickDrawMutex);
			quickDrawStopping = true;
			stopping = std::move(quickDrawThread);
		}
		// 管道等待观察同一个 token；不在 mutex 内等待线程或取消回调。
		stopping.request_stop();
		if (stopping.joinable()) stopping.join();
		quickDrawRunning.store(false, std::memory_order_release);
	}
}
