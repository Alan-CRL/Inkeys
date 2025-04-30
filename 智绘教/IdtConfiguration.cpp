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

		if (updateVal.isMember("SetSkinMode") && updateVal["SetSkinMode"].isInt())
			setlist.SetSkinMode = updateVal["SetSkinMode"].asInt();

		if (updateVal.isMember("TopSleepTime") && updateVal["TopSleepTime"].isInt())
			setlist.topSleepTime = updateVal["TopSleepTime"].asInt();
		if (updateVal.isMember("RightClickClose") && updateVal["RightClickClose"].isBool())
			setlist.RightClickClose = updateVal["RightClickClose"].asBool();
		{
			if (updateVal.isMember("BrushRecover") && updateVal["BrushRecover"].isBool())
				setlist.BrushRecover = updateVal["BrushRecover"].asBool();
			if (updateVal.isMember("RubberRecover") && updateVal["RubberRecover"].isBool())
				setlist.RubberRecover = updateVal["RubberRecover"].asBool();
		}
		if (updateVal.isMember("Regular") && updateVal["Regular"].isObject())
		{
			{
				if (updateVal["Regular"].isMember("MoveRecover") && updateVal["Regular"]["MoveRecover"].isBool())
					setlist.regularSetting.moveRecover = updateVal["Regular"]["MoveRecover"].asBool();
				if (updateVal["Regular"].isMember("ClickRecover") && updateVal["Regular"]["ClickRecover"].isBool())
					setlist.regularSetting.clickRecover = updateVal["Regular"]["ClickRecover"].asBool();
			}

			if (updateVal["Regular"].isMember("AvoidFullScreen") && updateVal["Regular"]["AvoidFullScreen"].isBool())
				setlist.regularSetting.avoidFullScreen = updateVal["Regular"]["AvoidFullScreen"].asBool();
			if (updateVal["Regular"].isMember("TeachingSafetyMode") && updateVal["Regular"]["TeachingSafetyMode"].isInt())
				setlist.regularSetting.teachingSafetyMode = updateVal["Regular"]["TeachingSafetyMode"].asInt();
		}

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
		if (updateVal.isMember("EraserSetting") && updateVal["EraserSetting"].isObject())
		{
			if (updateVal["EraserSetting"].isMember("EraserMode") && updateVal["EraserSetting"]["EraserMode"].isInt())
				setlist.eraserSetting.eraserMode = updateVal["EraserSetting"]["EraserMode"].asInt();
			if (updateVal["EraserSetting"].isMember("EraserSize") && updateVal["EraserSetting"]["EraserSize"].isInt())
				setlist.eraserSetting.eraserSize = updateVal["EraserSetting"]["EraserSize"].asInt();
		}
		if (updateVal.isMember("HideTouchPointerBeta") && updateVal["HideTouchPointerBeta"].isBool())
			setlist.hideTouchPointer = updateVal["HideTouchPointerBeta"].asBool();

		if (updateVal.isMember("Performance") && updateVal["Performance"].isObject())
		{
			if (updateVal["Performance"].isMember("PreparationQuantity") && updateVal["Performance"]["PreparationQuantity"].isInt())
				setlist.performanceSetting.preparationQuantity = updateVal["Performance"]["PreparationQuantity"].asInt();

			if (updateVal["Performance"].isMember("SuperDrawBeta") && updateVal["Performance"]["SuperDrawBeta"].isBool())
				setlist.performanceSetting.superDraw = updateVal["Performance"]["SuperDrawBeta"].asBool();
		}

		if (updateVal.isMember("UpdateSetting") && updateVal["UpdateSetting"].isObject())
		{
			if (updateVal["UpdateSetting"].isMember("EnableAutoUpdate") && updateVal["UpdateSetting"]["EnableAutoUpdate"].isBool())
				setlist.enableAutoUpdate = updateVal["UpdateSetting"]["EnableAutoUpdate"].asBool();
			if (updateVal["UpdateSetting"].isMember("UpdateChannel") && updateVal["UpdateSetting"]["UpdateChannel"].isString())
				setlist.UpdateChannel = updateVal["UpdateSetting"]["UpdateChannel"].asString();
			if (updateVal["UpdateSetting"].isMember("UpdateArchitecture") && updateVal["UpdateSetting"]["UpdateArchitecture"].isString())
			{
				setlist.updateArchitecture = updateVal["UpdateSetting"]["UpdateArchitecture"].asString();
				if (setlist.updateArchitecture != "win32" && setlist.updateArchitecture != "win64" && setlist.updateArchitecture != "arm64")
					setlist.updateArchitecture = "win32";
			}
		}

		if (updateVal.isMember("PlugIn") && updateVal["PlugIn"].isObject())
		{
			if (updateVal["PlugIn"].isMember("DesktopDrawpadBlocker") && updateVal["PlugIn"]["DesktopDrawpadBlocker"].isObject())
			{
				if (updateVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("Enable") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Enable"].isBool())
					ddbInteractionSetList.enable = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Enable"].asBool();
				if (updateVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("RunAsAdmin") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["RunAsAdmin"].isBool())
					ddbInteractionSetList.runAsAdmin = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["RunAsAdmin"].asBool();
				if (updateVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("SleepTime") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["SleepTime"].isInt())
					ddbInteractionSetList.sleepTime = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["SleepTime"].asInt();

				if (updateVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("Intercept") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isObject())
				{
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard3Floating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"].isBool())
						ddbInteractionSetList.intercept.seewoWhiteboard3Floating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard5Floating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"].isBool())
						ddbInteractionSetList.intercept.seewoWhiteboard5Floating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard5CFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"].isBool())
						ddbInteractionSetList.intercept.seewoWhiteboard5CFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPincoSideBarFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"].isBool())
						ddbInteractionSetList.intercept.seewoPincoSideBarFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPincoDrawingFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"].isBool())
						ddbInteractionSetList.intercept.seewoPincoDrawingFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPPTFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"].isBool())
						ddbInteractionSetList.intercept.seewoPPTFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("AiClassFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"].isBool())
						ddbInteractionSetList.intercept.aiClassFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("HiteAnnotationFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["HiteAnnotationFloating"].isBool())
						ddbInteractionSetList.intercept.hiteAnnotationFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["HiteAnnotationFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("ChangYanFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"].isBool())
						ddbInteractionSetList.intercept.changYanFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("ChangYanPptFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanPptFloating"].isBool())
						ddbInteractionSetList.intercept.changYanPptFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanPptFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("IntelligentClassFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"].isBool())
						ddbInteractionSetList.intercept.intelligentClassFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoDesktopAnnotationFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopAnnotationFloating"].isBool())
						ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopAnnotationFloating"].asBool();
					if (updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoDesktopSideBarFloating") && updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"].isBool())
						ddbInteractionSetList.intercept.seewoDesktopSideBarFloating = updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"].asBool();
				}
			}
			if (updateVal["PlugIn"].isMember("ShortcutAssistant") && updateVal["PlugIn"]["ShortcutAssistant"].isObject())
			{
				if (updateVal["PlugIn"]["ShortcutAssistant"].isMember("CorrectLnk") && updateVal["PlugIn"]["ShortcutAssistant"]["CorrectLnk"].isBool())
					setlist.shortcutAssistant.correctLnk = updateVal["PlugIn"]["ShortcutAssistant"]["CorrectLnk"].asBool();
				if (updateVal["PlugIn"]["ShortcutAssistant"].isMember("CreateLnk") && updateVal["PlugIn"]["ShortcutAssistant"]["CreateLnk"].isBool())
					setlist.shortcutAssistant.createLnk = updateVal["PlugIn"]["ShortcutAssistant"]["CreateLnk"].asBool();
			}
		}
		if (updateVal.isMember("Component") && updateVal["Component"].isObject())
		{
			// shortcutButton
			if (updateVal["Component"].isMember("ShortcutButton") && updateVal["Component"]["ShortcutButton"].isObject())
			{
				// appliance
				if (updateVal["Component"]["ShortcutButton"].isMember("Appliance") && updateVal["Component"]["ShortcutButton"]["Appliance"].isObject())
				{
					if (updateVal["Component"]["ShortcutButton"]["Appliance"].isMember("Explorer") && updateVal["Component"]["ShortcutButton"]["Appliance"]["Explorer"].isBool())
						setlist.component.shortcutButton.appliance.explorer = updateVal["Component"]["ShortcutButton"]["Appliance"]["Explorer"].asBool();
					if (updateVal["Component"]["ShortcutButton"]["Appliance"].isMember("Taskmgr") && updateVal["Component"]["ShortcutButton"]["Appliance"]["Taskmgr"].isBool())
						setlist.component.shortcutButton.appliance.taskmgr = updateVal["Component"]["ShortcutButton"]["Appliance"]["Taskmgr"].asBool();
					if (updateVal["Component"]["ShortcutButton"]["Appliance"].isMember("Control") && updateVal["Component"]["ShortcutButton"]["Appliance"]["Control"].isBool())
						setlist.component.shortcutButton.appliance.control = updateVal["Component"]["ShortcutButton"]["Appliance"]["Control"].asBool();
				}
				// system
				if (updateVal["Component"]["ShortcutButton"].isMember("System") && updateVal["Component"]["ShortcutButton"]["System"].isObject())
				{
					if (updateVal["Component"]["ShortcutButton"]["System"].isMember("Desktop") && updateVal["Component"]["ShortcutButton"]["System"]["Desktop"].isBool())
						setlist.component.shortcutButton.system.desktop = updateVal["Component"]["ShortcutButton"]["System"]["Desktop"].asBool();
					if (updateVal["Component"]["ShortcutButton"]["System"].isMember("LockWorkStation") && updateVal["Component"]["ShortcutButton"]["System"]["LockWorkStation"].isBool())
						setlist.component.shortcutButton.system.lockWorkStation = updateVal["Component"]["ShortcutButton"]["System"]["LockWorkStation"].asBool();
				}
				// keyboard
				if (updateVal["Component"]["ShortcutButton"].isMember("Keyboard") && updateVal["Component"]["ShortcutButton"]["Keyboard"].isObject())
				{
					if (updateVal["Component"]["ShortcutButton"]["Keyboard"].isMember("Keyboardesc") && updateVal["Component"]["ShortcutButton"]["Keyboard"]["Keyboardesc"].isBool())
						setlist.component.shortcutButton.keyboard.keyboardesc = updateVal["Component"]["ShortcutButton"]["Keyboard"]["Keyboardesc"].asBool();
					if (updateVal["Component"]["ShortcutButton"]["Keyboard"].isMember("KeyboardAltF4") && updateVal["Component"]["ShortcutButton"]["Keyboard"]["KeyboardAltF4"].isBool())
						setlist.component.shortcutButton.keyboard.keyboardAltF4 = updateVal["Component"]["ShortcutButton"]["Keyboard"]["KeyboardAltF4"].asBool();
				}
				// linkage
				if (updateVal["Component"]["ShortcutButton"].isMember("Linkage") && updateVal["Component"]["ShortcutButton"]["Linkage"].isObject())
				{
					if (updateVal["Component"]["ShortcutButton"]["Linkage"].isMember("ClassislandSettings") && updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandSettings"].isBool())
						setlist.component.shortcutButton.linkage.classislandSettings = updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandSettings"].asBool();
					if (updateVal["Component"]["ShortcutButton"]["Linkage"].isMember("ClassislandProfile") && updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandProfile"].isBool())
						setlist.component.shortcutButton.linkage.classislandProfile = updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandProfile"].asBool();
					if (updateVal["Component"]["ShortcutButton"]["Linkage"].isMember("ClassislandClassswap") && updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandClassswap"].isBool())
						setlist.component.shortcutButton.linkage.classislandClassswap = updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandClassswap"].asBool();
					if (updateVal["Component"]["ShortcutButton"]["Linkage"].isMember("ClassislandIslandCaller") && updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandIslandCaller"].isBool())
						setlist.component.shortcutButton.linkage.classislandIslandCaller = updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandIslandCaller"].asBool();
				}
			}
		}
	}
	else return false;

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

		updateVal["SetSkinMode"] = Json::Value(setlist.SetSkinMode);
		updateVal["TopSleepTime"] = Json::Value(setlist.topSleepTime);
		updateVal["RightClickClose"] = Json::Value(setlist.RightClickClose);
		{
			updateVal["BrushRecover"] = Json::Value(setlist.BrushRecover);
			updateVal["RubberRecover"] = Json::Value(setlist.RubberRecover);
		}
		{
			updateVal["Regular"]["MoveRecover"] = Json::Value(setlist.regularSetting.moveRecover);
			updateVal["Regular"]["ClickRecover"] = Json::Value(setlist.regularSetting.clickRecover);
		}
		updateVal["Regular"]["AvoidFullScreen"] = Json::Value(setlist.regularSetting.avoidFullScreen);
		updateVal["Regular"]["TeachingSafetyMode"] = Json::Value(setlist.regularSetting.teachingSafetyMode);

		updateVal["PaintDevice"] = Json::Value(setlist.paintDevice);
		updateVal["LiftStraighten"] = Json::Value(setlist.liftStraighten);
		updateVal["WaitStraighten"] = Json::Value(setlist.waitStraighten);
		updateVal["PointAdsorption"] = Json::Value(setlist.pointAdsorption);
		updateVal["SmoothWriting"] = Json::Value(setlist.smoothWriting);
		{
			updateVal["EraserSetting"]["EraserMode"] = Json::Value(setlist.eraserSetting.eraserMode);
			updateVal["EraserSetting"]["EraserSize"] = Json::Value(setlist.eraserSetting.eraserSize);
		}
		updateVal["HideTouchPointerBeta"] = Json::Value(setlist.hideTouchPointer);

		{
			updateVal["Performance"]["PreparationQuantity"] = Json::Value(setlist.performanceSetting.preparationQuantity);

			updateVal["Performance"]["SuperDrawBeta"] = Json::Value(setlist.performanceSetting.superDraw);
		}

		{
			updateVal["UpdateSetting"]["EnableAutoUpdate"] = Json::Value(setlist.enableAutoUpdate);
			updateVal["UpdateSetting"]["UpdateChannel"] = Json::Value(setlist.UpdateChannel);
			updateVal["UpdateSetting"]["UpdateArchitecture"] = Json::Value(setlist.updateArchitecture);
		}
		{
			updateVal["BasicInfo"]["UserID"] = Json::Value(utf16ToUtf8(userId));
			updateVal["BasicInfo"]["Edition"] = Json::Value(utf16ToUtf8(editionDate));
		}

		{
			{
				updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Enable"] = Json::Value(ddbInteractionSetList.enable);
				updateVal["PlugIn"]["DesktopDrawpadBlocker"]["RunAsAdmin"] = Json::Value(ddbInteractionSetList.runAsAdmin);
				updateVal["PlugIn"]["DesktopDrawpadBlocker"]["SleepTime"] = Json::Value(ddbInteractionSetList.sleepTime);

				{
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard3Floating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard5Floating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard5CFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPincoSideBarFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPincoDrawingFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPPTFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"] = Json::Value(ddbInteractionSetList.intercept.aiClassFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["HiteAnnotationFloating"] = Json::Value(ddbInteractionSetList.intercept.hiteAnnotationFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"] = Json::Value(ddbInteractionSetList.intercept.changYanFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanPptFloating"] = Json::Value(ddbInteractionSetList.intercept.changYanPptFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"] = Json::Value(ddbInteractionSetList.intercept.intelligentClassFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopAnnotationFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating);
					updateVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoDesktopSideBarFloating);
				}
			}

			{
				updateVal["PlugIn"]["ShortcutAssistant"]["CorrectLnk"] = Json::Value(setlist.shortcutAssistant.correctLnk);
				updateVal["PlugIn"]["ShortcutAssistant"]["CreateLnk"] = Json::Value(setlist.shortcutAssistant.createLnk);
			}
		}
		{
			// shortcutButton
			{
				// appliance
				{
					updateVal["Component"]["ShortcutButton"]["Appliance"]["Explorer"] = Json::Value(setlist.component.shortcutButton.appliance.explorer);
					updateVal["Component"]["ShortcutButton"]["Appliance"]["Taskmgr"] = Json::Value(setlist.component.shortcutButton.appliance.taskmgr);
					updateVal["Component"]["ShortcutButton"]["Appliance"]["Control"] = Json::Value(setlist.component.shortcutButton.appliance.control);
				}
				// system
				{
					updateVal["Component"]["ShortcutButton"]["System"]["Desktop"] = Json::Value(setlist.component.shortcutButton.system.desktop);
					updateVal["Component"]["ShortcutButton"]["System"]["LockWorkStation"] = Json::Value(setlist.component.shortcutButton.system.lockWorkStation);
				}
				// keyboard
				{
					updateVal["Component"]["ShortcutButton"]["Keyboard"]["Keyboardesc"] = Json::Value(setlist.component.shortcutButton.keyboard.keyboardesc);
					updateVal["Component"]["ShortcutButton"]["Keyboard"]["KeyboardAltF4"] = Json::Value(setlist.component.shortcutButton.keyboard.keyboardAltF4);
				}
				// linkage
				{
					updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandSettings"] = Json::Value(setlist.component.shortcutButton.linkage.classislandSettings);
					updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandProfile"] = Json::Value(setlist.component.shortcutButton.linkage.classislandProfile);
					updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandClassswap"] = Json::Value(setlist.component.shortcutButton.linkage.classislandClassswap);
					updateVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandIslandCaller"] = Json::Value(setlist.component.shortcutButton.linkage.classislandIslandCaller);
				}
			}
		}
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
//bool DdbReadInteraction()
//{
//	HANDLE fileHandle = NULL;
//	if (!OccupyFileForRead(&fileHandle, dataPath + L"\\DesktopDrawpadBlocker\\interaction_configuration.json"))
//	{
//		UnOccupyFile(&fileHandle);
//		return false;
//	}
//
//	LARGE_INTEGER fileSize;
//	if (!GetFileSizeEx(fileHandle, &fileSize))
//	{
//		UnOccupyFile(&fileHandle);
//		return false;
//	}
//
//	DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
//	string jsonContent = string(dwSize, '\0');
//
//	DWORD bytesRead = 0;
//	if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
//	{
//		UnOccupyFile(&fileHandle);
//		return false;
//	}
//	if (!ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)
//	{
//		UnOccupyFile(&fileHandle);
//		return false;
//	}
//
//	if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
//	UnOccupyFile(&fileHandle);
//
//	istringstream jsonContentStream(jsonContent);
//	Json::CharReaderBuilder readerBuilder;
//	Json::Value updateVal;
//	string jsonErr;
//
//	if (Json::parseFromStream(readerBuilder, jsonContentStream, &updateVal, &jsonErr))
//	{
//		if (updateVal.isMember("SleepTime") && updateVal["SleepTime"].isInt())
//		{
//			ddbInteractionSetList.sleepTime = updateVal["SleepTime"].asInt();
//		}
//
//		if (updateVal.isMember("Mode") && updateVal["Mode"].isObject())
//		{
//			if (updateVal["Mode"].isMember("Mode") && updateVal["Mode"]["Mode"].isInt())
//			{
//				ddbInteractionSetList.mode = updateVal["Mode"]["Mode"].asInt();
//			}
//			if (updateVal["Mode"].isMember("RestartHost") && updateVal["Mode"]["RestartHost"].isBool())
//			{
//				ddbInteractionSetList.restartHost = updateVal["Mode"]["RestartHost"].asBool();
//			}
//		}
//
//		if (updateVal.isMember("Intercept") && updateVal["Intercept"].isObject())
//		{
//			if (updateVal["Intercept"].isMember("SeewoWhiteboard3Floating") && updateVal["Intercept"]["SeewoWhiteboard3Floating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[0] = updateVal["Intercept"]["SeewoWhiteboard3Floating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("SeewoWhiteboard5Floating") && updateVal["Intercept"]["SeewoWhiteboard5Floating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[1] = updateVal["Intercept"]["SeewoWhiteboard5Floating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("SeewoWhiteboard5CFloating") && updateVal["Intercept"]["SeewoWhiteboard5CFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[2] = updateVal["Intercept"]["SeewoWhiteboard5CFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("SeewoPincoSideBarFloating") && updateVal["Intercept"]["SeewoPincoSideBarFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[3] = updateVal["Intercept"]["SeewoPincoSideBarFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("SeewoPincoDrawingFloating") && updateVal["Intercept"]["SeewoPincoDrawingFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[4] = updateVal["Intercept"]["SeewoPincoDrawingFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("SeewoPPTFloating") && updateVal["Intercept"]["SeewoPPTFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[5] = updateVal["Intercept"]["SeewoPPTFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("AiClassFloating") && updateVal["Intercept"]["AiClassFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[6] = updateVal["Intercept"]["AiClassFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("HiteAnnotationFloating") && updateVal["Intercept"]["HiteAnnotationFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[7] = updateVal["Intercept"]["HiteAnnotationFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("ChangYanFloating") && updateVal["Intercept"]["ChangYanFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[8] = updateVal["Intercept"]["ChangYanFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("IntelligentClassFloating") && updateVal["Intercept"]["IntelligentClassFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[9] = updateVal["Intercept"]["IntelligentClassFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("SeewoDesktopAnnotationFloating") && updateVal["Intercept"]["SeewoDesktopAnnotationFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[10] = updateVal["Intercept"]["SeewoDesktopAnnotationFloating"].asBool();
//			}
//			if (updateVal["Intercept"].isMember("SeewoDesktopSideBarFloating") && updateVal["Intercept"]["SeewoDesktopSideBarFloating"].isBool())
//			{
//				ddbInteractionSetList.InterceptWindow[11] = updateVal["Intercept"]["SeewoDesktopSideBarFloating"].asBool();
//			}
//		}
//	}
//	else return false;
//
//	return true;
//}
bool DdbWriteInteraction(bool change, bool close)
{
	Json::Value updateVal;
	{
		updateVal["SleepTime"] = Json::Value(ddbInteractionSetList.sleepTime);

		updateVal["Mode"]["Mode"] = Json::Value(ddbInteractionSetList.mode);
		updateVal["Mode"]["HostPath"] = Json::Value(utf16ToUtf8(ddbInteractionSetList.hostPath));
		updateVal["Mode"]["RestartHost"] = Json::Value(ddbInteractionSetList.restartHost);

		{
			updateVal["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard3Floating);
			updateVal["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard5Floating);
			updateVal["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard5CFloating);
			updateVal["Intercept"]["SeewoPincoSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPincoSideBarFloating);
			updateVal["Intercept"]["SeewoPincoDrawingFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPincoDrawingFloating);
			updateVal["Intercept"]["SeewoPPTFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPPTFloating);
			updateVal["Intercept"]["AiClassFloating"] = Json::Value(ddbInteractionSetList.intercept.aiClassFloating);
			updateVal["Intercept"]["HiteAnnotationFloating"] = Json::Value(ddbInteractionSetList.intercept.hiteAnnotationFloating);
			updateVal["Intercept"]["ChangYanFloating"] = Json::Value(ddbInteractionSetList.intercept.changYanFloating);
			updateVal["Intercept"]["ChangYanPptFloating"] = Json::Value(ddbInteractionSetList.intercept.changYanPptFloating);
			updateVal["Intercept"]["IntelligentClassFloating"] = Json::Value(ddbInteractionSetList.intercept.intelligentClassFloating);
			updateVal["Intercept"]["SeewoDesktopAnnotationFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating);
			updateVal["Intercept"]["SeewoDesktopSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoDesktopSideBarFloating);
		}

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