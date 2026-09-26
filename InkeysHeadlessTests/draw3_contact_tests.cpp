#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>

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

	void TestPageAdmissionAndQuarantine(int& failures)
	{
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		ContactRecord* record = nullptr;
		auto dequeueContact = [&]() -> ContactHandle
		{
			while (input.TryDequeue(record))
			{
				if (record) return { record, record->Generation() };
				input.AcknowledgeControlWake();
			}
			return {};
		};
		input.PublishDown(1, 1, InputDeviceType::Pen, MakeSnapshot(10, 20, ContactPhase::Down));
		const auto old = dequeueContact();
		if (!Expect(input.ContactAdmitted(old), "initial Down enters current page")) ++failures;
		input.SetAdmissionBlocked(true);
		if (!Expect(!input.ContactAdmitted(old), "page boundary invalidates queued old Down")) ++failures;
		input.DiscardUntilTerminal(old);
		if (!Expect(input.HasQuarantinedContacts() && input.DiagnosticsSnapshot().occupiedSlots == 1 &&
			!input.PublishMove(1, 1, MakeSnapshot(50, 60, ContactPhase::Move)),
			"old contact holds its route and ignores moves after sealing")) ++failures;
		input.PublishDown(1, 2, InputDeviceType::Touch, MakeSnapshot(30, 40, ContactPhase::Down));
		const auto during = dequeueContact();
		input.SetAdmissionBlocked(false);
		if (!Expect(!input.ContactAdmitted(during), "UI ack cannot admit a Down made while closed")) ++failures;
		input.DiscardUntilTerminal(during);
		input.PublishDown(1, 3, InputDeviceType::Pen, MakeSnapshot(70, 80, ContactPhase::Down));
		const auto fresh = dequeueContact();
		if (!Expect(input.ContactAdmitted(fresh), "new Down after UI ack is admitted")) ++failures;
		input.PublishUp(1, 1, MakeSnapshot(100, 110, ContactPhase::Up));
		input.PublishCancelled(1, 2, MakeSnapshot(100, 110, ContactPhase::Cancelled));
		if (!Expect(!input.HasQuarantinedContacts() && input.DiagnosticsSnapshot().recycled == 2 &&
			input.ContactAdmitted(fresh), "old terminals retire exactly their own routes")) ++failures;
		input.PublishUp(1, 3, MakeSnapshot(90, 100, ContactPhase::Up));
		input.Recycle(fresh);
		if (!Expect(input.DiagnosticsSnapshot().occupiedSlots == 0,
			"all page boundary slots are returned without waiting for another frame")) ++failures;

		// 真实 producer/consumer 并发覆盖 Up 抢先与隔离抢先两条 ownership 路径。
		for (int iteration = 0; iteration < 128; ++iteration)
		{
			input.PublishDown(2, 1, InputDeviceType::Pen, MakeSnapshot(1, 2, ContactPhase::Down));
			const auto concurrent = dequeueContact();
			std::jthread terminal([&]() { input.PublishUp(2, 1, MakeSnapshot(3, 4, ContactPhase::Up)); });
			input.DiscardUntilTerminal(concurrent);
			terminal.join();
			if (!Expect(input.DiagnosticsSnapshot().occupiedSlots == 0 && !input.HasQuarantinedContacts(),
				"terminal versus quarantine race neither leaks nor double-recycles")) { ++failures; break; }
		}
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
}

int RunDraw3ContactInputTests()
{
	int failures = 0;
	TestContactLifecycle(failures);
	TestInvalidAndWakeContracts(failures);
	TestPageAdmissionAndQuarantine(failures);
	TestCursorOpacityContracts(failures);
	return failures;
}
