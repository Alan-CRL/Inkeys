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
#include "Window.CandidateMailbox.h"
#include "Window.PresentationLease.h"
#include "Window.NativeEventMailbox.h"
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

import Inkeys.Display;

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

	void LogWhiteboardAnchorState(const wchar_t* eventName, HWND hwnd,
		bool groupMinimized) noexcept
	{
		RECT bounds{};
		const bool boundsValid = hwnd && GetWindowRect(hwnd, &bounds);
		wchar_t diagnostic[384]{};
		swprintf_s(diagnostic,
			L"[WhiteboardWindow] event=%s hwnd=0x%llX visible=%u iconic=%u groupMinimized=%u "
			L"boundsOk=%u bounds=(%ld,%ld,%ld,%ld)\n", eventName,
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(hwnd)),
			hwnd && IsWindowVisible(hwnd) ? 1u : 0u, hwnd && IsIconic(hwnd) ? 1u : 0u,
			groupMinimized ? 1u : 0u, boundsValid ? 1u : 0u,
			bounds.left, bounds.top, bounds.right, bounds.bottom);
		WriteWindowDiagnostic(diagnostic, false);
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
		// 系统命令只提交最新视图意图，不能绕过 Prepare→Frame→Complete 直接显示旧画布。
		if (message == WM_SYSCOMMAND || message == WM_SIZE)
		{
			auto& service = Inkeys::Window::GetService();
			if (service.Handle(WindowRole::Freeze) == hwnd)
			{
				if (message == WM_SYSCOMMAND)
				{
					const UINT command = static_cast<UINT>(wParam & 0xfff0u);
					if ((command == SC_MINIMIZE || command == SC_RESTORE)
						&& service.DispatchWhiteboardViewRequest(hwnd, command == SC_MINIMIZE)) return 0;
				}
				else if (wParam == SIZE_MINIMIZED || wParam == SIZE_RESTORED)
				{
					LogWhiteboardAnchorState(wParam == SIZE_MINIMIZED ? L"native-size-minimized" : L"native-size-restored", hwnd,
						service.WhiteboardWindowGroupMinimized());
					if (service.ConfirmWhiteboardWindowView(hwnd, wParam == SIZE_MINIMIZED)) return 0;
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
		struct NativeEventPayload
		{
			WindowRole role = WindowRole::Bar;
			HWND hwnd = nullptr;
			std::uint64_t generation = 0;
			std::chrono::steady_clock::time_point deadline{};
		};
		using NativeEventMailbox = Detail::NativeEventMailbox<NativeEventPayload>;
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
			promoteEvents_.Invalidate(); refreshEvents_.Invalidate();
			workspaceNativeRevision_->fetch_add(1, std::memory_order_acq_rel);
			candidateMailbox_.InvalidateRoles((std::uint64_t{1} << RoleCount) - 1);
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

		[[nodiscard]] bool Show(WindowRole role, std::function<bool()> stillDesired = {})
		{
			// 已加入画布链的设置在显示前同步抬升根；选择态保留普通窗口语义。
			if (IsSetting(role) && !PrepareSettingZOrder(true)) return false;
			Command command; command.type = CommandType::Show; command.role = role;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
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

		[[nodiscard]] bool Hide(WindowRole role, std::function<bool()> stillDesired = {})
		{
			Command command; command.type = CommandType::Hide; command.role = role;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
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

		[[nodiscard]] bool SetBounds(WindowRole role, const RECT& bounds, std::function<bool()> stillDesired = {})
		{
			Command command;
			command.type = CommandType::SetBounds;
			command.role = role;
			command.bounds = bounds;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
		}

		[[nodiscard]] SurfacePresentationLease TryAcquirePresentationLease(WindowRole first, WindowRole second) noexcept
		{
			if (!running_.load(std::memory_order_acquire) || shutdownRequested_.load(std::memory_order_acquire) || !IsValidRole(first)
				|| (!IsUiPopup(first) && first != WindowRole::Freeze)
				|| (second != WindowRole::Count && (!IsValidRole(second) || (!IsUiPopup(second) && second != WindowRole::Freeze)))) return {};
			const auto mask = (std::uint64_t{1} << RoleIndex(first))
				| (second == WindowRole::Count ? 0 : std::uint64_t{1} << RoleIndex(second));
			return AcquirePresentationLeaseMask(mask);
		}

		[[nodiscard]] std::uint64_t PublishCandidate(const WindowCandidate& candidate)
		{
			if (!running_.load(std::memory_order_acquire) || candidate.count == 0 || candidate.count > candidate.targets.size()) return 0;
			std::uint64_t mask = 0;
			for (std::size_t i = 0; i < candidate.count; ++i)
			{
				const auto& target = candidate.targets[i];
				if (shutdownRequested_.load(std::memory_order_acquire)
					&& (target.applyBounds || target.promote || target.visibility != WindowCandidateVisibility::Hide)) return 0;
				if ((!IsUiPopup(target.role) && target.role != WindowRole::Freeze) || !target.hwnd || !target.generation
					|| (mask & (std::uint64_t{1} << RoleIndex(target.role)))) return 0;
				mask |= std::uint64_t{1} << RoleIndex(target.role);
			}
			CandidatePayload payload{ candidate, std::chrono::steady_clock::now(), candidateDiagnosticsEnabled_.load(std::memory_order_relaxed) };
			const auto publication = candidateMailbox_.Publish(RoleIndex(candidate.targets[0].role), mask, payload);
			if (!publication.revision) return 0;
			if (payload.measured)
			{
				candidatesPublished_.fetch_add(1, std::memory_order_relaxed);
				if (publication.coalesced) candidatesCoalesced_.fetch_add(1, std::memory_order_relaxed);
				auto maximum = candidateMaxQueueDepth_.load(std::memory_order_relaxed);
				while (maximum < publication.depth && !candidateMaxQueueDepth_.compare_exchange_weak(maximum, publication.depth, std::memory_order_relaxed)) {}
			}
			if (overlayEvent_) SetEvent(overlayEvent_);
			return publication.revision;
		}

		[[nodiscard]] std::optional<WindowCandidateReceipt> TryReadCandidateReceipt(WindowRole role, std::uint64_t revision) const
		{
			return candidateMailbox_.TryRead(RoleIndex(role), revision);
		}
		void RequestCancelCandidatePreservingReceipt(WindowRole role, std::uint64_t revision) noexcept
		{
			candidateMailbox_.RequestCancel(RoleIndex(role), revision);
			if (overlayEvent_) SetEvent(overlayEvent_);
		}
		[[nodiscard]] std::optional<WindowCandidateReceipt> TryReadCandidateRetirement(WindowRole role, std::uint64_t revision) const
		{
			auto receipt = candidateMailbox_.TryReadRetirement(RoleIndex(role), revision);
			if (!receipt || receipt->phase == CommandPhase::Queued || receipt->phase == CommandPhase::Executing) return {};
			// 退休读回只用来协调真实几何；旧语义不能凭此开放输入或确认新 UI-ready。
			if (!candidateMailbox_.Current(RoleIndex(role), revision) && receipt->phase == CommandPhase::Committed)
			{ receipt->phase = CommandPhase::Cancelled; receipt->completedMask = 0; }
			return receipt;
		}
		[[nodiscard]] bool CandidateCurrent(WindowRole role, std::uint64_t revision) const noexcept
		{
			return candidateMailbox_.Current(RoleIndex(role), revision);
		}
		void CancelCandidate(WindowRole role, std::uint64_t revision) noexcept
		{
			candidateMailbox_.Cancel(RoleIndex(role), revision);
			if (overlayEvent_) SetEvent(overlayEvent_);
		}
		[[nodiscard]] std::optional<WindowSurfaceState> SurfaceSnapshot(WindowRole role) const
		{
			if (!IsValidRole(role)) return {};
			std::unique_lock lock(surfaceSnapshotsMutex_, std::try_to_lock);
			if (!lock.owns_lock()) return {};
			return surfaceSnapshots_[RoleIndex(role)];
		}
		void SetCandidateDiagnosticsEnabled(bool enabled) noexcept { candidateDiagnosticsEnabled_.store(enabled, std::memory_order_relaxed); }
		[[nodiscard]] WindowCandidateDiagnostics CandidateDiagnostics() const noexcept
		{
			return { candidatesPublished_.load(std::memory_order_relaxed), candidatesCoalesced_.load(std::memory_order_relaxed),
				candidatesStale_.load(std::memory_order_relaxed), candidatesExecuted_.load(std::memory_order_relaxed),
				candidateBoundsChanges_.load(std::memory_order_relaxed), candidateVisibilityChanges_.load(std::memory_order_relaxed),
				candidatePromotes_.load(std::memory_order_relaxed), candidateMailbox_.Depth(), candidateMaxQueueDepth_.load(std::memory_order_relaxed) };
		}

		[[nodiscard]] std::optional<WorkspaceWindowPrepared> PrepareWorkspaceWindowPolicy(
			WorkspaceWindowPolicy policy, std::function<bool()> stillDesired)
		{
			Command command; command.type = CommandType::PrepareWorkspacePolicy; command.role = WindowRole::Freeze;
			command.workspacePolicy = policy; command.stillDesired = std::move(stillDesired);
			command.workspaceResult = std::make_shared<std::optional<WorkspaceWindowPrepared>>();
			const auto result = command.workspaceResult;
			return Submit(std::move(command)) ? *result : std::nullopt;
		}
		[[nodiscard]] bool CompleteWorkspaceWindowPolicy(WorkspaceWindowPolicy policy,
			const WorkspaceWindowPrepared& prepared, std::function<bool()> stillDesired)
		{
			Command command; command.type = CommandType::CompleteWorkspacePolicy; command.role = WindowRole::Freeze;
			command.workspacePolicy = policy; command.workspacePrepared = prepared;
			command.stillDesired = std::move(stillDesired);
			return Submit(std::move(command));
		}

		[[nodiscard]] WorkspaceWindowReceipt RequestPrepareWorkspaceWindowPolicy(WorkspaceWindowPolicy policy, std::function<bool()> stillDesired)
		{
			try
			{
				Command command; command.type = CommandType::PrepareWorkspacePolicy; command.role = WindowRole::Freeze;
				command.workspacePolicy = policy; command.stillDesired = std::move(stillDesired);
				command.workspaceResult = std::make_shared<std::optional<WorkspaceWindowPrepared>>();
				command.workspaceRequest = std::make_shared<WorkspaceRequestState>();
				return SubmitWorkspaceRequest(std::move(command));
			}
			catch (...) { return {}; }
		}
		[[nodiscard]] WorkspaceWindowReceipt RequestCompleteWorkspaceWindowPolicy(WorkspaceWindowPolicy policy,
			const WorkspaceWindowPrepared& prepared, std::function<bool()> stillDesired)
		{
			try
			{
				Command command; command.type = CommandType::CompleteWorkspacePolicy; command.role = WindowRole::Freeze;
				command.workspacePolicy = policy; command.workspacePrepared = prepared; command.stillDesired = std::move(stillDesired);
				command.workspaceRequest = std::make_shared<WorkspaceRequestState>();
				return SubmitWorkspaceRequest(std::move(command));
			}
			catch (...) { return {}; }
		}
		void SetWorkspaceProgressObserver(std::function<void()> callback)
		{
			std::unique_lock lock(workspaceObserverMutex_);
			workspaceProgressObserver_ = {};
			if (!workspaceObserverExecuting_) workspaceObserverCondition_.wait(lock, [this] { return workspaceObserverActiveCalls_ == 0; });
			workspaceProgressObserver_ = std::move(callback);
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
			if (overlayRecoveryActive_.load(std::memory_order_acquire) && matches()) return true;
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

		[[nodiscard]] bool DispatchWhiteboardViewRequest(HWND hwnd, bool minimized) noexcept
		{
			if (!hwnd || hwnd != Handle(WindowRole::Freeze) || OwnerThreadId(WindowRole::Freeze) != GetCurrentThreadId()) return false;
			const auto& spec = records_[RoleIndex(WindowRole::Freeze)].activeSpec;
			bool allowed = true;
			try { if (minimized && spec && spec->whiteboardMinimizeAllowed) allowed = spec->whiteboardMinimizeAllowed(); } catch (...) { allowed = false; }
			return spec && DispatchWhiteboardViewIntent(true, WhiteboardWindowMode(), minimized,
				allowed, spec->whiteboardViewRequested);
		}
		[[nodiscard]] bool ConfirmWhiteboardWindowView(HWND hwnd, bool minimized) noexcept
		{
			if (!hwnd || hwnd != Handle(WindowRole::Freeze) || OwnerThreadId(WindowRole::Freeze) != GetCurrentThreadId()
				|| !WhiteboardWindowMode() || (IsIconic(hwnd) != FALSE) != minimized) return false;
			if (whiteboardGroupMinimized_.exchange(minimized, std::memory_order_acq_rel) != minimized)
			{ candidateMailbox_.InvalidateRoles((std::uint64_t{1} << RoleCount) - 1); workspaceNativeRevision_->fetch_add(1, std::memory_order_acq_rel); ApplyOverlayFullscreen(OverlayRoot()); NotifyWhiteboardMinimizedChanged(); }
			return true;
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
			const HWND anchor = Handle(WindowRole::Freeze);
			if (!CommandCurrent(command) || shutdownRequested_.load(std::memory_order_acquire)
				|| !WhiteboardWindowMode() || WhiteboardWindowGroupMinimized()
				|| !anchor || !IsWindowVisible(anchor) || IsIconic(anchor)) return false;
			// 原生前台调用可能同步进入 owned Setting；状态线程不能等待它才能完成白板进入。
			return Submit(std::move(command), false);
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

		[[nodiscard]] bool PublishNativeEvent(NativeEventMailbox& mailbox, WindowRole role)
		{
			if (!running_.load(std::memory_order_acquire) || shutdownRequested_.load(std::memory_order_acquire)) return false;
			const auto generation = records_[RoleIndex(role)].generation.load(std::memory_order_acquire);
			const HWND hwnd = Handle(role);
			if (!hwnd || !generation || generation != records_[RoleIndex(role)].generation.load(std::memory_order_acquire)) return false;
			const auto revision = mailbox.Publish({ role, hwnd, generation, std::chrono::steady_clock::now() + std::chrono::milliseconds(1500) });
			if (revision && overlayEvent_) SetEvent(overlayEvent_);
			return revision != 0;
		}
		[[nodiscard]] bool RequestPromotePptWindow(WindowRole role)
		{
			return IsPpt(role) && PublishNativeEvent(promoteEvents_, role);
		}
		[[nodiscard]] bool RequestTopmostRefreshAsync()
		{
			return PublishNativeEvent(refreshEvents_, Handle(WindowRole::MagnifierHost) ? WindowRole::MagnifierHost : WindowRole::Freeze);
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
			PrepareWorkspacePolicy,
			CompleteWorkspacePolicy,
			SetWhiteboardWindowMode,
			SetOverlayTopmost,
			SetOverlayFullscreen,
			MinimizeWhiteboardGroup,
			RestoreWhiteboardGroup,
			ActivateWhiteboard,
			CancelPointerCapture,
			RefreshTopmost,
			PromotePpt,
			ApplyCandidate,
			BindMessages,
			UnbindMessages,
			Count,
		};

		static constexpr std::size_t CommandCount =
			static_cast<std::size_t>(CommandType::Count);

		struct CandidatePayload
		{
			WindowCandidate value;
			std::chrono::steady_clock::time_point submittedAt{};
			bool measured = false;
		};
		using CandidateMailbox = Detail::CandidateMailbox<CandidatePayload, WindowCandidateReceipt, RoleCount>;
		using CandidateWork = CandidateMailbox::Work;


		struct WorkspaceRequestState
		{
			std::optional<WorkspaceWindowPrepared> prepared;
			std::uint64_t nativeRevision = 0;
		};

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
			WorkspaceWindowPolicy workspacePolicy;
			WorkspaceWindowPrepared workspacePrepared;
			std::shared_ptr<std::optional<WorkspaceWindowPrepared>> workspaceResult;
			std::shared_ptr<WorkspaceRequestState> workspaceRequest;
			WindowRole identityRole = WindowRole::Count;
			HWND identityWindow = nullptr;
			std::uint64_t identityGeneration = 0;
			bool notifyTopmost = false;
			unsigned nativeEventKind = 0;
			std::uint64_t nativeEventRevision = 0;
			Message::BindOptions bindOptions{};
			std::optional<WindowSpec> spec;
			std::shared_ptr<Detail::CommandCompletion> completion;
			std::uint64_t intentVersion = 0;
			Detail::CommandDeadline deadline;
			std::optional<CandidateWork> candidate;
		};

		struct CandidateRetirement
		{
			CandidateWork work;
			std::shared_ptr<Detail::CommandCompletion> completion;
			DWORD error = ERROR_SUCCESS;
			SurfacePresentationLease lease;
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
			bool activationStyleFramePending = false;
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
			workspaceNativeRevision_->fetch_add(1, std::memory_order_acq_rel);
			workspaceProgressPending_ = false;
			promoteEvents_.Invalidate(); refreshEvents_.Invalidate();
			pendingPromote_.reset(); pendingRefresh_.reset(); nativeEventRetryAt_.reset();
			candidateMailbox_.Reset();
			for (auto& retirement : candidateRetirements_) retirement.reset();
			{
				std::scoped_lock lock(surfaceSnapshotsMutex_);
				for (std::size_t i = 0; i < RoleCount; ++i)
				{ surfaceSnapshots_[i] = {}; surfaceSnapshots_[i].role = static_cast<WindowRole>(i); surfaceSnapshots_[i].known = true; }
			}
			canvasPolicyDirty_ = false;
			canvasPolicyApplying_ = false;
			canvasPolicyRetry_ = false;
			commandApplying_.fill(false);
			safetyCleanupRetry_.fill(false);
			safetyOverrides_.fill({});
			for (auto& recovery : commandRecovery_) recovery.reset();
			overlayRecoveryActive_.store(false, std::memory_order_release);
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
				record.activationStyleFramePending = false;
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
			WakeSettingModalLoop(); // stop 后不再接收命令，但仍须唤醒 USER32 的嵌套移动循环。
			if (overlayThread_.joinable()) overlayThread_.join();
			if (settingThread_.joinable()) settingThread_.join();

			Detail::StopShellFullscreen(shellFullscreen_);
			shellFullscreen_.reset();
			workspaceNativeRevision_->fetch_add(1, std::memory_order_acq_rel);
			candidateMailbox_.Reset();
			for (auto& retirement : candidateRetirements_) retirement.reset(); // 关闭 wake HANDLE 前释放 owner 持有的租约。
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
					PublishNativeSnapshots(settingGroup);
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
				DWORD timeout = needsRecovery ? 250 : (!settingGroup && canvasPolicyRetry_
					&& !shutdownRequested_.load(std::memory_order_acquire)
					? (canvasPolicyRetryAt_ <= now ? 0 : static_cast<DWORD>(
						std::chrono::duration_cast<std::chrono::milliseconds>(canvasPolicyRetryAt_ - now).count() + 1))
					: INFINITE);
				if (!settingGroup)
					for (const auto& deadline : { leaseDeferredDeadline_, candidateLeaseDeadline_, nativeEventRetryAt_ })
						if (deadline)
						{
							const DWORD leaseTimeout = *deadline <= now ? 0 : static_cast<DWORD>(
								std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count() + 1);
							timeout = (std::min)(timeout, leaseTimeout);
						}
				const DWORD waitResult = eventHandle
					? MsgWaitForMultipleObjectsEx(
						1, &eventHandle, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE)
					: MsgWaitForMultipleObjectsEx(
						0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

				if (eventHandle && waitResult == WAIT_OBJECT_0)
				{
					ResetEvent(eventHandle);
					DrainCommands(queue);
					if (!settingGroup) { DrainNativeEvents(); DrainCandidates(); }
				}
				else if (waitResult == WAIT_TIMEOUT && !settingGroup)
				{
					const auto expiredAt = std::chrono::steady_clock::now();
					if (leaseDeferredDeadline_ && expiredAt >= *leaseDeferredDeadline_) DrainCommands(queue);
					if (candidateLeaseDeadline_ && expiredAt >= *candidateLeaseDeadline_) DrainCandidates();
					if (nativeEventRetryAt_ && expiredAt >= *nativeEventRetryAt_) DrainNativeEvents();
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
					{
						const MessageDispatch::Scope dispatchScope(message);
						const bool changesNativeState = message.message == WM_SYSCOMMAND || message.message == WM_SIZE
							|| message.message == WM_WINDOWPOSCHANGED || message.message == WM_SHOWWINDOW
							|| message.message == WM_DPICHANGED || message.message == WM_DESTROY;
						if (changesNativeState && !settingGroup) { candidateMailbox_.InvalidateRoles((std::uint64_t{1} << RoleCount) - 1); workspaceNativeRevision_->fetch_add(1, std::memory_order_acq_rel); }
						DispatchMessageW(&message);
						if (changesNativeState) PublishNativeSnapshots(settingGroup);
					}
					// 私有 wake 可能先于 native loop 被消费；退栈后先排空隐藏，再取下一条鼠标消息。
					if (settingGroup && eventHandle && WaitForSingleObject(eventHandle, 0) == WAIT_OBJECT_0)
					{
						ResetEvent(eventHandle);
						if (!token.stop_requested()) DrainCommands(queue);
					}
				}
				if (!eventHandle)
					DrainCommands(queue);
				if (commandRecovery_[settingGroup ? 1 : 0])
				{
					commandApplying_[settingGroup ? 1 : 0] = true;
					(void)RetryCommandRecovery(settingGroup);
					PublishNativeSnapshots(settingGroup);
					commandApplying_[settingGroup ? 1 : 0] = false;
				}
				if (safetyCleanupRetry_[settingGroup ? 1 : 0])
				{
					commandApplying_[settingGroup ? 1 : 0] = true;
					(void)RetrySafetyCleanup(settingGroup);
					PublishNativeSnapshots(settingGroup);
					commandApplying_[settingGroup ? 1 : 0] = false;
				}
				if (!settingGroup)
				{
					RefreshCandidateRetirements();
					DrainWorkspaceProgress();
					std::uint64_t retiring = 0;
					for (std::size_t slot = 0; slot < RoleCount; ++slot)
						if (candidateRetirements_[slot]) retiring |= std::uint64_t{1} << slot;
					// 在途回滚的槽等既有 250ms 期限；不能因其最新候选仍排队而自唤醒忙转。
					if (Detail::CandidatePumpHasWork(candidateMailbox_.PendingMask(), retiring | candidateLeaseBlocked_) && overlayEvent_) SetEvent(overlayEvent_);
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
				PublishNativeSnapshots(settingGroup);
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
			if (spec.role == WindowRole::Freeze || spec.role == WindowRole::Drawpad)
			{
				record.channel->SetCallback([this, role = spec.role, callback = spec.messageCallback](
					HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
				{
					if (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE
						&& Handle(role) == hwnd && WhiteboardWindowMode()
						&& !WhiteboardWindowGroupMinimized() && shellFullscreen_)
						// 两个可激活白板表面共用一个绝对状态邮箱，WndProc 不等待 Explorer。
						shellFullscreen_->Invalidate();
					return callback ? callback(hwnd, message, wParam, lParam) : Message::Reply{};
				});
			}
			else if (spec.messageCallback)
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
			if (role == WindowRole::Freeze || role == WindowRole::Drawpad || hwnd == OverlayRoot())
				ApplyOverlayFullscreen(OverlayRoot(), true);
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
			record.activationStyleFramePending = false;
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
			record.activationStyleFramePending = false;
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

		void WakeSettingModalLoop() noexcept
		{
			const auto& record = records_[RoleIndex(WindowRole::Setting)];
			const auto generation = record.generation.load(std::memory_order_acquire);
			const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
			if (hwnd && generation && generation == record.generation.load(std::memory_order_acquire))
				(void)PostMessageW(hwnd, MessageDispatch::SettingCancelModalMessage,
					static_cast<WPARAM>(generation), 0);
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
			if (IsSetting(role) && (type == CommandType::Hide || type == CommandType::HideAll))
				WakeSettingModalLoop();
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
			if (type == CommandType::CompleteWorkspacePolicy) type = CommandType::PrepareWorkspacePolicy;
			if (type == CommandType::RestoreWhiteboardGroup) type = CommandType::MinimizeWhiteboardGroup;
			return static_cast<std::size_t>(type);
		}

		[[nodiscard]] SurfacePresentationLease AcquirePresentationLeaseMask(std::uint64_t mask) noexcept
		{
			if (!Detail::TryAcquirePresentationBits(presentationLeaseBits_, mask)) return {};
			return SurfacePresentationLease(&presentationLeaseBits_, presentationRevisions_.data(), mask, overlayEvent_);
		}
		[[nodiscard]] std::uint64_t CommandPresentationMask(const Command& command) const noexcept
		{
			if (command.candidate)
			{
				std::uint64_t mask = 0;
				for (std::size_t i = 0; i < command.candidate->candidate.value.count; ++i)
					if (command.candidate->candidate.value.targets[i].applyBounds)
						mask |= std::uint64_t{1} << RoleIndex(command.candidate->candidate.value.targets[i].role);
				return mask;
			}
			if (command.type == CommandType::SetBounds && (IsUiPopup(command.role) || command.role == WindowRole::Freeze))
				return std::uint64_t{1} << RoleIndex(command.role);
			if (command.type == CommandType::PrepareWorkspacePolicy || command.type == CommandType::CompleteWorkspacePolicy)
			{
				return std::uint64_t{1} << RoleIndex(WindowRole::Freeze);
			}
			return 0;
		}

		[[nodiscard]] bool CandidateIdentityCurrent(const CandidateWork& work) const noexcept
		{
			if (!candidateMailbox_.Current(work)) return false;
			// 安全 Hide 也必须先核对代际，不能让旧候选锁存到重建后的 HWND。
			for (std::size_t i = 0; i < work.candidate.value.count; ++i)
			{
				const auto& target = work.candidate.value.targets[i];
				if (Handle(target.role) != target.hwnd || records_[RoleIndex(target.role)].generation.load(std::memory_order_acquire) != target.generation) return false;
			}
			return true;
		}

		[[nodiscard]] bool CommandCurrent(const Command& command) const noexcept
		{
			try
			{
				return (!command.completion || (running_.load(std::memory_order_acquire) && command.completion->Current()))
					&& (!command.candidate || CandidateIdentityCurrent(*command.candidate))
					&& (!command.nativeEventKind || (command.nativeEventKind == 1 ? promoteEvents_ : refreshEvents_).Current(command.nativeEventRevision))
					&& (command.identityRole == WindowRole::Count || (Handle(command.identityRole) == command.identityWindow
						&& records_[RoleIndex(command.identityRole)].generation.load(std::memory_order_acquire) == command.identityGeneration))
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
			if (!command.completion) command.completion = std::make_shared<Detail::CommandCompletion>(command.deadline);
			receiptOwner_ = this;
			lastReceipt_ = command.completion;
			const auto completion = command.completion;
			{
				// 队列锁只被短临界区持有；try_lock 失败不能当作拒绝（会被协调器计为 API 故障或吞掉激活意图）。
				std::scoped_lock lock(queue.mutex);
				if (!running_.load(std::memory_order_acquire)) { completion->Cancel(); return false; }
				command.intentVersion = intentVersions_[IntentIndex(command.type)][RoleIndex(command.role)]
					.fetch_add(1, std::memory_order_acq_rel) + 1;
				// 周期刷新只保留最新一份排队责任；在途操作不能伪装成已经取消。
				if (command.type == CommandType::RefreshTopmost || command.type == CommandType::PromotePpt)
					for (auto it = queue.commands.begin(); it != queue.commands.end(); )
						if (it->type == command.type && (command.type == CommandType::RefreshTopmost || it->role == command.role))
						{
							if (it->completion) it->completion->Cancel();
							it = queue.commands.erase(it);
						}
						else ++it;
				if (!wait || !ownerThreadId || ownerThreadId != GetCurrentThreadId()
					|| command.type == CommandType::ActivateWhiteboard) queue.commands.push_back(command);
			}
			// WndProc 重入不能越过尚未退栈的原生写入并提前报告新意图成功。
			if (wait && ownerThreadId && ownerThreadId == GetCurrentThreadId()
				&& command.type != CommandType::ActivateWhiteboard) return ExecuteTracked(command);
			if (eventHandle) SetEvent(eventHandle);
			if (IsSetting(command.role)
				&& (command.type == CommandType::Hide || command.type == CommandType::HideAll))
				WakeSettingModalLoop();
			return wait ? completion->Wait() : true;
		}

		[[nodiscard]] WorkspaceWindowReceipt SubmitWorkspaceRequest(Command command)
		{
			const auto state = command.workspaceRequest;
			const auto truth = workspaceNativeRevision_;
			const auto revision = command.workspacePolicy.revision;
			if (!revision) return {};
			command.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
			command.completion = std::make_shared<Detail::CommandCompletion>(command.deadline);
			const auto completion = command.completion;
			WorkspaceWindowReceipt receipt{ revision,
				CommandReceipt([completion] { return static_cast<CommandPhase>(completion->Phase()); }, [completion] { completion->Cancel(); }),
				[state] { return state->prepared; },
				[state, truth] { return state->nativeRevision != 0 && state->nativeRevision == truth->load(std::memory_order_acquire); } };
			if (!Submit(std::move(command), false)) return {};
			return receipt;
		}
		void NotifyWorkspaceProgress()
		{
			std::function<void()> callback;
			try
			{
				std::scoped_lock lock(workspaceObserverMutex_);
				callback = workspaceProgressObserver_;
				if (callback) ++workspaceObserverActiveCalls_;
			}
			catch (...) { return; }
			if (!callback) return;
			workspaceObserverExecuting_ = true;
			try { callback(); } catch (...) {}
			workspaceObserverExecuting_ = false;
			{ std::scoped_lock lock(workspaceObserverMutex_); --workspaceObserverActiveCalls_; }
			workspaceObserverCondition_.notify_all();
		}
		void DrainWorkspaceProgress()
		{
			if (!workspaceProgressPending_) return;
			workspaceProgressPending_ = false;
			NotifyWorkspaceProgress();
		}
		[[nodiscard]] bool TouchesWorkspaceTruth(const Command& command) const noexcept
		{
			if (command.type == CommandType::PrepareWorkspacePolicy || command.type == CommandType::CompleteWorkspacePolicy
				|| command.type == CommandType::SetWhiteboardWindowMode || command.type == CommandType::MinimizeWhiteboardGroup
				|| command.type == CommandType::RestoreWhiteboardGroup || command.type == CommandType::HideAll
				|| ((command.type == CommandType::Create || command.type == CommandType::Destroy) && !IsSetting(command.role) && !IsDisplayObserver(command.role))) return true;
			if (command.candidate)
			{
				for (std::size_t i = 0; i < command.candidate->candidate.value.count; ++i)
				{
					const auto& target = command.candidate->candidate.value.targets[i];
					if (target.role == WindowRole::Freeze && (target.applyBounds || target.visibility != WindowCandidateVisibility::Preserve)) return true;
				}
				return false;
			}
			return (IsCanvas(command.role) || command.role == WindowRole::Freeze)
				&& command.type != CommandType::RefreshTopmost && command.type != CommandType::SetOverlayTopmost
				&& command.type != CommandType::SetOverlayFullscreen && command.type != CommandType::ActivateWhiteboard;
		}

		[[nodiscard]] WindowSurfaceState ReadNativeSurface(WindowRole role) const noexcept
		{
			WindowSurfaceState state;
			state.role = role;
			state.hwnd = Handle(role);
			state.generation = records_[RoleIndex(role)].generation.load(std::memory_order_acquire);
			state.presentationRevision = presentationRevisions_[RoleIndex(role)].load(std::memory_order_acquire);
			state.known = !state.hwnd;
			if (state.hwnd && IsWindow(state.hwnd))
			{
				state.known = GetWindowRect(state.hwnd, &state.bounds) != FALSE;
				state.visible = IsWindowVisible(state.hwnd) != FALSE;
				state.iconic = IsIconic(state.hwnd) != FALSE;
			}
			return state;
		}
		void InvalidateNativeSnapshots(bool setting)
		{
			std::scoped_lock lock(surfaceSnapshotsMutex_);
			for (std::size_t i = 0; i < RoleCount; ++i)
				if (IsSetting(static_cast<WindowRole>(i)) == setting) surfaceSnapshots_[i].known = false;
		}
		void PublishNativeSnapshots(bool setting)
		{
			std::array<WindowSurfaceState, RoleCount> actual{};
			for (std::size_t i = 0; i < RoleCount; ++i)
				if (IsSetting(static_cast<WindowRole>(i)) == setting) actual[i] = ReadNativeSurface(static_cast<WindowRole>(i));
			std::scoped_lock lock(surfaceSnapshotsMutex_);
			for (std::size_t i = 0; i < RoleCount; ++i)
				if (IsSetting(static_cast<WindowRole>(i)) == setting) surfaceSnapshots_[i] = actual[i];
		}
		void PublishCandidateSnapshots(const WindowCandidate& candidate, bool invalidating = false)
		{
			std::array<WindowSurfaceState, 2> actual{};
			if (!invalidating)
				for (std::size_t i = 0; i < candidate.count; ++i) actual[i] = ReadNativeSurface(candidate.targets[i].role);
			std::scoped_lock lock(surfaceSnapshotsMutex_);
			for (std::size_t i = 0; i < candidate.count; ++i)
				if (invalidating) surfaceSnapshots_[RoleIndex(candidate.targets[i].role)].known = false;
				else surfaceSnapshots_[RoleIndex(candidate.targets[i].role)] = actual[i];
		}
		void PublishCandidateReceipt(const CandidateRetirement& retirement)
		{
			const auto& work = retirement.work;
			const auto& candidate = work.candidate.value;
			WindowCandidateReceipt receipt;
			receipt.revision = work.revision;
			receipt.frameSerial = candidate.frameSerial;
			receipt.workspaceRevision = candidate.workspaceRevision;
			receipt.phase = static_cast<CommandPhase>(retirement.completion->Phase());
			receipt.count = candidate.count;
			receipt.error = retirement.completion->NativeError() ? retirement.completion->NativeError() : retirement.error;
			receipt.rollbackError = retirement.completion->RollbackError();
			for (std::size_t i = 0; i < candidate.count; ++i) receipt.actual[i] = ReadNativeSurface(candidate.targets[i].role);
			if (receipt.phase == CommandPhase::Committed && candidateMailbox_.Current(work))
				receipt.completedMask = (1u << candidate.count) - 1u;
			else if (receipt.phase == CommandPhase::Committed) receipt.phase = CommandPhase::Cancelled;
			if (work.candidate.measured)
			{
				const auto completedAt = std::chrono::steady_clock::now();
				receipt.completedTicks = completedAt.time_since_epoch().count();
				receipt.latencyMicroseconds = std::chrono::duration<double, std::micro>(completedAt - work.candidate.submittedAt).count();
			}
			(void)candidateMailbox_.Complete(work, receipt);
		}
		void RefreshCandidateRetirements()
		{
			for (auto& retirement : candidateRetirements_)
				if (retirement && retirement->completion->Phase() != Detail::CommandPhase::Executing)
				{ PublishCandidateReceipt(*retirement); retirement.reset(); }
		}
		void DrainCandidates()
		{
			RefreshCandidateRetirements();
			candidateLeaseBlocked_ = 0;
			candidateLeaseDeadline_.reset();
			// 每次泵最多消费各固定槽一次；持续输入不能饿死 Win32 消息。
			for (std::size_t slot = 0; slot < RoleCount; ++slot)
			{
				if (candidateRetirements_[slot]) continue;
				auto work = candidateMailbox_.Take(slot);
				if (!work) continue;
				Command command;
				command.type = CommandType::ApplyCandidate;
				command.role = work->candidate.value.targets[0].role;
				command.candidate = *work;
				command.deadline = work->candidate.submittedAt + std::chrono::milliseconds(1500);
				const auto leaseMask = CommandPresentationMask(command);
				auto lease = leaseMask ? AcquirePresentationLeaseMask(leaseMask) : SurfacePresentationLease{};
				if (leaseMask && !lease && CandidateIdentityCurrent(*work) && !Detail::CommandExpired(command.deadline)
					&& candidateMailbox_.Defer(*work))
				{
					if (!candidateLeaseDeadline_ || *command.deadline < *candidateLeaseDeadline_) candidateLeaseDeadline_ = command.deadline;
					candidateLeaseBlocked_ |= std::uint64_t{1} << slot;
					continue;
				}
				try { command.completion = std::make_shared<Detail::CommandCompletion>(command.deadline); }
				catch (...)
				{
					// 接纳后尚未触及 HWND 的分配失败也必须有终态，不能逃出 owner 消息循环。
					WindowCandidateReceipt rejected;
					rejected.revision = work->revision;
					rejected.frameSerial = work->candidate.value.frameSerial;
					rejected.workspaceRevision = work->candidate.value.workspaceRevision;
					rejected.phase = CommandPhase::Rejected;
					rejected.count = work->candidate.value.count;
					rejected.error = ERROR_NOT_ENOUGH_MEMORY;
					for (std::size_t i = 0; i < rejected.count; ++i) rejected.actual[i] = ReadNativeSurface(work->candidate.value.targets[i].role);
					(void)candidateMailbox_.Complete(*work, rejected);
					continue;
				}
				if (work->candidate.measured)
				{
					candidatesExecuted_.fetch_add(1, std::memory_order_relaxed);
					if (!candidateMailbox_.Current(*work)) candidatesStale_.fetch_add(1, std::memory_order_relaxed);
				}
				candidateRetirements_[slot].emplace(CandidateRetirement{ *work, command.completion, ERROR_SUCCESS, std::move(lease) });
				SetLastError(ERROR_SUCCESS);
				const bool success = ExecuteTracked(command, {}, static_cast<bool>(candidateRetirements_[slot]->lease));
				candidateRetirements_[slot]->error = success ? ERROR_SUCCESS : (GetLastError() ? GetLastError() : ERROR_GEN_FAILURE);
				if (command.completion->Phase() != Detail::CommandPhase::Executing)
				{ PublishCandidateReceipt(*candidateRetirements_[slot]); candidateRetirements_[slot].reset(); }
			}
		}

		void DrainNativeEvents()
		{
			const auto now = std::chrono::steady_clock::now();
			const auto previousRetry = nativeEventRetryAt_;
			nativeEventRetryAt_.reset();
			const auto retryAt = [&](std::chrono::steady_clock::time_point value)
			{ if (!nativeEventRetryAt_ || value < *nativeEventRetryAt_) nativeEventRetryAt_ = value; };
			const auto drain = [&](NativeEventMailbox& mailbox, std::optional<NativeEventMailbox::Work>& pending, unsigned kind)
			{
				const auto latest = mailbox.Take();
				if (latest) pending = latest;
				if (!pending) return;
				if (!mailbox.Current(pending->revision) || now >= pending->payload.deadline)
				{ pending.reset(); return; }
				if (!latest && previousRetry && now < *previousRetry) { retryAt(*previousRetry); return; }
				Command command; command.type = kind == 1 ? CommandType::PromotePpt : CommandType::RefreshTopmost;
				command.role = kind == 1 ? pending->payload.role : WindowRole::Bar;
				command.identityRole = pending->payload.role; command.identityWindow = pending->payload.hwnd;
				command.identityGeneration = pending->payload.generation;
				command.deadline = pending->payload.deadline; command.nativeEventKind = kind;
				command.nativeEventRevision = pending->revision; command.notifyTopmost = kind == 2;
				try
				{
					command.completion = std::make_shared<Detail::CommandCompletion>(command.deadline);
					if (ExecuteTracked(command)) { pending.reset(); return; }
				}
				catch (...) {} // 尚未接触 HWND 的资源失败保留原期限，不在 UI 线程重试窗口政策。
				if (!mailbox.Current(pending->revision)) { pending.reset(); return; }
				retryAt((std::min)(pending->payload.deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(250)));
			};
			drain(promoteEvents_, pendingPromote_, 1);
			drain(refreshEvents_, pendingRefresh_, 2);
		}

		void DrainCommands(CommandQueue& queue)
		{
			if (&queue == &overlayCommands_) leaseDeferredDeadline_.reset();
			std::deque<Command> commands;
			{
				std::scoped_lock lock(queue.mutex);
				commands.swap(queue.commands);
			}
			for (auto& command : commands)
			{
				const auto mask = CommandPresentationMask(command);
				auto lease = mask ? AcquirePresentationLeaseMask(mask) : SurfacePresentationLease{};
				if (mask && !lease && CommandCurrent(command))
				{
					if (command.deadline && (!leaseDeferredDeadline_ || *command.deadline < *leaseDeferredDeadline_)) leaseDeferredDeadline_ = command.deadline;
					try { std::scoped_lock lock(queue.mutex); queue.commands.push_back(std::move(command)); }
					catch (...) { if (command.completion) { command.completion->Cancel(); command.completion->Retired(); } }
					continue;
				}
				(void)ExecuteTracked(command, std::move(lease));
			}
		}

		static constexpr std::array GroupRoles{ WindowRole::MagnifierHost, WindowRole::Freeze,
			WindowRole::DrawpadPresentation, WindowRole::Drawpad, WindowRole::PptBottomLeft,
			WindowRole::PptBottomRight, WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight, WindowRole::Bar };
		using GroupSnapshot = Detail::ZOrderSnapshot<10>;

		[[nodiscard]] std::optional<GroupSnapshot> CaptureGroupZOrder(HWND setting = nullptr) const noexcept
		{
			GroupSnapshot snapshot;
			for (std::size_t i = 0; i < GroupRoles.size(); ++i)
			{
				snapshot.windows[i] = Handle(GroupRoles[i]);
				snapshot.generations[i] = snapshot.windows[i]
					? records_[RoleIndex(GroupRoles[i])].generation.load(std::memory_order_acquire) : 0;
			}
			snapshot.windows[9] = setting;
			snapshot.generations[9] = setting ? records_[RoleIndex(WindowRole::Setting)].generation.load(std::memory_order_acquire) : 0;
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
				if (hwnd != Handle(role) || snapshot.generations[i] != records_[RoleIndex(role)].generation.load(std::memory_order_acquire)
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

		[[nodiscard]] bool GroupIdentityCurrent(const GroupSnapshot& identity) const noexcept
		{
			for (std::size_t i = 0; i < GroupRoles.size(); ++i)
				if (identity.windows[i] != Handle(GroupRoles[i]) || (identity.windows[i] &&
					(identity.generations[i] != records_[RoleIndex(GroupRoles[i])].generation.load(std::memory_order_acquire)
						|| !IsWindow(identity.windows[i])))) return false;
			return true;
		}

		[[nodiscard]] bool ReconcileRelativeOverlayGroup(const GroupSnapshot& identity,
			const Detail::PptZOrder& order, const Command* command = nullptr, bool survivingOnly = false)
		{
			const auto expected = ExpectedGroupOwners(identity);
			HWND bandRoot = nullptr;
			for (std::size_t i = 0; i < GroupRoles.size(); ++i)
				if (identity.windows[i] && !expected[i]) { bandRoot = identity.windows[i]; break; }
			struct Operations
			{
				Impl& service;
				const GroupSnapshot& identity;
				const Command* command;
				bool survivingOnly;
				HWND bandRoot;
				bool Current() const
				{
					if (!service.running_.load(std::memory_order_acquire)
						|| service.shutdownRequested_.load(std::memory_order_acquire)
						|| (command && !service.CommandCurrent(*command))) return false;
					for (std::size_t i = 0; i < GroupRoles.size(); ++i)
					{
						if (!identity.windows[i] && survivingOnly) continue;
						if (service.Handle(GroupRoles[i]) != identity.windows[i]) return false;
						if (identity.windows[i] && (!IsWindow(identity.windows[i]) || identity.generations[i] !=
							service.records_[RoleIndex(GroupRoles[i])].generation.load(std::memory_order_acquire))) return false;
					}
					return true;
				}
				bool Read(GroupSnapshot& result) const
				{
					const auto actual = service.CaptureGroupZOrder();
					if (!actual) return false;
					result = *actual;
					if (survivingOnly)
						for (std::size_t i = 0; i < GroupRoles.size(); ++i)
							if (!identity.windows[i])
							{
								result.windows[i] = result.owners[i] = nullptr;
								result.generations[i] = 0;
								result.ordinal[i] = Detail::MissingZOrder;
							}
					return true;
				}
				bool SetOwner(std::size_t i, HWND owner) const
				{
					if (!Current()) return false;
					SetLastError(ERROR_SUCCESS);
					const auto previous = SetWindowLongPtrW(identity.windows[i], GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
					return (previous || GetLastError() == ERROR_SUCCESS) && Current();
				}
				bool SetBand(std::size_t i, bool topmost, bool noOwner) const
				{
					if (!Current() || !bandRoot) return false;
					// 只有固定旧代际 root 显式切 band；其余成员从它继承，不能单独抬到全局 TOPMOST。
					const HWND after = identity.windows[i] == bandRoot ? (topmost ? HWND_TOPMOST : HWND_NOTOPMOST) : bandRoot;
					return SetWindowPos(identity.windows[i], after, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (noOwner ? SWP_NOOWNERZORDER : 0)) && Current();
				}
				bool PlaceAfter(std::size_t i, std::size_t anchor) const
				{
					return Current() && SetWindowPos(identity.windows[i], identity.windows[anchor], 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) && Current();
				}
			} operations{ *this, identity, command, survivingOnly, bandRoot };
			return Detail::ReconcileRelativeOverlayZOrder(identity, expected,
				overlayTopmost_.load(std::memory_order_acquire) && !WhiteboardWindowMode(), order, operations);
		}

		[[nodiscard]] bool CommitOverlayGroupPolicy(const Command* command = nullptr, HWND promoted = nullptr)
		{
			const HWND root = OverlayRoot();
			if (!root) return false;
			// mode=false 的离场及重入也只交接物理政策，不依赖前台许可或全局连续性。
			return Detail::CommitOverlayGroupPolicy([this] { return CaptureGroupZOrder(); },
				[this, command](const GroupSnapshot& identity, const Detail::PptZOrder& order)
				{ return ReconcileRelativeOverlayGroup(identity, order, command); },
				[this, root] { ApplyOverlayFullscreen(root); },
				[this, command] { return !command || CommandCurrent(*command); }, promoted);
		}

		[[nodiscard]] bool RefreshOverlayGroup(const Command* command = nullptr, HWND promoted = nullptr)
		{
			if (WhiteboardWindowMode()) return CommitOverlayGroupPolicy(command, promoted);
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
					if (!current() || member != Handle(GroupRoles[i]) || !IsWindow(member)
						|| before->generations[i] != records_[RoleIndex(GroupRoles[i])].generation.load(std::memory_order_acquire)) return false;
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
			WindowRecord* only = nullptr, std::function<bool()> stillCurrent = {}) noexcept
		{
			if (stillCurrent && !stillCurrent()) return false;
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
				std::function<bool()> current;
				bool Current() const { return !current || current(); }
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
					if (!Current()) return false;
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
					if (!Current()) return false;
					ShowWindow(keys[i].hwnd, SW_HIDE);
					return Checked(!IsWindowVisible(keys[i].hwnd));
				}
				bool Apply(std::size_t i) noexcept
				{
					if (!Current()) return false;
					const auto& policy = records[i]->canvasPolicy;
					Detail::CanvasRegion region;
					return Checked(Detail::BuildCanvasRegion(keys[i], policy.originalRegion.Get(), region)
						&& SetCanvasNonRude(keys[i].hwnd,
							Detail::CanvasNonRudeValue(whiteboard, policy.originalNonRude))
						&& Current() && SetCanvasRegion(keys[i].hwnd, region.Get()));
				}
				bool ApplyScene() noexcept
				{
					if (!Current()) return false;
					try { return !changeStyle || Checked(owner.ApplyWhiteboardActivationStyle(whiteboard, current)); }
					catch (...) { return Checked(false); }
				}
				bool RollbackScene() noexcept
				{
					if (!Current()) return false;
					try { return !changeStyle || Checked(owner.ApplyWhiteboardActivationStyle(previousMode, current)); }
					catch (...) { return Checked(false); }
				}
				bool Restore(std::size_t i) noexcept
				{
					if (!Current()) return false;
					const bool property = SetCanvasNonRude(keys[i].hwnd, snapshots[i].property);
					const bool region = Current() && SetCanvasRegion(keys[i].hwnd, snapshots[i].region.Get());
					return Checked(property && region);
				}
				bool RestoreVisibility(std::size_t i) noexcept
				{
					if (!Current()) return false;
					const bool visible = snapshots[i].visible
						&& !owner.shutdownRequested_.load(std::memory_order_acquire)
						&& !owner.SafetyHidden(keys[i].hwnd) && !owner.WhiteboardWindowGroupMinimized();
					ShowWindow(keys[i].hwnd, visible
						? (snapshots[i].iconic ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE) : SW_HIDE);
					return Checked((IsWindowVisible(keys[i].hwnd) != FALSE) == visible);
				}
			} operations{ *this, whiteboard, changeStyle,
				whiteboardWindowMode_.load(std::memory_order_acquire), stillCurrent };

			const auto failClosed = [this, &stillCurrent](DWORD error) noexcept
			{
				if (stillCurrent && !stillCurrent()) return false;
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
				if (stillCurrent && !stillCurrent()) return false;
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

		[[nodiscard]] bool ApplyWhiteboardWindowMode(bool whiteboard, std::function<bool()> current = {})
		{
			const bool changed = whiteboardWindowMode_.load(std::memory_order_acquire) != whiteboard;
			if (!EnsureCanvasPolicy(whiteboard, changed || canvasPolicyDirty_, nullptr, current)) return false;
			if (current && !current()) return false;
			if (changed)
			{
				// 双画布策略提交成功后再清最小化锚点，失败回滚不得发布新场景。
				if (!whiteboard && whiteboardGroupMinimized_.load(std::memory_order_acquire))
				{
					if (const HWND anchor = Handle(WindowRole::Freeze); anchor && IsIconic(anchor))
					{
						whiteboardGroupMinimized_.store(false, std::memory_order_release);
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

		[[nodiscard]] bool ApplyWhiteboardActivationStyle(bool whiteboard, std::function<bool()> current = {})
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
				std::uintptr_t generation = 0;
			};
			std::vector<WindowStyleSnapshot> snapshots;
			snapshots.reserve(std::size(roles));
			auto RestoreVisibility = [this](const WindowStyleSnapshot& snapshot) noexcept
			{
				if (!snapshot.hwnd || !IsWindow(snapshot.hwnd) || snapshot.generation !=
					reinterpret_cast<std::uintptr_t>(GetPropW(snapshot.hwnd, Detail::WindowGenerationProperty))) return;
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
				if (!snapshot.hwnd || !IsWindow(snapshot.hwnd) || snapshot.generation !=
					reinterpret_cast<std::uintptr_t>(GetPropW(snapshot.hwnd, Detail::WindowGenerationProperty))) return;
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
				if (current && !current()) { succeeded = false; break; }
				auto* record = Record(role);
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
				if (desired == previousStyle && !record->activationStyleFramePending) continue;
				record->activationStyleFramePending = true;
				snapshots.push_back({ hwnd, previousStyle,
					IsWindowVisible(hwnd) != FALSE, IsIconic(hwnd) != FALSE, false,
					reinterpret_cast<std::uintptr_t>(GetPropW(hwnd, Detail::WindowGenerationProperty)) });
				// Shell 要求动态 APPWINDOW/TOOLWINDOW 先隐藏，再 FrameChanged 后恢复。
				ShowWindow(hwnd, SW_HIDE);
				if (current && !current()) { succeeded = false; break; }
				SetLastError(ERROR_SUCCESS);
				const LONG_PTR previous = SetWindowLongPtrW(
					hwnd, GWL_EXSTYLE, desired);
				if (!previous && GetLastError() != ERROR_SUCCESS)
				{
					succeeded = false;
					break;
				}
				snapshots.back().styleApplied = true;
				if (current && !current()) { succeeded = false; break; }
				if (!SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOACTIVATE | SWP_FRAMECHANGED))
				{
					succeeded = false;
					break;
				}
				if (current && !current()) { succeeded = false; break; }
				record->activationStyleFramePending = false;
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

		[[nodiscard]] bool ApplyMinimizeWhiteboardGroup(std::function<bool()> current = {}) noexcept
		{
			if ((current && !current()) || !whiteboardWindowMode_.load(std::memory_order_acquire)) return false;
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
				if (current && !current()) return false;
				const HWND hwnd = Handle(role);
				if (!hwnd || !IsWindow(hwnd)) continue;
				whiteboardVisibleBeforeMinimize_[RoleIndex(role)] =
					IsWindowVisible(hwnd) != FALSE;
			}
			const HWND anchor = Handle(WindowRole::Freeze);
			if (!anchor || !IsWindow(anchor)) return false;
			// 先锁存状态再触发 WM_SIZE，避免系统消息重入时再次递归最小化。
			whiteboardGroupMinimized_.store(true, std::memory_order_release);
			ApplyOverlayFullscreen(OverlayRoot());
			(void)ApplyCancelPointerCapture();
			ShowWindow(anchor, SW_MINIMIZE);
			if ((current && !current()) || !IsIconic(anchor)) return false;
			for (const WindowRole role : roles)
			{
				if (current && !current()) return false;
				if (role == WindowRole::Freeze) continue;
				if (const HWND hwnd = Handle(role); hwnd && IsWindow(hwnd))
					ShowWindow(hwnd, SW_HIDE);
			}
			// Windows 自动隐藏 owned popup，显式恢复主栏以保留工作区入口。
			if (const HWND bar = Handle(WindowRole::Bar)) ShowWindow(bar, SafetyHidden(bar) ? SW_HIDE : SW_SHOWNOACTIVATE);
			NotifyWhiteboardMinimizedChanged();
			return true;
		}

		[[nodiscard]] bool ApplyRestoreWhiteboardGroup(bool preparing = false, std::function<bool()> current = {}) noexcept
		{
			if (shutdownRequested_.load(std::memory_order_acquire)) return false;
			if ((current && !current()) || !whiteboardWindowMode_.load(std::memory_order_acquire)) return false;
			if (!whiteboardGroupMinimized_.load(std::memory_order_acquire)) return true;
			if (!preparing && !EnsureCanvasPolicy(true)) return false;
			constexpr WindowRole roles[] = {
				WindowRole::MagnifierHost, WindowRole::Freeze,
				WindowRole::DrawpadPresentation, WindowRole::Drawpad,
				WindowRole::PptBottomLeft, WindowRole::PptBottomRight,
				WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight,
			};
			const HWND anchor = Handle(WindowRole::Freeze);
			if (!anchor || !IsWindow(anchor)) return false;
			constexpr std::array surfaces{WindowRole::DrawpadPresentation, WindowRole::Drawpad, WindowRole::Freeze};
			std::array<WorkspaceWindowSurface, surfaces.size()> hiddenTargets{};
			for (std::size_t i = 0; i < surfaces.size(); ++i)
			{ hiddenTargets[i].hwnd = Handle(surfaces[i]); hiddenTargets[i].generation = records_[RoleIndex(surfaces[i])].generation.load(std::memory_order_acquire); }
			const auto hidePreparedSurfaces = [&]
			{
				bool hidden = true;
				for (std::size_t i = 0; i < surfaces.size(); ++i)
					if (Handle(surfaces[i]) == hiddenTargets[i].hwnd && hiddenTargets[i].generation == records_[RoleIndex(surfaces[i])].generation.load(std::memory_order_acquire))
					{ ShowWindow(hiddenTargets[i].hwnd, SW_HIDE); hidden &= IsWindowVisible(hiddenTargets[i].hwnd) == FALSE; }
				return hidden;
			};
			if (preparing && !hidePreparedSurfaces()) return false;
			if (current && !current()) return false;
			// 先释放锁存再触发 WM_SIZE，系统恢复消息只需观察到一次结算。
			whiteboardGroupMinimized_.store(false, std::memory_order_release);
			// 标准非激活恢复存在锚点的系统显示瞬态；owned 窗口可自动显示，必须立即显式重隐。
			ShowWindow(anchor, preparing ? SW_SHOWNOACTIVATE : SW_RESTORE);
			// 即便旧业务 guard 在同步消息内失效，也先收束本次捕获的旧表面，不接触新代际。
			if (preparing && !hidePreparedSurfaces()) return false;
			if ((current && !current()) || IsIconic(anchor)) return false;
			if (preparing || shutdownRequested_.load(std::memory_order_acquire) || SafetyHidden(anchor) ||
				!whiteboardVisibleBeforeMinimize_[RoleIndex(WindowRole::Freeze)])
				ShowWindow(anchor, SW_HIDE);
			for (const WindowRole role : roles)
			{
				if (current && !current()) return false;
				if (shutdownRequested_.load(std::memory_order_acquire)) break;
				if (role == WindowRole::Freeze || (preparing && IsCanvas(role))) continue;
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
			ApplyOverlayFullscreen(OverlayRoot());
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

		[[nodiscard]] WorkspaceWindowSurface CaptureWorkspaceSurface(WindowRole role) const
		{
			WorkspaceWindowSurface surface;
			surface.hwnd = Handle(role);
			surface.generation = records_[RoleIndex(role)].generation.load(std::memory_order_acquire);
			if (!surface.hwnd || !GetWindowRect(surface.hwnd, &surface.bounds)) throw std::runtime_error("workspace surface");
			return surface;
		}
		[[nodiscard]] bool WorkspaceSurfaceMatches(WindowRole role, const WorkspaceWindowSurface& surface) const noexcept
		{
			RECT bounds{};
			return surface.hwnd && Handle(role) == surface.hwnd
				&& records_[RoleIndex(role)].generation.load(std::memory_order_acquire) == surface.generation
				&& GetWindowRect(surface.hwnd, &bounds) && EqualRect(&bounds, &surface.bounds);
		}
		[[nodiscard]] bool WorkspacePreparedMatches(const WorkspaceWindowPolicy& policy,
			const WorkspaceWindowPrepared& prepared) const noexcept
		{
			const auto display = Inkeys::Display::GetSnapshot();
			const auto* monitor = display ? display->Find(prepared.targetMonitor) : nullptr;
			return preparedWorkspace_ && preparedWorkspace_->policy == policy && prepared.policy == policy
				&& prepared.safetyRevision == overlaySafetyRevision_
				&& (!policy.whiteboard || WhiteboardWindowGroupMinimized() == policy.minimized)
				&& monitor && (!policy.whiteboard || display->Primary() == monitor)
				&& EqualRect(&monitor->bounds, &prepared.monitorBounds)
				&& monitor->effectiveDpiX == prepared.dpiX && monitor->effectiveDpiY == prepared.dpiY
				&& WorkspaceSurfaceMatches(WindowRole::Freeze, prepared.freeze)
				&& WorkspaceSurfaceMatches(WindowRole::Drawpad, prepared.primary)
				&& WorkspaceSurfaceMatches(WindowRole::DrawpadPresentation, prepared.presentation);
		}
		[[nodiscard]] bool WorkspaceNativePolicyMatches(const WorkspaceWindowPrepared& prepared) const noexcept
		{
			if (WhiteboardWindowMode() != prepared.policy.whiteboard || canvasPolicyDirty_) return false;
			const bool expectedTopmost = !prepared.policy.whiteboard;
			const auto group = CaptureGroupZOrder();
			if (!group || !group->Matches(ExpectedGroupOwners(*group), expectedTopmost, false)) return false;
			for (std::size_t i = 4; i < 8; ++i)
				if (group->windows[i] && group->windows[8] && !group->Above(8, i)) return false;
			for (const auto role : {WindowRole::Freeze, WindowRole::Drawpad})
			{
				const auto desired = ResolveOverlayActivationStyle(role, prepared.policy.whiteboard
					? OverlayActivationMode::Whiteboard : OverlayActivationMode::Presentation);
				const auto actual = GetWindowLongPtrW(Handle(role), GWL_EXSTYLE);
				if ((actual & desired.setExStyle) != desired.setExStyle || (actual & desired.clearExStyle)
					|| records_[RoleIndex(role)].activationStyleFramePending) return false;
			}
			for (const auto role : {WindowRole::DrawpadPresentation, WindowRole::Drawpad})
			{
				const auto& record = records_[RoleIndex(role)];
				if (!record.canvasPolicy.applied) return false;
				Detail::CanvasRegion expected, actual;
				if (!Detail::BuildCanvasRegion(*record.canvasPolicy.applied, record.canvasPolicy.originalRegion.Get(), expected)
					|| !ReadCanvasRegion(Handle(role), actual)
					|| (expected.Get() ? !actual.Get() || !EqualRgn(expected.Get(), actual.Get()) : actual.Get() != nullptr)
					|| GetPropW(Handle(role), Detail::CanvasNonRudeProperty) != Detail::CanvasNonRudeValue(prepared.policy.whiteboard, record.canvasPolicy.originalNonRude)) return false;
			}
			return true;
		}

		[[nodiscard]] bool WorkspaceInput(const Command& command, bool preparing, std::function<bool()> current)
		{
			for (const auto role : {WindowRole::DrawpadPresentation, WindowRole::Drawpad})
			{
				Command input; input.type = CommandType::SetClickThrough; input.role = role;
				input.enabled = preparing || role == WindowRole::DrawpadPresentation || command.workspacePolicy.hideDrawpad
					|| command.workspacePolicy.minimized || (!command.workspacePolicy.whiteboard && command.workspacePolicy.selectionMode);
				input.stillDesired = current;
				if (!Execute(input)) return false;
			}
			return true;
		}
		[[nodiscard]] bool ApplyPrepareWorkspacePolicy(const Command& command)
		{
			if (!command.workspacePolicy.revision || !command.workspaceResult) return false;
			if (preparedWorkspace_ && WorkspacePreparedMatches(command.workspacePolicy, *preparedWorkspace_) && WorkspaceNativePolicyMatches(*preparedWorkspace_))
			{ *command.workspaceResult = *preparedWorkspace_; return CommandCurrent(command); }
			const auto identity = CaptureGroupZOrder();
			const auto display = Inkeys::Display::GetSnapshot();
			RECT outputBounds{};
			if (!GetWindowRect(Handle(WindowRole::Drawpad), &outputBounds)) return false;
			const auto* monitor = display ? (command.workspacePolicy.whiteboard ? display->Primary()
				: display->Find(MonitorFromRect(&outputBounds, MONITOR_DEFAULTTONEAREST))) : nullptr;
			if (!identity || !monitor) return false;
			struct Operations
			{
				Impl& owner; const Command& command; const GroupSnapshot& identity;
				const RECT bounds; const std::uint64_t displayGeneration;
				HMONITOR targetMonitor; UINT dpiX, dpiY;
				const std::uint64_t safetyRevision;
				bool Current() const
				{
					const auto display = Inkeys::Display::GetSnapshot();
					const auto* monitor = display ? display->Find(targetMonitor) : nullptr;
					const bool physical = !owner.shutdownRequested_.load(std::memory_order_acquire) && safetyRevision == owner.overlaySafetyRevision_ && owner.GroupIdentityCurrent(identity) && monitor
						&& (!command.workspacePolicy.whiteboard || display->Primary() == monitor)
						&& EqualRect(&monitor->bounds, &bounds) && monitor->effectiveDpiX == dpiX && monitor->effectiveDpiY == dpiY;
					if (!physical && command.completion) command.completion->Cancel();
					return physical && owner.CommandCurrent(command);
				}
				bool WithdrawFullscreen()
				{
					if (!command.workspacePolicy.whiteboard || command.workspacePolicy.minimized)
					{ owner.overlayFullscreen_.store(false, std::memory_order_release); owner.ApplyOverlayFullscreen(owner.OverlayRoot()); }
					return true;
				}
				bool PrepareGeometry()
				{
					const bool whiteboard = command.workspacePolicy.whiteboard;
					if (whiteboard && command.workspacePolicy.minimized)
						return owner.ApplyMinimizeWhiteboardGroup([this] { return Current(); });
					if (whiteboard && owner.WhiteboardWindowGroupMinimized()
						&& !owner.ApplyRestoreWhiteboardGroup(true, [this] { return Current(); })) return false;
					for (const auto role : {WindowRole::Freeze, WindowRole::DrawpadPresentation, WindowRole::Drawpad})
					{
						if (!Current()) return false;
						const HWND hwnd = owner.Handle(role); RECT actual{};
						if (!hwnd || !GetWindowRect(hwnd, &actual)) return false;
						const bool geometryChanged = whiteboard && !EqualRect(&actual, &bounds);
						if (geometryChanged || whiteboard != owner.WhiteboardWindowMode() || command.workspacePolicy.hideDrawpad)
						{ ShowWindow(hwnd, SW_HIDE); if (!Current() || IsWindowVisible(hwnd)) return false; }
						if (whiteboard && role == WindowRole::Freeze && IsIconic(hwnd))
						{ ShowWindow(hwnd, SW_SHOWNOACTIVATE); ShowWindow(hwnd, SW_HIDE); if (!Current() || IsIconic(hwnd)) return false; }
						if (geometryChanged && (!SetWindowPos(hwnd, nullptr, bounds.left, bounds.top,
							bounds.right-bounds.left, bounds.bottom-bounds.top, SWP_NOZORDER | SWP_NOACTIVATE) || !Current())) return false;
					}
					return true;
				}
				bool ApplyMode()
				{
					const bool result = owner.ApplyWhiteboardWindowMode(command.workspacePolicy.whiteboard, [this] { return Current(); });
					// 本请求的失败由捕获身份快照负责；旧自动策略重试不能转移到新 pair。
					if (!result) owner.canvasPolicyRetry_ = false;
					return result;
				}
				bool ApplyBand()
				{
					owner.overlayTopmost_.store(!command.workspacePolicy.whiteboard, std::memory_order_release);
					return owner.ReconcileRelativeOverlayGroup(identity, Detail::CapturePptZOrder(identity), &command);
				}
				bool BlockInput() { return owner.WorkspaceInput(command, true, [this] { return Current(); }); }
				bool CapturePrepared()
				{
					WorkspaceWindowPrepared prepared{command.workspacePolicy,
						owner.CaptureWorkspaceSurface(WindowRole::Freeze), owner.CaptureWorkspaceSurface(WindowRole::Drawpad),
						owner.CaptureWorkspaceSurface(WindowRole::DrawpadPresentation), displayGeneration, targetMonitor, bounds, dpiX, dpiY, safetyRevision};
					if (!Current()) return false;
					owner.preparedWorkspace_ = prepared; *command.workspaceResult = prepared; return true;
				}
			} operations{*this, command, *identity, monitor->bounds, display->generation, monitor->handle, monitor->effectiveDpiX, monitor->effectiveDpiY, overlaySafetyRevision_};
			return Detail::PrepareWorkspacePolicy(operations);
		}
		[[nodiscard]] bool ApplyCompleteWorkspacePolicy(const Command& command)
		{
			struct Operations
			{
				Impl& owner; const Command& command; const std::uint64_t safetyRevision;
				bool Current() const
				{
					if (safetyRevision != owner.overlaySafetyRevision_ || owner.shutdownRequested_.load(std::memory_order_acquire))
					{ if (command.completion) command.completion->Cancel(); return false; }
					return owner.CommandCurrent(command) && owner.WorkspacePreparedMatches(command.workspacePolicy, command.workspacePrepared);
				}
				bool ValidatePrepared() const { return owner.WorkspacePreparedMatches(command.workspacePolicy, command.workspacePrepared) && owner.WorkspaceNativePolicyMatches(command.workspacePrepared); }
				bool ApplyInput() { return owner.WorkspaceInput(command, false, [this] { return Current(); }); }
				bool ApplyVisibility()
				{
					const auto& policy = command.workspacePolicy;
					const auto desired = policy.hideDrawpad || policy.minimized ? DrawpadSurfaceVisibility::Hidden
						: (!policy.whiteboard && policy.selectionMode ? DrawpadSurfaceVisibility::Presentation : DrawpadSurfaceVisibility::Primary);
					for (const auto role : {WindowRole::DrawpadPresentation, WindowRole::Drawpad})
					{
						if (!Current()) return false;
						const HWND hwnd = role == WindowRole::Drawpad ? command.workspacePrepared.primary.hwnd : command.workspacePrepared.presentation.hwnd;
						const bool show = role == WindowRole::Drawpad ? desired == DrawpadSurfaceVisibility::Primary : desired == DrawpadSurfaceVisibility::Presentation;
						if ((IsWindowVisible(hwnd) != FALSE) != show) ShowWindow(hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
						if (!Current() || (IsWindowVisible(hwnd) != FALSE) != show) return false;
					}
					const HWND freeze = command.workspacePrepared.freeze.hwnd;
					// 最小化保持同一 APPWINDOW 任务栏锚点；只隐藏画布，不把 iconic 锚点撤掉。
					if (policy.whiteboard && policy.minimized)
					{
						if (!IsIconic(freeze)) return false;
						if (!IsWindowVisible(freeze)) ShowWindow(freeze, SW_SHOWMINNOACTIVE);
						return Current() && IsIconic(freeze) && IsWindowVisible(freeze);
					}
					const bool show = policy.whiteboard && !policy.minimized;
					if ((IsWindowVisible(freeze) != FALSE) != show) ShowWindow(freeze, show ? SW_SHOWNOACTIVATE : SW_HIDE);
					return Current() && ValidatePrepared() && ((IsWindowVisible(freeze) != FALSE) == show)
						&& (!show || !IsIconic(freeze));
				}
				bool PublishFullscreen()
				{
					owner.overlayFullscreen_.store(command.workspacePolicy.whiteboard && !command.workspacePolicy.minimized, std::memory_order_release);
					owner.ApplyOverlayFullscreen(owner.OverlayRoot()); return ValidatePrepared();
				}
			} operations{*this, command, overlaySafetyRevision_};
			return Detail::CompleteWorkspacePolicy(operations);
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
				bool viewOnly = false;
				bool restoreBounds = true, restoreVisibility = true;
				std::shared_ptr<Detail::CanvasRegion> region;
				HANDLE nonRude = nullptr;
				std::optional<Detail::CanvasPolicyKey> applied;
			};
			std::vector<Native> windows;
			bool mode = false;
			bool topmost = false;
			bool fullscreen = false;
			bool group = false;
			bool canvas = false;
			bool workspace = false;
			bool candidate = false;
			std::optional<GroupSnapshot> zOrder;
			bool minimized = false;
			std::array<bool, RoleCount> visibleBeforeMinimize{};
			std::optional<WindowSpec> destroyedSpec;
			HWND originalWindow = nullptr;
		};
		struct Recovery
		{
			std::function<bool()> restore;
			std::shared_ptr<Detail::CommandCompletion> completion;
			bool independentUi = false;
			SurfacePresentationLease presentationLease;
			bool workspace = false;
		};

		[[nodiscard]] static bool IsZOrderOnlyCommand(CommandType type) noexcept
		{
			return type == CommandType::RefreshTopmost || type == CommandType::PromotePpt
				|| type == CommandType::SetOverlayTopmost || type == CommandType::SetOverlayFullscreen;
		}

		[[nodiscard]] CommandSnapshot CaptureCommandSnapshot(const Command& command)
		{
			CommandSnapshot snapshot;
			snapshot.originalWindow = Handle(command.role);
			snapshot.candidate = command.type == CommandType::ApplyCandidate;
			snapshot.workspace = command.type == CommandType::PrepareWorkspacePolicy || command.type == CommandType::CompleteWorkspacePolicy;
			if (snapshot.workspace)
			{ snapshot.zOrder = CaptureGroupZOrder(); if (!snapshot.zOrder) throw std::runtime_error("workspace identity"); }
			snapshot.mode = WhiteboardWindowMode();
			snapshot.topmost = OverlayTopmost();
			snapshot.fullscreen = OverlayFullscreen();
			snapshot.minimized = WhiteboardWindowGroupMinimized();
			snapshot.visibleBeforeMinimize = whiteboardVisibleBeforeMinimize_;
			snapshot.group = command.type == CommandType::SetWhiteboardWindowMode
				|| command.type == CommandType::SetOverlayTopmost || command.type == CommandType::SetOverlayFullscreen
				|| command.type == CommandType::RefreshTopmost || command.type == CommandType::PromotePpt
				|| command.type == CommandType::MinimizeWhiteboardGroup || command.type == CommandType::RestoreWhiteboardGroup;
			snapshot.canvas = (snapshot.group && !IsZOrderOnlyCommand(command.type)) || command.type == CommandType::SetDrawpadSurfaceVisibility
				|| (command.type == CommandType::SetBounds && IsCanvas(command.role));
			if (IsZOrderOnlyCommand(command.type))
			{
				snapshot.zOrder = CaptureGroupZOrder();
				if (!snapshot.zOrder) throw std::runtime_error("z-order snapshot");
				return snapshot;
			}
			const auto capture = [&](WindowRole role)
			{
				const HWND hwnd = Handle(role);
				if (!hwnd || !IsWindow(hwnd)) return;
				CommandSnapshot::Native native;
				native.role = role;
				native.viewOnly = snapshot.workspace && !IsCanvas(role) && role != WindowRole::Freeze;
				native.hwnd = hwnd;
				native.generation = records_[RoleIndex(role)].generation.load(std::memory_order_acquire);
				if (command.candidate)
					for (std::size_t i = 0; i < command.candidate->candidate.value.count; ++i)
					{
						const auto& target = command.candidate->candidate.value.targets[i];
						if (target.role == role) { native.restoreBounds = target.applyBounds; native.restoreVisibility = target.visibility != WindowCandidateVisibility::Preserve; }
					}
				native.owner = GetWindow(hwnd, GW_OWNER);
				if (!GetWindowRect(hwnd, &native.bounds)) throw std::runtime_error("window snapshot");
				native.style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
				native.visible = IsWindowVisible(hwnd) != FALSE;
				native.iconic = IsIconic(hwnd) != FALSE;
				if (snapshot.workspace && IsCanvas(role))
				{
					native.region = std::make_shared<Detail::CanvasRegion>();
					if (!ReadCanvasRegion(hwnd, *native.region)) throw std::runtime_error("workspace region");
					native.nonRude = GetPropW(hwnd, Detail::CanvasNonRudeProperty);
					native.applied = records_[RoleIndex(role)].canvasPolicy.applied;
				}
				snapshot.windows.push_back(native);
			};
			if (command.candidate)
			{
				for (std::size_t i = 0; i < command.candidate->candidate.value.count; ++i)
				{
					const auto& target = command.candidate->candidate.value.targets[i];
					capture(target.role);
					if (target.promote || (target.visibility == WindowCandidateVisibility::Show && !IsWindowVisible(target.hwnd))) { snapshot.zOrder = CaptureGroupZOrder(); if (!snapshot.zOrder) throw std::runtime_error("candidate z-order"); }
				}
			}
			else if (snapshot.workspace)
			{
				capture(WindowRole::Freeze); capture(WindowRole::DrawpadPresentation); capture(WindowRole::Drawpad);
				if (command.type == CommandType::PrepareWorkspacePolicy && command.workspacePolicy.whiteboard
					&& command.workspacePolicy.minimized != snapshot.minimized)
					for (const auto role : {WindowRole::MagnifierHost, WindowRole::PptBottomLeft, WindowRole::PptBottomRight, WindowRole::PptMiddleLeft, WindowRole::PptMiddleRight, WindowRole::Bar}) capture(role);
			}
			else if (snapshot.group) for (const auto role : GroupRoles) capture(role);
			else if (command.type == CommandType::SetDrawpadSurfaceVisibility)
			{ capture(WindowRole::DrawpadPresentation); capture(WindowRole::Drawpad); }
			else if (command.type != CommandType::Create && command.type != CommandType::Destroy
				&& command.type != CommandType::BindMessages && command.type != CommandType::UnbindMessages
				&& command.type != CommandType::HideAll && command.type != CommandType::CancelPointerCapture)
				capture(command.role);
			if (command.type == CommandType::Destroy) snapshot.destroyedSpec = records_[RoleIndex(command.role)].activeSpec;
			return snapshot;
		}

		[[nodiscard]] bool RestoreWorkspaceSnapshot(const CommandSnapshot& snapshot)
		{
			preparedWorkspace_.reset();
			canvasPolicyRetry_ = false;
			whiteboardWindowMode_.store(snapshot.mode, std::memory_order_release);
			overlayTopmost_.store(snapshot.topmost, std::memory_order_release);
			overlayFullscreen_.store(snapshot.fullscreen, std::memory_order_release);
			whiteboardGroupMinimized_.store(snapshot.minimized, std::memory_order_release);
			whiteboardVisibleBeforeMinimize_ = snapshot.visibleBeforeMinimize;
			const auto actualGroup = CaptureGroupZOrder();
			if (!actualGroup || !snapshot.zOrder) return false;
			const auto identity = Detail::RetainSurvivingZOrder(*snapshot.zOrder, *actualGroup, ExpectedGroupOwners(*snapshot.zOrder));
			const auto alive = [this, &identity](const CommandSnapshot::Native& native)
			{
				for (std::size_t i = 0; i < GroupRoles.size(); ++i)
					if (GroupRoles[i] == native.role) return identity.windows[i] == native.hwnd && native.hwnd == Handle(native.role)
						&& native.generation == records_[RoleIndex(native.role)].generation.load(std::memory_order_acquire) && IsWindow(native.hwnd);
				return false;
			};
			bool restored = true;
			for (const auto& native : snapshot.windows)
			{
				if (!alive(native) || native.viewOnly) continue;
				const LONG_PTR actual = GetWindowLongPtrW(native.hwnd, GWL_EXSTYLE);
				const LONG_PTR style = (safetyOverrides_[RoleIndex(native.role)].Style(native.generation, native.style) & ~static_cast<LONG_PTR>(WS_EX_TOPMOST)) | (actual & WS_EX_TOPMOST);
				RECT bounds{};
				if (!GetWindowRect(native.hwnd, &bounds)) { restored = false; continue; }
				Detail::CanvasRegion actualRegion;
				const bool regionRead = !native.region || ReadCanvasRegion(native.hwnd, actualRegion);
				const bool regionChanged = native.region && (!regionRead || (native.region->Get()
					? !actualRegion.Get() || !EqualRgn(native.region->Get(), actualRegion.Get()) : actualRegion.Get() != nullptr)
					|| GetPropW(native.hwnd, Detail::CanvasNonRudeProperty) != native.nonRude);
				const bool framePending = records_[RoleIndex(native.role)].activationStyleFramePending
					|| records_[RoleIndex(native.role)].clickThroughFramePending;
				const bool changed = actual != style || !EqualRect(&bounds, &native.bounds) || regionChanged || framePending;
				if (!changed) continue;
				ShowWindow(native.hwnd, SW_HIDE);
				if (!alive(native)) continue;
				if (actual != style)
				{ SetLastError(ERROR_SUCCESS); if (!SetWindowLongPtrW(native.hwnd, GWL_EXSTYLE, style) && GetLastError()) restored = false; }
				if (!alive(native)) continue;
				if (!SetWindowPos(native.hwnd, nullptr, native.bounds.left, native.bounds.top,
					native.bounds.right-native.bounds.left, native.bounds.bottom-native.bounds.top,
					SWP_NOZORDER | SWP_NOACTIVATE | ((actual != style || framePending) ? SWP_FRAMECHANGED : 0))) restored = false;
				if (!alive(native)) continue;
				if (regionChanged && (!SetCanvasNonRude(native.hwnd, native.nonRude) || !alive(native) || !SetCanvasRegion(native.hwnd, native.region->Get()))) restored = false;
				if (native.region && alive(native)) records_[RoleIndex(native.role)].canvasPolicy.applied = native.applied;
				if (restored) { records_[RoleIndex(native.role)].activationStyleFramePending = false; records_[RoleIndex(native.role)].clickThroughFramePending = false; }
			}
			if (!ReconcileRelativeOverlayGroup(identity, Detail::CapturePptZOrder(identity), nullptr, true)) restored = false;
			if (!restored) return false;
			for (const auto& native : snapshot.windows)
			{
				if (!alive(native)) continue;
				const bool visible = safetyOverrides_[RoleIndex(native.role)].Visible(native.generation, native.visible)
					&& !shutdownRequested_.load(std::memory_order_acquire);
				if ((IsWindowVisible(native.hwnd) != FALSE) != visible) ShowWindow(native.hwnd, visible ? (native.iconic ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE) : SW_HIDE);
				RECT bounds{};
				if (alive(native) && ((!native.viewOnly && (!GetWindowRect(native.hwnd, &bounds) || !EqualRect(&bounds, &native.bounds)))
					|| (IsWindowVisible(native.hwnd) != FALSE) != visible
					|| (!native.viewOnly && ((GetWindowLongPtrW(native.hwnd, GWL_EXSTYLE) ^ safetyOverrides_[RoleIndex(native.role)].Style(native.generation, native.style)) & ~static_cast<LONG_PTR>(WS_EX_TOPMOST))))) restored = false;
			}
			if (restored) canvasPolicyDirty_ = false;
			ApplyOverlayFullscreen(OverlayRoot());
			return restored;
		}

		[[nodiscard]] bool RestoreZOrderSnapshot(const CommandSnapshot& snapshot, CommandType type)
		{
			if (!snapshot.zOrder || shutdownRequested_.load(std::memory_order_acquire)) return false;
			const auto actual = CaptureGroupZOrder();
			if (!actual) return false;
			if (type == CommandType::SetOverlayTopmost) overlayTopmost_.store(snapshot.topmost, std::memory_order_release);
			if (type == CommandType::SetOverlayFullscreen) overlayFullscreen_.store(snapshot.fullscreen, std::memory_order_release);
			const auto identity = Detail::RetainSurvivingZOrder(*snapshot.zOrder, *actual,
				ExpectedGroupOwners(*snapshot.zOrder));
			// identity 保留请求前的 ordinal；不能从部分失败后的现场重新取 PPT 兄弟序。
			const auto order = Detail::CapturePptZOrder(identity);
			// 回滚使用当前物理身份，不受原请求已过期的 Current guard 阻止；保留失败回执。
			if (!ReconcileRelativeOverlayGroup(identity, order, nullptr, true)) return false;
			ApplyOverlayFullscreen(OverlayRoot());
			return true;
		}

		[[nodiscard]] bool RestoreCommandSnapshot(const CommandSnapshot& snapshot, CommandType type, WindowRole role)
		{
			if (snapshot.candidate)
			{
				bool restored = true;
				for (const auto& native : snapshot.windows)
				{
					if (Handle(native.role) != native.hwnd || records_[RoleIndex(native.role)].generation.load(std::memory_order_acquire) != native.generation) continue;
					if (!IsWindow(native.hwnd)) { restored = false; continue; }
					// verify-only ULW 候选未写几何；撤回不能把新帧搬回旧坐标。
					if (native.restoreBounds)
					{
						RECT actual{};
						if (!GetWindowRect(native.hwnd, &actual)) restored = false;
						else if (!EqualRect(&actual, &native.bounds))
						{
							presentationRevisions_[RoleIndex(native.role)].fetch_add(1, std::memory_order_acq_rel);
							if (!SetWindowPos(native.hwnd, nullptr, native.bounds.left, native.bounds.top,
								native.bounds.right-native.bounds.left, native.bounds.bottom-native.bounds.top, SWP_NOZORDER | SWP_NOACTIVATE)) restored = false;
						}
						if (!GetWindowRect(native.hwnd, &actual) || !EqualRect(&actual, &native.bounds)) restored = false;
					}
					if (native.restoreVisibility)
					{
						const bool visible = safetyOverrides_[RoleIndex(native.role)].Visible(native.generation, native.visible) && !shutdownRequested_.load(std::memory_order_acquire);
						if ((IsWindowVisible(native.hwnd) != FALSE) != visible) ShowWindow(native.hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
						if ((IsWindowVisible(native.hwnd) != FALSE) != visible) restored = false;
					}
				}
				if (snapshot.zOrder && !RestoreZOrderSnapshot(snapshot, CommandType::PromotePpt)) restored = false;
				return restored;
			}
			// 场景恢复只写捕获的背景/画布；不能按当前 role 把旧策略应用到新 pair。
			if (snapshot.workspace) return RestoreWorkspaceSnapshot(snapshot);
			// 纯层级命令从未写画布/几何/显隐，必须在公共 FRAMECHANGED/Show 路径之前分流。
			if (IsZOrderOnlyCommand(type)) return RestoreZOrderSnapshot(snapshot, type);
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
			if (snapshot.group && !CommitOverlayGroupPolicy()) restored = false;
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
			SetLastError(ERROR_SUCCESS);
			try { restored = recovery->restore(); } catch (...) {}
			if (!restored)
			{
				if (recovery->completion) recovery->completion->RecordRollbackFailure(GetLastError() ? GetLastError() : ERROR_GEN_FAILURE);
				return false;
			}
			PublishNativeSnapshots(setting); // 物理恢复快照先于失败终态，异步协调器随后才能恢复源场景。
			if (recovery->completion)
			{
				if (!recovery->completion->Current()) recovery->completion->Retired();
				else if (!recovery->completion->Commit(false)) recovery->completion->Retired();
			}
			if (recovery->workspace) workspaceProgressPending_ = true;
			recovery.reset();
			if (!setting) overlayRecoveryActive_.store(false, std::memory_order_release);
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
			if (command.candidate)
			{
				for (std::size_t i = 0; i < command.candidate->candidate.value.count; ++i)
				{
					const auto& target = command.candidate->candidate.value.targets[i];
					if (target.visibility != WindowCandidateVisibility::Hide || target.applyBounds || target.promote) return false;
				}
				return true;
			}
			return command.type == CommandType::HideAll || command.type == CommandType::Hide
				|| (command.type == CommandType::SetDrawpadSurfaceVisibility && command.drawpadVisibility == DrawpadSurfaceVisibility::Hidden)
				|| (command.type == CommandType::SetClickThrough && command.enabled);
		}

		void LatchSafetyCleanup(const Command& command) noexcept
		{
			bool changed = false;
			const auto latch = [&](WindowRole role, bool hidden, bool transparent)
			{
				auto& record = records_[RoleIndex(role)];
				const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
				if (!hwnd) return;
				changed = safetyOverrides_[RoleIndex(role)].Latch(record.generation.load(std::memory_order_acquire), hidden, transparent) || changed;
				// 重复且已满足的安全意图不改变工作区凭据；实际漂移仍建立新的补偿屏障。
				changed = (hidden && IsWindowVisible(hwnd))
					|| (transparent && !(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT)) || changed;
				if (hidden && IsCanvas(role)) Detail::NoteCanvasRecoveryHidden(record.canvasPolicy.recoveryVisible, record.canvasPolicy.recoveryIconic);
			};
			if (command.candidate)
			{
				for (std::size_t i = 0; i < command.candidate->candidate.value.count; ++i)
					latch(command.candidate->candidate.value.targets[i].role, true, false);
			}
			else if (command.type == CommandType::HideAll)
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
			if (changed && !IsSetting(command.role)) { ++overlaySafetyRevision_; workspaceNativeRevision_->fetch_add(1, std::memory_order_acq_rel); }
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
					else { if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE); succeeded = !IsWindowVisible(hwnd) && succeeded; }
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
			if (command.type == CommandType::CompleteWorkspacePolicy)
			{
				if (command.workspacePolicy.whiteboard) allowShow(WindowRole::Freeze);
				if (!command.workspacePolicy.hideDrawpad && !command.workspacePolicy.minimized)
				{
					allowShow(!command.workspacePolicy.whiteboard && command.workspacePolicy.selectionMode ? WindowRole::DrawpadPresentation : WindowRole::Drawpad);
					if (command.workspacePolicy.whiteboard || !command.workspacePolicy.selectionMode)
						safetyOverrides_[RoleIndex(WindowRole::Drawpad)].AllowInput(command.workspacePrepared.primary.generation);
				}
			}
			else if (command.candidate)
			{
				for (std::size_t i = 0; i < command.candidate->candidate.value.count; ++i)
				{
					const auto& target = command.candidate->candidate.value.targets[i];
					if (target.visibility == WindowCandidateVisibility::Show) allowShow(target.role);
				}
			}
			else if (command.type == CommandType::Show) allowShow(command.role);
			else if (command.type == CommandType::SetDrawpadSurfaceVisibility)
			{
				if (command.drawpadVisibility == DrawpadSurfaceVisibility::Primary) allowShow(WindowRole::Drawpad);
				if (command.drawpadVisibility == DrawpadSurfaceVisibility::Presentation) allowShow(WindowRole::DrawpadPresentation);
			}
			else if (command.type == CommandType::SetClickThrough && !command.enabled)
				safetyOverrides_[RoleIndex(command.role)].AllowInput(records_[RoleIndex(command.role)].generation.load(std::memory_order_acquire));
		}

		[[nodiscard]] bool ExecuteTracked(const Command& command, SurfacePresentationLease lease = {}, bool externalLeaseHeld = false)
		{
			// 旧命令连缓存失效权也没有；先退休自身，不能伤及后来已提交的窗口凭据。
			if (!CommandCurrent(command))
			{
				if (command.completion) { command.completion->Cancel(); command.completion->Retired(); }
				if (command.workspaceRequest)
				{
					workspaceProgressPending_ = true;
					if (!commandApplying_[0]) DrainWorkspaceProgress();
				}
				SetLastError(Detail::CommandExpired(command.deadline) ? ERROR_TIMEOUT : ERROR_CANCELLED);
				return false;
			}
			const auto leaseMask = CommandPresentationMask(command);
			if (leaseMask && !lease && !externalLeaseHeld && CommandCurrent(command))
			{
				lease = AcquirePresentationLeaseMask(leaseMask);
				if (!lease)
				{
					if (command.completion && command.completion->Begin()) (void)command.completion->Commit(false);
					return false; // 同 owner 重入不能等待 UI 的 ULW 租约。
				}
			}
			if (!command.candidate && command.type != CommandType::RefreshTopmost && command.type != CommandType::PromotePpt
				&& command.type != CommandType::SetOverlayTopmost && command.type != CommandType::SetOverlayFullscreen
				&& command.type != CommandType::BindMessages && command.type != CommandType::UnbindMessages
				&& command.type != CommandType::CancelPointerCapture)
			{
				const bool group = command.type == CommandType::HideAll || command.type == CommandType::PrepareWorkspacePolicy
					|| command.type == CommandType::CompleteWorkspacePolicy || command.type == CommandType::SetWhiteboardWindowMode
					|| command.type == CommandType::MinimizeWhiteboardGroup || command.type == CommandType::RestoreWhiteboardGroup
					|| command.type == CommandType::SetDrawpadSurfaceVisibility || IsCanvas(command.role)
					|| command.role == WindowRole::Freeze || command.role == WindowRole::MagnifierHost;
				candidateMailbox_.InvalidateRoles(group ? (std::uint64_t{1} << RoleCount) - 1
					: std::uint64_t{1} << RoleIndex(command.role));
			}
			if (TouchesWorkspaceTruth(command)) workspaceNativeRevision_->fetch_add(1, std::memory_order_acq_rel);
			if (command.candidate) PublishCandidateSnapshots(command.candidate->candidate.value, true);
			else InvalidateNativeSnapshots(IsSetting(command.role));
			bool snapshotPublished = false;
			const bool result = ExecuteTrackedNative(command, snapshotPublished, std::move(lease));
			// 失败可能在早先读回后又进行了回滚；成功路径已在终态唤醒前发布，无须重复扫描。
			if (!result || !snapshotPublished)
			{
				if (command.candidate) PublishCandidateSnapshots(command.candidate->candidate.value);
				else PublishNativeSnapshots(IsSetting(command.role));
			}
			if (command.workspaceRequest && command.completion->Phase() != Detail::CommandPhase::Executing
				&& command.completion->Phase() != Detail::CommandPhase::Queued) workspaceProgressPending_ = true;
			if (!IsSetting(command.role)) DrainWorkspaceProgress();
			if (result && command.notifyTopmost) NotifyTopmostRefresh();
			return result;
		}

		[[nodiscard]] bool ExecuteTrackedNative(const Command& command, bool& snapshotPublished, SurfacePresentationLease lease)
		{
			const bool setting = IsSetting(command.role);
			const auto commitResult = [&](bool result)
			{
				// 实际快照先于完成 phase/条件变量唤醒，消费者不能读到终态配旧 native 状态。
				if (command.candidate) PublishCandidateSnapshots(command.candidate->candidate.value);
				else PublishNativeSnapshots(setting);
				snapshotPublished = true;
				if (result && command.workspaceRequest)
				{
					command.workspaceRequest->prepared = command.workspaceResult ? *command.workspaceResult : std::optional<WorkspaceWindowPrepared>(command.workspacePrepared);
					command.workspaceRequest->nativeRevision = workspaceNativeRevision_->load(std::memory_order_acquire);
				}
				if (!CommandCurrent(command)) return false;
				if (result && command.candidate)
					return candidateMailbox_.TryCommit(*command.candidate, [&]
					{ return !command.completion || command.completion->Commit(true); });
				return !command.completion || command.completion->Commit(result);
			};
			bool& applying = commandApplying_[setting ? 1 : 0];
			if (applying)
			{
				if (IsSafetyCleanup(command) && CommandCurrent(command))
				{
					LatchSafetyCleanup(command); // 同步重入只锁存，外层退栈后由既有安全重试执行。
					if (command.completion && command.completion->Begin() && !command.completion->Commit(false)) command.completion->Retired();
					return false;
				}
				if (command.type == CommandType::MinimizeWhiteboardGroup && WhiteboardWindowGroupMinimized()
					&& IsIconic(Handle(WindowRole::Freeze)))
				{
					if (command.completion && command.completion->Begin()) (void)commitResult(true);
					return true;
				}
				if (command.completion) { command.completion->Cancel(); command.completion->Retired(); }
				return false;
			}
			struct Scope { bool& applying; Scope(bool& value) : applying(value) { applying = true; } ~Scope() { applying = false; } } scope(applying);
			if (command.completion && !command.completion->Begin()) return false;
			if (setting && command.type == CommandType::SetSettingOwnedByDrawpad && overlayRecoveryActive_.load(std::memory_order_acquire))
			{
				if (command.completion && !command.completion->Commit(false)) command.completion->Retired();
				return false; // Setting 独立内容可继续，但 owner attach/detach 不能穿过 overlay 的物理恢复责任。
			}
			const bool cleanup = IsSafetyCleanup(command);
			const bool independentUi = commandRecovery_[setting ? 1 : 0] && commandRecovery_[setting ? 1 : 0]->independentUi
				&& (command.type == CommandType::CancelPointerCapture || (command.type == CommandType::SetBounds && IsUiPopup(command.role)));
			if (independentUi)
			{
				bool result = false; try { result = CommandCurrent(command) && Execute(command); } catch (...) {}
				if (command.completion && !commitResult(result))
				{ command.completion->Cancel(); command.completion->Retired(); }
				return result && CommandCurrent(command);
			}
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
					if (commitResult(result)) return result;
					command.completion->Cancel();
					command.completion->Retired();
				}
				return result && CommandCurrent(command);
			}
			if (command.type == CommandType::CompleteWorkspacePolicy && !WorkspacePreparedMatches(command.workspacePolicy, command.workspacePrepared))
			{
				if (command.completion) { command.completion->Cancel(); command.completion->Retired(); }
				return false; // 几何/代际已变：尚未写入，不制造恢复事务，调用方重准备真实帧。
			}
			std::optional<Recovery> prepared;
			bool result = false;
			try
			{
				prepared = Recovery{
					[this, snapshot = CaptureCommandSnapshot(command), type = command.type, role = command.role]
					{ return RestoreCommandSnapshot(snapshot, type, role); }, command.completion,
					command.type == CommandType::PrepareWorkspacePolicy || command.type == CommandType::CompleteWorkspacePolicy || IsZOrderOnlyCommand(command.type), std::move(lease), command.workspaceRequest != nullptr };
				SetLastError(ERROR_SUCCESS);
				result = Execute(command);
				if (!result && command.completion) command.completion->RecordNativeFailure(GetLastError() ? GetLastError() : ERROR_GEN_FAILURE);
			}
			catch (...) { if (command.completion) command.completion->RecordNativeFailure(ERROR_NOT_ENOUGH_MEMORY); }
			if (command.type == CommandType::CompleteWorkspacePolicy && !WorkspacePreparedMatches(command.workspacePolicy, command.workspacePrepared)
				&& command.completion) command.completion->Cancel();
			const bool current = CommandCurrent(command);
			if (result && current && commitResult(true))
			{
				ClearSafetyAfterCommit(command);
				return true;
			}
			// 原生写入可能在超时/重入前已发生。先撤回；失败则由 owner 持有恢复责任并拒绝新事务越过。
			if (!current && command.completion) { command.completion->RecordNativeFailure(ERROR_CANCELLED); command.completion->Cancel(); }
			if (prepared)
			{
				commandRecovery_[setting ? 1 : 0] = std::move(prepared);
				if (!setting) overlayRecoveryActive_.store(true, std::memory_order_release);
				(void)RetryCommandRecovery(setting);
			}
			else if (command.completion)
			{
				if (!current) { command.completion->Cancel(); command.completion->Retired(); }
				else if (!command.completion->Commit(false)) command.completion->Retired();
			}

			return false;
		}

		[[nodiscard]] bool ApplyCandidate(const Command& command)
		{
			if (!command.candidate) return false;
			const auto& work = *command.candidate;
			const auto& candidate = work.candidate.value;
			for (std::size_t i = 0; i < candidate.count; ++i)
			{
				const auto& target = candidate.targets[i];
				if (target.requirePresentationMatch && presentationRevisions_[RoleIndex(target.role)].load(std::memory_order_acquire) != target.presentationRevision)
				{ SetLastError(ERROR_CANCELLED); return false; }
			}
			const auto identityMatches = [&](const WindowCandidateTarget& target)
			{
				return CommandCurrent(command) && Handle(target.role) == target.hwnd
					&& records_[RoleIndex(target.role)].generation.load(std::memory_order_acquire) == target.generation
					&& IsWindow(target.hwnd);
			};
			for (std::size_t i = 0; i < candidate.count; ++i)
			{
				const auto& target = candidate.targets[i];
				if (!identityMatches(target)) { SetLastError(ERROR_CANCELLED); return false; }
				if (WhiteboardWindowGroupMinimized() && target.role == WindowRole::Freeze
					&& (target.applyBounds || target.visibility == WindowCandidateVisibility::Show)) return false;
				if (shutdownRequested_.load(std::memory_order_acquire)
					&& (target.applyBounds || target.promote || target.visibility != WindowCandidateVisibility::Hide)) return false;
				Command operation;
				operation.role = target.role;
				operation.stillDesired = [&] { return identityMatches(target); };
				RECT before{};
				if (!GetWindowRect(target.hwnd, &before)) return false;
				if (target.applyBounds && !EqualRect(&before, &target.bounds))
				{
					operation.type = CommandType::SetBounds;
					operation.bounds = target.bounds;
					if (!Execute(operation)) return false;
					if (work.candidate.measured) candidateBoundsChanges_.fetch_add(1, std::memory_order_relaxed);
				}
				if (!identityMatches(target)) return false;
				if (target.visibility != WindowCandidateVisibility::Preserve)
				{
					const bool visible = target.visibility == WindowCandidateVisibility::Show;
					if ((IsWindowVisible(target.hwnd) != FALSE) != visible)
					{
						operation.type = visible ? CommandType::Show : CommandType::Hide;
						if (!Execute(operation)) return false;
						if (work.candidate.measured) candidateVisibilityChanges_.fetch_add(1, std::memory_order_relaxed);
					}
				}
				if (target.promote)
				{
					if (!identityMatches(target) || !IsPpt(target.role) || !RefreshOverlayGroup(&command, target.hwnd)) return false;
					if (work.candidate.measured) candidatePromotes_.fetch_add(1, std::memory_order_relaxed);
				}
			}
			// pair 两个成员全部真实读回后才确认；半完成留给既有恢复事务撤回。
			for (std::size_t i = 0; i < candidate.count; ++i)
			{
				const auto& target = candidate.targets[i];
				if (!identityMatches(target)) return false;
				const auto actual = ReadNativeSurface(target.role);
				if (!actual.known || ((target.applyBounds || target.verifyBounds) && !EqualRect(&actual.bounds, &target.bounds))
					|| (target.visibility != WindowCandidateVisibility::Preserve
						&& actual.visible != (target.visibility == WindowCandidateVisibility::Show))
					|| (target.visibility == WindowCandidateVisibility::Show && actual.iconic))
				{ SetLastError(ERROR_GEN_FAILURE); return false; }
			}
			return CommandCurrent(command);
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
					command.type == CommandType::PrepareWorkspacePolicy || command.type == CommandType::CompleteWorkspacePolicy ||
					command.type == CommandType::SetWhiteboardWindowMode ||
					command.type == CommandType::RestoreWhiteboardGroup ||
					command.type == CommandType::ActivateWhiteboard ||
					(command.type == CommandType::SetDrawpadSurfaceVisibility &&
						command.drawpadVisibility != DrawpadSurfaceVisibility::Hidden))
					return false;
			}
			// 这些事务可能需要处理一组 HWND，不能被单角色 HWND 前置检查短路。
			if (command.type == CommandType::ApplyCandidate) return ApplyCandidate(command);
			if (command.type == CommandType::PrepareWorkspacePolicy) return ApplyPrepareWorkspacePolicy(command);
			if (command.type == CommandType::CompleteWorkspacePolicy) return ApplyCompleteWorkspacePolicy(command);
			if (command.type == CommandType::SetWhiteboardWindowMode)
				return ApplyWhiteboardWindowMode(command.whiteboardMode) && CommitOverlayGroupPolicy(&command);
			if (command.type == CommandType::SetOverlayTopmost)
			{
				overlayTopmost_.store(command.enabled, std::memory_order_release);
				return CommitOverlayGroupPolicy(&command);
			}
			if (command.type == CommandType::SetOverlayFullscreen)
			{
				overlayFullscreen_.store(command.enabled, std::memory_order_release);
				return CommitOverlayGroupPolicy(&command);
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
				const bool foreground = SetForegroundWindow(hwnd) != FALSE;
				if (!canActivate()) return false; // 激活消息可能同步取消本次进入。
				// 系统拒绝前台时保留后台白板，不能继续改动其他应用的键盘焦点。
				if (foreground && GetForegroundWindow() == hwnd)
				{
					(void)SetActiveWindow(hwnd);
					if (!canActivate()) return false;
					if (GetForegroundWindow() == hwnd) (void)SetFocus(hwnd);
				}
				// 全屏标记是持久状态；激活边沿补发以便任务栏重新核对白板两面。
				ApplyOverlayFullscreen(OverlayRoot(), false, true);
				return true;
			}
			case CommandType::Show:
			{
				// 最小化前的分页/画布请求只被抑制；Freeze 必须区分未提交与显示成功。
				if (WhiteboardWindowGroupMinimized() && command.role != WindowRole::Bar
					&& !IsSetting(command.role)) return command.role != WindowRole::Freeze;
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
					if (!IsWindowVisible(hwnd))
					{
						ShowWindow(hwnd, SW_SHOWNOACTIVATE);
						// 首次显示只维护目标与 Bar 的相对位置；普通 Show 不是整组 promote。
						const HWND bar = Handle(WindowRole::Bar);
						if (IsPpt(command.role) && bar && IsWindow(bar))
							succeeded = SetWindowPos(hwnd, bar, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
						if (!succeeded) error = GetLastError();
					}

					if (succeeded && (IsPpt(command.role) || command.role == WindowRole::Freeze)
						&& (!IsWindowVisible(hwnd) || (command.role == WindowRole::Freeze && IsIconic(hwnd))))
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
				if (succeeded && (command.role == WindowRole::Freeze || command.role == WindowRole::Drawpad))
					ApplyOverlayFullscreen(OverlayRoot());
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
				if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
				if (command.role == WindowRole::Freeze || command.role == WindowRole::Drawpad)
					ApplyOverlayFullscreen(OverlayRoot());
				if (IsWindowVisible(hwnd))
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
				if (WhiteboardWindowGroupMinimized() && command.role == WindowRole::Freeze) return false;
				RECT before{};
				if (command.role != WindowRole::Drawpad && GetWindowRect(hwnd, &before) && EqualRect(&before, &command.bounds)) return true;
				SetLastError(ERROR_SUCCESS);
				if (IsUiPopup(command.role) || command.role == WindowRole::Freeze)
					presentationRevisions_[RoleIndex(command.role)].fetch_add(1, std::memory_order_acq_rel);
				const bool succeeded = command.role == WindowRole::Drawpad
					? ApplyDrawpadBounds(command.bounds)
					: SetWindowPos(hwnd, nullptr, command.bounds.left,
						command.bounds.top, command.bounds.right - command.bounds.left,
						command.bounds.bottom - command.bounds.top,
						SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
				DWORD error = succeeded ? ERROR_SUCCESS : GetLastError();
				bool applied = succeeded;
				if (applied && (IsPpt(command.role) || command.role == WindowRole::Freeze))
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
			ApplyOverlayFullscreen(OverlayRoot(), true);
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
				if (changed) ApplyOverlayFullscreen(OverlayRoot());
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

			void ApplyOverlayFullscreen(HWND root, bool forceDisabled = false, bool invalidate = false) noexcept
			{
				if (!shellFullscreen_) return;
				const auto capture = [](HWND hwnd) noexcept
				{
					Detail::ShellFullscreenState::Target target;
					target.hwnd = hwnd;
					if (hwnd)
					{
						target.generation = reinterpret_cast<std::uintptr_t>(GetPropW(hwnd, Detail::WindowGenerationProperty));
						target.thread = GetWindowThreadProcessId(hwnd, &target.process);
					}
					return target;
				};
				const bool enabled = !forceDisabled && !shutdownRequested_.load(std::memory_order_acquire)
					&& overlayFullscreen_.load(std::memory_order_acquire);
				auto targets = Detail::ShellFullscreenState::ResolveTargets(capture(root),
					capture(Handle(WindowRole::Freeze)), capture(Handle(WindowRole::Drawpad)),
					WhiteboardWindowMode(), enabled, WhiteboardWindowGroupMinimized());
				for (auto& target : targets)
					target.enabled = target.enabled && target.hwnd && IsWindowVisible(target.hwnd)
						&& !IsIconic(target.hwnd) && !SafetyHidden(target.hwnd);
				// 一批固定目标共用版本：旧 TRUE 迟到后也必须对同一组重发最新 FALSE。
				shellFullscreen_->Publish(targets, invalidate);
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

		NativeEventMailbox promoteEvents_, refreshEvents_;
		std::optional<NativeEventMailbox::Work> pendingPromote_, pendingRefresh_;
		Detail::CommandDeadline nativeEventRetryAt_;
		std::shared_ptr<std::atomic<std::uint64_t>> workspaceNativeRevision_ = std::make_shared<std::atomic<std::uint64_t>>(1);
		bool workspaceProgressPending_ = false;
		std::mutex workspaceObserverMutex_;
		std::condition_variable workspaceObserverCondition_;
		std::function<void()> workspaceProgressObserver_;
		std::size_t workspaceObserverActiveCalls_ = 0;
		static thread_local bool workspaceObserverExecuting_;
		std::atomic<std::uint64_t> presentationLeaseBits_ = 0;
		std::array<std::atomic<std::uint64_t>, RoleCount> presentationRevisions_{};
		std::uint64_t candidateLeaseBlocked_ = 0;
		Detail::CommandDeadline leaseDeferredDeadline_, candidateLeaseDeadline_;
		CandidateMailbox candidateMailbox_;
		std::array<std::optional<CandidateRetirement>, RoleCount> candidateRetirements_{};
		mutable std::mutex surfaceSnapshotsMutex_;
		std::array<WindowSurfaceState, RoleCount> surfaceSnapshots_{};
		std::atomic_bool candidateDiagnosticsEnabled_ = false;
		std::atomic<std::uint64_t> candidatesPublished_ = 0, candidatesCoalesced_ = 0, candidatesStale_ = 0, candidatesExecuted_ = 0;
		std::atomic<std::uint64_t> candidateBoundsChanges_ = 0, candidateVisibilityChanges_ = 0, candidatePromotes_ = 0;
		std::atomic<std::size_t> candidateMaxQueueDepth_ = 0;
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
		std::atomic_bool overlayRecoveryActive_ = false;
		std::uint64_t overlaySafetyRevision_ = 0; // owner 内同步安全覆盖序号，不是业务场景版本。
		std::optional<WorkspaceWindowPrepared> preparedWorkspace_;
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

	thread_local bool Service::Impl::workspaceObserverExecuting_ = false;
	thread_local bool Service::Impl::topmostObserverExecuting_ = false;
	thread_local const Service::Impl* Service::Impl::receiptOwner_ = nullptr;
	thread_local std::shared_ptr<Detail::CommandCompletion> Service::Impl::lastReceipt_;

	SurfacePresentationLease::SurfacePresentationLease(SurfacePresentationLease&& other) noexcept
		: active_(std::exchange(other.active_, nullptr)), revisions_(std::exchange(other.revisions_, nullptr)),
		mask_(std::exchange(other.mask_, 0)), wake_(std::exchange(other.wake_, nullptr)) {}
	SurfacePresentationLease& SurfacePresentationLease::operator=(SurfacePresentationLease&& other) noexcept
	{
		if (this != &other)
		{
			Reset(); active_ = std::exchange(other.active_, nullptr); revisions_ = std::exchange(other.revisions_, nullptr);
			mask_ = std::exchange(other.mask_, 0); wake_ = std::exchange(other.wake_, nullptr);
		}
		return *this;
	}
	void SurfacePresentationLease::Reset() noexcept
	{
		if (mask_ && active_)
		{
			Detail::ReleasePresentationBits(*active_, mask_);
			if (wake_) SetEvent(wake_);
		}
		active_ = nullptr; revisions_ = nullptr; mask_ = 0; wake_ = nullptr;
	}
	std::uint64_t SurfacePresentationLease::Revision(WindowRole role) const noexcept
	{
		return IsValidRole(role) && (mask_ & (std::uint64_t{1} << RoleIndex(role))) && revisions_
			? revisions_[RoleIndex(role)].load(std::memory_order_acquire) : 0;
	}
	std::uint64_t SurfacePresentationLease::PixelsChanged(WindowRole role) noexcept
	{
		return IsValidRole(role) && (mask_ & (std::uint64_t{1} << RoleIndex(role))) && revisions_
			? revisions_[RoleIndex(role)].fetch_add(1, std::memory_order_acq_rel) + 1 : 0;
	}

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

	SettingPlatformCapabilities QuerySettingPlatformCapabilities() noexcept
	{
		static const auto platform = []() noexcept -> SettingPlatformCapabilities
		{
			// 不依赖兼容性清单；Win8+ 的 DWM 查询可能为旧程序返回兼容 FALSE。
			using RtlGetVersionProc = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
			const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
			const auto rtlGetVersion = ntdll
				? reinterpret_cast<RtlGetVersionProc>(GetProcAddress(ntdll, "RtlGetVersion"))
				: nullptr;
			RTL_OSVERSIONINFOW version{};
			version.dwOSVersionInfoSize = sizeof(version);
			if (!rtlGetVersion || rtlGetVersion(&version) != 0) return {};
			return ResolveSettingPlatformCapabilities(version.dwMajorVersion,
				version.dwMinorVersion, version.dwBuildNumber);
		}();
		return platform;
	}

	bool UsesLegacySettingFrame() noexcept
	{
		return QuerySettingPlatformCapabilities().legacyFrame;
	}

	bool IsSettingCompositionEnabled() noexcept
	{
		const auto platform = QuerySettingPlatformCapabilities();
		if (platform.compositionAlwaysEnabled) return true;
		BOOL enabled = FALSE;
		const HRESULT result = DwmIsCompositionEnabled(&enabled);
		return ResolveSettingCompositionEnabled(platform, SUCCEEDED(result), enabled != FALSE);
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
		const bool compositionEnabled = IsSettingCompositionEnabled();
		// Win10 不支持可见边框属性；保留非零顶部会让 DWM 在自绘窗口外重画系统标题栏。
		UINT visibleBorder = 0;
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

	bool IsSettingModalWakeCurrent(HWND hwnd, std::uintptr_t generation) noexcept
	{
		// Handle 读取实际记录，不依赖 running；Stop 的 wake 也必须通过代际验证。
		return MessageDispatch::MatchesSettingModalWake(
			hwnd && hwnd == GetService().Handle(WindowRole::Setting),
			hwnd && GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId(), generation,
			hwnd ? reinterpret_cast<std::uintptr_t>(GetPropW(hwnd, Detail::WindowGenerationProperty)) : 0);
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
	bool Service::Show(WindowRole role, std::function<bool()> stillDesired) { return impl_->Show(role, std::move(stillDesired)); }
	bool Service::Hide(WindowRole role, std::function<bool()> stillDesired) { return impl_->Hide(role, std::move(stillDesired)); }
	bool Service::HideAllUserWindows() { return impl_->HideAllUserWindows(); }
	bool Service::RequestHideAllUserWindows() { return impl_->RequestHideAllUserWindows(); }
	bool Service::SetDrawpadSurfaceVisibility(DrawpadSurfaceVisibility visibility,
		std::function<bool()> stillDesired)
	{
		return impl_->SetDrawpadSurfaceVisibility(visibility,
			std::move(stillDesired));
	}
	bool Service::SetBounds(WindowRole role, const RECT& bounds, std::function<bool()> stillDesired) { return impl_->SetBounds(role, bounds, std::move(stillDesired)); }
	SurfacePresentationLease Service::TryAcquirePresentationLease(WindowRole first, WindowRole second) noexcept { return impl_->TryAcquirePresentationLease(first, second); }
	WorkspaceWindowReceipt Service::RequestPrepareWorkspaceWindowPolicy(WorkspaceWindowPolicy policy, std::function<bool()> guard) { return impl_->RequestPrepareWorkspaceWindowPolicy(policy, std::move(guard)); }
	WorkspaceWindowReceipt Service::RequestCompleteWorkspaceWindowPolicy(WorkspaceWindowPolicy policy, const WorkspaceWindowPrepared& prepared, std::function<bool()> guard) { return impl_->RequestCompleteWorkspaceWindowPolicy(policy, prepared, std::move(guard)); }
	void Service::SetWorkspaceProgressObserver(std::function<void()> callback) { impl_->SetWorkspaceProgressObserver(std::move(callback)); }
	bool Service::RequestPromotePptWindow(WindowRole role) { return impl_->RequestPromotePptWindow(role); }
	bool Service::RequestTopmostRefreshAsync() { return impl_->RequestTopmostRefreshAsync(); }
	std::uint64_t Service::PublishCandidate(const WindowCandidate& candidate) { return impl_->PublishCandidate(candidate); }
	std::optional<WindowCandidateReceipt> Service::TryReadCandidateReceipt(WindowRole role, std::uint64_t revision) const { return impl_->TryReadCandidateReceipt(role, revision); }
	void Service::RequestCancelCandidatePreservingReceipt(WindowRole role, std::uint64_t revision) noexcept { impl_->RequestCancelCandidatePreservingReceipt(role, revision); }
	std::optional<WindowCandidateReceipt> Service::TryReadCandidateRetirement(WindowRole role, std::uint64_t revision) const { return impl_->TryReadCandidateRetirement(role, revision); }
	bool Service::CandidateCurrent(WindowRole role, std::uint64_t revision) const noexcept { return impl_->CandidateCurrent(role, revision); }
	void Service::CancelCandidate(WindowRole role, std::uint64_t revision) noexcept { impl_->CancelCandidate(role, revision); }
	std::optional<WindowSurfaceState> Service::SurfaceSnapshot(WindowRole role) const { return impl_->SurfaceSnapshot(role); }
	void Service::SetCandidateDiagnosticsEnabled(bool enabled) noexcept { impl_->SetCandidateDiagnosticsEnabled(enabled); }
	WindowCandidateDiagnostics Service::CandidateDiagnostics() const noexcept { return impl_->CandidateDiagnostics(); }

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
	std::optional<WorkspaceWindowPrepared> Service::PrepareWorkspaceWindowPolicy(
		WorkspaceWindowPolicy policy, std::function<bool()> stillDesired)
	{ return impl_->PrepareWorkspaceWindowPolicy(policy, std::move(stillDesired)); }
	bool Service::CompleteWorkspaceWindowPolicy(WorkspaceWindowPolicy policy,
		const WorkspaceWindowPrepared& prepared, std::function<bool()> stillDesired)
	{ return impl_->CompleteWorkspaceWindowPolicy(policy, prepared, std::move(stillDesired)); }
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
	bool Service::DispatchWhiteboardViewRequest(HWND hwnd, bool minimized) noexcept
	{ return impl_->DispatchWhiteboardViewRequest(hwnd, minimized); }
	bool Service::ConfirmWhiteboardWindowView(HWND hwnd, bool minimized) noexcept
	{ return impl_->ConfirmWhiteboardWindowView(hwnd, minimized); }
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
