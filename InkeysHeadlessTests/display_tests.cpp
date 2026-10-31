#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <cmath>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
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
	void RunRefreshReentrancyTests()
	{
		using namespace Inkeys::Display;
		Shutdown();
		Snapshot hardware;
		hardware.topology = DisplayTopology::Single;
		hardware.virtualBounds = { 0, 0, 1920, 1080 };
		auto& monitor = hardware.monitors.emplace_back();
		monitor.handle = reinterpret_cast<HMONITOR>(1);
		monitor.primary = true;
		monitor.bounds = hardware.virtualBounds;
		monitor.workArea = { 0, 0, 1920, 1032 };
		monitor.pixelWidth = 1920;
		monitor.pixelHeight = 1080;
		monitor.physicalSize = { true, 50, 30, PhysicalSizeUnavailableReason::None };
		int enumerations = 0;
		int samples = 0;
		bool nestedDeferred = true;
		Testing::RefreshHooks hooks;
		hooks.enumerate = [&]() -> std::optional<Snapshot> { ++enumerations; return hardware; };
		const auto queue = [&](ChangeReason reason) { nestedDeferred = !Refresh(reason) && nestedDeferred; };
		const auto sampleOnly = [&](Snapshot&, bool) { ++samples; };
		Testing::SetRefreshHooks(&hooks);

		// 在生产 Refresh 持锁采样处直接重入，模拟 SHAppBarMessage 同步派发消息。
		bool startupUnpublished = true;
		hooks.sampleTaskbars = [&](Snapshot&, bool)
		{
			++samples;
			startupUnpublished = !GetSnapshot() && startupUnpublished;
			if (samples == 1)
			{
				monitor.bounds.right = monitor.pixelWidth = hardware.virtualBounds.right = 2020;
				monitor.workArea.right = 2020;
			}
			for (int repeat = 0; repeat < 8; ++repeat) queue(ChangeReason::Settings);
		};
		Check(Initialize() && GetSnapshot() && GetSnapshot()->Primary()->pixelWidth == 2020
			&& enumerations == 2 && samples == 2 && startupUnpublished && nestedDeferred,
			"startup coalesces Shell message reentry under refresh lock and publishes final snapshot synchronously");
		Check(Testing::HasDeferredRefresh(), "startup retains overflow before observer creation");
		hooks.sampleTaskbars = sampleOnly;
		enumerations = samples = 0;
		Check(Testing::DrainDeferredRefresh() && enumerations == 1 && samples == 1
			&& !Testing::HasDeferredRefresh(), "deferred startup work is consumed without an HWND");

		int callbacks = 0;
		auto subscription = Subscribe([&](SnapshotPtr) { ++callbacks; });
		const auto unchangedGeneration = GetSnapshot()->generation;
		hooks.sampleTaskbars = [&](Snapshot&, bool)
		{
			++samples;
			queue(ChangeReason::Settings);
		};
		enumerations = samples = 0;
		Check(Refresh(ChangeReason::Manual) && enumerations == 2 && samples == 2
			&& GetSnapshot()->generation == unchangedGeneration && callbacks == 1
			&& Testing::HasDeferredRefresh(),
			"semantic no-change still consumes one reentrant follow-up without duplicate generation or callbacks");
		enumerations = samples = 0;
		Check(Testing::DrainDeferredRefresh() && enumerations == 2 && samples == 2
			&& Testing::HasDeferredRefresh(),
			"persistent Shell notifications leave bounded deferred work instead of retrying forever");
		hooks.sampleTaskbars = sampleOnly;
		(void)Testing::DrainDeferredRefresh();

		const int oldWidth = monitor.pixelWidth;
		hooks.sampleTaskbars = [&](Snapshot&, bool)
		{
			++samples;
			queue(ChangeReason::Settings);
			if (samples == 2)
			{
				// 末次完整枚举之后才来的拓扑变化，必须由延后请求重新枚举。
				monitor.bounds.right = monitor.pixelWidth = hardware.virtualBounds.right = oldWidth + 80;
				monitor.workArea.right = monitor.pixelWidth;
				queue(ChangeReason::Display);
			}
		};
		enumerations = samples = 0;
		Check(Refresh(ChangeReason::Settings) && enumerations == 2 && samples == 2
			&& GetSnapshot()->Primary()->pixelWidth == oldWidth && Testing::HasDeferredRefresh(),
			"late topology change during final full sample remains pending");
		hooks.sampleTaskbars = sampleOnly;
		enumerations = samples = 0;
		Check(Testing::DrainDeferredRefresh() && enumerations == 1 && samples == 1
			&& GetSnapshot()->Primary()->pixelWidth == oldWidth + 80 && !Testing::HasDeferredRefresh(),
			"deferred drain really enumerates and publishes topology missed by final Shell sample");

		hooks.sampleTaskbars = [&](Snapshot&, bool visibilityOnly)
		{
			++samples;
			if (samples == 1)
			{
				Check(visibilityOnly, "visibility refresh starts on cached path");
				++monitor.workArea.top;
				queue(ChangeReason::Settings);
				queue(ChangeReason::Display);
			}
			else Check(!visibilityOnly, "nested Settings/Display upgrades cached follow-up to full sampling");
		};
		enumerations = samples = 0;
		Check(Refresh(ChangeReason::TaskbarVisibility) && enumerations == 1 && samples == 2
			&& GetSnapshot()->Primary()->workArea.top == monitor.workArea.top && !Testing::HasDeferredRefresh(),
			"nested full requests are merged while taskbar visibility sampling is active");

		hooks.sampleTaskbars = [&](Snapshot&, bool visibilityOnly)
		{
			++samples;
			Check(visibilityOnly == (samples == 1), "taskbar structure upgrades cached sampler without topology enumeration");
			if (samples == 1) queue(ChangeReason::Taskbar);
		};
		enumerations = samples = 0;
		Check(Refresh(ChangeReason::TaskbarVisibility) && enumerations == 0 && samples == 2
			&& !Testing::HasDeferredRefresh(), "nested taskbar structure request retains its intermediate scope");
		hooks.sampleTaskbars = [&](Snapshot&, bool visibilityOnly)
		{
			++samples;
			Check(visibilityOnly, "bounded cached follow-up remains visibility-only until upgrade arrives");
			queue(samples == 1 ? ChangeReason::TaskbarVisibility : ChangeReason::Display);
		};
		enumerations = samples = 0;
		Check(Refresh(ChangeReason::TaskbarVisibility) && enumerations == 0 && samples == 2
			&& Testing::HasDeferredRefresh(), "stronger request during bounded cached follow-up is retained");
		hooks.sampleTaskbars = sampleOnly;
		enumerations = samples = 0;
		Check(Testing::DrainDeferredRefresh() && enumerations == 1 && samples == 1,
			"deferred stronger request uses topology enumeration rather than cached visibility");

		hooks.enumerate = [&]() -> std::optional<Snapshot> { ++enumerations; return std::nullopt; };
		hooks.sampleTaskbars = [&](Snapshot&, bool) { ++samples; queue(ChangeReason::Display); };
		enumerations = samples = 0;
		Check(!Refresh(ChangeReason::TaskbarVisibility) && enumerations == 1 && samples == 1
			&& GetSnapshot()->Primary()->pixelWidth == monitor.pixelWidth
			&& !GetSnapshot()->Primary()->physicalSize.available
			&& GetSnapshot()->topology == DisplayTopology::Unknown,
			"failed upgraded enumeration reports failure and invalidates physical size without losing geometry");
		hooks.enumerate = [&]() -> std::optional<Snapshot> { ++enumerations; return hardware; };
		hooks.sampleTaskbars = [&](Snapshot&, bool)
		{
			queue(ChangeReason::Display);
			throw std::runtime_error("injected sampler failure");
		};
		const auto beforeException = GetSnapshot();
		bool caught = false;
		try { (void)Refresh(ChangeReason::Manual); }
		catch (const std::runtime_error&) { caught = true; }
		hooks.sampleTaskbars = sampleOnly;
		Check(caught && GetSnapshot() == beforeException && Refresh(ChangeReason::Manual)
			&& !Testing::HasDeferredRefresh(), "sampler exception releases refresh lock and reentry scope without partial publication");

		bool refreshFromCallback = false;
		bool callbackRefreshSucceeded = false;
		auto callbackSubscription = Subscribe([&](SnapshotPtr)
		{
			if (!std::exchange(refreshFromCallback, false)) return;
			callbackRefreshSucceeded = Refresh(ChangeReason::Manual);
		});
		refreshFromCallback = true;
		++monitor.workArea.top;
		enumerations = samples = 0;
		Check(Refresh(ChangeReason::Manual) && callbackRefreshSucceeded && enumerations == 2 && samples == 2,
			"subscriber callback can synchronously refresh after internal reentry scope has ended");
		callbackSubscription.Reset();
		subscription.Reset();
		hooks.sampleTaskbars = [&](Snapshot&, bool) { queue(ChangeReason::Settings); };
		(void)Refresh(ChangeReason::Manual);
		Check(Testing::HasDeferredRefresh(), "shutdown fixture has deferred work");
		Shutdown();
		Check(!Testing::HasDeferredRefresh() && !Refresh(ChangeReason::Manual) && !GetSnapshot(),
			"shutdown clears pending reentrant work and blocks later publication");
		hooks.sampleTaskbars = sampleOnly;
		enumerations = samples = 0;
		Check(Initialize() && enumerations == 1 && samples == 1 && !Testing::HasDeferredRefresh(),
			"reinitialize does not replay requests from the stopped observer lifetime");
		Testing::SetRefreshHooks(nullptr);
		Shutdown();
	}
}

