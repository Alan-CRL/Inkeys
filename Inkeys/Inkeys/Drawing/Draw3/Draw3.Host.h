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

	struct HostStartOptions
	{
		HostPresentationMode requiredPresentationMode = HostPresentationMode::Automatic;
		// 仅 --draw3-hidden-test 开启合成 mailbox contact 注入，正式产品保持关闭。
		bool enableHiddenTestContactInjection = false;
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

		bool Start(HWND drawpad, HostStyleCallbacks styleCallbacks = {},
			HostStartOptions options = {});
		void Stop() noexcept;
		bool Running() const noexcept;
		bool FirstFrameReady() const noexcept;
		HostRuntimeSnapshot RuntimeSnapshot() const noexcept;
		LRESULT ForwardMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

		Bridge::StateBridge& ProductBridge() noexcept;
		void PublishState(const Bridge::ProductState& state) noexcept;
		Bridge::CommandResult PublishCommand(Bridge::CommandType command) noexcept;

	private:
		bool PublishHiddenTestContact(WPARAM phase, LPARAM position) noexcept;

		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
