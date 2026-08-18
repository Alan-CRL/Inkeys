module;

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
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
		WhiteboardLeft,
		WhiteboardRight,
		PptBottomLeft,
		PptBottomRight,
		PptMiddleLeft,
		PptMiddleRight,
		PptExitShow,
		Bar,
		Setting,
		DisplayObserver,
		Count,
	};

	enum class DrawpadSurfaceVisibility : std::uint8_t
	{
		Primary,
		Presentation,
		Hidden,
	};

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

		[[nodiscard]] bool Start(std::vector<WindowSpec> specs);
		void StopAndJoin() noexcept;
		void Stop() noexcept;
		[[nodiscard]] bool Running() const noexcept;

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
		[[nodiscard]] bool Show(WindowRole role);
		[[nodiscard]] bool Hide(WindowRole role);
		[[nodiscard]] bool HideAllUserWindows();
		[[nodiscard]] bool SetDrawpadSurfaceVisibility(
			DrawpadSurfaceVisibility visibility);
		[[nodiscard]] bool SetBounds(WindowRole role, const RECT& bounds);
		[[nodiscard]] bool SetClickThrough(WindowRole role, bool enabled);
		// 只在窗口所属 owner thread 修改扩展样式；调用方不得直接触碰 HWND 样式。
		[[nodiscard]] bool SetExtendedStyleFlags(
			WindowRole role, DWORD setMask, DWORD clearMask);
		[[nodiscard]] bool RequestTopmostRefresh();
		[[nodiscard]] bool SetOverlayTopmost(bool topmost);
		[[nodiscard]] bool OverlayTopmost() const noexcept;
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
