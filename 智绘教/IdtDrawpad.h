#pragma once
#include "IdtMain.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtFloating.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtTime.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"
#include "IdtHistoricalDrawpad.h"

extern bool main_open;
extern int SettingMainMode;
extern bool FirstDraw;

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

void MultiFingerDrawing(LONG pid, POINT pt);
void DrawpadDrawing();
int drawpad_main();