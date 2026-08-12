module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

module draw3.canvas_navigation;

namespace draw3
{
	namespace
	{
		float ClampFinite(float value, float minimum, float maximum) noexcept
		{
			if (!std::isfinite(value)) return 0.0f;
			return std::clamp(value, minimum, maximum);
		}

		double QpcSeconds(int64_t later, int64_t earlier, int64_t frequency) noexcept
		{
			if (frequency <= 0 || later <= earlier) return 0.0;
			return static_cast<double>(later - earlier) / static_cast<double>(frequency);
		}

		CanvasVector ClampVelocity(CanvasVector velocity) noexcept
		{
			if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) return {};
			const float speed = std::hypot(velocity.x, velocity.y);
			if (speed <= kCanvasPanMaximumSpeedDipPerSecond || speed <= 0.0f) return velocity;
			const float scale = kCanvasPanMaximumSpeedDipPerSecond / speed;
			return { velocity.x * scale, velocity.y * scale };
		}
	}

	CanvasVector ApplyCanvasContentTranslation(
		CanvasViewportState& viewport, CanvasVector contentDelta) noexcept
	{
		const float oldX = ClampFinite(viewport.x,
			-kCanvasViewportLimitDip, kCanvasViewportLimitDip);
		const float oldY = ClampFinite(viewport.y,
			-kCanvasViewportLimitDip, kCanvasViewportLimitDip);
		viewport.x = ClampFinite(oldX - contentDelta.x,
			-kCanvasViewportLimitDip, kCanvasViewportLimitDip);
		viewport.y = ClampFinite(oldY - contentDelta.y,
			-kCanvasViewportLimitDip, kCanvasViewportLimitDip);
		return { viewport.x - oldX, viewport.y - oldY };
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

	CanvasTouchDecision CanvasTouchGestureState::OnTouchDown(uint64_t contactKey,
		int64_t qpc, int64_t qpcFrequency, bool inertiaActive,
		bool blockingContactActive) noexcept
	{
		CanvasTouchDecision decision;
		if (contactKey == 0 || Disposition(contactKey) != CanvasTouchDisposition::Suppressed ||
			std::any_of(contacts_.begin(), contacts_.end(), [contactKey](const ContactState& item)
				{ return item.key == contactKey; }))
		{
			decision.disposition = CanvasTouchDisposition::Suppressed;
			return decision;
		}

		if (contacts_.empty())
		{
			firstDownQpc_ = qpc;
			batchStartedEligible_ = !blockingContactActive;
			batchAllowsPan_ = batchStartedEligible_;
			inertiaCandidate_ = inertiaActive && batchAllowsPan_;
			decision.disposition = inertiaCandidate_
				? CanvasTouchDisposition::PanCandidate : CanvasTouchDisposition::Draw;
			contacts_.push_back({ contactKey, decision.disposition });
			return decision;
		}

		if (panActive_)
		{
			decision.disposition = CanvasTouchDisposition::Pan;
			decision.joinedExistingPan = true;
			contacts_.push_back({ contactKey, decision.disposition });
			return decision;
		}

		const bool secondContact = contacts_.size() == 1;
		const double gapSeconds = QpcSeconds(qpc, firstDownQpc_, qpcFrequency);
		if (secondContact && batchStartedEligible_ &&
			gapSeconds <= kCanvasPanGestureWindowSeconds)
		{
			panActive_ = true;
			decision.disposition = CanvasTouchDisposition::Pan;
			decision.beginPan = true;
			decision.cancelExistingTouchDrawing = contacts_.front().disposition ==
				CanvasTouchDisposition::Draw;
			for (ContactState& item : contacts_)
				item.disposition = CanvasTouchDisposition::Pan;
			contacts_.push_back({ contactKey, CanvasTouchDisposition::Pan });
			inertiaCandidate_ = false;
			return decision;
		}

		batchAllowsPan_ = false;
		decision.disposition = CanvasTouchDisposition::Draw;
		contacts_.push_back({ contactKey, decision.disposition });
		return decision;
	}

	CanvasTouchDisposition CanvasTouchGestureState::OnTouchUp(uint64_t contactKey) noexcept
	{
		const auto iterator = std::find_if(contacts_.begin(), contacts_.end(),
			[contactKey](const ContactState& item) { return item.key == contactKey; });
		if (iterator == contacts_.end()) return CanvasTouchDisposition::Suppressed;
		const CanvasTouchDisposition disposition = iterator->disposition;
		contacts_.erase(iterator);
		if (contacts_.empty()) Reset();
		return disposition;
	}

	void CanvasTouchGestureState::Update(int64_t qpc, int64_t qpcFrequency) noexcept
	{
		if (contacts_.size() != 1 || panActive_ || !batchAllowsPan_) return;
		if (QpcSeconds(qpc, firstDownQpc_, qpcFrequency) <=
			kCanvasPanGestureWindowSeconds) return;
		batchAllowsPan_ = false;
		if (inertiaCandidate_)
		{
			// 惯性候选超时后首指本次不补画；迟到的其他指仍可独立绘制。
			contacts_.front().disposition = CanvasTouchDisposition::Suppressed;
			inertiaCandidate_ = false;
		}
	}

	void CanvasTouchGestureState::InterruptForPenOrMouse() noexcept
	{
		panActive_ = false;
		inertiaCandidate_ = false;
		batchStartedEligible_ = false;
		batchAllowsPan_ = false;
		for (ContactState& item : contacts_)
			if (item.disposition == CanvasTouchDisposition::Pan ||
				item.disposition == CanvasTouchDisposition::PanCandidate)
				item.disposition = CanvasTouchDisposition::Suppressed;
	}

	void CanvasTouchGestureState::Reset() noexcept
	{
		contacts_.clear();
		firstDownQpc_ = 0;
		batchStartedEligible_ = true;
		batchAllowsPan_ = true;
		panActive_ = false;
		inertiaCandidate_ = false;
	}

	bool CanvasTouchGestureState::PanActive() const noexcept { return panActive_; }
	bool CanvasTouchGestureState::InertiaCandidateActive() const noexcept
	{
		return inertiaCandidate_;
	}
	bool CanvasTouchGestureState::InertiaBrakeRequested() const noexcept
	{
		return !contacts_.empty() && !panActive_ && !inertiaCandidate_ &&
			!batchAllowsPan_ && contacts_.front().disposition ==
			CanvasTouchDisposition::Suppressed;
	}
	bool CanvasTouchGestureState::BatchAllowsPan() const noexcept { return batchAllowsPan_; }

	size_t CanvasTouchGestureState::GestureContactCount() const noexcept
	{
		return static_cast<size_t>(std::count_if(contacts_.begin(), contacts_.end(),
			[](const ContactState& item)
			{
				return item.disposition == CanvasTouchDisposition::Pan ||
					item.disposition == CanvasTouchDisposition::PanCandidate;
			}));
	}

	CanvasTouchDisposition CanvasTouchGestureState::Disposition(uint64_t contactKey) const noexcept
	{
		const auto iterator = std::find_if(contacts_.begin(), contacts_.end(),
			[contactKey](const ContactState& item) { return item.key == contactKey; });
		return iterator == contacts_.end()
			? CanvasTouchDisposition::Suppressed : iterator->disposition;
	}

	void BeginCanvasPan(CanvasPanMotionState& motion, bool inheritInertia) noexcept
	{
		motion.inheritedVelocity = inheritInertia ? motion.velocity : CanvasVector{};
		motion.inheritedBlendRemainingSeconds = inheritInertia
			? kCanvasPanMomentumBlendSeconds : 0.0;
		motion.inertiaActive = false;
		if (!inheritInertia) motion.velocity = {};
	}

	CanvasVector UpdateCanvasPan(CanvasPanMotionState& motion,
		CanvasVector contentDelta, double deltaSeconds) noexcept
	{
		if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return contentDelta;
		CanvasVector directVelocity = {
			static_cast<float>(contentDelta.x / deltaSeconds),
			static_cast<float>(contentDelta.y / deltaSeconds)
		};
		directVelocity = ClampVelocity(directVelocity);
		float inheritedWeight = 0.0f;
		if (motion.inheritedBlendRemainingSeconds > 0.0)
		{
			inheritedWeight = static_cast<float>(std::clamp(
				motion.inheritedBlendRemainingSeconds / kCanvasPanMomentumBlendSeconds,
				0.0, 1.0));
			motion.inheritedBlendRemainingSeconds = std::max(
				0.0, motion.inheritedBlendRemainingSeconds - deltaSeconds);
		}
		motion.velocity = ClampVelocity({
			directVelocity.x + motion.inheritedVelocity.x * inheritedWeight,
			directVelocity.y + motion.inheritedVelocity.y * inheritedWeight
		});
		return {
			contentDelta.x + static_cast<float>(motion.inheritedVelocity.x *
				inheritedWeight * deltaSeconds),
			contentDelta.y + static_cast<float>(motion.inheritedVelocity.y *
				inheritedWeight * deltaSeconds)
		};
	}

	void SetCanvasPanVelocity(CanvasPanMotionState& motion, CanvasVector velocity) noexcept
	{
		motion.velocity = ClampVelocity(velocity);
	}

	void EndCanvasPan(CanvasPanMotionState& motion,
		double secondsSinceLastInput) noexcept
	{
		if (!std::isfinite(secondsSinceLastInput) || secondsSinceLastInput < 0.0 ||
			secondsSinceLastInput > kCanvasPanReleaseVelocityHorizonSeconds)
			motion.velocity = {};
		motion.velocity = ClampVelocity(motion.velocity);
		motion.inheritedVelocity = {};
		motion.inheritedBlendRemainingSeconds = 0.0;
		motion.inertiaActive = CanvasPanSpeed(motion) >= 5.0f;
	}

	CanvasVector StepCanvasPanInertia(CanvasPanMotionState& motion,
		double deltaSeconds, bool penInRange) noexcept
	{
		if (!motion.inertiaActive || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
			return {};
		const double boundedSeconds = std::min(deltaSeconds, 0.05);
		const float speed = CanvasPanSpeed(motion);
		if (speed < 5.0f)
		{
			StopCanvasPan(motion);
			return {};
		}
		const float deceleration = penInRange
			? kCanvasPanPenBrakeDecelerationDipPerSecondSquared
			: kCanvasPanInertiaDecelerationDipPerSecondSquared;
		const float seconds = static_cast<float>(boundedSeconds);
		const float nextSpeed = (std::max)(0.0f, speed - deceleration * seconds);
		const float traveled = (speed + nextSpeed) * 0.5f * seconds;
		const CanvasVector delta = {
			motion.velocity.x / speed * traveled,
			motion.velocity.y / speed * traveled };
		if (nextSpeed < 5.0f) StopCanvasPan(motion);
		else
		{
			const float scale = nextSpeed / speed;
			motion.velocity.x *= scale;
			motion.velocity.y *= scale;
		}
		return delta;
	}

	void StopCanvasPan(CanvasPanMotionState& motion) noexcept { motion = {}; }

	void InterruptCanvasPanForDrawing(CanvasPanMotionState& motion,
		CanvasTouchGestureState& gesture) noexcept
	{
		StopCanvasPan(motion);
		gesture.InterruptForPenOrMouse();
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

	float CanvasPanSpeed(const CanvasPanMotionState& motion) noexcept
	{
		return std::hypot(motion.velocity.x, motion.velocity.y);
	}

	float CanvasPanFallbackBlurDip(float speedDipPerSecond) noexcept
	{
		if (!std::isfinite(speedDipPerSecond) ||
			speedDipPerSecond <= kCanvasPanSharpSpeedThresholdDipPerSecond) return 0.0f;
		const float ratio = std::clamp((speedDipPerSecond -
			kCanvasPanSharpSpeedThresholdDipPerSecond) / 6000.0f, 0.0f, 1.0f);
		return ratio * kCanvasPanMaximumFallbackBlurDip;
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
