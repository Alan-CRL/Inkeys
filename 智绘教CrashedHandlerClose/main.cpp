#include <iostream>
#include <windows.h>
#include <thread>
#include <filesystem>
#include <tlhelp32.h>

using namespace std;

#define Sleep(int) this_thread::sleep_for(chrono::milliseconds(int))
#define Test() MessageBox(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL)
#define Testi(int) MessageBox(NULL, to_wstring(int).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL)
#define Testw(wstring) MessageBox(NULL, wstring.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL)

string global_path; //程序当前路径
string main_path; //主程序路径

//string 转 wstring
wstring convert_to_wstring(const string s)
{
	LPCSTR pszSrc = s.c_str();
	int nLen = s.size();

	int nSize = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, 0, 0);
	WCHAR* pwszDst = new WCHAR[nSize + 1];
	MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, pwszDst, nSize);
	pwszDst[nSize] = 0;
	if (pwszDst[0] == 0xFEFF) // skip Oxfeff
		for (int i = 0; i < nSize; i++)
			pwszDst[i] = pwszDst[i + 1];
	wstring wcharString(pwszDst);
	delete pwszDst;
	return wcharString;
}
//wstring 转 string
string convert_to_string(const wstring str)
{
	int size = WideCharToMultiByte(CP_ACP, 0, str.c_str(), str.size(), nullptr, 0, nullptr, nullptr);
	auto p_str(std::make_unique<char[]>(size + 1));
	WideCharToMultiByte(CP_ACP, 0, str.c_str(), str.size(), p_str.get(), size, nullptr, nullptr);
	return std::string(p_str.get());
}

//终止进程
bool TerminateProcess(const std::wstring& processPath) {
	// 创建进程快照
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	// 遍历进程快照
	if (Process32First(snapshot, &entry)) {
		do {
			// 比较进程路径
			if (processPath == entry.szExeFile) {
				// 打开进程句柄
				HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
				if (process == NULL) {
					CloseHandle(snapshot);
					return false;
				}

				// 终止进程
				TerminateProcess(process, 0);
				CloseHandle(process);
			}
		} while (Process32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return true;
}
vector<HWND> GetProcessWindows(const std::wstring& processPath) {
	std::vector<HWND> result;

	// 获取进程 ID
	DWORD processId = 0;
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
					processId = entry.th32ProcessID;
					CloseHandle(process);
					break;
				}
			}

			CloseHandle(process);
		} while (Process32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);

	// 枚举窗口
	auto param = std::pair{ processId, &result };
	EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
		auto& [processId, result] = *reinterpret_cast<std::pair<DWORD, std::vector<HWND>*>*>(lParam);

		DWORD windowProcessId = 0;
		GetWindowThreadProcessId(hwnd, &windowProcessId);
		if (windowProcessId == processId) {
			result->push_back(hwnd);
		}

		return TRUE;
		}, reinterpret_cast<LPARAM>(&param));

	return result;
}

// 程序入口点
int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	//全局路径预处理
	{
		global_path = _pgmptr;
		for (int i = int(global_path.length() - 1); i >= 0; i--)
		{
			if (global_path[i] == '\\')
			{
				global_path = global_path.substr(0, i + 1);
				break;
			}
		}

		filesystem::path directory(global_path);
		main_path = directory.parent_path().parent_path().string();
	}

	vector<HWND> windows = GetProcessWindows(convert_to_wstring(main_path) + L"\\智绘教.exe");
	for (HWND hwnd : windows) ShowWindow(hwnd, SW_HIDE);
	TerminateProcess(convert_to_wstring(main_path) + L"\\智绘教.exe");

	MessageBox(NULL, L"智绘教(屏幕画板程序) 发生严重崩溃，可能导致死机。\n\n按下确定则尝试强制关闭", L"智绘教严重崩溃保护 版本20240408.01", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);

	return 0;
}