#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>
#include <windows.h>

import draw3.renderer;
import draw3.ink_prediction;

namespace
{
	bool NearlyEqual(float left, float right, float epsilon = 0.001f)
	{
		return std::abs(left - right) <= epsilon;
	}

	bool SamePrimitive(const draw3::HighlighterPrimitive& left,
		const draw3::HighlighterPrimitive& right)
	{
		return NearlyEqual(left.p1.x, right.p1.x) && NearlyEqual(left.p1.y, right.p1.y) &&
			NearlyEqual(left.p2.x, right.p2.x) && NearlyEqual(left.p2.y, right.p2.y) &&
			NearlyEqual(left.halfSize.x, right.halfSize.x) &&
			NearlyEqual(left.halfSize.y, right.halfSize.y);
	}

	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED highlighter line " << line << ": " << expression << std::endl;
	}

#define HIGHLIGHTER_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)
}

int RunHighlighterGeometryTests()
{
	int failures = 0;
	const draw3::StrokeModelConfiguration defaultConfiguration =
		draw3::CreateStrokeModelConfiguration(96);
	HIGHLIGHTER_CHECK(!defaultConfiguration.retainPredictionOnUp);
	HIGHLIGHTER_CHECK(defaultConfiguration.dpiScale == 1.0f);
	HIGHLIGHTER_CHECK(defaultConfiguration.laserParticlesEnabled);
	HIGHLIGHTER_CHECK(defaultConfiguration.laserHoldDurationSeconds == 3.0);
	HIGHLIGHTER_CHECK(draw3::CreateStrokeModelConfiguration(192).dpiScale == 2.0f);
	HIGHLIGHTER_CHECK(sizeof(draw3::LaserDot) == 16);
	HIGHLIGHTER_CHECK(sizeof(draw3::LaserStyleConstants) == 112);
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::kLaserSolidDiameterAt96Dpi, 5.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserSolidRadius(), 2.5f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserCoreRadius(
		draw3::LaserSolidRadius()) * 2.0f, 5.0f / 3.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserDiffuseExtent(), 5.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserVisualRadius(
		draw3::LaserSolidRadius()), 7.5f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserVisualRadius(1.625f) - 1.625f, 5.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserVisualRadius(3.5f) - 3.5f, 5.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserSolidRadius(2.0f), 5.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserDiffuseExtent(2.0f), 10.0f));

	draw3::LaserTrailLifecycle laserLifecycle;
	draw3::BeginLaserContact(laserLifecycle);
	draw3::BeginLaserContact(laserLifecycle);
	HIGHLIGHTER_CHECK(laserLifecycle.activeContactCount == 2);
	draw3::EndLaserContact(laserLifecycle, 1000);
	HIGHLIGHTER_CHECK(laserLifecycle.phase == draw3::LaserTrailPhase::Active);
	HIGHLIGHTER_CHECK(!draw3::ShouldBakeLaserBatch(laserLifecycle, 2));
	draw3::EndLaserContact(laserLifecycle, 1000);
	HIGHLIGHTER_CHECK(laserLifecycle.phase == draw3::LaserTrailPhase::Hold);
	draw3::RequireLaserMinimumHold(laserLifecycle, 3.0);
	HIGHLIGHTER_CHECK(draw3::EffectiveLaserHoldDurationSeconds(
		laserLifecycle, 0.25) == 3.0);
	HIGHLIGHTER_CHECK(draw3::EffectiveLaserHoldDurationSeconds(
		laserLifecycle, 5.0) == 5.0);
	HIGHLIGHTER_CHECK(draw3::ShouldBakeLaserBatch(laserLifecycle, 2));
	HIGHLIGHTER_CHECK(!draw3::ShouldBakeLaserBatch(laserLifecycle, 0));
	HIGHLIGHTER_CHECK(draw3::ShouldCompositeLaserLayer(false, 1));
	HIGHLIGHTER_CHECK(!draw3::ShouldCompositeLaserLayer(true, 1));
	HIGHLIGHTER_CHECK(!draw3::ShouldCompositeLaserLayer(false, 0));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::EvaluateLaserTrailOpacity(
		laserLifecycle, 3999, 1000, 3.0), 1.0f));
	const float halfFadeOpacity = draw3::EvaluateLaserTrailOpacity(
		laserLifecycle, 4400, 1000, 3.0);
	HIGHLIGHTER_CHECK(laserLifecycle.phase == draw3::LaserTrailPhase::Fade);
	HIGHLIGHTER_CHECK(NearlyEqual(halfFadeOpacity, 0.5f, 0.01f));
	draw3::BeginLaserContact(laserLifecycle);
	HIGHLIGHTER_CHECK(laserLifecycle.phase == draw3::LaserTrailPhase::Active);
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::EvaluateLaserTrailOpacity(
		laserLifecycle, 4500, 1000, 3.0), 1.0f));
	draw3::EndLaserContact(laserLifecycle, 4500);
	laserLifecycle.minimumHoldDurationSeconds = 0.0;
	HIGHLIGHTER_CHECK(draw3::EvaluateLaserTrailOpacity(
		laserLifecycle, 5301, 1000, 0.0) == 0.0f);
	HIGHLIGHTER_CHECK(laserLifecycle.phase == draw3::LaserTrailPhase::Inactive);

	const draw3::LaserParticleConfig particleConfiguration;
	HIGHLIGHTER_CHECK(draw3::IsValidLaserParticleConfig(particleConfiguration));
	draw3::LaserParticleConfig invalidParticleConfiguration = particleConfiguration;
	invalidParticleConfiguration.maximumSpawnPerFrame =
		draw3::kLaserParticleCapacity + 1;
	HIGHLIGHTER_CHECK(!draw3::IsValidLaserParticleConfig(
		invalidParticleConfiguration));
	invalidParticleConfiguration = particleConfiguration;
	invalidParticleConfiguration.minimumLifetimeSeconds = 1.1f;
	HIGHLIGHTER_CHECK(!draw3::IsValidLaserParticleConfig(
		invalidParticleConfiguration));
	invalidParticleConfiguration = particleConfiguration;
	invalidParticleConfiguration.endRadiusScale = 0.0f;
	HIGHLIGHTER_CHECK(!draw3::IsValidLaserParticleConfig(
		invalidParticleConfiguration));
	invalidParticleConfiguration = particleConfiguration;
	invalidParticleConfiguration.sizeTravelCorrelation = 1.1f;
	HIGHLIGHTER_CHECK(!draw3::IsValidLaserParticleConfig(
		invalidParticleConfiguration));
	HIGHLIGHTER_CHECK(draw3::kLaserParticleCapacity == 2048);
	HIGHLIGHTER_CHECK(sizeof(draw3::LaserGpuParticle) == 128);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, position) == 0);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, velocity) == 8);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, ageSeconds) == 16);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, traveledDistance) == 24);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, baseRadius) == 32);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, opacity) == 40);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, currentBrightness) == 48);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, breathingRampSeconds) == 64);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, seed) == 68);
	HIGHLIGHTER_CHECK(offsetof(draw3::LaserGpuParticle, padding) == 76);
HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.idleEmissionRatePerSecond, 6.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.motionEmissionSpacingDip, 1.8f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.maximumEmissionRatePerSecond, 90.0f));
		HIGHLIGHTER_CHECK(particleConfiguration.maximumSpawnPerFrame == 120);
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.minimumLifetimeSeconds, 0.55f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.maximumLifetimeSeconds, 0.75f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.minimumLaunchSpeedDipPerSecond, 18.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.maximumLaunchSpeedDipPerSecond, 40.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.maximumDeflectionAngleDegrees, 25.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.shrinkStartTravelRatio, 0.10f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.endRadiusScale, 0.20f));
		HIGHLIGHTER_CHECK(NearlyEqual(particleConfiguration.minimumRadiusDip, 0.28f));
		HIGHLIGHTER_CHECK(NearlyEqual(particleConfiguration.maximumRadiusDip, 1.4f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.sizeDistributionExponent, 2.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.sizeBrightnessCorrelation, 0.72f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.sizeTravelCorrelation, 0.30f));
		HIGHLIGHTER_CHECK(NearlyEqual(particleConfiguration.glowRadiusScale, 2.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(particleConfiguration.glowRed, 1.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(particleConfiguration.glowGreen, 0.32f));
		HIGHLIGHTER_CHECK(NearlyEqual(particleConfiguration.glowBlue, 0.40f));
		HIGHLIGHTER_CHECK(NearlyEqual(particleConfiguration.glowAlpha, 0.18f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.coreColorWhiteMix, 0.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.coreColorWhiteMixJitter, 0.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			particleConfiguration.minimumBrightness, 0.42f));

	// 尺寸采用连续偏小分布；同一随机输入下，小粒子略快且较暗。
	const draw3::LaserParticleBirthSample smallLayerBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			0.0f, 0.5f, 0.5f, 0.5f, particleConfiguration);
	const draw3::LaserParticleBirthSample largeLayerBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			1.0f, 0.5f, 0.5f, 0.5f, particleConfiguration);
	const draw3::LaserParticleBirthSample middleLayerBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			0.5f, 0.5f, 0.5f, 0.5f, particleConfiguration);
	const draw3::LaserParticleBirthSample maximumTravelBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			0.0f, 1.0f, 1.0f, 1.0f, particleConfiguration);
	const draw3::LaserParticleBirthSample minimumTravelBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			1.0f, 0.0f, 0.0f, 0.0f, particleConfiguration);
	const draw3::LaserParticleBirthSample maximumBrightnessBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			1.0f, 0.5f, 0.5f, 1.0f, particleConfiguration);
	HIGHLIGHTER_CHECK(smallLayerBirthSample.valid);
	HIGHLIGHTER_CHECK(largeLayerBirthSample.valid);
	HIGHLIGHTER_CHECK(NearlyEqual(
		smallLayerBirthSample.baseRadiusDip, 0.28f));
