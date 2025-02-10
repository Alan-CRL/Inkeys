#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <stdexcept>

#define try_win32(x) if(!(x)) [[unlikely]] throw_win32_error();

void throw_win32_error() {
	DWORD err = GetLastError();
	LPSTR msg = nullptr;

	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, err, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL);
	MessageBoxA(NULL, msg, NULL, 0);

	throw std::runtime_error(msg);
}

class Handle {
public:
	HANDLE handle = INVALID_HANDLE_VALUE;

	Handle() {}
	Handle(HANDLE handle) :handle(handle) {
		try_win32(handle != INVALID_HANDLE_VALUE);
	}
	~Handle() {
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
	}
};

bool is_elevated(HANDLE tok) {
	DWORD  ret_len;
	TOKEN_ELEVATION_TYPE elevation;
	try_win32(GetTokenInformation(tok, TokenElevationType, &elevation, sizeof(elevation), &ret_len));
	return elevation == TokenElevationTypeFull;
}

bool elevate() {
	TCHAR path_self[MAX_PATH];
	try_win32(GetModuleFileName(NULL, path_self, MAX_PATH));
	TCHAR param[256];
	_stprintf_s(param, 256, TEXT("%x"), GetCurrentProcessId());
	SHELLEXECUTEINFO sei = { .cbSize = sizeof(SHELLEXECUTEINFO), .fMask = SEE_MASK_NOASYNC,.lpVerb = TEXT("runas") ,.lpFile = path_self,.lpParameters = param,.nShow = SW_SHOWDEFAULT };
	return !!ShellExecuteEx(&sei);
}

bool has_ui_access(HANDLE tok) {
	DWORD  ret_len;
	BOOL ui_access;
	try_win32(GetTokenInformation(tok, TokenUIAccess, &ui_access, sizeof(ui_access), &ret_len));
	return ui_access;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpCmdLine, _In_ int nShowCmd) {
	HANDLE proc_self = GetCurrentProcess();
	Handle tok_self;
	try_win32(OpenProcessToken(proc_self, TOKEN_ALL_ACCESS, &tok_self.handle));

	if (has_ui_access(tok_self.handle)) {
		WNDCLASS wc = {};
		wc.lpfnWndProc = WindowProc;
		wc.hInstance = GetModuleHandle(NULL);
		wc.lpszClassName = TEXT("TopmostWindowClass");

		if (!RegisterClass(&wc))
			return -1;
		HWND hwnd = CreateWindowEx(
			WS_EX_TOPMOST,
			wc.lpszClassName,
			TEXT("Topmost Window"),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 500, 500,
			NULL, NULL, wc.hInstance, NULL);

		if (hwnd == NULL)
			return -1;

		ShowWindow(hwnd, SW_SHOWNORMAL);
		UpdateWindow(hwnd);
		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		return 0;
	}
	if (!is_elevated(tok_self.handle)) {
		return elevate() ? 0 : -1;
	}

	DWORD ses_self, ret_len;
	try_win32(GetTokenInformation(tok_self.handle, TokenSessionId, &ses_self, sizeof(ses_self), &ret_len));

	DWORD pid_ref = _tcstoul(lpCmdLine, NULL, 16);
	Handle snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	Handle tok_stolen;
	PROCESSENTRY32 pe = { .dwSize = sizeof(PROCESSENTRY32) };
	for (BOOL cont = Process32First(snapshot.handle, &pe); cont; cont = Process32Next(snapshot.handle, &pe)) {
		if (pid_ref == 0 && 0 == _tcsicmp(pe.szExeFile, TEXT("explorer.exe")))
			pid_ref = pe.th32ProcessID;
		if (0 != _tcsicmp(pe.szExeFile, TEXT("winlogon.exe")))
			continue;

		Handle proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);

		Handle tok;
		try_win32(OpenProcessToken(proc.handle, TOKEN_QUERY | TOKEN_DUPLICATE, &tok.handle));

		DWORD ses;
		if (!GetTokenInformation(tok.handle, TokenSessionId, &ses, sizeof(ses), &ret_len) || ses != ses_self) continue;

		try_win32(DuplicateTokenEx(tok.handle, TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES, NULL, SecurityImpersonation, TokenImpersonation, &tok_stolen.handle));
	}

	if (pid_ref == 0)
		throw std::runtime_error("failed to choose ref process");

	TOKEN_PRIVILEGES tkp = {};
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	try_win32(LookupPrivilegeValue(NULL, SE_ASSIGNPRIMARYTOKEN_NAME, &tkp.Privileges[0].Luid));
	try_win32(AdjustTokenPrivileges(tok_stolen.handle, FALSE, &tkp, sizeof(tkp), NULL, NULL));

	try_win32(SetThreadToken(NULL, tok_stolen.handle));

	Handle proc_ref = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid_ref);

	Handle tok_parent;
	try_win32(OpenProcessToken(proc_ref.handle, TOKEN_DUPLICATE, &tok_parent.handle));

	Handle tok_new;
	try_win32(DuplicateTokenEx(tok_parent.handle, TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, NULL, SecurityAnonymous, TokenPrimary, &tok_new.handle));

	BOOL ui_access = TRUE;
	try_win32(SetTokenInformation(tok_new.handle, TokenUIAccess, &ui_access, sizeof(ui_access)));

	TCHAR path_self[MAX_PATH];
	try_win32(GetModuleFileName(NULL, path_self, MAX_PATH));

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	GetStartupInfo(&si);
	try_win32(CreateProcessAsUser(tok_new.handle, path_self, NULL, NULL, NULL, false, 0, NULL, NULL, &si, &pi));
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	try_win32(RevertToSelf());

	return 0;
}