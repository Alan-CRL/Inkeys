module;

#include "Setting.Wrap.h"

#include "../../../IdtMain.h"

export module Inkeys.UI.Setting;
import :Base;
import :Widgets;

export int SettingWindowX;
export int SettingWindowY;
export int SettingWindowWidth;
export int SettingWindowHeight;

LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

export void SettingWindowBegin();
export WNDPROC SettingWindowProc() noexcept;

export void SettingMain(stop_token sT);
