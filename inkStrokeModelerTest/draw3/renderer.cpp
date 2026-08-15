module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../resource.h"
#include "../laserParticleResource.h"

#include <algorithm>
#include <cstdint>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

module draw3.renderer;

namespace draw3
{
	using renderer_detail::GlobalShaderConstants;

	namespace
	{
		struct ShaderBlob
		{
			const void* data;
			size_t size;
		};

		// 从当前模块的 SHADER 资源中读取已编译字节码。
		ShaderBlob LoadShaderFromResource(int resourceID)
		{
			HMODULE module = ::GetModuleHandle(nullptr);
			HRSRC resource = ::FindResource(module, MAKEINTRESOURCE(resourceID), L"SHADER");
			if (!resource) return { nullptr, 0 };

			HGLOBAL memory = ::LoadResource(module, resource);
			if (!memory) return { nullptr, 0 };
			return { ::LockResource(memory), static_cast<size_t>(::SizeofResource(module, resource)) };
		}

		void ReportLaserParticleUnavailableOnce() noexcept
		{
			static bool reported = false;
			if (reported) return;
			reported = true;
			::OutputDebugStringW(
				L"[Draw3] D3D11 Compute Shader 激光粒子不可用，已仅保留激光主体。\n");
		}
	}
	void InkRenderer::CopyResource(ID3D11Texture2D* dst, ID3D11Texture2D* src, RECT rect)
	{
		D3D11_BOX sourceRegion = {};
		sourceRegion.left = rect.left;
		sourceRegion.top = rect.top;
		sourceRegion.front = 0;
		sourceRegion.right = rect.right;
		sourceRegion.bottom = rect.bottom;
		sourceRegion.back = 1;
		context->CopySubresourceRegion(dst, 0, rect.left, rect.top, 0, src, 0, &sourceRegion); // 只复制脏矩形，减少 backbuffer 更新量。
	}

	bool InkRenderer::ApplyOperatorLayers(ID3D11RenderTargetView* dstRTV,
		const OperatorLayerResources& stableLayer, const OperatorLayerResources& liveLayer,
		RECT rect, OperatorLayerMergeMode mergeMode)
	{
		if (!dstRTV || !stableLayer.addSRV || !stableLayer.retainSRV ||
			!liveLayer.addSRV || !liveLayer.retainSRV) return false;
		rect.left = std::max(0L, rect.left);
		rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(viewportWidth), rect.right);
		rect.bottom = std::min(static_cast<LONG>(viewportHeight), rect.bottom);
		if (rect.left >= rect.right || rect.top >= rect.bottom) return true;

