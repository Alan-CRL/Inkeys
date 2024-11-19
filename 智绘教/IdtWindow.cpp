#include "IdtWindow.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
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
		MessageBox(NULL, L"Program unexpected exit: The program window creation failed or was intercepted. Please restart the software and try again.(#5)\n程序意外退出：程序窗口创建失败或被拦截，请重启软件重试。(#5)", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);

		offSignal = true;
		return;
	}
	IDTLogger->info("[窗口置顶线程][TopWindow] 等待窗口初次绘制完成");

	IDTLogger->info("[窗口置顶线程][TopWindow] 显示窗口");
	ShowWindow(floating_window, SW_SHOW);
	ShowWindow(ppt_window, SW_SHOW);
	ShowWindow(drawpad_window, SW_SHOW);
	ShowWindow(freeze_window, SW_SHOW);
	IDTLogger->info("[窗口置顶线程][TopWindow] 显示窗口完成");

	// 置顶前缓冲
	while (rtsWait) this_thread::sleep_for(chrono::milliseconds(500));
	this_thread::sleep_for(chrono::milliseconds(1000));

	while (!offSignal)
	{
		// 检查窗口显示状态
		{
			for (int i = 1; i <= 10 && !IsWindowVisible(floating_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 悬浮窗窗口被隐藏 Try" + to_string(i));
				ShowWindow(floating_window, SW_SHOW);

				if (IsWindowVisible(floating_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !IsWindowVisible(ppt_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] PPT控件窗口被隐藏 Try" + to_string(i));
				ShowWindow(ppt_window, SW_SHOW);

				if (IsWindowVisible(ppt_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !IsWindowVisible(drawpad_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 画板窗口被隐藏 Try" + to_string(i));
				ShowWindow(drawpad_window, SW_SHOW);

				if (IsWindowVisible(drawpad_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
			for (int i = 1; i <= 10 && !IsWindowVisible(freeze_window); i++)
			{
				IDTLogger->warn("[窗口置顶线程][TopWindow] 定格窗口被隐藏 Try" + to_string(i));
				ShowWindow(freeze_window, SW_SHOW);

				if (IsWindowVisible(freeze_window)) break;
				this_thread::sleep_for(chrono::milliseconds(10));
			}
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
		}

		// 置顶窗口
		if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && !penetrate.select)
		{
			std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
			bool flag = StrokeImageList.empty();
			lock1.unlock();
			if (flag)
			{
				if (!SetWindowPos(freeze_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
					IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶窗口时失败 Error" + to_string(GetLastError()));
			}
		}
		else
		{
			if (!SetWindowPos(freeze_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
				IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶窗口时失败 Error" + to_string(GetLastError()));
		}

		// 检查置顶情况
		if (setlist.forceTop && (!(GetWindowLong(freeze_window, GWL_EXSTYLE) & WS_EX_TOPMOST)))
		{
			IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶窗口失败");
			IDTLogger->info("[窗口置顶线程][TopWindow] 强制置顶窗口");

			HWND hForeWnd = GetForegroundWindow();
			DWORD dwForeID = GetCurrentThreadId();
			DWORD dwCurID = GetWindowThreadProcessId(hForeWnd, NULL);
			AttachThreadInput(dwCurID, dwForeID, TRUE);
			SetWindowPos(freeze_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
			SetWindowPos(freeze_window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
			AttachThreadInput(dwCurID, dwForeID, FALSE);

			SetWindowPos(freeze_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

			IDTLogger->info("[窗口置顶线程][TopWindow] 强制置顶窗口完成");
		}

		// 延迟等待
		for (int i = 1; i <= 30; i++)
		{
			if (offSignal) break;
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