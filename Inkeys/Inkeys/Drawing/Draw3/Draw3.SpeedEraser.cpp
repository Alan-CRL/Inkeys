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


	float DiameterToCanvasPx(float diameterDip, const DisplayScale& display) noexcept
	{
		const double unitsPerPixel = std::sqrt(Positive(display.dipPerPixelX,1.0) *
			Positive(display.dipPerPixelY,1.0));
		return static_cast<float>(Positive(diameterDip,32.0) / unitsPerPixel);
	}

	float FixedDiameterPx(float selectedDiameterDip, const DisplayScale& display) noexcept
	{
		return DiameterToCanvasPx(selectedDiameterDip,display);
	}

	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, bool touch, const EraserSizes& sizes) noexcept
	{
		Config config;
		config.display = display;
		config.mode = mode;
		config.sizes = sizes;
		config.sizes.minimumDiameterDip = static_cast<float>(Positive(sizes.minimumDiameterDip,16.0));
		config.sizes.maximumDiameterDip = std::max(config.sizes.minimumDiameterDip,
			static_cast<float>(Positive(sizes.maximumDiameterDip,160.0)));
		config.sizes.standardDiameterDip = std::clamp(static_cast<float>(Positive(sizes.standardDiameterDip,32.0)),
			config.sizes.minimumDiameterDip,config.sizes.maximumDiameterDip);
		config.sizes.touchStartDiameterDip = std::clamp(static_cast<float>(Positive(sizes.touchStartDiameterDip,16.0)),
			config.sizes.minimumDiameterDip,config.sizes.standardDiameterDip);
		config.sizes.fixedDiameterDip = static_cast<float>(Positive(sizes.fixedDiameterDip,50.0));
		// 尺寸只从DIP转换。EDID只决定动作单位，不接触任何尺寸配置。
		config.minimumDiameterPx = DiameterToCanvasPx(config.sizes.minimumDiameterDip,display);
		config.maximumDiameterPx = DiameterToCanvasPx(config.sizes.maximumDiameterDip,display);
		const bool physicalMotion = touch && display.directTouchMapped && display.physicalAvailable &&
			std::isfinite(display.cmPerPixelX) && display.cmPerPixelX > 0 &&
			std::isfinite(display.cmPerPixelY) && display.cmPerPixelY > 0;
		config.motionSource = physicalMotion ? ScaleSource::Physical : ScaleSource::Dip;
		config.motionPerPixelX = static_cast<float>(physicalMotion ? display.cmPerPixelX : Positive(display.dipPerPixelX,1));
		config.motionPerPixelY = static_cast<float>(physicalMotion ? display.cmPerPixelY : Positive(display.dipPerPixelY,1));
		const bool large = mode == DeviceMode::LargeScreen;
		config.sweepEnterSpeed = physicalMotion ? 25.0f : large ? 650.0f : 800.0f;
		config.sweepExitSpeed = physicalMotion ? 18.0f : large ? 450.0f : 600.0f;
		config.largeTargetSpeed = physicalMotion ? 70.0f : large ? 1700.0f : 1900.0f;
		config.movementNoiseDistance = physicalMotion ? 0.02f : 0.75f;
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
		const auto resolved=ResolveConfig(config.display,config.mode,kind==StartKind::Touch,config.sizes);
		config_.sizes=resolved.sizes;
		config_.minimumDiameterPx=resolved.minimumDiameterPx;
		config_.maximumDiameterPx=resolved.maximumDiameterPx;
		config_.sweepEnterSpeed=static_cast<float>(Positive(config.sweepEnterSpeed,resolved.sweepEnterSpeed));
		config_.sweepExitSpeed=std::min(config_.sweepEnterSpeed*0.99f,static_cast<float>(Positive(config.sweepExitSpeed,resolved.sweepExitSpeed)));
		config_.largeTargetSpeed=std::max(config_.sweepEnterSpeed+1.0f,static_cast<float>(Positive(config.largeTargetSpeed,resolved.largeTargetSpeed)));
		config_.movementNoiseDistance=static_cast<float>(Positive(config.movementNoiseDistance,resolved.movementNoiseDistance));
		config_.touchUnlockStart = static_cast<float>(Positive(config_.touchUnlockStart, 2.0));
		config_.touchUnlockEnd = std::max(config_.touchUnlockStart * 1.01f,
			static_cast<float>(Positive(config_.touchUnlockEnd, 6.0)));
		config_.historyWindowSeconds = Positive(config_.historyWindowSeconds, 0.080);
		const Config defaults;
		config_.referenceWindowSeconds = Positive(config_.referenceWindowSeconds, defaults.referenceWindowSeconds);
		config_.evidenceStartSeconds = Positive(config_.evidenceStartSeconds, defaults.evidenceStartSeconds);
		config_.evidenceFullSeconds = Positive(config_.evidenceFullSeconds, defaults.evidenceFullSeconds);
		config_.evidenceDecaySeconds = Positive(config_.evidenceDecaySeconds, defaults.evidenceDecaySeconds);
		config_.maximumEvidenceIntervalSeconds = Positive(config_.maximumEvidenceIntervalSeconds, defaults.maximumEvidenceIntervalSeconds);
		config_.holdSeconds = Positive(config_.holdSeconds, defaults.holdSeconds);
		config_.sweepHoldSeconds = Positive(config_.sweepHoldSeconds, defaults.sweepHoldSeconds);
		config_.decreaseConfirmationSeconds = Positive(config_.decreaseConfirmationSeconds, defaults.decreaseConfirmationSeconds);
		config_.sweepDecreaseConfirmationSeconds = Positive(config_.sweepDecreaseConfirmationSeconds, defaults.sweepDecreaseConfirmationSeconds);
		config_.growthTauSeconds = Positive(config_.growthTauSeconds, defaults.growthTauSeconds);
		config_.largeGrowthTauSeconds = Positive(config_.largeGrowthTauSeconds, defaults.largeGrowthTauSeconds);
		config_.shrinkTauSeconds = Positive(config_.shrinkTauSeconds, defaults.shrinkTauSeconds);
		config_.sweepShrinkTauSeconds = Positive(config_.sweepShrinkTauSeconds, defaults.sweepShrinkTauSeconds);
		config_.maximumLogGrowthPerSecond = Positive(config_.maximumLogGrowthPerSecond, defaults.maximumLogGrowthPerSecond);
		config_.largeLogGrowthPerSecond = Positive(config_.largeLogGrowthPerSecond, defaults.largeLogGrowthPerSecond);
		config_.maximumLogShrinkPerSecond = Positive(config_.maximumLogShrinkPerSecond, defaults.maximumLogShrinkPerSecond);
		config_.sweepLogShrinkPerSecond = Positive(config_.sweepLogShrinkPerSecond, defaults.sweepLogShrinkPerSecond);
		config_.mouseReleaseSeconds = Positive(config_.mouseReleaseSeconds, defaults.mouseReleaseSeconds);
		config_.settleLogTolerance = Positive(config_.settleLogTolerance, defaults.settleLogTolerance);
		config_.evidenceFullSeconds = std::max(config_.evidenceStartSeconds + 0.001, config_.evidenceFullSeconds);
		config_.idleStartSeconds=Positive(config.idleStartSeconds,defaults.idleStartSeconds);
		config_.idleTauSeconds=Positive(config.idleTauSeconds,defaults.idleTauSeconds);
		config_.idleLogShrinkPerSecond=Positive(config.idleLogShrinkPerSecond,defaults.idleLogShrinkPerSecond);
		config_.sweepEntryFraction = std::clamp(Positive(config_.sweepEntryFraction, defaults.sweepEntryFraction), 0.01, 1.0);
		config_.sweepExitFraction = std::clamp(Positive(config_.sweepExitFraction, defaults.sweepExitFraction), 0.0, config_.sweepEntryFraction - 0.001);
		config_.decreaseRatio = std::clamp(Positive(config_.decreaseRatio, 0.08), 0.001, 0.5);
		segments_.fill({});
		segmentCount_ = 0;
		acceptedX_ = downX_ = movementX_ = std::isfinite(x) ? x : 0.0;
		acceptedY_ = downY_ = movementY_ = std::isfinite(y) ? y : 0.0;
		acceptedTime_ = std::isfinite(seconds) ? seconds : 0.0;
		sampleState_ = {};
		sampleState_.time = acceptedTime_;
		sampleState_.lastMovementTime=acceptedTime_;
		sampleState_.logDiameter = sampleState_.logTarget = std::log(kind==StartKind::Touch ? config_.sizes.touchStartDiameterDip : config_.sizes.standardDiameterDip);
		frameState_ = sampleState_;
		pauseTime_ = 0.0;
		touchStartup_ = kind == StartKind::Touch;
		initialized_ = true;
		paused_ = false;
	}

	void Controller::AddSegment(const MotionSegment& segment) noexcept
	{
		const double retention = std::max(config_.historyWindowSeconds, config_.referenceWindowSeconds);
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


	double Controller::IdleDiameterDip(const DynamicsState& state) const noexcept
	{
		return touchStartup_ && state.maximumDisplacement < config_.touchUnlockEnd
			? config_.sizes.touchStartDiameterDip : config_.sizes.standardDiameterDip;
	}

	double Controller::TargetLogDiameter(double speed, double maximumDisplacement) const noexcept
	{
		const double ordinary = std::log(config_.sizes.standardDiameterDip);
		if (touchStartup_ && maximumDisplacement < config_.touchUnlockEnd)
		{
			const double amount = SmoothStep((maximumDisplacement - config_.touchUnlockStart) /
				(config_.touchUnlockEnd - config_.touchUnlockStart));
			const double start = std::log(config_.sizes.touchStartDiameterDip);
			return start + amount * (ordinary-start);
		}
		const double amount = SmoothStep((speed-config_.sweepEnterSpeed) /
			(config_.largeTargetSpeed-config_.sweepEnterSpeed));
		return ordinary + amount * std::log(config_.sizes.maximumDiameterDip / config_.sizes.standardDiameterDip);
	}


	void Controller::FollowTarget(DynamicsState& state, double endTime,
		double target, double realMotionSpeed) const noexcept
	{
		const double startTime=state.time, dt=endTime-startTime;
		state.time=endTime;
		state.logTarget=target;
		const double standard=std::log(config_.sizes.standardDiameterDip);
		const double logRange=std::log(config_.sizes.maximumDiameterDip/config_.sizes.standardDiameterDip);
		if (realMotionSpeed >= config_.sweepEnterSpeed) state.sweepQualified=true;
		else if (realMotionSpeed > 0 && realMotionSpeed < config_.sweepExitSpeed) state.sweepQualified=false;
		const bool qualifies=state.sweepQualified && realMotionSpeed >= config_.sweepEnterSpeed;
		const double strength=qualifies ? 0.4 + 0.6*SmoothStep((realMotionSpeed-config_.sweepEnterSpeed)/
			(config_.largeTargetSpeed-config_.sweepEnterSpeed)) : 0.0;
		const double leak=std::exp(-dt/config_.evidenceDecaySeconds);
		// 始终泄漏；低于进入速度的普通动作，无论持续多久都不能充满证据。
		state.sweepEvidence=std::min(config_.evidenceFullSeconds,
			state.sweepEvidence*leak + strength*config_.evidenceDecaySeconds*(1-leak));
		const double idleStart=state.lastMovementTime+config_.idleStartSeconds;
		if (endTime >= idleStart)
		{
			const double elapsed=endTime-std::max(startTime,idleStart);
			target=std::log(IdleDiameterDip(state));
			state.logTarget=target;
			state.sweepQualified=false;
			state.decreasePending=state.shrinking=false;
			state.logDiameter=Follow(state.logDiameter,target,elapsed,config_.idleTauSeconds,config_.idleLogShrinkPerSecond);
			if (std::abs(state.logDiameter-target)<=config_.settleLogTolerance) state.logDiameter=target;
			if (state.logDiameter<=standard+config_.settleLogTolerance) state.sweeping=false;
			return;
		}
		const double range=config_.sizes.maximumDiameterDip-config_.sizes.standardDiameterDip;
		const double size=range>0 ? std::clamp((std::exp(state.logDiameter)-config_.sizes.standardDiameterDip)/range,0.0,1.0) : 0;
		if (!state.sweeping && size>=config_.sweepEntryFraction && state.sweepEvidence>=config_.evidenceFullSeconds*0.8)
			state.sweeping=true;
		if (state.sweeping && size<=config_.sweepExitFraction) state.sweeping=false;
		const double resistance=state.sweeping ? SmoothStep((size-0.2)/0.6) : 0.0;
		const auto blend=[](double a,double b,double t){return a+(b-a)*t;};
		const double difference=target-state.logDiameter;
		const bool smaller=difference<=std::log(1-config_.decreaseRatio) || target<=standard+1e-9;
		if (!smaller && qualifies)
			state.holdUntil=endTime+blend(config_.holdSeconds,config_.sweepHoldSeconds,resistance);
		if (difference>=0)
		{
			state.decreasePending=state.shrinking=false;
			double permitted=target;
			if (target>standard)
			{
				const double permission=SmoothStep((state.sweepEvidence-config_.evidenceStartSeconds)/
					(config_.evidenceFullSeconds-config_.evidenceStartSeconds));
				permitted=std::min(target,standard+permission*logRange);
				if (!qualifies) return;
			}
			else if (realMotionSpeed<=0 && state.maximumDisplacement<config_.touchUnlockEnd) return;
			if (permitted<=state.logDiameter) return;
			const double growth=SmoothStep(size);
			state.logDiameter=Follow(state.logDiameter,permitted,dt,
				blend(config_.growthTauSeconds,config_.largeGrowthTauSeconds,growth),
				blend(config_.maximumLogGrowthPerSecond,config_.largeLogGrowthPerSecond,growth));
			target=permitted;
		}
		else
		{
			if (!smaller && !state.shrinking){state.decreasePending=false;return;}
			if (!state.decreasePending){state.decreasePending=true;state.decreaseSince=startTime;}
			const double confirmation=blend(config_.decreaseConfirmationSeconds,config_.sweepDecreaseConfirmationSeconds,resistance);
			const double releaseStart=std::max(state.holdUntil,state.decreaseSince+confirmation);
			const double elapsed=endTime-std::max(startTime,releaseStart);
			if (elapsed<=0)return;
			state.shrinking=true;
			state.logDiameter=Follow(state.logDiameter,target,elapsed,
				blend(config_.shrinkTauSeconds,config_.sweepShrinkTauSeconds,resistance),
				blend(config_.maximumLogShrinkPerSecond,config_.sweepLogShrinkPerSecond,resistance));
		}
		if(std::abs(state.logDiameter-target)<=config_.settleLogTolerance)
		{state.logDiameter=target;state.decreasePending=state.shrinking=false;}
	}

	void Controller::AdvanceState(DynamicsState& state, double seconds,
		const MotionSegment* incoming, double incomingX, double incomingY, bool effectiveMovement) const noexcept
	{
		const double retention = std::max(config_.historyWindowSeconds, config_.referenceWindowSeconds);
		const double historyEnd = segmentCount_ ? segments_[segmentCount_ - 1].endTime + retention : state.time;
		const double minimum = std::log(IdleDiameterDip(state));
		while (state.time < seconds)
		{
			if (!incoming && state.time >= historyEnd &&
				std::abs(state.logDiameter - minimum) < 1e-9 && state.sweepEvidence < 1e-6)
			{
				state.time = seconds;
				state.logTarget = minimum;
				state.sweepEvidence = 0.0;
				state.sweeping = state.shrinking = state.decreasePending = false;
				break;
			}
			const double end = std::min(seconds, state.time + 0.004);
			if (end <= state.time) { state.time = seconds; break; }
			const double midpoint = (state.time + end) * 0.5;
			if(effectiveMovement && incoming && incoming->endTime-incoming->startTime<=config_.maximumEvidenceIntervalSeconds)
				state.lastMovementTime=end;
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
			const double speed = midpoint < historyEnd ? MotionSpeed(midpoint, config_.historyWindowSeconds) : 0.0;
			// 稀疏的一个跳点不能证明整个空档都在快擦；正常输入仍按真实 dt 累积。
			state.speed=speed;
			const double observedSpeed = effectiveMovement && incoming && incoming->distance > 0.0 &&
				incoming->endTime - incoming->startTime <= config_.maximumEvidenceIntervalSeconds
				? std::min(speed, incoming->distance / (incoming->endTime - incoming->startTime)) : 0.0;
			FollowTarget(state, end, TargetLogDiameter(speed, state.maximumDisplacement), observedSpeed);
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
		const bool effectiveMovement=std::hypot((x-movementX_)*config_.motionPerPixelX,
			(y-movementY_)*config_.motionPerPixelY)>=config_.movementNoiseDistance;
		if (duration > 1.0)
		{
			// 缺失很久的输入不能证明连续运动；释放时间照常推进，但不猜测缺失轨迹。
			AdvanceState(sampleState_, seconds);
		}
		else
		{
			const MotionSegment segment{ acceptedTime_, seconds, distance };
			AddSegment(segment);
			AdvanceState(sampleState_, seconds, &segment, x, y, effectiveMovement);
		}
		if(effectiveMovement){movementX_=x;movementY_=y;sampleState_.lastMovementTime=seconds;}
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
		sampleState_.lastMovementTime += gap;
		sampleState_.holdUntil += gap;
		if (sampleState_.decreasePending) sampleState_.decreaseSince += gap;
		// 平移落点基准以排除断点距离，避免缺失段解锁 Touch 起步限制。
		movementX_ += static_cast<double>(x)-acceptedX_;
		movementY_ += static_cast<double>(y)-acceptedY_;
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
		return DiameterToCanvasPx(DiameterDip(),config_.display);
	}


	float Controller::DiameterDip() const noexcept
	{
		return initialized_ ? static_cast<float>(std::exp(frameState_.logDiameter)) : config_.sizes.standardDiameterDip;
	}
	double Controller::SecondsSinceMovement(double seconds) const noexcept
	{
		return initialized_ ? std::max(0.0,(paused_?pauseTime_:seconds)-frameState_.lastMovementTime) : 0.0;
	}

	float Controller::TargetDiameter() const noexcept
	{
		return DiameterToCanvasPx(initialized_ ? static_cast<float>(std::exp(frameState_.logTarget)) : config_.sizes.standardDiameterDip,config_.display);
	}

	bool Controller::NeedsAnimation(double seconds) const noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(seconds)) return false;
		return std::abs(frameState_.logDiameter - std::log(IdleDiameterDip(frameState_))) > 1e-9 ||
			(segmentCount_ && seconds < segments_[segmentCount_ - 1].endTime + config_.referenceWindowSeconds);
	}


	void MouseLifecycle::Configure(const Config& config) noexcept
	{
		if (configured_ && config_ == config) return;
		config_ = config;
		configured_ = true;
		CancelVisual();
	}

	void MouseLifecycle::CancelVisual() noexcept
	{
		logicalDiameter_ = static_cast<float>(Positive(config_.StandardDiameterPx(), 16.0));
		visualDiameter_ = logicalDiameter_;
		releaseCandidate_ = releasing_ = hasPosition_ = false;
	}

	void MouseLifecycle::ObserveHover(float x, float y, double seconds) noexcept
	{
		if (contactOwned_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(seconds) ||
			seconds < lastEventSeconds_ || seconds < lastHoverSeconds_) return;
		x_ = x;
		y_ = y;
		hasPosition_ = true;
		lastHoverSeconds_ = seconds;
		// 定位仅更新位置，不创建速度/加速度/清扫证据。
	}

	void MouseLifecycle::BeginContact(Controller& controller, float x, float y,
		double seconds, const Config& config) noexcept
	{
		Configure(config);
		CancelVisual();
		contactOwned_ = true;
		x_ = x;
		y_ = y;
		hasPosition_ = true;
		lastDownSeconds_ = seconds;
		lastEventSeconds_ = std::max(lastEventSeconds_, seconds);
		// StartKind::Hover 仍只是初始化类别；真正鼠标 Down 明确重置完整动态状态。
		controller.Reset(x, y, seconds, StartKind::Hover, config);
	}

	void MouseLifecycle::EndContact(Controller& controller, float acceptedDiameter, float x, float y,
		double seconds, bool anotherOwner, bool cancelled) noexcept
	{
		if (!std::isfinite(seconds)) return;
		if (!contactOwned_ && !anotherOwner) return;
		// Up 的逻辑重置立即发生，真实点的半径已由调用方接受，不随此重置变化。
		controller.Reset(x, y, seconds, StartKind::Hover, controller.Configuration());
		contactOwned_ = anotherOwner;
		lastEventSeconds_ = std::max(lastEventSeconds_, seconds);
		// 旧 Up 不恢复旧画面，但仍须按当前所有者集合释放占用；配置切换可能已丢弃候选。
		if (cancelled || (seconds < lastDownSeconds_ && !releaseCandidate_))
		{
			if (!anotherOwner) CancelVisual();
			return;
		}
		if (seconds >= lastDownSeconds_ && (!releaseCandidate_ || seconds >= releaseSeconds_))
		{
			// 复制真正接受的端点直径，不读取 Controller 的待用帧预览。
			releaseFrom_ = static_cast<float>(Positive(acceptedDiameter, config_.StandardDiameterPx()));
			releaseSeconds_ = seconds;
			x_ = x;
			y_ = y;
			hasPosition_ = true;
			releaseCandidate_ = true;
		}
		if (anotherOwner || !releaseCandidate_) return;
		logicalDiameter_ = std::min(releaseFrom_, config_.StandardDiameterPx());
		visualDiameter_ = releaseFrom_;
		releasing_ = releaseFrom_ > logicalDiameter_;
	}

	float MouseLifecycle::Advance(double seconds) noexcept
	{
		if (!std::isfinite(seconds)) return visualDiameter_;
		visualTime_ = std::max(visualTime_, seconds);
		if (!releasing_ || contactOwned_) return visualDiameter_;
		const double amount = std::clamp((visualTime_ - releaseSeconds_) /
			Positive(config_.mouseReleaseSeconds, 0.140), 0.0, 1.0);
		visualDiameter_ = static_cast<float>(releaseFrom_ +
			(logicalDiameter_ - releaseFrom_) * SmoothStep(amount));
		if (amount >= 1.0)
		{
			visualDiameter_ = logicalDiameter_;
			releasing_ = releaseCandidate_ = false;
		}
		return visualDiameter_;
	}

	bool MouseLifecycle::NeedsAnimation(double seconds) const noexcept
	{
		return releasing_ && !contactOwned_ && std::isfinite(seconds) &&
			seconds < releaseSeconds_ + Positive(config_.mouseReleaseSeconds, 0.140);
	}


	void ContactSizeState::Reset(float diameterPx,double seconds) noexcept
	{
		effectiveDiameterPx=resumeDiameterPx=diameterPx;
		effectiveTimeSeconds=seconds;
		breakTimeSeconds=seconds;
		breakPending=false;
	}
	void ContactSizeState::Update(float diameterPx,double seconds,bool stationary) noexcept
	{
		if(!std::isfinite(diameterPx)||diameterPx<=0||!std::isfinite(seconds)||seconds<effectiveTimeSeconds)return;
		if(stationary && std::abs(diameterPx-effectiveDiameterPx)>0.001f)
		{
			if(!breakPending)breakTimeSeconds=seconds;
			breakPending=true;
			resumeDiameterPx=diameterPx;
		}
		effectiveDiameterPx=diameterPx;
		effectiveTimeSeconds=seconds;
	}
	WidthInterval ContactSizeState::MakeInterval(double fromModelTime,double toModelTime,
		float oldDiameter,float newDiameter,double rawSeconds,const Config& config) const noexcept
	{
		const bool reanchor=breakPending && rawSeconds>=breakTimeSeconds;
		return {fromModelTime,toModelTime,reanchor?resumeDiameterPx:oldDiameter,newDiameter,
			config.minimumDiameterPx,config.maximumDiameterPx,reanchor};
	}
	void ContactSizeState::Accepted(const WidthInterval& interval) noexcept
	{
		if(interval.reanchor)breakPending=false;
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
