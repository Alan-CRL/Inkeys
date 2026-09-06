#pragma once

#include "Setting.Design.h"
#include "Setting.Typography.h"
#include "imfluent/imfluent.h"
#include <functional>

namespace Inkeys::UI::Setting::Design
{
	inline constexpr ImU32 AppBase = IM_COL32(243, 243, 243, 255);
	inline constexpr ImU32 PageSurface = IM_COL32(250, 250, 250, 255);
	inline constexpr ImU32 Card = IM_COL32(255, 255, 255, 255);
	inline constexpr ImU32 Stroke = IM_COL32(229, 229, 229, 255);
	inline constexpr ImU32 TextPrimary = IM_COL32(32, 32, 32, 255);
	inline constexpr ImU32 TextSecondary = IM_COL32(97, 97, 97, 255);
	inline constexpr ImU32 Accent = IM_COL32(0, 103, 192, 255);

	float Pixels(float dip);
	float TextHeight(const char* text, float widthPixels,
		ImFluentTextStyle style = ImFluentTextStyle_Body);
	float TextWidth(const char* text, ImFluentTextStyle style = ImFluentTextStyle_Body);
	void TextAt(const char* text, ImVec2 position, float widthPixels,
		ImFluentTextStyle style = ImFluentTextStyle_Body, ImU32 color = TextPrimary);
	void Text(const char* text, ImFluentTextStyle style = ImFluentTextStyle_Body,
		ImU32 color = TextPrimary);
	void PageHeader(const char* title, const char* description);
	void SectionHeader(const char* title);
	float ButtonWidth(const char* label);
	void SettingRow(const char* id, const char* title, const char* description,
		const char* glyph, float actionWidthDip, float actionHeightDip,
		const std::function<void(const LayoutRect&)>& action);
	bool ToggleAction(const LayoutRect& bounds, bool& value);
	bool SliderAction(const LayoutRect& bounds, float& value, float minimum,
		float maximum, bool& active);
	void ApplyPalette();
}
