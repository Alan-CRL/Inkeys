#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <initializer_list>
#include <span>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.WhiteboardPattern.h"
#include "../Inkeys/Inkeys/UI/Whiteboard/Whiteboard.PatternFollow.h"

import Inkeys.Drawing.Draw3.canvas_navigation;

// 白板产品手感与有序输入回归；背景/Freeze 回归保留在本文件末尾。
namespace
{
	using namespace Inkeys::Drawing::Draw3;
	constexpr int64_t kFreq = 10'000'000;
	constexpr int64_t kOrigin = 5'000'000'000LL;

	struct Checks
	{
		int failures = 0;
		void operator()(bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::cerr << "[Draw3CanvasPan] failed: " << name << '\n';
		}
		void Within(double value, double low, double high, const char* name)
		{
			if (value >= low && value <= high) return;
			++failures;
			std::cerr << "[Draw3CanvasPan] failed: " << name << " value=" << value
				<< " expected=[" << low << ", " << high << "]\n";
		}
		void Near(double value, double expected, double tolerance, const char* name)
		{
			Within(value, expected - tolerance, expected + tolerance, name);
		}
	};

	void Info(const std::string& text)
	{
		std::cout << "[Draw3CanvasPan] " << text << '\n';
	}

	int64_t QpcMs(double ms)
	{
		return kOrigin + static_cast<int64_t>(std::llround(ms * static_cast<double>(kFreq) / 1000.0));
	}

	CanvasPanContactSample Contact(uint64_t key, double ms, double x, double y = 0.0,
		CanvasPanContactPhase phase = CanvasPanContactPhase::Move)
	{
		return { key, QpcMs(ms), x, y, phase };
	}

	CanvasPanFrameOutput StepAt(CanvasPanDynamics& dynamics, double ms,
		std::span<const CanvasPanContactSample> contacts = {}, bool hover = false, bool pen = false,
		CanvasVector viewport = { 1920.0f, 1080.0f })
	{
		CanvasPanFrameInput input;
		input.frameQpc = QpcMs(ms);
		input.qpcFrequency = kFreq;
		input.contacts = contacts;
		input.penHover = hover;
		input.penContact = pen;
		input.viewportSizeDip = viewport;
		return dynamics.Step(input);
	}

	CanvasPanFrameOutput StepAt(CanvasPanDynamics& dynamics, double ms,
		std::initializer_list<CanvasPanContactSample> contacts, bool hover = false, bool pen = false,
		CanvasVector viewport = { 1920.0f, 1080.0f })
	{
		return StepAt(dynamics, ms, std::span<const CanvasPanContactSample>(contacts.begin(), contacts.size()), hover, pen, viewport);
	}

	CanvasPanFrameOutput StartFling(CanvasPanDynamics& dynamics, double startMs = 0.0,
		uint64_t key = 1, double vx = 3000.0, double vy = 0.0)
	{
		StepAt(dynamics, startMs, { Contact(key, startMs, 0.0, 0.0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 4; ++n)
			StepAt(dynamics, startMs + n * 8.0,
				{ Contact(key, startMs + n * 8.0, vx * n * 0.008, vy * n * 0.008) });
		return StepAt(dynamics, startMs + 33.0,
			{ Contact(key, startMs + 33.0, vx * 0.032, vy * 0.032, CanvasPanContactPhase::Up) });
	}

	CanvasPanGestureEvent Gesture(CanvasPanGestureType type, double seconds,
		CanvasVector velocity = {}, CanvasVector delta = {})
	{
		CanvasPanGestureEvent event;
		event.type = type;
		event.timeSeconds = seconds;
		event.velocity = velocity;
		event.delta = delta;
		event.sourceDevice = 1;
		return event;
	}

	CanvasPanFlingBooster BoostCandidate(float speed = 1000.0f)
	{
		CanvasPanFlingBooster booster;
		booster.GetVelocityForFlingStart(Gesture(CanvasPanGestureType::FlingStart, 1.0, { speed, 0 }));
		booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::FlingCancel, 1.010));
		return booster;
	}

