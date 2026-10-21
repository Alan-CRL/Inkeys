module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"
#include "../../../resource.h"
#include "../../../IdtStart.h"
#include "Draw3.ShaderCompatibility.h"
#include "Draw3.Win7GuardedShader.h"
#include "../../../../inkStrokeModelerTest/draw3/shape_core.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <utility>
#include <vector>
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include <wrl/client.h>

module Inkeys.Drawing.Draw3.renderer;

namespace Inkeys::Drawing::Draw3
{
	using renderer_detail::GlobalShaderConstants;

	HRESULT InkRenderer::UploadStructuredBuffer(ID3D11Buffer* buffer, const void* data, UINT bytes,
		UINT offset, D3D11_MAP mapType)
	{
		if (!buffer || !data || !context || !device || !bytes) return E_INVALIDARG;
		if (useDefaultStructuredUpload_)
		{
			// DEFAULT 每批从零更新实际字节；保留大容量，但避开大动态资源的映射成本。
			if (offset != 0) return E_INVALIDARG;
			const D3D11_BOX range = { 0, 0, 0, bytes, 1, 1 };
			context->UpdateSubresource(buffer, 0, &range, data, 0, 0);
			// UpdateSubresource 是 void；设备失效单独查询，不能伪造该 API 的 HRESULT。
			return device->GetDeviceRemovedReason();
		}
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT result = context->Map(buffer, 0, mapType, 0, &mapped);
		if (FAILED(result)) return result;
		std::memcpy(static_cast<unsigned char*>(mapped.pData) + offset, data, bytes);
		context->Unmap(buffer, 0);
		// Laser/Cursor 的 DISCARD 同样开启新一代墨迹数据，后续 NO_OVERWRITE 必须越过已用区。
		if (buffer == inkDataBuffer.Get()) m_bufferHead = (offset + bytes) / sizeof(InkPoint);
		return result;
	}

	namespace
	{
		constexpr bool UseZhaoxinDefaultUpload(bool enabled, D3D_DRIVER_TYPE driver,
			bool identityAvailable, UINT vendor) noexcept
		{
			return enabled && driver == D3D_DRIVER_TYPE_HARDWARE && identityAvailable && vendor == 0x1D17;
		}

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

		bool ExactDiagnosticEnvironmentEnabled(const wchar_t* name) noexcept
		{
			wchar_t value[2] = {};
			return GetEnvironmentVariableW(name, value, ARRAYSIZE(value)) == 1 && value[0] == L'1';
		}

