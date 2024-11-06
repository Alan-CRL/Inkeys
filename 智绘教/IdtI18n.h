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
	Settings_Operate_Reset,
	Settings_Operate_Save,

	Settings_Home,
	Settings_Home_Prompt,
	Settings_Home_1,
	Settings_Home_2,
	Settings_Home_3,

	Settings_Regular,
	Settings_Regular_1,
	Settings_Regular_1_1,
	Settings_Regular_1_2,
	Settings_Regular_2,
	Settings_Regular_2_1,
	Settings_Regular_2_2,
	Settings_Regular_3,
	Settings_Regular_3_1,
	Settings_Regular_3_2,
	Settings_Regular_3_3,
	Settings_Regular_3_4,
	Settings_Regular_3_5,
	Settings_Regular_3_6,
	Settings_Regular_4,
	Settings_Regular_4_1,
	Settings_Regular_4_2,
	Settings_Regular_4_3,
	Settings_Regular_4_4,
	Settings_Regular_5,
	Settings_Regular_5_1,
	Settings_Regular_5_2,
	Settings_Regular_5_3,
	Settings_Regular_5_4,

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