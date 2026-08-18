module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <windows.h>

export module Inkeys.Drawing.Draw3.window_control;

import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Drawing.Draw3.pen_cursor;

export namespace Inkeys::Drawing::Draw3
{
	// 画布平移尚未纳入产品桥接，保留编译门禁并隐藏实验入口。
	inline constexpr bool kCanvasNavigationProductIntegrationEnabled = false;

	// 表示当前选择或活动 contact 锁定的绘制工具。
	enum class DrawingTool
	{
		Pen = 0,
		Highlighter = 1,
		Eraser = 2,
		Laser = 3,
		SolidLine = 4,
		DashedLine = 5,
		OutlineRectangle = 6,
		FilledRectangle = 7
	};

	// 橡皮宽度模式由窗口线程低频发布，绘制线程在物理批次首个 Down 锁定。
	enum class EraserWidthMode : uint32_t
	{
		Fixed,
		Speed
	};

	constexpr EraserWidthMode ToggleEraserWidthMode(EraserWidthMode mode) noexcept
	{
		return mode == EraserWidthMode::Fixed
			? EraserWidthMode::Speed : EraserWidthMode::Fixed;
	}
	// revision 每次真实切换递增；最低位同时编码当前模式，避免跨线程读取撕裂。
	constexpr EraserWidthMode EraserWidthModeForRevision(uint32_t revision) noexcept
	{
		return (revision & 1u) != 0u
			? EraserWidthMode::Speed : EraserWidthMode::Fixed;
	}
	static_assert(ToggleEraserWidthMode(EraserWidthMode::Fixed) == EraserWidthMode::Speed);
	static_assert(ToggleEraserWidthMode(EraserWidthMode::Speed) == EraserWidthMode::Fixed);
	static_assert(EraserWidthModeForRevision(0) == EraserWidthMode::Fixed);
	static_assert(EraserWidthModeForRevision(1) == EraserWidthMode::Speed);
	static_assert(EraserWidthModeForRevision(2) == EraserWidthMode::Fixed);

	constexpr bool IsShapeDrawingTool(DrawingTool tool) noexcept
	{
		return tool == DrawingTool::SolidLine || tool == DrawingTool::DashedLine ||
			tool == DrawingTool::OutlineRectangle || tool == DrawingTool::FilledRectangle;
	}

	// 表示当前客户区尺寸。
	struct WindowSize
	{
		int width = 0;
		int height = 0;
	};

	struct ProductVisualStyle
	{
		uint32_t colorRgba = 0x000000FFu;
		float widthDip = 2.0f;
	};

	enum class CanvasCommandType : uint8_t
	{
		Clear,
		Undo,
		NextPage,
		PreviousPage,
		TranslateViewport,
		Redo,
		SetPage,
		SetWorkspace
	};

	struct CanvasCommand
	{
		CanvasCommandType type = CanvasCommandType::Undo;
		float deltaX = 0.0f;
		float deltaY = 0.0f;
		std::size_t pageIndex = 0;
		std::uint8_t workspace = 0;
	};

	using SetExtendedStyleFlagsCallback = bool(*)(
		void* context, DWORD setMask, DWORD clearMask);

	struct ExternalWindowCallbacks
	{
		void* context = nullptr;
		SetExtendedStyleFlagsCallback setExtendedStyleFlags = nullptr;
	};

