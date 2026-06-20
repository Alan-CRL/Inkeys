module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <compare>
#include <windows.h>

export module draw3.drawing_controller;

import draw3.ink_prediction;
import draw3.renderer;
import draw3.transparent_presentation;
import draw3.window_control;

export namespace draw3
{
	// 协调窗口请求、三层画布和单笔实时绘制循环。
	class DrawingController
	{
	public:
		DrawingController(WindowController& window, InkRenderer& renderer,
			TransparentPresentationController& presentation, StrokeModelConfiguration configuration);
		// 清空 L0/L1/L2 和 backbuffer，并立即全量呈现。
		void ClearCanvas();
		// 合成并呈现完整画布。
		void PresentFullCanvas();
		// 在绘制线程处理延迟的窗口缩放请求。
		bool ProcessPendingResize(bool presentAfterResize);
		// 运行一次从按下到抬起的鼠标笔画绘制循环。
		void DrawMouseStroke(const MouseMessage& startMessage);

	private:
		void CompositeLayersToBackBuffer(RECT dirty);
		bool PresentFrame(RECT dirty, bool presentFull);

		WindowController& window_;
		InkRenderer& renderer_;
		TransparentPresentationController& presentation_;
		StrokeModelConfiguration configuration_;
	};
}
