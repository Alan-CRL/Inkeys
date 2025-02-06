#include "IdtOther.h"

#include "IdtConfiguration.h"

#include <io.h>
#include <objbase.h>
#include <psapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

wstring GetCurrentExeDirectory()
{
	DWORD bufferSize = MAX_PATH;
	wstring buffer(bufferSize, L'\0');
	DWORD length = 0;

	while (true)
	{
		length = GetModuleFileNameW(NULL, &buffer[0], bufferSize);
		if (length == 0) return L"";
		else if (length < bufferSize)
		{
			buffer.resize(length);
			break;
		}
		else
		{
			bufferSize *= 2;
			buffer.resize(bufferSize, L'\0');
		}
	}

	filesystem::path fullPath(buffer);
	return fullPath.parent_path().wstring();
}
wstring GetCurrentExePath()
{
	DWORD bufferSize = MAX_PATH;
	wstring buffer(bufferSize, L'\0');
	DWORD length = 0;

	while (true)
	{
		length = GetModuleFileNameW(NULL, &buffer[0], bufferSize);
		if (length == 0) return L"";
		else if (length < bufferSize)
		{
			buffer.resize(length);
			break;
		}
		else
		{
			bufferSize *= 2;
			buffer.resize(bufferSize, L'\0');
		}
	}

	return buffer;
}
wstring GetCurrentExeName()
{
	DWORD bufferSize = MAX_PATH;
	wstring buffer(bufferSize, L'\0');
	DWORD length = 0;

	while (true)
	{
		length = GetModuleFileNameW(NULL, &buffer[0], bufferSize);
		if (length == 0) return L"";
		else if (length < bufferSize)
		{
			buffer.resize(length);
			break;
		}
		else
		{
			bufferSize *= 2;
			buffer.resize(bufferSize, L'\0');
		}
	}

	filesystem::path fullPath(buffer);
	return fullPath.filename().wstring();
}

//网络状态获取
bool checkIsNetwork()
{
	//  通过NLA接口获取网络状态
	IUnknown* pUnknown = NULL;
	BOOL   bOnline = TRUE;//是否在线
	HRESULT Result = CoCreateInstance(CLSID_NetworkListManager, NULL, CLSCTX_ALL,
		IID_IUnknown, (void**)&pUnknown);
	if (SUCCEEDED(Result))
	{
		INetworkListManager* pNetworkListManager = NULL;
		if (pUnknown)
			Result = pUnknown->QueryInterface(IID_INetworkListManager, (void
				**)&pNetworkListManager);
		if (SUCCEEDED(Result))
		{
			VARIANT_BOOL IsConnect = VARIANT_FALSE;
			if (pNetworkListManager)
				Result = pNetworkListManager->get_IsConnectedToInternet(&IsConnect);
			if (SUCCEEDED(Result))
			{
				bOnline = (IsConnect == VARIANT_TRUE) ? true : false;
			}
		}
		if (pNetworkListManager)
			pNetworkListManager->Release();
	}
	if (pUnknown) pUnknown->Release();

	return bOnline;
}
// 提取指定模块中的资源文件
bool ExtractResource(LPCTSTR strDstFile, LPCTSTR strResType, LPCTSTR strResName)
{
	// 创建文件
	HANDLE hFile = ::CreateFile(strDstFile, GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	// 查找资源文件、加载资源到内存、得到资源大小
	HRSRC	hRes = ::FindResource(NULL, strResName, strResType);
	HGLOBAL	hMem = ::LoadResource(NULL, hRes);
	DWORD	dwSize = ::SizeofResource(NULL, hRes);

	// 写入文件
	DWORD dwWrite = 0;		// 返回写入字节
	::WriteFile(hFile, hMem, dwSize, &dwWrite, NULL);
	::CloseHandle(hFile);

	return true;
}

//判断id是否错乱
bool isValidString(const wstring& str)
{
	for (wchar_t ch : str)
	{
		// 如果字符不是可打印的，并且不是空格，则认为是乱码
		if (!iswprint(ch) && !iswspace(ch))  return false;
	}
	return true;
}
// 判断字符串中是否是合法 ASCII 字符
bool isAsciiPrintable(const wstring& input)
{
	for (wchar_t c : input)
		if (c < 32 || c > 126)
			return false;
	return true;
}

// 程序进程状态获取
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
// 进程程序路径查询
int ProcessRunningCnt(const std::wstring& processPath)
{
	int ret = 0;

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(snapshot, &entry))
	{
		while (Process32Next(snapshot, &entry))
		{
			// 获取进程的完整路径
			wchar_t processFullPath[MAX_PATH] = L"";

			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
			if (hProcess)
			{
				HMODULE hMod;
				DWORD cbNeeded;
				if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
				{
					GetModuleFileNameExW(hProcess, hMod, processFullPath, MAX_PATH);
				}
				CloseHandle(hProcess);
			}

			// 比较路径是否相同
			if (wcslen(processFullPath) > 0 && wcscmp(processFullPath, processPath.c_str()) == 0) ret++;
		}
	}

	CloseHandle(snapshot);
	return ret;
}

// 设置开机自启状态
bool SetStartupState(bool bAutoRun, wstring path, wstring nameclass)
{
	path = L"\"" + path + L"\"";

	wchar_t pFileName[MAX_PATH] = { 0 };
	wcscpy_s(pFileName, path.c_str());

	HKEY hKey;
	LPCTSTR lpRun = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	long lRet = RegOpenKeyEx(HKEY_CURRENT_USER, lpRun, 0, KEY_WRITE, &hKey);
	if (lRet != ERROR_SUCCESS) return false;

	if (bAutoRun) RegSetValueEx(hKey, nameclass.c_str(), 0, REG_SZ, (const BYTE*)(LPCSTR)pFileName, MAX_PATH);
	else RegDeleteValue(hKey, nameclass.c_str());

	RegCloseKey(hKey);
	return true;
}
// 查询开机自启状态
bool QueryStartupState(wstring path, wstring nameclass)
{
	wchar_t pFileName[MAX_PATH] = { 0 };
	wcscpy_s(pFileName, path.c_str());

	path = L"\"" + path + L"\"";
	wchar_t pFileName2[MAX_PATH] = { 0 };
	wcscpy_s(pFileName2, path.c_str());

	HKEY hKey;
	LPCTSTR lpRun = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	long lRet = RegOpenKeyEx(HKEY_CURRENT_USER, lpRun, 0, KEY_READ, &hKey); // 修改此处为KEY_READ
	if (lRet != ERROR_SUCCESS) return false;

	wchar_t regValue[MAX_PATH] = { 0 };
	DWORD dwType;
	DWORD dwSize = sizeof(regValue);
	lRet = RegQueryValueEx(hKey, nameclass.c_str(), 0, &dwType, (LPBYTE)regValue, &dwSize);
	if (lRet != ERROR_SUCCESS || dwType != REG_SZ || (wcscmp(regValue, pFileName) != 0 && wcscmp(regValue, pFileName2) != 0)) // 如果查询不到值或者值不是字符串或者值不等于指定路径，则返回false
	{
		RegCloseKey(hKey);
		return false;
	}

	RegCloseKey(hKey);
	return true;
}