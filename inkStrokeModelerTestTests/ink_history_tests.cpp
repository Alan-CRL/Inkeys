#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

import draw3.ink_document;
import draw3.ink_history;

namespace
{
	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED ink history line " << line << ": " << expression << std::endl;
	}

#define INK_HISTORY_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)

	draw3::InkStroke MakeStroke(draw3::StoredInkType type,
		std::vector<draw3::StoredInkPoint> points)
	{
		return draw3::InkStroke({ type, 0x123456u, 0.75f, 0u }, std::move(points));
	}

	draw3::StrokeTileFootprint MakeFootprint(
		draw3::SignedTileCoordinate tile, float offset = 0.0f)
	{
		draw3::StrokeTileFootprint footprint;
		footprint.pixelBounds = { offset, offset, offset + 10.0f, offset + 10.0f };
		footprint.undoTiles = { tile };
		footprint.compositionTiles = { tile };
		return footprint;
	}

	bool ContainsTile(std::span<const draw3::SignedTileCoordinate> tiles,
		draw3::SignedTileCoordinate expected)
	{
		return std::find(tiles.begin(), tiles.end(), expected) != tiles.end();
	}

	void TestPoliciesAndUndoBudget(int& failures)
	{
		static_assert(draw3::kUndoTileBytes == 64ull * 1024ull);
		static_assert(draw3::kCompositionTileBytes == 384ull * 1024ull);
		INK_HISTORY_CHECK(draw3::CountTilesCoveringPixelBounds(
			draw3::InkPixelBounds{ 0.0f, 0.0f, 3840.0f, 2160.0f },
			draw3::kUndoTileSize) == 510u);
		INK_HISTORY_CHECK(draw3::CountTilesCoveringPixelBounds(
			draw3::InkPixelBounds{ -1.0f, -1.0f, 1.0f, 1.0f },
			draw3::kUndoTileSize) == 4u);

		draw3::UndoCachePlanner planner;
		INK_HISTORY_CHECK(planner.SlotCapacity() == 1024u);
		INK_HISTORY_CHECK(planner.PageCount() == 4u);
		INK_HISTORY_CHECK(planner.Policy().maxEntries == 20u);
		for (uint64_t value = 1; value <= 20; ++value)
		{
			const draw3::UndoCacheReservation reservation =
				planner.Reserve({ value }, 1);
			INK_HISTORY_CHECK(reservation.status ==
				draw3::UndoCacheReservationStatus::Reserved);
			INK_HISTORY_CHECK(reservation.evictedEntries.empty());
			INK_HISTORY_CHECK(planner.Commit({ value }));
		}
		const draw3::UndoCacheReservation twentyFirst = planner.Reserve({ 21 }, 1);
		INK_HISTORY_CHECK(twentyFirst.status ==
			draw3::UndoCacheReservationStatus::Reserved);
		INK_HISTORY_CHECK(twentyFirst.evictedEntries.size() == 1);
		INK_HISTORY_CHECK(twentyFirst.evictedEntries[0].value == 1u);
		INK_HISTORY_CHECK(planner.Commit({ 21 }));
		INK_HISTORY_CHECK(!planner.Contains({ 1 }));
		INK_HISTORY_CHECK(planner.Contains({ 2 }));
		INK_HISTORY_CHECK(planner.Entries().size() == 20);

		draw3::UndoCachePlanner tiny({ draw3::kUndoTileBytes * 2, 20 });
		INK_HISTORY_CHECK(tiny.Commit({ 1 }) == false);
		INK_HISTORY_CHECK(tiny.Reserve({ 1 }, 1).status ==
			draw3::UndoCacheReservationStatus::Reserved);
		INK_HISTORY_CHECK(tiny.Reserve({ 2 }, 1).status ==
			draw3::UndoCacheReservationStatus::ReservationPending);
		INK_HISTORY_CHECK(tiny.Cancel({ 1 }));
		INK_HISTORY_CHECK(tiny.Reserve({ 2 }, 1).status ==
			draw3::UndoCacheReservationStatus::Reserved);
		INK_HISTORY_CHECK(tiny.Commit({ 2 }));
		INK_HISTORY_CHECK(tiny.Reserve({ 3 }, 1).status ==
			draw3::UndoCacheReservationStatus::Reserved);
		INK_HISTORY_CHECK(tiny.Commit({ 3 }));
		const draw3::UndoCacheReservation oversized = tiny.Reserve({ 4 }, 3);
		INK_HISTORY_CHECK(oversized.status ==
			draw3::UndoCacheReservationStatus::EntryTooLarge);
		INK_HISTORY_CHECK(oversized.evictedEntries.empty());
		INK_HISTORY_CHECK(tiny.Contains({ 2 }) && tiny.Contains({ 3 }));

		const std::vector<draw3::UndoCacheEntryId> reduced = tiny.SetPolicy(
			{ draw3::kUndoTileBytes, 1 });
		INK_HISTORY_CHECK(reduced.size() == 1 && reduced[0].value == 2u);
		INK_HISTORY_CHECK(tiny.Contains({ 3 }));
		INK_HISTORY_CHECK(tiny.UsedSlotCount() == 1);
		const std::vector<draw3::UndoCacheEntryId> disabled = tiny.SetPolicy({ 0, 0 });
		INK_HISTORY_CHECK(disabled.size() == 1 && disabled[0].value == 3u);
		INK_HISTORY_CHECK(tiny.SlotCapacity() == 0 && tiny.Entries().empty());
		INK_HISTORY_CHECK(tiny.SetPolicy({ draw3::kUndoTileBytes * 8, 8 }).empty());
		INK_HISTORY_CHECK(tiny.Entries().empty());
	}

	void TestSparseFootprints(int& failures)
	{
		const draw3::InkPixelBounds fourK{ 0.0f, 0.0f, 3840.0f, 2160.0f };
		const std::optional<draw3::StrokeTileFootprint> diagonal =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::Pen,
				{ { 2.0f, 2.0f, 4.0f }, { 3837.0f, 2157.0f, 4.0f } }), fourK);
		INK_HISTORY_CHECK(diagonal.has_value());
		INK_HISTORY_CHECK(!diagonal->undoTiles.empty());
		INK_HISTORY_CHECK(diagonal->undoTiles.size() < 160);
		INK_HISTORY_CHECK(diagonal->compositionTiles.size() < 80);
		INK_HISTORY_CHECK(std::is_sorted(
			diagonal->undoTiles.begin(), diagonal->undoTiles.end()));

		const draw3::InkPixelBounds aroundOrigin{ -256.0f, -256.0f, 256.0f, 256.0f };
		const std::optional<draw3::StrokeTileFootprint> negative =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::Pen,
				{ { -1.0f, -1.0f, 8.0f } }), aroundOrigin);
		INK_HISTORY_CHECK(negative.has_value());
		INK_HISTORY_CHECK(ContainsTile(negative->undoTiles, { -1, -1 }));
		INK_HISTORY_CHECK(ContainsTile(negative->undoTiles, { 0, 0 }));

		const std::optional<draw3::StrokeTileFootprint> penBoundary =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::Pen,
				{ { 128.0f, 64.0f, 2.0f } }), fourK);
		INK_HISTORY_CHECK(penBoundary.has_value());
		INK_HISTORY_CHECK(ContainsTile(penBoundary->undoTiles, { 0, 0 }));
		INK_HISTORY_CHECK(ContainsTile(penBoundary->undoTiles, { 1, 0 }));

		const std::optional<draw3::StrokeTileFootprint> eraser =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::Eraser,
				{ { 128.0f, 128.0f, 50.0f } }), fourK);
		INK_HISTORY_CHECK(eraser.has_value());
		INK_HISTORY_CHECK(eraser->undoTiles.size() == 4);

		const std::optional<draw3::StrokeTileFootprint> highlighter =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::Highlighter,
				{ { 64.0f, 127.0f, 999.0f } }), fourK);
		INK_HISTORY_CHECK(highlighter.has_value());
		INK_HISTORY_CHECK(highlighter->pixelBounds.left == 60.75f);
		INK_HISTORY_CHECK(highlighter->pixelBounds.right == 67.25f);
		INK_HISTORY_CHECK(highlighter->pixelBounds.top == 100.0f);
		INK_HISTORY_CHECK(highlighter->pixelBounds.bottom == 154.0f);
		INK_HISTORY_CHECK(ContainsTile(highlighter->undoTiles, { 0, 0 }));
		INK_HISTORY_CHECK(ContainsTile(highlighter->undoTiles, { 0, 1 }));

		const draw3::InkPixelBounds shapeVisible{ 0.0f, 0.0f, 512.0f, 512.0f };
		const std::optional<draw3::StrokeTileFootprint> solidLine =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::SolidLine,
				{ { 64.0f, 64.0f, 4.0f }, { 448.0f, 448.0f, 4.0f } }), shapeVisible);
		INK_HISTORY_CHECK(solidLine.has_value());
		INK_HISTORY_CHECK(solidLine && solidLine->pixelBounds.left == 60.0f);
		INK_HISTORY_CHECK(solidLine && solidLine->pixelBounds.right == 452.0f);
		const std::optional<draw3::StrokeTileFootprint> outlineRectangle =
			draw3::BuildStrokeTileFootprint(MakeStroke(
				draw3::StoredInkType::OutlineRectangle,
				{ { 448.0f, 448.0f, 4.0f }, { 64.0f, 64.0f, 4.0f } }), shapeVisible);
		INK_HISTORY_CHECK(outlineRectangle.has_value());
		INK_HISTORY_CHECK(outlineRectangle &&
			!ContainsTile(outlineRectangle->undoTiles, { 1, 1 }));
		INK_HISTORY_CHECK(outlineRectangle &&
			ContainsTile(outlineRectangle->undoTiles, { 0, 0 }));
		const std::optional<draw3::StrokeTileFootprint> filledRectangle =
			draw3::BuildStrokeTileFootprint(MakeStroke(
				draw3::StoredInkType::FilledRectangle,
				{ { 448.0f, 448.0f, 4.0f }, { 64.0f, 64.0f, 4.0f } }), shapeVisible);
		INK_HISTORY_CHECK(filledRectangle.has_value());
		INK_HISTORY_CHECK(filledRectangle &&
			ContainsTile(filledRectangle->undoTiles, { 1, 1 }));
		INK_HISTORY_CHECK(filledRectangle && filledRectangle->pixelBounds.left == 62.0f);
		INK_HISTORY_CHECK(filledRectangle && filledRectangle->pixelBounds.right == 450.0f);

		const std::optional<draw3::StrokeTileFootprint> offscreen =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::Pen,
				{ { -10000.0f, -10000.0f, 4.0f } }), fourK);
		INK_HISTORY_CHECK(offscreen.has_value());
		INK_HISTORY_CHECK(offscreen->undoTiles.empty());
		INK_HISTORY_CHECK(offscreen->compositionTiles.empty());

		const float maximum = (std::numeric_limits<float>::max)();
		const std::optional<draw3::StrokeTileFootprint> extremeFinite =
			draw3::BuildStrokeTileFootprint(MakeStroke(draw3::StoredInkType::Pen,
				{ { maximum, maximum, 1.0f } }), fourK);
		INK_HISTORY_CHECK(extremeFinite.has_value());
		INK_HISTORY_CHECK(extremeFinite->pixelBounds.left == fourK.left);
		INK_HISTORY_CHECK(extremeFinite->pixelBounds.bottom == fourK.bottom);
		INK_HISTORY_CHECK(extremeFinite->undoTiles.empty());
		INK_HISTORY_CHECK(!draw3::BuildStrokeTileFootprint(
			draw3::InkStroke({ draw3::StoredInkType::Pen, 0, 1.0f, 0 }, {}),
			fourK).has_value());
	}

	void TestHistoryAndRangeTree(int& failures)
	{
		const draw3::SignedTileCoordinate tileA{ 0, 0 };
		const draw3::SignedTileCoordinate tileB{ 1, 0 };
		const draw3::SignedTileCoordinate tileC{ -1, 2 };
		draw3::CanvasRuntimeHistory branchHistory;
		const std::optional<draw3::RenderItemId> first =
			branchHistory.AppendStroke(10, MakeFootprint(tileA));
		const std::optional<draw3::RenderItemId> second =
			branchHistory.AppendStroke(11, MakeFootprint(tileB, 20.0f));
		INK_HISTORY_CHECK(first.has_value() && second.has_value());
		INK_HISTORY_CHECK(first->index == 0 && second->index == 1);
		INK_HISTORY_CHECK(first->generation != second->generation);
		INK_HISTORY_CHECK(branchHistory.LastVisibleItem() == second);
		INK_HISTORY_CHECK(!branchHistory.UndoLastVisible(*first));
		INK_HISTORY_CHECK(branchHistory.Find(*second)->visible);
		INK_HISTORY_CHECK(branchHistory.UndoLastVisible(*second));
		INK_HISTORY_CHECK(!branchHistory.Find(*second)->visible);
		INK_HISTORY_CHECK(branchHistory.LastVisibleItem() == first);

		const std::optional<draw3::RenderItemId> branch =
			branchHistory.AppendStroke(12, MakeFootprint(tileC, 40.0f));
		INK_HISTORY_CHECK(branch.has_value() && branch->index == 2);
		INK_HISTORY_CHECK(branchHistory.Items().size() == 3);
		INK_HISTORY_CHECK(branchHistory.Items()[1].strokeIndex == 11);
		INK_HISTORY_CHECK(!branchHistory.Items()[1].visible);
		INK_HISTORY_CHECK(branchHistory.UndoLastVisible() == branch);
		INK_HISTORY_CHECK(branchHistory.UndoLastVisible() == first);
		INK_HISTORY_CHECK(!branchHistory.UndoLastVisible().has_value());
		INK_HISTORY_CHECK(!branchHistory.LastVisibleItem().has_value());
		INK_HISTORY_CHECK(branchHistory.Items().size() == 3);

		draw3::CanvasRuntimeHistory history;
		std::vector<draw3::RenderItemId> ids;
		for (size_t index = 0; index < 70; ++index)
		{
			const draw3::SignedTileCoordinate tile = index % 2 == 0 ? tileA : tileB;
			const std::optional<draw3::RenderItemId> id = history.AppendStroke(
				index, MakeFootprint(tile, static_cast<float>(index * 20)));
			INK_HISTORY_CHECK(id.has_value());
			ids.push_back(*id);
		}
		const draw3::CompositionRangeTree& tree = history.CompositionTree();
		INK_HISTORY_CHECK(tree.ItemCount() == 70);
		const std::optional<draw3::CompositionNodeId> root = tree.RootNode();
		INK_HISTORY_CHECK((root == draw3::CompositionNodeId{ 0, 4 }));
		const auto children = tree.NodeChildren(*root);
		INK_HISTORY_CHECK(children.has_value());
		INK_HISTORY_CHECK((children->first == draw3::CompositionNodeId{ 0, 2 }));
		INK_HISTORY_CHECK((children->second == draw3::CompositionNodeId{ 2, 2 }));
		INK_HISTORY_CHECK(!tree.NodeChildren({ 1, 1 }).has_value());
		INK_HISTORY_CHECK((tree.LeafNodeForItem(33) ==
			draw3::CompositionNodeId{ 1, 1 }));
		const draw3::RenderItemRange rootRange = tree.NodeItemRange(*root);
		INK_HISTORY_CHECK(rootRange.begin == 0 && rootRange.end == 128);
		INK_HISTORY_CHECK(tree.NodeTouchesTile(*root, tileA));
		INK_HISTORY_CHECK(tree.NodeTouchesTile(*root, tileB));
		INK_HISTORY_CHECK(!tree.NodeTouchesTile(*root, tileC));

		const auto fullPlan = tree.DecomposeRange(0, 70);
		INK_HISTORY_CHECK(fullPlan.has_value() && fullPlan->size() == 1);
		INK_HISTORY_CHECK((*fullPlan)[0].kind ==
			draw3::CompositionRangePieceKind::CachedNode);
		INK_HISTORY_CHECK((*fullPlan)[0].node == *root);
		INK_HISTORY_CHECK((*fullPlan)[0].range.end == 70);
		const auto partialPlan = tree.DecomposeRange(1, 65);
		INK_HISTORY_CHECK(partialPlan.has_value() && partialPlan->size() == 3);
		INK_HISTORY_CHECK((*partialPlan)[0].kind ==
			draw3::CompositionRangePieceKind::OrderedItems);
		INK_HISTORY_CHECK((*partialPlan)[0].range.begin == 1 &&
			(*partialPlan)[0].range.end == 32);
		INK_HISTORY_CHECK((*partialPlan)[1].kind ==
			draw3::CompositionRangePieceKind::CachedNode);
		INK_HISTORY_CHECK(((*partialPlan)[1].node ==
			draw3::CompositionNodeId{ 1, 1 }));
		INK_HISTORY_CHECK((*partialPlan)[2].range.begin == 64 &&
			(*partialPlan)[2].range.end == 65);

		INK_HISTORY_CHECK(history.SetItemBarrier(ids[34], true));
		const auto barrierPlan = history.CompositionTree().DecomposeRange(0, 70);
		INK_HISTORY_CHECK(barrierPlan.has_value());
		INK_HISTORY_CHECK(std::any_of(barrierPlan->begin(), barrierPlan->end(),
			[](const draw3::CompositionRangePiece& piece)
			{
				return piece.kind == draw3::CompositionRangePieceKind::BarrierItem &&
					piece.range.begin == 34;
			}));
		const auto otherTilePlan = history.CompositionTree().DecomposeRange(0, 70, tileB);
		INK_HISTORY_CHECK(otherTilePlan.has_value() && otherTilePlan->size() == 1);
		INK_HISTORY_CHECK((*otherTilePlan)[0].kind ==
			draw3::CompositionRangePieceKind::CachedNode);
		INK_HISTORY_CHECK(history.SetItemBarrier(ids[34], false));

		const draw3::CompositionNodeId firstLeaf{ 0, 1 };
		const uint64_t leafABefore = history.CompositionTree().TileGeneration(firstLeaf, tileA);
		const uint64_t leafBBefore = history.CompositionTree().TileGeneration(firstLeaf, tileB);
		const uint64_t rootABefore = history.CompositionTree().TileGeneration(*root, tileA);
		const uint64_t rootBBefore = history.CompositionTree().TileGeneration(*root, tileB);
		INK_HISTORY_CHECK(history.UpdateItemGeometry(ids[0], MakeFootprint(tileC, 5.0f)));
		INK_HISTORY_CHECK(history.CompositionTree().TileGeneration(firstLeaf, tileA) > leafABefore);
		INK_HISTORY_CHECK(history.CompositionTree().TileGeneration(firstLeaf, tileB) == leafBBefore);
		INK_HISTORY_CHECK(history.CompositionTree().TileGeneration(*root, tileA) > rootABefore);
		INK_HISTORY_CHECK(history.CompositionTree().TileGeneration(*root, tileB) == rootBBefore);
		INK_HISTORY_CHECK(history.CompositionTree().TileGeneration(firstLeaf, tileC) != 0);
		INK_HISTORY_CHECK(history.CompositionTree().NodeTouchesTile({ 0, 2 }, tileC));
		INK_HISTORY_CHECK(!history.CompositionTree().NodeTouchesTile({ 2, 2 }, tileC));

		draw3::CanvasRuntimeHistory movedHistory;
		const std::optional<draw3::RenderItemId> movedItem =
			movedHistory.AppendStroke(0, MakeFootprint(tileA));
		INK_HISTORY_CHECK(movedItem.has_value());
		INK_HISTORY_CHECK(movedHistory.CompositionTree().NodeTouchesTile({ 0, 1 }, tileA));
		INK_HISTORY_CHECK(movedHistory.UpdateItemGeometry(
			*movedItem, MakeFootprint(tileC, 5.0f)));
		INK_HISTORY_CHECK(!movedHistory.CompositionTree().NodeTouchesTile({ 0, 1 }, tileA));
		INK_HISTORY_CHECK(movedHistory.CompositionTree().NodeTouchesTile({ 0, 1 }, tileC));

		const uint64_t rootAVisibilityBefore =
			history.CompositionTree().TileGeneration(*root, tileA);
		const uint64_t rootBVisibilityBefore =
			history.CompositionTree().TileGeneration(*root, tileB);
		INK_HISTORY_CHECK(history.UndoLastVisible(ids.back()));
		INK_HISTORY_CHECK(history.CompositionTree().TileGeneration(*root, tileA) ==
			rootAVisibilityBefore);
		INK_HISTORY_CHECK(history.CompositionTree().TileGeneration(*root, tileB) >
			rootBVisibilityBefore);

		// 可见链让连续撤回和隐藏分支后的 append 都保持 O(1) 尾部定位。
		draw3::CanvasRuntimeHistory visibleChain;
		std::vector<draw3::RenderItemId> chainIds;
		for (size_t index = 0; index < 96; ++index)
		{
			const std::optional<draw3::RenderItemId> id = visibleChain.AppendStroke(
				index, MakeFootprint(tileA, static_cast<float>(index * 12)));
			INK_HISTORY_CHECK(id.has_value());
			chainIds.push_back(*id);
		}
		for (size_t index = 0; index < 64; ++index)
			INK_HISTORY_CHECK(visibleChain.UndoLastVisible().has_value());
		INK_HISTORY_CHECK(visibleChain.LastVisibleItem() == chainIds[31]);
		const std::optional<draw3::RenderItemId> chainBranch = visibleChain.AppendStroke(
			96, MakeFootprint(tileC, 2000.0f));
		INK_HISTORY_CHECK(chainBranch.has_value());
		INK_HISTORY_CHECK(visibleChain.Find(*chainBranch)->previousVisibleIndex == 31u);
		INK_HISTORY_CHECK(visibleChain.UndoLastVisible() == chainBranch);
		INK_HISTORY_CHECK(visibleChain.LastVisibleItem() == chainIds[31]);
		for (size_t index = 0; index < 32; ++index)
			INK_HISTORY_CHECK(visibleChain.UndoLastVisible().has_value());
		INK_HISTORY_CHECK(!visibleChain.LastVisibleItem().has_value());
	}

	void TestCompositionLru(int& failures)
	{
		draw3::CompositionCachePlanner defaults;
		INK_HISTORY_CHECK(defaults.SlotCapacity() == 512u);
		INK_HISTORY_CHECK(defaults.PageCount() == 8u);

		draw3::CompositionCachePlanner planner(
			{ draw3::kCompositionTileBytes * 3 });
		INK_HISTORY_CHECK(planner.Acquire({ 1 }).status ==
			draw3::CompositionCacheAcquireStatus::Allocated);
		INK_HISTORY_CHECK(planner.Acquire({ 2 }).status ==
			draw3::CompositionCacheAcquireStatus::Allocated);
		INK_HISTORY_CHECK(planner.Acquire({ 3 }).status ==
			draw3::CompositionCacheAcquireStatus::Allocated);
		INK_HISTORY_CHECK(planner.Pin({ 2 }));
		INK_HISTORY_CHECK(planner.Acquire({ 1 }).status ==
			draw3::CompositionCacheAcquireStatus::Hit);
		const draw3::CompositionCacheAcquireResult fourth = planner.Acquire({ 4 });
		INK_HISTORY_CHECK(fourth.status ==
			draw3::CompositionCacheAcquireStatus::Allocated);
		INK_HISTORY_CHECK(fourth.evictedKey == draw3::CompositionCacheKeyId{ 3 });
		INK_HISTORY_CHECK(!planner.Contains({ 3 }));
		INK_HISTORY_CHECK(planner.Contains({ 1 }) && planner.Contains({ 2 }) &&
			planner.Contains({ 4 }));

		draw3::CompositionCachePlanner pinned(
			{ draw3::kCompositionTileBytes * 2 });
		INK_HISTORY_CHECK(pinned.Acquire({ 10 }).status ==
			draw3::CompositionCacheAcquireStatus::Allocated);
		INK_HISTORY_CHECK(pinned.Acquire({ 11 }).status ==
			draw3::CompositionCacheAcquireStatus::Allocated);
		INK_HISTORY_CHECK(pinned.Pin({ 10 }) && pinned.Pin({ 11 }));
		INK_HISTORY_CHECK(pinned.Acquire({ 12 }).status ==
			draw3::CompositionCacheAcquireStatus::AllUsableSlotsPinned);
		INK_HISTORY_CHECK(pinned.SetPolicy({ draw3::kCompositionTileBytes }).empty());
		INK_HISTORY_CHECK(pinned.ResidentCount() == 2 && pinned.PinnedCount() == 2);
		const std::vector<draw3::CompositionCacheKeyId> afterUnpin = pinned.Unpin({ 11 });
		INK_HISTORY_CHECK(afterUnpin.size() == 1 && afterUnpin[0].value == 11u);
		INK_HISTORY_CHECK(pinned.ResidentCount() == 1 && pinned.Contains({ 10 }));
		INK_HISTORY_CHECK(pinned.SetPolicy({ 0 }).empty());
		INK_HISTORY_CHECK(pinned.Acquire({ 13 }).status ==
			draw3::CompositionCacheAcquireStatus::Disabled);
		const std::vector<draw3::CompositionCacheKeyId> finalEviction = pinned.Unpin({ 10 });
		INK_HISTORY_CHECK(finalEviction.size() == 1 && finalEviction[0].value == 10u);
		INK_HISTORY_CHECK(pinned.ResidentCount() == 0);
		INK_HISTORY_CHECK(pinned.SetPolicy(
			{ draw3::kCompositionTileBytes * 4 }).empty());
		INK_HISTORY_CHECK(pinned.ResidentCount() == 0);
	}
}

int RunInkHistoryTests()
{
	int failures = 0;
	TestPoliciesAndUndoBudget(failures);
	TestSparseFootprints(failures);
	TestHistoryAndRangeTree(failures);
	TestCompositionLru(failures);
	if (failures == 0) std::cout << "All ink history tests passed." << std::endl;
	return failures;
}
