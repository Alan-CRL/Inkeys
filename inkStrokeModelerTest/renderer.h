#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <atlbase.h>
#include <vector>
#include <algorithm>

#include "resource.h"
// #include "main.h" // 如果你不需要 main.h 里的东西，建议注释掉以解耦

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
	uint32_t colorPacked; // 24-28
	float    shapeType;   // 28-32

	// 移除构造函数，保持为纯 POD 结构，方便直接内存操作
};

struct InkPoint
{
	float x, y, r;
};

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

	// 【修改】因为彻底关闭了 Stencil，这两个可以去掉了，或者保留为空
	CComPtr<ID3D11DepthStencilState> dsState;
	// CComPtr<ID3D11DepthStencilView> dsView; // 不需要 DSV 了

	UINT m_instanceOffset = 0;

	void SetOMTarget()
	{
		// 【修改】不需要绑定 DSV，只绑定 RenderTarget
		// 这样可以节省显存带宽，提高像素填充率
		ID3D11RenderTargetView* rtvs[] = { renderTargetView.p };
		context->OMSetRenderTargets(1, rtvs, nullptr);

		// 如果之前设置过 DepthStencilState，最好重置一下，虽然绑定 nullptr 已经足够
		if (dsState) context->OMSetDepthStencilState(dsState, 0);
	}

	// 【修改】ClearStencil 不再需要，函数留空防止外部调用报错
	void ClearStencil() {}

	bool Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain)
	{
		device = inDevice; context = inContext;

		CComPtr<ID3D11Texture2D> backBuffer;
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);

		// --- 【修改】不再创建 DepthStencil Texture 和 View ---
		// 既然为了性能要关闭 Stencil，就不要分配这部分显存
		// 原始代码中的 CreateTexture2D(descDepth) 被移除

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

		// --- 【修改】创建一个禁用 Stencil 的状态 ---
		D3D11_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = FALSE;
		dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsDesc.StencilEnable = FALSE; // 【关键】显式关闭 Stencil

		device->CreateDepthStencilState(&dsDesc, &dsState);

		SetOMTarget();

		// 实例缓冲
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

	// --- 【修改】高度优化的 DrawStroke ---
	// 包含 CPU 稀疏化逻辑，且去除了循环内的对象构造和颜色打包
	int DrawStroke(const vector<InkPoint>& points, XMFLOAT4 color)
	{
		// 至少需要两个点
		size_t count = points.size();
		if (count < 2) return 0;

		// 静态缓存，避免每次调用都重新分配内存
		// 注意：多线程调用时需改为成员变量或加锁，单线程渲染无问题
		static vector<InkVertex> batchCache;
		batchCache.clear();
		// 预估最大容量，避免 push_back 时的 realloc
		if (batchCache.capacity() < count) {
			batchCache.reserve(count);
		}

		// 1. 颜色打包提取到循环外
		uint32_t packedColor = PackColor(color);

		// 2. 稀疏化逻辑
		size_t lastValidIdx = 0;

		for (size_t i = 1; i < count; ++i)
		{
			const InkPoint& pA = points[lastValidIdx];
			const InkPoint& pB = points[i];

			// 距离计算 (使用平方距离避免 sqrt)
			float dx = pA.x - pB.x;
			float dy = pA.y - pB.y;
			float distSq = dx * dx + dy * dy;

			// 3. 阈值计算：半径的 1/5
			// 限制最小阈值 (例如 1.0f)，防止半径极小时除以5导致阈值过小，或者点完全重合
			float threshold = max(1.0f, pA.r * 0.2f);
			float thresholdSq = threshold * threshold;

			// 4. 判断逻辑：
			// 如果距离太近 且 不是最后一个点，则跳过
			if (distSq < thresholdSq && i != count - 1)
			{
				continue;
			}

			// 5. 直接构造数据，避免 InkVertex 临时对象的拷贝构造
			// 使用 emplace_back 或直接 push_back 构造好的对象
			InkVertex& v = batchCache.emplace_back();
			v.p1.x = pA.x; v.p1.y = pA.y;
			v.p2.x = pB.x; v.p2.y = pB.y;
			v.r1 = pA.r;
			v.r2 = pB.r;
			v.colorPacked = packedColor;
			v.shapeType = 0.0f;

			// 更新基准点
			lastValidIdx = i;
		}

		// 6. 提交绘制
		if (batchCache.empty()) return 0;
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

			// 极速拷贝
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
			// Slot 0: 模板 (矩形)
			{ "POSITION",      0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },

			// Slot 1: 实例数据
			{ "VAL_START",     0, DXGI_FORMAT_R32G32_FLOAT,       1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VAL_END",       0, DXGI_FORMAT_R32G32_FLOAT,       1, 8,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VAL_RAD_START", 0, DXGI_FORMAT_R32_FLOAT,          1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VAL_RAD_END",   0, DXGI_FORMAT_R32_FLOAT,          1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COLOR",         0, DXGI_FORMAT_R8G8B8A8_UNORM,     1, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
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