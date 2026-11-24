#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <type_traits>
#include <windows.h>

import Inkeys.Drawing.Draw3.dirty_region;
import Inkeys.Drawing.Draw3.ink_document;
import Inkeys.Drawing.Draw3.ink_history;

namespace
{
	using namespace Inkeys::Drawing::Draw3;

	static_assert(DirtyRegion::kCapacity == 16);
	static_assert(std::is_trivially_copyable_v<DirtyRegion>);

	struct Checks
	{
		int failures = 0;

		void operator()(bool condition, const char* name, int caseIndex = -1)
		{
			if (condition) return;
			++failures;
			std::cerr << "FAIL " << name;
			if (caseIndex >= 0) std::cerr << " (case " << caseIndex << ')';
			std::cerr << '\n';
		}
	};

	bool SameRect(const RECT& a, const RECT& b)
	{
		return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
	}

	bool Overlaps(const RECT& a, const RECT& b)
	{
		return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
	}

	bool CoveredByOneRect(const DirtyRegion& region, const RECT& rect)
	{
		for (const auto& stored : region.Rects())
		{
			if (stored.left <= rect.left && stored.top <= rect.top
				&& stored.right >= rect.right && stored.bottom >= rect.bottom) return true;
		}
		return false;
	}

	bool IsSingleRect(const DirtyRegion& region, const RECT& expected)
	{
		return region.Rects().size() == 1 && SameRect(region.Rects()[0], expected);
	}

	void CheckEmpty(Checks& check)
	{
		DirtyRegion region;
		check(region.Empty() && region.Rects().empty(), "new region is empty");
		check(SameRect(region.Bounds(), {}) && region.Area() == 0, "empty bounds and area are zero");
		region.Add(RECT{ 3, 3, 3, 5 });
		region.Add(RECT{ 1, 4, 5, 3 });
		check(region.Empty(), "add ignores empty and inverted rectangles");
		region.Normalize(kCompositeDirtyCost);
		check(region.Empty() && SameRect(region.Bounds(), {}), "normalize preserves empty region");
		region.Add(RECT{ 1, 2, 3, 4 });
		region.Clear();
		check(region.Empty() && SameRect(region.Bounds(), {}), "clear resets the region");
	}

	void CheckOverlapAndEdges(Checks& check)
	{
		DirtyRegion region;
		region.Add(RECT{ 0, 0, 40, 3 });
		region.Add(RECT{ 39, 2, 42, 50 });
		check(region.Rects().size() == 2 && region.Area() == 264, "add preserves overlapping rectangles and sums both areas");
		region.Normalize({ 0, 16 });
		check(IsSingleRect(region, { 0, 0, 42, 50 }) && region.Area() == 2100, "overlap merges even when bounding waste is positive");

		region.Clear();
		region.Add(RECT{ 0, 0, 2, 4 });
		region.Add(RECT{ 2, 0, 4, 4 });
		region.Add(RECT{ 100, 100, 101, 101 });
		for (const long long fixedCost : { 0LL, -1LL })
		{
			auto normalized = region;
			normalized.Normalize({ fixedCost, 16 });
			check(normalized.Rects().size() == 2 && SameRect(normalized.Rects()[0], { 0, 0, 4, 4 })
				&& SameRect(normalized.Rects()[1], { 100, 100, 101, 101 }),
				fixedCost == 0 ? "edge sharing with zero waste merges at zero cost" : "negative fixed cost is clamped to zero");
		}

		region.Clear();
		region.Add(RECT{ 0, 0, 2, 4 });
		region.Add(RECT{ 2, 0, 4, 1 });
		region.Normalize({ 0, 16 });
		check(region.Rects().size() == 2 && SameRect(region.Rects()[0], { 0, 0, 2, 4 })
			&& SameRect(region.Rects()[1], { 2, 0, 4, 1 }), "unequal edge sharing stays separate at cost zero and density 10 of 16");
	}

