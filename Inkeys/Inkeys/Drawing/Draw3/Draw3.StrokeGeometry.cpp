module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <ink_stroke_modeler/terminal_tail_reconstructor.h>
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

		double ApplyLiveTipTaper(std::vector<InkPoint>& points,
			double liveTipDurationSeconds, const ActiveStroke& stroke,
			bool includePrediction)
		{
			if (points.empty() || liveTipDurationSeconds <= 0.0) return 0.0;
			const double endTime = points.back().time;
			const double tipStartTime = endTime - liveTipDurationSeconds;
			size_t firstTipIndex = points.size() - 1;
			while (firstTipIndex > 0 && static_cast<double>(points[firstTipIndex - 1].time) >= tipStartTime) --firstTipIndex; // 沿用包含预测参考的原起点，不能扩展到整个 L0。

			double sourceLength = 0.0;
			float referenceRadius = points[firstTipIndex].r;
			for (size_t index = firstTipIndex + 1; index < points.size(); ++index)
			{
				sourceLength += std::hypot(static_cast<double>(points[index].x) - points[index - 1].x,
					static_cast<double>(points[index].y) - points[index - 1].y);
				referenceRadius = std::max(referenceRadius, points[index].r);
			}
			if (sourceLength <= 0.0 || referenceRadius <= 0.0) return 0.0;

			const double displayTime = ResolvePenDisplayTime(stroke);
			// 运动预测保留原未来参考；追赶和 Up 的合成点不能重置真实运动年龄。
			const double referenceTime = includePrediction && !stroke.predictedPoints.empty()
				? std::max(stroke.lastMovementInputTime, static_cast<double>(stroke.predictedPoints.back().time))
				: stroke.lastMovementInputTime;
			const double age = std::max(0.0, displayTime - referenceTime);
			double effectiveLength = sourceLength * (1.0 - SmoothStep01(
				static_cast<float>(age / liveTipDurationSeconds)));
			if (age > 0.0 && stroke.liveTipLength &&
				stroke.liveTipMovementTime == stroke.lastMovementInputTime &&
				stroke.liveTipDisplayTime <= displayTime)
				effectiveLength = std::min(effectiveLength, *stroke.liveTipLength);
			// 延迟处理的物理 Up 不能使用其后显示帧的缓存；新真实 Move 自然解除封顶。
			if (effectiveLength <= 0.0) return 0.0;

			const float tipScale = 1.0f - 0.7f * SmoothStep01(
				static_cast<float>(effectiveLength / (4.0 * referenceRadius)));
			const double taperStart = sourceLength - effectiveLength;
			double arcLength = 0.0;
			for (size_t index = firstTipIndex + 1; index < points.size(); ++index)
			{
				const InkPoint previous = points[index - 1];
				const InkPoint current = points[index];
				const double distance = std::hypot(static_cast<double>(current.x) - previous.x,
					static_cast<double>(current.y) - previous.y);
				const double nextArcLength = arcLength + distance;
				if (arcLength < taperStart && nextArcLength > taperStart && distance > 0.0)
				{
					// 只在派生几何插入 100% 连接点，稀疏采样也不能跨过压缩边界。
					const float ratio = static_cast<float>((taperStart - arcLength) / distance);
					points.insert(points.begin() + index, InkPoint{
						LerpFloat(previous.x, current.x, ratio), LerpFloat(previous.y, current.y, ratio),
						LerpFloat(previous.r, current.r, ratio), LerpFloat(previous.time, current.time, ratio) });
					++index;
				}
				const float progress = static_cast<float>((nextArcLength - taperStart) / effectiveLength);
				points[index].r *= LerpFloat(1.0f, tipScale, SmoothStep01(progress));
				arcLength = nextArcLength;
			}
			return effectiveLength;
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

	void LockPenTerminalState(ActiveStroke& stroke, double physicalUpTime,
		DirectX::XMFLOAT2 lastRawMove, DirectX::XMFLOAT2 rawUp) noexcept
	{
		if (stroke.terminalDisplayTime || !std::isfinite(physicalUpTime)) return;
		stroke.terminalDisplayTime = physicalUpTime;
		stroke.terminalFirstPoint = stroke.realPoints.size();
		stroke.terminalRawMove = lastRawMove;
		stroke.terminalRawUp = rawUp;
		stroke.predictedResults.clear();
		stroke.predictedPoints.clear();
	}

	void ClearPenTerminalState(ActiveStroke& stroke) noexcept
	{
		stroke.terminalDisplayTime.reset();
		stroke.liveTipLength.reset(); // 成功续接可能沿用相同真实时间戳，也必须解除旧停笔封顶。
		stroke.terminalFirstPoint = 0;
		stroke.terminalRawMove = {};
		stroke.terminalRawUp = {};
		stroke.terminalModelCount = 0;
		stroke.terminalAcceptedCount = 0;
		stroke.terminalModelTrace = {};
		stroke.terminalAcceptedTrace = {};
	}

	double ResolvePenDisplayTime(const ActiveStroke& stroke) noexcept
	{
		return stroke.terminalDisplayTime.value_or(stroke.logicalInputTime);
	}

	void AppendTerminalFallbackPoint(ActiveStroke& stroke, const InkPoint& finalPoint)
	{
		if (stroke.realPoints.empty())
		{
			stroke.realPoints.push_back(finalPoint);
			return;
		}
		const float dx = finalPoint.x - stroke.realPoints.back().x;
		const float dy = finalPoint.y - stroke.realPoints.back().y;
		const bool protectedTail = stroke.terminalDisplayTime &&
			stroke.realPoints.size() <= stroke.terminalFirstPoint;
		if (dx * dx + dy * dy > 0.0001f || (protectedTail && (dx != 0.0f || dy != 0.0f)))
			stroke.realPoints.push_back(finalPoint);
		else if (!protectedTail)
			stroke.realPoints.back() = finalPoint;
	}

	void CapturePenTerminalTrace(ActiveStroke& stroke) noexcept
	{
		// 仅诊断开启有界采样；不修改终态几何，末端拐动留待模型轨迹研究。
		if (!stroke.terminalDisplayTime || !stroke.captureTerminalTrace) return;
		stroke.terminalModelCount = stroke.modelScratch.size();
		const size_t start = std::min(stroke.terminalFirstPoint > 0
			? stroke.terminalFirstPoint - 1 : 0, stroke.realPoints.size());
		stroke.terminalAcceptedCount = stroke.realPoints.size() - start;
		stroke.terminalModelTrace = {};
		stroke.terminalAcceptedTrace = {};
		for (size_t i = 0; i < std::min(size_t{8}, stroke.terminalModelCount); ++i)
		{
			const size_t at = stroke.terminalModelCount > 8 ? i * (stroke.terminalModelCount - 1) / 7 : i;
			const auto& p = stroke.modelScratch[at].position;
			stroke.terminalModelTrace[i] = { p.x, p.y };
		}
		for (size_t i = 0; i < std::min(size_t{8}, stroke.terminalAcceptedCount); ++i)
		{
			const size_t at = stroke.terminalAcceptedCount > 8 ? i * (stroke.terminalAcceptedCount - 1) / 7 : i;
			const auto& p = stroke.realPoints[start + at];
			stroke.terminalAcceptedTrace[i] = { p.x, p.y };
		}
	}


	void RecordPenTailRawInput(ActiveStroke& stroke,
		DirectX::XMFLOAT2 position, double physicalTime) noexcept
	{
		if (!std::isfinite(physicalTime) || !std::isfinite(position.x) ||
			!std::isfinite(position.y)) return;
		if (stroke.tailRawCount && physicalTime < stroke.tailRawSamples[stroke.tailRawCount - 1].physicalTime)
			return; // 不把时间倒退样本塞进有序物理窗口。
		size_t remove = 0;
		while (remove < stroke.tailRawCount &&
			stroke.tailRawSamples[remove].physicalTime < physicalTime - 0.160) ++remove;
		if (!remove && stroke.tailRawCount == stroke.tailRawSamples.size()) remove = 1;
		for (size_t i = remove; i < stroke.tailRawCount; ++i)
			stroke.tailRawSamples[i - remove] = stroke.tailRawSamples[i];
		stroke.tailRawCount -= remove;
		stroke.tailRawSamples[stroke.tailRawCount++] = { position, physicalTime,
			stroke.realPoints.empty() ? 0 : stroke.realPoints.size() - 1 };
	}

	namespace
	{
		void FitCompletedPenGeometry(const ActiveStroke& stroke, std::vector<InkPoint>& output)
		{
			using namespace ink::stroke_model;
			// 仅确认完成时调用；断触等待和实时 L1/realPoints 不受影响。
			if (!stroke.terminalDisplayTime || output.size() < 4) return;
			const size_t count = output.size();
			size_t start = count > 512 ? count - 512 : 0;
			if (stroke.tailRawCount)
				start = std::max(start, std::min(stroke.tailRawSamples[0].firstPoint, count - 1));
			if (count - start < 4) return;
			std::vector<TerminalTailReferencePoint> reference;
			reference.reserve(count - start);
			for (size_t i = start; i < count; ++i)
				reference.push_back({ { output[i].x, output[i].y }, { i, i, 0 }, i + 1 == count });
			std::vector<TerminalTailRawPoint> raw;
			for (size_t i = 0; i < stroke.tailRawCount; ++i)
				if (stroke.tailRawSamples[i].physicalTime >= *stroke.terminalDisplayTime - 0.080)
					raw.push_back({ { stroke.tailRawSamples[i].position.x, stroke.tailRawSamples[i].position.y }, i });
			TerminalTailRequest request;
			request.reference = reference; request.raw = raw;
			request.endpoint = { stroke.terminalRawUp.x, stroke.terminalRawUp.y };
			request.replacement_range = { start, count }; request.baseline_token = count;
			request.config.max_raw_points = 64;
			// 产品坐标为画布像素，3px 为当前实验位移上限；不按显示时间匹配 raw。
			TerminalTailScratch fitScratch;
			const auto result = ReconstructLocalTerminalTail(request, fitScratch);
			if (result.status != TerminalTailStatus::kApply || result.patch.baseline_token != count ||
				result.patch.replacement_range.begin != start || result.patch.replacement_range.end != count ||
				result.patch.points.size() != count - start) return;
			for (size_t i = 0; i < result.patch.points.size(); ++i)
				if (result.patch.points[i].source.first != start + i ||
					result.patch.points[i].source.second != start + i || result.patch.points[i].source.fraction != 0)
					return;
			// 求解器只看尾窗；发布前有界检查新尾段与未传入的稳定前缀。
			auto cross = [](Vec2 a, Vec2 b, Vec2 c) {
				return (double(b.x) - a.x) * (double(c.y) - a.y) -
					(double(b.y) - a.y) * (double(c.x) - a.x); };
			auto intersects = [&](Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
				const auto ab1 = cross(a,b,c), ab2 = cross(a,b,d);
				const auto cd1 = cross(c,d,a), cd2 = cross(c,d,b);
				if (ab1 * ab2 < 0 && cd1 * cd2 < 0) return true;
				const auto on = [](Vec2 p, Vec2 q, Vec2 r, double area) {
					return std::abs(area) <= 1e-6 && r.x >= std::min(p.x,q.x) - 1e-6 &&
						r.x <= std::max(p.x,q.x) + 1e-6 && r.y >= std::min(p.y,q.y) - 1e-6 &&
						r.y <= std::max(p.y,q.y) + 1e-6; };
				return on(a,b,c,ab1) || on(a,b,d,ab2) || on(c,d,a,cd1) || on(c,d,b,cd2); };
			size_t work = 0;
			for (size_t i = 1; i < result.patch.points.size(); ++i)
			{
				const auto a = result.patch.points[i - 1].position, b = result.patch.points[i].position;
				// L1 内笔宽沿用原属性，位置变化后仍必须满足圆胶囊公切线条件。
				if (start + i <= stroke.committedIndex && stroke.hasCommittedGeometry &&
					std::abs(output[start+i].r - output[start+i-1].r) >
					0.95 * std::hypot(double(b.x) - a.x, double(b.y) - a.y)) return;
				if (a.x == output[start+i-1].x && a.y == output[start+i-1].y &&
					b.x == output[start+i].x && b.y == output[start+i].y) continue;
				for (size_t j = 1; j <= start; ++j)
				{
					if (++work > request.config.max_validation_work) return;
					if (start + i == j + 1) continue;
					if (intersects(a, b, {output[j-1].x,output[j-1].y}, {output[j].x,output[j].y})) return;
				}
			}
			for (size_t i = 0; i < result.patch.points.size(); ++i)
			{
				output[start + i].x = result.patch.points[i].position.x;
				output[start + i].y = result.patch.points[i].position.y;
			} // 只改位置；笔宽、压力来源、时间保持对应原点。
		}
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
			ApplyLiveTipTaper(output, liveTipTaperSeconds, stroke, false);
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
			scratch = stroke.realPoints;
			FitCompletedPenGeometry(stroke, scratch);
			if (!scratch.empty())
			{
				const size_t tailStart = stroke.hasCommittedGeometry
					? std::min(stroke.committedIndex, scratch.size() - 1) : 0;
				std::vector<InkPoint> tail(scratch.begin() + tailStart, scratch.end());
				ApplyLiveTipTaper(tail, liveTipTaperSeconds, stroke, false);
				EnforceCapsuleTangency(tail);
				// taper 可以插入派生连接点，不能复制到原点数的缓冲区。
				scratch.resize(tailStart);
				scratch.insert(scratch.end(), tail.begin(), tail.end());
			}
			else if (stroke.hasInputStartPoint) scratch.push_back(stroke.inputStartPoint);
			points.reserve(scratch.size());
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

	int RunPenTaperProductionProbe() noexcept
	{
		int failures = 0;
		int assertions = 0;
		const auto check = [&](bool condition, const char* name)
		{
			++assertions;
			if (condition) return;
			++failures;
			if (failures <= 40) std::fprintf(stderr, "[Draw3PenTaper] FAIL: %s\n", name);
		};
		const auto close = [](double actual, double expected, double tolerance = 0.0001)
		{
			return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
		};
		const auto samePoints = [&](std::span<const InkPoint> a, std::span<const InkPoint> b)
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].r != b[i].r || a[i].time != b[i].time)
					return false;
			return true;
		};
		const auto rebuild = [](ActiveStroke& stroke, double duration = 0.055)
		{
			RebuildL0DrawPoints(stroke, duration, StrokeShape::RoundCapsule, 4096, 4096);
		};
		const auto safeContour = [&](std::span<const InkPoint> points, bool monotone)
		{
			bool safe = true;
			for (size_t i = 0; i < points.size(); ++i)
			{
				safe &= std::isfinite(points[i].r) && points[i].r > 0.0f;
				if (!i) continue;
				const double distance = std::hypot(static_cast<double>(points[i].x) - points[i - 1].x,
					static_cast<double>(points[i].y) - points[i - 1].y);
				safe &= std::abs(points[i].r - points[i - 1].r) <= 0.95 * distance + 0.0001;
				if (monotone) safe &= points[i].r <= points[i - 1].r + 0.0001f;
			}
			return safe;
		};
		try
		{
			{
				ActiveStroke cache(5, 100, StrokeWidthMode::Fixed);
				for (int i = 0; i < 200; ++i)
					RecordPenTailRawInput(cache, {float(i), 0}, i * 0.0001);
				check( cache.tailRawCount == 64, "local finalization contract");
				check( cache.tailRawSamples[0].position.x == 136, "local finalization contract");
				RecordPenTailRawInput(cache, {200, 0}, 1.0);
				check( cache.tailRawCount == 1, "local finalization contract");
				RecordPenTailRawInput(cache, {201, 0}, 0.9);
				RecordPenTailRawInput(cache, {NAN, 0}, 1.1);
				check( cache.tailRawCount == 1, "local finalization contract");
				cache.useDisplayTime = true;
				LockPenTerminalState(cache, 1.0, {200,0}, {200,0});
				ClearPenTerminalState(cache);
				check( cache.tailRawCount == 1, "local finalization contract"); // 断触续接不丢历史，但新输入淘汰过期物理窗口。
				RecordPenTailRawInput(cache, {202,0}, 1.2);
				check( cache.tailRawCount == 1 && cache.tailRawSamples[0].position.x == 202, "local finalization contract");
				cache.Reset(5, 100, StrokeWidthMode::Fixed);
				check( cache.tailRawCount == 0, "local finalization contract");
				for (bool assisted : {false, true})
				{
					ActiveStroke stroke(5,100,StrokeWidthMode::Fixed);
					stroke.useDisplayTime = true;
					for (int i = 0; i < 80; ++i)
						stroke.realPoints.push_back({i * .1f,0,2.5f,i*.001f});
					stroke.realPoints.push_back({7.9f,.3f,2.5f,.080f});
					stroke.committedIndex = 78; stroke.hasCommittedGeometry = true;
					stroke.logicalInputTime = stroke.lastMovementInputTime = .080;
					if (assisted)
					{
						stroke.tailRawCount = 2;
						stroke.tailRawSamples[0] = {{7.5f,.1f},.070,0};
						stroke.tailRawSamples[1] = {{7.9f,.3f},.080,0};
					}
					const auto original = stroke.realPoints;
					std::vector<InkPoint> scratch;
					// 等待接续时只重绘实时尾段，不调用完成态拟合。
					LockPenTerminalState(stroke,.080,{7.9f,0},{7.9f,.3f});
					BuildCompletedPenTail(stroke,0,scratch);
					check( stroke.realPoints[79].y == 0 && stroke.committedIndex == 78, "local finalization contract");
					StoredInkStyle style;
					style.inkType = StoredInkType::Pen;
					const auto stored = FinalizeStoredStroke(stroke,style,0,scratch);
					check( stored && stored->Points().size() == original.size(), "local finalization contract");
					check( scratch.size() == original.size(), "local finalization contract");
					bool changedCommitted = false;
					for (size_t i = 0; i < scratch.size(); ++i)
					{
						check( stroke.realPoints[i].x == original[i].x && stroke.realPoints[i].y == original[i].y, "local finalization contract");
						check( scratch[i].time == original[i].time && scratch[i].r == original[i].r, "local finalization contract");
						if (i < stroke.committedIndex && scratch[i].y != original[i].y) changedCommitted = true;
						if (i < 35) check( scratch[i].x == original[i].x && scratch[i].y == original[i].y, "local finalization contract");
					}
					check( changedCommitted, "local finalization contract");
					check( scratch.back().x == 7.9f && scratch.back().y == .3f, "local finalization contract");
					check( stored && stored->Points()[78].y == scratch[78].y, "local finalization contract");
					// 非普通笔完全保留旧中心线；形状使用独立 FinalizeStoredShape。
					style.inkType = StoredInkType::Highlighter;
					const auto highlighter = FinalizeStoredStroke(stroke,style,0,scratch);
					check( highlighter && highlighter->Points()[78].y == original[78].y, "local finalization contract");
					style.inkType = StoredInkType::Pen;
					stroke.terminalRawUp.x = NAN;
					const auto fallback = FinalizeStoredStroke(stroke,style,0,scratch);
					check( fallback && scratch[78].y == original[78].y, "local finalization contract");
				}
			}

			// 测试实际 Rebuild/完成/Stored 入口；比例表独立列出，不能只复制算法自证。
			struct Ratio { float length; float scale; };
			constexpr Ratio ratios[] = { { 0, 1 }, { 0.25f, 0.969921875f },
				{ 0.5f, 0.890625f }, { 1, 0.65f }, { 1.5f, 0.409375f }, { 2, 0.3f }, { 4, 0.3f } };
			for (const float diameter : { 0.125f, 1.0f, 3.0f, 5.3333335f, 8.0f, 24.0f })
			{
				for (const auto test : ratios)
				{
					ActiveStroke stroke(diameter, 500, StrokeWidthMode::Fixed);
					stroke.lastMovementInputTime = stroke.logicalInputTime = 0.04;
					for (int i = 0; i <= 32; ++i)
						stroke.realPoints.push_back({ 100 + diameter * test.length * i / 32, 100,
							diameter * 0.5f, 0.04f * i / 32 });
					const auto original = stroke.realPoints;
					rebuild(stroke);
					check(close(stroke.l0DrawPoints.back().r / (diameter * 0.5), test.scale, 0.0002), "length/diameter table including fractional widths");
					check(stroke.l0DrawPoints.front().r == diameter * 0.5f && samePoints(stroke.realPoints, original), "100 percent start and untapered real points");
					check(safeContour(stroke.l0DrawPoints, true), "length contour has no coarse head or capsule degeneration");
					rebuild(stroke);
					check(close(stroke.l0DrawPoints.back().r / (diameter * 0.5), test.scale, 0.0002), "rebuild never compounds taper factors");
				}
				float previous = 1.0f;
				for (int i = 0; i <= 201; ++i)
				{
					ActiveStroke stroke(diameter, 500, StrokeWidthMode::Fixed);
					stroke.lastMovementInputTime = stroke.logicalInputTime = 0.04;
					stroke.realPoints = { { 100, 100, diameter * 0.5f, 0 },
						{ 100 + diameter * i / 100, 100, diameter * 0.5f, 0.04f } };
					rebuild(stroke);
					const float scale = stroke.l0DrawPoints.back().r / (diameter * 0.5f);
					check(scale <= previous + 0.0002f && std::abs(scale - previous) <= 0.006f,
						"continuous tip at zero and both sides of 2D threshold");
					previous = scale;
				}
			}
			{
				ActiveStroke curve(10, 500, StrokeWidthMode::Fixed);
				curve.lastMovementInputTime = curve.logicalInputTime = 0.04;
				for (int i = 0; i <= 32; ++i)
				{
					const double angle = i * 6.283185307179586 / 32;
					curve.realPoints.push_back({ 100 + 5 * static_cast<float>(std::cos(angle)),
						100 + 5 * static_cast<float>(std::sin(angle)), 5, 0.04f * i / 32 });
				}
				rebuild(curve);
				check(close(curve.l0DrawPoints.back().r, 1.5), "closed circle uses arc length instead of endpoint chord");
				check(safeContour(curve.l0DrawPoints, true), "curved tail remains monotone and tangent safe");
				ActiveStroke irregular(10, 500, StrokeWidthMode::Fixed);
				irregular.lastMovementInputTime = irregular.logicalInputTime = 0.04;
				irregular.realPoints = { {100,100,5,0.04f}, {100,100,5,0.04f},
					{103,100,5,0.04f}, {103,100,5,0.04f}, {107,100,5,0.04f}, {120,100,5,0.04f} };
				rebuild(irregular);
				check(close(irregular.l0DrawPoints.back().r, 1.5) && safeContour(irregular.l0DrawPoints, true),
					"duplicate and equal-time samples cannot erase the taper front");
				irregular.logicalInputTime += 0.0275;
				rebuild(irregular);
				check(irregular.l0DrawPoints.size() == irregular.realPoints.size() + 1 &&
					close(irregular.l0DrawPoints[5].x, 110) && close(irregular.l0DrawPoints[5].r, 5),
					"sparse compression frontier inserts only a derived 100 percent connector");
				check(close(irregular.l0DrawPoints.back().r, 3.25) && safeContour(irregular.l0DrawPoints, true),
					"equal-time catchup re-smooths the entire remaining 1D tail");
				ActiveStroke variable(10, 500, StrokeWidthMode::Fixed);
				variable.lastMovementInputTime = variable.logicalInputTime = 0.04;
				variable.realPoints = { {100,100,10,0}, {110,100,7.5f,0.02f}, {120,100,5,0.04f} };
				rebuild(variable);
				check(close(variable.l0DrawPoints.back().r, 3.25), "reference diameter uses largest original body radius");
			}
			{
				ActiveStroke stroke(10, 500, StrokeWidthMode::Fixed);
				for (int i = 0; i <= 20; ++i)
					stroke.realPoints.push_back({ 100 + 10.0f * i, 100, 5, 0.01f * i });
				stroke.committedIndex = 10; stroke.hasCommittedGeometry = true;
				stroke.lastMovementInputTime = stroke.logicalInputTime = 0.20;
				stroke.predictedPoints = { {310,100,5,0.21f}, {330,100,5,0.23f} };
				const auto original = stroke.realPoints;
				rebuild(stroke);
				check(stroke.l0DrawPoints.front().x == 200 && stroke.l0DrawPoints.front().r == 5 &&
					stroke.l0DrawPoints[8].r == 5 && stroke.l0DrawPoints[9].r < 5,
					"original prediction cutoff starts at real time .18 rather than L0 .10");
				check(close(*stroke.liveTipLength, 50), "prediction arc belongs to original taper candidate");
				const auto protectedIndex = ink_prediction_detail::FindProtectedStartIndex(stroke.realPoints, 0.055 + GetPredictionDurationSeconds(stroke));
				check(protectedIndex < 18 && stroke.committedIndex == 10 && samePoints(stroke.realPoints, original),
					"L1 protection and stable prefix remain independent from taper shape");
				const RECT oldBounds = stroke.currentL0Rect;
				stroke.predictedPoints.clear(); stroke.logicalInputTime += 0.0275;
				rebuild(stroke);
				RECT dirty = oldBounds; UnionRectInPlace(dirty, stroke.currentL0Rect);
				check(dirty.left <= oldBounds.left && dirty.right >= oldBounds.right &&
					dirty.top <= stroke.currentL0Rect.top && dirty.bottom >= stroke.currentL0Rect.bottom,
					"old prediction and newly wider L0 both belong to dirty union");
				check(stroke.l0DrawPoints.front().r == original[10].r && samePoints(stroke.realPoints, original), "compressed frontier never rewrites shared L1 anchor");
			}
			{
				ActiveStroke stopped(10, 500, StrokeWidthMode::Fixed);
				stopped.realPoints = { {100,100,5,0}, {140,100,5,0.04f} };
				stopped.lastMovementInputTime = stopped.logicalInputTime = 0.04;
				rebuild(stopped);
				stopped.logicalInputTime += 0.0275; rebuild(stopped);
				const double before = *stopped.liveTipLength;
				stopped.realPoints.push_back({180,100,5,0.04f});
				stopped.logicalInputTime += 0.001; rebuild(stopped);
				check(*stopped.liveTipLength <= before && safeContour(stopped.l0DrawPoints, true), "synthetic catchup cannot lengthen a retiring taper");
				stopped.logicalInputTime = 0.095; rebuild(stopped);
				check(*stopped.liveTipLength == 0 && stopped.l0DrawPoints.back().r == 5, "dwell finishes at normal body width");
				ActiveStroke other(10, 500, StrokeWidthMode::Fixed);
				other.realPoints = { {100,100,5,0}, {140,100,5,0.04f} };
				other.lastMovementInputTime = other.logicalInputTime = 0.04; rebuild(other);
				check(close(other.l0DrawPoints.back().r, 1.5) && *stopped.liveTipLength == 0, "simultaneous strokes keep independent taper caches");
				stopped.lastMovementInputTime = stopped.logicalInputTime = 0.12;
				stopped.realPoints.push_back({220,100,5,0.10f});
				stopped.realPoints.push_back({260,100,5,0.12f}); rebuild(stopped);
				check(*stopped.liveTipLength > 0 && close(stopped.l0DrawPoints.back().r, 1.5), "new real movement releases the old zero-length cap");
				stopped.Reset(10, 500, StrokeWidthMode::Fixed);
				check(!stopped.liveTipLength && stopped.liveTipDisplayTime == 0 && stopped.liveTipMovementTime == 0, "pooled Reset clears transient taper state");
			}
			// 物理 Up、延迟处理与 Stored 共用配方，不能被更晚的帧继续养粗。
			for (const double upAge : { 0.0, 0.015, 0.055, 1.0 })
			{
				ActiveStroke stroke(10, 500, StrokeWidthMode::Fixed);
				stroke.realPoints = { {100,100,5,0}, {110,100,5,0.01f}, {120,100,5,0.02f}, {140,100,5,0.04f} };
				stroke.lastMovementInputTime = stroke.logicalInputTime = 0.04;
				const double upTime = 0.04 + upAge;
				stroke.logicalInputTime = upTime + 2; rebuild(stroke); // 故意先计算晚于物理 Up 的 live cache。
				LockPenTerminalState(stroke, upTime, {140,100}, {140,100});
				std::vector<InkPoint> completed;
				BuildCompletedPenTail(stroke, 0.055, completed);
				check(upAge >= 0.055 ? completed.back().r == 5 : completed.back().r < 5,
					"late live cache cannot contaminate earlier physical Up");
				rebuild(stroke);
				check(samePoints(completed, stroke.l0DrawPoints), "locked active and completed share identical recipe");
				stroke.logicalInputTime += 100;
				std::vector<InkPoint> later; BuildCompletedPenTail(stroke, 0.055, later);
				check(samePoints(completed, later), "delayed completion does not age physical Up");
				const auto stored = FinalizeStoredStroke(stroke, {}, 0.055, later);
				bool sameStored = stored && stored->Points().size() == completed.size();
				if (sameStored) for (size_t i = 0; i < completed.size(); ++i)
					sameStored &= close(stored->Points()[i].width, completed[i].r * 2);
				check(sameStored, "Stored stroke persists exactly completed widths without prediction");
				ClearPenTerminalState(stroke);
				stroke.lastMovementInputTime = stroke.logicalInputTime;
				rebuild(stroke);
				check(stroke.l0DrawPoints.back().r < 5, "successful reconnect restores live aging epoch");
			}
			{
				ActiveStroke sameTime(10, 500, StrokeWidthMode::Fixed);
				sameTime.lastMovementInputTime = 0.04; sameTime.logicalInputTime = 0.0675;
				sameTime.realPoints = { {100,100,5,0}, {120,100,5,0.04f} };
				rebuild(sameTime);
				const double oldLength = *sameTime.liveTipLength;
				LockPenTerminalState(sameTime, 0.0675, {120,100}, {120,100});
				ClearPenTerminalState(sameTime);
				check(!sameTime.liveTipLength, "successful reconnect clears cap even with identical real timestamp");
				sameTime.realPoints.push_back({160,100,5,0.04f}); rebuild(sameTime);
				check(*sameTime.liveTipLength > oldLength && close(sameTime.l0DrawPoints.back().r, 1.5),
					"same-time resumed geometry can develop a new length-constrained tip");
			}
			for (const auto mode : { StrokeWidthMode::HardwarePressure, StrokeWidthMode::Fixed })
			{
				ActiveStroke stroke(10, 500, mode);
				stroke.realPoints = { {100,100,5,0}, {140,100,5,0.04f} };
				stroke.lastMovementInputTime = stroke.logicalInputTime = 0.04;
				rebuild(stroke, mode == StrokeWidthMode::HardwarePressure ? ResolveLiveTipTaperDurationSeconds(mode, 0.055) : 0.0);
				check(samePoints(stroke.realPoints, stroke.l0DrawPoints), "hardware pressure and disabled taper stay unchanged");
				std::vector<InkPoint> tail; BuildCompletedPenTail(stroke, 0, tail);
				check(samePoints(stroke.realPoints, tail), "disabled completed geometry stays unchanged");
			}
			{
				ActiveStroke marker(50, 500, StrokeWidthMode::Fixed, true);
				marker.realPoints = { {100,100,25,0}, {140,100,25,0.04f} };
				rebuild(marker);
				check(samePoints(marker.realPoints, marker.l0DrawPoints) && !marker.l0HighlighterGeometry.primitives.empty(), "highlighter retains its rectangle path");
			}
			int modelCases = 0;
			int resumeCases = 0;
			double maximumSettleAge = 0;
			size_t maximumOutputs = 0;
			for (const auto profileId : { StrokeTimingProfileId::Fps30, StrokeTimingProfileId::Fps60,
				StrokeTimingProfileId::Fps120, StrokeTimingProfileId::Fps240 })
				for (const auto prediction : { InkPredictionMode::Disabled, InkPredictionMode::StrokeEnd, InkPredictionMode::Kalman })
					for (const float speed : { 25.0f, 500.0f, 1500.0f })
						for (const auto mode : { StrokeWidthMode::Fixed, StrokeWidthMode::SimulatedPressure })
						{
							++modelCases;
							auto config = CreateStrokeModelConfiguration(96);
							config.timingProfile = ink_prediction_detail::GetStrokeTimingProfile(profileId);
							const auto& profile = config.timingProfile;
							const double frame = 1.0 / profile.target_fps;
							config.modelParams.sampling_params.min_output_rate = profile.min_output_rate;
							config.modelParams.wobble_smoother_params.timeout = ink::stroke_model::Duration(profile.wobble_timeout_seconds);
							config.kalmanPredictorParams.prediction_interval = ink::stroke_model::Duration(profile.prediction_interval_seconds);
							config.kalmanPredictorParams.max_time_samples = profile.kalman_max_time_samples;
							config.kalmanPredictorParams.confidence_params.desired_number_of_samples = profile.kalman_desired_number_of_samples;
							if (prediction == InkPredictionMode::Disabled) config.modelParams.prediction_params = ink::stroke_model::DisabledPredictorParams{};
							else if (prediction == InkPredictionMode::StrokeEnd) config.modelParams.prediction_params = ink::stroke_model::StrokeEndPredictorParams{};
							else config.modelParams.prediction_params = config.kalmanPredictorParams;
							const float diameter = speed < 100 ? 3.25f : speed < 1000 ? 6.0f : 16.0f;
							ActiveStroke stroke(diameter, config.expectedSpeed, mode);
							stroke.useDisplayTime = true;
							check(stroke.modeler.Reset(config.modelParams).ok(), "real model Reset");
							double modelTime = 0;
							DirectX::XMFLOAT2 endpoint{100,100};
							const auto update = [&](ink::stroke_model::Input::EventType event, double time,
								std::vector<ink::stroke_model::Result>& output)
							{
								const ink::stroke_model::Input input{ .event_type = event,
									.position = ink::stroke_model::Vec2(endpoint.x,endpoint.y), .time = ink::stroke_model::Time(time),
									.pressure = 1, .tilt = 1.57079632679f, .orientation = 0 };
								const size_t before = output.size();
								const bool ok = stroke.modeler.Update(input, output).ok();
								maximumOutputs = std::max(maximumOutputs, output.size() - before);
								check(ok && output.size() - before <= static_cast<size_t>(profile.max_outputs_per_call), "real model bounded output budget");
								return ok;
							};
							update(ink::stroke_model::Input::EventType::kDown, 0, stroke.modeledResults);
							AppendNewModeledPoints(stroke);
							for (int i = 1; i <= 24; ++i)
							{
								modelTime = i / 120.0;
								endpoint.x = 100 + speed * static_cast<float>(modelTime);
								stroke.logicalInputTime = stroke.lastMovementInputTime = modelTime;
								update(ink::stroke_model::Input::EventType::kMove, modelTime, stroke.modeledResults);
								AppendNewModeledPoints(stroke, speed);
								stroke.predictedResults.clear();
								if (prediction != InkPredictionMode::Disabled) check(stroke.modeler.Predict(stroke.predictedResults).ok(), "real moving prediction");
								RebuildPredictedPoints(stroke); rebuild(stroke); UpdateIdleFreezeState(stroke, true, false, 0.055);
							}
							const double stopTime = stroke.lastMovementInputTime;
							double settleAge = -1;
							double previousLength = *stroke.liveTipLength;
							float previousScale = 0;
							float previousDistance = std::hypot(stroke.realPoints.back().x - endpoint.x, stroke.realPoints.back().y - endpoint.y);
							size_t frozenPoints = 0;
							for (int i = 1; i <= static_cast<int>(10 * profile.target_fps); ++i)
							{
								stroke.logicalInputTime = stopTime + i * frame;
								if (!stroke.idleFrozen && ShouldStartEndpointSettling(stroke.logicalInputTime - stopTime, frame))
								{
									BeginEndpointAdmission(stroke, endpoint);
									bool settled = IsModeledTipSettled(LatestModeledTip(stroke), endpoint, frame);
									stroke.modelScratch.clear();
									if (!settled)
									{
										modelTime = ResolvePenModelInputTime(stroke, stroke.logicalInputTime, modelTime, frame);
										update(ink::stroke_model::Input::EventType::kMove, modelTime, stroke.modelScratch);
									}
									AppendEndpointBoundedModeledPoints(stroke, stroke.modelScratch, -1, stopTime, settled);
									settled = IsModeledTipSettled(LatestModeledTip(stroke), endpoint, frame);
									if (settled) { stroke.modelClockStopped = true; if (settleAge < 0) settleAge = i * frame; }
									const float distance = std::hypot(stroke.realPoints.back().x - endpoint.x, stroke.realPoints.back().y - endpoint.y);
									check(distance <= previousDistance + 0.0001f && stroke.realPoints.back().x <= endpoint.x + 0.05f, "catchup endpoint is monotone and never crosses raw plane");
									previousDistance = distance;
								}
								stroke.predictedResults.clear();
								if (!stroke.endpointAdmission.active && prediction != InkPredictionMode::Disabled) check(stroke.modeler.Predict(stroke.predictedResults).ok(), "tracking keeps prediction before settling gate");
								RebuildPredictedPoints(stroke);
								const auto raw = stroke.realPoints;
								rebuild(stroke);
								if (stroke.endpointAdmission.active)
								{
									check(*stroke.liveTipLength <= previousLength + 0.0001, "frame-driven dwell length never rebounds");
									const float scale = stroke.l0DrawPoints.back().r / stroke.realPoints.back().r;
									check(scale + 0.0001f >= previousScale, "frame-driven tip thickens continuously to body");
									previousScale = scale;
								}
								previousLength = *stroke.liveTipLength;
								check(samePoints(raw, stroke.realPoints) && safeContour(stroke.l0DrawPoints, mode == StrokeWidthMode::Fixed), "production frame geometry preserves raw radii and contour safety");
								UpdateIdleFreezeState(stroke, false, IsModeledTipSettled(LatestModeledTip(stroke), endpoint, frame), 0.055);
								if (stroke.idleFrozen && !frozenPoints) frozenPoints = stroke.realPoints.size();
								if (frozenPoints) check(stroke.realPoints.size() == frozenPoints, "ten-second frame loop retains bounded frozen points");
							}
							maximumSettleAge = std::max(maximumSettleAge, settleAge);
							check(settleAge >= 0 && settleAge <= 0.2 + 0.000001 && stroke.idleFrozen && *stroke.liveTipLength == 0,
								"real model settles by 200ms then normal-width visuals freeze");
							// 一半用真实模型续画，覆盖同向、直角、反向；其余保留停稳后真实 kUp。
							const bool resumed = (modelCases / 2) % 2 != 0;
							if (resumed)
							{
								++resumeCases;
								const size_t beforeResume = stroke.realPoints.size();
								const DirectX::XMFLOAT2 origin = endpoint;
								const double resumeTime = stroke.logicalInputTime;
								for (int i = 1; i <= 24; ++i)
								{
									const float distance = speed * static_cast<float>(i * frame);
									endpoint = prediction == InkPredictionMode::Disabled
										? DirectX::XMFLOAT2{origin.x - distance, origin.y}
										: prediction == InkPredictionMode::StrokeEnd
										? DirectX::XMFLOAT2{origin.x, origin.y + distance}
										: DirectX::XMFLOAT2{origin.x + distance, origin.y};
									stroke.lastMovementInputTime = stroke.logicalInputTime = resumeTime + i * frame;
									stroke.liveTipLength.reset(); stroke.idleFrozen = false; // 与生产真实位置变化分支一致。
									modelTime = ResolvePenModelInputTime(stroke, stroke.logicalInputTime, modelTime, frame);
									if (stroke.endpointAdmission.active)
									{
										stroke.modelScratch.clear();
										update(ink::stroke_model::Input::EventType::kMove, modelTime, stroke.modelScratch);
										AppendRecoveryModeledPoints(stroke, stroke.modelScratch, endpoint, speed);
									}
									else
									{
										update(ink::stroke_model::Input::EventType::kMove, modelTime, stroke.modeledResults);
										AppendNewModeledPoints(stroke, speed);
									}
									stroke.predictedResults.clear();
									if (!stroke.endpointAdmission.active && prediction != InkPredictionMode::Disabled) check(stroke.modeler.Predict(stroke.predictedResults).ok(), "real resumed prediction after admission unlock");
									RebuildPredictedPoints(stroke); rebuild(stroke);
									UpdateIdleFreezeState(stroke, true, false, 0.055);
								}
								check(!stroke.endpointAdmission.active && stroke.realPoints.size() > beforeResume && *stroke.liveTipLength > 0,
									"real long-stop resume unlocks model geometry and taper without hard raw pinning");
							}
							LockPenTerminalState(stroke, stroke.logicalInputTime, endpoint, endpoint);
							stroke.modelScratch.clear(); modelTime = ResolvePenModelInputTime(stroke, stroke.logicalInputTime, modelTime, frame);
							update(ink::stroke_model::Input::EventType::kUp, modelTime, stroke.modelScratch);
							BeginEndpointAdmission(stroke, endpoint); // 与实际终态分支相同，恢复后也重新建立 Up 接纳边界。
							const auto terminal = AppendEndpointBoundedModeledPoints(stroke, stroke.modelScratch, -1, stroke.lastMovementInputTime, true);
							check(!stroke.modelScratch.empty() && terminal.endpointPinned && stroke.endpointAdmission.visualPinned &&
								close(stroke.realPoints.back().x, endpoint.x) && close(stroke.realPoints.back().y, endpoint.y),
								"actual kUp output is admitted and pinned to the true terminal endpoint");
							std::vector<InkPoint> tail; BuildCompletedPenTail(stroke, 0.055, tail);
							check(resumed ? safeContour(tail, false) : close(tail.back().r, stroke.realPoints.back().r),
								"real model stationary/resumed Up preserves its physical-time recipe");
							const auto completed = tail;
							stroke.logicalInputTime += 100;
							const auto stored = FinalizeStoredStroke(stroke, {}, 0.055, tail);
							bool storedMatches = stored && samePoints(completed, tail) && stored->Points().size() == tail.size();
							if (storedMatches) for (size_t i = 0; i < tail.size(); ++i)
								storedMatches &= close(stored->Points()[i].width, tail[i].r * 2);
							check(storedMatches, "actual-model Up completed and Stored stay identical after processing delay");
						}
			std::fprintf(stderr, "[Draw3PenTaper] modelCases=%d resumeCases=%d maxSettleMs=%.3f maxOutputs=%zu\n", modelCases, resumeCases, maximumSettleAge * 1000, maximumOutputs);
		}
		catch (...)
		{
			check(false, "unexpected exception");
		}
		std::fprintf(stderr, "[Draw3PenTaper] assertions=%d failures=%d\n", assertions, failures);
		return failures == 0 ? 0 : 1;
	}

	void ActiveStroke::Reset(float baseDiameter, float expectedSpeed,
		StrokeWidthMode widthModeValue, bool highlighterValue)
	{
		modeledResults.clear();
		modelScratch.clear();
		predictedResults.clear();
		realPoints.clear();
		tailRawCount = 0;
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
		liveTipLength.reset();
		liveTipMovementTime = 0.0;
		liveTipDisplayTime = 0.0;
		lastFrameWallTime = 0.0;
		logicalInputTime = 0.0;
		ClearPenTerminalState(*this);
		captureTerminalTrace = false;
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
				(!stroke.terminalDisplayTime || stroke.realPoints.size() > stroke.terminalFirstPoint) &&
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
		stroke.liveTipLength = ApplyLiveTipTaper(stroke.l0DrawPoints, liveTipDurationSeconds, stroke, true);
		stroke.liveTipMovementTime = stroke.lastMovementInputTime;
		stroke.liveTipDisplayTime = ResolvePenDisplayTime(stroke);
		EnforceCapsuleTangency(stroke.l0DrawPoints); // 笔锋只做公切线安全投影，不再套用稳定笔宽时间限速。
		stroke.currentL0Rect = RectFromStrokePoints(stroke.l0DrawPoints, width, height, shape);
	}

	LiveRasterSubmission CommitStablePrefixToL1(ActiveStroke& stroke, double liveTipDurationSeconds,
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
			if (renderer.DrawHighlighterPrimitives(
				stroke.l0HighlighterGeometry.primitives, color) < 0) return { {}, false };
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
			if (renderer.DrawStrokeOrDot(stablePoints, color, shape) < 0)
				return { {}, false };
			dirty = RectFromStrokePoints(stablePoints, width, height, shape);
		}
		stroke.committedIndex = protectedStartIndex; // 推进提交游标，后续帧不重复提交稳定前缀。
		stroke.hasCommittedGeometry = true;
		return { dirty, true };
	}

	LiveRasterSubmission CommitEraserRealPointsToL1(ActiveStroke& stroke, StrokeShape shape,
		InkRenderer& renderer, int width, int height)
	{
		std::array<InkPoint, 1> fallbackPoint = {};
		std::span<const InkPoint> newPoints;
		size_t latestIndex = stroke.committedIndex;
		if (stroke.realPoints.empty())
		{
			if (stroke.hasCommittedGeometry || !stroke.hasInputStartPoint) return {};
			fallbackPoint[0] = stroke.inputStartPoint;
			newPoints = fallbackPoint; // 建模器尚未给点时也要保证单击橡皮可见。
		}
		else
		{
			latestIndex = stroke.realPoints.size() - 1;
			if (stroke.hasCommittedGeometry && latestIndex <= stroke.committedIndex) return {};
			const size_t startIndex = stroke.hasCommittedGeometry ? stroke.committedIndex : 0;
			newPoints = std::span<const InkPoint>(stroke.realPoints).subspan(startIndex);
		}

		renderer.SetOperatorTarget(renderer.layerL1);
		if (renderer.DrawStrokeOrDot(newPoints, kTransparentLayerClearColor,
			shape, InkOperatorKind::Erase) < 0) return { {}, false };
		if (!stroke.realPoints.empty()) stroke.committedIndex = latestIndex;
		stroke.hasCommittedGeometry = true;
		return { RectFromStrokePoints(newPoints, width, height, shape), true };
	}

	bool DrawL0LiveComposite(ActiveStroke& stroke, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkRenderer& renderer, bool clearLayer)
	{
		if (clearLayer) renderer.ClearOperatorLayer(renderer.layerL0); // 多 contact 帧由调用方只清一次共享 L0。
		if (stroke.highlighter)
		{
			if (stroke.l0HighlighterGeometry.primitives.empty()) return true;
			renderer.SetOperatorTarget(renderer.layerL0);
			return renderer.DrawHighlighterPrimitives(
				stroke.l0HighlighterGeometry.primitives, color) >= 0;
		}
		if (stroke.l0DrawPoints.empty()) return true;
		renderer.SetOperatorTarget(renderer.layerL0);
		return renderer.DrawStrokeOrDot(stroke.l0DrawPoints, color, shape) >= 0;
	}
}
