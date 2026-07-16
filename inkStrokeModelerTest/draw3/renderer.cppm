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

export namespace draw3
{
	inline const DirectX::XMFLOAT4 kTransparentLayerClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// 表示渲染器支持的墨迹几何形状。
	enum class StrokeShape : uint32_t
	{
		RoundCapsule = 0,
		SharpStrip = 3
	};

	// 描述一个带半径、时间戳和平均走势的墨迹采样点。
	struct InkPoint
	{
		float x;
		float y;
		float r;
		float time;
		float directionX = 1.0f;
		float directionY = 0.0f;
	};

	static_assert(sizeof(InkPoint) == 24, "InkPoint 必须与结构化缓冲区布局保持一致");

	// 管理墨迹着色器、绘制图层及其 D3D11 资源。
	class InkRenderer
	{
	public:
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBufferTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> layerL2Texture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> layerL1Texture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> layerL0Texture;

		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferRTV;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> layerL2RTV;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> layerL1RTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> layerL1SRV;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> layerL0RTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> layerL0SRV;

		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
		Microsoft::WRL::ComPtr<ID3D11Buffer> globalCB;
		Microsoft::WRL::ComPtr<ID3D11Buffer> inkDataBuffer;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inkDataSRV;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> alphaBlendSampler;

		Microsoft::WRL::ComPtr<ID3D11BlendState> strokeCoverageBlendState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> alphaBlendState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> destinationOutBlendState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dsState;

		size_t m_bufferHead = 0;
		static constexpr size_t kMaxBufferCapacity = 200000;
		float viewportWidth = 0.0f;
		float viewportHeight = 0.0f;

		// 绘制一组墨迹点，并兼容只有一个点的点击。
		int DrawStrokeOrDot(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color,
			StrokeShape shape = StrokeShape::RoundCapsule);
		// 将墨迹点分批写入结构化缓冲区并提交绘制。
		int DrawStroke(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color,
			StrokeShape shape = StrokeShape::RoundCapsule);
		// 复制纹理中的指定矩形区域。
		void CopyResource(ID3D11Texture2D* dst, ID3D11Texture2D* src, RECT rect);
		// 将源纹理混合到整个目标视图。
		void BlendResourceGlobal(ID3D11RenderTargetView* dstRTV, ID3D11ShaderResourceView* srcSRV);
		// 将源纹理按脏矩形混合到目标视图。
		void AlphaBlendResource(ID3D11RenderTargetView* dstRTV, ID3D11ShaderResourceView* srcSRV, RECT rect);
		// 使用两层覆盖率遮罩按脏矩形裁除目标内容。
		void ClipResource(ID3D11RenderTargetView* dstRTV, ID3D11ShaderResourceView* stableMaskSRV,
			ID3D11ShaderResourceView* liveMaskSRV, RECT rect);
		// 合并两层覆盖率遮罩，着色后只以 source-over 合成一次。
		void BlendCoverageResource(ID3D11RenderTargetView* dstRTV, ID3D11ShaderResourceView* stableMaskSRV,
			ID3D11ShaderResourceView* liveMaskSRV, DirectX::XMFLOAT4 color, RECT rect);
		// 更新视口和屏幕尺寸。
		void SetScreenSize(float width, float height);
		// 设置当前输出合并目标。
		void SetOMTarget(ID3D11RenderTargetView* renderTargetView);
		// 使用指定颜色清空渲染目标。
		void ClearRTV(ID3D11RenderTargetView* renderTargetView, DirectX::XMFLOAT4 color);
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
		enum class CompositeMode { SourceOver, DestinationOut, CoverageTint };
		// 使用矩形着色器执行普通复制、destination-out 或双遮罩着色合成。
		void CompositeResources(ID3D11RenderTargetView* dstRTV, ID3D11ShaderResourceView* primarySRV,
			ID3D11ShaderResourceView* secondarySRV, DirectX::XMFLOAT4 color, RECT rect, CompositeMode mode);
		// 从资源中加载并创建墨迹着色器。
		bool LoadShaders();
	};
}
