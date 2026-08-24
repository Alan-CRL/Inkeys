import Inkeys.Helper.CrashHandler;
import Inkeys.Window;

#include "Window.Legacy.hpp"

#include "../../IdtConfiguration.h"
#include "../../IdtMain.h"
#include "../../IdtOther.h"
#include "../../Launch/IdtLaunchState.h"

HWND floating_window = nullptr;
HWND drawpad_window = nullptr;
HWND freeze_window = nullptr;
HWND setting_window = nullptr;

IdtWindowsIsVisibleStruct IdtWindowsIsVisible;
bool rtsWait = true;
bool topWindowNow = false;

HWND GetLastFocusWindow()
{
	return Inkeys::Window::Service::LastFocusWindow();
}

std::wstring GetWindowTitle(HWND hwnd)
{
	const int length = GetWindowTextLengthW(hwnd);
	if (length <= 0) return {};
	std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
	const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
	title.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
	return title;
}

void TopWindow()
{
	auto& service = Inkeys::Window::GetService();
	if (IDTLogger) IDTLogger->info("[窗口线程][TopWindow] 等待覆盖层首帧");
	for (int index = 0; index < 40 && !offSignal; ++index)
	{
		if (IdtWindowsIsVisible.floatingWindow
			&& IdtWindowsIsVisible.pptWindow
			&& IdtWindowsIsVisible.freezeWindow)
		{
			IdtWindowsIsVisible.allCompleted = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	if (!IdtWindowsIsVisible.allCompleted)
	{
		if (IDTLogger) IDTLogger->warn("[窗口线程][TopWindow] 等待覆盖层首帧超时");
		if (LaunchState::warnTry)
			MessageBoxW(nullptr,
				L"Program unexpected exit: The program window creation failed or was intercepted. Please restart the software and try again.(#5)\n程序意外退出：程序窗口创建失败或被拦截，请重启软件重试。(#5)",
				L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);
		else
			ShellExecuteW(nullptr, nullptr, GetCurrentExePath().c_str(), L"-WarnTry", nullptr, SW_SHOWNORMAL);
		SetOffSignal(1);
		return;
	}

	(void)service.Show(Inkeys::Window::WindowRole::Freeze);
	// Drawpad 已由 Draw3 首帧握手确认，显隐只能交给选择/内容状态机。
	// PPT 五窗按各自配置决定显隐，TopWindow 只负责唤醒其首次提交。
	(void)service.Show(Inkeys::Window::WindowRole::PptBottomLeft);
	(void)service.Show(Inkeys::Window::WindowRole::PptBottomRight);
	(void)service.Show(Inkeys::Window::WindowRole::PptMiddleLeft);
	(void)service.Show(Inkeys::Window::WindowRole::PptMiddleRight);
	(void)service.Show(Inkeys::Window::WindowRole::Bar);
	while (rtsWait && !offSignal)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	if (!offSignal) CrashHandler::IsSecond(false);

	while (!offSignal)
	{
		(void)service.RequestTopmostRefresh();
		int ticks = 300;
		switch (setlist.topSleepTime)
		{
		case 0: ticks = 10; break;
		case 1: ticks = 50; break;
		case 2: ticks = 100; break;
		case 4: ticks = 500; break;
		case 5: ticks = 1000; break;
		case 6: ticks = 3000; break;
		default: break;
		}
		for (int index = 0; index < ticks && !offSignal; ++index)
		{
			if (topWindowNow)
			{
				topWindowNow = false;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}
