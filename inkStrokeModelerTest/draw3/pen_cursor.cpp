module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>

module draw3.pen_cursor;

namespace draw3
{
	namespace
	{
		constexpr float kCursorOutlineWidthPx = 1.0f;
		constexpr float kCursorFillAlpha = 0.5f;
		constexpr float kCursorOutlineComponent = 74.0f / 255.0f;
		constexpr float kAntialiasHalfWidthPx = 0.5f;
		constexpr float kTransparentPaddingPx = 2.0f;
		constexpr float kMaximumCursorExtentPx = 256.0f;

		float SignedDistanceToRectangle(float x, float y, float halfWidth, float halfHeight) noexcept
		{
			const float dx = std::abs(x) - halfWidth;
			const float dy = std::abs(y) - halfHeight;
			const float outside = std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f));
			return outside + std::min(std::max(dx, dy), 0.0f);
		}

		float CoverageFromSignedDistance(float distance) noexcept
		{
			return std::clamp(kAntialiasHalfWidthPx - distance, 0.0f, 1.0f);
		}

		uint8_t ToByte(float value) noexcept
		{
			return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
		}

		uint32_t PackPremultipliedBgra(float red, float green, float blue, float alpha) noexcept
		{
			const uint32_t a = ToByte(alpha);
			const uint32_t r = ToByte(std::min(red, alpha));
			const uint32_t g = ToByte(std::min(green, alpha));
			const uint32_t b = ToByte(std::min(blue, alpha));
			return (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	DrawingCursorBitmap BuildDrawingCursorBitmap(const DrawingCursorAppearance& appearance)
	{
		DrawingCursorBitmap bitmap;
		if (!std::isfinite(appearance.width) || !std::isfinite(appearance.height) ||
			appearance.width <= 0.0f || appearance.height <= 0.0f ||
			appearance.width > kMaximumCursorExtentPx ||
			appearance.height > kMaximumCursorExtentPx) return bitmap;

		const float halfWidth = appearance.width * 0.5f;
		const float halfHeight = appearance.height * 0.5f;
		const int bitmapHalfWidth = static_cast<int>(std::ceil(halfWidth + kTransparentPaddingPx));
		const int bitmapHalfHeight = static_cast<int>(std::ceil(halfHeight + kTransparentPaddingPx));
		bitmap.width = bitmapHalfWidth * 2 + 1;
		bitmap.height = bitmapHalfHeight * 2 + 1;
		bitmap.hotspotX = static_cast<uint32_t>(bitmapHalfWidth);
		bitmap.hotspotY = static_cast<uint32_t>(bitmapHalfHeight);
		bitmap.premultipliedBgra.resize(
			static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height));

		const float fillRed = std::clamp(appearance.red, 0.0f, 1.0f);
		const float fillGreen = std::clamp(appearance.green, 0.0f, 1.0f);
		const float fillBlue = std::clamp(appearance.blue, 0.0f, 1.0f);
		for (int y = 0; y < bitmap.height; ++y)
		{
			for (int x = 0; x < bitmap.width; ++x)
			{
				const float localX = static_cast<float>(x) - static_cast<float>(bitmap.hotspotX);
				const float localY = static_cast<float>(y) - static_cast<float>(bitmap.hotspotY);
				const float signedDistance = appearance.shape == DrawingCursorShape::Circle
					? std::hypot(localX, localY) - std::min(halfWidth, halfHeight)
					: SignedDistanceToRectangle(localX, localY, halfWidth, halfHeight);
				const float outerCoverage = CoverageFromSignedDistance(signedDistance);
				const float innerCoverage = CoverageFromSignedDistance(
					signedDistance + kCursorOutlineWidthPx);
				const float outlineCoverage = std::max(0.0f, outerCoverage - innerCoverage);
				const float fillAlpha = innerCoverage * kCursorFillAlpha;
				const float alpha = outlineCoverage + fillAlpha;
				const float red = outlineCoverage * kCursorOutlineComponent + fillAlpha * fillRed;
				const float green = outlineCoverage * kCursorOutlineComponent + fillAlpha * fillGreen;
				const float blue = outlineCoverage * kCursorOutlineComponent + fillAlpha * fillBlue;
				bitmap.premultipliedBgra[static_cast<size_t>(y) * bitmap.width + x] =
					PackPremultipliedBgra(red, green, blue, alpha);
			}
		}
		return bitmap;
	}

	HCURSOR CreateDrawingCursor(const DrawingCursorAppearance& appearance)
	{
		const DrawingCursorBitmap bitmap = BuildDrawingCursorBitmap(appearance);
		if (bitmap.premultipliedBgra.empty()) return nullptr;

		BITMAPINFO bitmapInfo = {};
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = bitmap.width;
		bitmapInfo.bmiHeader.biHeight = -bitmap.height; // top-down DIB 与 CPU 位图行序一致。
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;
		void* dibBits = nullptr;
		HBITMAP colorBitmap = CreateDIBSection(nullptr, &bitmapInfo,
			DIB_RGB_COLORS, &dibBits, nullptr, 0);
		if (!colorBitmap || !dibBits)
		{
			if (colorBitmap) DeleteObject(colorBitmap);
			return nullptr;
		}
		std::memcpy(dibBits, bitmap.premultipliedBgra.data(),
			bitmap.premultipliedBgra.size() * sizeof(uint32_t));

		const size_t maskStride = static_cast<size_t>((bitmap.width + 15) / 16) * 2;
		std::vector<uint8_t> maskBits(maskStride * static_cast<size_t>(bitmap.height), 0);
		HBITMAP maskBitmap = CreateBitmap(bitmap.width, bitmap.height, 1, 1, maskBits.data());
		if (!maskBitmap)
		{
			DeleteObject(colorBitmap);
			return nullptr;
		}

		ICONINFO iconInfo = {};
		iconInfo.fIcon = FALSE;
		iconInfo.xHotspot = bitmap.hotspotX;
		iconInfo.yHotspot = bitmap.hotspotY;
		iconInfo.hbmMask = maskBitmap;
		iconInfo.hbmColor = colorBitmap;
		HCURSOR cursor = reinterpret_cast<HCURSOR>(CreateIconIndirect(&iconInfo));
		DeleteObject(maskBitmap);
		DeleteObject(colorBitmap);
		return cursor;
	}

	bool ShouldShowDrawingCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority, bool toolEligible) noexcept
	{
		return toolEligible && deviceState == PenCursorDeviceState::Pen &&
			pointerAuthority != PenCursorPointerAuthority::NonPen;
	}
}
