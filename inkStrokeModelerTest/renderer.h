#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <atlbase.h>
#include <vector>
#include <algorithm>

#include "resource.h"

using namespace DirectX;
using namespace std;

// 辅助加载函数
struct ShaderBlob { const void* data; size_t size; };
inline ShaderBlob LoadShaderFromResource(int resourceID)
{
	HMODULE hModule = ::GetModuleHandle(nullptr);
	HRSRC hRes = ::FindResource(hModule, MAKEINTRESOURCE(resourceID), L"SHADER");
	if (!hRes) return { nullptr, 0 };
	HGLOBAL hMem = ::LoadResource(hModule, hRes);
	if (!hMem) return { nullptr, 0 };
	return { ::LockResource(hMem), static_cast<size_t>(::SizeofResource(hModule, hRes)) };
}

// 全局常量缓冲区
struct CB_Global
{
	float width;      // 屏幕宽
	float height;     // 屏幕高
	float shapeType;  // 笔刷形状

	// Buffer 开始位置
	uint32_t bufferOffset;

	XMFLOAT4 color;   // 当前笔画颜色
};

struct InkPoint
{
	float x, y, r;
	float time;
};

class InkRenderer
{
public:
	// 设备 上下文
	CComPtr<ID3D11Device> device;
	CComPtr<ID3D11DeviceContext> context;

	// 缓冲
	CComPtr<ID3D11Texture2D> screenTexture;
	CComPtr<ID3D11Texture2D> offScreenTexture1 = nullptr; // 1级缓冲 -> 烘干墨迹层

	// RTV
	CComPtr<ID3D11RenderTargetView> renderTargetView; // 窗口中的 RTV
	CComPtr<ID3D11RenderTargetView> offScreenTexture1RTV;

	// 着色器
	CComPtr<ID3D11VertexShader> vertexShader;
	CComPtr<ID3D11PixelShader> pixelShader;

	// 缓冲数据
	CComPtr<ID3D11Buffer> globalCB;
	CComPtr<ID3D11Buffer> inkDataBuffer;
	CComPtr<ID3D11ShaderResourceView> inkDataSRV;

	// 渲染属性
	CComPtr<ID3D11BlendState> alphaBlendState;
	CComPtr<ID3D11RasterizerState> rasterState;
	CComPtr<ID3D11DepthStencilState> dsState;

	// 缓冲区管理
	size_t m_bufferHead = 0; // 当前写入位置
	const size_t MAX_BUFFER_CAPACITY = 200000; // 固定大容量 (约2.4MB)，通常足够一帧使用

	// 视口属性
	float viewportWidth = 0.0f;
	float viewportHeight = 0.0f;

