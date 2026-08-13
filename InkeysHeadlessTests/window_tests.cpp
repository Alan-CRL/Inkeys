#include <windows.h>

#include <array>
#include <atomic>
#include <iostream>
#include <string>
#include <vector>

import Inkeys.Window;

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
	specs.push_back(makeSpec(WindowRole::Drawpad, L"Drawpad"));
	specs.push_back(makeSpec(WindowRole::PptBottomLeft, L"PptBottomLeft"));
	specs.push_back(makeSpec(WindowRole::PptBottomRight, L"PptBottomRight"));
	specs.push_back(makeSpec(WindowRole::PptMiddleLeft, L"PptMiddleLeft"));
	specs.push_back(makeSpec(WindowRole::PptMiddleRight, L"PptMiddleRight"));
	specs.push_back(makeSpec(WindowRole::PptExitShow, L"PptExitShow"));
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
		WindowRole::Drawpad,
		WindowRole::PptBottomLeft,
		WindowRole::PptBottomRight,
		WindowRole::PptMiddleLeft,
		WindowRole::PptMiddleRight,
		WindowRole::PptExitShow,
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
		&& service.OwnerThreadId(WindowRole::Drawpad) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptBottomLeft) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptBottomRight) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptMiddleLeft) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptMiddleRight) == overlayThread
		&& service.OwnerThreadId(WindowRole::PptExitShow) == overlayThread
		&& service.OwnerThreadId(WindowRole::Bar) == overlayThread
		&& service.OwnerThreadId(WindowRole::DisplayObserver) == overlayThread,
		"single overlay owner thread");
	check(service.OwnerThreadId(WindowRole::Setting) != overlayThread,
		"dedicated setting owner thread");

	const HWND magnifierHost = service.Handle(WindowRole::MagnifierHost);
	const HWND magnifierChild = service.Handle(WindowRole::MagnifierChild);
	const HWND freeze = service.Handle(WindowRole::Freeze);
	const HWND drawpad = service.Handle(WindowRole::Drawpad);
	const HWND pptBottomLeft = service.Handle(WindowRole::PptBottomLeft);
	const HWND pptBottomRight = service.Handle(WindowRole::PptBottomRight);
	const HWND pptMiddleLeft = service.Handle(WindowRole::PptMiddleLeft);
	const HWND pptMiddleRight = service.Handle(WindowRole::PptMiddleRight);
	const HWND pptExitShow = service.Handle(WindowRole::PptExitShow);
	const HWND bar = service.Handle(WindowRole::Bar);
	const HWND setting = service.Handle(WindowRole::Setting);
	const HWND observer = service.Handle(WindowRole::DisplayObserver);
	check(service.OverlayRoot() == magnifierHost, "overlay root");
	check(GetParent(magnifierChild) == magnifierHost, "magnifier child parent");
	check(GetWindow(freeze, GW_OWNER) == magnifierHost, "freeze owner");
	check(GetWindow(drawpad, GW_OWNER) == freeze, "drawpad owner");
	check(GetWindow(pptBottomLeft, GW_OWNER) == drawpad
		&& GetWindow(pptBottomRight, GW_OWNER) == drawpad
		&& GetWindow(pptMiddleLeft, GW_OWNER) == drawpad
		&& GetWindow(pptMiddleRight, GW_OWNER) == drawpad
		&& GetWindow(pptExitShow, GW_OWNER) == drawpad, "five ppt drawpad owner");
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
	check(!(GetWindowLongPtrW(freeze, GWL_EXSTYLE) & WS_EX_TOPMOST)
		&& !(GetWindowLongPtrW(drawpad, GWL_EXSTYLE) & WS_EX_TOPMOST)
		&& !(GetWindowLongPtrW(pptBottomLeft, GWL_EXSTYLE) & WS_EX_TOPMOST)
		&& !(GetWindowLongPtrW(bar, GWL_EXSTYLE) & WS_EX_TOPMOST),
		"non-root has no independent topmost before refresh");
	check(service.RequestTopmostRefresh(), "root-only topmost refresh");
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
