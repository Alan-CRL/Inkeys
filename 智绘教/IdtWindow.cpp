#include "IdtWindow.h"

#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtText.h"

HWND floating_window = NULL; //悬浮窗窗口
HWND drawpad_window = NULL; //画板窗口
HWND ppt_window = NULL; //PPT控件窗口
HWND setting_window = NULL; //程序调测窗口
HWND freeze_window = NULL; //定格背景窗口

bool FreezePPT;
HWND ppt_show;
wstring ppt_title, ppt_software;
map<wstring, bool> ppt_title_recond;

//窗口是否置顶
bool IsWindowFocused(HWND hWnd)
{
	return GetForegroundWindow() == hWnd;
}

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

//置顶程序窗口

void TopWindow()
{
	/*
	//窗口强制置顶
	{
		HWND hForeWnd = NULL;
		DWORD dwForeID = 0;
		DWORD dwCurID = 0;

		hForeWnd = ::GetForegroundWindow();
		dwCurID = ::GetCurrentThreadId();
		dwForeID = ::GetWindowThreadProcessId(hForeWnd, NULL);
		::AttachThreadInput(dwCurID, dwForeID, TRUE);
		//::ShowWindow(hWnd, SW_SHOWNORMAL);
		::SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		::SetWindowPos(floating_window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		::SetForegroundWindow(floating_window);
		// 将前台窗口线程贴附到当前线程（也就是程序A中的调用线程）
		::AttachThreadInput(dwCurID, dwForeID, FALSE);
	}
	SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	*/

	Sleep(3000);
	while (1)
	{
		try
		{
			SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			SetWindowPos(ppt_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

			if (!choose.select)
			{
				std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
				bool flag = StrokeImageList.empty();
				lock1.unlock();
				if (flag)
				{
					SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
					SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				}
			}
			else
			{
				SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
		}
		catch (...)
		{
		};

		Sleep(1000);
	}
}