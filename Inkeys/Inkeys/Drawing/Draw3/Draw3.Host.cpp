#include "Draw3.Host.h"

import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Drawing.Draw3.drawing_controller;
import Inkeys.Drawing.Draw3.graphics_initialization;
import Inkeys.Drawing.Draw3.ink_prediction;
import Inkeys.Drawing.Draw3.renderer;
import Inkeys.Drawing.Draw3.realtime_stylus;
import Inkeys.Drawing.Draw3.transparent_presentation;
import Inkeys.Drawing.Draw3.window_control;

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

namespace Inkeys::Drawing::Draw3
{
	struct Host::Impl
	{
		Bridge::StateBridge bridge;
		ContactInputCoordinator input;
		WindowController window;
		GraphicsDeviceResources graphics;
		InkRenderer renderer;
		TransparentPresentationController presentation;
		RealTimeStylusInput stylus;
		std::unique_ptr<DrawingController> drawing;
		std::jthread drawingThread;
		std::atomic_bool running = false;
		std::atomic_bool firstFrameReady = false;
		std::atomic<HWND> attachedWindow = nullptr;
		std::atomic<HWND> attachedPresentationWindow = nullptr;
		std::atomic<HostPresentationMode> presentationMode = HostPresentationMode::Automatic;
		std::atomic<std::uint64_t> presentCount = 0;
		std::atomic<std::uint64_t> successfulPresentCount = 0;
		std::atomic<std::uint64_t> partialPresentCount = 0;
		std::atomic<std::uint64_t> resizeCount = 0;
		std::atomic<std::uint64_t> clearCommandCount = 0;
		std::atomic<std::uint64_t> undoCommandCount = 0;
		std::atomic<std::uint64_t> redoCommandCount = 0;
		std::atomic<std::uint64_t> nextPageCommandCount = 0;
		std::atomic<std::uint64_t> previousPageCommandCount = 0;
		std::atomic<std::uint64_t> ulwDirtyRectPresentCount = 0;
		std::atomic<std::uint64_t> ulwPremultipliedAlphaFailureCount = 0;
		std::atomic_bool ulwTransparentFullFrameVerified = false;
		std::atomic_bool lastPresentSucceeded = false;
		std::atomic<int> committedWidth = 0;
		std::atomic<int> committedHeight = 0;
		std::atomic<std::size_t> currentPageIndex = 0;
		std::atomic<std::size_t> pageCount = 0;
		std::atomic_bool currentPageHasContent = false;
		std::atomic<std::uint64_t> contentRevision = 0;
		std::atomic_bool selectionMode = true;
		std::atomic<Bridge::Workspace> workspace = Bridge::Workspace::Presentation;
		std::atomic<HostOutputTarget> requestedOutputTarget =
			HostOutputTarget::PrimaryDrawpad;
		std::atomic<std::uint64_t> requestedOutputRevision = 0;
		std::atomic<HostOutputTarget> readyOutputTarget =
			HostOutputTarget::PrimaryDrawpad;
		std::atomic<std::uint64_t> readyOutputRevision = 0;
		std::atomic<std::uint64_t> presentedContentRevision = 0;
		std::atomic_bool auxiliaryFullFrameClean = false;
		std::atomic<std::uint64_t> runtimeRevision = 0;
		std::atomic<LONG> lastDirtyLeft = 0;
		std::atomic<LONG> lastDirtyTop = 0;
		std::atomic<LONG> lastDirtyRight = 0;
		std::atomic<LONG> lastDirtyBottom = 0;
		bool hiddenTestContactInjectionEnabled = false;
		std::uint64_t appliedBridgeRevision = (std::numeric_limits<std::uint64_t>::max)();
		bool requestedProductPage = false;
		std::uint32_t requestedProductPageIndex = 0;
		HostStyleCallbacks styleCallbacks = {};
		HostStartOptions startOptions = {};
		std::mutex startupMutex;
		std::condition_variable startupCondition;
		mutable std::mutex contentMutex;
		mutable std::condition_variable contentCondition;
		mutable std::mutex runtimeMutex;
		mutable std::condition_variable runtimeCondition;
		bool graphicsReady = false;
		bool stylusDecision = false;
		bool stylusSucceeded = false;
		bool startupCompleted = false;
		bool startupSucceeded = false;

