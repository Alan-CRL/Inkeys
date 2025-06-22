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
			DWORD  ret_len;
			TOKEN_ELEVATION_TYPE elevation;
			GetTokenInformation(token.get(), TokenElevationType, &elevation, sizeof(elevation), &ret_len);
			return elevation == TokenElevationTypeFull;
		}
		// 判断UI访问
		static bool hasUiAccess(const IdtHandle& token)
		{
			DWORD  ret_len;
			BOOL ui_access = FALSE;
			GetTokenInformation(token.get(), TokenUIAccess, &ui_access, sizeof(ui_access), &ret_len);
			return !!ui_access;
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
		static bool GetUserToken(IdtHandle& ret)
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
						if (!OpenProcessToken(hProc.get(), TOKEN_DUPLICATE, &hTokenRaw)) continue;
						IdtHandle hToken(hTokenRaw);

						if (DuplicateTokenEx(
							hToken.get(),
							TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
							NULL, SecurityAnonymous, TokenPrimary, &hDupToken))
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
		static bool GetWinlogonToken(IdtHandle& ret)
		{
			HANDLE hOpenToken = nullptr, hTargetToken = nullptr;
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
						if (!OpenProcessToken(hProc.get(), TOKEN_QUERY | TOKEN_DUPLICATE, &hTokenRaw)) continue;
						IdtHandle hToken(hTokenRaw);

						DWORD ses = 0;
						if (!GetTokenInformation(hToken.get(), TokenSessionId, &ses, sizeof(ses), &ret_len) || ses != ses_self) continue;

						if (DuplicateTokenEx(hToken.get(), TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES, NULL, SecurityImpersonation, TokenImpersonation, &hTargetToken))
						{
							// 获取成功
							ret.reset(hTargetToken);
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
			TOKEN_PRIVILEGES tkp = {};
			tkp.PrivilegeCount = 1;
			tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			if (!LookupPrivilegeValueW(NULL, SE_ASSIGNPRIMARYTOKEN_NAME, &tkp.Privileges[0].Luid)) return false;
			if (!AdjustTokenPrivileges(ret.get(), FALSE, &tkp, sizeof(tkp), NULL, NULL)) return false;

			return true;
		}

	private:
		GetToken() = delete;
	};

	class RunToken
	{
	public:
		static bool RunTokenProgram(const IdtHandle& token, wstring param)
		{
			if (!token) return false;

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
				CREATE_NEW_CONSOLE,
				NULL,
				NULL,
				&si,
				&pi
			);

			if (ok)
			{
				wcout << L"已降权启动同目录下的 Inkeys.exe！\n"; system("pause");
				CloseHandle(pi.hProcess); // 这些也是WIN句柄
				CloseHandle(pi.hThread);
				return true;
			}
			else
			{
				int i = GetLastError();
				wcerr << L"CreateProcessWithTokenW Error " << i << endl;
				system("pause");
				return false;
			}
		}
		static bool SetUiAccessToken(const IdtHandle& winlogonToken, const IdtHandle& targetToken)
		{
			if (!SetThreadToken(NULL, winlogonToken.get())) return false;
			BOOL ui_access = TRUE;
			if (!SetTokenInformation(targetToken.get(), TokenUIAccess, &ui_access, sizeof(ui_access))) return false;
			RevertToSelf();
			return true;
		}
	private:
		RunToken() = delete;
	};
}