HIGHLIGHTER_CHECK(NearlyEqual(
			largeLayerBirthSample.baseRadiusDip, 1.4f));
		HIGHLIGHTER_CHECK(middleLayerBirthSample.baseRadiusDip < 0.60f);
		HIGHLIGHTER_CHECK(smallLayerBirthSample.launchSpeedDipPerSecond >
			largeLayerBirthSample.launchSpeedDipPerSecond);
		HIGHLIGHTER_CHECK(smallLayerBirthSample.maximumTravelDistanceDip >
			largeLayerBirthSample.maximumTravelDistanceDip);
		HIGHLIGHTER_CHECK(smallLayerBirthSample.baseBrightness <
			largeLayerBirthSample.baseBrightness);
		HIGHLIGHTER_CHECK(NearlyEqual(
			maximumTravelBirthSample.launchSpeedDipPerSecond, 40.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			maximumTravelBirthSample.lifetimeSeconds, 0.75f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			maximumTravelBirthSample.maximumTravelDistanceDip, 15.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			minimumTravelBirthSample.launchSpeedDipPerSecond, 18.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			minimumTravelBirthSample.lifetimeSeconds, 0.55f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			minimumTravelBirthSample.maximumTravelDistanceDip, 4.95f));
	HIGHLIGHTER_CHECK(NearlyEqual(
		maximumBrightnessBirthSample.baseBrightness, 1.0f));
	const draw3::LaserParticleBirthSample sameSizeSlowBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			0.25f, 0.0f, 0.5f, 0.5f, particleConfiguration);
	const draw3::LaserParticleBirthSample sameSizeFastBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			0.25f, 1.0f, 0.5f, 0.5f, particleConfiguration);
	const draw3::LaserParticleBirthSample sameSizeDimBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			0.25f, 0.5f, 0.5f, 0.0f, particleConfiguration);
	const draw3::LaserParticleBirthSample sameSizeBrightBirthSample =
		draw3::ResolveLaserParticleBirthSample(
			0.25f, 0.5f, 0.5f, 1.0f, particleConfiguration);
	HIGHLIGHTER_CHECK(sameSizeFastBirthSample.launchSpeedDipPerSecond >
		sameSizeSlowBirthSample.launchSpeedDipPerSecond);
	HIGHLIGHTER_CHECK(sameSizeBrightBirthSample.baseBrightness >
		sameSizeDimBirthSample.baseBrightness);

