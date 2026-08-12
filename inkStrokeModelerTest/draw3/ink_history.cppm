module;

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module draw3.ink_history;

import draw3.ink_document;

export namespace draw3
{
	inline constexpr uint32_t kUndoTileSize = 128;
	inline constexpr uint64_t kUndoTileBytes =
		static_cast<uint64_t>(kUndoTileSize) * kUndoTileSize * 4;
	inline constexpr uint32_t kUndoTilesPerPage = 256;
	inline constexpr uint32_t kCompositionTileSize = 256;
	inline constexpr uint64_t kCompositionTileBytes =
		static_cast<uint64_t>(kCompositionTileSize) * kCompositionTileSize * 6;
	inline constexpr uint32_t kCompositionTilesPerPage = 64;
	inline constexpr uint32_t kCompositionLeafItemCount = 32;

	struct UndoCachePolicy
	{
		uint64_t byteBudget = 64ull * 1024ull * 1024ull;
		uint32_t maxEntries = 20;
	};

	struct CompositionCachePolicy
	{
		uint64_t byteBudget = 192ull * 1024ull * 1024ull;
	};

	// Tile 坐标始终位于 Canvas 空间，负值不会被裁成屏幕坐标。
	struct SignedTileCoordinate
	{
		int32_t x = 0;
		int32_t y = 0;

		friend auto operator<=>(const SignedTileCoordinate&,
			const SignedTileCoordinate&) noexcept = default;
	};

	// 半开像素区域 [left, right) x [top, bottom)。
	struct InkPixelBounds
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	struct StrokeTileFootprint
	{
		InkPixelBounds pixelBounds = {};
		std::vector<SignedTileCoordinate> undoTiles;
		std::vector<SignedTileCoordinate> compositionTiles;
	};

	// 只枚举与当前可见区域相交的稀疏 Tile；无法安全量化坐标时返回空。
	std::optional<StrokeTileFootprint> BuildStrokeTileFootprint(
		const InkStroke& stroke,
		std::optional<InkPixelBounds> visibleBounds = std::nullopt);
	std::optional<size_t> CountTilesCoveringPixelBounds(
		InkPixelBounds bounds, uint32_t tileSize) noexcept;

	struct RenderItemId
	{
		uint32_t index = 0;
		uint32_t generation = 0;

		constexpr bool IsValid() const noexcept { return generation != 0; }
		friend bool operator==(const RenderItemId&, const RenderItemId&) noexcept = default;
	};

	struct RenderItemState
	{
		RenderItemId id = {};
		size_t strokeIndex = 0;
		bool visible = true;
		bool compositionBarrier = false;
		InkPixelBounds pixelBounds = {};
		std::vector<SignedTileCoordinate> undoTiles;
		std::vector<SignedTileCoordinate> compositionTiles;
		uint64_t contentGeneration = 1;
		std::optional<uint32_t> previousVisibleIndex;
	};

	struct CompositionNodeId
	{
		uint32_t firstBlock = 0;
		uint32_t blockCount = 0;

		constexpr bool IsValid() const noexcept { return blockCount != 0; }
		friend auto operator<=>(const CompositionNodeId&,
			const CompositionNodeId&) noexcept = default;
	};

	struct RenderItemRange
	{
		size_t begin = 0;
		size_t end = 0;
	};

	enum class CompositionRangePieceKind : uint8_t
	{
		CachedNode,
		OrderedItems,
		BarrierItem
	};

	struct CompositionRangePiece
	{
		CompositionRangePieceKind kind = CompositionRangePieceKind::OrderedItems;
		RenderItemRange range = {};
		CompositionNodeId node = {};
	};

	// 树只保存 CPU 拓扑和 Tile generation，GPU operator 是可淘汰缓存。
	class CompositionRangeTree
	{
	public:
		bool AppendRenderItem(const RenderItemState& item);
		bool SetItemVisibility(RenderItemId id, bool visible);
		bool UpdateItemGeometry(RenderItemId id,
			InkPixelBounds bounds, std::span<const SignedTileCoordinate> compositionTiles);
		bool SetItemBarrier(RenderItemId id, bool barrier);

