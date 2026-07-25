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
		constexpr float kAntialiasHalfWidthPx = 0.5f;
		constexpr float kTransparentPaddingPx = 2.0f;
		constexpr float kMaximumCursorExtentPx = 256.0f;
		constexpr float kEraserGripStripeOffsetRatio = 0.12f;
		constexpr float kEraserGripStripeWidthRatio = 0.10f;
		constexpr float kEraserGripStripeHalfHeightRatio = 0.24f;

		float SignedDistanceToRectangle(float x, float y, float halfWidth, float halfHeight) noexcept
		{
			const float dx = std::abs(x) - halfWidth;
			const float dy = std::abs(y) - halfHeight;
			const float outside = std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f));
			return outside + std::min(std::max(dx, dy), 0.0f);
		}

		float SignedDistanceToVerticalCapsule(float x, float y,
			float radius, float halfHeight) noexcept
		{
			const float segmentHalfHeight = std::max(halfHeight - radius, 0.0f);
			const float nearestY = std::clamp(y, -segmentHalfHeight, segmentHalfHeight);
			return std::hypot(x, y - nearestY) - radius;
		}

		float CoverageFromSignedDistance(float distance) noexcept
		{
			return std::clamp(kAntialiasHalfWidthPx - distance, 0.0f, 1.0f);
		}

		uint8_t ToByte(float value) noexcept
		{
			return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
		}

		uint32_t PackStraightBgra(float red, float green, float blue, float alpha) noexcept
		{
			alpha = std::clamp(alpha, 0.0f, 1.0f);
			if (alpha <= 0.0f) return 0;
			const uint32_t a = ToByte(alpha);
			const uint32_t r = ToByte(std::clamp(red, 0.0f, 1.0f));
			const uint32_t g = ToByte(std::clamp(green, 0.0f, 1.0f));
			const uint32_t b = ToByte(std::clamp(blue, 0.0f, 1.0f));
			return (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	DrawingCursorBitmap BuildDrawingCursorBitmap(const DrawingCursorAppearance& appearance)
	{
		DrawingCursorBitmap bitmap;
		if (!std::isfinite(appearance.width) || !std::isfinite(appearance.height) ||
			!std::isfinite(appearance.opacity) || !std::isfinite(appearance.fillAlpha) ||
			!std::isfinite(appearance.outlineWidth) || !std::isfinite(appearance.outlineRed) ||
			!std::isfinite(appearance.outlineGreen) || !std::isfinite(appearance.outlineBlue) ||
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
		bitmap.bgra.resize(
			static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height));

		const bool eraserGrip = appearance.shape == DrawingCursorShape::EraserGripCircle;
		// 橡皮主体固定为纯白，避免调用方颜色影响半透明诊断和最终外观。
		const float fillRed = eraserGrip ? 1.0f : std::clamp(appearance.red, 0.0f, 1.0f);
		const float fillGreen = eraserGrip ? 1.0f : std::clamp(appearance.green, 0.0f, 1.0f);
		const float fillBlue = eraserGrip ? 1.0f : std::clamp(appearance.blue, 0.0f, 1.0f);
		const float opacity = std::clamp(appearance.opacity, 0.0f, 1.0f);
		const float fillAlphaRatio = std::clamp(appearance.fillAlpha, 0.0f, 1.0f);
		const float outlineWidth = std::max(appearance.outlineWidth, 0.0f);
		const float outlineRed = std::clamp(appearance.outlineRed, 0.0f, 1.0f);
		const float outlineGreen = std::clamp(appearance.outlineGreen, 0.0f, 1.0f);
		const float outlineBlue = std::clamp(appearance.outlineBlue, 0.0f, 1.0f);
		const float diameter = std::min(appearance.width, appearance.height);
		for (int y = 0; y < bitmap.height; ++y)
		{
			for (int x = 0; x < bitmap.width; ++x)
			{
				const float localX = static_cast<float>(x) - static_cast<float>(bitmap.hotspotX);
				const float localY = static_cast<float>(y) - static_cast<float>(bitmap.hotspotY);
				const float signedDistance = appearance.shape == DrawingCursorShape::Rectangle
					? SignedDistanceToRectangle(localX, localY, halfWidth, halfHeight)
					: std::hypot(localX, localY) - std::min(halfWidth, halfHeight);
				const float outerCoverage = CoverageFromSignedDistance(signedDistance);
				const float innerCoverage = CoverageFromSignedDistance(
					signedDistance + outlineWidth);
				const float outlineCoverage = std::max(0.0f, outerCoverage - innerCoverage);
				const float outlineAlpha = outlineCoverage * opacity;
				const float fillAlpha = innerCoverage * fillAlphaRatio * opacity;
				float alpha = outlineAlpha + fillAlpha;
				float red = 0.0f;
				float green = 0.0f;
				float blue = 0.0f;
				if (alpha > 0.0f)
				{
					red = (outlineAlpha * outlineRed + fillAlpha * fillRed) / alpha;
					green = (outlineAlpha * outlineGreen + fillAlpha * fillGreen) / alpha;
					blue = (outlineAlpha * outlineBlue + fillAlpha * fillBlue) / alpha;
				}
				if (eraserGrip)
				{
					const float stripeRadius = diameter * kEraserGripStripeWidthRatio * 0.5f;
					const float stripeHalfHeight = diameter * kEraserGripStripeHalfHeightRatio;
					float stripeCoverage = 0.0f;
					for (int stripe = -1; stripe <= 1; stripe += 2)
					{
						const float stripeCenter = static_cast<float>(stripe) *
							diameter * kEraserGripStripeOffsetRatio;
						stripeCoverage = std::max(stripeCoverage, CoverageFromSignedDistance(
							SignedDistanceToVerticalCapsule(localX - stripeCenter, localY,
								stripeRadius, stripeHalfHeight)));
					}
					stripeCoverage = std::min(stripeCoverage, innerCoverage);
					// 抓手线直接替换纯 RGB，Alpha 仍作为独立通道保持统一透明度。
					red += stripeCoverage * (outlineRed - red);
					green += stripeCoverage * (outlineGreen - green);
					blue += stripeCoverage * (outlineBlue - blue);
					alpha += stripeCoverage * (opacity - alpha);
				}
				bitmap.bgra[static_cast<size_t>(y) * bitmap.width + x] =
					PackStraightBgra(red, green, blue, alpha);
			}
		}
		return bitmap;
	}

	HCURSOR CreateDrawingCursor(const DrawingCursorAppearance& appearance)
	{
		const DrawingCursorBitmap bitmap = BuildDrawingCursorBitmap(appearance);
		if (bitmap.bgra.empty()) return nullptr;

		BITMAPV5HEADER bitmapHeader = {};
		bitmapHeader.bV5Size = sizeof(BITMAPV5HEADER);
		bitmapHeader.bV5Width = bitmap.width;
		bitmapHeader.bV5Height = bitmap.height;
		bitmapHeader.bV5Planes = 1;
		bitmapHeader.bV5BitCount = 32;
		bitmapHeader.bV5Compression = BI_BITFIELDS;
		// 与微软 Alpha Cursor 示例保持一致，只声明 RGBA masks，不额外引入颜色空间转换。
		bitmapHeader.bV5RedMask = 0x00FF0000;
		bitmapHeader.bV5GreenMask = 0x0000FF00;
		bitmapHeader.bV5BlueMask = 0x000000FF;
		bitmapHeader.bV5AlphaMask = 0xFF000000;
		HDC screenDc = GetDC(nullptr);
		if (!screenDc) return nullptr;
		void* dibBits = nullptr;
		HBITMAP colorBitmap = CreateDIBSection(screenDc,
			reinterpret_cast<const BITMAPINFO*>(&bitmapHeader),
			DIB_RGB_COLORS, &dibBits, nullptr, 0);
		HDC memoryDc = CreateCompatibleDC(screenDc);
		ReleaseDC(nullptr, screenDc);
		if (!colorBitmap || !dibBits || !memoryDc)
		{
			if (memoryDc) DeleteDC(memoryDc);
			if (colorBitmap) DeleteObject(colorBitmap);
			return nullptr;
		}
		HGDIOBJ oldBitmap = SelectObject(memoryDc, colorBitmap);
		if (!oldBitmap || oldBitmap == HGDI_ERROR)
		{
			DeleteDC(memoryDc);
			DeleteObject(colorBitmap);
			return nullptr;
		}
		SelectObject(memoryDc, oldBitmap);
		DeleteDC(memoryDc);

		// 官方示例使用 bottom-up DIB；CPU 栅格保持 top-down，拷贝时显式翻转行序。
		auto* destinationPixels = static_cast<uint32_t*>(dibBits);
		const size_t rowPixelCount = static_cast<size_t>(bitmap.width);
		for (int y = 0; y < bitmap.height; ++y)
		{
			const size_t sourceOffset = static_cast<size_t>(y) * rowPixelCount;
			const size_t destinationOffset =
				static_cast<size_t>(bitmap.height - 1 - y) * rowPixelCount;
			std::memcpy(destinationPixels + destinationOffset,
				bitmap.bgra.data() + sourceOffset, rowPixelCount * sizeof(uint32_t));
		}

		HBITMAP maskBitmap = CreateBitmap(bitmap.width, bitmap.height, 1, 1, nullptr);
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
		GdiFlush();
		HCURSOR cursor = reinterpret_cast<HCURSOR>(CreateIconIndirect(&iconInfo));
		DeleteObject(maskBitmap);
		DeleteObject(colorBitmap);
		return cursor;
	}

	PenCursorDeviceState ResolvePenCursorDeviceState(bool inverted, bool inContact) noexcept
	{
		if (inContact)
			return inverted ? PenCursorDeviceState::InvertedPenContact
				: PenCursorDeviceState::PenContact;
		return inverted ? PenCursorDeviceState::InvertedPenHover
			: PenCursorDeviceState::PenHover;
	}

	bool ShouldHideDrawingCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority) noexcept
	{
		return deviceState == PenCursorDeviceState::PenContact &&
			pointerAuthority != PenCursorPointerAuthority::NonPen;
	}

	bool IsPenCursorContact(PenCursorDeviceState deviceState) noexcept
	{
		return deviceState == PenCursorDeviceState::PenContact ||
			deviceState == PenCursorDeviceState::InvertedPenContact;
	}

	bool ShouldShowEraserCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority, bool eraserTool) noexcept
	{
		const bool penState = deviceState != PenCursorDeviceState::Default;
		const bool inverted = deviceState == PenCursorDeviceState::InvertedPenHover ||
			deviceState == PenCursorDeviceState::InvertedPenContact;
		return penState && pointerAuthority != PenCursorPointerAuthority::NonPen &&
			(eraserTool || inverted);
	}

	bool ShouldShowDrawingCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority, bool toolEligible) noexcept
	{
		return toolEligible && deviceState == PenCursorDeviceState::PenHover &&
			pointerAuthority != PenCursorPointerAuthority::NonPen;
	}
}
