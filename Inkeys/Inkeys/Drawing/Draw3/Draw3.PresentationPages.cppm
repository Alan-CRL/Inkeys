module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module Inkeys.Drawing.Draw3.presentation_pages;

export import Inkeys.Drawing.Draw3.paged_ink_store;

export namespace Inkeys::Drawing::Draw3
{
	struct PresentationPageTopology
	{
		std::int32_t binding = 0; // 有效 UInk 绑定：0 为 SlideID，1 为页序。
		std::uint32_t pageCount = 0; // 真实幻灯片数，结束页占下一槽。
		std::span<const std::int32_t> slideIds;
	};

	struct PresentationPageInput
	{
		draw3::uink::UInkGuid pageGuid;
		std::uint32_t pageIndex = 0; // 页序绑定的当前索引。
		std::optional<std::int32_t> slideId;
		bool retained = false;
		bool endScreen = false;
		std::optional<PagedInkPageMetadata> confirmed;
	};

	enum class PresentationPageConflictKind : std::uint8_t
	{
		InvalidTopology, DuplicateTopologySlideId, InvalidPageIdentity, BindingMismatch,
		InvalidSlideId, InvalidEndScreen, DuplicateGuid, DuplicateSlideId,
		DuplicateEndScreen, PageIndexOutOfRange, DuplicatePageIndex
	};

	struct PresentationPageConflict
	{
		PresentationPageConflictKind kind;
		// 页列表中的位置。拓扑本身冲突时没有页位置。
		std::optional<std::size_t> sourceIndex;
	};

	struct PresentationPageWriteMetadata
	{
		draw3::uink::UInkGuid pageGuid;
		std::uint32_t pageIndex = 0;
		std::uint32_t pageNumber = 0;
		std::optional<std::int32_t> slideId;
		std::optional<std::int32_t> binding;
		std::optional<std::int32_t> pageState;
		std::optional<std::int32_t> pageKind;
		// 只对已确认页比较元数据，未登记页仍由内容变更决定是否写入。
		bool revisionRequired = false;
	};

	struct PresentationPageMetadataPlan
	{
		std::vector<PresentationPageWriteMetadata> pages; // 与输入页顺序一致。
		std::optional<PresentationPageConflict> conflict;
	};

	struct PresentationLaneProjection
	{
		// 值为回执列表索引。即使该页读取失败，也保留其索引供调用方结算。
		std::vector<std::optional<std::size_t>> active;
		std::vector<std::size_t> retained;
		std::optional<std::size_t> endScreen;
		std::optional<PresentationPageConflict> conflict;
	};

	PresentationPageMetadataPlan BuildPresentationPageMetadata(
		const PresentationPageTopology& topology, std::span<const PresentationPageInput> pages);

	PresentationLaneProjection ProjectPresentationLane(
		const PresentationPageTopology& topology, std::span<const PagedInkLanePage> pages);
}
