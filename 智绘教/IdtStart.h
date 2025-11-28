#pragma once
#include "IdtMain.h"

// -------------------------
// UI 对象

extern bool StartUiAnimationEnable;

class StartUiWidgetValue
{
public:
	StartUiWidgetValue()
	{
		replace = false;
	}
	StartUiWidgetValue(float sTarget, float eTarget, bool replaceTarget = true)
	{
		v = 0, s = sTarget, e = eTarget;
		replace = replaceTarget;
	}

public:
	float v, s, e;
	bool replace;
};
class StartUiWidgetColor
{
public:
	StartUiWidgetColor()
	{
		replace = false;
	}
	StartUiWidgetColor(COLORREF vTarget, float sTarget, float eTarget, bool replaceTarget = true)
	{
		v = vTarget, s = sTarget, e = eTarget;
		replace = replaceTarget;
	}

public:
	COLORREF v;
	float s, e;
	bool replace;
};

enum StartUiLineWidgetID
{
};
enum StartUiRoundRectWidgetID
{
	StartUI_Page1_Button_Next
};
enum StartUiImageWidgetID
{
	StartUI_Page1_Welcome
};
enum StartUiWordsWidgetID
{
	StartUI_Page1_Next
};

class StartUiRegionWidgetClass
{
public:
	StartUiWidgetValue X;
	StartUiWidgetValue Y;
	StartUiWidgetValue Width;
	StartUiWidgetValue Height;
};
class StartUiLineWidgetClass
{
public:
	StartUiWidgetValue X1;
	StartUiWidgetValue Y1;
	StartUiWidgetValue X2;
	StartUiWidgetValue Y2;

	StartUiWidgetValue Thickness;
	StartUiWidgetColor Color;
};
class StartUiRoundRectWidgetClass
{
public:
	StartUiWidgetValue X;
	StartUiWidgetValue Y;
	StartUiWidgetValue Width;
	StartUiWidgetValue Height;
	StartUiWidgetValue EllipseWidth;
	StartUiWidgetValue EllipseHeight;

	StartUiWidgetValue FrameThickness;
	StartUiWidgetColor FrameColor;
	StartUiWidgetColor FillColor;
};
class StartUiImageWidgetClass
{
public:
	StartUiImageWidgetClass()
	{
		//Img = NULL;
	}

public:
	StartUiWidgetValue X;
	StartUiWidgetValue Y;
	StartUiWidgetValue Width;
	StartUiWidgetValue Height;
	StartUiWidgetValue Transparency;
	//ID2D1Bitmap* Img;
};
class StartUiWordsWidgetClass
{
public:
	StartUiWordsWidgetClass()
	{
		WordsContent = L"";
	}

public:
	StartUiWidgetValue Left;
	StartUiWidgetValue Top;
	StartUiWidgetValue Right;
	StartUiWidgetValue Bottom;

	StartUiWidgetValue WordsHeight;
	StartUiWidgetColor WordsColor;

	wstring WordsContent;
};

// extern StartUiLineWidgetClass startUiLineWidget[0], startUiLineWidgetTarget[0];
extern StartUiRoundRectWidgetClass startUiRoundRectWidget[1], startUiRoundRectWidgetTarget[1];
extern StartUiImageWidgetClass startUiImageWidget[1], startUiImageWidgetTarget[1];
extern StartUiWordsWidgetClass startUiWordsWidget[1], startUiWordsWidgetTarget[1];

// -------------------------
// Start 状态

enum StartUiWidgetStateEnum
{
	Start_State_Unknow,
	Start_State_Page1_Welcome
};
extern StartUiWidgetStateEnum startUiWidgetState;

// -------------------------
// Start 主项

void StartForInkeys();

// --------------------------------------------------
// 其他杂项

struct IdtSysVersionStruct
{
	int majorVersion;
	int	minorVersion;
	int buildNumber;
};

typedef LONG(WINAPI* RtlGetVersionPtr)(RTL_OSVERSIONINFOW*);
IdtSysVersionStruct GetWindowsVersion();
bool hasTouchDevice();
