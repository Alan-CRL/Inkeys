#include "../Inkeys/Inkeys/UI/Bar/Bar.Haptics.h"
#include <iostream>
#include <string_view>

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
	const char* rejection = nullptr;
	check(mailbox.Publish(BH::AcceptedClick(down, up, WM_LBUTTONUP, false), context, 1011) &&
		!mailbox.Publish(down, context, 1012, &rejection) &&
		std::string_view(rejection) == "duplicate", "duplicate click reports its rejection");
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
	// 实测 Surface 的 mask=0x4 使用提供方时间；Down/Up 必须完整通过原业务凭据链。
	for (const DWORD mask : { DWORD(TOUCHINPUTMASKF_TIMEFROMSYSTEM), DWORD(TOUCHINPUTMASKF_CONTACTAREA) })
	{
		BH::ContactTokens tracker;
		auto pointer = sample;
		auto input = touch;
		input.dwMask = mask;
		input.dwFlags = TOUCHEVENTF_PEN | TOUCHEVENTF_DOWN;
		tracker.Observe(WM_POINTERDOWN, pointer, context, 2000);
		const auto token = tracker.BindDown(input, pointer.point, 2001);
		check(token != 0, "system/provider time exact Pen Down binds");
		const auto press = tracker.Hold(token);
		++pointer.time; input.dwTime = pointer.time;
		input.dwFlags = TOUCHEVENTF_PEN | TOUCHEVENTF_UP;
		tracker.Observe(WM_POINTERUP, pointer, context, 2010);
		check(tracker.Touch(token, input, pointer.point, true) == token, "system/provider time exact Pen Up seals");
		const auto click = BH::AcceptedClick(press, tracker.Hold(token), WM_LBUTTONUP, false);
		check(mailbox.Publish(click, context, 2011) && mailbox.Take(context, 2012) == press,
			"system/provider contact reaches accepted click mailbox");
		check(!mailbox.Publish(click, context, 2012), "provider time retains submitted de-duplication");
		check(press && !press->Current(context, 2261), "provider time retains Up expiry");
		for (unsigned failure = 0; failure < 6; ++failure)
		{
			BH::ContactTokens rejected;
			auto candidate = sample;
			auto raw = touch;
			raw.dwMask = mask;
			raw.dwFlags = TOUCHEVENTF_PEN | TOUCHEVENTF_DOWN;
			rejected.Observe(WM_POINTERDOWN, candidate, context, 2000);
			if (failure == 0) raw.dwFlags = TOUCHEVENTF_DOWN;
			if (failure == 1) raw.hSource = reinterpret_cast<HANDLE>(99);
			if (failure == 2) ++raw.dwTime;
			POINT point = candidate.point;
			if (failure == 3) ++point.x;
			check(!rejected.BindDown(raw, point, failure == 4 ? 2101 : failure == 5 ? 1999 : 2001),
				"provider time preserves pen/source/time/point/freshness checks");
		}
		{
			BH::ContactTokens ambiguous;
			ambiguous.Observe(WM_POINTERDOWN, sample, context, 2000);
			auto other = sample;
			++other.id;
			ambiguous.Observe(WM_POINTERDOWN, other, context, 2000);
			auto raw = touch;
			raw.dwMask = mask;
			check(!ambiguous.BindDown(raw, sample.point, 2001), "provider time ambiguous candidates rejected");
		}
		// 所有终态反例都先成功绑定 Down，确保实际检验 Up/取消失效链。
		for (unsigned failure = 0; failure < 8; ++failure)
		{
			BH::ContactTokens rejected;
			auto candidate = sample;
			auto raw = touch;
			raw.dwMask = mask;
			raw.dwFlags = TOUCHEVENTF_PEN | TOUCHEVENTF_DOWN;
			rejected.Observe(WM_POINTERDOWN, candidate, context, 2000);
			const auto wireToken = rejected.BindDown(raw, candidate.point, 2001);
			const auto lease = rejected.Hold(wireToken);
			check(wireToken != 0, "negative Up case starts with valid Pen Down");
			++candidate.time; raw.dwTime = candidate.time;
			raw.dwFlags = TOUCHEVENTF_PEN | TOUCHEVENTF_UP;
			if (failure != 0) rejected.Observe(WM_POINTERUP, candidate, context, 2010);
			if (failure == 1) ++raw.dwTime;
			if (failure == 2) raw.hSource = reinterpret_cast<HANDLE>(99);
			if (failure == 3) ++raw.dwID;
			if (failure == 4) raw.dwFlags = TOUCHEVENTF_UP;
			POINT point = candidate.point;
			if (failure == 5) ++point.y;
			if (failure == 6) rejected.Cancel(wireToken);
			if (failure == 7) rejected.Invalidate(candidate.id);
			check(!rejected.Touch(wireToken, raw, point, true) && lease && !lease->Current(context, 2011),
				"provider time missing/mismatched Up or cancel invalidates click");
		}
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
	check(!mailbox.Publish(source(4000), context, 4002, &rejection) &&
		std::string_view(rejection) == "mailbox_full", "full mailbox reports optional feedback rejection");
	pending[0]->valid = false;
	Haptics::ClickTakeDiagnostics dropped;
	check(mailbox.Take(context, 4003, &dropped) == pending[1] && dropped.dropped == 1 &&
		dropped.lastPointerId == pending[0]->pointerId &&
		std::string_view(dropped.lastReason) == "cancelled",
		"cancelled queued click reports its first discarded contact");
	mailbox.Stop();
	check(!mailbox.Take(context, 4004) && !mailbox.Publish(source(4000), context, 4004), "Host stop closes pending clicks");
	mailbox.Start();
	check(!mailbox.Publish(source(4000), {mailbox.Run(), context.canvas}, 4004), "old run cannot cross restart");
	context.run = mailbox.Run();
	check(mailbox.Publish(source(5000), context, 5002), "fresh run accepts new contact");
	check(!mailbox.Take(context, 5252, &dropped) && dropped.dropped == 1 &&
		std::string_view(dropped.lastReason) == "expired",
		"expired queued source reports its drop before backend");
	return failures;
}
