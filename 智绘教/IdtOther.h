#pragma once
#include "IdtMain.h"
#include "IdtText.h"

wstring GetCurrentExeDirectory();
wstring GetCurrentExePath();
wstring GetCurrentExeName();

bool checkIsNetwork();

bool isValidString(const wstring& str);
bool isAsciiPrintable(const wstring& input);

bool isProcessRunning(const std::wstring& processPath);
int ProcessRunningCnt(const std::wstring& processPath);

bool SetStartupState(bool bAutoRun, wstring path, const wstring& nameclass);
bool QueryStartupState(wstring path, const wstring& nameclass);