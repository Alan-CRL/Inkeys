#pragma once

#include "imgui/imgui.h"
#include "imgui/misc/freetype/imgui_freetype.h"

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

	enum class TextFontRole { Legacy, Design };

	[[nodiscard]] inline ImFontConfig LegacyFontConfig(float rasterizerDensity)
	{
		ImFontConfig config;
		config.OversampleH = 1;
		config.OversampleV = 1;
		config.RasterizerDensity = rasterizerDensity;
		config.FontDataOwnedByAtlas = false;
		return config;
	}

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

	[[nodiscard]] inline ImFontConfig NotoFontConfig(float rasterizerDensity,
		TextFontRole role = TextFontRole::Design)
	{
		auto config = LegacyFontConfig(rasterizerDensity);
		const bool design = role == TextFontRole::Design;
		// stb 按 hhea 跨度归一化；Noto 的 1448 与 Harmony 的 1213 需分别对齐同一 em。
		config.ExtraSizeScale = design ? 1.448F * TextOpticalScale : 1.448F / 1.213F;
		// ExtraSizeScale 不改变 baked Ascent，按两字体基线比值差作角色内补偿。
		constexpr float baselineDifference = 1160.0F / 1448.0F - 928.0F / 1213.0F;
		config.GlyphOffset.y = FontReferenceSize * ((design ? 0.025F : 0.0F) - baselineDifference);
		return config;
	}

	[[nodiscard]] inline ImFontConfig TextFontConfig(float rasterizerDensity, bool notoSource,
		TextFontRole role, bool notoPrimary)
	{
		const bool design = role == TextFontRole::Design;
		auto config = notoSource ? NotoFontConfig(rasterizerDensity, role)
			: design ? HarmonyFontConfig(rasterizerDensity) : LegacyFontConfig(rasterizerDensity);
		// merged 字形共用主脸 baked Ascent；尺度按源字族，基线偏移必须按主字族。
		config.GlyphOffset.y = notoPrimary ? NotoFontConfig(rasterizerDensity, role).GlyphOffset.y
			: design ? HarmonyFontConfig(rasterizerDensity).GlyphOffset.y : 0.0F;
		return config;
	}

	inline void ConfigureFreeTypeAtlas(ImFontAtlas& atlas)
	{
		atlas.SetFontLoader(ImGuiFreeType::GetFontLoader());
		// 仅对齐基线，避免已hinting字图再次经过分数像素纵向采样；字号与真实字重保持原值。
		atlas.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_SnapAscentToPixelGrid;
		// 设置沿用灰度字形模式；显式启用彩色字形时保留RGBA，不能丢弃颜色通道。
		atlas.TexDesiredFormat = (atlas.FontLoaderFlags & ImGuiFreeTypeLoaderFlags_LoadColor)
			? ImTextureFormat_RGBA32 : ImTextureFormat_Alpha8;
	}

	[[nodiscard]] inline ImFontConfig FreeTypeTextFontConfig(bool notoSource,
		TextFontRole role, bool notoPrimary)
	{
		// 动态字号已经包含窗口倍率；源密度保持1，避免再放大字图后缩小采样。
		auto config = TextFontConfig(1.0F, notoSource, role, notoPrimary);
		config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
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
