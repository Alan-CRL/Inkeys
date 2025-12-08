#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <atlbase.h>
#include <vector>
#include <algorithm>

#include "resource.h"
#include "main.h"

using namespace DirectX;
using namespace std;

// 辅助加载函数
struct ShaderBlob { const void* data; size_t size; };
inline ShaderBlob LoadShaderFromResource(int resourceID) {
	HMODULE hModule = ::GetModuleHandle(nullptr);
	HRSRC hRes = ::FindResource(hModule, MAKEINTRESOURCE(resourceID), L"SHADER");
	if (!hRes) return { nullptr, 0 };
	HGLOBAL hMem = ::LoadResource(hModule, hRes);
	if (!hMem) return { nullptr, 0 };
	return { ::LockResource(hMem), static_cast<size_t>(::SizeofResource(hModule, hRes)) };
}

// 辅助：将 XMFLOAT4 颜色打包为 uint32 (RGBA)
inline uint32_t PackColor(const XMFLOAT4& color)
{
	uint8_t r = static_cast<uint8_t>(max(0.0f, min(1.0f, color.x)) * 255.0f);
	uint8_t g = static_cast<uint8_t>(max(0.0f, min(1.0f, color.y)) * 255.0f);
	uint8_t b = static_cast<uint8_t>(max(0.0f, min(1.0f, color.z)) * 255.0f);
	uint8_t a = static_cast<uint8_t>(max(0.0f, min(1.0f, color.w)) * 255.0f);

	// DXGI_FORMAT_R8G8B8A8_UNORM 在小端序内存中排列为: R(低位), G, B, A(高位)
	return (static_cast<uint32_t>(a) << 24) |
		(static_cast<uint32_t>(b) << 16) |
		(static_cast<uint32_t>(g) << 8) |
		static_cast<uint32_t>(r);
}

struct InkVertex
{
	XMFLOAT2 p1;          // 0-8
	XMFLOAT2 p2;          // 8-16
	float    r1;          // 16-20
	float    r2;          // 20-24
	uint32_t colorPacked; // 24-28 (RGBA8 -> float4 in shader)
	float    shapeType;   // 28-32 【修改这里：int -> float】

	InkVertex() : colorPacked(0), r1(0), r2(0), shapeType(0.0f) {};

	InkVertex(float x1, float y1, float r1, float x2, float y2, float r2, XMFLOAT4 c)
	{
		this->p1 = XMFLOAT2(x1, y1);
		this->p2 = XMFLOAT2(x2, y2);
		this->r1 = r1;
		this->r2 = r2;
		this->colorPacked = PackColor(c);
		this->shapeType = 0.0f; // 默认为0.0
	}
};
struct InkPoint
{
	float x, y, r;
};

// 单位矩形顶点结构
struct TemplateVertex {
	XMFLOAT2 pos; // 0.0 ~ 1.0
};

struct CB_ScreenSize {
	float width;
	float height;
	float padding[2];
};

class InkRenderer {
public:
	CComPtr<ID3D11Device>           device;
	CComPtr<ID3D11DeviceContext>    context;
	CComPtr<ID3D11RenderTargetView> renderTargetView;
	CComPtr<ID3D11VertexShader>     vertexShader;
	CComPtr<ID3D11PixelShader>      pixelShader;
	CComPtr<ID3D11InputLayout>      inputLayout;
	CComPtr<ID3D11Buffer>           screenCB;

	CComPtr<ID3D11Buffer>           instanceVB;
	CComPtr<ID3D11Buffer>           templateVB;

	CComPtr<ID3D11BlendState>       alphaBlendState;
	CComPtr<ID3D11RasterizerState>  rasterState;
	CComPtr<ID3D11Query>            g_frameFinishQuery;
	CComPtr<ID3D11DepthStencilState> dsState;

	// 只需要额外保存一个 DepthStencilView 用于 Clear 操作
	CComPtr<ID3D11DepthStencilView> dsView;

	UINT m_instanceOffset = 0;

	void SetOMTarget()
	{
		// 设置模版引用值为 1
		context->OMSetDepthStencilState(dsState, 1);
		ID3D11RenderTargetView* rtvs[] = { renderTargetView.p };
		// 绑定 DSV 以启用模版测试
		context->OMSetRenderTargets(1, rtvs, dsView);
	}
	void ClearStencil()
	{
		if (context && dsView)
		{
			context->ClearDepthStencilView(dsView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		}
	}

	bool Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain)
	{
		device = inDevice; context = inContext;

		CComPtr<ID3D11Texture2D> backBuffer;
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);

