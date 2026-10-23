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

// 专用内容可见，实际正式MagnifierThread采样；不运行旧owner读回。
int RunFreezeCaptureLiveTest();

// 仅严格早退CLI：真实Business/StateMonitoring/原Mag，私有专用内容。
int RunScrollFollowProductLiveTest();
// 实际OS画笔A/B、真实墨迹像素及原Exit文件的专用实屏入口。
int RunScrollFollowProductLiveInkTest();
// 独立严格长程CLI：真实二维wheel、至少十个主屏高度往返及原Exit。
int RunScrollFollowProductLiveLongTest();
// 私有runner拥有Edge；仅检查真实共享Mag的空页跟随，不声明墨迹验收。
int RunScrollFollowProductLiveExternalTest();
