#include "IdtDraw.h"

#include "IdtConfiguration.h"
import Inkeys.Display;

BrushColorChooseStruct BrushColorChoose = { 0,0,-1,-1 };
Inkeys::Graphics::DibSurface ColorPaletteImg;
shared_mutex ColorPaletteSm;

penetrateStruct penetrate; //窗口穿透
testStruct test; //调测

plug_in_RandomRollCallStruct plug_in_RandomRollCall;
FreezeFrameStruct FreezeFrame;

// 将标准的 sRGB 值转换为线性 RGB 值
double sRGBToLinear(double s) {
	if (s <= 0.04045) return s / 12.92;
	return pow((s + 0.055) / 1.055, 2.4);
}
// 计算相对亮度
double computeLuminance(COLORREF color) {
	double R = sRGBToLinear(GetRValue(color) / 255.0);
	double G = sRGBToLinear(GetGValue(color) / 255.0);
	double B = sRGBToLinear(GetBValue(color) / 255.0);
	return 0.2126 * R + 0.7152 * G + 0.0722 * B;
}
// 计算两种颜色的对比度
double computeContrast(COLORREF color1, COLORREF color2) {
	double L1 = computeLuminance(color1);
	double L2 = computeLuminance(color2);

	if (L1 > L2) return (L1 + 0.05) / (L2 + 0.05);
	return (L2 + 0.05) / (L1 + 0.05);
}

//像素颜色调整（将所有透明度不为0的像素点，改为指定颜色）
void RecolorSurface(Inkeys::Graphics::DibSurface& surface, COLORREF color)
{
	// 遍历每个像素点
	for (auto& pixel : surface.pixels())
	{
		// 获取当前像素点的颜色值
		const DWORD sourcePixel = pixel;

		// 获取当前像素点的透明度（alpha 值）
		const DWORD alpha = sourcePixel >> 24;

		// 如果源图像颜色与修改颜色相同则不修改（直接跳过）
		if (alpha != 0)
		{
			if (sourcePixel == PackSurfaceBgra(color, false)) continue;
		}
		else continue;

		// 将COLORREF转换为RGB
		DWORD rgb = ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16);

		// 根据透明度调整颜色的亮度
		DWORD r = (rgb & 0xFF0000) >> 16;
		DWORD g = (rgb & 0x00FF00) >> 8;
		DWORD b = (rgb & 0x0000FF);
		r = r * alpha / 255;
		g = g * alpha / 255;
		b = b * alpha / 255;
		rgb = (r << 16) | (g << 8) | b;

		// 将传入的颜色值与透明度合并
		DWORD newPixel = (alpha << 24) | (rgb & 0x00FFFFFF);

		// 将新的颜色值写入缓冲区
		pixel = newPixel;
	}
}

uint32_t PackSurfaceBgra(COLORREF color, bool preserveAlpha)
{
	const uint32_t alpha = preserveAlpha ? static_cast<uint32_t>((color >> 24) & 0xFF) : 255u;
	return (alpha << 24) |
		(static_cast<uint32_t>(GetRValue(color)) << 16) |
		(static_cast<uint32_t>(GetGValue(color)) << 8) |
		static_cast<uint32_t>(GetBValue(color));
}

Gdiplus::Color ToGdiplusColor(COLORREF color, bool preserveAlpha)
{
	return Gdiplus::Color(
		preserveAlpha ? static_cast<BYTE>((color >> 24) & 0xFF) : 255,
		GetRValue(color),
		GetGValue(color),
		GetBValue(color));
}

Gdiplus::RectF ToGdiplusRect(RECT rect)
{
	return Gdiplus::RectF(
		static_cast<Gdiplus::REAL>(rect.left),
		static_cast<Gdiplus::REAL>(rect.top),
		static_cast<Gdiplus::REAL>(rect.right - rect.left),
		static_cast<Gdiplus::REAL>(rect.bottom - rect.top));
}

