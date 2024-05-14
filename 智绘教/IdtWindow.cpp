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
		HWND hForeWnd = GetForegroundWindow();
		DWORD dwForeID = GetCurrentThreadId();
		DWORD dwCurID = GetWindowThreadProcessId(hForeWnd, NULL);
		AttachThreadInput(dwCurID, dwForeID, TRUE);
		SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetWindowPos(floating_window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetForegroundWindow(floating_window);
		AttachThreadInput(dwCurID, dwForeID, FALSE);

		SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	*/

	while (1)
	{
		if (!SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶悬浮窗窗口时失败 Error" + to_string(GetLastError()));
		if (!SetWindowPos(ppt_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶PPT批注控件窗口时失败 Error" + to_string(GetLastError()));

		if (!choose.select)
		{
			std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
			bool flag = StrokeImageList.empty();
			lock1.unlock();
			if (flag)
			{
				if (!SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶画板窗口时失败 Error" + to_string(GetLastError()));
				if (!SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶定格背景窗口时失败 Error" + to_string(GetLastError()));
			}
		}
		else
		{
			if (!SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶画板窗口时失败 Error" + to_string(GetLastError()));
			if (!SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) IDTLogger->warn("[窗口置顶线程][TopWindow] 置顶定格背景窗窗口时失败 Error" + to_string(GetLastError()));
		}

		Sleep(1000);
	}
}