	void TestFlingBooster(Checks& check)
	{
		const auto next = Gesture(CanvasPanGestureType::FlingStart, 1.020, { 500, 0 });
		CanvasPanFlingBooster empty;
		check(!empty.ShouldBoostFling(next), "boost rejection 1: no active previous fling");
		auto booster = BoostCandidate();
		auto changed = next; changed.sourceDevice = 2;
		check(!booster.ShouldBoostFling(changed), "boost rejection 2: different source device");
		changed = next; changed.modifiers = 1;
		check(!booster.ShouldBoostFling(changed), "boost rejection 3: different modifiers");
		CanvasPanFlingBooster noCutoff;
		noCutoff.GetVelocityForFlingStart(Gesture(CanvasPanGestureType::FlingStart, 1.0, { 1000, 0 }));
		check(!noCutoff.ShouldBoostFling(next), "boost rejection 4: cutoff not set");
		changed = next; changed.timeSeconds = 1.060001;
		check(!booster.ShouldBoostFling(changed), "boost rejection 5: event after fifty millisecond cutoff");
		changed = next; changed.velocity = { -500, 0 };
		check(!booster.ShouldBoostFling(changed), "boost rejection 6: reversed direction");
		changed.velocity = { 0, 500 };
		check(!booster.ShouldBoostFling(changed), "boost rejection 6 includes zero dot product");
		booster.ObserveProgressFling({ 349.9f, 0 });
		check(!booster.ShouldBoostFling(next), "boost rejection 7: current fling below 350");
		booster.ObserveProgressFling({ 350, 0 });
		changed = next; changed.velocity = { 349.9f, 0 };
		check(!booster.ShouldBoostFling(changed), "boost rejection 8: new fling below 350");
		changed.velocity = { 350, 0 }; changed.timeSeconds = booster.CutoffSeconds();
		check(booster.ShouldBoostFling(changed), "350 thresholds and exact fifty millisecond cutoff are inclusive");
		const auto boosted = booster.GetVelocityForFlingStart(changed);
		check(boosted.x == 700.0f && booster.StartingVelocity().x == 700.0f,
			"boost adds the current residual rather than historical starting velocity");
		check(booster.CutoffSeconds() == 0.0, "starting a fling resets cutoff");
		booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::FlingCancel, 1.070));
		check(booster.GetVelocityForFlingStart(Gesture(CanvasPanGestureType::FlingStart, 1.080, { 500, 0 })).x == 1200.0f,
			"consecutive boosts use the latest current residual");

		for (const float delta : { 1.49f, 1.5f })
		{
			booster = BoostCandidate();
			booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::ScrollBegin, 1.020));
			booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::ScrollUpdate, 1.030, {}, { 0.1f, 0 }));
			booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::ScrollUpdate, 1.040, {}, { delta, 0 }));
			check((booster.StartingVelocity().x != 0.0f) == (delta == 1.5f),
				"scroll below 150 resets while exactly 150 preserves the boost");
		}
		booster = BoostCandidate();
		booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::ScrollUpdate, 1.020, {}, { -1, 0 }));
		check(booster.StartingVelocity().x == 0.0f, "opposite scroll immediately resets booster");
		booster = BoostCandidate();
		changed = Gesture(CanvasPanGestureType::ScrollUpdate, 1.020, {}, { -1, 0 }); changed.inertial = true;
		booster.ObserveGestureEvent(changed);
		check(booster.StartingVelocity().x == 1000.0f, "momentum update bypasses touch direction checks");
		changed = Gesture(CanvasPanGestureType::FlingCancel, 1.030); changed.preventBoosting = true;
		booster.ObserveGestureEvent(changed);
		check(booster.StartingVelocity().x == 0.0f, "prevent boosting cancellation resets booster");
		booster = BoostCandidate();
		changed = Gesture(CanvasPanGestureType::ScrollBegin, 1.020); changed.sourceDevice = 2;
		booster.ObserveGestureEvent(changed);
		check(booster.StartingVelocity().x == 0.0f, "observed different source resets booster");
		booster = BoostCandidate();
		booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::ScrollEnd, 1.070));
		check(booster.StartingVelocity().x == 0.0f, "timeout is checked before event dispatch");
	}

	void TestFlingController(Checks& check)
	{
		CanvasPanFlingController fling;
		CanvasPanFlingBooster booster;
		fling.Start(Gesture(CanvasPanGestureType::FlingStart, 1.010, { 3000, -1500 }), 1.0, { 1000, 800 }, booster);
		check(fling.Active(), "nonzero reliable velocity starts the product curve");
		const auto wait = fling.Progress(1.0, booster);
		check(wait.x == 0 && wait.y == 0 && fling.Active(), "no progress before or at presentation start time");
		const auto first = fling.Progress(1.100, booster);
		CanvasPanFlingCurve curve;
		curve.Start({ 3000, -1500 }, { 1000, 800 });
		CanvasVector offset, velocity;
		curve.Advance(0.100, offset, velocity);
		check.Near(first.x, offset.x, .0002, "late product progress keeps the actual elapsed animation time");
		check(first.y < 0 && booster.CurrentVelocity().x == fling.Velocity().x,
			"progress preserves direction and publishes the current residual");
		const auto before = fling.Offset();
		const auto backwards = fling.Progress(1.090, booster);
		check(backwards.x == 0 && backwards.y == 0 && fling.Offset().x == before.x,
			"backwards progress does not advance cumulative offset");
		fling.Cancel();
		check(!fling.Active() && booster.StartingVelocity().x == 3000.0f, "contact cancellation preserves the boost candidate");
		fling.StopFling(booster);
		check(!fling.Active() && booster.StartingVelocity().x == 0.0f, "program stop clears the boost candidate");
		fling.Start(Gesture(CanvasPanGestureType::FlingStart, 2.0, { 3000, 0 }), 2.0, {}, booster);
		check(!fling.Active() && booster.StartingVelocity().x == 0.0f, "empty viewport cannot start a fling");
		fling.Start(Gesture(CanvasPanGestureType::FlingStart, 3.0, { 31, 0 }), 3.0, { 1000, 800 }, booster);
		const auto tiny = fling.Progress(3.000001, booster);
		check(tiny.x > 0 && tiny.x < .1f && tiny.x == fling.Offset().x,
			"product curve preserves sub-epsilon inertial increments");
	}

	void TestGestureDispatchAndSlop(Checks& check)
	{
		CanvasPanGestureDetector detector;
		const std::array<CanvasPanPointer, 1> down{ { { 1, 0.0, 0.0 } } };
		detector.Down(QpcMs(0.0), down, kFreq);
		const std::array<CanvasPanPointer, 1> inside{ { { 1, 15.05, 0.0 } } };
		check(detector.Move(QpcMs(8.0), inside, kFreq).count == 0 && !detector.Scrolling(),
			"15.05 DIP remains at inclusive slop boundary");
		const std::array<CanvasPanPointer, 1> outside{ { { 1, 16.0, 0.0 } } };
		const auto start = detector.Move(QpcMs(16.0), outside, kFreq);
		check(start.count == 2 && start.events[0].type == CanvasPanGestureType::ScrollBegin &&
			start.events[1].type == CanvasPanGestureType::ScrollUpdate,
			"GestureProvider dispatches Begin before first Update");
		check(start.events[0].delta.x > 0.0f && start.events[1].delta.x > 0.0f,
			"scroll hints and updates use content-following sign at module boundary");
		check(start.events[0].delta.x == 1.0f && start.events[1].delta.x == 1.0f,
			"first scroll removes fifteen DIP slop without detector epsilon");
		CanvasPanGestureDetector diagonal;
		diagonal.Down(QpcMs(0.0), down, kFreq);
		const std::array<CanvasPanPointer, 1> diagonalMove{ { { 1, 18.0, 24.0 } } };
		const auto diagonalStart = diagonal.Move(QpcMs(8.0), diagonalMove, kFreq);
		check(diagonalStart.count == 2 && diagonalStart.events[1].delta.x == 9.0f && diagonalStart.events[1].delta.y == 12.0f,
			"first diagonal scroll radially subtracts fifteen from length thirty");
		CanvasPanGestureDetector pairSlop;
		pairSlop.Down(QpcMs(0.0), down, kFreq);
		const std::array<CanvasPanPointer, 2> pairDown{ { { 1, 0.0, 0.0 }, { 2, 100.0, 100.0 } } };
		pairSlop.Down(QpcMs(1.0), pairDown, kFreq);
		const std::array<CanvasPanPointer, 2> pairMove{ { { 1, 18.0, 24.0 }, { 2, 100.0, 120.0 } } };
		const auto pairStart = pairSlop.Move(QpcMs(8.0), pairMove, kFreq);
		check(pairStart.count == 2 && pairStart.events[1].delta.x == 4.5f && pairStart.events[1].delta.y == 8.5f,
			"first two-pointer scroll averages individually slop-subtracted displacements");
		const std::array<CanvasPanPointer, 1> tiny{ { { 1, 16.05, 0.05 } } };
		check(detector.Move(QpcMs(24.0), tiny, kFreq).count == 0, "both axes below epsilon suppress update");
		const std::array<CanvasPanPointer, 1> large{ { { 1, 16.2, 0.05 } } };
		const auto update = detector.Move(QpcMs(32.0), large, kFreq);
		check(update.count == 1 && update.events[0].type == CanvasPanGestureType::ScrollUpdate,
			"one axis exceeding epsilon dispatches update");
		check.Near(update.events[0].delta.x, 0.2, 1e-5, "suppressed tiny movement remains in touch baseline");
		check.Near(update.events[0].delta.y, 0.05, 1e-5, "OR epsilon gate retains the smaller axis too");

		CanvasPanGestureDetector exactEpsilon;
		const std::array<CanvasPanPointer, 1> negativeDown{ { { 1, -20.0, 0.0 } } };
		exactEpsilon.Down(QpcMs(0.0), negativeDown, kFreq);
		exactEpsilon.Move(QpcMs(8.0), down, kFreq);
		const std::array<CanvasPanPointer, 1> exactTenth{ { { 1, 0.1, 0.0 } } };
		check(exactEpsilon.Move(QpcMs(16.0), exactTenth, kFreq).count == 0,
			"exact float 0.1 DIP increment is below dispatch gate");
		const std::array<CanvasPanPointer, 1> smallAxis{ { { 1, 0.2, 0.05 } } };
		const auto both = exactEpsilon.Move(QpcMs(24.0), smallAxis, kFreq);
		check(both.count == 1 && both.events[0].delta.y == 0.05f,
			"epsilon gate is an OR predicate without independently dropping small axis");

		// Provider 丢弃 focus 不动的零 scroll，但 Detector 已越过各自 slop 区域。
		CanvasPanGestureDetector zeroFocus;
		const std::array<CanvasPanPointer, 1> zeroDown{ { { 1, -20.0, 0.0 } } };
		const std::array<CanvasPanPointer, 2> zeroPair{ { { 1, -20.0, 0.0 }, { 2, 20.0, 0.0 } } };
		zeroFocus.Down(QpcMs(0.0), zeroDown, kFreq);
		zeroFocus.Down(QpcMs(1.0), zeroPair, kFreq);
		const std::array<CanvasPanPointer, 2> opposedPair{ { { 1, -40.0, 0.0 }, { 2, 40.0, 0.0 } } };
		check(zeroFocus.Move(QpcMs(8.0), opposedPair, kFreq).count == 0 && !zeroFocus.Scrolling(),
			"zero focus displacement does not dispatch Begin or Update after crossing pointer slop");
		const std::array<CanvasPanPointer, 2> translatedPair{ { { 1, -35.0, 0.0 }, { 2, 45.0, 0.0 } } };
		const auto zeroStart = zeroFocus.Move(QpcMs(16.0), translatedPair, kFreq);
		check(zeroStart.count == 2 && zeroStart.events[0].type == CanvasPanGestureType::ScrollBegin &&
			zeroStart.events[1].type == CanvasPanGestureType::ScrollUpdate && zeroStart.events[1].delta.x == 5.0f,
			"first nonzero Provider scroll recomputes slop-subtracted pointer average");

		CanvasPanGestureDetector flingDetector;
		flingDetector.Down(QpcMs(0.0), down, kFreq);
		const std::array<CanvasPanPointer, 1> move1{ { { 1, 30.0, 0.0 } } };
		const std::array<CanvasPanPointer, 1> move2{ { { 1, 60.0, 0.0 } } };
		flingDetector.Move(QpcMs(10.0), move1, kFreq);
		flingDetector.Move(QpcMs(20.0), move2, kFreq);
		const auto up = flingDetector.Up(QpcMs(21.0), move2, 1, false, kFreq, { true, 3000.0, 0.0 });
		check(up.count == 1 && up.events[0].type == CanvasPanGestureType::FlingStart,
			"FlingStart ends touch scrolling without an additional ScrollEnd");
		check.Near(up.events[0].velocity.x, 3000.0, 0.2, "UP dispatches the supplied reliable release velocity");

		CanvasPanDynamics perAxis;
		const auto fast = StartFling(perAxis, 0.0, 1, 30000.0, 40000.0);
		check(fast.phase == CanvasPanPhase::Coasting, "fast diagonal drag starts a fling");
		check(fast.boosterStartingVelocity.x == 7200.0f && fast.boosterStartingVelocity.y == 9600.0f,
			"product fling caps vector norm at twelve thousand without changing direction");
		for (const double speed : { 29.0, 31.0 })
		{
			CanvasPanDynamics threshold;
			StepAt(threshold, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
			StepAt(threshold, 8.0, { Contact(1, 8.0, 24.0) });
			for (int n = 2; n <= 25; ++n)
				StepAt(threshold, n * 8.0, { Contact(1, n * 8.0, 24.0 + speed * (n - 1) * 0.008) });
			const auto released = StepAt(threshold, 201.0,
				{ Contact(1, 201.0, 24.0 + speed * 24.0 * 0.008, 0.0, CanvasPanContactPhase::Up) });
			check((released.phase == CanvasPanPhase::Coasting) == (speed > 30.0),
				"release uses Chromium thirty DIP per second threshold without old 250 gate");
		}

	}

	void TestPointerTopology(Checks& check)
	{
		// §3 #1、#2、#7：独立 slop 起点与 POINTER_DOWN 基线，第二指按在远处不构成位移。
		CanvasPanDynamics pending;
		auto out = StepAt(pending, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		check(out.pointerCount == 1 && out.focus.x == 0.0f && out.contentDelta.x == 0.0f,
			"rule 1: first Down establishes focus and emits no scroll");
		StepAt(pending, 8.0, { Contact(1, 8.0, 8.0) });
		out = StepAt(pending, 9.0, { Contact(2, 9.0, 1000.0, 0.0, CanvasPanContactPhase::Down) });
		check(out.pointerCount == 2 && out.contentDelta.x == 0.0f && !out.scrolling,
			"rule 2: second Down inside slop rebases without jumping");
		check(pending.VelocityTracker().SampleCount(1) == 2 && pending.VelocityTracker().SampleCount(2) == 1,
			"rule 2: another pointer Down does not add a fake sample to first pointer history");
		out = StepAt(pending, 16.0, { Contact(1, 16.0, 8.0), Contact(2, 16.0, 1010.0) });
		check(!out.scrolling && out.contentDelta.x == 0.0f, "rule 7: distant second Down has its own independent slop origin");
		out = StepAt(pending, 24.0, { Contact(1, 24.0, 8.0), Contact(2, 24.0, 1016.0) });
		check(out.scrolling && out.contentDelta.x == 0.5f,
			"rule 7: average includes zero contribution from pointer still inside its own slop");

		// §3 #3、#4：已起滑后增减触点只重基线，不带入终态位置。
		CanvasPanDynamics dragging;
		StepAt(dragging, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(dragging, 8.0, { Contact(1, 8.0, 24.0) });
		out = StepAt(dragging, 9.0, { Contact(2, 9.0, 1000.0, 0.0, CanvasPanContactPhase::Down) });
		check(out.scrolling && out.contentDelta.x == 0.0f, "rule 3: adding a pointer to active scroll preserves scroll without jump");
		out = StepAt(dragging, 16.0, { Contact(1, 16.0, 34.0), Contact(2, 16.0, 1010.0) });
		check.Near(out.contentDelta.x, 10.0, 1e-5, "rule 3: both pointers translate centroid one to one after rebase");
		out = StepAt(dragging, 17.0, { Contact(2, 17.0, 50000.0, 0.0, CanvasPanContactPhase::Up) });
		check(out.heldContacts == 1 && out.focus.x == 34.0f && out.contentDelta.x == 0.0f,
			"rule 4: pointer Up excludes terminal pointer from focus and displacement");
		out = StepAt(dragging, 24.0, { Contact(1, 24.0, 44.0) });
		check.Near(out.contentDelta.x, 10.0, 1e-5, "rule 4: surviving pointer continues from its own current baseline");

		CanvasPanDynamics opposed;
		StepAt(opposed, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(opposed, 1.0, { Contact(2, 1.0, 300.0, 0.0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 4; ++n)
			StepAt(opposed, n * 8.0, { Contact(1, n * 8.0, n * 24.0), Contact(2, n * 8.0, 300.0 - n * 24.0) });
		out = StepAt(opposed, 33.0, { Contact(2, 33.0, 204.0, 0.0, CanvasPanContactPhase::Up) });
		check(!opposed.VelocityTracker().Estimate(1, kFreq).valid && !opposed.VelocityTracker().Estimate(2, kFreq).valid,
			"rule 4: opposing lifted finger clears the shared tracker for all pointers");
		out = StepAt(opposed, 34.0, { Contact(1, 34.0, 96.0, 0.0, CanvasPanContactPhase::Up) });
		check(out.phase == CanvasPanPhase::Idle, "rule 4: release immediately after opposing lift cannot fling");

		// §3 #5：不同手速同向时不清窗，最后抬起者用自己的速度，不用质心平均速度。
		CanvasPanDynamics lastPointer;
		StepAt(lastPointer, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(lastPointer, 1.0, { Contact(2, 1.0, 300.0, 0.0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 8; ++n)
			StepAt(lastPointer, 1.0 + n * 8.0,
				{ Contact(1, 1.0 + n * 8.0, n * 24.0), Contact(2, 1.0 + n * 8.0, 300.0 + n * 8.0) });
		StepAt(lastPointer, 66.0, { Contact(1, 66.0, 192.0, 0.0, CanvasPanContactPhase::Up) });
		out = StepAt(lastPointer, 67.0, { Contact(2, 67.0, 364.0, 0.0, CanvasPanContactPhase::Up) });
		check(out.released && out.phase == CanvasPanPhase::Coasting, "rule 5: last pointer releases into fling");
		check.Near(out.boosterStartingVelocity.x, 1000.0, 0.5, "rule 5: fling takes last pointer velocity rather than multi-pointer mean");

		// §3 #6：曾有三指后，即使当前只剩一指仍跳过 slop。
		CanvasPanDynamics three;
		StepAt(three, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down),
			Contact(2, 0.0, 100.0, 0.0, CanvasPanContactPhase::Down), Contact(3, 0.0, 200.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(three, 1.0, { Contact(2, 1.0, 100.0, 0.0, CanvasPanContactPhase::Up), Contact(3, 1.0, 200.0, 0.0, CanvasPanContactPhase::Up) });
		out = StepAt(three, 8.0, { Contact(1, 8.0, 1.0) });
		check(!out.scrolling && out.contentDelta.x == 0.0f,
			"rule 6: detector bypasses slop after three pointers but Provider subtracts slop for current single pointer");
		out = StepAt(three, 16.0, { Contact(1, 16.0, 16.0) });
		check(out.scrolling && out.contentDelta.x == 1.0f,
			"Provider retries first-scroll slop subtraction after zero delta was suppressed");
		CanvasPanDynamics currentThree;
		StepAt(currentThree, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down),
			Contact(2, 0.0, 100.0, 0.0, CanvasPanContactPhase::Down), Contact(3, 0.0, 200.0, 0.0, CanvasPanContactPhase::Down) });
		out = StepAt(currentThree, 8.0, { Contact(1, 8.0, 1.0), Contact(2, 8.0, 101.0), Contact(3, 8.0, 201.0) });
		check(out.scrolling && out.contentDelta.x == 1.0f, "current three-pointer first scroll does not subtract slop");

		// §3 #10：单指 Cancelled 不贡献速度、不做负点积清空。
		CanvasPanDynamics cancelled;
		StepAt(cancelled, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(cancelled, 1.0, { Contact(2, 1.0, 300.0, 0.0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 4; ++n)
			StepAt(cancelled, n * 8.0, { Contact(1, n * 8.0, n * 24.0), Contact(2, n * 8.0, 300.0 - n * 24.0) });
		out = StepAt(cancelled, 33.0, { Contact(2, 33.0, -50000.0, 0.0, CanvasPanContactPhase::Cancelled) });
		check(out.contentDelta.x == 0.0f && out.focus.x == 96.0f && cancelled.VelocityTracker().Estimate(1, kFreq).valid,
			"rule 10: partial cancellation rebases focus and preserves other pointer velocity");
		out = StepAt(cancelled, 34.0, { Contact(1, 34.0, 96.0, 0.0, CanvasPanContactPhase::Up) });
		check(out.phase == CanvasPanPhase::Coasting && out.boosterStartingVelocity.x > 0,
			"rule 10: cancelled opposing pointer cannot suppress surviving fling");

		// §3 #13：静止增减时contentDelta和视口不变；focus按新held集合重算。
		CanvasPanDynamics still;
		CanvasViewportState stillViewport{ 123.0f, -45.0f };
		StepAt(still, 0.0, { Contact(1, 0.0, 10.0, 20.0, CanvasPanContactPhase::Down) });
		out = StepAt(still, 1.0, { Contact(2, 1.0, 30.0, 40.0, CanvasPanContactPhase::Down) });
		ApplyCanvasContentTranslation(stillViewport, out.contentDelta);
		check(out.contentDelta.x == 0 && out.contentDelta.y == 0 && stillViewport.x == 123.0f && stillViewport.y == -45.0f,
			"rule 13: stationary pointer insertion keeps content delta zero and viewport unchanged");
		// 非零绝对坐标的LSQ float QR有舍入残差，只用绝对容差，不在生产中加归零阈值。
		check(std::hypot(out.contentVelocity.x, out.contentVelocity.y) < 0.01,
			"stationary insertion velocity stays within float LSQ absolute rounding tolerance");
		check(out.focus.x == 20 && out.focus.y == 30 && out.pointerCount == 2,
			"focus is recomputed for current held pointers after insertion");
		check.Near(out.span, std::sqrt(200.0), 1e-6, "span is mean distance of pointers from focus");
		check(out.scaleDelta == 1.0, "scale channel remains identity");
		out = StepAt(still, 2.0, { Contact(2, 2.0, 30.0, 40.0, CanvasPanContactPhase::Up) });
		ApplyCanvasContentTranslation(stillViewport, out.contentDelta);
		check(out.contentDelta.x == 0 && out.contentDelta.y == 0 && stillViewport.x == 123.0f && stillViewport.y == -45.0f &&
			out.focus.x == 10 && out.focus.y == 20 && out.span == 0 && out.scaleDelta == 1.0,
			"rule 13: stationary removal rebases centroid and span without changing viewport");
		check(std::hypot(out.contentVelocity.x, out.contentVelocity.y) < 0.01,
			"stationary removal velocity stays within float LSQ absolute rounding tolerance");

		// §3 #14、#15：容量外与未知 key 不能干扰现有手势，迟到 Move 不能复活已抬起指。
		CanvasPanDynamics crowd;
		std::vector<CanvasPanContactSample> downs;
		for (uint64_t key = 1; key <= kCanvasPanMaximumContacts + 2; ++key)
			downs.push_back(Contact(key, 0.0, static_cast<double>(key), 0.0, CanvasPanContactPhase::Down));
		out = StepAt(crowd, 0.0, downs);
		check(out.heldContacts == kCanvasPanMaximumContacts && out.contentDelta.x == 0,
			"rule 14: excess Down is ignored without disturbing accepted contacts");
		out = StepAt(crowd, 1.0, { Contact(999, 1.0, 0.0, 0.0, CanvasPanContactPhase::Up),
			Contact(998, 1.0, 0.0, 0.0, CanvasPanContactPhase::Cancelled) });
		check(out.heldContacts == kCanvasPanMaximumContacts && out.contentDelta.x == 0,
			"rule 15: unknown terminal keys are ignored");
		CanvasPanDynamics late;
		StepAt(late, 0.0, { Contact(1, 0.0, 10.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(late, 1.0, { Contact(1, 1.0, 10.0, 0.0, CanvasPanContactPhase::Up) });
		out = StepAt(late, 2.0, { Contact(1, 2.0, 100.0), Contact(2, 2.0, 100.0) });
		check(out.heldContacts == 0 && out.phase == CanvasPanPhase::Idle && out.contentDelta.x == 0,
			"rule 15: late and unannounced Move samples cannot create contacts");
	}

	void TestFrameEventOrdering(Checks& check)
	{
		CanvasPanDynamics merged;
		StepAt(merged, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(merged, 1.0, { Contact(2, 1.0, 300.0, 0.0, CanvasPanContactPhase::Down) });
		auto out = StepAt(merged, 12.0,
			{ Contact(2, 11.0, 324.0), Contact(1, 10.0, 20.0), Contact(1, 8.0, 16.0) });
		check(merged.VelocityTracker().LastSampleQpc() == QpcMs(11.0) &&
			merged.VelocityTracker().SampleCount(1) == 3 && merged.VelocityTracker().SampleCount(2) == 2,
			"all real Move samples retain their own pointer histories and latest event time");
		check(out.focus.x == 172.0f && out.pointerCount == 2,
			"merged Move uses latest per-pointer position after sorting QPC");

		// 拓扑边界前后的Move分别消费，不能把旧集合的位移延后到新集合。
		CanvasPanDynamics topology;
		CanvasViewportState topologyViewport;
		StepAt(topology, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		out = StepAt(topology, 8.0, { Contact(1, 8.0, 24.0) });
		ApplyCanvasContentTranslation(topologyViewport, out.contentDelta);
		out = StepAt(topology, 25.0, { Contact(2, 24.0, 1010.0), Contact(1, 24.0, 44.0),
			Contact(2, 17.0, 1000.0, 0.0, CanvasPanContactPhase::Down), Contact(1, 16.0, 34.0) });
		ApplyCanvasContentTranslation(topologyViewport, out.contentDelta);
		check(out.contentDelta.x == 20.0f && out.contentDelta.y == 0.0f && out.pointerCount == 2 && out.focus.x == 527.0f,
			"Move Down Move in one frame dispatches ten DIP before and ten after pointer insertion");
		check(topology.VelocityTracker().SampleCount(1) == 4 && topology.VelocityTracker().SampleCount(2) == 2,
			"Move Down Move retains each pointer real samples without topology-generated samples");
		out = StepAt(topology, 41.0, { Contact(1, 40.0, 64.0), Contact(2, 33.0, 1020.0, 0.0, CanvasPanContactPhase::Up),
			Contact(1, 32.0, 54.0), Contact(2, 32.0, 1020.0) });
		ApplyCanvasContentTranslation(topologyViewport, out.contentDelta);
		check(out.contentDelta.x == 20.0f && out.contentDelta.y == 0.0f && out.pointerCount == 1 && out.focus.x == 64.0f &&
			topology.VelocityTracker().LastSampleQpc() == QpcMs(40.0) && topologyViewport.x == -49.0f,
			"Move Up Move in one frame dispatches ten DIP before and ten after pointer removal");

		// §3 #9：同帧旧手势结束后新 Down 会取消刚生成的 fling；后续新指仍从独立基线开始。
		CanvasPanDynamics handoff;
		StepAt(handoff, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(handoff, 8.0, { Contact(1, 8.0, 24.0) });
		StepAt(handoff, 16.0, { Contact(1, 16.0, 48.0) });
		out = StepAt(handoff, 25.0,
			{ Contact(2, 24.0, 1000.0, 0.0, CanvasPanContactPhase::Down), Contact(1, 20.0, 48.0, 0.0, CanvasPanContactPhase::Up) });
		check(out.heldContacts == 1 && out.focus.x == 1000.0f && out.contentDelta.x == 0 && out.phase == CanvasPanPhase::Locked,
			"rule 9: QPC sorting completes old Up before later new Down without fling advance");
		out = StepAt(handoff, 32.0, { Contact(2, 32.0, 1005.0) });
		check(!out.scrolling && out.contentDelta.x == 0.0f, "rule 9: new gesture gets its own slop origin");

		CanvasPanDynamics simultaneous;
		out = StepAt(simultaneous, 8.0,
			{ Contact(1, 8.0, 0.0, 0.0, CanvasPanContactPhase::Up), Contact(1, 8.0, 20.0),
				Contact(1, 8.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		check(out.heldContacts == 0 && out.phase == CanvasPanPhase::Idle,
			"rule 9: same QPC events are stably ordered Down then Move then Up");

	}

	void TestOrderedInputEquivalence(Checks& check)
	{
		// 同阶段重复 Down/Move 故意保留不同坐标，稳定次序必须决定第一个有效样本。
		const std::array<CanvasPanContactSample, 13> ordered{ {
			Contact(1, 0, 0, 0, CanvasPanContactPhase::Down),
			Contact(1, 0, 1000, 0, CanvasPanContactPhase::Down),
			Contact(1, 8, 24), Contact(1, 8, 9000),
			Contact(2, 9, 300, 0, CanvasPanContactPhase::Down),
			Contact(1, 16, 48), Contact(2, 16, 324),
			Contact(1, 24, 72), Contact(1, 24, 9999), Contact(2, 24, 348),
			Contact(2, 25, 348, 0, CanvasPanContactPhase::Up),
			Contact(1, 32, 96), Contact(1, 33, 96, 0, CanvasPanContactPhase::Up) } };
		// 手工排列不同时间的组，不交换同时间/阶段组内顺序；不复制生产排序充当 oracle。
		const std::array<size_t, 13> permutation{ 12, 10, 4, 0, 1, 2, 3, 11, 5, 6, 7, 8, 9 };
		std::array<CanvasPanContactSample, 13> unordered;
		for (size_t i = 0; i < permutation.size(); ++i) unordered[i] = ordered[permutation[i]];
		CanvasPanDynamics direct, sorted;
		const auto compare = [&](const CanvasPanFrameOutput& a, const CanvasPanFrameOutput& b)
		{
			check(a.phase == b.phase && a.sourceKey == b.sourceKey && a.heldContacts == b.heldContacts &&
				a.scrolling == b.scrolling && a.released == b.released && a.releaseReason == b.releaseReason &&
				a.rejectedSamples == b.rejectedSamples && a.sampleCount == b.sampleCount &&
				a.reversalCount == b.reversalCount && a.timingResetCount == b.timingResetCount,
				"ordered span and stable-sort fallback keep identical state and event accounting");
			const auto same = [](CanvasVector x, CanvasVector y) { return x.x == y.x && x.y == y.y; };
			check(same(a.contentDelta, b.contentDelta) && same(a.contentVelocity, b.contentVelocity) &&
				same(a.handVelocity, b.handVelocity) && a.handVelocityKnown == b.handVelocityKnown &&
				same(a.rawReleaseVelocity, b.rawReleaseVelocity) && same(a.selectedReleaseVelocity, b.selectedReleaseVelocity) &&
				same(a.boostedReleaseVelocity, b.boostedReleaseVelocity) && same(a.boosterStartingVelocity, b.boosterStartingVelocity) &&
				same(a.boosterCurrentVelocity, b.boosterCurrentVelocity) && same(a.flingOffset, b.flingOffset) &&
				a.releaseSpeed == b.releaseSpeed && a.releaseAgeSeconds == b.releaseAgeSeconds &&
				a.releaseSampleCount == b.releaseSampleCount && a.releaseSpanSeconds == b.releaseSpanSeconds &&
				a.releaseTailSpeedLimit == b.releaseTailSpeedLimit && a.releaseTailSpeedLimited == b.releaseTailSpeedLimited &&
				a.boosterCutoffSeconds == b.boosterCutoffSeconds && same(a.focus, b.focus) &&
				a.span == b.span && a.pointerCount == b.pointerCount && a.scaleDelta == b.scaleDelta,
				"ordered span and fallback preserve exact geometry, release and every subsequent friction sample");
		};
		const auto a = StepAt(direct, 40, ordered), b = StepAt(sorted, 40, unordered);
		compare(a, b);
		check(a.contentDelta.x == 81 && a.contentDelta.y == 0 && a.rejectedSamples == 2 &&
			a.phase == CanvasPanPhase::Coasting && a.heldContacts == 0,
			"stable duplicate rejection and topology rebases retain the independently expected 81 DIP drag");
		check.Near(a.selectedReleaseVelocity.x, 3000, .01,
			"ordered fast path retains the last pointer's real 3000 DIP/s release");
		for (int ms = 48; ms <= 1000; ms += 8) compare(StepAt(direct, ms), StepAt(sorted, ms));
		check(!direct.Active() && !sorted.Active(), "both event orders reach the same idle endpoint");
	}

	void TestCancelPenAndStop(Checks& check)
	{
		CanvasPanDynamics grab;
		auto out = StartFling(grab);
		check(out.phase == CanvasPanPhase::Coasting && out.boosterStartingVelocity.x > 0.0f, "setup: release starts product friction fling");
		const float starting = out.boosterStartingVelocity.x;
		out = StepAt(grab, 40.0, { Contact(2, 40.0, 900.0, 0.0, CanvasPanContactPhase::Down) });
		check(out.contentDelta.x == 0 && out.contentDelta.y == 0 && out.contentVelocity.x == 0 && out.contentVelocity.y == 0,
			"rule 12: first Down cancels inertia before same-frame progress");
		check(out.boosterStartingVelocity.x == starting, "rule 12: landing finger preserves previous fling booster");
		const double cutoff = out.boosterCutoffSeconds;
		out = StepAt(grab, 41.0, { Contact(3, 41.0, 1100.0, 0.0, CanvasPanContactPhase::Down) });
		check(out.contentDelta.x == 0 && out.boosterCutoffSeconds == cutoff,
			"rule 12: additional pointer Down does not repeat FlingCancel");
		StepAt(grab, 42.0, { Contact(3, 42.0, 1100.0, 0.0, CanvasPanContactPhase::Cancelled) });
		out = StepAt(grab, 43.0, { Contact(2, 43.0, 900.0, 0.0, CanvasPanContactPhase::Cancelled) });
		check(out.phase == CanvasPanPhase::Idle && out.contentDelta.x == 0 && out.boosterStartingVelocity.x == 0,
			"product cancellation produces no fling and clears the residual boost candidate");

		CanvasPanDynamics hover;
		StartFling(hover);
		out = StepAt(hover, 40.0, {}, true);
		check(out.phase == CanvasPanPhase::Idle && out.contentDelta.x == 0 && out.boosterStartingVelocity.x == 0,
			"pen hover immediately stops fling and resets booster");
		StepAt(hover, 48.0, { Contact(2, 48.0, 100.0, 0.0, CanvasPanContactPhase::Down) }, true);
		out = StepAt(hover, 56.0, { Contact(2, 56.0, 124.0) }, true);
		check(out.heldContacts == 1 && out.scrolling, "hover does not interrupt a held touch gesture");
		out = StepAt(hover, 64.0, { Contact(2, 64.0, 148.0) }, true);
		check.Near(out.contentDelta.x, 24.0, 1e-5, "touch scroll remains one to one while pen hovers");
		out = StepAt(hover, 65.0, {}, false, true);
		check(out.phase == CanvasPanPhase::Idle && out.heldContacts == 0 && out.contentDelta.x == 0 && out.boosterStartingVelocity.x == 0,
			"pen contact clears live gesture and booster in same frame");
		out = StepAt(hover, 72.0, { Contact(2, 72.0, 172.0) });
		check(out.heldContacts == 0 && out.contentDelta.x == 0, "old Move after pen Stop cannot revive touch gesture");

		CanvasPanDynamics stopped;
		StartFling(stopped);
		stopped.Stop(); stopped.Stop();
		out = StepAt(stopped, 40.0);
		check(!stopped.Active() && out.phase == CanvasPanPhase::Idle && out.boosterStartingVelocity.x == 0 && out.contentDelta.x == 0,
			"programmatic Stop is idempotent and leaves no residual velocity or booster");

		CanvasPanDynamics aged;
		StepAt(aged, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(aged, 8.0, { Contact(1, 8.0, 24.0) });
		StepAt(aged, 16.0, { Contact(1, 16.0, 48.0) });
		out = StepAt(aged, 96.0, { Contact(1, 96.0, 48.0, 0.0, CanvasPanContactPhase::Up) });
		check(out.released && out.phase == CanvasPanPhase::Idle && out.releaseSpeed == 0.0 && !aged.Active(),
			"eighty millisecond release clears velocity and cannot start a fling");
		check.Near(out.releaseAgeSeconds, 0.080, 1e-12,
			"release diagnostics preserve sample age even when PointerUp clears tracker");

		CanvasPanDynamics axes;
		StartFling(axes, 0.0, 1, 3000.0, 1500.0);
		axes.ClearAxis(true, false);
		out = StepAt(axes, 40.0);
		check(out.phase == CanvasPanPhase::Coasting && out.contentVelocity.x == 0 && out.contentDelta.x == 0 && out.contentDelta.y > 0,
			"hard limit on x cancels only that fling axis");
		axes.ClearAxis(false, true);
		out = StepAt(axes, 48.0);
		check(out.phase == CanvasPanPhase::Idle && out.contentVelocity.x == 0 && out.contentVelocity.y == 0 && out.boosterStartingVelocity.x == 0,
			"clearing both axes ends fling and resets booster");
	}

	void TestInvalidSamplesAndTranslation(Checks& check)
	{
		CanvasPanDynamics dynamics;
		StepAt(dynamics, 0.0, { Contact(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		StepAt(dynamics, 8.0, { Contact(1, 8.0, 24.0) });
		StepAt(dynamics, 16.0, { Contact(1, 16.0, 48.0) });
		const auto count = dynamics.VelocityTracker().SampleCount(1);
		const auto velocity = dynamics.ContentVelocity();
		const auto unchanged = [&](const CanvasPanFrameOutput& out, const char* name)
		{
			check(out.contentDelta.x == 0 && out.contentDelta.y == 0 && out.heldContacts == 1 &&
				dynamics.VelocityTracker().SampleCount(1) == count && dynamics.ContentVelocity().x == velocity.x, name);
		};
		unchanged(StepAt(dynamics, 17.0, { Contact(1, 17.0, std::numeric_limits<double>::quiet_NaN()) }), "nonfinite sample preserves state");
		unchanged(StepAt(dynamics, 18.0, { Contact(1, 16.0, 999.0) }), "repeated Move QPC preserves state");
		unchanged(StepAt(dynamics, 19.0, { Contact(1, 15.0, 999.0) }), "backwards Move QPC preserves state");
		unchanged(StepAt(dynamics, 10.0, { Contact(1, 20.0, 999.0) }), "backwards frame QPC preserves state");
		CanvasPanFrameInput invalid;
		unchanged(dynamics.Step(invalid), "invalid frame clock and frequency preserve state");
		const auto resumed = StepAt(dynamics, 24.0, { Contact(1, 24.0, 72.0) });
		check.Near(resumed.contentDelta.x, 24.0, 1e-5, "valid Move resumes from unchanged baseline after rejection");

		CanvasViewportState viewport;
		const auto translated = ApplyCanvasContentTranslation(viewport, { 24.0f, -12.0f });
		check(translated.x == -24 && translated.y == 12 && viewport.x == -24 && viewport.y == 12,
			"content-following delta converts once to opposite viewport translation");
		const auto canvas = ScreenToCanvas({ 100, 200 }, viewport);
		const auto screen = CanvasToScreen(canvas, viewport);
		check(screen.x == 100 && screen.y == 200, "screen and canvas transforms preserve translation roundtrip");
		viewport.x = -kCanvasViewportLimitDip;
		const auto clamped = ApplyCanvasContentTranslationChecked(viewport, { 1, 0 });
		check(clamped.xClamped && !clamped.yClamped && clamped.viewportDelta.x == 0,
			"existing hard viewport limit reports only the clamped axis");
	}

	void TestReliableProductRelease(Checks& check)
	{
		CanvasPanReleaseVelocityTracker tracker;
		const auto add = [&](double ms, double x, double y = 0.0)
			{ return tracker.AddSample(1, { QpcMs(ms), x, y }, kFreq); };
		add(0, 0); add(10, 30);
		check(!tracker.Estimate(1, kFreq).valid, "two points do not launch product inertia");
		add(20, 60);
		check.Near(tracker.Estimate(1, kFreq).x, 3000, 1e-8, "three points across twenty milliseconds establish velocity");
		add(30, 90); add(40, 120); add(50, 150); add(60, 180); add(70, 210); add(80, 240);
		check(add(90, 210) == CanvasPanSampleDisposition::Reversed && !tracker.Estimate(1, kFreq).valid,
			"confirmed short reversal clears old direction before new tail qualifies");
		add(100, 180);
		check.Near(tracker.Estimate(1, kFreq).x, -3000, 1e-8, "reliable reversed tail cannot retain LSQ2 old sign or overshoot");
		const auto count = tracker.SampleCount(1);
		tracker.AddSample(2, { QpcMs(105), 5000, 0 }, kFreq);
		check(tracker.SampleCount(1) == count && tracker.LastSampleQpc(1) == QpcMs(100),
			"another pointer event cannot invent a new timestamp or stationary sample");

		tracker.Clear(); add(0, 0); add(10, 30); add(20, 60);
		check(add(21, 59.8) == CanvasPanSampleDisposition::Accepted && tracker.SampleCount(1) == 4,
			"subpixel reverse jitter alone does not clear a valid tail");
		check(add(22, 60) == CanvasPanSampleDisposition::Accepted && tracker.SampleCount(1) == 5,
			"subpixel round trip preserves velocity evidence");
		tracker.Clear(); add(0, 0); add(20, 30);
		check(add(21, 60) == CanvasPanSampleDisposition::Accepted && !tracker.Estimate(1, kFreq).valid &&
			tracker.Reason(1, kFreq) == CanvasPanReleaseReason::InsufficientSamples,
			"a compressed third arrival preserves raw history but lacks three independent time anchors");
		tracker.Clear();
		for (int n = 0; n < 10; ++n) add(n * (1000.0 / 30.0), n * 100.0);
		check(tracker.Estimate(1, kFreq).valid && tracker.SampleCount(1) >= 3,
			"thirty Hz real input keeps the left window anchor and can still fling");
		check.Near(tracker.Estimate(1, kFreq).x, 3000, .02, "low reporting rate linear tail remains accurate");

		// 生产feel探针：240fps每帧4次报告、10MHz QPC截断、float像素再换回DIP。
		// 12.5DIP / 10416ticks略大于12000，不能按一条超限线段把合法恒速历史清掉。
		for (const double dipPerPixel : { 1.0, 1.0 / 1.5, 0.5, 0.75 })
		{
			CanvasPanDynamics quantized;
			const auto sampleAt = [&](int report, CanvasPanContactPhase phase)
			{
				const double ms = 1000.0 + report * (1000.0 / 960.0);
				const double position = static_cast<double>(static_cast<float>(300.0 + report * 12.5 / dipPerPixel)) * dipPerPixel;
				return CanvasPanContactSample{ 1, kOrigin + static_cast<int64_t>(ms * kFreq / 1000.0), position,
					static_cast<double>(static_cast<float>(300.0)) * dipPerPixel, phase };
			};
			const auto down = sampleAt(0, CanvasPanContactPhase::Down);
			StepAt(quantized, 1000.0, std::span(&down, 1));
			size_t resets = 0;
			for (int frame = 0; frame < 96; ++frame)
			{
				std::array<CanvasPanContactSample, 4> batch;
				for (int sub = 0; sub < 4; ++sub) batch[sub] = sampleAt(frame * 4 + sub + 1, CanvasPanContactPhase::Move);
				const auto out = StepAt(quantized, 1000.0 + (frame + 1) * (1000.0 / 240.0), batch);
				resets += out.timingResetCount;
			}
			auto up = sampleAt(384, CanvasPanContactPhase::Up); up.qpc = QpcMs(1400.1);
			const auto released = StepAt(quantized, 1404.1667, std::span(&up, 1));
			check(resets == 0 && released.phase == CanvasPanPhase::Coasting,
				"quantized legal maximum-speed reports preserve history instead of repeatedly resetting timing");
			check.Within(released.releaseSpeed, 11999.0, 12000.01,
				"quantized maximum speed remains accurate and subject to the unchanged vector cap");
		}
		tracker.Clear(); add(0, 0); add(20, 30);
		check(add(21, 60) == CanvasPanSampleDisposition::Accepted && tracker.SampleCount(1) == 3 &&
			!tracker.Estimate(1, kFreq).valid,
			"short fast displacement is not bad time by itself, while insufficient independent evidence still blocks release");

		for (const double reversalDuration : { 10.0, 20.0, 40.0 })
		{
			CanvasPanDynamics motion;
			std::vector<CanvasPanContactSample> samples{ Contact(1, 0, 0, 0, CanvasPanContactPhase::Down) };
			for (int n = 1; n <= 8; ++n) samples.push_back(Contact(1, n * 10.0, n * 30.0));
			for (int n = 1; n <= static_cast<int>(reversalDuration / 10.0); ++n)
				samples.push_back(Contact(1, 80 + n * 10.0, 240 - n * 30.0));
			const double up = 81 + reversalDuration;
			samples.push_back(Contact(1, up, 240 - reversalDuration * 3, 0, CanvasPanContactPhase::Up));
			const auto released = StepAt(motion, 250, samples);
			check(released.reversalCount == 1 && released.selectedReleaseVelocity.x <= 0,
				"queued reversal and Up never launch in the old direction");
			check.Near(released.contentDelta.x, 240 - reversalDuration * 3 - 15, 1e-4,
				"hitch release retains every real Move geometry without pre-render fling catchup");
			check((released.phase == CanvasPanPhase::Coasting) == (reversalDuration >= 20),
				"short reverse releases zero until a complete twenty-millisecond new tail exists");
			if (reversalDuration >= 20)
				check.Near(released.selectedReleaseVelocity.x, -3000, .01, "queued tail velocity uses sample times not consumption time");
		}

		CanvasPanDynamics turning;
		const auto turnRelease = StepAt(turning, 41, {
			Contact(1, 0, 0, 0, CanvasPanContactPhase::Down), Contact(1, 10, 20, 0),
			Contact(1, 20, 40, 0), Contact(1, 30, 40, 20), Contact(1, 40, 20, 20),
			Contact(1, 41, 20, 20, CanvasPanContactPhase::Up) });
		check(turnRelease.phase == CanvasPanPhase::Idle && turnRelease.releaseReason == CanvasPanReleaseReason::DirectionConflict,
			"successive right-angle turns cannot release behind the latest reliable direction");

		tracker.Clear(); add(0, 0, 0); add(10, 30, 30); add(20, 60, 60);
		tracker.ClearAxis(true, false);
		check(add(30, 60, 59) == CanvasPanSampleDisposition::Reversed,
			"clearing one axis keeps the surviving reversal direction normalized");
		tracker.Clear(); add(0, 0); add(1, .4);
		check(!tracker.RecentSpeedLimit(1, kFreq, { 400, 0 }), "one short quantized segment cannot cap release velocity");
		add(2, .8); add(3, 1.2);
		check.Near(tracker.RecentSpeedLimit(1, kFreq, { 400, 0 }).value_or(0.0), 400, .001,
			"high-rate real points accumulate enough time and displacement for a reliable terminal segment");

		for (const double reportHz : { 30.0, 60.0, 120.0, 240.0 })
		{
			for (const double acceleration : { -7500.0, 0.0, 7500.0 })
			{
				CanvasPanDynamics motion;
				const double initial = acceleration < 0 ? 4000.0 : 1000.0;
				const int reportCount = static_cast<int>(reportHz * .4);
				const double stepSeconds = .4 / reportCount;
				std::vector<CanvasPanContactSample> samples{ Contact(1, 0, 0, 0, CanvasPanContactPhase::Down) };
				for (int n = 1; n <= reportCount; ++n)
				{
					const double t = n * stepSeconds;
					samples.push_back(Contact(1, t * 1000, initial * t + .5 * acceleration * t * t));
				}
				samples.push_back(Contact(1, 401, initial * .4 + .5 * acceleration * .16, 0, CanvasPanContactPhase::Up));
				const auto released = StepAt(motion, 500, samples);
				const double terminal = initial + acceleration * .4;
				if (acceleration < 0)
				{
					check(released.releaseTailSpeedLimited, "decelerating release exposes its reliable terminal speed cap");
					check.Near(released.releaseSpeed, terminal - acceleration * stepSeconds * .5, .05,
						"decelerating release matches final real segment instead of lagging fifty-ms mean");
					check.Within(released.releaseSpeed / terminal, .85, 1.15, "deceleration preserves the existing product speed-quality bound");
				}
				else if (acceleration == 0)
					check.Near(released.releaseSpeed, terminal, .05, "terminal cap does not slow constant-speed motion");
				else
					check.Within(released.releaseSpeed / terminal, .85, 1.0, "accelerating motion retains conservative linear estimate without cap-induced loss");
			}
		}

		for (const double age : { 1.0, 30.0, 50.0, 70.0, 100.0 })
		{
			CanvasPanDynamics motion;
			StepAt(motion, 0, { Contact(1, 0, 0, 0, CanvasPanContactPhase::Down) });
			StepAt(motion, 10, { Contact(1, 10, 30) });
			StepAt(motion, 20, { Contact(1, 20, 60) });
			const auto out = StepAt(motion, 20 + age, { Contact(1, 20 + age, 60, 0, CanvasPanContactPhase::Up) });
			const double gate = std::clamp((70.0 - age) / 40.0, 0.0, 1.0);
			check.Near(out.releaseSpeed, 3000 * gate, .01, "release age fades continuously between thirty and seventy milliseconds");
			check.Near(out.rawReleaseVelocity.x, 3000, .01, "raw release diagnostic is independent from age gate");
			check(out.contentDelta.x == 0, "release consumption frame does not append inertial catchup");
		}
	}

	void TestProductFrictionAndResidual(Checks& check)
	{
		CanvasPanFlingCurve curve;
		curve.Start({ 3000, 1500 }, { 1920, 1080 });
		check.Near(curve.Distance().x / curve.Distance().y, 2.0, 1e-6,
			"vector friction preserves two-dimensional release direction");
		const double speed = std::hypot(3000.0, 1500.0);
		const double distance = speed * .3 - 90 * std::log1p(speed / 300);
		check.Near(std::hypot(curve.Distance().x, curve.Distance().y), distance, .001,
			"free glide agrees with independent 708 closed-form distance");
		for (const double fps : { 30.0, 60.0, 120.0, 240.0 })
		{
			CanvasPanFlingBooster booster;
			CanvasPanFlingController fling;
			fling.Start(Gesture(CanvasPanGestureType::FlingStart, 1, { 3000, 1500 }), 2, { 1920, 1080 }, booster);
			check(fling.Progress(2, booster).x == 0, "product animation begins at presentation without backdated offset");
			double x = 0, y = 0;
			for (int n = 1; fling.Active() && n < 2000; ++n)
			{
				const auto delta = fling.Progress(2 + n / fps, booster);
				check(delta.x >= 0 && delta.y >= 0, "friction never reverses while decelerating");
				x += delta.x; y += delta.y;
			}
			check.Near(x, curve.Distance().x, .001, "terminal tail is preserved at every presentation rate");
			check.Near(y, curve.Distance().y, .001, "diagonal endpoint does not depend on presentation rate");
		}

		CanvasPanDynamics continued;
		StartFling(continued);
		const auto coast = StepAt(continued, 120);
		const double residual = coast.contentVelocity.x;
		auto out = StepAt(continued, 121, { Contact(2, 121, 1000, 0, CanvasPanContactPhase::Down) });
		check(out.contentDelta.x == 0 && out.contentVelocity.x == 0, "recontact stops the prior animation immediately");
		for (int n = 1; n <= 4; ++n) StepAt(continued, 121 + n * 8,
			{ Contact(2, 121 + n * 8, 1000 + n * 8) });
		out = StepAt(continued, 154, { Contact(2, 154, 1032, 0, CanvasPanContactPhase::Up) });
		check.Near(out.selectedReleaseVelocity.x, 1000, .01, "new release speed is independent of previous fling");
		check.Near(out.boostedReleaseVelocity.x, 1000 + residual, .01,
			"same-direction continuation adds current residual instead of historical starting speed");
		check(out.boostedReleaseVelocity.x < 4000 && out.contentDelta.x == 0,
			"continuation neither restores initial speed nor advances before the release presentation");

		CanvasPanFlingBooster projected = BoostCandidate(3000);
		projected.ObserveProgressFling({ 400, 200 });
		const auto boosted = projected.GetVelocityForFlingStart(Gesture(CanvasPanGestureType::FlingStart, 1.03, { 1000, 0 }));
		check(boosted.x == 1400 && boosted.y == 0, "residual projection cannot introduce an old transverse component");
		projected = BoostCandidate(12000);
		projected.ObserveProgressFling({ 12000, 0 });
		const auto capped = projected.GetVelocityForFlingStart(Gesture(CanvasPanGestureType::FlingStart, 1.03, { 9000, 9000 }));
		check.Near(std::hypot(capped.x, capped.y), 12000, .01, "boost is limited by vector norm after adding residual");

		CanvasPanFlingController limited;
		CanvasPanFlingBooster limitedBooster;
		limited.Start(Gesture(CanvasPanGestureType::FlingStart, 1, { 3000, 1500 }), 1, { 1920, 1080 }, limitedBooster);
		limited.ClearAxis(true, false, limitedBooster);
		check(limitedBooster.CurrentVelocity().x == 0 && limitedBooster.CurrentVelocity().y == 1500,
			"axis clamp clears residual booster velocity before a following Down can observe it");

		for (const auto sources : { std::array<uint64_t, 2>{ 1, 2 }, std::array<uint64_t, 2>{ 2, 1 },
			std::array<uint64_t, 2>{ 0x100000001ULL, 0x100000002ULL }, std::array<uint64_t, 2>{ 1, 1 } })
		{
			CanvasPanDynamics switched;
			std::vector<CanvasPanContactSample> samples{
				Contact(1, 0, 0, 0, CanvasPanContactPhase::Down), Contact(1, 10, 30), Contact(1, 20, 60),
				Contact(1, 30, 90), Contact(1, 31, 90, 0, CanvasPanContactPhase::Up),
				Contact(2, 32, 1000, 0, CanvasPanContactPhase::Down), Contact(2, 42, 1010), Contact(2, 52, 1020),
				Contact(2, 62, 1030), Contact(2, 63, 1030, 0, CanvasPanContactPhase::Up) };
			for (auto& sample : samples) sample.sourceKey = sample.key == 1 ? sources[0] : sources[1];
			const auto released = StepAt(switched, 100, samples);
			check.Near(released.boostedReleaseVelocity.x, sources[0] == sources[1] ? 4000.0 : 1000.0, .01,
				"same-batch source transition clears residual at actual Down while same-source continuation still boosts");
		}

		CanvasPanDynamics weak;
		StartFling(weak);
		StepAt(weak, 40, { Contact(2, 40, 1000, 0, CanvasPanContactPhase::Down) });
		StepAt(weak, 48, { Contact(2, 48, 1024) });
		out = StepAt(weak, 49, { Contact(2, 49, 1024, 0, CanvasPanContactPhase::Up) });
		check(out.phase == CanvasPanPhase::Idle && out.boosterStartingVelocity.x == 0,
			"weak new tail clears prior residual instead of keeping it for a later gesture");

		CanvasPanDynamics overlap;
		StepAt(overlap, 0, { Contact(1, 0, 0, 0, CanvasPanContactPhase::Down) });
		StepAt(overlap, 30, { Contact(1, 30, 90) });
		out = StepAt(overlap, 40, { Contact(2, 20, 1000, 0, CanvasPanContactPhase::Down) });
		check(out.heldContacts == 2 && out.contentDelta.x == 0,
			"new contact lifecycle is accepted even if its timestamp predates another pointer sample");
		out = StepAt(overlap, 41, { Contact(2, 41, 1010) });
		check(out.heldContacts == 2 && out.focus.x == 550,
			"accepted delayed Down allows its following Move to participate normally");
		CanvasPanDynamics badTerminal;
		StepAt(badTerminal, 0, { Contact(1, 0, 0, 0, CanvasPanContactPhase::Down) });
		StepAt(badTerminal, 10, { Contact(1, 10, 30) });
		StepAt(badTerminal, 20, { Contact(1, 20, 60) });
		out = StepAt(badTerminal, 30, { Contact(1, 19, 60, 0, CanvasPanContactPhase::Up) });
		check(out.heldContacts == 0 && out.phase == CanvasPanPhase::Idle &&
			out.releaseReason == CanvasPanReleaseReason::TimingRejected,
			"terminal with invalid time closes the contact without velocity or a stuck held state");
	}

	// ---- 白板背景纹理的 CPU 几何（GPU 像素对比见 Inkeys.exe --draw3-whiteboard-pattern-test）----
	using PatternPrimitive = std::array<float, 5>; // x0, y0, x1, y1, halfWidth

	std::vector<PatternPrimitive> EnumeratePattern(const WhiteboardPatternParams& params,
		int left, int top, int right, int bottom)
	{
		std::vector<PatternPrimitive> primitives;
		EnumerateWhiteboardPatternPrimitives(params, left, top, right, bottom,
			[&](float x0, float y0, float x1, float y1, float half) {
				primitives.push_back({ x0, y0, x1, y1, half });
			});
		return primitives;
	}

	void TestPerSourceInertiaGate(Checks& check)
	{
		for (bool firstAllowed : { false, true }) for (bool secondAllowed : { false, true })
		{
			CanvasPanDynamics motion;
			std::vector<CanvasPanContactSample> samples;
			for (int gesture = 0; gesture < 2; ++gesture)
			{
				const double begin = gesture * 40.0;
				const double origin = gesture * 1000.0;
				const bool allowed = gesture ? secondAllowed : firstAllowed;
				samples.push_back(Contact(gesture + 1, begin, origin, 0, CanvasPanContactPhase::Down));
				for (int n = 1; n <= 3; ++n)
					samples.push_back(Contact(gesture + 1, begin + n * 10, origin + n * 30));
				samples.push_back(Contact(gesture + 1, begin + 31, origin + 90, 0, CanvasPanContactPhase::Up));
				for (auto& sample : samples)
					if (sample.key == static_cast<uint64_t>(gesture + 1))
					{ sample.sourceKey = gesture + 1; sample.allowInertia = allowed; }
			}
			const auto out = StepAt(motion, 100, samples);
			check.Near(out.contentDelta.x, 150, 1e-4,
				"per-source release gates preserve both complete same-frame drag geometries");
			check(out.heldContacts == 0 && (out.phase == CanvasPanPhase::Coasting) == secondAllowed,
				"same-frame source changes use each terminal's gate instead of one frame-wide choice");
			if (!secondAllowed)
				check(out.releaseReason == CanvasPanReleaseReason::Disabled &&
					out.boosterStartingVelocity.x == 0 && out.contentVelocity.x == 0,
					"disabled release closes normally and leaves no residual booster or free velocity");
		}
		CanvasPanDynamics disabled;
		auto down = Contact(1, 0, 0, 0, CanvasPanContactPhase::Down);
		down.allowInertia = false;
		StepAt(disabled, 0, { down });
		for (int n = 1; n <= 4; ++n)
		{
			auto move = Contact(1, n * 10, n * 30); move.allowInertia = false;
			const auto out = StepAt(disabled, n * 10, { move });
			check(out.heldContacts == 1 && out.phase == CanvasPanPhase::Locked,
				"disabled inertia never cancels the held gesture");
		}
		auto up = Contact(1, 41, 120, 0, CanvasPanContactPhase::Up); up.allowInertia = false;
		StepAt(disabled, 41, { up });
		check(StepAt(disabled, 100).contentDelta.x == 0 && !disabled.Active(),
			"disabled release does not restart on later empty frames");
	}

	struct AdaptedPan
	{
		std::unique_ptr<CanvasPanDynamics> dynamics = std::make_unique<CanvasPanDynamics>();
		std::array<CanvasPanMotionContext, 4> contexts = {};
		size_t contextCount = 1;
		std::array<CanvasPanAdmissionDecision, 128> decisions = {};
		CanvasPanFrameOutput Step(double ms, std::span<const CanvasPanContactSample> contacts = {})
		{
			CanvasPanFrameInput input;
			input.frameQpc = QpcMs(ms); input.qpcFrequency = kFreq; input.contacts = contacts;
			input.viewportSizeDip = { 1920, 1080 };
			input.motionContexts = std::span(contexts).first(contextCount); input.admissionDecisions = decisions;
			return dynamics->Step(input);
		}
		CanvasPanFrameOutput Step(double ms, std::initializer_list<CanvasPanContactSample> contacts)
		{ return Step(ms, std::span<const CanvasPanContactSample>(contacts.begin(), contacts.size())); }
	};

	CanvasPanContactSample AdaptedContact(uint64_t key, CanvasPanSourceClass kind, size_t context,
		double ms, double x, double y = 0, CanvasPanContactPhase phase = CanvasPanContactPhase::Move)
	{
		auto c = Contact(key, ms, x, y, phase);
		c.sourceKey = (static_cast<uint64_t>(kind) << 32) | 1; c.motionContextIndex = context; return c;
	}
	CanvasPanMotionContext PhysicalContext(CanvasPanMotionProfile profile, double x = .2, double y = .3)
	{
		auto c = MakeCanvasPanMotionContext(profile, x, y); c.inputMapped = true; c.outputKey = 7; return c;
	}
	CanvasPanFrameOutput AdaptedFling(AdaptedPan& pan, double vx, double vy = 0)
	{
		const auto& c = pan.contexts[0];
		const auto kind = c.profile == CanvasPanMotionProfile::ScreenPenPhysical ? CanvasPanSourceClass::Pen : CanvasPanSourceClass::Touch;
		pan.Step(0, { AdaptedContact(1, kind, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 4; ++n)
			pan.Step(n * 8, { AdaptedContact(1, kind, 0, n * 8, vx * n * .008 / c.motionPerDipX, vy * n * .008 / c.motionPerDipY) });
		return pan.Step(33, { AdaptedContact(1, kind, 0, 33, vx * .032 / c.motionPerDipX, vy * .032 / c.motionPerDipY, CanvasPanContactPhase::Up) });
	}

	void TestOverspeedHistoryAndReleaseQuality(Checks& check)
	{
		// 真实第二轮key75：19ms空档后的末尾五点簇不能因超过输出cap清掉历史。
		// 附件缺少早期ordinal2120；此处仅重放可见点，末尾簇/Up链完整，不补造缺失点。
		struct Recorded { int64_t qpc; double x, y; CanvasPanContactPhase phase; };
		using Phase = CanvasPanContactPhase;
		static constexpr Recorded trace[] = {
			{ 63343489330LL, 1296.076, 1314.066, Phase::Down },
			{ 63343492286LL, 1296.151, 1313.159, Phase::Move },
			{ 63343557401LL, 1296.227, 1312.176, Phase::Move },
			{ 63343558040LL, 1296.453, 1311.345, Phase::Move },
			{ 63343628243LL, 1296.983, 1310.287, Phase::Move },
			{ 63343629073LL, 1297.587, 1309.228, Phase::Move },
			{ 63343707398LL, 1298.495, 1307.792, Phase::Move },
			{ 63343780868LL, 1302.047, 1301.367, Phase::Move },
			{ 63343781862LL, 1305.751, 1293.959, Phase::Move },
			{ 63343854934LL, 1311.572, 1282.772, Phase::Move },
			{ 63343855675LL, 1319.584, 1268.031, Phase::Move },
			{ 63343944484LL, 1338.028, 1232.126, Phase::Move },
			{ 63344005488LL, 1348.006, 1212.397, Phase::Move },
			{ 63344012092LL, 1358.589, 1191.912, Phase::Move },
			{ 63344083890LL, 1370.079, 1170.671, Phase::Move },
			{ 63344089333LL, 1382.022, 1148.976, Phase::Move },
			{ 63344156204LL, 1394.872, 1126.602, Phase::Move },
			{ 63344159526LL, 1408.328, 1103.622, Phase::Move },
			{ 63344245529LL, 1422.009, 1080.794, Phase::Move },
			{ 63344248737LL, 1434.935, 1059.402, Phase::Move },
			{ 63344308814LL, 1449.146, 1036.951, Phase::Move },
			{ 63344314149LL, 1464.491, 1013.972, Phase::Move },
			{ 63344380053LL, 1480.668, 990.690, Phase::Move },
			{ 63344382920LL, 1497.524, 967.710, Phase::Move },
			{ 63344458773LL, 1514.986, 944.126, Phase::Move },
			{ 63344462909LL, 1532.750, 920.239, Phase::Move },
			{ 63344547405LL, 1551.496, 895.824, Phase::Move },
			{ 63344549776LL, 1570.998, 871.257, Phase::Move },
			{ 63344610584LL, 1591.635, 846.463, Phase::Move },
			{ 63344613520LL, 1613.102, 821.745, Phase::Move },
			{ 63344804141LL, 1635.099, 797.405, Phase::Move },
			{ 63344813873LL, 1657.474, 773.972, Phase::Move },
			{ 63344822912LL, 1680.151, 751.446, Phase::Move },
			{ 63344834415LL, 1703.131, 729.676, Phase::Move },
			{ 63344841893LL, 1726.337, 708.359, Phase::Move },
			{ 63344916687LL, 1726.337, 708.359, Phase::Up },
		};
		for (const bool queued : { false, true })
		{
			CanvasPanDynamics dynamics;
			const auto context = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical, .19444445, .19791666);
			std::vector<CanvasPanContactSample> contacts;
			for (const auto& recorded : trace)
				contacts.push_back({ 1, recorded.qpc, recorded.x * .5, recorded.y * .5, recorded.phase,
					(static_cast<uint64_t>(CanvasPanSourceClass::Pen) << 32) | 3843376640u, true, 0 });
			std::array<CanvasPanAdmissionDecision, 1> decisions;
			CanvasPanFrameInput input;
			input.qpcFrequency = kFreq; input.viewportSizeDip = { 1920, 1080 };
			input.motionContexts = std::span(&context, 1); input.admissionDecisions = decisions;
			CanvasPanFrameOutput released; size_t resets = 0;
			if (queued)
			{
				input.contacts = contacts; input.frameQpc = contacts.back().qpc + 1;
				released = dynamics.Step(input);
			}
			else
				for (const auto& contact : contacts)
				{
					input.contacts = std::span(&contact, 1); input.frameQpc = contact.qpc;
					released = dynamics.Step(input); resets += released.timingResetCount;
				}
			check(released.phase == CanvasPanPhase::Coasting && released.releaseSpeed > 0 && released.timingResetCount == 0 && resets == 0,
				"second real screen pen tail burst retains its supported history in live and queued consumption");
			input.contacts = {}; input.frameQpc += kFreq / 100;
			const auto coast = dynamics.Step(input);
			check(std::hypot(coast.contentDelta.x, coast.contentDelta.y) > 0,
				"second real screen pen release advances coast after Up");
		}
		for (const auto profile : { CanvasPanMotionProfile::IndirectDip, CanvasPanMotionProfile::ScreenPenPhysical,
			CanvasPanMotionProfile::ScreenPenFallback, CanvasPanMotionProfile::TouchPhysical, CanvasPanMotionProfile::TouchFallback })
		{
			const auto context = profile == CanvasPanMotionProfile::ScreenPenPhysical || profile == CanvasPanMotionProfile::TouchPhysical ?
				PhysicalContext(profile, .19444445, .19791666) : MakeCanvasPanMotionContext(profile);
			const auto kind = profile == CanvasPanMotionProfile::TouchPhysical || profile == CanvasPanMotionProfile::TouchFallback ?
				CanvasPanSourceClass::Touch : CanvasPanSourceClass::Pen;
			for (const int hz : { 30, 60, 120, 240, 480, 960 })
				for (const double ratio : { .5, 1.0, 1801.0 / 1800, 2500.0 / 1800 })
				{
					AdaptedPan pan; pan.contexts[0] = context;
					const double speed = context.velocityLimit * ratio;
					double heldDelta = 0; size_t resets = 0;
					const int reports = hz / 10;
					for (int report = 0; report <= reports; ++report)
					{
						const double ms = 1000 + report * 1000.0 / hz;
						const auto sample = AdaptedContact(1, kind, 0, ms, speed * report / hz / context.motionPerDipX,
							0, report ? Phase::Move : Phase::Down);
						const auto out = pan.Step(ms, std::span(&sample, 1));
						heldDelta += out.contentDelta.x; resets += out.timingResetCount;
					}
					const double ms = 1000 + reports * 1000.0 / hz;
					const auto out = pan.Step(ms + 1, { AdaptedContact(1, kind, 0, ms + 1,
						speed * reports / hz / context.motionPerDipX, 0, Phase::Up) });
					check(out.phase == CanvasPanPhase::Coasting && resets == 0 && out.timingResetCount == 0,
						"reliable over-cap motion preserves history at every reporting rate and motion unit");
					if (hz == 30 && ratio == .5) std::cout << "[TouchCapFlags] profile=" << static_cast<unsigned>(profile)
						<< " mode=" << TouchReleaseModeName(out.touchDecision.mode) << " velocity=" << out.releaseMotionVelocity.x
						<< " support-limited=" << out.releaseSupportLimited << " boost-suppressed=" << out.releaseBoostSuppressed << '\n';
					check(!out.releaseSupportLimited && !out.releaseBoostSuppressed,
						"normal over-cap constant motion retains continuation eligibility instead of being labelled bad input");
					check.Near(out.releaseMotionVelocity.x, (std::min)(speed, context.velocityLimit), .15,
						"output cap bounds the release vector without declaring the input clock invalid");
					check.Near(heldDelta, (speed * reports / hz - context.touchSlop) / context.motionPerDipX, .02,
						"held geometry stays one-to-one even when actual motion exceeds the inertia cap");
				}
			// 小步量化：双段支持必须容纳既有空间噪声预算，不能把正常低速误判成静止。
			for (const int hz : { 120, 240, 480, 960 })
				for (const double multiplier : { 1.5, 2.0 })
				{
					AdaptedPan pan; pan.contexts[0] = context;
					const double speed = context.minimumFlingVelocity * multiplier;
					const double quantum = context.unit == CanvasPanMotionUnit::Millimeters ? .1 : .5;
					const double origin = context.unit == CanvasPanMotionUnit::Millimeters ? 200 : 1000;
					double position = origin;
					for (int report = 0; report <= hz / 2; ++report)
					{
						const double ms = 1000 + report * 1000.0 / hz;
						position = origin + std::round(speed * report / hz / quantum) * quantum;
						pan.Step(ms, { AdaptedContact(1, kind, 0, ms, position / context.motionPerDipX, 0, report ? Phase::Move : Phase::Down) });
					}
					const auto out = pan.Step(1501, { AdaptedContact(1, kind, 0, 1501, position / context.motionPerDipX, 0, Phase::Up) });
					check(out.phase == CanvasPanPhase::Coasting && out.releaseSpeed > 0 && !out.releaseOutputLimited,
						"normal low speed with quantized coordinates still has supported nonzero inertia across high reporting rates");
				}
			const double scale = context.unit == CanvasPanMotionUnit::Millimeters ? 1 : 5;
			const auto release = [&](std::initializer_list<std::pair<double, double>> points, double upMs)
			{
				AdaptedPan pan; pan.contexts[0] = context;
				std::vector<CanvasPanContactSample> contacts;
				for (const auto& [ms, x] : points)
					contacts.push_back(AdaptedContact(1, kind, 0, ms, x * scale / context.motionPerDipX, 0,
						contacts.empty() ? Phase::Down : Phase::Move));
				contacts.push_back(AdaptedContact(1, kind, 0, upMs, contacts.back().x, 0, Phase::Up));
				return pan.Step(upMs + .1, contacts);
			};
			for (const double jump : { 42.0, 102.0 })
			{
				const auto out = release({ { 0, 0 }, { 10, 3 }, { 20, 6 }, { 30, 9 }, { 40, 12 }, { 40.1, jump } }, 41);
				const double supportBound = 300 * scale + context.reversalDistance / .01 + .2;
				check(out.phase == CanvasPanPhase::Coasting && out.heldContacts == 0 && out.releaseSupportLimited &&
					out.releaseBoostSuppressed && std::isfinite(out.releaseSpeed) && out.releaseMotionVelocity.x > 0 &&
					out.releaseMotionVelocity.x <= supportBound && out.boostedReleaseVelocity.x * context.motionPerDipX <= supportBound,
					"one unsupported final point cannot exceed the supported prefix plus its existing spatial noise budget");
				check.Within(out.releaseMotionVelocity.x, 299.9 * scale, supportBound,
					"supported slow prefix remains nonzero instead of passing spike protection by cancelling all motion");
			}
			if (profile == CanvasPanMotionProfile::ScreenPenPhysical)
			{
				const auto mixed = release({ { 0, 0 }, { 10, 60 }, { 20, 120 }, { 30, 180 }, { 40, 200 }, { 50, 230 } }, 51);
				check(mixed.phase == CanvasPanPhase::Coasting && !mixed.releaseSupportLimited &&
					!mixed.releaseBoostSuppressed && mixed.releaseOutputLimited && mixed.releaseSupport.speedUpperBound > context.velocityLimit,
					"support above output cap preserves boost eligibility even when below the unconstrained raw fit");
				check.Near(mixed.releaseMotionVelocity.x, context.velocityLimit, .01,
					"mixed but supported overspeed tail is bounded by the output cap only");
			}
			const auto stationary = release({ { 0, 0 }, { 10, 0 }, { 20, 0 }, { 30, 0 }, { 40, 0 }, { 40.1, 100 } }, 41);
			const double stationaryBound = context.reversalDistance / .01 + .2;
			check(stationary.heldContacts == 0 && stationary.phase == CanvasPanPhase::Idle && stationary.releaseSpeed == 0 &&
				((kind == CanvasPanSourceClass::Touch && stationary.releaseMotionSampleCount == 2) ||
					(stationary.releaseSupport.positiveIntervalCount == 1 && stationary.rawReleaseVelocity.x > 0)),
				"a stationary tail with only one real movement interval retains raw diagnosis but cannot manufacture a fling");
			for (const double holdMs : { 2.1, 10.0 })
			{
				const auto plateau = release({ { 0, 0 }, { 10, 0 }, { 20, 0 }, { 30, 0 }, { 40, 100 }, { 40 + holdMs, 100 } }, 41 + holdMs);
				check(plateau.heldContacts == 0 && plateau.phase == CanvasPanPhase::Idle && plateau.releaseSpeed == 0 &&
					((kind == CanvasPanSourceClass::Touch && plateau.releaseMotionSampleCount == 2) ||
						(plateau.releaseSupport.positiveIntervalCount == 1 && plateau.rawReleaseVelocity.x > 0)),
					"repeating a jumped endpoint does not create a second independent positive movement interval");
			}

			const auto stoppedAfterJump = release({ { 0, 0 }, { 10, 3 }, { 20, 6 }, { 30, 9 }, { 40, 102 }, { 50, 102 } }, 51);
			check(stoppedAfterJump.heldContacts == 0 && stoppedAfterJump.releaseSupportLimited &&
				std::isfinite(stoppedAfterJump.releaseSpeed) && stoppedAfterJump.releaseMotionVelocity.x >= 0 &&
				stoppedAfterJump.releaseMotionVelocity.x <= stationaryBound + (kind == CanvasPanSourceClass::Touch ? 300 * scale : 0),
				"jump followed by stationary endpoint cannot revive old fast speed by skipping the latest zero interval");
			const auto accelerating = release({ { 0, 0 }, { 10, 5 }, { 20, 14 }, { 30, 27 }, { 40, 44 }, { 50, 65 }, { 60, 90 } }, 61);
			check(accelerating.phase == CanvasPanPhase::Coasting && accelerating.releaseMotionVelocity.x >= 1000 * scale,
				"multiple independent increasing intervals support real acceleration without a fixed acceleration ceiling");
		}
	}

	void TestQualityLimitedContinuation(Checks& check)
	{
		for (const auto profile : { CanvasPanMotionProfile::ScreenPenPhysical, CanvasPanMotionProfile::TouchPhysical })
			for (const bool spike : { false, true })
			{
				AdaptedPan pan; pan.contexts[0] = PhysicalContext(profile);
				const auto& context = pan.contexts[0];
				const auto kind = profile == CanvasPanMotionProfile::ScreenPenPhysical ? CanvasPanSourceClass::Pen : CanvasPanSourceClass::Touch;
				const auto first = AdaptedFling(pan, context.velocityLimit * .75);
				const auto coast = pan.Step(50);
				check(first.phase == CanvasPanPhase::Coasting && coast.phase == CanvasPanPhase::Coasting &&
					coast.contentVelocity.x * context.motionPerDipX > context.boostMinimumFlingVelocity,
					"quality continuation fixture starts with a real positive residual above the boost eligibility floor");
				constexpr double originDip = 1000;
				pan.Step(51, { AdaptedContact(2, kind, 0, 51, originDip, 0, CanvasPanContactPhase::Down) });
				CanvasPanFrameOutput beforeUp;
				for (int n = 1; n <= 4; ++n)
				{
					const double motion = spike ? 3.0 * n : context.velocityLimit * 1.5 * .01 * n;
					beforeUp = pan.Step(51 + n * 10, { AdaptedContact(2, kind, 0, 51 + n * 10,
						originDip + motion / context.motionPerDipX) });
				}
				if (spike)
					beforeUp = pan.Step(91.1, { AdaptedContact(2, kind, 0, 91.1,
						originDip + 42 / context.motionPerDipX) });
				// cap-only的Up延后40ms，age门把新手速降到cap之下，旧残余是否加入因此可观测。
				// 每次真实ScrollUpdate仍续期50ms资格窗；不是只检查饱和后的相同cap输出。
				const double upMs = spike ? 92 : 131;
				check(beforeUp.sourceKey == coast.sourceKey && beforeUp.boosterCurrentVelocity.x > 0 &&
					beforeUp.boosterCutoffSeconds > static_cast<double>(QpcMs(upMs)) / kFreq,
					"same-source positive continuation retains a real old residual inside its rolling qualification window");
				const double lastMotion = spike ? 42 : context.velocityLimit * 1.5 * .04;
				const auto released = pan.Step(upMs, { AdaptedContact(2, kind, 0, upMs,
					originDip + lastMotion / context.motionPerDipX, 0, CanvasPanContactPhase::Up) });
				const double selected = released.releaseMotionVelocity.x;
				const double boosted = released.boostedReleaseVelocity.x * context.motionPerDipX;
				check(released.phase == CanvasPanPhase::Coasting && released.heldContacts == 0 && selected > 0,
					"quality continuation closes its second real contact and launches a nonzero supported release");
				if (spike)
				{
					check(released.releaseSupportLimited && released.releaseBoostSuppressed &&
						coast.contentVelocity.x * context.motionPerDipX > selected,
						"a terminal spike actually limits a slow tail despite a larger eligible old residual");
					check.Near(boosted, selected, .02,
						"quality-limited release cannot re-add the old residual and bypass its independent support bound");
				}
				else if (kind == CanvasPanSourceClass::Touch)
				{
					check(released.releaseOutputLimited && !released.releaseSupportLimited && released.releaseRawAgeSeconds > .03 && !released.releaseMotionAgeLimited &&
						released.releaseBoostSuppressed && selected < context.velocityLimit,
						"Touch motion-age decay independently suppresses old residual even for otherwise cap-only release");
					check.Near(boosted, selected, .02, "aged Touch release cannot bypass explicit age decay through continuation");
				}
				else
				{
					check(released.releaseOutputLimited && !released.releaseSupportLimited && !released.releaseBoostSuppressed &&
						selected < context.velocityLimit && boosted > selected + 100,
						"output-cap-only continuation still inherits old residual after the age gate makes the addition observable");
					check.Near(boosted, (std::min)(context.velocityLimit,
						selected + coast.contentVelocity.x * context.motionPerDipX), .05,
						"cap-only continuation adds the actual current residual and applies the final vector cap");
				}
			}
	}

	void TestTouchRecordedTerminalPlatforms(Checks& check)
	{
		// 完整74/108与正常长尾107/109：固定快照中的真实QPC/像素点，无缺失事件补造。
		// 这是所选Touch进展策略的数值回归，不把无动作标注的平台一概认作应长滑。
		// 日志位置仅0.001px精度；按两端/两轴和>=2ms传播取0.2mm/s速度、0.1mm尾距容差。
		struct Recorded { int64_t qpc; double x, y; CanvasPanContactPhase phase; };
		using Phase = CanvasPanContactPhase;
		static constexpr Recorded trace74[] = {
			{ 111452911994LL, 1487.093, 738.746, Phase::Down },
			{ 111452950627LL, 1453.002, 778.507, Phase::Move },
			{ 111453030122LL, 1429.493, 810.482, Phase::Move },
			{ 111453146231LL, 1405.077, 840.869, Phase::Move },
			{ 111453195446LL, 1385.650, 868.384, Phase::Move },
			{ 111453280209LL, 1367.055, 891.666, Phase::Move },
			{ 111453361213LL, 1350.274, 912.907, Phase::Move },
			{ 111453445945LL, 1333.720, 932.258, Phase::Move },
			{ 111453530162LL, 1312.781, 954.860, Phase::Move },
			{ 111453643846LL, 1281.865, 984.718, Phase::Move },
			{ 111453694068LL, 1234.318, 1026.066, Phase::Move },
			{ 111453780029LL, 1166.891, 1088.958, Phase::Move },
			{ 111453860982LL, 1166.891, 1088.958, Phase::Move },
			{ 111453941409LL, 1166.891, 1088.958, Phase::Move },
			{ 111454022867LL, 1166.891, 1088.958, Phase::Move },
			{ 111454128869LL, 1166.891, 1088.958, Phase::Move },
			{ 111454194013LL, 1166.891, 1088.958, Phase::Move },
			{ 111454278431LL, 1166.891, 1088.958, Phase::Up },
		};
		static constexpr Recorded trace108[] = {
			{ 111541222763LL, 1284.586, 652.498, Phase::Down },
			{ 111541293566LL, 1276.724, 717.959, Phase::Move },
			{ 111541357725LL, 1270.677, 785.991, Phase::Move },
			{ 111541442442LL, 1263.647, 858.709, Phase::Move },
			{ 111541526925LL, 1255.332, 937.776, Phase::Move },
			{ 111541638732LL, 1244.296, 1024.479, Phase::Move },
			{ 111541690333LL, 1232.202, 1118.816, Phase::Move },
			{ 111541773659LL, 1221.997, 1221.014, Phase::Move },
			{ 111541859039LL, 1213.833, 1326.765, Phase::Move },
			{ 111541940892LL, 1209.298, 1422.841, Phase::Move },
			{ 111542022383LL, 1208.693, 1504.176, Phase::Move },
			{ 111542126418LL, 1210.734, 1566.841, Phase::Move },
			{ 111542189539LL, 1213.606, 1616.428, Phase::Move },
			{ 111542284382LL, 1218.520, 1650.898, Phase::Move },
			{ 111542351183LL, 1218.520, 1650.898, Phase::Move },
			{ 111542453765LL, 1218.520, 1650.898, Phase::Move },
			{ 111542517736LL, 1218.520, 1650.898, Phase::Move },
			{ 111542623620LL, 1218.520, 1650.898, Phase::Move },
			{ 111542687859LL, 1234.847, 1920.832, Phase::Move },
			{ 111542770718LL, 1234.847, 1920.832, Phase::Up },
		};
		static constexpr Recorded trace107[] = {
			{ 111538552539LL, 1219.351, 1272.945, Phase::Down },
			{ 111538638920LL, 1219.351, 1272.945, Phase::Move },
			{ 111538689593LL, 1219.351, 1272.945, Phase::Move },
			{ 111538775474LL, 1210.205, 1272.642, Phase::Move },
			{ 111538855765LL, 1201.965, 1269.241, Phase::Move },
			{ 111538940885LL, 1192.063, 1261.909, Phase::Move },
			{ 111539021419LL, 1181.027, 1249.285, Phase::Move },
			{ 111539142166LL, 1169.839, 1225.928, Phase::Move },
			{ 111539189743LL, 1163.112, 1181.858, Phase::Move },
			{ 111539275693LL, 1164.094, 1115.641, Phase::Move },
			{ 111539358733LL, 1175.357, 1018.280, Phase::Move },
			{ 111539439853LL, 1195.540, 905.348, Phase::Move },
			{ 111539523435LL, 1220.787, 796.346, Phase::Move },
			{ 111539639659LL, 1248.832, 699.288, Phase::Move },
			{ 111539688392LL, 1273.625, 614.854, Phase::Move },
			{ 111539772514LL, 1273.625, 614.854, Phase::Up },
		};
		static constexpr Recorded trace109[] = {
			{ 111543294974LL, 1205.065, 1208.088, Phase::Down },
			{ 111543362633LL, 1203.326, 1144.139, Phase::Move },
			{ 111543442118LL, 1200.680, 1078.299, Phase::Move },
			{ 111543521859LL, 1200.756, 1009.739, Phase::Move },
			{ 111543639396LL, 1202.872, 940.724, Phase::Move },
			{ 111543689000LL, 1206.728, 871.635, Phase::Move },
			{ 111543776157LL, 1214.362, 799.143, Phase::Move },
			{ 111543857961LL, 1224.642, 721.512, Phase::Move },
			{ 111543942392LL, 1237.871, 641.613, Phase::Move },
			{ 111544022545LL, 1258.885, 557.254, Phase::Move },
			{ 111544128233LL, 1285.644, 469.039, Phase::Move },
			{ 111544189100LL, 1316.409, 382.035, Phase::Move },
			{ 111544270926LL, 1316.409, 382.035, Phase::Up },
		};
		struct Case { std::span<const Recorded> trace; double raw, selected, distance, ageMs; };
		const Case cases[] = {
			{ trace74, 472.851227338, 472.851227338, 102.545209605, 49.840199999 },
			{ trace108, 621.633852623, 388.583938067, 80.363658837, 8.285899999 },
			{ trace107, 1129.772601033, 875.764060807, 213.282879650, 8.412199999 },
			{ trace109, 978.446406777, 883.027947209, 215.322859746, 8.182600001 },
		};
		for (const auto& test : cases)
			for (const bool queued : { false, true })
			{
				CanvasPanDynamics dynamics;
				const auto context = PhysicalContext(CanvasPanMotionProfile::TouchPhysical, .19444445, .19791666);
				std::vector<CanvasPanContactSample> contacts;
				for (const auto& point : test.trace)
					contacts.push_back({ 1, point.qpc, point.x * .5, point.y * .5, point.phase,
						(static_cast<uint64_t>(CanvasPanSourceClass::Touch) << 32) | 2122326080u, true, 0 });
				std::array<CanvasPanAdmissionDecision, 1> decisions;
				CanvasPanFrameInput input;
				input.qpcFrequency = kFreq; input.viewportSizeDip = { 1920, 1080 };
				input.motionContexts = std::span(&context, 1); input.admissionDecisions = decisions;
				CanvasPanFrameOutput out;
				if (queued) { input.contacts = contacts; input.frameQpc = contacts.back().qpc + kFreq / 10; out = dynamics.Step(input); }
				else for (const auto& contact : contacts)
				{ input.contacts = std::span(&contact, 1); input.frameQpc = contact.qpc; out = dynamics.Step(input); }
				const double raw = std::hypot(out.rawReleaseVelocity.x * context.motionPerDipX, out.rawReleaseVelocity.y * context.motionPerDipY);
				const double selected = std::hypot(out.releaseMotionVelocity.x, out.releaseMotionVelocity.y);
				check(out.released && out.phase == CanvasPanPhase::Coasting && out.heldContacts == 0,
					"recorded touch plateau and ordinary long-tail cases release in live and delayed same-frame consumption");
				check.Near(raw, test.raw, .2, "recorded touch raw motion fit uses only actual progress points and original time");
				check.Near(selected, test.selected, .2, "recorded touch selected speed matches independent progress-evidence calculation");
				check.Near(out.releaseAgeSeconds * 1000, test.ageMs, .001, "queued render time cannot replace actual Up-relative motion age");
				const auto parameters = out.frictionParameters;
				const double duration = parameters.tauSeconds * std::log1p(selected / (parameters.deceleration * parameters.tauSeconds));
				const double distance = parameters.tauSeconds * selected - parameters.deceleration * parameters.tauSeconds * duration;
				check.Near(distance, test.distance, .1, "recorded touch tail distance has a calibrated numeric expectation beyond Ready or nonzero");
				check(!out.releaseMotionAgeLimited && out.releaseRawAgeSeconds < .03 &&
					out.touchDecision.mode == TouchReleaseMode::Measured,
					"fresh raw plateau remains a measured handoff; motion age is observed separately from confirmed stop");
			}
	}

	void TestTouchProgressEvidence(Checks& check)
	{
		// Touch平台不刷新实际运动时钟；此处人为标注运动在32ms结束，年龄门仍用既有30/70ms。
		for (const auto profile : { CanvasPanMotionProfile::TouchPhysical, CanvasPanMotionProfile::TouchFallback })
		{
			const auto context = profile == CanvasPanMotionProfile::TouchPhysical ? PhysicalContext(profile) : MakeCanvasPanMotionContext(profile);
			const double scale = context.unit == CanvasPanMotionUnit::Millimeters ? 1 : 5;
			for (const double pause : { 25.0, 30.0, 50.0, 70.0, 100.0 })
			{
				AdaptedPan pan; pan.contexts[0] = context;
				pan.Step(0, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
				for (int report = 1; report <= 4; ++report)
					pan.Step(report * 8, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, report * 8,
						report * 8 * scale / context.motionPerDipX) });
				for (double ms = 40; ms < 32 + pause; ms += 8)
					pan.Step(ms, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, ms, 32 * scale / context.motionPerDipX) });
				const auto out = pan.Step(32 + pause, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 32 + pause,
					32 * scale / context.motionPerDipX, 0, CanvasPanContactPhase::Up) });
				const double gate = pause < 70 ? 1.0 : 0.0;
				check(out.heldContacts == 0, "touch platform release always closes the real held contact");
				check.Near(out.releaseAgeSeconds, pause / 1000, 1e-8,
					"repeated touch coordinates do not refresh the age of actual motion");
				check.Near(out.releaseMotionVelocity.x, 1000 * scale * gate, .1,
					"fresh touch plateau preserves supported speed until confirmed stop instead of mechanically halving handoff");
				if (pause < 70)
				{
					check.Near(out.rawReleaseVelocity.x * context.motionPerDipX, 1000 * scale, .1,
						"touch short platform preserves the recent moving tail without rewriting its sample times");
					const double speed = out.releaseMotionVelocity.x;
					const auto parameters = ResolveCanvasPanFriction(context, speed);
					const double duration = parameters.tauSeconds * std::log1p(speed / (parameters.deceleration * parameters.tauSeconds));
					const double distance = parameters.tauSeconds * speed - parameters.deceleration * parameters.tauSeconds * duration;
					check(distance > 50 * scale && out.phase == CanvasPanPhase::Coasting,
						"labelled short touch platform retains substantial calibrated tail distance, beyond mere nonzero release");
				}
				else check(out.phase == CanvasPanPhase::Idle && out.releaseReason == CanvasPanReleaseReason::Stale,
					"real long touch stop remains stale even while identical coordinates continue arriving");
			}
		}
	}

	void TestTouchProgressBoundaries(Checks& check)
	{
		for (const auto profile : { CanvasPanMotionProfile::TouchPhysical, CanvasPanMotionProfile::TouchFallback })
		{
			const auto context = profile == CanvasPanMotionProfile::TouchPhysical ? PhysicalContext(profile) : MakeCanvasPanMotionContext(profile);
			const double scale = context.unit == CanvasPanMotionUnit::Millimeters ? 1 : 5;
			const auto point = [&](uint64_t key, double ms, double x, CanvasPanContactPhase phase = CanvasPanContactPhase::Move)
			{ return AdaptedContact(key, CanvasPanSourceClass::Touch, 0, ms, x * scale / context.motionPerDipX, 0, phase); };
			const auto seed = [&]()
			{
				AdaptedPan pan; pan.contexts[0] = context;
				for (int n = 0; n <= 4; ++n)
					pan.Step(n * 8, { point(1, n * 8, n * 8, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down) });
				return pan;
			};
			{
				auto pan = seed();
				const auto lowRateFrame = pan.Step(40);
				check.Near(lowRateFrame.contentVelocity.x * context.motionPerDipX, 1000 * scale, .1,
					"absence of a new raw sample does not falsely stop normal low-rate touch rendering");
				const auto repeated = pan.Step(48, { point(1, 48, 32) });
				check(repeated.contentVelocity.x == 0 && repeated.contentVelocity.y == 0 && repeated.contentDelta.x == 0,
					"explicit repeated touch observation presents zero speed while preserving release evidence");
				check(pan.Step(56).contentVelocity.x == 0,
					"empty frames after an explicit touch repeat cannot revive the old speed used by fallback blur");
				const auto released = pan.Step(57, { point(1, 57, 32, CanvasPanContactPhase::Up) });
				check.Near(released.releaseMotionVelocity.x, 1000 * scale, .1,
					"presentation stationary observation does not overwrite the independent touch release candidate");
				check(released.releaseLastRawQpc == QpcMs(48) && released.releaseLastMotionQpc == QpcMs(32),
					"diagnostics distinguish raw observation time from actual touch motion time");
			}
			{
				auto pan = seed(); pan.Step(48, { point(1, 48, 32) });
				check(pan.Step(190).contentVelocity.x == 0, "late empty render frames observe stationary Touch without mutating queued event evidence");
				const auto out = pan.Step(200, { point(1, 57, 32, CanvasPanContactPhase::Up) });
				check.Near(out.releaseAgeSeconds, .025, 1e-8, "queued true Up uses event age even after a much later empty render frame");
				check.Near(out.releaseMotionVelocity.x, 1000 * scale, .1, "late presentation cannot erase a valid queued Touch release candidate");
				check(out.phase == CanvasPanPhase::Coasting && out.contentDelta.x == 0,
					"first consumption of queued Touch Up starts coast now without catching up historical free motion");
			}
			{
				CanvasPanReleaseVelocityTracker tracker; tracker.Configure(context);
				for (int n = 0; n <= 4; ++n) tracker.AddSample(1, { QpcMs(n * 8), n * 8 * scale, n * 16 * scale }, kFreq);
				tracker.AddSample(1, { QpcMs(48), 32 * scale, 64 * scale }, kFreq);
				tracker.ClearAxis(true, false);
				const auto remaining = tracker.Estimate(1, kFreq);
				check(remaining.valid && remaining.x == 0 && remaining.y > 0,
					"clearing an axis applies to both raw and retained Touch progress samples");
				tracker.ClearAxis(false, true);
				const auto stopped = tracker.Estimate(1, kFreq);
				check(stopped.x == 0 && stopped.y == 0 && tracker.LastSampleQpc(1) == QpcMs(48) && tracker.LastMotionQpc(1) == QpcMs(32),
					"axis clearing cannot restore old Touch velocity or rewrite its raw and motion clocks");
			}
			for (const double pause : { 40.3477, 50.0, 60.0, 70.0 })
			{
				auto pan = seed();
				for (double ms = 40; ms < 32 + pause; ms += 8) pan.Step(ms, { point(1, ms, 32) });
				pan.Step(32 + pause, { point(1, 32 + pause, 57) });
				const auto out = pan.Step(33 + pause, { point(1, 33 + pause, 57, CanvasPanContactPhase::Up) });
				if (pause <= 50)
					check(out.phase == CanvasPanPhase::Coasting && out.releaseMotionVelocity.x > 400 * scale &&
						out.releaseMotionSampleCount >= 3 && out.releaseAgeSeconds < .002,
						"touch catch-up crosses a real 40ms motion plateau without confusing it with a missing-raw callback gap");
				else
				{
					std::cout << "[TouchCatchup] pause=" << pause << " mode=" << TouchReleaseModeName(out.touchDecision.mode)
						<< " velocity=" << out.releaseMotionVelocity.x << " motion-points=" << out.releaseMotionSampleCount << '\n';
					if (pause < 70)
					{
						check(out.phase == CanvasPanPhase::Coasting && out.touchDecision.mode == TouchReleaseMode::Measured,
							"still-valid same-group candidate survives sparse catch-up with real new motion support");
						// 仅25运动单位/60ms的新进展不能继续宣称旧1000速度；误差只容纳tick/float传播。
						const double expectedSpeed = (25 * scale + context.reversalDistance) / (pause / 1000);
						check.Near(out.releaseMotionVelocity.x, expectedSpeed, .1 * scale,
							"sparse touch catch-up is limited to its actual long interval plus the existing spatial budget");
					}
					else check(out.phase == CanvasPanPhase::Idle && out.releaseSpeed == 0 && out.releaseMotionSampleCount <= 2,
						"confirmed seventy-ms stop retires group candidate and new single catch-up cannot regain distance intent");
				}
			}
			{
				auto pan = seed(); pan.Step(72, { point(1, 72, 57) });
				const auto out = pan.Step(73, { point(1, 73, 57, CanvasPanContactPhase::Up) });
				check(out.phase == CanvasPanPhase::Idle && out.releaseMotionSampleCount == 1,
					"an actual forty-ms raw callback gap still retires old motion evidence");
			}
			{
				auto pan = seed(); pan.Step(40, { point(1, 40, 32) }); pan.Step(48, { point(1, 48, 32) });
				const auto reverse = pan.Step(49, { point(1, 49, 20) });
				const auto out = pan.Step(50, { point(1, 50, 20, CanvasPanContactPhase::Up) });
				check(reverse.reversalCount == 1 && out.phase == CanvasPanPhase::Idle && out.releaseSpeed == 0,
					"raw reversal during repeated touch coordinates clears the old moving tail before the new leg qualifies");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context;
				for (int n = 0; n <= 4; ++n) pan.Step(n * 8, { point(1, n * 8, -n * 8, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down) });
				pan.Step(40, { point(2, 40, 100, CanvasPanContactPhase::Down) });
				for (int n = 1; n <= 6; ++n)
					pan.Step(40 + n * 10, { point(1, 40 + n * 10, -32), point(2, 40 + n * 10, 100 + n * 10) });
				pan.Step(108, { point(1, 108, -32) });
				const auto before = pan.dynamics->VelocityTracker().MotionSampleCount(1);
				pan.Step(110, { point(2, 110, 160, CanvasPanContactPhase::Up) });
				check(before >= 3 && pan.dynamics->VelocityTracker().MotionSampleCount(1) == before,
					"non-final finger Up does not treat stale opposite motion as fresh just because its raw coordinates repeat");
				const auto out = pan.Step(111, { point(1, 111, -32, CanvasPanContactPhase::Up) });
				std::cout << "[TouchGroupLast] mode=" << TouchReleaseModeName(out.touchDecision.mode) << " velocity="
					<< out.releaseMotionVelocity.x << " source=" << out.touchDecision.evidenceContactKey << '\n';
				check(out.phase == CanvasPanPhase::Coasting && out.heldContacts == 0 &&
					out.touchDecision.mode == TouchReleaseMode::Measured && out.touchDecision.evidenceContactKey == 0,
					"last stale opposite finger closes through the group's recent supported positive focus handoff");
				// 两个held的真实focus：有效指1000、静止旧指0；不能取最快指或混入旧反向速度。
				check.Near(out.releaseMotionVelocity.x, 500 * scale, .1 * scale,
					"group focus speed includes the stale stationary finger only as zero contribution in the held denominator");
			}
		}
	}

	void TestTouchReleaseHandoffDecision(Checks& check)
	{
		for (const auto profile : { CanvasPanMotionProfile::TouchPhysical, CanvasPanMotionProfile::TouchFallback })
		{
			const auto context = profile == CanvasPanMotionProfile::TouchPhysical ? PhysicalContext(profile) : MakeCanvasPanMotionContext(profile);
			const double scale = context.unit == CanvasPanMotionUnit::Millimeters ? 1 : 5;
			const auto point = [&](uint64_t key, double ms, double x, CanvasPanContactPhase phase = CanvasPanContactPhase::Move)
			{ return AdaptedContact(key, CanvasPanSourceClass::Touch, 0, ms, x * scale / context.motionPerDipX, 0, phase); };
			{
				AdaptedPan pan; pan.contexts[0] = context;
				for (int n = 0; n <= 4; ++n) pan.Step(n * 8, { point(1, n * 8, n * 8, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down) });
				for (int ms = 40; ms <= 80; ms += 8) pan.Step(ms, { point(1, ms, 32) });
				const auto out = pan.Step(82, { point(1, 82, 32, CanvasPanContactPhase::Up) });
				check.Near(out.releaseMotionVelocity.x, 1000 * scale, .1,
					"fresh raw terminal plateau preserves supported handoff instead of halving speed by progress age");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context;
				pan.Step(0, { point(1, 0, 0, CanvasPanContactPhase::Down) });
				pan.Step(4, { point(1, 4, 4) }); pan.Step(8, { point(1, 8, 8) });
				const auto out = pan.Step(9, { point(1, 9, 8, CanvasPanContactPhase::Up) });
				check(out.phase == CanvasPanPhase::Coasting && out.heldContacts == 0,
					"two independent high-signal short movement intervals no longer require an arbitrary twenty-ms wait");
				check.Near(out.releaseMotionVelocity.x, 1000 * scale, .1, "short supported touch velocity remains physically bounded and directional");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context;
				for (int n = 0; n <= 4; ++n) pan.Step(n * 8, { point(1, n * 8, n * 8, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down) });
				pan.Step(33, { point(2, 33, 100, CanvasPanContactPhase::Down) });
				pan.Step(40, { point(1, 40, 32, CanvasPanContactPhase::Up) });
				const auto out = pan.Step(41, { point(2, 41, 100, CanvasPanContactPhase::Up) });
				check(out.phase == CanvasPanPhase::Coasting && out.heldContacts == 0 && out.releaseMotionVelocity.x > 900 * scale,
					"same-group mature moving finger survives a later short stationary last finger without inventing samples");
			}
			{
				CanvasPanReleaseVelocityTracker tracker; tracker.Configure(context);
				for (const double crossX : { 0.0, -.1 })
				{
					tracker.Clear();
					tracker.AddSample(1, { QpcMs(0), 0, 0 }, kFreq);
					tracker.AddSample(1, { QpcMs(4), 4 * scale, 0 }, kFreq);
					tracker.AddSample(1, { QpcMs(8), (4 + crossX) * scale, 4 * scale }, kFreq);
					check(!tracker.ShortTouchSupported(1, kFreq) && tracker.Reason(1, kFreq) == CanvasPanReleaseReason::InsufficientSpan,
						"orthogonal or sub-threshold obtuse short intervals cannot become reliable only through positive net-direction projections");
				}
				tracker.Clear();
				for (int n = 0; n <= 2; ++n) tracker.AddSample(1, { QpcMs(n * 4), n * 4 * scale, 0 }, kFreq);
				check(tracker.ShortTouchSupported(1, kFreq) && tracker.Reason(1, kFreq) == CanvasPanReleaseReason::Ready,
					"normal aligned high-signal short intervals remain supported without a twenty-ms gate");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context;
				for (int n = 0; n <= 15; ++n)
					pan.Step(n * 10, { point(1, n * 10, -.2 * n, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down),
						point(2, n * 10, 100 + 10 * n, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down) });
				// 少量反向指退出仍执行原清组保护；其Up只给新腿意向定时，不作为运动点。
				pan.Step(151, { point(1, 151, -3, CanvasPanContactPhase::Up) });
				check(pan.dynamics->VelocityTracker().MotionSampleCount(2) == 0,
					"opposite non-final finger exit still clears previous individual and group motion evidence");
				const auto moved = pan.Step(152, { point(2, 152, 252) });
				CanvasViewportState viewport;
				const auto applied = ApplyCanvasContentTranslationChecked(viewport, moved.contentDelta);
				check(pan.dynamics->ConfirmAppliedMotion(moved.motionFeedback, { -applied.viewportDelta.x, -applied.viewportDelta.y }),
					"real immediate movement after a group-leg reset can confirm only its new applied displacement");
				const auto out = pan.Step(153, { point(2, 153, 252, CanvasPanContactPhase::Up) });
				check(out.phase == CanvasPanPhase::Coasting && out.heldContacts == 0 && out.releaseMotionSampleCount == 1 &&
					out.touchDecision.mode == TouchReleaseMode::DistanceBounded && out.touchDecision.response == TouchResponseSource::Base &&
					out.releaseMotionVelocity.x > 0 && out.touchDecision.distanceBudget <= 2 * scale + .001,
					"a long-running group reset cannot invent a seventy-ms stop before its immediate new motion leg");
			}
		}
	}


	void TestTouchSlowScrollAndPresentation(Checks& check)
	{
		for (const auto profile : { CanvasPanMotionProfile::TouchPhysical, CanvasPanMotionProfile::TouchFallback })
		{
			const auto context = profile == CanvasPanMotionProfile::TouchPhysical ? PhysicalContext(profile) : MakeCanvasPanMotionContext(profile);
			const double scale = context.unit == CanvasPanMotionUnit::Millimeters ? 1 : 5;
			const auto point = [&](uint64_t key, double ms, double x, CanvasPanContactPhase phase = CanvasPanContactPhase::Move)
			{ return AdaptedContact(key, CanvasPanSourceClass::Touch, 0, ms, x * scale / context.motionPerDipX, 0, phase); };
			const auto confirm = [&](AdaptedPan& pan, const CanvasPanFrameOutput& out, CanvasViewportState& viewport)
			{
				const auto applied = ApplyCanvasContentTranslationChecked(viewport, out.contentDelta);
				return pan.dynamics->ConfirmAppliedMotion(out.motionFeedback, { -applied.viewportDelta.x, -applied.viewportDelta.y });
			};
			for (const double tail : { .4, 0.0, 2.0, .6 })
			{
				AdaptedPan pan; pan.contexts[0] = context; const auto prior = AdaptedFling(pan, 500 * scale);
				check.Near(prior.touchDecision.velocitySeed.x, 500 * scale, .01, "slow-scroll fixture starts a real same-source nonzero old coast");
				pan.Step(34, { point(2, 34, 0, CanvasPanContactPhase::Down) });
				CanvasViewportState viewport;
				const auto first = pan.Step(35, { point(2, 35, 10) });
				check(confirm(pan, first, viewport), "compressed first scroll has real applied budget before the braking comparison");
				const auto slow = pan.Step(55, { point(2, 55, 10 + tail) }); confirm(pan, slow, viewport);
				check(!pan.dynamics->VelocityTracker().Estimate(2, kFreq).valid,
					"compressed first movement plus one tail interval cannot silently acquire a measured-speed candidate");
				const auto out = pan.Step(56, { point(2, 56, 10 + tail, CanvasPanContactPhase::Up) });
				if (tail == .4)
					check(out.touchDecision.mode == TouchReleaseMode::Stop && out.boostedReleaseVelocity.x == 0 &&
						out.touchDecision.distanceBudget == 0 && slow.boosterStartingVelocity.x == 0,
						"actual slow scroll cannot restore shadow residual or bypass braking with previously applied large-move distance");
				else
				{
					check(out.touchDecision.mode == TouchReleaseMode::ResidualOnly && !out.touchDecision.addedResidual,
						"zero-scroll platform, fast real scroll and inclusive minimum-scroll boundary retain legal direction-only continuation");
					check.Near(out.boostedReleaseVelocity.x * context.motionPerDipX, 500 * scale, .01,
						"residual-only final seed remains exactly the current residual rather than receiving a second boost");
				}
			}
			{
				AdaptedPan pan; pan.contexts[0] = context; AdaptedFling(pan, 500 * scale);
				pan.Step(34, { point(2, 34, 0, CanvasPanContactPhase::Down) });
				const auto move = pan.Step(55, { point(2, 35, 10), point(2, 55, 10.4) });
				CanvasViewportState viewport;
				check(move.motionFeedback.present && !move.motionFeedback.eligible && !confirm(pan, move, viewport),
					"a frame containing both pre-braking and slow displacement cannot re-acknowledge the retired old distance budget");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context; AdaptedFling(pan, 500 * scale);
				pan.Step(34, { point(2, 34, 0, CanvasPanContactPhase::Down) });
				pan.Step(35, { point(2, 35, 10) });
				const auto tiny = pan.Step(35.9, { point(2, 35.9, 10.025) });
				const auto out = pan.Step(36.9, { point(2, 36.9, 10.025, CanvasPanContactPhase::Up) });
				check(tiny.contentDelta.x > 0 && out.touchDecision.mode == TouchReleaseMode::ResidualOnly,
					"a real sub-millisecond scroll interval keeps the existing minimum-interval protection against spurious braking");
				check.Near(out.boostedReleaseVelocity.x * context.motionPerDipX, 500 * scale, .01,
					"minimum-interval handling does not replace legal residual continuation with an invented new speed");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context; AdaptedFling(pan, 500 * scale);
				pan.Step(34, { point(2, 34, 0, CanvasPanContactPhase::Down) });
				pan.Step(35, { point(2, 35, 10) });
				for (int n = 1; n <= 3; ++n) pan.Step(35 + n * 20, { point(2, 35 + n * 20, 10 + n * .4) });
				const auto out = pan.Step(96, { point(2, 96, 11.2, CanvasPanContactPhase::Up) });
				check(out.touchDecision.mode == TouchReleaseMode::Measured && out.phase == CanvasPanPhase::Coasting && !out.touchDecision.addedResidual,
					"braking retires low-confidence continuations without forbidding reliable current slow motion");
				check.Near(out.boostedReleaseVelocity.x * context.motionPerDipX, 20 * scale, .01,
					"supported slow motion hands off its own speed without recovering the old five-hundred residual");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context;
				pan.Step(0, { point(1, 0, 0, CanvasPanContactPhase::Down) });
				pan.Step(4, { point(1, 4, .6) }); const auto inside = pan.Step(8, { point(1, 8, 1.2) });
				check(pan.dynamics->VelocityTracker().Estimate(1, kFreq).valid && inside.handVelocityKnown && !inside.scrolling &&
					inside.contentDelta.x == 0 && inside.contentVelocity.x == 0 && inside.handVelocity.x == 0 && pan.dynamics->ContentVelocity().x == 0,
					"strong short evidence inside slop remains available but does not publish fictitious content presentation speed");
				const auto outside = pan.Step(12, { point(1, 12, 4) });
				check(outside.scrolling && outside.contentDelta.x > 0 && outside.contentVelocity.x > 0 && outside.handVelocity.x > 0,
					"crossing slop restores supported presentation speed without discarding the earlier release evidence");
			}
			{
				AdaptedPan pan; pan.contexts[0] = context; const auto prior = AdaptedFling(pan, 500 * scale);
				pan.Step(34, { point(2, 34, 0, CanvasPanContactPhase::Down) });
				const auto move = pan.Step(35, { point(2, 35, 15) }); CanvasViewportState viewport;
				check(confirm(pan, move, viewport) && move.boosterStartingVelocity.x > 0 && prior.phase == CanvasPanPhase::Coasting,
					"distance-seed regression has actual applied budget and a live same-direction legacy residual within its continuation window");
				const auto out = pan.Step(70, { point(2, 70, 15, CanvasPanContactPhase::Up) });
				check(out.touchDecision.mode == TouchReleaseMode::DistanceBounded && out.touchDecision.response == TouchResponseSource::Base &&
					!out.touchDecision.addedResidual && out.releaseRawAgeSeconds > .03 && out.releaseRawAgeSeconds < .05 &&
					out.touchDecision.velocitySeed.x > context.boostMinimumFlingVelocity,
					"stale direction-only residual chooses a base-response distance seed even when that seed could otherwise qualify for legacy boost");
				check.Near(out.boostedReleaseVelocity.x * context.motionPerDipX, out.touchDecision.velocitySeed.x, .001,
					"distance-bounded final seed cannot receive the still-nonzero old residual a second time");
				double coast = 0;
				for (double ms = 74; ms <= 2070 && pan.dynamics->Active(); ms += 4) coast += pan.Step(ms).contentDelta.x * context.motionPerDipX;
				check(coast > 0 && coast <= out.touchDecision.distanceBudget + .001 && out.touchDecision.distanceBudget <= 12.5 * scale + .001,
					"executed distance-bounded coast stays within the previously applied new-leg budget with a same-direction old residual present");
			}
		}
	}

	void TestTouchRepairRecordedDecisions(Checks& check)
	{
		struct Event { uint64_t key; int64_t qpc, frame; double x, y; CanvasPanContactPhase phase; };
		using Phase = CanvasPanContactPhase;
		static constexpr Event trace4[] = {
			{ 4, 155245467434LL, 155245469783LL, 814.9040, 622.3750, Phase::Down },
			{ 4, 155245502581LL, 155245555654LL, 814.9040, 622.3750, Phase::Move },
			{ 4, 155245587639LL, 155245638978LL, 814.9040, 622.3750, Phase::Move },
			{ 4, 155245667401LL, 155245722995LL, 820.4220, 619.6155, Phase::Move },
			{ 4, 155245721084LL, 155245722995LL, 826.7715, 615.7230, Phase::Move },
			{ 4, 155245807448LL, 155245889859LL, 835.5780, 610.2805, Phase::Move },
			{ 4, 155245888015LL, 155245889859LL, 845.5935, 602.7215, Phase::Move },
			{ 4, 155246002524LL, 155246057356LL, 856.9700, 594.3685, Phase::Move },
			{ 4, 155246057492LL, 155246057356LL, 869.9340, 584.3530, Phase::Move },
			{ 4, 155246149908LL, 155246224257LL, 883.8805, 573.2410, Phase::Move },
			{ 4, 155246224859LL, 155246307928LL, 899.4140, 560.2015, Phase::Move },
			{ 4, 155246311568LL, 155246391345LL, 917.2160, 544.9325, Phase::Move },
			{ 4, 155246392502LL, 155246475066LL, 937.4740, 527.4710, Phase::Move },
			{ 4, 155246503304LL, 155246558363LL, 960.1135, 507.6660, Phase::Move },
			{ 4, 155246556343LL, 155246558363LL, 985.6630, 484.8380, Phase::Move },
			{ 4, 155246639920LL, 155246642443LL, 1015.5970, 459.7040, Phase::Move },
			{ 4, 155246722873LL, 155246723865LL, 1015.5970, 459.7040, Phase::Up },
		};
		static constexpr Event trace25[] = {
			{ 25, 155321261405LL, 155321264081LL, 716.7495, 668.1450, Phase::Down },
			{ 25, 155321313943LL, 155321350384LL, 716.7495, 668.1450, Phase::Move },
			{ 25, 155321380217LL, 155321433800LL, 716.7495, 668.1450, Phase::Move },
			{ 25, 155321499161LL, 155321517212LL, 715.8425, 659.4140, Phase::Move },
			{ 25, 155321547977LL, 155321600518LL, 715.7290, 645.9590, Phase::Move },
			{ 25, 155321634445LL, 155321684272LL, 716.2205, 625.4740, Phase::Move },
			{ 25, 155321728393LL, 155321767757LL, 717.6945, 598.6015, Phase::Move },
			{ 25, 155321797973LL, 155321851437LL, 719.8865, 565.1150, Phase::Move },
			{ 25, 155321884298LL, 155321935300LL, 724.1195, 525.6565, Phase::Move },
			{ 25, 155321998816LL, 155322018463LL, 730.9985, 480.9450, Phase::Move },
			{ 25, 155322046626LL, 155322101892LL, 738.9730, 434.4190, Phase::Move },
			{ 25, 155322133014LL, 155322185379LL, 748.2710, 389.9340, Phase::Move },
			{ 25, 155322213573LL, 155322268782LL, 758.6270, 349.4930, Phase::Move },
			{ 25, 155322297243LL, 155322352170LL, 769.5875, 312.9070, Phase::Move },
			{ 25, 155322379976LL, 155322435651LL, 781.0015, 279.7605, Phase::Move },
			{ 25, 155322497604LL, 155322518987LL, 794.2300, 248.7685, Phase::Move },
			{ 25, 155322544925LL, 155322545868LL, 794.2300, 248.7685, Phase::Up },
		};
		static constexpr Event trace26[] = {
			{ 26, 155324246935LL, 155324250553LL, 853.8710, 196.2710, Phase::Down },
			{ 26, 155324299961LL, 155324337015LL, 853.8710, 196.2710, Phase::Move },
			{ 26, 155324381580LL, 155324420195LL, 852.6615, 200.9195, Phase::Move },
			{ 26, 155324499185LL, 155324503730LL, 849.7135, 212.1070, Phase::Move },
			{ 26, 155324550391LL, 155324587174LL, 843.3640, 231.6470, Phase::Move },
			{ 26, 155324641901LL, 155324670507LL, 834.8220, 259.2000, Phase::Move },
			{ 26, 155324714216LL, 155324754040LL, 822.6900, 294.0850, Phase::Move },
			{ 26, 155324799490LL, 155324837430LL, 808.5920, 336.4535, Phase::Move },
			{ 26, 155324897843LL, 155324920893LL, 791.6600, 383.3195, Phase::Move },
			{ 26, 155325000157LL, 155325004273LL, 772.9890, 432.5290, Phase::Move },
			{ 26, 155325048433LL, 155325087785LL, 756.1325, 483.4770, Phase::Move },
			{ 26, 155325134705LL, 155325171143LL, 740.7495, 533.4805, Phase::Move },
			{ 26, 155325216013LL, 155325254548LL, 727.9370, 580.1575, Phase::Move },
			{ 26, 155325297763LL, 155325338449LL, 718.1480, 623.2440, Phase::Move },
			{ 26, 155325381738LL, 155325421609LL, 710.9670, 663.8360, Phase::Move },
			{ 26, 155325495823LL, 155325505007LL, 706.4315, 701.5560, Phase::Move },
			{ 26, 155325545829LL, 155325588408LL, 705.8645, 734.0600, Phase::Move },
			{ 26, 155325632474LL, 155325671837LL, 705.8645, 734.0600, Phase::Move },
			{ 26, 155325708893LL, 155325755294LL, 705.8645, 734.0600, Phase::Move },
			{ 26, 155325794357LL, 155325838655LL, 705.8645, 734.0600, Phase::Move },
			{ 26, 155325876701LL, 155325922041LL, 705.8645, 734.0600, Phase::Move },
			{ 26, 155325979911LL, 155326005798LL, 705.8645, 734.0600, Phase::Move },
			{ 26, 155326046003LL, 155326046500LL, 705.8645, 734.0600, Phase::Up },
		};
		static constexpr Event trace64[] = {
			{ 64, 155394885401LL, 155394891026LL, 608.1260, 286.2615, Phase::Down },
			{ 64, 155394888597LL, 155394891026LL, 550.3370, 299.3385, Phase::Move },
			{ 64, 155394995672LL, 155394997020LL, 550.3370, 299.3385, Phase::Up },
		};
		static constexpr Event trace47_48[] = {
			{ 47, 155374171151LL, 155374173565LL, 749.4045, 265.0585, Phase::Down },
			{ 48, 155374173423LL, 155374177248LL, 883.6915, 318.8030, Phase::Down },
			{ 47, 155374213989LL, 155374261106LL, 697.8140, 274.0535, Phase::Move },
			{ 48, 155374214637LL, 155374261106LL, 837.0520, 323.2250, Phase::Move },
			{ 47, 155374296396LL, 155374296949LL, 697.8140, 274.0535, Phase::Up },
			{ 48, 155374297512LL, 155374299316LL, 781.6065, 327.8740, Phase::Move },
			{ 48, 155374376697LL, 155374377065LL, 781.6065, 327.8740, Phase::Up },
		};
		const std::span<const Event> cases[] = { trace4, trace25, trace26, trace64, trace47_48 };
		for (size_t which = 0; which < std::size(cases); ++which)
		{
			CanvasPanDynamics dynamics; CanvasViewportState viewport;
			const auto context = PhysicalContext(CanvasPanMotionProfile::TouchPhysical, .19444445, .19791666);
			std::array<CanvasPanAdmissionDecision, 16> decisions;
			CanvasPanFrameInput input; input.qpcFrequency = kFreq; input.viewportSizeDip = { 1920, 1080 };
			input.motionContexts = std::span(&context, 1); input.admissionDecisions = decisions;
			CanvasPanFrameOutput out; CanvasVector confirmed;
			for (size_t begin = 0; begin < cases[which].size();)
			{
				size_t end = begin + 1; while (end < cases[which].size() && cases[which][end].frame == cases[which][begin].frame) ++end;
				std::array<CanvasPanContactSample, 32> samples;
				input.frameQpc = cases[which][begin].frame;
				for (size_t i = begin; i < end; ++i)
				{
					const auto& e = cases[which][i]; input.frameQpc = (std::max)(input.frameQpc, e.qpc);
					samples[i - begin] = { e.key, e.qpc, e.x, e.y, e.phase, (static_cast<uint64_t>(CanvasPanSourceClass::Touch) << 32) | 2122326080u, true, 0 };
				}
				input.contacts = std::span(samples).first(end - begin); out = dynamics.Step(input);
				const auto applied = ApplyCanvasContentTranslationChecked(viewport, out.contentDelta);
				const CanvasVector actual{ -applied.viewportDelta.x, -applied.viewportDelta.y };
				if (dynamics.ConfirmAppliedMotion(out.motionFeedback, actual))
				{ confirmed.x += actual.x; confirmed.y += actual.y; }
				begin = end;
			}
			check(out.heldContacts == 0 && out.phase == CanvasPanPhase::Coasting,
				"recorded complete short and mature Touch groups close with explicitly qualified release decisions");
			const double hand = std::hypot(out.releaseMotionVelocity.x, out.releaseMotionVelocity.y);
			if (which == 2) check.Within(hand, 670, 685, "key26 supported velocity hands off without halving for its fresh terminal plateau");
			if (which == 0) check.Within(hand, 630, 637, "normal key4 measured handoff remains the established supported speed");
			if (which == 1) check.Within(hand, 560, 572, "normal key25 selected speed stays independent of prior-fling fixture availability");
			if (which == 3)
			{
				check(out.touchDecision.mode == TouchReleaseMode::DistanceBounded && out.touchDecision.response == TouchResponseSource::Base &&
					!out.touchDecision.addedResidual, "compressed key64 uses actual-applied distance, never its 36m/s arrival quotient");
				const double speed = std::hypot(out.boostedReleaseVelocity.x * context.motionPerDipX, out.boostedReleaseVelocity.y * context.motionPerDipY);
				const auto f = out.frictionParameters;
				const double distance = f.tauSeconds * speed - f.deceleration * f.tauSeconds * f.tauSeconds * std::log1p(speed / (f.deceleration * f.tauSeconds));
				const double appliedBudget = std::hypot(confirmed.x * context.motionPerDipX, confirmed.y * context.motionPerDipY);
				check(distance > 8 && distance <= appliedBudget + .001 && distance <= out.touchDecision.distanceBudget + .001,
					"key64 extra coast is at most the confirmed roughly-nine-mm new-leg content displacement");
			}
			if (which == 4) check(out.touchDecision.mode == TouchReleaseMode::ShortSupported,
				"recorded 47/48 multi-touch group uses independent strong short motion rather than a mandatory twenty-ms gate");
		}
	}

	void TestTouchDecisionBudgetSafety(Checks& check)
	{
		TouchReleaseFacts facts; facts.context = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		facts.scrolled = true; facts.direction = { -1, .2f }; facts.confirmedNet = { -8.8f, 1.8f };
		facts.budgetConfirmed = true; facts.residualEligible = true; facts.residual = { 217.45f, -77.98f };
		auto d = ResolveTouchRelease(facts);
		check(d.mode == TouchReleaseMode::DistanceBounded && !d.addedResidual && d.velocitySeed.x < 0 && d.velocitySeed.y > 0,
			"opposite previous residual cannot be borrowed for a newly confirmed reverse-direction short drag");
		facts.budgetConfirmed = false; check(ResolveTouchRelease(facts).mode == TouchReleaseMode::Stop,
			"requested same-frame displacement without prior Apply confirmation cannot fund distance fallback");
		facts.direction = { 1, 0 }; facts.residual = { 400, 0 }; d = ResolveTouchRelease(facts);
		check(d.mode == TouchReleaseMode::ResidualOnly && d.velocitySeed.x == 400 && d.response == TouchResponseSource::Base && !d.addedResidual,
			"direction-only handoff continues only the actual current same-direction residual with no fictitious new hand speed");
		facts.motionAgeSeconds = .07; check(ResolveTouchRelease(facts).mode == TouchReleaseMode::Stop,
			"confirmed stop cancels direction-only residual and all distance fallback");
		AdaptedPan pan; pan.contexts[0] = facts.context;
		pan.Step(0, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
		auto move = pan.Step(1, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 1, 50) });
		CanvasViewportState viewport;
		const auto actual = ApplyCanvasContentTranslationChecked(viewport, move.contentDelta);
		check(pan.dynamics->ConfirmAppliedMotion(move.motionFeedback, { -actual.viewportDelta.x, -actual.viewportDelta.y }),
			"new touch short leg accepts one actual viewport application confirmation");
		check(!pan.dynamics->ConfirmAppliedMotion(move.motionFeedback, { -actual.viewportDelta.x, -actual.viewportDelta.y }),
			"duplicate Apply confirmation cannot inflate a geometric coast budget");
		pan.dynamics->Stop();
		pan.Step(2, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 2, 0, 0, CanvasPanContactPhase::Down) });
		check(!pan.dynamics->ConfirmAppliedMotion(move.motionFeedback, { 100, 0 }), "Stop/new accepted group rejects a late old-epoch budget acknowledgement");
	}

	void TestSameFrameTouchReleaseMetadata(Checks& check)
	{
		using Phase = CanvasPanContactPhase;
		const auto context = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		const std::vector<CanvasPanContactSample> firstContacts = {
			AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, Phase::Down),
			AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 10, 50),
			AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 20, 100),
			AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 30, 150),
			AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 31, 150, 0, Phase::Up),
		};
		AdaptedPan control; control.contexts[0] = context;
		const auto firstRelease = control.Step(40, firstContacts);
		check(firstRelease.touchDecision.mode == TouchReleaseMode::Measured && firstRelease.releaseTouchTag.group != 0 &&
			firstRelease.releaseRawAgeSeconds > 0 && firstRelease.releasePlatformSeconds > 0,
			"metadata fixture first produces a real measured Touch release with nondefault tag and age facts");
		{
			AdaptedPan pan; pan.contexts[0] = context;
			auto contacts = firstContacts;
			contacts.push_back(AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 32, 300, 0, Phase::Up));
			contacts.push_back(AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 33, 300, 0, Phase::Down));
			const auto out = pan.Step(40, contacts);
			check(out.released && out.phase == CanvasPanPhase::Idle && out.heldContacts == 0 &&
				out.releaseReason == CanvasPanReleaseReason::TimingRejected && out.releaseContactKey == 2,
				"early terminal after an earlier same-frame valid Touch release closes only the malformed final contact");
			check(out.touchDecision.mode == TouchReleaseMode::NotEvaluated && out.touchDecision.reason == TouchReleaseReason::None &&
				out.touchDecision.handVelocity.x == 0 && out.touchDecision.handVelocity.y == 0 &&
				out.touchDecision.velocitySeed.x == 0 && out.touchDecision.velocitySeed.y == 0 &&
				out.touchDecision.distanceBudget == 0 && out.releaseTouchTag == TouchMotionTag{} &&
				out.releaseRawAgeSeconds == 0 && out.releasePlatformSeconds == 0,
				"invalid early terminal cannot retain a previous Touch decision, tag, response origin or platform age");
			check(out.rawReleaseVelocity.x == 0 && out.rawReleaseVelocity.y == 0 &&
				out.selectedReleaseVelocity.x == 0 && out.selectedReleaseVelocity.y == 0 &&
				out.boostedReleaseVelocity.x == 0 && out.boostedReleaseVelocity.y == 0 &&
				out.releaseSpeed == 0 && out.releaseAgeSeconds == 0 && out.releaseSpanSeconds == 0 &&
				out.releaseMotionSampleCount == 0 && out.releaseLastRawQpc == 0 && out.releaseLastMotionQpc == 0 &&
				!out.releaseSupport.evaluated && out.releaseSupport.anchorCount == 0,
				"early terminal resets all release-specific measurement and quality facts consistently");
			check(out.motionFeedback.present && out.motionFeedback.tag.group != 0,
				"resetting release metadata preserves frame-scoped Touch scroll feedback");
			check.Near(out.contentDelta.x, firstRelease.contentDelta.x, .001,
				"early terminal metadata cleanup cannot erase the earlier valid same-frame held movement");
		}
		{
			AdaptedPan pan; pan.contexts[0] = context;
			pan.contextCount = 2; pan.contexts[1] = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical);
			auto contacts = firstContacts;
			contacts.push_back(AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 40, 1000, 0, Phase::Down));
			for (int n = 1; n <= 3; ++n) contacts.push_back(AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 40 + n * 10, 1000 + n * 50));
			contacts.push_back(AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 71, 1150, 0, Phase::Up));
			const auto out = pan.Step(80, contacts);
			check(out.released && out.phase == CanvasPanPhase::Coasting && out.releaseReason == CanvasPanReleaseReason::Ready &&
				out.releaseMotionContext.profile == CanvasPanMotionProfile::ScreenPenPhysical &&
				out.touchDecision.mode == TouchReleaseMode::NotEvaluated && out.releaseTouchTag == TouchMotionTag{} &&
				out.releaseRawAgeSeconds == 0 && out.releasePlatformSeconds == 0,
				"normal non-Touch terminal clears earlier Touch-only fields while retaining its own valid release");
			check(out.releaseMotionSampleCount >= 3 && out.releaseLastRawQpc == QpcMs(70) && out.releaseLastMotionQpc == QpcMs(70) &&
				out.motionFeedback.present && !out.motionFeedback.eligible,
				"Touch cleanup preserves Pen measurement clocks and the frame-level mixed-source rejection");
			check.Near(out.releaseMotionVelocity.x, 1000, .1, "metadata reset does not alter the following Pen release speed");
		}
		{
			AdaptedPan pan; pan.contexts[0] = context;
			auto contacts = firstContacts;
			contacts.push_back(AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 40, 300, 0, Phase::Down));
			contacts.push_back(AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 41, 300, 0, Phase::Cancelled));
			const auto out = pan.Step(80, contacts);
			check(out.phase == CanvasPanPhase::Idle && out.releaseReason == CanvasPanReleaseReason::Cancelled &&
				out.touchDecision.mode == TouchReleaseMode::Stop && out.touchDecision.reason == TouchReleaseReason::Cancelled &&
				out.releaseTouchTag.group != firstRelease.releaseTouchTag.group && out.touchDecision.velocitySeed.x == 0 &&
				out.touchDecision.velocitySeed.y == 0 && out.touchDecision.handVelocity.x == 0 && out.touchDecision.distanceBudget == 0,
				"a second normal Touch cancellation overwrites earlier same-frame decision instead of retaining its seed");
		}
	}

	void TestTouchGroupVelocityAndBudgetTags(Checks& check)
	{
		const auto context = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		const auto point = [&](uint64_t key, double ms, double mm, CanvasPanContactPhase phase = CanvasPanContactPhase::Move)
		{ return AdaptedContact(key, CanvasPanSourceClass::Touch, 0, ms, mm / context.motionPerDipX, 0, phase); };
		const auto seed = [&]()
		{
			AdaptedPan pan; pan.contexts[0] = context;
			for (int n = 0; n <= 4; ++n) pan.Step(n * 8, { point(1, n * 8, n * 8, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down) });
			pan.Step(33, { point(2, 33, 100, CanvasPanContactPhase::Down) }); return pan;
		};
		{
			auto pan = seed(); pan.Step(40, { point(1, 40, 40) }); pan.Step(48, { point(1, 48, 48) });
			pan.Step(49, { point(1, 49, 48, CanvasPanContactPhase::Up) });
			const auto out = pan.Step(50, { point(2, 50, 100, CanvasPanContactPhase::Up) });
			check.Within(out.releaseMotionVelocity.x, 490, 510, "moving primary plus stationary second finger releases actual focus speed, not the fastest personal velocity");
			check(out.touchDecision.evidenceContactKey == 0, "multi-contact content candidate is explicitly group-sourced");
		}
		for (const double lastMove : { 100.3, 90.0 })
		{
			auto pan = seed(); pan.Step(40, { point(1, 40, 32, CanvasPanContactPhase::Up) });
			pan.Step(41, { point(2, 41, lastMove) }); const auto out = pan.Step(42, { point(2, 42, lastMove, CanvasPanContactPhase::Up) });
			check(out.releaseMotionVelocity.x < 200 && (lastMove > 100 || out.releaseMotionVelocity.x <= 0),
				"new slow or reverse last-finger movement cannot indefinitely reuse the retired primary's high forward candidate");
		}
		{
			AdaptedPan pan; pan.contexts[0] = context; CanvasViewportState viewport;
			for (int n = 0; n <= 4; ++n)
			{
				const auto out = pan.Step(n * 8, { point(1, n * 8, n * 8, n ? CanvasPanContactPhase::Move : CanvasPanContactPhase::Down) });
				const auto applied = ApplyCanvasContentTranslationChecked(viewport, out.contentDelta);
				pan.dynamics->ConfirmAppliedMotion(out.motionFeedback, { -applied.viewportDelta.x, -applied.viewportDelta.y });
			}
			for (int ms = 40; ms <= 120; ms += 8) pan.Step(ms, { point(1, ms, 32) });
			const auto jump = pan.Step(121, { point(1, 121, 132) });
			const auto applied = ApplyCanvasContentTranslationChecked(viewport, jump.contentDelta);
			pan.dynamics->ConfirmAppliedMotion(jump.motionFeedback, { -applied.viewportDelta.x, -applied.viewportDelta.y });
			const auto out = pan.Step(122, { point(1, 122, 132, CanvasPanContactPhase::Up) });
			check(out.phase == CanvasPanPhase::Idle && out.touchDecision.mode == TouchReleaseMode::Stop,
				"confirmed long stop retires low-confidence distance intent; one later jump cannot borrow old budget or candidate");
		}
		{
			AdaptedPan pan; pan.contexts[0] = context;
			const auto out = pan.Step(4, { point(1, 0, 0, CanvasPanContactPhase::Down), point(1, 1, 10),
				point(1, 2, 10, CanvasPanContactPhase::Up), point(2, 3, 20, CanvasPanContactPhase::Down), point(2, 4, 30) });
			check(out.motionFeedback.present && !out.motionFeedback.eligible && !pan.dynamics->ConfirmAppliedMotion(out.motionFeedback, out.contentDelta),
				"same-frame mixed accepted touch groups cannot charge their combined displacement to the newest group's budget");
		}
	}

	void TestMotionProfilesAndFriction(Checks& check)
	{
		Info("motion profiles: physical units, bounded mode response and final boost cap");
		auto touch = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		auto pen = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical);
		check(touch.touchSlop == 2.5 && pen.touchSlop == 1.5 && touch.velocityLimit == 2400 && pen.velocityLimit == 1800,
			"physical profiles use independently specified space and speed thresholds");
		for (const auto profile : { CanvasPanMotionProfile::IndirectDip, CanvasPanMotionProfile::TouchFallback,
			CanvasPanMotionProfile::ScreenPenFallback, CanvasPanMotionProfile::TouchPhysical, CanvasPanMotionProfile::ScreenPenPhysical })
			check(IsCanvasPanMotionContextValid(MakeCanvasPanMotionContext(profile, .2, .3)), "all profile factories yield finite numeric contexts");
		auto bad = touch; bad.motionPerDipX = .001;
		check(!IsCanvasPanMotionContextValid(bad), "individual density axis cannot hide behind a valid geometric mean");
		bad = touch; bad.motionPerDipY = std::numeric_limits<double>::quiet_NaN();
		check(!IsCanvasPanMotionContextValid(bad), "nonfinite physical axis is rejected");
		bad = touch; bad.unit = CanvasPanMotionUnit::SystemDip;
		check(!IsCanvasPanMotionContextValid(bad), "motion label cannot disagree with profile units");
		touch.touchWeight = 1;
		auto params = ResolveCanvasPanFriction(touch, 600);
		check.Near(params.tauSeconds, .30, 1e-12, "classroom response has no fast amplification at 600 mm/s");
		params = ResolveCanvasPanFriction(touch, 1500);
		check.Near(params.tauSeconds, .42, 1e-12, "classroom fast response reaches .42s tau");
		check.Near(params.deceleration, 150, 1e-12, "classroom fast response reaches 150 mm/s squared");
		auto fallback = MakeCanvasPanMotionContext(CanvasPanMotionProfile::TouchFallback);
		fallback.largeScreen = true;
		params = ResolveCanvasPanFriction(fallback, 2250);
		check.Near(params.tauSeconds, .36, 1e-12, "large-screen fallback has a smaller maximum tau increase");
		check.Near(params.deceleration, 875, 1e-12, "fallback response keeps DIP deceleration units");
		fallback.largeScreen = false;
		check.Near(ResolveCanvasPanFriction(fallback, 12000).tauSeconds, .3, 1e-12, "desktop fallback stays neutral at all speeds");

		for (const double speed : { 100.0, 500.0, 600.0, 750.0, 1000.0, 1500.0, 2400.0 })
		{
			CanvasPanFlingCurve base, shorter;
			base.Start({ static_cast<float>(speed), 0 }, { 1000, 1000 }, { .3, 200 });
			shorter.Start({ static_cast<float>(speed), 0 }, { 1000, 1000 }, { .2, 350 });
			check(shorter.Distance().x < base.Distance().x && shorter.Duration() < base.Duration(), "same-effective-speed screen pen is shorter in distance and time");
			double previousDistance = base.Distance().x;
			for (const double weight : { 0.0, .25, .5, .75, 1.0 })
			{
				touch.touchWeight = weight;
				CanvasPanFlingCurve curve; curve.Start({ static_cast<float>(speed), 0 }, { 1000, 1000 }, ResolveCanvasPanFriction(touch, speed));
				check(curve.Distance().x + 1e-4 >= previousDistance, "physical response is monotonic in surface weight");
				check(curve.Distance().x <= base.Distance().x * 1.4 + 1e-3 && curve.Duration() <= base.Duration() * 1.4 + 1e-9,
					"equal-effective-v0 single physical coast obeys forty-percent bound");
				previousDistance = curve.Distance().x;
			}
		}
		for (const auto profile : { CanvasPanMotionProfile::TouchPhysical, CanvasPanMotionProfile::ScreenPenPhysical,
			CanvasPanMotionProfile::TouchFallback, CanvasPanMotionProfile::ScreenPenFallback })
		{
			const auto c = MakeCanvasPanMotionContext(profile, .2, .3);
			CanvasPanFlingBooster booster; booster.Configure(c);
			booster.GetVelocityForFlingStart(Gesture(CanvasPanGestureType::FlingStart, 1, { static_cast<float>(c.velocityLimit), 0 }));
			booster.ObserveProgressFling({ static_cast<float>(c.velocityLimit), 0 });
			booster.ObserveGestureEvent(Gesture(CanvasPanGestureType::FlingCancel, 1.01));
			const auto v = booster.GetVelocityForFlingStart(Gesture(CanvasPanGestureType::FlingStart, 1.02, { static_cast<float>(c.velocityLimit), 0 }));
			check.Near(std::hypot(v.x, v.y), c.velocityLimit, .001, "profile vector cap applies after residual addition");
		}
	}

	void TestScreenPenArrivalTiming(Checks& check)
	{
		// 2026-11-14 用户日志的五个完整手势：保留原始QPC、像素坐标和全部Down/Move/Up。
		// 原版本五次均 compressed-time/selected=0；不为截断的key75补造Down或时间。
		struct Recorded { int64_t qpc; double x, y; CanvasPanContactPhase phase; };
		using Phase = CanvasPanContactPhase;
		static constexpr Recorded trace76[] = {
			{ 41347036923LL, 858.709, 1499.339, Phase::Down },
			{ 41347040260LL, 864.076, 1492.082, Phase::Move },
			{ 41347061593LL, 868.082, 1486.639, Phase::Move },
			{ 41347062181LL, 871.710, 1481.348, Phase::Move },
			{ 41347136198LL, 875.036, 1477.266, Phase::Move },
			{ 41347136713LL, 877.758, 1474.243, Phase::Move },
			{ 41347208352LL, 880.554, 1471.068, Phase::Move },
			{ 41347209713LL, 885.165, 1465.928, Phase::Move },
			{ 41347293344LL, 893.178, 1457.083, Phase::Move },
			{ 41347305159LL, 905.046, 1444.082, Phase::Move },
			{ 41347359860LL, 920.088, 1427.754, Phase::Move },
			{ 41347365996LL, 936.491, 1410.822, Phase::Move },
			{ 41347448451LL, 952.365, 1394.495, Phase::Move },
			{ 41347452734LL, 967.559, 1379.150, Phase::Move },
			{ 41347510070LL, 982.375, 1364.409, Phase::Move },
			{ 41347515283LL, 996.737, 1350.350, Phase::Move },
			{ 41347587260LL, 1011.402, 1336.063, Phase::Move },
			{ 41347591223LL, 1026.520, 1321.550, Phase::Move },
			{ 41347661124LL, 1041.562, 1307.036, Phase::Move },
			{ 41347664714LL, 1056.302, 1292.750, Phase::Move },
			{ 41347739217LL, 1071.647, 1277.783, Phase::Move },
			{ 41347742349LL, 1087.219, 1262.438, Phase::Move },
			{ 41347809618LL, 1103.924, 1245.959, Phase::Move },
			{ 41347813731LL, 1121.613, 1228.573, Phase::Move },
			{ 41347888437LL, 1140.132, 1210.205, Phase::Move },
			{ 41347893741LL, 1159.257, 1190.854, Phase::Move },
			{ 41347968203LL, 1178.759, 1170.822, Phase::Move },
			{ 41347972733LL, 1198.564, 1150.564, Phase::Move },
			{ 41348037929LL, 1219.276, 1129.398, Phase::Move },
			{ 41348042541LL, 1241.121, 1107.402, Phase::Move },
			{ 41348120938LL, 1263.874, 1084.498, Phase::Move },
			{ 41348125671LL, 1287.534, 1060.838, Phase::Move },
			{ 41348190652LL, 1311.269, 1036.951, Phase::Move },
			{ 41348195800LL, 1335.458, 1012.913, Phase::Move },
			{ 41348260445LL, 1360.781, 987.969, Phase::Move },
			{ 41348263534LL, 1387.389, 962.192, Phase::Move },
			{ 41348340137LL, 1415.509, 935.055, Phase::Move },
			{ 41348345601LL, 1445.291, 906.709, Phase::Move },
			{ 41348410683LL, 1475.452, 878.211, Phase::Move },
			{ 41348416363LL, 1505.008, 850.847, Phase::Move },
			{ 41348488535LL, 1534.942, 823.635, Phase::Move },
			{ 41348492363LL, 1566.312, 796.044, Phase::Move },
			{ 41348565918LL, 1599.420, 767.471, Phase::Move },
			{ 41348571608LL, 1634.117, 738.444, Phase::Move },
			{ 41348640645LL, 1669.568, 709.417, Phase::Move },
			{ 41348645566LL, 1705.852, 680.920, Phase::Move },
			{ 41348714627LL, 1742.665, 652.951, Phase::Move },
			{ 41348719122LL, 1779.628, 625.663, Phase::Move },
			{ 41348801529LL, 1817.726, 598.753, Phase::Move },
			{ 41348811027LL, 1856.504, 571.994, Phase::Move },
			{ 41348864341LL, 1895.357, 546.066, Phase::Move },
			{ 41348872252LL, 1932.775, 521.877, Phase::Move },
			{ 41348938766LL, 1970.192, 498.142, Phase::Move },
			{ 41348941921LL, 2008.365, 474.482, Phase::Move },
			{ 41349140899LL, 2047.143, 451.124, Phase::Move },
			{ 41349145355LL, 2086.375, 427.994, Phase::Move },
			{ 41349151986LL, 2126.135, 404.863, Phase::Move },
			{ 41349155950LL, 2166.123, 382.035, Phase::Move },
			{ 41349207963LL, 2205.506, 359.509, Phase::Move },
			{ 41349240139LL, 2205.506, 359.509, Phase::Up },
		};
		static constexpr Recorded trace77[] = {
			{ 41353643404LL, 2482.545, 232.365, Phase::Down },
			{ 41353664499LL, 2477.707, 234.784, Phase::Move },
			{ 41353742027LL, 2473.247, 237.354, Phase::Move },
			{ 41353742441LL, 2470.073, 239.471, Phase::Move },
			{ 41353812935LL, 2467.654, 241.436, Phase::Move },
			{ 41353813624LL, 2465.083, 243.477, Phase::Move },
			{ 41353891400LL, 2460.397, 246.728, Phase::Move },
			{ 41353891959LL, 2451.704, 252.472, Phase::Move },
			{ 41353964757LL, 2437.947, 261.770, Phase::Move },
			{ 41353975475LL, 2419.729, 274.167, Phase::Move },
			{ 41354043371LL, 2399.017, 288.378, Phase::Move },
			{ 41354045769LL, 2378.003, 303.194, Phase::Move },
			{ 41354117941LL, 2356.838, 318.312, Phase::Move },
			{ 41354124570LL, 2335.370, 333.959, Phase::Move },
			{ 41354195041LL, 2314.356, 349.455, Phase::Move },
			{ 41354269198LL, 2274.217, 379.313, Phase::Move },
			{ 41354273882LL, 2253.808, 394.734, Phase::Move },
			{ 41354341757LL, 2232.416, 411.061, Phase::Move },
			{ 41354345526LL, 2210.192, 428.069, Phase::Move },
			{ 41354418251LL, 2186.835, 445.984, Phase::Move },
			{ 41354425387LL, 2162.646, 464.504, Phase::Move },
			{ 41354495885LL, 2137.398, 483.855, Phase::Move },
			{ 41354500364LL, 2111.320, 503.811, Phase::Move },
			{ 41354569660LL, 2084.258, 524.750, Phase::Move },
			{ 41354574328LL, 2056.441, 546.595, Phase::Move },
			{ 41354642814LL, 2027.490, 570.104, Phase::Move },
			{ 41354648611LL, 1997.934, 594.746, Phase::Move },
			{ 41354726328LL, 1967.547, 619.918, Phase::Move },
			{ 41354730656LL, 1937.688, 644.485, Phase::Move },
			{ 41354797074LL, 1906.696, 670.110, Phase::Move },
			{ 41354802519LL, 1874.721, 696.491, Phase::Move },
			{ 41354867902LL, 1840.857, 724.989, Phase::Move },
			{ 41354871533LL, 1805.405, 755.074, Phase::Move },
			{ 41354946796LL, 1769.575, 785.991, Phase::Move },
			{ 41354950997LL, 1734.198, 817.058, Phase::Move },
			{ 41355017218LL, 1698.066, 849.033, Phase::Move },
			{ 41355021302LL, 1661.178, 881.991, Phase::Move },
			{ 41355095910LL, 1623.912, 915.326, Phase::Move },
			{ 41355169196LL, 1550.438, 982.526, Phase::Move },
			{ 41355173558LL, 1512.794, 1018.054, Phase::Move },
			{ 41355244521LL, 1475.150, 1053.732, Phase::Move },
			{ 41355248273LL, 1437.808, 1089.109, Phase::Move },
			{ 41355318152LL, 1400.542, 1125.392, Phase::Move },
			{ 41355323555LL, 1363.200, 1162.356, Phase::Move },
			{ 41355395858LL, 1326.539, 1199.017, Phase::Move },
			{ 41355399739LL, 1290.331, 1235.830, Phase::Move },
			{ 41355467477LL, 1254.576, 1272.189, Phase::Move },
			{ 41355471346LL, 1219.049, 1308.926, Phase::Move },
			{ 41355561025LL, 1184.277, 1345.058, Phase::Move },
			{ 41355566135LL, 1150.488, 1380.435, Phase::Move },
			{ 41355618776LL, 1117.833, 1415.660, Phase::Move },
			{ 41355624127LL, 1086.009, 1450.961, Phase::Move },
			{ 41355695279LL, 1054.791, 1484.976, Phase::Move },
			{ 41355699262LL, 1024.252, 1517.783, Phase::Move },
			{ 41355772208LL, 994.847, 1549.606, Phase::Move },
			{ 41355779174LL, 967.257, 1580.674, Phase::Move },
			{ 41355844949LL, 940.422, 1610.608, Phase::Move },
			{ 41355917407LL, 940.422, 1610.608, Phase::Up },
		};
		static constexpr Recorded trace78[] = {
			{ 41360248439LL, 799.672, 1457.386, Phase::Down },
			{ 41360270889LL, 802.091, 1456.025, Phase::Move },
			{ 41360350716LL, 804.888, 1454.665, Phase::Move },
			{ 41360351646LL, 807.307, 1453.606, Phase::Move },
			{ 41360420245LL, 809.272, 1452.850, Phase::Move },
			{ 41360420771LL, 811.465, 1452.170, Phase::Move },
			{ 41360498405LL, 815.169, 1450.809, Phase::Move },
			{ 41360499134LL, 821.972, 1448.391, Phase::Move },
			{ 41360571388LL, 832.706, 1444.460, Phase::Move },
			{ 41360572043LL, 846.539, 1439.773, Phase::Move },
			{ 41360657918LL, 875.943, 1430.400, Phase::Move },
			{ 41360727619LL, 890.154, 1426.394, Phase::Move },
			{ 41360732754LL, 904.365, 1422.690, Phase::Move },
			{ 41360801272LL, 918.728, 1419.439, Phase::Move },
			{ 41360804796LL, 933.468, 1416.265, Phase::Move },
			{ 41360873605LL, 948.737, 1413.165, Phase::Move },
			{ 41360880710LL, 964.384, 1409.991, Phase::Move },
			{ 41360953023LL, 980.712, 1406.816, Phase::Move },
			{ 41360958816LL, 997.644, 1403.565, Phase::Move },
			{ 41361022853LL, 1015.030, 1400.391, Phase::Move },
			{ 41361026900LL, 1032.945, 1397.367, Phase::Move },
			{ 41361106744LL, 1051.162, 1394.192, Phase::Move },
			{ 41361113514LL, 1069.757, 1391.168, Phase::Move },
			{ 41361175905LL, 1088.806, 1388.220, Phase::Move },
			{ 41361181724LL, 1107.931, 1385.272, Phase::Move },
			{ 41361250784LL, 1128.038, 1382.249, Phase::Move },
			{ 41361254885LL, 1148.901, 1378.847, Phase::Move },
			{ 41361334552LL, 1170.369, 1375.068, Phase::Move },
			{ 41361338888LL, 1192.441, 1371.061, Phase::Move },
			{ 41361402238LL, 1214.967, 1366.526, Phase::Move },
			{ 41361406242LL, 1238.022, 1361.613, Phase::Move },
			{ 41361473070LL, 1261.304, 1356.472, Phase::Move },
			{ 41361476545LL, 1284.359, 1351.408, Phase::Move },
			{ 41361566755LL, 1308.246, 1346.268, Phase::Move },
			{ 41361570778LL, 1333.039, 1341.203, Phase::Move },
			{ 41361623958LL, 1358.287, 1335.912, Phase::Move },
			{ 41361628154LL, 1384.063, 1330.545, Phase::Move },
			{ 41361705335LL, 1410.444, 1325.254, Phase::Move },
			{ 41361711590LL, 1437.354, 1319.962, Phase::Move },
			{ 41361773843LL, 1465.021, 1314.595, Phase::Move },
			{ 41361779097LL, 1493.140, 1309.228, Phase::Move },
			{ 41361857392LL, 1521.109, 1304.164, Phase::Move },
			{ 41361867077LL, 1549.153, 1299.553, Phase::Move },
			{ 41361925499LL, 1578.784, 1294.791, Phase::Move },
			{ 41361933405LL, 1609.474, 1290.180, Phase::Move },
			{ 41362026168LL, 1641.071, 1285.568, Phase::Move },
			{ 41362031977LL, 1673.575, 1280.958, Phase::Move },
			{ 41362074492LL, 1706.154, 1276.346, Phase::Move },
			{ 41362079386LL, 1738.432, 1271.660, Phase::Move },
			{ 41362152995LL, 1771.238, 1267.200, Phase::Move },
			{ 41362157379LL, 1804.876, 1262.665, Phase::Move },
			{ 41362224402LL, 1839.269, 1258.129, Phase::Move },
			{ 41362228873LL, 1874.268, 1253.820, Phase::Move },
			{ 41362304237LL, 1909.946, 1249.058, Phase::Move },
			{ 41362308911LL, 1946.079, 1244.372, Phase::Move },
			{ 41362375767LL, 1982.816, 1239.383, Phase::Move },
			{ 41362382743LL, 2019.704, 1234.545, Phase::Move },
			{ 41362452838LL, 2056.970, 1229.556, Phase::Move },
			{ 41362458718LL, 2094.690, 1224.491, Phase::Move },
			{ 41362531843LL, 2133.846, 1219.200, Phase::Move },
			{ 41362537644LL, 2173.984, 1213.984, Phase::Move },
			{ 41362601240LL, 2214.803, 1208.693, Phase::Move },
			{ 41362606110LL, 2256.302, 1203.477, Phase::Move },
			{ 41362693320LL, 2297.877, 1197.959, Phase::Move },
			{ 41362699960LL, 2338.469, 1192.819, Phase::Move },
			{ 41362750118LL, 2379.591, 1187.906, Phase::Move },
			{ 41362755899LL, 2421.090, 1183.143, Phase::Move },
			{ 41362823259LL, 2461.909, 1178.835, Phase::Move },
			{ 41362826882LL, 2501.972, 1174.979, Phase::Move },
			{ 41362902226LL, 2542.942, 1171.124, Phase::Move },
			{ 41362906870LL, 2584.592, 1167.723, Phase::Move },
			{ 41362974987LL, 2626.016, 1164.246, Phase::Move },
			{ 41362979913LL, 2667.288, 1161.071, Phase::Move },
			{ 41363052379LL, 2708.258, 1158.123, Phase::Move },
			{ 41363057040LL, 2749.153, 1155.402, Phase::Move },
			{ 41363272117LL, 2880.832, 1146.633, Phase::Move },
			{ 41363281683LL, 2880.832, 1146.633, Phase::Up },
		};
		static constexpr Recorded trace79[] = {
			{ 41368804677LL, 2526.161, 1217.613, Phase::Down },
			{ 41368836720LL, 2522.683, 1218.671, Phase::Move },
			{ 41368910185LL, 2518.828, 1219.427, Phase::Move },
			{ 41368910952LL, 2515.351, 1219.880, Phase::Move },
			{ 41368981146LL, 2512.176, 1219.956, Phase::Move },
			{ 41368982157LL, 2508.926, 1219.880, Phase::Move },
			{ 41369059541LL, 2503.559, 1219.880, Phase::Move },
			{ 41369060439LL, 2493.657, 1220.031, Phase::Move },
			{ 41369133206LL, 2477.783, 1220.258, Phase::Move },
			{ 41369145776LL, 2456.542, 1220.183, Phase::Move },
			{ 41369217968LL, 2432.353, 1219.502, Phase::Move },
			{ 41369231938LL, 2408.239, 1218.293, Phase::Move },
			{ 41369284618LL, 2385.109, 1216.479, Phase::Move },
			{ 41369304065LL, 2363.036, 1214.135, Phase::Move },
			{ 41369362308LL, 2341.190, 1211.339, Phase::Move },
			{ 41369366831LL, 2319.345, 1208.088, Phase::Move },
			{ 41369433890LL, 2296.895, 1204.082, Phase::Move },
			{ 41369436940LL, 2273.915, 1199.471, Phase::Move },
			{ 41369512990LL, 2250.104, 1194.482, Phase::Move },
			{ 41369524309LL, 2225.613, 1189.115, Phase::Move },
			{ 41369582027LL, 2200.592, 1183.748, Phase::Move },
			{ 41369586193LL, 2175.420, 1178.608, Phase::Move },
			{ 41369661695LL, 2149.493, 1173.694, Phase::Move },
			{ 41369666138LL, 2123.112, 1168.932, Phase::Move },
			{ 41369732620LL, 2095.143, 1164.321, Phase::Move },
			{ 41369738999LL, 2065.814, 1159.786, Phase::Move },
			{ 41369811856LL, 2036.031, 1155.628, Phase::Move },
			{ 41369815935LL, 2006.173, 1151.773, Phase::Move },
			{ 41369883233LL, 1975.332, 1148.674, Phase::Move },
			{ 41369888970LL, 1943.584, 1146.028, Phase::Move },
			{ 41369982661LL, 1911.005, 1144.214, Phase::Move },
			{ 41369988700LL, 1878.501, 1143.005, Phase::Move },
			{ 41370035789LL, 1844.183, 1142.249, Phase::Move },
			{ 41370046509LL, 1808.655, 1141.795, Phase::Move },
			{ 41370109643LL, 1771.691, 1141.946, Phase::Move },
			{ 41370114931LL, 1733.896, 1142.476, Phase::Move },
			{ 41370188354LL, 1695.647, 1143.609, Phase::Move },
			{ 41370193003LL, 1657.172, 1145.424, Phase::Move },
			{ 41370260315LL, 1618.167, 1147.691, Phase::Move },
			{ 41370265291LL, 1578.482, 1150.337, Phase::Move },
			{ 41370332893LL, 1538.872, 1153.209, Phase::Move },
			{ 41370336891LL, 1499.717, 1156.157, Phase::Move },
			{ 41370410066LL, 1459.880, 1158.954, Phase::Move },
			{ 41370414560LL, 1419.288, 1161.827, Phase::Move },
			{ 41370487042LL, 1378.772, 1164.397, Phase::Move },
			{ 41370493651LL, 1338.633, 1167.042, Phase::Move },
			{ 41370561404LL, 1298.041, 1169.235, Phase::Move },
			{ 41370569362LL, 1257.222, 1171.276, Phase::Move },
			{ 41370634615LL, 1216.554, 1172.939, Phase::Move },
			{ 41370654654LL, 1175.962, 1174.299, Phase::Move },
			{ 41370711804LL, 1136.202, 1175.357, Phase::Move },
			{ 41370716855LL, 1097.499, 1176.113, Phase::Move },
			{ 41370785626LL, 1056.151, 1176.416, Phase::Move },
			{ 41370797313LL, 1013.140, 1176.642, Phase::Move },
			{ 41370861145LL, 968.617, 1176.416, Phase::Move },
			{ 41370868310LL, 922.507, 1175.811, Phase::Move },
			{ 41370934793LL, 878.589, 1175.962, Phase::Move },
			{ 41370943881LL, 837.165, 1177.021, Phase::Move },
			{ 41371014049LL, 796.951, 1178.532, Phase::Move },
			{ 41371019409LL, 757.720, 1180.422, Phase::Move },
			{ 41371083712LL, 717.808, 1182.085, Phase::Move },
			{ 41371088074LL, 677.291, 1183.597, Phase::Move },
			{ 41371164432LL, 635.868, 1184.958, Phase::Move },
			{ 41371169248LL, 593.688, 1186.318, Phase::Move },
			{ 41371235848LL, 551.735, 1187.981, Phase::Move },
			{ 41371241981LL, 510.236, 1190.098, Phase::Move },
			{ 41371310755LL, 468.586, 1192.290, Phase::Move },
			{ 41371313870LL, 426.935, 1194.633, Phase::Move },
			{ 41371382437LL, 386.041, 1197.052, Phase::Move },
			{ 41371386409LL, 346.054, 1199.622, Phase::Move },
			{ 41371462370LL, 307.049, 1202.570, Phase::Move },
			{ 41371467713LL, 268.649, 1205.745, Phase::Move },
			{ 41371553102LL, 231.458, 1208.995, Phase::Move },
			{ 41371559018LL, 195.250, 1212.170, Phase::Move },
			{ 41371614146LL, 159.345, 1215.420, Phase::Move },
			{ 41371620413LL, 123.666, 1218.671, Phase::Move },
			{ 41371856626LL, 0.000, 1229.934, Phase::Move },
			{ 41371863955LL, 0.000, 1229.934, Phase::Up },
		};
		static constexpr Recorded trace80[] = {
			{ 41379621864LL, 909.883, 1212.624, Phase::Down },
			{ 41379647780LL, 914.419, 1210.809, Phase::Move },
			{ 41379717824LL, 918.954, 1209.298, Phase::Move },
			{ 41379718382LL, 922.658, 1208.315, Phase::Move },
			{ 41379790706LL, 925.682, 1207.408, Phase::Move },
			{ 41379791749LL, 929.386, 1206.425, Phase::Move },
			{ 41379868851LL, 935.660, 1204.460, Phase::Move },
			{ 41379869591LL, 947.225, 1200.832, Phase::Move },
			{ 41379953805LL, 984.794, 1189.644, Phase::Move },
			{ 41380037384LL, 1005.581, 1184.202, Phase::Move },
			{ 41380041579LL, 1025.159, 1179.817, Phase::Move },
			{ 41380093055LL, 1043.074, 1176.416, Phase::Move },
			{ 41380099049LL, 1059.326, 1173.921, Phase::Move },
			{ 41380170629LL, 1074.444, 1172.107, Phase::Move },
			{ 41380173709LL, 1088.882, 1170.746, Phase::Move },
			{ 41380241710LL, 1103.320, 1169.613, Phase::Move },
			{ 41380246388LL, 1117.833, 1168.706, Phase::Move },
			{ 41380323826LL, 1132.422, 1167.723, Phase::Move },
			{ 41380326872LL, 1147.011, 1166.665, Phase::Move },
			{ 41380393167LL, 1162.205, 1165.380, Phase::Move },
			{ 41380397173LL, 1177.625, 1163.868, Phase::Move },
			{ 41380471695LL, 1193.953, 1162.356, Phase::Move },
			{ 41380480653LL, 1211.112, 1160.617, Phase::Move },
			{ 41380543693LL, 1228.876, 1158.879, Phase::Move },
			{ 41380550838LL, 1247.017, 1156.989, Phase::Move },
			{ 41380621237LL, 1265.235, 1154.646, Phase::Move },
			{ 41380626762LL, 1283.452, 1151.924, Phase::Move },
			{ 41380705737LL, 1301.820, 1148.976, Phase::Move },
			{ 41380710295LL, 1320.038, 1145.953, Phase::Move },
			{ 41380771956LL, 1338.406, 1142.854, Phase::Move },
			{ 41380777999LL, 1357.153, 1139.830, Phase::Move },
			{ 41380842243LL, 1376.050, 1136.579, Phase::Move },
			{ 41380845285LL, 1395.024, 1133.178, Phase::Move },
			{ 41380919890LL, 1415.282, 1129.323, Phase::Move },
			{ 41380925466LL, 1436.220, 1125.317, Phase::Move },
			{ 41380991551LL, 1457.235, 1120.857, Phase::Move },
			{ 41380996565LL, 1478.249, 1116.472, Phase::Move },
			{ 41381069584LL, 1499.339, 1111.559, Phase::Move },
			{ 41381078129LL, 1520.655, 1106.419, Phase::Move },
			{ 41381144006LL, 1541.669, 1101.052, Phase::Move },
			{ 41381151156LL, 1562.003, 1095.836, Phase::Move },
			{ 41381220589LL, 1583.017, 1090.318, Phase::Move },
			{ 41381225979LL, 1604.485, 1084.649, Phase::Move },
			{ 41381295471LL, 1626.331, 1078.828, Phase::Move },
			{ 41381301508LL, 1648.403, 1072.932, Phase::Move },
			{ 41381386823LL, 1670.400, 1066.809, Phase::Move },
			{ 41381393202LL, 1691.641, 1060.838, Phase::Move },
			{ 41381442518LL, 1713.109, 1054.564, Phase::Move },
			{ 41381446746LL, 1735.030, 1047.987, Phase::Move },
			{ 41381521896LL, 1757.480, 1041.411, Phase::Move },
			{ 41381525074LL, 1780.157, 1034.910, Phase::Move },
			{ 41381595518LL, 1803.288, 1028.258, Phase::Move },
			{ 41381601604LL, 1826.419, 1021.833, Phase::Move },
			{ 41381671513LL, 1849.247, 1015.635, Phase::Move },
			{ 41381675436LL, 1871.320, 1009.739, Phase::Move },
			{ 41381743377LL, 1893.543, 1004.372, Phase::Move },
			{ 41381749575LL, 1915.843, 999.534, Phase::Move },
			{ 41381821072LL, 1937.688, 994.998, Phase::Move },
			{ 41381825293LL, 1959.080, 990.917, Phase::Move },
			{ 41381896501LL, 1979.641, 987.061, Phase::Move },
			{ 41381900621LL, 1999.446, 983.433, Phase::Move },
			{ 41381973098LL, 2018.646, 980.258, Phase::Move },
			{ 41381981545LL, 2037.694, 977.461, Phase::Move },
			{ 41382091436LL, 2055.987, 975.269, Phase::Move },
			{ 41382124041LL, 2055.987, 975.269, Phase::Up },
		};
		const std::span<const Recorded> traces[] = { trace76, trace77, trace78, trace79, trace80 };
		const double expectedSpeeds[] = { 1029.25107476, 1190.79830192, 879.28498027, 769.42038468, 509.16381190 };
		for (size_t index = 0; index < std::size(traces); ++index)
			for (const bool queued : { false, true })
			{
				CanvasPanDynamics dynamics;
				const auto context = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical, .19444445, .19791666);
				std::vector<CanvasPanContactSample> contacts;
				for (const auto& recorded : traces[index])
					contacts.push_back({ 1, recorded.qpc, recorded.x * .5, recorded.y * .5, recorded.phase,
						(static_cast<uint64_t>(CanvasPanSourceClass::Pen) << 32) | 1661867520u, true, 0 });
				std::array<CanvasPanAdmissionDecision, 1> decisions;
				CanvasPanFrameInput input;
				input.qpcFrequency = kFreq; input.viewportSizeDip = { 1920, 1080 };
				input.motionContexts = std::span(&context, 1); input.admissionDecisions = decisions;
				CanvasPanFrameOutput released;
				if (queued)
				{
					input.contacts = contacts; input.frameQpc = contacts.back().qpc + 1;
					released = dynamics.Step(input);
				}
				else
					for (const auto& contact : contacts)
					{
						input.contacts = std::span(&contact, 1); input.frameQpc = contact.qpc;
						released = dynamics.Step(input);
					}
				check(released.released && released.releaseReason == CanvasPanReleaseReason::Ready &&
					released.phase == CanvasPanPhase::Coasting && released.releaseSpeed > 0 && released.heldContacts == 0,
					"complete screen pen trace has supported nonzero release in live and queued delivery");
				check.Near(std::hypot(released.rawReleaseVelocity.x * context.motionPerDipX,
					released.rawReleaseVelocity.y * context.motionPerDipY), expectedSpeeds[index], .1,
					"real screen pen tail fits independent actual anchors instead of compressed adjacent arrivals");
				check(std::hypot(released.releaseMotionVelocity.x, released.releaseMotionVelocity.y) <= context.velocityLimit + .01 &&
					released.frictionParameters.tauSeconds == .2 && released.frictionParameters.deceleration == 350,
					"recovered release retains the same screen pen vector cap and precision friction");
				input.contacts = {}; input.frameQpc += kFreq / 100;
				const auto coast = dynamics.Step(input);
				check(std::hypot(coast.contentDelta.x, coast.contentDelta.y) > 0 && coast.phase == CanvasPanPhase::Coasting,
					"screen pen release advances actual free motion after the terminal instead of only delayed drag");
			}

		for (const auto profile : { CanvasPanMotionProfile::ScreenPenPhysical, CanvasPanMotionProfile::ScreenPenFallback })
		{
			const auto context = profile == CanvasPanMotionProfile::ScreenPenPhysical ? PhysicalContext(profile) : MakeCanvasPanMotionContext(profile);
			const double scale = profile == CanvasPanMotionProfile::ScreenPenPhysical ? 1 : 5;
			const auto release = [&](std::initializer_list<std::pair<double, double>> points, double upMs)
			{
				AdaptedPan pan; pan.contexts[0] = context;
				std::vector<CanvasPanContactSample> contacts;
				for (const auto& [ms, x] : points)
					contacts.push_back(AdaptedContact(1, CanvasPanSourceClass::Pen, 0, ms, x * scale / context.motionPerDipX, 0,
						contacts.empty() ? Phase::Down : Phase::Move));
				contacts.push_back(AdaptedContact(1, CanvasPanSourceClass::Pen, 0, upMs, contacts.back().x, 0, Phase::Up));
				return pan.Step(upMs + .1, contacts);
			};
			auto out = release({ { 0, 0 }, { 20, 15 }, { 21, 30 } }, 22);
			check(out.phase == CanvasPanPhase::Idle && out.releaseReason == CanvasPanReleaseReason::InsufficientSamples && out.releaseSpeed == 0,
				"two real time anchors plus a compressed arrival cannot masquerade as three supported samples");
			out = release({ { 0, 0 }, { .1, 1 }, { .2, 2 }, { .3, 3 }, { .4, 4 }, { .5, 5 } }, 1);
			check(out.phase == CanvasPanPhase::Idle && out.releaseSpeed == 0,
				"an entirely compressed burst with no real time span cannot launch capped fake inertia");
			out = release({ { 0, 0 }, { 10, 3 }, { 20, 6 }, { 30, 9 }, { 40, 12 }, { 40.1, 42 } }, 41);
			check(out.phase == CanvasPanPhase::Coasting && out.timingResetCount == 0 && out.releaseSupportLimited &&
				out.releaseBoostSuppressed && std::isfinite(out.releaseSpeed) && out.heldContacts == 0,
				"isolated final movement preserves history but cannot boost beyond independent earlier support");
			check.Within(out.releaseMotionVelocity.x, 299.9 * scale, 300 * scale + context.reversalDistance / .01 + .2,
				"supported slow motion remains nonzero and within the prefix plus its spatial uncertainty");
			out = release({ { 0, 0 }, { 10, 5 }, { 20, 10 }, { 30, 15 }, { 40, 20 }, { 41, 10 } }, 42);
			check(out.phase == CanvasPanPhase::Idle && out.reversalCount == 1 && out.selectedReleaseVelocity.x == 0,
				"a real reversal inside a dense callback pair clears the old direction before timing coalescing");
			out = release({ { 0, 0 }, { 10, 3 }, { 20, 6 }, { 30, 9 }, { 40, 12 } }, 110);
			check(out.phase == CanvasPanPhase::Idle && out.releaseReason == CanvasPanReleaseReason::Stale && out.releaseSpeed == 0,
				"screen pen timing recovery cannot revive a stale pen release");
			out = release({ { 0, 0 }, { 10, 10 }, { 20, 20 }, { 30, 30 }, { 40, 40 }, { 50, 40.5 }, { 50.1, 40.55 } }, 51);
			check(out.releaseTailSpeedLimited && out.releaseMotionVelocity.x > 0 && out.releaseMotionVelocity.x < 100 * scale,
				"latest real endpoint and decelerating tail still cap release after dense callback recovery");
		}
	}

	void TestMotionReleaseQuality(Checks& check)
	{
		for (const double gain : { .25, 1.0, 2.0 })
		{
			auto c = MakeCanvasPanMotionContext(CanvasPanMotionProfile::TouchFallback);
			c.largeScreen = true; c.intentGain = gain;
			for (const double speed : { 100.0, 750.0, 2250.0, 6000.0, 12000.0 })
			{
				CanvasPanFlingCurve base, curve;
				base.Start({ static_cast<float>(speed), 0 }, { 1920, 1080 });
				curve.Start({ static_cast<float>(speed), 0 }, { 1920, 1080 }, ResolveCanvasPanFriction(c, speed));
				check(curve.Distance().x >= base.Distance().x - 1e-4 && curve.Distance().x <= base.Distance().x * 1.2 + .001 &&
					curve.Duration() <= base.Duration() * 1.2 + 1e-9, "DIP intent gain selects bounded response without changing initial speed");
			}
		}
		AdaptedPan continued;
		continued.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		continued.contexts[0].touchWeight = 1;
		AdaptedFling(continued, 1500);
		const auto coast = continued.Step(90);
		continued.Step(91, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 91, 1000, 0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 4; ++n)
			continued.Step(91 + n * 8, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 91 + n * 8, 1000 + 900 * n * .008 / .2) });
		auto out = continued.Step(124, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 124, 1000 + 900 * .032 / .2, 0, CanvasPanContactPhase::Up) });
		check.Near(out.boostedReleaseVelocity.x, out.selectedReleaseVelocity.x + coast.contentVelocity.x, .1,
			"physical same-direction continuation adds actual residual in the same metric");
		check.Near(out.releaseMotionVelocity.x, 900, .05, "physical pre-boost speed is independently diagnosed");
		check.Near(out.frictionParameters.tauSeconds, .331111111111, 1e-5,
			"boost cannot promote its own friction selection to the full fast-response tier");
		continued.Step(160, { AdaptedContact(3, CanvasPanSourceClass::Touch, 0, 160, 2000, 0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 4; ++n)
			continued.Step(160 + n * 8, { AdaptedContact(3, CanvasPanSourceClass::Touch, 0, 160 + n * 8, 2000 - 900 * n * .008 / .2) });
		out = continued.Step(193, { AdaptedContact(3, CanvasPanSourceClass::Touch, 0, 193, 2000 - 900 * .032 / .2, 0, CanvasPanContactPhase::Up) });
		check(out.boostedReleaseVelocity.x < 0 && out.selectedReleaseVelocity.x < 0,
			"physical reversal does not restore old positive residual direction");

		AdaptedPan generation;
		generation.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical); generation.contexts[0].sourceGeneration = 1;
		AdaptedFling(generation, 1500); generation.Step(90);
		generation.contexts[0].sourceGeneration = 2;
		out = generation.Step(91);
		check(out.phase == CanvasPanPhase::Coasting && generation.dynamics->MotionContext().sourceGeneration == 1,
			"source generation publication alone does not replace the owned active coast context");
		generation.Step(92, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 92, 1000, 0, CanvasPanContactPhase::Down) });
		for (int n = 1; n <= 4; ++n)
			generation.Step(92 + n * 8, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 92 + n * 8, 1000 + 500 * n * .008 / .2) });
		out = generation.Step(125, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 125, 1080, 0, CanvasPanContactPhase::Up) });
		check.Near(out.boostedReleaseVelocity.x, out.selectedReleaseVelocity.x, .01,
			"a new producer generation cannot inherit the old generation's residual despite reusing tablet context ID");

		AdaptedPan rapid; rapid.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		std::vector<CanvasPanContactSample> samples{ AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 1000, 0, CanvasPanContactPhase::Down) };
		for (int n = 1; n <= 96; ++n)
		{
			const double t = n / 960.0;
			const float inputDip = static_cast<float>(1000 + 2400 * t / .2);
			samples.push_back(AdaptedContact(1, CanvasPanSourceClass::Touch, 0, t * 1000, inputDip));
		}
		samples.push_back(AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 101, 2200, 0, CanvasPanContactPhase::Up));
		out = rapid.Step(150, samples);
		check(out.timingResetCount == 0 && out.releaseReason == CanvasPanReleaseReason::Ready,
			"960Hz float DIP samples at physical cap survive transformed coordinate and QPC quantization guards");
		check.Near(out.releaseMotionVelocity.x, 2400, .1, "high-frequency physical cap remains a reliable positive release");
	}

	void TestPhysicalMotionCanonicalOutput(Checks& check)
	{
		Info("motion units: anisotropic canonical DIP output, lifetime and physical equivalence");
		for (const double ratio : { .1, .2, .4 })
		{
			AdaptedPan pan; pan.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical, ratio, ratio * 1.5);
			const auto release = AdaptedFling(pan, 300, 400);
			check.Near(release.releaseMotionVelocity.x, 300, .01, "physical selected velocity retains millimeters per second");
			check.Near(release.selectedReleaseVelocity.x, 300 / ratio, .05, "public release velocity is canonical system DIP per second");
			check.Near(release.selectedReleaseVelocity.y, 400 / (ratio * 1.5), .05, "each public velocity axis uses its own inverse metric");
			check(release.releaseMotionContext.unit == CanvasPanMotionUnit::Millimeters, "release has its own motion unit identity");
			pan.contexts[0].motionPerDipX = std::numeric_limits<double>::quiet_NaN(); // 帧数组可立即被复用。
			const auto terminal = pan.Step(1000);
			const double xMm = terminal.contentDelta.x * ratio, yMm = terminal.contentDelta.y * ratio * 1.5;
			check.Near(std::hypot(xMm, yMm), 109.795269, .002, "same physical release has the same endpoint across DPI/resolution metrics");
			check.Near(xMm / yMm, .75, 1e-5, "anisotropic inverse transform preserves physical direction");
			check(terminal.phase == CanvasPanPhase::Idle, "fling owns its context after caller storage is overwritten");
		}
		AdaptedPan geometry; geometry.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		auto out = geometry.Step(0, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
		out = geometry.Step(8, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 8, 20) });
		check.Near(out.contentDelta.x, 7.5, .0001, "physical slop is deducted once before one-to-one DIP tracking");
		out = geometry.Step(16, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 16, 40) });
		check.Near(out.contentDelta.x, 20, .0001, "held content remains one-to-one after starting physical pan");

		AdaptedPan pair; pair.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		pair.Step(0, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
		out = pair.Step(1, { AdaptedContact(2, CanvasPanSourceClass::Touch, 0, 1, 30, 40, CanvasPanContactPhase::Down) });
		check.Near(out.focus.x, 15, .0001, "anisotropic two-finger focus is public DIP");
		check.Near(out.focus.y, 20, .0001, "two-finger focus restores the y metric independently");
		check.Near(out.span, 25, .0001, "span is recomputed after restoring pointers rather than dividing an average scale");
		pair.Step(10, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 10, 15) });
		out = pair.Step(20, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 20, 30) });
		check.Near(out.pointerDiagnostics[0].velocity.x, 1500, .01, "per-pointer public diagnostics are DIP velocity");
		check.Near(pair.dynamics->ContentVelocity().x, 750, .01, "held velocity is not inverse-transformed twice via Snapshot");
	}

	void TestMotionAdmissionAndMixedFrames(Checks& check)
	{
		Info("motion admission: ordered mixed units, rejected contacts and terminal barriers");
		for (const bool reversePublication : { false, true })
		{
			AdaptedPan pan; pan.contextCount = 2;
			pan.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical, .25, .5);
			pan.contexts[1] = {}; pan.contexts[1].outputKey = 7;
			std::vector<CanvasPanContactSample> samples{
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down),
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 10, 20),
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 20, 40),
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 30, 60),
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 31, 60, 0, CanvasPanContactPhase::Up),
				AdaptedContact(2, CanvasPanSourceClass::Mouse, 1, 32, 100, 0, CanvasPanContactPhase::Down),
				AdaptedContact(2, CanvasPanSourceClass::Mouse, 1, 42, 120) };
			if (reversePublication) std::reverse(samples.begin(), samples.end());
			const auto out = pan.Step(50, samples);
			check.Near(out.contentDelta.x, 55, .0001, "same frame sums 50 DIP physical Touch plus 5 DIP Mouse independently");
			check(out.sourceKey >> 32 == static_cast<uint64_t>(CanvasPanSourceClass::Mouse) &&
				out.releaseSourceKey >> 32 == static_cast<uint64_t>(CanvasPanSourceClass::Touch), "release identity is independent of later accepted Down source");
			check(out.releaseMotionContext.unit == CanvasPanMotionUnit::Millimeters && pan.dynamics->MotionContext().unit == CanvasPanMotionUnit::SystemDip,
				"release and current context do not borrow one another's units");
			check(out.admissionDecisionCount == 2 && pan.decisions[0].accepted && pan.decisions[1].accepted, "ordered admission decisions follow actual QPC order");
		}
		AdaptedPan ties; ties.contextCount = 2;
		ties.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical); ties.contexts[1] = {}; ties.contexts[1].outputKey = 7;
		auto out = ties.Step(40, {
			AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down),
			AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 30, 20, 0, CanvasPanContactPhase::Up),
			AdaptedContact(2, CanvasPanSourceClass::Mouse, 1, 30, 0, 0, CanvasPanContactPhase::Down) });
		check(out.admissionDecisionCount == 2 && !ties.decisions[1].accepted && ties.decisions[1].reason == CanvasPanAdmissionReason::ClassConflict,
			"same QPC Mouse Down precedes Touch Up and remains overlapping");

		for (const bool reversePublication : { false, true })
		{
			AdaptedPan pan; pan.contextCount = 3;
			pan.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
			pan.contexts[1] = MakeCanvasPanMotionContext(CanvasPanMotionProfile::TouchFallback); pan.contexts[1].outputKey = 7;
			pan.contexts[2] = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical);
			std::vector<CanvasPanContactSample> events{
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down),
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 30, 0, 0, CanvasPanContactPhase::Up),
				AdaptedContact(2, CanvasPanSourceClass::Touch, 1, 20, 100, 0, CanvasPanContactPhase::Down),
				AdaptedContact(3, CanvasPanSourceClass::Pen, 2, 40, 0, 0, CanvasPanContactPhase::Down) };
			if (reversePublication) std::reverse(events.begin(), events.end());
			out = pan.Step(50, events);
			check(pan.decisions[0].accepted && !pan.decisions[1].accepted && pan.decisions[2].accepted && out.heldContacts == 1,
				"rejected incompatible Touch cannot bridge an ended gesture and block a later Pen");
		}
		AdaptedPan barrier; barrier.contextCount = 2;
		barrier.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		barrier.contexts[1] = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical);
		barrier.Step(0, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
		barrier.Step(10, { AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 10, 0, 0, CanvasPanContactPhase::Down) });
		barrier.Step(20, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 20, 0, 0, CanvasPanContactPhase::Up) });
		out = barrier.Step(30, { AdaptedContact(3, CanvasPanSourceClass::Pen, 1, 30, 0, 0, CanvasPanContactPhase::Down) });
		check(!barrier.decisions[0].accepted && out.heldContacts == 0, "rejected Pen remains a barrier across frames until its real terminal");
		barrier.Step(40, { AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 40, 0, 0, CanvasPanContactPhase::Up) });
		barrier.Step(50, { AdaptedContact(3, CanvasPanSourceClass::Pen, 1, 50, 0, 0, CanvasPanContactPhase::Cancelled) });
		out = barrier.Step(60, { AdaptedContact(4, CanvasPanSourceClass::Pen, 1, 60, 0, 0, CanvasPanContactPhase::Down) });
		check(barrier.decisions[0].accepted && out.heldContacts == 1, "all true rejected terminals release the Pen barrier");
		barrier.dynamics->Stop();
		barrier.Step(70, { AdaptedContact(5, CanvasPanSourceClass::Touch, 0, 70, 0, 0, CanvasPanContactPhase::Down) });
		barrier.Step(71, { AdaptedContact(6, CanvasPanSourceClass::Pen, 1, 71, 0, 0, CanvasPanContactPhase::Down) });
		barrier.dynamics->Stop(); // 新导航 epoch 必须同时撤销旧拒收身份。
		out = barrier.Step(72, { AdaptedContact(7, CanvasPanSourceClass::Pen, 1, 72, 0, 0, CanvasPanContactPhase::Down) });
		check(barrier.decisions[0].accepted, "Stop/new epoch clears rejected identities instead of permanently suppressing Pen");

		AdaptedPan malformed; malformed.contextCount = 2;
		malformed.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
		malformed.contexts[1] = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical);
		malformed.Step(0, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
		malformed.Step(10, { AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 10, 0, 0, CanvasPanContactPhase::Down) });
		auto badTouchUp = AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 20, std::numeric_limits<double>::quiet_NaN(), 0, CanvasPanContactPhase::Up);
		badTouchUp.qpc = 0;
		out = malformed.Step(20, { badTouchUp });
		check(out.heldContacts == 0 && out.phase == CanvasPanPhase::Idle && out.releaseReason == CanvasPanReleaseReason::TimingRejected,
			"accepted terminal with invalid time and coordinates closes contact without fabricating release speed");
		auto badPenUp = AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 30, std::numeric_limits<double>::infinity(), 0, CanvasPanContactPhase::Cancelled);
		badPenUp.qpc = -1;
		malformed.Step(30, { badPenUp });
		out = malformed.Step(40, { AdaptedContact(3, CanvasPanSourceClass::Pen, 1, 40, 0, 0, CanvasPanContactPhase::Down) });
		check(malformed.decisions[0].accepted && out.heldContacts == 1,
			"rejected Pen terminal retires its barrier even if payload clock or position is invalid");

		for (const int64_t badQpc : { int64_t{0}, QpcMs(5) })
		{
			AdaptedPan sameFrame; sameFrame.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
			auto earlyUp = AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 20, 0, 0, CanvasPanContactPhase::Up); earlyUp.qpc = badQpc;
			out = sameFrame.Step(30, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 10, 0, 0, CanvasPanContactPhase::Down), earlyUp });
			check(out.heldContacts == 0 && out.phase == CanvasPanPhase::Idle && out.releaseReason == CanvasPanReleaseReason::TimingRejected,
				"same-frame terminal sorted before its Down closes identity without leaving held or fling");
			AdaptedPan rejected; rejected.contextCount = 2;
			rejected.contexts[0] = PhysicalContext(CanvasPanMotionProfile::TouchPhysical);
			rejected.contexts[1] = PhysicalContext(CanvasPanMotionProfile::ScreenPenPhysical);
			rejected.Step(0, { AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 0, 0, 0, CanvasPanContactPhase::Down) });
			auto earlyPenUp = AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 20, 0, 0, CanvasPanContactPhase::Cancelled); earlyPenUp.qpc = badQpc;
			rejected.Step(30, { AdaptedContact(2, CanvasPanSourceClass::Pen, 1, 10, 0, 0, CanvasPanContactPhase::Down), earlyPenUp,
				AdaptedContact(1, CanvasPanSourceClass::Touch, 0, 20, 0, 0, CanvasPanContactPhase::Up) });
			out = rejected.Step(40, { AdaptedContact(3, CanvasPanSourceClass::Pen, 1, 40, 0, 0, CanvasPanContactPhase::Down) });
			check(rejected.decisions[0].accepted, "early malformed terminal also prevents a same-frame rejected Pen barrier from lingering");
		}

		AdaptedPan many; std::vector<CanvasPanContactSample> samples;
		for (int n = 0; n < 40; ++n)
		{
			samples.push_back(AdaptedContact(n + 1, CanvasPanSourceClass::Mouse, 0, n * 2, 0, 0, CanvasPanContactPhase::Down));
			samples.push_back(AdaptedContact(n + 1, CanvasPanSourceClass::Mouse, 0, n * 2 + 1, 0, 0, CanvasPanContactPhase::Up));
		}
		out = many.Step(100, samples);
		check(out.admissionDecisionCount == 40 && out.heldContacts == 0 &&
			std::all_of(many.decisions.begin(), many.decisions.begin() + 40, [](const auto& d) { return d.accepted; }),
			"one frame reports more than sixteen sequential Down admissions without truncation");
		AdaptedPan shortage; CanvasPanFrameInput insufficient;
		const std::array one{ AdaptedContact(1, CanvasPanSourceClass::Mouse, 0, 0, 0, 0, CanvasPanContactPhase::Down) };
		insufficient.frameQpc = QpcMs(1); insufficient.qpcFrequency = kFreq; insufficient.contacts = one;
		insufficient.motionContexts = std::span(shortage.contexts).first(1);
		out = shortage.dynamics->Step(insufficient);
		check(out.admissionFailed && out.heldContacts == 0, "decision shortage fails before installing any contact or changing frame time");
		out = shortage.Step(1, one);
		check(shortage.decisions[0].accepted && out.heldContacts == 1, "same frame can be retried with sufficient admission storage");
	}

	void TestWhiteboardPatternGeometry(Checks& check)
	{
		const WhiteboardTheme light{ 0xF5F1E6, WhiteboardPattern::Dots };
		const WhiteboardTheme dark{ 0x123B32, WhiteboardPattern::Grid };
		const WhiteboardTheme blank{ 0x123B32, WhiteboardPattern::Blank };
		const WhiteboardTheme invalid{ 0x123B32, static_cast<WhiteboardPattern>(9) };
		// 门控：桌面/PPT、Blank、非法主题、非法 DPI 一律不产生纹理（画布空时必须真透明）。
		check(!ResolveWhiteboardPatternForFrame(false, light, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: desktop/presentation");
		check(!ResolveWhiteboardPatternForFrame(true, blank, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: blank");
		check(!ResolveWhiteboardPatternForFrame(true, invalid, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: invalid theme");
		check(!ResolveWhiteboardPatternForFrame(true, light, 0, 0, 0, 1080, 1.0f).Enabled(), "pattern gate: empty canvas");
		check(!ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, std::numeric_limits<float>::quiet_NaN()).Enabled(), "pattern gate: NaN DPI");
		check(!ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, 0.1f).Enabled(), "pattern gate: absurdly small DPI scale");
		check(ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: whiteboard dots");
		check(EnumeratePattern(ResolveWhiteboardPatternForFrame(false, light, 0, 0, 1920, 1080, 1.0f), 0, 0, 1920, 1080).empty(),
			"a disabled pattern emits no primitive");

		// 颜色/尺寸：浅色背景黑纹理、深色背景白纹理；24 DIP 间距，点半径 1 DIP，线宽 1 DIP。
		const auto lightDots = ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, 1.5f);
		const auto darkGrid = ResolveWhiteboardPatternForFrame(true, dark, 0, 0, 1920, 1080, 1.5f);
		check(lightDots.shade == 0.0f && darkGrid.shade == 1.0f, "pattern shade follows the background luminance");
		check(lightDots.stepPx == 36.0 && lightDots.dotRadiusPx == 1.5f && darkGrid.lineHalfWidthPx == 0.75f,
			"pattern step/radius/line width scale with DPI");
		check(kWhiteboardPatternAlpha == 0.18f, "pattern alpha is 18%");

		// 相位公式：phase = (W/2 - viewport) mod step，用 double 避免 ±1048576 的 float 精度问题。
		check(WhiteboardPatternPhase(960.0, 0.0, 24.0) == 0.0, "phase at viewport 0 starts at the screen centre lattice");
		check(WhiteboardPatternPhase(960.0, 13.25, 24.0) == 10.75, "phase follows a fractional viewport");
		check(WhiteboardPatternPhase(960.0, -13.25, 24.0) == 13.25, "phase follows a negative viewport");
		for (const double viewport : { 1048576.0, -1048576.0, 1048575.875, -3.5, 24.0, 0.0 })
		{
			const double phase = WhiteboardPatternPhase(540.0, viewport, 36.0);
			check(phase >= 0.0 && phase < 36.0, "phase stays in [0, step)");
			check(phase == WhiteboardPatternPhase(540.0, viewport - 36.0 * 1000.0, 36.0), "phase is periodic in the step");
		}
		check(WhiteboardPatternPhase(960.0, std::numeric_limits<double>::infinity(), 24.0) == 0.0, "non-finite viewport falls back to phase 0");

		// 4K 图元数（设计 §4：约 14.6k/6.5k/3.7k 点，约 250/167/125 条网格线）。
		const auto count = [&](WhiteboardPattern kind, float dpi) {
			return EnumeratePattern(ResolveWhiteboardPatternForFrame(true, WhiteboardTheme{ 0x123B32, kind },
				0, 0, 3840, 2160, dpi), 0, 0, 3840, 2160).size();
		};
		check(count(WhiteboardPattern::Dots, 1.0f) >= 14000 && count(WhiteboardPattern::Dots, 1.0f) <= 15500, "4K 100% dot primitive count");
		check(count(WhiteboardPattern::Dots, 1.5f) >= 6200 && count(WhiteboardPattern::Dots, 1.5f) <= 7200, "4K 150% dot primitive count");
		check(count(WhiteboardPattern::Dots, 2.0f) >= 3500 && count(WhiteboardPattern::Dots, 2.0f) <= 4200, "4K 200% dot primitive count");
		check(count(WhiteboardPattern::Grid, 1.0f) >= 248 && count(WhiteboardPattern::Grid, 1.0f) <= 262, "4K 100% grid line count");
		check(count(WhiteboardPattern::Grid, 2.0f) >= 120 && count(WhiteboardPattern::Grid, 2.0f) <= 134, "4K 200% grid line count");

		// 几何形状：点是零长度线；网格线轴对齐且向两端越过矩形。
		for (const auto& primitive : EnumeratePattern(lightDots, 0, 0, 200, 150))
			check(primitive[0] == primitive[2] && primitive[1] == primitive[3] && primitive[4] == 1.5f, "dot is a zero-length line with radius 1 DIP");
		bool gridShapeOk = true;
		for (const auto& primitive : EnumeratePattern(darkGrid, 100, 50, 300, 250))
		{
			const bool vertical = primitive[0] == primitive[2];
			const bool horizontal = primitive[1] == primitive[3];
			gridShapeOk = gridShapeOk && (vertical != horizontal) && primitive[4] == 0.75f &&
				(vertical ? primitive[1] < 0.0f && primitive[3] > 1080.0f : primitive[0] < 0.0f && primitive[2] > 1920.0f);
		}
		check(gridShapeOk, "grid lines are axis-aligned and span the whole canvas with cap/AA margin");

		// 分块一致：每个分块发出的图元逐位等于整帧同一序号的图元（网格线按整幅画布长度发出，与 dirty 无关），
		// 且整帧中与分块相交的图元不会遗漏。
		for (const WhiteboardPattern kind : { WhiteboardPattern::Dots, WhiteboardPattern::Grid })
		{
			const auto params = ResolveWhiteboardPatternForFrame(true, WhiteboardTheme{ 0xF5F1E6, kind },
				-137.25, 61.5, 640, 360, 1.25f);
			const auto full = EnumeratePattern(params, 0, 0, 640, 360);
			bool subsetOk = true, coveredOk = true;
			for (int top = 0; top < 360; top += 53)
				for (int left = 0; left < 640; left += 97)
				{
					const int right = (std::min)(left + 97, 640), bottom = (std::min)(top + 53, 360);
					const auto tile = EnumeratePattern(params, left, top, right, bottom);
					for (const auto& primitive : tile)
						subsetOk = subsetOk && std::find(full.begin(), full.end(), primitive) != full.end();
					for (const auto& primitive : full)
					{
						const double halfWidth = primitive[4];
						const bool intersects = kind == WhiteboardPattern::Dots
							? primitive[0] + halfWidth > left && primitive[0] - halfWidth < right &&
								primitive[1] + halfWidth > top && primitive[1] - halfWidth < bottom
							: (primitive[0] == primitive[2]
								? primitive[0] + halfWidth > left && primitive[0] - halfWidth < right
								: primitive[1] + halfWidth > top && primitive[1] - halfWidth < bottom);
						if (intersects)
							coveredOk = coveredOk && std::find(tile.begin(), tile.end(), primitive) != tile.end();
					}
				}
			check(subsetOk, "tile primitives are bit-identical to the full-frame primitives");
			check(coveredOk, "every full-frame primitive touching a tile is emitted for that tile");
		}

		// 外观键：只含样式/明暗/间距；相位（视口平移）不入键，主题或 DPI 变化才触发整屏重合成。
		const auto keyAt = [&](WhiteboardPattern kind, std::uint32_t rgb, float dpi, double viewport) {
			return MakeWhiteboardPatternKey(ResolveWhiteboardPatternForFrame(true,
				WhiteboardTheme{ rgb, kind }, viewport, viewport, 1920, 1080, dpi));
		};
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) == keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 777.5),
			"pattern key ignores the viewport phase");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) == keyAt(WhiteboardPattern::Dots, 0x172A46, 1.0f, 0.0),
			"pattern key ignores the colour when the shade is unchanged");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) != keyAt(WhiteboardPattern::Grid, 0x123B32, 1.0f, 0.0), "pattern key: dots vs grid");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) != keyAt(WhiteboardPattern::Dots, 0xFFFFFF, 1.0f, 0.0), "pattern key: dark vs light shade");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) != keyAt(WhiteboardPattern::Dots, 0x123B32, 1.5f, 0.0), "pattern key: DPI");
		check(keyAt(WhiteboardPattern::Blank, 0x123B32, 1.0f, 0.0) == WhiteboardPatternKey{}, "pattern key: blank equals the disabled key");
	}
}

