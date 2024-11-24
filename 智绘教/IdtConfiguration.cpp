#include "IdtConfiguration.h"

#include "IdtText.h"

// 占用文件
bool OccupyFileForRead(HANDLE* hFile, const wstring& filePath)
{
	if (!filesystem::exists(filePath)) return false;

	for (int time = 1; time <= 5; time++)
	{
		*hFile = CreateFileW(
			filePath.c_str(),
			GENERIC_READ,
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
bool OccupyFileForWrite(HANDLE* hFile, const wstring& filePath)
{
	filesystem::path directoryPath = filesystem::path(filePath).parent_path();
	if (!filesystem::exists(directoryPath)) {
		error_code ec;
		filesystem::create_directories(directoryPath, ec);
	}

	for (int time = 1; time <= 5; time++)
	{
		*hFile = CreateFileW(
			filePath.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0,              // 不共享，独占访问
			NULL,
			OPEN_ALWAYS,
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

SetListStruct setlist;
bool ReadSetting()
{
	HANDLE fileHandle = NULL;
	if (!OccupyFileForRead(&fileHandle, globalPath + L"opt\\deploy.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(fileHandle, &fileSize))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
	string jsonContent = string(dwSize, '\0');

	DWORD bytesRead = 0;
	if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (!ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	UnOccupyFile(&fileHandle);

	istringstream jsonContentStream(jsonContent);
	Json::CharReaderBuilder readerBuilder;
	Json::Value updateVal;
	string jsonErr;

	if (Json::parseFromStream(readerBuilder, jsonContentStream, &updateVal, &jsonErr))
	{
		if (updateVal.isMember("SelectLanguage") && updateVal["SelectLanguage"].isBool())
			setlist.selectLanguage = updateVal["SelectLanguage"].asBool();
		if (updateVal.isMember("StartUp") && updateVal["StartUp"].isBool())
			setlist.startUp = updateVal["StartUp"].asBool();
		if (updateVal.isMember("SettingGlobalScale") && updateVal["SettingGlobalScale"].isDouble())
			setlist.settingGlobalScale = updateVal["SettingGlobalScale"].asDouble();
		if (updateVal.isMember("CorrectLnk") && updateVal["CorrectLnk"].isBool())
			setlist.correctLnk = updateVal["CorrectLnk"].asBool();
		if (updateVal.isMember("CreateLnk") && updateVal["CreateLnk"].isBool())
			setlist.createLnk = updateVal["CreateLnk"].asBool();

		if (updateVal.isMember("SetSkinMode") && updateVal["SetSkinMode"].isInt())
			setlist.SetSkinMode = updateVal["SetSkinMode"].asInt();
		if (updateVal.isMember("RightClickClose") && updateVal["RightClickClose"].isBool())
			setlist.RightClickClose = updateVal["RightClickClose"].asBool();
		if (updateVal.isMember("BrushRecover") && updateVal["BrushRecover"].isBool())
			setlist.BrushRecover = updateVal["BrushRecover"].asBool();
		if (updateVal.isMember("RubberRecover") && updateVal["RubberRecover"].isBool())
			setlist.RubberRecover = updateVal["RubberRecover"].asBool();
		if (updateVal.isMember("CompatibleTaskBarAutoHide") && updateVal["CompatibleTaskBarAutoHide"].isBool())
			setlist.compatibleTaskBarAutoHide = updateVal["CompatibleTaskBarAutoHide"].asBool();
		if (updateVal.isMember("ForceTop") && updateVal["ForceTop"].isBool())
			setlist.forceTop = updateVal["ForceTop"].asBool();

		if (updateVal.isMember("PaintDevice") && updateVal["PaintDevice"].isInt())
			setlist.paintDevice = updateVal["PaintDevice"].asInt();
		if (updateVal.isMember("LiftStraighten") && updateVal["LiftStraighten"].isBool())
			setlist.liftStraighten = updateVal["LiftStraighten"].asBool();
		if (updateVal.isMember("WaitStraighten") && updateVal["WaitStraighten"].isBool())
			setlist.waitStraighten = updateVal["WaitStraighten"].asBool();
		if (updateVal.isMember("PointAdsorption") && updateVal["PointAdsorption"].isBool())
			setlist.pointAdsorption = updateVal["PointAdsorption"].asBool();
		if (updateVal.isMember("SmoothWriting") && updateVal["SmoothWriting"].isBool())
			setlist.smoothWriting = updateVal["SmoothWriting"].asBool();
		if (updateVal.isMember("SmartEraser") && updateVal["SmartEraser"].isBool())
			setlist.smartEraser = updateVal["SmartEraser"].asBool();

		if (updateVal.isMember("BasicInfo") && updateVal["BasicInfo"].isObject())
		{
			if (updateVal["BasicInfo"].isMember("UpdateChannel") && updateVal["BasicInfo"]["UpdateChannel"].isString())
				setlist.UpdateChannel = updateVal["BasicInfo"]["UpdateChannel"].asString();
		}

		if (updateVal.isMember("PlugIn") && updateVal["PlugIn"].isObject())
		{
			if (updateVal["PlugIn"].isMember("DdbEnable") && updateVal["PlugIn"]["DdbEnable"].isBool())
				ddbInteractionSetList.DdbEnable = updateVal["PlugIn"]["DdbEnable"].asBool();
			if (updateVal["PlugIn"].isMember("DdbEnhance") && updateVal["PlugIn"]["DdbEnhance"].isBool())
				ddbInteractionSetList.DdbEnhance = updateVal["PlugIn"]["DdbEnhance"].asBool();
		}
	}
	return false;

	return true;
}
bool WriteSetting()
{
	if (_waccess((globalPath + L"opt").c_str(), 0) == -1)
	{
		error_code ec;
		filesystem::create_directory(globalPath + L"opt", ec);
	}

	Json::Value updateVal;
	{
		updateVal["SelectLanguage"] = Json::Value(setlist.selectLanguage);
		updateVal["StartUp"] = Json::Value(setlist.startUp);
		updateVal["SettingGlobalScale"] = Json::Value(setlist.settingGlobalScale);
		updateVal["CorrectLnk"] = Json::Value(setlist.correctLnk);
		updateVal["CreateLnk"] = Json::Value(setlist.createLnk);

		updateVal["SetSkinMode"] = Json::Value(setlist.SetSkinMode);
		updateVal["RightClickClose"] = Json::Value(setlist.RightClickClose);
		updateVal["BrushRecover"] = Json::Value(setlist.BrushRecover);
		updateVal["RubberRecover"] = Json::Value(setlist.RubberRecover);
		updateVal["CompatibleTaskBarAutoHide"] = Json::Value(setlist.compatibleTaskBarAutoHide);
		updateVal["ForceTop"] = Json::Value(setlist.forceTop);

		updateVal["PaintDevice"] = Json::Value(setlist.paintDevice);
		updateVal["LiftStraighten"] = Json::Value(setlist.liftStraighten);
		updateVal["WaitStraighten"] = Json::Value(setlist.waitStraighten);
		updateVal["PointAdsorption"] = Json::Value(setlist.pointAdsorption);
		updateVal["SmoothWriting"] = Json::Value(setlist.smoothWriting);
		updateVal["SmartEraser"] = Json::Value(setlist.smartEraser);

		updateVal["BasicInfo"]["UpdateChannel"] = Json::Value(setlist.UpdateChannel);
		updateVal["BasicInfo"]["edition"] = Json::Value(utf16ToUtf8(editionDate));

		updateVal["PlugIn"]["DdbEnable"] = Json::Value(ddbInteractionSetList.DdbEnable);
		updateVal["PlugIn"]["DdbEnhance"] = Json::Value(ddbInteractionSetList.DdbEnhance);
	}

	HANDLE fileHandle = NULL;
	if (!OccupyFileForWrite(&fileHandle, globalPath + L"opt\\deploy.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (!SetEndOfFile(fileHandle))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	Json::StreamWriterBuilder writerBuilder;
	string jsonContent = "\xEF\xBB\xBF" + Json::writeString(writerBuilder, updateVal);

	DWORD bytesWritten = 0;
	if (!WriteFile(fileHandle, jsonContent.data(), static_cast<DWORD>(jsonContent.size()), &bytesWritten, NULL) || bytesWritten != jsonContent.size())
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	UnOccupyFile(&fileHandle);
	return true;
}

PptComSetListStruct pptComSetlist;
bool PptComReadSetting()
{
	HANDLE fileHandle = NULL;
	if (!OccupyFileForRead(&fileHandle, globalPath + L"opt\\pptcom_configuration.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(fileHandle, &fileSize))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
	string jsonContent = string(dwSize, '\0');

	DWORD bytesRead = 0;
	if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (!ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	UnOccupyFile(&fileHandle);

	istringstream jsonContentStream(jsonContent);
	Json::CharReaderBuilder readerBuilder;
	Json::Value updateVal;
	string jsonErr;

	if (Json::parseFromStream(readerBuilder, jsonContentStream, &updateVal, &jsonErr))
	{
		if (updateVal.isMember("FixedHandWriting") && updateVal["FixedHandWriting"].isBool())
			pptComSetlist.fixedHandWriting = updateVal["FixedHandWriting"].asBool();
		if (updateVal.isMember("MemoryWidgetPosition") && updateVal["MemoryWidgetPosition"].isBool())
			pptComSetlist.memoryWidgetPosition = updateVal["MemoryWidgetPosition"].asBool();

		if (updateVal.isMember("ShowBottomBoth") && updateVal["ShowBottomBoth"].isBool())
			pptComSetlist.showBottomBoth = updateVal["ShowBottomBoth"].asBool();
		if (updateVal.isMember("ShowMiddleBoth") && updateVal["ShowMiddleBoth"].isBool())
			pptComSetlist.showMiddleBoth = updateVal["ShowMiddleBoth"].asBool();
		if (updateVal.isMember("ShowBottomMiddle") && updateVal["ShowBottomMiddle"].isBool())
			pptComSetlist.showBottomMiddle = updateVal["ShowBottomMiddle"].asBool();

		if (pptComSetlist.memoryWidgetPosition)
		{
			if (updateVal.isMember("BottomBothWidth") && updateVal["BottomBothWidth"].isDouble())
				pptComSetlist.bottomBothWidth = (float)updateVal["BottomBothWidth"].asDouble();
			if (updateVal.isMember("BottomBothHeight") && updateVal["BottomBothHeight"].isDouble())
				pptComSetlist.bottomBothHeight = (float)updateVal["BottomBothHeight"].asDouble();
			if (updateVal.isMember("MiddleBothWidth") && updateVal["MiddleBothWidth"].isDouble())
				pptComSetlist.middleBothWidth = (float)updateVal["MiddleBothWidth"].asDouble();
			if (updateVal.isMember("MiddleBothHeight") && updateVal["MiddleBothHeight"].isDouble())
				pptComSetlist.middleBothHeight = (float)updateVal["MiddleBothHeight"].asDouble();
			if (updateVal.isMember("BottomMiddleWidth") && updateVal["BottomMiddleWidth"].isDouble())
				pptComSetlist.bottomMiddleWidth = (float)updateVal["BottomMiddleWidth"].asDouble();
			if (updateVal.isMember("BottomMiddleHeight") && updateVal["BottomMiddleHeight"].isDouble())
				pptComSetlist.bottomMiddleHeight = (float)updateVal["BottomMiddleHeight"].asDouble();
		}

		if (updateVal.isMember("BottomSideBothWidgetScale") && updateVal["BottomSideBothWidgetScale"].isDouble())
			pptComSetlist.bottomSideBothWidgetScale = (float)updateVal["BottomSideBothWidgetScale"].asDouble();
		if (updateVal.isMember("BottomSideMiddleWidgetScale") && updateVal["BottomSideMiddleWidgetScale"].isDouble())
			pptComSetlist.bottomSideMiddleWidgetScale = (float)updateVal["BottomSideMiddleWidgetScale"].asDouble();
		if (updateVal.isMember("MiddleSideBothWidgetScale") && updateVal["MiddleSideBothWidgetScale"].isDouble())
			pptComSetlist.middleSideBothWidgetScale = (float)updateVal["MiddleSideBothWidgetScale"].asDouble();

		if (updateVal.isMember("AutoKillWpsProcess") && updateVal["AutoKillWpsProcess"].isBool())
			pptComSetlist.autoKillWpsProcess = updateVal["AutoKillWpsProcess"].asBool();
	}
	else return false;

	return true;
}
bool PptComWriteSetting()
{
	if (_waccess((globalPath + L"opt").c_str(), 0) == -1)
	{
		error_code ec;
		filesystem::create_directory(globalPath + L"opt", ec);
	}

	Json::Value updateVal;
	{
		updateVal["FixedHandWriting"] = Json::Value(pptComSetlist.fixedHandWriting);
		updateVal["MemoryWidgetPosition"] = Json::Value(pptComSetlist.memoryWidgetPosition);

		updateVal["ShowBottomBoth"] = Json::Value(pptComSetlist.showBottomBoth);
		updateVal["ShowMiddleBoth"] = Json::Value(pptComSetlist.showMiddleBoth);
		updateVal["ShowBottomMiddle"] = Json::Value(pptComSetlist.showBottomMiddle);

		updateVal["BottomBothWidth"] = Json::Value(pptComSetlist.bottomBothWidth);
		updateVal["BottomBothHeight"] = Json::Value(pptComSetlist.bottomBothHeight);
		updateVal["MiddleBothWidth"] = Json::Value(pptComSetlist.middleBothWidth);
		updateVal["MiddleBothHeight"] = Json::Value(pptComSetlist.middleBothHeight);
		updateVal["BottomMiddleWidth"] = Json::Value(pptComSetlist.bottomMiddleWidth);
		updateVal["BottomMiddleHeight"] = Json::Value(pptComSetlist.bottomMiddleHeight);

		updateVal["BottomSideBothWidgetScale"] = Json::Value(pptComSetlist.bottomSideBothWidgetScale);
		updateVal["BottomSideMiddleWidgetScale"] = Json::Value(pptComSetlist.bottomSideMiddleWidgetScale);
		updateVal["MiddleSideBothWidgetScale"] = Json::Value(pptComSetlist.middleSideBothWidgetScale);

		updateVal["AutoKillWpsProcess"] = Json::Value(pptComSetlist.autoKillWpsProcess);
	}

	HANDLE fileHandle = NULL;
	if (!OccupyFileForWrite(&fileHandle, globalPath + L"opt\\pptcom_configuration.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (!SetEndOfFile(fileHandle))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	Json::StreamWriterBuilder writerBuilder;
	string jsonContent = "\xEF\xBB\xBF" + Json::writeString(writerBuilder, updateVal);

	DWORD bytesWritten = 0;
	if (!WriteFile(fileHandle, jsonContent.data(), static_cast<DWORD>(jsonContent.size()), &bytesWritten, NULL) || bytesWritten != jsonContent.size())
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	UnOccupyFile(&fileHandle);
	return true;
}

DdbInteractionSetListStruct ddbInteractionSetList;
bool DdbReadInteraction()
{
	HANDLE fileHandle = NULL;
	if (!OccupyFileForRead(&fileHandle, dataPath + L"\\DesktopDrawpadBlocker\\interaction_configuration.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(fileHandle, &fileSize))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
	string jsonContent = string(dwSize, '\0');

	DWORD bytesRead = 0;
	if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (!ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	UnOccupyFile(&fileHandle);

	istringstream jsonContentStream(jsonContent);
	Json::CharReaderBuilder readerBuilder;
	Json::Value updateVal;
	string jsonErr;

	if (Json::parseFromStream(readerBuilder, jsonContentStream, &updateVal, &jsonErr))
	{
		if (updateVal.isMember("SleepTime") && updateVal["SleepTime"].isInt())
		{
			ddbInteractionSetList.sleepTime = updateVal["SleepTime"].asInt();
		}

		if (updateVal.isMember("Mode") && updateVal["Mode"].isObject())
		{
			if (updateVal["Mode"].isMember("Mode") && updateVal["Mode"]["Mode"].isInt())
			{
				ddbInteractionSetList.mode = updateVal["Mode"]["Mode"].asInt();
			}
			if (updateVal["Mode"].isMember("RestartHost") && updateVal["Mode"]["RestartHost"].isBool())
			{
				ddbInteractionSetList.restartHost = updateVal["Mode"]["RestartHost"].asBool();
			}
		}

		if (updateVal.isMember("Intercept") && updateVal["Intercept"].isObject())
		{
			if (updateVal["Intercept"].isMember("SeewoWhiteboard3Floating") && updateVal["Intercept"]["SeewoWhiteboard3Floating"].isBool())
			{
				ddbInteractionSetList.InterceptWindow[0] = updateVal["Intercept"]["SeewoWhiteboard3Floating"].asBool();
			}
			if (updateVal["Intercept"].isMember("SeewoWhiteboard5Floating") && updateVal["Intercept"]["SeewoWhiteboard5Floating"].isBool())
			{
				ddbInteractionSetList.InterceptWindow[1] = updateVal["Intercept"]["SeewoWhiteboard5Floating"].asBool();
			}
			if (updateVal["Intercept"].isMember("SeewoWhiteboard5CFloating") && updateVal["Intercept"]["SeewoWhiteboard5CFloating"].isBool())
			{
				ddbInteractionSetList.InterceptWindow[2] = updateVal["Intercept"]["SeewoWhiteboard5CFloating"].asBool();
			}
			if (updateVal["Intercept"].isMember("SeewoPincoFloating") && updateVal["Intercept"]["SeewoPincoFloating"].isBool())
			{
				ddbInteractionSetList.InterceptWindow[3] = ddbInteractionSetList.InterceptWindow[4] = updateVal["Intercept"]["SeewoPincoFloating"].asBool();
			}
			if (updateVal["Intercept"].isMember("SeewoPPTFloating") && updateVal["Intercept"]["SeewoPPTFloating"].isBool())
			{
				ddbInteractionSetList.InterceptWindow[5] = updateVal["Intercept"]["SeewoPPTFloating"].asBool();
			}
			if (updateVal["Intercept"].isMember("AiClassFloating") && updateVal["Intercept"]["AiClassFloating"].isBool())
			{
				ddbInteractionSetList.InterceptWindow[6] = updateVal["Intercept"]["AiClassFloating"].asBool();
			}
			if (updateVal["Intercept"].isMember("HiteAnnotationFloating") && updateVal["Intercept"]["HiteAnnotationFloating"].isBool())
			{
				ddbInteractionSetList.InterceptWindow[7] = updateVal["Intercept"]["HiteAnnotationFloating"].asBool();
			}
		}
	}
	else return false;

	return true;
}
bool DdbWriteInteraction(bool change, bool close)
{
	Json::Value updateVal;
	{
		updateVal["SleepTime"] = Json::Value(ddbInteractionSetList.sleepTime);

		updateVal["Mode"]["Mode"] = Json::Value(ddbInteractionSetList.mode);
		updateVal["Mode"]["HostPath"] = Json::Value(utf16ToUtf8(ddbInteractionSetList.hostPath));
		updateVal["Mode"]["RestartHost"] = Json::Value(ddbInteractionSetList.restartHost);

		updateVal["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(ddbInteractionSetList.InterceptWindow[0]);
		updateVal["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(ddbInteractionSetList.InterceptWindow[1]);
		updateVal["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(ddbInteractionSetList.InterceptWindow[2]);
		updateVal["Intercept"]["SeewoPincoFloating"] = Json::Value(ddbInteractionSetList.InterceptWindow[3]);
		updateVal["Intercept"]["SeewoPPTFloating"] = Json::Value(ddbInteractionSetList.InterceptWindow[5]);
		updateVal["Intercept"]["AiClassFloating"] = Json::Value(ddbInteractionSetList.InterceptWindow[6]);
		updateVal["Intercept"]["HiteAnnotationFloating"] = Json::Value(ddbInteractionSetList.InterceptWindow[7]);

		updateVal["~ConfigurationChange"] = Json::Value(change);
		updateVal["~KeepOpen"] = Json::Value(!close);
	}

	HANDLE fileHandle = NULL;
	if (!OccupyFileForWrite(&fileHandle, dataPath + L"\\DesktopDrawpadBlocker\\interaction_configuration.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
	if (!SetEndOfFile(fileHandle))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	Json::StreamWriterBuilder writerBuilder;
	string jsonContent = "\xEF\xBB\xBF" + Json::writeString(writerBuilder, updateVal);

	DWORD bytesWritten = 0;
	if (!WriteFile(fileHandle, jsonContent.data(), static_cast<DWORD>(jsonContent.size()), &bytesWritten, NULL) || bytesWritten != jsonContent.size())
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	UnOccupyFile(&fileHandle);
	return true;
}