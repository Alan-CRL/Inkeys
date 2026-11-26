#include "../Inkeys/Inkeys/Input/UiPointerInput.h"
#include "../Inkeys/Inkeys/Input/UiTouchRouting.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.PointerInput.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Scroll.h"
#include <cmath>
#include <array>
#include <cfloat>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
	namespace PI = Inkeys::UI::Setting::PointerInput;
	namespace Touch = Inkeys::Input::UiPointer::Touch;
	using PI::Kind;
	using PI::Source;
	using Verdict = PI::ContactArbiter::Verdict;
	using EventType = PI::PointerEvent::Type;

	int failures = 0;
	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAIL setting_pointer: " << name << '\n';
	}

	PI::Sample Make(Kind kind, std::uintptr_t device, DWORD contact, LONG x = 10, LONG y = 20, Source source = Source::Touch)
	{
		PI::Sample sample;
		sample.kind = kind; sample.device = device; sample.contact = contact;
		sample.screen = { x, y }; sample.source = source;
		return sample;
	}

	void TestArbiter()
	{
		PI::ContactArbiter arbiter;
		Check(arbiter.Accept(Make(Kind::Down, 1, 5), false).verdict == Verdict::Ignore, "non-primary first down is ignored");
		auto result = arbiter.Accept(Make(Kind::Down, 1, 5), true);
		Check(result.verdict == Verdict::Begin && !result.replaced && arbiter.Latest().generation == 1, "primary down begins generation 1");
		Check(arbiter.Accept(Make(Kind::Down, 1, 6), false).verdict == Verdict::Ignore && arbiter.Latest().contact == 5,
			"second finger is swallowed while a contact is active");
		Check(arbiter.Accept(Make(Kind::Down, 2, 7, 0, 0, Source::Pen), true).verdict == Verdict::Ignore && arbiter.Latest().contact == 5,
			"pen down on another device is swallowed while touch is active");
		Check(arbiter.Accept(Make(Kind::Move, 1, 6), false).verdict == Verdict::Ignore, "move of a swallowed contact is ignored");
		result = arbiter.Accept(Make(Kind::Move, 1, 5, 30, 40), true);
		Check(result.verdict == Verdict::Update && arbiter.Latest().screen.x == 30 && arbiter.Latest().generation == 1, "owned move updates");
		result = arbiter.Accept(Make(Kind::Up, 1, 5, 31, 41), true);
		Check(result.verdict == Verdict::End && !arbiter.Active() && arbiter.Latest().screen.y == 41, "owned up ends the contact");
		Check(arbiter.Accept(Make(Kind::Up, 1, 5), true).verdict == Verdict::Ignore, "late up after end is ignored");

		(void)arbiter.Accept(Make(Kind::Down, 1, 8), true);
		result = arbiter.Accept(Make(Kind::Down, 1, 8), true);
		Check(result.replaced && result.replacedLatest.kind == Kind::Cancel && result.replacedLatest.contact == 8
			&& result.verdict == Verdict::Begin && arbiter.Latest().generation == 3, "same pointer id down replaces the stale contact");
		result = arbiter.Accept(Make(Kind::Down, 1, 9), true);
		Check(result.replaced && result.replacedLatest.contact == 8 && result.verdict == Verdict::Begin && arbiter.Latest().contact == 9,
			"new primary contact on the same device replaces a lost primary");
		Check(!arbiter.CancelContact(8), "cancel of another pointer id keeps the active contact");
		PI::Sample cancelled;
		Check(arbiter.CancelContact(9, &cancelled) && cancelled.kind == Kind::Cancel && cancelled.contact == 9 && !arbiter.Active(),
			"cancel of the owned pointer id ends the contact");
		Check(!arbiter.Cancel(), "cancel without an active contact is a no-op");

		// 起始容差：按住时的细小抖动不进入 ImGui，越过后如实跟随；容差内抬起回到按下点。
		PI::ContactArbiter slop;
		(void)slop.Accept(Make(Kind::Down, 3, 20, 100, 100), true, 10);
		Check(slop.Accept(Make(Kind::Move, 3, 20, 106, 108), true).verdict == Verdict::Ignore && slop.Active()
			&& slop.Latest().screen.x == 100, "jitter inside the slop is absorbed");
		auto moved = slop.Accept(Make(Kind::Move, 3, 20, 111, 100), true);
		Check(moved.verdict == Verdict::Update && slop.Latest().screen.x == 111, "movement past the slop is delivered as is");
		Check(slop.Accept(Make(Kind::Move, 3, 20, 102, 100), true).verdict == Verdict::Update, "after leaving the slop every move is delivered");
		(void)slop.Accept(Make(Kind::Up, 3, 20, 102, 100), true);
		(void)slop.Accept(Make(Kind::Down, 3, 21, 50, 50), true, 10);
		moved = slop.Accept(Make(Kind::Up, 3, 21, 53, 54), true);
		Check(moved.verdict == Verdict::End && slop.Latest().screen.x == 50 && slop.Latest().screen.y == 50,
			"tap released inside the slop ends at the down point");
		(void)slop.Accept(Make(Kind::Down, 3, 22, 0, 0), true);
		Check(slop.Accept(Make(Kind::Move, 3, 22, 1, 0), true).verdict == Verdict::Update, "zero slop delivers every move");
	}

	void TestChannelFilter()
	{
		constexpr ULONG_PTR touch = 0xFF515780u, pen = 0xFF515700u, native = 0;
		Check(!PI::ShouldBlockChannelMouse(WM_LBUTTONDOWN, MK_LBUTTON, native, true, false), "native mouse button passes");
		Check(!PI::ShouldBlockChannelMouse(WM_MOUSEMOVE, 0, native, true, true), "native mouse move passes during a contact");
		Check(PI::ShouldBlockChannelMouse(WM_LBUTTONDOWN, MK_LBUTTON, touch, true, false), "touch promoted button is blocked");
		Check(PI::ShouldBlockChannelMouse(WM_RBUTTONDOWN, MK_RBUTTON, touch, true, false), "touch promoted right button is blocked");
		Check(PI::ShouldBlockChannelMouse(WM_LBUTTONDOWN, MK_LBUTTON, pen, true, false), "pen promoted button is blocked");
		Check(PI::ShouldBlockChannelMouse(WM_MOUSEMOVE, 0, touch, true, false), "touch promoted move is blocked");
		Check(!PI::ShouldBlockChannelMouse(WM_MOUSEMOVE, 0, pen, true, false), "pen hover move passes");
		Check(PI::ShouldBlockChannelMouse(WM_MOUSEMOVE, MK_LBUTTON, pen, true, false), "pen move with a button is blocked");
		Check(PI::ShouldBlockChannelMouse(WM_MOUSEMOVE, 0, pen, true, true), "pen move during a contact is blocked");
		Check(PI::ShouldBlockChannelMouse(WM_LBUTTONDOWN, MK_LBUTTON, touch, false, false), "legacy mode blocks touch promoted mouse");
		Check(!PI::ShouldBlockChannelMouse(WM_LBUTTONDOWN, MK_LBUTTON, pen, false, false), "legacy mode keeps pen promoted mouse");
		Check(!PI::ShouldBlockChannelMouse(WM_KEYDOWN, 0, touch, true, false), "non-mouse messages pass");
	}

	void TestTrackRing()
	{
		PI::TrackRing ring;
		std::array<PI::TrackSample, PI::TrackRing::Capacity> out{};
		std::uint64_t cursor = 0; bool overflowed = true;
		for (int index = 0; index < 3; ++index) ring.Push({ Kind::Move, Source::Touch, 0, static_cast<float>(index), index * 0.01, 1 });
		Check(ring.Read(cursor, std::span(out).first(8), overflowed) == 3 && cursor == 3 && !overflowed && out[2].y == 2.0F, "ring reads new samples");
		Check(ring.Read(cursor, out, overflowed) == 0 && cursor == 3, "ring has nothing new");
		for (int index = 0; index < 70; ++index) ring.Push({ Kind::Move, Source::Touch, 0, static_cast<float>(100 + index), 0, 1 });
		Check(ring.Read(cursor, out, overflowed) == PI::TrackRing::Capacity && overflowed && cursor == 73 && out[0].y == 106.0F,
			"ring overflow resumes from the oldest available sample");
		std::uint64_t ahead = 1000;
		Check(ring.Read(ahead, out, overflowed) == 0 && ahead == 73, "cursor ahead of the ring is clamped");
	}

	struct FakePointer
	{
		static inline POINTER_INFO info{};
		static inline BOOL ok = TRUE;
		static BOOL WINAPI Query(UINT32 id, POINTER_INFO* out)
		{
			*out = info; out->pointerId = id;
			return ok;
		}
	};
	std::vector<PI::PointerEvent> events;
	void Sink(const PI::PointerEvent& event) { events.push_back(event); }
	bool Send(UINT message, UINT32 id) { return PI::HandleWindowMessage(nullptr, message, MAKEWPARAM(id, 0), 0, Sink); }
	bool Last(EventType type, Source source) { return !events.empty() && events.back().type == type && events.back().source == source; }

	void TestWindowMessages()
	{
		Touch::Testing::SetPointerQuery(FakePointer::Query);
		PI::Testing::Reset();
		events.clear();
		auto& info = FakePointer::info;
		info = {}; FakePointer::ok = TRUE;
		info.pointerType = PT_TOUCH; info.sourceDevice = reinterpret_cast<HANDLE>(0x10); info.dwTime = 1234;
		info.ptPixelLocationRaw = { 100, 200 };
		constexpr UINT32 contactFlags = POINTER_FLAG_INCONTACT | POINTER_FLAG_PRIMARY | POINTER_FLAG_FIRSTBUTTON;

		info.pointerFlags = contactFlags | POINTER_FLAG_DOWN;
		Check(Send(WM_POINTERDOWN, 5) && events.size() == 1 && Last(EventType::Begin, Source::Touch) && events.back().x == 100.0F,
			"touch down is consumed and begins an ImGui contact");
		PI::PointerOverride pointer;
		Check(PI::ReadPointerOverride(pointer) && pointer.source == Source::Touch && pointer.y == 200.0F, "active touch overrides the pointer");
		info.ptPixelLocationRaw = { 110, 230 }; info.pointerFlags = contactFlags | POINTER_FLAG_UPDATE;
		Check(Send(WM_POINTERUPDATE, 5) && Last(EventType::Update, Source::Touch) && events.back().y == 230.0F, "touch move updates");
		info.pointerFlags = POINTER_FLAG_INCONTACT | POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_DOWN;
		Check(Send(WM_POINTERDOWN, 6) && events.size() == 2, "second finger is consumed without ImGui input");
		info.pointerFlags = POINTER_FLAG_PRIMARY | POINTER_FLAG_UPDATE;
		Check(!Send(WM_POINTERUPDATE, 5) && events.size() == 2, "hover update stays on the default path");
		info.pointerFlags = POINTER_FLAG_PRIMARY | POINTER_FLAG_UP;
		Check(Send(WM_POINTERUP, 5) && Last(EventType::End, Source::Touch), "touch up ends the contact");
		Check(PI::ReadPointerOverride(pointer) && pointer.x == -FLT_MAX, "touch up parks the pointer outside the window");
		Check(!PI::FilterChannelMouse(WM_MOUSEMOVE, 0) && !PI::ReadPointerOverride(pointer), "a passing native mouse message ends the park");
		Check(PI::TakeParkRelease() && !PI::TakeParkRelease(), "ending the park asks once for the real cursor position to be restored");
		Check(!PI::FilterChannelMouse(WM_MOUSEMOVE, 0) && !PI::TakeParkRelease(), "later mouse messages do not re-request the cursor position");

		info.pointerType = PT_PEN; info.sourceDevice = reinterpret_cast<HANDLE>(0x20);
		info.pointerFlags = contactFlags | POINTER_FLAG_SECONDBUTTON | POINTER_FLAG_DOWN;
		const auto beforeBarrel = events.size();
		Check(Send(WM_POINTERDOWN, 7) && events.size() == beforeBarrel, "pen barrel button down is swallowed");
		info.pointerFlags = contactFlags | POINTER_FLAG_DOWN;
		Check(Send(WM_POINTERDOWN, 8) && Last(EventType::Begin, Source::Pen), "pen down begins a pen contact");
		info.pointerFlags = contactFlags | POINTER_FLAG_CANCELED | POINTER_FLAG_UPDATE;
		Check(Send(WM_POINTERUPDATE, 8) && Last(EventType::Cancel, Source::Pen), "system cancel ends the pen contact without a click");
		Check(!PI::ReadPointerOverride(pointer), "pen end leaves hover to the compatible mouse");

		info.pointerFlags = contactFlags | POINTER_FLAG_DOWN;
		(void)Send(WM_POINTERDOWN, 9);
		Check(!Send(WM_POINTERCAPTURECHANGED, 9) && Last(EventType::Cancel, Source::Pen), "capture change cancels and stays on the default path");
		(void)Send(WM_POINTERDOWN, 10);
		FakePointer::ok = FALSE;
		Check(!Send(WM_POINTERUPDATE, 10) && Last(EventType::Cancel, Source::Pen), "query failure on move cancels the contact");
		FakePointer::ok = TRUE;
		(void)Send(WM_POINTERDOWN, 11);
		PI::CancelContact(Sink);
		Check(Last(EventType::Cancel, Source::Pen), "explicit cancel ends the active contact");

		Touch::Testing::ForceLegacy(true);
		const auto beforeLegacy = events.size();
		Check(!Send(WM_POINTERDOWN, 12) && events.size() == beforeLegacy, "legacy mode leaves pointer messages to the default path");
		Touch::Testing::ForceLegacy(false);

		std::array<PI::TrackSample, PI::TrackRing::Capacity> track{};
		std::uint64_t cursor = 0; bool overflowed = false;
		const auto count = PI::ReadTrack(cursor, track, overflowed);
		Check(count == events.size() && !overflowed && track[0].kind == Kind::Down && track[0].generation == 1
			&& track[0].timeSeconds == 1.234 && track[1].kind == Kind::Move && track[2].kind == Kind::Up,
			"track mirrors delivered contact samples with input time");

		Touch::Testing::SetPointerQuery(nullptr);
		PI::Testing::Reset();
		events.clear();
	}

	// 设置页面滚动的纯逻辑：释放速度估计、指数衰减惯性与越界换算。
	void TestScrollKernel()
	{
		namespace Scroll = Inkeys::UI::Setting::Scroll;
		Scroll::VelocityEstimator velocity;
		for (int index = 0; index <= 6; ++index) velocity.Add(index / 60.0, -20.0F * index);
		Check(std::abs(velocity.Estimate(6 / 60.0) + 1200.0F) < 1.0F, "steady motion estimates its velocity");
		velocity.Reset();
		for (int index = 0; index <= 3; ++index) velocity.Add(index / 60.0, -20.0F * index);
		for (int index = 4; index <= 12; ++index) velocity.Add(index / 60.0, -60.0F);
		Check(velocity.Estimate(12 / 60.0) == 0.0F, "holding still before release yields no velocity");
		velocity.Reset(); velocity.Add(0.0, 5.0F);
		Check(velocity.Estimate(0.0) == 0.0F, "a single sample yields no velocity");
		velocity.Reset();
		for (int index = 0; index < 40; ++index) velocity.Add(index / 60.0, index < 30 ? 10.0F * index : 300.0F - 15.0F * (index - 30));
		Check(velocity.Estimate(39 / 60.0) < 0.0F, "only the recent window decides the release direction");

		Scroll::InertiaCurve curve;
		curve.Start(100.0F, 1200.0F, 1.0);
		const double lambda = -std::log(1.0 - Scroll::InertiaDecayRate);
		Check(curve.Active() && std::abs(curve.Velocity(2.0) - 1200.0F * static_cast<float>(1.0 - Scroll::InertiaDecayRate)) < 0.5F,
			"velocity keeps 5 percent after one second");
		Check(std::abs(curve.Position(1000.0) - (100.0F + static_cast<float>(1200.0 / lambda))) < 0.5F, "total glide distance is v0 / ln(20)");
		Check(curve.Position(0.5) == 100.0F && curve.Velocity(0.5) == 1200.0F, "time before the start stays at the origin");
		float stepped = 0.0F;
		for (int frame = 0; frame <= 144; ++frame) stepped = curve.Position(1.0 + frame / 144.0);
		Check(std::abs(stepped - curve.Position(2.0)) < 0.001F, "position depends only on elapsed time, not frame rate");
		curve.Start(0.0F, 0.0F, 0.0);
		Check(!curve.Active(), "zero velocity does not start inertia");

		Check(Scroll::OverpanOffset(-40.0F, 60.0F) == -20.0F && Scroll::OverpanOffset(400.0F, 60.0F) == 60.0F,
			"overpan maps through the resistance and caps at the maximum offset");
		Check(Scroll::OverpanLimit(60.0F) == 120.0F, "overpan accumulation stops where the offset is capped");
	}
}

int RunSettingPointerInputTests()
{
	failures = 0;
	TestArbiter();
	TestChannelFilter();
	TestTrackRing();
	TestWindowMessages();
	TestScrollKernel();
	if (failures == 0) std::cout << "setting pointer input tests passed\n";
	return failures;
}
