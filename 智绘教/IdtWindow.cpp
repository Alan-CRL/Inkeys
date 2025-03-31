#include "IdtWindow.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtRts.h"
#include "IdtState.h"
#include "IdtText.h"

HWND floating_window = NULL; //悬浮窗窗口
HWND drawpad_window = NULL; //画板窗口
HWND ppt_window = NULL; //PPT控件窗口
HWND freeze_window = NULL; //定格背景窗口
HWND setting_window = NULL; //程序调测窗口

bool FreezePPT;
HWND ppt_show;
wstring ppt_title, ppt_software;
map<wstring, bool> ppt_title_recond;

HWND GetLastFocusWindow()
{
	GUITHREADINFO guiThreadInfo;
	guiThreadInfo.cbSize = sizeof(GUITHREADINFO);
	if (GetGUIThreadInfo(0, &guiThreadInfo))
	{
		return guiThreadInfo.hwndFocus;
	}
	return NULL;
}
wstring GetWindowText(HWND hWnd)
{
	// 获取窗口标题的长度
	int length = GetWindowTextLength(hWnd);

	// 创建一个足够大的缓冲区来存储窗口标题
	std::vector<wchar_t> buffer(length + 1);

	// 获取窗口标题
	GetWindowText(hWnd, &buffer[0], buffer.size());

	// 返回窗口标题
	return &buffer[0];
}

IdtWindowsIsVisibleStruct IdtWindowsIsVisible;
bool rtsWait = true;
bool topWindowNow;

