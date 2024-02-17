#pragma once
#include "IdtMain.h"

#include "IdtDrawpad.h"

#include <tlhelp32.h> // 提供进程、模块、线程的遍历等功能

extern HWND floating_window; //悬浮窗窗口
extern HWND drawpad_window; //画板窗口
extern HWND ppt_window; //PPT控件窗口
extern HWND setting_window; //程序调测窗口
extern HWND freeze_window; //定格背景窗口

extern bool FreezePPT;
extern HWND ppt_show;
extern wstring ppt_title;
extern map<wstring, bool> ppt_title_recond;

//窗口是否置顶
bool IsWindowFocused(HWND hWnd);
//程序进程状态获取
bool isProcessRunning(const std::wstring& processPath);
HWND GetLastFocusWindow();
wstring GetWindowText(HWND hWnd);

//置顶程序窗口
void TopWindow();