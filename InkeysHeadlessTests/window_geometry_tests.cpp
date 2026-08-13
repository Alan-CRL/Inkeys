#include "../Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h"

#include <iostream>
#include <string_view>

namespace
{
	using namespace Inkeys::UI::Bar;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	void TestCoordinateRoundTrip()
	{
		constexpr RECT viewport{ 120, 240, 620, 640 };
		constexpr POINT client{ 15, 25 };
		constexpr POINT layout = BarClientToLayoutPoint(client, viewport);
		constexpr POINT roundTrip = BarLayoutToClientPoint(layout, viewport);
		Check(layout.x == 135 && layout.y == 265,
			"client point maps to stable layout coordinates");
		Check(roundTrip.x == client.x && roundTrip.y == client.y,
			"layout and client point mapping round trips");

		constexpr RECT layoutRect{ 140, 260, 200, 320 };
		constexpr RECT clientRect = BarLayoutToClientRect(layoutRect, viewport);
		constexpr RECT surfaceRect = BarLayoutToSurfaceRect(
			layoutRect, POINT{ 100, 200 });
		Check(SameBarWindowRect(clientRect, RECT{ 20, 20, 80, 80 }),
			"layout damage maps into viewport-local coordinates");
		Check(SameBarWindowRect(surfaceRect, RECT{ 40, 60, 100, 120 }),
			"layout damage maps into capacity-surface coordinates");

		constexpr POINT negativeMonitorOrigin{ -1920, -200 };
		constexpr POINT screenPoint{ -1800, -50 };
		constexpr POINT monitorLayout = BarScreenToLayoutPoint(
			screenPoint, negativeMonitorOrigin);
		constexpr POINT screenRoundTrip = BarLayoutToScreenPoint(
			monitorLayout, negativeMonitorOrigin);
		Check(monitorLayout.x == 120 && monitorLayout.y == 150,
			"negative monitor origin maps into monitor-local layout coordinates");
		Check(screenRoundTrip.x == screenPoint.x
			&& screenRoundTrip.y == screenPoint.y,
			"screen and layout point mapping round trips");
	}

	void TestAnimationEnvelopeUsesSegmentDelta()
	{
		const auto monotonic = ResolveBarWindowSegmentRange(
			1000.0, 1100.0, 0.0, 1.0);
		Check(monotonic.minimum == 1000.0 && monotonic.maximum == 1100.0,
			"monotonic animation reserves only its real endpoints");

		const auto rebound = ResolveBarWindowSegmentRange(
			1000.0, 1100.0, 0.0, 1.05);
		Check(rebound.minimum == 1000.0 && rebound.maximum == 1105.0,
			"Back overshoot applies to segment delta instead of absolute position");

		const auto stable = ResolveBarWindowAnimationRange(
			1100.0, 1000.0, 1100.0, false, 0.0,
			{ 0.0, 1.05 }, { 0.0, 1.0 });
		Check(stable.minimum == 1100.0 && stable.maximum == 1100.0,
			"settled values ignore historical animation segments");

		const auto keyframes = ResolveBarWindowAnimationRange(
			1020.0, 1000.0, 1080.0, true, 1050.0,
			{ 0.0, 1.0 }, { 0.0, 1.1 });
		Check(keyframes.minimum == 1000.0 && keyframes.maximum == 1083.0,
			"keyframe animation merges each segment's own overshoot");

		const RECT envelope = ResolveBarWindowAnimatedRect(
			{ 500.0, 520.0 }, { 300.0, 310.0 },
			{ 80.0, 100.0 }, { 60.0, 70.0 }, 1.0, 8);
		Check(SameBarWindowRect(envelope, RECT{ 442, 257, 578, 353 }),
			"animated root bounds include maximum size and real visual outset");
	}

	void TestThicknessPreviewInteractionEnvelope()
	{
		Check(ResolveBarThicknessPreviewReservationMode(
			true, true, false, false, false)
				== BarThicknessPreviewReservationMode::Interaction,
			"active gesture reserves the full thickness interaction domain");
		Check(ResolveBarThicknessPreviewReservationMode(
			true, false, true, false, false)
				== BarThicknessPreviewReservationMode::Target,
			"visible popup reserves a known programmatic thickness target");
		Check(ResolveBarThicknessPreviewReservationMode(
			false, false, true, true, true)
				== BarThicknessPreviewReservationMode::None,
			"hidden popup does not expand for thickness-only animation");

		const RECT maximumPopup = ResolveBarThicknessPreviewEnvelope({
			500.0, 420.0, 490.0, 330.0,
			100.0, 24.0, 18.0, 8.0, 5.0,
			1.05, 2.0, 10 });
		Check(SameBarWindowRect(
			maximumPopup, RECT{ 816, 519, 1142, 783 }),
			"maximum thickness popup reserves outside-number and Back envelope");

		constexpr RECT layoutBounds{ 0, 0, 1920, 1080 };
		BarWindowViewportController controller;
		auto pressed = controller.Resolve(
			RECT{ 700, 500, 1000, 700 }, maximumPopup,
			layoutBounds, 2, false);
		controller.Commit(pressed.viewport);
		auto dragged = controller.Resolve(
			RECT{ 720, 510, 1080, 740 }, maximumPopup,
			layoutBounds, 2, false);
		Check(!dragged.changed
			&& SameBarWindowRect(dragged.viewport, pressed.viewport),
			"maximum thickness reservation prevents per-value viewport resize");
	}

