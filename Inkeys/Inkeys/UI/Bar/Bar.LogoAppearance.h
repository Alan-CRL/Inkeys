#pragma once

#include "Bar.ThemeMaterial.h"
#include <string>

// 主按钮专属外观快照：普通 SVG 不参与这套内部属性加工。
namespace BarLogoAppearance
{
	using Color = BarThemeMaterial::Color;
	struct Snapshot
	{
		Color penFill = 0, screenFill = 0, indicatorColor = 0;
		double screenOpacity = 0.0, indicatorOpacity = 0.0, outlineOpacity = 0.0;
		bool operator==(const Snapshot&) const = default;
	};

	constexpr bool IsDrawingMode(bool pen, bool shape) noexcept { return pen || shape; }

	// 浅色屏幕为 9% 中性黑，替代图标灰 0.22 的灰块；深色端保持 0.22。
	inline constexpr double DarkScreenOpacity = 0.22;
	inline constexpr double LightScreenOpacity = 0.09;
	// 外轮廓只给低对比笔色：对表面 ≥3:1 时为 0（深色笔不描边），接近 1:1（白、浅黄）时最高 0.55。
	inline constexpr double LightOutlineMaxOpacity = 0.55;

	inline double LightOutlineOpacity(Color lightPenFill) noexcept
	{
		using namespace BarThemeMaterial;
		const double contrast = ContrastRatio(lightPenFill, LightColor(ColorRole::Surface));
		return std::clamp((3.0 - contrast) / 2.0, 0.0, 1.0) * LightOutlineMaxOpacity;
	}

	inline Snapshot Resolve(double lightWeight, double drawingWeight, Color actualPenColor) noexcept
	{
		using namespace BarThemeMaterial;
		const double light = ClampWeight(lightWeight);
		const double drawing = ClampWeight(drawingWeight);
		const double ink = drawing * (1.0 - light);
		// 深色保留白色笔底及原渐变；浅色才把真实 RGB 填入整笔。
		// 浅色非绘制态笔身使用图标色；屏幕为 9% 中性黑，只作衬底不抢笔身。
		const Color lightPenFill = MixColor(LightColor(ColorRole::IconPrimary), actualPenColor, drawing);
		return {
			MixColor(Rgb(255, 255, 255), lightPenFill, light),
			MixColor(Rgb(255, 255, 255), Rgb(0, 0, 0), light),
			ink > 0.0 ? actualPenColor & 0x00FFFFFFU : 0U,
			Mix(DarkScreenOpacity, LightScreenOpacity, light), ink,
			light * LightOutlineOpacity(lightPenFill)
		};
	}

	template<class Setter>
	void ApplyAttributes(const Snapshot& appearance, Setter&& set)
	{
		auto rgb = [](Color color) {
			return "rgb(" + std::to_string(color & 255U) + ","
				+ std::to_string((color >> 8) & 255U) + ","
				+ std::to_string((color >> 16) & 255U) + ")";
		};
		const auto pen = rgb(appearance.penFill);
		const auto screen = rgb(appearance.screenFill);
		const auto ink = rgb(appearance.indicatorColor);
		for (const char* id : { "pen-cap", "pen-barrel", "pen-tip", "pen-nib" })
			set(id, "fill", pen);
		for (const char* id : { "screen-1", "screen-2" })
		{
			set(id, "fill", screen);
			set(id, "fill-opacity", std::to_string(appearance.screenOpacity));
		}
		set("pen-outline", "stroke-opacity", std::to_string(appearance.outlineOpacity));
		set("ink-indicator", "opacity", std::to_string(appearance.indicatorOpacity));
		set("ink-nib", "fill", ink);
		set("ink-highlight", "fill", ink);
		for (int i = 0; i < 14; ++i)
		{
			const auto id = "ink-stop-" + std::to_string(i);
			set(id.c_str(), "stop-color", ink);
		}
	}
}
