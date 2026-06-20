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
		dirty = ClampRectToCanvas(dirty, size.width, size.height);
		if (IsEmptyRect(dirty)) return;
		renderer_.CopyResource(renderer_.backBufferTexture, renderer_.layerL2Texture, dirty);
		renderer_.AlphaBlendResource(renderer_.backBufferRTV, renderer_.layerL1SRV, dirty);
		renderer_.AlphaBlendResource(renderer_.backBufferRTV, renderer_.layerL0SRV, dirty);
	}

	bool DrawingController::PresentFrame(RECT dirty, bool presentFull)
	{
		const bool succeeded = presentation_.Present(dirty, presentFull);
		if (!succeeded) window_.RequestFullPresent();
		return succeeded;
	}

	void DrawingController::ClearCanvas()
	{
		const WindowSize size = window_.Size();
		const RECT fullCanvas = GetFullCanvasRect(size.width, size.height);
		renderer_.ClearRTV(renderer_.layerL2RTV, presentation_.WindowBackgroundColor());
		renderer_.ClearRTV(renderer_.layerL1RTV, kTransparentLayerClearColor);
		renderer_.ClearRTV(renderer_.layerL0RTV, kTransparentLayerClearColor);
		renderer_.ClearRTV(renderer_.backBufferRTV, presentation_.WindowBackgroundColor());
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
		if (!window_.ConsumeResizeRequest(requestedSize)) return false;
		const WindowSize oldSize = window_.Size();
		if (requestedSize.width <= 0 || requestedSize.height <= 0 ||
			(requestedSize.width == oldSize.width && requestedSize.height == oldSize.height)) return false;

		if (!renderer_.Resize(presentation_.SwapChain(), requestedSize.width, requestedSize.height))
		{
			std::cout << "Failed to resize D3D resources to " << requestedSize.width << "x" << requestedSize.height << std::endl;
			return false;
		}
		if (!presentation_.Resize(requestedSize.width, requestedSize.height))
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

		ApplyPredictionMode(configuration_.modelParams, configuration_.kalmanPredictorParams);
		const float baseDiameter = eraser ? 50.0f : 5.0f;
		const float shapeType = static_cast<float>(window_.BrushShapeType());
		const DirectX::XMFLOAT4 stableInkColor(1.0f, 0.0f, 0.0f, 1.0f);
		const DirectX::XMFLOAT4 liveInkColor = kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer
			? DirectX::XMFLOAT4(0.0f, 0.35f, 1.0f, 1.0f)
			: stableInkColor;

		ActiveMouseStroke stroke(baseDiameter, configuration_.expectedSpeed);
		if (absl::Status status = stroke.modeler.Reset(configuration_.modelParams); !status.ok())
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
		AppendNewModeledPoints(stroke);
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
				stroke.lastL0Rect = {};
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
			stroke.lastFrameWallTime = wallElapsedSeconds;
			const bool rawMoved = UpdateRawPositionAndDetectMovement(stroke, cursorPosition);
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
				AppendNewModeledPoints(stroke);

				stroke.predictedResults.clear();
				if (kActivePredictionMode != InkPredictionMode::Disabled)
				{
					if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
						stroke.predictedResults.clear();
				}
				RebuildPredictedPoints(stroke);
				stableDirty = CommitStablePrefixToL1(stroke, configuration_.liveTipDurationSeconds,
					GetPredictionDurationSeconds(stroke), stableInkColor, shapeType, eraser,
					renderer_, size.width, size.height);

				stroke.lastL0Rect = stroke.currentL0Rect;
				RebuildL0DrawPoints(stroke, configuration_.liveTipDurationSeconds, size.width, size.height);
				UpdateIdleFreezeState(stroke, rawMoved, configuration_.liveTipDurationSeconds);
				DrawL0LiveComposite(stroke, liveInkColor, shapeType, eraser, renderer_);
				UnionRectInPlace(l0FrameDirty, stroke.lastL0Rect);
				UnionRectInPlace(l0FrameDirty, stroke.currentL0Rect);
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
				const RECT compositeRect = isFirstFrame ? GetFullCanvasRect(size.width, size.height) : frameDirty;
				CompositeLayersToBackBuffer(compositeRect);
				PresentFrame(compositeRect, isFirstFrame);
				isFirstFrame = false;
			}
			UnionRectInPlace(strokeDirty, stableDirty);

			if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) break;
			window_.FlushMouseMessages();
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
			renderer_.SetOMTarget(renderer_.layerL1RTV);
			renderer_.DrawStrokeOrDot(stroke.l0DrawPoints, liveInkColor, shapeType, eraser);
			UnionRectInPlace(strokeDirty, stroke.currentL0Rect);
		}
		strokeDirty = ClampRectToCanvas(strokeDirty, finalSize.width, finalSize.height);
		if (!IsEmptyRect(strokeDirty))
		{
			renderer_.AlphaBlendResource(renderer_.layerL2RTV, renderer_.layerL1SRV, strokeDirty);
			renderer_.ClearRTV(renderer_.layerL1RTV, kTransparentLayerClearColor);
			renderer_.ClearRTV(renderer_.layerL0RTV, kTransparentLayerClearColor);
			const RECT finalPresentRect = isFirstFrame ? GetFullCanvasRect(finalSize.width, finalSize.height) : strokeDirty;
			renderer_.CopyResource(renderer_.backBufferTexture, renderer_.layerL2Texture, finalPresentRect);
			PresentFrame(finalPresentRect, isFirstFrame);
		}
		else
		{
			renderer_.ClearRTV(renderer_.layerL0RTV, kTransparentLayerClearColor);
			if (isFirstFrame) PresentFullCanvas();
		}
		window_.FlushMouseMessages();
	}
}
