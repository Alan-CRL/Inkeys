module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <compare>
#include <cstddef>
#include <d3d11.h>
#include <mutex>
#include <optional>
#include <windows.h>
#include "Draw3.Bridge.h"
#include "Draw3.SpeedEraser.h"
#include "Draw3.PenDiagnostics.h"

export module Inkeys.Drawing.Draw3.drawing_controller;

import Inkeys.Drawing.Draw3.auto_save;
import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Drawing.Draw3.haptic_feedback;
import Inkeys.Drawing.Draw3.ink_document;
import Inkeys.Drawing.Draw3.ink_history;
import Inkeys.Drawing.Draw3.ink_history_gpu;
import Inkeys.Drawing.Draw3.ink_prediction;
import Inkeys.Drawing.Draw3.presentation_auto_save;
import Inkeys.Drawing.Draw3.renderer;
import Inkeys.Drawing.Draw3.runtime_metrics;
import Inkeys.Drawing.Draw3.transparent_presentation;
import Inkeys.Drawing.Draw3.window_control;

export namespace Inkeys::Drawing::Draw3
{
	// 显式无窗口诊断入口：用生产 Laser 烘干路径验证真实 WARP Map 失败。
	int RunLaserRasterFailureProductionProbe(InkRenderer& renderer,
		ID3D11Buffer* unwritableInkBuffer) noexcept;
	// 无 HWND、无磁盘：同一生产 Exit 捕获入口验证停放 Desktop 的源身份。
	int RunParkedDesktopExitAutoSaveTest() noexcept;
	// 无 HWND、无磁盘：生产 PPT 快照构造验证活动/停放槽的 retained 身份。
	int RunParkedPresentationRetainedSaveTest() noexcept;
	// 无 HWND、无磁盘：生产 PPT 加载 materialize/安装/再保存的 retained 身份。
	int RunPresentationLoadedRetainedInstallTest() noexcept;
	// 无 HWND、无磁盘：PPT 冷加载期间新旧 SlideID 拓扑双向投影。
	int RunPendingPresentationTopologyLoadTest() noexcept;
	// 无 HWND 策略红测：旧 page-index 与新 Stable 绝不能自动按 ordinal 共用槽。
	int RunFallbackStableControllerIsolationProbe() noexcept;
	// 无 HWND：通过 Run 共用的 PPT CPU 槽切换事务验证 fallback/Stable 双轨隔离。
	int RunFallbackStableControllerLaneProbe() noexcept;
	// 无 HWND：PPT Current Load 一次瞬态失败后的同目标有界重试。
	int RunPresentationCurrentLoadRetryProbe() noexcept;
	// 无 HWND：复用 Run 的 ingress 批次函数验证命令屏障前后不吞新 Down。
	int RunDraw3ControlFenceProductionProbe() noexcept;

	struct DrawingControllerRuntimeObserver
	{
		void* context = nullptr;
		void (*presented)(void*, bool, RECT, bool,
			TransparentPresentObservation) = nullptr;
		void (*resized)(void*, int, int) = nullptr;
		void (*commandProcessed)(void*, CanvasCommandType,
			std::size_t, std::size_t) = nullptr;
		void (*documentReady)(void*, std::size_t, std::size_t) = nullptr;
		void (*currentPageContentChanged)(void*, bool, std::uint64_t) = nullptr;
		void (*strokeCompleted)(void*, Bridge::CompletedStrokeKind) = nullptr;
		void (*workspaceChanged)(void*, Bridge::Workspace, std::size_t, std::size_t,
			const Bridge::PresentationReadyIdentity*) = nullptr;
		void (*controlWake)(void*, ControlWakeKind) = nullptr;
		bool (*desktopAutoSaveRequested)(void*, DesktopAutoSaveTrigger,
			draw3::uink::Draw3UInkExportSnapshot&&) = nullptr;
		bool (*desktopLoadRequested)(void*, draw3::uink::UInkGuid) = nullptr;
		bool (*presentationSaveRequested)(void*, PresentationSaveRequest&&) = nullptr;
		bool (*presentationLoadRequested)(void*, PresentationLoadRequest&&) = nullptr;
		void (*drawingActivityChanged)(void*, bool) = nullptr;
		void (*eraserDiagnostics)(void*, const SpeedEraser::Diagnostics&) = nullptr;
		void (*penDiagnostics)(void*, const PenRuntimeDiagnostics&) = nullptr;
	};

