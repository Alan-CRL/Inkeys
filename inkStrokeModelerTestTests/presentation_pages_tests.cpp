#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"

import Inkeys.Drawing.Draw3.presentation_pages;

namespace
{
	using namespace Inkeys::Drawing::Draw3;
	using draw3::uink::UInkGuid;
	using Conflict = PresentationPageConflictKind;

	bool Check(int& failures, bool passed, const char* expression)
	{
		if (!passed)
		{
			++failures;
			std::cerr << "[PresentationPages] failed: " << expression << '\n';
		}
		return passed;
	}

#define PRESENTATION_PAGES_CHECK(expression) Check(failures, (expression), #expression)

	UInkGuid Guid(std::uint8_t id)
	{
		std::array<std::uint8_t, 16> bytes{};
		bytes.front() = id;
		return UInkGuid(bytes);
	}

	PagedInkPageMetadata Confirmed(std::uint8_t id, std::uint32_t index,
		std::optional<std::int32_t> slideId, std::int32_t binding = 0,
		std::optional<std::int32_t> state = std::nullopt,
		std::optional<std::int32_t> kind = std::nullopt)
	{
		PagedInkPageMetadata page;
		page.pageGuid = Guid(id);
		page.pageIndex = index;
		page.pageNumber = index + 1;
		page.slideId = slideId;
		page.binding = binding;
		page.pageState = state;
		page.pageKind = kind;
		return page;
	}

	PagedInkLanePage Loaded(PagedInkPageMetadata metadata, bool readSucceeded = true)
	{
		PagedInkLanePage page;
		page.metadata = metadata;
		page.result = readSucceeded ? PagedInkResult::Succeeded : PagedInkResult::Failed;
		page.error = readSucceeded ? PagedInkError::None : PagedInkError::ReadFailed;
		if (readSucceeded) page.snapshot.emplace().pageGuid = metadata.pageGuid;
		return page;
	}

	void TestStablePresentationPageMetadata(int& failures)
	{
		const std::array<std::int32_t, 6> slideIds{ 20, 10, 50, 15, 40, 60 };
		const PresentationPageTopology topology{ 0, 6, slideIds };
		const std::array pages{
			PresentationPageInput{ Guid(1), 3, 30, false, false, Confirmed(1, 3, 30) },
			PresentationPageInput{ Guid(2), 0, 10, false, false, Confirmed(2, 0, 10) },
			PresentationPageInput{ Guid(3), 5, std::nullopt, false, true,
				Confirmed(3, 5, std::nullopt, 0, std::nullopt, 1) },
			PresentationPageInput{ Guid(4), 1, 20, false, false, Confirmed(4, 1, 20) },
			PresentationPageInput{ Guid(5), 6, 15, true, false, Confirmed(5, 6, 15, 0, 1) },
			PresentationPageInput{ Guid(6), 4, 40, false, false, std::nullopt },
			PresentationPageInput{ Guid(7), 4, 12, false, false, Confirmed(7, 4, 12) },
			PresentationPageInput{ Guid(8), 2, 50, false, false, Confirmed(8, 2, 50, 0, 0, 0) },
		};
		const auto plan = BuildPresentationPageMetadata(topology, pages);
		if (!PRESENTATION_PAGES_CHECK(!plan.conflict && plan.pages.size() == pages.size())) return;

		const std::array<std::uint32_t, 8> indices{ 8, 1, 6, 0, 3, 4, 7, 2 };
		for (std::size_t index = 0; index < pages.size(); ++index)
		{
			const auto& output = plan.pages[index];
			PRESENTATION_PAGES_CHECK(output.pageGuid == pages[index].pageGuid);
			PRESENTATION_PAGES_CHECK(output.pageIndex == indices[index] && output.pageNumber == indices[index] + 1);
			PRESENTATION_PAGES_CHECK(output.slideId == pages[index].slideId && !output.binding);
			PRESENTATION_PAGES_CHECK(output.revisionRequired == (index != 5 && index != 7));
		}
		PRESENTATION_PAGES_CHECK(plan.pages[0].pageState == 1 && plan.pages[6].pageState == 1);
		PRESENTATION_PAGES_CHECK(!plan.pages[4].pageState && !plan.pages[4].pageKind);
		PRESENTATION_PAGES_CHECK(plan.pages[2].pageKind == 1 && !plan.pages[2].pageState);
	}

