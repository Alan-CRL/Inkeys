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
	TestBatchExpansionAndIdleShrink();
	TestLayoutClipping();
	TestCapacityFallbackExpansion();
	TestReservedEnvelopeStaysInsideCapacity();
	return failureCount;
}
