#pragma once
#include "IdtMain.h"
#include "Inkeys/Graphics/Surface.hpp"

// 选色轮相关
struct BrushColorChooseStruct
{
	int x, y;
	int last_x, last_y;
};
extern BrushColorChooseStruct BrushColorChoose;
extern Inkeys::Graphics::DibSurface ColorPaletteImg;
extern shared_mutex ColorPaletteSm;

// TODO 老旧残留

//窗口穿透
struct penetrateStruct
{
	bool select;
};
extern penetrateStruct penetrate;
//调测（现为设置)
struct testStruct
{
	bool select;
};
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
void RecolorSurface(Inkeys::Graphics::DibSurface& surface, COLORREF color);
uint32_t PackSurfaceBgra(COLORREF color, bool preserveAlpha = true);
Gdiplus::Color ToGdiplusColor(COLORREF color, bool preserveAlpha = false);
Gdiplus::RectF ToGdiplusRect(RECT rect);
void DrawSurfaceRectangle(float x, float y, float width, float height, COLORREF color, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface);
void DrawSurfaceEllipse(float x, float y, float width, float height, COLORREF color, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface);
void FillSurfaceEllipse(float x, float y, float width, float height, COLORREF color, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface);
void DrawSurfaceRoundRect(float x, float y, float width, float height, float ellipseWidth, float ellipseHeight, COLORREF color, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface);
void FillSurfaceRoundRect(float x, float y, float width, float height, float ellipseWidth, float ellipseHeight, COLORREF color, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface);
void DrawFilledSurfaceRoundRect(float x, float y, float width, float height, float ellipseWidth, float ellipseHeight, COLORREF lineColor, COLORREF fillColor, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface);
// 计算两个COLORREF颜色之间的加权距离
double color_distance(COLORREF c1, COLORREF c2);
// 定义反色函数
COLORREF InvertColor(COLORREF color, bool alpha_enable = false);
//保存图像到本地
bool SaveSurfaceToPng(const Inkeys::Graphics::DibSurface& surface, const wstring& filePath);

//比较图像
bool CompareSurfaces(const Inkeys::Graphics::DibSurface* first, const Inkeys::Graphics::DibSurface* second);
//设置图像必须不拥有全透明像素（将所有全透明像素点透明度设置为1）
void EnsureNonZeroAlpha(Inkeys::Graphics::DibSurface* surface);
void ForceOpaqueAlpha(Inkeys::Graphics::DibSurface* surface);

double EuclideanDistance(POINT a, POINT b);
double EuclideanDistanceP(Point a, Point b);

//智能绘图部分
extern map<pair<int, int>, bool> extreme_point;
extern shared_mutex ExtremePointSm;
//extern map<pair<Point, Point >, bool> extreme_line;
double pointToLineSegmentDistance(Point lineStart, Point lineEnd, Point p);
bool isLine(vector<Point> points, double tolerance, double drawingScale, std::chrono::high_resolution_clock::time_point start);

extern int stopTimingError;
int GetStopTimingError();
extern float drawingScale;
float GetDrawingScale();
