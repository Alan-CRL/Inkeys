module;

#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#include <winternl.h>
#include <dwmapi.h>
#include "../UI/Setting/Setting.Layout.h"
#pragma comment(lib, "dwmapi.lib")

#include "../Helper/FailedCleanupDeadline.h"
#include "../Drawing/Draw3/Draw3.DiagnosticOutput.h"
#include "Window.CommandCompletion.h"
#include "Window.ZOrderPolicy.h"
#include "Window.SafetyOverride.h"
#include "Window.ShellFullscreen.h"
#include "Window.CanvasPolicy.h"
#include "Window.MessageDispatch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Inkeys.Window;

namespace
{
	using Inkeys::Window::WindowRole;
	thread_local std::weak_ptr<Inkeys::Window::Detail::ShellFullscreenState> shellWorkerForOwner;
	constexpr std::size_t RoleCount = static_cast<std::size_t>(WindowRole::Count);

	[[nodiscard]] constexpr std::size_t RoleIndex(WindowRole role) noexcept
	{
		return static_cast<std::size_t>(role);
	}

	[[nodiscard]] constexpr bool IsValidRole(WindowRole role) noexcept
	{
		return RoleIndex(role) < RoleCount;
	}

	[[nodiscard]] constexpr bool IsCanvas(WindowRole role) noexcept
	{
		return role == WindowRole::Drawpad || role == WindowRole::DrawpadPresentation;
	}

	[[nodiscard]] constexpr bool IsSetting(WindowRole role) noexcept
	{
		return role == WindowRole::Setting;
	}

	[[nodiscard]] constexpr bool IsDisplayObserver(WindowRole role) noexcept
	{
		return role == WindowRole::DisplayObserver;
	}

	[[nodiscard]] constexpr bool IsPpt(WindowRole role) noexcept
	{
		return role >= WindowRole::PptBottomLeft
			&& role <= WindowRole::PptMiddleRight;
	}

	[[nodiscard]] constexpr bool IsUiPopup(WindowRole role) noexcept
	{
		return IsPpt(role) || role == WindowRole::Bar;
	}

	[[nodiscard]] constexpr int OverlayChainPosition(WindowRole role) noexcept
	{
		switch (role)
		{
		case WindowRole::MagnifierHost: return 0;
		case WindowRole::Freeze: return 1;
		case WindowRole::DrawpadPresentation: return 2;
		case WindowRole::Drawpad: return 3;
		default: return -1;
		}
	}

	[[nodiscard]] const wchar_t* DrawpadSurfaceVisibilityName(
		Inkeys::Window::DrawpadSurfaceVisibility visibility) noexcept
	{
		switch (visibility)
		{
		case Inkeys::Window::DrawpadSurfaceVisibility::Primary: return L"primary";
		case Inkeys::Window::DrawpadSurfaceVisibility::Presentation: return L"presentation";
		case Inkeys::Window::DrawpadSurfaceVisibility::Hidden: return L"hidden";
		default: return L"unknown";
		}
	}

