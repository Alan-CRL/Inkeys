module;

#include "../../IdtMain.h"
#include "../../IdtI18n.h"
#include "../../IdtI18nKeys.g.h"

#include <dbghelp.h>

#include <sstream>
#include <algorithm>
#pragma comment(lib, "DbgHelp.lib")

namespace fs = std::filesystem;

#ifdef MessageBox
#undef MessageBox
#endif

module Inkeys.Helper.CrashHandler;

import Inkeys.UI.MessageBox;

// 静态成员初始化
LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::PreviousFilter = nullptr;
std::atomic<bool> g_isGeneratingDump = false;
std::atomic<int> CrashHandler::currentUserStateFlag = 0;
std::atomic<bool> CrashHandler::currentUserIsSecond = false;

// 初始化崩溃处理器
void CrashHandler::Initialize()
{
	PreviousFilter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);
	_set_invalid_parameter_handler(nullptr);
	_set_purecall_handler(nullptr);
}

// 设置用户标识
void CrashHandler::SetFlag(int initialState)
{
	currentUserStateFlag.store(initialState);
}
void CrashHandler::IsSecond(bool initialState)
{
	currentUserIsSecond.store(initialState);
}

// （可选）关闭/恢复
void CrashHandler::Shutdown()
{
	if (PreviousFilter) {
		SetUnhandledExceptionFilter(PreviousFilter);
		PreviousFilter = nullptr;
	}
}

// 获取可执行文件目录
fs::path CrashHandler::GetExeDirectory()
{
	std::vector<wchar_t> buffer(MAX_PATH);
	DWORD size = GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
	while (size == buffer.size()) { // 缓冲区太小，需要扩大
		buffer.resize(buffer.size() * 2);
		size = GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
	}

	if (size > 0 && size < buffer.size()) {
		fs::path exePath(buffer.data());
		return exePath; // 返回包含可执行文件的目录
	}
	else {
		OutputDebugStringW(L"CrashHandler: 无法获取模块文件名以确定根目录。\n");
		return fs::path(); // 返回空路径表示失败
	}
}

static bool IsDiskFullError(DWORD err)
{
	return err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL;
}

static void CleanupOldCrashFiles(const fs::path& crashDir, size_t keepPairs)
{
	try {
		if (!fs::exists(crashDir) || !fs::is_directory(crashDir)) return;

		struct CrashPair {
			fs::path dmp;
			fs::path txt;
			fs::file_time_type t;
		};
		std::vector<CrashPair> pairs;
		std::map<std::wstring, fs::path> txtByStem;

		for (const auto& entry : fs::directory_iterator(crashDir)) {
			if (!entry.is_regular_file()) continue;
			auto p = entry.path();
			auto ext = p.extension().wstring();
			std::wstring stem = p.stem().wstring();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
			if (ext == L".txt") {
				txtByStem[stem] = p;
			}
		}

		for (const auto& entry : fs::directory_iterator(crashDir)) {
			if (!entry.is_regular_file()) continue;
			auto p = entry.path();
			auto ext = p.extension().wstring();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
			if (ext != L".dmp") continue;

			CrashPair cp;
			cp.dmp = p;
			cp.t = fs::last_write_time(p);

			std::wstring stem = p.stem().wstring();
			auto it = txtByStem.find(stem);
			if (it != txtByStem.end()) cp.txt = it->second;

			pairs.push_back(cp);
		}

		if (pairs.size() <= keepPairs) return;

		std::sort(pairs.begin(), pairs.end(), [](const CrashPair& a, const CrashPair& b) {
			return a.t < b.t;
			});

		size_t needDelete = pairs.size() - keepPairs;
		for (size_t i = 0; i < needDelete; i++) {
			std::error_code ec;
			if (!pairs[i].dmp.empty()) fs::remove(pairs[i].dmp, ec);
			if (!pairs[i].txt.empty()) fs::remove(pairs[i].txt, ec);
		}
	}
	catch (...) {
		// best-effort cleanup only
	}
}

static std::string WideToUtf8(const std::wstring& ws)
{
	if (ws.empty()) return std::string();
	int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
	if (len <= 0) return std::string();
	std::string out;
	out.resize((size_t)len);
	WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len, nullptr, nullptr);
	return out;
}