	void CheckCostAndOverlapClosure(Checks& check)
	{
		DirtyRegion region;
		region.Add(RECT{ 0, 0, 1, 1 });
		region.Add(RECT{ 101, 0, 102, 1 });
		region.Normalize({ 100, 16 });
		check(IsSingleRect(region, { 0, 0, 102, 1 }), "waste exactly at fixed cost merges");

		region.Clear();
		region.Add(RECT{ 0, 0, 1, 1 });
		region.Add(RECT{ 102, 0, 103, 1 });
		region.Normalize({ 100, 16 });
		check(region.Rects().size() == 2, "waste one above fixed cost stays separate");

		region.Clear();
		region.Add(RECT{ 0, 0, 2, 2 });
		region.Add(RECT{ 3, 3, 5, 5 });
		region.Add(RECT{ 2, -20, 3, 2 });
		// 前两块合并的浪费是 17，外接矩形与第三块相交，再合并的浪费是 78。
		auto costMerge = region;
		costMerge.Normalize({ 17, 16 });
		check(IsSingleRect(costMerge, { 0, -20, 5, 5 }), "cost merge closes a newly created overlap");
		region.Normalize({ 0, 2 });
		check(IsSingleRect(region, { 0, -20, 5, 5 }), "region limit merge closes a newly created overlap");
	}

	void CheckRegionLimitAndDensity(Checks& check)
	{
		DirtyRegion region;
		region.Add(RECT{ 0, 0, 1, 1 });
		region.Add(RECT{ 100, 0, 101, 1 });
		region.Add(RECT{ 900, 0, 901, 1 });
		region.Add(RECT{ 960, 0, 961, 1 });
		auto threeRegions = region;
		threeRegions.Normalize({ 0, 3 });
		check(threeRegions.Rects().size() == 3 && SameRect(threeRegions.Rects()[2], { 900, 0, 961, 1 }),
			"region limit merges the lowest waste pair first");
		auto twoRegions = region;
		twoRegions.Normalize({ 0, 2 });
		check(twoRegions.Rects().size() == 2 && SameRect(twoRegions.Rects()[0], { 0, 0, 101, 1 })
			&& SameRect(twoRegions.Rects()[1], { 900, 0, 961, 1 }), "region limit picks least waste and keeps order");
		auto oneRegion = region;
		oneRegion.Normalize({ 0, 1 });
		check(IsSingleRect(oneRegion, { 0, 0, 961, 1 }), "explicit region limit one");
		region.Normalize({ 0, 0 });
		check(IsSingleRect(region, { 0, 0, 961, 1 }), "region limit zero is treated as one");

		region.Clear();
		region.Add(RECT{ 0, 0, 4, 1 });
		region.Add(RECT{ 0, 4, 4, 5 });
		region.Add(RECT{ 6, 0, 7, 5 });
		region.Normalize({ 10, 2 });
		// 为数量上限合并出的 {0,0,4,5} 与第三块的浪费恰为 10，应继续按代价合并。
		check(IsSingleRect(region, { 0, 0, 7, 5 }), "limit merge re-checks the cost rule");

		region.Clear();
		region.Add(RECT{ 0, 0, 4, 1 });
		region.Add(RECT{ 7, 0, 12, 1 });
		region.Normalize({ 0, 16 });
		check(IsSingleRect(region, { 0, 0, 12, 1 }), "density exactly three quarters uses the bounds");

		region.Clear();
		region.Add(RECT{ 0, 0, 3, 1 });
		region.Add(RECT{ 7, 0, 12, 1 });
		region.Normalize({ 0, 16 });
		check(region.Rects().size() == 2 && region.Area() == 8, "density one pixel below three quarters stays separate");

		// 6/9 低于 3/4；阈值 6.75 若向下取整为 6 会误合并。
		region.Clear();
		region.Add(RECT{ 0, 0, 3, 1 });
		region.Add(RECT{ 0, 2, 3, 3 });
		region.Normalize({ 0, 16 });
		check(region.Rects().size() == 2, "density threshold rounds up");
	}

