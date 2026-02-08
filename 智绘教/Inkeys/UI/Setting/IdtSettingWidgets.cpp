#include "IdtSettingWidgets.h"

#include "IdtSetting.h"

namespace SettingWidgets
{
	ToggleClass toggle;
	ToggleClass::ToggleClass()
	{
		config.Size = { 40.0f * settingGlobalScale,20.0f * settingGlobalScale };
		config.Flags = ImGuiToggleFlags_Animated | ImGuiToggleFlags_ShadowedFrame;
	}
	void ToggleClass::ToggleBool(const char* label, bool state, int& pushStyleColorNum)
	{
		pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
		pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
		pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
		pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
		if (!state)
		{
			pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
			pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
		}
		else
		{
			pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
			pushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
		}
		ImGui::Toggle(label, &state, config);
	}
}