module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
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

		bool IsFinitePositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		bool IsFiniteUnit(float value) noexcept
		{
			return value >= 0.0f && value <= 1.0f;
		}

		uint32_t HashParticleSeed(uint32_t value) noexcept
		{
			value ^= value >> 16;
			value *= 0x7FEB352Du;
			value ^= value >> 15;
			value *= 0x846CA68Bu;
			return value ^ (value >> 16);
		}

		float SampleParticleSeed(uint32_t value) noexcept
		{
			return static_cast<float>(HashParticleSeed(value) & 0x00FFFFFFu) / 16777216.0f;
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

	bool IsValidLaserParticleConfig(const LaserParticleConfig& config) noexcept
	{
		return std::isfinite(config.idleEmissionRatePerSecond) && config.idleEmissionRatePerSecond >= 0.0f &&
			IsFinitePositive(config.motionEmissionSpacingDip) &&
			IsFinitePositive(config.maximumEmissionRatePerSecond) &&
			config.maximumSpawnPerFrame > 0 && config.maximumSpawnPerFrame <= kLaserParticleCapacity &&
			IsFinitePositive(config.minimumLifetimeSeconds) &&
			config.maximumLifetimeSeconds >= config.minimumLifetimeSeconds &&
			config.maximumLifetimeSeconds <= 60.0f &&
			IsFinitePositive(config.minimumLaunchSpeedDipPerSecond) &&
			IsFinitePositive(config.maximumLaunchSpeedDipPerSecond) &&
			config.maximumLaunchSpeedDipPerSecond >= config.minimumLaunchSpeedDipPerSecond &&
			config.maximumDeflectionAngleDegrees >= 0.0f && config.maximumDeflectionAngleDegrees <= 90.0f &&
			config.backwardBiasDegrees >= 0.0f && config.backwardBiasDegrees <= 90.0f &&
			IsFiniteUnit(config.trailBirthOffsetRatio) &&
			config.shrinkStartTravelRatio >= 0.0f && config.shrinkStartTravelRatio <= 0.9999f &&
			IsFiniteUnit(config.endRadiusScale) && IsFinitePositive(config.minimumRadiusDip) &&
			IsFinitePositive(config.maximumRadiusDip) && config.maximumRadiusDip >= config.minimumRadiusDip &&
			IsFinitePositive(config.sizeDistributionExponent) && IsFiniteUnit(config.sizeBrightnessCorrelation) &&
			IsFiniteUnit(config.sizeTravelCorrelation) && IsFiniteUnit(config.minimumBrightness) &&
			IsFiniteUnit(config.breathingAmplitude) && IsFinitePositive(config.minimumBreathingFrequencyHz) &&
			IsFinitePositive(config.maximumBreathingFrequencyHz) &&
			config.maximumBreathingFrequencyHz >= config.minimumBreathingFrequencyHz &&
			IsFinitePositive(config.breathingRampSeconds) &&
			std::isfinite(config.clickMaximumSeconds) && config.clickMaximumSeconds >= 0.0 &&
			IsFinitePositive(config.clickMaximumDisplacementDip) &&
			config.clickWaveLifetimeMilliseconds > 0 && config.clickWaveLifetimeMilliseconds <= 60000 &&
			IsFinitePositive(config.clickWaveExpansionRatio) && IsFinitePositive(config.clickWaveLineWidthRatio);
	}

	bool IsLaserParticleClick(double durationSeconds, float maximumDisplacementSquaredPx,
		float dpiScale, const LaserParticleConfig& config) noexcept
	{
		const float maximumPx = config.clickMaximumDisplacementDip * dpiScale;
		return IsFinitePositive(dpiScale) && durationSeconds >= 0.0 &&
			durationSeconds <= config.clickMaximumSeconds && maximumDisplacementSquaredPx >= 0.0f &&
			std::isfinite(maximumDisplacementSquaredPx) && maximumDisplacementSquaredPx <= maximumPx * maximumPx;
	}

	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float wallDeltaSeconds,
		float motionSpeedDipPerSecond, float fractionalParticles, uint32_t remainingFrameBudget,
		const LaserParticleConfig& config) noexcept
	{
		LaserParticleEmissionSchedule result;
		if (!IsValidLaserParticleConfig(config)) return result;
		const float delta = std::isfinite(wallDeltaSeconds)
			? std::clamp(wallDeltaSeconds, 0.0f, kMaximumMotionDeltaSeconds) : 0.0f;
		const float motionSpeed = std::isfinite(motionSpeedDipPerSecond)
			? (std::max)(motionSpeedDipPerSecond, 0.0f) : 0.0f;
		result.emissionRatePerSecond = (std::min)(config.idleEmissionRatePerSecond +
			motionSpeed / config.motionEmissionSpacingDip, config.maximumEmissionRatePerSecond);
		const float carry = std::isfinite(fractionalParticles)
			? std::clamp(fractionalParticles, 0.0f, 0.999999f) : 0.0f;
		const double total = carry + static_cast<double>(result.emissionRatePerSecond) * delta;
		const double whole = std::floor(total);
		// 超预算的整数不回灌，避免随后补发。
		result.count = static_cast<uint32_t>((std::min)(whole, static_cast<double>(remainingFrameBudget)));
		result.fractionalParticles = static_cast<float>(total - whole);
		return result;
	}

	LaserParticleEmissionDirection ResolveLaserParticleEmissionDirection(float tangentX,
		float tangentY, float sideSample, float deflectionSample, const LaserParticleConfig& config) noexcept
	{
		const float length = std::hypot(tangentX, tangentY);
		if (!std::isfinite(length) || length <= kMinimumPositiveValue ||
			!std::isfinite(sideSample) || !std::isfinite(deflectionSample))
			return {};
		// 沿法线向两侧喷出才能离开激光主体；偏转角以“朝运动反方向”为正，整体再向后偏一点。
		const float angle = (std::lerp(-1.0f, 1.0f, std::clamp(deflectionSample, 0.0f, 1.0f)) *
			config.maximumDeflectionAngleDegrees + config.backwardBiasDegrees) * kPi / 180.0f;
		const float side = sideSample < 0.5f ? -1.0f : 1.0f;
		const float x = tangentX / length, y = tangentY / length;
		return { -y * side * std::cos(angle) - x * std::sin(angle),
			x * side * std::cos(angle) - y * std::sin(angle), true };
	}

	void AppendLaserTrailParticleSeeds(std::vector<LaserParticleSeed>& seeds, float x, float y,
		float tangentX, float tangentY, float entityRadius,
		float dpiScale, const float (&color)[4], uint32_t count,
		uint32_t seedBase, uint32_t group, const LaserParticleConfig& config)
	{
		for (uint32_t i = 0; i < count; ++i)
		{
			const uint32_t random = HashParticleSeed(seedBase + i * 0x9E3779B9u);
			const float sideSample = SampleParticleSeed(random ^ 0x967A889Bu);
			const auto direction = ResolveLaserParticleEmissionDirection(tangentX, tangentY,
				sideSample, SampleParticleSeed(random ^ 0x02E5BE93u), config);
			if (!direction.valid) return;
			const float size = std::pow(SampleParticleSeed(random ^ 0xA511E9B3u), config.sizeDistributionExponent);
			const float speed = std::lerp(config.minimumLaunchSpeedDipPerSecond,
				config.maximumLaunchSpeedDipPerSecond, std::lerp(
				SampleParticleSeed(random ^ 0xC2B2AE35u), 1.0f - size, config.sizeTravelCorrelation)) * dpiScale;
			const float radius = std::lerp(config.minimumRadiusDip, config.maximumRadiusDip, size) * dpiScale;
			// 出生在喷出一侧、贴着内芯散射带外沿，随后向外走：粒子画在主体之上也不会压到内芯。
			const float offset = (sideSample < 0.5f ? -1.0f : 1.0f) *
				(entityRadius * config.trailBirthOffsetRatio + radius);
			LaserParticleSeed seed;
			seed.position[0] = x - tangentY * offset;
			seed.position[1] = y + tangentX * offset;
			seed.velocity[0] = direction.x * speed;
			seed.velocity[1] = direction.y * speed;
			std::copy_n(color, 4, seed.color);
			seed.radius = radius;
			seed.brightness = std::lerp(config.minimumBrightness, 1.0f, std::lerp(
				SampleParticleSeed(random ^ 0x165667B1u), size, config.sizeBrightnessCorrelation));
			seed.breathingFrequencyHz = std::lerp(config.minimumBreathingFrequencyHz,
				config.maximumBreathingFrequencyHz, SampleParticleSeed(random ^ 0xD3A2646Cu));
			seed.breathingPhase = SampleParticleSeed(random ^ 0x85EBCA77u) * kTwoPi;
			seed.breathingAmplitude = config.breathingAmplitude;
			seed.breathingRampSeconds = config.breathingRampSeconds;
			seed.lifetimeMilliseconds = static_cast<uint32_t>(std::ceil(1000.0f * std::lerp(
				config.minimumLifetimeSeconds, config.maximumLifetimeSeconds, SampleParticleSeed(random ^ 0x27D4EB2Fu))));
			// 寿命减速曲线 1 - smoothstep 的积分为 1/2，与粒子着色器使用同一个运动模型。
			seed.maximumTravelDistance = 0.5f * speed * seed.lifetimeMilliseconds * 0.001f;
			seed.randomSeed = random;
			seed.group = group;
			seeds.push_back(seed);
		}
	}

	bool EvaluateLaserClickWave(const LaserClickWave& wave, int64_t nowQpc, int64_t qpcFrequency,
		const LaserParticleConfig& config, LaserClickWaveVisual& visual) noexcept
	{
		if (qpcFrequency <= 0 || !IsFinitePositive(wave.entityRadius)) return false;
		const double elapsed = static_cast<double>((std::max)(nowQpc - wave.startQpc, int64_t{0})) * 1000.0 /
			static_cast<double>(qpcFrequency);
		if (!(elapsed < static_cast<double>(config.clickWaveLifetimeMilliseconds))) return false;
		// 一圈细线从实体边缘向外荡开：先快后慢，线宽与透明度随扩散衰减。
		const float fade = 1.0f - static_cast<float>(elapsed / config.clickWaveLifetimeMilliseconds);
		visual.x = wave.x;
		visual.y = wave.y;
		visual.radius = wave.entityRadius * (1.0f + (1.0f - fade * fade * fade) * config.clickWaveExpansionRatio);
		visual.halfWidth = 0.5f * config.clickWaveLineWidthRatio * wave.entityRadius * (std::max)(fade, 0.25f);
		std::copy_n(wave.color, 3, visual.color);
		visual.opacity = 0.9f * fade * fade * std::clamp(wave.color[3], 0.0f, 1.0f);
		return true;
	}

	RECT LaserClickWaveBounds(const LaserClickWave& wave, const LaserParticleConfig& config) noexcept
	{
		// 最大环半径 + 起始半线宽，四边另加 2px 抗锯齿边距（与类型 10 的顶点外扩一致）。
		const double padding = static_cast<double>(wave.entityRadius) *
			(1.0 + config.clickWaveExpansionRatio + 0.5 * config.clickWaveLineWidthRatio) + 2.0;
		if (!std::isfinite(padding) || !std::isfinite(wave.x) || !std::isfinite(wave.y)) return {};
		return { SaturatingFloorToLong(wave.x - padding), SaturatingFloorToLong(wave.y - padding),
			SaturatingCeilToLong(wave.x + padding), SaturatingCeilToLong(wave.y + padding) };
	}

	namespace
	{
		// 与 laserParticleVS.hlsl 的 DriftWindField 相同：坐标取“风向, 法向”基下的 DIP。
		void DriftWindField(float x, float y, float& fieldX, float& fieldY) noexcept
		{
			fieldX = std::sin(x * -0.01376f + y * 0.03007f) + 0.6f * std::sin(x * 0.04706f + y * 0.05930f + 1.7f);
			fieldY = std::sin(x * 0.01200f + y * 0.04325f + 0.6f);
		}

		uint32_t PackUnorm8(float value) noexcept
		{
			return static_cast<uint32_t>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f) * 255.0f));
		}

		float Smoothstep01(float value) noexcept
		{
			const float t = std::clamp(value, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	LaserGpuParticle PackLaserParticle(const LaserParticleSeed& seed, uint32_t bornMilliseconds,
		uint32_t dissolveBatch) noexcept
	{
		LaserGpuParticle particle = {};
		particle.position[0] = seed.position[0];
		particle.position[1] = seed.position[1];
		particle.shapeParameters[0] = seed.radius;
		particle.shapeParameters[1] = seed.brightness;
		particle.color = PackUnorm8(seed.color[0]) | (PackUnorm8(seed.color[1]) << 8) |
			(PackUnorm8(seed.color[2]) << 16) | (PackUnorm8(seed.color[3]) << 24);
		particle.lifetimeMilliseconds = seed.lifetimeMilliseconds;
		particle.releaseMilliseconds = bornMilliseconds + seed.releaseDelayMilliseconds;
		if (seed.kind == LaserParticleKind::Dissolve)
		{
			// 种子字段复用见 AppendDriftParticleSeed。凡是每粒固定不变的量都在这里算好，
			// 顶点着色器每帧只剩两次 exp 和三次 sin。
			particle.motion[0] = seed.velocity[0];
			particle.motion[1] = seed.velocity[1];
			const float speed = (std::max)(std::hypot(seed.velocity[0], seed.velocity[1]), kMinimumPositiveValue);
			const float windX = seed.velocity[0] / speed, windY = seed.velocity[1] / speed;
			particle.motionParameters[0] = (std::max)(seed.dragPerSecond, kMinimumPositiveValue);
			particle.motionParameters[1] = seed.breathingAmplitude;
			particle.motionParameters[2] = std::cos(seed.breathingPhase) * seed.angularSpeedRadians;
			particle.motionParameters[3] = std::sin(seed.breathingPhase) * seed.angularSpeedRadians;
			const float dipPerPixel = seed.breathingFrequencyHz;
			DriftWindField((seed.position[0] * windX + seed.position[1] * windY) * dipPerPixel,
				(seed.position[1] * windX - seed.position[0] * windY) * dipPerPixel,
				particle.shapeParameters[2], particle.shapeParameters[3]);
			particle.kindAndBatch = static_cast<uint32_t>(LaserParticleKind::Dissolve) | (dissolveBatch << 2);
		}
		else
		{
			const float lifetimeSeconds = seed.lifetimeMilliseconds * 0.001f;
			particle.motion[0] = seed.velocity[0] * lifetimeSeconds;
			particle.motion[1] = seed.velocity[1] * lifetimeSeconds;
			particle.motionParameters[0] = kTwoPi * seed.breathingFrequencyHz;
			particle.motionParameters[1] = seed.breathingPhase;
			particle.motionParameters[2] = seed.breathingAmplitude;
			particle.motionParameters[3] = 1.0f / (std::max)(seed.breathingRampSeconds, kMinimumPositiveValue);
			particle.shapeParameters[2] = std::hypot(particle.motion[0], particle.motion[1]) /
				(std::max)(seed.maximumTravelDistance, kMinimumPositiveValue);
			particle.kindAndBatch = static_cast<uint32_t>(LaserParticleKind::Trail);
		}
		return particle;
	}

	LaserParticleState EvaluateLaserParticle(const LaserGpuParticle& particle, const LaserParticleClock& clock,
		float dipPerPixel, const LaserParticleConfig& config) noexcept
	{
		LaserParticleState state;
		const uint32_t kind = particle.kindAndBatch & 3u;
		const bool gathering = clock.gatherBatch != 0 && (particle.kindAndBatch >> 2) == clock.gatherBatch;
		const uint32_t elapsed = (gathering ? clock.gatherMilliseconds : clock.nowMilliseconds) -
			particle.releaseMilliseconds;
		if (elapsed >= particle.lifetimeMilliseconds) return state;
		const float age = static_cast<float>(elapsed) * 0.001f;
		const float life = static_cast<float>(elapsed) / static_cast<float>(particle.lifetimeMilliseconds);
		if (kind == static_cast<uint32_t>(LaserParticleKind::Dissolve))
		{
			const float speed = (std::max)(std::hypot(particle.motion[0], particle.motion[1]), kMinimumPositiveValue);
			const float windX = particle.motion[0] / speed, windY = particle.motion[1] / speed;
			const float drag = particle.motionParameters[0];
			const float travel = speed * (age - (1.0f - std::exp(-drag * age)) / drag);
			float fieldX = 0.0f, fieldY = 0.0f;
			DriftWindField((particle.position[0] * windX + particle.position[1] * windY + travel) * dipPerPixel,
				(particle.position[1] * windX - particle.position[0] * windY) * dipPerPixel, fieldX, fieldY);
			const float meanderX = (fieldX - particle.shapeParameters[2]) * particle.motionParameters[1];
			const float meanderY = (fieldY - particle.shapeParameters[3]) * particle.motionParameters[1];
			const float burst = 1.0f - std::exp(-3.0f * age);
			const float remaining = gathering ? clock.gatherRemaining : 1.0f;
			state.x = particle.position[0] + (windX * (travel + 0.32f * meanderY) - windY * meanderX +
				particle.motionParameters[2] * burst) * remaining;
			state.y = particle.position[1] + (windY * (travel + 0.32f * meanderY) + windX * meanderX +
				particle.motionParameters[3] * burst) * remaining;
			state.radius = particle.shapeParameters[0] * std::lerp(1.0f, kLaserDissolveParameters.endRadiusScale, life);
			state.opacity = (1.0f - life * life) * remaining;
			const float whiteness = std::clamp(1.0f - age / kLaserDissolveParameters.whiteFadeSeconds, 0.0f, 1.0f);
			state.white = 0.94f * std::clamp(particle.shapeParameters[1] * whiteness * whiteness, 0.0f, 1.0f);
		}
		else
		{
			const float progress = life - life * life * life * (1.0f - 0.5f * life);
			state.x = particle.position[0] + particle.motion[0] * progress;
			state.y = particle.position[1] + particle.motion[1] * progress;
			const float travel = std::clamp(progress * particle.shapeParameters[2], 0.0f, 1.0f);
			const float shrink = Smoothstep01((travel - config.shrinkStartTravelRatio) /
				(1.0f - config.shrinkStartTravelRatio));
			state.radius = particle.shapeParameters[0] * std::lerp(1.0f, config.endRadiusScale, shrink);
			const float brightness = std::clamp(particle.shapeParameters[1] + particle.motionParameters[2] *
				std::sin(particle.motionParameters[0] * age + particle.motionParameters[1]) *
				Smoothstep01(age * particle.motionParameters[3]), 0.0f, 1.0f);
			state.opacity = (1.0f - Smoothstep01(life)) * brightness;
		}
		state.visible = state.radius > 0.0f && state.opacity > 0.0f;
		return state;
	}

	LaserDissolveFront MakeLaserDissolveFront() noexcept
	{
		// 整条线同时开始碎，不沿某个方向推进：投影方向取零，
		// 下面与类型 11 共用的公式里 base 就退化成常数 crumbleMidpoint，先后只由像素噪声决定。
		return { 0.0f, 0.0f, kLaserDissolveParameters.crumbleMidpoint - 1.0f, 1.0f };
	}

	static float LaserDissolveEdgeCenter(const LaserDissolveFront& front, float x, float y) noexcept
	{
		// 粒子坐标取其覆盖的像素中心；与类型 11 的 SV_POSITION 使用同一哈希。
		const float cellX = std::floor(x), cellY = std::floor(y);
		const auto pixelX = static_cast<uint32_t>(SaturatingFloorToLong(cellX));
		const auto pixelY = static_cast<uint32_t>(SaturatingFloorToLong(cellY));
		// 与类型 11 相同：两个哈希取平均，得到缓入缓出的三角分布。
		const uint32_t hash = HashParticleSeed(pixelX ^ (pixelY * 0x9E3779B9u));
		uint32_t second = (hash ^ 0x68E31DA4u) * 0x9E3779B9u;
		second ^= second >> 16;
		const float noise = (static_cast<float>(hash & 0x00FFFFFFu) + static_cast<float>(second >> 8)) / 33554432.0f;
		const float projection = (cellX + 0.5f) * front.directionX + (cellY + 0.5f) * front.directionY;
		const float edge = kLaserDissolveParameters.edgeHalfWidth;
		const float base = 1.0f - (projection - front.projectionMinimum) * front.inverseProjectionSpan;
		const float threshold = std::clamp(base + (noise - 0.5f) * kLaserDissolveParameters.noiseAmplitude,
			0.0f, 1.0f);
		return edge + (1.0f - 2.0f * edge) * threshold;
	}

	static float LaserDissolveReleaseProgress(const LaserDissolveFront& front, float x, float y) noexcept
	{
		// 等软边完全扫过出生像素再释放，与类型 11 遮罩归零对齐。
		return (std::min)(1.0f, LaserDissolveEdgeCenter(front, x, y) + kLaserDissolveParameters.edgeHalfWidth);
	}

	float LaserDissolveCoverage(const LaserDissolveFront& front, float x, float y) noexcept
	{
		if (front.progress <= 0.0f) return 1.0f;
		if (front.progress >= 1.0f) return 0.0f;
		const float center = LaserDissolveEdgeCenter(front, x, y);
		const float edge = kLaserDissolveParameters.edgeHalfWidth;
		const float t = std::clamp((front.progress - (center - edge)) / (2.0f * edge), 0.0f, 1.0f);
		return 1.0f - t * t * (3.0f - 2.0f * t);
	}

	uint32_t LaserDissolveSeedCount(double lengthDip) noexcept
	{
		if (!std::isfinite(lengthDip) || lengthDip <= 0.0) return 0;
		return static_cast<uint32_t>((std::min)(lengthDip * kLaserDissolveParameters.seedsPerDip,
			static_cast<double>(kLaserDissolveParameters.maximumSeeds)));
	}

	// 随风飘散的一粒种子。激光飘散与清空画布共用：来源只决定出生位置、颜色、白芯比例与释放时刻。
	static void AppendDriftParticleSeed(std::vector<LaserParticleSeed>& seeds, float x, float y,
		float white, float dpiScale, const float (&color)[4], uint32_t random, uint32_t group,
		float windAngleDegrees, uint32_t releaseDelayMilliseconds)
	{
		const auto& config = kLaserDissolveParameters;
		LaserParticleSeed seed;
		seed.position[0] = x;
		seed.position[1] = y;
		const float windSpeed = config.windSpeedDipPerSecond * dpiScale * std::lerp(config.minimumWindSpeedFraction,
			config.maximumWindSpeedFraction, SampleParticleSeed(random ^ 0xC2B2AE35u));
		const float windAngle = windAngleDegrees * kPi / 180.0f;
		const float windX = std::cos(windAngle), windY = std::sin(windAngle);
		seed.velocity[0] = windX * windSpeed;
		seed.velocity[1] = windY * windSpeed;
		// 颜色只存来源色；白芯比例放在亮度字段，由粒子着色器在出生后很快褪掉。
		std::copy_n(color, 4, seed.color);
		seed.brightness = white;
		seed.radius = std::lerp(config.minimumRadiusDip, config.maximumRadiusDip,
			std::pow(SampleParticleSeed(random ^ 0xA511E9B3u), config.radiusDistributionExponent)) * dpiScale;
		// 阵风只跟出生位置有关（风向、法向基下的 DIP 坐标），取值范围 1 ± gustVariation。
		const float alongDip = (x * windX + y * windY) / dpiScale;
		const float acrossDip = (y * windX - x * windY) / dpiScale;
		const float gust = 1.0f + config.gustVariation * std::sin(0.019f * alongDip + 0.031f * acrossDip + 0.9f) *
			std::cos(0.027f * acrossDip - 0.011f * alongDip - 0.4f);
		seed.dragPerSecond = config.accelerationPerSecond * gust * std::lerp(1.0f - config.accelerationJitter,
			1.0f + config.accelerationJitter, SampleParticleSeed(random ^ 0xD3A2646Cu));
		// 飘散不做呼吸亮度，复用这几个种子字段把风场参数交给 PackLaserParticle：
		// 蜿蜒幅度、像素到 DIP 的系数、出生时散开的方向与距离。
		seed.breathingAmplitude = config.meanderDip * dpiScale;
		seed.breathingFrequencyHz = 1.0f / dpiScale;
		seed.breathingPhase = kTwoPi * SampleParticleSeed(random ^ 0x85EBCA77u);
		seed.angularSpeedRadians = std::lerp(config.minimumBurstDip, config.maximumBurstDip,
			SampleParticleSeed(random ^ 0x165667B1u)) * dpiScale;
		seed.releaseDelayMilliseconds = releaseDelayMilliseconds;
		seed.lifetimeMilliseconds = static_cast<uint32_t>(std::lerp(
			static_cast<float>(config.minimumLifetimeMilliseconds), static_cast<float>(config.maximumLifetimeMilliseconds),
			SampleParticleSeed(random ^ 0x27D4EB2Fu)));
		// 脏区按最坏位移计：顺风行程，加风场两个分量的最大摆幅（法向 3.2、顺风 0.32×2 倍幅度），再加散开距离。
		const float lifetimeSeconds = seed.lifetimeMilliseconds * 0.001f;
		seed.maximumTravelDistance = windSpeed * (lifetimeSeconds -
			(1.0f - std::exp(-seed.dragPerSecond * lifetimeSeconds)) / seed.dragPerSecond) +
			3.84f * seed.breathingAmplitude + seed.angularSpeedRadians;
		seed.randomSeed = random;
		seed.group = group;
		seed.kind = LaserParticleKind::Dissolve;
		seeds.push_back(seed);
	}

	void AppendLaserDissolveParticleSeed(std::vector<LaserParticleSeed>& seeds,
		const LaserDissolveFront& front, float x, float y, float tangentX, float tangentY,
		float radius, float coreRadius, float dpiScale, const float (&color)[4],
		uint32_t random, uint32_t group, float windAngleDegrees, bool point)
	{
		float offset = std::lerp(-radius, radius, SampleParticleSeed(random ^ 0x967A889Bu));
		if (point)
		{
			// sqrt 让出生位置按圆面积均匀分布；径向距离沿用笔迹的白芯取色。
			offset = radius * std::sqrt(SampleParticleSeed(random ^ 0x967A889Bu));
			const float angle = kTwoPi * SampleParticleSeed(random ^ 0x02E5BE93u);
			tangentX = std::cos(angle);
			tangentY = std::sin(angle);
		}
		const float white = 1.0f - std::clamp((std::abs(offset) - coreRadius) /
			(std::max)(radius - coreRadius, kMinimumPositiveValue), 0.0f, 1.0f);
		x -= tangentY * offset;
		y += tangentX * offset;
		AppendDriftParticleSeed(seeds, x, y, white, dpiScale, color, random, group, windAngleDegrees,
			static_cast<uint32_t>(std::ceil(LaserDissolveReleaseProgress(front, x, y) *
				kLaserDissolveParameters.durationMilliseconds)));
	}

	uint32_t AppendCanvasClearParticleSeeds(std::vector<LaserParticleSeed>& seeds,
		const uint32_t* premultipliedBgra, size_t rowPitchBytes, uint32_t width, uint32_t height,
		float dpiScale, float windAngleDegrees, uint32_t seedBase)
	{
		const auto& config = kCanvasClearParticleParameters;
		if (!premultipliedBgra || width == 0 || height == 0 || !IsFinitePositive(dpiScale) ||
			rowPitchBytes < static_cast<size_t>(width) * sizeof(uint32_t)) return 0;
		const auto rowAt = [&](uint32_t y)
		{
			return reinterpret_cast<const uint32_t*>(
				reinterpret_cast<const uint8_t*>(premultipliedBgra) + static_cast<size_t>(y) * rowPitchBytes);
		};
		// 粒子密度按 DIP 定，所以高 DPI 下不必逐像素看：每 step × step 个像素取一个代表，
		// 4K 两倍缩放时扫描量降到四分之一。笔迹在这个缩放下至少有几个像素宽，不会被漏掉。
		const uint32_t step = (std::max)(1u, static_cast<uint32_t>(dpiScale));
		// 第一遍只数有墨迹的取样点，据此把粒子数摊到整页：墨迹多时按比例变稀，不会只落在画面上半部。
		size_t inked = 0;
		for (uint32_t y = 0; y < height; y += step)
		{
			const uint32_t* row = rowAt(y);
			for (uint32_t x = 0; x < width; x += step)
				inked += (row[x] >> 24) >= config.minimumAlpha;
		}
		if (inked == 0) return 0;
		// 每个取样点代表 step² 个像素。
		const double wanted = (std::min)(static_cast<double>(config.maximumSeeds),
			static_cast<double>(inked) * step * step * config.seedsPerSquareDip /
			(static_cast<double>(dpiScale) * dpiScale));
		const auto threshold = static_cast<uint32_t>((std::min)(1.0, wanted / static_cast<double>(inked)) * 16777216.0);
		const size_t first = seeds.size();
		seeds.reserve(first + static_cast<size_t>(wanted) + 64);
		for (uint32_t y = 0; y < height; y += step)
		{
			const uint32_t* row = rowAt(y);
			for (uint32_t x = 0; x < width; x += step)
			{
				const uint32_t pixel = row[x];
				const uint32_t alpha = pixel >> 24;
				if (alpha < config.minimumAlpha) continue;
				const uint32_t random = HashParticleSeed((x ^ (y * 0x9E3779B9u)) + seedBase);
				if ((random & 0x00FFFFFFu) >= threshold) continue;
				if (seeds.size() - first >= config.maximumSeeds) return static_cast<uint32_t>(seeds.size() - first);
				// 画布是预乘 BGRA：还原成直通色，透明度随粒子保留（荧光笔等半透明墨迹）。
				const float inverse = 1.0f / static_cast<float>(alpha);
				const float color[4] = {
					(std::min)(1.0f, static_cast<float>((pixel >> 16) & 0xFFu) * inverse),
					(std::min)(1.0f, static_cast<float>((pixel >> 8) & 0xFFu) * inverse),
					(std::min)(1.0f, static_cast<float>(pixel & 0xFFu) * inverse),
					static_cast<float>(alpha) / 255.0f };
				// 墨迹当帧就清掉了，粒子大多立刻出现，少数稍晚，避免齐刷刷同时起飞。
				const float late = SampleParticleSeed(random ^ 0x7F4A7C15u);
				// 出生位置在取样格内随机，抽样不会留下网格痕迹。
				AppendDriftParticleSeed(seeds, static_cast<float>(x) + SampleParticleSeed(random ^ 0x967A889Bu) * step,
					static_cast<float>(y) + SampleParticleSeed(random ^ 0x02E5BE93u) * step, 0.0f, dpiScale, color, random,
					kCanvasClearParticleGroup, windAngleDegrees,
					static_cast<uint32_t>(config.releaseMilliseconds * late * late));
			}
		}
		return static_cast<uint32_t>(seeds.size() - first);
	}

	LaserParticleBatchEnvelope ConservativeLaserParticleBatchBounds(
		std::span<const LaserParticleSeed> seeds) noexcept
	{
		LaserParticleBatchEnvelope envelope;
		for (const auto& seed : seeds)
		{
			// 行程 + 粒子自身大小，四边另加 2px 抗锯齿边距。
			const double padding = static_cast<double>(seed.maximumTravelDistance) + 2.0 * seed.radius + 2.0;
			const double x = seed.position[0], y = seed.position[1];
			if (!std::isfinite(padding) || !std::isfinite(x) || !std::isfinite(y)) continue;
			UnionRectLocal(envelope.bounds, {
				SaturatingFloorToLong(x - padding), SaturatingFloorToLong(y - padding),
				SaturatingCeilToLong(x + padding), SaturatingCeilToLong(y + padding) });
			envelope.maximumLifetimeMilliseconds = (std::max)(envelope.maximumLifetimeMilliseconds,
				seed.releaseDelayMilliseconds + seed.lifetimeMilliseconds);
		}
		return envelope;
	}

	int64_t LaserParticleLifetimeDeadlineQpc(int64_t nowQpc,
		int64_t qpcFrequency, uint32_t lifetimeMilliseconds) noexcept
	{
		if (nowQpc < 0 || qpcFrequency <= 0 || lifetimeMilliseconds == 0) return nowQpc;
		const double ticks = std::ceil(static_cast<double>(lifetimeMilliseconds) *
			static_cast<double>(qpcFrequency) / 1000.0);
		const auto maximum = (std::numeric_limits<int64_t>::max)();
		if (ticks >= static_cast<double>(maximum - nowQpc)) return maximum;
		return nowQpc + static_cast<int64_t>(ticks);
	}

	bool LaserParticleActiveRing::Initialize() noexcept
	{
		Clear();
		if (!batches_) batches_.reset(new (std::nothrow) Batch[kLaserParticleCapacity]);
		return batches_ != nullptr;
	}

	void LaserParticleActiveRing::Clear() noexcept
	{
		first_ = batchCount_ = cursor_ = activeCount_ = 0;
		latestDissolve_ = kLaserParticleCapacity;
	}

	LaserParticleActiveRange LaserParticleActiveRing::Range() const noexcept
	{
		if (activeCount_ == 0) return {};
		return { (cursor_ + kLaserParticleCapacity - activeCount_) % kLaserParticleCapacity, activeCount_ };
	}

	void LaserParticleActiveRing::RemoveFront(uint32_t count) noexcept
	{
		activeCount_ -= count;
		while (count != 0)
		{
			auto& batch = batches_[first_];
			const uint32_t removed = (std::min)(count, batch.count);
			count -= removed;
			batch.count -= removed;
			if (batch.count == 0)
			{
				if (latestDissolve_ == first_) latestDissolve_ = kLaserParticleCapacity;
				first_ = (first_ + 1) % kLaserParticleCapacity;
				--batchCount_;
			}
		}
	}

	void LaserParticleActiveRing::Append(uint32_t count, uint64_t expiresMilliseconds, bool dissolve) noexcept
	{
		if (!batches_ || count == 0) return;
		count = (std::min)(count, kLaserParticleCapacity);
		const uint32_t freeCount = kLaserParticleCapacity - activeCount_;
		if (count > freeCount) RemoveFront(count - freeCount);
		const uint32_t batch = (first_ + batchCount_) % kLaserParticleCapacity;
		batches_[batch] = { count, expiresMilliseconds };
		if (dissolve) latestDissolve_ = batch;
		++batchCount_;
		activeCount_ += count;
		cursor_ = (cursor_ + count) % kLaserParticleCapacity;
	}

	void LaserParticleActiveRing::ExtendLatestDissolve(uint64_t expiresMilliseconds) noexcept
	{
		if (latestDissolve_ == kLaserParticleCapacity) return;
		auto& deadline = batches_[latestDissolve_].expiresMilliseconds;
		deadline = (std::max)(deadline, expiresMilliseconds);
	}

	void LaserParticleActiveRing::EndLatestDissolve(uint64_t nowMilliseconds) noexcept
	{
		if (latestDissolve_ == kLaserParticleCapacity) return;
		batches_[latestDissolve_].expiresMilliseconds = nowMilliseconds;
		latestDissolve_ = kLaserParticleCapacity;
	}

	LaserParticleActiveRange LaserParticleActiveRing::LatestDissolveRange() const noexcept
	{
		if (latestDissolve_ == kLaserParticleCapacity) return {};
		// 批次按写入顺序首尾相接，从活跃区间起点累加即可定位。
		uint32_t slot = Range().start;
		for (uint32_t i = 0; i < batchCount_; ++i)
		{
			const uint32_t batch = (first_ + i) % kLaserParticleCapacity;
			if (batch == latestDissolve_) return { slot, batches_[batch].count };
			slot = (slot + batches_[batch].count) % kLaserParticleCapacity;
		}
		return {};
	}

	void LaserParticleActiveRing::Expire(uint64_t nowMilliseconds) noexcept
	{
		// 中段先过期时保留连续区；两端过期的批次可以退出环并立即复用。
		while (batchCount_ != 0 && batches_[first_].expiresMilliseconds <= nowMilliseconds)
			RemoveFront(batches_[first_].count);
		while (batchCount_ != 0)
		{
			const uint32_t batch = (first_ + batchCount_ - 1) % kLaserParticleCapacity;
			const auto& last = batches_[batch];
			if (last.expiresMilliseconds > nowMilliseconds) break;
			if (latestDissolve_ == batch) latestDissolve_ = kLaserParticleCapacity;
			cursor_ = (cursor_ + kLaserParticleCapacity - last.count) % kLaserParticleCapacity;
			activeCount_ -= last.count;
			--batchCount_;
		}
	}

	void LaserParticleDirtyTracker::Add(RECT bounds, int64_t expiresQpc, bool dissolve) noexcept
	{
		if (dissolve) latestDissolve_ = nullptr;
		if (bounds.left >= bounds.right || bounds.top >= bounds.bottom ||
			expiresQpc <= 0) return;
		for (Region& region : regions_)
		{
			if (region.active) continue;
			region = { bounds, expiresQpc, true };
			if (dissolve) latestDissolve_ = &region;
			return;
		}

		// 极端高帧率下容量耗尽时合并最早到期槽，宁可扩大脏区也不能漏清残影。
		Region& fallback = *std::min_element(regions_.begin(), regions_.end(),
			[](const Region& left, const Region& right)
			{
				return left.expiresQpc < right.expiresQpc;
			});
		if (dissolve) latestDissolve_ = &fallback;
		fallback.shared = true;
		UnionRectLocal(fallback.bounds, bounds);
		fallback.expiresQpc = std::max(fallback.expiresQpc, expiresQpc);
		fallback.active = true;
	}

	void LaserParticleDirtyTracker::ExtendLatestDissolve(int64_t expiresQpc) noexcept
	{
		if (latestDissolve_)
			latestDissolve_->expiresQpc = (std::max)(latestDissolve_->expiresQpc, expiresQpc);
	}

	void LaserParticleDirtyTracker::EndLatestDissolve() noexcept
	{
		if (!latestDissolve_) return;
		// 合并槽包含其他批次，提前结束飘散时仍须保留它们的脏区。
		if (!latestDissolve_->shared) *latestDissolve_ = {};
		latestDissolve_ = nullptr;
	}

	LaserParticleDirtySnapshot LaserParticleDirtyTracker::Snapshot(
		int64_t nowQpc, int64_t latestDissolveQpc) noexcept
	{
		LaserParticleDirtySnapshot snapshot;
		for (Region& region : regions_)
		{
			if (!region.active) continue;
			const int64_t expiryCheckQpc = latestDissolveQpc > 0 && latestDissolve_ == &region
				? (std::min)(nowQpc, latestDissolveQpc) : nowQpc;
			if (region.expiresQpc <= expiryCheckQpc)
			{
				snapshot.expiredAny = true;
				if (latestDissolve_ == &region) latestDissolve_ = nullptr;
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
		latestDissolve_ = nullptr;
	}
}
