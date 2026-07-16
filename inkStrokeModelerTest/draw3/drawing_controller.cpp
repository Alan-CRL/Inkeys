module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <DirectXMath.h>
#include <iostream>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <windows.h>

module draw3.drawing_controller;

import draw3.diagnostics;

namespace draw3
{
	namespace
	{
		const DirectX::XMFLOAT4 kHighlighterCompositeColor(1.0f, 0.0f, 0.0f, 0.35f);
	}

	DrawingController::DrawingController(WindowController& window, InkRenderer& renderer,
		TransparentPresentationController& presentation, StrokeModelConfiguration configuration)
		: window_(window), renderer_(renderer), presentation_(presentation), configuration_(std::move(configuration))
	{
	}

	void DrawingController::CompositeLayersToBackBuffer(RECT dirty, DrawingTool tool)
	{
		const WindowSize size = window_.Size();
		dirty = ClampRectToCanvas(dirty, size.width, size.height); // 限制脏区，避免纹理复制越界。
		if (IsEmptyRect(dirty)) return;
		renderer_.CopyResource(renderer_.backBufferTexture.Get(), renderer_.layerL2Texture.Get(), dirty); // L2 是已经稳定的底层画布。
		if (tool == DrawingTool::Eraser)
		{
			// 橡皮先合并 L1/L0 的最大覆盖率，再仅裁除一次，避免分段重叠加重抗锯齿边缘。
			renderer_.ClipResource(renderer_.backBufferRTV.Get(), renderer_.layerL1SRV.Get(), renderer_.layerL0SRV.Get(), dirty);
		}
		else if (tool == DrawingTool::Highlighter)
		{
			// L1/L0 都是不透明覆盖率，取最大值着色后只透明合成一次。
			renderer_.BlendCoverageResource(renderer_.backBufferRTV.Get(), renderer_.layerL1SRV.Get(),
				renderer_.layerL0SRV.Get(), kHighlighterCompositeColor, dirty);
		}
		else
		{
			renderer_.AlphaBlendResource(renderer_.backBufferRTV.Get(), renderer_.layerL1SRV.Get(), dirty); // L1 叠加当前笔画已确认部分。
			renderer_.AlphaBlendResource(renderer_.backBufferRTV.Get(), renderer_.layerL0SRV.Get(), dirty); // L0 叠加实时笔锋和预测部分。
		}
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
		renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor); // 内部画布始终保持真透明背景。
		renderer_.ClearRTV(renderer_.layerL1RTV.Get(), kTransparentLayerClearColor); // 清掉当前笔画已确认层。
		renderer_.ClearRTV(renderer_.layerL0RTV.Get(), kTransparentLayerClearColor); // 清掉当前帧实时层。
		renderer_.ClearRTV(renderer_.backBufferRTV.Get(), kTransparentLayerClearColor); // backbuffer 也不写入 ULW 的命中测试底层。
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
		const DrawingTool tool = window_.ActiveTool(); // 每笔按下时固定工具，中途切换只影响下一笔。
		const bool eraser = tool == DrawingTool::Eraser;
		const bool highlighter = tool == DrawingTool::Highlighter;
		RECT strokeDirty = {};
		bool isFirstFrame = true;