	void CheckCapacity(Checks& check)
	{
		DirtyRegion region;
		std::array<RECT, DirtyRegion::kCapacity + 4> inputs{};
		for (std::size_t i = 0; i < inputs.size(); ++i)
		{
			const auto x = static_cast<LONG>(i) * 1000;
			inputs[i] = { x, 0, x + 1, 1 };
			region.Add(inputs[i]);
			check(region.Rects().size() == (std::min)(i + 1, DirtyRegion::kCapacity), "capacity count after add");
			if (i == DirtyRegion::kCapacity)
			{
				check(region.Rects().size() == DirtyRegion::kCapacity
					&& SameRect(region.Rects().front(), { 0, 0, 1001, 1 })
					&& SameRect(region.Rects().back(), inputs[i]), "capacity tie across all candidates merges the earliest pair");
			}
		}
		for (const auto& rect : inputs) check(CoveredByOneRect(region, rect), "capacity overflow retains every input");

		if (region.Rects().size() == DirtyRegion::kCapacity)
		{
			const auto last = region.Rects().back();
			const auto before = region;
			region.Add(region.Rects().back());
			// 重复矩形与自身的浪费为负，是最小的一对：整个序列应保持不变。
			bool unchanged = region.Rects().size() == before.Rects().size();
			for (std::size_t i = 0; unchanged && i < before.Rects().size(); ++i)
			{
				unchanged = SameRect(region.Rects()[i], before.Rects()[i]);
			}
			check(unchanged && SameRect(region.Rects().back(), last),
				"capacity merge can select the new rectangle and accepts a stored reference");
		}
	}

	void CheckClipAddAndIntersects(Checks& check)
	{
		DirtyRegion region;
		region.Add(RECT{ -3, 1, 4, 5 });
		region.Add(RECT{ 10, 1, 20, 3 });
		DirtyRegion other;
		other.Add(RECT{ 6, -3, 15, 9 });
		other.Add(RECT{ 1, 2, 3, 3 });
		region.Add(other);
		check(region.Rects().size() == 4 && SameRect(region.Rects()[2], { 6, -3, 15, 9 })
			&& SameRect(region.Rects()[3], { 1, 2, 3, 3 }), "add region appends in source order");
		region.Add(region);
		check(region.Rects().size() == 4, "self add is a no-op");
		region.ClipTo(10, 8);
		check(region.Rects().size() == 3 && SameRect(region.Rects()[0], { 0, 1, 4, 5 })
			&& SameRect(region.Rects()[1], { 6, 0, 10, 8 })
			&& SameRect(region.Rects()[2], { 1, 2, 3, 3 }), "clip truncates partial rectangles, removes outside rectangles and preserves order");
		check(region.Intersects({ 9, 6, 12, 9 }), "intersects finds positive overlap in a later rectangle");
		check(!region.Intersects({ 4, 1, 6, 3 }), "intersects excludes shared edges");
		check(!region.Intersects({ 4, 5, 5, 6 }), "intersects excludes a shared corner");
		check(!region.Intersects({ 1, 2, 1, 4 }), "intersects excludes an empty query inside a region");
		check(!region.Intersects({ 20, 20, 30, 30 }), "intersects excludes a distant query");
		region.ClipTo(0, 8);
		check(region.Empty(), "clip with zero width clears");
		region.Add(other);
		region.ClipTo(10, -1);
		check(region.Empty(), "clip with negative height clears");
	}

	void CheckLargeCoordinates(Checks& check)
	{
		constexpr auto kLow = (std::numeric_limits<LONG>::min)();
		constexpr auto kHigh = (std::numeric_limits<LONG>::max)();
		constexpr auto kMaxArea = (std::numeric_limits<long long>::max)();
		DirtyRegion region;
		region.Add(RECT{ kLow, kLow, -1073741824L, kHigh });
		region.Add(RECT{ 0, kLow, kHigh, kHigh });
		check(region.Area() == kMaxArea, "area sum saturates above long long range");
		region.Normalize({ 0, 16 });
		check(region.Rects().size() == 2, "large density just below three quarters stays separate");

		region.Clear();
		region.Add(RECT{ kLow, kLow, -1073741823L, kHigh });
		region.Add(RECT{ 0, kLow, kHigh, kHigh });
		region.Normalize({ 0, 16 });
		check(IsSingleRect(region, { kLow, kLow, kHigh, kHigh }) && region.Area() == kMaxArea,
			"large density just above three quarters uses the bounds and area saturates");
	}

