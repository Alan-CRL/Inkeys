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
	int RunBusinessCancellationProductionTest() noexcept;
	// 复用已启动的业务FIFO，不打开设置窗口。
	void RequestConfigWrite();
	// 仅设置渲染线程把阻塞窗口同步交给已有 FIFO，其他调用保持同步。
	[[nodiscard]] bool DeferDraw3SyncIfRendering();
	void Show();
	// 从橡皮属性直达详情，仍走同一 Show、激活与开屏策略。
	void ShowSpeedEraser();
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
