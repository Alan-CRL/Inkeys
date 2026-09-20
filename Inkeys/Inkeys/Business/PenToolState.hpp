#pragma once

namespace Inkeys::Business
{
	enum class PenColorStateSlot
	{
		Brush,
		Highlighter,
		Laser,
	};

	// Laser 的选择记忆与顶层工具模式分离；只有回到 Pen 时才真正激活。
	[[nodiscard]] constexpr bool IsLaserToolActive(
		bool penModeActive, bool laserSelected) noexcept
	{
		return penModeActive && laserSelected;
	}

	// Laser 颜色优先于保留的普通笔型，避免改色时污染 Brush/Highlighter 记忆。
	[[nodiscard]] constexpr PenColorStateSlot ResolvePenColorStateSlot(
		bool laserActive, bool highlighterSelected) noexcept
	{
		if (laserActive) return PenColorStateSlot::Laser;
		return highlighterSelected
			? PenColorStateSlot::Highlighter : PenColorStateSlot::Brush;
	}
}
