#pragma once

#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	enum class CanvasPanInputKind : std::uint8_t { None, Mouse, Touchpad, Touch, Pen, PenTail };

	// 保存全部子偏好；总开关仅参与有效值，不覆盖用户的输入类型选择。
	struct CanvasPanInertiaOptions
	{
		bool enabled = true;
		bool mouse = false;
		bool touchpad = true;
		bool touch = true;
		bool pen = true;
		bool penTail = false;

		constexpr bool operator==(const CanvasPanInertiaOptions&) const noexcept = default;
		constexpr bool Allows(CanvasPanInputKind kind) const noexcept
		{
			if (!enabled) return false;
			switch (kind)
			{
			case CanvasPanInputKind::Mouse: return mouse;
			case CanvasPanInputKind::Touchpad: return touchpad;
			case CanvasPanInputKind::Touch: return touch;
			case CanvasPanInputKind::Pen: return pen;
			case CanvasPanInputKind::PenTail: return penTail;
			default: return false;
			}
		}
		constexpr std::uint32_t Encode() const noexcept
		{
			return (enabled ? 1u : 0u) | (mouse ? 2u : 0u) | (touchpad ? 4u : 0u) |
				(touch ? 8u : 0u) | (pen ? 16u : 0u) | (penTail ? 32u : 0u);
		}
		static constexpr CanvasPanInertiaOptions Decode(std::uint32_t mask) noexcept
		{
			return { (mask & 1u) != 0, (mask & 2u) != 0, (mask & 4u) != 0,
				(mask & 8u) != 0, (mask & 16u) != 0, (mask & 32u) != 0 };
		}
	};
}
