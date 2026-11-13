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
		check(add(21, 60) == CanvasPanSampleDisposition::TimingRejected && !tracker.Estimate(1, kFreq).valid &&
			tracker.Reason(1, kFreq) == CanvasPanReleaseReason::TimingRejected,
			"compressed burst resets speed evidence instead of launching capped seventeen-thousand velocity");
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
		check(add(21, 60) == CanvasPanSampleDisposition::TimingRejected,
			"quantization allowance still rejects the real thirty-DIP one-ms compression");

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
	TestPerSourceInertiaGate(check);
	TestWhiteboardPatternGeometry(check);
	TestWhiteboardFreezePatternFollow(check);
	if (check.failures == 0)
		std::cout << "PASS Draw3 product canvas pan, whiteboard pattern geometry and Freeze pattern follow\n";
	return check.failures;
}
