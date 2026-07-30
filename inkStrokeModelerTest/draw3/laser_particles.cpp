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
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kTwoPi = 6.28318530717958647692f;
		constexpr float kMaximumMotionDeltaSeconds = 1.0f / 30.0f;
		constexpr float kMaximumCoreSpawnOffsetRatio = 0.72f;

		struct LaserParticleUpdateConstants
		{
			float wallDeltaSeconds = 0.0f;
			float motionDeltaSeconds = 0.0f;
			float shrinkStartTravelRatio = 0.10f;
			float endRadiusScale = 0.20f;
			uint32_t resetAll = 0;
			uint32_t particleCapacity = kLaserParticleCapacity;
			float padding[2] = {};
		};

		static_assert(sizeof(LaserParticleUpdateConstants) == 32);
		static_assert(sizeof(LaserParticleUpdateConstants) % 16 == 0);

		struct LaserParticleEmitConstants
		{
			uint32_t spawnStart = 0;
			uint32_t spawnCount = 0;
			uint32_t particleCapacity = kLaserParticleCapacity;
			uint32_t seedBase = 0;
			float positionX = 0.0f;
			float positionY = 0.0f;
			float tangentX = 1.0f;
			float tangentY = 0.0f;
			float entityRadius = 0.0f;
			float coreRadiusRatio = 1.0f / 3.0f;
			float minimumLifetimeSeconds = 0.7f;
			float maximumLifetimeSeconds = 1.0f;
			float minimumLaunchSpeed = 28.0f;
			float maximumLaunchSpeed = 64.0f;
			float maximumDeflectionRadians = 25.0f * kPi / 180.0f;
			float minimumRadius = 0.65f;
			float maximumRadius = 1.10f;
			float minimumBrightness = 0.68f;
			float maximumBrightness = 1.0f;
			float breathingAmplitude = 0.12f;
			float minimumBreathingFrequencyHz = 0.8f;
			float maximumBreathingFrequencyHz = 1.4f;
			float breathingRampSeconds = 0.20f;
			float padding = 0.0f;
		};

		static_assert(sizeof(LaserParticleEmitConstants) == 96);
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
		float launchSpeedSample, float lifetimeSample,
		const LaserParticleConfig& configuration) noexcept
	{
		LaserParticleBirthSample result;
		if (!IsValidLaserParticleConfig(configuration) ||
			!std::isfinite(launchSpeedSample) ||
			!std::isfinite(lifetimeSample)) return result;
		const float speedRatio = std::clamp(launchSpeedSample, 0.0f, 1.0f);
		const float lifetimeRatio = std::clamp(lifetimeSample, 0.0f, 1.0f);
		result.launchSpeedDipPerSecond =
			configuration.minimumLaunchSpeedDipPerSecond +
			(configuration.maximumLaunchSpeedDipPerSecond -
				configuration.minimumLaunchSpeedDipPerSecond) * speedRatio;
		result.lifetimeSeconds = configuration.minimumLifetimeSeconds +
			(configuration.maximumLifetimeSeconds -
				configuration.minimumLifetimeSeconds) * lifetimeRatio;
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
		const float padding = MaximumLaserParticleTravelDip(configuration) * scale +
			entityRadius * coreRatio * kMaximumCoreSpawnOffsetRatio +
			configuration.maximumRadiusDip * scale +
			configuration.glowExtentDip * scale + 2.0f;
		return {
			static_cast<LONG>(std::floor(request.positionX - padding)),
			static_cast<LONG>(std::floor(request.positionY - padding)),
			static_cast<LONG>(std::ceil(request.positionX + padding)),
			static_cast<LONG>(std::ceil(request.positionY + padding))
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

	bool LaserParticleSystem::Initialize(ID3D11Device* device,
		ID3D11DeviceContext* context, LaserParticleShaderBytecode updateShader,
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
		updateConstants_.Reset();
		emitConstants_.Reset();
		context_.Reset();
		device_.Reset();
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

	bool LaserParticleSystem::DispatchUpdate(
		float wallDeltaSeconds, float motionDeltaSeconds, bool resetAll) noexcept
	{
		if (!available_) return false;
		LaserParticleUpdateConstants constants;
		constants.wallDeltaSeconds = std::isfinite(wallDeltaSeconds)
			? std::max(wallDeltaSeconds, 0.0f) : 0.0f;
		constants.motionDeltaSeconds = std::isfinite(motionDeltaSeconds)
			? std::clamp(motionDeltaSeconds, 0.0f, kMaximumMotionDeltaSeconds) : 0.0f;
		constants.shrinkStartTravelRatio =
			configuration_.shrinkStartTravelRatio;
		constants.endRadiusScale = configuration_.endRadiusScale;
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
		DispatchUpdate(wallDeltaSeconds, motionDeltaSeconds, false);
	}

	void LaserParticleSystem::Emit(
		const LaserParticleEmissionRequest& request) noexcept
	{
		if (!available_ || request.count == 0 ||
			!std::isfinite(request.positionX) || !std::isfinite(request.positionY) ||
			!std::isfinite(request.tangentX) || !std::isfinite(request.tangentY))
			return;
		const float tangentLength = std::hypot(request.tangentX, request.tangentY);
		if (tangentLength <= kMinimumPositiveValue) return;

		const uint32_t count = std::min(request.count, kLaserParticleCapacity);
		LaserParticleEmitConstants constants;
		constants.spawnStart = spawnCursor_;
		constants.spawnCount = count;
		constants.seedBase = request.seedBase;
		constants.positionX = request.positionX;
		constants.positionY = request.positionY;
		constants.tangentX = request.tangentX / tangentLength;
		constants.tangentY = request.tangentY / tangentLength;
		constants.entityRadius = std::isfinite(request.entityRadius)
			? std::max(request.entityRadius, 0.0f) : 0.0f;
		constants.coreRadiusRatio = coreRadiusRatio_;
		constants.minimumLifetimeSeconds = configuration_.minimumLifetimeSeconds;
		constants.maximumLifetimeSeconds = configuration_.maximumLifetimeSeconds;
		constants.minimumLaunchSpeed =
			configuration_.minimumLaunchSpeedDipPerSecond * dpiScale_;
		constants.maximumLaunchSpeed =
			configuration_.maximumLaunchSpeedDipPerSecond * dpiScale_;
		constants.maximumDeflectionRadians =
			configuration_.maximumDeflectionAngleDegrees * kPi / 180.0f;
		constants.minimumRadius = configuration_.minimumRadiusDip * dpiScale_;
		constants.maximumRadius = configuration_.maximumRadiusDip * dpiScale_;
		constants.minimumBrightness = configuration_.minimumBrightness;
		constants.maximumBrightness = configuration_.maximumBrightness;
		constants.breathingAmplitude = configuration_.breathingAmplitude;
		constants.minimumBreathingFrequencyHz =
			configuration_.minimumBreathingFrequencyHz;
		constants.maximumBreathingFrequencyHz =
			configuration_.maximumBreathingFrequencyHz;
		constants.breathingRampSeconds = configuration_.breathingRampSeconds;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context_->Map(emitConstants_.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		std::memcpy(mapped.pData, &constants, sizeof(constants));
		context_->Unmap(emitConstants_.Get(), 0);

		ID3D11ShaderResourceView* nullParticle[] = { nullptr };
		context_->VSSetShaderResources(8, 1, nullParticle);
		ID3D11UnorderedAccessView* unorderedViews[] = { particleUAV_.Get() };
		context_->CSSetUnorderedAccessViews(0, 1, unorderedViews, nullptr);
		ID3D11Buffer* constantBuffers[] = { emitConstants_.Get() };
		context_->CSSetConstantBuffers(0, 1, constantBuffers);
		context_->CSSetShader(emitShader_.Get(), nullptr, 0);
		context_->Dispatch((count + 63u) / 64u, 1, 1);
		UnbindComputeResources();

		spawnCursor_ = (spawnCursor_ + count) % kLaserParticleCapacity;
	}

	void LaserParticleSystem::Reset() noexcept
	{
		if (!available_) return;
		spawnCursor_ = 0;
		DispatchUpdate(0.0f, 0.0f, true);
	}

	void LaserParticleSystem::UnbindComputeResources() noexcept
	{
		ID3D11UnorderedAccessView* nullUav[] = { nullptr };
		context_->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
		ID3D11Buffer* nullBuffer[] = { nullptr };
		context_->CSSetConstantBuffers(0, 1, nullBuffer);
		context_->CSSetShader(nullptr, nullptr, 0);
	}

	ID3D11ShaderResourceView* LaserParticleSystem::ParticleShaderResourceView() const noexcept
	{
		return available_ ? particleSRV_.Get() : nullptr;
	}
}
