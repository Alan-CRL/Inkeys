﻿#include "IdtUpdate.h"

#include "IdtConfiguration.h"
#include "IdtOther.h"
#include "IdtSetting.h"
#include "IdtText.h"
#include "IdtTime.h"
#include "IdtWindow.h"
#include "IdtNet.h"

// 程序自动更新

bool mandatoryUpdate; // 强制更新符号
bool inconsistentArchitecture;
AutomaticUpdateStateEnum AutomaticUpdateState;
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

	string editionInformation = GetEditionInformation();
	if (editionInformation == "Error")
	{
		retEditionInfo.errorCode = 1;
		return retEditionInfo;
	}

	istringstream jsonContentStream(editionInformation);
	Json::CharReaderBuilder readerBuilder;
	Json::Value editionInfoValue;
	string jsonErr;
	if (Json::parseFromStream(readerBuilder, jsonContentStream, &editionInfoValue, &jsonErr))
	{
		bool informationCompliance = true;
		int tryTime = 0;

	getInfoStart:
		if (editionInfoValue.isMember(channel))
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
					for (int i = 0; i < min(editionInfoValue[channel][path].size(), 10); i++)
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
	// 更新后的正则表达式，捕获前缀，并要求域名中至少包含一个点
	regex url_regex(R"(^\s*(?:([a-zA-Z]+://))?((?:[a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}(?::\d+)?)(/\S*)?\s*$)", regex::icase);
	smatch url_match_result;

	if (regex_match(input_url, url_match_result, url_regex))
	{
		prefix = url_match_result[1].matched ? url_match_result[1].str() : "";
		domain = url_match_result[2].str();
		path = url_match_result[3].matched ? url_match_result[3].str() : "";
	}
	else
	{
		prefix.clear();
		domain.clear();
		path.clear();
	}
}
AutomaticUpdateStateEnum DownloadNewProgram(DownloadNewProgramStateClass* state, EditionInfoClass editionInfo, string url, string arch)
{
	using enum AutomaticUpdateStateEnum;

	error_code ec;
	if (_waccess((globalPath + L"installer").c_str(), 4) == 0)
	{
		filesystem::remove_all(globalPath + L"installer", ec);
		filesystem::create_directory(globalPath + L"installer", ec);
	}
	else filesystem::create_directory(globalPath + L"installer", ec);

	string prefix, domain, path;
	splitUrl(url, prefix, domain, path);

	state->downloadedSize.store(0);
	state->fileSize.store(editionInfo.fileSize.load());

	wstring timestamp = getTimestamp();
	bool reslut = DownloadEdition(domain, path, globalPath + L"installer\\", L"new_procedure_" + timestamp + L".tmp", state->downloadedSize);

	if (reslut)
	{
		error_code ec;
		filesystem::remove(globalPath + L"installer\\new_procedure_" + timestamp + L".exe", ec);
		filesystem::remove(globalPath + L"installer\\" + editionInfo.representation, ec);

		filesystem::rename(globalPath + L"installer\\new_procedure_" + timestamp + L".tmp", globalPath + L"installer\\new_procedure_" + timestamp + L".zip", ec);
		if (ec) return UpdateDownloadDamage;

		HZIP hz = OpenZip((globalPath + L"installer\\new_procedure_" + timestamp + L".zip").c_str(), 0);
		SetUnzipBaseDir(hz, (globalPath + L"installer").c_str());
		ZIPENTRY ze;
		GetZipItem(hz, -1, &ze);
		int numitems = ze.index;
		for (int i = 0; i < numitems; i++)
		{
			GetZipItem(hz, i, &ze);
			UnzipItem(hz, i, ze.name);
		}
		CloseZip(hz);

		filesystem::remove(globalPath + L"installer\\new_procedure_" + timestamp + L".zip", ec);
		filesystem::rename(globalPath + L"installer\\" + editionInfo.representation, globalPath + L"installer\\new_procedure_" + timestamp + L".exe", ec);
		if (ec) return UpdateDownloadDamage;

		string hash_md5, hash_sha256;
		{
			hashwrapper* myWrapper = new md5wrapper();
			hash_md5 = myWrapper->getHashFromFileW(globalPath + L"installer\\new_procedure_" + timestamp + L".exe");
			delete myWrapper;
		}
		{
			hashwrapper* myWrapper = new sha256wrapper();
			hash_sha256 = myWrapper->getHashFromFileW(globalPath + L"installer\\new_procedure_" + timestamp + L".exe");
			delete myWrapper;
		}

		//创建 update.json 文件，指示更新
		if (editionInfo.hash_md5 == hash_md5 && editionInfo.hash_sha256 == hash_sha256)
		{
			if (!setlist.enableAutoUpdate && !mandatoryUpdate)
			{
				error_code ec;
				filesystem::remove(globalPath + L"installer\\new_procedure" + timestamp + L".exe", ec);

				return UpdateNew;
			}
			else
			{
				Json::Value root;

				root["edition"] = Json::Value(utf16ToUtf8(editionInfo.editionDate));
				root["path"] = Json::Value("installer\\new_procedure_" + utf16ToUtf8(timestamp) + ".exe");
				root["representation"] = Json::Value("new_procedure_" + utf16ToUtf8(timestamp) + ".exe");

				root["hash"]["md5"] = Json::Value(editionInfo.hash_md5);
				root["hash"]["sha256"] = Json::Value(editionInfo.hash_sha256);

				root["arch"] = Json::Value(arch);

				root["old_name"] = Json::Value(utf16ToUtf8(GetCurrentExeName()));
				if (mandatoryUpdate) root["MandatoryUpdate"] = Json::Value(mandatoryUpdate);

				Json::StreamWriterBuilder outjson;
				outjson.settings_["emitUTF8"] = true;
				unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
				ofstream writejson(globalPath + L"installer\\update.json", ios::binary);
				writejson << "\xEF\xBB\xBF";
				writer->write(root, &writejson);
				writejson.close();
			}
		}
		else
		{
			error_code ec;
			filesystem::remove(globalPath + L"installer\\new_procedure" + timestamp + L".exe", ec);

			return UpdateDownloadDamage;
		}
	}
	else return UpdateDownloadFail;

	return UpdateRestart;
}

