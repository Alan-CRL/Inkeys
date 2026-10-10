module;

#include "../../IdtMain.h"
#include "../../IdtI18n.h"
#include "../../IdtI18nKeys.g.h"
#include "ShutdownSupervisor.h"

#include <dbghelp.h>

#include <sstream>
#include <algorithm>
#include <mutex>
#pragma comment(lib, "DbgHelp.lib")

namespace fs = std::filesystem;

#ifdef MessageBox
#undef MessageBox
#endif

module Inkeys.Helper.CrashHandler;

import Inkeys.Window;
import Inkeys.UI.MessageBox;

// 静态成员初始化
LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::PreviousFilter = nullptr;
namespace
{
	std::mutex g_filterRegistrationMutex;
	bool g_filterInstalled = false;
}
std::atomic<bool> g_isGeneratingDump = false;
std::atomic<ULONGLONG> g_secondCrashStartTick{ 0 };
std::atomic<DWORD> g_dumpPrimaryError{ 0 };
std::atomic<bool> g_dumpFallbackAttempted{ false };
std::atomic<CrashHandler::IsolatedUefTestMode> g_isolatedUefTestMode{
	CrashHandler::IsolatedUefTestMode::Disabled };
std::atomic_bool g_isolatedReportDiskFullInjected{ false };
namespace
{
	constexpr DWORD kCrashReportDeadlineMs = 15000;
	constexpr DWORD kCrashReportTimeoutExitCode = 0xE1430017;
	std::atomic<ULONGLONG> g_reportDeadlineTick{ 0 };
	std::atomic<HANDLE> g_reportReadyEvent{ nullptr };
	std::atomic<HANDLE> g_reportCancelEvent{ nullptr };

	DWORD WINAPI CrashReportDeadlineThread(void*) noexcept
	{
		const HANDLE ready = g_reportReadyEvent.load(std::memory_order_acquire);
		const HANDLE cancel = g_reportCancelEvent.load(std::memory_order_acquire);
		if (!ready || !cancel || !SetEvent(ready)) return ERROR_INVALID_HANDLE;
		const ULONGLONG deadline = g_reportDeadlineTick.load(std::memory_order_acquire);
		const ULONGLONG now = GetTickCount64();
		const DWORD remaining = deadline > now ? static_cast<DWORD>(deadline - now) : 0;
		// 线程只碰 Win32 对象；报告卡住时不依赖堆、日志或故障线程的锁。
		if (WaitForSingleObject(cancel, remaining) != WAIT_OBJECT_0)
			TerminateProcess(GetCurrentProcess(), kCrashReportTimeoutExitCode);
		return 0;
	}

	bool ArmCrashReportDeadline() noexcept
	{
		const HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!ready) return false;
		const HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!cancel) { CloseHandle(ready); return false; }
		g_reportReadyEvent.store(ready, std::memory_order_release);
		g_reportCancelEvent.store(cancel, std::memory_order_release);
		g_reportDeadlineTick.store(GetTickCount64() + kCrashReportDeadlineMs,
			std::memory_order_release);
		const HANDLE thread = CreateThread(nullptr, 0, CrashReportDeadlineThread,
			nullptr, 0, nullptr);
		if (!thread)
		{
			CloseHandle(ready);
			CloseHandle(cancel);
			g_reportReadyEvent.store(nullptr, std::memory_order_release);
			g_reportCancelEvent.store(nullptr, std::memory_order_release);
			return false;
		}
		const DWORD started = WaitForSingleObject(ready, 500);
		CloseHandle(thread);
		if (started == WAIT_OBJECT_0) return true;
		// loader 初始化阻止线程 ready 时，不执行可能无界的 dump；延迟线程醒来后看到取消。
		SetEvent(cancel);
		return false;
	}

	struct CrashReportDeadlineGuard
	{
		bool armed = false;
		void Cancel() noexcept
		{
			if (!armed) return;
			armed = false;
			const HANDLE cancel = g_reportCancelEvent.load(std::memory_order_acquire);
			if (cancel) SetEvent(cancel);
		}
		~CrashReportDeadlineGuard() { Cancel(); }
	};
}
std::atomic<int> CrashHandler::currentUserStateFlag = 0;
std::atomic<bool> CrashHandler::currentUserIsSecond = false;

// 初始化崩溃处理器
void CrashHandler::Initialize()
{
	std::scoped_lock lock(g_filterRegistrationMutex);
	if (g_filterInstalled) return;
	PreviousFilter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);
	g_filterInstalled = true;
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
	g_secondCrashStartTick.store(initialState ? GetTickCount64() : 0,
		std::memory_order_release);
	currentUserIsSecond.store(initialState);
}

