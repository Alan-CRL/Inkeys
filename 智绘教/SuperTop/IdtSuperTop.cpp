#include "IdtSuperTop.h"

#include "../IdtConfiguration.h"
#include "../IdtOther.h"
#include "../Launch/IdtLaunchState.h"
#include "IdtToken.h"

#include <tlhelp32.h>
#include <stdexcept>

#define try_win32(x) if(!(x)) [[unlikely]] throw_win32_error();
void throw_win32_error()
{
	DWORD err = GetLastError();  // 获取错误代码
	LPSTR msg = nullptr;

	//// 格式化错误信息
	//FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	//	NULL, err, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL);
	//// 显示错误信息
	//MessageBoxA(NULL, msg, NULL, 0);

	//// 抛出运行时异常，包含错误信息
	//throw std::runtime_error(msg);

	//exit(0);
}

// 判断当前进程是否具有提升权限（管理员权限）
bool isElevated(HANDLE tok) {
	DWORD  ret_len;
	TOKEN_ELEVATION_TYPE elevation;
	GetTokenInformation(tok, TokenElevationType, &elevation, sizeof(elevation), &ret_len);  // 获取权限信息
	return elevation == TokenElevationTypeFull;  // 如果权限类型为完全提升，返回 true
}
// 判断进程句柄是否具有 UI 访问权限
bool hasUiAccess(HANDLE tok) {
	DWORD  ret_len;
	BOOL ui_access;
	GetTokenInformation(tok, TokenUIAccess, &ui_access, sizeof(ui_access), &ret_len);  // 获取 UI 访问权限信息
	return ui_access;  // 返回 UI 访问权限的值
}

