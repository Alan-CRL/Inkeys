#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <windows.h>

import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Drawing.Draw3.pen_cursor;

namespace
{
	using namespace Inkeys::Drawing::Draw3;

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[Draw3Contact] failed: " << name << '\n';
		return condition;
	}

	bool Near(float left, float right) noexcept
	{
		return std::abs(left - right) <= 0.0001f;
	}

	ContactSnapshot MakeSnapshot(float x, float y, ContactPhase phase)
	{
		ContactSnapshot snapshot{};
		snapshot.position = { x, y };
		snapshot.pressure = 0.75f;
		snapshot.qpc = 1;
		snapshot.phase = phase;
		return snapshot;
	}

	void TestContactLifecycle(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		constexpr std::uint32_t tabletContext = 0x31;
		constexpr std::uint32_t contactId = 0x41;
		if (!Expect(input.PublishDown(tabletContext, contactId, InputDeviceType::Pen,
			MakeSnapshot(10.0f, 20.0f, ContactPhase::Down)), "down is published"))
			++failures;

		ContactRecord* record = nullptr;
		if (!Expect(input.TryDequeue(record) && record != nullptr, "down is dequeued"))
		{
			++failures;
			return;
		}
		const ContactHandle handle{ record, record->Generation() };
		if (!Expect(record->DeviceType() == InputDeviceType::Pen &&
			record->DownSnapshot().position.x == 10.0f,
			"down snapshot keeps device and position")) ++failures;

		if (!Expect(input.PublishMove(tabletContext, contactId,
			MakeSnapshot(30.0f, 40.0f, ContactPhase::Move)), "move is published"))
			++failures;
		ContactSnapshot observed{};
		if (!Expect(input.TryReadSnapshot(handle, observed) &&
			observed.phase == ContactPhase::Move && observed.position.x == 30.0f,
			"move snapshot is visible to consumer")) ++failures;

		if (!Expect(input.PublishUp(tabletContext, contactId,
			MakeSnapshot(50.0f, 60.0f, ContactPhase::Up)), "up is published"))
			++failures;
		if (!Expect(input.TryReadSnapshot(handle, observed) &&
			observed.phase == ContactPhase::Up && observed.position.y == 60.0f,
			"terminal snapshot is retained")) ++failures;
		input.Recycle(handle);
		const auto diagnostics = input.DiagnosticsSnapshot();
		if (!Expect(diagnostics.downPublished == 1 && diagnostics.movePublished == 1 &&
			diagnostics.terminalPublished == 1 && diagnostics.recycled == 1 &&
			diagnostics.occupiedSlots == 0,
			"contact slot is recycled exactly once")) ++failures;
	}

	void TestInvalidAndWakeContracts(int& failures)
	{
		ContactInputCoordinator input;
		ContactSnapshot invalid = MakeSnapshot(1.0f, 2.0f, ContactPhase::Down);
		invalid.position.x = (std::numeric_limits<float>::quiet_NaN)();
		if (!Expect(!input.PublishDown(1, 1, InputDeviceType::Touch, invalid),
			"invalid coordinate is rejected")) ++failures;

		if (!Expect(input.PublishControlWake(), "control wake enters mailbox")) ++failures;
		ContactRecord* control = reinterpret_cast<ContactRecord*>(uintptr_t{ 1 });
		if (!Expect(input.TryDequeue(control) && control == nullptr,
			"control wake is distinguishable from contact")) ++failures;
		input.AcknowledgeControlWake();
	}

	void TestCursorOpacityContracts(int& failures)
	{
		DrawingCursorAppearance highlighterAppearance = {
			DrawingCursorShape::Rectangle, 6.25f, 50.0f, 1.0f, 0.2f, 0.8f
		};
		highlighterAppearance.opacity =
			Bridge::kHighlighterCompositeOpacity;
		highlighterAppearance.fillAlpha = 1.0f;
		DrawingCursorAppearance eraserAppearance = {
			DrawingCursorShape::EraserGripCircle, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f
		};
		eraserAppearance.opacity = 0.5f;
		eraserAppearance.fillAlpha = 1.0f;

		DrawingCursorSample penHover = {
			.x = 30.0f, .y = 40.0f, .qpc = 1, .valid = true
		};
		DrawingCursorSample mouseHover = {
			.x = 60.0f, .y = 70.0f, .qpc = 2, .valid = true
		};
		DrawingCursorVisual visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(visual.visible &&
			Near(visual.appearance.opacity, Bridge::kHighlighterCompositeOpacity) &&
			Near(visual.appearance.fillAlpha, 1.0f),
			"highlighter hover keeps Draw3 composite opacity")) ++failures;
		DrawingCursorSample penContact = penHover;
		penContact.inContact = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(!visual.visible,
			"pen contact keeps the default application cursor hidden")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, false, false, false, false);
		if (!Expect(visual.visible &&
			Near(visual.appearance.opacity, Bridge::kHighlighterCompositeOpacity),
			"mouse application cursor keeps highlighter opacity")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(!visual.visible,
			"ordinary mouse hover keeps the system cursor policy")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Touch,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(!visual.visible,
			"touch authority does not create a primary hover cursor")) ++failures;

		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 0.5f),
			"pen eraser hover remains translucent")) ++failures;
		visual = ResolvePrimaryDrawingCursorVisual(
			penContact, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"pen eraser contact becomes opaque")) ++failures;

		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 0.5f),
			"mouse eraser hover remains translucent")) ++failures;
		mouseHover.inContact = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			penHover, mouseHover, DrawingCursorPointerAuthority::Mouse,
			highlighterAppearance, eraserAppearance, true, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"mouse eraser contact becomes opaque")) ++failures;

		DrawingCursorSample invertedHover = penHover;
		invertedHover.inverted = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 0.5f),
			"inverted pen eraser hover remains translucent")) ++failures;
		invertedHover.inContact = true;
		visual = ResolvePrimaryDrawingCursorVisual(
			invertedHover, mouseHover, DrawingCursorPointerAuthority::Pen,
			highlighterAppearance, eraserAppearance, false, false);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"inverted pen eraser contact becomes opaque")) ++failures;
		visual = MakeTouchEraserDrawingCursorVisual(
			100.0f, 150.0f, eraserAppearance);
		if (!Expect(visual.visible && Near(visual.appearance.opacity, 1.0f),
			"touch eraser contact remains opaque")) ++failures;
	}

	void TestTouchCursorOwnership(int& failures)
	{
		const DrawingCursorSample penHover{ .x = 10.0f, .y = 20.0f, .valid = true };
		const DrawingCursorSample mouseHover{ .x = 30.0f, .y = 40.0f, .valid = true };
		const DrawingCursorAppearance eraser{
			DrawingCursorShape::EraserGripCircle, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f };
		// 最后一指 Up 后保持 Touch 视觉归属，旧 Pen/Mouse Hover 不能重新露出。
		const auto touchOwner = ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Pen, true, false, false);
		if (!Expect(touchOwner == DrawingCursorPointerAuthority::Touch &&
			!ResolvePrimaryDrawingCursorVisual(penHover, mouseHover, touchOwner,
				eraser, eraser, true, true).visible &&
			ShouldHideSystemDrawingCursor(touchOwner, false, false, true, true),
			"touch hides old application and system cursors")) ++failures;
		if (!Expect(ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Mouse, true, false, false) ==
			DrawingCursorPointerAuthority::Touch,
			"touch suppresses a stale mouse owner")) ++failures;
		if (!Expect(ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Mouse, true, true, true) ==
			DrawingCursorPointerAuthority::Mouse,
			"real mouse takeover during touch pan remains visible")) ++failures;
		if (!Expect(ResolveDrawingCursorVisualAuthority(
			DrawingCursorPointerAuthority::Mouse, false, false, false) ==
			DrawingCursorPointerAuthority::Mouse &&
			ResolveDrawingCursorVisualAuthority(
				DrawingCursorPointerAuthority::Pen, false, false, false) ==
			DrawingCursorPointerAuthority::Pen,
			"new mouse or pen input restores normal cursor ownership")) ++failures;
		if (!Expect(ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_TOUCH) &&
			ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_PEN) &&
			!ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_MOUSE) &&
			!ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_TOUCHPAD),
			"identified touch and pen compatibility mouse messages are ignored")) ++failures;
		if (!Expect(ShouldIgnoreMouseCursorMessage(true, false, false,
			true, 101u, 100u, IMDT_UNAVAILABLE) &&
			ShouldIgnoreMouseCursorMessage(false, false, false,
			true, 100u, 100u, IMDT_UNAVAILABLE) &&
			!ShouldIgnoreMouseCursorMessage(false, true, false,
			true, 101u, 100u, IMDT_UNAVAILABLE),
			"Win7 compatibility signature and touch barrier remain effective")) ++failures;
		// Touch Up 后来源缺失的原位 Move 不能恢复旧鼠标光标；真正移动仍可接管。
		if (!Expect(ShouldIgnoreUnattributedTouchMouseMove(
			true, IMDT_UNAVAILABLE, true, 2500, 1029, 2500, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				true, IMDT_UNAVAILABLE, true, 2500, 1029, 2501, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				true, IMDT_MOUSE, true, 2500, 1029, 2500, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				false, IMDT_UNAVAILABLE, true, 2500, 1029, 2500, 1029) &&
			!ShouldIgnoreUnattributedTouchMouseMove(
				true, IMDT_UNAVAILABLE, false, 2500, 1029, 2500, 1029),
			"unattributed touch-position move cannot reclaim the cursor")) ++failures;
	}
}

int RunDraw3ContactInputTests()
{
	int failures = 0;
	TestContactLifecycle(failures);
	TestInvalidAndWakeContracts(failures);
	TestCursorOpacityContracts(failures);
	TestTouchCursorOwnership(failures);
	return failures;
}