void CrashHandler::SetIsolatedUefTestMode(IsolatedUefTestMode mode) noexcept
{
	g_isolatedReportDiskFullInjected.store(false, std::memory_order_release);
	g_isolatedUefTestMode.store(mode, std::memory_order_release);
}

// （可选）关闭/恢复
void CrashHandler::Shutdown()
{
	std::scoped_lock lock(g_filterRegistrationMutex);
	if (!g_filterInstalled) return;
	// 旧处理器为 nullptr 仍表示本处理器已安装，正常退出必须恢复该空值。
	SetUnhandledExceptionFilter(PreviousFilter);
	PreviousFilter = nullptr;
	g_filterInstalled = false;
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

static void CleanupOldCrashFiles(const fs::path& crashDir, size_t keepPairs,
	const fs::path& protectedDump = {})
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
			// 报告写盘失败时可清理旧 pair，但本轮已提交 dump 不能被删后仍报告成功。
			if (!protectedDump.empty() && CompareStringOrdinal(p.filename().c_str(), -1,
				protectedDump.filename().c_str(), -1, TRUE) == CSTR_EQUAL) continue;

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
	if (g_isolatedUefTestMode.load(std::memory_order_acquire)
		== CrashHandler::IsolatedUefTestMode::ReportDiskFullOnce
		&& !g_isolatedReportDiskFullInjected.exchange(true, std::memory_order_acq_rel))
	{
		// 仅已验真私有child：模拟报告第一次写入前磁盘满，复用真实清理/重试调用链。
		SetLastError(ERROR_DISK_FULL);
		return false;
	}
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
	AppendLine(oss, std::string("DumpFallbackAttempted: ") +
		(g_dumpFallbackAttempted.load(std::memory_order_acquire) ? "true" : "false"));
	AppendLine(oss, "DumpPrimaryError: " + std::to_string(
		g_dumpPrimaryError.load(std::memory_order_acquire)));
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

	// 完整写本轮 pending 后无替换发布；报告中途被强退不会留下假完整 .txt。
	fs::path pendingPath = txtFilePath;
	pendingPath.replace_extension(L".tx_"); // 与最终路径等长，避免 Win7 深目录额外越过 MAX_PATH。
	HANDLE hFile = CreateFileW(pendingPath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}
	const unsigned char bom[] = { 0xEF,0xBB,0xBF };
	DWORD written = 0;
	std::string content = oss.str();
	const BOOL bomWritten = WriteFile(hFile, bom, (DWORD)sizeof(bom), &written, NULL);
	bool success = bomWritten && written == sizeof(bom);
	DWORD writeError = success ? 0 : (bomWritten ? ERROR_WRITE_FAULT : GetLastError());
	if (success)
	{
		const BOOL contentWritten = WriteFile(hFile, content.data(),
			(DWORD)content.size(), &written, NULL);
		success = contentWritten && written == content.size();
		if (!success) writeError = contentWritten ? ERROR_WRITE_FAULT : GetLastError();
	}
	if (success && !FlushFileBuffers(hFile))
	{
		writeError = GetLastError();
		success = false;
	}
	if (!CloseHandle(hFile) && success)
	{
		writeError = GetLastError();
		success = false;
	}
	if (success && !MoveFileExW(pendingPath.c_str(), txtFilePath.c_str(),
		MOVEFILE_WRITE_THROUGH))
	{
		writeError = GetLastError();
		success = false;
	}
	if (!success)
	{
		DeleteFileW(pendingPath.c_str()); // 仅本轮临时报告；不触碰现有正式报告。
		SetLastError(writeError ? writeError : ERROR_WRITE_FAULT);
	}
	return success;
}

