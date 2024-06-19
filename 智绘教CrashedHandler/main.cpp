#include <filesystem>
#include <fstream>
#include <iostream>
#include <ShlObj.h>
#include <sstream>
#include <string>
#include <thread>
#include <tlhelp32.h>
#include <windows.h>
using namespace std;

#define Sleep(int) this_thread::sleep_for(chrono::milliseconds(int))
#define Test() MessageBox(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL)
#define Testi(int) MessageBox(NULL, to_wstring(int).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL)
#define Testw(wstring) MessageBox(NULL, wstring.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL)

string edition_date = "20240618.01";
string global_path; //程序当前路径
string main_path; //主程序路径

//string to wstring
wstring string_to_wstring(const string& s)
{
	if (s.empty()) return L"";

	int sizeRequired = MultiByteToWideChar(936, 0, s.c_str(), -1, NULL, 0);
	if (sizeRequired == 0) return L"";

	wstring ws(sizeRequired - 1, L'\0');
	MultiByteToWideChar(936, 0, s.c_str(), -1, &ws[0], sizeRequired);

	return ws;
}
//wstring to string
string wstring_to_string(const wstring& ws)
{
	if (ws.empty()) return "";

	int sizeRequired = WideCharToMultiByte(936, 0, ws.c_str(), -1, NULL, 0, NULL, NULL);
	if (sizeRequired == 0) return "";

	string s(sizeRequired - 1, '\0');
	WideCharToMultiByte(936, 0, ws.c_str(), -1, &s[0], sizeRequired, NULL, NULL);

	return s;
}

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
// 路径权限检测
bool HasReadWriteAccess(const std::wstring& directoryPath)
{
	DWORD attributes = GetFileAttributesW(directoryPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
	if (attributes & FILE_ATTRIBUTE_READONLY) return false;
	if (attributes & FILE_ATTRIBUTE_READONLY) return false;

	return true;
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
		global_path = wstring_to_string(GetCurrentExeDirectory() + L"\\");

		if (!HasReadWriteAccess(string_to_wstring(global_path)))
		{
			if (IsUserAnAdmin()) MessageBox(NULL, L"当前目录权限受限无法正常运行，请将程序转移至其他目录", L"智绘教崩溃保护提示", MB_OK);
			else ShellExecute(NULL, L"runas", GetCurrentExePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
			return 0;
		}

		filesystem::path directory(global_path);
		main_path = directory.parent_path().parent_path().string() + "\\";
	}

	wstring exePath;
	wstringstream wss(GetCommandLineW());
	getline(wss, exePath, L'/');
	getline(wss, exePath, L'\"');
	getline(wss, exePath, L'\"');
	if (exePath == L"") exePath = string_to_wstring(main_path) + L"智绘教.exe";

	while (1)
	{
		for (int for_i = 1; for_i <= 20 && _waccess((string_to_wstring(global_path) + L"open.txt").c_str(), 4) != 0; for_i++)
		{
			Sleep(1000);
			if (for_i == 20) exit(0);
		}

		int now_value = -1, past_value = -1;
		ifstream read;
		read.imbue(locale("zh_CN.UTF8"));
		while (1)
		{
			for (int i = 1;; i++)
			{
				if (_waccess((string_to_wstring(global_path) + L"open.txt").c_str(), 4) == -1) exit(0);

				now_value = -1;
				read.open(wstring_to_string(string_to_wstring(global_path) + L"open.txt").c_str());
				read >> now_value;
				read.close();

				if (now_value == -2) break;
				if (now_value != -1 && now_value != 1) break;
				Sleep(100);

				if (i > 20) goto solve;
			}

			if (now_value == -2)
			{
				error_code ec;
				filesystem::remove(string_to_wstring(global_path) + L"open.txt", ec);

				ShellExecute(NULL, NULL, exePath.c_str(), NULL, NULL, SW_SHOWNORMAL);

				break;
			}
			else if (now_value == past_value)
			{
			solve:

				if (MessageBox(NULL, L"智绘教(屏幕批注程序) 疑似崩溃\n\n是否重启程序？", (L"智绘教崩溃重启 版本" + string_to_wstring(edition_date)).c_str(), MB_YESNO | MB_SYSTEMMODAL) == IDYES)
				{
					//if (isProcessRunning(exePath))
					//	ShellExecute(NULL, L"runas", (string_to_wstring(global_path) + L"智绘教CrashedHandlerClose.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);

					error_code ec;
					filesystem::remove(string_to_wstring(global_path) + L"open.txt", ec);

					std::ofstream file((string_to_wstring(main_path) + L"force_start.signal").c_str());
					file.close();

					ShellExecute(NULL, NULL, exePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
				}
				else exit(0);

				break;
			}
			past_value = now_value;

			Sleep(3000);
		}
		Sleep(1000);
	}

	return 0;
}