namespace
{
	// Freeze 归属下的点阵/网格相位跟随：无锁视口通道、相位数学、重画规划、页身份过期、归属开关门控与诊断聚合。
	void TestWhiteboardFreezePatternFollow(Checks& check)
	{
		namespace Follow = Inkeys::UI::WhiteboardFollow;
		using Sample = Follow::Sample;
		const auto sample = [](bool active, bool moving, std::uint32_t page, std::uint64_t generation, float x, float y) {
			Sample value;
			value.active = active; value.moving = moving; value.page = page;
			value.generation = generation; value.x = x; value.y = y;
			return value;
		};

		// ---- 归属开关：单点、互斥、Draw3 侧门控由同一纯函数决定 ----
		{
			using Inkeys::Drawing::Draw3::WhiteboardPatternOwner;
			check(kWhiteboardPatternOwnedByFreeze != kWhiteboardPatternOwnedByDraw3, "pattern owner constants are exclusive");
			check(WhiteboardPatternDrawnByDraw3(true) == kWhiteboardPatternOwnedByDraw3, "default gate follows the owner constant");
			check(!WhiteboardPatternDrawnByDraw3(false) && !WhiteboardPatternDrawnByDraw3(false, WhiteboardPatternOwner::Draw3),
				"desktop and presentation never draw the Draw3 pattern");
			check(!WhiteboardPatternDrawnByDraw3(true, WhiteboardPatternOwner::Freeze) &&
				WhiteboardPatternDrawnByDraw3(true, WhiteboardPatternOwner::Draw3), "explicit owners select the Draw3 pass");
			check(WhiteboardPatternDrawnByFreeze(WhiteboardPatternOwner::Freeze) && !WhiteboardPatternDrawnByFreeze(WhiteboardPatternOwner::Draw3),
				"explicit owners select the Freeze pattern");
			// 控制器 syncWhiteboardPattern 的组合：Freeze 归属下参数恒为未启用，外观键为空，不会触发 forceFullPresent。
			const WhiteboardTheme dots{ 0x123B32, WhiteboardPattern::Dots };
			for (const WhiteboardPatternOwner owner : { WhiteboardPatternOwner::Freeze, WhiteboardPatternOwner::Draw3 })
			{
				const auto params = ResolveWhiteboardPatternForFrame(WhiteboardPatternDrawnByDraw3(true, owner), dots, 0.0, 0.0, 1920, 1080, 1.0f);
				check(params.Enabled() == (owner == WhiteboardPatternOwner::Draw3), "Draw3 pattern params exist only under Draw3 ownership");
				check((MakeWhiteboardPatternKey(params) == WhiteboardPatternKey{}) == (owner == WhiteboardPatternOwner::Freeze),
					"Freeze ownership keeps the Draw3 appearance key empty");
			}
		}

		// ---- 通道：往返、空通道、位级保持 ----
		{
			Follow::ViewportChannel channel;
			Sample read;
			check(channel.Read(read) == Follow::ReadResult::Empty, "empty channel reports Empty");
			Sample value = sample(true, true, 7, 0x1234567890ABCDEFull, -1048576.0f, 1048575.5f);
			value.serial = 99; value.qpc = -123456789012345LL;
			channel.Write(value);
			check(channel.Read(read) == Follow::ReadResult::Ok && read.active && read.moving && read.page == 7 &&
				read.generation == value.generation && read.x == value.x && read.y == value.y &&
				read.serial == 99 && read.qpc == value.qpc, "channel round-trips every field");
			value = sample(false, false, 0xFFFFFFFFu, 1, std::numeric_limits<float>::quiet_NaN(), -0.0f);
			channel.Write(value);
			check(channel.Read(read) == Follow::ReadResult::Ok && !read.active && !read.moving && read.page == 0xFFFFFFFFu &&
				std::isnan(read.x) && std::signbit(read.y), "channel keeps NaN and signed zero bit-exactly");
		}

		// ---- seqlock 撕裂压力：每个样本满足不变量，读端永不见到混合样本 ----
		{
			Follow::ViewportChannel channel;
			std::atomic<bool> done{ false };
			constexpr std::uint64_t kWrites = 2'000'000;
			std::thread writer([&] {
				for (std::uint64_t n = 1; n <= kWrites; ++n)
				{
					Sample value;
					value.active = true;
					value.moving = (n & 1) != 0;
					value.generation = n;
					value.page = static_cast<std::uint32_t>(n);
					value.x = static_cast<float>(n % 100000);
					value.y = -static_cast<float>(n % 100000);
					value.serial = n * 3;
					value.qpc = static_cast<std::int64_t>(n * 7);
					channel.Write(value);
				}
				done.store(true, std::memory_order_release);
			});
			std::uint64_t reads = 0, torn = 0, contended = 0, lastGeneration = 0, regressions = 0;
			while (!done.load(std::memory_order_acquire) || reads < 1'000'000)
			{
				Sample read;
				const auto result = channel.Read(read);
				if (result == Follow::ReadResult::Empty) continue;
				if (result == Follow::ReadResult::Contended) { ++contended; continue; }
				++reads;
				const std::uint64_t n = read.generation;
				const bool consistent = read.page == static_cast<std::uint32_t>(n) && read.serial == n * 3 &&
					read.qpc == static_cast<std::int64_t>(n * 7) && read.x == static_cast<float>(n % 100000) &&
					read.y == -static_cast<float>(n % 100000) && read.moving == ((n & 1) != 0) && read.active;
				if (!consistent) ++torn;
				if (n < lastGeneration) ++regressions;
				lastGeneration = n;
				if (done.load(std::memory_order_acquire) && reads >= 1'000'000) break;
			}
			writer.join();
			Info("seqlock stress reads=" + std::to_string(reads) + " contended=" + std::to_string(contended) +
				" torn=" + std::to_string(torn));
			check(reads >= 1'000'000, "seqlock stress performed at least one million consistent reads");
			check(torn == 0, "seqlock never exposes a torn sample");
			check(regressions == 0, "seqlock samples never move backwards");
			Sample finalRead;
			check(channel.Read(finalRead) == Follow::ReadResult::Ok && finalRead.generation == kWrites, "final sample is the last write");
		}

		// ---- 相位数学：与 Draw3 后备路径同源，视口 (0,0) 等于旧 Freeze 相位 ----
		{
			for (const float scale : { 1.0f, 1.1f, 1.25f, 1.5f, 2.0f })
			{
				const double step = 24.0 * scale;
				const Follow::Phase zero = Follow::ResolvePhase(1920.0, 1080.0, scale, 0.0, 0.0);
				check(zero.xPx == std::fmod(960.0, step) && zero.yPx == std::fmod(540.0, step), "phase at viewport zero is fmod(W/2, step)");
				check(zero.xDip == zero.xPx / scale && zero.yDip == zero.yPx / scale, "DIP phase is the pixel phase over the scale");
				for (const std::pair<double, double> viewport : { std::pair<double, double>{ 13.25, -7.5 }, { -1048576.0, 1048576.0 },
					{ 1048575.5, -0.125 }, { 0.0, 1e-9 }, { -step, step * 1000.0 } })
				{
					const Follow::Phase phase = Follow::ResolvePhase(1920.0, 1080.0, scale, viewport.first, viewport.second);
					check(phase.xPx >= 0.0 && phase.xPx < step && phase.yPx >= 0.0 && phase.yPx < step, "phase stays inside one period");
					const auto draw3 = MakeWhiteboardPatternParams(WhiteboardTheme{ 0x123B32, WhiteboardPattern::Dots },
						viewport.first, viewport.second, 1920, 1080, scale);
					check(phase.xPx == draw3.phaseX && phase.yPx == draw3.phaseY, "Freeze phase equals the Draw3 fallback phase");
					const Follow::Phase shifted = Follow::ResolvePhase(1920.0, 1080.0, scale, viewport.first + step * 4.0, viewport.second - step * 6.0);
					check(std::fabs(shifted.xPx - phase.xPx) < 1e-6 || std::fabs(std::fabs(shifted.xPx - phase.xPx) - step) < 1e-6,
						"phase is periodic in the viewport");
				}
			}
			check(Follow::ResolvePhase(1920.0, 1080.0, 0.0f, 1.0, 2.0) == Follow::Phase{}, "invalid scale gives a zero phase");
			check(Follow::CircularDistance(23.99, 0.01, 24.0) < 0.03 && Follow::CircularDistance(5.0, 5.0, 24.0) == 0.0, "circular phase distance wraps");
		}

		// ---- 规划：最新者胜出、运动期 ε 合并、落定帧精确、页身份过期、Blank 不画 ----
		{
			constexpr double W = 1920.0, H = 1080.0;
			Follow::State state;
			const std::uint64_t known = 5;
			const auto plan = [&](const Sample& s, std::uint64_t knownGeneration = 5, bool enabled = true, float scale = 1.0f) {
				return Follow::PlanFollow(s, knownGeneration, state, W, H, scale, enabled);
			};
			// 从未画过：首帧必画，且空样本使用视口 (0,0) 的相位（与改动前像素一致）。
			Follow::Plan first = plan(sample(false, false, 0, 0, 0, 0));
			check(!first.usable && !first.draw && first.phase == Follow::ResolvePhase(W, H, 1.0f, 0.0, 0.0), "unusable sample falls back to the viewport-zero phase");
			first = plan(sample(true, false, 0, 5, 0.0f, 0.0f));
			check(first.usable && first.draw, "first usable sample always draws");
			state.drawnValid = true; state.drawn = first.phase;
			// 页身份：更老的 generation 属于上一段会话/上一页，绝不能画到当前页上。
			check(!plan(sample(true, false, 2, 4, 300.0f, 0.0f), known).usable && !plan(sample(true, false, 2, 4, 300.0f, 0.0f), known).draw,
				"sample from an older generation is rejected");
			check(plan(sample(true, false, 2, 5, 300.0f, 0.0f), known).draw, "sample with the known generation is accepted");
			check(plan(sample(true, false, 9, 6, 300.0f, 0.0f), known).draw, "newer generation arriving before the page snapshot is accepted");
			check(plan(sample(true, false, 3, 5, 300.0f, 0.0f), known).usable, "same generation on another page is still accepted");
			check(!plan(sample(false, false, 0, 9, 300.0f, 0.0f), known).usable, "inactive sample is never used");
			check(!plan(sample(true, false, 0, 9, std::numeric_limits<float>::infinity(), 0.0f), known).usable &&
				!plan(sample(true, false, 0, 9, 0.0f, std::numeric_limits<float>::quiet_NaN()), known).usable, "non-finite viewport is rejected");
			check(plan(sample(false, false, 0, 9, 300.0f, 0.0f), known).phase == state.drawn, "rejected sample keeps the drawn phase");
			// ε：运动期亚阈值位移合并，落定帧精确重画。
			check(!plan(sample(true, true, 0, 5, 0.2f, 0.0f)).draw, "moving sub-epsilon shift is coalesced");
			check(plan(sample(true, true, 0, 5, 0.3f, 0.0f)).draw, "moving shift above epsilon redraws");
			check(plan(sample(true, false, 0, 5, 0.2f, 0.0f)).draw, "settled frame redraws even for a sub-epsilon difference");
			check(!plan(sample(true, false, 0, 5, 0.0f, 0.0f)).draw, "settled frame equal to the drawn phase does not redraw");
			check(!plan(sample(true, true, 0, 5, 24.0f * 7.0f, 24.0f * -3.0f)).draw, "whole-period shift needs no redraw");
			state.drawn = Follow::ResolvePhase(W, H, 1.0f, 0.01, 0.0); // 相位约 23.99
			check(!plan(sample(true, true, 0, 5, -0.01f, 0.0f)).draw, "phase wrap 23.99 -> 0.01 counts as a tiny move");
			state.drawn = first.phase;
			check(!plan(sample(true, true, 0, 5, 500.0f, 0.0f), 5, false).draw && plan(sample(true, true, 0, 5, 500.0f, 0.0f), 5, false).usable,
				"Blank pattern never draws but still resolves the phase");
			check(!plan(sample(true, true, 0, 5, 500.0f, 0.0f), 5, true, 0.1f).draw, "out-of-range DPI scale is not drawn");
		}

		// ---- 合成回放：多样本合并为最新者；运动期少画；落定帧精确且之后静止 ----
		{
			constexpr double W = 1920.0, H = 1080.0;
			Follow::State state;
			std::uint64_t serial = 0, draws = 0;
			const std::uint64_t generation = 3;
			Sample latest;
			double viewport = 0.0;
			for (int callbackIndex = 0; callbackIndex < 60; ++callbackIndex)
			{
				// 每个回调之间 Draw3 发布 8 个样本（约 480 Hz）；只有最后一个会被读到。
				for (int n = 0; n < 8; ++n) { viewport += 3.7; latest = sample(true, true, 0, generation, static_cast<float>(viewport), 0.0f); latest.serial = ++serial; }
				const Follow::Plan p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
				if (p.draw) { state.drawnValid = true; state.drawn = p.phase; state.drawnSerial = latest.serial; ++draws; }
			}
			check(draws == 60, "one redraw per callback no matter how many samples were published in between");
			check(state.drawnSerial == serial, "the redraw uses the latest published sample");
			// 惰性尾巴：运动中但位移 <ε 的样本不触发重画，随后落定样本精确重画一次。
			const std::uint64_t before = draws;
			latest = sample(true, true, 0, generation, static_cast<float>(viewport + 0.1), 0.0f); latest.serial = ++serial;
			Follow::Plan p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
			check(!p.draw, "inertia tail below epsilon does not redraw");
			latest = sample(true, false, 0, generation, static_cast<float>(viewport + 0.1), 0.0f); latest.serial = ++serial;
			p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
			check(p.draw && p.phase == Follow::ResolvePhase(W, H, 1.0f, static_cast<float>(viewport + 0.1), 0.0), "settle redraws at the exact final phase");
			state.drawnValid = true; state.drawn = p.phase; state.drawnSerial = latest.serial; ++draws;
			p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
			check(!p.draw && draws == before + 1, "after the settled frame the plan is idle");
		}

		// ---- 唤醒门控与发布去重 ----
		{
			check(!Follow::ShouldWakeFreeze(0, true, true), "Blank pattern never wakes Freeze");
			check(!Follow::ShouldWakeFreeze(1, false, true) && !Follow::ShouldWakeFreeze(2, true, false), "inactive whiteboard or inactive sample never wakes Freeze");
			check(Follow::ShouldWakeFreeze(1, true, true) && Follow::ShouldWakeFreeze(2, true, true), "dots and grid wake Freeze");
			// 退休规则：Freeze 归属下纹理类型变化退休像素；Draw3 归属下只有底色变化才退休。视口相位不在此规则内。
			check(Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x123B32, 2, true), "Freeze ownership: pattern-kind change retires pixels");
			check(!Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x123B32, 2, false), "Draw3 ownership: pattern-kind change does not retire pixels");
			check(Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x172A46, 1, true) && Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x172A46, 1, false),
				"RGB change retires pixels under either owner");
			check(!Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x123B32, 1, true), "identical theme never retires pixels");
			const Sample base = sample(true, true, 2, 5, 10.0f, 20.0f);
			check(!WhiteboardViewportSampleChanged(base, base), "identical sample is deduplicated");
			Sample other = base; other.serial = 42; other.qpc = 99;
			check(!WhiteboardViewportSampleChanged(base, other), "serial and qpc do not affect deduplication");
			for (int field = 0; field < 5; ++field)
			{
				other = base;
				if (field == 0) other.moving = false;
				if (field == 1) other.page = 3;
				if (field == 2) other.generation = 6;
				if (field == 3) other.x = 10.5f;
				if (field == 4) other.y = 19.0f;
				check(WhiteboardViewportSampleChanged(base, other), "each identity or position field changes the sample");
			}
			check(WhiteboardViewportSampleChanged(base, sample(false, false, 0, 0, 0, 0)), "leaving the whiteboard publishes once");
			check(!WhiteboardViewportSampleChanged(sample(false, false, 0, 0, 0, 0), sample(false, true, 4, 9, 7.0f, 7.0f)),
				"inactive samples are equivalent regardless of stale fields");
		}

		// ---- 诊断聚合（M1/M2）：固定容量，无分配 ----
		{
			Follow::Profile profile;
			check(profile.Empty() && profile.Summarize().updates == 0, "empty profile summarises to zero");
			for (int n = 0; n < 101; ++n) profile.Record(1000.0 + n * 16.0, n % 3, 20.0 + (n % 5), 4.0 * (n % 7), 8.0);
			profile.RecordAck(2.0); profile.RecordAck(4.0);
			profile.RecordSettle(33.5);
			const auto summary = profile.Summarize();
			check(summary.updates == 101 && summary.dropped == 0, "profile counts updates");
			check(std::fabs(summary.segmentMs - 1600.0) < 1e-9 && std::fabs(summary.updatesPerSecond - 62.5) < 1e-9, "profile reports updates per second");
			check(std::fabs(summary.gapP50 - 16.0) < 1e-9 && std::fabs(summary.gapMax - 16.0) < 1e-9, "profile reports update gaps");
			check(summary.lagFramesMax == 2.0 && summary.lagMsMax == 24.0 && summary.lagPxMax == 24.0 && summary.costMsP50 == 8.0, "profile reports lag and cost");
			check(summary.ackSamples == 2 && summary.ackLagFramesMax == 4.0 && summary.settleMs == 33.5, "profile reports ack lag and settle latency");
			for (int n = 0; n < 600; ++n) profile.Record(5000.0 + n, 0, 0, 0, 0);
			check(profile.Summarize().updates == Follow::Profile::kCapacity && profile.Summarize().dropped > 0, "profile drops instead of allocating past capacity");
			profile.Reset();
			check(profile.Empty(), "profile resets for the next pan segment");
		}
	}
}

