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
#include <limits>
#include <memory>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

module draw3.drawing_controller;

import draw3.diagnostics;
import draw3.haptic_feedback;
import draw3.pen_cursor;

namespace draw3
{
	namespace
	{
		const DirectX::XMFLOAT4 kHighlighterCompositeColor(1.0f, 0.0f, 0.0f, 0.35f);
		const DirectX::XMFLOAT4 kMultiContactInkColor(1.0f, 0.0f, 0.0f, 1.0f);
		const DirectX::XMFLOAT4 kReconnectManualTestColor(0.0f, 1.0f, 0.0f, 1.0f);
		constexpr float kPenDiameter = 5.0f;
		constexpr float kMinimumPenCursorDiameterAt96Dpi = 5.0f;
		constexpr float kWideToolDiameter = 50.0f;
		constexpr float kReconnectManualTestRadiusPx = 4.0f;
		constexpr float kRawMoveThresholdPx = 0.25f;
		constexpr float kStylusPressureEpsilon = 0.0001f;
		constexpr float kStylusAngleEpsilon = 0.0001f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kHalfPi = kPi * 0.5f;
		constexpr float kTwoPi = kPi * 2.0f;
		constexpr double kInputSpeedSmoothingSeconds = 0.060;
		constexpr size_t kPreheatedStrokeCount = 16;

		float DiameterForTool(DrawingTool tool)
		{
			return tool == DrawingTool::Pen ? kPenDiameter : kWideToolDiameter;
		}

		StrokeWidthMode WidthModeForTool(DrawingTool tool)
		{
			return tool == DrawingTool::Pen
				? StrokeWidthMode::SimulatedPressure : StrokeWidthMode::Fixed;
		}

		bool HasLinearStylusChange(float current, float previous, float epsilon, float maximum) noexcept
		{
			if (!std::isfinite(current) || current < 0.0f || current > maximum) return false;
			return !std::isfinite(previous) || previous < 0.0f ||
				std::abs(current - previous) > epsilon;
		}

		bool HasOrientationChange(float current, float previous) noexcept
		{
			if (!std::isfinite(current) || current < 0.0f || current >= kTwoPi) return false;
			if (!std::isfinite(previous) || previous < 0.0f || previous >= kTwoPi) return true;
			const float difference = std::abs(current - previous);
			return std::min(difference, kTwoPi - difference) > kStylusAngleEpsilon;
		}

		bool HasStylusStateChange(const ContactSnapshot& current,
			const ContactSnapshot& previous) noexcept
		{
			return HasLinearStylusChange(current.pressure, previous.pressure,
				kStylusPressureEpsilon, 1.0f) ||
				HasLinearStylusChange(current.tilt, previous.tilt, kStylusAngleEpsilon, kHalfPi) ||
				HasOrientationChange(current.orientation, previous.orientation);
		}

		float KeepLastValidStylusValue(float value, float maximum, float& lastValue) noexcept
		{
			if (std::isfinite(value) && value >= 0.0f && value <= maximum)
				lastValue = value;
			return lastValue;
		}

		float KeepLastValidOrientation(float value, float& lastValue) noexcept
		{
			if (std::isfinite(value) && value >= 0.0f && value < kTwoPi)
				lastValue = value;
			return lastValue;
		}

		DirectX::XMFLOAT4 ColorForTool(DrawingTool tool)
		{
			if (tool == DrawingTool::Highlighter) return kHighlighterCompositeColor;
			if (tool == DrawingTool::Eraser) return kTransparentLayerClearColor;
			return kMultiContactInkColor;
		}

		struct ReconnectManualTestRange
		{
			size_t firstPointIndex = 0;
			size_t lastPointIndex = 0;
		};

		struct RuntimeStroke
		{
			explicit RuntimeStroke(float expectedSpeed)
				: stroke(kPenDiameter, expectedSpeed)
			{
				stroke.modeledResults.reserve(256);
				stroke.predictedResults.reserve(64);
				stroke.realPoints.reserve(256);
				stroke.predictedPoints.reserve(64);
				stroke.l0DrawPoints.reserve(128);
				stroke.previousL0DrawPoints.reserve(128);
				rebuildPoints.reserve(256);
				reconnectPredictedResults.reserve(64);
			}

			ActiveStroke stroke;
			ContactHandle handle = {};
			DrawingTool selectedTool = DrawingTool::Pen;
			DrawingTool tool = DrawingTool::Pen;
			bool suppressPressure = false;
			ContactSnapshot lastSpeedSnapshot = {};
			ContactSnapshot lastModelSnapshot = {};
			uint64_t lastConsumedSequence = 0;
			int64_t qpcOrigin = 0;
			double lastModelInputTime = 0.0;
			float filteredInputSpeed = 0.0f;
			float lastPressure = -1.0f;
			float lastTilt = -1.0f;
			float lastOrientation = -1.0f;
			RECT visibleDirty = {};
			std::vector<InkPoint> rebuildPoints;
			std::vector<ink::stroke_model::Result> reconnectPredictedResults;
			std::vector<ReconnectManualTestRange> reconnectManualTestRanges;
			InputDeviceType metricDeviceType = InputDeviceType::Touch;
			int64_t metricEligibleQpc = 0;
			bool inUse = false;
			bool ended = false;
			bool cancelled = false;
			bool metricVisible = false;
			bool movedThisFrame = false;
			bool hasFilteredInputSpeed = false;
			bool invertedCursor = false;
			bool hapticEligible = false;
			bool awaitingReconnect = false;
			bool reconnectVisualRefresh = false;
			ContactSnapshot deferredUpSnapshot = {};
			DirectX::XMFLOAT2 reconnectDirection = {};
			int64_t reconnectDeadlineQpc = 0;
		};

		const char* InputDeviceTypeName(InputDeviceType deviceType) noexcept
		{
			switch (deviceType)
			{
			case InputDeviceType::Pen: return "Pen";
			case InputDeviceType::MouseLeft: return "MouseLeft";
			case InputDeviceType::MouseRight: return "MouseRight";
			default: return "Touch";
			}
		}

		const char* DrawingToolName(DrawingTool tool) noexcept
		{
			switch (tool)
			{
			case DrawingTool::Highlighter: return "Highlighter";
			case DrawingTool::Eraser: return "Eraser";
			default: return "Pen";
			}
		}

		const char* ReconnectMotionSourceName(
			InterruptedStrokeReconnectMotionSource source) noexcept
		{
			switch (source)
			{
			case InterruptedStrokeReconnectMotionSource::PredictionPosition:
				return "prediction_position";
			case InterruptedStrokeReconnectMotionSource::RealTail:
				return "real_tail";
			default:
				return "none";
			}
		}

		const char* ReconnectRejectReasonName(
			InterruptedStrokeReconnectRejectReason reason) noexcept
		{
			switch (reason)
			{
			case InterruptedStrokeReconnectRejectReason::IdentityMismatch:
				return "identity";
			case InterruptedStrokeReconnectRejectReason::InvalidTime:
				return "invalid_time";
			case InterruptedStrokeReconnectRejectReason::WindowExpired:
				return "window";
			case InterruptedStrokeReconnectRejectReason::InvalidSpeed:
				return "invalid_speed";
			case InterruptedStrokeReconnectRejectReason::InvalidDirection:
				return "invalid_direction";
			case InterruptedStrokeReconnectRejectReason::Distance:
				return "distance";
			case InterruptedStrokeReconnectRejectReason::ForecastError:
				return "forecast_error";
			case InterruptedStrokeReconnectRejectReason::Angle:
				return "angle";
			default:
				return "none";
			}
		}

		InterruptedStrokeReconnectIdentity ReconnectIdentity(const RuntimeStroke& runtime) noexcept
		{
			return {
				runtime.metricDeviceType,
				static_cast<uint32_t>(runtime.selectedTool),
				static_cast<uint32_t>(runtime.tool),
				runtime.stroke.widthMode,
				runtime.invertedCursor,
				runtime.suppressPressure
			};
		}

		bool HasPhysicalContact(const std::vector<RuntimeStroke*>& active) noexcept
		{
			return std::any_of(active.begin(), active.end(), [](const RuntimeStroke* runtime)
				{
					return runtime && !runtime->ended && !runtime->awaitingReconnect;
				});
		}

		HapticToolFeedback HapticToolForDrawingTool(DrawingTool tool) noexcept
		{
			switch (tool)
			{
			case DrawingTool::Highlighter:
				return HapticToolFeedback::Highlighter;
			case DrawingTool::Eraser:
				return HapticToolFeedback::Eraser;
			default:
				return HapticToolFeedback::Pen;
			}
		}

		HapticContinuousFeedback HapticFeedbackForRuntime(
			const RuntimeStroke& runtime) noexcept
		{
			// 部分笔固件虽枚举 0x1010，却不会驱动笔尾；倒转笔尾使用必需的 InkContinuous。
			if (runtime.invertedCursor)
				return HapticContinuousFeedback::InkContinuous;
			return ResolveContinuousHapticFeedback(
				HapticToolForDrawingTool(runtime.tool));
		}

