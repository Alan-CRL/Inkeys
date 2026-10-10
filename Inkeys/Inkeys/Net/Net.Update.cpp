module;

#include "../../IdtMain.h"

#include "../../IdtConfiguration.h"
#include "../../IdtOther.h"
#include "../../IdtTime.h"
#include "../Window/Window.Legacy.hpp"
#include "UpdatePathSafety.h"
#include <cstdio>
#include <exception>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

module Inkeys.Net.Update;
import :Download;

import Inkeys.Conv.Text;
import Inkeys.Other.Config;

// 程序自动更新

bool mandatoryUpdate; // 强制更新符号
bool inconsistentArchitecture;
AutomaticUpdateStateEnum AutomaticUpdateState;
namespace
{
	constexpr unsigned long long kMaxUpdatePackageBytes = 512ull * 1024ull * 1024ull;
	constexpr unsigned long long kMaxUpdateExecutableBytes = 512ull * 1024ull * 1024ull;
	constexpr unsigned long long kMaxZipExpandedBytes = 1024ull * 1024ull * 1024ull;
	constexpr int kMaxZipEntries = 128;

	bool IsValidUpdateUrl(const string& url);

	struct UpdateTargetSnapshot
	{
		string channel;
		string architecture;
		bool enableAutoUpdate;
	};

	UpdateTargetSnapshot GetUpdateTargetSnapshot()
	{
		shared_lock<shared_mutex> lock(setlistUpdateMutex);
		return { setlist.UpdateChannel, setlist.updateArchitecture, setlist.enableAutoUpdate };
	}

	void SetUpdateChannelSnapshot(const string& channel)
	{
		unique_lock<shared_mutex> lock(setlistUpdateMutex);
		setlist.UpdateChannel = channel;
	}

	struct StagedUpdateMetadata
	{
		wstring edition;
		wstring path;
		string md5;
		string sha256;
		string channel;
		string arch;
	};

	bool TryReadStagedUpdateMetadata(const wstring& filePath,
		StagedUpdateMetadata& output) noexcept
	{
		try
		{
			constexpr DWORD kMaxStagedJsonBytes = 64 * 1024;
			const HANDLE raw = CreateFileW(filePath.c_str(), GENERIC_READ,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (raw == INVALID_HANDLE_VALUE) return false;
			struct FileGuard
			{
				HANDLE handle;
				~FileGuard() { CloseHandle(handle); }
			} file{ raw };
			LARGE_INTEGER length{};
			if (!GetFileSizeEx(raw, &length) || length.QuadPart <= 0 ||
				length.QuadPart > kMaxStagedJsonBytes) return false;
			std::string text(static_cast<size_t>(length.QuadPart), '\0');
			size_t offset = 0;
			while (offset < text.size())
			{
				DWORD read = 0;
				if (!ReadFile(raw, text.data() + offset,
					static_cast<DWORD>(text.size() - offset), &read, nullptr) || read == 0)
					return false;
				offset += read;
			}
			if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
			Json::CharReaderBuilder builder;
			builder["stackLimit"] = 32;
			builder["collectComments"] = false;
			builder["failIfExtra"] = true;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			Json::Value root;
			std::string errors;
			if (!reader || !reader->parse(text.data(), text.data() + text.size(),
				&root, &errors) || !root.isObject() || !root["hash"].isObject())
				return false;
			const Json::Value& hash = root["hash"];
			if (!root["edition"].isString() || !root["path"].isString() ||
				!hash["md5"].isString() || !hash["sha256"].isString() ||
				!root["channel"].isString() || !root["arch"].isString())
				return false;
			StagedUpdateMetadata candidate;
			candidate.edition = utf8ToUtf16(root["edition"].asString());
			candidate.path = utf8ToUtf16(root["path"].asString());
			candidate.md5 = hash["md5"].asString();
			candidate.sha256 = hash["sha256"].asString();
			candidate.channel = root["channel"].asString();
			candidate.arch = root["arch"].asString();
			// 所有字段先在局部完成，失败绝不向业务线程发布半份缓存元数据。
			output = std::move(candidate);
			return true;
		}
		catch (const std::exception&) { return false; }
		catch (...) { return false; }
	}
}

int RunStagedUpdateJsonBoundaryProbe(const std::wstring& path,
	bool expectedValid) noexcept
{
	try
	{
		StagedUpdateMetadata metadata;
		if (TryReadStagedUpdateMetadata(path, metadata) == expectedValid) return 0;
		std::fputs("[UpdateStagedJson] failed: unexpected validity\n", stderr);
		return 1;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "[UpdateStagedJson] exception: %s\n", error.what());
		return 2;
	}
	catch (...)
	{
		std::fputs("[UpdateStagedJson] exception: unknown\n", stderr);
		return 2;
	}
}
wstring get_domain_name(wstring url) {
	wregex pattern(L"([a-zA-z]+://[^/]+)");
	wsmatch match;
	if (regex_search(url, match, pattern)) return match[0].str();
	else return L"";
}
wstring convertToHttp(const wstring& url)
{
	//更新保险
	//if (getCurrentDate() >= L"20231020") return url;

	wstring httpPrefix = L"http://";
	if (url.length() >= 7 && url.compare(0, 7, httpPrefix) == 0) return url;
	else if (url.length() >= 8 && url.compare(0, 8, L"https://") == 0) return httpPrefix + url.substr(8);
	else return httpPrefix + url;
}