	// 协调窗口请求、三层画布和多 contact 实时绘制循环。
	class DrawingController
	{
	public:
		DrawingController(ContactInputCoordinator& input, WindowController& window, InkRenderer& renderer,
			TransparentPresentationController& presentation, StrokeModelConfiguration configuration,
			DrawingControllerRuntimeObserver observer = {},
			RuntimeMetricsSession* metrics = nullptr, PenHapticFeedback* haptics = nullptr);
		// 成组更新设备宽度设置；新设置只影响之后开始的普通笔笔画。
		bool SetInputWidthModeSettings(InputWidthModeSettings settings) noexcept;
		InputWidthModeSettings GetInputWidthModeSettings() const noexcept;
		// 控制倒转 Pen 是否在画笔/荧光笔下临时作为橡皮；只影响之后开始的笔画。
		void SetInvertedPenEraserEnabled(bool enabled) noexcept;
		bool GetInvertedPenEraserEnabled() const noexcept;
		// 即时控制 Touch 断触修正；关闭时已有候选会立即按正常 Up 收尾。
		void SetInterruptedStrokeReconnectEnabled(bool enabled) noexcept;
		bool GetInterruptedStrokeReconnectEnabled() const noexcept;
		// 即时控制普通 Pen/Highlighter 落笔时是否继续显示应用内光标。
		void SetDrawingCursorDuringContactEnabled(bool enabled) noexcept;
		bool GetDrawingCursorDuringContactEnabled() const noexcept;
		// 控制 Ink 设备光标是否使用旧的半透明外观；默认使用不透明外观。
		void SetTranslucentInkCursorEnabled(bool enabled) noexcept;
		bool GetTranslucentInkCursorEnabled() const noexcept;
		// 控制普通绘制工具下鼠标使用系统箭头或不透明应用光标；不影响 Eraser/Laser。
		void SetMouseUsesSystemCursor(bool enabled) noexcept;
		bool GetMouseUsesSystemCursor() const noexcept;
		// 即时控制激光笔的稀疏粒子点缀，不影响主轨迹和留存计时。
		void SetLaserParticlesEnabled(bool enabled) noexcept;
		bool GetLaserParticlesEnabled() const noexcept;
		// 控制激光笔是否接受同时按下的多根 Touch；只影响之后开始的 contact。
		void SetLaserMultiTouchDrawingEnabled(bool enabled) noexcept;
		bool GetLaserMultiTouchDrawingEnabled() const noexcept;
		// 设置最后一根激光笔抬起后的满亮留存秒数；只接受有限非负值。
		bool SetLaserHoldDurationSeconds(double seconds) noexcept;
		double GetLaserHoldDurationSeconds() const noexcept;
		// 运行时调整热前像和合成树预算；0 表示关闭对应缓存。
		void SetUndoCachePolicy(UndoCachePolicy policy);
		UndoCachePolicy GetUndoCachePolicy() const;
		void SetCompositionCachePolicy(CompositionCachePolicy policy);
		CompositionCachePolicy GetCompositionCachePolicy() const;
		// 清空 L0/L1/L2 和 backbuffer，并立即全量呈现。
		void ClearCanvas();
		// 合成并呈现完整画布。
		void PresentFullCanvas();
		// 在绘制线程处理延迟的窗口缩放请求。
		bool ProcessPendingResize(bool presentAfterResize);
		// 运行 RTS 多 contact 绘制循环；完全空闲时阻塞在零自旋信号量。
		void Run();
	private:
		bool CompositeLayersToBackBuffer(RECT dirty, bool orderLiveOverStable = false);
		bool PresentFrame(RECT dirty, bool presentFull);

		ContactInputCoordinator& input_;
		WindowController& window_;
		InkRenderer& renderer_;
		TransparentPresentationController& presentation_;
		StrokeModelConfiguration configuration_;
		DrawingControllerRuntimeObserver observer_;
		InputWidthModeSettingsState inputWidthModeSettings_;
		std::atomic<bool> invertedPenEraserEnabled_ = true;
		std::atomic<bool> interruptedStrokeReconnectEnabled_ = true;
		std::atomic<bool> drawingCursorDuringContactEnabled_ = false;
		std::atomic<bool> translucentInkCursorEnabled_ = false;
		std::atomic<bool> laserParticlesEnabled_ = false;
		std::atomic<bool> laserMultiTouchDrawingEnabled_ = false;
		std::atomic<double> laserHoldDurationSeconds_ = 1.0;
		mutable std::mutex historyCachePolicyMutex_;
		UndoCachePolicy undoCachePolicy_ = {};
		CompositionCachePolicy compositionCachePolicy_ = {};
		std::atomic<uint64_t> historyCachePolicyGeneration_ = 1;
		std::optional<InkCanvasCollection> document_;
		size_t currentPageIndex_ = 0;
		RuntimeMetricsSession* metrics_ = nullptr;
		PenHapticFeedback* haptics_ = nullptr;
		double lastPresentDurationMs_ = 0.0;
		bool lastPresentSucceeded_ = false;
		bool graphicsRecoveryPending_ = false;
		std::uint64_t currentContentRevision_ = 0;
	};
}
