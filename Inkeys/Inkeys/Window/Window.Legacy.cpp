import Inkeys.Helper.CrashHandler;
import Inkeys.Window;
import Inkeys.UI.MessageBox;
import Inkeys.UI.StartupPreview;
import Inkeys.Startup.Progress;

#include "Window.Legacy.hpp"

#include "../../IdtConfiguration.h"
#include "../../IdtI18n.h"
#include "../../IdtI18nKeys.g.h"
#include "../../IdtMain.h"
#include "../../IdtOther.h"
#include "../../Launch/IdtLaunchState.h"

#ifdef MessageBox
#undef MessageBox
#endif

HWND floating_window = nullptr;
HWND drawpad_window = nullptr;
HWND freeze_window = nullptr;
HWND setting_window = nullptr;

IdtWindowsIsVisibleStruct IdtWindowsIsVisible;
bool rtsWait = true;
bool topWindowNow = false;

namespace
{
	struct OverlaySnapshotRole
	{
		Inkeys::Window::WindowRole role;
		const char* name;
	};

	constexpr OverlaySnapshotRole OverlaySnapshotRoles[] = {
		{ Inkeys::Window::WindowRole::MagnifierHost, "MagnifierHost" },
		{ Inkeys::Window::WindowRole::Freeze, "Freeze" },
		{ Inkeys::Window::WindowRole::DrawpadPresentation, "DrawpadPresentation" },
		{ Inkeys::Window::WindowRole::Drawpad, "Drawpad" },
		{ Inkeys::Window::WindowRole::PptBottomLeft, "PptBottomLeft" },
		{ Inkeys::Window::WindowRole::PptBottomRight, "PptBottomRight" },
		{ Inkeys::Window::WindowRole::PptMiddleLeft, "PptMiddleLeft" },
		{ Inkeys::Window::WindowRole::PptMiddleRight, "PptMiddleRight" },
		{ Inkeys::Window::WindowRole::Bar, "Bar" },
	};

	void LogTopmostRefreshSnapshot(
		Inkeys::Window::Service& service, bool recovered) noexcept
	{
		if (!IDTLogger) return;
		const char* transition = recovered ? "recovered" : "failed";
		IDTLogger->warn(
			"[窗口线程][TopWindow] topmost refresh {}: overlayTopmost={} whiteboardMode={}",
			transition, service.OverlayTopmost(), service.WhiteboardWindowMode());
		// 只在失败状态转换时抓取整棵 owner 树，避免周期刷新产生常态日志。
		for (const auto& snapshotRole : OverlaySnapshotRoles)
		{
			const HWND hwnd = service.Handle(snapshotRole.role);
			RECT bounds{};
			if (hwnd) (void)GetWindowRect(hwnd, &bounds);
			const HWND owner = hwnd ? GetWindow(hwnd, GW_OWNER) : nullptr;
			const LONG_PTR exStyle = hwnd
				? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
			IDTLogger->warn(
				"[窗口线程][TopWindow] role={} hwnd=0x{:X} owner=0x{:X} "
				"valid={} visible={} topmost={} bounds=({},{},{},{})",
				snapshotRole.name,
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(hwnd)),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(owner)),
				hwnd && IsWindow(hwnd), hwnd && IsWindowVisible(hwnd),
				(exStyle & WS_EX_TOPMOST) != 0,
				bounds.left, bounds.top, bounds.right, bounds.bottom);
		}
	}
}

HWND GetLastFocusWindow()
{
	return Inkeys::Window::Service::LastFocusWindow();
}

std::wstring GetWindowTitle(HWND hwnd)
{
	const int length = GetWindowTextLengthW(hwnd);
	if (length <= 0) return {};
	std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
	const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
	title.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
	return title;
}

