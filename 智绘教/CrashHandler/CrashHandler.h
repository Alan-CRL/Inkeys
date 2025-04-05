#pragma once

#include <windows.h>
#include <dbghelp.h>
#include <filesystem>

#pragma comment(lib, "DbgHelp.lib")

class CrashHandler {
public:
	// 禁用构造函数和赋值操作
	CrashHandler() = delete;
	CrashHandler(const CrashHandler&) = delete;
	CrashHandler& operator=(const CrashHandler&) = delete;

	static void Initialize();

	static void SetFlag(int initialState = 0);
	static void IsSecond(bool initialState = false);

	static void Shutdown();

private:
	/**
	 * @brief 注册给 Windows 的顶层未处理异常过滤器函数。
	 * @param pExceptionInfo 包含异常信息的结构体指针。
	 * @return LONG 处理结果。
	 */
	static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* pExceptionInfo);

	/**
	 * @brief 生成 Minidump 文件。
	 * @param pExceptionInfo 包含异常信息的结构体指针。
	 * @param dumpFilePath 指定的 Minidump 文件完整路径。
	 * @return bool 是否成功生成 dump 文件。
	 */
	static bool GenerateMiniDump(EXCEPTION_POINTERS* pExceptionInfo, const std::filesystem::path& dumpFilePath);

	/**
	 * @brief 获取程序可执行文件所在的目录。
	 * @return std::filesystem::path 可执行文件目录路径，如果失败则返回空路径。
	 */
	static std::filesystem::path GetExeDirectory();

	// 保存原始的异常过滤器
	static LPTOP_LEVEL_EXCEPTION_FILTER PreviousFilter;

	// 用户定义的状态标志，使用原子变量以保证线程安全地设置和读取
	static std::atomic<int> currentUserStateFlag;
	static std::atomic<bool> currentUserIsSecond;
};
