module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <DirectXMath.h>
#include <iostream>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <memory>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

module draw3.drawing_controller;

import draw3.diagnostics;

namespace draw3
{
	namespace
	{
		const DirectX::XMFLOAT4 kHighlighterCompositeColor(1.0f, 0.0f, 0.0f, 0.35f);
		const DirectX::XMFLOAT4 kMultiContactInkColor(1.0f, 0.0f, 0.0f, 1.0f);
		constexpr float kMultiContactDiameter = 100.0f;
		constexpr float kRawMoveThresholdPx = 0.25f;
		constexpr double kInputSpeedSmoothingSeconds = 0.060;
		constexpr size_t kPreheatedStrokeCount = 16;

		struct RuntimeStroke
		{
			explicit RuntimeStroke(float expectedSpeed)
				: stroke(kMultiContactDiameter, expectedSpeed)
			{
				stroke.modeledResults.reserve(256);
				stroke.predictedResults.reserve(64);
				stroke.realPoints.reserve(256);
				stroke.predictedPoints.reserve(64);
				stroke.l0DrawPoints.reserve(128);
				stroke.previousL0DrawPoints.reserve(128);
				rebuildPoints.reserve(256);
			}

			ActiveStroke stroke;
			ContactHandle handle = {};
			ContactSnapshot lastSpeedSnapshot = {};
			uint64_t lastConsumedSequence = 0;
			int64_t qpcOrigin = 0;
			double lastModelInputTime = 0.0;
			float filteredInputSpeed = 0.0f;
			RECT visibleDirty = {};
			std::vector<InkPoint> rebuildPoints;
			bool inUse = false;
			bool ended = false;
			bool movedThisFrame = false;
			bool hasFilteredInputSpeed = false;
		};

		double QpcDeltaSeconds(int64_t newer, int64_t older, int64_t frequency)
		{
			if (frequency <= 0 || newer <= older) return 0.0;
			return static_cast<double>(newer - older) / static_cast<double>(frequency);
		}

		RECT DrawStablePrefix(RuntimeStroke& runtime, InkRenderer& renderer, int width, int height)
		{
			ActiveStroke& stroke = runtime.stroke;
			if (!stroke.hasCommittedGeometry || stroke.realPoints.empty()) return {};
			const size_t pointCount = std::min(stroke.committedIndex + 1, stroke.realPoints.size());
			if (pointCount == 0) return {};
			runtime.rebuildPoints.assign(stroke.realPoints.begin(), stroke.realPoints.begin() + pointCount);
			renderer.SetOperatorTarget(renderer.layerL1);
			renderer.DrawStrokeOrDot(runtime.rebuildPoints, kMultiContactInkColor);
			return RectFromStrokePoints(runtime.rebuildPoints, width, height);
		}

		RECT DrawCompletedStroke(RuntimeStroke& runtime, InkRenderer& renderer, int width, int height)
		{
			ActiveStroke& stroke = runtime.stroke;
			runtime.rebuildPoints.assign(stroke.realPoints.begin(), stroke.realPoints.end());
			if (runtime.rebuildPoints.empty() && stroke.hasInputStartPoint)
				runtime.rebuildPoints.push_back(stroke.inputStartPoint); // Down 后立即 Up 仍要落下点击圆点。

			RECT dirty = {};
			renderer.SetOperatorTarget(renderer.layerL1);
			if (!runtime.rebuildPoints.empty())
			{
				renderer.DrawStrokeOrDot(runtime.rebuildPoints, kMultiContactInkColor);
				UnionRectInPlace(dirty, RectFromStrokePoints(runtime.rebuildPoints, width, height));
			}
			if (!stroke.previousL0DrawPoints.empty())
			{
				// 冻结上一帧已经可见的尾部，避免最终 Up 重建几何时 prediction 回缩。
				renderer.DrawStrokeOrDot(stroke.previousL0DrawPoints, kMultiContactInkColor);
				UnionRectInPlace(dirty, RectFromStrokePoints(stroke.previousL0DrawPoints, width, height));
			}
			if (!stroke.l0DrawPoints.empty())
			{
				renderer.DrawStrokeOrDot(stroke.l0DrawPoints, kMultiContactInkColor);
				UnionRectInPlace(dirty, RectFromStrokePoints(stroke.l0DrawPoints, width, height));
			}
			return ClampRectToCanvas(dirty, width, height);
		}

