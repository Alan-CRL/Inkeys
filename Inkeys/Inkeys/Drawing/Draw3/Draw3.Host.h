#pragma once

#include "Draw3.Bridge.h"

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Inkeys::Drawing::Draw3
{
	// 隐藏窗口验收使用的 mailbox 消息；默认不会开启，产品输入仍由唯一 RTS 生产。
	inline constexpr UINT kDraw3HiddenTestContactMessage = WM_APP + 0x3D3u;
	enum class HiddenTestContactPhase : std::uint32_t
	{
		Down = 0,
		Move = 1,
		Up = 2,
		Cancelled = 3,
	};

	struct HostStyleCallbacks
	{
		void* context = nullptr;
		bool (*setExtendedStyleFlags)(void*, DWORD, DWORD) = nullptr;
	};

	enum class HostPresentationMode : std::uint8_t
	{
		Automatic,
		UlwDirtyRect,
		DirectCompositionVisualTree,
		DwmBlurBehind,
		DwmBlurBehind2,
	};

	enum class HostOutputTarget : std::uint8_t
	{
		PrimaryDrawpad,
		SelectionUlw,
	};

	struct HostStartOptions
	{
		HostPresentationMode requiredPresentationMode = HostPresentationMode::Automatic;
		// 仅 --draw3-hidden-test 开启合成 mailbox contact 注入，正式产品保持关闭。
		bool enableHiddenTestContactInjection = false;
		// Debug 命令行可显式开启 RTS 数据路径；不与其他控制台开关联动。
		bool enableRtsTrace = false;
		// legacy-compatible HWND 重启后禁止再次选择 DComp。
		bool allowDirectComposition = true;
	};

	// 原子快照仅用于无窗口验收和故障诊断，不暴露 Renderer/Document 所有权。
	struct HostRuntimeSnapshot
	{
		bool running = false;
		bool firstFrameReady = false;
		bool lastPresentSucceeded = false;
		HostPresentationMode presentationMode = HostPresentationMode::Automatic;
		std::uint64_t presentCount = 0;
		std::uint64_t successfulPresentCount = 0;
		std::uint64_t partialPresentCount = 0;
		std::uint64_t resizeCount = 0;
		std::uint64_t clearCommandCount = 0;
		std::uint64_t undoCommandCount = 0;
		std::uint64_t redoCommandCount = 0;
		std::uint64_t nextPageCommandCount = 0;
		std::uint64_t previousPageCommandCount = 0;
		std::uint64_t inputDownPublished = 0;
		std::uint64_t inputMovePublished = 0;
		std::uint64_t inputTerminalPublished = 0;
		std::uint64_t inputRecycled = 0;
		std::uint64_t ulwDirtyRectPresentCount = 0;
		std::uint64_t ulwPremultipliedAlphaFailureCount = 0;
		bool ulwTransparentFullFrameVerified = false;
		int committedWidth = 0;
		int committedHeight = 0;
		std::size_t currentPageIndex = 0;
		std::size_t pageCount = 0;
		bool currentPageHasContent = false;
		std::uint64_t contentRevision = 0;
		bool selectionMode = true;
		Bridge::Workspace workspace = Bridge::Workspace::Presentation;
		HostOutputTarget requestedOutputTarget = HostOutputTarget::PrimaryDrawpad;
		std::uint64_t requestedOutputRevision = 0;
		HostOutputTarget readyOutputTarget = HostOutputTarget::PrimaryDrawpad;
		std::uint64_t readyOutputRevision = 0;
		std::uint64_t presentedContentRevision = 0;
		bool auxiliaryFullFrameClean = false;
		std::uint64_t runtimeRevision = 0;
		RECT lastDirtyRect{};
	};

	// 产品生命周期外壳：只附着 Window Service HWND，独立持有 Draw3 设备和 RTS。
	class Host
	{
	public:
		Host();
		~Host();
		Host(const Host&) = delete;
		Host& operator=(const Host&) = delete;

		bool Start(HWND drawpad, HWND drawpadPresentation,
			HostStyleCallbacks styleCallbacks = {},
			HostStartOptions options = {});
		void Stop() noexcept;
		bool Running() const noexcept;
		bool FirstFrameReady() const noexcept;
		HostRuntimeSnapshot RuntimeSnapshot() const noexcept;
		// 内容 revision 变化或超时后返回；用于产品状态线程即时响应绘制线程更新。
		bool WaitForContentRevision(std::uint64_t revision,
			std::uint32_t timeoutMilliseconds) const noexcept;
		bool WaitForRuntimeRevision(std::uint64_t revision,
			std::uint32_t timeoutMilliseconds) const noexcept;
		LRESULT ForwardMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

		Bridge::StateBridge& ProductBridge() noexcept;
		void PublishState(const Bridge::ProductState& state) noexcept;
		Bridge::CommandResult PublishCommand(Bridge::CommandType command) noexcept;
		// 生命周期事务控制 Drawpad 是否允许本次鼠标输入激活窗口。
		void SetActivationAllowed(bool enabled) noexcept;

	private:
		bool PublishHiddenTestContact(WPARAM phase, LPARAM position) noexcept;

		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