string GetRefererInfo()
{
	return Inkeys::config.GetUploadInfo();
}

EditionInfoClass GetEditionInfo(string channel, string arch)
{
	/*
	* 错误码：
	* 1 下载失败
	* 2 下载信息损坏
	* 3 下载信息不符合规范
	* 200 下载信息成功
	*/
	EditionInfoClass retEditionInfo;

	string editionInformation = GetEditionInformation(GetRefererInfo());
	if (editionInformation == "Error")
	{
		retEditionInfo.errorCode = 1;
		return retEditionInfo;
	}

	istringstream jsonContentStream(editionInformation);
	Json::CharReaderBuilder readerBuilder;
	readerBuilder["stackLimit"] = 32;
	Json::Value editionInfoValue;
	string jsonErr;
	bool parsed = false;
	try
	{
		parsed = Json::parseFromStream(readerBuilder, jsonContentStream,
			&editionInfoValue, &jsonErr);
	}
	catch (const Json::Exception&) { parsed = false; }
	if (parsed && editionInfoValue.isObject())
	{
		bool informationCompliance = true;
		int tryTime = 0;

	getInfoStart:
		retEditionInfo = EditionInfoClass();
		if (editionInfoValue.isMember(channel) && editionInfoValue[channel].isObject())
		{
			if (editionInfoValue[channel].isMember("edition_date") && editionInfoValue[channel]["edition_date"].isString()) retEditionInfo.editionDate = utf8ToUtf16(editionInfoValue[channel]["edition_date"].asString());
			else informationCompliance = false;
			if (editionInfoValue[channel].isMember("edition_code") && editionInfoValue[channel]["edition_code"].isString()) retEditionInfo.editionCode = utf8ToUtf16(editionInfoValue[channel]["edition_code"].asString());
			if (editionInfoValue[channel].isMember("explain") && editionInfoValue[channel]["explain"].isString()) retEditionInfo.explain = utf8ToUtf16(editionInfoValue[channel]["explain"].asString());
			if (editionInfoValue[channel].isMember("hash") && editionInfoValue[channel]["hash"].isObject())
			{
				string hash1, hash2;
				if (arch == "win64") hash1 = "md5 64", hash2 = "sha256 64";
				else if (arch == "arm64") hash1 = "md5 Arm64", hash2 = "sha256 Arm64";
				else hash1 = "md5", hash2 = "sha256";

				if (editionInfoValue[channel]["hash"].isMember(hash1) && editionInfoValue[channel]["hash"][hash1].isString()) retEditionInfo.hash_md5 = editionInfoValue[channel]["hash"][hash1].asString();
				else informationCompliance = false;
				if (editionInfoValue[channel]["hash"].isMember(hash2) && editionInfoValue[channel]["hash"][hash2].isString()) retEditionInfo.hash_sha256 = editionInfoValue[channel]["hash"][hash2].asString();
				else informationCompliance = false;
			}
			else informationCompliance = false;

			{
				string path;
				if (arch == "win64") path = "path64";
				else if (arch == "arm64") path = "pathArm64";
				else path = "path";

				if (editionInfoValue[channel].isMember(path) && editionInfoValue[channel][path].isArray())
				{
					retEditionInfo.path_size = 0;
					const auto pathCount = std::min<Json::Value::ArrayIndex>(
						editionInfoValue[channel][path].size(), 10);
					for (Json::Value::ArrayIndex i = 0; i < pathCount; i++)
					{
						if (editionInfoValue[channel][path][i].isString())
						{
							retEditionInfo.path[retEditionInfo.path_size] = editionInfoValue[channel][path][i].asString();
							retEditionInfo.path_size++;
						}
					}
					if (retEditionInfo.path_size <= 0) informationCompliance = false;
				}
				else informationCompliance = false;
			}
			if (editionInfoValue[channel].isMember("size") && editionInfoValue[channel]["size"].isObject())
			{
				string path;
				if (arch == "win64") path = "file64";
				else if (arch == "arm64") path = "fileArm64";
				else path = "file";

				if (editionInfoValue[channel]["size"].isMember(path) && editionInfoValue[channel]["size"][path].isUInt64())
					retEditionInfo.fileSize = editionInfoValue[channel]["size"][path].asUInt64();
			}

			if (editionInfoValue[channel].isMember("representation") && editionInfoValue[channel]["representation"].isString()) retEditionInfo.representation = utf8ToUtf16(editionInfoValue[channel]["representation"].asString());
			else informationCompliance = false;
		}
		else informationCompliance = false;

		if (!IsSafeUpdateExecutableName(retEditionInfo.representation) ||
			!IsSafeUpdateHash(retEditionInfo.hash_md5, 32) ||
			!IsSafeUpdateHash(retEditionInfo.hash_sha256, 64) ||
			retEditionInfo.fileSize.load() == 0 ||
			retEditionInfo.fileSize.load() > kMaxUpdatePackageBytes)
			informationCompliance = false;
		for (int i = 0; i < retEditionInfo.path_size; ++i)
			if (!IsValidUpdateUrl(retEditionInfo.path[i]))
				informationCompliance = false;

		// 失败则尝试其他通道
		if (!informationCompliance && tryTime <= 1)
		{
			informationCompliance = true;
			tryTime++;

			// 尝试 LTS
			if (tryTime == 1)
			{
				channel = "LTS";
				goto getInfoStart;
			}
			// 尝试一个通道
			if (editionInfoValue.size() >= 1)
			{
				Json::Value::Members members = editionInfoValue.getMemberNames();
				if (channel != members[0])
				{
					channel = members[0];
					goto getInfoStart;
				}
			}

			informationCompliance = false;
		}

		if (!informationCompliance)
		{
			retEditionInfo.errorCode = 3;
			return retEditionInfo;
		}
		else
		{
			retEditionInfo.channel = channel;
			retEditionInfo.errorCode = 200;
		}
	}
	else
	{
		retEditionInfo.errorCode = 2;
		return retEditionInfo;
	}

	return retEditionInfo;
}
DownloadNewProgramStateClass downloadNewProgramState;

