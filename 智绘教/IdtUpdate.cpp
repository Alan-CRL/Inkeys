#include "IdtUpdate.h"

//程序崩溃保护
void CrashedHandler()
{
	thread_status[L"CrashedHandler"] = true;

	this_thread::sleep_for(chrono::seconds(3));
	while (!already) this_thread::sleep_for(chrono::milliseconds(50));

	ofstream write;
	write.imbue(locale("zh_CN.UTF8"));

	write.open(wstring_to_string(string_to_wstring(global_path) + L"api\\open.txt").c_str());
	write << 1;
	write.close();

	if (!isProcessRunning((string_to_wstring(global_path) + L"api\\智绘教CrashedHandler.exe").c_str()))
		ShellExecute(NULL, NULL, (string_to_wstring(global_path) + L"api\\智绘教CrashedHandler.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);

	int value = 2;
	while (!off_signal)
	{
		write.open(wstring_to_string(string_to_wstring(global_path) + L"api\\open.txt").c_str());
		write << value;
		write.close();

		value++;
		if (value > 100000000) value = 2;

		Sleep(1000);
	}

	if (off_signal == 2)
	{
		write.open(wstring_to_string(string_to_wstring(global_path) + L"api\\open.txt").c_str());
		write << -2;
		write.close();
	}
	else
	{
		error_code ec;
		filesystem::remove(string_to_wstring(global_path) + L"api\\open.txt", ec);
	}

	thread_status[L"CrashedHandler"] = false;
	off_signal_ready = true;
}

//程序自动更新
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

int AutomaticUpdateStep = 0;
void AutomaticUpdate()
{
	this_thread::sleep_for(chrono::seconds(3));
	while (!already) this_thread::sleep_for(chrono::milliseconds(50));

	struct
	{
		wstring edition_date;
		wstring edition_code;
		wstring explain;
		wstring representation;
		wstring path[10];
		int path_size;

		string hash_md5, hash_sha256;
	}info;
	bool state = true;
	bool update = true;

	bool against = false;

	while (1)
	{
		state = true;
		against = false;

		//获取最新版本信息
		if (state && checkIsNetwork())
		{
			filesystem::create_directory(string_to_wstring(global_path) + L"installer"); //创建路径
			filesystem::remove(string_to_wstring(global_path) + L"installer\\new_download.json");
			info.edition_date = L"";
			info.edition_code = L"";
			info.explain = L"";
			info.representation = L"";
			info.hash_md5 = "";
			info.hash_sha256 = "";
			info.path_size = 0;

			HRESULT download1;
			{
				DeleteUrlCacheEntry(L"http://home.alan-crl.top");
				download1 = URLDownloadToFileW( // 从网络上下载数据到本地文件
					nullptr,                  // 在这里，写 nullptr 就行
					(L"http://home.alan-crl.top/version_identification/official_version.json?timestamp=" + getTimestamp()).c_str(), // 在这里写上网址
					stringtoLPCWSTR(global_path + "installer\\new_download.json"),            // 文件名写在这
					0,                        // 写 0 就对了
					nullptr                   // 也是，在这里写 nullptr 就行
				);
			}

			if (download1 == S_OK)
			{
				procedure_updata_error = 1;

				Json::Reader reader;
				Json::Value root;

				ifstream injson;
				injson.imbue(locale("zh_CN.UTF8"));
				injson.open(wstring_to_string(string_to_wstring(global_path) + L"installer\\new_download.json").c_str());

				if (reader.parse(injson, root))
				{
					if (root.isMember("LTS"))
					{
						info.edition_date = string_to_wstring(convert_to_gbk(root["LTS"]["edition_date"].asString()));
						info.edition_code = string_to_wstring(convert_to_gbk(root["LTS"]["edition_code"].asString()));
						info.explain = string_to_wstring(convert_to_gbk(root["LTS"]["explain"].asString()));

						info.hash_md5 = convert_to_gbk(root["LTS"]["hash"]["md5"].asString());
						info.hash_sha256 = convert_to_gbk(root["LTS"]["hash"]["sha256"].asString());

						info.path_size = root["LTS"]["path"].size();
						for (int i = 0; i < min(info.path_size, 10); i++)
						{
							info.path[i] = string_to_wstring(convert_to_gbk(root["LTS"]["path"][i].asString()));
						}
						info.representation = string_to_wstring(convert_to_gbk(root["LTS"]["representation"].asString()));
					}
				}

				injson.close();
			}
			else state = false;

			filesystem::remove(string_to_wstring(global_path) + L"installer\\new_download.json");
		}
		else procedure_updata_error = 2;

		//下载最新版本
		if (state && checkIsNetwork() && info.edition_date != L"" && info.edition_date > string_to_wstring(edition_date))
		{
			update = true, AutomaticUpdateStep = 2;
			if (_waccess((string_to_wstring(global_path) + L"installer\\update.json").c_str(), 4) == 0)
			{
				wstring tedition, tpath;
				string thash_md5, thash_sha256;

				Json::Reader reader;
				Json::Value root;

				ifstream readjson;
				readjson.imbue(locale("zh_CN.UTF8"));
				readjson.open(wstring_to_string(string_to_wstring(global_path) + L"installer\\update.json").c_str());

				if (reader.parse(readjson, root))
				{
					tedition = string_to_wstring(convert_to_gbk(root["edition"].asString()));
					tpath = string_to_wstring(convert_to_gbk(root["path"].asString()));

					thash_md5 = convert_to_gbk(root["hash"]["md5"].asString());
					thash_sha256 = convert_to_gbk(root["hash"]["sha256"].asString());
				}

				readjson.close();

				string hash_md5, hash_sha256;
				{
					hashwrapper* myWrapper = new md5wrapper();
					hash_md5 = myWrapper->getHashFromFile(global_path + wstring_to_string(tpath));
					delete myWrapper;
				}
				{
					hashwrapper* myWrapper = new sha256wrapper();
					hash_sha256 = myWrapper->getHashFromFile(global_path + wstring_to_string(tpath));
					delete myWrapper;
				}

				if (tedition >= info.edition_date && _waccess((string_to_wstring(global_path) + tpath).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
				{
					update = false;
				}
			}

			if (update)
			{
				filesystem::create_directory(string_to_wstring(global_path) + L"installer"); //创建路径
				filesystem::remove(string_to_wstring(global_path) + L"installer\\new_procedure.tmp");

				against = true;
				for (int i = 0; i < info.path_size; i++)
				{
					DeleteUrlCacheEntry(convertToHttp(get_domain_name(info.path[i])).c_str());

					HRESULT download2;
					{
						download2 = URLDownloadToFileW( // 从网络上下载数据到本地文件
							nullptr,                  // 在这里，写 nullptr 就行
							(convertToHttp(info.path[i]) + L"?timestamp=" + getTimestamp()).c_str(), // 在这里写上网址
							stringtoLPCWSTR(global_path + "installer\\new_procedure.tmp"),            // 文件名写在这
							0,                        // 写 0 就对了
							nullptr                   // 也是，在这里写 nullptr 就行
						);
					}

					if (download2 == S_OK)
					{
						wstring timestamp = getTimestamp();

						filesystem::remove(string_to_wstring(global_path) + L"installer\\new_procedure" + timestamp + L".exe");
						filesystem::remove(string_to_wstring(global_path) + L"installer\\" + info.representation + L".exe");

						std::error_code ec;
						filesystem::rename(string_to_wstring(global_path) + L"installer\\new_procedure.tmp", string_to_wstring(global_path) + L"installer\\new_procedure" + timestamp + L".zip", ec);
						if (ec) continue;

						{
							HZIP hz = OpenZip((string_to_wstring(global_path) + L"installer\\new_procedure" + timestamp + L".zip").c_str(), 0);
							SetUnzipBaseDir(hz, (string_to_wstring(global_path) + L"installer\\").c_str());
							ZIPENTRY ze;
							GetZipItem(hz, -1, &ze);
							int numitems = ze.index;
							for (int i = 0; i < numitems; i++)
							{
								GetZipItem(hz, i, &ze);
								UnzipItem(hz, i, ze.name);
							}
							CloseZip(hz);
						}

						filesystem::remove(string_to_wstring(global_path) + L"installer\\new_procedure" + timestamp + L".zip");
						filesystem::rename(string_to_wstring(global_path) + L"installer\\" + info.representation, string_to_wstring(global_path) + L"installer\\new_procedure" + timestamp + L".exe", ec);
						if (ec) continue;

						string hash_md5, hash_sha256;
						{
							hashwrapper* myWrapper = new md5wrapper();
							hash_md5 = myWrapper->getHashFromFile(global_path + "installer\\new_procedure" + wstring_to_string(timestamp) + ".exe");
							delete myWrapper;
						}
						{
							hashwrapper* myWrapper = new sha256wrapper();
							hash_sha256 = myWrapper->getHashFromFile(global_path + "installer\\new_procedure" + wstring_to_string(timestamp) + ".exe");
							delete myWrapper;
						}

						//创建 update.json 文件，指示更新
						if (info.hash_md5 == hash_md5 && info.hash_sha256 == hash_sha256)
						{
							Json::Value root;

							root["edition"] = Json::Value(wstring_to_string(info.edition_date));
							root["path"] = Json::Value("installer\\new_procedure" + wstring_to_string(timestamp) + ".exe");
							root["representation"] = Json::Value("new_procedure" + wstring_to_string(timestamp) + ".exe");

							root["hash"]["md5"] = Json::Value(info.hash_md5);
							root["hash"]["sha256"] = Json::Value(info.hash_sha256);

							Json::StreamWriterBuilder outjson;
							outjson.settings_["emitUTF8"] = true;
							std::unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
							ofstream writejson;
							writejson.imbue(locale("zh_CN.UTF8"));
							writejson.open(wstring_to_string(string_to_wstring(global_path) + L"installer\\update.json").c_str());
							writer->write(root, &writejson);
							writejson.close();

							against = false;
							AutomaticUpdateStep = 3;

							break;
						}
						else
						{
							AutomaticUpdateStep = 0;
							filesystem::remove(string_to_wstring(global_path) + L"installer\\new_procedure" + timestamp + L".exe");
						}
					}
				}
			}
		}
		else AutomaticUpdateStep = 1;

		if (against) this_thread::sleep_for(chrono::seconds(30));
		else this_thread::sleep_for(chrono::minutes(30));
	}
}

//程序注册 + 网络登记
//用户量过大后被废弃
/*
void NetUpdate()
{
	this_thread::sleep_for(chrono::seconds(3));
	while (!already) this_thread::sleep_for(chrono::milliseconds(50));
	if (userid.empty() || userid == L"无法正确识别") return;

	thread_status[L"NetUpdate"] = true;

	//server_updata_error
	//1 网络错误
	//2 info 信息获取错误

	error_code ec;
	if (_waccess((string_to_wstring(global_path) + L"opt\\info.ini").c_str(), 4) == 0)
	{
		Json::Reader reader;
		Json::Value root;

		ifstream readjson;
		readjson.imbue(locale("zh_CN.UTF8"));
		readjson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\info.ini").c_str());

		if (reader.parse(readjson, root))
		{
			if (root.isMember("server_feedback")) server_feedback = convert_to_gbk(root["server_feedback"].asString());
			if (root.isMember("server_code")) server_code = convert_to_gbk(root["server_code"].asString());
		}
		readjson.close();
	}

	for (int i = 1; !off_signal; i++)
	{
		if (checkIsNetwork())
		{
			wstring ip, adm1, adm2;
			wstring announcement_address;

			while (1)
			{
				//获取 info 信息

				filesystem::create_directory(string_to_wstring(global_path) + L"tmp", ec); //创建路径

				DeleteUrlCacheEntry(L"https://api.vore.top");

				wstring Download1Request = L"https://api.vore.top/api/IPdata?timestamp=" + getTimestamp();
				string Download1Path = global_path + "tmp\\info.json";
				HRESULT	download1 = URLDownloadToFileW( // 从网络上下载数据到本地文件
					nullptr,
					Download1Request.c_str(),
					stringtoLPCWSTR(Download1Path),
					0,
					nullptr
				);

				if (download1 != S_OK)
				{
					server_updata_error = 2;

					server_updata_error_reason = L"发生下载错误，位于 download1 处\n目标路径 " + string_to_wstring(Download1Path) + L"\n调用报错信息 ";

					if (download1 == S_OK) server_updata_error_reason += L"0";
					else if (download1 == E_ABORT) server_updata_error_reason += L"1";
					else if (download1 == E_ACCESSDENIED) server_updata_error_reason += L"2";
					else if (download1 == E_FAIL) server_updata_error_reason += L"3";
					else if (download1 == E_HANDLE) server_updata_error_reason += L"4";
					else if (download1 == E_INVALIDARG) server_updata_error_reason += L"5";
					else if (download1 == E_NOINTERFACE) server_updata_error_reason += L"6";
					else if (download1 == E_NOTIMPL) server_updata_error_reason += L"7";
					else if (download1 == E_OUTOFMEMORY) server_updata_error_reason += L"8";
					else if (download1 == E_POINTER) server_updata_error_reason += L"9";
					else if (download1 == E_UNEXPECTED) server_updata_error_reason += L"10";
					else server_updata_error_reason += L"-1";

					break;
				}

				{
					Json::Reader reader;
					Json::Value root;

					ifstream readjson;
					readjson.imbue(locale("zh_CN.UTF8"));
					readjson.open(wstring_to_string(string_to_wstring(global_path) + L"tmp\\info.json").c_str());

					if (!(reader.parse(readjson, root) && root.isMember("code") && root["code"].asInt() == 200))
					{
						server_updata_error = 3.1;
						break;
					}
					readjson.close();
					filesystem::remove_all(string_to_wstring(global_path) + L"tmp", ec); //删除路径

					if (!(root.isMember("ipinfo") && root["ipinfo"].isObject() && root["ipinfo"].isMember("text")))
					{
						server_updata_error = 3.2;
						break;
					}
					if (!(root.isMember("ipdata") && root["ipdata"].isObject() && root["ipdata"].isMember("info1") && root["ipdata"].isMember("info2")))
					{
						server_updata_error = 3.3;
						break;
					}

					ip = string_to_wstring(convert_to_gbk(root["ipinfo"]["text"].asString()));
					adm1 = string_to_wstring(convert_to_gbk(root["ipdata"]["info1"].asString()));
					adm2 = string_to_wstring(convert_to_gbk(root["ipdata"]["info2"].asString()));
				}

				if (off_signal) break;
				//获取公告板内容

				{
					filesystem::create_directory(string_to_wstring(global_path) + L"tmp", ec); //创建路径

					DeleteUrlCacheEntry(L"http://api.a20safe.com");

					wstring Download2Request = L"http://api.a20safe.com/api.php?api=14&key=" + api_key + L"&timestamp=" + getTimestamp();
					string Download2Path = global_path + "tmp\\info.json";
					HRESULT	download2 = URLDownloadToFileW(
						nullptr,
						Download2Request.c_str(),
						stringtoLPCWSTR(Download2Path),
						0,
						nullptr
					);

					if (download2 != S_OK)
					{
						server_updata_error = 4;

						server_updata_error_reason = L"发生下载错误，位于 download2 处\n目标路径 " + string_to_wstring(Download2Path) + L"\n调用报错信息 ";

						if (download2 == S_OK) server_updata_error_reason += L"0";
						else if (download2 == E_ABORT) server_updata_error_reason += L"1";
						else if (download2 == E_ACCESSDENIED) server_updata_error_reason += L"2";
						else if (download2 == E_FAIL) server_updata_error_reason += L"3";
						else if (download2 == E_HANDLE) server_updata_error_reason += L"4";
						else if (download2 == E_INVALIDARG) server_updata_error_reason += L"5";
						else if (download2 == E_NOINTERFACE) server_updata_error_reason += L"6";
						else if (download2 == E_NOTIMPL) server_updata_error_reason += L"7";
						else if (download2 == E_OUTOFMEMORY) server_updata_error_reason += L"8";
						else if (download2 == E_POINTER) server_updata_error_reason += L"9";
						else if (download2 == E_UNEXPECTED) server_updata_error_reason += L"10";
						else server_updata_error_reason += L"-1";

						break;
					}

					{
						Json::Reader reader;
						Json::Value root;

						ifstream readjson;
						readjson.imbue(locale("zh_CN.UTF8"));
						readjson.open(wstring_to_string(string_to_wstring(global_path) + L"tmp\\info.json").c_str());

						if (!(reader.parse(readjson, root) && root.isMember("code") && root["code"].asInt() == 0))
						{
							server_updata_error = 5.1;
							break;
						}
						readjson.close();
						filesystem::remove_all(string_to_wstring(global_path) + L"tmp", ec); //删除路径

						announcement_address = string_to_wstring(convert_to_gbk(root["data"][0]["url"].asString()));
						if (announcement_address.empty())
						{
							server_updata_error = 5.2;
							break;
						}
					}
				}

				int update = 1;
				{
					filesystem::create_directory(string_to_wstring(global_path) + L"tmp", ec); //创建路径

					DeleteUrlCacheEntry(L"http://board.a20safe.com");

					wstring Download3Request = convertToHttp(announcement_address) + L"?timestamp=" + getTimestamp();
					string Download3Path = global_path + "tmp\\info.txt";
					HRESULT	download3 = URLDownloadToFileW(
						nullptr,
						Download3Request.c_str(),
						stringtoLPCWSTR(Download3Path),
						0,
						nullptr
					);

					if (download3 != S_OK)
					{
						server_updata_error = 6;

						server_updata_error_reason = L"发生下载错误，位于 download3 处\n目标路径 " + string_to_wstring(Download3Path) + L"\n调用报错信息 ";

						if (download3 == S_OK) server_updata_error_reason += L"0";
						else if (download3 == E_ABORT) server_updata_error_reason += L"1";
						else if (download3 == E_ACCESSDENIED) server_updata_error_reason += L"2";
						else if (download3 == E_FAIL) server_updata_error_reason += L"3";
						else if (download3 == E_HANDLE) server_updata_error_reason += L"4";
						else if (download3 == E_INVALIDARG) server_updata_error_reason += L"5";
						else if (download3 == E_NOINTERFACE) server_updata_error_reason += L"6";
						else if (download3 == E_NOTIMPL) server_updata_error_reason += L"7";
						else if (download3 == E_OUTOFMEMORY) server_updata_error_reason += L"8";
						else if (download3 == E_POINTER) server_updata_error_reason += L"9";
						else if (download3 == E_UNEXPECTED) server_updata_error_reason += L"10";
						else server_updata_error_reason += L"-1";

						break;
					}

					if (off_signal) break;
					{
						//Test();

						string whole;
						string tuserid, tip, tadm1, tadm2, tversion, prat;

						ifstream readjson;
						readjson.imbue(locale("zh_CN.UTF8"));
						readjson.open(wstring_to_string(string_to_wstring(global_path) + L"tmp\\info.txt").c_str());

						stringstream buffer;
						buffer << readjson.rdbuf();
						whole = buffer.str();

						readjson.close();
						filesystem::remove_all(string_to_wstring(global_path) + L"tmp", ec); //删除路径

						int l, r;
						for (int i = 0; i < (int)whole.length(); i++)
						{
						whole_start:

							l = r = -1;
							for (; i < (int)whole.length(); i++)
							{
								if (whole[i] == '[')
								{
									l = i;
									break;
								}
							}
							if (l == -1) break;

							i++, r = -1;
							for (int tmp = i; i < (int)whole.length(); i++)
							{
								if (whole[i] == ';')
								{
									r = i;
									tuserid = whole.substr(tmp, i - tmp);

									break;
								}
								else if (whole[i] == ']' || whole[i] == '[') goto whole_start;
							}
							if (r == -1) break;

							i++, r = -1;
							for (int tmp = i; i < (int)whole.length(); i++)
							{
								if (whole[i] == ';')
								{
									r = i;
									tip = whole.substr(tmp, i - tmp);

									break;
								}
								else if (whole[i] == ']' || whole[i] == '[') goto whole_start;
							}
							if (r == -1) break;

							i++, r = -1;
							for (int tmp = i; i < (int)whole.length(); i++)
							{
								if (whole[i] == ';')
								{
									r = i;
									tadm1 = whole.substr(tmp, i - tmp);

									break;
								}
								else if (whole[i] == ']' || whole[i] == '[') goto whole_start;
							}
							if (r == 0) break;

							i++, r = -1;
							for (int tmp = i; i < (int)whole.length(); i++)
							{
								if (whole[i] == ';')
								{
									r = i;
									tadm2 = whole.substr(tmp, i - tmp);

									break;
								}
								else if (whole[i] == ']' || whole[i] == '[') goto whole_start;
							}
							if (r == -1) break;

							i++, r = -1;
							for (int tmp = i; i < (int)whole.length(); i++)
							{
								if (whole[i] == ';')
								{
									r = i;
									tversion = whole.substr(tmp, i - tmp);

									break;
								}
								else if (whole[i] == ']' || whole[i] == '[') goto whole_start;
							}
							if (r == -1) break;

							i++, r = -1;
							for (int tmp = i; i < (int)whole.length(); i++)
							{
								if (whole[i] == ']')
								{
									r = i;
									prat = whole.substr(tmp, i - tmp);

									break;
								}
								else if (whole[i] == ';') goto whole_start;
							}
							if (r == -1) break;

							if (0 && string_to_wstring(tuserid) == userid && (string_to_wstring(tip) != ip || string_to_wstring(tadm1) != adm1 || string_to_wstring(tadm2) != adm2 || tversion != edition_date)) L"有意空受控语句";
							if (string_to_wstring(tuserid) == userid)
							{
								string tserver_code = "", tserver_feedback = "";
								{
									int lx, rx, inx = 0;
									for (int i = 0; i < (int)prat.length(); i++)
									{
										//起始定位
										lx = rx = -1;
										for (; i < (int)prat.length(); i++)
										{
											if (prat[i] == ',')
											{
												lx = i;
												break;
											}
											else if (i == (int)prat.length() - 1) lx = -1, inx = 1;
										}
										if (lx == -1 || inx == 1) break;

										i++, rx = -1;
										for (; i < (int)prat.length(); i++)
										{
											if (prat[i] == ',' || i == (int)prat.length() - 1)
											{
												if (i == (int)prat.length() - 1) rx = i + 1;
												else rx = i;

												tserver_code = prat.substr(lx + 1, rx - lx - 1);

												if (i == (int)prat.length() - 1) inx = 1;
												else lx = i;

												break;
											}
										}
										if (rx == -1 || inx == 1) break;

										//起始定位
										i++, rx = -1;
										for (; i < (int)prat.length(); i++)
										{
											if (prat[i] == ',' || i == (int)prat.length() - 1)
											{
												if (i == (int)prat.length() - 1) rx = i + 1;
												else rx = i;

												tserver_feedback = prat.substr(lx + 1, rx - lx - 1);

												if (i == (int)prat.length() - 1) inx = 1;
												else lx = i;

												break;
											}
										}
										if (rx == -1 || inx == 1) break;
									}
								}

								server_feedback = tserver_feedback;
								server_code = tserver_code;

								if (server_feedback != "" || server_code != "") whole.replace(l, r - l + 1, ("[" + wstring_to_string(userid) + ";" + wstring_to_string(ip) + ";" + wstring_to_string(adm1) + ";" + wstring_to_string(adm2) + ";" + edition_date + ";" + GetCurrentTimeAll() + "," + server_code + "," + server_feedback + "]"));
								else whole.replace(l, r - l + 1, ("[" + wstring_to_string(userid) + ";" + wstring_to_string(ip) + ";" + wstring_to_string(adm1) + ";" + wstring_to_string(adm2) + ";" + edition_date + ";" + GetCurrentTimeAll() + "]"));

								update = 2;

								//update = 0;
								//为了进一步监测使用情况，每次打开时数据库都会进行统计

								break;
							}

							if (tuserid.empty()) break;
						}

						//更新本地配置
						{
							Json::StyledWriter outjson;
							Json::Value root;
							root["userid"] = Json::Value(wstring_to_string(userid));
							root["ip"] = Json::Value(wstring_to_string(ip));
							root["adm1"] = Json::Value(wstring_to_string(adm1));
							root["adm2"] = Json::Value(wstring_to_string(adm2));
							root["server_feedback"] = Json::Value(convert_to_utf8(server_feedback));
							root["server_code"] = Json::Value(convert_to_utf8(server_code));

							Json::StreamWriterBuilder OutjsonBuilder;
							OutjsonBuilder.settings_["emitUTF8"] = true;
							std::unique_ptr<Json::StreamWriter> writer(OutjsonBuilder.newStreamWriter());
							ofstream writejson;
							writejson.imbue(locale("zh_CN.UTF8"));
							writejson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\info.ini").c_str());
							writer->write(root, &writejson);
							writejson.close();
						}

						if (off_signal) break;
						//更新
						if (update)
						{
							filesystem::create_directory(string_to_wstring(global_path) + L"tmp", ec); //创建路径

							if (update == 1)
							{
								if (whole.length()) whole += "\n";
								whole += ("[" + wstring_to_string(userid) + ";" + wstring_to_string(ip) + ";" + wstring_to_string(adm1) + ";" + wstring_to_string(adm2) + ";" + edition_date + ";" + GetCurrentTimeAll() + "]");
							}

							DeleteUrlCacheEntry(L"http://api.a20safe.com");
							HRESULT	download4 = URLDownloadToFileW( // 从网络上下载数据到本地文件
								nullptr,                  // 在这里，写 nullptr 就行
								(L"http://api.a20safe.com/api.php?api=14&key=" + api_key + L"&t=" + string_to_wstring(convert_to_urlencode(whole)) + L"&timestamp=" + getTimestamp()).c_str(), // 在这里写上网址
								stringtoLPCWSTR(global_path + "tmp\\info.json"),            // 文件名写在这
								0,                        // 写 0 就对了
								nullptr                   // 也是，在这里写 nullptr 就行
							);

							filesystem::remove_all(string_to_wstring(global_path) + L"tmp", ec); //删除路径
						}
					}
				}

				break;
			}
		}
		else server_updata_error = 1;

		for (int i = 1; !off_signal && i <= 1800; i++)
		{
			this_thread::sleep_for(chrono::seconds(1));
		}
	}

	thread_status[L"NetUpdate"] = false;
}
*/