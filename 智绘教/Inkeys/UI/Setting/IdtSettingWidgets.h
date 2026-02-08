#pragma once

#include "../../IdtMain.h"

namespace SettingWidgets
{
	// 一级封装类
	class ToggleClass
	{
	public:
		ToggleClass();

	public:
		void ToggleBool(const char* label, bool state, int& pushStyleColorNum);

	private:
		ImGuiToggleConfig config;
	};
	extern ToggleClass toggle;

	// 二级封装类

	//class EntryClass
	//{
	//}
}