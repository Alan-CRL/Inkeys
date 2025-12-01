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

// --------------------------------------------------------
// 优化后的实例数据结构 (32 字节紧凑版)
// --------------------------------------------------------
struct InkVertex
{
	InkVertex() : colorPacked(0), r1(0), r2(0), shapeType(0) {};

	// 构造函数兼容旧的传参方式，但在内部进行打包
	InkVertex(float x1, float y1, float r1, float x2, float y2, float r2, XMFLOAT4 c)
	{
		// 1. 几何属性直接赋值
		this->p1 = XMFLOAT2(x1, y1);
		this->p2 = XMFLOAT2(x2, y2);
		this->r1 = r1;
		this->r2 = r2;

		// 2. 颜色打包：将 float4 压缩为 uint32
		this->colorPacked = PackColor(c);

		// 3. 类型默认为 0
		this->shapeType = 0;
	}

	// --- 内存布局 (总计 32 字节) ---
	// 4字节对齐，无需显式 Padding

	XMFLOAT2 p1;          // Offset: 0  (Size: 8)
	XMFLOAT2 p2;          // Offset: 8  (Size: 8)
	float    r1;          // Offset: 16 (Size: 4)
	float    r2;          // Offset: 20 (Size: 4)
	uint32_t colorPacked; // Offset: 24 (Size: 4) - RGBA8888
	int      shapeType;   // Offset: 28 (Size: 4)

	// End at 32. Perfect alignment.
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

	UINT m_instanceOffset = 0;

	void SetOMTarget()
	{
		ID3D11RenderTargetView* rtvs[] = { renderTargetView.p };
		context->OMSetRenderTargets(1, rtvs, nullptr);
	}

	bool Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain)
	{
		device = inDevice; context = inContext;

		CComPtr<ID3D11Texture2D> backBuffer;
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
		SetOMTarget();

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

		// 【关键更新】Input Layout 匹配新的 32 字节紧凑结构
		D3D11_INPUT_ELEMENT_DESC layout[] = {
			// Slot 0: 模板
			{ "POSITION",      0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },

			// Slot 1: 实例数据 (32字节)
			// 1. P1 (offset 0)
			{ "VAL_START",     0, DXGI_FORMAT_R32G32_FLOAT,       1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			// 2. P2 (offset 8)
			{ "VAL_END",       0, DXGI_FORMAT_R32G32_FLOAT,       1, 8,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			// 3. R1 (offset 16)
			{ "VAL_RAD_START", 0, DXGI_FORMAT_R32_FLOAT,          1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			// 4. R2 (offset 20)
			{ "VAL_RAD_END",   0, DXGI_FORMAT_R32_FLOAT,          1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },

			// 5. Color (offset 24)
			// 使用 DXGI_FORMAT_R8G8B8A8_UNORM，GPU会自动把它标准化为 0.0-1.0 的 float4
			{ "COLOR",         0, DXGI_FORMAT_R8G8B8A8_UNORM,     1, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 },

			// 6. Type (offset 28)
			{ "VAL_TYPE",      0, DXGI_FORMAT_R32_SINT,           1, 28, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};

		HRESULT hr = device->CreateInputLayout(layout, _countof(layout), vsBlob.data, vsBlob.size, &inputLayout);
		if (FAILED(hr)) {
			MessageBox(NULL, L"CreateInputLayout Failed!", L"Error", MB_OK);
			return false;
		}
		return true;
	}
};