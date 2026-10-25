import Inkeys.Conv.Text;
import Inkeys.Text.Split;
import Inkeys.UI.MessageBox;

#include "IdtSuperTop.h"

#include "../IdtConfiguration.h"
#include "../IdtOther.h"
#include "../Launch/IdtLaunchState.h"
#include "IdtToken.h"

#include <tlhelp32.h>
#include <stdexcept>
#include <cstdio>

#ifdef MessageBox
#undef MessageBox
#endif

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
	DWORD ret_len = 0;
	TOKEN_ELEVATION_TYPE elevation = TokenElevationTypeDefault;
	return !GetTokenInformation(tok, TokenElevationType, &elevation,
		sizeof(elevation), &ret_len) || elevation == TokenElevationTypeFull;
}
// 判断进程句柄是否具有 UI 访问权限
bool hasUiAccess(HANDLE tok) {
	DWORD ret_len = 0;
	BOOL ui_access = FALSE;
	return GetTokenInformation(tok, TokenUIAccess, &ui_access,
		sizeof(ui_access), &ret_len) && ui_access;
}

int RunSuperTopTokenFailureTest() noexcept
{
	// 测试进程仅冒用自己的复制令牌；无效目标强制走设置失败后的撤销路径。
	HANDLE existing = nullptr;
	if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &existing))
	{
		CloseHandle(existing);
		std::fputs("[SuperTopToken] FAIL: test thread already impersonates\n", stderr);
		return 1;
	}
	if (GetLastError() != ERROR_NO_TOKEN) return 1;
	HANDLE rawProcessToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY,
		&rawProcessToken)) return 1;
	IdtHandle processToken(rawProcessToken);
	HANDLE rawImpersonation = nullptr;
	if (!DuplicateTokenEx(processToken.get(), TOKEN_IMPERSONATE | TOKEN_QUERY,
		nullptr, SecurityImpersonation, TokenImpersonation,
		&rawImpersonation)) return 1;
	IdtHandle impersonation(rawImpersonation);
	IdtHandle invalidTarget;
	const bool updated = UiAccess::RunToken::SetUiAccessToken(
		impersonation, invalidTarget);
	HANDLE after = nullptr;
	const bool stillImpersonating = OpenThreadToken(GetCurrentThread(),
		TOKEN_QUERY, TRUE, &after) != FALSE;
	const DWORD afterError = stillImpersonating ? ERROR_SUCCESS : GetLastError();
	if (after) CloseHandle(after);
	if (updated || stillImpersonating || afterError != ERROR_NO_TOKEN)
	{
		std::fputs("[SuperTopToken] FAIL: failed UIAccess update retained thread token\n", stderr);
		return 1;
	}
	const auto fail = [](const char* detail)
	{
		std::fprintf(stderr, "[SuperTopToken] FAIL: %s\n", detail);
		return 1;
	};
	if (hasUiAccess(nullptr) || !isElevated(nullptr) ||
		UiAccess::DetermineToken::hasUiAccess(invalidTarget) ||
		!UiAccess::DetermineToken::isElevated(invalidTarget))
		return fail("invalid token query did not fail closed");
	DWORD returned = 0;
	BOOL expectedUiAccess = FALSE;
	TOKEN_ELEVATION_TYPE expectedElevation = TokenElevationTypeDefault;
	if (!GetTokenInformation(processToken.get(), TokenUIAccess, &expectedUiAccess,
		sizeof(expectedUiAccess), &returned) ||
		!GetTokenInformation(processToken.get(), TokenElevationType, &expectedElevation,
			sizeof(expectedElevation), &returned) ||
		hasUiAccess(processToken.get()) != (expectedUiAccess != FALSE) ||
		UiAccess::DetermineToken::hasUiAccess(processToken) != (expectedUiAccess != FALSE) ||
		isElevated(processToken.get()) != (expectedElevation == TokenElevationTypeFull) ||
		UiAccess::DetermineToken::isElevated(processToken) != (expectedElevation == TokenElevationTypeFull))
		return fail("valid token query changed its result");
	{
		IdtHandle process, token;
		if (UiAccess::GetToken::GetProcessToken(0, process, token) || process || token)
			return fail("invalid parent PID published handles");
		if (!UiAccess::GetToken::GetProcessToken(GetCurrentProcessId(), process, token) ||
			GetProcessId(process.get()) != GetCurrentProcessId() ||
			hasUiAccess(token.get()) != (expectedUiAccess != FALSE))
			return fail("normal parent token duplication failed");
	}
	DWORD handlesBefore = 0, handlesAfter = 0;
	if (!GetProcessHandleCount(GetCurrentProcess(), &handlesBefore)) return 1;
	for (unsigned i = 0; i < 64; ++i)
	{
		IdtHandle process, token;
		if (!UiAccess::GetToken::GetProcessToken(GetCurrentProcessId(), process, token))
			return fail("repeated parent token duplication failed");
	}
	if (!GetProcessHandleCount(GetCurrentProcess(), &handlesAfter) || handlesAfter != handlesBefore)
		return fail("parent process or token handle leaked");

	// 只创建无窗口的暂停 cmd 子进程，覆盖真实恢复、超时和确认/恢复失败，不触发 UAC。
	wchar_t systemDirectory[MAX_PATH]{};
	const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
	if (!length || length >= MAX_PATH) return 1;
	const wstring executable = wstring(systemDirectory) + L"\\cmd.exe";
	for (unsigned mode = 0; mode < 5; ++mode)
	{
		wstring command = L"\"" + executable + L"\" /d /c exit 0";
		STARTUPINFOW startup{ sizeof(startup) };
		PROCESS_INFORMATION created{};
		if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
			CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &created))
			return fail("could not create isolated suspended child");
		IdtHandle child(created.hProcess), thread(created.hThread);
		IdtHandle parent(CreateEventW(nullptr, TRUE, mode != 1, nullptr));
		IdtHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		IdtHandle accepted(CreateEventW(nullptr, TRUE, mode != 4, nullptr));
		if (!parent || !ready || !accepted)
		{
			TerminateProcess(child.get(), ERROR_CANCELLED);
			(void)WaitForSingleObject(child.get(), 1000);
			return fail("could not create handoff test events");
		}
		const bool resumed = UiAccess::RunToken::ResumeAfterParentExit(child.get(),
			mode == 2 ? nullptr : thread.get(), parent.get(), mode == 3 ? nullptr : ready.get(), accepted.get(), 0);
		DWORD childExit = STILL_ACTIVE;
		if (resumed != (mode == 0) || WaitForSingleObject(child.get(), 5000) != WAIT_OBJECT_0 ||
			!GetExitCodeProcess(child.get(), &childExit) || childExit != (mode == 0 ? 0u : ERROR_CANCELLED))
			return fail("handoff did not resume or cancel the child correctly");
		if (WaitForSingleObject(ready.get(), 0) != (mode == 3 ? WAIT_TIMEOUT : WAIT_OBJECT_0))
			return fail("handoff readiness was not published correctly");
	}
	std::fputs("[SuperTopToken] PASS: token guards, handle ownership, suspended handoff and impersonation cleanup\n", stderr);
	return 0;
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
	wstring readyEventName;

	cout << utf16ToUtf8(lpCmdLine) << endl;

	{
		vector<wstring> args = Inkeys::Split::Run(lpCmdLine, L'?');
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
			else if (commandLine.rfind(L"-ReadyEvent=", 0) == 0)
				readyEventName = commandLine.substr(12);
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

	IdtHandle inkeysToken, parentProcess, readyEvent, acceptedEvent;
	if (!readyEventName.empty())
	{
		readyEvent.reset(OpenEventW(EVENT_MODIFY_STATE, FALSE, readyEventName.c_str()));
		acceptedEvent.reset(OpenEventW(SYNCHRONIZE, FALSE, (readyEventName + L".Accepted").c_str()));
		if (!readyEvent || !acceptedEvent) return;
	}

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
			if (!UiAccess::GetToken::GetProcessToken(inkeysPid, parentProcess, inkeysToken))
				return;
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

	// UiAccess 操作

	if (useUiAccess)
	{
		IdtHandle winlogonToken;
		if (!UiAccess::GetToken::GetWinlogonToken(winlogonToken))
		{
			cout << "GetWinlogonToken Fail" << endl;
			return;
		}
		if (!UiAccess::RunToken::SetUiAccessToken(winlogonToken, inkeysToken))
		{
			cout << "SetUiAccessToken Fail" << endl;
			return;
		}
	}

	// 启动新进程

	wstring param = L"\"" + GetCurrentExePath() + L"\" -SuperTopComplete " + inkeysCmdLine;
	if (!UiAccess::RunToken::RunTokenProgram(inkeysToken, param,
		readyEvent ? parentProcess.get() : nullptr, readyEvent.get(), acceptedEvent.get()))
	{
		cout << "RunTokenProgram Fail" << endl;
		// 旧实例仍存活时由它统一提示并降级；避免超时 helper 再弹出第二份提示。
		if (readyEvent && WaitForSingleObject(parentProcess.get(), 0) != WAIT_OBJECT_0)
			return;

		auto request = Inkeys::UI::MessageBox::MakeOkRequest(
			L"Inkeys Tips",
			L"Inkeys SuperTop failed. Reopen Inkeys; SuperTop will be disabled automatically.");
		request.ownerlessTopmostAtCreation = true;
		request.fallback.modality =
			Inkeys::UI::MessageBox::SystemModality::System;
		(void)Inkeys::UI::MessageBox::Show(request);
	}
}