		InkPoint rectPoints[2] = {
			{ static_cast<float>(rect.left), static_cast<float>(rect.top), 0.0f, 0.0f },
			{ static_cast<float>(rect.right), static_cast<float>(rect.bottom), 0.0f, 0.0f }
		};
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(inkDataBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		std::memcpy(mapped.pData, rectPoints, sizeof(rectPoints)); // 复用墨迹缓冲区传入要混合的矩形范围。
		context->Unmap(inkDataBuffer.Get(), 0);
		m_bufferHead = 2;

		if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->color = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		constants->shapeType = mergeMode == OperatorLayerMergeMode::Ordered ? 2.0f : 1.0f;
		constants->bufferOffset = 0;
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		context->Unmap(globalCB.Get(), 0);

		SetOMTarget(dstRTV);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
		context->VSSetShaderResources(0, 1, shaderResources);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
		context->VSSetConstantBuffers(0, 1, constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, constantBuffers);
		ID3D11ShaderResourceView* compositeResources[] = {
			stableLayer.addSRV.Get(),
			stableLayer.retainSRV.Get(),
			nullptr, // t3 留给 VS 的荧光笔结构化缓冲区。
			liveLayer.addSRV.Get(),
			liveLayer.retainSRV.Get()
		};
		context->PSSetShaderResources(1, ARRAYSIZE(compositeResources), compositeResources);
		ID3D11SamplerState* samplers[] = { operatorSampler.Get() };
		context->PSSetSamplers(0, 1, samplers);
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->Draw(6, 0); // 双源混合一次完成 Result = Add + Retain * Below。

		ID3D11ShaderResourceView* nullResources[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
		ID3D11SamplerState* nullSampler[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullResources);
		context->PSSetShaderResources(1, ARRAYSIZE(nullResources), nullResources); // 解除 SRV 绑定，避免后续作为 RTV 时冲突。
		context->PSSetSamplers(0, 1, nullSampler);
		return true;
	}

	void InkRenderer::SetScreenSize(float width, float height)
	{
		viewportWidth = width;
		viewportHeight = height;
		D3D11_VIEWPORT viewport = { 0, 0, width, height, 0.0f, 1.0f };
		context->RSSetViewports(1, &viewport);
	}

	double InkRenderer::QueryVideoMemoryUsageMiB() const noexcept
	{
		if (!videoMemoryAdapter_) return -1.0;
		DXGI_QUERY_VIDEO_MEMORY_INFO local = {};
		DXGI_QUERY_VIDEO_MEMORY_INFO nonLocal = {};
		const bool localAvailable = SUCCEEDED(videoMemoryAdapter_->QueryVideoMemoryInfo(
			0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local));
		const bool nonLocalAvailable = SUCCEEDED(videoMemoryAdapter_->QueryVideoMemoryInfo(
			0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal));
		if (!localAvailable && !nonLocalAvailable) return -1.0;
		const UINT64 usageBytes = (localAvailable ? local.CurrentUsage : 0) +
			(nonLocalAvailable ? nonLocal.CurrentUsage : 0);
		return static_cast<double>(usageBytes) / (1024.0 * 1024.0);
	}

	void InkRenderer::SetOMTarget(ID3D11RenderTargetView* renderTargetView)
	{
		ID3D11RenderTargetView* targets[] = { renderTargetView };
		context->OMSetRenderTargets(1, targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::SetOperatorTarget(const OperatorLayerResources& layer)
	{
		ID3D11RenderTargetView* targets[] = { layer.addRTV.Get(), layer.retainRTV.Get() };
		context->OMSetRenderTargets(ARRAYSIZE(targets), targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::SetLaserCoverageTarget(const LaserCoverageResources& layer)
	{
		UnbindLaserCoverageShaderResources();
		ID3D11RenderTargetView* targets[] = { layer.rtv.Get() };
		context->OMSetRenderTargets(1, targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::SetLaserLiveCoverageTarget()
	{
		SetLaserCoverageTarget(laserLiveCoverage);
	}

	void InkRenderer::ClearRTV(ID3D11RenderTargetView* renderTargetView, DirectX::XMFLOAT4 color)
	{
		const float clearColor[4] = { color.x, color.y, color.z, color.w };
		context->ClearRenderTargetView(renderTargetView, clearColor);
	}

	void InkRenderer::ClearOperatorLayer(const OperatorLayerResources& layer)
	{
		ClearRTV(layer.addRTV.Get(), kTransparentLayerClearColor);
		ClearRTV(layer.retainRTV.Get(), DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f)); // Retain=1 表示不改变下层。
	}

	void InkRenderer::ClearLaserCoverage(const LaserCoverageResources& layer)
	{
		if (!layer.rtv) return;
		UnbindLaserCoverageShaderResources();
		ClearRTV(layer.rtv.Get(), kTransparentLayerClearColor);
	}

	void InkRenderer::UnbindLaserCoverageShaderResources()
	{
		if (!context) return;
		ID3D11ShaderResourceView* nullLaserResources[] = {
			nullptr, nullptr, nullptr, nullptr };
		context->PSSetShaderResources(6, ARRAYSIZE(nullLaserResources), nullLaserResources);
	}

	void InkRenderer::ClearAllLaserCoverage()
	{
		ClearLaserCoverage(laserCompositedColor);
		ClearLaserIncrementalCoverage();
	}

	bool InkRenderer::CreateOperatorLayerResources(UINT width, UINT height, OperatorLayerResources& layer)
	{
		D3D11_TEXTURE2D_DESC textureDescription = {};
		textureDescription.Width = width;
		textureDescription.Height = height;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr,
			layer.addTexture.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layer.addTexture.Get(), nullptr,
			layer.addRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layer.addTexture.Get(), nullptr,
			layer.addSRV.ReleaseAndGetAddressOf()))) return false;

		textureDescription.Format = DXGI_FORMAT_R16_FLOAT;
		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr,
			layer.retainTexture.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layer.retainTexture.Get(), nullptr,
			layer.retainRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layer.retainTexture.Get(), nullptr,
			layer.retainSRV.ReleaseAndGetAddressOf()))) return false;
		return true;
	}

