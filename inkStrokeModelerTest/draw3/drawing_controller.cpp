module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <iostream>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <windows.h>

module draw3.drawing_controller;

import draw3.diagnostics;

namespace draw3
{
	DrawingController::DrawingController(WindowController& window, InkRenderer& renderer,
		TransparentPresentationController& presentation, StrokeModelConfiguration configuration)
		: window_(window), renderer_(renderer), presentation_(presentation), configuration_(std::move(configuration))
	{
	}

	void DrawingController::CompositeLayersToBackBuffer(RECT dirty)
	{
		const WindowSize size = window_.Size();
		dirty = ClampRectToCanvas(dirty, size.width, size.height); // 限制脏区，避免纹理复制越界。
		if (IsEmptyRect(dirty)) return;
		renderer_.CopyResource(renderer_.backBufferTexture.Get(), renderer_.layerL2Texture.Get(), dirty); // L2 是已经稳定的底层画布。
		renderer_.AlphaBlendResource(renderer_.backBufferRTV.Get(), renderer_.layerL1SRV.Get(), dirty); // L1 叠加当前笔画已确认部分。
		renderer_.AlphaBlendResource(renderer_.backBufferRTV.Get(), renderer_.layerL0SRV.Get(), dirty); // L0 叠加实时笔锋和预测部分。
	}

	bool DrawingController::PresentFrame(RECT dirty, bool presentFull)
	{
		const bool succeeded = presentation_.Present(dirty, presentFull);
		if (!succeeded) window_.RequestFullPresent(); // 呈现失败时下一轮强制全量刷新兜底。
		return succeeded;
	}

	void DrawingController::ClearCanvas()
	{
		const WindowSize size = window_.Size();
		const RECT fullCanvas = GetFullCanvasRect(size.width, size.height);
		renderer_.ClearRTV(renderer_.layerL2RTV.Get(), presentation_.WindowBackgroundColor()); // 清掉长期保存的稳定画布。
		renderer_.ClearRTV(renderer_.layerL1RTV.Get(), kTransparentLayerClearColor); // 清掉当前笔画已确认层。
		renderer_.ClearRTV(renderer_.layerL0RTV.Get(), kTransparentLayerClearColor); // 清掉当前帧实时层。
		renderer_.ClearRTV(renderer_.backBufferRTV.Get(), presentation_.WindowBackgroundColor()); // 先把最终缓冲区也恢复到背景。
		CompositeLayersToBackBuffer(fullCanvas);
		PresentFrame(fullCanvas, true);
	}

	void DrawingController::PresentFullCanvas()
	{
		const WindowSize size = window_.Size();
		const RECT fullCanvas = GetFullCanvasRect(size.width, size.height);
		CompositeLayersToBackBuffer(fullCanvas);
		PresentFrame(fullCanvas, true);
	}

	bool DrawingController::ProcessPendingResize(bool presentAfterResize)
	{
		WindowSize requestedSize = {};
		if (!window_.ConsumeResizeRequest(requestedSize)) return false; // 只有窗口过程投递过尺寸变化才处理。
		const WindowSize oldSize = window_.Size();
		if (requestedSize.width <= 0 || requestedSize.height <= 0 ||
			(requestedSize.width == oldSize.width && requestedSize.height == oldSize.height)) return false;

		if (!renderer_.Resize(presentation_.SwapChain(), requestedSize.width, requestedSize.height)) // 先重建所有尺寸相关 D3D 资源。
		{
			std::cout << "Failed to resize D3D resources to " << requestedSize.width << "x" << requestedSize.height << std::endl;
			return false;
		}
		if (!presentation_.Resize(requestedSize.width, requestedSize.height)) // 再通知当前透明呈现器更新外部资源。
		{
			std::cout << "Failed to resize transparent presenter to " << requestedSize.width << "x" << requestedSize.height << std::endl;
			return false;
		}

		// 仅在两组资源都重建成功后提交逻辑尺寸。
		window_.CommitSize(requestedSize.width, requestedSize.height);
		if (presentAfterResize) PresentFullCanvas();
		return true;
	}

