module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <vector>
#include <wrl/client.h>

export module draw3.renderer;

import draw3.pen_cursor;

export namespace draw3
{
	inline const DirectX::XMFLOAT4 kTransparentLayerClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// 表示渲染器支持的墨迹几何形状。
	enum class StrokeShape : uint32_t
	{
		RoundCapsule = 0
	};

	// 描述几何覆盖率要写入绘制墨水还是擦除墨水。
	enum class InkOperatorKind : uint32_t
	{
		Draw = 0,
		Erase = 1
	};

	// L1/L0 通常属于同一笔并取覆盖率并集；调试着色时可按时间顺序叠加。
	enum class OperatorLayerMergeMode : uint32_t
	{
		CoverageUnion = 0,
		Ordered = 1
	};

	// 描述一个带半径和时间戳的墨迹采样点。
	struct InkPoint
	{
		float x;
		float y;
		float r;
		float time;
	};

	static_assert(sizeof(InkPoint) == 16, "InkPoint 必须与结构化缓冲区布局保持一致");

	// 荧光笔是固定竖直矩形沿中心线扫掠的 primitive；p1 == p2 表示单击矩形。
	struct HighlighterPrimitive
	{
		DirectX::XMFLOAT2 p1 = {};
		DirectX::XMFLOAT2 p2 = {};
		DirectX::XMFLOAT2 halfSize = { 1.25f, 25.0f };
	};

	static_assert(sizeof(HighlighterPrimitive) == 24,
		"HighlighterPrimitive 必须与结构化缓冲区布局保持一致");

	// 激光笔尖和粒子共用四浮点批数据；time 保存瞬态 opacity。
	struct LaserDot
	{
		float x = 0.0f;
		float y = 0.0f;
		float radius = 0.0f;
		float opacity = 0.0f;
	};

	static_assert(sizeof(LaserDot) == 16,
		"LaserDot 必须与 InkPoint/结构化缓冲区布局保持一致");

	// 独立 b1 常量缓冲区；字段顺序必须与 ink.hlsli 的 LaserStyleBuffer 一致。
	struct LaserStyleConstants
	{
		DirectX::XMFLOAT4 radii = {};
		DirectX::XMFLOAT4 coreColor = {};
		DirectX::XMFLOAT4 scatterColor = {};
		DirectX::XMFLOAT4 borderColor = {};
		DirectX::XMFLOAT4 glowColor = {};
		DirectX::XMFLOAT4 parameters = {};
	};

	static_assert(sizeof(LaserStyleConstants) == 96,
		"LaserStyleConstants 必须与 HLSL b1 保持 16 字节对齐");

	struct HighlighterGeometry
	{
		std::vector<HighlighterPrimitive> primitives;
		RECT bounds = { 0, 0, 0, 0 };
	};

