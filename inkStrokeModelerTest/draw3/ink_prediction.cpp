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
#include <span>
#include <windows.h>

#pragma comment(lib, "ink_stroke_modeler_merge.lib")

module draw3.ink_prediction;

namespace draw3
{
	void BeginLaserContact(LaserTrailLifecycle& lifecycle) noexcept
	{
		if (lifecycle.phase == LaserTrailPhase::Inactive)
			lifecycle.minimumHoldDurationSeconds = 0.0;
		++lifecycle.activeContactCount;
		lifecycle.phase = LaserTrailPhase::Active;
	}

	void EndLaserContact(LaserTrailLifecycle& lifecycle, int64_t upQpc) noexcept
	{
		if (lifecycle.activeContactCount == 0) return;
		--lifecycle.activeContactCount;
		if (lifecycle.activeContactCount != 0) return;
		lifecycle.lastAllUpQpc = upQpc;
		lifecycle.phase = LaserTrailPhase::Hold;
	}

	void RequireLaserMinimumHold(
		LaserTrailLifecycle& lifecycle, double minimumSeconds) noexcept
	{
		if (!std::isfinite(minimumSeconds) || minimumSeconds <= 0.0) return;
		lifecycle.minimumHoldDurationSeconds =
			std::max(lifecycle.minimumHoldDurationSeconds, minimumSeconds);
	}

	double EffectiveLaserHoldDurationSeconds(
		const LaserTrailLifecycle& lifecycle, double configuredSeconds) noexcept
	{
		if (!std::isfinite(configuredSeconds) || configuredSeconds < 0.0)
			return configuredSeconds;
		const double minimumSeconds =
			std::isfinite(lifecycle.minimumHoldDurationSeconds)
			? std::max(lifecycle.minimumHoldDurationSeconds, 0.0) : 0.0;
		return std::max(configuredSeconds, minimumSeconds);
	}

	float EvaluateLaserTrailOpacity(LaserTrailLifecycle& lifecycle,
		int64_t nowQpc, int64_t qpcFrequency, double holdDurationSeconds) noexcept
	{
		if (lifecycle.phase == LaserTrailPhase::Inactive) return 0.0f;
		if (lifecycle.activeContactCount != 0 || lifecycle.phase == LaserTrailPhase::Active)
		{
			lifecycle.phase = LaserTrailPhase::Active;
			return 1.0f;
		}
		if (qpcFrequency <= 0 || lifecycle.lastAllUpQpc <= 0 ||
			!std::isfinite(holdDurationSeconds) || holdDurationSeconds < 0.0)
		{
			lifecycle = {};
			return 0.0f;
		}
		holdDurationSeconds = EffectiveLaserHoldDurationSeconds(
			lifecycle, holdDurationSeconds);

		const double elapsedSeconds = std::max(0.0,
			static_cast<double>(nowQpc - lifecycle.lastAllUpQpc) /
			static_cast<double>(qpcFrequency));
		if (elapsedSeconds < holdDurationSeconds)
		{
			lifecycle.phase = LaserTrailPhase::Hold;
			return 1.0f;
		}
		const double fadeProgress = (elapsedSeconds - holdDurationSeconds) /
			kLaserFadeDurationSeconds;
		if (fadeProgress >= 1.0)
		{
			lifecycle = {};
			return 0.0f;
		}
		lifecycle.phase = LaserTrailPhase::Fade;
		const float t = std::clamp(static_cast<float>(fadeProgress), 0.0f, 1.0f);
		const float smooth = t * t * (3.0f - 2.0f * t);
		return 1.0f - smooth;
	}

	bool ShouldBakeLaserBatch(
		const LaserTrailLifecycle& lifecycle, size_t pendingLayerCount) noexcept
	{
		return lifecycle.activeContactCount == 0 && pendingLayerCount > 0;
	}

	bool ShouldCompositeLaserLayer(bool cancelled, size_t pointCount) noexcept
	{
		return !cancelled && pointCount > 0;
	}

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
		constexpr float kHighlighterRadiusPx = 25.0f;
		constexpr float kHighlighterBoundsPaddingPx = 3.0f;
		constexpr uint32_t kMouseWidthModeShift = 0;
		constexpr uint32_t kTouchWidthModeShift = 1;
		constexpr uint32_t kPenWidthModeShift = 2;
		constexpr uint32_t kInputWidthModeMask = 0x1;
		constexpr uint32_t kPenInputWidthModeMask = 0x3;
		static_assert(kMaxRadiusChangePerPixel > 0.0f && kMaxRadiusChangePerPixel < 1.0f,
			"半径空间变化率必须严格小于胶囊切线退化阈值");
		static_assert(kCapsuleRadiusSlope > 0.0f && kCapsuleRadiusSlope < 1.0f,
			"笔锋公切线斜率必须严格小于胶囊切线退化阈值");
		static_assert(std::atomic<uint32_t>::is_always_lock_free,
			"设备宽度设置要求 32 位原子始终无锁");

		bool IsValid(InputWidthMode value) noexcept
		{
			return value == InputWidthMode::Fixed || value == InputWidthMode::SimulatedPressure;
		}

		bool IsValid(PenInputWidthMode value) noexcept
		{
			return value == PenInputWidthMode::Fixed || value == PenInputWidthMode::SimulatedPressure ||
				value == PenInputWidthMode::HardwarePressure;
		}

		bool IsValid(InputWidthModeSettings settings) noexcept
		{
			return IsValid(settings.mouse) && IsValid(settings.touch) && IsValid(settings.pen);
		}

		uint32_t EncodeInputWidthModeSettings(InputWidthModeSettings settings) noexcept
		{
			return (static_cast<uint32_t>(settings.mouse) << kMouseWidthModeShift) |
				(static_cast<uint32_t>(settings.touch) << kTouchWidthModeShift) |
				(static_cast<uint32_t>(settings.pen) << kPenWidthModeShift);
		}

