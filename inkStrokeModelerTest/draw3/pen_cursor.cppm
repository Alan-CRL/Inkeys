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
		Rectangle,
		EraserGripCircle
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
		float opacity = 1.0f;
		float fillAlpha = 0.5f;
		float outlineWidth = 0.5f;
		float outlineRed = 184.0f / 255.0f;
		float outlineGreen = 184.0f / 255.0f;
		float outlineBlue = 184.0f / 255.0f;
	};

	enum class PenCursorDeviceState : uint32_t
	{
		Default,
		PenHover,
		InvertedPenHover,
		PenContact,
		InvertedPenContact
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
		std::vector<uint32_t> bgra;
	};

	// 生成供 CreateIconIndirect 使用的直通 Alpha BGRA 光标位图；RGB 与 Alpha 独立存储。
	DrawingCursorBitmap BuildDrawingCursorBitmap(const DrawingCursorAppearance& appearance);
	// 从 CPU 位图创建调用方拥有的彩色 HCURSOR；失败时返回 nullptr。
	HCURSOR CreateDrawingCursor(const DrawingCursorAppearance& appearance);
	// 统一把倒转和接触信息映射为窗口光标设备状态。
	PenCursorDeviceState ResolvePenCursorDeviceState(bool inverted, bool inContact) noexcept;
	// Pen 接触时隐藏当前窗口光标；系统已切换到非 Pen 时不得继续隐藏。
	bool ShouldHideDrawingCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority) noexcept;
	// 判断当前 Pen 状态是否应显示橡皮光标，而不是隐藏或显示墨迹光标。
	bool ShouldShowEraserCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority, bool eraserTool) noexcept;
	// 判断橡皮光标应使用 Hover 半透明句柄还是 Contact 不透明句柄。
	bool IsPenCursorContact(PenCursorDeviceState deviceState) noexcept;
	// 统一决定当前状态是否允许显示自定义绘制光标。
	bool ShouldShowDrawingCursor(PenCursorDeviceState deviceState,
		PenCursorPointerAuthority pointerAuthority, bool toolEligible) noexcept;
}