	void CheckRandomizedProperties(Checks& check)
	{
		constexpr int kWidth = 2880;
		constexpr int kHeight = 1920;
		constexpr std::array profiles{
			kCompositeDirtyCost, kReadbackDirtyCost, kPresentDirtyCost, kUlwCommitDirtyCost,
			DirtyCostProfile{ 0, 16 }, DirtyCostProfile{ 1, 2 }, DirtyCostProfile{ -1, 0 }
		};
		std::mt19937 random(0xD123C1u);
		std::uniform_int_distribution<int> inputCount(1, 40);
		std::uniform_int_distribution<LONG> xPosition(0, kWidth - 1);
		std::uniform_int_distribution<LONG> yPosition(0, kHeight - 1);
		std::uniform_int_distribution<int> sizeClass(0, 2);
		std::uniform_int_distribution<LONG> tinySize(1, 12);
		std::uniform_int_distribution<LONG> largeSize(48, 900);

		for (int caseIndex = 0; caseIndex < 2000; ++caseIndex)
		{
			DirtyRegion region;
			std::array<RECT, 40> inputs{};
			const auto count = static_cast<std::size_t>(inputCount(random));
			RECT inputBounds{ kWidth, kHeight, 0, 0 };
			for (std::size_t i = 0; i < count; ++i)
			{
				const LONG x = xPosition(random);
				const LONG y = yPosition(random);
				auto& size = sizeClass(random) == 0 ? largeSize : tinySize;
				const RECT rect{ x, y, (std::min)(x + size(random), static_cast<LONG>(kWidth)),
					(std::min)(y + size(random), static_cast<LONG>(kHeight)) };
				inputs[i] = rect;
				region.Add(rect);
				inputBounds.left = (std::min)(inputBounds.left, rect.left);
				inputBounds.top = (std::min)(inputBounds.top, rect.top);
				inputBounds.right = (std::max)(inputBounds.right, rect.right);
				inputBounds.bottom = (std::max)(inputBounds.bottom, rect.bottom);
			}

			const auto& profile = profiles[static_cast<std::size_t>(caseIndex) % profiles.size()];
			region.Normalize(profile);
			const auto output = region.Rects();
			const auto limit = (std::max)(std::uint32_t{ 1 }, profile.maxRegions);
			check(!output.empty() && output.size() <= limit, "random region count", caseIndex);
			check(SameRect(region.Bounds(), inputBounds), "random bounds equal input bounds", caseIndex);
			for (std::size_t i = 0; i < output.size(); ++i)
			{
				check(output[i].left < output[i].right && output[i].top < output[i].bottom,
					"random output is non-empty", caseIndex);
				for (std::size_t j = i + 1; j < output.size(); ++j)
				{
					check(!Overlaps(output[i], output[j]), "random outputs do not overlap", caseIndex);
				}
			}
			for (std::size_t i = 0; i < count; ++i)
			{
				// 合并只扩大外接矩形，每块原始输入都应由单个输出矩形完整覆盖。
				check(CoveredByOneRect(region, inputs[i]), "random input is contained by one output", caseIndex);
			}

			// 规整结果是不动点：再规整一次不应改变。
			auto again = region;
			again.Normalize(profile);
			bool same = again.Rects().size() == output.size();
			for (std::size_t i = 0; same && i < output.size(); ++i) same = SameRect(again.Rects()[i], output[i]);
			check(same, "random normalize is idempotent", caseIndex);
		}
	}
	void CheckScreenRegionContainsTiles(Checks& check, const DirtyRegion& region,
		std::span<const SignedTileCoordinate> tiles,
		float viewportX, float viewportY, int width, int height)
	{
		for (const RECT& rect : region.Rects())
			check(rect.left >= 0 && rect.top >= 0 && rect.right <= width && rect.bottom <= height &&
				rect.left < rect.right && rect.top < rect.bottom, "screen region rectangles are clipped");

		for (const SignedTileCoordinate tile : tiles)
		{
			const double left = static_cast<double>(tile.x) * kUndoTileSize - viewportX;
			const double top = static_cast<double>(tile.y) * kUndoTileSize - viewportY;
			const auto clip = [](double edge, int limit)
			{
				return static_cast<LONG>(std::clamp(edge, 0.0, static_cast<double>(limit)));
			};
			const RECT expected{
				clip(std::floor(left), width), clip(std::floor(top), height),
				clip(std::ceil(left + kUndoTileSize), width), clip(std::ceil(top + kUndoTileSize), height)
			};
			bool covered = true;
			for (LONG y = expected.top; covered && y < expected.bottom; ++y)
				for (LONG x = expected.left; covered && x < expected.right; ++x)
					covered = CoveredByOneRect(region, { x, y, x + 1, y + 1 });
			check(covered, "screen region covers every visible pixel in an undo tile");
		}
	}

