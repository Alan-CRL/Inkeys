module;

#include <windows.h>

#include "../Helper/FailedCleanupDeadline.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module Inkeys.Window;

export import Inkeys.Message;

export namespace Inkeys::Window
{
	enum class WindowRole : std::uint8_t
	{
		MagnifierHost,
		MagnifierChild,
		Freeze,
		DrawpadPresentation,
		Drawpad,
		PptBottomLeft,
		PptBottomRight,
		PptMiddleLeft,
		PptMiddleRight,
		Bar,
		Setting,
		DisplayObserver,
		Count,
	};

	// 先按标准桌面样式创建；旧 Aero 建立系统菜单后仅移除 caption，现代 DWM 保持原样。
	inline constexpr DWORD SettingWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
	inline constexpr DWORD SettingWindowExStyle = WS_EX_APPWINDOW;
	[[nodiscard]] SIZE QuerySettingFrameThickness(UINT dpi = 0) noexcept;
	[[nodiscard]] bool UsesLegacySettingFrame() noexcept;
	struct SettingPlatformCapabilities
	{
		bool legacyFrame = true;
		bool compositionAlwaysEnabled = false;
		bool acrylic = false;
	};
	[[nodiscard]] constexpr SettingPlatformCapabilities ResolveSettingPlatformCapabilities(
		DWORD major, DWORD minor, DWORD build) noexcept
	{
		const bool modernDwm = major > 6 || (major == 6 && minor >= 2);
		return { !modernDwm, modernDwm, major > 10 || (major == 10 && build >= 17134) };
	}
	[[nodiscard]] constexpr bool ResolveSettingCompositionEnabled(
		SettingPlatformCapabilities platform, bool querySucceeded, bool queriedEnabled) noexcept
	{
		return platform.compositionAlwaysEnabled || (querySucceeded && queriedEnabled);
	}
	[[nodiscard]] SettingPlatformCapabilities QuerySettingPlatformCapabilities() noexcept;
	[[nodiscard]] bool IsSettingCompositionEnabled() noexcept;
	[[nodiscard]] bool IsSettingModalWakeCurrent(HWND hwnd, std::uintptr_t generation) noexcept;
	// RECT 四字段表示正向 inset；客户区和 resize 热区分别使用各自的度量。
	[[nodiscard]] RECT QuerySettingClientFrameInsets(HWND hwnd = nullptr,
		bool maximized = false, UINT dpi = 0) noexcept;
	enum class DrawpadSurfaceVisibility : std::uint8_t
	{
		Primary,
		Presentation,
		Hidden,
	};

	struct WorkspaceWindowPolicy
	{
		std::uint64_t revision = 0;
		bool whiteboard = false;
		bool selectionMode = true;
		bool hideDrawpad = false;
		bool minimized = false;
		bool operator==(const WorkspaceWindowPolicy&) const = default;
	};
	struct WorkspaceWindowSurface
	{
		HWND hwnd = nullptr;
		std::uint64_t generation = 0;
		RECT bounds{};
	};
	struct WorkspaceWindowPrepared
	{
		WorkspaceWindowPolicy policy;
		WorkspaceWindowSurface freeze, primary, presentation;
		std::uint64_t displayGeneration = 0;
		HMONITOR targetMonitor = nullptr;
		RECT monitorBounds{};
		UINT dpiX = 96, dpiY = 96;
		std::uint64_t safetyRevision = 0;
	};

	enum class OverlayActivationMode : std::uint8_t
	{
		Presentation,
		Whiteboard,
	};

	struct OverlayActivationStyle
	{
		DWORD setExStyle = 0;
		DWORD clearExStyle = 0;
		bool taskbarAnchor = false;
		bool acceptsActivation = false;
	};

	[[nodiscard]] constexpr OverlayActivationStyle ResolveOverlayActivationStyle(
		WindowRole role, OverlayActivationMode mode) noexcept
	{
		if (mode == OverlayActivationMode::Whiteboard)
		{
			if (role == WindowRole::Freeze)
				return { WS_EX_APPWINDOW, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
					true, true };
			if (role == WindowRole::Drawpad)
				return { WS_EX_TOOLWINDOW, WS_EX_NOACTIVATE, false, true };
		}
		return { WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, WS_EX_APPWINDOW,
			false, false };
	}

	[[nodiscard]] constexpr bool RequiresOverlayActivationStyleTransition(
		WindowRole role, OverlayActivationMode from,
		OverlayActivationMode to) noexcept
	{
		const auto before = ResolveOverlayActivationStyle(role, from);
		const auto after = ResolveOverlayActivationStyle(role, to);
		return before.setExStyle != after.setExStyle
			|| before.clearExStyle != after.clearExStyle;
	}

