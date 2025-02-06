#pragma once

#include "IdtMain.h"

enum i18nEnum
{
	MainColumnSelect,
	MainColumnSelectClean,
	MainColumnPen,
	MainColumnEraser,
	MainColumnOptions,

	LnkName,

	Settings,
	SettingsW,
	SettingsOperateReset,
	SettingsOperateRepair,
	SettingsOperateCopy,

	SettingsHome,
	SettingsHomePrompt,
	SettingsHome1,
	SettingsHome2,
	SettingsHome3,

	SettingsRegular,
	SettingsRegular1,
	SettingsRegular1_1,
	SettingsRegular1_2,
	SettingsRegular2,
	SettingsRegular2_1,
	SettingsRegular2_2,
	SettingsRegular3,
	SettingsRegular3_1,
	SettingsRegular3_2,
	SettingsRegular3_3,
	SettingsRegular3_4,
	SettingsRegular3_5,
	SettingsRegular3_6,
	SettingsRegular3_7,
	SettingsRegular3_8,
	SettingsRegular3_9,
	SettingsRegular4,
	SettingsRegular4_1,
	SettingsRegular4_2,
	SettingsRegular4_3,
	SettingsRegular4_4,
	SettingsRegular5,
	SettingsRegular5_1,
	SettingsRegular5_2,
	SettingsRegular5_3,
	SettingsRegular5_4,

	SettingsUpdateTip0,
	SettingsUpdateTip1,
	SettingsUpdateTip2,
	SettingsUpdateTip3,
	SettingsUpdateTip4,
	SettingsUpdateTip5,
	SettingsUpdateTip6,
	SettingsUpdateTip7,
	SettingsUpdateTip8,
	SettingsUpdateTip9,
	SettingsUpdateTip10,
	SettingsUpdateTip11,
	SettingsUpdate0
};

extern wstring i18nIdentifying;
extern unordered_map<i18nEnum, variant<string, wstring>> i18n;

bool loadI18n(int type, wstring path, wstring lang = L"en-US");