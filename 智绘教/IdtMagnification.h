#pragma once
#include "IdtMain.h"

#include <magnification.h>
#pragma comment(lib, "magnification.lib")

extern IMAGE MagnificationBackground;
extern HINSTANCE hInst;
extern HWND hwndHost, hwndMag;

extern bool magnificationReady;

extern shared_mutex MagnificationBackgroundSm;
extern RECT hostWindowRect;

BOOL MagImageScaling(HWND hwnd, void* srcdata, MAGIMAGEHEADER srcheader, void* destdata, MAGIMAGEHEADER destheader, RECT unclipped, RECT clipped, HRGN dirty);
void UpdateMagWindow();

LRESULT CALLBACK MagnifierWindowWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
ATOM RegisterHostWindowClass(HINSTANCE hInstance);
BOOL SetupMagnifier(HINSTANCE hinst);

extern bool RequestUpdateMagWindow;

void MagnifierThread();
void MagnifierUpdate();