#include "IdtConfiguration.h"

#include "IdtText.h"

SetListStruct setlist;

// 占用文件（读写权限）
bool OccupyFile(HANDLE* hFile, const wstring& filePath)
{
	if (_waccess(filePath.c_str(), 0) == -1) return false;

	for (int time = 1; time <= 5; time++)
	{
		*hFile = CreateFileW(
			filePath.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0,              // 不共享，独占访问
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);

		if (*hFile != INVALID_HANDLE_VALUE) break;
		else if (time >= 3) return false;

		this_thread::sleep_for(chrono::milliseconds(100));
	}

	if (*hFile != INVALID_HANDLE_VALUE) return true;
	return false;
}
// 释放文件
bool UnOccupyFile(HANDLE* hFile)
{
	if (*hFile != NULL)
	{
		CloseHandle(*hFile);
		return true;
	}
	return false;
}

PptComSetListStruct pptComSetlist;
bool PptComReadSetting()
{
	Json::Reader reader;
	Json::Value root;

	ifstream readjson;
	readjson.imbue(locale("zh_CN.UTF8"));
	readjson.open((globalPath + L"opt\\pptcom_configuration.json").c_str());

	if (reader.parse(readjson, root))
	{
		if (root.isMember("FixedHandWriting") && root["FixedHandWriting"].isBool()) pptComSetlist.fixedHandWriting = root["FixedHandWriting"].asBool();
		if (root.isMember("MemoryWidgetPosition") && root["MemoryWidgetPosition"].isBool()) pptComSetlist.memoryWidgetPosition = root["MemoryWidgetPosition"].asBool();

		if (root.isMember("ShowBottomBoth") && root["ShowBottomBoth"].isBool()) pptComSetlist.showBottomBoth = root["ShowBottomBoth"].asBool();
		if (root.isMember("ShowMiddleBoth") && root["ShowMiddleBoth"].isBool()) pptComSetlist.showMiddleBoth = root["ShowMiddleBoth"].asBool();
		if (root.isMember("ShowBottomMiddle") && root["ShowBottomMiddle"].isBool()) pptComSetlist.showBottomMiddle = root["ShowBottomMiddle"].asBool();

		if (pptComSetlist.memoryWidgetPosition)
		{
			if (root.isMember("BottomBothWidth") && root["BottomBothWidth"].isDouble()) pptComSetlist.bottomBothWidth = (float)root["BottomBothWidth"].asDouble();
			if (root.isMember("BottomBothHeight") && root["BottomBothHeight"].isDouble()) pptComSetlist.bottomBothHeight = (float)root["BottomBothHeight"].asDouble();
			if (root.isMember("MiddleBothWidth") && root["MiddleBothWidth"].isDouble()) pptComSetlist.middleBothWidth = (float)root["MiddleBothWidth"].asDouble();
			if (root.isMember("MiddleBothHeight") && root["MiddleBothHeight"].isDouble()) pptComSetlist.middleBothHeight = (float)root["MiddleBothHeight"].asDouble();
			if (root.isMember("BottomMiddleWidth") && root["BottomMiddleWidth"].isDouble()) pptComSetlist.bottomMiddleWidth = (float)root["BottomMiddleWidth"].asDouble();
			if (root.isMember("BottomMiddleHeight") && root["BottomMiddleHeight"].isDouble()) pptComSetlist.bottomMiddleHeight = (float)root["BottomMiddleHeight"].asDouble();
		}

		if (root.isMember("BottomSideBothWidgetScale") && root["BottomSideBothWidgetScale"].isDouble()) pptComSetlist.bottomSideBothWidgetScale = (float)root["BottomSideBothWidgetScale"].asDouble();
		if (root.isMember("BottomSideMiddleWidgetScale") && root["BottomSideMiddleWidgetScale"].isDouble()) pptComSetlist.bottomSideMiddleWidgetScale = (float)root["BottomSideMiddleWidgetScale"].asDouble();
		if (root.isMember("MiddleSideBothWidgetScale") && root["MiddleSideBothWidgetScale"].isDouble()) pptComSetlist.middleSideBothWidgetScale = (float)root["MiddleSideBothWidgetScale"].asDouble();
	}

	readjson.close();

	return true;
}
bool PptComWriteSetting()
{
	if (_waccess((globalPath + L"opt").c_str(), 0) == -1)
		filesystem::create_directory(globalPath + L"opt");

	Json::Value root;

	root["FixedHandWriting"] = Json::Value(pptComSetlist.fixedHandWriting);
	root["MemoryWidgetPosition"] = Json::Value(pptComSetlist.memoryWidgetPosition);

	root["ShowBottomBoth"] = Json::Value(pptComSetlist.showBottomBoth);
	root["ShowMiddleBoth"] = Json::Value(pptComSetlist.showMiddleBoth);
	root["ShowBottomMiddle"] = Json::Value(pptComSetlist.showBottomMiddle);

	root["BottomBothWidth"] = Json::Value(pptComSetlist.bottomBothWidth);
	root["BottomBothHeight"] = Json::Value(pptComSetlist.bottomBothHeight);
	root["MiddleBothWidth"] = Json::Value(pptComSetlist.middleBothWidth);
	root["MiddleBothHeight"] = Json::Value(pptComSetlist.middleBothHeight);
	root["BottomMiddleWidth"] = Json::Value(pptComSetlist.bottomMiddleWidth);
	root["BottomMiddleHeight"] = Json::Value(pptComSetlist.bottomMiddleHeight);

	root["BottomSideBothWidgetScale"] = Json::Value(pptComSetlist.bottomSideBothWidgetScale);
	root["BottomSideMiddleWidgetScale"] = Json::Value(pptComSetlist.bottomSideMiddleWidgetScale);
	root["MiddleSideBothWidgetScale"] = Json::Value(pptComSetlist.middleSideBothWidgetScale);

	Json::StreamWriterBuilder outjson;
	outjson.settings_["emitUTF8"] = true;
	std::unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
	ofstream writejson;
	writejson.imbue(locale("zh_CN.UTF8"));
	writejson.open((globalPath + L"opt\\pptcom_configuration.json").c_str());
	writer->write(root, &writejson);
	writejson.close();

	return true;
}