int RunDraw3CanvasPanTests()
{
	Checks check;
	TestFlingBooster(check);
	TestFlingController(check);
	TestGestureDispatchAndSlop(check);
	TestPointerTopology(check);
	TestFrameEventOrdering(check);
	TestOrderedInputEquivalence(check);
	TestCancelPenAndStop(check);
	TestInvalidSamplesAndTranslation(check);
	TestReliableProductRelease(check);
	TestProductFrictionAndResidual(check);
	TestMotionProfilesAndFriction(check);
	TestTouchReleaseHandoffDecision(check);
	TestTouchSlowScrollAndPresentation(check);
	TestTouchRepairRecordedDecisions(check);
	TestTouchDecisionBudgetSafety(check);
	TestSameFrameTouchReleaseMetadata(check);
	TestTouchGroupVelocityAndBudgetTags(check);
	TestTouchProgressEvidence(check);
	TestTouchRecordedTerminalPlatforms(check);
	TestTouchProgressBoundaries(check);
	TestOverspeedHistoryAndReleaseQuality(check);
	TestQualityLimitedContinuation(check);
	TestMotionReleaseQuality(check);
	TestScreenPenArrivalTiming(check);
	TestPhysicalMotionCanonicalOutput(check);
	TestMotionAdmissionAndMixedFrames(check);
	TestPerSourceInertiaGate(check);
	TestWhiteboardPatternGeometry(check);
	TestWhiteboardFreezePatternFollow(check);
	if (check.failures == 0)
		std::cout << "PASS Draw3 product canvas pan, whiteboard pattern geometry and Freeze pattern follow\n";
	return check.failures;
}
