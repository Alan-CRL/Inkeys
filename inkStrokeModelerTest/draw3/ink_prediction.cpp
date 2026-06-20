module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <windows.h>

#pragma comment(lib, "ink_stroke_modeler_merge.lib")

module draw3.ink_prediction;

namespace draw3
{
	namespace
	{
		constexpr float kIdleMoveThresholdPx = 0.25f;
		constexpr float kVisualStablePositionEpsilonPx = 0.05f;
		constexpr float kVisualStableRadiusEpsilonPx = 0.02f;
		constexpr int kVisualStableRequiredFrames = 3;

		float LerpFloat(float from, float to, float ratio)
		{
			return from + (to - from) * ratio;
		}

		float SmoothStep01(float value)
		{
			value = std::clamp(value, 0.0f, 1.0f);
			return value * value * (3.0f - 2.0f * value);
		}

		StrokeTimingProfile GetStrokeTimingProfile(StrokeTimingProfileId id)
		{
			switch (id)
			{
			case StrokeTimingProfileId::Fps30:
				return { 30.0, 180.0, 0.055, 1.0 / 30.0, 4, 5, 2.5 / 30.0, 0.015f, 0.08f, 2000 };
			case StrokeTimingProfileId::Fps60:
				return { 60.0, 240.0, 0.055, 1.0 / 45.0, 5, 10, 2.5 / 60.0, 0.015f, 0.08f, 2000 };
			case StrokeTimingProfileId::Fps120:
				return { 120.0, 360.0, 0.055, 1.0 / 60.0, 10, 20, 2.5 / 120.0, 0.015f, 0.08f, 2000 };
			case StrokeTimingProfileId::Fps240:
				return { 240.0, 480.0, 0.055, 1.0 / 120.0, 20, 40, 2.5 / 240.0, 0.015f, 0.08f, 2000 };
			default:
				return GetStrokeTimingProfile(StrokeTimingProfileId::Fps60);
			}
		}

		double GetLiveTipDurationSeconds(const StrokeTimingProfile& profile)
		{
			switch (kActiveLiveTipLengthMode)
			{
			case LiveTipLengthMode::Short: return profile.live_tail_duration_seconds * 0.65;
			case LiveTipLengthMode::Long: return profile.live_tail_duration_seconds * 1.6;
			default: return profile.live_tail_duration_seconds;
			}
		}

		bool AreL0VisualsClose(const std::vector<InkPoint>& current, const std::vector<InkPoint>& previous)
		{
			if (current.size() != previous.size()) return false;
			const float positionEpsilonSquared = kVisualStablePositionEpsilonPx * kVisualStablePositionEpsilonPx;
			for (size_t index = 0; index < current.size(); ++index)
			{
				const float deltaX = current[index].x - previous[index].x;
				const float deltaY = current[index].y - previous[index].y;
				if (deltaX * deltaX + deltaY * deltaY > positionEpsilonSquared) return false;
				if (std::abs(current[index].r - previous[index].r) > kVisualStableRadiusEpsilonPx) return false;
			}
			return true;
		}

		size_t FindProtectedStartIndex(const std::vector<InkPoint>& points, double protectedDurationSeconds)
		{
			if (points.size() < 2) return 0;
			const double startTime = static_cast<double>(points.back().time) - std::max(0.0, protectedDurationSeconds);
			size_t startIndex = 0;
			// 多保留一个连接点，避免 L1 与 L0 的交界断开。
			while (startIndex + 1 < points.size() && points[startIndex + 1].time < startTime) ++startIndex;
			return startIndex;
		}

