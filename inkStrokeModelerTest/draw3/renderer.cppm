module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <span>
#include <vector>
#include <wrl/client.h>

export module draw3.renderer;

export import draw3.laser_particles;
import draw3.pen_cursor;

export namespace draw3
{
	inline const DirectX::XMFLOAT4 kTransparentLayerClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	inline constexpr float kLaserSolidDiameterAt96Dpi = 5.0f;
	inline constexpr float kLaserCoreDiameterRatio = 1.0f / 3.0f;
	inline constexpr float kLaserDiffuseExtentAt96Dpi = 5.0f;
	inline constexpr float kLaserScatterHalfWidthToCoreRatio = 0.4f;
	// 临时性能日志开关；用户回传数据后可在后续任务中关闭或移除。
	inline constexpr bool kLaserIncrementalDiagnosticsEnabled = true;

	// Laser 的 InkPoint.r 统一表示红色实体外半径；漫反射宽度不随压力变化。
	constexpr float LaserSolidRadius(float dpiScale = 1.0f) noexcept
	{
		return kLaserSolidDiameterAt96Dpi * 0.5f * dpiScale;
	}

	constexpr float LaserCoreRadius(float solidRadius) noexcept
	{
		return solidRadius * kLaserCoreDiameterRatio;
	}

	constexpr float LaserDiffuseExtent(float dpiScale = 1.0f) noexcept
	{
		return kLaserDiffuseExtentAt96Dpi * dpiScale;
	}

	constexpr float LaserVisualRadius(float solidRadius, float dpiScale = 1.0f) noexcept
	{
		return solidRadius + LaserDiffuseExtent(dpiScale);
	}

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

