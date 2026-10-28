#pragma once

#include "Draw3.Bridge.h"
#include "Draw3.Recovery.h"
#include "Draw3.HapticRequest.h"
#include "Draw3.Shortcut.h"
#include "Draw3.MouseInput.h"
#include "Draw3.SpeedEraser.h"
#include "Draw3.PenDiagnostics.h"
#include "../../Helper/FailedCleanupDeadline.h"

#include <windows.h>
#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
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
				const std::atomic_bool& running, bool waitWhileStopped = false) const noexcept
			{
				std::unique_lock lock(mutex_);
				return condition_.wait_for(lock,
					std::chrono::milliseconds(timeoutMilliseconds),
					[this, revision, &running, waitWhileStopped]
					{
						return Revision() != revision
							|| (!waitWhileStopped && !running.load(std::memory_order_acquire));
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

		struct HostAcceptedStrokeContext
		{
			bool running = false;
			Bridge::Workspace desiredWorkspace = Bridge::Workspace::Desktop;
			std::optional<Bridge::PresentationReadyIdentity> desiredPresentation;
			bool commandScenePending = false;
			bool admissionBlocked = false;
		};

		// Host 与无窗口测试共享逻辑场景准入；不把延后的 Present 回执当作 CPU 场景。
		class HostAcceptedStrokeState
		{
		public:
			void BeginHost() noexcept
			{
				std::scoped_lock lock(mutex_);
				if (++hostGeneration_ == 0) ++hostGeneration_;
				scene_ = {};
			}
			void PublishScene(const Bridge::AcceptedStrokeBegin& scene) noexcept
			{
				std::scoped_lock lock(mutex_);
				scene_ = scene;
			}
			void Stamp(Bridge::AcceptedStrokeBegin& accepted) const noexcept
			{
				std::scoped_lock lock(mutex_);
				accepted.hostGeneration = hostGeneration_;
			}
			[[nodiscard]] std::uint64_t HostGeneration() const noexcept
			{
				std::scoped_lock lock(mutex_);
				return hostGeneration_;
			}
			[[nodiscard]] bool IsCurrent(const Bridge::AcceptedStrokeBegin& accepted,
				const HostAcceptedStrokeContext& current) const noexcept
			{
				std::scoped_lock lock(mutex_);
				if (!current.running || current.commandScenePending ||
					accepted.kind == Bridge::CompletedStrokeKind::None ||
					accepted.hostGeneration == 0 || accepted.hostGeneration != hostGeneration_ ||
					accepted.sceneGeneration == 0 || accepted.sceneGeneration != scene_.sceneGeneration ||
					accepted.workspace != scene_.workspace || accepted.pageIndex != scene_.pageIndex ||
					accepted.presentation != scene_.presentation ||
					accepted.workspace != current.desiredWorkspace) return false;
				return accepted.workspace != Bridge::Workspace::Presentation ||
					(accepted.presentation && !current.admissionBlocked &&
						accepted.presentation == current.desiredPresentation);
			}

		private:
			mutable std::mutex mutex_;
			std::uint64_t hostGeneration_ = 0;
			Bridge::AcceptedStrokeBegin scene_;
		};
	}

	// 隐藏窗口验收使用的 mailbox 消息；默认不会开启，产品输入仍由唯一选定后端生产。
	inline constexpr UINT kDraw3HiddenTestContactMessage = WM_APP + 0x3D3u;
	inline constexpr WPARAM kHiddenTestMouseFlag = 0x100u;
	inline constexpr WPARAM kHiddenTestRightMouseFlag = 0x1000u;
	inline constexpr WPARAM kHiddenTestPenTailFlag = 0x2000u;
	inline constexpr WPARAM kHiddenTestTouchFlag = 0x200u;
	inline constexpr WPARAM kHiddenTestIntegratedPenFlag = 0x400u;
	inline constexpr WPARAM kHiddenTestExternalPenFlag = 0x800u;
	inline constexpr WPARAM kHiddenTestNoPressureFlag = 0x4000u;
	inline constexpr WPARAM kHiddenTestDelayedUpFlag = 0x8000u;
	enum class HiddenTestContactPhase : std::uint32_t
	{
		Down = 0,
		Move = 1,
		Up = 2,
		Cancelled = 3,
		Hover = 4,
	};

	struct HostStyleCallbacks
	{
		void* context = nullptr;
		bool (*setExtendedStyleFlags)(void*, DWORD, DWORD) = nullptr;
	};

	// 只复制拒绝快照；控制台输出由产品调用方退出生命周期闸门后执行。
	struct HostHapticClickDiagnostics
	{
		const char* rejection = nullptr;
		Haptics::Context context;
	};

	struct HostRuntimeCallbacks
	{
		void* context = nullptr;
		void (*drawingActivityChanged)(void*, bool) noexcept = nullptr;
		// 同步发布当前画布目标；产品层在返回前更新 DIP 状态及其 bridge。
		void (*penWidthContextChanged)(void*,
			const Inkeys::Business::PenWidth::Context&) noexcept = nullptr;
		void (*acceptedStrokeBegan)(void*, const Bridge::AcceptedStrokeBegin&) noexcept = nullptr;
		void (*whiteboardThemeEdited)(void*, WhiteboardTheme) noexcept = nullptr;
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

	enum class HostInputMode : std::uint8_t
	{
		Disabled,
		RtsExtended,
		RtsBasicXY,
		Win32Mouse,
	};

	enum class HostStartupStage : std::uint8_t
	{
		WindowAttached,
		GraphicsReady,
		PresenterReady,
		InputReady,
		ControllerReady,
		FirstFrameCommitted,
	};

	struct HostStartOptions
	{
		HostPresentationMode requiredPresentationMode = HostPresentationMode::Automatic;
		// 仅 --draw3-hidden-test 开启合成 mailbox contact 注入，正式产品保持关闭。
		bool enableHiddenTestContactInjection = false;
		// legacy-compatible HWND 重启后禁止再次选择 DComp。
		bool allowDirectComposition = true;
		// 产品需要页码提交确认；独立 Host 无 PageControl 时可显式省略。
		bool requirePresentationUiReady = false;
		// 空路径关闭持久化 worker；产品固定传入 <程序根目录>/Inkeys/AutoSave。
		std::wstring autoSaveRoot;
		WhiteboardTheme whiteboardDefaultTheme;
		void* startupContext = nullptr;
		void (*startupMilestone)(void*, HostStartupStage) noexcept = nullptr;
		bool enableEraserDiagnostics = false; // 默认关闭；仅发布有界快照，不逐点写日志。
		std::optional<SpeedEraser::DisplayScale> hiddenTestDisplayScale;
		// 只在已知启动失败后激活；按值保活，不借用 Main 的局部 scope。
		Shutdown::FailedCleanupSignal failedCleanup;
		// 仅验真 hidden fixture 借用；事件保留到真实 owner join 或进程死亡。
		HANDLE successfulPresentReachedEvent = nullptr;
		HANDLE continueSuccessfulPresentEvent = nullptr;
		// 显式诊断只在本次 run 创建会话；越界容量仅关闭指标，不阻止产品启动。
		bool enableRuntimeMetrics = false;
		std::size_t runtimeMetricsMaximumSamples = 32768;
		// 启动时按值捕获；呈现回退与设备丢失后的恢复沿用本代偏好。
		bool enableHardwareAcceleration = true;
		bool enableZhaoxinCompatibility = true;
	};

	// 只供显式 hidden caller 读取 worker 事实，不加到普通帧/RuntimeSnapshot 热路。
	struct HostHiddenPersistenceSnapshot
	{
		std::uint64_t desktopAccepted = 0, desktopCommitted = 0;
		std::uint64_t desktopFailed = 0, desktopPending = 0;
		std::uint64_t presentationAccepted = 0, presentationCommitted = 0;
		std::uint64_t presentationLoaded = 0, presentationFailed = 0;
		std::uint64_t presentationNotFound = 0; // hidden实测Load终态，不能当Save失败。
	};

	// GPU owner 按值采集；adapter 描述只在启动和释放前复制，快照不持有 COM 对象。
	struct HostRuntimeMetricsMetadata
	{
		bool graphicsAvailable = false;
		bool adapterAvailable = false;
		std::uint32_t driverType = 0;
		std::uint32_t featureLevel = 0;
		HostPresentationMode presentationMode = HostPresentationMode::Automatic;
		HostOutputTarget outputTarget = HostOutputTarget::PrimaryDrawpad;
		std::uint64_t outputRevision = 0;
		std::int64_t qpcFrequency = 0;
		std::array<wchar_t, 128> adapterDescription{};
	};

	// 进度只用于等待；Session 只能由绘制 owner 或真 join 后的生命周期 owner 读取。
	struct HostRuntimeMetricsSnapshot
	{
		bool enabled = false, unavailable = false, joined = false, sealed = false;
		bool startupFailed = false, runFailed = false, exportUnavailable = false;
		std::uint64_t runSerial = 0;
		std::uint64_t contactSeen = 0, confirmed = 0, pending = 0;
		std::uint64_t invalid = 0, contactDropped = 0, pendingOverflow = 0;
		std::uint64_t frameSerial = 0, presentAttempts = 0;
		std::uint64_t allocatedBytes = 0, payloadByteUpperBound = 0, effectiveSamples = 0;
		HostRuntimeMetricsMetadata metadata;
	};

	// 原子快照仅用于无窗口验收和故障诊断，不暴露 Renderer/Document 所有权。
	struct HostRuntimeSnapshot
	{
		bool running = false;
		std::uint64_t hostGeneration = 0;
		std::uint64_t appliedProductRevision = 0;
		Bridge::Tool appliedTool = Bridge::Tool::Pen;
		bool firstFrameReady = false;
		bool lastPresentSucceeded = false;
		HostPresentationMode presentationMode = HostPresentationMode::Automatic;
		HostInputMode inputMode = HostInputMode::Disabled;
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
		WhiteboardTheme whiteboardTheme;
		std::uint64_t whiteboardGeneration = 0;
		bool whiteboardLoadPending = false;
		bool whiteboardUnsaved = false;
		bool canUndo = false;
		bool canRedo = false;
		std::uint64_t contentRevision = 0;
		Bridge::CompletedStrokeKind completedStrokeKind =
			Bridge::CompletedStrokeKind::None;
		bool selectionMode = true;
		Bridge::Workspace workspace = Bridge::Workspace::Desktop;
		std::optional<Bridge::PresentationReadyIdentity> presentationReady;
		std::optional<Bridge::PresentationReadyIdentity> presentationUiReady;
		std::optional<Bridge::PresentationReadyIdentity> presentationSuspended;
		bool commandScenePending = false;
		bool consumerFatal = false;
		CanvasRecoveryState recovery;
		bool presentationInputReady = false;
		HostOutputTarget requestedOutputTarget = HostOutputTarget::PrimaryDrawpad;
		std::uint64_t requestedOutputRevision = 0;
		HostOutputTarget readyOutputTarget = HostOutputTarget::PrimaryDrawpad;
		std::uint64_t readyOutputRevision = 0;
		std::uint64_t presentedContentRevision = 0;
		bool auxiliaryFullFrameClean = false;
		std::uint64_t runtimeRevision = 0;
		RECT lastDirtyRect{};
		SpeedEraser::Diagnostics eraser;
		PenRuntimeDiagnostics pen;
		bool touchContactAreaAssistanceEnabled = false;
		HostRuntimeMetricsSnapshot runtimeMetrics;
	};

#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	// 无窗口生产回归：PPT 终态保留、FIFO 屏障与代际清理。
	int RunPresentationCompletionDeliveryTest() noexcept;
#endif

	// 无窗口生产接缝：真实 Host 准入与 contact 生命周期，不启动设备或附着 HWND。
	int RunHostWorkspaceTransitionInputProductionProbe() noexcept;

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
		// 串行 Start/Stop owner 在真 join 后调用；caller 提供新私有绝对路径，旧报告读完再 Start。
		bool WriteRuntimeMetrics(const wchar_t* absoluteOutputPath) const noexcept;
		bool Running() const noexcept;
		bool FirstFrameReady() const noexcept;
		HostRuntimeSnapshot RuntimeSnapshot() const noexcept;
		bool RetryCanvasRecovery(const CanvasRecoveryState& expected,
			const std::function<bool()>& stillDesired = {}) noexcept;
		// 异步 UI 起笔通知只验证场景/生命周期；短笔结束后同场景仍有效。
		bool IsAcceptedStrokeCurrent(const Bridge::AcceptedStrokeBegin& accepted) const noexcept;
		// Caller 仅在成功 Start 后且不并行下一 Start 时读取；未启用 hidden 则 nullopt。
		std::optional<HostHiddenPersistenceSnapshot> HiddenPersistenceSnapshot() const noexcept;
		// 保存期望值并唤醒绘制线程，启动前设置也保留到下一次 Start。
		void SetInterruptedStrokeReconnectEnabled(bool enabled) noexcept;
		// 临时开发策略在既有显示配置发布路径应用，活动批次不换代。
		void SetEraserDevelopmentOptions(const SpeedEraser::DevelopmentOptions& options);
		SpeedEraser::DevelopmentOptions EraserDevelopmentOptions() const;
		SpeedEraser::DisplayScale EraserDisplayScaleSnapshot() const;
		void SetHiddenTestContactArea(const SpeedEraser::ContactAreaSample& sample);
		// 内容 revision 变化或超时后返回；用于产品状态线程即时响应绘制线程更新。
		bool WaitForContentRevision(std::uint64_t revision,
			std::uint32_t timeoutMilliseconds) const noexcept;
		bool WaitForRuntimeRevision(std::uint64_t revision,
			std::uint32_t timeoutMilliseconds, bool waitWhileStopped = false) const noexcept;
		// 只推进通知版本，不写 Frame/UI-ready；原生/背景完成复用同一个无丢醒握手。
		void NotifyWorkspaceProgress() noexcept;
		LRESULT ForwardMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

		Haptics::Context HapticContext() const noexcept;
		void SetHapticPreferences(bool drawingEnabled, bool buttonsEnabled) noexcept;
		bool PublishHapticClick(const Haptics::Click& source,
			HostHapticClickDiagnostics* diagnostics = nullptr) noexcept;
		Bridge::StateBridge& ProductBridge() noexcept;
		void PublishState(const Bridge::ProductState& state) noexcept;
		bool SetWorkspaceTransitionPending(std::uint64_t epoch, bool pending,
			std::uint64_t expectedHostGeneration) noexcept;
		Bridge::CommandResult PublishCommand(Bridge::CommandType command) noexcept;
		Bridge::CommandResult PublishWhiteboardTheme(WhiteboardTheme theme, std::uint64_t generation, std::size_t page) noexcept;
		Shortcuts::Context ShortcutContext() const noexcept;
		std::uint64_t PublishShortcutCommand(Bridge::CommandType type,
			const Shortcuts::Scope& expected, std::uint64_t leaseId,
			std::optional<std::uint64_t> expectedContentRevision = std::nullopt) noexcept;
		bool TakeShortcutCompletion(std::uint64_t id, Shortcuts::Completion& result) noexcept;
		void RetireShortcutRequest(std::uint64_t id) noexcept;
		std::uint64_t AcquireShortcutLease(const Shortcuts::Context& expected,
			bool requireContentRevision) noexcept;
		void ReleaseShortcutLease(std::uint64_t id) noexcept;
		// 生命周期事务控制 Drawpad 是否允许本次鼠标输入激活窗口。
		void SetActivationAllowed(bool enabled) noexcept;
		bool PublishPresentationUiReady(
			const Bridge::PresentationReadyIdentity& identity) noexcept;
		bool SetPresentationInputSuspended(
			const Bridge::PresentationReadyIdentity& expected, bool suspended) noexcept;

	private:
		friend int RunHostWorkspaceTransitionInputProductionProbe() noexcept;
		bool PublishHiddenTestContact(WPARAM phase, LPARAM position) noexcept;

		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