		void ApplyLiveTipTaper(std::vector<InkPoint>& points, double liveTipDurationSeconds)
		{
			if (points.empty() || liveTipDurationSeconds <= 0.0) return;
			const double endTime = points.back().time;
			const double tipStartTime = endTime - liveTipDurationSeconds;
			size_t firstTipIndex = points.size() - 1;
			while (firstTipIndex > 0 && static_cast<double>(points[firstTipIndex - 1].time) >= tipStartTime) --firstTipIndex;

			const double actualTipSpan = std::max(0.0, endTime - static_cast<double>(points[firstTipIndex].time));
			const float spanRatio = SmoothStep01(static_cast<float>(actualTipSpan / liveTipDurationSeconds));
			const float newestScale = LerpFloat(1.0f, 0.28f, spanRatio);
			for (size_t index = firstTipIndex; index < points.size(); ++index)
			{
				const float ageRatio = actualTipSpan > 0.000001
					? static_cast<float>((endTime - static_cast<double>(points[index].time)) / actualTipSpan)
					: 0.0f;
				points[index].r *= LerpFloat(newestScale, 1.0f, SmoothStep01(ageRatio));
			}
		}
	}

	StrokeModelConfiguration CreateStrokeModelConfiguration(int dpiX)
	{
		const StrokeTimingProfile timingProfile = GetStrokeTimingProfile(kActiveStrokeTimingProfileId);
		const float expectedSpeed = 500.0f * (static_cast<float>(dpiX) / 96.0f);
		ink::stroke_model::KalmanPredictorParams kalmanParams;
		kalmanParams.process_noise = 0.05;
		kalmanParams.measurement_noise = 0.01;
		kalmanParams.min_stable_iteration = 4;
		kalmanParams.max_time_samples = timingProfile.kalman_max_time_samples;
		kalmanParams.min_catchup_velocity = expectedSpeed / 1000.0f;
		kalmanParams.acceleration_weight = 0.5f;
		kalmanParams.jerk_weight = 0.1f;
		kalmanParams.prediction_interval = ink::stroke_model::Duration(timingProfile.prediction_interval_seconds);
		kalmanParams.confidence_params = {
			.desired_number_of_samples = timingProfile.kalman_desired_number_of_samples,
			.max_estimation_distance = 1.5f * static_cast<float>(kalmanParams.measurement_noise),
			.min_travel_speed = 0.05f * expectedSpeed,
			.max_travel_speed = 0.25f * expectedSpeed,
			.max_linear_deviation = 10.0f * static_cast<float>(kalmanParams.measurement_noise),
			.baseline_linearity_confidence = 0.4f
		};

		ink::stroke_model::StrokeModelParams modelParams{
			.wobble_smoother_params{
				.is_enabled = true,
				.timeout = ink::stroke_model::Duration(timingProfile.wobble_timeout_seconds),
				.speed_floor = timingProfile.wobble_speed_floor_ratio * expectedSpeed,
				.speed_ceiling = timingProfile.wobble_speed_ceiling_ratio * expectedSpeed
			},
			.position_modeler_params{ .spring_mass_constant = 11.f / 32400, .drag_constant = 72.f },
			.sampling_params{
				.min_output_rate = timingProfile.min_output_rate,
				.end_of_stroke_stopping_distance = .001f,
				.end_of_stroke_max_iterations = 20,
				.max_outputs_per_call = timingProfile.max_outputs_per_call
			}
		};
		return { timingProfile, expectedSpeed, GetLiveTipDurationSeconds(timingProfile), kalmanParams, modelParams };
	}

	void ApplyPredictionMode(ink::stroke_model::StrokeModelParams& params,
		const ink::stroke_model::KalmanPredictorParams& kalmanPredictorParams)
	{
		switch (kActivePredictionMode)
		{
		case InkPredictionMode::Disabled:
			params.prediction_params = ink::stroke_model::DisabledPredictorParams{};
			break;
		case InkPredictionMode::StrokeEnd:
			params.prediction_params = ink::stroke_model::StrokeEndPredictorParams{};
			break;
		default:
			params.prediction_params = kalmanPredictorParams;
			break;
		}
	}

