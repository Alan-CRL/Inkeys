module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <d3d11.h>
#include <DirectXMath.h>
#include <windows.h>

module Inkeys.Drawing.Draw3.renderer;

namespace Inkeys::Drawing::Draw3
{
	using renderer_detail::GlobalShaderConstants;
	using renderer_detail::TrustedL2SnapshotShaderConstants;

	namespace
	{
		constexpr float kMaximumTrustedSnapshotBlurDip = 12.0f;

		void ReportTrustedSnapshotUnavailableOnce() noexcept
		{
			static bool reported = false;
			if (reported) return;
			reported = true;
			::OutputDebugStringW(
				L"[Draw3] 可信 L2 快照资源不可用，已回退到权威 tile 恢复。\n");
		}

		bool IsFinite(float value) noexcept
		{
			return std::isfinite(value);
		}
	}

	bool InkRenderer::CreateTrustedL2SnapshotResources(UINT width, UINT height) noexcept
	{
		ReleaseTrustedL2SnapshotResources();
		if (!device || width == 0 || height == 0) return false;

		D3D11_TEXTURE2D_DESC description = {};
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&description, nullptr,
			trustedL2SnapshotTexture_.ReleaseAndGetAddressOf())) ||
			FAILED(device->CreateShaderResourceView(trustedL2SnapshotTexture_.Get(), nullptr,
				trustedL2SnapshotSRV_.ReleaseAndGetAddressOf())))
		{
			ReleaseTrustedL2SnapshotResources();
			ReportTrustedSnapshotUnavailableOnce();
			return false;
		}
		return true;
	}

	bool InkRenderer::CreateTrustedL2SnapshotPipeline() noexcept
	{
		ReleaseTrustedL2SnapshotPipeline();
		if (!device) return false;

		D3D11_BUFFER_DESC bufferDescription = {};
		bufferDescription.ByteWidth = sizeof(TrustedL2SnapshotShaderConstants);
		bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
		bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&bufferDescription, nullptr,
			trustedL2SnapshotCB_.ReleaseAndGetAddressOf())))
		{
			ReleaseTrustedL2SnapshotPipeline();
			ReportTrustedSnapshotUnavailableOnce();
			return false;
		}

		D3D11_SAMPLER_DESC samplerDescription = {};
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&samplerDescription,
			trustedL2SnapshotSampler_.ReleaseAndGetAddressOf())))
		{
			ReleaseTrustedL2SnapshotPipeline();
			ReportTrustedSnapshotUnavailableOnce();
			return false;
		}

		D3D11_BLEND_DESC blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		// 当前清晰 L2 已在 destination；快照只补在其透明度下方。
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_ALPHA;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription,
			trustedL2SnapshotUnderBlendState_.ReleaseAndGetAddressOf())))
		{
			ReleaseTrustedL2SnapshotPipeline();
			ReportTrustedSnapshotUnavailableOnce();
			return false;
		}
		return true;
	}

	void InkRenderer::ReleaseTrustedL2SnapshotResources() noexcept
	{
		if (context)
		{
			ID3D11ShaderResourceView* nullSnapshot[] = { nullptr };
			context->PSSetShaderResources(14, 1, nullSnapshot);
		}
		trustedL2SnapshotSRV_.Reset();
		trustedL2SnapshotTexture_.Reset();
		InvalidateTrustedL2Snapshot();
	}

	void InkRenderer::ReleaseTrustedL2SnapshotPipeline() noexcept
	{
		trustedL2SnapshotUnderBlendState_.Reset();
		trustedL2SnapshotSampler_.Reset();
		trustedL2SnapshotCB_.Reset();
	}

	void InkRenderer::InvalidateTrustedL2Snapshot() noexcept
	{
		trustedL2SnapshotValid_ = false;
		trustedL2SnapshotViewportX_ = 0.0f;
		trustedL2SnapshotViewportY_ = 0.0f;
	}

	bool InkRenderer::RefreshTrustedL2Snapshot(
		float snapshotViewportX, float snapshotViewportY) noexcept
	{
		if (!context || !layerL2Texture || !trustedL2SnapshotTexture_ ||
			!IsFinite(snapshotViewportX) || !IsFinite(snapshotViewportY))
		{
			InvalidateTrustedL2Snapshot();
			return false;
		}

		D3D11_TEXTURE2D_DESC sourceDescription = {};
		D3D11_TEXTURE2D_DESC snapshotDescription = {};
		layerL2Texture->GetDesc(&sourceDescription);
		trustedL2SnapshotTexture_->GetDesc(&snapshotDescription);
		if (sourceDescription.Width != snapshotDescription.Width ||
			sourceDescription.Height != snapshotDescription.Height ||
			sourceDescription.Format != snapshotDescription.Format)
		{
			InvalidateTrustedL2Snapshot();
			return false;
		}

		ID3D11ShaderResourceView* nullSnapshot[] = { nullptr };
		context->PSSetShaderResources(14, 1, nullSnapshot);
		context->OMSetRenderTargets(0, nullptr, nullptr); // L2 可能仍作为 RTV，复制前先解除输出绑定。
		context->CopyResource(trustedL2SnapshotTexture_.Get(), layerL2Texture.Get());
		trustedL2SnapshotViewportX_ = snapshotViewportX;
		trustedL2SnapshotViewportY_ = snapshotViewportY;
		trustedL2SnapshotValid_ = true;
		return true;
	}

	bool InkRenderer::CompositeTrustedL2SnapshotToBackBuffer(
		const TrustedL2SnapshotCompositeRequest& request) noexcept
	{
		if (!context || !backBufferTexture || !backBufferRTV || !layerL2Texture ||
			!globalCB || !vertexShader || !pixelShader || !rasterState ||
			viewportWidth <= 0.0f || viewportHeight <= 0.0f ||
			!IsFinite(request.currentViewportX) || !IsFinite(request.currentViewportY) ||
			!IsFinite(request.contentMotionX) || !IsFinite(request.contentMotionY) ||
			!IsFinite(request.blurDip) || !IsFinite(request.dpiScale) ||
			request.dpiScale <= 0.0f) return false;

		RECT copyRect = request.rect;
		copyRect.left = std::max(0L, copyRect.left);
		copyRect.top = std::max(0L, copyRect.top);
		copyRect.right = std::min(static_cast<LONG>(viewportWidth), copyRect.right);
		copyRect.bottom = std::min(static_cast<LONG>(viewportHeight), copyRect.bottom);
		if (copyRect.left >= copyRect.right || copyRect.top >= copyRect.bottom) return false;

		// 先放回当前权威 L2；快照 pass 只在其下方补透明缺口。
		context->OMSetRenderTargets(0, nullptr, nullptr);
		CopyResource(backBufferTexture.Get(), layerL2Texture.Get(), copyRect);
		if (!trustedL2SnapshotValid_ || !trustedL2SnapshotTexture_ ||
			!trustedL2SnapshotSRV_ || !trustedL2SnapshotCB_ ||
			!trustedL2SnapshotSampler_ || !trustedL2SnapshotUnderBlendState_)
			return false;

		const double currentLeft = request.currentViewportX;
		const double currentTop = request.currentViewportY;
		const double snapshotLeft = trustedL2SnapshotViewportX_;
		const double snapshotTop = trustedL2SnapshotViewportY_;
		const double worldLeft = std::max(currentLeft, snapshotLeft);
		const double worldTop = std::max(currentTop, snapshotTop);
		const double worldRight = std::min(
			currentLeft + viewportWidth, snapshotLeft + viewportWidth);
		const double worldBottom = std::min(
			currentTop + viewportHeight, snapshotTop + viewportHeight);
		if (!(worldLeft < worldRight && worldTop < worldBottom)) return false;

		float targetLeft = static_cast<float>(worldLeft - currentLeft);
		float targetTop = static_cast<float>(worldTop - currentTop);
		float targetRight = static_cast<float>(worldRight - currentLeft);
		float targetBottom = static_cast<float>(worldBottom - currentTop);
		targetLeft = std::max(targetLeft, static_cast<float>(copyRect.left));
		targetTop = std::max(targetTop, static_cast<float>(copyRect.top));
		targetRight = std::min(targetRight, static_cast<float>(copyRect.right));
		targetBottom = std::min(targetBottom, static_cast<float>(copyRect.bottom));
		if (!(targetLeft < targetRight && targetTop < targetBottom)) return false;

		const float sourceOffsetX = request.currentViewportX - trustedL2SnapshotViewportX_;
		const float sourceOffsetY = request.currentViewportY - trustedL2SnapshotViewportY_;
		const float inverseWidth = 1.0f / viewportWidth;
		const float inverseHeight = 1.0f / viewportHeight;
		float motionX = request.contentMotionX;
		float motionY = request.contentMotionY;
		const float motionLength = std::sqrt(motionX * motionX + motionY * motionY);
		if (motionLength > 1e-5f)
		{
			motionX /= motionLength;
			motionY /= motionLength;
		}
		else motionX = motionY = 0.0f;
		const float blurPixels = std::clamp(
			request.blurDip, 0.0f, kMaximumTrustedSnapshotBlurDip) * request.dpiScale;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;
		auto* global = static_cast<GlobalShaderConstants*>(mapped.pData);
		*global = {};
		global->width = viewportWidth;
		global->height = viewportHeight;
		global->shapeType = 20.0f;
		context->Unmap(globalCB.Get(), 0);

		if (FAILED(context->Map(
			trustedL2SnapshotCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;
		auto* snapshot = static_cast<TrustedL2SnapshotShaderConstants*>(mapped.pData);
		snapshot->targetRect = { targetLeft, targetTop, targetRight, targetBottom };
		snapshot->sourceUvRect = {
			(targetLeft + sourceOffsetX) * inverseWidth,
			(targetTop + sourceOffsetY) * inverseHeight,
			(targetRight + sourceOffsetX) * inverseWidth,
			(targetBottom + sourceOffsetY) * inverseHeight
		};
		snapshot->blurUv = {
			motionX * blurPixels * inverseWidth,
			motionY * blurPixels * inverseHeight,
			0.0f, 0.0f
		};
		context->Unmap(trustedL2SnapshotCB_.Get(), 0);

		SetOMTarget(backBufferRTV.Get());
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11Buffer* globalBuffers[] = { globalCB.Get() };
		ID3D11Buffer* snapshotBuffers[] = { trustedL2SnapshotCB_.Get() };
		context->VSSetConstantBuffers(0, 1, globalBuffers);
		context->VSSetConstantBuffers(3, 1, snapshotBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, globalBuffers);
		context->PSSetConstantBuffers(3, 1, snapshotBuffers);
		ID3D11ShaderResourceView* snapshotResources[] = { trustedL2SnapshotSRV_.Get() };
		context->PSSetShaderResources(14, 1, snapshotResources);
		ID3D11SamplerState* samplers[] = { trustedL2SnapshotSampler_.Get() };
		context->PSSetSamplers(0, 1, samplers);
		context->OMSetBlendState(
			trustedL2SnapshotUnderBlendState_.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->Draw(6, 0);

		ID3D11ShaderResourceView* nullResources[] = { nullptr };
		ID3D11Buffer* nullBuffers[] = { nullptr };
		ID3D11SamplerState* nullSamplers[] = { nullptr };
		context->PSSetShaderResources(14, 1, nullResources);
		context->VSSetConstantBuffers(3, 1, nullBuffers);
		context->PSSetConstantBuffers(3, 1, nullBuffers);
		context->PSSetSamplers(0, 1, nullSamplers);
		return true;
	}
}
