#pragma once

#include <algorithm>

namespace Inkeys::Drawing::Draw3::HighlighterNib
{
	inline constexpr float kHighlighterNibAspectRatio = 8.0f;
	inline constexpr float kHighlighterDuplicateDistancePx = 0.25f;

	struct HalfSize { float x, y; };

	constexpr HalfSize ResolveHalfSize(float radius) noexcept
	{
		const float halfHeight = (std::max)(0.5f, radius);
		return { halfHeight / kHighlighterNibAspectRatio, halfHeight };
	}

	constexpr bool IsDuplicate(float deltaX, float deltaY) noexcept
	{
		return deltaX * deltaX + deltaY * deltaY <= kHighlighterDuplicateDistancePx * kHighlighterDuplicateDistancePx;
	}
}
