#include <windows.h>
#include "../Inkeys/Inkeys/UI/Setting/Setting.Layout.h"

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "../Inkeys/Inkeys/Window/Window.CommandCompletion.h"
#include "../Inkeys/Inkeys/Window/Window.ZOrderPolicy.h"
#include "../Inkeys/Inkeys/Window/Window.SafetyOverride.h"
#include "../Inkeys/Inkeys/Window/Window.ShellFullscreen.h"
#include "../Inkeys/Inkeys/Window/Window.CanvasPolicy.h"

import Inkeys.Window;

namespace
{
	thread_local SIZE settingNativeMinimumTrack{};
	constexpr UINT kSettingOwnerProbe = WM_APP + 74;

	LRESULT CALLBACK SettingFrameTestProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == kSettingOwnerProbe)
		{
			auto* service = reinterpret_cast<Inkeys::Window::Service*>(lParam);
			return service->SetSettingOwnedByDrawpad(wParam != 0);
		}
		if (message == WM_GETMINMAXINFO && lParam)
		{
			auto& limits = *reinterpret_cast<MINMAXINFO*>(lParam);
			settingNativeMinimumTrack = { limits.ptMinTrackSize.x, limits.ptMinTrackSize.y };
			// 此小窗口只测客户区创建合同，隔离原生 caption 的最小宽度；产品最小尺寸不变。
			Inkeys::UI::Setting::ApplyWindowFrameMinimumTrack(limits, { 1, 1 });
			return 0;
		}
		if (message == WM_NCCALCSIZE && lParam)
		{
			// 测试复用产品 frame 度量，不能让默认原生 caption 再占一次客户区高度。
			RECT& rect = wParam ? reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]
				: *reinterpret_cast<RECT*>(lParam);
			const RECT frame = Inkeys::Window::QuerySettingClientFrameInsets(hwnd, IsZoomed(hwnd) != FALSE);
			const auto client = Inkeys::UI::Setting::InsetWindowFrameRect(
				{ static_cast<int>(rect.left), static_cast<int>(rect.top),
					static_cast<int>(rect.right), static_cast<int>(rect.bottom) },
				{ static_cast<int>(frame.left), static_cast<int>(frame.top),
					static_cast<int>(frame.right), static_cast<int>(frame.bottom) });
			rect = { client.left, client.top, client.right, client.bottom };
			return 0;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	constexpr UINT kExitGateBlockOwner = WM_APP + 71;
	constexpr UINT kExitGateCapture = WM_APP + 72;
	constexpr UINT kExitGateReadCapture = WM_APP + 73;
	HANDLE exitGateEntered = nullptr;
	HANDLE exitGateRelease = nullptr;

	LRESULT CALLBACK ExitGateWindowProc(HWND hwnd, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		if (message == kExitGateBlockOwner)
		{
			if (exitGateEntered) SetEvent(exitGateEntered);
			if (exitGateRelease) (void)WaitForSingleObject(exitGateRelease, 5000);
			return 0;
		}
		if (message == kExitGateCapture)
		{
			(void)SetCapture(hwnd);
			return GetCapture() == hwnd;
		}
		if (message == kExitGateReadCapture)
			return reinterpret_cast<LRESULT>(GetCapture());
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	constexpr UINT kSettingShutdownOnShow = WM_APP + 75;
	constexpr UINT kSettingNativeHide = WM_APP + 76;
	thread_local Inkeys::Window::Service* settingShutdownOnShow = nullptr;

	LRESULT CALLBACK SettingCaptureProbeProc(HWND hwnd, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		if (message == kSettingNativeHide)
		{
			ShowWindow(hwnd, SW_HIDE);
			return !IsWindowVisible(hwnd);
		}
		if (message == kSettingShutdownOnShow)
		{
			settingShutdownOnShow = reinterpret_cast<Inkeys::Window::Service*>(lParam);
			return 0;
		}
		if (message == WM_SHOWWINDOW && wParam && settingShutdownOnShow)
		{
			auto* service = settingShutdownOnShow;
			settingShutdownOnShow = nullptr;
			(void)SetCapture(hwnd);
			service->BeginShutdown();
		}
		// 此测试窗口只核验 WindowService，不模拟完整 Setting/ImGui 会话。
		if (message == WM_CAPTURECHANGED) return 0;
		return ExitGateWindowProc(hwnd, message, wParam, lParam);
	}

	template<std::size_t WindowCount>
	[[nodiscard]] bool ContainsWindow(
		const std::array<HWND, WindowCount>& windows, HWND candidate) noexcept
	{
		for (const HWND hwnd : windows)
			if (hwnd == candidate) return true;
		return false;
	}

	[[nodiscard]] int ZOrderIndex(HWND target) noexcept
	{
		int index = 0;
		for (HWND hwnd = GetTopWindow(nullptr); hwnd;
			hwnd = GetWindow(hwnd, GW_HWNDNEXT), ++index)
		{
			if (hwnd == target) return index;
		}
		return -1;
	}

	template<std::size_t WindowCount>
	[[nodiscard]] bool HasConsistentOwnerBand(
		const std::array<HWND, WindowCount>& windows, bool topmost) noexcept
	{
		for (const HWND hwnd : windows)
		{
			if (!hwnd || !IsWindow(hwnd)
				|| ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost)
				return false;
			const HWND owner = GetWindow(hwnd, GW_OWNER);
			if (ContainsWindow(windows, owner)
				&& (ZOrderIndex(hwnd) < 0 || ZOrderIndex(hwnd) >= ZOrderIndex(owner)))
				return false;
		}
		return true;
	}

	template<std::size_t WindowCount>
	[[nodiscard]] bool IsAdditionalOwnedWindow(
		const std::array<HWND, WindowCount>& windows, HWND candidate) noexcept
	{
		// IME 等合法辅助窗可插入基础成员之间；只接受同进程、可追溯的 owner 后代。
		const DWORD expectedProcess = GetCurrentProcessId();
		for (unsigned depth = 0; candidate && depth < 64; ++depth)
		{
			DWORD process = 0;
			if (!GetWindowThreadProcessId(candidate, &process) || process != expectedProcess)
				return false;
			if (ContainsWindow(windows, candidate)) return true;
			candidate = GetWindow(candidate, GW_OWNER);
		}
		return false;
	}

	template<std::size_t WindowCount>
	[[nodiscard]] bool IsContinuousOwnerTree(
		const std::array<HWND, WindowCount>& windows) noexcept
	{
		std::size_t found = 0;
		bool unrelatedBetweenMembers = false;
		for (HWND hwnd = GetTopWindow(nullptr); hwnd;
			hwnd = GetWindow(hwnd, GW_HWNDNEXT))
		{
			if (ContainsWindow(windows, hwnd))
			{
				if (unrelatedBetweenMembers) return false;
				if (++found == WindowCount) return true;
			}
			else if (found && !IsAdditionalOwnedWindow(windows, hwnd))
				unrelatedBetweenMembers = true;
		}
		return false;
	}

	template<std::size_t WindowCount>
	[[nodiscard]] bool IsOwnerTreeAbove(
		const std::array<HWND, WindowCount>& windows, HWND reference) noexcept
	{
		const int referenceIndex = ZOrderIndex(reference);
		if (referenceIndex < 0) return false;
		for (const HWND hwnd : windows)
		{
			const int index = ZOrderIndex(hwnd);
			if (index < 0 || index >= referenceIndex) return false;
		}
		return true;
	}

	template<std::size_t WindowCount>
	[[nodiscard]] bool IsOwnerTreeBelow(
		const std::array<HWND, WindowCount>& windows, HWND reference) noexcept
	{
		const int referenceIndex = ZOrderIndex(reference);
		if (referenceIndex < 0) return false;
		for (const HWND hwnd : windows)
		{
			const int index = ZOrderIndex(hwnd);
			if (index <= referenceIndex) return false;
		}
		return true;
	}
}


