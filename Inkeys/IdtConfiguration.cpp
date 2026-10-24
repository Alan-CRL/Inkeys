import Inkeys.Conv.Text;

import Inkeys.Other.Config;

#include "IdtConfiguration.h"
#include "Inkeys/Drawing/Draw3/Draw3.Bridge.h"

#include "IdtState.h"
#include "Inkeys/Business/PenWidthPreferences.hpp"
#include <cstdio>

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
	if (*hFile != NULL && *hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(*hFile);
		return true;
	}
	return false;
}

namespace
{
	constexpr LONGLONG kMaxLegacyConfigFileBytes = 16LL * 1024 * 1024;
	mutex g_legacySettingWriteMutex;

	bool ReadBoundedConfigFileSize(HANDLE file, DWORD& size) noexcept
	{
		LARGE_INTEGER length = {};
		// 读取前拒绝空文件、截断到 DWORD 的长度和异常大的本地 JSON。
		if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
			length.QuadPart > kMaxLegacyConfigFileBytes ||
			length.QuadPart > MAXDWORD) return false;
		size = static_cast<DWORD>(length.QuadPart);
		return true;
	}

	bool ExistingConfigValidOrAbsent(const wstring& path) noexcept
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			const DWORD error = GetLastError();
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
		}
		if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
			return false;
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		try
		{
			DWORD size = 0;
			if (!ReadBoundedConfigFileSize(file, size))
			{
				CloseHandle(file);
				return false;
			}
			string content(size, '\0');
			DWORD read = 0;
			const bool readOk = ReadFile(file, content.data(), size, &read, nullptr) &&
				read == size;
			CloseHandle(file);
			file = INVALID_HANDLE_VALUE;
			if (!readOk) return false;
			if (content.compare(0, 3, "\xEF\xBB\xBF") == 0)
				content.erase(0, 3);
			istringstream stream(content);
			Json::CharReaderBuilder reader;
			reader["stackLimit"] = 64;
			Json::Value root;
			string error;
			return Json::parseFromStream(reader, stream, &root, &error) &&
				root.isObject();
		}
		catch (...)
		{
			// 解析或分配失败必须保留磁盘原件。
			if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
			return false;
		}
	}

	bool ParseConfigJson(Json::CharReaderBuilder& reader, istream& stream,
		Json::Value& root, string& error) noexcept
	{
		try
		{
			return Json::parseFromStream(reader, stream, &root, &error) &&
				root.isObject();
		}
		catch (...) { return false; }
	}
}

SetListStruct setlist;
shared_mutex setlistUpdateMutex;
Json::Value setlistVal;
namespace
{
    std::mutex g_penWidthPreferencesMutex;
    Inkeys::Business::PenWidth::Preferences g_penWidthStartup;
    bool g_penWidthStartupInitialized = false;
    bool ReadMemoryRoot(const filesystem::path& path, Json::Value& root)
    {
        root = Json::Value(Json::objectValue);
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD size = 0;
        if (!ReadBoundedConfigFileSize(file, size)) { CloseHandle(file); return false; }
        string content(size, '\0');
        DWORD read = 0;
        const bool ok = ReadFile(file, content.data(), size, &read, nullptr) && read == size;
        CloseHandle(file);
        if (!ok) return false;
        if (content.compare(0, 3, "\xEF\xBB\xBF") == 0) content.erase(0, 3);
        istringstream input(content);
        Json::CharReaderBuilder reader;
        reader["stackLimit"] = 64;
        string error;
        return ParseConfigJson(reader, input, root, error);
    }
    bool SaveMemoryRoot(const filesystem::path& path, const Json::Value& root)
    {
        Json::StreamWriterBuilder writer;
        const string content = "\xEF\xBB\xBF" + Json::writeString(writer, root);
        if (content.size() > static_cast<size_t>(kMaxLegacyConfigFileBytes) ||
            !ExistingConfigValidOrAbsent(path.wstring())) return false;
        return Inkeys::PptSettings::WriteAtomically(path, content);
    }
    const filesystem::path MemoryPath()
    {
        return filesystem::path(globalPath) / L"Inkeys" / L"Memory" / L"memory.json";
    }
    bool ReadMemoryColor(const Json::Value& root, const char* slot, COLORREF& color)
    {
        if (!root["Draw"].isObject() || !root["Draw"][slot].isObject()) return false;
        const auto& value = root["Draw"][slot]["Color"];
        if (!value.isObject()) return false;
        for (const char* component : { "R", "G", "B", "A" })
            if (!value[component].isInt() || value[component].asInt() < 0 || value[component].asInt() > 255) return false;
        color = RGBA(value["R"].asInt(), value["G"].asInt(), value["B"].asInt(), value["A"].asInt());
        return true;
    }
}

