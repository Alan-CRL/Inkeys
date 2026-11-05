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

	// 数值同时作为设备掩码；鼠标和触摸不参与落笔隐藏。
	enum class DotCursorHideMode : std::uint8_t { Off = 0, ScreenPen = 1, Tablet = 2, Both = 3 };

	constexpr DotCursorHideMode RestoreDotCursorHideMode(int value) noexcept
	{
		return value >= 0 && value <= 3 ? static_cast<DotCursorHideMode>(value) : DotCursorHideMode::ScreenPen;
	}

	// 只保存可选视觉；系统触摸光标始终隐藏，不进入用户配置。
	struct CursorSettings
	{
		CursorStyle mouse = CursorStyle::Arrow;
		CursorStyle touch = CursorStyle::None;
		CursorStyle screenPen = CursorStyle::Dot;
		CursorStyle tablet = CursorStyle::HighlightDot;
		// 非 Windows Ink 数位板以“驱动注入的鼠标”到达；开启后按数位板行处理。
		bool injectedMouseAsTablet = false;
		DotCursorHideMode dotHideOnContact = DotCursorHideMode::ScreenPen;
		int penOpacity = 50;
		int highlightPenFillOpacity = 50;
		int dotOpacity = 25;
		int highlightDotFillOpacity = 50;
		friend bool operator==(const CursorSettings&, const CursorSettings&) = default;
	};

	// 光标设置页预览与实际渲染共用的外观常量，避免两处漂移。
	inline constexpr float kCursorPenIconDip = 24.0f;
	// 按原生 192 DPI 可见 25×39 px 校准；参考轮廓最右为盒宽的 95%，其形状保持不变。
	inline constexpr float kCursorArrowWidthDip = 13.0f;
	inline constexpr float kCursorArrowHeightDip = 19.5f;
	inline constexpr float kCursorArrowEdgeDip = 0.5f;

	// 箭头使用细黑边；低 DPI 仍保留至少一个物理像素，其它样式的描边不经过这里。
	constexpr float CursorArrowOutlineWidthPx(float scale) noexcept
	{
		const float scaledWidth = kCursorArrowEdgeDip * scale;
		return scaledWidth > 1.0f ? scaledWidth : 1.0f;
	}
	inline constexpr float kCursorHighlightEdgeDip = 1.25f;
	inline constexpr float kCursorPlainEdgeDip = 0.5f;
	inline constexpr float kCursorHighlightOuterEdgePx = 1.0f;

	constexpr float CursorHighlightDotEdgeWidthPx(float diameter, float scale) noexcept
	{
		const float scaledMaximum = kCursorHighlightEdgeDip * scale;
		const float proportional = diameter / 8.0f;
		return proportional < kCursorHighlightOuterEdgePx ? kCursorHighlightOuterEdgePx
			: proportional > scaledMaximum ? scaledMaximum : proportional;
	}

	constexpr bool IsPenCursorStyle(CursorStyle style) noexcept
	{
		return style == CursorStyle::Pen || style == CursorStyle::HighlightPen;
	}

	constexpr bool IsDotCursorStyle(CursorStyle style) noexcept
	{
		return style == CursorStyle::Dot || style == CursorStyle::HighlightDot;
	}

	constexpr bool ShouldHideDotCursorOnContact(CursorDevice device, CursorStyle style, DotCursorHideMode mode) noexcept
	{
		const auto mask = device == CursorDevice::ScreenPen ? 1u : device == CursorDevice::Tablet ? 2u : 0u;
		return IsDotCursorStyle(style) && (static_cast<unsigned>(mode) & mask) != 0;
	}

	// 高亮变体使用固定高亮描边；其余使用普通描边（鼠标系统光标/箭头不使用该描边）。
	constexpr bool IsHighlightedCursorStyle(CursorStyle style) noexcept
	{
		return style == CursorStyle::HighlightPen || style == CursorStyle::HighlightDot;
	}

	constexpr int DefaultCursorOpacityPercent(CursorStyle style) noexcept
	{
		if (style == CursorStyle::Dot) return 25;
		if (style == CursorStyle::Pen || IsHighlightedCursorStyle(style)) return 50;
		return 100;
	}

	constexpr int NormalizeCursorOpacityPercent(CursorStyle style, int value) noexcept
	{
		return value >= 25 && value <= 100 ? value : DefaultCursorOpacityPercent(style);
	}

	constexpr int CursorOpacityPercent(const CursorSettings& settings, CursorStyle style) noexcept
	{
		const int value = style == CursorStyle::Pen ? settings.penOpacity
			: style == CursorStyle::HighlightPen ? settings.highlightPenFillOpacity
			: style == CursorStyle::Dot ? settings.dotOpacity
			: style == CursorStyle::HighlightDot ? settings.highlightDotFillOpacity
			: DefaultCursorOpacityPercent(style);
		return NormalizeCursorOpacityPercent(style, value);
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

	// 低位保持设备样式合同；bit16 起以四个 7-bit 字段保存 Pen/HighlightPen/Dot/HighlightDot 百分比。
	inline constexpr std::uint64_t kCursorSettingsValidBit = std::uint64_t{ 1 } << 13;

	constexpr std::uint64_t PackCursorSettings(const CursorSettings& s) noexcept
	{
		return static_cast<std::uint64_t>(s.mouse)
			| (static_cast<std::uint64_t>(s.touch) << 3)
			| (static_cast<std::uint64_t>(s.screenPen) << 6)
			| (static_cast<std::uint64_t>(s.tablet) << 9)
			| (s.injectedMouseAsTablet ? std::uint64_t{ 1 } << 12 : 0u)
			| (static_cast<std::uint64_t>(s.dotHideOnContact) << 14)
			| (static_cast<std::uint64_t>(NormalizeCursorOpacityPercent(CursorStyle::Pen, s.penOpacity)) << 16)
			| (static_cast<std::uint64_t>(NormalizeCursorOpacityPercent(CursorStyle::HighlightPen, s.highlightPenFillOpacity)) << 23)
			| (static_cast<std::uint64_t>(NormalizeCursorOpacityPercent(CursorStyle::Dot, s.dotOpacity)) << 30)
			| (static_cast<std::uint64_t>(NormalizeCursorOpacityPercent(CursorStyle::HighlightDot, s.highlightDotFillOpacity)) << 37)
			| kCursorSettingsValidBit;
	}

	constexpr CursorSettings UnpackCursorSettings(std::uint64_t bits) noexcept
	{
		return CursorSettings{
			RestoreCursorStyle(CursorDevice::Mouse, static_cast<int>(bits & 7u)),
			RestoreCursorStyle(CursorDevice::Touch, static_cast<int>((bits >> 3) & 7u)),
			RestoreCursorStyle(CursorDevice::ScreenPen, static_cast<int>((bits >> 6) & 7u)),
			RestoreCursorStyle(CursorDevice::Tablet, static_cast<int>((bits >> 9) & 7u)),
			((bits >> 12) & 1u) != 0,
			RestoreDotCursorHideMode(static_cast<int>((bits >> 14) & 3u)),
			NormalizeCursorOpacityPercent(CursorStyle::Pen, static_cast<int>((bits >> 16) & 0x7fu)),
			NormalizeCursorOpacityPercent(CursorStyle::HighlightPen, static_cast<int>((bits >> 23) & 0x7fu)),
			NormalizeCursorOpacityPercent(CursorStyle::Dot, static_cast<int>((bits >> 30) & 0x7fu)),
			NormalizeCursorOpacityPercent(CursorStyle::HighlightDot, static_cast<int>((bits >> 37) & 0x7fu)) };
	}
}