	bool InkRenderer::CreateLaserCoverageResources(
		UINT width, UINT height, LaserCoverageResources& layer)
	{
		D3D11_TEXTURE2D_DESC description = {};
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&description, nullptr,
			layer.texture.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layer.texture.Get(), nullptr,
			layer.rtv.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layer.texture.Get(), nullptr,
			layer.srv.ReleaseAndGetAddressOf()))) return false;
		return true;
	}

	bool InkRenderer::CreateSizeDependentResources(IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		if (!device || !context || !swapChain || width == 0 || height == 0) return false;
		if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(backBufferTexture.ReleaseAndGetAddressOf())))) return false; // 取得最终呈现缓冲区。
		if (FAILED(device->CreateRenderTargetView(backBufferTexture.Get(), nullptr,
			backBufferRTV.ReleaseAndGetAddressOf()))) return false;

		D3D11_TEXTURE2D_DESC textureDescription = {};
		textureDescription.Width = width;
		textureDescription.Height = height;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr, layerL2Texture.ReleaseAndGetAddressOf()))) return false; // L2 保存已经落定的完整画布。

		D3D11_RENDER_TARGET_VIEW_DESC renderTargetDescription = {};
		renderTargetDescription.Format = textureDescription.Format;
		renderTargetDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateRenderTargetView(layerL2Texture.Get(), &renderTargetDescription, layerL2RTV.ReleaseAndGetAddressOf()))) return false;
		// 快照是可选视觉兜底；分配失败时权威 L2 与逐 tile 恢复仍可继续。
		CreateTrustedL2SnapshotResources(width, height);
		if (!CreateOperatorLayerResources(width, height, layerL1)) return false; // L1 保存当前笔画已确认前缀操作。
		if (!CreateOperatorLayerResources(width, height, layerL0)) return false; // L0 保存每帧变化的笔锋和预测操作。
		if (!CreateLaserCoverageResources(width, height, laserCompositedColor)) return false;
		if (!CreateLaserCoverageResources(width, height, laserStrokeCoverage)) return false;
		if (laserIncrementalCoverageEnabled_ &&
			!CreateLaserCoverageResources(width, height, laserLiveCoverage))
		{
			// 可选资源失败只关闭增量，主体和完整重绘路径仍可继续工作。
			laserLiveCoverage = {};
			laserIncrementalCoverageEnabled_ = false;
			laserIncrementalCoverageUnavailable_ = true;
		}

		SetScreenSize(static_cast<float>(width), static_cast<float>(height));
		return true;
	}

	void InkRenderer::ReleaseSizeDependentResources()
	{
		if (context)
		{
			ID3D11ShaderResourceView* nullResources[] = {
				nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->VSSetShaderResources(0, ARRAYSIZE(nullResources), nullResources); // t8 粒子 SRV 也随 Resize 显式解绑。
			context->PSSetShaderResources(0, ARRAYSIZE(nullResources), nullResources); // 释放前先解绑，避免 D3D 仍持有引用。
			context->Flush();
		}
		backBufferRTV.Reset();
		backBufferTexture.Reset();
		ReleaseTrustedL2SnapshotResources();
		layerL2RTV.Reset();
		layerL2Texture.Reset();
		layerL1.addRTV.Reset();
		layerL1.addSRV.Reset();
		layerL1.addTexture.Reset();
		layerL1.retainRTV.Reset();
		layerL1.retainSRV.Reset();
		layerL1.retainTexture.Reset();
		layerL0.addRTV.Reset();
		layerL0.addSRV.Reset();
		layerL0.addTexture.Reset();
		layerL0.retainRTV.Reset();
		layerL0.retainSRV.Reset();
		layerL0.retainTexture.Reset();
		laserCompositedColor.rtv.Reset();
		laserCompositedColor.srv.Reset();
		laserCompositedColor.texture.Reset();
		laserStrokeCoverage.rtv.Reset();
		laserStrokeCoverage.srv.Reset();
		laserStrokeCoverage.texture.Reset();
		laserLiveCoverage.rtv.Reset();
		laserLiveCoverage.srv.Reset();
		laserLiveCoverage.texture.Reset();
	}

	void InkRenderer::ReleaseResources()
	{
		laserParticleSystem_.Release();
		ReleaseSizeDependentResources();
		vertexShader.Reset();
		pixelShader.Reset();
		globalCB.Reset();
		laserStyleCB.Reset();
		inkDataBuffer.Reset();
		inkDataSRV.Reset();
		highlighterPrimitiveBuffer.Reset();
		highlighterPrimitiveSRV.Reset();
		operatorSampler.Reset();
		ReleaseTrustedL2SnapshotPipeline();
		strokeOperatorBlendState.Reset();
		operatorResolveBlendState.Reset();
		laserCoverageBlendState.Reset();
		rasterState.Reset();
		laserScissorRasterState.Reset();
		dsState.Reset();
		videoMemoryAdapter_.Reset();
		device.Reset();
		context.Reset();
		laserStyleCacheValid_ = false;
		uploadedLaserStyleGeneration_ = 0;
		uploadedLaserStyleOpacity_ = -1.0f;
		laserIncrementalCoverageEnabled_ = false;
		laserIncrementalCoverageUnavailable_ = false;
		m_bufferHead = 0;
		viewportWidth = 0.0f;
		viewportHeight = 0.0f;
	}

	bool InkRenderer::Resize(IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		if (!swapChain || width == 0 || height == 0) return false;
		DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {};
		if (FAILED(swapChain->GetDesc1(&swapChainDescription))) return false;

		const UINT oldWidth = static_cast<UINT>(viewportWidth);
		const UINT oldHeight = static_cast<UINT>(viewportHeight);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL2Texture = layerL2Texture; // 临时保留旧稳定层用于拷贝。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL1AddTexture = layerL1.addTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL1RetainTexture = layerL1.retainTexture; // L1 的完整仿射操作都要保留。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldLaserCompositedTexture =
			laserCompositedColor.texture;
		ReleaseSizeDependentResources();

		// waitable swapchain 在部分驱动上要求 resize 时原样保留 BufferCount/Format/Flags。
		if (FAILED(swapChain->ResizeBuffers(swapChainDescription.BufferCount, width, height,
			swapChainDescription.Format, swapChainDescription.Flags))) return false;
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;

		ClearRTV(layerL2RTV.Get(), kTransparentLayerClearColor);
		ClearOperatorLayer(layerL1);
		ClearOperatorLayer(layerL0);
		ClearAllLaserCoverage();
		ClearRTV(backBufferRTV.Get(), kTransparentLayerClearColor);

		// 缩放后只保留新旧画布左上角的交集，不拉伸已有内容。
		const UINT copyWidth = std::min(oldWidth, width);
		const UINT copyHeight = std::min(oldHeight, height);
		if (copyWidth > 0 && copyHeight > 0)
		{
			RECT keepRect = { 0, 0, static_cast<LONG>(copyWidth), static_cast<LONG>(copyHeight) };
			if (oldL2Texture) CopyResource(layerL2Texture.Get(), oldL2Texture.Get(), keepRect); // 保留已完成笔迹。
			if (oldL1AddTexture) CopyResource(layerL1.addTexture.Get(), oldL1AddTexture.Get(), keepRect);
			if (oldL1RetainTexture) CopyResource(layerL1.retainTexture.Get(), oldL1RetainTexture.Get(), keepRect); // 保留正在绘制的稳定前缀。
			if (oldLaserCompositedTexture)
				CopyResource(laserCompositedColor.texture.Get(),
					oldLaserCompositedTexture.Get(), keepRect);
		}
		return true;
	}

	bool InkRenderer::Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		device = inDevice; // 渲染器只借用外部统一创建的 D3D 设备。
		context = inContext;
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		if (SUCCEEDED(device.As(&dxgiDevice)) && dxgiDevice &&
			SUCCEEDED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf())) && adapter)
			adapter.As(&videoMemoryAdapter_); // Win7/旧 DXGI 查询失败时仅禁用显存指标。
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;

		D3D11_BUFFER_DESC constantBufferDescription = {
			sizeof(GlobalShaderConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
			D3D11_CPU_ACCESS_WRITE, 0, 0
		};
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr, globalCB.ReleaseAndGetAddressOf()))) return false;
		constantBufferDescription.ByteWidth = sizeof(LaserStyleConstants);
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr,
			laserStyleCB.ReleaseAndGetAddressOf()))) return false;
		ConfigureLaserStyle(1.0f);

		D3D11_BLEND_DESC blendDescription = {};
		blendDescription.IndependentBlendEnable = TRUE;
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		// Add 取最大值，避免同一墨迹的胶囊段在抗锯齿边缘重复叠加。
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		// Retain 从 1 开始取最小值，与 Add 的最大覆盖率保持同一个并集。
		blendDescription.RenderTarget[1].BlendEnable = TRUE;
		blendDescription.RenderTarget[1].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].BlendOp = D3D11_BLEND_OP_MIN;
		blendDescription.RenderTarget[1].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].BlendOpAlpha = D3D11_BLEND_OP_MIN;
		blendDescription.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
		if (FAILED(device->CreateBlendState(&blendDescription, strokeOperatorBlendState.ReleaseAndGetAddressOf()))) return false;

		blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		// PS 的 SV_Target1 提供 Retain，双源混合一次计算 Add + Retain * Destination。
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_SRC1_COLOR;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC1_ALPHA;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription, operatorResolveBlendState.ReleaseAndGetAddressOf()))) return false;

		blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription,
			laserCoverageBlendState.ReleaseAndGetAddressOf()))) return false;

		D3D11_DEPTH_STENCIL_DESC depthStencilDescription = {};
		if (FAILED(device->CreateDepthStencilState(&depthStencilDescription, dsState.ReleaseAndGetAddressOf()))) return false;

		D3D11_RASTERIZER_DESC rasterizerDescription = {};
		rasterizerDescription.FillMode = D3D11_FILL_SOLID;
		rasterizerDescription.CullMode = D3D11_CULL_NONE;
		rasterizerDescription.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rasterizerDescription, rasterState.ReleaseAndGetAddressOf()))) return false;
		rasterizerDescription.ScissorEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rasterizerDescription,
			laserScissorRasterState.ReleaseAndGetAddressOf())))
		{
			// scissor 只减少激光局部 pass 的像素工作，创建失败不应阻断 renderer 初始化。
			laserScissorRasterState.Reset();
		}

		D3D11_BUFFER_DESC inkBufferDescription = {};
		inkBufferDescription.ByteWidth = static_cast<UINT>(kMaxBufferCapacity * sizeof(InkPoint));
		inkBufferDescription.Usage = D3D11_USAGE_DYNAMIC;
		inkBufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		inkBufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		inkBufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		inkBufferDescription.StructureByteStride = sizeof(InkPoint);
		if (FAILED(device->CreateBuffer(&inkBufferDescription, nullptr, inkDataBuffer.ReleaseAndGetAddressOf()))) return false; // CPU 每帧写点，GPU 着色器按结构化缓冲区读取。

		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDescription = {};
		shaderResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
		shaderResourceDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		shaderResourceDescription.Buffer.NumElements = static_cast<UINT>(kMaxBufferCapacity);
		if (FAILED(device->CreateShaderResourceView(inkDataBuffer.Get(), &shaderResourceDescription, inkDataSRV.ReleaseAndGetAddressOf()))) return false;

		D3D11_BUFFER_DESC highlighterBufferDescription = inkBufferDescription;
		highlighterBufferDescription.ByteWidth = static_cast<UINT>(kMaxBufferCapacity * sizeof(HighlighterPrimitive));
		highlighterBufferDescription.StructureByteStride = sizeof(HighlighterPrimitive);
		if (FAILED(device->CreateBuffer(&highlighterBufferDescription, nullptr,
			highlighterPrimitiveBuffer.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(highlighterPrimitiveBuffer.Get(), &shaderResourceDescription,
			highlighterPrimitiveSRV.ReleaseAndGetAddressOf()))) return false; // 荧光笔几何单独上传，避免改变普通墨迹点布局。

		D3D11_SAMPLER_DESC samplerDescription = {};
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 混合图层按像素采样，不做线性模糊。
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&samplerDescription, operatorSampler.ReleaseAndGetAddressOf()))) return false;
		CreateTrustedL2SnapshotPipeline(); // 可选 pass 失败时仅禁用动态快照兜底。
		return LoadShaders();
	}

	bool InkRenderer::LoadShaders()
	{
		const ShaderBlob vertexShaderBlob = LoadShaderFromResource(IDR_VS1);
		const ShaderBlob pixelShaderBlob = LoadShaderFromResource(IDR_PS1);
		if (!vertexShaderBlob.data || !pixelShaderBlob.data) return false;
		if (FAILED(device->CreateVertexShader(vertexShaderBlob.data, vertexShaderBlob.size, nullptr, vertexShader.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreatePixelShader(pixelShaderBlob.data, pixelShaderBlob.size, nullptr, pixelShader.ReleaseAndGetAddressOf()))) return false;
		const ShaderBlob updateShaderBlob =
			LoadShaderFromResource(IDR_LASER_PARTICLE_UPDATE_CS);
		const ShaderBlob emitShaderBlob =
			LoadShaderFromResource(IDR_LASER_PARTICLE_EMIT_CS);
		const bool particlesReady = updateShaderBlob.data && emitShaderBlob.data &&
			laserParticleSystem_.Initialize(device.Get(), context.Get(),
				{ updateShaderBlob.data, updateShaderBlob.size },
				{ emitShaderBlob.data, emitShaderBlob.size });
		if (!particlesReady)
			ReportLaserParticleUnavailableOnce();
		return true;
	}
}
