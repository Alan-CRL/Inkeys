module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <windows.h>

module draw3.ink_prediction;

namespace draw3
{
	// 本实现单元集中维护笔宽估算、荧光笔几何和 ActiveStroke 热路径。
	namespace
	{
		constexpr float kIdleMoveThresholdPx = 0.25f;
		constexpr float kVisualStablePositionEpsilonPx = 0.05f;
		constexpr float kVisualStableRadiusEpsilonPx = 0.02f;
		constexpr int kVisualStableRequiredFrames = 3;
		constexpr float kMaxDiameterChangePerBaseDiameterPerSecond = 3.0f;
		constexpr float kMaxRadiusChangePerPixel = 0.35f;
		// L0 笔锋只做公切线安全投影，允许比稳定笔宽更快收细，但仍严格小于 1。
		constexpr float kCapsuleRadiusSlope = 0.95f;
		constexpr float kHighlighterDuplicateDistancePx = 0.25f;
		constexpr float kHighlighterRadiusPx = 25.0f;
		constexpr float kHighlighterBoundsPaddingPx = 3.0f;
		static_assert(kMaxRadiusChangePerPixel > 0.0f && kMaxRadiusChangePerPixel < 1.0f,
			"半径空间变化率必须严格小于胶囊切线退化阈值");
		static_assert(kCapsuleRadiusSlope > 0.0f && kCapsuleRadiusSlope < 1.0f,
			"笔锋公切线斜率必须严格小于胶囊切线退化阈值");
		float LerpFloat(float from, float to, float ratio)
		{
			return from + (to - from) * ratio;
		}

		float SmoothStep01(float value)
		{
			value = std::clamp(value, 0.0f, 1.0f);
			return value * value * (3.0f - 2.0f * value); // 比线性插值更平滑，避免笔宽突变。
		}

		LONG SaturatingFloorToLong(double value) noexcept
		{
			value = std::floor(value);
			if (value <= static_cast<double>((std::numeric_limits<LONG>::min)()))
				return (std::numeric_limits<LONG>::min)();
			if (value >= static_cast<double>((std::numeric_limits<LONG>::max)()))
				return (std::numeric_limits<LONG>::max)();
			return static_cast<LONG>(value);
		}

		LONG SaturatingCeilToLong(double value) noexcept
		{
			value = std::ceil(value);
			if (value <= static_cast<double>((std::numeric_limits<LONG>::min)()))
				return (std::numeric_limits<LONG>::min)();
			if (value >= static_cast<double>((std::numeric_limits<LONG>::max)()))
				return (std::numeric_limits<LONG>::max)();
			return static_cast<LONG>(value);
		}

