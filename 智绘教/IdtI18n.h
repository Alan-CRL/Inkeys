#pragma once

#include "IdtMain.h"

enum i18nEnum
{
	MainColumn_Centre_Select,
	MainColumn_Centre_SelectClean,
	MainColumn_Centre_Pen,
	MainColumn_Centre_Eraser,
	MainColumn_Centre_Options,

	Settings,
	SettingsW,
	Settings_Operate_Query,
	Settings_Operate_Save,
	Settings_Home,
	Settings_Home_Prompt,
	Settings_Home_Feedback,
	Settings_Home_Thanks,
	Settings_Home_Button1,
	Settings_Home_Button2,
	Settings_Home_Button3,
	Settings_Home_Button4,
	Settings_Home_Language,
	Settings_Regular,
	Settings_Update_Tip0,
	Settings_Update_Tip1,
	Settings_Update_Tip2,
	Settings_Update_Tip3,
	Settings_Update_Tip4,
	Settings_Update_Tip5,
	Settings_Update_Tip6,
	Settings_Update_Tip7,
	Settings_Update_Tip8,
	Settings_Update_Tip9
};

extern wstring i18nIdentifying;
extern unordered_map<i18nEnum, variant<string, wstring>> i18n;

bool loadI18n(int type, wstring path, wstring lang = L"en-US", bool initialization = false);