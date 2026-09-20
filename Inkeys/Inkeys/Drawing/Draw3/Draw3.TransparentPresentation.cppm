module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <dxgi1_2.h>
#include <cstdint>
#include <memory>
#include <windows.h>

export module Inkeys.Drawing.Draw3.transparent_presentation;

import Inkeys.Drawing.Draw3.graphics_initialization;
import Inkeys.Drawing.Draw3.renderer;

export namespace Inkeys::Drawing::Draw3
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

	enum class TransparentOutputTarget : std::uint8_t
	{
		PrimaryDrawpad,
		SelectionUlw,
	};

	using SetWindowStyleCallback = bool(*)(
		void* context, DWORD setMask, DWORD clearMask);
	struct TransparentPresentationCallbacks
	{
		void* context = nullptr;
		SetWindowStyleCallback setExtendedStyleFlags = nullptr;
	};

	struct TransparentPresentationOptions
	{
		// 指定后只尝试该真实后端；Automatic 仍由 Host 使用默认回退链。
		bool requireMode = false;
		TransparentPresentMode requiredMode = kPreferredTransparentPresentMode;
		// Window Service 重建 legacy-compatible HWND 后跳过 DComp，避免再次固化窗口样式。
		bool allowDirectComposition = true;
	};

	struct TransparentPresentObservation
	{
		bool ulw = false;
		bool usedDirtyRect = false;
		bool premultipliedAlphaValid = true;
		bool updatedRegionAllZeroAlpha = false;
		bool fullFrameAllZeroAlpha = false;
		TransparentOutputTarget outputTarget =
			TransparentOutputTarget::PrimaryDrawpad;
		std::uint64_t outputRevision = 0;
		std::uint64_t presentedContentRevision = 0;
	};

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
		bool Initialize(HWND primaryWindow, HWND selectionWindow,
			GraphicsDeviceResources& graphics, InkRenderer& renderer,
			UINT width, UINT height, TransparentPresentationCallbacks callbacks = {},
			TransparentPresentationOptions options = {});
		// 只记录绘制线程请求；目标首个完整帧成功前不会改变窗口可见性。
		std::uint64_t SetOutputTarget(TransparentOutputTarget target) noexcept;
		TransparentOutputTarget RequestedOutputTarget() const noexcept;
		std::uint64_t RequestedOutputRevision() const noexcept;
		// 通知当前 presenter 窗口尺寸已经改变。
		bool Resize(UINT width, UINT height);
		// 释放交换链和透明 presenter，但不触碰外部 HWND 生命周期。
		void Shutdown() noexcept;
		// 呈现指定脏矩形或整张画布。
		bool Present(RECT dirty, bool presentFull,
			std::uint64_t contentRevision = 0);
		// 在 DWM 合成状态变化后刷新玻璃效果。
		bool RefreshAfterCompositionChanged();
		// Resize 等外部图形操作失败后，记录当前设备状态并安排恢复。
		void MarkRuntimeFailure(HRESULT result = E_FAIL) noexcept;
		// 返回是否需要在绘制线程重建设备或降级透明后端。
		bool RecoveryPending() const noexcept;
		// 在原 HWND 上执行恢复；设备仍有效时只尝试当前模式之后的回退项。
		bool RecoverFromRuntimeFailure();
		HRESULT LastFailure() const noexcept;
		// 返回当前生效的透明呈现模式。
		TransparentPresentMode ActiveMode() const;
		// 返回绘制线程上一次真实 Present 的 ULW alpha/dirty-rect 观测。
		TransparentPresentObservation LastPresentObservation() const;
		// 返回当前模式是否通过 GPU/DWM 读取 backbuffer alpha。
		bool IsGpuTransparentComposition() const;
		// 返回当前交换链供渲染器缩放使用。
		IDXGISwapChain1* SwapChain() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