	void TestFallbackPresentationPageMetadata(int& failures)
	{
		const PresentationPageTopology topology{ 1, 4, {} };
		const std::array pages{
			PresentationPageInput{ Guid(1), 3, 301, false, false, Confirmed(1, 3, std::nullopt, 1) },
			PresentationPageInput{ Guid(2), 9, {}, true, false, Confirmed(2, 9, std::nullopt, 1, 1) },
			PresentationPageInput{ Guid(3), 8, {}, true, false, Confirmed(3, 8, std::nullopt, 1, 1) },
			PresentationPageInput{ Guid(4), 6, 403, false, true, Confirmed(4, 6, std::nullopt, 1, 0, 1) },
		};
		const auto plan = BuildPresentationPageMetadata(topology, pages);
		if (!PRESENTATION_PAGES_CHECK(!plan.conflict && plan.pages.size() == pages.size())) return;

		const std::array<std::uint32_t, 4> indices{ 3, 6, 5, 4 };
		for (std::size_t index = 0; index < pages.size(); ++index)
		{
			const auto& output = plan.pages[index];
			PRESENTATION_PAGES_CHECK(output.pageGuid == pages[index].pageGuid);
			PRESENTATION_PAGES_CHECK(output.pageIndex == indices[index] && output.pageNumber == indices[index] + 1);
			PRESENTATION_PAGES_CHECK(output.binding == 1 && !output.slideId);
			PRESENTATION_PAGES_CHECK(output.revisionRequired == (index != 0));
		}
		PRESENTATION_PAGES_CHECK(plan.pages[1].pageState == 1 && plan.pages[2].pageState == 1);
		PRESENTATION_PAGES_CHECK(plan.pages[3].pageKind == 1 && !plan.pages[3].pageState);
	}

	void TestPresentationMetadataChangeFields(int& failures)
	{
		const std::array<std::int32_t, 1> slideIds{ 10 };
		const PresentationPageTopology topology{ 0, 1, slideIds };
		const auto equal = Confirmed(1, 0, 10, 0, 0, 0);
		auto number = equal;
		++number.pageNumber;
		auto slide = equal;
		slide.slideId = 20;
		auto binding = equal;
		binding.binding = 1;
		auto state = equal;
		state.pageState = 1;
		auto kind = equal;
		kind.pageKind = 1;
		const std::array changed{ number, slide, binding, state, kind };

		auto checkPlan = [&](const PagedInkPageMetadata& confirmed, bool shouldRevise)
		{
			const std::array pages{ PresentationPageInput{ Guid(1), 0, 10, false, false, confirmed } };
			const auto plan = BuildPresentationPageMetadata(topology, pages);
			if (!PRESENTATION_PAGES_CHECK(!plan.conflict && plan.pages.size() == 1)) return;
			PRESENTATION_PAGES_CHECK(plan.pages[0].revisionRequired == shouldRevise);
		};
		checkPlan(equal, false);
		for (const auto& confirmed : changed) checkPlan(confirmed, true);

		const std::array wrongIdentity{ PresentationPageInput{ Guid(2), 0, 10, false, false, equal } };
		const auto invalid = BuildPresentationPageMetadata(topology, wrongIdentity);
		if (!PRESENTATION_PAGES_CHECK(invalid.conflict.has_value())) return;
		PRESENTATION_PAGES_CHECK(invalid.conflict->kind == Conflict::InvalidPageIdentity);
		PRESENTATION_PAGES_CHECK(invalid.conflict->sourceIndex == 0 && invalid.pages.empty());
	}