static void AppendLine(std::ostringstream& oss, const std::string& s)
{
	oss << s << "\r\n";
}

static bool WriteCrashReportTxt(EXCEPTION_POINTERS* pExceptionInfo, const fs::path& txtFilePath, const fs::path& dumpFilePath, bool dumpGenerated, DWORD dumpLastError)
{
	std::ostringstream oss;

	AppendLine(oss, "Inkeys Crash Report");
	AppendLine(oss, "==================");

	// Timestamp
	time_t now = time(nullptr);
	struct tm timeinfo;
	localtime_s(&timeinfo, &now);
	char timebuf[64] = { 0 };
	strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
	AppendLine(oss, std::string("Time: ") + timebuf);

	AppendLine(oss, "ProcessId: " + std::to_string(GetCurrentProcessId()));
	AppendLine(oss, "ThreadId: " + std::to_string(GetCurrentThreadId()));

	// Dump status
	AppendLine(oss, "DumpPath: " + WideToUtf8(dumpFilePath.wstring()));
	AppendLine(oss, std::string("DumpGenerated: ") + (dumpGenerated ? "true" : "false"));
	if (!dumpGenerated) {
		AppendLine(oss, "DumpLastError: " + std::to_string(dumpLastError));
	}

	if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) {
		AppendLine(oss, "Exception: (no exception record)");
	}
	else {
		auto* er = pExceptionInfo->ExceptionRecord;
		char buf[128];

		sprintf_s(buf, "ExceptionCode: 0x%08lX", er->ExceptionCode);
		AppendLine(oss, buf);

		sprintf_s(buf, "ExceptionFlags: 0x%08lX", er->ExceptionFlags);
		AppendLine(oss, buf);

		sprintf_s(buf, "ExceptionAddress: 0x%p", er->ExceptionAddress);
		AppendLine(oss, buf);

		AppendLine(oss, "NumberParameters: " + std::to_string((unsigned)er->NumberParameters));
		for (ULONG i = 0; i < er->NumberParameters; i++) {
			sprintf_s(buf, "  Param[%lu]: 0x%p", i, (void*)er->ExceptionInformation[i]);
			AppendLine(oss, buf);
		}
	}

	AppendLine(oss, "");
	AppendLine(oss, "StackTrace");
	AppendLine(oss, "----------");

	// Initialize symbol handler in best-effort mode (no PDB required)
	HANDLE hProcess = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_PUBLICS_ONLY);

	SymInitialize(hProcess, NULL, TRUE);

	CONTEXT ctx = {};
	if (pExceptionInfo && pExceptionInfo->ContextRecord) {
		ctx = *pExceptionInfo->ContextRecord;
	}
	else {
		RtlCaptureContext(&ctx);
	}

	STACKFRAME64 frame = {};
	DWORD machineType = 0;

#if defined(_M_X64)
	machineType = IMAGE_FILE_MACHINE_AMD64;
	frame.AddrPC.Offset = ctx.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
	machineType = IMAGE_FILE_MACHINE_I386;
	frame.AddrPC.Offset = ctx.Eip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Ebp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Esp;
	frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
	machineType = IMAGE_FILE_MACHINE_ARM64;
	frame.AddrPC.Offset = ctx.Pc;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Fp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Sp;
	frame.AddrStack.Mode = AddrModeFlat;
#else
	machineType = 0;
