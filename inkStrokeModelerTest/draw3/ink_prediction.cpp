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
		constexpr uint32_t kMouseWidthModeShift = 0;
		constexpr uint32_t kTouchWidthModeShift = 1;
		constexpr uint32_t kPenWidthModeShift = 2;
		constexpr uint32_t kInputWidthModeMask = 0x1;
		constexpr uint32_t kPenInputWidthModeMask = 0x3;
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

	}

	LaserIncrementalRanges PlanLaserIncrementalRanges(
		std::span<const InkPoint> realPoints,
		const LaserIncrementalStrokeState& state,
		double protectedDurationSeconds) noexcept
	{
		LaserIncrementalRanges ranges;
		if (realPoints.empty()) return ranges;
		const size_t protectedIndex = ink_prediction_detail::FindProtectedStartIndex(
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
		const StrokeTimingProfile timingProfile = ink_prediction_detail::GetStrokeTimingProfile(kActiveStrokeTimingProfileId);
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
			ink_prediction_detail::GetLiveTipDurationSeconds(timingProfile), kalmanParams, modelParams };
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
		return IsInterruptedStrokeReconnectIdentitySupported(previous) &&
			IsInterruptedStrokeReconnectIdentitySupported(current) &&
			previous.deviceType == current.deviceType &&
			previous.selectedTool == current.selectedTool &&
			previous.tool == current.tool &&
			previous.widthMode == current.widthMode &&
			previous.invertedCursor == current.invertedCursor &&
			previous.suppressPressure == current.suppressPressure;
	}

	bool IsInterruptedStrokeReconnectIdentitySupported(
		const InterruptedStrokeReconnectIdentity& identity) noexcept
	{
		constexpr uint32_t kEraserDrawingToolValue = 2; // DrawingTool::Eraser 的 append-only 数值。
		if (identity.deviceType == InputDeviceType::Touch) return true;
		return identity.deviceType == InputDeviceType::Pen && identity.invertedCursor &&
			identity.tool == kEraserDrawingToolValue;
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

}