namespace
{
	void BuildRoundRectPath(Gdiplus::GraphicsPath& path, float x, float y, float width, float height, float ellipseWidth, float ellipseHeight)
	{
		ellipseWidth = min(ellipseWidth, width - 1);
		ellipseHeight = min(ellipseHeight, height - 1);
		path.AddArc(x, y, ellipseWidth, ellipseHeight, 180, 90);
		path.AddArc(x + width - ellipseWidth - 1, y, ellipseWidth, ellipseHeight, 270, 90);
		path.AddArc(x + width - ellipseWidth - 1, y + height - ellipseHeight - 1, ellipseWidth, ellipseHeight, 0, 90);
		path.AddArc(x, y + height - ellipseHeight - 1, ellipseWidth, ellipseHeight, 90, 90);
		path.CloseFigure();
	}
}

void DrawSurfaceRectangle(float x, float y, float width, float height, COLORREF color, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface)
{
	if (!surface || surface->empty()) return;
	if (!Inkeys::Graphics::Detail::EnsureGdiplusReady()) return;
	Gdiplus::Graphics graphics(surface->dc());
	Gdiplus::Pen pen(ToGdiplusColor(color, preserveAlpha), lineWidth);
	graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
	graphics.SetSmoothingMode(smoothingMode);
	graphics.DrawRectangle(&pen, x, y, width, height);
}

void DrawSurfaceEllipse(float x, float y, float width, float height, COLORREF color, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface)
{
	if (!surface || surface->empty()) return;
	if (!Inkeys::Graphics::Detail::EnsureGdiplusReady()) return;
	Gdiplus::Graphics graphics(surface->dc());
	Gdiplus::Pen pen(ToGdiplusColor(color, preserveAlpha), lineWidth);
	graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
	graphics.SetSmoothingMode(smoothingMode);
	graphics.DrawEllipse(&pen, x, y, width, height);
}

void FillSurfaceEllipse(float x, float y, float width, float height, COLORREF color, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface)
{
	if (!surface || surface->empty()) return;
	if (!Inkeys::Graphics::Detail::EnsureGdiplusReady()) return;
	Gdiplus::Graphics graphics(surface->dc());
	Gdiplus::SolidBrush brush(ToGdiplusColor(color, preserveAlpha));
	graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
	graphics.SetSmoothingMode(smoothingMode);
	graphics.FillEllipse(&brush, x, y, width, height);
}

void DrawSurfaceRoundRect(float x, float y, float width, float height, float ellipseWidth, float ellipseHeight, COLORREF color, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface)
{
	if (!surface || surface->empty()) return;
	if (!Inkeys::Graphics::Detail::EnsureGdiplusReady()) return;
	Gdiplus::Graphics graphics(surface->dc());
	Gdiplus::Pen pen(ToGdiplusColor(color, preserveAlpha), lineWidth);
	Gdiplus::GraphicsPath path;
	BuildRoundRectPath(path, x, y, width, height, ellipseWidth, ellipseHeight);
	graphics.SetSmoothingMode(smoothingMode);
	graphics.DrawPath(&pen, &path);
}

void FillSurfaceRoundRect(float x, float y, float width, float height, float ellipseWidth, float ellipseHeight, COLORREF color, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface)
{
	if (!surface || surface->empty()) return;
	if (!Inkeys::Graphics::Detail::EnsureGdiplusReady()) return;
	Gdiplus::Graphics graphics(surface->dc());
	Gdiplus::SolidBrush brush(ToGdiplusColor(color, preserveAlpha));
	Gdiplus::GraphicsPath path;
	BuildRoundRectPath(path, x, y, width, height, ellipseWidth, ellipseHeight);
	graphics.SetSmoothingMode(smoothingMode);
	graphics.FillPath(&brush, &path);
}

