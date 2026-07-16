module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <iterator>
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
		constexpr float kVisualStableDirectionEpsilon = 0.002f;
		constexpr int kVisualStableRequiredFrames = 3;
		constexpr float kMaxDiameterChangePerBaseDiameterPerSecond = 12.0f;
		constexpr float kMaxRadiusChangePerPixel = 0.8f;
		constexpr float kMaxDirectionEdgeChangePerPixel = 0.8f;
		static_assert(kMaxRadiusChangePerPixel > 0.0f && kMaxRadiusChangePerPixel < 1.0f,
			"半径空间变化率必须严格小于胶囊切线退化阈值");

		float LerpFloat(float from, float to, float ratio)
		{
			return from + (to - from) * ratio;
		}

		float SmoothStep01(float value)
		{
			value = std::clamp(value, 0.0f, 1.0f);
			return value * value * (3.0f - 2.0f * value); // 比线性插值更平滑，避免笔宽突变。
		}

		struct Float2
		{
			float x;
			float y;
		};

		struct SharpQuad
		{
			Float2 corners[4];
		};

		float Cross2D(Float2 a, Float2 b)
		{
			return a.x * b.y - a.y * b.x;
		}

		Float2 Subtract(Float2 a, Float2 b)
		{
			return { a.x - b.x, a.y - b.y };
		}

		Float2 NormalizeOr(Float2 value, Float2 fallback)
		{
			const float valueLength = std::hypot(value.x, value.y);
			return valueLength > 0.00001f
				? Float2{ value.x / valueLength, value.y / valueLength } : fallback;
		}

		bool IsConvexQuad(const SharpQuad& quad)
		{
			float crosses[4] = {};
			for (size_t index = 0; index < 4; ++index)
			{
				const Float2 edge1 = Subtract(quad.corners[(index + 1) % 4], quad.corners[index]);
				const Float2 edge2 = Subtract(quad.corners[(index + 2) % 4], quad.corners[(index + 1) % 4]);
				crosses[index] = Cross2D(edge1, edge2);
			}
			const bool allPositive = std::all_of(std::begin(crosses), std::end(crosses),
				[](float value) { return value > 0.0001f; });
			const bool allNegative = std::all_of(std::begin(crosses), std::end(crosses),
				[](float value) { return value < -0.0001f; });
			return allPositive || allNegative;
		}

		SharpQuad BuildSharpQuad(const InkPoint& startPoint, const InkPoint& endPoint)
		{
			Float2 p1{ startPoint.x, startPoint.y };
			Float2 p2{ endPoint.x, endPoint.y };
			float r1 = startPoint.r;
			float r2 = endPoint.r;
			Float2 direction1 = NormalizeOr({ startPoint.directionX, startPoint.directionY }, { 1.0f, 0.0f });
			Float2 direction2 = NormalizeOr({ endPoint.directionX, endPoint.directionY }, direction1);
			Float2 segment = Subtract(p2, p1);
			float segmentLength = std::hypot(segment.x, segment.y);
			if (segmentLength <= 0.00001f)
			{
				const float halfSize = std::max(r1, r2);
				const Float2 center = p1;
				p1 = { center.x - direction2.x * halfSize, center.y - direction2.y * halfSize };
				p2 = { center.x + direction2.x * halfSize, center.y + direction2.y * halfSize };
				r1 = r2 = halfSize;
				direction1 = direction2;
				segment = Subtract(p2, p1);
				segmentLength = std::hypot(segment.x, segment.y);
			}

			const Float2 tangent = segmentLength > 0.00001f
				? Float2{ segment.x / segmentLength, segment.y / segmentLength } : Float2{ 1.0f, 0.0f };
			if (direction1.x * tangent.x + direction1.y * tangent.y < 0.0f)
				direction1 = { -direction1.x, -direction1.y };
			if (direction2.x * tangent.x + direction2.y * tangent.y < 0.0f)
				direction2 = { -direction2.x, -direction2.y };

			Float2 normal1{ -direction1.y, direction1.x };
			Float2 normal2{ -direction2.y, direction2.x };
			auto makeQuad = [&]()
			{
				return SharpQuad{ {
					{ p1.x + normal1.x * r1, p1.y + normal1.y * r1 },
					{ p2.x + normal2.x * r2, p2.y + normal2.y * r2 },
					{ p2.x - normal2.x * r2, p2.y - normal2.y * r2 },
					{ p1.x - normal1.x * r1, p1.y - normal1.y * r1 }
				} };
			};
			SharpQuad quad = makeQuad();
			if (!IsConvexQuad(quad))
			{
				normal1 = { -tangent.y, tangent.x };
				normal2 = normal1;
				quad = makeQuad();
			}
			return quad;
		}

		float ClampRadiusTransition(float previousRadius, float desiredRadius, float baseDiameter,
			float pointDistance, double deltaTime)
		{
			// 时间限速跟随基准笔宽；空间限速确保两端圆仍能形成有效公切线。
			const float maxRadiusDeltaByTime = 0.5f * std::max(0.0f, baseDiameter) *
				kMaxDiameterChangePerBaseDiameterPerSecond * static_cast<float>(std::max(0.0, deltaTime));
			const float maxRadiusDeltaByDistance = kMaxRadiusChangePerPixel * std::max(0.0f, pointDistance);
			const float maxRadiusDelta = std::min(maxRadiusDeltaByTime, maxRadiusDeltaByDistance);
			return previousRadius + std::clamp(desiredRadius - previousRadius, -maxRadiusDelta, maxRadiusDelta);
		}

		void LimitRadiusTransitions(std::vector<InkPoint>& points, float baseDiameter)
		{
			for (size_t index = 1; index < points.size(); ++index)
			{
				const InkPoint& previous = points[index - 1];
				InkPoint& current = points[index];
				const float pointDistance = std::hypot(current.x - previous.x, current.y - previous.y);
				const double deltaTime = std::max(0.0,
					static_cast<double>(current.time) - static_cast<double>(previous.time));
				current.r = ClampRadiusTransition(previous.r, current.r, baseDiameter, pointDistance, deltaTime);
			}
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
				if (deltaX * deltaX + deltaY * deltaY > positionEpsilonSquared) return false; // 位置仍变化时不能冻结。
				if (std::abs(current[index].r - previous[index].r) > kVisualStableRadiusEpsilonPx) return false; // 半径还在收敛时也继续刷新。
				const float directionDeltaX = current[index].directionX - previous[index].directionX;
				const float directionDeltaY = current[index].directionY - previous[index].directionY;
				if (directionDeltaX * directionDeltaX + directionDeltaY * directionDeltaY >
					kVisualStableDirectionEpsilon * kVisualStableDirectionEpsilon) return false;
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
			while (firstTipIndex > 0 && static_cast<double>(points[firstTipIndex - 1].time) >= tipStartTime) --firstTipIndex; // 找到需要渐细的实时尾部起点。

			const double actualTipSpan = std::max(0.0, endTime - static_cast<double>(points[firstTipIndex].time));
			const float spanRatio = SmoothStep01(static_cast<float>(actualTipSpan / liveTipDurationSeconds));
			const float newestScale = LerpFloat(1.0f, 0.28f, spanRatio); // 尾部越完整，最新端点越细。
			for (size_t index = firstTipIndex; index < points.size(); ++index)
			{
				const float ageRatio = actualTipSpan > 0.000001
					? static_cast<float>((endTime - static_cast<double>(points[index].time)) / actualTipSpan)
					: 0.0f;
				points[index].r *= LerpFloat(newestScale, 1.0f, SmoothStep01(ageRatio)); // 从最新端点向旧点逐步恢复正常半径。
			}
		}
	}

	StrokeModelConfiguration CreateStrokeModelConfiguration(int dpiX)
	{
		const StrokeTimingProfile timingProfile = GetStrokeTimingProfile(kActiveStrokeTimingProfileId);
		const float expectedSpeed = 500.0f * (static_cast<float>(dpiX) / 96.0f); // DPI 越高，像素速度按比例放大。
		ink::stroke_model::KalmanPredictorParams kalmanParams;
		kalmanParams.process_noise = 0.05;
		kalmanParams.measurement_noise = 0.01;
		kalmanParams.min_stable_iteration = 4;
		kalmanParams.max_time_samples = timingProfile.kalman_max_time_samples;
		kalmanParams.min_catchup_velocity = expectedSpeed / 1000.0f; // 保证预测点追上真实点时不会过慢。
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
				.speed_floor = timingProfile.wobble_speed_floor_ratio * expectedSpeed, // 低速时启用更强防抖。
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
			params.prediction_params = ink::stroke_model::DisabledPredictorParams{}; // 完全不输出预测点，便于对比延迟。
			break;
		case InkPredictionMode::StrokeEnd:
			params.prediction_params = ink::stroke_model::StrokeEndPredictorParams{}; // 只使用库内置的笔尾预测。
			break;
		default:
			params.prediction_params = kalmanPredictorParams; // 默认使用调过参的 Kalman 预测。
			break;
		}
	}

	StrokeWidthEstimator::StrokeWidthEstimator(float baseDiameterValue, float expectedSpeedValue)
		: baseDiameter(baseDiameterValue), minDiameter(baseDiameterValue * 0.8f),
		maxDiameter(baseDiameterValue * 1.4f), expectedSpeed(std::max(1.0f, expectedSpeedValue)),
		currentDiameter(baseDiameterValue)
	{
	}

	InkPoint StrokeWidthEstimator::Append(const ink::stroke_model::Result& result, float inputSpeed)
	{
		const double pointTime = result.time.Value();
		if (!hasSample)
		{
			currentDiameter = baseDiameter;
			hasSample = true;
		}
		if (inputSpeed >= 0.0f)
		{
			const float targetDiameter = LerpFloat(maxDiameter, minDiameter,
				SmoothStep01(inputSpeed / expectedSpeed)); // 使用原始输入速度，避免弹簧速度的起步坡度和过冲污染笔宽。
			if (!hasInputSpeed)
			{
				currentDiameter = targetDiameter; // 第一个有效速度负责确定起笔宽度，不使用必然为零的 down 速度。
				hasInputSpeed = true;
			}
			else
			{
				const double deltaTime = std::max(0.0, pointTime - lastTime);
				const float alpha = std::clamp(static_cast<float>(1.0 - std::exp(-deltaTime / 0.035)), 0.05f, 0.65f); // 时间间隔越大，笔宽追随越快。
				const float desiredDiameter = LerpFloat(currentDiameter, targetDiameter, alpha); // 先保留原有指数平滑手感。
				const float pointDistance = std::hypot(result.position.x - lastPositionX, result.position.y - lastPositionY);
				currentDiameter = 2.0f * ClampRadiusTransition(currentDiameter * 0.5f, desiredDiameter * 0.5f,
					baseDiameter, pointDistance, deltaTime); // 再做对称硬限速，避免胶囊退化为外露端帽。
			}
		}
		lastTime = pointTime;
		lastPositionX = result.position.x;
		lastPositionY = result.position.y;
		return { result.position.x, result.position.y, currentDiameter * 0.5f, static_cast<float>(pointTime) };
	}

	StrokeDirectionEstimator::StrokeDirectionEstimator(float responseDistanceValue)
		: responseDistance(std::max(0.001f, responseDistanceValue))
	{
	}

	void StrokeDirectionEstimator::Append(InkPoint& point)
	{
		if (!hasPosition)
		{
			lastPositionX = point.x;
			lastPositionY = point.y;
			hasPosition = true;
			point.directionX = directionX;
			point.directionY = directionY;
			return;
		}

		const float deltaX = point.x - lastPositionX;
		const float deltaY = point.y - lastPositionY;
		const float pointDistance = std::hypot(deltaX, deltaY);
		if (pointDistance > 0.00001f)
		{
			const float rawDirectionX = deltaX / pointDistance;
			const float rawDirectionY = deltaY / pointDistance;
			if (!hasDirection)
			{
				directionX = rawDirectionX;
				directionY = rawDirectionY;
				hasDirection = true;
			}
			else
			{
				const float directionDot = std::clamp(directionX * rawDirectionX + directionY * rawDirectionY, -1.0f, 1.0f);
				const float directionCross = directionX * rawDirectionY - directionY * rawDirectionX;
				const float angularDifference = std::atan2(directionCross, directionDot);
				const float averageWeight = 1.0f - std::exp(-pointDistance / responseDistance);
				const float desiredAngleChange = angularDifference * averageWeight;
				const float maxAngleChange = kMaxDirectionEdgeChangePerPixel * pointDistance /
					std::max(point.r, 0.001f); // 限制横截面边缘位移，避免短线段上的四边形翻转。
				const float angleChange = std::clamp(desiredAngleChange, -maxAngleChange, maxAngleChange);
				const float cosine = std::cos(angleChange);
				const float sine = std::sin(angleChange);
				const float rotatedX = directionX * cosine - directionY * sine;
				const float rotatedY = directionX * sine + directionY * cosine;
				const float rotatedLength = std::hypot(rotatedX, rotatedY);
				if (rotatedLength > 0.00001f)
				{
					directionX = rotatedX / rotatedLength;
					directionY = rotatedY / rotatedLength;
				}
			}
		}
		lastPositionX = point.x;
		lastPositionY = point.y;
		point.directionX = directionX;
		point.directionY = directionY;
	}

	ActiveMouseStroke::ActiveMouseStroke(float baseDiameter, float expectedSpeed,
		bool fixedWidthValue, bool trackDirectionValue)
		: widthEstimator(baseDiameter, expectedSpeed), directionEstimator(baseDiameter),
		fixedWidth(fixedWidthValue), trackDirection(trackDirectionValue)
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
		StrokeShape shape, size_t firstIndex, size_t lastIndex)
	{
		if (points.empty() || firstIndex >= points.size()) return {};
		lastIndex = std::min(lastIndex, points.size());
		if (firstIndex >= lastIndex) return {};
		RECT rect = {};
		if (shape == StrokeShape::SharpStrip)
		{
			const size_t segmentEnd = lastIndex > firstIndex + 1 ? lastIndex - 1 : firstIndex + 1;
			for (size_t index = firstIndex; index < segmentEnd; ++index)
			{
				const InkPoint& startPoint = points[index];
				const InkPoint& endPoint = index + 1 < lastIndex ? points[index + 1] : points[index];
				const SharpQuad quad = BuildSharpQuad(startPoint, endPoint);
				float minX = quad.corners[0].x;
				float minY = quad.corners[0].y;
				float maxX = quad.corners[0].x;
				float maxY = quad.corners[0].y;
				for (size_t cornerIndex = 1; cornerIndex < 4; ++cornerIndex)
				{
					minX = std::min(minX, quad.corners[cornerIndex].x);
					minY = std::min(minY, quad.corners[cornerIndex].y);
					maxX = std::max(maxX, quad.corners[cornerIndex].x);
					maxY = std::max(maxY, quad.corners[cornerIndex].y);
				}
				UnionRectInPlace(rect, RECT{
					static_cast<LONG>(std::floor(minX - 3.0f)),
					static_cast<LONG>(std::floor(minY - 3.0f)),
					static_cast<LONG>(std::ceil(maxX + 3.0f)),
					static_cast<LONG>(std::ceil(maxY + 3.0f)) });
			}
			return ClampRectToCanvas(rect, width, height);
		}

		for (size_t index = firstIndex; index < lastIndex; ++index)
		{
			const float padding = points[index].r + 3.0f; // 额外 3px 覆盖抗锯齿和胶囊端点。
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
		if (deltaX * deltaX + deltaY * deltaY <= kIdleMoveThresholdPx * kIdleMoveThresholdPx) return false; // 忽略小于阈值的鼠标抖动。
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
			++stroke.visualStableFrameCount; // 连续多帧几乎不变才认为视觉已经稳定。
		else
			stroke.visualStableFrameCount = 0;
		stroke.previousL0DrawPoints = stroke.l0DrawPoints;
		if (stroke.visualStableFrameCount >= kVisualStableRequiredFrames) stroke.idleFrozen = true; // 冻结后不再持续喂入相同坐标。
	}

	void AppendNewModeledPoints(ActiveMouseStroke& stroke, float inputSpeed)
	{
		const float effectiveInputSpeed = stroke.fixedWidth ? -1.0f : inputSpeed;
		const bool seedInitialWidth = effectiveInputSpeed >= 0.0f && !stroke.widthEstimator.hasInputSpeed && stroke.committedIndex == 0;
		for (size_t index = stroke.convertedResultCount; index < stroke.modeledResults.size(); ++index)
		{
			InkPoint point = stroke.widthEstimator.Append(stroke.modeledResults[index], effectiveInputSpeed);
			if (stroke.fixedWidth) point.r = stroke.widthEstimator.baseDiameter * 0.5f;
			const bool hadDirection = stroke.directionEstimator.hasDirection;
			if (stroke.trackDirection)
			{
				stroke.directionEstimator.Append(point);
				if (!hadDirection && stroke.directionEstimator.hasDirection)
				{
					// 首个有效走势只回填此前尚无方向的起笔点，保留本批后续点各自的平滑结果。
					for (size_t pointIndex = stroke.committedIndex; pointIndex < stroke.realPoints.size(); ++pointIndex)
					{
						stroke.realPoints[pointIndex].directionX = stroke.directionEstimator.directionX;
						stroke.realPoints[pointIndex].directionY = stroke.directionEstimator.directionY;
					}
				}
			}
			stroke.realPoints.push_back(point);
		}
		if (seedInitialWidth && stroke.widthEstimator.hasInputSpeed)
		{
			const float initialRadius = stroke.widthEstimator.currentDiameter * 0.5f;
			for (InkPoint& point : stroke.realPoints) point.r = initialRadius; // 首次移动仍在 L0，可安全回填起笔宽度以消除启动鼓包。
		}
		stroke.convertedResultCount = stroke.modeledResults.size(); // 记录已转换位置，下一帧只处理增量。
	}

	void RebuildPredictedPoints(ActiveMouseStroke& stroke)
	{
		stroke.predictedPoints.clear();
		StrokeWidthEstimator predictionWidth = stroke.widthEstimator;
		StrokeDirectionEstimator predictionDirection = stroke.directionEstimator;
		// 预测器只预测几何位置，笔宽继承最后真实输入，避免预测速度变化造成尾部回粗。
		for (const auto& result : stroke.predictedResults)
		{
			InkPoint point = predictionWidth.Append(result);
			if (stroke.fixedWidth) point.r = stroke.widthEstimator.baseDiameter * 0.5f;
			if (stroke.trackDirection) predictionDirection.Append(point); // 副本保证预测走势不会污染真实点状态。
			stroke.predictedPoints.push_back(point);
		}
	}

	double GetPredictionDurationSeconds(const ActiveMouseStroke& stroke)
	{
		if (stroke.realPoints.empty() || stroke.predictedPoints.empty()) return 0.0;
		return std::max(0.0, static_cast<double>(stroke.predictedPoints.back().time - stroke.realPoints.back().time));
	}

	void RebuildL0DrawPoints(ActiveMouseStroke& stroke, double liveTipDurationSeconds,
		StrokeShape shape, int width, int height)
	{
		stroke.l0DrawPoints.clear();
		if (!stroke.realPoints.empty())
		{
			const size_t startIndex = std::min(stroke.committedIndex, stroke.realPoints.size() - 1); // 从最后已提交点开始保留连接。
			stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.realPoints.begin() + startIndex, stroke.realPoints.end());
		}
		stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.predictedPoints.begin(), stroke.predictedPoints.end()); // 预测点只放在 L0，便于下一帧擦除重画。
		ApplyLiveTipTaper(stroke.l0DrawPoints, liveTipDurationSeconds);
		LimitRadiusTransitions(stroke.l0DrawPoints, stroke.widthEstimator.baseDiameter); // 收细会再次改半径，绘制前必须重新保证相邻段的几何约束。
		stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, width, height, shape);
	}

	RECT CommitStablePrefixToL1(ActiveMouseStroke& stroke, double liveTipDurationSeconds,
		double predictionDurationSeconds, DirectX::XMFLOAT4 color, StrokeShape shape,
		InkRenderer& renderer, int width, int height)
	{
		if (stroke.realPoints.size() < 2) return {};
		if (stroke.trackDirection && !stroke.directionEstimator.hasDirection) return {}; // 未移动前留在 L0，避免默认方向方块过早烘干。
		const size_t protectedStartIndex = FindProtectedStartIndex(
			stroke.realPoints, liveTipDurationSeconds + predictionDurationSeconds); // 尾部和预测窗口内的点暂不落到 L1。
		if (protectedStartIndex <= stroke.committedIndex) return {};

		std::vector<InkPoint> stablePoints(stroke.realPoints.begin() + stroke.committedIndex,
			stroke.realPoints.begin() + protectedStartIndex + 1);
		renderer.SetOMTarget(renderer.layerL1RTV.Get());
		renderer.DrawStrokeOrDot(stablePoints, color, shape);
		stroke.committedIndex = protectedStartIndex; // 推进提交游标，后续帧不重复提交稳定前缀。
		return RectFromStrokePoints(stablePoints, width, height, shape);
	}

	void DrawL0LiveComposite(ActiveMouseStroke& stroke, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkRenderer& renderer)
	{
		renderer.ClearRTV(renderer.layerL0RTV.Get(), kTransparentLayerClearColor); // L0 每帧从零重画，才能移除旧预测。
		if (stroke.l0DrawPoints.empty()) return;
		renderer.SetOMTarget(renderer.layerL0RTV.Get());
		renderer.DrawStrokeOrDot(stroke.l0DrawPoints, color, shape);
	}
}
