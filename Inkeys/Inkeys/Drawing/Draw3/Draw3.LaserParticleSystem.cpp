module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <d3d11.h>
#include <span>
#include <utility>
#include <windows.h>

module Inkeys.Drawing.Draw3.laser_particles;

import Inkeys.Drawing.Draw3.diagnostics;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元只负责 GPU 粒子池资源、出生记录的上传和实例化绘制。
	namespace
	{
		// 与 laserParticleVS.hlsl 的 LaserParticleDrawBuffer 同步。
		struct LaserParticleDrawConstants
		{
			float clipScale[2];
			uint32_t activeStart;
			uint32_t drawKind;
			uint32_t nowMilliseconds;
			uint32_t gatherMilliseconds;
			uint32_t gatherBatch;
			float gatherRemaining;
			float shrinkStartTravelRatio;
			float trailEndRadiusScale;
			float driftEndRadiusScale;
			float inverseWhiteFadeSeconds;
			float dipPerPixel;
			uint32_t capacityMask;
			float padding[2];
		};
		static_assert(sizeof(LaserParticleDrawConstants) == 64);
		static_assert(offsetof(LaserParticleDrawConstants, nowMilliseconds) == 16);
		static_assert(offsetof(LaserParticleDrawConstants, dipPerPixel) == 48);
		static_assert(offsetof(LaserParticleDrawConstants, capacityMask) == 52);

		// 与 laserParticleSpawnCS.hlsl 的 b0 和两个原子计数器逐字段同步。
		struct LaserClearSpawnConstants
		{
			int32_t rectOrigin[2];
			uint32_t rectSize[2];
			uint32_t mode, batchStart, reservedCount, capacityMask;
			uint32_t maximumSeeds, seedBase, bornMilliseconds, minimumAlpha;
			float dpiScale, dipPerPixel, seedsPerSquareDip;
			uint32_t durationMilliseconds;
			float front[4];
			float wind[2], windSpeedPxPerSecond, meanderPx;
			float minimumWindSpeedFraction, maximumWindSpeedFraction, minimumRadiusDip, maximumRadiusDip;
			float radiusDistributionExponent, accelerationPerSecond, gustVariation, accelerationJitter;
			float minimumBurstDip, maximumBurstDip, edgeHalfWidth, noiseAmplitude;
			uint32_t minimumLifetimeMilliseconds, maximumLifetimeMilliseconds, padding[2];
		};
		static_assert(sizeof(LaserClearSpawnConstants) == 160);
		static_assert(offsetof(LaserClearSpawnConstants, rectSize) == 8);
		static_assert(offsetof(LaserClearSpawnConstants, mode) == 16);
		static_assert(offsetof(LaserClearSpawnConstants, batchStart) == 20);
		static_assert(offsetof(LaserClearSpawnConstants, reservedCount) == 24);
		static_assert(offsetof(LaserClearSpawnConstants, capacityMask) == 28);
		static_assert(offsetof(LaserClearSpawnConstants, maximumSeeds) == 32);
		static_assert(offsetof(LaserClearSpawnConstants, bornMilliseconds) == 40);
		static_assert(offsetof(LaserClearSpawnConstants, dpiScale) == 48);
		static_assert(offsetof(LaserClearSpawnConstants, durationMilliseconds) == 60);
		static_assert(offsetof(LaserClearSpawnConstants, front) == 64);
		static_assert(offsetof(LaserClearSpawnConstants, wind) == 80);
		static_assert(offsetof(LaserClearSpawnConstants, minimumWindSpeedFraction) == 96);
		static_assert(offsetof(LaserClearSpawnConstants, radiusDistributionExponent) == 112);
		static_assert(offsetof(LaserClearSpawnConstants, minimumBurstDip) == 128);
		static_assert(offsetof(LaserClearSpawnConstants, minimumLifetimeMilliseconds) == 144);

		struct LaserClearSpawnCounters
		{
			uint32_t inked, written, padding[2];
		};
		static_assert(sizeof(LaserClearSpawnCounters) == 16);
		static_assert(offsetof(LaserClearSpawnCounters, inked) == 0);
		static_assert(offsetof(LaserClearSpawnCounters, written) == 4);

		constexpr uint32_t kNoParticleGroup = 0xFFFFFFFEu; // 已作废的槽，不与任何分组号相同
		constexpr uint32_t kMaximumDissolveBatch = (1u << 30) - 1u; // 批次号与种类共用一个 32 位字段
	}

	bool LaserParticleSystem::Initialize(ID3D11Device* device,
		ID3D11DeviceContext* context, LaserParticleShaderBytecode vertexShader,
		LaserParticleShaderBytecode pixelShader, LaserParticleShaderBytecode spawnShader) noexcept
	{
		Release();
		++deviceGeneration_;
		if (!device || !context || !vertexShader.data || vertexShader.size == 0 ||
			!pixelShader.data || pixelShader.size == 0 ||
			device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) return false;
		// 这里只记下设备与字节码；着色器、粒子池和暂存区按需创建。
		device_ = device;
		context_ = context;
		vertexBytecode_ = vertexShader;
		pixelBytecode_ = pixelShader;
		spawnBytecode_ = spawnShader;
		spawnUnavailable_ = !spawnShader.data || spawnShader.size == 0;
		if (spawnUnavailable_)
			DiagnosticOutput::Printf(DiagnosticOutput::OutWarn, "[Draw3.LaserParticles] clear spawn bytecode unavailable\n");
		available_ = true;
		return true;
	}

	bool LaserParticleSystem::EnsureDrawResources() noexcept
	{
		if (drawConstants_) return true;
		if (!staging_) staging_.reset(new (std::nothrow) LaserGpuParticle[kLaserParticleStagingChunk]);
		if (!staging_) return false;

		D3D11_BUFFER_DESC constantDescription = {};
		constantDescription.ByteWidth = sizeof(LaserParticleDrawConstants);
		constantDescription.Usage = D3D11_USAGE_DYNAMIC;
		constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		const bool created =
			SUCCEEDED(device_->CreateVertexShader(vertexBytecode_.data, vertexBytecode_.size,
				nullptr, vertexShader_.ReleaseAndGetAddressOf())) &&
			SUCCEEDED(device_->CreatePixelShader(pixelBytecode_.data, pixelBytecode_.size,
				nullptr, pixelShader_.ReleaseAndGetAddressOf())) &&
			SUCCEEDED(device_->CreateBuffer(&constantDescription, nullptr, drawConstants_.ReleaseAndGetAddressOf()));
		if (!created) Release(); // 共享绘制资源失败时，本次设备会话内两池都不可用。
		return created;
	}

	HRESULT LaserParticleSystem::CreatePool(ParticlePool& pool, uint32_t capacity, bool unorderedAccess) noexcept
	{
		// DEFAULT + UpdateSubresource：不依赖动态结构化缓冲区的 NO_OVERWRITE（Win7 没有），
		// 也是兆芯兼容上传采用的方式。槽只在被写过之后才进入活跃区间，所以不需要初始数据。
		ParticlePool candidate;
		D3D11_BUFFER_DESC description = {};
		description.ByteWidth = static_cast<UINT>(sizeof(LaserGpuParticle) * capacity);
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (unorderedAccess) description.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		description.StructureByteStride = sizeof(LaserGpuParticle);
		const HRESULT bufferResult = device_->CreateBuffer(&description, nullptr, candidate.buffer.GetAddressOf());
		if (FAILED(bufferResult)) return bufferResult;

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription = {};
		viewDescription.Format = DXGI_FORMAT_UNKNOWN;
		viewDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		viewDescription.Buffer.NumElements = capacity;
		const HRESULT viewResult = device_->CreateShaderResourceView(candidate.buffer.Get(), &viewDescription,
			candidate.srv.GetAddressOf());
		if (FAILED(viewResult)) return viewResult;
		candidate.slotGroups.reset(new (std::nothrow) uint32_t[capacity]);
		if (!candidate.slotGroups || !candidate.ring.Initialize(capacity)) return E_OUTOFMEMORY;
		candidate.capacity = capacity;
		candidate.capacityMask = capacity - 1u;
		pool = std::move(candidate);
		return S_OK;
	}

	bool LaserParticleSystem::EnsureResources(LaserParticlePass pass) noexcept
	{
		const bool trail = pass == LaserParticlePass::OverBody;
		auto& pool = trail ? trailPool_ : driftPool_;
		if (!available_ || (trail ? trailUnavailable_ : driftUnavailable_)) return false;
		if (pool.buffer) return true;
		if (!EnsureDrawResources()) return false;
		if (trail)
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			const HRESULT result = failTrailCreationForTest_ ? E_OUTOFMEMORY : CreatePool(pool, kLaserTrailPoolCapacity, false);
#else
			const HRESULT result = CreatePool(pool, kLaserTrailPoolCapacity, false);
#endif
			trailUnavailable_ = FAILED(result);
			return !trailUnavailable_;
		}

		const int requested = static_cast<int>(limit_);
		constexpr int low = static_cast<int>(LaserParticleLimit::Low);
		HRESULT lastFailure = S_OK;
		for (int tier = requested; tier >= low; --tier)
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			const HRESULT result = tier > maximumDriftTierForTest_ ? E_OUTOFMEMORY :
				CreatePool(pool, kLaserParticleLimitTiers[static_cast<size_t>(tier)].poolCapacity, true);