	void TestStablePresentationLaneProjection(int& failures)
	{
		const std::array<std::int32_t, 4> slideIds{ 200, 500, 100, 400 };
		const PresentationPageTopology topology{ 0, 4, slideIds };
		const std::array pages{
			Loaded(Confirmed(1, 1, 300)),
			Loaded(Confirmed(2, 9, 100, 0, 1)),
			Loaded(Confirmed(3, 1, 200), false),
			Loaded(Confirmed(4, 7, std::nullopt, 0, std::nullopt, 1)),
			Loaded(Confirmed(5, 8, 250, 0, 1)),
		};
		const auto projected = ProjectPresentationLane(topology, pages);
		if (!PRESENTATION_PAGES_CHECK(!projected.conflict && projected.active.size() == 4
			&& projected.retained.size() == 2)) return;

		PRESENTATION_PAGES_CHECK(projected.active[0] == 2 && !projected.active[1]);
		PRESENTATION_PAGES_CHECK(projected.active[2] == 1 && !projected.active[3]);
		PRESENTATION_PAGES_CHECK(projected.retained[0] == 4 && projected.retained[1] == 0);
		PRESENTATION_PAGES_CHECK(projected.endScreen == 3);
		// 登记页读失败时仍占原槽；只有未登记的槽以空索引表示。
		if (!PRESENTATION_PAGES_CHECK(projected.active[0] && *projected.active[0] < pages.size())) return;
		const auto& failedPage = pages[*projected.active[0]];
		PRESENTATION_PAGES_CHECK(failedPage.metadata.pageGuid == Guid(3) &&
			failedPage.result == PagedInkResult::Failed && !failedPage.snapshot);
	}

	void TestFallbackPresentationLaneProjection(int& failures)
	{
		const PresentationPageTopology topology{ 1, 5, {} };
		const std::array pages{
			Loaded(Confirmed(1, 3, std::nullopt, 1)),
			Loaded(Confirmed(2, 1, std::nullopt, 1)),
			Loaded(Confirmed(3, 8, std::nullopt, 1, 1)),
			Loaded(Confirmed(4, 3, std::nullopt, 1, 0, 1)),
			Loaded(Confirmed(5, 2, std::nullopt, 1, 1)),
		};
		const auto projected = ProjectPresentationLane(topology, pages);
		if (!PRESENTATION_PAGES_CHECK(!projected.conflict && projected.active.size() == 5
			&& projected.retained.size() == 2)) return;

		PRESENTATION_PAGES_CHECK(projected.active[1] == 1 && projected.active[3] == 0);
		PRESENTATION_PAGES_CHECK(!projected.active[0] && !projected.active[2] && !projected.active[4]);
		PRESENTATION_PAGES_CHECK(projected.retained[0] == 4 && projected.retained[1] == 2);
		PRESENTATION_PAGES_CHECK(projected.endScreen == 3);

		const auto empty = ProjectPresentationLane(topology, {});
		if (!PRESENTATION_PAGES_CHECK(!empty.conflict && empty.active.size() == 5)) return;
		PRESENTATION_PAGES_CHECK(empty.retained.empty() && !empty.endScreen);
		for (const auto& page : empty.active) PRESENTATION_PAGES_CHECK(!page);
	}

