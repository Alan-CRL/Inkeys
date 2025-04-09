#include "CrashHandler.h"
#include <tchar.h>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <atomic>
#include <string>
#include <vector>
#include <system_error>

namespace fs = std::filesystem;

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

	OutputDebugStringW((L"CrashHandler: 准备生成 Minidump 文件: " + dumpFilePath.wstring() + L"\n").c_str());

	bool dumpGenerated = GenerateMiniDump(pExceptionInfo, dumpFilePath);

	if (dumpGenerated) {
		OutputDebugStringW((L"CrashHandler: Minidump 已成功生成: " + dumpFilePath.wstring() + L"\n").c_str());
	}
	else {
		OutputDebugStringW((L"CrashHandler: 生成 Minidump 文件失败: " + dumpFilePath.wstring() + L"\n").c_str());
	}

	OutputDebugStringW(L"--- CrashHandler: 处理结束 ---\n");

	if (currentUserStateFlag == 0)
	{
		MessageBoxW(NULL, L"There is a problem with 智绘教Inkeys, click OK to restart 智绘教Inkeys to try to resolve the problem.\n智绘教Inkeys 出现问题，点击确定重启 智绘教Inkeys 以尝试解决问题。", L"Inkeys Error | 智绘教错误", MB_OK | MB_ICONERROR);
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
		wchar_t errorMsg[100];
		_snwprintf_s(errorMsg, _countof(errorMsg), _TRUNCATE, L"CrashHandler: MiniDumpWriteDump 失败 (错误 %lu)\n", GetLastError());
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
		return false;
	}

	return true;
}