		static HostPresentationMode ToHostMode(TransparentPresentMode mode) noexcept
		{
			switch (mode)
			{
			case TransparentPresentMode::UlwDirtyRect: return HostPresentationMode::UlwDirtyRect;
			case TransparentPresentMode::DirectCompositionVisualTree:
				return HostPresentationMode::DirectCompositionVisualTree;
			case TransparentPresentMode::DwmBlurBehind: return HostPresentationMode::DwmBlurBehind;
			case TransparentPresentMode::DwmBlurBehind2: return HostPresentationMode::DwmBlurBehind2;
			default: return HostPresentationMode::Automatic;
			}
		}

		static bool IsRequiredMode(HostPresentationMode mode) noexcept
		{
			return mode != HostPresentationMode::Automatic;
		}

		static HostOutputTarget ToHostOutputTarget(
			TransparentOutputTarget target) noexcept
		{
			return target == TransparentOutputTarget::SelectionUlw
				? HostOutputTarget::SelectionUlw
				: HostOutputTarget::PrimaryDrawpad;
		}

		void PublishRuntimeRevision() noexcept
		{
			if (runtimeRevision.fetch_add(1, std::memory_order_release) ==
				(std::numeric_limits<std::uint64_t>::max)())
				runtimeRevision.store(1, std::memory_order_release);
			runtimeCondition.notify_all();
		}

		static TransparentPresentMode ToTransparentMode(HostPresentationMode mode) noexcept
		{
			switch (mode)
			{
			case HostPresentationMode::UlwDirtyRect: return TransparentPresentMode::UlwDirtyRect;
			case HostPresentationMode::DwmBlurBehind: return TransparentPresentMode::DwmBlurBehind;
			case HostPresentationMode::DwmBlurBehind2: return TransparentPresentMode::DwmBlurBehind2;
			case HostPresentationMode::DirectCompositionVisualTree:
			default: return TransparentPresentMode::DirectCompositionVisualTree;
			}
		}

		void ResetRuntimeDiagnostics()
		{
			presentationMode.store(HostPresentationMode::Automatic, std::memory_order_release);
			presentCount.store(0, std::memory_order_release);
			successfulPresentCount.store(0, std::memory_order_release);
			partialPresentCount.store(0, std::memory_order_release);
			resizeCount.store(0, std::memory_order_release);
			clearCommandCount.store(0, std::memory_order_release);
			undoCommandCount.store(0, std::memory_order_release);
			redoCommandCount.store(0, std::memory_order_release);
			nextPageCommandCount.store(0, std::memory_order_release);
			previousPageCommandCount.store(0, std::memory_order_release);
			ulwDirtyRectPresentCount.store(0, std::memory_order_release);
			ulwPremultipliedAlphaFailureCount.store(0, std::memory_order_release);
			ulwTransparentFullFrameVerified.store(false, std::memory_order_release);
			lastPresentSucceeded.store(false, std::memory_order_release);
			currentPageIndex.store(0, std::memory_order_release);
			pageCount.store(0, std::memory_order_release);
			currentPageHasContent.store(false, std::memory_order_release);
			contentRevision.store(0, std::memory_order_release);
			selectionMode.store(true, std::memory_order_release);
			workspace.store(Bridge::Workspace::Presentation, std::memory_order_release);
			requestedOutputTarget.store(
				HostOutputTarget::PrimaryDrawpad, std::memory_order_release);
			requestedOutputRevision.store(0, std::memory_order_release);
			readyOutputTarget.store(
				HostOutputTarget::PrimaryDrawpad, std::memory_order_release);
			readyOutputRevision.store(0, std::memory_order_release);
			presentedContentRevision.store(0, std::memory_order_release);
			auxiliaryFullFrameClean.store(false, std::memory_order_release);
			runtimeRevision.store(0, std::memory_order_release);
			lastDirtyLeft.store(0, std::memory_order_release);
			lastDirtyTop.store(0, std::memory_order_release);
			lastDirtyRight.store(0, std::memory_order_release);
			lastDirtyBottom.store(0, std::memory_order_release);
			appliedBridgeRevision = (std::numeric_limits<std::uint64_t>::max)();
			requestedProductPage = false;
			requestedProductPageIndex = 0;
		}