	// Laser 笔尖的 radius 是红色实体外半径；粒子改由独立 GPU 缓冲区保存。
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
		DirectX::XMFLOAT4 edgeColor = {};
		DirectX::XMFLOAT4 glowColor = {};
		DirectX::XMFLOAT4 parameters = {};
	};

	static_assert(sizeof(LaserStyleConstants) == 112,
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

	// 通用 RGBA8 Laser 层；按用途保存稳定预乘颜色或单笔 coverage。
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
		LaserCoverageResources laserCompositedColor;
		LaserCoverageResources laserStrokeCoverage;

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
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> laserScissorRasterState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> dsState;

		size_t m_bufferHead = 0;
		static constexpr size_t kMaxBufferCapacity = 200000;
		float viewportWidth = 0.0f;
		float viewportHeight = 0.0f;

		// 绘制一组墨迹点，并兼容只有一个点的点击。
		int DrawStrokeOrDot(std::span<const InkPoint> points, DirectX::XMFLOAT4 color,
			StrokeShape shape = StrokeShape::RoundCapsule,
			InkOperatorKind operatorKind = InkOperatorKind::Draw);
		// 将墨迹点分批写入结构化缓冲区并提交绘制。
		int DrawStroke(std::span<const InkPoint> points, DirectX::XMFLOAT4 color,
			StrokeShape shape = StrokeShape::RoundCapsule,
			InkOperatorKind operatorKind = InkOperatorKind::Draw);
		// 绘制固定矩形 sweep，临时层使用 Add/MAX、Retain/MIN 累积覆盖率。
		int DrawHighlighterPrimitives(std::span<const HighlighterPrimitive> primitives,
			DirectX::XMFLOAT4 color, InkOperatorKind operatorKind = InkOperatorKind::Draw);
		// 在当前 backbuffer 最上层绘制一枚瞬态应用光标，不修改 L0/L1/L2。
		void DrawTransientDrawingCursor(const DrawingCursorVisual& visual);
		// 把可变压力胶囊写入当前 Laser coverage，四通道使用 MAX 累积。
		int DrawLaserCoverage(std::span<const InkPoint> points, RECT scissorRect = {});
		// 将单笔 coverage 解析为材质，并按 source-over 叠加到目标。
		void ResolveLaserStrokeCoverage(
			ID3D11RenderTargetView* dstRTV, RECT rect, float opacity = 1.0f);
		// 对稳定 L1 与实时 L0 coverage 逐通道取 MAX 后，只解析一次 Laser 材质。
		bool ResolveLaserIncrementalCoverage(
			ID3D11RenderTargetView* dstRTV, RECT rect, float opacity = 1.0f);
		// 将已烘干的预乘颜色层按整组 opacity 叠加到目标。
		void ResolveLaserCompositedColor(
			ID3D11RenderTargetView* dstRTV, RECT rect, float opacity);
		// 以不混合的矩形写零局部清理单笔 scratch。
		void ClearLaserCoverageRect(RECT rect);
		bool ClearLaserLiveCoverageRect(RECT rect);
		// 仅在绘制线程选择 Laser 后创建 L0 coverage；失败会永久降级到完整重绘。
		bool EnsureLaserIncrementalCoverageResources();
		bool LaserIncrementalCoverageAvailable() const noexcept;
		void ClearLaserIncrementalCoverage();
		// 批量绘制 Laser 笔尖，不修改任何画布层。
		void DrawLaserDots(std::span<const LaserDot> dots);
		// 固定实例绘制 GPU 粒子；死亡槽在 VS 中退化。
		void DrawLaserParticles();
		// 在主循环开始前提交零像素 draw call，迫使驱动提前 JIT 编译所有激光着色器路径。
		// 消除 Qualcomm/Adreno 等延迟编译驱动的首笔卡顿；对 Nvidia/AMD/Intel/WARP 无额外开销。
		void WarmUpLaserShaders() noexcept;
		// 按 DPI 配置白芯、实体外套、散射和固定漫反射尺寸。
		void ConfigureLaserStyle(float dpiScale) noexcept;
		void ConfigureLaserParticles(
			const LaserParticleConfig& configuration, float dpiScale) noexcept;
		bool LaserParticlesAvailable() const noexcept;
		// 一次绑定 UAV，按顺序完成存量更新与本帧全部 contact 发射。
		void StepLaserParticles(float wallDeltaSeconds, float motionDeltaSeconds,
			bool simulateExisting,
			std::span<const LaserParticleEmissionRequest> emissionRequests) noexcept;
		void ResetLaserParticles() noexcept;
		// 返回本进程当前 local/non-local GPU 显存占用；旧 DXGI 不支持时返回负值。
		double QueryVideoMemoryUsageMiB() const noexcept;
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
		// 绑定按需创建的单 contact Laser live coverage RTV。
		void SetLaserLiveCoverageTarget();
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
		void UnbindLaserCoverageShaderResources();
		bool UpdateLaserStyleConstants(float opacity);
		bool DrawLaserRectPass(ID3D11RenderTargetView* dstRTV, RECT rect,
			float opacity, float shapeType, ID3D11ShaderResourceView* source,
			UINT sourceSlot, ID3D11BlendState* blendState,
			ID3D11ShaderResourceView* secondarySource = nullptr,
			UINT secondarySourceSlot = 0);
		// 从资源中加载并创建墨迹着色器。
		bool LoadShaders();
		LaserStyleConstants laserStyleConstants_ = {};
		uint64_t laserStyleGeneration_ = 1;
		uint64_t uploadedLaserStyleGeneration_ = 0;
		float uploadedLaserStyleOpacity_ = -1.0f;
		bool laserStyleCacheValid_ = false;
		float laserParticleGlowRadiusScale_ = LaserParticleConfig{}.glowRadiusScale;
		float laserParticleGlowRed_ = LaserParticleConfig{}.glowRed;
		float laserParticleGlowGreen_ = LaserParticleConfig{}.glowGreen;
		float laserParticleGlowBlue_ = LaserParticleConfig{}.glowBlue;
		float laserParticleGlowAlpha_ = LaserParticleConfig{}.glowAlpha;
		LaserParticleSystem laserParticleSystem_;
		Microsoft::WRL::ComPtr<IDXGIAdapter3> videoMemoryAdapter_;
		// 单 contact Laser 的实时尾部 coverage 按需创建，避免非 Laser 会话分配额外画布。
		LaserCoverageResources laserLiveCoverage;
		bool laserIncrementalCoverageEnabled_ = false;
		bool laserIncrementalCoverageUnavailable_ = false;
	};
}

// 多个 Renderer 实现单元共享的 CPU/HLSL 布局，不导出到模块使用方。
namespace draw3::renderer_detail
{
	struct GlobalShaderConstants
	{
		float width;
		float height;
		float shapeType;
		uint32_t bufferOffset;
		DirectX::XMFLOAT4 color;
		uint32_t operatorKind;
		float padding[3];
	};

	static_assert(sizeof(GlobalShaderConstants) == 48);
	static_assert(sizeof(GlobalShaderConstants) % 16 == 0);
}