		double QpcDeltaSeconds(int64_t newer, int64_t older, int64_t frequency)
		{
			if (frequency <= 0 || newer <= older) return 0.0;
			return static_cast<double>(newer - older) / static_cast<double>(frequency);
		}

		RECT DrawReconnectManualTestRanges(RuntimeStroke& runtime, InkRenderer& renderer,
			int width, int height)
		{
			if constexpr (!kInterruptedStrokeReconnectManualTestModeEnabled) return {};
			if (runtime.reconnectManualTestRanges.empty() || runtime.stroke.realPoints.empty()) return {};

			renderer.SetOperatorTarget(renderer.layerL1);
			RECT dirty = {};
			for (const ReconnectManualTestRange& range : runtime.reconnectManualTestRanges)
			{
				const size_t firstIndex = std::min(range.firstPointIndex,
					runtime.stroke.realPoints.size());
				const size_t lastIndex = std::min(range.lastPointIndex,
					runtime.stroke.realPoints.size());
				if (lastIndex <= firstIndex) continue;
				runtime.rebuildPoints.assign(runtime.stroke.realPoints.begin() + firstIndex,
					runtime.stroke.realPoints.begin() + lastIndex);
				for (InkPoint& point : runtime.rebuildPoints)
					point.r = std::min(point.r, kReconnectManualTestRadiusPx);
				renderer.DrawStrokeOrDot(runtime.rebuildPoints, kReconnectManualTestColor);
				UnionRectInPlace(dirty,
					RectFromStrokePoints(runtime.rebuildPoints, width, height));
			}
			return ClampRectToCanvas(dirty, width, height);
		}

		RECT DrawStablePrefix(RuntimeStroke& runtime, InkRenderer& renderer, int width, int height)
		{
			ActiveStroke& stroke = runtime.stroke;
			if (!stroke.hasCommittedGeometry) return {};
			if (runtime.tool == DrawingTool::Highlighter)
			{
				if (stroke.committedHighlighterGeometry.primitives.empty()) return {};
				renderer.SetOperatorTarget(renderer.layerL1);
				renderer.DrawHighlighterPrimitives(stroke.committedHighlighterGeometry.primitives,
					ColorForTool(runtime.tool));
				return ClampRectToCanvas(stroke.committedHighlighterGeometry.bounds, width, height);
			}
			if (stroke.realPoints.empty())
			{
				if (runtime.tool != DrawingTool::Eraser || !stroke.hasInputStartPoint) return {};
				runtime.rebuildPoints.assign(1, stroke.inputStartPoint);
			}
			else
			{
				const size_t pointCount = std::min(stroke.committedIndex + 1, stroke.realPoints.size());
				if (pointCount == 0) return {};
				runtime.rebuildPoints.assign(stroke.realPoints.begin(), stroke.realPoints.begin() + pointCount);
			}
			renderer.SetOperatorTarget(renderer.layerL1);
			const InkOperatorKind operatorKind = runtime.tool == DrawingTool::Eraser
				? InkOperatorKind::Erase : InkOperatorKind::Draw;
			renderer.DrawStrokeOrDot(runtime.rebuildPoints, ColorForTool(runtime.tool),
				StrokeShape::RoundCapsule, operatorKind);
			return RectFromStrokePoints(runtime.rebuildPoints, width, height);
		}

