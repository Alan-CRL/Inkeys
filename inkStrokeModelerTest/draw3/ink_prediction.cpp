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
		constexpr int kVisualStableRequiredFrames = 3;
		constexpr float kMaxDiameterChangePerBaseDiameterPerSecond = 12.0f;
		constexpr float kMaxRadiusChangePerPixel = 0.8f;
		constexpr float kHighlighterDuplicateDistancePx = 0.01f;
		constexpr float kHighlighterShortStrokeLengthPx = kHighlighterMinimumStrokeLengthPx;
		constexpr float kHighlighterRadiusPx = 25.0f;
		constexpr float kHighlighterBodyOverlapPx = 2.0f;
		constexpr float kHighlighterBoundsPaddingPx = 3.0f;
		constexpr float kHighlighterJoinThresholdRadians = 0.5f * 3.14159265358979323846f / 180.0f;
		constexpr float kHighlighterCircleJoinThresholdRadians = 177.0f * 3.14159265358979323846f / 180.0f;
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

		float Cross2D(Float2 a, Float2 b)
		{
			return a.x * b.y - a.y * b.x;
		}

		float Dot2D(Float2 a, Float2 b)
		{
			return a.x * b.x + a.y * b.y;
		}

		Float2 Subtract(Float2 a, Float2 b)
		{
			return { a.x - b.x, a.y - b.y };
		}

		Float2 Add(Float2 a, Float2 b)
		{
			return { a.x + b.x, a.y + b.y };
		}

		Float2 Multiply(Float2 value, float scale)
		{
			return { value.x * scale, value.y * scale };
		}

		float Length(Float2 value)
		{
			return std::hypot(value.x, value.y);
		}

		Float2 NormalizeOr(Float2 value, Float2 fallback)
		{
			const float valueLength = std::hypot(value.x, value.y);
			return valueLength > 0.00001f
				? Float2{ value.x / valueLength, value.y / valueLength } : fallback;
		}

		Float2 ToFloat2(const InkPoint& point)
		{
			return { point.x, point.y };
		}

		DirectX::XMFLOAT2 ToXMFLOAT2(Float2 point)
		{
			return { point.x, point.y };
		}

		bool HasHighlighterBoundary(HighlighterBoundaryFlags flags, HighlighterBoundaryFlags value)
		{
			return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(value)) != 0;
		}

		InkPoint InterpolatePoint(const InkPoint& start, const InkPoint& end, float ratio)
		{
			ratio = std::clamp(ratio, 0.0f, 1.0f);
			return {
				LerpFloat(start.x, end.x, ratio), LerpFloat(start.y, end.y, ratio),
				LerpFloat(start.r, end.r, ratio), LerpFloat(start.time, end.time, ratio)
			};
		}

		void AppendDistinctPoint(std::vector<InkPoint>& points, const InkPoint& point)
		{
			if (!points.empty())
			{
				const float deltaX = point.x - points.back().x;
				const float deltaY = point.y - points.back().y;
				if (deltaX * deltaX + deltaY * deltaY <=
					kHighlighterDuplicateDistancePx * kHighlighterDuplicateDistancePx) return;
			}
			points.push_back(point);
		}

		std::vector<InkPoint> DeduplicateHighlighterPoints(const std::vector<InkPoint>& points)
		{
			std::vector<InkPoint> deduplicated;
			deduplicated.reserve(points.size());
			for (const InkPoint& point : points) AppendDistinctPoint(deduplicated, point);
			return deduplicated;
		}

		InkPoint PointAtPathDistance(const std::vector<InkPoint>& points,
			const std::vector<float>& cumulativeDistances, float distance)
		{
			if (points.empty()) return {};
			if (distance <= 0.0f) return points.front();
			if (distance >= cumulativeDistances.back()) return points.back();
			for (size_t index = 1; index < points.size(); ++index)
			{
				if (cumulativeDistances[index] < distance) continue;
				const float segmentLength = cumulativeDistances[index] - cumulativeDistances[index - 1];
				const float ratio = segmentLength > 0.0f
					? (distance - cumulativeDistances[index - 1]) / segmentLength : 0.0f;
				return InterpolatePoint(points[index - 1], points[index], ratio);
			}
			return points.back();
		}

		std::vector<InkPoint> BuildHighlighterRenderPath(const std::vector<InkPoint>& inputPoints,
			HighlighterBoundaryFlags boundaryFlags, const HighlighterStartDirectionState& startDirectionState)
		{
			std::vector<InkPoint> points = DeduplicateHighlighterPoints(inputPoints);
			if (points.size() < 2) return points;

			std::vector<float> cumulativeDistances(points.size(), 0.0f);
			for (size_t index = 1; index < points.size(); ++index)
				cumulativeDistances[index] = cumulativeDistances[index - 1] +
					Length(Subtract(ToFloat2(points[index]), ToFloat2(points[index - 1])));
			const float totalLength = cumulativeDistances.back();
			const bool collapseStart = HasHighlighterBoundary(boundaryFlags, HighlighterBoundaryFlags::Start) &&
				startDirectionState.locked && totalLength >= kHighlighterShortStrokeLengthPx;
			const bool collapseEnd = HasHighlighterBoundary(boundaryFlags, HighlighterBoundaryFlags::End);
			const float startCut = collapseStart ? kHighlighterShortStrokeLengthPx : 0.0f;
			const float endCut = collapseEnd
				? std::max(0.0f, totalLength - kHighlighterShortStrokeLengthPx) : totalLength;

			// 两个 12px 稳定窗口相交时整段折叠为一条弦，避免生成倒序小段。
			if (collapseStart && collapseEnd && startCut >= endCut)
				return { points.front(), points.back() };

			std::vector<InkPoint> renderPath;
			renderPath.reserve(points.size() + 2);
			AppendDistinctPoint(renderPath, points.front());
			if (startCut > 0.0f)
			{
				InkPoint startAnchor = PointAtPathDistance(points, cumulativeDistances, startCut);
				const float chordLength = Length(Subtract(ToFloat2(startAnchor), ToFloat2(points.front())));
				const Float2 lockedDirection = NormalizeOr(
					{ startDirectionState.direction.x, startDirectionState.direction.y }, { 1.0f, 0.0f });
				startAnchor.x = points.front().x + lockedDirection.x * chordLength;
				startAnchor.y = points.front().y + lockedDirection.y * chordLength;
				AppendDistinctPoint(renderPath, startAnchor); // 使用锁定方向，后续帧不再让起笔平帽旋转。
			}
			for (size_t index = 1; index + 1 < points.size(); ++index)
			{
				if (cumulativeDistances[index] > startCut && cumulativeDistances[index] < endCut)
					AppendDistinctPoint(renderPath, points[index]);
			}
			if (endCut < totalLength)
				AppendDistinctPoint(renderPath, PointAtPathDistance(points, cumulativeDistances, endCut));
			AppendDistinctPoint(renderPath, points.back());
			return renderPath;
		}

		void IncludeHighlighterBounds(RECT& bounds, float minX, float minY, float maxX, float maxY)
		{
			const RECT addition = {
				static_cast<LONG>(std::floor(minX - kHighlighterBoundsPaddingPx)),
				static_cast<LONG>(std::floor(minY - kHighlighterBoundsPaddingPx)),
				static_cast<LONG>(std::ceil(maxX + kHighlighterBoundsPaddingPx)),
				static_cast<LONG>(std::ceil(maxY + kHighlighterBoundsPaddingPx))
			};
			if (bounds.left >= bounds.right || bounds.top >= bounds.bottom)
				bounds = addition;
			else
			{
				bounds.left = std::min(bounds.left, addition.left);
				bounds.top = std::min(bounds.top, addition.top);
				bounds.right = std::max(bounds.right, addition.right);
				bounds.bottom = std::max(bounds.bottom, addition.bottom);
			}
		}

		void IncludeHighlighterBodyBounds(RECT& bounds, Float2 p1, Float2 p2, float radius,
			float startExtension, float endExtension)
		{
			const Float2 tangent = NormalizeOr(Subtract(p2, p1), { 1.0f, 0.0f });
			const Float2 normal{ -tangent.y, tangent.x };
			p1 = Subtract(p1, Multiply(tangent, startExtension));
			p2 = Add(p2, Multiply(tangent, endExtension));
			const Float2 corners[] = {
				Add(p1, Multiply(normal, radius)), Subtract(p1, Multiply(normal, radius)),
				Add(p2, Multiply(normal, radius)), Subtract(p2, Multiply(normal, radius))
			};
			float minX = corners[0].x;
			float minY = corners[0].y;
			float maxX = corners[0].x;
			float maxY = corners[0].y;
			for (size_t index = 1; index < std::size(corners); ++index)
			{
				minX = std::min(minX, corners[index].x);
				minY = std::min(minY, corners[index].y);
				maxX = std::max(maxX, corners[index].x);
				maxY = std::max(maxY, corners[index].y);
			}
			IncludeHighlighterBounds(bounds, minX, minY, maxX, maxY);
		}

		size_t FindTrailingDistanceStartIndex(const std::vector<InkPoint>& points, float protectedDistance)
		{
			if (points.size() < 2) return 0;
			float accumulatedDistance = 0.0f;
			for (size_t index = points.size() - 1; index > 0; --index)
			{
				accumulatedDistance += Length(Subtract(ToFloat2(points[index]), ToFloat2(points[index - 1])));
				if (accumulatedDistance >= protectedDistance) return index - 1;
			}
			return 0;
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

	HighlighterGeometry BuildHighlighterGeometry(const std::vector<InkPoint>& inputPoints,
		HighlighterBoundaryFlags boundaryFlags, bool shortStrokeMode,
		const HighlighterStartDirectionState& startDirectionState)
	{
		HighlighterGeometry geometry;
		if (inputPoints.empty()) return geometry;

		if (shortStrokeMode)
		{
			const Float2 start = ToFloat2(inputPoints.front());
			const Float2 direction = NormalizeOr(
				{ startDirectionState.direction.x, startDirectionState.direction.y }, { 1.0f, 0.0f });
			const Float2 end = Add(start, Multiply(direction, kHighlighterShortStrokeLengthPx));
			HighlighterPrimitive primitive;
			primitive.p1 = ToXMFLOAT2(start);
			primitive.p2 = ToXMFLOAT2(end);
			primitive.direction1 = ToXMFLOAT2(direction);
			primitive.radius = kHighlighterRadiusPx;
			primitive.type = HighlighterPrimitiveType::ShortMark;
			geometry.primitives.push_back(primitive);
			IncludeHighlighterBodyBounds(geometry.bounds, start, end, kHighlighterRadiusPx, 0.0f, 0.0f);
			return geometry;
		}

		const std::vector<InkPoint> points = BuildHighlighterRenderPath(
			inputPoints, boundaryFlags, startDirectionState);
		if (points.size() < 2) return geometry; // 纯点击按住期间不生成任何几何。

		const bool hasGlobalStart = HasHighlighterBoundary(boundaryFlags, HighlighterBoundaryFlags::Start);
		const bool hasGlobalEnd = HasHighlighterBoundary(boundaryFlags, HighlighterBoundaryFlags::End);
		geometry.primitives.reserve(points.size() * 2);
		for (size_t index = 0; index + 1 < points.size(); ++index)
		{
			const Float2 p1 = ToFloat2(points[index]);
			const Float2 p2 = ToFloat2(points[index + 1]);
			if (Length(Subtract(p2, p1)) <= kHighlighterDuplicateDistancePx) continue;

			HighlighterPrimitive primitive;
			primitive.p1 = ToXMFLOAT2(p1);
			primitive.p2 = ToXMFLOAT2(p2);
			primitive.radius = kHighlighterRadiusPx;
			primitive.startExtension = index == 0 && hasGlobalStart ? 0.0f : kHighlighterBodyOverlapPx;
			primitive.endExtension = index + 2 == points.size() && hasGlobalEnd
				? 0.0f : kHighlighterBodyOverlapPx;
			primitive.type = HighlighterPrimitiveType::Body;
			geometry.primitives.push_back(primitive);
			IncludeHighlighterBodyBounds(geometry.bounds, p1, p2, primitive.radius,
				primitive.startExtension, primitive.endExtension);
		}

		for (size_t index = 1; index + 1 < points.size(); ++index)
		{
			const Float2 center = ToFloat2(points[index]);
			const Float2 incoming = NormalizeOr(Subtract(center, ToFloat2(points[index - 1])), { 1.0f, 0.0f });
			const Float2 outgoing = NormalizeOr(Subtract(ToFloat2(points[index + 1]), center), incoming);
			const float directionDot = std::clamp(Dot2D(incoming, outgoing), -1.0f, 1.0f);
			const float angle = std::acos(directionDot);
			if (angle <= kHighlighterJoinThresholdRadians) continue;

			HighlighterPrimitive primitive;
			primitive.p1 = ToXMFLOAT2(center);
			primitive.radius = kHighlighterRadiusPx;
			if (angle >= kHighlighterCircleJoinThresholdRadians)
			{
				primitive.type = HighlighterPrimitiveType::RoundJoinCircle;
			}
			else
			{
				const float turnSign = Cross2D(incoming, outgoing) >= 0.0f ? 1.0f : -1.0f;
				const Float2 incomingLeft{ -incoming.y, incoming.x };
				const Float2 outgoingLeft{ -outgoing.y, outgoing.x };
				const Float2 outerIncoming = turnSign > 0.0f ? Multiply(incomingLeft, -1.0f) : incomingLeft;
				const Float2 outerOutgoing = turnSign > 0.0f ? Multiply(outgoingLeft, -1.0f) : outgoingLeft;
				primitive.direction1 = ToXMFLOAT2(outerIncoming);
				primitive.direction2 = ToXMFLOAT2(outerOutgoing);
				primitive.startExtension = turnSign; // sector 在 GPU 端用符号选择顺/逆时针外侧圆弧。
				primitive.type = HighlighterPrimitiveType::RoundJoinSector;
			}
			geometry.primitives.push_back(primitive);
			IncludeHighlighterBounds(geometry.bounds, center.x - primitive.radius, center.y - primitive.radius,
				center.x + primitive.radius, center.y + primitive.radius);
		}
		return geometry;
	}

	HighlighterStartDirectionState GetHighlighterShortStrokeDirectionState(const ActiveStroke& stroke)
	{
		HighlighterStartDirectionState state;
		state.locked = true;
		if (!stroke.realPoints.empty())
		{
			const InkPoint& startPoint = stroke.hasInputStartPoint ? stroke.inputStartPoint : stroke.realPoints.front();
			const Float2 chord = Subtract(ToFloat2(stroke.realPoints.back()), ToFloat2(startPoint));
			if (Length(chord) > kHighlighterDuplicateDistancePx)
			{
				const Float2 direction = NormalizeOr(chord, { 1.0f, 0.0f });
				state.direction = ToXMFLOAT2(direction);
				return state;
			}
		}
		if (stroke.hasFirstMovementDirection) state.direction = stroke.firstMovementDirection;
		return state; // 完全没有有效移动时保留 +X，点击点位于 12×50 矩形左侧中点。
	}

	ActiveStroke::ActiveStroke(float baseDiameter, float expectedSpeed,
		StrokeWidthMode widthModeValue, bool highlighterValue)
	{
		Reset(baseDiameter, expectedSpeed, widthModeValue, highlighterValue);
	}

	void ActiveStroke::Reset(float baseDiameter, float expectedSpeed,
		StrokeWidthMode widthModeValue, bool highlighterValue)
	{
		modeledResults.clear();
		predictedResults.clear();
		realPoints.clear();
		predictedPoints.clear();
		l0DrawPoints.clear();
		previousL0DrawPoints.clear();
		l0HighlighterGeometry.primitives.clear();
		l0HighlighterGeometry.bounds = {};
		convertedResultCount = 0;
		committedIndex = 0;
		lastL0Rect = {};
		currentL0Rect = {};
		widthEstimator = StrokeWidthEstimator(baseDiameter, expectedSpeed);
		widthMode = widthModeValue;
		highlighter = highlighterValue;
		hasCommittedGeometry = false;
		realPathLength = 0.0f;
		inputStartPoint = {};
		hasInputStartPoint = false;
		firstMovementDirection = { 1.0f, 0.0f };
		hasFirstMovementDirection = false;
		startDirectionState = {};
		lastRawPosition = {};
		hasLastRawPosition = false;
		idleFrozen = false;
		visualStableFrameCount = 0;
		lastMovementInputTime = 0.0;
		lastFrameWallTime = 0.0;
		logicalInputTime = 0.0;
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

	bool UpdateRawPositionAndDetectMovement(ActiveStroke& stroke, const POINT& rawPosition)
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

	void UpdateIdleFreezeState(ActiveStroke& stroke, bool rawMoved, double liveTipDurationSeconds)
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

	void AppendNewModeledPoints(ActiveStroke& stroke, float inputSpeed)
	{
		const bool fixedWidth = stroke.widthMode == StrokeWidthMode::Fixed;
		const float effectiveInputSpeed = fixedWidth ? -1.0f : inputSpeed;
		const bool seedInitialWidth = effectiveInputSpeed >= 0.0f && !stroke.widthEstimator.hasInputSpeed && stroke.committedIndex == 0;
		for (size_t index = stroke.convertedResultCount; index < stroke.modeledResults.size(); ++index)
		{
			const auto& result = stroke.modeledResults[index];
			// 固定宽度工具只取建模后的位置与时间，不经过普通笔的模拟压感估算器。
			InkPoint point = fixedWidth
				? InkPoint{ result.position.x, result.position.y, stroke.widthEstimator.baseDiameter * 0.5f,
					static_cast<float>(result.time.Value()) }
				: stroke.widthEstimator.Append(result, effectiveInputSpeed);
			if (stroke.highlighter && stroke.realPoints.empty() && stroke.hasInputStartPoint)
			{
				point.x = stroke.inputStartPoint.x;
				point.y = stroke.inputStartPoint.y; // 长笔画和最终短划共用完全相同的按下起点。
			}
			if (stroke.highlighter && !stroke.realPoints.empty())
			{
				const float deltaX = point.x - stroke.realPoints.back().x;
				const float deltaY = point.y - stroke.realPoints.back().y;
				const float pointDistance = std::hypot(deltaX, deltaY);
				if (pointDistance <= kHighlighterDuplicateDistancePx) continue; // 重复点不再生成零长度 body 或方块。
				stroke.realPathLength += pointDistance;
				if (!stroke.hasFirstMovementDirection)
				{
					stroke.firstMovementDirection = { deltaX / pointDistance, deltaY / pointDistance };
					stroke.hasFirstMovementDirection = true;
				}
			}
			stroke.realPoints.push_back(point);

			if (stroke.highlighter && !stroke.startDirectionState.locked &&
				stroke.realPathLength >= kHighlighterShortStrokeLengthPx)
			{
				std::vector<float> cumulativeDistances(stroke.realPoints.size(), 0.0f);
				for (size_t pointIndex = 1; pointIndex < stroke.realPoints.size(); ++pointIndex)
					cumulativeDistances[pointIndex] = cumulativeDistances[pointIndex - 1] +
						Length(Subtract(ToFloat2(stroke.realPoints[pointIndex]), ToFloat2(stroke.realPoints[pointIndex - 1])));
				const InkPoint chordEnd = PointAtPathDistance(stroke.realPoints, cumulativeDistances,
					kHighlighterShortStrokeLengthPx);
				Float2 chord = Subtract(ToFloat2(chordEnd), ToFloat2(stroke.realPoints.front()));
				chord = NormalizeOr(chord, { stroke.firstMovementDirection.x, stroke.firstMovementDirection.y });
				stroke.startDirectionState.direction = ToXMFLOAT2(chord);
				stroke.startDirectionState.locked = true; // 首 12px 真实路径一旦完整，起笔平帽方向不再变化。
			}
		}
		if (seedInitialWidth && stroke.widthEstimator.hasInputSpeed)
		{
			const float initialRadius = stroke.widthEstimator.currentDiameter * 0.5f;
			for (InkPoint& point : stroke.realPoints) point.r = initialRadius; // 首次移动仍在 L0，可安全回填起笔宽度以消除启动鼓包。
		}
		stroke.convertedResultCount = stroke.modeledResults.size(); // 记录已转换位置，下一帧只处理增量。
	}

	void RebuildPredictedPoints(ActiveStroke& stroke)
	{
		stroke.predictedPoints.clear();
		StrokeWidthEstimator predictionWidth = stroke.widthEstimator;
		const bool fixedWidth = stroke.widthMode == StrokeWidthMode::Fixed;
		// 预测器只预测几何位置，笔宽继承最后真实输入，避免预测速度变化造成尾部回粗。
		for (const auto& result : stroke.predictedResults)
		{
			InkPoint point = fixedWidth
				? InkPoint{ result.position.x, result.position.y, stroke.widthEstimator.baseDiameter * 0.5f,
					static_cast<float>(result.time.Value()) }
				: predictionWidth.Append(result);
			if (stroke.highlighter)
			{
				const InkPoint* previous = !stroke.predictedPoints.empty() ? &stroke.predictedPoints.back()
					: !stroke.realPoints.empty() ? &stroke.realPoints.back() : nullptr;
				if (previous && std::hypot(point.x - previous->x, point.y - previous->y) <=
					kHighlighterDuplicateDistancePx) continue;
			}
			stroke.predictedPoints.push_back(point);
		}
	}

	double GetPredictionDurationSeconds(const ActiveStroke& stroke)
	{
		if (stroke.realPoints.empty() || stroke.predictedPoints.empty()) return 0.0;
		return std::max(0.0, static_cast<double>(stroke.predictedPoints.back().time - stroke.realPoints.back().time));
	}

	void RebuildL0DrawPoints(ActiveStroke& stroke, double liveTipDurationSeconds,
		StrokeShape shape, int width, int height)
	{
		stroke.l0DrawPoints.clear();
		stroke.l0HighlighterGeometry = {};
		if (!stroke.realPoints.empty())
		{
			const size_t contextIndex = stroke.highlighter && stroke.committedIndex > 0
				? stroke.committedIndex - 1 : stroke.committedIndex; // 荧光笔多留一个上下文点，跨层 round join 才完整。
			const size_t startIndex = std::min(contextIndex, stroke.realPoints.size() - 1);
			stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.realPoints.begin() + startIndex, stroke.realPoints.end());
			if (stroke.highlighter)
			{
				HighlighterBoundaryFlags flags = HighlighterBoundaryFlags::End;
				if (startIndex == 0) flags = flags | HighlighterBoundaryFlags::Start;
				if (stroke.realPathLength >= kHighlighterShortStrokeLengthPx)
					stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.predictedPoints.begin(), stroke.predictedPoints.end());
				stroke.l0HighlighterGeometry = BuildHighlighterGeometry(
					stroke.l0DrawPoints, flags, false, stroke.startDirectionState);
				stroke.currentL0Rect = ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, width, height);
				return;
			}
		}
		stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.predictedPoints.begin(), stroke.predictedPoints.end()); // 预测点只放在 L0，便于下一帧擦除重画。
		ApplyLiveTipTaper(stroke.l0DrawPoints, liveTipDurationSeconds);
		LimitRadiusTransitions(stroke.l0DrawPoints, stroke.widthEstimator.baseDiameter); // 收细会再次改半径，绘制前必须重新保证相邻段的几何约束。
		stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, width, height, shape);
	}

	RECT CommitStablePrefixToL1(ActiveStroke& stroke, double liveTipDurationSeconds,
		double predictionDurationSeconds, DirectX::XMFLOAT4 color, StrokeShape shape,
		InkRenderer& renderer, int width, int height)
	{
		if (stroke.realPoints.size() < 2) return {};
		if (stroke.highlighter && !stroke.startDirectionState.locked) return {}; // 首 12px 方向锁定前不烘干起笔。
		size_t protectedStartIndex = FindProtectedStartIndex(
			stroke.realPoints, liveTipDurationSeconds + predictionDurationSeconds); // 尾部和预测窗口内的点暂不落到 L1。
		if (stroke.highlighter)
			protectedStartIndex = std::min(protectedStartIndex,
				FindTrailingDistanceStartIndex(stroke.realPoints, kHighlighterShortStrokeLengthPx)); // 末端始终保留 12px 弦方向上下文。
		if (protectedStartIndex <= stroke.committedIndex) return {};

		const size_t stableStartIndex = stroke.highlighter && stroke.committedIndex > 0
			? stroke.committedIndex - 1 : stroke.committedIndex;
		std::vector<InkPoint> stablePoints(stroke.realPoints.begin() + stableStartIndex,
			stroke.realPoints.begin() + protectedStartIndex + 1);
		renderer.SetOperatorTarget(renderer.layerL1);
		RECT dirty = {};
		if (stroke.highlighter)
		{
			const HighlighterBoundaryFlags flags = stableStartIndex == 0
				? HighlighterBoundaryFlags::Start : HighlighterBoundaryFlags::None;
			const HighlighterGeometry geometry = BuildHighlighterGeometry(
				stablePoints, flags, false, stroke.startDirectionState);
			renderer.DrawHighlighterPrimitives(geometry.primitives, color);
			dirty = ClampRectToCanvas(geometry.bounds, width, height);
		}
		else
		{
			renderer.DrawStrokeOrDot(stablePoints, color, shape);
			dirty = RectFromStrokePoints(stablePoints, width, height, shape);
		}
		stroke.committedIndex = protectedStartIndex; // 推进提交游标，后续帧不重复提交稳定前缀。
		stroke.hasCommittedGeometry = true;
		return dirty;
	}

	RECT CommitEraserRealPointsToL1(ActiveStroke& stroke, StrokeShape shape,
		InkRenderer& renderer, int width, int height)
	{
		std::vector<InkPoint> newPoints;
		if (stroke.realPoints.empty())
		{
			if (stroke.hasCommittedGeometry || !stroke.hasInputStartPoint) return {};
			newPoints.push_back(stroke.inputStartPoint); // 建模器尚未给点时也要保证单击橡皮可见。
		}
		else
		{
			const size_t latestIndex = stroke.realPoints.size() - 1;
			if (stroke.hasCommittedGeometry && latestIndex <= stroke.committedIndex) return {};
			const size_t startIndex = stroke.hasCommittedGeometry ? stroke.committedIndex : 0;
			newPoints.assign(stroke.realPoints.begin() + startIndex, stroke.realPoints.end());
			stroke.committedIndex = latestIndex;
		}

		renderer.SetOperatorTarget(renderer.layerL1);
		renderer.DrawStrokeOrDot(newPoints, kTransparentLayerClearColor, shape, InkOperatorKind::Erase);
		stroke.hasCommittedGeometry = true;
		return RectFromStrokePoints(newPoints, width, height, shape);
	}

	void DrawL0LiveComposite(ActiveStroke& stroke, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkRenderer& renderer, bool clearLayer)
	{
		if (clearLayer) renderer.ClearOperatorLayer(renderer.layerL0); // 多 contact 帧由调用方只清一次共享 L0。
		if (stroke.highlighter)
		{
			if (stroke.l0HighlighterGeometry.primitives.empty()) return;
			renderer.SetOperatorTarget(renderer.layerL0);
			renderer.DrawHighlighterPrimitives(stroke.l0HighlighterGeometry.primitives, color);
			return;
		}
		if (stroke.l0DrawPoints.empty()) return;
		renderer.SetOperatorTarget(renderer.layerL0);
		renderer.DrawStrokeOrDot(stroke.l0DrawPoints, color, shape);
	}
}
