#pragma once
#include "IdtMain.h"
#include "Inkeys/Graphics/Surface.hpp"

#include <atomic>
#include <magnification.h>
#pragma comment(lib, "magnification.lib")

extern Inkeys::Graphics::DibSurface MagnificationBackground;
extern HWND magnifierWindow, magnifierChild;

extern std::atomic<bool> magnificationCreateReady;

extern shared_mutex MagnificationBackgroundSm;
extern RECT hostWindowRect;

LRESULT CALLBACK MagnifierHostWindowWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool PrepareMagnifierWindow();
void MagnifierHostCreated(HWND hwnd);
void MagnifierChildCreated(HWND hwnd);
void StopMagnifierCoordinator() noexcept;
void ShutdownMagnifierWindow();
void MagnifierThread();
// 仅显式内部 harness 入口创建自动关闭的专用测试窗口。
int RunFreezeMagnifierPrintWindowTest();