	// 一个临时层保存仿射操作 Result = Add + Retain * Below。
	struct OperatorLayerResources
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> addTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> addRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> addSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> retainTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> retainRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> retainSRV;
	};

	// 一张 RGBA8 coverage 分别保存白芯、散射脊、红边和红晕。
	struct LaserCoverageResources
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
	};

	// 管理墨迹着色器、绘制图层及其 D3D11 资源。
	class InkRenderer
	{
	public:
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBufferTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> layerL2Texture;
		OperatorLayerResources layerL1;
		OperatorLayerResources layerL0;
		LaserCoverageResources laserStableCoverage;
		LaserCoverageResources laserLiveCoverage;

		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferRTV;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> layerL2RTV;

		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
		Microsoft::WRL::ComPtr<ID3D11Buffer> globalCB;
		Microsoft::WRL::ComPtr<ID3D11Buffer> laserStyleCB;
		Microsoft::WRL::ComPtr<ID3D11Buffer> inkDataBuffer;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inkDataSRV;
		Microsoft::WRL::ComPtr<ID3D11Buffer> highlighterPrimitiveBuffer;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> highlighterPrimitiveSRV;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> operatorSampler;

		Microsoft::WRL::ComPtr<ID3D11BlendState> strokeOperatorBlendState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> operatorResolveBlendState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> laserCoverageBlendState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dsState;

		size_t m_bufferHead = 0;
		static constexpr size_t kMaxBufferCapacity = 200000;
		float viewportWidth = 0.0f;
		float viewportHeight = 0.0f;

		// 绘制一组墨迹点，并兼容只有一个点的点击。
		int DrawStrokeOrDot(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color,
			StrokeShape shape = StrokeShape::RoundCapsule,
			InkOperatorKind operatorKind = InkOperatorKind::Draw);
		// 将墨迹点分批写入结构化缓冲区并提交绘制。
		int DrawStroke(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color,
			StrokeShape shape = StrokeShape::RoundCapsule,
			InkOperatorKind operatorKind = InkOperatorKind::Draw);
		// 绘制固定矩形 sweep，临时层使用 Add/MAX、Retain/MIN 累积覆盖率。
		int DrawHighlighterPrimitives(const std::vector<HighlighterPrimitive>& primitives,
			DirectX::XMFLOAT4 color, InkOperatorKind operatorKind = InkOperatorKind::Draw);
		// 在当前 backbuffer 最上层绘制一枚瞬态应用光标，不修改 L0/L1/L2。
		void DrawTransientDrawingCursor(const DrawingCursorVisual& visual);
		// 把固定宽度胶囊写入当前 Laser coverage，四通道使用 MAX 累积。
		int DrawLaserCoverage(const std::vector<InkPoint>& points);
		// 合并 stable/live coverage，并以合法预乘 Alpha 叠加到目标。
		void ResolveLaserCoverage(ID3D11RenderTargetView* dstRTV, RECT rect, float opacity);
		// 批量绘制 Laser 笔尖或低密度粒子，不修改任何画布层。
		void DrawLaserDots(const std::vector<LaserDot>& dots, bool particles);
		// 按 DPI 配置白芯、红边、散射和外晕尺寸。
		void ConfigureLaserStyle(float dpiScale) noexcept;
		// 复制纹理中的指定矩形区域。
		void CopyResource(ID3D11Texture2D* dst, ID3D11Texture2D* src, RECT rect);
		// 将 L1/L0 仿射操作统一应用到目标 RGBA。
		void ApplyOperatorLayers(ID3D11RenderTargetView* dstRTV,
			const OperatorLayerResources& stableLayer, const OperatorLayerResources& liveLayer,
			RECT rect, OperatorLayerMergeMode mergeMode = OperatorLayerMergeMode::CoverageUnion);
		// 更新视口和屏幕尺寸。
		void SetScreenSize(float width, float height);
		// 设置当前输出合并目标。
		void SetOMTarget(ID3D11RenderTargetView* renderTargetView);
		// 同时绑定操作层的 Add/Retain 两个输出目标。
		void SetOperatorTarget(const OperatorLayerResources& layer);
		// 绑定单张 Laser coverage RTV。
		void SetLaserCoverageTarget(const LaserCoverageResources& layer);
		// 使用指定颜色清空渲染目标。
		void ClearRTV(ID3D11RenderTargetView* renderTargetView, DirectX::XMFLOAT4 color);
		// 将操作层恢复为不改变下层的单位操作。
		void ClearOperatorLayer(const OperatorLayerResources& layer);
		void ClearLaserCoverage(const LaserCoverageResources& layer);
		void ClearAllLaserCoverage();
		// 创建依赖窗口尺寸的 backbuffer 和三层画布资源。
		bool CreateSizeDependentResources(IDXGISwapChain1* swapChain, UINT width, UINT height);
		// 释放依赖窗口尺寸的资源。
		void ReleaseSizeDependentResources();
		// 释放渲染器持有的全部 D3D11 资源。
		void ReleaseResources();
		// 调整交换链和三层画布尺寸，并保留左上角交集内容。
		bool Resize(IDXGISwapChain1* swapChain, UINT width, UINT height);
		// 初始化渲染器及其固定资源。
		bool Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain, UINT width, UINT height);

	private:
		// 创建 BGRA8 Add 与 R16F Retain 两张尺寸相关纹理。
		bool CreateOperatorLayerResources(UINT width, UINT height, OperatorLayerResources& layer);
		bool CreateLaserCoverageResources(UINT width, UINT height, LaserCoverageResources& layer);
		bool UpdateLaserStyleConstants(float opacity);
		// 从资源中加载并创建墨迹着色器。
		bool LoadShaders();
		LaserStyleConstants laserStyleConstants_ = {};
	};
}
