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

extern int RequestUpdateMagWindow;

void MagnifierThread();
void MagnifierUpdate();