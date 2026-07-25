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
		constexpr float kEraserGripStripeOffsetRatio = 0.18f;
		constexpr float kEraserGripStripeWidthRatio = 0.025f;
		constexpr float kEraserGripStripeHalfHeightRatio = 0.35f;

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

		uint32_t PackStraightBgra(float premultipliedRed, float premultipliedGreen,
			float premultipliedBlue, float alpha) noexcept
		{
			if (alpha <= 0.0f) return 0;
			const uint32_t a = ToByte(alpha);
			// Windows 彩色光标会自行应用 Alpha；这里反预乘，避免颜色被二次衰减。
			const uint32_t r = ToByte(premultipliedRed / alpha);
			const uint32_t g = ToByte(premultipliedGreen / alpha);
			const uint32_t b = ToByte(premultipliedBlue / alpha);
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

		const float fillRed = std::clamp(appearance.red, 0.0f, 1.0f);
		const float fillGreen = std::clamp(appearance.green, 0.0f, 1.0f);
		const float fillBlue = std::clamp(appearance.blue, 0.0f, 1.0f);
		const float opacity = std::clamp(appearance.opacity, 0.0f, 1.0f);
		const float fillAlphaRatio = std::clamp(appearance.fillAlpha, 0.0f, 1.0f);
		const float outlineWidth = std::max(appearance.outlineWidth, 0.0f);
		const float outlineRed = std::clamp(appearance.outlineRed, 0.0f, 1.0f);
		const float outlineGreen = std::clamp(appearance.outlineGreen, 0.0f, 1.0f);
		const float outlineBlue = std::clamp(appearance.outlineBlue, 0.0f, 1.0f);
		const float diameter = std::min(appearance.width, appearance.height);
		const bool eraserGrip = appearance.shape == DrawingCursorShape::EraserGripCircle;
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
				const float fillAlpha = innerCoverage * fillAlphaRatio * opacity;
				float alpha = (outlineCoverage * opacity) + fillAlpha;
				float red = (outlineCoverage * outlineRed * opacity) + fillAlpha * fillRed;
				float green = (outlineCoverage * outlineGreen * opacity) + fillAlpha * fillGreen;
				float blue = (outlineCoverage * outlineBlue * opacity) + fillAlpha * fillBlue;
				if (eraserGrip)
				{
					const float stripeHalfWidth = diameter * kEraserGripStripeWidthRatio * 0.5f;
					const float stripeHalfHeight = diameter * kEraserGripStripeHalfHeightRatio;
					float stripeCoverage = 0.0f;
					for (int stripe = -1; stripe <= 1; ++stripe)
					{
						const float stripeCenter = static_cast<float>(stripe) *
							diameter * kEraserGripStripeOffsetRatio;
						stripeCoverage = std::max(stripeCoverage, CoverageFromSignedDistance(
							SignedDistanceToRectangle(localX - stripeCenter, localY,
								stripeHalfWidth, stripeHalfHeight)));
					}
					stripeCoverage = std::min(stripeCoverage, innerCoverage);
					const float stripeAlpha = stripeCoverage * opacity;
					red = stripeAlpha * outlineRed + red * (1.0f - stripeAlpha);
					green = stripeAlpha * outlineGreen + green * (1.0f - stripeAlpha);
					blue = stripeAlpha * outlineBlue + blue * (1.0f - stripeAlpha);
					alpha = stripeAlpha + alpha * (1.0f - stripeAlpha);
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
		std::memcpy(dibBits, bitmap.bgra.data(),
			bitmap.bgra.size() * sizeof(uint32_t));

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
