#include "Draw3.HistoryProbe.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>

import Inkeys.Drawing.Draw3.ink_document;
import Inkeys.Drawing.Draw3.ink_history;

#pragma comment(lib, "psapi.lib")

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr size_t kWarmupBlocks = 3;
		constexpr size_t kMeasuredBlocks = 11;
		constexpr InkPixelBounds kVisibleBounds{ 0.0f, 0.0f, 2048.0f, 2048.0f };

		struct Scenario
		{
			const char* name;
			size_t strokeCount;
			bool mixedEraser;
		};

		constexpr std::array<Scenario, 6> kScenarios = { {
			{ "pen_10", 10, false }, { "mixed_eraser_10", 10, true },
			{ "pen_100", 100, false }, { "mixed_eraser_100", 100, true },
			{ "pen_1000", 1000, false }, { "mixed_eraser_1000", 1000, true }
		} };

		struct PhaseTimes
		{
			uint64_t appendNs = 0;
			uint64_t footprintNs = 0;
			uint64_t historyNs = 0;
			uint64_t undoNs = 0;
			uint64_t redoNs = 0;
			uint64_t branchUndoNs = 0;
			uint64_t branchDiscardNs = 0;
			uint64_t branchAppendNs = 0;
			uint64_t branchFootprintNs = 0;
			uint64_t branchHistoryNs = 0;
			uint64_t visibleQueryNs = 0;
			uint64_t decomposeNs = 0;
		};

		struct ProcessResources
		{
			bool memoryAvailable = false;
			bool handlesAvailable = false;
			uint64_t privateBytes = 0;
			uint64_t workingSetBytes = 0;
			uint32_t handles = 0;
		};

		uint64_t ElapsedNs(Clock::time_point start) noexcept
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<
				std::chrono::nanoseconds>(Clock::now() - start).count());
		}

		ProcessResources ReadProcessResources() noexcept
		{
			ProcessResources result;
			PROCESS_MEMORY_COUNTERS_EX counters = {};
			counters.cb = sizeof(counters);
			if (GetProcessMemoryInfo(GetCurrentProcess(),
				reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
			{
				result.memoryAvailable = true;
				result.privateBytes = static_cast<uint64_t>(counters.PrivateUsage);
				result.workingSetBytes = static_cast<uint64_t>(counters.WorkingSetSize);
			}
			DWORD handles = 0;
			if (GetProcessHandleCount(GetCurrentProcess(), &handles))
			{
				result.handlesAvailable = true;
				result.handles = handles;
			}
			return result;
		}

		InkGuid MakeGuid(uint8_t marker) noexcept
		{
			std::array<uint8_t, 16> bytes = {};
			bytes[0] = marker;
			bytes[15] = 0xA5;
			return InkGuid(bytes);
		}

		uint64_t Mix(uint64_t digest, uint64_t value) noexcept
		{
			return (digest ^ value) * 1099511628211ull;
		}

		std::vector<InkStroke> MakeStrokes(const Scenario& scenario)
		{
			std::vector<InkStroke> strokes;
			strokes.reserve(scenario.strokeCount);
			for (size_t index = 0; index < scenario.strokeCount; ++index)
			{
				const bool eraser = scenario.mixedEraser && index % 4 == 3;
				StoredInkStyle style;
				style.inkType = eraser ? StoredInkType::Eraser : StoredInkType::Pen;
				style.fallbackRgb = 0x1267AB;
				style.opacity = 1.0f;
				std::vector<StoredInkPoint> points;
				points.reserve(64);
				// 每四笔共用一段轨迹，让橡皮覆盖前面的笔，同时保持固定输入顺序。
				const size_t group = scenario.mixedEraser ? index / 4 : index;
				const float originX = 32.0f + static_cast<float>(group % 20) * 40.0f;
				const float originY = 40.0f + static_cast<float>(group / 20) * 40.0f;
				for (size_t point = 0; point < 64; ++point)
					points.push_back({ originX + static_cast<float>(point) * 0.75f,
						originY + static_cast<float>((point * 7) % 13) * 0.6f,
						eraser ? 18.0f : 3.5f });
				strokes.emplace_back(style, std::move(points));
			}
			return strokes;
		}

		bool Fail(const Scenario& scenario, size_t block, const char* invariant) noexcept
		{
			std::fprintf(stderr, "Draw3HistoryBenchmark FAIL scenario=%s block=%zu invariant=%s\n",
				scenario.name, block, invariant);
			return false;
		}

		uint64_t Digest(const InkCanvasCollection& document,
			const CanvasRuntimeHistory& history) noexcept
		{
			uint64_t digest = 1469598103934665603ull;
			for (const InkPage& page : document.Pages())
			{
				for (uint8_t byte : page.PageGuid().Bytes()) digest = Mix(digest, byte);
				for (const InkCanvas& canvas : page.Canvases())
				{
					for (const InkStroke& stroke : canvas.Strokes())
					{
						digest = Mix(digest, static_cast<uint8_t>(stroke.Style().inkType));
						digest = Mix(digest, stroke.Points().size());
						for (const StoredInkPoint& point : stroke.Points())
						{
							digest = Mix(digest, std::bit_cast<uint32_t>(point.x));
							digest = Mix(digest, std::bit_cast<uint32_t>(point.y));
							digest = Mix(digest, std::bit_cast<uint32_t>(point.width));
						}
					}
				}
			}
			for (const RenderItemState& item : history.Items())
			{
				digest = Mix(digest, item.id.index);
				digest = Mix(digest, item.id.generation);
				digest = Mix(digest, item.strokeIndex);
				digest = Mix(digest, item.visible);
				digest = Mix(digest, item.contentGeneration);
				digest = Mix(digest, std::bit_cast<uint32_t>(item.pixelBounds.left));
				digest = Mix(digest, std::bit_cast<uint32_t>(item.pixelBounds.top));
				digest = Mix(digest, std::bit_cast<uint32_t>(item.pixelBounds.right));
				digest = Mix(digest, std::bit_cast<uint32_t>(item.pixelBounds.bottom));
				digest = Mix(digest, item.undoTiles.size());
				digest = Mix(digest, item.compositionTiles.size());
				for (SignedTileCoordinate tile : item.undoTiles)
				{
					digest = Mix(digest, static_cast<uint32_t>(tile.x));
					digest = Mix(digest, static_cast<uint32_t>(tile.y));
				}
				for (SignedTileCoordinate tile : item.compositionTiles)
				{
					digest = Mix(digest, static_cast<uint32_t>(tile.x));
					digest = Mix(digest, static_cast<uint32_t>(tile.y));
				}
			}
			return digest;
		}

		bool CheckExtremeFootprintBoundary() noexcept
		{
			StoredInkStyle style;
			style.inkType = StoredInkType::Pen;
			const float extreme = (std::numeric_limits<float>::max)();
			InkStroke stroke(style, { { extreme, 1.0f, extreme } });
			const bool valid = stroke.IsValid();
			const bool footprintRejected = !BuildStrokeTileFootprint(stroke);
			std::fprintf(stdout,
				"Draw3HistoryBenchmark boundary=extreme_finite extreme_valid=%d footprint_rejected=%d\n",
				valid ? 1 : 0, footprintRejected ? 1 : 0);
			return valid && footprintRejected;
		}

		bool RunBlock(const Scenario& scenario, size_t block, bool warmup,
			const std::vector<InkStroke>& source, std::optional<uint64_t>& expectedDigest)
		{
			// 从输入复制前记录进程基线，append 后的快照才包含已转入文档的点数组。
			const ProcessResources before = ReadProcessResources();
			// 输入拷贝在计时区外完成；append、footprint、history 各测生产接口。
			std::vector<InkStroke> prepared = source;
			InkStroke branchStroke = source.front();
			InkCanvasCollection document(MakeGuid(0x10));
			const std::optional<size_t> firstPage = document.AppendPage(MakeGuid(0x11));
			const std::optional<size_t> secondPage = document.AppendPage(MakeGuid(0x12));
			if (!firstPage || *firstPage != 0 || !secondPage || *secondPage != 1)
				return Fail(scenario, block, "page identity creation");
			InkPage* page = document.PageAt(0);
			InkPage* untouchedPage = document.PageAt(1);
			InkCanvas* canvas = page ? page->GetOrCreateCanvas(kDefaultDeviceKey) : nullptr;
			InkCanvas* untouchedCanvas = untouchedPage
				? untouchedPage->GetOrCreateCanvas(kDefaultDeviceKey) : nullptr;
			if (!canvas || !untouchedCanvas) return Fail(scenario, block, "canvas creation");
			CanvasRuntimeHistory history;
			std::vector<RenderItemId> undone;
			undone.reserve((std::max)(size_t{ 1 }, scenario.strokeCount / 4));
			PhaseTimes times;
			size_t undoTileCount = 0;
			size_t compositionTileCount = 0;

			for (size_t index = 0; index < scenario.strokeCount; ++index)
			{
				const auto appendStart = Clock::now();
				const std::optional<size_t> strokeIndex =
					canvas->AppendStroke(std::move(prepared[index]));
				times.appendNs += ElapsedNs(appendStart);
				if (!strokeIndex || *strokeIndex != index)
					return Fail(scenario, block, "append returned sequential stroke index");
				const auto footprintStart = Clock::now();
				std::optional<StrokeTileFootprint> footprint =
					BuildStrokeTileFootprint(canvas->Strokes()[*strokeIndex]);
				times.footprintNs += ElapsedNs(footprintStart);
				if (!footprint || footprint->undoTiles.empty() ||
					footprint->compositionTiles.empty())
					return Fail(scenario, block, "nonempty tile footprint");
				undoTileCount += footprint->undoTiles.size();
				compositionTileCount += footprint->compositionTiles.size();
				const auto historyStart = Clock::now();
				const std::optional<RenderItemId> id =
					history.AppendStroke(*strokeIndex, std::move(*footprint), true);
				times.historyNs += ElapsedNs(historyStart);
				if (!id || id->index != index || !history.Find(*id) ||
					history.Find(*id)->strokeIndex != index)
					return Fail(scenario, block, "history ID maps to stored stroke");
			}
			const ProcessResources afterAppend = ReadProcessResources();
			const size_t undoCount = (std::max)(size_t{ 1 }, scenario.strokeCount / 4);
			for (size_t index = 0; index < undoCount; ++index)
			{
				const auto start = Clock::now();
				const std::optional<RenderItemId> id = history.UndoLastVisible();
				times.undoNs += ElapsedNs(start);
				if (!id) return Fail(scenario, block, "undo returned item");
				undone.push_back(*id);
			}
			if (history.RedoDepth() != undoCount)
				return Fail(scenario, block, "undo created redo depth");
			for (size_t index = 0; index < undoCount; ++index)
			{
				const std::optional<RenderItemId> id = history.LastRedoItem();
				const auto start = Clock::now();
				const bool redone = id && history.RedoLastUndone(*id);
				times.redoNs += ElapsedNs(start);
				if (!redone || *id != undone[undoCount - index - 1])
					return Fail(scenario, block, "redo restored exact reverse order");
			}
			if (history.RedoDepth() != 0 || !history.LastVisibleItem() ||
				history.LastVisibleItem()->index != scenario.strokeCount - 1)
				return Fail(scenario, block, "redo restored full visible tail");
			for (size_t index = 0; index < undoCount; ++index)
			{
				const auto start = Clock::now();
				const auto id = history.UndoLastVisible();
				times.branchUndoNs += ElapsedNs(start);
				if (!id) return Fail(scenario, block, "branch undo returned item");
			}
			const auto branchDiscardStart = Clock::now();
			history.DiscardRedoBranch();
			times.branchDiscardNs += ElapsedNs(branchDiscardStart);
			const auto branchAppendStart = Clock::now();
			const std::optional<size_t> branchIndex =
				canvas->AppendStroke(std::move(branchStroke));
			times.branchAppendNs += ElapsedNs(branchAppendStart);
			if (!branchIndex || *branchIndex != scenario.strokeCount)
				return Fail(scenario, block, "branch appended without reusing stored index");
			const auto branchFootprintStart = Clock::now();
			std::optional<StrokeTileFootprint> branchFootprint =
				BuildStrokeTileFootprint(canvas->Strokes()[*branchIndex]);
			times.branchFootprintNs += ElapsedNs(branchFootprintStart);
			if (!branchFootprint) return Fail(scenario, block, "branch footprint");
			const auto branchHistoryStart = Clock::now();
			const std::optional<RenderItemId> branchId =
				history.AppendStroke(*branchIndex, std::move(*branchFootprint), true);
			times.branchHistoryNs += ElapsedNs(branchHistoryStart);
			if (!branchId || branchId->index != scenario.strokeCount ||
				history.RedoDepth() != 0 || history.LastRedoItem())
				return Fail(scenario, block, "new branch invalidated redo");
			const ProcessResources afterBranch = ReadProcessResources();

			const auto visibleQueryStart = Clock::now();
			const auto visibleTiles = history.VisibleCompositionTiles(kVisibleBounds);
			times.visibleQueryNs += ElapsedNs(visibleQueryStart);
			if (visibleTiles.empty()) return Fail(scenario, block, "visible composition tiles");
			const auto decomposeStart = Clock::now();
			const auto pieces = history.CompositionTree().DecomposeRange(
				0, history.Items().size(), visibleTiles.front());
			times.decomposeNs += ElapsedNs(decomposeStart);
			if (!pieces || pieces->empty())
				return Fail(scenario, block, "tile-specific composition range");
			const size_t visibleCount = static_cast<size_t>(std::count_if(
				history.Items().begin(), history.Items().end(),
				[](const RenderItemState& item) { return item.visible; }));
			if (visibleCount != scenario.strokeCount - undoCount + 1 ||
				history.Items().size() != scenario.strokeCount + 1 ||
				history.CompositionTree().ItemCount() != history.Items().size() ||
				canvas->Strokes().size() != scenario.strokeCount + 1 ||
				!untouchedCanvas->Strokes().empty() ||
				document.WorkspaceGuid() != MakeGuid(0x10) ||
				page->PageGuid() != MakeGuid(0x11) ||
				untouchedPage->PageGuid() != MakeGuid(0x12))
				return Fail(scenario, block, "visible/retained counts and page isolation");
			const uint64_t digest = Digest(document, history);
			if (expectedDigest && digest != *expectedDigest)
				return Fail(scenario, block, "deterministic document/history digest");
			expectedDigest = digest;

			std::fprintf(stdout,
				"Draw3HistoryBenchmark scenario=%s phase=%s block=%zu strokes=%zu "
				"append_ns=%llu footprint_ns=%llu history_ns=%llu undo_ns=%llu "
				"redo_ns=%llu branch_undo_ns=%llu branch_discard_ns=%llu branch_append_ns=%llu "
				"branch_footprint_ns=%llu branch_history_ns=%llu visible_query_ns=%llu decompose_ns=%llu "
				"undo_tiles=%zu composition_tiles=%zu visible_tiles=%zu pieces=%zu "
				"stored_strokes=%zu retained_items=%zu visible_items=%zu redo_items=%zu eraser_strokes=%zu "
				"memory_available=%d private_before=%llu private_append=%llu private_branch=%llu "
				"working_set_before=%llu working_set_append=%llu working_set_branch=%llu "
				"handles_available=%d handles_before=%u handles_append=%u handles_branch=%u digest=%llu\n",
				scenario.name, warmup ? "warmup" : "measured", block,
				scenario.strokeCount,
				static_cast<unsigned long long>(times.appendNs),
				static_cast<unsigned long long>(times.footprintNs),
				static_cast<unsigned long long>(times.historyNs),
				static_cast<unsigned long long>(times.undoNs),
				static_cast<unsigned long long>(times.redoNs),
				static_cast<unsigned long long>(times.branchUndoNs),
				static_cast<unsigned long long>(times.branchDiscardNs),
				static_cast<unsigned long long>(times.branchAppendNs),
				static_cast<unsigned long long>(times.branchFootprintNs),
				static_cast<unsigned long long>(times.branchHistoryNs),
				static_cast<unsigned long long>(times.visibleQueryNs),
				static_cast<unsigned long long>(times.decomposeNs),
				undoTileCount, compositionTileCount, visibleTiles.size(), pieces->size(),
				canvas->Strokes().size(), history.Items().size(), visibleCount,
				history.RedoDepth(), scenario.mixedEraser ? scenario.strokeCount / 4 : 0,
				before.memoryAvailable && afterAppend.memoryAvailable &&
					afterBranch.memoryAvailable ? 1 : 0,
				static_cast<unsigned long long>(before.privateBytes),
				static_cast<unsigned long long>(afterAppend.privateBytes),
				static_cast<unsigned long long>(afterBranch.privateBytes),
				static_cast<unsigned long long>(before.workingSetBytes),
				static_cast<unsigned long long>(afterAppend.workingSetBytes),
				static_cast<unsigned long long>(afterBranch.workingSetBytes),
				before.handlesAvailable && afterAppend.handlesAvailable &&
					afterBranch.handlesAvailable ? 1 : 0,
				before.handles, afterAppend.handles, afterBranch.handles,
				static_cast<unsigned long long>(digest));
			return true;
		}

		bool RunScenario(const Scenario& scenario)
		{
			const std::vector<InkStroke> source = MakeStrokes(scenario);
			std::optional<uint64_t> expectedDigest;
			for (size_t block = 0; block < kWarmupBlocks + kMeasuredBlocks; ++block)
			{
				const bool warmup = block < kWarmupBlocks;
				if (!RunBlock(scenario,
					warmup ? block + 1 : block - kWarmupBlocks + 1,
					warmup, source, expectedDigest)) return false;
			}
			return true;
		}
	}

	int RunDraw3HistoryBenchmark() noexcept
	{
		try
		{
			if (!CheckExtremeFootprintBoundary()) return 1;
			std::fprintf(stdout,
				"Draw3HistoryBenchmark metric=production_cpu_document_footprint_history "
				"warmup_blocks=%zu measured_blocks=%zu point_count=64 hwnd=0 gpu=0 present=0\n",
				kWarmupBlocks, kMeasuredBlocks);
			for (const Scenario& scenario : kScenarios)
				if (!RunScenario(scenario)) return 1;
			std::fflush(stdout);
			return 0;
		}
		catch (const std::exception& error)
		{
			std::fprintf(stderr, "Draw3HistoryBenchmark exception: %s\n", error.what());
			return 2;
		}
		catch (...)
		{
			std::fputs("Draw3HistoryBenchmark unknown exception\n", stderr);
			return 2;
		}
	}
}