		InputWidthModeSettings DecodeInputWidthModeSettings(uint32_t encoded) noexcept
		{
			return {
				static_cast<InputWidthMode>((encoded >> kMouseWidthModeShift) & kInputWidthModeMask),
				static_cast<InputWidthMode>((encoded >> kTouchWidthModeShift) & kInputWidthModeMask),
				static_cast<PenInputWidthMode>((encoded >> kPenWidthModeShift) & kPenInputWidthModeMask)
			};
		}

		float LerpFloat(float from, float to, float ratio)
		{
			return from + (to - from) * ratio;
		}

		float SmoothStep01(float value)
		{
			value = std::clamp(value, 0.0f, 1.0f);
			return value * value * (3.0f - 2.0f * value); // 比线性插值更平滑，避免笔宽突变。
		}

		void AppendDistinctPoint(std::vector<InkPoint>& points, const InkPoint& point)
		{
			if (!points.empty())
			{
				const float deltaX = point.x - points.back().x;
				const float deltaY = point.y - points.back().y;
				if (deltaX * deltaX + deltaY * deltaY <=
					kHighlighterDuplicateDistancePx * kHighlighterDuplicateDistancePx) return;
			}
			points.push_back(point);
		}

		std::vector<InkPoint> DeduplicateHighlighterPoints(const std::vector<InkPoint>& points)
		{
			std::vector<InkPoint> deduplicated;
			deduplicated.reserve(points.size());
			for (const InkPoint& point : points) AppendDistinctPoint(deduplicated, point);
			return deduplicated;
		}

