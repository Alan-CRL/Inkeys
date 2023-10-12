#pragma once
#include "IdtMain.h"

#include "IdtDraw.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtPlug-in.h"
#include "IdtText.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

//窗口控制集
struct floating_windowsStruct
{
	int x, y;
	int height, width;
	int translucent;
};
extern floating_windowsStruct floating_windows;

extern IMAGE floating_icon[20], sign;

extern double state;
extern double target_status;

extern bool RestoreSketchpad, empty_drawpad;
extern bool smallcard_refresh;

//置顶程序窗口
void TopWindow();

//UI 控件

extern int BackgroundColorMode;
struct UIControlStruct
{
	float v, s, e;
};
extern map<wstring, UIControlStruct> UIControl, UIControlTarget;
map<wstring, UIControlStruct>& map<wstring, UIControlStruct>::operator=(const map<wstring, UIControlStruct>& m);

struct UIControlColorStruct
{
	COLORREF v;
	float s, e;
};
extern map<wstring, UIControlColorStruct> UIControlColor, UIControlColorTarget;
map<wstring, UIControlColorStruct>& map<wstring, UIControlColorStruct>::operator=(const map<wstring, UIControlColorStruct>& m);

//选色盘
Color ColorFromHSV(float hue, float saturation, float value);
IMAGE DrawHSVWheel(int r, int z = 0, int angle = 0);

//绘制屏幕
void DrawScreen();
void SeekBar(ExMessage m);

void MouseInteraction();

int floating_main();