	StrokeWidthEstimator::StrokeWidthEstimator(float baseDiameterValue, float expectedSpeedValue)
		: baseDiameter(baseDiameterValue), minDiameter(baseDiameterValue * 0.8f),
		maxDiameter(baseDiameterValue * 1.4f), expectedSpeed(std::max(1.0f, expectedSpeedValue)),
		currentDiameter(baseDiameterValue)
	{
	}

	InkPoint StrokeWidthEstimator::Append(const ink::stroke_model::Result& result)
	{
		const double pointTime = result.time.Value();
		const float rawSpeed = std::hypot(result.velocity.x, result.velocity.y);
		const float targetDiameter = LerpFloat(maxDiameter, minDiameter, SmoothStep01(rawSpeed / expectedSpeed));
		if (!hasSample)
		{
			currentDiameter = targetDiameter;
			hasSample = true;
		}
		else
		{
			const double deltaTime = std::max(0.0, pointTime - lastTime);
			const float alpha = std::clamp(static_cast<float>(1.0 - std::exp(-deltaTime / 0.035)), 0.05f, 0.65f);
			currentDiameter = LerpFloat(currentDiameter, targetDiameter, alpha);
		}
		lastTime = pointTime;
		return { result.position.x, result.position.y, currentDiameter * 0.5f, static_cast<float>(pointTime) };
	}

	ActiveMouseStroke::ActiveMouseStroke(float baseDiameter, float expectedSpeed)
		: widthEstimator(baseDiameter, expectedSpeed)
	{
	}

	void UnionRectInPlace(RECT& target, const RECT& addition)
	{
		if (IsEmptyRect(addition)) return;
		if (IsEmptyRect(target))
		{
			target = addition;
			return;
		}
		target.left = std::min(target.left, addition.left);
		target.top = std::min(target.top, addition.top);
		target.right = std::max(target.right, addition.right);
		target.bottom = std::max(target.bottom, addition.bottom);
	}

	bool IsEmptyRect(const RECT& rect)
	{
		return rect.left >= rect.right || rect.top >= rect.bottom;
	}