// Down 帧 dt 为零，只建立时间发射基线，不产生旧式起笔爆发。
		draw3::LaserParticleEmissionSchedule emission =
			draw3::ScheduleLaserParticleEmission(
				0.0f, 0.0f, 0.0f, 120, particleConfiguration);
		HIGHLIGHTER_CHECK(emission.count == 0);
		float emissionFraction = 0.0f;
		for (int frame = 0; frame < 4; ++frame)
		{
			emission = draw3::ScheduleLaserParticleEmission(
				1.0f / 30.0f, 0.0f, emissionFraction, 120, particleConfiguration);
			HIGHLIGHTER_CHECK(emission.count == 0);
			emissionFraction = emission.fractionalParticles;
		}
		emission = draw3::ScheduleLaserParticleEmission(
			1.0f / 30.0f, 0.0f, emissionFraction, 120, particleConfiguration);
		HIGHLIGHTER_CHECK(emission.count == 1);
		HIGHLIGHTER_CHECK(NearlyEqual(emission.fractionalParticles, 0.0f));
		emission = draw3::ScheduleLaserParticleEmission(
			1.0f / 60.0f, 100.0f, 0.0f, 120, particleConfiguration);
		HIGHLIGHTER_CHECK(NearlyEqual(emission.emissionRatePerSecond,
			6.0f + 100.0f / 1.8f));
		emission = draw3::ScheduleLaserParticleEmission(
			1.0f / 60.0f, 1000.0f, 0.0f, 120, particleConfiguration);
		HIGHLIGHTER_CHECK(NearlyEqual(emission.emissionRatePerSecond, 90.0f));
		emission = draw3::ScheduleLaserParticleEmission(
			1.0f / 60.0f, -100.0f, 0.0f, 120, particleConfiguration);
		HIGHLIGHTER_CHECK(NearlyEqual(emission.emissionRatePerSecond, 6.0f));
		HIGHLIGHTER_CHECK(emission.count == 0); // 无真实移动时只保留静止基线。
		emission = draw3::ScheduleLaserParticleEmission(
			0.5f, 120.0f, 0.0f, 120, particleConfiguration);
		HIGHLIGHTER_CHECK(NearlyEqual(emission.emissionRatePerSecond,
			6.0f + 120.0f / 1.8f));
		HIGHLIGHTER_CHECK(emission.count == 2); // 卡顿只积分一个 1/30 秒安全步。
		emission = draw3::ScheduleLaserParticleEmission(
			1.0f / 30.0f, 1000.0f, 0.0f, 0, particleConfiguration);
		HIGHLIGHTER_CHECK(emission.count == 0);
		// 90 粒/秒 * 1/30s = 3.0，预算为 0 时只保留小数部分 0。
		HIGHLIGHTER_CHECK(NearlyEqual(emission.fractionalParticles, 0.0f));
		emission = draw3::ScheduleLaserParticleEmission(
			0.0f, 0.0f, emission.fractionalParticles, 120, particleConfiguration);
		HIGHLIGHTER_CHECK(emission.count == 0); // 超额整数部分不积压到下一帧。

	const draw3::LaserParticleEmissionDirection negativeNormal =
		draw3::ResolveLaserParticleEmissionDirection(
			1.0f, 0.0f, 0.25f, 0.5f, particleConfiguration);
	const draw3::LaserParticleEmissionDirection positiveNormal =
		draw3::ResolveLaserParticleEmissionDirection(
			1.0f, 0.0f, 0.75f, 0.5f, particleConfiguration);
	HIGHLIGHTER_CHECK(negativeNormal.valid && positiveNormal.valid);
	HIGHLIGHTER_CHECK(NearlyEqual(negativeNormal.x, 0.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(negativeNormal.y, -1.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(positiveNormal.x, 0.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(positiveNormal.y, 1.0f));
	const draw3::LaserParticleEmissionDirection maximumDeflection =
		draw3::ResolveLaserParticleEmissionDirection(
			1.0f, 0.0f, 0.75f, 1.0f, particleConfiguration);
	const float deflectionDegrees = std::acos(std::clamp(
		maximumDeflection.y, -1.0f, 1.0f)) * 180.0f / 3.14159265359f;
	HIGHLIGHTER_CHECK(NearlyEqual(deflectionDegrees, 25.0f, 0.01f));

	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleLifeFactor(
		0.0f, 1.0f), 1.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleLifeFactor(
		0.5f, 1.0f), 0.5f));
	HIGHLIGHTER_CHECK(draw3::LaserParticleLifeFactor(
		0.25f, 1.0f) >
		draw3::LaserParticleLifeFactor(
			0.5f, 1.0f));
	HIGHLIGHTER_CHECK(draw3::LaserParticleLifeFactor(
		0.5f, 1.0f) >
		draw3::LaserParticleLifeFactor(
			0.75f, 1.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleLifeFactor(
		1.0f, 1.0f), 0.0f));
	const draw3::LaserParticleMotionStep earlyMotion =
		draw3::EvaluateLaserParticleMotionStep(
			64.0f, 0.0f, 0.25f, 1.0f, 1.0f / 30.0f);
	const draw3::LaserParticleMotionStep lateMotion =
		draw3::EvaluateLaserParticleMotionStep(
			64.0f, 0.0f, 0.75f, 1.0f, 1.0f / 30.0f);
	HIGHLIGHTER_CHECK(earlyMotion.distance > lateMotion.distance);
	HIGHLIGHTER_CHECK(NearlyEqual(earlyMotion.deltaY, 0.0f));
	// Up 或 prediction 跳变没有输入参数，因此同一粒子状态得到完全相同的运动步。
	const draw3::LaserParticleMotionStep independentMotion =
		draw3::EvaluateLaserParticleMotionStep(
			64.0f, 0.0f, 0.25f, 1.0f, 1.0f / 30.0f);
	HIGHLIGHTER_CHECK(NearlyEqual(
		independentMotion.distance, earlyMotion.distance));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleRadiusScale(
		0.0f, 10.0f, particleConfiguration), 1.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleRadiusScale(
		1.0f, 10.0f, particleConfiguration), 1.0f));
	const float middleRadiusScale = draw3::LaserParticleRadiusScale(
		5.5f, 10.0f, particleConfiguration);
	HIGHLIGHTER_CHECK(middleRadiusScale > 0.20f && middleRadiusScale < 1.0f);
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleRadiusScale(
		10.0f, 10.0f, particleConfiguration), 0.20f));
HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleGlowExtentDip(
			0.28f, particleConfiguration), 0.56f));
		HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleGlowExtentDip(
			1.4f, particleConfiguration), 2.8f));
		// 粒子核心已对齐 border 红，whiteMix 默认为 0；helper 仍保持公式兼容。
		HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleCoreColorMix(
			particleConfiguration.minimumBrightness,
			particleConfiguration), 0.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(draw3::LaserParticleCoreColorMix(
			1.0f, particleConfiguration), 0.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(
			draw3::LaserParticleCoreColorWhiteMixScale(1u, particleConfiguration),
			1.0f));
		const float breathingAge = 0.2f;
		const float peakBreathingPhase = 1.57079632679f -
			6.28318530718f * breathingAge;
		HIGHLIGHTER_CHECK(NearlyEqual(draw3::EvaluateLaserParticleBrightness(
			0.8f, 0.0f, 1.0f, peakBreathingPhase,
			particleConfiguration), 0.8f));
		const float peakBreathingBrightness =
			draw3::EvaluateLaserParticleBrightness(
				0.8f, breathingAge, 1.0f, peakBreathingPhase,
				particleConfiguration);
		const float troughBreathingBrightness =
			draw3::EvaluateLaserParticleBrightness(
				0.8f, breathingAge, 1.0f,
				peakBreathingPhase + 3.14159265359f,
				particleConfiguration);
		HIGHLIGHTER_CHECK(peakBreathingBrightness > 0.91f);
		HIGHLIGHTER_CHECK(troughBreathingBrightness < 0.69f);
		const float peakBreathingOpacity =
			draw3::LaserParticleLifeFactor(breathingAge, 1.0f);
		const float troughBreathingOpacity =
			draw3::LaserParticleLifeFactor(breathingAge, 1.0f);
		HIGHLIGHTER_CHECK(NearlyEqual(
			peakBreathingOpacity, troughBreathingOpacity)); // 呼吸相位不进入 Alpha。
		// maxLaunch * (maxLife*0.5 + 1/30) = 40 * (0.375 + 1/30)
		const float expectedMaximumTravel =
			40.0f * (0.75f * 0.5f + 1.0f / 30.0f);
		HIGHLIGHTER_CHECK(NearlyEqual(draw3::MaximumLaserParticleTravelDip(
			particleConfiguration), expectedMaximumTravel));
		HIGHLIGHTER_CHECK(draw3::LaserParticleLifetimeDeadlineQpc(
			100, 1000, particleConfiguration) == 850);

	const draw3::LaserParticleEmissionRequest boundsRequest = {
		10.0f, 20.0f, 1.0f, 0.0f, 2.5f, 1, 7 };
	const RECT unclippedParticleBounds =
		draw3::ConservativeLaserParticleBatchBounds(
			boundsRequest, particleConfiguration, 1.0f,
			draw3::kLaserCoreDiameterRatio);
	HIGHLIGHTER_CHECK(unclippedParticleBounds.left < 0);
	HIGHLIGHTER_CHECK(unclippedParticleBounds.right > 20);
	const RECT smallCanvasParticleBounds = draw3::ClampRectToCanvas(
		unclippedParticleBounds, 20, 30);
	const RECT resizedCanvasParticleBounds = draw3::ClampRectToCanvas(
		unclippedParticleBounds, 200, 200);
	HIGHLIGHTER_CHECK(smallCanvasParticleBounds.right == 20);
	HIGHLIGHTER_CHECK(resizedCanvasParticleBounds.right > 20);

	draw3::LaserParticleDirtyTracker dirtyTracker;
	const int64_t dirtyExpiry = draw3::LaserParticleLifetimeDeadlineQpc(
		120, 1000, particleConfiguration);
	dirtyTracker.Add(RECT{ 10, 20, 30, 40 }, 100);
	dirtyTracker.Add(RECT{ 100, 200, 130, 240 }, dirtyExpiry);
	HIGHLIGHTER_CHECK(dirtyTracker.HasActive(50));
	const RECT dirtyBounds = dirtyTracker.ActiveBounds(50);
	HIGHLIGHTER_CHECK(dirtyBounds.left == 10 && dirtyBounds.top == 20);
	HIGHLIGHTER_CHECK(dirtyBounds.right == 130 && dirtyBounds.bottom == 240);
	const RECT afterFirstBatchExpiry = dirtyTracker.ActiveBounds(100);
	HIGHLIGHTER_CHECK(afterFirstBatchExpiry.left == 100);
	HIGHLIGHTER_CHECK(afterFirstBatchExpiry.top == 200);
	HIGHLIGHTER_CHECK(dirtyTracker.HasActive(dirtyExpiry - 1));
	HIGHLIGHTER_CHECK(!dirtyTracker.HasActive(dirtyExpiry));
	HIGHLIGHTER_CHECK(dirtyTracker.ActiveBounds(dirtyExpiry).left ==
		dirtyTracker.ActiveBounds(dirtyExpiry).right);
	const RECT laserBounds = draw3::RectFromLaserPoints({
		{ 50.0f, 50.0f, 2.5f, 0.0f } }, 1.0f, 100, 100);
	HIGHLIGHTER_CHECK(laserBounds.left == 39 && laserBounds.top == 39);
	HIGHLIGHTER_CHECK(laserBounds.right == 61 && laserBounds.bottom == 61);
	const RECT maximumPressureLaserBounds = draw3::RectFromLaserPoints({
		{ 50.0f, 50.0f, 3.5f, 0.0f } }, 1.0f, 100, 100);
	HIGHLIGHTER_CHECK(maximumPressureLaserBounds.left == 38);
	HIGHLIGHTER_CHECK(maximumPressureLaserBounds.right == 62);
	const RECT highDpiLaserBounds = draw3::RectFromLaserPoints({
		{ 50.0f, 50.0f, 5.0f, 0.0f } }, 2.0f, 100, 100);
	HIGHLIGHTER_CHECK(highDpiLaserBounds.left == 32);
	HIGHLIGHTER_CHECK(highDpiLaserBounds.right == 68);

	draw3::ActiveStroke completedPen(5.0f, 500.0f);
	completedPen.realPoints = {
		{ 10.0f, 20.0f, 2.5f, 0.0f },
		{ 20.0f, 20.0f, 2.4f, 0.01f },
		{ 30.0f, 22.0f, 1.8f, 0.02f },
		{ 40.0f, 25.0f, 0.8f, 0.03f }
	};
	completedPen.committedIndex = 1;
	completedPen.hasCommittedGeometry = true;
	completedPen.previousL0DrawPoints = {
		{ 20.0f, 20.0f, 2.4f, 0.01f },
		{ 50.0f, 30.0f, 1.2f, 0.04f }
	};