void splitUrl(string input_url, string& prefix, string& domain, string& path)
{
	SafeUpdateUrlParts parsed;
	if (ParseSafeUpdateUrl(input_url, parsed))
	{
		prefix = parsed.scheme == UpdateUrlScheme::Https ? "https://" : "http://";
		domain.assign(parsed.host);
		if (parsed.explicitPort) domain += ":" + to_string(parsed.port);
		path.assign(parsed.path);
	}
	else
	{
		prefix.clear();
		domain.clear();
		path.clear();
	}
}
namespace
{
	bool IsValidUpdateUrl(const string& url)
	{
		SafeUpdateUrlParts parsed;
		return ParseSafeUpdateUrl(url, parsed);
	}
}
AutomaticUpdateStateEnum DownloadNewProgram(DownloadNewProgramStateClass* state, EditionInfoClass editionInfo, string url, string arch)
{
	using enum AutomaticUpdateStateEnum;

	// 远端字段必须先过边界，再创建、删除或移动任何安装文件。
	if (!state || !IsValidUpdateUrl(url) ||
		!IsSafeUpdateExecutableName(editionInfo.representation) ||
		!IsSafeUpdateHash(editionInfo.hash_md5, 32) ||
		!IsSafeUpdateHash(editionInfo.hash_sha256, 64) ||
		editionInfo.fileSize.load() == 0 ||
		editionInfo.fileSize.load() > kMaxUpdatePackageBytes)
		return UpdateDownloadDamage;
	string prefix, domain, path;
	splitUrl(url, prefix, domain, path);
	if ((prefix != "https://" && prefix != "http://") ||
		domain.empty() || path.empty())
		return UpdateDownloadDamage;

	const wstring installer = globalPath + L"installer";
	error_code ec;
	filesystem::create_directory(installer, ec);
	if (ec) return UpdateDownloadDamage;
	const DWORD installerAttributes = GetFileAttributesW(installer.c_str());
	if (installerAttributes == INVALID_FILE_ATTRIBUTES ||
		(installerAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
		(installerAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		return UpdateDownloadDamage;

	const wstring timestamp = getTimestamp();
	const wstring stageBase = L"stage_" + timestamp + L"_" +
		to_wstring(GetCurrentProcessId());
	wstring stageName, stageDirectory;
	bool stageCreated = false;
	for (int attempt = 0; attempt < 10; ++attempt)
	{
		stageName = stageBase + L"_" + to_wstring(attempt);
		stageDirectory = installer + L"\\" + stageName;
		ec.clear();
		if (filesystem::create_directory(stageDirectory, ec))
		{
			stageCreated = true;
			break;
		}
		if (ec) break;
	}
	if (!stageCreated) return UpdateDownloadDamage;
	const wstring zipPath = stageDirectory + L"\\package.tmp";
	const wstring payloadPath = stageDirectory + L"\\payload.exe";
	const wstring stagedJsonPath = stageDirectory + L"\\update.tmp";
	const wstring finalName = L"new_procedure_" + stageName + L".exe";
	const wstring finalPath = installer + L"\\" + finalName;
	const wstring updateJsonPath = installer + L"\\update.json";
	const auto cleanupStage = [&]
	{
		error_code ignored;
		filesystem::remove(zipPath, ignored);
		filesystem::remove(payloadPath, ignored);
		filesystem::remove(stagedJsonPath, ignored);
		filesystem::remove(stageDirectory, ignored); // 仅尝试删除本次创建的空目录。
	};
	if (!IsSafeUpdateExecutableName(finalName))
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}

	state->downloadedSize.store(0);
	state->fileSize.store(editionInfo.fileSize.load());
	if (!DownloadEdition(prefix + domain, path, stageDirectory + L"\\", L"package.tmp",
		state->downloadedSize, GetRefererInfo()))
	{
		cleanupStage();
		return UpdateDownloadFail;
	}
	const DWORD zipAttributes = GetFileAttributesW(zipPath.c_str());
	ec.clear();
	const auto compressedSize = filesystem::file_size(zipPath, ec);
	if (zipAttributes == INVALID_FILE_ATTRIBUTES ||
		(zipAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
		ec || compressedSize == 0 || compressedSize > kMaxUpdatePackageBytes)
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}

	HZIP archive = OpenZip(zipPath.c_str(), 0);
	if (!archive)
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}
	bool extracted = false;
	do
	{
		ZIPENTRY summary{};
		if (GetZipItem(archive, -1, &summary) != ZR_OK ||
			summary.index <= 0 || summary.index > kMaxZipEntries) break;
		unsigned long long expandedSize = 0;
		int executableIndex = -1;
		long executableSize = 0;
		bool entriesValid = true;
		for (int i = 0; i < summary.index; ++i)
		{
			ZIPENTRY entry{};
			if (GetZipItem(archive, i, &entry) != ZR_OK ||
				entry.comp_size < 0 || entry.unc_size < 0 ||
				static_cast<unsigned long long>(entry.unc_size) >
					kMaxUpdateExecutableBytes)
			{
				entriesValid = false;
				break;
			}
			expandedSize += static_cast<unsigned long long>(entry.unc_size);
			if (expandedSize > kMaxZipExpandedBytes)
			{
				entriesValid = false;
				break;
			}
			size_t nameLength = 0;
			while (nameLength < MAX_PATH && entry.name[nameLength] != L'\0')
				++nameLength;
			if (nameLength == MAX_PATH)
			{
				entriesValid = false;
				break;
			}
			if (_wcsicmp(entry.name, editionInfo.representation.c_str()) == 0)
			{
				if (executableIndex != -1 ||
					wstring_view(entry.name, nameLength) !=
						wstring_view(editionInfo.representation.c_str(),
							editionInfo.representation.size()) ||
					(entry.attr & FILE_ATTRIBUTE_DIRECTORY) != 0 || entry.unc_size == 0)
				{
					entriesValid = false;
					break;
				}
				executableIndex = i;
				executableSize = entry.unc_size;
			}
		}
		if (!entriesValid || executableIndex < 0) break;

		// 只解到有容量上限的内存，绝不把 ZIP entry.name 当落盘路径。
		vector<char> executable;
		try { executable.resize(static_cast<size_t>(executableSize)); }
		catch (...) { break; }
		if (UnzipItem(archive, executableIndex, executable.data(),
			static_cast<unsigned int>(executable.size())) != ZR_OK) break;
		const HANDLE output = CreateFileW(payloadPath.c_str(), GENERIC_WRITE, 0,
			nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (output == INVALID_HANDLE_VALUE) break;
		DWORD written = 0;
		const bool wrote = WriteFile(output, executable.data(),
			static_cast<DWORD>(executable.size()), &written, nullptr) &&
			written == executable.size() && FlushFileBuffers(output);
		const bool closed = CloseHandle(output) != 0;
		if (!wrote || !closed) break;
		extracted = true;
	} while (false);
	if (CloseZip(archive) != ZR_OK) extracted = false;
	if (!extracted)
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}

	md5wrapper md5;
	sha256wrapper sha256;
	if (editionInfo.hash_md5 != md5.getHashFromFileW(payloadPath) ||
		editionInfo.hash_sha256 != sha256.getHashFromFileW(payloadPath))
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}
	if (!GetUpdateTargetSnapshot().enableAutoUpdate && !mandatoryUpdate)
	{
		cleanupStage();
		return UpdateNew;
	}
	const wstring oldName = GetCurrentExeName();
	if (!IsSafeUpdateExecutableName(oldName))
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}

	Json::Value root;
	root["edition"] = Json::Value(utf16ToUtf8(editionInfo.editionDate));
	root["path"] = Json::Value(utf16ToUtf8(L"installer\\" + finalName));
	root["representation"] = Json::Value(utf16ToUtf8(finalName));
	root["channel"] = Json::Value(editionInfo.channel);
	root["hash"]["md5"] = Json::Value(editionInfo.hash_md5);
	root["hash"]["sha256"] = Json::Value(editionInfo.hash_sha256);
	root["arch"] = Json::Value(arch);
	root["old_name"] = Json::Value(utf16ToUtf8(oldName));
	if (mandatoryUpdate) root["MandatoryUpdate"] = Json::Value(mandatoryUpdate);
	Json::StreamWriterBuilder outjson;
	outjson.settings_["emitUTF8"] = true;
	unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
	ostringstream jsonStream;
	jsonStream << "\xEF\xBB\xBF";
	if (writer->write(root, &jsonStream) != 0)
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}
	const string jsonBytes = jsonStream.str();
	if (!jsonStream || jsonBytes.empty() || jsonBytes.size() > 64 * 1024)
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}
	const HANDLE jsonFile = CreateFileW(stagedJsonPath.c_str(), GENERIC_WRITE, 0,
		nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (jsonFile == INVALID_HANDLE_VALUE)
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}
	DWORD jsonWritten = 0;
	const bool jsonReady = WriteFile(jsonFile, jsonBytes.data(),
		static_cast<DWORD>(jsonBytes.size()), &jsonWritten, nullptr) &&
		jsonWritten == jsonBytes.size() && FlushFileBuffers(jsonFile);
	const bool jsonClosed = CloseHandle(jsonFile) != 0;
	if (!jsonReady || !jsonClosed)
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}

	if (!MoveFileExW(payloadPath.c_str(), finalPath.c_str(),
		MOVEFILE_WRITE_THROUGH))
	{
		cleanupStage();
		return UpdateDownloadDamage;
	}
	const DWORD jsonAttributes = GetFileAttributesW(updateJsonPath.c_str());
	if ((jsonAttributes != INVALID_FILE_ATTRIBUTES &&
		(jsonAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) ||
		!MoveFileExW(stagedJsonPath.c_str(), updateJsonPath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		error_code ignored;
		filesystem::remove(finalPath, ignored);
		cleanupStage();
		return UpdateDownloadDamage;
	}
	cleanupStage();
	return UpdateRestart;
}

IdtAtomic<int> downloadLine = 1;

void AutomaticUpdate()
{
	bool state = true;
	bool update = true;

	bool against = false;
	int updateTimes = 0;

	UpdateTargetSnapshot updateTarget = GetUpdateTargetSnapshot();
	string updateArch = updateTarget.architecture;

	EditionInfoClass editionInfo;
	using enum AutomaticUpdateStateEnum;

updateStart:
	for (updateTimes = 0; !offSignal && updateTimes < 3; updateTimes++)
	{
		AutomaticUpdateState = UpdateObtainInformation;

		state = true;
		against = false;

		updateTarget = GetUpdateTargetSnapshot();
		updateArch = updateTarget.architecture;

		//获取最新版本信息
		if (state)
		{
			editionInfo = GetEditionInfo(updateTarget.channel, updateArch);

			if (editionInfo.errorCode != 200)
			{
				state = false, against = true;
				if (editionInfo.errorCode == 1) AutomaticUpdateState = UpdateInformationFail;
				else if (editionInfo.errorCode == 2) AutomaticUpdateState = UpdateInformationDamage;
				else AutomaticUpdateState = UpdateInformationUnStandardized;
			}
			else if (updateTarget.channel != editionInfo.channel)
			{
				SetUpdateChannelSnapshot(editionInfo.channel);
				WriteSetting();
			}
		}

		//下载最新版本
		if (state && editionInfo.editionDate != L"" && ((editionInfo.editionDate > editionDate && GetUpdateTargetSnapshot().enableAutoUpdate) || mandatoryUpdate))
		{
			update = true;
			if (_waccess((globalPath + L"installer\\update.json").c_str(), 4) == 0 && !mandatoryUpdate)
			{
				wstring tedition, tpath;
				string thash_md5, thash_sha256;
				string tchannel, tarch;

				StagedUpdateMetadata cached;
				bool fileDamage = !TryReadStagedUpdateMetadata(
					globalPath + L"installer\\update.json", cached);
				if (!fileDamage)
				{
					tedition = std::move(cached.edition);
					tpath = std::move(cached.path);
					thash_md5 = std::move(cached.md5);
					thash_sha256 = std::move(cached.sha256);
					tchannel = std::move(cached.channel);
					tarch = std::move(cached.arch);
				}

				if (!IsSafeStagedUpdatePath(tpath) ||
					!IsSafeUpdateHash(thash_md5, 32) ||
					!IsSafeUpdateHash(thash_sha256, 64))
					fileDamage = true;
				if (!fileDamage)
				{
					string hash_md5, hash_sha256;
					{
						hashwrapper* myWrapper = new md5wrapper();
						hash_md5 = myWrapper->getHashFromFileW(globalPath + tpath);
						delete myWrapper;
					}
					{
						hashwrapper* myWrapper = new sha256wrapper();
						hash_sha256 = myWrapper->getHashFromFileW(globalPath + tpath);
						delete myWrapper;
					}

					if (tedition == editionInfo.editionDate && _waccess((globalPath + tpath).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256 && editionInfo.channel == tchannel && updateArch == tarch)
					{
						if (!GetUpdateTargetSnapshot().enableAutoUpdate)
						{
							error_code ec;
							filesystem::remove(globalPath + L"installer\\update.json", ec);
						}
						else
						{
							update = false;
							AutomaticUpdateState = UpdateRestart;
						}
					}
				}
			}

			if (update)
			{
				downloadLine = 1;
				AutomaticUpdateState = UpdateDownloading;

				against = true;
				bool hasUpdateNew = false;
				bool updateTargetChanged = false;
				for (int i = 0; i < editionInfo.path_size; i++)
				{
					downloadLine = i + 1;
					AutomaticUpdateState = UpdateDownloading;

					AutomaticUpdateState = DownloadNewProgram(&downloadNewProgramState, editionInfo, editionInfo.path[i], updateArch);

					if (AutomaticUpdateState == UpdateRestart)
					{
						UpdateTargetSnapshot currentUpdateTarget = GetUpdateTargetSnapshot();
						if (currentUpdateTarget.channel != editionInfo.channel || currentUpdateTarget.architecture != updateArch)
						{
							error_code ec;
							filesystem::remove(globalPath + L"installer\\update.json", ec);

							updateTargetChanged = true;
							break;
						}

						against = false;

						if (mandatoryUpdate)
						{
							mandatoryUpdate = false;
							RestartProgram();
						}
						break;
					}
					else if (AutomaticUpdateState == UpdateNew && !mandatoryUpdate)
					{
						error_code ec;
						filesystem::remove(globalPath + L"installer\\update.json", ec);

						hasUpdateNew = true;
						break;
					}
				}

				if (updateTargetChanged)
				{
					AutomaticUpdateState = UpdateObtainInformation;
					continue;
				}
				if (hasUpdateNew) continue;
			}
		}
		else if (state && editionInfo.editionDate != L"")
		{
			if (editionInfo.editionDate > editionDate) AutomaticUpdateState = UpdateNew;
			else if (editionInfo.editionDate < editionDate) AutomaticUpdateState = UpdateNewer;
			else AutomaticUpdateState = UpdateLatest;
		}

		if (against && !mandatoryUpdate)
		{
			for (int i = 1; i <= 10; i++)
			{
				if (offSignal) break;
				if (AutomaticUpdateState == UpdateNotStarted || AutomaticUpdateState == UpdateObtainInformation) break;

				this_thread::sleep_for(chrono::seconds(1));
			}
		}
		else
		{
			against = mandatoryUpdate = false;
			for (int i = 1; i <= 1800; i++)
			{
				if (offSignal) break;
				if (AutomaticUpdateState == UpdateNotStarted || AutomaticUpdateState == UpdateObtainInformation) break;

				this_thread::sleep_for(chrono::seconds(1));
			}
			updateTimes = 0;
		}
	}

	for (; !offSignal;)
	{
		if (AutomaticUpdateState == UpdateNotStarted || AutomaticUpdateState == UpdateObtainInformation) goto updateStart;
		this_thread::sleep_for(chrono::seconds(1));
	}
}
