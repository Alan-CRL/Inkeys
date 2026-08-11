#include "../Inkeys/Inkeys/UI/Bar/Bar.DirtyRegion.h"

#include <iostream>
#include <string_view>

namespace
{
	using Inkeys::UI::Bar::BarDirtyRegionTracker;
	using Inkeys::UI::Bar::ResolveBarDebugDamage;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	[[nodiscard]] constexpr bool SameRect(
		const RECT& left, const RECT& right) noexcept
	{
		return left.left == right.left && left.top == right.top
			&& left.right == right.right && left.bottom == right.bottom;
	}

	void TestInitialAndFallbackDamage()
	{
		constexpr RECT window{ 0, 0, 1920, 1080 };
		BarDirtyRegionTracker tracker;
		tracker.BeginFrame(window);
		Check(SameRect(tracker.ResolveDamage(false), window),
			"first frame is full damage");
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		Check(BarDirtyRegionTracker::IsEmpty(tracker.ResolveDamage(false)),
			"idle frame has no damage");
		Check(SameRect(tracker.ResolveDamage(true), window),
			"unclassified demand falls back to full damage");
	}

	void TestChangedBoundsUnionAndClipping()
	{
		constexpr RECT window{ 0, 0, 100, 80 };
		constexpr std::uint64_t control = 1;
		BarDirtyRegionTracker tracker;
		tracker.BeginFrame(window);
		tracker.Observe(control, RECT{ 10, 10, 30, 30 });
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		tracker.Observe(control, RECT{ 20, 5, 120, 40 });
		tracker.MarkChanged(control);
		Check(SameRect(tracker.ResolveDamage(false), RECT{ 10, 5, 100, 40 }),
			"changed control unions old and clipped new bounds");
	}

	void TestAppearanceDisappearanceAndMultipleKeys()
	{
		constexpr RECT window{ 0, 0, 200, 120 };
		constexpr std::uint64_t first = 1;
		constexpr std::uint64_t second = 2;
		BarDirtyRegionTracker tracker;
		tracker.BeginFrame(window);
		tracker.Observe(first, RECT{ 10, 10, 30, 30 });
		tracker.Observe(second, RECT{});
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		tracker.Observe(first, RECT{});
		tracker.Observe(second, RECT{ 80, 40, 120, 70 });
		tracker.MarkChanged(first);
		tracker.MarkChanged(second);
		Check(SameRect(tracker.ResolveDamage(false), RECT{ 10, 10, 120, 70 }),
			"appearance and disappearance merge old and new keys");
	}

	void TestRetryRetainsDamageAndCommitAdvancesSnapshot()
	{
		constexpr RECT window{ 0, 0, 200, 120 };
		constexpr std::uint64_t control = 7;
		BarDirtyRegionTracker tracker;
		tracker.BeginFrame(window);
		tracker.Observe(control, RECT{ 10, 10, 20, 20 });
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		tracker.Observe(control, RECT{ 30, 30, 40, 40 });
		tracker.MarkChanged(control);
		Check(SameRect(tracker.ResolveDamage(false), RECT{ 10, 10, 40, 40 }),
			"retry candidate contains committed and current bounds");
		tracker.RetainForRetry(false);

		tracker.BeginFrame(window);
		tracker.Observe(control, RECT{ 50, 50, 60, 60 });
		Check(SameRect(tracker.ResolveDamage(false), RECT{ 10, 10, 60, 60 }),
			"deferred damage survives the next frame");
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		tracker.Observe(control, RECT{ 55, 55, 65, 65 });
		tracker.MarkChanged(control);
		Check(SameRect(tracker.ResolveDamage(false), RECT{ 50, 50, 65, 65 }),
			"successful commit advances the old snapshot");
	}

	void TestExplicitAndFullRetryDamage()
	{
		constexpr RECT window{ 0, 0, 200, 120 };
		BarDirtyRegionTracker tracker;
		tracker.BeginFrame(window);
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		tracker.IncludeDamage(RECT{ -5, 10, 20, 30 }, RECT{ 40, 50, 220, 130 });
		Check(SameRect(tracker.ResolveDamage(false), RECT{ 0, 10, 200, 120 }),
			"explicit old and new damage is clipped and merged");
		tracker.RetainForRetry(true);
		Check(SameRect(tracker.ResolveDamage(false), window),
			"failed present retains full damage until commit");
	}

	void TestDebugOverlayDamageAndClear()
	{
		constexpr RECT business{ 20, 20, 40, 40 };
		constexpr RECT previousText{ 5, 70, 80, 90 };
		constexpr RECT previousFrame{ 10, 10, 100, 100 };
		constexpr RECT currentText{ 50, 70, 130, 90 };
		const auto enabled = ResolveBarDebugDamage(
			business, previousText, previousFrame, currentText, true);
		Check(SameRect(enabled.frameTarget, RECT{ 20, 20, 130, 90 }),
			"debug frame targets business damage and current text only");
		Check(SameRect(enabled.presentDamage, RECT{ 5, 10, 130, 100 }),
			"debug present damage includes old and new overlays");

		const auto disabled = ResolveBarDebugDamage(
			RECT{}, previousText, previousFrame, RECT{}, false);
		Check(BarDirtyRegionTracker::IsEmpty(disabled.frameTarget),
			"disabled debug mode emits no new frame");
		Check(SameRect(disabled.presentDamage, RECT{ 5, 10, 100, 100 }),
			"disabled debug mode clears stale text and frame");
	}
}

int RunDirtyRegionTests()
{
	TestInitialAndFallbackDamage();
	TestChangedBoundsUnionAndClipping();
	TestAppearanceDisappearanceAndMultipleKeys();
	TestRetryRetainsDamageAndCommitAdvancesSnapshot();
	TestExplicitAndFullRetryDamage();
	TestDebugOverlayDamageAndClear();
	return failureCount;
}
