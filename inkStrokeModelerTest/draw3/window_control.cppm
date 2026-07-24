module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdint>
#include <windows.h>

export module draw3.window_control;

import draw3.contact_input;

export namespace draw3
{
	// 表示鼠标左键当前使用的绘制工具。
	enum class DrawingTool
	{
		Pen,
		Highlighter,
		Eraser
	};

	// 表示当前客户区尺寸。
	struct WindowSize
	{
		int width = 0;
		int height = 0;
	};

	// 表示主循环关心的鼠标消息。
	struct MouseMessage
	{
		UINT message = 0;
		int x = 0;
		int y = 0;
	};

	// 管理窗口创建、消息回调及跨线程控制请求。
	class WindowController
	{
	public:
		// 创建覆盖主显示器的绘图窗口。
		bool Initialize(bool preconfigureNoRedirectionBitmap);
		// 首个透明帧准备完成后显示绘图窗口。
		void Show();
		// 返回窗口句柄。
		HWND Handle() const;
		// 返回当前客户区尺寸。
		WindowSize Size() const;
		// 在 D3D 资源重建成功后提交新的逻辑尺寸。
		void CommitSize(int width, int height);
		// 获取一条鼠标消息；没有消息时返回 false。
		bool TryGetMouseMessage(MouseMessage& message) const;
		// 清空当前窗口积压的鼠标消息。
		void FlushMouseMessages() const;
		// 消费一次清屏请求。
		bool ConsumeClearCanvasRequest();
		// 消费一次窗口缩放请求并返回目标尺寸。
		bool ConsumeResizeRequest(WindowSize& size);
		// 消费一次全画布呈现请求。
		bool ConsumeFullPresentRequest();
		// 消费一次 DWM 合成状态变化请求。
		bool ConsumeCompositionChangedRequest();
		// 请求下一帧执行全画布呈现。
		void RequestFullPresent();
		// 关联非拥有的输入协调器，使窗口控制请求能唤醒空闲绘制线程。
		void SetInputCoordinator(ContactInputCoordinator* coordinator);
		// 更新窗口过程对透明 GPU 合成模式的判断。
		void SetGpuTransparentComposition(bool enabled);
		// 返回当前绘制工具。
		DrawingTool ActiveTool() const;
		// 消费最近一次 Pointer 消息中的 pointerId 和笔尾提示；仅用于触觉预启动。
		bool ConsumeHapticPointerId(uint32_t& pointerId, bool& eraserHint);
		bool ConsumeHapticPointerLeave();
		// 返回主循环是否应该退出。
		bool ExitRequested() const;

	private:
		static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		LRESULT HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		void RequestControlWake();

		static WindowController* activeController_;
		HWND window_ = nullptr;
		WindowSize size_ = {};
		std::atomic<bool> clearCanvasRequested_ = false;
		std::atomic<bool> resizeRequested_ = false;
		std::atomic<bool> fullPresentRequested_ = false;
		std::atomic<bool> compositionChangedRequested_ = false;
		std::atomic<bool> exitRequested_ = false;
		std::atomic<bool> gpuTransparentComposition_ = false;
		std::atomic<int> pendingResizeWidth_ = 0;
		std::atomic<int> pendingResizeHeight_ = 0;
		std::atomic<uint32_t> pendingHapticPointerId_ = 0;
		std::atomic<bool> pendingHapticPointerEraser_ = false;
		std::atomic<bool> hapticPointerIdRequested_ = false;
		std::atomic<bool> hapticPointerLeaveRequested_ = false;
		std::atomic<DrawingTool> activeTool_ = DrawingTool::Pen;
		std::atomic<ContactInputCoordinator*> inputCoordinator_ = nullptr;
		uint32_t lastHapticPenInfoPointerId_ = 0;
		bool lastHapticPenInfoKnown_ = false;
		bool lastHapticPenInfoEraser_ = false;
	};
}
