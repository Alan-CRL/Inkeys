#include "../Inkeys/Inkeys/UI/Setting/Setting.SessionState.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Layout.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Theme.h"
#include "../Inkeys/Inkeys/Window/Window.MessageDispatch.h"

#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <thread>

import Inkeys.Window;

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
			const auto legacy = ResolveVisibleWindowFrameInsets(sizing, false, true, visibleBorder, false);
			const WindowFrameRect outer{ -1600, -900, -600, -200 };
			const auto client = InsetWindowFrameRect(outer, restored);
			const auto legacyClient = InsetWindowFrameRect(outer, legacy);
			const auto legacyMinimum = ResolveMinimumWindowFrame(720, 520, legacy, 1920, 1040);
			if (!Expect(legacyClient.top - outer.top == framePixels
				&& legacyClient.left - outer.left == framePixels
				&& outer.right - legacyClient.right == framePixels
				&& outer.bottom - legacyClient.bottom == framePixels
				&& SameFrameRect(ExpandWindowFrameRect(legacyClient, legacy), outer)
				&& legacyMinimum.width == 720 + framePixels * 2
				&& legacyMinimum.height == 520 + framePixels * 2
				&& HitTestWindowFrame(outer, legacyClient, -1100, legacyClient.top - 1, false, framePixels)
					== WindowFrameHit::Top
				&& HitTestWindowFrame(outer, legacyClient, -1100, legacyClient.top, false, framePixels)
					== WindowFrameHit::Client,
				"legacy Aero keeps all native frame insets, minimum extents and resize/client boundary")) ++failures;
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
			for (const auto& control : { title.themeToggle, title.feedback, title.debug, title.version, title.minimize, title.maximize, title.close })
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

		// 创建前顶部估算为 0，实际 DWM 为 2px 时仍恢复请求的客户区高度。
		const WindowFrameRect requestedClient{ 200, 200, 2120, 1600 };
		const WindowFrameRect initialOuter{ 188, 194, 2132, 1606 };
		const WindowFrameRect desktopWork{ 0, 0, 2880, 1800 };
		const auto corrected = ResolveWindowFrameCreationCorrection(
			requestedClient, initialOuter, { 1920, 1398 }, desktopWork);
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

		// 用户倍率提高后的 HWND 最小尺寸复用同一实测 frame 校正，不把系统边框再乘倍率。
		const WindowFrameRect currentScaleOuter{ 600, 500, 1584, 1224 };
		const WindowFrameRect doubledClient{ 372, 342, 1812, 1382 };
		const auto doubledOuter = ResolveWindowFrameCreationCorrection(doubledClient,
			currentScaleOuter, { 960, 700 }, { 0, 0, 3840, 2160 });
		if (!Expect(doubledOuter.Width() == 1464 && doubledOuter.Height() == 1064
			&& doubledOuter.left + doubledOuter.Width() / 2 == 1092
			&& doubledOuter.top + doubledOuter.Height() / 2 == 862,
			"scale-two insufficient client grows to minimum using measured frame and current center")) ++failures;
		const WindowFrameRect sufficientOuter{ 600, 500, 2544, 1924 };
		if (!Expect(SameFrameRect(ResolveWindowFrameCreationCorrection(
			{ 612, 512, 2532, 1912 }, sufficientOuter, { 1920, 1400 }, { 0, 0, 3840, 2160 }), sufficientOuter),
			"already sufficient client keeps its existing size without shrinking after a scale change")) ++failures;
		if (!Expect(SameFrameRect(ResolveWindowFrameCreationCorrection(doubledClient,
			currentScaleOuter, { 960, 700 }, { 0, 0, 1000, 800 }), { 0, 0, 1000, 800 }),
			"scale minimum yields to a work area smaller than the required fixed sidebar")) ++failures;

		// 无 HWND 的生产查询也走属性不可用分支，不创建任何测试窗口。
		if (!Inkeys::Window::UsesLegacySettingFrame()
			&& Inkeys::Window::IsSettingCompositionEnabled())
		{
			const auto unknownBorder = Inkeys::Window::QuerySettingClientFrameInsets(nullptr, false, 96);
			const auto sizing = Inkeys::Window::QuerySettingFrameThickness(96);
			if (!Expect(unknownBorder.top == 0 && unknownBorder.left == sizing.cx
				&& unknownBorder.right == sizing.cx && unknownBorder.bottom == sizing.cy,
				"production frame query without a supported DWM border never substitutes SM_CYBORDER")) ++failures;
		}

		// Win10 不支持可见描边属性：顶部必须为零，但系统 resize 命中与其他三边不变。
		for (const int framePixels : { 8, 10, 12, 16 })
		{
			const WindowFrameInsets sizing{ framePixels, framePixels, framePixels, framePixels };
			const auto frame10 = ResolveVisibleWindowFrameInsets(sizing, false, true, 0);
			const WindowFrameRect outer{ -1600, -900, -600, -200 };
			const auto client = InsetWindowFrameRect(outer, frame10);
			if (!Expect(client.top == outer.top && frame10.left == framePixels
				&& frame10.right == framePixels && frame10.bottom == framePixels
				&& SameFrameRect(ExpandWindowFrameRect(client, frame10), outer)
				&& HitTestWindowFrame(outer, client, -1100, outer.top, false, framePixels) == WindowFrameHit::Top
				&& HitTestWindowFrame(outer, client, outer.left, outer.top, false, framePixels) == WindowFrameHit::TopLeft
				&& HitTestWindowFrame(outer, client, outer.right - 1, outer.top, false, framePixels) == WindowFrameHit::TopRight
				&& HitTestWindowFrame(outer, client, -1100, outer.top, false, framePixels, true) == WindowFrameHit::Client
				&& ResolveVisibleWindowFrameInsets(sizing, true, true, 0).top == framePixels
				&& ResolveVisibleWindowFrameInsets(sizing, false, true, 0, false).top == framePixels,
				"Win10 zero top removes the system caption without changing resize grips, controls, max or legacy")) ++failures;
		}

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
					&& title.themeToggle.right < title.feedback.left
					&& title.feedback.right < title.debug.left
					&& std::abs(title.themeToggle.Width() - 32.0F * scale) < 0.001F
					&& std::abs(title.feedback.Width() - 32.0F * scale) < 0.001F
					&& title.feedback.Contains(title.feedback.left + 16.0F * scale, y)
					&& !title.drag.Contains(title.feedback.left + 16.0F * scale, y)
					&& title.drag.Width() > 0 && (width < 720 || title.drag.Width() >= 96 * scale)
					&& std::abs(title.debug.Width() - 32.0F * scale) < 0.001F
					&& title.debug.right < title.minimize.left
					&& title.debug.Contains(title.debug.left + 16.0F * scale, y)
					&& !title.drag.Contains(title.debug.left + 16.0F * scale, y)
					&& (!title.versionVisible || title.version.right < title.themeToggle.left),
					"theme, feedback and debug retain ordered independent 32 DIP client targets and the available drag budget")) ++failures;
			}
		return failures;
	}
	int CheckSettingPlatformCompatibility()
	{
		using namespace Inkeys::Window;
		using namespace Inkeys::UI::Setting;
		int failures = 0;
		struct PlatformCase { DWORD major, minor, build; bool legacy, alwaysDwm, acrylic; };
		for (const auto& version : { PlatformCase{ 6, 1, 7601, true, false, false },
			PlatformCase{ 6, 2, 9200, false, true, false }, PlatformCase{ 6, 3, 9600, false, true, false },
			PlatformCase{ 10, 0, 10240, false, true, false }, PlatformCase{ 10, 0, 17133, false, true, false },
			PlatformCase{ 10, 0, 17134, false, true, true }, PlatformCase{ 10, 0, 19045, false, true, true },
			PlatformCase{ 10, 0, 22000, false, true, true }, PlatformCase{ 10, 0, 26100, false, true, true } })
		{
			const auto platform = ResolveSettingPlatformCapabilities(version.major, version.minor, version.build);
			if (!Expect(platform.legacyFrame == version.legacy && platform.compositionAlwaysEnabled == version.alwaysDwm
				&& platform.acrylic == version.acrylic,
				"Windows 7, 8, 8.1 and Windows 10 build boundaries select only available Settings capabilities")) ++failures;
			for (const bool succeeded : { false, true }) for (const bool enabled : { false, true })
				if (!Expect(ResolveSettingCompositionEnabled(platform, succeeded, enabled)
					== (version.alwaysDwm || (succeeded && enabled)),
					"Win8+ ignores manifest compatibility FALSE; Win7 still respects actual composition query and switch")) ++failures;
		}
		if (!Expect(ResolveSettingClientDpi(192, 144, 120, 96) == 192
			&& ResolveSettingClientDpi(0, 192, 96, 96) == 192
			&& ResolveSettingClientDpi(0, 0, 144, 96) == 144
			&& ResolveSettingClientDpi(0, 0, 0, 120) == 120
			&& ResolveSettingClientDpi(0, 0, 0, 0) == 96,
			"client DPI prefers modern HWND API, latest Win8.1 message, initial monitor, then system defaults")) ++failures;
		for (const UINT messageDpi : { 120U, 144U, 192U })
		{
			const float scale = DpiScale(ResolveSettingClientDpi(0, messageDpi, 96, 96));
			// 旧 AdjustWindowRectEx 的 NC 仍是系统物理度量，不跟随客户区 DPI 再乘倍率。
			const WindowFrameInsets systemFrame{ 8, 0, 8, 8 };
			const auto minimum = ResolveMinimumWindowFrame(ScaleDip(720, scale), ScaleDip(520, scale),
				systemFrame, 0, 0);
			const auto title = ResolveTitleBarGeometry(960 * scale, scale, 46 * scale, 100 * scale, 120 * scale);
			if (!Expect(minimum.width == ScaleDip(720, scale) + 16
				&& minimum.height == ScaleDip(520, scale) + 8 && title.height == 32 * scale,
				"Win8.1 latest message scales client minimum and title together without scaling the native frame")) ++failures;
		}
		using Inkeys::Window::MessageDispatch::MatchesSettingModalWake;
		if (!Expect(MatchesSettingModalWake(true, true, 17, 17)
			&& !MatchesSettingModalWake(false, true, 17, 17)
			&& !MatchesSettingModalWake(true, false, 17, 17)
			&& !MatchesSettingModalWake(true, true, 16, 17)
			&& !MatchesSettingModalWake(true, true, 0, 0)
			&& !IsSettingImGuiInputMessage(Inkeys::Window::MessageDispatch::SettingCancelModalMessage, 17, 0),
			"modal wake requires the current Settings HWND, owner and generation, independently of running state")) ++failures;
		return failures;
	}

	int CheckSettingNativeMessageRouting()
	{
		using namespace Inkeys::UI::Setting;
		int failures = 0;
		for (const UINT message : { WM_MOVE, WM_MOVING, WM_SIZE, WM_SIZING,
			WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_ENTERSIZEMOVE, WM_EXITSIZEMOVE,
			WM_GETMINMAXINFO, WM_NCCALCSIZE, WM_NCPAINT, WM_CANCELMODE, WM_SHOWWINDOW,
			WM_DPICHANGED, WM_THEMECHANGED, WM_SETTINGCHANGE, WM_PAINT })
			if (!Expect(!IsSettingImGuiInputMessage(message, 0, 0),
				"native window transactions bypass ImGui IO and cannot wait for rendering")) ++failures;
		for (const auto hit : { HTCAPTION, HTLEFT, HTRIGHT, HTTOP, HTBOTTOM, HTTOPLEFT,
			HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT, HTMINBUTTON, HTMAXBUTTON, HTCLOSE })
			if (!Expect(!IsSettingImGuiInputMessage(WM_SETCURSOR, 0, MAKELPARAM(hit, WM_MOUSEMOVE)),
				"native move, resize and caption cursors never acquire the IO lock")) ++failures;
		for (const UINT message : { WM_MOUSEMOVE, WM_MOUSELEAVE, WM_LBUTTONDOWN, WM_LBUTTONUP,
			WM_RBUTTONDOWN, WM_RBUTTONUP, WM_MBUTTONDOWN, WM_MBUTTONUP, WM_XBUTTONDOWN, WM_XBUTTONUP,
			WM_MOUSEWHEEL, WM_MOUSEHWHEEL, WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP,
			WM_CHAR, WM_IME_CHAR, WM_IME_COMPOSITION, WM_SETFOCUS, WM_KILLFOCUS, WM_INPUTLANGCHANGE })
			if (!Expect(IsSettingImGuiInputMessage(message, VK_SPACE, 0),
				"mouse, keyboard, Alt-Space, text, IME and focus still reach the synchronized backend")) ++failures;
		if (!Expect(IsSettingImGuiInputMessage(WM_SETCURSOR, 0, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE))
			&& IsSettingImGuiInputMessage(WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0)
			&& !IsSettingImGuiInputMessage(WM_DEVICECHANGE, DBT_DEVICEARRIVAL, 0)
			&& !IsSettingImGuiInputMessage(WM_KEYDOWN, 256, 0),
			"cursor and device notifications synchronize only backend-supported parameter branches")) ++failures;

		using Op = InteractiveWindowOperation;
		for (const auto operation : { Op::Move, Op::Size })
		{
			// 同一策略也用于 loop 中的主题重应用；取消、隐藏和 EXIT 都回到 None。
			if (!Expect(ResolveSettingAccentState(true, Op::None) == SettingAccentState::AcrylicBlurBehind
				&& ResolveSettingAccentState(true, operation) == SettingAccentState::BlurBehind
				&& ResolveSettingAccentState(false, operation) == SettingAccentState::Disabled
				&& ResolveSettingAccentState(true, Op::None) == SettingAccentState::AcrylicBlurBehind,
				"Acrylic move/size and theme reapplication use light blur; exit restores Acrylic and disabled stays disabled")) ++failures;
		}
		return failures;
	}

	int CheckSettingPresentInputLock()
	{
		using Inkeys::UI::Setting::WithSettingInputUnlocked;
		using namespace std::chrono_literals;
		struct PresentException {};
		int failures = 0;
		const HRESULT outcomes[] = { S_OK, DXGI_STATUS_OCCLUDED, DXGI_ERROR_DEVICE_REMOVED, E_FAIL };
		for (unsigned path = 0; path < 4; ++path)
		{
			const bool throwOnPresent = path == 3;
			std::recursive_mutex inputMutex;
			std::unique_lock inputLock(inputMutex);
			std::promise<void> presentStarted, focusHandled, probeRequested;
			std::promise<bool> probeResult;
			auto presentReady = presentStarted.get_future();
			auto focusReady = focusHandled.get_future();
			auto probeReady = probeRequested.get_future();
			auto probeDone = probeResult.get_future();
			int processedInputs = 0;
			std::thread windowThread([&]
			{
				if (presentReady.wait_for(2s) != std::future_status::ready) return;
				{
					std::lock_guard inputGuard(inputMutex);
					++processedInputs;
				}
				focusHandled.set_value();
				if (probeReady.wait_for(2s) != std::future_status::ready) return;
				const bool acquired = inputMutex.try_lock();
				if (acquired) inputMutex.unlock();
				probeResult.set_value(acquired);
			});
			bool focusServicedDuringPresent = false, caught = false;
			HRESULT result = E_UNEXPECTED;
			try
			{
				result = WithSettingInputUnlocked(inputLock, [&]() -> HRESULT
				{
					// 模拟 DXGI 等待真实窗口线程处理焦点消息；旧的持锁 Present 路径会超时。
					presentStarted.set_value();
					focusServicedDuringPresent = focusReady.wait_for(2s) == std::future_status::ready;
					if (throwOnPresent) throw PresentException{};
					return outcomes[path];
				});
			}
			catch (const PresentException&) { caught = true; }
			const bool ownsOnResume = inputLock.owns_lock();
			probeRequested.set_value();
			const bool probedOnResume = probeDone.wait_for(2s) == std::future_status::ready;
			const bool acquiredOnResume = probedOnResume ? probeDone.get() : true;
			// 即使回归导致握手失败，也先放锁再 join，避免测试自身永久挂起。
			if (inputLock.owns_lock()) inputLock.unlock();
			windowThread.join();
			if (!Expect(focusServicedDuringPresent && processedInputs == 1,
				"Present allows the waiting window thread to process synchronized focus input")) ++failures;
			if (!Expect(ownsOnResume && probedOnResume && !acquiredOnResume,
				"Present reacquires the real input mutex before the caller resumes")) ++failures;
			if (!Expect(throwOnPresent ? caught : !caught && result == outcomes[path],
				"success, occlusion, device failure and exception paths retain their original result")) ++failures;
		}
		return failures;
	}

	int CheckLinkedPptScaleDraft()
	{
		using Inkeys::UI::Setting::LinkedPptScaleDraft;
		using Inkeys::UI::Setting::ResolveLinkedPptScaleDraft;
		int failures = 0;
		auto equal = [](float a, float b) { return std::abs(a - b) < 0.0001F; };
		LinkedPptScaleDraft draft{ 1.0F, 1.0F, true, true };
		// 持续拖动中组时保存 Record 尚未更新，下一帧仍须保留当前真实编辑。
		for (const float value : { 1.2F, 1.4F, 0.6F, 2.0F })
		{
			draft.middle = value;
			draft = ResolveLinkedPptScaleDraft(draft, false, true, false, false);
			const auto idle = ResolveLinkedPptScaleDraft(draft, false, false, false, false);
			if (!Expect(equal(draft.bottom, value) && equal(draft.middle, value)
				&& equal(idle.bottom, value) && equal(idle.middle, value),
				"linked middle drag and following idle frame retain latest edit")) ++failures;
		}
		for (const float value : { 1.6F, 0.8F, 1.0F })
		{
			draft.bottom = value;
			draft = ResolveLinkedPptScaleDraft(draft, true, false, false, false);
			if (!Expect(equal(draft.bottom, value) && equal(draft.middle, value),
				"linked bottom drag propagates its actual edit")) ++failures;
		}
		const auto reset = ResolveLinkedPptScaleDraft(
			{ 1.0F, 1.8F, true, true }, true, false, false, false);
		if (!Expect(equal(reset.bottom, 1.0F) && equal(reset.middle, 1.0F),
			"reset at the default value still synchronizes the other group")) ++failures;
		const auto unlinked = ResolveLinkedPptScaleDraft(
			{ 0.8F, 1.7F, true, false }, false, true, false, false);
		if (!Expect(equal(unlinked.bottom, 0.8F) && equal(unlinked.middle, 1.7F),
			"editing an unlinked group does not change the other scale")) ++failures;
		const auto bottomJoined = ResolveLinkedPptScaleDraft(
			{ 1.0F, 1.7F, true, true }, false, false, true, false);
		const auto middleJoined = ResolveLinkedPptScaleDraft(
			{ 1.6F, 1.0F, true, true }, false, false, false, true);
		if (!Expect(equal(bottomJoined.bottom, 1.7F)
			&& equal(bottomJoined.middle, 1.7F)
			&& equal(middleJoined.bottom, 1.6F)
			&& equal(middleJoined.middle, 1.6F),
			"enabling linked scale copies the already linked group")) ++failures;
		const auto unchanged = ResolveLinkedPptScaleDraft(
			{ 1.1F, 1.9F, true, true }, false, false, false, false);
		if (!Expect(equal(unchanged.bottom, 1.1F) && equal(unchanged.middle, 1.9F),
			"idle linked groups preserve differing initial preferences")) ++failures;
		return failures;
	}
}

