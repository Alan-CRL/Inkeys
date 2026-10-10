#pragma once
#include "IdtMain.h"

#include <magnification.h>
#pragma comment(lib, "magnification.lib")

extern IMAGE MagnificationBackground;
extern HWND magnifierWindow, magnifierChild;

extern bool magnificationCreateReady;
extern bool magnificationReady;

extern shared_mutex MagnificationBackgroundSm;
extern RECT hostWindowRect;

extern int RequestUpdateMagWindow;

void CreateMagnifierWindow();
void MagnifierThread();