		RECT RebuildActiveLayers(const std::vector<RuntimeStroke*>& active,
			InkRenderer& renderer, int width, int height)
		{
			renderer.ClearOperatorLayer(renderer.layerL1);
			renderer.ClearOperatorLayer(renderer.layerL0);
			RECT dirty = {};
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended) continue;
				UnionRectInPlace(dirty, DrawStablePrefix(*runtime, renderer, width, height));
				ActiveStroke& stroke = runtime->stroke;
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, width, height);
				if (!stroke.l0DrawPoints.empty())
					DrawL0LiveComposite(stroke, kMultiContactInkColor,
						StrokeShape::RoundCapsule, renderer, false);
				UnionRectInPlace(dirty, stroke.currentL0Rect);
			}
			return ClampRectToCanvas(dirty, width, height);
		}
	}

	DrawingController::DrawingController(ContactInputCoordinator& input, WindowController& window, InkRenderer& renderer,
		TransparentPresentationController& presentation, StrokeModelConfiguration configuration)
		: input_(input), window_(window), renderer_(renderer),
		presentation_(presentation), configuration_(std::move(configuration))
	{
	}

	void DrawingController::CompositeLayersToBackBuffer(RECT dirty, bool orderLiveOverStable)
	{
		const WindowSize size = window_.Size();
		dirty = ClampRectToCanvas(dirty, size.width, size.height); // 限制脏区，避免纹理复制越界。
		if (IsEmptyRect(dirty)) return;
		renderer_.CopyResource(renderer_.backBufferTexture.Get(), renderer_.layerL2Texture.Get(), dirty); // L2 是已经稳定的底层画布。
		const OperatorLayerMergeMode mergeMode = orderLiveOverStable
			? OperatorLayerMergeMode::Ordered : OperatorLayerMergeMode::CoverageUnion;
		renderer_.ApplyOperatorLayers(renderer_.backBufferRTV.Get(),
			renderer_.layerL1, renderer_.layerL0, dirty, mergeMode);
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
		renderer_.ClearOperatorLayer(renderer_.layerL1); // 清掉当前笔画已确认层。
		renderer_.ClearOperatorLayer(renderer_.layerL0); // 清掉当前帧实时层。
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

	void DrawingController::Run()
	{
		using namespace ink::stroke_model;

		auto strokeModelParams = configuration_.modelParams;
		ApplyPredictionMode(strokeModelParams, configuration_.kalmanPredictorParams);
		LARGE_INTEGER qpcFrequencyValue = {};
		QueryPerformanceFrequency(&qpcFrequencyValue);
		const int64_t qpcFrequency = qpcFrequencyValue.QuadPart;

		std::vector<std::unique_ptr<RuntimeStroke>> strokePool;
		strokePool.reserve(kPreheatedStrokeCount);
		for (size_t index = 0; index < kPreheatedStrokeCount; ++index)
		{
			auto runtime = std::make_unique<RuntimeStroke>(configuration_.expectedSpeed);
			if (absl::Status status = runtime->stroke.modeler.Reset(strokeModelParams); !status.ok())
			{
				std::cout << "Failed to preheat stroke model: " << status.message() << std::endl;
				return;
			}
			strokePool.push_back(std::move(runtime)); // 首批模型和 predictor 在接收 Down 前完成分配。
		}
		std::vector<RuntimeStroke*> active;
		active.reserve(kPreheatedStrokeCount);

		auto acquireStroke = [&]() -> RuntimeStroke*
			{
				for (const auto& candidate : strokePool)
				{
					if (!candidate->inUse)
					{
						candidate->inUse = true;
						return candidate.get();
					}
				}
				auto runtime = std::make_unique<RuntimeStroke>(configuration_.expectedSpeed);
				if (absl::Status status = runtime->stroke.modeler.Reset(strokeModelParams); !status.ok())
				{
					std::cout << "Failed to initialize expanded stroke model: " << status.message() << std::endl;
					return nullptr;
				}
				runtime->inUse = true;
				strokePool.push_back(std::move(runtime));
				return strokePool.back().get();
			};

		auto initializeStroke = [&](ContactHandle handle) -> bool
			{
				if (!handle.record || handle.record->Generation() != handle.generation) return false;
				const ContactSnapshot down = handle.record->DownSnapshot();
				RuntimeStroke* runtime = acquireStroke();
				if (!runtime)
				{
					ContactSnapshot cancelled = down;
					cancelled.phase = ContactPhase::Cancelled;
					input_.PublishCancelled(handle.record->TabletContextId(),
						handle.record->ContactId(), cancelled);
					input_.Recycle(handle);
					return false;
				}
				runtime->handle = handle;
				runtime->ended = false;
				runtime->movedThisFrame = false;
				runtime->visibleDirty = {};
				runtime->stroke.Reset(kMultiContactDiameter, configuration_.expectedSpeed);
				if (absl::Status status = runtime->stroke.modeler.Reset(); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
					ContactSnapshot cancelled = down;
					cancelled.phase = ContactPhase::Cancelled;
					input_.PublishCancelled(handle.record->TabletContextId(),
						handle.record->ContactId(), cancelled);
					input_.Recycle(handle);
					runtime->handle = {};
					runtime->inUse = false;
					return false;
				}

				runtime->lastSpeedSnapshot = down;
				runtime->lastConsumedSequence = down.sequence;
				runtime->qpcOrigin = down.qpc;
				runtime->lastModelInputTime = 0.0;
				runtime->filteredInputSpeed = 0.0f;
				runtime->hasFilteredInputSpeed = false;
				ActiveStroke& stroke = runtime->stroke;
				stroke.inputStartPoint = {
					down.position.x, down.position.y, kMultiContactDiameter * 0.5f, 0.0f };
				stroke.hasInputStartPoint = true;
				const Input downInput{
					.event_type = Input::EventType::kDown,
					.position = Vec2(down.position.x, down.position.y),
					.time = Time(0.0)
				};
				if (absl::Status status = stroke.modeler.Update(downInput, stroke.modeledResults); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
					ContactSnapshot cancelled = down;
					cancelled.phase = ContactPhase::Cancelled;
					input_.PublishCancelled(handle.record->TabletContextId(),
						handle.record->ContactId(), cancelled);
					input_.Recycle(handle);
					runtime->handle = {};
					runtime->inUse = false;
					return false;
				}
				AppendNewModeledPoints(stroke);
				active.push_back(runtime);
				return true;
			};

		auto processCommand = [&](const IngressCommand& command)
			{
				if (command.kind == IngressCommandKind::ControlWake)
				{
					input_.AcknowledgeControlWake(); // 先清 pending，随后复查窗口的全部原子请求。
					return;
				}
				initializeStroke(command.contact);
			};

		auto consumeLatestSnapshot = [&](RuntimeStroke& runtime) -> bool
			{
				ContactSnapshot snapshot;
				if (!input_.TryReadSnapshot(runtime.handle, snapshot) ||
					snapshot.sequence == runtime.lastConsumedSequence) return false;
				runtime.lastConsumedSequence = snapshot.sequence;
				if (snapshot.phase == ContactPhase::Down) return false;

				const float deltaX = snapshot.position.x - runtime.lastSpeedSnapshot.position.x;
				const float deltaY = snapshot.position.y - runtime.lastSpeedSnapshot.position.y;
				const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
				const bool terminal = snapshot.phase == ContactPhase::Up ||
					snapshot.phase == ContactPhase::Cancelled;
				if (!terminal && distanceSquared <= kRawMoveThresholdPx * kRawMoveThresholdPx)
					return false; // Move 抖动已消费但不进入模型，也不改变下一次真实速度基准。

				const double deltaSeconds = QpcDeltaSeconds(
					snapshot.qpc, runtime.lastSpeedSnapshot.qpc, qpcFrequency);
				float inputSpeed = -1.0f;
				if (distanceSquared > kRawMoveThresholdPx * kRawMoveThresholdPx && deltaSeconds > 0.0)
				{
					const float measuredSpeed = std::sqrt(distanceSquared) / static_cast<float>(deltaSeconds);
					if (!runtime.hasFilteredInputSpeed)
					{
						runtime.filteredInputSpeed = measuredSpeed;
						runtime.hasFilteredInputSpeed = true;
					}
					else
					{
						const float alpha = std::clamp(static_cast<float>(
							1.0 - std::exp(-deltaSeconds / kInputSpeedSmoothingSeconds)), 0.02f, 0.35f);
						runtime.filteredInputSpeed +=
							(measuredSpeed - runtime.filteredInputSpeed) * alpha;
					}
					inputSpeed = runtime.filteredInputSpeed; // 每个真实快照先滤速，再交给半径估算器。
				}

				double inputTime = QpcDeltaSeconds(snapshot.qpc, runtime.qpcOrigin, qpcFrequency);
				inputTime = std::max(inputTime, runtime.lastModelInputTime + 0.000001);
				runtime.lastModelInputTime = inputTime;
				const Input input{
					.event_type = terminal ? Input::EventType::kUp : Input::EventType::kMove,
					.position = Vec2(snapshot.position.x, snapshot.position.y),
					.time = Time(inputTime)
				};
				if (absl::Status status = runtime.stroke.modeler.Update(
					input, runtime.stroke.modeledResults); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
					if (terminal)
					{
						const float radius = runtime.stroke.realPoints.empty()
							? kMultiContactDiameter * 0.5f : runtime.stroke.realPoints.back().r;
						const InkPoint finalPoint{ snapshot.position.x, snapshot.position.y,
							radius, static_cast<float>(inputTime) };
						if (runtime.stroke.realPoints.empty())
							runtime.stroke.realPoints.push_back(finalPoint);
						else
						{
							const float finalDeltaX = finalPoint.x - runtime.stroke.realPoints.back().x;
							const float finalDeltaY = finalPoint.y - runtime.stroke.realPoints.back().y;
							if (finalDeltaX * finalDeltaX + finalDeltaY * finalDeltaY > 0.0001f)
								runtime.stroke.realPoints.push_back(finalPoint);
							else
								runtime.stroke.realPoints.back() = finalPoint;
						}
						// 模型异常也保留 RTS 的最终位置，不能因随后回收 contact 而吞掉 Up 点。
					}
				}
				AppendNewModeledPoints(runtime.stroke, inputSpeed);
				runtime.lastSpeedSnapshot = snapshot;
				if (distanceSquared > kRawMoveThresholdPx * kRawMoveThresholdPx)
				{
					runtime.stroke.idleFrozen = false;
					runtime.stroke.visualStableFrameCount = 0;
					runtime.stroke.lastMovementInputTime = inputTime;
				}
				if (terminal) runtime.ended = true;
				return distanceSquared > kRawMoveThresholdPx * kRawMoveThresholdPx;
			};

		bool clearPending = false;
		bool timerPeriodActive = false;
		bool timerPeriodAttempted = false;
		while (true)
		{
			const double frameStartMs = GetQpcTimeMilliseconds();
			IngressCommand command;
			while (input_.TryDequeue(command)) processCommand(command);

			bool forceFullPresent = false;
			if (window_.ConsumeClearCanvasRequest()) clearPending = true;
			if (window_.ConsumeCompositionChangedRequest())
			{
				presentation_.RefreshAfterCompositionChanged();
				window_.RequestFullPresent();
			}
			if (ProcessPendingResize(false))
			{
				const WindowSize size = window_.Size();
				RebuildActiveLayers(active, renderer_, size.width, size.height);
				forceFullPresent = true; // Resize 保留 L2，并从 CPU 状态恢复共享 L1/L0。
			}
			if (window_.ConsumeFullPresentRequest()) forceFullPresent = true;
			if (window_.ExitRequested()) break;

			RECT frameDirty = {};
			if (active.empty() && clearPending)
			{
				const WindowSize size = window_.Size();
				frameDirty = GetFullCanvasRect(size.width, size.height);
				renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor);
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				renderer_.ClearRTV(renderer_.backBufferRTV.Get(), kTransparentLayerClearColor);
				clearPending = false;
				forceFullPresent = true;
			}

			if (active.empty() && !forceFullPresent)
			{
				if (timerPeriodActive)
				{
					timeEndPeriod(1);
					timerPeriodActive = false;
				}
				timerPeriodAttempted = false;
				// 二次排空后才等待；竞态窗口内到达的命令会留下信号量计数。
				if (input_.TryDequeue(command))
				{
					processCommand(command);
					continue;
				}
				input_.WaitDequeue(command);
				processCommand(command);
				continue;
			}

			if (!active.empty() && !timerPeriodAttempted)
			{
				timerPeriodAttempted = true;
				timerPeriodActive = timeBeginPeriod(1) == TIMERR_NOERROR; // 只为成功的请求配对 timeEndPeriod。
			}

			LARGE_INTEGER frameQpc = {};
			QueryPerformanceCounter(&frameQpc);
			bool hasEndedStroke = false;
			for (RuntimeStroke* runtime : active)
			{
				runtime->movedThisFrame = consumeLatestSnapshot(*runtime);
				hasEndedStroke = hasEndedStroke || runtime->ended;
			}

			const WindowSize size = window_.Size();
			for (RuntimeStroke* runtime : active)
			{
				ActiveStroke& stroke = runtime->stroke;
				stroke.lastL0Rect = stroke.currentL0Rect;
				stroke.logicalInputTime = std::max(stroke.logicalInputTime,
					QpcDeltaSeconds(frameQpc.QuadPart, runtime->qpcOrigin, qpcFrequency));

				RECT stableDirty = {};
				if (!runtime->ended)
				{
					stroke.predictedResults.clear();
					if (kActivePredictionMode != InkPredictionMode::Disabled)
					{
						if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
							stroke.predictedResults.clear();
					}
					RebuildPredictedPoints(stroke);
					stableDirty = CommitStablePrefixToL1(stroke,
						configuration_.liveTipDurationSeconds, GetPredictionDurationSeconds(stroke),
						kMultiContactInkColor, StrokeShape::RoundCapsule,
						renderer_, size.width, size.height);
				}
				// 结束帧保留上一帧 prediction，同时接入最终 Up 的真实几何。
				RebuildL0DrawPoints(stroke, configuration_.liveTipDurationSeconds,
					StrokeShape::RoundCapsule, size.width, size.height);
				if (!runtime->ended)
					UpdateIdleFreezeState(stroke, runtime->movedThisFrame,
						configuration_.liveTipDurationSeconds);

				UnionRectInPlace(frameDirty, stableDirty);
				UnionRectInPlace(frameDirty, stroke.lastL0Rect);
				UnionRectInPlace(frameDirty, stroke.currentL0Rect);
				UnionRectInPlace(runtime->visibleDirty, stableDirty);
				UnionRectInPlace(runtime->visibleDirty, stroke.lastL0Rect);
				UnionRectInPlace(runtime->visibleDirty, stroke.currentL0Rect);
			}

			if (hasEndedStroke)
			{
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				RECT completedDirty = {};
				for (RuntimeStroke* runtime : active)
				{
					if (!runtime->ended) continue;
					UnionRectInPlace(completedDirty,
						DrawCompletedStroke(*runtime, renderer_, size.width, size.height));
					UnionRectInPlace(frameDirty, runtime->visibleDirty);
				}
				completedDirty = ClampRectToCanvas(completedDirty, size.width, size.height);
				if (!IsEmptyRect(completedDirty))
				{
					// 所有同帧结束 contact 共用一次 resolve，L2 从不接收仍活动的几何。
					renderer_.ApplyOperatorLayers(renderer_.layerL2RTV.Get(),
						renderer_.layerL1, renderer_.layerL0, completedDirty);
					UnionRectInPlace(frameDirty, completedDirty);
				}
				UnionRectInPlace(frameDirty,
					RebuildActiveLayers(active, renderer_, size.width, size.height));

				std::erase_if(active, [&](RuntimeStroke* runtime)
					{
						if (!runtime->ended) return false;
						input_.Recycle(runtime->handle); // L2 提交与活动层重建完成后才归还 slot。
						runtime->stroke.Reset(kMultiContactDiameter, configuration_.expectedSpeed);
						runtime->handle = {};
						runtime->visibleDirty = {};
						runtime->ended = false;
						runtime->inUse = false;
						return true;
					});
			}
			else if (!active.empty())
			{
				renderer_.ClearOperatorLayer(renderer_.layerL0); // 共享 L0 每帧只恢复一次单位操作。
				for (RuntimeStroke* runtime : active)
				{
					if (!runtime->stroke.l0DrawPoints.empty())
						DrawL0LiveComposite(runtime->stroke, kMultiContactInkColor,
							StrokeShape::RoundCapsule, renderer_, false);
				}
			}

			if (active.empty() && clearPending)
			{
				frameDirty = GetFullCanvasRect(size.width, size.height);
				renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor);
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				renderer_.ClearRTV(renderer_.backBufferRTV.Get(), kTransparentLayerClearColor);
				clearPending = false;
				forceFullPresent = true;
			}

			frameDirty = ClampRectToCanvas(frameDirty, size.width, size.height);
			if (forceFullPresent) frameDirty = GetFullCanvasRect(size.width, size.height);
			if (!IsEmptyRect(frameDirty))
			{
				CompositeLayersToBackBuffer(frameDirty);
				PresentFrame(frameDirty, forceFullPresent); // 一帧最多一次 backbuffer 合成和一次 Present。
			}

			if (!active.empty())
			{
				const double workMs = GetQpcTimeMilliseconds() - frameStartMs;
				HighPrecisionWait(workMs, configuration_.timingProfile.target_fps);
			}
		}

		if (timerPeriodActive) timeEndPeriod(1);
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
		const float baseDiameter = tool == DrawingTool::Pen ? 100.0f : 50.0f; // 普通笔暂用 100px 测试；荧光笔和橡皮仍保持 50px。
		const StrokeShape shape = StrokeShape::RoundCapsule;
		const DirectX::XMFLOAT4 stableInkColor = highlighter
			? kHighlighterCompositeColor
			: eraser ? kTransparentLayerClearColor
			: DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
		const DirectX::XMFLOAT4 liveInkColor = tool == DrawingTool::Pen &&
			kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer
			? DirectX::XMFLOAT4(0.0f, 0.35f, 1.0f, 1.0f)
			: stableInkColor;
		const double liveTipDurationSeconds = eraser || highlighter ? 0.0 : configuration_.liveTipDurationSeconds;
		const StrokeWidthMode widthMode = tool == DrawingTool::Pen
			? StrokeWidthMode::SimulatedPressure : StrokeWidthMode::Fixed;
		const bool orderedPreview = tool == DrawingTool::Pen &&
			kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer;

		ActiveStroke stroke(baseDiameter, configuration_.expectedSpeed, widthMode, highlighter);
		if (absl::Status status = stroke.modeler.Reset(strokeModelParams); !status.ok()) // 每一笔都用干净的模型状态开始。
		{
			std::cout << "Error: " << status.message() << std::endl;
		}
		const float originX = static_cast<float>(startMessage.x);
		const float originY = static_cast<float>(startMessage.y);
		stroke.inputStartPoint = { originX, originY, baseDiameter * 0.5f, 0.0f };
		stroke.hasInputStartPoint = true; // 最短矩形严格锚定 WM_LBUTTONDOWN 的输入点，而不是预测或平滑后的端点。
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
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = eraser ? RECT{}
					: highlighter
						? ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, size.width, size.height)
						: RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height, shape);
				strokeDirty = ClampRectToCanvas(strokeDirty, size.width, size.height);
				isFirstFrame = true;
				forceL0Redraw = !eraser; // 橡皮的稳定 L1 已由 Resize 保留，不存在需要重建的 L0。
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
				if (eraser)
				{
					// 橡皮没有预测和实时笔锋，所有新增真实几何直接落到 L1。
					stroke.predictedPoints.clear();
					stableDirty = CommitEraserRealPointsToL1(
						stroke, shape, renderer_, size.width, size.height);
					UpdateIdleFreezeState(stroke, rawMoved, 0.0);
				}
				else
				{
					if ((!highlighter || stroke.realPathLength >= kHighlighterMinimumStrokeLengthPx) &&
						kActivePredictionMode != InkPredictionMode::Disabled)
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
			}
			else if (forceL0Redraw)
			{
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = highlighter
					? ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, size.width, size.height)
					: RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height, shape);
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
				CompositeLayersToBackBuffer(compositeRect, orderedPreview);
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
			stroke.currentL0Rect = eraser ? RECT{}
				: highlighter
					? ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, size.width, size.height)
					: RectFromStrokePoints(stroke.l0DrawPoints, size.width, size.height, shape);
			strokeDirty = ClampRectToCanvas(strokeDirty, size.width, size.height);
			isFirstFrame = true;
		}

		const WindowSize finalSize = window_.Size();
		if (highlighter && stroke.realPathLength < kHighlighterMinimumStrokeLengthPx)
		{
			const RECT oldLiveRect = stroke.currentL0Rect;
			stroke.predictedResults.clear();
			stroke.predictedPoints.clear(); // 短划只由真实路径分类，抬笔时彻底丢弃预测后缀。
			stroke.l0DrawPoints = stroke.realPoints;
			const HighlighterStartDirectionState finalDirection = GetHighlighterShortStrokeDirectionState(stroke);
			const InkPoint shortMarkStart = stroke.hasInputStartPoint
				? stroke.inputStartPoint : InkPoint{ originX, originY, baseDiameter * 0.5f, 0.0f };
			const std::vector<InkPoint> shortMarkPoints = { shortMarkStart };
			stroke.l0HighlighterGeometry = BuildHighlighterGeometry(shortMarkPoints,
				HighlighterBoundaryFlags::Start | HighlighterBoundaryFlags::End, true, finalDirection);
			stroke.currentL0Rect = ClampRectToCanvas(
				stroke.l0HighlighterGeometry.bounds, finalSize.width, finalSize.height);
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			if (!stroke.l0HighlighterGeometry.primitives.empty())
			{
				renderer_.SetOperatorTarget(renderer_.layerL0);
				renderer_.DrawHighlighterPrimitives(stroke.l0HighlighterGeometry.primitives, liveInkColor);
			}
			UnionRectInPlace(strokeDirty, oldLiveRect); // 最终呈现同时清除按住期间较短的实时预览。
		}
		// 抬笔时把最后一帧可见 L0 原样落到 L1，避免笔锋和预测回缩。
		const bool hasFinalLiveGeometry = !eraser && (highlighter
			? !stroke.l0HighlighterGeometry.primitives.empty() : !stroke.l0DrawPoints.empty());
		if (hasFinalLiveGeometry)
		{
			renderer_.SetOperatorTarget(renderer_.layerL1);
			if (highlighter)
				renderer_.DrawHighlighterPrimitives(stroke.l0HighlighterGeometry.primitives, liveInkColor);
			else
				renderer_.DrawStrokeOrDot(stroke.l0DrawPoints, liveInkColor, shape);
			UnionRectInPlace(strokeDirty, stroke.currentL0Rect);
		}
		strokeDirty = ClampRectToCanvas(strokeDirty, finalSize.width, finalSize.height);
		if (!IsEmptyRect(strokeDirty))
		{
			renderer_.ApplyOperatorLayers(renderer_.layerL2RTV.Get(),
				renderer_.layerL1, renderer_.layerL0, strokeDirty); // 工具语义已经编码在 Add/Retain 中。
			renderer_.ClearOperatorLayer(renderer_.layerL1); // L1/L0 都是本笔临时层，结束后恢复单位操作。
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			const RECT finalPresentRect = isFirstFrame ? GetFullCanvasRect(finalSize.width, finalSize.height) : strokeDirty;
			renderer_.CopyResource(renderer_.backBufferTexture.Get(), renderer_.layerL2Texture.Get(), finalPresentRect);
			PresentFrame(finalPresentRect, isFirstFrame);
		}
		else
		{
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			if (isFirstFrame) PresentFullCanvas();
		}
		window_.FlushMouseMessages();
	}
}
