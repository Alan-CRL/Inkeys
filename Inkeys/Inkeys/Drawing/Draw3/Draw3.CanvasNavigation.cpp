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

		// 对应 SolveLeastSquares 的无权 LSQ2；float 运算和 QR 退化阈值沿用 Chromium。
		bool PanLeastSquares(const std::array<float, kCanvasPanTrackerCapacity>& time,
			const std::array<float, kCanvasPanTrackerCapacity>& value, size_t m, size_t n,
			std::array<float, 3>& coefficients) noexcept
		{
			std::array<std::array<float, kCanvasPanTrackerCapacity>, 3> a = {}, q = {};
			std::array<std::array<float, 3>, 3> r = {};
			for (size_t h = 0; h < m; ++h)
			{
				a[0][h] = 1.0f;
				for (size_t i = 1; i < n; ++i) a[i][h] = a[i - 1][h] * time[h];
			}
			for (size_t j = 0; j < n; ++j)
			{
				q[j] = a[j];
				for (size_t i = 0; i < j; ++i)
				{
					float dot = 0.0f;
					for (size_t h = 0; h < m; ++h) dot += q[i][h] * q[j][h];
					for (size_t h = 0; h < m; ++h) q[j][h] -= dot * q[i][h];
				}
				float norm = 0.0f;
				for (size_t h = 0; h < m; ++h) norm += q[j][h] * q[j][h];
				norm = std::sqrt(norm);
				if (norm < 0.000001f) return false;
				const float invNorm = 1.0f / norm;
				for (size_t h = 0; h < m; ++h) q[j][h] *= invNorm;
				for (size_t i = j; i < n; ++i)
					for (size_t h = 0; h < m; ++h) r[j][i] += q[j][h] * a[i][h];
			}
			for (size_t i = n; i-- != 0;)
			{
				for (size_t h = 0; h < m; ++h) coefficients[i] += q[i][h] * value[h];
				for (size_t j = n - 1; j > i; --j) coefficients[i] -= r[i][j] * coefficients[j];
				coefficients[i] /= r[i][i];
			}
			return true;
		}

		double PanLegacyPosition(double t) noexcept
		{
			return kCanvasPanLegacyAlpha * std::exp(-kCanvasPanLegacyGamma * t) -
				kCanvasPanLegacyBeta * t - kCanvasPanLegacyAlpha;
		}
		double PanLegacyVelocity(double t) noexcept
		{
			return -kCanvasPanLegacyAlpha * kCanvasPanLegacyGamma *
				std::exp(-kCanvasPanLegacyGamma * t) - kCanvasPanLegacyBeta;
		}
		double PanLegacyTime(double v) noexcept
		{
			return -std::log((v + kCanvasPanLegacyBeta) /
				(-kCanvasPanLegacyAlpha * kCanvasPanLegacyGamma)) / kCanvasPanLegacyGamma;
		}
	}

	bool CanvasPanVelocityTracker::AddMovement(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, int64_t frequency) noexcept
	{
		if (frequency <= 0 || qpc <= 0 || pointers.empty() ||
			pointers.size() > kCanvasPanMaximumContacts || qpc < lastEventQpc_) return false;
		for (const auto& pointer : pointers)
			if (!std::isfinite(pointer.x) || !std::isfinite(pointer.y)) return false;
		bool overlap = false;
		if (count_)
			for (const auto& pointer : pointers)
				overlap |= PanPointerAt(std::span(movements_[index_].pointers).first(movements_[index_].count), pointer.key) != nullptr;
		if (overlap && PanSecondsBetween(qpc, lastEventQpc_, frequency) >= kCanvasPanTrackerGapSeconds)
			Clear();
		lastEventQpc_ = qpc;
		index_ = (index_ + 1) % kCanvasPanTrackerCapacity;
		auto& movement = movements_[index_];
		movement.qpc = qpc;
		movement.count = pointers.size();
		std::copy(pointers.begin(), pointers.end(), movement.pointers.begin());
		count_ = (std::min)(count_ + 1, kCanvasPanTrackerCapacity);
		return true;
	}

	void CanvasPanVelocityTracker::Clear() noexcept
	{
		index_ = 0; count_ = 0; lastEventQpc_ = 0;
		movements_[0].count = 0;
	}
	void CanvasPanVelocityTracker::Reset() noexcept { Clear(); }

	void CanvasPanVelocityTracker::ClearPointer(uint64_t key) noexcept
	{
		if (!count_) return;
		auto& newest = movements_[index_];
		for (size_t i = 0; i < newest.count; ++i)
			if (newest.pointers[i].key == key)
			{
				std::move(newest.pointers.begin() + i + 1, newest.pointers.begin() + newest.count,
					newest.pointers.begin() + i);
				--newest.count;
				return;
			}
	}

	void CanvasPanVelocityTracker::PointerUp(int64_t qpc, int64_t frequency) noexcept
	{
		if (frequency > 0 && count_ &&
			PanSecondsBetween(qpc, lastEventQpc_, frequency) >= kCanvasPanTrackerUpGapSeconds) Clear();
	}

	CanvasPanVelocityEstimate CanvasPanVelocityTracker::Estimate(uint64_t key, int64_t frequency) const noexcept
	{
		if (frequency <= 0 || !count_) return {};
		std::array<float, kCanvasPanTrackerCapacity> time = {}, x = {}, y = {};
		size_t m = 0;
		for (size_t back = 0; back < count_; ++back)
		{
			const auto& movement = movements_[(index_ + kCanvasPanTrackerCapacity - back) % kCanvasPanTrackerCapacity];
			const auto* pointer = PanPointerAt(std::span(movement.pointers).first(movement.count), key);
			if (!pointer) break;
			const double age = PanSecondsBetween(movements_[index_].qpc, movement.qpc, frequency);
			if (age > kCanvasPanTrackerWindowSeconds) break;
			time[m] = -static_cast<float>(age); x[m] = static_cast<float>(pointer->x);
			y[m++] = static_cast<float>(pointer->y);
		}
		if (m < 2) return {};
		std::array<float, 3> bx = {}, by = {};
		const size_t n = (std::min)(size_t{3}, m);
		if (!PanLeastSquares(time, x, m, n, bx) || !PanLeastSquares(time, y, m, n, by)) return {};
		// GestureDetector::ComputeCurrentVelocity 按轴封顶，不按向量模长缩放。
		return { true, std::clamp(static_cast<double>(bx[1]), -kCanvasPanVelocityLimitDipPerSecond,
			kCanvasPanVelocityLimitDipPerSecond), std::clamp(static_cast<double>(by[1]),
			-kCanvasPanVelocityLimitDipPerSecond, kCanvasPanVelocityLimitDipPerSecond) };
	}

	size_t CanvasPanVelocityTracker::SampleCount(uint64_t key) const noexcept
	{
		size_t count = 0;
		for (size_t back = 0; back < count_; ++back)
		{
			const auto& movement = movements_[(index_ + kCanvasPanTrackerCapacity - back) % kCanvasPanTrackerCapacity];
			if (!PanPointerAt(std::span(movement.pointers).first(movement.count), key)) break;
			++count;
		}
		return count;
	}
	int64_t CanvasPanVelocityTracker::LastSampleQpc() const noexcept { return count_ ? lastEventQpc_ : 0; }

	void CanvasPanVelocityTracker::ClearAxis(bool x, bool y) noexcept
	{
		if (!count_ || (!x && !y)) return;
		const auto& newest = movements_[index_];
		for (size_t back = 0; back < count_; ++back)
		{
			auto& movement = movements_[(index_ + kCanvasPanTrackerCapacity - back) % kCanvasPanTrackerCapacity];
			for (size_t i = 0; i < movement.count; ++i)
				if (const auto* current = PanPointerAt(std::span(newest.pointers).first(newest.count), movement.pointers[i].key))
				{
					if (x) movement.pointers[i].x = current->x;
					if (y) movement.pointers[i].y = current->y;
				}
		}
	}

	CanvasPanGestureBatch CanvasPanGestureDetector::Down(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, uint64_t key, int64_t frequency) noexcept
	{
		CanvasPanGestureBatch batch;
		if (pointers.size() == 1)
		{
			Reset(); primaryDown_ = pointers.front(); maximumPointerCount_ = 1;
			batch.events[batch.count++] = { CanvasPanGestureType::FlingCancel, static_cast<double>(qpc) / frequency };
		}
		else
		{
			tracker_.ClearPointer(key);
			maximumPointerCount_ = (std::max)(maximumPointerCount_, pointers.size());
			if (pointers.size() == 2)
			{
				std::copy(pointers.begin(), pointers.end(), secondaryDown_.begin()); secondaryCount_ = 2;
			}
		}
		tracker_.AddMovement(qpc, pointers, frequency);
		lastFocus_ = PanFocus(pointers);
		return batch;
	}

	CanvasPanGestureBatch CanvasPanGestureDetector::Move(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, int64_t frequency) noexcept
	{
		CanvasPanGestureBatch batch;
		tracker_.AddMovement(qpc, pointers, frequency);
		const CanvasVector focus = PanFocus(pointers);
		bool within = maximumPointerCount_ <= 2;
		for (const auto& pointer : pointers)
		{
			const auto* down = pointer.key == primaryDown_.key ? &primaryDown_ :
				PanPointerAt(std::span(secondaryDown_).first(secondaryCount_), pointer.key);
			if (!down) { within = false; break; }
			const float dx = static_cast<float>(pointer.x) - static_cast<float>(down->x);
			const float dy = static_cast<float>(pointer.y) - static_cast<float>(down->y);
			const float slop = kCanvasPanTouchSlopDip + kCanvasPanSlopEpsilonDip;
			if (dx * dx + dy * dy > slop * slop * kCanvasPanExpandSlopFactor) within = false;
		}
		CanvasVector delta{ focus.x - lastFocus_.x, focus.y - lastFocus_.y };
		if ((withinSlop_ && !within) || (!withinSlop_ &&
			(std::abs(delta.x) > kCanvasPanScrollEpsilonDip || std::abs(delta.y) > kCanvasPanScrollEpsilonDip)))
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
						const float ratio = (std::max)(0.0f, distance - kCanvasPanTouchSlopDip) / distance;
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
		std::span<const CanvasPanPointer> pointers, uint64_t key, bool cancelled, int64_t frequency) noexcept
	{
		CanvasPanGestureBatch batch;
		tracker_.PointerUp(qpc, frequency);
		if (pointers.size() > 1)
		{
			lastFocus_ = PanFocus(pointers, key);
			if (cancelled) tracker_.ClearPointer(key);
			else
			{
				const auto lifted = tracker_.Estimate(key, frequency);
				for (const auto& pointer : pointers)
				{
					if (pointer.key == key) continue;
					const auto remaining = tracker_.Estimate(pointer.key, frequency);
					if (lifted.x * remaining.x + lifted.y * remaining.y < 0.0) { tracker_.Clear(); break; }
				}
			}
			return batch;
		}
		const auto velocity = tracker_.Estimate(key, frequency);
		if (!cancelled && !withinSlop_ &&
			(std::abs(velocity.x) > kCanvasPanMinimumFlingVelocity || std::abs(velocity.y) > kCanvasPanMinimumFlingVelocity))
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
		tracker_.Clear(); maximumPointerCount_ = 0; scrollSent_ = false;
		return batch;
	}
	void CanvasPanGestureDetector::Reset() noexcept
	{
		tracker_.Clear(); lastFocus_ = {}; primaryDown_ = {}; secondaryDown_ = {};
		secondaryCount_ = maximumPointerCount_ = 0; withinSlop_ = true; scrollSent_ = false;
	}
	bool CanvasPanGestureDetector::Scrolling() const noexcept { return scrollSent_; }
	CanvasPanVelocityTracker& CanvasPanGestureDetector::Tracker() noexcept { return tracker_; }
	const CanvasPanVelocityTracker& CanvasPanGestureDetector::Tracker() const noexcept { return tracker_; }

	bool CanvasPanFlingBooster::ShouldBoostFling(const CanvasPanGestureEvent& event) const noexcept
	{
		return !PanZero(startingVelocity_) && event.sourceDevice == sourceDevice_ &&
			event.modifiers == modifiers_ && cutoff_ && PanMicroseconds(event.timeSeconds) <= PanMicroseconds(*cutoff_) &&
			PanDot(startingVelocity_, event.velocity) > 0.0 &&
			PanDot(currentVelocity_, currentVelocity_) >= kCanvasPanBoostMinimumFlingSpeedSquared &&
			PanDot(event.velocity, event.velocity) >= kCanvasPanBoostMinimumFlingSpeedSquared;
	}
	CanvasVector CanvasPanFlingBooster::GetVelocityForFlingStart(const CanvasPanGestureEvent& event) noexcept
	{
		CanvasVector velocity = event.velocity;
		if (ShouldBoostFling(event)) { velocity.x += startingVelocity_.x; velocity.y += startingVelocity_.y; }
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
					if (PanDot(scrollVelocity, scrollVelocity) < kCanvasPanBoostMinimumScrollSpeedSquared)
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

	double SolveCanvasPanCubicBezier(double x, double p1x, double p1y, double p2x, double p2y) noexcept
	{
		const double cx = 3.0 * p1x, bx = 3.0 * (p2x - p1x) - cx, ax = 1.0 - cx - bx;
		const double cy = 3.0 * p1y, by = 3.0 * (p2y - p1y) - cy, ay = 1.0 - cy - by;
		const auto sx = [&](double t) { return ((ax * t + bx) * t + cx) * t; };
		const auto sy = [&](double t) { return ((ay * t + by) * t + cy) * t; };
		const auto finite = [](double value) { return std::isinf(value) ?
			std::copysign((std::numeric_limits<double>::max)(), value) : value; };
		if (x < 0.0)
		{
			const double gradient = p1x > 0 ? p1y / p1x : (!p1y && p2x > 0 ? p2y / p2x : (!p1y && !p2y ? 1 : 0));
			return finite(gradient * x);
		}
		if (x > 1.0)
		{
			const double gradient = p2x < 1 ? (p2y - 1) / (p2x - 1) :
				(p2y == 1 && p1x < 1 ? (p1y - 1) / (p1x - 1) : (p2y == 1 && p1y == 1 ? 1 : 0));
			return finite(1.0 + gradient * (x - 1.0));
		}
		// Chromium CubicBezier::InitSpline/SolveCurveX：11点初猜、4轮Newton，最后二分。
		std::array<double, 11> spline = {};
		for (size_t i = 0; i < spline.size(); ++i) spline[i] = sx(i * 0.1);
		double t0 = 0.0, t1 = 1.0, t2 = x, error = 0.0;
		for (size_t i = 1; i < spline.size(); ++i)
			if (x <= spline[i])
			{
				t1 = 0.1 * i; t0 = t1 - 0.1;
				t2 = t0 + (t1 - t0) * (x - spline[i - 1]) / (spline[i] - spline[i - 1]); break;
			}
		for (int i = 0; i < 4; ++i)
		{
			error = sx(t2) - x;
			if (std::abs(error) < kCanvasPanBezierEpsilon) return finite(sy(t2));
			const double derivative = (3.0 * ax * t2 + 2.0 * bx) * t2 + cx;
			if (std::abs(derivative) < kCanvasPanBezierEpsilon) break;
			t2 -= error / derivative;
		}
		if (std::abs(error) < kCanvasPanBezierEpsilon) return finite(sy(t2));
		while (t0 < t1)
		{
			const double value = sx(t2);
			if (std::abs(value - x) < kCanvasPanBezierEpsilon) return finite(sy(t2));
			if (x > value) t0 = t2; else t1 = t2;
			t2 = (t1 + t0) * 0.5;
		}
		return finite(sy(t2));
	}

	void CanvasPanFlingCurve::Start(CanvasVector velocity, CanvasVector viewport,
		float boost, CanvasPanFlingCurveKind kind) noexcept
	{
		*this = CanvasPanFlingCurve{}; kind_ = kind;
		const float vmax = (std::max)(std::abs(velocity.x), std::abs(velocity.y));
		if (!(vmax > 0.0f) || !std::isfinite(vmax) || viewport.x <= 0.0f || viewport.y <= 0.0f) return;
		if (kind == CanvasPanFlingCurveKind::Legacy)
		{
			const float start = (std::min)(vmax, static_cast<float>(PanLegacyVelocity(0.0)));
			legacyRatio_ = { velocity.x / start, velocity.y / start };
			legacyTimeOffset_ = PanLegacyTime(start);
			legacyPositionOffset_ = PanLegacyPosition(legacyTimeOffset_);
			duration_ = PanLegacyTime(0.0) - legacyTimeOffset_;
			const double end = PanLegacyPosition(PanLegacyTime(0.0)) - legacyPositionOffset_;
			distance_ = ToCanvasVector(legacyRatio_.x * end, legacyRatio_.y * end);
			return;
		}
		const float a = kCanvasPanPixelsPerInch * kCanvasPanPhysicalDeceleration;
		const auto endpoint = [&](float v, float size)
		{
			const float millisecondsVelocity = v * (1.0f / 1000.0f);
			const float t = std::abs(millisecondsVelocity / a);
			const float d = static_cast<float>(std::copysign((std::abs(millisecondsVelocity) - a * t * 0.5) * t, static_cast<double>(v)));
			const float bound = std::floor(size * boost * kCanvasPanFlingBoundsMultiplier);
			return std::clamp(d, -bound, bound);
		};
		distance_ = { endpoint(velocity.x, viewport.x), endpoint(velocity.y, viewport.y) };
		const float duration = (std::min)(kCanvasPanMaximumFlingSeconds, vmax / kCanvasPanPixelDeceleration);
		duration_ = duration; p1_ = { kCanvasPanBezierP1X, kCanvasPanBezierP1Y };
		const float dominantDistance = std::abs(velocity.y) == vmax ? distance_.y : distance_.x;
		const float dominantVelocity = std::abs(velocity.y) == vmax ? velocity.y : velocity.x;
		// 空/不足1 DIP视口经取整可能产生零距离；保护斜率，不制造 NaN 控制点。
		const float slope = dominantDistance != 0.0f ? std::abs(dominantVelocity * duration / dominantDistance) :
			(std::numeric_limits<float>::infinity)();
		if (slope * p1_.x < 1.0f) p1_.y = p1_.x * slope; else p1_.x = p1_.y / slope;
	}

	bool CanvasPanFlingCurve::Advance(double elapsed, CanvasVector& offset, CanvasVector& velocity) noexcept
	{
		offset = {}; velocity = {};
		if (elapsed < 0.0) return true;
		if (!(duration_ > 0.0) || elapsed >= duration_) { offset = distance_; return false; }
		if (kind_ == CanvasPanFlingCurveKind::Legacy)
		{
			const double t = elapsed + legacyTimeOffset_;
			const float position = static_cast<float>(PanLegacyPosition(t) - legacyPositionOffset_);
			const float speed = static_cast<float>(PanLegacyVelocity(t));
			offset = { legacyRatio_.x * position, legacyRatio_.y * position };
			velocity = { legacyRatio_.x * speed, legacyRatio_.y * speed };
		}
		else
		{
			const double progress = SolveCanvasPanCubicBezier(elapsed / duration_, p1_.x, p1_.y);
			offset = ToCanvasVector(distance_.x * progress, distance_.y * progress);
			const double dt = elapsed - previousSeconds_;
			if (dt != 0.0) velocity = ToCanvasVector((offset.x - previousOffset_.x) / dt, (offset.y - previousOffset_.y) / dt);
			previousOffset_ = offset; previousSeconds_ = elapsed;
		}
		return true;
	}
	void CanvasPanFlingCurve::ClearAxis(bool x, bool y) noexcept
	{
		if (x) { distance_.x = previousOffset_.x = legacyRatio_.x = 0.0f; }
		if (y) { distance_.y = previousOffset_.y = legacyRatio_.y = 0.0f; }
	}
	double CanvasPanFlingCurve::Duration() const noexcept { return duration_; }
	CanvasVector CanvasPanFlingCurve::Distance() const noexcept { return distance_; }
	CanvasVector CanvasPanFlingCurve::P1() const noexcept { return p1_; }

	void CanvasPanFlingController::Start(const CanvasPanGestureEvent& event, double lastScroll,
		CanvasVector viewport, CanvasPanFlingBooster& booster) noexcept
	{
		Cancel();
		velocity_ = booster.GetVelocityForFlingStart(event);
		if (PanZero(velocity_) || viewport.x <= 0.0f || viewport.y <= 0.0f)
		{ StopFling(booster); return; }
		const float raw = (std::max)(std::abs(event.velocity.x), std::abs(event.velocity.y));
		const float boosted = (std::max)(std::abs(velocity_.x), std::abs(velocity_.y));
		curve_.Start(velocity_, viewport, raw > 0.0f ? boosted / raw : 1.0f);
		startSeconds_ = lastScroll;
		active_ = true;
	}
	CanvasVector CanvasPanFlingController::Progress(double now, CanvasPanFlingBooster& booster) noexcept
	{
		if (!active_) return {};
		if (!firstUpdateSent_ && now >= startSeconds_ + 2.0 * kCanvasPanFlingFrameSeconds)
			startSeconds_ = now - kCanvasPanFlingFrameSeconds;
		if (now < lastProgressSeconds_ || now <= startSeconds_) return {};
		CanvasVector offset, velocity;
		const bool active = curve_.Advance(now - startSeconds_, offset, velocity);
		CanvasVector delta{ offset.x - cumulative_.x, offset.y - cumulative_.y };
		cumulative_ = offset; velocity_ = velocity;
		if (clearX_) delta.x = velocity_.x = 0.0f;
		if (clearY_) delta.y = velocity_.y = 0.0f;
		if (!active)
		{
			// Chromium 在曲线 inactive 时立即结束，终点剩余 delta 不派发。
			StopFling(booster); return {};
		}
		if (std::abs(delta.x) > kCanvasPanScrollEpsilonDip || std::abs(delta.y) > kCanvasPanScrollEpsilonDip)
		{
			firstUpdateSent_ = true; lastProgressSeconds_ = now;
			booster.ObserveProgressFling(velocity_); return delta;
		}
		// 与 WebGestureCurve::Advance 一致：小增量虽不派发，cumulative 已前进，不累计补发。
		return {};
	}
	void CanvasPanFlingController::Cancel() noexcept
	{
		active_ = firstUpdateSent_ = clearX_ = clearY_ = false;
		velocity_ = cumulative_ = {}; startSeconds_ = lastProgressSeconds_ = 0.0;
	}
	void CanvasPanFlingController::StopFling(CanvasPanFlingBooster& booster) noexcept { Cancel(); booster.Reset(); }
	void CanvasPanFlingController::ClearAxis(bool x, bool y, CanvasPanFlingBooster& booster) noexcept
	{
		clearX_ |= x; clearY_ |= y; curve_.ClearAxis(x, y);
		if (x) velocity_.x = cumulative_.x = 0.0f;
		if (y) velocity_.y = cumulative_.y = 0.0f;
		if (PanZero(velocity_)) StopFling(booster);
	}
	bool CanvasPanFlingController::Active() const noexcept { return active_; }
	CanvasVector CanvasPanFlingController::Velocity() const noexcept { return velocity_; }
	CanvasVector CanvasPanFlingController::Offset() const noexcept { return cumulative_; }
	double CanvasPanFlingController::StartSeconds() const noexcept { return startSeconds_; }

	CanvasPanFrameOutput CanvasPanDynamics::Snapshot(int64_t frequency) const noexcept
	{
		CanvasPanFrameOutput out;
		out.heldContacts = out.pointerCount = HeldContactCount(); out.phase = Phase();
		out.scrolling = detector_.Scrolling(); out.contentVelocity = velocity_;
		std::array<CanvasPanPointer, kCanvasPanMaximumContacts> pointers = {};
		size_t count = 0; double vx = 0.0, vy = 0.0;
		for (const auto& contact : contacts_)
		{
			if (!contact.held) continue;
			pointers[count] = contact.pointer;
			const auto estimate = detector_.Tracker().Estimate(contact.pointer.key, frequency);
			out.pointerDiagnostics[count] = { contact.pointer.key,
				detector_.Tracker().SampleCount(contact.pointer.key), ToCanvasVector(estimate.x, estimate.y) };
			vx += estimate.x; vy += estimate.y; out.handVelocityKnown |= estimate.valid; ++count;
		}
		if (count)
		{
			out.focus = PanFocus(std::span(pointers).first(count));
			out.handVelocity = ToCanvasVector(vx / count, vy / count);
			for (size_t i = 0; i < count; ++i)
				out.span += std::hypot(pointers[i].x - out.focus.x, pointers[i].y - out.focus.y) / count;
		}
		out.boosterStartingVelocity = booster_.StartingVelocity();
		out.boosterCurrentVelocity = booster_.CurrentVelocity(); out.boosterCutoffSeconds = booster_.CutoffSeconds();
		out.flingOffset = fling_.Offset();
		return out;
	}

	void CanvasPanDynamics::Dispatch(const CanvasPanGestureBatch& batch,
		const CanvasPanFrameInput& input, CanvasPanFrameOutput& out) noexcept
	{
		for (size_t i = 0; i < batch.count; ++i)
		{
			const auto& event = batch.events[i];
			// Chromium 丢弃无活动fling的FlingCancel，不让轻点反复延长旧boost窗口。
			if (event.type != CanvasPanGestureType::FlingCancel || fling_.Active())
				booster_.ObserveGestureEvent(event);
			switch (event.type)
			{
			case CanvasPanGestureType::FlingCancel:
				// 落指只取消当前动画，保留上次起滑速度供50ms窗口内的连甩。
				fling_.Cancel(); lastScrollSeconds_.reset(); velocity_ = {};
				break;
			case CanvasPanGestureType::ScrollBegin:
			case CanvasPanGestureType::ScrollEnd:
				lastScrollSeconds_.reset();
				break;
			case CanvasPanGestureType::ScrollUpdate:
				out.contentDelta.x += event.delta.x; out.contentDelta.y += event.delta.y;
				lastScrollSeconds_ = event.timeSeconds;
				break;
			case CanvasPanGestureType::FlingStart:
				out.released = true; out.releaseSpeed = std::hypot(event.velocity.x, event.velocity.y);
				fling_.Start(event, lastScrollSeconds_.value_or(event.timeSeconds), input.viewportSizeDip, booster_);
				break;
			default: break;
			}
		}
	}

	CanvasPanFrameOutput CanvasPanDynamics::Step(const CanvasPanFrameInput& input) noexcept
	{
		if (input.penContact) { Stop(); return Snapshot(input.qpcFrequency); }
		if (input.qpcFrequency <= 0 || input.frameQpc <= 0 ||
			(lastFrameQpc_ && input.frameQpc <= lastFrameQpc_)) return Snapshot(input.qpcFrequency);
		lastFrameQpc_ = input.frameQpc;
		CanvasPanFrameOutput events;
		std::vector<CanvasPanContactSample> samples(input.contacts.begin(), input.contacts.end());
		std::stable_sort(samples.begin(), samples.end(), [](const auto& a, const auto& b)
		{
			if (a.qpc != b.qpc) return a.qpc < b.qpc;
			return static_cast<unsigned>(a.phase) < static_cast<unsigned>(b.phase);
		});
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
			Dispatch(detector_.Move(moveQpc, currentPointers(), input.qpcFrequency), input, events);
			moveQpc = 0;
		};
		for (const auto& sample : samples)
		{
			if (sample.qpc <= 0 || sample.qpc < lastInputQpc_ || sample.qpc > input.frameQpc ||
				!std::isfinite(sample.x) || !std::isfinite(sample.y)) continue;
			auto found = std::find_if(contacts_.begin(), contacts_.end(), [&](const Contact& contact)
				{ return contact.held && contact.pointer.key == sample.key; });
			if (sample.phase == CanvasPanContactPhase::Move)
			{
				// 仅 Down 建立触点，迟到 Move 不能复活已回收的 key；连续Move段共享一次MOVE。
				if (found == contacts_.end() || sample.qpc <= found->lastQpc) continue;
				found->pointer = { sample.key, sample.x, sample.y }; found->lastQpc = sample.qpc;
				moveQpc = (std::max)(moveQpc, sample.qpc); lastInputQpc_ = sample.qpc;
				++events.sampleCount; continue;
			}
			// 拓扑事件作为MOVE合并边界，先派发前段，保持严格QPC顺序。
			flushMove();
			if (sample.phase == CanvasPanContactPhase::Down)
			{
				if (found != contacts_.end()) continue;
				found = std::find_if(contacts_.begin(), contacts_.end(), [](const Contact& contact) { return !contact.held; });
				if (found == contacts_.end()) continue;
				const bool first = HeldContactCount() == 0;
				*found = { true, { sample.key, sample.x, sample.y }, sample.qpc };
				Dispatch(detector_.Down(sample.qpc, currentPointers(), sample.key, input.qpcFrequency), input, events);
				if (first && seedKey_ == sample.key && !seedSamples_.empty())
				{
					auto& tracker = detector_.Tracker(); tracker.Clear();
					for (const auto& history : seedSamples_)
					{
						if (history.qpc >= sample.qpc) break;
						const CanvasPanPointer pointer{ sample.key, history.x, history.y };
						tracker.AddMovement(history.qpc, std::span(&pointer, 1), input.qpcFrequency);
					}
					tracker.AddMovement(sample.qpc, currentPointers(), input.qpcFrequency);
					seedSamples_.clear();
				}
				lastInputQpc_ = sample.qpc; ++events.sampleCount;
			}
			else
			{
				// 终态允许等于最后Move的QPC（同QPC Down<Move<Up），倒退仍无副作用。
				if (found == contacts_.end() || sample.qpc < found->lastQpc) continue;
				found->pointer = { sample.key, sample.x, sample.y };
				const bool last = HeldContactCount() == 1;
				// 先冻结年龄；PointerUp 的 80ms 清窗会清掉最后样本时刻。
				const int64_t latest = detector_.Tracker().LastSampleQpc();
				detector_.Tracker().PointerUp(sample.qpc, input.qpcFrequency);
				const auto estimate = sample.phase == CanvasPanContactPhase::Cancelled ? CanvasPanVelocityEstimate{} :
					detector_.Tracker().Estimate(sample.key, input.qpcFrequency);
				if (last)
				{
					events.released = true; events.releaseSpeed = std::hypot(estimate.x, estimate.y);
					events.releaseAgeSeconds = latest ? PanSecondsBetween(sample.qpc, latest, input.qpcFrequency) : 0.0;
				}
				Dispatch(detector_.Up(sample.qpc, currentPointers(), sample.key,
					sample.phase == CanvasPanContactPhase::Cancelled, input.qpcFrequency), input, events);
				found->held = false; lastInputQpc_ = sample.qpc; ++events.sampleCount;
			}
		}
		flushMove();
		if (input.penHover && fling_.Active()) StopFling();
		const CanvasVector flingDelta = fling_.Progress(static_cast<double>(input.frameQpc) / input.qpcFrequency, booster_);
		events.contentDelta.x += flingDelta.x; events.contentDelta.y += flingDelta.y;
		velocity_ = {};
		if (fling_.Active()) velocity_ = fling_.Velocity();
		else if (HeldContactCount()) velocity_ = Snapshot(input.qpcFrequency).handVelocity;
		auto out = Snapshot(input.qpcFrequency);
		out.contentDelta = events.contentDelta; out.released = events.released;
		out.releaseSpeed = events.releaseSpeed; out.releaseAgeSeconds = events.releaseAgeSeconds;
		out.sampleCount = events.sampleCount;
		return out;
	}

	void CanvasPanDynamics::Stop() noexcept
	{
		contacts_ = {}; detector_.Reset(); fling_.StopFling(booster_); velocity_ = {};
		lastFrameQpc_ = lastInputQpc_ = 0; lastScrollSeconds_.reset(); seedSamples_.clear(); seedKey_ = 0;
	}
	void CanvasPanDynamics::StopFling() noexcept { fling_.StopFling(booster_); velocity_ = {}; }
	void CanvasPanDynamics::ClearAxis(bool x, bool y) noexcept
	{
		detector_.Tracker().ClearAxis(x, y); fling_.ClearAxis(x, y, booster_);
		if (x) velocity_.x = 0.0f;
		if (y) velocity_.y = 0.0f;
	}
	CanvasPanPhase CanvasPanDynamics::Phase() const noexcept
	{ return HeldContactCount() ? CanvasPanPhase::Locked : (fling_.Active() ? CanvasPanPhase::Coasting : CanvasPanPhase::Idle); }
	bool CanvasPanDynamics::Active() const noexcept { return HeldContactCount() != 0 || fling_.Active(); }
	CanvasVector CanvasPanDynamics::ContentVelocity() const noexcept { return velocity_; }
	size_t CanvasPanDynamics::HeldContactCount() const noexcept
	{
		return static_cast<size_t>(std::count_if(contacts_.begin(), contacts_.end(), [](const Contact& contact) { return contact.held; }));
	}
	const CanvasPanVelocityTracker& CanvasPanDynamics::VelocityTracker() const noexcept { return detector_.Tracker(); }
	bool CanvasPanDynamics::SeedContactHistory(uint64_t key,
		std::span<const CanvasPanVelocitySample> samples, int64_t frequency) noexcept
	{
		// 多指历史没有定义缺失id的补位规则，先只开放首指，避免伪造其余触点轨迹。
		if (frequency <= 0 || HeldContactCount() || samples.empty()) return false;
		int64_t previous = 0;
		for (const auto& sample : samples)
		{
			if (sample.qpc <= previous || !std::isfinite(sample.x) || !std::isfinite(sample.y)) return false;
			previous = sample.qpc;
		}
		seedKey_ = key; seedSamples_.assign(samples.begin(), samples.end()); return true;
	}

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
