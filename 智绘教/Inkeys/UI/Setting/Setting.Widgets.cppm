module;

#include "Setting.Wrap.h"

export module Inkeys.UI.Setting.Widgets;

import Inkeys.UI.Setting;

namespace SettingWidgets
{
	// 一级封装类

	// 二级封装类
	class ToggleClass
	{
	public:
		ToggleClass()
		{
			config.Size = { 40.0f * settingGlobalScale,20.0f * settingGlobalScale };
			config.Flags = ImGuiToggleFlags_Animated | ImGuiToggleFlags_ShadowedFrame;
		}

	public:
		void ToggleBool(const char* label, bool state)
		{
			/*
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
			ImGui::Toggle(label, &state, config);*/
		}

	private:
		ImGuiToggleConfig config;
	};
	extern ToggleClass toggle;

	// 三级封装类
	using Encapsulation = variant<ToggleClass>;

	class EntryClass
	{
	public:
		void EntryOneLine(const string& line, const vector<Encapsulation>& vec)
		{
		}
		void EntryTwoLines(const string& line1, const string& line2, const vector<Encapsulation>& vec)
		{
		}
		void EntryMultiLines(const string& line1, const string& text, const vector<Encapsulation>& vec)
		{
		}
	};
}