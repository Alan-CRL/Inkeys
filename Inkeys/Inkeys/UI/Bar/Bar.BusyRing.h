#pragma once

#include <cmath>

namespace Inkeys::UI::Bar
{
	// 白板翻页（switching）超过延迟仍未完成时，在页码区显示旋转圆环；短暂切换不闪烁。
	inline constexpr double BusyRingDelaySeconds = 0.30;
	inline constexpr double BusyRingPeriodSeconds = 0.90;
	inline constexpr float BusyRingArcFraction = 0.28F;

	[[nodiscard]] constexpr bool BusyRingVisible(bool switching, double elapsedSeconds) noexcept
	{
		return switching && elapsedSeconds >= BusyRingDelaySeconds;
	}

	// 以开始计时后的时间推导角度；不保存额外动画状态，隐藏/重开自然归零。
	[[nodiscard]] inline double BusyRingAngleDegrees(double elapsedSeconds) noexcept
	{
		if (!(elapsedSeconds > 0.0)) return 0.0;
		return std::fmod(elapsedSeconds, BusyRingPeriodSeconds) / BusyRingPeriodSeconds * 360.0;
	}
}
