module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <DirectXMath.h>
#include <optional>
#include <span>
#include <utility>
#include <windows.h>

module Inkeys.Drawing.Draw3.renderer;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元集中维护激光 coverage、材质解析和粒子绘制路径。
	using renderer_detail::GlobalShaderConstants;

	namespace
	{
		float ResolveLaserColorChannel(float value, float fallback) noexcept
		{
			return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
		}

		float MixLaserColorWithWhite(float value, float whiteMix) noexcept
		{
			return value + (1.0f - value) * whiteMix;
		}
	}

	void InkRenderer::ConfigureLaserStyle(float dpiScale,
		DirectX::XMFLOAT4 shellColor) noexcept
	{
		const float scale = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		const float solidRadius = LaserSolidRadius(scale);
		const float coreRadius = LaserCoreRadius(solidRadius);
		const DirectX::XMFLOAT4 resolvedShellColor(
			ResolveLaserColorChannel(shellColor.x, 1.0f),
			ResolveLaserColorChannel(shellColor.y, 16.0f / 255.0f),
			ResolveLaserColorChannel(shellColor.z, 0.0f), 1.0f);
		LaserStyleConstants next = laserStyleConstants_;
		next.radii = DirectX::XMFLOAT4(
			coreRadius, solidRadius, LaserDiffuseExtent(scale),
			coreRadius * kLaserScatterHalfWidthToCoreRatio);
		next.coreColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		// 内侧散射和外缘高光只向白色提亮，实体外框保留用户所选 RGB。
		next.scatterColor = DirectX::XMFLOAT4(
			MixLaserColorWithWhite(resolvedShellColor.x, 0.94f),
			MixLaserColorWithWhite(resolvedShellColor.y, 0.94f),
			MixLaserColorWithWhite(resolvedShellColor.z, 0.94f), 0.94f);
		next.borderColor = DirectX::XMFLOAT4(
			resolvedShellColor.x, resolvedShellColor.y, resolvedShellColor.z, 0.98f);
		next.edgeColor = DirectX::XMFLOAT4(
			MixLaserColorWithWhite(resolvedShellColor.x, 0.43f),
			MixLaserColorWithWhite(resolvedShellColor.y, 0.43f),
			MixLaserColorWithWhite(resolvedShellColor.z, 0.43f), 0.72f);
		// 漫反射在实体边界达到满 alpha；edgeColor.a 只控制提亮色的 RGB 混合强度。
		next.glowColor = DirectX::XMFLOAT4(
			resolvedShellColor.x, resolvedShellColor.y, resolvedShellColor.z, 1.0f);
		// x 由每次 resolve 写入；z/w 是实体外侧漫反射曲线的边缘高亮阈值。
		next.parameters = DirectX::XMFLOAT4(
			laserStyleConstants_.parameters.x, scale, 0.20f, 0.29f);
		if (std::memcmp(&next, &laserStyleConstants_, sizeof(next)) == 0) return;
		laserStyleConstants_ = next;
		++laserStyleGeneration_;
		if (laserStyleGeneration_ == 0) laserStyleGeneration_ = 1;
		laserStyleCacheValid_ = false;
	}

	void InkRenderer::ConfigureLaserParticles(
		const LaserParticleConfig& configuration) noexcept
	{
		laserParticleSystem_.Configure(configuration);
	}

	bool InkRenderer::VisitCanvasPixels(CanvasPixelVisitor visitor, void* visitorContext)
	{
		if (!visitor || !device || !context || !layerL2Texture) return false;
		D3D11_TEXTURE2D_DESC description = {};
		layerL2Texture->GetDesc(&description);
		description.Usage = D3D11_USAGE_STAGING;
		description.BindFlags = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		description.MiscFlags = 0;
		// 只在清空那一帧用一次，不常驻：读回纹理随函数返回释放。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
		if (FAILED(device->CreateTexture2D(&description, nullptr, staging.GetAddressOf()))) return false;
		context->CopyResource(staging.Get(), layerL2Texture.Get());
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
		try
		{
			visitor(visitorContext, static_cast<const uint32_t*>(mapped.pData), mapped.RowPitch,
				description.Width, description.Height);
		}
		catch (...)
		{
			context->Unmap(staging.Get(), 0);
			throw;
		}
		context->Unmap(staging.Get(), 0);
		return true;
	}

	void InkRenderer::ReleaseIdleLaserParticles() noexcept
	{
		laserParticleSystem_.ReleaseIdleResources();
	}

	void InkRenderer::ReleaseIdleLaserDriftPool() noexcept
	{
		laserParticleSystem_.ReleaseIdleDriftPool();
	}

	bool InkRenderer::CaptureCanvasClearSnapshot()
	{
		if (!device || !context || !layerL2Texture) return false;
		D3D11_TEXTURE2D_DESC description = {};
		layerL2Texture->GetDesc(&description);
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		description.CPUAccessFlags = 0;
		description.MiscFlags = 0;
		D3D11_TEXTURE2D_DESC existing = {};
		if (canvasClearSnapshot_) canvasClearSnapshot_->GetDesc(&existing);
		// 上一次清空的过渡还没结束就再次清空：尺寸相同时直接复用那张纹理。
		if (!canvasClearSnapshotSRV_ || existing.Width != description.Width || existing.Height != description.Height)
		{
			ReleaseCanvasClearSnapshot();
			if (FAILED(device->CreateTexture2D(&description, nullptr, canvasClearSnapshot_.GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(canvasClearSnapshot_.Get(), nullptr,
					canvasClearSnapshotSRV_.GetAddressOf())))
			{
				ReleaseCanvasClearSnapshot();
				return false;
			}
		}
		context->CopyResource(canvasClearSnapshot_.Get(), layerL2Texture.Get());
		return true;
	}

	bool InkRenderer::ResolveCanvasClearSnapshot(ID3D11RenderTargetView* dstRTV, RECT rect,
		const LaserDissolveFront& front)
	{
		if (!canvasClearSnapshotSRV_) return false;
		// 画布层与已烘激光颜色一样是预乘颜色，类型 11 的“采样 × 碎裂遮罩”可以原样复用，不需要新的着色器分支。
		return DrawLaserRectPass(dstRTV, rect, 1.0f, 11.0f, canvasClearSnapshotSRV_.Get(), 6,
			operatorResolveBlendState.Get(), nullptr, 0, front);
	}

	void InkRenderer::ReleaseCanvasClearSnapshot() noexcept
	{
		canvasClearSnapshotSRV_.Reset();
		canvasClearSnapshot_.Reset();
	}

	void InkRenderer::PrepareLaserParticles() noexcept
	{
		if (!context || !backBufferRTV || !laserParticleSystem_.Prepare()) return;
		// 与 WarmUpLaserShaders 相同的做法：零宽高视口下提交一次真实绘制，驱动现在就编译粒子着色器。
		D3D11_VIEWPORT savedViewport = {};
		UINT viewportCount = 1;
		context->RSGetViewports(&viewportCount, &savedViewport);
		const D3D11_VIEWPORT emptyViewport = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		context->RSSetViewports(1, &emptyViewport);
		SetOMTarget(backBufferRTV.Get());
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		laserParticleSystem_.WarmUp();
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
		context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		context->RSSetViewports(1, &savedViewport);
	}

	bool InkRenderer::LaserParticlesAvailable() const noexcept
	{
		return laserParticleSystem_.IsAvailable();
	}

	bool InkRenderer::HasActiveLaserParticles() const noexcept
	{
		return laserParticleSystem_.ActiveRange(LaserParticlePass::OverBody).count != 0 ||
			laserParticleSystem_.ActiveRange(LaserParticlePass::UnderBody).count != 0;
	}

	bool InkRenderer::SetLaserParticleLimit(LaserParticleLimit limit) noexcept
	{
		const auto previous = laserParticleSystem_.EffectiveLimit();
		laserParticleSystem_.SetLimit(limit);
		return laserParticleSystem_.EffectiveLimit() != previous;
	}

	uint32_t InkRenderer::LaserParticleMaximumSeeds() const noexcept
	{
		return laserParticleSystem_.MaximumSeeds();
	}

	uint32_t InkRenderer::PrepareLaserDriftPool() noexcept
	{
		return laserParticleSystem_.PrepareDriftPool();
	}

	LaserParticleStepResult InkRenderer::StepLaserParticles(int64_t nowQpc, int64_t qpcFrequency,
		std::span<const LaserParticleSeed> trailSeeds, std::span<const LaserDriftParticle> plainDrift,
		std::span<const LaserDriftParticle> dissolveDrift, float dipPerPixel,
		LaserDissolveParticleControl dissolve, const LaserClearSpawnRequest* clearSpawn) noexcept
	{
		if (!clearSpawn)
			return laserParticleSystem_.Step(nowQpc, qpcFrequency, trailSeeds, plainDrift, dissolveDrift, dipPerPixel, dissolve);

		auto request = *clearSpawn;
		request.snapshot = canvasClearSnapshotSRV_.Get();
		if (request.snapshot) UnbindLaserCoverageShaderResources(); // 快照由碎裂绘制使用 PS t6。
		return laserParticleSystem_.Step(nowQpc, qpcFrequency, trailSeeds, plainDrift, dissolveDrift,
			dipPerPixel, dissolve, &request);
	}

	bool InkRenderer::GpuClearSpawnAvailable() noexcept
	{
		return laserParticleSystem_.GpuClearSpawnAvailable();
	}

	std::optional<LaserClearSpawnReadback> InkRenderer::PollClearSpawn() noexcept
	{
		return laserParticleSystem_.PollClearSpawn();
	}

	void InkRenderer::RemoveLaserParticleGroup(uint32_t group, bool endDissolve) noexcept
	{
		laserParticleSystem_.RemoveGroup(group, endDissolve);
	}

	void InkRenderer::ResetLaserParticles() noexcept
	{
		laserParticleSystem_.Reset();
	}

	bool InkRenderer::UpdateLaserStyleConstants(float opacity, const LaserDissolveFront& front)
	{
		if (!laserStyleCB) return false;
		const float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);
		const LaserDissolveFront effectiveFront = front.progress > 0.0f ? front : LaserDissolveFront{};
		if (laserStyleCacheValid_ &&
			uploadedLaserStyleGeneration_ == laserStyleGeneration_ &&
			uploadedLaserStyleOpacity_ == clampedOpacity && uploadedLaserDissolveFront_ == effectiveFront)
			return true;
		laserStyleConstants_.parameters.x = clampedOpacity;
		laserStyleConstants_.dissolveProjection = DirectX::XMFLOAT4(effectiveFront.directionX,
			effectiveFront.directionY, effectiveFront.projectionMinimum, effectiveFront.inverseProjectionSpan);
		laserStyleConstants_.dissolveEdge = DirectX::XMFLOAT4(effectiveFront.progress,
			kLaserDissolveParameters.noiseAmplitude, kLaserDissolveParameters.edgeHalfWidth, 0.0f);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(laserStyleCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			laserStyleCacheValid_ = false;
			return false;
		}
		std::memcpy(mapped.pData, &laserStyleConstants_, sizeof(laserStyleConstants_));
		context->Unmap(laserStyleCB.Get(), 0);
		uploadedLaserStyleGeneration_ = laserStyleGeneration_;
		uploadedLaserStyleOpacity_ = clampedOpacity;
		uploadedLaserDissolveFront_ = effectiveFront;
		laserStyleCacheValid_ = true;
		return true;
	}

	int InkRenderer::DrawLaserCoverage(
		std::span<const InkPoint> points, RECT scissorRect)
	{
		if (points.empty()) return 0;
		if (!UpdateLaserStyleConstants(1.0f)) return -1;
		scissorRect.left = std::max(0L, scissorRect.left);
		scissorRect.top = std::max(0L, scissorRect.top);
		scissorRect.right = std::min(
			static_cast<LONG>(viewportWidth), scissorRect.right);
		scissorRect.bottom = std::min(
			static_cast<LONG>(viewportHeight), scissorRect.bottom);
		const bool useScissor = scissorRect.left < scissorRect.right &&
			scissorRect.top < scissorRect.bottom && laserScissorRasterState;
		std::array<InkPoint, 2> dotPoints = {};
		std::span<const InkPoint> drawPoints = points;
		if (points.size() == 1)
		{
			dotPoints[0] = points.front();
			InkPoint dotEnd = points.front();
			dotEnd.x += 0.25f;
			dotPoints[1] = dotEnd;
			drawPoints = dotPoints;
		}

		size_t startIndex = 0;
		while (startIndex + 1 < drawPoints.size())
		{
			const size_t remaining = drawPoints.size() - startIndex;
			const size_t batchCount = std::min(remaining, kMaxBufferCapacity);
			if (FAILED(UploadStructuredBuffer(inkDataBuffer.Get(), drawPoints.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(InkPoint)), 0, D3D11_MAP_WRITE_DISCARD))) return -1;
			D3D11_MAPPED_SUBRESOURCE mapped = {};

			if (FAILED(context->Map(globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->shapeType = 7.0f;
			constants->bufferOffset = 0;
			constants->color = {};
			constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
			context->Unmap(globalCB.Get(), 0);

			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* inkResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, inkResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
			context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->OMSetBlendState(laserCoverageBlendState.Get(), nullptr, 0xFFFFFFFF);
			if (useScissor)
			{
				context->RSSetScissorRects(1, &scissorRect);
				context->RSSetState(laserScissorRasterState.Get());
			}
			else
			{
				context->RSSetState(rasterState.Get());
			}
			context->Draw((static_cast<UINT>(batchCount) - 1) * 6, 0);
			if (useScissor) context->RSSetState(rasterState.Get());

			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResource);
			startIndex += batchCount - 1;
		}
		return 0;
	}

	bool InkRenderer::DrawLaserRectPass(ID3D11RenderTargetView* dstRTV, RECT rect,
		float opacity, float shapeType, ID3D11ShaderResourceView* source,
		UINT sourceSlot, ID3D11BlendState* blendState,
		ID3D11ShaderResourceView* secondarySource, UINT secondarySourceSlot, const LaserDissolveFront& front)
	{
		if (!dstRTV) return false;
		rect.left = std::max(0L, rect.left);
		rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(viewportWidth), rect.right);
		rect.bottom = std::min(static_cast<LONG>(viewportHeight), rect.bottom);
		if (rect.left >= rect.right || rect.top >= rect.bottom) return true;
		if (!UpdateLaserStyleConstants(opacity, front)) return false;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(globalCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->shapeType = shapeType;
		constants->bufferOffset = 0;
		constants->color = DirectX::XMFLOAT4(
			static_cast<float>(rect.left), static_cast<float>(rect.top),
			static_cast<float>(rect.right), static_cast<float>(rect.bottom));
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		context->Unmap(globalCB.Get(), 0);

		// 先解绑 t6-t9，再把其中任意一张 Laser 纹理安全地切换为 RTV。
		UnbindLaserCoverageShaderResources();
		SetOMTarget(dstRTV);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
		context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		if (source && sourceSlot >= 6 && sourceSlot <= 9)
		{
			ID3D11ShaderResourceView* sourceResources[] = { source };
			context->PSSetShaderResources(sourceSlot, 1, sourceResources);
			ID3D11SamplerState* samplers[] = { operatorSampler.Get() };
			context->PSSetSamplers(0, 1, samplers);
		}
		if (secondarySource && secondarySourceSlot >= 6 && secondarySourceSlot <= 9)
		{
			ID3D11ShaderResourceView* sourceResources[] = { secondarySource };
			context->PSSetShaderResources(secondarySourceSlot, 1, sourceResources);
		}
		context->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
		if (laserScissorRasterState)
		{
			context->RSSetScissorRects(1, &rect);
			context->RSSetState(laserScissorRasterState.Get());
		}
		else
		{
			// scissor 是可选优化；矩形 quad 本身仍会把像素范围限制在 rect。
			context->RSSetState(rasterState.Get());
		}
		context->Draw(6, 0);
		context->RSSetState(rasterState.Get());

		ID3D11SamplerState* nullSampler[] = { nullptr };
		UnbindLaserCoverageShaderResources();
		context->PSSetSamplers(0, 1, nullSampler);
		return true;
	}

	bool InkRenderer::ResolveLaserStrokeCoverage(
		ID3D11RenderTargetView* dstRTV, RECT rect, float opacity)
	{
		if (!laserStrokeCoverage.srv) return false;
		return DrawLaserRectPass(dstRTV, rect, opacity, 8.0f,
			laserStrokeCoverage.srv.Get(), 7, operatorResolveBlendState.Get());
	}

	bool InkRenderer::ResolveLaserIncrementalCoverage(
		ID3D11RenderTargetView* dstRTV, RECT rect, float opacity)
	{
		if (!laserStrokeCoverage.srv || !laserLiveCoverage.srv) return false;
		return DrawLaserRectPass(dstRTV, rect, opacity, 13.0f,
			laserStrokeCoverage.srv.Get(), 7, operatorResolveBlendState.Get(),
			laserLiveCoverage.srv.Get(), 9);
	}

	bool InkRenderer::ResolveLaserCompositedColor(
		ID3D11RenderTargetView* dstRTV, RECT rect, float opacity, const LaserDissolveFront& front)
	{
		if (!laserCompositedColor.srv) return false;
		return DrawLaserRectPass(dstRTV, rect, opacity, 11.0f,
			laserCompositedColor.srv.Get(), 6, operatorResolveBlendState.Get(), nullptr, 0, front);
	}

	bool InkRenderer::ClearLaserRect(LaserCoverageResources& layer, RECT rect)
	{
		if (!layer.rtv) return false;
		const RECT canvas = { 0, 0, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight) };
		RECT clipped = {};
		if (!IntersectRect(&clipped, &rect, &canvas)) return true;
		if (!DrawLaserRectPass(layer.rtv.Get(), clipped, 1.0f, 12.0f, nullptr, 0, nullptr))
			return false;

		++laserCounters_.rectClears;
		const RECT& written = layer.writtenBounds;
		// 局部擦除只有覆盖全部已知写域时才归空，实时尾段不会累积历帧写域。
		if (!layer.contentUnknown && clipped.left <= written.left && clipped.top <= written.top &&
			clipped.right >= written.right && clipped.bottom >= written.bottom)
			layer.writtenBounds = {};
		return true;
	}

	bool InkRenderer::BeginLaserUndo(RECT rect, bool preserveIncrementalCoverage)
	{
		LaserCoverageResources replayCoverage;
		if (preserveIncrementalCoverage &&
			!CreateLaserCoverageResources(static_cast<UINT>(viewportWidth),
				static_cast<UINT>(viewportHeight), replayCoverage)) return false;
		if (!BeginLaserBake(rect, true)) return false;
		if (preserveIncrementalCoverage)
		{
			// 活动笔的 t7 前缀需原样保留，临时纹理只用作撤回的逐笔 coverage。
			laserUndoSavedCoverage_ = std::move(laserStrokeCoverage);
			laserStrokeCoverage = std::move(replayCoverage);
		}
		// 至事务提交才更新已烘写域，回滚时保留原范围。
		if (!DrawLaserRectPass(LaserBakeTarget(), rect, 1.0f, 12.0f, nullptr, 0, nullptr))
		{
			FinishLaserUndo(false, {});
			return false;
		}
		++laserCounters_.rectClears;
		return true;
	}

	bool InkRenderer::ReplayLaserStroke(std::span<const InkPoint> points, RECT rect,
		int rasterWidth, int rasterHeight)
	{
		if (!ClearLaserCoverageRect(rect)) return false;
		const float width = viewportWidth, height = viewportHeight;
		SetLaserCoverageTarget(laserStrokeCoverage, laserScissorRasterState ? rect :
			RECT{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) });
		// Resize 只拷贝旧像素。胶囊使用原视口生成坐标和 AA，再在当前纹理中解析。
		const float originalWidth = static_cast<float>(rasterWidth);
		const float originalHeight = static_cast<float>(rasterHeight);
		const bool resized = originalWidth != width || originalHeight != height;
		if (resized) SetScreenSize(originalWidth, originalHeight);
		const bool drawn = DrawLaserCoverage(points, rect) == 0;
		if (resized) SetScreenSize(width, height);
		return drawn && ResolveLaserStrokeCoverage(LaserBakeTarget(), rect);
	}

	void InkRenderer::FinishLaserUndo(bool succeeded, RECT remainingBounds)
	{
		if (succeeded)
		{
			laserCompositedColor.writtenBounds = remainingBounds;
			CommitLaserBake({});
		}
		else AbortLaserBake();
		if (laserUndoSavedCoverage_.rtv)
			laserStrokeCoverage = std::exchange(laserUndoSavedCoverage_, {});
		else
			ClearLaserIncrementalCoverage(); // 下一批不能继承重烘时的 scratch。
	}

	bool InkRenderer::ClearLaserCoverageRect(RECT rect)
	{
		return ClearLaserRect(laserStrokeCoverage, rect);
	}

	bool InkRenderer::ClearLaserLiveCoverageRect(RECT rect)
	{
		return ClearLaserRect(laserLiveCoverage, rect);
	}

	bool InkRenderer::EnsureLaserIncrementalCoverageResources()
	{
		if (!LaserResourcesAvailable()) return false;
		if (laserIncrementalCoverageEnabled_) return true;
		if (laserIncrementalCoverageUnavailable_ || viewportWidth <= 0.0f || viewportHeight <= 0.0f)
			return false;
		if (!CreateLaserCoverageResources(static_cast<UINT>(viewportWidth),
			static_cast<UINT>(viewportHeight), laserLiveCoverage))
		{
			laserLiveCoverage = {};
			laserIncrementalCoverageUnavailable_ = true;
			return false;
		}
		ClearLaserCoverage(laserLiveCoverage);
		laserIncrementalCoverageEnabled_ = true;
		// 资源在绘制线程创建后立刻走零像素 shape 13，避免首笔触发驱动 JIT。
		WarmUpLaserShaders();
		return true;
	}

	bool InkRenderer::LaserIncrementalCoverageAvailable() const noexcept
	{
		return laserIncrementalCoverageEnabled_ && laserLiveCoverage.rtv &&
			laserLiveCoverage.srv;
	}

	void InkRenderer::ClearLaserIncrementalCoverage()
	{
		ClearLaserCoverageContent(laserStrokeCoverage);
		ClearLaserCoverageContent(laserLiveCoverage);
	}

	void InkRenderer::DrawLaserDots(std::span<const LaserDot> dots)
	{
		if (dots.empty() || !backBufferRTV || !UpdateLaserStyleConstants(1.0f)) return;
		size_t startIndex = 0;
		while (startIndex < dots.size())
		{
			const size_t batchCount = std::min(dots.size() - startIndex, kMaxBufferCapacity);
			if (FAILED(UploadStructuredBuffer(inkDataBuffer.Get(), dots.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(LaserDot)), 0, D3D11_MAP_WRITE_DISCARD))) return;
			D3D11_MAPPED_SUBRESOURCE mapped = {};

			if (FAILED(context->Map(globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->shapeType = 9.0f;
			constants->bufferOffset = 0;
			constants->color = {};
			constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
			context->Unmap(globalCB.Get(), 0);

			SetOMTarget(backBufferRTV.Get());
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* inkResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, inkResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
			context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw(static_cast<UINT>(batchCount) * 6, 0);
			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResource);
			startIndex += batchCount;
		}
	}

	void InkRenderer::DrawLaserClickWaves(std::span<const LaserClickWaveVisual> waves)
	{
		if (waves.empty() || !backBufferRTV) return;
		size_t startIndex = 0;
		while (startIndex < waves.size())
		{
			// 每圈占 InkData 两个槽。
			const size_t batchCount = std::min(waves.size() - startIndex, kMaxBufferCapacity / 2);
			if (FAILED(UploadStructuredBuffer(inkDataBuffer.Get(), waves.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(LaserClickWaveVisual)), 0, D3D11_MAP_WRITE_DISCARD))) return;
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->shapeType = 10.0f;
			constants->bufferOffset = 0;
			constants->color = {};
			constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
			context->Unmap(globalCB.Get(), 0);

			SetOMTarget(backBufferRTV.Get());
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* inkResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, inkResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
			context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw(static_cast<UINT>(batchCount) * 6, 0);
			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResource);
			startIndex += batchCount;
		}
	}

	void InkRenderer::DrawLaserParticles(LaserParticlePass pass)
	{
		if (laserParticleSystem_.ActiveRange(pass).count == 0 || !backBufferRTV) return;
		SetOMTarget(backBufferRTV.Get());
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		if (!laserParticleSystem_.Draw(pass, viewportWidth, viewportHeight)) return;
		// 粒子用自己的着色器和 b0。画完把墨迹管线的默认绑定交还，后续绘制不必关心这里改过什么。
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
		context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
	}

	void InkRenderer::WarmUpLaserShaders() noexcept
	{
		// 在主循环开始前，用零宽高视口提交一次各激光 shape 的 draw call。
		// GPU 驱动（尤其 Qualcomm/Adreno 的延迟 JIT 模型）会在收到 draw call 时立即编译
		// 对应的着色器执行路径，而非等到第一次真实绘制。
		// 对已在 CreateShader 阶段完成编译的驱动（Nvidia/AMD/Intel/WARP），此函数
		// 只提交几个零像素 draw call，无可见副作用，开销可忽略。
		if (!context || !backBufferRTV) return;

		// 保存当前视口，替换为零宽高视口以抑制任何像素写入。
		D3D11_VIEWPORT savedVP = {};
		UINT vpCount = 1;
		context->RSGetViewports(&vpCount, &savedVP);
		const D3D11_VIEWPORT emptyVP = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		context->RSSetViewports(1, &emptyVP);

		// shape 9: 激光笔尖 LaserDot（需至少1条数据以通过 dots.empty() 检查）
		const std::array<LaserDot, 1> warmUpDots = {
			LaserDot{ 0.0f, 0.0f, 1.0f }
		};
		DrawLaserDots(warmUpDots);

		// shape 7/8/11/12: 笔画 coverage 生成与 resolve 路径
		if (laserStrokeCoverage.rtv && laserCompositedColor.rtv && laserCompositedColor.srv)
		{
			const RECT warmUpRect{ 0, 0, 1, 1 };
			const std::array<InkPoint, 2> warmUpPts = {
				InkPoint{ 0.0f, 0.0f, 1.0f, 0.0f },
				InkPoint{ 1.0f, 0.0f, 1.0f, 0.0f }
			};
			DrawLaserRectPass(laserStrokeCoverage.rtv.Get(), warmUpRect, 1.0f,
				12.0f, nullptr, 0, nullptr); // 零视口只预热，不登记清除或写域。
			SetLaserCoverageTarget(laserStrokeCoverage, RECT{});
			DrawLaserCoverage(warmUpPts);                                    // shape 7
			ResolveLaserStrokeCoverage(                                      // shape 8
				laserCompositedColor.rtv.Get(), warmUpRect);
			if (LaserIncrementalCoverageAvailable())
			{
				DrawLaserRectPass(laserLiveCoverage.rtv.Get(), warmUpRect, 1.0f,
					12.0f, nullptr, 0, nullptr);
				ResolveLaserIncrementalCoverage( // shape 13
					laserCompositedColor.rtv.Get(), warmUpRect);
			}
			ResolveLaserCompositedColor(                                     // shape 11
				backBufferRTV.Get(), warmUpRect, 1.0f);
		}

		// 恢复视口，不留任何可见副作用。
		context->RSSetViewports(1, &savedVP);
	}

}
