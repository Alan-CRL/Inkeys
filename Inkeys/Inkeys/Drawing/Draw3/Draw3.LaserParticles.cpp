module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
			config.maximumSpawnPerFrame > 0 && config.maximumSpawnPerFrame <= kLaserTrailPoolCapacity &&
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
		// 与 laserParticleCommon.hlsli 的 DriftWindField 相同：坐标取“风向, 法向”基下的 DIP。
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

	static uint32_t PackParticleColor(const float (&color)[4]) noexcept
	{
		return PackUnorm8(color[0]) | (PackUnorm8(color[1]) << 8) |
			(PackUnorm8(color[2]) << 16) | (PackUnorm8(color[3]) << 24);
	}

	static LaserGpuParticle PackParticleSeedFields(const LaserParticleSeed& seed, uint32_t bornMilliseconds) noexcept
	{
		LaserGpuParticle particle = {};
		particle.position[0] = seed.position[0];
		particle.position[1] = seed.position[1];
		particle.shapeParameters[0] = seed.radius;
		particle.shapeParameters[1] = seed.brightness;
		particle.color = PackParticleColor(seed.color);
		particle.lifetimeMilliseconds = seed.lifetimeMilliseconds;
		particle.releaseMilliseconds = bornMilliseconds + seed.releaseDelayMilliseconds;
		return particle;
	}

	LaserGpuParticle PackLaserTrailParticle(const LaserParticleSeed& seed, uint32_t bornMilliseconds) noexcept
	{
		auto particle = PackParticleSeedFields(seed, bornMilliseconds);
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
		return particle;
	}

	LaserGpuParticle PackLaserParticle(const LaserParticleSeed& seed, uint32_t bornMilliseconds,
		uint32_t dissolveBatch) noexcept
	{
		if (seed.kind == LaserParticleKind::Trail) return PackLaserTrailParticle(seed, bornMilliseconds);
		auto particle = PackParticleSeedFields(seed, bornMilliseconds);
		// 种子字段复用见 AppendReferenceDriftParticleSeed。凡是每粒固定不变的量都在这里算好，
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

	float LaserDissolveCrumbleEndProgress() noexcept
	{
		// 与 LaserDissolveEdgeCenter 同一公式取阈值上限，再加上单个像素淡出的半宽。
		const auto& config = kLaserDissolveParameters;
		const float latest = std::clamp(config.crumbleMidpoint + 0.5f * config.noiseAmplitude, 0.0f, 1.0f);
		return (std::min)(1.0f, 2.0f * config.edgeHalfWidth + (1.0f - 2.0f * config.edgeHalfWidth) * latest);
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

	uint32_t LaserDissolveSeedCount(double lengthDip, uint32_t maximumSeeds) noexcept
	{
		if (!std::isfinite(lengthDip) || lengthDip <= 0.0) return 0;
		return static_cast<uint32_t>((std::min)(lengthDip * kLaserDissolveParameters.seedsPerDip,
			static_cast<double>(maximumSeeds)));
	}

	void LaserDriftParticleContext::SetDpiScale(float scale) noexcept
	{
		dpiScale = scale;
		dipPerPixel = 1.0f / scale;
		windSpeedPxPerSecond = kLaserDissolveParameters.windSpeedDipPerSecond * scale;
		meanderPx = kLaserDissolveParameters.meanderDip * scale;
	}

	LaserDriftParticleContext MakeDriftParticleContext(float dpiScale, float windAngleDegrees) noexcept
	{
		const float windAngle = windAngleDegrees * kPi / 180.0f;
		LaserDriftParticleContext context = {};
		context.windX = std::cos(windAngle);
		context.windY = std::sin(windAngle);
		context.SetDpiScale(dpiScale);
		return context;
	}

	LaserDriftParticle MakeDriftParticle(const LaserDriftParticleContext& context, LaserParticleBatchEnvelope& envelope,
		float x, float y, float white, const float (&color)[4], uint32_t random, uint32_t group,
		uint32_t releaseDelayMilliseconds) noexcept
	{
		const auto& config = kLaserDissolveParameters;
		LaserGpuParticle particle = {};
		particle.position[0] = x;
		particle.position[1] = y;
		const float windSpeed = context.windSpeedPxPerSecond * std::lerp(config.minimumWindSpeedFraction,
			config.maximumWindSpeedFraction, SampleParticleSeed(random ^ 0xC2B2AE35u));
		particle.motion[0] = context.windX * windSpeed;
		particle.motion[1] = context.windY * windSpeed;
		particle.shapeParameters[0] = std::lerp(config.minimumRadiusDip, config.maximumRadiusDip,
			std::pow(SampleParticleSeed(random ^ 0xA511E9B3u), config.radiusDistributionExponent)) * context.dpiScale;
		particle.shapeParameters[1] = white;
		particle.color = PackParticleColor(color);

		// 保留阵风的除法及出生风场的速度归一化，避免风场接近零时放大舍入误差。
		const float alongDip = (x * context.windX + y * context.windY) / context.dpiScale;
		const float acrossDip = (y * context.windX - x * context.windY) / context.dpiScale;
		const float gust = 1.0f + config.gustVariation * std::sin(0.019f * alongDip + 0.031f * acrossDip + 0.9f) *
			std::cos(0.027f * acrossDip - 0.011f * alongDip - 0.4f);
		const float drag = config.accelerationPerSecond * gust * std::lerp(1.0f - config.accelerationJitter,
			1.0f + config.accelerationJitter, SampleParticleSeed(random ^ 0xD3A2646Cu));
		const float burstAngle = kTwoPi * SampleParticleSeed(random ^ 0x85EBCA77u);
		const float burstDistance = std::lerp(config.minimumBurstDip, config.maximumBurstDip,
			SampleParticleSeed(random ^ 0x165667B1u)) * context.dpiScale;
		particle.motionParameters[0] = (std::max)(drag, kMinimumPositiveValue);
		particle.motionParameters[1] = context.meanderPx;
		particle.motionParameters[2] = std::cos(burstAngle) * burstDistance;
		particle.motionParameters[3] = std::sin(burstAngle) * burstDistance;
		const float speed = (std::max)(std::hypot(particle.motion[0], particle.motion[1]), kMinimumPositiveValue);
		const float windX = particle.motion[0] / speed, windY = particle.motion[1] / speed;
		DriftWindField((x * windX + y * windY) * context.dipPerPixel,
			(y * windX - x * windY) * context.dipPerPixel, particle.shapeParameters[2], particle.shapeParameters[3]);

		particle.releaseMilliseconds = releaseDelayMilliseconds;
		particle.lifetimeMilliseconds = static_cast<uint32_t>(std::lerp(
			static_cast<float>(config.minimumLifetimeMilliseconds), static_cast<float>(config.maximumLifetimeMilliseconds),
			SampleParticleSeed(random ^ 0x27D4EB2Fu)));
		particle.kindAndBatch = static_cast<uint32_t>(LaserParticleKind::Dissolve);
		// 用未夹紧的加速参数计算旧包络中的最大位移，避免改变脏区的像素取整或到期时刻。
		const float lifetimeSeconds = particle.lifetimeMilliseconds * 0.001f;
		const float maximumTravelDistance = windSpeed * (lifetimeSeconds -
			(1.0f - std::exp(-drag * lifetimeSeconds)) / drag) + 3.84f * context.meanderPx + burstDistance;
		const double padding = static_cast<double>(maximumTravelDistance) + 2.0 * particle.shapeParameters[0] + 2.0;
		if (std::isfinite(padding) && std::isfinite(x) && std::isfinite(y))
		{
			UnionRectLocal(envelope.bounds, {
				SaturatingFloorToLong(x - padding), SaturatingFloorToLong(y - padding),
				SaturatingCeilToLong(x + padding), SaturatingCeilToLong(y + padding) });
			envelope.maximumLifetimeMilliseconds = (std::max)(envelope.maximumLifetimeMilliseconds,
				releaseDelayMilliseconds + particle.lifetimeMilliseconds);
		}
		return { particle, group };
	}

	// 旧飘散种子计算保留为自检的参考实现。
	void AppendReferenceDriftParticleSeed(std::vector<LaserParticleSeed>& seeds, float x, float y,
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

	template <typename Emit>
	static void EmitLaserDissolveParticle(const LaserDissolveFront& front, float x, float y,
		float tangentX, float tangentY, float radius, float coreRadius, uint32_t random, bool isDot, const Emit& emit)
	{
		float offset = std::lerp(-radius, radius, SampleParticleSeed(random ^ 0x967A889Bu));
		if (isDot)
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
		emit(x, y, white, static_cast<uint32_t>(std::ceil(LaserDissolveReleaseProgress(front, x, y) *
			kLaserDissolveParameters.durationMilliseconds)));
	}

	void AppendLaserDissolveParticle(std::vector<LaserDriftParticle>& particles, LaserParticleBatchEnvelope& envelope,
		const LaserDriftParticleContext& context, const LaserDissolveFront& front,
		float x, float y, float tangentX, float tangentY, float radius, float coreRadius, const float (&color)[4],
		uint32_t random, uint32_t group, bool isDot)
	{
		EmitLaserDissolveParticle(front, x, y, tangentX, tangentY, radius, coreRadius, random, isDot,
			[&](float bornX, float bornY, float white, uint32_t delay)
			{
				particles.push_back(MakeDriftParticle(context, envelope, bornX, bornY, white, color, random, group, delay));
			});
	}

	void AppendReferenceLaserDissolveParticleSeed(std::vector<LaserParticleSeed>& seeds,
		const LaserDissolveFront& front, float x, float y, float tangentX, float tangentY,
		float radius, float coreRadius, float dpiScale, const float (&color)[4],
		uint32_t random, uint32_t group, float windAngleDegrees, bool isDot)
	{
		EmitLaserDissolveParticle(front, x, y, tangentX, tangentY, radius, coreRadius, random, isDot,
			[&](float bornX, float bornY, float white, uint32_t delay)
			{
				AppendReferenceDriftParticleSeed(seeds, bornX, bornY, white, dpiScale, color, random, group, windAngleDegrees, delay);
			});
	}

	template <typename Particle, typename Emit>
	static uint32_t SampleCanvasClearParticles(std::vector<Particle>& particles,
		const uint32_t* premultipliedBgra, size_t rowPitchBytes, uint32_t width, uint32_t height,
		float dpiScale, uint32_t seedBase, const LaserDissolveFront& front, uint32_t maximumSeeds,
		RECT* inkBounds, const Emit& emit)
	{
		if (inkBounds) *inkBounds = {};
		const auto& config = kCanvasClearParticleParameters;
		if (!premultipliedBgra || width == 0 || height == 0 || !IsFinitePositive(dpiScale) ||
			rowPitchBytes < static_cast<size_t>(width) * sizeof(uint32_t)) return 0;
		const auto rowAt = [&](uint32_t y)
		{
			return reinterpret_cast<const uint32_t*>(
				reinterpret_cast<const uint8_t*>(premultipliedBgra) + static_cast<size_t>(y) * rowPitchBytes);
		};
		// 粒子密度按 DIP 定，所以高 DPI 下不必逐像素看：每 step × step 个像素取一个代表，
		// 4K 两倍缩放时扫描量降到四分之一，但 2×2 代表像素可能漏采极细的线。
		// 碎裂范围来自历史查询，不受此抽样影响。
		const uint32_t step = (std::max)(1u, static_cast<uint32_t>(dpiScale));
		// 第一遍只数有墨迹的取样点，据此把粒子数摊到整页：墨迹多时按比例变稀，不会只落在画面上半部。
		size_t inked = 0;
		uint32_t minimumX = width, minimumY = height, maximumX = 0, maximumY = 0;
		for (uint32_t y = 0; y < height; y += step)
		{
			const uint32_t* row = rowAt(y);
			for (uint32_t x = 0; x < width; x += step)
			{
				if ((row[x] >> 24) < config.minimumAlpha) continue;
				++inked;
				minimumX = (std::min)(minimumX, x);
				maximumX = (std::max)(maximumX, x);
				minimumY = (std::min)(minimumY, y);
				maximumY = (std::max)(maximumY, y);
			}
		}
		if (inked == 0) return 0;
		// 四周各放宽一个步长，盖住取样点之间和抗锯齿边缘上没被数到的像素。
		if (inkBounds)
			*inkBounds = { static_cast<LONG>(minimumX) - static_cast<LONG>(step),
				static_cast<LONG>(minimumY) - static_cast<LONG>(step),
				static_cast<LONG>(maximumX) + 2 * static_cast<LONG>(step),
				static_cast<LONG>(maximumY) + 2 * static_cast<LONG>(step) };
		// 每个取样点代表 step² 个像素。
		const double wanted = (std::min)(static_cast<double>(maximumSeeds),
			static_cast<double>(inked) * step * step * config.seedsPerSquareDip /
			(static_cast<double>(dpiScale) * dpiScale));
		const auto threshold = static_cast<uint32_t>((std::min)(1.0, wanted / static_cast<double>(inked)) * 16777216.0);
		const size_t first = particles.size();
		particles.reserve(first + static_cast<size_t>(wanted) + 64);
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
				if (particles.size() - first >= maximumSeeds) return static_cast<uint32_t>(particles.size() - first);
				// 画布是预乘 BGRA：还原成直通色，透明度随粒子保留（荧光笔等半透明墨迹）。
				const float inverse = 1.0f / static_cast<float>(alpha);
				const float color[4] = {
					(std::min)(1.0f, static_cast<float>((pixel >> 16) & 0xFFu) * inverse),
					(std::min)(1.0f, static_cast<float>((pixel >> 8) & 0xFFu) * inverse),
					(std::min)(1.0f, static_cast<float>(pixel & 0xFFu) * inverse),
					static_cast<float>(alpha) / 255.0f };
				// 出生位置在取样格内随机，抽样不会留下网格痕迹。
				const float bornX = static_cast<float>(x) + SampleParticleSeed(random ^ 0x967A889Bu) * step;
				const float bornY = static_cast<float>(y) + SampleParticleSeed(random ^ 0x02E5BE93u) * step;
				// 清空前的画布快照按同一张遮罩逐像素碎掉：粒子在自己那个像素消失的时刻出现，
				// 画面由墨迹连续过渡到粒子，不会先空一下再冒出点来。
				emit(bornX, bornY, color, random,
					static_cast<uint32_t>(std::ceil(LaserDissolveReleaseProgress(front, bornX, bornY) *
						kLaserDissolveParameters.durationMilliseconds)));
			}
		}
		return static_cast<uint32_t>(particles.size() - first);
	}

	uint32_t AppendCanvasClearParticleSeeds(std::vector<LaserDriftParticle>& particles, LaserParticleBatchEnvelope& envelope,
		const uint32_t* premultipliedBgra, size_t rowPitchBytes, uint32_t width, uint32_t height,
		const LaserDriftParticleContext& context, uint32_t seedBase,
		const LaserDissolveFront& front, uint32_t maximumSeeds, RECT* inkBounds)
	{
		return SampleCanvasClearParticles(particles, premultipliedBgra, rowPitchBytes, width, height,
			context.dpiScale, seedBase, front, maximumSeeds, inkBounds,
			[&](float x, float y, const float (&color)[4], uint32_t random, uint32_t delay)
			{
				particles.push_back(MakeDriftParticle(context, envelope, x, y, 0.0f, color, random, kCanvasClearParticleGroup, delay));
			});
	}

	uint32_t AppendReferenceCanvasClearParticleSeeds(std::vector<LaserParticleSeed>& seeds,
		const uint32_t* premultipliedBgra, size_t rowPitchBytes, uint32_t width, uint32_t height,
		float dpiScale, float windAngleDegrees, uint32_t seedBase,
		const LaserDissolveFront& front, uint32_t maximumSeeds, RECT* inkBounds)
	{
		return SampleCanvasClearParticles(seeds, premultipliedBgra, rowPitchBytes, width, height,
			dpiScale, seedBase, front, maximumSeeds, inkBounds,
			[&](float x, float y, const float (&color)[4], uint32_t random, uint32_t delay)
			{
				AppendReferenceDriftParticleSeed(seeds, x, y, 0.0f, dpiScale, color, random,
					kCanvasClearParticleGroup, windAngleDegrees, delay);
			});
	}

	bool MatchesReferenceDriftBatch(std::span<const LaserDriftParticle> particles,
		std::span<const LaserParticleSeed> seeds, const LaserParticleBatchEnvelope& envelope) noexcept
	{
		if (particles.size() != seeds.size()) return false;
		constexpr uint32_t born = 1479, batch = 19;
		for (size_t i = 0; i < seeds.size(); ++i)
		{
			auto actual = particles[i].particle;
			if (particles[i].group != seeds[i].group ||
				actual.kindAndBatch != static_cast<uint32_t>(LaserParticleKind::Dissolve)) return false;
			actual.releaseMilliseconds += born;
			actual.kindAndBatch |= batch << 2;
			const auto expected = PackLaserParticle(seeds[i], born, batch);
			// GPU 记录无填充，逐字节断言也覆盖每个浮点字段的位级一致性。
			if (std::memcmp(&actual, &expected, sizeof(actual)) != 0) return false;
		}
		const auto expected = ConservativeLaserParticleBatchBounds(seeds);
		return EqualRect(&expected.bounds, &envelope.bounds) &&
			expected.maximumLifetimeMilliseconds == envelope.maximumLifetimeMilliseconds;
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

	LaserParticleBatchEnvelope ConservativeCanvasClearParticleBounds(RECT pixelRect, float dpiScale) noexcept
	{
		if (pixelRect.left >= pixelRect.right || pixelRect.top >= pixelRect.bottom) return {};
		const auto& config = kLaserDissolveParameters;
		const double lifetime = config.maximumLifetimeMilliseconds * 0.001;
		const double acceleration = static_cast<double>(config.accelerationPerSecond) *
			(1.0 + config.gustVariation) * (1.0 + config.accelerationJitter);
		// 风速、寿命和加速同时取上限，再覆盖风场差、径向散开与粒子绘制半径。
		static const double maximumTravelDip = config.windSpeedDipPerSecond * config.maximumWindSpeedFraction *
			(lifetime - (1.0 - std::exp(-acceleration * lifetime)) / acceleration) +
			3.84 * config.meanderDip + config.maximumBurstDip + 2.0 * config.maximumRadiusDip;
		const double padding = dpiScale * maximumTravelDip + 2.0;
		return {
			{ SaturatingFloorToLong(pixelRect.left - padding), SaturatingFloorToLong(pixelRect.top - padding),
				SaturatingCeilToLong(pixelRect.right + padding), SaturatingCeilToLong(pixelRect.bottom + padding) },
			config.maximumLifetimeMilliseconds + static_cast<uint32_t>(
				std::ceil(LaserDissolveCrumbleEndProgress() * config.durationMilliseconds)) };
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

	bool LaserParticleActiveRing::Initialize(uint32_t capacity) noexcept
	{
		if (capacity == 0 || capacity > kLaserParticleMaximumCapacity ||
			(capacity & (capacity - 1u)) != 0) return false;
		if (!batches_) batches_.reset(new (std::nothrow) Batch[kLaserParticleMaximumBatches]);
		if (!batches_) return false;
		capacity_ = capacity;
		capacityMask_ = capacity - 1u;
		Clear();
		return true;
	}

	void LaserParticleActiveRing::Clear() noexcept
	{
		first_ = batchCount_ = cursor_ = activeCount_ = 0;
		latestDissolve_ = kLaserParticleMaximumBatches;
	}

	LaserParticleActiveRange LaserParticleActiveRing::Range() const noexcept
	{
		if (activeCount_ == 0) return {};
		return { (cursor_ + capacity_ - activeCount_) & capacityMask_, activeCount_ };
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
				if (latestDissolve_ == first_) latestDissolve_ = kLaserParticleMaximumBatches;
				first_ = (first_ + 1) % kLaserParticleMaximumBatches;
				--batchCount_;
			}
		}
	}

	LaserParticleBatchId LaserParticleActiveRing::Append(uint32_t count, uint64_t expiresMilliseconds, bool dissolve) noexcept
	{
		if (!batches_ || count == 0) return {};
		count = (std::min)(count, capacity_);
		// 批次表满时先整批淘汰最旧记录，即使槽位还有余量。
		if (batchCount_ == kLaserParticleMaximumBatches) RemoveFront(batches_[first_].count);
		const uint32_t freeCount = capacity_ - activeCount_;
		if (count > freeCount) RemoveFront(count - freeCount);
		const uint32_t batch = (first_ + batchCount_) % kLaserParticleMaximumBatches;
		batches_[batch] = { count, ++generation_, expiresMilliseconds };
		if (dissolve) latestDissolve_ = batch;
		++batchCount_;
		activeCount_ += count;
		cursor_ = (cursor_ + count) & capacityMask_;
		return { batch, generation_ };
	}

	bool LaserParticleActiveRing::TrimTail(LaserParticleBatchId id, uint32_t count) noexcept
	{
		if (batchCount_ == 0 || id.index != (first_ + batchCount_ - 1) % kLaserParticleMaximumBatches) return false;
		auto& batch = batches_[id.index];
		if (batch.generation != id.generation || count >= batch.count) return false;

		// 只回收尾部，保持已在环内的其余槽位和活跃区间连续。
		const uint32_t removed = batch.count - count;
		cursor_ = (cursor_ + capacity_ - removed) & capacityMask_;
		activeCount_ -= removed;
		batch.count = count;
		if (count == 0)
		{
			--batchCount_;
			if (latestDissolve_ == id.index) latestDissolve_ = kLaserParticleMaximumBatches;
		}
		return true;
	}

	uint64_t LaserParticleActiveRing::EndMilliseconds() const noexcept
	{
		uint64_t end = 0;
		for (uint32_t i = 0; i < batchCount_; ++i)
			end = (std::max)(end, batches_[(first_ + i) % kLaserParticleMaximumBatches].expiresMilliseconds);
		return end;
	}

	void LaserParticleActiveRing::ExtendLatestDissolve(uint64_t expiresMilliseconds) noexcept
	{
		if (latestDissolve_ == kLaserParticleMaximumBatches) return;
		auto& deadline = batches_[latestDissolve_].expiresMilliseconds;
		deadline = (std::max)(deadline, expiresMilliseconds);
	}

	void LaserParticleActiveRing::EndLatestDissolve(uint64_t nowMilliseconds) noexcept
	{
		if (latestDissolve_ == kLaserParticleMaximumBatches) return;
		batches_[latestDissolve_].expiresMilliseconds = nowMilliseconds;
		latestDissolve_ = kLaserParticleMaximumBatches;
	}

	LaserParticleActiveRange LaserParticleActiveRing::LatestDissolveRange() const noexcept
	{
		if (latestDissolve_ == kLaserParticleMaximumBatches) return {};
		// 批次按写入顺序首尾相接，从活跃区间起点累加即可定位。
		uint32_t slot = Range().start;
		for (uint32_t i = 0; i < batchCount_; ++i)
		{
			const uint32_t batch = (first_ + i) % kLaserParticleMaximumBatches;
			if (batch == latestDissolve_) return { slot, batches_[batch].count };
			slot = (slot + batches_[batch].count) & capacityMask_;
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
			const uint32_t batch = (first_ + batchCount_ - 1) % kLaserParticleMaximumBatches;
			const auto& last = batches_[batch];
			if (last.expiresMilliseconds > nowMilliseconds) break;
			if (latestDissolve_ == batch) latestDissolve_ = kLaserParticleMaximumBatches;
			cursor_ = (cursor_ + capacity_ - last.count) & capacityMask_;
			activeCount_ -= last.count;
			--batchCount_;
		}
	}

	void LaserParticleDirtyTracker::Add(RECT bounds, int64_t expiresQpc, Kind kind) noexcept
	{
		const bool dissolve = kind == Kind::LaserDissolve;
		if (dissolve) latestDissolve_ = nullptr;
		if (bounds.left >= bounds.right || bounds.top >= bounds.bottom ||
			expiresQpc <= 0) return;
		auto& regions = kind == Kind::Other ? regions_ : driftRegions_;
		for (Region& region : regions)
		{
			if (region.active) continue;
			region = { bounds, expiresQpc, true };
			if (dissolve) latestDissolve_ = &region;
			return;
		}

		// 极端高帧率下容量耗尽时合并最早到期槽，宁可扩大脏区也不能漏清残影。
		Region& fallback = *std::min_element(regions.begin(), regions.end(),
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

	void LaserParticleDirtyTracker::TrackClearSpawn(RECT bounds, int64_t expiresQpc) noexcept
	{
		// GPU 只回读最近一次成功派发，独占一项以便零粒时精确撤销。
		if (latestClearSpawn_.active)
			Add(latestClearSpawn_.bounds, latestClearSpawn_.expiresQpc, Kind::ClearDrift);
		latestClearSpawn_ = { bounds, expiresQpc, true };
	}

	RECT LaserParticleDirtyTracker::RevokeClearSpawn() noexcept
	{
		const RECT bounds = latestClearSpawn_.bounds;
		latestClearSpawn_ = {};
		return bounds;
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
		for (const auto regions : { std::span<Region>(regions_), std::span<Region>(driftRegions_),
			std::span<Region>(&latestClearSpawn_, 1) })
		{
			for (Region& region : regions)
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
		}
		return snapshot;
	}

	RECT LaserParticleDirtyTracker::ClearDrift() noexcept
	{
		RECT bounds = RevokeClearSpawn();
		for (Region& region : driftRegions_)
		{
			if (region.active) UnionRectLocal(bounds, region.bounds);
			region = {};
		}
		latestDissolve_ = nullptr;
		return bounds;
	}

	void LaserParticleDirtyTracker::Clear() noexcept
	{
		regions_ = {};
		driftRegions_ = {};
		latestClearSpawn_ = {};
		latestDissolve_ = nullptr;
	}
}
