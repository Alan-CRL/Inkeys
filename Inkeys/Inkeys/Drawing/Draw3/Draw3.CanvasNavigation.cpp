module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

module Inkeys.Drawing.Draw3.canvas_navigation;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		float ClampFinite(float value, float minimum, float maximum) noexcept
		{
			if (!std::isfinite(value)) return 0.0f;
			return std::clamp(value, minimum, maximum);
		}
	}

	CanvasContentTranslationResult ApplyCanvasContentTranslationChecked(
		CanvasViewportState& viewport, CanvasVector contentDelta) noexcept
	{
		const float oldX = ClampFinite(viewport.x,
			-kCanvasViewportLimitDip, kCanvasViewportLimitDip);
		const float oldY = ClampFinite(viewport.y,
			-kCanvasViewportLimitDip, kCanvasViewportLimitDip);
		const double requestedX = static_cast<double>(oldX) -
			static_cast<double>(contentDelta.x);
		const double requestedY = static_cast<double>(oldY) -
			static_cast<double>(contentDelta.y);
		const bool finiteX = std::isfinite(requestedX);
		const bool finiteY = std::isfinite(requestedY);
		viewport.x = static_cast<float>(std::clamp(finiteX ? requestedX : 0.0,
			-static_cast<double>(kCanvasViewportLimitDip),
			static_cast<double>(kCanvasViewportLimitDip)));
		viewport.y = static_cast<float>(std::clamp(finiteY ? requestedY : 0.0,
			-static_cast<double>(kCanvasViewportLimitDip),
			static_cast<double>(kCanvasViewportLimitDip)));
		return {
			{ viewport.x - oldX, viewport.y - oldY },
			!finiteX || requestedX < -static_cast<double>(kCanvasViewportLimitDip) ||
				requestedX > static_cast<double>(kCanvasViewportLimitDip),
			!finiteY || requestedY < -static_cast<double>(kCanvasViewportLimitDip) ||
				requestedY > static_cast<double>(kCanvasViewportLimitDip)
		};
	}

	CanvasVector ApplyCanvasContentTranslation(
		CanvasViewportState& viewport, CanvasVector contentDelta) noexcept
	{
		return ApplyCanvasContentTranslationChecked(viewport, contentDelta).viewportDelta;
	}

	CanvasVector ScreenToCanvas(CanvasVector screen, CanvasViewportState viewport) noexcept
	{
		if (!std::isfinite(screen.x) || !std::isfinite(screen.y) ||
			!std::isfinite(viewport.x) || !std::isfinite(viewport.y)) return {};
		return { screen.x + viewport.x, screen.y + viewport.y };
	}

	CanvasVector CanvasToScreen(CanvasVector canvas, CanvasViewportState viewport) noexcept
	{
		if (!std::isfinite(canvas.x) || !std::isfinite(canvas.y) ||
			!std::isfinite(viewport.x) || !std::isfinite(viewport.y)) return {};
		return { canvas.x - viewport.x, canvas.y - viewport.y };
	}

	bool ShouldConsumeCanvasPanContactSnapshot(uint64_t snapshotSequence,
		uint64_t lastConsumedSequence, bool terminalPending) noexcept
	{
		return terminalPending || snapshotSequence != lastConsumedSequence;
	}

	bool ShouldBeginSuppressingPenContactDuringTouchPan(bool touchPanActive,
		bool penInContact) noexcept
	{
		return touchPanActive && penInContact;
	}

	bool ShouldSuppressPenContactForTouchPan(bool touchPanContactLive,
		int64_t penDownQpc, int64_t lastTouchPanEndQpc,
		bool suppressedPenContactLive) noexcept
	{
		return suppressedPenContactLive || touchPanContactLive ||
			(lastTouchPanEndQpc > 0 && penDownQpc <= lastTouchPanEndQpc);
	}

	bool IsPenContactSampleFresh(bool inContact, int64_t sampleQpc,
		int64_t suppressedTerminalQpc) noexcept
	{
		return inContact && (suppressedTerminalQpc <= 0 ||
			sampleQpc > suppressedTerminalQpc);
	}

	bool ShouldPrioritizeDrawingContact(bool navigationInProgress,
		bool penInContact, bool mouseInContact) noexcept
	{
		return navigationInProgress && (penInContact || mouseInContact);
	}

	float CanvasPanFallbackBlurDip(float speedDipPerSecond) noexcept
	{
		if (!std::isfinite(speedDipPerSecond) ||
			speedDipPerSecond <= kCanvasPanSharpSpeedThresholdDipPerSecond) return 0.0f;
		const float ratio = std::clamp((speedDipPerSecond -
			kCanvasPanSharpSpeedThresholdDipPerSecond) / 6000.0f, 0.0f, 1.0f);
		return ratio * kCanvasPanMaximumFallbackBlurDip;
	}

	namespace
	{
		double PanSecondsBetween(int64_t later, int64_t earlier, int64_t frequency) noexcept
		{
			return static_cast<double>(later - earlier) / static_cast<double>(frequency);
		}

		CanvasVector ToCanvasVector(double x, double y) noexcept
		{
			return { static_cast<float>(x), static_cast<float>(y) };
		}

		const CanvasPanPointer* PanPointerAt(std::span<const CanvasPanPointer> pointers, uint64_t key) noexcept
		{
			for (const auto& pointer : pointers) if (pointer.key == key) return &pointer;
			return nullptr;
		}

		CanvasVector PanFocus(std::span<const CanvasPanPointer> pointers,
			std::optional<uint64_t> skip = {}) noexcept
		{
			float x = 0.0f, y = 0.0f;
			size_t count = 0;
			for (const auto& pointer : pointers)
			{
				if (skip && pointer.key == *skip) continue;
				x += static_cast<float>(pointer.x); y += static_cast<float>(pointer.y); ++count;
			}
			return count ? ToCanvasVector(x / count, y / count) : CanvasVector{};
		}

		double PanDot(CanvasVector a, CanvasVector b) noexcept
		{
			return a.x * b.x + a.y * b.y;
		}

		int64_t PanMicroseconds(double seconds) noexcept
		{ return static_cast<int64_t>(std::llround(seconds * 1000000.0)); }

		bool PanZero(CanvasVector value) noexcept { return value.x == 0.0f && value.y == 0.0f; }

		void ResetTouchReleaseMetadata(CanvasPanFrameOutput& output) noexcept
		{
			// 仅清最后一次释放的Touch专属结果；帧级位移与混组反馈必须保留。
			output.touchDecision = {}; output.releaseTouchTag = {};
			output.releaseRawAgeSeconds = output.releasePlatformSeconds = 0;
		}

		size_t PanIndependentReleaseEvidence(std::span<const CanvasPanVelocitySample> samples, int64_t frequency,
			std::array<size_t, kCanvasPanReleaseCapacity>& evidence) noexcept
		{
			size_t count = 0;
			for (size_t back = samples.size(); back > 0; --back)
			{
				const auto sample = samples[back - 1];
				// 从最新真实端点向前取有时间支撑的点，不把同批密集回调当作独立时钟证据。
				if (count == 0 ||
					PanSecondsBetween(samples[evidence[count - 1]].qpc, sample.qpc, frequency) + 1e-9 >= kCanvasPanReleaseMinimumIntervalSeconds)
					evidence[count++] = back - 1;
			}
			std::reverse(evidence.begin(), evidence.begin() + count);
			return count;
		}

	}

	CanvasPanMotionContext MakeCanvasPanMotionContext(CanvasPanMotionProfile profile,
		double motionPerDipX, double motionPerDipY) noexcept
	{
		CanvasPanMotionContext c; c.profile = profile;
		if (profile == CanvasPanMotionProfile::TouchPhysical || profile == CanvasPanMotionProfile::ScreenPenPhysical)
		{
			c.unit = CanvasPanMotionUnit::Millimeters;
			c.motionPerDipX = motionPerDipX; c.motionPerDipY = motionPerDipY;
			c.slopEpsilon = .01; c.scrollEpsilon = .02;
		}
		switch (profile)
		{
		case CanvasPanMotionProfile::TouchPhysical:
			c.touchSlop = 2.5; c.reversalDistance = .25; c.minimumFlingVelocity = 10;
			c.boostMinimumFlingVelocity = 70; c.boostMinimumScrollVelocity = 30; c.velocityLimit = 2400; break;
		case CanvasPanMotionProfile::ScreenPenPhysical:
			c.touchSlop = 1.5; c.reversalDistance = .15; c.minimumFlingVelocity = 15;
			c.boostMinimumFlingVelocity = 100; c.boostMinimumScrollVelocity = 45; c.velocityLimit = 1800; break;
		case CanvasPanMotionProfile::ScreenPenFallback:
			c.touchSlop = 8; c.reversalDistance = .75; c.minimumFlingVelocity = 60;
			c.boostMinimumFlingVelocity = 500; c.boostMinimumScrollVelocity = 225; c.velocityLimit = 9000; break;
		default: break;
		}
		return c;
	}

	bool IsCanvasPanMotionContextValid(const CanvasPanMotionContext& c) noexcept
	{
		const auto positive = [](double v) { return std::isfinite(v) && v > 0 && v <= std::numeric_limits<float>::max(); };
		const bool physical = c.profile == CanvasPanMotionProfile::TouchPhysical || c.profile == CanvasPanMotionProfile::ScreenPenPhysical;
		if (static_cast<unsigned>(c.profile) > static_cast<unsigned>(CanvasPanMotionProfile::ScreenPenFallback) ||
			c.unit != (physical ? CanvasPanMotionUnit::Millimeters : CanvasPanMotionUnit::SystemDip)) return false;
		if (!positive(c.motionPerDipX) || !positive(c.motionPerDipY) ||
			(physical ? (c.motionPerDipX < .01 || c.motionPerDipX > 10 || c.motionPerDipY < .01 || c.motionPerDipY > 10) :
				(c.motionPerDipX != 1 || c.motionPerDipY != 1))) return false;
		return positive(c.touchSlop) && std::isfinite(c.slopEpsilon) && c.slopEpsilon >= 0 && c.slopEpsilon <= 1 &&
			std::isfinite(c.scrollEpsilon) && c.scrollEpsilon >= 0 && c.scrollEpsilon <= 1 && positive(c.reversalDistance) &&
			positive(c.minimumFlingVelocity) && positive(c.boostMinimumFlingVelocity) && positive(c.boostMinimumScrollVelocity) &&
			positive(c.velocityLimit) && c.minimumFlingVelocity <= c.velocityLimit &&
			std::isfinite(c.touchWeight) && c.touchWeight >= 0 && c.touchWeight <= 1 &&
			std::isfinite(c.intentGain) && c.intentGain >= .25 && c.intentGain <= 2;
	}

	CanvasPanFrictionParameters ResolveCanvasPanFriction(const CanvasPanMotionContext& c, double speed) noexcept
	{
		const auto smooth = [](double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); };
		if (!IsCanvasPanMotionContextValid(c) || !std::isfinite(speed)) return {};
		speed = std::clamp(speed, 0.0, c.velocityLimit);
		switch (c.profile)
		{
		case CanvasPanMotionProfile::TouchPhysical:
		{
			const double a = c.touchWeight * smooth((speed - 600) / 900);
			return { .30 + .12 * a, 200 * (1 - .25 * a) };
		}
		case CanvasPanMotionProfile::TouchFallback:
		{
			// 回退增益只判断动作意图，绝不放大跟手几何、真实初速或残余。
			const double a = c.largeScreen ? .5 * smooth((speed * c.intentGain - 750) / 1500) : 0;
			return { .30 + .12 * a, 1000 * (1 - .25 * a) };
		}
		case CanvasPanMotionProfile::ScreenPenPhysical: return { .20, 350 };
		case CanvasPanMotionProfile::ScreenPenFallback: return { .20, 1750 };
		default: return {};
		}
	}

	const char* TouchReleaseModeName(TouchReleaseMode mode) noexcept
	{
		switch (mode)
		{
		case TouchReleaseMode::Stop: return "stop";
		case TouchReleaseMode::Measured: return "measured";
		case TouchReleaseMode::ShortSupported: return "short-supported";
		case TouchReleaseMode::ResidualOnly: return "residual-only";
		case TouchReleaseMode::DistanceBounded: return "distance-bounded";
		default: return "not-evaluated";
		}
	}
	TouchReleaseDecision ResolveTouchRelease(const TouchReleaseFacts& f) noexcept
	{
		TouchReleaseDecision d; d.mode = TouchReleaseMode::Stop;
		const auto stop = [&](TouchReleaseReason reason) { auto out = d; out.reason = reason; return out; };
		if (!IsCanvasPanMotionContextValid(f.context) || !std::isfinite(f.rawAgeSeconds) || !std::isfinite(f.motionAgeSeconds) ||
			!std::isfinite(f.scrollAgeSeconds) || !std::isfinite(f.direction.x) || !std::isfinite(f.direction.y) ||
			!std::isfinite(f.confirmedNet.x) || !std::isfinite(f.confirmedNet.y) || !std::isfinite(f.residual.x) || !std::isfinite(f.residual.y) ||
			(f.evidence.valid && (!std::isfinite(f.evidence.velocity.x) || !std::isfinite(f.evidence.velocity.y)))) return stop(TouchReleaseReason::Unsupported);
		if (f.cancelled) return stop(TouchReleaseReason::Cancelled);
		if (!f.allowInertia) return stop(TouchReleaseReason::Disabled);
		if (f.confirmedStopped) return stop(TouchReleaseReason::ConfirmedStop);
		if (!f.scrolled) return stop(TouchReleaseReason::NoScroll);
		if (f.motionAgeSeconds >= kCanvasPanReleaseZeroAgeSeconds) return stop(TouchReleaseReason::ConfirmedStop);
		if (f.rawAgeSeconds >= kCanvasPanReleaseZeroAgeSeconds) return stop(TouchReleaseReason::StaleRaw);
		const double fresh = std::clamp((kCanvasPanReleaseZeroAgeSeconds - f.rawAgeSeconds) /
			(kCanvasPanReleaseZeroAgeSeconds - kCanvasPanReleaseFullAgeSeconds), 0.0, 1.0);
		const double residualSpeed = std::hypot(f.residual.x, f.residual.y);
		const double directionSpeed = std::hypot(f.direction.x, f.direction.y);
		const bool residualFresh = f.residualEligible && f.rawAgeSeconds <= kCanvasPanReleaseFullAgeSeconds &&
			f.scrollAgeSeconds <= (f.platformObserved ? kCanvasPanReleaseZeroAgeSeconds : kCanvasPanBoostTimeoutSeconds);
		if (f.evidence.valid)
		{
			d.handVelocity = { static_cast<float>(f.evidence.velocity.x * fresh), static_cast<float>(f.evidence.velocity.y * fresh) };
			const double hand = std::hypot(d.handVelocity.x, d.handVelocity.y);
			if (hand <= f.context.minimumFlingVelocity) return stop(TouchReleaseReason::Unsupported);
			const double ux = d.handVelocity.x / hand, uy = d.handVelocity.y / hand;
			const double extra = residualFresh && !f.evidence.supportLimited && hand >= f.context.boostMinimumFlingVelocity &&
				residualSpeed >= f.context.boostMinimumFlingVelocity ? (std::max)(0.0, f.residual.x * ux + f.residual.y * uy) : 0;
			const double speed = (std::min)(f.context.velocityLimit, hand + extra);
			d.velocitySeed = ToCanvasVector(ux * speed, uy * speed); d.addedResidual = extra > 0;
			d.boostSuppressed = f.evidence.supportLimited || (f.residualEligible && !residualFresh);
			d.mode = f.evidence.shortSupported ? TouchReleaseMode::ShortSupported : TouchReleaseMode::Measured;
			d.reason = TouchReleaseReason::Velocity; d.response = TouchResponseSource::Measured;
			d.parameters = ResolveCanvasPanFriction(f.context, hand); d.evidenceContactKey = f.evidence.contactKey; d.evidenceMotionQpc = f.evidence.motionQpc;
			return d;
		}
		if (!(directionSpeed > 0)) return stop(TouchReleaseReason::Unsupported);
		const double ux = f.direction.x / directionSpeed, uy = f.direction.y / directionSpeed;
		d.parameters = ResolveCanvasPanFriction(f.context, 0); d.response = TouchResponseSource::Base;
		const double residual = residualFresh ? (std::max)(0.0, f.residual.x * ux + f.residual.y * uy) : 0;
		if (residual > f.context.minimumFlingVelocity)
		{
			const double speed = (std::min)(residual, f.context.velocityLimit);
			d.velocitySeed = ToCanvasVector(ux * speed, uy * speed); d.mode = TouchReleaseMode::ResidualOnly;
			d.reason = TouchReleaseReason::Residual; d.boostSuppressed = true; return d;
		}
		if (!f.budgetConfirmed) return stop(TouchReleaseReason::Unsupported);
		const double budget = (std::max)(0.0, f.confirmedNet.x * ux + f.confirmedNet.y * uy);
		const double tau = d.parameters.tauSeconds, c = d.parameters.deceleration;
		const auto distance = [&](double speed) { return tau * speed - c * tau * tau * std::log1p(speed / (c * tau)); };
		d.distanceBudget = (std::min)(budget, distance(f.context.velocityLimit));
		if (!(d.distanceBudget > 0)) return stop(TouchReleaseReason::Unsupported);
		// 固定有界逆解基础摩擦D(v)，只取下界；float种子再留舍入余量，不能超支实际位移。
		double low = 0, high = f.context.velocityLimit;
		for (int iteration = 0; iteration < 64; ++iteration)
		{ const double mid = (low + high) * .5; if (distance(mid) <= d.distanceBudget) low = mid; else high = mid; }
		const double seed = low * fresh * (1 - 4 * std::numeric_limits<float>::epsilon());
		if (seed <= f.context.minimumFlingVelocity) return stop(TouchReleaseReason::Unsupported);
		d.velocitySeed = ToCanvasVector(ux * seed, uy * seed); d.mode = TouchReleaseMode::DistanceBounded;
		d.reason = TouchReleaseReason::Distance; d.boostSuppressed = true; return d;
	}

	const char* CanvasPanAdmissionReasonName(CanvasPanAdmissionReason reason) noexcept
	{
		switch (reason)
		{
		case CanvasPanAdmissionReason::Accepted: return "accepted";
		case CanvasPanAdmissionReason::ClassConflict: return "class-conflict";
		case CanvasPanAdmissionReason::MetricConflict: return "metric-conflict";
		case CanvasPanAdmissionReason::Capacity: return "capacity";
		default: return "invalid-context";
		}
	}

	const char* CanvasPanReleaseReasonName(CanvasPanReleaseReason reason) noexcept
	{
		switch (reason)
		{
		case CanvasPanReleaseReason::Ready: return "ready";
		case CanvasPanReleaseReason::InsufficientSamples: return "few-samples";
		case CanvasPanReleaseReason::InsufficientSpan: return "short-span";
		case CanvasPanReleaseReason::TimingRejected: return "invalid-time";
		case CanvasPanReleaseReason::Stationary: return "slow-or-stationary";
		case CanvasPanReleaseReason::Stale: return "stale";
		case CanvasPanReleaseReason::Cancelled: return "cancelled";
		case CanvasPanReleaseReason::WithinSlop: return "within-slop";
		case CanvasPanReleaseReason::EmptyViewport: return "empty-viewport";
		case CanvasPanReleaseReason::DirectionConflict: return "direction-conflict";
		case CanvasPanReleaseReason::Disabled: return "disabled";
		case CanvasPanReleaseReason::InsufficientSupport: return "unsupported-tail";
		default: return "none";
		}
	}

	const CanvasPanReleaseVelocityTracker::History* CanvasPanReleaseVelocityTracker::Find(uint64_t key) const noexcept
	{
		for (const auto& history : histories_) if (history.used && history.key == key) return &history;
		return nullptr;
	}

	void CanvasPanReleaseVelocityTracker::Configure(const CanvasPanMotionContext& context) noexcept
	{
		reversalDistance_ = context.reversalDistance;
		independentFit_ = context.profile != CanvasPanMotionProfile::IndirectDip;
		touchProgress_ = context.profile == CanvasPanMotionProfile::TouchPhysical || context.profile == CanvasPanMotionProfile::TouchFallback;
	}

	CanvasPanSampleDisposition CanvasPanReleaseVelocityTracker::AddSample(uint64_t key,
		CanvasPanVelocitySample sample, int64_t frequency) noexcept
	{
		if (frequency <= 0 || sample.qpc <= 0 || !std::isfinite(sample.x) || !std::isfinite(sample.y))
			return CanvasPanSampleDisposition::Rejected;
		auto found = std::find_if(histories_.begin(), histories_.end(),
			[&](const History& history) { return history.used && history.key == key; });
		if (found == histories_.end())
		{
			found = std::find_if(histories_.begin(), histories_.end(), [](const History& history) { return !history.used; });
			if (found == histories_.end()) return CanvasPanSampleDisposition::Rejected;
			*found = {}; found->used = true; found->key = key;
		}
		auto& history = *found;
		const std::optional<CanvasPanVelocitySample> previousRaw = touchProgress_ && history.count ?
			std::optional(history.samples[history.count - 1]) : std::nullopt;
		CanvasPanSampleDisposition result = CanvasPanSampleDisposition::Accepted;
		if (history.count)
		{
			const auto previous = history.samples[history.count - 1];
			if (sample.qpc <= previous.qpc) return CanvasPanSampleDisposition::Rejected;
			const double dt = PanSecondsBetween(sample.qpc, previous.qpc, frequency);
			const double dx = sample.x - previous.x, dy = sample.y - previous.y;
			const double distance = std::hypot(dx, dy);
			// 输入速度可以高于惯性输出上限。只有真实长空档清时间窗，不用cap否定输入。
			if (dt >= kCanvasPanTrackerGapSeconds)
			{
				history.count = 0; history.direction = {}; history.reversalAnchor.reset();
				if (touchProgress_) history.progressCount = 0;
			}
			else if (distance > 0.0)
			{
				const double along = dx * history.direction.x + dy * history.direction.y;
				if (!PanZero(history.direction) && along < 0.0)
				{
					if (!history.reversalAnchor) history.reversalAnchor = previous;
					const auto anchor = *history.reversalAnchor;
					const double reverseX = sample.x - anchor.x, reverseY = sample.y - anchor.y;
					// 反向累计达到本批运动域阈值才确认转折，避免细碎往返噪声不断清窗。
					if (reverseX * history.direction.x + reverseY * history.direction.y <= -reversalDistance_)
					{
						history.samples[0] = anchor; history.count = 1;
					if (touchProgress_) { history.progressSamples[0] = anchor; history.progressCount = 1; }
						const double length = std::hypot(reverseX, reverseY);
						history.direction = ToCanvasVector(reverseX / length, reverseY / length);
						history.reversalAnchor.reset();
						result = CanvasPanSampleDisposition::Reversed;
					}
				}
				else
				{
					history.reversalAnchor.reset();
					if (distance >= reversalDistance_ || PanZero(history.direction))
						history.direction = ToCanvasVector(dx / distance, dy / distance);
				}
			}
		}
		// 保留窗口左侧的一个真实锚点，低报点率也能覆盖完整的尾段时间。
		while (history.count > 1 && PanSecondsBetween(sample.qpc, history.samples[1].qpc, frequency) > kCanvasPanReleaseWindowSeconds)
		{
			std::move(history.samples.begin() + 1, history.samples.begin() + history.count, history.samples.begin());
			--history.count;
		}
		if (history.count == history.samples.size())
		{
			std::move(history.samples.begin() + 1, history.samples.end(), history.samples.begin()); --history.count;
		}
		history.samples[history.count++] = sample;
		if (touchProgress_)
		{
			const auto appendProgress = [&](CanvasPanVelocitySample point)
			{
				while (history.progressCount > 1 && PanSecondsBetween(point.qpc, history.progressSamples[1].qpc, frequency) > kCanvasPanReleaseWindowSeconds)
				{
					std::move(history.progressSamples.begin() + 1, history.progressSamples.begin() + history.progressCount, history.progressSamples.begin());
					--history.progressCount;
				}
				if (history.progressCount == history.progressSamples.size())
				{
					std::move(history.progressSamples.begin() + 1, history.progressSamples.end(), history.progressSamples.begin()); --history.progressCount;
				}
				history.progressSamples[history.progressCount++] = point;
			};
			if (!history.progressCount) appendProgress(sample);
			else
			{
				const auto previous = history.progressSamples[history.progressCount - 1];
				const double error = std::numeric_limits<float>::epsilon() *
					(std::hypot(sample.x, sample.y) + std::hypot(previous.x, previous.y));
				if (std::hypot(sample.x - previous.x, sample.y - previous.y) > error)
				{
					// 重复点保留在raw历史，不刷新运动QPC或裁掉运动窗；catch-up使用原真实时间差。
					// >=70ms无进展后的新移动退休旧证据，仅用上一真实raw点作为新leg锚。
					if (result != CanvasPanSampleDisposition::Reversed &&
						PanSecondsBetween(sample.qpc, previous.qpc, frequency) >= kCanvasPanReleaseZeroAgeSeconds)
					{
						history.progressCount = 0;
						if (previousRaw) appendProgress(*previousRaw);
					}
					appendProgress(sample);
				}
			}
		}
		return result;
	}

	std::span<const CanvasPanVelocitySample> CanvasPanReleaseVelocityTracker::MotionSamples(const History& history) const noexcept
	{
		return touchProgress_ ? std::span(history.progressSamples).first(history.progressCount) : std::span(history.samples).first(history.count);
	}
	int64_t CanvasPanReleaseVelocityTracker::LastMotionQpc(uint64_t key) const noexcept
	{
		const auto* history = Find(key);
		if (!history) return 0;
		const auto samples = MotionSamples(*history);
		return samples.empty() ? 0 : samples.back().qpc;
	}
	size_t CanvasPanReleaseVelocityTracker::MotionSampleCount(uint64_t key) const noexcept
	{ const auto* history = Find(key); return history ? MotionSamples(*history).size() : 0; }
	bool CanvasPanReleaseVelocityTracker::HasStationaryObservation(uint64_t key) const noexcept
	{ return touchProgress_ && LastSampleQpc(key) > LastMotionQpc(key); }

	void CanvasPanReleaseVelocityTracker::Clear() noexcept { histories_ = {}; }
	void CanvasPanReleaseVelocityTracker::ClearPointer(uint64_t key) noexcept
	{ for (auto& history : histories_) if (history.used && history.key == key) { history = {}; return; } }
	void CanvasPanReleaseVelocityTracker::ClearAxis(bool x, bool y) noexcept
	{
		for (auto& history : histories_)
		{
			if (!history.used || !history.count) continue;
			const auto newest = history.samples[history.count - 1];
			for (size_t i = 0; i < history.count; ++i)
			{ if (x) history.samples[i].x = newest.x; if (y) history.samples[i].y = newest.y; }
			if (touchProgress_)
				for (size_t i = 0; i < history.progressCount; ++i)
				{ if (x) history.progressSamples[i].x = newest.x; if (y) history.progressSamples[i].y = newest.y; }
			if (x) history.direction.x = 0.0f;
			if (y) history.direction.y = 0.0f;
			const double length = std::hypot(history.direction.x, history.direction.y);
			if (length > 0.0) history.direction = ToCanvasVector(history.direction.x / length, history.direction.y / length);
			history.reversalAnchor.reset();
		}
	}
	size_t CanvasPanReleaseVelocityTracker::SampleCount(uint64_t key) const noexcept
	{ const auto* history = Find(key); return history ? history->count : 0; }
	int64_t CanvasPanReleaseVelocityTracker::LastSampleQpc(uint64_t key) const noexcept
	{ const auto* history = Find(key); return history && history->count ? history->samples[history->count - 1].qpc : 0; }
	int64_t CanvasPanReleaseVelocityTracker::LastSampleQpc() const noexcept
	{
		int64_t latest = 0;
		for (const auto& history : histories_) if (history.used && history.count)
			latest = (std::max)(latest, history.samples[history.count - 1].qpc);
		return latest;
	}
	double CanvasPanReleaseVelocityTracker::SampleSpanSeconds(uint64_t key, int64_t frequency) const noexcept
	{
		const auto* history = Find(key);
		return frequency > 0 && history && history->count > 1 ?
			PanSecondsBetween(history->samples[history->count - 1].qpc, history->samples[0].qpc, frequency) : 0.0;
	}
	std::optional<double> CanvasPanReleaseVelocityTracker::RecentSpeedLimit(uint64_t key,
		int64_t frequency, CanvasVector estimatedVelocity) const noexcept
	{
		const auto* history = Find(key);
		if (frequency <= 0 || !history || PanZero(estimatedVelocity)) return {};
		const auto samples = MotionSamples(*history);
		if (samples.size() < 2) return {};
		const auto newest = samples[samples.size() - 1];
		for (size_t back = 1; back < samples.size(); ++back)
		{
			const auto previous = samples[samples.size() - back - 1];
			const double dt = PanSecondsBetween(newest.qpc, previous.qpc, frequency);
			const double dx = newest.x - previous.x, dy = newest.y - previous.y;
			const double distance = std::hypot(dx, dy);
			// 用真实点累积末段；一毫秒报点和亚像素量化不能单点决定释放上限。
			if (dt + 1e-9 < kCanvasPanReleaseMinimumIntervalSeconds || distance < reversalDistance_) continue;
			if (dx * estimatedVelocity.x + dy * estimatedVelocity.y <= 0.0) return {};
			return distance / dt;
		}
		return {};
	}

	CanvasPanReleaseSupport CanvasPanReleaseVelocityTracker::ReleaseSupport(uint64_t key,
		int64_t frequency, CanvasPanVelocityEstimate estimatedVelocity) const noexcept
	{
		CanvasPanReleaseSupport support;
		const auto* history = Find(key);
		if (frequency <= 0 || !history) return support;
		const auto samples = MotionSamples(*history);
		if (samples.empty()) return support;
		std::array<size_t, kCanvasPanReleaseCapacity> evidence;
		support.anchorCount = PanIndependentReleaseEvidence(samples, frequency, evidence);
		support.firstQpc = samples[evidence[0]].qpc;
		support.lastQpc = samples[evidence[support.anchorCount - 1]].qpc;
		const double speed = std::hypot(estimatedVelocity.x, estimatedVelocity.y);
		if (support.anchorCount < kCanvasPanReleaseMinimumSamples ||
			(PanSecondsBetween(support.lastQpc, support.firstQpc, frequency) + 1e-9 < kCanvasPanReleaseMinimumSpanSeconds && !ShortTouchSupported(key, frequency)) ||
			!estimatedVelocity.valid || !std::isfinite(speed) || speed <= 0) return support;
		support.evaluated = true;
		const double ux = estimatedVelocity.x / speed, uy = estimatedVelocity.y / speed;
		// 至少两份真实正向位移证据；静止窗中的单个跳点不能凭空间容差制造惯性。
		for (size_t index = 1; index < support.anchorCount; ++index)
		{
			const auto first = samples[evidence[index - 1]], last = samples[evidence[index]];
			const double projected = (last.x - first.x) * ux + (last.y - first.y) * uy;
			const double error = std::numeric_limits<float>::epsilon() *
				(std::hypot(first.x, first.y) + std::hypot(last.x, last.y));
			if (projected > error) ++support.positiveIntervalCount;
		}
		if (support.positiveIntervalCount < 2) return support;
		const auto a = samples[evidence[support.anchorCount - 3]];
		const auto b = samples[evidence[support.anchorCount - 2]];
		support.supportFirstQpc = a.qpc; support.supportLastQpc = b.qpc;
		const auto c = samples[evidence[support.anchorCount - 1]];
		if (b.qpc - a.qpc <= 1 || c.qpc - b.qpc <= 1) return support;
		// AB不含最新C；BC即使静止/小位移也必须参与，不能跳过后恢复更旧的快速度。
		const auto intervalUpper = [&](const CanvasPanVelocitySample& first, const CanvasPanVelocitySample& last)
		{
			const double dt = PanSecondsBetween(last.qpc, first.qpc, frequency) - 1.0 / static_cast<double>(frequency);
			if (dt <= 0) return 0.0;
			const double projected = (last.x - first.x) * ux + (last.y - first.y) * uy;
			const double error = std::numeric_limits<float>::epsilon() *
				(std::hypot(first.x, first.y) + std::hypot(last.x, last.y));
			// 复用当前profile的空间噪声预算；不是另设输入速度或硬件量化上限。
			return ((std::max)(0.0, projected) + reversalDistance_ + error) / dt;
		};
		support.speedUpperBound = (std::min)(intervalUpper(a, b), intervalUpper(b, c));
		support.valid = std::isfinite(support.speedUpperBound);
		return support;
	}

	bool CanvasPanReleaseVelocityTracker::ShortTouchSupported(uint64_t key, int64_t frequency) const noexcept
	{
		const auto* history = Find(key); if (!touchProgress_ || !history || frequency <= 0) return false;
		const auto samples = MotionSamples(*history); std::array<size_t, kCanvasPanReleaseCapacity> evidence;
		const size_t count = PanIndependentReleaseEvidence(samples, frequency, evidence);
		if (count < 3) return false;
		const auto a = samples[evidence[count - 3]], b = samples[evidence[count - 2]], c = samples[evidence[count - 1]];
		// 两段对AC都有正投影仍可能互成直角；可靠短证据要求相邻段本身同向。
		if (!((b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) > 0)) return false;
		const double dx = c.x - a.x, dy = c.y - a.y, length = std::hypot(dx, dy);
		if (!(length > 0)) return false;
		const auto supported = [&](CanvasPanVelocitySample first, CanvasPanVelocitySample last)
		{
			const double error = std::numeric_limits<float>::epsilon() * (std::hypot(first.x, first.y) + std::hypot(last.x, last.y));
			return ((last.x - first.x) * dx + (last.y - first.y) * dy) / length > reversalDistance_ + error;
		};
		return supported(a, b) && supported(b, c);
	}

	CanvasPanReleaseReason CanvasPanReleaseVelocityTracker::Reason(uint64_t key, int64_t frequency) const noexcept
	{
		const auto* history = Find(key);
		if (!history) return CanvasPanReleaseReason::InsufficientSamples;
		const auto samples = MotionSamples(*history);
		if (samples.size() < kCanvasPanReleaseMinimumSamples) return CanvasPanReleaseReason::InsufficientSamples;
		if (frequency <= 0) return CanvasPanReleaseReason::InsufficientSpan;
		std::array<size_t, kCanvasPanReleaseCapacity> evidence;
		const size_t count = PanIndependentReleaseEvidence(samples, frequency, evidence);
		if (count < kCanvasPanReleaseMinimumSamples) return CanvasPanReleaseReason::InsufficientSamples;
		if (PanSecondsBetween(samples[evidence[count - 1]].qpc, samples[evidence[0]].qpc, frequency) + 1e-9 < kCanvasPanReleaseMinimumSpanSeconds && !ShortTouchSupported(key, frequency))
			return CanvasPanReleaseReason::InsufficientSpan;
		return CanvasPanReleaseReason::Ready;
	}
	CanvasPanVelocityEstimate CanvasPanReleaseVelocityTracker::Estimate(uint64_t key, int64_t frequency) const noexcept
	{
		if (frequency <= 0 || Reason(key, frequency) != CanvasPanReleaseReason::Ready) return {};
		const auto& history = *Find(key);
		const auto samples = MotionSamples(history);
		std::array<size_t, kCanvasPanReleaseCapacity> evidence;
		const size_t count = independentFit_ ?
			PanIndependentReleaseEvidence(samples, frequency, evidence) : samples.size();
		const auto sampleAt = [&](size_t index) -> const CanvasPanVelocitySample&
		{ return samples[independentFit_ ? evidence[index] : index]; };
		const auto newest = samples[samples.size() - 1];
		double sumT = 0.0, sumX = 0.0, sumY = 0.0;
		for (size_t i = 0; i < count; ++i)
		{
			sumT += PanSecondsBetween(sampleAt(i).qpc, newest.qpc, frequency);
			sumX += sampleAt(i).x - newest.x; sumY += sampleAt(i).y - newest.y;
		}
		const double meanT = sumT / count, meanX = sumX / count, meanY = sumY / count;
		double denominator = 0.0, numeratorX = 0.0, numeratorY = 0.0;
		for (size_t i = 0; i < count; ++i)
		{
			const double dt = PanSecondsBetween(sampleAt(i).qpc, newest.qpc, frequency) - meanT;
			denominator += dt * dt;
			numeratorX += dt * (sampleAt(i).x - newest.x - meanX);
			numeratorY += dt * (sampleAt(i).y - newest.y - meanY);
		}
		if (!(denominator > 0.0)) return {};
		const double vx = numeratorX / denominator, vy = numeratorY / denominator;
		if (!std::isfinite(vx) || !std::isfinite(vy)) return {};
		// 连续转弯也可能让窗口拟合落在最新可靠方向背后；拒绝该速度，不回退旧窗口。
		const bool consistent = PanZero(history.direction) || (vx == 0.0 && vy == 0.0) ||
			vx * history.direction.x + vy * history.direction.y > 0.0;
		return { consistent, vx, vy };
	}

	void CanvasPanGestureDetector::Configure(const CanvasPanMotionContext& context) noexcept
	{
		touchSlop_ = static_cast<float>(context.touchSlop); slopEpsilon_ = static_cast<float>(context.slopEpsilon);
		scrollEpsilon_ = static_cast<float>(context.scrollEpsilon); minimumFlingVelocity_ = context.minimumFlingVelocity;
	}

	CanvasPanGestureBatch CanvasPanGestureDetector::Down(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, int64_t frequency) noexcept
	{
		CanvasPanGestureBatch batch;
		if (pointers.size() == 1)
		{
			Reset(); primaryDown_ = pointers.front(); maximumPointerCount_ = 1;
			batch.events[batch.count++] = { CanvasPanGestureType::FlingCancel, static_cast<double>(qpc) / frequency };
		}
		else
		{
			maximumPointerCount_ = (std::max)(maximumPointerCount_, pointers.size());
			if (pointers.size() == 2)
			{
				std::copy(pointers.begin(), pointers.end(), secondaryDown_.begin()); secondaryCount_ = 2;
			}
		}
		lastFocus_ = PanFocus(pointers);
		return batch;
	}

	CanvasPanGestureBatch CanvasPanGestureDetector::Move(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, int64_t frequency) noexcept
	{
		CanvasPanGestureBatch batch;
		const CanvasVector focus = PanFocus(pointers);
		bool within = maximumPointerCount_ <= 2;
		for (const auto& pointer : pointers)
		{
			const auto* down = pointer.key == primaryDown_.key ? &primaryDown_ :
				PanPointerAt(std::span(secondaryDown_).first(secondaryCount_), pointer.key);
			if (!down) { within = false; break; }
			const float dx = static_cast<float>(pointer.x) - static_cast<float>(down->x);
			const float dy = static_cast<float>(pointer.y) - static_cast<float>(down->y);
			const float slop = touchSlop_ + slopEpsilon_;
			if (dx * dx + dy * dy > slop * slop * kCanvasPanExpandSlopFactor) within = false;
		}
		CanvasVector delta{ focus.x - lastFocus_.x, focus.y - lastFocus_.y };
		if ((withinSlop_ && !within) || (!withinSlop_ &&
			(std::abs(delta.x) > scrollEpsilon_ || std::abs(delta.y) > scrollEpsilon_)))
		{
			// GestureDetector 的 scroll=last-focus；Provider 在边界取负后内容与手指同向。
			lastFocus_ = focus; withinSlop_ = false;
			if (!scrollSent_ && pointers.size() < 3)
			{
				// Provider 首次滚动逐指径向扣除15 DIP；0.05只参与Detector的越限判定。
				delta = {};
				for (const auto& pointer : pointers)
				{
					const auto* down = pointer.key == primaryDown_.key ? &primaryDown_ :
						PanPointerAt(std::span(secondaryDown_).first(secondaryCount_), pointer.key);
					if (!down) continue;
					float dx = static_cast<float>(pointer.x) - static_cast<float>(down->x);
					float dy = static_cast<float>(pointer.y) - static_cast<float>(down->y);
					const float distance = std::sqrt(dx * dx + dy * dy);
					if (distance > 1e-3f)
					{
						const float ratio = (std::max)(0.0f, distance - touchSlop_) / distance;
						dx *= ratio; dy *= ratio;
					}
					delta.x += dx; delta.y += dy;
				}
				const float inverseCount = 1.0f / static_cast<float>(pointers.size());
				delta.x *= inverseCount; delta.y *= inverseCount;
			}
			// GD 已越过 slop，但 Provider 的最终二维增量全零时仍不派发。
			// 不虚构 Begin/Update，否则会用零点积清掉 booster 并改写 fling 起点时间。
			if (!PanZero(delta))
			{
				if (!scrollSent_)
					batch.events[batch.count++] = { CanvasPanGestureType::ScrollBegin, static_cast<double>(qpc) / frequency, delta };
				batch.events[batch.count++] = { CanvasPanGestureType::ScrollUpdate, static_cast<double>(qpc) / frequency, delta };
				scrollSent_ = true;
			}
		}
		return batch;
	}

	CanvasPanGestureBatch CanvasPanGestureDetector::Up(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, uint64_t key, bool cancelled, int64_t frequency,
		CanvasPanVelocityEstimate releaseVelocity) noexcept
	{
		CanvasPanGestureBatch batch;
		if (pointers.size() > 1)
		{
			lastFocus_ = PanFocus(pointers, key);
			return batch;
		}
		const auto velocity = releaseVelocity;
		if (!cancelled && !withinSlop_ &&
			std::hypot(velocity.x, velocity.y) > minimumFlingVelocity_)
		{
			// Provider 在 GFS 后不再紧跟 GSE；无 GFS 的终态才发送 GSE。
			if (!scrollSent_)
				batch.events[batch.count++] = { CanvasPanGestureType::ScrollBegin, static_cast<double>(qpc) / frequency,
					ToCanvasVector(velocity.x, velocity.y) };
			CanvasPanGestureEvent fling{ CanvasPanGestureType::FlingStart, static_cast<double>(qpc) / frequency };
			fling.velocity = ToCanvasVector(velocity.x, velocity.y); batch.events[batch.count++] = fling;
		}
		else if (scrollSent_)
			batch.events[batch.count++] = { CanvasPanGestureType::ScrollEnd, static_cast<double>(qpc) / frequency };
		maximumPointerCount_ = 0; scrollSent_ = false;
		return batch;
	}
	void CanvasPanGestureDetector::Reset() noexcept
	{
		lastFocus_ = {}; primaryDown_ = {}; secondaryDown_ = {};
		secondaryCount_ = maximumPointerCount_ = 0; withinSlop_ = true; scrollSent_ = false;
	}
	bool CanvasPanGestureDetector::Scrolling() const noexcept { return scrollSent_; }
	void CanvasPanFlingBooster::Configure(const CanvasPanMotionContext& context) noexcept
	{
		minimumFlingSpeedSquared_ = context.boostMinimumFlingVelocity * context.boostMinimumFlingVelocity;
		minimumScrollSpeedSquared_ = context.boostMinimumScrollVelocity * context.boostMinimumScrollVelocity;
		velocityLimit_ = context.velocityLimit;
	}
	bool CanvasPanFlingBooster::ShouldBoostFling(const CanvasPanGestureEvent& event) const noexcept
	{
		return !PanZero(startingVelocity_) && event.sourceDevice == sourceDevice_ &&
			event.modifiers == modifiers_ && cutoff_ && PanMicroseconds(event.timeSeconds) <= PanMicroseconds(*cutoff_) &&
			PanDot(startingVelocity_, event.velocity) > 0.0 &&
			PanDot(currentVelocity_, currentVelocity_) >= minimumFlingSpeedSquared_ &&
			PanDot(event.velocity, event.velocity) >= minimumFlingSpeedSquared_;
	}
	CanvasVector CanvasPanFlingBooster::GetVelocityForFlingStart(const CanvasPanGestureEvent& event) noexcept
	{
		CanvasVector velocity = event.velocity;
		const double speed = std::hypot(velocity.x, velocity.y);
		if (speed > 0.0)
		{
			// 旧动画已停止；仅投影当前残余，不能恢复历史初速或带入旧横向分量。
			const double ux = velocity.x / speed, uy = velocity.y / speed;
			const double residual = ShouldBoostFling(event) ?
				(std::max)(0.0, currentVelocity_.x * ux + currentVelocity_.y * uy) : 0.0;
			const double bounded = (std::min)(speed + residual, velocityLimit_);
			velocity = ToCanvasVector(ux * bounded, uy * bounded);
		}
		Reset(); startingVelocity_ = currentVelocity_ = velocity;
		sourceDevice_ = event.sourceDevice; modifiers_ = event.modifiers;
		return velocity;
	}
	void CanvasPanFlingBooster::ObserveGestureEvent(const CanvasPanGestureEvent& event) noexcept
	{
		if (PanZero(startingVelocity_)) return;
		if ((cutoff_ && PanMicroseconds(event.timeSeconds) > PanMicroseconds(*cutoff_)) || event.sourceDevice != sourceDevice_) { Reset(); return; }
		switch (event.type)
		{
		case CanvasPanGestureType::ScrollBegin:
			cutoff_ = event.timeSeconds + kCanvasPanBoostTimeoutSeconds;
			break;
		case CanvasPanGestureType::ScrollUpdate:
			if (event.inertial || !cutoff_) return;
			if (PanDot(startingVelocity_, event.delta) <= 0.0) { Reset(); return; }
			if (previousScrollTime_)
			{
				// Chromium TimeDelta 使用整数微秒，避免绝对秒相减把150边界误判为149.999… 。
				const double dt = static_cast<double>(PanMicroseconds(event.timeSeconds) -
					PanMicroseconds(*previousScrollTime_)) / 1000000.0;
				if (dt >= kCanvasPanBoostMinimumIntervalSeconds)
				{
					const float scale = static_cast<float>(1.0 / dt);
					const CanvasVector scrollVelocity{ event.delta.x * scale, event.delta.y * scale };
					if (PanDot(scrollVelocity, scrollVelocity) < minimumScrollSpeedSquared_)
					{ Reset(); return; }
				}
			}
			previousScrollTime_ = event.timeSeconds; cutoff_ = event.timeSeconds + kCanvasPanBoostTimeoutSeconds;
			break;
		case CanvasPanGestureType::ScrollEnd:
			previousScrollTime_.reset();
			break;
		case CanvasPanGestureType::FlingCancel:
			if (event.preventBoosting) { Reset(); return; }
			previousScrollTime_.reset(); cutoff_ = event.timeSeconds + kCanvasPanBoostTimeoutSeconds;
			break;
		default: break;
		}
	}
	void CanvasPanFlingBooster::ObserveProgressFling(CanvasVector velocity) noexcept
	{ if (!PanZero(startingVelocity_)) currentVelocity_ = velocity; }
	void CanvasPanFlingBooster::Reset() noexcept
	{ startingVelocity_ = currentVelocity_ = {}; cutoff_.reset(); previousScrollTime_.reset(); sourceDevice_ = modifiers_ = 0; }
	CanvasVector CanvasPanFlingBooster::StartingVelocity() const noexcept { return startingVelocity_; }
	CanvasVector CanvasPanFlingBooster::CurrentVelocity() const noexcept { return currentVelocity_; }
	double CanvasPanFlingBooster::CutoffSeconds() const noexcept { return cutoff_.value_or(0.0); }

	void CanvasPanFlingCurve::Start(CanvasVector velocity, CanvasVector viewport, CanvasPanFrictionParameters parameters) noexcept
	{
		*this = CanvasPanFlingCurve{};
		if (!std::isfinite(parameters.tauSeconds) || !std::isfinite(parameters.deceleration) ||
			parameters.tauSeconds <= 0 || parameters.deceleration <= 0) return;
		parameters_ = parameters;
		const float vmax = (std::max)(std::abs(velocity.x), std::abs(velocity.y));
		if (!(vmax > 0.0f) || !std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
			!std::isfinite(vmax) || viewport.x <= 0.0f || viewport.y <= 0.0f) return;
		frictionInitialSpeed_ = std::hypot(velocity.x, velocity.y);
		frictionDirection_ = ToCanvasVector(velocity.x / frictionInitialSpeed_, velocity.y / frictionInitialSpeed_);
		const double scale = parameters_.deceleration * parameters_.tauSeconds;
		duration_ = parameters_.tauSeconds * std::log1p(frictionInitialSpeed_ / scale);
		const double distance = frictionInitialSpeed_ * parameters_.tauSeconds - scale * duration_;
		distance_ = ToCanvasVector(frictionDirection_.x * distance, frictionDirection_.y * distance);
	}

	bool CanvasPanFlingCurve::Advance(double elapsed, CanvasVector& offset, CanvasVector& velocity) noexcept
	{
		offset = {}; velocity = {};
		if (elapsed < 0.0) return true;
		if (!(duration_ > 0.0) || elapsed >= duration_) { offset = distance_; return false; }
		const double tau = parameters_.tauSeconds;
		const double scale = parameters_.deceleration * tau;
		const double decay = std::exp(-elapsed / tau);
		const double distance = (frictionInitialSpeed_ + scale) * tau * -std::expm1(-elapsed / tau) - scale * elapsed;
		const double speed = (std::max)(0.0, (frictionInitialSpeed_ + scale) * decay - scale);
		offset = ToCanvasVector(frictionDirection_.x * distance, frictionDirection_.y * distance);
		velocity = ToCanvasVector(frictionDirection_.x * speed, frictionDirection_.y * speed);
		return true;
	}
	void CanvasPanFlingCurve::ClearAxis(bool x, bool y) noexcept
	{
		if (x) { distance_.x = frictionDirection_.x = 0.0f; }
		if (y) { distance_.y = frictionDirection_.y = 0.0f; }
	}
	double CanvasPanFlingCurve::Duration() const noexcept { return duration_; }
	CanvasVector CanvasPanFlingCurve::Distance() const noexcept { return distance_; }
	void CanvasPanFlingController::Start(const CanvasPanGestureEvent& event, double lastScroll,
		CanvasVector viewport, CanvasPanFlingBooster& booster, CanvasPanFrictionParameters parameters) noexcept
	{
		Cancel();
		velocity_ = booster.GetVelocityForFlingStart(event);
		if (PanZero(velocity_) || viewport.x <= 0.0f || viewport.y <= 0.0f)
		{ StopFling(booster); return; }
		curve_.Start(velocity_, viewport, parameters);
		startSeconds_ = lastScroll;
		active_ = true;
	}
	CanvasVector CanvasPanFlingController::Progress(double now, CanvasPanFlingBooster& booster) noexcept
	{
		if (!active_) return {};
		if (now < lastProgressSeconds_ || now <= startSeconds_) return {};
		CanvasVector offset, velocity;
		const bool active = curve_.Advance(now - startSeconds_, offset, velocity);
		CanvasVector delta{ offset.x - cumulative_.x, offset.y - cumulative_.y };
		cumulative_ = offset; velocity_ = velocity;
		if (clearX_) delta.x = velocity_.x = 0.0f;
		if (clearY_) delta.y = velocity_.y = 0.0f;
		if (!active)
		{
			// 闭式曲线终帧补齐实际剩余位移，不能随呈现帧率丢掉不同的尾巴。
			StopFling(booster); return delta;
		}
		lastProgressSeconds_ = now;
		booster.ObserveProgressFling(velocity_); return delta;
	}
	void CanvasPanFlingController::Cancel() noexcept
	{
		active_ = clearX_ = clearY_ = false;
		velocity_ = cumulative_ = {}; startSeconds_ = lastProgressSeconds_ = 0.0;
	}
	void CanvasPanFlingController::StopFling(CanvasPanFlingBooster& booster) noexcept { Cancel(); booster.Reset(); }
	void CanvasPanFlingController::ClearAxis(bool x, bool y, CanvasPanFlingBooster& booster) noexcept
	{
		clearX_ |= x; clearY_ |= y; curve_.ClearAxis(x, y);
		if (x) velocity_.x = cumulative_.x = 0.0f;
		if (y) velocity_.y = cumulative_.y = 0.0f;
		if (PanZero(velocity_)) StopFling(booster);
		else booster.ObserveProgressFling(velocity_);
	}
	bool CanvasPanFlingController::Active() const noexcept { return active_; }
	CanvasVector CanvasPanFlingController::Velocity() const noexcept { return velocity_; }
	CanvasVector CanvasPanFlingController::Offset() const noexcept { return cumulative_; }
	namespace
	{
		CanvasVector PanToDip(const CanvasPanMotionContext& c, CanvasVector v) noexcept
		{ return ToCanvasVector(v.x / c.motionPerDipX, v.y / c.motionPerDipY); }
		CanvasVector PanToMotion(const CanvasPanMotionContext& c, CanvasVector v) noexcept
		{ return ToCanvasVector(v.x * c.motionPerDipX, v.y * c.motionPerDipY); }
		bool SamePanPolicy(CanvasPanMotionContext a, CanvasPanMotionContext b) noexcept
		{
			// 识别补全不改运动；每条释放按手速选择的摩擦也不属于批次签名。
			a.inputMapped = b.inputMapped = false; a.mappingConflict = b.mappingConflict = false;
			if (a.profile != CanvasPanMotionProfile::TouchFallback) a.largeScreen = false;
			if (b.profile != CanvasPanMotionProfile::TouchFallback) b.largeScreen = false;
			if (a.profile != CanvasPanMotionProfile::TouchPhysical) a.touchWeight = 0;
			if (b.profile != CanvasPanMotionProfile::TouchPhysical) b.touchWeight = 0;
			if (a.profile != CanvasPanMotionProfile::TouchFallback || !a.largeScreen) a.intentGain = 1;
			if (b.profile != CanvasPanMotionProfile::TouchFallback || !b.largeScreen) b.intentGain = 1;
			return a == b;
		}
		bool CompatiblePanMetric(const CanvasPanMotionContext& active, const CanvasPanMotionContext& incoming) noexcept
		{
			if (incoming.mappingConflict || active.outputKey != incoming.outputKey) return false;
			if (active.unit == CanvasPanMotionUnit::SystemDip) return true; // 本批不因后来可信指升级。
			return incoming.inputMapped && incoming.unit == CanvasPanMotionUnit::Millimeters &&
				active.motionPerDipX == incoming.motionPerDipX && active.motionPerDipY == incoming.motionPerDipY;
		}
		CanvasPanSourceClass PanSourceClass(uint64_t source) noexcept
		{ return static_cast<CanvasPanSourceClass>(source >> 32); }
		bool PanSourceIsPen(uint64_t source) noexcept
		{ return PanSourceClass(source) == CanvasPanSourceClass::Pen || PanSourceClass(source) == CanvasPanSourceClass::PenTail; }
	}

	CanvasPanFrameOutput CanvasPanDynamics::Snapshot(int64_t frequency, CanvasVector* handMotion) const noexcept
	{
		CanvasPanFrameOutput out;
		out.sourceKey = lastSourceKey_;
		out.heldContacts = out.pointerCount = HeldContactCount(); out.phase = Phase();
		out.scrolling = detector_.Scrolling(); out.contentVelocity = PanToDip(motionContext_, velocity_);
		std::array<CanvasPanPointer, kCanvasPanMaximumContacts> pointers = {};
		size_t count = 0; double vx = 0, vy = 0;
		for (const auto& contact : contacts_)
		{
			if (!contact.held) continue;
			pointers[count] = { contact.pointer.key, contact.pointer.x / motionContext_.motionPerDipX,
				contact.pointer.y / motionContext_.motionPerDipY };
			const auto estimate = releaseTracker_.Estimate(contact.pointer.key, frequency);
			out.pointerDiagnostics[count] = { contact.pointer.key, releaseTracker_.SampleCount(contact.pointer.key),
				PanToDip(motionContext_, ToCanvasVector(estimate.x, estimate.y)) };
			const double age = frequency > 0 ? PanSecondsBetween(lastFrameQpc_,
				releaseTracker_.LastSampleQpc(contact.pointer.key), frequency) : kCanvasPanReleaseZeroAgeSeconds;
			const double gate = std::clamp((kCanvasPanReleaseZeroAgeSeconds - age) /
				(kCanvasPanReleaseZeroAgeSeconds - kCanvasPanReleaseFullAgeSeconds), 0.0, 1.0);
			const double speed = std::hypot(estimate.x, estimate.y);
			const auto tail = releaseTracker_.RecentSpeedLimit(contact.pointer.key, frequency, ToCanvasVector(estimate.x, estimate.y));
			const auto support = releaseTracker_.ReleaseSupport(contact.pointer.key, frequency, estimate);
			const double limit = (std::min)({ motionContext_.velocityLimit, tail.value_or(motionContext_.velocityLimit),
				support.valid ? support.speedUpperBound : 0 });
			const double scale = speed > limit ? limit / speed : 1;
			// 显式更新的同位Touch观察用于呈现静止；无新raw的普通低报点帧不清估计。
			if (estimate.valid && !releaseTracker_.HasStationaryObservation(contact.pointer.key))
			{ vx += estimate.x * gate * scale; vy += estimate.y * gate * scale; }
			out.handVelocityKnown |= estimate.valid; ++count;
		}
		// slop内的短强证据只用于释放资格，未实际scroll时不能给预取/blur虚构呈现速度。
		const bool presentMotion = detector_.Scrolling() ||
			(motionContext_.profile != CanvasPanMotionProfile::TouchPhysical && motionContext_.profile != CanvasPanMotionProfile::TouchFallback);
		const auto motionVelocity = count && presentMotion ? ToCanvasVector(vx / count, vy / count) : CanvasVector{};
		if (handMotion) *handMotion = motionVelocity;
		out.handVelocity = PanToDip(motionContext_, motionVelocity);
		if (count)
		{
			out.focus = PanFocus(std::span(pointers).first(count));
			// 异向标尺下先恢复每指 DIP，再求欧氏 span，不能除一个平均倍率。
			for (size_t i = 0; i < count; ++i)
				out.span += std::hypot(pointers[i].x - out.focus.x, pointers[i].y - out.focus.y) / count;
		}
		out.boosterStartingVelocity = PanToDip(motionContext_, booster_.StartingVelocity());
		out.boosterCurrentVelocity = PanToDip(motionContext_, booster_.CurrentVelocity());
		out.boosterCutoffSeconds = booster_.CutoffSeconds(); out.flingOffset = PanToDip(motionContext_, fling_.Offset());
		return out;
	}

	void CanvasPanDynamics::Dispatch(const CanvasPanGestureBatch& batch,
		const CanvasPanFrameInput& input, CanvasPanFrameOutput& out) noexcept
	{
		for (const auto& event : std::span(batch.events).first(batch.count))
		{
			if (event.type != CanvasPanGestureType::FlingCancel || fling_.Active()) booster_.ObserveGestureEvent(event);
			switch (event.type)
			{
			case CanvasPanGestureType::FlingCancel:
				fling_.Cancel(); velocity_ = {}; break;
			case CanvasPanGestureType::ScrollEnd:
				booster_.Reset(); break;
			case CanvasPanGestureType::ScrollUpdate:
			{
				RecordTouchScroll(event.delta, static_cast<int64_t>(std::llround(event.timeSeconds * input.qpcFrequency)), input, out);
				// 一帧可包含毫米触摸和 DIP 鼠标，必须在事件边界恢复后再累加。
				const auto delta = PanToDip(motionContext_, event.delta);
				out.contentDelta.x += delta.x; out.contentDelta.y += delta.y; break;
			}
			case CanvasPanGestureType::FlingStart:
				out.released = true;
				if (out.touchDecision.mode == TouchReleaseMode::NotEvaluated ||
					(motionContext_.profile != CanvasPanMotionProfile::TouchPhysical && motionContext_.profile != CanvasPanMotionProfile::TouchFallback))
				{ const auto v = PanToDip(motionContext_, event.velocity); out.releaseSpeed = std::hypot(v.x, v.y); }
				if ((motionContext_.profile == CanvasPanMotionProfile::TouchPhysical || motionContext_.profile == CanvasPanMotionProfile::TouchFallback) &&
					out.touchDecision.mode != TouchReleaseMode::NotEvaluated)
				{
					// Touch纯决策已给最终seed/响应；不得再次加旧残余或由seed触发快甩增强。
					booster_.Reset(); out.frictionParameters = out.touchDecision.parameters;
				}
				else out.frictionParameters = ResolveCanvasPanFriction(motionContext_, std::hypot(event.velocity.x, event.velocity.y));
				fling_.Start(event, static_cast<double>(input.frameQpc) / input.qpcFrequency,
					PanToMotion(motionContext_, input.viewportSizeDip), booster_, out.frictionParameters);
				out.boostedReleaseVelocity = PanToDip(motionContext_, fling_.Velocity()); break;
			default: break;
			}
		}
	}

	void CanvasPanDynamics::BeginTouchLeg(bool requireIndependent, int64_t qpc) noexcept
	{
		touch_.tag.leg = ++touchLegSerial_; touch_.candidate = {};
		touch_.requestedNet = touch_.confirmedNet = {}; touch_.scrolled = touch_.budgetConfirmed = false;
		touch_.lastScrollQpc = 0; touch_.residualEligible = false;
		touch_.distanceEligible = !requireIndependent; touch_.confirmedStopped = requireIndependent;
		if (qpc) touch_.intentQpc = qpc;
	}
	void CanvasPanDynamics::RecordTouchScroll(CanvasVector delta, int64_t qpc,
		const CanvasPanFrameInput& input, CanvasPanFrameOutput& out) noexcept
	{
		const bool touch = motionContext_.profile == CanvasPanMotionProfile::TouchPhysical || motionContext_.profile == CanvasPanMotionProfile::TouchFallback;
		if (touch)
		{
			const double oldLength = std::hypot(touch_.requestedNet.x, touch_.requestedNet.y);
			if (oldLength > 0 && (delta.x * touch_.requestedNet.x + delta.y * touch_.requestedNet.y) / oldLength <= -motionContext_.reversalDistance)
				BeginTouchLeg(false, qpc); // 新末指的第一次反向Move也能确认组新腿，不借已退出主指旧速。
			if (touch_.lastScrollQpc && PanSecondsBetween(qpc, touch_.lastScrollQpc, input.qpcFrequency) >= kCanvasPanReleaseZeroAgeSeconds) BeginTouchLeg(true, qpc);
			touch_.requestedNet.x += delta.x; touch_.requestedNet.y += delta.y;
			const int64_t previousScroll = touch_.lastScrollQpc;
			if (previousScroll && !PanZero(delta))
			{
				// 与既有booster共用微秒/最短间隔语义；只让真实慢scroll取消接力，零位移平台不走这里。
				const double dt = static_cast<double>(PanMicroseconds(static_cast<double>(qpc) / input.qpcFrequency) -
					PanMicroseconds(static_cast<double>(previousScroll) / input.qpcFrequency)) / 1000000.0;
				if (dt >= kCanvasPanBoostMinimumIntervalSeconds)
				{
					const float scale = static_cast<float>(1.0 / dt);
					const CanvasVector scrollVelocity{ delta.x * scale, delta.y * scale };
					if (PanDot(scrollVelocity, scrollVelocity) < motionContext_.boostMinimumScrollVelocity * motionContext_.boostMinimumScrollVelocity)
					{
						touch_.residualEligible = touch_.distanceEligible = touch_.budgetConfirmed = false;
						touch_.confirmedNet = {};
						// 本帧若已含减速前位移，整帧ACK不能把刚退役的旧预算再领回来。
						if (out.motionFeedback.present) out.motionFeedback.eligible = false;
					}
				}
			}
			touch_.scrolled = true; touch_.lastScrollQpc = qpc;
			if (!CacheTouchEvidence(qpc, input.qpcFrequency) && touch_.candidate.valid)
			{
				if (PanSecondsBetween(qpc, touch_.candidate.motionQpc, input.qpcFrequency) >= kCanvasPanReleaseZeroAgeSeconds) touch_.candidate = {};
				else if (previousScroll)
				{
					const double dt = PanSecondsBetween(qpc, previousScroll, input.qpcFrequency);
					const double speed = std::hypot(touch_.candidate.velocity.x, touch_.candidate.velocity.y);
					if (dt >= kCanvasPanReleaseMinimumIntervalSeconds && speed > 0)
					{
						const double along = (delta.x * touch_.candidate.velocity.x + delta.y * touch_.candidate.velocity.y) / speed;
						const double upper = ((std::max)(0.0, along) + motionContext_.reversalDistance +
							std::numeric_limits<float>::epsilon() * std::hypot(touch_.requestedNet.x, touch_.requestedNet.y)) /
							(dt - 1.0 / input.qpcFrequency);
						if (upper < speed)
						{
							touch_.candidate.velocity = ToCanvasVector(touch_.candidate.velocity.x * upper / speed, touch_.candidate.velocity.y * upper / speed);
							touch_.candidate.supportLimited = true; touch_.candidate.motionQpc = qpc; touch_.candidate.contactKey = 0;
						}
					}
				}
			}
		}
		const TouchMotionTag tag = touch ? touch_.tag : TouchMotionTag{};
		if (!out.motionFeedback.present)
			out.motionFeedback = { tag, input.frameQpc, qpc, true, touch };
		else
		{
			if (out.motionFeedback.tag != tag) out.motionFeedback.eligible = false;
			out.motionFeedback.scrollQpc = qpc;
		}
	}
	bool CanvasPanDynamics::CacheTouchEvidence(int64_t motionQpc, int64_t frequency) noexcept
	{
		double rawX = 0, rawY = 0, vx = 0, vy = 0; size_t count = 0;
		bool known = false, shortOnly = true, limited = false; uint64_t singleKey = 0;
		// 只在实际ScrollUpdate采样内容/focus速度。拓扑不采样，未知/静止指计入分母，不能取最快个人。
		for (const auto& contact : contacts_)
		{
			if (!contact.held) continue; ++count; singleKey = contact.pointer.key;
			const auto raw = releaseTracker_.Estimate(contact.pointer.key, frequency);
			const auto support = releaseTracker_.ReleaseSupport(contact.pointer.key, frequency, raw);
			const double speed = std::hypot(raw.x, raw.y);
			if (!raw.valid || !support.valid || !(speed > 0) || releaseTracker_.HasStationaryObservation(contact.pointer.key)) continue;
			const auto tail = releaseTracker_.RecentSpeedLimit(contact.pointer.key, frequency, ToCanvasVector(raw.x, raw.y));
			const double baseline = (std::min)({ speed, tail.value_or(speed), motionContext_.velocityLimit });
			const double bounded = (std::min)(baseline, support.speedUpperBound);
			const double age = PanSecondsBetween(motionQpc, releaseTracker_.LastSampleQpc(contact.pointer.key), frequency);
			const double fresh = std::clamp((kCanvasPanReleaseZeroAgeSeconds - age) /
				(kCanvasPanReleaseZeroAgeSeconds - kCanvasPanReleaseFullAgeSeconds), 0.0, 1.0);
			rawX += raw.x; rawY += raw.y; vx += raw.x * bounded / speed * fresh; vy += raw.y * bounded / speed * fresh;
			known = true; limited |= support.speedUpperBound < baseline;
			shortOnly &= PanSecondsBetween(support.lastQpc, support.firstQpc, frequency) < kCanvasPanReleaseMinimumSpanSeconds;
		}
		if (!count || !known) return false;
		const auto velocity = ToCanvasVector(vx / count, vy / count);
		if (!PanZero(velocity) && PanDot(velocity, touch_.requestedNet) < 0) return false;
		touch_.distanceEligible = true; touch_.confirmedStopped = false;
		touch_.candidate = { ToCanvasVector(rawX / count, rawY / count), velocity, motionQpc,
			count == 1 ? singleKey : 0, true, shortOnly, limited };
		return true;
	}
	bool CanvasPanDynamics::ConfirmAppliedMotion(const TouchMotionFeedback& f, CanvasVector actualDip) noexcept
	{
		if (!f.present || !f.eligible || f.tag != touch_.tag || !f.tag.group || !HeldContactCount() ||
			f.frameQpc != lastFrameQpc_ || f.frameQpc <= touch_.lastConfirmedFrame || f.scrollQpc > f.frameQpc ||
			!std::isfinite(actualDip.x) || !std::isfinite(actualDip.y)) return false;
		// 仅累计已成功应用的内容净位移；确认时刻不能续租运动/停止时钟。
		touch_.confirmedNet.x += static_cast<float>(actualDip.x * motionContext_.motionPerDipX);
		touch_.confirmedNet.y += static_cast<float>(actualDip.y * motionContext_.motionPerDipY);
		touch_.lastConfirmedFrame = f.frameQpc; touch_.budgetConfirmed = true; return true;
	}

	CanvasPanFrameOutput CanvasPanDynamics::Step(const CanvasPanFrameInput& input) noexcept
	{
		if (input.penContact) { Stop(); return Snapshot(input.qpcFrequency); }
		// 时钟未推进不是准入/容量失败；调用方须保留尚未消费的真实事件。
		if (input.qpcFrequency <= 0 || input.frameQpc <= 0 || (lastFrameQpc_ && input.frameQpc <= lastFrameQpc_))
			return Snapshot(input.qpcFrequency);
		const bool adapted = !input.motionContexts.empty();
		const size_t downs = static_cast<size_t>(std::count_if(input.contacts.begin(), input.contacts.end(),
			[](const auto& s) { return s.phase == CanvasPanContactPhase::Down; }));
		// 在任何状态变化/消费之前校验 caller 缓冲；不返回部分准入结果。
		if (adapted && (input.admissionDecisions.size() < downs || rejectedContacts_.size() + downs > kCanvasPanMaximumRejectedContacts))
		{ auto out = Snapshot(input.qpcFrequency); out.admissionFailed = true; return out; }
		if (adapted && downs)
		{
			try { rejectedContacts_.reserve(rejectedContacts_.size() + downs); earlyTerminalKeys_.reserve(downs); }
			catch (...) { auto out = Snapshot(input.qpcFrequency); out.admissionFailed = true; return out; }
		}
		earlyTerminalKeys_.clear();
		lastFrameQpc_ = input.frameQpc;
		CanvasPanFrameOutput events;
		const auto orderedBefore = [](const auto& a, const auto& b)
		{
			if (a.qpc != b.qpc) return a.qpc < b.qpc;
			return static_cast<unsigned>(a.phase) < static_cast<unsigned>(b.phase);
		};
		std::span<const CanvasPanContactSample> samples = input.contacts;
		std::vector<CanvasPanContactSample> sortedSamples;
		if (!std::is_sorted(samples.begin(), samples.end(), orderedBefore))
		{
			sortedSamples.assign(samples.begin(), samples.end());
			std::stable_sort(sortedSamples.begin(), sortedSamples.end(), orderedBefore); samples = sortedSamples;
		}
		size_t sampleIndex = 0;
		const auto terminalBeforeDown = [&](uint64_t key)
		{ return std::find(earlyTerminalKeys_.begin(), earlyTerminalKeys_.end(), key) != earlyTerminalKeys_.end(); };
		const auto decide = [&](const CanvasPanContactSample& sample, CanvasPanAdmissionReason reason, bool suppressesPen = false)
		{
			if (!adapted) return;
			const bool accepted = reason == CanvasPanAdmissionReason::Accepted;
			input.admissionDecisions[events.admissionDecisionCount++] = { sample.key, sample.qpc, sample.sourceKey, accepted, reason };
			if (!accepted && !terminalBeforeDown(sample.key) && std::none_of(rejectedContacts_.begin(), rejectedContacts_.end(),
				[&](const auto& c) { return c.key == sample.key; })) rejectedContacts_.push_back({ sample.key, reason, suppressesPen });
		};
		std::array<CanvasPanPointer, kCanvasPanMaximumContacts> pointers = {};
		const auto currentPointers = [&]()
		{
			size_t count = 0;
			for (const auto& contact : contacts_) if (contact.held) pointers[count++] = contact.pointer;
			return std::span<const CanvasPanPointer>(pointers).first(count);
		};
		int64_t moveQpc = 0;
		const auto flushMove = [&]()
		{
			if (!moveQpc) return;
			Dispatch(detector_.Move(moveQpc, currentPointers(), input.qpcFrequency), input, events); moveQpc = 0;
		};
		for (sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
		{
			const auto& sourceSample = samples[sampleIndex];
			auto sample = sourceSample;
			const bool terminal = sample.phase == CanvasPanContactPhase::Up || sample.phase == CanvasPanContactPhase::Cancelled;
			auto ignored = std::find_if(rejectedContacts_.begin(), rejectedContacts_.end(), [&](const auto& c) { return c.key == sample.key; });
			if (ignored != rejectedContacts_.end() && terminal)
			{
				// 终态身份足以退休拒收屏障；坏坐标/时间不能让它永久挡住后来的笔。
				rejectedContacts_.erase(ignored); ++events.rejectedSamples; continue;
			}
			auto found = std::find_if(contacts_.begin(), contacts_.end(), [&](const Contact& c) { return c.held && c.pointer.key == sample.key; });
			if (terminal && found == contacts_.end())
			{
				const auto later = samples.subspan(sampleIndex + 1);
				if (std::any_of(later.begin(), later.end(), [&](const auto& s) { return s.key == sample.key && s.phase == CanvasPanContactPhase::Down; }))
				{
					// 仅坏终态会排在同身份 Down 前；记住闭合，不改正常 QPC/phase 次序。
					if (!terminalBeforeDown(sample.key))
					{
						try { earlyTerminalKeys_.push_back(sample.key); }
						catch (...) { Stop(); auto out = Snapshot(input.qpcFrequency); out.admissionFailed = true; return out; }
					}
					++events.rejectedSamples; continue;
				}
			}
			const bool invalidPayload = sample.qpc <= 0 || sample.qpc > input.frameQpc || !std::isfinite(sample.x) || !std::isfinite(sample.y);
			const bool invalidTerminalPayload = terminal && found != contacts_.end() && invalidPayload;
			if (invalidPayload && !invalidTerminalPayload)
			{
				if (sample.phase == CanvasPanContactPhase::Down) decide(sample, CanvasPanAdmissionReason::InvalidContext,
					PanSourceIsPen(sample.sourceKey) && PanSourceClass(lastSourceKey_) == CanvasPanSourceClass::Touch && HeldContactCount() != 0);
				++events.rejectedSamples; continue;
			}
			if (invalidTerminalPayload)
			{
				// 已准入 Up 只负责闭合；沿最后已知点取消，不拿异常终态补几何或估速。
				sample.x = found->pointer.x / motionContext_.motionPerDipX;
				sample.y = found->pointer.y / motionContext_.motionPerDipY;
				sample.qpc = found->lastQpc;
			}
			if (ignored != rejectedContacts_.end())
			{
				if (sample.phase == CanvasPanContactPhase::Down) decide(sample, ignored->reason);
				++events.rejectedSamples; continue;
			}
			if (sample.phase == CanvasPanContactPhase::Down)
			{
				if (found != contacts_.end()) { decide(sample, CanvasPanAdmissionReason::Accepted); continue; }
				const bool first = HeldContactCount() == 0;
				CanvasPanMotionContext proposed;
				if (adapted)
				{
					const bool pen = PanSourceIsPen(sample.sourceKey);
					const bool suppressedPen = std::any_of(rejectedContacts_.begin(), rejectedContacts_.end(), [](const auto& c) { return c.suppressesPen; });
					if ((!first && PanSourceClass(sample.sourceKey) != PanSourceClass(lastSourceKey_)) || (pen && suppressedPen))
					{
						decide(sample, CanvasPanAdmissionReason::ClassConflict,
							pen && (suppressedPen || PanSourceClass(lastSourceKey_) == CanvasPanSourceClass::Touch));
						++events.rejectedSamples; continue;
					}
					if (sample.motionContextIndex >= input.motionContexts.size() || !IsCanvasPanMotionContextValid(input.motionContexts[sample.motionContextIndex]))
					{ decide(sample, CanvasPanAdmissionReason::InvalidContext); ++events.rejectedSamples; continue; }
					proposed = input.motionContexts[sample.motionContextIndex];
					if ((first && proposed.unit == CanvasPanMotionUnit::Millimeters && !proposed.inputMapped) ||
						(!first && !CompatiblePanMetric(motionContext_, proposed)))
					{ decide(sample, CanvasPanAdmissionReason::MetricConflict); ++events.rejectedSamples; continue; }
				}
				found = std::find_if(contacts_.begin(), contacts_.end(), [](const Contact& c) { return !c.held; });
				if (found == contacts_.end()) { decide(sample, CanvasPanAdmissionReason::Capacity); ++events.rejectedSamples; continue; }
				flushMove();
				if (first)
				{
					// 先结算旧域，真正的新 Down 才能换单位或隔离旧响应残余。
					if (!SamePanPolicy(motionContext_, proposed)) StopFling();
					const CanvasVector residual = fling_.Active() && SamePanPolicy(motionContext_, proposed) && sample.sourceKey == lastSourceKey_ ? fling_.Velocity() : CanvasVector{};
					if (!SamePanPolicy(motionContext_, proposed)) ++touchMetricSerial_;
					touch_ = {};
					if (proposed.profile == CanvasPanMotionProfile::TouchPhysical || proposed.profile == CanvasPanMotionProfile::TouchFallback)
					{
						touch_.tag = { ++touchGroupSerial_, ++touchLegSerial_, touchEpoch_, touchMetricSerial_ };
						touch_.residual = residual; touch_.residualEligible = !PanZero(residual); touch_.intentQpc = sample.qpc;
					}
					motionContext_ = proposed; releaseTracker_.Configure(proposed); detector_.Configure(proposed); booster_.Configure(proposed);
					releaseTracker_.Clear();
				}
				sample.x *= motionContext_.motionPerDipX; sample.y *= motionContext_.motionPerDipY;
				if (sample.sourceKey != lastSourceKey_) booster_.Reset();
				lastSourceKey_ = sample.sourceKey;
				*found = { true, { sample.key, sample.x, sample.y }, sample.qpc };
				releaseTracker_.ClearPointer(sample.key);
				Dispatch(detector_.Down(sample.qpc, currentPointers(), input.qpcFrequency), input, events);
				releaseTracker_.AddSample(sample.key, { sample.qpc, sample.x, sample.y }, input.qpcFrequency);
				if (touch_.tag.group) touch_.lastRawQpc = (std::max)(touch_.lastRawQpc, sample.qpc);
				decide(sourceSample, CanvasPanAdmissionReason::Accepted); ++events.sampleCount;
				if (terminalBeforeDown(sample.key))
				{
					const bool last = HeldContactCount() == 1;
					Dispatch(detector_.Up(sample.qpc, currentPointers(), sample.key, true, input.qpcFrequency, {}), input, events);
					releaseTracker_.ClearPointer(sample.key); found->held = false; booster_.Reset();
					if (last)
					{
						events.released = true; events.releaseSourceKey = sample.sourceKey;
						ResetTouchReleaseMetadata(events);
						events.releaseMotionContext = motionContext_; events.releaseReason = CanvasPanReleaseReason::TimingRejected;
						events.frictionParameters = ResolveCanvasPanFriction(motionContext_, 0);
						events.rawReleaseVelocity = events.selectedReleaseVelocity = events.boostedReleaseVelocity = events.releaseMotionVelocity = {};
						events.releaseSpeed = events.releaseAgeSeconds = events.releaseTailSpeedLimit = events.releaseSpanSeconds = 0;
						events.releaseSampleCount = 0; events.releaseTailSpeedLimited = false;
						events.releaseContactKey = sample.key; events.releaseSupport = {};
						events.releaseSupportLimited = events.releaseOutputLimited = events.releaseBoostSuppressed = false;
						events.releaseMotionAgeLimited = false; events.releaseMotionSampleCount = 0;
						events.releaseLastRawQpc = events.releaseLastMotionQpc = 0;
					}
				}
				continue;
			}
			sample.x *= motionContext_.motionPerDipX; sample.y *= motionContext_.motionPerDipY;
			if (sample.phase == CanvasPanContactPhase::Move)
			{
				if (found == contacts_.end() || sample.qpc <= found->lastQpc) { ++events.rejectedSamples; continue; }
				if (moveQpc && moveQpc != sample.qpc) flushMove();
				const bool rawGap = PanSecondsBetween(sample.qpc, found->lastQpc, input.qpcFrequency) >= kCanvasPanTrackerGapSeconds;
				const auto result = releaseTracker_.AddSample(sample.key, { sample.qpc, sample.x, sample.y }, input.qpcFrequency);
				if (touch_.tag.group)
				{
					if (rawGap || result == CanvasPanSampleDisposition::Reversed) BeginTouchLeg(rawGap, sample.qpc);
					else if (!touch_.confirmedStopped && touch_.intentQpc && PanSecondsBetween(sample.qpc,
						touch_.lastScrollQpc ? touch_.lastScrollQpc : touch_.intentQpc, input.qpcFrequency) >= kCanvasPanReleaseZeroAgeSeconds)
						BeginTouchLeg(true, sample.qpc);
				}
				if (touch_.tag.group)
				{ touch_.lastRawQpc = (std::max)(touch_.lastRawQpc, sample.qpc); }
				if (result == CanvasPanSampleDisposition::Reversed)
				{
					booster_.Reset(); ++events.reversalCount;
				}
				found->pointer = { sample.key, sample.x, sample.y }; found->lastQpc = sample.qpc;
				moveQpc = sample.qpc; ++events.sampleCount; continue;
			}
			flushMove();
			if (found == contacts_.end()) { ++events.rejectedSamples; continue; }
			const bool invalidTerminalTime = invalidTerminalPayload || sample.qpc < found->lastQpc;
			if (invalidTerminalTime) ++events.rejectedSamples;
			found->pointer = { sample.key, sample.x, sample.y };
			const bool last = HeldContactCount() == 1;
			const bool cancelled = sample.phase == CanvasPanContactPhase::Cancelled || invalidTerminalTime;
			const int64_t latest = releaseTracker_.LastMotionQpc(sample.key);
			const double age = latest ? PanSecondsBetween(sample.qpc, latest, input.qpcFrequency) : 0;
			const auto raw = releaseTracker_.Estimate(sample.key, input.qpcFrequency);
			auto selected = raw.valid ? raw : CanvasPanVelocityEstimate{};
			const bool touchRelease = motionContext_.profile == CanvasPanMotionProfile::TouchPhysical || motionContext_.profile == CanvasPanMotionProfile::TouchFallback;
			const double rawAge = PanSecondsBetween(sample.qpc, releaseTracker_.LastSampleQpc(sample.key), input.qpcFrequency);
			const double gate = std::clamp((kCanvasPanReleaseZeroAgeSeconds - (touchRelease ? rawAge : age)) /
				(kCanvasPanReleaseZeroAgeSeconds - kCanvasPanReleaseFullAgeSeconds), 0.0, 1.0);
			const double speed = std::hypot(raw.x, raw.y);
			const auto tailLimit = raw.valid ? releaseTracker_.RecentSpeedLimit(sample.key, input.qpcFrequency, ToCanvasVector(raw.x, raw.y)) : std::optional<double>{};
			const auto support = releaseTracker_.ReleaseSupport(sample.key, input.qpcFrequency, raw);
			const double beforeSupport = (std::min)(speed, tailLimit.value_or(speed));
			const bool supportLimited = raw.valid && speed > 0 && (!support.valid || support.speedUpperBound < (std::min)(beforeSupport, motionContext_.velocityLimit));
			const double supported = (std::min)(beforeSupport, support.valid ? support.speedUpperBound : 0);
			const double limit = (std::min)(motionContext_.velocityLimit, supported);
			const double scale = speed > limit ? limit / speed : 1;
			selected.x *= gate * scale; selected.y *= gate * scale;
			// 质量降级不能被旧残余再次抬高；单纯超出输出cap不清历史或续甩资格。
			const bool touchMotionAgeLimited = false; // Touch平台/明确停住由唯一纯决策区分，不再机械衰减。
			if (supportLimited && !touchRelease) booster_.Reset();
			if (touchRelease)
			{
				touch_.lastRawQpc = (std::max)(touch_.lastRawQpc, releaseTracker_.LastSampleQpc(sample.key));
				if (cancelled) BeginTouchLeg(true, sample.qpc);
			}
			if (cancelled) { selected = {}; booster_.Reset(); }
			if (!sample.allowInertia) { if (last) selected = {}; booster_.Reset(); }
			const double selectedSpeed = std::hypot(selected.x, selected.y);
			if (last)
			{
				lastSourceKey_ = sample.sourceKey;
				events.released = true; events.releaseAgeSeconds = age;
				events.releaseSourceKey = sample.sourceKey; events.releaseMotionContext = motionContext_;
				events.releaseMotionVelocity = ToCanvasVector(selected.x, selected.y);
				events.frictionParameters = ResolveCanvasPanFriction(motionContext_, selectedSpeed);
				events.releaseTailSpeedLimit = motionContext_.motionPerDipX == 1 && motionContext_.motionPerDipY == 1 ? tailLimit.value_or(0) :
					tailLimit && speed > 0 ? std::hypot(
					raw.x / speed * *tailLimit / motionContext_.motionPerDipX,
					raw.y / speed * *tailLimit / motionContext_.motionPerDipY) : 0;
				events.releaseTailSpeedLimited = tailLimit && *tailLimit < speed;
				events.releaseContactKey = sample.key; events.releaseSupport = support;
				events.releaseSupportLimited = supportLimited; events.releaseBoostSuppressed = supportLimited || touchMotionAgeLimited;
				events.releaseMotionAgeLimited = touchMotionAgeLimited;
				events.releaseMotionSampleCount = releaseTracker_.MotionSampleCount(sample.key);
				events.releaseLastRawQpc = releaseTracker_.LastSampleQpc(sample.key); events.releaseLastMotionQpc = latest;
				events.releaseOutputLimited = supported > motionContext_.velocityLimit;
				events.rawReleaseVelocity = PanToDip(motionContext_, ToCanvasVector(raw.x, raw.y));
				events.selectedReleaseVelocity = PanToDip(motionContext_, events.releaseMotionVelocity);
				events.releaseSpeed = std::hypot(selected.x / motionContext_.motionPerDipX, selected.y / motionContext_.motionPerDipY);
				events.releaseSampleCount = releaseTracker_.SampleCount(sample.key);
				events.releaseSpanSeconds = releaseTracker_.SampleSpanSeconds(sample.key, input.qpcFrequency);
				events.releaseReason = invalidTerminalTime ? CanvasPanReleaseReason::TimingRejected :
					cancelled ? CanvasPanReleaseReason::Cancelled : !sample.allowInertia ? CanvasPanReleaseReason::Disabled :
					age >= kCanvasPanReleaseZeroAgeSeconds ? CanvasPanReleaseReason::Stale : releaseTracker_.Reason(sample.key, input.qpcFrequency);
				if (events.releaseReason == CanvasPanReleaseReason::Ready && !raw.valid) events.releaseReason = CanvasPanReleaseReason::DirectionConflict;
				else if (events.releaseReason == CanvasPanReleaseReason::Ready && speed > 0 && !support.valid)
					events.releaseReason = CanvasPanReleaseReason::InsufficientSupport;
				else if (events.releaseReason == CanvasPanReleaseReason::Ready && selectedSpeed <= motionContext_.minimumFlingVelocity)
					events.releaseReason = CanvasPanReleaseReason::Stationary;
			}
			else if (!cancelled && selected.valid)
			{
				for (const auto& other : contacts_)
				{
					if (!other.held || other.pointer.key == sample.key) continue;
					const auto velocity = releaseTracker_.Estimate(other.pointer.key, input.qpcFrequency);
					const double otherAge = PanSecondsBetween(sample.qpc, releaseTracker_.LastMotionQpc(other.pointer.key), input.qpcFrequency);
					if (velocity.valid && otherAge < kCanvasPanReleaseZeroAgeSeconds && selected.x * velocity.x + selected.y * velocity.y < 0)
					{ releaseTracker_.Clear(); booster_.Reset(); if (touchRelease) BeginTouchLeg(false, sample.qpc); break; }
				}
			}
			if (last && touchRelease)
			{
				TouchReleaseFacts facts; facts.context = motionContext_; facts.evidence = touch_.candidate;
				facts.direction = touch_.requestedNet; facts.confirmedNet = touch_.confirmedNet; facts.residual = touch_.residual;
				facts.scrolled = touch_.scrolled; facts.cancelled = cancelled; facts.allowInertia = sample.allowInertia;
				facts.residualEligible = touch_.residualEligible; facts.budgetConfirmed = touch_.budgetConfirmed && touch_.distanceEligible; facts.confirmedStopped = touch_.confirmedStopped;
				facts.rawAgeSeconds = touch_.lastRawQpc ? PanSecondsBetween(sample.qpc, touch_.lastRawQpc, input.qpcFrequency) : kCanvasPanReleaseZeroAgeSeconds;
				facts.motionAgeSeconds = facts.scrollAgeSeconds = touch_.lastScrollQpc ? PanSecondsBetween(sample.qpc, touch_.lastScrollQpc, input.qpcFrequency) : kCanvasPanReleaseZeroAgeSeconds;
				facts.platformObserved = touch_.lastRawQpc > touch_.lastScrollQpc;
				if (!PanZero(facts.evidence.velocity) && PanDot(facts.evidence.velocity, facts.direction) <= 0) facts.evidence = {};
				const auto decision = ResolveTouchRelease(facts); events.touchDecision = decision; events.releaseTouchTag = touch_.tag;
				selected = { decision.mode != TouchReleaseMode::Stop, decision.velocitySeed.x, decision.velocitySeed.y };
				const bool measured = decision.mode == TouchReleaseMode::Measured || decision.mode == TouchReleaseMode::ShortSupported;
				events.releaseMotionVelocity = measured ? decision.handVelocity : decision.velocitySeed;
				events.selectedReleaseVelocity = PanToDip(motionContext_, events.releaseMotionVelocity);
				events.releaseSpeed = std::hypot(events.selectedReleaseVelocity.x, events.selectedReleaseVelocity.y);
				events.rawReleaseVelocity = PanToDip(motionContext_, facts.evidence.raw);
				events.releaseRawAgeSeconds = facts.rawAgeSeconds; events.releasePlatformSeconds = facts.motionAgeSeconds;
				events.releaseBoostSuppressed = decision.boostSuppressed; events.releaseMotionAgeLimited = false;
				events.releaseReason = decision.mode != TouchReleaseMode::Stop ? CanvasPanReleaseReason::Ready :
					invalidTerminalTime ? CanvasPanReleaseReason::TimingRejected : cancelled ? CanvasPanReleaseReason::Cancelled : !sample.allowInertia ? CanvasPanReleaseReason::Disabled :
					decision.reason == TouchReleaseReason::ConfirmedStop || decision.reason == TouchReleaseReason::StaleRaw ? CanvasPanReleaseReason::Stale : CanvasPanReleaseReason::InsufficientSupport;
				booster_.Reset(); // 最终seed已包括一次合法残余，执行器不得重复加。
			}
			else if (last) ResetTouchReleaseMetadata(events);
			Dispatch(detector_.Up(sample.qpc, currentPointers(), sample.key, cancelled, input.qpcFrequency, selected), input, events);
			if (last && !fling_.Active())
			{
				booster_.Reset();
				if (events.releaseReason == CanvasPanReleaseReason::Ready) events.releaseReason = input.viewportSizeDip.x <= 0 || input.viewportSizeDip.y <= 0 ?
					CanvasPanReleaseReason::EmptyViewport : CanvasPanReleaseReason::WithinSlop;
			}
			releaseTracker_.ClearPointer(sample.key); found->held = false; ++events.sampleCount;
		}
		flushMove();
		if (input.penHover && fling_.Active()) StopFling();
		const auto flingDelta = PanToDip(motionContext_, fling_.Progress(static_cast<double>(input.frameQpc) / input.qpcFrequency, booster_));
		events.contentDelta.x += flingDelta.x; events.contentDelta.y += flingDelta.y;
		if (!PanZero(flingDelta)) events.motionFeedback.eligible = false;
		velocity_ = fling_.Active() ? fling_.Velocity() : CanvasVector{};
		CanvasVector handMotion;
		auto out = Snapshot(input.qpcFrequency, &handMotion);
		if (!fling_.Active() && HeldContactCount())
		{
			// 同一次拟合交出内部 motion 和公共 DIP，既不重复拟合也不二次逆变换。
			velocity_ = handMotion; out.contentVelocity = out.handVelocity;
		}
		out.contentDelta = events.contentDelta; out.released = events.released;
		out.releaseSpeed = events.releaseSpeed; out.releaseAgeSeconds = events.releaseAgeSeconds; out.sampleCount = events.sampleCount;
		out.rawReleaseVelocity = events.rawReleaseVelocity; out.selectedReleaseVelocity = events.selectedReleaseVelocity;
		out.boostedReleaseVelocity = events.boostedReleaseVelocity; out.releaseReason = events.releaseReason;
		out.releaseSampleCount = events.releaseSampleCount; out.releaseSpanSeconds = events.releaseSpanSeconds;
		out.releaseTailSpeedLimit = events.releaseTailSpeedLimit; out.releaseTailSpeedLimited = events.releaseTailSpeedLimited;
		out.releaseContactKey = events.releaseContactKey; out.releaseSupport = events.releaseSupport;
		out.releaseSupportLimited = events.releaseSupportLimited; out.releaseOutputLimited = events.releaseOutputLimited;
		out.releaseBoostSuppressed = events.releaseBoostSuppressed;
		out.releaseMotionSampleCount = events.releaseMotionSampleCount; out.releaseMotionAgeLimited = events.releaseMotionAgeLimited;
		out.releaseLastRawQpc = events.releaseLastRawQpc; out.releaseLastMotionQpc = events.releaseLastMotionQpc;
		out.rejectedSamples = events.rejectedSamples; out.reversalCount = events.reversalCount; out.timingResetCount = events.timingResetCount;
		out.admissionDecisionCount = events.admissionDecisionCount; out.releaseSourceKey = events.releaseSourceKey;
		out.releaseMotionContext = events.releaseMotionContext; out.releaseMotionVelocity = events.releaseMotionVelocity;
		out.frictionParameters = events.frictionParameters;
		out.touchDecision = events.touchDecision; out.releaseTouchTag = events.releaseTouchTag; out.motionFeedback = events.motionFeedback;
		out.releaseRawAgeSeconds = events.releaseRawAgeSeconds; out.releasePlatformSeconds = events.releasePlatformSeconds;
		return out;
	}

	void CanvasPanDynamics::Stop() noexcept
	{
		contacts_ = {}; rejectedContacts_.clear(); earlyTerminalKeys_.clear(); detector_.Reset(); releaseTracker_.Clear(); fling_.StopFling(booster_); velocity_ = {};
		lastFrameQpc_ = 0; lastSourceKey_ = 0; motionContext_ = {};
		touch_ = {}; ++touchEpoch_;
	}
	void CanvasPanDynamics::StopFling() noexcept
	{
		fling_.StopFling(booster_); velocity_ = {}; ++touchEpoch_;
		if (HeldContactCount() && touch_.tag.group) { BeginTouchLeg(true, touch_.lastRawQpc); touch_.tag.epoch = touchEpoch_; }
		else touch_ = {};
	}
	void CanvasPanDynamics::ClearAxis(bool x, bool y) noexcept
	{
		releaseTracker_.ClearAxis(x, y); fling_.ClearAxis(x, y, booster_);
		if (x) { touch_.confirmedNet.x = touch_.requestedNet.x = touch_.residual.x = touch_.candidate.velocity.x = touch_.candidate.raw.x = 0; }
		if (y) { touch_.confirmedNet.y = touch_.requestedNet.y = touch_.residual.y = touch_.candidate.velocity.y = touch_.candidate.raw.y = 0; }
		if (x) velocity_.x = 0;
		if (y) velocity_.y = 0;
	}
	CanvasPanPhase CanvasPanDynamics::Phase() const noexcept
	{ return HeldContactCount() ? CanvasPanPhase::Locked : (fling_.Active() ? CanvasPanPhase::Coasting : CanvasPanPhase::Idle); }
	bool CanvasPanDynamics::Active() const noexcept { return HeldContactCount() != 0 || fling_.Active(); }
	CanvasVector CanvasPanDynamics::ContentVelocity() const noexcept { return PanToDip(motionContext_, velocity_); }
	size_t CanvasPanDynamics::HeldContactCount() const noexcept
	{ return static_cast<size_t>(std::count_if(contacts_.begin(), contacts_.end(), [](const Contact& c) { return c.held; })); }
	const CanvasPanReleaseVelocityTracker& CanvasPanDynamics::VelocityTracker() const noexcept { return releaseTracker_; }
	const CanvasPanMotionContext& CanvasPanDynamics::MotionContext() const noexcept { return motionContext_; }

	CanvasRenderBudget ComputeCanvasRenderBudget(
		const CanvasRenderBudgetInput& input) noexcept
	{
		CanvasRenderBudget result;
		if (!std::isfinite(input.targetFrameMilliseconds) ||
			!std::isfinite(input.workMilliseconds) ||
			!std::isfinite(input.presentMilliseconds) ||
			!std::isfinite(input.tileEwmaMilliseconds) ||
			input.targetFrameMilliseconds <= 0.0 || input.tileEwmaMilliseconds <= 0.0)
			return result;
		result.milliseconds = std::clamp(input.targetFrameMilliseconds -
			input.workMilliseconds - input.presentMilliseconds - 1.0, 0.0, 4.0);
		result.maximumTiles = static_cast<size_t>(std::floor(
			result.milliseconds / input.tileEwmaMilliseconds));
		return result;
	}

	bool CanvasVisibleClarityAfterAuthoritativeWrite(
		bool wasClear, bool writeSucceeded) noexcept
	{
		return wasClear && writeSucceeded;
	}

	CanvasVector ComputeCanvasPredictionOffset(CanvasVector velocity,
		float viewportWidth, float viewportHeight) noexcept
	{
		if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
			!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) ||
			viewportWidth <= 0.0f || viewportHeight <= 0.0f) return {};
		CanvasVector offset = {
			velocity.x * static_cast<float>(kCanvasPanPredictionSeconds),
			velocity.y * static_cast<float>(kCanvasPanPredictionSeconds)
		};
		const float maximum = 1.5f * std::hypot(viewportWidth, viewportHeight);
		const float length = std::hypot(offset.x, offset.y);
		if (length > maximum && length > 0.0f)
		{
			const float scale = maximum / length;
			offset.x *= scale;
			offset.y *= scale;
		}
		return offset;
	}

	std::optional<CanvasRect> ComputeCanvasRenderCoverageBounds(
		CanvasViewportState viewport, float viewportWidth, float viewportHeight,
		CanvasVector contentVelocity, uint32_t tileSize) noexcept
	{
		if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
			!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) ||
			viewportWidth <= 0.0f || viewportHeight <= 0.0f || tileSize == 0)
			return std::nullopt;
		const CanvasVector contentPrediction = ComputeCanvasPredictionOffset(
			contentVelocity, viewportWidth, viewportHeight);
		const CanvasVector viewportPrediction{
			-contentPrediction.x, -contentPrediction.y };
		const CanvasRect current{ viewport.x, viewport.y,
			viewport.x + viewportWidth, viewport.y + viewportHeight };
		const CanvasRect future{ current.left + viewportPrediction.x,
			current.top + viewportPrediction.y, current.right + viewportPrediction.x,
			current.bottom + viewportPrediction.y };
		const CanvasRect trailing{ current.left + contentPrediction.x,
			current.top + contentPrediction.y, current.right + contentPrediction.x,
			current.bottom + contentPrediction.y };
		const float margin = static_cast<float>(tileSize);
		return CanvasRect{
			(std::min)({ current.left, future.left, trailing.left }) - margin,
			(std::min)({ current.top, future.top, trailing.top }) - margin,
			(std::max)({ current.right, future.right, trailing.right }) + margin,
			(std::max)({ current.bottom, future.bottom, trailing.bottom }) + margin };
	}

	CanvasRenderTilePlan PlanCanvasRenderTiles(
		std::span<const CanvasTileCoordinate> contentTiles,
		CanvasViewportState viewport, float viewportWidth, float viewportHeight,
		CanvasVector contentVelocity, uint32_t tileSize)
	{
		CanvasRenderTilePlan result;
		if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
			!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) ||
			viewportWidth <= 0.0f || viewportHeight <= 0.0f || tileSize == 0) return result;
		const CanvasVector contentPrediction = ComputeCanvasPredictionOffset(
			contentVelocity, viewportWidth, viewportHeight);
		const CanvasVector viewportPrediction{
			-contentPrediction.x, -contentPrediction.y };
		const CanvasRect current{ viewport.x, viewport.y,
			viewport.x + viewportWidth, viewport.y + viewportHeight };
		const CanvasRect future{ current.left + viewportPrediction.x,
			current.top + viewportPrediction.y, current.right + viewportPrediction.x,
			current.bottom + viewportPrediction.y };
		const float margin = static_cast<float>(tileSize);
		const CanvasRect swept{
			(std::min)(current.left, future.left) - margin,
			(std::min)(current.top, future.top) - margin,
			(std::max)(current.right, future.right) + margin,
			(std::max)(current.bottom, future.bottom) + margin };
		const CanvasRect trailing{
			current.left + contentPrediction.x - margin,
			current.top + contentPrediction.y - margin,
			current.right + contentPrediction.x + margin,
			current.bottom + contentPrediction.y + margin };
		const auto overlaps = [](CanvasRect first, CanvasRect second) noexcept
		{
			return first.left < second.right && first.right > second.left &&
				first.top < second.bottom && first.bottom > second.top;
		};
		for (CanvasTileCoordinate tile : contentTiles)
		{
			const double left = static_cast<double>(tile.x) * tileSize;
			const double top = static_cast<double>(tile.y) * tileSize;
			if (!std::isfinite(left) || !std::isfinite(top) ||
				left < -(std::numeric_limits<float>::max)() ||
				top < -(std::numeric_limits<float>::max)() ||
				left > (std::numeric_limits<float>::max)() - tileSize ||
				top > (std::numeric_limits<float>::max)() - tileSize) continue;
			const CanvasRect tileRect{ static_cast<float>(left), static_cast<float>(top),
				static_cast<float>(left + tileSize), static_cast<float>(top + tileSize) };
			CanvasTilePriority priority;
			if (overlaps(tileRect, current)) priority = CanvasTilePriority::Visible;
			else if (overlaps(tileRect, future)) priority = CanvasTilePriority::LeadingEdge;
			else if (overlaps(tileRect, swept)) priority = CanvasTilePriority::Predicted;
			else if (overlaps(tileRect, trailing)) priority = CanvasTilePriority::Trailing;
			else continue;
			result.tiles.push_back({ tile, priority });
		}
		std::sort(result.tiles.begin(), result.tiles.end(), [&](const CanvasPlannedTile& left,
			const CanvasPlannedTile& right)
		{
			if (left.priority != right.priority) return left.priority < right.priority;
			const float centerX = current.left + viewportWidth * 0.5f;
			const float centerY = current.top + viewportHeight * 0.5f;
			const auto distanceSquared = [&](CanvasTileCoordinate tile)
			{
				const float x = (static_cast<float>(tile.x) + 0.5f) * tileSize - centerX;
				const float y = (static_cast<float>(tile.y) + 0.5f) * tileSize - centerY;
				return x * x + y * y;
			};
			const float leftDistance = distanceSquared(left.tile);
			const float rightDistance = distanceSquared(right.tile);
			return leftDistance != rightDistance ? leftDistance < rightDistance :
				left.tile < right.tile;
		});
		result.tiles.erase(std::unique(result.tiles.begin(), result.tiles.end(),
			[](const CanvasPlannedTile& left, const CanvasPlannedTile& right)
				{ return left.tile == right.tile; }), result.tiles.end());
		result.visibleTileCount = static_cast<size_t>(std::count_if(
			result.tiles.begin(), result.tiles.end(), [](const CanvasPlannedTile& tile)
				{ return tile.priority == CanvasTilePriority::Visible; }));
		return result;
	}

	std::optional<CanvasRect> ComputeCanvasSnapshotScreenIntersection(
		CanvasViewportState snapshotViewport, CanvasViewportState currentViewport,
		float viewportWidth, float viewportHeight) noexcept
	{
		if (!std::isfinite(snapshotViewport.x) || !std::isfinite(snapshotViewport.y) ||
			!std::isfinite(currentViewport.x) || !std::isfinite(currentViewport.y) ||
			!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) ||
			viewportWidth <= 0.0f || viewportHeight <= 0.0f) return std::nullopt;
		const float canvasLeft = (std::max)(snapshotViewport.x, currentViewport.x);
		const float canvasTop = (std::max)(snapshotViewport.y, currentViewport.y);
		const float canvasRight = (std::min)(snapshotViewport.x + viewportWidth,
			currentViewport.x + viewportWidth);
		const float canvasBottom = (std::min)(snapshotViewport.y + viewportHeight,
			currentViewport.y + viewportHeight);
		if (!(canvasLeft < canvasRight && canvasTop < canvasBottom)) return std::nullopt;
		return CanvasRect{ canvasLeft - currentViewport.x,
			canvasTop - currentViewport.y, canvasRight - currentViewport.x,
			canvasBottom - currentViewport.y };
	}
}