	void TestPresentationLaneConflicts(int& failures)
	{
		const std::array<std::int32_t, 2> slideIds{ 10, 20 };
		const PresentationPageTopology stable{ 0, 2, slideIds };
		const PresentationPageTopology fallback{ 1, 2, {} };
		auto expectConflict = [&](const PresentationPageTopology& topology,
			std::span<const PagedInkLanePage> pages, Conflict kind, std::optional<std::size_t> sourceIndex)
		{
			const auto projection = ProjectPresentationLane(topology, pages);
			if (!PRESENTATION_PAGES_CHECK(projection.conflict.has_value())) return;
			PRESENTATION_PAGES_CHECK(projection.conflict->kind == kind);
			PRESENTATION_PAGES_CHECK(projection.conflict->sourceIndex == sourceIndex);
			PRESENTATION_PAGES_CHECK(projection.active.empty() && projection.retained.empty() && !projection.endScreen);
		};

		const std::array duplicateGuid{ Loaded(Confirmed(1, 0, 10)), Loaded(Confirmed(1, 1, 20), false) };
		expectConflict(stable, duplicateGuid, Conflict::DuplicateGuid, 1);
		const std::array duplicateSlideId{ Loaded(Confirmed(1, 0, 10)), Loaded(Confirmed(2, 3, 10, 0, 1), false) };
		expectConflict(stable, duplicateSlideId, Conflict::DuplicateSlideId, 1);
		const std::array duplicateEnd{
			Loaded(Confirmed(1, 2, std::nullopt, 0, 0, 1)),
			Loaded(Confirmed(2, 2, std::nullopt, 0, 0, 1), false),
		};
		expectConflict(stable, duplicateEnd, Conflict::DuplicateEndScreen, 1);

		const std::array fallbackDuplicateIndex{
			Loaded(Confirmed(1, 1, std::nullopt, 1)), Loaded(Confirmed(2, 1, std::nullopt, 1), false),
		};
		expectConflict(fallback, fallbackDuplicateIndex, Conflict::DuplicatePageIndex, 1);
		const std::array outOfRange{ Loaded(Confirmed(1, 2, std::nullopt, 1)) };
		expectConflict(fallback, outOfRange, Conflict::PageIndexOutOfRange, 0);
		expectConflict(stable, outOfRange, Conflict::BindingMismatch, 0);
		const std::array invalidSlide{ Loaded(Confirmed(1, 0, std::nullopt)) };
		expectConflict(stable, invalidSlide, Conflict::InvalidSlideId, 0);
		const std::array fallbackWithSlideId{ Loaded(Confirmed(1, 1, 20, 1)) };
		expectConflict(fallback, fallbackWithSlideId, Conflict::InvalidSlideId, 0);
		const std::array invalidEnd{ Loaded(Confirmed(1, 2, std::nullopt, 0, 1, 1)) };
		expectConflict(stable, invalidEnd, Conflict::InvalidEndScreen, 0);
		const std::array endWithSlideId{ Loaded(Confirmed(1, 2, 20, 0, 0, 1)) };
		expectConflict(stable, endWithSlideId, Conflict::InvalidEndScreen, 0);
		const std::array zeroGuid{ Loaded(Confirmed(0, 0, 10)) };
		expectConflict(stable, zeroGuid, Conflict::InvalidPageIdentity, 0);

		std::array invalidSnapshot{ Loaded(Confirmed(1, 0, 10)) };
		if (!PRESENTATION_PAGES_CHECK(invalidSnapshot[0].snapshot.has_value())) return;
		invalidSnapshot[0].snapshot->deviceGuid = Guid(2);
		expectConflict(stable, invalidSnapshot, Conflict::InvalidPageIdentity, 0);
		invalidSnapshot[0].snapshot->deviceGuid.reset();
		invalidSnapshot[0].snapshot->pageGuid = Guid(2);
		expectConflict(stable, invalidSnapshot, Conflict::InvalidPageIdentity, 0);

		const std::array<std::int32_t, 2> duplicateTargetIds{ 10, 10 };
		expectConflict({ 0, 2, duplicateTargetIds }, {}, Conflict::DuplicateTopologySlideId, std::nullopt);
		expectConflict({ 0, 3, slideIds }, {}, Conflict::InvalidTopology, std::nullopt);
		expectConflict({ 1, Bridge::kMaximumPresentationPages + 1, {} }, {}, Conflict::InvalidTopology, std::nullopt);
	}
}

int RunPresentationPagesTests()
{
	int failures = 0;
	TestStablePresentationPageMetadata(failures);
	TestFallbackPresentationPageMetadata(failures);
	TestPresentationMetadataChangeFields(failures);
	TestStablePresentationLaneProjection(failures);
	TestFallbackPresentationLaneProjection(failures);
	TestPresentationLaneConflicts(failures);
	return failures;
}
