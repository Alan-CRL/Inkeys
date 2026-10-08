#pragma once
#include "IdtMain.h"
#include "Inkeys/Graphics/Surface.hpp"

#include <magnification.h>
#pragma comment(lib, "magnification.lib")

extern Inkeys::Graphics::DibSurface MagnificationBackground;
extern HWND magnifierWindow, magnifierChild;

extern bool magnificationCreateReady;

extern shared_mutex MagnificationBackgroundSm;
extern RECT hostWindowRect;

LRESULT CALLBACK MagnifierHostWindowWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool PrepareMagnifierWindow();
void MagnifierHostCreated(HWND hwnd);
void MagnifierChildCreated(HWND hwnd);
void StopMagnifierCoordinator() noexcept;
void ShutdownMagnifierWindow();
void MagnifierThread();
