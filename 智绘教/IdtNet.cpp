#include "IdtNet.h"

#pragma comment(lib, "libssl_static.lib")
#pragma comment(lib, "libcrypto_static.lib")

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "cpphttplib/httplib.h"

extern std::wstring editionDate;
extern std::wstring editionChannel;
extern std::wstring windowsEdition;
std::string utf16ToUtf8(const std::wstring& input);

std::string GetEditionInformation()
{
	httplib::Result res;
	httplib::Headers headers =
	{
		{ "Cache-Control", "no-cache" },
		{ "Pragma", "no-cache" },
		{ "Referer", utf16ToUtf8(editionDate + L"," + editionChannel + L"," + windowsEdition).c_str() }
	};

	// 尝试主地址
	{
		httplib::SSLClient scli("vip.123pan.cn");
		scli.set_follow_location(true);
		scli.set_connection_timeout(5);
		scli.set_read_timeout(10);

		// 尝试 Https 连接
		res = scli.Get("/1709404/Inkeys/Version/version.json", headers);
		if (!res || res->status != 200)
		{
			// 失败后尝试使用 Http 连接
			httplib::Client cli("vip.123pan.cn");
			scli.set_follow_location(true);
			scli.set_connection_timeout(5);
			scli.set_read_timeout(10);

			res = cli.Get("/1709404/Inkeys/Version/version.json", headers);
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
		res = scli.Get("/Inkeys/Version/version.json", headers);
		if (!res || res->status != 200)
		{
			// 失败后尝试使用 Http 连接
			httplib::Client cli("home.alan-crl.top");
			scli.set_follow_location(true);
			scli.set_connection_timeout(5);
			scli.set_read_timeout(10);

			res = cli.Get("/Inkeys/Version/version.json", headers);
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
bool DownloadEdition(std::string domain, std::string path, std::wstring directory, std::wstring fileName, std::atomic_ullong& downloadedSize)
{
	httplib::Result res;
	httplib::Headers headers =
	{
		{ "Cache-Control", "no-cache" },
		{ "Pragma", "no-cache" },
		{ "Referer", utf16ToUtf8(editionDate + L"," + editionChannel + L"," + windowsEdition).c_str() }
	};

	std::ofstream file;
	auto callback = [&](const char* data, size_t data_length)
		{
			file.write(data, data_length);
			downloadedSize += data_length;
			return true;
		};

	{
		file.open(directory + fileName, std::ios::binary | std::ios::out | std::ios::trunc);
		if (file)
		{
			file.seekp(0);

			// 使用 Https
			httplib::SSLClient scli(domain);
			scli.set_follow_location(true);
			scli.set_connection_timeout(5);
			res = scli.Get(path.c_str(), callback);
		}
		file.close();
	}
	if (!(res && res->status == 200))
	{
		file.open(directory + fileName, std::ios::binary | std::ios::out | std::ios::trunc);
		if (file)
		{
			file.seekp(0);

			// 使用 Http
			httplib::Client cli(domain);
			cli.set_follow_location(true);
			cli.set_connection_timeout(5);
			res = cli.Get(path.c_str(), callback);
		}
		file.close();
	}

	if (res && res->status == 200) return true;
	return false;
}
