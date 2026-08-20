#pragma once

namespace Inkeys::Business
{
	// Laser 的选择记忆与顶层工具模式分离；只有回到 Pen 时才真正激活。
	[[nodiscard]] constexpr bool IsLaserToolActive(
		bool penModeActive, bool laserSelected) noexcept
	{
		return penModeActive && laserSelected;
	}
}
