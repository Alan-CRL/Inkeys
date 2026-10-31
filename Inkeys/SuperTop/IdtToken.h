#pragma once

#include <iostream>

#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

#include <userenv.h>
#pragma comment(lib, "userenv.lib")

using namespace std;

// 句柄deleter
struct HandleDeleter
{
	void operator()(HANDLE h) const
	{
		if (h && h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
	}
};
using IdtHandle = std::unique_ptr<std::remove_pointer<HANDLE>::type, HandleDeleter>;

namespace UiAccess
{
	class DetermineToken
	{
	public:
		// 判断提升
		static bool isElevated(const IdtHandle& token)
		{
			DWORD ret_len = 0;
			TOKEN_ELEVATION_TYPE elevation = TokenElevationTypeDefault;
			// 无法确认已降权时拒绝候选令牌，不能把查询失败当成普通权限。
			return !GetTokenInformation(token.get(), TokenElevationType,
				&elevation, sizeof(elevation), &ret_len) || elevation == TokenElevationTypeFull;
		}
		// 判断UI访问
		static bool hasUiAccess(const IdtHandle& token)
		{
			DWORD ret_len = 0;
			BOOL ui_access = FALSE;
			return GetTokenInformation(token.get(), TokenUIAccess,
				&ui_access, sizeof(ui_access), &ret_len) && ui_access;
		}
	private:
		DetermineToken() = delete;
	};
	class GetToken
	{
	public:
		static bool GetSelfToken(IdtHandle& ret)
		{
			HANDLE hOpenToken = nullptr, hRetToken = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &hOpenToken)) return false;
			IdtHandle openToken(hOpenToken);
			if (!DuplicateTokenEx(openToken.get(), TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID, NULL, SecurityAnonymous, TokenPrimary, &hRetToken))
				return false;
			ret.reset(hRetToken);
			return true;
		}
		static bool GetProcessToken(DWORD pid, IdtHandle& process, IdtHandle& ret)
		{
			IdtHandle source(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
				FALSE, pid));
			if (!source) return false;
			HANDLE rawToken = nullptr, rawDuplicate = nullptr;
			if (!OpenProcessToken(source.get(), TOKEN_DUPLICATE, &rawToken)) return false;
			IdtHandle token(rawToken);
			if (!DuplicateTokenEx(token.get(), TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE |
				TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
				nullptr, SecurityAnonymous, TokenPrimary, &rawDuplicate)) return false;
			// 原进程句柄保留到交接结束；每个成功取得的句柄只有一个所有者。
			ret.reset(rawDuplicate);
			process = std::move(source);
			return true;
		}
		static bool GetUserToken(IdtHandle& ret, bool forSimulate = false)
		{
			const wchar_t* candidateNames[] = { L"explorer.exe", L"ctfmon.exe" };
			HANDLE hDupToken = nullptr;
			for (auto name : candidateNames)
			{
				HANDLE hSnapRaw = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
				if (hSnapRaw == INVALID_HANDLE_VALUE) continue;
				IdtHandle hSnapshot(hSnapRaw);

				PROCESSENTRY32W pe = {};
				pe.dwSize = sizeof(pe);
				wcout << L"尝试 " << name << " 的句柄" << endl;
				for (BOOL next = Process32FirstW(hSnapshot.get(), &pe); next; next = Process32NextW(hSnapshot.get(), &pe))
				{
					if (_wcsicmp(pe.szExeFile, name) == 0)
					{
						wcout << L"找到 " << name << " 的句柄" << endl;
						HANDLE hProcRaw = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
						if (!hProcRaw) continue;
						IdtHandle hProc(hProcRaw);

						HANDLE hTokenRaw = nullptr;
						bool success = false;
						success = OpenProcessToken(hProc.get(), TOKEN_QUERY | TOKEN_DUPLICATE, &hTokenRaw);
						if (!success) continue;
						IdtHandle hToken(hTokenRaw);

						success = false;
						if (forSimulate == false)
						{
							success = DuplicateTokenEx(
								hToken.get(),
								TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
								NULL, SecurityAnonymous, TokenPrimary, &hDupToken);
						}
						else
						{
							success = DuplicateTokenEx(
								hToken.get(),
								TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES,
								NULL, SecurityImpersonation, TokenImpersonation, &hDupToken);
						}

						if (success)
						{
							wcout << L"已获取到句柄：" << name << endl;
							IdtHandle uDup(hDupToken);
							if (!DetermineToken::isElevated(hToken))
							{
								wcout << L"确认为非特权：" << name << endl;
								ret = std::move(uDup);
								return true;
							}
							else
							{
								wcout << L"失败，具有特权：" << name << endl;
							}
						}
					}
				}
			}
			wcerr << L"获取用户token失败，无法降权启动Inkeys.exe。\n";
			return false;
		}
		static bool GetWinlogonToken(IdtHandle& ret, bool forSimulate = true)
		{
			// 目前只为 ForSimulate 而设计

			HANDLE hOpenToken = nullptr, hDupToken = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hOpenToken)) return false;
			IdtHandle openToken(hOpenToken);

			DWORD ses_self = 0, ret_len = 0;
			if (!GetTokenInformation(openToken.get(), TokenSessionId, &ses_self, sizeof(ses_self), &ret_len)) return false;

			bool success = false;
			const wchar_t* candidateNames[] = { L"winlogon.exe" };
			for (auto name : candidateNames)
			{
				HANDLE hSnapRaw = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
				if (hSnapRaw == INVALID_HANDLE_VALUE) return false;
				IdtHandle snapshot(hSnapRaw);

				PROCESSENTRY32 pe = { .dwSize = sizeof(PROCESSENTRY32) };
				for (BOOL cont = Process32First(snapshot.get(), &pe); cont; cont = Process32Next(snapshot.get(), &pe))
				{
					if (_wcsicmp(pe.szExeFile, name) == 0)
					{
						wcout << L"找到 " << name << " 的句柄" << endl;

						HANDLE hProcRaw = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
						if (!hProcRaw) continue;
						IdtHandle hProc(hProcRaw);

						HANDLE hTokenRaw = nullptr;
						bool success = false;
						if (forSimulate == false) success = OpenProcessToken(hProc.get(), TOKEN_QUERY | TOKEN_DUPLICATE, &hTokenRaw);
						else success = OpenProcessToken(hProc.get(), TOKEN_QUERY | TOKEN_DUPLICATE, &hTokenRaw);
						if (!success) continue;
						IdtHandle hToken(hTokenRaw);

						DWORD ses = 0;
						if (!GetTokenInformation(hToken.get(), TokenSessionId, &ses, sizeof(ses), &ret_len) || ses != ses_self) continue;

						success = false;
						if (forSimulate == false)
						{
							success = DuplicateTokenEx(
								hToken.get(),
								TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
								NULL, SecurityAnonymous, TokenPrimary, &hDupToken);
						}
						else
						{
							success = DuplicateTokenEx(
								hToken.get(),
								TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES,
								NULL, SecurityImpersonation, TokenImpersonation, &hDupToken);
						}

						if (success)
						{
							// 获取成功
							ret.reset(hDupToken);
							success = true;

							wcout << L"已获取到句柄：" << name << endl;
							break;
						}
					}
				}
				if (success) break;
			}
			if (!ret) return false;

			// 设置令牌权限
			if (forSimulate == true)
			{
				TOKEN_PRIVILEGES tkp = {};
				tkp.PrivilegeCount = 1;
				tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				if (!LookupPrivilegeValueW(NULL, SE_ASSIGNPRIMARYTOKEN_NAME, &tkp.Privileges[0].Luid)) return false;
				if (!AdjustTokenPrivileges(ret.get(), FALSE, &tkp, sizeof(tkp), NULL, NULL)) return false;
			}

			return true;
		}

	private:
		GetToken() = delete;
	};

	class RunToken
	{
	public:
		static bool ResumeAfterParentExit(HANDLE process, HANDLE thread,
			HANDLE parent, HANDLE ready, HANDLE accepted, DWORD timeout = 10000)
		{
			// 创建成功才确认交接；超时的 helper 不能迟到启动第二个实例。
			if (SetEvent(ready) && WaitForSingleObject(accepted, timeout) == WAIT_OBJECT_0
				&& WaitForSingleObject(parent, timeout) == WAIT_OBJECT_0
				&& ResumeThread(thread) != static_cast<DWORD>(-1)) return true;
			TerminateProcess(process, ERROR_CANCELLED);
			(void)WaitForSingleObject(process, 1000);
			return false;
		}
		static bool RunTokenProgram(const IdtHandle& token, wstring param,
			HANDLE parent = nullptr, HANDLE ready = nullptr, HANDLE accepted = nullptr)
		{
			if (!token || (!!parent != !!ready) || (!!parent != !!accepted)) return false;

			vector<wchar_t> buffer(param.begin(), param.end());
			buffer.push_back(L'\0');

			wcerr << param << endl;

			STARTUPINFO si = { sizeof(si) };
			PROCESS_INFORMATION pi = { 0 };
			GetStartupInfoW(&si);
			BOOL ok = CreateProcessWithTokenW(
				token.get(),
				LOGON_WITH_PROFILE,
				NULL,
				buffer.data(),
				CREATE_NEW_CONSOLE | (parent ? CREATE_SUSPENDED : 0),
				NULL,
				NULL,
				&si,
				&pi
			);

			if (ok)
			{
				IdtHandle process(pi.hProcess), thread(pi.hThread);
				if (parent && !ResumeAfterParentExit(process.get(), thread.get(), parent, ready, accepted))
					return false;
				wcout << L"已降权启动同目录下的 Inkeys.exe！\n";// system("pause");
				return true;
			}
			else
			{
				int i = GetLastError();
				wcerr << L"CreateProcessWithTokenW Error " << i << endl;
				//system("pause");
				return false;
			}
		}
		static bool SetUiAccessToken(const IdtHandle& winlogonToken, const IdtHandle& targetToken)
		{
			if (!winlogonToken) return false;
			if (!SetThreadToken(NULL, winlogonToken.get())) return false;
			BOOL ui_access = TRUE;
			const BOOL updated = SetTokenInformation(targetToken.get(),
				TokenUIAccess, &ui_access, sizeof(ui_access));
			// 冒用 winlogon 后任何失败都先撤销；撤销失败不能继续执行 helper。
			if (!RevertToSelf())
			{
				TerminateProcess(GetCurrentProcess(), ERROR_PRIVILEGE_NOT_HELD);
				return false;
			}
			return updated != FALSE;
		}
	private:
		RunToken() = delete;
	};
}
