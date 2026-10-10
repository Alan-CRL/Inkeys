module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <windows.h>

module Inkeys.Drawing.Draw3.laser_particles;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr float kMinimumPositiveValue = 0.0001f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kTwoPi = 6.28318530717958647692f;
		constexpr float kMaximumMotionDeltaSeconds = 1.0f / 30.0f;
		constexpr float kMaximumCoreSpawnOffsetRatio = 0.72f;

		bool IsFinitePositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		bool IsFiniteUnit(float value) noexcept
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		}

		LONG SaturatingFloorToLong(double value) noexcept
		{
			value = std::floor(value);
			if (value <= static_cast<double>((std::numeric_limits<LONG>::min)()))
				return (std::numeric_limits<LONG>::min)();
			if (value >= static_cast<double>((std::numeric_limits<LONG>::max)()))
				return (std::numeric_limits<LONG>::max)();
			return static_cast<LONG>(value);
		}

		LONG SaturatingCeilToLong(double value) noexcept
		{
			value = std::ceil(value);
			if (value <= static_cast<double>((std::numeric_limits<LONG>::min)()))
				return (std::numeric_limits<LONG>::min)();
			if (value >= static_cast<double>((std::numeric_limits<LONG>::max)()))
				return (std::numeric_limits<LONG>::max)();
			return static_cast<LONG>(value);
		}

		void UnionRectLocal(RECT& destination, RECT source) noexcept
		{
			if (source.left >= source.right || source.top >= source.bottom) return;
			if (destination.left >= destination.right || destination.top >= destination.bottom)
			{
				destination = source;
				return;
			}
			destination.left = std::min(destination.left, source.left);
			destination.top = std::min(destination.top, source.top);
			destination.right = std::max(destination.right, source.right);
			destination.bottom = std::max(destination.bottom, source.bottom);
		}
	}

	bool IsValidLaserParticleConfig(const LaserParticleConfig& configuration) noexcept
	{
		return IsFinitePositive(configuration.idleEmissionRatePerSecond) &&
			IsFinitePositive(configuration.motionEmissionSpacingDip) &&
			IsFinitePositive(configuration.maximumEmissionRatePerSecond) &&
			configuration.maximumEmissionRatePerSecond >=
				configuration.idleEmissionRatePerSecond &&
			configuration.maximumSpawnPerFrame > 0 &&
			configuration.maximumSpawnPerFrame <= kLaserParticleCapacity &&
			IsFinitePositive(configuration.minimumLifetimeSeconds) &&
			IsFinitePositive(configuration.maximumLifetimeSeconds) &&
			configuration.maximumLifetimeSeconds >=
				configuration.minimumLifetimeSeconds &&
			IsFinitePositive(configuration.minimumLaunchSpeedDipPerSecond) &&
			IsFinitePositive(configuration.maximumLaunchSpeedDipPerSecond) &&
			configuration.maximumLaunchSpeedDipPerSecond >=
				configuration.minimumLaunchSpeedDipPerSecond &&
			std::isfinite(configuration.maximumDeflectionAngleDegrees) &&
			configuration.maximumDeflectionAngleDegrees >= 0.0f &&
			configuration.maximumDeflectionAngleDegrees <= 90.0f &&
			std::isfinite(configuration.shrinkStartTravelRatio) &&
			configuration.shrinkStartTravelRatio >= 0.0f &&
			configuration.shrinkStartTravelRatio < 1.0f &&
			IsFinitePositive(configuration.endRadiusScale) &&
			configuration.endRadiusScale <= 1.0f &&
			IsFinitePositive(configuration.minimumRadiusDip) &&
			IsFinitePositive(configuration.maximumRadiusDip) &&
			configuration.maximumRadiusDip >= configuration.minimumRadiusDip &&
			IsFinitePositive(configuration.sizeDistributionExponent) &&
			IsFiniteUnit(configuration.sizeBrightnessCorrelation) &&
			IsFiniteUnit(configuration.sizeTravelCorrelation) &&
			IsFinitePositive(configuration.glowRadiusScale) &&
			IsFiniteUnit(configuration.glowRed) &&
			IsFiniteUnit(configuration.glowGreen) &&
			IsFiniteUnit(configuration.glowBlue) &&
			IsFiniteUnit(configuration.glowAlpha) &&
			IsFiniteUnit(configuration.coreColorWhiteMix) &&
			IsFiniteUnit(configuration.coreColorWhiteMixJitter) &&
			IsFiniteUnit(configuration.minimumBrightness) &&
			configuration.maximumBrightness >= configuration.minimumBrightness &&
			configuration.maximumBrightness <= 1.0f &&
			IsFiniteUnit(configuration.breathingAmplitude) &&
			IsFinitePositive(configuration.minimumBreathingFrequencyHz) &&
			IsFinitePositive(configuration.maximumBreathingFrequencyHz) &&
			configuration.maximumBreathingFrequencyHz >=
				configuration.minimumBreathingFrequencyHz &&
			IsFinitePositive(configuration.breathingRampSeconds);
	}

	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float wallDeltaSeconds,
		float motionSpeedDipPerSecond, float fractionalParticles,
		uint32_t remainingFrameBudget,
		const LaserParticleConfig& configuration) noexcept
	{
		LaserParticleEmissionSchedule result;
		if (!IsValidLaserParticleConfig(configuration)) return result;
		const float wallDelta = std::isfinite(wallDeltaSeconds)
			? std::max(wallDeltaSeconds, 0.0f) : 0.0f;
		const float emissionDelta = std::min(wallDelta, kMaximumMotionDeltaSeconds);
		const float motionSpeed = std::isfinite(motionSpeedDipPerSecond)
			? std::max(motionSpeedDipPerSecond, 0.0f) : 0.0f;
		result.emissionRatePerSecond = std::min(
			configuration.idleEmissionRatePerSecond +
				motionSpeed / configuration.motionEmissionSpacingDip,
			configuration.maximumEmissionRatePerSecond);
		const float carry = std::isfinite(fractionalParticles)
			? std::clamp(fractionalParticles, 0.0f, 0.999999f) : 0.0f;
		const float total = carry + result.emissionRatePerSecond * emissionDelta;
		const uint32_t available = static_cast<uint32_t>(std::min(
			std::floor(static_cast<double>(total)),
			static_cast<double>((std::numeric_limits<uint32_t>::max)())));
		result.count = std::min(available, remainingFrameBudget);
		// 全局预算截断的整数不回灌，避免下一帧补发积压。
		result.fractionalParticles = total - std::floor(total);
		if (!std::isfinite(result.fractionalParticles))
			result.fractionalParticles = 0.0f;
		return result;
	}

	float LaserParticleLifeFactor(float ageSeconds, float lifetimeSeconds) noexcept
	{
		const float age = std::isfinite(ageSeconds) ? std::max(ageSeconds, 0.0f) : 0.0f;
		const float lifetime = IsFinitePositive(lifetimeSeconds)
			? lifetimeSeconds : kMinimumPositiveValue;
		const float ratio = std::clamp(age / lifetime, 0.0f, 1.0f);
		const float smooth = ratio * ratio * (3.0f - 2.0f * ratio);
		return 1.0f - smooth;
	}

	float LaserParticleRadiusScale(float traveledDistance, float maximumTravelDistance,
		const LaserParticleConfig& configuration) noexcept
	{
		if (!IsValidLaserParticleConfig(configuration)) return 0.0f;
		const float traveled = std::isfinite(traveledDistance)
			? std::max(traveledDistance, 0.0f) : 0.0f;
		const float maximumTravel = IsFinitePositive(maximumTravelDistance)
			? maximumTravelDistance : kMinimumPositiveValue;
		const float travelRatio = std::clamp(traveled / maximumTravel, 0.0f, 1.0f);
		const float shrinkRatio = std::clamp(
			(travelRatio - configuration.shrinkStartTravelRatio) /
			(1.0f - configuration.shrinkStartTravelRatio), 0.0f, 1.0f);
		const float smooth = shrinkRatio * shrinkRatio * (3.0f - 2.0f * shrinkRatio);
		return 1.0f + (configuration.endRadiusScale - 1.0f) * smooth;
	}

	LaserParticleBirthSample ResolveLaserParticleBirthSample(
		float sizeSample, float launchSpeedSample, float lifetimeSample,
		float brightnessSample,
		const LaserParticleConfig& configuration) noexcept
	{
		LaserParticleBirthSample result;
		if (!IsValidLaserParticleConfig(configuration) ||
			!std::isfinite(sizeSample) ||
			!std::isfinite(launchSpeedSample) ||
			!std::isfinite(lifetimeSample) ||
			!std::isfinite(brightnessSample)) return result;
		const float sizeRatio = std::pow(
			std::clamp(sizeSample, 0.0f, 1.0f),
			configuration.sizeDistributionExponent);
		const float independentSpeedRatio =
			std::clamp(launchSpeedSample, 0.0f, 1.0f);
		const float speedRatio =
			independentSpeedRatio *
				(1.0f - configuration.sizeTravelCorrelation) +
			(1.0f - sizeRatio) * configuration.sizeTravelCorrelation;
		const float lifetimeRatio = std::clamp(lifetimeSample, 0.0f, 1.0f);
		const float independentBrightnessRatio =
			std::clamp(brightnessSample, 0.0f, 1.0f);
		const float brightnessRatio =
			independentBrightnessRatio *
				(1.0f - configuration.sizeBrightnessCorrelation) +
			sizeRatio * configuration.sizeBrightnessCorrelation;
		result.launchSpeedDipPerSecond =
			configuration.minimumLaunchSpeedDipPerSecond +
			(configuration.maximumLaunchSpeedDipPerSecond -
				configuration.minimumLaunchSpeedDipPerSecond) * speedRatio;
		result.lifetimeSeconds = configuration.minimumLifetimeSeconds +
			(configuration.maximumLifetimeSeconds -
				configuration.minimumLifetimeSeconds) * lifetimeRatio;
		result.baseRadiusDip = configuration.minimumRadiusDip +
			(configuration.maximumRadiusDip -
				configuration.minimumRadiusDip) * sizeRatio;
		result.baseBrightness = configuration.minimumBrightness +
			(configuration.maximumBrightness -
				configuration.minimumBrightness) * brightnessRatio;
		result.maximumTravelDistanceDip =
			result.launchSpeedDipPerSecond * result.lifetimeSeconds * 0.5f;
		result.valid = true;
		return result;
	}

	float EvaluateLaserParticleBrightness(float baseBrightness, float ageSeconds,
		float breathingFrequencyHz, float breathingPhase,
		const LaserParticleConfig& configuration) noexcept
	{
		const float base = std::isfinite(baseBrightness)
			? std::clamp(baseBrightness, 0.0f, 1.0f) : 1.0f;
		const float age = std::isfinite(ageSeconds) ? std::max(ageSeconds, 0.0f) : 0.0f;
		const float frequency = IsFinitePositive(breathingFrequencyHz)
			? breathingFrequencyHz : configuration.minimumBreathingFrequencyHz;
		const float phase = std::isfinite(breathingPhase) ? breathingPhase : 0.0f;
		const float rampRatio = std::clamp(age /
			std::max(configuration.breathingRampSeconds, kMinimumPositiveValue),
			0.0f, 1.0f);
		const float ramp = rampRatio * rampRatio * (3.0f - 2.0f * rampRatio);
		return std::clamp(base + configuration.breathingAmplitude *
			std::sin(kTwoPi * frequency * age + phase) * ramp, 0.0f, 1.0f);
	}

	float LaserParticleGlowExtentDip(float currentRadiusDip,
		const LaserParticleConfig& configuration) noexcept
	{
		if (!IsValidLaserParticleConfig(configuration) ||
			!std::isfinite(currentRadiusDip)) return 0.0f;
		return std::max(currentRadiusDip, 0.0f) *
			configuration.glowRadiusScale;
	}

	float MaximumLaserParticleTravelDip(
		const LaserParticleConfig& configuration) noexcept
	{
		if (!IsValidLaserParticleConfig(configuration)) return 0.0f;
		// 1-smoothstep 在完整寿命上的积分为 1/2；额外保留一个最大运动步。
		return configuration.maximumLaunchSpeedDipPerSecond *
			(configuration.maximumLifetimeSeconds * 0.5f +
				kMaximumMotionDeltaSeconds);
	}

	int64_t LaserParticleLifetimeDeadlineQpc(int64_t nowQpc,
		int64_t qpcFrequency, const LaserParticleConfig& configuration) noexcept
	{
		if (nowQpc < 0 || qpcFrequency <= 0 ||
			!IsValidLaserParticleConfig(configuration)) return nowQpc;
		const double lifetimeTicks = std::ceil(
			static_cast<double>(configuration.maximumLifetimeSeconds) *
			static_cast<double>(qpcFrequency));
		const double remainingTicks = static_cast<double>(
			(std::numeric_limits<int64_t>::max)() - nowQpc);
		if (lifetimeTicks >= remainingTicks)
			return (std::numeric_limits<int64_t>::max)();
		return nowQpc + static_cast<int64_t>(lifetimeTicks);
	}

	LaserParticleEmissionDirection ResolveLaserParticleEmissionDirection(
		float tangentX, float tangentY, float sideSample, float deflectionSample,
		const LaserParticleConfig& configuration) noexcept
	{
		LaserParticleEmissionDirection result;
		if (!IsValidLaserParticleConfig(configuration) ||
			!std::isfinite(tangentX) || !std::isfinite(tangentY) ||
			!std::isfinite(sideSample) || !std::isfinite(deflectionSample))
			return result;
		const float tangentLength = std::hypot(tangentX, tangentY);
		if (tangentLength <= kMinimumPositiveValue) return result;
		const float normalizedTangentX = tangentX / tangentLength;
		const float normalizedTangentY = tangentY / tangentLength;
		const float side = sideSample < 0.5f ? -1.0f : 1.0f;
		const float normalX = -normalizedTangentY * side;
		const float normalY = normalizedTangentX * side;
		const float deflectionRadians =
			(std::clamp(deflectionSample, 0.0f, 1.0f) * 2.0f - 1.0f) *
			configuration.maximumDeflectionAngleDegrees * kPi / 180.0f;
		const float cosine = std::cos(deflectionRadians);
		const float sine = std::sin(deflectionRadians);
		result.x = normalX * cosine + normalizedTangentX * sine;
		result.y = normalY * cosine + normalizedTangentY * sine;
		const float directionLength = std::hypot(result.x, result.y);
		if (directionLength <= kMinimumPositiveValue) return {};
		result.x /= directionLength;
		result.y /= directionLength;
		result.valid = true;
		return result;
	}

	LaserParticleMotionStep EvaluateLaserParticleMotionStep(float velocityX,
		float velocityY, float ageSeconds, float lifetimeSeconds,
		float motionDeltaSeconds) noexcept
	{
		LaserParticleMotionStep result;
		if (!std::isfinite(velocityX) || !std::isfinite(velocityY)) return result;
		const float delta = std::isfinite(motionDeltaSeconds)
			? std::clamp(motionDeltaSeconds, 0.0f, kMaximumMotionDeltaSeconds) : 0.0f;
		const float lifeFactor = LaserParticleLifeFactor(ageSeconds, lifetimeSeconds);
		result.deltaX = velocityX * lifeFactor * delta;
		result.deltaY = velocityY * lifeFactor * delta;
		result.distance = std::hypot(result.deltaX, result.deltaY);
		return result;
	}

	RECT ConservativeLaserParticleBatchBounds(
		const LaserParticleEmissionRequest& request,
		const LaserParticleConfig& configuration, float dpiScale,
		float coreRadiusRatio) noexcept
	{
		if (!IsValidLaserParticleConfig(configuration) ||
			!std::isfinite(request.positionX) || !std::isfinite(request.positionY))
			return {};
		const float scale = std::isfinite(dpiScale)
			? std::max(dpiScale, 0.01f) : 1.0f;
		const float coreRatio = std::isfinite(coreRadiusRatio)
			? std::clamp(coreRadiusRatio, 0.0f, 1.0f) : 1.0f / 3.0f;
		const float entityRadius = std::isfinite(request.entityRadius)
			? std::max(request.entityRadius, 0.0f) : 0.0f;
		// 与 VS 一致计入固定 2 DIP 辉光地板和 quad 额外 2px，避免高 DPI 清理范围偏小。
		const double padding =
			static_cast<double>(MaximumLaserParticleTravelDip(configuration)) * scale +
			static_cast<double>(entityRadius) * coreRatio * kMaximumCoreSpawnOffsetRatio +
			static_cast<double>(configuration.maximumRadiusDip) *
				(1.0 + configuration.glowRadiusScale) * scale +
			2.0 * scale + 2.0;
		if (!std::isfinite(padding)) return {};
		return {
			SaturatingFloorToLong(static_cast<double>(request.positionX) - padding),
			SaturatingFloorToLong(static_cast<double>(request.positionY) - padding),
			SaturatingCeilToLong(static_cast<double>(request.positionX) + padding),
			SaturatingCeilToLong(static_cast<double>(request.positionY) + padding)
		};
	}

	void LaserParticleDirtyTracker::Add(RECT bounds, int64_t expiresQpc) noexcept
	{
		if (bounds.left >= bounds.right || bounds.top >= bounds.bottom ||
			expiresQpc <= 0) return;
		for (Region& region : regions_)
		{
			if (region.active) continue;
			region = { bounds, expiresQpc, true };
			return;
		}

		// 极端高帧率下容量耗尽时合并最早到期槽，宁可扩大脏区也不能漏清残影。
		Region& fallback = *std::min_element(regions_.begin(), regions_.end(),
			[](const Region& left, const Region& right)
			{
				return left.expiresQpc < right.expiresQpc;
			});
		UnionRectLocal(fallback.bounds, bounds);
		fallback.expiresQpc = std::max(fallback.expiresQpc, expiresQpc);
		fallback.active = true;
	}

	LaserParticleDirtySnapshot LaserParticleDirtyTracker::Snapshot(
		int64_t nowQpc) noexcept
	{
		LaserParticleDirtySnapshot snapshot;
		for (Region& region : regions_)
		{
			if (!region.active) continue;
			if (region.expiresQpc <= nowQpc)
			{
				snapshot.expiredAny = true;
				region = {};
				continue;
			}
			snapshot.hasActive = true;
			UnionRectLocal(snapshot.activeBounds, region.bounds);
		}
		return snapshot;
	}

	void LaserParticleDirtyTracker::Clear() noexcept
	{
		regions_ = {};
	}
}
