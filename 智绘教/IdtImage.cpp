#include "IdtImage.h"

//drawpad画笔
IMAGE alpha_drawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //临时画板
IMAGE putout; //主画板上叠加的控件内容
IMAGE tester(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //图形绘制画板
IMAGE pptdrawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //PPT控件画板

int recall_image_recond, recall_image_reference;
shared_mutex RecallImageManipulatedSm;
chrono::high_resolution_clock::time_point RecallImageManipulated;

tm RecallImageTm;
int RecallImagePeak = 0;
deque<RecallStruct> RecallImage;//撤回栈

//悬浮窗
IMAGE background(576, 386);
Graphics graphics(GetImageHDC(&background));

Bitmap* IMAGEToBitmap(IMAGE* easyXImage)
{
	if (!easyXImage || easyXImage->getwidth() <= 0 || easyXImage->getheight() <= 0) {
		return nullptr;
	}

	// 获取 EasyX 图像的信息
	int width = easyXImage->getwidth();
	int height = easyXImage->getheight();
	int channels = 4;  // 假设 EasyX 图像使用 32 位 ARGB 格式

	// 创建 GDI+ Bitmap
	Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
	if (!bitmap) {
		return nullptr;
	}

	// 锁定 GDI+ Bitmap 的数据
	Gdiplus::BitmapData bitmapData;
	Gdiplus::Rect rect(0, 0, width, height);
	bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bitmapData);

	// 将 EasyX 图像数据复制到 GDI+ Bitmap
	DWORD* srcData = GetImageBuffer(easyXImage);
	BYTE* destData = static_cast<BYTE*>(bitmapData.Scan0);

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			// 每个像素有四个字节，分别是 B, G, R, A
			destData[4 * (y * width + x) + 0] = GetRValue(srcData[(y * width + x)]);  // Blue
			destData[4 * (y * width + x) + 1] = GetGValue(srcData[(y * width + x)]);  // Green
			destData[4 * (y * width + x) + 2] = GetBValue(srcData[(y * width + x)]);  // Red
			destData[4 * (y * width + x) + 3] = (srcData[(y * width + x)] >> 24) & 0xFF;  // Alpha
		}
	}

	// 解锁 GDI+ Bitmap 的数据
	bitmap->UnlockBits(&bitmapData);

	return bitmap;
}
bool ImgCpy(IMAGE* tag, IMAGE* src)
{
	if (tag == NULL || src == NULL) return false;
	if (tag->getwidth() != src->getwidth() || tag->getheight() != src->getheight())
	{
		tag->Resize(src->getwidth(), src->getheight());
	}

	int width = src->getwidth();
	int height = src->getheight();
	DWORD* pSrc = GetImageBuffer(src);
	DWORD* pTag = GetImageBuffer(tag);

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			pTag[y * width + x] = pSrc[y * width + x];
		}
	}

	return true;
}