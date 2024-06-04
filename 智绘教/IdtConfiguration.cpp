#include "IdtConfiguration.h"

#include "IdtText.h"

SetListStruct setlist;

PptComSetListStruct pptComSetlist;

DdbSetListStruct ddbSetList;
bool DdbReadSetting()
{
	if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\interaction_configuration.json").c_str(), 4) == -1) return false;

	Json::Reader reader;
	Json::Value root;

	ifstream readjson;
	readjson.imbue(locale("zh_CN.UTF8"));
	readjson.open(WstringToString(StringToWstring(globalPath) + L"PlugIn\\DDB\\interaction_configuration.json").c_str());

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
	root["Mode"]["HostPath"] = Json::Value(WstringToString(ddbSetList.hostPath));
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
	writejson.open(WstringToString(StringToWstring(globalPath) + L"PlugIn\\DDB\\interaction_configuration.json").c_str());
	writer->write(root, &writejson);
	writejson.close();

	return true;
}