	// 系统关闭只请求业务退出，不能销毁仍承载其他覆盖层的白板锚点。
	[[nodiscard]] inline bool DispatchWhiteboardCloseRequest(bool managedAnchor, bool whiteboardActive,
		const std::function<void()>& closeRequested) noexcept
	{
		if (!managedAnchor || (!whiteboardActive && !closeRequested)) return false;
		try { if (whiteboardActive && closeRequested) closeRequested(); }
		catch (...) {} // 回调异常或未安装也保留共享 HWND，不能落入 DestroyWindow。
		return true;
	}

	[[nodiscard]] inline bool DispatchWhiteboardViewIntent(bool managedAnchor, bool whiteboardActive,
		bool minimized, bool minimizeAllowed, const std::function<void(bool)>& requested) noexcept
	{
		if (!managedAnchor || !whiteboardActive || !requested) return false;
		try { if (!minimized || minimizeAllowed) requested(minimized); } catch (...) {}
		return true;
	}

	struct WindowSpec
	{
		WindowRole role = WindowRole::Bar;
		std::wstring className;
		std::wstring title;
		int x = 0;
		int y = 0;
		int width = 1;
		int height = 1;
		DWORD style = 0;
		DWORD exStyle = 0;
		UINT classStyle = CS_HREDRAW | CS_VREDRAW;
		WNDPROC windowProc = nullptr;
		HICON largeIcon = nullptr;
		HICON smallIcon = nullptr;
		HCURSOR cursor = nullptr;
		HBRUSH background = nullptr;
		bool visible = false;
		bool bindMessages = true;
		bool optional = false;
		Message::BindOptions messageOptions{};
		Message::Channel::Callback messageCallback;
		std::function<bool()> beforeCreate;
		std::function<void(HWND)> created;
		std::function<void()> destroyed;
		// Freeze owner 线程调用业务策略与非阻塞渲染唤醒，窗口层不依赖 UI 模块。
		std::function<bool()> whiteboardMinimizeAllowed;
		std::function<void()> whiteboardMinimizedChanged;
		std::function<void(bool)> whiteboardViewRequested;
		std::function<void()> whiteboardCloseRequested;
	};

	enum class CommandPhase { Queued, Executing, Committed, Rejected, Expired, Cancelled };

	// 回执只持有独立完成状态；Service 停止后仍可确认超时命令是否已经退休。
	class CommandReceipt
	{
	public:
		CommandReceipt() = default;
		CommandReceipt(std::function<CommandPhase()> phase, std::function<void()> cancel)
			: phase_(std::move(phase)), cancel_(std::move(cancel)) {}
		[[nodiscard]] CommandPhase Phase() const { return phase_ ? phase_() : CommandPhase::Rejected; }
		void Cancel() const { if (cancel_) cancel_(); }
	private:
		std::function<CommandPhase()> phase_;
		std::function<void()> cancel_;
	};

	class Service
	{
	public:
		explicit Service(std::size_t messageCapacity = 63);
		~Service();
		Service(const Service&) = delete;
		Service& operator=(const Service&) = delete;
		Service(Service&&) = delete;
		Service& operator=(Service&&) = delete;

		[[nodiscard]] bool Start(std::vector<WindowSpec> specs,
			Shutdown::FailedCleanupSignal failedCleanup = {});
		void StopAndJoin() noexcept;
		void Stop() noexcept;
		[[nodiscard]] bool Running() const noexcept;
		void BeginShutdown() noexcept;

		[[nodiscard]] HWND Handle(WindowRole role) const noexcept;
		[[nodiscard]] bool Ready(WindowRole role) const noexcept;
		// Overlay ready 不包含独立生命周期的 Setting 窗口。
		[[nodiscard]] bool AllReady() const noexcept;
		[[nodiscard]] bool OverlayReady() const noexcept;
		[[nodiscard]] bool SettingReady() const noexcept;
		[[nodiscard]] DWORD OwnerThreadId(WindowRole role) const noexcept;
		[[nodiscard]] HWND OverlayRoot() const noexcept;
		[[nodiscard]] std::wstring Title(WindowRole role) const;
		[[nodiscard]] static HWND LastFocusWindow() noexcept;

