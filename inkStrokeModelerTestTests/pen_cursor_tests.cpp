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
		return bitmap.bgra[
			static_cast<size_t>(y) * static_cast<size_t>(bitmap.width) + static_cast<size_t>(x)];
	}

#define PEN_CURSOR_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)
}

int RunPenCursorTests()
{
	int failures = 0;
	draw3::DrawingCursorAppearance penAppearance = {
		draw3::DrawingCursorShape::Circle, 6.0f, 6.0f, 1.0f, 0.0f, 0.0f
	};
	const draw3::DrawingCursorBitmap pen = draw3::BuildDrawingCursorBitmap(penAppearance);
	PEN_CURSOR_CHECK(pen.width == 11);
	PEN_CURSOR_CHECK(pen.height == 11);
	PEN_CURSOR_CHECK(pen.hotspotX == 5);
	PEN_CURSOR_CHECK(pen.hotspotY == 5);
	PEN_CURSOR_CHECK(pen.bgra.size() == 121);
	const uint32_t penCenter = PixelAt(pen, 5, 5);
	PEN_CURSOR_CHECK(Alpha(penCenter) == 128);
	PEN_CURSOR_CHECK(Red(penCenter) == 255);
	PEN_CURSOR_CHECK(Green(penCenter) == 0);
	PEN_CURSOR_CHECK(Blue(penCenter) == 0);
	const uint32_t penInnerOutline = PixelAt(pen, 7, 5);
	PEN_CURSOR_CHECK(Alpha(penInnerOutline) == 128);
	const uint32_t penOutline = PixelAt(pen, 8, 5);
	PEN_CURSOR_CHECK(Alpha(penOutline) == 128);
	PEN_CURSOR_CHECK(Red(penOutline) == 184);
	PEN_CURSOR_CHECK(Green(penOutline) == 184);
	PEN_CURSOR_CHECK(Blue(penOutline) == 184);
	PEN_CURSOR_CHECK(Alpha(PixelAt(pen, 9, 5)) == 0);
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
	PEN_CURSOR_CHECK(Red(highlighterCenter) == 255);
	const uint32_t fractionalEdge = PixelAt(highlighter, 9, 27);
	PEN_CURSOR_CHECK(Alpha(fractionalEdge) > 0 && Alpha(fractionalEdge) < 255);
	PEN_CURSOR_CHECK(Red(fractionalEdge) > Green(fractionalEdge));
	PEN_CURSOR_CHECK(Green(fractionalEdge) == Blue(fractionalEdge));
	PEN_CURSOR_CHECK(Alpha(PixelAt(highlighter, 10, 27)) == 0);
	PEN_CURSOR_CHECK(Alpha(PixelAt(highlighter, 6, 52)) == 128);

	draw3::DrawingCursorAppearance eraserHoverAppearance = {
		draw3::DrawingCursorShape::EraserGripCircle, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f
	};
	eraserHoverAppearance.opacity = 0.75f;
	eraserHoverAppearance.fillAlpha = 1.0f;
	eraserHoverAppearance.outlineWidth = 2.0f;
	eraserHoverAppearance.outlineRed = 128.0f / 255.0f;
	eraserHoverAppearance.outlineGreen = 128.0f / 255.0f;
	eraserHoverAppearance.outlineBlue = 128.0f / 255.0f;
	const draw3::DrawingCursorBitmap eraserHover =
		draw3::BuildDrawingCursorBitmap(eraserHoverAppearance);
	PEN_CURSOR_CHECK(eraserHover.width == 55);
	PEN_CURSOR_CHECK(eraserHover.height == 55);
	PEN_CURSOR_CHECK(eraserHover.hotspotX == 27);
	PEN_CURSOR_CHECK(eraserHover.hotspotY == 27);
	const uint32_t eraserWhite = PixelAt(eraserHover, 32, 27);
	PEN_CURSOR_CHECK(Alpha(eraserWhite) == 191);
	PEN_CURSOR_CHECK(Red(eraserWhite) == 255);
	PEN_CURSOR_CHECK(Green(eraserWhite) == 255);
	PEN_CURSOR_CHECK(Blue(eraserWhite) == 255);
	const uint32_t eraserStripe = PixelAt(eraserHover, 27, 27);
	PEN_CURSOR_CHECK(Alpha(eraserStripe) == 239);
	PEN_CURSOR_CHECK(Red(eraserStripe) == 153);
	PEN_CURSOR_CHECK(Green(eraserStripe) == 153);
	PEN_CURSOR_CHECK(Blue(eraserStripe) == 153);
	const uint32_t eraserRing = PixelAt(eraserHover, 51, 27);
	PEN_CURSOR_CHECK(Alpha(eraserRing) == 191);
	PEN_CURSOR_CHECK(Red(eraserRing) == 128);
	PEN_CURSOR_CHECK(Green(eraserRing) == 128);
	PEN_CURSOR_CHECK(Blue(eraserRing) == 128);
	eraserHoverAppearance.opacity = 1.0f;
	const draw3::DrawingCursorBitmap eraserContact =
		draw3::BuildDrawingCursorBitmap(eraserHoverAppearance);
	PEN_CURSOR_CHECK(Alpha(PixelAt(eraserContact, 32, 27)) == 255);
	PEN_CURSOR_CHECK(Red(PixelAt(eraserContact, 32, 27)) == 255);
	PEN_CURSOR_CHECK(Alpha(PixelAt(eraserContact, 27, 27)) == 255);
	PEN_CURSOR_CHECK(Red(PixelAt(eraserContact, 27, 27)) == 128);
	const draw3::DrawingCursorBitmap fractionalEraser =
		draw3::BuildDrawingCursorBitmap({
			draw3::DrawingCursorShape::EraserGripCircle, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f
		});
	PEN_CURSOR_CHECK(!fractionalEraser.bgra.empty());

	const draw3::DrawingCursorBitmap invalid = draw3::BuildDrawingCursorBitmap({});
	PEN_CURSOR_CHECK(invalid.bgra.empty());
	HCURSOR nativeCursor = draw3::CreateDrawingCursor(penAppearance);
	PEN_CURSOR_CHECK(nativeCursor != nullptr);
	if (nativeCursor) DestroyCursor(nativeCursor);

	using draw3::PenCursorDeviceState;
	using draw3::PenCursorPointerAuthority;
	PEN_CURSOR_CHECK(draw3::ResolvePenCursorDeviceState(false, false) ==
		PenCursorDeviceState::PenHover);
	PEN_CURSOR_CHECK(draw3::ResolvePenCursorDeviceState(true, false) ==
		PenCursorDeviceState::InvertedPenHover);
	PEN_CURSOR_CHECK(draw3::ResolvePenCursorDeviceState(false, true) ==
		PenCursorDeviceState::PenContact);
	PEN_CURSOR_CHECK(draw3::ResolvePenCursorDeviceState(true, true) ==
		PenCursorDeviceState::InvertedPenContact);
	PEN_CURSOR_CHECK(draw3::ShouldHideDrawingCursor(
		PenCursorDeviceState::PenContact, PenCursorPointerAuthority::Unknown));
	PEN_CURSOR_CHECK(!draw3::ShouldHideDrawingCursor(
		PenCursorDeviceState::InvertedPenContact, PenCursorPointerAuthority::Pen));
	PEN_CURSOR_CHECK(!draw3::ShouldHideDrawingCursor(
		PenCursorDeviceState::PenContact, PenCursorPointerAuthority::NonPen));
	PEN_CURSOR_CHECK(!draw3::ShouldHideDrawingCursor(
		PenCursorDeviceState::PenHover, PenCursorPointerAuthority::Pen));
	PEN_CURSOR_CHECK(draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::PenHover, PenCursorPointerAuthority::Unknown, true));
	PEN_CURSOR_CHECK(draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::PenHover, PenCursorPointerAuthority::Pen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::PenHover, PenCursorPointerAuthority::NonPen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::Default, PenCursorPointerAuthority::Unknown, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::InvertedPenHover, PenCursorPointerAuthority::Pen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::PenContact, PenCursorPointerAuthority::Pen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::InvertedPenContact, PenCursorPointerAuthority::Pen, true));
	PEN_CURSOR_CHECK(!draw3::ShouldShowDrawingCursor(
		PenCursorDeviceState::PenHover, PenCursorPointerAuthority::Pen, false));
	PEN_CURSOR_CHECK(draw3::ShouldShowEraserCursor(
		PenCursorDeviceState::PenHover, PenCursorPointerAuthority::Unknown, true));
	PEN_CURSOR_CHECK(draw3::ShouldShowEraserCursor(
		PenCursorDeviceState::PenContact, PenCursorPointerAuthority::Pen, true));
	PEN_CURSOR_CHECK(draw3::ShouldShowEraserCursor(
		PenCursorDeviceState::InvertedPenHover, PenCursorPointerAuthority::Pen, false));
	PEN_CURSOR_CHECK(draw3::ShouldShowEraserCursor(
		PenCursorDeviceState::InvertedPenContact, PenCursorPointerAuthority::Pen, false));
	PEN_CURSOR_CHECK(!draw3::ShouldShowEraserCursor(
		PenCursorDeviceState::PenHover, PenCursorPointerAuthority::NonPen, true));
	PEN_CURSOR_CHECK(draw3::IsPenCursorContact(PenCursorDeviceState::PenContact));
	PEN_CURSOR_CHECK(draw3::IsPenCursorContact(PenCursorDeviceState::InvertedPenContact));
	PEN_CURSOR_CHECK(!draw3::IsPenCursorContact(PenCursorDeviceState::PenHover));

	if (failures == 0) std::cout << "All pen cursor tests passed." << std::endl;
	return failures;
}
