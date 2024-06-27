/*
 * @file		IdtPlug-in.h
 * @brief		IDT plugin linkage | 智绘教插件联动
 * @note		PPT linkage components and other plugins | PPT联动组件和其他插件等
 *
 * @author		AlanCRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

#pragma once
#include "IdtMain.h"

#include "IdtD2DPreparation.h"

// All function and variable descriptions should be in the corresponding cpp file.
// 所有的函数和变量说明应该在对应的 cpp 文件中。

class PptUiWidgetValue
{
public:
	PptUiWidgetValue()
	{
		replace = false;
	}
	PptUiWidgetValue(float sTarget, float eTarget, bool replaceTarget = true)
	{
		v = 0, s = sTarget, e = eTarget;
		replace = replaceTarget;
	}

public:
	float v, s, e;
	bool replace;
};
class PptUiWidgetColor
{
public:
	PptUiWidgetColor()
	{
		replace = false;
	}
	PptUiWidgetColor(COLORREF vTarget, float sTarget, float eTarget, bool replaceTarget = true)
	{
		v = vTarget, s = sTarget, e = eTarget;
		replace = replaceTarget;
	}

public:
	COLORREF v;
	float s, e;
	bool replace;
};

enum PptUiRegionWidgetID
{
	LeftSide,
	MiddleSide,
	RightSide
};
enum PptUiRoundRectWidgetID
{
	LeftSide_PageWidget,
	LeftSide_PageWidget_PreviousPage,
	LeftSide_PageWidget_NextPage,
	MiddleSide_TabSlideWidget,
	MiddleSide_TabSlideWidget_EndShow,
	MiddleSide_TabDrawpadWidget,
	// ...
	RightSide_PageWidget,
	RightSide_PageWidget_PreviousPage,
	RightSide_PageWidget_NextPage
};
enum PptUiImageWidgetID
{
	LeftSide_PreviousPage,
	LeftSide_NextPage,
	MiddleSide_EndShow,
	RightSide_PreviousPage,
	RightSide_NextPage
};
enum PptUiWordsWidgetID
{
	LeftSide_PageNum,
	RightSide_PageNum
};

class PptUiRegionWidgetClass
{
public:
	PptUiWidgetValue X;
	PptUiWidgetValue Y;
	PptUiWidgetValue Width;
	PptUiWidgetValue Height;
};
class PptUiRoundRectWidgetClass
{
public:
	PptUiWidgetValue X;
	PptUiWidgetValue Y;
	PptUiWidgetValue Width;
	PptUiWidgetValue Height;
	PptUiWidgetValue EllipseWidth;
	PptUiWidgetValue EllipseHeight;

	PptUiWidgetValue FrameThickness;
	PptUiWidgetColor FrameColor;
	PptUiWidgetColor FillColor;
};
class PptUiImageWidgetClass
{
public:
	PptUiImageWidgetClass()
	{
		Img = NULL;
	}

public:
	PptUiWidgetValue X;
	PptUiWidgetValue Y;
	PptUiWidgetValue Width;
	PptUiWidgetValue Height;
	PptUiWidgetValue Transparency;
	ID2D1Bitmap* Img;
};
class PptUiWordsWidgetClass
{
public:
	PptUiWordsWidgetClass()
	{
		WordsContent = L"";
	}

public:
	PptUiWidgetValue Left;
	PptUiWidgetValue Top;
	PptUiWidgetValue Right;
	PptUiWidgetValue Bottom;

	PptUiWidgetValue WordsHeight;
	PptUiWidgetColor WordsColor;

	wstring WordsContent;
};

extern PptUiRoundRectWidgetClass pptUiRoundRectWidget[9], pptUiRoundRectWidgetTarget[9];
extern PptUiImageWidgetClass pptUiImageWidget[5], pptUiImageWidgetTarget[5];
extern PptUiWordsWidgetClass pptUiWordsWidget[2], pptUiWordsWidgetTarget[2];

extern float PPTUIScale;

struct PptImgStruct
{
	bool IsSave;
	map<int, bool> IsSaved;
	map<int, IMAGE> Image;
};
extern PptImgStruct PptImg;
struct PptInfoStateStruct
{
	long CurrentPage, TotalPage;
};
extern PptInfoStateStruct PptInfoStateBuffer;
extern PptInfoStateStruct PptInfoState;
extern bool PptWindowBackgroundUiChange;

extern wstring pptComVersion;
wstring GetPptComVersion();

void NextPptSlides(int check);
void PreviousPptSlides();
bool EndPptShow();

void PPTLinkageMain();

// --------------------------------------------------
// 插件

// DesktopDrawpadBlocker 插件
void StartDesktopDrawpadBlocker();