// ---

IdtAtomic<bool> hasSuperTop;
void LaunchSurperTop(wstring cmdLine)
{
	const auto reportFailure = []
	{
		auto request = Inkeys::UI::MessageBox::MakeOkRequest(L"Inkeys Tips",
			L"Inkeys SuperTop failed. Inkeys will continue without SuperTop.");
		request.ownerlessTopmostAtCreation = true;
		request.fallback.modality = Inkeys::UI::MessageBox::SystemModality::System;
		(void)Inkeys::UI::MessageBox::Show(request);
	};
	wstring pid = to_wstring(GetCurrentProcessId());
	const wstring readyEventName = L"Local\\Inkeys.SuperTop.Ready." + pid +
		L"." + to_wstring(GetTickCount64());
	IdtHandle readyEvent(CreateEventW(nullptr, TRUE, FALSE, readyEventName.c_str()));
	if (!readyEvent || GetLastError() == ERROR_ALREADY_EXISTS)
	{
		reportFailure();
		return;
	}
	IdtHandle acceptedEvent(CreateEventW(nullptr, TRUE, FALSE, (readyEventName + L".Accepted").c_str()));
	if (!acceptedEvent || GetLastError() == ERROR_ALREADY_EXISTS)
	{
		reportFailure();
		return;
	}
	wstring useUiAccess = L"1"; // 0/1 是否设置
	wstring usePermission = L"1"; // 0 使用自身；1 使用 PID；2 降权使用普通用户

	wstring launchLine = L"-SuperTop=*";
	launchLine += L"-CmdLine=?" + cmdLine + L"? ";
	launchLine += L"-UiAccess=" + useUiAccess + L" ";
	launchLine += L"-Permission=" + usePermission + L" ";
	launchLine += L"-Pid=" + pid + L" ";
	launchLine += L"-ReadyEvent=" + readyEventName;
	launchLine += L"*";

	const wstring executable = GetCurrentExePath();
	SHELLEXECUTEINFOW execute{ sizeof(execute) };
	execute.fMask = SEE_MASK_NOCLOSEPROCESS;
	execute.lpVerb = L"runas";
	execute.lpFile = executable.c_str();
	execute.lpParameters = launchLine.c_str();
	execute.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&execute))
	{
		if (GetLastError() != ERROR_CANCELLED) reportFailure();
		return;
	}
	IdtHandle helperProcess(execute.hProcess);
	if (helperProcess)
	{
		const HANDLE waits[]{ readyEvent.get(), helperProcess.get() };
		// helper 退出或等待超时均保留旧实例；只有替代进程已暂停就绪才交接。
		if (WaitForMultipleObjects(2, waits, FALSE, 10000) == WAIT_OBJECT_0
			&& SetEvent(acceptedEvent.get()))
			exit(0);
	}
	reportFailure();
}
