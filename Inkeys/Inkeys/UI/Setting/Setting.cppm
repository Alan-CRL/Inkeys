module;

#include "Setting.Wrap.h"
#include "Setting.SessionState.h"

#include "../../../IdtMain.h"

export module Inkeys.UI.Setting;
import :Base;
import :Widgets;

export namespace Inkeys::UI::Setting
{
	[[nodiscard]] bool Initialize();
	void Shutdown() noexcept;
	void Show();
	void Hide();
	void Toggle();
	[[nodiscard]] bool IsVisible() noexcept;
	[[nodiscard]] WNDPROC WindowProc() noexcept;
}

export int SettingWindowX;
export int SettingWindowY;
export int SettingWindowWidth;
export int SettingWindowHeight;

LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

export void SettingWindowBegin();
export WNDPROC SettingWindowProc() noexcept;
