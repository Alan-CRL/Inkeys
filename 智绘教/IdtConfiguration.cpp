#include "IdtConfiguration.h"

#include "IdtText.h"
#include "IdtState.h"

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
Json::Value setlistVal;
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
	string jsonErr;

	if (Json::parseFromStream(readerBuilder, jsonContentStream, &setlistVal, &jsonErr))
	{
		if (setlistVal.isMember("ConfigurationSetting") && setlistVal["ConfigurationSetting"].isObject())
		{
			if (setlistVal["ConfigurationSetting"].isMember("Enable") && setlistVal["ConfigurationSetting"]["Enable"].isBool())
				setlist.configurationSetting.enable = setlistVal["ConfigurationSetting"]["Enable"].asBool();
		}

		if (setlistVal.isMember("SelectLanguage") && setlistVal["SelectLanguage"].isInt())
			setlist.selectLanguage = setlistVal["SelectLanguage"].asInt();
		if (setlistVal.isMember("StartUp") && setlistVal["StartUp"].isBool())
			setlist.startUp = setlistVal["StartUp"].asBool();
		if (setlistVal.isMember("SettingGlobalScale") && setlistVal["SettingGlobalScale"].isDouble())
			setlist.settingGlobalScale = setlistVal["SettingGlobalScale"].asDouble();

		if (setlistVal.isMember("SetSkinMode") && setlistVal["SetSkinMode"].isInt())
			setlist.SetSkinMode = setlistVal["SetSkinMode"].asInt();

		if (setlistVal.isMember("TopSleepTime") && setlistVal["TopSleepTime"].isInt())
			setlist.topSleepTime = setlistVal["TopSleepTime"].asInt();
		if (setlistVal.isMember("RightClickClose") && setlistVal["RightClickClose"].isBool())
			setlist.RightClickClose = setlistVal["RightClickClose"].asBool();
		{
			if (setlistVal.isMember("BrushRecover") && setlistVal["BrushRecover"].isBool())
				setlist.BrushRecover = setlistVal["BrushRecover"].asBool();
			if (setlistVal.isMember("RubberRecover") && setlistVal["RubberRecover"].isBool())
				setlist.RubberRecover = setlistVal["RubberRecover"].asBool();
		}
		if (setlistVal.isMember("Regular") && setlistVal["Regular"].isObject())
		{
			{
				if (setlistVal["Regular"].isMember("MoveRecover") && setlistVal["Regular"]["MoveRecover"].isBool())
					setlist.regularSetting.moveRecover = setlistVal["Regular"]["MoveRecover"].asBool();
				if (setlistVal["Regular"].isMember("ClickRecover") && setlistVal["Regular"]["ClickRecover"].isBool())
					setlist.regularSetting.clickRecover = setlistVal["Regular"]["ClickRecover"].asBool();
			}

			if (setlistVal["Regular"].isMember("AvoidFullScreen") && setlistVal["Regular"]["AvoidFullScreen"].isBool())
				setlist.regularSetting.avoidFullScreen = setlistVal["Regular"]["AvoidFullScreen"].asBool();
			if (setlistVal["Regular"].isMember("TeachingSafetyMode") && setlistVal["Regular"]["TeachingSafetyMode"].isInt())
				setlist.regularSetting.teachingSafetyMode = setlistVal["Regular"]["TeachingSafetyMode"].asInt();
		}

		if (setlistVal.isMember("PaintDevice") && setlistVal["PaintDevice"].isInt())
			setlist.paintDevice = setlistVal["PaintDevice"].asInt();
		if (setlistVal.isMember("DisableRTS") && setlistVal["DisableRTS"].isBool())
			setlist.disableRTS = setlistVal["DisableRTS"].asBool();
		if (setlistVal.isMember("LiftStraighten") && setlistVal["LiftStraighten"].isBool())
			setlist.liftStraighten = setlistVal["LiftStraighten"].asBool();
		if (setlistVal.isMember("WaitStraighten") && setlistVal["WaitStraighten"].isBool())
			setlist.waitStraighten = setlistVal["WaitStraighten"].asBool();
		if (setlistVal.isMember("PointAdsorption") && setlistVal["PointAdsorption"].isBool())
			setlist.pointAdsorption = setlistVal["PointAdsorption"].asBool();
		if (setlistVal.isMember("SmoothWriting") && setlistVal["SmoothWriting"].isBool())
			setlist.smoothWriting = setlistVal["SmoothWriting"].asBool();
		if (setlistVal.isMember("EraserSetting") && setlistVal["EraserSetting"].isObject())
		{
			if (setlistVal["EraserSetting"].isMember("EraserMode") && setlistVal["EraserSetting"]["EraserMode"].isInt())
				setlist.eraserSetting.eraserMode = setlistVal["EraserSetting"]["EraserMode"].asInt();
			if (setlistVal["EraserSetting"].isMember("EraserSize") && setlistVal["EraserSetting"]["EraserSize"].isInt())
				setlist.eraserSetting.eraserSize = setlistVal["EraserSetting"]["EraserSize"].asInt();
		}
		if (setlistVal.isMember("HideTouchPointerBeta") && setlistVal["HideTouchPointerBeta"].isBool())
			setlist.hideTouchPointer = setlistVal["HideTouchPointerBeta"].asBool();

		if (setlistVal.isMember("Save") && setlistVal["Save"].isObject())
		{
			if (setlistVal["Save"].isMember("Enable") && setlistVal["Save"]["Enable"].isBool())
				setlist.saveSetting.enable = setlistVal["Save"]["Enable"].asBool();
			if (setlistVal["Save"].isMember("SaveDays") && setlistVal["Save"]["SaveDays"].isInt())
				setlist.saveSetting.saveDays = setlistVal["Save"]["SaveDays"].asInt();
		}
		if (setlistVal.isMember("Performance") && setlistVal["Performance"].isObject())
		{
			if (setlistVal["Performance"].isMember("PreparationQuantity") && setlistVal["Performance"]["PreparationQuantity"].isInt())
				setlist.performanceSetting.preparationQuantity = setlistVal["Performance"]["PreparationQuantity"].asInt();

			if (setlistVal["Performance"].isMember("SuperDrawBeta2") && setlistVal["Performance"]["SuperDrawBeta2"].isBool())
				setlist.performanceSetting.superDraw = setlistVal["Performance"]["SuperDrawBeta2"].asBool();
		}
		if (setlistVal.isMember("Preset") && setlistVal["Preset"].isObject())
		{
			if (setlistVal["Preset"].isMember("MemoryWidth") && setlistVal["Preset"]["MemoryWidth"].isBool())
				setlist.presetSetting.memoryWidth = setlistVal["Preset"]["MemoryWidth"].asBool();
			if (setlistVal["Preset"].isMember("MemoryColor") && setlistVal["Preset"]["MemoryColor"].isBool())
				setlist.presetSetting.memoryColor = setlistVal["Preset"]["MemoryColor"].asBool();

			if (setlistVal["Preset"].isMember("AutoDefaultWidth") && setlistVal["Preset"]["AutoDefaultWidth"].isBool())
				setlist.presetSetting.autoDefaultWidth = setlistVal["Preset"]["AutoDefaultWidth"].asBool();
			if (setlistVal["Preset"].isMember("DefaultBrush1Width") && setlistVal["Preset"]["DefaultBrush1Width"].isDouble())
				setlist.presetSetting.defaultBrush1Width = static_cast<float>(setlistVal["Preset"]["DefaultBrush1Width"].asDouble());
			if (setlistVal["Preset"].isMember("DefaultHighlighter1Width") && setlistVal["Preset"]["DefaultHighlighter1Width"].isDouble())
				setlist.presetSetting.defaultHighlighter1Width = static_cast<float>(setlistVal["Preset"]["DefaultHighlighter1Width"].asDouble());
		}

		if (setlistVal.isMember("UpdateSetting") && setlistVal["UpdateSetting"].isObject())
		{
			if (setlistVal["UpdateSetting"].isMember("EnableAutoUpdate") && setlistVal["UpdateSetting"]["EnableAutoUpdate"].isBool())
				setlist.enableAutoUpdate = setlistVal["UpdateSetting"]["EnableAutoUpdate"].asBool();
			if (setlistVal["UpdateSetting"].isMember("UpdateChannel") && setlistVal["UpdateSetting"]["UpdateChannel"].isString())
				setlist.UpdateChannel = setlistVal["UpdateSetting"]["UpdateChannel"].asString();
			if (setlistVal["UpdateSetting"].isMember("UpdateArchitecture") && setlistVal["UpdateSetting"]["UpdateArchitecture"].isString())
			{
				setlist.updateArchitecture = setlistVal["UpdateSetting"]["UpdateArchitecture"].asString();
				if (setlist.updateArchitecture != "win32" && setlist.updateArchitecture != "win64" && setlist.updateArchitecture != "arm64")
					setlist.updateArchitecture = "win32";
			}
		}

		if (setlistVal.isMember("PlugIn") && setlistVal["PlugIn"].isObject())
		{
			if (setlistVal["PlugIn"].isMember("DesktopDrawpadBlocker") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"].isObject())
			{
				if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("Enable") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Enable"].isBool())
					ddbInteractionSetList.enable = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Enable"].asBool();
				if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("RunAsAdmin") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["RunAsAdmin"].isBool())
					ddbInteractionSetList.runAsAdmin = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["RunAsAdmin"].asBool();
				if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("SleepTime") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["SleepTime"].isInt())
					ddbInteractionSetList.sleepTime = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["SleepTime"].asInt();

				if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"].isMember("Intercept") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isObject())
				{
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard3Floating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"].isBool())
						ddbInteractionSetList.intercept.seewoWhiteboard3Floating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard5Floating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"].isBool())
						ddbInteractionSetList.intercept.seewoWhiteboard5Floating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard5CFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"].isBool())
						ddbInteractionSetList.intercept.seewoWhiteboard5CFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPincoSideBarFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"].isBool())
						ddbInteractionSetList.intercept.seewoPincoSideBarFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPincoDrawingFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"].isBool())
						ddbInteractionSetList.intercept.seewoPincoDrawingFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPPTFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"].isBool())
						ddbInteractionSetList.intercept.seewoPPTFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("AiClassFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"].isBool())
						ddbInteractionSetList.intercept.aiClassFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("HiteAnnotationFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["HiteAnnotationFloating"].isBool())
						ddbInteractionSetList.intercept.hiteAnnotationFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["HiteAnnotationFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("ChangYanFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"].isBool())
						ddbInteractionSetList.intercept.changYanFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("ChangYanPptFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanPptFloating"].isBool())
						ddbInteractionSetList.intercept.changYanPptFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanPptFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("IntelligentClassFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"].isBool())
						ddbInteractionSetList.intercept.intelligentClassFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoDesktopAnnotationFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopAnnotationFloating"].isBool())
						ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopAnnotationFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoDesktopSideBarFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"].isBool())
						ddbInteractionSetList.intercept.seewoDesktopSideBarFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"].asBool();
				}
			}
			if (setlistVal["PlugIn"].isMember("ShortcutAssistant") && setlistVal["PlugIn"]["ShortcutAssistant"].isObject())
			{
				if (setlistVal["PlugIn"]["ShortcutAssistant"].isMember("CorrectLnk") && setlistVal["PlugIn"]["ShortcutAssistant"]["CorrectLnk"].isBool())
					setlist.shortcutAssistant.correctLnk = setlistVal["PlugIn"]["ShortcutAssistant"]["CorrectLnk"].asBool();
				if (setlistVal["PlugIn"]["ShortcutAssistant"].isMember("CreateLnk") && setlistVal["PlugIn"]["ShortcutAssistant"]["CreateLnk"].isBool())
					setlist.shortcutAssistant.createLnk = setlistVal["PlugIn"]["ShortcutAssistant"]["CreateLnk"].asBool();
			}

			if (setlistVal["PlugIn"].isMember("SuperTop") && setlistVal["PlugIn"]["SuperTop"].isObject())
			{
				if (setlistVal["PlugIn"]["SuperTop"].isMember("Enable") && setlistVal["PlugIn"]["SuperTop"]["Enable"].isBool())
					setlist.plugInSetting.superTop.enable = setlistVal["PlugIn"]["SuperTop"]["Enable"].asBool();
				if (setlistVal["PlugIn"]["SuperTop"].isMember("Indicator") && setlistVal["PlugIn"]["SuperTop"]["Indicator"].isBool())
					setlist.plugInSetting.superTop.indicator = setlistVal["PlugIn"]["SuperTop"]["Indicator"].asBool();
			}
		}
		if (setlistVal.isMember("Component") && setlistVal["Component"].isObject())
		{
			// shortcutButton
			if (setlistVal["Component"].isMember("ShortcutButton") && setlistVal["Component"]["ShortcutButton"].isObject())
			{
				// appliance
				if (setlistVal["Component"]["ShortcutButton"].isMember("Appliance") && setlistVal["Component"]["ShortcutButton"]["Appliance"].isObject())
				{
					if (setlistVal["Component"]["ShortcutButton"]["Appliance"].isMember("Explorer") && setlistVal["Component"]["ShortcutButton"]["Appliance"]["Explorer"].isBool())
						setlist.component.shortcutButton.appliance.explorer = setlistVal["Component"]["ShortcutButton"]["Appliance"]["Explorer"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["Appliance"].isMember("Taskmgr") && setlistVal["Component"]["ShortcutButton"]["Appliance"]["Taskmgr"].isBool())
						setlist.component.shortcutButton.appliance.taskmgr = setlistVal["Component"]["ShortcutButton"]["Appliance"]["Taskmgr"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["Appliance"].isMember("Control") && setlistVal["Component"]["ShortcutButton"]["Appliance"]["Control"].isBool())
						setlist.component.shortcutButton.appliance.control = setlistVal["Component"]["ShortcutButton"]["Appliance"]["Control"].asBool();
				}
				// system
				if (setlistVal["Component"]["ShortcutButton"].isMember("System") && setlistVal["Component"]["ShortcutButton"]["System"].isObject())
				{
					if (setlistVal["Component"]["ShortcutButton"]["System"].isMember("Desktop") && setlistVal["Component"]["ShortcutButton"]["System"]["Desktop"].isBool())
						setlist.component.shortcutButton.system.desktop = setlistVal["Component"]["ShortcutButton"]["System"]["Desktop"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["System"].isMember("LockWorkStation") && setlistVal["Component"]["ShortcutButton"]["System"]["LockWorkStation"].isBool())
						setlist.component.shortcutButton.system.lockWorkStation = setlistVal["Component"]["ShortcutButton"]["System"]["LockWorkStation"].asBool();
				}
				// keyboard
				if (setlistVal["Component"]["ShortcutButton"].isMember("Keyboard") && setlistVal["Component"]["ShortcutButton"]["Keyboard"].isObject())
				{
					if (setlistVal["Component"]["ShortcutButton"]["Keyboard"].isMember("Keyboardesc") && setlistVal["Component"]["ShortcutButton"]["Keyboard"]["Keyboardesc"].isBool())
						setlist.component.shortcutButton.keyboard.keyboardesc = setlistVal["Component"]["ShortcutButton"]["Keyboard"]["Keyboardesc"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["Keyboard"].isMember("KeyboardAltF4") && setlistVal["Component"]["ShortcutButton"]["Keyboard"]["KeyboardAltF4"].isBool())
						setlist.component.shortcutButton.keyboard.keyboardAltF4 = setlistVal["Component"]["ShortcutButton"]["Keyboard"]["KeyboardAltF4"].asBool();
				}
				// rollCall
				if (setlistVal["Component"]["ShortcutButton"].isMember("RollCall") && setlistVal["Component"]["ShortcutButton"]["RollCall"].isObject())
				{
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("IslandCaller") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller"].isBool())
						setlist.component.shortcutButton.rollCall.IslandCaller = setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("SecRandom") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom"].isBool())
						setlist.component.shortcutButton.rollCall.SecRandom = setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("NamePicker") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["NamePicker"].isBool())
						setlist.component.shortcutButton.rollCall.NamePicker = setlistVal["Component"]["ShortcutButton"]["RollCall"]["NamePicker"].asBool();
				}
				// linkage
				if (setlistVal["Component"]["ShortcutButton"].isMember("Linkage") && setlistVal["Component"]["ShortcutButton"]["Linkage"].isObject())
				{
					if (setlistVal["Component"]["ShortcutButton"]["Linkage"].isMember("ClassislandSettings") && setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandSettings"].isBool())
						setlist.component.shortcutButton.linkage.classislandSettings = setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandSettings"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["Linkage"].isMember("ClassislandProfile") && setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandProfile"].isBool())
						setlist.component.shortcutButton.linkage.classislandProfile = setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandProfile"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["Linkage"].isMember("ClassislandClassswap") && setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandClassswap"].isBool())
						setlist.component.shortcutButton.linkage.classislandClassswap = setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandClassswap"].asBool();
				}
			}
		}
	}
	else return false;

	return true;
}
bool ReadSettingMini()
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
		if (updateVal.isMember("PlugIn") && updateVal["PlugIn"].isObject())
		{
			if (updateVal["PlugIn"].isMember("SuperTop") && updateVal["PlugIn"]["SuperTop"].isObject())
			{
				if (updateVal["PlugIn"]["SuperTop"].isMember("Enable") && updateVal["PlugIn"]["SuperTop"]["Enable"].isBool())
					setlist.plugInSetting.superTop.enable = updateVal["PlugIn"]["SuperTop"]["Enable"].asBool();
			}
		}
	}
	else return false;

	return true;
}
bool WriteSetting()
{
	if (setlist.configurationSetting.enable) setlistVal.clear();
	{
		{
			setlistVal["ConfigurationSetting"]["Enable"] = Json::Value(setlist.configurationSetting.enable);
		}

		setlistVal["SelectLanguage"] = Json::Value(setlist.selectLanguage);
		setlistVal["StartUp"] = Json::Value(setlist.startUp);
		setlistVal["SettingGlobalScale"] = Json::Value(setlist.settingGlobalScale);

		setlistVal["SetSkinMode"] = Json::Value(setlist.SetSkinMode);
		setlistVal["TopSleepTime"] = Json::Value(setlist.topSleepTime);
		setlistVal["RightClickClose"] = Json::Value(setlist.RightClickClose);
		{
			setlistVal["BrushRecover"] = Json::Value(setlist.BrushRecover);
			setlistVal["RubberRecover"] = Json::Value(setlist.RubberRecover);
		}
		{
			setlistVal["Regular"]["MoveRecover"] = Json::Value(setlist.regularSetting.moveRecover);
			setlistVal["Regular"]["ClickRecover"] = Json::Value(setlist.regularSetting.clickRecover);
		}
		setlistVal["Regular"]["AvoidFullScreen"] = Json::Value(setlist.regularSetting.avoidFullScreen);
		setlistVal["Regular"]["TeachingSafetyMode"] = Json::Value(setlist.regularSetting.teachingSafetyMode);

		setlistVal["PaintDevice"] = Json::Value(setlist.paintDevice);
		setlistVal["DisableRTS"] = Json::Value(setlist.disableRTS);
		setlistVal["LiftStraighten"] = Json::Value(setlist.liftStraighten);
		setlistVal["WaitStraighten"] = Json::Value(setlist.waitStraighten);
		setlistVal["PointAdsorption"] = Json::Value(setlist.pointAdsorption);
		setlistVal["SmoothWriting"] = Json::Value(setlist.smoothWriting);
		{
			setlistVal["EraserSetting"]["EraserMode"] = Json::Value(setlist.eraserSetting.eraserMode);
			setlistVal["EraserSetting"]["EraserSize"] = Json::Value(setlist.eraserSetting.eraserSize);
		}
		setlistVal["HideTouchPointerBeta"] = Json::Value(setlist.hideTouchPointer);

		{
			setlistVal["Save"]["Enable"] = Json::Value(setlist.saveSetting.enable);
			setlistVal["Save"]["SaveDays"] = Json::Value(setlist.saveSetting.saveDays);
		}
		{
			setlistVal["Performance"]["PreparationQuantity"] = Json::Value(setlist.performanceSetting.preparationQuantity);

			setlistVal["Performance"]["SuperDrawBeta2"] = Json::Value(setlist.performanceSetting.superDraw);
		}
		{
			setlistVal["Preset"]["MemoryWidth"] = Json::Value(setlist.presetSetting.memoryWidth);
			setlistVal["Preset"]["MemoryColor"] = Json::Value(setlist.presetSetting.memoryColor);

			setlistVal["Preset"]["AutoDefaultWidth"] = Json::Value(setlist.presetSetting.autoDefaultWidth);
			setlistVal["Preset"]["DefaultBrush1Width"] = Json::Value(static_cast<double>(setlist.presetSetting.defaultBrush1Width));
			setlistVal["Preset"]["DefaultHighlighter1Width"] = Json::Value(static_cast<double>(setlist.presetSetting.defaultHighlighter1Width));
		}

		{
			setlistVal["UpdateSetting"]["EnableAutoUpdate"] = Json::Value(setlist.enableAutoUpdate);
			setlistVal["UpdateSetting"]["UpdateChannel"] = Json::Value(setlist.UpdateChannel);
			setlistVal["UpdateSetting"]["UpdateArchitecture"] = Json::Value(setlist.updateArchitecture);
		}
		{
			setlistVal["BasicInfo"]["UserID"] = Json::Value(utf16ToUtf8(userId));
			setlistVal["BasicInfo"]["Edition"] = Json::Value(utf16ToUtf8(editionDate));
		}

		{
			{
				setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Enable"] = Json::Value(ddbInteractionSetList.enable);
				setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["RunAsAdmin"] = Json::Value(ddbInteractionSetList.runAsAdmin);
				setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["SleepTime"] = Json::Value(ddbInteractionSetList.sleepTime);

				{
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard3Floating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard5Floating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoWhiteboard5CFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPincoSideBarFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPincoDrawingFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoPPTFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"] = Json::Value(ddbInteractionSetList.intercept.aiClassFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["HiteAnnotationFloating"] = Json::Value(ddbInteractionSetList.intercept.hiteAnnotationFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"] = Json::Value(ddbInteractionSetList.intercept.changYanFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanPptFloating"] = Json::Value(ddbInteractionSetList.intercept.changYanPptFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"] = Json::Value(ddbInteractionSetList.intercept.intelligentClassFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopAnnotationFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.seewoDesktopSideBarFloating);
				}
			}
			{
				setlistVal["PlugIn"]["ShortcutAssistant"]["CorrectLnk"] = Json::Value(setlist.shortcutAssistant.correctLnk);
				setlistVal["PlugIn"]["ShortcutAssistant"]["CreateLnk"] = Json::Value(setlist.shortcutAssistant.createLnk);
			}

			{
				setlistVal["PlugIn"]["SuperTop"]["Enable"] = Json::Value(setlist.plugInSetting.superTop.enable);
				setlistVal["PlugIn"]["SuperTop"]["Indicator"] = Json::Value(setlist.plugInSetting.superTop.indicator);
			}
		}
		{
			// shortcutButton
			{
				// appliance
				{
					setlistVal["Component"]["ShortcutButton"]["Appliance"]["Explorer"] = Json::Value(setlist.component.shortcutButton.appliance.explorer);
					setlistVal["Component"]["ShortcutButton"]["Appliance"]["Taskmgr"] = Json::Value(setlist.component.shortcutButton.appliance.taskmgr);
					setlistVal["Component"]["ShortcutButton"]["Appliance"]["Control"] = Json::Value(setlist.component.shortcutButton.appliance.control);
				}
				// system
				{
					setlistVal["Component"]["ShortcutButton"]["System"]["Desktop"] = Json::Value(setlist.component.shortcutButton.system.desktop);
					setlistVal["Component"]["ShortcutButton"]["System"]["LockWorkStation"] = Json::Value(setlist.component.shortcutButton.system.lockWorkStation);
				}
				// keyboard
				{
					setlistVal["Component"]["ShortcutButton"]["Keyboard"]["Keyboardesc"] = Json::Value(setlist.component.shortcutButton.keyboard.keyboardesc);
					setlistVal["Component"]["ShortcutButton"]["Keyboard"]["KeyboardAltF4"] = Json::Value(setlist.component.shortcutButton.keyboard.keyboardAltF4);
				}
				// rollCall
				{
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller"] = Json::Value(setlist.component.shortcutButton.rollCall.IslandCaller);
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom"] = Json::Value(setlist.component.shortcutButton.rollCall.SecRandom);
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["NamePicker"] = Json::Value(setlist.component.shortcutButton.rollCall.NamePicker);
				}
				// linkage
				{
					setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandSettings"] = Json::Value(setlist.component.shortcutButton.linkage.classislandSettings);
					setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandProfile"] = Json::Value(setlist.component.shortcutButton.linkage.classislandProfile);
					setlistVal["Component"]["ShortcutButton"]["Linkage"]["ClassislandClassswap"] = Json::Value(setlist.component.shortcutButton.linkage.classislandClassswap);
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
	string jsonContent = "\xEF\xBB\xBF" + Json::writeString(writerBuilder, setlistVal);

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
		if (updateVal.isMember("ShowLoadingScreen") && updateVal["ShowLoadingScreen"].isBool())
			pptComSetlist.showLoadingScreen = updateVal["ShowLoadingScreen"].asBool();
		if (updateVal.isMember("MemoryWidgetPosition") && updateVal["MemoryWidgetPosition"].isBool())
			pptComSetlist.memoryWidgetPosition = updateVal["MemoryWidgetPosition"].asBool();

		if (updateVal.isMember("ShowBottomBoth") && updateVal["ShowBottomBoth"].isBool())
			pptComSetlist.showBottomBoth = updateVal["ShowBottomBoth"].asBool();
		if (updateVal.isMember("ShowMiddleBoth") && updateVal["ShowMiddleBoth"].isBool())
			pptComSetlist.showMiddleBoth = updateVal["ShowMiddleBoth"].asBool();
		if (updateVal.isMember("ShowBottomMiddle") && updateVal["ShowBottomMiddle"].isBool())
			pptComSetlist.showBottomMiddle = updateVal["ShowBottomMiddle"].asBool();

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

		// if (updateVal.isMember("AutoKillWpsProcess") && updateVal["AutoKillWpsProcess"].isBool())
		//		pptComSetlist.autoKillWpsProcess = updateVal["AutoKillWpsProcess"].asBool();
	}
	else return false;

	return true;
}
bool PptComReadSettingPositionOnly()
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
	else return false;

	return true;
}
bool PptComWriteSetting()
{
	Json::Value updateVal;
	{
		updateVal["FixedHandWriting"] = Json::Value(pptComSetlist.fixedHandWriting);
		updateVal["ShowLoadingScreen"] = Json::Value(pptComSetlist.showLoadingScreen);
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

		//updateVal["AutoKillWpsProcess"] = Json::Value(pptComSetlist.autoKillWpsProcess);
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
//	if (!OccupyFileForRead(&fileHandle, pluginPath + L"\\DesktopDrawpadBlocker\\interaction_configuration.json"))
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
			updateVal["Intercept"]["Iclass30Floating"] = Json::Value(ddbInteractionSetList.intercept.iclass30Floating);
			updateVal["Intercept"]["Iclass30SidebarFloating"] = Json::Value(ddbInteractionSetList.intercept.iclass30SidebarFloating);
		}

		updateVal["~ConfigurationChange"] = Json::Value(change);
		updateVal["~KeepOpen"] = Json::Value(!close);
	}

	HANDLE fileHandle = NULL;
	if (!OccupyFileForWrite(&fileHandle, pluginPath + L"\\DesktopDrawpadBlocker\\interaction_configuration.json"))
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

bool GetMemory()
{
	HANDLE fileHandle = NULL;
	if (!OccupyFileForRead(&fileHandle, globalPath + L"Inkeys\\Memory\\memory.json"))
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
		// Draw
		if (updateVal.isMember("Draw") && updateVal["Draw"].isObject())
		{
			// Brush1
			if (updateVal["Draw"].isMember("Brush1") && updateVal["Draw"]["Brush1"].isObject())
			{
				if (updateVal["Draw"]["Brush1"].isMember("Width") && updateVal["Draw"]["Brush1"]["Width"].isInt())
				{
					if (setlist.presetSetting.memoryWidth)
						stateMode.Pen.Brush1.width = updateVal["Draw"]["Brush1"]["Width"].asInt();
				}

				if (updateVal["Draw"]["Brush1"].isMember("Color") && updateVal["Draw"]["Brush1"]["Color"].isObject())
				{
					if (updateVal["Draw"]["Brush1"]["Color"].isMember("R") && updateVal["Draw"]["Brush1"]["Color"]["R"].isInt() && \
						updateVal["Draw"]["Brush1"]["Color"].isMember("G") && updateVal["Draw"]["Brush1"]["Color"]["G"].isInt() && \
						updateVal["Draw"]["Brush1"]["Color"].isMember("B") && updateVal["Draw"]["Brush1"]["Color"]["B"].isInt() && \
						updateVal["Draw"]["Brush1"]["Color"].isMember("A") && updateVal["Draw"]["Brush1"]["Color"]["A"].isInt())
					{
						if (setlist.presetSetting.memoryColor)
							stateMode.Pen.Brush1.color = RGBA(updateVal["Draw"]["Brush1"]["Color"]["R"].asInt(), updateVal["Draw"]["Brush1"]["Color"]["G"].asInt(), updateVal["Draw"]["Brush1"]["Color"]["B"].asInt(), updateVal["Draw"]["Brush1"]["Color"]["A"].asInt());
					}
				}
			}
			// Highlighter1
			if (updateVal["Draw"].isMember("Highlighter1") && updateVal["Draw"]["Highlighter1"].isObject())
			{
				if (updateVal["Draw"]["Highlighter1"].isMember("Width") && updateVal["Draw"]["Highlighter1"]["Width"].isInt())
				{
					if (setlist.presetSetting.memoryWidth)
						stateMode.Pen.Highlighter1.width = updateVal["Draw"]["Highlighter1"]["Width"].asInt();
				}

				if (updateVal["Draw"]["Highlighter1"].isMember("Color") && updateVal["Draw"]["Highlighter1"]["Color"].isObject())
				{
					if (updateVal["Draw"]["Highlighter1"]["Color"].isMember("R") && updateVal["Draw"]["Highlighter1"]["Color"]["R"].isInt() && \
						updateVal["Draw"]["Highlighter1"]["Color"].isMember("G") && updateVal["Draw"]["Highlighter1"]["Color"]["G"].isInt() && \
						updateVal["Draw"]["Highlighter1"]["Color"].isMember("B") && updateVal["Draw"]["Highlighter1"]["Color"]["B"].isInt() && \
						updateVal["Draw"]["Highlighter1"]["Color"].isMember("A") && updateVal["Draw"]["Highlighter1"]["Color"]["A"].isInt())
					{
						if (setlist.presetSetting.memoryColor)
							stateMode.Pen.Highlighter1.color = RGBA(updateVal["Draw"]["Highlighter1"]["Color"]["R"].asInt(), updateVal["Draw"]["Highlighter1"]["Color"]["G"].asInt(), updateVal["Draw"]["Highlighter1"]["Color"]["B"].asInt(), updateVal["Draw"]["Highlighter1"]["Color"]["A"].asInt());
					}
				}
			}
		}
	}
	else return false;

	return true;
}
bool SetMemory()
{
	Json::Value updateVal;
	{
		// Draw
		{
			// Brush1
			{
				updateVal["Draw"]["Brush1"]["Width"] = Json::Value(stateMode.Pen.Brush1.width);
				updateVal["Draw"]["Brush1"]["Color"]["R"] = Json::Value(static_cast<int>(stateMode.Pen.Brush1.color & 0xFF));
				updateVal["Draw"]["Brush1"]["Color"]["G"] = Json::Value(static_cast<int>((stateMode.Pen.Brush1.color >> 8) & 0xFF));
				updateVal["Draw"]["Brush1"]["Color"]["B"] = Json::Value(static_cast<int>((stateMode.Pen.Brush1.color >> 16) & 0xFF));
				updateVal["Draw"]["Brush1"]["Color"]["A"] = Json::Value(static_cast<int>((stateMode.Pen.Brush1.color >> 24) & 0xFF));
			}
			// Highlighter1
			{
				updateVal["Draw"]["Highlighter1"]["Width"] = Json::Value(stateMode.Pen.Highlighter1.width);
				updateVal["Draw"]["Highlighter1"]["Color"]["R"] = Json::Value(static_cast<int>(stateMode.Pen.Highlighter1.color & 0xFF));
				updateVal["Draw"]["Highlighter1"]["Color"]["G"] = Json::Value(static_cast<int>((stateMode.Pen.Highlighter1.color >> 8) & 0xFF));
				updateVal["Draw"]["Highlighter1"]["Color"]["B"] = Json::Value(static_cast<int>((stateMode.Pen.Highlighter1.color >> 16) & 0xFF));
				updateVal["Draw"]["Highlighter1"]["Color"]["A"] = Json::Value(static_cast<int>((stateMode.Pen.Highlighter1.color >> 24) & 0xFF));
			}
		}
	}

	HANDLE fileHandle = NULL;
	if (!OccupyFileForWrite(&fileHandle, globalPath + L"Inkeys\\Memory\\memory.json"))
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