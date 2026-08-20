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

module Inkeys.Drawing.Draw3.ink_prediction;

namespace Inkeys::Drawing::Draw3
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

	void SpeedEraserOcController::Reset(float positionX, float positionY,
		double timeSeconds, SpeedEraserStartKind startKind) noexcept
	{
		segments_.fill({});
		segmentStart_ = 0;
		segmentCount_ = 0;
		acceptedPositionX_ = std::isfinite(positionX) ? positionX : 0.0f;
		acceptedPositionY_ = std::isfinite(positionY) ? positionY : 0.0f;
		acceptedTime_ = std::isfinite(timeSeconds) ? timeSeconds : 0.0;
		lastAdvanceTime_ = acceptedTime_;
		pauseTime_ = 0.0;
		decreaseCandidateTime_ = 0.0;
		middleReachedTime_ = 0.0;
		reverseCandidateTime_ = 0.0;
		turnGuardUntil_ = 0.0;
		turnGuardHardDeadline_ = 0.0;
		totalTravelDip_ = 0.0f;
		directionTravelDip_ = 0.0f;
		directionSumX_ = 0.0f;
		directionSumY_ = 0.0f;
		reliableDirectionX_ = 0.0f;
		reliableDirectionY_ = 0.0f;
		reverseDirectionX_ = 0.0f;
		reverseDirectionY_ = 0.0f;
		reverseTravelDip_ = 0.0f;
		turnRearmTravelDip_ = 8.0f;
		turnGuardDiameter_ = kSpeedEraserMinimumDiameterPx;
		currentDiameter_ = kSpeedEraserMinimumDiameterPx;
		targetDiameter_ = kSpeedEraserMinimumDiameterPx;
		touchStartup_ = startKind == SpeedEraserStartKind::Touch;
		initialized_ = true;
		paused_ = false;
		hasReliableDirection_ = false;
		reverseCandidate_ = false;
		decreaseCandidate_ = false;
		decreaseCandidateCoherent_ = false;
		waitAtMiddle_ = false;
	}

	void SpeedEraserOcController::AddSegment(const MotionSegment& segment) noexcept
	{
		constexpr double kMotionWindowSeconds = 0.064;
		const double expiredBefore = segment.endTime - kMotionWindowSeconds;
		while (segmentCount_ > 0 &&
			segments_[segmentStart_].endTime <= expiredBefore)
		{
			segmentStart_ = (segmentStart_ + 1) % kSegmentCapacity;
			--segmentCount_;
		}
		if (segmentCount_ == kSegmentCapacity)
		{
			segmentStart_ = (segmentStart_ + 1) % kSegmentCapacity;
			--segmentCount_;
		}
		segments_[(segmentStart_ + segmentCount_) % kSegmentCapacity] = segment;
		++segmentCount_;
	}

	float SpeedEraserOcController::MotionDistance(double timeSeconds) const noexcept
	{
		constexpr double kMotionWindowSeconds = 0.064;
		if (!initialized_ || !std::isfinite(timeSeconds)) return 0.0f;
		const double windowStart = timeSeconds - kMotionWindowSeconds;
		float distance = 0.0f;
		for (size_t offset = 0; offset < segmentCount_; ++offset)
		{
			const MotionSegment& segment =
				segments_[(segmentStart_ + offset) % kSegmentCapacity];
			if (segment.endTime < windowStart || segment.startTime > timeSeconds) continue;
			const double duration = segment.endTime - segment.startTime;
			if (duration <= 0.000001)
			{
				distance += segment.distanceDip;
				continue;
			}
			const double overlapStart = std::max(segment.startTime, windowStart);
			const double overlapEnd = std::min(segment.endTime, timeSeconds);
			if (overlapEnd <= overlapStart) continue;
			distance += segment.distanceDip * static_cast<float>(
				(overlapEnd - overlapStart) / duration);
		}
		return distance;
	}

	float SpeedEraserOcController::ResolveTargetDiameter(double timeSeconds) const noexcept
	{
		const float distance = MotionDistance(timeSeconds);
		float target = kSpeedEraserMinimumDiameterPx;
		if (distance > 3.0f)
		{
			const float ratio = std::clamp((distance - 3.0f) / 21.0f, 0.0f, 1.0f);
			target = LerpFloat(kSpeedEraserNormalDiameterPx,
				kSpeedEraserMaximumDiameterPx, ratio);
		}
		else if (distance > 0.75f)
		{
			const float ratio = std::clamp((distance - 0.75f) / 2.25f, 0.0f, 1.0f);
			target = LerpFloat(kSpeedEraserMinimumDiameterPx,
				kSpeedEraserNormalDiameterPx, ratio);
		}

		if (!touchStartup_) return target;
		float startupLimit = kSpeedEraserMinimumDiameterPx;
		if (totalTravelDip_ > 20.0f)
		{
			const float ratio = std::clamp((totalTravelDip_ - 20.0f) / 12.0f, 0.0f, 1.0f);
			startupLimit = LerpFloat(kSpeedEraserNormalDiameterPx,
				kSpeedEraserMaximumDiameterPx, ratio);
		}
		else if (totalTravelDip_ > 8.0f)
		{
			const float ratio = std::clamp((totalTravelDip_ - 8.0f) / 12.0f, 0.0f, 1.0f);
			startupLimit = LerpFloat(kSpeedEraserMinimumDiameterPx,
				kSpeedEraserNormalDiameterPx, ratio);
		}
		return std::min(target, startupLimit);
	}

	void SpeedEraserOcController::UpdateDirection(const MotionSegment& segment,
		float previousWindowDistance, double timeSeconds) noexcept
	{
		constexpr float kReverseDot = -0.70710678f; // 135°。
		constexpr double kReverseConfirmationSeconds = 0.110;
		constexpr float kDirectionMinimumTravelDip = 1.5f;
		constexpr float kTurnRearmTravelDip = 8.0f;
		const auto normalize = [](float& x, float& y) noexcept
		{
			const float length = std::hypot(x, y);
			if (length <= 0.0001f) return false;
			x /= length;
			y /= length;
			return true;
		};

		if (reverseCandidate_ &&
			timeSeconds - reverseCandidateTime_ > kReverseConfirmationSeconds)
		{
			reverseCandidate_ = false;
			reverseTravelDip_ = 0.0f;
		}

		const float reliableDot = hasReliableDirection_
			? segment.directionX * reliableDirectionX_ +
				segment.directionY * reliableDirectionY_ : 1.0f;
		if (!reverseCandidate_ && hasReliableDirection_ && reliableDot <= kReverseDot &&
			previousWindowDistance >= 3.0f && turnRearmTravelDip_ >= kTurnRearmTravelDip)
		{
			reverseCandidate_ = true;
			reverseCandidateTime_ = timeSeconds;
			reverseDirectionX_ = segment.directionX;
			reverseDirectionY_ = segment.directionY;
			reverseTravelDip_ = segment.distanceDip;
			turnGuardDiameter_ = currentDiameter_;
		}
		else if (reverseCandidate_)
		{
			const float reverseDot = segment.directionX * reverseDirectionX_ +
				segment.directionY * reverseDirectionY_;
			if (reverseDot >= 0.5f)
			{
				reverseDirectionX_ += segment.directionX * segment.distanceDip;
				reverseDirectionY_ += segment.directionY * segment.distanceDip;
				normalize(reverseDirectionX_, reverseDirectionY_);
				reverseTravelDip_ += segment.distanceDip;
			}
			else if (reliableDot > kReverseDot)
			{
				reverseCandidate_ = false;
				reverseTravelDip_ = 0.0f;
			}
		}

		if (reverseCandidate_ && reverseTravelDip_ >= kDirectionMinimumTravelDip &&
			timeSeconds - reverseCandidateTime_ <= kReverseConfirmationSeconds)
		{
			// 可靠折返只保护一次；必须继续移动足够距离后才重新布防。
			turnGuardUntil_ = timeSeconds + 0.120;
			turnGuardHardDeadline_ = reverseCandidateTime_ + 0.160;
			turnGuardDiameter_ = std::max(turnGuardDiameter_, currentDiameter_);
			reliableDirectionX_ = reverseDirectionX_;
			reliableDirectionY_ = reverseDirectionY_;
			hasReliableDirection_ = normalize(
				reliableDirectionX_, reliableDirectionY_);
			directionTravelDip_ = reverseTravelDip_;
			directionSumX_ = reliableDirectionX_ * directionTravelDip_;
			directionSumY_ = reliableDirectionY_ * directionTravelDip_;
			turnRearmTravelDip_ = 0.0f;
			reverseCandidate_ = false;
			reverseTravelDip_ = 0.0f;
			decreaseCandidate_ = false;
			return;
		}

		if (reverseCandidate_) return;
		turnRearmTravelDip_ = std::min(kTurnRearmTravelDip,
			turnRearmTravelDip_ + segment.distanceDip);
		if (hasReliableDirection_ && reliableDot >= 0.5f)
		{
			directionSumX_ = reliableDirectionX_ * std::min(directionTravelDip_, 8.0f) +
				segment.directionX * segment.distanceDip;
			directionSumY_ = reliableDirectionY_ * std::min(directionTravelDip_, 8.0f) +
				segment.directionY * segment.distanceDip;
			directionTravelDip_ = std::min(8.0f,
				directionTravelDip_ + segment.distanceDip);
			reliableDirectionX_ = directionSumX_;
			reliableDirectionY_ = directionSumY_;
			hasReliableDirection_ = normalize(
				reliableDirectionX_, reliableDirectionY_);
			return;
		}
		if (!hasReliableDirection_ && directionTravelDip_ > 0.0f)
		{
			float provisionalX = directionSumX_;
			float provisionalY = directionSumY_;
			if (normalize(provisionalX, provisionalY) &&
				segment.directionX * provisionalX + segment.directionY * provisionalY >= 0.5f)
			{
				directionSumX_ += segment.directionX * segment.distanceDip;
				directionSumY_ += segment.directionY * segment.distanceDip;
				directionTravelDip_ += segment.distanceDip;
				if (directionTravelDip_ >= kDirectionMinimumTravelDip)
				{
					reliableDirectionX_ = directionSumX_;
					reliableDirectionY_ = directionSumY_;
					hasReliableDirection_ = normalize(
						reliableDirectionX_, reliableDirectionY_);
				}
				return;
			}
		}

		directionSumX_ = segment.directionX * segment.distanceDip;
		directionSumY_ = segment.directionY * segment.distanceDip;
		directionTravelDip_ = segment.distanceDip;
		hasReliableDirection_ = false;
		if (directionTravelDip_ >= kDirectionMinimumTravelDip)
		{
			reliableDirectionX_ = directionSumX_;
			reliableDirectionY_ = directionSumY_;
			hasReliableDirection_ = normalize(
				reliableDirectionX_, reliableDirectionY_);
		}
	}

	float SpeedEraserOcController::StepDiameter(
		double timeSeconds, bool coherentMotionUpdate) noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(timeSeconds))
			return currentDiameter_;
		timeSeconds = std::max(timeSeconds, lastAdvanceTime_);
		const double previousAdvanceTime = lastAdvanceTime_;
		const double deltaSeconds = std::max(0.0, timeSeconds - previousAdvanceTime);
		lastAdvanceTime_ = timeSeconds;

		if (reverseCandidate_ && timeSeconds - reverseCandidateTime_ > 0.110)
		{
			reverseCandidate_ = false;
			reverseTravelDip_ = 0.0f;
		}
		float requestedTarget = ResolveTargetDiameter(timeSeconds);
		const double guardDeadline = std::min(turnGuardUntil_, turnGuardHardDeadline_);
		if (guardDeadline > timeSeconds)
		{
			requestedTarget = std::max(requestedTarget, turnGuardDiameter_);
			decreaseCandidate_ = false;
		}
		else if (turnGuardUntil_ > 0.0)
		{
			turnGuardUntil_ = 0.0;
			turnGuardHardDeadline_ = 0.0;
			// 保护消费后再累计 8 DIP，避免短幅往返在保护期内提前重装而永久锁住大尺寸。
			turnRearmTravelDip_ = 0.0f;
		}
		targetDiameter_ = std::clamp(requestedTarget,
			kSpeedEraserMinimumDiameterPx, kSpeedEraserMaximumDiameterPx);

		if (targetDiameter_ >= currentDiameter_)
		{
			decreaseCandidate_ = false;
			waitAtMiddle_ = false;
			middleReachedTime_ = 0.0;
			currentDiameter_ = std::min(targetDiameter_, currentDiameter_ +
				static_cast<float>(900.0 * deltaSeconds));
			return currentDiameter_;
		}

		if (currentDiameter_ - targetDiameter_ < 4.0f && !decreaseCandidate_)
		{
			if (targetDiameter_ <= kSpeedEraserMinimumDiameterPx + 0.05f &&
				MotionDistance(timeSeconds) <= 0.0001f)
			{
				// 小于 4px 的滞回残差不进入候选；运动窗清空后仍按 release 速率归零，
				// 避免 20–24px 的轻微 attack 永久维持 Hover 动画。
				currentDiameter_ = std::max(targetDiameter_, currentDiameter_ -
					static_cast<float>(500.0 * deltaSeconds));
				return currentDiameter_;
			}
			targetDiameter_ = currentDiameter_;
			return currentDiameter_;
		}
		const bool coherentDecreaseMotion = coherentMotionUpdate &&
			MotionDistance(timeSeconds) >= 0.75f &&
			hasReliableDirection_ && !reverseCandidate_;
		if (!decreaseCandidate_)
		{
			decreaseCandidate_ = true;
			decreaseCandidateTime_ = timeSeconds;
			decreaseCandidateCoherent_ = coherentDecreaseMotion;
			return currentDiameter_;
		}
		// 4px 只控制是否进入减小候选；一旦进入便必须能释放到真实目标，
		// 否则会永久停在 20–24px 并让 Hover 绘制线程持续唤醒。
		decreaseCandidateCoherent_ = decreaseCandidateCoherent_ || coherentDecreaseMotion;

		const float resistance = std::clamp(
			(currentDiameter_ - kSpeedEraserNormalDiameterPx) /
			(kSpeedEraserMaximumDiameterPx - kSpeedEraserNormalDiameterPx), 0.0f, 1.0f);
		const double confirmationSeconds =
			(decreaseCandidateCoherent_ ? 0.060 : 0.110) + 0.030 * resistance;
		const double releaseStart = decreaseCandidateTime_ + confirmationSeconds;
		const double releaseDelta = std::max(0.0,
			timeSeconds - std::max(previousAdvanceTime, releaseStart));
		if (releaseDelta <= 0.0) return currentDiameter_;

		float effectiveTarget = targetDiameter_;
		if (currentDiameter_ > kSpeedEraserNormalDiameterPx + 0.01f &&
			targetDiameter_ < kSpeedEraserNormalDiameterPx)
		{
			effectiveTarget = kSpeedEraserNormalDiameterPx;
			waitAtMiddle_ = true;
			middleReachedTime_ = 0.0;
		}
		else if (waitAtMiddle_ && targetDiameter_ < kSpeedEraserNormalDiameterPx)
		{
			if (middleReachedTime_ <= 0.0) middleReachedTime_ = timeSeconds;
			if (timeSeconds - middleReachedTime_ < 0.060)
				effectiveTarget = kSpeedEraserNormalDiameterPx;
			else
				waitAtMiddle_ = false;
		}
		else if (targetDiameter_ >= kSpeedEraserNormalDiameterPx)
		{
			waitAtMiddle_ = false;
			middleReachedTime_ = 0.0;
		}

		currentDiameter_ = std::max(effectiveTarget, currentDiameter_ -
			static_cast<float>(500.0 * releaseDelta));
		if (waitAtMiddle_ && currentDiameter_ <= kSpeedEraserNormalDiameterPx + 0.01f)
		{
			currentDiameter_ = kSpeedEraserNormalDiameterPx;
			if (middleReachedTime_ <= 0.0) middleReachedTime_ = timeSeconds;
		}
		return currentDiameter_;
	}

	float SpeedEraserOcController::UpdatePosition(float positionX, float positionY,
		double timeSeconds, float dpiScale) noexcept
	{
		if (!initialized_)
		{
			Reset(positionX, positionY, timeSeconds);
			return currentDiameter_;
		}
		if (paused_) return currentDiameter_;
		if (!std::isfinite(positionX) || !std::isfinite(positionY) ||
			!std::isfinite(timeSeconds) || !std::isfinite(dpiScale) || dpiScale <= 0.0f)
			return Advance(timeSeconds);
		timeSeconds = std::max(timeSeconds, acceptedTime_);
		const float deltaX = (positionX - acceptedPositionX_) / dpiScale;
		const float deltaY = (positionY - acceptedPositionY_) / dpiScale;
		const float distanceDip = std::hypot(deltaX, deltaY);
		if (distanceDip < 0.25f) return StepDiameter(timeSeconds);

		const float previousWindowDistance = MotionDistance(timeSeconds);
		MotionSegment segment;
		segment.startTime = acceptedTime_;
		segment.endTime = timeSeconds;
		segment.distanceDip = distanceDip;
		segment.directionX = deltaX / distanceDip;
		segment.directionY = deltaY / distanceDip;
		acceptedPositionX_ = positionX;
		acceptedPositionY_ = positionY;
		acceptedTime_ = timeSeconds;
		totalTravelDip_ += distanceDip;
		AddSegment(segment);
		UpdateDirection(segment, previousWindowDistance, timeSeconds);
		return StepDiameter(timeSeconds, true);
	}

	float SpeedEraserOcController::Advance(double timeSeconds) noexcept
	{
		return StepDiameter(timeSeconds);
	}

	void SpeedEraserOcController::ShiftTimeBase(double deltaSeconds) noexcept
	{
		if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return;
		for (size_t offset = 0; offset < segmentCount_; ++offset)
		{
			MotionSegment& segment = segments_[(segmentStart_ + offset) % kSegmentCapacity];
			segment.startTime += deltaSeconds;
			segment.endTime += deltaSeconds;
		}
		acceptedTime_ += deltaSeconds;
		lastAdvanceTime_ += deltaSeconds;
		if (decreaseCandidateTime_ > 0.0) decreaseCandidateTime_ += deltaSeconds;
		if (middleReachedTime_ > 0.0) middleReachedTime_ += deltaSeconds;
		if (reverseCandidateTime_ > 0.0) reverseCandidateTime_ += deltaSeconds;
		if (turnGuardUntil_ > 0.0) turnGuardUntil_ += deltaSeconds;
		if (turnGuardHardDeadline_ > 0.0) turnGuardHardDeadline_ += deltaSeconds;
	}

	void SpeedEraserOcController::PauseForReconnect(double timeSeconds) noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(timeSeconds)) return;
		StepDiameter(timeSeconds);
		paused_ = true;
		pauseTime_ = std::max(timeSeconds, lastAdvanceTime_);
	}

	float SpeedEraserOcController::ResumeFromReconnect(float positionX,
		float positionY, double timeSeconds, float dpiScale) noexcept
	{
		if (!std::isfinite(positionX) || !std::isfinite(positionY) ||
			!std::isfinite(timeSeconds) || !std::isfinite(dpiScale) || dpiScale <= 0.0f)
			return currentDiameter_; // 非法桥接输入不得污染暂停态和内部时间基准。
		if (!initialized_)
		{
			Reset(positionX, positionY, timeSeconds);
			return currentDiameter_;
		}
		const double resumeTime = std::max(
			timeSeconds, std::max(lastAdvanceTime_, acceptedTime_));
		if (paused_)
		{
			ShiftTimeBase(std::max(0.0, resumeTime - pauseTime_));
			paused_ = false;
			pauseTime_ = 0.0;
		}
		// 桥接段以极短合成时长进入路程窗，保留状态且不把断触间隙误判为停笔。
		acceptedTime_ = resumeTime - 0.000001;
		return UpdatePosition(positionX, positionY, resumeTime, dpiScale);
	}

	bool SpeedEraserOcController::NeedsAnimation(double timeSeconds) const noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(timeSeconds)) return false;
		if (currentDiameter_ > kSpeedEraserMinimumDiameterPx + 0.05f ||
			std::abs(targetDiameter_ - currentDiameter_) > 0.05f || decreaseCandidate_ ||
			reverseCandidate_ || waitAtMiddle_ || turnGuardUntil_ > timeSeconds) return true;
		for (size_t offset = 0; offset < segmentCount_; ++offset)
		{
			const MotionSegment& segment =
				segments_[(segmentStart_ + offset) % kSegmentCapacity];
			if (segment.endTime + 0.064 > timeSeconds) return true;
		}
		return false;
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
			ApplyLiveTipTaper(output, liveTipTaperSeconds);
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

	float InterpolateSpeedEraserDiameter(
		const SpeedEraserWidthInterval& interval, double pointTimeSeconds) noexcept
	{
		const float startDiameter = std::isfinite(interval.startDiameter)
			? std::clamp(interval.startDiameter, kSpeedEraserMinimumDiameterPx,
				kSpeedEraserMaximumDiameterPx)
			: kSpeedEraserMinimumDiameterPx;
		const float endDiameter = std::isfinite(interval.endDiameter)
			? std::clamp(interval.endDiameter, kSpeedEraserMinimumDiameterPx,
				kSpeedEraserMaximumDiameterPx)
			: startDiameter;
		if (!std::isfinite(pointTimeSeconds) ||
			!std::isfinite(interval.startTimeSeconds) ||
			!std::isfinite(interval.endTimeSeconds) ||
			interval.endTimeSeconds <= interval.startTimeSeconds)
			return endDiameter;
		const float ratio = static_cast<float>(std::clamp(
			(pointTimeSeconds - interval.startTimeSeconds) /
			(interval.endTimeSeconds - interval.startTimeSeconds), 0.0, 1.0));
		return LerpFloat(startDiameter, endDiameter, ratio);
	}

	void AppendNewModeledPoints(ActiveStroke& stroke, float inputSpeed,
		const SpeedEraserWidthInterval* speedEraserWidth)
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
			case StrokeWidthMode::SpeedEraser:
			{
				const float diameter = speedEraserWidth
					? InterpolateSpeedEraserDiameter(
						*speedEraserWidth, result.time.Value())
					: stroke.widthEstimator.baseDiameter;
				point = { result.position.x, result.position.y, diameter * 0.5f,
					static_cast<float>(result.time.Value()) };
				break;
			}
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
