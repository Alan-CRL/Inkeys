#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <iostream>
#include <limits>
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

	bool Near(float left, float right) noexcept
	{
		return std::abs(left - right) <= 0.0001f;
	}

#define PEN_CURSOR_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)
}

int RunPenCursorTests()
{
	int failures = 0;
	draw3::DrawingCursorAppearance penAppearance = {
		draw3::DrawingCursorShape::Circle, 10.0f, 10.0f, 1.0f, 0.0f, 0.0f
	};
	draw3::DrawingCursorAppearance laserAppearance = {
		draw3::DrawingCursorShape::Circle, 5.0f, 5.0f, 1.0f, 0.0f, 0.0f
	};
	draw3::DrawingCursorAppearance highlighterAppearance = {
		draw3::DrawingCursorShape::Rectangle, 6.25f, 50.0f, 1.0f, 0.0f, 0.0f
	};
	draw3::DrawingCursorAppearance eraserAppearance = {
		draw3::DrawingCursorShape::EraserGripCircle, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f
	};
	eraserAppearance.opacity = 0.5f;
	eraserAppearance.fillAlpha = 1.0f;
	eraserAppearance.outlineWidth = 2.0f;
	eraserAppearance.outlineRed = 207.0f / 255.0f;
	eraserAppearance.outlineGreen = 207.0f / 255.0f;
	eraserAppearance.outlineBlue = 207.0f / 255.0f;

	PEN_CURSOR_CHECK(draw3::IsValidDrawingCursorAppearance(penAppearance));
	PEN_CURSOR_CHECK(draw3::IsValidDrawingCursorAppearance(laserAppearance));
	PEN_CURSOR_CHECK(draw3::IsValidDrawingCursorAppearance(highlighterAppearance));
	PEN_CURSOR_CHECK(draw3::IsValidDrawingCursorAppearance(eraserAppearance));
	PEN_CURSOR_CHECK(!draw3::IsValidDrawingCursorAppearance({}));

	draw3::DrawingCursorSampleMailbox mailbox;
	draw3::DrawingCursorSample sample = {
		.x = 100.25f,
		.y = 200.5f,
		.qpc = 42,
		.valid = true,
		.inverted = false,
		.inContact = false
	};
	PEN_CURSOR_CHECK(mailbox.Publish(sample));
	draw3::DrawingCursorSample mailboxRead;
	PEN_CURSOR_CHECK(mailbox.Read(mailboxRead));
	PEN_CURSOR_CHECK(mailboxRead.valid);
	PEN_CURSOR_CHECK(Near(mailboxRead.x, sample.x));
	PEN_CURSOR_CHECK(Near(mailboxRead.y, sample.y));
	PEN_CURSOR_CHECK(mailboxRead.qpc == sample.qpc);
	PEN_CURSOR_CHECK((mailboxRead.sequence & 1u) == 0);
	PEN_CURSOR_CHECK(!mailbox.Publish(sample));
	sample.inContact = true;
	PEN_CURSOR_CHECK(mailbox.Publish(sample));
	PEN_CURSOR_CHECK(mailbox.Clear());
	PEN_CURSOR_CHECK(mailbox.Read(mailboxRead));
	PEN_CURSOR_CHECK(!mailboxRead.valid);
	PEN_CURSOR_CHECK(!mailbox.Clear());

	using draw3::DrawingCursorPointerAuthority;
	draw3::DrawingCursorSample penHover = {
		.x = 30.0f, .y = 40.0f, .qpc = 1, .valid = true
	};
	draw3::DrawingCursorSample mouseHover = {
		.x = 60.0f, .y = 70.0f, .qpc = 2, .valid = true
	};

	draw3::DrawingCursorVisual visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penHover, mouseHover, DrawingCursorPointerAuthority::Pen,
		penAppearance, eraserAppearance, false, false);
	PEN_CURSOR_CHECK(visual.visible);
	PEN_CURSOR_CHECK(visual.appearance.shape == draw3::DrawingCursorShape::Circle);
	PEN_CURSOR_CHECK(Near(visual.x, penHover.x));
	PEN_CURSOR_CHECK(Near(visual.appearance.fillAlpha, 1.0f));
	const draw3::DrawingCursorVisual penHoverVisual = visual;
	const draw3::DrawingCursorVisual translucentPenHoverVisual =
		draw3::ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			penAppearance, eraserAppearance, false, false, true, true);
	PEN_CURSOR_CHECK(translucentPenHoverVisual.visible);
	PEN_CURSOR_CHECK(Near(translucentPenHoverVisual.appearance.fillAlpha, 0.5f));

	draw3::DrawingCursorSample penContact = penHover;
	penContact.inContact = true;
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
		penAppearance, eraserAppearance, false, false);
	PEN_CURSOR_CHECK(!visual.visible);
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
		penAppearance, eraserAppearance, false, true);
	PEN_CURSOR_CHECK(draw3::AreDrawingCursorVisualsEquivalent(visual, penHoverVisual));

	const draw3::DrawingCursorVisual highlighterHoverVisual =
		draw3::ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
	PEN_CURSOR_CHECK(highlighterHoverVisual.visible);
	PEN_CURSOR_CHECK(!draw3::ResolvePrimaryDrawingCursorVisual(
		penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
		highlighterAppearance, eraserAppearance, false, false).visible);
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
		highlighterAppearance, eraserAppearance, false, true);
	PEN_CURSOR_CHECK(draw3::AreDrawingCursorVisualsEquivalent(
		visual, highlighterHoverVisual));

	draw3::DrawingCursorSample invertedHover = penHover;
	invertedHover.inverted = true;
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
		penAppearance, eraserAppearance, false, true);
	PEN_CURSOR_CHECK(visual.visible);
	PEN_CURSOR_CHECK(visual.appearance.shape == draw3::DrawingCursorShape::EraserGripCircle);
	PEN_CURSOR_CHECK(Near(visual.appearance.opacity, 1.0f));
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
		penAppearance, eraserAppearance, false, true, true, true);
	PEN_CURSOR_CHECK(Near(visual.appearance.opacity, 0.5f));

	invertedHover.inContact = true;
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
		penAppearance, eraserAppearance, false, true);
	PEN_CURSOR_CHECK(visual.visible);
	PEN_CURSOR_CHECK(Near(visual.appearance.opacity, 1.0f));
	PEN_CURSOR_CHECK(draw3::AreDrawingCursorVisualsEquivalent(
		visual, draw3::ResolvePrimaryDrawingCursorVisual(
			invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			penAppearance, eraserAppearance, false, false)));

	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
		penAppearance, eraserAppearance, false, true);
	PEN_CURSOR_CHECK(!visual.visible);
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
		penAppearance, eraserAppearance, false, true, true, false);
	PEN_CURSOR_CHECK(visual.visible);
	PEN_CURSOR_CHECK(Near(visual.appearance.opacity, 1.0f));
	PEN_CURSOR_CHECK(Near(visual.appearance.fillAlpha, 1.0f));
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
		penAppearance, eraserAppearance, true, true);
	PEN_CURSOR_CHECK(visual.visible);
	PEN_CURSOR_CHECK(Near(visual.x, mouseHover.x));
	PEN_CURSOR_CHECK(Near(visual.appearance.opacity, 0.5f));
	mouseHover.inContact = true;
	PEN_CURSOR_CHECK(!draw3::ResolvePrimaryDrawingCursorVisual(
		penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
		penAppearance, eraserAppearance, false, true).visible);
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
		penAppearance, eraserAppearance, true, true);
	PEN_CURSOR_CHECK(visual.visible);
	PEN_CURSOR_CHECK(Near(visual.appearance.opacity, 1.0f));
	PEN_CURSOR_CHECK(draw3::AreDrawingCursorVisualsEquivalent(
		visual, draw3::ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			penAppearance, eraserAppearance, true, false)));
	PEN_CURSOR_CHECK(!draw3::ResolvePrimaryDrawingCursorVisual(
		penHover, mouseHover, DrawingCursorPointerAuthority::Touch,
		penAppearance, eraserAppearance, true, true).visible);

	// Unknown 仍优先使用有效 Pen；只有该 Pen contact 受新开关控制。
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		penContact, mouseHover, DrawingCursorPointerAuthority::Unknown,
		penAppearance, eraserAppearance, false, true);
	PEN_CURSOR_CHECK(visual.visible);
	PEN_CURSOR_CHECK(Near(visual.x, penContact.x));
	PEN_CURSOR_CHECK(!draw3::ResolvePrimaryDrawingCursorVisual(
		penContact, mouseHover, DrawingCursorPointerAuthority::Unknown,
		penAppearance, eraserAppearance, false, false).visible);
	draw3::DrawingCursorSample absentPen;
	PEN_CURSOR_CHECK(!draw3::ResolvePrimaryDrawingCursorVisual(
		absentPen, mouseHover, DrawingCursorPointerAuthority::Unknown,
		penAppearance, eraserAppearance, false, true).visible);

	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Pen, false, false, true, false));
	PEN_CURSOR_CHECK(!draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Mouse, false, false, false, true));
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Mouse, true, false, false, true));
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Mouse, true, false, false, true, false));
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Touch, true, false, false, false));
	PEN_CURSOR_CHECK(!draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Touch, false, false, false, false));
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Unknown, false, false, true, false));
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Mouse, false, true, false, true));
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Mouse, false, true, false, true, false));
	PEN_CURSOR_CHECK(draw3::ShouldSuppressMouseButtonUpCursorSample(
		DrawingCursorPointerAuthority::Pen));
	PEN_CURSOR_CHECK(draw3::ShouldSuppressMouseButtonUpCursorSample(
		DrawingCursorPointerAuthority::Touch));
	PEN_CURSOR_CHECK(!draw3::ShouldSuppressMouseButtonUpCursorSample(
		DrawingCursorPointerAuthority::Mouse));
	PEN_CURSOR_CHECK(!draw3::ShouldSuppressMouseButtonUpCursorSample(
		DrawingCursorPointerAuthority::Unknown));
	PEN_CURSOR_CHECK(!draw3::ShouldSuppressPenFeedbackForTouchPan(
		false, false, true, true));
	PEN_CURSOR_CHECK(!draw3::ShouldSuppressPenFeedbackForTouchPan(
		true, false, true, false));
	PEN_CURSOR_CHECK(draw3::ShouldSuppressPenFeedbackForTouchPan(
		true, false, true, true));
	PEN_CURSOR_CHECK(draw3::ShouldSuppressPenFeedbackForTouchPan(
		false, true, true, false));
	PEN_CURSOR_CHECK(!draw3::ShouldSuppressPenFeedbackForTouchPan(
		true, true, false, true));
	PEN_CURSOR_CHECK(draw3::ShouldIgnoreMouseCursorMessage(true, true, true));
	PEN_CURSOR_CHECK(!draw3::ShouldIgnoreMouseCursorMessage(false, true, true));
	PEN_CURSOR_CHECK(draw3::ShouldIgnoreMouseCursorMessage(false, false, true));
	PEN_CURSOR_CHECK(!draw3::ShouldIgnoreMouseCursorMessage(false, false, false));

	mouseHover.inContact = false;
	const draw3::DrawingCursorVisual laserMouseHover =
		draw3::ResolveLaserDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse, laserAppearance);
	PEN_CURSOR_CHECK(laserMouseHover.visible);
	PEN_CURSOR_CHECK(Near(laserMouseHover.x, mouseHover.x));
	PEN_CURSOR_CHECK(Near(laserMouseHover.appearance.width, 5.0f));
	PEN_CURSOR_CHECK(Near(laserMouseHover.appearance.width * 0.5f, 2.5f));
	PEN_CURSOR_CHECK(!draw3::ResolveLaserDrawingCursorVisual(
		penContact, mouseHover, DrawingCursorPointerAuthority::Pen, laserAppearance).visible);

	// Pen 离开后 authority 仍拒绝陈旧 Mouse 样本，直到真实鼠标移动切换 authority。
	PEN_CURSOR_CHECK(!draw3::ResolveLaserDrawingCursorVisual(
		absentPen, mouseHover, DrawingCursorPointerAuthority::Pen,
		laserAppearance).visible);
	PEN_CURSOR_CHECK(draw3::ResolveLaserDrawingCursorVisual(
		absentPen, mouseHover, DrawingCursorPointerAuthority::Mouse,
		laserAppearance).visible);
	visual = draw3::ResolvePrimaryDrawingCursorVisual(
		absentPen, mouseHover, DrawingCursorPointerAuthority::Pen,
		penAppearance, eraserAppearance, false, true);
	PEN_CURSOR_CHECK(!visual.visible);
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Pen, false, false, false, true));
	PEN_CURSOR_CHECK(draw3::ShouldHideSystemDrawingCursor(
		DrawingCursorPointerAuthority::Pen, true, false, false, true));

	const draw3::DrawingCursorVisual firstTouch =
		draw3::MakeTouchEraserDrawingCursorVisual(100.0f, 150.0f, eraserAppearance);
	const draw3::DrawingCursorVisual secondTouch =
		draw3::MakeTouchEraserDrawingCursorVisual(300.0f, 350.0f, eraserAppearance);
	PEN_CURSOR_CHECK(firstTouch.visible && secondTouch.visible);
	PEN_CURSOR_CHECK(Near(firstTouch.appearance.opacity, 1.0f));
	PEN_CURSOR_CHECK(!draw3::AreDrawingCursorVisualsEquivalent(firstTouch, secondTouch));
	PEN_CURSOR_CHECK(draw3::AreDrawingCursorVisualsEquivalent(firstTouch, firstTouch));

	const RECT touchBounds = draw3::DrawingCursorVisualBounds(firstTouch, 500, 500);
	PEN_CURSOR_CHECK(touchBounds.left == 73);
	PEN_CURSOR_CHECK(touchBounds.top == 123);
	PEN_CURSOR_CHECK(touchBounds.right == 127);
	PEN_CURSOR_CHECK(touchBounds.bottom == 177);
	const RECT clippedBounds = draw3::DrawingCursorVisualBounds(
		draw3::MakeTouchEraserDrawingCursorVisual(2.0f, 2.0f, eraserAppearance), 500, 500);
	PEN_CURSOR_CHECK(clippedBounds.left == 0 && clippedBounds.top == 0);
	draw3::DrawingCursorVisual extremeVisual = firstTouch;
	extremeVisual.x = (std::numeric_limits<float>::max)();
	PEN_CURSOR_CHECK(draw3::DrawingCursorVisualBounds(
		extremeVisual, 500, 500).left ==
		draw3::DrawingCursorVisualBounds(extremeVisual, 500, 500).right);
	extremeVisual.x = (std::numeric_limits<float>::quiet_NaN)();
	const RECT invalidBounds = draw3::DrawingCursorVisualBounds(extremeVisual, 500, 500);
	PEN_CURSOR_CHECK(invalidBounds.left == invalidBounds.right &&
		invalidBounds.top == invalidBounds.bottom);

	if (failures == 0) std::cout << "All pen cursor tests passed." << std::endl;
	return failures;
}