		static void ObservePresented(void* context, bool succeeded, RECT dirty,
			bool presentFull, TransparentPresentObservation observation)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			// 运行期设备恢复可能切换透明后端，快照和窗口语义随真实 presenter 更新。
			self->presentationMode.store(ToHostMode(self->presentation.ActiveMode()),
				std::memory_order_release);
			self->window.SetGpuTransparentComposition(
				self->presentation.IsGpuTransparentComposition());
			self->presentCount.fetch_add(1, std::memory_order_acq_rel);
			if (succeeded) self->successfulPresentCount.fetch_add(1, std::memory_order_acq_rel);
			if (!presentFull) self->partialPresentCount.fetch_add(1, std::memory_order_acq_rel);
			self->lastPresentSucceeded.store(succeeded, std::memory_order_release);
			self->lastDirtyLeft.store(dirty.left, std::memory_order_relaxed);
			self->lastDirtyTop.store(dirty.top, std::memory_order_relaxed);
			self->lastDirtyRight.store(dirty.right, std::memory_order_relaxed);
			self->lastDirtyBottom.store(dirty.bottom, std::memory_order_relaxed);
			bool runtimeChanged = false;
			const HostOutputTarget outputTarget =
				ToHostOutputTarget(observation.outputTarget);
			runtimeChanged = self->requestedOutputTarget.exchange(
				outputTarget, std::memory_order_acq_rel) != outputTarget || runtimeChanged;
			runtimeChanged = self->requestedOutputRevision.exchange(
				observation.outputRevision, std::memory_order_acq_rel) !=
				observation.outputRevision || runtimeChanged;
			if (succeeded)
			{
				runtimeChanged = self->presentedContentRevision.exchange(
					observation.presentedContentRevision, std::memory_order_acq_rel) !=
					observation.presentedContentRevision || runtimeChanged;
				if (presentFull)
				{
					runtimeChanged = self->readyOutputTarget.exchange(
						outputTarget, std::memory_order_acq_rel) != outputTarget || runtimeChanged;
					runtimeChanged = self->readyOutputRevision.exchange(
						observation.outputRevision, std::memory_order_acq_rel) !=
						observation.outputRevision || runtimeChanged;
				}
				if (outputTarget == HostOutputTarget::SelectionUlw)
				{
					bool clean = self->auxiliaryFullFrameClean.load(
						std::memory_order_acquire);
					if (observation.fullFrameAllZeroAlpha) clean = true;
					else if (presentFull || !observation.updatedRegionAllZeroAlpha) clean = false;
					runtimeChanged = self->auxiliaryFullFrameClean.exchange(
						clean, std::memory_order_acq_rel) != clean || runtimeChanged;
				}
			}
			if (observation.ulw)
			{
				if (observation.usedDirtyRect)
					self->ulwDirtyRectPresentCount.fetch_add(1, std::memory_order_acq_rel);
				if (!observation.premultipliedAlphaValid)
					self->ulwPremultipliedAlphaFailureCount.fetch_add(1, std::memory_order_acq_rel);
				if (observation.fullFrameAllZeroAlpha)
					self->ulwTransparentFullFrameVerified.store(true, std::memory_order_release);
			}
			if (runtimeChanged) self->PublishRuntimeRevision();
		}