	void WriteWindowDiagnostic(const wchar_t* diagnostic, bool mirror) noexcept
	{
		const DWORD savedError = GetLastError();
		char utf8[2048]{};
		const int count = WideCharToMultiByte(CP_UTF8, 0, diagnostic, -1,
			utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
		// 窗口 owner 只做有界格式化和尝试入队；标准流/debugger 背压不可阻塞安全隐藏。
		if (count > 1) (void)Inkeys::Drawing::Draw3::DiagnosticOutput::Write(
			Inkeys::Drawing::Draw3::DiagnosticOutput::Stream::Error, utf8, static_cast<std::size_t>(count - 1), mirror);
		SetLastError(savedError);
	}

	void LogDrawpadSurfaceState(const wchar_t* eventName,
		Inkeys::Window::DrawpadSurfaceVisibility requested,
		DWORD error, HWND primary, HWND presentation) noexcept
	{
		const auto logWindow = [eventName, requested, error](
			const wchar_t* role, HWND hwnd) noexcept
		{
			const bool valid = hwnd && IsWindow(hwnd);
			RECT bounds{};
			const bool boundsValid = valid && GetWindowRect(hwnd, &bounds);
			const LONG_PTR exStyle = valid ? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
			wchar_t diagnostic[640]{};
			swprintf_s(diagnostic,
				L"[DrawpadSurface] event=%s requested=%s role=%s hwnd=0x%llX "
				L"owner=0x%llX previous=0x%llX valid=%u visible=%u topmost=%u "
				L"layered=%u transparent=%u error=%lu boundsOk=%u bounds=(%ld,%ld,%ld,%ld)\n",
				eventName, DrawpadSurfaceVisibilityName(requested), role,
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(hwnd)),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(
					valid ? GetWindow(hwnd, GW_OWNER) : nullptr)),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(
					valid ? GetWindow(hwnd, GW_HWNDPREV) : nullptr)),
				valid ? 1u : 0u, valid && IsWindowVisible(hwnd) ? 1u : 0u,
				(exStyle & WS_EX_TOPMOST) ? 1u : 0u,
				(exStyle & WS_EX_LAYERED) ? 1u : 0u,
				(exStyle & WS_EX_TRANSPARENT) ? 1u : 0u, error,
				boundsValid ? 1u : 0u,
				bounds.left, bounds.top, bounds.right, bounds.bottom);
			WriteWindowDiagnostic(diagnostic, false);
		};
		logWindow(L"Drawpad", primary);
		logWindow(L"DrawpadPresentation", presentation);
	}

	[[nodiscard]] const wchar_t* DefaultClassName(WindowRole role) noexcept
	{
		switch (role)
		{
		case WindowRole::MagnifierHost: return L"Inkeys.Window.MagnifierHost";
		case WindowRole::MagnifierChild: return L"Inkeys.Window.MagnifierChild";
		case WindowRole::Freeze: return L"Inkeys.Window.Freeze";
		case WindowRole::DrawpadPresentation: return L"Inkeys.Window.DrawpadPresentation";
		case WindowRole::Drawpad: return L"Inkeys.Window.Drawpad";
		case WindowRole::PptBottomLeft: return L"Inkeys.Window.PptBottomLeft";
		case WindowRole::PptBottomRight: return L"Inkeys.Window.PptBottomRight";
		case WindowRole::PptMiddleLeft: return L"Inkeys.Window.PptMiddleLeft";
		case WindowRole::PptMiddleRight: return L"Inkeys.Window.PptMiddleRight";
		case WindowRole::Bar: return L"Inkeys.Window.Bar";
		case WindowRole::Setting: return L"Inkeys.Window.Setting";
		case WindowRole::DisplayObserver: return L"Inkeys.Window.DisplayObserver";
		default: return L"Inkeys.Window.Unknown";
		}
	}

	[[nodiscard]] LRESULT CALLBACK DefaultWindowProc(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
		if (taskbarCreated && message == taskbarCreated)
			if (const auto state = shellWorkerForOwner.lock()) state->Invalidate();
		// 白板锚点收到系统最小化/恢复消息时，统一协调整个 owned window 组。
		// 该过程运行在 Window Service owner thread，Service 内部会直接执行命令，避免自锁。
		if (message == WM_SYSCOMMAND || message == WM_SIZE)
		{
			auto& service = Inkeys::Window::GetService();
			const bool whiteboardAnchor = service.WhiteboardWindowMode()
				&& service.Handle(WindowRole::Freeze) == hwnd;
			if (whiteboardAnchor)
			{
				if (message == WM_SYSCOMMAND)
				{
					const UINT command = static_cast<UINT>(wParam & 0xfff0u);
					if (command == SC_MINIMIZE)
					{
						(void)service.MinimizeWhiteboardWindowGroup();
						return 0;
					}
					else if (command == SC_RESTORE)
					{
						if (service.RestoreWhiteboardWindowGroup()) return 0;
					}
				}
				else if (wParam == SIZE_MINIMIZED)
				{
					if (service.MinimizeWhiteboardWindowGroup()) return 0;
					// ShowWindow 可绕过 SC_MINIMIZE；业务拒绝后立即撤销系统已发生的最小化。
					// SIZE_RESTORED 只读取未最小化的组状态，不会再次调用 ShowWindow。
					if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
					return 0;
				}
				else if (wParam == SIZE_RESTORED)
				{
					if (service.RestoreWhiteboardWindowGroup()) return 0;
				}
			}
		}
		if (message == WM_CLOSE)
		{
			if (Inkeys::Window::GetService().DispatchWhiteboardClose(hwnd)) return 0;
			DestroyWindow(hwnd);
			return 0;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

namespace Inkeys::Window
{
	namespace
	{
		Service processService{4096};
	}

	class Service::Impl
	{
	public:
		explicit Impl(std::size_t capacity) : messageCapacity_(capacity) {}

		~Impl()
		{
			Stop();
		}

		[[nodiscard]] bool Start(std::vector<WindowSpec> specs,
			Shutdown::FailedCleanupSignal failedCleanup)
		{
			std::scoped_lock lifecycleLock(lifecycleMutex_);
			if (shutdownRequested_.load(std::memory_order_acquire))
				return false;
			if (running_.load(std::memory_order_acquire))
				return false;

			try
			{
				ResetState();
				for (auto& spec : specs)
				{
					if (!IsValidRole(spec.role) || specs_[RoleIndex(spec.role)].has_value())
						return false;
					if (spec.width <= 0 || spec.height <= 0)
						return false;
					configured_[RoleIndex(spec.role)].store(true, std::memory_order_relaxed);
					specs_[RoleIndex(spec.role)] = std::move(spec);
				}

				overlayEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				settingEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				std::promise<bool> overlayPromise;
				std::promise<bool> settingPromise;
				auto overlayReady = overlayPromise.get_future();
				auto settingReady = settingPromise.get_future();
				running_.store(true, std::memory_order_release);

				shellFullscreen_ = Detail::StartShellFullscreen();
			overlayThread_ = std::jthread(
					[this, failedCleanup, promise = std::move(overlayPromise)](std::stop_token token) mutable
					{
						RunGroup(false, token, std::move(promise), failedCleanup);
					});
				settingThread_ = std::jthread(
					[this, failedCleanup, promise = std::move(settingPromise)](std::stop_token token) mutable
					{
						RunGroup(true, token, std::move(promise), failedCleanup);
					});

				const bool started = overlayReady.get() && settingReady.get();
				if (!started)
				{
					failedCleanup.BeginKnownFailure();
					StopUnlocked();
					return false;
				}
				return true;
			}
			catch (...)
			{
				// thread/promise 的部分启动失败仍先建立截止，再等待已创建 owner。
				failedCleanup.BeginKnownFailure();
				StopUnlocked();
				return false;
			}
		}

		void Stop() noexcept
		{
			std::scoped_lock lifecycleLock(lifecycleMutex_);
			StopUnlocked();
		}

		[[nodiscard]] bool Running() const noexcept
		{
			return running_.load(std::memory_order_acquire);
		}

		void BeginShutdown() noexcept
		{
			// 正式退出意图一经接受就不可撤销；此处不得等待可能已卡住的 HWND owner。
			shutdownRequested_.store(true, std::memory_order_release);
		}

		[[nodiscard]] HWND Handle(WindowRole role) const noexcept
		{
			return Record(role) ? Record(role)->hwnd.load(std::memory_order_acquire) : nullptr;
		}

		[[nodiscard]] bool Ready(WindowRole role) const noexcept
		{
			const auto* record = Record(role);
			return record && record->ready.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool AllReady() const noexcept
		{
			return OverlayReady();
		}

		[[nodiscard]] bool OverlayReady() const noexcept
		{
			constexpr WindowRole overlayRoles[] = {
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
				WindowRole::DisplayObserver,
			};
			for (const auto role : overlayRoles)
			{
				const auto index = RoleIndex(role);
				if (configured_[index].load(std::memory_order_acquire) &&
					!records_[index].ready.load(std::memory_order_acquire))
					return false;
			}
			return running_.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool SettingReady() const noexcept
		{
			return Ready(WindowRole::Setting);
		}

		[[nodiscard]] DWORD OwnerThreadId(WindowRole role) const noexcept
		{
			const auto* record = Record(role);
			return record ? record->threadId.load(std::memory_order_acquire) : 0;
		}

		[[nodiscard]] HWND OverlayRoot() const noexcept
		{
			constexpr WindowRole order[] = {
				WindowRole::MagnifierHost,
				WindowRole::Freeze,
				WindowRole::Drawpad,
				WindowRole::Bar,
			};
			for (const auto role : order)
			{
				if (const auto hwnd = Handle(role))
					return hwnd;
			}
			return nullptr;
		}

		[[nodiscard]] std::wstring Title(WindowRole role) const
		{
			const auto hwnd = Handle(role);
			if (!hwnd)
				return {};
			const int length = GetWindowTextLengthW(hwnd);
			if (length <= 0)
				return {};
			std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
			const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
			title.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
			return title;
		}

		[[nodiscard]] bool Show(WindowRole role)
		{
			// 已加入画布链的设置在显示前同步抬升根；选择态保留普通窗口语义。
			if (IsSetting(role) && !PrepareSettingZOrder(true)) return false;
			return Submit(role, CommandType::Show);
		}

		[[nodiscard]] bool Create(WindowSpec spec)
		{
			if (!IsValidRole(spec.role) || spec.width <= 0 || spec.height <= 0)
				return false;
			Command command;
			command.type = CommandType::Create;
			command.role = spec.role;
			command.spec = std::move(spec);
			return Submit(std::move(command));
		}

		[[nodiscard]] bool Destroy(WindowRole role)
		{
			return Submit(role, CommandType::Destroy);
		}

		[[nodiscard]] bool Hide(WindowRole role)
		{
			return Submit(role, CommandType::Hide);
		}

		[[nodiscard]] bool HideAllUserWindows()
		{
			// 两组窗口分别在自己的 owner thread 隐藏，避免跨线程直接操作 HWND。
			const bool overlayHidden = Submit(WindowRole::Bar, CommandType::HideAll);
			const bool settingHidden = Submit(WindowRole::Setting, CommandType::HideAll);
			return overlayHidden && settingHidden;
		}

		[[nodiscard]] bool RequestHideAllUserWindows()
		{
			const bool overlayQueued = SubmitNoWait(WindowRole::Bar, CommandType::HideAll);
			const bool settingQueued = SubmitNoWait(WindowRole::Setting, CommandType::HideAll);
			return overlayQueued && settingQueued;
		}

		[[nodiscard]] bool SetBounds(WindowRole role, const RECT& bounds)
		{
			Command command;
			command.type = CommandType::SetBounds;
			command.role = role;
			command.bounds = bounds;
			return Submit(std::move(command));
		}

		[[nodiscard]] bool SetDrawpadSurfaceVisibility(
			DrawpadSurfaceVisibility visibility,
			std::function<bool()> stillDesired)
		{
			Command command;
			command.type = CommandType::SetDrawpadSurfaceVisibility;
			command.role = WindowRole::Drawpad;
			command.drawpadVisibility = visibility;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
		}

		[[nodiscard]] bool SetClickThrough(WindowRole role, bool enabled,
			std::function<bool()> stillDesired)
		{
			Command command;
			command.type = CommandType::SetClickThrough;
			command.role = role;
			command.enabled = enabled;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
		}

		[[nodiscard]] bool SetSettingOwnedByDrawpad(bool enabled)
		{
			Command command;
			command.type = CommandType::SetSettingOwnedByDrawpad;
			command.role = WindowRole::Setting;
			command.enabled = enabled;
			const HWND setting = Handle(WindowRole::Setting);
			const HWND previousOwner = setting ? GetWindow(setting, GW_OWNER) : nullptr;
			if (!Submit(std::move(command))) return false;
			if (!enabled) return true;
			if (!PrepareSettingZOrder(previousOwner != Handle(WindowRole::Drawpad)))
			{
				// 层级准备失败时在 Setting 线程恢复原 owner，避免留下半完成的归属。
				if (previousOwner != Handle(WindowRole::Drawpad))
				{
					Command rollback;
					rollback.type = CommandType::SetSettingOwnedByDrawpad;
					rollback.role = WindowRole::Setting;
					rollback.enabled = previousOwner != nullptr;
					rollback.settingOwner = previousOwner;
					(void)Submit(std::move(rollback));
				}
				return false;
			}
			return setting && setting == Handle(WindowRole::Setting)
				&& IsWindow(setting) && GetWindow(setting, GW_OWNER) == Handle(WindowRole::Drawpad)
				&& Handle(WindowRole::Drawpad) != nullptr;
		}

		[[nodiscard]] bool SettingAboveDrawpads(HWND setting) const noexcept
		{
			// 这只是局部 above 谓词：不把遍历到的三方窗口误认成 owner 组成员。
			const auto snapshot = CaptureGroupZOrder(setting);
			return snapshot && snapshot->Above(9, 3) && snapshot->Above(9, 2);
		}

		[[nodiscard]] bool PrepareSettingZOrder(bool forceRefresh)
		{
			const HWND setting = Handle(WindowRole::Setting);
			if (!setting || !IsWindow(setting)) return false;
			const HWND owner = GetWindow(setting, GW_OWNER);
			if (!owner) return true;
			if (owner != Handle(WindowRole::Drawpad) || !IsWindow(owner)) return false;
			const HWND root = OverlayRoot();
			const bool topmost = overlayTopmost_.load(std::memory_order_acquire)
				&& !whiteboardWindowMode_.load(std::memory_order_acquire);
			const auto matches = [&]() noexcept
			{
				return setting == Handle(WindowRole::Setting) && IsWindow(setting)
					&& owner == Handle(WindowRole::Drawpad) && IsWindow(owner)
					&& GetWindow(setting, GW_OWNER) == owner
					&& root && root == OverlayRoot() && IsWindow(root)
					&& topmost == (overlayTopmost_.load(std::memory_order_acquire)
						&& !whiteboardWindowMode_.load(std::memory_order_acquire))
					&& ((GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) == topmost
					&& ((GetWindowLongPtrW(setting, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) == topmost
					&& SettingAboveDrawpads(setting);
			};
			if (forceRefresh || !matches())
			{
				// 根操作可能同步向 owned 窗口发消息；设置线程不能等待 overlay 线程。
				if (GetCurrentThreadId() == settingThreadId_.load(std::memory_order_acquire))
					return false;
				if (!RequestTopmostRefresh()) return false;
				// 根已置顶时，新接入的 Setting 仍可能留在画布下方；由自身线程补齐带内顺序。
				if (!Submit(WindowRole::Setting, CommandType::RaiseOwnedSetting)) return false;
			}
			return matches();
		}

		[[nodiscard]] bool SetExtendedStyleFlags(WindowRole role, DWORD setMask, DWORD clearMask)
		{
			if ((setMask & clearMask) != 0) return false;
			Command command;
			command.type = CommandType::SetExtendedStyleFlags;
			command.role = role;
			command.setMask = setMask;
			command.clearMask = clearMask;
			return Submit(std::move(command));
		}

		[[nodiscard]] bool SetWhiteboardMode(bool enabled, std::function<bool()> stillDesired)
		{
			Command command;
			command.role = WindowRole::Freeze;
			command.type = CommandType::SetWhiteboardWindowMode;
			command.whiteboardMode = enabled;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
		}

		[[nodiscard]] bool WhiteboardWindowMode() const noexcept
		{
			return whiteboardWindowMode_.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool WhiteboardWindowGroupMinimized() const noexcept
		{
			return whiteboardGroupMinimized_.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool DispatchWhiteboardClose(HWND hwnd) noexcept
		{
			if (!hwnd || Handle(WindowRole::Freeze) != hwnd
				|| OwnerThreadId(WindowRole::Freeze) != GetCurrentThreadId()) return false;
			const auto& spec = records_[RoleIndex(WindowRole::Freeze)].activeSpec;
			// 离场后迟到的 WM_CLOSE 仍须保留共享 owner；不重复请求已结束的白板事务。
			return spec ? DispatchWhiteboardCloseRequest(true, WhiteboardWindowMode(), spec->whiteboardCloseRequested)
				: WhiteboardWindowMode();
		}

		[[nodiscard]] bool MinimizeWhiteboardWindowGroup()
		{
			return Submit(WindowRole::Freeze, CommandType::MinimizeWhiteboardGroup);
		}

		[[nodiscard]] bool RestoreWhiteboardWindowGroup()
		{
			return Submit(WindowRole::Freeze, CommandType::RestoreWhiteboardGroup);
		}

		[[nodiscard]] bool RequestWhiteboardActivation(std::function<bool()> stillDesired)
		{
			Command command;
			command.role = WindowRole::Freeze;
			command.type = CommandType::ActivateWhiteboard;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
		}

		[[nodiscard]] bool CancelPointerCapture()
		{
			return Submit(WindowRole::Freeze, CommandType::CancelPointerCapture);
		}

		[[nodiscard]] bool RequestTopmostRefresh(DWORD timeoutMilliseconds = 1500)
		{
			Command command;
			command.role = WindowRole::MagnifierHost;
			command.type = CommandType::RefreshTopmost;
			if (timeoutMilliseconds != INFINITE)
				command.deadline = std::chrono::steady_clock::now()
					+ std::chrono::milliseconds(timeoutMilliseconds);
			const bool refreshed = Submit(std::move(command));
			if (refreshed) NotifyTopmostRefresh();
			return refreshed;
		}

		void NotifyTopmostRefresh()
		{
			std::function<void()> callback;
			{
				std::scoped_lock lock(topmostObserverMutex_);
				callback = topmostRefreshObserver_;
				if (callback) ++topmostObserverActiveCalls_;
			}
			if (callback)
			{
				topmostObserverExecuting_ = true;
				try { callback(); }
				catch (...) {}
				topmostObserverExecuting_ = false;
				{
					std::scoped_lock lock(topmostObserverMutex_);
					--topmostObserverActiveCalls_;
				}
				topmostObserverCondition_.notify_all();
			}
		}

		void SetTopmostRefreshObserver(std::function<void()> callback)
		{
			std::unique_lock lock(topmostObserverMutex_);
			topmostRefreshObserver_ = {};
			if (!topmostObserverExecuting_)
				(void)topmostObserverCondition_.wait_for(lock,
					std::chrono::milliseconds(1500), [this]
					{ return topmostObserverActiveCalls_ == 0; });
			topmostRefreshObserver_ = std::move(callback);
		}

			[[nodiscard]] bool SetOverlayTopmost(bool topmost, std::function<bool()> stillDesired)
			{
				if (!running_.load(std::memory_order_acquire))
				{
					if (stillDesired && !stillDesired()) return false;
					overlayTopmost_.store(topmost, std::memory_order_release);
					return true;
				}
				Command command;
				command.role = WindowRole::Freeze;
				command.type = CommandType::SetOverlayTopmost;
				command.enabled = topmost;
				command.stillDesired = std::move(stillDesired);
				const bool refreshed = Submit(std::move(command));
				if (refreshed) NotifyTopmostRefresh();
				return refreshed;
			}

			[[nodiscard]] bool OverlayTopmost() const noexcept
			{
				return overlayTopmost_.load(std::memory_order_acquire);
			}

			[[nodiscard]] bool SetOverlayFullscreen(bool fullscreen, std::function<bool()> stillDesired)
			{
				if (!running_.load(std::memory_order_acquire))
				{
					if (stillDesired && !stillDesired()) return false;
					overlayFullscreen_.store(fullscreen, std::memory_order_release);
					return true;
				}
				Command command;
				command.role = WindowRole::Freeze;
				command.type = CommandType::SetOverlayFullscreen;
				command.enabled = fullscreen;
				command.stillDesired = std::move(stillDesired);
				const bool refreshed = Submit(std::move(command));
				if (refreshed) NotifyTopmostRefresh();
				return refreshed;
			}

			[[nodiscard]] bool OverlayFullscreen() const noexcept
			{
				return overlayFullscreen_.load(std::memory_order_acquire);
			}

		[[nodiscard]] bool PromotePptWindow(WindowRole role)
		{
			if (!IsPpt(role)) return false;
			return Submit(role, CommandType::PromotePpt);
		}

		[[nodiscard]] bool BindMessages(WindowRole role, const Message::BindOptions& options)
		{
			Command command;
			command.type = CommandType::BindMessages;
			command.role = role;
			command.bindOptions = options;
			return Submit(std::move(command));
		}

		[[nodiscard]] bool UnbindMessages(WindowRole role)
		{
			return Submit(role, CommandType::UnbindMessages);
		}

		[[nodiscard]] Message::Channel* Channel(WindowRole role) noexcept
		{
			auto* record = Record(role);
			return record && record->channel ? record->channel.get() : nullptr;
		}

		[[nodiscard]] const Message::Channel* Channel(WindowRole role) const noexcept
		{
			const auto* record = Record(role);
			return record && record->channel ? record->channel.get() : nullptr;
		}

	private:
		enum class CommandType
		{
			Create,
			Destroy,
			Show,
			Hide,
			HideAll,
			SetDrawpadSurfaceVisibility,
			SetBounds,
			SetClickThrough,
			SetSettingOwnedByDrawpad,
			RaiseOwnedSetting,
			SetExtendedStyleFlags,
			SetWhiteboardWindowMode,
			SetOverlayTopmost,
			SetOverlayFullscreen,
			MinimizeWhiteboardGroup,
			RestoreWhiteboardGroup,
			ActivateWhiteboard,
			CancelPointerCapture,
			RefreshTopmost,
			PromotePpt,
			BindMessages,
			UnbindMessages,
			Count,
		};

		static constexpr std::size_t CommandCount =
			static_cast<std::size_t>(CommandType::Count);

		struct Command
		{
			CommandType type = CommandType::Show;
			WindowRole role = WindowRole::Bar;
			RECT bounds{};
			bool enabled = false;
			std::optional<HWND> settingOwner;
			DrawpadSurfaceVisibility drawpadVisibility =
				DrawpadSurfaceVisibility::Hidden;
			std::function<bool()> stillDesired;
			DWORD setMask = 0;
			DWORD clearMask = 0;
			bool whiteboardMode = false;
			Message::BindOptions bindOptions{};
			std::optional<WindowSpec> spec;
			std::shared_ptr<Detail::CommandCompletion> completion;
			std::uint64_t intentVersion = 0;
			Detail::CommandDeadline deadline;
		};

		struct CommandQueue
		{
			std::mutex mutex;
			std::deque<Command> commands;
		};

		struct CanvasWindowPolicy
		{
			Detail::CanvasRegion originalRegion;
			HANDLE originalNonRude = nullptr;
			bool initialized = false;
			std::optional<Detail::CanvasPolicyKey> applied;
			std::optional<bool> recoveryVisible;
			bool recoveryIconic = false;
		};

		struct WindowRecord
		{
			std::atomic<HWND> hwnd = nullptr;
			std::atomic<std::uint64_t> generation = 0;
			std::atomic_bool ready = false;
			std::atomic<DWORD> threadId = 0;
			std::unique_ptr<Message::Channel> channel;
			std::optional<WindowSpec> activeSpec;
			bool messagesBound = false;
			bool classRegistered = false;
			bool lifecycleActive = false;
			std::wstring className;
			CanvasWindowPolicy canvasPolicy;
			bool clickThroughFramePending = false; // 仅本窗口 owner 使用，不缓存期望值。
			bool clickThroughApplying = false;
		};

		[[nodiscard]] WindowRecord* Record(WindowRole role) noexcept
		{
			return IsValidRole(role) ? &records_[RoleIndex(role)] : nullptr;
		}

		[[nodiscard]] const WindowRecord* Record(WindowRole role) const noexcept
		{
			return IsValidRole(role) ? &records_[RoleIndex(role)] : nullptr;
		}

		[[nodiscard]] static bool ShouldReportCommandFailure(
			CommandType type) noexcept
		{
			return type == CommandType::Show || type == CommandType::Hide ||
				type == CommandType::SetDrawpadSurfaceVisibility ||
				type == CommandType::SetBounds || type == CommandType::SetClickThrough ||
				type == CommandType::SetSettingOwnedByDrawpad ||
				type == CommandType::RaiseOwnedSetting ||
				type == CommandType::RefreshTopmost ||
				type == CommandType::PromotePpt;
		}

		[[nodiscard]] static const wchar_t* CommandName(
			CommandType type) noexcept
		{
			switch (type)
			{
			case CommandType::Show: return L"Show";
			case CommandType::Hide: return L"Hide";
			case CommandType::SetDrawpadSurfaceVisibility:
				return L"SetDrawpadSurfaceVisibility";
			case CommandType::SetBounds: return L"SetBounds";
			case CommandType::SetClickThrough: return L"SetClickThrough";
			case CommandType::SetSettingOwnedByDrawpad:
				return L"SetSettingOwnedByDrawpad";
			case CommandType::RaiseOwnedSetting: return L"RaiseOwnedSetting";
			case CommandType::RefreshTopmost: return L"RefreshTopmost";
			case CommandType::PromotePpt: return L"PromotePpt";
			default: return L"Other";
			}
		}

		void ReportCommandResult(const Command& command, bool succeeded,
			DWORD error, HWND hwnd) noexcept
		{
			if (!ShouldReportCommandFailure(command.type) ||
				!IsValidRole(command.role)) return;
			auto& failureActive = commandFailureActive_[
				static_cast<std::size_t>(command.type)][RoleIndex(command.role)];
			const bool recovered = succeeded && failureActive;
			if (succeeded)
			{
				if (!recovered) return;
				failureActive = false;
			}
			else
			{
				if (failureActive) return;
				failureActive = true;
			}

			if (command.type == CommandType::SetClickThrough)
			{
				// 穿透失败/恢复不能在窗口 owner 同步写标准流，否则会阻塞下一次样式收敛。
				using namespace Inkeys::Drawing::Draw3;
				DiagnosticOutput::Line(DiagnosticOutput::Stream::Error, true)
					<< "[Window.ClickThrough] result=" << (recovered ? "recovered" : "failed")
					<< " role=" << static_cast<unsigned>(command.role) << " error=" << error
					<< " requested=" << command.enabled << " actual="
					<< (hwnd && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0) << '\n';
				return;
			}
			RECT bounds{};
			if (hwnd) (void)GetWindowRect(hwnd, &bounds);
			const HWND owner = hwnd ? GetWindow(hwnd, GW_OWNER) : nullptr;
			const LONG_PTR exStyle = hwnd
				? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
			wchar_t diagnostic[640]{};
			swprintf_s(diagnostic,
				L"Inkeys.Window command %s: operation=%s role=%u error=%lu "
				L"hwnd=0x%llX owner=0x%llX valid=%d visible=%d topmost=%d "
				L"bounds=(%ld,%ld,%ld,%ld)\n",
				recovered ? L"recovered" : L"failed", CommandName(command.type),
				static_cast<unsigned>(command.role),
				error,
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(hwnd)),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(owner)),
				hwnd && IsWindow(hwnd) ? 1 : 0,
				hwnd && IsWindowVisible(hwnd) ? 1 : 0,
				(exStyle & WS_EX_TOPMOST) != 0 ? 1 : 0,
				bounds.left, bounds.top, bounds.right, bounds.bottom);
			WriteWindowDiagnostic(diagnostic, true);
		}

			void ResetState()
			{
				overlayTopmost_.store(true, std::memory_order_release);
				overlayFullscreen_.store(false, std::memory_order_release);
				whiteboardWindowMode_.store(false, std::memory_order_release);
				whiteboardGroupMinimized_.store(false, std::memory_order_release);
				whiteboardVisibleBeforeMinimize_.fill(false);
			canvasPolicyDirty_ = false;
			canvasPolicyApplying_ = false;
			canvasPolicyRetry_ = false;
			commandApplying_.fill(false);
			safetyCleanupRetry_.fill(false);
			safetyOverrides_.fill({});
			for (auto& recovery : commandRecovery_) recovery.reset();
			for (auto& spec : specs_)
				spec.reset();
			for (auto& configured : configured_)
				configured.store(false, std::memory_order_relaxed);
			for (auto& record : records_)
			{
				record.hwnd.store(nullptr, std::memory_order_relaxed);
				record.ready.store(false, std::memory_order_relaxed);
				record.threadId.store(0, std::memory_order_relaxed);
				record.channel.reset();
				record.activeSpec.reset();
				record.messagesBound = false;
				record.classRegistered = false;
				record.lifecycleActive = false;
				record.className.clear();
				record.canvasPolicy = {};
				record.clickThroughFramePending = false;
				record.clickThroughApplying = false;
			}
			for (auto& commandFailures : commandFailureActive_)
				commandFailures.fill(false);
			{
				std::scoped_lock lock(overlayCommands_.mutex, settingCommands_.mutex);
				overlayCommands_.commands.clear();
				settingCommands_.commands.clear();
			}
		}

		void StopUnlocked() noexcept
		{
			if (!running_.exchange(false, std::memory_order_acq_rel) &&
				!overlayThread_.joinable() && !settingThread_.joinable() &&
				!overlayEvent_ && !settingEvent_)
				return;

			overlayThread_.request_stop();
			settingThread_.request_stop();
			if (overlayEvent_) SetEvent(overlayEvent_);
			if (settingEvent_) SetEvent(settingEvent_);
			if (overlayThread_.joinable()) overlayThread_.join();
			if (settingThread_.joinable()) settingThread_.join();

			Detail::StopShellFullscreen(shellFullscreen_);
			shellFullscreen_.reset();
			FailPendingCommands(overlayCommands_);
			FailPendingCommands(settingCommands_);
			if (overlayEvent_)
			{
				CloseHandle(overlayEvent_);
				overlayEvent_ = nullptr;
			}
			if (settingEvent_)
			{
				CloseHandle(settingEvent_);
				settingEvent_ = nullptr;
			}
		}

		static void FailPendingCommands(CommandQueue& queue) noexcept
		{
			std::deque<Command> pending;
			{
				std::scoped_lock lock(queue.mutex);
				pending.swap(queue.commands);
			}
			for (auto& command : pending)
			{
				if (!command.completion) continue;
				command.completion->Cancel();
				command.completion->Retired();
			}
		}

			void RunGroup(bool settingGroup, std::stop_token token, std::promise<bool> readyPromise,
				Shutdown::FailedCleanupSignal failedCleanup)
			{
				const DWORD threadId = GetCurrentThreadId();
				if (!settingGroup) shellWorkerForOwner = shellFullscreen_;
				(settingGroup ? settingThreadId_ : overlayThreadId_).store(
					threadId, std::memory_order_release);
				const HRESULT comInit = settingGroup ? E_FAIL : CoInitializeEx(
					nullptr, COINIT_APARTMENTTHREADED);
				const bool comOwned = !settingGroup &&
					(comInit == S_OK || comInit == S_FALSE);
				bool created = false;
				try
				{
					created = CreateGroup(settingGroup, threadId, failedCleanup);
					if (!created) failedCleanup.BeginKnownFailure();
					readyPromise.set_value(created);
				}
				catch (...)
				{
					failedCleanup.BeginKnownFailure();
					created = false;
					try { readyPromise.set_value(false); }
					catch (...) {}
				}
				if (!created)
				{
					failedCleanup.BeginKnownFailure();
					DestroyGroup(settingGroup);
					(settingGroup ? settingThreadId_ : overlayThreadId_).store(
						0, std::memory_order_release);
					if (comOwned) CoUninitialize();
					return;
				}

			HANDLE eventHandle = settingGroup ? settingEvent_ : overlayEvent_;
			std::stop_callback stopCallback(token, [eventHandle]
				{
					if (eventHandle) SetEvent(eventHandle);
				});
			CommandQueue& queue = settingGroup ? settingCommands_ : overlayCommands_;
			MSG message{};
			while (!token.stop_requested())
			{
				// 仅失败后的画布策略使用有界重试；正常静止仍保持无限等待。
				const auto now = std::chrono::steady_clock::now();
				const bool needsRecovery = commandRecovery_[settingGroup ? 1 : 0].has_value() || safetyCleanupRetry_[settingGroup ? 1 : 0];
				const DWORD timeout = needsRecovery ? 250 : (!settingGroup && canvasPolicyRetry_
					&& !shutdownRequested_.load(std::memory_order_acquire)
					? (canvasPolicyRetryAt_ <= now ? 0 : static_cast<DWORD>(
						std::chrono::duration_cast<std::chrono::milliseconds>(canvasPolicyRetryAt_ - now).count() + 1))
					: INFINITE);
				const DWORD waitResult = eventHandle
					? MsgWaitForMultipleObjectsEx(
						1, &eventHandle, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE)
					: MsgWaitForMultipleObjectsEx(
						0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

				if (eventHandle && waitResult == WAIT_OBJECT_0)
				{
					ResetEvent(eventHandle);
					DrainCommands(queue);
				}
				else if (waitResult == WAIT_FAILED)
				{
					break;
				}

				while (!token.stop_requested() &&
					PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
				{
					if (message.message == WM_QUIT)
						break;
					TranslateMessage(&message);
					const MessageDispatch::Scope dispatchScope(message);
					DispatchMessageW(&message);
				}
				if (!eventHandle)
					DrainCommands(queue);
				if (commandRecovery_[settingGroup ? 1 : 0])
				{
					commandApplying_[settingGroup ? 1 : 0] = true;
					(void)RetryCommandRecovery(settingGroup);
					commandApplying_[settingGroup ? 1 : 0] = false;
				}
				if (safetyCleanupRetry_[settingGroup ? 1 : 0])
				{
					commandApplying_[settingGroup ? 1 : 0] = true;
					(void)RetrySafetyCleanup(settingGroup);
					commandApplying_[settingGroup ? 1 : 0] = false;
				}
				if (!settingGroup && canvasPolicyRetry_ && !token.stop_requested()
					&& !shutdownRequested_.load(std::memory_order_acquire)
					&& std::chrono::steady_clock::now() >= canvasPolicyRetryAt_)
					(void)EnsureCanvasPolicy(whiteboardWindowMode_.load(std::memory_order_acquire));
			}

				FailPendingCommands(queue);
				if (auto& recovery = commandRecovery_[settingGroup ? 1 : 0]; recovery)
				{
					if (recovery->completion) { recovery->completion->Cancel(); recovery->completion->Retired(); }
					recovery.reset();
				}
				if (!settingGroup)
				{
					overlayFullscreen_.store(false, std::memory_order_release);
					if (const HWND root = OverlayRoot())
						ApplyOverlayFullscreen(root);
				}
				DestroyGroup(settingGroup);
				if (!settingGroup) shellWorkerForOwner.reset();
				(settingGroup ? settingThreadId_ : overlayThreadId_).store(
					0, std::memory_order_release);
				if (comOwned) CoUninitialize();
			}

		[[nodiscard]] bool CreateGroup(bool settingGroup, DWORD threadId,
			Shutdown::FailedCleanupSignal failedCleanup)
		{
			constexpr WindowRole overlayCreationOrder[] = {
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
				WindowRole::DisplayObserver,
			};

			if (settingGroup)
			{
				const auto& spec = specs_[RoleIndex(WindowRole::Setting)];
				if (!spec || CreateStartupWindowFor(*spec, nullptr, threadId, failedCleanup)) return true;
				if (spec->optional)
				{
					configured_[RoleIndex(WindowRole::Setting)].store(
						false, std::memory_order_release);
					return true;
				}
				failedCleanup.BeginKnownFailure();
				return false;
			}

			HWND owner = nullptr;
			for (const auto role : overlayCreationOrder)
			{
				const auto& spec = specs_[RoleIndex(role)];
				if (!spec) continue;
				if (role == WindowRole::MagnifierChild && !Handle(WindowRole::MagnifierHost))
				{
					configured_[RoleIndex(role)].store(false, std::memory_order_release);
					continue;
				}
				HWND roleOwner = owner;
				if (role == WindowRole::MagnifierChild)
					roleOwner = Handle(WindowRole::MagnifierHost);
				else if (role == WindowRole::DisplayObserver)
					roleOwner = HWND_MESSAGE;
				else if (role == WindowRole::DrawpadPresentation)
				{
					roleOwner = Handle(WindowRole::Freeze);
					if (!roleOwner)
					{
						if (!spec->optional)
						{
							failedCleanup.BeginKnownFailure();
							return false;
						}
						configured_[RoleIndex(role)].store(false, std::memory_order_release);
						continue;
					}
				}
				else if (role == WindowRole::Drawpad)
				{
					// 主 Drawpad 挂到 presentation surface 下，Bar/PPT 才会稳定高于两块画布。
					roleOwner = Handle(WindowRole::DrawpadPresentation);
					if (!roleOwner)
					{
						if (!spec->optional)
						{
							failedCleanup.BeginKnownFailure();
							return false;
						}
						configured_[RoleIndex(role)].store(false, std::memory_order_release);
						continue;
					}
				}
				else if (IsUiPopup(role))
					roleOwner = Handle(WindowRole::Drawpad);
				if (!CreateStartupWindowFor(*spec, roleOwner, threadId, failedCleanup))
				{
					if (!spec->optional)
					{
						failedCleanup.BeginKnownFailure();
						return false;
					}
					configured_[RoleIndex(role)].store(false, std::memory_order_release);
					continue;
				}
				if (role != WindowRole::MagnifierChild &&
					role != WindowRole::DisplayObserver && !IsUiPopup(role) &&
					role != WindowRole::DrawpadPresentation)
					owner = Handle(role);
			}
			return true;
		}

		[[nodiscard]] bool CreateStartupWindowFor(const WindowSpec& spec, HWND owner,
			DWORD threadId, Shutdown::FailedCleanupSignal failedCleanup)
		{
			if (!spec.optional || !failedCleanup.HasPublisher())
				return CreateWindowFor(spec, owner, threadId, failedCleanup);
			// optional 的失败只约束本次 rollback，真 join 后才继续下一普通创建。
			Shutdown::FailedCleanupDeadline optionalCleanup(failedCleanup.Publisher());
			optionalCleanup.PrepareOrFatal();
			const auto optionalSignal = optionalCleanup.Signal();
			bool created = false;
			try
			{
				created = CreateWindowFor(spec, owner, threadId, optionalSignal);
			}
			catch (...)
			{
				// 未处理异常将使整个 group 失败，后续 DestroyGroup 继承同一原 tick。
				failedCleanup.BeginKnownFailure(optionalSignal.BeginKnownFailure());
				throw;
			}
			optionalCleanup.CompleteOrFatal();
			return created;
		}

		[[nodiscard]] bool CreateWindowFor(
			const WindowSpec& spec,
			HWND owner,
			DWORD threadId,
			Shutdown::FailedCleanupSignal failedCleanup = {})
		{
			auto& record = records_[RoleIndex(spec.role)];
			// 每次创建都绑定自己的生命周期描述，动态重建不会误用旧回调。
			record.activeSpec = spec;
			if (spec.beforeCreate)
			{
				bool prepared = false;
				try { prepared = spec.beforeCreate(); }
				catch (...) { failedCleanup.BeginKnownFailure(); }
				if (!prepared)
				{
					RollbackCreation(record, failedCleanup);
					return false;
				}
				record.lifecycleActive = true;
			}
			if (!record.channel)
				record.channel = std::make_unique<Message::Channel>(messageCapacity_);
			if (spec.messageCallback)
				record.channel->SetCallback(spec.messageCallback);
			else
				record.channel->ClearCallback();
			record.className = spec.className.empty() ? DefaultClassName(spec.role) : spec.className;

			const HINSTANCE instance = GetModuleHandleW(nullptr);
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.style = spec.classStyle;
			windowClass.lpfnWndProc = spec.windowProc ? spec.windowProc : DefaultWindowProc;
			windowClass.hInstance = instance;
			windowClass.hIcon = spec.largeIcon ? spec.largeIcon : LoadIconW(nullptr, IDI_APPLICATION);
			windowClass.hIconSm = spec.smallIcon ? spec.smallIcon : windowClass.hIcon;
			windowClass.hCursor = spec.cursor ? spec.cursor : LoadCursorW(nullptr, IDC_ARROW);
			windowClass.hbrBackground = spec.background;
			windowClass.lpszClassName = record.className.c_str();
			const bool systemClass = spec.role == WindowRole::MagnifierChild;
			if (!systemClass)
			{
				if (RegisterClassExW(&windowClass))
					record.classRegistered = true;
				else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
				{
					RollbackCreation(record, failedCleanup);
					return false;
				}
			}

			DWORD style = spec.style;
			DWORD exStyle = spec.exStyle;
			int windowX = spec.x;
			int windowY = spec.y;
			int windowWidth = spec.width;
			int windowHeight = spec.height;
			if (IsSetting(spec.role))
			{
				// Windows 拥有 sizing frame，Setting 只在客户区绘制 caption。
				style = SettingWindowStyle;
				exStyle = (exStyle | SettingWindowExStyle) &
					~(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
				owner = nullptr;

				// 自绘标题栏已属于客户区，只外扩系统 sizing frame，不重复预留原生 caption。
				const RECT frame = QuerySettingClientFrameInsets();
				const RECT adjusted{ -frame.left, -frame.top,
					spec.width + frame.right, spec.height + frame.bottom };
				{
					windowWidth = adjusted.right - adjusted.left;
					windowHeight = adjusted.bottom - adjusted.top;
					const POINT center{
						spec.x + spec.width / 2,
						spec.y + spec.height / 2 };
					windowX = center.x - windowWidth / 2;
					windowY = center.y - windowHeight / 2;

					const HMONITOR monitor = MonitorFromPoint(
						center, MONITOR_DEFAULTTONEAREST);
					MONITORINFO monitorInfo{ sizeof(monitorInfo) };
					if (monitor && GetMonitorInfoW(monitor, &monitorInfo))
					{
						const int workWidth = monitorInfo.rcWork.right
							- monitorInfo.rcWork.left;
						const int workHeight = monitorInfo.rcWork.bottom
							- monitorInfo.rcWork.top;
						if (windowWidth > workWidth) windowWidth = workWidth;
						if (windowHeight > workHeight) windowHeight = workHeight;
						windowX = center.x - windowWidth / 2;
						windowY = center.y - windowHeight / 2;
						if (windowX < monitorInfo.rcWork.left)
							windowX = monitorInfo.rcWork.left;
						if (windowY < monitorInfo.rcWork.top)
							windowY = monitorInfo.rcWork.top;
						if (windowX + windowWidth > monitorInfo.rcWork.right)
							windowX = monitorInfo.rcWork.right - windowWidth;
						if (windowY + windowHeight > monitorInfo.rcWork.bottom)
							windowY = monitorInfo.rcWork.bottom - windowHeight;
					}
				}
			}
			else if (IsDisplayObserver(spec.role))
			{
				style = 0;
				exStyle = 0;
				owner = HWND_MESSAGE;
			}
			else if (spec.role == WindowRole::MagnifierChild)
			{
				style = style ? style : WS_CHILD | WS_CLIPCHILDREN;
				exStyle |= WS_EX_NOACTIVATE;
			}
			else
			{
				style = style ? style : WS_POPUP | WS_CLIPCHILDREN;
				exStyle = exStyle ? exStyle :
					WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
			}

			// NonRude 必须在首次显示前设置，不能让调用者的 WS_VISIBLE 提前揭开画布。
			if (IsCanvas(spec.role)) style &= ~WS_VISIBLE;
			const HWND hwnd = CreateWindowExW(
				exStyle,
				record.className.c_str(),
				spec.title.c_str(),
				style,
				windowX,
				windowY,
				windowWidth,
				windowHeight,
				owner,
				nullptr,
				instance,
				nullptr);
			if (!hwnd)
			{
				const DWORD createError = GetLastError();
				if (spec.role == WindowRole::Drawpad ||
					spec.role == WindowRole::DrawpadPresentation)
					LogDrawpadSurfaceState(L"window-create-failed",
						DrawpadSurfaceVisibility::Hidden, createError,
						Handle(WindowRole::Drawpad),
						Handle(WindowRole::DrawpadPresentation));
				failedCleanup.BeginKnownFailure();
				wchar_t diagnostic[256]{};
				swprintf_s(diagnostic, L"Inkeys.Window create failed: role=%u error=%lu class=%s\n",
					static_cast<unsigned>(spec.role), createError, record.className.c_str());
				WriteWindowDiagnostic(diagnostic, true);
				RollbackCreation(record, failedCleanup);
				return false;
			}
			if (!record.lifecycleActive)
				record.lifecycleActive = true;

			record.generation.store(Detail::NextWindowGeneration.fetch_add(1, std::memory_order_acq_rel) + 1, std::memory_order_release);
			(void)SetPropW(hwnd, Detail::WindowGenerationProperty, reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(record.generation.load(std::memory_order_acquire))));
			record.hwnd.store(hwnd, std::memory_order_release);
			record.threadId.store(threadId, std::memory_order_release);
			if (IsSetting(spec.role) && UsesLegacySettingFrame())
			{
				// 隐藏态先按标准 caption 建立 HWND 自有系统菜单，再去掉旧 Aero 原生标题栏。
				// 保留 sizing/min/max/sysmenu；后续统一 FRAMECHANGED 校正，不改变现代 DWM 样式。
				SetLastError(ERROR_SUCCESS);
				const HMENU menu = GetSystemMenu(hwnd, FALSE);
				DWORD frameError = menu ? ERROR_SUCCESS : GetLastError();
				if (menu)
				{
					const LONG_PTR frameStyle = GetWindowLongPtrW(hwnd, GWL_STYLE)
						& ~static_cast<LONG_PTR>(WS_CAPTION);
					SetLastError(ERROR_SUCCESS);
					if (!SetWindowLongPtrW(hwnd, GWL_STYLE, frameStyle))
						frameError = GetLastError();
				}
				if (!menu || frameError != ERROR_SUCCESS)
				{
					if (frameError == ERROR_SUCCESS) frameError = ERROR_INVALID_MENU_HANDLE;
					wchar_t diagnostic[128]{};
					swprintf_s(diagnostic, L"Inkeys.Window legacy setting frame failed: error=%lu\n", frameError);
					OutputDebugStringW(diagnostic);
					std::fwprintf(stderr, L"%s", diagnostic);
					RollbackCreation(record, failedCleanup);
					return false;
				}
			}
			if (spec.bindMessages && record.channel)
			{
				record.messagesBound = record.channel->Bind(hwnd, spec.messageOptions);
				if (!record.messagesBound)
				{
					RollbackCreation(record, failedCleanup);
					return false;
				}
			}

			if (IsCanvas(spec.role))
			{
				auto& policy = record.canvasPolicy;
				policy = {};
				if (!ReadCanvasRegion(hwnd, policy.originalRegion))
				{
					RollbackCreation(record, failedCleanup);
					return false;
				}
				policy.originalNonRude = GetPropW(hwnd, Detail::CanvasNonRudeProperty);
				policy.initialized = true;
				if (!EnsureCanvasPolicy(whiteboardWindowMode_.load(std::memory_order_acquire),
					false, &record))
				{
					RollbackCreation(record, failedCleanup);
					return false;
				}
			}
			if (IsSetting(spec.role) || spec.role == WindowRole::Freeze)
			{
				// 外框校正只用于设置，Freeze 保持原来的无边框尺寸。
				if (IsSetting(spec.role))
				{
					// 创建后重新计算一次自定义非客户区，避免首帧保留系统 caption 的旧几何。
					SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
						SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
					RECT actualOuter{}, actualClient{};
					if (GetWindowRect(hwnd, &actualOuter) && GetClientRect(hwnd, &actualClient))
					{
						// DWM 的真实描边在创建后才可查询；仍按原请求中心和当前工作区只校正一次。
						MONITORINFO monitorInfo{ sizeof(monitorInfo) };
						const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
						if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
							monitorInfo.rcWork = {};
						const auto corrected = Inkeys::UI::Setting::ResolveWindowFrameCreationCorrection(
							{ spec.x, spec.y, spec.x + spec.width, spec.y + spec.height },
							{ static_cast<int>(actualOuter.left), static_cast<int>(actualOuter.top),
								static_cast<int>(actualOuter.right), static_cast<int>(actualOuter.bottom) },
							{ static_cast<int>(actualClient.right - actualClient.left),
								static_cast<int>(actualClient.bottom - actualClient.top) },
							{ static_cast<int>(monitorInfo.rcWork.left), static_cast<int>(monitorInfo.rcWork.top),
								static_cast<int>(monitorInfo.rcWork.right), static_cast<int>(monitorInfo.rcWork.bottom) });
						if (corrected.Width() != actualOuter.right - actualOuter.left
							|| corrected.Height() != actualOuter.bottom - actualOuter.top)
							SetWindowPos(hwnd, nullptr, corrected.left, corrected.top,
								corrected.Width(), corrected.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
					}
				}
				SendMessageW(hwnd, WM_SETICON, ICON_BIG,
					reinterpret_cast<LPARAM>(windowClass.hIcon));
				SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
					reinterpret_cast<LPARAM>(windowClass.hIconSm));
			}
			ShowWindow(hwnd, spec.visible &&
				!shutdownRequested_.load(std::memory_order_acquire)
				? (IsSetting(spec.role) ? SW_SHOW : SW_SHOWNOACTIVATE)
				: SW_HIDE);
			if (spec.role == WindowRole::Drawpad ||
				spec.role == WindowRole::DrawpadPresentation)
			{
				const auto initialSurface = !spec.visible
					? DrawpadSurfaceVisibility::Hidden
					: spec.role == WindowRole::Drawpad
						? DrawpadSurfaceVisibility::Primary
						: DrawpadSurfaceVisibility::Presentation;
				LogDrawpadSurfaceState(L"window-created", initialSurface,
					ERROR_SUCCESS, Handle(WindowRole::Drawpad),
					Handle(WindowRole::DrawpadPresentation));
			}
			record.ready.store(true, std::memory_order_release);
			if (spec.created)
			{
				try { spec.created(hwnd); }
				catch (...)
				{
					RollbackCreation(record, failedCleanup);
					return false;
				}
			}
			return true;
		}

		void DestroyGroup(bool settingGroup) noexcept
		{
			constexpr WindowRole overlayDestructionOrder[] = {
				WindowRole::Bar,
				WindowRole::PptMiddleRight,
				WindowRole::PptMiddleLeft,
				WindowRole::PptBottomRight,
				WindowRole::PptBottomLeft,
				WindowRole::Drawpad,
				WindowRole::DrawpadPresentation,
				WindowRole::Freeze,
				WindowRole::MagnifierChild,
				WindowRole::MagnifierHost,
				WindowRole::DisplayObserver,
			};
			if (settingGroup)
			{
				DestroyWindowFor(WindowRole::Setting);
				return;
			}
			for (const auto role : overlayDestructionOrder)
				DestroyWindowFor(role);
		}

		void DestroyWindowFor(WindowRole role) noexcept
		{
			auto& record = records_[RoleIndex(role)];
			const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
			record.ready.store(false, std::memory_order_release);
			if (hwnd && record.messagesBound && record.channel)
			{
				(void)record.channel->Unbind(hwnd);
				record.messagesBound = false;
			}
			if ((role == WindowRole::Drawpad ||
				role == WindowRole::DrawpadPresentation) && hwnd)
				LogDrawpadSurfaceState(L"window-destroying",
					DrawpadSurfaceVisibility::Hidden, ERROR_SUCCESS,
					Handle(WindowRole::Drawpad),
					Handle(WindowRole::DrawpadPresentation));
			CleanupCanvasPolicy(record);
			if (hwnd && IsWindow(hwnd))
				DestroyWindow(hwnd);
			if (role == WindowRole::Drawpad ||
				role == WindowRole::DrawpadPresentation)
				LogDrawpadSurfaceState(L"window-destroyed",
					DrawpadSurfaceVisibility::Hidden, ERROR_SUCCESS,
					role == WindowRole::Drawpad ? nullptr : Handle(WindowRole::Drawpad),
					role == WindowRole::DrawpadPresentation
						? nullptr : Handle(WindowRole::DrawpadPresentation));
			record.hwnd.store(nullptr, std::memory_order_release);
			record.clickThroughFramePending = false;
			record.threadId.store(0, std::memory_order_release);
			if (record.channel)
			{
				record.channel->Shutdown();
				record.channel.reset();
			}
			if (record.classRegistered && !record.className.empty())
				UnregisterClassW(record.className.c_str(), GetModuleHandleW(nullptr));
			record.classRegistered = false;
			CleanupLifecycle(record);
			record.activeSpec.reset();
			record.className.clear();
		}

		static void RollbackCreation(WindowRecord& record,
			Shutdown::FailedCleanupSignal failedCleanup) noexcept
		{
			// 已知失败必须先激活，再进入 channel/DestroyWindow/lifecycle 清理。
			failedCleanup.BeginKnownFailure();
			record.ready.store(false, std::memory_order_release);
			const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
			if (hwnd && record.messagesBound && record.channel)
			{
				(void)record.channel->Unbind(hwnd);
				record.messagesBound = false;
			}
			CleanupCanvasPolicy(record);
			if (hwnd && IsWindow(hwnd))
			{
				(void)RemovePropW(hwnd, Detail::WindowGenerationProperty);
				(void)DestroyWindow(hwnd);
			}
			record.hwnd.store(nullptr, std::memory_order_release);
			record.clickThroughFramePending = false;
			record.threadId.store(0, std::memory_order_release);
			if (record.channel)
			{
				record.channel->Shutdown();
				record.channel.reset();
			}
			if (record.classRegistered && !record.className.empty())
				(void)UnregisterClassW(
					record.className.c_str(), GetModuleHandleW(nullptr));
			record.classRegistered = false;
			CleanupLifecycle(record);
			record.activeSpec.reset();
			record.className.clear();
		}

		static void CleanupLifecycle(WindowRecord& record) noexcept
		{
			if (!record.lifecycleActive) return;
			record.lifecycleActive = false;
			if (!record.activeSpec || !record.activeSpec->destroyed) return;
			try { record.activeSpec->destroyed(); }
			catch (...) {}
		}

		[[nodiscard]] bool Submit(WindowRole role, CommandType type)
		{
			Command command;
			command.type = type;
			command.role = role;
			return Submit(std::move(command));
		}

		[[nodiscard]] bool SubmitNoWait(WindowRole role, CommandType type)
		{
			Command command;
			command.type = type;
			command.role = role;
			if (!running_.load(std::memory_order_acquire) || !IsValidRole(role))
				return false;
			CommandQueue& queue = IsSetting(role) ? settingCommands_ : overlayCommands_;
			HANDLE eventHandle = IsSetting(role) ? settingEvent_ : overlayEvent_;
			const DWORD ownerThreadId = (IsSetting(role)
				? settingThreadId_ : overlayThreadId_).load(std::memory_order_acquire);
			if (ownerThreadId && ownerThreadId == GetCurrentThreadId())
				return ExecuteTracked(command);
			{
				std::scoped_lock lock(queue.mutex);
				if (!running_.load(std::memory_order_acquire)) return false;
				queue.commands.push_back(std::move(command));
			}
			if (eventHandle) SetEvent(eventHandle);
			return true;
		}

		[[nodiscard]] bool Submit(WindowRole role, CommandType type, bool mode)
		{
			Command command;
			command.type = type;
			command.role = role;
			command.whiteboardMode = mode;
			return Submit(std::move(command));
		}

		[[nodiscard]] std::size_t IntentIndex(CommandType type) const noexcept
		{
			if (type == CommandType::Hide) type = CommandType::Show;
			if (type == CommandType::RestoreWhiteboardGroup) type = CommandType::MinimizeWhiteboardGroup;
			return static_cast<std::size_t>(type);
		}

		[[nodiscard]] bool CommandCurrent(const Command& command) const noexcept
		{
			try
			{
				return (!command.completion || (running_.load(std::memory_order_acquire) && command.completion->Current()))
					&& (!command.intentVersion || command.intentVersion ==
						intentVersions_[IntentIndex(command.type)][RoleIndex(command.role)].load(std::memory_order_acquire))
					&& (!command.stillDesired || command.stillDesired());
			}
			catch (...) { return false; }
		}

	public:
		[[nodiscard]] CommandReceipt LastCommandReceipt() const
		{
			const auto completion = receiptOwner_ == this ? lastReceipt_ : nullptr;
			if (!completion) return {};
			return { [completion] { return static_cast<CommandPhase>(completion->Phase()); },
				[completion] { completion->Cancel(); } };
		}

	private:
		[[nodiscard]] bool Submit(Command command, bool wait = true)
		{
			if (!running_.load(std::memory_order_acquire) || !IsValidRole(command.role)) return false;
			CommandQueue& queue = IsSetting(command.role) ? settingCommands_ : overlayCommands_;
			HANDLE eventHandle = IsSetting(command.role) ? settingEvent_ : overlayEvent_;
			const DWORD ownerThreadId = (IsSetting(command.role)
				? settingThreadId_ : overlayThreadId_).load(std::memory_order_acquire);
			if (ownerThreadId && ownerThreadId == GetCurrentThreadId() && commandApplying_[IsSetting(command.role) ? 1 : 0]
				&& ((command.type == CommandType::MinimizeWhiteboardGroup && WhiteboardWindowGroupMinimized()
					&& IsIconic(Handle(WindowRole::Freeze)))
					|| (command.type == CommandType::RestoreWhiteboardGroup && !WhiteboardWindowGroupMinimized()
						&& !IsIconic(Handle(WindowRole::Freeze)))))
				return true; // 同次 ShowWindow 的 WM_SIZE 回执不是新意图，不能退休外层事务。
			if (!command.deadline) command.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
			command.completion = std::make_shared<Detail::CommandCompletion>(command.deadline);
			receiptOwner_ = this;
			lastReceipt_ = command.completion;
			const auto completion = command.completion;
			{
				std::scoped_lock lock(queue.mutex);
				if (!running_.load(std::memory_order_acquire)) { completion->Cancel(); return false; }
				command.intentVersion = intentVersions_[IntentIndex(command.type)][RoleIndex(command.role)]
					.fetch_add(1, std::memory_order_acq_rel) + 1;
				// 周期刷新只保留最新一份排队责任；在途操作不能伪装成已经取消。
				if (command.type == CommandType::RefreshTopmost)
					for (auto it = queue.commands.begin(); it != queue.commands.end(); )
						if (it->type == CommandType::RefreshTopmost)
						{
							if (it->completion) it->completion->Cancel();
							it = queue.commands.erase(it);
						}
						else ++it;
				if (!ownerThreadId || ownerThreadId != GetCurrentThreadId()) queue.commands.push_back(command);
			}
			// WndProc 重入不能越过尚未退栈的原生写入并提前报告新意图成功。
			if (ownerThreadId && ownerThreadId == GetCurrentThreadId()) return ExecuteTracked(command);
			if (eventHandle) SetEvent(eventHandle);
			return wait ? completion->Wait() : true;
		}

		void DrainCommands(CommandQueue& queue)
		{
			std::deque<Command> commands;
			{
				std::scoped_lock lock(queue.mutex);
				commands.swap(queue.commands);
			}
			for (auto& command : commands) (void)ExecuteTracked(command);
		}

		static constexpr std::array GroupRoles{ WindowRole::MagnifierHost, WindowRole::Freeze,
			WindowRole::DrawpadPresentation, WindowRole::Drawpad, WindowRole::PptBottomLeft,
			WindowRole::PptBottomRight, WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight, WindowRole::Bar };
		using GroupSnapshot = Detail::ZOrderSnapshot<10>;

		[[nodiscard]] std::optional<GroupSnapshot> CaptureGroupZOrder(HWND setting = nullptr) const noexcept
		{
			GroupSnapshot snapshot;
			std::array<std::uint64_t, 10> generations{};
			for (std::size_t i = 0; i < GroupRoles.size(); ++i)
			{
				snapshot.windows[i] = Handle(GroupRoles[i]);
				generations[i] = records_[RoleIndex(GroupRoles[i])].generation.load(std::memory_order_acquire);
			}
			snapshot.windows[9] = setting;
			generations[9] = records_[RoleIndex(WindowRole::Setting)].generation.load(std::memory_order_acquire);
			struct Enumeration
			{
				GroupSnapshot& snapshot;
				std::size_t ordinal = 0;
				bool seen = false;
				bool gap = false;
			} enumeration{ snapshot };
			// EnumWindows 提供有限的一次采样；变化中的全局 GetWindow 链可能形成回路。
			if (!EnumWindows([](HWND hwnd, LPARAM context) -> BOOL
				{
					auto& state = *reinterpret_cast<Enumeration*>(context);
					bool member = false;
					for (std::size_t i = 0; i < state.snapshot.windows.size(); ++i)
						if (state.snapshot.windows[i] == hwnd)
						{
							state.snapshot.ordinal[i] = state.ordinal;
							member = true;
							break;
						}
					if (member)
					{
						if (state.seen && state.gap) state.snapshot.continuous = false;
						state.seen = true;
						state.gap = false;
					}
					else if (state.seen)
					{
						// 允许同进程、已知 owner 树派生的辅助窗穿插；它们不由本策略直接重排。
						bool owned = false;
						DWORD process = 0;
						(void)GetWindowThreadProcessId(hwnd, &process);
						HWND owner = process == GetCurrentProcessId() ? GetWindow(hwnd, GW_OWNER) : nullptr;
						for (unsigned depth = 0; owner && depth < 16 && !owned; ++depth)
						{
							for (const HWND known : state.snapshot.windows) if (known && known == owner) owned = true;
							owner = GetWindow(owner, GW_OWNER);
						}
						if (!owned) state.gap = true;
					}
					++state.ordinal;
					return TRUE;
				}, reinterpret_cast<LPARAM>(&enumeration))) return {};
			for (std::size_t i = 0; i < snapshot.windows.size(); ++i)
			{
				const auto role = i == 9 ? WindowRole::Setting : GroupRoles[i];
				const HWND hwnd = snapshot.windows[i];
				if (!hwnd) continue;
				if (hwnd != Handle(role) || generations[i] != records_[RoleIndex(role)].generation.load(std::memory_order_acquire)
					|| !IsWindow(hwnd) || snapshot.ordinal[i] == Detail::MissingZOrder) return {};
				snapshot.owners[i] = GetWindow(hwnd, GW_OWNER);
				snapshot.topmost[i] = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
			}
			return snapshot;
		}

		[[nodiscard]] std::array<HWND, 10> ExpectedGroupOwners(const GroupSnapshot& snapshot) const noexcept
		{
			std::array<HWND, 10> owners{};
			HWND parent = snapshot.windows[0];
			for (std::size_t i = 1; i <= 3; ++i)
			{
				owners[i] = parent;
				if (snapshot.windows[i]) parent = snapshot.windows[i];
			}
			for (std::size_t i = 4; i <= 8; ++i) owners[i] = snapshot.windows[3];
			owners[9] = snapshot.windows[3];
			return owners;
		}

		[[nodiscard]] bool GroupMatches(const GroupSnapshot& snapshot, bool topmost) const noexcept
		{
			if (!snapshot.Matches(ExpectedGroupOwners(snapshot), topmost)) return false;
			for (std::size_t i = 4; i < 8; ++i)
				if (snapshot.windows[i] && snapshot.windows[8] && !snapshot.Above(8, i)) return false;
			return true;
		}

		[[nodiscard]] bool RefreshOverlayGroup(const Command* command = nullptr, HWND promoted = nullptr)
		{
			const auto current = [&] { return !command || CommandCurrent(*command); };
			const bool topmost = overlayTopmost_.load(std::memory_order_acquire) && !WhiteboardWindowMode();
			const HWND root = OverlayRoot();
			const auto before = CaptureGroupZOrder();
			if (!before || !root || !current() || (promoted && !before->windows[8])) return false;
			std::array<HWND, 4> ppt{};
			std::size_t count = 0;
			for (std::size_t i = 4; i < 8; ++i) if (before->windows[i]) ppt[count++] = before->windows[i];
			std::sort(ppt.begin(), ppt.begin() + count, [&](HWND left, HWND right)
				{
					std::size_t a = 0, b = 0;
					for (std::size_t i = 4; i < 8; ++i) { if (before->windows[i] == left) a = i; if (before->windows[i] == right) b = i; }
					return before->ordinal[a] < before->ordinal[b];
				});
			if (promoted)
				for (std::size_t i = 0; i < count; ++i) if (ppt[i] == promoted)
				{
					std::rotate(ppt.begin(), ppt.begin() + i, ppt.begin() + i + 1);
					break;
				}
			const auto expected = ExpectedGroupOwners(*before);
			constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
			// 两种 band 都核验完整基础组；失败只进行有限轮次，不转成正常态忙循环。
			for (unsigned attempt = 0; attempt < 2 && current(); ++attempt)
			{
				for (std::size_t i = 0; i < GroupRoles.size(); ++i)
				{
					const HWND member = before->windows[i];
					if (!member) continue;
					if (!current() || member != Handle(GroupRoles[i]) || !IsWindow(member)) return false;
					if (GetWindow(member, GW_OWNER) != expected[i])
					{
						SetLastError(ERROR_SUCCESS);
						if (!SetWindowLongPtrW(member, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(expected[i]))
							&& GetLastError() != ERROR_SUCCESS) return false;
						if (!current() || GetWindow(member, GW_OWNER) != expected[i]) return false;
					}
				}
				if (!current() || !SetWindowPos(root, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags)) return false;
				if (!current()) return false;
				if (((GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost
					&& !SetWindowPos(root, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags | SWP_NOOWNERZORDER)) return false;
				std::array<HWND, 9> order{ before->windows[0], before->windows[1], before->windows[2], before->windows[3] };
				for (std::size_t i = 0; i < count; ++i) order[4 + i] = ppt[count - i - 1];
				order[8] = before->windows[8];
				for (const HWND member : order)
				{
					if (!member) continue;
					if (!current()) return false;
					if (member != root && ((GetWindowLongPtrW(member, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost)
						if (!SetWindowPos(member, root, 0, 0, 0, 0, flags | SWP_NOOWNERZORDER)) return false;
					// 保留 Win32 对合法 owned Setting/辅助窗的传播合同，不统一禁止 owner 联动。
					if (!current() || !SetWindowPos(member, HWND_TOP, 0, 0, 0, 0, flags)) return false;
				}
				const auto after = CaptureGroupZOrder();
				bool ordered = after && GroupMatches(*after, topmost);
				for (std::size_t i = 1; ordered && i < count; ++i)
				{
					std::size_t a = 0, b = 0;
					for (std::size_t j = 4; j < 8; ++j) { if (after->windows[j] == ppt[i-1]) a = j; if (after->windows[j] == ppt[i]) b = j; }
					ordered = after->Above(a, b);
				}
				if (ordered && current()) { ApplyOverlayFullscreen(root); return true; }
			}
			SetLastError(ERROR_GEN_FAILURE);
			return false;
		}

		[[nodiscard]] static bool ReadCanvasRegion(HWND hwnd, Detail::CanvasRegion& region) noexcept
		{
			region.Reset(CreateRectRgn(0, 0, 0, 0));
			if (!region.Get()) return false;
			// GetWindowRgn 对“未设 region”也返回 ERROR；空 region 的返回值是 NULLREGION。
			if (GetWindowRgn(hwnd, region.Get()) == ERROR) region.Reset();
			return true;
		}

		[[nodiscard]] static bool SetCanvasRegion(HWND hwnd, HRGN source) noexcept
		{
			Detail::CanvasRegion copy;
			if (!Detail::CopyCanvasRegion(source, copy)) return false;
			if (!SetWindowRgn(hwnd, copy.Get(), TRUE)) return false;
			// USER32 成功后接管 HRGN，快照仍由本方持有，回滚时另复制一份。
			(void)copy.Release();
			Detail::CanvasRegion actual;
			return ReadCanvasRegion(hwnd, actual) && (source
				? actual.Get() && EqualRgn(source, actual.Get())
				: !actual.Get());
		}

		[[nodiscard]] static bool SetCanvasNonRude(HWND hwnd, HANDLE value) noexcept
		{
			if (GetPropW(hwnd, Detail::CanvasNonRudeProperty) == value) return true;
			if (value)
			{
				if (!SetPropW(hwnd, Detail::CanvasNonRudeProperty, value)) return false;
			}
			else (void)RemovePropW(hwnd, Detail::CanvasNonRudeProperty);
			return GetPropW(hwnd, Detail::CanvasNonRudeProperty) == value;
		}

		static void CleanupCanvasPolicy(WindowRecord& record) noexcept
		{
			auto& policy = record.canvasPolicy;
			const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
			if (policy.initialized && hwnd && IsWindow(hwnd)
				&& policy.originalNonRude != reinterpret_cast<HANDLE>(TRUE)
				&& GetPropW(hwnd, Detail::CanvasNonRudeProperty) == reinterpret_cast<HANDLE>(TRUE))
				(void)SetCanvasNonRude(hwnd, policy.originalNonRude);
			policy = {};
		}

		[[nodiscard]] bool EnsureCanvasPolicy(bool whiteboard, bool changeStyle = false,
			WindowRecord* only = nullptr) noexcept
		{
			if (canvasPolicyApplying_) return false;
			struct ApplyingGuard
			{
				bool& active;
				explicit ApplyingGuard(bool& value) noexcept : active(value) { active = true; }
				~ApplyingGuard() { active = false; }
			} applying(canvasPolicyApplying_);
			changeStyle |= canvasPolicyDirty_;
			// SetBounds 的调用者不一定会重发；失败由本 owner 的既有消息循环收敛。
			canvasPolicyRetry_ = true;
			canvasPolicyRetryAt_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
			struct Snapshot
			{
				Detail::CanvasRegion region;
				HANDLE property = nullptr;
				bool visible = false;
				bool iconic = false;
			};
			struct Operations
			{
				Impl& owner;
				bool whiteboard;
				bool changeStyle;
				bool previousMode;
				std::array<WindowRecord*, 2> records{};
				std::array<Detail::CanvasPolicyKey, 2> keys{};
				std::array<Snapshot, 2> snapshots{};
				DWORD error = ERROR_SUCCESS;

				bool Checked(bool result) noexcept
				{
					if (!result && error == ERROR_SUCCESS)
					{
						error = GetLastError();
						if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
					}
					return result;
				}
				bool Capture(std::size_t i) noexcept
				{
					const HWND hwnd = keys[i].hwnd;
					const auto& policy = records[i]->canvasPolicy;
					auto& snapshot = snapshots[i];
					snapshot.property = GetPropW(hwnd, Detail::CanvasNonRudeProperty);
					snapshot.visible = policy.recoveryVisible.value_or(IsWindowVisible(hwnd) != FALSE);
					snapshot.iconic = policy.recoveryVisible ? policy.recoveryIconic : IsIconic(hwnd) != FALSE;
					return Checked(ReadCanvasRegion(hwnd, snapshot.region));
				}
				bool Hide(std::size_t i) noexcept
				{
					ShowWindow(keys[i].hwnd, SW_HIDE);
					return Checked(!IsWindowVisible(keys[i].hwnd));
				}
				bool Apply(std::size_t i) noexcept
				{
					const auto& policy = records[i]->canvasPolicy;
					Detail::CanvasRegion region;
					return Checked(Detail::BuildCanvasRegion(keys[i], policy.originalRegion.Get(), region)
						&& SetCanvasNonRude(keys[i].hwnd,
							Detail::CanvasNonRudeValue(whiteboard, policy.originalNonRude))
						&& SetCanvasRegion(keys[i].hwnd, region.Get()));
				}
				bool ApplyScene() noexcept
				{
					try { return !changeStyle || Checked(owner.ApplyWhiteboardActivationStyle(whiteboard)); }
					catch (...) { return Checked(false); }
				}
				bool RollbackScene() noexcept
				{
					try { return !changeStyle || Checked(owner.ApplyWhiteboardActivationStyle(previousMode)); }
					catch (...) { return Checked(false); }
				}
				bool Restore(std::size_t i) noexcept
				{
					const bool property = SetCanvasNonRude(keys[i].hwnd, snapshots[i].property);
					const bool region = SetCanvasRegion(keys[i].hwnd, snapshots[i].region.Get());
					return Checked(property && region);
				}
				bool RestoreVisibility(std::size_t i) noexcept
				{
					const bool visible = snapshots[i].visible
						&& !owner.shutdownRequested_.load(std::memory_order_acquire)
						&& !owner.SafetyHidden(keys[i].hwnd);
					ShowWindow(keys[i].hwnd, visible
						? (snapshots[i].iconic ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE) : SW_HIDE);
					return Checked((IsWindowVisible(keys[i].hwnd) != FALSE) == visible);
				}
			} operations{ *this, whiteboard, changeStyle,
				whiteboardWindowMode_.load(std::memory_order_acquire) };

			const auto failClosed = [this](DWORD error) noexcept
			{
				// 查询失败时无法证明旧 region 仍适用；成对隐藏并保留显隐意图供 owner 重试。
				for (const auto role : { WindowRole::DrawpadPresentation, WindowRole::Drawpad })
				{
					auto& record = records_[RoleIndex(role)];
					const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
					if (!hwnd || !IsWindow(hwnd)) continue;
					auto& policy = record.canvasPolicy;
					if (!policy.recoveryVisible)
					{
						policy.recoveryVisible = IsWindowVisible(hwnd) != FALSE;
						policy.recoveryIconic = IsIconic(hwnd) != FALSE;
					}
					policy.applied.reset();
				}
				for (const auto role : { WindowRole::DrawpadPresentation, WindowRole::Drawpad })
					if (const HWND hwnd = Handle(role); hwnd && IsWindow(hwnd)) ShowWindow(hwnd, SW_HIDE);
				SetLastError(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
				return false;
			};
			std::size_t count = 0;
			bool changed = changeStyle || canvasPolicyDirty_;
			bool geometryChanged = false;
			// 两画布都参加，不受激活样式 helper 仅处理 Freeze/Drawpad 的筛选限制。
			for (const auto role : { WindowRole::DrawpadPresentation, WindowRole::Drawpad })
			{
				auto& record = records_[RoleIndex(role)];
				if (only && only != &record) continue;
				const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
				if (!hwnd) continue;
				if (!record.canvasPolicy.initialized || !IsWindow(hwnd))
					return failClosed(ERROR_INVALID_WINDOW_HANDLE);
				Detail::CanvasPolicyKey key{ .hwnd = hwnd, .whiteboard = whiteboard };
				MONITORINFO monitor{ sizeof(monitor) };
				if (!GetWindowRect(hwnd, &key.bounds)
					|| !GetMonitorInfoW(MonitorFromRect(&key.bounds, MONITOR_DEFAULTTONEAREST), &monitor))
					return failClosed(GetLastError());
				key.monitor = monitor.rcMonitor;
				operations.records[count] = &record;
				operations.snapshots[count].visible = record.canvasPolicy.recoveryVisible.value_or(
					IsWindowVisible(hwnd) != FALSE);
				operations.snapshots[count].iconic = record.canvasPolicy.recoveryVisible
					? record.canvasPolicy.recoveryIconic : IsIconic(hwnd) != FALSE;
				operations.keys[count++] = key;
				changed |= Detail::CanvasPolicyNeedsUpdate(record.canvasPolicy.applied, key);
				// 故障隐藏后 applied 已清除，后续再失败也不能揭开旧 region。
				geometryChanged |= record.canvasPolicy.recoveryVisible.has_value();
				if (record.canvasPolicy.applied)
					geometryChanged |= !EqualRect(&record.canvasPolicy.applied->bounds, &key.bounds)
						|| !EqualRect(&record.canvasPolicy.applied->monitor, &key.monitor);
			}
			if (!changed)
			{
				canvasPolicyRetry_ = false;
				return true;
			}
			// 已有 dirty 时旧样式同样不可信，回滚期间也不能短暂揭开画布。
			const auto result = Detail::ApplyCanvasPairTransaction(count, operations,
				!geometryChanged && !canvasPolicyDirty_);
			const auto completion = Detail::ResolveCanvasPolicyCompletion(
				result, geometryChanged, canvasPolicyDirty_);
			for (std::size_t i = 0; i < count; ++i)
			{
				auto& policy = operations.records[i]->canvasPolicy;
				completion.UpdateApplied(policy.applied, operations.keys[i]);
				if (result.succeeded) policy.recoveryVisible.reset();
				else if (completion.retry)
				{
					// 旧区域或样式不可信；回滚不揭开画布，重试成功后再恢复原显隐。
					(void)operations.Hide(i);
					policy.recoveryVisible = operations.snapshots[i].visible;
					policy.recoveryIconic = operations.snapshots[i].iconic;
				}
			}
			canvasPolicyDirty_ = completion.dirty;
			canvasPolicyRetry_ = completion.retry;
			SetLastError(result.succeeded ? ERROR_SUCCESS : operations.error);
			return result.succeeded;
		}

		[[nodiscard]] bool ApplyWhiteboardWindowMode(bool whiteboard)
		{
			const bool changed = whiteboardWindowMode_.load(std::memory_order_acquire) != whiteboard;
			if (!EnsureCanvasPolicy(whiteboard, changed || canvasPolicyDirty_)) return false;
			if (changed)
			{
				// 双画布策略提交成功后再清最小化锚点，失败回滚不得发布新场景。
				if (!whiteboard && whiteboardGroupMinimized_.load(std::memory_order_acquire))
				{
					if (const HWND anchor = Handle(WindowRole::Freeze); anchor && IsIconic(anchor))
					{
						whiteboardGroupMinimized_.store(false, std::memory_order_release);
						ShowWindow(anchor, SW_SHOWNOACTIVATE);
						ShowWindow(anchor, SW_HIDE);
					}
				}
				whiteboardWindowMode_.store(whiteboard, std::memory_order_release);
				whiteboardGroupMinimized_.store(false, std::memory_order_release);
				whiteboardVisibleBeforeMinimize_.fill(false);
				NotifyWhiteboardMinimizedChanged();
			}
			return true;
		}

		[[nodiscard]] bool ApplyWhiteboardActivationStyle(bool whiteboard)
		{
			constexpr WindowRole roles[] = {
				WindowRole::MagnifierHost,
				WindowRole::Freeze,
				WindowRole::DrawpadPresentation,
				WindowRole::Drawpad,
				WindowRole::PptBottomLeft,
				WindowRole::PptBottomRight,
				WindowRole::PptMiddleLeft,
				WindowRole::PptMiddleRight,
				WindowRole::Bar,
			};
			struct WindowStyleSnapshot
			{
				HWND hwnd = nullptr;
				LONG_PTR exStyle = 0;
				bool visible = false;
				bool iconic = false;
				bool styleApplied = false;
			};
			std::vector<WindowStyleSnapshot> snapshots;
			snapshots.reserve(std::size(roles));
			auto RestoreVisibility = [this](const WindowStyleSnapshot& snapshot) noexcept
			{
				if (!snapshot.hwnd || !IsWindow(snapshot.hwnd)) return;
				if (shutdownRequested_.load(std::memory_order_acquire) || SafetyHidden(snapshot.hwnd))
				{
					ShowWindow(snapshot.hwnd, SW_HIDE);
					return;
				}
				if (snapshot.visible)
					ShowWindow(snapshot.hwnd, snapshot.iconic
						? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
				else
					ShowWindow(snapshot.hwnd, SW_HIDE);
			};
			auto RestoreSnapshot = [&](const WindowStyleSnapshot& snapshot) noexcept
			{
				if (!snapshot.hwnd || !IsWindow(snapshot.hwnd)) return;
				if (snapshot.styleApplied)
				{
					SetLastError(ERROR_SUCCESS);
					(void)SetWindowLongPtrW(snapshot.hwnd, GWL_EXSTYLE,
						snapshot.exStyle);
					(void)SetWindowPos(snapshot.hwnd, nullptr, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
						SWP_NOACTIVATE | SWP_FRAMECHANGED);
				}
				RestoreVisibility(snapshot);
			};
			bool succeeded = true;
			for (const WindowRole role : roles)
			{
				// 分页 HWND 与 Bar 的激活样式不变，不能为无效重写制造隐藏帧。
				if (!RequiresOverlayActivationStyleTransition(role,
					whiteboard ? OverlayActivationMode::Presentation
						: OverlayActivationMode::Whiteboard,
					whiteboard ? OverlayActivationMode::Whiteboard
						: OverlayActivationMode::Presentation)) continue;
				const auto* record = Record(role);
				const HWND hwnd = record ? record->hwnd.load(std::memory_order_acquire)
					: nullptr;
				if (!record || !hwnd || !IsWindow(hwnd)) continue;
				const auto target = ResolveOverlayActivationStyle(role,
					whiteboard ? OverlayActivationMode::Whiteboard
						: OverlayActivationMode::Presentation);
				SetLastError(ERROR_SUCCESS);
				const LONG_PTR previousStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
				if (!previousStyle && GetLastError() != ERROR_SUCCESS)
				{
					succeeded = false;
					break;
				}
				// 只切换激活策略负责的位，保留 click-through 等运行时样式。
				const LONG_PTR desired =
					(previousStyle | static_cast<LONG_PTR>(target.setExStyle))
					& ~static_cast<LONG_PTR>(target.clearExStyle);
				snapshots.push_back({ hwnd, previousStyle,
					IsWindowVisible(hwnd) != FALSE, IsIconic(hwnd) != FALSE, false });
				// Shell 要求动态 APPWINDOW/TOOLWINDOW 先隐藏，再 FrameChanged 后恢复。
				ShowWindow(hwnd, SW_HIDE);
				SetLastError(ERROR_SUCCESS);
				const LONG_PTR previous = SetWindowLongPtrW(
					hwnd, GWL_EXSTYLE, desired);
				if (!previous && GetLastError() != ERROR_SUCCESS)
				{
					succeeded = false;
					break;
				}
				snapshots.back().styleApplied = true;
				if (!SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOACTIVATE | SWP_FRAMECHANGED))
				{
					succeeded = false;
					break;
				}
				RestoreVisibility(snapshots.back());
			}
			if (!succeeded)
			{
				// 任一 HWND 失败时恢复整组，禁止进入半套 taskbar/activation 样式。
				for (std::size_t index = snapshots.size(); index > 0; --index)
					RestoreSnapshot(snapshots[index - 1]);
				return false;
			}
			return succeeded;
		}

		void NotifyWhiteboardMinimizedChanged() noexcept
		{
			const auto& spec = records_[RoleIndex(WindowRole::Freeze)].activeSpec;
			try { if (spec && spec->whiteboardMinimizedChanged) spec->whiteboardMinimizedChanged(); }
			catch (...) {}
		}

		[[nodiscard]] bool ApplyMinimizeWhiteboardGroup() noexcept
		{
			if (!whiteboardWindowMode_.load(std::memory_order_acquire)) return false;
			const auto& spec = records_[RoleIndex(WindowRole::Freeze)].activeSpec;
			try { if (spec && spec->whiteboardMinimizeAllowed && !spec->whiteboardMinimizeAllowed()) return false; }
			catch (...) { return false; }
			if (whiteboardGroupMinimized_.load(std::memory_order_acquire)) return true;
			constexpr WindowRole roles[] = {
				WindowRole::MagnifierHost, WindowRole::Freeze,
				WindowRole::DrawpadPresentation, WindowRole::Drawpad,
				WindowRole::PptBottomLeft, WindowRole::PptBottomRight,
				WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight,
			};
			for (const WindowRole role : roles)
			{
				const HWND hwnd = Handle(role);
				if (!hwnd || !IsWindow(hwnd)) continue;
				whiteboardVisibleBeforeMinimize_[RoleIndex(role)] =
					IsWindowVisible(hwnd) != FALSE;
			}
			const HWND anchor = Handle(WindowRole::Freeze);
			if (!anchor || !IsWindow(anchor)) return false;
			// 先锁存状态再触发 WM_SIZE，避免系统消息重入时再次递归最小化。
			whiteboardGroupMinimized_.store(true, std::memory_order_release);
			(void)ApplyCancelPointerCapture();
			ShowWindow(anchor, SW_MINIMIZE);
			for (const WindowRole role : roles)
			{
				if (role == WindowRole::Freeze) continue;
				if (const HWND hwnd = Handle(role); hwnd && IsWindow(hwnd))
					ShowWindow(hwnd, SW_HIDE);
			}
			// Windows 自动隐藏 owned popup，显式恢复主栏以保留工作区入口。
			if (const HWND bar = Handle(WindowRole::Bar)) ShowWindow(bar, SafetyHidden(bar) ? SW_HIDE : SW_SHOWNOACTIVATE);
			NotifyWhiteboardMinimizedChanged();
			return true;
		}

		[[nodiscard]] bool ApplyRestoreWhiteboardGroup() noexcept
		{
			if (shutdownRequested_.load(std::memory_order_acquire)) return false;
			if (!whiteboardWindowMode_.load(std::memory_order_acquire)) return false;
			if (!whiteboardGroupMinimized_.load(std::memory_order_acquire)) return true;
			if (!EnsureCanvasPolicy(true)) return false;
			constexpr WindowRole roles[] = {
				WindowRole::MagnifierHost, WindowRole::Freeze,
				WindowRole::DrawpadPresentation, WindowRole::Drawpad,
				WindowRole::PptBottomLeft, WindowRole::PptBottomRight,
				WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight,
			};
			const HWND anchor = Handle(WindowRole::Freeze);
			if (!anchor || !IsWindow(anchor)) return false;
			// 先释放锁存再触发 WM_SIZE，系统恢复消息只需观察到一次结算。
			whiteboardGroupMinimized_.store(false, std::memory_order_release);
			ShowWindow(anchor, SW_RESTORE);
			if (shutdownRequested_.load(std::memory_order_acquire) || SafetyHidden(anchor) ||
				!whiteboardVisibleBeforeMinimize_[RoleIndex(WindowRole::Freeze)])
				ShowWindow(anchor, SW_HIDE);
			for (const WindowRole role : roles)
			{
				if (shutdownRequested_.load(std::memory_order_acquire)) break;
				if (role == WindowRole::Freeze) continue;
				if (!whiteboardVisibleBeforeMinimize_[RoleIndex(role)]) continue;
				if (const HWND hwnd = Handle(role); hwnd && IsWindow(hwnd))
					ShowWindow(hwnd, SafetyHidden(hwnd) ? SW_HIDE : SW_SHOWNOACTIVATE);
			}
			if (shutdownRequested_.load(std::memory_order_acquire))
			{
				(void)HideUserWindowsInGroup(false);
				return false;
			}
			// Windows 自动隐藏 owned popup，显式恢复主栏以保留工作区入口。
			if (const HWND bar = Handle(WindowRole::Bar)) ShowWindow(bar, SafetyHidden(bar) ? SW_HIDE : SW_SHOWNOACTIVATE);
			NotifyWhiteboardMinimizedChanged();
			return true;
		}

		[[nodiscard]] bool ApplyCancelPointerCapture() noexcept
		{
			constexpr WindowRole roles[] = {
				WindowRole::Drawpad, WindowRole::PptBottomLeft,
				WindowRole::PptBottomRight, WindowRole::PptMiddleLeft,
				WindowRole::PptMiddleRight, WindowRole::Bar,
			};
			for (const WindowRole role : roles)
			{
				const HWND hwnd = Handle(role);
				if (!hwnd || !IsWindow(hwnd)) continue;
				SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
				if (GetCapture() == hwnd) ReleaseCapture();
			}
			return true;
		}

		struct CommandSnapshot
		{
			struct Native
			{
				WindowRole role{};
				HWND hwnd = nullptr;
				std::uint64_t generation = 0;
				HWND owner = nullptr;
				RECT bounds{};
				LONG_PTR style = 0;
				bool visible = false;
				bool iconic = false;
			};
			std::vector<Native> windows;
			bool mode = false;
			bool topmost = false;
			bool fullscreen = false;
			bool group = false;
			bool canvas = false;
			bool minimized = false;
			std::array<bool, RoleCount> visibleBeforeMinimize{};
			std::optional<WindowSpec> destroyedSpec;
			HWND originalWindow = nullptr;
		};
		struct Recovery
		{
			std::function<bool()> restore;
			std::shared_ptr<Detail::CommandCompletion> completion;
		};

		[[nodiscard]] CommandSnapshot CaptureCommandSnapshot(const Command& command)
		{
			CommandSnapshot snapshot;
			snapshot.originalWindow = Handle(command.role);
			snapshot.mode = WhiteboardWindowMode();
			snapshot.topmost = OverlayTopmost();
			snapshot.fullscreen = OverlayFullscreen();
			snapshot.minimized = WhiteboardWindowGroupMinimized();
			snapshot.visibleBeforeMinimize = whiteboardVisibleBeforeMinimize_;
			snapshot.group = command.type == CommandType::SetWhiteboardWindowMode
				|| command.type == CommandType::SetOverlayTopmost || command.type == CommandType::SetOverlayFullscreen
				|| command.type == CommandType::RefreshTopmost || command.type == CommandType::PromotePpt
				|| command.type == CommandType::MinimizeWhiteboardGroup || command.type == CommandType::RestoreWhiteboardGroup;
			snapshot.canvas = snapshot.group || command.type == CommandType::SetDrawpadSurfaceVisibility
				|| (command.type == CommandType::SetBounds && IsCanvas(command.role));
			const auto capture = [&](WindowRole role)
			{
				const HWND hwnd = Handle(role);
				if (!hwnd || !IsWindow(hwnd)) return;
				CommandSnapshot::Native native;
				native.role = role;
				native.hwnd = hwnd;
				native.generation = records_[RoleIndex(role)].generation.load(std::memory_order_acquire);
				native.owner = GetWindow(hwnd, GW_OWNER);
				if (!GetWindowRect(hwnd, &native.bounds)) throw std::runtime_error("window snapshot");
				native.style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
				native.visible = IsWindowVisible(hwnd) != FALSE;
				native.iconic = IsIconic(hwnd) != FALSE;
				snapshot.windows.push_back(native);
			};
			if (snapshot.group) for (const auto role : GroupRoles) capture(role);
			else if (command.type == CommandType::SetDrawpadSurfaceVisibility)
			{ capture(WindowRole::DrawpadPresentation); capture(WindowRole::Drawpad); }
			else if (command.type != CommandType::Create && command.type != CommandType::Destroy
				&& command.type != CommandType::BindMessages && command.type != CommandType::UnbindMessages
				&& command.type != CommandType::HideAll && command.type != CommandType::CancelPointerCapture)
				capture(command.role);
			if (command.type == CommandType::Destroy) snapshot.destroyedSpec = records_[RoleIndex(command.role)].activeSpec;
			return snapshot;
		}

		[[nodiscard]] bool RestoreCommandSnapshot(const CommandSnapshot& snapshot, CommandType type, WindowRole role)
		{
			if (type == CommandType::Create) return snapshot.originalWindow || !Handle(role) || DestroyDynamic(role);
			if (type == CommandType::Destroy)
				return Handle(role) || !snapshot.destroyedSpec || CreateDynamic(*snapshot.destroyedSpec);
			if (snapshot.group)
			{
				overlayTopmost_.store(snapshot.topmost, std::memory_order_release);
				overlayFullscreen_.store(snapshot.fullscreen, std::memory_order_release);
				whiteboardWindowMode_.store(snapshot.mode, std::memory_order_release);
				whiteboardGroupMinimized_.store(snapshot.minimized, std::memory_order_release);
				whiteboardVisibleBeforeMinimize_ = snapshot.visibleBeforeMinimize;
			}
			bool restored = true;
			for (const auto& native : snapshot.windows)
			{
				if (Handle(native.role) != native.hwnd || records_[RoleIndex(native.role)].generation.load(std::memory_order_acquire) != native.generation)
					continue; // 已销毁的一代不再拥有任何写回权限。
				if (!IsWindow(native.hwnd)) { restored = false; continue; }
				// TOPMOST 只能通过窗口组政策恢复；完整写回其余样式，保留原来的动态穿透位。
				const LONG_PTR actual = GetWindowLongPtrW(native.hwnd, GWL_EXSTYLE);
				const LONG_PTR expectedStyle = safetyOverrides_[RoleIndex(native.role)].Style(native.generation, native.style);
				const LONG_PTR style = (expectedStyle & ~static_cast<LONG_PTR>(WS_EX_TOPMOST)) | (actual & WS_EX_TOPMOST);
				if (GetWindow(native.hwnd, GW_OWNER) != native.owner)
				{
					SetLastError(ERROR_SUCCESS);
					if (!SetWindowLongPtrW(native.hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(native.owner)) && GetLastError()) restored = false;
				}
				if (actual != style)
				{
					SetLastError(ERROR_SUCCESS);
					if (!SetWindowLongPtrW(native.hwnd, GWL_EXSTYLE, style) && GetLastError()) restored = false;
				}
				if (!SetWindowPos(native.hwnd, nullptr, native.bounds.left, native.bounds.top,
					native.bounds.right-native.bounds.left, native.bounds.bottom-native.bounds.top,
					SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) restored = false;
			}
			if (snapshot.canvas)
			{
				canvasPolicyDirty_ = true;
				if (!EnsureCanvasPolicy(snapshot.mode, true)) restored = false;
			}
			if (snapshot.group && !RefreshOverlayGroup()) restored = false;
			for (const auto& native : snapshot.windows)
			{
				if (Handle(native.role) != native.hwnd || records_[RoleIndex(native.role)].generation.load(std::memory_order_acquire) != native.generation) continue;
				if (!restored && snapshot.canvas && IsCanvas(native.role)) continue;
				const bool visible = safetyOverrides_[RoleIndex(native.role)].Visible(native.generation, native.visible)
					&& !shutdownRequested_.load(std::memory_order_acquire);
				ShowWindow(native.hwnd, visible ? (native.iconic ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE) : SW_HIDE);
				RECT actual{};
				if (!GetWindowRect(native.hwnd, &actual) || !EqualRect(&actual, &native.bounds)
					|| (IsWindowVisible(native.hwnd) != FALSE) != visible
					|| (!snapshot.group && GetWindow(native.hwnd, GW_OWNER) != native.owner)
					|| ((GetWindowLongPtrW(native.hwnd, GWL_EXSTYLE) ^ safetyOverrides_[RoleIndex(native.role)].Style(native.generation, native.style)) & ~static_cast<LONG_PTR>(WS_EX_TOPMOST))) restored = false;
			}
			if (restored && snapshot.group) NotifyWhiteboardMinimizedChanged();
			return restored;
		}

		[[nodiscard]] bool RetryCommandRecovery(bool setting)
		{
			auto& recovery = commandRecovery_[setting ? 1 : 0];
			if (!recovery) return true;
			bool restored = false;
			try { restored = recovery->restore(); } catch (...) {}
			if (!restored) return false;
			if (recovery->completion)
			{
				if (!recovery->completion->Current()) recovery->completion->Retired();
				else if (!recovery->completion->Commit(false)) recovery->completion->Retired();
			}
			recovery.reset();
			return true;
		}

		[[nodiscard]] bool SafetyHidden(HWND hwnd) const noexcept
		{
			for (std::size_t i = 0; i < RoleCount; ++i)
				if (records_[i].hwnd.load(std::memory_order_acquire) == hwnd && hwnd)
					return safetyOverrides_[i].Hidden(records_[i].generation.load(std::memory_order_acquire));
			return false;
		}

		[[nodiscard]] static bool IsSafetyCleanup(const Command& command) noexcept
		{
			return command.type == CommandType::HideAll || command.type == CommandType::Hide
				|| (command.type == CommandType::SetDrawpadSurfaceVisibility && command.drawpadVisibility == DrawpadSurfaceVisibility::Hidden)
				|| (command.type == CommandType::SetClickThrough && command.enabled);
		}

		void LatchSafetyCleanup(const Command& command) noexcept
		{
			const auto latch = [&](WindowRole role, bool hidden, bool transparent)
			{
				auto& record = records_[RoleIndex(role)];
				if (!record.hwnd.load(std::memory_order_acquire)) return;
				safetyOverrides_[RoleIndex(role)].Latch(record.generation.load(std::memory_order_acquire), hidden, transparent);
				if (hidden && IsCanvas(role)) Detail::NoteCanvasRecoveryHidden(record.canvasPolicy.recoveryVisible, record.canvasPolicy.recoveryIconic);
			};
			if (command.type == CommandType::HideAll)
				for (std::size_t i = 0; i < RoleCount; ++i)
				{
					const auto role = static_cast<WindowRole>(i);
					if (IsSetting(role) == IsSetting(command.role) && !IsDisplayObserver(role)) latch(role, true, false);
				}
			else if (command.type == CommandType::SetDrawpadSurfaceVisibility)
			{
				latch(WindowRole::Drawpad, true, false);
				latch(WindowRole::DrawpadPresentation, true, false);
			}
			else latch(command.role, command.type == CommandType::Hide, command.type == CommandType::SetClickThrough);
			safetyCleanupRetry_[IsSetting(command.role) ? 1 : 0] = true;
		}

		[[nodiscard]] bool RetrySafetyCleanup(bool setting)
		{
			bool succeeded = true;
			try
			{
			for (std::size_t i = 0; i < RoleCount; ++i)
			{
				const auto role = static_cast<WindowRole>(i);
				if (IsSetting(role) != setting) continue;
				const auto generation = records_[i].generation.load(std::memory_order_acquire);
				const auto& safety = safetyOverrides_[i];
				const HWND hwnd = Handle(role);
				if (!hwnd || !IsWindow(hwnd) || safety.generation != generation) continue;
				if (safety.hidden)
				{
					if (IsSetting(role)) succeeded = HideSettingWindow(hwnd) && succeeded;
					else { ShowWindow(hwnd, SW_HIDE); succeeded = !IsWindowVisible(hwnd) && succeeded; }
				}
				if (safety.clickThrough)
				{
					Command protect;
					protect.type = CommandType::SetClickThrough;
					protect.role = role;
					protect.enabled = true;
					protect.stillDesired = [this, i, generation] { return records_[i].generation.load(std::memory_order_acquire) == generation
						&& safetyOverrides_[i].ClickThrough(generation); };
					succeeded = Execute(protect) && succeeded;
				}
			}
			}
			catch (...) { succeeded = false; }
			safetyCleanupRetry_[setting ? 1 : 0] = !succeeded;
			return succeeded;
		}

		void ClearSafetyAfterCommit(const Command& command) noexcept
		{
			const auto allowShow = [&](WindowRole role)
			{
				safetyOverrides_[RoleIndex(role)].AllowShow(records_[RoleIndex(role)].generation.load(std::memory_order_acquire));
			};
			if (command.type == CommandType::Show) allowShow(command.role);
			else if (command.type == CommandType::SetDrawpadSurfaceVisibility)
			{
				if (command.drawpadVisibility == DrawpadSurfaceVisibility::Primary) allowShow(WindowRole::Drawpad);
				if (command.drawpadVisibility == DrawpadSurfaceVisibility::Presentation) allowShow(WindowRole::DrawpadPresentation);
			}
			else if (command.type == CommandType::SetClickThrough && !command.enabled)
				safetyOverrides_[RoleIndex(command.role)].AllowInput(records_[RoleIndex(command.role)].generation.load(std::memory_order_acquire));
		}

		[[nodiscard]] bool ExecuteTracked(const Command& command)
		{
			const bool setting = IsSetting(command.role);
			bool& applying = commandApplying_[setting ? 1 : 0];
			if (applying)
			{
				if (command.type == CommandType::MinimizeWhiteboardGroup && WhiteboardWindowGroupMinimized()
					&& IsIconic(Handle(WindowRole::Freeze)))
				{
					if (command.completion && command.completion->Begin()) (void)command.completion->Commit(true);
					return true;
				}
				if (command.completion) { command.completion->Cancel(); command.completion->Retired(); }
				return false;
			}
			struct Scope { bool& applying; Scope(bool& value) : applying(value) { applying = true; } ~Scope() { applying = false; } } scope(applying);
			if (command.completion && !command.completion->Begin()) return false;
			const bool cleanup = IsSafetyCleanup(command);
			if (cleanup && CommandCurrent(command)) LatchSafetyCleanup(command);
			if ((!RetryCommandRecovery(setting) && !cleanup) || !CommandCurrent(command))
			{
				if (command.completion)
				{
					if (!CommandCurrent(command)) { command.completion->Cancel(); command.completion->Retired(); }
					else if (!command.completion->Commit(false)) command.completion->Retired();
				}
				return false;
			}
			if (cleanup)
			{
				// 安全覆盖是单调操作；保留旧恢复责任，绝不能用其旧快照撤销新隐藏/穿透。
				bool result = false;
				try { result = Execute(command); result = RetrySafetyCleanup(setting) && result; } catch (...) {}
				if (command.completion)
				{
					if (CommandCurrent(command) && command.completion->Commit(result)) return result;
					command.completion->Cancel();
					command.completion->Retired();
				}
				return result && CommandCurrent(command);
			}
			std::optional<Recovery> prepared;
			bool result = false;
			try
			{
				prepared = Recovery{
					[this, snapshot = CaptureCommandSnapshot(command), type = command.type, role = command.role]
					{ return RestoreCommandSnapshot(snapshot, type, role); }, command.completion };
				result = Execute(command);
			}
			catch (...) {}
			const bool current = CommandCurrent(command);
			if (result && current && (!command.completion || command.completion->Commit(true)))
			{
				ClearSafetyAfterCommit(command);
				return true;
			}
			// 原生写入可能在超时/重入前已发生。先撤回；失败则由 owner 持有恢复责任并拒绝新事务越过。
			if (!current && command.completion) command.completion->Cancel();
			if (prepared)
			{
				commandRecovery_[setting ? 1 : 0] = std::move(prepared);
				(void)RetryCommandRecovery(setting);
			}
			else if (command.completion)
			{
				if (!current) { command.completion->Cancel(); command.completion->Retired(); }
				else if (!command.completion->Commit(false)) command.completion->Retired();
			}

			return false;
		}

		[[nodiscard]] bool Execute(const Command& command)
		{
			// 启动调用已超时的排队刷新不能在 owner 恢复后重新修改窗口层级。
			if (!CommandCurrent(command)) return false;
			if (command.type == CommandType::HideAll)
				return HideUserWindowsInGroup(IsSetting(command.role));
			if (shutdownRequested_.load(std::memory_order_acquire))
			{
				// 旧队列中的显示事务不能越过正式退出；隐藏和销毁仍可排空。
				if (command.type == CommandType::Show ||
					command.type == CommandType::RaiseOwnedSetting ||
					command.type == CommandType::Create ||
					command.type == CommandType::SetWhiteboardWindowMode ||
					command.type == CommandType::RestoreWhiteboardGroup ||
					command.type == CommandType::ActivateWhiteboard ||
					(command.type == CommandType::SetDrawpadSurfaceVisibility &&
						command.drawpadVisibility != DrawpadSurfaceVisibility::Hidden))
					return false;
			}
			// 这些事务可能需要处理一组 HWND，不能被单角色 HWND 前置检查短路。
			if (command.type == CommandType::SetWhiteboardWindowMode)
				return ApplyWhiteboardWindowMode(command.whiteboardMode) && RefreshOverlayGroup(&command);
			if (command.type == CommandType::SetOverlayTopmost)
			{
				overlayTopmost_.store(command.enabled, std::memory_order_release);
				return RefreshOverlayGroup(&command);
			}
			if (command.type == CommandType::SetOverlayFullscreen)
			{
				overlayFullscreen_.store(command.enabled, std::memory_order_release);
				return RefreshOverlayGroup(&command);
			}
			if (command.type == CommandType::MinimizeWhiteboardGroup)
				return ApplyMinimizeWhiteboardGroup();
			if (command.type == CommandType::RestoreWhiteboardGroup)
				return ApplyRestoreWhiteboardGroup();
			if (command.type == CommandType::CancelPointerCapture)
				return ApplyCancelPointerCapture();
			auto* record = Record(command.role);
			const HWND hwnd = record ? record->hwnd.load(std::memory_order_acquire) : nullptr;
			if (command.type == CommandType::Create)
				return command.spec && CreateDynamic(*command.spec);
			if (command.type == CommandType::Destroy)
				return DestroyDynamic(command.role);
			if (command.type != CommandType::RefreshTopmost
				&& (!record || !hwnd || !IsWindow(hwnd)))
			{
				ReportCommandResult(command, false,
					ERROR_INVALID_WINDOW_HANDLE, hwnd);
				return false;
			}

			switch (command.type)
			{
			case CommandType::Create:
			case CommandType::Destroy:
			case CommandType::SetOverlayTopmost:
			case CommandType::SetOverlayFullscreen:
				return false;
			case CommandType::ActivateWhiteboard:
			{
				const auto canActivate = [&]
				{
					return CommandCurrent(command) && !shutdownRequested_.load(std::memory_order_acquire)
						&& WhiteboardWindowMode() && !WhiteboardWindowGroupMinimized()
						&& !SafetyHidden(hwnd) && IsWindowVisible(hwnd) && !IsIconic(hwnd);
				};
				if (!canActivate()) return false;
				// 普通背景 Show 必须保持不激活；打开时才把唯一任务栏锚点带到前台。
				(void)SetForegroundWindow(hwnd);
				if (!canActivate()) return false; // 激活消息可能同步取消本次进入。
				(void)SetActiveWindow(hwnd);
				if (!canActivate()) return false;
				(void)SetFocus(hwnd);
				// Windows 可拒绝前台切换；不能因此无限等待就绪或持续重试抢焦点。
				return true;
			}
			case CommandType::Show:
			{
				// 拒绝最小化之前排队的呈现请求，不能重新唤醒画布或翻页控件。
				if (WhiteboardWindowGroupMinimized() && command.role != WindowRole::Bar
					&& !IsSetting(command.role)) return true;
				if (IsCanvas(command.role) && !EnsureCanvasPolicy(
					whiteboardWindowMode_.load(std::memory_order_acquire))) return false;
				bool succeeded = true;
				DWORD error = ERROR_SUCCESS;
				if (IsSetting(command.role))
				{
					ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
					if (GetWindow(hwnd, GW_OWNER)
						&& GetWindow(hwnd, GW_OWNER) == Handle(WindowRole::Drawpad))
					{
						// 只调整当前继承窗口带内的顺序，不独立设置 TOPMOST。
						SetLastError(ERROR_SUCCESS);
						succeeded = SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
						if (succeeded) succeeded = SettingAboveDrawpads(hwnd);
						if (!succeeded)
						{
							error = GetLastError();
							if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
						}
					}
					// Setting 是普通应用窗口；每次打开都交还键盘焦点。
					(void)SetForegroundWindow(hwnd);
					(void)SetActiveWindow(hwnd);
					(void)SetFocus(hwnd);
				}
				else
				{
					ShowWindow(hwnd, SW_SHOWNOACTIVATE);
					if (IsPpt(command.role))
					{
						succeeded = RefreshOverlayGroup(&command, hwnd);
						if (!succeeded) error = GetLastError();
					}

					if (succeeded && IsPpt(command.role)
						&& !IsWindowVisible(hwnd))
					{
						succeeded = false;
						error = ERROR_GEN_FAILURE;
					}
				}
				if (shutdownRequested_.load(std::memory_order_acquire))
				{
					if (IsSetting(command.role)) (void)HideSettingWindow(hwnd);
					else ShowWindow(hwnd, SW_HIDE);
					return false;
				}
				ReportCommandResult(command, succeeded,
					error == ERROR_SUCCESS && !succeeded ? ERROR_GEN_FAILURE : error,
					hwnd);
				return succeeded;
			}
			case CommandType::Hide:
				if (IsCanvas(command.role))
					Detail::NoteCanvasRecoveryHidden(record->canvasPolicy.recoveryVisible,
						record->canvasPolicy.recoveryIconic);
				if (IsSetting(command.role))
				{
					const bool hidden = HideSettingWindow(hwnd);
					const DWORD error = hidden ? ERROR_SUCCESS : GetLastError();
					ReportCommandResult(command, hidden, error, hwnd);
					return hidden;
				}
				ShowWindow(hwnd, SW_HIDE);
				if (IsPpt(command.role) && IsWindowVisible(hwnd))
				{
					ReportCommandResult(command, false, ERROR_GEN_FAILURE, hwnd);
					return false;
				}
				ReportCommandResult(command, true, ERROR_SUCCESS, hwnd);
				return true;
			case CommandType::HideAll:
				return false;
			case CommandType::SetDrawpadSurfaceVisibility:
			{
				// 提交前在窗口 owner thread 复核版本，过期退出不能撤销新笔的 capture。
				if (command.stillDesired && !command.stillDesired()) return false;
				if (command.drawpadVisibility != DrawpadSurfaceVisibility::Hidden &&
					shutdownRequested_.load(std::memory_order_acquire)) return false;
				SetLastError(ERROR_SUCCESS);
				const bool succeeded =
					ApplyDrawpadSurfaceVisibility(command.drawpadVisibility);
				DWORD error = succeeded ? ERROR_SUCCESS : GetLastError();
				if (!succeeded && error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
				ReportCommandResult(command, succeeded, error, hwnd);
				return succeeded;
			}
			case CommandType::SetBounds:
			{
				if (WhiteboardWindowGroupMinimized() && command.role == WindowRole::Freeze) return true;
				SetLastError(ERROR_SUCCESS);
				const bool succeeded = command.role == WindowRole::Drawpad
					? ApplyDrawpadBounds(command.bounds)
					: SetWindowPos(hwnd, nullptr, command.bounds.left,
						command.bounds.top, command.bounds.right - command.bounds.left,
						command.bounds.bottom - command.bounds.top,
						SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
				DWORD error = succeeded ? ERROR_SUCCESS : GetLastError();
				bool applied = succeeded;
				if (applied && IsPpt(command.role))
				{
					RECT actual{};
					applied = GetWindowRect(hwnd, &actual) != FALSE
						&& EqualRect(&actual, &command.bounds) != FALSE;
					if (!applied) error = ERROR_GEN_FAILURE;
				}
				if (!applied && error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
				ReportCommandResult(command, applied, error, hwnd);
				return applied;
			}
			case CommandType::SetClickThrough:
			{
				struct Operations
				{
					HWND hwnd;
					const Command& command;
					const Impl& owner;
					bool StillDesired() const
					{
						if (owner.CommandCurrent(command)) return true;
						SetLastError(ERROR_CANCELLED);
						return false;
					}
					bool ReadStyle(LONG_PTR& style) const noexcept
					{
						SetLastError(ERROR_SUCCESS);
						style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
						return style != 0 || GetLastError() == ERROR_SUCCESS;
					}
					bool WriteStyle(LONG_PTR style) const noexcept
					{
						SetLastError(ERROR_SUCCESS);
						return SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style) != 0
							|| GetLastError() == ERROR_SUCCESS;
					}
					bool CommitStyle() const noexcept
					{
						return SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
							SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
					}
				} operations{ hwnd, command, *this };
				const bool applied = Detail::ApplyCanvasClickThrough(command.enabled,
					record->clickThroughFramePending, record->clickThroughApplying, operations);
				const DWORD error = applied ? ERROR_SUCCESS : GetLastError();
				ReportCommandResult(command, applied, !applied && !error ? ERROR_GEN_FAILURE : error, hwnd);
				return applied;
			}
			case CommandType::RaiseOwnedSetting:
			{
				const HWND owner = Handle(WindowRole::Drawpad);
				if (!owner || !IsWindow(owner) || GetWindow(hwnd, GW_OWNER) != owner)
					return false;
				const HWND root = OverlayRoot();
				const bool topmost = overlayTopmost_.load(std::memory_order_acquire)
					&& !whiteboardWindowMode_.load(std::memory_order_acquire);
				if (!root || !IsWindow(root)
					|| ((GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost)
					return false;
				if (((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost)
				{
					// 只在实际band失配时从当前根继承；随后TOP将owned恢复到owner上方。
					SetLastError(ERROR_SUCCESS);
					const bool inherited = SetWindowPos(hwnd, root, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
					const DWORD inheritError = inherited ? ERROR_SUCCESS : GetLastError();
					if (!inherited || ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost)
					{
						ReportCommandResult(command, false, inheritError ? inheritError : ERROR_GEN_FAILURE, hwnd);
						return false;
					}
				}
				// 不显示、不激活、不改变 owner 的 Z 序；topmost band 继续只由根策略决定。
				SetLastError(ERROR_SUCCESS);
				const bool succeeded = SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
				DWORD error = succeeded ? ERROR_SUCCESS : GetLastError();
				if (!succeeded && error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
				ReportCommandResult(command, succeeded, error, hwnd);
				return succeeded;
			}
			case CommandType::SetSettingOwnedByDrawpad:
			{
				const HWND drawpad = Handle(WindowRole::Drawpad);
				if (command.enabled && !command.settingOwner && (!drawpad || !IsWindow(drawpad)))
				{
					ReportCommandResult(command, false,
						ERROR_INVALID_WINDOW_HANDLE, hwnd);
					return false;
				}

				const HWND desiredOwner = command.settingOwner.value_or(
					command.enabled ? drawpad : nullptr);
				const HWND previousOwner = GetWindow(hwnd, GW_OWNER);
				bool succeeded = true;
				DWORD error = ERROR_SUCCESS;
				if (previousOwner != desiredOwner)
				{
					// GWLP_HWNDPARENT 对顶层窗口修改的是 owner，保留 Fluent frame；命令在 Setting 线程执行。
					SetLastError(ERROR_SUCCESS);
					const LONG_PTR previous = SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT,
						reinterpret_cast<LONG_PTR>(desiredOwner));
					error = GetLastError();
					succeeded = previous != 0 || error == ERROR_SUCCESS;
					if (succeeded && GetWindow(hwnd, GW_OWNER) != desiredOwner)
					{
						succeeded = false;
						error = ERROR_GEN_FAILURE;
					}
				}

				if (succeeded && !command.enabled)
				{
					SetLastError(ERROR_SUCCESS);
					succeeded = SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
					if (!succeeded)
					{
						error = GetLastError();
						if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
					}
				}

				if (!succeeded && GetWindow(hwnd, GW_OWNER) != previousOwner)
				{
					// 后半段失败时恢复原 owner，禁止留下只完成一半的窗口关系。
					SetLastError(ERROR_SUCCESS);
					const LONG_PTR restored = SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT,
						reinterpret_cast<LONG_PTR>(previousOwner));
					const DWORD rollbackError = GetLastError();
					if ((restored == 0 && rollbackError != ERROR_SUCCESS) ||
						GetWindow(hwnd, GW_OWNER) != previousOwner)
						error = rollbackError == ERROR_SUCCESS
							? ERROR_GEN_FAILURE : rollbackError;
				}
				ReportCommandResult(command, succeeded, error, hwnd);
				return succeeded;
			}
			case CommandType::SetExtendedStyleFlags:
			{
				LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
				exStyle |= static_cast<LONG_PTR>(command.setMask);
				exStyle &= ~static_cast<LONG_PTR>(command.clearMask);
				SetLastError(ERROR_SUCCESS);
				const LONG_PTR previous = SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
				if (!previous && GetLastError() != ERROR_SUCCESS) return false;
				if (!SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOACTIVATE | SWP_FRAMECHANGED)) return false;
				// 某些 DComp HWND 会拒绝清除 NOREDIRECTIONBITMAP；写后核对，禁止误报后端切换成功。
				SetLastError(ERROR_SUCCESS);
				const LONG_PTR applied = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
				if (!applied && GetLastError() != ERROR_SUCCESS) return false;
				return (applied & static_cast<LONG_PTR>(command.setMask)) ==
					static_cast<LONG_PTR>(command.setMask) &&
					(applied & static_cast<LONG_PTR>(command.clearMask)) == 0;
			}
			case CommandType::RefreshTopmost:
			case CommandType::PromotePpt:
			{
				if (command.type == CommandType::PromotePpt && !IsPpt(command.role)) return false;
				const bool succeeded = RefreshOverlayGroup(&command,
					command.type == CommandType::PromotePpt ? hwnd : nullptr);
				ReportCommandResult(command, succeeded, succeeded ? ERROR_SUCCESS : GetLastError(), hwnd);
				return succeeded;
			}

			case CommandType::BindMessages:
				if (!record->channel) return false;
				if (record->messagesBound)
					(void)record->channel->Unbind(hwnd);
				record->messagesBound = record->channel->Bind(hwnd, command.bindOptions);
				return record->messagesBound;
			case CommandType::UnbindMessages:
				if (!record->channel || !record->messagesBound) return false;
				record->messagesBound = !record->channel->Unbind(hwnd);
				return !record->messagesBound;
			case CommandType::Count:
				return false;
			}
			return false;
		}

		[[nodiscard]] bool HideSettingWindow(HWND hwnd) noexcept
		{
			DWORD error = ERROR_SUCCESS;
			// 仅在 HWND owner thread 收束设置自己的捕获，不影响已转交给主栏/画布的输入。
			if (GetCapture() == hwnd)
			{
				SetLastError(ERROR_SUCCESS);
				const bool released = ReleaseCapture() != FALSE;
				const DWORD releaseError = released ? ERROR_SUCCESS : GetLastError();
				if (GetCapture() == hwnd)
					error = releaseError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : releaseError;
			}
			ShowWindow(hwnd, SW_HIDE);
			if (IsWindowVisible(hwnd) && error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
			SetLastError(error);
			return error == ERROR_SUCCESS;
		}

		[[nodiscard]] bool HideUserWindowsInGroup(bool settingGroup) noexcept
		{
			if (settingGroup)
			{
				if (const HWND hwnd = Handle(WindowRole::Setting); hwnd && IsWindow(hwnd))
					return HideSettingWindow(hwnd);
				return true;
			}

			constexpr WindowRole overlayRoles[] = {
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
			};
			bool hidden = true;
			const HWND captured = GetCapture();
			if (captured)
			{
				for (const auto role : overlayRoles)
				{
					if (Handle(role) != captured) continue;
					// 隐藏透明画布前撤销本 owner 的 capture，避免退出期继续吞掉桌面输入。
					if (!ReleaseCapture() && GetCapture() == captured)
						hidden = false;
					break;
				}
			}
			for (const auto role : overlayRoles)
			{
				if (IsCanvas(role))
					Detail::NoteCanvasRecoveryHidden(records_[RoleIndex(role)].canvasPolicy.recoveryVisible,
						records_[RoleIndex(role)].canvasPolicy.recoveryIconic);
				if (const HWND hwnd = Handle(role); hwnd && IsWindow(hwnd))
				{
					ShowWindow(hwnd, SW_HIDE);
					if (IsWindowVisible(hwnd)) hidden = false;
				}
			}
			return hidden;
		}

		[[nodiscard]] bool ApplyDrawpadSurfaceVisibility(
			DrawpadSurfaceVisibility visibility) noexcept
		{
			if (WhiteboardWindowGroupMinimized()) visibility = DrawpadSurfaceVisibility::Hidden;
			const HWND primary = Handle(WindowRole::Drawpad);
			const HWND presentation = Handle(WindowRole::DrawpadPresentation);
			const bool wasPrimaryVisible = primary && IsWindowVisible(primary);
			const bool wasPresentationVisible = presentation && IsWindowVisible(presentation);
			const auto complete = [&](bool succeeded, DWORD error) noexcept
			{
				// 只在可见性变化或失败时采样窗口快照，并恢复 Win32 错误供命令层记录。
				const bool changed = wasPrimaryVisible !=
					(primary && IsWindowVisible(primary)) || wasPresentationVisible !=
					(presentation && IsWindowVisible(presentation));
				if (!succeeded || changed)
					LogDrawpadSurfaceState(succeeded ? L"visibility-applied"
						: L"visibility-failed", visibility, error, primary, presentation);
				SetLastError(error);
				return succeeded;
			};
			if (!primary || !presentation || !IsWindow(primary) || !IsWindow(presentation))
			{
				return complete(false, ERROR_INVALID_WINDOW_HANDLE);
			}
			if (visibility == DrawpadSurfaceVisibility::Hidden)
				for (const auto role : { WindowRole::DrawpadPresentation, WindowRole::Drawpad })
					Detail::NoteCanvasRecoveryHidden(records_[RoleIndex(role)].canvasPolicy.recoveryVisible,
						records_[RoleIndex(role)].canvasPolicy.recoveryIconic);
			if (visibility != DrawpadSurfaceVisibility::Hidden && !EnsureCanvasPolicy(
				whiteboardWindowMode_.load(std::memory_order_acquire)))
				return complete(false, GetLastError());
			if (visibility != DrawpadSurfaceVisibility::Primary && GetCapture() == primary)
			{
				// 仅释放本次要撤下的主画布捕获，不向主栏或分页控件广播取消消息。
				if (!ReleaseCapture() && GetCapture() == primary)
				{
					const DWORD error = GetLastError();
					ShowWindow(primary, SW_HIDE);
					ShowWindow(presentation, SW_HIDE);
					return complete(false,
						error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
				}
			}

			const auto matchesVisibility = [&]() noexcept
			{
				return (IsWindowVisible(primary) != FALSE) ==
					(visibility == DrawpadSurfaceVisibility::Primary) &&
					(IsWindowVisible(presentation) != FALSE) ==
					(visibility == DrawpadSurfaceVisibility::Presentation);
			};
			DWORD deferredError = ERROR_SUCCESS;
			HDWP positions = BeginDeferWindowPos(2);
			if (positions)
			{
				const UINT primaryFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOACTIVATE | (visibility == DrawpadSurfaceVisibility::Primary
						? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
				const UINT presentationFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOACTIVATE | (visibility == DrawpadSurfaceVisibility::Presentation
						? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
				positions = DeferWindowPos(positions, primary, nullptr,
					0, 0, 0, 0, primaryFlags);
				if (!positions)
					deferredError = GetLastError();
				else
				{
					positions = DeferWindowPos(positions, presentation, nullptr,
						0, 0, 0, 0, presentationFlags);
					if (!positions) deferredError = GetLastError();
				}
			}
			else deferredError = GetLastError();
			if (positions)
			{
				if (EndDeferWindowPos(positions))
				{
					if (visibility != DrawpadSurfaceVisibility::Hidden &&
						shutdownRequested_.load(std::memory_order_acquire))
					{
						ShowWindow(primary, SW_HIDE);
						ShowWindow(presentation, SW_HIDE);
						return complete(false, ERROR_OPERATION_ABORTED);
					}
					// USER32 返回成功后仍读回两窗；旧主窗可见会继续拦截桌面输入。
					if (matchesVisibility()) return complete(true, ERROR_SUCCESS);
					deferredError = ERROR_GEN_FAILURE;
				}
				else deferredError = GetLastError();
			}

			// 批量切换失败时先清空两窗可见性，再显示唯一目标，禁止 alpha 叠加。
			ShowWindow(primary, SW_HIDE);
			ShowWindow(presentation, SW_HIDE);
			if (visibility != DrawpadSurfaceVisibility::Hidden &&
				shutdownRequested_.load(std::memory_order_acquire))
			{
				return complete(false, ERROR_OPERATION_ABORTED);
			}
			if (visibility == DrawpadSurfaceVisibility::Primary)
				ShowWindow(primary, SW_SHOWNOACTIVATE);
			else if (visibility == DrawpadSurfaceVisibility::Presentation)
				ShowWindow(presentation, SW_SHOWNOACTIVATE);
			if (matchesVisibility())
			{
				return complete(true, ERROR_SUCCESS);
			}
			// 目标表面失败时保留安全的双隐藏状态，由状态线程按最新版本重试。
			ShowWindow(primary, SW_HIDE);
			ShowWindow(presentation, SW_HIDE);
			return complete(false, deferredError == ERROR_SUCCESS
				? ERROR_GEN_FAILURE : deferredError);
		}

		[[nodiscard]] bool ApplyDrawpadBounds(const RECT& bounds) noexcept
		{
			const HWND primary = Handle(WindowRole::Drawpad);
			const HWND presentation = Handle(WindowRole::DrawpadPresentation);
			if (!primary || !presentation || !IsWindow(primary) || !IsWindow(presentation))
			{
				SetLastError(ERROR_INVALID_WINDOW_HANDLE);
				return false;
			}
			const int width = bounds.right - bounds.left;
			const int height = bounds.bottom - bounds.top;
			HDWP positions = BeginDeferWindowPos(2);
			if (!positions) return false;
			positions = DeferWindowPos(positions, primary, nullptr,
				bounds.left, bounds.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
			if (positions)
				positions = DeferWindowPos(positions, presentation, nullptr,
					bounds.left, bounds.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
			const bool positioned = positions && EndDeferWindowPos(positions) != FALSE;
			const DWORD positionError = positioned ? ERROR_SUCCESS : GetLastError();
			// USER32 批量移动可能部分失败，仍按两窗真实 RECT 重新计算裁剪。
			const bool policyApplied = EnsureCanvasPolicy(
				whiteboardWindowMode_.load(std::memory_order_acquire));
			if (!positioned) SetLastError(positionError);
			return positioned && policyApplied;
		}

		[[nodiscard]] bool CreateDynamic(const WindowSpec& spec)
		{
			if (Handle(spec.role) || !CanChangeChainRole(spec.role))
				return false;

			HWND owner = nullptr;
			if (spec.role == WindowRole::MagnifierChild)
			{
				owner = Handle(WindowRole::MagnifierHost);
				if (!owner) return false;
			}
			else if (spec.role == WindowRole::DisplayObserver)
			{
				owner = HWND_MESSAGE;
			}
			else if (IsUiPopup(spec.role))
			{
				owner = Handle(WindowRole::Drawpad);
				if (!owner) return false;
			}
			else if (spec.role == WindowRole::DrawpadPresentation)
			{
				owner = Handle(WindowRole::Freeze);
				if (!owner) return false;
			}
			else if (spec.role == WindowRole::Drawpad)
			{
				owner = Handle(WindowRole::DrawpadPresentation);
				if (!owner) return false;
			}
			else if (!IsSetting(spec.role))
			{
				constexpr WindowRole chain[] = {
					WindowRole::MagnifierHost,
					WindowRole::Freeze,
					WindowRole::DrawpadPresentation,
					WindowRole::Drawpad,
				};
				const int position = OverlayChainPosition(spec.role);
				for (int index = position - 1; index >= 0; --index)
				{
					if ((owner = Handle(chain[index]))) break;
				}
			}

			if (!CreateWindowFor(spec, owner, GetCurrentThreadId()))
				return false;
			specs_[RoleIndex(spec.role)] = spec;
			configured_[RoleIndex(spec.role)].store(true, std::memory_order_release);
			return true;
		}

		[[nodiscard]] bool DestroyDynamic(WindowRole role)
		{
			if (!Handle(role) || !CanChangeChainRole(role))
				return false;
			DestroyWindowFor(role);
			configured_[RoleIndex(role)].store(false, std::memory_order_release);
			return true;
		}

			void ApplyOverlayFullscreen(HWND root) noexcept
			{
				HWND target = Handle(WindowRole::Freeze);
				if (!target || !IsWindow(target)) target = root;
				if (!target || !IsWindow(target) || !shellFullscreen_) return;
				// owner 只发布绝对值；Explorer/COM 故障不能阻塞画布窗口消息泵。
				shellFullscreen_->Publish(target,
					reinterpret_cast<std::uintptr_t>(GetPropW(target, Detail::WindowGenerationProperty)),
					overlayFullscreen_.load(std::memory_order_acquire) && !WhiteboardWindowGroupMinimized());
			}

			[[nodiscard]] bool CanChangeChainRole(WindowRole role) const noexcept
			{
			const int position = OverlayChainPosition(role);
			if (position < 0)
				return true;
			constexpr WindowRole chain[] = {
				WindowRole::MagnifierHost,
				WindowRole::Freeze,
				WindowRole::DrawpadPresentation,
				WindowRole::Drawpad,
			};
			for (int index = position + 1; index < static_cast<int>(std::size(chain)); ++index)
			{
				if (Handle(chain[index])) return false;
			}
			if (role == WindowRole::Drawpad)
			{
				for (auto popup = WindowRole::PptBottomLeft;
					popup <= WindowRole::Bar;
					popup = static_cast<WindowRole>(static_cast<unsigned>(popup) + 1))
					if (Handle(popup)) return false;
			}
			if (role == WindowRole::MagnifierHost && Handle(WindowRole::MagnifierChild))
				return false;
			return true;
		}

		std::size_t messageCapacity_ = 63;
		std::array<std::optional<WindowSpec>, RoleCount> specs_{};
		std::array<std::atomic_bool, RoleCount> configured_{};
		std::array<WindowRecord, RoleCount> records_{};
		std::atomic_bool running_ = false;
		std::atomic_bool shutdownRequested_ = false;
		std::shared_ptr<Detail::ShellFullscreenState> shellFullscreen_;
		std::jthread overlayThread_;
		std::jthread settingThread_;
		std::atomic<DWORD> overlayThreadId_ = 0;
		std::atomic<DWORD> settingThreadId_ = 0;
		HANDLE overlayEvent_ = nullptr;
		HANDLE settingEvent_ = nullptr;
		CommandQueue overlayCommands_;
		CommandQueue settingCommands_;
		std::array<bool, 2> commandApplying_{};
		std::array<Detail::WindowSafetyOverride, RoleCount> safetyOverrides_{};
		std::array<bool, 2> safetyCleanupRetry_{};
		std::array<std::optional<Recovery>, 2> commandRecovery_{};
		std::mutex lifecycleMutex_;
		std::array<std::array<std::atomic<std::uint64_t>, RoleCount>, CommandCount> intentVersions_{};
		static thread_local const Impl* receiptOwner_;
		static thread_local std::shared_ptr<Detail::CommandCompletion> lastReceipt_;
		std::array<std::array<bool, RoleCount>, CommandCount>
			commandFailureActive_{};
			std::atomic_bool overlayTopmost_ = true;
			std::mutex topmostObserverMutex_;
			std::condition_variable topmostObserverCondition_;
			std::function<void()> topmostRefreshObserver_;
			std::size_t topmostObserverActiveCalls_ = 0;
			static thread_local bool topmostObserverExecuting_;
			std::atomic_bool overlayFullscreen_ = false;
			std::atomic_bool whiteboardWindowMode_ = false;
			bool canvasPolicyDirty_ = false; // 仅 overlay owner thread 使用。
			bool canvasPolicyApplying_ = false;
			bool canvasPolicyRetry_ = false;
			std::chrono::steady_clock::time_point canvasPolicyRetryAt_{};
			std::atomic_bool whiteboardGroupMinimized_ = false;
			std::array<bool, RoleCount> whiteboardVisibleBeforeMinimize_{};
		};

	thread_local bool Service::Impl::topmostObserverExecuting_ = false;
	thread_local const Service::Impl* Service::Impl::receiptOwner_ = nullptr;
	thread_local std::shared_ptr<Detail::CommandCompletion> Service::Impl::lastReceipt_;

	SIZE QuerySettingFrameThickness(UINT dpi) noexcept
	{
		if (!dpi)
		{
			HDC screen = GetDC(nullptr);
			dpi = screen ? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX)) : 96U;
			if (screen) ReleaseDC(nullptr, screen);
		}
		using AdjustForDpiProc = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
		static const auto adjustForDpi = reinterpret_cast<AdjustForDpiProc>(
			GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
		RECT frame{};
		// 去掉 caption 后只查询四周 resize frame；不使用用户 UI 缩放倍率。
		const DWORD sizingStyle = SettingWindowStyle & ~WS_CAPTION;
		const BOOL adjusted = adjustForDpi
			? adjustForDpi(&frame, sizingStyle, FALSE, SettingWindowExStyle, dpi ? dpi : 96U)
			: AdjustWindowRectEx(&frame, sizingStyle, FALSE, SettingWindowExStyle);
		if (adjusted && frame.left < 0 && frame.top < 0)
			return { -frame.left, -frame.top };
		return { GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER),
			GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER) };
	}

	bool UsesLegacySettingFrame() noexcept
	{
		static const bool legacy = []() noexcept
		{
			// 不依赖启动信息或版本清单；旧 Aero 与现代 DWM 的顶部 frame 合同不同。
			using RtlGetVersionProc = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
			const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
			const auto rtlGetVersion = ntdll
				? reinterpret_cast<RtlGetVersionProc>(GetProcAddress(ntdll, "RtlGetVersion"))
				: nullptr;
			RTL_OSVERSIONINFOW version{};
			version.dwOSVersionInfoSize = sizeof(version);
			if (!rtlGetVersion || rtlGetVersion(&version) != 0) return true;
			return version.dwMajorVersion < 6
				|| (version.dwMajorVersion == 6 && version.dwMinorVersion < 2);
		}();
		return legacy;
	}

	RECT QuerySettingClientFrameInsets(HWND hwnd, bool maximized, UINT dpi) noexcept
	{
		if (!dpi && hwnd)
		{
			using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
			static const auto getDpi = reinterpret_cast<GetDpiForWindowProc>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			if (getDpi) dpi = getDpi(hwnd);
		}
		if (!dpi)
		{
			HDC screen = GetDC(nullptr);
			dpi = screen ? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX)) : 96U;
			if (screen) ReleaseDC(nullptr, screen);
		}
		const SIZE sizing = QuerySettingFrameThickness(dpi);
		BOOL compositionEnabled = FALSE;
		(void)DwmIsCompositionEnabled(&compositionEnabled);
		using GetMetricForDpiProc = int(WINAPI*)(int, UINT);
		static const auto getMetric = reinterpret_cast<GetMetricForDpiProc>(
			GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
		UINT visibleBorder = static_cast<UINT>((std::max)(0, getMetric
			? getMetric(SM_CYBORDER, dpi ? dpi : 96U) : GetSystemMetrics(SM_CYBORDER)));
		if (hwnd && compositionEnabled && !IsZoomed(hwnd))
		{
			UINT dwmBorder = 0;
			if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS,
				&dwmBorder, sizeof(dwmBorder))) && dwmBorder > 0)
				visibleBorder = dwmBorder;
		}
		// Win7 Aero 保留四周 sizing frame；现代 DWM 只在恢复态收薄顶部，经典外框保持原样。
		const auto frame = Inkeys::UI::Setting::ResolveVisibleWindowFrameInsets(
			{ static_cast<int>(sizing.cx), static_cast<int>(sizing.cy),
				static_cast<int>(sizing.cx), static_cast<int>(sizing.cy) },
			maximized, compositionEnabled != FALSE, static_cast<int>(visibleBorder), !UsesLegacySettingFrame());
		return { frame.left, frame.top, frame.right, frame.bottom };
	}

	Service::Service(std::size_t messageCapacity)
		: impl_(std::make_unique<Impl>(messageCapacity))
	{
	}

	Service::~Service() = default;
	CommandReceipt Service::LastCommandReceipt() const { return impl_->LastCommandReceipt(); }

	bool Service::Start(std::vector<WindowSpec> specs,
		Shutdown::FailedCleanupSignal failedCleanup)
	{
		return impl_->Start(std::move(specs), std::move(failedCleanup));
	}

	void Service::StopAndJoin() noexcept { impl_->Stop(); }
	void Service::Stop() noexcept { StopAndJoin(); }
	bool Service::Running() const noexcept { return impl_->Running(); }
	void Service::BeginShutdown() noexcept { impl_->BeginShutdown(); }
	HWND Service::Handle(WindowRole role) const noexcept { return impl_->Handle(role); }
	bool Service::Ready(WindowRole role) const noexcept { return impl_->Ready(role); }
	bool Service::AllReady() const noexcept { return impl_->AllReady(); }
	bool Service::OverlayReady() const noexcept { return impl_->OverlayReady(); }
	bool Service::SettingReady() const noexcept { return impl_->SettingReady(); }
	DWORD Service::OwnerThreadId(WindowRole role) const noexcept { return impl_->OwnerThreadId(role); }
	HWND Service::OverlayRoot() const noexcept { return impl_->OverlayRoot(); }
	std::wstring Service::Title(WindowRole role) const { return impl_->Title(role); }

	HWND Service::LastFocusWindow() noexcept
	{
		GUITHREADINFO information{};
		information.cbSize = sizeof(information);
		return GetGUIThreadInfo(0, &information) ? information.hwndFocus : nullptr;
	}

	bool Service::Create(WindowSpec spec) { return impl_->Create(std::move(spec)); }
	bool Service::Destroy(WindowRole role) { return impl_->Destroy(role); }
	bool Service::Show(WindowRole role) { return impl_->Show(role); }
	bool Service::Hide(WindowRole role) { return impl_->Hide(role); }
	bool Service::HideAllUserWindows() { return impl_->HideAllUserWindows(); }
	bool Service::RequestHideAllUserWindows() { return impl_->RequestHideAllUserWindows(); }
	bool Service::SetDrawpadSurfaceVisibility(DrawpadSurfaceVisibility visibility,
		std::function<bool()> stillDesired)
	{
		return impl_->SetDrawpadSurfaceVisibility(visibility,
			std::move(stillDesired));
	}
	bool Service::SetBounds(WindowRole role, const RECT& bounds) { return impl_->SetBounds(role, bounds); }
	bool Service::SetClickThrough(WindowRole role, bool enabled,
		std::function<bool()> stillDesired)
	{ return impl_->SetClickThrough(role, enabled, std::move(stillDesired)); }
	bool Service::SetSettingOwnedByDrawpad(bool enabled)
	{
		return impl_->SetSettingOwnedByDrawpad(enabled);
	}
		bool Service::SetExtendedStyleFlags(WindowRole role, DWORD setMask, DWORD clearMask)
		{
			return impl_->SetExtendedStyleFlags(role, setMask, clearMask);
		}
		bool Service::EnterWhiteboardWindowMode(std::function<bool()> stillDesired)
		{
			return impl_->SetWhiteboardMode(true, std::move(stillDesired));
		}
		bool Service::LeaveWhiteboardWindowMode(std::function<bool()> stillDesired)
		{
			return impl_->SetWhiteboardMode(false, std::move(stillDesired));
		}
		bool Service::WhiteboardWindowMode() const noexcept
		{
			return impl_->WhiteboardWindowMode();
		}
		bool Service::WhiteboardWindowGroupMinimized() const noexcept
		{
			return impl_->WhiteboardWindowGroupMinimized();
		}
		bool Service::DispatchWhiteboardClose(HWND hwnd) noexcept
		{
			return impl_->DispatchWhiteboardClose(hwnd);
		}
		bool Service::MinimizeWhiteboardWindowGroup()
		{
			return impl_->MinimizeWhiteboardWindowGroup();
		}
		bool Service::RestoreWhiteboardWindowGroup()
		{
			return impl_->RestoreWhiteboardWindowGroup();
		}
		bool Service::RequestWhiteboardActivation(std::function<bool()> stillDesired)
		{
			return impl_->RequestWhiteboardActivation(std::move(stillDesired));
		}
		bool Service::CancelPointerCapture()
		{
			return impl_->CancelPointerCapture();
		}
		bool Service::RequestTopmostRefresh(DWORD timeoutMilliseconds)
		{ return impl_->RequestTopmostRefresh(timeoutMilliseconds); }
		void Service::SetTopmostRefreshObserver(std::function<void()> callback)
		{
			impl_->SetTopmostRefreshObserver(std::move(callback));
		}
		bool Service::SetOverlayTopmost(bool topmost, std::function<bool()> stillDesired) { return impl_->SetOverlayTopmost(topmost, std::move(stillDesired)); }
		bool Service::OverlayTopmost() const noexcept { return impl_->OverlayTopmost(); }
		bool Service::SetOverlayFullscreen(bool fullscreen, std::function<bool()> stillDesired)
		{
			return impl_->SetOverlayFullscreen(fullscreen, std::move(stillDesired));
		}
		bool Service::OverlayFullscreen() const noexcept { return impl_->OverlayFullscreen(); }
		bool Service::PromotePptWindow(WindowRole role) { return impl_->PromotePptWindow(role); }
		bool Service::BindMessages(WindowRole role, const Message::BindOptions& options) { return impl_->BindMessages(role, options); }
	bool Service::UnbindMessages(WindowRole role) { return impl_->UnbindMessages(role); }

	bool Service::Enqueue(WindowRole role, Message::Message message)
	{
		auto* channel = impl_->Channel(role);
		return channel && channel->Enqueue(message);
	}

	bool Service::Get(
		WindowRole role,
		Message::Message& message,
		Message::Filter filter,
		DWORD timeoutMilliseconds)
	{
		auto* channel = impl_->Channel(role);
		return channel && channel->Get(message, filter, timeoutMilliseconds);
	}

	bool Service::TryGet(WindowRole role, Message::Message& message, Message::Filter filter)
	{
		auto* channel = impl_->Channel(role);
		return channel && channel->TryGet(message, filter);
	}

	std::size_t Service::Clear(WindowRole role, Message::Filter filter)
	{
		auto* channel = impl_->Channel(role);
		return channel ? channel->Clear(filter) : 0;
	}

	std::size_t Service::MessageCount(WindowRole role) const noexcept
	{
		const auto* channel = impl_->Channel(role);
		return channel ? channel->Size() : 0;
	}

	std::uint64_t Service::DroppedMessageCount(WindowRole role) const noexcept
	{
		const auto* channel = impl_->Channel(role);
		return channel ? channel->DroppedCount() : 0;
	}

	Service& GetService() noexcept
	{
		return processService;
	}

	WindowRole RoleFromHandle(HWND hwnd) noexcept
	{
		if (!hwnd) return WindowRole::Count;
		for (std::size_t index = 0; index < RoleCount; ++index)
		{
			const auto role = static_cast<WindowRole>(index);
			if (processService.Handle(role) == hwnd) return role;
		}
		return WindowRole::Count;
	}

	bool Enqueue(HWND hwnd, Message::Message message)
	{
		const auto role = RoleFromHandle(hwnd);
		if (role == WindowRole::Count) return false;
		message.hwnd = hwnd;
		return processService.Enqueue(role, message);
	}

	bool Get(HWND hwnd, Message::Message& message, Message::Filter filter,
		DWORD timeoutMilliseconds)
	{
		const auto role = RoleFromHandle(hwnd);
		return role != WindowRole::Count && processService.Get(
			role, message, filter, timeoutMilliseconds);
	}

	bool TryGet(HWND hwnd, Message::Message& message, Message::Filter filter)
	{
		const auto role = RoleFromHandle(hwnd);
		return role != WindowRole::Count && processService.TryGet(role, message, filter);
	}

	std::size_t Clear(HWND hwnd, Message::Filter filter)
	{
		const auto role = RoleFromHandle(hwnd);
		return role == WindowRole::Count ? 0 : processService.Clear(role, filter);
	}
}
