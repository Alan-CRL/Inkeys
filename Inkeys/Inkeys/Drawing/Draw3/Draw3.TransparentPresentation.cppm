module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <dxgi1_2.h>
#include <cstdint>
#include <memory>
#include <string>
#include <windows.h>

#include "../../Helper/FailedCleanupDeadline.h"

export module Inkeys.Drawing.Draw3.transparent_presentation;

import Inkeys.Drawing.Draw3.graphics_initialization;
import Inkeys.Drawing.Draw3.renderer;

export namespace Inkeys::Drawing::Draw3
{
	// 首发只选择 DComp/ULW；DWM 枚举值保留给历史兼容代码。
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

	// 主画布实际生效的呈现状态（不是偏好）：设备性能看板按它划分设置签名。只在绘制 owner 线程读取。
	struct TransparentPresentationEffectiveState
	{
		TransparentPresentMode mode = TransparentPresentMode::UlwDirtyRect;
		bool warpDevice = false;
		bool waitableWait = false;        // DComp 下真正在等帧延迟句柄（取不到句柄或已熔断为 false）
		bool ulwAsyncReadback = false;    // ULW 下异步读回实际开启（未熔断）
		bool ulwCompositionAlign = false; // 且 DWM 正在合成、对齐开关开启
	};

	using SetWindowStyleCallback = bool(*)(
		void* context, DWORD setMask, DWORD clearMask);
	struct TransparentPresentationCallbacks
	{
		void* context = nullptr;
		SetWindowStyleCallback setExtendedStyleFlags = nullptr;
		// 绘制 owner 低频发布实际初始化结果，UI 只读取 Host 缓存。
		void* informationContext = nullptr;
		void (*informationChanged)(void*, const std::string&) noexcept = nullptr;
	};

	struct TransparentPresentationOptions
	{
		// 强制模式只允许 DComp/ULW；Automatic 仍由 Host 使用默认回退链。
		bool requireMode = false;
		TransparentPresentMode requiredMode = kPreferredTransparentPresentMode;
		// Window Service 重建 legacy-compatible HWND 后跳过 DComp，避免再次固化窗口样式。
		bool allowDirectComposition = true;
		// startup 专用；运行期 Recover/Resize/Present 不激活这个 scope。
		Shutdown::FailedCleanupSignal failedCleanup;
		// 与 Host 启动快照一致，运行中改设置不影响设备恢复。
		bool enableHardwareAcceleration = true;
		bool enableZhaoxinCompatibility = true;
		// 仅对取得帧延迟句柄的 DComp waitable 交换链生效；关闭时保留标志但不等待。
		bool enableWaitableWait = true;
		// ULW 主画布异步读回（最多领先一帧）、提交不超过刷新率；关闭 = 同步读回、不节流、不对齐。
		bool enableUlwAsyncReadback = true;
		// 异步读回开启且 DWM 合成时，ULW 提交对齐合成时机；无界面配置键，用于实机开/关对照。
		bool enableUlwCompositionAlign = true;
	};

	// ULW 异步读回在帧开头/空闲前的完成结果：阻塞 Map 计入等待而非工作耗时。
	struct UlwFrameReport
	{
		double waitedMs = 0.0;
		// 完成失败已安排恢复：调用方请求整帧并在下一帧走恢复流程。
		bool failed = false;
	};

	enum class FrameLatencyWaitState : std::uint8_t
	{
		NotApplicable,
		AlreadyHeld,
		Signaled,
		TimedOut,
		Tripped,
	};

	struct FrameLatencyWaitReport
	{
		FrameLatencyWaitState state = FrameLatencyWaitState::NotApplicable;
		double waitedMs = 0.0;
		// 本次等待是否真的超时；等待失败导致的熔断不算超时。
		bool timedOut = false;
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
	// 只测生产 ULW 的 CPU 脏区拷贝与 alpha 检查，不创建窗口或 GPU 资源。
	int RunUlwDirtyCopyBenchmark() noexcept;
	// 双槽异步读回自测：WARP 设备 + 从不显示的分层窗，对照异步与同步 presenter 的 DIB（流水、节流、Resize 丢弃）。
	int RunUlwPipelineSelfTest() noexcept;

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
		// 帧开头、消费输入之前调用：DComp waitable 交换链未持有令牌时做一次有界等待。
		// 超时不是错误，本帧照常呈现；返回 Tripped 时已安排恢复（重建为不带 waitable 标志的交换链）。
		FrameLatencyWaitReport PrepareFrameLatency(double targetFrameMs, bool countsTowardTrip);
		// 取走帧循环外 Present 前补等的结果，供调用方从 Present 耗时中扣除并记入指标。
		// ULW 异步读回在 Present 内的阻塞 Map（背压与即时完成）也经此计入等待。
		FrameLatencyWaitReport ConsumePresentLatencyWait() noexcept;
		// C1 帧开头（恢复/Resize 之后、读输入之前）：主画布 ULW 异步读回时探测上一帧是否就绪（不等待）。
		// 其余模式立即返回。
		UlwFrameReport BeginUlwFrame(double frameStartMs, double targetFrameMs);
		// 固定帧截止等待前（帧尾）：被节流累计的脏区到点时拷出提交（纹理已含本帧最新内容）。其余模式立即返回。
		UlwFrameReport ServiceUlwBeforeFrameWait();
		// 本帧光栅失败（最终纹理可能被部分覆写）：丢弃被节流累计的脏区并要求下一次整帧读回，
		// 之后任何收尾（输出切换、熔断、空闲前）都不会读回失败纹理。已冻结在槽里的提交不受影响。
		void DiscardUlwAccumulated() noexcept;
		// C3 进入空闲等待前：阻塞完成待完成读回并拷出累计脏区，保证停笔后最后一帧上屏。
		// textureValid=false（本帧光栅失败，最终纹理可能被部分覆写）时只完成已冻结的提交，累计脏区留给重建后的整帧。
		UlwFrameReport CompleteUlwBeforeIdle(bool textureValid = true);
		// 固定帧截止等待前：DWM 合成对齐且预设等于刷新率或为其约数时，下一帧开始的建议调整量（毫秒，正=多等）。
		double UlwFramePhaseAdjustMs(double frameStartMs) const;
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
		// 主画布实际生效的呈现状态，与选择模式输出目标无关。
		TransparentPresentationEffectiveState EffectiveState() const noexcept;
		// 返回绘制线程上一次真实 Present 的 ULW alpha/dirty-rect 观测。
		TransparentPresentObservation LastPresentObservation() const;
		// 绘制 owner 的名义像素载荷，仅用于生命周期观测，不等同进程/驱动驻留字节。
		std::uint64_t AuxiliaryReadbackPayloadBytes() const noexcept;
		// 仅提升已有辅助视觉帧；资源缺失本身不能制造空闲 Present。
		bool AuxiliaryReadbackNeedsFullPresent() const noexcept;
		// 仅在 Window Service 隐藏回执通过 Host 代际校验后，由绘制 owner 回收读回像素。
		bool ReleaseAuxiliaryReadbackResources(TransparentOutputTarget target,
			std::uint64_t outputRevision, std::uint64_t contentRevision) noexcept;
		// 返回当前模式是否通过 GPU/DWM 读取 backbuffer alpha。
		bool IsGpuTransparentComposition() const;
		// 返回当前交换链供渲染器缩放使用；ULW 模式不建交换链，返回空（最终目标是独立纹理）。
		IDXGISwapChain1* SwapChain() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
