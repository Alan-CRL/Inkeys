#include "IdtI18n.h"

#include "IdtText.h"

wstring i18nIdentifying;
unordered_map<i18nEnum, variant<string, wstring>> i18n;

bool loadI18n(int type, wstring path, wstring lang, bool initialization)
{
	string jsonContent;
	if (type == 1)
	{
		int resNum = 245;
		if (lang == L"zh-CN") resNum = 245;
		if (lang == L"en-US") resNum = 246;

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
		else if (initialization) i18nIdentifying = L"en-US";

		if (i18nVal.isMember("MainColumn") && i18nVal["MainColumn"].isObject())
		{
			if (i18nVal["MainColumn"].isMember("Centre") && i18nVal["MainColumn"]["Centre"].isObject())
			{
				if (i18nVal["MainColumn"]["Centre"].isMember("Select") && i18nVal["MainColumn"]["Centre"]["Select"].isString()) i18n[i18nEnum::MainColumn_Centre_Select] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Select"].asString());
				else if (initialization) i18n[i18nEnum::MainColumn_Centre_Select] = L"";
				if (i18nVal["MainColumn"]["Centre"].isMember("SelectClean") && i18nVal["MainColumn"]["Centre"]["SelectClean"].isString()) i18n[i18nEnum::MainColumn_Centre_SelectClean] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["SelectClean"].asString());
				else if (initialization) i18n[i18nEnum::MainColumn_Centre_SelectClean] = L"";
				if (i18nVal["MainColumn"]["Centre"].isMember("Pen") && i18nVal["MainColumn"]["Centre"]["Pen"].isString()) i18n[i18nEnum::MainColumn_Centre_Pen] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Pen"].asString());
				else if (initialization) i18n[i18nEnum::MainColumn_Centre_Pen] = L"";
				if (i18nVal["MainColumn"]["Centre"].isMember("Eraser") && i18nVal["MainColumn"]["Centre"]["Eraser"].isString()) i18n[i18nEnum::MainColumn_Centre_Eraser] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Eraser"].asString());
				else if (initialization) i18n[i18nEnum::MainColumn_Centre_Eraser] = L"";
				if (i18nVal["MainColumn"]["Centre"].isMember("Options") && i18nVal["MainColumn"]["Centre"]["Options"].isString()) i18n[i18nEnum::MainColumn_Centre_Options] = utf8ToUtf16(i18nVal["MainColumn"]["Centre"]["Options"].asString());
				else if (initialization) i18n[i18nEnum::MainColumn_Centre_Options] = L"";
			}
		}

		if (i18nVal.isMember("Settings") && i18nVal["Settings"].isObject())
		{
			if (i18nVal["Settings"].isMember("Settings") && i18nVal["Settings"]["Settings"].isString())
			{
				i18n[i18nEnum::Settings] = i18nVal["Settings"]["Settings"].asString();
				i18n[i18nEnum::SettingsW] = utf8ToUtf16(i18nVal["Settings"]["Settings"].asString());
			}
			else if (initialization)
			{
				i18n[i18nEnum::Settings] = "";
				i18n[i18nEnum::SettingsW] = L"";
			}

			if (i18nVal["Settings"].isMember("Operate") && i18nVal["Settings"]["Operate"].isObject())
			{
				if (i18nVal["Settings"]["Operate"].isMember("Query") && i18nVal["Settings"]["Operate"]["Query"].isString()) i18n[i18nEnum::Settings_Operate_Query] = i18nVal["Settings"]["Operate"]["Query"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Operate_Query] = "";
				if (i18nVal["Settings"]["Operate"].isMember("Save") && i18nVal["Settings"]["Operate"]["Save"].isString()) i18n[i18nEnum::Settings_Operate_Save] = i18nVal["Settings"]["Operate"]["Save"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Operate_Save] = "";
			}

			if (i18nVal["Settings"].isMember("Home") && i18nVal["Settings"]["Home"].isObject())
			{
				if (i18nVal["Settings"]["Home"].isMember("Home") && i18nVal["Settings"]["Home"]["Home"].isString()) i18n[i18nEnum::Settings_Home] = i18nVal["Settings"]["Home"]["Home"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home] = "";
				if (i18nVal["Settings"]["Home"].isMember("Prompt") && i18nVal["Settings"]["Home"]["Prompt"].isString()) i18n[i18nEnum::Settings_Home_Prompt] = i18nVal["Settings"]["Home"]["Prompt"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Prompt] = "";
				if (i18nVal["Settings"]["Home"].isMember("Feedback") && i18nVal["Settings"]["Home"]["Feedback"].isString()) i18n[i18nEnum::Settings_Home_Feedback] = i18nVal["Settings"]["Home"]["Feedback"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Feedback] = "";
				if (i18nVal["Settings"]["Home"].isMember("Thanks") && i18nVal["Settings"]["Home"]["Thanks"].isString()) i18n[i18nEnum::Settings_Home_Thanks] = i18nVal["Settings"]["Home"]["Thanks"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Thanks] = "";
				if (i18nVal["Settings"]["Home"].isMember("Button1") && i18nVal["Settings"]["Home"]["Button1"].isString()) i18n[i18nEnum::Settings_Home_Button1] = i18nVal["Settings"]["Home"]["Button1"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Button1] = "";
				if (i18nVal["Settings"]["Home"].isMember("Button2") && i18nVal["Settings"]["Home"]["Button2"].isString()) i18n[i18nEnum::Settings_Home_Button2] = i18nVal["Settings"]["Home"]["Button2"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Button2] = "";
				if (i18nVal["Settings"]["Home"].isMember("Button3") && i18nVal["Settings"]["Home"]["Button3"].isString()) i18n[i18nEnum::Settings_Home_Button3] = i18nVal["Settings"]["Home"]["Button3"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Button3] = "";
				if (i18nVal["Settings"]["Home"].isMember("Button4") && i18nVal["Settings"]["Home"]["Button4"].isString()) i18n[i18nEnum::Settings_Home_Button4] = i18nVal["Settings"]["Home"]["Button4"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Button4] = "";
				if (i18nVal["Settings"]["Home"].isMember("Language") && i18nVal["Settings"]["Home"]["Language"].isString()) i18n[i18nEnum::Settings_Home_Language] = i18nVal["Settings"]["Home"]["Language"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Home_Language] = "";
			}

			if (i18nVal["Settings"].isMember("Regular") && i18nVal["Settings"]["Regular"].isObject())
			{
				if (i18nVal["Settings"]["Regular"].isMember("Regular") && i18nVal["Settings"]["Regular"]["Regular"].isString()) i18n[i18nEnum::Settings_Regular] = i18nVal["Settings"]["Regular"]["Regular"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Regular] = "";
			}

			if (i18nVal["Settings"].isMember("Update") && i18nVal["Settings"]["Update"].isObject())
			{
				if (i18nVal["Settings"]["Update"].isMember("Tip0") && i18nVal["Settings"]["Update"]["Tip0"].isString()) i18n[i18nEnum::Settings_Update_Tip0] = i18nVal["Settings"]["Update"]["Tip0"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip0] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip1") && i18nVal["Settings"]["Update"]["Tip1"].isString()) i18n[i18nEnum::Settings_Update_Tip1] = i18nVal["Settings"]["Update"]["Tip1"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip1] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip2") && i18nVal["Settings"]["Update"]["Tip2"].isString()) i18n[i18nEnum::Settings_Update_Tip2] = i18nVal["Settings"]["Update"]["Tip2"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip2] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip3") && i18nVal["Settings"]["Update"]["Tip3"].isString()) i18n[i18nEnum::Settings_Update_Tip3] = i18nVal["Settings"]["Update"]["Tip3"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip3] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip4") && i18nVal["Settings"]["Update"]["Tip4"].isString()) i18n[i18nEnum::Settings_Update_Tip4] = i18nVal["Settings"]["Update"]["Tip4"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip4] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip5") && i18nVal["Settings"]["Update"]["Tip5"].isString()) i18n[i18nEnum::Settings_Update_Tip5] = i18nVal["Settings"]["Update"]["Tip5"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip5] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip6") && i18nVal["Settings"]["Update"]["Tip6"].isString()) i18n[i18nEnum::Settings_Update_Tip6] = i18nVal["Settings"]["Update"]["Tip6"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip6] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip7") && i18nVal["Settings"]["Update"]["Tip7"].isString()) i18n[i18nEnum::Settings_Update_Tip7] = i18nVal["Settings"]["Update"]["Tip7"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip7] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip8") && i18nVal["Settings"]["Update"]["Tip8"].isString()) i18n[i18nEnum::Settings_Update_Tip8] = i18nVal["Settings"]["Update"]["Tip8"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip8] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip9") && i18nVal["Settings"]["Update"]["Tip9"].isString()) i18n[i18nEnum::Settings_Update_Tip9] = i18nVal["Settings"]["Update"]["Tip9"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip9] = "";
				if (i18nVal["Settings"]["Update"].isMember("Tip10") && i18nVal["Settings"]["Update"]["Tip10"].isString()) i18n[i18nEnum::Settings_Update_Tip10] = i18nVal["Settings"]["Update"]["Tip10"].asString();
				else if (initialization) i18n[i18nEnum::Settings_Update_Tip10] = "";
			}
		}

		return true;
	}
	return false;
}