int RunCanvasPolicyTests()
{
	using namespace Inkeys::Window::Detail;
	int failures = 0;
	auto check = [&](bool passed, const char* name)
	{
		if (!passed) { ++failures; std::cerr << "FAIL canvas policy: " << name << '\n'; }
	};
	// 不创建 HWND，故障点与 owner 执行的是同一个实际样式事务。
	struct ClickThroughState
	{
		LONG_PTR style = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
		bool pending = false, applying = false;
		std::uint64_t epoch = 1;
	};
	struct ClickThroughOperations
	{
		ClickThroughState& state;
		std::uint64_t epoch = 1;
		bool failRead = false, failWrite = false, failCommit = false;
		unsigned writes = 0, commits = 0;
		std::function<void()> beforeWrite;
		std::function<void()> afterWrite;
		std::function<void()> afterCommit;
		bool StillDesired() const { return epoch == state.epoch; }
		bool ReadStyle(LONG_PTR& value) const { value = state.style; return !failRead; }
		bool WriteStyle(LONG_PTR value)
		{
			++writes;
			if (failWrite) return false;
			if (beforeWrite) beforeWrite();
			state.style = value;
			if (afterWrite) afterWrite();
			return true;
		}
		bool CommitStyle()
		{
			++commits;
			if (afterCommit) afterCommit();
			return !failCommit;
		}
	};
	ClickThroughState clickState;
	ClickThroughOperations click{ clickState };
	check(ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click) && !click.writes && !click.commits,
		"same actual style performs no redundant Win32 mutation");
	check(ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, click) && click.writes == 1
		&& click.commits == 1 && clickState.style == WS_EX_NOACTIVATE && !clickState.pending,
		"clear click-through preserves unrelated extended styles");
	clickState.style |= WS_EX_TRANSPARENT;
	check(ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, click) && click.writes == 2,
		"external style drift is repaired without stale HWND or bool cache");
	click.failRead = true;
	check(!ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click) && click.writes == 2,
		"failed actual style read never fabricates success");
	click.failRead = false; click.failWrite = true;
	check(!ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click) && clickState.pending,
		"failed style write retains retry responsibility");
	click.failWrite = false; click.failCommit = true;
	check(!ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click) && clickState.pending,
		"failed FRAMECHANGED retains retry responsibility after style bit changed");
	const auto writesBeforeRetry = click.writes;
	const auto commitsBeforeRetry = click.commits;
	click.failCommit = false;
	check(ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click) && !clickState.pending
		&& click.writes == writesBeforeRetry && click.commits == commitsBeforeRetry + 1,
		"same-valued retry completes previously failed FRAMECHANGED");
	++clickState.epoch;
	check(!ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, click)
		&& (clickState.style & WS_EX_TRANSPARENT) && click.writes == writesBeforeRetry,
		"retired tool or whiteboard request cannot mutate current owner style");
	click.epoch = clickState.epoch;
	click.afterCommit = [&] { clickState.style |= WS_EX_TRANSPARENT; };
	check(!ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, click) && clickState.pending,
		"successful APIs with mismatched actual style keep retry pending");
	click.afterCommit = {};
	check(ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, click) && !clickState.pending,
		"later retry verifies and completes actual writable state");
	// 三个真实同步重入窗口：WM_STYLECHANGING、WM_STYLECHANGED、FRAMECHANGED。
	for (int boundary = 0; boundary < 3; ++boundary)
	{
		click.epoch = clickState.epoch;
		bool nestedApplied = true;
		const auto reenter = [&]
		{
			++clickState.epoch;
			ClickThroughOperations latest{ clickState, clickState.epoch };
			nestedApplied = ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, latest);
		};
		if (boundary == 0) click.beforeWrite = reenter;
		else if (boundary == 1) click.afterWrite = reenter;
		else click.afterCommit = reenter;
		check(!ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click)
			&& !nestedApplied && !clickState.applying && clickState.pending,
			"nested owner request cannot acknowledge before outer style API returns");
		click.beforeWrite = {}; click.afterWrite = {}; click.afterCommit = {};
		ClickThroughOperations latest{ clickState, clickState.epoch };
		check(ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, latest)
			&& !(clickState.style & WS_EX_TRANSPARENT) && !clickState.pending && !clickState.applying,
			"latest writable intent retries after outer owner frame retires");
	}
	click.epoch = clickState.epoch;
	click.afterWrite = [] { throw 1; };
	bool interrupted = false;
	try { (void)ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click); }
	catch (...) { interrupted = true; }
	check(interrupted && !clickState.applying && clickState.pending,
		"owner exception clears applying scope while preserving unfinished style responsibility");
	click.afterWrite = {};
	check(ApplyCanvasClickThrough(false, clickState.pending, clickState.applying, click),
		"owner can retry after exception without a permanently latched applying flag");
	ClickThroughState otherRole;
	ClickThroughOperations other{ otherRole };
	bool otherApplied = false;
	click.beforeWrite = [&]
	{ otherApplied = ApplyCanvasClickThrough(false, otherRole.pending, otherRole.applying, other); };
	check(ApplyCanvasClickThrough(true, clickState.pending, clickState.applying, click) && otherApplied,
		"independent HWND roles do not share a global reentry gate");
	unsigned closeRequests = 0;
	const std::function<void()> close = [&] { ++closeRequests; };
	check(!Inkeys::Window::DispatchWhiteboardCloseRequest(false, true, close) && closeRequests == 0,
		"desktop close retains the original window lifecycle");
	check(Inkeys::Window::DispatchWhiteboardCloseRequest(true, true, close) && closeRequests == 1,
		"taskbar whiteboard close requests exit while preserving the shared anchor");
	check(Inkeys::Window::DispatchWhiteboardCloseRequest(true, true, {}),
		"a missing close callback cannot destroy the whiteboard owner chain");
	check(Inkeys::Window::DispatchWhiteboardCloseRequest(true, true, [] { throw 1; }),
		"a failed close request cannot destroy the whiteboard owner chain");
	check(Inkeys::Window::DispatchWhiteboardCloseRequest(true, false, close) && closeRequests == 1,
		"late close after leaving whiteboard preserves its owner without replaying exit");
	check(!Inkeys::Window::DispatchWhiteboardCloseRequest(true, false, {}),
		"an ordinary unmanaged desktop window keeps the original close policy");
	const RECT screen{ -1920, -1080, 0, 0 };
	const HWND generation = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
	CanvasPolicyKey key{ generation, screen, screen, false };
	const RECT originalBounds = key.bounds;
	for (const bool whiteboard : { false, false, true }) // Desktop、PPT、Whiteboard。
	{
		key.whiteboard = whiteboard;
		CanvasRegion region;
		check(BuildCanvasRegion(key, nullptr, region), "build scene region");
		check(!region.Get() || PtInRegion(region.Get(), 17, 1078), "penultimate physical row retained");
		check(whiteboard ? !region.Get() : !PtInRegion(region.Get(), 17, 1079),
			"only desktop/ppt exclude last physical row");
		check(CanvasNonRudeValue(whiteboard, nullptr) ==
			(whiteboard ? nullptr : reinterpret_cast<HANDLE>(TRUE)), "scene NonRude intent");
		check(EqualRect(&key.bounds, &originalBounds), "policy preserves full HWND/client geometry");
	}
	key.whiteboard = false;
	const RECT expected{ 0, 1079, 1920, 1080 };
	const RECT excluded = CanvasBottomExclusion(key);
	check(EqualRect(&excluded, &expected), "negative monitor origin uses window-local physical row");
	const auto sameKey = key;
	check(sameKey == key, "desktop/ppt same policy key is idempotent");
	key.hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(2));
	check(key != sameKey, "recreated HWND never reuses prior applied generation");
	key = sameKey;
	key.bounds.top -= 40;
	check(key != sameKey && CanvasBottomExclusion(key).top == 1119,
		"real moved bounds invalidate applied policy without DIP scaling");
	key = sameKey;
	key.bounds.bottom = -1;
	const RECT alreadyExcluded = CanvasBottomExclusion(key);
	check(IsRectEmpty(&alreadyExcluded), "window already above edge needs no clipping");

	// 原有自定义形状在白板与失败回滚时均保留，不能将其误清成矩形。
	key = sameKey;
	CanvasRegion original(CreateRectRgn(8, 12, 1900, 1080));
	DWORD regionCount = 0;
	// 先完整预热相同批次，避免 GDI 的延迟初始化/回收使冷启动总数误报。
	for (int batch = 0; batch < 2; ++batch)
	{
		for (int repeat = 0; repeat < 64; ++repeat)
		{
			CanvasRegion clipped;
			check(BuildCanvasRegion(key, original.Get(), clipped)
				&& !PtInRegion(clipped.Get(), 0, 40) && PtInRegion(clipped.Get(), 16, 1078)
				&& !PtInRegion(clipped.Get(), 16, 1079), "clip intersects original custom region");
			CanvasRegion restored;
			auto whiteboardKey = key;
			whiteboardKey.whiteboard = true;
			check(BuildCanvasRegion(whiteboardKey, original.Get(), restored)
				&& EqualRgn(restored.Get(), original.Get()), "whiteboard copies original shape");
		}
		const DWORD currentCount = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
		if (batch == 0) regionCount = currentCount;
		else check(currentCount == regionCount,
			"region construction and snapshots release owned GDI resources");
	}
	key.bounds = { -2000, -1080, 80, 0 };
	CanvasRegion spanning;
	check(BuildCanvasRegion(key, nullptr, spanning)
		&& PtInRegion(spanning.Get(), 40, 1079)
		&& !PtInRegion(spanning.Get(), 80, 1079)
		&& PtInRegion(spanning.Get(), 2000, 1079), "only target monitor row intersection is excluded");

	struct PairOperations
	{
		struct WindowState
		{
			bool property = true;
			bool gap = true;
			bool visible = false;
			bool operator==(const WindowState&) const = default;
		};
		std::array<WindowState, 2> windows{ WindowState{ true, true, true }, WindowState{} };
		std::array<WindowState, 2> before{};
		std::vector<int> restoredOrder;
		int call = 0;
		int failCall = 0;
		bool failRollback = false;
		bool scene = false;
		bool target = true;
		bool enteredRollback = false;
		bool changedWhileVisible = false;
		int rollbackVisibilityRestores = 0;
		bool Passed() { return ++call != failCall; }
		bool Capture(std::size_t i)
		{
			before[i] = windows[i];
			return Passed();
		}
		bool Hide(std::size_t i)
		{
			if (!Passed()) return false;
			windows[i].visible = false;
			return true;
		}
		bool Apply(std::size_t i)
		{
			changedWhileVisible |= windows[0].visible || windows[1].visible;
			if (!Passed()) return false;
			windows[i].property = !target;
			if (!Passed()) return false;
			windows[i].gap = !target;
			return true;
		}
		bool ApplyScene()
		{
			if (!Passed()) return false;
			scene = target;
			return true;
		}
		bool RollbackScene() { scene = !target; return true; }
		bool RestoreVisibility(std::size_t i)
		{
			if (!Passed()) return false;
			if (enteredRollback) ++rollbackVisibilityRestores;
			windows[i].visible = before[i].visible;
			return true;
		}
		bool Restore(std::size_t i)
		{
			enteredRollback = true;
			restoredOrder.push_back(static_cast<int>(i));
			if (failRollback) return false;
			windows[i].property = before[i].property;
			windows[i].gap = before[i].gap;
			return true;
		}
	};
	PairOperations success;
	check(ApplyCanvasPairTransaction(2, success).succeeded && success.scene
		&& !success.windows[0].gap && !success.windows[1].gap
		&& !success.windows[0].property && !success.windows[1].property
		&& success.windows[0].visible && !success.windows[1].visible && !success.changedWhileVisible,
		"whiteboard commits both shapes/properties before restoring exclusive visibility");
	const int forwardCalls = success.call;
	auto whiteboardPolicyKey = sameKey;
	whiteboardPolicyKey.whiteboard = true;
	for (int failCall = 1; failCall <= forwardCalls; ++failCall)
	{
		PairOperations failure;
		const auto before = failure.windows;
		failure.failCall = failCall;
		const auto result = ApplyCanvasPairTransaction(2, failure);
		check(!result.succeeded && result.rollbackSucceeded && failure.windows == before && !failure.scene && !failure.changedWhileVisible,
			"any capture/hide/property/region/style/show step failure restores both canvases");
		if (failure.enteredRollback)
			check(failure.restoredOrder == std::vector<int>{ 1, 0 }
				&& failure.rollbackVisibilityRestores == 2, "trusted pair rollback restores in reverse order and then restores both visibility intents");
		// 完整恢复旧场景后，owner 的同场景检查必须跳过事务，不能周期性双隐藏/显示。
		std::optional<CanvasPolicyKey> applied = sameKey;
		const auto completion = ResolveCanvasPolicyCompletion(result, false, false);
		completion.UpdateApplied(applied, whiteboardPolicyKey);
		check(applied && *applied == sameKey && !completion.retry && !completion.dirty
			&& !CanvasPolicyNeedsUpdate(applied, sameKey),
			"same-geometry full rollback retains old policy and needs no owner retry or old-scene transaction");
	}
	// 首次应用失败不能将从未生效的目标登记为 applied；后续显式请求仍可重新应用。
	PairOperations firstApplication;
	firstApplication.failCall = 8;
	const auto firstResult = ApplyCanvasPairTransaction(2, firstApplication);
	std::optional<CanvasPolicyKey> initiallyEmpty;
	const auto firstCompletion = ResolveCanvasPolicyCompletion(firstResult, false, false);
	firstCompletion.UpdateApplied(initiallyEmpty, whiteboardPolicyKey);
	check(!initiallyEmpty && !firstCompletion.retry && !firstCompletion.dirty
		&& CanvasPolicyNeedsUpdate(initiallyEmpty, whiteboardPolicyKey),
		"initial full rollback keeps empty applied state eligible for a later explicit application");
	PairOperations changedBounds;
	changedBounds.failCall = 8;
	const auto boundsRollback = ApplyCanvasPairTransaction(2, changedBounds, false);
	check(!boundsRollback.succeeded && boundsRollback.rollbackSucceeded
		&& !changedBounds.windows[0].visible && !changedBounds.windows[1].visible,
		"old region rollback after changed bounds stays hidden until new geometry converges");
	std::optional<CanvasPolicyKey> movedApplied = sameKey;
	const auto movedCompletion = ResolveCanvasPolicyCompletion(boundsRollback, true, false);
	movedCompletion.UpdateApplied(movedApplied, whiteboardPolicyKey);
	check(!movedApplied && movedCompletion.retry && !movedCompletion.dirty
		&& CanvasPolicyNeedsUpdate(movedApplied, sameKey),
		"changed bounds invalidate old applied key and retain recovery retry after complete rollback");
	PairOperations rollbackFailure;
	rollbackFailure.failCall = 8; // 第二窗 property 已更新、region 失败。
	rollbackFailure.failRollback = true;
	const auto failedRollback = ApplyCanvasPairTransaction(2, rollbackFailure);
	check(!failedRollback.succeeded && !failedRollback.rollbackSucceeded
		&& !rollbackFailure.windows[0].visible && !rollbackFailure.windows[1].visible,
		"incomplete rollback never reveals mixed canvas policies");
	std::optional<CanvasPolicyKey> failedApplied = sameKey;
	const auto failedCompletion = ResolveCanvasPolicyCompletion(failedRollback, false, false);
	failedCompletion.UpdateApplied(failedApplied, whiteboardPolicyKey);
	check(!failedApplied && failedCompletion.retry && failedCompletion.dirty,
		"incomplete rollback clears applied key and retains scene repair plus owner retry");
	// 验证回滚过程本身不恢复可见性，不能只在末尾再次 Hide 后检查最终状态。
	PairOperations dirtyRollback;
	dirtyRollback.failCall = 8;
	const auto dirtyResult = ApplyCanvasPairTransaction(2, dirtyRollback, false);
	check(!dirtyResult.succeeded && dirtyResult.rollbackSucceeded
		&& dirtyRollback.enteredRollback && dirtyRollback.rollbackVisibilityRestores == 0
		&& !dirtyRollback.windows[0].visible && !dirtyRollback.windows[1].visible,
		"untrusted dirty canvas rollback never restores visibility even before owner retry");
	std::optional<CanvasPolicyKey> dirtyApplied = sameKey;
	const auto dirtyCompletion = ResolveCanvasPolicyCompletion(dirtyResult, false, true);
	dirtyCompletion.UpdateApplied(dirtyApplied, whiteboardPolicyKey);
	check(!dirtyApplied && dirtyCompletion.retry && dirtyCompletion.dirty,
		"previously dirty scene still needs owner repair after same-geometry complete rollback");
	// 已在故障隐藏中的旧 region 不可信；捕获失败也不能因未发生写入而停止恢复。
	PairOperations recoveryCapture;
	recoveryCapture.failCall = 1;
	const auto recoveryCaptureResult = ApplyCanvasPairTransaction(2, recoveryCapture, false);
	check(!recoveryCaptureResult.succeeded && recoveryCaptureResult.rollbackSucceeded,
		"recovery capture failure leaves transaction unmodified");
	const auto recoveryCompletion = ResolveCanvasPolicyCompletion(recoveryCaptureResult, true, true);
	recoveryCompletion.UpdateApplied(failedApplied, whiteboardPolicyKey);
	check(!failedApplied && recoveryCompletion.retry && recoveryCompletion.dirty,
		"recovery-hidden or capture-failed state keeps retry and unresolved style until real success");
	const auto recoveredCompletion = ResolveCanvasPolicyCompletion({ true, true }, true, true);
	recoveredCompletion.UpdateApplied(failedApplied, whiteboardPolicyKey);
	check(failedApplied && *failedApplied == whiteboardPolicyKey
		&& !recoveredCompletion.retry && !recoveredCompletion.dirty
		&& !CanvasPolicyNeedsUpdate(failedApplied, whiteboardPolicyKey),
		"successful recovery commits target policy and clears retry plus stale style repair");
	// 新的隐藏请求覆盖故障前的主表面快照；重试成功也不能重新揭开旧画布。
	std::optional<bool> recoveryVisible = true;
	bool recoveryIconic = true;
	NoteCanvasRecoveryHidden(recoveryVisible, recoveryIconic);
	PairOperations hiddenRetry;
	hiddenRetry.windows = rollbackFailure.windows;
	hiddenRetry.windows[0].visible = recoveryVisible.value_or(false);
	check(ApplyCanvasPairTransaction(2, hiddenRetry).succeeded
		&& !hiddenRetry.windows[0].visible && !hiddenRetry.windows[1].visible
		&& !recoveryIconic, "later Hidden request prevents failed-policy retry revealing old surface");
	std::optional<bool> noRecovery;
	NoteCanvasRecoveryHidden(noRecovery, recoveryIconic);
	check(!noRecovery, "ordinary hide does not fabricate pending recovery visibility");
	rollbackFailure.failRollback = false;
	rollbackFailure.before = {};
	rollbackFailure.windows[0].visible = true;
	check(ApplyCanvasPairTransaction(2, rollbackFailure).succeeded,
		"same scene request can reconcile after failed rollback");
	success.target = false;
	check(ApplyCanvasPairTransaction(2, success).succeeded && !success.scene
		&& success.windows[0].gap && success.windows[1].gap
		&& success.windows[0].property && success.windows[1].property,
		"whiteboard exit restores desktop/ppt gap and NonRude together");
	std::cout << "[CanvasPolicy] failures=" << failures << '\n';
	return failures;
}

