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

		void PushStandardControlColors(ImU32 textColor)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, FluentColor::ControlFill);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::ControlFillHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, FluentColor::ControlFillPressed);
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
			ImGui::PushStyleColor(ImGuiCol_Border, FluentColor::ControlStroke);
		}

		void PushComboColors()
		{
			ImGui::PushStyleColor(ImGuiCol_Border, FluentColor::ControlStroke);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, FluentColor::ControlFill);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, FluentColor::ControlFillHovered);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, FluentColor::ControlFillPressed);
			ImGui::PushStyleColor(ImGuiCol_PopupBg, FluentColor::PopupBackground);
			ImGui::PushStyleColor(ImGuiCol_Button, FluentColor::PopupBackground);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::ControlFillHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, FluentColor::ControlFillPressed);
			ImGui::PushStyleColor(ImGuiCol_Text, FluentColor::TextStrong);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * settingGlobalScale, 8.0f * settingGlobalScale));
		}

		void PushSliderColors()
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, FluentColor::ControlFill);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, FluentColor::ControlFillHovered);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, FluentColor::ControlFillPressed);
			ImGui::PushStyleColor(ImGuiCol_SliderGrab, FluentColor::Accent);
			ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, FluentColor::AccentHovered);
			ImGui::PushStyleColor(ImGuiCol_Border, FluentColor::ControlStroke);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f * settingGlobalScale));
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
		ImGuiStyle& imguiStyle = ImGui::GetStyle();
		imguiStyle.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(FluentColor::WindowBackground);
		imguiStyle.Colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(FluentColor::CardBackground);
		imguiStyle.Colors[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(FluentColor::WindowBackground);
		imguiStyle.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(FluentColor::Divider);

		imguiStyle.ItemSpacing.y = 0;
		imguiStyle.ScrollbarSize = scrollbarWidth * settingGlobalScale;
		imguiStyle.WindowTitleAlign = ImVec2(0.0f, 0.5f);
		imguiStyle.WindowPadding = ImVec2(0.0f, 0.0f);
		imguiStyle.FramePadding = ImVec2(0.0f, 0.0f);
		imguiStyle.FrameBorderSize = 1.0f * settingGlobalScale;
		imguiStyle.ChildBorderSize = 1.0f * settingGlobalScale;
		imguiStyle.ChildRounding = 4.0f * settingGlobalScale;
		imguiStyle.FrameRounding = 4.0f * settingGlobalScale;
		imguiStyle.PopupRounding = 4.0f * settingGlobalScale;
		imguiStyle.GrabRounding = 4.0f * settingGlobalScale;
		imguiStyle.GrabMinSize = 12.0f * settingGlobalScale;
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

	bool ButtonClass::Navigation(const char* label, const ImVec2& size, bool selected, ImU32 textColor, const ImVec2& alignment) const
	{
		(void)size;
		(void)alignment;
		if (textColor != FluentColor::TextPrimary)
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		const bool clicked = ImFluent::NavItem(label, selected);
		if (textColor != FluentColor::TextPrimary) ImGui::PopStyleColor();
		return clicked;
	}

	bool ButtonClass::AccentToggle(const char* label, const ImVec2& size, bool selected) const
	{
		return selected
			? ImFluent::AccentButton(label, size)
			: ImFluent::Button(label, size);
	}

	bool ButtonClass::HeroIcon(const char* label, const ImVec2& size) const
	{
		ImGui::PushStyleColor(ImGuiCol_Button, FluentColor::HeroFill);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::HeroFillHovered);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, FluentColor::HeroFillPressed);
		ImGui::PushStyleColor(ImGuiCol_Text, FluentColor::TextOnAccent);
		ImGui::PushStyleColor(ImGuiCol_Border, FluentColor::Transparent);

		const bool clicked = ImGui::Button(label, size);
		PopControlStyle(5);
		return clicked;
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

	bool ComboClass::Begin(const char* label, const char* preview, int itemCount) const
	{
		const float itemHeight = ImGui::GetTextLineHeightWithSpacing() + 8.0f * settingGlobalScale;
		const float popupHeight = itemCount * itemHeight + ImGui::GetStyle().WindowPadding.y * 2 * settingGlobalScale + 16.0f * settingGlobalScale;
		ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, popupHeight));

		PushComboColors();
		const bool open = ImGui::BeginCombo(label, preview);
		if (!open) PopControlStyle(9, 1);
		return open;
	}

	bool ComboClass::Selectable(const char* label, bool selected) const
	{
		const bool clicked = ImGui::Selectable(label, selected);
		if (selected) ImGui::SetItemDefaultFocus();
		return clicked;
	}

	void ComboClass::End() const
	{
		ImGui::EndCombo();
		PopControlStyle(9, 1);
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

	// 三级封装类
	void EntryClass::EntryOneLine(const string& line, const vector<Encapsulation>& vec)
	{
	}
	void EntryClass::EntryTwoLines(const string& line1, const string& line2, const vector<Encapsulation>& vec)
	{
	}
	void EntryClass::EntryMultiLines(const string& line1, const string& text, const vector<Encapsulation>& vec)
	{
	}
}
