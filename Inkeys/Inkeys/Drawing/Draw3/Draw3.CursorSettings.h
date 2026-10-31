#pragma once

#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	enum class MouseCursorStyle : std::uint8_t { System = 0, Drawing = 1 };
	enum class PenCursorStyle : std::uint8_t { Circle = 0, PenIcon = 1 };

	// 只保存可选视觉；系统触摸光标始终隐藏，不进入用户配置。
	struct CursorSettings
	{
		MouseCursorStyle mouseStyle = MouseCursorStyle::System;
		PenCursorStyle penStyle = PenCursorStyle::Circle;
		friend bool operator==(const CursorSettings&, const CursorSettings&) = default;
	};

	constexpr MouseCursorStyle RestoreMouseCursorStyle(int value) noexcept
	{
		return value == 1 ? MouseCursorStyle::Drawing : MouseCursorStyle::System;
	}

	constexpr PenCursorStyle RestorePenCursorStyle(int value) noexcept
	{
		return value == 1 ? PenCursorStyle::PenIcon : PenCursorStyle::Circle;
	}
}
