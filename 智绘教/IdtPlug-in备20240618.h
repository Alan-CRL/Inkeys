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

// All function and variable descriptions should be in the corresponding cpp file.
// 所有的函数和变量说明应该在对应的 cpp 文件中。

class UiWidgetValue
{
public:
	float v, s, e;
};
class UiWidgetColor
{
public:
	COLORREF v;
	float s, e;
};

enum PptUiRegionWidgetID
{
	LeftSide,
	MiddleSide,
	RightSide
};
enum PptUiRoundRectWidgetID
{
	LeftSide_PreviousPage,
	LeftSide_NextPage,
	MiddleSide_TabSlideWidget,
	MiddleSide_TabSlideWidget_EndShow,
	MiddleSide_TabDrawpadWidget,
	RightSide_PreviousPage,
	RightSide_NextPage
};
enum PptUiImageWidgetID
{
	LeftSide_PreviousPage,
	LeftSide_NextPage,
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
	UiWidgetValue X;
	UiWidgetValue Y;
	UiWidgetValue Width;
	UiWidgetValue Height;
};
class PptUiRoundRectWidgetClass
{
public:
	UiWidgetValue X;
	UiWidgetValue Y;
	UiWidgetValue Width;
	UiWidgetValue Height;
	UiWidgetValue EllipseWidth;
	UiWidgetValue EllipseHeight;

	UiWidgetValue FrameThickness;
	UiWidgetColor FrameColor;
	UiWidgetColor FillColor;
};
class PptUiImageWidgetClass
{
public:
	UiWidgetValue X;
	UiWidgetValue Y;
	UiWidgetValue Width;
	UiWidgetValue Height;
	IMAGE* Img;
};
class PptUiWordsWidgetClass
{
public:
	UiWidgetValue left;
	UiWidgetValue top;
	UiWidgetValue right;
	UiWidgetValue bottom;

	UiWidgetValue WordsHeight;
	UiWidgetColor WordsColor;

	wstring WordsContent;
};

extern PptUiRoundRectWidgetClass pptUiRoundRectWidget[55], pptUiRoundRectWidgetTarget[55];
extern PptUiImageWidgetClass pptUiImageWidget[15], pptUiImageWidgetTarget[15];
extern PptUiWordsWidgetClass pptUiWordsWidget[15], pptUiWordsWidgetTarget[15];

extern map<wstring, PPTUIControlStruct> PPTUIControl, PPTUIControlTarget;
map<wstring, PPTUIControlStruct>& map<wstring, PPTUIControlStruct>::operator=(const map<wstring, PPTUIControlStruct>& m);
extern map<wstring, PPTUIControlColorStruct> PPTUIControlColor, PPTUIControlColorTarget;
map<wstring, PPTUIControlColorStruct>& map<wstring, PPTUIControlColorStruct>::operator=(const map<wstring, PPTUIControlColorStruct>& m);
extern map<wstring, wstring> PPTUIControlString, PPTUIControlStringTarget;

extern float PPTUIScale;
extern bool PPTUIScaleRecommend;

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

void GetPptState();
void PPTLinkageMain();

// --------------------------------------------------
// 插件

// DesktopDrawpadBlocker 插件
void StartDesktopDrawpadBlocker();