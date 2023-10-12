#pragma once
#include "IdtMain.h"

#include "IdtWindow.h"
#include <magnification.h>							// ∑≈¥ÛAPI
#pragma comment(lib, "magnification.lib")
#define RESTOREDWINDOWSTYLES WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CAPTION | WS_MAXIMIZEBOX

extern IMAGE MagnificationBackground;
extern HWND hwndHost, hwndMag;
extern int magnificationWindowReady;

extern shared_mutex MagnificationBackgroundSm;
extern RECT magWindowRect;
extern RECT hostWindowRect;

extern HINSTANCE hInst;
BOOL MagImageScaling(HWND hwnd, void* srcdata, MAGIMAGEHEADER srcheader, void* destdata, MAGIMAGEHEADER destheader, RECT unclipped, RECT clipped, HRGN dirty);
void UpdateMagWindow();

LRESULT CALLBACK MagnifierWindowWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
ATOM RegisterHostWindowClass(HINSTANCE hInstance);
BOOL SetupMagnifier(HINSTANCE hinst);

void MagnifierThread();