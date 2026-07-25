module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <vector>
#include <windows.h>

export module draw3.pen_cursor;

export namespace draw3
{
	enum class DrawingCursorShape : uint32_t
	{
		Circle,
		Rectangle
	};

	// 描述当前工具笔尖的窗口光标外观；颜色分量使用 [0,1]。
	struct DrawingCursorAppearance
	{
		DrawingCursorShape shape = DrawingCursorShape::Circle;
		float width = 0.0f;
		float height = 0.0f;
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;
	};

	enum class PenCursorDeviceState : uint32_t
	{
		Default,
		Pen,
		InvertedPen
	};

	// Pointer API 可用时覆盖 RTS 回退；Unknown 表示仅依据 RTS 状态。
	enum class PenCursorPointerAuthority : uint32_t
	{
		Unknown,
		Pen,
		NonPen
	};

	// RTS 通过窄接口发布 Pen 状态，避免输入模块依赖具体窗口实现。
	class PenCursorEventSink
	{
	public:
		virtual ~PenCursorEventSink() = default;
		virtual void PublishPenCursorDeviceState(PenCursorDeviceState state) noexcept = 0;
	};

	struct DrawingCursorBitmap
	{
		int width = 0;
		int height = 0;
		uint32_t hotspotX = 0;
		uint32_t hotspotY = 0;
		std::vector<uint32_t> premultipliedBgra;
	};

	// 生成带 1px 内描边和 50% 当前颜色填充的预乘 BGRA 光标位图。
	DrawingCursorBitmap BuildDrawingCursorBitmap(const DrawingCursorAppearance& appearance);
	// 从 CPU 位图创建调用方拥有的彩色 HCURSOR；失败时返回 nullptr。
	HCURSOR CreateDrawingCursor(const DrawingCursorAppearance& appearance);
	// 统一决定当前状态是否允许显示自定义绘制光标。
	bool ShouldShowDrawingCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority, bool toolEligible) noexcept;
}
