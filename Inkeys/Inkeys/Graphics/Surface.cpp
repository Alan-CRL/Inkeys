#include "Surface.Core.hpp"

#include <gdiplus.h>
#include <objidl.h>

#include <algorithm>
#include <cwchar>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	using Inkeys::Graphics::Detail::DibSurfaceImpl;

	class GdiplusLifetime
	{
	public:
		GdiplusLifetime() noexcept
		{
			Gdiplus::GdiplusStartupInput input;
			ready_ = Gdiplus::GdiplusStartup(&token_, &input, nullptr) == Gdiplus::Ok;
		}

		~GdiplusLifetime()
		{
			if (ready_)
				Gdiplus::GdiplusShutdown(token_);
		}

		[[nodiscard]] bool ready() const noexcept { return ready_; }

	private:
		ULONG_PTR token_ = 0;
		bool ready_ = false;
	};

	[[nodiscard]] int ResolveDimension(int requested, UINT natural) noexcept
	{
		return requested > 0 ? requested : static_cast<int>(natural);
	}

	[[nodiscard]] bool DrawBitmap(
		DibSurfaceImpl& destination,
		Gdiplus::Bitmap& bitmap) noexcept
	{
		if (destination.empty() || bitmap.GetLastStatus() != Gdiplus::Ok)
			return false;

		Gdiplus::Graphics graphics(destination.dc());
		graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
		graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		return graphics.DrawImage(
			&bitmap,
			Gdiplus::Rect(0, 0, destination.width(), destination.height()),
			0,
			0,
			static_cast<INT>(bitmap.GetWidth()),
			static_cast<INT>(bitmap.GetHeight()),
			Gdiplus::UnitPixel) == Gdiplus::Ok;
	}

	[[nodiscard]] bool GetPngEncoderClsid(CLSID& clsid) noexcept
	{
		UINT count = 0;
		UINT bytes = 0;
		if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
			return false;

		auto buffer = std::unique_ptr<std::byte[]>(new (std::nothrow) std::byte[bytes]);
		if (!buffer)
			return false;
		auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.get());
		if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok)
			return false;

		for (UINT index = 0; index < count; ++index)
		{
			if (encoders[index].MimeType && wcscmp(encoders[index].MimeType, L"image/png") == 0)
			{
				clsid = encoders[index].Clsid;
				return true;
			}
		}
		return false;
	}
}

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#endif

namespace Inkeys::Graphics::Detail
{
	bool EnsureGdiplusReady() noexcept
	{
		// 函数内静态对象保证所有加载、保存和 GDI+ 绘制共享同一生命周期。
		static GdiplusLifetime lifetime;
		return lifetime.ready();
	}

	DibSurfaceImpl::DibSurfaceImpl(int width, int height)
	{
		if (!create(width, height))
			throw std::runtime_error("CreateDIBSection failed");
	}

	DibSurfaceImpl::DibSurfaceImpl(const DibSurfaceImpl& other)
	{
		if (other.empty())
			return;
		if (!create(other.width_, other.height_))
			throw std::runtime_error("CreateDIBSection failed");
		std::memcpy(bits_, other.bits_, pixels().size_bytes());
	}

	DibSurfaceImpl::DibSurfaceImpl(DibSurfaceImpl&& other) noexcept
	{
		swap(other);
	}

	DibSurfaceImpl& DibSurfaceImpl::operator=(DibSurfaceImpl other) noexcept
	{
		swap(other);
		return *this;
	}

	DibSurfaceImpl::~DibSurfaceImpl()
	{
		reset();
	}

	bool DibSurfaceImpl::create(int width, int height) noexcept
	{
		if (width <= 0 || height <= 0)
			return false;

		HDC screenDc = GetDC(nullptr);
		HDC newDc = CreateCompatibleDC(screenDc);
		if (screenDc)
			ReleaseDC(nullptr, screenDc);
		if (!newDc)
			return false;

		BITMAPINFO bitmapInfo{};
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = width;
		// 负高度创建 top-down DIB，像素 span 与屏幕坐标同向。
		bitmapInfo.bmiHeader.biHeight = -height;
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;

		void* newBits = nullptr;
		HBITMAP newBitmap = CreateDIBSection(newDc, &bitmapInfo, DIB_RGB_COLORS, &newBits, nullptr, 0);
		if (!newBitmap || !newBits)
		{
			if (newBitmap)
				DeleteObject(newBitmap);
			DeleteDC(newDc);
			return false;
		}

		HGDIOBJ previous = SelectObject(newDc, newBitmap);
		if (!previous || previous == HGDI_ERROR)
		{
			DeleteObject(newBitmap);
			DeleteDC(newDc);
			return false;
		}

		width_ = width;
		height_ = height;
		dc_ = newDc;
		bitmap_ = newBitmap;
		previousObject_ = previous;
		bits_ = static_cast<Pixel*>(newBits);
		clear();
		return true;
	}

	bool DibSurfaceImpl::resize(int width, int height) noexcept
	{
		if (width == width_ && height == height_ && !empty())
			return true;
		DibSurfaceImpl replacement;
		if (!replacement.create(width, height))
			return false;
		swap(replacement);
		return true;
	}

	void DibSurfaceImpl::reset() noexcept
	{
		if (dc_ && previousObject_)
			SelectObject(dc_, previousObject_);
		if (bitmap_)
			DeleteObject(bitmap_);
		if (dc_)
			DeleteDC(dc_);
		width_ = 0;
		height_ = 0;
		dc_ = nullptr;
		bitmap_ = nullptr;
		previousObject_ = nullptr;
		bits_ = nullptr;
	}