void DrawFilledSurfaceRoundRect(float x, float y, float width, float height, float ellipseWidth, float ellipseHeight, COLORREF lineColor, COLORREF fillColor, float lineWidth, bool preserveAlpha, Gdiplus::SmoothingMode smoothingMode, Inkeys::Graphics::DibSurface* surface)
{
	if (!surface || surface->empty()) return;
	if (!Inkeys::Graphics::Detail::EnsureGdiplusReady()) return;
	Gdiplus::Graphics graphics(surface->dc());
	Gdiplus::Pen pen(ToGdiplusColor(lineColor, preserveAlpha), lineWidth);
	Gdiplus::SolidBrush brush(ToGdiplusColor(fillColor, preserveAlpha));
	Gdiplus::GraphicsPath path;
	BuildRoundRectPath(path, x, y, width, height, ellipseWidth, ellipseHeight);
	graphics.SetSmoothingMode(smoothingMode);
	graphics.FillPath(&brush, &path);
	graphics.DrawPath(&pen, &path);
}
// 计算两个COLORREF颜色之间的加权距离
double color_distance(COLORREF c1, COLORREF c2) {
	// 提取各个颜色分量
	int r1 = GetRValue(c1);
	int g1 = GetGValue(c1);
	int b1 = GetBValue(c1);
	int r2 = GetRValue(c2);
	int g2 = GetGValue(c2);
	int b2 = GetBValue(c2);

	// 设置各个分量的权重
	double wr = 0.3;
	double wg = 0.59;
	double wb = 0.11;

	// 计算加权平方和
	double sum = wr * (r1 - r2) * (r1 - r2) +
		wg * (g1 - g2) * (g1 - g2) +
		wb * (b1 - b2) * (b1 - b2);

	// 开平方并返回
	return sqrt(sum);
}
// 定义反色函数
COLORREF InvertColor(COLORREF color, bool alpha_enable)
{
	// 提取颜色分量
	BYTE red = GetRValue(color);
	BYTE green = GetGValue(color);
	BYTE blue = GetBValue(color);
	BYTE alpha;
	if (alpha_enable) alpha = (color >> 24) & 0xff;
	else alpha = 255;

	// 反色分量
	red = 255 - red;
	green = 255 - green;
	blue = 255 - blue;

	// 合并颜色分量和透明度
	COLORREF inverted = red | (green << 8) | (blue << 16) | (alpha << 24);

	// 返回反色
	return inverted;
}

//保存图像到本地
bool SaveSurfaceToPng(const Inkeys::Graphics::DibSurface& surface, const wstring& filePath)
{
	return surface.savePng(filePath);
}

//比较图像
bool CompareSurfaces(const Inkeys::Graphics::DibSurface* first, const Inkeys::Graphics::DibSurface* second)
{
	return first && second && first->equals(*second);
}
//设置图像必须不拥有全透明像素（将所有全透明像素点透明度设置为1）
void EnsureNonZeroAlpha(Inkeys::Graphics::DibSurface* surface)
{
	if (!surface) return;
	for (auto& color : surface->pixels())
	{
		// 如果透明度为0，则将其设为1
		if ((color >> 24) == 0)
			color = (color & 0x00FFFFFF) | 0x01000000;
	}
}

void ForceOpaqueAlpha(Inkeys::Graphics::DibSurface* surface)
{
	if (!surface) return;
	for (auto& color : surface->pixels())
		color |= 0xFF000000u;
}

double EuclideanDistance(POINT a, POINT b)
{
	return std::sqrt(std::pow(a.x - b.x, 2) + std::pow(a.y - b.y, 2));
}
double EuclideanDistanceP(Point a, Point b)
{
	return std::sqrt(std::pow(a.X - b.X, 2) + std::pow(a.Y - b.Y, 2));
}

//智能绘图部分
map<pair<int, int>, bool> extreme_point;
shared_mutex ExtremePointSm;
//map<pair<Point, Point >, bool> extreme_line;