		auto strokeModelParams = configuration_.modelParams;
		if (eraser)
			strokeModelParams.prediction_params = DisabledPredictorParams{}; // 橡皮保留同样的建模流程，但彻底关闭轨迹预测。
		else
			ApplyPredictionMode(strokeModelParams, configuration_.kalmanPredictorParams);
		const float baseDiameter = 50.0f; // 当前三种工具都使用 50px 基准直径，荧光笔固定不变。
		const StrokeShape shape = highlighter ? StrokeShape::SharpStrip : StrokeShape::RoundCapsule;
		const DirectX::XMFLOAT4 stableInkColor = highlighter
			? DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) // 荧光笔临时层只保存不透明覆盖率。
			: eraser ? DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f)
			: DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
		const DirectX::XMFLOAT4 liveInkColor = tool == DrawingTool::Pen &&
			kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer
			? DirectX::XMFLOAT4(0.0f, 0.35f, 1.0f, 1.0f)
			: stableInkColor;
		const double liveTipDurationSeconds = highlighter ? 0.0 : configuration_.liveTipDurationSeconds;

		ActiveMouseStroke stroke(baseDiameter, configuration_.expectedSpeed, highlighter, highlighter);
		if (absl::Status status = stroke.modeler.Reset(strokeModelParams); !status.ok()) // 每一笔都用干净的模型状态开始。
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
		AppendNewModeledPoints(stroke); // down 没有有效移动速度，暂时保持基准笔宽。
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
				stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height, shape);
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
			const POINT previousRawPosition = stroke.lastRawPosition;
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
				float inputSpeed = -1.0f;
				if (rawMoved)
				{
					if (wallDeltaSeconds > 0.0)
					{
						const float deltaX = static_cast<float>(cursorPosition.x - previousRawPosition.x);
						const float deltaY = static_cast<float>(cursorPosition.y - previousRawPosition.y);
						inputSpeed = std::hypot(deltaX, deltaY) / static_cast<float>(wallDeltaSeconds);
					}
					stroke.lastMovementInputTime = stroke.logicalInputTime;
				}
				Input moveInput{
					.event_type = Input::EventType::kMove,
					.position = Vec2(static_cast<float>(cursorPosition.x), static_cast<float>(cursorPosition.y)),
					.time = Time(stroke.logicalInputTime)
				};
				if (absl::Status status = stroke.modeler.Update(moveInput, stroke.modeledResults); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
				}
				AppendNewModeledPoints(stroke, inputSpeed); // 固定宽度工具会在笔画状态内忽略输入速度。

				stroke.predictedResults.clear();
				if (!eraser && kActivePredictionMode != InkPredictionMode::Disabled)
				{
					if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
						stroke.predictedResults.clear();
				}
				RebuildPredictedPoints(stroke); // 用当前笔宽状态推导预测点半径。
				stableDirty = CommitStablePrefixToL1(stroke, liveTipDurationSeconds,
					GetPredictionDurationSeconds(stroke), stableInkColor, shape,
					renderer_, size.width, size.height);

				stroke.lastL0Rect = stroke.currentL0Rect;
				RebuildL0DrawPoints(stroke, liveTipDurationSeconds, shape, size.width, size.height); // 荧光笔只保留预测保护窗口，不做实时笔锋。
				UpdateIdleFreezeState(stroke, rawMoved, liveTipDurationSeconds); // 停笔后视觉稳定则冻结 L0，减少空转。
				DrawL0LiveComposite(stroke, liveInkColor, shape, renderer_);
				UnionRectInPlace(l0FrameDirty, stroke.lastL0Rect); // 旧 L0 区域也要刷新，清掉上一帧预测残影。
				UnionRectInPlace(l0FrameDirty, stroke.currentL0Rect); // 新 L0 区域需要显示最新笔锋。
			}
			else if (forceL0Redraw)
			{
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height, shape);
				DrawL0LiveComposite(stroke, liveInkColor, shape, renderer_);
				UnionRectInPlace(l0FrameDirty, stroke.currentL0Rect);
			}

			RECT frameDirty = {};
			UnionRectInPlace(frameDirty, stableDirty);
			UnionRectInPlace(frameDirty, l0FrameDirty);
			frameDirty = ClampRectToCanvas(frameDirty, size.width, size.height);
			if (!IsEmptyRect(frameDirty))
			{
				const RECT compositeRect = isFirstFrame ? GetFullCanvasRect(size.width, size.height) : frameDirty; // 第一帧强制全量，避免旧 backbuffer 残留。
				CompositeLayersToBackBuffer(compositeRect, tool);
				PresentFrame(compositeRect, isFirstFrame);
				isFirstFrame = false;
			}
			UnionRectInPlace(strokeDirty, stableDirty); // 记录最终需要落盘到 L2 的真实笔迹范围。

			if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) break;
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
			stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height, shape);
			strokeDirty = ClampRectToCanvas(strokeDirty, size.width, size.height);
			isFirstFrame = true;
		}

		const WindowSize finalSize = window_.Size();
		// 抬笔时把最后一帧可见 L0 原样落到 L1，避免笔锋和预测回缩。
		if (!stroke.l0DrawPoints.empty())
		{
			renderer_.SetOMTarget(renderer_.layerL1RTV.Get());
			renderer_.DrawStrokeOrDot(stroke.l0DrawPoints, liveInkColor, shape);
			UnionRectInPlace(strokeDirty, stroke.currentL0Rect);
		}
		strokeDirty = ClampRectToCanvas(strokeDirty, finalSize.width, finalSize.height);
		if (!IsEmptyRect(strokeDirty))
		{
			if (eraser)
				renderer_.ClipResource(renderer_.layerL2RTV.Get(), renderer_.layerL1SRV.Get(), renderer_.layerL0SRV.Get(), strokeDirty); // 合并整笔遮罩后只对 L2 裁除一次。
			else if (highlighter)
				renderer_.BlendCoverageResource(renderer_.layerL2RTV.Get(), renderer_.layerL1SRV.Get(),
					renderer_.layerL0SRV.Get(), kHighlighterCompositeColor, strokeDirty); // 整笔覆盖率只以 35% 红色落入 L2 一次。
			else
				renderer_.AlphaBlendResource(renderer_.layerL2RTV.Get(), renderer_.layerL1SRV.Get(), strokeDirty); // 普通笔仍用 source-over 落到稳定画布。
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