		void IncludeHighlighterBounds(RECT& bounds, float minX, float minY, float maxX, float maxY)
		{
			if (!std::isfinite(minX) || !std::isfinite(minY) ||
				!std::isfinite(maxX) || !std::isfinite(maxY)) return;
			const RECT addition = {
				SaturatingFloorToLong(static_cast<double>(minX) - kHighlighterBoundsPaddingPx),
				SaturatingFloorToLong(static_cast<double>(minY) - kHighlighterBoundsPaddingPx),
				SaturatingCeilToLong(static_cast<double>(maxX) + kHighlighterBoundsPaddingPx),
				SaturatingCeilToLong(static_cast<double>(maxY) + kHighlighterBoundsPaddingPx)
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

		void IncludeHighlighterSweepBounds(RECT& bounds, const InkPoint& p1, const InkPoint& p2)
		{
			const float halfWidth = kHighlighterRadiusPx / kHighlighterNibAspectRatio;
			IncludeHighlighterBounds(bounds,
				std::min(p1.x, p2.x) - halfWidth,
				std::min(p1.y, p2.y) - kHighlighterRadiusPx,
				std::max(p1.x, p2.x) + halfWidth,
				std::max(p1.y, p2.y) + kHighlighterRadiusPx);
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

		float ClampRadiusByDistance(float previousRadius, float desiredRadius, float pointDistance) noexcept
		{
			if (pointDistance <= 0.000001f) return previousRadius; // 零长度段不能形成公切线，保持前一半径。
			const float maxRadiusDelta = kCapsuleRadiusSlope * pointDistance;
			return previousRadius + std::clamp(desiredRadius - previousRadius, -maxRadiusDelta, maxRadiusDelta);
		}

		// 笔锋叠加后只做空间公切线投影，不再套用稳定笔宽的时间限速。
		void EnforceCapsuleTangency(std::vector<InkPoint>& points)
		{
			if (points.size() < 2) return;
			for (size_t index = 1; index < points.size(); ++index)
			{
				const float pointDistance = std::hypot(
					points[index].x - points[index - 1].x, points[index].y - points[index - 1].y);
				points[index].r = ClampRadiusByDistance(
					points[index - 1].r, points[index].r, pointDistance);
			}
			for (size_t index = points.size() - 1; index > 0; --index)
			{
				const float pointDistance = std::hypot(
					points[index].x - points[index - 1].x, points[index].y - points[index - 1].y);
				points[index - 1].r = ClampRadiusByDistance(
					points[index].r, points[index - 1].r, pointDistance);
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

	namespace ink_prediction_detail
	{
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

		size_t FindProtectedStartIndex(
			std::span<const InkPoint> points, double protectedDurationSeconds)
		{
			if (points.size() < 2) return 0;
			const double startTime = static_cast<double>(points.back().time) - std::max(0.0, protectedDurationSeconds);
			size_t startIndex = 0;
			// 多保留一个连接点，避免 L1 与 L0 的交界断开。
			while (startIndex + 1 < points.size() && points[startIndex + 1].time < startTime) ++startIndex;
			return startIndex;
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
				// 起笔先保持基准宽度，再按时间/距离渐进追随，避免第一份 Move 让整段突然变粗。
				hasInputSpeed = true;
			}
			const double deltaTime = std::max(0.0, pointTime - lastTime);
			const float alpha = std::clamp(static_cast<float>(1.0 - std::exp(-deltaTime / 0.060)), 0.02f, 0.35f); // RTS 覆盖采样下放慢追随，避免单批输出放大宽度变化。
			const float desiredDiameter = LerpFloat(currentDiameter, targetDiameter, alpha);
			const float pointDistance = std::hypot(result.position.x - lastPositionX, result.position.y - lastPositionY);
			currentDiameter = 2.0f * ClampRadiusTransition(currentDiameter * 0.5f, desiredDiameter * 0.5f,
				baseDiameter, pointDistance, deltaTime); // 时间和空间双重限速，保持胶囊公切线稳定。
		}
		lastTime = pointTime;
		lastPositionX = result.position.x;
		lastPositionY = result.position.y;
		return { result.position.x, result.position.y, currentDiameter * 0.5f, static_cast<float>(pointTime) };
	}

	InkPoint StrokeWidthEstimator::AppendHardwarePressure(const ink::stroke_model::Result& result)
	{
		const double pointTime = result.time.Value();
		const bool hasPressure = std::isfinite(result.pressure) && result.pressure >= 0.0f;
		const float targetDiameter = hasPressure
			? HardwarePressureDiameter(baseDiameter, result.pressure) : currentDiameter;
		if (!hasSample)
		{
			currentDiameter = hasPressure ? targetDiameter : baseDiameter;
			hasSample = true;
		}
		else if (hasPressure)
		{
			const double deltaTime = std::max(0.0, pointTime - lastTime);
			const float pointDistance = std::hypot(
				result.position.x - lastPositionX, result.position.y - lastPositionY);
			currentDiameter = 2.0f * ClampRadiusTransition(currentDiameter * 0.5f,
				targetDiameter * 0.5f, baseDiameter, pointDistance, deltaTime);
		}
		lastTime = pointTime;
		lastPositionX = result.position.x;
		lastPositionY = result.position.y;
		return { result.position.x, result.position.y, currentDiameter * 0.5f, static_cast<float>(pointTime) };
	}

	InkPoint StrokeWidthEstimator::AppendLaserPressure(const ink::stroke_model::Result& result)
	{
		const double pointTime = result.time.Value();
		const bool hasPressure = std::isfinite(result.pressure) && result.pressure >= 0.0f;
		const float targetDiameter = hasPressure
			? LaserPressureDiameter(baseDiameter, result.pressure) : currentDiameter;
		if (!hasSample)
		{
			currentDiameter = hasPressure ? targetDiameter : baseDiameter;
			hasSample = true;
		}
		else if (hasPressure)
		{
			const double deltaTime = std::max(0.0, pointTime - lastTime);
			const float pointDistance = std::hypot(
				result.position.x - lastPositionX, result.position.y - lastPositionY);
			currentDiameter = 2.0f * ClampRadiusTransition(currentDiameter * 0.5f,
				targetDiameter * 0.5f, baseDiameter, pointDistance, deltaTime);
		}
		lastTime = pointTime;
		lastPositionX = result.position.x;
		lastPositionY = result.position.y;
		return { result.position.x, result.position.y,
			currentDiameter * 0.5f, static_cast<float>(pointTime) };
	}

	void RebuildHighlighterGeometry(
		std::span<const InkPoint> inputPoints, HighlighterGeometry& geometry)
	{
		geometry.primitives.clear();
		geometry.bounds = {};
		if (inputPoints.empty()) return;
		const DirectX::XMFLOAT2 halfSize = {
			kHighlighterRadiusPx / kHighlighterNibAspectRatio, kHighlighterRadiusPx };
		geometry.primitives.reserve(inputPoints.size());
		InkPoint previous = inputPoints.front();
		for (size_t index = 1; index < inputPoints.size(); ++index)
		{
			const InkPoint& current = inputPoints[index];
			const float deltaX = current.x - previous.x;
			const float deltaY = current.y - previous.y;
			if (deltaX * deltaX + deltaY * deltaY <=
				kHighlighterDuplicateDistancePx * kHighlighterDuplicateDistancePx) continue;
			HighlighterPrimitive primitive;
			primitive.p1 = { previous.x, previous.y };
			primitive.p2 = { current.x, current.y };
			primitive.halfSize = halfSize;
			geometry.primitives.push_back(primitive);
			IncludeHighlighterSweepBounds(geometry.bounds, previous, current);
			previous = current;
		}
		if (geometry.primitives.empty())
		{
			HighlighterPrimitive primitive;
			primitive.p1 = { previous.x, previous.y };
			primitive.p2 = primitive.p1;
			primitive.halfSize = halfSize;
			geometry.primitives.push_back(primitive);
			IncludeHighlighterSweepBounds(geometry.bounds, previous, previous);
		}
	}

	HighlighterGeometry BuildHighlighterGeometry(const std::vector<InkPoint>& inputPoints)
	{
		HighlighterGeometry geometry;
		RebuildHighlighterGeometry(inputPoints, geometry);
		return geometry;
	}

	void BuildCompletedPenTail(const ActiveStroke& stroke,
		double liveTipTaperSeconds, std::vector<InkPoint>& output)
	{
		output.clear();
		// 完成态只从真实点生成；prediction 永远不进入持久 Stroke。
		if (!stroke.realPoints.empty())
		{
			const size_t tailStart = stroke.hasCommittedGeometry
				? std::min(stroke.committedIndex, stroke.realPoints.size() - 1) : 0;
			output.assign(stroke.realPoints.begin() + tailStart, stroke.realPoints.end());
			ApplyLiveTipTaper(output, liveTipTaperSeconds);
			EnforceCapsuleTangency(output); // 与 L0 实时笔锋同一套公切线安全，不再套稳定笔宽时间限速。
		}
		if (output.empty() && stroke.hasInputStartPoint)
			output.push_back(stroke.inputStartPoint); // Down 后立即 Up 尚无建模点时仍生成点击。
	}

	std::optional<InkStroke> FinalizeStoredStroke(const ActiveStroke& stroke,
		StoredInkStyle style, double liveTipTaperSeconds,
		std::vector<InkPoint>& scratch)
	{
		std::vector<StoredInkPoint> points;
		auto appendPoint = [&](const InkPoint& point)
		{
			points.push_back({ point.x, point.y, point.r * 2.0f });
		};

		if (style.inkType == StoredInkType::Pen)
		{
			BuildCompletedPenTail(stroke, liveTipTaperSeconds, scratch);
			const size_t stablePointCount = stroke.hasCommittedGeometry &&
				!stroke.realPoints.empty()
				? std::min(stroke.committedIndex + 1, stroke.realPoints.size()) : 0;
			points.reserve((stablePointCount > 0 ? stablePointCount - 1 : 0) +
				scratch.size());
			// 尾段首点已经烘入 taper；用它替换稳定前缀连接点，避免接缝重复。
			for (size_t index = 0; index + 1 < stablePointCount; ++index)
				appendPoint(stroke.realPoints[index]);
			for (const InkPoint& point : scratch) appendPoint(point);
		}
		else if (style.inkType == StoredInkType::Highlighter ||
			style.inkType == StoredInkType::Eraser)
		{
			if (!stroke.realPoints.empty())
			{
				points.reserve(stroke.realPoints.size());
				for (const InkPoint& point : stroke.realPoints) appendPoint(point);
			}
			else if (stroke.hasInputStartPoint)
			{
				points.reserve(1);
				appendPoint(stroke.inputStartPoint);
			}
		}
		else
		{
			return std::nullopt;
		}

		InkStroke storedStroke(style, std::move(points));
		if (!storedStroke.IsValid()) return std::nullopt;
		return storedStroke;
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
		committedHighlighterGeometry.primitives.clear();
		committedHighlighterGeometry.bounds = {};
		convertedResultCount = 0;
		committedIndex = 0;
		lastL0Rect = {};
		currentL0Rect = {};
		widthEstimator = StrokeWidthEstimator(baseDiameter, expectedSpeed);
		widthMode = widthModeValue;
		highlighter = highlighterValue;
		hasCommittedGeometry = false;
		inputStartPoint = {};
		hasInputStartPoint = false;
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

	RECT RectFromStrokePoints(std::span<const InkPoint> points, int width, int height,
		StrokeShape shape)
	{
		if (points.empty()) return {};
		RECT rect = {};
		for (const InkPoint& point : points)
		{
			if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
				!std::isfinite(point.r)) continue;
			const double padding = static_cast<double>(point.r) + 3.0; // 额外 3px 覆盖抗锯齿和胶囊端点。
			UnionRectInPlace(rect, RECT{
				SaturatingFloorToLong(static_cast<double>(point.x) - padding),
				SaturatingFloorToLong(static_cast<double>(point.y) - padding),
				SaturatingCeilToLong(static_cast<double>(point.x) + padding),
				SaturatingCeilToLong(static_cast<double>(point.y) + padding) });
		}
		return ClampRectToCanvas(rect, width, height);
	}

	RECT RectFromLaserPoints(std::span<const InkPoint> points,
		float dpiScale, int width, int height)
	{
		if (points.empty()) return {};
		const float scale = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		const float fallbackSolidRadius = LaserSolidRadius(scale);
		RECT rect = {};
		for (const InkPoint& point : points)
		{
			if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
			const float solidRadius = std::isfinite(point.r) && point.r > 0.0f
				? point.r : fallbackSolidRadius;
			const double padding = static_cast<double>(LaserVisualRadius(solidRadius, scale)) + 3.0;
			if (!std::isfinite(padding)) continue;
			UnionRectInPlace(rect, RECT{
				SaturatingFloorToLong(static_cast<double>(point.x) - padding),
				SaturatingFloorToLong(static_cast<double>(point.y) - padding),
				SaturatingCeilToLong(static_cast<double>(point.x) + padding),
				SaturatingCeilToLong(static_cast<double>(point.y) + padding) });
		}
		return ClampRectToCanvas(rect, width, height);
	}

	LaserLayerDirtyPlan PlanLaserLayerDirty(
		std::span<const InkPoint> realPoints,
		std::span<const InkPoint> visiblePoints,
		const LaserIncrementalStrokeState& state,
		RECT stableBounds, RECT previousLiveBounds,
		double protectedDurationSeconds, float dpiScale,
		int width, int height) noexcept
	{
		LaserLayerDirtyPlan plan;
		plan.ranges = PlanLaserIncrementalRanges(
			realPoints, state, protectedDurationSeconds);
		plan.stableBounds = stableBounds;
		plan.previousLiveBounds = previousLiveBounds;

		if (plan.ranges.stablePointCount > 0 &&
			plan.ranges.stableFirstIndex < realPoints.size())
		{
			const size_t stableCount = std::min(
				plan.ranges.stablePointCount,
				realPoints.size() - plan.ranges.stableFirstIndex);
			const std::span<const InkPoint> stableDelta = realPoints.subspan(
				plan.ranges.stableFirstIndex, stableCount);
			plan.stableDeltaBounds = RectFromLaserPoints(
				stableDelta, dpiScale, width, height);
			UnionRectInPlace(plan.stableBounds, plan.stableDeltaBounds);
		}

		const size_t liveFirstIndex = std::min(
			plan.ranges.liveFirstIndex, visiblePoints.size());
		plan.liveBounds = RectFromLaserPoints(
			visiblePoints.subspan(liveFirstIndex), dpiScale, width, height);
		plan.layerBounds = plan.stableBounds;
		UnionRectInPlace(plan.layerBounds, plan.liveBounds);
		plan.dirtyBounds = plan.previousLiveBounds;
		UnionRectInPlace(plan.dirtyBounds, plan.stableDeltaBounds);
		UnionRectInPlace(plan.dirtyBounds, plan.liveBounds);
		return plan;
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
		for (size_t index = stroke.convertedResultCount; index < stroke.modeledResults.size(); ++index)
		{
			const auto& result = stroke.modeledResults[index];
			InkPoint point;
			switch (stroke.widthMode)
			{
			case StrokeWidthMode::Fixed:
				point = { result.position.x, result.position.y, stroke.widthEstimator.baseDiameter * 0.5f,
					static_cast<float>(result.time.Value()) };
				break;
			case StrokeWidthMode::HardwarePressure:
				point = stroke.widthEstimator.AppendHardwarePressure(result);
				break;
			case StrokeWidthMode::LaserPressure:
				point = stroke.widthEstimator.AppendLaserPressure(result);
				break;
			case StrokeWidthMode::SimulatedPressure:
			default:
				point = stroke.widthEstimator.Append(result, inputSpeed);
				break;
			}
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
				if (pointDistance <= kHighlighterDuplicateDistancePx) continue; // 亚像素抖动累计到 0.25px 后再生成 sweep。
			}
			stroke.realPoints.push_back(point);
		}
		stroke.convertedResultCount = stroke.modeledResults.size(); // 记录已转换位置，下一帧只处理增量。
	}

	void RebuildPredictedPoints(ActiveStroke& stroke)
	{
		stroke.predictedPoints.clear();
		const float predictedRadius = !stroke.realPoints.empty()
			? stroke.realPoints.back().r : stroke.widthEstimator.baseDiameter * 0.5f;
		// 预测器只预测几何位置，笔宽继承最后真实输入，避免预测速度变化造成尾部回粗。
		for (const auto& result : stroke.predictedResults)
		{
			InkPoint point{ result.position.x, result.position.y, predictedRadius,
				static_cast<float>(result.time.Value()) };
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
		stroke.l0HighlighterGeometry.primitives.clear();
		stroke.l0HighlighterGeometry.bounds = {};
		if (!stroke.realPoints.empty())
		{
			const size_t startIndex = std::min(stroke.committedIndex, stroke.realPoints.size() - 1);
			stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.realPoints.begin() + startIndex, stroke.realPoints.end());
		}
		if (stroke.highlighter)
		{
			if (stroke.l0DrawPoints.empty() && stroke.hasInputStartPoint)
				stroke.l0DrawPoints.push_back(stroke.inputStartPoint); // Down 当帧直接显示居中的竖直点击矩形。
			stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(),
				stroke.predictedPoints.begin(), stroke.predictedPoints.end());
			RebuildHighlighterGeometry(stroke.l0DrawPoints, stroke.l0HighlighterGeometry);
			stroke.currentL0Rect = ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, width, height);
			return;
		}
		stroke.l0DrawPoints.insert(stroke.l0DrawPoints.end(), stroke.predictedPoints.begin(), stroke.predictedPoints.end()); // 预测点只放在 L0，便于下一帧擦除重画。
		ApplyLiveTipTaper(stroke.l0DrawPoints, liveTipDurationSeconds);
		EnforceCapsuleTangency(stroke.l0DrawPoints); // 笔锋只做公切线安全投影，不再套用稳定笔宽时间限速。
		stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, width, height, shape);
	}

