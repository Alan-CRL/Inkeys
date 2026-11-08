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
		default: return "none";
		}
	}

	const CanvasPanReleaseVelocityTracker::History* CanvasPanReleaseVelocityTracker::Find(uint64_t key) const noexcept
	{
		for (const auto& history : histories_) if (history.used && history.key == key) return &history;
		return nullptr;
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
			const double maximumDistance = kCanvasPanProductVelocityLimit *
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
					// 反向累计一 DIP 才确认为转折，避免亚像素往返噪声不断清窗。
					if (reverseX * history.direction.x + reverseY * history.direction.y <= -kCanvasPanReleaseReversalDip)
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
					if (distance >= kCanvasPanReleaseReversalDip || PanZero(history.direction))
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
			if (dt + 1e-9 < kCanvasPanReleaseMinimumIntervalSeconds || distance < kCanvasPanReleaseReversalDip) continue;
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

	CanvasPanGestureBatch CanvasPanGestureDetector::Down(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, uint64_t key, int64_t frequency, bool trackVelocity) noexcept
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
		if (trackVelocity) tracker_.AddMovement(qpc, pointers, frequency);
		lastFocus_ = PanFocus(pointers);
		return batch;
	}

	CanvasPanGestureBatch CanvasPanGestureDetector::Move(int64_t qpc,
		std::span<const CanvasPanPointer> pointers, int64_t frequency, bool trackVelocity) noexcept
	{
		CanvasPanGestureBatch batch;
		if (trackVelocity) tracker_.AddMovement(qpc, pointers, frequency);
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
		std::span<const CanvasPanPointer> pointers, uint64_t key, bool cancelled, int64_t frequency,
		std::optional<CanvasPanVelocityEstimate> releaseOverride) noexcept
	{
		CanvasPanGestureBatch batch;
		tracker_.PointerUp(qpc, frequency);
		if (pointers.size() > 1)
		{
			lastFocus_ = PanFocus(pointers, key);
			if (cancelled) tracker_.ClearPointer(key);
			else if (!releaseOverride)
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
		const auto velocity = releaseOverride.value_or(tracker_.Estimate(key, frequency));
		if (!cancelled && !withinSlop_ &&
			(releaseOverride ? std::hypot(velocity.x, velocity.y) > kCanvasPanMinimumFlingVelocity :
			(std::abs(velocity.x) > kCanvasPanMinimumFlingVelocity || std::abs(velocity.y) > kCanvasPanMinimumFlingVelocity)))
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
	CanvasVector CanvasPanFlingBooster::GetVelocityForFlingStart(const CanvasPanGestureEvent& event, bool projectCurrentResidual) noexcept
	{
		CanvasVector velocity = event.velocity;
		if (projectCurrentResidual)
		{
			const double speed = std::hypot(velocity.x, velocity.y);
			if (speed > 0.0)
			{
				// 旧动画已停止；仅投影当前残余，不能恢复历史初速或带入旧横向分量。
				const double ux = velocity.x / speed, uy = velocity.y / speed;
				const double residual = ShouldBoostFling(event) ?
					(std::max)(0.0, currentVelocity_.x * ux + currentVelocity_.y * uy) : 0.0;
				const double bounded = (std::min)(speed + residual, kCanvasPanProductVelocityLimit);
				velocity = ToCanvasVector(ux * bounded, uy * bounded);
			}
		}
		else if (ShouldBoostFling(event)) { velocity.x += startingVelocity_.x; velocity.y += startingVelocity_.y; }
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
		if (kind == CanvasPanFlingCurveKind::Friction)
		{
			frictionInitialSpeed_ = std::hypot(velocity.x, velocity.y);
			frictionDirection_ = ToCanvasVector(velocity.x / frictionInitialSpeed_, velocity.y / frictionInitialSpeed_);
			const double scale = kCanvasPanFrictionDeceleration * kCanvasPanFrictionTauSeconds;
			duration_ = kCanvasPanFrictionTauSeconds * std::log1p(frictionInitialSpeed_ / scale);
			const double distance = frictionInitialSpeed_ * kCanvasPanFrictionTauSeconds - scale * duration_;
			distance_ = ToCanvasVector(frictionDirection_.x * distance, frictionDirection_.y * distance);
			return;
		}
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
		if (kind_ == CanvasPanFlingCurveKind::Friction)
		{
			const double tau = kCanvasPanFrictionTauSeconds;
			const double scale = kCanvasPanFrictionDeceleration * tau;
			const double decay = std::exp(-elapsed / tau);
			const double distance = (frictionInitialSpeed_ + scale) * tau * -std::expm1(-elapsed / tau) - scale * elapsed;
			const double speed = (std::max)(0.0, (frictionInitialSpeed_ + scale) * decay - scale);
			offset = ToCanvasVector(frictionDirection_.x * distance, frictionDirection_.y * distance);
			velocity = ToCanvasVector(frictionDirection_.x * speed, frictionDirection_.y * speed);
		}
		else if (kind_ == CanvasPanFlingCurveKind::Legacy)
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
		if (x) { distance_.x = previousOffset_.x = legacyRatio_.x = frictionDirection_.x = 0.0f; }
		if (y) { distance_.y = previousOffset_.y = legacyRatio_.y = frictionDirection_.y = 0.0f; }
	}
	double CanvasPanFlingCurve::Duration() const noexcept { return duration_; }
	CanvasVector CanvasPanFlingCurve::Distance() const noexcept { return distance_; }
	CanvasVector CanvasPanFlingCurve::P1() const noexcept { return p1_; }

	void CanvasPanFlingController::Start(const CanvasPanGestureEvent& event, double lastScroll,
		CanvasVector viewport, CanvasPanFlingBooster& booster, CanvasPanFlingCurveKind kind) noexcept
	{
		Cancel();
		productFriction_ = kind == CanvasPanFlingCurveKind::Friction;
		velocity_ = booster.GetVelocityForFlingStart(event, productFriction_);
		if (PanZero(velocity_) || viewport.x <= 0.0f || viewport.y <= 0.0f)
		{ StopFling(booster); return; }
		const float raw = (std::max)(std::abs(event.velocity.x), std::abs(event.velocity.y));
		const float boosted = (std::max)(std::abs(velocity_.x), std::abs(velocity_.y));
		curve_.Start(velocity_, viewport, raw > 0.0f ? boosted / raw : 1.0f, kind);
		startSeconds_ = lastScroll;
		active_ = true;
	}
	CanvasVector CanvasPanFlingController::Progress(double now, CanvasPanFlingBooster& booster) noexcept
	{
		if (!active_) return {};
		if (!productFriction_ && !firstUpdateSent_ && now >= startSeconds_ + 2.0 * kCanvasPanFlingFrameSeconds)
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
			if (productFriction_)
			{
				// 闭式曲线终帧补齐实际剩余位移，不能随呈现帧率丢掉不同的尾巴。
				StopFling(booster); return delta;
			}
			// Chromium 在曲线 inactive 时立即结束，终点剩余 delta 不派发。
			StopFling(booster); return {};
		}
		if (productFriction_ || std::abs(delta.x) > kCanvasPanScrollEpsilonDip || std::abs(delta.y) > kCanvasPanScrollEpsilonDip)
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
		else booster.ObserveProgressFling(velocity_);
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
			const auto estimate = releaseTracker_.Estimate(contact.pointer.key, frequency);
			out.pointerDiagnostics[count] = { contact.pointer.key,
				releaseTracker_.SampleCount(contact.pointer.key), ToCanvasVector(estimate.x, estimate.y) };
			const double age = frequency > 0 ? PanSecondsBetween(lastFrameQpc_,
				releaseTracker_.LastSampleQpc(contact.pointer.key), frequency) : kCanvasPanReleaseZeroAgeSeconds;
			const double gate = std::clamp((kCanvasPanReleaseZeroAgeSeconds - age) /
				(kCanvasPanReleaseZeroAgeSeconds - kCanvasPanReleaseFullAgeSeconds), 0.0, 1.0);
			const double speed = std::hypot(estimate.x, estimate.y);
			const auto tailLimit = releaseTracker_.RecentSpeedLimit(contact.pointer.key, frequency, ToCanvasVector(estimate.x, estimate.y));
			const double limit = (std::min)(kCanvasPanProductVelocityLimit, tailLimit.value_or(kCanvasPanProductVelocityLimit));
			const double scale = speed > limit ? limit / speed : 1.0;
			if (estimate.valid) { vx += estimate.x * gate * scale; vy += estimate.y * gate * scale; }
			out.handVelocityKnown |= estimate.valid; ++count;
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
				// 落指先停动画；booster中的当前残余仅作为后续同向释放候选。
				fling_.Cancel(); lastScrollSeconds_.reset(); velocity_ = {};
				break;
			case CanvasPanGestureType::ScrollBegin:
				lastScrollSeconds_.reset();
				break;
			case CanvasPanGestureType::ScrollEnd:
				lastScrollSeconds_.reset(); booster_.Reset();
				break;
			case CanvasPanGestureType::ScrollUpdate:
				out.contentDelta.x += event.delta.x; out.contentDelta.y += event.delta.y;
				lastScrollSeconds_ = event.timeSeconds;
				break;
			case CanvasPanGestureType::FlingStart:
				out.released = true; out.releaseSpeed = std::hypot(event.velocity.x, event.velocity.y);
				// 输入释放时间只决定样本时效；动画从本次呈现起步，不追补排队时间。
				fling_.Start(event, static_cast<double>(input.frameQpc) / input.qpcFrequency,
					input.viewportSizeDip, booster_, CanvasPanFlingCurveKind::Friction);
				out.boostedReleaseVelocity = fling_.Velocity();
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
			Dispatch(detector_.Move(moveQpc, currentPointers(), input.qpcFrequency, false), input, events);
			moveQpc = 0;
		};
		for (const auto& sample : samples)
		{
			if (sample.qpc <= 0 || sample.qpc > input.frameQpc ||
				!std::isfinite(sample.x) || !std::isfinite(sample.y)) { ++events.rejectedSamples; continue; }
			auto found = std::find_if(contacts_.begin(), contacts_.end(), [&](const Contact& contact)
				{ return contact.held && contact.pointer.key == sample.key; });
			if (sample.phase == CanvasPanContactPhase::Move)
			{
				// 仅同时间的几何更新可合并；每个真实样本都进入该指自己的估速历史。
				if (found == contacts_.end() || sample.qpc <= found->lastQpc) { ++events.rejectedSamples; continue; }
				if (moveQpc && moveQpc != sample.qpc) flushMove();
				const auto result = releaseTracker_.AddSample(sample.key, { sample.qpc, sample.x, sample.y }, input.qpcFrequency);
				if (result == CanvasPanSampleDisposition::Reversed || result == CanvasPanSampleDisposition::TimingRejected)
				{
					booster_.Reset();
					if (result == CanvasPanSampleDisposition::Reversed) ++events.reversalCount;
					else ++events.timingResetCount;
				}
				found->pointer = { sample.key, sample.x, sample.y }; found->lastQpc = sample.qpc;
				moveQpc = sample.qpc; lastInputQpc_ = (std::max)(lastInputQpc_, sample.qpc);
				++events.sampleCount; continue;
			}
			// 拓扑事件先结清此前真实Move；新Down不能因另一指更晚的采样时间而丢失。
			flushMove();
			if (sample.phase == CanvasPanContactPhase::Down)
			{
				if (found != contacts_.end()) continue;
				found = std::find_if(contacts_.begin(), contacts_.end(), [](const Contact& contact) { return !contact.held; });
				if (found == contacts_.end()) continue;
				const bool first = HeldContactCount() == 0;
				// 必须在真实Down边界隔离来源，同批旧Up会在Register之后才重新生成fling。
				if (sample.sourceKey != lastSourceKey_) booster_.Reset();
				lastSourceKey_ = sample.sourceKey;
				*found = { true, { sample.key, sample.x, sample.y }, sample.qpc };
				if (first) releaseTracker_.Clear();
				releaseTracker_.ClearPointer(sample.key);
				Dispatch(detector_.Down(sample.qpc, currentPointers(), sample.key, input.qpcFrequency, false), input, events);
				if (first && seedKey_ == sample.key && !seedSamples_.empty())
				{
					for (const auto& history : seedSamples_)
					{
						if (history.qpc >= sample.qpc) break;
						releaseTracker_.AddSample(sample.key, history, input.qpcFrequency);
					}
					seedSamples_.clear();
				}
				releaseTracker_.AddSample(sample.key, { sample.qpc, sample.x, sample.y }, input.qpcFrequency);
				lastInputQpc_ = (std::max)(lastInputQpc_, sample.qpc); ++events.sampleCount;
			}
			else
			{
				// 终态仅闭合；最后真实Move已在有序输入流中消费，不拿Up坐标虚构速度。
				if (found == contacts_.end()) { ++events.rejectedSamples; continue; }
				// 已准入终态必须闭合触点；坏时间只取消释放，不能把held永久留在内核。
				const bool invalidTerminalTime = sample.qpc < found->lastQpc;
				if (invalidTerminalTime) ++events.rejectedSamples;
				found->pointer = { sample.key, sample.x, sample.y };
				const bool last = HeldContactCount() == 1;
				const bool cancelled = sample.phase == CanvasPanContactPhase::Cancelled || invalidTerminalTime;
				const int64_t latest = releaseTracker_.LastSampleQpc(sample.key);
				const double age = latest ? PanSecondsBetween(sample.qpc, latest, input.qpcFrequency) : 0.0;
				const auto raw = releaseTracker_.Estimate(sample.key, input.qpcFrequency);
				auto selected = raw.valid ? raw : CanvasPanVelocityEstimate{};
				const double gate = std::clamp((kCanvasPanReleaseZeroAgeSeconds - age) /
					(kCanvasPanReleaseZeroAgeSeconds - kCanvasPanReleaseFullAgeSeconds), 0.0, 1.0);
				const double speed = std::hypot(raw.x, raw.y);
				const auto tailLimit = raw.valid ? releaseTracker_.RecentSpeedLimit(sample.key, input.qpcFrequency,
					ToCanvasVector(raw.x, raw.y)) : std::optional<double>{};
				const double limit = (std::min)(kCanvasPanProductVelocityLimit, tailLimit.value_or(kCanvasPanProductVelocityLimit));
				const double scale = speed > limit ? limit / speed : 1.0;
				selected.x *= gate * scale; selected.y *= gate * scale;
				if (cancelled) { selected = {}; booster_.Reset(); }
				if (last)
				{
					lastSourceKey_ = sample.sourceKey;
					events.released = true; events.releaseAgeSeconds = age;
					events.releaseTailSpeedLimit = tailLimit.value_or(0.0);
					events.releaseTailSpeedLimited = tailLimit && *tailLimit < speed;
					events.rawReleaseVelocity = ToCanvasVector(raw.x, raw.y);
					events.selectedReleaseVelocity = ToCanvasVector(selected.x, selected.y);
					events.releaseSpeed = std::hypot(selected.x, selected.y);
					events.releaseSampleCount = releaseTracker_.SampleCount(sample.key);
					events.releaseSpanSeconds = releaseTracker_.SampleSpanSeconds(sample.key, input.qpcFrequency);
					events.releaseReason = invalidTerminalTime ? CanvasPanReleaseReason::TimingRejected :
						cancelled ? CanvasPanReleaseReason::Cancelled :
						age >= kCanvasPanReleaseZeroAgeSeconds ? CanvasPanReleaseReason::Stale :
						releaseTracker_.Reason(sample.key, input.qpcFrequency);
					if (events.releaseReason == CanvasPanReleaseReason::Ready && !raw.valid)
						events.releaseReason = CanvasPanReleaseReason::DirectionConflict;
					else if (events.releaseReason == CanvasPanReleaseReason::Ready && events.releaseSpeed <= kCanvasPanMinimumFlingVelocity)
						events.releaseReason = CanvasPanReleaseReason::Stationary;
				}
				else if (!cancelled && selected.valid)
				{
					for (const auto& other : contacts_)
					{
						if (!other.held || other.pointer.key == sample.key) continue;
						const auto velocity = releaseTracker_.Estimate(other.pointer.key, input.qpcFrequency);
						const double otherAge = PanSecondsBetween(sample.qpc,
							releaseTracker_.LastSampleQpc(other.pointer.key), input.qpcFrequency);
						if (velocity.valid && otherAge < kCanvasPanReleaseZeroAgeSeconds && selected.x * velocity.x + selected.y * velocity.y < 0.0)
						{ releaseTracker_.Clear(); booster_.Reset(); break; }
					}
				}
				Dispatch(detector_.Up(sample.qpc, currentPointers(), sample.key, cancelled,
					input.qpcFrequency, selected), input, events);
				if (last && !fling_.Active())
				{
					booster_.Reset(); // 点按、弱尾段和取消不能把旧残余留给下一次手势复活。
					if (events.releaseReason == CanvasPanReleaseReason::Ready)
						events.releaseReason = input.viewportSizeDip.x <= 0 || input.viewportSizeDip.y <= 0 ?
							CanvasPanReleaseReason::EmptyViewport : CanvasPanReleaseReason::WithinSlop;
				}
				releaseTracker_.ClearPointer(sample.key);
				found->held = false; lastInputQpc_ = (std::max)(lastInputQpc_, sample.qpc); ++events.sampleCount;
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
		out.rawReleaseVelocity = events.rawReleaseVelocity; out.selectedReleaseVelocity = events.selectedReleaseVelocity;
		out.boostedReleaseVelocity = events.boostedReleaseVelocity; out.releaseReason = events.releaseReason;
		out.releaseSampleCount = events.releaseSampleCount; out.releaseSpanSeconds = events.releaseSpanSeconds;
		out.releaseTailSpeedLimit = events.releaseTailSpeedLimit; out.releaseTailSpeedLimited = events.releaseTailSpeedLimited;
		out.rejectedSamples = events.rejectedSamples; out.reversalCount = events.reversalCount; out.timingResetCount = events.timingResetCount;
		return out;
	}

	void CanvasPanDynamics::Stop() noexcept
	{
		contacts_ = {}; detector_.Reset(); releaseTracker_.Clear(); fling_.StopFling(booster_); velocity_ = {};
		lastFrameQpc_ = lastInputQpc_ = 0; lastSourceKey_ = 0; lastScrollSeconds_.reset(); seedSamples_.clear(); seedKey_ = 0;
	}
	void CanvasPanDynamics::StopFling() noexcept { fling_.StopFling(booster_); velocity_ = {}; }
	void CanvasPanDynamics::ClearAxis(bool x, bool y) noexcept
	{
		releaseTracker_.ClearAxis(x, y); fling_.ClearAxis(x, y, booster_);
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
	const CanvasPanReleaseVelocityTracker& CanvasPanDynamics::VelocityTracker() const noexcept { return releaseTracker_; }
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
