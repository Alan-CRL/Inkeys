module;

#include "Draw3.Bridge.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>

module Inkeys.Drawing.Draw3.presentation_pages;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		struct PageRoute
		{
			draw3::uink::UInkGuid pageGuid;
			std::uint32_t pageIndex;
			std::optional<std::int32_t> slideId;
			bool retained;
			bool endScreen;
		};

		PresentationLaneProjection RejectProjection(PresentationPageConflictKind kind,
			std::optional<std::size_t> sourceIndex = std::nullopt)
		{
			PresentationLaneProjection result;
			result.conflict = PresentationPageConflict{ kind, sourceIndex };
			return result;
		}

		template <class PageAt>
		PresentationLaneProjection ProjectPages(const PresentationPageTopology& topology,
			std::size_t sourceCount, PageAt pageAt)
		{
			using Conflict = PresentationPageConflictKind;
			const bool stable = topology.binding == 0;
			if ((topology.binding != 0 && topology.binding != 1) ||
				topology.pageCount == 0 || topology.pageCount > Bridge::kMaximumPresentationPages ||
				(stable ? topology.slideIds.size() != topology.pageCount : !topology.slideIds.empty()))
				return RejectProjection(Conflict::InvalidTopology);

			std::map<std::int32_t, std::uint32_t> topologyBySlideId;
			for (std::uint32_t index = 0; index < topology.slideIds.size(); ++index)
			{
				const auto slideId = topology.slideIds[index];
				if (slideId <= 0) return RejectProjection(Conflict::InvalidTopology);
				if (!topologyBySlideId.emplace(slideId, index).second)
					return RejectProjection(Conflict::DuplicateTopologySlideId);
			}

			PresentationLaneProjection result;
			result.active.resize(topology.pageCount);
			std::set<std::array<std::uint8_t, 16>> seenGuids;
			std::set<std::int32_t> seenSlideIds;
			for (std::size_t index = 0; index < sourceCount; ++index)
			{
				const PageRoute page = pageAt(index);
				if (page.pageGuid.IsZero())
					return RejectProjection(Conflict::InvalidPageIdentity, index);
				if (!seenGuids.insert(page.pageGuid.Bytes()).second)
					return RejectProjection(Conflict::DuplicateGuid, index);

				if (page.endScreen)
				{
					if (page.retained || page.slideId)
						return RejectProjection(Conflict::InvalidEndScreen, index);
					if (result.endScreen)
						return RejectProjection(Conflict::DuplicateEndScreen, index);
					result.endScreen = index;
					continue;
				}

				std::uint32_t activeIndex = page.pageIndex;
				if (stable)
				{
					if (!page.slideId || *page.slideId <= 0)
						return RejectProjection(Conflict::InvalidSlideId, index);
					if (!seenSlideIds.insert(*page.slideId).second)
						return RejectProjection(Conflict::DuplicateSlideId, index);
					const auto slide = topologyBySlideId.find(*page.slideId);
					// 当前拓扑决定 SlideID 页是否活动，允许原保留页重新出现。
					if (slide == topologyBySlideId.end())
					{
						result.retained.push_back(index);
						continue;
					}
					activeIndex = slide->second;
				}
				else
				{
					if (page.slideId)
						return RejectProjection(Conflict::InvalidSlideId, index);
					if (page.retained)
					{
						result.retained.push_back(index);
						continue;
					}
				}

				if (activeIndex >= topology.pageCount)
					return RejectProjection(Conflict::PageIndexOutOfRange, index);
				if (result.active[activeIndex])
					return RejectProjection(Conflict::DuplicatePageIndex, index);
				result.active[activeIndex] = index;
			}

			// 保留页沿用旧 SlideID 升序。页序保留页按已有页序排列，同序时以 GUID 确定顺序。
			std::sort(result.retained.begin(), result.retained.end(),
				[&](std::size_t left, std::size_t right)
				{
					const PageRoute a = pageAt(left);
					const PageRoute b = pageAt(right);
					if (stable) return a.slideId < b.slideId;
					if (a.pageIndex != b.pageIndex) return a.pageIndex < b.pageIndex;
					return a.pageGuid.Bytes() < b.pageGuid.Bytes();
				});
			return result;
		}

		bool MetadataChanged(const PresentationPageWriteMetadata& page,
			const PagedInkPageMetadata& confirmed) noexcept
		{
			return page.pageIndex != confirmed.pageIndex || page.pageNumber != confirmed.pageNumber ||
				page.slideId != confirmed.slideId || page.binding.value_or(0) != confirmed.binding ||
				(page.pageState == 1) != (confirmed.pageState == 1) ||
				(page.pageKind == 1) != (confirmed.pageKind == 1);
		}
	}

	PresentationPageMetadataPlan BuildPresentationPageMetadata(
		const PresentationPageTopology& topology, std::span<const PresentationPageInput> pages)
	{
		using Conflict = PresentationPageConflictKind;
		for (std::size_t index = 0; index < pages.size(); ++index)
			if (pages[index].confirmed && pages[index].confirmed->pageGuid != pages[index].pageGuid)
				return { {}, PresentationPageConflict{ Conflict::InvalidPageIdentity, index } };

		const auto projection = ProjectPages(topology, pages.size(), [&](std::size_t index)
			{
				const auto& page = pages[index];
				const auto slideId = topology.binding == 0 && !page.endScreen ? page.slideId : std::nullopt;
				return PageRoute{ page.pageGuid, page.pageIndex, slideId, page.retained, page.endScreen };
			});
		if (projection.conflict) return { {}, projection.conflict };
		// 页码比索引多一，结束页之后为保留页连续预留页序。
		if (projection.retained.size() > (std::numeric_limits<std::uint32_t>::max)() -
			topology.pageCount - 1)
			return { {}, PresentationPageConflict{ Conflict::PageIndexOutOfRange, std::nullopt } };

		PresentationPageMetadataPlan result;
		result.pages.resize(pages.size());
		auto setPage = [&](std::size_t sourceIndex, std::uint32_t pageIndex, bool retained, bool endScreen)
		{
			const auto& source = pages[sourceIndex];
			auto& page = result.pages[sourceIndex];
			page.pageGuid = source.pageGuid;
			page.pageIndex = pageIndex;
			page.pageNumber = pageIndex + 1;
			if (topology.binding == 1) page.binding = 1;
			else if (!endScreen) page.slideId = source.slideId;
			if (retained) page.pageState = 1;
			if (endScreen) page.pageKind = 1;
			page.revisionRequired = source.confirmed && MetadataChanged(page, *source.confirmed);
		};

		for (std::uint32_t index = 0; index < topology.pageCount; ++index)
			if (projection.active[index]) setPage(*projection.active[index], index, false, false);
		if (projection.endScreen) setPage(*projection.endScreen, topology.pageCount, false, true);
		std::uint32_t retainedIndex = topology.pageCount + 1;
		for (const auto sourceIndex : projection.retained)
			setPage(sourceIndex, retainedIndex++, true, false);
		return result;
	}

	PresentationLaneProjection ProjectPresentationLane(
		const PresentationPageTopology& topology, std::span<const PagedInkLanePage> pages)
	{
		using Conflict = PresentationPageConflictKind;
		for (std::size_t index = 0; index < pages.size(); ++index)
		{
			const auto& page = pages[index];
			if (page.metadata.binding != topology.binding)
				return RejectProjection(Conflict::BindingMismatch, index);
			if (page.snapshot && (page.snapshot->pageGuid != page.metadata.pageGuid || page.snapshot->deviceGuid))
				return RejectProjection(Conflict::InvalidPageIdentity, index);
		}
		return ProjectPages(topology, pages.size(), [&](std::size_t index)
			{
				const auto& page = pages[index].metadata;
				return PageRoute{ page.pageGuid, page.pageIndex, page.slideId,
					page.pageState == 1, page.pageKind == 1 };
			});
	}
}
