#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <fstream>
#include <sstream>
#include <json/json.h>

#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDockTrace.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.DirtyRegion.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h"

namespace
{
	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	bool Near(double lhs, double rhs, double epsilon = 0.000001)
	{
		return std::abs(lhs - rhs) <= epsilon;
	}

	void TestRealVerticalGrabReplay()
	{
		using namespace Inkeys::UI::Bar;
		struct Sample { int frame; double q, e, c, oldError; bool docked; };
		const Sample samples[]{
			{ 138, 0.40625, -1.5, 0.0, 1.2222222222, true },
			{ 354, 0.40625, -20.0, 0.0, 16.2962962963, true },
			{ 776, 0.46875, -2.0, -25.5, -22.049382716, true },
			{ 1216, 0.46875, 23.36698, -18.9872, -39.7397, false },
		};
		for (const auto& sample : samples)
		{
			constexpr double top = 831.0, bottom = 912.0, root = 871.5, zoom = 2.0;
			const double logical = root + (sample.q - 0.5) * 80.0;
			const double target = logical + (sample.docked ? sample.e : 0.0);
			const auto old = sample.docked
				? ResolveBarBottomDockVerticalMapping(top, bottom, sample.e, sample.c, true)
				: ResolveBarBottomDockRecoveringVerticalMapping(top, bottom, sample.e, sample.c, true);
			Check(Near((old.MapY(logical) - target) * zoom, sample.oldError, 0.001),
				"frozen real frames reproduce the former actual-grab error independently of window latency");
			BarBottomDockVerticalFrameInput input;
			input.mode = sample.docked ? BarBottomDockMode::BottomDocked : BarBottomDockMode::Floating;
			input.dragging = input.anchorValid = true;
			input.baseTop = top; input.baseBottom = input.targetBottom = bottom;
			input.logicalGrab = logical; input.effectiveGrab = target;
			input.minimumY = 0.0; input.maximumY = 1080.0;
			BarBottomDockSpringState grip{}, shape{ sample.c, 0.0 };
			if (!sample.docked)
			{
				input.handoff = true;
				input.previousShape = ResolveBarBottomDockShiftedShape(old, root, 80.0, sample.q, zoom, target, zoom);
			}
			const auto next = AdvanceBarBottomDockVerticalFrame(grip, shape, input);
			Check(Near(next.geometry.mapping.MapY(logical), target) && !next.geometry.constrained,
				"real-frame replay holds the actual normalized local grab point");
			if (sample.docked)
				Check(Near(next.geometry.mapping.visualBottomDip, bottom + sample.c),
					"actual-grab solve retains the independently supplied dock/capture bottom");
			const double outset = ResolveBarBottomDockVerticalOutset(input, grip, shape, next.geometry.mapping);
			Check(outset >= std::abs(next.geometry.mapping.visualTopDip - top)
				&& outset >= std::abs(next.geometry.mapping.visualBottomDip - bottom),
				"viewport reservation contains solved endpoints beyond the old twenty-four-DIP input limit");
		}

		// g2 f776：旧底端1773、旧实际抓点1687，本次指针1734，正常本次底端应为1820。
		const auto previous = ResolveBarBottomDockRecoveringVerticalMapping(805.5, 886.5, 0.0);
		const auto shifted = ResolveBarBottomDockShiftedShape(previous, 846.0, 80.0, 0.46875, 2.0, 867.0, 2.0);
		Check(Near(shifted.bottomDip * 2.0, 1820.0),
			"f776 moves the last successful shape by the current pointer movement before seeding capture");
		BarBottomDockVerticalFrameInput capture;
		capture.mode = BarBottomDockMode::BottomDocked;
		capture.dragging = capture.anchorValid = capture.handoff = true;
		capture.baseTop = 831.0; capture.baseBottom = capture.targetBottom = 912.0;
		capture.logicalGrab = 869.0; capture.effectiveGrab = 867.0;
		capture.minimumY = 0.0; capture.maximumY = 1080.0; capture.dtSeconds = 1.0 / 60.0;
		capture.previousShape = shifted;
		BarBottomDockSpringState grip{ -20.0, -1113.0 }, shape{ -25.5, 500.0 };
		auto first = AdvanceBarBottomDockVerticalFrame(grip, shape, capture);
		Check(first.seeded && Near(shape.positionDip, -2.0)
			&& Near(grip.velocityDipPerSecond, 0.0) && Near(shape.velocityDipPerSecond, 0.0)
			&& Near(first.geometry.mapping.visualTopDip, shifted.topDip)
			&& Near(first.geometry.mapping.visualBottomDip, shifted.bottomDip),
			"first capture preserves moved successful shape and discards cross-coordinate velocity without consuming dt");
		capture.handoff = false;
		(void)AdvanceBarBottomDockVerticalFrame(grip, shape, capture);
		capture.handoff = true; // 候选未成功：同一成功图像仍是下一次交接来源。
		auto retry = AdvanceBarBottomDockVerticalFrame(grip, shape, capture);
		Check(Near(retry.geometry.mapping.visualBottomDip, shifted.bottomDip) && Near(shape.positionDip, -2.0),
			"failed or superseded capture candidate cannot consume the successful-shape handoff");
		capture.handoff = false;
		for (int i = 0; i < 180; ++i) first = AdvanceBarBottomDockVerticalFrame(grip, shape, capture);
		Check(Near(first.geometry.mapping.visualBottomDip * 2.0, 1824.0)
			&& Near(first.geometry.mapping.MapY(869.0) * 2.0, 1734.0),
			"capture settles at real dock line while the same actual point remains held");

		// f1215/1216 的旧速度会先冲到24；新 Free 恢复只接住成功高度，不继承该速度。
		const auto held = ResolveBarBottomDockAnchoredMapping(831.0, 912.0, 869.0, 889.0,
			912.0, 81.0, true, 0.0, 1080.0).mapping;
		BarBottomDockSpringState oldVelocity{ 20.0, 999.7 };
		(void)AdvanceBarBottomDockSpring(oldVelocity, 0.0, 1.0 / 60.0, true);
		Check(oldVelocity.positionDip > 20.0, "f1216 legacy recovery reproduces the false outward velocity kick");
		BarBottomDockVerticalFrameInput detach = capture;
		detach.mode = BarBottomDockMode::Floating; detach.handoff = true; detach.effectiveGrab = 869.0;
		detach.previousShape = ResolveBarBottomDockShiftedShape(held, 871.5, 80.0, 0.46875, 2.0, 869.0, 2.0);
		grip = { 20.0, 999.7 }; shape = { -18.9872, 600.0 };
		const auto detached = AdvanceBarBottomDockVerticalFrame(grip, shape, detach);
		const double seededHeight = shape.positionDip;
		Check(Near(detached.geometry.mapping.visualTopDip, detach.previousShape.topDip)
			&& Near(detached.geometry.mapping.visualBottomDip, detach.previousShape.bottomDip)
			&& Near(detached.geometry.mapping.MapY(869.0), 869.0)
			&& Near(grip.velocityDipPerSecond, 0.0) && Near(shape.velocityDipPerSecond, 0.0),
			"fast detach preserves the moved successful shape and the actual grab rather than clipped raw input");
		detach.handoff = false;
		const auto recovery = AdvanceBarBottomDockVerticalFrame(grip, shape, detach);
		Check(std::abs(shape.positionDip - seededHeight) < 3.0,
			"first free recovery frame has no twenty-four-DIP shape clamp");
		capture.handoff = true;
		capture.previousShape = ResolveBarBottomDockShiftedShape(recovery.geometry.mapping,
			871.5, 80.0, 0.46875, 2.0, 867.0, 2.0);
		const auto recaptured = AdvanceBarBottomDockVerticalFrame(grip, shape, capture);
		Check(Near(recaptured.geometry.mapping.visualTopDip, capture.previousShape.topDip)
			&& Near(recaptured.geometry.mapping.visualBottomDip, capture.previousShape.bottomDip)
			&& Near(recaptured.geometry.mapping.MapY(869.0), 867.0),
			"recapture during recovery keeps successful height and moves it to the new effective grab");
	}

	void TestActualHeightReleaseAndAbsorption()
	{
		using namespace Inkeys::UI::Bar;
		BarBottomDockVerticalFrameInput input;
		input.mode = BarBottomDockMode::BottomDocked; input.anchorValid = true;
		input.baseTop = 829.0; input.baseBottom = 914.0; input.targetBottom = 912.0;
		input.logicalGrab = 871.5 + (0.46875 - 0.5) * 84.0;
		input.minimumY = 0.0; input.maximumY = 1080.0; input.dtSeconds = 1.0 / 60.0;
		BarBottomDockSpringState grip{ 10.0, 0.0 }, shape{ -6.0, 0.0 };
		BarBottomDockVerticalFrameResult result;
		for (int i = 0; i < 180; ++i) result = AdvanceBarBottomDockVerticalFrame(grip, shape, input);
		Check(Near(result.geometry.mapping.visualBottomDip, 912.0) && Near(result.geometry.mapping.scaleY, 1.0),
			"actual h84 releases to dock line even when root was positioned using h80");
		const auto released = result.geometry.mapping;
		input.anchorValid = false;
		const auto unanchored = AdvanceBarBottomDockVerticalFrame(grip, shape, input);
		Check(Near(unanchored.geometry.mapping.visualTopDip, released.visualTopDip)
			&& Near(unanchored.geometry.mapping.visualBottomDip, released.visualBottomDip),
			"clearing release anchor has exactly the same non-eighty-DIP terminal geometry");
		const double grabbedScreen = released.MapY(input.logicalGrab) * 2.0 + 13.0;
		const auto actualAnchor = ResolveBarBottomDockGrabAnchor(released, 871.5, 84.0, grabbedScreen, 0.0, 13.0, 2.0);
		Check(actualAnchor.valid && Near(actualAnchor.normalizedY, 0.46875),
			"new Down inversely maps one successful actual height instead of a fixed eighty-DIP proxy");

		for (LONG absorbedY : { -158L, 205L })
		{
			auto vertical = ResolveBarBottomDockAnchoredMapping(831.0, 912.0, 864.0, 844.0,
				912.0, 81.0, true, 0.0, 1080.0).mapping;
			auto horizontal = ResolveBarBottomDockHorizontalMapping(100.0, 500.0, true, -30.0, 0.0);
			const auto previous = vertical;
			const POINT oldTranslation{ 31, absorbedY }, absorbed{ 31, absorbedY };
			const POINT nextTranslation = ResolveBarDirectWindowTranslationAfterAbsorb(oldTranslation, absorbed);
			const RECT viewport{ 100, 1400, 1600, 1900 }; const POINT capacity{ -200, 1000 };
			const POINT originalSource{ viewport.left - capacity.x, viewport.top - capacity.y };
			const auto movedViewport = TranslateBarWindowRect(viewport, absorbed);
			const POINT movedCapacity{ capacity.x + absorbed.x, capacity.y + absorbed.y };
			const double pointScreen = vertical.MapY(864.0) * 2.0 + oldTranslation.y;
			const double oldBottom = vertical.visualBottomDip * 2.0 + oldTranslation.y;
			RebaseBarBottomDockMapping(vertical, horizontal, absorbed.x / 2.0, absorbed.y / 2.0);
			Check(Near(vertical.visualBottomDip * 2.0 + nextTranslation.y, oldBottom),
				"recorded minus158/plus205 translation absorption preserves successful screen bottom");
			const double nextRoot = 871.5 + absorbed.y / 2.0;
			const auto regrab = ResolveBarBottomDockGrabAnchor(vertical, nextRoot, 80.0,
				pointScreen, 0.0, nextTranslation.y, 2.0);
			Check(regrab.valid && Near(regrab.normalizedY, 0.40625),
				"release and immediate regrab keep the same actual local point after layout absorption");
			const double newSurfaceY = vertical.MapY(864.0 + absorbed.y / 2.0) * 2.0 - movedCapacity.y;
			const double oldSurfaceY = previous.MapY(864.0) * 2.0 - capacity.y;
			Check(Near(newSurfaceY, oldSurfaceY)
				&& movedViewport.top - movedCapacity.y == originalSource.y
				&& movedViewport.top + nextTranslation.y == viewport.top + oldTranslation.y,
				"absorbed source, viewport and destination describe identical actual window pixels");
		}
		BarBottomDockVerticalFrameInput shapeOnly;
		shapeOnly.mode = BarBottomDockMode::Floating;
		shapeOnly.anchorValid = shapeOnly.dragging = shapeOnly.handoff = true;
		shapeOnly.baseTop = 831.0; shapeOnly.baseBottom = shapeOnly.targetBottom = 912.0;
		shapeOnly.logicalGrab = shapeOnly.effectiveGrab = 864.0;
		shapeOnly.minimumY = 0.0; shapeOnly.maximumY = 1080.0; shapeOnly.dtSeconds = 1.0 / 60.0;
		const auto successfulRecovery = ResolveBarBottomDockAnchoredMapping(831.0, 912.0, 864.0, 864.0,
			912.0, 110.0, false, 0.0, 1080.0).mapping;
		const auto recoveredGrab = ResolveBarBottomDockGrabAnchor(successfulRecovery, 871.5, 80.0,
			successfulRecovery.MapY(864.0) * 2.0, 0.0, 0.0, 2.0);
		shapeOnly.previousShape = ResolveBarBottomDockShiftedShape(successfulRecovery, 871.5, 80.0,
			recoveredGrab.normalizedY, 2.0, 864.0, 2.0);
		grip = {}; shape = {};
		const auto grabbedRecovery = AdvanceBarBottomDockVerticalFrame(grip, shape, shapeOnly);
		Check(Near(grip.positionDip, 0.0) && Near(shape.positionDip, 29.0)
			&& ShouldRecoverBarBottomDockOnRelease(grabbedRecovery.geometry.mapping, true, 0.0, 0.0),
			"shape-only recovery regrab and immediate release retain the recovery/layout lock even with zero input e");
		shapeOnly.dragging = false;
		const auto releaseShape = AdvanceBarBottomDockVerticalFrame(grip, shape, shapeOnly);
		Check(releaseShape.shapeActive && Near(releaseShape.geometry.mapping.scaleY, successfulRecovery.scaleY)
			&& Near(releaseShape.geometry.mapping.MapY(864.0), 864.0),
			"shape-only release preserves successful scale and actual grab before its first recovery integration");
		shapeOnly.handoff = false;
		BarBottomDockVerticalFrameResult settledShape;
		for (int i = 0; i < 180; ++i) settledShape = AdvanceBarBottomDockVerticalFrame(grip, shape, shapeOnly);
		Check(!settledShape.shapeActive && Near(settledShape.geometry.mapping.scaleY, 1.0),
			"shape-only recovery actually settles before releasing its layout ownership");

		const auto mapping = ResolveBarBottomDockAnchoredMapping(831.0, 912.0, 869.0, 869.0,
			912.0, 71.0, false, 0.0, 1080.0).mapping;
		const auto presentation = ResolveBarBottomDockFramePresentation(40, POINT{ 0, 10 },
			42, 42, 38, 40, POINT{ 0, 65 }, POINT{ 0, 65 });
		Check(!presentation.deferred && Near(mapping.MapY(869.0) * 2.0 + presentation.translation.y,
			869.0 * 2.0 + 10.0 + 55.0),
			"f789-style newer pointer movement is retained as normal fifty-five-pixel direct translation");
		const auto guarded = ResolveBarBottomDockAnchoredMapping(0.0, 81.0, 80.999, 100.0,
			81.0, 81.0, true, 0.0, 1080.0);
		Check(guarded.constrained && std::isfinite(guarded.mapping.scaleY)
			&& guarded.mapping.scaleY >= 0.000001
			&& Near(guarded.mapping.MapY(80.999), guarded.effectiveGrabDip),
			"infeasible bottom-edge grip uses explicit finite positive-height guard while retaining its effective point");
	}

