module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdint>
#include <windows.h>

export module draw3.pen_cursor;

export namespace draw3
{
	enum class DrawingCursorShape : uint32_t
	{
		Circle,
		Rectangle,
		EraserGripCircle
	};

	// 描述应用内瞬态光标外观；颜色分量使用 [0,1]，尺寸使用画布像素。
	struct DrawingCursorAppearance
	{
		DrawingCursorShape shape = DrawingCursorShape::Circle;
		float width = 0.0f;
		float height = 0.0f;
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;
		float opacity = 1.0f;
		float fillAlpha = 0.5f;
		float outlineWidth = 0.5f;
		float outlineRed = 184.0f / 255.0f;
		float outlineGreen = 184.0f / 255.0f;
		float outlineBlue = 184.0f / 255.0f;
	};

	// Pointer API 可用时决定当前系统指针来源；Unknown 使用 RTS/Mouse 样本回退。
	enum class DrawingCursorPointerAuthority : uint32_t
	{
		Unknown,
		Pen,
		Mouse,
		Touch
	};

	// RTS 或窗口线程发布的单个指针最新状态。
	struct DrawingCursorSample
	{
		float x = 0.0f;
		float y = 0.0f;
		int64_t qpc = 0;
		bool valid = false;
		bool inverted = false;
		bool inContact = false;
		uint64_t sequence = 0;
	};

	// 为跨线程 Pen/Mouse 更新提供单写者门闩和一致快照。
	class DrawingCursorSampleMailbox
	{
	public:
		DrawingCursorSampleMailbox() noexcept = default;
		DrawingCursorSampleMailbox(const DrawingCursorSampleMailbox&) = delete;
		DrawingCursorSampleMailbox& operator=(const DrawingCursorSampleMailbox&) = delete;

		// 发布最新样本；返回 true 表示可见几何或状态发生变化。
		bool Publish(DrawingCursorSample sample) noexcept;
		bool Clear() noexcept;
		bool Read(DrawingCursorSample& sample) const noexcept;

	private:
		std::atomic_flag writerLatch_ = ATOMIC_FLAG_INIT;
		std::atomic<uint64_t> sequence_ = 0;
		std::atomic<float> x_ = 0.0f;
		std::atomic<float> y_ = 0.0f;
		std::atomic<int64_t> qpc_ = 0;
		std::atomic<uint32_t> valid_ = 0;
		std::atomic<uint32_t> inverted_ = 0;
		std::atomic<uint32_t> inContact_ = 0;
	};

	// 绘制线程解析后的单枚瞬态光标；不进入 L0/L1/L2 的持久状态。
	struct DrawingCursorVisual
	{
		bool visible = false;
		float x = 0.0f;
		float y = 0.0f;
		DrawingCursorAppearance appearance = {};
	};

	// RTS 通过窄接口发布完整 Pen 光标样本，避免输入模块依赖窗口实现。
	class DrawingCursorEventSink
	{
	public:
		virtual ~DrawingCursorEventSink() = default;
		virtual void PublishPenCursorSample(const DrawingCursorSample& sample) noexcept = 0;
		virtual void ClearPenCursorSample() noexcept = 0;
	};

	bool IsValidDrawingCursorAppearance(const DrawingCursorAppearance& appearance) noexcept;
	// 依据 pointer authority、Pen/Mouse 状态和当前工具解析唯一主光标。
	DrawingCursorVisual ResolvePrimaryDrawingCursorVisual(
		const DrawingCursorSample& penSample,
		const DrawingCursorSample& mouseSample,
		DrawingCursorPointerAuthority pointerAuthority,
		const DrawingCursorAppearance& selectedAppearance,
		const DrawingCursorAppearance& eraserAppearance,
		bool selectedToolIsEraser) noexcept;
	// 决定当前 HWND 客户区应使用系统箭头还是隐藏系统光标。
	bool ShouldHideSystemDrawingCursor(DrawingCursorPointerAuthority pointerAuthority,
		bool selectedToolIsEraser, bool penSampleValid, bool mouseSampleValid) noexcept;
	// Touch 橡皮接触始终生成不透明 visual；Hover 不调用该函数。
	DrawingCursorVisual MakeTouchEraserDrawingCursorVisual(float x, float y,
		const DrawingCursorAppearance& eraserAppearance) noexcept;
	// 计算包含 1px AA 和 shader quad padding 的画布内脏区。
	RECT DrawingCursorVisualBounds(const DrawingCursorVisual& visual,
		int canvasWidth, int canvasHeight) noexcept;
	bool AreDrawingCursorVisualsEquivalent(
		const DrawingCursorVisual& left, const DrawingCursorVisual& right) noexcept;
}
