#include "Draw3.DiagnosticOutput.h"
#include "Draw3.Host.h"
#include "Draw3.PresentationState.h"
#include "Draw3.HapticDiagnostics.h"
#include "Draw3.Product.h"
#include <dxgi.h>
#include "Draw3.PptTiming.h"
#include "Draw3.SpeedEraser.h"

import Inkeys.Display;
import Inkeys.UI.PageControl;

import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Drawing.Draw3.pen_cursor;
import Inkeys.Drawing.Draw3.auto_save;
import Inkeys.Drawing.Draw3.drawing_controller;
import Inkeys.Drawing.Draw3.graphics_initialization;
import Inkeys.Drawing.Draw3.haptic_feedback;
import Inkeys.Drawing.Draw3.ink_prediction;
import Inkeys.Drawing.Draw3.presentation_auto_save;
import Inkeys.Drawing.Draw3.renderer;
import Inkeys.Drawing.Draw3.realtime_stylus;
import Inkeys.Drawing.Draw3.runtime_metrics;
import Inkeys.Drawing.Draw3.transparent_presentation;
import Inkeys.Drawing.Draw3.window_control;

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
#include <filesystem>
#endif
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		std::uint64_t DrainPresentationCompletions(PresentationAutoSaveService& service,
			std::shared_ptr<const PresentationPersistenceCompletion>& pending,
			WindowController& window) noexcept
		{
			std::uint64_t notFound = 0;
			while (pending || service.TryTakeCompletion(pending))
			{
				if (!window.TryEnqueuePresentationCompletion(pending)) break;
				if (pending->operation == PresentationPersistenceOperation::Load &&
					pending->status == PresentationPersistenceStatus::NotFound) ++notFound;
				pending.reset();
			}
			return notFound;
		}

		UINT QueryCompatibleWindowDpi(HWND hwnd) noexcept
		{
			if (!hwnd) return 96;
			using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
			const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			const auto getDpiForWindow = user32
				? reinterpret_cast<GetDpiForWindowFunction>(
					GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
			if (getDpiForWindow)
			{
				const UINT dpi = getDpiForWindow(hwnd);
				if (dpi != 0) return dpi;
			}
			// Win7 无 GetDpiForWindow：沿用显示快照，再退到系统 GDI DPI。
			const auto display = Inkeys::Display::GetSnapshot();
			const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
			if (display)
				if (const auto* info = display->Find(monitor))
					if (info->effectiveDpiX != 0) return info->effectiveDpiX;
			const HDC dc = GetDC(hwnd);
			if (dc)
			{
				const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
				ReleaseDC(hwnd, dc);
				if (dpi > 0) return static_cast<UINT>(dpi);
			}
			return 96;
		}

		const char* DisplayTopologyName(Inkeys::Display::DisplayTopology value) noexcept
		{
			switch(value)
			{
			case Inkeys::Display::DisplayTopology::Single:return "Single";
			case Inkeys::Display::DisplayTopology::Extended:return "Extended";
			case Inkeys::Display::DisplayTopology::CloneOrMixed:return "CloneOrMixed";
			default:return "Unknown";
			}
		}
		const char* EdidStatusName(Inkeys::Display::EdidStatus value) noexcept
		{
			switch(value)
			{
			case Inkeys::Display::EdidStatus::Parsed:return "Parsed";
			case Inkeys::Display::EdidStatus::ReadFailed:return "ReadFailed";
			case Inkeys::Display::EdidStatus::ParseFailed:return "ParseFailed";
			default:return "Unavailable";
			}
		}

		const char* HostOutputTargetName(HostOutputTarget value) noexcept
		{
			return value == HostOutputTarget::SelectionUlw
				? "DrawpadPresentation" : "Drawpad";
		}

		const char* Draw3BuildArchitectureName() noexcept
		{
#if defined(_M_ARM64)
			return "ARM64";
#elif defined(_M_X64)
			return "x64";
#elif defined(_M_IX86)
			return "Win32";
#else
			return "unknown";
#endif
		}

		void LogDraw3BuildIdentity()
		{
			// 工程的 Debug/Release 配置共用 NDEBUG 定义，配置名由交付构建命令记录；
			// 这里只输出不会被配置宏误判的编译身份。
			DiagnosticOutput::Line() << "[Draw3Diag][build] id=draw3-" << __DATE__ << "T" << __TIME__
				<< " arch=" << Draw3BuildArchitectureName()
				<< " msvc=" << _MSC_VER << std::endl;
		}

		void LogDraw3WindowState(const char* eventName, HWND primary, HWND selection)
		{
			if (!eventName) return;
			const auto logWindow = [eventName](const char* role, HWND window)
			{
				const BOOL valid = window && IsWindow(window);
				const BOOL visible = valid && IsWindowVisible(window);
				RECT windowRect = {};
				RECT clientRect = {};
				const BOOL windowRectOk = valid && GetWindowRect(window, &windowRect);
				const BOOL clientRectOk = valid && GetClientRect(window, &clientRect);
				DWORD processId = 0;
				const DWORD threadId = valid ? GetWindowThreadProcessId(window, &processId) : 0;
				DiagnosticOutput::Line() << "[Draw3Diag][window] event=" << eventName << " role=" << role
					<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window)
					<< " valid=" << std::dec << (valid ? 1 : 0)
					<< " visible=" << (visible ? 1 : 0)
					<< " pid=" << processId << " tid=" << threadId
					<< " owner=0x" << std::hex << reinterpret_cast<UINT_PTR>(valid ? GetWindow(window, GW_OWNER) : nullptr)
					<< " previous=0x" << reinterpret_cast<UINT_PTR>(valid ? GetWindow(window, GW_HWNDPREV) : nullptr)
					<< " style=0x" << std::hex
					<< static_cast<unsigned long>(valid ? GetWindowLongPtrW(window, GWL_STYLE) : 0)
					<< " exStyle=0x" << static_cast<unsigned long>(valid ? GetWindowLongPtrW(window, GWL_EXSTYLE) : 0)
					<< std::dec << " windowRectOk=" << (windowRectOk ? 1 : 0)
					<< " clientRectOk=" << (clientRectOk ? 1 : 0)
					<< " window=(" << windowRect.left << "," << windowRect.top << ","
					<< windowRect.right << "," << windowRect.bottom << ")"
					<< " client=(" << clientRect.left << "," << clientRect.top << ","
					<< clientRect.right << "," << clientRect.bottom << ")" << std::endl;
			};
			logWindow("Drawpad", primary);
			logWindow("DrawpadPresentation", selection);
		}
		const char* PhysicalReasonName(Inkeys::Display::PhysicalSizeUnavailableReason value) noexcept
		{
			switch(value)
			{
			case Inkeys::Display::PhysicalSizeUnavailableReason::None:return "None";
			case Inkeys::Display::PhysicalSizeUnavailableReason::SnapshotFallback:return "SnapshotFallback";
			case Inkeys::Display::PhysicalSizeUnavailableReason::TopologyUnknown:return "TopologyUnknown";
			case Inkeys::Display::PhysicalSizeUnavailableReason::CloneOrMixed:return "CloneOrMixed";
			case Inkeys::Display::PhysicalSizeUnavailableReason::DisplayTargetAmbiguous:return "DisplayTargetAmbiguous";
			case Inkeys::Display::PhysicalSizeUnavailableReason::EdidUnavailable:return "EdidUnavailable";
			case Inkeys::Display::PhysicalSizeUnavailableReason::EdidReadFailed:return "EdidReadFailed";
			case Inkeys::Display::PhysicalSizeUnavailableReason::EdidParseFailed:return "EdidParseFailed";
			case Inkeys::Display::PhysicalSizeUnavailableReason::MissingDimensions:return "MissingDimensions";
			default:return "DimensionsBelowMinimum";
			}
		}
		const char* InputDeviceTypeName(uint32_t value) noexcept
		{
			switch(static_cast<InputDeviceType>(value))
			{
			case InputDeviceType::Touch:return "Touch";
			case InputDeviceType::Pen:return "Pen";
			case InputDeviceType::MouseLeft:return "MouseLeft";
			case InputDeviceType::MouseRight:return "MouseRight";
			default:return "Unknown";
			}
		}
	}

#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	int RunPresentationCompletionDeliveryTest() noexcept
	{
		int failures = 0;
		const auto check = [&](bool result, const char* message)
		{
			if (!result)
			{
				++failures;
				std::fprintf(stderr, "[Draw3PptCompletion] FAIL: %s\n", message);
			}
		};
		try
		{
			WindowController window;
			ContactInputCoordinator input;
			window.SetInputCoordinator(&input);
			const auto clearWakes = [&]
			{
				ContactRecord* record = nullptr;
				while (input.TryDequeue(record))
					if (!record) input.AcknowledgeControlWake();
			};
			Bridge::PresentationTarget target;
			for (std::size_t i = 0; i < target.key.bytes.size(); ++i)
				target.key.bytes[i] = static_cast<std::uint8_t>(i + 1);
			target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
			target.sourceIdentity = "path:c:\\draw3-completion-test\\deck.pptx";
			target.presentationName = "deck.pptx";
			target.provider = "PowerPoint";
			target.bindingToken = "PowerPoint:12:34:1";
			target.slideIds = { 101 };
			target.slideId = 101;
			target.totalPages = 1;
			target.bindingRevision = 1;
			target.targetRevision = 1;
			auto first = std::make_shared<PresentationPersistenceCompletion>();
			first->target = target;
			first->slotGeneration = 1;
			auto second = std::make_shared<PresentationPersistenceCompletion>(*first);
			second->slotGeneration = 2;
			CanvasCommand clear;
			clear.type = CanvasCommandType::Clear;
			CanvasCommand undo;
			undo.type = CanvasCommandType::Undo;
			CanvasCommand scene;
			scene.type = CanvasCommandType::SetPresentationTarget;
			scene.presentationTarget = std::make_shared<Bridge::PresentationTarget>(target);
			window.EnqueueCanvasCommand(clear);
			window.EnqueueCanvasCommand(scene);
			check(window.TryEnqueuePresentationCompletion(first), "first terminal enters reserved slot");
			check(!window.TryEnqueuePresentationCompletion(second), "occupied slot cannot overwrite terminal");
			for (std::uint64_t revision : { 2, 3 })
			{
				target.targetRevision = revision;
				scene.presentationTarget = std::make_shared<Bridge::PresentationTarget>(target);
				window.EnqueueCanvasCommand(scene);
			}
			window.EnqueueCanvasCommand(undo);
			CanvasCommand command;
			check(window.TryDequeueCanvasCommand(command) && command.type == clear.type,
				"ordinary command before terminal keeps FIFO");
			check(window.TryDequeueCanvasCommand(command) && command.type == scene.type &&
				command.presentationTarget->targetRevision == 1, "target coalescing cannot cross terminal barrier");
			clearWakes();
			const auto beforeWake = input.CaptureWakeGeneration();
			check(window.TryDequeueCanvasCommand(command) && command.presentationPersistenceCompletion == first,
				"reserved terminal preserves payload identity");
			check(input.CaptureWakeGeneration() != beforeWake, "terminal consumption wakes next retained delivery");
			check(window.TryDequeueCanvasCommand(command) && command.type == scene.type &&
				command.presentationTarget->targetRevision == 3, "adjacent targets after barrier still coalesce");
			check(window.TryDequeueCanvasCommand(command) && command.type == undo.type &&
				!window.HasPendingCanvasCommand(), "ordinary command after terminal keeps FIFO");
			check(window.TryEnqueuePresentationCompletion(second) && window.HasPendingCanvasCommand(),
				"terminal alone keeps command boundary pending");
			window.DetachExternal();
			check(!window.HasPendingCanvasCommand() && !window.TryDequeueCanvasCommand(command),
				"stop clears terminal slot and barrier count");
			window.SetInputCoordinator(&input);

			// 使用真实 service 和 Host 共用 drain；不初始化 HWND、GPU 或 RTS。
			wchar_t temporary[MAX_PATH]{};
			const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporary);
			check(temporaryLength != 0 && temporaryLength < MAX_PATH, "private fixture root available");
			if (temporaryLength == 0 || temporaryLength >= MAX_PATH) return 1;
			const auto root = std::filesystem::path(temporary) /
				(L"InkeysPptCompletion-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
					std::to_wstring(GetTickCount64()));
			PresentationAutoSaveService service;
			check(service.Start(root.wstring()), "real completion service starts");
			target.targetRevision = 4;
			for (std::uint64_t generation : { 11, 12, 13 })
			{
				PresentationLoadRequest request{ target };
				request.slotGeneration = generation;
				check(service.SubmitLoad(std::move(request)) == PresentationPersistenceSubmitStatus::Accepted,
					"real service accepts each identity-stamped request");
			}
			service.CloseAndDrain();
			std::shared_ptr<const PresentationPersistenceCompletion> pending;
			window.EnqueueCanvasCommand(clear);
			check(DrainPresentationCompletions(service, pending, window) == 1 && pending &&
				pending->slotGeneration == 12, "full slot retains second terminal in Host");
			window.EnqueueCanvasCommand(undo);
			check(window.TryDequeueCanvasCommand(command) && command.type == clear.type,
				"real delivery follows earlier ordinary command");
			check(window.TryDequeueCanvasCommand(command) && command.presentationPersistenceCompletion &&
				command.presentationPersistenceCompletion->slotGeneration == 11, "first real terminal delivered once");
			check(DrainPresentationCompletions(service, pending, window) == 1 && pending &&
				pending->slotGeneration == 13, "third terminal retained while second waits");
			check(window.TryDequeueCanvasCommand(command) && command.type == undo.type,
				"later terminal cannot overtake already queued ordinary command");
			check(window.TryDequeueCanvasCommand(command) && command.presentationPersistenceCompletion &&
				command.presentationPersistenceCompletion->slotGeneration == 12, "second terminal not starved");
			check(DrainPresentationCompletions(service, pending, window) == 1 && !pending,
				"last terminal drains without a new persistence event or user input");
			check(window.TryDequeueCanvasCommand(command) && command.presentationPersistenceCompletion &&
				command.presentationPersistenceCompletion->slotGeneration == 13 && !window.HasPendingCanvasCommand(),
				"all accepted terminals delivered in order");
			check(window.TryEnqueuePresentationCompletion(first), "fixture leaves old terminal pending");
			pending = second;
			window.DetachExternal();
			pending.reset(); // 与 Host Stop 的 owner-join 后清理一致。
			check(service.Start(root.wstring()), "service restarts after drain");
			target.targetRevision = 5;
			check(service.SubmitLoad({ target }) == PresentationPersistenceSubmitStatus::Accepted,
				"new generation load accepted");
			service.CloseAndDrain();
			check(DrainPresentationCompletions(service, pending, window) == 1 &&
				window.TryDequeueCanvasCommand(command) && command.presentationPersistenceCompletion &&
				command.presentationPersistenceCompletion->target.targetRevision == 5 &&
				!window.HasPendingCanvasCommand() && !pending, "restart cannot deliver stale terminal");
			window.SetInputCoordinator(nullptr);
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}
		catch (...)
		{
			check(false, "completion delivery fixture completes without exception");
		}
		if (failures == 0)
			std::fputs("[Draw3PptCompletion] PASS: reserved FIFO terminal, real Host drain, wake and restart\n", stderr);
		return failures == 0 ? 0 : 1;
	}

