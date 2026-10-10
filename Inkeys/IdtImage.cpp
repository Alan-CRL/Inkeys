#include "IdtImage.h"

//drawpad画笔
Inkeys::Graphics::DibSurface alpha_drawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //临时画板
Inkeys::Graphics::DibSurface putout; //主画板上叠加的控件内容
Inkeys::Graphics::DibSurface tester(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //图形绘制画板
Inkeys::Graphics::DibSurface pptdrawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //PPT控件画板

int recall_image_recond, recall_image_reference;
shared_mutex RecallImageManipulatedSm;
chrono::high_resolution_clock::time_point RecallImageManipulated;

tm RecallImageTm;
int RecallImagePeak = 0;
deque<RecallStruct> RecallImage;//撤回栈

//悬浮窗
Inkeys::Graphics::DibSurface background(576, 386);
namespace
{
	HDC GetBackgroundGraphicsDc()
	{
		// 全局 Graphics 构造前先启动 GDI+，并让其析构早于进程级 GDI+ 生命周期。
		(void)Inkeys::Graphics::Detail::EnsureGdiplusReady();
		return background.dc();
	}
}
Graphics graphics(GetBackgroundGraphicsDc());

Bitmap* SurfaceToBitmap(const Inkeys::Graphics::DibSurface* surface)
{
	if (!surface || surface->empty() || !Inkeys::Graphics::Detail::EnsureGdiplusReady()) {
		return nullptr;
	}

	const int width = surface->width();
	const int height = surface->height();

	// 创建 GDI+ Bitmap
	Gdiplus::Bitmap* bitmap = nullptr;
	try
	{
		bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppPARGB);
	}
	catch (...)
	{
		return nullptr;
	}
	if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
		delete bitmap;
		return nullptr;
	}

	// 锁定 GDI+ Bitmap 的数据
	Gdiplus::BitmapData bitmapData{};
	Gdiplus::Rect rect(0, 0, width, height);
	if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &bitmapData) != Gdiplus::Ok) {
		delete bitmap;
		return nullptr;
	}

	// Surface 为 top-down BGRA，按行复制以兼容 GDI+ 的 stride。
	const auto pixels = surface->pixels();
	for (int y = 0; y < height; ++y)
	{
		const auto* sourceRow = reinterpret_cast<const BYTE*>(pixels.data() + static_cast<size_t>(y) * width);
		auto* targetRow = static_cast<BYTE*>(bitmapData.Scan0) + static_cast<ptrdiff_t>(y) * bitmapData.Stride;
		memcpy(targetRow, sourceRow, static_cast<size_t>(width) * sizeof(Inkeys::Graphics::DibSurface::Pixel));
	}
	bitmap->UnlockBits(&bitmapData);

	return bitmap;
}
bool CopySurface(Inkeys::Graphics::DibSurface* target, const Inkeys::Graphics::DibSurface* source)
{
	if (!target || !source) return false;
	try
	{
		*target = *source;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

shared_mutex loadImageSm;
bool LoadSurfaceFromFile(Inkeys::Graphics::DibSurface* destination, LPCTSTR path, int width, int height)
{
	if (!destination || !path) return false;
	lock_guard loadImageLock(loadImageSm);
	return destination->loadFromFile(path, width, height);
}
bool LoadSurfaceFromResource(Inkeys::Graphics::DibSurface* destination, LPCTSTR resourceType, LPCTSTR resourceName, int width, int height)
{
	if (!destination || !resourceType || !resourceName) return false;
	lock_guard loadImageLock(loadImageSm);
	return destination->loadFromResource(GetModuleHandleW(nullptr), resourceType, resourceName, width, height);
}
