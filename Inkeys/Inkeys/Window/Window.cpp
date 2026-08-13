module;

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Inkeys.Window;

namespace
{
	using Inkeys::Window::WindowRole;
	constexpr std::size_t RoleCount = static_cast<std::size_t>(WindowRole::Count);

	[[nodiscard]] constexpr std::size_t RoleIndex(WindowRole role) noexcept
	{
		return static_cast<std::size_t>(role);
	}

	[[nodiscard]] constexpr bool IsValidRole(WindowRole role) noexcept
	{
		return RoleIndex(role) < RoleCount;
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
			&& role <= WindowRole::PptExitShow;
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
		case WindowRole::Drawpad: return 2;
		default: return -1;
		}
	}

	[[nodiscard]] const wchar_t* DefaultClassName(WindowRole role) noexcept
	{
		switch (role)
		{
		case WindowRole::MagnifierHost: return L"Inkeys.Window.MagnifierHost";
		case WindowRole::MagnifierChild: return L"Inkeys.Window.MagnifierChild";
		case WindowRole::Freeze: return L"Inkeys.Window.Freeze";
		case WindowRole::Drawpad: return L"Inkeys.Window.Drawpad";
		case WindowRole::PptBottomLeft: return L"Inkeys.Window.PptBottomLeft";
		case WindowRole::PptBottomRight: return L"Inkeys.Window.PptBottomRight";
		case WindowRole::PptMiddleLeft: return L"Inkeys.Window.PptMiddleLeft";
		case WindowRole::PptMiddleRight: return L"Inkeys.Window.PptMiddleRight";
		case WindowRole::PptExitShow: return L"Inkeys.Window.PptExitShow";
		case WindowRole::Bar: return L"Inkeys.Window.Bar";
		case WindowRole::Setting: return L"Inkeys.Window.Setting";
		case WindowRole::DisplayObserver: return L"Inkeys.Window.DisplayObserver";
		default: return L"Inkeys.Window.Unknown";
		}
	}

	[[nodiscard]] LRESULT CALLBACK DefaultWindowProc(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_CLOSE)
		{
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

		[[nodiscard]] bool Start(std::vector<WindowSpec> specs)
		{
			std::scoped_lock lifecycleLock(lifecycleMutex_);
			if (running_.load(std::memory_order_acquire))
				return false;

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

			overlayThread_ = std::jthread(
				[this, promise = std::move(overlayPromise)](std::stop_token token) mutable
				{
					RunGroup(false, token, std::move(promise));
				});
			settingThread_ = std::jthread(
				[this, promise = std::move(settingPromise)](std::stop_token token) mutable
				{
					RunGroup(true, token, std::move(promise));
				});

			const bool started = overlayReady.get() && settingReady.get();
			if (!started)
			{
				StopUnlocked();
				return false;
			}
			return true;
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
				WindowRole::Drawpad,
				WindowRole::PptBottomLeft,
				WindowRole::PptBottomRight,
				WindowRole::PptMiddleLeft,
				WindowRole::PptMiddleRight,
				WindowRole::PptExitShow,
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

		[[nodiscard]] bool SetBounds(WindowRole role, const RECT& bounds)
		{
			Command command;
			command.type = CommandType::SetBounds;
			command.role = role;
			command.bounds = bounds;
			return Submit(std::move(command));
		}

		[[nodiscard]] bool SetClickThrough(WindowRole role, bool enabled)
		{
			Command command;
			command.type = CommandType::SetClickThrough;
			command.role = role;
			command.enabled = enabled;
			return Submit(std::move(command));
		}

		[[nodiscard]] bool RequestTopmostRefresh()
		{
			return Submit(WindowRole::MagnifierHost, CommandType::RefreshTopmost);
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
			SetBounds,
			SetClickThrough,
			RefreshTopmost,
			PromotePpt,
			BindMessages,
			UnbindMessages,
		};

		struct Command
		{
			CommandType type = CommandType::Show;
			WindowRole role = WindowRole::Bar;
			RECT bounds{};
			bool enabled = false;
			Message::BindOptions bindOptions{};
			std::optional<WindowSpec> spec;
			std::shared_ptr<std::promise<bool>> completion;
		};

		struct CommandQueue
		{
			std::mutex mutex;
			std::deque<Command> commands;
		};

		struct WindowRecord
		{
			std::atomic<HWND> hwnd = nullptr;
			std::atomic_bool ready = false;
			std::atomic<DWORD> threadId = 0;
			std::unique_ptr<Message::Channel> channel;
			std::optional<WindowSpec> activeSpec;
			bool messagesBound = false;
			bool classRegistered = false;
			bool lifecycleActive = false;
			std::wstring className;
		};

		[[nodiscard]] WindowRecord* Record(WindowRole role) noexcept
		{
			return IsValidRole(role) ? &records_[RoleIndex(role)] : nullptr;
		}

		[[nodiscard]] const WindowRecord* Record(WindowRole role) const noexcept
		{
			return IsValidRole(role) ? &records_[RoleIndex(role)] : nullptr;
		}

		void ResetState()
		{
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
			}
			{
				std::scoped_lock lock(overlayCommands_.mutex, settingCommands_.mutex);
				overlayCommands_.commands.clear();
				settingCommands_.commands.clear();
			}
		}

		void StopUnlocked() noexcept
		{
			if (!running_.exchange(false, std::memory_order_acq_rel) &&
				!overlayThread_.joinable() && !settingThread_.joinable())
				return;

			overlayThread_.request_stop();
			settingThread_.request_stop();
			if (overlayEvent_) SetEvent(overlayEvent_);
			if (settingEvent_) SetEvent(settingEvent_);
			if (overlayThread_.joinable()) overlayThread_.join();
			if (settingThread_.joinable()) settingThread_.join();

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
				try { command.completion->set_value(false); }
				catch (...) {}
			}
		}

		void RunGroup(bool settingGroup, std::stop_token token, std::promise<bool> readyPromise)
		{
			const DWORD threadId = GetCurrentThreadId();
			(settingGroup ? settingThreadId_ : overlayThreadId_).store(
				threadId, std::memory_order_release);
			bool created = false;
			try
			{
				created = CreateGroup(settingGroup, threadId);
				readyPromise.set_value(created);
			}
			catch (...)
			{
				try { readyPromise.set_value(false); }
				catch (...) {}
			}
			if (!created)
			{
				DestroyGroup(settingGroup);
				(settingGroup ? settingThreadId_ : overlayThreadId_).store(
					0, std::memory_order_release);
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
				const DWORD waitResult = eventHandle
					? MsgWaitForMultipleObjectsEx(
						1, &eventHandle, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE)
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
					DispatchMessageW(&message);
				}
				if (!eventHandle)
					DrainCommands(queue);
			}

			FailPendingCommands(queue);
			DestroyGroup(settingGroup);
			(settingGroup ? settingThreadId_ : overlayThreadId_).store(
				0, std::memory_order_release);
		}

		[[nodiscard]] bool CreateGroup(bool settingGroup, DWORD threadId)
		{
			constexpr WindowRole overlayCreationOrder[] = {
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
				WindowRole::DisplayObserver,
			};

			if (settingGroup)
			{
				const auto& spec = specs_[RoleIndex(WindowRole::Setting)];
				if (!spec || CreateWindowFor(*spec, nullptr, threadId)) return true;
				if (spec->optional)
				{
					configured_[RoleIndex(WindowRole::Setting)].store(
						false, std::memory_order_release);
					return true;
				}
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
				else if (IsUiPopup(role))
					roleOwner = Handle(WindowRole::Drawpad);
				if (!CreateWindowFor(*spec, roleOwner, threadId))
				{
					if (!spec->optional) return false;
					configured_[RoleIndex(role)].store(false, std::memory_order_release);
					continue;
				}
				if (role != WindowRole::MagnifierChild
					&& role != WindowRole::DisplayObserver && !IsUiPopup(role))
					owner = Handle(role);
			}
			return true;
		}

		[[nodiscard]] bool CreateWindowFor(
			const WindowSpec& spec,
			HWND owner,
			DWORD threadId)
		{
			auto& record = records_[RoleIndex(spec.role)];
			// 每次创建都绑定自己的生命周期描述，动态重建不会误用旧回调。
			record.activeSpec = spec;
			if (spec.beforeCreate)
			{
				bool prepared = false;
				try { prepared = spec.beforeCreate(); }
				catch (...) {}
				if (!prepared)
				{
					RollbackCreation(record);
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
					RollbackCreation(record);
					return false;
				}
			}

			DWORD style = spec.style;
			DWORD exStyle = spec.exStyle;
			if (IsSetting(spec.role))
			{
				style = WS_POPUP | WS_CLIPCHILDREN;
				exStyle = (exStyle | WS_EX_APPWINDOW) &
					~(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
				owner = nullptr;
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

			const HWND hwnd = CreateWindowExW(
				exStyle,
				record.className.c_str(),
				spec.title.c_str(),
				style,
				spec.x,
				spec.y,
				spec.width,
				spec.height,
				owner,
				nullptr,
				instance,
				nullptr);
			if (!hwnd)
			{
				const DWORD createError = GetLastError();
				wchar_t diagnostic[256]{};
				swprintf_s(diagnostic, L"Inkeys.Window create failed: role=%u error=%lu class=%s\n",
					static_cast<unsigned>(spec.role), createError, record.className.c_str());
				OutputDebugStringW(diagnostic);
				std::fwprintf(stderr, L"%s", diagnostic);
				RollbackCreation(record);
				return false;
			}
			if (!record.lifecycleActive)
				record.lifecycleActive = true;

			record.hwnd.store(hwnd, std::memory_order_release);
			record.threadId.store(threadId, std::memory_order_release);
			if (spec.bindMessages && record.channel)
			{
				record.messagesBound = record.channel->Bind(hwnd, spec.messageOptions);
				if (!record.messagesBound)
				{
					RollbackCreation(record);
					return false;
				}
			}

			if (IsSetting(spec.role))
			{
				SendMessageW(hwnd, WM_SETICON, ICON_BIG,
					reinterpret_cast<LPARAM>(windowClass.hIcon));
				SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
					reinterpret_cast<LPARAM>(windowClass.hIconSm));
			}
			ShowWindow(hwnd, spec.visible
				? (IsSetting(spec.role) ? SW_SHOW : SW_SHOWNOACTIVATE)
				: SW_HIDE);
			record.ready.store(true, std::memory_order_release);
			if (spec.created)
			{
				try { spec.created(hwnd); }
				catch (...)
				{
					RollbackCreation(record);
					return false;
				}
			}
			return true;
		}

		void DestroyGroup(bool settingGroup) noexcept
		{
			constexpr WindowRole overlayDestructionOrder[] = {
				WindowRole::Bar,
				WindowRole::PptExitShow,
				WindowRole::PptMiddleRight,
				WindowRole::PptMiddleLeft,
				WindowRole::PptBottomRight,
				WindowRole::PptBottomLeft,
				WindowRole::Drawpad,
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
			if (hwnd && IsWindow(hwnd))
				DestroyWindow(hwnd);
			record.hwnd.store(nullptr, std::memory_order_release);
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

		static void RollbackCreation(WindowRecord& record) noexcept
		{
			record.ready.store(false, std::memory_order_release);
			const HWND hwnd = record.hwnd.load(std::memory_order_acquire);
			if (hwnd && record.messagesBound && record.channel)
			{
				(void)record.channel->Unbind(hwnd);
				record.messagesBound = false;
			}
			if (hwnd && IsWindow(hwnd))
				(void)DestroyWindow(hwnd);
			record.hwnd.store(nullptr, std::memory_order_release);
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

		[[nodiscard]] bool Submit(Command command)
		{
			if (!running_.load(std::memory_order_acquire) || !IsValidRole(command.role))
				return false;
			CommandQueue& queue = IsSetting(command.role) ? settingCommands_ : overlayCommands_;
			HANDLE eventHandle = IsSetting(command.role) ? settingEvent_ : overlayEvent_;
			const DWORD ownerThreadId = (IsSetting(command.role)
				? settingThreadId_ : overlayThreadId_).load(std::memory_order_acquire);
			// WNDPROC/lifecycle hook 可能在所属线程内调用服务，此时直接执行避免自锁。
			if (ownerThreadId && ownerThreadId == GetCurrentThreadId())
				return Execute(command);
			command.completion = std::make_shared<std::promise<bool>>();
			auto result = command.completion->get_future();
			{
				std::scoped_lock lock(queue.mutex);
				if (!running_.load(std::memory_order_acquire))
					return false;
				queue.commands.push_back(std::move(command));
			}
			if (eventHandle) SetEvent(eventHandle);
			return result.get();
		}

		void DrainCommands(CommandQueue& queue)
		{
			std::deque<Command> commands;
			{
				std::scoped_lock lock(queue.mutex);
				commands.swap(queue.commands);
			}
			for (auto& command : commands)
			{
				bool result = false;
				try { result = Execute(command); }
				catch (...) { result = false; }
				if (command.completion)
				{
					try { command.completion->set_value(result); }
					catch (...) {}
				}
			}
		}

		[[nodiscard]] bool Execute(const Command& command)
		{
			if (command.type == CommandType::HideAll)
				return HideUserWindowsInGroup(IsSetting(command.role));
			auto* record = Record(command.role);
			const HWND hwnd = record ? record->hwnd.load(std::memory_order_acquire) : nullptr;
			if (command.type == CommandType::Create)
				return command.spec && CreateDynamic(*command.spec);
			if (command.type == CommandType::Destroy)
				return DestroyDynamic(command.role);
			if (command.type != CommandType::RefreshTopmost
				&& (!record || !hwnd || !IsWindow(hwnd)))
				return false;

			switch (command.type)
			{
			case CommandType::Create:
			case CommandType::Destroy:
				return false;
			case CommandType::Show:
				if (IsSetting(command.role))
				{
					ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
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
						const HWND bar = Handle(WindowRole::Bar);
						if (!bar || !IsWindow(bar) ||
							!SetWindowPos(bar, HWND_TOP, 0, 0, 0, 0,
								SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) ||
							!SetWindowPos(hwnd, bar, 0, 0, 0, 0,
								SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
							return false;
					}
				}
				return true;
			case CommandType::Hide:
				ShowWindow(hwnd, SW_HIDE);
				return true;
			case CommandType::HideAll:
				return false;
			case CommandType::SetBounds:
				return SetWindowPos(
					hwnd, nullptr,
					command.bounds.left,
					command.bounds.top,
					command.bounds.right - command.bounds.left,
					command.bounds.bottom - command.bounds.top,
					SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
			case CommandType::SetClickThrough:
			{
				LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
				if (command.enabled) exStyle |= WS_EX_TRANSPARENT;
				else exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
				SetLastError(ERROR_SUCCESS);
				const LONG_PTR previous = SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
				if (!previous && GetLastError() != ERROR_SUCCESS)
					return false;
				return SetWindowPos(
					hwnd, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
			}
			case CommandType::RefreshTopmost:
			{
				const HWND root = OverlayRoot();
				// Win32 会让 owned popup 跟随 owner 进入 topmost band；这里只操作链根。
				return root && SetWindowPos(
					root, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
			}
			case CommandType::PromotePpt:
			{
				if (!IsPpt(command.role)) return false;
				const HWND bar = Handle(WindowRole::Bar);
				if (!bar || !IsWindow(bar)) return false;
				// no-activate 窗口没有焦点自动排序：Bar 固定最上，交互 PPT 紧随其后。
				if (!SetWindowPos(bar, HWND_TOP, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) return false;
				return SetWindowPos(hwnd, bar, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
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
			}
			return false;
		}

		[[nodiscard]] bool HideUserWindowsInGroup(bool settingGroup) noexcept
		{
			if (settingGroup)
			{
				if (const HWND hwnd = Handle(WindowRole::Setting); hwnd && IsWindow(hwnd))
					ShowWindow(hwnd, SW_HIDE);
				return true;
			}

			constexpr WindowRole overlayRoles[] = {
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
			};
			for (const auto role : overlayRoles)
			{
				if (const HWND hwnd = Handle(role); hwnd && IsWindow(hwnd))
					ShowWindow(hwnd, SW_HIDE);
			}
			return true;
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
			else if (!IsSetting(spec.role))
			{
				constexpr WindowRole chain[] = {
					WindowRole::MagnifierHost,
					WindowRole::Freeze,
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

		[[nodiscard]] bool CanChangeChainRole(WindowRole role) const noexcept
		{
			const int position = OverlayChainPosition(role);
			if (position < 0)
				return true;
			constexpr WindowRole chain[] = {
				WindowRole::MagnifierHost,
				WindowRole::Freeze,
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
		std::jthread overlayThread_;
		std::jthread settingThread_;
		std::atomic<DWORD> overlayThreadId_ = 0;
		std::atomic<DWORD> settingThreadId_ = 0;
		HANDLE overlayEvent_ = nullptr;
		HANDLE settingEvent_ = nullptr;
		CommandQueue overlayCommands_;
		CommandQueue settingCommands_;
		std::mutex lifecycleMutex_;
	};

	Service::Service(std::size_t messageCapacity)
		: impl_(std::make_unique<Impl>(messageCapacity))
	{
	}

	Service::~Service() = default;

	bool Service::Start(std::vector<WindowSpec> specs)
	{
		return impl_->Start(std::move(specs));
	}

	void Service::StopAndJoin() noexcept { impl_->Stop(); }
	void Service::Stop() noexcept { StopAndJoin(); }
	bool Service::Running() const noexcept { return impl_->Running(); }
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
	bool Service::SetBounds(WindowRole role, const RECT& bounds) { return impl_->SetBounds(role, bounds); }
	bool Service::SetClickThrough(WindowRole role, bool enabled) { return impl_->SetClickThrough(role, enabled); }
	bool Service::RequestTopmostRefresh() { return impl_->RequestTopmostRefresh(); }
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
