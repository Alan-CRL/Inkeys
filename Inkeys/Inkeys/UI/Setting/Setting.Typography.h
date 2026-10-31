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
	inline constexpr TypographyMetrics ControlText{ 13.0F, 20.0F };
	inline constexpr float FontReferenceSize = 30.0F;
	inline constexpr float TextOpticalScale = 0.97F;

	[[nodiscard]] inline ImFontConfig HarmonyFontConfig(float rasterizerDensity)
	{
		ImFontConfig config;
		config.OversampleH = 1;
		config.OversampleV = 1;
		config.RasterizerDensity = rasterizerDensity;
		config.FontDataOwnedByAtlas = false;
		// 保留语义字号与行框；按实际窗口观感小幅收敛字面，控件另用 ControlText。
		config.ExtraSizeScale = 1.213F * TextOpticalScale;
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
