module;

#include "Setting.Wrap.h"

module Inkeys.UI.Setting;
import :Widgets;

namespace Widgets
{
	namespace
	{
		void AdvanceToGap(float desiredDip)
		{
			// ImGui 已在上一 item 后加入 ItemSpacing，这里只补足目标视觉距离。
			const float extra = max(0.0F,
				Dip(desiredDip) - ImGui::GetStyle().ItemSpacing.y);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + extra);
		}

		void PopControlStyle(int colorCount, int variableCount = 0)
		{
			if (variableCount > 0) ImGui::PopStyleVar(variableCount);
			if (colorCount > 0) ImGui::PopStyleColor(colorCount);
		}

	}

	void StyleClass::ApplyGlobal(float scrollbarWidth) const
	{
		// ImFluent preset 拥有 Fluent2 的全局间距、圆角和控件尺寸。
		ImGuiStyle& imguiStyle = ImGui::GetStyle();
		imguiStyle.ScrollbarSize = scrollbarWidth * settingGlobalScale;
		imguiStyle.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	}

	ImU32 Color(ImFluentCol color)
	{
		return ImGui::ColorConvertFloat4ToU32(ImFluent::GetStyle().Colors[color]);
	}

	float Dip(float value)
	{
		return value * settingGlobalScale;
	}

	void PageHeader(const char* title, const char* description, bool hero)
	{
		ImFluent::TextBlock(title ? title : "",
			hero ? ImFluentTextStyle_TitleLarge : ImFluentTextStyle_Title);
		if (description && *description)
			ImFluent::TextBlockColored(description, Color(ImFluentCol_TextSecondary),
				ImFluentTextStyle_Body);
	}

	void PageContentStart()
	{
		AdvanceToGap(24.0F);
	}

	void SectionHeader(const char* title)
	{
		AdvanceToGap(24.0F);
		ImFluent::TextBlock(title ? title : "", ImFluentTextStyle_BodyStrong);
		AdvanceToGap(8.0F);
	}

	bool BeginSettingsCard(const char* id, const char* header,
		const char* description, const char* glyph)
	{
		const bool cardOpen = ImFluent::BeginCard(id, { -FLT_MIN, 0.0F },
			ImFluentCardStyle_Filled);
		if (!cardOpen)
		{
			ImFluent::EndCard();
			return false;
		}

		ImGui::PushID(id);
		const ImFluentStyle& fluentStyle = ImFluent::GetStyle();
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float availableWidth = max(0.0F, ImGui::GetContentRegionAvail().x);
		const bool stacked = availableWidth < Dip(480.0F);
		const float glyphWidth = glyph && *glyph ? Dip(24.0F) : 0.0F;
		const float glyphGap = glyphWidth > 0.0F ? Dip(12.0F) : 0.0F;
		const float trailingWidth = stacked ? availableWidth
			: clamp(availableWidth * 0.34F, Dip(120.0F), Dip(240.0F));
		const float trailingGap = stacked ? 0.0F : Dip(16.0F);
		const float textWidth = max(0.0F, availableWidth - glyphWidth
			- glyphGap - (stacked ? 0.0F : trailingWidth + trailingGap));
		const float textX = start.x + glyphWidth + glyphGap;

		if (glyphWidth > 0.0F)
		{
			ImFluent::PushFont(ImFluentTextStyle_Body);
			ImGui::SetCursorScreenPos(start);
			ImGui::TextUnformatted(glyph);
			ImFluent::PopFont();
		}

		ImGui::SetCursorScreenPos({ textX, start.y });
		ImGui::BeginGroup();
		if (header && *header)
		{
			ImFluent::PushFont(ImFluentTextStyle_Body);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
			ImGui::TextWrapped("%s", header);
			ImGui::PopTextWrapPos();
			ImFluent::PopFont();
		}
		if (description && *description)
		{
			ImFluent::PushFont(ImFluentTextStyle_Caption);
			ImGui::PushStyleColor(ImGuiCol_Text, Color(ImFluentCol_TextSecondary));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
			ImGui::TextWrapped("%s", description);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImFluent::PopFont();
		}
		ImGui::EndGroup();

		const float textBottom = ImGui::GetCursorScreenPos().y;
		const float controlHeight = Dip(fluentStyle.ControlHeight);
		const float controlX = stacked ? textX
			: start.x + availableWidth - trailingWidth;
		const float controlY = stacked ? textBottom + Dip(8.0F)
			: start.y + max(0.0F, (textBottom - start.y - controlHeight) * 0.5F);
		ImGui::SetCursorScreenPos({ controlX, controlY });
		ImGui::PushItemWidth(stacked
			? max(0.0F, availableWidth - (textX - start.x)) : trailingWidth);
		return true;
	}

	void EndSettingsCard()
	{
		ImGui::PopItemWidth();
		ImGui::PopID();
		ImFluent::EndCard();
	}
	StyleClass style;

	bool ButtonClass::TitleBar(const char* label, const ImVec2& size, bool critical) const
	{
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(
			critical ? ImFluentCol_SystemFillCritical : ImFluentCol_SubtleFillSecondary));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(
			critical ? ImFluentCol_SystemFillCritical : ImFluentCol_SubtleFillTertiary));
		ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, Color(
			critical ? ImFluentCol_TextOnAccentPrimary : ImFluentCol_TextPrimary));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

		const bool clicked = ImGui::Button(label, size);
		PopControlStyle(5, 1);
		return clicked;
	}
	ButtonClass button;

	bool ComboClass::Select(const char* label, int* currentItem, const vector<string>& items) const
	{
		if (!currentItem || items.empty()) return false;
		vector<const char*> itemViews;
		itemViews.reserve(items.size());
		for (const auto& item : items) itemViews.push_back(item.c_str());
		return ImFluent::ComboBox(label, currentItem, itemViews.data(),
			static_cast<int>(itemViews.size()));
	}
	ComboClass combo;
}
