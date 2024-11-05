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

vector<MonitorInfoStruct> DisplaysInfo;
shared_mutex DisplaysInfoSm;

MonitorInfoStruct MainMonitor;

int MonitorEnumProcCount;
int MonitorEnumProcCountTarget;
map<HMONITOR, MONITORINFOEX> DisplaysInfoTemp;

bool enableAppBarAutoHide;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	if (MonitorEnumProcCount == 0) MonitorEnumProcCountTarget = GetSystemMetrics(SM_CMONITORS);
	MonitorEnumProcCount++;

	MONITORINFOEX monitorInfo{};
	monitorInfo.cbSize = sizeof(MONITORINFOEX);
	GetMonitorInfo(hMonitor, &monitorInfo);
	DisplaysInfoTemp[hMonitor] = monitorInfo;

	if (MonitorEnumProcCount == MonitorEnumProcCountTarget)
	{
		unique_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
		DisplaysInfo.clear();
		DisplaysInfoLock.unlock();

		for (const auto& [Monitor, MonitorInfo] : DisplaysInfoTemp)
		{
			bool edidValid = false;
			wstring edidVersion;
			int phyWidth = 0, phyHeight = 0;
			int displayOrientation = 0;
			wstring strModel, strDriver;

			DISPLAY_DEVICE ddMonitorTmp;
			ddMonitorTmp.cb = sizeof(DISPLAY_DEVICE);
			DWORD devIndex = 0;
			wstring deviceId;
			while (EnumDisplayDevices(MonitorInfo.szDevice, devIndex, &ddMonitorTmp, 0))
			{
				if ((ddMonitorTmp.StateFlags & DISPLAY_DEVICE_ACTIVE) == DISPLAY_DEVICE_ACTIVE &&
					(ddMonitorTmp.StateFlags & DISPLAY_DEVICE_ATTACHED) == DISPLAY_DEVICE_ATTACHED)
				{
					deviceId = ddMonitorTmp.DeviceID;
					break;
				}
			}

			if (!deviceId.empty() && IdtGetModelDriverFromDeviceID(deviceId.c_str(), strModel, strDriver))
			{
				// 获取 EDID 数据
				BYTE EDIDBuf[32]; // 我们只需要获取 18~22 的值，后面的可以截断
				DWORD dwRealGetBytes = 0;
				if (IdtGetDeviceEDID(strModel.c_str(), strDriver.c_str(), EDIDBuf, sizeof(EDIDBuf), &dwRealGetBytes) && dwRealGetBytes >= 23)
				{
					edidValid = true;
					edidVersion = to_wstring(EDIDBuf[18]) + L"." + to_wstring(EDIDBuf[19]);
					phyWidth = EDIDBuf[21];
					phyHeight = EDIDBuf[22];
				}
			}

			DEVMODE devMode;
			ZeroMemory(&devMode, sizeof(devMode));
			devMode.dmSize = sizeof(devMode);
			if (EnumDisplaySettings(MonitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
				displayOrientation = devMode.dmDisplayOrientation;

			if (MonitorInfo.dwFlags & MONITORINFOF_PRIMARY)
			{
				MainMonitor.Monitor = Monitor;
				MainMonitor.rcMonitor = MonitorInfo.rcMonitor;
				MainMonitor.MonitorWidth = MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left;
				MainMonitor.MonitorHeight = MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top;
				MainMonitor.MonitorPhyWidth = phyWidth;
				MainMonitor.MonitorPhyHeight = phyHeight;
			}

			unique_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
			DisplaysInfo.push_back(MonitorInfoStruct(Monitor,
				MonitorInfo.rcMonitor,
				MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left,
				MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top,
				EDIDInfoStruct(edidValid, edidVersion, deviceId),
				phyWidth, phyHeight,
				displayOrientation,
				(monitorInfo.dwFlags & MONITORINFOF_PRIMARY)));
			DisplaysInfoLock.unlock();
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

bool IdtGetModelDriverFromDeviceID(LPCWSTR lpDeviceID, wstring& model, wstring& driver)
{
	if (lpDeviceID == NULL) return false;

	LPCWSTR lpBegin = wcschr(lpDeviceID, L'\\');
	if (lpBegin == NULL) return false;
	lpBegin++;

	LPCWSTR lpSlash = wcschr(lpBegin, L'\\');
	if (lpSlash == NULL) return false;

	wchar_t wcModelBuf[8] = { 0 };
	size_t szLen = lpSlash - lpBegin;
	if (szLen >= 8) szLen = 7;
	wcsncpy_s(wcModelBuf, lpBegin, szLen);

	model = wstring(wcModelBuf);
	driver = wstring(lpSlash + 1);

	return true;
}
bool IdtIsCorrectEDID(const BYTE* pEDIDBuf, DWORD dwcbBufSize, LPCWSTR lpModel)
{
	// 参数有效性
	if (pEDIDBuf == NULL || dwcbBufSize < 24 || lpModel == NULL) return false;

	// 判断EDID头
	if (pEDIDBuf[0] != 0x00
		|| pEDIDBuf[1] != 0xFF
		|| pEDIDBuf[2] != 0xFF
		|| pEDIDBuf[3] != 0xFF
		|| pEDIDBuf[4] != 0xFF
		|| pEDIDBuf[5] != 0xFF
		|| pEDIDBuf[6] != 0xFF
		|| pEDIDBuf[7] != 0x00
		)
		return false;

	// 厂商名称 2个字节 可表三个大写英文字母
	// 每个字母有5位 共15位不足一位 在第一个字母代码最高位补 0” 字母 A”至 Z”对应的代码为00001至11010
	// 例如 MAG”三个字母 M代码为01101 A代码为00001 G代码为00111 在M代码前补0为001101
	// 自左向右排列得2字节 001101 00001 00111 转化为十六进制数即为34 27
	DWORD dwPos = 8;
	wchar_t wcModelBuf[9] = { 0 };
	char byte1 = pEDIDBuf[dwPos];
	char byte2 = pEDIDBuf[dwPos + 1];
	wcModelBuf[0] = ((byte1 & 0x7C) >> 2) + 64;
	wcModelBuf[1] = ((byte1 & 3) << 3) + ((byte2 & 0xE0) >> 5) + 64;
	wcModelBuf[2] = (byte2 & 0x1F) + 64;
	swprintf_s(wcModelBuf + 3, sizeof(wcModelBuf) / sizeof(wchar_t) - 3, L"%X%X%X%X", (pEDIDBuf[dwPos + 3] & 0xf0) >> 4, pEDIDBuf[dwPos + 3] & 0xf, (pEDIDBuf[dwPos + 2] & 0xf0) >> 4, pEDIDBuf[dwPos + 2] & 0x0f);

	// 比较MODEL是否匹配
	return (_wcsicmp(wcModelBuf, lpModel) == 0) ? true : false;
}
bool IdtGetDeviceEDID(LPCWSTR lpModel, LPCWSTR lpDriver, BYTE* pDataBuf, DWORD dwcbBufSize, DWORD* pdwGetBytes)
{
	if (pdwGetBytes != NULL) *pdwGetBytes = 0;
	if (lpModel == NULL || lpDriver == NULL || pDataBuf == NULL || dwcbBufSize == 0) return false;

	// 打开设备注册表子键
	wchar_t wcSubKey[MAX_PATH] = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\";
	wcscat_s(wcSubKey, lpModel);
	HKEY hSubKey;
	if (::RegOpenKeyEx(HKEY_LOCAL_MACHINE, wcSubKey, 0, KEY_READ, &hSubKey) != ERROR_SUCCESS) return false;

	bool bGetEDIDSuccess = false;

	// 枚举该子键下的键
	DWORD dwIndex = 0;
	DWORD dwSubKeyLen = sizeof(wcSubKey) / sizeof(wchar_t);
	FILETIME ft;
	while (bGetEDIDSuccess == false && ::RegEnumKeyEx(hSubKey, dwIndex, wcSubKey, &dwSubKeyLen, NULL, NULL, NULL, &ft) == ERROR_SUCCESS)
	{
		// 打开枚举到的键
		HKEY hEnumKey;
		if (::RegOpenKeyEx(hSubKey, wcSubKey, 0, KEY_READ, &hEnumKey) == ERROR_SUCCESS)
		{
			// 打开的键下查询Driver键的值
			dwSubKeyLen = sizeof(wcSubKey) / sizeof(wchar_t);
			if (::RegQueryValueEx(hEnumKey, L"Driver", NULL, NULL, (LPBYTE)wcSubKey, &dwSubKeyLen) == ERROR_SUCCESS
				&& _wcsicmp(wcSubKey, lpDriver) == 0 // Driver匹配
				)
			{
				// 打开键Device Parameters
				HKEY hDevParaKey;
				if (::RegOpenKeyEx(hEnumKey, L"Device Parameters", 0, KEY_READ, &hDevParaKey) == ERROR_SUCCESS)
				{
					// 读取EDID
					vector<BYTE> EDIDBuf; // EDID 通常的块大小都为 128 字节，但是注册表信息中会自动包含拓展块。
					// EDID 1.4 通常大小是 256，而带有 HDR 的 EDID 1.4 则通常是 384，2.0 则更多。所以我们考虑动态开内存，并在存储前先获取大小
					DWORD dwEDIDSize = 0;
					::RegQueryValueEx(hDevParaKey, L"EDID", NULL, NULL, NULL, &dwEDIDSize);
					EDIDBuf.resize(dwEDIDSize);

					if (::RegQueryValueEx(hDevParaKey, L"EDID", NULL, NULL, (LPBYTE)EDIDBuf.data(), &dwEDIDSize) == ERROR_SUCCESS
						&& IdtIsCorrectEDID(EDIDBuf.data(), dwEDIDSize, lpModel) == true // 正确的EDID数据
						)
					{
						// 得到输出参数
						DWORD dwRealGetBytes = min(dwEDIDSize, dwcbBufSize);
						if (pdwGetBytes != NULL)
						{
							*pdwGetBytes = dwRealGetBytes;
						}
						memcpy(pDataBuf, EDIDBuf.data(), dwRealGetBytes);

						// 成功获取EDID数据
						bGetEDIDSuccess = true;
					}

					// 关闭键Device Parameters
					::RegCloseKey(hDevParaKey);
				}
			}

			// 关闭枚举到的键
			::RegCloseKey(hEnumKey);
		}

		// 下一个子键
		dwIndex += 1;
	}

	// 关闭设备注册表子键
	::RegCloseKey(hSubKey);

	// 返回获取EDID数据结果
	return bGetEDIDSuccess;
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