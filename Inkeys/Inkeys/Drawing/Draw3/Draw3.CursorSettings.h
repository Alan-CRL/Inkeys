#pragma once

#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	// 取值 0/1 与旧版两个下拉保持一致，使旧配置无需转换即可被读取。
	// Circle 即界面上的“小圆点”（随笔宽的圆形预览）；HighlightDot 为固定高亮描边的变体。
	enum class MouseCursorStyle : std::uint8_t { System = 0, Drawing = 1, Circle = 2, HighlightDot = 3 };
	enum class PenCursorStyle : std::uint8_t { Circle = 0, PenIcon = 1, HighlightDot = 2 };
	// 触摸默认无光标，且“无光标”只属于触摸。
	enum class TouchCursorStyle : std::uint8_t { None = 0, Circle = 1, PenIcon = 2, HighlightDot = 3 };
	// 只控制屏幕笔/数位板圆形与画笔图标的外描边；小圆点的两个变体和鼠标不受其影响。
	enum class PenHighlightMode : std::uint8_t { Off = 0, DigitizerOnly = 1, Always = 2 };

	// 只保存可选视觉；系统触摸光标始终隐藏，不进入用户配置。
	struct CursorSettings
	{
		MouseCursorStyle mouseStyle = MouseCursorStyle::System;
		PenCursorStyle screenPenStyle = PenCursorStyle::Circle;
		PenCursorStyle tabletStyle = PenCursorStyle::Circle;
		TouchCursorStyle touchStyle = TouchCursorStyle::None;
		PenHighlightMode penHighlight = PenHighlightMode::DigitizerOnly;
		// 非 Windows Ink 数位板以“驱动注入的鼠标”到达；开启后按数位板行处理。
		bool injectedMouseAsTablet = false;
		friend bool operator==(const CursorSettings&, const CursorSettings&) = default;
	};

	constexpr MouseCursorStyle RestoreMouseCursorStyle(int value) noexcept
	{
		return value >= 0 && value <= 3 ? static_cast<MouseCursorStyle>(value) : MouseCursorStyle::System;
	}

	constexpr PenCursorStyle RestorePenCursorStyle(int value) noexcept
	{
		return value >= 0 && value <= 2 ? static_cast<PenCursorStyle>(value) : PenCursorStyle::Circle;
	}

	constexpr TouchCursorStyle RestoreTouchCursorStyle(int value) noexcept
	{
		return value >= 0 && value <= 3 ? static_cast<TouchCursorStyle>(value) : TouchCursorStyle::None;
	}

	constexpr PenHighlightMode RestorePenHighlightMode(int value) noexcept
	{
		return value >= 0 && value <= 2 ? static_cast<PenHighlightMode>(value) : PenHighlightMode::DigitizerOnly;
	}

	// 光标设置页预览与实际渲染共用的外观常量，避免两处漂移。
	inline constexpr float kCursorPenIconDip = 24.0f;
	inline constexpr float kCursorHighlightEdgeDip = 1.25f;
	inline constexpr float kCursorPlainEdgeDip = 0.5f;

	enum class CursorDevice : std::uint8_t { Mouse, Touch, ScreenPen, Tablet };

	// 小圆点与画笔光标的外描边是否高亮（高亮小圆点恒高亮，不走此函数）：
	// 鼠标的画笔图标高亮、小圆点普通；触摸不高亮；笔设备按高亮模式。
	// Unknown/Conflict 笔与开启识别的注入鼠标按数位板行处理。
	constexpr bool IsCursorEdgeHighlighted(CursorDevice device, PenHighlightMode mode, bool penIcon) noexcept
	{
		switch (device)
		{
		case CursorDevice::Mouse: return penIcon;
		case CursorDevice::Touch: return false;
		case CursorDevice::ScreenPen: return mode == PenHighlightMode::Always;
		default: return mode != PenHighlightMode::Off;
		}
	}

	// 屏幕笔/数位板键为 -1 表示沿用旧版单一“笔”类型（只有 0/1 合法）。
	constexpr PenCursorStyle RestoreDevicePenCursorStyle(int deviceValue, int legacyPenValue) noexcept
	{
		return deviceValue >= 0 ? RestorePenCursorStyle(deviceValue)
			: (legacyPenValue == 1 ? PenCursorStyle::PenIcon : PenCursorStyle::Circle);
	}

	// Controller 用一个原子字发布完整设置：mouse2 | screenPen2 | tablet2 | touch3 | highlight2 | injected1 | valid1。
	inline constexpr std::uint32_t kCursorSettingsValidBit = 1u << 12;

	constexpr std::uint32_t PackCursorSettings(const CursorSettings& s) noexcept
	{
		return static_cast<std::uint32_t>(s.mouseStyle)
			| (static_cast<std::uint32_t>(s.screenPenStyle) << 2)
			| (static_cast<std::uint32_t>(s.tabletStyle) << 4)
			| (static_cast<std::uint32_t>(s.touchStyle) << 6)
			| (static_cast<std::uint32_t>(s.penHighlight) << 9)
			| (s.injectedMouseAsTablet ? 1u << 11 : 0u)
			| kCursorSettingsValidBit;
	}

	constexpr CursorSettings UnpackCursorSettings(std::uint32_t bits) noexcept
	{
		return CursorSettings{
			RestoreMouseCursorStyle(static_cast<int>(bits & 3u)),
			RestorePenCursorStyle(static_cast<int>((bits >> 2) & 3u)),
			RestorePenCursorStyle(static_cast<int>((bits >> 4) & 3u)),
			RestoreTouchCursorStyle(static_cast<int>((bits >> 6) & 7u)),
			RestorePenHighlightMode(static_cast<int>((bits >> 9) & 3u)),
			((bits >> 11) & 1u) != 0 };
	}
}
