#pragma once

#include "imgui/imgui.h"

namespace Inkeys::UI::Setting::Design
{
	struct TypographyMetrics
	{
		float size;
		float lineHeight;
	};
	inline constexpr TypographyMetrics CaptionText{ 12.0F, 16.0F };
	inline constexpr TypographyMetrics BodyText{ 14.0F, 20.0F };
	inline constexpr TypographyMetrics TitleText{ 28.0F, 36.0F };
	inline constexpr float FontReferenceSize = 30.0F;

	[[nodiscard]] inline ImFontConfig HarmonyFontConfig(float rasterizerDensity)
	{
		ImFontConfig config;
		config.OversampleH = 1;
		config.OversampleV = 1;
		config.RasterizerDensity = rasterizerDensity;
		config.FontDataOwnedByAtlas = false;
		// hhea 高 1213 / em 1000；字面校准与固定基线补偿同时设置。
		config.ExtraSizeScale = 1.213F;
		config.GlyphOffset.y = FontReferenceSize * 0.025F;
		return config;
	}

	[[nodiscard]] inline ImFontConfig IconFontConfig(float rasterizerDensity)
	{
		ImFontConfig config;
		config.OversampleH = 1;
		config.OversampleV = 1;
		config.RasterizerDensity = rasterizerDensity;
		config.FontDataOwnedByAtlas = false;
		// 图标单独注册，按字形包围盒放进固定槽位，不沿用正文基线偏移。
		config.PixelSnapH = true;
		return config;
	}
}
