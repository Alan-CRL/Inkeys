#include "../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.DirtyRegion.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h"

#include <iostream>
#include <string_view>
#include <vector>

namespace
{
	using Inkeys::UI::Bar::BarDirtyRegionTracker;
	using Inkeys::UI::Bar::ResolveBarDebugDamage;
	using Inkeys::UI::Bar::ResolveBarLightBorderDamage;
	using Inkeys::UI::Bar::ResolveBarScaledDirtyBounds;

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

	void TestCenteredShrinkClearsReinterpretedSurfacePixels()
	{
		using namespace Inkeys::UI::Bar;
		for (bool opensRight : { true, false })
			for (double zoom : { 1.0, 1.5 })
			{
				BarDirtyRegionTracker tracker;
				BarWindowViewportController viewport;
				BarPresentMappingTracker mapping;
				const RECT layout{ 0, 0, 6000, 2000 };
				const SIZE capacity{ 2400, 1200 };
				std::vector<bool> surface(capacity.cx, false);
				POINT previousAnchor{};
				bool initialized = false;
				for (double width : { 560.0, 400.0, 240.0 })
				{
					const double centerOffset = (opensRight ? 1.0 : -1.0) * (50.0 + width / 2.0);
					const auto root = ResolveBarBottomDockCenteredRootPlacement(
						2000.0, 81.0, centerOffset, width + 1.0);
					const POINT anchor{ static_cast<LONG>(std::lround(root.mainCenterDip * zoom)), 1000 };
					const POINT origin{ anchor.x - capacity.cx / 2, anchor.y - capacity.cy / 2 };
					const RECT body{ static_cast<LONG>(std::floor(root.bodyLeftDip * zoom)) - 6, 950,
						static_cast<LONG>(std::ceil(root.bodyRightDip * zoom)) + 6, 1050 };
					const POINT delta = initialized ? POINT{ anchor.x - previousAnchor.x, 0 } : POINT{};
					const auto candidate = viewport.Resolve(body, {}, layout, 2, false, delta).viewport;
					const BarPresentMappingTuple tuple{
						POINT{ candidate.left - origin.x, candidate.top - origin.y },
						SIZE{ candidate.right - candidate.left, candidate.bottom - candidate.top }, capacity, 1 };
					const auto mode = mapping.Resolve(tuple);
					if (initialized) Check(mode == BarPresentMappingMode::LocalDirty,
						"centered shrink can move the backing origin while source and size stay unchanged");
					tracker.BeginFrame(layout);
					tracker.MarkChanged(1);
					tracker.Observe(1, body);
					RECT damage = tracker.ResolveDamage(false);
					const bool rootRelayout = initialized && delta.x != 0;
					if (ShouldForceBarFullWindowReplacement(false, mode, rootRelayout))
					{
						tracker.ForceFullDamage();
						damage = candidate;
					}
					// 模拟持久 D2D 位图：旧像素不随新的 capacityOrigin 自动搬家。
					const RECT clear = BarLayoutToSurfaceRect(damage, origin);
					const RECT drawn = BarLayoutToSurfaceRect(body, origin);
					for (LONG x = clear.left; x < clear.right; ++x) surface[x] = false;
					for (LONG x = drawn.left; x < drawn.right; ++x) surface[x] = true;
					bool clean = true;
					for (LONG x = tuple.source.x; x < tuple.source.x + tuple.windowSize.cx; ++x)
						clean &= surface[x] == (x >= drawn.left && x < drawn.right);
					Check(clean, "centered shrink removes both old content and old debug-edge pixels on both sides");
					tracker.CommitPresented();
					viewport.Commit(candidate);
					mapping.CommitPresented(tuple);
					previousAnchor = anchor;
					initialized = true;
				}
			}
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

	void TestWholeBarTranslationRebasesCommittedDamage()
	{
		constexpr RECT window{ 0, 0, 1600, 900 };
		constexpr std::uint64_t control = 3;
		BarDirtyRegionTracker tracker;
		tracker.BeginFrame(window);
		tracker.Observe(control, RECT{ 100, 200, 220, 280 });
		tracker.CommitPresented();

		tracker.TranslateCommitted(POINT{ 900, 0 });
		tracker.BeginFrame(window);
		tracker.MarkChanged(control);
		tracker.Observe(control, RECT{ 1010, 200, 1140, 280 });
		Check(SameRect(tracker.ResolveDamage(false),
			RECT{ 1000, 200, 1140, 280 }),
			"whole-Bar translation rebases old bounds before animation damage");
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

	void TestStableRecordsAndSelectiveObservation()
	{
		constexpr RECT window{ 0, 0, 200, 120 };
		constexpr std::uint64_t moving = 7;
		constexpr std::uint64_t stable = 8;
		BarDirtyRegionTracker tracker;
		tracker.BeginFrame(window);
		tracker.Observe(moving, RECT{ 10, 10, 20, 20 });
		tracker.Observe(stable, RECT{ 80, 10, 100, 30 });
		tracker.CommitPresented();

		for (LONG frame = 0; frame < 100; ++frame)
		{
			tracker.BeginFrame(window);
			tracker.MarkChanged(moving);
			Check(tracker.HasOnlyChangedKeys(moving, 99),
				"single light-like key selects the narrow hot path");
			Check(tracker.ShouldObserve(moving),
				"changed visual requests current bounds");
			Check(!tracker.ShouldObserve(stable),
				"stable visual skips ordinary-frame bounds work");
			tracker.Observe(moving, RECT{ 11 + frame, 10, 21 + frame, 20 });
			(void)tracker.ResolveDamage(false);
			tracker.CommitPresented();
		}
		Check(tracker.VisualRecordCount() == 2,
			"stable tracker records do not grow across frames");

		tracker.BeginFrame(window);
		tracker.MarkChanged(moving);
		tracker.Observe(moving, RECT{ 120, 10, 130, 20 });
		tracker.Observe(stable, RECT{ 90, 10, 110, 30 });
		(void)tracker.ResolveDamage(false);
		tracker.CommitPresented();
		tracker.BeginFrame(window);
		tracker.MarkChanged(stable);
		tracker.Observe(stable, RECT{ 100, 10, 120, 30 });
		Check(SameRect(tracker.ResolveDamage(false), RECT{ 90, 10, 120, 30 }),
			"observed group child advances without a full snapshot copy");
	}

	void TestTrackerRetainsPendingGroupDamageAcrossFrames()
	{
		constexpr RECT window{ 0, 0, 1400, 800 };
		constexpr std::uint64_t mainGroup = 21;
		constexpr std::uint64_t moreGroup = 22;
		BarDirtyRegionTracker tracker;
		// 这里只验证 tracker 的 pending/commit 事务，不覆盖 RenderLoop 的 Inherit 时序。
		tracker.BeginFrame(window);
		tracker.Observe(mainGroup, RECT{ 90, 300, 900, 430 });
		tracker.Observe(moreGroup, RECT{ 480, 150, 780, 290 });
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		tracker.MarkChanged(mainGroup);
		tracker.MarkChanged(moreGroup);
		tracker.Observe(mainGroup, RECT{ 320, 300, 1050, 430 });
		tracker.Observe(moreGroup, RECT{ 500, 120, 820, 290 });
		Check(SameRect(tracker.ResolveDamage(false),
			RECT{ 90, 120, 1050, 430 }),
			"rapid panel switch includes last presented and intermediate extents");
		tracker.RetainForRetry(false);

		// 中间帧未提交便立刻收缩；最后 damage 仍不能丢掉最左旧像素。
		tracker.BeginFrame(window);
		tracker.Observe(mainGroup, RECT{ 610, 320, 760, 410 });
		tracker.Observe(moreGroup, RECT{});
		Check(SameRect(tracker.ResolveDamage(false),
			RECT{ 90, 120, 1050, 430 }),
			"interrupted collapse retains the outermost presented group boundary");
		tracker.CommitPresented();

		tracker.BeginFrame(window);
		tracker.MarkChanged(mainGroup);
		tracker.Observe(mainGroup, RECT{ 500, 300, 820, 430 });
		Check(SameRect(tracker.ResolveDamage(false),
			RECT{ 500, 300, 820, 430 }),
			"successful interrupted collapse commits only the final presented bounds");
	}

	void TestLightDamageUsesAffectedBorderBands()
	{
		constexpr RECT outer{ 0, 0, 1000, 400 };
		constexpr RECT content{ 10, 10, 990, 390 };
		Check(BarDirtyRegionTracker::IsEmpty(ResolveBarLightBorderDamage(
			outer, content, RECT{ 450, 150, 550, 250 })),
			"light inside a large control without reaching its border has no damage");
		Check(SameRect(ResolveBarLightBorderDamage(
			outer, content, RECT{ 450, 0, 550, 30 }),
			RECT{ 450, 0, 550, 20 }),
			"top light damage is clipped to the affected border band");
		Check(SameRect(ResolveBarLightBorderDamage(
			outer, content, RECT{ 0, 180, 30, 220 }),
			RECT{ 0, 180, 20, 220 }),
			"side light damage is clipped to the affected border band");
		Check(SameRect(ResolveBarLightBorderDamage(
			outer, content, RECT{ -20, -20, 30, 30 }),
			RECT{ 0, 0, 30, 30 }),
			"corner light conservatively merges the two affected edge bands");
	}

	void TestScaledDirectContentUsesPresentedBounds()
	{
		Check(SameRect(ResolveBarScaledDirtyBounds(
			400.0, 200.0, 460.0, 220.0,
			420.0, 220.0, 0.5, 1.0, 3),
			RECT{ 407, 207, 443, 223 }),
			"scaled direct content uses its presented position instead of zero origin");
		Check(BarDirtyRegionTracker::IsEmpty(ResolveBarScaledDirtyBounds(
			400.0, 200.0, 460.0, 220.0,
			420.0, 220.0, 0.0, 1.0, 3)),
			"hidden scaled direct content contributes no damage");
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

		const auto frameOnly = ResolveBarDebugDamage(
			business, previousText, previousFrame, RECT{}, true);
		Check(SameRect(frameOnly.frameTarget, business),
			"dirty debug without FPS frames business damage only");
		Check(SameRect(frameOnly.presentDamage, RECT{ 5, 10, 100, 100 }),
			"dirty debug without FPS clears stale text and frame");

		const auto finalIdle = ResolveBarDebugDamage(
			RECT{}, previousText, previousFrame, RECT{}, true, true);
		Check(SameRect(finalIdle.frameTarget, previousFrame),
			"final idle frame reuses the last red frame as its green target");
		Check(SameRect(finalIdle.presentDamage, RECT{ 5, 10, 100, 100 }),
			"final idle frame damages the previous overlays before recoloring");

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
	TestCenteredShrinkClearsReinterpretedSurfacePixels();
	TestInitialAndFallbackDamage();
	TestChangedBoundsUnionAndClipping();
	TestWholeBarTranslationRebasesCommittedDamage();
	TestAppearanceDisappearanceAndMultipleKeys();
	TestRetryRetainsDamageAndCommitAdvancesSnapshot();
	TestExplicitAndFullRetryDamage();
	TestStableRecordsAndSelectiveObservation();
	TestTrackerRetainsPendingGroupDamageAcrossFrames();
	TestLightDamageUsesAffectedBorderBands();
	TestScaledDirectContentUsesPresentedBounds();
	TestDebugOverlayDamageAndClear();
	return failureCount;
}
