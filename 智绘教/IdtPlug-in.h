#pragma once
#include "IdtMain.h"

extern IMAGE PptIcon[5];
extern bool SeewoCamera; //希沃视频展台是否开启

// PPT联动

struct PptImgStruct
{
	bool IsSave;
	map<int, bool> IsSaved;
	map<int, IMAGE> Image;
};
extern PptImgStruct PptImg;

struct PptInfoStateStruct
{
	int CurrentPage, TotalPage;
};
extern PptInfoStateStruct PptInfoStateBuffer;
extern PptInfoStateStruct PptInfoState;

extern shared_mutex PPTManipulatedSm;
extern std::chrono::high_resolution_clock::time_point PPTManipulated;

wstring LinkTest();
wstring IsPptDependencyLoaded();
HWND GetPptShow();
wstring GetPptTitle();
bool EndPptShow();

struct PPTUIControlStruct
{
	float v, s, e;
};
extern map<wstring, PPTUIControlStruct> PPTUIControl, PPTUIControlTarget;
map<wstring, PPTUIControlStruct>& map<wstring, PPTUIControlStruct>::operator=(const map<wstring, PPTUIControlStruct>& m);

struct PPTUIControlColorStruct
{
	COLORREF v;
	float s, e;
};
extern map<wstring, PPTUIControlColorStruct> PPTUIControlColor, PPTUIControlColorTarget;
map<wstring, PPTUIControlColorStruct>& map<wstring, PPTUIControlColorStruct>::operator=(const map<wstring, PPTUIControlColorStruct>& m);

extern map<wstring, wstring> PPTUIControlString, PPTUIControlStringTarget;

int NextPptSlides(int check);
int PreviousPptSlides();

//ppt 控件
void DrawControlWindow();
void ControlManipulation();
void KeyboardInteraction();

//获取当前页编号
int GetCurrentPage();
//获取总页数
int GetTotalPage();
void ppt_state();

//弹窗拦截
//关闭AIClass和希沃白板5窗口
HWND FindWindowByStrings(const std::wstring& className, const std::wstring& windowTitle, const std::wstring& style, int width = 0, int height = 0);
void black_block();