int RunSettingSessionStateTests()
{
	using Inkeys::UI::Setting::BarButtonClickAction;
	using Inkeys::UI::Setting::ResolveBarButtonClickAction;
	using Inkeys::UI::Setting::SessionState;
	using Inkeys::UI::Setting::NavigationLayout;
	using Inkeys::UI::Setting::InteractiveWindowOperation;
	using Inkeys::UI::Setting::BackdropMode;
	int failures = 0;
	failures += CheckSettingWindowFrame();
	failures += CheckSettingNativeMessageRouting();
	failures += CheckSettingPresentInputLock();
	failures += CheckSettingPlatformCompatibility();
	failures += CheckLinkedPptScaleDraft();
	{
		using Inkeys::UI::Setting::ResolveSettingSystemMenuState;
		const auto normal = ResolveSettingSystemMenuState(false, false, true, true, true);
		const auto maximized = ResolveSettingSystemMenuState(false, true, true, true, true);
		const auto minimized = ResolveSettingSystemMenuState(true, false, true, true, true);
		const auto fixed = ResolveSettingSystemMenuState(false, false, false, false, false);
		if (!Expect(!normal.restore && normal.move && normal.size && normal.minimize && normal.maximize,
			"restored legacy system menu offers move, resize, minimize and maximize")) ++failures;
		if (!Expect(maximized.restore && !maximized.move && !maximized.size && maximized.minimize && !maximized.maximize,
			"maximized legacy system menu restores and minimizes without impossible resize or repeated maximize")) ++failures;
		if (!Expect(minimized.restore && !minimized.move && !minimized.size && !minimized.minimize && minimized.maximize,
			"minimized legacy system menu restores and maximizes without repeated minimize")) ++failures;
		if (!Expect(!fixed.restore && fixed.move && !fixed.size && !fixed.minimize && !fixed.maximize,
			"system menu respects missing sizing and caption-command capabilities")) ++failures;
	}

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

	if (!Expect(Inkeys::UI::Setting::DpiScale(0U) == 1.0F
		&& Inkeys::UI::Setting::DpiScale(96U) == 1.0F
		&& Inkeys::UI::Setting::DpiScale(120U) == 1.25F
		&& Inkeys::UI::Setting::DpiScale(144U) == 1.5F
		&& Inkeys::UI::Setting::DpiScale(192U) == 2.0F,
		"setting scale follows system DPI without a user multiplier")) ++failures;
	{
		using Inkeys::UI::Setting::ShouldPlaySettingEntryAnimation;
		SessionState entrySession;
		if (!Expect(!ShouldPlaySettingEntryAnimation(1, entrySession.ShowSerial())
			&& !ShouldPlaySettingEntryAnimation(2, entrySession.ShowSerial()),
			"hidden resident warm-up does not consume or play an entry")) ++failures;
		entrySession.SetVisible(true);
		entrySession.SetVisible(true);
		if (!Expect(entrySession.ShowSerial() == 1
			&& !ShouldPlaySettingEntryAnimation(0, entrySession.ShowSerial())
			&& ShouldPlaySettingEntryAnimation(1, entrySession.ShowSerial())
			&& ShouldPlaySettingEntryAnimation(2, entrySession.ShowSerial())
			&& ShouldPlaySettingEntryAnimation(-1, entrySession.ShowSerial())
			&& ShouldPlaySettingEntryAnimation(3, entrySession.ShowSerial()),
			"entry modes and invalid-mode fallback use the first actual Show; activation does not consume another")) ++failures;
		// 不报告播放成功，模拟首次尺寸不足、总动画关闭或输入取消。
		entrySession.SetVisible(false);
		entrySession.ReleaseResident();
		entrySession.SetVisible(true);
		if (!Expect(entrySession.ShowSerial() == 2
			&& !ShouldPlaySettingEntryAnimation(0, entrySession.ShowSerial())
			&& !ShouldPlaySettingEntryAnimation(1, entrySession.ShowSerial())
			&& !ShouldPlaySettingEntryAnimation(3, entrySession.ShowSerial())
			&& ShouldPlaySettingEntryAnimation(2, entrySession.ShowSerial()),
			"a skipped first opening stays consumed across Hide and resident rebuild; always may replay")) ++failures;
		SessionState nextProcess;
		nextProcess.SetVisible(true);
		if (!Expect(ShouldPlaySettingEntryAnimation(1, nextProcess.ShowSerial()),
			"a new process owns a fresh first opening")) ++failures;
	}
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
			&& narrowTitleBar.drag.Width() > 0.0F
			&& narrowTitleBar.drag.right < narrowTitleBar.themeToggle.left
			&& narrowTitleBar.feedback.Width() == 32.0F
			&& narrowTitleBar.close.right == 360.0F,
			"sub-minimum titlebar hides version and preserves all three actions, captions and a positive remaining drag area"))
			++failures;

		const auto scaledTitleBar = Inkeys::UI::Setting::ResolveTitleBarGeometry(
			1920.0F, 2.0F, 92.0F, 112.0F, 160.0F);
		if (!Expect(scaledTitleBar.height == titleBar.height * 2.0F
			&& scaledTitleBar.icon.Width() == titleBar.icon.Width() * 2.0F
			&& scaledTitleBar.close.left == titleBar.close.left * 2.0F
			&& scaledTitleBar.feedback.left == titleBar.feedback.left * 2.0F
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

	using Inkeys::UI::Setting::StartupPreviewPreference;
	if (!Expect(ResolveBarButtonClickAction(false, false)
		== BarButtonClickAction::ShowAndActivate
		&& ResolveBarButtonClickAction(false, true)
		== BarButtonClickAction::ShowAndActivate,
		"hidden setting is shown and activated")) ++failures;
	if (!Expect(ResolveBarButtonClickAction(true, false)
		== BarButtonClickAction::Activate,
		"visible unfocused setting is activated")) ++failures;
	if (!Expect(ResolveBarButtonClickAction(true, true)
		== BarButtonClickAction::Hide,
		"visible focused setting is hidden")) ++failures;
	SessionState visibility;
	if (!Expect(visibility.ShowSerial()==0 && visibility.HideSerial()==0,"hidden session starts without a home entry or reset")) ++failures;
	visibility.SetVisible(true); visibility.SetVisible(true);
	if (!Expect(visibility.ShowSerial()==1,"repeated visible requests do not replay home")) ++failures;
	visibility.SetOccluded(true); visibility.QueueResize(720,520); visibility.ReleasePresentation();
	if (!Expect(visibility.ShowSerial()==1,"occlusion resize and device presentation do not count as reopening")) ++failures;
	visibility.SetVisible(false); visibility.SetVisible(false); visibility.SetVisible(true);
	if (!Expect(visibility.ShowSerial()==2 && visibility.HideSerial()==1,"one hide-to-show transition increments each serial exactly once even between render frames")) ++failures;
	SessionState state;
	StartupPreviewPreference startupPreview;
	if (!Expect(startupPreview.StartupEnabled()
		&& startupPreview.ConfiguredEnabled(),
		"startup preview defaults enabled")) ++failures;
	if (!Expect(startupPreview.SetConfigured(false)
		&& startupPreview.StartupEnabled()
		&& !startupPreview.ConfiguredEnabled()
		&& startupPreview.ConsumeWritePending()
		&& !startupPreview.ConsumeWritePending(),
		"setting changes persistence but not current startup snapshot")) ++failures;

	auto decision = state.Resolve(1, false, false);
	if (!Expect(decision.initializeResident && !decision.createPresentation
		&& !decision.render && !decision.HasPresentationWork(), "hidden initialization builds resident resources"))
		++failures;
	state.CommitEpoch(1);

	state.SetVisible(true);
	decision = state.Resolve(1, true, false);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& decision.createPresentation
		&& decision.render, "first visible frame creates presentation")) ++failures;

	decision = state.Resolve(1, true, true);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& !decision.releasePresentation && decision.render && decision.HasPresentationWork(),
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
	if (!Expect(decision.probeOcclusion && !decision.render && decision.HasPresentationWork(),
		"occlusion keeps the production presentation gate open for probing")) ++failures;
	const auto occludedShowSerial = state.ShowSerial();
	state.QueueResize(1200, 800);
	const auto occludedResize = state.Resize();
	for (int retry = 0; retry < 3; ++retry)
	{
		decision = state.Resolve(1, true, true);
		if (!Expect(decision.HasPresentationWork() && decision.probeOcclusion && !decision.render
			&& decision.resize && state.Resize().serial == occludedResize.serial,
			"occluded retries keep probing and preserve the pending resize")) ++failures;
	}
	// 成功探测清除遮挡后，已经准入的帧继续提交；下一帧也恢复普通绘制。
	state.SetOccluded(false);
	if (!Expect(decision.HasPresentationWork() && !decision.render,
		"the admitted probe frame remains eligible after occlusion clears")) ++failures;
	decision = state.Resolve(1, true, true);
	if (!Expect(decision.HasPresentationWork() && decision.render && !decision.probeOcclusion
		&& decision.resize && state.ShowSerial() == occludedShowSerial,
		"recovery resumes rendering and resize without reopening settings")) ++failures;
	state.ConsumeResize(occludedResize.serial);

	state.PublishBusinessCompletion(1, false);
	decision = state.Resolve(1, true, true);
	const auto completion = state.BusinessCompletion();
	if (!Expect(decision.consumeBusinessCompletion && completion.serial == 1
		&& !completion.succeeded,
		"business completion requests state consumption")) ++failures;
	state.ConsumeBusinessCompletion(completion.serial);
	if (!Expect(!state.Resolve(1, true, true).consumeBusinessCompletion,
		"business completion is consumed once")) ++failures;

	state.SetOccluded(true);
	decision = state.Resolve(2, true, true);
	if (!Expect(!decision.initializeResident && decision.rebuildDeviceResources
		&& decision.releasePresentation && decision.createPresentation
		&& decision.render && decision.HasPresentationWork() && !decision.probeOcclusion,
		"generation change replaces an occluded presentation and resumes rendering")) ++failures;
	state.ReleaseResident();
	state.CommitEpoch(2);

	state.SetOccluded(true);
	state.SetVisible(false);
	decision = state.Resolve(2, true, true);
	if (!Expect(!decision.initializeResident && !decision.rebuildDeviceResources
		&& decision.releasePresentation && !decision.createPresentation
		&& !decision.render && !decision.probeOcclusion && !decision.HasPresentationWork(),
		"hide stops both rendering and occlusion probes while retaining resident resources"))
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

	// 快速 Hide→Show 清除输入，常驻 context/字体/设备资源不因输入失效而重建。
	state.SetVisible(true);
	decision = state.Resolve(2, true, true);
	if (!Expect(decision.resetInput && !decision.initializeResident
		&& !decision.rebuildDeviceResources && !decision.releasePresentation && decision.render,
		"hide then show invalidates input without rebuilding resident resources")) ++failures;
	state.ConsumeInputReset(state.InputResetSerial());
	if (!Expect(!state.Resolve(2, true, true).resetInput,
		"input reset is consumed once")) ++failures;

	state.SetVisible(false);
	state.SetVisible(false);
	state.SetVisible(true);
	decision = state.Resolve(2, true, true);
	if (!Expect(decision.resetInput && decision.render,
		"repeated hides preserve input invalidation")) ++failures;
	state.ConsumeInputReset(state.InputResetSerial());
	const auto oldResetSerial = state.InputResetSerial();
	state.SetVisible(false);
	state.SetVisible(true);
	state.CommitEpoch(2);
	state.ConsumeInputReset(oldResetSerial);
	if (!Expect(state.Resolve(2, true, true).resetInput,
		"stale frame and epoch commit cannot consume a later hide")) ++failures;
	state.ConsumeInputReset(state.InputResetSerial());

	// FIFO 的原生 Hide 晚于逻辑 Hide/Show 时，清理新输入但不覆盖新的可见意图。
	SessionState fifoSession;
	fifoSession.SetVisible(true);
	fifoSession.CommitEpoch(1);
	fifoSession.SetVisible(false);
	fifoSession.SetVisible(true);
	fifoSession.ConsumeInputReset(fifoSession.InputResetSerial());
	fifoSession.InvalidateSession();
	fifoSession.CommitEpoch(1);
	decision = fifoSession.Resolve(1, true, true);
	if (!Expect(fifoSession.IsVisible() && decision.resetInput
		&& !decision.releasePresentation && !decision.rebuildDeviceResources && decision.render,
		"delayed native hide preserves show intent and resident resources")) ++failures;
	const auto nativeHideSerial = fifoSession.InputResetSerial();
	fifoSession.InvalidateSession();
	fifoSession.ConsumeInputReset(nativeHideSerial);
	if (!Expect(fifoSession.Resolve(1, true, true).resetInput,
		"another native hide after the input snapshot remains pending")) ++failures;
	fifoSession.ConsumeInputReset(fifoSession.InputResetSerial());
	if (!Expect(!fifoSession.Resolve(1, true, true).resetInput,
		"latest native hide is consumed once")) ++failures;
	fifoSession.SetVisible(false);
	fifoSession.InvalidateSession();
	decision = fifoSession.Resolve(1, true, true);
	if (!Expect(!fifoSession.IsVisible() && decision.resetInput
		&& decision.releasePresentation && !decision.createPresentation && !decision.render,
		"native hide preserves a final hidden intent")) ++failures;

	SessionState firstSession;
	firstSession.SetVisible(false);
	decision = firstSession.Resolve(1, false, false);
	if (!Expect(decision.initializeResident && !decision.resetInput && !decision.render,
		"hide without a session permits hidden resident initialization")) ++failures;
	firstSession.ReleaseResident();
	firstSession.SetVisible(true);
	firstSession.CommitEpoch(1);
	if (!Expect(!firstSession.Resolve(1, true, false).resetInput,
		"initial creation consumes historical input invalidation")) ++failures;
	firstSession.ReleaseResident();
	firstSession.ReleaseResident();
	if (!Expect(!firstSession.Resolve(1, false, false).resetInput,
		"empty resident release stays idempotent")) ++failures;

	using Inkeys::UI::Setting::IsSharedDeviceLoss;
	if (!Expect(IsSharedDeviceLoss(DXGI_ERROR_DEVICE_REMOVED)
		&& IsSharedDeviceLoss(DXGI_ERROR_DEVICE_RESET)
		&& IsSharedDeviceLoss(DXGI_ERROR_DRIVER_INTERNAL_ERROR)
		&& !IsSharedDeviceLoss(DXGI_STATUS_OCCLUDED)
		&& !IsSharedDeviceLoss(E_FAIL),
		"DXGI failures distinguish shared device loss")) ++failures;
	if (!Expect(IsSharedDeviceLoss(E_OUTOFMEMORY, DXGI_ERROR_DEVICE_HUNG)
		&& IsSharedDeviceLoss(E_FAIL, DXGI_ERROR_INVALID_CALL)
		&& IsSharedDeviceLoss(DXGI_ERROR_DEVICE_RESET, S_OK)
		&& !IsSharedDeviceLoss(E_OUTOFMEMORY, S_OK)
		&& !IsSharedDeviceLoss(E_INVALIDARG, S_OK),
		"actual removed reason overrides generic resource HRESULT without treating ordinary OOM as device loss")) ++failures;

	return failures;
}
