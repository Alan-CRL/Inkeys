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

extern bool main_open;
extern int TestMainMode;

RECT DrawGradientLine(HDC hdc, int x1, int y1, int x2, int y2, float width, Color color);
bool checkIntersection(RECT rect1, RECT rect2);
double EuclideanDistance(POINT a, POINT b);

void removeEmptyFolders(std::wstring path);
void removeUnknownFiles(std::wstring path, std::deque<std::wstring> knownFiles);
deque<wstring> getPrevTwoDays(const std::wstring& date, int day = 7);
//保存图像到指定目录
void SaveScreenShot(IMAGE img);

void ControlTestMain();

LRESULT CALLBACK TestWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
int test_main();

void FreezeFrameWindow();

int drawpad_main();