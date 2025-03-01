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

// --------------------------------------------------
// PPT 联动插件

// -------------------------
// UI 对象

extern bool PptUiAnimationEnable;

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
enum PptUiLineWidgetID
{
	BottomSide_LeftPageWidget_SeekBar,
	BottomSide_RightPageWidget_SeekBar,
	BottomSide_MiddleWidget_SeekBar,
	MiddleSide_LeftPageWidget_SeekBar,
	MiddleSide_RightPageWidget_SeekBar
};
enum PptUiRoundRectWidgetID
{
	BottomSide_LeftPageWidget,
	BottomSide_LeftPageWidget_PreviousPage,
	BottomSide_LeftPageWidget_NextPage,
	BottomSide_RightPageWidget,
	BottomSide_RightPageWidget_PreviousPage,
	BottomSide_RightPageWidget_NextPage,

	BottomSide_MiddleTabSlideWidget,
	BottomSide_MiddleTabSlideWidget_EndShow,
	BottomSide_MiddleTabDrawpadWidget,

	MiddleSide_LeftPageWidget,
	MiddleSide_LeftPageWidget_PreviousPage,
	MiddleSide_LeftPageWidget_NextPage,
	MiddleSide_RightPageWidget,
	MiddleSide_RightPageWidget_PreviousPage,
	MiddleSide_RightPageWidget_NextPage,

	EndShow_ConfirmWidget
};
enum PptUiImageWidgetID
{
	BottomSide_LeftPreviousPage,
	BottomSide_LeftNextPage,
	BottomSide_MiddleEndShow,
	BottomSide_RightPreviousPage,
	BottomSide_RightNextPage,

	MiddleSide_LeftPreviousPage,
	MiddleSide_LeftNextPage,
	MiddleSide_RightPreviousPage,
	MiddleSide_RightNextPage
};
enum PptUiWordsWidgetID
{
	BottomSide_LeftPageNum_Above,
	BottomSide_LeftPageNum_Below,
	BottomSide_RightPageNum_Above,
	BottomSide_RightPageNum_Below,

	MiddleSide_LeftPageNum_Above,
	MiddleSide_LeftPageNum_Below,
	MiddleSide_RightPageNum_Above,
	MiddleSide_RightPageNum_Below
};

class PptUiRegionWidgetClass
{
public:
	PptUiWidgetValue X;
	PptUiWidgetValue Y;
	PptUiWidgetValue Width;
	PptUiWidgetValue Height;
};
class PptUiLineWidgetClass
{
public:
	PptUiWidgetValue X1;
	PptUiWidgetValue Y1;
	PptUiWidgetValue X2;
	PptUiWidgetValue Y2;

	PptUiWidgetValue Thickness;
	PptUiWidgetColor Color;
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

extern PptUiLineWidgetClass pptUiLineWidget[5], pptUiLineWidgetTarget[5];
extern PptUiRoundRectWidgetClass pptUiRoundRectWidget[15], pptUiRoundRectWidgetTarget[15];
extern PptUiImageWidgetClass pptUiImageWidget[9], pptUiImageWidgetTarget[9];
extern PptUiWordsWidgetClass pptUiWordsWidget[8], pptUiWordsWidgetTarget[8];

// -------------------------
// ppt 信息

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
extern wstring pptComVersion;

// -------------------------
// Ppt 状态

enum PptUiWidgetStateEnum
{
	Unknow,
	Close,
	Expand,
	Hide,
	EndShowQ
};
extern PptUiWidgetStateEnum pptUiWidgetState;

// -------------------------
// Ppt 主项

void NextPptSlides(int check);
void PreviousPptSlides();
bool EndPptShow();

void PPTLinkageMain();

bool IsPowerPointRunAsAdminSet();
class CheckEndShowClass
{
public:
	CheckEndShowClass()
	{
		isChecking = false;
	}
public:
	bool isChecking;
	bool Check();
};
extern CheckEndShowClass CheckEndShow;

// --------------------------------------------------
// 其他插件

void StartDesktopDrawpadBlocker();

class ShortcutAssistantClass
{
public:
	void SetShortcut();
	bool IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory);
	bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath);
};
extern ShortcutAssistantClass shortcutAssistant;