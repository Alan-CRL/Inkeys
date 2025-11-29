// renderer.h

#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <atlbase.h>
#include <vector>
#include <algorithm>

#include "resource.h"

#include "main.h"

using namespace DirectX;

// 辅助加载函数 (保持不变)
struct ShaderBlob { const void* data; size_t size; };
inline ShaderBlob LoadShaderFromResource(int resourceID) {
	HMODULE hModule = ::GetModuleHandle(nullptr);
	HRSRC hRes = ::FindResource(hModule, MAKEINTRESOURCE(resourceID), L"SHADER");
	if (!hRes) return { nullptr, 0 };
	HGLOBAL hMem = ::LoadResource(hModule, hRes);
	if (!hMem) return { nullptr, 0 };
	return { ::LockResource(hMem), static_cast<size_t>(::SizeofResource(hModule, hRes)) };
}

// 包含绘制所需的全部逻辑数据，不仅仅是位置
struct InkVertex
{
	InkVertex() {};
	InkVertex(float x1Tar, float y1Tar, float r1Tar, float x2Tar, float y2Tar, float r2Tar, XMFLOAT4 colorTar)
	{
		pos = XMFLOAT2(x1Tar, y1Tar);
		color = colorTar;
		p1 = XMFLOAT2(x1Tar, y1Tar);
		p2 = XMFLOAT2(x2Tar, y2Tar);
		r1 = r1Tar;
		r2 = r2Tar;
		shapeType = 0;
	}

	XMFLOAT2 pos;       // POSITION
	XMFLOAT4 color;     // COLOR
	XMFLOAT2 p1;        // VAL_P1
	XMFLOAT2 p2;        // VAL_P2
	float    r1;        // VAL_R1
	float    r2;        // VAL_R2
	int      shapeType; // VAL_TYPE

	// 【关键修复】增加 12 字节的填充，使总大小达到 64 字节 (16的倍数)
	float    padding[3];
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
	CComPtr<ID3D11Buffer>           dynamicVB;
	CComPtr<ID3D11BlendState>       alphaBlendState;
	CComPtr<ID3D11RasterizerState>  rasterState;

	CComPtr<ID3D11Query> g_frameFinishQuery;

	// 【新增】记录当前 Vertex Buffer 写到了哪个顶点索引
	UINT m_vbOffset = 0;

	void SetOMTarget()
	{
		ID3D11RenderTargetView* rtvs[] = { renderTargetView.p };
		context->OMSetRenderTargets(1, rtvs, nullptr);
	}

