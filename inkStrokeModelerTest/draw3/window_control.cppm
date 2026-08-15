module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <windows.h>

export module draw3.window_control;

import draw3.contact_input;
import draw3.pen_cursor;

export namespace draw3
{
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

	enum class CanvasCommandType : uint8_t
	{
		Undo,
		NextPage,
		PreviousPage,
		TranslateViewport,
		Redo
	};

	struct CanvasCommand
	{
		CanvasCommandType type = CanvasCommandType::Undo;
		float deltaX = 0.0f;
		float deltaY = 0.0f;
	};

	// 临时产品接入门禁：接入 Inkeys 白板后改为 true，同时恢复双指和方向键平移。
	inline constexpr bool kCanvasNavigationProductIntegrationEnabled = false;

	// 管理窗口创建、消息回调、系统光标和跨线程瞬态光标请求。
	class WindowController : public DrawingCursorEventSink
	{
	public:
		~WindowController() override;
		// 创建覆盖主显示器的绘图窗口。
		bool Initialize(bool preconfigureNoRedirectionBitmap);
#if defined(DRAW3_RTS_DIAGNOSTICS)
		// Debug 下启用有界 RTS 初始化与回调诊断。
		void SetRtsTraceEnabled(bool enabled) noexcept;
#endif
		// 首个透明帧准备完成后显示绘图窗口。
		void Show();
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
		// 控制独立性能测试 HUD；窗口不参与墨迹 backbuffer 和 dirty rect。
		void SetPerformanceHudEnabled(bool enabled) noexcept;
		bool GetPerformanceHudEnabled() const noexcept;
		void UpdatePerformanceHudText(std::wstring_view text);
		// 返回主循环是否应该退出。
		bool ExitRequested() const;

	private:
		static unsigned __stdcall WindowThreadEntry(void* context);
		static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		static LRESULT CALLBACK PerformanceHudWindowProcedure(
			HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		void RunWindowThread();
		bool CreatePerformanceHudWindow(HWND owner);
		void DestroyPerformanceHudWindow() noexcept;
		void RefreshPerformanceHudWindow();
		void PostPerformanceHudRefresh() noexcept;
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
		HANDLE windowThread_ = nullptr;
		DWORD windowThreadId_ = 0;
		HANDLE windowReadyEvent_ = nullptr;
		DWORD initialExtendedStyle_ = 0;
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
		std::atomic<HWND> performanceHudWindow_ = nullptr;
		std::atomic<bool> performanceHudEnabled_ = false;
		std::atomic<bool> performanceHudRefreshPosted_ = false;
		mutable std::mutex performanceHudMutex_;
		std::wstring performanceHudText_ = L"性能监控\r\n等待开始绘制……";
	};
}
