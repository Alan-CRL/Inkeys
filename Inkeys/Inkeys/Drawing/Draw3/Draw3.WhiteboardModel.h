#pragma once
#include <cstdint>
namespace Inkeys::Drawing::Draw3
{
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