		RECT DrawCompletedStroke(RuntimeStroke& runtime, InkRenderer& renderer, int width, int height,
			bool retainPredictionOnUp)
		{
			ActiveStroke& stroke = runtime.stroke;
			RECT dirty = {};
			renderer.SetOperatorTarget(renderer.layerL1);
			if (runtime.tool == DrawingTool::Highlighter)
			{
				HighlighterGeometry geometry = MergeHighlighterGeometry(
					stroke.committedHighlighterGeometry, stroke.l0HighlighterGeometry);
				if (geometry.primitives.empty())
				{
					std::vector<InkPoint> clickPoints;
					if (stroke.hasInputStartPoint)
						clickPoints.push_back(stroke.inputStartPoint);
					else if (!stroke.realPoints.empty())
						clickPoints.push_back(stroke.realPoints.front());
					geometry = BuildHighlighterGeometry(clickPoints); // 同帧 Down→Up 尚未生成 L0 时只补一次点击矩形。
				}
				if (geometry.primitives.empty()) return {};
				// 已可见笔画只重放缓存的稳定前缀和最后一帧 live 几何，不回扫 realPoints。
				renderer.DrawHighlighterPrimitives(geometry.primitives, ColorForTool(runtime.tool));
				return ClampRectToCanvas(geometry.bounds, width, height);
			}
			runtime.rebuildPoints.assign(stroke.realPoints.begin(), stroke.realPoints.end());
			if (runtime.rebuildPoints.empty() && stroke.hasInputStartPoint)
				runtime.rebuildPoints.push_back(stroke.inputStartPoint); // Down 后立即 Up 仍要落下点击圆点。
			if (runtime.tool == DrawingTool::Pen)
			{
				if (stroke.hasCommittedGeometry && !stroke.realPoints.empty())
				{
					const size_t stablePointCount =
						std::min(stroke.committedIndex + 1, stroke.realPoints.size());
					runtime.rebuildPoints.assign(
						stroke.realPoints.begin(), stroke.realPoints.begin() + stablePointCount);
					renderer.DrawStrokeOrDot(runtime.rebuildPoints, ColorForTool(runtime.tool));
					UnionRectInPlace(dirty,
						RectFromStrokePoints(runtime.rebuildPoints, width, height));
				}
				BuildCompletedPenTail(stroke, retainPredictionOnUp, runtime.rebuildPoints);
			}
			if (!runtime.rebuildPoints.empty())
			{
				const InkOperatorKind operatorKind = runtime.tool == DrawingTool::Eraser
					? InkOperatorKind::Erase : InkOperatorKind::Draw;
				renderer.DrawStrokeOrDot(runtime.rebuildPoints, ColorForTool(runtime.tool),
					StrokeShape::RoundCapsule, operatorKind);
				UnionRectInPlace(dirty, RectFromStrokePoints(runtime.rebuildPoints, width, height));
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
				if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
					UnionRectInPlace(dirty,
						DrawReconnectManualTestRanges(*runtime, renderer, width, height));
				ActiveStroke& stroke = runtime->stroke;
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = runtime->tool == DrawingTool::Highlighter
					? ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, width, height)
					: RectFromStrokePoints(stroke.l0DrawPoints, width, height);
				if (runtime->tool != DrawingTool::Eraser && !stroke.l0DrawPoints.empty())
					DrawL0LiveComposite(stroke, ColorForTool(runtime->tool),
						StrokeShape::RoundCapsule, renderer, false);
				UnionRectInPlace(dirty, stroke.currentL0Rect);
			}
			return ClampRectToCanvas(dirty, width, height);
		}
	}

	DrawingController::DrawingController(ContactInputCoordinator& input, WindowController& window, InkRenderer& renderer,
		TransparentPresentationController& presentation, StrokeModelConfiguration configuration,
		RuntimeMetricsSession* metrics, PenHapticFeedback* haptics)
		: input_(input), window_(window), renderer_(renderer),
		presentation_(presentation), configuration_(std::move(configuration)),
		inputWidthModeSettings_(configuration_.inputWidthModes),
		invertedPenEraserEnabled_(configuration_.invertedPenEraserEnabled), metrics_(metrics),
		haptics_(haptics)
	{
		const DirectX::XMFLOAT4 penColor = ColorForTool(DrawingTool::Pen);
		const float penCursorDiameter = std::max(
			kPenDiameter, kMinimumPenCursorDiameterAt96Dpi * configuration_.dpiScale);
		window_.ConfigureDrawingCursor(DrawingTool::Pen, {
			DrawingCursorShape::Circle,
			penCursorDiameter,
			penCursorDiameter,
			penColor.x,
			penColor.y,
			penColor.z
		});
		const DirectX::XMFLOAT4 highlighterColor = ColorForTool(DrawingTool::Highlighter);
		window_.ConfigureDrawingCursor(DrawingTool::Highlighter, {
			DrawingCursorShape::Rectangle,
			kWideToolDiameter / kHighlighterNibAspectRatio,
			kWideToolDiameter,
			highlighterColor.x,
			highlighterColor.y,
			highlighterColor.z
		});
		// 橡皮实际模型使用画布像素宽度，光标不能再次乘 DPI。
		const float eraserCursorDiameter = kWideToolDiameter;
		DrawingCursorAppearance eraserAppearance = {
			DrawingCursorShape::EraserGripCircle,
			eraserCursorDiameter,
			eraserCursorDiameter,
			1.0f, 1.0f, 1.0f
		};
		eraserAppearance.opacity = 0.5f;
		eraserAppearance.fillAlpha = 1.0f;
		eraserAppearance.outlineWidth = eraserCursorDiameter * 0.04f;
		eraserAppearance.outlineRed = 207.0f / 255.0f;
		eraserAppearance.outlineGreen = 207.0f / 255.0f;
		eraserAppearance.outlineBlue = 207.0f / 255.0f;
		window_.ConfigureDrawingCursor(DrawingTool::Eraser, eraserAppearance);
		if (haptics_) haptics_->SetEnabled(configuration_.hapticFeedbackEnabled);
	}

	bool DrawingController::SetInputWidthModeSettings(InputWidthModeSettings settings) noexcept
	{
		return inputWidthModeSettings_.Set(settings);
	}

	InputWidthModeSettings DrawingController::GetInputWidthModeSettings() const noexcept
	{
		return inputWidthModeSettings_.Get();
	}

	void DrawingController::SetInvertedPenEraserEnabled(bool enabled) noexcept
	{
		invertedPenEraserEnabled_.store(enabled, std::memory_order_release);
	}

	bool DrawingController::GetInvertedPenEraserEnabled() const noexcept
	{
		return invertedPenEraserEnabled_.load(std::memory_order_acquire);
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
		const double presentStartMs = GetQpcTimeMilliseconds();
		const bool succeeded = presentation_.Present(dirty, presentFull);
		lastPresentDurationMs_ = GetQpcTimeMilliseconds() - presentStartMs;
		lastPresentSucceeded_ = succeeded;
		if (metrics_) metrics_->RecordPresent(lastPresentDurationMs_);
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
		auto eraserModelParams = configuration_.modelParams;
		eraserModelParams.prediction_params = DisabledPredictorParams{};
		LARGE_INTEGER qpcFrequencyValue = {};
		QueryPerformanceFrequency(&qpcFrequencyValue);
		const int64_t qpcFrequency = qpcFrequencyValue.QuadPart;
		const float reconnectDpiScale = configuration_.dpiScale;
		bool effectiveInvertedPenEraserEnabled = false;
		if constexpr (!kInterruptedStrokeReconnectManualTestModeEnabled)
			effectiveInvertedPenEraserEnabled =
				invertedPenEraserEnabled_.load(std::memory_order_acquire);
		std::cout << "[StrokeReconnect] enabled=" <<
			(configuration_.interruptedStrokeReconnectEnabled ? "true" : "false") <<
			" window_ms=" << kInterruptedStrokeReconnectWindowSeconds * 1000.0 <<
			" prediction_endpoint_base_px=" <<
			kInterruptedStrokeReconnectDistanceSlackPx * reconnectDpiScale <<
			" prediction_relative=" << kInterruptedStrokeReconnectEndpointRelativeTolerance <<
			" beyond_horizon_ratio=" <<
			kInterruptedStrokeReconnectBeyondHorizonUncertaintyRatio <<
			" extrapolation_angle_deg=" <<
			kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees <<
			" terminal_direction_corridor_angle_deg=" <<
			kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees <<
			" in_horizon_terminal_gap_ms=" <<
			kInterruptedStrokeReconnectInHorizonTerminalMaximumGapSeconds * 1000.0 <<
			" in_horizon_terminal_angle_deg=" <<
			kInterruptedStrokeReconnectInHorizonTerminalMaximumAngleDegrees <<
			" in_horizon_terminal_speed_ratio=[" <<
			kInterruptedStrokeReconnectInHorizonTerminalMinimumSpeedRatio << "," <<
			kInterruptedStrokeReconnectInHorizonTerminalMaximumSpeedRatio << "]" <<
			" comparison_epsilon_px=" <<
			kInterruptedStrokeReconnectComparisonEpsilonPx * reconnectDpiScale <<
			" predicted_base_max_px=" <<
			kInterruptedStrokeReconnectPredictedMaximumDistancePx * reconnectDpiScale <<
			" predicted_adaptive_max_px=" <<
			kInterruptedStrokeReconnectAdaptiveMaximumDistancePx * reconnectDpiScale <<
			" adaptive_distance_speed_ratio=" <<
			kInterruptedStrokeReconnectAdaptiveDistanceSpeedRatio <<
			" adaptive_relative=" <<
			kInterruptedStrokeReconnectAdaptiveRelativeTolerance <<
			" fallback_angle_deg=" << kInterruptedStrokeReconnectMaximumAngleDegrees <<
			" fallback_max_distance_px=" <<
			kInterruptedStrokeReconnectFallbackMaximumDistancePx * reconnectDpiScale <<
			" fallback_speed_ratio=[" << kInterruptedStrokeReconnectMinimumSpeedRatio << "," <<
			kInterruptedStrokeReconnectMaximumSpeedRatio << "] candidates=" <<
			kMaximumInterruptedStrokeReconnectCandidates <<
			" manual_test=" << (kInterruptedStrokeReconnectManualTestModeEnabled ? "true" : "false") <<
			" simulation=" << (kInterruptedStrokeReconnectSimulationEnabled ? "true" : "false") <<
			" simulation_interval_ms=[" <<
			kInterruptedStrokeReconnectSimulationMinimumIntervalMs << "," <<
			kInterruptedStrokeReconnectSimulationMaximumIntervalMs << "]" <<
			" simulation_drop_ms=[" << kInterruptedStrokeReconnectSimulationMinimumDropMs << "," <<
			kInterruptedStrokeReconnectSimulationMaximumDropMs << "]" <<
			" inverted_pen_eraser=" << (effectiveInvertedPenEraserEnabled ? "true" : "false") <<
			std::endl;

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
		const int originalThreadPriority = GetThreadPriority(GetCurrentThread());
		const bool drawingPriorityRaised = originalThreadPriority != THREAD_PRIORITY_ERROR_RETURN &&
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != FALSE;
		// 绘制线程只在活动期占用 CPU；提高一级优先级降低 120 FPS deadline 被后台窗口抢占的概率。

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
				const InputDeviceType deviceType = handle.record->DeviceType();
				DrawingTool batchTool = window_.ActiveTool();
				for (RuntimeStroke* activeRuntime : active)
				{
					if (activeRuntime && !activeRuntime->ended && !activeRuntime->awaitingReconnect)
					{
						batchTool = activeRuntime->selectedTool;
						break; // 仍有真实落笔时，新 contact 必须沿用当前批次工具。
					}
				}
				const bool selectedToolSupportsOverride =
					batchTool == DrawingTool::Pen || batchTool == DrawingTool::Highlighter;
				bool effectiveInvertedPenEraserEnabled = false;
				if constexpr (!kInterruptedStrokeReconnectManualTestModeEnabled)
					effectiveInvertedPenEraserEnabled =
						invertedPenEraserEnabled_.load(std::memory_order_acquire);
				const bool invertedEraser = ShouldUseInvertedPenEraser(deviceType,
					down.isInvertedCursor, effectiveInvertedPenEraserEnabled,
					selectedToolSupportsOverride);
				const DrawingTool tool = invertedEraser ? DrawingTool::Eraser : batchTool;
				const bool suppressPressure = deviceType == InputDeviceType::Pen && down.isInvertedCursor;
				const float downPressure = ResolveStylusPressureForModel(
					deviceType, down.isInvertedCursor, down.pressure);
				const StrokeWidthMode widthMode = tool == DrawingTool::Pen
					? ResolveStrokeWidthMode(deviceType,
						inputWidthModeSettings_.Get(), downPressure)
					: StrokeWidthMode::Fixed;
				const InterruptedStrokeReconnectIdentity downIdentity{
					deviceType,
					static_cast<uint32_t>(batchTool),
					static_cast<uint32_t>(tool),
					widthMode,
					down.isInvertedCursor,
					suppressPressure
				};

				RuntimeStroke* reconnectRuntime = nullptr;
				InterruptedStrokeReconnectResult reconnectResult;
				RuntimeStroke* diagnosticRuntime = nullptr;
				InterruptedStrokeReconnectResult diagnosticResult;
				if (configuration_.interruptedStrokeReconnectEnabled)
				{
					for (RuntimeStroke* candidate : active)
					{
						if (!candidate || !candidate->awaitingReconnect || candidate->ended) continue;
						if (!AreInterruptedStrokeReconnectIdentitiesCompatible(
							ReconnectIdentity(*candidate), downIdentity))
						{
							if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
							{
								if (!diagnosticRuntime || candidate->deferredUpSnapshot.qpc >
									diagnosticRuntime->deferredUpSnapshot.qpc)
								{
									diagnosticRuntime = candidate;
									diagnosticResult = {};
									diagnosticResult.rejectReason =
										InterruptedStrokeReconnectRejectReason::IdentityMismatch;
									diagnosticResult.gapMilliseconds = QpcDeltaSeconds(
										down.qpc, candidate->deferredUpSnapshot.qpc, qpcFrequency) * 1000.0;
									diagnosticResult.distance = std::hypot(
										down.position.x - candidate->deferredUpSnapshot.position.x,
										down.position.y - candidate->deferredUpSnapshot.position.y);
								}
							}
							continue;
						}
						const double gapSeconds = QpcDeltaSeconds(
							down.qpc, candidate->deferredUpSnapshot.qpc, qpcFrequency);
						const InterruptedStrokeReconnectMotion motion =
							ResolveInterruptedStrokeReconnectMotion(
								candidate->reconnectPredictedResults,
								candidate->stroke.realPoints,
								candidate->reconnectDirection,
								candidate->filteredInputSpeed,
								candidate->lastModelInputTime,
								gapSeconds,
								reconnectDpiScale);
						const InterruptedStrokeReconnectResult result =
							EvaluateInterruptedStrokeReconnect({
								.previousPosition = candidate->deferredUpSnapshot.position,
								.previousUpQpc = candidate->deferredUpSnapshot.qpc,
								.newPosition = down.position,
								.newDownQpc = down.qpc,
								.qpcFrequency = qpcFrequency,
								.dpiScale = reconnectDpiScale,
								.motion = motion
							});
						if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
						{
							if (!diagnosticRuntime || candidate->deferredUpSnapshot.qpc >
								diagnosticRuntime->deferredUpSnapshot.qpc)
							{
								diagnosticRuntime = candidate;
								diagnosticResult = result;
							}
						}
						if (IsBetterInterruptedStrokeReconnectMatch(result,
							candidate->deferredUpSnapshot.qpc, reconnectResult,
							reconnectRuntime ? reconnectRuntime->deferredUpSnapshot.qpc : 0))
						{
							reconnectRuntime = candidate;
							reconnectResult = result;
						}
					}
				}
				if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
				{
					if (!reconnectRuntime && diagnosticRuntime)
					{
						std::cout << "[StrokeReconnect] rejected device=" <<
							InputDeviceTypeName(deviceType) << " tool=" << DrawingToolName(tool) <<
							" new_tcid=" << handle.record->TabletContextId() <<
							" new_cid=" << handle.record->ContactId() <<
							" candidate_tcid=" << diagnosticRuntime->handle.record->TabletContextId() <<
							" candidate_cid=" << diagnosticRuntime->handle.record->ContactId() <<
							" candidate_generation=" << diagnosticRuntime->handle.generation <<
							" reason=" << ReconnectRejectReasonName(diagnosticResult.rejectReason) <<
							" motion=" << ReconnectMotionSourceName(diagnosticResult.motionSource) <<
							" gap_ms=" << diagnosticResult.gapMilliseconds <<
							" distance_px=" << diagnosticResult.distance <<
							" predicted_displacement_px=" << diagnosticResult.expectedDistance <<
							" endpoint_error_px=" << diagnosticResult.endpointError <<
							" endpoint_limit_px=" << diagnosticResult.maximumEndpointError <<
							" forecast_ms=" << diagnosticResult.forecastDurationMilliseconds <<
							" horizon_ms=" << diagnosticResult.predictionHorizonMilliseconds <<
							" beyond_horizon_ms=" <<
							diagnosticResult.beyondPredictionHorizonMilliseconds <<
							" reference_speed=" << diagnosticResult.referenceSpeed <<
							" recent_input_speed=" << diagnosticResult.recentInputSpeed <<
							" terminal_speed=" << diagnosticResult.terminalSpeed <<
							" filtered_input_speed=" << diagnosticRuntime->filteredInputSpeed <<
							" longitudinal_px=" << diagnosticResult.longitudinalDistance <<
							" longitudinal_error_px=" << diagnosticResult.longitudinalError <<
							" longitudinal_limit_px=" <<
							diagnosticResult.maximumLongitudinalError <<
							" lateral_error_px=" << diagnosticResult.lateralError <<
							" lateral_limit_px=" << diagnosticResult.maximumLateralError <<
							" range_px=[" << diagnosticResult.minimumDistance << "," <<
							diagnosticResult.maximumDistance << "] forecast_angle_deg=" <<
							diagnosticResult.angleDegrees << " terminal_angle_deg=" <<
							diagnosticResult.terminalVelocityAngleDegrees <<
							" selected_angle_deg=" <<
							diagnosticResult.selectedDirectionAngleDegrees <<
							" direction_reliable=" <<
							(diagnosticResult.directionReliable ? "true" : "false") <<
							" prediction_extrapolated=" <<
							(diagnosticResult.predictionExtrapolated ? "true" : "false") <<
							" terminal_direction_corridor_selected=" <<
							(diagnosticResult.selectedTerminalDirectionCorridor ? "true" : "false") <<
							" in_horizon_terminal_corridor_selected=" <<
							(diagnosticResult.selectedInHorizonTerminalDirectionCorridor ?
								"true" : "false") <<
							" speed_ratio=" << diagnosticResult.speedRatio <<
							" match_score=" << diagnosticResult.matchScore << std::endl;
					}
				}

				if (reconnectRuntime)
				{
					const float reconnectPreviousRawSpeed = reconnectRuntime->filteredInputSpeed;
					const uint32_t reconnectPreviousTabletContextId =
						reconnectRuntime->handle.record->TabletContextId();
					const uint32_t reconnectPreviousContactId =
						reconnectRuntime->handle.record->ContactId();
					const uint64_t reconnectPreviousGeneration = reconnectRuntime->handle.generation;
					ContactSnapshot modelDown = down;
					modelDown.pressure = downPressure;
					float lastPressure = reconnectRuntime->lastPressure;
					float lastTilt = reconnectRuntime->lastTilt;
					float lastOrientation = reconnectRuntime->lastOrientation;
					const float pressure = KeepLastValidStylusValue(
						modelDown.pressure, 1.0f, lastPressure);
					const float tilt = KeepLastValidStylusValue(modelDown.tilt, kHalfPi, lastTilt);
					const float orientation = KeepLastValidOrientation(
						modelDown.orientation, lastOrientation);
					double inputTime = QpcDeltaSeconds(
						down.qpc, reconnectRuntime->qpcOrigin, qpcFrequency);
					inputTime = std::max(inputTime, reconnectRuntime->lastModelInputTime + 0.000001);
					const Input reconnectInput{
						.event_type = Input::EventType::kMove,
						.position = Vec2(down.position.x, down.position.y),
						.time = Time(inputTime),
						.pressure = pressure,
						.tilt = tilt,
						.orientation = orientation
					};
					const size_t reconnectManualTestFirstPointIndex =
						reconnectRuntime->stroke.realPoints.empty()
						? 0 : reconnectRuntime->stroke.realPoints.size() - 1;
					if (absl::Status status = reconnectRuntime->stroke.modeler.Update(
						reconnectInput, reconnectRuntime->stroke.modeledResults); status.ok())
					{
						const double gapSeconds = reconnectResult.gapMilliseconds / 1000.0;
						const float alpha = std::clamp(static_cast<float>(
							1.0 - std::exp(-gapSeconds / kInputSpeedSmoothingSeconds)), 0.02f, 0.35f);
						reconnectRuntime->filteredInputSpeed +=
							(reconnectResult.bridgeSpeed - reconnectRuntime->filteredInputSpeed) * alpha;
						AppendNewModeledPoints(reconnectRuntime->stroke,
							reconnectRuntime->filteredInputSpeed);
						if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
						{
							const size_t lastPointIndex = reconnectRuntime->stroke.realPoints.size();
							if (lastPointIndex > reconnectManualTestFirstPointIndex + 1)
							{
								// 记录旧末点到续接 Down 的模型输出，仅用于人工观察桥接范围。
								reconnectRuntime->reconnectManualTestRanges.push_back({
									reconnectManualTestFirstPointIndex, lastPointIndex });
							}
						}
						input_.Recycle(reconnectRuntime->handle); // 新 contact 接管前释放已经 ConsumerOwned 的旧 slot。
						reconnectRuntime->handle = handle;
						reconnectRuntime->lastSpeedSnapshot = down;
						reconnectRuntime->lastModelSnapshot = modelDown;
						reconnectRuntime->lastConsumedSequence = down.sequence;
						reconnectRuntime->lastModelInputTime = inputTime;
						reconnectRuntime->lastPressure = lastPressure;
						reconnectRuntime->lastTilt = lastTilt;
						reconnectRuntime->lastOrientation = lastOrientation;
						reconnectRuntime->awaitingReconnect = false;
						reconnectRuntime->reconnectVisualRefresh = true;
						reconnectRuntime->deferredUpSnapshot = {};
						reconnectRuntime->reconnectDeadlineQpc = 0;
						reconnectRuntime->reconnectPredictedResults.clear();
						reconnectRuntime->metricEligibleQpc = down.qpc;
						reconnectRuntime->metricVisible = false;
						reconnectRuntime->movedThisFrame = true;
						reconnectRuntime->stroke.idleFrozen = false;
						reconnectRuntime->stroke.visualStableFrameCount = 0;
						reconnectRuntime->stroke.lastMovementInputTime = inputTime;
						if (haptics_ && reconnectRuntime->hapticEligible)
						{
							if (reconnectRuntime->invertedCursor)
								haptics_->StopFeedback(); // 倒转笔尾需在真正接触后重新提交波形。
							haptics_->TickContinuous(HapticFeedbackForRuntime(*reconnectRuntime));
						}
						std::cout << "[StrokeReconnect] linked device=" << InputDeviceTypeName(deviceType) <<
							" tool=" << DrawingToolName(tool) <<
							" new_tcid=" << handle.record->TabletContextId() <<
							" new_cid=" << handle.record->ContactId() <<
							" candidate_tcid=" << reconnectPreviousTabletContextId <<
							" candidate_cid=" << reconnectPreviousContactId <<
							" candidate_generation=" << reconnectPreviousGeneration <<
							" gap_ms=" << reconnectResult.gapMilliseconds <<
							" motion=" << ReconnectMotionSourceName(reconnectResult.motionSource) <<
							" distance_px=" << reconnectResult.distance <<
							" predicted_displacement_px=" << reconnectResult.expectedDistance <<
							" endpoint_error_px=" << reconnectResult.endpointError <<
							" endpoint_limit_px=" << reconnectResult.maximumEndpointError <<
							" forecast_ms=" << reconnectResult.forecastDurationMilliseconds <<
							" horizon_ms=" << reconnectResult.predictionHorizonMilliseconds <<
							" beyond_horizon_ms=" <<
							reconnectResult.beyondPredictionHorizonMilliseconds <<
							" reference_speed=" << reconnectResult.referenceSpeed <<
							" recent_input_speed=" << reconnectResult.recentInputSpeed <<
							" terminal_speed=" << reconnectResult.terminalSpeed <<
							" filtered_input_speed=" << reconnectPreviousRawSpeed <<
							" longitudinal_px=" << reconnectResult.longitudinalDistance <<
							" longitudinal_error_px=" << reconnectResult.longitudinalError <<
							" longitudinal_limit_px=" <<
							reconnectResult.maximumLongitudinalError <<
							" lateral_error_px=" << reconnectResult.lateralError <<
							" lateral_limit_px=" << reconnectResult.maximumLateralError <<
							" forecast_angle_deg=" << reconnectResult.angleDegrees <<
							" terminal_angle_deg=" <<
							reconnectResult.terminalVelocityAngleDegrees <<
							" selected_angle_deg=" <<
							reconnectResult.selectedDirectionAngleDegrees <<
							" direction_reliable=" <<
							(reconnectResult.directionReliable ? "true" : "false") <<
							" prediction_extrapolated=" <<
							(reconnectResult.predictionExtrapolated ? "true" : "false") <<
							" terminal_direction_corridor_selected=" <<
							(reconnectResult.selectedTerminalDirectionCorridor ? "true" : "false") <<
							" in_horizon_terminal_corridor_selected=" <<
							(reconnectResult.selectedInHorizonTerminalDirectionCorridor ?
								"true" : "false") <<
							" speed_ratio=" << reconnectResult.speedRatio <<
							" match_score=" << reconnectResult.matchScore << std::endl;
						return true;
					}
					else
					{
						std::cout << "Failed to continue interrupted stroke: " << status.message() << std::endl;
					}
				}

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
				runtime->selectedTool = batchTool; // 倒转覆盖不能污染同批后续 contact 的原始选择。
				runtime->tool = tool;
				runtime->suppressPressure = suppressPressure;
				runtime->ended = false;
				runtime->cancelled = false;
				runtime->metricVisible = false;
				runtime->movedThisFrame = false;
				runtime->visibleDirty = {};
				runtime->metricDeviceType = deviceType;
				runtime->invertedCursor = down.isInvertedCursor;
				runtime->hapticEligible = deviceType == InputDeviceType::Pen;
				runtime->awaitingReconnect = false;
				runtime->reconnectVisualRefresh = false;
				runtime->deferredUpSnapshot = {};
				runtime->reconnectDirection = {};
				runtime->reconnectDeadlineQpc = 0;
				runtime->reconnectPredictedResults.clear();
				runtime->reconnectManualTestRanges.clear();
				runtime->metricEligibleQpc = down.qpc;
				const float baseDiameter = DiameterForTool(runtime->tool);
				const bool highlighter = runtime->tool == DrawingTool::Highlighter;
				runtime->stroke.Reset(baseDiameter, configuration_.expectedSpeed,
					widthMode, highlighter);
				const auto& modelParams = runtime->tool == DrawingTool::Eraser
					? eraserModelParams : strokeModelParams;
				if (absl::Status status = runtime->stroke.modeler.Reset(modelParams); !status.ok())
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

				ContactSnapshot modelDown = down;
				modelDown.pressure = downPressure;
				runtime->lastSpeedSnapshot = down;
				runtime->lastModelSnapshot = modelDown;
				runtime->lastConsumedSequence = down.sequence;
				runtime->qpcOrigin = down.qpc;
				runtime->lastModelInputTime = 0.0;
				runtime->filteredInputSpeed = 0.0f;
				runtime->hasFilteredInputSpeed = false;
				runtime->lastPressure = downPressure;
				runtime->lastTilt = down.tilt;
				runtime->lastOrientation = down.orientation;
				ActiveStroke& stroke = runtime->stroke;
				const float initialDiameter = widthMode == StrokeWidthMode::HardwarePressure
					? HardwarePressureDiameter(baseDiameter, downPressure) : baseDiameter;
				stroke.inputStartPoint = {
					down.position.x, down.position.y, initialDiameter * 0.5f, 0.0f };
				stroke.hasInputStartPoint = true;
				const Input downInput{
					.event_type = Input::EventType::kDown,
					.position = Vec2(down.position.x, down.position.y),
					.time = Time(0.0),
					.pressure = downPressure,
					.tilt = down.tilt,
					.orientation = down.orientation
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
				if (haptics_ && runtime->hapticEligible)
				{
					if (runtime->invertedCursor)
						haptics_->StopFeedback(); // 倒转笔尾不沿用悬停预热的同波形状态。
					haptics_->TickContinuous(HapticFeedbackForRuntime(*runtime));
				}
				return true;
			};

		auto processCommand = [&](ContactRecord* record)
			{
				if (!record)
				{
					input_.AcknowledgeControlWake(); // 先清 pending，随后复查窗口的全部原子请求。
					return;
				}
				initializeStroke(ContactHandle{ record, record->Generation() }); // 出队后立即固定本地 generation。
			};

		auto appendTerminalFallback = [&](RuntimeStroke& runtime,
			const ContactSnapshot& snapshot, double inputTime)
			{
				const float radius = runtime.stroke.realPoints.empty()
					? runtime.stroke.inputStartPoint.r : runtime.stroke.realPoints.back().r;
				const InkPoint finalPoint{ snapshot.position.x, snapshot.position.y,
					radius, static_cast<float>(inputTime) };
				if (runtime.stroke.realPoints.empty())
					runtime.stroke.realPoints.push_back(finalPoint);
				else
				{
					const float deltaX = finalPoint.x - runtime.stroke.realPoints.back().x;
					const float deltaY = finalPoint.y - runtime.stroke.realPoints.back().y;
					if (deltaX * deltaX + deltaY * deltaY > 0.0001f)
						runtime.stroke.realPoints.push_back(finalPoint);
					else
						runtime.stroke.realPoints.back() = finalPoint;
				}
				// 模型异常也保留 RTS 的最终位置，不能因随后回收 contact 而吞掉 Up 点。
			};

		auto completeModelUp = [&](RuntimeStroke& runtime,
			const ContactSnapshot& snapshot, bool cancelled)
			{
				ContactSnapshot modelSnapshot = snapshot;
				if (runtime.suppressPressure) modelSnapshot.pressure = -1.0f;
				double inputTime = QpcDeltaSeconds(snapshot.qpc, runtime.qpcOrigin, qpcFrequency);
				inputTime = std::max(inputTime, runtime.lastModelInputTime + 0.000001);
				runtime.lastModelInputTime = inputTime;
				const float pressure = KeepLastValidStylusValue(
					modelSnapshot.pressure, 1.0f, runtime.lastPressure);
				const float tilt = KeepLastValidStylusValue(
					modelSnapshot.tilt, kHalfPi, runtime.lastTilt);
				const float orientation = KeepLastValidOrientation(
					modelSnapshot.orientation, runtime.lastOrientation);
				const Input upInput{
					.event_type = Input::EventType::kUp,
					.position = Vec2(snapshot.position.x, snapshot.position.y),
					.time = Time(inputTime),
					.pressure = pressure,
					.tilt = tilt,
					.orientation = orientation
				};
				if (absl::Status status = runtime.stroke.modeler.Update(
					upInput, runtime.stroke.modeledResults); status.ok())
					AppendNewModeledPoints(runtime.stroke);
				else
				{
					std::cout << "Error: " << status.message() << std::endl;
					appendTerminalFallback(runtime, snapshot, inputTime);
				}
				runtime.lastModelSnapshot = modelSnapshot;
				runtime.ended = true;
				runtime.cancelled = cancelled;
				runtime.awaitingReconnect = false;
				runtime.reconnectVisualRefresh = false;
				runtime.reconnectDeadlineQpc = 0;
			};

		auto consumeLatestSnapshot = [&](RuntimeStroke& runtime) -> bool
			{
				if (runtime.ended || runtime.awaitingReconnect) return false;
				ContactSnapshot snapshot;
				if (!input_.TryReadSnapshot(runtime.handle, snapshot) ||
					snapshot.sequence == runtime.lastConsumedSequence) return false;
				runtime.lastConsumedSequence = snapshot.sequence;
				if (snapshot.phase == ContactPhase::Down) return false;
				ContactSnapshot modelSnapshot = snapshot;
				if (runtime.suppressPressure) modelSnapshot.pressure = -1.0f;

				const float deltaX = snapshot.position.x - runtime.lastSpeedSnapshot.position.x;
				const float deltaY = snapshot.position.y - runtime.lastSpeedSnapshot.position.y;
				const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
				const bool terminal = snapshot.phase == ContactPhase::Up ||
					snapshot.phase == ContactPhase::Cancelled;
				const bool deferUp = snapshot.phase == ContactPhase::Up &&
					configuration_.interruptedStrokeReconnectEnabled;
				const bool positionMoved = distanceSquared > kRawMoveThresholdPx * kRawMoveThresholdPx;
				const bool stylusStateChanged = HasStylusStateChange(modelSnapshot, runtime.lastModelSnapshot);
				if (!terminal && !positionMoved && !stylusStateChanged)
					return false; // Move 抖动已消费但不进入模型，也不改变下一次真实速度基准。

				const double deltaSeconds = QpcDeltaSeconds(
					snapshot.qpc, runtime.lastSpeedSnapshot.qpc, qpcFrequency);
				float inputSpeed = -1.0f;
				if (positionMoved && deltaSeconds > 0.0)
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
				const float pressure = KeepLastValidStylusValue(modelSnapshot.pressure, 1.0f,
					runtime.lastPressure);
				const float tilt = KeepLastValidStylusValue(modelSnapshot.tilt, kHalfPi,
					runtime.lastTilt);
				const float orientation = KeepLastValidOrientation(
					modelSnapshot.orientation, runtime.lastOrientation);
				const Input input{
					.event_type = terminal && !deferUp ? Input::EventType::kUp : Input::EventType::kMove,
					.position = Vec2(snapshot.position.x, snapshot.position.y),
					.time = Time(inputTime),
					.pressure = pressure,
					.tilt = tilt,
					.orientation = orientation
				};
				bool modelUpdateSucceeded = false;
				if (absl::Status status = runtime.stroke.modeler.Update(
					input, runtime.stroke.modeledResults); status.ok())
				{
					modelUpdateSucceeded = true;
					AppendNewModeledPoints(runtime.stroke, inputSpeed);
				}
				else
				{
					std::cout << "Error: " << status.message() << std::endl;
					if (terminal && !deferUp) appendTerminalFallback(runtime, snapshot, inputTime);
				}
				runtime.lastModelSnapshot = modelSnapshot;
				if (positionMoved) runtime.lastSpeedSnapshot = snapshot;
				if (positionMoved || stylusStateChanged)
				{
					runtime.stroke.idleFrozen = false;
					runtime.stroke.visualStableFrameCount = 0;
					runtime.stroke.lastMovementInputTime = inputTime;
				}
				if (deferUp)
				{
					DirectX::XMFLOAT2 direction = {};
					if (modelUpdateSucceeded && runtime.hasFilteredInputSpeed &&
						TryGetInterruptedStrokeTailDirection(
							runtime.stroke.realPoints, reconnectDpiScale, direction))
					{
						runtime.reconnectPredictedResults.clear();
						if (runtime.tool != DrawingTool::Eraser &&
							kActivePredictionMode != InkPredictionMode::Disabled)
						{
							// 同帧新 Down 会在渲染前出队，因此候选创建时必须先冻结最新 prediction。
							if (absl::Status status = runtime.stroke.modeler.Predict(
								runtime.reconnectPredictedResults); !status.ok())
								runtime.reconnectPredictedResults.clear();
						}
						runtime.awaitingReconnect = true;
						const bool anotherPenContactActive = std::any_of(active.begin(), active.end(),
							[&](const RuntimeStroke* candidate)
							{
								return candidate && candidate != &runtime &&
									candidate->hapticEligible && !candidate->ended &&
									!candidate->awaitingReconnect;
							});
						if (haptics_ && runtime.hapticEligible && !anotherPenContactActive)
							haptics_->StopFeedback();
						runtime.reconnectVisualRefresh = true;
						runtime.deferredUpSnapshot = snapshot;
						runtime.reconnectDirection = direction;
						runtime.reconnectDeadlineQpc = snapshot.qpc + static_cast<int64_t>(
							kInterruptedStrokeReconnectWindowSeconds * static_cast<double>(qpcFrequency));
					}
					else
					{
						completeModelUp(runtime, snapshot, false);
					}
				}
				else if (terminal)
				{
					runtime.ended = true;
					runtime.cancelled = snapshot.phase == ContactPhase::Cancelled;
				}
				return positionMoved || stylusStateChanged || deferUp;
			};

		bool clearPending = false;
		bool timerPeriodActive = false;
		bool timerPeriodAttempted = false;
		double lastActiveFrameStartMs = 0.0;
		bool hapticContinuousActive = false;
		std::vector<DrawingCursorVisual> previousCursorVisuals;
		std::vector<DrawingCursorVisual> currentCursorVisuals;
		previousCursorVisuals.reserve(kPreheatedStrokeCount + 1);
		currentCursorVisuals.reserve(kPreheatedStrokeCount + 1);

		auto buildDrawingCursorVisuals = [&]()
		{
			currentCursorVisuals.clear();
			DrawingCursorSample penSample;
			DrawingCursorSample mouseSample;
			window_.ReadPenCursorSample(penSample);
			window_.ReadMouseCursorSample(mouseSample);
			const DrawingTool cursorTool = window_.EffectiveDrawingCursorTool();
			const DrawingCursorVisual primary = ResolvePrimaryDrawingCursorVisual(
				penSample, mouseSample, window_.CursorPointerAuthority(),
				window_.CursorAppearanceForTool(cursorTool),
				window_.CursorAppearanceForTool(DrawingTool::Eraser),
				cursorTool == DrawingTool::Eraser);
			if (primary.visible) currentCursorVisuals.push_back(primary);

			const DrawingCursorAppearance eraserAppearance =
				window_.CursorAppearanceForTool(DrawingTool::Eraser);
			for (const RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended || runtime->awaitingReconnect ||
					runtime->metricDeviceType != InputDeviceType::Touch ||
					runtime->tool != DrawingTool::Eraser) continue;
				const ContactSnapshot& snapshot = runtime->lastModelSnapshot;
				const DrawingCursorVisual touchVisual = MakeTouchEraserDrawingCursorVisual(
					snapshot.position.x, snapshot.position.y, eraserAppearance);
				if (touchVisual.visible) currentCursorVisuals.push_back(touchVisual);
			}
		};

		auto cursorVisualsEquivalent = [&]() noexcept
		{
			if (previousCursorVisuals.size() != currentCursorVisuals.size()) return false;
			for (size_t index = 0; index < currentCursorVisuals.size(); ++index)
			{
				if (!AreDrawingCursorVisualsEquivalent(
					previousCursorVisuals[index], currentCursorVisuals[index])) return false;
			}
			return true;
		};

		auto cursorVisualBounds = [&](const std::vector<DrawingCursorVisual>& visuals)
		{
			RECT bounds = {};
			const WindowSize size = window_.Size();
			for (const DrawingCursorVisual& visual : visuals)
				UnionRectInPlace(bounds,
					DrawingCursorVisualBounds(visual, size.width, size.height));
			return bounds;
		};
		while (true)
		{
			const double frameStartMs = GetQpcTimeMilliseconds();
			if (metrics_) metrics_->BeginFrame();
			lastPresentDurationMs_ = 0.0;
			lastPresentSucceeded_ = false;
			if (active.empty()) window_.ClearActiveDrawingCursorTool();
			const bool drawingCursorRequested = window_.ConsumeDrawingCursorRenderRequest();
			const double previousFrameMs = lastActiveFrameStartMs > 0.0
				? frameStartMs - lastActiveFrameStartMs : 0.0;
			ContactRecord* record = nullptr;

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
			if (haptics_)
			{
				if (window_.ConsumeHapticPointerLeave())
					haptics_->StopFeedback();
				uint32_t pointerId = 0;
				bool pointerEraserHint = false;
				if (window_.ConsumeHapticPointerId(pointerId, pointerEraserHint))
				{
					if (haptics_->AttachPointerId(pointerId))
					{
						// 笔尾需在 RTS Down 前预启动橡皮波形，RTS 仍在 Down 时校验真实工具。
						const HapticContinuousFeedback hapticFeedback = pointerEraserHint
							? HapticContinuousFeedback::InkContinuous
							: ResolveContinuousHapticFeedback(
								HapticToolForDrawingTool(window_.ActiveTool()));
						haptics_->TickContinuous(hapticFeedback);
					}
				}
			}
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

			if (active.empty() && !forceFullPresent && !drawingCursorRequested)
			{
				if (hapticContinuousActive && haptics_)
				{
					haptics_->StopFeedback();
					hapticContinuousActive = false;
				}
				if (metrics_) metrics_->BeginIdle(frameStartMs);
				lastActiveFrameStartMs = 0.0;
				if (timerPeriodActive)
				{
					timeEndPeriod(1);
					timerPeriodActive = false;
				}
				timerPeriodAttempted = false;
				// 二次排空后才等待；竞态窗口内到达的命令会留下信号量计数。
				if (input_.TryDequeue(record))
				{
					processCommand(record);
					if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
					continue;
				}
				input_.WaitDequeue(record);
				processCommand(record);
				if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
				continue;
			}

			const bool frameHadActiveContact = HasPhysicalContact(active);
			const uint64_t frameWakeGeneration = input_.CaptureWakeGeneration();
			LARGE_INTEGER frameQpc = {};
			QueryPerformanceCounter(&frameQpc);
			bool hasEndedStroke = false;
			if (!configuration_.interruptedStrokeReconnectEnabled)
			{
				while (input_.TryDequeue(record)) processCommand(record);
				// 关闭开关时保留原先的 Down 出队顺序，确保它是完整的回滚点。
			}
			// 先把旧 contact 的 Up 转成候选，同帧随后出队的新 Down 才能看到它。
			for (RuntimeStroke* runtime : active)
			{
				runtime->movedThisFrame = consumeLatestSnapshot(*runtime);
				hasEndedStroke = hasEndedStroke || runtime->ended;
			}

			size_t reconnectCandidateCount = static_cast<size_t>(std::count_if(
				active.begin(), active.end(), [](const RuntimeStroke* runtime)
					{ return runtime && runtime->awaitingReconnect; }));
			size_t reconnectEvictionCount =
				GetInterruptedStrokeReconnectEvictionCount(reconnectCandidateCount);
			while (reconnectEvictionCount > 0)
			{
				RuntimeStroke* oldest = nullptr;
				for (RuntimeStroke* runtime : active)
				{
					if (!runtime || !runtime->awaitingReconnect) continue;
					if (!oldest || runtime->deferredUpSnapshot.qpc < oldest->deferredUpSnapshot.qpc)
						oldest = runtime;
				}
				if (!oldest) break;
				completeModelUp(*oldest, oldest->deferredUpSnapshot, false);
				hasEndedStroke = true;
				--reconnectEvictionCount;
			}

			if (configuration_.interruptedStrokeReconnectEnabled)
				while (input_.TryDequeue(record)) processCommand(record);
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || !runtime->awaitingReconnect ||
					!IsInterruptedStrokeReconnectExpired(
						runtime->reconnectDeadlineQpc, frameQpc.QuadPart)) continue;
				completeModelUp(*runtime, runtime->deferredUpSnapshot, false);
				hasEndedStroke = true;
			}

			const bool hasPhysicalContact = HasPhysicalContact(active);
			if (hasPhysicalContact && !timerPeriodAttempted)
			{
				timerPeriodAttempted = true;
				timerPeriodActive = timeBeginPeriod(1) == TIMERR_NOERROR; // 只为成功的请求配对 timeEndPeriod。
			}
			const auto frameToolIterator = std::find_if(active.begin(), active.end(),
				[](const RuntimeStroke* runtime)
					{ return runtime && !runtime->ended && !runtime->awaitingReconnect; });
			const DrawingTool frameTool = frameToolIterator != active.end()
				? (*frameToolIterator)->tool
				: active.empty() ? window_.ActiveTool() : active.front()->tool;
			const DrawingCursorPointerAuthority cursorAuthority =
				window_.CursorPointerAuthority();
			auto activeCursorIterator = std::find_if(active.begin(), active.end(),
				[&](const RuntimeStroke* runtime)
				{
					if (!runtime || runtime->ended || runtime->awaitingReconnect) return false;
					if (cursorAuthority == DrawingCursorPointerAuthority::Pen ||
						cursorAuthority == DrawingCursorPointerAuthority::Unknown)
						return runtime->metricDeviceType == InputDeviceType::Pen;
					if (cursorAuthority == DrawingCursorPointerAuthority::Mouse)
						return runtime->metricDeviceType == InputDeviceType::MouseLeft ||
							runtime->metricDeviceType == InputDeviceType::MouseRight;
					return false;
				});
			if (activeCursorIterator == active.end() &&
				cursorAuthority == DrawingCursorPointerAuthority::Unknown)
			{
				activeCursorIterator = std::find_if(active.begin(), active.end(),
					[](const RuntimeStroke* runtime)
					{
						return runtime && !runtime->ended && !runtime->awaitingReconnect &&
							(runtime->metricDeviceType == InputDeviceType::MouseLeft ||
								runtime->metricDeviceType == InputDeviceType::MouseRight);
					});
			}
			if (activeCursorIterator != active.end())
				window_.SetActiveDrawingCursorTool((*activeCursorIterator)->tool);
			else if (frameToolIterator != active.end())
				window_.SetActiveDrawingCursorTool((*frameToolIterator)->selectedTool);
			else
				window_.ClearActiveDrawingCursorTool();

			const WindowSize size = window_.Size();
			for (RuntimeStroke* runtime : active)
			{
				ActiveStroke& stroke = runtime->stroke;
				if (runtime->awaitingReconnect && !runtime->reconnectVisualRefresh)
					continue; // 暂留候选保持上一帧 L0/L1，不制造脏区或重复 prediction。
				const bool eraser = runtime->tool == DrawingTool::Eraser;
				const bool highlighter = runtime->tool == DrawingTool::Highlighter;
				const double liveTipDurationSeconds =
					eraser || highlighter ? 0.0 : configuration_.liveTipDurationSeconds;
				stroke.lastL0Rect = stroke.currentL0Rect;
				stroke.logicalInputTime = std::max(stroke.logicalInputTime,
					QpcDeltaSeconds(frameQpc.QuadPart, runtime->qpcOrigin, qpcFrequency));

				RECT stableDirty = {};
				if (!runtime->ended)
				{
					stroke.predictedResults.clear();
					if (runtime->awaitingReconnect)
					{
						if (!eraser)
							stroke.predictedResults.assign(
								runtime->reconnectPredictedResults.begin(),
								runtime->reconnectPredictedResults.end());
					}
					else if (!eraser &&
						kActivePredictionMode != InkPredictionMode::Disabled)
					{
						if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
							stroke.predictedResults.clear();
					}
					RebuildPredictedPoints(stroke);
					stableDirty = eraser
						? CommitEraserRealPointsToL1(stroke, StrokeShape::RoundCapsule,
							renderer_, size.width, size.height)
						: CommitStablePrefixToL1(stroke, liveTipDurationSeconds,
							GetPredictionDurationSeconds(stroke), ColorForTool(runtime->tool),
							StrokeShape::RoundCapsule, renderer_, size.width, size.height);
				}
				if (eraser)
				{
					stroke.predictedPoints.clear();
					stroke.l0DrawPoints.clear();
					stroke.currentL0Rect = {};
				}
				else if (!runtime->ended)
				{
					RebuildL0DrawPoints(stroke, liveTipDurationSeconds,
						StrokeShape::RoundCapsule, size.width, size.height);
				}
				else
				{
					// Up 后旧 prediction 仍基于上一真实末端，禁止把它接到新的最终点之后形成折返。
					stroke.predictedResults.clear();
					stroke.predictedPoints.clear();
					stroke.l0DrawPoints.clear();
					stroke.currentL0Rect = {};
				}
				if (!runtime->ended && !runtime->awaitingReconnect)
					UpdateIdleFreezeState(stroke, runtime->movedThisFrame,
						liveTipDurationSeconds);
				if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
				{
					if (!runtime->ended)
						UnionRectInPlace(stableDirty,
							DrawReconnectManualTestRanges(*runtime, renderer_, size.width, size.height));
				}

				UnionRectInPlace(frameDirty, stableDirty);
				UnionRectInPlace(frameDirty, stroke.lastL0Rect);
				UnionRectInPlace(frameDirty, stroke.currentL0Rect);
				UnionRectInPlace(runtime->visibleDirty, stableDirty);
				UnionRectInPlace(runtime->visibleDirty, stroke.lastL0Rect);
				UnionRectInPlace(runtime->visibleDirty, stroke.currentL0Rect);
				if (!IsEmptyRect(stableDirty) || !IsEmptyRect(stroke.currentL0Rect))
					runtime->metricVisible = true;
				runtime->reconnectVisualRefresh = false;
			}

			if (hasEndedStroke)
			{
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				RECT completedDirty = {};
				for (RuntimeStroke* runtime : active)
				{
					if (!runtime->ended) continue;
					if (!runtime->cancelled)
					{
						RECT completedStrokeDirty =
							DrawCompletedStroke(*runtime, renderer_, size.width, size.height,
								configuration_.retainPredictionOnUp);
						if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
							UnionRectInPlace(completedStrokeDirty,
								DrawReconnectManualTestRanges(*runtime, renderer_, size.width, size.height));
						UnionRectInPlace(completedDirty, completedStrokeDirty);
						if (!IsEmptyRect(completedStrokeDirty)) runtime->metricVisible = true;
					}
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
						if (metrics_ && runtime->metricVisible && !runtime->cancelled)
							metrics_->StageLanding(runtime->handle.record, runtime->handle.generation,
								runtime->metricDeviceType, static_cast<uint32_t>(runtime->tool),
								runtime->metricEligibleQpc);
						input_.Recycle(runtime->handle); // L2 提交与活动层重建完成后才归还 slot。
						runtime->stroke.Reset(kPenDiameter, configuration_.expectedSpeed);
						runtime->handle = {};
						runtime->selectedTool = DrawingTool::Pen;
						runtime->tool = DrawingTool::Pen;
						runtime->suppressPressure = false;
						runtime->invertedCursor = false;
						runtime->hapticEligible = false;
						runtime->visibleDirty = {};
						runtime->ended = false;
						runtime->cancelled = false;
						runtime->awaitingReconnect = false;
						runtime->reconnectVisualRefresh = false;
						runtime->deferredUpSnapshot = {};
						runtime->reconnectDirection = {};
						runtime->reconnectDeadlineQpc = 0;
						runtime->reconnectPredictedResults.clear();
						runtime->reconnectManualTestRanges.clear();
						runtime->metricVisible = false;
						runtime->metricEligibleQpc = 0;
						runtime->inUse = false;
						return true;
					});
			}
			else if (!active.empty())
			{
				renderer_.ClearOperatorLayer(renderer_.layerL0); // 共享 L0 每帧只恢复一次单位操作。
				for (RuntimeStroke* runtime : active)
				{
					if (runtime->tool != DrawingTool::Eraser &&
						!runtime->stroke.l0DrawPoints.empty())
						DrawL0LiveComposite(runtime->stroke, ColorForTool(runtime->tool),
							StrokeShape::RoundCapsule, renderer_, false);
				}
			}

			if (haptics_)
			{
				RuntimeStroke* hapticRuntime = nullptr;
				for (RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->hapticEligible &&
						!runtime->ended && !runtime->awaitingReconnect)
					{
						hapticRuntime = runtime;
						break;
					}
				}
				if (hapticRuntime)
				{
					hapticContinuousActive = haptics_->TickContinuous(
					HapticFeedbackForRuntime(*hapticRuntime)) ||
						hapticContinuousActive;
				}
				else if (hapticContinuousActive)
				{
					haptics_->StopFeedback();
					hapticContinuousActive = false;
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

			buildDrawingCursorVisuals();
			if (!cursorVisualsEquivalent())
			{
				// 先重建旧区清除上一帧，再把全部当前 visual 绘制到 backbuffer 最上层。
				UnionRectInPlace(frameDirty, cursorVisualBounds(previousCursorVisuals));
				UnionRectInPlace(frameDirty, cursorVisualBounds(currentCursorVisuals));
			}
			else if (!IsEmptyRect(frameDirty) && !currentCursorVisuals.empty())
			{
				// 其他几何触发 Present 时也要先重建光标区，避免半透明像素在旧 backbuffer 上重复叠加。
				UnionRectInPlace(frameDirty, cursorVisualBounds(currentCursorVisuals));
			}
			frameDirty = ClampRectToCanvas(frameDirty, size.width, size.height);
			if (forceFullPresent) frameDirty = GetFullCanvasRect(size.width, size.height);
			if (metrics_)
			{
				for (RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->metricVisible && !runtime->cancelled)
						metrics_->StageLanding(runtime->handle.record, runtime->handle.generation,
							runtime->metricDeviceType, static_cast<uint32_t>(runtime->tool),
							runtime->metricEligibleQpc);
				}
			}
			bool presentSucceeded = false;
			if (!IsEmptyRect(frameDirty))
			{
				const bool orderedPreview = frameTool == DrawingTool::Pen &&
					kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer;
				CompositeLayersToBackBuffer(frameDirty, orderedPreview);
				for (const DrawingCursorVisual& visual : currentCursorVisuals)
					renderer_.DrawTransientDrawingCursor(visual);
				presentSucceeded = PresentFrame(
					frameDirty, forceFullPresent); // 一帧最多一次 backbuffer 合成和一次 Present。
			}
			previousCursorVisuals = currentCursorVisuals;
			if (metrics_)
			{
				LARGE_INTEGER presentQpc = {};
				QueryPerformanceCounter(&presentQpc);
				metrics_->CommitStagedLandings(presentSucceeded, presentQpc.QuadPart);
			}

			const bool hasPhysicalContactAfterFrame = HasPhysicalContact(active);
			if (hasPhysicalContactAfterFrame)
			{
				const double workMs = GetQpcTimeMilliseconds() - frameStartMs;
				if (metrics_ && frameHadActiveContact)
					metrics_->RecordActiveFrame(frameStartMs, workMs,
						lastPresentDurationMs_, lastPresentSucceeded_);
				const double remainingFrameBudgetMs =
					1000.0 / configuration_.timingProfile.target_fps - workMs;
				input_.WaitForWake(frameWakeGeneration, remainingFrameBudgetMs);
				size_t committedCount = 0;
				size_t realPointCount = 0;
				size_t predictedPointCount = 0;
				size_t l0PointCount = 0;
				bool allIdleFrozen = true;
				for (const RuntimeStroke* runtime : active)
				{
					committedCount += runtime->stroke.committedIndex;
					realPointCount += runtime->stroke.realPoints.size();
					predictedPointCount += runtime->stroke.predictedPoints.size();
					l0PointCount += runtime->stroke.l0DrawPoints.size();
					allIdleFrozen = allIdleFrozen && runtime->stroke.idleFrozen;
				}
				LogFrameTiming(committedCount, realPointCount, predictedPointCount,
					l0PointCount, workMs, previousFrameMs, allIdleFrozen); // Debug 输出全部活动 contact 的聚合帧率。
				lastActiveFrameStartMs = frameStartMs;
			}
			else if (!active.empty())
			{
				lastActiveFrameStartMs = 0.0;
				if (timerPeriodActive)
				{
					timeEndPeriod(1);
					timerPeriodActive = false;
				}
				timerPeriodAttempted = false;
				int64_t nearestDeadlineQpc = (std::numeric_limits<int64_t>::max)();
				for (const RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->awaitingReconnect)
						nearestDeadlineQpc = std::min(
							nearestDeadlineQpc, runtime->reconnectDeadlineQpc);
				}
				LARGE_INTEGER waitStartQpc = {};
				QueryPerformanceCounter(&waitStartQpc);
				const double timeoutMilliseconds = nearestDeadlineQpc ==
					(std::numeric_limits<int64_t>::max)()
					? 0.0
					: QpcDeltaSeconds(nearestDeadlineQpc,
						waitStartQpc.QuadPart, qpcFrequency) * 1000.0;
				if (metrics_) metrics_->BeginIdle(GetQpcTimeMilliseconds());
				input_.WaitForWake(frameWakeGeneration, timeoutMilliseconds);
				if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
			}
		}

		if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
		if (haptics_) haptics_->StopFeedback();
		if (timerPeriodActive) timeEndPeriod(1);
		if (drawingPriorityRaised)
			SetThreadPriority(GetCurrentThread(), originalThreadPriority);
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
		const float baseDiameter = tool == DrawingTool::Pen ? 5.0f : 50.0f; // 普通笔恢复默认 5px；荧光笔和橡皮保持 50px。
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
					if (kActivePredictionMode != InkPredictionMode::Disabled)
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
		if (highlighter && stroke.l0HighlighterGeometry.primitives.empty() && stroke.hasInputStartPoint)
		{
			const RECT oldLiveRect = stroke.currentL0Rect;
			stroke.predictedResults.clear();
			stroke.predictedPoints.clear();
			stroke.l0DrawPoints = { stroke.inputStartPoint };
			stroke.l0HighlighterGeometry = BuildHighlighterGeometry(stroke.l0DrawPoints);
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
