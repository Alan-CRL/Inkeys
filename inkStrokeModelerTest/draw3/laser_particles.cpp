module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <limits>
#include <vector>
#include <windows.h>

module draw3.laser_particles;

namespace draw3
{
	namespace
	{
		constexpr float kMinimumPositiveValue = 0.0001f;
		constexpr float kTwoPi = 6.28318530717958647692f;
		constexpr float kMaximumMotionDeltaSeconds = 1.0f / 30.0f;
		constexpr float kMaximumSpeedJitter = 1.12f;

		struct LaserParticleUpdateConstants
		{
			float wallDeltaSeconds = 0.0f;
			float motionDeltaSeconds = 0.0f;
			float speedResponseSeconds = 0.080f;
			float spreadSeconds = 0.120f;
			float minimumSpeed = 12.0f;
			float maximumSpeed = 96.0f;
			float inputSpeedInfluence = 0.08f;
			float lifetimeSeconds = 3.0f;
			float positionResponseSeconds = 0.040f;
			float predictionCorrectionSpeedMultiplier = 2.0f;
			float predictionJumpThreshold = 6.0f;
			float dpiScale = 1.0f;
			uint32_t resetAll = 0;
			uint32_t pathPointCapacity = kLaserParticlePathPointCapacity;
			uint32_t particleCapacity = kLaserParticleCapacity;
			uint32_t padding1 = 0;
		};

		static_assert(sizeof(LaserParticleUpdateConstants) == 64);
		static_assert(sizeof(LaserParticleUpdateConstants) % 16 == 0);

		struct LaserParticleEmitConstants
		{
			uint32_t pathSlot = kInvalidLaserParticlePathSlot;
			uint32_t pathGeneration = 0;
			uint32_t spawnStart = 0;
			uint32_t spawnCount = 0;
			float arcLength = 0.0f;
			float anchorX = 0.0f;
			float anchorY = 0.0f;
			float maximumLateralExtra = 10.0f;
			float lifetimeSeconds = 3.0f;
			float minimumBrightness = 0.68f;
			float maximumBrightness = 1.0f;
			float breathingAmplitude = 0.12f;
			float minimumBreathingFrequencyHz = 0.8f;
			float maximumBreathingFrequencyHz = 1.4f;
			float breathingRampSeconds = 0.20f;
			float minimumRadius = 0.65f;
			float maximumRadius = 1.10f;
			float coreRadiusRatio = 1.0f / 3.0f;
			float minimumSpeed = 12.0f;
			float maximumSpeed = 96.0f;
			float inputSpeedInfluence = 0.08f;
			float padding0[3] = {};
			uint32_t seedBase = 0;
			uint32_t pathPointCapacity = kLaserParticlePathPointCapacity;
			uint32_t particleCapacity = kLaserParticleCapacity;
			uint32_t padding1 = 0;
		};

		static_assert(sizeof(LaserParticleEmitConstants) == 112);
		static_assert(sizeof(LaserParticleEmitConstants) % 16 == 0);