	void DrawingController::DrawMouseStroke(const MouseMessage& startMessage)
	{
		using namespace ink::stroke_model;
		bool eraser = startMessage.message == WM_RBUTTONDOWN;
		eraser = false; // 当前阶段仍只验证画笔路径，保留原有行为。
		RECT strokeDirty = {};
		bool isFirstFrame = true;

		ApplyPredictionMode(configuration_.modelParams, configuration_.kalmanPredictorParams); // 将当前全局预测模式写入本次笔画配置。
		const float baseDiameter = eraser ? 50.0f : 5.0f;
		const float shapeType = static_cast<float>(window_.BrushShapeType());
		const DirectX::XMFLOAT4 stableInkColor(1.0f, 0.0f, 0.0f, 1.0f);
		const DirectX::XMFLOAT4 liveInkColor = kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer
			? DirectX::XMFLOAT4(0.0f, 0.35f, 1.0f, 1.0f)
			: stableInkColor;

		ActiveMouseStroke stroke(baseDiameter, configuration_.expectedSpeed);
		if (absl::Status status = stroke.modeler.Reset(configuration_.modelParams); !status.ok()) // 每一笔都用干净的模型状态开始。
		{
			std::cout << "Error: " << status.message() << std::endl;
		}
		const float originX = static_cast<float>(startMessage.x);
		const float originY = static_cast<float>(startMessage.y);
		const auto startTime = std::chrono::high_resolution_clock::now();
		Input downInput{ .event_type = Input::EventType::kDown, .position = Vec2(originX, originY), .time = Time(0.0) };
		if (absl::Status status = stroke.modeler.Update(downInput, stroke.modeledResults); !status.ok())
		{
			std::cout << "Error: " << status.message() << std::endl;
		}
		AppendNewModeledPoints(stroke); // 把模型输出转换成渲染器可直接消费的 InkPoint。
		stroke.lastRawPosition = POINT{ static_cast<LONG>(originX), static_cast<LONG>(originY) };
		stroke.hasLastRawPosition = true;

		double lastFrameStartMs = GetQpcTimeMilliseconds();
		bool hasFrameTiming = false;
		while (true)
		{
			const double frameStartMs = GetQpcTimeMilliseconds();
			const double previousFrameMs = hasFrameTiming ? frameStartMs - lastFrameStartMs : 0.0;
			lastFrameStartMs = frameStartMs;
			hasFrameTiming = true;

			bool forceL0Redraw = false;
			if (ProcessPendingResize(false))
			{
				const WindowSize size = window_.Size();
				stroke.lastL0Rect = {}; // Resize 后旧 L0 脏区已无意义，改用重算后的当前区域。
				stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height);
				strokeDirty = ClampRectToCanvas(strokeDirty, size.width, size.height);
				isFirstFrame = true;
				forceL0Redraw = true;
			}

			POINT cursorPosition = {};
			GetCursorPos(&cursorPosition);
			ScreenToClient(window_.Handle(), &cursorPosition);
			const double wallElapsedSeconds = std::chrono::duration<double>(
				std::chrono::high_resolution_clock::now() - startTime).count();
			const double wallDeltaSeconds = std::max(0.0, wallElapsedSeconds - stroke.lastFrameWallTime);
			stroke.lastFrameWallTime = wallElapsedSeconds; // 用墙钟时间推进模型，避免 Sleep 抖动导致输入时间倒退。
			const bool rawMoved = UpdateRawPositionAndDetectMovement(stroke, cursorPosition); // 过滤极小抖动，只保留有效移动。
			if (rawMoved)
			{
				stroke.idleFrozen = false;
				stroke.visualStableFrameCount = 0;
			}

			RECT stableDirty = {};
			RECT l0FrameDirty = {};
			const WindowSize size = window_.Size();
			if (!stroke.idleFrozen)
			{
				stroke.logicalInputTime += wallDeltaSeconds;
				if (rawMoved) stroke.lastMovementInputTime = stroke.logicalInputTime;
				Input moveInput{
					.event_type = Input::EventType::kMove,
					.position = Vec2(static_cast<float>(cursorPosition.x), static_cast<float>(cursorPosition.y)),
					.time = Time(stroke.logicalInputTime)
				};
				if (absl::Status status = stroke.modeler.Update(moveInput, stroke.modeledResults); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
				}
				AppendNewModeledPoints(stroke); // 只追加本帧新增的模型输出，避免重复绘制点。

				stroke.predictedResults.clear();
				if (kActivePredictionMode != InkPredictionMode::Disabled)
				{
					if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
						stroke.predictedResults.clear();
				}
				RebuildPredictedPoints(stroke); // 用当前笔宽状态推导预测点半径。
				stableDirty = CommitStablePrefixToL1(stroke, configuration_.liveTipDurationSeconds,
					GetPredictionDurationSeconds(stroke), stableInkColor, shapeType, eraser,
					renderer_, size.width, size.height);

				stroke.lastL0Rect = stroke.currentL0Rect;
				RebuildL0DrawPoints(stroke, configuration_.liveTipDurationSeconds, size.width, size.height); // L0 保留可变化尾部和预测点。
				UpdateIdleFreezeState(stroke, rawMoved, configuration_.liveTipDurationSeconds); // 停笔后视觉稳定则冻结 L0，减少空转。
				DrawL0LiveComposite(stroke, liveInkColor, shapeType, eraser, renderer_);
				UnionRectInPlace(l0FrameDirty, stroke.lastL0Rect); // 旧 L0 区域也要刷新，清掉上一帧预测残影。
				UnionRectInPlace(l0FrameDirty, stroke.currentL0Rect); // 新 L0 区域需要显示最新笔锋。
			}
			else if (forceL0Redraw)
			{
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height);
				DrawL0LiveComposite(stroke, liveInkColor, shapeType, eraser, renderer_);
				UnionRectInPlace(l0FrameDirty, stroke.currentL0Rect);
			}

