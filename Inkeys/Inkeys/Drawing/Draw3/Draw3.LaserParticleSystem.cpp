module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <d3d11.h>
#include <span>
#include <windows.h>

module Inkeys.Drawing.Draw3.laser_particles;

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
			float padding[3];
		};
		static_assert(sizeof(LaserParticleDrawConstants) == 64);
		static_assert(offsetof(LaserParticleDrawConstants, nowMilliseconds) == 16);
		static_assert(offsetof(LaserParticleDrawConstants, dipPerPixel) == 48);
		static_assert((kLaserParticleCapacity & (kLaserParticleCapacity - 1)) == 0); // 着色器用按位与取环下标。

		constexpr uint32_t kNoParticleGroup = 0xFFFFFFFEu; // 已作废的槽，不与任何分组号相同
		constexpr uint32_t kMaximumDissolveBatch = (1u << 30) - 1u; // 批次号与种类共用一个 32 位字段
	}

	bool LaserParticleSystem::Initialize(ID3D11Device* device,
		ID3D11DeviceContext* context, LaserParticleShaderBytecode vertexShader,
		LaserParticleShaderBytecode pixelShader) noexcept
	{
		Release();
		if (!device || !context || !vertexShader.data || vertexShader.size == 0 ||
			!pixelShader.data || pixelShader.size == 0 ||
			device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 || !ring_.Initialize()) return false;
		// 这里只记下设备与字节码；着色器、粒子池和暂存区等到第一次有粒子时才创建。
		device_ = device;
		context_ = context;
		vertexBytecode_ = vertexShader;
		pixelBytecode_ = pixelShader;
		available_ = true;
		return true;
	}

	bool LaserParticleSystem::EnsureResources() noexcept
	{
		if (!available_) return false;
		if (particleBuffer_) return true;
		if (!staging_) staging_.reset(new (std::nothrow) LaserGpuParticle[kLaserParticleCapacity]);
		if (!slotGroups_) slotGroups_.reset(new (std::nothrow) uint32_t[kLaserParticleCapacity]);
		bool created = staging_ && slotGroups_ &&
			SUCCEEDED(device_->CreateVertexShader(vertexBytecode_.data, vertexBytecode_.size,
				nullptr, vertexShader_.ReleaseAndGetAddressOf())) &&
			SUCCEEDED(device_->CreatePixelShader(pixelBytecode_.data, pixelBytecode_.size,
				nullptr, pixelShader_.ReleaseAndGetAddressOf()));
		if (created)
		{
			// DEFAULT + UpdateSubresource：不依赖动态结构化缓冲区的 NO_OVERWRITE（Win7 没有），
			// 也是兆芯兼容上传采用的方式。槽只在被写过之后才进入活跃区间，所以不需要初始数据。
			D3D11_BUFFER_DESC description = {};
			description.ByteWidth = static_cast<UINT>(sizeof(LaserGpuParticle) * kLaserParticleCapacity);
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			description.StructureByteStride = sizeof(LaserGpuParticle);
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription = {};
			viewDescription.Format = DXGI_FORMAT_UNKNOWN;
			viewDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			viewDescription.Buffer.NumElements = kLaserParticleCapacity;
			D3D11_BUFFER_DESC constantDescription = {};
			constantDescription.ByteWidth = sizeof(LaserParticleDrawConstants);
			constantDescription.Usage = D3D11_USAGE_DYNAMIC;
			constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			created = SUCCEEDED(device_->CreateBuffer(&description, nullptr,
					particleBuffer_.ReleaseAndGetAddressOf())) &&
				SUCCEEDED(device_->CreateShaderResourceView(particleBuffer_.Get(), &viewDescription,
					particleSRV_.ReleaseAndGetAddressOf())) &&
				SUCCEEDED(device_->CreateBuffer(&constantDescription, nullptr,
					drawConstants_.ReleaseAndGetAddressOf()));
		}
		if (!created) Release(); // 创建失败后本次设备会话内不再重试，粒子效果整体不可用。
		return created;
	}

	void LaserParticleSystem::Release() noexcept
	{
		available_ = false;
		vertexShader_.Reset();
		pixelShader_.Reset();
		particleSRV_.Reset();
		particleBuffer_.Reset();
		drawConstants_.Reset();
		staging_.reset();
		slotGroups_.reset();
		vertexBytecode_ = {};
		pixelBytecode_ = {};
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
		const auto toMilliseconds = [&](int64_t qpc)
		{
			return static_cast<uint64_t>(static_cast<double>(
				(std::max)(qpc - clockOriginQpc_, int64_t{0})) * 1000.0 / qpcFrequency);
		};
		const uint64_t milliseconds = toMilliseconds(nowQpc);
		if (milliseconds > lastMilliseconds_ &&
			milliseconds - lastMilliseconds_ > (std::numeric_limits<uint32_t>::max)())
			ring_.Clear(); // 长时间挂起跨过了 32 位时钟整圈，旧槽已全部过期。
		if (ring_.Range().count == 0 && seeds.empty()) return;

		bool succeeded = true;
		if (ring_.Range().count != 0)
		{
			if (dissolve.stop)
			{
				// 汇聚完成或飘散被打断：这一批直接作废。槽在环的中段时无法退出区间，写成空槽即可。
				succeeded = KillSlots(ring_.LatestDissolveRange());
				ring_.EndLatestDissolve(milliseconds);
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
					ring_.ExtendLatestDissolve(gatherEndMilliseconds);
					driftEndMilliseconds_ = (std::max)(driftEndMilliseconds_, gatherEndMilliseconds);
				}
			}
			else if (gatheringBatch_ != 0)
			{
				// 汇聚中途被撤掉而没有显式停止：冻结时钟已不成立，整批作废，不让粒子跳回实时位置。
				if (gatheringBatch_ == dissolveBatch_)
				{
					succeeded = KillSlots(ring_.LatestDissolveRange());
					ring_.EndLatestDissolve(milliseconds);
				}
				gatheringBatch_ = 0;
			}
			ring_.Expire(milliseconds);
		}
		const bool gathering = dissolve.gatherStartQpc > 0 && !dissolve.stop && gatheringBatch_ != 0;
		clock_.nowMilliseconds = static_cast<uint32_t>(milliseconds);
		clock_.gatherMilliseconds = gathering ? static_cast<uint32_t>(toMilliseconds(dissolve.gatherStartQpc)) : 0u;
		clock_.gatherBatch = gathering ? gatheringBatch_ : 0u;
		clock_.gatherRemaining = gathering ? 1.0f - std::clamp(dissolve.gatherProgress, 0.0f, 1.0f) : 1.0f;

		if (succeeded && !seeds.empty() && (succeeded = EnsureResources()))
		{
			seeds = seeds.last((std::min)(seeds.size(), static_cast<size_t>(kLaserParticleCapacity)));
			const auto count = static_cast<uint32_t>(seeds.size());
			dissolveSeedCount = (std::min)(dissolveSeedCount, count);
			if (dissolveSeedCount != 0)
			{
				dissolveBatch_ = dissolveBatch_ >= kMaximumDissolveBatch ? 1u : dissolveBatch_ + 1u;
				gatheringBatch_ = 0;
			}
			const uint32_t start = ring_.Cursor();
			const auto bornMilliseconds = static_cast<uint32_t>(milliseconds);
			uint32_t plainLifetime = 0, dissolveLifetime = 0;
			for (uint32_t i = 0; i < count; ++i)
			{
				const LaserParticleSeed& seed = seeds[i];
				const bool inDissolveBatch = i >= count - dissolveSeedCount;
				staging_[i] = PackLaserParticle(seed, bornMilliseconds, inDissolveBatch ? dissolveBatch_ : 0u);
				slotGroups_[(start + i) % kLaserParticleCapacity] = seed.group;
				const uint32_t lifetime = seed.releaseDelayMilliseconds + seed.lifetimeMilliseconds;
				uint32_t& batchLifetime = inDissolveBatch ? dissolveLifetime : plainLifetime;
				batchLifetime = (std::max)(batchLifetime, lifetime);
				if (seed.kind == LaserParticleKind::Dissolve)
				{
					driftEndMilliseconds_ = (std::max)(driftEndMilliseconds_, milliseconds + lifetime);
					if (seed.breathingFrequencyHz > 0.0f) dipPerPixel_ = seed.breathingFrequencyHz;
				}
				else trailEndMilliseconds_ = (std::max)(trailEndMilliseconds_, milliseconds + lifetime);
			}
			succeeded = Upload(start, count);
			if (succeeded)
			{
				// 环内独立记飘散批次，汇聚可提前结束而不影响其他粒子。
				ring_.Append(count - dissolveSeedCount, milliseconds + plainLifetime, false);
				ring_.Append(dissolveSeedCount, milliseconds + dissolveLifetime, true);
			}
		}
		lastMilliseconds_ = milliseconds;
		if (available_ && (!succeeded || FAILED(device_->GetDeviceRemovedReason()))) Release();
	}

	bool LaserParticleSystem::Upload(uint32_t start, uint32_t count) noexcept
	{
		if (count == 0) return true;
		if (!particleBuffer_ || !staging_ || count > kLaserParticleCapacity ||
			start >= kLaserParticleCapacity) return false;
		// 环形写入：跨过末尾时分两段。
		const uint32_t first = (std::min)(count, kLaserParticleCapacity - start);
		const auto stride = static_cast<UINT>(sizeof(LaserGpuParticle));
		D3D11_BOX box = { start * stride, 0, 0, (start + first) * stride, 1, 1 };
		context_->UpdateSubresource(particleBuffer_.Get(), 0, &box, staging_.get(), 0, 0);
		if (count > first)
		{
			box = { 0, 0, 0, (count - first) * stride, 1, 1 };
			context_->UpdateSubresource(particleBuffer_.Get(), 0, &box, staging_.get() + first, 0, 0);
		}
		return true;
	}

	bool LaserParticleSystem::KillSlots(LaserParticleActiveRange range) noexcept
	{
		if (range.count == 0 || !particleBuffer_ || !staging_ || !slotGroups_) return true;
		// 寿命为 0 的记录在顶点着色器里恒被剔除。
		std::memset(staging_.get(), 0, sizeof(LaserGpuParticle) * range.count);
		for (uint32_t i = 0; i < range.count; ++i)
			slotGroups_[(range.start + i) % kLaserParticleCapacity] = kNoParticleGroup;
		return Upload(range.start, range.count);
	}

	bool LaserParticleSystem::Draw(LaserParticlePass pass, float viewportWidth, float viewportHeight) noexcept
	{
		const auto range = ring_.Range();
		if (!available_ || range.count == 0 || !particleBuffer_) return false;
		const bool trail = pass == LaserParticlePass::OverBody;
		// 这一类粒子已全部结束时不提交绘制，省掉整段区间的顶点求值。
		if (lastMilliseconds_ >= (trail ? trailEndMilliseconds_ : driftEndMilliseconds_)) return false;
		return Submit(range.start, range.count, trail ? LaserParticleKind::Trail : LaserParticleKind::Dissolve,
			viewportWidth, viewportHeight);
	}

	bool LaserParticleSystem::Prepare() noexcept
	{
		if (particleBuffer_ || !EnsureResources()) return false;
		// 预热要读第 0 槽：先写成空槽，不让着色器读到未初始化的内容。
		staging_[0] = {};
		return Upload(0, 1);
	}

	void LaserParticleSystem::ReleaseIdleResources() noexcept
	{
		if (!particleBuffer_ || ring_.Range().count != 0) return;
		// 只归还占内存的对象；设备、字节码和可用状态保留，下次有粒子时 EnsureResources 重新创建。
		vertexShader_.Reset();
		pixelShader_.Reset();
		particleSRV_.Reset();
		particleBuffer_.Reset();
		drawConstants_.Reset();
		staging_.reset();
		slotGroups_.reset();
		Reset();
	}

	void LaserParticleSystem::WarmUp() noexcept
	{
		if (available_ && particleBuffer_) Submit(0, 1, LaserParticleKind::Trail, 1.0f, 1.0f);
	}

	bool LaserParticleSystem::Submit(uint32_t start, uint32_t count, LaserParticleKind kind,
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
			dipPerPixel_, {} };
		std::memcpy(mapped.pData, &constants, sizeof(constants));
		context_->Unmap(drawConstants_.Get(), 0);

		ID3D11Buffer* constantBuffer = drawConstants_.Get();
		ID3D11ShaderResourceView* particles = particleSRV_.Get();
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
		const auto range = ring_.Range();
		if (!available_ || range.count == 0 || !slotGroups_) return;
		// 撤回某一笔：把它名下的槽（已释放与未释放）写成空槽，其余粒子与时钟都不动。
		// 同一笔的种子是连续生成的，通常只有一两段。
		bool succeeded = true;
		uint32_t runStart = 0, runLength = 0;
		for (uint32_t i = 0; i <= range.count && succeeded; ++i)
		{
			const bool match = i < range.count &&
				slotGroups_[(range.start + i) % kLaserParticleCapacity] == group;
			if (match)
			{
				if (runLength++ == 0) runStart = (range.start + i) % kLaserParticleCapacity;
			}
			else if (runLength != 0)
			{
				succeeded = KillSlots({ runStart, runLength });
				runLength = 0;
			}
		}
		if (succeeded && endDissolve)
		{
			ring_.EndLatestDissolve(lastMilliseconds_);
			ring_.Expire(lastMilliseconds_);
			gatheringBatch_ = 0;
		}
		if (!succeeded || FAILED(device_->GetDeviceRemovedReason())) Release();
	}

	void LaserParticleSystem::Reset() noexcept
	{
		ring_.Clear();
		clock_ = {};
		clockOriginQpc_ = 0;
		lastMilliseconds_ = 0;
		trailEndMilliseconds_ = 0;
		driftEndMilliseconds_ = 0;
		gatheringBatch_ = 0;
	}
}
