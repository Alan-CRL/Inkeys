#pragma once
#include "IdtMain.h"

extern bool main_open;
extern bool FirstDraw;
extern bool IdtHotkey;

RECT DrawGradientLine(HDC hdc, int x1, int y1, int x2, int y2, float width, Color color);
bool checkIntersection(RECT rect1, RECT rect2);
double EuclideanDistance(POINT a, POINT b);

void FreezeFrameWindow();

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

void MultiFingerDrawing(LONG pid, POINT pt);
void DrawpadDrawing();
int drawpad_main();