int RunWindowCommandCompletionTests()
{
	using namespace Inkeys::Window::Detail;
	int failures = 0;
	const auto check = [&](bool passed, const char* name)
	{
		if (!passed) { ++failures; std::cerr << "FAIL " << name << '\n'; }
	};
	for (const bool value : { false, true })
	{
		std::promise<bool> completion;
		auto result = completion.get_future();
		completion.set_value(value);
		check(WaitForCommandResult(result, std::chrono::steady_clock::now()
			+ std::chrono::seconds(1)) == value, "bounded command preserves result");
	}
	std::promise<bool> pending;
	auto result = pending.get_future();
	const auto start = std::chrono::steady_clock::now();
	const CommandDeadline deadline = start + std::chrono::milliseconds(20);
	check(!WaitForCommandResult(result, deadline), "stalled owner command times out");
	check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
		"stalled owner wait stays bounded");
	check(CommandExpired(deadline), "late queued command is rejected");
	// 调用方已失败返回，队列仍能安全完成 promise；迟到成功不改变超时结果。
	pending.set_value(true);
	check(result.get(), "late completion remains safe after timeout");
	check(!CommandExpired({}), "ordinary commands have no startup deadline");
	std::promise<bool> ordinary;
	auto ordinaryResult = ordinary.get_future();
	ordinary.set_value(true);
	check(WaitForCommandResult(ordinaryResult, {}), "ordinary completion unchanged");
	{
		CommandCompletion queued(std::chrono::steady_clock::now() - std::chrono::milliseconds(1));
		check(!queued.Begin() && queued.Phase() == CommandPhase::Expired,
			"expired queued command never enters native mutation");
		CommandCompletion cancelled(std::chrono::steady_clock::now() + std::chrono::seconds(1));
		cancelled.Cancel();
		check(!cancelled.Begin() && cancelled.Phase() == CommandPhase::Cancelled,
			"cancelled queued command never executes");
		CommandCompletion executing(std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
		check(executing.Begin(), "owner begins current command");
		check(!executing.Wait() && executing.Phase() == CommandPhase::Executing && !executing.Current(),
			"in-flight timeout retains owner retirement responsibility");
		check(!executing.Commit(true), "late native success cannot commit expired intent");
		// 模拟恢复失败：回执不能在物理恢复完成之前变成终态。
		check(executing.Phase() == CommandPhase::Executing, "failed rollback retains executing receipt");
		executing.Retired();
		check(executing.Phase() == CommandPhase::Expired, "successful rollback retires expired command");
		CommandCompletion normal(std::chrono::steady_clock::now() + std::chrono::seconds(1));
		check(normal.Begin() && normal.Commit(true) && normal.Wait(), "normal command commits once");
		normal.Cancel();
		check(normal.Phase() == CommandPhase::Committed, "cancel after commit cannot undo terminal receipt");
		CommandCompletion old(std::chrono::steady_clock::now() + std::chrono::seconds(1));
		check(old.Begin(), "old command starts before newer intent");
		old.Cancel();
		check(!old.Commit(true) && old.Phase() == CommandPhase::Executing,
			"superseded in-flight operation remains uncommitted until restored");
		old.Retired();
		check(old.Phase() == CommandPhase::Cancelled, "superseded operation reaches cancelled after restore");
	}
	{
		ZOrderSnapshot<4> group;
		for (std::size_t i = 0; i < 4; ++i)
		{
			group.windows[i] = reinterpret_cast<HWND>(static_cast<UINT_PTR>(i + 1));
			group.ordinal[i] = 3 - i;
			group.owners[i] = i ? group.windows[i-1] : nullptr;
		}
		const auto expected = group.owners;
		check(group.Matches(expected, false), "ordinary group validates all owners and bands");
		group.topmost[2] = true;
		check(!group.Matches(expected, false), "ordinary refresh rejects a mixed band below Bar");
		group.topmost.fill(true);
		check(group.Matches(expected, true), "topmost group uses identical complete predicate");
		group.owners[2] = group.windows[0];
		check(!group.Matches(expected, true), "valid-looking owner chain cannot substitute wrong owner identity");
		group.owners = expected;
		group.ordinal[2] = group.ordinal[1] + 1;
		check(!group.Matches(expected, true), "child below its owner fails order predicate");
		group.ordinal[2] = 1;
		group.continuous = false;
		check(!group.Matches(expected, true), "foreign interleaving fails whole-group continuity");
		check(group.Above(3, 1), "Setting-like local above predicate ignores unrelated group continuity");
		group.ordinal[3] = MissingZOrder;
		check(!group.Above(3, 1) && !group.Matches(expected, true), "missing enumerated target fails finitely");
	}
	{
		// 不创建 HWND：Fake 只执行插入/读回，排序、恢复目标及失败判定均调用生产策略。
		using Snapshot = ZOrderSnapshot<10>;
		struct RelativeOperations
		{
			Snapshot state, identity;
			std::vector<HWND> stack;
			std::vector<std::size_t> moved;
			unsigned calls = 0, failAt = 0, ownerWrites = 0, bandWrites = 0;
			bool current = true, stopped = false, ignoreMove = false, ignoreOwner = false, ignoreBand = false;
			std::function<void()> afterWrite;
			RelativeOperations()
			{
				for (std::size_t i = 0; i < 9; ++i)
				{
					state.windows[i] = reinterpret_cast<HWND>(static_cast<UINT_PTR>(i + 1));
					state.generations[i] = i + 11;
					state.owners[i] = i ? state.windows[i < 4 ? i - 1 : 3] : nullptr;
				}
				// 与现场相同：四个隐藏 PPT 都在 Bar 上方，owner/band/continuous 完全合法。
				for (const auto i : { 7, 6, 5, 4, 8, 3, 2, 1, 0 }) stack.push_back(state.windows[i]);
				identity = state;
			}
			bool Passed() { return ++calls != failAt; }
			bool Current() const
			{
				if (!current || stopped) return false;
				for (std::size_t i = 0; i < 9; ++i)
					if (identity.windows[i] && (identity.windows[i] != state.windows[i]
						|| identity.generations[i] != state.generations[i])) return false;
				return true;
			}
			bool Read(Snapshot& actual)
			{
				if (!Passed()) return false;
				actual = state;
				for (std::size_t i = 0; i < 10; ++i)
				{
					if (!identity.windows[i])
					{
						actual.windows[i] = actual.owners[i] = nullptr;
						actual.generations[i] = 0;
						actual.ordinal[i] = MissingZOrder;
						continue;
					}
					const auto found = std::find(stack.begin(), stack.end(), actual.windows[i]);
					actual.ordinal[i] = found == stack.end() ? MissingZOrder : static_cast<std::size_t>(found - stack.begin());
				}
				return true;
			}
			bool SetOwner(std::size_t i, HWND owner)
			{
				if (!Passed()) return false;
				++ownerWrites;
				if (!ignoreOwner) state.owners[i] = owner;
				if (afterWrite) afterWrite();
				return true;
			}
			bool SetBand(std::size_t i, bool topmost, bool)
			{
				if (!Passed()) return false;
				++bandWrites;
				if (!ignoreBand) state.topmost[i] = topmost;
				if (afterWrite) afterWrite();
				return true;
			}
			bool PlaceAfter(std::size_t i, std::size_t anchor)
			{
				if (!Passed()) return false;
				// 不提供 HWND_TOP/激活/几何/画布/显隐入口；只模拟 Win32 相对插入。
				moved.push_back(i);
				if (!ignoreMove)
				{
					const auto hwnd = state.windows[i];
					stack.erase(std::find(stack.begin(), stack.end(), hwnd));
					stack.insert(std::find(stack.begin(), stack.end(), state.windows[anchor]) + 1, hwnd);
				}
				if (afterWrite) afterWrite();
				return true;
			}
		};
		const auto capture = [](RelativeOperations& ops) { Snapshot snapshot; (void)ops.Read(snapshot); return snapshot; };
		const auto reconcile = [](RelativeOperations& ops, const Snapshot& saved, const PptZOrder& order)
		{ return ReconcileRelativeOverlayZOrder(saved, saved.owners, false, order, ops); };
		RelativeOperations initial;
		const auto before = capture(initial);
		const auto originalOrder = CapturePptZOrder(before);
		check(reconcile(initial, before, originalOrder) && initial.moved == std::vector<std::size_t>({7,6,5,4})
			&& !initial.ownerWrites && !initial.bandWrites,
			"background hidden Middle is lowered below stationary Bar without global TOP or canvas writes");
		const auto healthy = capture(initial);
		const auto moves = initial.moved.size();
		check(reconcile(initial, healthy, originalOrder) && initial.moved.size() == moves,
			"healthy background group requires no native ordering mutation");
		std::array<std::size_t, 4> permutation{4,5,6,7};
		do
		{
			for (int promoted = -1; promoted < 4; ++promoted)
			{
				RelativeOperations ops;
				ops.stack.clear();
				for (const auto i : permutation) ops.stack.push_back(ops.state.windows[i]);
				for (const auto i : {8,3,2,1,0}) ops.stack.push_back(ops.state.windows[i]);
				const auto saved = capture(ops);
				const HWND target = promoted < 0 ? nullptr : saved.windows[4 + promoted];
				const auto desired = CapturePptZOrder(saved, target);
				check(reconcile(ops, saved, desired), "all PPT permutations preserve or promote with lower-only operations");
				std::vector<HWND> expected{saved.windows[8]};
				if (target) expected.push_back(target);
				for (const auto i : permutation) if (saved.windows[i] != target) expected.push_back(saved.windows[i]);
				check(std::equal(expected.begin(), expected.end(), ops.stack.begin()),
					"promote preserves original relative order of the other PPT siblings");
			}
		} while (std::next_permutation(permutation.begin(), permutation.end()));
		RelativeOperations foreign;
		const HWND external = reinterpret_cast<HWND>(100);
		foreign.stack.insert(foreign.stack.begin() + 3, external);
		foreign.state.continuous = false;
		const auto separated = capture(foreign);
		check(!separated.Matches(separated.owners, false) && reconcile(foreign, separated, CapturePptZOrder(separated))
			&& std::find(foreign.stack.begin(), foreign.stack.end(), external) < std::find(foreign.stack.begin(), foreign.stack.end(), separated.windows[8]),
			"whiteboard permits foreign interleaving while retaining Bar versus foreign order and strict desktop default");
		for (const auto missing : { 0, 4, 8 })
		{
			RelativeOperations partial;
			const auto absent = partial.state.windows[missing];
			partial.stack.erase(std::find(partial.stack.begin(), partial.stack.end(), absent));
			partial.state.windows[missing] = partial.identity.windows[missing] = nullptr;
			partial.state.generations[missing] = partial.identity.generations[missing] = 0;
			if (missing == 0) partial.state.owners[1] = nullptr;
			const auto saved = capture(partial);
			check(reconcile(partial, saved, CapturePptZOrder(saved)), "valid missing Mag/PPT/Bar fixtures converge without lowering owned windows below owners");
		}
		RelativeOperations badBase;
		std::swap(badBase.stack[7], badBase.stack[8]);
		const auto invalidBase = capture(badBase);
		check(!reconcile(badBase, invalidBase, CapturePptZOrder(invalidBase)),
			"correct owner identity cannot hide a child physically below its owner");
		RelativeOperations mutation;
		const auto mutationBefore = capture(mutation);
		mutation.state.owners[6] = nullptr;
		mutation.state.topmost[7] = true;
		check(reconcile(mutation, mutationBefore, originalOrder) && mutation.ownerWrites == 1 && mutation.bandWrites == 1,
			"actual owner and mixed-band drift must be repaired before success");
		const auto successfulCalls = mutation.calls;
		for (unsigned failedCall = 1; failedCall <= successfulCalls - 1; ++failedCall)
		{
			RelativeOperations failure;
			const auto saved = capture(failure);
			failure.calls = 0; failure.failAt = failedCall;
			failure.state.owners[6] = nullptr; failure.state.topmost[7] = true;
			check(!reconcile(failure, saved, originalOrder), "every native capture/owner/band/move failure rejects rather than reporting ready");
		}
		for (int ignored = 0; ignored < 3; ++ignored)
		{
			RelativeOperations ignoredWrite;
			const auto saved = capture(ignoredWrite);
			ignoredWrite.ignoreMove = ignored == 0;
			ignoredWrite.ignoreOwner = ignored == 1;
			ignoredWrite.ignoreBand = ignored == 2;
			if (ignored == 1) ignoredWrite.state.owners[6] = nullptr;
			if (ignored == 2) ignoredWrite.state.topmost[7] = true;
			check(!reconcile(ignoredWrite, saved, originalOrder) && ignoredWrite.moved.size() <= 8,
				"successful BOOL with failed readback never fabricates readiness or loops without bound");
		}
		for (int changed = 0; changed < 3; ++changed)
		{
			RelativeOperations stale;
			const auto saved = capture(stale);
			stale.afterWrite = [&] { if (changed == 0) ++stale.state.generations[6];
				else if (changed == 1) stale.state.windows[6] = reinterpret_cast<HWND>(66); else stale.current = false; };
			check(!reconcile(stale, saved, originalOrder) && stale.moved.size() == 1,
				"generation reuse, replacement and reentrant cancellation stop before next native write");
		}
		RelativeOperations recovery;
		const auto saved = capture(recovery);
		check(reconcile(recovery, saved, CapturePptZOrder(saved, saved.windows[4])), "prepare partially promoted physical state");
		// 分别验证生产排序 helper 与回执；Service 私有恢复/Retired 分派由源码复审确认。
		CommandCompletion expired(std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
		check(expired.Begin() && !expired.Wait(), "expired request retains restoration responsibility");
		recovery.ignoreMove = true;
		for (int attempt = 0; attempt < 3; ++attempt)
			check(!reconcile(recovery, saved, originalOrder) && expired.Phase() == CommandPhase::Executing,
				"relative recovery helper may fail repeatedly while receipt remains executing");
		recovery.ignoreMove = false;
		check(reconcile(recovery, saved, originalOrder),
			"pure relative recovery helper can restore captured sibling order");
		expired.Retired();
		check(expired.Phase() == CommandPhase::Expired, "restored expired operation is retired, never committed");
		const auto restored = capture(recovery);
		check(CapturePptZOrder(restored).members == originalOrder.members, "failed promote restores request-before sibling sequence");
		++recovery.state.generations[6];
		const auto current = capture(recovery);
		const auto retained = RetainSurvivingZOrder(saved, current, saved.owners);
		check(!retained.windows[6] && retained.windows[7], "retired PPT is excluded without discarding surviving siblings");
		recovery.identity = retained;
		const auto oldWrites = recovery.moved.size();
		check(reconcile(recovery, retained, CapturePptZOrder(retained))
			&& std::find(recovery.moved.begin() + oldWrites, recovery.moved.end(), 6) == recovery.moved.end(),
			"old recovery cannot write replacement generation");
		for (const std::size_t retiredParent : { 1, 3 }) // Freeze 或 Drawpad 原 HWND 的 generation 已换代。
		{
			RelativeOperations replacedParent;
			const auto parentBefore = capture(replacedParent);
			++replacedParent.state.generations[retiredParent];
			const auto parentCurrent = capture(replacedParent);
			const auto surviving = RetainSurvivingZOrder(parentBefore, parentCurrent, parentBefore.owners);
			bool excludedSubtree = surviving.windows[retiredParent - 1] == parentBefore.windows[retiredParent - 1];
			for (std::size_t i = retiredParent; i < 9; ++i) excludedSubtree = excludedSubtree && !surviving.windows[i];
			check(excludedSubtree, "same HWND with new parent generation excludes its saved old subtree but retains independent old ancestors");
			replacedParent.identity = surviving;
			check(reconcile(replacedParent, surviving, CapturePptZOrder(surviving))
				&& replacedParent.moved.empty() && !replacedParent.ownerWrites && !replacedParent.bandWrites,
				"old recovery performs no ordering, owner or band write to replacement parent subtree");
		}
		RelativeOperations shutdown;
		const auto stoppingSnapshot = capture(shutdown);
		shutdown.stopped = true;
		check(!reconcile(shutdown, stoppingSnapshot, originalOrder) && shutdown.moved.empty(),
			"shutdown forbids stale physical ordering writes");
		check(!CapturePptZOrder(saved, external).valid, "unknown promoted HWND is rejected");
	}
	{
		WindowSafetyOverride safety;
		safety.Latch(7, true, false);
		safety.Latch(7, false, true);
		check(!safety.Visible(7, true) && (safety.Style(7, 0) & WS_EX_TRANSPARENT),
			"later safe hide and transparency survive old visible/input snapshot");
		check(safety.Visible(8, true) && safety.Style(8, 0) == 0,
			"reused HWND generation does not inherit an old safety override");
		safety.AllowShow(7);
		check(safety.Visible(7, true) && safety.ClickThrough(7),
			"committed newer show clears only visibility protection");
		safety.AllowInput(8);
		check(safety.ClickThrough(7), "different generation cannot clear input protection");
		safety.AllowInput(7);
		check(!safety.ClickThrough(7), "committed newer input intent can clear matching protection");
	}
	{
		// 只驱动生产邮箱和固定目标策略；不创建窗口、不启动 Shell COM worker。
		using State = ShellFullscreenState;
		const State::Target root{ reinterpret_cast<HWND>(1), 11, 17, 23 };
		const State::Target freeze{ reinterpret_cast<HWND>(2), 12, 17, 23 };
		const State::Target drawpad{ reinterpret_cast<HWND>(3), 13, 17, 23 };
		const auto fullscreen = State::ResolveTargets(root, freeze, drawpad, true, true, false);
		check(!fullscreen[0].enabled && fullscreen[1].enabled && fullscreen[2].enabled,
			"whiteboard marks both activation surfaces and clears the ordinary root");
		const auto ordinaryFullscreen = State::ResolveTargets(root, freeze, drawpad, false, true, false);
		check(ordinaryFullscreen[0].enabled && !ordinaryFullscreen[1].enabled && !ordinaryFullscreen[2].enabled,
			"ordinary fullscreen keeps its root and clears both whiteboard surfaces");
		const auto duplicate = State::ResolveTargets(freeze, freeze, drawpad, true, true, false);
		check(duplicate[0].hwnd == freeze.hwnd && duplicate[0].enabled && !duplicate[1].hwnd
			&& duplicate[2].enabled, "shared fullscreen HWND receives one merged absolute value");
		const auto minimized = State::ResolveTargets(root, freeze, drawpad, true, true, true);
		const auto disabled = State::ResolveTargets(root, freeze, drawpad, true, false, false);
		check(minimized == disabled && !disabled[0].enabled && !disabled[1].enabled && !disabled[2].enabled,
			"minimize and leave clear all known surfaces without losing their identities");
		State state;
		state.Publish(fullscreen);
		const auto entering = state.Snapshot();
		state.Publish(fullscreen);
		check(state.Snapshot().revision == entering.revision,
			"equivalent fullscreen publishes retain a stable mailbox revision");
		check(state.Complete(entering, S_OK) && state.confirmedRevision.load() == entering.revision,
			"current fullscreen batch records its own completion");
		state.Invalidate();
		const auto activated = state.Snapshot();
		check(activated.revision > entering.revision && activated.targets == entering.targets
			&& WaitForSingleObject(state.wake, 0) == WAIT_OBJECT_0,
			"activation edge wakes the same bounded batch even when values are unchanged");
		check(!state.Complete(entering, S_OK), "pre-activation completion cannot confirm a refreshed batch");
		state.Publish(minimized);
		const auto minimizedRequest = state.Snapshot();
		check(!state.Current(activated) && !state.Complete(activated, S_OK)
			&& state.confirmedRevision.load() == entering.revision,
			"late fullscreen TRUE cannot confirm newer whole-group FALSE");
		check(state.Complete(minimizedRequest, E_FAIL) && state.lastResult.load() == E_FAIL
			&& state.confirmedRevision.load() == entering.revision,
			"failed clear keeps the current batch unconfirmed for bounded worker retry");
		state.Publish(fullscreen);
		const auto restored = state.Snapshot();
		check(restored.targets == fullscreen && !state.Complete(minimizedRequest, S_OK)
			&& state.Complete(restored, S_OK), "restore supersedes late minimized completion with both current surfaces");
		state.Publish(disabled);
		const auto leaving = state.Snapshot();
		check(!state.Complete(restored, S_OK) && state.Current(leaving) && leaving.targets == disabled,
			"latest leave retains absolute FALSE compensation for every known surface");
		auto replacement = disabled;
		++replacement[1].generation;
		state.Publish(replacement);
		const auto recreated = state.Snapshot();
		check(!state.Current(leaving) && recreated.targets[1].generation != leaving.targets[1].generation
			&& !state.TargetAlive(recreated.targets[1]), "HWND identity changes retire old generation and reject dead targets");
		state.stopping.store(true);
		state.Invalidate();
		state.Publish(fullscreen);
		check(state.Snapshot().revision == recreated.revision && !state.Current(recreated)
			&& !state.Complete(recreated, S_OK), "stopping mailbox cannot accept or confirm a late fullscreen request");
	}
	std::cout << "[WindowCommandCompletion] failures=" << failures << '\n';
	return failures;
}

int RunWindowExitVisibilityTests()
{
	using namespace Inkeys::Window;
	using namespace std::chrono_literals;
	int failures = 0;
	auto check = [&](bool condition, const char* name)
	{
		if (!condition)
		{
			std::cerr << "FAILED exit visibility: " << name << '\n';
			++failures;
		}
	};
	auto specs = [](const wchar_t* suffix)
	{
		std::vector<WindowSpec> result;
		for (const auto role : { WindowRole::MagnifierHost, WindowRole::Freeze,
			WindowRole::DrawpadPresentation, WindowRole::Drawpad })
		{
			WindowSpec spec;
			spec.role = role;
			spec.className = std::wstring(L"Inkeys.Window.ExitGate.") + suffix
				+ std::to_wstring(static_cast<unsigned>(role));
			spec.x = -25000;
			spec.y = -25000;
			spec.width = 16;
			spec.height = 16;
			spec.bindMessages = false;
			if (role == WindowRole::Drawpad)
				spec.windowProc = ExitGateWindowProc;
			result.push_back(std::move(spec));
		}
		return result;
	};

	// owner 停在自建消息中；HideAll 排在旧 Show 前，关闭后 Show 不得再显画布。
	{
		Service service(8);
		check(service.Start(specs(L"Queued")), "queued service starts");
		const HWND primary = service.Handle(WindowRole::Drawpad);
		const HWND presentation = service.Handle(WindowRole::DrawpadPresentation);
		if (primary && presentation)
		{
			check(service.SetDrawpadSurfaceVisibility(
				DrawpadSurfaceVisibility::Primary), "primary visible before shutdown");
			check(SendMessageW(primary, kExitGateCapture, 0, 0) != 0,
				"owner captures primary before shutdown");
			exitGateEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			exitGateRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			check(exitGateEntered && exitGateRelease, "owner block events");
			if (exitGateEntered && exitGateRelease)
			{
				check(PostMessageW(primary, kExitGateBlockOwner, 0, 0) != FALSE,
					"owner block posted");
				const bool entered = WaitForSingleObject(exitGateEntered, 2000)
					== WAIT_OBJECT_0;
				check(entered, "owner blocked before queue");
				if (entered)
				{
					check(service.RequestHideAllUserWindows(), "hide queued first");
					auto lateShow = std::async(std::launch::async, [&]
						{ return service.Show(WindowRole::Drawpad); });
					check(lateShow.wait_for(50ms) == std::future_status::timeout,
						"late show waits behind owner");
					service.BeginShutdown();
					SetEvent(exitGateRelease);
					const bool shown = lateShow.get();
					const bool primaryVisible = IsWindowVisible(primary) != FALSE;
					const bool presentationVisible = IsWindowVisible(presentation) != FALSE;
					std::cout << "[ExitVisibilityQueued] show=" << shown
						<< " primaryVisible=" << primaryVisible
						<< " presentationVisible=" << presentationVisible << '\n';
					check(!shown && !primaryVisible && !presentationVisible,
						"late show cannot restore either surface");
					check(SendMessageW(primary, kExitGateReadCapture, 0, 0) == 0,
						"hide all releases owner primary capture");
				}
				else SetEvent(exitGateRelease);
			}
			if (exitGateEntered) CloseHandle(exitGateEntered);
			if (exitGateRelease) CloseHandle(exitGateRelease);
			exitGateEntered = nullptr;
			exitGateRelease = nullptr;
		}
		service.StopAndJoin();
	}

	// 旧 stillDesired 已进入回调时再关门；owner 返回后必须再次复核。
	{
		Service service(8);
		check(service.Start(specs(L"Callback")), "callback service starts");
		const HWND primary = service.Handle(WindowRole::Drawpad);
		const HWND presentation = service.Handle(WindowRole::DrawpadPresentation);
		if (primary && presentation)
		{
			const HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			const HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			check(entered && release, "callback block events");
			if (entered && release)
			{
				auto oldVisibility = std::async(std::launch::async, [&]
				{
					return service.SetDrawpadSurfaceVisibility(
						DrawpadSurfaceVisibility::Primary, [&]
						{
							SetEvent(entered);
							(void)WaitForSingleObject(release, 5000);
							return true;
						});
				});
				const bool enteredCallback = WaitForSingleObject(entered, 2000)
					== WAIT_OBJECT_0;
				check(enteredCallback, "old visibility callback entered");
				service.BeginShutdown();
				SetEvent(release);
				const bool applied = oldVisibility.get();
				const bool primaryVisible = IsWindowVisible(primary) != FALSE;
				const bool presentationVisible = IsWindowVisible(presentation) != FALSE;
				std::cout << "[ExitVisibilityCallback] applied=" << applied
					<< " primaryVisible=" << primaryVisible
					<< " presentationVisible=" << presentationVisible << '\n';
				check(!applied && !primaryVisible && !presentationVisible,
					"old callback cannot restore primary");
				check(!service.SetDrawpadSurfaceVisibility(
					DrawpadSurfaceVisibility::Presentation)
					&& !service.Show(WindowRole::DrawpadPresentation)
					&& !IsWindowVisible(primary) && !IsWindowVisible(presentation),
					"post-shutdown presentation requests fail closed");
				check(service.SetDrawpadSurfaceVisibility(
					DrawpadSurfaceVisibility::Hidden), "hidden remains allowed");
			}
			if (entered) CloseHandle(entered);
			if (release) CloseHandle(release);
		}
		service.StopAndJoin();
	}
	if (!failures)
		std::cout << "PASS exit visibility gate" << '\n';
	return failures;
}

int RunSettingCaptureTests()
{
	using namespace Inkeys::Window;
	int failures = 0;
	const auto check = [&](bool condition, const char* name)
	{
		if (!condition)
		{
			std::cerr << "FAILED setting capture: " << name << '\n';
			++failures;
		}
	};
	std::vector<WindowSpec> specs;
	for (const auto role : { WindowRole::Freeze, WindowRole::DrawpadPresentation,
		WindowRole::Drawpad, WindowRole::PptBottomLeft, WindowRole::PptBottomRight,
		WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight, WindowRole::Bar, WindowRole::Setting })
	{
		WindowSpec spec;
		spec.role = role;
		spec.className = L"Inkeys.Window.SettingCapture."
			+ std::to_wstring(static_cast<unsigned>(role));
		spec.x = -25000;
		spec.y = -25000;
		spec.width = 16;
		spec.height = 16;
		spec.bindMessages = false;
		if (role == WindowRole::Setting || role == WindowRole::Bar)
			spec.windowProc = SettingCaptureProbeProc;
		specs.push_back(std::move(spec));
	}

	Service service(8);
	const bool started = service.Start(std::move(specs));
	check(started, "service starts");
	if (!started) return failures;
	const HWND setting = service.Handle(WindowRole::Setting);
	const HWND bar = service.Handle(WindowRole::Bar);
	const auto captureFor = [&](WindowRole role)
	{
		GUITHREADINFO info{};
		info.cbSize = sizeof(info);
		const bool queried = GetGUIThreadInfo(service.OwnerThreadId(role), &info) != FALSE;
		check(queried, "query real owner capture");
		return queried ? info.hwndCapture : nullptr;
	};

	for (const bool owned : { false, true })
	{
		check(service.SetSettingOwnedByDrawpad(owned), "set setting owner");
		const std::array fallbackTree{ service.Handle(WindowRole::Freeze),
			service.Handle(WindowRole::DrawpadPresentation), service.Handle(WindowRole::Drawpad),
			service.Handle(WindowRole::PptBottomLeft), service.Handle(WindowRole::PptBottomRight),
			service.Handle(WindowRole::PptMiddleLeft), service.Handle(WindowRole::PptMiddleRight), bar, setting };
		const bool consistent = !owned || HasConsistentOwnerBand(fallbackTree, true);
		check(consistent, "fallback overlay and owned setting preserve full owner band and order");
		check(service.Show(WindowRole::Setting), "show setting before capture");
		check(SendMessageW(setting, kExitGateCapture, 0, 0) != 0
			&& captureFor(WindowRole::Setting) == setting,
			"setting captures on its owner thread");
		check(service.Hide(WindowRole::Setting) && !IsWindowVisible(setting)
			&& captureFor(WindowRole::Setting) == nullptr,
			"hide releases setting capture");

		check(SendMessageW(setting, kExitGateCapture, 0, 0) != 0,
			"hidden setting recaptures for repeated hide regression");
		check(service.Hide(WindowRole::Setting)
			&& captureFor(WindowRole::Setting) == nullptr,
			"repeated hide still releases setting capture");

		check(service.Show(WindowRole::Setting) && service.Show(WindowRole::Bar),
			"show setting and bar before capture transfer");
		check(SendMessageW(setting, kExitGateCapture, 0, 0) != 0
			&& SendMessageW(bar, kExitGateCapture, 0, 0) != 0
			&& captureFor(WindowRole::Bar) == bar,
			"capture transfers to a different window");
		if (owned)
			check(SendMessageW(setting, kExitGateReadCapture, 0, 0)
				== reinterpret_cast<LRESULT>(bar),
				"owned setting sees other window capture in attached input queue");
		const bool hiddenForTransfer = service.Hide(WindowRole::Setting);
		const HWND capturedAfterTransferHide = captureFor(WindowRole::Bar);
		check(hiddenForTransfer, "hide setting after capture transfer");
		(void)SendMessageW(bar, WM_CANCELMODE, 0, 0);
		check(captureFor(WindowRole::Bar) == nullptr, "release test bar capture");

		// 同条件仅执行原生 SW_HIDE，区分服务主动清理与系统激活切换带来的取消。
		check(service.Show(WindowRole::Setting) && service.Show(WindowRole::Bar)
			&& SendMessageW(setting, kExitGateCapture, 0, 0) != 0
			&& SendMessageW(bar, kExitGateCapture, 0, 0) != 0
			&& captureFor(WindowRole::Bar) == bar,
			"native hide control captures bar");
		const bool nativeHidden = SendMessageW(setting, kSettingNativeHide, 0, 0) != 0;
		const HWND nativeCapture = captureFor(WindowRole::Bar);
		std::cout << "[NativeCaptureHide] owned=" << owned << " hidden=" << nativeHidden
			<< " bar=" << bar << " capture=" << nativeCapture << '\n';
		check(nativeHidden, "native hide control hides setting");
		if (capturedAfterTransferHide != nativeCapture)
			std::cerr << "capture hide differs from native owned=" << owned
				<< " serviceCapture=" << capturedAfterTransferHide << " nativeCapture=" << nativeCapture << '\n';
		// 两次可见窗口隐藏的激活状态可能不同；仅记录原生对照，不将 capture 相等作为合同。
		(void)SendMessageW(bar, WM_CANCELMODE, 0, 0);

		// 已隐藏窗口不会触发新的激活切换，此时必须保留另一个 HWND 的新捕获。
		check(!IsWindowVisible(setting)
			&& SendMessageW(bar, kExitGateCapture, 0, 0) != 0
			&& captureFor(WindowRole::Bar) == bar, "hidden setting control captures bar");
		check(service.Hide(WindowRole::Setting)
			&& captureFor(WindowRole::Bar) == bar,
			"hiding setting preserves other window capture");
		(void)SendMessageW(bar, WM_CANCELMODE, 0, 0);
		check(captureFor(WindowRole::Bar) == nullptr, "release repeated hide bar capture");

		check(service.Show(WindowRole::Setting)
			&& SendMessageW(setting, kExitGateCapture, 0, 0) != 0,
			"setting captures before hide all");
		check(service.HideAllUserWindows() && !IsWindowVisible(setting)
			&& captureFor(WindowRole::Setting) == nullptr,
			"hide all releases setting capture");
	}

	// 在真实 ShowWindow 回调期间关门，覆盖 Execute(Show) 的退出隐藏回退。
	check(service.SetSettingOwnedByDrawpad(true), "attach before show shutdown race");
	(void)SendMessageW(setting, kSettingShutdownOnShow, 0,
		reinterpret_cast<LPARAM>(&service));
	check(!service.Show(WindowRole::Setting) && !IsWindowVisible(setting)
		&& captureFor(WindowRole::Setting) == nullptr,
		"shutdown during show hides and releases setting capture");
	check(SendMessageW(setting, kExitGateCapture, 0, 0) != 0,
		"hidden setting captures after shutdown gate");
	check(service.HideAllUserWindows()
		&& captureFor(WindowRole::Setting) == nullptr,
		"shutdown hide all still releases setting capture");
	service.StopAndJoin();
	check(!IsWindow(setting) && !IsWindow(bar), "capture test windows are destroyed");
	if (!failures) std::cout << "PASS setting capture ownership" << '\n';
	return failures;
}

int RunWindowTests()
{
	using namespace Inkeys::Window;
	int failures = 0;
	auto check = [&](bool condition, const char* name)
		{
			if (!condition)
			{
				std::cerr << "FAILED window: " << name << '\n';
				++failures;
			}
		};

	std::atomic_bool whiteboardMinimizeAllowed = true;
	// 使用真实 HWND 对照：允许 owner 树的辅助后代，但独立竞争窗夹入仍须被拒绝。
	{
		const auto create = [](HWND owner, const wchar_t* title)
		{
			return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
				L"Static", title, WS_POPUP, -25000, -25000, 16, 16,
				owner, nullptr, GetModuleHandleW(nullptr), nullptr);
		};
		const HWND root = create(nullptr, L"Owner continuity root");
		const HWND first = create(root, L"Owner continuity first");
		const HWND second = create(root, L"Owner continuity second");
		const HWND auxiliary = create(first, L"Owner continuity auxiliary");
		const HWND external = create(nullptr, L"Owner continuity external");
		check(root && first && second && auxiliary && external, "create owner continuity controls");
		if (root && first && second && auxiliary && external)
		{
			const std::array members{ root, first, second };
			constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
			check(SetWindowPos(root, HWND_TOP, 0, 0, 0, 0, flags)
				&& SetWindowPos(first, HWND_TOP, 0, 0, 0, 0, flags | SWP_NOOWNERZORDER)
				&& SetWindowPos(auxiliary, HWND_TOP, 0, 0, 0, 0, flags | SWP_NOOWNERZORDER)
				&& SetWindowPos(second, HWND_TOP, 0, 0, 0, 0, flags | SWP_NOOWNERZORDER),
				"position owned auxiliary between base members");
			check(ZOrderIndex(second) < ZOrderIndex(auxiliary)
				&& ZOrderIndex(auxiliary) < ZOrderIndex(first)
				&& IsAdditionalOwnedWindow(members, auxiliary)
				&& IsContinuousOwnerTree(members), "owned auxiliary preserves owner tree continuity");
			check(SetWindowPos(external, second, 0, 0, 0, 0, flags | SWP_NOOWNERZORDER)
				&& ZOrderIndex(second) < ZOrderIndex(external)
				&& ZOrderIndex(external) < ZOrderIndex(first),
				"position independent competitor between base members");
			check(GetWindow(external, GW_OWNER) == nullptr
				&& !IsAdditionalOwnedWindow(members, external)
				&& !IsContinuousOwnerTree(members), "independent window still breaks owner tree continuity");
			check(SetWindowPos(external, HWND_BOTTOM, 0, 0, 0, 0, flags)
				&& IsContinuousOwnerTree(members), "owner continuity recovers after competitor leaves");
		}
		for (const HWND hwnd : { auxiliary, second, first, root, external })
			if (hwnd && IsWindow(hwnd)) (void)DestroyWindow(hwnd);
	}

	std::array<std::atomic<DWORD>, static_cast<std::size_t>(WindowRole::Count)> callbackThreads{};
	auto makeSpec = [&](WindowRole role, const wchar_t* suffix)
		{
			WindowSpec spec;
			spec.role = role;
			spec.className = std::wstring(L"Inkeys.Window.Tests.") + suffix;
			spec.title = std::wstring(L"Window test ") + suffix;
			spec.x = 20;
			spec.y = 30;
			spec.width = 160;
			spec.height = 90;
			spec.created = [&, role](HWND)
				{
					callbackThreads[static_cast<std::size_t>(role)].store(
						GetCurrentThreadId(), std::memory_order_release);
				};
			return spec;
		};

	std::vector<WindowSpec> specs;
	specs.push_back(makeSpec(WindowRole::MagnifierHost, L"MagnifierHost"));
	auto magnifierChildSpec = makeSpec(WindowRole::MagnifierChild, L"MagnifierChild");
	// MagnifierChild 使用系统类，不经过服务注册测试类。
	magnifierChildSpec.className = L"Static";
	specs.push_back(std::move(magnifierChildSpec));
	auto freezeSpec = makeSpec(WindowRole::Freeze, L"Freeze");
	freezeSpec.whiteboardMinimizeAllowed = [&] { return whiteboardMinimizeAllowed.load(); };
	specs.push_back(std::move(freezeSpec));
	specs.push_back(makeSpec(WindowRole::DrawpadPresentation, L"DrawpadPresentation"));
	specs.push_back(makeSpec(WindowRole::Drawpad, L"Drawpad"));
	specs.push_back(makeSpec(WindowRole::PptBottomLeft, L"PptBottomLeft"));
	specs.push_back(makeSpec(WindowRole::PptBottomRight, L"PptBottomRight"));
	specs.push_back(makeSpec(WindowRole::PptMiddleLeft, L"PptMiddleLeft"));
	specs.push_back(makeSpec(WindowRole::PptMiddleRight, L"PptMiddleRight"));
	specs.push_back(makeSpec(WindowRole::Bar, L"Bar"));
	RECT settingClient{}, settingOuter{};
	SIZE settingMinimumTrack{};
	UINT settingDpi = 0;
	bool settingClientMeasured = false;
	auto settingSpec = makeSpec(WindowRole::Setting, L"Setting");
	const auto settingCreated = settingSpec.created;
	settingSpec.created = [&, settingCreated](HWND hwnd)
		{
			settingCreated(hwnd);
			// 在创建线程量客户区，避免跨 DPI awareness 调用产生坐标虚拟化。
			settingClientMeasured = GetClientRect(hwnd, &settingClient) != FALSE;
			GetWindowRect(hwnd, &settingOuter);
			settingMinimumTrack = settingNativeMinimumTrack;
			using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
			const auto getDpi = reinterpret_cast<GetDpiForWindowProc>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			if (getDpi) settingDpi = getDpi(hwnd);
		};
	// 仅设置窗口的专用线程使用产品 DPI 模式，不改变其余测试窗口或测试进程。
	using SetThreadDpiContextProc = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
	const auto setThreadDpiContext = reinterpret_cast<SetThreadDpiContextProc>(
		GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
	DPI_AWARENESS_CONTEXT previousSettingDpiContext = nullptr;
	settingSpec.beforeCreate = [&]
		{
			if (!setThreadDpiContext) return true;
			previousSettingDpiContext = setThreadDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
			return previousSettingDpiContext != nullptr;
		};
	settingSpec.destroyed = [&]
		{
			if (setThreadDpiContext && previousSettingDpiContext)
				setThreadDpiContext(previousSettingDpiContext);
		};
	// 故意传旧 popup 样式，确认 Setting 角色会恢复独立应用窗口合同。
	settingSpec.style = WS_POPUP;
	settingSpec.windowProc = SettingFrameTestProc;
	settingSpec.exStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
	specs.push_back(std::move(settingSpec));
	auto observerSpec = makeSpec(WindowRole::DisplayObserver, L"DisplayObserver");
	observerSpec.bindMessages = false;
	specs.push_back(std::move(observerSpec));

	Service service(8);
	check(service.Start(std::move(specs)), "start");
	check(service.Running() && service.AllReady() && service.OverlayReady()
		&& service.SettingReady(), "ready");

	constexpr WindowRole roles[] = {
		WindowRole::MagnifierHost,
		WindowRole::MagnifierChild,
		WindowRole::Freeze,
		WindowRole::DrawpadPresentation,
		WindowRole::Drawpad,
		WindowRole::PptBottomLeft,
		WindowRole::PptBottomRight,
		WindowRole::PptMiddleLeft,
		WindowRole::PptMiddleRight,
		WindowRole::Bar,
		WindowRole::Setting,
		WindowRole::DisplayObserver,
	};
	for (const auto role : roles)
	{
		const auto index = static_cast<std::size_t>(role);
		check(service.Handle(role) && service.Ready(role), "role handle ready");
		check(service.OwnerThreadId(role) != 0
			&& service.OwnerThreadId(role) == callbackThreads[index].load(std::memory_order_acquire),
			"create callback owner thread");
	}

	const DWORD overlayThread = service.OwnerThreadId(WindowRole::MagnifierHost);
	check(service.OwnerThreadId(WindowRole::MagnifierChild) == overlayThread
		&& service.OwnerThreadId(WindowRole::Freeze) == overlayThread
		&& service.OwnerThreadId(WindowRole::DrawpadPresentation) == overlayThread
		&& service.OwnerThreadId(WindowRole::Drawpad) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptBottomLeft) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptBottomRight) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptMiddleLeft) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptMiddleRight) == overlayThread
		&& service.OwnerThreadId(WindowRole::Bar) == overlayThread
		&& service.OwnerThreadId(WindowRole::DisplayObserver) == overlayThread,
		"single overlay owner thread");
	check(service.OwnerThreadId(WindowRole::Setting) != overlayThread,
		"dedicated setting owner thread");

	const HWND magnifierHost = service.Handle(WindowRole::MagnifierHost);
	const HWND magnifierChild = service.Handle(WindowRole::MagnifierChild);
	const HWND freeze = service.Handle(WindowRole::Freeze);
	const HWND drawpadPresentation = service.Handle(WindowRole::DrawpadPresentation);
	const HWND drawpad = service.Handle(WindowRole::Drawpad);
	const HWND pptBottomLeft = service.Handle(WindowRole::PptBottomLeft);
	const HWND pptBottomRight = service.Handle(WindowRole::PptBottomRight);
	const HWND pptMiddleLeft = service.Handle(WindowRole::PptMiddleLeft);
	const HWND pptMiddleRight = service.Handle(WindowRole::PptMiddleRight);
	const HWND bar = service.Handle(WindowRole::Bar);
	const HWND setting = service.Handle(WindowRole::Setting);
	const HWND observer = service.Handle(WindowRole::DisplayObserver);
	const std::array overlayOwnerTree{
		magnifierHost,
		freeze,
		drawpadPresentation,
		drawpad,
		pptBottomLeft,
		pptBottomRight,
		pptMiddleLeft,
		pptMiddleRight,
		bar,
	};
	const std::array nonRootOverlayWindows{
		freeze,
		drawpadPresentation,
		drawpad,
		pptBottomLeft,
		pptBottomRight,
		pptMiddleLeft,
		pptMiddleRight,
		bar,
	};
	check(service.OverlayRoot() == magnifierHost, "overlay root");
	check(GetParent(magnifierChild) == magnifierHost, "magnifier child parent");
	check(GetWindow(freeze, GW_OWNER) == magnifierHost, "freeze owner");
	check(GetWindow(drawpadPresentation, GW_OWNER) == freeze,
		"drawpad presentation freeze owner");
	check(GetWindow(drawpad, GW_OWNER) == drawpadPresentation,
		"drawpad presentation owner");
	check(GetWindow(pptBottomLeft, GW_OWNER) == drawpad
		&& GetWindow(pptBottomRight, GW_OWNER) == drawpad
		&& GetWindow(pptMiddleLeft, GW_OWNER) == drawpad
		&& GetWindow(pptMiddleRight, GW_OWNER) == drawpad, "four ppt drawpad owner");
	check(GetWindow(bar, GW_OWNER) == drawpad, "bar drawpad owner");
	check(GetWindow(setting, GW_OWNER) == nullptr, "setting no owner");
	check((GetWindowLongPtrW(drawpad, GWL_STYLE) & WS_CHILD) == 0,
		"drawpad remains a top-level owned popup");
	auto barAboveDrawpadSurfaces = [&]() noexcept
		{
			const int barIndex = ZOrderIndex(bar);
			return barIndex >= 0 && barIndex < ZOrderIndex(drawpad)
				&& barIndex < ZOrderIndex(drawpadPresentation);
		};
	check(service.Show(WindowRole::Bar), "show bar for drawpad surface ordering");
	check(service.SetDrawpadSurfaceVisibility(DrawpadSurfaceVisibility::Presentation)
		&& !IsWindowVisible(drawpad) && IsWindowVisible(drawpadPresentation)
		&& IsWindowVisible(bar) && barAboveDrawpadSurfaces(),
		"bar stays above both drawpad surfaces in presentation mode");
	check(service.SetDrawpadSurfaceVisibility(DrawpadSurfaceVisibility::Primary)
		&& IsWindowVisible(drawpad) && !IsWindowVisible(drawpadPresentation)
		&& IsWindowVisible(bar) && barAboveDrawpadSurfaces(),
		"bar stays above both drawpad surfaces in primary mode");
	check(service.SetDrawpadSurfaceVisibility(DrawpadSurfaceVisibility::Hidden)
		&& service.Hide(WindowRole::Bar)
		&& !IsWindowVisible(drawpad) && !IsWindowVisible(drawpadPresentation)
		&& !IsWindowVisible(bar),
		"drawpad surface test restores hidden owner tree");
	check(FindWindowExW(HWND_MESSAGE, nullptr,
		L"Inkeys.Window.Tests.DisplayObserver", nullptr) == observer,
		"observer message-only");

	const auto settingStyle = static_cast<DWORD>(GetWindowLongPtrW(setting, GWL_STYLE));
	const auto settingExStyle = static_cast<DWORD>(GetWindowLongPtrW(setting, GWL_EXSTYLE));
	const bool legacySettingFrame = UsesLegacySettingFrame();
	const DWORD expectedSettingStyle = legacySettingFrame
		? SettingWindowStyle & ~WS_CAPTION : SettingWindowStyle;
	check((settingStyle & expectedSettingStyle) == expectedSettingStyle
		&& (settingStyle & WS_CAPTION) == (legacySettingFrame ? 0U : WS_CAPTION)
		&& (settingStyle & WS_POPUP) == 0,
		"setting keeps native sizing semantics and only legacy Aero removes caption drawing");
	const HMENU settingSystemMenu = GetSystemMenu(setting, FALSE);
	check(settingSystemMenu && IsMenu(settingSystemMenu)
		&& GetMenuState(settingSystemMenu, SC_CLOSE, MF_BYCOMMAND) != static_cast<UINT>(-1)
		&& GetMenuState(settingSystemMenu, SC_MINIMIZE, MF_BYCOMMAND) != static_cast<UINT>(-1)
		&& GetMenuState(settingSystemMenu, SC_MAXIMIZE, MF_BYCOMMAND) != static_cast<UINT>(-1),
		"setting retains its HWND-owned native close/minimize/maximize system menu after caption policy");
	check((settingExStyle & WS_EX_APPWINDOW) != 0
		&& (settingExStyle & (WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) == 0,
		"setting app ex-style");
	const bool settingClientMatches = settingClientMeasured
		&& settingClient.right - settingClient.left == 160
		&& settingClient.bottom - settingClient.top == 90;
	if (!settingClientMatches)
		std::cerr << "Setting creation geometry: client="
			<< settingClient.right - settingClient.left << 'x' << settingClient.bottom - settingClient.top
			<< " outer=" << settingOuter.right - settingOuter.left << 'x' << settingOuter.bottom - settingOuter.top
			<< " dpi=" << settingDpi << " native-min-track="
			<< settingMinimumTrack.cx << 'x' << settingMinimumTrack.cy << '\n';
	check(settingClientMatches, "setting spec dimensions describe the client area");
	check(reinterpret_cast<HICON>(SendMessageW(setting, WM_GETICON, ICON_BIG, 0)) != nullptr,
		"setting icon");
	check(service.Title(WindowRole::Setting) == L"Window test Setting", "title helper");

	const RECT requestedBounds{ 110, 120, 310, 260 };
	check(service.SetBounds(WindowRole::Bar, requestedBounds), "set bounds command");
	RECT actualBounds{};
	GetWindowRect(bar, &actualBounds);
	check(actualBounds.left == requestedBounds.left && actualBounds.top == requestedBounds.top
		&& actualBounds.right == requestedBounds.right && actualBounds.bottom == requestedBounds.bottom,
		"bounds applied");
	check(service.SetClickThrough(WindowRole::Bar, true)
		&& (GetWindowLongPtrW(bar, GWL_EXSTYLE) & WS_EX_TRANSPARENT), "click through on");
	check(service.SetClickThrough(WindowRole::Bar, false)
		&& !(GetWindowLongPtrW(bar, GWL_EXSTYLE) & WS_EX_TRANSPARENT), "click through off");
	check(service.Show(WindowRole::Setting)
		&& (GetWindowLongPtrW(setting, GWL_STYLE) & WS_VISIBLE), "show command");
	check(service.Hide(WindowRole::Setting)
		&& !(GetWindowLongPtrW(setting, GWL_STYLE) & WS_VISIBLE), "hide command");
	bool nonRootTopmost = false;
	for (const HWND hwnd : nonRootOverlayWindows)
		nonRootTopmost = nonRootTopmost
			|| (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	check(!nonRootTopmost,
		"non-root has no independent topmost before refresh");

	// 竞争窗保持隐藏，验证根刷新会把完整 owner 树作为连续整体抬升。
	const HWND competingTopmost = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		L"Static", L"Window test competing topmost", WS_POPUP,
		400, 400, 120, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	check(competingTopmost != nullptr, "create hidden competing topmost");
	if (competingTopmost)
	{
		check(SetWindowPos(competingTopmost, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE,
			"position hidden competing topmost");
		check(!IsWindowVisible(magnifierHost)
			&& !IsWindowVisible(competingTopmost),
			"topmost ordering probe remains hidden");
		check(IsContinuousOwnerTree(overlayOwnerTree)
			&& IsOwnerTreeBelow(overlayOwnerTree, competingTopmost),
			"competing topmost starts above continuous owner tree");
	}
	auto logOwnerTree = [&](const char* phase)
		{
			std::cerr << "window owner order phase=" << phase
				<< " continuous=" << IsContinuousOwnerTree(overlayOwnerTree)
				<< " policy=" << service.OverlayTopmost()
				<< " whiteboard=" << service.WhiteboardWindowMode()
				<< " competitorZ=" << ZOrderIndex(competingTopmost) << '\n';
			for (const HWND member : overlayOwnerTree)
				std::cerr << " hwnd=" << member << " owner=" << GetWindow(member, GW_OWNER)
					<< " z=" << ZOrderIndex(member)
					<< " topmost=" << ((GetWindowLongPtrW(member, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0)
					<< " visible=" << IsWindowVisible(member) << '\n';
			const HWND foreground = GetForegroundWindow();
			DWORD foregroundProcess = 0;
			const DWORD foregroundThread = GetWindowThreadProcessId(foreground, &foregroundProcess);
			std::cerr << " foreground=" << foreground << " foregroundPid=" << foregroundProcess
				<< " foregroundTid=" << foregroundThread
				<< " overlayHkl=" << GetKeyboardLayout(overlayThread)
				<< " settingHkl=" << GetKeyboardLayout(service.OwnerThreadId(WindowRole::Setting))
				<< " foregroundHkl=" << GetKeyboardLayout(foregroundThread) << '\n';
			// 默认 IME 等系统辅助窗也可能进入 owner 链；失败时保留全部同进程事实。
			int z = 0;
			for (HWND candidate = GetTopWindow(nullptr); candidate;
				candidate = GetWindow(candidate, GW_HWNDNEXT), ++z)
			{
				DWORD process = 0;
				const DWORD thread = GetWindowThreadProcessId(candidate, &process);
				if (process != GetCurrentProcessId() || ContainsWindow(overlayOwnerTree, candidate))
					continue;
				wchar_t title[256]{};
				wchar_t className[256]{};
				GetWindowTextW(candidate, title, 256);
				GetClassNameW(candidate, className, 256);
				std::wcerr << L" auxiliary hwnd=" << candidate << L" owner=" << GetWindow(candidate, GW_OWNER)
					<< L" tid=" << thread << L" z=" << z
					<< L" style=" << GetWindowLongPtrW(candidate, GWL_STYLE)
					<< L" exstyle=" << GetWindowLongPtrW(candidate, GWL_EXSTYLE)
					<< L" visible=" << IsWindowVisible(candidate)
					<< L" class=[" << className << L"] title=[" << title << L"]\n";
			}
		};
	if (competingTopmost && (!IsContinuousOwnerTree(overlayOwnerTree)
		|| !IsOwnerTreeBelow(overlayOwnerTree, competingTopmost)))
		logOwnerTree("before-root-refresh");
	check(service.RequestTopmostRefresh(), "root-only topmost refresh");
	if (!(GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST)
		|| (competingTopmost && (!IsContinuousOwnerTree(overlayOwnerTree)
			|| !IsOwnerTreeAbove(overlayOwnerTree, competingTopmost))))
		logOwnerTree("after-root-refresh");
	check((GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
		"overlay root receives topmost style");
	check(HasConsistentOwnerBand(overlayOwnerTree, true),
		"root topmost refresh propagates band and owner order to every overlay");
	if (competingTopmost)
	{
		check(IsContinuousOwnerTree(overlayOwnerTree)
			&& IsOwnerTreeAbove(overlayOwnerTree, competingTopmost),
			"hidden root refresh raises continuous owner tree above competitor");
	}
	check(service.SetOverlayTopmost(false) && !service.OverlayTopmost()
		&& !(GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST),
		"persistent overlay notopmost refresh");
	if (competingTopmost)
		check(IsOwnerTreeBelow(overlayOwnerTree, competingTopmost),
			"notopmost owner tree returns below competing topmost");
	check(HasConsistentOwnerBand(overlayOwnerTree, false),
		"root notopmost refresh propagates band and owner order to every overlay");
	check(!service.OverlayFullscreen(), "overlay defaults to non-fullscreen");
	check(service.SetOverlayFullscreen(true) && service.OverlayFullscreen()
		&& !(GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST),
		"fullscreen mark persists independently of topmost");
	check(service.SetOverlayFullscreen(false) && !service.OverlayFullscreen(),
		"fullscreen mark clears without restoring topmost");
	check(service.SetOverlayTopmost(true) && service.OverlayTopmost()
		&& (GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST),
		"persistent overlay topmost restore");
	// 已置顶根 + 隐藏 Setting 的 attach 也必须立即收敛，不能靠 Show/激活补救。
	check(service.SetSettingOwnedByDrawpad(true)
		&& !IsWindowVisible(setting)
		&& GetWindow(setting, GW_OWNER) == drawpad
		&& ZOrderIndex(setting) >= 0
		&& ZOrderIndex(setting) < ZOrderIndex(drawpad)
		&& ZOrderIndex(setting) < ZOrderIndex(drawpadPresentation),
		"hidden setting attach to prepromoted root prepares ordering without show");
	check(service.SetSettingOwnedByDrawpad(false)
		&& service.RequestTopmostRefresh(),
		"restore detached setting and promoted root before visible attach");
	check(service.Show(WindowRole::Setting),
		"show setting before owner attach");
	const DWORD settingOwnerThread = service.OwnerThreadId(WindowRole::Setting);
	const auto settingStyleBeforeOwner = GetWindowLongPtrW(setting, GWL_STYLE);
	const auto settingExStyleBeforeOwner = GetWindowLongPtrW(setting, GWL_EXSTYLE);
	const auto settingIconBeforeOwner =
		reinterpret_cast<HICON>(SendMessageW(setting, WM_GETICON, ICON_BIG, 0));
	std::atomic_int settingRefreshes = 0;
	service.SetTopmostRefreshObserver([&] { ++settingRefreshes; });
	// Setting 线程不能等待根刷新；attach 已成功但准备失败时必须回滚 owner。
	check(SendMessageW(setting, kSettingOwnerProbe, TRUE,
		reinterpret_cast<LPARAM>(&service)) == FALSE,
		"setting thread rejects attach requiring root refresh");
	check(GetWindow(setting, GW_OWNER) == nullptr,
		"failed setting preparation restores original owner");
	check(service.SetSettingOwnedByDrawpad(true), "setting attach owner");
	const int attachRefreshes = settingRefreshes.load();
	check(attachRefreshes == 1, "setting attach refreshes root exactly once");
	check(service.SetSettingOwnedByDrawpad(true) && settingRefreshes.load() == attachRefreshes,
		"unchanged correct setting owner avoids repeated root refresh");
	service.SetTopmostRefreshObserver({});
	check(GetWindow(setting, GW_OWNER) == drawpad,
		"setting joins drawpad owner chain");
	check((GetWindowLongPtrW(setting, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0
		&& ZOrderIndex(setting) >= 0
		&& ZOrderIndex(setting) < ZOrderIndex(drawpad)
		&& ZOrderIndex(setting) < ZOrderIndex(drawpadPresentation),
		"visible selection setting immediately rises above both canvases on attach");
	check(service.OwnerThreadId(WindowRole::Setting) == settingOwnerThread,
		"setting owner thread remains stable");
	check(settingOwnerThread != service.OwnerThreadId(WindowRole::Drawpad),
		"setting keeps dedicated owner thread");
	check(GetWindowLongPtrW(setting, GWL_STYLE) == settingStyleBeforeOwner
		&& (GetWindowLongPtrW(setting, GWL_EXSTYLE) & ~WS_EX_TOPMOST)
			== (settingExStyleBeforeOwner & ~WS_EX_TOPMOST)
		&& reinterpret_cast<HICON>(SendMessageW(
			setting, WM_GETICON, ICON_BIG, 0)) == settingIconBeforeOwner,
		"setting owner attach preserves app window contract");
	check(service.Hide(WindowRole::Setting) && service.Show(WindowRole::Setting)
		&& IsWindowVisible(setting)
		&& ZOrderIndex(setting) >= 0
		&& ZOrderIndex(setting) < ZOrderIndex(drawpad)
		&& ZOrderIndex(setting) < ZOrderIndex(drawpadPresentation),
		"owned setting reshow immediately stays above both canvases");
	// 周期根刷新也必须随Drawpad带起已显示的owned Setting，不能只验证Show瞬间。
	const std::array visibleSettingTree{ magnifierHost, freeze, drawpadPresentation, drawpad,
		pptBottomLeft, pptBottomRight, pptMiddleLeft, pptMiddleRight, bar, setting };
	const bool settingSurvivesRefresh = service.RequestTopmostRefresh()
		&& IsWindowVisible(setting) && GetWindow(setting, GW_OWNER) == drawpad
		&& HasConsistentOwnerBand(visibleSettingTree, true)
		&& ZOrderIndex(setting) >= 0 && ZOrderIndex(setting) < ZOrderIndex(drawpad)
		&& ZOrderIndex(setting) < ZOrderIndex(drawpadPresentation);
	if (!settingSurvivesRefresh) logOwnerTree("visible-setting-after-root-refresh");
	check(settingSurvivesRefresh, "root refresh preserves visible owned setting above both canvases");
	check(service.SetOverlayTopmost(false)
		&& service.SetSettingOwnedByDrawpad(true)
		&& (GetWindowLongPtrW(setting, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0
		&& service.Show(WindowRole::Setting)
		&& ZOrderIndex(setting) >= 0
		&& ZOrderIndex(setting) < ZOrderIndex(drawpad)
		&& ZOrderIndex(setting) < ZOrderIndex(drawpadPresentation),
		"setting immediate ordering respects non-topmost root policy");
	check(service.SetOverlayTopmost(true), "restore root policy after setting show");
	check(barAboveDrawpadSurfaces(), "root policy restore keeps bar above both canvas owners");
	if (competingTopmost && !IsOwnerTreeAbove(overlayOwnerTree, competingTopmost))
		logOwnerTree("after-setting-root-restore");
	check(service.SetSettingOwnedByDrawpad(false)
		&& service.SetSettingOwnedByDrawpad(false),
		"setting detach owner idempotent");
	check(GetWindow(setting, GW_OWNER) == nullptr
		&& (GetWindowLongPtrW(setting, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
		"setting detach leaves topmost owner chain");
	if (competingTopmost)
		check(ZOrderIndex(setting) > ZOrderIndex(competingTopmost),
			"setting detach returns below competing topmost");
	if (competingTopmost && !IsOwnerTreeAbove(overlayOwnerTree, competingTopmost))
		logOwnerTree("after-setting-detach");
	if (competingTopmost)
		check(IsOwnerTreeAbove(overlayOwnerTree, competingTopmost),
			"topmost restore raises the complete owner tree again");
	const HWND focusBeforePromote = Service::LastFocusWindow();
	check(service.PromotePptWindow(WindowRole::PptMiddleRight), "promote ppt below bar");
	check(GetWindow(bar, GW_HWNDNEXT) == pptMiddleRight
		&& GetWindow(pptMiddleRight, GW_OWNER) == drawpad,
		"promoted ppt remains drawpad sibling below bar");
	check(Service::LastFocusWindow() == focusBeforePromote,
		"promote ppt does not activate");
	check(service.Hide(WindowRole::PptBottomLeft)
		&& service.Show(WindowRole::PptBottomLeft), "reshow ppt below bar");
	check(GetWindow(bar, GW_HWNDNEXT) == pptBottomLeft
		&& GetWindow(pptBottomLeft, GW_OWNER) == drawpad,
		"reshown ppt remains drawpad sibling below bar");
	check(Service::LastFocusWindow() == focusBeforePromote,
		"reshow ppt does not activate");
	check(service.RequestTopmostRefresh() && GetWindow(bar, GW_HWNDNEXT) == pptBottomLeft
		&& HasConsistentOwnerBand(overlayOwnerTree, true),
		"root refresh preserves promoted ppt sibling order and complete owner band");
	if (competingTopmost)
		check(IsOwnerTreeAbove(overlayOwnerTree, competingTopmost),
			"bar and promoted ppt remain above competing topmost");
	if (competingTopmost)
		check(DestroyWindow(competingTopmost) != FALSE,
			"destroy hidden competing topmost");

	// 白板模式只把 Freeze 变成 taskbar anchor，其余窗口仍保持 owned popup。
	for (const auto role : {
		WindowRole::MagnifierHost, WindowRole::Freeze,
		WindowRole::PptBottomLeft, WindowRole::PptBottomRight,
		WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight,
		WindowRole::Bar })
		check(service.Show(role), "show whiteboard group member");
	check(service.SetDrawpadSurfaceVisibility(DrawpadSurfaceVisibility::Primary),
		"show primary drawpad for whiteboard group");
	check(!service.RequestWhiteboardActivation(), "presentation mode rejects whiteboard activation");
	check(service.EnterWhiteboardWindowMode(), "enter whiteboard window mode");
	check(service.RequestTopmostRefresh() && HasConsistentOwnerBand(overlayOwnerTree, false),
		"whiteboard root refresh preserves normal owner band and order");
	const auto whiteboardFreezeExStyle = static_cast<DWORD>(
		GetWindowLongPtrW(freeze, GWL_EXSTYLE));
	const auto whiteboardDrawpadExStyle = static_cast<DWORD>(
		GetWindowLongPtrW(drawpad, GWL_EXSTYLE));
	check((whiteboardFreezeExStyle & WS_EX_APPWINDOW) != 0
		&& (whiteboardFreezeExStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == 0,
		"whiteboard Freeze is the only activatable taskbar anchor");
	check((whiteboardDrawpadExStyle & WS_EX_TOOLWINDOW) != 0
		&& (whiteboardDrawpadExStyle & WS_EX_NOACTIVATE) == 0,
		"whiteboard Drawpad accepts activation without a taskbar button");
	const HWND focusBeforeWhiteboardActivation = Service::LastFocusWindow();
	check(!service.RequestWhiteboardActivation([] { return false; })
		&& Service::LastFocusWindow() == focusBeforeWhiteboardActivation,
		"cancelled whiteboard entry cannot activate its anchor");
	check(service.Hide(WindowRole::Freeze) && !service.RequestWhiteboardActivation()
		&& !IsWindowVisible(freeze), "activation cannot revive a hidden anchor");
	check(service.Show(WindowRole::Freeze) && service.RequestWhiteboardActivation(),
		"ready whiteboard accepts its explicit asynchronous activation request");
	const auto activationReceipt = service.LastCommandReceipt();
	check(service.RequestTopmostRefresh() && activationReceipt.Phase() == CommandPhase::Committed,
		"owner barrier observes the queued activation independently of caller admission");
	GUITHREADINFO whiteboardThreadInfo{ sizeof(GUITHREADINFO) };
	check(GetGUIThreadInfo(overlayThread, &whiteboardThreadInfo)
		&& (GetForegroundWindow() != freeze
			|| (whiteboardThreadInfo.hwndActive == freeze && whiteboardThreadInfo.hwndFocus == freeze)),
		"granted whiteboard foreground owns keyboard focus while denied foreground remains valid");
	whiteboardMinimizeAllowed.store(false);
	check(!service.MinimizeWhiteboardWindowGroup() && !service.WhiteboardWindowGroupMinimized(),
		"PPT policy rejects whiteboard minimization");
	whiteboardMinimizeAllowed.store(true);
	check(service.MinimizeWhiteboardWindowGroup(), "minimize whiteboard window group");
	check(service.WhiteboardWindowGroupMinimized(), "minimize publishes minimized state");
	check(!service.RequestWhiteboardActivation(), "activation cannot restore a minimized whiteboard");
	RECT boundsBeforeRejectedShow{};
	check(GetWindowRect(freeze, &boundsBeforeRejectedShow) != FALSE, "capture minimized freeze bounds");
	const RECT minimizedFreezeBounds{ 11, 13, 1911, 1073 };
	check(service.Show(WindowRole::PptBottomLeft) && !service.Show(WindowRole::Freeze)
		&& !service.SetBounds(WindowRole::Freeze, minimizedFreezeBounds)
		&& service.LastCommandReceipt().Phase() == CommandPhase::Rejected,
		"minimized freeze show and bounds cannot report a successful presentation");
	RECT boundsAfterRejectedShow{};
	check(GetWindowRect(freeze, &boundsAfterRejectedShow) != FALSE
		&& EqualRect(&boundsBeforeRejectedShow, &boundsAfterRejectedShow) != FALSE,
		"rejected minimized bounds leave the native window unchanged");
	check(IsIconic(freeze) && !IsWindowVisible(drawpad)
		&& !IsWindowVisible(pptBottomLeft) && !IsWindowVisible(pptBottomRight)
		&& IsWindowVisible(bar), "minimize hides canvas and pagination while preserving toolbar");
	check(service.RestoreWhiteboardWindowGroup(), "restore whiteboard window group");
	check(!service.WhiteboardWindowGroupMinimized() && IsWindowVisible(freeze) && IsWindowVisible(drawpad)
		&& !IsWindowVisible(drawpadPresentation)
		&& IsWindowVisible(pptBottomLeft) && IsWindowVisible(pptBottomRight)
		&& IsWindowVisible(bar), "restore returns the prior whiteboard visible state");
	RECT presentedFreezeBounds{};
	check(service.SetBounds(WindowRole::Freeze, minimizedFreezeBounds)
		&& service.Show(WindowRole::Freeze) && !IsIconic(freeze)
		&& GetWindowRect(freeze, &presentedFreezeBounds) != FALSE
		&& EqualRect(&presentedFreezeBounds, &minimizedFreezeBounds) != FALSE,
		"restored freeze commits its first background bounds and visibility retry");
	check(service.MinimizeWhiteboardWindowGroup(), "minimize before toolbar close");
	check(service.LeaveWhiteboardWindowMode(), "leave whiteboard window mode");
	check(!IsIconic(freeze) && !IsWindowVisible(freeze) && !service.WhiteboardWindowGroupMinimized(),
		"closing minimized whiteboard clears iconic anchor without restoring canvas");
	check(service.RequestTopmostRefresh() && HasConsistentOwnerBand(overlayOwnerTree, true),
		"leaving whiteboard root refresh restores complete owner band and order");
	const auto presentationFreezeExStyle = static_cast<DWORD>(
		GetWindowLongPtrW(freeze, GWL_EXSTYLE));
	check((presentationFreezeExStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) ==
		(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
		&& (presentationFreezeExStyle & WS_EX_APPWINDOW) == 0,
		"leaving whiteboard restores presentation overlay style");

	for (const auto role : roles)
	{
		if (role != WindowRole::DisplayObserver)
			check(service.Show(role), "show before hide all");
	}
	check(service.HideAllUserWindows(), "hide all user windows");
	for (const auto role : roles)
	{
		const HWND hwnd = service.Handle(role);
		check(hwnd && IsWindow(hwnd), "hide all preserves window lifecycle");
		if (role != WindowRole::DisplayObserver)
			check(!IsWindowVisible(hwnd), "hide all removes visible ui");
	}

	Inkeys::Message::Message expected{};
	expected.message = WM_MOUSEMOVE;
	expected.x = 17;
	expected.y = 29;
	expected.lbutton = true;
	check(service.Enqueue(WindowRole::Bar, expected), "message enqueue");
	Inkeys::Message::Message actual{};
	check(service.TryGet(WindowRole::Bar, actual, Inkeys::Message::Filter::Mouse)
		&& actual.message == expected.message
		&& actual.x == expected.x && actual.y == expected.y
		&& actual.lbutton, "message roundtrip");
	check(service.UnbindMessages(WindowRole::Bar), "message unbind on owner thread");
	check(service.BindMessages(WindowRole::Bar), "message bind on owner thread");
	// 绑定 HWND 后队列也可能包含原生 window 消息，只断言目标类别的清除语义。
	service.Clear(WindowRole::Bar);
	check(service.Enqueue(WindowRole::Bar, expected)
		&& service.Clear(WindowRole::Bar, Inkeys::Message::Filter::Mouse) == 1
		&& !service.TryGet(WindowRole::Bar, actual, Inkeys::Message::Filter::Mouse),
		"message clear");

	// 基础 owner 链存在时只能按逆序动态销毁；UI popup 可独立销毁。
	check(!service.Destroy(WindowRole::Freeze), "reject middle owner destruction");
	check(service.Destroy(WindowRole::Bar) && !IsWindow(bar), "destroy leaf on owner thread");
	auto replacementBar = makeSpec(WindowRole::Bar, L"ReplacementBar");
	check(service.Create(std::move(replacementBar))
		&& IsWindow(service.Handle(WindowRole::Bar)), "create leaf on owner thread");
	check(service.Destroy(WindowRole::Setting) && !IsWindow(setting),
		"destroy setting on owner thread");
	check(!service.SetSettingOwnedByDrawpad(true),
		"setting owner command rejects missing setting handle");

	std::array<HWND, static_cast<std::size_t>(WindowRole::Count)> handles{};
	for (const auto role : roles)
		handles[static_cast<std::size_t>(role)] = service.Handle(role);
	service.StopAndJoin();
	check(!service.Running() && !service.AllReady(), "stopped state");
	for (const auto role : roles)
	{
		const auto index = static_cast<std::size_t>(role);
		check(service.Handle(role) == nullptr && !service.Ready(role)
			&& service.OwnerThreadId(role) == 0 && !IsWindow(handles[index]),
			"reverse lifecycle cleanup");
	}

	std::vector<WindowSpec> incompleteSpecs;
	incompleteSpecs.push_back(makeSpec(WindowRole::Setting, L"IncompleteSetting"));
	check(service.Start(std::move(incompleteSpecs)), "start setting without drawpad");
	check(!service.SetSettingOwnedByDrawpad(true),
		"setting owner command rejects missing drawpad");
	check(service.SetSettingOwnedByDrawpad(false),
		"setting detach remains idempotent without drawpad");
	service.StopAndJoin();

	std::vector<WindowSpec> incompleteChainSpecs;
	incompleteChainSpecs.push_back(makeSpec(WindowRole::Freeze, L"IncompleteFreeze"));
	incompleteChainSpecs.push_back(makeSpec(WindowRole::Drawpad, L"IncompleteDrawpad"));
	check(!service.Start(std::move(incompleteChainSpecs)),
		"static drawpad rejects missing presentation owner");

	std::vector<WindowSpec> dynamicChainSpecs;
	dynamicChainSpecs.push_back(makeSpec(WindowRole::MagnifierHost, L"DynamicMagnifier"));
	dynamicChainSpecs.push_back(makeSpec(WindowRole::Freeze, L"DynamicFreeze"));
	check(service.Start(std::move(dynamicChainSpecs)), "start dynamic drawpad chain");
	auto orphanDynamicDrawpad = makeSpec(WindowRole::Drawpad, L"OrphanDynamicDrawpad");
	check(!service.Create(std::move(orphanDynamicDrawpad)),
		"dynamic drawpad rejects missing presentation owner");
	auto dynamicPresentation = makeSpec(
		WindowRole::DrawpadPresentation, L"DynamicPresentation");
	auto dynamicDrawpad = makeSpec(WindowRole::Drawpad, L"DynamicDrawpad");
	check(service.Create(std::move(dynamicPresentation))
		&& service.Create(std::move(dynamicDrawpad)),
		"create dynamic drawpad surfaces");
	check(GetWindow(service.Handle(WindowRole::DrawpadPresentation), GW_OWNER) ==
		service.Handle(WindowRole::Freeze)
		&& GetWindow(service.Handle(WindowRole::Drawpad), GW_OWNER) ==
		service.Handle(WindowRole::DrawpadPresentation),
		"dynamic drawpad surface owner chain");
	service.StopAndJoin();

	// stop 后可以重新创建新的 owner threads 和 channels。
	std::vector<WindowSpec> restartSpecs;
	restartSpecs.push_back(makeSpec(WindowRole::Bar, L"RestartBar"));
	check(service.Start(std::move(restartSpecs))
		&& service.Running() && service.Ready(WindowRole::Bar), "restart");
	service.StopAndJoin();
	failures += RunWindowExitVisibilityTests();
	failures += RunSettingCaptureTests();
	return failures;
}
