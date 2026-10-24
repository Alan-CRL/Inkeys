#pragma once

#include <cmath>

namespace Inkeys::UI::Bar::Detail
{
	[[nodiscard]] inline double ResolveBarFontCorrectionTarget(
		double targetPixels, bool allowFractional) noexcept
	{
		if (!std::isfinite(targetPixels) || targetPixels <= 0.0) return 0.0;
		// 只约束静止目标，原始字号和中间帧继续沿现有动画轨迹变化。
		return allowFractional ? 1.0 : std::floor(targetPixels) / targetPixels;
	}
}
