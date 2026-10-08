#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

import Inkeys.Display;

namespace
{
	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	void FinalizeEdidChecksum(std::array<std::uint8_t, 128>& bytes)
	{
		bytes[127] = 0;
		const auto sum = std::accumulate(bytes.begin(), bytes.begin() + 127, 0u);
		bytes[127] = static_cast<std::uint8_t>(0u - sum);
	}

	std::array<std::uint8_t, 128> MakeEdid(
		std::uint8_t widthCm, std::uint8_t heightCm)
	{
		std::array<std::uint8_t, 128> bytes{};
		constexpr std::array<std::uint8_t, 8> header{
			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
		std::copy(header.begin(), header.end(), bytes.begin());
		bytes[18] = 1;
		bytes[19] = 4;
		bytes[21] = widthCm;
		bytes[22] = heightCm;
		FinalizeEdidChecksum(bytes);
		return bytes;
	}

	Inkeys::Display::ActiveDisplayTargetInfo MakeTarget(
		LONG adapterHigh, DWORD adapterLow, UINT32 sourceId, UINT32 targetId)
	{
		Inkeys::Display::ActiveDisplayTargetInfo target;
		target.sourceAdapterId.HighPart = adapterHigh;
		target.sourceAdapterId.LowPart = adapterLow;
		target.sourceId = sourceId;
		target.targetAdapterId = target.sourceAdapterId;
		target.targetId = targetId;
		return target;
	}
}

int RunDisplayTests()
{
	using namespace Inkeys::Display;

	auto bytes = MakeEdid(52, 29);
	const auto parsed = ParseEdid(bytes, L"DISPLAY\\ABC1234\\1");
	Check(parsed.valid && parsed.status == EdidStatus::Parsed &&
		parsed.VersionText() == L"1.4", "valid EDID exposes parse status and version");
	Check(parsed.rawPhysicalWidthCm == 52 && parsed.rawPhysicalHeightCm == 29 &&
		parsed.rawBytes.size() == bytes.size(),
		"valid EDID preserves raw bytes and physical size");
	const auto rotated = ResolvePhysicalSize(
		parsed, DMDO_90, DisplayTopology::Single, true);
	Check(rotated.available && rotated.widthCm == 29 && rotated.heightCm == 52 &&
		parsed.rawPhysicalWidthCm == 52 && parsed.rawPhysicalHeightCm == 29,
		"portrait orientation swaps only business physical size");

	bytes[0] = 1;
	FinalizeEdidChecksum(bytes);
	const auto invalidHeader = ParseEdid(bytes);
	Check(invalidHeader.status == EdidStatus::ParseFailed,
		"invalid EDID header remains diagnostic parse failure");
	Check(invalidHeader.rawBytes.size() == bytes.size() &&
		std::equal(invalidHeader.rawBytes.begin(), invalidHeader.rawBytes.end(),
			bytes.begin()),
		"invalid EDID still preserves every byte read for diagnostics");
	bytes = MakeEdid(52, 29);
	bytes[10] ^= 1;
	Check(ParseEdid(bytes).status == EdidStatus::ParseFailed,
		"invalid EDID checksum remains diagnostic parse failure");
	Check(ParseEdid(std::span<const std::uint8_t>(bytes.data(), 22)).status ==
		EdidStatus::ParseFailed, "short EDID remains diagnostic parse failure");

	const auto missingSizeEdid = ParseEdid(MakeEdid(0, 29));
	const auto missingSize = ResolvePhysicalSize(
		missingSizeEdid, DMDO_DEFAULT, DisplayTopology::Single, true);
	Check(missingSizeEdid.valid && !missingSize.available &&
		missingSize.unavailableReason ==
		PhysicalSizeUnavailableReason::MissingDimensions,
		"zero dimensions are parsed raw data but unavailable to business");
	const auto smallSize = ResolvePhysicalSize(ParseEdid(MakeEdid(4, 29)),
		DMDO_DEFAULT, DisplayTopology::Single, true);
	Check(!smallSize.available && smallSize.widthCm == 0 && smallSize.heightCm == 0 &&
		smallSize.unavailableReason ==
		PhysicalSizeUnavailableReason::DimensionsBelowMinimum,
		"dimensions below five centimeters are rejected");
	const auto boundarySize = ResolvePhysicalSize(ParseEdid(MakeEdid(5, 5)),
		DMDO_DEFAULT, DisplayTopology::Single, true);
	Check(boundarySize.available && boundarySize.widthCm == 5 && boundarySize.heightCm == 5,
		"five centimeter dimensions are accepted");

	const std::array singleTargets{ MakeTarget(0, 1, 0, 0) };
	Check(ClassifyTopology(singleTargets) == DisplayTopology::Single,
		"one active target is a real single display topology");
	const std::array extendedTargets{
		MakeTarget(0, 1, 0, 0), MakeTarget(0, 1, 1, 1) };
	Check(ClassifyTopology(extendedTargets) == DisplayTopology::Extended,
		"unique sources are extended regardless of equal resolutions");
	const std::array clonedTargets{
		MakeTarget(0, 1, 0, 0), MakeTarget(0, 1, 0, 1) };
	Check(ClassifyTopology(clonedTargets) == DisplayTopology::CloneOrMixed,
		"one source with multiple targets is clone topology");
	const std::array mixedTargets{
		MakeTarget(0, 1, 0, 0), MakeTarget(0, 1, 0, 1),
		MakeTarget(0, 1, 1, 2) };
	Check(ClassifyTopology(mixedTargets) == DisplayTopology::CloneOrMixed,
		"partial clone and partial extension is globally clone-or-mixed");
	Check(ClassifyTopology(std::span<const ActiveDisplayTargetInfo>{}) ==
		DisplayTopology::Unknown, "no reliable active paths is unknown topology");

	EdidInfo readFailure;
	readFailure.status = EdidStatus::ReadFailed;
	const auto validExtended = ResolvePhysicalSize(
		parsed, DMDO_DEFAULT, DisplayTopology::Extended, true);
	const auto failedExtended = ResolvePhysicalSize(
		readFailure, DMDO_DEFAULT, DisplayTopology::Extended, true);
	Check(validExtended.available && !failedExtended.available &&
		failedExtended.unavailableReason ==
		PhysicalSizeUnavailableReason::EdidReadFailed,
		"pure extension evaluates EDID availability per monitor");
	const auto cloneBlocked = ResolvePhysicalSize(
		parsed, DMDO_DEFAULT, DisplayTopology::CloneOrMixed, true);
	Check(!cloneBlocked.available && cloneBlocked.unavailableReason ==
		PhysicalSizeUnavailableReason::CloneOrMixed,
		"clone topology globally blocks otherwise valid EDID");
	const auto ambiguous = ResolvePhysicalSize(
		parsed, DMDO_DEFAULT, DisplayTopology::Extended, false);
	Check(!ambiguous.available && ambiguous.unavailableReason ==
		PhysicalSizeUnavailableReason::DisplayTargetAmbiguous,
		"ambiguous target mapping blocks physical size");
	const auto fallbackSize = ResolvePhysicalSize(
		parsed, DMDO_DEFAULT, DisplayTopology::Single, true, true);
	Check(!fallbackSize.available && fallbackSize.unavailableReason ==
		PhysicalSizeUnavailableReason::SnapshotFallback,
		"fallback snapshots never expose physical size");

	Snapshot snapshot;
	MonitorInfo first;
	first.handle = reinterpret_cast<HMONITOR>(1);
	MonitorInfo second;
	second.handle = reinterpret_cast<HMONITOR>(2);
	snapshot.monitors = { first, second };
	snapshot.primaryIndex = 1;
	Check(snapshot.Primary() == &snapshot.monitors[1],
		"snapshot primary index is coherent");
	Check(snapshot.Find(reinterpret_cast<HMONITOR>(1)) == &snapshot.monitors[0],
		"snapshot finds monitor by handle");
	Snapshot equivalent = snapshot;
	equivalent.generation = 42;
	Check(snapshot.SemanticallyEquals(equivalent),
		"snapshot generation is not part of semantic equality");
	equivalent.topology = DisplayTopology::Extended;
	Check(!snapshot.SemanticallyEquals(equivalent),
		"topology changes are semantic snapshot changes");
	equivalent = snapshot;
	equivalent.monitors[0].physicalSize.unavailableReason =
		PhysicalSizeUnavailableReason::EdidReadFailed;
	Check(!snapshot.SemanticallyEquals(equivalent),
		"physical availability changes are semantic snapshot changes");
	equivalent = snapshot;
	equivalent.monitors[0].targetIndex = 0;
	Check(!snapshot.SemanticallyEquals(equivalent),
		"logical monitor target mapping changes are semantic snapshot changes");
	snapshot.activeTargets = { MakeTarget(0, 1, 0, 0) };
	snapshot.activeTargets[0].edid = parsed;
	equivalent = snapshot;
	equivalent.activeTargets[0].targetId = 1;
	Check(!snapshot.SemanticallyEquals(equivalent),
		"active target identity changes are semantic snapshot changes");
	equivalent = snapshot;
	equivalent.activeTargets[0].edid.rawBytes[20] ^= 1;
	Check(!snapshot.SemanticallyEquals(equivalent),
		"active target EDID changes are semantic snapshot changes");

	Shutdown();
	const bool enumerated = Initialize();
	const auto firstPublished = GetSnapshot();
	Check(firstPublished && !firstPublished->monitors.empty() &&
		firstPublished->Primary(), "initialize publishes a coherent snapshot");
	if (firstPublished)
	{
		LONG left = (std::numeric_limits<LONG>::max)();
		LONG top = (std::numeric_limits<LONG>::max)();
		LONG right = (std::numeric_limits<LONG>::min)();
		LONG bottom = (std::numeric_limits<LONG>::min)();
		for (const auto& monitor : firstPublished->monitors)
		{
			left = (std::min)(left, monitor.bounds.left);
			top = (std::min)(top, monitor.bounds.top);
			right = (std::max)(right, monitor.bounds.right);
			bottom = (std::max)(bottom, monitor.bounds.bottom);
			Check(monitor.pixelWidth == monitor.bounds.right - monitor.bounds.left &&
				monitor.pixelHeight == monitor.bounds.bottom - monitor.bounds.top &&
				monitor.effectiveDpiX > 0 && monitor.effectiveDpiY > 0,
				"monitor geometry and DPI remain available in one snapshot generation");
			if (monitor.physicalSize.available)
				Check(monitor.targetIndex.has_value() && monitor.edid.valid &&
					monitor.physicalSize.widthCm >= 5 &&
					monitor.physicalSize.heightCm >= 5,
					"available physical size has exact target and valid EDID");
			if (firstPublished->topology == DisplayTopology::CloneOrMixed)
				Check(!monitor.physicalSize.available &&
					monitor.physicalSize.unavailableReason ==
					PhysicalSizeUnavailableReason::CloneOrMixed,
					"live clone topology disables every monitor physical size");
		}
		Check(firstPublished->virtualBounds.left == left &&
			firstPublished->virtualBounds.top == top &&
			firstPublished->virtualBounds.right == right &&
			firstPublished->virtualBounds.bottom == bottom,
			"virtual desktop covers positive and negative monitor coordinates");
		Check(firstPublished->Primary()->primary,
			"published primary index points to the primary record");
		if (!enumerated)
			Check(firstPublished->fallback && firstPublished->Primary()->fallback &&
				!firstPublished->Primary()->physicalSize.available &&
				firstPublished->Primary()->physicalSize.unavailableReason ==
				PhysicalSizeUnavailableReason::SnapshotFallback,
				"first enumeration failure publishes fallback without physical size");
	}

	int callbackCount = 0;
	std::uint64_t callbackGeneration = 0;
	auto subscription = Subscribe(
		[&](SnapshotPtr value)
		{
			++callbackCount;
			callbackGeneration = value ? value->generation : 0;
		});
	Check(callbackCount == 1 && firstPublished &&
		callbackGeneration == firstPublished->generation,
		"subscription receives the current immutable snapshot once");
	(void)Refresh(ChangeReason::Manual);
	const auto duplicate = GetSnapshot();
	Check(firstPublished && duplicate &&
		duplicate->generation == firstPublished->generation && callbackCount == 1,
		"semantically equal refresh does not advance generation or notify");
	subscription.Reset();

	std::vector<int> nestedOrder;
	Subscription nestedSubscription;
	auto outerSubscription = Subscribe(
		[&](SnapshotPtr)
		{
			nestedOrder.push_back(1);
			nestedSubscription = Subscribe(
				[&](SnapshotPtr) { nestedOrder.push_back(2); });
			nestedOrder.push_back(3);
		});
	Check(nestedOrder == std::vector<int>{ 1, 3, 2 },
		"nested subscription callbacks stay serialized in publication order");
	outerSubscription.Reset();
	nestedSubscription.Reset();

	std::promise<void> firstEnteredPromise;
	auto firstEntered = firstEnteredPromise.get_future();
	std::promise<void> releaseFirstPromise;
	const auto releaseFirst = releaseFirstPromise.get_future().share();
	std::thread publicationThread([&]
		{
			auto blocking = Subscribe(
				[&](SnapshotPtr)
				{
					firstEnteredPromise.set_value();
					releaseFirst.wait();
				});
			blocking.Reset();
		});
	firstEntered.wait();

	std::promise<void> secondEnteredPromise;
	auto secondEntered = secondEnteredPromise.get_future();
	std::promise<void> releaseSecondPromise;
	const auto releaseSecond = releaseSecondPromise.get_future().share();
	auto blockingSubscription = Subscribe(
		[&](SnapshotPtr)
		{
			secondEnteredPromise.set_value();
			releaseSecond.wait();
		});
	releaseFirstPromise.set_value();
	secondEntered.wait();

	std::promise<void> shutdownFinishedPromise;
	auto shutdownFinished = shutdownFinishedPromise.get_future();
	std::thread shutdownThread([&]
		{
			Shutdown();
			shutdownFinishedPromise.set_value();
		});
	const auto shutdownDeadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(1);
	while (GetSnapshot() && std::chrono::steady_clock::now() < shutdownDeadline)
		std::this_thread::yield();
	Check(!GetSnapshot(),
		"shutdown blocks new publications before draining callbacks");

	std::promise<void> resetFinishedPromise;
	auto resetFinished = resetFinishedPromise.get_future();
	std::promise<void> resetStartedPromise;
	auto resetStarted = resetStartedPromise.get_future();
	std::thread resetThread([&]
		{
			resetStartedPromise.set_value();
			blockingSubscription.Reset();
			resetFinishedPromise.set_value();
		});
	resetStarted.wait();
	Check(shutdownFinished.wait_for(std::chrono::milliseconds(20)) ==
		std::future_status::timeout,
		"shutdown waits for an executing subscription callback");
	Check(resetFinished.wait_for(std::chrono::milliseconds(20)) ==
		std::future_status::timeout,
		"subscription reset still waits after shutdown removes the subscriber");
	releaseSecondPromise.set_value();
	resetThread.join();
	shutdownThread.join();
	publicationThread.join();
	Check(!GetSnapshot(), "shutdown releases the published snapshot");
	return failureCount;
}