			RECT frameDirty = {};
			UnionRectInPlace(frameDirty, stableDirty);
			UnionRectInPlace(frameDirty, l0FrameDirty);
			frameDirty = ClampRectToCanvas(frameDirty, size.width, size.height);
			if (!IsEmptyRect(frameDirty))
			{
				const RECT compositeRect = isFirstFrame ? GetFullCanvasRect(size.width, size.height) : frameDirty; // 第一帧强制全量，避免旧 backbuffer 残留。
				CompositeLayersToBackBuffer(compositeRect);
				PresentFrame(compositeRect, isFirstFrame);
				isFirstFrame = false;
			}
			UnionRectInPlace(strokeDirty, stableDirty); // 记录最终需要落盘到 L2 的真实笔迹范围。

			if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) break;
			window_.FlushMouseMessages(); // 循环直接读当前鼠标位置，清掉积压消息降低延迟。
			const double workMs = GetQpcTimeMilliseconds() - frameStartMs;
			HighPrecisionWait(workMs, configuration_.timingProfile.target_fps);
			LogFrameTiming(stroke.committedIndex, stroke.realPoints.size(), stroke.predictedPoints.size(),
				stroke.l0DrawPoints.size(), workMs, previousFrameMs, stroke.idleFrozen);
		}

		if (ProcessPendingResize(false))
		{
			const WindowSize size = window_.Size();
			stroke.lastL0Rect = {};
			stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height);
			strokeDirty = ClampRectToCanvas(strokeDirty, size.width, size.height);
			isFirstFrame = true;
		}

		const WindowSize finalSize = window_.Size();
		// 抬笔时把最后一帧可见 L0 原样落到 L1，避免笔锋和预测回缩。
		if (!stroke.l0DrawPoints.empty())
		{
			renderer_.SetOMTarget(renderer_.layerL1RTV.Get());
			renderer_.DrawStrokeOrDot(stroke.l0DrawPoints, liveInkColor, shapeType, eraser);
			UnionRectInPlace(strokeDirty, stroke.currentL0Rect);
		}
		strokeDirty = ClampRectToCanvas(strokeDirty, finalSize.width, finalSize.height);
		if (!IsEmptyRect(strokeDirty))
		{
			renderer_.AlphaBlendResource(renderer_.layerL2RTV.Get(), renderer_.layerL1SRV.Get(), strokeDirty); // 抬笔后把本笔真实内容并入稳定画布。
			renderer_.ClearRTV(renderer_.layerL1RTV.Get(), kTransparentLayerClearColor); // L1/L0 都是本笔临时层，结束后清空。
			renderer_.ClearRTV(renderer_.layerL0RTV.Get(), kTransparentLayerClearColor);
			const RECT finalPresentRect = isFirstFrame ? GetFullCanvasRect(finalSize.width, finalSize.height) : strokeDirty;
			renderer_.CopyResource(renderer_.backBufferTexture.Get(), renderer_.layerL2Texture.Get(), finalPresentRect);
			PresentFrame(finalPresentRect, isFirstFrame);
		}
		else
		{
			renderer_.ClearRTV(renderer_.layerL0RTV.Get(), kTransparentLayerClearColor);
			if (isFirstFrame) PresentFullCanvas();
		}
		window_.FlushMouseMessages();
	}
}
