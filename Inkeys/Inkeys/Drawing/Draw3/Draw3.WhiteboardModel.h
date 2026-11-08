#pragma once
#include <cstdint>
namespace Inkeys::Drawing::Draw3
{
	// 复用白板纹理的亮度边界，纹理类型不参与分类。
	inline constexpr bool IsLightWhiteboardBackground(std::uint32_t rgb) noexcept
	{
		return ((rgb >> 16) & 255) * 299 + ((rgb >> 8) & 255) * 587 + (rgb & 255) * 114 > 128000;
	}

	enum class WhiteboardPattern : std::uint8_t { Blank, Dots, Grid };
	struct WhiteboardTheme
	{
		std::uint32_t backgroundRgb = 0x123B32;
		WhiteboardPattern pattern = WhiteboardPattern::Blank;
		friend bool operator==(const WhiteboardTheme&, const WhiteboardTheme&) = default;
	};
	constexpr bool ValidWhiteboardTheme(const WhiteboardTheme& value) noexcept
	{
		return value.backgroundRgb <= 0xFFFFFF && value.pattern <= WhiteboardPattern::Grid;
	}

	// 白板点阵/网格的绘制归属：编译期单点开关。默认由 Freeze 窗口（D2D/WARP）绘制并随视口重画相位，
	// 不占用画布 GPU；Draw3 归属是保留的后备（合成路径 under pass），真机体验不佳时切换。
	// 两个取值都必须能编译；弃用一侧只是惰性，不删除。
	enum class WhiteboardPatternOwner : std::uint8_t { Freeze, Draw3 };
	inline constexpr WhiteboardPatternOwner kWhiteboardPatternOwner = WhiteboardPatternOwner::Freeze;
	inline constexpr bool kWhiteboardPatternOwnedByFreeze = kWhiteboardPatternOwner == WhiteboardPatternOwner::Freeze;
	inline constexpr bool kWhiteboardPatternOwnedByDraw3 = kWhiteboardPatternOwner == WhiteboardPatternOwner::Draw3;

	// Draw3 合成路径的唯一门控：只有白板工作区且归属为 Draw3 才产生纹理像素。
	[[nodiscard]] constexpr bool WhiteboardPatternDrawnByDraw3(bool whiteboardWorkspace,
		WhiteboardPatternOwner owner = kWhiteboardPatternOwner) noexcept
	{
		return whiteboardWorkspace && owner == WhiteboardPatternOwner::Draw3;
	}
	// Freeze 侧对应门控：归属为 Freeze 时才按视口重画相位并以 RGB 或纹理变化退休像素。
	[[nodiscard]] constexpr bool WhiteboardPatternDrawnByFreeze(
		WhiteboardPatternOwner owner = kWhiteboardPatternOwner) noexcept
	{
		return owner == WhiteboardPatternOwner::Freeze;
	}

	// 绘制线程发布给 Freeze 的“当前显示页视口”。纯值、无 Draw3 类型，经运行时回调进入 Whiteboard 模块。
	// generation 取控制器 whiteboardGeneration（每次进入白板/翻页单调递增），page 为 0 起页序号；
	// x/y 为 InkCanvas::Viewport()（后备缓冲像素，屏幕 = 画布 - 视口）。serial/qpc 由接收通道填写。
	struct WhiteboardViewportSample
	{
		bool active = false;  // 绘制线程处于白板工作区
		bool moving = false;  // 手按住或惯性滑行中：Freeze 可忽略亚像素位移，落定帧必须精确
		std::uint32_t page = 0;
		std::uint64_t generation = 0;
		float x = 0.0F, y = 0.0F;
		std::uint64_t serial = 0;
		std::int64_t qpc = 0;
	};

	// 绘制线程发布去重：只比较身份与位置，serial/qpc 由接收通道填写。离开白板后只需发布一次 active=false。
	[[nodiscard]] constexpr bool WhiteboardViewportSampleChanged(
		const WhiteboardViewportSample& previous, const WhiteboardViewportSample& next) noexcept
	{
		if (previous.active != next.active) return true;
		if (!next.active) return false;
		return previous.moving != next.moving || previous.page != next.page ||
			previous.generation != next.generation || previous.x != next.x || previous.y != next.y;
	}
}
