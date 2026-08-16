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
#include <span>
#include <windows.h>

module Inkeys.Drawing.Draw3.renderer;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元集中维护激光 coverage、材质解析和粒子绘制路径。
	using renderer_detail::GlobalShaderConstants;

	void InkRenderer::ConfigureLaserStyle(float dpiScale) noexcept
	{
		const float scale = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		const float solidRadius = LaserSolidRadius(scale);
		const float coreRadius = LaserCoreRadius(solidRadius);
		laserStyleConstants_.radii = DirectX::XMFLOAT4(
			coreRadius, solidRadius, LaserDiffuseExtent(scale),
			coreRadius * kLaserScatterHalfWidthToCoreRatio);
		laserStyleConstants_.coreColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		laserStyleConstants_.scatterColor = DirectX::XMFLOAT4(
			1.0f, 240.0f / 255.0f, 243.0f / 255.0f, 0.94f);
		laserStyleConstants_.borderColor = DirectX::XMFLOAT4(
			1.0f, 11.0f / 255.0f, 30.0f / 255.0f, 0.98f);
		laserStyleConstants_.edgeColor = DirectX::XMFLOAT4(
			1.0f, 112.0f / 255.0f, 128.0f / 255.0f, 0.72f);
		// 漫反射在实体边界达到满 alpha；edgeColor.a 只控制粉色高光的 RGB 混合强度。
		laserStyleConstants_.glowColor = DirectX::XMFLOAT4(1.0f, 0.04f, 0.10f, 1.0f);
		// z/w 是红色实体外侧漫反射曲线的边缘高亮阈值。
		laserStyleConstants_.parameters = DirectX::XMFLOAT4(
			1.0f, scale, 0.20f, 0.29f);
		++laserStyleGeneration_;
		if (laserStyleGeneration_ == 0) laserStyleGeneration_ = 1;
		laserStyleCacheValid_ = false;
	}

	void InkRenderer::ConfigureLaserParticles(
		const LaserParticleConfig& configuration, float dpiScale) noexcept
	{
		const LaserParticleConfig effectiveConfiguration =
			IsValidLaserParticleConfig(configuration)
			? configuration : LaserParticleConfig{};
		laserParticleGlowRadiusScale_ =
			effectiveConfiguration.glowRadiusScale;
		laserParticleGlowRed_ = effectiveConfiguration.glowRed;
		laserParticleGlowGreen_ = effectiveConfiguration.glowGreen;
		laserParticleGlowBlue_ = effectiveConfiguration.glowBlue;
		laserParticleGlowAlpha_ = effectiveConfiguration.glowAlpha;
		laserParticleSystem_.Configure(
			effectiveConfiguration, dpiScale, kLaserCoreDiameterRatio);
	}

	bool InkRenderer::LaserParticlesAvailable() const noexcept
	{
		return laserParticleSystem_.IsAvailable();
	}

	void InkRenderer::StepLaserParticles(float wallDeltaSeconds,
		float motionDeltaSeconds, bool simulateExisting,
		std::span<const LaserParticleEmissionRequest> emissionRequests) noexcept
	{
		laserParticleSystem_.Step(wallDeltaSeconds, motionDeltaSeconds,
			simulateExisting, emissionRequests);
	}

	void InkRenderer::ResetLaserParticles() noexcept
	{
		laserParticleSystem_.Reset();
	}

	bool InkRenderer::UpdateLaserStyleConstants(float opacity)
	{
		if (!laserStyleCB) return false;
		const float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);
		if (laserStyleCacheValid_ &&
			uploadedLaserStyleGeneration_ == laserStyleGeneration_ &&
			uploadedLaserStyleOpacity_ == clampedOpacity)
			return true;
		laserStyleConstants_.parameters.x = clampedOpacity;
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
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(inkDataBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			std::memcpy(mapped.pData, drawPoints.data() + startIndex,
				batchCount * sizeof(InkPoint));
			context->Unmap(inkDataBuffer.Get(), 0);

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
		ID3D11ShaderResourceView* secondarySource, UINT secondarySourceSlot)
	{
		if (!dstRTV) return false;
		rect.left = std::max(0L, rect.left);
		rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(viewportWidth), rect.right);
		rect.bottom = std::min(static_cast<LONG>(viewportHeight), rect.bottom);
		if (rect.left >= rect.right || rect.top >= rect.bottom) return true;
		if (!UpdateLaserStyleConstants(opacity)) return false;

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

	void InkRenderer::ResolveLaserStrokeCoverage(
		ID3D11RenderTargetView* dstRTV, RECT rect, float opacity)
	{
		if (!laserStrokeCoverage.srv) return;
		DrawLaserRectPass(dstRTV, rect, opacity, 8.0f,
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

	void InkRenderer::ResolveLaserCompositedColor(
		ID3D11RenderTargetView* dstRTV, RECT rect, float opacity)
	{
		if (!laserCompositedColor.srv) return;
		DrawLaserRectPass(dstRTV, rect, opacity, 11.0f,
			laserCompositedColor.srv.Get(), 6, operatorResolveBlendState.Get());
	}

	void InkRenderer::ClearLaserCoverageRect(RECT rect)
	{
		if (!laserStrokeCoverage.rtv) return;
		DrawLaserRectPass(laserStrokeCoverage.rtv.Get(), rect, 1.0f,
			12.0f, nullptr, 0, nullptr);
	}

	bool InkRenderer::ClearLaserLiveCoverageRect(RECT rect)
	{
		if (!laserLiveCoverage.rtv) return false;
		return DrawLaserRectPass(laserLiveCoverage.rtv.Get(), rect, 1.0f,
			12.0f, nullptr, 0, nullptr);
	}

	bool InkRenderer::EnsureLaserIncrementalCoverageResources()
	{
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
		ClearLaserCoverage(laserStrokeCoverage);
		ClearLaserCoverage(laserLiveCoverage);
	}

	void InkRenderer::DrawLaserDots(std::span<const LaserDot> dots)
	{
		if (dots.empty() || !backBufferRTV || !UpdateLaserStyleConstants(1.0f)) return;
		size_t startIndex = 0;
		while (startIndex < dots.size())
		{
			const size_t batchCount = std::min(dots.size() - startIndex, kMaxBufferCapacity);
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(inkDataBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
			std::memcpy(mapped.pData, dots.data() + startIndex,
				batchCount * sizeof(LaserDot));
			context->Unmap(inkDataBuffer.Get(), 0);
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

	void InkRenderer::DrawLaserParticles()
	{
		ID3D11ShaderResourceView* particleResource =
			laserParticleSystem_.ParticleShaderResourceView();
		if (!particleResource || !backBufferRTV ||
			!UpdateLaserStyleConstants(1.0f)) return;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(globalCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->shapeType = 10.0f;
		constants->bufferOffset = 0;
		// shape 10：color.x 传辉光半径倍率，color.zw 与 padding.xy 传辉光颜色。
		// 其余槽位由 shader 忽略，显式清零以免保留无效状态。
		constants->color = DirectX::XMFLOAT4(
			laserParticleGlowRadiusScale_, 0.0f,
			laserParticleGlowGreen_, laserParticleGlowBlue_);
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		constants->padding[0] = laserParticleGlowRed_;
		constants->padding[1] = laserParticleGlowAlpha_;
		constants->padding[2] = 0.0f;
		context->Unmap(globalCB.Get(), 0);

		SetOMTarget(backBufferRTV.Get());
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		context->VSSetShaderResources(8, 1, &particleResource);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
		context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->DrawInstanced(6, kLaserParticleCapacity, 0, 0);

		ID3D11ShaderResourceView* nullParticle[] = { nullptr };
		context->VSSetShaderResources(8, 1, nullParticle);
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

		// shape 10: 粒子 VS/PS（DrawLaserParticles 内部含所有绑定与 DrawInstanced）
		DrawLaserParticles();

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
			ClearLaserCoverageRect(warmUpRect);                              // shape 12
			SetLaserCoverageTarget(laserStrokeCoverage);
			DrawLaserCoverage(warmUpPts);                                    // shape 7
			ResolveLaserStrokeCoverage(                                      // shape 8
				laserCompositedColor.rtv.Get(), warmUpRect);
			if (LaserIncrementalCoverageAvailable())
			{
				ClearLaserLiveCoverageRect(warmUpRect);
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
