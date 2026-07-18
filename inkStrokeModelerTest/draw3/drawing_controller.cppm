module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <compare>
#include <windows.h>

export module draw3.drawing_controller;

import draw3.contact_input;
import draw3.ink_prediction;
import draw3.renderer;
import draw3.transparent_presentation;
import draw3.window_control;

export namespace draw3
{
	// 协调窗口请求、三层画布和多 contact 实时绘制循环。
	class DrawingController
	{
	public:
		DrawingController(ContactInputCoordinator& input, WindowController& window, InkRenderer& renderer,
			TransparentPresentationController& presentation, StrokeModelConfiguration configuration);
		// 清空 L0/L1/L2 和 backbuffer，并立即全量呈现。
		void ClearCanvas();
		// 合成并呈现完整画布。
		void PresentFullCanvas();
		// 在绘制线程处理延迟的窗口缩放请求。
		bool ProcessPendingResize(bool presentAfterResize);
		// 运行 RTS 多 contact 绘制循环；完全空闲时阻塞在零自旋信号量。
		void Run();
		// 保留旧鼠标单笔入口，主绘制流程不再调用。
		void DrawMouseStroke(const MouseMessage& startMessage);

	private:
		void CompositeLayersToBackBuffer(RECT dirty, bool orderLiveOverStable = false);
		bool PresentFrame(RECT dirty, bool presentFull);

		ContactInputCoordinator& input_;
		WindowController& window_;
		InkRenderer& renderer_;
		TransparentPresentationController& presentation_;
		StrokeModelConfiguration configuration_;
	};
}