void SurperTopMain(wstring lpCmdLine)
{
	//AllocConsole();
	//FILE* fp;
	//freopen_s(&fp, "CONOUT$", "w", stdout);
	//freopen_s(&fp, "CONOUT$", "w", stderr);
	//std::ios::sync_with_stdio();

	// 基础信息
	wstring inkeysCmdLine;
	int useUiAccess = 1; // 0/1 是否设置
	int usePermission = 0; // 0 使用自身；1 使用 PID；2 降权使用普通用户
	DWORD inkeysPid = 0;

	cout << utf16ToUtf8(lpCmdLine) << endl;

	{
		vector<wstring> args = CustomSplit::Run(lpCmdLine, L'?');
		for (size_t i = 0; i < args.size(); i++)
		{
			wstring commandLine = args[i];

			cout << i << " : \"" << utf16ToUtf8(commandLine) << "\"" << endl;

			if (commandLine.substr(0, 8) == L"-CmdLine")
			{
				wregex pattern(LR"(^[^?]*\?([^?]+)\?[^?]*$)");
				wsmatch match;
				if (regex_match(commandLine, match, pattern))
				{
					inkeysCmdLine = match[1].str();
					cout << "1 inkeysCmdLine=" << utf16ToUtf8(inkeysCmdLine) << endl;
				}
			}
			else if (commandLine.substr(0, 9) == L"-UiAccess")
			{
				wregex pattern(LR"(^.*=(-?[0-9]+)$)");
				wsmatch match;

				if (regex_match(commandLine, match, pattern))
				{
					try
					{
						useUiAccess = stoi(match[1].str());
						cout << "2 useUiAccess=" << useUiAccess << endl;
					}
					catch (const out_of_range&) {}
					catch (const invalid_argument&) {}
				}
			}
			else if (commandLine.substr(0, 11) == L"-Permission")
			{
				wregex pattern(LR"(^.*=(-?[0-9]+)$)");
				wsmatch match;

				if (regex_match(commandLine, match, pattern))
				{
					try
					{
						usePermission = stoi(match[1].str());
						cout << "3 usePermission=" << usePermission << endl;
					}
					catch (const out_of_range&) {}
					catch (const invalid_argument&) {}
				}
			}
			else if (commandLine.substr(0, 4) == L"-Pid")
			{
				wregex pattern(LR"(^.*=(-?[0-9]+)$)");
				wsmatch match;

				if (regex_match(commandLine, match, pattern))
				{
					try
					{
						inkeysPid = stoi(match[1].str());
						cout << "4 inkeysPid=" << inkeysPid << endl;
					}
					catch (const out_of_range&) {}
					catch (const invalid_argument&) {}
				}
			}
		}
	}

	// 准备工作

	IdtHandle inkeysToken;

	// 获取当前进程的令牌
	if (usePermission == 0)
	{
		if (!UiAccess::GetToken::GetSelfToken(inkeysToken))
		{
			usePermission++;
		}
	}
	// 获取之前的智绘教令牌，通过Pid
	if (usePermission == 1)
	{
		if (inkeysPid)
		{
			HANDLE proc_ref = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, inkeysPid);
			HANDLE tok_parent, tInkeysToken;
			try_win32(OpenProcessToken(proc_ref, TOKEN_DUPLICATE, &tok_parent));
			try_win32(DuplicateTokenEx(tok_parent, TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID, NULL, SecurityAnonymous, TokenPrimary, &tInkeysToken));

			IdtHandle uDup(tInkeysToken);
			inkeysToken = std::move(uDup);
		}
		else usePermission++;
	}
	// 获取降权令牌
	if (usePermission == 2)
	{
		if (!UiAccess::GetToken::GetUserToken(inkeysToken))
		{
			usePermission++;
		}
	}

	// 标识准备工作完成
	// 例如可能需要获取之前智绘教进程的令牌，现在原来的智绘教则可以关闭
	{
		HANDLE fileHandle = NULL;
		OccupyFileForWrite(&fileHandle, globalPath + L"superTop_wait.signal");
		UnOccupyFile(&fileHandle);
	}

	// UiAccess 操作

	if (useUiAccess)
	{
		IdtHandle winlogonToken;
		if (!UiAccess::GetToken::GetWinlogonToken(winlogonToken))
		{
			cout << "GetWinlogonToken Fail" << endl;
		}
		if (!UiAccess::RunToken::SetUiAccessToken(winlogonToken, inkeysToken))
		{
			cout << "SetUiAccessToken Fail" << endl;
		}
	}

	// 启动新进程

	// 等待原先 智绘教 退出
	for (int i = 1; i <= 60; i++)
	{
		this_thread::sleep_for(chrono::milliseconds(50));
		if (!filesystem::exists(globalPath + L"superTop_wait.signal")) break;
	}

	wstring param = L"\"" + GetCurrentExePath() + L"\" -SuperTopComplete " + inkeysCmdLine;
	if (!UiAccess::RunToken::RunTokenProgram(inkeysToken, param))
	{
		cout << "RunTokenProgram Fail" << endl;
	}
}

// ---

IdtAtomic<bool> hasSuperTop;
void LaunchSurperTop(wstring cmdLine)
{
	wstring pid = to_wstring(GetCurrentProcessId());
	wstring useUiAccess = L"1"; // 0/1 是否设置
	wstring usePermission = L"1"; // 0 使用自身；1 使用 PID；2 降权使用普通用户

	wstring launchLine = L"-SuperTop=*";
	launchLine += L"-CmdLine=?" + cmdLine + L"? ";
	launchLine += L"-UiAccess=" + useUiAccess + L" ";
	launchLine += L"-Permission=" + usePermission + L" ";
	launchLine += L"-Pid=" + pid;
	launchLine += L"*";

	error_code ec;
	if (filesystem::exists(globalPath + L"superTop_wait.signal"))
		filesystem::remove(globalPath + L"superTop_wait.signal", ec);

	HINSTANCE hResult = ShellExecuteW(NULL, L"runas", GetCurrentExePath().c_str(), launchLine.c_str(), NULL, SW_SHOWNORMAL);

	if ((INT_PTR)hResult <= 32) return;
	else
	{
		for (int i = 1; i <= 60; i++)
		{
			if (filesystem::exists(globalPath + L"superTop_wait.signal"))break;
			this_thread::sleep_for(chrono::milliseconds(50));
		}
		filesystem::remove(globalPath + L"superTop_wait.signal", ec);

		exit(0);
	}
}