module;

#include <windows.h>

#ifndef RGBA
#define RGBA(r, g, b, a) (COLORREF)(((b) << 16) | ((g) << 8) | (r) | ((a) << 24))
#endif

export module Inkeys.UI.Bar:Theme;

export import Inkeys.UI.UI3;
import Inkeys.UI.Bar.Metrics;
import Inkeys.UI.UI3.Material;

using namespace Inkeys::UI::UI3;

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

static_assert(ThemeMaterial::ResidentBaseFillOpacity == BarMainBarFillOpacity,
	"resident opacity policy and main bar metric must share the Dark baseline");

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

	return ApplyThemeAlpha(color, alpha);
}