void AutomaticUpdate()
{
	bool state = true;
	bool update = true;

	bool against = false;
	int updateTimes = 0;

	string updateArch = setlist.updateArchitecture;

	EditionInfoClass editionInfo;
	using enum AutomaticUpdateStateEnum;

updateStart:
	for (updateTimes = 0; !offSignal && updateTimes < 3; updateTimes++)
	{
		AutomaticUpdateState = UpdateObtainInformation;

		state = true;
		against = false;

		updateArch = setlist.updateArchitecture;

		//获取最新版本信息
		if (state)
		{
			editionInfo = GetEditionInfo(setlist.UpdateChannel, updateArch);

			if (editionInfo.errorCode != 200)
			{
				state = false, against = true;
				if (editionInfo.errorCode == 1) AutomaticUpdateState = UpdateInformationFail;
				else if (editionInfo.errorCode == 2) AutomaticUpdateState = UpdateInformationDamage;
				else AutomaticUpdateState = UpdateInformationUnStandardized;
			}
			else if (setlist.UpdateChannel != editionInfo.channel)
			{
				setlist.UpdateChannel = editionInfo.channel;
				WriteSetting();
			}
		}

		//下载最新版本
		if (state && editionInfo.editionDate != L"" && ((editionInfo.editionDate > editionDate && setlist.enableAutoUpdate) || mandatoryUpdate))
		{
			update = true;
			if (_waccess((globalPath + L"installer\\update.json").c_str(), 4) == 0 && !mandatoryUpdate)
			{
				wstring tedition, tpath;
				string thash_md5, thash_sha256;
				string tarch;

				Json::Reader reader;
				Json::Value root;

				ifstream readjson;
				readjson.imbue(locale("zh_CN.UTF8"));
				readjson.open((globalPath + L"installer\\update.json").c_str());

				bool fileDamage = false;
				if (reader.parse(readjson, root))
				{
					if (root.isMember("edition")) tedition = utf8ToUtf16(root["edition"].asString());
					else fileDamage = true;
					if (root.isMember("path")) tpath = utf8ToUtf16(root["path"].asString());
					else fileDamage = true;

					if (root.isMember("hash"))
					{
						if (root["hash"].isMember("md5")) thash_md5 = root["hash"]["md5"].asString();
						else fileDamage = true;
						if (root["hash"].isMember("sha256")) thash_sha256 = root["hash"]["sha256"].asString();
						else fileDamage = true;
					}
					else fileDamage = true;

					// 架构确定
					if (root.isMember("arch")) tarch = root["arch"].asString();
					else fileDamage = true;
				}
				readjson.close();

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

					if (tedition == editionInfo.editionDate && _waccess((globalPath + tpath).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256 && updateArch == tarch)
					{
						update = false;
						AutomaticUpdateState = UpdateRestart;
					}
				}
			}
			if (update)
			{
				AutomaticUpdateState = UpdateDownloading;

				against = true;
				bool hasUpdateNew = false;
				for (int i = 0; i < editionInfo.path_size; i++)
				{
					AutomaticUpdateState = DownloadNewProgram(&downloadNewProgramState, editionInfo, editionInfo.path[i], updateArch);
					if (AutomaticUpdateState == UpdateRestart)
					{
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
						hasUpdateNew = true;
						break;
					}
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