	void TestViewportResizeKeepsLayoutInputStable()
	{
		constexpr POINT monitorOrigin{ -1920, -200 };
		constexpr POINT screenPoint{ -1320, 360 };
		constexpr RECT oldViewport{ 480, 420, 980, 720 };
		constexpr RECT expandedViewport{ 300, 160, 1100, 720 };
		constexpr POINT layout = BarScreenToLayoutPoint(
			screenPoint, monitorOrigin);
		constexpr POINT oldClient = BarLayoutToClientPoint(layout, oldViewport);
		constexpr POINT expandedClient = BarLayoutToClientPoint(
			layout, expandedViewport);
		Check(oldClient.x != expandedClient.x || oldClient.y != expandedClient.y,
			"client coordinates change when the dynamic viewport expands");
		Check(BarClientToLayoutPoint(oldClient, oldViewport).x == layout.x
			&& BarClientToLayoutPoint(oldClient, oldViewport).y == layout.y
			&& BarClientToLayoutPoint(expandedClient, expandedViewport).x == layout.x
			&& BarClientToLayoutPoint(expandedClient, expandedViewport).y == layout.y,
			"screen-derived layout input remains stable across viewport resize");

		constexpr POINT directTranslation{ 180, -40 };
		constexpr POINT translatedLayout = BarScreenToLayoutPoint(
			screenPoint, monitorOrigin, directTranslation);
		Check(translatedLayout.x == layout.x - directTranslation.x
			&& translatedLayout.y == layout.y - directTranslation.y,
			"unabsorbed direct-window movement preserves layout hit coordinates");
		constexpr POINT followUpDelta = ResolveBarDirectWindowMoveDelta(
			POINT{ 260, 20 }, directTranslation);
		Check(followUpDelta.x == 80 && followUpDelta.y == 60,
			"rapid drag appends only the translation not already presented");
		constexpr POINT afterAbsorb =
			ResolveBarDirectWindowTranslationAfterAbsorb(
				POINT{ 180, -40 }, POINT{ 260, 20 });
		Check(afterAbsorb.x == -80 && afterAbsorb.y == -60,
			"layout absorption retains the HWND offset until the next ULW commit");
	}

	void TestBatchExpansionAndIdleShrink()
	{
		constexpr RECT layoutBounds{ 0, 0, 1920, 1080 };
		BarWindowViewportController controller;
		auto initial = controller.Resolve(
			RECT{ 800, 400, 900, 500 }, RECT{ 780, 380, 1200, 720 },
			layoutBounds, 4, false);
		Check(initial.changed
			&& SameBarWindowRect(initial.viewport, RECT{ 776, 376, 1204, 724 }),
			"batch begins with one predicted expansion");
		controller.Commit(initial.viewport);

		auto interior = controller.Resolve(
			RECT{ 810, 410, 930, 520 }, RECT{ 800, 400, 1100, 680 },
			layoutBounds, 4, false);
		Check(!interior.changed
			&& SameBarWindowRect(interior.viewport, initial.viewport),
			"animation frames inside the reservation do not resize");

		auto redirect = controller.Resolve(
			RECT{ 810, 410, 930, 520 }, RECT{ 700, 350, 1100, 680 },
			layoutBounds, 4, false);
		Check(redirect.changed
			&& SameBarWindowRect(redirect.viewport, RECT{ 696, 346, 1204, 724 }),
			"redirect expands only the newly exceeded side");
		controller.Commit(redirect.viewport);

		auto settled = controller.Resolve(
			RECT{ 820, 420, 920, 520 }, RECT{ 0, 0, 1920, 1080 },
			layoutBounds, 4, true);
		Check(settled.changed
			&& SameBarWindowRect(settled.viewport, RECT{ 816, 416, 924, 524 }),
			"idle frame shrinks once to current content");
		controller.Commit(settled.viewport);

		auto hover = controller.Resolve(
			RECT{ 820, 420, 920, 520 }, RECT{}, layoutBounds, 4, false);
		Check(!hover.changed && SameBarWindowRect(hover.viewport, settled.viewport),
			"ordinary hover inside settled content does not reserve full capacity");

		auto moved = controller.Resolve(
			RECT{ 900, 480, 1000, 580 }, RECT{}, layoutBounds, 4, false,
			POINT{ 80, 60 });
		Check(moved.viewport.right - moved.viewport.left
				== settled.viewport.right - settled.viewport.left
			&& moved.viewport.bottom - moved.viewport.top
				== settled.viewport.bottom - settled.viewport.top,
			"whole-Bar drag translates the reservation without resizing");
	}

