module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <windows.h>

export module draw3.ink_history_gpu;

import draw3.ink_document;
import draw3.ink_history;
import draw3.renderer;

export namespace draw3
{
	struct HistoryCanvasIdentity
	{
		InkGuid pageGuid = {};
		DeviceKey device = {};

		friend bool operator==(const HistoryCanvasIdentity& left,
			const HistoryCanvasIdentity& right) noexcept
		{
			return left.pageGuid.Bytes() == right.pageGuid.Bytes() &&
				left.device.Value() == right.device.Value();
		}
	};

	// Raster key 隔离 DPI/格式/管线变化；viewport 平移不进入 key。
	struct InkHistoryRasterKey
	{
		DeviceKey device = {};
		float scale = 1.0f;
		uint32_t targetFormat = 0;
		uint64_t pipelineGeneration = 1;

		friend bool operator==(const InkHistoryRasterKey& left,
			const InkHistoryRasterKey& right) noexcept
		{
			return left.device.Value() == right.device.Value() &&
				left.scale == right.scale && left.targetFormat == right.targetFormat &&
				left.pipelineGeneration == right.pipelineGeneration;
		}
	};

	using InkRasterStateToken = uint64_t;

	enum class HotPreimageCaptureStatus : uint8_t
	{
		Captured,
		Disabled,
		EntryTooLarge,
		ResourceFailure,
		InvalidRequest
	};

	struct HotPreimageCaptureRequest
	{
		HistoryCanvasIdentity canvas = {};
		RenderItemId item = {};
		InkHistoryRasterKey rasterKey = {};
		InkRasterStateToken beforeState = 0;
		InkRasterStateToken afterState = 0;
		std::span<const SignedTileCoordinate> tiles;
		float viewportX = 0.0f;
		float viewportY = 0.0f;
		int canvasWidth = 0;
		int canvasHeight = 0;
	};

	struct HotPreimageCaptureResult
	{
		HotPreimageCaptureStatus status = HotPreimageCaptureStatus::InvalidRequest;
		uint64_t ticket = 0;
		size_t tileCount = 0;
	};

	struct HotPreimageRestoreResult
	{
		bool restored = false;
		InkRasterStateToken restoredState = 0;
		RECT dirty = {};
	};

	enum class CompositionRestorePath : uint8_t
	{
		CompositionCache,
		CompositionRebuild,
		OrderedTileReplay,
		Empty,
		Failed
	};

	struct CompositionRestoreRequest
	{
		HistoryCanvasIdentity canvas = {};
		InkHistoryRasterKey rasterKey = {};
		const InkCanvas* documentCanvas = nullptr;
		const CanvasRuntimeHistory* history = nullptr;
		std::span<const SignedTileCoordinate> tiles;
		size_t rangeEnd = 0;
		float viewportX = 0.0f;
		float viewportY = 0.0f;
		int canvasWidth = 0;
		int canvasHeight = 0;
		bool clearTargetTiles = true;
		// 冷撤回先按候选可见性重建，成功后再提交 CPU history。
		std::optional<RenderItemId> excludedItem;
	};

	struct CompositionRestoreResult
	{
		CompositionRestorePath path = CompositionRestorePath::Failed;
		RECT dirty = {};
		size_t tileCount = 0;
	};

	// 绘制线程独占的 GPU cache；全部方法只提交即时上下文命令，不做 readback/wait。
	class InkHistoryGpuCache
	{
	public:
		InkHistoryGpuCache();
		~InkHistoryGpuCache();
		InkHistoryGpuCache(InkHistoryGpuCache&&) noexcept;
		InkHistoryGpuCache& operator=(InkHistoryGpuCache&&) noexcept;
		InkHistoryGpuCache(const InkHistoryGpuCache&) = delete;
		InkHistoryGpuCache& operator=(const InkHistoryGpuCache&) = delete;

		bool Initialize(InkRenderer& renderer,
			UndoCachePolicy undoPolicy = {},
			CompositionCachePolicy compositionPolicy = {});
		void Release() noexcept;
		void SetUndoPolicy(UndoCachePolicy policy);
		void SetCompositionPolicy(CompositionCachePolicy policy);

		HotPreimageCaptureResult CapturePreimage(
			const HotPreimageCaptureRequest& request);
		bool CommitPreimage(uint64_t ticket) noexcept;
		void CancelPreimage(uint64_t ticket) noexcept;
		HotPreimageRestoreResult RestorePreimage(
			HistoryCanvasIdentity canvas, RenderItemId item,
			InkHistoryRasterKey rasterKey, InkRasterStateToken currentState,
			float viewportX, float viewportY,
			int canvasWidth, int canvasHeight);
		size_t ConsecutiveHotDepth(HistoryCanvasIdentity canvas,
			InkHistoryRasterKey rasterKey, InkRasterStateToken currentState) const noexcept;
		void DiscardHotPreimages() noexcept;

		CompositionRestoreResult RestoreComposition(
			const CompositionRestoreRequest& request);
		bool PrimeCompositionNode(HistoryCanvasIdentity canvas,
			InkHistoryRasterKey rasterKey, const InkCanvas& documentCanvas,
			const CanvasRuntimeHistory& history, CompositionNodeId node,
			SignedTileCoordinate tile, int canvasWidth, int canvasHeight);
		void DiscardCompositionCache() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