	// 初始化 (保持大部分逻辑不变，只修改 InputLayout 和 VB 大小)
	bool Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain)
	{
		device = inDevice; context = inContext;

		// ... (RTV 创建代码同原版，略) ...
		CComPtr<ID3D11Texture2D> backBuffer;
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);

		SetOMTarget();

		// 1. 创建常量缓冲
		D3D11_BUFFER_DESC cbDesc = { sizeof(CB_ScreenSize), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		device->CreateBuffer(&cbDesc, nullptr, &screenCB);

		// 2. 创建混合状态 (Premultiplied Alpha or Standard)
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; // or INV_SRC_ALPHA
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;
		device->CreateBlendState(&blendDesc, &alphaBlendState);

		// 3. 创建动态顶点缓冲：固定 2MB
		const UINT INITIAL_VB_BYTES = 2 * 1024 * 1024; // 2MB
		D3D11_BUFFER_DESC vbDesc = {};
		vbDesc.ByteWidth = INITIAL_VB_BYTES;
		vbDesc.Usage = D3D11_USAGE_DYNAMIC;
		vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		HRESULT hr = device->CreateBuffer(&vbDesc, nullptr, &dynamicVB);
		if (FAILED(hr))
		{
			MessageBox(NULL, L"Create Dynamic Vertex Buffer Failed!", L"Error", MB_OK);
			return false;
		}

		cerr << "着色器 VB 缓冲上限 " << 2 << " MB。" << endl;

		// 4. 加载 Shader
		if (!LoadShaders()) return false;

		// 5. 创建光栅化状态
		{
			D3D11_RASTERIZER_DESC rasterDesc = {};
			rasterDesc.FillMode = D3D11_FILL_SOLID;

			// [关键修改] 必须设置为 D3D11_CULL_NONE，因为我们的 2D 投影翻转了 Y 轴，
			// 导致顺时针定义的三角形变成了逆时针，默认设置会把它们剔除掉。
			rasterDesc.CullMode = D3D11_CULL_NONE;

			rasterDesc.FrontCounterClockwise = FALSE;
			rasterDesc.DepthClipEnable = TRUE;
			// 如果你想启用多重采样抗锯齿(MSAA)，这里也要设为TRUE，但我们用的是Shader抗锯齿，所以无所谓
			rasterDesc.MultisampleEnable = FALSE;
			rasterDesc.AntialiasedLineEnable = FALSE;

			// 后续注意，光栅化已经设置抗锯齿，所以 Shader 抗锯齿是否需要（存疑）

			HRESULT hr = device->CreateRasterizerState(&rasterDesc, &rasterState);
			if (FAILED(hr)) return false;
		}

		InitFrameSync(inDevice);

		return true;
	}

	void SetScreenSize(float w, float h)
	{
		// 1. 更新常量缓冲
		D3D11_MAPPED_SUBRESOURCE map;
		if (SUCCEEDED(context->Map(screenCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			CB_ScreenSize* data = (CB_ScreenSize*)map.pData;
			data->width = w; data->height = h;
			context->Unmap(screenCB, 0);
		}

		// 2. 设置视口 (Viewport)
		// 如果没有这一步，光栅化器不知道要把 NDC 坐标映射到屏幕的哪个区域
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = w;
		vp.Height = h;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);
	}

	void InitFrameSync(ID3D11Device* device)
	{
		D3D11_QUERY_DESC desc{};
		desc.Query = D3D11_QUERY_EVENT;
		desc.MiscFlags = 0;

		HRESULT hr = device->CreateQuery(&desc, &g_frameFinishQuery);
		if (FAILED(hr))
		{
			Testw(L"QUERY EVENT 管线创建失败");
		}
	}

	// --- 核心绘制函数 ---
	int DrawStrokeSegment2(const vector<InkVertex>& capsules, size_t beginIndex, size_t endIndex)
	{
		if (!device || !context) return 1;
		if (beginIndex >= endIndex) return 2;
		if (beginIndex >= capsules.size()) return 3;

		endIndex = min(endIndex, capsules.size());
		size_t capsuleCountTotal = endIndex - beginIndex;
		if (capsuleCountTotal == 0) return 4;

		// 查询当前 VB 实际能容纳多少胶囊
		VBCapacity cap = GetVBCapacity();
		if (cap.maxCapsules == 0) return 5;

		size_t remainingCapsules = capsuleCountTotal;
		size_t capsuleOffset = 0; // 在输入数组中的偏移

		// 循环处理，直到画完所有胶囊
		while (remainingCapsules > 0)
		{
			// 本次最多能画多少？受限于 VB 总容量
			size_t capsulesThisDraw = min(remainingCapsules, cap.maxCapsules);
			size_t vertsThisDraw = capsulesThisDraw * VERTS_PER_CAPSULE;

			// 【核心逻辑修改 START】
			D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;

			// 1. 检查是否有足够空间追加数据
			if (m_vbOffset + vertsThisDraw > cap.maxVertices)
			{
				// 空间不够，或者已经到了 Buffer 末尾 -> 回卷 (Discard)
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_vbOffset = 0; // 重置偏移
			}

			// 首次运行保护（虽然通常 offset=0 时 NO_OVERWRITE 也可以，但 DISCARD 更安全）
			if (m_vbOffset == 0)
			{
				mapType = D3D11_MAP_WRITE_DISCARD;
			}

			// 2. Map
			D3D11_MAPPED_SUBRESOURCE map{};
			HRESULT hr = context->Map(dynamicVB, 0, mapType, 0, &map);
			if (FAILED(hr)) return 6;

			// 3. 计算写入指针
			// map.pData 返回的是 Buffer 的首地址（哪怕是 NO_OVERWRITE）
			// 所以必须加上 m_vbOffset 才能写到正确的位置
			InkVertex* bufferStart = reinterpret_cast<InkVertex*>(map.pData);
			InkVertex* currentBatchVertices = bufferStart + m_vbOffset;

			// 4. 填充数据
			for (size_t i = 0; i < capsulesThisDraw; ++i)
			{
				const InkVertex& capDesc = capsules[beginIndex + capsuleOffset + i];

				// ... 几何计算保持不变 ...
				float x1 = capDesc.p1.x; float y1 = capDesc.p1.y;
				float x2 = capDesc.p2.x; float y2 = capDesc.p2.y;
				float r1 = capDesc.r1;   float r2 = capDesc.r2;
				DirectX::XMFLOAT4 color = capDesc.color;
				int shapeType = capDesc.shapeType;

				float minX = min(x1 - r1, x2 - r2);
				float minY = min(y1 - r1, y2 - r2);
				float maxX = max(x1 + r1, x2 + r2);
				float maxY = max(y1 + r1, y2 + r2);
				float padding = 2.0f;
				minX -= padding; minY -= padding; maxX += padding; maxY += padding;

				// 指向当前胶囊的6个顶点位置
				InkVertex* v = currentBatchVertices + i * VERTS_PER_CAPSULE;

				auto SetV = [&](int idx, float px, float py) {
					v[idx].pos = DirectX::XMFLOAT2(px, py);
					v[idx].color = color;
					v[idx].p1 = DirectX::XMFLOAT2(x1, y1);
					v[idx].p2 = DirectX::XMFLOAT2(x2, y2);
					v[idx].r1 = r1; v[idx].r2 = r2;
					v[idx].shapeType = shapeType;
					// padding 不需要赋值，内存里是什么就是什么
					};

				SetV(0, minX, minY); SetV(1, maxX, minY); SetV(2, minX, maxY);
				SetV(3, minX, maxY); SetV(4, maxX, minY); SetV(5, maxX, maxY);
			}

			context->Unmap(dynamicVB, 0);

			// 5. 渲染状态设置
			UINT stride = sizeof(InkVertex); // 这里已经是 64 了
			UINT offset = 0;
			context->IASetInputLayout(inputLayout);
			context->IASetVertexBuffers(0, 1, &dynamicVB.p, &stride, &offset);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			context->VSSetShader(vertexShader, nullptr, 0);
			context->VSSetConstantBuffers(0, 1, &screenCB.p);
			context->PSSetShader(pixelShader, nullptr, 0);
			context->OMSetBlendState(alphaBlendState, nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState);

			// 6. Draw 调用
			// 参数2 (StartVertexLocation): 告诉 GPU 从 m_vbOffset 处开始读数据
			context->Draw(static_cast<UINT>(vertsThisDraw), static_cast<UINT>(m_vbOffset));

			// 7. 更新状态，为下一批次做准备
			m_vbOffset += static_cast<UINT>(vertsThisDraw);
			// 【核心逻辑修改 END】

			capsuleOffset += capsulesThisDraw;
			remainingCapsules -= capsulesThisDraw;
		}

		return 0;
	}

private:
	bool LoadShaders() {
		// 假设资源 ID 为 IDR_VS_INK 和 IDR_PS_INK
		// 为了演示，沿用你的 IDR_VS1 写法，但要注意 Shader 代码必须更新
		ShaderBlob vsBlob = LoadShaderFromResource(IDR_VS1);
		ShaderBlob psBlob = LoadShaderFromResource(IDR_PS1);

		if (!vsBlob.data || !psBlob.data) return false;

		device->CreateVertexShader(vsBlob.data, vsBlob.size, nullptr, &vertexShader);
		device->CreatePixelShader(psBlob.data, psBlob.size, nullptr, &pixelShader);

		// 更新 Input Layout 以匹配 InkVertex
		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION",      0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,                                D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",         0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },

			{ "VAL_START",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_END",       0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_RAD_START", 0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_RAD_END",   0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "VAL_TYPE",      0, DXGI_FORMAT_R32_SINT,           0, D3D11_APPEND_ALIGNED_ELEMENT,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		HRESULT hr = device->CreateInputLayout(layout, _countof(layout), vsBlob.data, vsBlob.size, &inputLayout);
		if (FAILED(hr))
		{
			MessageBox(NULL, L"CreateInputLayout Failed! Check Output Window for details.", L"Error", MB_OK);
			return false;
		}

		return true;
	}

	// 每个胶囊用 6 个顶点（三角形列表：两个三角形）
	static constexpr size_t VERTS_PER_CAPSULE = 6;

	struct VBCapacity
	{
		size_t maxVertices;
		size_t maxCapsules;
	};

	VBCapacity GetVBCapacity() const
	{
		VBCapacity cap{ 0, 0 };
		if (!dynamicVB) return cap;

		D3D11_BUFFER_DESC desc{};
		dynamicVB->GetDesc(&desc);

		size_t maxVertices = desc.ByteWidth / sizeof(InkVertex);
		size_t maxCapsules = maxVertices / VERTS_PER_CAPSULE;

		cap.maxVertices = maxVertices;
		cap.maxCapsules = maxCapsules;
		return cap;
	}
};