		// 仅诊断初始化时计算实际所选字节码的 SHA；不把生成时的预期 hash 冒充当前资源身份。
		void ReportSelectedPixelShader(const ShaderBlob& blob, bool guarded, HRESULT result) noexcept
		{
			char hash[65] = "unavailable";
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE state = nullptr;
			DWORD objectSize = 0, returned = 0;
			std::array<UCHAR, 32> digest = {};
			std::array<UCHAR, 4096> object = {};
			if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0)
			{
				if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
					sizeof(objectSize), &returned, 0) >= 0 && objectSize > 0 && objectSize <= object.size())
				{
					if (BCryptCreateHash(algorithm, &state, object.data(), objectSize, nullptr, 0, 0) >= 0)
					{
						if (BCryptHashData(state, static_cast<PUCHAR>(const_cast<void*>(blob.data)),
							static_cast<ULONG>(blob.size), 0) >= 0 &&
							BCryptFinishHash(state, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0)
							for (size_t index = 0; index < digest.size(); ++index)
								std::snprintf(hash + index * 2, 3, "%02X", digest[index]);
						BCryptDestroyHash(state);
					}
				}
				BCryptCloseAlgorithmProvider(algorithm, 0);
			}
			char message[256] = {};
			std::snprintf(message, sizeof(message),
				"[Draw3ShaderSelection] actual=%s sha256=%s bytes=%zu create_hr=0x%08X\n",
				guarded ? "guarded" : "original", hash, blob.size, static_cast<unsigned int>(result));
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Output, message, true);
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
		if (!dstRTV || !compositeVertexBuffer || !compositeVertexSRV || !stableLayer.addSRV || !stableLayer.retainSRV ||
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
		if (FAILED(context->Map(compositeVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		std::memcpy(mapped.pData, rectPoints, sizeof(rectPoints)); // 合成仅更新独立小资源，不能打断墨迹环形追加。
		context->Unmap(compositeVertexBuffer.Get(), 0);

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
		ID3D11ShaderResourceView* shaderResources[] = { compositeVertexSRV.Get() };
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
		if (context) context->OMSetRenderTargets(0, nullptr, nullptr);
		laserBakeScratchColor_ = {}; // Laser 生命周期结束后归还按需分配的全尺寸 scratch。
	}

	bool InkRenderer::BeginLaserBake()
	{
		if (!device || !context || !laserCompositedColor.texture ||
			!laserCompositedColor.rtv) return false;
		D3D11_TEXTURE2D_DESC committedDescription = {};
		laserCompositedColor.texture->GetDesc(&committedDescription);
		if (committedDescription.Width == 0 || committedDescription.Height == 0) return false;
		if (laserBakeScratchColor_.texture)
		{
			D3D11_TEXTURE2D_DESC scratchDescription = {};
			laserBakeScratchColor_.texture->GetDesc(&scratchDescription);
			if (scratchDescription.Width != committedDescription.Width ||
				scratchDescription.Height != committedDescription.Height ||
				scratchDescription.Format != committedDescription.Format)
				laserBakeScratchColor_ = {};
		}
		if (!laserBakeScratchColor_.texture &&
			!CreateLaserCoverageResources(committedDescription.Width,
				committedDescription.Height, laserBakeScratchColor_))
		{
			laserBakeScratchColor_ = {};
			return false;
		}
		if (!laserBakeScratchColor_.rtv || !laserBakeScratchColor_.srv) return false;
		// 先解绑读写槽，再从最后已提交颜色复制；失败重试永远从同一状态起步。
		UnbindLaserCoverageShaderResources();
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->CopyResource(laserBakeScratchColor_.texture.Get(),
			laserCompositedColor.texture.Get());
		return true;
	}

	ID3D11RenderTargetView* InkRenderer::LaserBakeTarget() const noexcept
	{
		return laserBakeScratchColor_.rtv.Get();
	}

	void InkRenderer::CommitLaserBake() noexcept
	{
		UnbindLaserCoverageShaderResources();
		if (context) context->OMSetRenderTargets(0, nullptr, nullptr);
		std::swap(laserCompositedColor, laserBakeScratchColor_);
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
		if (!device || !context || width == 0 || height == 0) return false;
		if (swapChain)
		{
			if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
				reinterpret_cast<void**>(backBufferTexture.ReleaseAndGetAddressOf())))) return false;
		}
		else
		{
			// 无窗口测试仍创建正式 renderer 的全部离屏资源，不需要 HWND 或 Present。
			D3D11_TEXTURE2D_DESC offscreen = {};
			offscreen.Width = width; offscreen.Height = height;
			offscreen.MipLevels = 1; offscreen.ArraySize = 1;
			offscreen.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			offscreen.SampleDesc.Count = 1;
			offscreen.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&offscreen, nullptr,
				backBufferTexture.ReleaseAndGetAddressOf()))) return false;
		}
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
		shapeMaterialLayer_ = {};
		shapeCompositeLayer_ = {};
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
		laserBakeScratchColor_ = {};
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
		shapeVertexShader_.Reset();
		shapePixelShader_.Reset();
		shapeCB_.Reset();
		shapeOrderedBlend_.Reset();
		shapeRenderCache_.clear();
		vertexShader.Reset();
		pixelShader.Reset();
		globalCB.Reset();
		laserStyleCB.Reset();
		inkDataBuffer.Reset();
		inkDataSRV.Reset();
		compositeVertexBuffer.Reset();
		compositeVertexSRV.Reset();
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
		mapNoOverwriteOnDynamicBufferSRV = false;
		useDefaultStructuredUpload_ = false;
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

	bool InkRenderer::Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain, UINT width, UINT height, D3D_DRIVER_TYPE driver,
		bool enableZhaoxinCompatibility)
	{
		device = inDevice; // 渲染器只借用外部统一创建的 D3D 设备。
		context = inContext;
		useDefaultStructuredUpload_ = false;
		if (!device || !context)
		{
			mapNoOverwriteOnDynamicBufferSRV = false;
			return false;
		}
		D3D11_FEATURE_DATA_D3D11_OPTIONS deviceOptions = {};
		// Win7 Platform Update/旧驱动可拒绝动态 SRV 的 NO_OVERWRITE；按本次设备查询，失败即回退。
		mapNoOverwriteOnDynamicBufferSRV = SUCCEEDED(device->CheckFeatureSupport(
			D3D11_FEATURE_D3D11_OPTIONS, &deviceOptions, sizeof(deviceOptions))) &&
			deviceOptions.MapNoOverwriteOnDynamicBufferSRV != FALSE;
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		if (SUCCEEDED(device.As(&dxgiDevice)) && dxgiDevice &&
			SUCCEEDED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf())) && adapter)
			adapter.As(&videoMemoryAdapter_); // Win7/旧 DXGI 查询失败时仅禁用显存指标。
		DXGI_ADAPTER_DESC adapterDescription = {};
		const HRESULT adapterResult = adapter ? adapter->GetDesc(&adapterDescription) : E_NOINTERFACE;
		const bool adapterIdentityAvailable = SUCCEEDED(adapterResult);
		// 按实际绘图 GPU 选择，不把兆芯 CPU + 其他显卡误判为兼容设备。
		useDefaultStructuredUpload_ = UseZhaoxinDefaultUpload(enableZhaoxinCompatibility,
			driver, adapterIdentityAvailable, adapterDescription.VendorId);
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;
		return CreateFixedResources(driver);
	}

	bool InkRenderer::CreateFixedResources(D3D_DRIVER_TYPE driver)
	{
		D3D11_BUFFER_DESC constantBufferDescription = {
			sizeof(GlobalShaderConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
			D3D11_CPU_ACCESS_WRITE, 0, 0
		};
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr, globalCB.ReleaseAndGetAddressOf()))) return false;
		constantBufferDescription.ByteWidth = sizeof(LaserStyleConstants);
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr,
			laserStyleCB.ReleaseAndGetAddressOf()))) return false;
		ConfigureLaserStyle(1.0f,
			DirectX::XMFLOAT4(1.0f, 16.0f / 255.0f, 0.0f, 1.0f));

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
		inkBufferDescription.Usage = useDefaultStructuredUpload_ ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
		inkBufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		inkBufferDescription.CPUAccessFlags = useDefaultStructuredUpload_ ? 0 : D3D11_CPU_ACCESS_WRITE;
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

		// 合成矩形只需两个点，专用小动态资源避免 DISCARD 整个 3.2 MB 墨迹缓冲区。
		D3D11_BUFFER_DESC compositeDescription = inkBufferDescription;
		compositeDescription.ByteWidth = 256;
		compositeDescription.Usage = D3D11_USAGE_DYNAMIC;
		compositeDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&compositeDescription, nullptr,
			compositeVertexBuffer.ReleaseAndGetAddressOf()))) return false;
		shaderResourceDescription.Buffer.NumElements = compositeDescription.ByteWidth / sizeof(InkPoint);
		if (FAILED(device->CreateShaderResourceView(compositeVertexBuffer.Get(), &shaderResourceDescription,
			compositeVertexSRV.ReleaseAndGetAddressOf()))) return false;

		D3D11_SAMPLER_DESC samplerDescription = {};
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 混合图层按像素采样，不做线性模糊。
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&samplerDescription, operatorSampler.ReleaseAndGetAddressOf()))) return false;
		CreateTrustedL2SnapshotPipeline(); // 可选 pass 失败时仅禁用动态快照兜底。
		return LoadShaders(driver) && EnsureShapePipeline();
	}

	bool InkRenderer::LoadShaders(D3D_DRIVER_TYPE driver)
	{
		const ShaderBlob vertexShaderBlob =
			LoadShaderFromResource(IDR_DRAW3_INK_VERTEX_SHADER);
		const bool diagnostics = ExactDiagnosticEnvironmentEnabled(L"INKEYS_DRAW3_WIN7_DIAGNOSTICS");
		wchar_t guardedOverride[2] = {};
		const DWORD overrideLength = GetEnvironmentVariableW(L"INKEYS_DRAW3_WIN7_GUARDED_SHADER",
			guardedOverride, ARRAYSIZE(guardedOverride));
		// 复用 RtlGetVersion 的真实版本查询，不依赖正常启动才赋值的全局版本状态。
		const auto version = GetWindowsVersion();
		const bool guarded = shader_compatibility::UseGuardedPixelShader(
			version.majorVersion, version.minorVersion, driver, diagnostics,
			overrideLength == 1 ? std::wstring_view(guardedOverride, 1) : std::wstring_view{});
		const ShaderBlob pixelShaderBlob = guarded
			? ShaderBlob{ Draw3Win7GuardedShader::Bytecode, sizeof(Draw3Win7GuardedShader::Bytecode) }
			: LoadShaderFromResource(IDR_DRAW3_INK_PIXEL_SHADER);
		if (!vertexShaderBlob.data || !pixelShaderBlob.data) return false;
		if (FAILED(device->CreateVertexShader(vertexShaderBlob.data, vertexShaderBlob.size, nullptr, vertexShader.ReleaseAndGetAddressOf()))) return false;
		const HRESULT pixelResult = device->CreatePixelShader(pixelShaderBlob.data, pixelShaderBlob.size, nullptr, pixelShader.ReleaseAndGetAddressOf());
		if (diagnostics) ReportSelectedPixelShader(pixelShaderBlob, guarded, pixelResult);
		if (FAILED(pixelResult)) return false; // 不静默切回原版，否则会把候选创建失败误记为实测通过。
		const ShaderBlob updateShaderBlob =
			LoadShaderFromResource(IDR_DRAW3_LASER_UPDATE_CS);
		const ShaderBlob emitShaderBlob =
			LoadShaderFromResource(IDR_DRAW3_LASER_EMIT_CS);
		const bool particlesReady = updateShaderBlob.data && emitShaderBlob.data &&
			laserParticleSystem_.Initialize(device.Get(), context.Get(),
				{ updateShaderBlob.data, updateShaderBlob.size },
				{ emitShaderBlob.data, emitShaderBlob.size });
		if (!particlesReady)
			ReportLaserParticleUnavailableOnce();
		return true;
	}

	int RunRendererUploadValidation() noexcept
	{
		try
		{
			int failures = 0;
			const auto check = [&](bool condition, const char* name) {
				if (!condition) { ++failures; std::fprintf(stderr, "[Draw3Upload] FAIL %s\n", name); }
				return condition;
			};
			check(UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_HARDWARE, true, 0x1D17) &&
				!UseZhaoxinDefaultUpload(false, D3D_DRIVER_TYPE_HARDWARE, true, 0x1D17) &&
				!UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_WARP, true, 0x1D17) &&
				!UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_HARDWARE, false, 0x1D17) &&
				!UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_HARDWARE, true, 0x5143), "compatibility policy gates");
			Microsoft::WRL::ComPtr<ID3D11Device> testDevice;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> testContext;
			constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
			D3D_FEATURE_LEVEL actualLevel = {};
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
				testDevice.GetAddressOf(), &actualLevel, testContext.GetAddressOf())), "offscreen WARP device")) return 1;
			D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
			const bool canAppend = SUCCEEDED(testDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,
				&options, sizeof(options))) && options.MapNoOverwriteOnDynamicBufferSRV;
			std::vector<unsigned char> referencePixels;
			// 两种动态能力分支与 DEFAULT 都调用产品 Draw 方法，不通过测试替身模拟上传。
			for (int mode = 0; mode < 3; ++mode)
			{
				if (mode == 1 && !canAppend) continue;
				InkRenderer renderer;
				renderer.device = testDevice;
				renderer.context = testContext;
				renderer.useDefaultStructuredUpload_ = mode == 2;
				renderer.mapNoOverwriteOnDynamicBufferSRV = mode == 1;
				if (!check(renderer.CreateFixedResources(D3D_DRIVER_TYPE_WARP), "production fixed resources")) return 1;
				D3D11_BUFFER_DESC description = {};
				renderer.inkDataBuffer->GetDesc(&description);
				check(description.ByteWidth == 3200000 && description.Usage ==
					(mode == 2 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC) &&
					description.CPUAccessFlags == (mode == 2 ? 0u : D3D11_CPU_ACCESS_WRITE), "ink resource policy");
				renderer.highlighterPrimitiveBuffer->GetDesc(&description);
				check(description.ByteWidth == 4800000 && description.Usage ==
					(mode == 2 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC), "highlighter resource policy");
				renderer.compositeVertexBuffer->GetDesc(&description);
				check(description.ByteWidth == 256 && description.Usage == D3D11_USAGE_DYNAMIC &&
					description.StructureByteStride == sizeof(InkPoint), "independent composite resource");
				D3D11_TEXTURE2D_DESC targetDescription = {};
				targetDescription.Width = targetDescription.Height = 64;
				targetDescription.MipLevels = targetDescription.ArraySize = targetDescription.SampleDesc.Count = 1;
				targetDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				targetDescription.Usage = D3D11_USAGE_DEFAULT;
				targetDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (!check(SUCCEEDED(testDevice->CreateTexture2D(&targetDescription, nullptr,
					renderer.backBufferTexture.GetAddressOf())) && SUCCEEDED(testDevice->CreateRenderTargetView(
					renderer.backBufferTexture.Get(), nullptr, renderer.backBufferRTV.GetAddressOf())) &&
					renderer.CreateOperatorLayerResources(64, 64, renderer.layerL1) &&
					renderer.CreateOperatorLayerResources(64, 64, renderer.layerL0) &&
					renderer.CreateLaserCoverageResources(64, 64, renderer.laserStrokeCoverage), "offscreen render targets")) return 1;
				renderer.SetScreenSize(64, 64);
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearOperatorLayer(renderer.layerL0);
				const auto readBytes = [&](ID3D11Buffer* source, UINT offset, const void* expected,
					UINT bytes, const char* name) {
					D3D11_BUFFER_DESC readDescription = {};
					source->GetDesc(&readDescription);
					readDescription.Usage = D3D11_USAGE_STAGING;
					readDescription.BindFlags = readDescription.MiscFlags = readDescription.StructureByteStride = 0;
					readDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					Microsoft::WRL::ComPtr<ID3D11Buffer> readback;
					if (!check(SUCCEEDED(testDevice->CreateBuffer(&readDescription, nullptr,
						readback.GetAddressOf())), "create upload readback")) return false;
					// 常量缓冲区也按完整资源复制，避免测试使用不支持的局部 CB copy。
					testContext->CopyResource(readback.Get(), source);
					D3D11_MAPPED_SUBRESOURCE mapped = {};
					if (!check(SUCCEEDED(testContext->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
						"map upload readback")) return false;
					const bool same = std::memcmp(static_cast<unsigned char*>(mapped.pData) + offset, expected, bytes) == 0;
					testContext->Unmap(readback.Get(), 0);
					return check(same, name);
				};
				const auto checkOffset = [&](UINT expected) {
					readBytes(renderer.globalCB.Get(), 12, &expected, sizeof(expected), "shader batch offset");
				};
				const auto composite = [&] {
					const size_t head = renderer.m_bufferHead;
					check(renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(), renderer.layerL1,
						renderer.layerL0, { 0, 0, 64, 64 }), "production composite submission");
					check(renderer.m_bufferHead == head, "composite preserves ink head");
					const InkPoint rectangle[] = { { 0, 0, 0, 0 }, { 64, 64, 0, 0 } };
					readBytes(renderer.compositeVertexBuffer.Get(), 0, rectangle, sizeof(rectangle), "composite rectangle bytes");
				};
				D3D11_BUFFER_DESC rejectedDescription = {};
				renderer.compositeVertexBuffer->GetDesc(&rejectedDescription);
				rejectedDescription.Usage = D3D11_USAGE_DEFAULT;
				rejectedDescription.CPUAccessFlags = 0;
				Microsoft::WRL::ComPtr<ID3D11Buffer> rejectedComposite;
				if (!check(SUCCEEDED(testDevice->CreateBuffer(&rejectedDescription, nullptr,
					rejectedComposite.GetAddressOf())), "non-mappable composite fault resource")) return 1;
				const auto writableComposite = renderer.compositeVertexBuffer;
				renderer.compositeVertexBuffer = rejectedComposite;
				check(!renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(), renderer.layerL1,
					renderer.layerL0, { 0, 0, 64, 64 }) && renderer.m_bufferHead == 0, "composite Map failure preserves ink state");
				renderer.compositeVertexBuffer = writableComposite;
				const std::array<InkPoint, 2> first = { InkPoint{ 8, 12, 3, 0 }, InkPoint{ 52, 12, 3, 1 } };
				const std::array<InkPoint, 2> second = { InkPoint{ 8, 28, 3, 2 }, InkPoint{ 52, 28, 3, 3 } };
				const DirectX::XMFLOAT4 red(1, 0, 0, 1), green(0, 1, 0, 1);
				if (mode == 2)
				{
					const InkPoint seed[] = { first[0], first[1], second[0], second[1] };
					const D3D11_BOX seedRange = { 0, 0, 0, sizeof(seed), 1, 1 };
					testContext->UpdateSubresource(renderer.inkDataBuffer.Get(), 0, &seedRange, seed, 0, 0);
				}
				renderer.SetOperatorTarget(renderer.layerL1);
				check(renderer.DrawStroke(first, red) == 0, "first production stroke");
				if (mode == 2) readBytes(renderer.inkDataBuffer.Get(), sizeof(first), second.data(), sizeof(second), "DEFAULT only updates active bytes");
				checkOffset(0);
				composite();
				readBytes(renderer.inkDataBuffer.Get(), 0, first.data(), sizeof(first), "composite leaves ink bytes intact");
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(second, green) == 0, "second production stroke");
				const UINT secondOffset = mode == 1 ? 2 : 0;
				checkOffset(secondOffset);
				readBytes(renderer.inkDataBuffer.Get(), secondOffset * sizeof(InkPoint), second.data(), sizeof(second), "second stroke byte range");
				if (mode == 1) readBytes(renderer.inkDataBuffer.Get(), 0, first.data(), sizeof(first), "NO_OVERWRITE retains first batch");
				composite();
				const std::array<ShapePrimitive, 1> shape = { ShapePrimitive{ { 14, 42, 2, 0 }, { 48, 42, 0, 0 } } };
				const UINT shapeOffset = mode == 1 ? static_cast<UINT>(renderer.m_bufferHead) : 0;
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawShapePrimitives(shape, ShapePrimitiveKind::SolidLine, red) == 0, "production analytic shape");
				checkOffset(shapeOffset);
				readBytes(renderer.inkDataBuffer.Get(), shapeOffset * sizeof(InkPoint), shape.data(), sizeof(shape), "shape bytes");
				const std::array<HighlighterPrimitive, 1> highlighter = { HighlighterPrimitive{ { 30, 32 }, { 34, 32 }, { 1.25f, 12 } } };
				check(renderer.DrawHighlighterPrimitives(highlighter, green) == 0, "production highlighter");
				checkOffset(0);
				readBytes(renderer.highlighterPrimitiveBuffer.Get(), 0, highlighter.data(), sizeof(highlighter), "highlighter bytes");
				composite();
				DrawingCursorVisual cursor{};
				cursor.visible = true; cursor.x = 54; cursor.y = 54;
				cursor.appearance.width = cursor.appearance.height = 8;
				cursor.appearance.red = 1;
				renderer.DrawTransientDrawingCursor(cursor);
				const InkPoint cursorData[] = { { 54, 54, 4, 4 }, { 0.5f, 0.5f, 0, 0 } };
				readBytes(renderer.inkDataBuffer.Get(), 0, cursorData, sizeof(cursorData), "cursor uses selected upload path");
				checkOffset(0);
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(first, red) == 0, "cursor to stroke interleave");
				checkOffset(mode == 1 ? 2 : 0);
				renderer.ClearLaserCoverage(renderer.laserStrokeCoverage);
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				check(renderer.DrawLaserCoverage(first) == 0, "production laser coverage");
				readBytes(renderer.inkDataBuffer.Get(), 0, first.data(), sizeof(first), "laser coverage upload bytes");
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawShapePrimitives(shape, ShapePrimitiveKind::SolidLine, red) == 0, "laser to shape interleave");
				checkOffset(mode == 1 ? 2 : 0);
				check(renderer.ResolveLaserStrokeCoverage(renderer.backBufferRTV.Get(), { 0, 0, 64, 64 }), "laser resolve");
				const std::array<LaserDot, 1> dots = { LaserDot{ 12, 52, 2.5f, 1 } };
				renderer.DrawLaserDots(dots);
				readBytes(renderer.inkDataBuffer.Get(), 0, dots.data(), sizeof(dots), "laser dot upload bytes");
				checkOffset(0);
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(second, green) == 0, "laser dots to stroke interleave");
				checkOffset(mode == 1 ? 1 : 0);

				// 参数图形和普通笔共用 InkData，三种上传策略都必须保留批次边界和实际像素。
				::draw3::shapes::ShapeObject parameterShape;
				parameterShape.id[0] = 1;
				parameterShape.kind = ::draw3::shapes::Kind::Circle;
				parameterShape.geometry.start = { 44, 18 };
				parameterShape.geometry.radius = 6;
				parameterShape.style.strokeRgb = 0xFF0000;
				parameterShape.style.fill = true;
				parameterShape.style.fillRgb = 0x2040FF;
				check(renderer.DrawShapeObject(parameterShape), "production parametric shape selected upload path");
				const UINT afterShapeOffset = mode == 1 ? static_cast<UINT>(renderer.m_bufferHead) : 0;
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(second, green) == 0, "parametric shape to stroke interleave");
				checkOffset(afterShapeOffset);
				composite();

				// 同一命令序列的实际像素必须一致，避免只验证 CPU 内容而漏掉 Draw 顺序/偏移。
				testContext->OMSetRenderTargets(0, nullptr, nullptr);
				targetDescription.Usage = D3D11_USAGE_STAGING;
				targetDescription.BindFlags = 0;
				targetDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Microsoft::WRL::ComPtr<ID3D11Texture2D> pixelReadback;
				if (!check(SUCCEEDED(testDevice->CreateTexture2D(&targetDescription, nullptr,
					pixelReadback.GetAddressOf())), "pixel staging texture")) return 1;
				testContext->CopyResource(pixelReadback.Get(), renderer.backBufferTexture.Get());
				D3D11_MAPPED_SUBRESOURCE pixels = {};
				if (!check(SUCCEEDED(testContext->Map(pixelReadback.Get(), 0, D3D11_MAP_READ, 0, &pixels)), "pixel readback")) return 1;
				std::vector<unsigned char> currentPixels(64 * 64 * 4);
				for (UINT row = 0; row < 64; ++row) std::memcpy(currentPixels.data() + row * 64 * 4,
					static_cast<unsigned char*>(pixels.pData) + row * pixels.RowPitch, 64 * 4);
				testContext->Unmap(pixelReadback.Get(), 0);
				if (mode == 0) referencePixels = currentPixels;
				else check(currentPixels == referencePixels, "dynamic/DEFAULT tool and composite pixels identical");
				check(std::any_of(currentPixels.begin(), currentPixels.end(), [](unsigned char value) { return value != 0; }),
					"rendered pixels are nonempty");

				// 两次更新和 Draw 之间不回读/等待，实际像素验证旧批次没有被后一次更新覆盖。
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearOperatorLayer(renderer.layerL0);
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				renderer.SetOperatorTarget(renderer.layerL1);
				check(renderer.DrawStroke(first, red) == 0 && renderer.DrawStroke(second, green) == 0,
					"consecutive update and Draw submissions");
				composite();
				testContext->OMSetRenderTargets(0, nullptr, nullptr);
				testContext->CopyResource(pixelReadback.Get(), renderer.backBufferTexture.Get());
				if (!check(SUCCEEDED(testContext->Map(pixelReadback.Get(), 0, D3D11_MAP_READ, 0, &pixels)),
					"consecutive Draw pixel readback")) return 1;
				const auto* redPixel = static_cast<const unsigned char*>(pixels.pData) + 12 * pixels.RowPitch + 12 * 4;
				const auto* greenPixel = static_cast<const unsigned char*>(pixels.pData) + 28 * pixels.RowPitch + 12 * 4;
				check(redPixel[2] > 240 && redPixel[1] < 8 && redPixel[0] < 8 && redPixel[3] > 240 &&
					greenPixel[1] > 240 && greenPixel[2] < 8 && greenPixel[0] < 8 && greenPixel[3] > 240,
					"both consecutive batches rendered with their own data");
				testContext->Unmap(pixelReadback.Get(), 0);

				// 越界拆批仍共享笔端点；最终批次与回卷偏移由真实上传读回验证。
				std::vector<InkPoint> longStroke(InkRenderer::kMaxBufferCapacity + 2, { -100, -100, 0, 0 });
				longStroke[InkRenderer::kMaxBufferCapacity - 1] = { 20, 20, 2, 1 };
				longStroke[InkRenderer::kMaxBufferCapacity] = { 28, 20, 2, 2 };
				longStroke.back() = { 36, 20, 2, 3 };
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(longStroke, red) == 0, "multi-batch stroke and wrap");
				check(renderer.m_bufferHead == (mode == 2 ? 0u : 3u), "final stroke batch head");
				checkOffset(0);
				readBytes(renderer.inkDataBuffer.Get(), 0, longStroke.data() + InkRenderer::kMaxBufferCapacity - 1,
					3 * sizeof(InkPoint), "multi-batch shared endpoint");
				std::vector<ShapePrimitive> shapes(InkRenderer::kMaxBufferCapacity / 2 + 1,
					ShapePrimitive{ { -100, -100, 0, 0 }, { -90, -90, 0, 0 } });
				shapes.back() = shape.front();
				check(renderer.DrawShapePrimitives(shapes, ShapePrimitiveKind::SolidLine, red) == 0, "multi-batch shapes");
				checkOffset(0);
				readBytes(renderer.inkDataBuffer.Get(), 0, &shapes.back(), sizeof(ShapePrimitive), "final shape batch");
				std::vector<HighlighterPrimitive> highlights(InkRenderer::kMaxBufferCapacity + 1,
					HighlighterPrimitive{ { -100, -100 }, { -90, -90 }, { 1.25f, 25 } });
				highlights.back() = highlighter.front();
				check(renderer.DrawHighlighterPrimitives(highlights, green) == 0, "multi-batch highlighter");
				readBytes(renderer.highlighterPrimitiveBuffer.Get(), 0, &highlights.back(), sizeof(HighlighterPrimitive), "final highlighter batch");
				testContext->ClearState();
				renderer.ReleaseResources();
				std::fprintf(stderr, "[Draw3Upload] mode=%s completed\n", mode == 2 ? "DEFAULT" : mode == 1 ? "dynamic_append" : "dynamic_discard");
			}
			if (!canAppend) std::fputs("[Draw3Upload] NO_OVERWRITE unavailable on WARP; append case skipped\n", stderr);
			std::fprintf(stderr, "[Draw3Upload] %s failures=%d no_hwnd=1 no_swapchain=1\n", failures ? "FAIL" : "PASS", failures);
			return failures ? 1 : 0;
		}
		catch (...) { std::fputs("[Draw3Upload] FAIL unexpected exception\n", stderr); return 1; }
	}
}
