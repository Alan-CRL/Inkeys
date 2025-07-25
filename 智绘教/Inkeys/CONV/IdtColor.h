#pragma once

#include "../UI/IdtBar.h"

class IdtColor
{
private:
	IdtColor() = delete;

public:
	static BarLogaColorSchemeEnum BarLogaColorSchemeCalc(COLORREF color)
	{
		// 内联相对亮度计算
		auto getLum = [](int r, int g, int b) -> double {
			auto comp = [](int x) -> double {
				double v = x / 255.0;
				return (v <= 0.03928) ? (v / 12.92) : pow((v + 0.055) / 1.055, 2.4);
				};
			return 0.2126 * comp(r) + 0.7152 * comp(g) + 0.0722 * comp(b);
			};

		// 获取输入颜色的RGB
		int r = GetRValue(color);
		int g = GetGValue(color);
		int b = GetBValue(color);

		double lum_fg = getLum(r, g, b);
		double lum_bg1 = getLum(120, 120, 120);
		double lum_bg2 = getLum(160, 160, 160);

		// 内联对比度计算
		auto getContrast = [](double l1, double l2) -> double {
			if (l1 < l2) std::swap(l1, l2);
			return (l1 + 0.05) / (l2 + 0.05);
			};

		double contrast1 = getContrast(lum_fg, lum_bg1);
		double contrast2 = getContrast(lum_fg, lum_bg2);

		return (contrast1 >= contrast2) ? BarLogaColorSchemeEnum::Slate : BarLogaColorSchemeEnum::Default;
	}

	static D2D1::ColorF ConvertToD2dColor(COLORREF color, double pct = 1.0)
	{
		return D2D1::ColorF(
			GetRValue(color) / 255.0f,
			GetGValue(color) / 255.0f,
			GetBValue(color) / 255.0f,
			static_cast<FLOAT>(pct)
		);
	}
};
