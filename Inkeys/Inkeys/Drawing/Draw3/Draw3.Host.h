#pragma once

#include "Draw3.Bridge.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <string>

namespace Inkeys::Drawing::Draw3
{
	namespace Detail
	{
		// Host 与无窗口测试共用真实 revision 发布/等待实现，不以测试专用 helper 代替唤醒链。
		class HostRuntimeRevisionSignal
		{
		public:
			void Reset() noexcept
			{
				revision_.store(0, std::memory_order_release);
			}
			void Publish() noexcept
			{
				{
					// 与 waiter 建立 mutex 握手，避免谓词检查后、进入等待前丢失通知。
					std::scoped_lock lock(mutex_);
					if (revision_.fetch_add(1, std::memory_order_acq_rel) ==
						(std::numeric_limits<std::uint64_t>::max)())
						revision_.store(1, std::memory_order_release);
				}
				condition_.notify_all();
			}
			bool PublishPageChange(
				std::size_t previousPage, std::size_t previousCount,
				std::size_t currentPage, std::size_t currentCount) noexcept
			{
				if (previousPage == currentPage && previousCount == currentCount)
					return false;
				Publish();
				return true;
			}
			[[nodiscard]] std::uint64_t Revision() const noexcept
			{
				return revision_.load(std::memory_order_acquire);
			}
			bool WaitForChange(std::uint64_t revision,
				std::uint32_t timeoutMilliseconds,
				const std::atomic_bool& running) const noexcept
			{
				std::unique_lock lock(mutex_);
				return condition_.wait_for(lock,
					std::chrono::milliseconds(timeoutMilliseconds),
					[this, revision, &running]
					{
						return Revision() != revision
							|| !running.load(std::memory_order_acquire);
					});
			}
			void NotifyAll() const noexcept
			{
				{
					// 停止状态由外部原子发布；这里同步 waiter 的检查/休眠边界。
					std::scoped_lock lock(mutex_);
				}
				condition_.notify_all();
			}

		private:
			std::atomic<std::uint64_t> revision_ = 0;
			mutable std::mutex mutex_;
			mutable std::condition_variable condition_;
		};

		class HostDrawingActivityState
		{
		public:
			using Callback = void (*)(void*, bool) noexcept;

			void Reset() noexcept
			{
				active_.store(false, std::memory_order_release);
			}
			bool Publish(bool active, void* context, Callback callback) noexcept
			{
				if (active_.exchange(active, std::memory_order_acq_rel) == active)
					return false;
				if (callback) callback(context, active);
				return true;
			}
			bool EndIfActive(void* context, Callback callback) noexcept
			{
				return Publish(false, context, callback);
			}
			[[nodiscard]] bool Active() const noexcept
			{
				return active_.load(std::memory_order_acquire);
			}

		private:
			std::atomic_bool active_ = false;
		};
	}

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

	struct HostRuntimeCallbacks
	{
		void* context = nullptr;
		void (*drawingActivityChanged)(void*, bool) noexcept = nullptr;
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
		// legacy-compatible HWND 重启后禁止再次选择 DComp。
		bool allowDirectComposition = true;
		// 空路径关闭持久化 worker；产品固定传入 <程序根目录>/Inkeys/AutoSave。
		std::wstring autoSaveRoot;
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
		Bridge::Workspace workspace = Bridge::Workspace::Desktop;
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
			HostStartOptions options = {},
			HostRuntimeCallbacks runtimeCallbacks = {});
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