#endif

	const int kMaxFrames = 64;
	for (int i = 0; i < kMaxFrames && machineType != 0; i++) {
		BOOL ok = StackWalk64(
			machineType,
			hProcess,
			GetCurrentThread(),
			&frame,
			&ctx,
			NULL,
			SymFunctionTableAccess64,
			SymGetModuleBase64,
			NULL
		);

		if (!ok || frame.AddrPC.Offset == 0) break;

		DWORD64 addr = frame.AddrPC.Offset;

		// Module + RVA
		std::string modName = "unknown";
		DWORD64 base = SymGetModuleBase64(hProcess, addr);
		if (base) {
			HMODULE hMod = NULL;
			wchar_t modPath[MAX_PATH] = { 0 };
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)addr, &hMod) && hMod) {
				GetModuleFileNameW(hMod, modPath, _countof(modPath));
				fs::path mp(modPath);
				modName = WideToUtf8(mp.filename().wstring());
			}
		}

		std::ostringstream line;
		line << "#" << i << " ";
		line << "0x" << std::hex << addr;

		if (base) {
			line << " " << modName << "+0x" << std::hex << (addr - base);
		}

		// Symbol name (best-effort; may come from exports even without PDB)
		char symBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)] = { 0 };
		auto* sym = (SYMBOL_INFO*)symBuffer;
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = MAX_SYM_NAME;

		DWORD64 disp = 0;
		if (SymFromAddr(hProcess, addr, &disp, sym)) {
			line << " " << sym->Name;
			if (disp) line << "+0x" << std::hex << disp;
		}

		AppendLine(oss, line.str());
	}

	SymCleanup(hProcess);

	// Write file (UTF-8 with BOM)
	HANDLE hFile = CreateFileW(txtFilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}

	const unsigned char bom[] = { 0xEF,0xBB,0xBF };
	DWORD written = 0;
	WriteFile(hFile, bom, (DWORD)sizeof(bom), &written, NULL);

	std::string content = oss.str();
	WriteFile(hFile, content.data(), (DWORD)content.size(), &written, NULL);

	FlushFileBuffers(hFile);
	CloseHandle(hFile);

	return true;
}

