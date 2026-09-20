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

#include "Draw3.SpeedEraser.h"

module Inkeys.Drawing.Draw3.ink_prediction;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元集中维护笔宽估算、荧光笔几何和 ActiveStroke 热路径。
	namespace
	{
		constexpr float kIdleMoveThresholdPx = 0.25f;
		constexpr float kVisualStablePositionEpsilonPx = 0.05f;
		constexpr float kEndpointDirectionEpsilonPx = 0.000001f;
		constexpr float kVisualStableRadiusEpsilonPx = 0.02f;
		constexpr int kVisualStableRequiredFrames = 3;
		constexpr float kMaxDiameterChangePerBaseDiameterPerSecond = 3.0f;
		constexpr float kMaxRadiusChangePerPixel = 0.35f;
		// L0 笔锋只做公切线安全投影，允许比稳定笔宽更快收细，但仍严格小于 1。
		constexpr float kCapsuleRadiusSlope = 0.95f;
		constexpr float kHighlighterDuplicateDistancePx = 0.25f;
		constexpr float kHighlighterBoundsPaddingPx = 3.0f;
		constexpr float kShapeBoundsPaddingPx = 3.0f;
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

		void IncludeHighlighterSweepBounds(RECT& bounds, const InkPoint& p1, const InkPoint& p2,
			DirectX::XMFLOAT2 halfSize)
		{
			IncludeHighlighterBounds(bounds,
				std::min(p1.x, p2.x) - halfSize.x,
				std::min(p1.y, p2.y) - halfSize.y,
				std::max(p1.x, p2.x) + halfSize.x,
				std::max(p1.y, p2.y) + halfSize.y);
		}

		std::optional<ShapePrimitiveKind> ShapeKindForStoredType(
			StoredInkType type) noexcept
		{
			switch (type)
			{
			case StoredInkType::SolidLine: return ShapePrimitiveKind::SolidLine;
			case StoredInkType::DashedLine: return ShapePrimitiveKind::DashedLine;
			case StoredInkType::OutlineRectangle: return ShapePrimitiveKind::OutlineRectangle;
			case StoredInkType::FilledRectangle: return ShapePrimitiveKind::FilledRectangle;
			default: return std::nullopt;
			}
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

		void ApplyLiveTipTaper(std::vector<InkPoint>& points,
			double liveTipDurationSeconds, double displayTime)
		{
			if (points.empty() || liveTipDurationSeconds <= 0.0) return;
			const double endTime = points.back().time;
			const double tipStartTime = endTime - liveTipDurationSeconds;
			size_t firstTipIndex = points.size() - 1;
			while (firstTipIndex > 0 && static_cast<double>(points[firstTipIndex - 1].time) >= tipStartTime) --firstTipIndex; // 找到需要渐细的实时尾部起点。

			const double actualTipSpan = std::max(0.0, endTime - static_cast<double>(points[firstTipIndex].time));
			const float spanRatio = SmoothStep01(static_cast<float>(actualTipSpan / liveTipDurationSeconds));
			const double now = std::max(displayTime, endTime);
			const float newestScale = LerpFloat(1.0f, 0.28f, spanRatio); // 尾部越完整，最新端点越细。
			for (size_t index = firstTipIndex; index < points.size(); ++index)
			{
				const float ageRatio = actualTipSpan > 0.000001
					? static_cast<float>((now - static_cast<double>(points[index].time)) / actualTipSpan)
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
		const auto ResolveHalfSize = [](const InkPoint& point) noexcept
		{
			const float halfHeight = std::max(0.5f, point.r);
			return DirectX::XMFLOAT2{
				halfHeight / kHighlighterNibAspectRatio, halfHeight };
		};
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
			const DirectX::XMFLOAT2 previousHalfSize = ResolveHalfSize(previous);
			const DirectX::XMFLOAT2 currentHalfSize = ResolveHalfSize(current);
			primitive.halfSize = {
				(previousHalfSize.x + currentHalfSize.x) * 0.5f,
				(previousHalfSize.y + currentHalfSize.y) * 0.5f };
			geometry.primitives.push_back(primitive);
			IncludeHighlighterSweepBounds(geometry.bounds, previous, current,
				primitive.halfSize);
			previous = current;
		}
		if (geometry.primitives.empty())
		{
			HighlighterPrimitive primitive;
			primitive.p1 = { previous.x, previous.y };
			primitive.p2 = primitive.p1;
			primitive.halfSize = ResolveHalfSize(previous);
			geometry.primitives.push_back(primitive);
			IncludeHighlighterSweepBounds(geometry.bounds, previous, previous,
				primitive.halfSize);
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
			ApplyLiveTipTaper(output, liveTipTaperSeconds, stroke.logicalInputTime);
			EnforceCapsuleTangency(output); // 与 L0 实时笔锋同一套公切线安全，不再套稳定笔宽时间限速。
		}
		if (output.empty() && stroke.hasInputStartPoint)
			output.push_back(stroke.inputStartPoint); // Down 后立即 Up 尚无建模点时仍生成点击。
	}

	std::optional<InkStroke> FinalizeStoredStroke(const ActiveStroke& stroke,
		StoredInkStyle style, double liveTipTaperSeconds,
		std::vector<InkPoint>& scratch, float canvasOffsetX, float canvasOffsetY)
	{
		if (!std::isfinite(canvasOffsetX) || !std::isfinite(canvasOffsetY))
			return std::nullopt;
		std::vector<StoredInkPoint> points;
		auto appendPoint = [&](const InkPoint& point)
		{
			points.push_back({ point.x + canvasOffsetX,
				point.y + canvasOffsetY, point.r * 2.0f });
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

	std::optional<InkStroke> FinalizeStoredShape(
		const ShapePrimitive& primitive, StoredInkStyle style,
		float canvasOffsetX, float canvasOffsetY)
	{
		if (!IsStoredShapeType(style.inkType) ||
			!std::isfinite(canvasOffsetX) || !std::isfinite(canvasOffsetY) ||
			!std::isfinite(primitive.start.x) || !std::isfinite(primitive.start.y) ||
			!std::isfinite(primitive.end.x) || !std::isfinite(primitive.end.y) ||
			!std::isfinite(primitive.start.r) || primitive.start.r <= 0.0f)
			return std::nullopt;
		const float width = primitive.start.r * 2.0f;
		InkStroke storedStroke(style, {
			{ primitive.start.x + canvasOffsetX,
				primitive.start.y + canvasOffsetY, width },
			{ primitive.end.x + canvasOffsetX,
				primitive.end.y + canvasOffsetY, width }
		});
		if (!storedStroke.IsValid()) return std::nullopt;
		return storedStroke;
	}

	DirectX::XMFLOAT2 ResolveShapeLiveEndpoint(
		std::span<const ink::stroke_model::Result> predictedResults,
		DirectX::XMFLOAT2 modeledEndpoint, bool hasModeledEndpoint,
		DirectX::XMFLOAT2 rawEndpoint, bool forceRawEndpoint) noexcept
	{
		if (forceRawEndpoint) return rawEndpoint;
		if (!predictedResults.empty())
		{
			const auto& predicted = predictedResults.back().position;
			if (std::isfinite(predicted.x) && std::isfinite(predicted.y))
				return { predicted.x, predicted.y };
		}
		if (hasModeledEndpoint && std::isfinite(modeledEndpoint.x) &&
			std::isfinite(modeledEndpoint.y)) return modeledEndpoint;
		return rawEndpoint;
	}

	std::vector<StoredStrokePointRange> PlanStoredStrokeRasterRanges(
		const InkStroke& stroke, const StoredStrokeRasterTarget& target)
	{
		std::vector<StoredStrokePointRange> ranges;
		if (!stroke.IsValid() || IsStoredShapeType(stroke.Style().inkType) ||
			target.width <= 0 || target.height <= 0 ||
			!std::isfinite(target.originX) || !std::isfinite(target.originY)) return ranges;
		const std::span<const StoredInkPoint> points = stroke.Points();
		if (points.empty()) return ranges;
		const double targetLeft = target.originX;
		const double targetTop = target.originY;
		const double targetRight = targetLeft + target.width;
		const double targetBottom = targetTop + target.height;
		const bool highlighter = stroke.Style().inkType == StoredInkType::Highlighter;
		const auto pointPadding = [&](const StoredInkPoint& point) noexcept
		{
			return highlighter
				? std::pair<double, double>{
					static_cast<double>(point.width) * 0.5 /
						kHighlighterNibAspectRatio + kHighlighterBoundsPaddingPx,
					static_cast<double>(point.width) * 0.5 +
						kHighlighterBoundsPaddingPx }
				: std::pair<double, double>{
					static_cast<double>(point.width) * 0.5 + 3.0,
					static_cast<double>(point.width) * 0.5 + 3.0 };
		};
		const auto pointTouches = [&](const StoredInkPoint& point) noexcept
		{
			const auto [paddingX, paddingY] = pointPadding(point);
			return static_cast<double>(point.x) - paddingX < targetRight &&
				static_cast<double>(point.x) + paddingX > targetLeft &&
				static_cast<double>(point.y) - paddingY < targetBottom &&
				static_cast<double>(point.y) + paddingY > targetTop;
		};
		if (points.size() == 1)
		{
			if (pointTouches(points.front())) ranges.push_back({ 0, 1 });
			return ranges;
		}
		for (size_t index = 1; index < points.size(); ++index)
		{
			const StoredInkPoint& first = points[index - 1];
			const StoredInkPoint& second = points[index];
			const auto [firstPaddingX, firstPaddingY] = pointPadding(first);
			const auto [secondPaddingX, secondPaddingY] = pointPadding(second);
			const double left = (std::min)(
				static_cast<double>(first.x) - firstPaddingX,
				static_cast<double>(second.x) - secondPaddingX);
			const double top = (std::min)(
				static_cast<double>(first.y) - firstPaddingY,
				static_cast<double>(second.y) - secondPaddingY);
			const double right = (std::max)(
				static_cast<double>(first.x) + firstPaddingX,
				static_cast<double>(second.x) + secondPaddingX);
			const double bottom = (std::max)(
				static_cast<double>(first.y) + firstPaddingY,
				static_cast<double>(second.y) + secondPaddingY);
			if (!(left < targetRight && right > targetLeft &&
				top < targetBottom && bottom > targetTop)) continue;
			const StoredStrokePointRange next{ index - 1, index + 1 };
			if (!ranges.empty() && ranges.back().end >= next.begin)
				ranges.back().end = next.end;
			else ranges.push_back(next);
		}
		return ranges;
	}

	StoredStrokeRasterResult DrawStoredStroke(const InkStroke& stroke, InkRenderer& renderer,
		const StoredStrokeRasterTarget& target, std::vector<InkPoint>& pointScratch,
		HighlighterGeometry& highlighterScratch)
	{
		if (!target.operatorLayer || !target.operatorLayer->addRTV ||
			!target.operatorLayer->retainRTV || target.width <= 0 || target.height <= 0 ||
			!std::isfinite(target.originX) || !std::isfinite(target.originY)) return {};

		if (!stroke.IsValid()) return {};
		const StoredInkStyle& style = stroke.Style();
		constexpr float kByteToFloat = 1.0f / 255.0f;
		const DirectX::XMFLOAT4 color = {
			static_cast<float>((style.fallbackRgb >> 16) & 0xFFu) * kByteToFloat,
			static_cast<float>((style.fallbackRgb >> 8) & 0xFFu) * kByteToFloat,
			static_cast<float>(style.fallbackRgb & 0xFFu) * kByteToFloat,
			style.opacity
		};

		// tile 目标只改变坐标原点和 viewport，笔刷颜色/几何仍走正式 renderer。
		renderer.SetScreenSize(static_cast<float>(target.width),
			static_cast<float>(target.height));
		renderer.SetOperatorTarget(*target.operatorLayer);
		if (const std::optional<ShapePrimitiveKind> shapeKind =
			ShapeKindForStoredType(style.inkType))
		{
			const std::span<const StoredInkPoint> points = stroke.Points();
			ShapePrimitive primitive;
			primitive.start = { points[0].x - target.originX,
				points[0].y - target.originY, points[0].width * 0.5f, 0.0f };
			primitive.end = { points[1].x - target.originX,
				points[1].y - target.originY, 0.0f, 0.0f };
			if (renderer.DrawShapePrimitives(
				std::span<const ShapePrimitive>(&primitive, 1), *shapeKind, color) < 0) return {};
			return { true, RectFromShapePrimitive(
				primitive, *shapeKind, target.width, target.height) };
		}

		const std::vector<StoredStrokePointRange> ranges =
			PlanStoredStrokeRasterRanges(stroke, target);
		RECT dirty = {};
		for (StoredStrokePointRange range : ranges)
		{
			pointScratch.clear();
			pointScratch.reserve(range.end - range.begin);
			for (const StoredInkPoint& point : stroke.Points().subspan(
				range.begin, range.end - range.begin))
			{
				pointScratch.push_back({ point.x - target.originX,
					point.y - target.originY, point.width * 0.5f, 0.0f });
			}
			if (style.inkType == StoredInkType::Highlighter)
			{
				RebuildHighlighterGeometry(pointScratch, highlighterScratch);
				if (renderer.DrawHighlighterPrimitives(
					highlighterScratch.primitives, color) < 0) return {};
				UnionRectInPlace(dirty, ClampRectToCanvas(
					highlighterScratch.bounds, target.width, target.height));
				continue;
			}

			const InkOperatorKind operatorKind = style.inkType == StoredInkType::Eraser
				? InkOperatorKind::Erase : InkOperatorKind::Draw;
			if (renderer.DrawStrokeOrDot(pointScratch, color,
				StrokeShape::RoundCapsule, operatorKind) < 0) return {};
			UnionRectInPlace(dirty, RectFromStrokePoints(
				pointScratch, target.width, target.height));
		}
		return { true, dirty };
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
		modelScratch.clear();
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
		modelTimeOffset = 0.0;
		modelClockStopped = false;
		useDisplayTime = false;
		latestModeledResult = {};
		endpointAdmission = {};
		hasLatestModeledResult = false;
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

	RECT RectFromShapePrimitive(const ShapePrimitive& primitive,
		ShapePrimitiveKind kind, int width, int height)
	{
		if ((!IsLineShapePrimitive(kind) && !IsRectangleShapePrimitive(kind)) ||
			!std::isfinite(primitive.start.x) || !std::isfinite(primitive.start.y) ||
			!std::isfinite(primitive.end.x) || !std::isfinite(primitive.end.y) ||
			!std::isfinite(primitive.start.r)) return {};
		const double linePadding = static_cast<double>(
			std::max(primitive.start.r, 0.0f)) + kShapeBoundsPaddingPx;
		const double padding = kind == ShapePrimitiveKind::FilledRectangle
			? kShapeBoundsPaddingPx : linePadding;
		const double left = static_cast<double>(
			std::min(primitive.start.x, primitive.end.x)) - padding;
		const double top = static_cast<double>(
			std::min(primitive.start.y, primitive.end.y)) - padding;
		const double right = static_cast<double>(
			std::max(primitive.start.x, primitive.end.x)) + padding;
		const double bottom = static_cast<double>(
			std::max(primitive.start.y, primitive.end.y)) + padding;
		return ClampRectToCanvas({
			SaturatingFloorToLong(left), SaturatingFloorToLong(top),
			SaturatingCeilToLong(right), SaturatingCeilToLong(bottom) }, width, height);
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

	bool IsModeledTipSettled(
		std::span<const ink::stroke_model::Result> modeledResults,
		DirectX::XMFLOAT2 rawEndpoint, double frameIntervalSeconds) noexcept
	{
		if (modeledResults.empty() || !std::isfinite(rawEndpoint.x) ||
			!std::isfinite(rawEndpoint.y) || !std::isfinite(frameIntervalSeconds) ||
			frameIntervalSeconds <= 0.0) return false;
		const ink::stroke_model::Result& tip = modeledResults.back();
		if (!std::isfinite(tip.position.x) || !std::isfinite(tip.position.y) ||
			!std::isfinite(tip.velocity.x) || !std::isfinite(tip.velocity.y)) return false;
		const double endpointError = std::hypot(
			static_cast<double>(tip.position.x) - rawEndpoint.x,
			static_cast<double>(tip.position.y) - rawEndpoint.y);
		const double nextFrameTravel = std::hypot(
			static_cast<double>(tip.velocity.x),
			static_cast<double>(tip.velocity.y)) * frameIntervalSeconds;
		return std::isfinite(endpointError) && std::isfinite(nextFrameTravel) &&
			endpointError <= kVisualStablePositionEpsilonPx &&
			nextFrameTravel <= kVisualStablePositionEpsilonPx;
	}

	bool ShouldStartEndpointSettling(
		double sampleAgeSeconds, double frameIntervalSeconds) noexcept
	{
		return std::isfinite(sampleAgeSeconds) &&
			std::isfinite(frameIntervalSeconds) && frameIntervalSeconds > 0.0 &&
			sampleAgeSeconds > frameIntervalSeconds;
	}

	void CaptureLatestModeledResult(ActiveStroke& stroke,
		std::span<const ink::stroke_model::Result> modeledResults) noexcept
	{
		if (modeledResults.empty()) return;
		stroke.latestModeledResult = modeledResults.back();
		stroke.hasLatestModeledResult = true;
	}

	std::span<const ink::stroke_model::Result> LatestModeledTip(
		const ActiveStroke& stroke) noexcept
	{
		return stroke.hasLatestModeledResult
			? std::span<const ink::stroke_model::Result>(
				&stroke.latestModeledResult, 1)
			: std::span<const ink::stroke_model::Result>{};
	}

	void UpdateIdleFreezeState(ActiveStroke& stroke, bool rawMoved,
		bool modelSettled, double liveTipDurationSeconds)
	{
		if (rawMoved)
		{
			stroke.visualStableFrameCount = 0;
			stroke.previousL0DrawPoints = stroke.l0DrawPoints;
			return;
		}
		const bool stoppedLongEnough = stroke.logicalInputTime - stroke.lastMovementInputTime >= liveTipDurationSeconds;
		const bool endpointReady = !stroke.endpointAdmission.active ||
			(stroke.endpointAdmission.visualPinned && !stroke.endpointAdmission.recovering);
		if (stoppedLongEnough && modelSettled && endpointReady &&
			AreL0VisualsClose(stroke.l0DrawPoints, stroke.previousL0DrawPoints))
			++stroke.visualStableFrameCount; // 连续多帧几乎不变才认为视觉已经稳定。
		else
			stroke.visualStableFrameCount = 0;
		stroke.previousL0DrawPoints = stroke.l0DrawPoints;
		if (stroke.visualStableFrameCount >= kVisualStableRequiredFrames) stroke.idleFrozen = true; // 冻结后不再持续喂入相同坐标。
	}

	float InterpolateSpeedEraserDiameter(
		const SpeedEraserWidthInterval& interval, double pointTimeSeconds) noexcept
	{
		return SpeedEraser::InterpolateDiameter(interval, pointTimeSeconds);
	}


	bool AppendEraserSizeAnchor(ActiveStroke& stroke, const SpeedEraserWidthInterval& interval)
	{
		if(!interval.reanchor || stroke.realPoints.empty())return false;
		InkPoint anchor=stroke.realPoints.back();
		anchor.r=interval.startDiameter*0.5f;
		if(std::abs(anchor.r-stroke.realPoints.back().r)<=0.001f)return false;
		stroke.realPoints.push_back(anchor); // 保留旧大圆，新增同位小圆后再连接恢复段。
		return true;
	}

	namespace
	{
		InkPoint ConvertModeledResultToInkPoint(ActiveStroke& stroke,
			const ink::stroke_model::Result& result, float inputSpeed,
			const SpeedEraserWidthInterval* speedEraserWidth)
		{
			switch (stroke.widthMode)
			{
			case StrokeWidthMode::Fixed:
				return { result.position.x, result.position.y,
					stroke.widthEstimator.baseDiameter * 0.5f,
					static_cast<float>(result.time.Value()) };
			case StrokeWidthMode::HardwarePressure:
				return stroke.widthEstimator.AppendHardwarePressure(result);
			case StrokeWidthMode::LaserPressure:
				return stroke.widthEstimator.AppendLaserPressure(result);
			case StrokeWidthMode::SpeedEraser:
			{
				const float diameter = speedEraserWidth
					? InterpolateSpeedEraserDiameter(
						*speedEraserWidth, result.time.Value())
					: stroke.widthEstimator.baseDiameter;
				return { result.position.x, result.position.y, diameter * 0.5f,
					static_cast<float>(result.time.Value()) };
			}
			case StrokeWidthMode::SimulatedPressure:
			default:
				return stroke.widthEstimator.Append(result, inputSpeed);
			}
		}

		void PinEndpointGeometry(ActiveStroke& stroke, double endpointTime,
			EndpointAdmissionResult& result)
		{
			EndpointAdmissionState& admission = stroke.endpointAdmission;
			if (admission.visualPinned) return;
			const float radius = !stroke.realPoints.empty()
				? stroke.realPoints.back().r
				: stroke.hasInputStartPoint
					? stroke.inputStartPoint.r
					: stroke.widthEstimator.baseDiameter * 0.5f;
			const float previousPointTime = !stroke.realPoints.empty()
				? stroke.realPoints.back().time : 0.0f;
			const float pointTime = std::isfinite(endpointTime)
				? std::max(static_cast<float>(endpointTime), previousPointTime)
				: previousPointTime;
			const InkPoint endpointPoint{
				admission.endpoint.x, admission.endpoint.y, radius, pointTime };
			const float tailDistance = stroke.realPoints.empty()
				? (std::numeric_limits<float>::infinity)()
				: std::hypot(stroke.realPoints.back().x - admission.endpoint.x,
					stroke.realPoints.back().y - admission.endpoint.y);
			const bool tailMayChange = !stroke.hasCommittedGeometry ||
				stroke.committedIndex + 1 < stroke.realPoints.size();
			const bool canReplaceTail = !stroke.realPoints.empty() &&
				tailDistance <= kVisualStablePositionEpsilonPx && tailMayChange;
			if (canReplaceTail)
				stroke.realPoints.back() = endpointPoint;
			else if (tailDistance > kEndpointDirectionEpsilonPx)
				stroke.realPoints.push_back(endpointPoint);
			// 已提交尾点若已精确命中 raw endpoint，不再追加第二个同位中心点。
			stroke.widthEstimator.currentDiameter = radius * 2.0f;
			stroke.widthEstimator.lastTime = stroke.hasLatestModeledResult
				? stroke.latestModeledResult.time.Value() : pointTime - stroke.modelTimeOffset;
			stroke.widthEstimator.lastPositionX = admission.endpoint.x;
			stroke.widthEstimator.lastPositionY = admission.endpoint.y;
			stroke.widthEstimator.hasSample = true;
			admission.previousDistance = 0.0f;
			admission.visualPinned = true;
			stroke.predictedResults.clear();
			stroke.predictedPoints.clear();
			result.endpointPinned = true;
			result.geometryChanged = canReplaceTail ||
				tailDistance > kEndpointDirectionEpsilonPx;
		}
	}

	void BeginEndpointAdmission(ActiveStroke& stroke,
		DirectX::XMFLOAT2 endpoint) noexcept
	{
		stroke.predictedResults.clear();
		stroke.predictedPoints.clear();
		if (!std::isfinite(endpoint.x) || !std::isfinite(endpoint.y))
		{
			stroke.endpointAdmission = {};
			return;
		}
		if (stroke.endpointAdmission.active && !stroke.endpointAdmission.recovering &&
			std::hypot(stroke.endpointAdmission.endpoint.x - endpoint.x,
				stroke.endpointAdmission.endpoint.y - endpoint.y) <=
				kEndpointDirectionEpsilonPx)
			return; // 同一停笔/Up 终点重复进入时保留 pinned，避免补出重复中心点。
		stroke.endpointAdmission = {};
		EndpointAdmissionState& admission = stroke.endpointAdmission;
		admission.endpoint = endpoint;
		admission.active = true;
		DirectX::XMFLOAT2 visibleStart = endpoint;
		if (!stroke.realPoints.empty())
			visibleStart = { stroke.realPoints.back().x, stroke.realPoints.back().y };
		else if (stroke.hasInputStartPoint)
			visibleStart = { stroke.inputStartPoint.x, stroke.inputStartPoint.y };
		const float axisX = endpoint.x - visibleStart.x;
		const float axisY = endpoint.y - visibleStart.y;
		admission.previousDistance = std::hypot(axisX, axisY);
		if (admission.previousDistance > kEndpointDirectionEpsilonPx)
		{
			admission.approachDirection = {
				axisX / admission.previousDistance, axisY / admission.previousDistance };
			admission.hasApproachDirection = true;
			return;
		}
		for (size_t index = stroke.realPoints.size(); index > 1; --index)
		{
			const InkPoint& current = stroke.realPoints[index - 1];
			const InkPoint& previous = stroke.realPoints[index - 2];
			const float directionX = current.x - previous.x;
			const float directionY = current.y - previous.y;
			const float directionLength = std::hypot(directionX, directionY);
			if (directionLength <= kEndpointDirectionEpsilonPx) continue;
			admission.approachDirection = {
				directionX / directionLength, directionY / directionLength };
			admission.hasApproachDirection = true;
			break;
		}
	}

	EndpointAdmissionResult AppendEndpointBoundedModeledPoints(
		ActiveStroke& stroke,
		std::span<const ink::stroke_model::Result> modeledResults,
		float inputSpeed, double endpointTime, bool pinEndpointAtEnd)
	{
		EndpointAdmissionResult admissionResult;
		CaptureLatestModeledResult(stroke, modeledResults);
		EndpointAdmissionState& admission = stroke.endpointAdmission;
		if (!admission.active) return admissionResult;
		if (admission.visualPinned)
		{
			admissionResult.endpointPinned = true;
			return admissionResult;
		}

		bool shouldPinEndpoint =
			admission.previousDistance <= kVisualStablePositionEpsilonPx;
		for (const ink::stroke_model::Result& modeledResult : modeledResults)
		{
			if (shouldPinEndpoint) break;
			if (!std::isfinite(modeledResult.position.x) ||
				!std::isfinite(modeledResult.position.y))
			{
				shouldPinEndpoint = true;
				break;
			}
			const float endpointDeltaX = modeledResult.position.x - admission.endpoint.x;
			const float endpointDeltaY = modeledResult.position.y - admission.endpoint.y;
			const float endpointDistance = std::hypot(endpointDeltaX, endpointDeltaY);
			const float forwardProjection = admission.hasApproachDirection
				? endpointDeltaX * admission.approachDirection.x +
					endpointDeltaY * admission.approachDirection.y
				: 0.0f;
			if (!std::isfinite(endpointDistance) ||
				endpointDistance <= kVisualStablePositionEpsilonPx ||
				(admission.hasApproachDirection &&
					forwardProjection > kVisualStablePositionEpsilonPx) ||
				endpointDistance + kEndpointDirectionEpsilonPx >=
					admission.previousDistance)
			{
				shouldPinEndpoint = true;
				break;
			}
			admission.previousDistance = endpointDistance;
			++admissionResult.acceptedResultCount;
		}

		for (size_t index = 0; index < admissionResult.acceptedResultCount; ++index)
		{
			InkPoint point = ConvertModeledResultToInkPoint(
				stroke, modeledResults[index], inputSpeed, nullptr);
			if (stroke.useDisplayTime)
				point.time = static_cast<float>(std::min(
					modeledResults[index].time.Value() + stroke.modelTimeOffset, endpointTime));
			stroke.realPoints.push_back(point);
		}
		admissionResult.geometryChanged = admissionResult.acceptedResultCount > 0;
		if (shouldPinEndpoint || pinEndpointAtEnd)
			PinEndpointGeometry(stroke, endpointTime, admissionResult);
		return admissionResult;
	}

	void ClearEndpointAdmission(ActiveStroke& stroke) noexcept
	{
		stroke.endpointAdmission = {};
	}

	double ResolvePenModelInputTime(ActiveStroke& stroke, double realTime,
		double lastModelTime, double frameIntervalSeconds) noexcept
	{
		if (stroke.modelClockStopped)
		{
			stroke.modelTimeOffset = std::max(stroke.modelTimeOffset,
				realTime - lastModelTime - frameIntervalSeconds);
			stroke.modelClockStopped = false;
		}
		return std::max(realTime - stroke.modelTimeOffset, lastModelTime + 0.000001);
	}

	void AppendRecoveryModeledPoints(ActiveStroke& stroke,
		std::span<const ink::stroke_model::Result> results,
		DirectX::XMFLOAT2 rawEndpoint, float inputSpeed)
	{
		CaptureLatestModeledResult(stroke, results);
		if (stroke.realPoints.empty()) return;
		auto& gate = stroke.endpointAdmission;
		if (!gate.recovering)
		{
			gate.recovering = true;
			gate.recoveryOrigin = stroke.realPoints.empty() ? gate.endpoint :
				DirectX::XMFLOAT2{ stroke.realPoints.back().x, stroke.realPoints.back().y };
		}
		const float dx = rawEndpoint.x - gate.recoveryOrigin.x;
		const float dy = rawEndpoint.y - gate.recoveryOrigin.y;
		const float length = std::hypot(dx, dy);
		if (length <= kEndpointDirectionEpsilonPx) return;
		const float ux = dx / length, uy = dy / length;
		bool accepted = false;
		bool safeTail = false;
		for (const auto& result : results)
		{
			const float x = result.position.x - gate.recoveryOrigin.x;
			const float y = result.position.y - gate.recoveryOrigin.y;
			const float along = x * ux + y * uy;
			const float distance = std::hypot(x, y);
			const auto& previous = stroke.realPoints.back();
			const float step = (result.position.x - previous.x) * ux +
				(result.position.y - previous.y) * uy;
			safeTail = std::isfinite(along) && std::isfinite(distance) &&
				along > 0.0f && along <= length + kVisualStablePositionEpsilonPx &&
				distance <= length + kVisualStablePositionEpsilonPx &&
				step > kEndpointDirectionEpsilonPx &&
				result.velocity.x * ux + result.velocity.y * uy >= 0.0f;
			if (!safeTail) continue; // 旧惯性前缀可以丢弃；安全后缀仍能解除门禁。
			InkPoint point = ConvertModeledResultToInkPoint(stroke, result, inputSpeed, nullptr);
			if (stroke.useDisplayTime)
				point.time = static_cast<float>(std::min(result.time.Value() +
					stroke.modelTimeOffset, stroke.lastMovementInputTime));
			stroke.realPoints.push_back(point);
			accepted = true;
		}
		if (accepted && safeTail) ClearEndpointAdmission(stroke);
	}

	void AppendNewModeledPoints(ActiveStroke& stroke, float inputSpeed,
		const SpeedEraserWidthInterval* speedEraserWidth)
	{
		CaptureLatestModeledResult(stroke, stroke.modeledResults);
		if(stroke.widthMode==StrokeWidthMode::SpeedEraser && speedEraserWidth &&
			stroke.convertedResultCount<stroke.modeledResults.size())
			AppendEraserSizeAnchor(stroke,*speedEraserWidth);
		for (size_t index = stroke.convertedResultCount; index < stroke.modeledResults.size(); ++index)
		{
			const auto& result = stroke.modeledResults[index];
			InkPoint point = ConvertModeledResultToInkPoint(
				stroke, result, inputSpeed, speedEraserWidth);
			if (stroke.useDisplayTime)
				point.time = static_cast<float>(std::min(result.time.Value() +
					stroke.modelTimeOffset, stroke.lastMovementInputTime));
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
				static_cast<float>(result.time.Value() + stroke.modelTimeOffset) };
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
		ApplyLiveTipTaper(stroke.l0DrawPoints, liveTipDurationSeconds, stroke.logicalInputTime);
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