		void IncludeHighlighterBounds(RECT& bounds, float minX, float minY, float maxX, float maxY)
		{
			const RECT addition = {
				static_cast<LONG>(std::floor(minX - kHighlighterBoundsPaddingPx)),
				static_cast<LONG>(std::floor(minY - kHighlighterBoundsPaddingPx)),
				static_cast<LONG>(std::ceil(maxX + kHighlighterBoundsPaddingPx)),
				static_cast<LONG>(std::ceil(maxY + kHighlighterBoundsPaddingPx))
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

		void IncludeHighlighterSweepBounds(RECT& bounds, const InkPoint& p1, const InkPoint& p2)
		{
			const float halfWidth = kHighlighterRadiusPx / kHighlighterNibAspectRatio;
			IncludeHighlighterBounds(bounds,
				std::min(p1.x, p2.x) - halfWidth,
				std::min(p1.y, p2.y) - kHighlighterRadiusPx,
				std::max(p1.x, p2.x) + halfWidth,
				std::max(p1.y, p2.y) + kHighlighterRadiusPx);
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

	LaserIncrementalRanges PlanLaserIncrementalRanges(
		std::span<const InkPoint> realPoints,
		const LaserIncrementalStrokeState& state,
		double protectedDurationSeconds) noexcept
	{
		LaserIncrementalRanges ranges;
		if (realPoints.empty()) return ranges;
		const size_t protectedIndex = FindProtectedStartIndex(
			realPoints, protectedDurationSeconds);
		const size_t committedIndex = std::min(
			state.rebuildRequired ? size_t{ 0 } : state.stableCommittedIndex,
			realPoints.size() - 1);
		const size_t stableEnd = std::max(committedIndex, protectedIndex);
		if (stableEnd > committedIndex)
		{
			ranges.stableFirstIndex = committedIndex;
			ranges.stablePointCount = stableEnd - committedIndex + 1;
		}
		ranges.nextStableCommittedIndex = stableEnd;
		ranges.liveFirstIndex = stableEnd;
		ranges.livePointCount = realPoints.size() - stableEnd;
		return ranges;
	}

	LaserCoverageMode SelectLaserCoverageMode(
		LaserCoverageMode current, size_t layerCount, bool resourcesAvailable) noexcept
	{
		if (current == LaserCoverageMode::FullRedraw) return current;
		if (layerCount == 0) return LaserCoverageMode::Inactive;
		if (!resourcesAvailable || layerCount > 1) return LaserCoverageMode::FullRedraw;
		if (current == LaserCoverageMode::Inactive) return LaserCoverageMode::Incremental;
		return current;
	}

	InputWidthModeSettingsState::InputWidthModeSettingsState(InputWidthModeSettings settings) noexcept
		: encoded_(EncodeInputWidthModeSettings(IsValid(settings) ? settings : InputWidthModeSettings{}))
	{
	}

	bool InputWidthModeSettingsState::Set(InputWidthModeSettings settings) noexcept
	{
		if (!IsValid(settings)) return false;
		encoded_.store(EncodeInputWidthModeSettings(settings), std::memory_order_release);
		return true;
	}

	InputWidthModeSettings InputWidthModeSettingsState::Get() const noexcept
	{
		return DecodeInputWidthModeSettings(encoded_.load(std::memory_order_acquire));
	}

	StrokeModelConfiguration CreateStrokeModelConfiguration(int dpiX)
	{
		const StrokeTimingProfile timingProfile = GetStrokeTimingProfile(kActiveStrokeTimingProfileId);
		const float dpiScale = std::max(static_cast<float>(dpiX) / 96.0f, 0.1f);
		const float expectedSpeed = 500.0f * dpiScale; // DPI 越高，像素速度按比例放大。
		ink::stroke_model::KalmanPredictorParams kalmanParams;
		kalmanParams.process_noise = 0.05;
		kalmanParams.measurement_noise = 0.01;
		kalmanParams.min_stable_iteration = 4;
		kalmanParams.max_time_samples = timingProfile.kalman_max_time_samples;
		kalmanParams.min_catchup_velocity = expectedSpeed / 1000.0f; // 保证预测点追上真实点时不会过慢。
		kalmanParams.acceleration_weight = 0.5f;
		kalmanParams.jerk_weight = 0.1f;
		kalmanParams.prediction_interval = ink::stroke_model::Duration(timingProfile.prediction_interval_seconds);
		kalmanParams.confidence_params = {
			.desired_number_of_samples = timingProfile.kalman_desired_number_of_samples,
			.max_estimation_distance = 1.5f * static_cast<float>(kalmanParams.measurement_noise),
			.min_travel_speed = 0.05f * expectedSpeed,
			.max_travel_speed = 0.25f * expectedSpeed,
			.max_linear_deviation = 10.0f * static_cast<float>(kalmanParams.measurement_noise),
			.baseline_linearity_confidence = 0.4f
		};

		ink::stroke_model::StrokeModelParams modelParams{
			.wobble_smoother_params{
				.is_enabled = true,
				.timeout = ink::stroke_model::Duration(timingProfile.wobble_timeout_seconds),
				.speed_floor = timingProfile.wobble_speed_floor_ratio * expectedSpeed, // 低速时启用更强防抖。
				.speed_ceiling = timingProfile.wobble_speed_ceiling_ratio * expectedSpeed
			},
			.position_modeler_params{ .spring_mass_constant = 11.f / 32400, .drag_constant = 72.f },
			.sampling_params{
				.min_output_rate = timingProfile.min_output_rate,
				.end_of_stroke_stopping_distance = .001f,
				.end_of_stroke_max_iterations = 20,
				.max_outputs_per_call = timingProfile.max_outputs_per_call
			}
		};
		modelParams.stylus_state_modeler_params.use_stroke_normal_projection = true;
		return { timingProfile, expectedSpeed, dpiScale,
			GetLiveTipDurationSeconds(timingProfile), kalmanParams, modelParams };
	}

	void ApplyPredictionMode(ink::stroke_model::StrokeModelParams& params,
		const ink::stroke_model::KalmanPredictorParams& kalmanPredictorParams)
	{
		switch (kActivePredictionMode)
		{
		case InkPredictionMode::Disabled:
			params.prediction_params = ink::stroke_model::DisabledPredictorParams{}; // 完全不输出预测点，便于对比延迟。
			break;
		case InkPredictionMode::StrokeEnd:
			params.prediction_params = ink::stroke_model::StrokeEndPredictorParams{}; // 只使用库内置的笔尾预测。
			break;
		default:
			params.prediction_params = kalmanPredictorParams; // 默认使用调过参的 Kalman 预测。
			break;
		}
	}

double ResolveLiveTipTaperDurationSeconds(StrokeWidthMode widthMode,
		double configuredLiveTipDurationSeconds) noexcept
	{
		// 硬件压感本身形成粗细变化，普通笔 L0 不再叠加实时笔锋。
		if (widthMode == StrokeWidthMode::HardwarePressure) return 0.0;
		return std::max(0.0, configuredLiveTipDurationSeconds);
	}

	StrokeWidthMode ResolveStrokeWidthMode(InputDeviceType deviceType,
		InputWidthModeSettings settings, float downPressure) noexcept
	{
		const auto resolveBasicMode = [](InputWidthMode mode)
		{
				return mode == InputWidthMode::Fixed
					? StrokeWidthMode::Fixed : StrokeWidthMode::SimulatedPressure;
		};
		switch (deviceType)
		{
		case InputDeviceType::Touch:
			return resolveBasicMode(IsValid(settings.touch)
				? settings.touch : InputWidthMode::SimulatedPressure);
		case InputDeviceType::MouseLeft:
		case InputDeviceType::MouseRight:
			return resolveBasicMode(IsValid(settings.mouse)
				? settings.mouse : InputWidthMode::SimulatedPressure);
		case InputDeviceType::Pen:
		default:
			break;
		}

		switch (IsValid(settings.pen) ? settings.pen : PenInputWidthMode::HardwarePressure)
		{
		case PenInputWidthMode::Fixed:
			return StrokeWidthMode::Fixed;
		case PenInputWidthMode::SimulatedPressure:
			return StrokeWidthMode::SimulatedPressure;
		case PenInputWidthMode::HardwarePressure:
		default:
			return std::isfinite(downPressure) && downPressure >= 0.0f && downPressure <= 1.0f
				? StrokeWidthMode::HardwarePressure : StrokeWidthMode::SimulatedPressure;
		}
	}

	bool ShouldUseInvertedPenEraser(InputDeviceType deviceType,
		bool isInvertedCursor, bool enabled, bool selectedToolSupportsOverride) noexcept
	{
		return enabled && selectedToolSupportsOverride &&
			deviceType == InputDeviceType::Pen && isInvertedCursor;
	}

	float ResolveStylusPressureForModel(InputDeviceType deviceType,
		bool isInvertedCursor, float pressure) noexcept
	{
		return deviceType == InputDeviceType::Pen && isInvertedCursor ? -1.0f : pressure;
	}

	float HardwarePressureDiameter(float baseDiameter, float pressure) noexcept
	{
		if (!std::isfinite(baseDiameter) || baseDiameter <= 0.0f) return 0.0f;
		if (!std::isfinite(pressure) || pressure < 0.0f) return baseDiameter;
		return baseDiameter * (0.2f + 1.2f * std::clamp(pressure, 0.0f, 1.0f));
	}

	float LaserPressureDiameter(float baseDiameter, float pressure) noexcept
	{
		if (!std::isfinite(baseDiameter) || baseDiameter <= 0.0f) return 0.0f;
		if (!std::isfinite(pressure) || pressure < 0.0f) return baseDiameter;
		return baseDiameter * (0.65f + 0.75f * std::clamp(pressure, 0.0f, 1.0f));
	}

	bool TryGetInterruptedStrokeTailDirection(const std::vector<InkPoint>& realPoints,
		float dpiScale, DirectX::XMFLOAT2& direction) noexcept
	{
		direction = {};
		if (realPoints.size() < 2) return false;
		const float safeScale = std::max(dpiScale, 0.1f);
		const float lookbackDistance =
			kInterruptedStrokeReconnectDirectionLookbackPx * safeScale;
		const float minimumDistance =
			kInterruptedStrokeReconnectMinimumDirectionPx * safeScale;
		const InkPoint& end = realPoints.back();
		float startX = end.x;
		float startY = end.y;
		float remaining = lookbackDistance;
		for (size_t index = realPoints.size() - 1; index > 0 && remaining > 0.0f; --index)
		{
			const InkPoint& current = realPoints[index];
			const InkPoint& previous = realPoints[index - 1];
			const float deltaX = current.x - previous.x;
			const float deltaY = current.y - previous.y;
			const float segmentLength = std::sqrt(deltaX * deltaX + deltaY * deltaY);
			if (segmentLength <= 0.0001f) continue;
			if (segmentLength >= remaining)
			{
				const float ratio = remaining / segmentLength;
				startX = current.x - deltaX * ratio;
				startY = current.y - deltaY * ratio;
				remaining = 0.0f;
			}
			else
			{
				startX = previous.x;
				startY = previous.y;
				remaining -= segmentLength;
			}
		}
		const float chordX = end.x - startX;
		const float chordY = end.y - startY;
		const float chordLength = std::sqrt(chordX * chordX + chordY * chordY);
		if (chordLength < minimumDistance) return false;
		direction = { chordX / chordLength, chordY / chordLength };
		return true;
	}

	InterruptedStrokeReconnectMotion ResolveInterruptedStrokeReconnectMotion(
		const std::vector<ink::stroke_model::Result>& predictedResults,
		const std::vector<InkPoint>& realPoints,
		DirectX::XMFLOAT2 fallbackDirection, float fallbackSpeed,
		double upInputTime, double gapSeconds, float dpiScale) noexcept
	{
		InterruptedStrokeReconnectMotion motion;
		if (std::isfinite(fallbackSpeed) && fallbackSpeed > 0.0f)
			motion.recentInputSpeed = fallbackSpeed;
		const auto useFallback = [&]()
			{
				const float directionLength = std::hypot(
					fallbackDirection.x, fallbackDirection.y);
				float resolvedSpeed = fallbackSpeed;
				if (realPoints.size() >= 2)
				{
					const float lookbackDistance =
						kInterruptedStrokeReconnectDirectionLookbackPx * std::max(dpiScale, 0.1f);
					const float minimumDistance =
						kInterruptedStrokeReconnectMinimumDirectionPx * std::max(dpiScale, 0.1f);
					float accumulatedDistance = 0.0f;
					double startTime = realPoints.back().time;
					for (size_t index = realPoints.size() - 1; index > 0; --index)
					{
						const InkPoint& current = realPoints[index];
						const InkPoint& previous = realPoints[index - 1];
						const float segmentLength = std::hypot(
							current.x - previous.x, current.y - previous.y);
						accumulatedDistance += segmentLength;
						startTime = previous.time;
						if (accumulatedDistance >= lookbackDistance) break;
					}
					const double duration = static_cast<double>(realPoints.back().time) - startTime;
					if (accumulatedDistance >= minimumDistance && duration > 0.000001)
					{
						const float modeledTailSpeed =
							accumulatedDistance / static_cast<float>(duration);
						if (std::isfinite(modeledTailSpeed) && modeledTailSpeed > 0.0f)
							resolvedSpeed = modeledTailSpeed; // 模型时间窗比 RTS 尾包瞬时速度更抗异常尖峰。
					}
				}
				if ((!std::isfinite(resolvedSpeed) || resolvedSpeed <= 0.0f) &&
					std::isfinite(fallbackSpeed) && fallbackSpeed > 0.0f)
					resolvedSpeed = fallbackSpeed;
				if (gapSeconds > 0.0 && std::isfinite(resolvedSpeed))
				{
					const float maximumUsefulSpeed =
						kInterruptedStrokeReconnectFallbackMaximumDistancePx * std::max(dpiScale, 0.1f) /
						(kInterruptedStrokeReconnectMinimumSpeedRatio * static_cast<float>(gapSeconds));
					resolvedSpeed = std::min(resolvedSpeed, maximumUsefulSpeed);
				}
				if (!std::isfinite(directionLength) || directionLength <= 0.0001f ||
					!std::isfinite(resolvedSpeed) || resolvedSpeed <= 0.0f) return;
				motion.valid = true;
				motion.source = InterruptedStrokeReconnectMotionSource::RealTail;
				motion.directionReliable = true;
				motion.direction = {
					fallbackDirection.x / directionLength,
					fallbackDirection.y / directionLength
				};
				motion.speed = resolvedSpeed;
			};

		if (predictedResults.empty() || !std::isfinite(upInputTime) ||
			!std::isfinite(gapSeconds) || gapSeconds < 0.0)
		{
			useFallback();
			return motion;
		}

		const double lastPredictionTime = predictedResults.back().time.Value();
		motion.predictionHorizonMilliseconds = std::max(
			0.0, (lastPredictionTime - upInputTime) * 1000.0);
		const double targetTime = upInputTime + gapSeconds;
		motion.beyondPredictionHorizonMilliseconds = std::max(
			0.0, (targetTime - lastPredictionTime) * 1000.0);
		ink::stroke_model::Vec2 selectedPosition = predictedResults.front().position;
		ink::stroke_model::Vec2 selectedVelocity = predictedResults.front().velocity;
		double selectedTime = predictedResults.front().time.Value();
		for (size_t index = 1; index < predictedResults.size(); ++index)
		{
			const auto& previous = predictedResults[index - 1];
			const auto& current = predictedResults[index];
			const double previousTime = previous.time.Value();
			const double currentTime = current.time.Value();
			if (targetTime > currentTime) continue;
			const double duration = currentTime - previousTime;
			const float ratio = duration > 0.0
				? std::clamp(static_cast<float>((targetTime - previousTime) / duration), 0.0f, 1.0f)
				: 1.0f;
			selectedPosition = {
				previous.position.x + (current.position.x - previous.position.x) * ratio,
				previous.position.y + (current.position.y - previous.position.y) * ratio
			};
			selectedVelocity = {
				previous.velocity.x + (current.velocity.x - previous.velocity.x) * ratio,
				previous.velocity.y + (current.velocity.y - previous.velocity.y) * ratio
			};
			selectedTime = previousTime + duration * ratio;
			break;
		}
		if (targetTime > lastPredictionTime)
		{
			selectedPosition = predictedResults.back().position;
			selectedVelocity = predictedResults.back().velocity;
			selectedTime = lastPredictionTime;
		}

		const double forecastDuration = selectedTime - upInputTime;
		if (!realPoints.empty() && forecastDuration > 0.0 &&
			std::isfinite(selectedPosition.x) && std::isfinite(selectedPosition.y))
		{
			const float deltaX = selectedPosition.x - realPoints.back().x;
			const float deltaY = selectedPosition.y - realPoints.back().y;
			const float predictedDistance = std::hypot(deltaX, deltaY);
			if (std::isfinite(predictedDistance))
			{
				motion.valid = true;
				motion.source = InterruptedStrokeReconnectMotionSource::PredictionPosition;
				motion.predictedDisplacement = { deltaX, deltaY };
				motion.predictedDistance = predictedDistance;
				motion.forecastDurationMilliseconds = forecastDuration * 1000.0;
				motion.speed = predictedDistance / static_cast<float>(forecastDuration);
				motion.directionReliable = predictedDistance >=
					kInterruptedStrokeReconnectMinimumDirectionPx * std::max(dpiScale, 0.1f);
				if (predictedDistance > 0.0001f)
					motion.direction = { deltaX / predictedDistance, deltaY / predictedDistance };

				const float terminalSpeed = std::hypot(selectedVelocity.x, selectedVelocity.y);
				if (std::isfinite(terminalSpeed) && terminalSpeed > 0.0001f)
				{
					motion.terminalDirectionValid = true;
					motion.terminalSpeed = terminalSpeed;
					motion.terminalDirection = {
						selectedVelocity.x / terminalSpeed,
						selectedVelocity.y / terminalSpeed
					};
				}
				return motion;
			}
		}

		useFallback();
		return motion;
	}

	InterruptedStrokeReconnectResult EvaluateInterruptedStrokeReconnect(
		const InterruptedStrokeReconnectInput& input) noexcept
	{
		InterruptedStrokeReconnectResult result;
		result.motionSource = input.motion.source;
		result.referenceSpeed = input.motion.speed;
		result.recentInputSpeed = input.motion.recentInputSpeed;
		result.terminalSpeed = input.motion.terminalSpeed;
		result.forecastDurationMilliseconds = input.motion.forecastDurationMilliseconds;
		result.predictionHorizonMilliseconds = input.motion.predictionHorizonMilliseconds;
		result.beyondPredictionHorizonMilliseconds =
			input.motion.beyondPredictionHorizonMilliseconds;
		result.directionReliable = input.motion.directionReliable;
		if (input.qpcFrequency <= 0 || input.newDownQpc <= input.previousUpQpc)
		{
			result.rejectReason = InterruptedStrokeReconnectRejectReason::InvalidTime;
			return result;
		}

		const double gapSeconds = static_cast<double>(input.newDownQpc - input.previousUpQpc) /
			static_cast<double>(input.qpcFrequency);
		result.gapMilliseconds = gapSeconds * 1000.0;
		if (gapSeconds > kInterruptedStrokeReconnectWindowSeconds)
		{
			result.rejectReason = InterruptedStrokeReconnectRejectReason::WindowExpired;
			return result;
		}

		const float safeScale = std::max(input.dpiScale, 0.1f);
		const float deltaX = input.newPosition.x - input.previousPosition.x;
		const float deltaY = input.newPosition.y - input.previousPosition.y;
		result.distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
		result.bridgeSpeed = result.distance / static_cast<float>(gapSeconds);
		const float comparisonEpsilon =
			kInterruptedStrokeReconnectComparisonEpsilonPx * safeScale;
		if (!input.motion.valid)
		{
			result.rejectReason = InterruptedStrokeReconnectRejectReason::InvalidDirection;
			return result;
		}

		if (input.motion.source == InterruptedStrokeReconnectMotionSource::PredictionPosition)
		{
			result.expectedDistance = input.motion.predictedDistance;
			result.minimumDistance = 0.0f;
			const float predictionSpeed = std::isfinite(input.motion.speed) && input.motion.speed > 0.0f
				? input.motion.speed : 0.0f;
			const float recentInputSpeed = std::isfinite(input.motion.recentInputSpeed) &&
				input.motion.recentInputSpeed > 0.0f ? input.motion.recentInputSpeed : 0.0f;
			result.referenceSpeed = std::max(predictionSpeed, recentInputSpeed);
			const float adaptiveDistanceLimit = result.referenceSpeed > 0.0f
				? result.referenceSpeed * static_cast<float>(gapSeconds) *
					kInterruptedStrokeReconnectAdaptiveDistanceSpeedRatio +
					kInterruptedStrokeReconnectDistanceSlackPx * safeScale
				: 0.0f;
			result.maximumDistance = std::min(
				kInterruptedStrokeReconnectAdaptiveMaximumDistancePx * safeScale,
				std::max(kInterruptedStrokeReconnectPredictedMaximumDistancePx * safeScale,
					adaptiveDistanceLimit));

			const float expectedX = input.previousPosition.x +
				input.motion.predictedDisplacement.x;
			const float expectedY = input.previousPosition.y +
				input.motion.predictedDisplacement.y;
			result.endpointError = std::hypot(
				input.newPosition.x - expectedX, input.newPosition.y - expectedY);
			const float beyondHorizonSeconds = static_cast<float>(
				std::max(0.0, input.motion.beyondPredictionHorizonMilliseconds) / 1000.0);
			const float horizonUncertainty = std::max(0.0f, input.motion.speed) *
				beyondHorizonSeconds * kInterruptedStrokeReconnectBeyondHorizonUncertaintyRatio;
			result.maximumEndpointError = std::min(result.maximumDistance,
				kInterruptedStrokeReconnectDistanceSlackPx * safeScale +
				input.motion.predictedDistance *
				kInterruptedStrokeReconnectEndpointRelativeTolerance + horizonUncertainty);
			result.speedRatio = result.referenceSpeed > 0.0001f
				? result.bridgeSpeed / result.referenceSpeed : 0.0f;

			if (input.motion.directionReliable && result.distance > 0.0001f)
			{
				const float directionLength = std::hypot(
					input.motion.direction.x, input.motion.direction.y);
				if (std::isfinite(directionLength) && directionLength > 0.0001f)
				{
					const float dot = std::clamp(
						(deltaX * input.motion.direction.x + deltaY * input.motion.direction.y) /
						(result.distance * directionLength), -1.0f, 1.0f);
					result.angleDegrees = std::acos(dot) * 180.0f / 3.14159265358979323846f;
				}
			}
			else
			{
				result.angleDegrees = 0.0f; // 预测弦过短时角度不稳定，只按落点误差判断。
			}
			result.selectedDirectionAngleDegrees = result.angleDegrees;
			if (input.motion.terminalDirectionValid && result.distance > 0.0001f)
			{
				const float terminalDot = std::clamp(
					(deltaX * input.motion.terminalDirection.x +
						deltaY * input.motion.terminalDirection.y) / result.distance,
					-1.0f, 1.0f);
				result.terminalVelocityAngleDegrees =
					std::acos(terminalDot) * 180.0f / 3.14159265358979323846f;
			}
			result.matchScore = result.maximumEndpointError > 0.0001f
				? result.endpointError / result.maximumEndpointError
				: (result.endpointError <= comparisonEpsilon ? 0.0f :
					(std::numeric_limits<float>::infinity)());
			const bool withinDistanceLimit =
				result.distance <= result.maximumDistance + comparisonEpsilon;
			if (result.endpointError <= result.maximumEndpointError + comparisonEpsilon &&
				withinDistanceLimit)
			{
				result.matched = std::isfinite(result.matchScore);
				result.rejectReason = result.matched
					? InterruptedStrokeReconnectRejectReason::None
					: InterruptedStrokeReconnectRejectReason::InvalidDirection;
				return result;
			}

			const double forecastSeconds =
				input.motion.forecastDurationMilliseconds / 1000.0;
			const bool canExtrapolateCommon =
				std::isfinite(result.referenceSpeed) && result.referenceSpeed > 0.0f &&
				forecastSeconds > 0.0 && gapSeconds > forecastSeconds &&
				input.motion.beyondPredictionHorizonMilliseconds > 0.0 &&
				result.speedRatio >= kInterruptedStrokeReconnectMinimumSpeedRatio &&
				result.speedRatio <= kInterruptedStrokeReconnectMaximumSpeedRatio;
			const bool canUseInHorizonTerminalCorridor =
				std::isfinite(result.referenceSpeed) && result.referenceSpeed > 0.0f &&
				forecastSeconds > 0.0 &&
				input.motion.beyondPredictionHorizonMilliseconds <= 0.0 &&
				gapSeconds <= kInterruptedStrokeReconnectInHorizonTerminalMaximumGapSeconds &&
				result.speedRatio >= kInterruptedStrokeReconnectInHorizonTerminalMinimumSpeedRatio &&
				result.speedRatio <= kInterruptedStrokeReconnectInHorizonTerminalMaximumSpeedRatio;
			struct DirectionCorridor
			{
				bool evaluated = false;
				bool matched = false;
				float angleDegrees = 180.0f;
				float longitudinalDistance = 0.0f;
				float longitudinalError = 0.0f;
				float maximumLongitudinalError = 0.0f;
				float lateralError = 0.0f;
				float maximumLateralError = 0.0f;
				float expectedDistance = 0.0f;
				float endpointError = 0.0f;
				float maximumEndpointError = 0.0f;
				float matchScore = (std::numeric_limits<float>::infinity)();
			};
			const auto evaluateDirectionCorridor = [&](DirectX::XMFLOAT2 direction,
				float angleDegrees, bool directionValid, bool policyEnabled,
				float maximumAngleDegrees)
				{
					DirectionCorridor corridor;
					const float directionLength = std::hypot(direction.x, direction.y);
					if (!policyEnabled || !directionValid || !std::isfinite(directionLength) ||
						directionLength <= 0.0001f || angleDegrees > maximumAngleDegrees)
						return corridor;

					corridor.evaluated = true;
					corridor.angleDegrees = angleDegrees;
					corridor.expectedDistance = std::max(input.motion.predictedDistance,
						result.referenceSpeed * static_cast<float>(gapSeconds));
					const float directionX = direction.x / directionLength;
					const float directionY = direction.y / directionLength;
					corridor.longitudinalDistance = deltaX * directionX + deltaY * directionY;
					corridor.lateralError = std::abs(deltaX * directionY - deltaY * directionX);
					corridor.longitudinalError = std::abs(
						corridor.longitudinalDistance - corridor.expectedDistance);
					corridor.maximumLongitudinalError =
						kInterruptedStrokeReconnectDistanceSlackPx * safeScale +
						corridor.expectedDistance * kInterruptedStrokeReconnectAdaptiveRelativeTolerance;
					corridor.maximumLateralError = corridor.maximumLongitudinalError;
					const float longitudinalScore = corridor.maximumLongitudinalError > 0.0001f
						? corridor.longitudinalError / corridor.maximumLongitudinalError :
						(std::numeric_limits<float>::infinity)();
					const float lateralScore = corridor.maximumLateralError > 0.0001f
						? corridor.lateralError / corridor.maximumLateralError :
						(std::numeric_limits<float>::infinity)();
					corridor.endpointError = std::hypot(
						corridor.longitudinalError, corridor.lateralError);
					corridor.maximumEndpointError = std::hypot(
						corridor.maximumLongitudinalError, corridor.maximumLateralError);
					corridor.matchScore = std::max(longitudinalScore, lateralScore);
					corridor.matched = corridor.longitudinalDistance > 0.0f && withinDistanceLimit &&
						corridor.longitudinalError <=
							corridor.maximumLongitudinalError + comparisonEpsilon &&
						corridor.lateralError <=
							corridor.maximumLateralError + comparisonEpsilon;
					return corridor;
				};
			const auto applyDirectionCorridor = [&](const DirectionCorridor& corridor,
				bool usedTerminalDirection)
				{
					result.longitudinalDistance = corridor.longitudinalDistance;
					result.longitudinalError = corridor.longitudinalError;
					result.maximumLongitudinalError = corridor.maximumLongitudinalError;
					result.lateralError = corridor.lateralError;
					result.maximumLateralError = corridor.maximumLateralError;
					result.expectedDistance = corridor.expectedDistance;
					result.endpointError = corridor.endpointError;
					result.maximumEndpointError = corridor.maximumEndpointError;
					result.matchScore = corridor.matchScore;
					result.selectedDirectionAngleDegrees = corridor.angleDegrees;
					result.selectedTerminalDirectionCorridor = usedTerminalDirection;
				};

			// 先保持原预测弦走廊；曲线导致短弦滞后时，再尝试模型预测末端速度方向。
			const DirectionCorridor chordCorridor = evaluateDirectionCorridor(
				input.motion.direction, result.angleDegrees, input.motion.directionReliable,
				canExtrapolateCommon,
				kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees);
			if (chordCorridor.matched)
			{
				applyDirectionCorridor(chordCorridor, false);
				result.predictionExtrapolated = true;
				result.matched = std::isfinite(result.matchScore);
				result.rejectReason = result.matched
					? InterruptedStrokeReconnectRejectReason::None
					: InterruptedStrokeReconnectRejectReason::InvalidDirection;
				return result;
			}

			const DirectionCorridor terminalCorridor = evaluateDirectionCorridor(
				input.motion.terminalDirection, result.terminalVelocityAngleDegrees,
				input.motion.terminalDirectionValid, canExtrapolateCommon,
				kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees);
			if (terminalCorridor.matched)
			{
				applyDirectionCorridor(terminalCorridor, true);
				result.predictionExtrapolated = true;
				result.matched = std::isfinite(result.matchScore);
				result.rejectReason = result.matched
					? InterruptedStrokeReconnectRejectReason::None
					: InterruptedStrokeReconnectRejectReason::InvalidDirection;
				return result;
			}

			// 预测仍覆盖新 Down 时只允许更短间隔、更小角度和更窄速度比的终速走廊，
			// 修复急弯中预测位移严重偏短，而不放宽正常的预测落点容差。
			const DirectionCorridor inHorizonTerminalCorridor = evaluateDirectionCorridor(
				input.motion.terminalDirection, result.terminalVelocityAngleDegrees,
				input.motion.terminalDirectionValid, canUseInHorizonTerminalCorridor,
				kInterruptedStrokeReconnectInHorizonTerminalMaximumAngleDegrees);
			if (inHorizonTerminalCorridor.matched)
			{
				applyDirectionCorridor(inHorizonTerminalCorridor, true);
				result.selectedInHorizonTerminalDirectionCorridor = true;
				result.matched = std::isfinite(result.matchScore);
				result.rejectReason = result.matched
					? InterruptedStrokeReconnectRejectReason::None
					: InterruptedStrokeReconnectRejectReason::InvalidDirection;
				return result;
			}

			if (inHorizonTerminalCorridor.evaluated)
			{
				applyDirectionCorridor(inHorizonTerminalCorridor, true);
				result.selectedInHorizonTerminalDirectionCorridor = true;
			}
			else if (chordCorridor.evaluated || terminalCorridor.evaluated)
			{
				const bool useTerminalDiagnostics = terminalCorridor.evaluated &&
					(!chordCorridor.evaluated || terminalCorridor.matchScore < chordCorridor.matchScore);
				applyDirectionCorridor(useTerminalDiagnostics ? terminalCorridor : chordCorridor,
					useTerminalDiagnostics);
			}
			result.rejectReason = withinDistanceLimit
				? InterruptedStrokeReconnectRejectReason::ForecastError
				: InterruptedStrokeReconnectRejectReason::Distance;
			return result;
		}

		if (!std::isfinite(input.motion.speed) || input.motion.speed <= 0.0f)
		{
			result.rejectReason = InterruptedStrokeReconnectRejectReason::InvalidSpeed;
			return result;
		}
		result.expectedDistance = input.motion.speed * static_cast<float>(gapSeconds);
		result.speedRatio = result.bridgeSpeed / input.motion.speed;
		result.matchScore = std::isfinite(result.speedRatio) && result.speedRatio > 0.0f
			? std::abs(std::log(result.speedRatio))
			: (std::numeric_limits<float>::infinity)();
		result.minimumDistance =
			result.expectedDistance * kInterruptedStrokeReconnectMinimumSpeedRatio;
		result.maximumDistance = std::min(
			kInterruptedStrokeReconnectFallbackMaximumDistancePx * safeScale,
			std::max(6.0f * safeScale,
				result.expectedDistance * 1.75f +
				kInterruptedStrokeReconnectDistanceSlackPx * safeScale));
		if (result.distance <= 0.0001f ||
			result.distance + comparisonEpsilon < result.minimumDistance ||
			result.distance > result.maximumDistance + comparisonEpsilon)
		{
			result.rejectReason = InterruptedStrokeReconnectRejectReason::Distance;
			return result;
		}

		const float directionLength = std::sqrt(
			input.motion.direction.x * input.motion.direction.x +
			input.motion.direction.y * input.motion.direction.y);
		if (!std::isfinite(directionLength) || directionLength <= 0.0001f)
		{
			result.rejectReason = InterruptedStrokeReconnectRejectReason::InvalidDirection;
			return result;
		}
		const float dot = std::clamp(
			(deltaX * input.motion.direction.x + deltaY * input.motion.direction.y) /
			(result.distance * directionLength), -1.0f, 1.0f);
		result.angleDegrees = std::acos(dot) * 180.0f / 3.14159265358979323846f;
		result.selectedDirectionAngleDegrees = result.angleDegrees;
		if (result.angleDegrees > kInterruptedStrokeReconnectMaximumAngleDegrees)
		{
			result.rejectReason = InterruptedStrokeReconnectRejectReason::Angle;
			return result;
		}

		result.matched = std::isfinite(result.matchScore);
		result.rejectReason = result.matched
			? InterruptedStrokeReconnectRejectReason::None
			: InterruptedStrokeReconnectRejectReason::InvalidSpeed;
		return result;
	}

	bool AreInterruptedStrokeReconnectIdentitiesCompatible(
		const InterruptedStrokeReconnectIdentity& previous,
		const InterruptedStrokeReconnectIdentity& current) noexcept
	{
		return IsInterruptedStrokeReconnectDeviceSupported(previous.deviceType) &&
			IsInterruptedStrokeReconnectDeviceSupported(current.deviceType) &&
			previous.deviceType == current.deviceType &&
			previous.selectedTool == current.selectedTool &&
			previous.tool == current.tool &&
			previous.widthMode == current.widthMode &&
			previous.invertedCursor == current.invertedCursor &&
			previous.suppressPressure == current.suppressPressure;
	}

	bool IsInterruptedStrokeReconnectDeviceSupported(InputDeviceType deviceType) noexcept
	{
		return deviceType == InputDeviceType::Touch;
	}

	bool IsBetterInterruptedStrokeReconnectMatch(
		const InterruptedStrokeReconnectResult& candidate, int64_t candidateUpQpc,
		const InterruptedStrokeReconnectResult& current, int64_t currentUpQpc) noexcept
	{
		if (!candidate.matched) return false;
		if (!current.matched) return true;
		if (candidate.matchScore != current.matchScore)
			return candidate.matchScore < current.matchScore;
		if (candidate.selectedDirectionAngleDegrees != current.selectedDirectionAngleDegrees)
			return candidate.selectedDirectionAngleDegrees < current.selectedDirectionAngleDegrees;
		if (candidate.distance != current.distance) return candidate.distance < current.distance;
		return candidateUpQpc > currentUpQpc;
	}

	size_t GetInterruptedStrokeReconnectEvictionCount(size_t candidateCount) noexcept
	{
		return candidateCount > kMaximumInterruptedStrokeReconnectCandidates
			? candidateCount - kMaximumInterruptedStrokeReconnectCandidates : 0;
	}

	bool IsInterruptedStrokeReconnectExpired(int64_t deadlineQpc, int64_t nowQpc) noexcept
	{
		return deadlineQpc > 0 && nowQpc >= deadlineQpc;
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
		const DirectX::XMFLOAT2 halfSize = {
			kHighlighterRadiusPx / kHighlighterNibAspectRatio, kHighlighterRadiusPx };
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
			primitive.halfSize = halfSize;
			geometry.primitives.push_back(primitive);
			IncludeHighlighterSweepBounds(geometry.bounds, previous, current);
			previous = current;
		}
		if (geometry.primitives.empty())
		{
			HighlighterPrimitive primitive;
			primitive.p1 = { previous.x, previous.y };
			primitive.p2 = primitive.p1;
			primitive.halfSize = halfSize;
			geometry.primitives.push_back(primitive);
			IncludeHighlighterSweepBounds(geometry.bounds, previous, previous);
		}
	}

	HighlighterGeometry BuildHighlighterGeometry(const std::vector<InkPoint>& inputPoints)
	{
		HighlighterGeometry geometry;
		RebuildHighlighterGeometry(inputPoints, geometry);
		return geometry;
	}

	void UnionRectInPlace(RECT& target, const RECT& addition);

	HighlighterGeometry MergeHighlighterGeometry(const HighlighterGeometry& committedGeometry,
		const HighlighterGeometry& liveGeometry)
	{
		HighlighterGeometry merged;
		merged.primitives.reserve(committedGeometry.primitives.size() + liveGeometry.primitives.size());
		merged.primitives.insert(merged.primitives.end(),
			committedGeometry.primitives.begin(), committedGeometry.primitives.end());
		merged.primitives.insert(merged.primitives.end(),
			liveGeometry.primitives.begin(), liveGeometry.primitives.end());
		if (!committedGeometry.primitives.empty())
			merged.bounds = committedGeometry.bounds;
		if (!liveGeometry.primitives.empty())
			UnionRectInPlace(merged.bounds, liveGeometry.bounds); // 完成态只合并缓存几何的 bounds。
		return merged;
	}

	void BuildCompletedPenTail(const ActiveStroke& stroke, bool retainPredictionOnUp,
		double liveTipTaperSeconds, std::vector<InkPoint>& output)
	{
		output.clear();
		if (retainPredictionOnUp)
		{
			// 开关启用时原样烘干最后可见 L0（含 prediction）；优先本帧，再回退上一帧快照。
			if (!stroke.l0DrawPoints.empty())
				output.assign(stroke.l0DrawPoints.begin(), stroke.l0DrawPoints.end());
			else if (!stroke.previousL0DrawPoints.empty())
				output.assign(stroke.previousL0DrawPoints.begin(), stroke.previousL0DrawPoints.end());
			if (!output.empty()) return;
		}
		// 默认：定住真实尾部并叠加笔锋，明确去掉 prediction，避免抬笔瞬间 tip 回缩。
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
			const float padding = point.r + 3.0f; // 额外 3px 覆盖抗锯齿和胶囊端点。
			UnionRectInPlace(rect, RECT{
				static_cast<LONG>(std::floor(point.x - padding)),
				static_cast<LONG>(std::floor(point.y - padding)),
				static_cast<LONG>(std::ceil(point.x + padding)),
				static_cast<LONG>(std::ceil(point.y + padding)) });
		}
		return ClampRectToCanvas(rect, width, height);
	}

	RECT RectFromLaserPoints(std::span<const InkPoint> points,
		float dpiScale, int width, int height)
	{
		if (points.empty()) return {};
		const float scale = std::max(dpiScale, 0.01f);
		const float fallbackSolidRadius = LaserSolidRadius(scale);
		RECT rect = {};
		for (const InkPoint& point : points)
		{
			if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
			const float solidRadius = std::isfinite(point.r) && point.r > 0.0f
				? point.r : fallbackSolidRadius;
			const float padding = LaserVisualRadius(solidRadius, scale) + 3.0f;
			UnionRectInPlace(rect, RECT{
				static_cast<LONG>(std::floor(point.x - padding)),
				static_cast<LONG>(std::floor(point.y - padding)),
				static_cast<LONG>(std::ceil(point.x + padding)),
				static_cast<LONG>(std::ceil(point.y + padding)) });
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

	void AppendNewModeledPoints(ActiveStroke& stroke, float inputSpeed)
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
		size_t protectedStartIndex = FindProtectedStartIndex(
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
