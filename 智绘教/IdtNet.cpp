#include "IdtNet.h"

#pragma comment(lib, "libssl_static.lib")
#pragma comment(lib, "libcrypto_static.lib")

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "cpphttplib/httplib.h"

std::string GetEditionInformation()
{
	httplib::Result res;
	httplib::Headers headers = { {"Cache-Control", "no-cache"}, {"Pragma", "no-cache"} };

	// 尝试主地址
	{
		httplib::SSLClient scli("vip.123pan.cn");
		scli.set_follow_location(true);
		scli.set_connection_timeout(5);
		scli.set_read_timeout(10);

		// 尝试 Https 连接
		res = scli.Get("/1709404/version_identification/official_version.json", headers);
		if (!res || res->status != 200)
		{
			// 失败后尝试使用 Http 连接
			httplib::Client cli("vip.123pan.cn");
			scli.set_follow_location(true);
			scli.set_connection_timeout(5);
			scli.set_read_timeout(10);

			res = cli.Get("/1709404/version_identification/official_version.json", headers);
		}

		if (res && res->status == 200)
		{
			std::string ret = res->body;
			if (ret.compare(0, 3, "\xEF\xBB\xBF") == 0) ret = ret.substr(3);
			return ret;
		}
	}
	// 尝试备用地址
	{
		httplib::SSLClient scli("home.alan-crl.top");
		scli.set_follow_location(true);
		scli.set_connection_timeout(5);
		scli.set_read_timeout(10);

		// 尝试 Https 连接
		res = scli.Get("/version_identification/official_version.json", headers);
		if (!res || res->status != 200)
		{
			// 失败后尝试使用 Http 连接
			httplib::Client cli("home.alan-crl.top");
			scli.set_follow_location(true);
			scli.set_connection_timeout(5);
			scli.set_read_timeout(10);

			res = cli.Get("/version_identification/official_version.json", headers);
		}

		if (res && res->status == 200)
		{
			std::string ret = res->body;
			if (ret.compare(0, 3, "\xEF\xBB\xBF") == 0) ret = ret.substr(3);
			return ret;
		}
	}

	return "Error";
}
bool DownloadEdition(std::string domain, std::string path, std::wstring directory, std::wstring fileName, long long& fileSize, long long& downloadedSize)
{
	httplib::Result res;
	httplib::Headers headers = { {"Cache-Control", "no-cache"}, {"Pragma", "no-cache"} };
	fileSize = 0, downloadedSize = 0;

	httplib::SSLClient scli(domain);
	scli.set_follow_location(true);
	scli.set_connection_timeout(5);

	httplib::Client cli(domain);
	bool isHttpsConnect = false;

	// 尝试 Https 连接
	res = scli.Head(path.c_str());
	if (res && res->status == 200)
	{
		isHttpsConnect = true;

		auto it = res->headers.find("Content-Length");
		if (it == res->headers.end()) fileSize = 0;
		else fileSize = stoll(it->second);
	}
	else
	{
		// 失败后尝试 Http 连接
		cli.set_follow_location(true);
		cli.set_connection_timeout(5);

		res = cli.Head(path.c_str());
		if (res && res->status == 200)
		{
			auto it = res->headers.find("Content-Length");
			if (it == res->headers.end()) fileSize = 0;
			else fileSize = stoll(it->second);
		}
		else return false;
	}

	std::ofstream file(directory + fileName, std::ios::binary | std::ios::out);
	if (!file) return false;

	if (fileSize > 0)
	{
		file.seekp(fileSize - 1);
		file.write("", 1);
		file.seekp(0);
	}

	auto callback = [&](const char* data, size_t data_length)
		{
			file.write(data, data_length);
			downloadedSize += data_length;

			return true;
		};

	if (isHttpsConnect) res = scli.Get(path.c_str(), callback);
	else res = cli.Get(path.c_str(), callback);

	file.close();

	if (res && res->status == 200) return true;
	return false;
}