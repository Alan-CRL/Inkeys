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

		struct LaserParticleUpdateConstants
		{
			float wallDeltaSeconds = 0.0f;
			float motionDeltaSeconds = 0.0f;
			float dpiScale = 1.0f;
			float speedResponseSeconds = 0.080f;
			float minimumSpeed = 12.0f;
			float maximumSpeed = 96.0f;
			float inputSpeedInfluence = 0.08f;
			float spreadSeconds = 0.120f;
			float fadeStartFraction = 0.55f;
			float convergenceSeconds = 0.220f;
			float positionResponseSeconds = 0.040f;
			float padding0 = 0.0f;
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
			float startArcLength = 0.0f;
			float endArcLength = 0.0f;
			float dpiScale = 1.0f;
			float maximumLateralExtra = 1.5f;
			float minimumLifetime = 0.45f;
			float maximumLifetime = 0.70f;
			float minimumTravel = 24.0f;
			float maximumTravel = 56.0f;
			float minimumRadius = 0.65f;
			float maximumRadius = 1.10f;
			float coreRadiusRatio = 1.0f / 3.0f;
			float minimumSpeed = 12.0f;
			float maximumSpeed = 96.0f;
			float inputSpeedInfluence = 0.08f;
			float padding0[2] = {};
			uint32_t seedBase = 0;
			uint32_t pathPointCapacity = kLaserParticlePathPointCapacity;
			uint32_t particleCapacity = kLaserParticleCapacity;
			uint32_t padding1 = 0;
		};

		static_assert(sizeof(LaserParticleEmitConstants) == 96);
		static_assert(sizeof(LaserParticleEmitConstants) % 16 == 0);

		bool IsFinitePositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
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

		uint32_t MixParticleSeed(uint32_t value) noexcept
		{
			value ^= value >> 16;
			value *= 0x7FEB352Du;
			value ^= value >> 15;
			value *= 0x846CA68Bu;
			return value ^ (value >> 16);
		}
	}

	bool IsValidLaserParticleConfig(const LaserParticleConfig& configuration) noexcept
	{
		return IsFinitePositive(configuration.particlesPerPixel) &&
			configuration.maximumSpawnPerFrame > 0 &&
			configuration.maximumSpawnPerFrame <= kLaserParticleCapacity &&
			IsFinitePositive(configuration.minimumLifetimeSeconds) &&
			configuration.maximumLifetimeSeconds >= configuration.minimumLifetimeSeconds &&
			IsFinitePositive(configuration.minimumSpeedDipPerSecond) &&
			configuration.maximumSpeedDipPerSecond >= configuration.minimumSpeedDipPerSecond &&
			std::isfinite(configuration.inputSpeedInfluence) &&
			configuration.inputSpeedInfluence >= 0.0f &&
			IsFinitePositive(configuration.speedResponseSeconds) &&
			IsFinitePositive(configuration.spreadSeconds) &&
			std::isfinite(configuration.maximumLateralExtraDip) &&
			configuration.maximumLateralExtraDip >= 0.0f &&
			IsFinitePositive(configuration.minimumTravelDip) &&
			configuration.maximumTravelDip >= configuration.minimumTravelDip &&
			IsFinitePositive(configuration.minimumRadiusDip) &&
			configuration.maximumRadiusDip >= configuration.minimumRadiusDip &&
			IsFinitePositive(configuration.glowExtentDip) &&
			std::isfinite(configuration.fadeStartFraction) &&
			configuration.fadeStartFraction > 0.0f &&
			configuration.fadeStartFraction < 1.0f &&
			IsFinitePositive(configuration.convergenceSeconds) &&
			IsFinitePositive(configuration.positionResponseSeconds);
	}

	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float appendedArcLength,
		float distanceSinceLastEmission, uint32_t remainingFrameBudget,
		const LaserParticleConfig& configuration) noexcept
	{
		LaserParticleEmissionSchedule result;
		if (!IsValidLaserParticleConfig(configuration)) return result;
		const float spacing = 1.0f / configuration.particlesPerPixel;
		const float appended = std::isfinite(appendedArcLength)
			? std::max(appendedArcLength, 0.0f) : 0.0f;
		const float carry = std::isfinite(distanceSinceLastEmission)
			? std::clamp(distanceSinceLastEmission, 0.0f, spacing) : 0.0f;
		const float total = carry + appended;
		const uint32_t available = static_cast<uint32_t>(std::min(
			std::floor(static_cast<double>(total) / static_cast<double>(spacing)),
			static_cast<double>((std::numeric_limits<uint32_t>::max)())));
		result.count = std::min(available, remainingFrameBudget);
		result.distanceSinceLastEmission = std::fmod(total, spacing);
		if (!std::isfinite(result.distanceSinceLastEmission))
			result.distanceSinceLastEmission = 0.0f;
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

	LaserParticleArcAdvance AdvanceLaserParticleArc(float arcLength, float pathEndArcLength,
		float traveledDistance, float maximumTravelDistance, float speed,
		float motionDeltaSeconds) noexcept
	{
		LaserParticleArcAdvance result;
		result.arcLength = std::isfinite(arcLength) ? std::max(arcLength, 0.0f) : 0.0f;
		result.traveledDistance = std::isfinite(traveledDistance)
			? std::max(traveledDistance, 0.0f) : 0.0f;
		const float pathEnd = std::isfinite(pathEndArcLength)
			? std::max(pathEndArcLength, 0.0f) : result.arcLength;
		const float maximumTravel = std::isfinite(maximumTravelDistance)
			? std::max(maximumTravelDistance, 0.0f) : 0.0f;
		const float remainingTravel = std::max(maximumTravel - result.traveledDistance, 0.0f);
		const float delta = std::min(std::max(speed, 0.0f) *
			std::max(motionDeltaSeconds, 0.0f), remainingTravel);
		const float newArcLength = std::min(result.arcLength + delta, pathEnd);
		const float actualAdvance = std::max(newArcLength - result.arcLength, 0.0f);
		result.arcLength = newArcLength;
		result.traveledDistance += actualAdvance;
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

	bool LaserParticleConvergesToEdge(uint32_t seed) noexcept
	{
		return (MixParticleSeed(seed ^ 0xA511E9B3u) & 0xFFFFu) < 49152u;
	}

	void LaserParticleDirtyTracker::Add(
		LaserParticlePathHandle path, RECT bounds, int64_t expiresQpc) noexcept
	{
		if (!path.IsValid() || bounds.left >= bounds.right ||
			bounds.top >= bounds.bottom || expiresQpc <= 0) return;
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

	void LaserParticleDirtyTracker::EndPath(
		LaserParticlePathHandle path, int64_t expiresQpc) noexcept
	{
		if (!path.IsValid()) return;
		for (Region& region : regions_)
		{
			if (!region.active || region.path != path) continue;
			region.expiresQpc = std::min(region.expiresQpc, expiresQpc);
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

	uint32_t LaserParticleSystem::AppendPathPoints(LaserParticlePathHandle path,
		std::span<const LaserParticlePathPoint> points) noexcept
	{
		if (!available_ || !IsCurrentPath(path) || points.empty()) return 0;
		PathSlotState& slot = pathSlots_[path.slot];
		const uint32_t appendCount = ClampLaserParticlePathAppendCount(
			slot.header.pointCount, static_cast<uint32_t>(std::min<size_t>(
				points.size(), (std::numeric_limits<uint32_t>::max)())));
		if (appendCount == 0)
		{
			slot.overflowed = true;
			return 0;
		}

		const uint32_t baseElement = path.slot * kLaserParticlePathPointCapacity +
			slot.header.pointCount;
		D3D11_BOX destinationBox = {};
		destinationBox.left = baseElement * sizeof(LaserParticlePathPoint);
		destinationBox.right = destinationBox.left +
			appendCount * sizeof(LaserParticlePathPoint);
		destinationBox.top = 0;
		destinationBox.bottom = 1;
		destinationBox.front = 0;
		destinationBox.back = 1;
		context_->UpdateSubresource(pathPointBuffer_.Get(), 0, &destinationBox,
			points.data(), 0, 0);
		slot.header.pointCount += appendCount;
		slot.overflowed = appendCount < points.size();
		return appendCount;
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
		if (!slot.hasEmitted)
		{
			slot.header.active = 0;
			return;
		}
		slot.retireSeconds = configuration_.convergenceSeconds;
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
		constants.dpiScale = dpiScale_;
		constants.speedResponseSeconds = configuration_.speedResponseSeconds;
		constants.minimumSpeed = configuration_.minimumSpeedDipPerSecond * dpiScale_;
		constants.maximumSpeed = configuration_.maximumSpeedDipPerSecond * dpiScale_;
		constants.inputSpeedInfluence = configuration_.inputSpeedInfluence;
		constants.spreadSeconds = configuration_.spreadSeconds;
		constants.fadeStartFraction = configuration_.fadeStartFraction;
		constants.convergenceSeconds = configuration_.convergenceSeconds;
		constants.positionResponseSeconds = configuration_.positionResponseSeconds;
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
		constants.startArcLength = std::max(request.startArcLength, 0.0f);
		constants.endArcLength = std::max(request.endArcLength, constants.startArcLength);
		constants.dpiScale = dpiScale_;
		constants.maximumLateralExtra =
			configuration_.maximumLateralExtraDip * dpiScale_;
		constants.minimumLifetime = configuration_.minimumLifetimeSeconds;
		constants.maximumLifetime = configuration_.maximumLifetimeSeconds;
		constants.minimumTravel = configuration_.minimumTravelDip * dpiScale_;
		constants.maximumTravel = configuration_.maximumTravelDip * dpiScale_;
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