		size_t ItemCount() const noexcept;
		std::optional<CompositionNodeId> RootNode() const noexcept;
		std::optional<CompositionNodeId> LeafNodeForItem(size_t itemIndex) const noexcept;
		std::optional<std::pair<CompositionNodeId, CompositionNodeId>> NodeChildren(
			CompositionNodeId node) const noexcept;
		RenderItemRange NodeItemRange(CompositionNodeId node) const noexcept;
		uint64_t TileGeneration(CompositionNodeId node,
			SignedTileCoordinate tile) const noexcept;
		bool NodeTouchesTile(CompositionNodeId node,
			SignedTileCoordinate tile) const noexcept;

		// 指定 Tile 时，只把实际覆盖该 Tile 的非仿射项视为屏障。
		std::optional<std::vector<CompositionRangePiece>> DecomposeRange(
			size_t begin, size_t end,
			std::optional<SignedTileCoordinate> tile = std::nullopt) const;

	private:
		struct TreeItem
		{
			RenderItemId id = {};
			bool visible = true;
			bool barrier = false;
			InkPixelBounds bounds = {};
			std::vector<SignedTileCoordinate> tiles;
		};

		struct NodeTileKey
		{
			CompositionNodeId node = {};
			SignedTileCoordinate tile = {};

			friend auto operator<=>(const NodeTileKey&, const NodeTileKey&) noexcept = default;
		};

		void InvalidateTiles(size_t itemIndex,
			std::span<const SignedTileCoordinate> tiles);

		std::vector<TreeItem> items_;
		std::map<NodeTileKey, uint64_t> tileGenerations_;
		std::map<SignedTileCoordinate, std::vector<uint32_t>> tileBlocks_;
		uint64_t generationClock_ = 0;
		size_t visibleBarrierCount_ = 0;
	};

	// Runtime sidecar 只隐藏最后可见项；不删除 Stroke，也不暴露 redo。
	class CanvasRuntimeHistory
	{
	public:
		std::optional<RenderItemId> AppendStroke(size_t strokeIndex,
			StrokeTileFootprint footprint, bool affineOperator = true);
		std::optional<RenderItemId> LastVisibleItem() const noexcept;
		bool UndoLastVisible(RenderItemId expected);
		std::optional<RenderItemId> UndoLastVisible();
		bool UpdateItemGeometry(RenderItemId id, StrokeTileFootprint footprint);
		bool SetItemBarrier(RenderItemId id, bool barrier);

		const RenderItemState* Find(RenderItemId id) const noexcept;
		std::span<const RenderItemState> Items() const noexcept;
		// 由可见项引用计数维护稀疏索引，只枚举与查询范围相交的 Canvas Tile。
		std::vector<SignedTileCoordinate> VisibleCompositionTiles(
			InkPixelBounds bounds) const;
		uint64_t Revision() const noexcept;
		const CompositionRangeTree& CompositionTree() const noexcept;

	private:
		RenderItemState* FindMutable(RenderItemId id) noexcept;
		void AddVisibleCompositionTiles(
			std::span<const SignedTileCoordinate> tiles);
		void RemoveVisibleCompositionTiles(
			std::span<const SignedTileCoordinate> tiles);

		std::vector<RenderItemState> items_;
		CompositionRangeTree compositionTree_;
		std::map<SignedTileCoordinate, uint32_t> visibleCompositionTileReferences_;
		uint64_t revision_ = 0;
		uint32_t nextItemGeneration_ = 1;
		std::optional<uint32_t> lastVisibleIndex_;
	};

	struct UndoCacheEntryId
	{
		uint64_t value = 0;
		friend bool operator==(const UndoCacheEntryId&,
			const UndoCacheEntryId&) noexcept = default;
	};

	struct UndoCacheEntryState
	{
		UndoCacheEntryId id = {};
		size_t tileCount = 0;
		bool committed = false;
	};