	void CheckStrokeScreenRegion(Checks& check)
	{
		check(BuildStrokeScreenRegion({}, 0.f, 0.f, 256, 256).Empty(), "empty tiles produce empty screen region");
		const std::array<SignedTileCoordinate, 1> single{ { { 1, 1 } } };
		check(IsSingleRect(BuildStrokeScreenRegion(single, 0.f, 0.f, 256, 256),
			{ 128, 128, 256, 256 }), "single tile maps to a 128 pixel square");
		check(BuildStrokeScreenRegion(single, 0.f, 0.f, 128, 128).Empty(), "tile at canvas edge is invisible");
		check(BuildStrokeScreenRegion(single, 0.f, 0.f, 0, 256).Empty(), "zero width canvas has no screen region");
		check(BuildStrokeScreenRegion(single, (std::numeric_limits<float>::infinity)(), 0.f, 256, 256).Empty(),
			"non-finite viewport has no screen region");

		const std::array<SignedTileCoordinate, 5> lShape{ { { 0, 1 }, { 0, 2 }, { 1, 0 }, { 0, 0 }, { 0, 2 } } };
		const DirtyRegion lRegion = BuildStrokeScreenRegion(lShape, 0.f, 0.f, 384, 384);
		check(lRegion.Rects().size() == 2 && lRegion.Area() == 4LL * 128 * 128,
			"unsorted L-shape coalesces matching rows and columns without filling the corner");
		CheckScreenRegionContainsTiles(check, lRegion, lShape, 0.f, 0.f, 384, 384);

		std::array<SignedTileCoordinate, 40> staircase{};
		for (int i = 0; i < static_cast<int>(staircase.size()); ++i)
			staircase[i] = { (i + 1) / 2, i / 2 };
		const DirtyRegion stairRegion = BuildStrokeScreenRegion(staircase, 0.f, 0.f, 2688, 2560);
		check(stairRegion.Rects().size() == DirtyRegion::kCapacity, "40-tile staircase reaches the region capacity");
		CheckScreenRegionContainsTiles(check, stairRegion, staircase, 0.f, 0.f, 2688, 2560);

		const std::array<SignedTileCoordinate, 4> fractional{ { { 0, 0 }, { -2, -1 }, { -1, -1 }, { 0, 0 } } };
		const DirtyRegion clipped = BuildStrokeScreenRegion(fractional, -32.25f, -64.5f, 100, 110);
		check(clipped.Rects().size() == 2 && clipped.Area() == 33LL * 65 + 68LL * 46,
			"fractional negative viewport rounds outward, clips, and retains separate rectangles");
		CheckScreenRegionContainsTiles(check, clipped, fractional, -32.25f, -64.5f, 100, 110);

		const float tiny = (std::numeric_limits<float>::min)();
		check(IsSingleRect(BuildStrokeScreenRegion(single, tiny, -tiny, 300, 300),
			{ 127, 128, 256, 257 }), "tiny fractional viewport still rounds outward at integral tile edges");

		constexpr auto maxIndex = (std::numeric_limits<int32_t>::max)();
		constexpr auto minIndex = (std::numeric_limits<int32_t>::min)();
		const float maxWorldEdge = static_cast<float>((int64_t{ maxIndex } + 1) * kUndoTileSize);
		const float minWorldEdge = static_cast<float>(int64_t{ minIndex } * kUndoTileSize);
		const float extremeViewportX = std::nextafter(maxWorldEdge, 0.f);
		const std::array<SignedTileCoordinate, 3> extremes{ { { maxIndex, minIndex }, { minIndex, maxIndex },
			{ maxIndex - 1, minIndex } } };
		const DirtyRegion extreme = BuildStrokeScreenRegion(extremes, extremeViewportX, minWorldEdge, 16300, 256);
		check(IsSingleRect(extreme, { 16128, 0, 16300, 128 }), "extreme tile indices merge and clip without wrapping");
		CheckScreenRegionContainsTiles(check, extreme, extremes, extremeViewportX, minWorldEdge, 16300, 256);
	}

