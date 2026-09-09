#include "../Inkeys/Inkeys/UI/Setting/Setting.SessionState.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Layout.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Theme.h"

#include <cmath>
#include <iostream>

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[SettingSessionState] failed: " << name << '\n';
		return condition;
	}

	bool SameFrameRect(const Inkeys::UI::Setting::WindowFrameRect& left,
		const Inkeys::UI::Setting::WindowFrameRect& right)
	{
		return left.left == right.left && left.top == right.top
			&& left.right == right.right && left.bottom == right.bottom;
	}

	int CheckSettingWindowFrame()
	{
		using namespace Inkeys::UI::Setting;
		int failures = 0;
		struct NativeMaximizeFixture
		{
			WindowFrameRect monitor;
			WindowFrameRect work;
			WindowFrameInsets frame;
			WindowFrameRect nativeOuter;
		};
		// primary 为 1920×1080/work 1920×1040；包含更大/更小 secondary 与负原点。
		// nativeOuter 是协议场景的预期外框 fixture，不是本测试调用 USER32 的结果。
		const NativeMaximizeFixture cases[]{
			{ { 0, 0, 1920, 1080 }, { 0, 0, 1920, 1040 }, { 8, 8, 8, 8 },
				{ -8, -8, 1928, 1048 } },
			{ { 1920, 0, 4480, 1440 }, { 1920, 0, 4480, 1400 }, { 8, 8, 8, 8 },
				{ 1912, -8, 4488, 1408 } },
			{ { -1280, 80, 0, 1104 }, { -1280, 80, 0, 1064 }, { 8, 8, 8, 8 },
				{ -1288, 72, 8, 1072 } },
			{ { -2560, -200, 0, 1240 }, { -2520, -200, 0, 1240 }, { 12, 12, 12, 12 },
				{ -2532, -212, 12, 1252 } },
			{ { 1920, -1200, 3840, 0 }, { 1920, -1160, 3840, 0 }, { 10, 11, 10, 11 },
				{ 1910, -1171, 3850, 11 } },
			{ { -1920, 0, 0, 1080 }, { -1920, 0, 0, 1080 }, { 16, 16, 16, 16 },
				{ -1936, -16, 16, 1096 } },
		};
		for (const auto& test : cases)
		{
			// 真实 SDK 结构中的最大化值仍以 primary 为基准，只允许生产 adapter 修改 min。
			MINMAXINFO limits{};
			limits.ptReserved = { 314, -159 };
			limits.ptMaxSize = { 1920 + test.frame.left + test.frame.right,
				1040 + test.frame.top + test.frame.bottom };
			limits.ptMaxPosition = { -test.frame.left, -test.frame.top };
			limits.ptMinTrackSize = { 111, 222 };
			limits.ptMaxTrackSize = { 6400, 3200 };
			const MINMAXINFO original = limits;
			const auto minimum = ResolveMinimumWindowFrame(720, 520, test.frame,
				test.work.Width(), test.work.Height());
			ApplyWindowFrameMinimumTrack(limits, minimum);
			ApplyWindowFrameMinimumTrack(limits, minimum);
			const auto samePoint = [](const POINT& left, const POINT& right)
				{ return left.x == right.x && left.y == right.y; };
			if (!Expect(limits.ptMinTrackSize.x == minimum.width
				&& limits.ptMinTrackSize.y == minimum.height
				&& samePoint(limits.ptReserved, original.ptReserved)
				&& samePoint(limits.ptMaxSize, original.ptMaxSize)
				&& samePoint(limits.ptMaxPosition, original.ptMaxPosition)
				&& samePoint(limits.ptMaxTrackSize, original.ptMaxTrackSize),
				"minimum adapter preserves native primary max, max-track and reserved MINMAXINFO fields"))
				++failures;
			// 这里只验证最终外框 fixture 的客户区内缩，不另写 USER32 的多显示器补偿算法。
			if (!Expect(SameFrameRect(InsetWindowFrameRect(test.nativeOuter, test.frame), test.work)
				&& test.work.left >= test.monitor.left && test.work.top >= test.monitor.top
				&& test.work.right <= test.monitor.right && test.work.bottom <= test.monitor.bottom,
				"native maximum fixtures inset to larger, smaller and negative-origin work areas"))
				++failures;
		}

		for (const int framePixels : { 8, 10, 12, 16 })
		{
			const WindowFrameInsets frame{ framePixels, framePixels + 1, framePixels, framePixels + 1 };
			const WindowFrameRect client{ -1200, 100, -240, 800 };
			const auto outer = ExpandWindowFrameRect(client, frame);
			struct HitCase { int x; int y; WindowFrameHit expected; };
			const HitCase hits[]{
				{ outer.left, 400, WindowFrameHit::Left },
				{ outer.right - 1, 400, WindowFrameHit::Right },
				{ -800, outer.top, WindowFrameHit::Top },
				{ -800, outer.bottom - 1, WindowFrameHit::Bottom },
				{ outer.left, outer.top, WindowFrameHit::TopLeft },
				{ outer.right - 1, outer.top, WindowFrameHit::TopRight },
				{ outer.left, outer.bottom - 1, WindowFrameHit::BottomLeft },
				{ outer.right - 1, outer.bottom - 1, WindowFrameHit::BottomRight },
			};
			for (const auto& hit : hits)
			{
				if (!Expect(HitTestWindowFrame(outer, client, hit.x, hit.y, false) == hit.expected
					&& HitTestWindowFrame(outer, client, hit.x, hit.y, true) == WindowFrameHit::Client,
					"all eight native frame directions resize only while restored")) ++failures;
			}
			// 整条滚动条命中轨道都在 client 内，不能只保护 thumb 中心。
			for (int x = client.right - 32; x < client.right; ++x)
				for (const int y : { client.top, 400, client.bottom - 1 })
					if (!Expect(HitTestWindowFrame(outer, client, x, y, false) == WindowFrameHit::Client
						&& HitTestWindowFrame(outer, client, x, y, true) == WindowFrameHit::Client,
						"right-side client scrollbar never becomes a resize hit")) ++failures;
			if (!Expect(HitTestWindowFrame(outer, client, outer.right, 400, false) == WindowFrameHit::Outside
				&& HitTestWindowFrame(outer, client, outer.left - 1, 400, true) == WindowFrameHit::Outside
				&& SameFrameRect(InsetWindowFrameRect(outer, frame), client),
				"frame round trip preserves negative origins and half-open outer bounds")) ++failures;
		}

		// 可见细边框与顶部命中高度分开：去掉粗条不缩小顶角、不截走按钮/滚动条。
		for (const int framePixels : { 8, 10, 12, 16 })
		{
			const WindowFrameInsets sizing{ framePixels, framePixels, framePixels, framePixels };
			const int visibleBorder = framePixels >= 12 ? 2 : 1;
			const auto restored = ResolveVisibleWindowFrameInsets(sizing, false, true, visibleBorder);
			const auto maximized = ResolveVisibleWindowFrameInsets(sizing, true, true, visibleBorder);
			const auto classic = ResolveVisibleWindowFrameInsets(sizing, false, false, visibleBorder);
			const WindowFrameRect outer{ -1600, -900, -600, -200 };
			const auto client = InsetWindowFrameRect(outer, restored);
			if (!Expect(client.top - outer.top == visibleBorder
				&& restored.left == sizing.left && restored.right == sizing.right
				&& restored.bottom == sizing.bottom && maximized.top == sizing.top
				&& classic.top == sizing.top
				&& SameFrameRect(ExpandWindowFrameRect(client, restored), outer),
				"restored DWM top uses visible stroke while other borders, max and classic stay native")) ++failures;
			const int gripBottom = outer.top + framePixels;
			if (!Expect(HitTestWindowFrame(outer, client, -1100, gripBottom - 1, false, framePixels)
				== WindowFrameHit::Top
				&& HitTestWindowFrame(outer, client, -1100, gripBottom, false, framePixels)
				== WindowFrameHit::Client
				&& HitTestWindowFrame(outer, client, outer.left, gripBottom - 1, false, framePixels)
				== WindowFrameHit::TopLeft
				&& HitTestWindowFrame(outer, client, outer.right - 1, gripBottom - 1, false, framePixels)
				== WindowFrameHit::TopRight
				&& HitTestWindowFrame(outer, client, outer.left, gripBottom, false, framePixels)
				== WindowFrameHit::Left,
				"blank caption and outer corners retain full system-height top resize grips")) ++failures;
			const auto title = ResolveTitleBarGeometry(static_cast<float>(client.Width()),
				1.0F, 46.0F, 100.0F, 120.0F);
			for (const auto& control : { title.themeToggle, title.version, title.minimize, title.maximize, title.close })
			{
				const int x = client.left + static_cast<int>((control.left + control.right) * 0.5F);
				for (const int y : { client.top, client.top + 1, gripBottom - 1 })
					if (!Expect(HitTestWindowFrame(outer, client, x, y, false, framePixels, true)
						== WindowFrameHit::Client,
						"caption and theme controls stay clickable through their complete top edge")) ++failures;
			}
			for (int x = client.right - 32; x < client.right; ++x)
				for (const int y : { client.top + 32, client.bottom - 1 })
					if (!Expect(HitTestWindowFrame(outer, client, x, y, false, framePixels)
						== WindowFrameHit::Client,
						"top-only grip cannot steal any of the content scrollbar track")) ++failures;
			if (!Expect(HitTestWindowFrame(outer, client, -1100, client.top, true, framePixels)
				== WindowFrameHit::Client
				&& HitTestWindowFrame(outer, client, outer.right - 1, gripBottom - 1, true, framePixels)
				== WindowFrameHit::Client,
				"maximized frame never exposes the restored top or corner grips")) ++failures;
		}

		// 创建前细边框估算为 1px，实际 DWM 为 2px 时仍恢复请求的客户区高度。
		const WindowFrameRect requestedClient{ 200, 200, 2120, 1600 };
		const WindowFrameRect initialOuter{ 188, 194, 2132, 1607 };
		const WindowFrameRect desktopWork{ 0, 0, 2880, 1800 };
		const auto corrected = ResolveWindowFrameCreationCorrection(
			requestedClient, initialOuter, { 1920, 1399 }, desktopWork);
		if (!Expect(corrected.Width() - 24 == 1920 && corrected.Height() - 14 == 1400
			&& corrected.left + corrected.Width() / 2 == 1160
			&& corrected.top + corrected.Height() / 2 == 900,
			"creation correction uses measured frame and preserves the requested center")) ++failures;
		if (!Expect(SameFrameRect(ResolveWindowFrameCreationCorrection(
			requestedClient, corrected, { 1920, 1400 }, desktopWork), corrected),
			"matching measured client does not move or resize the created window")) ++failures;
		const WindowFrameRect smallWork{ -1800, -1000, -300, 0 };
		const WindowFrameRect largeRequest{ -2050, -1200, -130, 200 };
		if (!Expect(SameFrameRect(ResolveWindowFrameCreationCorrection(
			largeRequest, smallWork, { 1476, 986 }, smallWork), smallWork),
			"already clamped creation stays inside a smaller negative-origin work area")) ++failures;
		const WindowFrameRect bottomRequest{ -1600, -100, -1000, 0 };
		const auto bottomCorrection = ResolveWindowFrameCreationCorrection(
			bottomRequest, { -1612, -113, -988, 0 }, { 600, 99 }, smallWork);
		if (!Expect(bottomCorrection.Height() - 14 == 100 && bottomCorrection.bottom == 0
			&& bottomCorrection.top >= smallWork.top,
			"one-pixel correction moves upward when the original outer touches the work area bottom")) ++failures;

		const WindowFrameInsets frame{ 12, 12, 12, 12 };
		const auto normalMinimum = ResolveMinimumWindowFrame(720, 520, frame, 1920, 1040);
		const auto enlargedMinimum = ResolveMinimumWindowFrame(1440, 1040, frame, 3840, 2080);
		const auto constrainedMinimum = ResolveMinimumWindowFrame(2880, 2080, frame, 1920, 1040);
		if (!Expect(normalMinimum.width == 744 && normalMinimum.height == 544
			&& enlargedMinimum.width == 1464 && enlargedMinimum.height == 1064
			&& constrainedMinimum.width == 1920 && constrainedMinimum.height == 1040,
			"minimum client scale adds system frame once and yields to a smaller work area")) ++failures;

		for (const float scale : { 1.0F, 1.25F, 1.875F, 2.0F, 4.0F })
			for (const float width : { 360.0F, 720.0F, 960.0F })
			{
				const auto title = ResolveTitleBarGeometry(width * scale, scale,
					46.0F * scale, 100.0F * scale, 140.0F * scale);
				const float x = title.themeToggle.left + title.themeToggle.Width() * 0.5F;
				const float y = title.height * 0.5F;
				if (!Expect(title.themeToggle.Width() > 0.0F && title.themeToggle.Contains(x, y)
					&& !title.drag.Contains(x, y) && !title.minimize.Contains(x, y)
					&& !title.maximize.Contains(x, y) && !title.close.Contains(x, y)
					&& title.themeToggle.right < title.minimize.left
					&& (!title.versionVisible || title.version.right < title.themeToggle.left),
					"theme caption button remains an independent non-drag client target")) ++failures;
			}
		return failures;
	}
}