bool ReadSetting()
{
	HANDLE fileHandle = NULL;
	if (!OccupyFileForRead(&fileHandle, globalPath + L"opt\\deploy.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	DWORD dwSize = 0;
	if (!ReadBoundedConfigFileSize(fileHandle, dwSize))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
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
	readerBuilder["stackLimit"] = 64;
	Json::Value parsedSetting;
	string jsonErr;

	if (ParseConfigJson(readerBuilder, jsonContentStream, parsedSetting, jsonErr))
	{
		// 仅在根对象完整验证后更新权威配置树，坏文件不能污染后续写入。
		setlistVal = std::move(parsedSetting);
		{ std::scoped_lock widthLock(g_penWidthPreferencesMutex); g_penWidthStartupInitialized = false; }
		if (setlistVal.isMember("SelectLanguage") && setlistVal["SelectLanguage"].isInt())
			setlist.selectLanguage = setlistVal["SelectLanguage"].asInt();
		if (setlistVal.isMember("StartUp") && setlistVal["StartUp"].isBool())
			setlist.startUp = setlistVal["StartUp"].asBool();
		// 缺项或非布尔值均回退浅色，不接受数字、字符串等隐式转换。
		setlist.settingDarkMode = setlistVal.isMember("SettingDarkMode")
			&& setlistVal["SettingDarkMode"].isBool() && setlistVal["SettingDarkMode"].asBool();
		// 缺项、错误类型和越界值均保持“本进程首次打开”的默认策略。
		setlist.settingEntryAnimation = 1;
		if (setlistVal.isMember("SettingEntryAnimation") && setlistVal["SettingEntryAnimation"].isInt())
		{
			const int mode = setlistVal["SettingEntryAnimation"].asInt();
			if (mode >= 0 && mode <= 2) setlist.settingEntryAnimation = mode;
		}

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
				if (setlistVal["Regular"].isMember("MoveRecover") && setlistVal["Regular"]["MoveRecover1"].isBool())
					setlist.regularSetting.moveRecover = setlistVal["Regular"]["MoveRecover1"].asBool();
				if (setlistVal["Regular"].isMember("ClickRecover") && setlistVal["Regular"]["ClickRecover"].isBool())
					setlist.regularSetting.clickRecover = setlistVal["Regular"]["ClickRecover"].asBool();
			}

			if (setlistVal["Regular"].isMember("TeachingSafetyMode") && setlistVal["Regular"]["TeachingSafetyMode"].isInt())
			{
				const int requestedMode = setlistVal["Regular"]["TeachingSafetyMode"].asInt();
				// 设置页只有四个合法选项；损坏配置不能作为 vector 下标传播。
				if (requestedMode >= 0 && requestedMode < 4)
					setlist.regularSetting.teachingSafetyMode = requestedMode;
			}
		}

		if (setlistVal.isMember("PaintDevice") && setlistVal["PaintDevice"].isInt())
			setlist.paintDevice = setlistVal["PaintDevice"].asInt();
		if (setlistVal.isMember("DisableRTS") && setlistVal["DisableRTS"].isBool())
			setlist.disableRTS = setlistVal["DisableRTS"].asBool();
		if (setlistVal.isMember("LiftStraighten") && setlistVal["LiftStraighten"].isBool())
			setlist.liftStraighten = setlistVal["LiftStraighten"].asBool();
		if (setlistVal.isMember("WaitStraighten") && setlistVal["WaitStraighten"].isBool())
			setlist.waitStraighten = setlistVal["WaitStraighten"].asBool();
		if (setlistVal.isMember("EraserSetting") && setlistVal["EraserSetting"].isObject())
		{
			if (setlistVal["EraserSetting"].isMember("EraserMode") && setlistVal["EraserSetting"]["EraserMode"].isInt())
			{
				const int saved=setlistVal["EraserSetting"]["EraserMode"].asInt();
				setlist.eraserSetting.savedFixedChoice=saved==2;
				setlist.eraserSetting.eraserMode=Inkeys::Drawing::Draw3::Bridge::NormalizeLegacyEraserMode(saved);
			}
			if (setlistVal["EraserSetting"].isMember("EraserSize") && setlistVal["EraserSetting"]["EraserSize"].isInt())
				setlist.eraserSetting.eraserSize = setlistVal["EraserSetting"]["EraserSize"].asInt();
		}
		if (setlistVal.isMember("Save") && setlistVal["Save"].isObject())
		{
			if (setlistVal["Save"].isMember("Enable") && setlistVal["Save"]["Enable"].isBool())
				setlist.saveSetting.enable = setlistVal["Save"]["Enable"].asBool();
			if (setlistVal["Save"].isMember("SaveDays") && setlistVal["Save"]["SaveDays"].isInt())
				setlist.saveSetting.saveDays = setlistVal["Save"]["SaveDays"].asInt();
		}
		if (setlistVal.isMember("UpdateSetting") && setlistVal["UpdateSetting"].isObject())
		{
			{
				unique_lock<shared_mutex> lock(setlistUpdateMutex);
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
						ddbInteractionSetList.intercept.SeewoWhiteboard3Floating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard5Floating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"].isBool())
						ddbInteractionSetList.intercept.SeewoWhiteboard5Floating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoWhiteboard5CFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"].isBool())
						ddbInteractionSetList.intercept.SeewoWhiteboard5CFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPincoSideBarFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"].isBool())
						ddbInteractionSetList.intercept.SeewoPincoSideBarFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPincoDrawingFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"].isBool())
						ddbInteractionSetList.intercept.SeewoPincoDrawingFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoPPTFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"].isBool())
						ddbInteractionSetList.intercept.SeewoPPTFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoIwbAssistantFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoIwbAssistantFloating"].isBool())
						ddbInteractionSetList.intercept.SeewoIwbAssistantFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoIwbAssistantFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("YiouBoardFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["YiouBoardFloating"].isBool())
						ddbInteractionSetList.intercept.YiouBoardFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["YiouBoardFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("AiClassFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"].isBool())
						ddbInteractionSetList.intercept.AiClassFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("ClassInXFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ClassInXFloating"].isBool())
						ddbInteractionSetList.intercept.ClassInXFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ClassInXFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("IntelligentClassFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"].isBool())
						ddbInteractionSetList.intercept.IntelligentClassFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("ChangYanFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"].isBool())
						ddbInteractionSetList.intercept.ChangYanFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("ChangYan5Floating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYan5Floating"].isBool())
						ddbInteractionSetList.intercept.ChangYan5Floating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYan5Floating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("Iclass30SidebarFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["Iclass30SidebarFloating"].isBool())
						ddbInteractionSetList.intercept.Iclass30SidebarFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["Iclass30SidebarFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("Iclass30Floating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["Iclass30Floating"].isBool())
						ddbInteractionSetList.intercept.Iclass30Floating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["Iclass30Floating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoDesktopSideBarFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"].isBool())
						ddbInteractionSetList.intercept.SeewoDesktopSideBarFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"].asBool();
					if (setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"].isMember("SeewoDesktopDrawingFloating") && setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopDrawingFloating"].isBool())
						ddbInteractionSetList.intercept.SeewoDesktopDrawingFloating = setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopDrawingFloating"].asBool();
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
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("IslandCaller1") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller1"].isBool())
						setlist.component.shortcutButton.rollCall.IslandCaller1 = setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller1"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("IslandCaller2") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller2"].isBool())
						setlist.component.shortcutButton.rollCall.IslandCaller2 = setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller2"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("SecRandom1") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom1"].isBool())
						setlist.component.shortcutButton.rollCall.SecRandom1 = setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom1"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("SecRandom2") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom2"].isBool())
						setlist.component.shortcutButton.rollCall.SecRandom2 = setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom2"].asBool();
					if (setlistVal["Component"]["ShortcutButton"]["RollCall"].isMember("SecRandom2Compat") && setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom2Compat"].isBool())
						setlist.component.shortcutButton.rollCall.SecRandom2Compat = setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom2Compat"].asBool();
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

	DWORD dwSize = 0;
	if (!ReadBoundedConfigFileSize(fileHandle, dwSize))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
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
	readerBuilder["stackLimit"] = 64;
	Json::Value updateVal;
	string jsonErr;

	if (ParseConfigJson(readerBuilder, jsonContentStream, updateVal, jsonErr))
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
string CaptureSettingJson()
{
    Json::Value widthPreferences;
    {
        std::scoped_lock widthLock(g_penWidthPreferencesMutex);
        // 仅为安全保存保留退休偏好/迁移证据，不将其应用到本版启动状态。
        // AutoClean 前冻结原对象，未来版本也不得降级覆盖。
        g_penWidthStartup = Inkeys::Business::PenWidth::DecodePreferences(setlistVal, false, GetPenWidthContext());
        widthPreferences = Inkeys::Business::PenWidth::EncodePreferences(g_penWidthStartup, false);
    }
    if (Inkeys::config.Config.AutoClean) setlistVal.clear();
    setlistVal["PenWidth"] = std::move(widthPreferences);

	bool enableAutoUpdate;
	string updateChannel;
	string updateArchitecture;
	{
		shared_lock<shared_mutex> lock(setlistUpdateMutex);
		enableAutoUpdate = setlist.enableAutoUpdate;
		updateChannel = setlist.UpdateChannel;
		updateArchitecture = setlist.updateArchitecture;
	}
	{
		setlistVal["SelectLanguage"] = Json::Value(setlist.selectLanguage);
		setlistVal["StartUp"] = Json::Value(setlist.startUp);
		setlistVal["SettingDarkMode"] = Json::Value(setlist.settingDarkMode);
		setlistVal["SettingEntryAnimation"] = Json::Value(
			setlist.settingEntryAnimation >= 0 && setlist.settingEntryAnimation <= 2
			? setlist.settingEntryAnimation : 1);

		setlistVal["SetSkinMode"] = Json::Value(setlist.SetSkinMode);
		setlistVal["TopSleepTime"] = Json::Value(setlist.topSleepTime);
		setlistVal["RightClickClose"] = Json::Value(setlist.RightClickClose);
		{
			setlistVal["BrushRecover"] = Json::Value(setlist.BrushRecover);
			setlistVal["RubberRecover"] = Json::Value(setlist.RubberRecover);
		}
		{
			setlistVal["Regular"]["MoveRecover1"] = Json::Value(setlist.regularSetting.moveRecover);
			setlistVal["Regular"]["ClickRecover"] = Json::Value(setlist.regularSetting.clickRecover);
		}
		setlistVal["Regular"]["TeachingSafetyMode"] = Json::Value(setlist.regularSetting.teachingSafetyMode);

		setlistVal["PaintDevice"] = Json::Value(setlist.paintDevice.load());
		setlistVal["DisableRTS"] = Json::Value(setlist.disableRTS);
		setlistVal["LiftStraighten"] = Json::Value(setlist.liftStraighten);
		setlistVal["WaitStraighten"] = Json::Value(setlist.waitStraighten);
		{
			setlist.eraserSetting.eraserMode = setlist.eraserSetting.eraserMode == 1 ? 1 : 2;
			setlistVal["EraserSetting"]["EraserMode"] = Json::Value(setlist.eraserSetting.eraserMode);
			setlistVal["EraserSetting"]["EraserSize"] = Json::Value(setlist.eraserSetting.eraserSize);
		}
		{
			setlistVal["Save"]["Enable"] = Json::Value(setlist.saveSetting.enable);
			setlistVal["Save"]["SaveDays"] = Json::Value(setlist.saveSetting.saveDays);
		}
		// 已删除的旧设置不依赖 AutoClean；每次保存都清除历史 JSON 键。
		setlistVal.removeMember("SettingGlobalScale");
		setlistVal["Regular"].removeMember("AvoidFullScreen");
		setlistVal.removeMember("PointAdsorption");
		setlistVal.removeMember("SmoothWriting");
		setlistVal.removeMember("HideTouchPointerBeta");
		setlistVal.removeMember("Performance");
		setlistVal.removeMember("Preset");

		{
			setlistVal["UpdateSetting"]["EnableAutoUpdate"] = Json::Value(enableAutoUpdate);
			setlistVal["UpdateSetting"]["UpdateChannel"] = Json::Value(updateChannel);
			setlistVal["UpdateSetting"]["UpdateArchitecture"] = Json::Value(updateArchitecture);
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
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(ddbInteractionSetList.intercept.SeewoWhiteboard3Floating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(ddbInteractionSetList.intercept.SeewoWhiteboard5Floating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoWhiteboard5CFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoPincoSideBarFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPincoDrawingFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoPincoDrawingFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoPPTFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoPPTFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoIwbAssistantFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoIwbAssistantFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["YiouBoardFloating"] = Json::Value(ddbInteractionSetList.intercept.YiouBoardFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["AiClassFloating"] = Json::Value(ddbInteractionSetList.intercept.AiClassFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ClassInXFloating"] = Json::Value(ddbInteractionSetList.intercept.ClassInXFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["IntelligentClassFloating"] = Json::Value(ddbInteractionSetList.intercept.IntelligentClassFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYanFloating"] = Json::Value(ddbInteractionSetList.intercept.ChangYanFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["ChangYan5Floating"] = Json::Value(ddbInteractionSetList.intercept.ChangYan5Floating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["Iclass30SidebarFloating"] = Json::Value(ddbInteractionSetList.intercept.Iclass30SidebarFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["Iclass30Floating"] = Json::Value(ddbInteractionSetList.intercept.Iclass30Floating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoDesktopSideBarFloating);
					setlistVal["PlugIn"]["DesktopDrawpadBlocker"]["Intercept"]["SeewoDesktopDrawingFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoDesktopDrawingFloating);
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
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller1"] = Json::Value(setlist.component.shortcutButton.rollCall.IslandCaller1);
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["IslandCaller2"] = Json::Value(setlist.component.shortcutButton.rollCall.IslandCaller2);
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom1"] = Json::Value(setlist.component.shortcutButton.rollCall.SecRandom1);
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom2"] = Json::Value(setlist.component.shortcutButton.rollCall.SecRandom2);
					setlistVal["Component"]["ShortcutButton"]["RollCall"]["SecRandom2Compat"] = Json::Value(setlist.component.shortcutButton.rollCall.SecRandom2Compat);
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

		// UI3 已成为唯一入口，升级写回时移除旧实验开关。
		if (setlistVal.isMember("Experimental") && setlistVal["Experimental"].isObject()
			&& setlistVal["Experimental"].isMember("Inkeys3")
			&& setlistVal["Experimental"]["Inkeys3"].isObject())
		{
			setlistVal["Experimental"]["Inkeys3"].removeMember("UI3");
			if (setlistVal["Experimental"]["Inkeys3"].empty())
				setlistVal["Experimental"].removeMember("Inkeys3");
			if (setlistVal["Experimental"].empty())
				setlistVal.removeMember("Experimental");
		}
	}

	Json::StreamWriterBuilder writerBuilder;
	return "\xEF\xBB\xBF" + Json::writeString(writerBuilder, setlistVal);
}

bool WriteSettingJson(const string& jsonContent)
{
	scoped_lock writeLock(g_legacySettingWriteMutex);
	if (jsonContent.empty() || jsonContent.size() >
		static_cast<size_t>(kMaxLegacyConfigFileBytes))
		return false;
	const filesystem::path path = filesystem::path(globalPath) / L"opt" / L"deploy.json";
	// 已存在但不可读/不可解析时保留原件；首次缺文件才允许写默认值。
	if (!ExistingConfigValidOrAbsent(path.wstring())) return false;
	return Inkeys::PptSettings::WriteAtomically(path, jsonContent);
}

bool WriteSetting()
{
	return WriteSettingJson(CaptureSettingJson());
}

PptComSetListStruct pptComSetlist;
bool PptComReadSetting()
{
	// 加载页暂不开放；缺失/损坏的配置以及旧 true 都不能重新启用。
	pptComSetlist.showLoadingScreen = false;
	HANDLE fileHandle = NULL;
	if (!OccupyFileForRead(&fileHandle, globalPath + L"opt\\pptcom_configuration.json"))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	DWORD dwSize = 0;
	if (!ReadBoundedConfigFileSize(fileHandle, dwSize))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
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
	readerBuilder["stackLimit"] = 64;
	Json::Value updateVal;
	string jsonErr;

	if (ParseConfigJson(readerBuilder, jsonContentStream, updateVal, jsonErr))
	{
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
		// 	pptComSetlist.autoKillWpsProcess = updateVal["AutoKillWpsProcess"].asBool();
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

	DWORD dwSize = 0;
	if (!ReadBoundedConfigFileSize(fileHandle, dwSize))
	{
		UnOccupyFile(&fileHandle);
		return false;
	}
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
	readerBuilder["stackLimit"] = 64;
	Json::Value updateVal;
	string jsonErr;

	if (ParseConfigJson(readerBuilder, jsonContentStream, updateVal, jsonErr))
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
namespace
{
	std::mutex pptSettingsMutex;
	std::mutex pptSettingsWriteMutex;
	Inkeys::PptSettings::WriteJournal pptSettingsJournal;
}

static Json::Value CapturePptComSettingValue()
{
	Json::Value updateVal;
	{
		updateVal["ShowLoadingScreen"] = Json::Value(false);
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

		// updateVal["AutoKillWpsProcess"] = Json::Value(pptComSetlist.autoKillWpsProcess);
	}

	return updateVal;
}

static void EnsurePptSettingsJournal()
{
	if (!pptSettingsJournal.Initialized())
		pptSettingsJournal.Initialize(CapturePptComSettingValue());
}

string CapturePptComSettingJson()
{
	std::scoped_lock lock(pptSettingsMutex);
	EnsurePptSettingsJournal();
	return pptSettingsJournal.CaptureSettings(CapturePptComSettingValue());
}

string CapturePptComPositionSettingJson(Inkeys::PptSettings::Positions positions, bool remember)
{
	std::scoped_lock lock(pptSettingsMutex);
	EnsurePptSettingsJournal();
	return pptSettingsJournal.CapturePositions(positions, remember);
}

Inkeys::PptSettings::Positions SavedPptComPositions()
{
	std::scoped_lock lock(pptSettingsMutex);
	EnsurePptSettingsJournal();
	return pptSettingsJournal.Saved();
}

Inkeys::PptSettings::Positions RestorablePptComPositions()
{
	std::scoped_lock lock(pptSettingsMutex);
	EnsurePptSettingsJournal();
	return pptSettingsJournal.RestoreBaseline();
}

string RetryPptComSettingJson()
{
	std::scoped_lock lock(pptSettingsMutex);
	return pptSettingsJournal.Retry();
}

bool WritePptComSettingJson(const string& jsonContent)
{
	// 文件提交串行，但 UI 捕获和读取保存基线不等待磁盘 I/O。
	std::scoped_lock writeLock(pptSettingsWriteMutex);
	const filesystem::path path = filesystem::path(globalPath) /
		L"opt" / L"pptcom_configuration.json";
	if (!ExistingConfigValidOrAbsent(path.wstring())) return false;
	Inkeys::PptSettings::WriteJournal::PreparedWrite prepared;
	{
		std::scoped_lock lock(pptSettingsMutex);
		if (!pptSettingsJournal.Prepare(jsonContent, prepared)) return false;
	}
	if (prepared.revision == 0) return true;
	const bool succeeded = !prepared.content.empty() &&
		prepared.content.size() <= static_cast<size_t>(kMaxLegacyConfigFileBytes) &&
		Inkeys::PptSettings::WriteAtomically(path, prepared.content);
	{
		std::scoped_lock lock(pptSettingsMutex);
		pptSettingsJournal.Complete(prepared, succeeded);
	}
	if (!succeeded) OutputDebugStringW(L"[Ppt] settings commit failed; frozen request retained for retry\n");
	return succeeded;
}

bool PptComWriteSetting()
{
	return WritePptComSettingJson(CapturePptComSettingJson());
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
string CaptureDdbInteractionJson(bool change, bool close)
{
	Json::Value updateVal;
	{
		updateVal["SleepTime"] = Json::Value(ddbInteractionSetList.sleepTime);

		updateVal["Mode"]["Mode"] = Json::Value(ddbInteractionSetList.mode);
		updateVal["Mode"]["HostPath"] = Json::Value(utf16ToUtf8(ddbInteractionSetList.hostPath));
		updateVal["Mode"]["RestartHost"] = Json::Value(ddbInteractionSetList.restartHost);

		{
			updateVal["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(ddbInteractionSetList.intercept.SeewoWhiteboard3Floating);
			updateVal["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(ddbInteractionSetList.intercept.SeewoWhiteboard5Floating);
			updateVal["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoWhiteboard5CFloating);
			updateVal["Intercept"]["SeewoPincoSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoPincoSideBarFloating);
			updateVal["Intercept"]["SeewoPincoDrawingFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoPincoDrawingFloating);
			updateVal["Intercept"]["SeewoPPTFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoPPTFloating);
			updateVal["Intercept"]["SeewoIwbAssistantFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoIwbAssistantFloating);
			updateVal["Intercept"]["YiouBoardFloating"] = Json::Value(ddbInteractionSetList.intercept.YiouBoardFloating);
			updateVal["Intercept"]["AiClassFloating"] = Json::Value(ddbInteractionSetList.intercept.AiClassFloating);
			updateVal["Intercept"]["ClassInXFloating"] = Json::Value(ddbInteractionSetList.intercept.ClassInXFloating);
			updateVal["Intercept"]["IntelligentClassFloating"] = Json::Value(ddbInteractionSetList.intercept.IntelligentClassFloating);
			updateVal["Intercept"]["ChangYanFloating"] = Json::Value(ddbInteractionSetList.intercept.ChangYanFloating);
			updateVal["Intercept"]["ChangYan5Floating"] = Json::Value(ddbInteractionSetList.intercept.ChangYan5Floating);
			updateVal["Intercept"]["Iclass30SidebarFloating"] = Json::Value(ddbInteractionSetList.intercept.Iclass30SidebarFloating);
			updateVal["Intercept"]["Iclass30Floating"] = Json::Value(ddbInteractionSetList.intercept.Iclass30Floating);
			updateVal["Intercept"]["SeewoDesktopSideBarFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoDesktopSideBarFloating);
			updateVal["Intercept"]["SeewoDesktopDrawingFloating"] = Json::Value(ddbInteractionSetList.intercept.SeewoDesktopDrawingFloating);
		}

		updateVal["~ConfigurationChange"] = Json::Value(change);
		updateVal["~KeepOpen"] = Json::Value(!close);
	}

	Json::StreamWriterBuilder writerBuilder;
	return "\xEF\xBB\xBF" + Json::writeString(writerBuilder, updateVal);
}

bool WriteDdbInteractionJson(const string& jsonContent)
{

	HANDLE fileHandle = NULL;
	if (!OccupyFileForWrite(&fileHandle, pluginPath + L"DesktopDrawpadBlocker\\interaction_configuration.json"))
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

	DWORD bytesWritten = 0;
	if (!WriteFile(fileHandle, jsonContent.data(), static_cast<DWORD>(jsonContent.size()), &bytesWritten, NULL) || bytesWritten != jsonContent.size())
	{
		UnOccupyFile(&fileHandle);
		return false;
	}

	UnOccupyFile(&fileHandle);
	return true;
}

bool DdbWriteInteraction(bool change, bool close)
{
	return WriteDdbInteractionJson(CaptureDdbInteractionJson(change, close));
}

void InitializePenWidthPreferences(Inkeys::Business::PenWidth::Context context)
{
    using namespace Inkeys::Business::PenWidth;
    ApplyPenWidthContext(context);
    {
        std::scoped_lock lock(g_penWidthPreferencesMutex);
        // 默认仅初始化一次；后续换屏只重解析身份，不能覆盖手动粗细。
        if (g_penWidthStartupInitialized) return;
        // 本版隐藏且暂不支持启动记忆/启动预设；保留兼容代码但不读取应用残留配置。
        // g_penWidthStartup = DecodePreferences(setlistVal, false, context);
        // setlist.presetSetting.memoryWidth = g_penWidthStartup.memoryWidth;
        // setlist.presetSetting.memoryColor = g_penWidthStartup.memoryColor;
        // setlist.presetSetting.autoDefaultWidth = g_penWidthStartup.autoDefaultWidth;
        g_penWidthStartupInitialized = true;
    }
    RestorePenWidthSelections({}, {}, {});
    // GetMemory(); // 暂停启动恢复粗细/颜色；与 Draw3 文档历史恢复无关。
}

bool GetMemory()
{
    using namespace Inkeys::Business::PenWidth;
    std::scoped_lock lock(g_penWidthPreferencesMutex);
    const auto context = GetPenWidthContext();
    if (!g_penWidthStartupInitialized)
    {
        g_penWidthStartup = DecodePreferences(setlistVal, false, context);
        g_penWidthStartupInitialized = true;
    }
    Json::Value root;
    const bool loaded = ReadMemoryRoot(MemoryPath(), root);
    const Preferences memory = loaded ? DecodePreferences(root, true, context) : Preferences{};
    COLORREF brushColor = 0, highlighterColor = 0;
    const bool brushColorValid = loaded && g_penWidthStartup.memoryColor && ReadMemoryColor(root, "Brush1", brushColor);
    const bool highlighterColorValid = loaded && g_penWidthStartup.memoryColor && ReadMemoryColor(root, "Highlighter1", highlighterColor);
    RestorePenWidthSelections(StartupSelection(g_penWidthStartup, memory, 0),
        StartupSelection(g_penWidthStartup, memory, 1), StartupSelection(g_penWidthStartup, memory, 2),
        brushColorValid ? &brushColor : nullptr, highlighterColorValid ? &highlighterColor : nullptr);
    if (loaded && memory.migrated)
    {
        // 版本/身份/迁移证据原子写入；失败保留旧文件，内存已解析值不再除 DPI。
        root["PenWidth"] = EncodePreferences(memory, true);
        return SaveMemoryRoot(MemoryPath(), root);
    }
    return loaded;
}

bool SetMemory()
{
    using namespace Inkeys::Business::PenWidth;
    std::scoped_lock lock(g_penWidthPreferencesMutex);
    const auto snapshot = GetStateModeSnapshot();
    Json::Value root;
    if (!ReadMemoryRoot(MemoryPath(), root)) return false;
    auto preferences = DecodePreferences(root, true, snapshot.penWidthContext);
    if (!preferences.unknownSchema)
    {
        preferences.selections = { snapshot.Pen.Brush1.widthSelection,
            snapshot.Pen.Highlighter1.widthSelection, snapshot.Pen.Laser.widthSelection };
        preferences.valid = { true, true, true };
        root["PenWidth"] = EncodePreferences(preferences, true);
    }
    // 旧 Width 原值保留作兼容证据；新真值只写带单位的 Selections，不混入旧字段。
    const auto color = [&](const char* slot, COLORREF value)
    {
        if (!root["Draw"].isObject()) root["Draw"] = Json::Value(Json::objectValue);
        if (!root["Draw"][slot].isObject()) root["Draw"][slot] = Json::Value(Json::objectValue);
        auto& target = root["Draw"][slot]["Color"];
        target = Json::Value(Json::objectValue);
        target["R"] = static_cast<int>(value & 0xff);
        target["G"] = static_cast<int>((value >> 8) & 0xff);
        target["B"] = static_cast<int>((value >> 16) & 0xff);
        target["A"] = static_cast<int>((value >> 24) & 0xff);
    };
    color("Brush1", snapshot.Pen.Brush1.color);
    color("Highlighter1", snapshot.Pen.Highlighter1.color);
    return SaveMemoryRoot(MemoryPath(), root);
}

int RunPenWidthPreferencesProductionTest() noexcept
{
    using namespace Inkeys::Business::PenWidth;
    const auto savedPath = globalPath;
    const auto savedRoot = setlistVal;
    const auto savedAutoClean = Inkeys::config.Config.AutoClean.load();
    Preferences savedStartup;
    bool savedInitialized = false;
    {
        std::scoped_lock lock(g_penWidthPreferencesMutex);
        savedStartup = g_penWidthStartup;
        savedInitialized = g_penWidthStartupInitialized;
    }
    filesystem::path directory;
    bool ownsDirectory = false;
    HANDLE blockReplacement = INVALID_HANDLE_VALUE;
    const char* stage = "temporary-config";
    int result = 1;
    try
    {
        const auto require = [](bool passed) { if (!passed) throw 1; };
        wchar_t temporary[MAX_PATH]{};
        const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporary);
        require(temporaryLength > 0 && temporaryLength < MAX_PATH);
        directory = filesystem::path(temporary) / (L"InkeysPenWidthTest-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64()));
        require(!filesystem::exists(directory));
        require(filesystem::create_directories(directory));
        ownsDirectory = true;
        globalPath = directory.wstring() + L"\\";
        const auto writeRoot = [&](const filesystem::path& path, const Json::Value& root)
        {
            Json::StreamWriterBuilder writer;
            return Inkeys::PptSettings::WriteAtomically(path, Json::writeString(writer, root));
        };
        const auto resetStartup = [&](Json::Value root)
        {
            setlistVal = std::move(root);
            std::scoped_lock lock(g_penWidthPreferencesMutex);
            g_penWidthStartupInitialized = false;
        };
        Context context{ 1, 1, 144.0, 1920, 1080 };
        setlist.paintDevice = 1;
        stage = "fresh-default-and-first-medium";
        resetStartup(Json::Value(Json::objectValue));
        InitializePenWidthPreferences(context);
        auto snapshot = GetStateModeSnapshot();
        require(snapshot.Pen.Brush1.widthSelection.source == Source::Preset && snapshot.Pen.Brush1.width == 3.0f
            && snapshot.Pen.Highlighter1.width == 24.0f && snapshot.Pen.Laser.width == 5.0f);
        ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
        const auto first = GetPenWidthMetrics(GetStateModeSnapshot());
        require(SetPenWidthPresetIfRevision(1, StateModeTransitionRevision(), false)
            && first.idealCanvasWidthPx == GetPenWidthMetrics(GetStateModeSnapshot()).idealCanvasWidthPx);

        stage = "retired-legacy-preferences-and-memory-ignored";
        require(SetPenColor(RGB(12, 34, 56), false));
        ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
        require(SetPenColor(RGB(65, 43, 21), false));
        ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
        Json::Value deploy(Json::objectValue);
        deploy["Preset"]["MemoryWidth"] = true;
        deploy["Preset"]["MemoryColor"] = true;
        deploy["Preset"]["AutoDefaultWidth"] = false;
        deploy["Preset"]["DefaultBrush1Width"] = 3.0;
        deploy["Preset"]["DefaultHighlighter1Width"] = 35.0;
        deploy["Unrelated"] = "keep";
        require(writeRoot(directory / L"opt" / L"deploy.json", deploy));
        Json::Value memory(Json::objectValue);
        memory["Draw"]["Brush1"]["Width"] = 12.0;
        memory["Draw"]["Highlighter1"]["Width"] = 70.0;
        memory["Draw"]["Laser"]["Width"] = 7.0;
        memory["Draw"]["Brush1"]["Color"]["R"] = 255;
        memory["Draw"]["Brush1"]["Color"]["G"] = 255;
        memory["Draw"]["Brush1"]["Color"]["B"] = 255;
        memory["Draw"]["Brush1"]["Color"]["A"] = 0;
        memory["Draw"]["Highlighter1"]["Color"] = memory["Draw"]["Brush1"]["Color"];
        memory["Unrelated"] = "keep-memory";
        require(writeRoot(MemoryPath(), memory) && ReadSetting());
        const auto brushColor = GetStateModeSnapshot().Pen.Brush1.color;
        const auto requireMedium = [&]()
        {
            const auto state = GetStateModeSnapshot();
            const auto medium = [](Selection selection)
            { return selection.source == Source::Preset && selection.preset == Preset::Medium; };
            require(medium(state.Pen.Brush1.widthSelection) && medium(state.Pen.Highlighter1.widthSelection)
                && medium(state.Pen.Laser.widthSelection));
            require(std::abs(state.Pen.Brush1.width - Resolve({}, Family::Brush, context).resolvedWidthDip) < 1e-6
                && std::abs(state.Pen.Highlighter1.width - Resolve({}, Family::Highlighter, context).resolvedWidthDip) < 1e-6
                && state.Pen.Laser.width == 5.0f && state.Pen.Brush1.color == brushColor
                && state.Pen.Highlighter1.color == RGB(65, 43, 21));
        };
        InitializePenWidthPreferences(context);
        requireMedium();
        Json::Value migrated;
        require(ReadMemoryRoot(MemoryPath(), migrated) && memory == migrated);
        // 兼容解码仅用于保全保存证据，不代表当前产品支持恢复启动偏好。
        require(WriteSetting() && ReadMemoryRoot(directory / L"opt" / L"deploy.json", migrated)
            && migrated["PenWidth"]["Migration"]["Legacy"] == deploy["Preset"]
            && migrated["Unrelated"].asString() == "keep");
        require(ReadSetting());
        context.effectiveDpi = 264.0;
        InitializePenWidthPreferences(context);
        requireMedium();
        require(ReadMemoryRoot(MemoryPath(), migrated) && memory == migrated);

        stage = "retired-versioned-startup-and-memory-ignored";
        Preferences startup;
        startup.memoryWidth = true; startup.memoryColor = true; startup.autoDefaultWidth = false;
        startup.valid = { true, true, true };
        startup.selections = { Selection{ Source::ExplicitDip, Preset::Medium, 2.5 },
            Selection{ Source::Preset, Preset::Thick, 3.0 }, Selection{ Source::ExplicitDip, Preset::Medium, 7.0 } };
        deploy["PenWidth"] = EncodePreferences(startup, false);
        Preferences remembered = startup;
        remembered.selections[0] = { Source::ExplicitDip, Preset::Medium, 17.0 };
        memory["PenWidth"] = EncodePreferences(remembered, true);
        require(writeRoot(directory / L"opt" / L"deploy.json", deploy)
            && writeRoot(MemoryPath(), memory) && ReadSetting());
        InitializePenWidthPreferences(context);
        requireMedium();
        require(WriteSetting() && ReadMemoryRoot(directory / L"opt" / L"deploy.json", migrated)
            && migrated["PenWidth"] == deploy["PenWidth"]);
        startup.memoryWidth = false;
        deploy["PenWidth"] = EncodePreferences(startup, false);
        require(writeRoot(directory / L"opt" / L"deploy.json", deploy) && ReadSetting());
        InitializePenWidthPreferences(context);
        requireMedium();

        stage = "classroom-default-and-followup-context-preserves-manual";
        context.paintDevice = 0; context.pixelWidth = 2560; context.pixelHeight = 1440;
        setlist.paintDevice = 0;
        require(ReadSetting());
        InitializePenWidthPreferences(context);
        requireMedium();
        require(std::abs(GetPenWidthMetrics(GetStateModeSnapshot()).idealCanvasWidthPx - 16.0 / 3.0) < 1e-9);
        require(SetPenWidth(17.0, false));
        InitializePenWidthPreferences(context);
        require(GetStateModeSnapshot().Pen.Brush1.widthSelection.source == Source::ExplicitDip
            && GetStateModeSnapshot().Pen.Brush1.widthSelection.explicitDip == 17.0);
        context.effectiveDpi = 192.0;
        InitializePenWidthPreferences(context);
        require(GetStateModeSnapshot().Pen.Brush1.widthSelection.explicitDip == 17.0);
        require(SetPenWidthPresetIfRevision(2, StateModeTransitionRevision(), false));
        context.pixelWidth = 3840; context.pixelHeight = 2160;
        InitializePenWidthPreferences(context);
        snapshot = GetStateModeSnapshot();
        require(snapshot.Pen.Brush1.widthSelection.source == Source::Preset
            && snapshot.Pen.Brush1.widthSelection.preset == Preset::Thick
            && std::abs(GetPenWidthMetrics(snapshot).idealCanvasWidthPx - 12.0) < 1e-9);

        stage = "atomic-failure-preserves-original";
        require(ReadMemoryRoot(MemoryPath(), memory));
        blockReplacement = CreateFileW(MemoryPath().c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(blockReplacement != INVALID_HANDLE_VALUE);
        require(!SetMemory());
        CloseHandle(blockReplacement); blockReplacement = INVALID_HANDLE_VALUE;
        require(ReadMemoryRoot(MemoryPath(), migrated) && memory == migrated);

        stage = "future-schema-autoclean-preservation";
        deploy["PenWidth"]["Version"] = 99;
        deploy["PenWidth"]["FuturePayload"] = "opaque";
        require(writeRoot(directory / L"opt" / L"deploy.json", deploy) && ReadSetting());
        InitializePenWidthPreferences(context);
        Inkeys::config.Config.AutoClean = true;
        const auto expectedFuture = deploy["PenWidth"];
        require(WriteSetting() && ReadMemoryRoot(directory / L"opt" / L"deploy.json", migrated)
            && migrated["PenWidth"] == expectedFuture);
        memory["PenWidth"]["Version"] = 99;
        memory["PenWidth"]["FuturePayload"] = "opaque memory";
        require(writeRoot(MemoryPath(), memory) && SetMemory() && ReadMemoryRoot(MemoryPath(), migrated)
            && memory["PenWidth"] == migrated["PenWidth"]);

        stage = "malformed-file-no-overwrite";
        require(Inkeys::PptSettings::WriteAtomically(MemoryPath(), "{broken"));
        require(!SetMemory());
        std::puts("pen-width-preferences-production-test: PASS");
        result = 0;
    }
    catch (...) { std::fprintf(stderr, "pen-width-preferences-production-test: FAIL %s\n", stage); }
    if (blockReplacement != INVALID_HANDLE_VALUE) CloseHandle(blockReplacement);
    globalPath = savedPath;
    setlistVal = savedRoot;
    Inkeys::config.Config.AutoClean = savedAutoClean;
    {
        std::scoped_lock lock(g_penWidthPreferencesMutex);
        g_penWidthStartup = std::move(savedStartup);
        g_penWidthStartupInitialized = savedInitialized;
    }
    if (ownsDirectory)
    {
        // 只清理本探针刚创建并持有的独立目录，不接触用户配置目录。
        std::error_code error;
        filesystem::remove_all(directory, error);
    }
    return result;
}
