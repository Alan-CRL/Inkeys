#pragma once
#include "IdtMain.h"
#include "IdtText.h"

wstring GetCurrentExeDirectory();
wstring GetCurrentExePath();
wstring GetCurrentExeName();

//网络状态获取
bool checkIsNetwork();
// 提取指定模块中的资源文件
bool ExtractResource(LPCTSTR strDstFile, LPCTSTR strResType, LPCTSTR strResName);

//判断id是否错乱
bool isValidString(const wstring& str);

//快捷方式判断
bool IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory);
bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath);
void SetShortcut();

// 程序进程状态获取
bool isProcessRunning(const std::wstring& processPath);
// 进程程序路径查询
int ProcessRunningCnt(const std::wstring& processPath);

// 路径权限检测
bool HasReadWriteAccess(const std::wstring& directoryPath);