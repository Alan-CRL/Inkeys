#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string_view>

import Inkeys.UI.UI3.Interaction;

using namespace Inkeys::UI::UI3;

namespace
{
	using namespace std::chrono_literals;
	int failures = 0;
	void Check(bool condition, std::string_view name)
	{
		if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; }
	}
	PointerGestureRecognizer::Time At(int ms) { return PointerGestureRecognizer::Time{std::chrono::milliseconds(ms)}; }
	GesturePointerInput Input(ButtonPointerEvent kind, int ms, std::uint64_t hit = 7,
		double x = 0, double y = 0, GestureButton button = GestureButton::Primary, bool nativeDouble = false)
	{ return {kind, At(ms), x, y, hit, button, nativeDouble}; }
	bool Has(const GestureUpdate& result, GestureKind kind, std::uint64_t hit = 7)
	{
		for (std::size_t i = 0; i < result.count; ++i)
			if (result.events[i].kind == kind && result.events[i].target == hit) return true;
		return false;
	}
	void TestActivation()
	{
		PointerGestureRecognizer gesture;
		Check(gesture.Update(Input(ButtonPointerEvent::Down, 0)).count == 0, "release activation waits for Up");
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Up, 10)), GestureKind::Click), "release activates same target");
		Check(gesture.Update(Input(ButtonPointerEvent::Up, 11)).count == 0, "stale Up cannot repeat click");
		gesture.Update(Input(ButtonPointerEvent::Down, 20));
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Move, 21, 8)), GestureKind::Cancelled), "moving out cancels contact");
		Check(gesture.Update(Input(ButtonPointerEvent::Up, 22)).count == 0, "returning Up cannot reactivate");
		gesture.Update(Input(ButtonPointerEvent::Down, 30));
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Up, 31, 0)), GestureKind::Cancelled), "outside Up cancels click");
		GestureConfig config; config.activation = GestureActivation::OnPress;
		PointerGestureRecognizer palette(config);
		Check(Has(palette.Update(Input(ButtonPointerEvent::Down, 0)), GestureKind::Click), "palette activates on Down");
		Check(palette.Update(Input(ButtonPointerEvent::Move, 1)).count == 0, "palette same item does not repeat");
		Check(Has(palette.Update(Input(ButtonPointerEvent::Move, 2, 8)), GestureKind::Click, 8), "palette drag selects adjacent item");
		Check(palette.Update(Input(ButtonPointerEvent::Move, 3, 0)).count == 0, "palette gap has no action");
		Check(palette.Update(Input(ButtonPointerEvent::Move, 4, 8)).count == 0, "palette reentry keeps last swatch without duplicate selection");
		Check(Has(palette.Update(Input(ButtonPointerEvent::Move, 5, 7)), GestureKind::Click), "palette returning from another swatch selects original again");
		Check(palette.Update(Input(ButtonPointerEvent::Up, 6)).count == 0, "palette Up adds no click");
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Down, 40, 7, 0, 0, GestureButton::Secondary)),
			GestureKind::SecondaryClick), "main secondary click occurs on Down");
		Check(gesture.Update(Input(ButtonPointerEvent::Up, 41, 7, 0, 0, GestureButton::Secondary)).count == 0,
			"secondary Up does not repeat");
		config.secondaryActivation = GestureActivation::OnRelease;
		PointerGestureRecognizer secondary(config);
		secondary.Update(Input(ButtonPointerEvent::Down, 0, 7, 0, 0, GestureButton::Secondary));
		Check(secondary.Update(Input(ButtonPointerEvent::Up, 1)).count == 0, "other button Up cannot end secondary contact");
		Check(Has(secondary.Update(Input(ButtonPointerEvent::Up, 2, 7, 0, 0, GestureButton::Secondary)),
			GestureKind::SecondaryClick), "secondary release mode activates once");
	}
	void TestDoubleClick()
	{
		GestureConfig config; config.doubleClick = GestureDoubleClick::Interval; config.doubleClickInterval = 300ms;
		PointerGestureRecognizer gesture(config);
		gesture.Update(Input(ButtonPointerEvent::Down, 0));
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Up, 10)), GestureKind::Click), "first click is immediate");
		Check(!gesture.NextWake(), "double click history needs no timer wake");
		gesture.Update(Input(ButtonPointerEvent::Down, 300));
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Up, 500)), GestureKind::DoubleClick), "double interval includes exact Down boundary");
		gesture.Update(Input(ButtonPointerEvent::Down, 600)); gesture.Update(Input(ButtonPointerEvent::Up, 610));
		gesture.Update(Input(ButtonPointerEvent::Down, 901));
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Up, 902)), GestureKind::Click), "double interval plus one is single click");
		gesture.Update(Input(ButtonPointerEvent::Down, 910, 8));
		Check(Has(gesture.Update(Input(ButtonPointerEvent::Up, 911, 8)), GestureKind::Click, 8), "double requires same identity");
		config.doubleClick = GestureDoubleClick::Native;
		PointerGestureRecognizer native(config);
		native.Update(Input(ButtonPointerEvent::Down, 0)); native.Update(Input(ButtonPointerEvent::Up, 1));
		native.Update(Input(ButtonPointerEvent::Down, 5000, 0, 0, 0, GestureButton::Primary, true));
		native.Update(Input(ButtonPointerEvent::Move, 5001, 0));
		Check(Has(native.Update(Input(ButtonPointerEvent::Up, 5002, 0)), GestureKind::DoubleClick),
			"native continuation keeps first button despite animated hit displacement");
		native.Update(Input(ButtonPointerEvent::Down, 6000)); native.Update(Input(ButtonPointerEvent::Cancel, 6001));
		native.Update(Input(ButtonPointerEvent::Down, 6002, 0, 0, 0, GestureButton::Primary, true));
		Check(Has(native.Update(Input(ButtonPointerEvent::Up, 6003, 0)), GestureKind::DoubleClick), "native mode preserves last successful identity through cancelled attempts");
	}
	void TestHold()
	{
		GestureConfig config; config.holdEnabled = true; config.distance = GestureDistance::Axes;
		PointerGestureRecognizer geometry(config);
		geometry.Update(Input(ButtonPointerEvent::Down, 0));
		Check(geometry.NextWake() == At(200), "geometry first wake is ring delay");
		Check(geometry.Advance(At(199)).count == 0, "hold does not start one ms early");
		const auto start = geometry.Advance(At(200));
		Check(Has(start, GestureKind::LongPressStarted) && start.nextWake == At(1000), "hold starts at exact delay and schedules completion");
		Check(geometry.Advance(At(999)).count == 0, "hold does not complete one ms early");
		const auto done = geometry.Advance(At(1000));
		Check(Has(done, GestureKind::LongPressCompleted) && !done.nextWake, "hold completes exactly once at total deadline");
		Check(geometry.Advance(At(1001)).count == 0 && geometry.Update(Input(ButtonPointerEvent::Up, 1002)).count == 0,
			"completed hold consumes release click");
		geometry.Update(Input(ButtonPointerEvent::Down, 2000));
		const auto late = geometry.Update(Input(ButtonPointerEvent::Up, 3000));
		Check(late.count == 2 && Has(late, GestureKind::LongPressStarted) && Has(late, GestureKind::LongPressCompleted),
			"Up catches up both missed geometry deadlines");
		geometry.Update(Input(ButtonPointerEvent::Down, 4000));
		geometry.Update(Input(ButtonPointerEvent::Move, 4100, 7, 5, 5));
		Check(geometry.NextWake() == At(4200), "axis threshold allows diagonal equality");
		Check(Has(geometry.Update(Input(ButtonPointerEvent::Move, 4101, 7, 5.01, 0)), GestureKind::Cancelled)
			&& !geometry.NextWake(), "axis excess cancels hold without restart");
		Check(Has(geometry.Update(Input(ButtonPointerEvent::Up, 4102)), GestureKind::Click), "early movement cancels hold but preserves short click");
		geometry.Update(Input(ButtonPointerEvent::Down, 5000));
		geometry.Update(Input(ButtonPointerEvent::Move, 5200, 7, 6));
		Check(geometry.Update(Input(ButtonPointerEvent::Up, 5201)).count == 0, "movement at ring delay consumes short click");
		config.holdDelay = 500ms; config.holdDuration = 1500ms;
		config.movePolicy = GestureMovePolicy::RestartHold; config.distance = GestureDistance::Euclidean;
		config.cancelOnHitExit = false; config.consumeClickOnHoldStart = false; config.advanceHoldOnRelease = false;
		PointerGestureRecognizer picker(config);
		picker.Update(Input(ButtonPointerEvent::Down, 0));
		picker.Update(Input(ButtonPointerEvent::Move, 100, 0, 3, 4));
		Check(picker.NextWake() == At(500), "euclidean threshold includes exact distance five");
		Check(Has(picker.Update(Input(ButtonPointerEvent::Move, 500, 0, 4, 4)), GestureKind::Cancelled)
			&& picker.NextWake() == At(1000), "picker movement at deadline resets before locking");
		Check(picker.Advance(At(999)).count == 0 && Has(picker.Advance(At(1000)), GestureKind::LongPressStarted), "picker restart delay boundary");
		Check(picker.Advance(At(2499)).count == 0 && Has(picker.Advance(At(2500)), GestureKind::LongPressCompleted), "picker restart completion boundary");
		Check(picker.Update(Input(ButtonPointerEvent::Move, 2501, 0, 50)).count == 0 && !picker.NextWake(), "locked picker ignores subsequent movement");
		Check(Has(picker.Update(Input(ButtonPointerEvent::Up, 2502, 0)), GestureKind::Click), "picker lock preserves release submission");
		picker.Update(Input(ButtonPointerEvent::Down, 3000));
		const auto up = picker.Update(Input(ButtonPointerEvent::Up, 5000));
		Check(up.count == 1 && Has(up, GestureKind::Click), "picker Up does not synthesize unsampled lock");
	}
	void TestCancelAndLeave()
	{
		GestureConfig config; config.holdEnabled = true;
		for (const auto kind : {ButtonPointerEvent::Cancel, ButtonPointerEvent::Leave})
		{
			PointerGestureRecognizer gesture(config); gesture.Update(Input(ButtonPointerEvent::Down, 0));
			Check(Has(gesture.Update(Input(kind, 200)), GestureKind::Cancelled), "cancel or leave precedes same-time deadline");
			Check(!gesture.NextWake() && gesture.Advance(At(1000)).count == 0
				&& gesture.Update(Input(ButtonPointerEvent::Up, 1001)).count == 0, "terminal event removes all pending hold and click work");
		}
	}
	void TestCoroutineButtonMapping()
	{
		// 协程把等待主键期间的次键消息映射成 Move，来源已归一化，触摸/鼠标共用相同序列。
		PointerGestureRecognizer gesture;
		ButtonPointerState legacy;
		bool equivalent = true;
		for (const auto input : {Input(ButtonPointerEvent::Down, 0), Input(ButtonPointerEvent::Move, 1, 7, 0, 0, GestureButton::Secondary),
			Input(ButtonPointerEvent::Move, 2), Input(ButtonPointerEvent::Up, 3),
			Input(ButtonPointerEvent::Down, 4), Input(ButtonPointerEvent::Move, 5, 0), Input(ButtonPointerEvent::Move, 6), Input(ButtonPointerEvent::Up, 7),
			Input(ButtonPointerEvent::Down, 8), Input(ButtonPointerEvent::Cancel, 9), Input(ButtonPointerEvent::Up, 10)})
		{
			const auto old = legacy.Update(input.kind, input.hit);
			equivalent &= Has(gesture.Update(input), GestureKind::Click) == (old.activate == 7);
		}
		Check(equivalent, "coroutine click mapping matches pointer state through secondary messages exit reentry and cancel");
		GestureConfig config; config.doubleClick = GestureDoubleClick::Native;
		PointerGestureRecognizer native(config);
		native.Update(Input(ButtonPointerEvent::Down, 0)); native.Update(Input(ButtonPointerEvent::Up, 1));
		native.Update(Input(ButtonPointerEvent::Down, 2));
		InteractionMailbox<GesturePointerInput, 2> mailbox;
		const auto separate = [](const GesturePointerInput&, const GesturePointerInput&) { return false; };
		mailbox.Push(Input(ButtonPointerEvent::Move, 3), separate); mailbox.Push(Input(ButtonPointerEvent::Move, 4), separate);
		Check(!mailbox.Push(Input(ButtonPointerEvent::Up, 5), separate) && mailbox.ConsumeOverflow(), "mailbox overflow enters cancellation barrier");
		native.Update(Input(ButtonPointerEvent::Cancel, 6));
		Check(!Has(native.Update(Input(ButtonPointerEvent::Up, 7)), GestureKind::Click), "overflow cancel cannot activate queued or late Up");
		for (const int down : {10, 10000})
		{
			native.Update(Input(ButtonPointerEvent::Down, down));
			Check(Has(native.Update(Input(ButtonPointerEvent::Up, down+1)), GestureKind::Click), "native mode never infers double from time interval");
		}
		native.Update(Input(ButtonPointerEvent::Down, 20000, 0, 0, 0, GestureButton::Primary, true));
		Check(Has(native.Update(Input(ButtonPointerEvent::Up, 20001, 0)), GestureKind::DoubleClick), "native flag alone continues last accepted button");
	}
}

int RunUi3GestureTests()
{
	TestActivation(); TestDoubleClick(); TestHold(); TestCancelAndLeave(); TestCoroutineButtonMapping(); return failures;
}