		[[nodiscard]] bool Create(WindowSpec spec);
		[[nodiscard]] bool Destroy(WindowRole role);
		[[nodiscard]] bool Show(WindowRole role, std::function<bool()> stillDesired = {});
		[[nodiscard]] bool Hide(WindowRole role, std::function<bool()> stillDesired = {});
		[[nodiscard]] bool HideAllUserWindows();
		// 受控退出用：只投递给各 HWND owner，不等待可能已经卡住的 owner 完成。
		[[nodiscard]] bool RequestHideAllUserWindows();
		[[nodiscard]] bool SetDrawpadSurfaceVisibility(
			DrawpadSurfaceVisibility visibility,
			std::function<bool()> stillDesired = {});
		[[nodiscard]] bool SetBounds(WindowRole role, const RECT& bounds, std::function<bool()> stillDesired = {});
		[[nodiscard]] bool SetClickThrough(WindowRole role, bool enabled,
			std::function<bool()> stillDesired = {});
		// Setting 保持顶层应用窗口，只在自身 owner thread 动态加入或离开 Drawpad owner 链。
		[[nodiscard]] bool SetSettingOwnedByDrawpad(bool enabled);
		// 只在窗口所属 owner thread 修改扩展样式；调用方不得直接触碰 HWND 样式。
		[[nodiscard]] bool SetExtendedStyleFlags(
			WindowRole role, DWORD setMask, DWORD clearMask);
		// 同一请求分两次轻量 owner 回执；准备后返回消息循环，绝不等待 Render/Host。
		[[nodiscard]] std::optional<WorkspaceWindowPrepared> PrepareWorkspaceWindowPolicy(
			WorkspaceWindowPolicy policy, std::function<bool()> stillDesired = {});
		[[nodiscard]] bool CompleteWorkspaceWindowPolicy(WorkspaceWindowPolicy policy,
			const WorkspaceWindowPrepared& prepared, std::function<bool()> stillDesired = {});
		// Whiteboard 统一切换 taskbar/activation 样式，所有 HWND 操作仍在 owner thread。
		[[nodiscard]] bool EnterWhiteboardWindowMode(std::function<bool()> stillDesired = {});
		[[nodiscard]] bool LeaveWhiteboardWindowMode(std::function<bool()> stillDesired = {});
		[[nodiscard]] bool WhiteboardWindowMode() const noexcept;
		[[nodiscard]] bool WhiteboardWindowGroupMinimized() const noexcept;
		// 仅由匹配的 Freeze owner WndProc 调用，不跨线程执行退出回调。
		[[nodiscard]] bool DispatchWhiteboardClose(HWND hwnd) noexcept;
		[[nodiscard]] bool DispatchWhiteboardViewRequest(HWND hwnd, bool minimized) noexcept;
		[[nodiscard]] bool ConfirmWhiteboardWindowView(HWND hwnd, bool minimized) noexcept;
		[[nodiscard]] bool MinimizeWhiteboardWindowGroup();
		[[nodiscard]] bool RestoreWhiteboardWindowGroup();
		// 仅在打开白板时异步排队一次激活；true 只表示已接纳，不等待或保证系统授予前台。
		[[nodiscard]] bool RequestWhiteboardActivation(std::function<bool()> stillDesired = {});
		[[nodiscard]] bool CancelPointerCapture();
			// 所有跨线程命令默认有限等待；false 后可由回执区分仍在撤回的在途操作。
			[[nodiscard]] bool RequestTopmostRefresh(DWORD timeoutMilliseconds = 1500);
			[[nodiscard]] CommandReceipt LastCommandReceipt() const;
			// 成功 refresh 后在内部锁外调用；传空函数可注销并 drain 正在执行的回调。
			void SetTopmostRefreshObserver(std::function<void()> callback);
			[[nodiscard]] bool SetOverlayTopmost(bool topmost, std::function<bool()> stillDesired = {});
			[[nodiscard]] bool OverlayTopmost() const noexcept;
			// 无焦点 overlay 不会被 Explorer 当成普通全屏窗；显式标记后任务栏才会让出。
			[[nodiscard]] bool SetOverlayFullscreen(bool fullscreen, std::function<bool()> stillDesired = {});
			[[nodiscard]] bool OverlayFullscreen() const noexcept;
			[[nodiscard]] bool PromotePptWindow(WindowRole role);

		[[nodiscard]] bool BindMessages(
			WindowRole role,
			const Message::BindOptions& options = {});
		[[nodiscard]] bool UnbindMessages(WindowRole role);
		[[nodiscard]] bool Enqueue(WindowRole role, Message::Message message);
		[[nodiscard]] bool Get(
			WindowRole role,
			Message::Message& message,
			Message::Filter filter = Message::Filter::All,
			DWORD timeoutMilliseconds = INFINITE);
		[[nodiscard]] bool TryGet(
			WindowRole role,
			Message::Message& message,
			Message::Filter filter = Message::Filter::All);
		std::size_t Clear(
			WindowRole role,
			Message::Filter filter = Message::Filter::All);
		[[nodiscard]] std::size_t MessageCount(WindowRole role) const noexcept;
		[[nodiscard]] std::uint64_t DroppedMessageCount(WindowRole role) const noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> impl_;
	};

	// 生产代码共享一个进程级窗口服务；旧交互代码按 HWND 映射到角色队列。
	[[nodiscard]] Service& GetService() noexcept;
	[[nodiscard]] WindowRole RoleFromHandle(HWND hwnd) noexcept;
	[[nodiscard]] bool Enqueue(HWND hwnd, Message::Message message);
	[[nodiscard]] bool Get(
		HWND hwnd,
		Message::Message& message,
		Message::Filter filter = Message::Filter::All,
		DWORD timeoutMilliseconds = INFINITE);
	[[nodiscard]] bool TryGet(
		HWND hwnd,
		Message::Message& message,
		Message::Filter filter = Message::Filter::All);
	std::size_t Clear(
		HWND hwnd,
		Message::Filter filter = Message::Filter::All);
}