	// 绘制函数部分
public:
	int DrawStroke(const vector<InkPoint>& points, XMFLOAT4 color, float shapeType = 0.0f)
	{
		size_t totalPoints = points.size();
		if (totalPoints < 2) return 0;

		// 第几次分段绘制
		size_t startIndex = 0;

		// 循环切分提交，直到所有点都画完
		// 条件：只要剩余点数足以构成至少一条线段（>=2点），就继续处理
		while (startIndex < totalPoints - 1)
		{
			// 1. 计算当前批次的大小
			size_t remaining = totalPoints - startIndex;
			size_t batchCount = min(remaining, MAX_BUFFER_CAPACITY); // 每次最多提交 Buffer 的最大容量

			// 2. 环形缓冲区逻辑：判断是追加(NoOverwrite)还是重置(Discard)
			D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
			// 如果剩余空间不足，或者当前是 buffer 的起始，则重置
			if (m_bufferHead + batchCount > MAX_BUFFER_CAPACITY)
			{
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}

			// 3. 拷贝数据到显存
			D3D11_MAPPED_SUBRESOURCE map;
			if (SUCCEEDED(context->Map(inkDataBuffer, 0, mapType, 0, &map)))
			{
				InkPoint* dst = reinterpret_cast<InkPoint*>(map.pData);

				// 源数据指针偏移：points.data() + startIndex
				memcpy(dst + m_bufferHead, points.data() + startIndex, batchCount * sizeof(InkPoint));

				context->Unmap(inkDataBuffer, 0);
			}

			// 4. 更新常量缓冲
			if (SUCCEEDED(context->Map(globalCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
			{
				CB_Global* cb = (CB_Global*)map.pData;
				cb->width = viewportWidth;
				cb->height = viewportHeight;
				cb->color = color;
				cb->shapeType = shapeType;

				// 告知 Shader 当前批次在 Buffer 中的起始偏移
				cb->bufferOffset = static_cast<uint32_t>(m_bufferHead);

				context->Unmap(globalCB, 0);
			}

			// 5. 设置管线
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			context->VSSetShader(vertexShader, nullptr, 0);
			ID3D11ShaderResourceView* srvs[] = { inkDataSRV.p };
			context->VSSetShaderResources(0, 1, srvs);
			context->VSSetConstantBuffers(0, 1, &globalCB.p);

			context->PSSetShader(pixelShader, nullptr, 0);
			context->OMSetBlendState(alphaBlendState, nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState);

			// 6. 绘制当前批次
			// 顶点数 = (点数 - 1) * 6
			UINT vertexCount = (static_cast<UINT>(batchCount) - 1) * 6;
			context->Draw(vertexCount, 0);

			// 清理绑定
			ID3D11ShaderResourceView* nullSRV[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullSRV);

			// 7. 更新环形缓冲区指针
			m_bufferHead += batchCount;

			// 8. 更新下一次循环的起始位置
			startIndex += (batchCount - 1);

			// 为了保证连接处不断开，下一批次的起点必须是这一批次的终点
			// 所以偏移量增加 (batchCount - 1)
		}

		return 0;
	}

	void CopyResource(ID3D11Texture2D* dst, ID3D11Texture2D* src, RECT rect)
	{
		// 1. 定义源纹理中要复制的区域 (D3D11_BOX)
		D3D11_BOX sourceRegion;
		sourceRegion.left = rect.left;
		sourceRegion.top = rect.top;
		sourceRegion.front = 0;
		sourceRegion.right = rect.right;
		sourceRegion.bottom = rect.bottom;
		sourceRegion.back = 1;

		// 2. 执行局部拷贝
		// 参数说明：
		// - 目标资源 (screenTexture)
		// - 目标子资源索引 (通常为0，除非有Mipmap)
		// - 目标位置 X, Y, Z (将源矩形粘贴到目标的哪个坐标开始)
		// - 源资源 (offScreenTexture1)
		// - 源子资源索引 (通常为0)
		// - 源区域定义 (&sourceRegion)
		context->CopySubresourceRegion(
			dst,                            // pDstResource
			0,                              // DstSubresource
			rect.left,
			rect.top,
			0,
			src,                            // pSrcResource
			0,                              // SrcSubresource
			&sourceRegion                   // pSrcBox
		);
	}

	// 属性设置部分
public:
	void SetScreenSize(float w, float h)
	{
		viewportWidth = w;
		viewportHeight = h;
		D3D11_VIEWPORT vp = { 0, 0, w, h, 0.0f, 1.0f };
		context->RSSetViewports(1, &vp);
	}
	void SetOMTarget(ID3D11RenderTargetView* renderTargetView)
	{
		ID3D11RenderTargetView* rtvs[] = { renderTargetView };
		context->OMSetRenderTargets(1, rtvs, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState, 0);
	}
	void ClearRTV(ID3D11RenderTargetView* renderTargetView, XMFLOAT4 color)
	{
		const float clearColor[4] =
		{
			color.x,
			color.y,
			color.z,
			color.w
		};
		context->ClearRenderTargetView(renderTargetView, clearColor);
	}

	bool Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain)
	{
		device = inDevice; context = inContext;

		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&screenTexture);
		device->CreateRenderTargetView(screenTexture, nullptr, &renderTargetView);

		// 1. 创建常量缓冲区
		D3D11_BUFFER_DESC cbDesc = { sizeof(CB_Global), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		device->CreateBuffer(&cbDesc, nullptr, &globalCB);

		// 创建其他缓冲
		{
			// 创建画布
			D3D11_TEXTURE2D_DESC textureDesc;
			ZeroMemory(&textureDesc, sizeof(textureDesc));
			textureDesc.Width = (float)windowInfo.w;
			textureDesc.Height = (float)windowInfo.h;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;
			device->CreateTexture2D(&textureDesc, nullptr, &offScreenTexture1);

			// 创建画布渲染目标
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
			ZeroMemory(&rtvDesc, sizeof(rtvDesc));
			rtvDesc.Format = textureDesc.Format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;
			device->CreateRenderTargetView(offScreenTexture1, &rtvDesc, &offScreenTexture1RTV);
		}

		// 2. 混合状态
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;
		device->CreateBlendState(&blendDesc, &alphaBlendState);

		// 3. 关闭 Stencil
		D3D11_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = FALSE;
		dsDesc.StencilEnable = FALSE;
		device->CreateDepthStencilState(&dsDesc, &dsState);

		// 4. 光栅化状态
		D3D11_RASTERIZER_DESC rasterDesc = {};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.FrontCounterClockwise = FALSE;
		rasterDesc.DepthClipEnable = TRUE;
		device->CreateRasterizerState(&rasterDesc, &rasterState);

		// 5. 初始化固定大小的结构化缓冲区
		D3D11_BUFFER_DESC bufDesc = {};
		bufDesc.ByteWidth = static_cast<UINT>(MAX_BUFFER_CAPACITY * sizeof(InkPoint));
		bufDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufDesc.StructureByteStride = sizeof(InkPoint);

		if (FAILED(device->CreateBuffer(&bufDesc, nullptr, &inkDataBuffer))) return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(MAX_BUFFER_CAPACITY);
		if (FAILED(device->CreateShaderResourceView(inkDataBuffer, &srvDesc, &inkDataSRV))) return false;

		return LoadShaders();
	}

private:
	bool LoadShaders()
	{
		ShaderBlob vsBlob = LoadShaderFromResource(IDR_VS1);
		ShaderBlob psBlob = LoadShaderFromResource(IDR_PS1);
		if (!vsBlob.data || !psBlob.data) return false;

		device->CreateVertexShader(vsBlob.data, vsBlob.size, nullptr, &vertexShader);
		device->CreatePixelShader(psBlob.data, psBlob.size, nullptr, &pixelShader);

		return true;
	}
};