	RECT CommitStablePrefixToL1(ActiveStroke& stroke, double liveTipDurationSeconds,
		double predictionDurationSeconds, DirectX::XMFLOAT4 color, StrokeShape shape,
		InkRenderer& renderer, int width, int height)
	{
		if (stroke.realPoints.size() < 2) return {};
		size_t protectedStartIndex = ink_prediction_detail::FindProtectedStartIndex(
			stroke.realPoints, liveTipDurationSeconds + predictionDurationSeconds); // 尾部和预测窗口内的点暂不落到 L1。
		if (protectedStartIndex <= stroke.committedIndex) return {};

		const size_t stableStartIndex = stroke.committedIndex;
		const std::span<const InkPoint> stablePoints(
			stroke.realPoints.data() + stableStartIndex,
			protectedStartIndex - stableStartIndex + 1);
		renderer.SetOperatorTarget(renderer.layerL1);
		RECT dirty = {};
		if (stroke.highlighter)
		{
			RebuildHighlighterGeometry(stablePoints, stroke.l0HighlighterGeometry);
			renderer.DrawHighlighterPrimitives(
				stroke.l0HighlighterGeometry.primitives, color);
			if (!stroke.l0HighlighterGeometry.primitives.empty())
			{
				// 只缓存已经提交到 L1 的稳定前缀，Up 时直接重放。
				stroke.committedHighlighterGeometry.primitives.insert(
					stroke.committedHighlighterGeometry.primitives.end(),
					stroke.l0HighlighterGeometry.primitives.begin(),
					stroke.l0HighlighterGeometry.primitives.end());
				if (stroke.committedHighlighterGeometry.primitives.size() ==
					stroke.l0HighlighterGeometry.primitives.size())
					stroke.committedHighlighterGeometry.bounds =
						stroke.l0HighlighterGeometry.bounds;
				else
					UnionRectInPlace(stroke.committedHighlighterGeometry.bounds,
						stroke.l0HighlighterGeometry.bounds);
			}
			dirty = ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, width, height);
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
		std::array<InkPoint, 1> fallbackPoint = {};
		std::span<const InkPoint> newPoints;
		if (stroke.realPoints.empty())
		{
			if (stroke.hasCommittedGeometry || !stroke.hasInputStartPoint) return {};
			fallbackPoint[0] = stroke.inputStartPoint;
			newPoints = fallbackPoint; // 建模器尚未给点时也要保证单击橡皮可见。
		}
		else
		{
			const size_t latestIndex = stroke.realPoints.size() - 1;
			if (stroke.hasCommittedGeometry && latestIndex <= stroke.committedIndex) return {};
			const size_t startIndex = stroke.hasCommittedGeometry ? stroke.committedIndex : 0;
			newPoints = std::span<const InkPoint>(stroke.realPoints).subspan(startIndex);
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
