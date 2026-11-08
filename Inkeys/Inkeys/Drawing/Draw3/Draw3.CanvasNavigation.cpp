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

		int64_t PanSecondsToTicks(double seconds, int64_t frequency) noexcept
		{
			return static_cast<int64_t>(seconds * static_cast<double>(frequency));
		}

		// 速度等比限幅到 kCanvasPanVelocityLimitDipPerSecond，保持方向；非有限值无效。
		CanvasPanVelocityEstimate LimitPanVelocity(double x, double y) noexcept
		{
			if (!std::isfinite(x) || !std::isfinite(y)) return {};
			const double speed = std::hypot(x, y);
			if (speed > kCanvasPanVelocityLimitDipPerSecond && speed > 0.0)
			{
				const double scale = kCanvasPanVelocityLimitDipPerSecond / speed;
				x *= scale;
				y *= scale;
			}
			return { true, x, y };
		}

		CanvasVector ToCanvasVector(double x, double y) noexcept
		{
			return { static_cast<float>(x), static_cast<float>(y) };
		}

		double PanDeterminant3(double a11, double a12, double a13, double a21, double a22,
			double a23, double a31, double a32, double a33) noexcept
		{
			return a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) +
				a13 * (a21 * a32 - a22 * a31);
		}
	}

	CanvasPanFrictionStep StepCanvasPanFriction(double speed, double deltaSeconds,
		CanvasPanFriction friction) noexcept
	{
		if (!std::isfinite(speed) || speed < kCanvasPanStopSpeedDipPerSecond) return {};
		if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return { 0.0, speed };
		const double tau = friction.tauSeconds;
		if (!std::isfinite(tau) || tau <= 0.0) return {};
		const double coulomb = std::isfinite(friction.coulombDipPerSecondSquared)
			? (std::max)(friction.coulombDipPerSecondSquared, 0.0) : 0.0;
		const double step = (std::min)(deltaSeconds, kCanvasPanMaximumStepSeconds);
		const double scale = coulomb * tau; // c*tau
		const double amplitude = speed + scale; // A
		const double next = amplitude * std::exp(-step / tau) - scale;
		if (next >= kCanvasPanStopSpeedDipPerSecond)
			return { amplitude * tau * -std::expm1(-step / tau) - scale * step, next };
		// 本步内越过停止阈值：积分到速度恰为阈值的时刻，其后视为静止。
		const double stopSeconds = std::clamp(tau * std::log(amplitude /
			(scale + kCanvasPanStopSpeedDipPerSecond)), 0.0, step);
		return { amplitude * tau * -std::expm1(-stopSeconds / tau) - scale * stopSeconds, 0.0 };
	}

	double CanvasPanGlideDistance(double speed, CanvasPanFriction friction) noexcept
	{
		if (!std::isfinite(speed) || speed <= 0.0 ||
			!std::isfinite(friction.tauSeconds) || friction.tauSeconds <= 0.0) return 0.0;
		const double coulomb = std::isfinite(friction.coulombDipPerSecondSquared)
			? (std::max)(friction.coulombDipPerSecondSquared, 0.0) : 0.0;
		const double scale = coulomb * friction.tauSeconds;
		if (scale <= 0.0) return speed * friction.tauSeconds;
		return speed * friction.tauSeconds -
			scale * friction.tauSeconds * std::log1p(speed / scale);
	}

	double CanvasPanReleaseGate(double ageSeconds) noexcept
	{
		if (!std::isfinite(ageSeconds) || ageSeconds < 0.0) return 0.0;
		if (ageSeconds <= kCanvasPanReleaseGateFullSeconds) return 1.0;
		if (ageSeconds >= kCanvasPanReleaseGateZeroSeconds) return 0.0;
		return (kCanvasPanReleaseGateZeroSeconds - ageSeconds) /
			(kCanvasPanReleaseGateZeroSeconds - kCanvasPanReleaseGateFullSeconds);
	}

	bool CanvasPanVelocityTracker::AddSample(int64_t qpc, double x, double y,
		int64_t qpcFrequency, double frameSeconds) noexcept
	{
		if (qpc <= 0 || qpcFrequency <= 0 || !std::isfinite(x) || !std::isfinite(y))
			return false;
		const int64_t newest = count_ != 0 ? samples_[count_ - 1].qpc : 0;
		if (count_ != 0 && qpc <= newest) return false;
		if (count_ != 0)
		{
			// 以“簇起点”而不是末样本判断合并：1 kHz 逐事件输入不会被链式合并成单个样本。
			if (qpc - clusterStartQpc_ <= PanSecondsToTicks(
				kCanvasPanTrackerMergeSeconds, qpcFrequency))
			{
				samples_[count_ - 1] = { qpc, x, y };
				return true;
			}
			const double frame = std::isfinite(frameSeconds) && frameSeconds > 0.0
				? (std::min)(frameSeconds, kCanvasPanMaximumStepSeconds) : 0.0;
			const double gapSeconds = (std::max)(kCanvasPanTrackerGapSeconds,
				frame + kCanvasPanTrackerGapFramePaddingSeconds);
			// 触摸屏不会为静止手指报点：长间隔意味着手曾停下，开始新的一段。
			if (qpc - newest > PanSecondsToTicks(gapSeconds, qpcFrequency)) count_ = 0;
		}
		if (count_ == samples_.size())
		{
			std::move(samples_.begin() + 1, samples_.end(), samples_.begin());
			--count_;
		}
		samples_[count_++] = { qpc, x, y };
		clusterStartQpc_ = qpc;
		const int64_t horizon = PanSecondsToTicks(kCanvasPanTrackerWindowSeconds, qpcFrequency);
		size_t drop = 0;
		while (drop + 1 < count_ && qpc - samples_[drop].qpc > horizon) ++drop;
		if (drop != 0)
		{
			std::move(samples_.begin() + drop, samples_.begin() + count_, samples_.begin());
			count_ -= drop;
		}
		return true;
	}

	CanvasPanVelocityEstimate CanvasPanVelocityTracker::Estimate(
		int64_t qpcFrequency) const noexcept
	{
		if (qpcFrequency <= 0 || count_ < kCanvasPanTrackerMinimumSamples) return {};
		const int64_t newest = samples_[count_ - 1].qpc;
		const double span = PanSecondsBetween(newest, samples_[0].qpc, qpcFrequency);
		if (!(span >= kCanvasPanTrackerMinimumSpanSeconds)) return {};
		// x(u) = a + b*u + c*u^2，u 为相对最新样本、按窗口归一化的时间([-1,0])；
		// 最新样本时刻的速度为 b / 窗口。
		double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
		double sx = 0.0, sux = 0.0, su2x = 0.0;
		double sy = 0.0, suy = 0.0, su2y = 0.0;
		for (size_t index = 0; index < count_; ++index)
		{
			const CanvasPanVelocitySample& sample = samples_[index];
			const double u = PanSecondsBetween(sample.qpc, newest, qpcFrequency) /
				kCanvasPanTrackerWindowSeconds;
			const double u2 = u * u;
			s1 += u;
			s2 += u2;
			s3 += u2 * u;
			s4 += u2 * u2;
			sx += sample.x;
			sux += u * sample.x;
			su2x += u2 * sample.x;
			sy += sample.y;
			suy += u * sample.y;
			su2y += u2 * sample.y;
		}
		const double n = static_cast<double>(count_);
		const double determinant = PanDeterminant3(n, s1, s2, s1, s2, s3, s2, s3, s4);
		double bx = 0.0;
		double by = 0.0;
		if (std::isfinite(determinant) && determinant > 1.0e-12)
		{
			// Cramer 法则：用右端项替换第二列(b)。
			bx = PanDeterminant3(n, sx, s2, s1, sux, s3, s2, su2x, s4) / determinant;
			by = PanDeterminant3(n, sy, s2, s1, suy, s3, s2, su2y, s4) / determinant;
		}
		else
		{
			const double linear = n * s2 - s1 * s1;
			if (!std::isfinite(linear) || linear <= 1.0e-12) return {};
			bx = (n * sux - s1 * sx) / linear;
			by = (n * suy - s1 * sy) / linear;
		}
		return LimitPanVelocity(bx / kCanvasPanTrackerWindowSeconds,
			by / kCanvasPanTrackerWindowSeconds);
	}

	void CanvasPanVelocityTracker::ClearAxis(bool x, bool y) noexcept
	{
		if (count_ == 0) return;
		const CanvasPanVelocitySample newest = samples_[count_ - 1];
		for (size_t index = 0; index < count_; ++index)
		{
			if (x) samples_[index].x = newest.x;
			if (y) samples_[index].y = newest.y;
		}
	}

	void CanvasPanVelocityTracker::Reset() noexcept
	{
		count_ = 0;
		clusterStartQpc_ = 0;
	}

	size_t CanvasPanVelocityTracker::SampleCount() const noexcept { return count_; }

	int64_t CanvasPanVelocityTracker::LastSampleQpc() const noexcept
	{
		return count_ != 0 ? samples_[count_ - 1].qpc : 0;
	}

	CanvasPanDynamics::Contact* CanvasPanDynamics::FindHeld(uint64_t key) noexcept
	{
		for (Contact& contact : contacts_)
			if (contact.used && contact.held && contact.key == key) return &contact;
		return nullptr;
	}

	bool CanvasPanDynamics::HasLiftedRecord(uint64_t key) const noexcept
	{
		for (const Contact& contact : contacts_)
			if (contact.used && !contact.held && contact.key == key) return true;
		return false;
	}

	CanvasPanDynamics::Contact* CanvasPanDynamics::Allocate() noexcept
	{
		for (Contact& contact : contacts_)
			if (!contact.used) return &contact;
		return nullptr;
	}

	void CanvasPanDynamics::Ingest(const CanvasPanContactSample& sample,
		int64_t qpcFrequency, double frameSeconds, bool firstHand) noexcept
	{
		if (sample.qpc <= 0 || !std::isfinite(sample.x) || !std::isfinite(sample.y)) return;
		Contact* contact = FindHeld(sample.key);
		const bool terminal = sample.phase == CanvasPanContactPhase::Up ||
			sample.phase == CanvasPanContactPhase::Cancelled;
		if (contact == nullptr)
		{
			if (terminal) return;
			// 已抬起触点的迟到 Move 不能复活成新触点；新代次必须以 Down 开始。
			if (sample.phase == CanvasPanContactPhase::Move &&
				HasLiftedRecord(sample.key)) return;
			contact = Allocate();
			if (contact == nullptr) return;
			*contact = {};
			contact->used = true;
			contact->held = true;
			// 空闲时的首批手：Down 到本帧最新位置的位移也属于跟手内容位移。
			contact->joined = firstHand;
			contact->key = sample.key;
			contact->x = contact->refX = contact->anchorX = sample.x;
			contact->y = contact->refY = contact->anchorY = sample.y;
			contact->anchorQpc = contact->lastSampleQpc = sample.qpc;
			contact->tracker.AddSample(sample.qpc, sample.x, sample.y, qpcFrequency,
				frameSeconds);
			return;
		}
		if (!terminal)
		{
			// 重复/倒退 QPC 保持状态不变（位置与估速器一致）。
			if (sample.qpc <= contact->lastSampleQpc) return;
			contact->tracker.AddSample(sample.qpc, sample.x, sample.y, qpcFrequency,
				frameSeconds);
			contact->x = sample.x;
			contact->y = sample.y;
			contact->lastSampleQpc = sample.qpc;
			return;
		}
		// 抬起：终点位置计入内容位移，但不追加速度样本；冻结已过停手门限的速度。
		const bool cancelled = sample.phase == CanvasPanContactPhase::Cancelled;
		const double age = PanSecondsBetween(sample.qpc, contact->lastSampleQpc, qpcFrequency);
		const CanvasPanVelocityEstimate estimate = contact->tracker.Estimate(qpcFrequency);
		contact->held = false;
		contact->liftedStep = true;
		contact->cancelledStep = cancelled;
		contact->x = sample.x;
		contact->y = sample.y;
		contact->upQpc = (std::max)(sample.qpc, contact->lastSampleQpc);
		contact->lifted = {};
		// Cancelled 与倒退的 Up 时间不给惯性（age<0 时门限为 0，这里显式拒绝）。
		if (estimate.valid && !cancelled && age >= 0.0)
		{
			const double gate = CanvasPanReleaseGate(age);
			contact->lifted = { true, estimate.x * gate, estimate.y * gate };
		}
	}

	CanvasPanVelocityEstimate CanvasPanDynamics::HandVelocity(int64_t referenceQpc,
		int64_t qpcFrequency) const noexcept
	{
		// 保留早抬手的速度以容忍错开抬起；但剩余手已停报 >=70 ms 时，旧手速不能复活惯性。
		// 最后 Up 也算剩余手的证据，按 Up 时间区分先后，兼容两次 Up 在同帧消费。
		bool remainingStopped = false;
		for (const Contact& contact : contacts_)
		{
			if (contact.used && (contact.held ||
				(contact.liftedStep && contact.upQpc == referenceQpc)) &&
				PanSecondsBetween(referenceQpc, contact.lastSampleQpc, qpcFrequency) >=
					kCanvasPanReleaseGateZeroSeconds)
			{
				remainingStopped = true;
				break;
			}
		}
		double sumX = 0.0;
		double sumY = 0.0;
		size_t count = 0;
		for (const Contact& contact : contacts_)
		{
			if (!contact.used) continue;
			if (contact.held)
			{
				const CanvasPanVelocityEstimate estimate = contact.tracker.Estimate(qpcFrequency);
				if (!estimate.valid) continue;
				// 按下但已停止上报的手：速度随停手年龄软衰减到 0。
				const double gate = CanvasPanReleaseGate((std::max)(0.0,
					PanSecondsBetween(referenceQpc, contact.lastSampleQpc, qpcFrequency)));
				sumX += estimate.x * gate;
				sumY += estimate.y * gate;
				++count;
			}
			else if (contact.lifted.valid && PanSecondsBetween(referenceQpc, contact.upQpc,
				qpcFrequency) <= kCanvasPanLiftedHandHoldSeconds)
			{
				if (remainingStopped && contact.upQpc < referenceQpc) continue;
				sumX += contact.lifted.x;
				sumY += contact.lifted.y;
				++count;
			}
		}
		if (count == 0) return {};
		return LimitPanVelocity(sumX / static_cast<double>(count),
			sumY / static_cast<double>(count));
	}

	bool CanvasPanDynamics::HandStationary(int64_t nowQpc, int64_t qpcFrequency) const noexcept
	{
		size_t participants = 0;
		for (const Contact& contact : contacts_)
		{
			if (!contact.used || !(contact.held || contact.liftedStep)) continue;
			++participants;
			// 已抬起的手只提供到 Up 为止的静止证据；排队到低帧率消费帧不延长轻点。
			const int64_t evidenceQpc = contact.held ? nowQpc : (std::min)(nowQpc, contact.upQpc);
			const bool settled =
				PanSecondsBetween(evidenceQpc, contact.anchorQpc, qpcFrequency) >=
					kCanvasPanHandStationaryDelaySeconds &&
				std::hypot(contact.x - contact.anchorX, contact.y - contact.anchorY) <
					kCanvasPanHandStationaryDisplacementDip;
			// 位移已超过阈值但从未形成有效速度、且已停止上报：同样是静止证据。
			const bool silent = !contact.tracker.Estimate(qpcFrequency).valid &&
				PanSecondsBetween(evidenceQpc, contact.lastSampleQpc, qpcFrequency) >=
					kCanvasPanHandStationaryDelaySeconds;
			if (!settled && !silent) return false;
		}
		return participants != 0;
	}

	void CanvasPanDynamics::EndStep(int64_t nowQpc, size_t heldContacts,
		int64_t qpcFrequency) noexcept
	{
		const int64_t holdTicks = PanSecondsToTicks(kCanvasPanLiftedHandHoldSeconds,
			qpcFrequency);
		for (Contact& contact : contacts_)
		{
			if (!contact.used) continue;
			if (contact.held)
			{
				// 触点数量变化/锁定只重基线内容位移，估速器保持不动。
				contact.refX = contact.x;
				contact.refY = contact.y;
				contact.joined = true;
				continue;
			}
			// 最后一只手离开后，抬起记录不得污染下一次按下的手速。
			if (heldContacts == 0 || nowQpc - contact.upQpc > holdTicks) contact = {};
			else
			{
				contact.liftedStep = false;
				contact.cancelledStep = false;
				contact.joined = false;
			}
		}
	}

	CanvasPanFrameOutput CanvasPanDynamics::Step(const CanvasPanFrameInput& input) noexcept
	{
		CanvasPanFrameOutput out;
		out.phase = phase_;
		out.contentVelocity = ToCanvasVector(velocityX_, velocityY_);
		out.heldContacts = HeldContactCount();
		const int64_t frequency = input.qpcFrequency;
		const int64_t now = input.frameQpc;
		// 非法或倒退的帧时钟：整帧忽略，状态保持不变。
		if (now <= 0 || frequency <= 0) return out;
		if (lastFrameQpc_ > 0 && now < lastFrameQpc_) return out;
		if (input.penContact)
		{
			Stop();
			lastFrameQpc_ = now;
			out.phase = CanvasPanPhase::Idle;
			out.contentVelocity = {};
			out.heldContacts = 0;
			return out;
		}
		int64_t segmentStartQpc = lastFrameQpc_ > 0 ? lastFrameQpc_ : now;
		double dt = lastFrameQpc_ > 0 ? (std::min)(
			PanSecondsBetween(now, lastFrameQpc_, frequency),
			kCanvasPanMaximumStepSeconds) : 0.0;
		lastFrameQpc_ = now;

		auto segmentContacts = contacts_;
		size_t segmentBegin = 0;
		bool firstHand = phase_ == CanvasPanPhase::Idle && HeldContactCount() == 0;
		int64_t releaseBoundaryQpc = 0;
		double totalDx = 0.0;
		double totalDy = 0.0;
		const auto appendSegment = [&](CanvasPanFrameOutput segment) noexcept
		{
			totalDx += segment.contentDelta.x;
			totalDy += segment.contentDelta.y;
			// 最终状态取尾段；若尾段未释放，保留本帧前一手势的释放诊断。
			if (!segment.released && out.released)
			{
				segment.released = true;
				segment.releaseSpeed = out.releaseSpeed;
				segment.releaseAgeSeconds = out.releaseAgeSeconds;
			}
			out = segment;
		};
		for (size_t index = 0; index < input.contacts.size(); ++index)
		{
			const CanvasPanContactSample& sample = input.contacts[index];
			if (releaseBoundaryQpc > 0 && sample.phase == CanvasPanContactPhase::Down &&
				sample.qpc >= releaseBoundaryQpc && std::isfinite(sample.x) && std::isfinite(sample.y))
			{
				// 全部旧触点离开后才开始的新 Down 是下一手势：先复用正常释放，清掉旧冻结手速。
				// 排队终态可能早于上一帧；仅积分时钟限在本帧内，释放门限仍取旧手势自身的 Up。
				const int64_t boundary = std::clamp(releaseBoundaryQpc, segmentStartQpc, now);
				const double segmentDt = (std::min)(PanSecondsBetween(boundary, segmentStartQpc, frequency),
					kCanvasPanMaximumStepSeconds);
				if (segmentDt != dt)
				{
					// 发现分段后按该段帧间隔重放摄入；否则估速断口阈值仍会误用整个绘制帧的 dt。
					// Ingest 只改变触点，按下的 phase 转移不依赖 dt，无需重放状态机。
					contacts_ = segmentContacts;
					for (size_t replay = segmentBegin; replay < index; ++replay)
						Ingest(input.contacts[replay], frequency, segmentDt, firstHand);
				}
				CanvasPanFrameInput segment = input;
				segment.frameQpc = boundary;
				appendSegment(FinishStep(segment, segmentDt));
				segmentContacts = contacts_;
				segmentBegin = index;
				segmentStartQpc = boundary;
				dt = (std::min)(PanSecondsBetween(now, boundary, frequency), kCanvasPanMaximumStepSeconds);
				firstHand = phase_ == CanvasPanPhase::Idle;
				releaseBoundaryQpc = 0;
			}
			const size_t previouslyHeld = HeldContactCount();
			Ingest(sample, frequency, dt, firstHand);
			const size_t held = HeldContactCount();
			if (previouslyHeld != 0 && held == 0)
			{
				for (const Contact& contact : contacts_)
					if (contact.used && contact.liftedStep)
						releaseBoundaryQpc = (std::max)(releaseBoundaryQpc, contact.upQpc);
			}
			else if (held != 0) releaseBoundaryQpc = 0;
			// 按输入顺序立即执行首只手的按下转移，同帧 Up 也走该手势的正常释放路径。
			if (phase_ == CanvasPanPhase::Idle && held != 0) phase_ = CanvasPanPhase::Locked;
			else if (phase_ == CanvasPanPhase::Coasting && held != 0) phase_ = CanvasPanPhase::Slipping;
		}
		appendSegment(FinishStep(input, dt));
		out.contentDelta = ToCanvasVector(totalDx, totalDy);
		return out;
	}

	CanvasPanFrameOutput CanvasPanDynamics::FinishStep(const CanvasPanFrameInput& input,
		double dt) noexcept
	{
		CanvasPanFrameOutput out;
		const int64_t frequency = input.qpcFrequency;
		const int64_t now = input.frameQpc;
		size_t held = 0;
		bool cancelled = false;
		int64_t lastUp = 0;
		int64_t newestLiftedSample = 0;
		double sumDx = 0.0;
		double sumDy = 0.0;
		size_t common = 0;
		for (const Contact& contact : contacts_)
		{
			if (!contact.used) continue;
			if (contact.held) ++held;
			else if (contact.liftedStep)
			{
				cancelled = cancelled || contact.cancelledStep;
				lastUp = (std::max)(lastUp, contact.upQpc);
				newestLiftedSample = (std::max)(newestLiftedSample, contact.lastSampleQpc);
			}
			// 只统计上一帧已参与基线的触点：新触点只加入基线，质心不跳。
			// 位置没有变化的抬起触点只是退出，不拖低剩余手指的平均位移。
			const bool liftedMoved = contact.liftedStep &&
				(contact.x != contact.refX || contact.y != contact.refY);
			if (contact.joined && (contact.held || liftedMoved))
			{
				sumDx += contact.x - contact.refX;
				sumDy += contact.y - contact.refY;
				++common;
			}
		}
		const double centroidDx = common != 0 ? sumDx / static_cast<double>(common) : 0.0;
		const double centroidDy = common != 0 ? sumDy / static_cast<double>(common) : 0.0;

		if (phase_ == CanvasPanPhase::Idle && held != 0) phase_ = CanvasPanPhase::Locked;
		else if (phase_ == CanvasPanPhase::Coasting && held != 0)
			phase_ = CanvasPanPhase::Slipping;

		double dx = 0.0;
		double dy = 0.0;
		if (phase_ == CanvasPanPhase::Locked)
		{
			dx = centroidDx;
			dy = centroidDy;
			if (held != 0)
			{
				const CanvasPanVelocityEstimate hand = HandVelocity(now, frequency);
				velocityX_ = hand.valid ? hand.x : 0.0;
				velocityY_ = hand.valid ? hand.y : 0.0;
				out.handVelocityKnown = hand.valid;
				out.handVelocity = ToCanvasVector(velocityX_, velocityY_);
			}
			else
			{
				// 全部手离开：释放速度 = 抬起手(80 ms 内)已过停手门限的速度均值。
				const CanvasPanVelocityEstimate release = HandVelocity(
					lastUp > 0 ? lastUp : now, frequency);
				const double speed = release.valid ? std::hypot(release.x, release.y) : 0.0;
				out.released = true;
				out.releaseSpeed = cancelled ? 0.0 : speed; // 诊断：低于门限时仍报告计算出的速度
				out.releaseAgeSeconds = lastUp > 0
					? PanSecondsBetween(lastUp, newestLiftedSample, frequency) : 0.0;
				if (!cancelled && speed >= kCanvasPanMinimumReleaseSpeedDipPerSecond)
				{
					phase_ = CanvasPanPhase::Coasting;
					velocityX_ = release.x;
					velocityY_ = release.y;
				}
				else
				{
					phase_ = CanvasPanPhase::Idle;
					velocityX_ = velocityY_ = 0.0;
				}
			}
		}
		else if (phase_ == CanvasPanPhase::Slipping)
		{
			if (held == 0 && cancelled)
			{
				// 系统取消手势：不给惯性。
				phase_ = CanvasPanPhase::Idle;
				velocityX_ = velocityY_ = 0.0;
				out.released = true;
			}
			else
			{
				CanvasPanVelocityEstimate hand = HandVelocity(now, frequency);
				if (!hand.valid && HandStationary(now, frequency)) hand = { true, 0.0, 0.0 };
				out.handVelocityKnown = hand.valid;
				out.handVelocity = ToCanvasVector(hand.x, hand.y);
				bool lock = false;
				if (hand.valid)
				{
					// 位置只积分画布速度 v = v手 + w；w 按手摩擦衰减，趋零即抓住。
					const double wx = velocityX_ - hand.x;
					const double wy = velocityY_ - hand.y;
					const double slip = std::hypot(wx, wy);
					const CanvasPanFrictionStep step = StepCanvasPanFriction(
						slip, dt, kCanvasPanHandFriction);
					const double ux = slip > 0.0 ? wx / slip : 0.0;
					const double uy = slip > 0.0 ? wy / slip : 0.0;
					dx = hand.x * dt + ux * step.distance;
					dy = hand.y * dt + uy * step.distance;
					velocityX_ = hand.x + ux * step.speed;
					velocityY_ = hand.y + uy * step.speed;
					lock = step.speed <= 0.0;
				}
				else
				{
					// 手速未知且尚无静止证据：暂不耦合，只受自由摩擦。
					const double speed = std::hypot(velocityX_, velocityY_);
					const CanvasPanFrictionStep step = StepCanvasPanFriction(
						speed, dt, kCanvasPanFreeFriction);
					const double ux = speed > 0.0 ? velocityX_ / speed : 0.0;
					const double uy = speed > 0.0 ? velocityY_ / speed : 0.0;
					dx = ux * step.distance;
					dy = uy * step.distance;
					velocityX_ = ux * step.speed;
					velocityY_ = uy * step.speed;
					// 画布已在未分类的手下停住：按“静止画布按下即抓住”处理。
					lock = step.speed <= 0.0;
				}
				if (held == 0)
				{
					// 未抓住前抬起：保持当前画布速度继续自由滑行。
					out.released = true;
					out.releaseAgeSeconds = lastUp > 0
						? PanSecondsBetween(lastUp, newestLiftedSample, frequency) : 0.0;
					out.releaseSpeed = std::hypot(velocityX_, velocityY_);
					if (out.releaseSpeed >= kCanvasPanStopSpeedDipPerSecond)
						phase_ = CanvasPanPhase::Coasting;
					else
					{
						phase_ = CanvasPanPhase::Idle;
						velocityX_ = velocityY_ = 0.0;
					}
				}
				else if (lock) phase_ = CanvasPanPhase::Locked;
			}
		}
		else if (phase_ == CanvasPanPhase::Coasting)
		{
			// 无手：自由摩擦；笔悬停改用手摩擦(v手=0)加速制动。
			const double speed = std::hypot(velocityX_, velocityY_);
			const CanvasPanFrictionStep step = StepCanvasPanFriction(speed, dt,
				input.penHover ? kCanvasPanHandFriction : kCanvasPanFreeFriction);
			const double ux = speed > 0.0 ? velocityX_ / speed : 0.0;
			const double uy = speed > 0.0 ? velocityY_ / speed : 0.0;
			dx = ux * step.distance;
			dy = uy * step.distance;
			velocityX_ = ux * step.speed;
			velocityY_ = uy * step.speed;
			if (step.speed <= 0.0)
			{
				phase_ = CanvasPanPhase::Idle;
				velocityX_ = velocityY_ = 0.0;
			}
		}

		EndStep(now, held, frequency);
		out.contentDelta = ToCanvasVector(dx, dy);
		out.contentVelocity = ToCanvasVector(velocityX_, velocityY_);
		out.phase = phase_;
		out.heldContacts = held;
		return out;
	}

	void CanvasPanDynamics::Stop() noexcept
	{
		contacts_ = {};
		phase_ = CanvasPanPhase::Idle;
		velocityX_ = velocityY_ = 0.0;
		lastFrameQpc_ = 0;
	}

	void CanvasPanDynamics::ClearAxis(bool x, bool y) noexcept
	{
		if (x) velocityX_ = 0.0;
		if (y) velocityY_ = 0.0;
		for (Contact& contact : contacts_)
		{
			if (!contact.used) continue;
			contact.tracker.ClearAxis(x, y);
			if (x) contact.lifted.x = 0.0;
			if (y) contact.lifted.y = 0.0;
		}
		if (phase_ == CanvasPanPhase::Coasting &&
			std::hypot(velocityX_, velocityY_) < kCanvasPanStopSpeedDipPerSecond)
		{
			phase_ = CanvasPanPhase::Idle;
			velocityX_ = velocityY_ = 0.0;
		}
	}

	CanvasPanPhase CanvasPanDynamics::Phase() const noexcept { return phase_; }

	bool CanvasPanDynamics::Active() const noexcept { return phase_ != CanvasPanPhase::Idle; }

	CanvasVector CanvasPanDynamics::ContentVelocity() const noexcept
	{
		return ToCanvasVector(velocityX_, velocityY_);
	}

	size_t CanvasPanDynamics::HeldContactCount() const noexcept
	{
		size_t held = 0;
		for (const Contact& contact : contacts_)
			if (contact.used && contact.held) ++held;
		return held;
	}

	const CanvasPanVelocityTracker* CanvasPanDynamics::FindTracker(uint64_t key) const noexcept
	{
		for (const Contact& contact : contacts_)
			if (contact.used && contact.held && contact.key == key) return &contact.tracker;
		return nullptr;
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