// 核心：Windows 回调的异常处理函数
LONG WINAPI CrashHandler::UnhandledExceptionHandler(EXCEPTION_POINTERS* pExceptionInfo)
{
	// 表明启动过程中遇到了错误，防止重复循环
	if (currentUserIsSecond) return EXCEPTION_EXECUTE_HANDLER;

	bool expected = false;
	if (!g_isGeneratingDump.compare_exchange_strong(expected, true)) {
		OutputDebugStringW(L"!!! CrashHandler: 重入异常处理器，放弃处理后续异常 !!!\n");
		return EXCEPTION_CONTINUE_SEARCH;
	}

	OutputDebugStringW(L"--- CrashHandler: 检测到未处理异常 ---\n");

	// --- 确定基础路径和文件名 ---
	fs::path exeDir = GetExeDirectory();
	fs::path rootDir = exeDir.parent_path();
	if (rootDir.empty()) {
		OutputDebugStringW(L"CrashHandler: 无法确定程序根目录，将尝试使用当前工作目录。\n");
		try {
			rootDir = fs::current_path();
		}
		catch (const fs::filesystem_error& e) {
			wchar_t errorMsg[256];
			_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE, L"CrashHandler: 无法获取当前工作目录: %hs\n", e.what());
			OutputDebugStringW(errorMsg);
			// 极端情况，无法确定任何目录，后续文件操作会失败
			g_isGeneratingDump = false;
			return EXCEPTION_CONTINUE_SEARCH; // 无法继续
		}
	}

	fs::path crashDir = rootDir / L"Inkeys" / L"Crash"; // 定义 Crash 文件夹路径

	// 尝试创建 crash 文件夹 (create_directories 会创建所有不存在的父目录)
	try {
		if (!fs::exists(crashDir)) {
			fs::create_directories(crashDir);
			OutputDebugStringW((L"CrashHandler: 已创建 Crash 目录: " + crashDir.wstring() + L"\n").c_str());
		}
	}
	catch (const fs::filesystem_error& e) {
		wchar_t errorMsg[MAX_PATH + 100];
		// 注意：e.what() 返回的是 char*，需要转换或直接用 %hs
		_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE,
			L"CrashHandler: 无法创建 Crash 目录 '%s' (错误: %hs). 文件将尝试保存在根目录。\n",
			crashDir.wstring().c_str(), e.what());
		OutputDebugStringW(errorMsg);
		crashDir = rootDir; // 退回到根目录
	}

	// 生成基于时间戳的文件名 (例如: 20231027_153000_PID)
	time_t now = time(nullptr);
	struct tm timeinfo;
	localtime_s(&timeinfo, &now);
	wchar_t timestamp[100];
	wcsftime(timestamp, _countof(timestamp), L"%Y%m%d_%H%M%S", &timeinfo);

	DWORD processId = GetCurrentProcessId();
	wchar_t baseFilename[150];
	_snwprintf_s(baseFilename, _countof(baseFilename), _TRUNCATE, L"%s_%lu", timestamp, processId);

	fs::path baseFilePath = crashDir / baseFilename; // 基础文件路径（无扩展名）

	// --- 生成 Minidump 文件 (.dmp) ---
	fs::path dumpFilePath = baseFilePath;
	dumpFilePath.replace_extension(L".dmp");
	// --- 生成 Crash Report 文件 (.txt) ---
	fs::path txtFilePath = baseFilePath;
	txtFilePath.replace_extension(L".txt");

	OutputDebugStringW((L"CrashHandler: 准备生成 Minidump 文件: " + dumpFilePath.wstring() + L"\n").c_str());

	bool dumpGenerated = GenerateMiniDump(pExceptionInfo, dumpFilePath);
	DWORD dumpLastError = dumpGenerated ? 0 : GetLastError();

	if (!dumpGenerated && IsDiskFullError(dumpLastError)) {
		CleanupOldCrashFiles(crashDir, 0);
		dumpGenerated = GenerateMiniDump(pExceptionInfo, dumpFilePath);
		dumpLastError = dumpGenerated ? 0 : GetLastError();
	}

	if (dumpGenerated) {
		OutputDebugStringW((L"CrashHandler: Minidump 已成功生成: " + dumpFilePath.wstring() + L"\n").c_str());
	}
	else {
		OutputDebugStringW((L"CrashHandler: 生成 Minidump 文件失败: " + dumpFilePath.wstring() + L"\n").c_str());
	}

	OutputDebugStringW((L"CrashHandler: 准备生成 Crash Report 文件: " + txtFilePath.wstring() + L"\n").c_str());

	bool txtGenerated = WriteCrashReportTxt(pExceptionInfo, txtFilePath, dumpFilePath, dumpGenerated, dumpLastError);
	DWORD txtLastError = txtGenerated ? 0 : GetLastError();

	if (!txtGenerated && IsDiskFullError(txtLastError)) {
		CleanupOldCrashFiles(crashDir, 0);
		txtGenerated = WriteCrashReportTxt(pExceptionInfo, txtFilePath, dumpFilePath, dumpGenerated, dumpLastError);
		txtLastError = txtGenerated ? 0 : GetLastError();
	}

	if (txtGenerated) {
		OutputDebugStringW((L"CrashHandler: Crash Report 已成功生成: " + txtFilePath.wstring() + L"\n").c_str());
	}
	else {
		wchar_t errorMsg[MAX_PATH + 100];
		_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE, L"CrashHandler: 生成 Crash Report 文件失败: %s (错误 %lu)\n", txtFilePath.wstring().c_str(), txtLastError);
		OutputDebugStringW(errorMsg);
	}

	OutputDebugStringW(L"--- CrashHandler: 处理结束 ---\n");

	if (currentUserStateFlag == 0)
	{
		wstring title = L"Inkeys Error";
		wstring body = L"Inkeys encountered a problem. Select OK to restart Inkeys and try to recover.";
		wstring okLabel = L"OK";
		LANGID language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
		wstring localizedTitle;
		wstring localizedBody;
		wstring localizedOk;
		LANGID localizedLanguage = language;
		// 崩溃路径仅在 i18n 读锁可立即获取时整组切换，否则保持完整英文回退。
		if (I18n::tryLanguageId(localizedLanguage)
			&& I18n::tryGetW(I18nKey.Dialogs.Common.ErrorTitle,
				localizedTitle)
			&& I18n::tryGetW(I18nKey.Dialogs.Crash.Body, localizedBody)
			&& I18n::tryGetW(I18nKey.Dialogs.Common.OK, localizedOk))
		{
			title = move(localizedTitle);
			body = move(localizedBody);
			okLabel = move(localizedOk);
			language = localizedLanguage;
		}
		auto request = Inkeys::UI::MessageBox::MakeOkRequest(
			title.c_str(), body.c_str());
		request.language = language;
		request.labels.ok = okLabel.c_str();
		request.icon = Inkeys::UI::MessageBox::IconSource::BuiltInError();
		request.ownerlessTopmostAtCreation = true;
		request.reliability =
			Inkeys::UI::MessageBox::Reliability::CriticalNoWait;
		request.fallback.icon = Inkeys::UI::MessageBox::SystemIcon::Error;
		if (Inkeys::UI::MessageBox::Show(request)
			== Inkeys::UI::MessageBox::Result::Ok)
			ShellExecuteW(NULL, NULL, exeDir.wstring().c_str(), L"-CrashTry", NULL, SW_SHOWNORMAL);
	}
	else if (currentUserStateFlag == 1) ShellExecuteW(NULL, NULL, exeDir.wstring().c_str(), L"-CrashTry", NULL, SW_SHOWNORMAL);

	g_isGeneratingDump = false; // 重置标志

	// EXCEPTION_EXECUTE_HANDLER: 表示“我处理了异常”，阻止系统默认的错误报告对话框（例如 "xxx 已停止工作"）出现，然后通常进程会终止。
	// EXCEPTION_CONTINUE_SEARCH: 表示“我没处理（或处理了一部分），让系统继续查找其他处理器”（例如 JIT 调试器或 Windows 错误报告）。
	// EXCEPTION_CONTINUE_EXECUTION: (极其危险，不推荐) 尝试从异常发生点恢复执行，除非你非常清楚你在做什么并且异常是可恢复的，否则不要用。
	if (currentUserStateFlag == 3) return EXCEPTION_CONTINUE_SEARCH;
	return EXCEPTION_EXECUTE_HANDLER;
}

