#pragma once

#include <cmath>
#include <limits>

namespace Inkeys::UI::Bar::Detail
{
	[[nodiscard]] inline float ResolveBarFontSize(
		double fontSize, double pixelsPerUnit, bool allowFractional) noexcept
	{
		if (!std::isfinite(fontSize) || fontSize <= 0.0
			|| !std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0.0)
			return 0.0F;
		// 合并所有缩放后再截断实际像素字号，不能四舍五入或把不足 1px 的文字放大。
		const double resolved = allowFractional ? fontSize
			: std::floor(fontSize * pixelsPerUnit) / pixelsPerUnit;
		if (!std::isfinite(resolved) || resolved <= 0.0
			|| resolved > (std::numeric_limits<float>::max)()) return 0.0F;
		return static_cast<float>(resolved);
	}
}
