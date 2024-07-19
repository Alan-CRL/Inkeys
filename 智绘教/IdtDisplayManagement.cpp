/*
 * @file		IdtDisplayManagement.cpp
 * @brief		IDT Display management | 显示器管理
 * @note		Obtain and manage display status | 获取并管理显示器状态
 *
 * @author		AlanCRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// INFO: This source file will take the lead in refactoring the code logic and optimizing the reading experience.
// 提示：这个源文件将率先重构代码逻辑，并优化阅读体验。

#include "IdtDisplayManagement.h"

#include <dbt.h>
#include <initguid.h>
#include <ntddvdeo.h>

int DisplaysNumber;
shared_mutex DisplaysNumberSm;
vector<tuple<HMONITOR, RECT, bool>> DisplaysInfo; // 显示器句柄、显示器范围、是否是主显示器
shared_mutex DisplaysInfoSm;
MainMonitorStruct MainMonitor;
shared_mutex MainMonitorSm;

int MonitorEnumProcCount;
int MonitorEnumProcCountTarget;
map<HMONITOR, MONITORINFO> DisplaysInfoTemp;

bool enableAppBarAutoHide;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	if (MonitorEnumProcCount == 0) MonitorEnumProcCountTarget = GetSystemMetrics(SM_CMONITORS);
	MonitorEnumProcCount++;

	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(MONITORINFO);
	GetMonitorInfo(hMonitor, &monitorInfo);
	DisplaysInfoTemp[hMonitor] = monitorInfo;

	if (MonitorEnumProcCount == MonitorEnumProcCountTarget)
	{
		unique_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
		DisplaysInfo.clear();
		DisplaysInfoLock.unlock();

		for (const auto& [Monitor, MonitorInfo] : DisplaysInfoTemp)
		{
			unique_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
			DisplaysInfo.push_back(make_tuple(Monitor, MonitorInfo.rcMonitor, (monitorInfo.dwFlags & MONITORINFOF_PRIMARY)));
			DisplaysInfoLock.unlock();

			if (MonitorInfo.dwFlags & MONITORINFOF_PRIMARY)
			{
				unique_lock<shared_mutex> MainMonitorLock(MainMonitorSm);

				MainMonitor.Monitor = Monitor;
				MainMonitor.rcMonitor = MonitorInfo.rcMonitor;

				MainMonitor.MonitorWidth = MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left;
				MainMonitor.MonitorHeight = MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top;

				MainMonitorLock.unlock();
			}
		}

		unique_lock<shared_mutex> DisplaysNumberLock(DisplaysNumberSm);
		DisplaysNumber = DisplaysInfo.size();
		DisplaysNumberLock.unlock();

		DisplaysInfoTemp.clear();
		MonitorEnumProcCount = 0;
	}

	return TRUE;
}
LRESULT CALLBACK IdtDisplayManagementWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_DISPLAYCHANGE || uMsg == WM_DEVICECHANGE) EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, NULL);

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void DisplayManagementPolling()
{
	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = IdtDisplayManagementWindowProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = L"IdtDisplayManagementClass";
	RegisterClass(&wc);
	HWND hwnd = CreateWindowEx(0, L"IdtDisplayManagementClass", NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

	MSG msg;
	while (!offSignal && GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return;
}
void DisplayManagementMain()
{
	if (EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, NULL))
	{
		thread DisplayManagementPollingThread(DisplayManagementPolling);
		DisplayManagementPollingThread.detach();
	}
	else
	{
		unique_lock<shared_mutex> DisplaysNumberLock(DisplaysNumberSm);
		DisplaysNumber = 1;
		DisplaysNumberLock.unlock();
	}
}