#include "Setting.Controls.h"
#include "imgui/imgui_internal.h"
#include <cstring>
#include <string_view>
#include <vector>

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
		TypographyMetrics Metrics(ImFluentTextStyle style)
		{
			switch (style)
			{
			case ImFluentTextStyle_Caption: return CaptionText;
			case ImFluentTextStyle_Title: return TitleText;
			case ImFluentTextStyle_Subtitle: return { 20.0F, 28.0F };
			default: return BodyText;
			}
		}

		std::vector<std::string_view> Lines(const char* text, float widthPixels)
		{
			std::vector<std::string_view> lines;
			if (!text || !*text) return lines;
			const char* cursor = text;
			const char* end = text + std::strlen(text);
			while (cursor < end)
			{
				const char* paragraphEnd = (std::find)(cursor, end, '\n');
				const char* lineEnd = ImGui::GetFont()->CalcWordWrapPosition(
					ImGui::GetFontSize(), cursor, paragraphEnd, (std::max)(1.0F, widthPixels));
				if (lineEnd == cursor && cursor < paragraphEnd)
				{
					// 最窄可用宽度下仍按完整 UTF-8 字符前进，避免拆出半个汉字。
					unsigned int codepoint;
					lineEnd += (std::max)(1, ImTextCharFromUtf8(&codepoint, cursor, paragraphEnd));
				}
				lines.emplace_back(cursor, lineEnd - cursor);
				cursor = lineEnd;
				if (cursor == paragraphEnd && cursor < end) ++cursor;
				else while (cursor < end && (*cursor == ' ' || *cursor == '\t')) ++cursor;
			}
			return lines;
		}
	}

	float Pixels(float dip) { return dip * ImGui::GetStyle().FontScaleDpi; }

	float TextHeight(const char* text, float widthPixels, ImFluentTextStyle style)
	{
		ImFluent::PushFont(style);
		const float height = static_cast<float>(Lines(text, widthPixels).size()) * Pixels(Metrics(style).lineHeight);
		ImFluent::PopFont();
		return height;
	}

	float TextWidth(const char* text, ImFluentTextStyle style)
	{
		ImFluent::PushFont(style);
		const float width = ImGui::CalcTextSize(text ? text : "").x;
		ImFluent::PopFont();
		return width;
	}

	void TextAt(const char* text, ImVec2 position, float widthPixels,
		ImFluentTextStyle style, ImU32 color)
	{
		ImFluent::PushFont(style);
		const auto lines = Lines(text, widthPixels);
		const float lineHeight = Pixels(Metrics(style).lineHeight);
		position.y += (lineHeight - ImGui::GetFontSize()) * 0.5F;
		for (const auto line : lines)
		{
			ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
				{ std::round(position.x), std::round(position.y) }, color,
				line.data(), line.data() + line.size());
			position.y += lineHeight;
		}
		ImFluent::PopFont();
	}

	void Text(const char* text, ImFluentTextStyle style, ImU32 color)
	{
		const ImVec2 position = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = TextHeight(text, width, style);
		TextAt(text, position, width, style, color);
		ImGui::Dummy({ width, height });
	}

	void PageHeader(const char* title, const char* description)
	{
		Text(title, ImFluentTextStyle_Title);
		if (description && *description)
			Text(description, ImFluentTextStyle_Body, TextSecondary);
	}

	void SectionHeader(const char* title)
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(SectionGap) - ImGui::GetStyle().ItemSpacing.y);
		Text(title, ImFluentTextStyle_BodyStrong);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(8.0F) - ImGui::GetStyle().ItemSpacing.y);
	}

	float ButtonWidth(const char* label)
	{
		return (std::max)(72.0F, TextWidth(label) / Pixels(1.0F) + 32.0F);
	}

	void SettingRow(const char* id, const char* title, const char* description,
		const char* glyph, float actionWidthDip, float actionHeightDip,
		const std::function<void(const LayoutRect&)>& action)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const auto measure = MeasureRow(width / Pixels(1.0F), actionWidthDip,
			actionHeightDip, glyph && *glyph);
		const float titleHeight = TextHeight(title, Pixels(measure.textWidth));
		const float descriptionHeight = TextHeight(description, Pixels(measure.textWidth), ImFluentTextStyle_Caption);
		const auto row = ResolveRowLayout(measure,
			(titleHeight + descriptionHeight) / Pixels(1.0F));
		// 整行占位同时登记尾部间隙，最后一行后直接 EndChild 也不会越过内容边界。
		ImGui::Dummy({ width, Pixels(row.height + RowGap) });
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 bottom(origin.x + width, origin.y + Pixels(row.height));
		draw->AddRectFilled(origin, bottom, Card, Pixels(4.0F));
		draw->AddRect(origin, bottom, Stroke, Pixels(4.0F));
		TextAt(title, { origin.x + Pixels(row.text.left), origin.y + Pixels(row.text.top) },
			Pixels(row.text.Width()));
		TextAt(description, { origin.x + Pixels(row.text.left), origin.y + Pixels(row.text.top) + titleHeight },
			Pixels(row.text.Width()), ImFluentTextStyle_Caption, TextSecondary);
		if (measure.hasIcon)
			ImFluent::DrawIcon(glyph,
				{ origin.x + Pixels(row.icon.left), origin.y + Pixels(row.icon.top) },
				{ origin.x + Pixels(row.icon.right), origin.y + Pixels(row.icon.bottom) }, 20.0F, TextPrimary);
		const LayoutRect actionPixels{ origin.x + Pixels(row.action.left), origin.y + Pixels(row.action.top),
			origin.x + Pixels(row.action.right), origin.y + Pixels(row.action.bottom) };
		ImGui::PushID(id);
		ImGui::SetCursorScreenPos({ actionPixels.left, actionPixels.top });
		ImGui::PushItemWidth((std::max)(1.0F, actionPixels.Width()));
		action(actionPixels);
		ImGui::PopItemWidth();
		ImGui::PopID();
		// ItemSize 会截到物理像素；结束游标使用同一取整，避免分数 DPI 多出半像素。
		ImGui::SetCursorScreenPos({ origin.x, std::floor(bottom.y + Pixels(RowGap)) });
	}

	bool ToggleAction(const LayoutRect& bounds, bool& value)
	{
		// 状态文本由页面传递到行左侧时也能保持开关末端在同一条轴上。
		ImGui::SetCursorScreenPos({ bounds.right - Pixels(40.0F), bounds.top + (bounds.Height() - Pixels(20.0F)) * 0.5F });
		return ImFluent::ToggleSwitch("##toggle", &value, "", "");
	}

	bool SliderAction(const LayoutRect& bounds, float& value, float minimum,
		float maximum, bool& active)
	{
		char percentage[24];
		ImFormatString(percentage, IM_ARRAYSIZE(percentage), "%d%%", static_cast<int>(std::round(value * 100.0F)));
		const float numberWidth = Pixels(40.0F);
		TextAt(percentage, { bounds.left, bounds.top + Pixels(6.0F) }, numberWidth);
		ImGui::SetCursorScreenPos({ bounds.left + numberWidth + Pixels(12.0F), bounds.top });
		ImGui::SetNextItemWidth((std::max)(Pixels(24.0F), bounds.Width() - numberWidth - Pixels(12.0F)));
		const bool changed = ImFluent::Slider("##slider", &value, minimum, maximum, "%.2f");
		active = ImGui::IsItemActive();
		value = std::round(value * 100.0F) / 100.0F;
		return changed;
	}

	void ApplyPalette()
	{
		auto& style = ImFluent::GetStyle();
		style.NavItemHeight = 36.0F;
		style.NavPaneOpenWidth = OpenPaneWidth;
		style.NavPaneCompactWidth = CompactPaneWidth;
		style.ControlHeight = 32.0F;
		style.ControlCornerRadius = 4.0F;
		style.ToggleSwitchWidth = 40.0F;
		style.ToggleSwitchHeight = 20.0F;
		style.Colors[ImFluentCol_TextPrimary] = ImGui::ColorConvertU32ToFloat4(TextPrimary);
		style.Colors[ImFluentCol_TextSecondary] = ImGui::ColorConvertU32ToFloat4(TextSecondary);
		style.Colors[ImFluentCol_CardBgDefault] = ImGui::ColorConvertU32ToFloat4(Card);
		style.Colors[ImFluentCol_CardStrokeDefault] = ImGui::ColorConvertU32ToFloat4(Stroke);
		style.Colors[ImFluentCol_SubtleFillSecondary] = ImGui::ColorConvertU32ToFloat4(IM_COL32(231, 231, 231, 255));
		ImFluent::SetAccentColor(ImColor(Accent));
	}
}
