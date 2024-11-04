#pragma once
#include "IdtMain.h"
#include "IdtText.h"

wstring GetCurrentExeDirectory();
wstring GetCurrentExePath();
wstring GetCurrentExeName();

bool checkIsNetwork();
bool ExtractResource(LPCTSTR strDstFile, LPCTSTR strResType, LPCTSTR strResName);

bool isValidString(const wstring& str);
bool isAsciiPrintable(const wstring& input);

bool IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory);
bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath);
void SetShortcut();

bool isProcessRunning(const std::wstring& processPath);
int ProcessRunningCnt(const std::wstring& processPath);