	void DibSurfaceImpl::swap(DibSurfaceImpl& other) noexcept
	{
		using std::swap;
		swap(width_, other.width_);
		swap(height_, other.height_);
		swap(dc_, other.dc_);
		swap(bitmap_, other.bitmap_);
		swap(previousObject_, other.previousObject_);
		swap(bits_, other.bits_);
	}

	std::span<DibSurfaceImpl::Pixel> DibSurfaceImpl::pixels() noexcept
	{
		return bits_ ? std::span<Pixel>(bits_, static_cast<std::size_t>(width_) * height_) : std::span<Pixel>{};
	}

	std::span<const DibSurfaceImpl::Pixel> DibSurfaceImpl::pixels() const noexcept
	{
		return bits_ ? std::span<const Pixel>(bits_, static_cast<std::size_t>(width_) * height_) : std::span<const Pixel>{};
	}

	void DibSurfaceImpl::clear(Pixel bgra) noexcept
	{
		std::fill(pixels().begin(), pixels().end(), bgra);
	}

	bool DibSurfaceImpl::equals(const DibSurfaceImpl& other) const noexcept
	{
		return width_ == other.width_ && height_ == other.height_ &&
			pixels().size_bytes() == other.pixels().size_bytes() &&
			(pixels().empty() || std::memcmp(bits_, other.bits_, pixels().size_bytes()) == 0);
	}

	bool DibSurfaceImpl::composite(
		const DibSurfaceImpl& source,
		int destinationX,
		int destinationY,
		BYTE opacity) noexcept
	{
		RECT destination{ destinationX, destinationY, destinationX + source.width_, destinationY + source.height_ };
		RECT sourceRect{ 0, 0, source.width_, source.height_ };
		return compositeScaled(source, destination, sourceRect, opacity);
	}

	bool DibSurfaceImpl::compositeScaled(
		const DibSurfaceImpl& source,
		const RECT& destination,
		const RECT& sourceRect,
		BYTE opacity) noexcept
	{
		if (empty() || source.empty())
			return false;
		const int destinationWidth = destination.right - destination.left;
		const int destinationHeight = destination.bottom - destination.top;
		const int sourceWidth = sourceRect.right - sourceRect.left;
		const int sourceHeight = sourceRect.bottom - sourceRect.top;
		if (destinationWidth <= 0 || destinationHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
			return false;

		BLENDFUNCTION blend{ AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA };
		return AlphaBlend(
			dc_, destination.left, destination.top, destinationWidth, destinationHeight,
			source.dc_, sourceRect.left, sourceRect.top, sourceWidth, sourceHeight, blend) != FALSE;
	}

	bool DibSurfaceImpl::loadFromFile(
		std::wstring_view path,
		int requestedWidth,
		int requestedHeight) noexcept
	{
		if (!EnsureGdiplusReady())
			return false;
		try
		{
			const std::wstring pathCopy(path);
			Gdiplus::Bitmap bitmap(pathCopy.c_str(), FALSE);
			if (bitmap.GetLastStatus() != Gdiplus::Ok)
				return false;
			DibSurfaceImpl replacement;
			if (!replacement.create(
				ResolveDimension(requestedWidth, bitmap.GetWidth()),
				ResolveDimension(requestedHeight, bitmap.GetHeight())) ||
				!DrawBitmap(replacement, bitmap))
				return false;
			swap(replacement);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool DibSurfaceImpl::loadFromResource(
		HINSTANCE instance,
		LPCWSTR resourceType,
		LPCWSTR resourceName,
		int requestedWidth,
		int requestedHeight) noexcept
	{
		if (!EnsureGdiplusReady())
			return false;
		HRSRC resource = FindResourceW(instance, resourceName, resourceType);
		if (!resource)
			return false;
		const DWORD size = SizeofResource(instance, resource);
		HGLOBAL loaded = LoadResource(instance, resource);
		const void* bytes = loaded ? LockResource(loaded) : nullptr;
		if (!bytes || size == 0)
			return false;

		HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
		if (!copy)
			return false;
		void* destination = GlobalLock(copy);
		if (!destination)
		{
			GlobalFree(copy);
			return false;
		}
		std::memcpy(destination, bytes, size);
		GlobalUnlock(copy);

		IStream* stream = nullptr;
		if (FAILED(CreateStreamOnHGlobal(copy, TRUE, &stream)))
		{
			GlobalFree(copy);
			return false;
		}
		DibSurfaceImpl replacement;
		bool loadedSuccessfully = false;
		{
			// GDI+ 要求流在 Bitmap 的整个生命周期内保持有效。
			Gdiplus::Bitmap bitmap(stream, FALSE);
			loadedSuccessfully = bitmap.GetLastStatus() == Gdiplus::Ok &&
				replacement.create(
					ResolveDimension(requestedWidth, bitmap.GetWidth()),
					ResolveDimension(requestedHeight, bitmap.GetHeight())) &&
				DrawBitmap(replacement, bitmap);
		}
		stream->Release();
		if (!loadedSuccessfully)
			return false;
		swap(replacement);
		return true;
	}

	bool DibSurfaceImpl::savePng(std::wstring_view path) const noexcept
	{
		if (empty() || !EnsureGdiplusReady())
			return false;
		CLSID encoder{};
		if (!GetPngEncoderClsid(encoder))
			return false;
		try
		{
			Gdiplus::Bitmap bitmap(
				width_,
				height_,
				width_ * static_cast<int>(sizeof(Pixel)),
				PixelFormat32bppPARGB,
				reinterpret_cast<BYTE*>(bits_));
			if (bitmap.GetLastStatus() != Gdiplus::Ok)
				return false;
			const std::wstring pathCopy(path);
			return bitmap.Save(pathCopy.c_str(), &encoder, nullptr) == Gdiplus::Ok;
		}
		catch (...)
		{
			return false;
		}
	}
}
