#include "IdtOther.h"

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
	wchar_t buffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(NULL, buffer, sizeof(buffer) / sizeof(wchar_t));
	if (length == 0 || length == sizeof(buffer) / sizeof(wchar_t)) return L"";

	filesystem::path fullPath(buffer);
	return fullPath.parent_path().wstring();
}
wstring GetCurrentExePath()
{
	wchar_t buffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(NULL, buffer, sizeof(buffer) / sizeof(wchar_t));
	if (length == 0 || length == sizeof(buffer) / sizeof(wchar_t)) return L"";

	return (wstring)buffer;
}
wstring GetCurrentExeName()
{
	wchar_t buffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(NULL, buffer, sizeof(buffer) / sizeof(wchar_t));
	if (length == 0 || length == sizeof(buffer) / sizeof(wchar_t)) return L"";

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

//快捷方式判断
bool IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory)
{
	IShellLink* psl;
	//CoInitialize(NULL);

	// 创建一个IShellLink对象
	HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
	if (SUCCEEDED(hres)) {
		IPersistFile* ppf;

		// 获取IShellLink的IPersistFile接口
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
		if (SUCCEEDED(hres)) {
			// 打开快捷方式文件
			hres = ppf->Load(shortcutPath.c_str(), STGM_READ);
			if (SUCCEEDED(hres)) {
				WIN32_FIND_DATAW wfd;
				ZeroMemory(&wfd, sizeof(wfd));
				// 获取快捷方式的目标路径
				hres = psl->GetPath(wfd.cFileName, MAX_PATH, NULL, SLGP_RAWPATH);
				if (SUCCEEDED(hres)) {
					// 检查目标路径是否与指定目录相匹配
					if (std::wstring(wfd.cFileName).find(targetDirectory) != std::wstring::npos) {
						return true;
					}
				}
			}
			ppf->Release();
		}
		psl->Release();
	}
	//CoUninitialize();

	return false;
}
bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath)
{
	//CoInitialize(NULL);

	// 创建一个IShellLink对象
	IShellLink* psl;
	HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);

	if (SUCCEEDED(hres)) {
		// 设置快捷方式的目标路径
		psl->SetPath(targetExePath.c_str());

		// 获取桌面目录
		LPITEMIDLIST pidl;
		SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidl);

		// 创建一个IShellLink对象的IPersistFile接口
		IPersistFile* ppf;
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

		if (SUCCEEDED(hres)) {
			// 保存快捷方式
			hres = ppf->Save(shortcutPath.c_str(), TRUE);
			ppf->Release();
		}

		psl->Release();
	}

	//CoUninitialize();

	return SUCCEEDED(hres);
}
void SetShortcut()
{
	wchar_t desktopPath[MAX_PATH];
	wstring DesktopPath;

	if (SHGetSpecialFolderPathW(0, desktopPath, CSIDL_DESKTOP, FALSE)) DesktopPath = wstring(desktopPath) + L"\\";
	else return;

	if (_waccess_s((DesktopPath + L"智绘教(屏幕批注工具).lnk").c_str(), 0) == 0)
	{
		if (IsShortcutPointingToDirectory(DesktopPath + L"智绘教(屏幕批注工具).lnk", GetCurrentExePath()))
		{
			bool flag = false;

			for (const auto& entry : std::filesystem::directory_iterator(DesktopPath))
			{
				if (std::filesystem::is_regular_file(entry) && entry.path().extension() == L".lnk")
				{
					if (entry.path().wstring() != DesktopPath + L"智绘教(屏幕批注工具).lnk" && IsShortcutPointingToDirectory(entry.path().wstring(), GetCurrentExePath()))
					{
						std::error_code ec;
						std::filesystem::remove(entry.path().wstring(), ec);

						flag = true;
					}
				}
			}

			if (!flag) return;
		}
		else
		{
			std::error_code ec;
			std::filesystem::remove(DesktopPath + L"智绘教(屏幕批注工具).lnk", ec);
			CreateShortcut(DesktopPath + L"智绘教(屏幕批注工具).lnk", GetCurrentExePath());

			for (const auto& entry : std::filesystem::directory_iterator(DesktopPath))
			{
				if (std::filesystem::is_regular_file(entry) && entry.path().extension() == L".lnk")
				{
					if (entry.path().wstring() != DesktopPath + L"智绘教(屏幕批注工具).lnk" && IsShortcutPointingToDirectory(entry.path().wstring(), GetCurrentExePath()))
					{
						std::error_code ec;
						std::filesystem::remove(entry.path().wstring(), ec);
					}
				}
			}
		}
	}
	else
	{
		for (const auto& entry : std::filesystem::directory_iterator(DesktopPath))
		{
			if (std::filesystem::is_regular_file(entry) && entry.path().extension() == L".lnk")
			{
				if (IsShortcutPointingToDirectory(entry.path().wstring(), GetCurrentExePath()))
				{
					std::error_code ec;
					std::filesystem::remove(entry.path().wstring(), ec);
				}
			}
		}
		CreateShortcut(DesktopPath + L"智绘教(屏幕批注工具).lnk", GetCurrentExePath());
	}

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	return;
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