		bool IsFinitePositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		bool IsFiniteUnit(float value) noexcept
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
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
			IsFinitePositive(configuration.lifetimeSeconds) &&
			IsFinitePositive(configuration.minimumSpeedDipPerSecond) &&
			IsFinitePositive(configuration.maximumSpeedDipPerSecond) &&
			configuration.maximumSpeedDipPerSecond >= configuration.minimumSpeedDipPerSecond &&
			std::isfinite(configuration.inputSpeedInfluence) &&
			configuration.inputSpeedInfluence >= 0.0f &&
			IsFinitePositive(configuration.speedResponseSeconds) &&
			IsFinitePositive(configuration.spreadSeconds) &&
			std::isfinite(configuration.maximumLateralExtraDip) &&
			configuration.maximumLateralExtraDip >= 0.0f &&
			IsFinitePositive(configuration.minimumRadiusDip) &&
			IsFinitePositive(configuration.maximumRadiusDip) &&
			configuration.maximumRadiusDip >= configuration.minimumRadiusDip &&
			IsFinitePositive(configuration.glowExtentDip) &&
			IsFiniteUnit(configuration.glowRed) &&
			IsFiniteUnit(configuration.glowGreen) &&
			IsFiniteUnit(configuration.glowBlue) &&
			IsFiniteUnit(configuration.glowAlpha) &&
			IsFiniteUnit(configuration.minimumBrightness) &&
			configuration.maximumBrightness >= configuration.minimumBrightness &&
			configuration.maximumBrightness <= 1.0f &&
			IsFiniteUnit(configuration.breathingAmplitude) &&
			IsFinitePositive(configuration.minimumBreathingFrequencyHz) &&
			IsFinitePositive(configuration.maximumBreathingFrequencyHz) &&
			configuration.maximumBreathingFrequencyHz >=
				configuration.minimumBreathingFrequencyHz &&
			IsFinitePositive(configuration.breathingRampSeconds) &&
			IsFinitePositive(configuration.positionResponseSeconds) &&
			IsFinitePositive(configuration.predictionCorrectionSpeedMultiplier) &&
			IsFinitePositive(configuration.predictionJumpThresholdDip);
	}

	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float wallDeltaSeconds,
		float forwardArcLength, float fractionalParticles, uint32_t remainingFrameBudget,
		const LaserParticleConfig& configuration) noexcept
	{
		LaserParticleEmissionSchedule result;
		if (!IsValidLaserParticleConfig(configuration)) return result;
		const float wallDelta = std::isfinite(wallDeltaSeconds)
			? std::max(wallDeltaSeconds, 0.0f) : 0.0f;
		const float emissionDelta = std::min(wallDelta, kMaximumMotionDeltaSeconds);
		const float forward = std::isfinite(forwardArcLength)
			? std::max(forwardArcLength, 0.0f) : 0.0f;
		const float forwardSpeed = emissionDelta > kMinimumPositiveValue
			? forward / emissionDelta : 0.0f;
		result.emissionRatePerSecond = std::min(
			configuration.idleEmissionRatePerSecond +
				forwardSpeed / configuration.motionEmissionSpacingDip,
			configuration.maximumEmissionRatePerSecond);
		const float carry = std::isfinite(fractionalParticles)
			? std::clamp(fractionalParticles, 0.0f, 0.999999f) : 0.0f;
		const float total = carry + result.emissionRatePerSecond * emissionDelta;
		const uint32_t available = static_cast<uint32_t>(std::min(
			std::floor(static_cast<double>(total)),
			static_cast<double>((std::numeric_limits<uint32_t>::max)())));
		result.count = std::min(available, remainingFrameBudget);
		result.fractionalParticles = total - std::floor(total);
		if (!std::isfinite(result.fractionalParticles))
			result.fractionalParticles = 0.0f;
		return result;
	}

	float LaserParticleTargetSpeed(float filteredInputSpeed, float dpiScale,
		const LaserParticleConfig& configuration) noexcept
	{
		const float scale = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		const float inputSpeed = std::isfinite(filteredInputSpeed)
			? std::max(filteredInputSpeed, 0.0f) : 0.0f;
		return std::clamp(configuration.minimumSpeedDipPerSecond * scale +
			configuration.inputSpeedInfluence * inputSpeed,
			configuration.minimumSpeedDipPerSecond * scale,
			configuration.maximumSpeedDipPerSecond * scale);
	}

	float SmoothLaserParticleSpeed(float currentSpeed, float targetSpeed,
		float deltaSeconds, float responseSeconds) noexcept
	{
		const float current = std::isfinite(currentSpeed)
			? std::max(currentSpeed, 0.0f) : 0.0f;
		const float target = std::isfinite(targetSpeed)
			? std::max(targetSpeed, 0.0f) : current;
		const float delta = std::isfinite(deltaSeconds)
			? std::max(deltaSeconds, 0.0f) : 0.0f;
		const float response = IsFinitePositive(responseSeconds)
			? responseSeconds : kMinimumPositiveValue;
		const float blend = 1.0f - std::exp(-delta / response);
		return current + (target - current) * blend;
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

	float MaximumLaserParticleTravelDip(
		const LaserParticleConfig& configuration) noexcept
	{
		if (!IsValidLaserParticleConfig(configuration)) return 0.0f;
		// 1-smoothstep 在完整寿命上的积分为 1/2；再保留一个最大运动步的余量。
		return configuration.maximumSpeedDipPerSecond * kMaximumSpeedJitter *
			(configuration.lifetimeSeconds * 0.5f + kMaximumMotionDeltaSeconds);
	}

	float LaserParticlePathRetirementSeconds(bool hasEmitted,
		const LaserParticleConfig& configuration) noexcept
	{
		return hasEmitted && IsValidLaserParticleConfig(configuration)
			? configuration.lifetimeSeconds : 0.0f;
	}

	int64_t LaserParticleLifetimeDeadlineQpc(int64_t nowQpc,
		int64_t qpcFrequency, const LaserParticleConfig& configuration) noexcept
	{
		if (nowQpc < 0 || qpcFrequency <= 0 ||
			!IsValidLaserParticleConfig(configuration)) return nowQpc;
		const double lifetimeTicks = std::ceil(
			static_cast<double>(configuration.lifetimeSeconds) *
			static_cast<double>(qpcFrequency));
		const double remainingTicks = static_cast<double>(
			(std::numeric_limits<int64_t>::max)() - nowQpc);
		if (lifetimeTicks >= remainingTicks)
			return (std::numeric_limits<int64_t>::max)();
		return nowQpc + static_cast<int64_t>(lifetimeTicks);
	}

	LaserParticleEmissionAnchor UpdateLaserParticleEmissionAnchor(
		LaserParticleEmissionAnchor current, float targetX, float targetY,
		float filteredInputSpeed, float wallDeltaSeconds, float dpiScale,
		const LaserParticleConfig& configuration) noexcept
	{
		if (!std::isfinite(targetX) || !std::isfinite(targetY)) return current;
		if (!current.valid || !std::isfinite(current.x) || !std::isfinite(current.y))
			return { targetX, targetY, true };

		const float deltaSeconds = std::isfinite(wallDeltaSeconds)
			? std::clamp(wallDeltaSeconds, 0.0f, kMaximumMotionDeltaSeconds) : 0.0f;
		const float scale = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		const float speed = std::isfinite(filteredInputSpeed)
			? std::max(filteredInputSpeed, 0.0f) : 0.0f;
		const float deltaX = targetX - current.x;
		const float deltaY = targetY - current.y;
		const float distance = std::hypot(deltaX, deltaY);
		const float directFollowDistance = std::max(
			configuration.predictionJumpThresholdDip * scale,
			2.0f * speed * deltaSeconds + 2.0f * scale);
		const bool requiresLimitedCorrection =
			current.predictionCorrectionActive || distance > directFollowDistance;
		if (!requiresLimitedCorrection)
			return { targetX, targetY, true };
		current.predictionCorrectionActive = true;
		if (deltaSeconds <= 0.0f || !std::isfinite(distance) ||
			distance <= kMinimumPositiveValue) return current;

		// 极端 prediction 修正只按正常粒子速度的固定倍率追赶，避免大距离指数吸附。
		const float correctionMultiplier =
			IsFinitePositive(configuration.predictionCorrectionSpeedMultiplier)
			? configuration.predictionCorrectionSpeedMultiplier : 0.0f;
		const float maximumCorrectionDistance = LaserParticleTargetSpeed(
			speed, scale, configuration) * correctionMultiplier * deltaSeconds;
		if (maximumCorrectionDistance >= distance)
			return { targetX, targetY, true };
		const float blend = std::clamp(
			maximumCorrectionDistance / distance, 0.0f, 1.0f);
		current.x += deltaX * blend;
		current.y += deltaY * blend;
		current.valid = true;
		return current;
	}

	LaserParticleArcAdvance AdvanceLaserParticleArc(float arcLength, float pathEndArcLength,
		float speed, float lifeFactor, float motionDeltaSeconds) noexcept
	{
		LaserParticleArcAdvance result;
		result.arcLength = std::isfinite(arcLength) ? std::max(arcLength, 0.0f) : 0.0f;
		const float pathEnd = std::isfinite(pathEndArcLength)
			? std::max(pathEndArcLength, 0.0f) : result.arcLength;
		const float multiplier = std::isfinite(lifeFactor)
			? std::clamp(lifeFactor, 0.0f, 1.0f) : 0.0f;
		const float delta = std::max(speed, 0.0f) * multiplier *
			std::max(motionDeltaSeconds, 0.0f);
		const float newArcLength = std::min(result.arcLength + delta, pathEnd);
		result.actualAdvance = std::max(newArcLength - result.arcLength, 0.0f);
		result.arcLength = newArcLength;
		return result;
	}

	uint32_t NextLaserParticlePathGeneration(uint32_t generation) noexcept
	{
		++generation;
		return generation == 0 ? 1 : generation;
	}

	uint32_t ClampLaserParticlePathAppendCount(
		uint32_t currentPointCount, uint32_t requestedPointCount) noexcept
	{
		if (currentPointCount >= kLaserParticlePathPointCapacity) return 0;
		return std::min(requestedPointCount,
			kLaserParticlePathPointCapacity - currentPointCount);
	}

	void LaserParticleDirtyTracker::Add(
		LaserParticlePathHandle path, RECT bounds, int64_t expiresQpc) noexcept
	{
		if (!path.IsValid() || bounds.left >= bounds.right ||
			bounds.top >= bounds.bottom || expiresQpc <= 0) return;
		for (Region& region : regions_)
		{
			if (!region.active || region.path != path) continue;
			// 同一路径只保留一个包络，避免 3 秒寿命下按帧耗尽固定数组。
			UnionRectLocal(region.bounds, bounds);
			region.expiresQpc = std::max(region.expiresQpc, expiresQpc);
			return;
		}
		for (Region& region : regions_)
		{
			if (region.active) continue;
			region = { path, bounds, expiresQpc, true };
			return;
		}

		// 极端高帧率下容量耗尽时合并到首槽，宁可扩大脏区也不能漏清残影。
		Region& fallback = regions_.front();
		UnionRectLocal(fallback.bounds, bounds);
		fallback.expiresQpc = std::max(fallback.expiresQpc, expiresQpc);
		fallback.path = {};
		fallback.active = true;
	}

	void LaserParticleDirtyTracker::ExpandPath(
		LaserParticlePathHandle path, RECT bounds) noexcept
	{
		if (!path.IsValid() || bounds.left >= bounds.right ||
			bounds.top >= bounds.bottom) return;
		for (Region& region : regions_)
		{
			if (!region.active) continue;
			if (region.path == path || !region.path.IsValid())
				UnionRectLocal(region.bounds, bounds);
		}
	}

	RECT LaserParticleDirtyTracker::ActiveBounds(int64_t nowQpc) noexcept
	{
		RECT bounds = {};
		for (Region& region : regions_)
		{
			if (!region.active) continue;
			if (region.expiresQpc <= nowQpc)
			{
				region = {};
				continue;
			}
			UnionRectLocal(bounds, region.bounds);
		}
		return bounds;
	}

	bool LaserParticleDirtyTracker::HasActive(int64_t nowQpc) const noexcept
	{
		return std::any_of(regions_.begin(), regions_.end(),
			[nowQpc](const Region& region)
			{
				return region.active && region.expiresQpc > nowQpc;
			});
	}

	bool LaserParticleDirtyTracker::HasAny() const noexcept
	{
		return std::any_of(regions_.begin(), regions_.end(),
			[](const Region& region) { return region.active; });
	}

	void LaserParticleDirtyTracker::Clear() noexcept
	{
		regions_ = {};
	}

	bool LaserParticleSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
		LaserParticleShaderBytecode updateShader,
		LaserParticleShaderBytecode emitShader) noexcept
	{
		Release();
		if (!device || !context || !updateShader.data || updateShader.size == 0 ||
			!emitShader.data || emitShader.size == 0 ||
			device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) return false;
		device_ = device;
		context_ = context;

		if (FAILED(device_->CreateComputeShader(updateShader.data, updateShader.size,
			nullptr, updateShader_.ReleaseAndGetAddressOf())) ||
			FAILED(device_->CreateComputeShader(emitShader.data, emitShader.size,
				nullptr, emitShader_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}

		std::vector<LaserGpuParticle> emptyParticles(kLaserParticleCapacity);
		D3D11_SUBRESOURCE_DATA particleData = {};
		particleData.pSysMem = emptyParticles.data();
		D3D11_BUFFER_DESC bufferDescription = {};
		bufferDescription.ByteWidth = static_cast<UINT>(
			sizeof(LaserGpuParticle) * kLaserParticleCapacity);
		bufferDescription.Usage = D3D11_USAGE_DEFAULT;
		bufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
			D3D11_BIND_UNORDERED_ACCESS;
		bufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDescription.StructureByteStride = sizeof(LaserGpuParticle);
		if (FAILED(device_->CreateBuffer(&bufferDescription, &particleData,
			particleBuffer_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription = {};
		srvDescription.Format = DXGI_FORMAT_UNKNOWN;
		srvDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDescription.Buffer.NumElements = kLaserParticleCapacity;
		if (FAILED(device_->CreateShaderResourceView(particleBuffer_.Get(),
			&srvDescription, particleSRV_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription = {};
		uavDescription.Format = DXGI_FORMAT_UNKNOWN;
		uavDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDescription.Buffer.NumElements = kLaserParticleCapacity;
		if (FAILED(device_->CreateUnorderedAccessView(particleBuffer_.Get(),
			&uavDescription, particleUAV_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}

		bufferDescription = {};
		bufferDescription.ByteWidth = static_cast<UINT>(sizeof(LaserParticlePathPoint) *
			kLaserParticlePathCapacity * kLaserParticlePathPointCapacity);
		bufferDescription.Usage = D3D11_USAGE_DEFAULT;
		bufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDescription.StructureByteStride = sizeof(LaserParticlePathPoint);
		if (FAILED(device_->CreateBuffer(&bufferDescription, nullptr,
			pathPointBuffer_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}
		srvDescription.Buffer.NumElements =
			kLaserParticlePathCapacity * kLaserParticlePathPointCapacity;
		if (FAILED(device_->CreateShaderResourceView(pathPointBuffer_.Get(),
			&srvDescription, pathPointSRV_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}

		bufferDescription.ByteWidth = static_cast<UINT>(
			sizeof(LaserParticlePathHeader) * kLaserParticlePathCapacity);
		bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
		bufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDescription.StructureByteStride = sizeof(LaserParticlePathHeader);
		if (FAILED(device_->CreateBuffer(&bufferDescription, nullptr,
			pathHeaderBuffer_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}
		srvDescription.Buffer.NumElements = kLaserParticlePathCapacity;
		if (FAILED(device_->CreateShaderResourceView(pathHeaderBuffer_.Get(),
			&srvDescription, pathHeaderSRV_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}

		bufferDescription = {};
		bufferDescription.ByteWidth = sizeof(LaserParticleUpdateConstants);
		bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
		bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device_->CreateBuffer(&bufferDescription, nullptr,
			updateConstants_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}
		bufferDescription.ByteWidth = sizeof(LaserParticleEmitConstants);
		if (FAILED(device_->CreateBuffer(&bufferDescription, nullptr,
			emitConstants_.ReleaseAndGetAddressOf())))
		{
			Release();
			return false;
		}

		available_ = true;
		if (!UploadPathHeaders())
		{
			Release();
			return false;
		}
		return true;
	}

	void LaserParticleSystem::Release() noexcept
	{
		available_ = false;
		updateShader_.Reset();
		emitShader_.Reset();
		particleUAV_.Reset();
		particleSRV_.Reset();
		particleBuffer_.Reset();
		pathPointSRV_.Reset();
		pathPointBuffer_.Reset();
		pathHeaderSRV_.Reset();
		pathHeaderBuffer_.Reset();
		updateConstants_.Reset();
		emitConstants_.Reset();
		context_.Reset();
		device_.Reset();
		pathSlots_ = {};
		spawnCursor_ = 0;
	}

	void LaserParticleSystem::Configure(
		const LaserParticleConfig& configuration, float dpiScale,
		float coreRadiusRatio) noexcept
	{
		configuration_ = IsValidLaserParticleConfig(configuration)
			? configuration : LaserParticleConfig{};
		dpiScale_ = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		coreRadiusRatio_ = std::isfinite(coreRadiusRatio)
			? std::clamp(coreRadiusRatio, 0.0f, 1.0f) : 1.0f / 3.0f;
	}

	bool LaserParticleSystem::IsAvailable() const noexcept
	{
		return available_;
	}

	bool LaserParticleSystem::IsCurrentPath(LaserParticlePathHandle path) const noexcept
	{
		if (!path.IsValid()) return false;
		const PathSlotState& slot = pathSlots_[path.slot];
		return slot.header.active != 0 && slot.header.generation == path.generation;
	}

	LaserParticlePathHandle LaserParticleSystem::AcquirePath() noexcept
	{
		if (!available_) return {};
		for (uint32_t index = 0; index < kLaserParticlePathCapacity; ++index)
		{
			PathSlotState& slot = pathSlots_[index];
			if (slot.header.active != 0) continue;
			const uint32_t generation = NextLaserParticlePathGeneration(
				slot.header.generation);
			slot = {};
			slot.header.generation = generation;
			slot.header.active = 1;
			return { index, generation };
		}
		return {};
	}

	void LaserParticleSystem::WritePathPointRange(uint32_t pathSlot,
		uint32_t firstPoint, std::span<const LaserParticlePathPoint> points) noexcept
	{
		if (points.empty()) return;
		const uint32_t baseElement = pathSlot * kLaserParticlePathPointCapacity +
			firstPoint;
		D3D11_BOX destinationBox = {};
		destinationBox.left = baseElement * sizeof(LaserParticlePathPoint);
		destinationBox.right = destinationBox.left +
			static_cast<UINT>(points.size()) * sizeof(LaserParticlePathPoint);
		destinationBox.top = 0;
		destinationBox.bottom = 1;
		destinationBox.front = 0;
		destinationBox.back = 1;
		context_->UpdateSubresource(pathPointBuffer_.Get(), 0, &destinationBox,
			points.data(), 0, 0);
	}

	uint32_t LaserParticleSystem::AppendRealPathPoints(LaserParticlePathHandle path,
		std::span<const LaserParticlePathPoint> points) noexcept
	{
		if (!available_ || !IsCurrentPath(path) || points.empty()) return 0;
		PathSlotState& slot = pathSlots_[path.slot];
		const uint32_t appendCount = ClampLaserParticlePathAppendCount(
			slot.realPointCount, static_cast<uint32_t>(std::min<size_t>(
				points.size(), (std::numeric_limits<uint32_t>::max)())));
		if (appendCount == 0)
		{
			slot.overflowed = true;
			return 0;
		}

		WritePathPointRange(path.slot, slot.realPointCount,
			points.first(appendCount));
		slot.realPointCount += appendCount;
		slot.header.pointCount = slot.realPointCount;
		slot.overflowed = appendCount < points.size();
		return appendCount;
	}

	uint32_t LaserParticleSystem::ReplacePredictionPathPoints(
		LaserParticlePathHandle path,
		std::span<const LaserParticlePathPoint> points) noexcept
	{
		if (!available_ || !IsCurrentPath(path)) return 0;
		PathSlotState& slot = pathSlots_[path.slot];
		const uint32_t predictionCount = ClampLaserParticlePathAppendCount(
			slot.realPointCount, static_cast<uint32_t>(std::min<size_t>(
				points.size(), (std::numeric_limits<uint32_t>::max)())));
		if (predictionCount > 0)
			WritePathPointRange(path.slot, slot.realPointCount,
				points.first(predictionCount));
		slot.header.pointCount = slot.realPointCount + predictionCount;
		slot.overflowed = predictionCount < points.size();
		return predictionCount;
	}

	void LaserParticleSystem::SetPathInputSpeed(
		LaserParticlePathHandle path, float filteredInputSpeed) noexcept
	{
		if (!IsCurrentPath(path)) return;
		pathSlots_[path.slot].header.filteredInputSpeed =
			std::isfinite(filteredInputSpeed) ? std::max(filteredInputSpeed, 0.0f) : 0.0f;
	}

	void LaserParticleSystem::EndPath(LaserParticlePathHandle path) noexcept
	{
		if (!IsCurrentPath(path)) return;
		PathSlotState& slot = pathSlots_[path.slot];
		slot.header.ended = 1;
		slot.retireSeconds = LaserParticlePathRetirementSeconds(
			slot.hasEmitted, configuration_);
		if (slot.retireSeconds <= 0.0f)
		{
			slot.header.active = 0;
			return;
		}
	}

	bool LaserParticleSystem::UploadPathHeaders() noexcept
	{
		if (!available_ || !pathHeaderBuffer_) return false;
		std::array<LaserParticlePathHeader, kLaserParticlePathCapacity> headers = {};
		for (uint32_t index = 0; index < kLaserParticlePathCapacity; ++index)
			headers[index] = pathSlots_[index].header;
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context_->Map(pathHeaderBuffer_.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		std::memcpy(mapped.pData, headers.data(), sizeof(headers));
		context_->Unmap(pathHeaderBuffer_.Get(), 0);
		return true;
	}

	bool LaserParticleSystem::DispatchUpdate(
		float wallDeltaSeconds, float motionDeltaSeconds, bool resetAll) noexcept
	{
		if (!available_ || !UploadPathHeaders()) return false;
		LaserParticleUpdateConstants constants;
		constants.wallDeltaSeconds = std::isfinite(wallDeltaSeconds)
			? std::max(wallDeltaSeconds, 0.0f) : 0.0f;
		constants.motionDeltaSeconds = std::isfinite(motionDeltaSeconds)
			? std::clamp(motionDeltaSeconds, 0.0f, 1.0f / 30.0f) : 0.0f;
		constants.speedResponseSeconds = configuration_.speedResponseSeconds;
		constants.spreadSeconds = configuration_.spreadSeconds;
		constants.minimumSpeed = configuration_.minimumSpeedDipPerSecond * dpiScale_;
		constants.maximumSpeed = configuration_.maximumSpeedDipPerSecond * dpiScale_;
		constants.inputSpeedInfluence = configuration_.inputSpeedInfluence;
		constants.lifetimeSeconds = configuration_.lifetimeSeconds;
		constants.positionResponseSeconds = configuration_.positionResponseSeconds;
		constants.predictionCorrectionSpeedMultiplier =
			configuration_.predictionCorrectionSpeedMultiplier;
		constants.predictionJumpThreshold =
			configuration_.predictionJumpThresholdDip * dpiScale_;
		constants.dpiScale = dpiScale_;
		constants.resetAll = resetAll ? 1u : 0u;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context_->Map(updateConstants_.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		std::memcpy(mapped.pData, &constants, sizeof(constants));
		context_->Unmap(updateConstants_.Get(), 0);

		// 粒子 SRV 先从 VS 解绑，再把同一缓冲区安全绑定为 Compute UAV。
		ID3D11ShaderResourceView* nullParticle[] = { nullptr };
		context_->VSSetShaderResources(8, 1, nullParticle);
		ID3D11UnorderedAccessView* unorderedViews[] = { particleUAV_.Get() };
		context_->CSSetUnorderedAccessViews(0, 1, unorderedViews, nullptr);
		ID3D11ShaderResourceView* shaderResources[] = {
			pathPointSRV_.Get(), pathHeaderSRV_.Get()
		};
		context_->CSSetShaderResources(0, 2, shaderResources);
		ID3D11Buffer* constantBuffers[] = { updateConstants_.Get() };
		context_->CSSetConstantBuffers(0, 1, constantBuffers);
		context_->CSSetShader(updateShader_.Get(), nullptr, 0);
		context_->Dispatch((kLaserParticleCapacity + 63u) / 64u, 1, 1);
		UnbindComputeResources();
		return true;
	}

	void LaserParticleSystem::Simulate(
		float wallDeltaSeconds, float motionDeltaSeconds) noexcept
	{
		if (!DispatchUpdate(wallDeltaSeconds, motionDeltaSeconds, false)) return;
		const float elapsed = std::isfinite(wallDeltaSeconds)
			? std::max(wallDeltaSeconds, 0.0f) : 0.0f;
		for (PathSlotState& slot : pathSlots_)
		{
			if (slot.header.active == 0 || slot.header.ended == 0 ||
				slot.retireSeconds <= 0.0f) continue;
			slot.retireSeconds -= elapsed;
			if (slot.retireSeconds <= 0.0f)
				slot.header.active = 0;
		}
	}

	void LaserParticleSystem::Emit(const LaserParticleEmissionRequest& request) noexcept
	{
		if (!available_ || !IsCurrentPath(request.path) || request.count == 0) return;
		const uint32_t count = std::min(request.count, kLaserParticleCapacity);
		LaserParticleEmitConstants constants;
		constants.pathSlot = request.path.slot;
		constants.pathGeneration = request.path.generation;
		constants.spawnStart = spawnCursor_;
		constants.spawnCount = count;
		constants.arcLength = std::isfinite(request.arcLength)
			? std::max(request.arcLength, 0.0f) : 0.0f;
		constants.anchorX = std::isfinite(request.anchorX) ? request.anchorX : 0.0f;
		constants.anchorY = std::isfinite(request.anchorY) ? request.anchorY : 0.0f;
		constants.maximumLateralExtra =
			configuration_.maximumLateralExtraDip * dpiScale_;
		constants.lifetimeSeconds = configuration_.lifetimeSeconds;
		constants.minimumBrightness = configuration_.minimumBrightness;
		constants.maximumBrightness = configuration_.maximumBrightness;
		constants.breathingAmplitude = configuration_.breathingAmplitude;
		constants.minimumBreathingFrequencyHz =
			configuration_.minimumBreathingFrequencyHz;
		constants.maximumBreathingFrequencyHz =
			configuration_.maximumBreathingFrequencyHz;
		constants.breathingRampSeconds = configuration_.breathingRampSeconds;
		constants.minimumRadius = configuration_.minimumRadiusDip * dpiScale_;
		constants.maximumRadius = configuration_.maximumRadiusDip * dpiScale_;
		constants.coreRadiusRatio = coreRadiusRatio_;
		constants.minimumSpeed = configuration_.minimumSpeedDipPerSecond * dpiScale_;
		constants.maximumSpeed = configuration_.maximumSpeedDipPerSecond * dpiScale_;
		constants.inputSpeedInfluence = configuration_.inputSpeedInfluence;
		constants.seedBase = request.seedBase;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context_->Map(emitConstants_.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		std::memcpy(mapped.pData, &constants, sizeof(constants));
		context_->Unmap(emitConstants_.Get(), 0);

		ID3D11ShaderResourceView* nullParticle[] = { nullptr };
		context_->VSSetShaderResources(8, 1, nullParticle);
		ID3D11UnorderedAccessView* unorderedViews[] = { particleUAV_.Get() };
		context_->CSSetUnorderedAccessViews(0, 1, unorderedViews, nullptr);
		ID3D11ShaderResourceView* shaderResources[] = {
			pathPointSRV_.Get(), pathHeaderSRV_.Get()
		};
		context_->CSSetShaderResources(0, 2, shaderResources);
		ID3D11Buffer* constantBuffers[] = { emitConstants_.Get() };
		context_->CSSetConstantBuffers(0, 1, constantBuffers);
		context_->CSSetShader(emitShader_.Get(), nullptr, 0);
		context_->Dispatch((count + 63u) / 64u, 1, 1);
		UnbindComputeResources();

		spawnCursor_ = (spawnCursor_ + count) % kLaserParticleCapacity;
		pathSlots_[request.path.slot].hasEmitted = true;
	}

	void LaserParticleSystem::Reset() noexcept
	{
		if (!available_) return;
		for (PathSlotState& slot : pathSlots_)
		{
			const uint32_t generation = NextLaserParticlePathGeneration(
				slot.header.generation);
			slot = {};
			slot.header.generation = generation;
		}
		spawnCursor_ = 0;
		DispatchUpdate(0.0f, 0.0f, true);
	}

	void LaserParticleSystem::UnbindComputeResources() noexcept
	{
		ID3D11UnorderedAccessView* nullUav[] = { nullptr };
		context_->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
		ID3D11ShaderResourceView* nullSrvs[] = { nullptr, nullptr };
		context_->CSSetShaderResources(0, 2, nullSrvs);
		ID3D11Buffer* nullBuffer[] = { nullptr };
		context_->CSSetConstantBuffers(0, 1, nullBuffer);
		context_->CSSetShader(nullptr, nullptr, 0);
	}

	ID3D11ShaderResourceView* LaserParticleSystem::ParticleShaderResourceView() const noexcept
	{
		return available_ ? particleSRV_.Get() : nullptr;
	}
}
