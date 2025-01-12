#pragma once
#include "IdtMain.h"

extern bool main_open;
extern bool FirstDraw;
extern bool IdtHotkey;

class StrokeImageClass
{
public:
	StrokeImageClass()
	{
		canvas = nullptr;
		endMode = 0;
		alpha = 255;
	}
	~StrokeImageClass()
	{
		if (canvas != nullptr)
		{
			delete canvas;
			canvas = nullptr;
		}
	}

public:
	shared_mutex sm;
	IMAGE* canvas;
	int endMode; // 1 绘制到画布上 2 不绘制到画布上
	int alpha;
};
extern StrokeImageClass strokeImage;

extern shared_mutex StrokeImageListSm;
extern vector<StrokeImageClass*> StrokeImageList;

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