//置顶程序窗口
void TopWindow()
{
	// 等待窗口绘制
	IDTLogger->info("[窗口置顶线程][TopWindow] 等待窗口初次绘制");
	for (int i = 1; i <= 20; i++)
	{
		this_thread::sleep_for(chrono::milliseconds(500));

		bool flag = true;
		if (flag && !IdtWindowsIsVisible.floatingWindow) flag = false;
		if (flag && !IdtWindowsIsVisible.pptWindow) flag = false;
		if (flag && !IdtWindowsIsVisible.drawpadWindow) flag = false;
		if (flag && !IdtWindowsIsVisible.freezeWindow) flag = false;

		if (flag)
		{
			IdtWindowsIsVisible.allCompleted = true;
			break;
		}
	}

	// 状态超时 -> 结束程序
	if (!IdtWindowsIsVisible.allCompleted)
	{
		IDTLogger->warn("[窗口置顶线程][TopWindow] 等待窗口初次绘制超时");

		if (filesystem::exists(globalPath + L"force_start_try.signal"))
		{
			error_code ec;
			filesystem::remove(globalPath + L"force_start_try.signal", ec);

			MessageBox(NULL, L"Program unexpected exit: The program window creation failed or was intercepted. Please restart the software and try again.(#5)\n程序意外退出：程序窗口创建失败或被拦截，请重启软件重试。(#5)", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);
		}
		else
		{
			HANDLE fileHandle = NULL;
			OccupyFileForWrite(&fileHandle, globalPath + L"force_start_try.signal");
			UnOccupyFile(&fileHandle);

			ShellExecuteW(NULL, NULL, GetCurrentExePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		exit(0);
	}
	IDTLogger->info("[窗口置顶线程][TopWindow] 等待窗口初次绘制完成");

	IDTLogger->info("[窗口置顶线程][TopWindow] 显示窗口");
	ShowWindow(floating_window, SW_SHOWNOACTIVATE);
	ShowWindow(ppt_window, SW_SHOWNOACTIVATE);
	ShowWindow(drawpad_window, SW_SHOWNOACTIVATE);
	ShowWindow(freeze_window, SW_SHOWNOACTIVATE);
	IDTLogger->info("[窗口置顶线程][TopWindow] 显示窗口完成");

	// 置顶前缓冲
	while (rtsWait) this_thread::sleep_for(chrono::milliseconds(500));
	this_thread::sleep_for(chrono::milliseconds(1000));

	while (!offSignal)
	{
		if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && !penetrate.select)
		{
			shared_lock LockStrokeImageListSm(StrokeImageListSm);
			bool flag = !StrokeImageList.empty();
			LockStrokeImageListSm.unlock();

			if (flag)
			{
				// 跳过当次置顶
				goto topWait;
			}
		}

		// 检查窗口显示状态
		{
			for (int i = 1; i <= 10 && !IsWindowVisible(floating_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 悬浮窗窗口被隐藏 Try" + to_string(i));
				ShowWindow(floating_window, SW_SHOWNOACTIVATE);

				if (IsWindowVisible(floating_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !IsWindowVisible(ppt_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] PPT控件窗口被隐藏 Try" + to_string(i));
				ShowWindow(ppt_window, SW_SHOWNOACTIVATE);

				if (IsWindowVisible(ppt_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !IsWindowVisible(drawpad_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 画板窗口被隐藏 Try" + to_string(i));
				ShowWindow(drawpad_window, SW_SHOWNOACTIVATE);

				if (IsWindowVisible(drawpad_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !IsWindowVisible(freeze_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 定格窗口被隐藏 Try" + to_string(i));
				ShowWindow(freeze_window, SW_SHOWNOACTIVATE);

				if (IsWindowVisible(freeze_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !IsWindowVisible(magnifierWindow); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] magnifierWindow 被隐藏 Try" + to_string(i));
				ShowWindow(magnifierWindow, SW_SHOWNOACTIVATE);

				if (IsWindowVisible(magnifierWindow)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			/*for (int i = 1; i <= 10 && !IsWindowVisible(setting_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 选项窗口被隐藏 Try" + to_string(i));
				ShowWindow(setting_window, SW_SHOWNOACTIVATE);

				if (IsWindowVisible(setting_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}*/
		}

		// 检查窗口扩展样式
		{
			for (int i = 1; i <= 10 && !(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 悬浮窗窗口 WS_EX_LAYERED 样式被隐藏 Try" + to_string(i));
				SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_LAYERED);

				if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !(GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 悬浮窗窗口 WS_EX_NOACTIVATE 样式被隐藏 Try" + to_string(i));
				SetWindowLong(floating_window, GWL_EXSTYLE, GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

				if (GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}

			for (int i = 1; i <= 10 && !(GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_LAYERED); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] PPT控件窗口 WS_EX_LAYERED 样式被隐藏 Try" + to_string(i));
				SetWindowLong(ppt_window, GWL_EXSTYLE, GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_LAYERED);

				if (GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !(GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] PPT控件窗口 WS_EX_NOACTIVATE 样式被隐藏 Try" + to_string(i));
				SetWindowLong(ppt_window, GWL_EXSTYLE, GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

				if (GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}

			for (int i = 1; i <= 10 && !(GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_LAYERED); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 画板窗口 WS_EX_LAYERED 样式被隐藏 Try" + to_string(i));
				SetWindowLong(drawpad_window, GWL_EXSTYLE, GetWindowLong(drawpad_window, GWL_EXSTYLE) | WS_EX_LAYERED);

				if (GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !(GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 画板窗口 WS_EX_NOACTIVATE 样式被隐藏 Try" + to_string(i));
				SetWindowLong(drawpad_window, GWL_EXSTYLE, GetWindowLong(drawpad_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

				if (GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}

			for (int i = 1; i <= 10 && !(GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_LAYERED); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 定格窗口 WS_EX_LAYERED 样式被隐藏 Try" + to_string(i));
				SetWindowLong(freeze_window, GWL_EXSTYLE, GetWindowLong(freeze_window, GWL_EXSTYLE) | WS_EX_LAYERED);

				if (GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !(GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 定格窗口 WS_EX_NOACTIVATE 样式被隐藏 Try" + to_string(i));
				SetWindowLong(freeze_window, GWL_EXSTYLE, GetWindowLong(freeze_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

				if (GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}

			for (int i = 1; i <= 10 && !(GetWindowLong(magnifierWindow, GWL_EXSTYLE) & WS_EX_LAYERED); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] magnifierWindow WS_EX_LAYERED 样式被隐藏 Try" + to_string(i));
				SetWindowLong(magnifierWindow, GWL_EXSTYLE, GetWindowLong(magnifierWindow, GWL_EXSTYLE) | WS_EX_LAYERED);

				if (GetWindowLong(magnifierWindow, GWL_EXSTYLE) & WS_EX_LAYERED) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !(GetWindowLong(magnifierWindow, GWL_EXSTYLE) & WS_EX_NOACTIVATE); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] magnifierWindow WS_EX_NOACTIVATE 样式被隐藏 Try" + to_string(i));
				SetWindowLong(magnifierWindow, GWL_EXSTYLE, GetWindowLong(magnifierWindow, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

				if (GetWindowLong(magnifierWindow, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !(GetWindowLong(magnifierChild, GWL_EXSTYLE) & WS_EX_NOACTIVATE); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] magnifierChild WS_EX_NOACTIVATE 样式被隐藏 Try" + to_string(i));
				SetWindowLong(magnifierChild, GWL_EXSTYLE, GetWindowLong(magnifierChild, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

				if (GetWindowLong(magnifierChild, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}

			for (int i = 1; i <= 10 && !(GetWindowLong(setting_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 选项窗口 WS_EX_NOACTIVATE 样式被隐藏 Try" + to_string(i));
				SetWindowLong(setting_window, GWL_EXSTYLE, GetWindowLong(setting_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

				if (GetWindowLong(setting_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
		}

		// 置顶窗口
		{
			// 设置窗口顺序（非必要：会导致窗口闪烁）
			//SetWindowPos(ppt_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			//SetWindowPos(setting_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			//SetWindowPos(drawpad_window, setting_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			//SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			//SetWindowPos(magnifierWindow, freeze_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

			// 统一置顶
			if (!SetWindowPos(magnifierWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
				IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶窗口时失败 Error" + to_string(GetLastError()));
		}

	topWait:

		// 延迟等待时间计算
		int sleepTime = 30;
		{
			if (setlist.topSleepTime == 0) sleepTime = 1;
			else if (setlist.topSleepTime == 1) sleepTime = 5;
			else if (setlist.topSleepTime == 2) sleepTime = 10;
			else if (setlist.topSleepTime == 4) sleepTime = 50;
			else if (setlist.topSleepTime == 5) sleepTime = 100;
			else if (setlist.topSleepTime == 6) sleepTime = 300;
			else sleepTime = 30;
		}

		// 延迟等待
		for (int i = 1; i <= sleepTime; i++)
		{
			if (offSignal) break;
			if (topWindowNow)
			{
				topWindowNow = false;
				break;
			}
			this_thread::sleep_for(chrono::milliseconds(100));
		}
	}

	IDTLogger->info("[窗口置顶线程][TopWindow] 隐藏窗口");
	ShowWindow(floating_window, SW_HIDE);
	ShowWindow(ppt_window, SW_HIDE);
	ShowWindow(drawpad_window, SW_HIDE);
	ShowWindow(freeze_window, SW_HIDE);
	IDTLogger->info("[窗口置顶线程][TopWindow] 隐藏窗口完成");

	return;
}