		// --- 【修改】创建带模版的 DepthStencilBuffer ---
		D3D11_TEXTURE2D_DESC descDepth = {};
		backBuffer->GetDesc(&descDepth); // 获取屏幕尺寸
		descDepth.MipLevels = 1;
		descDepth.ArraySize = 1;
		descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 24位深度，8位模版
		descDepth.SampleDesc.Count = 1;
		descDepth.SampleDesc.Quality = 0;
		descDepth.Usage = D3D11_USAGE_DEFAULT;
		descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		CComPtr<ID3D11Texture2D> depthStencilTexture;
		if (FAILED(device->CreateTexture2D(&descDepth, nullptr, &depthStencilTexture))) return false;
		if (FAILED(device->CreateDepthStencilView(depthStencilTexture, nullptr, &dsView))) return false;

		D3D11_BUFFER_DESC cbDesc = { sizeof(CB_ScreenSize), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		device->CreateBuffer(&cbDesc, nullptr, &screenCB);

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
		// --- 【修改】模版状态 (优化：重复区域检测) ---
		D3D11_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = FALSE; // 关闭深度测试（墨迹通常不需要ZBuffer）
		dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

		dsDesc.StencilEnable = TRUE; // 开启模版
		dsDesc.StencilReadMask = 0xFF;
		dsDesc.StencilWriteMask = 0xFF;

		// 正面渲染逻辑：
		// COMPARISON_NOT_EQUAL: 如果当前像素模版值 != Ref(1)，则通过测试（即没画过的地方画）
		// STENCIL_OP_REPLACE: 通过测试后，将模版值设为 Ref(1)
		dsDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;

		// 背面同理（虽然我们通常只画正面）
		dsDesc.BackFace = dsDesc.FrontFace;

		device->CreateDepthStencilState(&dsDesc, &dsState);

		SetOMTarget();

		// 实例缓冲: 32768 * 32字节 = 1MB (非常小且高效)
		const UINT MAX_INSTANCES = 32768;
		D3D11_BUFFER_DESC instDesc = {};
		instDesc.ByteWidth = MAX_INSTANCES * sizeof(InkVertex);
		instDesc.Usage = D3D11_USAGE_DYNAMIC;
		instDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		instDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&instDesc, nullptr, &instanceVB))) return false;

		TemplateVertex quadVerts[] = {
			{ XMFLOAT2(0.0f, 0.0f) }, { XMFLOAT2(1.0f, 0.0f) }, { XMFLOAT2(0.0f, 1.0f) },
			{ XMFLOAT2(0.0f, 1.0f) }, { XMFLOAT2(1.0f, 0.0f) }, { XMFLOAT2(1.0f, 1.0f) }
		};
		D3D11_BUFFER_DESC tbDesc = {};
		tbDesc.ByteWidth = sizeof(quadVerts);
		tbDesc.Usage = D3D11_USAGE_IMMUTABLE;
		tbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA tbData = { quadVerts, 0, 0 };
		if (FAILED(device->CreateBuffer(&tbDesc, &tbData, &templateVB))) return false;

		if (!LoadShaders()) return false;

		D3D11_RASTERIZER_DESC rasterDesc = {};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.FrontCounterClockwise = FALSE;
		rasterDesc.DepthClipEnable = TRUE;
		device->CreateRasterizerState(&rasterDesc, &rasterState);

		InitFrameSync(inDevice);
		return true;
	}

	void SetScreenSize(float w, float h)
	{
		D3D11_MAPPED_SUBRESOURCE map;
		if (SUCCEEDED(context->Map(screenCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			CB_ScreenSize* data = (CB_ScreenSize*)map.pData;
			data->width = w; data->height = h;
			context->Unmap(screenCB, 0);
		}
		D3D11_VIEWPORT vp = { 0, 0, w, h, 0.0f, 1.0f };
		context->RSSetViewports(1, &vp);
	}

	void InitFrameSync(ID3D11Device* device)
	{
		D3D11_QUERY_DESC desc{ D3D11_QUERY_EVENT, 0 };
		device->CreateQuery(&desc, &g_frameFinishQuery);
	}

	// --- 【修改】新的绘制函数 ---
	// 传入点集 vector<InkPoint> 和 统一颜色
	int DrawStroke(const vector<InkPoint>& points, XMFLOAT4 color)
	{
		if (points.size() < 2) return 0;

		// 将连续的点转换为线段胶囊实例
		// 建议在外部做这个转换以复用 buffer，但为了接口简单，这里演示在内部转换
		// 如果点非常多，建议用 vector::reserve 优化
		static vector<InkVertex> batchCache;
		batchCache.clear();
		batchCache.reserve(points.size() - 1);

		for (size_t i = 0; i < points.size() - 1; ++i)
		{
			const InkPoint& pA = points[i];
			const InkPoint& pB = points[i + 1];

			InkVertex v;
			v.p1 = XMFLOAT2(pA.x, pA.y);
			v.p2 = XMFLOAT2(pB.x, pB.y);
			v.r1 = pA.r;
			v.r2 = pB.r;
			v.colorPacked = PackColor(color); // 所有段使用相同颜色
			v.shapeType = 0.0f; // 0: 普通胶囊

			batchCache.push_back(v);
		}

		// 调用底层的绘制 (复用你之前的 DrawStrokeSegment2 逻辑，只需要传入转换后的数据)
		return DrawStrokeSegment2(batchCache, 0, batchCache.size());
	}
	int DrawStrokeSegment2(const vector<InkVertex>& capsules, size_t beginIndex, size_t endIndex)
	{
		if (!device || !context) return 1;
		if (beginIndex >= endIndex) return 2;
		if (beginIndex >= capsules.size()) return 3;

		endIndex = min(endIndex, capsules.size());
		size_t countTotal = endIndex - beginIndex;
		if (countTotal == 0) return 4;

		D3D11_BUFFER_DESC desc;
		instanceVB->GetDesc(&desc);
		size_t maxInstancesInBuf = desc.ByteWidth / sizeof(InkVertex);

		size_t remaining = countTotal;
		size_t currentInputIndex = beginIndex;

		while (remaining > 0)
		{
			size_t drawCount = min(remaining, maxInstancesInBuf);

			D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
			if (m_instanceOffset + drawCount > maxInstancesInBuf || m_instanceOffset == 0)
			{
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_instanceOffset = 0;
			}

			D3D11_MAPPED_SUBRESOURCE map{};
			HRESULT hr = context->Map(instanceVB, 0, mapType, 0, &map);
			if (FAILED(hr)) return 6;

			InkVertex* dest = reinterpret_cast<InkVertex*>(map.pData) + m_instanceOffset;
			const InkVertex* src = &capsules[currentInputIndex];

			// 极速拷贝：现在每个顶点只有 32 字节
			memcpy(dest, src, drawCount * sizeof(InkVertex));

			context->Unmap(instanceVB, 0);

			UINT strides[2] = { sizeof(TemplateVertex), sizeof(InkVertex) };
			UINT offsets[2] = { 0, m_instanceOffset * sizeof(InkVertex) };
			ID3D11Buffer* vbs[2] = { templateVB.p, instanceVB.p };

			context->IASetInputLayout(inputLayout);
			context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			context->VSSetShader(vertexShader, nullptr, 0);
			context->VSSetConstantBuffers(0, 1, &screenCB.p);
			context->PSSetShader(pixelShader, nullptr, 0);
			context->OMSetBlendState(alphaBlendState, nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState);

			context->DrawInstanced(6, static_cast<UINT>(drawCount), 0, 0);

			m_instanceOffset += static_cast<UINT>(drawCount);
			currentInputIndex += drawCount;
			remaining -= drawCount;
		}

		return 0;
	}

private:
	bool LoadShaders() {
		ShaderBlob vsBlob = LoadShaderFromResource(IDR_VS1);
		ShaderBlob psBlob = LoadShaderFromResource(IDR_PS1);
		if (!vsBlob.data || !psBlob.data) return false;

		device->CreateVertexShader(vsBlob.data, vsBlob.size, nullptr, &vertexShader);
		device->CreatePixelShader(psBlob.data, psBlob.size, nullptr, &pixelShader);

		D3D11_INPUT_ELEMENT_DESC layout[] = {
			// Slot 0: 模板
			{ "POSITION",      0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },

			// Slot 1: 实例数据
			{ "VAL_START",     0, DXGI_FORMAT_R32G32_FLOAT,       1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VAL_END",       0, DXGI_FORMAT_R32G32_FLOAT,       1, 8,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VAL_RAD_START", 0, DXGI_FORMAT_R32_FLOAT,          1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VAL_RAD_END",   0, DXGI_FORMAT_R32_FLOAT,          1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COLOR",         0, DXGI_FORMAT_R8G8B8A8_UNORM,     1, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 },

			// 【关键修改】这里必须是 FLOAT，否则 Intel HD 4600 会发生 Input Assembler 数据错位
			{ "VAL_TYPE",      0, DXGI_FORMAT_R32_FLOAT,          1, 28, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};

		HRESULT hr = device->CreateInputLayout(layout, _countof(layout), vsBlob.data, vsBlob.size, &inputLayout);
		if (FAILED(hr)) {
			MessageBox(NULL, L"CreateInputLayout Failed!", L"Error", MB_OK);
			return false;
		}
		return true;
	}
};