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

		ImGuiToggleConfig config;
		config.Size = { 40.0f * settingGlobalScale, 20.0f * settingGlobalScale };
		config.Flags = ImGuiToggleFlags_Animated | ImGuiToggleFlags_ShadowedFrame;

		// 开关轨道、滑块和文字颜色成组压入，避免调用处重复维护样式栈。
		ImGui::PushStyleColor(ImGuiCol_FrameBg, FluentColor::SubtleFillPressed);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, FluentColor::ControlStroke);
		ImGui::PushStyleColor(ImGuiCol_Button, FluentColor::Accent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::AccentHovered);
		ImGui::PushStyleColor(ImGuiCol_Text, *state ? FluentColor::TextOnAccent : FluentColor::TextDisabled);
		ImGui::PushStyleColor(ImGuiCol_BorderShadow, *state ? FluentColor::Accent : FluentColor::TextDisabled);

		const bool changed = ImGui::Toggle(label, state, config);
		PopControlStyle(6);
		return changed;
	}
	ToggleClass toggle;

	bool ButtonClass::Standard(const char* label, const ImVec2& size, ImU32 textColor) const
	{
		PushStandardControlColors(textColor);
		const bool clicked = ImGui::Button(label, size);
		PopControlStyle(5);
		return clicked;
	}

	bool ButtonClass::Navigation(const char* label, const ImVec2& size, bool selected, ImU32 textColor, const ImVec2& alignment) const
	{
		ImGui::PushStyleColor(ImGuiCol_Button, selected ? FluentColor::SubtleFill : FluentColor::Transparent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::SubtleFill);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, selected ? FluentColor::SubtleFill : FluentColor::SubtleFillPressed);
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::PushStyleColor(ImGuiCol_Border, FluentColor::Transparent);
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, alignment);

		const bool clicked = ImGui::Button(label, size);
		PopControlStyle(5, 1);
		return clicked;
	}

	bool ButtonClass::AccentToggle(const char* label, const ImVec2& size, bool selected) const
	{
		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, FluentColor::Accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::AccentHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, FluentColor::AccentPressed);
			ImGui::PushStyleColor(ImGuiCol_Text, FluentColor::TextOnAccent);
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, FluentColor::ControlFill);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FluentColor::ControlFillHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, FluentColor::ControlFillPressed);
			ImGui::PushStyleColor(ImGuiCol_Text, FluentColor::TextPrimary);
		}
		ImGui::PushStyleColor(ImGuiCol_Border, FluentColor::ControlStroke);

		const bool clicked = ImGui::Button(label, size);
		PopControlStyle(5);
		return clicked;
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
		PushSliderColors();
		const bool changed = ImGui::SliderFloat(label, value, minValue, maxValue, format);
		PopControlStyle(6, 1);
		return changed;
	}

	bool SliderClass::Int(const char* label, int* value, int minValue, int maxValue, const char* format) const
	{
		PushSliderColors();
		const bool changed = ImGui::SliderInt(label, value, minValue, maxValue, format);
		PopControlStyle(6, 1);
		return changed;
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
