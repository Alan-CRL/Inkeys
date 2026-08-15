module;

#include <windows.h>

#ifndef RGBA
#define RGBA(r, g, b, a) (COLORREF)(((b) << 16) | ((g) << 8) | (r) | ((a) << 24))
#endif

export module Inkeys.UI.Bar:Theme;

import :State;

// Bar 主题模式，当前先只区分深浅色。
export enum class BarThemeModeEnum : int
{
	Dark,
	Light,
};

// Bar 常用角色色，避免绘制逻辑里散落具体 RGB。
export enum class BarThemeColorEnum : int
{
	Surface,
	SurfaceFrame,
	TextPrimary,
	Accent,
	PressedFill,
	SubtleFill,
	SwatchFrame,
	DockTarget,
};

// 画笔颜色预设，保持当前色块顺序和值不变。
export enum class BarThemePresetColorEnum : int
{
	ColorSelect1,
	ColorSelect2,
	ColorSelect3,
	ColorSelect4,
	ColorSelect5,
	ColorSelect6,
	ColorSelect7,
	ColorSelect8,
	ColorSelect9,
	ColorSelect10,
	ColorSelect11,
};

COLORREF ApplyBarThemeAlpha(COLORREF color, int alpha)
{
	if (alpha < 0) alpha = 0;
	if (alpha > 255) alpha = 255;

	return RGBA(GetRValue(color), GetGValue(color), GetBValue(color), alpha);
}

COLORREF GetBarThemeBaseColor(BarThemeModeEnum mode, BarThemeColorEnum color)
{
	if (mode == BarThemeModeEnum::Light)
	{
		switch (color)
		{
		case BarThemeColorEnum::Surface: return RGB(247, 248, 250);
		case BarThemeColorEnum::SurfaceFrame: return RGB(0, 0, 0);
		case BarThemeColorEnum::TextPrimary: return RGB(27, 27, 27);
		case BarThemeColorEnum::Accent: return RGB(0, 140, 105);
		case BarThemeColorEnum::PressedFill: return RGB(64, 72, 80);
		case BarThemeColorEnum::SubtleFill: return RGB(0, 140, 105);
		case BarThemeColorEnum::SwatchFrame: return RGB(176, 176, 176);
		case BarThemeColorEnum::DockTarget: return RGB(0, 120, 212);
		default: return RGB(27, 27, 27);
		}
	}

	switch (color)
	{
	case BarThemeColorEnum::Surface: return RGB(24, 24, 24);
	case BarThemeColorEnum::SurfaceFrame: return RGB(255, 255, 255);
	case BarThemeColorEnum::TextPrimary: return RGB(255, 255, 255);
	case BarThemeColorEnum::Accent: return RGB(88, 255, 236);
	case BarThemeColorEnum::PressedFill: return RGB(127, 127, 127);
	case BarThemeColorEnum::SubtleFill: return RGB(127, 127, 127);
	case BarThemeColorEnum::SwatchFrame: return RGB(80, 80, 80);
	case BarThemeColorEnum::DockTarget: return RGB(76, 158, 255);
	default: return RGB(255, 255, 255);
	}
}

BarStyleClass* currentBarThemeStyle = nullptr;

export BarThemeModeEnum GetThemeMode(bool darkStyle)
{
	return darkStyle ? BarThemeModeEnum::Dark : BarThemeModeEnum::Light;
}

void SetThemeStyleSource(BarStyleClass* style)
{
	currentBarThemeStyle = style;
}

export COLORREF GetThemeColor(BarThemeModeEnum mode, BarThemeColorEnum color, int alpha = 255)
{
	return ApplyBarThemeAlpha(GetBarThemeBaseColor(mode, color), alpha);
}

export COLORREF GetThemeColor(bool darkStyle, BarThemeColorEnum color, int alpha = 255)
{
	return GetThemeColor(GetThemeMode(darkStyle), color, alpha);
}

export COLORREF GetThemeColor(BarThemeColorEnum color, int alpha = 255)
{
	// 默认浅色；绑定样式后由当前 Bar 状态实时决定。
	if (!currentBarThemeStyle) return GetThemeColor(BarThemeModeEnum::Light, color, alpha);

	return GetThemeColor(currentBarThemeStyle->darkStyle, color, alpha);
}

export COLORREF GetPresetColor(BarThemePresetColorEnum preset, int alpha = 255)
{
	COLORREF color = RGB(255, 255, 255);

	switch (preset)
	{
	case BarThemePresetColorEnum::ColorSelect1: color = RGB(255, 255, 255); break;
	case BarThemePresetColorEnum::ColorSelect2: color = RGB(0, 0, 0); break;
	case BarThemePresetColorEnum::ColorSelect3: color = RGB(255, 139, 0); break;
	case BarThemePresetColorEnum::ColorSelect4: color = RGB(50, 30, 181); break;
	case BarThemePresetColorEnum::ColorSelect5: color = RGB(255, 197, 16); break;
	case BarThemePresetColorEnum::ColorSelect6: color = RGB(255, 16, 0); break;
	case BarThemePresetColorEnum::ColorSelect7: color = RGB(78, 161, 183); break;
	case BarThemePresetColorEnum::ColorSelect8: color = RGB(50, 110, 217); break;
	case BarThemePresetColorEnum::ColorSelect9: color = RGB(102, 213, 82); break;
	case BarThemePresetColorEnum::ColorSelect10: color = RGB(48, 108, 0); break;
	case BarThemePresetColorEnum::ColorSelect11: color = RGB(255, 30, 207); break;
	default: break;
	}

	return ApplyBarThemeAlpha(color, alpha);
}