	void TestTemporaryBottomDockTrace()
	{
		namespace Trace = Inkeys::UI::Bar::BottomDockTrace;
		Trace::Buffer<2> buffer;
		Trace::Record record;
		record.frame = 7;
		Check(buffer.TryPush(record) && buffer.TryPush(record) && !buffer.TryPush(record),
			"trace queue rejects overflow without waiting or overwriting earlier evidence");
		Trace::Record first, second;
		Check(buffer.TryPop(first) && buffer.TryPop(second)
			&& first.sequence == 1 && second.sequence == 2 && buffer.Dropped() == 1,
			"trace queue preserves order and explicitly counts dropped records");
		Check(buffer.TryPush(record) && buffer.TryPop(first)
			&& first.sequence == 3 && first.dropped == 1,
			"trace queue reports preceding losses after buffer wrap");
		Trace::Buffer<8> concurrent;
		std::atomic<unsigned> accepted{ 0 };
		auto Publish = [&]
			{ for (int i = 0; i < 200; ++i) if (concurrent.TryPush(record)) accepted.fetch_add(1); };
		std::thread a(Publish), c(Publish);
		a.join(); c.join();
		Check(accepted.load() == 8 && concurrent.Dropped() == 392,
			"competing trace producers remain bounded and account for every refused record");

		Trace::Record basis;
		basis.event = Trace::Event::Committed;
		basis.groups = Trace::State | Trace::Environment | Trace::Geometry | Trace::Window | Trace::Result;
		basis.frame = 1; basis.consumedSerial = basis.taggedSerial = basis.presentedSerial = 2;
		basis.monitor = { 0, 0, 1920, 1080 }; basis.workArea = { 0, 0, 1920, 540 };
		basis.zoom = basis.configZoom = basis.dpiScale = 1.0; basis.dpi = 96; basis.dockLine = 540.0;
		basis.root = { 400.0, 500.0 }; basis.mainSize = { 80.0, 80.0 }; basis.baseSize = 80.0;
		basis.baseY = basis.visualY = { 460.0, 540.0 }; basis.scaleY = 1.0;
		basis.horizontal = { 450.0, 750.0, 450.0, 750.0, 1.0, 0.0 };
		basis.barBounds = { 450.0, 460.0, 300.0, 80.0 };
		basis.viewport = { 300, 400, 800, 600 }; basis.capacityOrigin = { 200, 300 };
		basis.source = { 100, 100 }; basis.destination = { 300, 400 };
		basis.capacitySize = { 1000, 800 }; basis.windowSize = { 500, 200 };
		basis.cachedWindow = basis.viewport;
		basis.getDc = basis.releaseDc = basis.endDraw = S_OK; basis.ulw = 1; basis.committed = true;
		basis.grabSolverValid = true; basis.grabNormalizedY = 0.75; basis.grabRawPointerScreenY = 520.0;
		basis.grabSolverDip = { 520.0, 520.0, 520.0 };
		basis.groups |= Trace::PresentedState;
		basis.presentedState.mainHeightDip = 80.0; basis.presentedState.zoom = 1.0;
		basis.presentedState.baseY = basis.baseY; basis.presentedState.visualY = basis.visualY;
		basis.presentedState.mainCenterScreen = { 400.0, 500.0 };
		basis.presentedState.transitionSerial = 2;
		Json::Value knownGeometry; Json::CharReaderBuilder knownReader; std::string knownErrors;
		std::istringstream knownStream(Trace::Serialize(basis, 1));
		Check(Json::parseFromStream(knownReader, knownStream, &knownGeometry, &knownErrors)
			&& Near(knownGeometry["presented_snapshot"]["main_height"].asDouble(), 80.0)
			&& Near(knownGeometry["geometry"]["grab_solver"]["raw_screen_y"].asDouble(), 520.0),
			"native fixture preserves successful actual height and raw pointer for anchored-solver analysis");
		Trace::Record down;
		down.groups = Trace::Input | Trace::State; down.pointer = { 400.0, 520.0 };
		down.grabOffset = { 0.0, 20.0 }; down.rawGrip = { 400.0, 500.0 };
		Check(Trace::ResolveInitialGrab(down, basis, basis.viewport)
			&& Near(down.normalizedDown[1], 0.75), "trace inverse maps actual Down to the successful image height");
		Trace::Record movedDown = down;
		movedDown.pointer[1] += 20.0;
		Check(Trace::ResolveInitialGrab(movedDown, basis, RECT{ 300, 420, 800, 620 })
			&& Near(movedDown.normalizedDown[1], 0.75),
			"image probe stays in original scene coordinates through direct move or layout absorption");
		Trace::Record pulsingBasis = basis;
		pulsingBasis.mainSize[1] = 84.0; pulsingBasis.scaleY = 0.75;
		pulsingBasis.visualY = { 470.0, 530.0 };
		Trace::Record pulsingDown; pulsingDown.pointer = { 400.0, 535.75 };
		Check(Trace::ResolveInitialGrab(pulsingDown, pulsingBasis, RECT{ 300, 420, 800, 620 })
			&& Near(pulsingDown.normalizedDown[1], 0.75),
			"trace grab basis uses actual pulsing height and inverse nonidentity mapping");
		Trace::Record invalidBasis = basis; invalidBasis.mainSize[1] = 0.0;
		Check(!Trace::ResolveInitialGrab(down, invalidBasis, basis.viewport),
			"missing successful height never silently falls back to eighty DIP");
		Trace::Record snapshotRecord = basis;
		snapshotRecord.presentedState.mainHeightDip = 0.0;
		snapshotRecord.groups |= Trace::PresentedState;
		snapshotRecord.deviceGeneration = 31;
		snapshotRecord.resourceHr = E_OUTOFMEMORY;
		snapshotRecord.getDc = E_PENDING;
		snapshotRecord.flags |= Trace::GripRecoverySeeded;
		snapshotRecord.elasticInputDip = { -20.0, 39.0 };
		snapshotRecord.root[1] = 620.0;
		snapshotRecord.presentedState.origin = { 0, 50 };
		snapshotRecord.presentedState.zoom = 1.5;
		snapshotRecord.presentedState.baseY = { 460.0, 540.0 };
		snapshotRecord.presentedState.visualY = { 470.0, 530.0 };
		snapshotRecord.presentedState.directTranslation = { 0, 17 };
		snapshotRecord.presentedState.mainCenterScreen = { 400.0, 500.0 };
		snapshotRecord.presentedState.transitionSerial = 22;
		Json::Value snapshotJson; Json::CharReaderBuilder snapshotReader; std::string snapshotErrors;
		std::istringstream snapshotStream(Trace::Serialize(snapshotRecord, 1));
		Check(Json::parseFromStream(snapshotReader, snapshotStream, &snapshotJson, &snapshotErrors)
			&& snapshotJson["device_generation"].asUInt64() == 31
			&& snapshotJson["result"]["resource_hr"].asInt() == E_OUTOFMEMORY
			&& snapshotJson["result"]["get_dc"].asInt() == E_PENDING
			&& snapshotJson["state"]["elastic_dip"][1].asDouble() == 39.0
			&& snapshotJson["presented_snapshot"]["main_center_screen"][1].asDouble() == 500.0
			&& snapshotJson["geometry"]["root"][1].asDouble() == 620.0
			&& snapshotJson["presented_snapshot"]["main_height"].isNull(),
			"trace distinguishes consumed elastic input, resource failure and production snapshot from candidate geometry");
		Trace::Recorder disabled;
		Check(!disabled.Start(L"uncreated-disabled-bar-trace", {}, false)
			&& disabled.ActiveGesture() == 0, "disabled trace does not start a writer or create files");
		if (!Trace::Enabled) return;

		const auto fixture = std::filesystem::absolute("Build/ui3-bottom-dock-validation/trace-fixture");
		Trace::Recorder idle;
		Check(idle.Start(fixture / "idle"), "idle recorder starts before any input arrives");
		idle.Stop();
		Check(idle.Stats().written == 0 && idle.Stats().writerErrors == 0,
			"stopping before the first gesture does not write an unopened stream");
		Trace::Recorder recorder;
		Check(recorder.Start(fixture, Trace::Limits{ 8 * 1024 * 1024, 4, 20 }), "temporary trace writer starts without a window");
		recorder.Presented(basis); // 手势前只缓存真实成功图像，不记录空闲帧。
		SetLastError(12345);
		down.gesture = recorder.BeginGesture(down, basis.viewport);
		Check(down.gesture == 1 && (down.flags & Trace::GrabPointValid) && GetLastError() == 12345,
			"gesture start preserves LastError and captures a valid successful image basis");
		Trace::Record pointer = down; pointer.event = Trace::Event::Pointer;
		pointer.consumedSerial = pointer.currentSerial = 4; pointer.mode = 1; pointer.phase = 2;
		SetLastError(23456); recorder.Push(pointer);
		Check(GetLastError() == 23456, "hot trace push preserves API failure evidence");
		Trace::Record frame = basis; frame.gesture = down.gesture; frame.frame = 2;
		frame.consumedSerial = frame.taggedSerial = frame.currentSerial = frame.presentedSerial = 4;
		frame.mode = 1; frame.phase = 2; frame.drag = true; frame.rawGrip = pointer.rawGrip;
		frame.snapshotQpc = Trace::Now(); frame.submitQpc = Trace::Now();
		frame.event = Trace::Event::VerticalMapping; recorder.Push(frame);
		frame.event = Trace::Event::Committed; recorder.Presented(frame);
		Trace::Record inactiveFrame = frame; inactiveFrame.gesture = 0;
		recorder.Push(inactiveFrame); // 不能把快照时无手势的在途帧贴给当前新手势。
		recorder.InvalidatePresented();
		Trace::Record unavailable = down; unavailable.flags = 0; unavailable.gesture = 0;
		recorder.EndGesture(down);
		unavailable.gesture = recorder.BeginGesture(unavailable, basis.viewport);
		Check((unavailable.flags & Trace::GrabPointValid) == 0,
			"partial ULW failure invalidates the probe until a complete presentation returns");
		recorder.EndGesture(unavailable);
		bool savedTail = false;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!savedTail && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(fixture, ec))
			{
				if (!Trace::IsOwnedFilename(entry.path())) continue;
				std::ifstream input(entry.path());
				const std::string text((std::istreambuf_iterator<char>(input)), {});
				savedTail |= text.find("\"run_id\":" + std::to_string(recorder.Stats().runId)) != std::string::npos
					&& text.find("\"event\":\"tail_complete\"") != std::string::npos;
			}
		}
		Check(savedTail && recorder.ActiveGesture() == 0,
			"release automatically flushes the bounded recovery tail without rendering requests");
		recorder.Stop();
		Check(recorder.Stats().written + recorder.Stats().dropped == 8 && recorder.Stats().writerErrors == 0,
			"managed writer drains every accepted gesture event and excludes unowned frames");
		bool validJson = true;
		for (const auto& entry : std::filesystem::directory_iterator(fixture))
		{
			if (!Trace::IsOwnedFilename(entry.path())) continue;
			std::ifstream input(entry.path()); std::string line; std::uint64_t previous = 0;
			while (std::getline(input, line))
			{
				Json::Value row; Json::CharReaderBuilder reader; std::string errors;
				std::istringstream stream(line);
				validJson &= Json::parseFromStream(reader, stream, &row, &errors);
				if (row.isMember("seq"))
				{
					validJson &= row["seq"].asUInt64() > previous;
					previous = row["seq"].asUInt64();
				}
			}
		}
		Check(validJson, "native trace fixture is complete JSONL with ordered event sequence");
		std::cout << "TRACE_FIXTURE " << fixture.string() << '\n';

		const auto retention = fixture / "retention";
		std::filesystem::create_directories(retention);
		{ std::ofstream keep(retention / "unrelated.jsonl"); keep << "keep"; }
		Trace::Recorder rotated;
		Check(rotated.Start(retention, Trace::Limits{ 8192, 2, 0 }), "bounded trace file rotation starts");
		Trace::Record start; start.groups = Trace::Input;
		start.gesture = rotated.BeginGesture(start, {});
		for (int i = 0; i < 100; ++i)
		{
			Trace::Record item = frame; item.gesture = start.gesture;
			rotated.Push(item);
		}
		rotated.EndGesture(start); rotated.Stop();
		std::size_t traceFiles = 0; bool bounded = true;
		for (const auto& entry : std::filesystem::directory_iterator(retention))
			if (Trace::IsOwnedFilename(entry.path())) { ++traceFiles; bounded &= entry.file_size() <= 8192; }
		Check(traceFiles == 2 && bounded && std::filesystem::exists(retention / "unrelated.jsonl"),
			"rotation caps trace-only files and leaves unrelated logs untouched");

		const auto smallParts = fixture / "bounded-metadata";
		Trace::Recorder limited;
		Check(limited.Start(smallParts, Trace::Limits{ 4096, 2, 0 }), "small-part trace starts for metadata bounds regression");
		Trace::Record smallStart; smallStart.groups = Trace::Input;
		smallStart.gesture = limited.BeginGesture(smallStart, {});
		Trace::Record oversized = snapshotRecord;
		oversized.gesture = smallStart.gesture;
		oversized.groups = Trace::State | Trace::Environment | Trace::Input | Trace::Geometry
			| Trace::Spring | Trace::Window | Trace::Result | Trace::Timing | Trace::PresentedState;
		const double extreme = std::numeric_limits<double>::max();
		auto Fill = [extreme](auto& values) { values.fill(extreme); };
		Fill(oversized.pointer); Fill(oversized.grabOffset); Fill(oversized.logicalDown);
		Fill(oversized.normalizedDown); Fill(oversized.rawGrip); Fill(oversized.elasticInputDip);
		Fill(oversized.root); Fill(oversized.mainSize); Fill(oversized.baseY); Fill(oversized.visualY);
		Fill(oversized.barBounds); Fill(oversized.horizontal); Fill(oversized.gripSpring);
		Fill(oversized.captureSpring); Fill(oversized.gripBefore); Fill(oversized.captureBefore); Fill(oversized.seedScreen);
		oversized.configZoom = oversized.zoom = oversized.dockLine = oversized.insetDip = oversized.dpiScale = extreme;
		oversized.baseSize = oversized.stroke = oversized.scaleY = oversized.translationY = oversized.barStroke = extreme;
		oversized.rawDt = oversized.frameDt = oversized.integratedDt = oversized.animationSpeed = extreme;
		oversized.gripDt = oversized.captureDt = extreme;
		Fill(oversized.presentedState.baseY); Fill(oversized.presentedState.visualY); Fill(oversized.presentedState.mainCenterScreen);
		oversized.presentedState.zoom = oversized.presentedState.rawMainCenterScreenX = oversized.presentedState.rawBodyCenterScreenX = extreme;
		const LONG coordinate = std::numeric_limits<LONG>::min();
		const POINT extremePoint{ coordinate, coordinate };
		const RECT extremeRect{ coordinate, coordinate, coordinate, coordinate };
		oversized.monitorOrigin = oversized.desired = oversized.actual = oversized.frameTranslation = extremePoint;
		oversized.capacityOrigin = oversized.source = oversized.destination = extremePoint;
		oversized.monitor = oversized.workArea = oversized.viewport = oversized.cachedWindow = oversized.osWindow = extremeRect;
		oversized.capacitySize = oversized.windowSize = { coordinate, coordinate };
		oversized.presentedState.origin = oversized.presentedState.directTranslation = extremePoint;
		Check(Trace::Serialize(oversized, limited.Stats().runId).size() + 1024 > 4096,
			"oversized fixture exceeds one part after required metadata reserve");
		for (int i = 0; i < 16; ++i) limited.Push(oversized);
		limited.EndGesture(smallStart); limited.Stop();
		bool allBytesBounded = true, stopMetadata = false;
		std::size_t limitedFiles = 0;
		for (const auto& entry : std::filesystem::directory_iterator(smallParts))
		{
			if (!Trace::IsOwnedFilename(entry.path())) continue;
			++limitedFiles;
			allBytesBounded &= entry.file_size() <= 4096;
			std::ifstream input(entry.path()); std::string line;
			while (std::getline(input, line))
			{
				Json::Value row; Json::CharReaderBuilder reader; std::string errors;
				std::istringstream stream(line);
				allBytesBounded &= Json::parseFromStream(reader, stream, &row, &errors);
				if (row["event"].asString() == "stop" && row["run_id"].asUInt64() == limited.Stats().runId)
					stopMetadata = row["writer_dropped"].asUInt64() > 0;
			}
		}
		Check(allBytesBounded && limitedFiles == 2 && stopMetadata
			&& limited.Stats().writerDropped > 0 && limited.Stats().writerErrors == 0
			&& limited.Stats().written + limited.Stats().dropped == 18,
			"every part includes bounded run/tail/stop metadata and accounts for records too large to serialize safely");
	}

	void TestHorizontalCapturePresentedSequence()
	{
		using namespace Inkeys::UI::Bar;
		for (bool opensRight : { true, false })
			for (double zoom : { 1.0, 1.5, 1.875 })
				for (double width : { 200.0, 560.0 })
				{
					const BarBottomDockEnvironment environment{
						RECT{ 100, 50, 2020, 1130 }, RECT{ 100, 50, 2020, 1082 }, zoom };
					const double monitorCenter = ResolveBarBottomDockMonitorCenterScreenX(environment.monitorBounds);
					const double bodyToMain = (opensRight ? 1.0 : -1.0) * (width / 2.0 + 10.0) * zoom;
					const double baseMain = monitorCenter - 60.0 * zoom - bodyToMain - 37.0;
					const double baseLeft = baseMain / zoom + (opensRight ? 50.0 : -50.0 - width);
					const double baseRight = baseLeft + width;
					const double baseFar = opensRight ? baseRight : baseLeft;
					BarBottomDockCenterDragTracker tracker;
					tracker.Begin(BarBottomDockCenterMode::Free, monitorCenter - 60.0 * zoom,
						true, true, environment);
					BarBottomDockCenterMode presentedMode = BarBottomDockCenterMode::Free;
					LONG presentedTranslation = 37;
					double presentedFar = baseFar;
					BarBottomDockSpringState farSpring;
					int sampleIndex = 0;
					bool recapturedDuringRecovery = false;
					constexpr int recoveryFrameCounts[]{ 180, 4, 1 };
					for (double offset : { -45.0, -40.0, -39.0, -25.0, 0.0, 25.0, 39.0, 41.0, 39.0, -41.0, -39.0 })
					{
						const double rawBody = monitorCenter + offset * zoom;
						const double rawMain = rawBody - bodyToMain;
						const auto update = tracker.Update(rawBody, true, true, environment);
						const LONG translation = static_cast<LONG>(std::lround(
							update.constrainedCenterScreenX - bodyToMain - baseMain));
						const bool centered = update.mode == BarBottomDockCenterMode::Centered;
						const double grip = centered ? (rawMain - baseMain - translation) / zoom : 0.0;
						const bool transition = update.mode != presentedMode;
						const double previousFarScreen = presentedFar * zoom + presentedTranslation;
						if (transition)
						{
							recapturedDuringRecovery |= centered
								&& std::abs(farSpring.positionDip) > BarBottomDockSettleDistanceDip;
							farSpring = { ResolveBarBottomDockRebasedFarEdgeOffsetDip(
								presentedFar, presentedTranslation, translation, baseFar, zoom), 0.0 };
						}
						const auto mapping = ResolveBarBottomDockHorizontalMapping(
							baseLeft, baseRight, opensRight, grip, farSpring.positionDip);
						const double currentFar = opensRight ? mapping.visualRightDip : mapping.visualLeftDip;
						Check(std::abs(baseMain + translation + mapping.rigidGripTranslationXDip * zoom - rawMain) <= 0.5,
							"capturing between 24 and 40 DIP, crossing and detaching preserve the held main button");
						if (transition)
							Check(Near(currentFar * zoom + translation, previousFarScreen),
								"capture, detach and recapture seed the far edge from exactly the last presented pixels");
						const double nearBase = opensRight ? baseLeft : baseRight;
						Check(Near(mapping.MapX(nearBase), nearBase + grip)
							&& Near(mapping.UnmapX(mapping.MapX(baseLeft + width / 3.0)), baseLeft + width / 3.0),
							"near edge shares rigid grip motion and body hit mapping round trips");
						const RECT baseBounds{ static_cast<LONG>(std::floor(baseLeft * zoom)), 900,
							static_cast<LONG>(std::ceil(baseRight * zoom)), 1000 };
						const auto envelope = ResolveBarBottomDockCapacityEnvelope(baseBounds, zoom,
							std::max({ BarBottomDockCenterThresholdDip, std::abs(grip), std::abs(farSpring.positionDip) }));
						Check(envelope.left <= mapping.visualLeftDip * zoom
							&& envelope.right >= mapping.visualRightDip * zoom,
							"horizontal capacity includes exact grip and recovery seed at every scale");
						// 很短的下一帧应连续衰减，不能把 39/41 DIP 的真实像素瞬间截到 24。
						const double initialFarOffset = farSpring.positionDip;
						(void)AdvanceBarBottomDockSpring(farSpring, 0.0, 0.0001, true, true);
						Check(std::abs(farSpring.positionDip - initialFarOffset) < 0.1,
							"far edge recovery has no first-frame clamp at the old visual limit");
						// 交错完整恢复与仅 1～4 帧的快速反向，重捕获必须接住仍在运动的远端。
						const int recoveryFrames = recoveryFrameCounts[sampleIndex++ % 3];
						for (int frame = 0; frame < recoveryFrames; ++frame)
							(void)AdvanceBarBottomDockSpring(farSpring, 0.0, 1.0 / 60.0, true, true);
						if (recoveryFrames == 180)
							Check(Near(farSpring.positionDip, 0.0), "far edge still settles with the original spring timing");
						presentedMode = update.mode;
						presentedTranslation = translation;
						const auto presentedMapping = ResolveBarBottomDockHorizontalMapping(
							baseLeft, baseRight, opensRight, grip, farSpring.positionDip);
						presentedFar = opensRight ? presentedMapping.visualRightDip : presentedMapping.visualLeftDip;
					}
					Check(recapturedDuringRecovery, "sequence recaptures before the preceding far-edge recovery settles");
					for (int frame = 0; frame < 180; ++frame)
						(void)AdvanceBarBottomDockSpring(farSpring, 0.0, 1.0 / 60.0, true, true);
					Check(Near(farSpring.positionDip, 0.0), "rapid recapture recovery still converges");
				}
	}

	void TestOldFloatingFrameCannotAcknowledgeCapture()
	{
		using namespace Inkeys::UI::Bar;
		for (bool frameWasDragging : { true, false })
		{
			std::atomic<unsigned long long> serial{ 100 }, deferred{ 0 };
			unsigned long long presentedSerial = 100, frameSerial = 100;
			POINT actualTranslation{}, desiredTranslation{};
			BarBottomDockMode presentedMode = BarBottomDockMode::Floating;
			const auto floatingMapping = ResolveBarBottomDockRecoveringVerticalMapping(900.0, 980.0, 0.0);
			// 旧帧已消费 Floating；捕获在它绘制/归位阶段之前夹入。
			BeginBarBottomDockTransition(serial);
			desiredTranslation = { 0, 20 };
			const auto captureSerial = FinishBarBottomDockTransition(serial, deferred, true);
			Check(captureSerial == 102, "capture publishes geometry and deferred barrier together");
			if (TryBeginBarBottomDockFrameTransition(serial, frameSerial, frameWasDragging))
				frameSerial = FinishBarBottomDockTransition(serial, deferred, true);
			const auto staleFrame = ResolveBarBottomDockFramePresentation(frameSerial, {},
				serial.load(), serial.load(), deferred.load(), presentedSerial,
				desiredTranslation, actualTranslation);
			Check(frameSerial == 100 && serial.load() == 102 && staleFrame.deferred,
				"old floating or pre-press frame cannot promote itself past a newer capture");
			if (!staleFrame.deferred) presentedSerial = frameSerial;

			// 下一次只有 X 多一像素；错误确认会把旧的正常高度位图整张向下直移 20。
			BeginBarBottomDockTransition(serial);
			desiredTranslation.x = 1;
			FinishBarBottomDockTransition(serial, deferred, false);
			if (presentedSerial >= deferred.load())
				actualTranslation = desiredTranslation;
			Check(actualTranslation.y == 0,
				"tiny X motion cannot carry an undeformed floating bitmap down to the dock target");
			const double legacyTopScreen = floatingMapping.visualTopDip + desiredTranslation.y;
			Check(legacyTopScreen == 920.0, "regression setup reproduces the former whole-bar drop");

			// 生产种子入口只消费成功像素；第一个失败候选不算已经捕获。
			BarBottomDockSpringState bottomSpring;
			Check(SeedBarBottomDockCaptureBottom(bottomSpring, presentedMode,
				BarBottomDockMode::BottomDocked, 980.0, 1000.0, 1.0, true),
				"first capture candidate seeds from the successfully presented floating bottom");
			(void)AdvanceBarBottomDockSpring(bottomSpring, 0.0, 0.032, true);
			Check(SeedBarBottomDockCaptureBottom(bottomSpring, presentedMode,
				BarBottomDockMode::BottomDocked, 980.0, 1000.0, 1.0, true)
				&& Near(bottomSpring.positionDip, -20.0),
				"skipped or failed capture cannot consume the successful-pixel seed");
			const double gripOffset = ResolveBarBottomDockElasticOffsetForScreenGrip(
				940.0, 940.0 + desiredTranslation.y, 1.0);
			auto captureMapping = ResolveBarBottomDockVerticalMapping(
				900.0, 980.0, gripOffset, bottomSpring.positionDip);
			const auto captureFrame = ResolveBarBottomDockFramePresentation(serial.load(), desiredTranslation,
				serial.load(), serial.load(), deferred.load(), presentedSerial,
				desiredTranslation, actualTranslation);
			Check(!captureFrame.deferred
				&& Near(captureMapping.visualTopDip + captureFrame.translation.y, 900.0)
				&& Near(captureMapping.visualBottomDip + captureFrame.translation.y, 980.0),
				"first successful dock bitmap keeps the grip and bottom pixels while changing HWND placement");
			presentedMode = BarBottomDockMode::BottomDocked;
			presentedSerial = serial.load();
			actualTranslation = captureFrame.translation;
			Check(!SeedBarBottomDockCaptureBottom(bottomSpring, presentedMode,
				BarBottomDockMode::BottomDocked, 980.0, 1000.0, 1.0, true),
				"next held frame continues the committed capture instead of reseeding it");
			(void)AdvanceBarBottomDockSpring(bottomSpring, 0.0, 1.0 / 60.0, true);
			captureMapping = ResolveBarBottomDockVerticalMapping(900.0, 980.0, gripOffset, bottomSpring.positionDip);
			Check(Near(captureMapping.visualTopDip + actualTranslation.y, 900.0)
				&& captureMapping.visualBottomDip + actualTranslation.y > 980.0
				&& captureMapping.visualBottomDip + actualTranslation.y < 1000.0,
				"following frame keeps the pointer grip and springs only the bottom toward dock");
		}
	}

	void TestCaptureBottomUsesSuccessfulRecoveryPixels()
	{
		using namespace Inkeys::UI::Bar;
		for (double previousZoom : { 1.0, 1.5 })
			for (double nextZoom : { 1.0, 1.875 })
			{
				const auto previous = ResolveBarBottomDockRecoveringVerticalMapping(800.0, 880.0, -14.0, -4.0);
				const double oldBottomScreen = 50.0 + previous.visualBottomDip * previousZoom - 7.0;
				const double nextOrigin = -120.0, nextTranslation = 17.0;
				const double nextBottomScreen = oldBottomScreen + 30.0 * nextZoom;
				const double baseBottom = (nextBottomScreen - nextOrigin - nextTranslation) / nextZoom;
				BarBottomDockSpringState spring{ 0.0, 100.0 };
				Check(SeedBarBottomDockCaptureBottom(spring, BarBottomDockMode::Floating,
					BarBottomDockMode::BottomDocked, oldBottomScreen, nextBottomScreen, nextZoom, true)
					&& Near(spring.positionDip, -30.0),
					"capture seed includes prior recovery, origins, zooms and translations without clipping successful pixels");
				const auto captured = ResolveBarBottomDockVerticalMapping(
					baseBottom - 80.0, baseBottom, -20.0, spring.positionDip, true);
				Check(Near(captured.visualBottomDip * nextZoom + nextOrigin + nextTranslation, oldBottomScreen),
					"first capture preserves actual old bottom rather than replacing it with current grip offset");
				const auto recovering = ResolveBarBottomDockRecoveringVerticalMapping(
					baseBottom - 80.0, baseBottom, 14.0, spring.positionDip, true);
				Check(Near(recovering.visualBottomDip, baseBottom - 44.0),
					"floating recovery preserves the exact capture-bottom term with ordinary bounded grip physics");
				const RECT baseBounds{ 100, 400, 700, 600 };
				const auto envelope = ResolveBarBottomDockCapacityEnvelope(baseBounds, nextZoom,
					BarBottomDockCenterThresholdDip, BarBottomDockVisualLimitDip + std::abs(spring.positionDip));
				Check(envelope.top <= baseBounds.top - 44.0 * nextZoom,
					"vertical capacity covers capture seed plus floating recovery excursion");
				(void)AdvanceBarBottomDockSpring(spring, 0.0, 0.0001, true, true);
				Check(spring.positionDip < -29.9,
					"capture-bottom spring decays continuously instead of clamping its first active frame to 24");
				for (int i = 0; i < 180; ++i)
					(void)AdvanceBarBottomDockSpring(spring, 0.0, 1.0 / 60.0, true, true);
				Check(Near(spring.positionDip, 0.0), "capture-bottom exception still settles with unchanged force parameters");
				(void)SeedBarBottomDockCaptureBottom(spring, BarBottomDockMode::Floating,
					BarBottomDockMode::BottomDocked, oldBottomScreen, nextBottomScreen, nextZoom, false);
				Check(Near(spring.positionDip, 0.0), "animation-disabled capture still applies the immediate dock geometry");
			}
	}

	void TestBottomDockPublicationWriterOwnership()
	{
		using namespace Inkeys::UI::Bar;
		std::atomic<unsigned long long> serial{ 0 }, deferred{ 0 };
		Check(TryBeginBarBottomDockFrameTransition(serial, 0, false)
			&& serial.load() == 1, "idle frame can claim exactly the state it consumed");
		Check(!TryBeginBarBottomDockFrameTransition(serial, 0, false)
			&& serial.load() == 1, "second publisher cannot turn an in-flight odd serial even");
		FinishBarBottomDockTransition(serial, deferred, true);
		Check(!TryBeginBarBottomDockFrameTransition(serial, 2, true)
			&& serial.load() == 2, "held tracker owns phase even when render serial is current");
		std::atomic<int> first{ 0 }, second{ 0 };
		std::atomic<bool> doneA{ false }, doneB{ false };
		auto Publish = [&](int sign, std::atomic<bool>& done)
			{
				for (int i = 1; i <= 200; ++i)
				{
					BeginBarBottomDockTransition(serial);
					first.store(sign * i, std::memory_order_relaxed);
					second.store(-sign * i, std::memory_order_relaxed);
					FinishBarBottomDockTransition(serial, deferred, i % 2 == 0);
				}
				done.store(true);
			};
		std::thread a([&] { Publish(1, doneA); });
		std::thread b([&] { Publish(-1, doneB); });
		bool completeTuples = true;
		while (!doneA.load() || !doneB.load())
		{
			const auto before = serial.load(std::memory_order_acquire);
			if ((before & 1ULL) != 0) continue;
			const int x = first.load(std::memory_order_relaxed), y = second.load(std::memory_order_relaxed);
			const auto barrier = deferred.load(std::memory_order_relaxed);
			if (serial.load(std::memory_order_acquire) == before)
				completeTuples &= x + y == 0 && barrier <= before && (barrier & 1ULL) == 0;
		}
		a.join();
		b.join();
		Check(completeTuples && serial.load() == 802,
			"competing input and external publishers expose only complete paired tuples and barriers");
		Check(!TryBeginBarBottomDockFrameTransition(serial, 2, false),
			"stale release or automatic-center frame cannot overwrite a newer fold or display tuple");
	}

	void TestInvalidatedFinalCandidateRetainsPresentationDemand()
	{
		using namespace Inkeys::UI::Bar;
		const RECT window{ 0, 0, 800, 600 };
		const RECT previous{ 100, 100, 400, 200 };
		const RECT finalBounds{ 150, 100, 350, 200 };
		BarDirtyRegionTracker dirty;
		dirty.BeginFrame(window);
		dirty.Observe(1, previous);
		dirty.CommitPresented();
		BarPresentDecision present(previous);
		Check(!present.ShouldPresent(), "regression starts from idle with no pending presentation demand");

		// 最后一帧几何已推进完毕，但新输入淘汰了候选；下一帧不再有动画变化。
		dirty.BeginFrame(window);
		dirty.MarkChanged(1);
		dirty.Observe(1, finalBounds);
		dirty.RetainForRetry(true);
		present.RequireVisualRetry();
		present.AddDemand({ false, false, false });
		Check(present.ShouldPresent() && present.NeedsInteractivePass() && present.NeedsFullDirty()
			&& SameBarWindowRect(dirty.ResolveDamage(false), window),
			"invalidated last animation candidate still presents when the next frame has no new activity");
		Check(!present.HasFailureBackoff() && SameBarWindowRect(present.LastPresentedBounds(), previous),
			"candidate invalidation retains the successful snapshot without manufacturing a failure");
		const auto skipped = present.CompleteAttempt(BarPresentAttemptResult::CosmeticLeaseSkipped());
		const auto failed = present.CompleteAttempt(BarPresentAttemptResult::Acquired(
			S_OK, TRUE, S_OK, E_FAIL, finalBounds));
		Check(!skipped.IsCommitted() && !failed.IsCommitted() && present.ShouldPresent()
			&& present.NeedsFullDirty() && SameBarWindowRect(present.LastPresentedBounds(), previous),
			"deferred and failed retry retain the unpresented final candidate until success");
		present.AddDemand({ false, true, true });
		present.RequireVisualRetry();
		Check(present.HasPendingLighting() && present.HasPendingRenderOnce(),
			"visual invalidation retry preserves independent lighting and alpha-only demand");
		const auto committed = present.CompleteAttempt(BarPresentAttemptResult::Acquired(
			S_OK, TRUE, S_OK, S_OK, finalBounds));
		if (committed.IsCommitted()) dirty.CommitPresented();
		Check(committed.IsCommitted() && !present.ShouldPresent() && !present.NeedsFullDirty()
			&& !dirty.HasPendingDamage() && SameBarWindowRect(present.LastPresentedBounds(), finalBounds),
			"only a successful final frame clears retained demand and damage");
	}

	void TestFirstCapturePresentationAcrossNewSamples()
	{
		using namespace Inkeys::UI::Bar;
		for (bool opensRight : { true, false })
			for (double zoom : { 1.0, 1.5, 1.875 })
			{
				const POINT origin{ 100, 50 };
				const POINT previousTranslation{ 13, -7 };
				const POINT captureTranslation{
					previousTranslation.x - static_cast<LONG>(std::lround((opensRight ? 39.0 : -39.0) * zoom)),
					previousTranslation.y - static_cast<LONG>(std::lround(16.0 * zoom)) };
				const double gripX = (previousTranslation.x - captureTranslation.x) / zoom;
				const double gripY = (previousTranslation.y - captureTranslation.y) / zoom;
				const double mainX = 400.0;
				const double mainY = 500.0;
				const double left = mainX + (opensRight ? 50.0 : -250.0);
				const double right = left + 200.0;
				const double baseFarEdge = opensRight ? right : left;
				const double captureFar = ResolveBarBottomDockRebasedFarEdgeOffsetDip(
					baseFarEdge, previousTranslation.x, captureTranslation.x, baseFarEdge, zoom);
				const auto horizontal = ResolveBarBottomDockHorizontalMapping(
					left, right, opensRight, gripX, captureFar);
				const auto vertical = ResolveBarBottomDockVerticalMapping(
					mainY - 40.5, mainY + 40.5, gripY, gripY);
				// 首次捕获等待 ULW 时，普通采样仍可连续推进 serial，但不改变此屏障。
				for (unsigned long long serial : { 2ULL, 4ULL, 12ULL, 40ULL })
				{
					const auto decision = ResolveBarBottomDockFramePresentation(
						2, captureTranslation, serial, serial, 2, 0,
						captureTranslation, previousTranslation);
					Check(!decision.deferred, "ordinary samples cannot starve the first capture presentation");
					const double screenX = origin.x + (mainX + horizontal.rigidGripTranslationXDip) * zoom
						+ decision.translation.x;
					const double screenY = origin.y + vertical.MapY(mainY) * zoom + decision.translation.y;
					Check(Near(screenX, origin.x + mainX * zoom + previousTranslation.x)
						&& Near(screenY, origin.y + mainY * zoom + previousTranslation.y),
						"first capture bitmap and destination preserve both held screen coordinates when serial advances");
				}
				const POINT staleFallback = ResolveBarBottomDockFrameTranslation(
					2, 4, 4, captureTranslation, previousTranslation);
				Check(std::abs(horizontal.rigidGripTranslationXDip * zoom + staleFallback.x
					- previousTranslation.x) > 24.0 * zoom
					&& std::abs(vertical.MapY(mainY) * zoom + staleFallback.y
						- mainY * zoom - previousTranslation.y) > 10.0 * zoom,
					"old stale-destination fallback reproduces the X/Y capture jump");

				BarPresentDecision present;
				present.AddDemand({ true, false, false });
				const auto failed = present.CompleteAttempt(BarPresentAttemptResult::Acquired(
					S_OK, TRUE, S_OK, E_FAIL, {}));
				const auto retry = ResolveBarBottomDockFramePresentation(
					4, captureTranslation, 6, 6, 2, 0, captureTranslation, previousTranslation);
				Check(!failed.IsCommitted() && present.ShouldPresent() && present.NeedsFullDirty()
					&& !retry.deferred && retry.translation.x == captureTranslation.x
					&& retry.translation.y == captureTranslation.y,
					"failed first capture retains its barrier and retries with matching bitmap translation");
				Check(present.CompleteAttempt(BarPresentAttemptResult::Acquired(
					S_OK, TRUE, S_OK, S_OK, {})).IsCommitted(), "retry establishes the first capture snapshot");

				const auto next = ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 6, 6, 2, 4, captureTranslation, captureTranslation);
				const auto nextHorizontal = ResolveBarBottomDockHorizontalMapping(
					left, right, opensRight, gripX + 2.0 / zoom, captureFar);
				const auto nextVertical = ResolveBarBottomDockVerticalMapping(
					mainY - 40.5, mainY + 40.5, gripY + 2.0 / zoom, gripY);
				Check(!next.deferred
					&& Near((mainX + nextHorizontal.rigidGripTranslationXDip) * zoom + next.translation.x,
						mainX * zoom + previousTranslation.x + 2.0)
					&& Near(nextVertical.MapY(mainY - 40.5) * zoom + next.translation.y,
						(mainY - 40.5) * zoom + previousTranslation.y + 2.0),
					"next current sample follows the new pointer without an extra capture offset");
				const POINT movedTranslation{ captureTranslation.x + 3, captureTranslation.y + 5 };
				const auto staleMove = ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 8, 8, 2, 4, movedTranslation, movedTranslation);
				Check(!staleMove.deferred && staleMove.translation.x == movedTranslation.x
					&& staleMove.translation.y == movedTranslation.y,
					"stale ordinary frame preserves a direct move after capture is presented");
				present.AddDemand({ true, false, false });
				const auto obsolete = ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 8, 8, 8, 4, movedTranslation, captureTranslation);
				if (obsolete.deferred) present.RequireFullDirtyRetry();
				Check(obsolete.deferred && present.ShouldPresent() && present.NeedsFullDirty()
					&& !present.HasFailureBackoff(),
					"new shape/display barrier skips obsolete ULW without consuming demand or reporting failure");
				Check(ResolveBarBottomDockFramePresentation(
					6, captureTranslation, 7, 7, 8, 4, movedTranslation, captureTranslation).deferred
					&& ResolveBarBottomDockFramePresentation(
						6, captureTranslation, 6, 8, 8, 4, movedTranslation, captureTranslation).deferred,
					"publishing or changing transition tuple cannot submit mixed barrier state");
			}
	}

	bool RectEquals(const RECT& value, const RECT& expected)
	{
		return value.left == expected.left && value.top == expected.top
			&& value.right == expected.right && value.bottom == expected.bottom;
	}
}