#endif

	struct Host::Impl
	{
		struct RuntimeMetricsState
		{
			// 会话在 drawing 之前声明，析构顺序也保证 Controller 的借用先结束。
			std::unique_ptr<RuntimeMetricsSession> session;
			std::atomic_bool enabled = false, unavailable = false, joined = false, sealed = false;
			std::atomic_bool startupFailed = false, runFailed = false;
			mutable std::atomic_bool exportUnavailable = false;
			std::atomic_uint64_t runSerial = 0;
			std::atomic_uint64_t contactSeen = 0, confirmed = 0, pending = 0;
			std::atomic_uint64_t invalid = 0, contactDropped = 0, pendingOverflow = 0;
			std::atomic_uint64_t frameSerial = 0, presentAttempts = 0;
			std::atomic_uint64_t allocatedBytes = 0, payloadByteUpperBound = 0, effectiveSamples = 0;
			std::uint64_t nextRunSerial = 0;
			bool ownerCreated = false;
			mutable std::mutex metadataMutex;
			HostRuntimeMetricsMetadata metadata;
			ContactInputDiagnosticsSnapshot inputBaseline, finalInput;
			RuntimeMetricsSnapshot finalSnapshot;
		};
		static_assert(sizeof(RuntimeMetricsState) <= 4096);
		RuntimeMetricsState runtimeMetrics;
		Bridge::StateBridge bridge;
		DesktopAutoSaveService autoSave;
		PresentationAutoSaveService presentationAutoSave;
		// 只由绘制 owner 访问；槽占用时保留终态，下一次 General wake 再交付。
		std::shared_ptr<const PresentationPersistenceCompletion> pendingPresentationCompletion;
		ContactInputCoordinator input;
		WindowController window;
		Haptics::ClickMailbox hapticClicks;
		std::mutex commandPublishMutex;
		mutable std::mutex eraserDiagnosticsMutex;
		SpeedEraser::Diagnostics eraserDiagnostics;
		PenRuntimeDiagnostics penDiagnostics;
		mutable std::mutex displayMutex;
		SpeedEraser::DevelopmentOptions eraserDevelopment;
		SpeedEraser::ContactAreaSample hiddenContactArea;
		std::atomic_bool touchAreaTraceEnabled = false;
		std::atomic_uint64_t touchAreaTraceRevision = 0;
		uint64_t observedTouchAreaTraceRevision = 0;
		std::chrono::steady_clock::time_point lastTouchAreaMetadataTrace{};
		SpeedEraser::InputSource lastTouchAreaMetadataSource;
		bool touchAreaMetadataPending = true;
		std::chrono::steady_clock::time_point lastTouchAreaTrace{};
		bool touchAreaTraceWasEnabled = false, lastTracedContact = false, lastTracedInputActivity = false;
		Inkeys::Display::SnapshotPtr pendingDisplaySnapshot;
		Inkeys::Display::Subscription displaySubscription;
		std::atomic_bool displayScaleDirty = false;
		RECT appliedDisplayClientBounds{};
		Inkeys::Business::PenWidth::Context appliedPenWidthContext;
		std::uintptr_t appliedPenWidthMonitor = 0;
		bool penWidthContextPrepared = false;
		GraphicsDeviceResources graphics;
		InkRenderer renderer;
		TransparentPresentationController presentation;
		RealTimeStylusInput stylus;
		std::unique_ptr<DrawingController> drawing;
		std::atomic_bool interruptedStrokeReconnectEnabled = true;
		std::atomic_bool drawingHapticsEnabled = true;
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
		bool presentDiagnosticInitialized = false;
		HostOutputTarget lastDiagnosticOutputTarget = HostOutputTarget::PrimaryDrawpad;
		std::uint64_t lastDiagnosticOutputRevision = 0;
		bool lastDiagnosticPresentSucceeded = false;
		std::atomic<int> committedWidth = 0;
		std::atomic<int> committedHeight = 0;
		std::atomic<std::size_t> currentPageIndex = 0;
		std::atomic<std::size_t> pageCount = 0;
		std::atomic_bool currentPageHasContent = false;
		mutable std::mutex whiteboardMutex;
		WhiteboardTheme whiteboardTheme;
		std::uint64_t whiteboardGeneration = 0;
		std::atomic_bool whiteboardLoadPending = false;
		bool whiteboardUnsaved = false;
		std::size_t whiteboardPageIndex = 0, whiteboardPageCount = 1;
		bool whiteboardCanUndo = false, whiteboardCanRedo = false;
		bool whiteboardHasContent = false;
		std::uint64_t whiteboardContentRevision = 0;
		std::atomic_bool canUndo = false;
		std::atomic_bool canRedo = false;
		std::atomic<std::uint64_t> contentRevision = 0;
		std::atomic<Bridge::CompletedStrokeKind> completedStrokeKind =
			Bridge::CompletedStrokeKind::None;
		std::atomic_bool selectionMode = true;
		std::atomic<Bridge::Workspace> workspace = Bridge::Workspace::Desktop;
		mutable std::mutex presentationTargetMutex;
		std::optional<Bridge::PresentationReadyIdentity> readyPresentationTarget;
		std::optional<Bridge::PresentationReadyIdentity> uiReadyPresentationTarget;
		std::optional<Bridge::PresentationReadyIdentity> suspendedPresentationTarget;
		bool commandScenePending = false;
		std::uint64_t workspaceTransitionEpoch = 0;
		bool workspaceTransitionPending = false;
		std::uint64_t appliedProductRevision = 0;
		Bridge::Tool appliedTool = Bridge::Tool::Pen;
		std::uint32_t inputGateReasons = 0;
		CanvasRecoveryState recovery;
		CanvasRecoveryState reportedRecovery;
		bool recoveryReportAvailable = false;
		std::uint64_t recoveryRevision = 0;
		bool shortcutSelectionHandoffPending = false;
		bool restoreLatestScene = false;
		std::atomic<HostOutputTarget> requestedOutputTarget =
			HostOutputTarget::PrimaryDrawpad;
		std::atomic<std::uint64_t> requestedOutputRevision = 0;
		std::atomic<HostOutputTarget> readyOutputTarget =
			HostOutputTarget::PrimaryDrawpad;
		std::atomic<std::uint64_t> readyOutputRevision = 0;
		std::atomic<std::uint64_t> presentedContentRevision = 0;
		std::atomic_bool auxiliaryFullFrameClean = false;
		Detail::HostRuntimeRevisionSignal runtimeRevision;
		Detail::HostDrawingActivityState drawingActivity;
		Detail::HostAcceptedStrokeState acceptedStrokes;
		Shortcuts::State shortcuts;
		std::atomic<LONG> lastDirtyLeft = 0;
		std::atomic<LONG> lastDirtyTop = 0;
		std::atomic<LONG> lastDirtyRight = 0;
		std::atomic<LONG> lastDirtyBottom = 0;
		bool hiddenTestContactInjectionEnabled = false;
		std::uint64_t appliedBridgeRevision = (std::numeric_limits<std::uint64_t>::max)();
		bool requestedProductPage = false;
		std::uint32_t requestedProductPageIndex = 0;
		// 绘制 owner 记录最后实际入队的场景，不能用尚未前进的 presented workspace 代替。
		Bridge::Workspace requestedWorkspace = Bridge::Workspace::Desktop;
		std::uint64_t requestedPresentationTargetRevision = 0;
		HostStyleCallbacks styleCallbacks = {};
		HostRuntimeCallbacks runtimeCallbacks = {};
		HostStartOptions startOptions = {};
		std::atomic_uint64_t hiddenPresentationNotFound = 0;
		std::mutex startupMutex;
		std::condition_variable startupCondition;
		mutable std::mutex contentMutex;
		mutable std::condition_variable contentCondition;
		std::mutex exitAutoSaveMutex;
		std::condition_variable exitAutoSaveCondition;
		bool exitAutoSavePrepared = false;
		std::atomic_bool whiteboardExitDurable = true;
		bool graphicsReady = false;
		std::atomic<HostInputMode> inputMode = HostInputMode::Disabled;
		std::atomic_bool inputStopping = false;
		bool inputDecision = false;
		bool inputSucceeded = false;
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

		void BeginRuntimeMetricsRun(const HostStartOptions& options) noexcept
		{
			// 串行 Start 已确认上一线程不可 join；每 run 新建会话，不 Reset 或复用旧键。
			runtimeMetrics.session.reset();
			runtimeMetrics.enabled.store(options.enableRuntimeMetrics, std::memory_order_release);
			runtimeMetrics.unavailable.store(false, std::memory_order_release);
			runtimeMetrics.joined.store(false, std::memory_order_release);
			runtimeMetrics.sealed.store(false, std::memory_order_release);
			runtimeMetrics.startupFailed.store(options.enableRuntimeMetrics, std::memory_order_release);
			runtimeMetrics.runFailed.store(false, std::memory_order_release);
			runtimeMetrics.exportUnavailable.store(false, std::memory_order_release);
			runtimeMetrics.runSerial.store(0, std::memory_order_release);
			runtimeMetrics.contactSeen.store(0, std::memory_order_release);
			runtimeMetrics.confirmed.store(0, std::memory_order_release);
			runtimeMetrics.pending.store(0, std::memory_order_release);
			runtimeMetrics.invalid.store(0, std::memory_order_release);
			runtimeMetrics.contactDropped.store(0, std::memory_order_release);
			runtimeMetrics.pendingOverflow.store(0, std::memory_order_release);
			runtimeMetrics.frameSerial.store(0, std::memory_order_release);
			runtimeMetrics.presentAttempts.store(0, std::memory_order_release);
			runtimeMetrics.allocatedBytes.store(0, std::memory_order_release);
			runtimeMetrics.payloadByteUpperBound.store(0, std::memory_order_release);
			runtimeMetrics.effectiveSamples.store(0, std::memory_order_release);
			runtimeMetrics.inputBaseline = {};
			runtimeMetrics.finalInput = {};
			runtimeMetrics.finalSnapshot = {};
			runtimeMetrics.ownerCreated = false;
			{ std::scoped_lock lock(runtimeMetrics.metadataMutex); runtimeMetrics.metadata = {}; }
			if (!options.enableRuntimeMetrics) return;
			if (runtimeMetrics.nextRunSerial == (std::numeric_limits<std::uint64_t>::max)() ||
				options.runtimeMetricsMaximumSamples == 0 || options.runtimeMetricsMaximumSamples > 32768)
			{
				runtimeMetrics.unavailable.store(true, std::memory_order_release);
				return;
			}
			runtimeMetrics.runSerial.store(++runtimeMetrics.nextRunSerial, std::memory_order_release);
		}

		void PrepareRuntimeMetrics(const HostStartOptions& options) noexcept
		{
			if (!runtimeMetrics.enabled.load(std::memory_order_acquire) ||
				runtimeMetrics.unavailable.load(std::memory_order_acquire)) return;
			try
			{
				runtimeMetrics.session = std::make_unique<RuntimeMetricsSession>(options.runtimeMetricsMaximumSamples);
				// Controller Prepare 仍执行共同预算；此处另预留其静态上限和 Host 固定载荷。
				constexpr std::uint64_t auxiliaryBytes = 64u * 1024 + sizeof(RuntimeMetricsState) + sizeof(RuntimeMetricsSession);
				constexpr std::uint64_t byteBudget = 32u * 1024 * 1024;
				const auto snapshot = runtimeMetrics.session->Snapshot();
				LARGE_INTEGER frequency{};
				if (snapshot.allocatedBytes > byteBudget - auxiliaryBytes ||
					!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
				{
					runtimeMetrics.session.reset();
					runtimeMetrics.unavailable.store(true, std::memory_order_release);
					return;
				}
				runtimeMetrics.payloadByteUpperBound.store(snapshot.allocatedBytes + auxiliaryBytes, std::memory_order_release);
				{ std::scoped_lock lock(runtimeMetrics.metadataMutex); runtimeMetrics.metadata.qpcFrequency = frequency.QuadPart; }
				PublishRuntimeMetricsProgress();
			}
			catch (...)
			{
				runtimeMetrics.session.reset();
				runtimeMetrics.unavailable.store(true, std::memory_order_release);
			}
		}

		void PublishRuntimeMetricsProgress() noexcept
		{
			if (!runtimeMetrics.session) return;
			// 绘制 callback 读取 plain Session，再发布原子进度；caller 不跨线程读 Session。
			const auto value = runtimeMetrics.session->Snapshot();
			runtimeMetrics.contactSeen.store(value.contactSeen, std::memory_order_release);
			runtimeMetrics.confirmed.store(value.confirmed, std::memory_order_release);
			runtimeMetrics.pending.store(value.pending, std::memory_order_release);
			runtimeMetrics.invalid.store(value.invalid, std::memory_order_release);
			runtimeMetrics.contactDropped.store(value.contactDropped, std::memory_order_release);
			runtimeMetrics.pendingOverflow.store(value.pendingOverflow, std::memory_order_release);
			runtimeMetrics.frameSerial.store(value.frameSerial, std::memory_order_release);
			runtimeMetrics.presentAttempts.store(value.presentAttempts, std::memory_order_release);
			runtimeMetrics.allocatedBytes.store(value.allocatedBytes, std::memory_order_release);
			runtimeMetrics.effectiveSamples.store(value.effectiveSamples, std::memory_order_release);
		}

		void CaptureRuntimeMetricsMetadata(bool copyAdapter) noexcept
		{
			if (!runtimeMetrics.session) return;
			// 只有 GPU owner 调用；正常帧只抄数值，不查询 adapter 或增加时钟采样。
			std::scoped_lock lock(runtimeMetrics.metadataMutex);
			auto& value = runtimeMetrics.metadata;
			value.graphicsAvailable = graphics.device && graphics.context;
			value.driverType = value.graphicsAvailable ? static_cast<std::uint32_t>(graphics.driverType) : 0;
			value.featureLevel = value.graphicsAvailable ? static_cast<std::uint32_t>(graphics.featureLevel) : 0;
			value.presentationMode = ToHostMode(presentation.ActiveMode());
			value.outputTarget = ToHostOutputTarget(presentation.RequestedOutputTarget());
			value.outputRevision = presentation.RequestedOutputRevision();
			if (copyAdapter)
			{
				DXGI_ADAPTER_DESC description{};
				value.adapterAvailable = graphics.adapter && SUCCEEDED(graphics.adapter->GetDesc(&description));
				value.adapterDescription = {};
				if (value.adapterAvailable)
					std::copy_n(description.Description, value.adapterDescription.size(), value.adapterDescription.begin());
				value.adapterDescription.back() = L'\0';
			}
		}

		void SealRuntimeMetricsRun(bool startupFailed = false) noexcept
		{
			if (!runtimeMetrics.enabled.load(std::memory_order_acquire) ||
				runtimeMetrics.sealed.load(std::memory_order_acquire) || drawingThread.joinable() || drawing) return;
			if (startupFailed) runtimeMetrics.startupFailed.store(true, std::memory_order_release);
			runtimeMetrics.joined.store(runtimeMetrics.ownerCreated, std::memory_order_release);
			if (runtimeMetrics.session)
			{
				// producer 已停止、Controller 已销毁且线程真 join 后，尾 pending 才终结。
				runtimeMetrics.session->InvalidatePending();
				runtimeMetrics.finalSnapshot = runtimeMetrics.session->Snapshot();
				runtimeMetrics.finalInput = input.DiagnosticsSnapshot();
				const auto subtract = [](std::uint64_t value, std::uint64_t baseline) noexcept
				{ return value >= baseline ? value - baseline : 0; };
				auto& final = runtimeMetrics.finalInput;
				const auto& base = runtimeMetrics.inputBaseline;
				final.downPublished = subtract(final.downPublished, base.downPublished);
				final.downRejected = subtract(final.downRejected, base.downRejected);
				final.movePublished = subtract(final.movePublished, base.movePublished);
				final.moveContended = subtract(final.moveContended, base.moveContended);
				final.terminalPublished = subtract(final.terminalPublished, base.terminalPublished);
				final.recycled = subtract(final.recycled, base.recycled);
				final.controlWakes = subtract(final.controlWakes, base.controlWakes);
				final.controlWakeEnqueueFailures = subtract(final.controlWakeEnqueueFailures, base.controlWakeEnqueueFailures);
				final.controlWakeInlineRecoveries = subtract(final.controlWakeInlineRecoveries, base.controlWakeInlineRecoveries);
				final.activeWaits = subtract(final.activeWaits, base.activeWaits);
				PublishRuntimeMetricsProgress();
			}
			runtimeMetrics.sealed.store(true, std::memory_order_release);
		}

		bool WriteRuntimeMetrics(const wchar_t* path) const noexcept
		{
			if (!runtimeMetrics.enabled.load(std::memory_order_acquire) ||
				runtimeMetrics.unavailable.load(std::memory_order_acquire) ||
				!runtimeMetrics.joined.load(std::memory_order_acquire) ||
				!runtimeMetrics.sealed.load(std::memory_order_acquire) ||
				runtimeMetrics.startupFailed.load(std::memory_order_acquire) ||
				runtimeMetrics.runFailed.load(std::memory_order_acquire) || !runtimeMetrics.session) return false;
			// 私有根及 reparse 由 caller 验真；此薄接口至少拒绝相对/drive-relative 路径。
			if (!path || path[0] == L'\0') return false;
			const bool driveAbsolute = ((path[0] >= L'A' && path[0] <= L'Z') ||
				(path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
			const bool uncAbsolute = path[0] == L'\\' && path[1] == L'\\' && path[2] != L'\0';
			if (!driveAbsolute && !uncAbsolute) return false;
			try
			{
				const bool written = runtimeMetrics.session->WriteJson(path, runtimeMetrics.finalInput);
				runtimeMetrics.exportUnavailable.store(!written, std::memory_order_release);
				return written;
			}
			catch (...)
			{
				// 排序/分群/格式化也会分配；noexcept 导出不能把诊断失败变为 terminate。
				runtimeMetrics.exportUnavailable.store(true, std::memory_order_release);
				return false;
			}
		}

		HostRuntimeMetricsSnapshot CopyRuntimeMetricsSnapshot() const noexcept
		{
			HostRuntimeMetricsSnapshot value;
			value.enabled = runtimeMetrics.enabled.load(std::memory_order_acquire);
			// defaultoff 也暴露实际分配计数，smoke 不能仅凭 enable=false 推断无 Session。
			value.allocatedBytes = runtimeMetrics.allocatedBytes.load(std::memory_order_acquire);
			if (!value.enabled) return value;
			value.unavailable = runtimeMetrics.unavailable.load(std::memory_order_acquire);
			value.joined = runtimeMetrics.joined.load(std::memory_order_acquire);
			value.sealed = runtimeMetrics.sealed.load(std::memory_order_acquire);
			value.startupFailed = runtimeMetrics.startupFailed.load(std::memory_order_acquire);
			value.runFailed = runtimeMetrics.runFailed.load(std::memory_order_acquire);
			value.exportUnavailable = runtimeMetrics.exportUnavailable.load(std::memory_order_acquire);
			value.runSerial = runtimeMetrics.runSerial.load(std::memory_order_acquire);
			value.contactSeen = runtimeMetrics.contactSeen.load(std::memory_order_acquire);
			value.confirmed = runtimeMetrics.confirmed.load(std::memory_order_acquire);
			value.pending = runtimeMetrics.pending.load(std::memory_order_acquire);
			value.invalid = runtimeMetrics.invalid.load(std::memory_order_acquire);
			value.contactDropped = runtimeMetrics.contactDropped.load(std::memory_order_acquire);
			value.pendingOverflow = runtimeMetrics.pendingOverflow.load(std::memory_order_acquire);
			value.frameSerial = runtimeMetrics.frameSerial.load(std::memory_order_acquire);
			value.presentAttempts = runtimeMetrics.presentAttempts.load(std::memory_order_acquire);
			value.payloadByteUpperBound = runtimeMetrics.payloadByteUpperBound.load(std::memory_order_acquire);
			value.effectiveSamples = runtimeMetrics.effectiveSamples.load(std::memory_order_acquire);
			{ std::scoped_lock lock(runtimeMetrics.metadataMutex); value.metadata = runtimeMetrics.metadata; }
			return value;
		}

		void PublishRuntimeRevision() noexcept
		{
			runtimeRevision.Publish();
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

		bool ShortcutBaselineBlockedLocked() const noexcept
		{
			const auto desired = bridge.Snapshot();
			const auto expected = desired.presentationTarget
				? std::optional(Bridge::ReadyIdentityFor(*desired.presentationTarget)) : std::nullopt;
			return workspaceTransitionPending || inputStopping.load(std::memory_order_acquire) ||
				input.ConsumerFatal() || CanvasRecoveryBlocksAdmission(recovery,
					recoveryReportAvailable ? &reportedRecovery : nullptr) ||
				shortcutSelectionHandoffPending || commandScenePending ||
				(desired.workspace == Bridge::Workspace::Whiteboard && whiteboardLoadPending.load(std::memory_order_acquire)) ||
				(desired.workspace == Bridge::Workspace::Presentation
				? !expected || readyPresentationTarget != expected ||
					suspendedPresentationTarget.has_value() ||
					(startOptions.requirePresentationUiReady && uiReadyPresentationTarget != expected)
				: workspace.load(std::memory_order_acquire) == Bridge::Workspace::Presentation);
		}

		Shortcuts::Context ShortcutContextLocked() noexcept
		{
			auto context = shortcuts.Snapshot();
			const auto desired = bridge.Snapshot();
			const auto target = desired.presentationTarget
				? std::optional(Bridge::ReadyIdentityFor(*desired.presentationTarget)) : std::nullopt;
			context.available = running.load(std::memory_order_acquire) &&
				context.scope.pageEpoch != 0 && !ShortcutBaselineBlockedLocked() &&
				context.scope.workspace == desired.workspace && context.scope.presentation == target &&
				(!desired.hasPage || context.scope.pageIndex == desired.page);
			// 即使渲染合并了 A→B→A，也要退休旧意图；内容修订不参与该身份。
			shortcuts.Publish(context, desired.shortcutIntentEpoch);
			return shortcuts.Snapshot();
		}

		void RetireRecoveryForDesiredLocked(const Bridge::ProductState& desired)
		{
			// Bridge 可在 owner 消费前合并 D→W→D；仅投影当前身份，不丢弃 owner 仍承担的故障报告。
			auto next = ProjectCanvasRecovery(recoveryReportAvailable ? &reportedRecovery : nullptr,
				desired, input.ConsumerFatal());
			next.revision = recovery.revision;
			if (next == recovery) return;
			next.revision = ++recoveryRevision;
			recovery = next;
			PublishRuntimeRevision();
		}

		static void ObserveRecovery(void* context, const CanvasRecoveryState& value) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			std::scoped_lock lock(self->presentationTargetMutex);
			self->reportedRecovery = value;
			self->reportedRecovery.revision = 0;
			self->recoveryReportAvailable = true;
			// 旧业务报告可以保留在 owner 侧；对外只暴露与当前 Bridge 精确匹配的状态。
			self->RefreshPresentationInputGateLocked();
		}

		static bool RecoveryRetryAllowed(void* context, std::uint64_t revision) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return false;
			std::scoped_lock lock(self->presentationTargetMutex);
			return revision == self->recovery.revision && CanvasRecoveryRetryMatches(self->recovery,
				self->recovery, self->bridge.Snapshot(), self->running.load(std::memory_order_acquire),
				self->inputStopping.load(std::memory_order_acquire), self->input.ConsumerFatal());
		}

		// 调用方持有 presentationTargetMutex；发布、ready 和 UI ack 共用这一交接门。
		void RefreshPresentationInputGateLocked()
		{
			const auto desired = bridge.Snapshot();
			RetireRecoveryForDesiredLocked(desired);
			const auto expected = desired.presentationTarget
				? std::optional(Bridge::ReadyIdentityFor(*desired.presentationTarget))
				: std::nullopt;
			if (suspendedPresentationTarget != expected) suspendedPresentationTarget.reset();
			if (uiReadyPresentationTarget != expected) uiReadyPresentationTarget.reset();
			// 原有准入条件分别记录原因，持续关闸时也能观察阻塞从加载转为 UI 等待。
			std::uint32_t reasons = 0;
			if (inputStopping.load(std::memory_order_acquire)) reasons |= 1u;
			if (commandScenePending) reasons |= 2u;
			if (workspaceTransitionPending) reasons |= 1024u;
			if (input.ConsumerFatal()) reasons |= 256u;
			if (CanvasRecoveryBlocksAdmission(recovery,
				recoveryReportAvailable ? &reportedRecovery : nullptr)) reasons |= 512u;
			if (desired.workspace == Bridge::Workspace::Whiteboard &&
				whiteboardLoadPending.load(std::memory_order_acquire)) reasons |= 4u;
			if (desired.workspace == Bridge::Workspace::Presentation)
			{
				if (!expected) reasons |= 8u;
				if (readyPresentationTarget != expected) reasons |= 16u;
				if (suspendedPresentationTarget) reasons |= 32u;
				if (startOptions.requirePresentationUiReady && uiReadyPresentationTarget != expected)
					reasons |= 64u;
			}
			else if (workspace.load(std::memory_order_acquire) == Bridge::Workspace::Presentation)
				reasons |= 128u;
			const bool blocked = reasons != 0;
			const bool changed = input.BaseAdmissionBlocked() != blocked;
			input.SetAdmissionBlocked(blocked);
			if (reasons != inputGateReasons)
			{
				inputGateReasons = reasons;
				DiagnosticOutput::Printf(DiagnosticOutput::Stream::Output,
					"[Draw3.Input] action=admission reasons=%u desired=%u actual=%u session=%llu target=%llu\n",
					reasons, static_cast<unsigned>(desired.workspace),
					static_cast<unsigned>(workspace.load(std::memory_order_acquire)),
					static_cast<unsigned long long>(expected ? expected->sessionRevision : 0),
					static_cast<unsigned long long>(expected ? expected->targetRevision : 0));
			}
			if (changed)
			{
				if (expected) TracePptTiming(blocked ? "input_closed" : "input_open",
					expected->sessionRevision, expected->targetRevision);
				PublishRuntimeRevision();
			}
		}

		void ResetRuntimeDiagnostics()
		{
			hiddenPresentationNotFound.store(0, std::memory_order_release);
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
			presentDiagnosticInitialized = false;
			lastDiagnosticOutputTarget = HostOutputTarget::PrimaryDrawpad;
			lastDiagnosticOutputRevision = 0;
			lastDiagnosticPresentSucceeded = false;
			currentPageIndex.store(0, std::memory_order_release);
			pageCount.store(0, std::memory_order_release);
			currentPageHasContent.store(false, std::memory_order_release);
			// 新 Host 生命周期不能沿用上次运行的撤回/重做可用性。
			canUndo.store(false, std::memory_order_release);
			canRedo.store(false, std::memory_order_release);
			contentRevision.store(0, std::memory_order_release);
			completedStrokeKind.store(
				Bridge::CompletedStrokeKind::None, std::memory_order_release);
			acceptedStrokes.BeginHost();
			shortcuts.BeginHost();
			selectionMode.store(true, std::memory_order_release);
			workspace.store(Bridge::Workspace::Desktop, std::memory_order_release);
			whiteboardLoadPending.store(false, std::memory_order_release);
			{
				std::scoped_lock lock(presentationTargetMutex);
				readyPresentationTarget.reset();
				uiReadyPresentationTarget.reset();
				suspendedPresentationTarget.reset();
				input.SetAdmissionBlocked(false);
				input.SetShortcutAdmissionBlocked(false);
				shortcutSelectionHandoffPending = false;
				commandScenePending = false;
				workspaceTransitionEpoch = 0;
				workspaceTransitionPending = false;
				appliedProductRevision = 0;
				appliedTool = Bridge::Tool::Pen;
				inputGateReasons = 0;
				recovery = {};
				reportedRecovery = {};
				recoveryReportAvailable = false;
				recovery.revision = ++recoveryRevision;
				restoreLatestScene = false;
			}
			requestedOutputTarget.store(
				HostOutputTarget::PrimaryDrawpad, std::memory_order_release);
			requestedOutputRevision.store(0, std::memory_order_release);
			readyOutputTarget.store(
				HostOutputTarget::PrimaryDrawpad, std::memory_order_release);
			readyOutputRevision.store(0, std::memory_order_release);
			presentedContentRevision.store(0, std::memory_order_release);
			auxiliaryFullFrameClean.store(false, std::memory_order_release);
			runtimeRevision.Reset();
			drawingActivity.Reset();
			lastDirtyLeft.store(0, std::memory_order_release);
			lastDirtyTop.store(0, std::memory_order_release);
			lastDirtyRight.store(0, std::memory_order_release);
			lastDirtyBottom.store(0, std::memory_order_release);
			appliedBridgeRevision = (std::numeric_limits<std::uint64_t>::max)();
			requestedProductPage = false;
			requestedProductPageIndex = 0;
			requestedWorkspace = Bridge::Workspace::Desktop;
			requestedPresentationTargetRevision = 0;
			{
				std::scoped_lock lock(exitAutoSaveMutex);
				exitAutoSavePrepared = false;
			}
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
			const bool diagnosticTargetChanged = !self->presentDiagnosticInitialized ||
				self->lastDiagnosticOutputTarget != outputTarget ||
				self->lastDiagnosticOutputRevision != observation.outputRevision;
			const bool diagnosticRecovery = self->presentDiagnosticInitialized &&
				self->lastDiagnosticPresentSucceeded != succeeded;
			if (diagnosticTargetChanged || diagnosticRecovery)
			{
				DiagnosticOutput::Line() << "[Draw3Diag][present] event="
					<< (succeeded ? (diagnosticRecovery ? "recovery" : "success") : "failure")
					<< " mode=" << TransparentPresentModeName(self->presentation.ActiveMode())
					<< " target=" << HostOutputTargetName(outputTarget)
					<< " revision=" << observation.outputRevision
					<< " contentRevision=" << observation.presentedContentRevision
					<< " dirty=(" << dirty.left << "," << dirty.top << ","
					<< dirty.right << "," << dirty.bottom << ")"
					<< " full=" << (presentFull ? 1 : 0)
					<< " hwnd=0x" << std::hex
					<< reinterpret_cast<UINT_PTR>(outputTarget == HostOutputTarget::SelectionUlw
						? self->attachedPresentationWindow.load(std::memory_order_acquire)
						: self->attachedWindow.load(std::memory_order_acquire))
					<< " result=0x" << static_cast<unsigned long>(
						succeeded ? S_OK : self->presentation.LastFailure())
					<< std::dec << std::endl;
				if (!succeeded || diagnosticTargetChanged || diagnosticRecovery)
					LogDraw3WindowState(succeeded ? "present-success" : "present-failure",
						self->attachedWindow.load(std::memory_order_acquire),
						self->attachedPresentationWindow.load(std::memory_order_acquire));
			}
			self->presentDiagnosticInitialized = true;
			self->lastDiagnosticOutputTarget = outputTarget;
			self->lastDiagnosticOutputRevision = observation.outputRevision;
			self->lastDiagnosticPresentSucceeded = succeeded;
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
			if (self->runtimeMetrics.session)
			{
				self->CaptureRuntimeMetricsMetadata(false);
				self->PublishRuntimeMetricsProgress();
			}
			// 首帧尚未置 firstFrameReady；只有已 Stored 内容的真实成功版本可停在此门。
			const auto& options = self->startOptions;
			if (options.enableHiddenTestContactInjection && options.successfulPresentReachedEvent &&
				options.continueSuccessfulPresentEvent && succeeded &&
				self->firstFrameReady.load(std::memory_order_acquire) &&
				self->currentPageHasContent.load(std::memory_order_acquire) &&
				observation.presentedContentRevision == self->contentRevision.load(std::memory_order_acquire))
			{
				if (!SetEvent(options.successfulPresentReachedEvent) ||
					WaitForSingleObject(options.continueSuccessfulPresentEvent, INFINITE) != WAIT_OBJECT_0)
					throw std::runtime_error("hidden successful-present gate failed");
			}
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
			const std::size_t previousPage = self->currentPageIndex.exchange(
				currentPage, std::memory_order_acq_rel);
			const std::size_t previousCount = self->pageCount.exchange(
				pages, std::memory_order_acq_rel);
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
			case CanvasCommandType::PrepareExitAutoSave:
				{
					std::scoped_lock lock(self->exitAutoSaveMutex);
					self->exitAutoSavePrepared = true;
				}
				self->exitAutoSaveCondition.notify_all();
				break;
			default: break;
			}
			(void)self->runtimeRevision.PublishPageChange(
				previousPage, previousCount, currentPage, pages);
		}

		static void ObserveDocument(void* context, std::size_t currentPage, std::size_t pages)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			const std::size_t previousPage = self->currentPageIndex.exchange(
				currentPage, std::memory_order_acq_rel);
			const std::size_t previousCount = self->pageCount.exchange(
				pages, std::memory_order_acq_rel);
			(void)self->runtimeRevision.PublishPageChange(
				previousPage, previousCount, currentPage, pages);
		}

		static void ObserveCurrentPageContent(
			void* context, bool hasContent, std::uint64_t revision)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			{
				std::scoped_lock lock(self->contentMutex);
				// 不同页面可同为有墨迹或空白；内容版本也是发布载荷的一部分。
				if (self->currentPageHasContent.load(
					std::memory_order_relaxed) == hasContent &&
					self->contentRevision.load(std::memory_order_relaxed) == revision) return;
				self->currentPageHasContent.store(hasContent, std::memory_order_release);
				self->contentRevision.store(revision, std::memory_order_release);
			}
			self->contentCondition.notify_all();
			self->PublishRuntimeRevision();
		}

		static void ObserveWhiteboardThemeEdited(void* context, WhiteboardTheme theme) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self->runtimeCallbacks.whiteboardThemeEdited)
				self->runtimeCallbacks.whiteboardThemeEdited(self->runtimeCallbacks.context, theme);
		}

		static void ObserveWhiteboardExit(void* context, bool durable)
		{
			static_cast<Impl*>(context)->whiteboardExitDurable.store(durable, std::memory_order_release);
		}

		static void ObserveWhiteboard(void* context, WhiteboardTheme theme,
			std::uint64_t generation, bool pending, bool unsaved, std::size_t page, std::size_t count,
			bool hasContent, std::uint64_t contentVersion)
		{
			auto* self = static_cast<Impl*>(context);
			{
				std::scoped_lock lock(self->whiteboardMutex);
				self->whiteboardTheme = theme;
				self->whiteboardGeneration = generation;
				self->whiteboardLoadPending.store(pending, std::memory_order_release);
				self->whiteboardUnsaved = unsaved;
				self->whiteboardPageIndex = page;
				self->whiteboardPageCount = count;
				self->whiteboardHasContent = hasContent;
				self->whiteboardContentRevision = contentVersion;
				self->whiteboardCanUndo = self->canUndo.load(std::memory_order_acquire);
				self->whiteboardCanRedo = self->canRedo.load(std::memory_order_acquire);
			}
			{
				// 白板冷加载与 PPT、快捷键租约各自关门，恢复只更新基础准入。
				std::scoped_lock lock(self->presentationTargetMutex);
				self->RefreshPresentationInputGateLocked();
			}
			self->PublishRuntimeRevision();
		}

		static void ObserveHistoryAvailability(void* context, bool canUndo, bool canRedo)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			const bool undoChanged = self->canUndo.exchange(canUndo, std::memory_order_acq_rel) != canUndo;
			const bool redoChanged = self->canRedo.exchange(canRedo, std::memory_order_acq_rel) != canRedo;
			if (undoChanged || redoChanged) self->PublishRuntimeRevision();
		}

		static void ObserveStrokeCompleted(
			void* context, Bridge::CompletedStrokeKind kind)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self || kind == Bridge::CompletedStrokeKind::None) return;
			self->completedStrokeKind.store(kind, std::memory_order_release);
		}

		static void ObserveWorkspace(void* context, Bridge::Workspace value,
			std::size_t currentPage, std::size_t pages,
			const Bridge::PresentationReadyIdentity* presentationTarget)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			if (presentationTarget) TracePptTiming("canvas_presented",
				presentationTarget->sessionRevision, presentationTarget->targetRevision);
			{
				std::scoped_lock lock(self->presentationTargetMutex, self->whiteboardMutex);
				self->workspace.store(value, std::memory_order_release);
				self->currentPageIndex.store(currentPage, std::memory_order_release);
				self->pageCount.store(pages, std::memory_order_release);
				self->readyPresentationTarget = presentationTarget
					? std::optional<Bridge::PresentationReadyIdentity>(*presentationTarget)
					: std::nullopt;
				self->commandScenePending = false;
				if (value != Bridge::Workspace::Whiteboard)
					self->whiteboardLoadPending.store(false, std::memory_order_release);
				self->RefreshPresentationInputGateLocked();
			}
			self->requestedProductPage = false;
			self->PublishRuntimeRevision();
			const Bridge::ProductState desiredState = self->bridge.Snapshot();
			const Bridge::Workspace desired = desiredState.workspace;
			if (desired != value)
			{
				// 活动 contact 延迟期间目标可能再次变化；确认旧请求后立即补发最新目标。
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(desired);
				workspaceCommand.sceneClosedAdmission = true;
				self->window.EnqueueCanvasCommand(workspaceCommand);
				self->requestedWorkspace = desired;
				self->requestedPresentationTargetRevision = 0;
			}
			else if (desired == Bridge::Workspace::Presentation &&
				desiredState.presentationTarget && (!presentationTarget ||
					desiredState.presentationTarget->targetRevision !=
						presentationTarget->targetRevision))
			{
				CanvasCommand targetCommand;
				targetCommand.type = CanvasCommandType::SetPresentationTarget;
				targetCommand.presentationTarget = desiredState.presentationTarget;
				targetCommand.sceneClosedAdmission = true;
				self->window.EnqueueCanvasCommand(std::move(targetCommand));
				self->requestedWorkspace = desired;
				self->requestedPresentationTargetRevision = desiredState.presentationTarget->targetRevision;
			}
		}

		static bool ObserveDesktopAutoSave(void* context,
			DesktopAutoSaveTrigger trigger,
			draw3::uink::Draw3UInkExportSnapshot&& snapshot)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return false;
			const DesktopAutoSaveSubmitStatus status = self->autoSave.Submit(
				trigger, std::move(snapshot));
			if (status == DesktopAutoSaveSubmitStatus::Accepted ||
				status == DesktopAutoSaveSubmitStatus::Existing) return true;
			const char* reason = status == DesktopAutoSaveSubmitStatus::Closed
				? "closed" : "invalid";
			DiagnosticOutput::Printf(DiagnosticOutput::Stream::Error,
				"[Draw3.AutoSave] action=submit trigger=%s result=failed reason=%s\n",
				trigger == DesktopAutoSaveTrigger::Exit ? "exit" : "clear", reason);
			return false;
		}

		static bool ObserveDesktopLoad(void* context,
			draw3::uink::UInkGuid fileGuid)
		{
			auto* self = static_cast<Impl*>(context);
			return self && self->autoSave.SubmitLoad(std::move(fileGuid)) ==
				DesktopAutoSaveSubmitStatus::Accepted;
		}

		static bool ObservePresentationSave(void* context,
			PresentationSaveRequest&& request)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return false;
			const auto status = self->presentationAutoSave.SubmitSave(std::move(request));
			if (status == PresentationPersistenceSubmitStatus::Accepted ||
				status == PresentationPersistenceSubmitStatus::ReplacedPending) return true;
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.Presentation] action=save_submit result=failed\n");
			return false;
		}

		static bool ObservePresentationLoad(void* context,
			PresentationLoadRequest&& request)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return false;
			if (self->presentationAutoSave.SubmitLoad(std::move(request)) ==
				PresentationPersistenceSubmitStatus::Accepted) return true;
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.Presentation] action=load_submit result=failed\n");
			return false;
		}

		static void WakeForPresentationPersistence(void* context) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self) (void)self->input.PublishControlWake();
		}


		static void ObservePenDiagnostics(void* context, const PenRuntimeDiagnostics& value)
		{
			auto* self = static_cast<Impl*>(context);
			// 复用诊断快照锁；只有隐藏测试安装此回调，产品绘制帧不增加锁开销。
			std::scoped_lock lock(self->eraserDiagnosticsMutex);
			self->penDiagnostics = value;
		}

		static void ObserveEraserDiagnostics(void* context,const SpeedEraser::Diagnostics& value)
		{
			auto* self=static_cast<Impl*>(context);
			uint64_t sequence=0;
			{
				std::scoped_lock lock(self->eraserDiagnosticsMutex);
				sequence=self->eraserDiagnostics.frameSequence+1;
				self->eraserDiagnostics=value;
				self->eraserDiagnostics.frameSequence=sequence;
			}
			if(!self->touchAreaTraceEnabled.load(std::memory_order_relaxed))
			{self->touchAreaTraceWasEnabled=false;return;}
			const auto now=std::chrono::steady_clock::now();
			const bool contact=value.eraserContact;
			const bool inputActivity=value.inputContact || value.preview;
			const auto traceRevision=self->touchAreaTraceRevision.load(std::memory_order_relaxed);
			const bool newlyEnabled=!self->touchAreaTraceWasEnabled || traceRevision!=self->observedTouchAreaTraceRevision;
			self->observedTouchAreaTraceRevision=traceRevision;
			const bool touch=value.inputType==static_cast<uint32_t>(InputDeviceType::Touch) &&
				value.inputSource.contextId!=0;
			if(newlyEnabled || (touch && value.inputSource!=self->lastTouchAreaMetadataSource))
				self->touchAreaMetadataPending=true;
			if(self->inputMode.load(std::memory_order_acquire) != HostInputMode::Win32Mouse &&
				self->touchAreaMetadataPending && (newlyEnabled ||
				now-self->lastTouchAreaMetadataTrace>=std::chrono::seconds(1)))
			{
				const auto source=touch?value.inputSource:SpeedEraser::InputSource{};
				self->touchAreaMetadataPending=!self->stylus.TraceTouchAreaDiagnostics(source);
				self->lastTouchAreaMetadataTrace=now;
				if(!self->touchAreaMetadataPending)self->lastTouchAreaMetadataSource=source;
			}
			const bool edge=newlyEnabled || contact!=self->lastTracedContact ||
				inputActivity!=self->lastTracedInputActivity;
			if(!edge && (!inputActivity || now-self->lastTouchAreaTrace<std::chrono::milliseconds(250)))return;
			const char* event=newlyEnabled?"enabled":contact!=self->lastTracedContact?
				(contact?"begin":"end"):"sample";
			const char* inputEvent=newlyEnabled?"enabled":inputActivity!=self->lastTracedInputActivity?
				(inputActivity?"begin":"end"):"sample";
			self->touchAreaTraceWasEnabled=true;self->lastTracedContact=contact;
			self->lastTracedInputActivity=inputActivity;self->lastTouchAreaTrace=now;
			const auto& d=value;const auto& a=d.contactArea;const auto& source=d.inputSource;
			const bool requested=self->window.TouchContactAreaAssistance();
			const char* gate=!contact?"no-eraser-contact":!d.active?"not-speed-eraser":
				!requested?"option-off":!a.enabled?"waiting-new-contact-batch":
				d.inputType!=static_cast<uint32_t>(InputDeviceType::Touch)?"not-touch-input":
				source.kind!=SpeedEraser::SourceKind::Touch?"touch-relationship-unknown":
				!d.inputMapped?"display-mapping-unknown":
				a.sample.units!=SpeedEraser::ContactAreaUnits::CanvasPixels?"area-units-unusable":
				!a.sampleValid?"area-rejected":!a.referenceReady?"waiting-stable-drag":
				!d.touchUnlocked?"waiting-startup-displacement":!a.referenceFresh?"reference-expired":
				!a.active?"waiting-accepted-movement":"area-floor-active";
			if(d.eraserContact || d.preview)
				DiagnosticOutput::Printf(DiagnosticOutput::Stream::Error,"[EraserEntry] seq=%llu entry=%s kind=%s penResponse=%s debugOverride=%d inherited=%d reason=%s hoverTime=%.6f downTime=%.6f frameTime=%.6f previousShownPx=%.3f downPx=%.3f firstRadiusPx=%.3f cursorPx=%.3f currentRadiusPx=%.3f\n",
					static_cast<unsigned long long>(sequence),SpeedEraser::InputEntryName(d.entry),
					d.eraserKind==SpeedEraser::EraserKind::Speed?"Speed":"Fixed",SpeedEraser::PenResponseName(d.formalPenResponse),
					d.developmentResponseOverride,d.sessionInherited,d.sessionReason,d.hoverSeconds,d.downSeconds,d.frameSeconds,d.previousShownDiameterPx,d.downDiameterPx,
					d.firstPointRadiusPx,d.cursorDiameterPx,d.nextRadiusPx);
			// 同一选中输入的限频快照；无可见光标或非橡皮尺寸明确写 -1，不代用其他设备。
			const bool eraserSizeValid=d.eraserContact || d.preview;
			const char* eraserKind=eraserSizeValid?
				(d.eraserKind==SpeedEraser::EraserKind::Speed?"Speed":"Fixed"):"N/A";
			char inputText[1024]{};
			std::snprintf(inputText,sizeof(inputText),
				"[EraserInput] seq=%llu event=%s frameSeconds=%.6f inputContact=%d eraserContact=%d preview=%d tool=%u/%u device=%s inputType=%u source=%s recognition=%u tcid=%u contactId=%u contactGen=%llu cursorId=%u inputCanvasPx=(%.3f,%.3f) inputPositionValid=%d cursorCanvasPx=(%.3f,%.3f) cursorVisible=%d eraserKind=%s B=%.3f targetDip=%.3f actualDip=%.3f cursorDiameterPx=%.3f geometryDiameterPx=%.3f speed=%.3f sweepSpeed=%.3f unit=%s qualified=%d evidenceMs=%.1f areaRaw=(%.3f,%.3f) areaDip=(%.3f,%.3f) areaActive=%d\n",
				static_cast<unsigned long long>(sequence),inputEvent,d.frameSeconds,d.inputContact,d.eraserContact,d.preview,
				d.selectedTool,d.effectiveTool,d.inputPositionValid?InputDeviceTypeName(d.inputType):"Unknown",d.inputType,
				SpeedEraser::SourceKindName(source.kind),static_cast<unsigned>(source.recognition),source.contextId,
				d.contactId,static_cast<unsigned long long>(d.contactGeneration),source.cursorId,
				d.inputPositionValid?d.inputCanvasXpx:-1.0f,d.inputPositionValid?d.inputCanvasYpx:-1.0f,d.inputPositionValid,
				d.cursorVisible?d.cursorCanvasXpx:-1.0f,d.cursorVisible?d.cursorCanvasYpx:-1.0f,d.cursorVisible,
				eraserKind,eraserSizeValid?d.sizes.standardDiameterDip:-1.0f,
				eraserSizeValid?d.targetDiameterDip:-1.0f,eraserSizeValid?d.effectiveDiameterDip:-1.0f,
				d.cursorVisible?d.cursorDiameterPx:-1.0f,d.eraserContact?d.nextRadiusPx*2:-1.0f,
				d.speed,d.sweepSpeed,SpeedEraser::MotionUnitName(d.motionUnit),d.qualified,d.evidenceSeconds*1000,
				a.sample.rawWidth,a.sample.rawHeight,a.widthDip,a.heightDip,a.active);
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, inputText, true);
			if(d.eraserContact && d.eraserKind==SpeedEraser::EraserKind::Speed &&
				(d.response==SpeedEraser::ResponseModel::DirectTouch ||
					d.response==SpeedEraser::ResponseModel::ScreenPenHybrid))
			{
				const auto& f=d.follow;
				char followText[768]{};
				// 当前帧无Move时许可为零；另列最近真实输入，避免把残留目标当成新运动。
				std::snprintf(followText,sizeof(followText),
					"[EraserFollow] seq=%llu rawTargetDip=%.3f effectiveTargetDip=%.3f evidenceCapDip=%.3f growthGoalDip=%.3f actualDip=%.3f lastInputSweepSpeed=%.3f lastInputAgeMs=%.1f sweepMotionQualified=%d areaMotionQualified=%d holdRemainingMs=%.1f holdReason=%s decreaseMs=%.1f decreaseConfirmMs=%.1f decreasePending=%d shrinking=%d sweeping=%d decreaseAction=%s resetCount=%llu lastResetReason=%s\n",
					static_cast<unsigned long long>(sequence),f.rawTargetDip,d.targetDiameterDip,d.evidenceCapDiameterDip,
					f.growthGoalDip,d.effectiveDiameterDip,f.lastInputSweepSpeed,f.lastInputAgeSeconds*1000,
					f.motionGrowthPermitted,f.areaGrowthPermitted,f.holdRemainingSeconds*1000,f.holdReason,
					f.decreaseProgressSeconds*1000,f.decreaseConfirmationSeconds*1000,f.decreasePending,f.shrinking,
					d.sweeping,f.decreaseReason,static_cast<unsigned long long>(f.decreaseResetCount),f.lastDecreaseResetReason);
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, followText, true);
			}
			char text[3072]{};
			if(!d.active)
			{
				std::snprintf(text,sizeof(text),"[TouchArea] seq=%llu event=%s gate=%s contact=%d inputType=%u source=%s cursorPx=%.3f requested=%d\n",
					static_cast<unsigned long long>(sequence),event,gate,contact,d.inputType,
					SpeedEraser::SourceKindName(source.kind),d.cursorDiameterPx,requested);
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, text, true);return;
			}
			std::snprintf(text,sizeof(text),
				"[TouchArea] seq=%llu event=%s gate=%s contact=%d speedMode=%d inputType=%u source=%s recognition=%u tcid=%u cid=%u sourceGen=%llu\n"
				"[TouchArea] seq=%llu model=%s scale=%s unit=%s monitor=%p mappedMonitor=%p mapped=%d mappedRect=(%d,%d,%d,%d) pixels=%dx%d dpi=%.1fx%.1f DIP/px=%.6fx%.6f motion/px=%.6fx%.6f rho=%.6f manualCm=%.1fx%.1f displayGen=%llu/%llu\n"
				"[TouchArea] seq=%llu requested=%d latched=%d raw=%.3fx%.3f convertedPx=%.3fx%.3f units=%s DIP=%.3fx%.3f valid=%d reason=%s ready=%d fresh=%d releasing=%d unlocked=%d stableMs=%.1f refDip=%.3fx%.3f outlierAxes=%u outlierRatio=%.3fx%.3f recoveryCandidateDip=%.3fx%.3f recoveryMs=%.1f recovering=%d recoveryCount=%u firstRefFloor=%.3f refFloor=%.3f acceptedFloor=%.3f areaActive=%d areaAboveB=%d speed=%.3f evidenceMs=%.1f targetDIP=%.3f actualDIP=%.3f cursorPx=%.3f nextRadiusPx=%.3f historyRadiusPx=%.3f points=%llu idleMs=%.1f animate=%d\n",
				static_cast<unsigned long long>(sequence),event,gate,contact,d.active,d.inputType,
				SpeedEraser::SourceKindName(source.kind),static_cast<unsigned>(source.recognition),source.contextId,source.cursorId,static_cast<unsigned long long>(source.generation),
				static_cast<unsigned long long>(sequence),SpeedEraser::ResponseModelName(d.response),SpeedEraser::ScaleSourceName(d.motionSource),SpeedEraser::MotionUnitName(d.motionUnit),
				reinterpret_cast<void*>(d.monitor),reinterpret_cast<void*>(source.mappedMonitor),d.inputMapped,
				source.mappedLeft,source.mappedTop,source.mappedWidth,source.mappedHeight,d.pixelWidth,d.pixelHeight,d.dpiX,d.dpiY,
				d.dipPerPixelX,d.dipPerPixelY,d.motionPerPixelX,d.motionPerPixelY,d.rhoMmPerDip,d.manualWidthCm,d.manualHeightCm,
				static_cast<unsigned long long>(d.displayGeneration),static_cast<unsigned long long>(d.displayRevision),
				static_cast<unsigned long long>(sequence),requested,a.enabled,a.sample.rawWidth,a.sample.rawHeight,a.sample.widthPx,a.sample.heightPx,
				SpeedEraser::ContactAreaUnitsName(a.sample.units),a.widthDip,a.heightDip,a.sampleValid,SpeedEraser::ContactAreaReasonName(a.reason),
				a.referenceReady,a.referenceFresh,a.releasing,d.touchUnlocked,a.stableMotionSeconds*1000,
				a.referenceWidthDip,a.referenceHeightDip,static_cast<unsigned>(a.outlierAxes),
				a.outlierWidthRatio,a.outlierHeightRatio,a.recoveryCandidateWidthDip,a.recoveryCandidateHeightDip,
				a.recoveryMotionSeconds*1000,a.recovering,a.recoveryCount,a.firstReferenceFloorDip,
				a.referenceFloorDip,a.activeFloorDip,a.active,a.areaFloorAboveStandard,
				d.speed,d.evidenceSeconds*1000,d.targetDiameterDip,d.effectiveDiameterDip,d.cursorDiameterPx,d.nextRadiusPx,d.historyRadiusPx,
				static_cast<unsigned long long>(d.realPointCount),d.idleSeconds*1000,d.needsAnimation);
			DiagnosticOutput::Printf(DiagnosticOutput::Stream::Error,"[FineBand] seq=%llu speed=%.3f unit=%s held=%d enter=%.3f release=%.3f change=%.3f direction=%d targetDIP=%.3f actualDIP=%.3f areaFloorDIP=%.3f animate=%d\n",
				static_cast<unsigned long long>(sequence),d.fine.speed,SpeedEraser::MotionUnitName(d.motionUnit),
				d.fine.held,d.fine.enterProgress,d.fine.releaseProgress,d.fine.changeProgress,d.fine.direction,
				d.targetDiameterDip,d.effectiveDiameterDip,a.activeFloorDip,d.needsAnimation);
			const char* profile=d.response!=SpeedEraser::ResponseModel::DirectTouch?"N/A":
				d.motionUnit!=SpeedEraser::MotionUnit::MillimetersPerSecond?"Fallback":
				d.touchProfileWeight<=0?"Small":d.touchProfileWeight>=1?"Classroom":"Intermediate";
			char curveText[960]{};
			std::snprintf(curveText,sizeof(curveText),
				"[TouchCurve] seq=%llu requestedDeviceMode=%s resolvedTouchProfile=%s profileWeight=%.4f profileSource=%s longEdgeMm=%.1f motionSource=%s unit=%s fine=%.3f enter=%.3f exit=%.3f large=%.3f B=%.3f sweepGain=%.3f speed=%.3f sweepSpeed=%.3f fineSpeed=%.3f qualified=%d evidenceMs=%.1f evidenceStartMs=%.1f evidenceFullMs=%.1f evidenceDecayMs=%.1f evidenceCapDIP=%.3f growthTauMs=%.1f largeGrowthTauMs=%.1f logGrowth=%.2f largeLogGrowth=%.2f targetDIP=%.3f actualDIP=%.3f cursorPx=%.3f geometryPx=%.3f\n",
				static_cast<unsigned long long>(sequence),SpeedEraser::DeviceModeName(d.requestedDeviceMode),profile,
				d.touchProfileWeight,SpeedEraser::TouchProfileSourceName(d.touchProfileSource),d.touchSurfaceLongEdgeMm,
				SpeedEraser::ScaleSourceName(d.motionSource),SpeedEraser::MotionUnitName(d.motionUnit),
				d.fineToStandardSpeed,d.sweepEnterSpeed,d.sweepExitSpeed,d.largeTargetSpeed,d.sizes.standardDiameterDip,
				d.sweepGain,d.speed,d.sweepSpeed,d.fine.speed,d.qualified,d.evidenceSeconds*1000,
				d.evidenceStartSeconds*1000,d.evidenceFullSeconds*1000,d.evidenceDecaySeconds*1000,
				d.evidenceCapDiameterDip,d.growthTauSeconds*1000,d.largeGrowthTauSeconds*1000,
				d.maximumLogGrowthPerSecond,d.largeLogGrowthPerSecond,
				d.targetDiameterDip,d.effectiveDiameterDip,d.cursorDiameterPx,d.nextRadiusPx*2);
			// 只在帧级诊断入口限频输出，不在 RTS packet 热路径写日志，也不持快照锁输出。
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, text, true);
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, curveText, true);
		}

		static void ObserveDrawingActivity(void* context, bool active) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			(void)self->drawingActivity.Publish(active,
				self->runtimeCallbacks.context,
				self->runtimeCallbacks.drawingActivityChanged);
		}

		static void ObserveShortcutContext(void* context, Shortcuts::Context value) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			std::scoped_lock lock(self->presentationTargetMutex);
			self->shortcuts.Publish(value, self->bridge.Snapshot().shortcutIntentEpoch);
		}
		static Shortcuts::Result AllowShortcutCommand(void* context,
			const Shortcuts::Request& request, const Shortcuts::Context& actual) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return Shortcuts::Result::Stale;
			std::scoped_lock lock(self->presentationTargetMutex);
			(void)self->ShortcutContextLocked();
			return self->shortcuts.Authorize(request, actual);
		}
		static void CompleteShortcut(void* context, std::uint64_t id, Shortcuts::Result result) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self) self->shortcuts.Complete(id, result);
		}

		static void ObserveAcceptedStrokeScene(void* context, const Bridge::AcceptedStrokeBegin& scene) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self) self->acceptedStrokes.PublishScene(scene);
		}

		static void ObserveAcceptedStroke(void* context, const Bridge::AcceptedStrokeBegin& value) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self || !self->runtimeCallbacks.acceptedStrokeBegan) return;
			auto accepted = value;
			self->acceptedStrokes.Stamp(accepted);
			self->runtimeCallbacks.acceptedStrokeBegan(self->runtimeCallbacks.context, accepted);
		}

		void EndDrawingActivity() noexcept
		{
			(void)drawingActivity.EndIfActive(runtimeCallbacks.context,
				runtimeCallbacks.drawingActivityChanged);
		}

		static bool ApplyStyle(void* context, DWORD setMask, DWORD clearMask)
		{
			const auto* callbacks = static_cast<const HostStyleCallbacks*>(context);
			// 无产品宿主时允许隐藏测试使用既有样式；产品宿主必须提供 Window Service 回调。
			return !callbacks || !callbacks->setExtendedStyleFlags ||
				callbacks->setExtendedStyleFlags(callbacks->context, setMask, clearMask);
		}


		void PublishDisplaySnapshot(Inkeys::Display::SnapshotPtr snapshot)
		{
			{
				std::scoped_lock lock(displayMutex);
				pendingDisplaySnapshot = std::move(snapshot);
			}
			displayScaleDirty.store(true, std::memory_order_release);
			(void)input.PublishControlWake();
		}

		void PumpDisplayScale()
		{
			if (!displayScaleDirty.exchange(false, std::memory_order_acq_rel)) return;
			Inkeys::Display::SnapshotPtr snapshot;
			SpeedEraser::DevelopmentOptions development;
			{
				std::scoped_lock lock(displayMutex);
				snapshot = pendingDisplaySnapshot;
				development = eraserDevelopment;
			}
			const HWND hwnd = attachedWindow.load(std::memory_order_acquire);
			SpeedEraser::DisplayScale scale;
			scale.development = development;
			window.SetTouchContactAreaAssistance(development.touchContactAreaAssistance);
			scale.development.touchAreaTrace=false;
			touchAreaTraceEnabled.store(development.touchAreaTrace,std::memory_order_relaxed);
			scale.development.touchContactAreaAssistance=false; // 面积开关不属于 Mouse/Pen 的显示标尺。
			window.SetEraserDiagnosticsEnabled(development.diagnostics || development.touchAreaTrace || hiddenTestContactInjectionEnabled || startOptions.enableEraserDiagnostics);
			scale.generation = snapshot ? snapshot->generation : 0;
			const HMONITOR monitorHandle = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
			scale.monitor = reinterpret_cast<std::uintptr_t>(monitorHandle);
			const auto* monitor = snapshot ? snapshot->Find(monitorHandle) : nullptr;
			RECT clientBounds{};
			POINT origin{};
			const bool clientKnown = hwnd && GetClientRect(hwnd, &clientBounds) && ClientToScreen(hwnd, &origin);
			if (clientKnown) OffsetRect(&clientBounds, origin.x, origin.y);
			if (monitor)
			{
				scale.dipPerPixelX = 96.0f / (monitor->effectiveDpiX ? monitor->effectiveDpiX : 96u);
				scale.dipPerPixelY = 96.0f / (monitor->effectiveDpiY ? monitor->effectiveDpiY : 96u);
				scale.pixelWidth=monitor->pixelWidth;scale.pixelHeight=monitor->pixelHeight;
				scale.desktopLeft=monitor->bounds.left;scale.desktopTop=monitor->bounds.top;
				scale.orientation=monitor->orientation;
				scale.logicalOutputKnown=!snapshot->fallback && !monitor->fallback && clientKnown &&
					clientBounds.left>=monitor->bounds.left && clientBounds.top>=monitor->bounds.top &&
					clientBounds.right<=monitor->bounds.right && clientBounds.bottom<=monitor->bounds.bottom;
				scale.physicalAvailable = monitor->physicalSize.available &&
					monitor->pixelWidth > 0 && monitor->pixelHeight > 0;
				if (scale.physicalAvailable)
				{
					scale.cmPerPixelX = static_cast<float>(monitor->physicalSize.widthCm) / monitor->pixelWidth;
					scale.cmPerPixelY = static_cast<float>(monitor->physicalSize.heightCm) / monitor->pixelHeight;
				}
				// 输入直接性及目标映射交由当前 RTS 来源确认，不能按整机单屏推断。
			}
			else if (hwnd)
			{
				const UINT dpi = QueryCompatibleWindowDpi(hwnd);
				scale.dipPerPixelX = scale.dipPerPixelY = 96.0f / (dpi ? dpi : 96u);
			}
			const bool syntheticDisplay=hiddenTestContactInjectionEnabled && startOptions.hiddenTestDisplayScale.has_value();
			if(syntheticDisplay)
			{
				const auto policy=scale.development;
				scale=*startOptions.hiddenTestDisplayScale;scale.development=policy;
			}
			if(development.touchAreaTrace)
			{
				// 只在显示/诊断配置发布时记录同一快照；活动分辨率不冒充 EDID 原始时序。
				const auto* edidMonitor=syntheticDisplay?nullptr:monitor;
				const Inkeys::Display::EdidInfo emptyEdid;
				const Inkeys::Display::PhysicalSizeInfo emptyPhysicalSize;
				const auto& edid=edidMonitor?edidMonitor->edid:emptyEdid;
				const auto& physicalSize=edidMonitor?edidMonitor->physicalSize:emptyPhysicalSize;
				char displayText[768]{};
				std::snprintf(displayText,sizeof(displayText),
					"[EraserDisplay] synthetic=%d snapshotGen=%llu scaleGen=%llu topology=%s monitor=%p desktopLeftTopSizePx=(%d,%d,%d,%d) activeResolutionPx=%dx%d effectiveDpi=%.1fx%.1f DIP/px=%.6fx%.6f logicalKnown=%d edidStatus=%s edidValid=%d edidVersion=%u.%u edidRawCm=%dx%d physicalAvailable=%d physicalCm=%dx%d physicalReason=%s\n",
					syntheticDisplay,static_cast<unsigned long long>(snapshot?snapshot->generation:0),
					static_cast<unsigned long long>(scale.generation),DisplayTopologyName(snapshot?snapshot->topology:Inkeys::Display::DisplayTopology::Unknown),
					reinterpret_cast<void*>(scale.monitor),scale.desktopLeft,scale.desktopTop,scale.pixelWidth,scale.pixelHeight,
					scale.pixelWidth,scale.pixelHeight,96.0/scale.dipPerPixelX,96.0/scale.dipPerPixelY,
					scale.dipPerPixelX,scale.dipPerPixelY,scale.logicalOutputKnown,EdidStatusName(edid.status),edid.valid,
					static_cast<unsigned>(edid.majorVersion),static_cast<unsigned>(edid.minorVersion),
					edid.rawPhysicalWidthCm,edid.rawPhysicalHeightCm,physicalSize.available,
					physicalSize.widthCm,physicalSize.heightCm,
					edidMonitor?PhysicalReasonName(physicalSize.unavailableReason):"NoMonitor");
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, displayText, true);
			}
			const auto previous = window.SpeedEraserDisplayScaleSnapshot();
			scale.revision = previous.revision;
			if (!(scale == previous) || !EqualRect(&clientBounds, &appliedDisplayClientBounds))
			{
				scale.revision = previous.revision + 1;
				appliedDisplayClientBounds = clientBounds;
				window.SetSpeedEraserDisplayScale(scale);
			}
			Inkeys::Business::PenWidth::Context penContext;
			penContext.paintDevice = bridge.Snapshot().paintDevice;
			penContext.effectiveDpi = monitor && !syntheticDisplay
				? (monitor->effectiveDpiX ? monitor->effectiveDpiX : 96u)
				: syntheticDisplay ? 96.0 / scale.dipPerPixelX : QueryCompatibleWindowDpi(hwnd);
			penContext.pixelWidth = scale.pixelWidth;
			penContext.pixelHeight = scale.pixelHeight;
			if (!monitor && !syntheticDisplay)
			{
				// 快照缺目标时用同一目标全幅矩形回退，不借用窗口/工作区/虚拟桌面。
				MONITORINFO info{};
				info.cbSize = sizeof(info);
				if (monitorHandle && GetMonitorInfoW(monitorHandle, &info))
				{
					penContext.pixelWidth = info.rcMonitor.right - info.rcMonitor.left;
					penContext.pixelHeight = info.rcMonitor.bottom - info.rcMonitor.top;
				}
			}
			if (!penWidthContextPrepared || appliedPenWidthMonitor != scale.monitor ||
				!Inkeys::Business::PenWidth::EquivalentContext(penContext, appliedPenWidthContext))
			{
				penContext.revision = appliedPenWidthContext.revision + 1;
				appliedPenWidthContext = penContext;
				appliedPenWidthMonitor = scale.monitor;
				penWidthContextPrepared = true;
				// 不持显示锁；返回前产品必须发布同代 resolved DIP 和目标上下文。
				if (runtimeCallbacks.penWidthContextChanged)
					runtimeCallbacks.penWidthContextChanged(runtimeCallbacks.context, penContext);
			}
		}

		void PumpBridgeState()
		{
			// 仅绘制 owner 访问 Controller；独立配置不受 bridge revision 去重影响。
			const bool reconnectEnabled = interruptedStrokeReconnectEnabled.load(std::memory_order_acquire);
			if (drawing && drawing->GetInterruptedStrokeReconnectEnabled() != reconnectEnabled)
				drawing->SetInterruptedStrokeReconnectEnabled(reconnectEnabled);
			if (drawing) drawing->SetDrawingHapticsEnabled(drawingHapticsEnabled.load(std::memory_order_acquire));
			PumpDisplayScale();
			{
				std::scoped_lock lock(presentationTargetMutex);
				RefreshPresentationInputGateLocked();
			}
			const Bridge::ProductState state = bridge.Snapshot();
			if (state.revision == appliedBridgeRevision && !restoreLatestScene) return;
			appliedBridgeRevision = state.revision;

			window.SetSelectionMode(state.selectionMode);
			{
				std::scoped_lock lock(presentationTargetMutex);
				if (shortcutSelectionHandoffPending && window.SelectionMode() == bridge.Snapshot().selectionMode)
				{
					shortcutSelectionHandoffPending = false;
					input.SetShortcutAdmissionBlocked(shortcuts.Lease() != 0);
				}
			}
			window.SetAutoSaveEnabled(state.autoSaveEnabled);
			const bool selectionChanged = selectionMode.exchange(state.selectionMode,
				std::memory_order_acq_rel) != state.selectionMode;
			DrawingTool tool = DrawingTool::Pen;
			switch (state.tool)
			{
			case Bridge::Tool::HardPen: tool = DrawingTool::HardPen; break;
			case Bridge::Tool::Highlighter: tool = DrawingTool::Highlighter; break;
			case Bridge::Tool::ConfiguredEraser:
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
			// 活动 contact 可能仍锁存旧工具，原始偏好交由 Controller 按实际工具投影。
			if (drawing) drawing->SetCursorSettings(state.cursorSettings);
			window.SetSpeedEraserDeviceMode(state.paintDevice == 0
				? SpeedEraser::DeviceMode::LargeScreen : SpeedEraser::DeviceMode::Laptop);
			window.SetEraserWidthMode(state.tool == Bridge::Tool::SpeedEraser
				? EraserWidthMode::Speed : EraserWidthMode::Fixed);
			window.SetEraserInputs(state.eraserInputs);
			window.SetEraserToolPolicy(state.tool==Bridge::Tool::FixedEraser?SpeedEraser::EraserToolPolicy::Fixed:
				state.tool==Bridge::Tool::SpeedEraser?SpeedEraser::EraserToolPolicy::Speed:SpeedEraser::EraserToolPolicy::ByEntry);
			window.SetProductVisualStyle(state.colorRgba, state.widthDip, state.penWidthContext);
			const bool dispatchScene = PresentationSceneNeedsDispatch(state,
				workspace.load(std::memory_order_acquire), requestedWorkspace,
				requestedPresentationTargetRevision, restoreLatestScene);
			if (state.workspace != Bridge::Workspace::Presentation && dispatchScene)
			{
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(state.workspace);
				{
					std::scoped_lock lock(presentationTargetMutex);
					workspaceCommand.sceneClosedAdmission = PresentationSceneNeedsReady(state,
						workspace.load(std::memory_order_acquire), readyPresentationTarget, commandScenePending);
				}
				window.EnqueueCanvasCommand(workspaceCommand);
				requestedWorkspace = state.workspace;
				requestedPresentationTargetRevision = 0;
			}
			if (state.workspace == Bridge::Workspace::Presentation &&
				state.presentationTarget && dispatchScene)
			{
				CanvasCommand targetCommand;
				targetCommand.type = CanvasCommandType::SetPresentationTarget;
				targetCommand.presentationTarget = state.presentationTarget;
				{
					std::scoped_lock lock(presentationTargetMutex);
					targetCommand.sceneClosedAdmission = PresentationSceneNeedsReady(state,
						workspace.load(std::memory_order_acquire), readyPresentationTarget, commandScenePending);
				}
				window.EnqueueCanvasCommand(std::move(targetCommand));
				requestedWorkspace = state.workspace;
				requestedPresentationTargetRevision =
					state.presentationTarget->targetRevision;
			}
			else if (state.workspace == Bridge::Workspace::Presentation &&
				!state.presentationTarget && dispatchScene)
			{
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(
					Bridge::Workspace::Presentation);
				{
					std::scoped_lock lock(presentationTargetMutex);
					workspaceCommand.sceneClosedAdmission = PresentationSceneNeedsReady(state,
						workspace.load(std::memory_order_acquire), readyPresentationTarget, commandScenePending);
				}
				window.EnqueueCanvasCommand(workspaceCommand);
				requestedWorkspace = state.workspace;
				requestedPresentationTargetRevision = 0;
			}
			restoreLatestScene = false;
			bool preferencesChanged;
			{
				std::scoped_lock lock(presentationTargetMutex);
				// 只确认 owner 已完整应用的版本；Bridge 发布目标不等于实际工具已就绪。
				preferencesChanged = appliedProductRevision != state.revision || appliedTool != state.tool;
				appliedProductRevision = state.revision;
				appliedTool = state.tool;
			}
			if (selectionChanged || preferencesChanged) PublishRuntimeRevision();
		}

		void EnqueueCommandScene(const Bridge::Command& command)
		{
			bool sceneClosedAdmission = false;
			{
				std::scoped_lock lock(presentationTargetMutex);
				const auto captured = command.presentationTarget
					? std::optional(Bridge::ReadyIdentityFor(*command.presentationTarget))
					: std::nullopt;
				if ((command.workspace == Bridge::Workspace::Presentation ||
					workspace.load(std::memory_order_acquire) == Bridge::Workspace::Presentation) &&
					(captured != readyPresentationTarget || command.workspace != workspace.load(std::memory_order_acquire)))
				{
					// 旧场景上的已接受命令仍须 FIFO 完成；重放期间不能沿用 latest 的旧 ready 开放输入。
					readyPresentationTarget.reset();
					commandScenePending = true;
					input.SetAdmissionBlocked(true);
					sceneClosedAdmission = true;
					PublishRuntimeRevision();
				}
			}
			CanvasCommand scene;
			scene.sceneClosedAdmission = sceneClosedAdmission;
			if (command.workspace == Bridge::Workspace::Presentation &&
				command.presentationTarget)
			{
				scene.type = CanvasCommandType::SetPresentationTarget;
				scene.presentationTarget = command.presentationTarget;
			}
			else
			{
				scene.type = CanvasCommandType::SetWorkspace;
				scene.workspace = static_cast<std::uint8_t>(command.workspace);
			}
			window.EnqueueCanvasCommand(std::move(scene));
			requestedWorkspace = command.workspace;
			requestedPresentationTargetRevision = command.workspace == Bridge::Workspace::Presentation &&
				command.presentationTarget ? command.presentationTarget->targetRevision : 0;
		}

		void PumpOneBridgeCommand()
		{
			Bridge::Command command;
			if (!bridge.TryConsume(command))
			{
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.Input] action=command_wake result=failed reason=bridge_empty\n");
				return;
			}
			// 每个独立 Command marker 只执行一条 scene-stamped 命令。
			restoreLatestScene = true;
			if (!command.shortcut) EnqueueCommandScene(command); // 快捷键只条件执行当前页，禁止重放旧页。
			CanvasCommand canvas;
			switch (command.type)
			{
			case Bridge::CommandType::SetWhiteboardTheme:
				canvas.type = CanvasCommandType::SetWhiteboardTheme;
				canvas.whiteboardTheme = command.whiteboardTheme;
				canvas.whiteboardGeneration = command.whiteboardGeneration;
				canvas.pageIndex = command.whiteboardPage;
				break;
			case Bridge::CommandType::Clear: canvas.type = CanvasCommandType::Clear; break;
			case Bridge::CommandType::Undo: canvas.type = CanvasCommandType::Undo; break;
			case Bridge::CommandType::Redo: canvas.type = CanvasCommandType::Redo; break;
			case Bridge::CommandType::ShortcutFence: canvas.type = CanvasCommandType::ShortcutFence; break;
			case Bridge::CommandType::NextPage: canvas.type = CanvasCommandType::NextPage; break;
			case Bridge::CommandType::PreviousPage: canvas.type = CanvasCommandType::PreviousPage; break;
			case Bridge::CommandType::PrepareExitAutoSave:
				canvas.type = CanvasCommandType::PrepareExitAutoSave; break;
			default: return;
			}
			canvas.shortcut = std::move(command.shortcut);
			if (!canvas.shortcut && canvas.type != CanvasCommandType::PrepareExitAutoSave)
			{
				// 场景前缀若准备失败，业务命令必须明确失败，不能落到别的文稿。
				canvas.requireSceneMatch = true;
				canvas.workspace = static_cast<std::uint8_t>(command.workspace);
				canvas.presentationTarget = std::move(command.presentationTarget);
			}
			window.EnqueueCanvasCommand(canvas);
		}

		void PumpPresentationCompletions()
		{
			const auto notFound = DrainPresentationCompletions(
				presentationAutoSave, pendingPresentationCompletion, window);
			if (startOptions.enableHiddenTestContactInjection)
				hiddenPresentationNotFound.fetch_add(notFound, std::memory_order_acq_rel);
		}

		void PumpDesktopCompletions()
		{
			DesktopPersistenceCompletion completion;
			while (autoSave.TryTakeCompletion(completion))
			{
				CanvasCommand command;
				command.type = CanvasCommandType::DesktopPersistenceCompleted;
				command.desktopPersistenceCompletion = std::make_shared<
					DesktopPersistenceCompletion>(std::move(completion));
				window.EnqueueCanvasCommand(std::move(command));
			}
		}

		static void ConsumeBridge(void* context, ControlWakeKind kind)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			// General 只做状态/完成收敛；Command 恰好对应一条 Bridge 命令。
			self->PumpDesktopCompletions();
			self->PumpPresentationCompletions();
			if (kind == ControlWakeKind::Command) self->PumpOneBridgeCommand();
			self->PumpBridgeState();
		}

		bool Start(HWND hwnd, HWND presentationHwnd,
			HostStyleCallbacks styleCallbacks, HostStartOptions options,
			HostRuntimeCallbacks runtimeCallbacks)
		{
			const auto failedCleanup = options.failedCleanup;
			if (running.load(std::memory_order_acquire) || drawingThread.joinable() ||
				attachedWindow.load(std::memory_order_acquire) ||
				attachedPresentationWindow.load(std::memory_order_acquire)) return false;
			LogDraw3BuildIdentity();
			LogDraw3WindowState("start-request", hwnd, presentationHwnd);
			BeginRuntimeMetricsRun(options);
			// 强制 DWM 已禁用，必须在重置 bridge 或附着外部 HWND 前拒绝。
			if (options.requiredPresentationMode == HostPresentationMode::DwmBlurBehind ||
				options.requiredPresentationMode == HostPresentationMode::DwmBlurBehind2 ||
				!hwnd || !presentationHwnd || !IsWindow(hwnd) || !IsWindow(presentationHwnd))
			{
				SealRuntimeMetricsRun(true);
				return false;
			}
			PrepareRuntimeMetrics(options);
			// 上一代 Stop 尾部可能留下普通唤醒；新 Bridge 不能消费旧 marker。
			input.ResetForNextRun();
			inputMode.store(HostInputMode::Disabled, std::memory_order_release);
			inputStopping.store(false, std::memory_order_release);
			bridge.Reset();
			firstFrameReady.store(false, std::memory_order_release);
			ResetRuntimeDiagnostics();
			{std::scoped_lock lock(eraserDiagnosticsMutex);eraserDiagnostics={};penDiagnostics={};}
			this->styleCallbacks = styleCallbacks;
			this->runtimeCallbacks = runtimeCallbacks;
			startOptions = options;
			hiddenTestContactInjectionEnabled = options.enableHiddenTestContactInjection;
			attachedWindow.store(hwnd, std::memory_order_release);
			attachedPresentationWindow.store(presentationHwnd, std::memory_order_release);
			ExternalWindowCallbacks windowCallbacks{};
			if (!window.AttachExternal(hwnd, windowCallbacks))
			{
				failedCleanup.BeginKnownFailure();
				attachedWindow.store(nullptr, std::memory_order_release);
				attachedPresentationWindow.store(nullptr, std::memory_order_release);
				hiddenTestContactInjectionEnabled = false;
				SealRuntimeMetricsRun(true);
				return false;
			}
			if (startOptions.startupMilestone)
				startOptions.startupMilestone(startOptions.startupContext,
					HostStartupStage::WindowAttached);
			LogDraw3WindowState("attached", hwnd, presentationHwnd);
			autoSave.CloseAndDrain();
			presentationAutoSave.CloseAndDrain();
			if (!options.autoSaveRoot.empty() && !autoSave.Start(
				options.autoSaveRoot, this, &WakeForPresentationPersistence))
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.AutoSave] action=start result=failed\n");
			if (!options.autoSaveRoot.empty() && !presentationAutoSave.Start(
				options.autoSaveRoot, this, &WakeForPresentationPersistence))
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.Presentation] action=start result=failed\n");
			input.EnableDiagnostics(options.enableHiddenTestContactInjection || options.enableRuntimeMetrics);
			if (options.enableRuntimeMetrics) runtimeMetrics.inputBaseline = input.DiagnosticsSnapshot();
			window.canvasContactDown = [] { Inkeys::UI::PageControl::RequestWhiteboardMenuDismiss(); };
			window.SetInputCoordinator(&input);
			window.SetSpeedEraserDisplayScale({});
			appliedDisplayClientBounds = {};
			penWidthContextPrepared = false;
			PublishDisplaySnapshot(Inkeys::Display::GetSnapshot());
			// 启动握手保证绘制线程先拥有独立 GPU 资源，再启用唯一 RTS producer。
			{
				std::scoped_lock lock(startupMutex);
				graphicsReady = false;
				inputDecision = false;
				inputSucceeded = false;
				startupCompleted = false;
				startupSucceeded = false;
			}
			hapticClicks.Start();
			running.store(true, std::memory_order_release);
			auto runDrawing = [this, failedCleanup](std::stop_token token)
			{
				// COM 的创建、调用和析构都留在此 owner；异常/首帧失败同样配对清理。
				PenHapticFeedback haptics(false);
				const bool hapticReady = haptics.Initialize();
				HapticDiagnostics::Log("host", "backend_initialized=%d run=%llu", hapticReady, static_cast<unsigned long long>(hapticClicks.Run()));
				bool initialized = false;
				bool graphicsInitialized = false;
					DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=begin hwnd=0x" << std::hex
						<< reinterpret_cast<UINT_PTR>(attachedWindow.load(std::memory_order_acquire))
						<< " selectionHwnd=0x"
						<< reinterpret_cast<UINT_PTR>(attachedPresentationWindow.load(std::memory_order_acquire))
						<< std::dec << std::endl;
				try
				{
					const HWND windowHandle = attachedWindow.load(std::memory_order_acquire);
					const HWND presentationWindowHandle =
						attachedPresentationWindow.load(std::memory_order_acquire);
					graphicsInitialized = windowHandle && InitializeGraphicsDevice(graphics,
						startOptions.enableHardwareAcceleration);
					DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=graphics result="
						<< (graphicsInitialized ? "ready" : "failed")
						<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(windowHandle)
						<< " selectionHwnd=0x" << reinterpret_cast<UINT_PTR>(presentationWindowHandle)
						<< std::dec << " driver=" << (graphics.driverType == D3D_DRIVER_TYPE_WARP ? "WARP" : "Hardware")
						<< " featureLevel=0x" << std::hex << static_cast<unsigned>(graphics.featureLevel)
						<< std::dec << std::endl;
					// 已知失败后先激活；普通设备初始化尚未返回时不设置冷启动上限。
					if (!graphicsInitialized) failedCleanup.BeginKnownFailure();
					if (graphicsInitialized && startOptions.startupMilestone)
						startOptions.startupMilestone(startOptions.startupContext,
							HostStartupStage::GraphicsReady);
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
						presentationOptions.failedCleanup = failedCleanup;
						presentationOptions.enableHardwareAcceleration = startOptions.enableHardwareAcceleration;
						presentationOptions.enableZhaoxinCompatibility = startOptions.enableZhaoxinCompatibility;
						graphicsInitialized = presentation.Initialize(windowHandle,
							presentationWindowHandle, graphics, renderer,
							static_cast<UINT>((std::max)(1, size.width)),
							static_cast<UINT>((std::max)(1, size.height)), presentationCallbacks,
							presentationOptions);
						DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=presenter result="
							<< (graphicsInitialized ? "ready" : "failed")
							<< " mode=" << TransparentPresentModeName(presentation.ActiveMode())
							<< " renderer=0x" << std::hex << reinterpret_cast<UINT_PTR>(&renderer)
							<< std::dec << " size=" << size.width << "x" << size.height << std::endl;
						if (!graphicsInitialized) failedCleanup.BeginKnownFailure();
						if (graphicsInitialized)
						{
							if (startOptions.startupMilestone)
								startOptions.startupMilestone(startOptions.startupContext,
									HostStartupStage::PresenterReady);
							presentationMode.store(ToHostMode(presentation.ActiveMode()),
								std::memory_order_release);
							window.SetGpuTransparentComposition(
								presentation.IsGpuTransparentComposition());
							CaptureRuntimeMetricsMetadata(true);
						}
					}

					// 输入准入前由绘制 owner 同步解析目标并消费产品样式，首笔无异步纠正。
					if (graphicsInitialized) PumpBridgeState();
					{
						std::scoped_lock lock(startupMutex);
						graphicsReady = graphicsInitialized;
					}
					startupCondition.notify_all();
					if (graphicsInitialized)
					{
						// 输入必须在图形资源准备好后才启用，避免 producer 先于 renderer 存活。
						bool inputApproved = false;
						bool inputDecisionValue = false;
						bool inputSucceededValue = false;
						{
							std::unique_lock lock(startupMutex);
							DiagnosticOutput::Line() << "[Draw3Diag][startup] gate=input-ready state=waiting" << std::endl;
							startupCondition.wait(lock, [this, &token]
								{ return inputDecision || token.stop_requested(); });
							inputDecisionValue = inputDecision;
							inputSucceededValue = inputSucceeded;
							inputApproved = inputDecision && inputSucceeded && !token.stop_requested();
						}
						DiagnosticOutput::Line() << "[Draw3Diag][startup] gate=input-ready result="
							<< (inputApproved ? "open" : "closed")
							<< " decision=" << (inputDecisionValue ? 1 : 0)
							<< " input=" << (inputSucceededValue ? 1 : 0)
							<< " stopped=" << (token.stop_requested() ? 1 : 0) << std::endl;
						if (inputApproved)
						{
							StrokeModelConfiguration configuration =
								CreateStrokeModelConfiguration(QueryCompatibleWindowDpi(windowHandle));
							configuration.interruptedStrokeReconnectEnabled =
								interruptedStrokeReconnectEnabled.load(std::memory_order_acquire);
							window.SetEraserDiagnosticsEnabled(startOptions.enableEraserDiagnostics ||
								startOptions.enableHiddenTestContactInjection);
							DrawingControllerRuntimeObserver observer{
								this, &ObservePresented, &ObserveResized,
								&ObserveCommand, &ObserveDocument,
								&ObserveCurrentPageContent, &ObserveStrokeCompleted,
								&ObserveWorkspace, &ConsumeBridge,
								&ObserveDesktopAutoSave,
								&ObserveDesktopLoad,
								&ObservePresentationSave,
								&ObservePresentationLoad,
								&ObserveDrawingActivity,
								&ObserveEraserDiagnostics,
								startOptions.enableHiddenTestContactInjection ? &ObservePenDiagnostics : nullptr
							};
							observer.historyAvailabilityChanged = &ObserveHistoryAvailability;
							observer.whiteboardChanged = &ObserveWhiteboard;
							observer.whiteboardExitSaved = &ObserveWhiteboardExit;
							observer.whiteboardRoot = startOptions.autoSaveRoot;
							observer.whiteboardDefaultTheme = startOptions.whiteboardDefaultTheme;
							observer.whiteboardThemeEdited = &ObserveWhiteboardThemeEdited;
							observer.acceptedStrokeBegan = &ObserveAcceptedStroke;
							observer.acceptedStrokeSceneChanged = &ObserveAcceptedStrokeScene;
							observer.shortcutContextChanged = &ObserveShortcutContext;
							observer.shortcutCommandAllowed = &AllowShortcutCommand;
							observer.shortcutCompleted = &CompleteShortcut;
							observer.recoveryChanged = &ObserveRecovery;
							observer.recoveryRetryAllowed = &RecoveryRetryAllowed;
							observer.legacyWindowRebuildRequested = &RequestProductLegacyRecovery;
							drawing = std::make_unique<DrawingController>(input, window, renderer,
								presentation, configuration, observer, runtimeMetrics.session.get(), &haptics, &hapticClicks);
							// 创建前已消费过 Bridge revision，首帧前显式补齐新 Controller 的光标偏好。
							drawing->SetCursorSettings(bridge.Snapshot().cursorSettings);
							DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=controller result=ready renderer=0x"
								<< std::hex << reinterpret_cast<UINT_PTR>(&renderer)
								<< std::dec << std::endl;
							if (startOptions.startupMilestone)
								startOptions.startupMilestone(startOptions.startupContext,
									HostStartupStage::ControllerReady);
							// 首帧清屏和所有 Renderer 访问均发生在 Draw3 绘制线程。
							PumpBridgeState();
							presentation.SetOutputTarget(window.SelectionMode()
								? TransparentOutputTarget::SelectionUlw
								: TransparentOutputTarget::PrimaryDrawpad);
							DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=first-frame attempt target="
								<< HostOutputTargetName(ToHostOutputTarget(presentation.RequestedOutputTarget()))
								<< " revision=" << presentation.RequestedOutputRevision()
								<< " size=" << committedWidth.load(std::memory_order_acquire)
								<< "x" << committedHeight.load(std::memory_order_acquire) << std::endl;
							drawing->ClearCanvas();
							if (runtimeMetrics.session)
							{
								// Controller 旁挂失败会关闭真实计数；不把空 Session 导出成正常 run。
								const auto metrics = runtimeMetrics.session->Snapshot();
								if (metrics.frameSerial == 0 || metrics.presentAttempts == 0)
									runtimeMetrics.unavailable.store(true, std::memory_order_release);
							}
							initialized = lastPresentSucceeded.load(std::memory_order_acquire);
							DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=first-frame result="
								<< (initialized ? "committed" : "failed")
								<< " target=" << HostOutputTargetName(
									ToHostOutputTarget(presentation.RequestedOutputTarget()))
								<< " revision=" << presentation.RequestedOutputRevision()
								<< " size=" << committedWidth.load(std::memory_order_acquire)
								<< "x" << committedHeight.load(std::memory_order_acquire) << std::endl;
							LogDraw3WindowState("first-frame", windowHandle, presentationWindowHandle);
							if (!initialized) failedCleanup.BeginKnownFailure();
							firstFrameReady.store(initialized, std::memory_order_release);
							if (initialized && startOptions.startupMilestone)
								startOptions.startupMilestone(startOptions.startupContext,
									HostStartupStage::FirstFrameCommitted);
						}
					}
				}
				catch (const std::exception& exception)
				{
					failedCleanup.BeginKnownFailure();
					// 初始化失败也必须完成握手；保留异常原因供隐藏测试和现场诊断。
					DiagnosticOutput::Printf(DiagnosticOutput::Stream::Error, "[Draw3] startup failed: %s\n", exception.what());
					initialized = false;
				}
				catch (...)
				{
					failedCleanup.BeginKnownFailure();
					DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3] startup failed: unknown exception\n");
					initialized = false;
				}
				// Signal 由此 owner 按值持有，覆盖失败握手与随后全部 GPU 释放。
				if (!initialized) failedCleanup.BeginKnownFailure();
				if (!initialized && runtimeMetrics.enabled.load(std::memory_order_acquire))
					runtimeMetrics.startupFailed.store(true, std::memory_order_release);
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
						runtimeMetrics.runFailed.store(true, std::memory_order_release);
						// 绘制循环异常必须收敛到宿主停止，不能穿出 jthread 触发 terminate。
						DiagnosticOutput::Printf(DiagnosticOutput::Stream::Error, "[Draw3] drawing loop stopped: %s\n",
							exception.what());
						window.RequestExit();
					}
					catch (...)
					{
						runtimeMetrics.runFailed.store(true, std::memory_order_release);
						DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3] drawing loop stopped: unknown exception\n");
						window.RequestExit();
					}
				}
				// Run 正常、异常或 stop 返回时都补齐活动结束，再释放 controller。
				EndDrawingActivity();
				CaptureRuntimeMetricsMetadata(true);
				// GPU 资源在拥有它们的绘制线程释放，避免跨线程访问 Renderer。
				if (drawing) drawing.reset();
				hapticClicks.Stop();
				haptics.Shutdown();
				presentation.Shutdown();
				renderer.ReleaseResources();
				graphics = {};
				running.store(false, std::memory_order_release);
				shortcuts.Stop();
				contentCondition.notify_all();
				exitAutoSaveCondition.notify_all();
				runtimeRevision.NotifyAll();
			};
			try
			{
				drawingThread = std::jthread(std::move(runDrawing));
				runtimeMetrics.ownerCreated = true;
			}
			catch (...)
			{
				// owner 创建失败也先监督，再排空已建立的 worker 并解除外部窗口附着。
				failedCleanup.BeginKnownFailure();
				running.store(false, std::memory_order_release);
				shortcuts.Stop();
				autoSave.CloseAndDrain();
				presentationAutoSave.CloseAndDrain();
				EndDrawingActivity();
				window.SetInputCoordinator(nullptr);
				window.DetachExternal();
				pendingPresentationCompletion.reset();
				attachedWindow.store(nullptr, std::memory_order_release);
				attachedPresentationWindow.store(nullptr, std::memory_order_release);
				hiddenTestContactInjectionEnabled = false;
				firstFrameReady.store(false, std::memory_order_release);
				runtimeCallbacks = {};
				SealRuntimeMetricsRun(true);
				return false;
			}

		std::unique_lock lock(startupMutex);
		startupCondition.wait(lock, [this] { return graphicsReady || startupCompleted; });
		if (!graphicsReady)
		{
			failedCleanup.BeginKnownFailure();
			lock.unlock();
			if (drawingThread.joinable()) drawingThread.join();
			autoSave.CloseAndDrain();
			presentationAutoSave.CloseAndDrain();
			EndDrawingActivity();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			pendingPresentationCompletion.reset();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			runtimeCallbacks = {};
			SealRuntimeMetricsRun(true);
			return false;
		}

		lock.unlock();
		bool inputInitialized = false;
		try
		{
			const bool stylusInitialized = stylus.Initialize(hwnd, input, &window, failedCleanup);
			HostInputMode selected = HostInputMode::Disabled;
			if (stylusInitialized)
			{
				selected = stylus.UsesBasicPacketDescription()
					? HostInputMode::RtsBasicXY : HostInputMode::RtsExtended;
				stylus.EnableContactPublication();
				inputInitialized = true;
			}
			else
			{
				// Initialize false 只表示 RTS 已完整停产；窗口 owner 确认后再打开输入门。
				if (!window.SetMouseCompatibilityEnabled(true))
				{
					failedCleanup.BeginKnownFailure();
					if (!window.SetMouseCompatibilityEnabled(false))
						failedCleanup.FailUnprovenProducerStop();
				}
				else
				{
					selected = HostInputMode::Win32Mouse;
					inputInitialized = true;
					DiagnosticOutput::Text(DiagnosticOutput::Stream::Error,
						"[Draw3] RTS unavailable; Win32 mouse compatibility input enabled\n");
				}
			}
			inputMode.store(selected, std::memory_order_release);
			const char* modeName = selected == HostInputMode::RtsExtended ? "RtsExtended" :
				selected == HostInputMode::RtsBasicXY ? "RtsBasicXY" :
				selected == HostInputMode::Win32Mouse ? "Win32Mouse" : "Disabled";
			DiagnosticOutput::Line() << "[Draw3] Input initialization result="
				<< (inputInitialized ? "ready" : "failed") << " input_mode=" << modeName << std::endl;
			if (!inputInitialized) failedCleanup.BeginKnownFailure();
			if (inputInitialized && startOptions.startupMilestone)
				startOptions.startupMilestone(startOptions.startupContext,
					HostStartupStage::InputReady);
		}
		catch (...)
		{
			failedCleanup.BeginKnownFailure();
			// 意外异常不降级；仍完成握手，避免绘制 owner 永久等待。
			inputInitialized = false;
		}
		{
			std::scoped_lock startupLock(startupMutex);
			inputSucceeded = inputInitialized;
			inputDecision = true;
		}
		startupCondition.notify_all();

		lock.lock();
		startupCondition.wait(lock, [this] { return startupCompleted; });
		const bool startupSucceededValue = startupSucceeded;
		lock.unlock();
		if (!startupSucceededValue)
		{
			failedCleanup.BeginKnownFailure();
			// 失败时先确认最终输入 producer 停止，再让绘制线程退出并释放 GPU 资源。
			StopInputProducer();
			window.RequestExit();
			input.PublishControlWake();
			if (drawingThread.joinable()) drawingThread.request_stop();
			startupCondition.notify_all();
			if (drawingThread.joinable()) drawingThread.join();
			autoSave.CloseAndDrain();
			presentationAutoSave.CloseAndDrain();
			EndDrawingActivity();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			pendingPresentationCompletion.reset();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			runtimeCallbacks = {};
			SealRuntimeMetricsRun(true);
			return false;
		}
		displaySubscription = Inkeys::Display::Subscribe(
			[this](Inkeys::Display::SnapshotPtr snapshot)
			{
				// 通知线程只发布快照并唤醒，窗口信息由绘制线程低频解析。
				PublishDisplaySnapshot(std::move(snapshot));
			});
		// Subscribe 仍可能抛出；只有 caller 完整 Start 成功后才清失败标记。
		if (runtimeMetrics.enabled.load(std::memory_order_acquire))
			runtimeMetrics.startupFailed.store(false, std::memory_order_release);
		return true;
		}

		void CloseInputAdmission() noexcept
		{
			window.CloseMouseInputAdmission();
			{
				std::scoped_lock lock(presentationTargetMutex);
				inputStopping.store(true, std::memory_order_release);
				workspaceTransitionEpoch = 0;
				workspaceTransitionPending = false;
				RefreshPresentationInputGateLocked();
			}
		}

		void StopInputProducer() noexcept
		{
			CloseInputAdmission();
			if (!window.SetMouseCompatibilityEnabled(false))
			{
				startOptions.failedCleanup.BeginKnownFailure();
				startOptions.failedCleanup.FailUnprovenProducerStop();
			}
			stylus.Shutdown();
			inputMode.store(HostInputMode::Disabled, std::memory_order_release);
		}

		void Stop() noexcept
		{
			hapticClicks.Stop();
			displaySubscription.Reset(); // 等待回调退出后再拆除输入与窗口。
			if (!attachedWindow.load(std::memory_order_acquire) &&
				!attachedPresentationWindow.load(std::memory_order_acquire) &&
				!running.load(std::memory_order_acquire) && !drawingThread.joinable())
			{
				autoSave.CloseAndDrain();
				presentationAutoSave.CloseAndDrain();
				EndDrawingActivity();
				runtimeCallbacks = {};
				SealRuntimeMetricsRun();
				return;
			}
			// 先关闭 contact 准入，仍保留业务命令通道供下面发布最终保存屏障。
			CloseInputAdmission();
			{
				std::scoped_lock lock(exitAutoSaveMutex);
				exitAutoSavePrepared = false;
			}
			// 关闭产品命令生产端，并把退出保存屏障排在所有已接受命令之后。
			bool exitBarrierQueued = false;
			{
				std::scoped_lock lock(commandPublishMutex);
				if (input.TryReserveCommandWake())
				{
					exitBarrierQueued = bridge.StopWithFinalCommand(
						Bridge::CommandType::PrepareExitAutoSave);
					if (!exitBarrierQueued) input.CancelReservedCommandWake();
				}
				else bridge.Stop(); // 预约失败不可接受一个无法送达的最终屏障。
			}
			// 停止选定 producer 并确认 owner drain 后，才发布最终保存屏障。
			StopInputProducer();
			if (exitBarrierQueued) input.PublishReservedCommandWake();
			else input.PublishControlWake();
			if (exitBarrierQueued)
			{
				std::unique_lock lock(exitAutoSaveMutex);
				exitAutoSaveCondition.wait(lock, [this]
					{
						return exitAutoSavePrepared ||
							!running.load(std::memory_order_acquire);
					});
				if (exitAutoSavePrepared && !whiteboardExitDurable.load(std::memory_order_acquire))
					DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.AutoSave] action=exit_barrier result=failed reason=whiteboard_not_durable\n");
				if (!exitAutoSavePrepared)
					DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.AutoSave] action=exit_barrier result=failed reason=controller_stopped\n");
			}
			else
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.AutoSave] action=exit_barrier result=failed reason=bridge_closed\n");
			// worker 无超时排空后才允许绘制线程销毁 Document/Controller。
			autoSave.CloseAndDrain();
			presentationAutoSave.CloseAndDrain();
			window.RequestExit();
			input.PublishControlWake();
			if (drawingThread.joinable()) drawingThread.request_stop();
			startupCondition.notify_all();
			if (drawingThread.joinable()) drawingThread.join();
			EndDrawingActivity();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			pendingPresentationCompletion.reset();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			running.store(false, std::memory_order_release);
			shortcuts.Stop();
			contentCondition.notify_all();
			runtimeRevision.NotifyAll();
			runtimeCallbacks = {};
			SealRuntimeMetricsRun();
		}
	};

	Host::Host() : impl_(std::make_unique<Impl>()) {}
	Host::~Host() { Stop(); }
	bool Host::Start(HWND drawpad, HWND drawpadPresentation,
		HostStyleCallbacks callbacks, HostStartOptions options,
		HostRuntimeCallbacks runtimeCallbacks)
	{
		return impl_->Start(drawpad, drawpadPresentation, callbacks, options,
			runtimeCallbacks);
	}
	void Host::Stop() noexcept { impl_->Stop(); }
	bool Host::WriteRuntimeMetrics(const wchar_t* absoluteOutputPath) const noexcept
	{
		return impl_->WriteRuntimeMetrics(absoluteOutputPath);
	}
	bool Host::Running() const noexcept { return impl_->running.load(std::memory_order_acquire); }
	std::optional<HostHiddenPersistenceSnapshot> Host::HiddenPersistenceSnapshot() const noexcept
	{
		// startOptions 在当前 generation 不变，Stop 不改写此字段；禁止并行下一 Start。
		if (!impl_->startOptions.enableHiddenTestContactInjection) return std::nullopt;
		const auto desktop = impl_->autoSave.Diagnostics();
		const auto presentation = impl_->presentationAutoSave.Diagnostics();
		return HostHiddenPersistenceSnapshot{ desktop.accepted, desktop.committed,
			desktop.failed, desktop.pending, presentation.accepted, presentation.committed,
			presentation.loaded, presentation.failed,
			impl_->hiddenPresentationNotFound.load(std::memory_order_acquire) };
	}

	bool Host::FirstFrameReady() const noexcept
	{
		return impl_->firstFrameReady.load(std::memory_order_acquire);
	}
	HostRuntimeSnapshot Host::RuntimeSnapshot() const noexcept
	{
		HostRuntimeSnapshot snapshot;
		// 等待基准先取：后续字段读取期间的新发布必须使 WaitForChange 立即返回。
		snapshot.runtimeRevision = impl_->runtimeRevision.Revision();
		{std::scoped_lock lock(impl_->eraserDiagnosticsMutex);snapshot.eraser=impl_->eraserDiagnostics;snapshot.pen=impl_->penDiagnostics;}
		snapshot.touchContactAreaAssistanceEnabled=impl_->window.TouchContactAreaAssistance();
		snapshot.runtimeMetrics = impl_->CopyRuntimeMetricsSnapshot();
		snapshot.running = impl_->running.load(std::memory_order_acquire);
		snapshot.firstFrameReady = impl_->firstFrameReady.load(std::memory_order_acquire);
		snapshot.lastPresentSucceeded =
			impl_->lastPresentSucceeded.load(std::memory_order_acquire);
		snapshot.presentationMode = impl_->presentationMode.load(std::memory_order_acquire);
		snapshot.inputMode = impl_->inputMode.load(std::memory_order_acquire);
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
		{
			std::scoped_lock lock(impl_->whiteboardMutex);
			snapshot.whiteboardTheme = impl_->whiteboardTheme;
			snapshot.whiteboardGeneration = impl_->whiteboardGeneration;
			snapshot.whiteboardLoadPending = impl_->whiteboardLoadPending;
			snapshot.whiteboardUnsaved = impl_->whiteboardUnsaved;
		}
		snapshot.canUndo = impl_->canUndo.load(std::memory_order_acquire);
		snapshot.canRedo = impl_->canRedo.load(std::memory_order_acquire);
		snapshot.completedStrokeKind =
			impl_->completedStrokeKind.load(std::memory_order_acquire);
		snapshot.selectionMode = impl_->selectionMode.load(std::memory_order_acquire);
		// 页码、主题、代次和加载闸门取同一个发布快照。
		{
			std::scoped_lock lock(impl_->whiteboardMutex);
			snapshot.workspace = impl_->workspace.load(std::memory_order_acquire);
			if (snapshot.workspace == Bridge::Workspace::Whiteboard)
			{
				snapshot.currentPageIndex = impl_->whiteboardPageIndex;
				snapshot.pageCount = impl_->whiteboardPageCount;
				snapshot.whiteboardTheme = impl_->whiteboardTheme;
				snapshot.whiteboardGeneration = impl_->whiteboardGeneration;
				snapshot.whiteboardLoadPending = impl_->whiteboardLoadPending;
				snapshot.whiteboardUnsaved = impl_->whiteboardUnsaved;
				snapshot.canUndo = impl_->whiteboardCanUndo;
				snapshot.canRedo = impl_->whiteboardCanRedo;
				snapshot.currentPageHasContent = impl_->whiteboardHasContent;
				snapshot.contentRevision = impl_->whiteboardContentRevision;
			}
		}
		{
			std::scoped_lock lock(impl_->presentationTargetMutex);
			snapshot.hostGeneration = impl_->acceptedStrokes.HostGeneration();
			snapshot.appliedProductRevision = impl_->appliedProductRevision;
			snapshot.appliedTool = impl_->appliedTool;
			snapshot.presentationReady = impl_->readyPresentationTarget;
			snapshot.presentationUiReady = impl_->uiReadyPresentationTarget;
			snapshot.presentationSuspended = impl_->suspendedPresentationTarget;
			snapshot.commandScenePending = impl_->commandScenePending;
			snapshot.consumerFatal = impl_->input.ConsumerFatal();
			snapshot.recovery = impl_->recovery;
			snapshot.presentationInputReady = !impl_->input.AdmissionBlocked();
		}
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
		snapshot.lastDirtyRect.left = impl_->lastDirtyLeft.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.top = impl_->lastDirtyTop.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.right = impl_->lastDirtyRight.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.bottom = impl_->lastDirtyBottom.load(std::memory_order_relaxed);
		return snapshot;
	}

	bool Host::IsAcceptedStrokeCurrent(const Bridge::AcceptedStrokeBegin& accepted) const noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto desired = impl_->bridge.Snapshot();
		Detail::HostAcceptedStrokeContext current;
		current.running = impl_->running.load(std::memory_order_acquire);
		current.desiredWorkspace = desired.workspace;
		if (desired.presentationTarget)
			current.desiredPresentation = Bridge::ReadyIdentityFor(*desired.presentationTarget);
		current.commandScenePending = impl_->commandScenePending;
		current.admissionBlocked = impl_->input.AdmissionBlocked();
		return impl_->acceptedStrokes.IsCurrent(accepted, current) &&
			impl_->running.load(std::memory_order_acquire);
	}

	bool Host::WaitForRuntimeRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) const noexcept
	{
		return impl_->runtimeRevision.WaitForChange(
			revision, timeoutMilliseconds, impl_->running);
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
		const auto phase = static_cast<HiddenTestContactPhase>(static_cast<std::uint32_t>(phaseValue)&0xffu);
		const auto deviceType=(phaseValue & kHiddenTestRightMouseFlag)?InputDeviceType::MouseRight:
			(phaseValue & kHiddenTestMouseFlag) ? InputDeviceType::MouseLeft :
			(phaseValue & kHiddenTestTouchFlag) ? InputDeviceType::Touch : InputDeviceType::Pen;
		ContactSnapshot snapshot{};
		{
			std::scoped_lock lock(impl_->displayMutex);
			const auto& area=impl_->hiddenContactArea;
			snapshot.rawContactSize={area.rawWidth,area.rawHeight};
			snapshot.contactSize={area.widthPx,area.heightPx};snapshot.contactAreaUnits=area.units;
		}
		snapshot.position.x = static_cast<float>(static_cast<short>(LOWORD(position)));
		snapshot.position.y = static_cast<float>(static_cast<short>(HIWORD(position)));
		snapshot.pressure = phase == HiddenTestContactPhase::Down ? 0.8f : 0.7f;
		LARGE_INTEGER qpc = {};
		QueryPerformanceCounter(&qpc);
		snapshot.qpc = qpc.QuadPart;
		if ((phaseValue & kHiddenTestNoPressureFlag) != 0) snapshot.pressure = -1.0f;
		if (phase == HiddenTestContactPhase::Up && (phaseValue & kHiddenTestDelayedUpFlag) != 0)
		{
			// 隐藏验收在最后 Move 后等待 40ms，模拟 Up 包迟到 30ms 而非篡改模型时间。
			LARGE_INTEGER frequency = {};
			QueryPerformanceFrequency(&frequency);
			snapshot.qpc -= frequency.QuadPart * 30 / 1000;
		}
		snapshot.isInvertedCursor=(phaseValue & kHiddenTestPenTailFlag)!=0;
		snapshot.source.kind=(deviceType==InputDeviceType::MouseLeft || deviceType==InputDeviceType::MouseRight) ? SpeedEraser::SourceKind::Mouse :
			deviceType==InputDeviceType::Touch ? SpeedEraser::SourceKind::Touch :
			(phaseValue & kHiddenTestIntegratedPenFlag) ? SpeedEraser::SourceKind::IntegratedPen :
			(phaseValue & kHiddenTestExternalPenFlag) ? SpeedEraser::SourceKind::ExternalPen : SpeedEraser::SourceKind::Unknown;
		snapshot.source.recognition=SpeedEraser::SourceRecognition::RtsCapabilities;
		snapshot.source.contextId=0xD303u;snapshot.source.cursorId=0xD304u+static_cast<uint32_t>(SpeedEraser::EntryForInput(static_cast<uint32_t>(deviceType),snapshot.isInvertedCursor));snapshot.source.generation=1;
		const auto scale=impl_->window.SpeedEraserDisplayScaleSnapshot();
		snapshot.source.mappedMonitor=scale.monitor;
		snapshot.source.mappedLeft=scale.desktopLeft;snapshot.source.mappedTop=scale.desktopTop;
		snapshot.source.mappedWidth=scale.pixelWidth;snapshot.source.mappedHeight=scale.pixelHeight;

		// 隐藏测试走同一个无锁 contact mailbox，不触碰 Renderer 或 RTS 内部状态。
		constexpr std::uint32_t tabletContextId = 0xD303u;
		const std::uint32_t contactId = snapshot.source.cursorId;
		bool published = false;
		switch (phase)
		{
		case HiddenTestContactPhase::Down:
			if(deviceType==InputDeviceType::Touch)impl_->window.NotifyTouchContactBegin();
			snapshot.phase = ContactPhase::Down;
			published = impl_->input.PublishDown(tabletContextId, contactId,
				deviceType, snapshot);
			break;
		case HiddenTestContactPhase::Move:
			snapshot.phase = ContactPhase::Move;
			published = impl_->input.PublishMove(tabletContextId, contactId, snapshot);
			break;
		case HiddenTestContactPhase::Up:
			snapshot.phase = ContactPhase::Up;
			published = impl_->input.PublishUp(tabletContextId, contactId, snapshot);
			break;
		case HiddenTestContactPhase::Hover:
			published = true;
			break;
		case HiddenTestContactPhase::Cancelled:
			snapshot.phase = ContactPhase::Cancelled;
			published = impl_->input.PublishCancelled(tabletContextId, contactId, snapshot);
			break;
		default:
			return false;
		}

		// 隐藏注入复用真实 RTS 的 Touch 模态通知，不能让旧 Mouse Hover 掩盖 Touch 光标。
		if(deviceType==InputDeviceType::Touch &&
			((phase==HiddenTestContactPhase::Down && !published) ||
			 (published && (phase==HiddenTestContactPhase::Up || phase==HiddenTestContactPhase::Cancelled))))
			impl_->window.NotifyTouchContactEnd();

		if(published && (deviceType==InputDeviceType::MouseLeft || deviceType==InputDeviceType::MouseRight || deviceType==InputDeviceType::Pen))
		{
			DrawingCursorSample cursor;
			cursor.x=snapshot.position.x;cursor.y=snapshot.position.y;cursor.qpc=snapshot.qpc;
			cursor.valid=phase!=HiddenTestContactPhase::Cancelled;
			cursor.inContact=phase==HiddenTestContactPhase::Down || phase==HiddenTestContactPhase::Move;
			cursor.source=snapshot.source;cursor.inverted=snapshot.isInvertedCursor;
			if(deviceType==InputDeviceType::MouseLeft || deviceType==InputDeviceType::MouseRight)impl_->window.PublishHiddenTestMouseCursor(cursor);
			else impl_->window.PublishPenCursorSample(cursor);
		}
		if (published) (void)impl_->input.PublishControlWake();
		return published;
	}

	void Host::SetInterruptedStrokeReconnectEnabled(bool enabled) noexcept
	{
		if (impl_->interruptedStrokeReconnectEnabled.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		// Start/Stop 不清除此期望值；空闲时也唤醒，关闭后及时正常提交等待接续的笔画。
		(void)impl_->input.PublishControlWake();
	}

	void Host::SetEraserDevelopmentOptions(const SpeedEraser::DevelopmentOptions& options)
	{
		{
			std::scoped_lock lock(impl_->displayMutex);
			if(impl_->eraserDevelopment==options)return;
			impl_->eraserDevelopment=options;
		}
		if(impl_->touchAreaTraceEnabled.exchange(options.touchAreaTrace,std::memory_order_relaxed)!=options.touchAreaTrace)
			impl_->touchAreaTraceRevision.fetch_add(1,std::memory_order_relaxed);
		impl_->window.SetEraserDiagnosticsEnabled(options.diagnostics || options.touchAreaTrace ||
			impl_->hiddenTestContactInjectionEnabled || impl_->startOptions.enableEraserDiagnostics);
		impl_->displayScaleDirty.store(true,std::memory_order_release);
		(void)impl_->input.PublishControlWake();
	}
	SpeedEraser::DevelopmentOptions Host::EraserDevelopmentOptions() const
	{
		std::scoped_lock lock(impl_->displayMutex);
		return impl_->eraserDevelopment;
	}
	void Host::SetHiddenTestContactArea(const SpeedEraser::ContactAreaSample& sample)
	{
		if(!impl_->hiddenTestContactInjectionEnabled)return;
		std::scoped_lock lock(impl_->displayMutex);impl_->hiddenContactArea=sample;
	}

	SpeedEraser::DisplayScale Host::EraserDisplayScaleSnapshot() const
	{
		return impl_->window.SpeedEraserDisplayScaleSnapshot();
	}

	LRESULT Host::ForwardMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == kDraw3HiddenTestContactMessage &&
			impl_->hiddenTestContactInjectionEnabled)
			return PublishHiddenTestContact(wParam, lParam) ? 0 : -1;
		if (window == impl_->attachedWindow.load(std::memory_order_acquire) &&
			(message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED || message == WM_DISPLAYCHANGE))
		{
			impl_->displayScaleDirty.store(true, std::memory_order_release);
			(void)impl_->input.PublishControlWake();
		}
		return impl_->window.HandleExternalMessage(window, message, wParam, lParam);
	}
	bool Host::RetryCanvasRecovery(const CanvasRecoveryState& expected,
		const std::function<bool()>& stillDesired) noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto desired = impl_->bridge.Snapshot();
		if ((stillDesired && !stillDesired()) || !CanvasRecoveryRetryMatches(expected, impl_->recovery, desired, Running(),
			impl_->inputStopping.load(std::memory_order_acquire), impl_->input.ConsumerFatal())) return false;
		CanvasCommand command;
		command.type = desired.workspace == Bridge::Workspace::Presentation && desired.presentationTarget
			? CanvasCommandType::SetPresentationTarget : CanvasCommandType::SetWorkspace;
		command.workspace = static_cast<std::uint8_t>(desired.workspace);
		command.presentationTarget = desired.presentationTarget;
		command.sceneClosedAdmission = true;
		command.explicitRecoveryRetry = true;
		command.recoveryRevision = expected.revision;
		try { impl_->window.EnqueueCanvasCommand(std::move(command)); }
		catch (...) { return false; }
		// 当前恢复状态已经关闸；仅成功呈现能解除，队列中的旧 token 不额外制造场景债务。
		impl_->PublishRuntimeRevision();
		return true;
	}

	void Host::SetActivationAllowed(bool enabled) noexcept
	{
		impl_->window.SetActivationAllowed(enabled);
	}
	Haptics::Context Host::HapticContext() const noexcept
	{
		return Haptics::ButtonContext(Running() ? impl_->hapticClicks.Run() : 0);
	}
	void Host::SetHapticPreferences(bool drawingEnabled, bool buttonsEnabled) noexcept
	{
		impl_->drawingHapticsEnabled.store(drawingEnabled, std::memory_order_release);
		impl_->hapticClicks.SetEnabled(buttonsEnabled);
		// 空闲或悬停预设也必须收敛，COM 的停止和恢复仍由绘制 owner 执行。
		(void)impl_->input.PublishControlWake();
	}
	bool Host::PublishHapticClick(const Haptics::Click& source,
		HostHapticClickDiagnostics* diagnostics) noexcept
	{
		const auto context = HapticContext();
		const auto now = GetTickCount64();
		if (diagnostics) diagnostics->context = context;
		if (!impl_->hapticClicks.Publish(source, context, now,
			diagnostics ? &diagnostics->rejection : nullptr)) return false;
		(void)impl_->input.PublishControlWake();
		return true;
	}
	Bridge::StateBridge& Host::ProductBridge() noexcept { return impl_->bridge; }
	void Host::PublishState(const Bridge::ProductState& state) noexcept
	{
		{
			std::scoped_lock lock(impl_->presentationTargetMutex);
			impl_->bridge.PublishState(state);
			impl_->RefreshPresentationInputGateLocked();
		}
		if (impl_->attachedWindow.load(std::memory_order_acquire))
			(void)impl_->input.PublishControlWake();
	}
	bool Host::SetWorkspaceTransitionPending(std::uint64_t epoch, bool pending,
		std::uint64_t expectedHostGeneration) noexcept
	{
		if (!epoch || !expectedHostGeneration) return false;
		{
			std::scoped_lock lock(impl_->presentationTargetMutex);
			if (!Running() || impl_->inputStopping.load(std::memory_order_acquire) ||
				expectedHostGeneration != impl_->acceptedStrokes.HostGeneration() ||
				epoch < impl_->workspaceTransitionEpoch ||
				(!pending && epoch != impl_->workspaceTransitionEpoch) ||
				(pending && epoch == impl_->workspaceTransitionEpoch && !impl_->workspaceTransitionPending))
				return false;
			if (epoch == impl_->workspaceTransitionEpoch && pending == impl_->workspaceTransitionPending)
				return true;
			// 新意图先同步关闸；Host 重启和同 run 的旧版本都不能覆盖当前原因。
			impl_->workspaceTransitionEpoch = epoch;
			impl_->workspaceTransitionPending = pending;
			impl_->RefreshPresentationInputGateLocked();
		}
		// 准入只拦新 Down，目标应用、实际帧和终态仍由原 owner 主动推进。
		(void)impl_->input.PublishControlWake();
		impl_->PublishRuntimeRevision();
		return true;
	}

	int RunHostWorkspaceTransitionInputProductionProbe() noexcept
	{
		int failures = 0;
		const auto check = [&](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3WorkspaceTransition] FAIL: %s\n", name);
		};
		try
		{
			Host host;
			auto& state = *host.impl_;
			check(!host.SetWorkspaceTransitionPending(1, true, 1), "stopped Host refuses transition publication");
			// 只建立准入所需运行前提；使用真实 Impl/Coordinator，不 Start、附着 HWND 或启动 owner。
			state.input.ResetForNextRun();
			state.bridge.Reset();
			state.ResetRuntimeDiagnostics();
			state.startOptions.requirePresentationUiReady = true;
			state.running.store(true, std::memory_order_release);
			struct RestoreStopped
			{
				std::atomic_bool& running;
				~RestoreStopped() { running.store(false, std::memory_order_release); }
			} restore{ state.running };
			state.input.EnableDiagnostics(true);
			const auto firstRun = host.RuntimeSnapshot();
			Bridge::ProductState preferences;
			preferences.tool = Bridge::Tool::HardPen; preferences.selectionMode = false;
			host.PublishState(preferences);
			const auto preferenceRevision = state.bridge.Snapshot().revision;
			check(firstRun.hostGeneration != 0 && firstRun.appliedProductRevision == 0 &&
				host.RuntimeSnapshot().appliedProductRevision == 0,
				"Bridge publication cannot forge an applied tool acknowledgement");
			state.PumpBridgeState();
			const auto applied = host.RuntimeSnapshot();
			check(applied.hostGeneration == firstRun.hostGeneration &&
				applied.appliedProductRevision == preferenceRevision && applied.appliedTool == Bridge::Tool::HardPen &&
				applied.runtimeRevision > firstRun.runtimeRevision &&
				state.window.ActiveTool() == DrawingTool::HardPen && !state.window.SelectionMode(),
				"actual owner preference application publishes its exact tool and ProductState revision");
			int controlWakes = 0;
			const auto takeDown = [&]() -> ContactHandle
			{
				ContactRecord* record = nullptr;
				while (state.input.TryDequeue(record))
				{
					if (record) return { record, record->Generation() };
					++controlWakes;
					state.input.AcknowledgeControlWake();
				}
				return {};
			};
			ContactSnapshot sample;
			sample.position = { 1.0f, 1.0f }; sample.qpc = 1;
			check(state.input.PublishDown(0xF501, 1, InputDeviceType::MouseLeft, sample),
				"source contact enters before the transition");
			const auto source = takeDown();
			const auto beforeTransitionWake = state.input.CaptureWakeGeneration();
			const auto beforeTransitionRevision = state.runtimeRevision.Revision();
			check(state.input.ContactAdmitted(source) && host.SetWorkspaceTransitionPending(1, true, firstRun.hostGeneration) &&
				state.input.CaptureWakeGeneration() != beforeTransitionWake &&
				state.runtimeRevision.Revision() > beforeTransitionRevision &&
				state.input.AdmissionBlocked() && !state.input.ContactAdmitted(source),
				"new pending synchronously closes real admission and invalidates earlier queued Down");
			check(state.input.PublishMove(0xF501, 1, sample) && state.input.PublishUp(0xF501, 1, sample),
				"existing producer Move and physical terminal continue while pending");
			ContactSnapshot terminal;
			check(state.input.TryReadSnapshot(source, terminal) && terminal.phase == ContactPhase::Up,
				"pending keeps the source contact terminal readable for drain");
			state.input.Recycle(source);
			check(state.input.PublishDown(0xF501, 2, InputDeviceType::MouseRight, sample),
				"producer may queue a Down while admission is closed");
			const auto duringPending = takeDown();
			check(duringPending && controlWakes != 0 && !state.input.ContactAdmitted(duringPending),
				"control wake continues and transition Down is not admitted");
			Bridge::PresentationTarget target;
			target.key.bytes[0] = 0xF5;
			target.sourceIdentity = "path:c:\\tests\\transition.pptx";
			target.totalPages = 1; target.bindingRevision = 1; target.sessionRevision = 1;
			check(state.bridge.PublishPresentationTarget(target).has_value(), "pending permits target publication");
			const auto desired = state.bridge.Snapshot();
			if (!desired.presentationTarget) return failures + 1;
			host.PublishState(desired);
			const auto ready = Bridge::ReadyIdentityFor(*desired.presentationTarget);
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Presentation, 0, 1, &ready);
			check(host.PublishPresentationUiReady(ready) && state.inputGateReasons == 1024u &&
				state.input.AdmissionBlocked(), "CPU scene and UI ACK cannot retire the workspace fence");
			check(host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) && host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(1, false, firstRun.hostGeneration) && !host.SetWorkspaceTransitionPending(1, true, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(3, false, firstRun.hostGeneration) && state.input.AdmissionBlocked(),
				"new epoch supersedes pending and stale or unmatched retirement cannot clear it");
			check(host.SetWorkspaceTransitionPending(2, false, firstRun.hostGeneration) && host.SetWorkspaceTransitionPending(2, false, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) && !state.input.AdmissionBlocked() &&
				!state.input.ContactAdmitted(duringPending),
				"only matching retirement opens its own reason; old Down and late Begin remain rejected");
			state.input.DiscardUntilTerminal(duringPending);
			check(state.input.PublishUp(0xF501, 2, sample) && !state.input.HasQuarantinedContacts(),
				"rejected transition Down still retires through its real terminal");
			check(state.input.PublishDown(0xF501, 2, InputDeviceType::MouseRight, sample),
				"new contact can follow matching transition retirement");
			const auto next = takeDown();
			check(state.input.ContactAdmitted(next) && state.input.PublishUp(0xF501, 2, sample),
				"new generation uses the reopened admission revision");
			state.input.Recycle(next);
			Bridge::ProductState desktop;
			host.PublishState(desktop);
			state.PumpBridgeState();
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Desktop, 0, 1, nullptr);
			Shortcuts::Context inkShortcut;
			inkShortcut.scope.pageEpoch = 1;
			inkShortcut.available = true; inkShortcut.hasContent = true;
			Host::Impl::ObserveShortcutContext(&state, inkShortcut);
			const auto shortcutBefore = host.ShortcutContext();
			const auto oldLease = host.AcquireShortcutLease(shortcutBefore, false);
			check(shortcutBefore.available && oldLease && host.SetWorkspaceTransitionPending(3, true, firstRun.hostGeneration),
				"ink shortcut lease can predate workspace pending");
			host.ReleaseShortcutLease(oldLease);
			check(!state.shortcuts.Lease() && state.input.BaseAdmissionBlocked() &&
				!host.ShortcutContext().available && !host.AcquireShortcutLease(shortcutBefore, false),
				"old shortcut release completes while pending prevents a new ink scope and lease");
			check(host.SetWorkspaceTransitionPending(3, false, firstRun.hostGeneration) && host.ShortcutContext().available,
				"matching retirement restores an otherwise eligible ink shortcut scope");
			std::uint64_t epoch = 4;
			for (const auto reason : { 2u, 4u, 512u })
			{
				Bridge::ProductState baseline;
				baseline.workspace = reason == 4u ? Bridge::Workspace::Whiteboard : Bridge::Workspace::Desktop;
				host.PublishState(baseline);
				Host::Impl::ObserveWorkspace(&state, baseline.workspace, 0, 1, nullptr);
				check(host.SetWorkspaceTransitionPending(epoch, true, firstRun.hostGeneration), "next gate fixture begins a real transition");
				if (reason == 2u) state.commandScenePending = true;
				if (reason == 4u) state.whiteboardLoadPending.store(true, std::memory_order_release);
				if (reason == 512u)
				{
					CanvasRecoveryState recovery; recovery.status = CanvasRecoveryStatus::Retrying;
					Host::Impl::ObserveRecovery(&state, recovery);
				}
				{ std::scoped_lock lock(state.presentationTargetMutex); state.RefreshPresentationInputGateLocked(); }
				check(host.SetWorkspaceTransitionPending(epoch++, false, firstRun.hostGeneration) && state.input.AdmissionBlocked() &&
					(state.inputGateReasons & reason) != 0,
					"retirement preserves independent scene, whiteboard load and owner recovery reasons");
				inkShortcut.scope.workspace = baseline.workspace;
				Host::Impl::ObserveShortcutContext(&state, inkShortcut);
				const auto blockedShortcut = host.ShortcutContext();
				check(!blockedShortcut.available && !host.AcquireShortcutLease(blockedShortcut, false),
					"scene, load and recovery still refuse new ink shortcuts after pending retires");
				state.commandScenePending = false;
				state.whiteboardLoadPending.store(false, std::memory_order_release);
				Host::Impl::ObserveRecovery(&state, {});
				check(!state.input.AdmissionBlocked(), "original owner can later clear its own reason");
			}
			const auto minimizedEpoch = epoch | std::uint64_t{ 1 };
			check(host.SetWorkspaceTransitionPending(minimizedEpoch, true, firstRun.hostGeneration),
				"minimize begins with the existing workspace admission reason");
			Bridge::ProductState whiteboard;
			whiteboard.workspace = Bridge::Workspace::Whiteboard; whiteboard.selectionMode = false;
			host.PublishState(whiteboard); state.PumpBridgeState();
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Whiteboard, 0, 1, nullptr);
			// 稳定最小化只退休协调 Pending，不撤销同一个 Host 原因。
			check(host.SetWorkspaceTransitionPending(minimizedEpoch, true, firstRun.hostGeneration) &&
				state.input.AdmissionBlocked() && state.inputGateReasons == 1024u,
				"ready and repeated stable-minimized publication keep Pen admission closed");
			const auto restoreEpoch = minimizedEpoch + 2;
			check(host.SetWorkspaceTransitionPending(restoreEpoch, true, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(minimizedEpoch, false, firstRun.hostGeneration) &&
				state.input.AdmissionBlocked(), "restore takes a new odd epoch and an old minimize retirement cannot open it");
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Whiteboard, 0, 1, nullptr);
			check(state.input.AdmissionBlocked() &&
				host.SetWorkspaceTransitionPending(restoreEpoch, false, firstRun.hostGeneration) &&
				!state.input.AdmissionBlocked(), "only matching restore retirement reopens otherwise ready Pen admission");
			host.PublishState(desktop); state.PumpBridgeState();
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Desktop, 0, 1, nullptr);
			inkShortcut.scope.workspace = Bridge::Workspace::Desktop;
			Host::Impl::ObserveShortcutContext(&state, inkShortcut);
			epoch = restoreEpoch + 1;
			check(host.SetWorkspaceTransitionPending(epoch, true, firstRun.hostGeneration), "shortcut fixture begins transition");
			state.input.SetShortcutAdmissionBlocked(true);
			check(host.SetWorkspaceTransitionPending(epoch++, false, firstRun.hostGeneration) && state.input.AdmissionBlocked() &&
				!state.input.BaseAdmissionBlocked(), "transition retirement does not clear the separate shortcut OR gate");
			state.input.SetShortcutAdmissionBlocked(false);
			check(host.SetWorkspaceTransitionPending(epoch, true, firstRun.hostGeneration), "fatal fixture begins transition");
			state.input.SetConsumerFatal();
			check(host.SetWorkspaceTransitionPending(epoch, false, firstRun.hostGeneration) && state.input.ConsumerFatal() &&
				state.input.AdmissionBlocked() && !host.ShortcutContext().available &&
				!host.AcquireShortcutLease(host.ShortcutContext(), false),
				"transition retirement cannot reopen a fatal consumer or its ink shortcuts");
			// 与 Start 相同的无 producer reset；旧 pending/fatal 不泄漏到下一 run。
			state.input.ResetForNextRun(); state.bridge.Reset(); state.ResetRuntimeDiagnostics();
			const auto restarted = host.RuntimeSnapshot();
			check(!state.input.AdmissionBlocked() && !state.input.ConsumerFatal() &&
				restarted.hostGeneration != firstRun.hostGeneration && restarted.appliedProductRevision == 0 &&
				!host.SetWorkspaceTransitionPending(1, true, firstRun.hostGeneration) && !state.input.AdmissionBlocked(),
				"new run advances Host identity and rejects an old-run Begin without resurrecting its fence");
			check(host.SetWorkspaceTransitionPending(1, true, restarted.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(1, false, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) && state.input.AdmissionBlocked(),
				"same numeric scene epoch in a new run cannot be cleared or replaced by old-run publications");
			check(host.SetWorkspaceTransitionPending(1, false, restarted.hostGeneration) &&
				!state.input.AdmissionBlocked() && !host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) &&
				!state.input.AdmissionBlocked() && host.SetWorkspaceTransitionPending(3, true, restarted.hostGeneration),
				"new-run owner alone retires and reopens its fence; late old Begin cannot close a stable scene");
			inkShortcut.scope.workspace = Bridge::Workspace::Desktop;
			Host::Impl::ObserveShortcutContext(&state, inkShortcut);
			state.CloseInputAdmission();
			check(state.input.AdmissionBlocked() && !state.workspaceTransitionPending &&
				state.workspaceTransitionEpoch == 0 && state.inputGateReasons == 1u &&
				!host.SetWorkspaceTransitionPending(3, false, restarted.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(4, true, restarted.hostGeneration) && !host.ShortcutContext().available &&
				!host.AcquireShortcutLease(host.ShortcutContext(), false),
				"stop retires its fence and refuses reopen or new ink shortcuts while preserving stop admission");
			check(state.input.DiagnosticsSnapshot().occupiedSlots == 0, "all real contact routes are released");
		}
		catch (...) { check(false, "workspace transition production fixture completes without exception"); }
		if (!failures) std::fputs("[Draw3WorkspaceTransition] PASS: epoch fence, owner ACK, independent reasons and real contact drain\n", stderr);
		return failures;
	}

	bool Host::PublishPresentationUiReady(
		const Bridge::PresentationReadyIdentity& identity) noexcept
	{
		if (!Running()) return false;
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto state = impl_->bridge.Snapshot();
		if (!TryLatchPresentationUiReady(state, impl_->suspendedPresentationTarget,
			identity, impl_->uiReadyPresentationTarget)) return false;
		TracePptTiming("page_ui_ack", identity.sessionRevision, identity.targetRevision);
		impl_->RefreshPresentationInputGateLocked();
		return true;
	}

	bool Host::SetPresentationInputSuspended(
		const Bridge::PresentationReadyIdentity& expected, bool suspended) noexcept
	{
		if (!Running()) return false;
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto state = impl_->bridge.Snapshot();
		if (state.workspace != Bridge::Workspace::Presentation || !state.presentationTarget ||
			Bridge::ReadyIdentityFor(*state.presentationTarget) != expected) return false;
		impl_->suspendedPresentationTarget = suspended ? std::optional(expected) : std::nullopt;
		// Unknown/结束页可能把数字改成占位；恢复时必须等待当前 UI 再次确认。
		if (suspended)
		{
			impl_->uiReadyPresentationTarget.reset();
			// UI 可合并掉中间的占位页；恢复时换 target revision，要求真实呈现及新回执。
			(void)impl_->bridge.RequirePresentationTargetRefresh(expected);
		}
		impl_->RefreshPresentationInputGateLocked();
		return true;
	}

	Bridge::CommandResult Host::PublishWhiteboardTheme(WhiteboardTheme theme,
		std::uint64_t generation, std::size_t page) noexcept
	{
		std::scoped_lock lock(impl_->commandPublishMutex);
		if (!impl_->input.TryReserveCommandWake()) return Bridge::CommandResult::QueueFull;
		const auto result = impl_->bridge.PublishWhiteboardTheme(theme, generation, page);
		if (result == Bridge::CommandResult::Accepted) impl_->input.PublishReservedCommandWake();
		else impl_->input.CancelReservedCommandWake();
		return result;
	}

	Shortcuts::Context Host::ShortcutContext() const noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		return impl_->ShortcutContextLocked();
	}

	std::uint64_t Host::PublishShortcutCommand(Bridge::CommandType type,
		const Shortcuts::Scope& expected, std::uint64_t leaseId,
		std::optional<std::uint64_t> expectedContentRevision) noexcept
	{
		std::scoped_lock commandLock(impl_->commandPublishMutex);
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto current = impl_->ShortcutContextLocked();
		if (!current.available || current.scope != expected || impl_->shortcuts.Lease() != leaseId)
			return 0;
		const auto id = impl_->shortcuts.Reserve(expected);
		if (!id) return 0;
		if (!impl_->input.TryReserveCommandWake())
		{
			impl_->shortcuts.Retire(id);
			return 0;
		}
		Bridge::CommandResult result = Bridge::CommandResult::QueueFull;
		try
		{
			auto request = std::make_shared<Shortcuts::Request>();
			*request = { id, expected, leaseId, expectedContentRevision };
			result = impl_->bridge.PublishShortcut(type, std::move(request));
		}
		catch (...) {}
		if (result == Bridge::CommandResult::Accepted)
		{
			impl_->input.PublishReservedCommandWake();
			return id;
		}
		impl_->input.CancelReservedCommandWake();
		impl_->shortcuts.Retire(id);
		return 0;
	}

	bool Host::TakeShortcutCompletion(std::uint64_t id, Shortcuts::Completion& result) noexcept
	{
		return impl_->shortcuts.Take(id, result);
	}
	void Host::RetireShortcutRequest(std::uint64_t id) noexcept { impl_->shortcuts.Retire(id); }
	std::uint64_t Host::AcquireShortcutLease(const Shortcuts::Context& expected,
		bool requireContentRevision) noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto current = impl_->ShortcutContextLocked();
		if (!current.available || current.scope != expected.scope) return 0;
		const auto id = impl_->shortcuts.Acquire(expected, requireContentRevision);
		if (id) impl_->input.SetShortcutAdmissionBlocked(true);
		return id;
	}
	void Host::ReleaseShortcutLease(std::uint64_t id) noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		if (impl_->shortcuts.Release(id))
		{
			// 状态内核已发布 Selection，但绘制 owner 尚未应用时继续关门，避免 Clear 后插入新笔迹。
			impl_->shortcutSelectionHandoffPending =
				impl_->bridge.Snapshot().selectionMode != impl_->window.SelectionMode();
			impl_->input.SetShortcutAdmissionBlocked(impl_->shortcutSelectionHandoffPending);
		}
	}

	Bridge::CommandResult Host::PublishCommand(Bridge::CommandType command) noexcept
	{
		std::scoped_lock lock(impl_->commandPublishMutex);
		// 与 Acquire/Release 共用提交门；已接受的旧命令仍按原 FIFO 在 Clear/Fence 之前完成。
		std::scoped_lock sceneLock(impl_->presentationTargetMutex);
		if (!impl_->shortcuts.OrdinaryCommandAllowed(command)) return Bridge::CommandResult::QueueFull;
		if (!impl_->input.TryReserveCommandWake()) return Bridge::CommandResult::QueueFull;
		const Bridge::CommandResult result = impl_->bridge.Publish(command);
		if (result == Bridge::CommandResult::Accepted)
			impl_->input.PublishReservedCommandWake();
		else impl_->input.CancelReservedCommandWake();
		return result;
	}
}
