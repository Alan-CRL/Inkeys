module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../resource.h"

#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

module draw3.renderer;

namespace draw3
{
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

		// 该结构必须满足 D3D11 常量缓冲区的 16 字节对齐要求。
		struct GlobalShaderConstants
		{
			float width;
			float height;
			float shapeType;
			uint32_t bufferOffset;
			DirectX::XMFLOAT4 color;
		};

		static_assert(sizeof(GlobalShaderConstants) == 32);
		static_assert(sizeof(GlobalShaderConstants) % 16 == 0);
	}

	int InkRenderer::DrawStrokeOrDot(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color, float shapeType, bool eraser)
	{
		if (points.empty()) return 0;
		if (points.size() >= 2) return DrawStroke(points, color, shapeType, eraser);

		// 单点使用极短胶囊段复用现有笔刷着色器。
		std::vector<InkPoint> dotPoints;
		dotPoints.reserve(2);
		dotPoints.push_back(points.front());
		InkPoint dotEnd = points.front();
		dotEnd.x += std::max(0.25f, dotEnd.r * 0.05f);
		dotPoints.push_back(dotEnd);
		return DrawStroke(dotPoints, color, shapeType, eraser);
	}

	int InkRenderer::DrawStroke(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color, float shapeType, bool eraser)
	{
		const size_t totalPoints = points.size();
		if (totalPoints < 2) return 0;

		size_t startIndex = 0;
		while (startIndex < totalPoints - 1)
		{
			const size_t remaining = totalPoints - startIndex;
			const size_t batchCount = std::min(remaining, kMaxBufferCapacity);
			D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
			if (m_bufferHead + batchCount > kMaxBufferCapacity)
			{
				mapType = D3D11_MAP_WRITE_DISCARD; // 环形缓冲区写满后丢弃旧内容从头写。
				m_bufferHead = 0;
			}

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(inkDataBuffer.Get(), 0, mapType, 0, &mapped)))
			{
				auto* destination = static_cast<InkPoint*>(mapped.pData);
				std::memcpy(destination + m_bufferHead, points.data() + startIndex, batchCount * sizeof(InkPoint)); // 把本批点写入结构化缓冲区。
				context->Unmap(inkDataBuffer.Get(), 0);
			}

			if (SUCCEEDED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
				constants->width = viewportWidth;
				constants->height = viewportHeight;
				constants->color = color;
				constants->shapeType = shapeType;
				constants->bufferOffset = static_cast<uint32_t>(m_bufferHead); // 着色器用偏移定位当前批次的起点。
				context->Unmap(globalCB.Get(), 0);
			}

			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, shaderResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
			context->VSSetConstantBuffers(0, 1, constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			if (eraser)
			{
				float blendFactor[4] = { 0, 0, 0, 0 };
				context->OMSetBlendState(eraserBlendState.Get(), blendFactor, 0xFFFFFFFF); // 橡皮使用独立混合状态处理透明度。
			}
			else
			{
				context->OMSetBlendState(penBlendState.Get(), nullptr, 0xFFFFFFFF); // 画笔用 MAX 混合避免重叠边缘变厚。
			}
			context->RSSetState(rasterState.Get());
			context->Draw((static_cast<UINT>(batchCount) - 1) * 6, 0); // 每两个相邻点生成一个胶囊段，段数乘 6 个顶点。

			ID3D11ShaderResourceView* nullResources[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResources);
			m_bufferHead += batchCount;
			// 相邻批次共享一个端点，避免分段处出现断裂。
			startIndex += batchCount - 1;
		}
		return 0;
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

	void InkRenderer::BlendResourceGlobal(ID3D11RenderTargetView* dstRTV, ID3D11ShaderResourceView* srcSRV)
	{
		AlphaBlendResource(dstRTV, srcSRV, RECT{ 0, 0, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight) });
	}

	void InkRenderer::AlphaBlendResource(ID3D11RenderTargetView* dstRTV, ID3D11ShaderResourceView* srcSRV, RECT rect)
	{
		if (!dstRTV || !srcSRV) return;
		rect.left = std::max(0L, rect.left);
		rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(viewportWidth), rect.right);
		rect.bottom = std::min(static_cast<LONG>(viewportHeight), rect.bottom);
		if (rect.left >= rect.right || rect.top >= rect.bottom) return;

		InkPoint rectPoints[2] = {
			{ static_cast<float>(rect.left), static_cast<float>(rect.top), 0.0f, 0.0f },
			{ static_cast<float>(rect.right), static_cast<float>(rect.bottom), 0.0f, 0.0f }
		};
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(inkDataBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		std::memcpy(mapped.pData, rectPoints, sizeof(rectPoints)); // 复用墨迹缓冲区传入要混合的矩形范围。
		context->Unmap(inkDataBuffer.Get(), 0);
		m_bufferHead = 2;

		if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		constants->shapeType = 1.0f; // 这里走矩形混合路径，不使用实际画笔形状。
		constants->bufferOffset = 0;
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
		context->PSSetShaderResources(1, 1, &srcSRV);
		ID3D11SamplerState* samplers[] = { alphaBlendSampler.Get() };
		context->PSSetSamplers(0, 1, samplers);
		context->OMSetBlendState(alphaBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->Draw(6, 0); // 一个矩形由两个三角形组成。

		ID3D11ShaderResourceView* nullResource[] = { nullptr };
		ID3D11SamplerState* nullSampler[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullResource);
		context->PSSetShaderResources(1, 1, nullResource); // 解除 SRV 绑定，避免后续作为 RTV 时冲突。
		context->PSSetSamplers(0, 1, nullSampler);
	}

	void InkRenderer::SetScreenSize(float width, float height)
	{
		viewportWidth = width;
		viewportHeight = height;
		D3D11_VIEWPORT viewport = { 0, 0, width, height, 0.0f, 1.0f };
		context->RSSetViewports(1, &viewport);
	}

	void InkRenderer::SetOMTarget(ID3D11RenderTargetView* renderTargetView)
	{
		ID3D11RenderTargetView* targets[] = { renderTargetView };
		context->OMSetRenderTargets(1, targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::ClearRTV(ID3D11RenderTargetView* renderTargetView, DirectX::XMFLOAT4 color)
	{
		const float clearColor[4] = { color.x, color.y, color.z, color.w };
		context->ClearRenderTargetView(renderTargetView, clearColor);
	}

	void InkRenderer::SetWindowBackgroundColor(DirectX::XMFLOAT4 color)
	{
		windowBackgroundColor = color;
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
		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr, layerL1Texture.ReleaseAndGetAddressOf()))) return false; // L1 保存当前笔画已确认前缀。
		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr, layerL0Texture.ReleaseAndGetAddressOf()))) return false; // L0 保存每帧变化的笔锋和预测。

		D3D11_RENDER_TARGET_VIEW_DESC renderTargetDescription = {};
		renderTargetDescription.Format = textureDescription.Format;
		renderTargetDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateRenderTargetView(layerL2Texture.Get(), &renderTargetDescription, layerL2RTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layerL1Texture.Get(), &renderTargetDescription, layerL1RTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layerL1Texture.Get(), nullptr, layerL1SRV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layerL0Texture.Get(), &renderTargetDescription, layerL0RTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layerL0Texture.Get(), nullptr, layerL0SRV.ReleaseAndGetAddressOf()))) return false;

		SetScreenSize(static_cast<float>(width), static_cast<float>(height));
		return true;
	}

	void InkRenderer::ReleaseSizeDependentResources()
	{
		if (context)
		{
			ID3D11ShaderResourceView* nullResources[] = { nullptr, nullptr };
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->VSSetShaderResources(0, 2, nullResources);
			context->PSSetShaderResources(0, 2, nullResources); // 释放前先解绑，避免 D3D 仍持有引用。
			context->Flush();
		}
		backBufferRTV.Reset();
		backBufferTexture.Reset();
		layerL2RTV.Reset();
		layerL2Texture.Reset();
		layerL1RTV.Reset();
		layerL1SRV.Reset();
		layerL1Texture.Reset();
		layerL0RTV.Reset();
		layerL0SRV.Reset();
		layerL0Texture.Reset();
	}

	void InkRenderer::ReleaseResources()
	{
		ReleaseSizeDependentResources();
		vertexShader.Reset();
		pixelShader.Reset();
		globalCB.Reset();
		inkDataBuffer.Reset();
		inkDataSRV.Reset();
		alphaBlendSampler.Reset();
		penBlendState.Reset();
		eraserBlendState.Reset();
		alphaBlendState.Reset();
		rasterState.Reset();
		dsState.Reset();
		device.Reset();
		context.Reset();
		m_bufferHead = 0;
		viewportWidth = 0.0f;
		viewportHeight = 0.0f;
	}

	bool InkRenderer::Resize(IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		if (!swapChain || width == 0 || height == 0) return false;
		const UINT oldWidth = static_cast<UINT>(viewportWidth);
		const UINT oldHeight = static_cast<UINT>(viewportHeight);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL2Texture = layerL2Texture; // 临时保留旧稳定层用于拷贝。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL1Texture = layerL1Texture; // 当前笔画层也保留，Resize 后继续显示。
		ReleaseSizeDependentResources();

		if (FAILED(swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) return false; // 先让交换链获得新尺寸的 backbuffer。
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;

		ClearRTV(layerL2RTV.Get(), windowBackgroundColor);
		ClearRTV(layerL1RTV.Get(), kTransparentLayerClearColor);
		ClearRTV(layerL0RTV.Get(), kTransparentLayerClearColor);
		ClearRTV(backBufferRTV.Get(), windowBackgroundColor);

		// 缩放后只保留新旧画布左上角的交集，不拉伸已有内容。
		const UINT copyWidth = std::min(oldWidth, width);
		const UINT copyHeight = std::min(oldHeight, height);
		if (copyWidth > 0 && copyHeight > 0)
		{
			RECT keepRect = { 0, 0, static_cast<LONG>(copyWidth), static_cast<LONG>(copyHeight) };
			if (oldL2Texture) CopyResource(layerL2Texture.Get(), oldL2Texture.Get(), keepRect); // 保留已完成笔迹。
			if (oldL1Texture) CopyResource(layerL1Texture.Get(), oldL1Texture.Get(), keepRect); // 保留正在绘制的稳定前缀。
		}
		return true;
	}

	bool InkRenderer::Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		device = inDevice; // 渲染器只借用外部统一创建的 D3D 设备。
		context = inContext;
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;

		D3D11_BUFFER_DESC constantBufferDescription = {
			sizeof(GlobalShaderConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
			D3D11_CPU_ACCESS_WRITE, 0, 0
		};
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr, globalCB.ReleaseAndGetAddressOf()))) return false;

		D3D11_BLEND_DESC blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		// 同一墨迹内取最大覆盖率，避免胶囊段边缘重复叠加变厚。
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription, penBlendState.ReleaseAndGetAddressOf()))) return false;

		blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription, alphaBlendState.ReleaseAndGetAddressOf()))) return false;

		D3D11_DEPTH_STENCIL_DESC depthStencilDescription = {};
		if (FAILED(device->CreateDepthStencilState(&depthStencilDescription, dsState.ReleaseAndGetAddressOf()))) return false;

		D3D11_RASTERIZER_DESC rasterizerDescription = {};
		rasterizerDescription.FillMode = D3D11_FILL_SOLID;
		rasterizerDescription.CullMode = D3D11_CULL_NONE;
		rasterizerDescription.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rasterizerDescription, rasterState.ReleaseAndGetAddressOf()))) return false;

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

		D3D11_SAMPLER_DESC samplerDescription = {};
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 混合图层按像素采样，不做线性模糊。
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&samplerDescription, alphaBlendSampler.ReleaseAndGetAddressOf()))) return false;
		return LoadShaders();
	}

	bool InkRenderer::LoadShaders()
	{
		const ShaderBlob vertexShaderBlob = LoadShaderFromResource(IDR_VS1);
		const ShaderBlob pixelShaderBlob = LoadShaderFromResource(IDR_PS1);
		if (!vertexShaderBlob.data || !pixelShaderBlob.data) return false;
		if (FAILED(device->CreateVertexShader(vertexShaderBlob.data, vertexShaderBlob.size, nullptr, vertexShader.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreatePixelShader(pixelShaderBlob.data, pixelShaderBlob.size, nullptr, pixelShader.ReleaseAndGetAddressOf()))) return false;
		return true;
	}
}