	// 适配外部窗口消息、系统光标和跨线程瞬态光标请求。
	class WindowController : public DrawingCursorEventSink
	{
	public:
		~WindowController() override;
		// 产品路径只附着 Window Service 已创建的 HWND，不取得窗口生命周期所有权。
		bool AttachExternal(HWND window, ExternalWindowCallbacks callbacks = {});
		void DetachExternal() noexcept;
		LRESULT HandleExternalMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
#if defined(DRAW3_RTS_DIAGNOSTICS)
		// Debug 下启用有界 RTS 初始化与回调诊断。
		void SetRtsTraceEnabled(bool enabled) noexcept;
#endif
		// 返回窗口句柄。
		HWND Handle() const;
		// 返回当前客户区尺寸。
		WindowSize Size() const;
		// 在 D3D 资源重建成功后提交新的逻辑尺寸。
		void CommitSize(int width, int height);
		// 按真实按键发布顺序消费低频画布命令。
		bool TryDequeueCanvasCommand(CanvasCommand& command);
		// 消费一次窗口缩放请求并返回目标尺寸。
		bool ConsumeResizeRequest(WindowSize& size);
		// 消费一次全画布呈现请求。
		bool ConsumeFullPresentRequest();
		// 消费一次 DWM 合成状态变化请求。
		bool ConsumeCompositionChangedRequest();
		// 消费一次合并的瞬态光标重绘请求。
		bool ConsumeDrawingCursorRenderRequest();
		// 请求下一帧执行全画布呈现。
		void RequestFullPresent();
		// 关联非拥有的输入协调器，使窗口控制请求能唤醒空闲绘制线程。
		void SetInputCoordinator(ContactInputCoordinator* coordinator);
		// 更新窗口过程对透明 GPU 合成模式的判断。
		void SetGpuTransparentComposition(bool enabled);
		// 返回当前绘制工具。
		DrawingTool ActiveTool() const;
		void SetActiveTool(DrawingTool tool) noexcept;
		// 选择模式与穿透样式解耦；绘制线程据此管理高精度计时器。
		bool SelectionMode() const noexcept;
		void SetSelectionMode(bool enabled) noexcept;
		// 返回当前橡皮宽度模式；活动批次仍使用其 Down 时锁定值。
		EraserWidthMode ActiveEraserWidthMode() const noexcept;
		// 返回每次 C 切换都变化的模式 revision，供绘制线程淘汰陈旧 Hover OC。
		uint32_t ActiveEraserWidthModeRevision() const noexcept;
		void SetEraserWidthMode(EraserWidthMode mode) noexcept;
		// 产品状态只发布原子样式快照，实际绘制仍由绘制线程消费。
		void SetProductVisualStyle(uint32_t colorRgba, float widthDip) noexcept;
		ProductVisualStyle ProductVisualStyleSnapshot() const noexcept;
		void EnqueueCanvasCommand(CanvasCommand command);
		void RequestExit() noexcept;
		// 配置基础工具的应用内瞬态光标外观；Shape 统一复用 Pen 外观。
		bool ConfigureDrawingCursor(DrawingTool tool,
			const DrawingCursorAppearance& appearance);
		DrawingCursorAppearance CursorAppearanceForTool(DrawingTool tool) const noexcept;
		// 控制普通绘制工具下鼠标使用系统箭头或应用光标；Eraser/Laser 不受影响。
		void SetMouseUsesSystemCursor(bool enabled) noexcept;
		bool GetMouseUsesSystemCursor() const noexcept;
		// 活动 Pen/Mouse contact 锁定有效工具；没有主指针 contact 时恢复批次选择。
		void SetActiveDrawingCursorTool(DrawingTool tool) noexcept;
		void ClearActiveDrawingCursorTool() noexcept;
		DrawingTool EffectiveDrawingCursorTool() const noexcept;
		DrawingCursorPointerAuthority CursorOwner() const noexcept;
		bool ReadPenCursorSample(DrawingCursorSample& sample) const noexcept;
		bool ReadMouseCursorSample(DrawingCursorSample& sample) const noexcept;
		// 绘制线程发布直接跟手平移状态，窗口线程据此抑制 Pen 接触反馈。
		void SetTouchPanActive(bool active) noexcept;
		bool TouchPanActive() const noexcept;
		// 活动平移观察到 Pen contact 时立即锁存抑制并清除旧光标/触觉状态。
		void SuppressPenContactForTouchPan() noexcept;
		bool PenContactSuppressedForTouchPan() const noexcept;
		void NotifyTouchContactBegin() noexcept override;
		void NotifyTouchContactEnd() noexcept override;
		void PublishPenCursorSample(const DrawingCursorSample& sample) noexcept override;
		void ClearPenCursorSample() noexcept override;
		// 消费最近一次 Pointer 消息中的 pointerId 和笔尾提示；仅用于触觉预启动。
		bool ConsumeHapticPointerId(uint32_t& pointerId, bool& eraserHint);
		bool ConsumeHapticPointerLeave();
		// 返回主循环是否应该退出。
		bool ExitRequested() const;

	private:
		LRESULT HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		void RequestControlWake();
		void QueueCanvasCommand(CanvasCommand command);
		void RequestDrawingCursorRender() noexcept;
		void QueueSystemCursorRefresh() noexcept;
		void SetDrawingCursorOwner(DrawingCursorPointerAuthority owner) noexcept;
		void SetPenContactSuppressedForTouchPan(bool suppressed) noexcept;
		void NotifyTouchContactBegin(bool trackActiveContact,
			uint32_t touchBarrierTick) noexcept;
		void PublishMouseCursorSample(const DrawingCursorSample& sample) noexcept;
		void ClearMouseCursorSample() noexcept;
		bool ShouldIgnoreMouseCursorMessage(bool promotedPointerMessage,
			bool penSampleValid, bool touchBarrierKnown,
			uint32_t mouseMessageTick, uint32_t touchBarrierTick) const noexcept;
		void ApplyWindowCursor(const char* trigger) noexcept;
#if defined(DRAW3_RTS_DIAGNOSTICS)
		void TraceCursorState(const char* eventName, uint32_t pointerId,
			POINTER_INPUT_TYPE pointerType, bool pointerTypeKnown,
			bool forceLifecycle = false) noexcept;
		void TraceTouchMouseMessage(UINT message, uint32_t messageTick,
			uint32_t touchBarrierTick, bool touchBarrierKnown,
			ULONG_PTR extraInfo, bool promotedPointerMessage,
			int x, int y, uint32_t activeTouchContactCount,
			bool accepted, const char* reason) noexcept;
#endif

