#include "IdtUpdate.h"

#include "IdtConfiguration.h"
#include "IdtHash.h"
#include "IdtOther.h"
#include "IdtSetting.h"
#include "IdtText.h"
#include "IdtTime.h"
#include "IdtWindow.h"
#include "IdtNet.h"

//程序崩溃保护
void CrashedHandler()
{
	threadStatus[L"CrashedHandler"] = true;

	ofstream write;
	write.imbue(locale("zh_CN.UTF8"));

	write.open(globalPath + L"api\\open.txt");
	write << 1;
	write.close();

	// 启动崩溃重启助手
	{
		bool start = true;

		// 检查本地文件完整性
		{
			if (_waccess((globalPath + L"api").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directory(globalPath + L"api", ec);
			}

			if (_waccess((globalPath + L"api\\智绘教CrashedHandler.exe").c_str(), 0) == -1)
				if (!ExtractResource((globalPath + L"api\\智绘教CrashedHandler.exe").c_str(), L"EXE", MAKEINTRESOURCE(201)))
					start = false;

			{
				string hash_md5, hash_sha256;
				{
					hashwrapper* myWrapper = new md5wrapper();
					hash_md5 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教CrashedHandler.exe");
					delete myWrapper;
				}
				{
					hashwrapper* myWrapper = new sha256wrapper();
					hash_sha256 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教CrashedHandler.exe");
					delete myWrapper;
				}

				if (hash_md5 != CrashedHandlerMd5 || hash_sha256 != CrashedHandlerSHA256)
					if (!ExtractResource((globalPath + L"api\\智绘教CrashedHandler.exe").c_str(), L"EXE", MAKEINTRESOURCE(201)))
						start = false;
			}
		}

		// 启动崩溃助手
		if (start && !isProcessRunning((globalPath + L"api\\智绘教CrashedHandler.exe").c_str()))
		{
			wstring path = GetCurrentExePath();
			ShellExecute(NULL, NULL, (globalPath + L"api\\智绘教CrashedHandler.exe").c_str(), (L"/\"" + path + L"\"").c_str(), NULL, SW_SHOWNORMAL);
		}
	}

	int value = 2;
	while (!offSignal)
	{
		write.open(globalPath + L"api\\open.txt");
		write << value;
		write.close();

		value++;
		if (value > 100000000) value = 2;

		this_thread::sleep_for(chrono::milliseconds(1000));
	}

	if (offSignal == 2)
	{
		write.open(globalPath + L"api\\open.txt");
		write << -2;
		write.close();
	}
	else
	{
		error_code ec;
		filesystem::remove(globalPath + L"api\\open.txt", ec);
	}

	threadStatus[L"CrashedHandler"] = false;
	offSignalReady = true;
}

//程序自动更新
int AutomaticUpdateStep = 0;
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

EditionInfoClass GetEditionInfo(string channel)
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

	getInfoStart:
		if (editionInfoValue.isMember(channel))
		{
			if (editionInfoValue[channel].isMember("edition_date")) retEditionInfo.editionDate = utf8ToUtf16(editionInfoValue[channel]["edition_date"].asString());
			else informationCompliance = false;
			if (editionInfoValue[channel].isMember("edition_code")) retEditionInfo.editionCode = utf8ToUtf16(editionInfoValue[channel]["edition_code"].asString());
			// else informationCompliance = false;
			if (editionInfoValue[channel].isMember("explain")) retEditionInfo.explain = utf8ToUtf16(editionInfoValue[channel]["explain"].asString());
			// else informationCompliance = false;
			if (editionInfoValue[channel].isMember("hash"))
			{
				if (editionInfoValue[channel]["hash"].isMember("md5")) retEditionInfo.hash_md5 = editionInfoValue[channel]["hash"]["md5"].asString();
				else informationCompliance = false;
				if (editionInfoValue[channel]["hash"].isMember("sha256")) retEditionInfo.hash_sha256 = editionInfoValue[channel]["hash"]["sha256"].asString();
				else informationCompliance = false;
			}
			else informationCompliance = false;

			if (editionInfoValue[channel].isMember("path"))
			{
				retEditionInfo.path_size = editionInfoValue[channel]["path"].size();
				for (int i = 0; i < min(retEditionInfo.path_size, 10); i++) retEditionInfo.path[i] = editionInfoValue[channel]["path"][i].asString();
				if (retEditionInfo.path_size <= 0) informationCompliance = false;
			}
			else informationCompliance = false;

			if (editionInfoValue[channel].isMember("representation")) retEditionInfo.representation = utf8ToUtf16(editionInfoValue[channel]["representation"].asString());
			else informationCompliance = false;
		}
		else informationCompliance = false;

		// 失败则尝试其他通道
		if (!informationCompliance)
		{
			informationCompliance = true;

			// 尝试 LTS 通道
			if (channel != "LTS")
			{
				channel = "LTS";
				goto getInfoStart;
			}
			// 尝试第一个通道
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
int DownloadNewProgram(DownloadNewProgramStateClass* state, EditionInfoClass editionInfo, string url)
{
	error_code ec;
	if (_waccess((globalPath + L"installer").c_str(), 4) == 0)
	{
		filesystem::remove_all(globalPath + L"installer", ec);
		filesystem::create_directory(globalPath + L"installer", ec);
	}
	else filesystem::create_directory(globalPath + L"installer", ec);

	string prefix, domain, path;
	splitUrl(url, prefix, domain, path);

	wstring timestamp = getTimestamp();
	bool reslut = DownloadEdition(domain, path, globalPath + L"installer\\", L"new_procedure_" + timestamp + L".tmp", state->fileSize, state->downloadedSize);

	if (reslut)
	{
		error_code ec;
		filesystem::remove(globalPath + L"installer\\new_procedure_" + timestamp + L".exe", ec);
		filesystem::remove(globalPath + L"installer\\" + editionInfo.representation, ec);

		filesystem::rename(globalPath + L"installer\\new_procedure_" + timestamp + L".tmp", globalPath + L"installer\\new_procedure_" + timestamp + L".zip", ec);
		if (ec) return 7;

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
		if (ec) return 7;

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
			Json::Value root;

			root["edition"] = Json::Value(utf16ToUtf8(editionInfo.editionDate));
			root["path"] = Json::Value("installer\\new_procedure_" + utf16ToUtf8(timestamp) + ".exe");
			root["representation"] = Json::Value("new_procedure_" + utf16ToUtf8(timestamp) + ".exe");

			root["hash"]["md5"] = Json::Value(editionInfo.hash_md5);
			root["hash"]["sha256"] = Json::Value(editionInfo.hash_sha256);

			root["old_name"] = Json::Value(utf16ToUtf8(GetCurrentExeName()));

			Json::StreamWriterBuilder outjson;
			outjson.settings_["emitUTF8"] = true;
			unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
			ofstream writejson(globalPath + L"installer\\update.json", ios::binary);
			writejson << "\xEF\xBB\xBF";
			writer->write(root, &writejson);
			writejson.close();
		}
		else
		{
			error_code ec;
			filesystem::remove(globalPath + L"installer\\new_procedure" + timestamp + L".exe", ec);

			return 7;
		}
	}
	else return 6;

	return 8;
}

void AutomaticUpdate()
{
	/*
	AutomaticUpdateStep 含义
	0 自动更新未启动
	1 获取新版本信息中
	2 下载版本信息失败
	3 下载版本信息损坏
	4 下载版本信息不符合规范
	5 新版本正极速下载中
	6 下载最新版本程序失败
	7 下载最新版本程序损坏
	8 重启更新到最新版本
	9 程序已经是最新版本
	10 存在新版本，但是用户未开启自动更新
	*/

	bool state = true;
	bool update = true;

	bool against = false;
	int updateTimes = 0;

	EditionInfoClass editionInfo;
	DownloadNewProgramStateClass downloadNewProgramState;

	string channel = setlist.UpdateChannel;

	for (; !offSignal && updateTimes <= 10; updateTimes++)
	{
		AutomaticUpdateStep = 1;

		state = true;
		against = false;

		//获取最新版本信息
		if (state)
		{
			editionInfo = GetEditionInfo(channel);

			if (editionInfo.errorCode != 200)
			{
				state = false;
				if (editionInfo.errorCode == 1) AutomaticUpdateStep = 2;
				else if (editionInfo.errorCode == 2) AutomaticUpdateStep = 3;
				else AutomaticUpdateStep = 4;
			}
			else if (channel != editionInfo.channel)
			{
				setlist.UpdateChannel = channel = editionInfo.channel;
				WriteSetting();
			}
		}

		//下载最新版本
		if (state && editionInfo.editionDate != L"" && editionInfo.editionDate > editionDate)
		{
			update = true;
			if (_waccess((globalPath + L"installer\\update.json").c_str(), 4) == 0)
			{
				wstring tedition, tpath;
				string thash_md5, thash_sha256;

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

					if (tedition >= editionInfo.editionDate && _waccess((globalPath + tpath).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
					{
						update = false;
						AutomaticUpdateStep = 8;
					}
				}
			}

			if (update)
			{
				AutomaticUpdateStep = 5;

				against = true;
				for (int i = 0; i < editionInfo.path_size; i++)
				{
					int result = DownloadNewProgram(&downloadNewProgramState, editionInfo, editionInfo.path[i]);
					AutomaticUpdateStep = result;
					if (AutomaticUpdateStep == 8)
					{
						against = false;
						break;
					}
				}
			}
		}
		else if (state && editionInfo.editionDate != L"" && editionInfo.editionDate <= editionDate) AutomaticUpdateStep = 9;

		if (against)
		{
			for (int i = 1; i <= 10; i++)
			{
				if (offSignal) break;
				if (channel != setlist.UpdateChannel)
				{
					channel = setlist.UpdateChannel;
					break;
				}

				this_thread::sleep_for(chrono::seconds(1));
			}
		}
		else
		{
			for (int i = 1; i <= 1800; i++)
			{
				if (offSignal) break;
				if (channel != setlist.UpdateChannel)
				{
					channel = setlist.UpdateChannel;
					break;
				}

				this_thread::sleep_for(chrono::seconds(1));
			}
			updateTimes = 0;
		}
	}
}