// 辅助函数：生成 Minidump 文件
bool CrashHandler::GenerateMiniDump(EXCEPTION_POINTERS* pExceptionInfo, const fs::path& dumpFilePath) {
	// --- 创建文件句柄 ---
	// 使用 fs::path 的 c_str() 获取宽字符路径
	HANDLE hFile = CreateFileW(
		dumpFilePath.c_str(),          // 文件路径 (宽字符)
		GENERIC_WRITE,                 // 写入权限
		0,                             // 不共享写入
		NULL,                          // 默认安全属性
		CREATE_ALWAYS,                 // 总是创建新文件
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // 普通文件，尝试立即写入磁盘
		NULL);                         // 无模板文件

	if (hFile == INVALID_HANDLE_VALUE) {
		wchar_t errorMsg[MAX_PATH + 100];
		_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE, L"CrashHandler: 无法创建 Dump 文件 '%s' (错误 %lu)\n", dumpFilePath.wstring().c_str(), GetLastError());
		OutputDebugStringW(errorMsg);
		return false;
	}

	// --- 准备 MiniDump 参数 ---
	MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
	exceptionInfo.ThreadId = GetCurrentThreadId();
	exceptionInfo.ExceptionPointers = pExceptionInfo;
	exceptionInfo.ClientPointers = TRUE;

	// 选择 Dump 类型
	MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(MiniDumpNormal |
		MiniDumpWithProcessThreadData |
		MiniDumpWithDataSegs |
		MiniDumpWithHandleData |
		MiniDumpWithUnloadedModules |
		MiniDumpWithThreadInfo
		| MiniDumpWithPrivateReadWriteMemory);

	// --- 写入 Dump 文件 ---
	// MiniDumpWriteDump 函数本身是 ANSI/Unicode 中性的，参数决定行为
	BOOL success = MiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		hFile,
		dumpType,
		&exceptionInfo,
		NULL,
		NULL
	);

	// --- 清理 ---
	FlushFileBuffers(hFile); // 确保数据刷盘
	CloseHandle(hFile);

	if (!success) {
		DWORD lastErr = GetLastError();
		wchar_t errorMsg[100];
		_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE, L"CrashHandler: MiniDumpWriteDump 失败 (错误 %lu)\n", lastErr);
		OutputDebugStringW(errorMsg);
		// 尝试删除可能不完整的 dump 文件
		try {
			fs::remove(dumpFilePath);
		}
		catch (const fs::filesystem_error& e) {
			wchar_t deleteErrorMsg[MAX_PATH + 100];
			_snwprintf_s(deleteErrorMsg, _countof(deleteErrorMsg), _TRUNCATE, L"CrashHandler: 删除失败的 dump 文件 '%s' 时出错: %hs\n", dumpFilePath.wstring().c_str(), e.what());
			OutputDebugStringW(deleteErrorMsg);
		}
		SetLastError(lastErr);
		return false;
	}

	return true;
}