	RECT ClampRectToCanvas(RECT rect, int width, int height)
	{
		rect.left = std::max(0L, rect.left);
		rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(width), rect.right);
		rect.bottom = std::min(static_cast<LONG>(height), rect.bottom);
		return IsEmptyRect(rect) ? RECT{ 0, 0, 0, 0 } : rect;
	}

	RECT GetFullCanvasRect(int width, int height)
	{
		return RECT{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	}

	RECT RectFromStrokePoints(const std::vector<InkPoint>& points, int width, int height,
		size_t firstIndex, size_t lastIndex)
	{
		if (points.empty() || firstIndex >= points.size()) return {};
		lastIndex = std::min(lastIndex, points.size());
		if (firstIndex >= lastIndex) return {};
		RECT rect = {};
		for (size_t index = firstIndex; index < lastIndex; ++index)
		{
			const float padding = points[index].r + 3.0f;
			UnionRectInPlace(rect, RECT{
				static_cast<LONG>(std::floor(points[index].x - padding)),
				static_cast<LONG>(std::floor(points[index].y - padding)),
				static_cast<LONG>(std::ceil(points[index].x + padding)),
				static_cast<LONG>(std::ceil(points[index].y + padding)) });
		}
		return ClampRectToCanvas(rect, width, height);
	}

	bool UpdateRawPositionAndDetectMovement(ActiveMouseStroke& stroke, const POINT& rawPosition)
	{
		if (!stroke.hasLastRawPosition)
		{
			stroke.lastRawPosition = rawPosition;
			stroke.hasLastRawPosition = true;
			return false;
		}
		const float deltaX = static_cast<float>(rawPosition.x - stroke.lastRawPosition.x);
		const float deltaY = static_cast<float>(rawPosition.y - stroke.lastRawPosition.y);
		if (deltaX * deltaX + deltaY * deltaY <= kIdleMoveThresholdPx * kIdleMoveThresholdPx) return false;
		stroke.lastRawPosition = rawPosition;
		return true;
	}

	void UpdateIdleFreezeState(ActiveMouseStroke& stroke, bool rawMoved, double liveTipDurationSeconds)
	{
		if (rawMoved)
		{
			stroke.visualStableFrameCount = 0;
			stroke.previousL0DrawPoints = stroke.l0DrawPoints;
			return;
		}
		const bool stoppedLongEnough = stroke.logicalInputTime - stroke.lastMovementInputTime >= liveTipDurationSeconds;
		if (stoppedLongEnough && AreL0VisualsClose(stroke.l0DrawPoints, stroke.previousL0DrawPoints))
			++stroke.visualStableFrameCount;
		else
			stroke.visualStableFrameCount = 0;
		stroke.previousL0DrawPoints = stroke.l0DrawPoints;
		if (stroke.visualStableFrameCount >= kVisualStableRequiredFrames) stroke.idleFrozen = true;
	}

	void AppendNewModeledPoints(ActiveMouseStroke& stroke)
	{
		for (size_t index = stroke.convertedResultCount; index < stroke.modeledResults.size(); ++index)
			stroke.realPoints.push_back(stroke.widthEstimator.Append(stroke.modeledResults[index]));
		stroke.convertedResultCount = stroke.modeledResults.size();
	}

	void RebuildPredictedPoints(ActiveMouseStroke& stroke)
	{
		stroke.predictedPoints.clear();
		StrokeWidthEstimator predictionWidth = stroke.widthEstimator;
		for (const auto& result : stroke.predictedResults) stroke.predictedPoints.push_back(predictionWidth.Append(result));
	}

	double GetPredictionDurationSeconds(const ActiveMouseStroke& stroke)
	{
		if (stroke.realPoints.empty() || stroke.predictedPoints.empty()) return 0.0;
		return std::max(0.0, static_cast<double>(stroke.predictedPoints.back().time - stroke.realPoints.back().time));
	}

	void RebuildL0DrawPoints(ActiveMouseStroke& stroke, double liveTipDurationSeconds, int width, int height)
	{
		stroke.l0DrawPoints.clear();
		if (!stroke.realPoints.empty())
		{
			const size_t startIndex = std::min(stroke.committedIndex, stroke.realPoints.size() - 1);
			stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.realPoints.begin() + startIndex, stroke.realPoints.end());
		}
		stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.predictedPoints.begin(), stroke.predictedPoints.end());
		ApplyLiveTipTaper(stroke.l0DrawPoints, liveTipDurationSeconds);
		stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, width, height);
	}

	RECT CommitStablePrefixToL1(ActiveMouseStroke& stroke, double liveTipDurationSeconds,
		double predictionDurationSeconds, DirectX::XMFLOAT4 color, float shapeType,
		bool eraser, InkRenderer& renderer, int width, int height)
	{
		if (stroke.realPoints.size() < 2) return {};
		const size_t protectedStartIndex = FindProtectedStartIndex(
			stroke.realPoints, liveTipDurationSeconds + predictionDurationSeconds);
		if (protectedStartIndex <= stroke.committedIndex) return {};

		std::vector<InkPoint> stablePoints(stroke.realPoints.begin() + stroke.committedIndex,
			stroke.realPoints.begin() + protectedStartIndex + 1);
		renderer.SetOMTarget(renderer.layerL1RTV);
		renderer.DrawStrokeOrDot(stablePoints, color, shapeType, eraser);
		stroke.committedIndex = protectedStartIndex;
		return RectFromStrokePoints(stablePoints, width, height);
	}

	void DrawL0LiveComposite(ActiveMouseStroke& stroke, DirectX::XMFLOAT4 color,
		float shapeType, bool eraser, InkRenderer& renderer)
	{
		renderer.ClearRTV(renderer.layerL0RTV, kTransparentLayerClearColor);
		if (stroke.l0DrawPoints.empty()) return;
		renderer.SetOMTarget(renderer.layerL0RTV);
		renderer.DrawStrokeOrDot(stroke.l0DrawPoints, color, shapeType, eraser);
	}
}