DdbSetListStruct ddbSetList;
bool DdbReadSetting()
{
	if (_waccess((globalPath + L"PlugIn\\DDB\\interaction_configuration.json").c_str(), 4) == -1) return false;

	Json::Reader reader;
	Json::Value root;

	ifstream readjson;
	readjson.imbue(locale("zh_CN.UTF8"));
	readjson.open(globalPath + L"PlugIn\\DDB\\interaction_configuration.json");

	if (reader.parse(readjson, root))
	{
		if (root.isMember("SleepTime") && root["SleepTime"].isInt())
		{
			ddbSetList.sleepTime = root["SleepTime"].asInt();
		}

		if (root.isMember("Mode") && root["Mode"].isObject())
		{
			if (root["Mode"].isMember("Mode") && root["Mode"]["Mode"].isInt())
			{
				ddbSetList.mode = root["Mode"]["Mode"].asInt();
			}
			if (root["Mode"].isMember("RestartHost") && root["Mode"]["RestartHost"].isBool())
			{
				ddbSetList.restartHost = root["Mode"]["RestartHost"].asBool();
			}
		}

		if (root.isMember("Intercept") && root["Intercept"].isObject())
		{
			if (root["Intercept"].isMember("SeewoWhiteboard3Floating") && root["Intercept"]["SeewoWhiteboard3Floating"].isBool())
			{
				ddbSetList.InterceptWindow[0] = root["Intercept"]["SeewoWhiteboard3Floating"].asBool();
			}
			if (root["Intercept"].isMember("SeewoWhiteboard5Floating") && root["Intercept"]["SeewoWhiteboard5Floating"].isBool())
			{
				ddbSetList.InterceptWindow[1] = root["Intercept"]["SeewoWhiteboard5Floating"].asBool();
			}
			if (root["Intercept"].isMember("SeewoWhiteboard5CFloating") && root["Intercept"]["SeewoWhiteboard5CFloating"].isBool())
			{
				ddbSetList.InterceptWindow[2] = root["Intercept"]["SeewoWhiteboard5CFloating"].asBool();
			}
			if (root["Intercept"].isMember("SeewoPincoFloating") && root["Intercept"]["SeewoPincoFloating"].isBool())
			{
				ddbSetList.InterceptWindow[3] = ddbSetList.InterceptWindow[4] = root["Intercept"]["SeewoPincoFloating"].asBool();
			}
			if (root["Intercept"].isMember("SeewoPPTFloating") && root["Intercept"]["SeewoPPTFloating"].isBool())
			{
				ddbSetList.InterceptWindow[5] = root["Intercept"]["SeewoPPTFloating"].asBool();
			}
			if (root["Intercept"].isMember("AiClassFloating") && root["Intercept"]["AiClassFloating"].isBool())
			{
				ddbSetList.InterceptWindow[6] = root["Intercept"]["AiClassFloating"].asBool();
			}
			if (root["Intercept"].isMember("HiteAnnotationFloating") && root["Intercept"]["HiteAnnotationFloating"].isBool())
			{
				ddbSetList.InterceptWindow[7] = root["Intercept"]["HiteAnnotationFloating"].asBool();
			}
		}
	}

	readjson.close();

	return true;
}
bool DdbWriteSetting(bool change, bool close)
{
	Json::Value root;

	root["SleepTime"] = Json::Value(ddbSetList.sleepTime);

	root["Mode"]["Mode"] = Json::Value(ddbSetList.mode);
	root["Mode"]["HostPath"] = Json::Value(utf16ToUtf8(ddbSetList.hostPath));
	root["Mode"]["RestartHost"] = Json::Value(ddbSetList.restartHost);

	root["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(ddbSetList.InterceptWindow[0]);
	root["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(ddbSetList.InterceptWindow[1]);
	root["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(ddbSetList.InterceptWindow[2]);
	root["Intercept"]["SeewoPincoFloating"] = Json::Value(ddbSetList.InterceptWindow[3]);
	root["Intercept"]["SeewoPPTFloating"] = Json::Value(ddbSetList.InterceptWindow[5]);
	root["Intercept"]["AiClassFloating"] = Json::Value(ddbSetList.InterceptWindow[6]);
	root["Intercept"]["HiteAnnotationFloating"] = Json::Value(ddbSetList.InterceptWindow[7]);

	root["~ConfigurationChange"] = Json::Value(change);
	root["~KeepOpen"] = Json::Value(!close);

	Json::StreamWriterBuilder outjson;
	outjson.settings_["emitUTF8"] = true;
	std::unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
	ofstream writejson;
	writejson.imbue(locale("zh_CN.UTF8"));
	writejson.open(globalPath + L"PlugIn\\DDB\\interaction_configuration.json");
	writer->write(root, &writejson);
	writejson.close();

	return true;
}