		std::atomic<HWND> window_ = nullptr;
		// 外部 HWND 的消息来自 Window Service 线程，绘制线程也会读取该状态。
		std::atomic<bool> externalWindow_ = false;
		WindowSize size_ = {};
		std::mutex canvasCommandMutex_;
		std::deque<CanvasCommand> canvasCommands_;
		std::atomic<bool> resizeRequested_ = false;
		std::atomic<bool> fullPresentRequested_ = false;
		std::atomic<bool> compositionChangedRequested_ = false;
		std::atomic<bool> drawingCursorRenderRequested_ = false;
		std::atomic<bool> exitRequested_ = false;
		std::atomic<bool> gpuTransparentComposition_ = false;
		std::atomic<int> pendingResizeWidth_ = 0;
		std::atomic<int> pendingResizeHeight_ = 0;
		std::atomic<uint32_t> pendingHapticPointerId_ = 0;
		std::atomic<bool> pendingHapticPointerEraser_ = false;
		std::atomic<bool> hapticPointerIdRequested_ = false;
		std::atomic<bool> hapticPointerLeaveRequested_ = false;
		std::atomic<bool> touchPanActive_ = false;
		std::atomic<bool> realMouseTakeoverDuringTouchPan_ = false;
		std::atomic<bool> penContactSuppressedForTouchPan_ = false;
		std::atomic<bool> penCompatibilityMouseContactSuppressed_ = false;
		// 高 32 位为有效标志，低 32 位保存 Windows uptime tick，保证跨线程一致快照。
		std::atomic<uint64_t> latestTouchInputBarrierTick_ = 0;
		std::atomic<uint32_t> activeTouchContactCount_ = 0;
		std::atomic<DrawingTool> activeTool_ = DrawingTool::Pen;
		std::atomic<bool> selectionMode_ = true;
		std::atomic<uint32_t> eraserWidthModeRevision_ = 0;
		// 产品颜色按 0xRRGGBBAA 保存，默认使用不透明黑色。
		std::atomic<uint32_t> productColorRgba_ = 0x000000FFu;
		std::atomic<float> productWidthDip_ = 2.0f;
		std::atomic<int32_t> activeDrawingCursorTool_ = -1;
		std::atomic<DrawingCursorPointerAuthority> cursorOwner_ =
			DrawingCursorPointerAuthority::Unknown;
		std::atomic<bool> systemCursorRefreshPosted_ = false;
		std::atomic<bool> mouseUsesSystemCursor_ = true;
#if defined(DRAW3_RTS_DIAGNOSTICS)
		std::atomic<bool> cursorTraceEnabled_ = false;
		std::mutex cursorTraceMutex_;
		uint64_t lastCursorTraceStateKey_ = 0;
		bool lastCursorTraceStateKnown_ = false;
		uint64_t lastSystemCursorDecisionKey_ = 0;
		bool lastSystemCursorDecisionKnown_ = false;
		std::atomic<uint32_t> lastCursorTracePointerId_ = 0;
		std::atomic<uint32_t> lastCursorTracePointerType_ = PT_POINTER;
		std::atomic<bool> lastCursorTracePointerTypeKnown_ = false;
#endif
		std::atomic<ContactInputCoordinator*> inputCoordinator_ = nullptr;
		DrawingCursorSampleMailbox penCursorSample_;
		DrawingCursorSampleMailbox mouseCursorSample_;
		DrawingCursorAppearance penCursorAppearance_ = {};
		DrawingCursorAppearance highlighterCursorAppearance_ = {};
		DrawingCursorAppearance eraserCursorAppearance_ = {};
		DrawingCursorAppearance laserCursorAppearance_ = {};
		HCURSOR defaultCursor_ = nullptr;
		uint32_t lastHapticPenInfoPointerId_ = 0;
		std::atomic<uint32_t> suppressedPenPointerId_ = 0;
		bool lastHapticPenInfoKnown_ = false;
		bool lastHapticPenInfoEraser_ = false;
		bool trackingMouseLeave_ = false;
	};
}