int RunDisplayTests()
{
	using namespace Inkeys::Display;

	const RECT taskbarMonitor{ -1920, -200, 0, 880 };
	Check(ResolveTaskbarThickness(taskbarMonitor, { -1920, 832, 0, 880 }, ABE_BOTTOM) == 48
		&& ResolveTaskbarThickness(taskbarMonitor, { -1920, -200, 0, -152 }, ABE_TOP) == 48
		&& ResolveTaskbarThickness(taskbarMonitor, { -1920, -200, -1872, 880 }, ABE_LEFT) == 48
		&& ResolveTaskbarThickness(taskbarMonitor, { -48, -200, 0, 880 }, ABE_RIGHT) == 48,
		"taskbar thickness resolves all four edges with negative monitor coordinates");
	Check(ResolveTaskbarThickness(taskbarMonitor, { -1920, 878, 0, 926 }, ABE_BOTTOM) == 48
		&& ResolveTaskbarThickness(taskbarMonitor, { -1920, 878, 0, 880 }, ABE_BOTTOM) == 0
		&& ResolveTaskbarThickness(taskbarMonitor, { -1920, 0, 0, 700 }, ABE_BOTTOM) == 0
		&& ResolveTaskbarThickness(taskbarMonitor, { 0, 0, 0, 0 }, ABE_LEFT) == 0,
		"auto-hide uses full offscreen thickness and rejects sliver/invalid geometry");
	for (const UINT dpi : { 96U, 144U, 192U })
		for (const int thickness : { 24, 48, 72, 96 })
			for (const double user : { 0.5, 1.0, 1.5, 2.0 })
			{
				const double base = ResolveUiScaleBase(dpi, thickness, 96, 48, UiScaleBaseMode::TaskbarActual);
				Check(std::abs(80 * base * user - thickness * user) < 1e-9,
					"UI actual taskbar base is physical thickness independent of DPI and user multiplier");
				Check(ResolveUiScaleBase(dpi, thickness, 96, 48, UiScaleBaseMode::Dpi) == dpi / 96.0,
					"disabled taskbar base restores real DPI");
				const double basic = ResolveUiScaleBase(dpi, thickness, 96, 48, UiScaleBaseMode::TaskbarBasic);
				Check(std::abs(80 * basic * user - 48 * dpi / 96.0 * user) < 1e-9,
					"UI basic taskbar base keeps 48 DIP at 100 percent independent of actual thickness");
			}
	for (const UINT dpi : { 0U, 96U, 144U, 192U })
		for (const int thickness : { -1, 0, 24, 96 })
		{
			const double dpiScale = (dpi ? dpi : 96) / 96.0;
			Check(std::abs(ResolveUiScaleBase(dpi, thickness, 0, 0, UiScaleBaseMode::TaskbarBasic)
				- 0.6 * dpiScale) < 1e-9,
				"basic taskbar mode retains 48 DIP without Shell samples and defaults missing DPI to 96");
		}
	Check(std::abs(ResolveUiScaleBase(192, 0, 144, 72, UiScaleBaseMode::TaskbarActual) - 1.2) < 1e-9
		&& ResolveUiScaleBase(144, 0, 0, 0, UiScaleBaseMode::TaskbarActual) == 1.5
		&& ResolveUiScaleBase(0, -1, 0, -1, UiScaleBaseMode::TaskbarActual) == 1.0,
		"actual taskbar mode retains primary DPI and unavailable Shell fallbacks");
	Snapshot hiddenSnapshot;
	hiddenSnapshot.monitors.resize(2);
	hiddenSnapshot.monitors[0].handle = reinterpret_cast<HMONITOR>(1);
	hiddenSnapshot.monitors[1].handle = reinterpret_cast<HMONITOR>(2);
	hiddenSnapshot.monitors[0].bounds = { 0, 0, 1920, 1080 };
	hiddenSnapshot.monitors[1].bounds = { 0, 1080, 1920, 2160 };
	Check(ResolveTaskbarOwner(hiddenSnapshot, { 0, 1078, 1920, 1126 }, ABE_BOTTOM) == hiddenSnapshot.monitors[0].handle
		&& ApplyTaskbarSample(hiddenSnapshot, hiddenSnapshot.monitors[0].handle,
		{ 0, 1078, 1920, 1126 }, ABE_BOTTOM)
		&& hiddenSnapshot.monitors[0].taskbarThicknessPx == 48
		&& hiddenSnapshot.monitors[1].taskbarThicknessPx == 0,
		"autohide owner keeps upper-monitor taskbar when full rectangle lies mostly in lower monitor");
	hiddenSnapshot.monitors[0].bounds = { -1920, 0, 0, 1080 };
	hiddenSnapshot.monitors[1].bounds = { 0, 0, 1920, 1080 };
	hiddenSnapshot.monitors[0].taskbarThicknessPx = 0;
	Check(ResolveTaskbarOwner(hiddenSnapshot, { -2, 0, 46, 1080 }, ABE_RIGHT) == hiddenSnapshot.monitors[0].handle
		&& ApplyTaskbarSample(hiddenSnapshot, hiddenSnapshot.monitors[0].handle,
		{ -2, 0, 46, 1080 }, ABE_RIGHT)
		&& hiddenSnapshot.monitors[0].taskbarThicknessPx == 48
		&& hiddenSnapshot.monitors[1].taskbarThicknessPx == 0,
		"autohide owner keeps left-monitor vertical taskbar when rectangle moves into right monitor");
	// 共享屏边：常驻 Top 属于下屏，不能因先枚举上屏而被误认成 Bottom。
	Snapshot sharedBoundary;
	sharedBoundary.monitors.resize(2);
	sharedBoundary.monitors[0].handle = reinterpret_cast<HMONITOR>(1);
	sharedBoundary.monitors[0].bounds = { 0, 0, 1920, 1080 };
	sharedBoundary.monitors[1].handle = reinterpret_cast<HMONITOR>(2);
	sharedBoundary.monitors[1].bounds = { 0, 1080, 1920, 2160 };
	const RECT boundaryTaskbar{ 0, 1080, 1920, 1128 };
	for (int order = 0; order < 2; ++order)
	{
		const auto regular = ResolveTaskbarOwnerCandidate(sharedBoundary, boundaryTaskbar, std::nullopt, false);
		const auto staleHint = ResolveTaskbarOwnerCandidate(sharedBoundary, boundaryTaskbar, std::nullopt, false,
			{ reinterpret_cast<HMONITOR>(1), ABE_BOTTOM });
		Check(regular.monitor == reinterpret_cast<HMONITOR>(2) && regular.edge == ABE_TOP
			&& staleHint.monitor == regular.monitor && staleHint.edge == regular.edge,
			"shared-boundary normal taskbar belongs to lower Top regardless of order or weaker history");
		Check(ResolveTaskbarOwner(sharedBoundary, boundaryTaskbar, ABE_TOP) == reinterpret_cast<HMONITOR>(2)
			&& ResolveTaskbarOwner(sharedBoundary, boundaryTaskbar, ABE_BOTTOM) == reinterpret_cast<HMONITOR>(1),
			"known edge distinguishes raised lower Top from fully hidden upper Bottom");
		Check(!ResolveTaskbarOwnerCandidate(sharedBoundary, boundaryTaskbar, std::nullopt, true).monitor,
			"unknown auto-hide edge at shared boundary stays unknown without trusted history");
		std::reverse(sharedBoundary.monitors.begin(), sharedBoundary.monitors.end());
	}

	// Mini 启动查询仅含一屏，不能依赖邻屏的零距离候选排除屏外常驻栏。
	Snapshot singleScreen;
	singleScreen.monitors.resize(1);
	singleScreen.monitors[0].handle = reinterpret_cast<HMONITOR>(1);
	singleScreen.monitors[0].bounds = { -960, -540, 960, 540 };
	struct RegularOwnerCase { RECT outside; RECT inside; UINT edge; };
	const RegularOwnerCase regularOwnerCases[]{
		{ { -1008, -540, -960, 540 }, { -960, -540, -912, 540 }, ABE_LEFT },
		{ { -960, -588, 960, -540 }, { -960, -540, 960, -492 }, ABE_TOP },
		{ { 960, -540, 1008, 540 }, { 912, -540, 960, 540 }, ABE_RIGHT },
		{ { -960, 540, 960, 588 }, { -960, 492, 960, 540 }, ABE_BOTTOM },
	};
	for (const auto& sample : regularOwnerCases)
	{
		Check(!ResolveTaskbarOwnerCandidate(singleScreen, sample.outside, std::nullopt, false).monitor
			&& !ResolveTaskbarOwnerCandidate(singleScreen, sample.outside, sample.edge, false).monitor,
			"single-screen sample rejects adjacent outside regular taskbar on every edge");
		const auto inside = ResolveTaskbarOwnerCandidate(singleScreen, sample.inside, std::nullopt, false);
		Check(inside.monitor == singleScreen.monitors[0].handle && inside.edge == sample.edge,
			"single-screen sample keeps its own aligned regular taskbar on every edge");
		const auto hidden = ResolveTaskbarOwnerCandidate(singleScreen, sample.outside, sample.edge, true);
		Check(hidden.monitor == singleScreen.monitors[0].handle && hidden.edge == sample.edge,
			"single-screen known-edge auto-hide taskbar may still lie outside screen");
	}

	struct HiddenOwnerCase { RECT neighbor; RECT taskbar; UINT edge; };
	const RECT centeredScreen{ -960, -540, 960, 540 };
	const HiddenOwnerCase hiddenOwnerCases[]{
		{ { -2880, -540, -960, 540 }, { -1006, -540, -958, 540 }, ABE_LEFT },
		{ { -960, -1620, 960, -540 }, { -960, -586, 960, -538 }, ABE_TOP },
		{ { 960, -540, 2880, 540 }, { 958, -540, 1006, 540 }, ABE_RIGHT },
		{ { -960, 540, 960, 1620 }, { -960, 538, 960, 586 }, ABE_BOTTOM },
	};
	for (const auto& sample : hiddenOwnerCases)
	{
		sharedBoundary.monitors[0].handle = reinterpret_cast<HMONITOR>(1);
		sharedBoundary.monitors[0].bounds = centeredScreen;
		sharedBoundary.monitors[1].handle = reinterpret_cast<HMONITOR>(2);
		sharedBoundary.monitors[1].bounds = sample.neighbor;
		for (int order = 0; order < 2; ++order)
		{
			const auto known = ResolveTaskbarOwnerCandidate(sharedBoundary, sample.taskbar, sample.edge, true);
			const auto cached = ResolveTaskbarOwnerCandidate(sharedBoundary, sample.taskbar, std::nullopt, true,
				{ reinterpret_cast<HMONITOR>(1), sample.edge });
			Check(known.monitor == reinterpret_cast<HMONITOR>(1) && known.edge == sample.edge
				&& ResolveTaskbarOwner(sharedBoundary, sample.taskbar, sample.edge) == known.monitor
				&& cached.monitor == known.monitor && cached.edge == known.edge,
				"four-edge cross-monitor hidden taskbar keeps known or trusted cached owner with negative origins");
			Check(!ResolveTaskbarOwnerCandidate(sharedBoundary, sample.taskbar, std::nullopt, true).monitor
				&& !ResolveTaskbarOwnerCandidate(sharedBoundary, sample.taskbar, std::nullopt, true,
					{ reinterpret_cast<HMONITOR>(99), sample.edge }).monitor,
				"ambiguous hidden taskbar does not choose arbitrary monitor or accept absent cached owner");
			std::reverse(sharedBoundary.monitors.begin(), sharedBoundary.monitors.end());
		}
	}
	sharedBoundary.monitors[0].bounds = sharedBoundary.monitors[1].bounds = centeredScreen;
	const RECT tiedTaskbar{ -960, 492, 960, 540 };
	for (int order = 0; order < 2; ++order)
	{
		Check(!ResolveTaskbarOwner(sharedBoundary, tiedTaskbar, ABE_BOTTOM)
			&& !ResolveTaskbarOwnerCandidate(sharedBoundary, tiedTaskbar, ABE_BOTTOM, false).monitor,
			"known-edge identical monitor geometry remains unknown rather than depend on monitor order");
		const auto cached = ResolveTaskbarOwnerCandidate(sharedBoundary, tiedTaskbar, ABE_BOTTOM, true,
			{ reinterpret_cast<HMONITOR>(2), ABE_BOTTOM });
		Check(cached.monitor == reinterpret_cast<HMONITOR>(2) && cached.edge == ABE_BOTTOM,
			"matching trusted owner resolves genuine known-edge geometry ties regardless of monitor order");
		std::reverse(sharedBoundary.monitors.begin(), sharedBoundary.monitors.end());
	}
	Check(!ResolveTaskbarOwnerCandidate(sharedBoundary, tiedTaskbar, 99U, true).monitor
		&& !ResolveTaskbarOwnerCandidate(sharedBoundary, {}, std::nullopt, false).monitor,
		"invalid known edge or taskbar geometry produces no owner");
	Snapshot scaleSnapshot;
	scaleSnapshot.monitors.resize(2);
	scaleSnapshot.monitors[0].effectiveDpiX = 144;
	scaleSnapshot.monitors[0].taskbarThicknessPx = 72;
	scaleSnapshot.monitors[1].effectiveDpiX = 192;
	Check(std::abs(ResolveUiScaleBase(scaleSnapshot, scaleSnapshot.monitors[1], UiScaleBaseMode::TaskbarActual) - 1.2) < 1e-9,
		"snapshot scale uses target and primary from the same publication");
	scaleSnapshot.monitors[0].taskbarThicknessPx = 96;
	Check(std::abs(ResolveUiScaleBase(scaleSnapshot, scaleSnapshot.monitors[1], UiScaleBaseMode::TaskbarActual) - 1.6) < 1e-9
		&& std::abs(ResolveUiScaleBase(scaleSnapshot, scaleSnapshot.monitors[1], UiScaleBaseMode::TaskbarBasic) - 1.2) < 1e-9
		&& ResolveUiScaleBase(scaleSnapshot, scaleSnapshot.monitors[1], UiScaleBaseMode::Dpi) == 2.0,
		"snapshot basic mode ignores primary fallback thickness while actual mode follows it");
	scaleSnapshot.monitors[0].taskbarThicknessPx = 72;
	auto scaleChanged = scaleSnapshot;
	Check(scaleSnapshot.SemanticallyEquals(scaleChanged), "same taskbar thickness does not publish another generation");
	scaleChanged.monitors[0].taskbarThicknessPx = 96;
	Check(!scaleSnapshot.SemanticallyEquals(scaleChanged), "taskbar-only thickness change invalidates display snapshot");

	MonitorInfo dockMonitor;
	dockMonitor.bounds = taskbarMonitor;
	dockMonitor.workArea = { -1900, -180, 0, 832 };
	dockMonitor.taskbarThicknessPx = 48;
	dockMonitor.taskbar = { TaskbarEdge::Bottom, true, TaskbarExposure::Hidden, TaskbarExposure::Hidden, 832 };
	struct DockGapCase { UINT dpi; int gapPx; };
	constexpr std::array<DockGapCase, 4> dockGapCases{ { { 96, 5 }, { 120, 6 }, { 144, 8 }, { 192, 10 } } };
	for (const auto& gap : dockGapCases)
	{
		dockMonitor.effectiveDpiY = gap.dpi;
		Check(ResolveDesktopBottomDockLine(dockMonitor) == 880 - gap.gapPx
			&& ShouldProtectTaskbarBottomEdge(dockMonitor),
			"hidden taskbar dock gap uses real screen DIP and protects exactly one physical bottom row");
		auto raisedMonitor = dockMonitor;
		raisedMonitor.taskbar.exposure = TaskbarExposure::Raised;
		Check(ResolveDesktopBottomDockLine(raisedMonitor) == 832,
			"raised bottom taskbar remains at stable top for every screen DPI");
		raisedMonitor.taskbar.autoHide = false;
		Check(ResolveDesktopBottomDockLine(raisedMonitor) == 832,
			"ordinary bottom taskbar remains at stable top for every screen DPI");
	}
	dockMonitor.taskbar.exposure = TaskbarExposure::Raised;
	Check(ResolveDesktopBottomDockLine(dockMonitor) == 832,
		"raised autohide uses stable full-thickness top rather than animated rect");
	dockMonitor.taskbar.exposure = TaskbarExposure::Unknown;
	dockMonitor.taskbar.lastKnownExposure = TaskbarExposure::Raised;
	Check(ResolveDesktopBottomDockLine(dockMonitor) == 832,
		"unknown exposure preserves last reliable raised target");
	dockMonitor.taskbar.lastKnownExposure = TaskbarExposure::Hidden;
	Check(ResolveDesktopBottomDockLine(dockMonitor) == 870,
		"unknown exposure preserves last reliable hidden target");
	dockMonitor.taskbar.lastKnownExposure = TaskbarExposure::Unknown;
	Check(ResolveDesktopBottomDockLine(dockMonitor) == 832,
		"unknown exposure without history uses validated work area");
	for (const RECT bounds : { taskbarMonitor, RECT{ -1920, -1400, 0, -320 } })
		for (const auto edge : { TaskbarEdge::Left, TaskbarEdge::Top, TaskbarEdge::Right })
			for (const bool autoHide : { false, true })
				for (const auto& gap : dockGapCases)
				{
					auto sideMonitor = dockMonitor;
					sideMonitor.bounds = bounds;
					sideMonitor.workArea = { bounds.left + 20, bounds.top + 20, bounds.right, bounds.bottom - 48 };
					sideMonitor.effectiveDpiY = gap.dpi;
					sideMonitor.taskbar.edge = edge;
					sideMonitor.taskbar.autoHide = autoHide;
					sideMonitor.taskbar.exposure = autoHide ? TaskbarExposure::Hidden : TaskbarExposure::Raised;
					Check(ResolveDesktopBottomDockLine(sideMonitor) == bounds.bottom - gap.gapPx
						&& !ShouldProtectTaskbarBottomEdge(sideMonitor),
						"top and side taskbars retain five real screen DIP regardless of autohide and other work-area inset");
				}
	dockMonitor.taskbar = { TaskbarEdge::Bottom, false, TaskbarExposure::Raised, TaskbarExposure::Raised, 832 };
	Check(ResolveDesktopBottomDockLine(dockMonitor) == 832 && !ShouldProtectTaskbarBottomEdge(dockMonitor),
		"ordinary bottom taskbar retains stable top without autohide protection");
	dockMonitor.taskbar = {};
	dockMonitor.workArea = { -1920, -200, 0, 880 };
	Check(ResolveDesktopBottomDockLine(dockMonitor) == 880,
		"absent taskbar does not borrow another monitor taskbar or scale fallback");
	dockMonitor.workArea = { 0, 0, 0, 832 };
	Check(ResolveDesktopBottomDockLine(dockMonitor) == 880,
		"invalid work area cannot create a desktop dock target");

	auto exposure = TaskbarExposure::Hidden;
	for (const int visibleHeight : { 0, 1, 2, 3, 8, 20, 23 })
	{
		exposure = ResolveTaskbarExposure(taskbarMonitor,
			{ -1920, 880 - visibleHeight, 0, 928 - visibleHeight }, 48, 96, exposure);
		Check(exposure == TaskbarExposure::Hidden, "trigger strip and early raise do not churn dock target");
	}
	exposure = ResolveTaskbarExposure(taskbarMonitor, { -1920, 856, 0, 904 }, 48, 96, exposure);
	Check(exposure == TaskbarExposure::Raised, "half-exposed taskbar selects stable raised endpoint");
	for (const int visibleHeight : { 48, 32, 24, 20, 8, 4 })
	{
		exposure = ResolveTaskbarExposure(taskbarMonitor,
			{ -1920, 880 - visibleHeight, 0, 928 - visibleHeight }, 48, 96, exposure);
		Check(exposure == TaskbarExposure::Raised, "partial hide retains endpoint until trigger strip");
	}
	Check(ResolveTaskbarExposure(taskbarMonitor, { -1920, 878, 0, 926 }, 48, 96, exposure) == TaskbarExposure::Hidden
		&& ResolveTaskbarExposure(taskbarMonitor, { -1920, 832, 0, 880 }, 48, 96, exposure, false) == TaskbarExposure::Hidden,
		"small strip or genuinely hidden style returns hidden endpoint");
	Check(ResolveTaskbarExposure(taskbarMonitor, { -1920, 870, 0, 918 }, 48, 96) == TaskbarExposure::Unknown
		&& ResolveTaskbarExposure(taskbarMonitor, {}, 48, 96, exposure) == TaskbarExposure::Unknown
		&& ResolveTaskbarExposure(taskbarMonitor, { 1, 832, 1921, 880 }, 48, 96) == TaskbarExposure::Unknown,
		"unresolved middle animation and invalid or unrelated geometry stay unknown");
	Check(ResolveTaskbarExposure(taskbarMonitor, { -1920, 874, 0, 922 }, 48, 192) == TaskbarExposure::Hidden,
		"high-DPI trigger strip is still hidden");
	Check(ResolveTaskbarFullThickness(2, 48, true, false) == 48
		&& ResolveTaskbarFullThickness(24, 48, true, false) == 48
		&& ResolveTaskbarFullThickness(24, 48, true, true) == 24
		&& ResolveTaskbarFullThickness(72, 48, true, true) == 72
		&& ResolveTaskbarFullThickness(24, 48, false, false) == 24,
		"transient thin rectangles retain scale while trustworthy complete taskbar resize can shrink or grow");

	Snapshot taskbarStateSnapshot;
	taskbarStateSnapshot.monitors.push_back(dockMonitor);
	auto stateChanged = taskbarStateSnapshot;
	stateChanged.monitors[0].taskbar = { TaskbarEdge::Bottom, true, TaskbarExposure::Hidden, TaskbarExposure::Hidden, 832 };
	Check(!taskbarStateSnapshot.SemanticallyEquals(stateChanged), "normalized taskbar policy changes publication");
	taskbarStateSnapshot = stateChanged;
	for (int sample = 0; sample < 60; ++sample)
	{
		stateChanged.monitors[0].taskbar.exposure = ResolveTaskbarExposure(taskbarMonitor,
			{ -1920, 878, 0, 926 }, 48, 96, TaskbarExposure::Hidden);
		Check(taskbarStateSnapshot.SemanticallyEquals(stateChanged), "repeated shell samples retain semantic generation");
	}
	stateChanged.monitors[0].taskbar.exposure = TaskbarExposure::Raised;
	Check(!taskbarStateSnapshot.SemanticallyEquals(stateChanged), "exposure-only change invalidates publication");
	stateChanged = taskbarStateSnapshot;
	stateChanged.monitors[0].taskbar.dockTopPx -= 1;
	Check(!taskbarStateSnapshot.SemanticallyEquals(stateChanged), "stable dock anchor participates in publication");

	TaskbarEventBatch eventBatch;
	eventBatch.Reset(1);
	Check(eventBatch.Queue(1, false) && !eventBatch.Queue(1, false)
		&& !eventBatch.Queue(1, true), "event burst queues only one owner message while retaining structural work");
	Check(eventBatch.BeginCoalescing(1) && eventBatch.Queue(1, false)
		&& !eventBatch.BeginCoalescing(1), "later events cannot restart existing coalesce timer");
	const auto structuralBatch = eventBatch.Consume(1);
	Check(structuralBatch && *structuralBatch && !eventBatch.Consume(1),
		"one timer consumes one merged structural request and ignores duplicate timer delivery");
	Check(eventBatch.Queue(1, false), "next event can start a new batch");
	eventBatch.QueueFailed(1);
	Check(eventBatch.Queue(1, false) && eventBatch.BeginCoalescing(1),
		"failed PostMessage can be retried without losing pending work");
	const auto visibilityBatch = eventBatch.Consume(1);
	Check(visibilityBatch && !*visibilityBatch, "visibility-only batch remains cached sampling");
	eventBatch.Reset(2);
	Check(!eventBatch.Queue(1, true) && !eventBatch.BeginCoalescing(1) && !eventBatch.Consume(1)
		&& eventBatch.Queue(2, false), "old observer epoch cannot enqueue or consume new lifecycle work");
	eventBatch.Reset();
	Check(!eventBatch.Queue(2, false) && !eventBatch.BeginCoalescing(2) && !eventBatch.Consume(2),
		"shutdown gate rejects delayed observer messages");

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

	RunRefreshReentrancyTests();

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
