#include <windows.h>

#include <array>
#include <atomic>
#include <iostream>
#include <string>
#include <vector>

import Inkeys.Window;

namespace
{
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
	[[nodiscard]] bool IsContinuousOwnerTree(
		const std::array<HWND, WindowCount>& windows) noexcept
	{
		int first = -1;
		int last = -1;
		std::size_t found = 0;
		int index = 0;
		for (HWND hwnd = GetTopWindow(nullptr); hwnd;
			hwnd = GetWindow(hwnd, GW_HWNDNEXT), ++index)
		{
			if (!ContainsWindow(windows, hwnd)) continue;
			if (first < 0) first = index;
			last = index;
			++found;
		}
		return found == WindowCount && first >= 0
			&& last - first + 1 == static_cast<int>(WindowCount);
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
	specs.push_back(makeSpec(WindowRole::Freeze, L"Freeze"));
	specs.push_back(makeSpec(WindowRole::DrawpadPresentation, L"DrawpadPresentation"));
	specs.push_back(makeSpec(WindowRole::Drawpad, L"Drawpad"));
	specs.push_back(makeSpec(WindowRole::PptBottomLeft, L"PptBottomLeft"));
	specs.push_back(makeSpec(WindowRole::PptBottomRight, L"PptBottomRight"));
	specs.push_back(makeSpec(WindowRole::PptMiddleLeft, L"PptMiddleLeft"));
	specs.push_back(makeSpec(WindowRole::PptMiddleRight, L"PptMiddleRight"));
	specs.push_back(makeSpec(WindowRole::Bar, L"Bar"));
	auto settingSpec = makeSpec(WindowRole::Setting, L"Setting");
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
	check(GetWindow(drawpad, GW_OWNER) == freeze, "drawpad owner");
	check(GetWindow(pptBottomLeft, GW_OWNER) == drawpad
		&& GetWindow(pptBottomRight, GW_OWNER) == drawpad
		&& GetWindow(pptMiddleLeft, GW_OWNER) == drawpad
		&& GetWindow(pptMiddleRight, GW_OWNER) == drawpad, "four ppt drawpad owner");
	check(GetWindow(bar, GW_OWNER) == drawpad, "bar drawpad owner");
	check(GetWindow(setting, GW_OWNER) == nullptr, "setting no owner");
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
	check(service.RequestTopmostRefresh(), "root-only topmost refresh");
	check((GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
		"overlay root receives topmost style");
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
	check(!service.OverlayFullscreen(), "overlay defaults to non-fullscreen");
	check(service.SetOverlayFullscreen(true) && service.OverlayFullscreen()
		&& !(GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST),
		"fullscreen mark persists independently of topmost");
	check(service.SetOverlayFullscreen(false) && !service.OverlayFullscreen(),
		"fullscreen mark clears without restoring topmost");
	check(service.SetOverlayTopmost(true) && service.OverlayTopmost()
		&& (GetWindowLongPtrW(magnifierHost, GWL_EXSTYLE) & WS_EX_TOPMOST),
		"persistent overlay topmost restore");
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
	if (competingTopmost)
		check(IsOwnerTreeAbove(overlayOwnerTree, competingTopmost),
			"bar and promoted ppt remain above competing topmost");
	if (competingTopmost)
		check(DestroyWindow(competingTopmost) != FALSE,
			"destroy hidden competing topmost");

	// 白板模式只把 Freeze 变成 taskbar anchor，其余窗口仍保持 owned popup。
	for (const auto role : {
		WindowRole::MagnifierHost, WindowRole::Freeze,
		WindowRole::DrawpadPresentation, WindowRole::Drawpad,
		WindowRole::PptBottomLeft, WindowRole::PptBottomRight,
		WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight,
		WindowRole::Bar })
		check(service.Show(role), "show whiteboard group member");
	check(service.EnterWhiteboardWindowMode(), "enter whiteboard window mode");
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
	check(service.MinimizeWhiteboardWindowGroup(), "minimize whiteboard window group");
	check(IsIconic(freeze) && !IsWindowVisible(drawpad)
		&& !IsWindowVisible(pptBottomLeft) && !IsWindowVisible(pptBottomRight)
		&& !IsWindowVisible(bar), "minimize hides the complete whiteboard group");
	check(service.RestoreWhiteboardWindowGroup(), "restore whiteboard window group");
	check(IsWindowVisible(freeze) && IsWindowVisible(drawpad)
		&& IsWindowVisible(pptBottomLeft) && IsWindowVisible(pptBottomRight)
		&& IsWindowVisible(bar), "restore returns the prior whiteboard visible state");
	check(service.LeaveWhiteboardWindowMode(), "leave whiteboard window mode");
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

	// stop 后可以重新创建新的 owner threads 和 channels。
	std::vector<WindowSpec> restartSpecs;
	restartSpecs.push_back(makeSpec(WindowRole::Bar, L"RestartBar"));
	check(service.Start(std::move(restartSpecs))
		&& service.Running() && service.Ready(WindowRole::Bar), "restart");
	service.StopAndJoin();
	return failures;
}
