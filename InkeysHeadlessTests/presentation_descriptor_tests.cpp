#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.h"

#include <iostream>
#include <string>
#include <utility>

namespace
{
	using namespace Inkeys::Drawing::Draw3;
	static_assert(sizeof(Bridge::PresentationReadyIdentity) <= 64,
		"runtime ready identity must stay fixed-size");

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[PresentationDescriptor] failed: " << name << '\n';
		return condition;
	}

	std::wstring StableJson(const wchar_t* provider, const wchar_t* path)
	{
		return std::wstring(L"{\"schemaVersion\":1,\"provider\":\"") + provider +
			L"\",\"status\":\"StableSlideIds\",\"fullName\":\"" + path +
			L"\",\"presentationName\":\"Lesson.pptx\",\"applicationProcessId\":42,"
			L"\"slideShowHwnd\":84,\"currentPage\":2,\"totalPage\":3,"
			L"\"currentSlideId\":202,\"slideIds\":[101,202,303],"
			L"\"bindingRevision\":7}";
	}
}

int RunPresentationDescriptorTests()
{
	int failures = 0;
	if (!Expect(Bridge::PresentationInputSuppressed(
		Bridge::Workspace::Presentation, true) &&
		!Bridge::PresentationInputSuppressed(Bridge::Workspace::Presentation, false) &&
		!Bridge::PresentationInputSuppressed(Bridge::Workspace::Desktop, true),
		"only a pending Presentation load suppresses input")) ++failures;
	if (!Expect(Bridge::PresentationCanvasCommandSuppressed(
		Bridge::Workspace::Presentation, true, false) &&
		!Bridge::PresentationCanvasCommandSuppressed(
			Bridge::Workspace::Presentation, true, true) &&
		!Bridge::PresentationCanvasCommandSuppressed(
			Bridge::Workspace::Desktop, true, false),
		"load suppresses canvas commands but preserves the final exit barrier"))
		++failures;
	if (!Expect(ResolvePresentationTargetDisposition(
		PresentationDescriptorStatus::StableSlideIds, false, 7, 7) ==
			PresentationTargetDisposition::PreservePrevious &&
		ResolvePresentationTargetDisposition(
			PresentationDescriptorStatus::TransientBusy, true, 7, 7) ==
			PresentationTargetDisposition::PreservePrevious &&
		ResolvePresentationTargetDisposition(
			PresentationDescriptorStatus::TransientBusy, true, 8, 7) ==
			PresentationTargetDisposition::Isolate &&
		ResolvePresentationTargetDisposition(
			PresentationDescriptorStatus::StableSlideIds, false, 8, 7) ==
			PresentationTargetDisposition::Isolate &&
		ResolvePresentationTargetDisposition(
			PresentationDescriptorStatus::Unavailable, true, 7, 7) ==
			PresentationTargetDisposition::Isolate,
		"only the same binding may preserve a stale or busy target")) ++failures;
	const auto powerPoint = ParsePresentationDescriptorJson(
		StableJson(L"PowerPoint", L"C:\\\\课程\\\\Lesson.pptx"));
	const auto wps = ParsePresentationDescriptorJson(
		StableJson(L"Wps", L"c:\\\\课程\\\\LESSON.pptx"));
	const auto first = powerPoint.descriptor
		? ResolvePresentationTarget(*powerPoint.descriptor) : std::nullopt;
	const auto second = wps.descriptor
		? ResolvePresentationTarget(*wps.descriptor) : std::nullopt;
	if (!Expect(first && second && first->key == second->key,
		"same Unicode path is provider/case independent")) ++failures;
	if (!Expect(first && first->bindingMode == Bridge::SlideBindingMode::StableSlideId &&
		first->pageIndex == 1 && first->slideId == 202 &&
		first->slideIds.size() == 3,
		"stable descriptor preserves page and SlideID topology")) ++failures;
	if (first)
	{
		const auto ready = Bridge::ReadyIdentityFor(*first);
		if (!Expect(ready.key == first->key && ready.slideId == first->slideId &&
			ready.pageIndex == first->pageIndex &&
			ready.bindingRevision == first->bindingRevision,
			"runtime ready snapshot keeps identity without copying topology")) ++failures;
	}

	const std::wstring fallbackJson =
		L"{\"schemaVersion\":1,\"provider\":\"Wps\","
		L"\"status\":\"PageIndexFallback\",\"fullName\":\"D:\\\\deck.pptx\","
		L"\"presentationName\":\"deck.pptx\",\"applicationProcessId\":12,"
		L"\"slideShowHwnd\":34,\"currentPage\":1,\"totalPage\":2,"
		L"\"currentSlideId\":null,\"slideIds\":[],\"bindingRevision\":9}";
	const auto fallback = ParsePresentationDescriptorJson(fallbackJson);
	const auto fallbackTarget = fallback.descriptor
		? ResolvePresentationTarget(*fallback.descriptor) : std::nullopt;
	if (!Expect(fallbackTarget && fallbackTarget->bindingMode ==
		Bridge::SlideBindingMode::PageIndexFallback && !fallbackTarget->slideId &&
		fallbackTarget->slideIds.empty(),
		"fallback never synthesizes a SlideID")) ++failures;
	std::wstring oversizedFallback = fallbackJson;
	const std::wstring fallbackPages = L"\"totalPage\":2";
	oversizedFallback.replace(oversizedFallback.find(fallbackPages),
		fallbackPages.size(), L"\"totalPage\":10001");
	if (!Expect(!ParsePresentationDescriptorJson(oversizedFallback).descriptor,
		"fallback totalPage cannot bypass the 10000-page budget")) ++failures;
	if (fallbackTarget)
	{
		Bridge::PresentationTarget oversizedTarget = *fallbackTarget;
		oversizedTarget.totalPages = Bridge::kMaximumPresentationPages + 1;
		Bridge::StateBridge boundedBridge;
		if (!Expect(!boundedBridge.PublishPresentationTarget(
			std::move(oversizedTarget)),
			"bridge rejects oversized fallback targets before controller allocation"))
			++failures;
		Bridge::PresentationTarget upgraded = *fallbackTarget;
		upgraded.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		upgraded.slideIds = { 11, 22 };
		upgraded.slideId = 11;
		if (!Expect(CanUpgradePresentationBindingByOrdinal(
			*fallbackTarget, upgraded, 2),
			"same binding can upgrade all ordinal pages to stable SlideIDs"))
			++failures;
		if (!Expect(CanReusePresentationDocumentSlot(
			*fallbackTarget, upgraded, 2),
			"parked fallback slots accept the same proven ordinal upgrade"))
			++failures;
		upgraded.bindingToken += ":other";
		upgraded.bindingRevision += 1;
		if (!Expect(CanUpgradePresentationBindingByOrdinal(
			*fallbackTarget, upgraded, 2),
			"a saved-path fallback can upgrade after slideshow re-entry")) ++failures;
		Bridge::PresentationTarget otherFallback = *fallbackTarget;
		otherFallback.bindingRevision += 1;
		otherFallback.bindingToken += ":other";
		if (!Expect(CanReusePresentationDocumentSlot(
			*fallbackTarget, otherFallback, 2),
			"a saved-path fallback survives a new slideshow HWND")) ++failures;
		Bridge::PresentationTarget processLocalFallback = *fallbackTarget;
		processLocalFallback.processLocalIdentity = true;
		processLocalFallback.sourceIdentity = "process:current:wps:12:34:9:deck";
		processLocalFallback.key.bytes[0] ^= 0x80;
		Bridge::PresentationTarget otherLocalBinding = processLocalFallback;
		otherLocalBinding.bindingRevision += 1;
		otherLocalBinding.bindingToken += ":other";
		if (!Expect(!CanReusePresentationDocumentSlot(
			processLocalFallback, otherLocalBinding, 2),
			"process-local fallback remains isolated to its exact binding")) ++failures;
	}
	if (first)
	{
		Bridge::PresentationTarget rebound = *first;
		rebound.bindingRevision += 1;
		rebound.bindingToken += ":rebound";
		if (!Expect(CanReusePresentationDocumentSlot(*first, rebound, 3),
			"stable SlideID slots survive an Office binding refresh")) ++failures;
		Bridge::PresentationTarget originalOrder = *first;
		originalOrder.slideIds = { 101, 202, 303, 404 };
		originalOrder.slideId = 202;
		originalOrder.pageIndex = 1;
		originalOrder.totalPages = 4;
		Bridge::PresentationTarget reordered = originalOrder;
		reordered.slideIds = { 101, 303, 202, 404 };
		reordered.slideId = 303;
		reordered.pageIndex = 1;
		if (!Expect(CanReusePresentationDocumentSlot(originalOrder, reordered, 4),
			"stable SlideID slots survive slide reorder")) ++failures;
		if (!Expect(StablePresentationTopologyChanged(originalOrder, reordered),
			"parked stable slots remap when the ordered SlideID topology changes"))
			++failures;
		Bridge::PresentationTarget pageTurn = originalOrder;
		pageTurn.pageIndex = 2;
		pageTurn.slideId = 303;
		pageTurn.bindingRevision += 1;
		pageTurn.bindingToken += ":page-turn";
		pageTurn.targetRevision += 1;
		if (!Expect(!StablePresentationTopologyChanged(originalOrder, pageTurn),
			"page, SlideID and target revisions do not rebuild stable page runtime"))
			++failures;
		Bridge::PresentationTarget inserted = reordered;
		inserted.slideIds = { 101, 303, 505, 202, 404 };
		inserted.totalPages = 5;
		if (!Expect(CanReusePresentationDocumentSlot(originalOrder, inserted, 5),
			"stable SlideID slots accept inserted slides")) ++failures;
		Bridge::PresentationTarget deleted = inserted;
		deleted.slideIds = { 101, 303, 202, 404 };
		deleted.totalPages = 4;
		if (!Expect(CanReusePresentationDocumentSlot(inserted, deleted, 4),
			"stable SlideID slots accept deleted slides")) ++failures;
	}

	std::wstring duplicate = StableJson(L"PowerPoint", L"C:\\\\dup.pptx");
	const std::wstring validIds = L"[101,202,303]";
	duplicate.replace(duplicate.find(validIds), validIds.size(), L"[101,202,202]");
	if (!Expect(!ParsePresentationDescriptorJson(duplicate).descriptor,
		"duplicate SlideID rejects the complete descriptor")) ++failures;
	std::wstring unknownField = StableJson(L"PowerPoint", L"C:\\\\extra.pptx");
	unknownField.insert(unknownField.size() - 1, L",\"unexpected\":true");
	if (!Expect(!ParsePresentationDescriptorJson(unknownField).descriptor,
		"descriptor schema rejects unknown fields")) ++failures;

	Bridge::StateBridge bridge;
	if (first)
	{
		const auto revision = bridge.PublishPresentationTarget(*first);
		const auto repeated = bridge.PublishPresentationTarget(*first);
		Bridge::PresentationTarget changed = *first;
		changed.pageIndex = 2;
		changed.slideId = 303;
		const auto changedRevision = bridge.PublishPresentationTarget(changed);
		if (!Expect(revision && repeated == revision && changedRevision &&
			*changedRevision > *revision,
			"atomic targets are idempotent and revisions are monotonic")) ++failures;
		const auto snapshot = bridge.Snapshot();
		if (!Expect(snapshot.presentationTarget && snapshot.hasPage &&
			snapshot.page == 2 && snapshot.presentationTarget->slideId == 303,
			"bridge never publishes a page without its presentation identity")) ++failures;
	}
	const auto otherParsed = ParsePresentationDescriptorJson(
		StableJson(L"PowerPoint", L"C:\\\\课程\\\\Other.pptx"));
	const auto other = otherParsed.descriptor
		? ResolvePresentationTarget(*otherParsed.descriptor) : std::nullopt;
	if (first && other)
	{
		Bridge::StateBridge switching;
		const auto firstRevision = switching.PublishPresentationTarget(*first);
		const auto otherRevision = switching.PublishPresentationTarget(*other);
		const auto returnRevision = switching.PublishPresentationTarget(*first);
		if (!Expect(firstRevision && otherRevision && returnRevision &&
			*firstRevision < *otherRevision && *otherRevision < *returnRevision &&
			switching.Snapshot().presentationTarget->key == first->key,
			"direct A-to-B-to-A switching keeps identity and monotonic targets")) ++failures;
	}

	return failures;
}
