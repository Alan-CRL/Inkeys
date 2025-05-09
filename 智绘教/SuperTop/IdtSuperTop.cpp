#include "IdtSuperTop.h"

#include "../IdtConfiguration.h"
#include "../IdtOther.h"
#include "../Launch/IdtLaunchState.h"

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

class Handle {
public:
	HANDLE handle = INVALID_HANDLE_VALUE;  // 默认句柄为无效句柄

	// 默认构造函数
	Handle() {}

	// 传入句柄的构造函数
	Handle(HANDLE handle) :handle(handle) {}

	// 析构函数，确保句柄被关闭
	~Handle() {
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
	}
};

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
	/*AllocConsole();
	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	std::ios::sync_with_stdio();*/

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

	//cerr << "step1" << endl;

	// 获取 智绘教 的令牌
	Handle inkeysToken;
	if (inkeysPid)
	{
		Handle proc_ref = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, inkeysPid);
		Handle tok_parent;
		try_win32(OpenProcessToken(proc_ref.handle, TOKEN_DUPLICATE, &tok_parent.handle));
		try_win32(DuplicateTokenEx(tok_parent.handle, TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, NULL, SecurityAnonymous, TokenPrimary, &inkeysToken.handle));

		//if (isElevated(inkeysToken.handle)) Testi(999);
		//if (inkeysToken.handle == INVALID_HANDLE_VALUE) Testi(999);
	}
	// 目标获取的令牌
	Handle winlogonToken;

	// ---

	HANDLE fileHandle = NULL;
	OccupyFileForWrite(&fileHandle, globalPath + L"superTop_wait.signal");
	UnOccupyFile(&fileHandle);

	//cerr << "step2" << endl;
	// 获取当前进程的信息
	HANDLE proc_self = GetCurrentProcess();
	Handle tok_self;
	try_win32(OpenProcessToken(proc_self, TOKEN_ALL_ACCESS, &tok_self.handle));
	DWORD ses_self, ret_len;
	try_win32(GetTokenInformation(tok_self.handle, TokenSessionId, &ses_self, sizeof(ses_self), &ret_len)); // 获取当前会话 ID

	//cerr << "step3" << endl;
	// 创建进程快照
	// 目前不涉及降权，暂时不用
	//bool ctfmonAble = false, explorerAble = false;
	//Handle ctfmonToken, explorerToken;

	Handle snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe = { .dwSize = sizeof(PROCESSENTRY32) };
	// 遍历进程快照，查找目标进程
	for (BOOL cont = Process32First(snapshot.handle, &pe); cont; cont = Process32Next(snapshot.handle, &pe))
	{
		//if (0 == _tcsicmp(pe.szExeFile, TEXT("explorer.exe")))
		//{
		//	Handle proc_ref = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
		//	Handle tok_parent;
		//	try_win32(OpenProcessToken(proc_ref.handle, TOKEN_DUPLICATE, &tok_parent.handle));
		//	try_win32(DuplicateTokenEx(tok_parent.handle, TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, NULL, SecurityAnonymous, TokenPrimary, &explorerToken.handle));

		//	if (explorerToken.handle != INVALID_HANDLE_VALUE && !isElevated(explorerToken.handle)) explorerAble = true;
		//}
		//if (0 == _tcsicmp(pe.szExeFile, TEXT("ctfmon.exe")))
		//{
		//	Handle proc_ref = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
		//	Handle tok_parent;
		//	try_win32(OpenProcessToken(proc_ref.handle, TOKEN_DUPLICATE, &tok_parent.handle));
		//	try_win32(DuplicateTokenEx(tok_parent.handle, TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, NULL, SecurityAnonymous, TokenPrimary, &ctfmonToken.handle));

		//	if (ctfmonToken.handle != INVALID_HANDLE_VALUE && !isElevated(ctfmonToken.handle)) ctfmonAble = true;
		//}

		if (0 == _tcsicmp(pe.szExeFile, TEXT("winlogon.exe")))
		{
			// 打开目标进程，并获取其令牌
			Handle proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
			Handle tok;
			try_win32(OpenProcessToken(proc.handle, TOKEN_QUERY | TOKEN_DUPLICATE, &tok.handle));
			DWORD ses;
			if (!GetTokenInformation(tok.handle, TokenSessionId, &ses, sizeof(ses), &ret_len) || ses != ses_self) continue;

			// 执行令牌复制
			try_win32(DuplicateTokenEx(tok.handle, TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES, NULL, SecurityImpersonation, TokenImpersonation, &winlogonToken.handle));
		}
	}
	//cerr << "step4" << endl;
	{
		//if (!useAdmin && isElevated(inkeysToken.handle))
		//{
		//	if (explorerAble) inkeysToken = explorerToken;
		//	else if (ctfmonAble) inkeysToken = ctfmonToken;
		//}
		//if (inkeysToken.handle == INVALID_HANDLE_VALUE)
		//{
		//	if (explorerAble) inkeysToken = explorerToken;
		//	else if (ctfmonAble) inkeysToken = ctfmonToken;
		//}
	}
	//cerr << "step5" << endl;
	{
		// 设置令牌权限
		TOKEN_PRIVILEGES tkp = {};
		tkp.PrivilegeCount = 1;
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		try_win32(LookupPrivilegeValueW(NULL, SE_ASSIGNPRIMARYTOKEN_NAME, &tkp.Privileges[0].Luid));  // 查找权限
		try_win32(AdjustTokenPrivileges(winlogonToken.handle, FALSE, &tkp, sizeof(tkp), NULL, NULL));  // 调整权限
	}
	//cerr << "step6" << endl;

	// 设置线程令牌（好戏开始了）
	try_win32(SetThreadToken(NULL, winlogonToken.handle));

	if (useUiAccess && !hasUiAccess(inkeysToken.handle))
	{
		BOOL ui_access = TRUE;
		try_win32(SetTokenInformation(inkeysToken.handle, TokenUIAccess, &ui_access, sizeof(ui_access)));  // 设置 UIAccess 访问权限
	}

	//cerr << "step7" << endl;
	// 等待原先 智绘教 退出
	for (int i = 1; i <= 30; i++)
	{
		this_thread::sleep_for(chrono::milliseconds(50));
		if (!filesystem::exists(globalPath + L"superTop_wait.signal")) break;
	}

	// 启动智绘教
	{
		wstring param = L"\"" + GetCurrentExePath() + L"\" -SuperTopC " + inkeysCmdLine;
		vector<wchar_t> buffer(param.begin(), param.end());
		buffer.push_back(L'\0');

		//wcerr << L"open " + param << endl;
		//Testw(L"open " + param);
		//Testb(isElevated(inkeysToken.handle));

		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		GetStartupInfoW(&si);
		CreateProcessAsUserW(inkeysToken.handle, NULL, buffer.data(), NULL, NULL, false, DETACHED_PROCESS, NULL, NULL, &si, &pi);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}

	// 恢复到原始用户上下文
	try_win32(RevertToSelf());
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