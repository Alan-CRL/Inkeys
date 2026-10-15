module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.Performance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <span>
#include <windows.h>

module Inkeys.Drawing.Draw3.laser_particles;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元只负责 GPU 粒子资源和 Compute Shader 调度。
	namespace
	{
		constexpr float kMinimumPositiveValue = 0.0001f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kMaximumMotionDeltaSeconds = 1.0f / 30.0f;

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
			float minimumRadius = 0.45f;
			float maximumRadius = 2.2f;
			float minimumBrightness = 0.42f;
			float maximumBrightness = 1.0f;
			float breathingAmplitude = 0.18f;
			float minimumBreathingFrequencyHz = 0.8f;
			float maximumBreathingFrequencyHz = 1.4f;
			float breathingRampSeconds = 0.20f;
			float sizeDistributionExponent = 1.6f;
			float sizeBrightnessCorrelation = 0.72f;
			float sizeTravelCorrelation = 0.30f;
			float padding[2] = {};
		};

		static_assert(sizeof(LaserParticleEmitConstants) == 112);
		static_assert(sizeof(LaserParticleEmitConstants) % 16 == 0);
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

		if (FAILED(PerformanceGraphicsCall(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw3_laserparticlesystem_createcomputeshader_1"), [&] { return TraceArguments(); }, [&] { return device_->CreateComputeShader(updateShader.data, updateShader.size,
			nullptr, updateShader_.ReleaseAndGetAddressOf()); })) ||
			FAILED(PerformanceGraphicsCall(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw3_laserparticlesystem_createcomputeshader_2"), [&] { return TraceArguments(); }, [&] { return device_->CreateComputeShader(emitShader.data, emitShader.size,
				nullptr, emitShader_.ReleaseAndGetAddressOf()); })))
		{
			Release();
			return false;
		}

		D3D11_BUFFER_DESC bufferDescription = {};
		bufferDescription.ByteWidth = static_cast<UINT>(
			sizeof(LaserGpuParticle) * kLaserParticleCapacity);
		bufferDescription.Usage = D3D11_USAGE_DEFAULT;
		bufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
			D3D11_BIND_UNORDERED_ACCESS;
		bufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDescription.StructureByteStride = sizeof(LaserGpuParticle);
		if (FAILED(TraceCreateResource(particleBuffer_, PerformanceGraphicsSite("laser_particle_create_particleBuffer_"), [&] { return device_->CreateBuffer(&bufferDescription, nullptr,
			particleBuffer_.ReleaseAndGetAddressOf()); })))
		{
			Release();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription = {};
		srvDescription.Format = DXGI_FORMAT_UNKNOWN;
		srvDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDescription.Buffer.NumElements = kLaserParticleCapacity;
		if (FAILED(PerformanceGraphicsCall(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw3_laserparticlesystem_createshaderresourceview_4"), [&] { return TraceArguments(); }, [&] { return device_->CreateShaderResourceView(particleBuffer_.Get(),
			&srvDescription, particleSRV_.ReleaseAndGetAddressOf()); })))
		{
			Release();
			return false;
		}
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription = {};
		uavDescription.Format = DXGI_FORMAT_UNKNOWN;
		uavDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDescription.Buffer.NumElements = kLaserParticleCapacity;
		if (FAILED(PerformanceGraphicsCall(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw3_laserparticlesystem_createunorderedaccessview_5"), [&] { return TraceArguments(); }, [&] { return device_->CreateUnorderedAccessView(particleBuffer_.Get(),
			&uavDescription, particleUAV_.ReleaseAndGetAddressOf()); })))
		{
			Release();
			return false;
		}

		bufferDescription = {};
		bufferDescription.ByteWidth = sizeof(LaserParticleUpdateConstants);
		bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
		bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(TraceCreateResource(updateConstants_, PerformanceGraphicsSite("laser_particle_create_updateConstants_"), [&] { return device_->CreateBuffer(&bufferDescription, nullptr,
			updateConstants_.ReleaseAndGetAddressOf()); })))
		{
			Release();
			return false;
		}
		bufferDescription.ByteWidth = sizeof(LaserParticleEmitConstants);
		if (FAILED(TraceCreateResource(emitConstants_, PerformanceGraphicsSite("laser_particle_create_emitConstants_"), [&] { return device_->CreateBuffer(&bufferDescription, nullptr,
			emitConstants_.ReleaseAndGetAddressOf()); })))
		{
			Release();
			return false;
		}

		available_ = true;
		// 默认缓冲区由 GPU reset 初始化，避免创建 2048 项 CPU 零数组。
		if (!DispatchUpdate(0.0f, 0.0f, true))
		{
			Release();
			return false;
		}
		return true;
	}

	void LaserParticleSystem::Release() noexcept
	{
		if (performance_)
			for (auto* resource : { particleBuffer_.Get(), updateConstants_.Get(), emitConstants_.Get() })
				performance_->RetireResource(resource);
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
		if (FAILED(PerformanceGraphicsCall(performance_, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_map_1"), [&] { return [&] { auto arguments = TraceArguments(updateConstants_.Get()); arguments.subresource = 0; arguments.mapType = D3D11_MAP_WRITE_DISCARD; arguments.flags = 0; return arguments; }(); }, [&] { return context_->Map(updateConstants_.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped); }))) return false;
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_constant_memcpy_1"), [&] { return TraceArguments(updateConstants_.Get(), sizeof(constants)); }, [&] { std::memcpy(mapped.pData, &constants, sizeof(constants)); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_unmap_2"), [&] { return TraceArguments(updateConstants_.Get()); }, [&] { context_->Unmap(updateConstants_.Get(), 0); });

		// 粒子 SRV 先从 VS 解绑，再把同一缓冲区安全绑定为 Compute UAV。
		ID3D11ShaderResourceView* nullParticle[] = { nullptr };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_vssetshaderresources_3"), [&] { return TraceArguments(nullptr); }, [&] { context_->VSSetShaderResources(8, 1, nullParticle); });
		ID3D11UnorderedAccessView* unorderedViews[] = { particleUAV_.Get() };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetunorderedaccessviews_4"), [&] { return TraceArguments(particleBuffer_.Get()); }, [&] { context_->CSSetUnorderedAccessViews(0, 1, unorderedViews, nullptr); });
		ID3D11Buffer* constantBuffers[] = { updateConstants_.Get() };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetconstantbuffers_5"), [&] { return TraceArguments((constantBuffers)[0]); }, [&] { context_->CSSetConstantBuffers(0, 1, constantBuffers); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetshader_6"), [&] { return TraceArguments(nullptr); }, [&] { context_->CSSetShader(updateShader_.Get(), nullptr, 0); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_dispatch_7"), [&] { return TraceArguments(particleBuffer_.Get(), 0, (kLaserParticleCapacity + 63u) / 64u, 1); }, [&] { context_->Dispatch((kLaserParticleCapacity + 63u) / 64u, 1, 1); });
		UnbindComputeResources();
		return true;
	}

	void LaserParticleSystem::Step(float wallDeltaSeconds,
		float motionDeltaSeconds, bool simulateExisting,
		std::span<const LaserParticleEmissionRequest> emissionRequests) noexcept
	{
		if (!available_ || (!simulateExisting && emissionRequests.empty())) return;

		// 同一帧 update/emit 共享一次 UAV 绑定，逐请求常量和 Dispatch 顺序保持不变。
		ID3D11ShaderResourceView* nullParticle[] = { nullptr };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_vssetshaderresources_8"), [&] { return TraceArguments(nullptr); }, [&] { context_->VSSetShaderResources(8, 1, nullParticle); });
		ID3D11UnorderedAccessView* unorderedViews[] = { particleUAV_.Get() };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetunorderedaccessviews_9"), [&] { return TraceArguments(particleBuffer_.Get()); }, [&] { context_->CSSetUnorderedAccessViews(0, 1, unorderedViews, nullptr); });

		if (simulateExisting)
		{
			LaserParticleUpdateConstants constants;
			constants.wallDeltaSeconds = std::isfinite(wallDeltaSeconds)
				? std::max(wallDeltaSeconds, 0.0f) : 0.0f;
			constants.motionDeltaSeconds = std::isfinite(motionDeltaSeconds)
				? std::clamp(motionDeltaSeconds, 0.0f,
					kMaximumMotionDeltaSeconds) : 0.0f;
			constants.shrinkStartTravelRatio =
				configuration_.shrinkStartTravelRatio;
			constants.endRadiusScale = configuration_.endRadiusScale;

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(PerformanceGraphicsCall(performance_, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_map_10"), [&] { return [&] { auto arguments = TraceArguments(updateConstants_.Get()); arguments.subresource = 0; arguments.mapType = D3D11_MAP_WRITE_DISCARD; arguments.flags = 0; return arguments; }(); }, [&] { return context_->Map(updateConstants_.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped); })))
			{
				// 无法推进存量槽时直接降级，避免旧 alive 槽在后续发射时重现。
				UnbindComputeResources();
				Release();
				return;
			}
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_constant_memcpy_2"), [&] { return TraceArguments(updateConstants_.Get(), sizeof(constants)); }, [&] { std::memcpy(mapped.pData, &constants, sizeof(constants)); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_unmap_11"), [&] { return TraceArguments(updateConstants_.Get()); }, [&] { context_->Unmap(updateConstants_.Get(), 0); });
			ID3D11Buffer* constantBuffers[] = { updateConstants_.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetconstantbuffers_12"), [&] { return TraceArguments((constantBuffers)[0]); }, [&] { context_->CSSetConstantBuffers(0, 1, constantBuffers); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetshader_13"), [&] { return TraceArguments(nullptr); }, [&] { context_->CSSetShader(updateShader_.Get(), nullptr, 0); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_dispatch_14"), [&] { return TraceArguments(particleBuffer_.Get(), 0, (kLaserParticleCapacity + 63u) / 64u, 1); }, [&] { context_->Dispatch((kLaserParticleCapacity + 63u) / 64u, 1, 1); });
		}

		for (const LaserParticleEmissionRequest& request : emissionRequests)
		{
			if (request.count == 0 ||
				!std::isfinite(request.positionX) ||
				!std::isfinite(request.positionY) ||
				!std::isfinite(request.tangentX) ||
				!std::isfinite(request.tangentY))
				continue;
			const float tangentLength = std::hypot(
				request.tangentX, request.tangentY);
			if (tangentLength <= kMinimumPositiveValue) continue;

			const uint32_t count = std::min(
				request.count, kLaserParticleCapacity);
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
			constants.minimumLifetimeSeconds =
				configuration_.minimumLifetimeSeconds;
			constants.maximumLifetimeSeconds =
				configuration_.maximumLifetimeSeconds;
			constants.minimumLaunchSpeed =
				configuration_.minimumLaunchSpeedDipPerSecond * dpiScale_;
			constants.maximumLaunchSpeed =
				configuration_.maximumLaunchSpeedDipPerSecond * dpiScale_;
			constants.maximumDeflectionRadians =
				configuration_.maximumDeflectionAngleDegrees * kPi / 180.0f;
			constants.minimumRadius =
				configuration_.minimumRadiusDip * dpiScale_;
			constants.maximumRadius =
				configuration_.maximumRadiusDip * dpiScale_;
			constants.minimumBrightness = configuration_.minimumBrightness;
			constants.maximumBrightness = configuration_.maximumBrightness;
			constants.breathingAmplitude = configuration_.breathingAmplitude;
			constants.minimumBreathingFrequencyHz =
				configuration_.minimumBreathingFrequencyHz;
			constants.maximumBreathingFrequencyHz =
				configuration_.maximumBreathingFrequencyHz;
			constants.breathingRampSeconds =
				configuration_.breathingRampSeconds;
			constants.sizeDistributionExponent =
				configuration_.sizeDistributionExponent;
			constants.sizeBrightnessCorrelation =
				configuration_.sizeBrightnessCorrelation;
			constants.sizeTravelCorrelation =
				configuration_.sizeTravelCorrelation;

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(PerformanceGraphicsCall(performance_, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_map_15"), [&] { return [&] { auto arguments = TraceArguments(emitConstants_.Get()); arguments.subresource = 0; arguments.mapType = D3D11_MAP_WRITE_DISCARD; arguments.flags = 0; return arguments; }(); }, [&] { return context_->Map(emitConstants_.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped); }))) continue;
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_constant_memcpy_3"), [&] { return TraceArguments(emitConstants_.Get(), sizeof(constants)); }, [&] { std::memcpy(mapped.pData, &constants, sizeof(constants)); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_unmap_16"), [&] { return TraceArguments(emitConstants_.Get()); }, [&] { context_->Unmap(emitConstants_.Get(), 0); });
			ID3D11Buffer* constantBuffers[] = { emitConstants_.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetconstantbuffers_17"), [&] { return TraceArguments((constantBuffers)[0]); }, [&] { context_->CSSetConstantBuffers(0, 1, constantBuffers); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetshader_18"), [&] { return TraceArguments(nullptr); }, [&] { context_->CSSetShader(emitShader_.Get(), nullptr, 0); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_dispatch_19"), [&] { return TraceArguments(particleBuffer_.Get(), 0, (count + 63u) / 64u, 1); }, [&] { context_->Dispatch((count + 63u) / 64u, 1, 1); });
			spawnCursor_ = (spawnCursor_ + count) % kLaserParticleCapacity;
		}

		UnbindComputeResources();
	}

	void LaserParticleSystem::Reset() noexcept
	{
		if (!available_) return;
		if (!DispatchUpdate(0.0f, 0.0f, true))
		{
			Release(); // reset 失败时禁用粒子，避免之后重新绘制未清空的旧槽。
			return;
		}
		spawnCursor_ = 0;
	}

	void LaserParticleSystem::UnbindComputeResources() noexcept
	{
		ID3D11UnorderedAccessView* nullUav[] = { nullptr };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetunorderedaccessviews_20"), [&] { return TraceArguments(particleBuffer_.Get()); }, [&] { context_->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr); });
		ID3D11Buffer* nullBuffer[] = { nullptr };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetconstantbuffers_21"), [&] { return TraceArguments((nullBuffer)[0]); }, [&] { context_->CSSetConstantBuffers(0, 1, nullBuffer); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LaserData,
			PerformanceGraphicsSite("laser_particle_cssetshader_22"), [&] { return TraceArguments(nullptr); }, [&] { context_->CSSetShader(nullptr, nullptr, 0); });
	}

	ID3D11ShaderResourceView* LaserParticleSystem::ParticleShaderResourceView() const noexcept
	{
		return available_ ? particleSRV_.Get() : nullptr;
	}
}
