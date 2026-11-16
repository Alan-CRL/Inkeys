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

namespace
{
	std::mutex pptSettingsMutex;
	std::mutex pptSettingsWriteMutex;
	Inkeys::PptSettings::WriteJournal pptSettingsJournal;
}

// PPT 控件位置与设置页共用唯一保存入口 Inkeys::config.Write()；
// 写入日志只记录版本、已保存基线与失败重试，不再携带独立的配置内容。
static Inkeys::PptSettings::Positions ConfiguredPptPositions()
{
	const auto& ppt = Inkeys::config.PlugIn.PPTHelper;
	return { static_cast<float>(ppt.BottomBothWidth.load()), static_cast<float>(ppt.BottomBothHeight.load()),
		static_cast<float>(ppt.MiddleBothWidth.load()), static_cast<float>(ppt.MiddleBothHeight.load()) };
}

static void EnsurePptSettingsJournal()
{
	if (pptSettingsJournal.Initialized()) return;
	Json::Value settings(Json::objectValue);
	Inkeys::PptSettings::SetPositions(settings, ConfiguredPptPositions());
	settings["MemoryWidgetPosition"] = Inkeys::config.PlugIn.PPTHelper.MemoryWidgetPosition.load();
	pptSettingsJournal.Initialize(std::move(settings));
}

string CapturePptComPositionSettingJson(Inkeys::PptSettings::Positions positions, bool remember)
{
	std::scoped_lock lock(pptSettingsMutex);
	EnsurePptSettingsJournal();
	// 内存配置立即更新，实际写盘由持久化回调串行完成。
	auto& ppt = Inkeys::config.PlugIn.PPTHelper;
	ppt.BottomBothWidth = positions.bottomX;
	ppt.BottomBothHeight = positions.bottomY;
	ppt.MiddleBothWidth = positions.middleX;
	ppt.MiddleBothHeight = positions.middleY;
	ppt.MemoryWidgetPosition = remember;
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
	Inkeys::PptSettings::WriteJournal::PreparedWrite prepared;
	{
		std::scoped_lock lock(pptSettingsMutex);
		if (!pptSettingsJournal.Prepare(jsonContent, prepared)) return false;
	}
	if (prepared.revision == 0) return true;
	const bool succeeded = Inkeys::config.Write();
	{
		std::scoped_lock lock(pptSettingsMutex);
		pptSettingsJournal.Complete(prepared, succeeded);
	}
	if (!succeeded) OutputDebugStringW(L"[Ppt] settings commit failed; frozen request retained for retry\n");
	return succeeded;
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
		updateVal["SleepTime"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.SleepTime.load());

		updateVal["Mode"]["Mode"] = Json::Value(ddbInteractionSetList.mode);
		updateVal["Mode"]["HostPath"] = Json::Value(utf16ToUtf8(ddbInteractionSetList.hostPath));
		updateVal["Mode"]["RestartHost"] = Json::Value(ddbInteractionSetList.restartHost);

		{
			updateVal["Intercept"]["SeewoWhiteboard3Floating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoWhiteboard3Floating.load());
			updateVal["Intercept"]["SeewoWhiteboard5Floating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoWhiteboard5Floating.load());
			updateVal["Intercept"]["SeewoWhiteboard5CFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoWhiteboard5CFloating.load());
			updateVal["Intercept"]["SeewoPincoSideBarFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoPincoSideBarFloating.load());
			updateVal["Intercept"]["SeewoPincoDrawingFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoPincoDrawingFloating.load());
			updateVal["Intercept"]["SeewoPPTFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoPPTFloating.load());
			updateVal["Intercept"]["SeewoIwbAssistantFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoIwbAssistantFloating.load());
			updateVal["Intercept"]["YiouBoardFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.YiouBoardFloating.load());
			updateVal["Intercept"]["AiClassFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.AiClassFloating.load());
			updateVal["Intercept"]["ClassInXFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.ClassInXFloating.load());
			updateVal["Intercept"]["IntelligentClassFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.IntelligentClassFloating.load());
			updateVal["Intercept"]["ChangYanFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.ChangYanFloating.load());
			updateVal["Intercept"]["ChangYan5Floating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.ChangYan5Floating.load());
			updateVal["Intercept"]["Iclass30SidebarFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.Iclass30SidebarFloating.load());
			updateVal["Intercept"]["Iclass30Floating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.Iclass30Floating.load());
			updateVal["Intercept"]["SeewoDesktopSideBarFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoDesktopSideBarFloating.load());
			updateVal["Intercept"]["SeewoDesktopDrawingFloating"] = Json::Value(Inkeys::config.PlugIn.DesktopDrawpadBlocker.Intercept.SeewoDesktopDrawingFloating.load());
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
        // 本版隐藏且暂不支持启动记忆/启动预设；启动偏好没有持久化来源。
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
    // 启动偏好不再有持久化来源，保持默认（不记忆）。
    g_penWidthStartupInitialized = true;
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
    StateModeClass snapshot;
    if (!TryGetDesktopStateModeSnapshot(snapshot)) return true;
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
    const auto savedPaintDevice = Inkeys::config.Drawing.PaintDevice.load();
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
            (void)root;
            std::scoped_lock lock(g_penWidthPreferencesMutex);
            g_penWidthStartupInitialized = false;
        };
        // 旧 ReadSetting 会重置启动偏好初始化标记；迁移后仅保留该重置语义。
        const auto ResetStartup = [&]() { resetStartup(Json::Value(Json::objectValue)); return true; };
        Context context{ 1, 1, 144.0, 1920, 1080 };
        Inkeys::config.Drawing.PaintDevice = 1;
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
        require(writeRoot(MemoryPath(), memory) && ResetStartup());
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
        require(ResetStartup());
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
        Preferences remembered = startup;
        remembered.selections[0] = { Source::ExplicitDip, Preset::Medium, 17.0 };
        memory["PenWidth"] = EncodePreferences(remembered, true);
        require(writeRoot(MemoryPath(), memory) && ResetStartup());
        InitializePenWidthPreferences(context);
        requireMedium();
        startup.memoryWidth = false;
        require(ResetStartup());
        InitializePenWidthPreferences(context);
        requireMedium();

        stage = "classroom-default-and-followup-context-preserves-manual";
        context.paintDevice = 0; context.pixelWidth = 2560; context.pixelHeight = 1440;
        Inkeys::config.Drawing.PaintDevice = 0;
        require(ResetStartup());
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
        require(ResetStartup());
        InitializePenWidthPreferences(context);
        Inkeys::config.Config.AutoClean = true;
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
    Inkeys::config.Drawing.PaintDevice = savedPaintDevice;
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
