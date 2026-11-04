#pragma once

#include <cstdint>

namespace Inkeys::Drawing::Draw3
{
	// 所有设备共用同一组样式；“无光标”只对触摸有效。
	// Arrow 对鼠标是真实系统光标；对屏幕笔/数位板/触摸则由 Draw3 自绘同形状的箭头。
	enum class CursorStyle : std::uint8_t
	{
		None = 0,
		Arrow = 1,
		Pen = 2,
		HighlightPen = 3,
		Dot = 4,
		HighlightDot = 5
	};

	enum class CursorDevice : std::uint8_t { Mouse, Touch, ScreenPen, Tablet };

	// 只保存可选视觉；系统触摸光标始终隐藏，不进入用户配置。
	struct CursorSettings
	{
		CursorStyle mouse = CursorStyle::Arrow;
		CursorStyle touch = CursorStyle::None;
		CursorStyle screenPen = CursorStyle::Dot;
		CursorStyle tablet = CursorStyle::HighlightDot;
		// 非 Windows Ink 数位板以“驱动注入的鼠标”到达；开启后按数位板行处理。
		bool injectedMouseAsTablet = false;
		friend bool operator==(const CursorSettings&, const CursorSettings&) = default;
	};

	// 光标设置页预览与实际渲染共用的外观常量，避免两处漂移。
	inline constexpr float kCursorPenIconDip = 24.0f;
	inline constexpr float kCursorArrowWidthDip = 12.0f;
	inline constexpr float kCursorArrowHeightDip = 19.0f;
	inline constexpr float kCursorHighlightEdgeDip = 1.25f;
	inline constexpr float kCursorPlainEdgeDip = 0.5f;

	constexpr bool IsPenCursorStyle(CursorStyle style) noexcept
	{
		return style == CursorStyle::Pen || style == CursorStyle::HighlightPen;
	}

	constexpr bool IsDotCursorStyle(CursorStyle style) noexcept
	{
		return style == CursorStyle::Dot || style == CursorStyle::HighlightDot;
	}

	// 高亮变体使用固定高亮描边；其余使用普通描边（鼠标系统光标/箭头不使用该描边）。
	constexpr bool IsHighlightedCursorStyle(CursorStyle style) noexcept
	{
		return style == CursorStyle::HighlightPen || style == CursorStyle::HighlightDot;
	}

	constexpr CursorStyle DefaultCursorStyle(CursorDevice device) noexcept
	{
		switch (device)
		{
		case CursorDevice::Mouse: return CursorStyle::Arrow;
		case CursorDevice::Touch: return CursorStyle::None;
		case CursorDevice::ScreenPen: return CursorStyle::Dot;
		default: return CursorStyle::HighlightDot;
		}
	}

	constexpr bool IsCursorStyleValueValid(CursorDevice device, int value) noexcept
	{
		return value >= 0 && value <= 5 && (value != 0 || device == CursorDevice::Touch);
	}

	constexpr CursorStyle RestoreCursorStyle(CursorDevice device, int value) noexcept
	{
		return IsCursorStyleValueValid(device, value) ? static_cast<CursorStyle>(value) : DefaultCursorStyle(device);
	}

	// 旧版只有“鼠标/笔”两个下拉：鼠标 1 为（高亮的）画笔光标，笔 1 为画笔光标。
	constexpr CursorStyle LegacyCursorStyle(CursorDevice device, int legacyMouseValue, int legacyPenValue) noexcept
	{
		switch (device)
		{
		case CursorDevice::Mouse: return legacyMouseValue == 1 ? CursorStyle::HighlightPen : CursorStyle::Arrow;
		case CursorDevice::Touch: return CursorStyle::None;
		case CursorDevice::ScreenPen: return legacyPenValue == 1 ? CursorStyle::Pen : CursorStyle::Dot;
		default: return legacyPenValue == 1 ? CursorStyle::HighlightPen : CursorStyle::HighlightDot;
		}
	}

	// 设备键为 -1（或非法）表示沿用旧版配置/默认值。
	constexpr CursorStyle RestoreDeviceCursorStyle(CursorDevice device, int configured,
		int legacyMouseValue, int legacyPenValue) noexcept
	{
		return IsCursorStyleValueValid(device, configured) ? static_cast<CursorStyle>(configured)
			: LegacyCursorStyle(device, legacyMouseValue, legacyPenValue);
	}

	// 设置页下拉：触摸有“无光标”作为首项，其它设备从“鼠标”开始。
	constexpr int CursorStyleToItemIndex(CursorDevice device, CursorStyle style) noexcept
	{
		return static_cast<int>(style) - (device == CursorDevice::Touch ? 0 : 1);
	}

	constexpr CursorStyle CursorStyleFromItemIndex(CursorDevice device, int index) noexcept
	{
		const int value = index + (device == CursorDevice::Touch ? 0 : 1);
		return RestoreCursorStyle(device, value);
	}

	constexpr int CursorStyleItemCount(CursorDevice device) noexcept
	{
		return device == CursorDevice::Touch ? 6 : 5;
	}

	// Controller 用一个原子字发布完整设置：mouse3 | touch3 | screenPen3 | tablet3 | injected1 | valid1。
	inline constexpr std::uint32_t kCursorSettingsValidBit = 1u << 13;

	constexpr std::uint32_t PackCursorSettings(const CursorSettings& s) noexcept
	{
		return static_cast<std::uint32_t>(s.mouse)
			| (static_cast<std::uint32_t>(s.touch) << 3)
			| (static_cast<std::uint32_t>(s.screenPen) << 6)
			| (static_cast<std::uint32_t>(s.tablet) << 9)
			| (s.injectedMouseAsTablet ? 1u << 12 : 0u)
			| kCursorSettingsValidBit;
	}

	constexpr CursorSettings UnpackCursorSettings(std::uint32_t bits) noexcept
	{
		return CursorSettings{
			RestoreCursorStyle(CursorDevice::Mouse, static_cast<int>(bits & 7u)),
			RestoreCursorStyle(CursorDevice::Touch, static_cast<int>((bits >> 3) & 7u)),
			RestoreCursorStyle(CursorDevice::ScreenPen, static_cast<int>((bits >> 6) & 7u)),
			RestoreCursorStyle(CursorDevice::Tablet, static_cast<int>((bits >> 9) & 7u)),
			((bits >> 12) & 1u) != 0 };
	}
}
