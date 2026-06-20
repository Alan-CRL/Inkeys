module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <memory>
#include <windows.h>

export module draw3.transparent_presentation;

import draw3.graphics_initialization;
import draw3.renderer;

export namespace draw3
{
	// 列出程序支持的四种透明窗口呈现方式。
	enum class TransparentPresentMode
	{
		UlwDirtyRect,
		DirectCompositionVisualTree,
		DwmBlurBehind,
		DwmBlurBehind2
	};

	inline constexpr TransparentPresentMode kPreferredTransparentPresentMode =
		TransparentPresentMode::DirectCompositionVisualTree;

	// 返回透明呈现模式的日志名称。
	const char* TransparentPresentModeName(TransparentPresentMode mode);
	// 返回启动创建窗口前是否应预置 DComp 扩展样式。
	bool ShouldPreconfigureNoRedirectionBitmap();

	// 统一管理交换链、透明模式初始化、回退、缩放和呈现。
	class TransparentPresentationController
	{
	public:
		TransparentPresentationController();
		~TransparentPresentationController();
		TransparentPresentationController(const TransparentPresentationController&) = delete;
		TransparentPresentationController& operator=(const TransparentPresentationController&) = delete;

		// 按首选模式和回退链创建交换链、渲染器及 presenter。
		bool Initialize(HWND window, GraphicsDeviceResources& graphics, InkRenderer& renderer, UINT width, UINT height);
		// 通知当前 presenter 窗口尺寸已经改变。
		bool Resize(UINT width, UINT height);
		// 呈现指定脏矩形或整张画布。
		bool Present(RECT dirty, bool presentFull);
		// 在 DWM 合成状态变化后刷新玻璃效果。
		void RefreshAfterCompositionChanged();
		// 返回当前生效的透明呈现模式。
		TransparentPresentMode ActiveMode() const;
		// 返回当前模式是否通过 GPU/DWM 读取 backbuffer alpha。
		bool IsGpuTransparentComposition() const;
		// 返回当前窗口背景清屏颜色。
		DirectX::XMFLOAT4 WindowBackgroundColor() const;
		// 返回当前交换链供渲染器缩放使用。
		IDXGISwapChain1* SwapChain() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