// 核心：Windows 回调的异常处理函数
LONG WINAPI CrashHandler::UnhandledExceptionHandler(EXCEPTION_POINTERS* pExceptionInfo)
{
	// 崩溃拉起后的五分钟内再崩溃，不再次拉起；首帧出现不能证明启动稳定。
	constexpr ULONGLONG kCrashRetrySuppressionMilliseconds = 5ULL * 60ULL * 1000ULL;
	const ULONGLONG secondStart = g_secondCrashStartTick.load(std::memory_order_acquire);
	if (currentUserIsSecond.load(std::memory_order_acquire) && secondStart != 0 &&
		GetTickCount64() - secondStart < kCrashRetrySuppressionMilliseconds)
		return EXCEPTION_EXECUTE_HANDLER;

	bool expected = false;
	if (!g_isGeneratingDump.compare_exchange_strong(expected, true)) {
		OutputDebugStringW(L"!!! CrashHandler: 重入异常处理器，放弃处理后续异常 !!!\n");
		return EXCEPTION_CONTINUE_SEARCH;
	}
	const int crashMode = currentUserStateFlag.load(std::memory_order_acquire);
	// 自动模式在报告前争意图；手动模式待确认后才争，不阻止另一线程正式退出。
	const bool earlyCrashIntent = Inkeys::Shutdown::TryClaimCrashRestartIntent(
		GetOffSignalInteropPointer(), crashMode, false);
	auto armCrashRestart = []() noexcept -> bool
	{
		// 仅成功抢到重启意图后关闭 HWND 显示门；手动提示尚未确认时不触碰窗口。
		Inkeys::Window::GetService().BeginShutdown();
		// 异常路径只建立低层进程监督，不进入业务退出、窗口或保存锁。
		const auto result = Inkeys::Shutdown::ArmShutdownSupervisor(
			Inkeys::Shutdown::Intent::CrashRestart);
		if (result == Inkeys::Shutdown::ArmResult::FallbackArmed)
		{
			OutputDebugStringW(L"CrashHandler: 重启 helper 未建立，仅保留本进程 15 秒强制结束；不会拉起新实例。\n");
			return true;
		}
		if (result != Inkeys::Shutdown::ArmResult::Armed)
		{
			const DWORD error = GetLastError();
			wchar_t message[128]{};
			_snwprintf_s(message, _countof(message), _TRUNCATE,
				L"CrashHandler: 自动重启监督未建立，错误码=%lu\n", error);
			OutputDebugStringW(message);
		}
		return result == Inkeys::Shutdown::ArmResult::Armed;
	};
	// 自动模式仅在自己赢得意图时由外部监督；CAS 输给受控退出但对方尚未 Arm 时，
	// 本线程仍须给报告阶段建立不拉起新实例的本地截止。
	bool automaticDeadlineReady = true;
	if (earlyCrashIntent && crashMode == 1)
		automaticDeadlineReady = armCrashRestart();
	CrashReportDeadlineGuard reportDeadline;
	const bool needsLocalReportDeadline = crashMode != 1 || !earlyCrashIntent;
	const bool reportAllowed = needsLocalReportDeadline
		? ArmCrashReportDeadline() : automaticDeadlineReady;
	reportDeadline.armed = needsLocalReportDeadline && reportAllowed;
	if (reportAllowed)
	{

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
			// 异常链已不可继续；保留一次性门闩，避免另一线程重复询问或拉起。
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
		CleanupOldCrashFiles(crashDir, 0, dumpFilePath);
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
	}
	else OutputDebugStringW(L"CrashHandler: 报告截止保护未建立，跳过可能无界的 dump/report。\n");
	// 用户确认框可以等待用户选择；报告阶段的自杀时钟必须先撤销。
	reportDeadline.Cancel();
	if (crashMode == 0 && g_isolatedUefTestMode.load(std::memory_order_acquire)
		== IsolatedUefTestMode::ManualHoldAfterReport)
	{
		// 仅已授权私有测试：模拟用户思考超过15秒，验证报告时钟已撤销且取消不重启。
		Sleep(16000);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	if (crashMode == 0 &&
		InterlockedCompareExchange(GetOffSignalInteropPointer(), 0, 0) == 0)
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
			== Inkeys::UI::MessageBox::Result::Ok
			&& Inkeys::Shutdown::TryClaimCrashRestartIntent(
				GetOffSignalInteropPointer(), crashMode, true))
			(void)armCrashRestart();
	}

	// 未处理异常后进程应终止；保持一次性门闩，避免旧进程退出前另一线程再次拉起实例。

	// EXCEPTION_EXECUTE_HANDLER: 表示“我处理了异常”，阻止系统默认的错误报告对话框（例如 "xxx 已停止工作"）出现，然后通常进程会终止。
	// EXCEPTION_CONTINUE_SEARCH: 表示“我没处理（或处理了一部分），让系统继续查找其他处理器”（例如 JIT 调试器或 Windows 错误报告）。
	// EXCEPTION_CONTINUE_EXECUTION: (极其危险，不推荐) 尝试从异常发生点恢复执行，除非你非常清楚你在做什么并且异常是可恢复的，否则不要用。
	if (crashMode == 3) return EXCEPTION_CONTINUE_SEARCH;
	return EXCEPTION_EXECUTE_HANDLER;
}

