#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <iostream>
#include <windows.h>

import draw3.pen_cursor;

namespace
{
	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED pen cursor line " << line << ": " << expression << std::endl;
	}

	uint8_t Alpha(uint32_t pixel) noexcept
	{
		return static_cast<uint8_t>(pixel >> 24);
	}

	uint8_t Red(uint32_t pixel) noexcept
	{
		return static_cast<uint8_t>(pixel >> 16);
	}

	uint8_t Green(uint32_t pixel) noexcept
	{
		return static_cast<uint8_t>(pixel >> 8);
	}

	uint8_t Blue(uint32_t pixel) noexcept
	{
		return static_cast<uint8_t>(pixel);
	}

	uint32_t PixelAt(const draw3::DrawingCursorBitmap& bitmap, int x, int y)
	{
		return bitmap.premultipliedBgra[
			static_cast<size_t>(y) * static_cast<size_t>(bitmap.width) + static_cast<size_t>(x)];
	}

#define PEN_CURSOR_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)
}

int RunPenCursorTests()
{
	int failures = 0;
	draw3::DrawingCursorAppearance penAppearance = {
		draw3::DrawingCursorShape::Circle, 5.0f, 5.0f, 1.0f, 0.0f, 0.0f
	};
	const draw3::DrawingCursorBitmap pen = draw3::BuildDrawingCursorBitmap(penAppearance);
	PEN_CURSOR_CHECK(pen.width == 11);
	PEN_CURSOR_CHECK(pen.height == 11);
	PEN_CURSOR_CHECK(pen.hotspotX == 5);
	PEN_CURSOR_CHECK(pen.hotspotY == 5);
	PEN_CURSOR_CHECK(pen.premultipliedBgra.size() == 121);
	const uint32_t penCenter = PixelAt(pen, 5, 5);
	PEN_CURSOR_CHECK(Alpha(penCenter) == 128);
	PEN_CURSOR_CHECK(Red(penCenter) == 128);
	PEN_CURSOR_CHECK(Green(penCenter) == 0);
	PEN_CURSOR_CHECK(Blue(penCenter) == 0);
	const uint32_t penOutline = PixelAt(pen, 7, 5);
	PEN_CURSOR_CHECK(Alpha(penOutline) == 255);
	PEN_CURSOR_CHECK(Red(penOutline) == 74);
	PEN_CURSOR_CHECK(Green(penOutline) == 74);
	PEN_CURSOR_CHECK(Blue(penOutline) == 74);
	PEN_CURSOR_CHECK(Alpha(PixelAt(pen, 8, 5)) == 0);
	PEN_CURSOR_CHECK(Alpha(PixelAt(pen, 0, 0)) == 0);

	const draw3::DrawingCursorAppearance highlighterAppearance = {
		draw3::DrawingCursorShape::Rectangle, 6.25f, 50.0f, 1.0f, 0.0f, 0.0f
	};
	const draw3::DrawingCursorBitmap highlighter =
		draw3::BuildDrawingCursorBitmap(highlighterAppearance);
	PEN_CURSOR_CHECK(highlighter.width == 13);
	PEN_CURSOR_CHECK(highlighter.height == 55);
	PEN_CURSOR_CHECK(highlighter.hotspotX == 6);
	PEN_CURSOR_CHECK(highlighter.hotspotY == 27);
	const uint32_t highlighterCenter = PixelAt(highlighter, 6, 27);
	PEN_CURSOR_CHECK(Alpha(highlighterCenter) == 128);
	PEN_CURSOR_CHECK(Red(highlighterCenter) == 128);
	const uint32_t fractionalEdge = PixelAt(highlighter, 9, 27);
	PEN_CURSOR_CHECK(Alpha(fractionalEdge) > 0 && Alpha(fractionalEdge) < 255);
	PEN_CURSOR_CHECK(Red(fractionalEdge) == Green(fractionalEdge));
	PEN_CURSOR_CHECK(Green(fractionalEdge) == Blue(fractionalEdge));
	PEN_CURSOR_CHECK(Alpha(PixelAt(highlighter, 10, 27)) == 0);
	PEN_CURSOR_CHECK(Alpha(PixelAt(highlighter, 6, 52)) == 128);

	const draw3::DrawingCursorBitmap invalid = draw3::BuildDrawingCursorBitmap({});
	PEN_CURSOR_CHECK(invalid.premultipliedBgra.empty());
	HCURSOR nativeCursor = draw3::CreateDrawingCursor(penAppearance);
	PEN_CURSOR_CHECK(nativeCursor != nullptr);
	if (nativeCursor) DestroyCursor(nativeCursor);

	using draw3::PenCursorDeviceState;
	using draw3::PenCursorPointerAuthority;
	PEN_CURSOR_CHECK(draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::Pen, PenCursorPointerAuthority::Unknown, true));
	PEN_CURSOR_CHECK(draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::Pen, PenCursorPointerAuthority::Pen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::Pen, PenCursorPointerAuthority::NonPen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::Default, PenCursorPointerAuthority::Unknown, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::InvertedPen, PenCursorPointerAuthority::Pen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::Pen, PenCursorPointerAuthority::Pen, false));

	if (failures == 0) std::cout << "All pen cursor tests passed." << std::endl;
	return failures;
}
