#pragma once
#include "IdtMain.h"
#include "IdtText.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <io.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

wstring GetCurrentExeDirectory();
wstring GetCurrentExePath();
//开机启动项设置
bool ModifyRegedit(bool bAutoRun);
//网络状态获取
bool checkIsNetwork();
// 提取指定模块中的资源文件
bool ExtractResource(LPCTSTR strDstFile, LPCTSTR strResType, LPCTSTR strResName);

wstring GetCPUID();
wstring GetMotherboardUUID();
wstring GetMainHardDiskSerialNumber();
//判断硬盘序列号是否错乱
bool isValidString(const wstring& str);

//快捷方式判断
bool IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory);
bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath);
void SetShortcut();