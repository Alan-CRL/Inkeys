module;

#include "Setting.Wrap.h"

module Inkeys.UI.Setting;
import :Widgets;

namespace Widgets
{
	namespace
	{
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

}
