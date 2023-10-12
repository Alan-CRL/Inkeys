#include "IdtWindow.h"

HWND floating_window = NULL; //悬浮窗窗口
HWND drawpad_window = NULL; //画板窗口
HWND ppt_window = NULL; //PPT控件窗口
HWND test_window = NULL; //程序调测窗口
HWND freeze_window = NULL; //定格背景窗口

bool FreezePPT;
HWND ppt_show;

//窗口强制置顶
BOOL OnForceShow(HWND hWnd)
{
	HWND hForeWnd = NULL;
	DWORD dwForeID = 0;
	DWORD dwCurID = 0;

	hForeWnd = ::GetForegroundWindow();
	dwCurID = ::GetCurrentThreadId();
	dwForeID = ::GetWindowThreadProcessId(hForeWnd, NULL);
	::AttachThreadInput(dwCurID, dwForeID, TRUE);
	//::ShowWindow(hWnd, SW_SHOWNORMAL);
	::SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	::SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	::SetForegroundWindow(hWnd);
	// 将前台窗口线程贴附到当前线程（也就是程序A中的调用线程）
	::AttachThreadInput(dwCurID, dwForeID, FALSE);

	return TRUE;
}
//窗口是否置顶
bool IsWindowFocused(HWND hWnd)
{
	return GetForegroundWindow() == hWnd;
}
//程序进程状态获取
bool isProcessRunning(const std::wstring& processPath)
{
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(snapshot, &entry)) {
		do {
			// 打开进程句柄
			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
			if (process == NULL) {
				continue;
			}

			// 获取进程完整路径
			wchar_t path[MAX_PATH];
			DWORD size = MAX_PATH;
			if (QueryFullProcessImageName(process, 0, path, &size)) {
				if (processPath == path) {
					CloseHandle(process);
					CloseHandle(snapshot);
					return true;
				}
			}

			CloseHandle(process);
		} while (Process32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return false;
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