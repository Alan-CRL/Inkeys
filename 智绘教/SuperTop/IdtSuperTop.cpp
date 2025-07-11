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
	DWORD inkeysPid = 0;
	//bool useAdmin = false;
	bool useUiAccess = true;

	{
		wstring cmdLine(lpCmdLine);

		wregex re(L"^-PATH=\\$(.*?)\\$\\s+-PID=(\\d+)\\s+-UIACCESS=(\\d+)\\s+-ADMIN=(\\d+)");
		wsmatch match;
		if (regex_search(cmdLine, match, re))
		{
			inkeysCmdLine = match[1].str();
			inkeysPid = stoi(match[2]);
			useUiAccess = stoi(match[3]);
			//useAdmin = stoi(match[4]);
		}

		//if (inkeysPid == 0) useAdmin = false;
	}

	// 获取 智绘教 的令牌
	IdtHandle inkeysToken;
	if (inkeysPid)
	{
		HANDLE proc_ref = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, inkeysPid);
		HANDLE tok_parent, tInkeysToken;
		try_win32(OpenProcessToken(proc_ref, TOKEN_DUPLICATE, &tok_parent));
		try_win32(DuplicateTokenEx(tok_parent, TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID, NULL, SecurityAnonymous, TokenPrimary, &tInkeysToken));

		//if (isElevated(inkeysToken.handle)) Testi(999);
		//if (inkeysToken.handle == INVALID_HANDLE_VALUE) Testi(999);

		IdtHandle uDup(tInkeysToken);
		inkeysToken = std::move(uDup);
	}

	// ---

	HANDLE fileHandle = NULL;
	OccupyFileForWrite(&fileHandle, globalPath + L"superTop_wait.signal");
	UnOccupyFile(&fileHandle);

	IdtHandle winlogonToken;
	cout << "1 " << UiAccess::GetToken::GetWinlogonToken(winlogonToken) << endl;

	cout << "2 " << UiAccess::RunToken::SetUiAccessToken(winlogonToken, inkeysToken) << endl;

	// ---

	// 等待原先 智绘教 退出
	for (int i = 1; i <= 30; i++)
	{
		this_thread::sleep_for(chrono::milliseconds(50));
		if (!filesystem::exists(globalPath + L"superTop_wait.signal")) break;
	}

	// 启动智绘教
	wstring param = L"\"" + GetCurrentExePath() + L"\" -SuperTopC " + inkeysCmdLine;
	cout << "3 " << UiAccess::RunToken::RunTokenProgram(inkeysToken, param) << endl;
}

// ---

IdtAtomic<bool> hasSuperTop;
void LaunchSurperTop()
{
	wstring cmdLine;
	{
		if (launchState == LaunchStateEnum::Restart) cmdLine = L"-Restart";
		else if (launchState == LaunchStateEnum::WarnTry) cmdLine = L"-WarnTry";
		else if (launchState == LaunchStateEnum::CrashTry)  cmdLine = L"-CrashTry";
	}
	wstring pid = to_wstring(GetCurrentProcessId());
	wstring useUiAccess = L"1";
	wstring useAdmin = L"0";

	wstring launchLine = L"-SuperTop ";
	launchLine += L"-PATH=$" + cmdLine + L"$ ";
	launchLine += L"-PID=" + pid + L" ";
	launchLine += L"-UIACCESS=" + useUiAccess + L" ";
	launchLine += L"-ADMIN=" + useAdmin;

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