#else
			const HRESULT result = CreatePool(pool, kLaserParticleLimitTiers[static_cast<size_t>(tier)].poolCapacity, true);
#endif
			if (SUCCEEDED(result))
			{
				if (tier != requested)
				{
					// 空闲归还后也沿用已成功的档位，避免再次申请本设备建不出的资源。
					limit_ = limitCeiling_ = static_cast<LaserParticleLimit>(tier);
					DiagnosticOutput::Printf(DiagnosticOutput::OutWarn,
						"[Draw3.LaserParticles] drift pool requested=%d actual=%d HRESULT=0x%08lX\n",
						requested, tier, static_cast<unsigned long>(lastFailure));
				}
				return true;
			}
			lastFailure = result;
		}
		limit_ = limitCeiling_ = LaserParticleLimit::Low;
		driftUnavailable_ = true;
		DiagnosticOutput::Printf(DiagnosticOutput::OutWarn,
			"[Draw3.LaserParticles] drift pool requested=%d actual=unavailable HRESULT=0x%08lX\n",
			requested, static_cast<unsigned long>(lastFailure));
		return false;
	}

	void LaserParticleSystem::SetLimit(LaserParticleLimit limit) noexcept
	{
		const auto effective = (std::min)(limit, limitCeiling_);
		if (effective == limit_) return;
		limit_ = effective;
		// 时钟仍服务于行进池，仅归还飘散资源和依赖旧槽的状态。
		ReleaseClearSpawnResources();
		driftPool_ = {};
		gatheringBatch_ = 0;
		clock_.gatherMilliseconds = clock_.gatherBatch = 0;
		clock_.gatherRemaining = 1.0f;
	}

	uint32_t LaserParticleSystem::MaximumSeeds() const noexcept
	{
		return kLaserParticleLimitTiers[static_cast<size_t>(limit_)].maximumSeeds;
	}

	uint32_t LaserParticleSystem::PrepareDriftPool() noexcept
	{
		return EnsureResources(LaserParticlePass::UnderBody) ? MaximumSeeds() : 0u;
	}

	void LaserParticleSystem::Release() noexcept
	{
		++deviceGeneration_;
		available_ = false;
		ReleaseClearSpawnResources();
		trailPool_ = {};
		driftPool_ = {};
		vertexShader_.Reset();
		pixelShader_.Reset();
		drawConstants_.Reset();
		staging_.reset();
		vertexBytecode_ = {};
		pixelBytecode_ = {};
		spawnBytecode_ = {};
		spawnUnavailable_ = false;
		context_.Reset();
		device_.Reset();
		Reset();
		limit_ = LaserParticleLimit::Low;
		limitCeiling_ = LaserParticleLimit::Extreme;
		trailUnavailable_ = driftUnavailable_ = false;
	}

	void LaserParticleSystem::Configure(const LaserParticleConfig& configuration) noexcept
	{
		configuration_ = IsValidLaserParticleConfig(configuration) ? configuration : LaserParticleConfig{};
	}

	bool LaserParticleSystem::IsAvailable() const noexcept
	{
		return available_;
	}

	LaserParticleStepResult LaserParticleSystem::Step(int64_t nowQpc, int64_t qpcFrequency,
		std::span<const LaserParticleSeed> trailSeeds, std::span<const LaserDriftParticle> plainDrift,
		std::span<const LaserDriftParticle> dissolveDrift, float dipPerPixel,
		LaserDissolveParticleControl dissolve, const LaserClearSpawnRequest* clearSpawn) noexcept
	{
		if (!available_ || qpcFrequency <= 0 || nowQpc <= 0) return {};
		if (clockOriginQpc_ == 0) clockOriginQpc_ = nowQpc;
		const auto toMilliseconds = [&](int64_t qpc)
		{
			return static_cast<uint64_t>(static_cast<double>(
				(std::max)(qpc - clockOriginQpc_, int64_t{0})) * 1000.0 / qpcFrequency);
		};
		const uint64_t milliseconds = toMilliseconds(nowQpc);
		if (milliseconds > lastMilliseconds_ &&
			milliseconds - lastMilliseconds_ > (std::numeric_limits<uint32_t>::max)())
		{
			// 长时间挂起跨过了 32 位时钟整圈，旧槽已全部过期。
			pendingClearSpawn_.reset();
			trailPool_.ring.Clear();
			driftPool_.ring.Clear();
		}
		if (trailPool_.ring.Range().count == 0 && driftPool_.ring.Range().count == 0 &&
			trailSeeds.empty() && plainDrift.empty() && dissolveDrift.empty() && !clearSpawn) return {};

		bool succeeded = true;
		if (driftPool_.ring.Range().count != 0)
		{
			if (dissolve.stop)
			{
				// 汇聚完成或飘散被打断：这一批直接作废。槽在环的中段时无法退出区间，写成空槽即可。
				succeeded = KillSlots(driftPool_, driftPool_.ring.LatestDissolveRange());
				driftPool_.ring.EndLatestDissolve(milliseconds);
				gatheringBatch_ = 0;
			}
			else if (dissolve.gatherStartQpc > 0)
			{
				gatheringBatch_ = dissolveBatch_;
				if (dissolve.gatherEndQpc > clockOriginQpc_)
				{
					// 汇聚按冻结时钟求值，这一批要保留到汇聚结束。
					const auto gatherEndMilliseconds = static_cast<uint64_t>(std::ceil(
						static_cast<double>(dissolve.gatherEndQpc - clockOriginQpc_) * 1000.0 / qpcFrequency));
					driftPool_.ring.ExtendLatestDissolve(gatherEndMilliseconds);
					driftPool_.endMilliseconds = (std::max)(driftPool_.endMilliseconds, gatherEndMilliseconds);
				}
			}
			else if (gatheringBatch_ != 0)
			{
				// 汇聚中途被撤掉而没有显式停止：冻结时钟已不成立，整批作废，不让粒子跳回实时位置。
				if (gatheringBatch_ == dissolveBatch_)
				{
					succeeded = KillSlots(driftPool_, driftPool_.ring.LatestDissolveRange());
					driftPool_.ring.EndLatestDissolve(milliseconds);
				}
				gatheringBatch_ = 0;
			}
			driftPool_.ring.Expire(milliseconds);
		}
		trailPool_.ring.Expire(milliseconds);
		const bool gathering = dissolve.gatherStartQpc > 0 && !dissolve.stop && gatheringBatch_ != 0;
		clock_.nowMilliseconds = static_cast<uint32_t>(milliseconds);
		clock_.gatherMilliseconds = gathering ? static_cast<uint32_t>(toMilliseconds(dissolve.gatherStartQpc)) : 0u;
		clock_.gatherBatch = gathering ? gatheringBatch_ : 0u;
		clock_.gatherRemaining = gathering ? 1.0f - std::clamp(dissolve.gatherProgress, 0.0f, 1.0f) : 1.0f;

		LaserParticleStepResult result;
		if (succeeded && !trailSeeds.empty() && EnsureResources(LaserParticlePass::OverBody))
		{
			succeeded = AppendTrailSeeds(trailSeeds, milliseconds);
			result.trailAccepted = succeeded;
		}
		if (succeeded && (!plainDrift.empty() || !dissolveDrift.empty()) && EnsureResources(LaserParticlePass::UnderBody))
			succeeded = AppendDriftParticles(plainDrift, dissolveDrift, dipPerPixel, milliseconds, result);
		if (succeeded && clearSpawn && AppendClearSpawn(*clearSpawn, milliseconds))
			result.clearSpawnAccepted = true;
		if (!available_) return {};
		lastMilliseconds_ = milliseconds;
		if (!succeeded || FAILED(device_->GetDeviceRemovedReason()))
		{
			Release();
			return {}; // 两池都已释放，本帧上传的批次也不再有效。
		}
		return result;
	}

	bool LaserParticleSystem::GpuClearSpawnAvailable() noexcept
	{
		// 在清空 L2 前确认惰性资源可用，创建失败时还能走 CPU 读回。
		return available_ && !driftUnavailable_ && !spawnUnavailable_ &&
			EnsureResources(LaserParticlePass::UnderBody) && EnsureClearSpawnResources();
	}

	bool LaserParticleSystem::EnsureClearSpawnResources() noexcept
	{
		if (spawnConstants_) return true;
		D3D11_UNORDERED_ACCESS_VIEW_DESC particleView = {};
		particleView.Format = DXGI_FORMAT_UNKNOWN;
		particleView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		particleView.Buffer.NumElements = driftPool_.capacity;

		D3D11_BUFFER_DESC counters = {};
		counters.ByteWidth = sizeof(LaserClearSpawnCounters);
		counters.Usage = D3D11_USAGE_DEFAULT;
		counters.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		counters.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

		D3D11_UNORDERED_ACCESS_VIEW_DESC counterView = {};
		counterView.Format = DXGI_FORMAT_R32_TYPELESS;
		counterView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		counterView.Buffer.NumElements = sizeof(LaserClearSpawnCounters) / sizeof(uint32_t);
		counterView.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

		D3D11_BUFFER_DESC readback = {};
		readback.ByteWidth = sizeof(LaserClearSpawnCounters);
		readback.Usage = D3D11_USAGE_STAGING;
		readback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		D3D11_BUFFER_DESC constants = {};
		constants.ByteWidth = sizeof(LaserClearSpawnConstants);
		constants.Usage = D3D11_USAGE_DEFAULT;
		constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		const bool created =
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			!failClearSpawnCreationForTest_ &&
#endif
			SUCCEEDED(device_->CreateComputeShader(spawnBytecode_.data, spawnBytecode_.size,
				nullptr, spawnShader_.GetAddressOf())) &&
			SUCCEEDED(device_->CreateUnorderedAccessView(driftPool_.buffer.Get(), &particleView,
				spawnParticleUav_.GetAddressOf())) &&
			SUCCEEDED(device_->CreateBuffer(&counters, nullptr, spawnCounters_.GetAddressOf())) &&
			SUCCEEDED(device_->CreateUnorderedAccessView(spawnCounters_.Get(), &counterView,
				spawnCountersUav_.GetAddressOf())) &&
			SUCCEEDED(device_->CreateBuffer(&readback, nullptr, spawnReadback_.GetAddressOf())) &&
			SUCCEEDED(device_->CreateBuffer(&constants, nullptr, spawnConstants_.GetAddressOf()));
		if (!created)
		{
			// 生成资源失败只禁用清空快路；飘散池仍能接收 CPU 记录。
			ReleaseClearSpawnResources();
			spawnUnavailable_ = true;
			DiagnosticOutput::Printf(DiagnosticOutput::OutWarn, "[Draw3.LaserParticles] clear spawn resources unavailable\n");
		}
		return created;
	}

	void LaserParticleSystem::ReleaseClearSpawnResources() noexcept
	{
		pendingClearSpawn_.reset();
		spawnParticleUav_.Reset();
		spawnCountersUav_.Reset();
		spawnCounters_.Reset();
		spawnReadback_.Reset();
		spawnConstants_.Reset();
		spawnShader_.Reset();
	}

	bool LaserParticleSystem::AppendClearSpawn(const LaserClearSpawnRequest& request, uint64_t milliseconds) noexcept
	{
		if (!request.snapshot || request.reservedCount == 0 || !std::isfinite(request.dpiScale) ||
			request.dpiScale <= 0.0f || !std::isfinite(request.windAngleDegrees) || !GpuClearSpawnAvailable()) return false;

		const double begin = GetQpcTimeMilliseconds();
		constexpr LONG maximumDimension = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
		const RECT rect = { std::clamp(request.pixelRect.left, 0L, maximumDimension),
			std::clamp(request.pixelRect.top, 0L, maximumDimension),
			std::clamp(request.pixelRect.right, 0L, maximumDimension),
			std::clamp(request.pixelRect.bottom, 0L, maximumDimension) };
		const auto width = static_cast<uint32_t>((std::max)(0L, rect.right - rect.left));
		const auto height = static_cast<uint32_t>((std::max)(0L, rect.bottom - rect.top));
		const auto maximumSeeds = MaximumSeeds();
		const auto reserved = (std::min)(request.reservedCount, maximumSeeds);
		auto& pool = driftPool_;
		const uint32_t start = pool.ring.Cursor();
		const auto& config = kLaserDissolveParameters;
		const auto& front = request.front;
		// 常规清空用固定碎裂前沿；若给出其他投影，完整进度仍是释放的保守上界。
		auto crumbleFront = front;
		crumbleFront.progress = 0.0f;
		const float latest = crumbleFront == MakeLaserDissolveFront() ? LaserDissolveCrumbleEndProgress() : 1.0f;
		const auto deadline = milliseconds + static_cast<uint32_t>(std::ceil(latest * config.durationMilliseconds)) +
			config.maximumLifetimeMilliseconds;
		const auto batch = pool.ring.Append(reserved, deadline);
		const uint32_t first = (std::min)(reserved, pool.capacity - start);
		auto* groups = pool.slotGroups.get();
		std::fill(groups + start, groups + start + first, kCanvasClearParticleGroup);
		std::fill(groups, groups + reserved - first, kCanvasClearParticleGroup);
		pool.endMilliseconds = (std::max)(pool.endMilliseconds, deadline);

		const auto drift = MakeDriftParticleContext(request.dpiScale, request.windAngleDegrees);
		dipPerPixel_ = drift.dipPerPixel;
		const auto& clear = kCanvasClearParticleParameters;
		LaserClearSpawnConstants constants = {
			{ rect.left, rect.top }, { width, height }, 0, start, reserved, pool.capacityMask,
			maximumSeeds, request.seedBase, static_cast<uint32_t>(milliseconds), clear.minimumAlpha,
			drift.dpiScale, drift.dipPerPixel, clear.seedsPerSquareDip, config.durationMilliseconds,
			{ front.directionX, front.directionY, front.projectionMinimum, front.inverseProjectionSpan },
			{ drift.windX, drift.windY }, drift.windSpeedPxPerSecond, drift.meanderPx,
			config.minimumWindSpeedFraction, config.maximumWindSpeedFraction, config.minimumRadiusDip,
			config.maximumRadiusDip, config.radiusDistributionExponent, config.accelerationPerSecond,
			config.gustVariation, config.accelerationJitter, config.minimumBurstDip, config.maximumBurstDip,
			config.edgeHalfWidth, config.noiseAmplitude, config.minimumLifetimeMilliseconds,
			config.maximumLifetimeMilliseconds, {} };
		// 复用 staging 前丢弃旧结果；三遍在同一 immediate context 上按提交顺序访问 UAV。
		pendingClearSpawn_.reset();
		const LaserClearSpawnCounters zero = {};
		context_->UpdateSubresource(spawnCounters_.Get(), 0, nullptr, &zero, 0, 0);
		ID3D11ShaderResourceView* nullSrv = nullptr;
		context_->VSSetShaderResources(0, 1, &nullSrv);
		ID3D11UnorderedAccessView* uavs[] = { spawnParticleUav_.Get(), spawnCountersUav_.Get() };
		ID3D11Buffer* constantBuffer = spawnConstants_.Get();
		context_->CSSetShader(spawnShader_.Get(), nullptr, 0);
		context_->CSSetShaderResources(0, 1, &request.snapshot);
		context_->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		context_->CSSetConstantBuffers(0, 1, &constantBuffer);
		context_->UpdateSubresource(constantBuffer, 0, nullptr, &constants, 0, 0);
		context_->Dispatch((reserved + 63u) / 64u, 1, 1);
		if (width != 0 && height != 0)
		{
			const uint32_t groupsX = (width + 7u) / 8u, groupsY = (height + 7u) / 8u;
			for (constants.mode = 1; constants.mode <= 2; ++constants.mode)
			{
				context_->UpdateSubresource(constantBuffer, 0, nullptr, &constants, 0, 0);
				context_->Dispatch(groupsX, groupsY, 1);
			}
		}
		ID3D11UnorderedAccessView* nullUavs[2] = {};
		ID3D11Buffer* nullBuffer = nullptr;
		context_->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
		context_->CSSetShaderResources(0, 1, &nullSrv);
		context_->CSSetConstantBuffers(0, 1, &nullBuffer);
		context_->CSSetShader(nullptr, nullptr, 0);
		context_->CopyResource(spawnReadback_.Get(), spawnCounters_.Get());
		const double submitted = GetQpcTimeMilliseconds();
		pendingClearSpawn_ = PendingClearSpawn{ deviceGeneration_, batch, reserved, submitted };
		DiagnosticOutput::Printf(DiagnosticOutput::Stream::Output,
			"[Draw3.LaserParticles] clear spawn reserved=%u submit_ms=%.3f\n",
			reserved, submitted - begin);
		return true;
	}

	std::optional<LaserClearSpawnReadback> LaserParticleSystem::PollClearSpawn() noexcept
	{
		if (!pendingClearSpawn_) return {};
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT status = context_->Map(spawnReadback_.Get(), 0, D3D11_MAP_READ,
			D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (status == DXGI_ERROR_WAS_STILL_DRAWING) return {};
		const auto pending = *pendingClearSpawn_;
		pendingClearSpawn_.reset();
		if (FAILED(status))
		{
			if (FAILED(device_->GetDeviceRemovedReason())) Release();
			return {};
		}

		LaserClearSpawnCounters counters;
		std::memcpy(&counters, mapped.pData, sizeof(counters));
		context_->Unmap(spawnReadback_.Get(), 0);
		const uint32_t written = (std::min)(counters.written, pending.reservedCount);
		auto& pool = driftPool_;
		// 已被部分覆盖的批次即使又回到环尾，也不能按原预留长度裁剪。
		const bool batchRevoked = pending.deviceGeneration == deviceGeneration_ &&
			pool.ring.Range().count >= pending.reservedCount && pool.ring.TrimTail(pending.batch, written) && written == 0;
		if (batchRevoked)
		{
			pool.ring.Expire(lastMilliseconds_);
			pool.endMilliseconds = pool.ring.EndMilliseconds();
		}
		DiagnosticOutput::Printf(DiagnosticOutput::Stream::Output,
			"[Draw3.LaserParticles] clear spawn written=%u wait_ms=%.3f\n",
			written, GetQpcTimeMilliseconds() - pending.submittedMilliseconds);
		return LaserClearSpawnReadback{ written, batchRevoked };
	}

	bool LaserParticleSystem::AppendTrailSeeds(std::span<const LaserParticleSeed> seeds, uint64_t milliseconds) noexcept
	{
		auto& pool = trailPool_;
		const auto count = static_cast<uint32_t>((std::min)(seeds.size(), static_cast<size_t>(pool.capacity)));
		seeds = seeds.last(count);
		const uint32_t start = pool.ring.Cursor();
		const auto bornMilliseconds = static_cast<uint32_t>(milliseconds);
		uint32_t lifetime = 0;
		for (uint32_t offset = 0; offset < count;)
		{
			const uint32_t chunkCount = (std::min)(kLaserParticleStagingChunk, count - offset);
			const uint32_t chunkStart = (start + offset) & pool.capacityMask;
			for (uint32_t i = 0; i < chunkCount; ++i)
			{
				const auto& seed = seeds[offset + i];
				staging_[i] = PackLaserTrailParticle(seed, bornMilliseconds);
				pool.slotGroups[(chunkStart + i) & pool.capacityMask] = seed.group;
				lifetime = (std::max)(lifetime, seed.releaseDelayMilliseconds + seed.lifetimeMilliseconds);
			}
			if (!Upload(pool, chunkStart, chunkCount)) return false;
			offset += chunkCount;
		}
		pool.endMilliseconds = (std::max)(pool.endMilliseconds, milliseconds + lifetime);
		pool.ring.Append(count, milliseconds + lifetime, false);
		return true;
	}

	bool LaserParticleSystem::AppendDriftParticles(std::span<const LaserDriftParticle> plainDrift,
		std::span<const LaserDriftParticle> dissolveDrift, float dipPerPixel, uint64_t milliseconds,
		LaserParticleStepResult& result) noexcept
	{
		auto& pool = driftPool_;
		// 与旧池相同，从两批的尾部保留最新记录，激光飘散优先占用容量。
		dissolveDrift = dissolveDrift.last((std::min)(dissolveDrift.size(), static_cast<size_t>(pool.capacity)));
		const auto dissolveCount = static_cast<uint32_t>(dissolveDrift.size());
		const auto keptPlain = static_cast<uint32_t>((std::min)(plainDrift.size(),
			static_cast<size_t>(pool.capacity - dissolveCount)));
		plainDrift = plainDrift.last(keptPlain);
		if (dissolveCount != 0)
		{
			dissolveBatch_ = dissolveBatch_ >= kMaximumDissolveBatch ? 1u : dissolveBatch_ + 1u;
			gatheringBatch_ = 0;
		}

		const uint32_t count = keptPlain + dissolveCount;
		const uint32_t start = pool.ring.Cursor();
		const auto bornMilliseconds = static_cast<uint32_t>(milliseconds);
		uint32_t plainLifetime = 0, dissolveLifetime = 0;
		// 复制到分块暂存区时补绝对释放时刻和批号；分组仍只保留在 CPU。
		for (uint32_t offset = 0; offset < count;)
		{
			const uint32_t chunkCount = (std::min)(kLaserParticleStagingChunk, count - offset);
			const uint32_t chunkStart = (start + offset) & pool.capacityMask;
			for (uint32_t i = 0; i < chunkCount; ++i)
			{
				const uint32_t index = offset + i;
				const bool inDissolveBatch = index >= keptPlain;
				const auto& drift = inDissolveBatch ? dissolveDrift[index - keptPlain] : plainDrift[index];
				const auto& particle = drift.particle;
				staging_[i] = particle;
				staging_[i].releaseMilliseconds += bornMilliseconds;
				if (inDissolveBatch) staging_[i].kindAndBatch |= dissolveBatch_ << 2;
				pool.slotGroups[(chunkStart + i) & pool.capacityMask] = drift.group;
				uint32_t& batchLifetime = inDissolveBatch ? dissolveLifetime : plainLifetime;
				batchLifetime = (std::max)(batchLifetime, particle.releaseMilliseconds + particle.lifetimeMilliseconds);
			}
			if (!Upload(pool, chunkStart, chunkCount)) return false;
			offset += chunkCount;
		}
		// 调用方给出最后一条飘散记录的尺度；尾部截断规则保证那条记录仍在。
		if (dipPerPixel > 0.0f) dipPerPixel_ = dipPerPixel;
		pool.endMilliseconds = (std::max)(pool.endMilliseconds,
			milliseconds + (std::max)(plainLifetime, dissolveLifetime));
		pool.ring.Append(keptPlain, milliseconds + plainLifetime, false);
		pool.ring.Append(dissolveCount, milliseconds + dissolveLifetime, true);
		// 溢出可能挤掉整批清空记录，按实际保留的数量报告。
		result.clearAccepted = keptPlain != 0;
		result.dissolveAccepted = dissolveCount != 0;
		return true;
	}

	bool LaserParticleSystem::Upload(const ParticlePool& pool, uint32_t start, uint32_t count) noexcept
	{
		if (count == 0) return true;
		if (!pool.buffer || !staging_ || count > kLaserParticleStagingChunk || count > pool.capacity ||
			start >= pool.capacity) return false;
		// 环形写入：跨过末尾时分两段。
		const uint32_t first = (std::min)(count, pool.capacity - start);
		const auto stride = static_cast<UINT>(sizeof(LaserGpuParticle));
		D3D11_BOX box = { start * stride, 0, 0, (start + first) * stride, 1, 1 };
		context_->UpdateSubresource(pool.buffer.Get(), 0, &box, staging_.get(), 0, 0);
		if (count > first)
		{
			box = { 0, 0, 0, (count - first) * stride, 1, 1 };
			context_->UpdateSubresource(pool.buffer.Get(), 0, &box, staging_.get() + first, 0, 0);
		}
		return true;
	}

	bool LaserParticleSystem::KillSlots(ParticlePool& pool, LaserParticleActiveRange range) noexcept
	{
		if (range.count == 0 || !pool.buffer || !staging_ || !pool.slotGroups) return true;
		// 寿命为 0 的记录在顶点着色器里恒被剔除；后续各块复用同一份零数据。
		std::memset(staging_.get(), 0, sizeof(LaserGpuParticle) *
			(std::min)(kLaserParticleStagingChunk, range.count));
		for (uint32_t offset = 0; offset < range.count;)
		{
			const uint32_t chunkCount = (std::min)(kLaserParticleStagingChunk, range.count - offset);
			const uint32_t chunkStart = (range.start + offset) & pool.capacityMask;
			for (uint32_t i = 0; i < chunkCount; ++i)
				pool.slotGroups[(chunkStart + i) & pool.capacityMask] = kNoParticleGroup;
			if (!Upload(pool, chunkStart, chunkCount)) return false;
			offset += chunkCount;
		}
		return true;
	}

	bool LaserParticleSystem::Draw(LaserParticlePass pass, float viewportWidth, float viewportHeight) noexcept
	{
		const auto& pool = Pool(pass);
		const auto range = pool.ring.Range();
		if (!available_ || range.count == 0 || !pool.buffer || lastMilliseconds_ >= pool.endMilliseconds) return false;
		const auto kind = pass == LaserParticlePass::OverBody ? LaserParticleKind::Trail : LaserParticleKind::Dissolve;
		return Submit(pool, range.start, range.count, kind, viewportWidth, viewportHeight);
	}

	bool LaserParticleSystem::Prepare() noexcept
	{
		if (trailPool_.buffer || !EnsureResources(LaserParticlePass::OverBody)) return false;
		// 预热要读第 0 槽：先写成空槽，不让着色器读到未初始化的内容。
		staging_[0] = {};
		return Upload(trailPool_, 0, 1);
	}

	void LaserParticleSystem::ReleaseIdleDriftPool() noexcept
	{
		if (!driftPool_.buffer || driftPool_.ring.Range().count != 0) return;
		// 归还飘散池的缓冲区和环，保留行进池仍可能在用的共享资源与时钟。
		ReleaseClearSpawnResources();
		driftPool_ = {};
		gatheringBatch_ = 0;
	}

	void LaserParticleSystem::ReleaseIdleResources() noexcept
	{
		const bool trailIdle = trailPool_.ring.Range().count == 0;
		const bool driftIdle = driftPool_.ring.Range().count == 0;
		if (trailIdle) trailPool_ = {};
		if (driftIdle) ReleaseIdleDriftPool();
		if (!trailIdle || !driftIdle || !drawConstants_) return;
		vertexShader_.Reset();
		pixelShader_.Reset();
		drawConstants_.Reset();
		staging_.reset();
		Reset();
	}

	void LaserParticleSystem::WarmUp() noexcept
	{
		if (available_ && trailPool_.buffer) Submit(trailPool_, 0, 1, LaserParticleKind::Trail, 1.0f, 1.0f);
	}

	bool LaserParticleSystem::Submit(const ParticlePool& pool, uint32_t start, uint32_t count, LaserParticleKind kind,
		float viewportWidth, float viewportHeight) noexcept
	{
		if (!(viewportWidth > 0.0f) || !(viewportHeight > 0.0f)) return false;
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context_->Map(drawConstants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		const LaserParticleDrawConstants constants = {
			{ 2.0f / viewportWidth, -2.0f / viewportHeight }, start, static_cast<uint32_t>(kind),
			clock_.nowMilliseconds, clock_.gatherMilliseconds, clock_.gatherBatch, clock_.gatherRemaining,
			configuration_.shrinkStartTravelRatio, configuration_.endRadiusScale,
			kLaserDissolveParameters.endRadiusScale, 1.0f / kLaserDissolveParameters.whiteFadeSeconds,
			dipPerPixel_, pool.capacityMask, {} };
		std::memcpy(mapped.pData, &constants, sizeof(constants));
		context_->Unmap(drawConstants_.Get(), 0);

		ID3D11Buffer* constantBuffer = drawConstants_.Get();
		ID3D11ShaderResourceView* particles = pool.srv.Get();
		context_->IASetInputLayout(nullptr);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
		context_->VSSetConstantBuffers(0, 1, &constantBuffer);
		context_->VSSetShaderResources(0, 1, &particles);
		context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
		context_->DrawInstanced(4, count, 0, 0);
		ID3D11ShaderResourceView* nullParticles = nullptr;
		context_->VSSetShaderResources(0, 1, &nullParticles);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		return true;
	}

	void LaserParticleSystem::RemoveGroup(uint32_t group, bool endDissolve) noexcept
	{
		if (!available_ || (trailPool_.ring.Range().count == 0 && driftPool_.ring.Range().count == 0)) return;
		const bool succeeded = RemovePoolGroup(trailPool_, group) && RemovePoolGroup(driftPool_, group);
		if (succeeded && endDissolve)
		{
			driftPool_.ring.EndLatestDissolve(lastMilliseconds_);
			driftPool_.ring.Expire(lastMilliseconds_);
			gatheringBatch_ = 0;
		}
		if (!succeeded || FAILED(device_->GetDeviceRemovedReason())) Release();
	}

	bool LaserParticleSystem::RemovePoolGroup(ParticlePool& pool, uint32_t group) noexcept
	{
		const auto range = pool.ring.Range();
		if (range.count == 0 || !pool.slotGroups) return true;
		// 撤回某一笔：把它名下的槽（已释放与未释放）写成空槽，其余粒子与时钟都不动。
		// 同一笔的种子是连续生成的，通常只有一两段。
		bool succeeded = true;
		uint32_t runStart = 0, runLength = 0;
		for (uint32_t i = 0; i <= range.count && succeeded; ++i)
		{
			const bool match = i < range.count &&
				pool.slotGroups[(range.start + i) & pool.capacityMask] == group;
			if (match)
			{
				if (runLength++ == 0) runStart = (range.start + i) & pool.capacityMask;
			}
			else if (runLength != 0)
			{
				succeeded = KillSlots(pool, { runStart, runLength });
				runLength = 0;
			}
		}
		return succeeded;
	}

	void LaserParticleSystem::Reset() noexcept
	{
		pendingClearSpawn_.reset();
		trailPool_.ring.Clear();
		driftPool_.ring.Clear();
		clock_ = {};
		clockOriginQpc_ = 0;
		lastMilliseconds_ = 0;
		trailPool_.endMilliseconds = 0;
		driftPool_.endMilliseconds = 0;
		gatheringBatch_ = 0;
	}
}
