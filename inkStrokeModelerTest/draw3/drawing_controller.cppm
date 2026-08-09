module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <compare>
#include <windows.h>

export module draw3.drawing_controller;

import draw3.contact_input;
import draw3.haptic_feedback;
import draw3.ink_prediction;
import draw3.renderer;
import draw3.runtime_metrics;
import draw3.transparent_presentation;
import draw3.window_control;

export namespace draw3
{
	// 协调窗口请求、三层画布和多 contact 实时绘制循环。
	class DrawingController
	{
	public:
		DrawingController(ContactInputCoordinator& input, WindowController& window, InkRenderer& renderer,
			TransparentPresentationController& presentation, StrokeModelConfiguration configuration,
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
		// 即时控制激光笔的稀疏粒子点缀，不影响主轨迹和留存计时。
		void SetLaserParticlesEnabled(bool enabled) noexcept;
		bool GetLaserParticlesEnabled() const noexcept;
		// 控制激光笔是否接受同时按下的多根 Touch；只影响之后开始的 contact。
		void SetLaserMultiTouchDrawingEnabled(bool enabled) noexcept;
		bool GetLaserMultiTouchDrawingEnabled() const noexcept;
		// 设置最后一根激光笔抬起后的满亮留存秒数；只接受有限非负值。
		bool SetLaserHoldDurationSeconds(double seconds) noexcept;
		double GetLaserHoldDurationSeconds() const noexcept;
		// 控制独立性能 HUD；默认关闭，不进入墨迹合成路径。
		void SetPerformanceHudEnabled(bool enabled) noexcept;
		bool GetPerformanceHudEnabled() const noexcept;
		// 清空 L0/L1/L2 和 backbuffer，并立即全量呈现。
		void ClearCanvas();
		// 合成并呈现完整画布。
		void PresentFullCanvas();
		// 在绘制线程处理延迟的窗口缩放请求。
		bool ProcessPendingResize(bool presentAfterResize);
		// 运行 RTS 多 contact 绘制循环；完全空闲时阻塞在零自旋信号量。
		void Run();
	private:
		void CompositeLayersToBackBuffer(RECT dirty, bool orderLiveOverStable = false);
		bool PresentFrame(RECT dirty, bool presentFull);

		ContactInputCoordinator& input_;
		WindowController& window_;
		InkRenderer& renderer_;
		TransparentPresentationController& presentation_;
		StrokeModelConfiguration configuration_;
		InputWidthModeSettingsState inputWidthModeSettings_;
		std::atomic<bool> invertedPenEraserEnabled_ = true;
		std::atomic<bool> interruptedStrokeReconnectEnabled_ = true;
		std::atomic<bool> drawingCursorDuringContactEnabled_ = false;
		std::atomic<bool> laserParticlesEnabled_ = false;
		std::atomic<bool> laserMultiTouchDrawingEnabled_ = false;
		std::atomic<bool> performanceHudEnabled_ = false;
		std::atomic<bool> performanceHudResetRequested_ = false;
		std::atomic<double> laserHoldDurationSeconds_ = 1.0;
		PerformanceHudTracker performanceHudTracker_;
		RuntimeMetricsSession* metrics_ = nullptr;
		PenHapticFeedback* haptics_ = nullptr;
		double lastPresentDurationMs_ = 0.0;
		bool lastPresentSucceeded_ = false;
	};
}
