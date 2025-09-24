#pragma once
#include "IdtMain.h"

#include <tlhelp32.h> // 提供进程、模块、线程的遍历等功能

extern HWND floating_window; //悬浮窗窗口
extern HWND drawpad_window; //画板窗口
extern HWND ppt_window; //PPT控件窗口
extern HWND freeze_window; //定格背景窗口
extern HWND setting_window; //程序调测窗口

extern bool FreezePPT;
extern HWND ppt_show;
extern wstring ppt_title, ppt_software;
extern map<wstring, bool> ppt_title_recond;

HWND GetLastFocusWindow();
wstring GetWindowText(HWND hWnd);

struct IdtWindowsIsVisibleStruct
{
	IdtWindowsIsVisibleStruct()
	{
		floatingWindow = false;
		drawpadWindow = false;
		pptWindow = false;
		freezeWindow = false;

		allCompleted = false;
	}

	bool floatingWindow;
	bool drawpadWindow;
	bool pptWindow;
	bool freezeWindow;

	bool allCompleted;
};
extern IdtWindowsIsVisibleStruct IdtWindowsIsVisible;
extern bool rtsWait;
extern bool topWindowNow;

//置顶程序窗口
void TopWindow();