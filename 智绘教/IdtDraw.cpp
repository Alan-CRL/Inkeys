#include "IdtDraw.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stbimage/stb_image_write.h"

BrushColorChooseStruct BrushColorChoose = { 0,0,-1,-1 };
IMAGE ColorPaletteImg;
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
void ChangeColor(IMAGE& img, COLORREF color)
{
	// 获取图像的宽度和高度
	int width = img.getwidth();
	int height = img.getheight();

	// 获取图像的缓冲区指针
	DWORD* pBuf = GetImageBuffer(&img);

	// 遍历每个像素点
	for (int i = 0; i < width * height; i++)
	{
		// 获取当前像素点的颜色值
		DWORD pixel = pBuf[i];

		// 获取当前像素点的透明度（alpha 值）
		DWORD alpha = pixel >> 24;

		// 如果源图像颜色与修改颜色相同则不修改（直接跳过）
		if (alpha != 0)
		{
			if (pixel == BGR(color)) continue;
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
		pBuf[i] = newPixel;
	}
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
bool saveImageToPNG(IMAGE img, const wstring& filePath, bool alpha, int compression_level)
{
	int width = img.getwidth();
	int height = img.getheight();

	// Get the image buffer of the IMAGE object
	DWORD* pMem = GetImageBuffer(&img);

	unsigned char* data = new unsigned char[width * height * 4];
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			DWORD color = pMem[y * width + x];
			if (alpha)
			{
				unsigned char alpha = (color & 0xFF000000) >> 24;
				if (alpha != 0)
				{
					data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
					data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
					data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
				}
				else
				{
					data[(y * width + x) * 4 + 0] = 0;
					data[(y * width + x) * 4 + 1] = 0;
					data[(y * width + x) * 4 + 2] = 0;
				}
				data[(y * width + x) * 4 + 3] = alpha;
			}
			else
			{
				data[(y * width + x) * 4 + 0] = unsigned char((color & 0x00FF0000) >> 16);
				data[(y * width + x) * 4 + 1] = unsigned char((color & 0x0000FF00) >> 8);
				data[(y * width + x) * 4 + 2] = unsigned char((color & 0x000000FF) >> 0);
				data[(y * width + x) * 4 + 3] = 255;
			}
		}
	}

	// 使用宽字符函数打开文件
	FILE* file = nullptr;
	errno_t err = _wfopen_s(&file, filePath.c_str(), L"wb");
	if (err != 0 || file == nullptr)
	{
		delete[] data;
		return false;
	}

	auto writeCallback = [](void* context, void* data, int size)
		{
			FILE* file = static_cast<FILE*>(context);
			size_t written = fwrite(data, 1, size, file);
		};

	stbi_write_png_compression_level = compression_level;
	int result = stbi_write_png_to_func(writeCallback, file, width, height, 4, data, width * 4);

	fclose(file);
	delete[] data;

	if (result == 0) return false;
	return true;
}

//比较图像
bool CompareImagesWithBuffer(IMAGE* img1, IMAGE* img2)
{
	// 检查宽度和高度
	if (img1->getwidth() != img2->getwidth() || img1->getheight() != img2->getheight()) return false;

	DWORD* pBuf1 = GetImageBuffer(img1);
	DWORD* pBuf2 = GetImageBuffer(img2);

	int dataSize = img1->getwidth() * img1->getheight() * sizeof(DWORD);

	return memcmp(pBuf1, pBuf2, dataSize) == 0;
}
//设置图像必须不拥有全透明像素（将所有全透明像素点透明度设置为1）
void SetAlphaToOne(IMAGE* pImg) // pImg是绘图设备指针
{
	// 获取图像缓冲区指针
	DWORD* pBuffer = GetImageBuffer(pImg);
	// 获取图像宽度和高度
	int width = pImg->getwidth();
	int height = pImg->getheight();
	// 遍历每个点
	for (int i = 0; i < width * height; i++)
	{
		// 获取当前点的颜色值（ARGB格式）
		DWORD color = pBuffer[i];
		// 如果透明度为0，则将其设为1
		if ((color >> 24) == 0)
		{
			// 将最高8位设为1，并保持其他位不变
			color = (color & 0x00FFFFFF) | 0x01000000;
			// 将修改后的颜色值写回到图像缓冲区
			pBuffer[i] = color;
		}
	}
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
	if (setlist.paintDevice == 1 || MainMonitor.MonitorPhyHeight == 0 || MainMonitor.MonitorPhyWidth == 0) return 5;
	else return min(0.3f * (float)MainMonitor.MonitorWidth / (float)MainMonitor.MonitorPhyHeight, 0.5f * (float)MainMonitor.MonitorHeight / (float)MainMonitor.MonitorPhyHeight);
}
float drawingScale = 1.0f;
float GetDrawingScale()
{
	return min((float)MainMonitor.MonitorWidth / 1920.0f, (float)MainMonitor.MonitorHeight / 1080.0f);
}