int RunSettingSessionStateTests()
{
	using Inkeys::UI::Setting::SessionState;
	using Inkeys::UI::Setting::NavigationLayout;
	using Inkeys::UI::Setting::InteractiveWindowOperation;
	using Inkeys::UI::Setting::BackdropMode;
	int failures = 0;
	failures += CheckSettingWindowFrame();

	if (!Expect(Inkeys::UI::Setting::InteractiveOperationFromHitTest(HTCAPTION)
		== InteractiveWindowOperation::Move
		&& Inkeys::UI::Setting::InteractiveOperationFromHitTest(HTBOTTOMRIGHT)
		== InteractiveWindowOperation::Size
		&& Inkeys::UI::Setting::InteractiveOperationFromHitTest(HTMAXBUTTON)
		== InteractiveWindowOperation::None
		&& Inkeys::UI::Setting::InteractiveOperationFromSystemCommand(SC_MOVE | 2)
		== InteractiveWindowOperation::Move
		&& Inkeys::UI::Setting::InteractiveOperationFromSystemCommand(SC_SIZE | 8)
		== InteractiveWindowOperation::Size,
		"interactive move and size inputs resolve independently")) ++failures;

	if (!Expect(Inkeys::UI::Setting::NormalizeUserScale(0.5F) == 1.0F
		&& Inkeys::UI::Setting::NormalizeUserScale(2.5F) == 2.0F
		&& Inkeys::UI::Setting::NormalizeUserScale(1.35F) == 1.35F,
		"user scale clamps to the 1.0-2.0 contract")) ++failures;
	if (!Expect(std::abs(Inkeys::UI::Setting::EffectiveScale(144U, 1.25F)
		- 1.875F) < 0.0001F,
		"effective scale multiplies system DPI and user scale")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolveWindowExtent(960.0F, 1.5F, 1920)
		== 1440
		&& Inkeys::UI::Setting::ResolveWindowExtent(700.0F, 3.0F, 1776)
		== 1776
		&& Inkeys::UI::Setting::ResolveWindowExtent(700.0F, 3.0F, 0)
		== 2100,
		"default setting extent is capped by the monitor work area")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolveNavigationLayout(1800.0F, 2.0F)
		== NavigationLayout::Open
		&& Inkeys::UI::Setting::ResolveNavigationLayout(1799.0F, 2.0F)
		== NavigationLayout::Compact
		&& Inkeys::UI::Setting::ResolveNavigationLayout(1520.0F, 2.0F)
		== NavigationLayout::Compact
		&& Inkeys::UI::Setting::ResolveNavigationLayout(1519.0F, 2.0F)
		== NavigationLayout::Overlay,
		"responsive breakpoints use logical DIP width")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolvePageWidth(1400.0F, 1.0F) == 920.0F
		&& Inkeys::UI::Setting::ResolvePageWidth(1400.0F, 1.5F) == 1380.0F
		&& Inkeys::UI::Setting::ResolvePageWidth(800.0F, 2.0F) == 800.0F,
		"page width caps at 920 DIP without expanding narrow content")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolvePageTransitionProgress(-0.01F) == 0.0F
		&& std::abs(Inkeys::UI::Setting::ResolvePageTransitionProgress(0.08F) - 0.5F)
		< 0.0001F
		&& Inkeys::UI::Setting::ResolvePageTransitionProgress(0.16F) == 1.0F
		&& Inkeys::UI::Setting::ResolvePageTransitionProgress(1.0F) == 1.0F,
		"page transition stays within the 160ms bounds")) ++failures;
	{
		const auto titleBar = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			960.0F, 1.0F, 46.0F, 56.0F, 80.0F);
		if (!Expect(titleBar.height == 32.0F
			&& titleBar.icon.Width() == 16.0F
			&& titleBar.minimize.left == 822.0F
			&& titleBar.maximize.left == 868.0F
			&& titleBar.close.left == 914.0F
			&& titleBar.versionVisible
			&& titleBar.drag.Width() >= 96.0F,
			"titlebar geometry keeps 32 DIP identity, drag, right header and captions"))
			++failures;
		if (!Expect(titleBar.close.Contains(959.0F, 16.0F)
			&& !titleBar.close.Contains(914.0F, 32.0F)
			&& titleBar.version.right < titleBar.minimize.left,
			"titlebar hit rectangles stay disjoint and use half-open bounds"))
			++failures;

		const auto narrowTitleBar = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			360.0F, 1.0F, 46.0F, 56.0F, 80.0F);
		if (!Expect(!narrowTitleBar.versionVisible
			&& narrowTitleBar.drag.Width() >= 96.0F
			&& narrowTitleBar.close.right == 360.0F,
			"narrow titlebar hides version before sacrificing drag or captions"))
			++failures;

		const auto scaledTitleBar = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			1920.0F, 2.0F, 92.0F, 112.0F, 160.0F);
		if (!Expect(scaledTitleBar.height == titleBar.height * 2.0F
			&& scaledTitleBar.icon.Width() == titleBar.icon.Width() * 2.0F
			&& scaledTitleBar.close.left == titleBar.close.left * 2.0F
			&& scaledTitleBar.versionVisible,
			"titlebar geometry scales logical DIP coordinates exactly once"))
			++failures;
	}
	if (!Expect(Inkeys::UI::Setting::ResolveThemeMode(false)
		== Inkeys::UI::Setting::ThemeMode::Light
		&& Inkeys::UI::Setting::ResolveThemeMode(true)
		== Inkeys::UI::Setting::ThemeMode::Dark,
		"settings resolves the saved explicit light or dark preference")) ++failures;
	if (!Expect(Inkeys::UI::Setting::ResolveBackdropMode(true, false, true)
		== BackdropMode::Mica
		&& Inkeys::UI::Setting::ResolveBackdropMode(false, true, true)
		== BackdropMode::Mica
		&& Inkeys::UI::Setting::ResolveBackdropMode(false, false, true)
		== BackdropMode::Acrylic
		&& Inkeys::UI::Setting::ResolveBackdropMode(false, false, false)
		== BackdropMode::Solid,
		"setting backdrop prefers Mica, then Acrylic, then solid fallback"))
		++failures;

	SessionState state;

	auto decision = state.Resolve(1, false, false);
	if (!Expect(decision.initializeResident && !decision.createPresentation
		&& !decision.render, "hidden initialization builds resident resources"))
		++failures;
	state.CommitEpoch(1);

	state.SetVisible(true);
	decision = state.Resolve(1, true, false);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& decision.createPresentation
		&& decision.render, "first visible frame creates presentation")) ++failures;

	decision = state.Resolve(1, true, true);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& !decision.releasePresentation && decision.render,
		"stable visible session renders")) ++failures;

	state.QueueResize(960, 700);
	decision = state.Resolve(1, true, true);
	const auto firstResize = state.Resize();
	if (!Expect(decision.resize && firstResize.width == 960
		&& firstResize.height == 700, "resize is queued once")) ++failures;
	state.ConsumeResize(firstResize.serial);
	if (!Expect(!state.Resolve(1, true, true).resize, "resize clears after commit"))
		++failures;
	state.QueueResize(1000, 720);
	const auto staleResize = state.Resize();
	state.QueueResize(1024, 768);
	state.ConsumeResize(staleResize.serial);
	if (!Expect(state.Resolve(1, true, true).resize
		&& state.Resize().width == 1024,
		"new resize survives stale completion")) ++failures;
	state.ConsumeResize(state.Resize().serial);

	state.QueueFontRebuild();
	decision = state.Resolve(1, true, true);
	const auto fontSerial = state.FontRebuildSerial();
	if (!Expect(decision.rebuildFonts,
		"DPI or user scale change requests a font rebuild")) ++failures;
	state.ConsumeFontRebuild(fontSerial);
	if (!Expect(!state.Resolve(1, true, true).rebuildFonts,
		"font rebuild is consumed once")) ++failures;

	state.SetOccluded(true);
	decision = state.Resolve(1, true, true);
	if (!Expect(decision.probeOcclusion && !decision.render,
		"occlusion probes without rendering")) ++failures;
	state.SetOccluded(false);

	state.PublishBusinessCompletion(1, false);
	decision = state.Resolve(1, true, true);
	const auto completion = state.BusinessCompletion();
	if (!Expect(decision.consumeBusinessCompletion && completion.serial == 1
		&& !completion.succeeded,
		"business completion requests state consumption")) ++failures;
	state.ConsumeBusinessCompletion(completion.serial);
	if (!Expect(!state.Resolve(1, true, true).consumeBusinessCompletion,
		"business completion is consumed once")) ++failures;

	decision = state.Resolve(2, true, true);
	if (!Expect(!decision.initializeResident && decision.rebuildDeviceResources
		&& decision.releasePresentation && decision.createPresentation
		&& decision.render, "generation change rebuilds both layers")) ++failures;
	state.ReleaseResident();
	state.CommitEpoch(2);

	state.SetVisible(false);
	decision = state.Resolve(2, true, true);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& decision.releasePresentation && !decision.createPresentation
		&& !decision.render, "hide keeps resident resources and becomes idle"))
		++failures;
	state.ReleasePresentation();
	decision = state.Resolve(2, true, false);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& !decision.createPresentation
		&& !decision.render, "hidden resident session stays idle")) ++failures;

	decision = state.Resolve(3, true, false);
	if (!Expect(!decision.initializeResident && decision.rebuildDeviceResources
		&& !decision.createPresentation && !decision.render,
		"hidden generation change rebuilds resident resources")) ++failures;

	using Inkeys::UI::Setting::IsSharedDeviceLoss;
	if (!Expect(IsSharedDeviceLoss(DXGI_ERROR_DEVICE_REMOVED)
		&& IsSharedDeviceLoss(DXGI_ERROR_DEVICE_RESET)
		&& IsSharedDeviceLoss(DXGI_ERROR_DRIVER_INTERNAL_ERROR)
		&& !IsSharedDeviceLoss(DXGI_STATUS_OCCLUDED)
		&& !IsSharedDeviceLoss(E_FAIL),
		"DXGI failures distinguish shared device loss")) ++failures;

	return failures;
}
