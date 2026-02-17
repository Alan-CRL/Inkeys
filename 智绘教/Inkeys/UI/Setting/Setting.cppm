module;

#include "Setting.Wrap.h"

#include "../../../IdtMain.h"

export module Inkeys.UI.Setting;
import :Base;
import :Widgets;

int SettingWindowX;
int SettingWindowY;
int SettingWindowWidth;
int SettingWindowHeight;

void SettingSeekBar();

LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void SettingWindow(promise<void>& promise);
export void SettingWindowBegin();

export void SettingMain();