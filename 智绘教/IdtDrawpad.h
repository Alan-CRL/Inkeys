#pragma once
#include "IdtMain.h"

extern bool main_open;
extern bool FirstDraw;
extern bool IdtHotkey;

extern unordered_map<LONG, shared_mutex> StrokeImageSm;
extern shared_mutex StrokeImageListSm;
extern map<LONG, pair<IMAGE*, int>> StrokeImage;
extern vector<LONG> StrokeImageList;
extern shared_mutex StrokeBackImageSm;

extern bool drawWaiting;
extern shared_mutex drawWaitingSm;

extern IMAGE drawpad;
extern IMAGE window_background;

extern unordered_map<BYTE, bool> KeyBoradDown;
extern HHOOK DrawpadHookCall;
LRESULT CALLBACK DrawpadHookCallback(int nCode, WPARAM wParam, LPARAM lParam);
void DrawpadInstallHook();

void ResetPrepareCanvas();
int drawpad_main();