std::vector<draw3::InkPoint> completedTail;
		draw3::BuildCompletedPenTail(completedPen, false, 0.055, completedTail);
		HIGHLIGHTER_CHECK(completedTail.size() == 3);
		HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().x, 20.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(completedTail.back().x, 40.0f));
		// 默认定住真实尾并叠加笔锋，末端应比未 taper 的 realPoints 更细。
		HIGHLIGHTER_CHECK(completedTail.back().r < completedPen.realPoints.back().r);
		draw3::BuildCompletedPenTail(completedPen, true, 0.055, completedTail);
		HIGHLIGHTER_CHECK(completedTail.size() == 2);
		HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().x, 20.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(completedTail.back().x, 50.0f));

		draw3::ActiveStroke clickPen(5.0f, 500.0f);
		clickPen.inputStartPoint = { 12.0f, 34.0f, 2.5f, 0.0f };
		clickPen.hasInputStartPoint = true;
		draw3::BuildCompletedPenTail(clickPen, false, 0.055, completedTail);
		HIGHLIGHTER_CHECK(completedTail.size() == 1);
		HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().x, 12.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().y, 34.0f));

	constexpr float kHalfHeight = 25.0f;
	constexpr float kHalfWidth = 3.125f;
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::kHighlighterNibAspectRatio, 8.0f));
	HIGHLIGHTER_CHECK(sizeof(draw3::HighlighterPrimitive) == 24);

	draw3::HighlighterGeometry geometry = draw3::BuildHighlighterGeometry({});
	HIGHLIGHTER_CHECK(geometry.primitives.empty());
	HIGHLIGHTER_CHECK(geometry.bounds.left == geometry.bounds.right);
	HIGHLIGHTER_CHECK(geometry.bounds.top == geometry.bounds.bottom);

	const draw3::InkPoint click = { 10.0f, 20.0f, kHalfHeight, 0.0f };
	geometry = draw3::BuildHighlighterGeometry({ click });
	HIGHLIGHTER_CHECK(geometry.primitives.size() == 1);
	if (!geometry.primitives.empty())
	{
		const draw3::HighlighterPrimitive& mark = geometry.primitives.front();
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p1.x, click.x));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p1.y, click.y));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p2.x, click.x));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p2.y, click.y));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.halfSize.x, kHalfWidth));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.halfSize.y, kHalfHeight));
	}
	HIGHLIGHTER_CHECK(geometry.bounds.left == 3);
	HIGHLIGHTER_CHECK(geometry.bounds.top == -8);
	HIGHLIGHTER_CHECK(geometry.bounds.right == 17);
	HIGHLIGHTER_CHECK(geometry.bounds.bottom == 48);

	const std::vector<draw3::InkPoint> underTwelvePixels = {
		click, { 18.0f, 20.0f, kHalfHeight, 0.01f }
	};
	const draw3::HighlighterGeometry underTwelve =
		draw3::BuildHighlighterGeometry(underTwelvePixels);
	HIGHLIGHTER_CHECK(underTwelve.primitives.size() == 1);
	const std::vector<draw3::InkPoint> exactlyTwelvePixels = {
		click, { 22.0f, 20.0f, kHalfHeight, 0.01f }
	};
	const draw3::HighlighterGeometry exactlyTwelve =
		draw3::BuildHighlighterGeometry(exactlyTwelvePixels);
	HIGHLIGHTER_CHECK(exactlyTwelve.primitives.size() == 1); // 12px 不再是可见性或几何分支。
	if (!exactlyTwelve.primitives.empty())
	{
		const draw3::HighlighterPrimitive& sweep = exactlyTwelve.primitives.front();
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.p1.x, 10.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.p2.x, 22.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.halfSize.x, kHalfWidth));
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.halfSize.y, kHalfHeight));
	}

	const std::vector<draw3::InkPoint> curve = {
		{ 10.0f, 20.0f, kHalfHeight, 0.0f },
		{ 40.0f, 20.0f, kHalfHeight, 0.01f },
		{ 40.0f, 50.0f, kHalfHeight, 0.02f },
		{ 12.0f, 52.0f, kHalfHeight, 0.03f }
	};
	geometry = draw3::BuildHighlighterGeometry(curve);
	HIGHLIGHTER_CHECK(geometry.primitives.size() == curve.size() - 1); // 转角和回折不再追加圆角 primitive。
	for (std::size_t index = 0; index < geometry.primitives.size(); ++index)
	{
		const draw3::HighlighterPrimitive& primitive = geometry.primitives[index];
		HIGHLIGHTER_CHECK(NearlyEqual(primitive.halfSize.x, kHalfWidth));
		HIGHLIGHTER_CHECK(NearlyEqual(primitive.halfSize.y, kHalfHeight));
		if (index + 1 < geometry.primitives.size())
		{
			const draw3::HighlighterPrimitive& next = geometry.primitives[index + 1];
			HIGHLIGHTER_CHECK(NearlyEqual(primitive.p2.x, next.p1.x));
			HIGHLIGHTER_CHECK(NearlyEqual(primitive.p2.y, next.p1.y));
		}
	}

	const std::vector<draw3::InkPoint> subpixelJitter = {
		click,
		{ 10.01f, 20.0f, kHalfHeight, 0.001f },
		{ 10.20f, 20.0f, kHalfHeight, 0.002f }
	};
	geometry = draw3::BuildHighlighterGeometry(subpixelJitter);
	HIGHLIGHTER_CHECK(geometry.primitives.size() == 1);
	if (!geometry.primitives.empty())
	{
		HIGHLIGHTER_CHECK(NearlyEqual(geometry.primitives.front().p1.x, 10.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(geometry.primitives.front().p2.x, 10.0f));
	}
	std::vector<draw3::InkPoint> accumulatedMovement = subpixelJitter;
	accumulatedMovement.push_back({ 10.26f, 20.0f, kHalfHeight, 0.003f });
	geometry = draw3::BuildHighlighterGeometry(accumulatedMovement);
	HIGHLIGHTER_CHECK(geometry.primitives.size() == 1);
	if (!geometry.primitives.empty())
		HIGHLIGHTER_CHECK(NearlyEqual(geometry.primitives.front().p2.x, 10.26f));

	const std::vector<draw3::InkPoint> committedPoints = {
		{ 10.0f, 20.0f, kHalfHeight, 0.0f },
		{ 22.0f, 20.0f, kHalfHeight, 0.01f }
	};
	const std::vector<draw3::InkPoint> liveTailPoints = {
		{ 22.0f, 20.0f, kHalfHeight, 0.01f },
		{ 22.0f, 36.0f, kHalfHeight, 0.02f },
		{ 40.0f, 48.0f, kHalfHeight, 0.03f }
	};
	const draw3::HighlighterGeometry committedPrefix =
		draw3::BuildHighlighterGeometry(committedPoints);
	const draw3::HighlighterGeometry liveTail =
		draw3::BuildHighlighterGeometry(liveTailPoints);
	const draw3::HighlighterGeometry completedFromCache =
		draw3::MergeHighlighterGeometry(committedPrefix, liveTail);
	HIGHLIGHTER_CHECK(completedFromCache.primitives.size() == 3);
	HIGHLIGHTER_CHECK(SamePrimitive(completedFromCache.primitives.front(),
		committedPrefix.primitives.front()));
	HIGHLIGHTER_CHECK(SamePrimitive(completedFromCache.primitives.back(),
		liveTail.primitives.back()));
	HIGHLIGHTER_CHECK(NearlyEqual(committedPrefix.primitives.back().p2.x,
		liveTail.primitives.front().p1.x));
	HIGHLIGHTER_CHECK(NearlyEqual(committedPrefix.primitives.back().p2.y,
		liveTail.primitives.front().p1.y));

	draw3::ActiveStroke cachedCompletion(50.0f, 500.0f,
		draw3::StrokeWidthMode::Fixed, true);
	cachedCompletion.committedHighlighterGeometry = committedPrefix;
	cachedCompletion.l0HighlighterGeometry = liveTail;
	cachedCompletion.realPoints = {
		{ 300.0f, 400.0f, kHalfHeight, 1.0f },
		{ 100.0f, 50.0f, kHalfHeight, 1.1f }
	};
	const draw3::HighlighterGeometry afterRealPointMutation = draw3::MergeHighlighterGeometry(
		cachedCompletion.committedHighlighterGeometry, cachedCompletion.l0HighlighterGeometry);
	HIGHLIGHTER_CHECK(afterRealPointMutation.primitives.size() == completedFromCache.primitives.size());
	for (std::size_t index = 0; index < completedFromCache.primitives.size(); ++index)
		HIGHLIGHTER_CHECK(SamePrimitive(afterRealPointMutation.primitives[index],
			completedFromCache.primitives[index]));

	if (failures == 0) std::cout << "All highlighter geometry tests passed." << std::endl;
	return failures;
}
