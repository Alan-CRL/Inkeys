#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <vector>

#include "../Inkeys/Inkeys/Window/Window.CommandCompletion.h"
#include "../Inkeys/Inkeys/Window/Window.CanvasPolicy.h"

import Inkeys.Window;

namespace
{
	constexpr UINT kSettingOwnerProbe = WM_APP + 74;
	LRESULT CALLBACK SettingOwnerProbeProc(HWND hwnd, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		if (message == kSettingOwnerProbe)
		{
			auto* service = reinterpret_cast<Inkeys::Window::Service*>(lParam);
			return service->SetSettingOwnedByDrawpad(wParam != 0);
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
	auto settingSpec = makeSpec(WindowRole::Setting, L"Setting");
	settingSpec.windowProc = SettingOwnerProbeProc;
	settingSpec.style = WS_POPUP;
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
	check((settingStyle & (WS_POPUP | WS_CLIPCHILDREN)) ==
		(WS_POPUP | WS_CLIPCHILDREN)
		&& (settingStyle & (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
			WS_MAXIMIZEBOX | WS_SYSMENU)) == 0,
		"setting fixed borderless style");
	check((settingExStyle & WS_EX_APPWINDOW) != 0
		&& (settingExStyle & (WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) == 0,
		"setting app ex-style");
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
	whiteboardMinimizeAllowed.store(false);
	check(!service.MinimizeWhiteboardWindowGroup() && !service.WhiteboardWindowGroupMinimized(),
		"PPT policy rejects whiteboard minimization");
	whiteboardMinimizeAllowed.store(true);
	check(service.MinimizeWhiteboardWindowGroup(), "minimize whiteboard window group");
	check(service.WhiteboardWindowGroupMinimized(), "minimize publishes minimized state");
	check(service.Show(WindowRole::PptBottomLeft) && service.Show(WindowRole::Freeze),
		"stale show requests are safely consumed");
	check(IsIconic(freeze) && !IsWindowVisible(drawpad)
		&& !IsWindowVisible(pptBottomLeft) && !IsWindowVisible(pptBottomRight)
		&& IsWindowVisible(bar), "minimize hides canvas and pagination while preserving toolbar");
	check(service.RestoreWhiteboardWindowGroup(), "restore whiteboard window group");
	check(!service.WhiteboardWindowGroupMinimized() && IsWindowVisible(freeze) && IsWindowVisible(drawpad)
		&& !IsWindowVisible(drawpadPresentation)
		&& IsWindowVisible(pptBottomLeft) && IsWindowVisible(pptBottomRight)
		&& IsWindowVisible(bar), "restore returns the prior whiteboard visible state");
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
