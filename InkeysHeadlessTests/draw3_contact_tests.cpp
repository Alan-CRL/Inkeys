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

	void TestSystemTouchMoveFilter(int& failures)
	{
		MouseCursorMessageFilterInput input{
			.buttonDown = true, .pointerApiAvailable = true,
			.touchBarrierKnown = true, .messageTick = 118939656u, .touchBarrierTick = 118939218u,
			.sourceQuerySucceeded = true, .origin = IMO_SYSTEM, .touchSuppressed = true,
			.touchPositionKnown = true, .touchX = 2034, .touchY = 811, .mouseX = 2034, .mouseY = 811
		};
		// 回放 event=51 的按键态与 event=60 的非按键态，并覆盖多指使末点不同/不可得。
		for (int contact = 0; contact < 2; ++contact)
		{
			input.buttonDown = contact != 0;
			for (int position = 0; position < 3; ++position)
			{
				input.touchPositionKnown = position != 2;
				input.mouseX = position == 0 ? 2034 : 2359;
				const auto result = FilterMouseCursorMessage(input);
				if (!Expect(result.rejectionReason && result.systemRejected && !result.buttonBypass,
					"system move cannot reclaim touch with buttons or a different last contact")) ++failures;
			}
		}

		input.touchPositionKnown = true;
		input.mouseX = input.touchX;
		input.buttonDown = true;
		input.inputSource = IMDT_MOUSE;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"identified mouse can take over at the same touch position")) ++failures;
		input.inputSource = IMDT_TOUCHPAD;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"identified touchpad can take over")) ++failures;
		input.inputSource = IMDT_PEN;
		if (!Expect(FilterMouseCursorMessage(input).sourceRejected,
			"pen compatibility mouse still cannot publish a mouse sample")) ++failures;
		input.inputSource = IMDT_TOUCH;
		if (!Expect(FilterMouseCursorMessage(input).sourceRejected,
			"touch compatibility mouse still cannot publish a mouse sample")) ++failures;

		input.inputSource = IMDT_UNAVAILABLE;
		input.touchSuppressed = false;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"system move retains existing mouse behavior after confirmed takeover")) ++failures;
		input.touchSuppressed = true;
		input.sourceQuerySucceeded = false;
		input.pointerApiAvailable = false;
		if (!Expect(!FilterMouseCursorMessage(input).systemRejected &&
			!FilterMouseCursorMessage(input).rejectionReason,
			"missing or failed source API is not classified as system input")) ++failures;
		input.buttonDown = false;
		if (!Expect(FilterMouseCursorMessage(input).positionRejected,
			"Win7 stationary unknown move retains the existing fallback")) ++failures;
		++input.mouseX;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"Win7 actual mouse movement retains the existing takeover path")) ++failures;
		input.sourceQuerySucceeded = true;
		input.pointerApiAvailable = true;
		input.origin = IMO_INJECTED;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"application injection is not blanket classified as system touch move")) ++failures;
		input.origin = IMO_SYSTEM;
		input.message = WM_LBUTTONDOWN;
		if (!Expect(!FilterMouseCursorMessage(input).rejectionReason,
			"system move rule does not alter button message policy")) ++failures;
	}

	void TestSystemTouchMoveSequence(int& failures)
	{
		DrawingCursorSampleMailbox mouseMailbox;
		DrawingCursorPointerAuthority persistentOwner = DrawingCursorPointerAuthority::Mouse;
		bool touchSuppressed = true; // RTS Down 清旧样本，Up 不恢复旧归属。
		mouseMailbox.Publish({ .x = 30.0f, .y = 40.0f, .valid = true });
		mouseMailbox.Clear();
		MouseCursorMessageFilterInput input{
			.buttonDown = true, .pointerApiAvailable = true,
			.touchBarrierKnown = true, .messageTick = 118939656u, .touchBarrierTick = 118939218u,
			.sourceQuerySucceeded = true, .origin = IMO_SYSTEM,
			.touchPositionKnown = true, .touchX = 2034, .touchY = 811, .mouseX = 2034, .mouseY = 811
		};
		// 测试生产过滤入口到 mailbox/visual 的组合；不模拟 Windows 的消息来源 API。
		const auto receiveMouse = [&]()
		{
			input.touchSuppressed = touchSuppressed;
			const auto decision = FilterMouseCursorMessage(input);
			if (decision.rejectionReason) return false;
			touchSuppressed = false;
			persistentOwner = DrawingCursorPointerAuthority::Mouse;
			mouseMailbox.Publish({ .x = static_cast<float>(input.mouseX),
				.y = static_cast<float>(input.mouseY), .valid = true, .inContact = input.buttonDown });
			return true;
		};
		const DrawingCursorAppearance eraser{
			DrawingCursorShape::EraserGripCircle, 64.0f, 64.0f, 1.0f, 1.0f, 1.0f, 0.5f };
		const auto primaryVisual = [&]()
		{
			DrawingCursorSample mouse;
			mouseMailbox.Read(mouse);
			return ResolvePrimaryDrawingCursorVisual({}, mouse,
				ResolveDrawingCursorVisualAuthority(persistentOwner, touchSuppressed, false, false),
				eraser, eraser, true, true);
		};
		if (!Expect(!receiveMouse() && !primaryVisual().visible &&
			MakeTouchEraserDrawingCursorVisual(2035.0f, 811.5f, eraser).visible,
			"event 51 leaves only the active touch eraser visual")) ++failures;
		// Touch Up 的兼容 Mouse Up 仍被拒绝，不能给错误的 Mouse 样本补一次释放。
		input.message = WM_LBUTTONUP;
		input.buttonDown = false;
		input.inputSource = IMDT_TOUCH;
		input.promotedPointerMessage = true;
		input.messageTick = 118939718u;
		if (!Expect(!receiveMouse() && !primaryVisual().visible,
			"touch up has no pressed primary cursor to leave behind")) ++failures;
		input.message = WM_MOUSEMOVE;
		input.inputSource = IMDT_UNAVAILABLE;
		input.promotedPointerMessage = false;
		input.messageTick = 118942828u;
		input.mouseX = input.touchX = 2359;
		input.mouseY = input.touchY = 762;
		if (!Expect(!receiveMouse() && !primaryVisual().visible && touchSuppressed,
			"event 60 does not resurrect a hover cursor after touch up")) ++failures;
		input.inputSource = IMDT_MOUSE;
		input.origin = IMO_HARDWARE;
		++input.messageTick;
		if (!Expect(receiveMouse() && primaryVisual().visible &&
			Near(primaryVisual().appearance.opacity, 0.5f),
			"real mouse immediately recovers normal hover at the same position")) ++failures;
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
	TestSystemTouchMoveFilter(failures);
	TestSystemTouchMoveSequence(failures);
	return failures;
}
