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
		case CanvasPanReleaseReason::TimingRejected: return "compressed-time";
		case CanvasPanReleaseReason::Stationary: return "slow-or-stationary";
		case CanvasPanReleaseReason::Stale: return "stale";
		case CanvasPanReleaseReason::Cancelled: return "cancelled";
		case CanvasPanReleaseReason::WithinSlop: return "within-slop";
		case CanvasPanReleaseReason::EmptyViewport: return "empty-viewport";
		case CanvasPanReleaseReason::DirectionConflict: return "direction-conflict";
		case CanvasPanReleaseReason::Disabled: return "disabled";
		default: return "none";
		}
	}

	const CanvasPanReleaseVelocityTracker::History* CanvasPanReleaseVelocityTracker::Find(uint64_t key) const noexcept
	{
		for (const auto& history : histories_) if (history.used && history.key == key) return &history;
		return nullptr;
	}

	void CanvasPanReleaseVelocityTracker::Configure(const CanvasPanMotionContext& context) noexcept
	{ reversalDistance_ = context.reversalDistance; velocityLimit_ = context.velocityLimit; }

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
		CanvasPanSampleDisposition result = CanvasPanSampleDisposition::Accepted;
		if (history.count)
		{
			const auto previous = history.samples[history.count - 1];
			if (sample.qpc <= previous.qpc) return CanvasPanSampleDisposition::Rejected;
			const double dt = PanSecondsBetween(sample.qpc, previous.qpc, frequency);
			const double dx = sample.x - previous.x, dy = sample.y - previous.y;
			const double distance = std::hypot(dx, dy);
			// QPC量化会使960Hz的合法12000速度略超上限；两端float像素还会带来坐标量化。
			// 在位移域扣除可计算的误差界，不能把这些舍入误判为时间压缩而反复清窗。
			const double coordinateError = std::numeric_limits<float>::epsilon() *
				(std::hypot(sample.x, sample.y) + std::hypot(previous.x, previous.y));
			const double maximumDistance = velocityLimit_ *
				(dt + 1.0 / static_cast<double>(frequency)) + coordinateError;
			if (dt < kCanvasPanReleaseMinimumIntervalSeconds && distance > maximumDistance)
			{
				history.count = 0; history.direction = {}; history.reversalAnchor.reset();
				history.timingRejected = true; result = CanvasPanSampleDisposition::TimingRejected;
			}
			else if (dt >= kCanvasPanTrackerGapSeconds)
			{
				history.count = 0; history.direction = {}; history.reversalAnchor.reset();
				history.timingRejected = false;
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
						const double length = std::hypot(reverseX, reverseY);
						history.direction = ToCanvasVector(reverseX / length, reverseY / length);
						history.reversalAnchor.reset(); history.timingRejected = false;
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
		if (history.count >= kCanvasPanReleaseMinimumSamples &&
			SampleSpanSeconds(key, frequency) + 1e-9 >= kCanvasPanReleaseMinimumSpanSeconds) history.timingRejected = false;
		return result;
	}

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
		if (frequency <= 0 || !history || history->count < 2 || PanZero(estimatedVelocity)) return {};
		const auto newest = history->samples[history->count - 1];
		for (size_t back = 1; back < history->count; ++back)
		{
			const auto previous = history->samples[history->count - back - 1];
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

	CanvasPanReleaseReason CanvasPanReleaseVelocityTracker::Reason(uint64_t key, int64_t frequency) const noexcept
	{
		const auto* history = Find(key);
		if (history && history->timingRejected) return CanvasPanReleaseReason::TimingRejected;
		if (!history || history->count < kCanvasPanReleaseMinimumSamples) return CanvasPanReleaseReason::InsufficientSamples;
		if (SampleSpanSeconds(key, frequency) + 1e-9 < kCanvasPanReleaseMinimumSpanSeconds) return CanvasPanReleaseReason::InsufficientSpan;
		return CanvasPanReleaseReason::Ready;
	}
	CanvasPanVelocityEstimate CanvasPanReleaseVelocityTracker::Estimate(uint64_t key, int64_t frequency) const noexcept
	{
		if (frequency <= 0 || Reason(key, frequency) != CanvasPanReleaseReason::Ready) return {};
		const auto& history = *Find(key);
		const auto newest = history.samples[history.count - 1];
		double sumT = 0.0, sumX = 0.0, sumY = 0.0;
		for (size_t i = 0; i < history.count; ++i)
		{
			sumT += PanSecondsBetween(history.samples[i].qpc, newest.qpc, frequency);
			sumX += history.samples[i].x - newest.x; sumY += history.samples[i].y - newest.y;
		}
		const double meanT = sumT / history.count, meanX = sumX / history.count, meanY = sumY / history.count;
		double denominator = 0.0, numeratorX = 0.0, numeratorY = 0.0;
		for (size_t i = 0; i < history.count; ++i)
		{
			const double dt = PanSecondsBetween(history.samples[i].qpc, newest.qpc, frequency) - meanT;
			denominator += dt * dt;
			numeratorX += dt * (history.samples[i].x - newest.x - meanX);
			numeratorY += dt * (history.samples[i].y - newest.y - meanY);
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
			const double limit = (std::min)(motionContext_.velocityLimit, tail.value_or(motionContext_.velocityLimit));
			const double scale = speed > limit ? limit / speed : 1;
			if (estimate.valid) { vx += estimate.x * gate * scale; vy += estimate.y * gate * scale; }
			out.handVelocityKnown |= estimate.valid; ++count;
		}
		const auto motionVelocity = count ? ToCanvasVector(vx / count, vy / count) : CanvasVector{};
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
				// 一帧可包含毫米触摸和 DIP 鼠标，必须在事件边界恢复后再累加。
				const auto delta = PanToDip(motionContext_, event.delta);
				out.contentDelta.x += delta.x; out.contentDelta.y += delta.y; break;
			}
			case CanvasPanGestureType::FlingStart:
				out.released = true;
				{ const auto v = PanToDip(motionContext_, event.velocity); out.releaseSpeed = std::hypot(v.x, v.y); }
				out.frictionParameters = ResolveCanvasPanFriction(motionContext_, std::hypot(event.velocity.x, event.velocity.y));
				fling_.Start(event, static_cast<double>(input.frameQpc) / input.qpcFrequency,
					PanToMotion(motionContext_, input.viewportSizeDip), booster_, out.frictionParameters);
				out.boostedReleaseVelocity = PanToDip(motionContext_, fling_.Velocity()); break;
			default: break;
			}
		}
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
				decide(sourceSample, CanvasPanAdmissionReason::Accepted); ++events.sampleCount;
				if (terminalBeforeDown(sample.key))
				{
					const bool last = HeldContactCount() == 1;
					Dispatch(detector_.Up(sample.qpc, currentPointers(), sample.key, true, input.qpcFrequency, {}), input, events);
					releaseTracker_.ClearPointer(sample.key); found->held = false; booster_.Reset();
					if (last)
					{
						events.released = true; events.releaseSourceKey = sample.sourceKey;
						events.releaseMotionContext = motionContext_; events.releaseReason = CanvasPanReleaseReason::TimingRejected;
						events.frictionParameters = ResolveCanvasPanFriction(motionContext_, 0);
						events.rawReleaseVelocity = events.selectedReleaseVelocity = events.boostedReleaseVelocity = events.releaseMotionVelocity = {};
						events.releaseSpeed = events.releaseAgeSeconds = events.releaseTailSpeedLimit = events.releaseSpanSeconds = 0;
						events.releaseSampleCount = 0; events.releaseTailSpeedLimited = false;
					}
				}
				continue;
			}
			sample.x *= motionContext_.motionPerDipX; sample.y *= motionContext_.motionPerDipY;
			if (sample.phase == CanvasPanContactPhase::Move)
			{
				if (found == contacts_.end() || sample.qpc <= found->lastQpc) { ++events.rejectedSamples; continue; }
				if (moveQpc && moveQpc != sample.qpc) flushMove();
				const auto result = releaseTracker_.AddSample(sample.key, { sample.qpc, sample.x, sample.y }, input.qpcFrequency);
				if (result == CanvasPanSampleDisposition::Reversed || result == CanvasPanSampleDisposition::TimingRejected)
				{
					booster_.Reset();
					if (result == CanvasPanSampleDisposition::Reversed) ++events.reversalCount; else ++events.timingResetCount;
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
			const int64_t latest = releaseTracker_.LastSampleQpc(sample.key);
			const double age = latest ? PanSecondsBetween(sample.qpc, latest, input.qpcFrequency) : 0;
			const auto raw = releaseTracker_.Estimate(sample.key, input.qpcFrequency);
			auto selected = raw.valid ? raw : CanvasPanVelocityEstimate{};
			const double gate = std::clamp((kCanvasPanReleaseZeroAgeSeconds - age) /
				(kCanvasPanReleaseZeroAgeSeconds - kCanvasPanReleaseFullAgeSeconds), 0.0, 1.0);
			const double speed = std::hypot(raw.x, raw.y);
			const auto tailLimit = raw.valid ? releaseTracker_.RecentSpeedLimit(sample.key, input.qpcFrequency, ToCanvasVector(raw.x, raw.y)) : std::optional<double>{};
			const double limit = (std::min)(motionContext_.velocityLimit, tailLimit.value_or(motionContext_.velocityLimit));
			const double scale = speed > limit ? limit / speed : 1;
			selected.x *= gate * scale; selected.y *= gate * scale;
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
				events.rawReleaseVelocity = PanToDip(motionContext_, ToCanvasVector(raw.x, raw.y));
				events.selectedReleaseVelocity = PanToDip(motionContext_, events.releaseMotionVelocity);
				events.releaseSpeed = std::hypot(selected.x / motionContext_.motionPerDipX, selected.y / motionContext_.motionPerDipY);
				events.releaseSampleCount = releaseTracker_.SampleCount(sample.key);
				events.releaseSpanSeconds = releaseTracker_.SampleSpanSeconds(sample.key, input.qpcFrequency);
				events.releaseReason = invalidTerminalTime ? CanvasPanReleaseReason::TimingRejected :
					cancelled ? CanvasPanReleaseReason::Cancelled : !sample.allowInertia ? CanvasPanReleaseReason::Disabled :
					age >= kCanvasPanReleaseZeroAgeSeconds ? CanvasPanReleaseReason::Stale : releaseTracker_.Reason(sample.key, input.qpcFrequency);
				if (events.releaseReason == CanvasPanReleaseReason::Ready && !raw.valid) events.releaseReason = CanvasPanReleaseReason::DirectionConflict;
				else if (events.releaseReason == CanvasPanReleaseReason::Ready && selectedSpeed <= motionContext_.minimumFlingVelocity)
					events.releaseReason = CanvasPanReleaseReason::Stationary;
			}
			else if (!cancelled && selected.valid)
			{
				for (const auto& other : contacts_)
				{
					if (!other.held || other.pointer.key == sample.key) continue;
					const auto velocity = releaseTracker_.Estimate(other.pointer.key, input.qpcFrequency);
					const double otherAge = PanSecondsBetween(sample.qpc, releaseTracker_.LastSampleQpc(other.pointer.key), input.qpcFrequency);
					if (velocity.valid && otherAge < kCanvasPanReleaseZeroAgeSeconds && selected.x * velocity.x + selected.y * velocity.y < 0)
					{ releaseTracker_.Clear(); booster_.Reset(); break; }
				}
			}
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
		out.rejectedSamples = events.rejectedSamples; out.reversalCount = events.reversalCount; out.timingResetCount = events.timingResetCount;
		out.admissionDecisionCount = events.admissionDecisionCount; out.releaseSourceKey = events.releaseSourceKey;
		out.releaseMotionContext = events.releaseMotionContext; out.releaseMotionVelocity = events.releaseMotionVelocity;
		out.frictionParameters = events.frictionParameters;
		return out;
	}

	void CanvasPanDynamics::Stop() noexcept
	{
		contacts_ = {}; rejectedContacts_.clear(); earlyTerminalKeys_.clear(); detector_.Reset(); releaseTracker_.Clear(); fling_.StopFling(booster_); velocity_ = {};
		lastFrameQpc_ = 0; lastSourceKey_ = 0; motionContext_ = {};
	}
	void CanvasPanDynamics::StopFling() noexcept { fling_.StopFling(booster_); velocity_ = {}; }
	void CanvasPanDynamics::ClearAxis(bool x, bool y) noexcept
	{
		releaseTracker_.ClearAxis(x, y); fling_.ClearAxis(x, y, booster_);
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