static bool CanRetryMinimalDump(DWORD error) noexcept
{
	// DbgHelp 的 GetLastError 是 HRESULT；只对内存不可读/标志不兼容降载，不掩盖磁盘和权限错误。
	return error == static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY))
		|| error == ERROR_PARTIAL_COPY
		|| error == static_cast<DWORD>(E_INVALIDARG)
		|| error == static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER));
}

// 辅助函数：生成 Minidump 文件
bool CrashHandler::GenerateMiniDump(EXCEPTION_POINTERS* pExceptionInfo, const fs::path& dumpFilePath) {
	g_dumpPrimaryError.store(0, std::memory_order_release);
	g_dumpFallbackAttempted.store(false, std::memory_order_release);
	// 本轮只写唯一 pending；完成后才发布最终 .dmp，强退时残片不会冒充有效文件。
	fs::path pendingPath = dumpFilePath;
	pendingPath.replace_extension(L".dm_"); // CREATE_NEW 碰到旧残片则失败闭合，不覆盖。
	HANDLE hFile = CreateFileW(
		pendingPath.c_str(),           // 本轮待提交文件
		GENERIC_READ | GENERIC_WRITE,  // 写入并校验实际长度
		0,                             // 不共享写入
		NULL,                          // 默认安全属性
		CREATE_NEW,                    // 不覆盖任何旧的 pending 或正式 dump
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // 普通文件，尝试立即写入磁盘
		NULL);                         // 无模板文件

	if (hFile == INVALID_HANDLE_VALUE) {
		const DWORD createError = GetLastError();
		wchar_t errorMsg[MAX_PATH + 100];
		_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE, L"CrashHandler: 无法创建 Dump pending 文件 '%s' (错误 %lu)\n", pendingPath.wstring().c_str(), createError);
		OutputDebugStringW(errorMsg);
		SetLastError(createError);
		return false;
	}
	if (g_isolatedUefTestMode.load(std::memory_order_acquire)
		== IsolatedUefTestMode::StallAfterDumpOpen)
	{
		// 只有已验明继承父 HANDLE 的私有测试进程能设置；故意模拟 DbgHelp 无界挂起。
		constexpr char partial[] = "PARTIAL";
		DWORD written = 0;
		WriteFile(hFile, partial, sizeof(partial) - 1, &written, nullptr);
		FlushFileBuffers(hFile);
		Sleep(INFINITE);
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

	// 高保真路径优先；失败后立即保存 DbgHelp 的 HRESULT，不能让 Flush/Close 覆盖首因。
	BOOL success = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
		hFile, dumpType, &exceptionInfo, nullptr, nullptr);
	DWORD lastErr = success ? 0 : GetLastError();
	if (!success)
	{
		g_dumpPrimaryError.store(lastErr, std::memory_order_release);
		if (CanRetryMinimalDump(lastErr))
		{
			g_dumpFallbackAttempted.store(true, std::memory_order_release);
			LARGE_INTEGER beginning{};
			if (SetFilePointerEx(hFile, beginning, nullptr, FILE_BEGIN) && SetEndOfFile(hFile))
			{
				// 仅当前高保真 dump 不可读时回退；MiniDumpNormal 仍保留异常和线程栈。
				success = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
					hFile, MiniDumpNormal, &exceptionInfo, nullptr, nullptr);
				lastErr = success ? 0 : GetLastError();
			}
			else lastErr = GetLastError();
		}
	}
	if (success)
	{
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(hFile, &size))
		{
			lastErr = GetLastError();
			success = FALSE;
		}
		else if (size.QuadPart <= 0)
		{
			lastErr = ERROR_INVALID_DATA;
			success = FALSE;
		}
		else if (!FlushFileBuffers(hFile))
		{
			lastErr = GetLastError();
			success = FALSE;
		}
	}
	if (!CloseHandle(hFile) && success)
	{
		lastErr = GetLastError();
		success = FALSE;
	}
	if (success && !MoveFileExW(pendingPath.c_str(), dumpFilePath.c_str(),
		MOVEFILE_WRITE_THROUGH))
	{
		lastErr = GetLastError();
		success = FALSE;
	}
	if (!success) {
		wchar_t errorMsg[100];
		_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE, L"CrashHandler: Minidump 写入失败 (错误 %lu)\n", lastErr);
		OutputDebugStringW(errorMsg);
		// 仅删除本轮 pending；已有同名最终 dump 不覆盖，也不扫描其他文件。
		if (!DeleteFileW(pendingPath.c_str()))
			OutputDebugStringW(L"CrashHandler: 失败 dump pending 未能删除，正式路径未发布。\n");
		SetLastError(lastErr);
		return false;
	}

	return true;
}