double pointToLineSegmentDistance(Point lineStart, Point lineEnd, Point p)
{
	double x1 = lineStart.X;
	double y1 = lineStart.Y;
	double x2 = lineEnd.X;
	double y2 = lineEnd.Y;
	double x3 = p.X;
	double y3 = p.Y;

	if (x1 == x2) {
		if (y3 >= min(y1, y2) && y3 <= max(y1, y2)) {
			return (x3 - x1);  // 移除了abs
		}
		else {
			// 对于端点距离，仍然使用绝对值，因为这是实际距离
			return min(sqrt(pow(x3 - x1, 2) + pow(y3 - y1, 2)), sqrt(pow(x3 - x2, 2) + pow(y3 - y2, 2)));
		}
	}

	double a = (y2 - y1) / (x2 - x1);
	double b = y1 - a * x1;
	double x4 = (a * (y3 - b) + x3) / (a * a + 1);
	if (x4 >= min(x1, x2) && x4 <= max(x1, x2)) {
		return (a * x3 - y3 + b) / sqrt(a * a + 1);  // 移除了abs
	}
	else {
		// 对于端点距离，仍然使用绝对值，因为这是实际距离
		return min(sqrt(pow(x3 - x1, 2) + pow(y3 - y1, 2)), sqrt(pow(x3 - x2, 2) + pow(y3 - y2, 2)));
	}
}
bool isLine(vector<Point> points, double tolerance, double drawingScale, std::chrono::high_resolution_clock::time_point start)
{
	int n = points.size();
	if (n < 2) return false;
	if (n == 2) return true;

	double minD = 0, maxD = 0;

	double lineDis = EuclideanDistanceP(points.front(), points.back());
	double minDis = lineDis;// 离终点最近的点到终点的距离

	for (int i = 1; i < n - 1; i++)
	{
		if (i % 10 == 0 && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() > 100) return false;

		double distance = pointToLineSegmentDistance(points.front(), points.back(), points[i]);
		if (abs(distance) > tolerance) return false;

		minD = min(minD, distance);
		maxD = max(maxD, distance);

		double tmpDis = EuclideanDistanceP(points[i], points.back());
		if (tmpDis < minDis) minDis = tmpDis;
		if (tmpDis > minDis + lineDis / 4.0) return false;
	}

	double disparity = maxD - minD;
	if (disparity >= 5 * drawingScale)
	{
		int trend = 0; // 1: 标记正数最大点 2：标记负数最小点
		double markMax = maxD, markMin = minD;
		double minFluctuate = max(5 * drawingScale, disparity / 3.0);
		int fluctuate = 0; // 波动次数

		for (int i = 1; i < n - 1; i++)
		{
			if (i % 10 == 0 && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() > 100) return false;

			double distance = pointToLineSegmentDistance(points.front(), points.back(), points[i]);

			if (distance > markMin + minFluctuate && trend != 1)
			{
				trend = 1;
				markMax = distance;
				fluctuate++;
			}

			if (distance < markMax - minFluctuate && trend != 2)
			{
				trend = 2;
				markMin = distance;
				fluctuate++;
			}

			if (trend != 2) markMax = max(markMax, distance);
			if (trend != 1) markMin = min(markMin, distance);
		}

		if (fluctuate <= 4) return true;
		else return false;
	}
	return true;
}

// 临时过渡方案
/*
* 在绘图库架构 3.0 到来之前，不会对对当前架构做高分屏的支持
*
* 现在显示物理宽高只会运用于停留拉直的停留检测模块，期望停留范围是 0.3cm 触控设备/ 5px 鼠标
*/
// 停留拉直误差(px)
int stopTimingError = 5;
int GetStopTimingError()
{
	const auto snapshot = Inkeys::Display::GetSnapshot();
	const auto* monitor = snapshot ? snapshot->Primary() : nullptr;
	if (!monitor || setlist.paintDevice == 1 ||
		monitor->edid.physicalHeightCm == 0 || monitor->edid.physicalWidthCm == 0) return 5;
	return min(0.3f * static_cast<float>(monitor->pixelWidth) /
		static_cast<float>(monitor->edid.physicalHeightCm),
		0.5f * static_cast<float>(monitor->pixelHeight) /
		static_cast<float>(monitor->edid.physicalHeightCm));
}
float drawingScale = 1.0f;
float GetDrawingScale()
{
	const auto snapshot = Inkeys::Display::GetSnapshot();
	const auto* monitor = snapshot ? snapshot->Primary() : nullptr;
	if (!monitor) return 1.0F;
	return min(static_cast<float>(monitor->pixelWidth) / 1920.0f,
		static_cast<float>(monitor->pixelHeight) / 1080.0f);
}
