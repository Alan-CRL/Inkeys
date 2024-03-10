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

extern IMAGE drawpad; //Ö÷»­°å
extern IMAGE window_background;

extern unordered_map<BYTE, bool> KeyBoradDown;
extern HHOOK DrawpadHookCall;
LRESULT CALLBACK DrawpadHookCallback(int nCode, WPARAM wParam, LPARAM lParam);
void DrawpadInstallHook();

double EuclideanDistance(POINT a, POINT b);

void MultiFingerDrawing(LONG pid, POINT pt);
void DrawpadDrawing();
int drawpad_main();