		static void ObserveResized(void* context, int width, int height)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			self->committedWidth.store(width, std::memory_order_release);
			self->committedHeight.store(height, std::memory_order_release);
			self->resizeCount.fetch_add(1, std::memory_order_acq_rel);
		}

		static void ObserveCommand(void* context, CanvasCommandType type,
			std::size_t currentPage, std::size_t pages)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			self->currentPageIndex.store(currentPage, std::memory_order_release);
			self->pageCount.store(pages, std::memory_order_release);
			switch (type)
			{
			case CanvasCommandType::Clear:
				self->clearCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::Undo:
				self->undoCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::Redo:
				self->redoCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::NextPage:
				self->nextPageCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::PreviousPage:
				self->previousPageCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			default: break;
			}
		}

		static void ObserveDocument(void* context, std::size_t currentPage, std::size_t pages)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			self->currentPageIndex.store(currentPage, std::memory_order_release);
			self->pageCount.store(pages, std::memory_order_release);
		}

		static void ObserveCurrentPageContent(
			void* context, bool hasContent, std::uint64_t revision)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			{
				std::scoped_lock lock(self->contentMutex);
				if (self->currentPageHasContent.load(
					std::memory_order_relaxed) == hasContent) return;
				self->currentPageHasContent.store(hasContent, std::memory_order_release);
				self->contentRevision.store(revision, std::memory_order_release);
			}
			self->contentCondition.notify_all();
			self->PublishRuntimeRevision();
		}

		static void ObserveWorkspace(void* context, Bridge::Workspace value,
			std::size_t currentPage, std::size_t pages)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			self->workspace.store(value, std::memory_order_release);
			self->currentPageIndex.store(currentPage, std::memory_order_release);
			self->pageCount.store(pages, std::memory_order_release);
			self->requestedProductPage = false;
			self->PublishRuntimeRevision();
			const Bridge::Workspace desired = self->bridge.Snapshot().workspace;
			if (desired != value)
			{
				// 活动 contact 延迟期间目标可能再次变化；确认旧请求后立即补发最新目标。
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(desired);
				self->window.EnqueueCanvasCommand(workspaceCommand);
			}
		}

		static bool ApplyStyle(void* context, DWORD setMask, DWORD clearMask)
		{
			const auto* callbacks = static_cast<const HostStyleCallbacks*>(context);
			// 无产品宿主时允许隐藏测试使用既有样式；产品宿主必须提供 Window Service 回调。
			return !callbacks || !callbacks->setExtendedStyleFlags ||
				callbacks->setExtendedStyleFlags(callbacks->context, setMask, clearMask);
		}

		void PumpBridgeState()
		{
			const Bridge::ProductState state = bridge.Snapshot();
			if (state.revision == appliedBridgeRevision) return;
			appliedBridgeRevision = state.revision;
			window.SetSelectionMode(state.selectionMode);
			const bool selectionChanged = selectionMode.exchange(state.selectionMode,
				std::memory_order_acq_rel) != state.selectionMode;
			DrawingTool tool = DrawingTool::Pen;
			switch (state.tool)
			{
			case Bridge::Tool::HardPen: tool = DrawingTool::HardPen; break;
			case Bridge::Tool::Highlighter: tool = DrawingTool::Highlighter; break;
			case Bridge::Tool::FixedEraser:
			case Bridge::Tool::SpeedEraser: tool = DrawingTool::Eraser; break;
			case Bridge::Tool::Laser: tool = DrawingTool::Laser; break;
			case Bridge::Tool::SolidLine: tool = DrawingTool::SolidLine; break;
			case Bridge::Tool::DashedLine: tool = DrawingTool::DashedLine; break;
			case Bridge::Tool::OutlineRectangle: tool = DrawingTool::OutlineRectangle; break;
			case Bridge::Tool::FilledRectangle: tool = DrawingTool::FilledRectangle; break;
			default: break;
			}
			window.SetActiveTool(tool);
			window.SetEraserWidthMode(state.tool == Bridge::Tool::SpeedEraser
				? EraserWidthMode::Speed : EraserWidthMode::Fixed);
			window.SetProductVisualStyle(state.colorRgba, state.widthDip);
			if (workspace.load(std::memory_order_acquire) != state.workspace)
			{
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(state.workspace);
				window.EnqueueCanvasCommand(workspaceCommand);
			}
			if (state.workspace == Bridge::Workspace::Presentation &&
				workspace.load(std::memory_order_acquire) ==
					Bridge::Workspace::Presentation &&
				state.hasPage && (!requestedProductPage ||
				requestedProductPageIndex != state.page))
			{
				CanvasCommand pageCommand;
				pageCommand.type = CanvasCommandType::SetPage;
				pageCommand.pageIndex = state.page;
				window.EnqueueCanvasCommand(pageCommand);
				requestedProductPage = true;
				requestedProductPageIndex = state.page;
			}
			// 显隐线程观察到新模式前，工具、橡皮模式和样式必须已完整应用。
			if (selectionChanged) PublishRuntimeRevision();
		}

		void PumpBridgeCommands()
		{
			Bridge::Command command;
			while (bridge.TryConsume(command))
			{
				CanvasCommand canvas;
				switch (command.type)
				{
				case Bridge::CommandType::Clear: canvas.type = CanvasCommandType::Clear; break;
				case Bridge::CommandType::Undo: canvas.type = CanvasCommandType::Undo; break;
				case Bridge::CommandType::Redo: canvas.type = CanvasCommandType::Redo; break;
				case Bridge::CommandType::NextPage: canvas.type = CanvasCommandType::NextPage; break;
				case Bridge::CommandType::PreviousPage: canvas.type = CanvasCommandType::PreviousPage; break;
				default: continue;
				}
				window.EnqueueCanvasCommand(canvas);
			}
		}

		static void ConsumeBridge(void* context)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			// ControlWake 只在绘制线程消费产品快照和命令队列。
			self->PumpBridgeState();
			self->PumpBridgeCommands();
		}

		bool Start(HWND hwnd, HWND presentationHwnd,
			HostStyleCallbacks styleCallbacks, HostStartOptions options)
		{
			if (running.load(std::memory_order_acquire) ||
				attachedWindow.load(std::memory_order_acquire) ||
				attachedPresentationWindow.load(std::memory_order_acquire) ||
				!hwnd || !presentationHwnd || !IsWindow(hwnd) ||
				!IsWindow(presentationHwnd)) return false;
			bridge.Reset();
			firstFrameReady.store(false, std::memory_order_release);
			ResetRuntimeDiagnostics();
			this->styleCallbacks = styleCallbacks;
			startOptions = options;
			hiddenTestContactInjectionEnabled = options.enableHiddenTestContactInjection;
			attachedWindow.store(hwnd, std::memory_order_release);
			attachedPresentationWindow.store(presentationHwnd, std::memory_order_release);
			ExternalWindowCallbacks windowCallbacks{};
			if (!window.AttachExternal(hwnd, windowCallbacks))
			{
				attachedWindow.store(nullptr, std::memory_order_release);
				attachedPresentationWindow.store(nullptr, std::memory_order_release);
				hiddenTestContactInjectionEnabled = false;
				return false;
			}
			input.EnableDiagnostics(options.enableHiddenTestContactInjection);
			window.SetInputCoordinator(&input);
			// 启动握手保证绘制线程先拥有独立 GPU 资源，再启用唯一 RTS producer。
			{
				std::scoped_lock lock(startupMutex);
				graphicsReady = false;
				stylusDecision = false;
				stylusSucceeded = false;
				startupCompleted = false;
				startupSucceeded = false;
			}
			running.store(true, std::memory_order_release);
			drawingThread = std::jthread([this](std::stop_token token)
			{
				bool initialized = false;
				bool graphicsInitialized = false;
				try
				{
					const HWND windowHandle = attachedWindow.load(std::memory_order_acquire);
					const HWND presentationWindowHandle =
						attachedPresentationWindow.load(std::memory_order_acquire);
					graphicsInitialized = windowHandle && InitializeGraphicsDevice(graphics);
					if (graphicsInitialized)
					{
						const WindowSize size = window.Size();
						committedWidth.store(size.width, std::memory_order_release);
						committedHeight.store(size.height, std::memory_order_release);
						const TransparentPresentationCallbacks presentationCallbacks{
							&this->styleCallbacks, &ApplyStyle
						};
						TransparentPresentationOptions presentationOptions{};
						presentationOptions.requireMode =
							IsRequiredMode(startOptions.requiredPresentationMode);
						presentationOptions.requiredMode =
							ToTransparentMode(startOptions.requiredPresentationMode);
						presentationOptions.allowDirectComposition =
							startOptions.allowDirectComposition;
						graphicsInitialized = presentation.Initialize(windowHandle,
							presentationWindowHandle, graphics, renderer,
							static_cast<UINT>((std::max)(1, size.width)),
							static_cast<UINT>((std::max)(1, size.height)), presentationCallbacks,
							presentationOptions);
						if (graphicsInitialized)
						{
							presentationMode.store(ToHostMode(presentation.ActiveMode()),
								std::memory_order_release);
							window.SetGpuTransparentComposition(
								presentation.IsGpuTransparentComposition());
						}
					}

					{
						std::scoped_lock lock(startupMutex);
						graphicsReady = graphicsInitialized;
					}
					startupCondition.notify_all();
					if (graphicsInitialized)
					{
						// RTS 必须在图形资源准备好后才启用，避免输入 producer 先于 renderer 存活。
						bool stylusApproved = false;
						{
							std::unique_lock lock(startupMutex);
							startupCondition.wait(lock, [this, &token]
								{ return stylusDecision || token.stop_requested(); });
							stylusApproved = stylusDecision && stylusSucceeded && !token.stop_requested();
						}
						if (stylusApproved)
						{
							StrokeModelConfiguration configuration =
								CreateStrokeModelConfiguration(GetDpiForWindow(windowHandle));
							const DrawingControllerRuntimeObserver observer{
								this, &ObservePresented, &ObserveResized,
								&ObserveCommand, &ObserveDocument,
								&ObserveCurrentPageContent, &ObserveWorkspace, &ConsumeBridge
							};
							drawing = std::make_unique<DrawingController>(input, window, renderer,
								presentation, configuration, observer);
							// 首帧清屏和所有 Renderer 访问均发生在 Draw3 绘制线程。
							PumpBridgeState();
							presentation.SetOutputTarget(window.SelectionMode()
								? TransparentOutputTarget::SelectionUlw
								: TransparentOutputTarget::PrimaryDrawpad);
							drawing->ClearCanvas();
							initialized = lastPresentSucceeded.load(std::memory_order_acquire);
							firstFrameReady.store(initialized, std::memory_order_release);
						}
					}
				}
				catch (const std::exception& exception)
				{
					// 初始化失败也必须完成握手；保留异常原因供隐藏测试和现场诊断。
					std::fprintf(stderr, "[Draw3] startup failed: %s\n", exception.what());
					initialized = false;
				}
				catch (...)
				{
					std::fputs("[Draw3] startup failed: unknown exception\n", stderr);
					initialized = false;
				}
				{
					std::scoped_lock lock(startupMutex);
					startupSucceeded = initialized;
					startupCompleted = true;
				}
				startupCondition.notify_all();
				if (initialized && drawing && !token.stop_requested())
				{
					try
					{
						drawing->Run();
					}
					catch (const std::exception& exception)
					{
						// 绘制循环异常必须收敛到宿主停止，不能穿出 jthread 触发 terminate。
						std::fprintf(stderr, "[Draw3] drawing loop stopped: %s\n",
							exception.what());
						window.RequestExit();
					}
					catch (...)
					{
						std::fputs("[Draw3] drawing loop stopped: unknown exception\n", stderr);
						window.RequestExit();
					}
				}
				// GPU 资源在拥有它们的绘制线程释放，避免跨线程访问 Renderer。
				if (drawing) drawing.reset();
				presentation.Shutdown();
				renderer.ReleaseResources();
				graphics = {};
				running.store(false, std::memory_order_release);
				contentCondition.notify_all();
				runtimeCondition.notify_all();
			});

		std::unique_lock lock(startupMutex);
		startupCondition.wait(lock, [this] { return graphicsReady || startupCompleted; });
		if (!graphicsReady)
		{
			lock.unlock();
			if (drawingThread.joinable()) drawingThread.join();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			return false;
		}

		lock.unlock();
		bool stylusInitialized = false;
		try
		{
		#if defined(DRAW3_RTS_DIAGNOSTICS)
			// 窗口光标与唯一 RTS producer 共享同一显式诊断状态。
			window.SetRtsTraceEnabled(startOptions.enableRtsTrace);
			stylus.SetRtsTraceEnabled(startOptions.enableRtsTrace);
		#endif
			stylusInitialized = stylus.Initialize(hwnd, input, &window);
		}
		catch (...)
		{
			// 即使 RTS 构造意外抛出，也必须完成握手，不能让绘制线程永久等待。
			stylusInitialized = false;
		}
		{
			std::scoped_lock startupLock(startupMutex);
			stylusSucceeded = stylusInitialized;
			stylusDecision = true;
		}
		startupCondition.notify_all();

		lock.lock();
		startupCondition.wait(lock, [this] { return startupCompleted; });
		const bool startupSucceededValue = startupSucceeded;
		lock.unlock();
		if (!startupSucceededValue)
		{
			// 失败时先停止 RTS producer，再让绘制线程退出并释放 GPU 资源。
			stylus.Shutdown();
			window.RequestExit();
			input.PublishControlWake();
			if (drawingThread.joinable()) drawingThread.request_stop();
			startupCondition.notify_all();
			if (drawingThread.joinable()) drawingThread.join();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			return false;
		}
		return true;
		}

		void Stop() noexcept
		{
			if (!attachedWindow.load(std::memory_order_acquire) &&
				!attachedPresentationWindow.load(std::memory_order_acquire) &&
				!running.load(std::memory_order_acquire)) return;
			bridge.Stop();
			// 先停止 RTS producer，再唤醒绘制线程，确保不再产生新的 contact。
			stylus.Shutdown();
			window.RequestExit();
			input.PublishControlWake();
			if (drawingThread.joinable()) drawingThread.request_stop();
			startupCondition.notify_all();
			if (drawingThread.joinable()) drawingThread.join();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			running.store(false, std::memory_order_release);
			contentCondition.notify_all();
			runtimeCondition.notify_all();
		}
	};

	Host::Host() : impl_(std::make_unique<Impl>()) {}
	Host::~Host() { Stop(); }
	bool Host::Start(HWND drawpad, HWND drawpadPresentation,
		HostStyleCallbacks callbacks, HostStartOptions options)
	{
		return impl_->Start(drawpad, drawpadPresentation, callbacks, options);
	}
	void Host::Stop() noexcept { impl_->Stop(); }
	bool Host::Running() const noexcept { return impl_->running.load(std::memory_order_acquire); }
	bool Host::FirstFrameReady() const noexcept
	{
		return impl_->firstFrameReady.load(std::memory_order_acquire);
	}
	HostRuntimeSnapshot Host::RuntimeSnapshot() const noexcept
	{
		HostRuntimeSnapshot snapshot;
		snapshot.running = impl_->running.load(std::memory_order_acquire);
		snapshot.firstFrameReady = impl_->firstFrameReady.load(std::memory_order_acquire);
		snapshot.lastPresentSucceeded =
			impl_->lastPresentSucceeded.load(std::memory_order_acquire);
		snapshot.presentationMode = impl_->presentationMode.load(std::memory_order_acquire);
		snapshot.presentCount = impl_->presentCount.load(std::memory_order_acquire);
		snapshot.successfulPresentCount =
			impl_->successfulPresentCount.load(std::memory_order_acquire);
		snapshot.partialPresentCount =
			impl_->partialPresentCount.load(std::memory_order_acquire);
		snapshot.resizeCount = impl_->resizeCount.load(std::memory_order_acquire);
		snapshot.clearCommandCount =
			impl_->clearCommandCount.load(std::memory_order_acquire);
		snapshot.undoCommandCount = impl_->undoCommandCount.load(std::memory_order_acquire);
		snapshot.redoCommandCount = impl_->redoCommandCount.load(std::memory_order_acquire);
		snapshot.nextPageCommandCount =
			impl_->nextPageCommandCount.load(std::memory_order_acquire);
		snapshot.previousPageCommandCount =
			impl_->previousPageCommandCount.load(std::memory_order_acquire);
		const ContactInputDiagnosticsSnapshot inputDiagnostics =
			impl_->input.DiagnosticsSnapshot();
		snapshot.inputDownPublished = inputDiagnostics.downPublished;
		snapshot.inputMovePublished = inputDiagnostics.movePublished;
		snapshot.inputTerminalPublished = inputDiagnostics.terminalPublished;
		snapshot.inputRecycled = inputDiagnostics.recycled;
		snapshot.ulwDirtyRectPresentCount =
			impl_->ulwDirtyRectPresentCount.load(std::memory_order_acquire);
		snapshot.ulwPremultipliedAlphaFailureCount =
			impl_->ulwPremultipliedAlphaFailureCount.load(std::memory_order_acquire);
		snapshot.ulwTransparentFullFrameVerified =
			impl_->ulwTransparentFullFrameVerified.load(std::memory_order_acquire);
		snapshot.committedWidth = impl_->committedWidth.load(std::memory_order_acquire);
		snapshot.committedHeight = impl_->committedHeight.load(std::memory_order_acquire);
		snapshot.currentPageIndex = impl_->currentPageIndex.load(std::memory_order_acquire);
		snapshot.pageCount = impl_->pageCount.load(std::memory_order_acquire);
		snapshot.contentRevision = impl_->contentRevision.load(std::memory_order_acquire);
		// revision 的 release 发布发生在内容布尔值之后；先 acquire revision，
		// 再读取布尔值，避免把新 revision 与旧内容拼成不可重试的快照。
		snapshot.currentPageHasContent =
			impl_->currentPageHasContent.load(std::memory_order_acquire);
		snapshot.selectionMode = impl_->selectionMode.load(std::memory_order_acquire);
		snapshot.workspace = impl_->workspace.load(std::memory_order_acquire);
		snapshot.requestedOutputTarget =
			impl_->requestedOutputTarget.load(std::memory_order_acquire);
		snapshot.requestedOutputRevision =
			impl_->requestedOutputRevision.load(std::memory_order_acquire);
		snapshot.readyOutputTarget =
			impl_->readyOutputTarget.load(std::memory_order_acquire);
		snapshot.readyOutputRevision =
			impl_->readyOutputRevision.load(std::memory_order_acquire);
		snapshot.presentedContentRevision =
			impl_->presentedContentRevision.load(std::memory_order_acquire);
		snapshot.auxiliaryFullFrameClean =
			impl_->auxiliaryFullFrameClean.load(std::memory_order_acquire);
		snapshot.runtimeRevision = impl_->runtimeRevision.load(std::memory_order_acquire);
		snapshot.lastDirtyRect.left = impl_->lastDirtyLeft.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.top = impl_->lastDirtyTop.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.right = impl_->lastDirtyRight.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.bottom = impl_->lastDirtyBottom.load(std::memory_order_relaxed);
		return snapshot;
	}

	bool Host::WaitForRuntimeRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) const noexcept
	{
		std::unique_lock lock(impl_->runtimeMutex);
		return impl_->runtimeCondition.wait_for(lock,
			std::chrono::milliseconds(timeoutMilliseconds), [this, revision]
			{
				return impl_->runtimeRevision.load(std::memory_order_acquire) != revision ||
					!impl_->running.load(std::memory_order_acquire);
			});
	}

	bool Host::WaitForContentRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) const noexcept
	{
		std::unique_lock lock(impl_->contentMutex);
		return impl_->contentCondition.wait_for(lock,
			std::chrono::milliseconds(timeoutMilliseconds), [this, revision]
			{
				return impl_->contentRevision.load(std::memory_order_acquire) != revision ||
					!impl_->running.load(std::memory_order_acquire);
			});
	}

	bool Host::PublishHiddenTestContact(WPARAM phaseValue, LPARAM position) noexcept
	{
		if (!impl_->hiddenTestContactInjectionEnabled) return false;
		const auto phase = static_cast<HiddenTestContactPhase>(
			static_cast<std::uint32_t>(phaseValue));
		ContactSnapshot snapshot{};
		snapshot.position.x = static_cast<float>(static_cast<short>(LOWORD(position)));
		snapshot.position.y = static_cast<float>(static_cast<short>(HIWORD(position)));
		snapshot.pressure = phase == HiddenTestContactPhase::Down ? 0.8f : 0.7f;
		LARGE_INTEGER qpc = {};
		QueryPerformanceCounter(&qpc);
		snapshot.qpc = qpc.QuadPart;

		// 隐藏测试走同一个无锁 contact mailbox，不触碰 Renderer 或 RTS 内部状态。
		constexpr std::uint32_t tabletContextId = 0xD303u;
		constexpr std::uint32_t contactId = 0xD304u;
		bool published = false;
		switch (phase)
		{
		case HiddenTestContactPhase::Down:
			snapshot.phase = ContactPhase::Down;
			published = impl_->input.PublishDown(tabletContextId, contactId,
				InputDeviceType::Pen, snapshot);
			break;
		case HiddenTestContactPhase::Move:
			snapshot.phase = ContactPhase::Move;
			published = impl_->input.PublishMove(tabletContextId, contactId, snapshot);
			break;
		case HiddenTestContactPhase::Up:
			snapshot.phase = ContactPhase::Up;
			published = impl_->input.PublishUp(tabletContextId, contactId, snapshot);
			break;
		case HiddenTestContactPhase::Cancelled:
			snapshot.phase = ContactPhase::Cancelled;
			published = impl_->input.PublishCancelled(tabletContextId, contactId, snapshot);
			break;
		default:
			return false;
		}
		if (published) (void)impl_->input.PublishControlWake();
		return published;
	}

	LRESULT Host::ForwardMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == kDraw3HiddenTestContactMessage &&
			impl_->hiddenTestContactInjectionEnabled)
			return PublishHiddenTestContact(wParam, lParam) ? 0 : -1;
		return impl_->window.HandleExternalMessage(window, message, wParam, lParam);
	}
	void Host::SetActivationAllowed(bool enabled) noexcept
	{
		impl_->window.SetActivationAllowed(enabled);
	}
	Bridge::StateBridge& Host::ProductBridge() noexcept { return impl_->bridge; }
	void Host::PublishState(const Bridge::ProductState& state) noexcept
	{
		impl_->bridge.PublishState(state);
		if (impl_->attachedWindow.load(std::memory_order_acquire))
			(void)impl_->input.PublishControlWake();
	}
	Bridge::CommandResult Host::PublishCommand(Bridge::CommandType command) noexcept
	{
		const Bridge::CommandResult result = impl_->bridge.Publish(command);
		if (result == Bridge::CommandResult::Accepted)
			(void)impl_->input.PublishControlWake();
		return result;
	}
}
