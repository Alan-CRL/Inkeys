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
}
