#include "Draw3.SpeedEraser.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Inkeys::Drawing::Draw3::SpeedEraser
{
	namespace
	{
		double Positive(double value, double fallback) noexcept
		{
			return std::isfinite(value) && value > 0.0 ? value : fallback;
		}

		double SmoothStep(double value) noexcept
		{
			value = std::clamp(value, 0.0, 1.0);
			return value * value * (3.0 - 2.0 * value);
		}

		// 比例限速段与指数段的精确解，长帧不会越过目标或丢失经过的时间。
		double Follow(double current, double target, double seconds,
			double tau, double rate) noexcept
		{
			const double gap = std::abs(target - current);
			const double limit = tau * rate;
			const double limitedTime = gap > limit ? (gap - limit) / rate : 0.0;
			const double spent = std::min(seconds, limitedTime);
			const double remainder = (gap - rate * spent) *
				std::exp(-(seconds - spent) / tau);
			return target + (current > target ? remainder : -remainder);
		}
	}

	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, bool touch) noexcept
	{
		Config config;
		config.display = display;
		config.mode = mode;
		const bool large = mode == DeviceMode::LargeScreen;
		const double dipX = Positive(display.dipPerPixelX, 1.0);
		const double dipY = Positive(display.dipPerPixelY, 1.0);
		const bool physical = display.physicalAvailable &&
			std::isfinite(display.cmPerPixelX) && display.cmPerPixelX > 0.0f &&
			std::isfinite(display.cmPerPixelY) && display.cmPerPixelY > 0.0f;
		const bool physicalMotion = physical && touch && display.directTouchMapped;
		config.motionSource = physicalMotion ? ScaleSource::Physical : ScaleSource::Dip;
		config.coverageSource = physical ? ScaleSource::Physical : ScaleSource::Dip;
		config.motionPerPixelX = static_cast<float>(physicalMotion ? display.cmPerPixelX : dipX);
		config.motionPerPixelY = static_cast<float>(physicalMotion ? display.cmPerPixelY : dipY);
		// 圆形几何使用面积等价密度；运动距离仍分别换算 X/Y。
		const double coverageUnitPerPixel = physical
			? std::sqrt(static_cast<double>(display.cmPerPixelX) * display.cmPerPixelY)
			: std::sqrt(dipX * dipY);
		config.minimumDiameterPx = static_cast<float>(
			(physical ? (large ? 1.0 : 0.4) : (large ? 24.0 : 16.0)) / coverageUnitPerPixel);
		config.maximumDiameterPx = static_cast<float>(
			(physical ? (large ? 12.0 : 4.0) : (large ? 288.0 : 160.0)) / coverageUnitPerPixel);
		config.minimumSpeed = physicalMotion ? 2.0f : large ? 40.0f : 30.0f;
		config.maximumSpeed = physicalMotion ? 60.0f : large ? 900.0f : 700.0f;
		config.touchUnlockStart = physicalMotion ? 0.1f : 2.0f;
		config.touchUnlockEnd = physicalMotion ? 0.3f : 6.0f;
		return config;
	}

	void Controller::Reset(float x, float y, double seconds, StartKind kind,
		const Config& config) noexcept
	{
		config_ = config;
		config_.motionPerPixelX = static_cast<float>(Positive(config_.motionPerPixelX, 1.0));
		config_.motionPerPixelY = static_cast<float>(Positive(config_.motionPerPixelY, 1.0));
		config_.minimumDiameterPx = static_cast<float>(Positive(config_.minimumDiameterPx, 16.0));
		config_.maximumDiameterPx = std::max(config_.minimumDiameterPx,
			static_cast<float>(Positive(config_.maximumDiameterPx, 160.0)));
		config_.minimumSpeed = static_cast<float>(Positive(config_.minimumSpeed, 30.0));
		config_.maximumSpeed = std::max(config_.minimumSpeed * 1.01f,
			static_cast<float>(Positive(config_.maximumSpeed, 700.0)));
		config_.touchUnlockStart = static_cast<float>(Positive(config_.touchUnlockStart, 2.0));
		config_.touchUnlockEnd = std::max(config_.touchUnlockStart * 1.01f,
			static_cast<float>(Positive(config_.touchUnlockEnd, 6.0)));
		config_.historyWindowSeconds = Positive(config_.historyWindowSeconds, 0.080);
		config_.accelerationWindowSeconds = Positive(config_.accelerationWindowSeconds, 0.040);
		config_.referenceWindowSeconds = Positive(config_.referenceWindowSeconds, 0.160);
		config_.holdSeconds = Positive(config_.holdSeconds, 0.180);
		config_.decreaseConfirmationSeconds = Positive(config_.decreaseConfirmationSeconds, 0.180);
		config_.decreaseRatio = std::clamp(Positive(config_.decreaseRatio, 0.08), 0.001, 0.5);
		config_.growthTauSeconds = Positive(config_.growthTauSeconds, 0.140);
		config_.acceleratedGrowthTauSeconds = Positive(config_.acceleratedGrowthTauSeconds, 0.080);
		config_.shrinkTauSeconds = Positive(config_.shrinkTauSeconds, 0.240);
		config_.maximumLogGrowthPerSecond = Positive(config_.maximumLogGrowthPerSecond, 6.0);
		config_.maximumLogShrinkPerSecond = Positive(config_.maximumLogShrinkPerSecond, 3.0);
		config_.settleLogTolerance = Positive(config_.settleLogTolerance, 0.003);
		segments_.fill({});
		segmentCount_ = 0;
		acceptedX_ = downX_ = std::isfinite(x) ? x : 0.0;
		acceptedY_ = downY_ = std::isfinite(y) ? y : 0.0;
		acceptedTime_ = std::isfinite(seconds) ? seconds : 0.0;
		sampleState_ = {};
		sampleState_.time = acceptedTime_;
		sampleState_.logDiameter = sampleState_.logTarget = std::log(config_.minimumDiameterPx);
		frameState_ = sampleState_;
		pauseTime_ = 0.0;
		touchStartup_ = kind == StartKind::Touch;
		initialized_ = true;
		paused_ = false;
	}

	void Controller::AddSegment(const MotionSegment& segment) noexcept
	{
		const double retention = std::max({ config_.historyWindowSeconds,
			config_.accelerationWindowSeconds, config_.referenceWindowSeconds });
		// 保留本次积分起点需要的历史，而非提前按新样本终点删掉旧区间。
		const double cutoff = sampleState_.time - retention;
		size_t expired = 0;
		while (expired < segmentCount_ && segments_[expired].endTime <= cutoff) ++expired;
		if (expired)
		{
			for (size_t i = expired; i < segmentCount_; ++i) segments_[i - expired] = segments_[i];
			segmentCount_ -= expired;
		}
		if (segmentCount_ && segments_[0].startTime < cutoff)
		{
			auto& first = segments_[0];
			first.distance *= (first.endTime - cutoff) / (first.endTime - first.startTime);
			first.startTime = cutoff;
		}
		if (segmentCount_ == kSegmentCapacity)
		{
			// 合并最短的相邻区间；不能反复延长同一旧段，让过期高速路程拖尾。
			size_t merge = 0;
			for (size_t i = 1; i + 1 < segmentCount_; ++i)
				if (segments_[i + 1].endTime - segments_[i].startTime <
					segments_[merge + 1].endTime - segments_[merge].startTime)
					merge = i;
			segments_[merge].endTime = segments_[merge + 1].endTime;
			segments_[merge].distance += segments_[merge + 1].distance;
			for (size_t i = merge + 2; i < segmentCount_; ++i) segments_[i - 1] = segments_[i];
			--segmentCount_;
		}
		segments_[segmentCount_++] = segment;
	}

	double Controller::MotionSpeed(double seconds, double windowSeconds) const noexcept
	{
		double distance = 0.0;
		for (size_t i = 0; i < segmentCount_; ++i)
		{
			const auto& segment = segments_[i];
			const double start = std::max(segment.startTime, seconds - windowSeconds);
			const double end = std::min(segment.endTime, seconds);
			if (end > start && segment.endTime > segment.startTime)
				distance += segment.distance * (end - start) / (segment.endTime - segment.startTime);
		}
		return distance / windowSeconds;
	}

	double Controller::TargetLogDiameter(double speed, double maximumDisplacement) const noexcept
	{
		double amount = SmoothStep(std::log(std::max(speed, static_cast<double>(config_.minimumSpeed)) /
			config_.minimumSpeed) / std::log(config_.maximumSpeed / config_.minimumSpeed));
		if (touchStartup_)
			amount = std::min(amount, SmoothStep((maximumDisplacement - config_.touchUnlockStart) /
				(config_.touchUnlockEnd - config_.touchUnlockStart)));
		const double minimum = std::log(config_.minimumDiameterPx);
		return minimum + amount * std::log(config_.maximumDiameterPx / config_.minimumDiameterPx);
	}

	void Controller::FollowTarget(DynamicsState& state, double endTime,
		double target, double realMotionSpeed, double growthTau) const noexcept
	{
		const double startTime = state.time;
		state.time = endTime;
		state.logTarget = target;
		const double minimum = std::log(config_.minimumDiameterPx);
		const double difference = target - state.logDiameter;
		const bool minimumTarget = target <= minimum + 1e-9;
		const bool smaller = difference <= std::log(1.0 - config_.decreaseRatio) || minimumTarget;
		if (!smaller && realMotionSpeed > config_.minimumSpeed &&
			target > minimum + 0.5 * std::log(config_.maximumDiameterPx / config_.minimumDiameterPx))
			state.holdUntil = endTime + config_.holdSeconds;

		if (difference >= 0.0)
		{
			state.decreasePending = state.shrinking = false;
			state.logDiameter = Follow(state.logDiameter, target, endTime - startTime,
				growthTau, config_.maximumLogGrowthPerSecond);
		}
		else
		{
			if (!smaller && !state.shrinking)
			{
				state.decreasePending = false;
				return;
			}
			if (!state.decreasePending)
			{
				state.decreasePending = true;
				state.decreaseSince = startTime;
			}
			// 两个期限同时推进，不在保持结束后重新计一次精擦确认。
			const double releaseStart = std::max(state.holdUntil,
				state.decreaseSince + config_.decreaseConfirmationSeconds);
			const double elapsed = endTime - std::max(startTime, releaseStart);
			if (elapsed <= 0.0) return;
			state.shrinking = true;
			state.logDiameter = Follow(state.logDiameter, target, elapsed,
				config_.shrinkTauSeconds, config_.maximumLogShrinkPerSecond);
		}
		if (std::abs(state.logDiameter - target) <= config_.settleLogTolerance)
		{
			state.logDiameter = target;
			state.decreasePending = state.shrinking = false;
		}
	}

	void Controller::AdvanceState(DynamicsState& state, double seconds,
		const MotionSegment* incoming, double incomingX, double incomingY) const noexcept
	{
		const double retention = std::max({ config_.historyWindowSeconds,
			config_.referenceWindowSeconds, config_.accelerationWindowSeconds });
		const double historyEnd = segmentCount_ ? segments_[segmentCount_ - 1].endTime + retention : state.time;
		while (state.time < seconds)
		{
			if (!incoming && state.time >= historyEnd)
			{
				// 历史耗尽后目标恒为最小值，长停顿用闭式解推进而非逐帧补算。
				FollowTarget(state, seconds, std::log(config_.minimumDiameterPx), 0.0, config_.growthTauSeconds);
				break;
			}
			const double end = std::min(seconds, state.time + 0.004);
			if (end <= state.time) { state.time = seconds; break; }
			const double midpoint = (state.time + end) * 0.5;
			if (incoming)
			{
				const double fraction = std::clamp((end - incoming->startTime) /
					(incoming->endTime - incoming->startTime), 0.0, 1.0);
				const double x = acceptedX_ + fraction * (incomingX - acceptedX_);
				const double y = acceptedY_ + fraction * (incomingY - acceptedY_);
				state.maximumDisplacement = std::max(state.maximumDisplacement,
					std::hypot((x - downX_) * config_.motionPerPixelX,
						(y - downY_) * config_.motionPerPixelY));
			}
			const double speed = MotionSpeed(midpoint, config_.historyWindowSeconds);
			const double fast = MotionSpeed(midpoint, config_.accelerationWindowSeconds);
			const double reference = MotionSpeed(midpoint, config_.referenceWindowSeconds);
			const bool accelerating = fast > std::max(config_.minimumSpeed * 2.0, reference * 1.5);
			FollowTarget(state, end, TargetLogDiameter(speed, state.maximumDisplacement),
				incoming && incoming->distance > 0.0 ? speed : 0.0,
				accelerating ? config_.acceleratedGrowthTauSeconds : config_.growthTauSeconds);
		}
	}

	float Controller::UpdatePosition(float x, float y, double seconds) noexcept
	{
		if (!initialized_) { Reset(x, y, seconds); return Diameter(); }
		if (paused_ || !std::isfinite(x) || !std::isfinite(y) ||
			!std::isfinite(seconds) || seconds <= sampleState_.time) return Diameter();
		const double duration = seconds - acceptedTime_;
		const double distance = std::hypot((static_cast<double>(x) - acceptedX_) * config_.motionPerPixelX,
			(static_cast<double>(y) - acceptedY_) * config_.motionPerPixelY);
		if (duration > 1.0)
		{
			// 缺失很久的输入不能证明连续运动；释放时间照常推进，但不猜测缺失轨迹。
			AdvanceState(sampleState_, seconds);
		}
		else
		{
			const MotionSegment segment{ acceptedTime_, seconds, distance };
			AddSegment(segment);
			AdvanceState(sampleState_, seconds, &segment, x, y);
		}
		acceptedX_ = x;
		acceptedY_ = y;
		acceptedTime_ = seconds;
		// 帧预览不能反写真实输入锚点；迟到的新 raw 输入从真实状态重新积分。
		frameState_ = sampleState_;
		return Diameter();
	}

	float Controller::Advance(double seconds) noexcept
	{
		if (initialized_ && !paused_ && std::isfinite(seconds) && seconds > frameState_.time)
			AdvanceState(frameState_, seconds);
		return Diameter();
	}

	void Controller::PauseForReconnect(double seconds) noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(seconds)) return;
		AdvanceState(sampleState_, std::max(seconds, sampleState_.time));
		frameState_ = sampleState_;
		pauseTime_ = sampleState_.time;
		paused_ = true;
	}

	float Controller::ResumeFromReconnect(float x, float y, double seconds) noexcept
	{
		if (!initialized_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(seconds) ||
			seconds < sampleState_.time) return Diameter();
		const double gap = seconds - (paused_ ? pauseTime_ : sampleState_.time);
		for (size_t i = 0; i < segmentCount_; ++i)
		{
			segments_[i].startTime += gap;
			segments_[i].endTime += gap;
		}
		sampleState_.time += gap;
		sampleState_.holdUntil += gap;
		if (sampleState_.decreasePending) sampleState_.decreaseSince += gap;
		// 平移落点基准以排除断点距离，避免缺失段解锁 Touch 起步限制。
		downX_ += static_cast<double>(x) - acceptedX_;
		downY_ += static_cast<double>(y) - acceptedY_;
		acceptedX_ = x;
		acceptedY_ = y;
		acceptedTime_ = seconds;
		frameState_ = sampleState_;
		paused_ = false;
		return Diameter();
	}

	float Controller::Diameter() const noexcept
	{
		return initialized_ ? static_cast<float>(std::exp(frameState_.logDiameter)) : config_.minimumDiameterPx;
	}

	float Controller::TargetDiameter() const noexcept
	{
		return initialized_ ? static_cast<float>(std::exp(frameState_.logTarget)) : config_.minimumDiameterPx;
	}

	bool Controller::NeedsAnimation(double seconds) const noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(seconds)) return false;
		return std::abs(frameState_.logDiameter - std::log(config_.minimumDiameterPx)) > 1e-9 ||
			(segmentCount_ && seconds < segments_[segmentCount_ - 1].endTime + config_.referenceWindowSeconds);
	}

	float InterpolateDiameter(const WidthInterval& interval, double seconds) noexcept
	{
		const double minimum = Positive(interval.minimumDiameter, 0.001);
		const double maximum = std::max(minimum, Positive(interval.maximumDiameter, minimum));
		const double start = std::clamp(Positive(interval.startDiameter, minimum), minimum, maximum);
		const double end = std::clamp(Positive(interval.endDiameter, minimum), minimum, maximum);
		if (!std::isfinite(seconds) || !std::isfinite(interval.startTimeSeconds) ||
			!std::isfinite(interval.endTimeSeconds) || interval.endTimeSeconds <= interval.startTimeSeconds)
			return static_cast<float>(end);
		const double amount = std::clamp((seconds - interval.startTimeSeconds) /
			(interval.endTimeSeconds - interval.startTimeSeconds), 0.0, 1.0);
		return static_cast<float>(start + amount * (end - start));
	}

	float ContactDiameter(float acceptedRadius, float fallbackDiameter) noexcept
	{
		const double diameter = static_cast<double>(acceptedRadius) * 2.0;
		return std::isfinite(diameter) && diameter > 0.0 && diameter <= std::numeric_limits<float>::max()
			? static_cast<float>(diameter) : static_cast<float>(Positive(fallbackDiameter, 0.001));
	}
}
