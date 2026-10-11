#include "../Inkeys/Inkeys/UI/Bar/Bar.Haptics.h"
#include <iostream>

import Inkeys.Drawing.Draw3.haptic_feedback;

int RunDraw3HapticTests()
{
	using namespace Inkeys::Drawing::Draw3;
	namespace BH = Inkeys::UI::Bar::Haptics;
	int failures = RunPenHapticBackendTests();
	auto check = [&](bool result, const char* name)
	{ if (!result) { ++failures; std::cerr << "FAILED pen haptics: " << name << '\n'; } };
	check(ResolveContinuousHapticFeedback(HapticToolFeedback::Pen) == HapticContinuousFeedback::InkContinuous &&
		ResolveContinuousHapticFeedback(HapticToolFeedback::Highlighter) == HapticContinuousFeedback::ChiselMarkerContinuous &&
		ResolveContinuousHapticFeedback(HapticToolFeedback::Eraser) == HapticContinuousFeedback::EraserContinuous,
		"production tool waveforms");
	check(Haptics::ContinuousAllowed(false, true, false, false, false, false, false), "eligible continuous pen");
	for (unsigned boundary = 0; boundary < 7; ++boundary)
	{
		check(!Haptics::ContinuousAllowed(boundary == 0, boundary != 1, boundary == 2,
			boundary == 3, boundary == 4, boundary == 5, boundary == 6),
			"selection/hidden/laser/admission/quarantine/pan/suppression stops preset");
	}
	for (unsigned token = 0; token <= BH::SlotCount; ++token)
		for (unsigned flags = 0; flags < 4; ++flags)
		{
			const auto marker = BH::Marker(token, flags & 1, flags & 2);
			check(BH::IsMarker(WM_LBUTTONDOWN, marker) && BH::Token(WM_LBUTTONUP, marker) == token &&
				((marker - BH::MarkerBase) & 3) == static_cast<int>(flags), "marker preserves source/cancel/screen");
			check(!BH::IsMarker(WM_MOUSEWHEEL, marker) && !BH::IsMarker(WM_MOUSEHWHEEL, marker) &&
				!BH::IsMarker(WM_KEYDOWN, marker), "wheel and union data never become contact tokens");
			check(BH::Token(WM_MOUSEMOVE, BH::Marker(BH::Token(WM_MOUSEMOVE, marker), flags & 1, false)) == token,
				"screen normalization and Seek preserve token");
		}
	for (const auto message : { WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE })
		check(!BH::IsMarker(message, 0) && !BH::IsMarker(message, -1) &&
			!BH::IsMarker(message, 32767) && !BH::IsMarker(message, static_cast<short>(BH::Marker(BH::SlotCount, true, true) + 1)),
			"ordinary mouse data and marker upper bound never become touch metadata");
	Haptics::ClickMailbox mailbox;
	mailbox.Start();
	Haptics::Context context{ mailbox.Run(), 9 };
	BH::PointerSample sample{ 11, 33, reinterpret_cast<HANDLE>(44), {10,20}, 100 };
	TOUCHINPUT touch{};
	touch.dwID = 7; // 不把 TOUCHINPUT ID 当 Pointer ID。
	touch.hSource = sample.device;
	touch.dwTime = sample.time;
	touch.dwMask = TOUCHINPUTMASKF_TIMEFROMSYSTEM;
	touch.dwFlags = TOUCHEVENTF_PEN | TOUCHEVENTF_DOWN;
	BH::ContactTokens tokens;
	tokens.Observe(WM_POINTERDOWN, sample, context, 1000);
	const auto wire = tokens.BindDown(touch, sample.point, 1001);
	check(wire != 0, "strict same source/time/point associates independent touch ID");
	tokens.Enqueued(wire);
	auto down = tokens.Dequeued(wire);
	sample.time = touch.dwTime = 101;
	touch.dwFlags = TOUCHEVENTF_PEN | TOUCHEVENTF_UP;
	tokens.Observe(WM_POINTERUP, sample, context, 1010);
	check(tokens.Touch(wire, touch, sample.point, true) == wire, "matching Pen Up seals contact");
	tokens.Enqueued(wire);
	auto up = tokens.Dequeued(wire);
	check(!BH::AcceptedClick(down, up, WM_MOUSEMOVE, false) && !BH::AcceptedClick(down, up, WM_LBUTTONUP, true) &&
		!BH::AcceptedClick({}, up, WM_LBUTTONUP, false), "move/cancel/mouse/touch cannot authorize click");
	check(mailbox.Publish(BH::AcceptedClick(down, up, WM_LBUTTONUP, false), context, 1011) &&
		!mailbox.Publish(down, context, 1012), "one accepted click at most once");
	check(mailbox.Take(context, 1012) == down && !mailbox.Take(context, 1012), "one owner consumption");
	check(!down->Current({context.run, context.canvas + 1}, 1012), "newer canvas presence rejects old Bar click");
	check(!down->Current(context, 1261), "UI stall expires click without changing operation");
	tokens.Invalidate(sample.id);
	check(!down->Current(context, 1012), "leave/cancel invalidates already consumed source");
	// 队列和按下 session 分别持有旧代际；短 token 不得提前复用。
	tokens.Enqueued(wire);
	sample.time = touch.dwTime = 102;
	tokens.Observe(WM_POINTERDOWN, sample, context, 1020);
	const auto second = tokens.BindDown(touch, sample.point, 1021);
	check(second && second != wire && tokens.Dequeued(wire) == down, "queued tombstone cannot alias new pointer lifetime");
	tokens.Enqueued(second);
	auto held = tokens.Dequeued(second);
	tokens.Invalidate();
	tokens.Cleared();
	sample.time = touch.dwTime = 103;
	tokens.Observe(WM_POINTERDOWN, sample, context, 1030);
	check(tokens.BindDown(touch, sample.point, 1031) != second, "held press keeps slot after queue clear");
	{
		BH::AcceptedClickScope scope(held);
		check(BH::acceptedClick == held, "real callback has scoped source");
	}
	check(!BH::acceptedClick, "programmatic callback has no source");
	for (unsigned failure = 0; failure < 6; ++failure)
	{
		BH::ContactTokens tracker;
		auto pointer = sample;
		auto input = touch;
		tracker.Observe(WM_POINTERDOWN, pointer, context, 2000);
		if (failure == 0) input.dwFlags = TOUCHEVENTF_DOWN;
		if (failure == 1) input.dwMask = 0;
		if (failure == 2) input.hSource = reinterpret_cast<HANDLE>(99);
		if (failure == 3) ++input.dwTime;
		POINT point = pointer.point;
		if (failure == 4) ++point.x;
		check(!tracker.BindDown(input, point, failure == 5 ? 2101 : 2001),
			"touch/untrusted time/different device/time/position/stale association drops metadata");
	}
	{
		BH::ContactTokens tracker;
		tracker.Observe(WM_POINTERDOWN, sample, context, 2000);
		auto ambiguous = sample;
		++ambiguous.id;
		tracker.Observe(WM_POINTERDOWN, ambiguous, context, 2000);
		check(!tracker.BindDown(touch, sample.point, 2001), "ambiguous pen candidates rejected");
	}
	{
		BH::ContactTokens tracker;
		std::array<Haptics::Click, BH::SlotCount> leases;
		for (unsigned i = 0; i < leases.size(); ++i)
		{
			++sample.time; touch.dwTime = sample.time;
			tracker.Observe(WM_POINTERDOWN, sample, context, 3000 + i);
			const auto token = tracker.BindDown(touch, sample.point, 3000 + i);
			leases[i] = tracker.Hold(token);
		}
		++sample.time; touch.dwTime = sample.time;
		tracker.Observe(WM_POINTERDOWN, sample, context, 3010);
		check(!tracker.BindDown(touch, sample.point, 3011), "full token table only drops optional metadata");
		leases = {};
		tracker.Cleared();
		tracker.Observe(WM_POINTERDOWN, sample, context, 3020);
		check(tracker.BindDown(touch, sample.point, 3021) != 0, "drained source slots reusable");
	}
	auto source = [&](std::uint64_t now)
	{
		auto value = std::make_shared<Haptics::ClickSource>();
		value->context = context; value->pointerId = 11; value->downAt = now; value->upAt = now + 1;
		return value;
	};
	std::array<Haptics::Click, 8> pending;
	for (auto& item : pending) { item = source(4000); check(mailbox.Publish(item, context, 4002), "bounded mailbox accepts free slot"); }
	check(!mailbox.Publish(source(4000), context, 4002), "full mailbox rejects only feedback");
	pending[0]->valid = false;
	check(mailbox.Take(context, 4003) == pending[1], "cancellation bypasses full queue");
	mailbox.Stop();
	check(!mailbox.Take(context, 4004) && !mailbox.Publish(source(4000), context, 4004), "Host stop closes pending clicks");
	mailbox.Start();
	check(!mailbox.Publish(source(4000), {mailbox.Run(), context.canvas}, 4004), "old run cannot cross restart");
	context.run = mailbox.Run();
	check(mailbox.Publish(source(5000), context, 5002), "fresh run accepts new contact");
	check(!mailbox.Take(context, 5252), "expired queued source never reaches backend");
	return failures;
}