	enum class UndoCacheReservationStatus : uint8_t
	{
		Reserved,
		Disabled,
		EntryTooLarge,
		DuplicateEntry,
		ReservationPending
	};

	struct UndoCacheReservation
	{
		UndoCacheReservationStatus status = UndoCacheReservationStatus::Disabled;
		UndoCacheEntryId id = {};
		size_t tileCount = 0;
		std::vector<UndoCacheEntryId> evictedEntries;
	};

	// 前像按 Stroke FIFO 淘汰，并提供 capture 前 reserve / 成功后 commit。
	class UndoCachePlanner
	{
	public:
		explicit UndoCachePlanner(UndoCachePolicy policy = {});

		std::vector<UndoCacheEntryId> SetPolicy(UndoCachePolicy policy);
		UndoCacheReservation Reserve(UndoCacheEntryId id, size_t tileCount);
		bool Commit(UndoCacheEntryId id) noexcept;
		bool Cancel(UndoCacheEntryId id) noexcept;
		bool Consume(UndoCacheEntryId id) noexcept;
		bool Contains(UndoCacheEntryId id) const noexcept;

		const UndoCachePolicy& Policy() const noexcept;
		size_t SlotCapacity() const noexcept;
		size_t PageCount() const noexcept;
		size_t UsedSlotCount() const noexcept;
		std::span<const UndoCacheEntryState> Entries() const noexcept;

	private:
		std::vector<UndoCacheEntryId> TrimToPolicy();

		UndoCachePolicy policy_ = {};
		std::vector<UndoCacheEntryState> entries_;
		size_t usedSlotCount_ = 0;
	};

	struct CompositionCacheKeyId
	{
		uint64_t value = 0;
		friend bool operator==(const CompositionCacheKeyId&,
			const CompositionCacheKeyId&) noexcept = default;
	};

	struct CompositionCacheSlotId
	{
		uint32_t value = 0;
		friend bool operator==(const CompositionCacheSlotId&,
			const CompositionCacheSlotId&) noexcept = default;
	};

	enum class CompositionCacheAcquireStatus : uint8_t
	{
		Hit,
		Allocated,
		Disabled,
		AllUsableSlotsPinned
	};

	struct CompositionCacheAcquireResult
	{
		CompositionCacheAcquireStatus status = CompositionCacheAcquireStatus::Disabled;
		CompositionCacheSlotId slot = {};
		std::optional<CompositionCacheKeyId> evictedKey;
	};

	// Slot 规划器不持有 D3D 资源；pin 期间预算下降只延迟对应槽的释放。
	class CompositionCachePlanner
	{
	public:
		explicit CompositionCachePlanner(CompositionCachePolicy policy = {});

		std::vector<CompositionCacheKeyId> SetPolicy(CompositionCachePolicy policy);
		CompositionCacheAcquireResult Acquire(CompositionCacheKeyId key);
		bool Pin(CompositionCacheKeyId key) noexcept;
		std::vector<CompositionCacheKeyId> Unpin(CompositionCacheKeyId key);
		bool Release(CompositionCacheKeyId key) noexcept;
		bool Contains(CompositionCacheKeyId key) const noexcept;
		std::optional<CompositionCacheSlotId> SlotFor(
			CompositionCacheKeyId key) const noexcept;

		const CompositionCachePolicy& Policy() const noexcept;
		size_t SlotCapacity() const noexcept;
		size_t PageCount() const noexcept;
		size_t ResidentCount() const noexcept;
		size_t PinnedCount() const noexcept;

	private:
		struct ResidentEntry
		{
			CompositionCacheKeyId key = {};
			CompositionCacheSlotId slot = {};
			uint64_t lastUse = 0;
			uint32_t pinCount = 0;
		};

		std::vector<CompositionCacheKeyId> TrimToPolicy();
		void Touch(ResidentEntry& entry) noexcept;

		CompositionCachePolicy policy_ = {};
		std::vector<ResidentEntry> entries_;
		uint64_t useClock_ = 0;
	};
}
