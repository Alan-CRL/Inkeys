#pragma once
#include "IdtMain.h"

//窗口控制集
struct floating_windowsStruct
{
	int x, y;
	int height, width;
	int translucent;
};
extern floating_windowsStruct floating_windows;

extern IMAGE floating_icon[35], sign;
extern IMAGE skin[5];

extern double state;
extern double target_status;

extern bool reserve_drawpad;
extern bool smallcard_refresh;

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
//时钟表盘
pair<double, double> GetPointOnCircle(double x, double y, double r, double angle);

//绘制屏幕
void DrawScreen();
int SeekBar(ExMessage m);

extern IdtAtomic<bool> confirmaNoMouUpSignal;

void MouseInteraction();

int floating_main();