#pragma once

#include "../Message/Message.Legacy.hpp"

#include <map>
#include <string>

extern HWND floating_window;
extern HWND drawpad_window;
extern HWND freeze_window;
extern HWND setting_window;

// PPT 业务状态暂留在联动层，窗口模块只负责 HWND 与线程生命周期。
extern bool FreezePPT;
extern HWND ppt_show;
extern std::wstring ppt_title, ppt_software;
extern std::map<std::wstring, bool> ppt_title_recond;

struct IdtWindowsIsVisibleStruct
{
	bool floatingWindow = false;
	bool drawpadWindow = false;
	bool pptWindow = false;
	bool freezeWindow = false;
	bool allCompleted = false;
};

extern IdtWindowsIsVisibleStruct IdtWindowsIsVisible;
extern bool rtsWait;
extern bool topWindowNow;

[[nodiscard]] HWND GetLastFocusWindow();
[[nodiscard]] std::wstring GetWindowTitle(HWND hwnd);

// 仅操作 owner 链根；owned popup 会随根窗口保持层级。
void TopWindow();