int RunBarBottomDockTests()
{
	using namespace Inkeys::UI::Bar;
	TestRealVerticalGrabReplay();
	TestActualHeightReleaseAndAbsorption();
	TestTemporaryBottomDockTrace();
	TestHorizontalCapturePresentedSequence();
	TestFirstCapturePresentationAcrossNewSamples();
	TestOldFloatingFrameCannotAcknowledgeCapture();
	TestBottomDockPublicationWriterOwnership();
	TestInvalidatedFinalCandidateRetainsPresentationDemand();
	TestCaptureBottomUsesSuccessfulRecoveryPixels();
	const RECT monitor{ 100, 50, 2020, 1130 };
	Check(ResolveBarBottomDockLine(monitor, RECT{ 100, 50, 2020, 1082 }) == 1082.0,
		"bottom taskbar uses work-area bottom");
	Check(ResolveBarBottomDockLine(monitor, monitor) == 1130.0,
		"no or auto-hidden taskbar uses monitor bottom");
	Check(ResolveBarBottomDockLine(monitor, RECT{ 148, 50, 2020, 1130 }) == 1130.0,
		"side taskbar does not move dock line");
	Check(ResolveBarBottomDockLine(monitor, RECT{ 100, 98, 2020, 1130 }) == 1130.0,
		"top taskbar does not move dock line");
	Check(ResolveBarBottomDockLine(monitor, RECT{}) == 1130.0,
		"invalid work area falls back to monitor bottom");
	Check(ResolveBarBottomDockLine(
		monitor, RECT{ 99, 50, 2020, 1082 }) == 1130.0,
		"work area outside monitor left falls back");
	Check(ResolveBarBottomDockLine(
		monitor, RECT{ 100, 49, 2020, 1082 }) == 1130.0,
		"work area outside monitor top falls back");
		Check(ResolveBarBottomDockLine(
			monitor, RECT{ 100, 50, 2021, 1082 }) == 1130.0,
			"work area outside monitor right falls back");
		Check(ResolveBarBottomDockLine(
			monitor, RECT{ 100, 50, 2020, 1082 }, 5.0, 1.0) == 1125.0,
			"whiteboard inset uses monitor bottom minus five DIP");
		Check(ResolveBarBottomDockLine(
			monitor, RECT{ 100, 50, 2020, 1082 }, 5.0, 1.5) == 1122.0,
			"whiteboard inset converts DIP with monitor DPI only");
		Check(Near(ResolveBarBottomDockInsetPixels(5.0, 1.0), 5.0)
			&& Near(ResolveBarBottomDockInsetPixels(5.0, 1.5), 8.0),
			"five DIP inset matches page-bar physical margin");

	for (double zoom : { 1.0, 1.5, 1.875 })
	{
		const double center = ResolveBarBottomDockCenterScreenY(
			1082.0, 80.0, 1.0, zoom);
		Check(Near(ResolveBarVisibleBorderBottomScreen(
			center, 80.0, 1.0, zoom), 1082.0),
			"visible stroke bottom aligns exactly at every zoom");
	}
	Check(Near(ResolveBarBottomDockInteractionZoom(96, 1.0), 1.0)
		&& Near(ResolveBarBottomDockInteractionZoom(144, 1.0), 1.5)
		&& Near(ResolveBarBottomDockInteractionZoom(144, 1.25), 1.875),
		"pending DPI and config zoom are combined exactly once");
	Check(Near(ResolveBarBottomDockInteractionZoom(0, 0.0), 1.0),
		"invalid interaction zoom inputs use the stable fallback");
	const double oldRigidGripScreenY = 1000.0 + 15.0 * 1.0;
	const double rebasedElasticDip =
		ResolveBarBottomDockElasticOffsetForScreenGrip(
			oldRigidGripScreenY, 980.0, 1.5);
	Check(Near(980.0 + rebasedElasticDip * 1.5,
		oldRigidGripScreenY),
		"DPI and work-area changes preserve the absolute rigid drag grip");
	Check(Near(ResolveBarBottomDockElasticOffsetForScreenGrip(
		std::numeric_limits<double>::quiet_NaN(), 980.0, 1.5), 0.0)
		&& Near(ResolveBarBottomDockElasticOffsetForScreenGrip(
			1000.0, std::numeric_limits<double>::quiet_NaN(), 1.5),
			BarBottomDockVisualLimitDip)
		&& Near(ResolveBarBottomDockElasticOffsetForScreenGrip(
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::quiet_NaN(), 1.5), 0.0),
		"non-finite physical grip inputs use finite fallbacks");
	Check(ShouldAllowBarBottomDockClick(
		false, false, false, 4.0, 2.5, 1.0)
		&& !ShouldAllowBarBottomDockClick(
			false, true, false, 0.0, 0.0, 1.0)
		&& !ShouldAllowBarBottomDockClick(
			false, false, true, 0.0, 0.0, 1.0),
		"cancelled and mode-changing elastic gestures never trigger clicks");

	const double centeredMainX = ResolveBarBottomDockInitialMainCenterScreenX(
		RECT{ 0, 0, 1000, 600 }, -40.5, 359.5, 40.5, 1.0);
	Check(Near(centeredMainX, 340.5),
		"expanded body centers while main button remains on the left");
	Check(Near(ClampBarBottomDockMainCenterScreenX(
		-100.0, RECT{ 0, 0, 1000, 600 }, 40.5, 1.5), 60.75),
		"horizontal clamp only keeps main button visible");
	Check(Near(ClampBarBottomDockMainCenterScreenX(
		800.0, RECT{ 0, 0, 1000, 600 }, 40.0, 1.0), 800.0)
		&& Near(ClampBarBottomDockMainCenterScreenX(
			100.0, RECT{ 0, 0, 1000, 600 }, 40.0, 1.0), 100.0)
		&& Near(ClampBarBottomDockMainCenterScreenX(
			650.0, RECT{ 0, 0, 1000, 600 }, 40.0, 1.0), 650.0),
		"horizontal reversal remains exact and independent");
	Check(ResolveBarMainBarRightSide(500.0, 1000.0)
		&& ResolveBarMainBarRightSide(499.99, 1000.0)
		&& !ResolveBarMainBarRightSide(500.01, 1000.0),
		"main bar reverses only after crossing the horizontal center line");
	Check(!ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Centered, 100.0, 1000.0, false)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Centered, 900.0, 1000.0, true)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 900.0, 0.0, true)
		&& !ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 100.0,
			std::numeric_limits<double>::quiet_NaN(), false)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 900.0,
			std::numeric_limits<double>::infinity(), true)
		&& ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 100.0, 1000.0, false)
		&& !ResolveBarBottomDockPositionMainBarSide(
			BarBottomDockCenterMode::Free, 900.0, 1000.0, true),
		"centered and invalid-width position updates preserve the current layout side");
	const BarBottomDockFeedbackGeometry dockMainButtonBounds{
		-40.0, 608.0, 40.0, 688.0 };
	const BarBottomDockFeedbackGeometry dockMainBarBounds{
		-40.0, 608.0, 320.0, 688.0 };
	const auto dockIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 52.0, 14.0, 608.0);
	Check(Near(dockIndicator.leftDip, 106.0)
		&& Near(dockIndicator.topDip, 593.0)
		&& Near(dockIndicator.rightDip, 174.0)
		&& Near(dockIndicator.bottomDip, 623.0),
		"dock indicator uses equal text padding and centers on main bar top");
	const auto normalizedDockIndicator = ResolveBarBottomDockIndicatorGeometry(
		BarBottomDockFeedbackGeometry{
			std::numeric_limits<double>::quiet_NaN(), 30.0, -20.0, 10.0 },
		BarBottomDockFeedbackGeometry{ 40.0, 60.0, 10.0, 20.0 },
		52.0, 14.0, std::numeric_limits<double>::quiet_NaN());
	Check(Near(normalizedDockIndicator.leftDip, -24.0)
		&& Near(normalizedDockIndicator.topDip, 5.0)
		&& Near(normalizedDockIndicator.rightDip, 44.0)
		&& Near(normalizedDockIndicator.bottomDip, 35.0),
		"dock indicator normalizes non-finite and reversed visible bounds");
	for (double zoom : { 1.0, 1.5 })
	{
		const double centerPx = (dockIndicator.leftDip
			+ dockIndicator.rightDip) * zoom / 2.0;
		const double topPx = dockIndicator.topDip * zoom;
		const double widthPx = (dockIndicator.rightDip
			- dockIndicator.leftDip) * zoom;
		const double heightPx = (dockIndicator.bottomDip
			- dockIndicator.topDip) * zoom;
		Check(Near(centerPx, 140.0 * zoom)
			&& Near(topPx, 593.0 * zoom)
			&& Near(widthPx, 68.0 * zoom)
			&& Near(heightPx, 30.0 * zoom),
			"dock indicator center top width and height scale exactly once");
	}
	const auto actualWiderIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 120.0, 14.0, 608.0);
	Check(Near(actualWiderIndicator.rightDip - actualWiderIndicator.leftDip,
		136.0), "dock indicator expands only for the current language text");
	const auto invalidMetricsIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds,
		std::numeric_limits<double>::quiet_NaN(), -1.0, 608.0);
	const auto tallTextIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 52.0, 42.0, 608.0);
	Check(Near(invalidMetricsIndicator.rightDip
		- invalidMetricsIndicator.leftDip, 30.0)
		&& Near(tallTextIndicator.rightDip - tallTextIndicator.leftDip, 52.0),
		"dock indicator clamps invalid metrics and never uses negative padding");
	const auto scaledIndicator = ResolveBarBottomDockIndicatorScaledGeometry(
		dockIndicator, 1.1);
	const auto hiddenIndicator = ResolveBarBottomDockIndicatorScaledGeometry(
		dockIndicator, std::numeric_limits<double>::quiet_NaN());
	Check(Near(scaledIndicator.leftDip, 102.6)
		&& Near(scaledIndicator.topDip, 591.5)
		&& Near(scaledIndicator.rightDip, 177.4)
		&& Near(scaledIndicator.bottomDip, 624.5)
		&& Near(hiddenIndicator.leftDip, 140.0)
		&& Near(hiddenIndicator.topDip, 608.0)
		&& Near(hiddenIndicator.rightDip, 140.0)
		&& Near(hiddenIndicator.bottomDip, 608.0),
		"dock indicator scales equally around its center and hides at zero");
	const RECT indicatorVisualEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			dockIndicator, 1.1, 1.0, 6.0, 1.5, 3);
	const RECT indicatorBackPeakEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			dockIndicator, 1.2, 1.0, 6.0, 1.5, 3);
	Check(RectEquals(indicatorVisualEnvelope, RECT{ 139, 873, 281, 951 })
		&& RectEquals(indicatorBackPeakEnvelope,
			RECT{ 134, 871, 286, 953 })
		&& indicatorBackPeakEnvelope.top < indicatorVisualEnvelope.top,
		"indicator envelope covers Back overshoot stroke Gaussian and AA top edge");
	Check(RectEquals(ResolveBarBottomDockIndicatorVisualEnvelope(
		dockIndicator, std::numeric_limits<double>::quiet_NaN(),
		1.0, 6.0, 1.5, 3), RECT{}),
		"invalid indicator scale produces no visual envelope");
	const auto pulledVerticalMapping = ResolveBarBottomDockVerticalMapping(
		608.0, 688.0, -20.0);
	const auto pulledDockIndicator = ResolveBarBottomDockIndicatorGeometry(
		dockMainButtonBounds, dockMainBarBounds, 52.0, 14.0,
		pulledVerticalMapping.MapY(dockMainBarBounds.topDip));
	const RECT pulledIndicatorEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			pulledDockIndicator, 1.0, 1.0, 6.0, 1.0, 3);
	const RECT stableIndicatorEnvelope =
		ResolveBarBottomDockIndicatorVisualEnvelope(
			dockIndicator, 1.0, 1.0, 6.0, 1.0, 3);
	BarDirtyRegionTracker indicatorDirtyTracker;
	constexpr std::uint64_t indicatorDirtyKey = 1;
	const RECT indicatorWindow{ 0, 0, 1920, 1080 };
	indicatorDirtyTracker.BeginFrame(indicatorWindow);
	indicatorDirtyTracker.Observe(indicatorDirtyKey, stableIndicatorEnvelope);
	indicatorDirtyTracker.CommitPresented();
	indicatorDirtyTracker.BeginFrame(indicatorWindow);
	indicatorDirtyTracker.MarkChanged(indicatorDirtyKey);
	indicatorDirtyTracker.Observe(indicatorDirtyKey, pulledIndicatorEnvelope);
	const RECT pulledIndicatorDamage =
		indicatorDirtyTracker.ResolveDamage(false);
	Check(pulledIndicatorEnvelope.top < stableIndicatorEnvelope.top
		&& pulledIndicatorDamage.top <= pulledIndicatorEnvelope.top
		&& pulledIndicatorDamage.bottom >= stableIndicatorEnvelope.bottom,
		"vertical dock jelly damages old and new indicator light envelopes");
	Check(IsBarBottomDockIndicatorHit(true, RECT{ 106, 593, 174, 623 }, 106, 593)
		&& !IsBarBottomDockIndicatorHit(
			true, RECT{ 106, 593, 174, 623 }, 174, 610)
		&& !IsBarBottomDockIndicatorHit(
			false, RECT{ 106, 593, 174, 623 }, 120, 610),
		"indicator hit test follows visible half-open presented bounds");
	Check(ResolveBarBottomDockIndicatorTarget(
		BarBottomDockMode::BottomDocked, true, true)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::BottomDocked, false, true)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::Floating, true, true)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::BottomDocked, true, false)
		&& !ResolveBarBottomDockIndicatorTarget(
			BarBottomDockMode::BottomDocked, true, true, false),
		"dock indicator additionally requires a gesture eligibility latch");
	const auto floatingEntry =
		ResolveBarBottomDockIndicatorGestureEligibility(
			false, true, false, true, true);
	const auto centeredOriginDetach =
		ResolveBarBottomDockIndicatorGestureEligibility(
			false, false, true, true, true);
	const auto sameGestureRecapture =
		ResolveBarBottomDockIndicatorGestureEligibility(
			centeredOriginDetach.gestureActive, false, true, true, true);
	const auto sameGestureUnchanged =
		ResolveBarBottomDockIndicatorGestureEligibility(
			sameGestureRecapture.gestureActive, false, false, true, true);
	const auto leftBottomDock =
		ResolveBarBottomDockIndicatorGestureEligibility(
			sameGestureRecapture.gestureActive, false, false, false, true);
	const auto foldedGesture =
		ResolveBarBottomDockIndicatorGestureEligibility(
			true, false, false, true, false);
	const auto unchangedBottomGesture =
		ResolveBarBottomDockIndicatorGestureEligibility(
			false, false, false, true, true);
	Check(floatingEntry.gestureActive && floatingEntry.eligible
		&& centeredOriginDetach.gestureActive && centeredOriginDetach.eligible
		&& sameGestureRecapture.gestureActive && sameGestureRecapture.eligible
		&& sameGestureUnchanged.gestureActive && sameGestureUnchanged.eligible
		&& !leftBottomDock.gestureActive && !leftBottomDock.eligible
		&& !foldedGesture.gestureActive && !foldedGesture.eligible
		&& !unchangedBottomGesture.gestureActive
		&& !unchangedBottomGesture.eligible,
		"indicator latches on bottom entry or either center-mode transition only");
	Check(ResolveBarBottomDockFeedbackAction(false, true)
		== BarBottomDockFeedbackAction::FadeIn
		&& ResolveBarBottomDockFeedbackAction(true, false)
			== BarBottomDockFeedbackAction::FadeOut
		&& ResolveBarBottomDockFeedbackAction(true, true)
			== BarBottomDockFeedbackAction::None,
		"feedback animations reverse from their current visibility target");
	Check(ResolveBarBottomDockIndicatorContentAction(48.0, 96.0, false)
		== BarBottomDockIndicatorContentAction::ExpandFrame
		&& ResolveBarBottomDockIndicatorContentAction(96.0, 48.0, false)
			== BarBottomDockIndicatorContentAction::SwapText
		&& ResolveBarBottomDockIndicatorContentAction(96.0, 48.0, true)
			== BarBottomDockIndicatorContentAction::ShrinkFrame
		&& ResolveBarBottomDockIndicatorContentAction(48.0, 48.0, true)
			== BarBottomDockIndicatorContentAction::None,
		"indicator expands before swapping wider text and shrinks only after swap");

	BarBottomDockEnvironment environment{
		RECT{ 0, 0, 1920, 1080 }, RECT{ 0, 0, 1920, 1032 }, 1.5 };
	BarBottomDockDragTracker tracker;
	tracker.Begin(BarBottomDockMode::Floating,
		951.0, 1002.0, 981.0, environment);
	auto capturedAbove = tracker.Update(966.0, 1017.0, environment);
	Check(capturedAbove.captured
		&& capturedAbove.mode == BarBottomDockMode::BottomDocked
		&& capturedAbove.phase == BarBottomDockPhase::Capturing
		&& Near(capturedAbove.elasticOffsetDip, -10.0),
		"approach from above captures with stretch offset");
	auto draggingAfterCapture = tracker.Update(966.0, 1017.0, environment);
	Check(draggingAfterCapture.phase == BarBottomDockPhase::Dragging,
		"capturing advances to dragging on the next sample");
	auto exactDetachThreshold = tracker.Update(
		capturedAbove.stableGripScreenY + 30.0, 1047.0, environment);
	Check(!exactDetachThreshold.detached
		&& exactDetachThreshold.mode == BarBottomDockMode::BottomDocked,
		"exact twenty DIP remains docked");
	auto detached = tracker.Update(
		capturedAbove.stableGripScreenY + 30.01, 1047.01, environment);
	Check(detached.detached
		&& detached.mode == BarBottomDockMode::Floating
		&& detached.phase == BarBottomDockPhase::Detaching,
		"more than twenty DIP detaches");
	BarBottomDockDragTracker zoomRebasedTracker;
	zoomRebasedTracker.Begin(BarBottomDockMode::BottomDocked,
		1000.0, 1032.0, 1000.0,
		BarBottomDockEnvironment{ environment.monitorBounds,
			environment.workArea, 1.0 });
	zoomRebasedTracker.RebaseDockGrip(1000.0, 1032.0);
	auto zoomExactThreshold = zoomRebasedTracker.Update(
		1030.0, 1062.0, environment);
	Check(!zoomExactThreshold.detached
		&& zoomExactThreshold.mode == BarBottomDockMode::BottomDocked,
		"zoom rebase keeps the exact twenty DIP detach boundary");

	BarBottomDockDragTracker exactCaptureTracker;
	exactCaptureTracker.Begin(BarBottomDockMode::Floating,
		900.0, 1001.0, 981.0, environment);
	auto exactCapture = exactCaptureTracker.Update(901.0, 1002.0, environment);
	Check(exactCapture.captured
		&& Near(exactCapture.elasticOffsetDip, -20.0),
		"exact twenty DIP captures and zoom is applied once");
	BarBottomDockDragTracker outsideCaptureTracker;
	outsideCaptureTracker.Begin(BarBottomDockMode::Floating,
		900.0, 1000.0, 981.0, environment);
	auto outsideCapture = outsideCaptureTracker.Update(
		900.99, 1000.99, environment);
	Check(!outsideCapture.captured
		&& outsideCapture.mode == BarBottomDockMode::Floating,
		"outside the capture band remains floating");

	tracker.Begin(BarBottomDockMode::Floating,
		1042.0, 1062.0, 1012.0, environment);
	auto capturedBelow = tracker.Update(1032.0, 1052.0, environment);
	Check(capturedBelow.captured && capturedBelow.elasticOffsetDip > 0.0,
		"approach from below captures with compression offset");

	tracker.Begin(BarBottomDockMode::Floating,
		950.0, 980.0, 1000.0, environment);
	auto fastCross = tracker.Update(1035.0, 1065.0, environment);
	Check(fastCross.captured && fastCross.detached
		&& fastCross.mode == BarBottomDockMode::Floating
		&& fastCross.phase == BarBottomDockPhase::Detaching
		&& Near(fastCross.elasticOffsetDip, BarBottomDockThresholdDip),
		"fast segment consumes capture and detach without a docked stall");
	auto fastCrossRecovery = tracker.Update(1036.0, 1066.0, environment);
	Check(fastCrossRecovery.phase == BarBottomDockPhase::Recovering
		&& !fastCrossRecovery.modeChanged,
		"detaching advances to recovering on the next absolute sample");
	tracker.Begin(BarBottomDockMode::Floating,
		1080.0, 1080.0, 1000.0, environment);
	auto fastReverseCross = tracker.Update(950.0, 950.0, environment);
	Check(fastReverseCross.captured && fastReverseCross.detached
		&& fastReverseCross.mode == BarBottomDockMode::Floating
		&& Near(fastReverseCross.elasticOffsetDip,
			-BarBottomDockThresholdDip),
		"fast reverse segment consumes both thresholds symmetrically");

	BarBottomDockCenterDragTracker centerTracker;
	centerTracker.Begin(BarBottomDockCenterMode::Free, 880.0,
		true, true, environment);
	auto exactCenterCapture = centerTracker.Update(1020.0,
		true, true, environment);
	Check(exactCenterCapture.captured
		&& exactCenterCapture.mode == BarBottomDockCenterMode::Centered
		&& Near(exactCenterCapture.constrainedCenterScreenX, 960.0)
		&& Near(exactCenterCapture.elasticOffsetDip, 40.0),
		"horizontal center capture includes the exact forty DIP boundary");
	auto exactCenterDetach = centerTracker.Update(1020.0,
		true, true, environment);
	Check(!exactCenterDetach.detached
		&& exactCenterDetach.mode == BarBottomDockCenterMode::Centered,
		"horizontal center remains captured at the inclusive boundary");
	auto outsideCenterDetach = centerTracker.Update(1020.01,
		true, true, environment);
	Check(outsideCenterDetach.detached
		&& outsideCenterDetach.mode == BarBottomDockCenterMode::Free,
		"horizontal center detaches strictly outside forty DIP");
	centerTracker.Begin(BarBottomDockCenterMode::Free, 880.0,
		true, true, environment);
	const auto stableCapture = centerTracker.Update(950.0,
		true, true, environment);
	const auto repeatedInside = centerTracker.Update(950.0,
		true, true, environment);
	const auto pushedInside = centerTracker.Update(970.0,
		true, true, environment);
	const auto stableDetach = centerTracker.Update(1021.0,
		true, true, environment);
	const auto stableRecapture = centerTracker.Update(1019.0,
		true, true, environment);
	Check(stableCapture.mode == BarBottomDockCenterMode::Centered
		&& repeatedInside.mode == BarBottomDockCenterMode::Centered
		&& !repeatedInside.modeChanged
		&& pushedInside.mode == BarBottomDockCenterMode::Centered
		&& stableDetach.mode == BarBottomDockCenterMode::Free
		&& stableRecapture.mode == BarBottomDockCenterMode::Centered,
		"raw pointer samples do not oscillate after a presented barrier");
	BarBottomDockCenterDragTracker foldedCenterTracker;
	foldedCenterTracker.Begin(BarBottomDockCenterMode::Free, 960.0,
		true, false, environment);
	const auto foldedCenter = foldedCenterTracker.Update(960.0,
		true, false, environment);
	Check(foldedCenter.mode == BarBottomDockCenterMode::Free
		&& !foldedCenter.captured
		&& !ShouldCaptureBarBottomDockCenter(
			true, false, 960.0, environment.monitorBounds, environment.zoom),
		"folded main bar cannot enter centered mode");
	Check(ShouldCaptureBarBottomDockCenter(
		true, true, 900.0, environment.monitorBounds, environment.zoom)
		&& ShouldCaptureBarBottomDockCenter(
			true, true, 960.0, environment.monitorBounds, environment.zoom)
		&& ShouldCaptureBarBottomDockCenter(
		true, true, 1020.0, environment.monitorBounds, environment.zoom)
		&& !ShouldCaptureBarBottomDockCenter(
			true, true, 899.99, environment.monitorBounds, environment.zoom)
		&& !ShouldCaptureBarBottomDockCenter(
			true, true, 1020.01, environment.monitorBounds, environment.zoom)
		&& !ShouldCaptureBarBottomDockCenter(
			false, true, 960.0, environment.monitorBounds, environment.zoom),
		"horizontal capture uses the forty DIP monitor-center band and dock gate");

	const auto stretched = ResolveBarBottomDockVerticalMapping(0.0, 80.0, -20.0);
	Check(Near(stretched.visualTopDip, -20.0) && Near(stretched.scaleY, 1.25)
		&& Near(stretched.rigidGripYDip, 20.0)
		&& Near(stretched.MapY(40.0), 30.0)
		&& Near(stretched.UnmapY(30.0), 40.0),
		"upward grip stretches body while rigid grip follows full delta");
	const auto compressed = ResolveBarBottomDockVerticalMapping(0.0, 80.0, 20.0);
	Check(Near(compressed.visualTopDip, 20.0) && Near(compressed.scaleY, 0.75)
		&& Near(compressed.rigidOverlayTranslationYDip, 20.0),
		"downward grip compresses body and translates rigid overlays");
	const auto captureAboveStart = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, -12.0, -12.0);
	const auto captureAboveProgress = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, -12.0, -6.0);
	Check(Near(captureAboveStart.visualTopDip, -12.0)
		&& Near(captureAboveStart.visualBottomDip, 68.0)
		&& Near(captureAboveStart.scaleY, 1.0)
		&& Near(captureAboveProgress.visualTopDip, -12.0)
		&& Near(captureAboveProgress.visualBottomDip, 74.0),
		"capture from above keeps the first frame continuous then animates the bottom edge");
	const auto captureBelowStart = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, 12.0, 12.0);
	const auto captureBelowProgress = ResolveBarBottomDockVerticalMapping(
		0.0, 80.0, 12.0, 6.0);
	Check(Near(captureBelowStart.visualTopDip, 12.0)
		&& Near(captureBelowStart.visualBottomDip, 92.0)
		&& Near(captureBelowStart.scaleY, 1.0)
		&& Near(captureBelowProgress.visualTopDip, 12.0)
		&& Near(captureBelowProgress.visualBottomDip, 86.0),
		"capture from below keeps the first frame continuous then compresses toward dock");
	const auto detachedDown = ResolveBarBottomDockRecoveringVerticalMapping(
		20.0, 100.0, 20.0);
	Check(Near(detachedDown.visualTopDip, 20.0)
		&& Near(detachedDown.visualBottomDip, 80.0)
		&& Near(detachedDown.scaleY, 0.75)
		&& Near(detachedDown.rigidOverlayTranslationYDip, 0.0),
		"downward detach preserves the compressed frame while the window moves");
	const auto detachedUp = ResolveBarBottomDockRecoveringVerticalMapping(
		-20.0, 60.0, -20.0);
	Check(Near(detachedUp.visualTopDip, -20.0)
		&& Near(detachedUp.visualBottomDip, 80.0)
		&& Near(detachedUp.scaleY, 1.25),
		"upward detach preserves the stretched frame while the window moves");
	const auto rightExpandedStretch = ResolveBarBottomDockHorizontalMapping(
		180.0, 500.0, true, -20.0);
	const auto rightExpandedCompress = ResolveBarBottomDockHorizontalMapping(
		180.0, 500.0, true, 20.0);
	const auto leftExpandedStretch = ResolveBarBottomDockHorizontalMapping(
		100.0, 420.0, false, 20.0);
	Check(Near(rightExpandedStretch.visualLeftDip, 160.0)
		&& Near(rightExpandedStretch.visualRightDip, 500.0)
		&& Near(rightExpandedCompress.visualLeftDip, 200.0)
		&& Near(rightExpandedCompress.visualRightDip, 500.0)
		&& Near(leftExpandedStretch.visualLeftDip, 100.0)
		&& Near(leftExpandedStretch.visualRightDip, 440.0)
		&& Near(rightExpandedStretch.rigidGripTranslationXDip, -20.0),
		"horizontal jelly moves the near edge with the rigid grip and fixes the far edge");
	Check(ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			false, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Free,
			BarBottomDockPhase::Stable, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Capturing, false, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, true, true,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, false,
			false, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			true, false, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, true, false)
		&& !ShouldDeriveBarBottomDockCenteredRoot(
			true, BarBottomDockCenterMode::Centered,
			BarBottomDockPhase::Stable, false, true,
			false, false, true),
		"only a stable idle centered bottom dock derives the root position");
	const auto centeredRight = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 80.0, 250.0, 400.0);
	const auto centeredLeft = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 80.0, -250.0, 400.0);
	const auto centeredStroke = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 82.0, 251.0, 402.0);
	const auto invalidCenteredRoot = ResolveBarBottomDockCenteredRootPlacement(
		960.0, 0.0, 250.0, 400.0);
	const auto centeredRootRange = ResolveBarBottomDockCenteredRootRange(
		960.0, 82.0, 82.0, 150.0, 251.0, 202.0, 402.0);
	const auto invalidCenteredRootRange = ResolveBarBottomDockCenteredRootRange(
		960.0, 82.0, 82.0, 251.0, 150.0, 202.0, 402.0);
	Check(centeredRight.valid && Near(centeredRight.mainCenterDip, 755.0)
		&& Near(centeredRight.bodyLeftDip, 715.0)
		&& Near(centeredRight.bodyRightDip, 1205.0)
		&& Near((centeredRight.bodyLeftDip + centeredRight.bodyRightDip)
			/ 2.0, 960.0)
		&& centeredLeft.valid && Near(centeredLeft.mainCenterDip, 1165.0)
		&& Near(centeredLeft.bodyLeftDip, 715.0)
		&& Near(centeredLeft.bodyRightDip, 1205.0),
		"centered root preserves the visible union center on either side");
	Check(centeredStroke.valid
		&& Near(centeredStroke.mainCenterDip, 754.5)
		&& Near((centeredStroke.bodyLeftDip + centeredStroke.bodyRightDip)
			/ 2.0, 960.0)
		&& !invalidCenteredRoot.valid,
		"centered root includes visible strokes and rejects invalid geometry");
	Check(centeredRootRange.valid
		&& Near(centeredRootRange.minimumDip, 754.5)
		&& Near(centeredRootRange.maximumDip, 860.0)
		&& centeredStroke.mainCenterDip >= centeredRootRange.minimumDip
		&& centeredStroke.mainCenterDip <= centeredRootRange.maximumDip
		&& !invalidCenteredRootRange.valid,
		"centered root range conservatively reserves the full animated parent travel");
	Check(ResolveBarBottomDockInitialMainBarSide(false, false)
		&& ResolveBarBottomDockInitialMainBarSide(false, true)
		&& !ResolveBarBottomDockInitialMainBarSide(true, false)
		&& ResolveBarBottomDockInitialMainBarSide(true, true),
		"desktop initial placement opens right while whiteboard preserves its side");
	const auto horizontalCaptureStart = ResolveBarBottomDockHorizontalMapping(
		180.0, 500.0, true, -20.0, -20.0);
	const double detachedFarOffset = ResolveBarBottomDockRebasedFarEdgeOffsetDip(
		rightExpandedCompress.visualRightDip, 0.0, 20.0, 500.0, 1.0);
	const auto horizontalDetachStart =
		ResolveBarBottomDockRecoveringHorizontalMapping(
			180.0, 500.0, true, detachedFarOffset);
	Check(Near(horizontalCaptureStart.visualLeftDip, 160.0)
		&& Near(horizontalCaptureStart.visualRightDip, 480.0)
		&& Near(horizontalCaptureStart.rigidGripTranslationXDip, -20.0)
		&& Near(horizontalDetachStart.visualLeftDip + 20.0,
			rightExpandedCompress.visualLeftDip)
		&& Near(horizontalDetachStart.visualRightDip + 20.0,
			rightExpandedCompress.visualRightDip),
		"horizontal capture and detach preserve the previous screen-space pixels");

	const RECT logicalBody{ 10, 0, 110, 80 };
	Check(RectEquals(TransformBarBottomDockBodyRect(
		logicalBody, compressed, 1.5), RECT{ 10, 30, 110, 90 }),
		"body dirty bounds use the same endpoint mapping as drawing");
	Check(RectEquals(TransformBarBottomDockBodyRect(
		RECT{ 15, 0, 165, 120 },
		ResolveBarBottomDockHorizontalMapping(10.0, 110.0, true, 20.0),
		compressed, 1.5), RECT{ 45, 30, 165, 120 }),
		"two-axis dirty bounds use the same composed mapping as drawing");
	Check(RectEquals(TransformBarBottomDockGripRect(
		logicalBody,
		ResolveBarBottomDockHorizontalMapping(10.0, 110.0, true, 20.0),
		compressed, 1.5), RECT{ 40, 30, 140, 90 }),
		"main grip keeps its horizontal size while composing vertical jelly");
	Check(Near(UnmapBarBottomDockGripPixelX(
		90.0, ResolveBarBottomDockHorizontalMapping(
			10.0, 110.0, true, 20.0), 1.5), 60.0),
		"main grip hit testing removes only the rigid horizontal translation");
	Check(RectEquals(TranslateBarBottomDockRigidRect(
		logicalBody, 20.0, 1.5), RECT{ 10, 30, 110, 110 }),
		"rigid panel dirty bounds only translate");
	Check(RectEquals(TranslateBarBottomDockRigidRect(
		logicalBody, -20.0, 20.0, 1.5), RECT{ -20, 30, 80, 110 }),
		"rigid panel dirty bounds compose horizontal and vertical translation");
	Check(RectEquals(ResolveBarBottomDockVisualEnvelope(
		RECT{ 10, 20, 110, 120 }, 1.5), RECT{ -26, -16, 146, 156 }),
		"elastic capacity reserves the full two-axis twenty-four DIP envelope once");
	Check(RectEquals(ResolveBarBottomDockCapacityEnvelope(
		RECT{ 10, 50, 110, 150 }, 1.5),
		RECT{ -26, 14, 146, 186 }),
		"capacity envelope is derived from the untransformed baseline");
	Check(ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
		true, 1040.0, 1039.25, 1039.0)
		&& !ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
			true, 1040.0, 1039.25, 1000.0)
		&& !ShouldKeepBarBottomDockedAfterBlockedDownwardRelease(
			false, 1040.0, 1039.25, 1039.0),
		"downward release re-docks only when the visible constraint blocks it");
	const POINT latestTranslation{ 30, 40 };
	const POINT presentedTranslation{ 17, 23 };
	const POINT currentFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 4, 4, latestTranslation, presentedTranslation);
	Check(currentFrameTranslation.x == 30 && currentFrameTranslation.y == 40,
		"current transition frame may consume the latest drag translation");
	const POINT staleFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 6, 6, latestTranslation, presentedTranslation);
	const POINT publishingFrameTranslation = ResolveBarBottomDockFrameTranslation(
		4, 5, 5, latestTranslation, presentedTranslation);
	Check(staleFrameTranslation.x == 17 && staleFrameTranslation.y == 23
		&& publishingFrameTranslation.x == 17
		&& publishingFrameTranslation.y == 23,
		"stale or in-flight transition frames retain the actual HWND translation");
	Check(ShouldDeferBarBottomDockReleaseHandoff(false, true, true)
		&& !ShouldDeferBarBottomDockReleaseHandoff(true, true, true)
		&& !ShouldDeferBarBottomDockReleaseHandoff(false, false, true)
		&& !ShouldDeferBarBottomDockReleaseHandoff(false, true, false),
		"release waits only while pending direct translation still owns the window");
	const double mappedHorizontalSample = MapBarBottomDockBodyPixelX(
		315.0, rightExpandedStretch, 1.5);
	const double mappedSample = MapBarBottomDockBodyPixelY(
		67.5, stretched, 1.5);
	Check(Near(UnmapBarBottomDockBodyPixelX(
			mappedHorizontalSample, rightExpandedStretch, 1.5), 315.0)
		&& Near(UnmapBarBottomDockBodyPixelY(
			mappedSample, stretched, 1.5), 67.5),
		"body hit testing inverts both non-identity drawing axes");
	const BarBottomDockHorizontalMapping hitHorizontal{
		10.0, 110.0, 30.0, 90.0, 0.6, 12.0, -8.0 };
	const BarBottomDockVerticalMapping hitVertical{
		20.0, 100.0, 35.0, 95.0, 0.75, 0.0, 6.0 };
	const auto rigidHit = ResolveBarBottomDockRigidHitTestPoint(
		180.0, 150.0, hitHorizontal, hitVertical, 1.5);
	const auto bodyHit = ResolveBarBottomDockBodyHitTestPointFromRigid(
		rigidHit.logicalX, rigidHit.logicalY,
		hitHorizontal, hitVertical, 1.5);
	const auto gripHit = ResolveBarBottomDockGripHitTestPointFromRigid(
		rigidHit.logicalX, rigidHit.logicalY,
		hitHorizontal, hitVertical, 1.5);
	Check(Near(rigidHit.logicalX, 192.0)
		&& Near(rigidHit.logicalY, 141.0)
		&& Near(bodyHit.visualX, 180.0)
		&& Near(bodyHit.visualY, 150.0)
		&& Near(bodyHit.logicalX,
			UnmapBarBottomDockBodyPixelX(180.0, hitHorizontal, 1.5))
		&& Near(bodyHit.logicalY,
			UnmapBarBottomDockBodyPixelY(150.0, hitVertical, 1.5))
		&& Near(gripHit.logicalX,
			UnmapBarBottomDockGripPixelX(180.0, hitHorizontal, 1.5))
		&& Near(gripHit.logicalY, bodyHit.logicalY),
		"combined body and grip hit tests map both axes from one tuple");
	const auto bodyLight = ResolveBarBottomDockBodyLocalLight(
		240.0, mappedSample, 90.0, stretched, 1.5);
	Check(Near(MapBarBottomDockBodyPixelY(
		bodyLight.centerY, stretched, 1.5), mappedSample)
		&& Near(bodyLight.centerX, 240.0)
		&& Near(bodyLight.radiusY * stretched.scaleY, 90.0),
		"body cursor light inverse mapping preserves its visual center and radius");
	const auto rigidLight = ResolveBarBottomDockRigidLocalLight(
		240.0, 315.0, 90.0, -20.0, 20.0, 1.5);
	Check(Near(rigidLight.centerY + 20.0 * 1.5, 315.0)
		&& Near(rigidLight.centerX - 20.0 * 1.5, 240.0)
		&& Near(rigidLight.radiusX, 90.0)
		&& Near(rigidLight.radiusY, 90.0),
		"rigid cursor light inverse translation preserves screen geometry");

	auto CheckDetachContinuity = [&](double offsetDip,
		double captureBottomOffsetDip, std::string_view name)
		{
			const double zoom = 1.5;
			const auto dockedMapping = ResolveBarBottomDockVerticalMapping(
				100.0, 180.0, offsetDip, captureBottomOffsetDip);
			const auto recoveryMapping =
				ResolveBarBottomDockRecoveringVerticalMapping(
					100.0, 180.0, offsetDip, captureBottomOffsetDip);
			const double windowShiftPx = offsetDip * zoom;
			const double dockedTopPx = dockedMapping.visualTopDip * zoom;
			const double dockedBottomPx = dockedMapping.visualBottomDip * zoom;
			const double recoveryTopPx = recoveryMapping.visualTopDip * zoom
				+ windowShiftPx;
			const double recoveryBottomPx = recoveryMapping.visualBottomDip * zoom
				+ windowShiftPx;
			const double dockedGripPx = dockedMapping.rigidGripYDip * zoom;
			const double recoveryGripPx = recoveryMapping.rigidGripYDip * zoom
				+ windowShiftPx;
			Check(Near(dockedTopPx, recoveryTopPx)
				&& Near(dockedBottomPx, recoveryBottomPx)
				&& Near(dockedGripPx, recoveryGripPx), name);
		};
	CheckDetachContinuity(20.0, 0.0,
		"downward detach preserves body and rigid grip in screen space");
	CheckDetachContinuity(-20.0, 0.0,
		"upward detach preserves body and rigid grip in screen space");
	CheckDetachContinuity(-12.0, -6.0,
		"detach during capture preserves the animated bottom edge");

	BarBottomDockSpringState spring{ 20.0, 0.0 };
	bool sawOvershoot = false;
	bool active = true;
	for (int frame = 0; frame < 240 && active; ++frame)
	{
		const auto result = AdvanceBarBottomDockSpring(
			spring, 0.0, 1.0 / 60.0, true);
		active = result.active;
		sawOvershoot |= result.positionDip < 0.0;
	}
	Check(sawOvershoot, "underdamped spring has a gentle overshoot");
	Check(!active && Near(spring.positionDip, 0.0)
		&& Near(spring.velocityDipPerSecond, 0.0),
		"spring settles and stops requesting frames");
	BarBottomDockSpringState captureBottomSpring{ -12.0, 0.0 };
	const auto captureBottomFirstFrame = AdvanceBarBottomDockSpring(
		captureBottomSpring, 0.0, 1.0 / 60.0, true);
	Check(captureBottomFirstFrame.active
		&& captureBottomFirstFrame.positionDip > -12.0
		&& captureBottomFirstFrame.positionDip < 0.0,
		"captured bottom edge advances over time instead of snapping to dock");
	BarBottomDockSpringState disabledSpring{ -15.0, 100.0 };
	const auto disabled = AdvanceBarBottomDockSpring(
		disabledSpring, 0.0, 0.016, false);
	Check(!disabled.active && Near(disabled.positionDip, 0.0),
		"disabled animations settle immediately");
	BarBottomDockSpringState clampedDt{ 20.0, -15.0 };
	BarBottomDockSpringState exactDt = clampedDt;
	(void)AdvanceBarBottomDockSpring(clampedDt, 0.0, 0.5, true);
	(void)AdvanceBarBottomDockSpring(
		exactDt, 0.0, BarBottomDockSpringMaxDtSeconds, true);
	Check(Near(clampedDt.positionDip, exactDt.positionDip)
		&& Near(clampedDt.velocityDipPerSecond,
			exactDt.velocityDipPerSecond),
		"spring clamps queued frame time to thirty-two milliseconds");

	return failureCount;
}
