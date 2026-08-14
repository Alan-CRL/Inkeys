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
		auto color = [](ImFluentCol index)
			{ return ImGui::ColorConvertFloat4ToU32(ImFluent::GetStyle().Colors[index]); };
		FluentColor::White = color(ImFluentCol_LayerFillAlt);
		FluentColor::WindowBackground = color(ImFluentCol_SolidBgBase);
		FluentColor::CardBackground = color(ImFluentCol_CardBgDefault);
		FluentColor::PopupBackground = color(ImFluentCol_LayerFillAlt);
		FluentColor::Divider = color(ImFluentCol_DividerStrokeDefault);
		FluentColor::WindowBorder = color(ImFluentCol_SurfaceStrokeDefault);
		FluentColor::ControlStroke = color(ImFluentCol_ControlStrokeDefault);
		FluentColor::TextStrong = color(ImFluentCol_TextPrimary);
		FluentColor::TextPrimary = color(ImFluentCol_TextPrimary);
		FluentColor::TextSecondary = color(ImFluentCol_TextSecondary);
		FluentColor::TextDisabled = color(ImFluentCol_TextDisabled);
		FluentColor::TextOnAccent = color(ImFluentCol_TextOnAccentPrimary);
		FluentColor::Accent = color(ImFluentCol_AccentFillDefault);
		FluentColor::AccentText = color(ImFluentCol_AccentTextPrimary);
		FluentColor::AccentHovered = color(ImFluentCol_AccentFillSecondary);
		FluentColor::AccentPressed = color(ImFluentCol_AccentFillTertiary);
		FluentColor::Danger = color(ImFluentCol_SystemFillCritical);
		FluentColor::WarningBackground = color(ImFluentCol_CardBgDefault);
		FluentColor::WarningText = color(ImFluentCol_SystemFillCaution);
		FluentColor::DangerBackground = color(ImFluentCol_CardBgDefault);
		FluentColor::SuccessBackground = color(ImFluentCol_CardBgDefault);
		FluentColor::SuccessText = color(ImFluentCol_SystemFillSuccess);
		FluentColor::ControlFill = color(ImFluentCol_ControlFillDefault);
		FluentColor::ControlFillHovered = color(ImFluentCol_ControlFillSecondary);
		FluentColor::ControlFillPressed = color(ImFluentCol_ControlFillTertiary);
		FluentColor::SubtleFill = color(ImFluentCol_SubtleFillSecondary);
		FluentColor::SubtleFillPressed = color(ImFluentCol_SubtleFillTertiary);
		// ImFluent preset 拥有 Fluent2 的全局间距、圆角和控件尺寸。
		ImGuiStyle& imguiStyle = ImGui::GetStyle();
		imguiStyle.ScrollbarSize = scrollbarWidth * settingGlobalScale;
		imguiStyle.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	}
	StyleClass style;

	bool ToggleClass::ToggleBool(const char* label, bool* state) const
	{
		if (!state) return false;
		return ImFluent::ToggleSwitch(label, state, "", "");
	}
	ToggleClass toggle;

	bool ButtonClass::Standard(const char* label, const ImVec2& size, ImU32 textColor) const
	{
		if (textColor != FluentColor::TextPrimary)
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		const bool clicked = ImFluent::Button(label, size);
		if (textColor != FluentColor::TextPrimary) ImGui::PopStyleColor();
		return clicked;
	}

	bool ButtonClass::AccentToggle(const char* label, const ImVec2& size, bool selected) const
	{
		return selected
			? ImFluent::AccentButton(label, size)
			: ImFluent::Button(label, size);
	}

	bool ButtonClass::TitleBarClose(const char* label, const ImVec2& size) const
	{
		ImGui::PushStyleColor(ImGuiCol_Button, FluentColor::Transparent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::Danger);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, FluentColor::DangerPressed);
		ImGui::PushStyleColor(ImGuiCol_Border, FluentColor::Transparent);
		ImGui::PushStyleColor(ImGuiCol_Text, FluentColor::TextPrimary);
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

	bool SliderClass::Float(const char* label, float* value, float minValue, float maxValue, const char* format) const
	{
		return ImFluent::Slider(label, value, minValue, maxValue,
			format && *format ? format : "%.2f");
	}

	bool SliderClass::Int(const char* label, int* value, int minValue, int maxValue, const char* format) const
	{
		return ImFluent::SliderInt(label, value, minValue, maxValue,
			format && *format ? format : "%d");
	}
	SliderClass slider;

}