	void TestLayoutClipping()
	{
		constexpr RECT layoutBounds{ 0, 0, 1000, 800 };
		BarWindowViewportController controller;
		auto clipped = controller.Resolve(
			RECT{ -20, -10, 80, 90 }, RECT{ -50, -40, 1100, 900 },
			layoutBounds, 8, false);
		Check(SameBarWindowRect(clipped.viewport, layoutBounds),
			"capacity and viewport never escape monitor-local layout bounds");
	}

	void TestCapacityFallbackExpansion()
	{
		constexpr RECT layoutBounds{ 0, 0, 1920, 1080 };
		auto unchanged = ResolveBarWindowCapacity(
			SIZE{ 600, 400 }, POINT{ 960, 540 },
			RECT{ 800, 480, 1100, 620 }, layoutBounds, 2);
		Check(!unchanged.sizeChanged
			&& unchanged.size.cx == 600 && unchanged.size.cy == 400,
			"content inside predicted capacity reuses the target size");
		Check(unchanged.origin.x == 660 && unchanged.origin.y == 340,
			"capacity remains anchored around the main button");

		auto expanded = ResolveBarWindowCapacity(
			SIZE{ 400, 240 }, POINT{ 500, 400 },
			RECT{ 450, 350, 820, 470 }, layoutBounds, 2);
		RECT expandedBounds{
			expanded.origin.x, expanded.origin.y,
			expanded.origin.x + expanded.size.cx,
			expanded.origin.y + expanded.size.cy };
		Check(expanded.sizeChanged && expanded.size.cx >= 644,
			"content escape expands target once around the anchor");
		Check(ContainsBarWindowRect(expandedBounds,
			RECT{ 448, 348, 822, 472 }),
			"expanded capacity contains padded visible content");

		auto edge = ResolveBarWindowCapacity(
			SIZE{ 100, 100 }, POINT{ 20, 20 },
			RECT{ 0, 0, 90, 80 }, layoutBounds, 2);
		RECT edgeBounds{
			edge.origin.x, edge.origin.y,
			edge.origin.x + edge.size.cx,
			edge.origin.y + edge.size.cy };
		Check(ContainsBarWindowRect(edgeBounds, RECT{ 0, 0, 92, 82 }),
			"capacity source remains valid for content near a monitor edge");
	}

	void TestReservedEnvelopeStaysInsideCapacity()
	{
		constexpr RECT layoutBounds{ 0, 0, 1920, 1080 };
		constexpr POINT capacityOrigin{ 640, 320 };
		constexpr SIZE capacitySize{ 640, 440 };
		constexpr LONG padding = 2;
		const RECT capacityBounds{
			capacityOrigin.x, capacityOrigin.y,
			capacityOrigin.x + capacitySize.cx,
			capacityOrigin.y + capacitySize.cy };
		const RECT reservedEnvelope = DeflateBarWindowRect(
			capacityBounds, padding);

		BarWindowViewportController controller;
		const auto decision = controller.Resolve(
			RECT{ 900, 500, 1020, 580 }, reservedEnvelope,
			layoutBounds, padding, false);
		const POINT source{
			decision.viewport.left - capacityOrigin.x,
			decision.viewport.top - capacityOrigin.y };
		Check(ContainsBarWindowRect(capacityBounds, decision.viewport),
			"reserved viewport remains inside the allocated target capacity");
		Check(source.x >= 0 && source.y >= 0
			&& source.x + decision.viewport.right - decision.viewport.left
				<= capacitySize.cx
			&& source.y + decision.viewport.bottom - decision.viewport.top
				<= capacitySize.cy,
			"ULW source offset and size stay inside the target bitmap");
	}
}

int RunWindowGeometryTests()
{
	TestCoordinateRoundTrip();
	TestAnimationEnvelopeUsesSegmentDelta();
	TestThicknessPreviewInteractionEnvelope();
	TestViewportResizeKeepsLayoutInputStable();
	TestBatchExpansionAndIdleShrink();
	TestLayoutClipping();
	TestCapacityFallbackExpansion();
	TestReservedEnvelopeStaysInsideCapacity();
	return failureCount;
}
