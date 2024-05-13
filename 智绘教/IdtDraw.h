#pragma once
#include "IdtMain.h"

// 选色轮相关
struct BrushColorChooseStruct
{
	int x, y;
	int last_x, last_y;
};
extern BrushColorChooseStruct BrushColorChoose;
extern IMAGE ColorPaletteImg;
extern shared_mutex ColorPaletteSm;

enum DrawModeSelectEnum
{
	IdtSelection,
	IdtPen,
	IdtEraser
};

class DrawMode
{
public:
	DrawModeSelectEnum DrawModeSelect = DrawModeSelectEnum::IdtSelection;
};

struct penetrateStruct
{
	bool select;
}; //窗口穿透
extern penetrateStruct penetrate;
struct chooseStruct
{
	bool select;
}; //选择
extern chooseStruct choose;
struct brushStruct
{
	bool select;
	int width, mode;
	COLORREF color, primary_colour;

	int PenWidthHistory, HighlighterWidthHistory;
}; //画笔
extern brushStruct brush;
struct rubberStruct
{
	bool select;
}; //橡皮
extern rubberStruct rubber;

// TODO 老旧残留
struct testStruct
{
	bool select;
}; //调测
extern testStruct test;

struct plug_in_RandomRollCallStruct
{
	int select;
};
extern plug_in_RandomRollCallStruct plug_in_RandomRollCall;
struct FreezeFrameStruct
{
	bool select;
	int mode;

	bool update;
};
extern FreezeFrameStruct FreezeFrame;

// 将标准的 sRGB 值转换为线性 RGB 值
double sRGBToLinear(double s);
// 计算相对亮度
double computeLuminance(COLORREF color);
// 计算两种颜色的对比度
double computeContrast(COLORREF color1, COLORREF color2);

//像素颜色调整（将所有透明度不为0的像素点，改为指定颜色）
void ChangeColor(IMAGE& img, COLORREF color);
// 计算两个COLORREF颜色之间的加权距离
double color_distance(COLORREF c1, COLORREF c2);
// 定义反色函数
COLORREF InvertColor(COLORREF color, bool alpha_enable = false);
//保存图像到本地
void saveImageToPNG(IMAGE img, const char* filename, bool alpha = true, int compression_level = 9);
void SaveHBITMAPToPNG(HBITMAP hBitmap, const char* filename);
void saveImageToJPG(IMAGE img, const char* filename, int quality = 100);
//比较图像
bool CompareImagesWithBuffer(IMAGE* img1, IMAGE* img2);
//设置图像必须不拥有全透明像素（将所有全透明像素点透明度设置为1）
void SetAlphaToOne(IMAGE* pImg);

//智能绘图部分

extern map<pair<int, int>, bool> extreme_point;
extern shared_mutex ExtremePointSm;
//extern map<pair<Point, Point >, bool> extreme_line;
double pointToLineDistance(Point lineStart, Point lineEnd, Point p);
double pointToLineSegmentDistance(Point lineStart, Point lineEnd, Point p);
bool isLine(vector<Point> points, int tolerance, std::chrono::high_resolution_clock::time_point start);