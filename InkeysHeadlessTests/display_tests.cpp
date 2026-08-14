#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

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
}

int RunDisplayTests()
{
	std::array<std::uint8_t, 128> bytes{};
	constexpr std::array<std::uint8_t, 8> header{
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	std::copy(header.begin(), header.end(), bytes.begin());
	bytes[18] = 1;
	bytes[19] = 4;
	bytes[21] = 52;
	bytes[22] = 29;
	const auto parsed = Inkeys::Display::ParseEdid(bytes, L"DISPLAY\\ABC1234\\1");
	Check(parsed.valid && parsed.VersionText() == L"1.4",
		"valid EDID exposes version");
	Check(parsed.rawPhysicalWidthCm == 52 && parsed.rawPhysicalHeightCm == 29,
		"valid EDID exposes raw physical size");
	const auto rotated = Inkeys::Display::OrientEdid(parsed, DMDO_90);
	Check(rotated.physicalWidthCm == 29 && rotated.physicalHeightCm == 52,
		"portrait orientation swaps physical size");

	bytes[0] = 1;
	Check(!Inkeys::Display::ParseEdid(bytes).valid,
		"invalid EDID header remains unknown");
	bytes[0] = 0;
	bytes[21] = 0;
	Check(!Inkeys::Display::ParseEdid(bytes).valid,
		"zero EDID physical size remains unknown");
	Check(!Inkeys::Display::ParseEdid(
		std::span<const std::uint8_t>(bytes.data(), 22)).valid,
		"short EDID remains unknown");

	Inkeys::Display::Snapshot snapshot;
	Inkeys::Display::MonitorInfo first;
	first.handle = reinterpret_cast<HMONITOR>(1);
	Inkeys::Display::MonitorInfo second;
	second.handle = reinterpret_cast<HMONITOR>(2);
	snapshot.monitors = { first, second };
	snapshot.primaryIndex = 1;
	Check(snapshot.Primary() == &snapshot.monitors[1],
		"snapshot primary index is coherent");
	Check(snapshot.Find(reinterpret_cast<HMONITOR>(1)) == &snapshot.monitors[0],
		"snapshot finds monitor by handle");

	Inkeys::Display::Shutdown();
	const bool enumerated = Inkeys::Display::Initialize();
	const auto firstPublished = Inkeys::Display::GetSnapshot();
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
				monitor.pixelHeight == monitor.bounds.bottom - monitor.bounds.top,
				"monitor dimensions and bounds share one snapshot generation");
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
				!firstPublished->Primary()->edid.valid,
				"first enumeration failure publishes explicit fallback without EDID");
	}

	int callbackCount = 0;
	std::uint64_t callbackGeneration = 0;
	auto subscription = Inkeys::Display::Subscribe(
		[&](Inkeys::Display::SnapshotPtr value)
		{
			++callbackCount;
			callbackGeneration = value ? value->generation : 0;
		});
	Check(callbackCount == 1 && firstPublished &&
		callbackGeneration == firstPublished->generation,
		"subscription receives the current immutable snapshot once");
	(void)Inkeys::Display::Refresh(Inkeys::Display::ChangeReason::Manual);
	const auto duplicate = Inkeys::Display::GetSnapshot();
	Check(firstPublished && duplicate &&
		duplicate->generation == firstPublished->generation && callbackCount == 1,
		"semantically equal refresh does not advance generation or notify");
	subscription.Reset();
	Inkeys::Display::Shutdown();
	Check(!Inkeys::Display::GetSnapshot(), "shutdown releases the published snapshot");
	return failureCount;
}