void TopWindow()
{
	auto& service = Inkeys::Window::GetService();
	if (IDTLogger) IDTLogger->info("[窗口线程][TopWindow] 等待覆盖层首帧");
	for (int index = 0; index < 40 && !offSignal; ++index)
	{
		const auto startup = Inkeys::Startup::ActiveSnapshot();
		const auto barState = Inkeys::UI::StartupPreview::GetBarStartupState();
		if (startup.failed
			|| Inkeys::UI::StartupPreview::IsBarStartupFailure(barState))
		{
			if (IDTLogger)
				IDTLogger->warn("[窗口线程][TopWindow] 启动生产者已报告失败，停止等待覆盖层");
			return;
		}
		if (Inkeys::UI::StartupPreview::IsBarStartupReady(barState)
			&& IdtWindowsIsVisible.pptWindow
			&& IdtWindowsIsVisible.freezeWindow)
		{
			IdtWindowsIsVisible.allCompleted = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	if (!IdtWindowsIsVisible.allCompleted)
	{
		if (IDTLogger) IDTLogger->warn("[窗口线程][TopWindow] 等待覆盖层首帧超时");
		// 统一交给主线程的 red-frame + popup 流程；只有无 tracker 时保留旧兜底。
		if (Inkeys::Startup::ReportFailure(0xF201u)
			|| Inkeys::Startup::ActiveSnapshot().failed) return;
		if (LaunchState::warnTry)
		{
			const auto title = I18n::getWOr(
				I18nKey.Dialogs.Common.TipsTitle, L"Inkeys Tips");
			const auto body = I18n::getWOr(
				I18nKey.Dialogs.WindowCreationFailed.Body,
				L"Inkeys exited unexpectedly because a program window could not be created or was blocked. Restart Inkeys and try again. (#5)");
			const auto okLabel = I18n::getWOr(
				I18nKey.Dialogs.Common.OK, L"OK");
			auto request = Inkeys::UI::MessageBox::MakeOkRequest(
				title.c_str(), body.c_str());
			request.language = I18n::languageId();
			request.labels.ok = okLabel.c_str();
			request.ownerlessTopmostAtCreation = true;
			request.fallback.modality =
				Inkeys::UI::MessageBox::SystemModality::System;
			(void)Inkeys::UI::MessageBox::Show(request);
		}
		else
			ShellExecuteW(nullptr, nullptr, GetCurrentExePath().c_str(), L"-WarnTry", nullptr, SW_SHOWNORMAL);
		SetOffSignal(1);
		return;
	}

	(void)service.Show(Inkeys::Window::WindowRole::Freeze);
	// Drawpad 已由 Draw3 首帧握手确认，显隐只能交给选择/内容状态机。
	// PPT 五窗按各自配置决定显隐，TopWindow 只负责唤醒其首次提交。
	(void)service.Show(Inkeys::Window::WindowRole::PptBottomLeft);
	(void)service.Show(Inkeys::Window::WindowRole::PptBottomRight);
	(void)service.Show(Inkeys::Window::WindowRole::PptMiddleLeft);
	(void)service.Show(Inkeys::Window::WindowRole::PptMiddleRight);
	(void)service.Show(Inkeys::Window::WindowRole::Bar);
	while (rtsWait && !offSignal)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	if (!offSignal) CrashHandler::IsSecond(false);

	bool topmostRefreshFailureActive = false;
	while (!offSignal)
	{
		const bool refreshed = service.RequestTopmostRefresh();
		if (!refreshed && !topmostRefreshFailureActive)
		{
			topmostRefreshFailureActive = true;
			LogTopmostRefreshSnapshot(service, false);
		}
		else if (refreshed && topmostRefreshFailureActive)
		{
			topmostRefreshFailureActive = false;
			LogTopmostRefreshSnapshot(service, true);
		}
		int ticks = 300;
		switch (setlist.topSleepTime)
		{
		case 0: ticks = 10; break;
		case 1: ticks = 50; break;
		case 2: ticks = 100; break;
		case 4: ticks = 500; break;
		case 5: ticks = 1000; break;
		case 6: ticks = 3000; break;
		default: break;
		}
		for (int index = 0; index < ticks && !offSignal; ++index)
		{
			if (topWindowNow)
			{
				topWindowNow = false;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}
