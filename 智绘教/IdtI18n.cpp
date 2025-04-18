﻿#include "IdtI18n.h"

#include "IdtText.h"

wstring i18nIdentifying;
unordered_map<i18nEnum, variant<string, wstring>> i18n;

bool loadI18n(int type, wstring path, wstring lang)
{
	string jsonContent;
	if (type == 1)
	{
		int resNum = 246;
		if (lang == L"zh-CN") resNum = 245;
		if (lang == L"en-US") resNum = 246;
		//if (lang == L"zh-TW") resNum = ;

		HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCE(resNum), path.c_str());
		HGLOBAL hMem = LoadResource(NULL, hRes);
		DWORD dwSize = SizeofResource(NULL, hRes);

		jsonContent = string(static_cast<char*>(hMem), dwSize);
		if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	}
	else if (type == 2)
	{
		ifstream ifs(path, ios::binary);
		jsonContent = string((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
		ifs.close();

		if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	}

	istringstream jsonContentStream(jsonContent);
	Json::CharReaderBuilder readerBuilder;
	Json::Value i18nVal;
	string jsonErr;

	if (Json::parseFromStream(readerBuilder, jsonContentStream, &i18nVal, &jsonErr))
	{
		if (i18nVal.isMember("Language") && i18nVal["Language"].isString()) i18nIdentifying = utf8ToUtf16(i18nVal["Language"].asString());

		if (i18nVal.isMember("MainColumn") && i18nVal["MainColumn"].isObject())
		{
			if (i18nVal["MainColumn"].isMember("Centre") && i18nVal["MainColumn"]["Centre"].isObject())
			{
				if (i18nVal["MainColumn"]["Centre"].isMember("Select") && i18nVal["MainColumn"]["Centre"]["Select"].isString()) i18n[i18nEnum::MainColumnSelect] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Select"].asString());
				if (i18nVal["MainColumn"]["Centre"].isMember("SelectClean") && i18nVal["MainColumn"]["Centre"]["SelectClean"].isString()) i18n[i18nEnum::MainColumnSelectClean] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["SelectClean"].asString());
				if (i18nVal["MainColumn"]["Centre"].isMember("Pen") && i18nVal["MainColumn"]["Centre"]["Pen"].isString()) i18n[i18nEnum::MainColumnPen] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Pen"].asString());
				if (i18nVal["MainColumn"]["Centre"].isMember("Eraser") && i18nVal["MainColumn"]["Centre"]["Eraser"].isString()) i18n[i18nEnum::MainColumnEraser] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Eraser"].asString());
				if (i18nVal["MainColumn"]["Centre"].isMember("Options") && i18nVal["MainColumn"]["Centre"]["Options"].isString()) i18n[i18nEnum::MainColumnOptions] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Options"].asString());
			}
		}

		if (i18nVal.isMember("LnkName") && i18nVal["LnkName"].isString())i18n[i18nEnum::LnkName] = utf8ToUtf16(i18nVal["LnkName"].asString());

		if (i18nVal.isMember("Settings") && i18nVal["Settings"].isObject())
		{
			if (i18nVal["Settings"].isMember("Settings") && i18nVal["Settings"]["Settings"].isString())
			{
				i18n[i18nEnum::Settings] = i18nVal["Settings"]["Settings"].asString();
				i18n[i18nEnum::SettingsW] = utf8ToUtf16(i18nVal["Settings"]["Settings"].asString());
			}

			if (i18nVal["Settings"].isMember("Operate") && i18nVal["Settings"]["Operate"].isObject())
			{
				if (i18nVal["Settings"]["Operate"].isMember("Reset") && i18nVal["Settings"]["Operate"]["Reset"].isString()) i18n[i18nEnum::SettingsOperateReset] = i18nVal["Settings"]["Operate"]["Reset"].asString();
				if (i18nVal["Settings"]["Operate"].isMember("Repair") && i18nVal["Settings"]["Operate"]["Repair"].isString()) i18n[i18nEnum::SettingsOperateRepair] = i18nVal["Settings"]["Operate"]["Repair"].asString();
				if (i18nVal["Settings"]["Operate"].isMember("Copy") && i18nVal["Settings"]["Operate"]["Copy"].isString()) i18n[i18nEnum::SettingsOperateCopy] = i18nVal["Settings"]["Operate"]["Copy"].asString();
			}

			if (i18nVal["Settings"].isMember("Home") && i18nVal["Settings"]["Home"].isObject())
			{
				if (i18nVal["Settings"]["Home"].isMember("Home") && i18nVal["Settings"]["Home"]["Home"].isString()) i18n[i18nEnum::SettingsHome] = i18nVal["Settings"]["Home"]["Home"].asString();
				if (i18nVal["Settings"]["Home"].isMember("Prompt") && i18nVal["Settings"]["Home"]["Prompt"].isString()) i18n[i18nEnum::SettingsHomePrompt] = i18nVal["Settings"]["Home"]["Prompt"].asString();
				if (i18nVal["Settings"]["Home"].isMember("#1") && i18nVal["Settings"]["Home"]["#1"].isString()) i18n[i18nEnum::SettingsHome1] = i18nVal["Settings"]["Home"]["#1"].asString();
				if (i18nVal["Settings"]["Home"].isMember("#2") && i18nVal["Settings"]["Home"]["#2"].isString()) i18n[i18nEnum::SettingsHome2] = i18nVal["Settings"]["Home"]["#2"].asString();
				if (i18nVal["Settings"]["Home"].isMember("#3") && i18nVal["Settings"]["Home"]["#3"].isString()) i18n[i18nEnum::SettingsHome3] = i18nVal["Settings"]["Home"]["#3"].asString();
			}

			if (i18nVal["Settings"].isMember("Regular") && i18nVal["Settings"]["Regular"].isObject())
			{
				if (i18nVal["Settings"]["Regular"].isMember("Regular") && i18nVal["Settings"]["Regular"]["Regular"].isString()) i18n[i18nEnum::SettingsRegular] = i18nVal["Settings"]["Regular"]["Regular"].asString();

				if (i18nVal["Settings"]["Regular"].isMember("#1") && i18nVal["Settings"]["Regular"]["#1"].isString()) i18n[i18nEnum::SettingsRegular1] = i18nVal["Settings"]["Regular"]["#1"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#1_1") && i18nVal["Settings"]["Regular"]["#1_1"].isString()) i18n[i18nEnum::SettingsRegular1_1] = i18nVal["Settings"]["Regular"]["#1_1"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#1_2") && i18nVal["Settings"]["Regular"]["#1_2"].isString()) i18n[i18nEnum::SettingsRegular1_2] = i18nVal["Settings"]["Regular"]["#1_2"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#2") && i18nVal["Settings"]["Regular"]["#2"].isString()) i18n[i18nEnum::SettingsRegular2] = i18nVal["Settings"]["Regular"]["#2"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#2_1") && i18nVal["Settings"]["Regular"]["#2_1"].isString()) i18n[i18nEnum::SettingsRegular2_1] = i18nVal["Settings"]["Regular"]["#2_1"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#2_2") && i18nVal["Settings"]["Regular"]["#2_2"].isString()) i18n[i18nEnum::SettingsRegular2_2] = i18nVal["Settings"]["Regular"]["#2_2"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3") && i18nVal["Settings"]["Regular"]["#3"].isString()) i18n[i18nEnum::SettingsRegular3] = i18nVal["Settings"]["Regular"]["#3"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_1") && i18nVal["Settings"]["Regular"]["#3_1"].isString()) i18n[i18nEnum::SettingsRegular3_1] = i18nVal["Settings"]["Regular"]["#3_1"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_2") && i18nVal["Settings"]["Regular"]["#3_2"].isString()) i18n[i18nEnum::SettingsRegular3_2] = i18nVal["Settings"]["Regular"]["#3_2"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_3") && i18nVal["Settings"]["Regular"]["#3_3"].isString()) i18n[i18nEnum::SettingsRegular3_3] = i18nVal["Settings"]["Regular"]["#3_3"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_4") && i18nVal["Settings"]["Regular"]["#3_4"].isString()) i18n[i18nEnum::SettingsRegular3_4] = i18nVal["Settings"]["Regular"]["#3_4"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_5") && i18nVal["Settings"]["Regular"]["#3_5"].isString()) i18n[i18nEnum::SettingsRegular3_5] = i18nVal["Settings"]["Regular"]["#3_5"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_6") && i18nVal["Settings"]["Regular"]["#3_6"].isString()) i18n[i18nEnum::SettingsRegular3_6] = i18nVal["Settings"]["Regular"]["#3_6"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_7") && i18nVal["Settings"]["Regular"]["#3_7"].isString()) i18n[i18nEnum::SettingsRegular3_7] = i18nVal["Settings"]["Regular"]["#3_7"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_8") && i18nVal["Settings"]["Regular"]["#3_8"].isString()) i18n[i18nEnum::SettingsRegular3_8] = i18nVal["Settings"]["Regular"]["#3_8"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#3_9") && i18nVal["Settings"]["Regular"]["#3_9"].isString()) i18n[i18nEnum::SettingsRegular3_9] = i18nVal["Settings"]["Regular"]["#3_9"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#4") && i18nVal["Settings"]["Regular"]["#4"].isString()) i18n[i18nEnum::SettingsRegular4] = i18nVal["Settings"]["Regular"]["#4"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#4_1") && i18nVal["Settings"]["Regular"]["#4_1"].isString()) i18n[i18nEnum::SettingsRegular4_1] = i18nVal["Settings"]["Regular"]["#4_1"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#4_2") && i18nVal["Settings"]["Regular"]["#4_2"].isString()) i18n[i18nEnum::SettingsRegular4_2] = i18nVal["Settings"]["Regular"]["#4_2"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#4_3") && i18nVal["Settings"]["Regular"]["#4_3"].isString()) i18n[i18nEnum::SettingsRegular4_3] = i18nVal["Settings"]["Regular"]["#4_3"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#4_4") && i18nVal["Settings"]["Regular"]["#4_4"].isString()) i18n[i18nEnum::SettingsRegular4_4] = i18nVal["Settings"]["Regular"]["#4_4"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#5") && i18nVal["Settings"]["Regular"]["#5"].isString()) i18n[i18nEnum::SettingsRegular5] = i18nVal["Settings"]["Regular"]["#5"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#5_1") && i18nVal["Settings"]["Regular"]["#5_1"].isString()) i18n[i18nEnum::SettingsRegular5_1] = i18nVal["Settings"]["Regular"]["#5_1"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#5_2") && i18nVal["Settings"]["Regular"]["#5_2"].isString()) i18n[i18nEnum::SettingsRegular5_2] = i18nVal["Settings"]["Regular"]["#5_2"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#5_3") && i18nVal["Settings"]["Regular"]["#5_3"].isString()) i18n[i18nEnum::SettingsRegular5_3] = i18nVal["Settings"]["Regular"]["#5_3"].asString();
				if (i18nVal["Settings"]["Regular"].isMember("#5_4") && i18nVal["Settings"]["Regular"]["#5_4"].isString()) i18n[i18nEnum::SettingsRegular5_4] = i18nVal["Settings"]["Regular"]["#5_4"].asString();
			}

			if (i18nVal["Settings"].isMember("Update") && i18nVal["Settings"]["Update"].isObject())
			{
				if (i18nVal["Settings"]["Update"].isMember("Tip0") && i18nVal["Settings"]["Update"]["Tip0"].isString()) i18n[i18nEnum::SettingsUpdateTip0] = i18nVal["Settings"]["Update"]["Tip0"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip1") && i18nVal["Settings"]["Update"]["Tip1"].isString()) i18n[i18nEnum::SettingsUpdateTip1] = i18nVal["Settings"]["Update"]["Tip1"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip2") && i18nVal["Settings"]["Update"]["Tip2"].isString()) i18n[i18nEnum::SettingsUpdateTip2] = i18nVal["Settings"]["Update"]["Tip2"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip3") && i18nVal["Settings"]["Update"]["Tip3"].isString()) i18n[i18nEnum::SettingsUpdateTip3] = i18nVal["Settings"]["Update"]["Tip3"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip4") && i18nVal["Settings"]["Update"]["Tip4"].isString()) i18n[i18nEnum::SettingsUpdateTip4] = i18nVal["Settings"]["Update"]["Tip4"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip5") && i18nVal["Settings"]["Update"]["Tip5"].isString()) i18n[i18nEnum::SettingsUpdateTip5] = i18nVal["Settings"]["Update"]["Tip5"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip6") && i18nVal["Settings"]["Update"]["Tip6"].isString()) i18n[i18nEnum::SettingsUpdateTip6] = i18nVal["Settings"]["Update"]["Tip6"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip7") && i18nVal["Settings"]["Update"]["Tip7"].isString()) i18n[i18nEnum::SettingsUpdateTip7] = i18nVal["Settings"]["Update"]["Tip7"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip8") && i18nVal["Settings"]["Update"]["Tip8"].isString()) i18n[i18nEnum::SettingsUpdateTip8] = i18nVal["Settings"]["Update"]["Tip8"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip9") && i18nVal["Settings"]["Update"]["Tip9"].isString()) i18n[i18nEnum::SettingsUpdateTip9] = i18nVal["Settings"]["Update"]["Tip9"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip10") && i18nVal["Settings"]["Update"]["Tip10"].isString()) i18n[i18nEnum::SettingsUpdateTip10] = i18nVal["Settings"]["Update"]["Tip10"].asString();
				if (i18nVal["Settings"]["Update"].isMember("Tip11") && i18nVal["Settings"]["Update"]["Tip11"].isString()) i18n[i18nEnum::SettingsUpdateTip11] = i18nVal["Settings"]["Update"]["Tip11"].asString();
				if (i18nVal["Settings"]["Update"].isMember("#0") && i18nVal["Settings"]["Update"]["#0"].isString()) i18n[i18nEnum::SettingsUpdate0] = i18nVal["Settings"]["Update"]["#0"].asString();
			}
		}

		return true;
	}
	return false;
}