	void CheckStrokeTileFootprints(Checks& check)
	{
		StoredInkStyle style;
		style.inkType = StoredInkType::Highlighter;
		constexpr std::array<float, 3> centers{ 170.f, 300.f, 595.f };
		const InkStroke diagonal(style, { { centers[0], centers[0], 120.f },
			{ centers[1], centers[1], 120.f }, { centers[2], centers[2], 120.f } });
		const auto footprint = BuildStrokeTileFootprint(diagonal);
		check(footprint.has_value(), "wide diagonal highlighter has a history footprint");
		if (footprint)
		{
			for (size_t i = 1; i < centers.size(); ++i)
			{
				for (int y = 0; y <= 5; ++y)
					for (int x = 0; x <= 4; ++x)
					{
						// 对角线的 x 等于 y；在扩张后的瓦片内求中心线区间，等价于矩形笔尖扫过瓦片。
						const float first = (std::max)({ centers[i - 1], x * 128.f - 7.5f, y * 128.f - 60.f });
						const float last = (std::min)({ centers[i], (x + 1) * 128.f + 7.5f, (y + 1) * 128.f + 60.f });
						if (first <= last)
							check(std::binary_search(footprint->undoTiles.begin(), footprint->undoTiles.end(),
								SignedTileCoordinate{ x, y }), "wide diagonal highlighter covers swept nib tile");
					}
			}
		}

		const InkStroke tapered(style, { { 24.f, 64.f, 120.f }, { 125.f, 64.f, 2.f } });
		const auto taperedFootprint = BuildStrokeTileFootprint(tapered);
		check(taperedFootprint && taperedFootprint->pixelBounds.right >= 128.8125f &&
			std::binary_search(taperedFootprint->undoTiles.begin(), taperedFootprint->undoTiles.end(),
				SignedTileCoordinate{ 1, 0 }), "averaged highlighter nib extends beyond its narrow endpoint");

		const InkStroke withDuplicate(style, { { 64.f, 100.f, 120.f }, { 64.f, 100.125f, 2.f },
			{ 340.f, 100.f, 2.f } });
		const auto duplicateFootprint = BuildStrokeTileFootprint(withDuplicate);
		check(duplicateFootprint && std::binary_search(duplicateFootprint->undoTiles.begin(),
			duplicateFootprint->undoTiles.end(), SignedTileCoordinate{ 2, 1 }),
			"varying-width highlighter covers the sweep across a skipped point");

		const InkStroke offscreenDot(style, { { 64.f, -380.f, 999.f } });
		const auto offscreenFootprint = BuildStrokeTileFootprint(offscreenDot, InkPixelBounds{ 0, 0, 128, 128 });
		check(offscreenFootprint && std::binary_search(offscreenFootprint->undoTiles.begin(),
			offscreenFootprint->undoTiles.end(), SignedTileCoordinate{ 0, 0 }),
			"wide offscreen highlighter reaches the single visible tile");

		for (const StoredInkType type : { StoredInkType::Pen, StoredInkType::Eraser })
		{
			style.inkType = type;
			const InkStroke dot(style, { { 61.f, 64.f, 128.f } });
			const auto dotFootprint = BuildStrokeTileFootprint(dot);
			check(dotFootprint && dotFootprint->pixelBounds.right > 128.f &&
				std::binary_search(dotFootprint->undoTiles.begin(), dotFootprint->undoTiles.end(),
					SignedTileCoordinate{ 1, 0 }), "round dot capsule reaches the tile to its right");
		}
	}
}

int RunDraw3DirtyRegionTests()
{
	Checks check;
	CheckEmpty(check);
	CheckOverlapAndEdges(check);
	CheckCostAndOverlapClosure(check);
	CheckRegionLimitAndDensity(check);
	CheckCapacity(check);
	CheckClipAddAndIntersects(check);
	CheckLargeCoordinates(check);
	CheckRandomizedProperties(check);
	CheckStrokeScreenRegion(check);
	CheckStrokeTileFootprints(check);
	return check.failures;
}
