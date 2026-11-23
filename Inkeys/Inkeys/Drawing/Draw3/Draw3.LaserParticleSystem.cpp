module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <d3d11.h>
#include <span>
#include <windows.h>

module Inkeys.Drawing.Draw3.laser_particles;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元只负责 GPU 粒子资源和 Compute Shader 调度。
	namespace
	{
		struct LaserParticleUpdateConstants
		{
			uint32_t start;
			uint32_t count;
			uint32_t milliseconds;
			uint32_t mode;
			float shrinkStartTravelRatio;
			float endRadiusScale;
			uint32_t group;
			uint32_t dissolveBirthMilliseconds;
			uint32_t gatherMilliseconds;
			float gatherProgress;
			float dissolveEndRadiusScale;
			float dissolveWhiteFadeSeconds;
		};
		static_assert(sizeof(LaserParticleUpdateConstants) == 48);
		static_assert(offsetof(LaserParticleUpdateConstants, shrinkStartTravelRatio) == 16);

		struct LaserParticleEmitConstants
		{
			uint32_t start;
			uint32_t count;
			uint32_t milliseconds;
			uint32_t padding;
		};
		static_assert(sizeof(LaserParticleEmitConstants) == 16);
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

		D3D11_BUFFER_DESC bufferDescription = {};
		bufferDescription.ByteWidth = static_cast<UINT>(
			sizeof(LaserGpuParticle) * kLaserParticleCapacity);
		bufferDescription.Usage = D3D11_USAGE_DEFAULT;
		bufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
			D3D11_BIND_UNORDERED_ACCESS;
		bufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDescription.StructureByteStride = sizeof(LaserGpuParticle);
		if (FAILED(device_->CreateBuffer(&bufferDescription, nullptr,
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

		bufferDescription = {};
		bufferDescription.ByteWidth = sizeof(LaserParticleSeed) * kLaserParticleCapacity;
		bufferDescription.Usage = D3D11_USAGE_DEFAULT;
		bufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDescription.StructureByteStride = sizeof(LaserParticleSeed);
		if (FAILED(device_->CreateBuffer(&bufferDescription, nullptr, seedBuffer_.ReleaseAndGetAddressOf())) ||
			FAILED(device_->CreateShaderResourceView(seedBuffer_.Get(), &srvDescription, seedSRV_.ReleaseAndGetAddressOf())) ||
			!ring_.Initialize())
		{
			Release();
			return false;
		}
		// 从空环开始；每个槽先由 EmitCS 完整写入，随后才允许 Update/VS 读取。
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
		seedSRV_.Reset();
		seedBuffer_.Reset();
		context_.Reset();
		device_.Reset();
		Reset();
	}

	void LaserParticleSystem::Configure(const LaserParticleConfig& configuration) noexcept
	{
		configuration_ = IsValidLaserParticleConfig(configuration) ? configuration : LaserParticleConfig{};
	}

	bool LaserParticleSystem::IsAvailable() const noexcept
	{
		return available_;
	}

	void LaserParticleSystem::Step(int64_t nowQpc, int64_t qpcFrequency,
		std::span<const LaserParticleSeed> seeds, uint32_t dissolveSeedCount,
		LaserDissolveParticleControl dissolve) noexcept
	{
		if (!available_ || qpcFrequency <= 0 || nowQpc <= 0) return;
		if (clockOriginQpc_ == 0) clockOriginQpc_ = nowQpc;
		const auto milliseconds = static_cast<uint64_t>(static_cast<double>(
			(std::max)(nowQpc - clockOriginQpc_, int64_t{0})) * 1000.0 / qpcFrequency);
		if (milliseconds > lastMilliseconds_ &&
			milliseconds - lastMilliseconds_ > (std::numeric_limits<uint32_t>::max)())
			ring_.Clear(); // 长时间挂起跨过了 GPU 时钟整圈，旧槽已全部过期。
		const auto range = ring_.Range();
		if (range.count == 0 && seeds.empty()) return;

		const auto dispatch = [&](ID3D11Buffer* constantBuffer, const auto& constants,
			ID3D11ComputeShader* shader, uint32_t count)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context_->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
				return false;
			std::memcpy(mapped.pData, &constants, sizeof(constants));
			context_->Unmap(constantBuffer, 0);
			context_->CSSetConstantBuffers(0, 1, &constantBuffer);
			context_->CSSetShader(shader, nullptr, 0);
			context_->Dispatch((count + 63u) / 64u, 1, 1);
			return true;
		};

		BindComputeParticles();
		const auto gpuMilliseconds = static_cast<uint32_t>(milliseconds);
		bool succeeded = true;
		if (range.count != 0)
		{
			const uint32_t gatherMilliseconds = dissolve.gatherStartQpc > 0 ?
				static_cast<uint32_t>(static_cast<uint64_t>(static_cast<double>(
					(std::max)(dissolve.gatherStartQpc - clockOriginQpc_, int64_t{0})) *
					1000.0 / qpcFrequency)) : 0u;
			succeeded = DispatchUpdate(range.start, range.count, gpuMilliseconds,
				dissolve.stop ? 2u : dissolve.gatherStartQpc > 0 ? 1u : 0u, 0u,
				gatherMilliseconds, dissolve.gatherProgress);
			if (dissolve.stop) ring_.EndLatestDissolve(milliseconds);
			else if (dissolve.gatherEndQpc > clockOriginQpc_)
			{
				// GPU 汇聚时使用冻结年龄，CPU 环也须保留这一批到汇聚结束。
				const auto gatherEndMilliseconds = static_cast<uint64_t>(std::ceil(
					static_cast<double>(dissolve.gatherEndQpc - clockOriginQpc_) * 1000.0 / qpcFrequency));
				ring_.ExtendLatestDissolve(gatherEndMilliseconds);
			}
			// GPU 先标记本帧到期槽，收缩后的环才用于绘制；未释放槽也保留到自身截止时刻。
			ring_.Expire(milliseconds);
		}
		if (succeeded && !seeds.empty())
		{
			seeds = seeds.last((std::min)(seeds.size(), static_cast<size_t>(kLaserParticleCapacity)));
			const auto count = static_cast<uint32_t>(seeds.size());
			dissolveSeedCount = (std::min)(dissolveSeedCount, count);
			const D3D11_BOX upload = { 0, 0, 0, static_cast<UINT>(seeds.size_bytes()), 1, 1 };
			context_->UpdateSubresource(seedBuffer_.Get(), 0, &upload, seeds.data(), 0, 0);
			ID3D11ShaderResourceView* seedView = seedSRV_.Get();
			context_->CSSetShaderResources(0, 1, &seedView);
			const LaserParticleEmitConstants constants = { ring_.Cursor(), count, gpuMilliseconds, 0 };
			succeeded = dispatch(emitConstants_.Get(), constants, emitShader_.Get(), count);
			if (succeeded)
			{
				// 一次上传和派发；环内独立记飘散批次，汇聚可提前结束而不影响其他粒子。
				const auto append = [&](std::span<const LaserParticleSeed> batch, bool isDissolve)
				{
					uint32_t maximumLifetime = 0;
					for (const auto& seed : batch)
						maximumLifetime = (std::max)(maximumLifetime,
							seed.releaseDelayMilliseconds + seed.lifetimeMilliseconds);
					ring_.Append(static_cast<uint32_t>(batch.size()), milliseconds + maximumLifetime, isDissolve);
				};
				append(seeds.first(count - dissolveSeedCount), false);
				append(seeds.last(dissolveSeedCount), true);
				if (dissolveSeedCount != 0) dissolveBirthMilliseconds_ = gpuMilliseconds;
			}
		}
		UnbindComputeResources();
		lastMilliseconds_ = milliseconds;
		if (!succeeded || FAILED(device_->GetDeviceRemovedReason())) Release();
	}

	bool LaserParticleSystem::DispatchUpdate(uint32_t start, uint32_t count, uint32_t milliseconds,
		uint32_t mode, uint32_t group, uint32_t gatherMilliseconds, float gatherProgress) noexcept
	{
		const LaserParticleUpdateConstants constants = {
			start, count, milliseconds, mode, configuration_.shrinkStartTravelRatio,
			configuration_.endRadiusScale, group, dissolveBirthMilliseconds_, gatherMilliseconds,
			gatherProgress, kLaserDissolveParameters.endRadiusScale, kLaserDissolveParameters.whiteFadeSeconds };
		ID3D11Buffer* buffer = updateConstants_.Get();
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		std::memcpy(mapped.pData, &constants, sizeof(constants));
		context_->Unmap(buffer, 0);
		context_->CSSetConstantBuffers(0, 1, &buffer);
		context_->CSSetShader(updateShader_.Get(), nullptr, 0);
		context_->Dispatch((count + 63u) / 64u, 1, 1);
		return true;
	}

	void LaserParticleSystem::BindComputeParticles() noexcept
	{
		ID3D11ShaderResourceView* nullParticle = nullptr;
		context_->VSSetShaderResources(8, 1, &nullParticle);
		ID3D11UnorderedAccessView* unorderedView = particleUAV_.Get();
		context_->CSSetUnorderedAccessViews(0, 1, &unorderedView, nullptr);
	}

	void LaserParticleSystem::RemoveGroup(uint32_t group, bool endDissolve) noexcept
	{
		const auto range = ring_.Range();
		if (!available_ || range.count == 0) return;
		// 撤回可能晚于本帧更新；独立派发只置死对应组，不推进任何粒子时钟。
		BindComputeParticles();
		const bool succeeded = DispatchUpdate(range.start, range.count, 0, 3u, group, 0, 0.0f);
		UnbindComputeResources();
		if (succeeded && endDissolve)
		{
			ring_.EndLatestDissolve(lastMilliseconds_);
			ring_.Expire(lastMilliseconds_);
		}
		if (!succeeded || FAILED(device_->GetDeviceRemovedReason())) Release();
	}

	void LaserParticleSystem::Reset() noexcept
	{
		ring_.Clear();
		clockOriginQpc_ = 0;
		lastMilliseconds_ = 0;
		dissolveBirthMilliseconds_ = 0;
	}

	void LaserParticleSystem::UnbindComputeResources() noexcept
	{
		ID3D11ShaderResourceView* nullSeed = nullptr;
		context_->CSSetShaderResources(0, 1, &nullSeed);
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
