module;

#define CPPHTTPLIB_OPENSSL_SUPPORT
#pragma warning(push)
#pragma warning(disable: 4996)
#include "httplib.h"
#include "UpdatePathSafety.h"
#pragma warning(pop)

#include <cstdint>
#include <limits>
#include <string>
#include <windows.h>

#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

module Inkeys.Net.Update;
import :Download;

namespace
{
	constexpr std::uint64_t kMaxVersionBytes = 1024ull * 1024ull;
	constexpr std::uint64_t kMaxDownloadBytes = 512ull * 1024ull * 1024ull;
	constexpr int kMaxUpdateRedirects = 3;

	struct UpdateTarget
	{
		std::string host;
		int port = 443;
		std::string path;
		UpdateUrlScheme scheme = UpdateUrlScheme::Https;
		bool explicitPort = false;
	};

	bool ParseUpdateTarget(const std::string& url, UpdateTarget& target)
	{
		SafeUpdateUrlParts parts;
		if (!ParseSafeUpdateUrl(url, parts)) return false;
		target = { std::string(parts.host), parts.port,
			std::string(parts.path), parts.scheme, parts.explicitPort };
		return true;
	}

	bool ResolveUpdateRedirect(const UpdateTarget& current,
		const std::string& location, UpdateTarget& next)
	{
		std::string resolved;
		const std::string prefix = current.scheme == UpdateUrlScheme::Https
			? "https://" : "http://";
		const std::string authority = current.host +
			(current.explicitPort ? ":" + std::to_string(current.port) : "");
		if (!ResolveSafeUpdateRedirect(
			prefix + authority + current.path, location, resolved)) return false;
		return ParseUpdateTarget(resolved, next);
	}

	template <typename Sink>
	bool FetchUpdate(UpdateTarget target, const httplib::Headers& headers,
		std::uint64_t maxBytes, Sink&& sink)
	{
		const std::string initialHost = target.host;
		for (int redirects = 0; redirects <= kMaxUpdateRedirects; ++redirects)
		{
			httplib::Headers requestHeaders = headers;
			// 跨主机跳转不转发版本/系统信息；原主机的请求语义保持不变。
			if (target.host != initialHost) requestHeaders.erase("Referer");
			bool receiveBody = false;
			bool hasLength = false;
			std::uint64_t expectedLength = 0;
			std::uint64_t received = 0;
			const auto get = [&](auto& client)
			{
				client.set_follow_location(false);
				client.set_connection_timeout(5);
				client.set_read_timeout(10);
				return client.Get(target.path, requestHeaders,
					[&](const httplib::Response& response)
					{
						receiveBody = response.status == 200;
						const std::uint64_t limit = receiveBody ? maxBytes : 16ull * 1024ull;
						if (response.get_header_value_count("Content-Length") > 1)
							return false;
						if (receiveBody && response.has_header("Content-Length"))
						{
							hasLength = true;
							expectedLength = response.get_header_value_u64(
								"Content-Length", maxBytes + 1);
							if (expectedLength > maxBytes) return false;
						}
						if (!receiveBody && response.has_header("Content-Length") &&
							response.get_header_value_u64("Content-Length", limit + 1) > limit)
							return false;
						return true;
					},
					[&](const char* data, size_t length)
					{
						const std::uint64_t limit = receiveBody ? maxBytes : 16ull * 1024ull;
						if (length > limit - received) return false;
						received += length;
						return !receiveBody || sink(data, length);
					});
			};
			const httplib::Result result = [&]() -> httplib::Result
			{
				if (target.scheme == UpdateUrlScheme::Https)
				{
					httplib::SSLClient client(target.host, target.port);
					client.enable_server_certificate_verification(true);
					client.enable_server_hostname_verification(true);
					return get(client);
				}
				httplib::Client client(target.host, target.port);
				return get(client);
			}();
			if (!result) return false; // TLS/证书、写入和超限均失败关闭。
			if (result->status == 200)
				return received > 0 && (!hasLength || received == expectedLength);
			if (redirects == kMaxUpdateRedirects ||
				(result->status != 301 && result->status != 302 &&
					result->status != 303 && result->status != 307 &&
					result->status != 308) ||
				result->get_header_value_count("Location") != 1) return false;
			UpdateTarget next;
			if (!ResolveUpdateRedirect(target, result->get_header_value("Location"), next))
				return false;
			target = std::move(next);
		}
		return false;
	}
}

std::string GetEditionInformation(std::string referer)
{
	httplib::Headers headers =
	{
		{ "Cache-Control", "no-cache" },
		{ "Pragma", "no-cache" },
		{ "Referer", referer.c_str() }
	};

	// 每个固定版本源先验 HTTPS；失败后仅尝试同源 host 的 HTTP，再进入备用源。
	const char* urls[] = {
		"https://1709404.cdn.123clouddisk.com/1709404/Inkeys/Version/version.json",
		"https://home.alan-crl.top/Inkeys/Version/version.json"
	};
	for (const char* url : urls)
	{
		UpdateTarget target;
		if (!ParseUpdateTarget(url, target)) continue;
		std::string body;
		const auto append = [&](const char* data, size_t length)
		{
			body.append(data, length);
			return true;
		};
		bool fetched = FetchUpdate(target, headers, kMaxVersionBytes, append);
		if (!fetched)
		{
			body.clear();
			target.scheme = UpdateUrlScheme::Http;
			target.port = target.explicitPort ? target.port : 80;
			fetched = FetchUpdate(target, headers, kMaxVersionBytes, append);
		}
		if (fetched)
		{
			if (body.compare(0, 3, "\xEF\xBB\xBF") == 0) body.erase(0, 3);
			return body;
		}
	}
	return "Error";
}
bool DownloadEdition(std::string domain, std::string path, std::wstring directory, std::wstring fileName, std::atomic_ullong& downloadedSize, std::string referer)
{
	UpdateTarget target;
	const std::string sourceUrl = domain.find("://") == std::string::npos
		? "https://" + domain + path : domain + path;
	if (fileName.empty() || fileName.find_first_of(L"\\/") != std::wstring::npos ||
		!ParseUpdateTarget(sourceUrl, target)) return false;
	httplib::Headers headers =
	{
		{ "Cache-Control", "no-cache" },
		{ "Pragma", "no-cache" },
		{ "User-Agent", "Inkeys-Updater/3.0" },
		{ "Referer", referer.c_str() }
	};
	const std::wstring filePath = directory + fileName;
	const HANDLE file = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr); // 本次 staging 不覆盖未知文件。
	if (file == INVALID_HANDLE_VALUE) return false;
	const UpdateTarget source = target;
	target.scheme = UpdateUrlScheme::Https;
	target.port = source.explicitPort ? source.port : 443;
	downloadedSize.store(0);
	bool localWriteFailed = false;
	const auto writeChunk =
		[&](const char* data, size_t length)
		{
			if (length > (std::numeric_limits<DWORD>::max)())
			{
				localWriteFailed = true;
				return false;
			}
			DWORD written = 0;
			const bool succeeded = WriteFile(file, data, static_cast<DWORD>(length),
				&written, nullptr) && written == length;
			if (succeeded) downloadedSize.fetch_add(written);
			else localWriteFailed = true;
			return succeeded;
		};
	bool fetched = FetchUpdate(target, headers, kMaxDownloadBytes, writeChunk);
	if (!fetched && !localWriteFailed)
	{
		// HTTPS 已写的部分包不能与 HTTP 退路拼接；同一私有句柄回卷并截断。
		LARGE_INTEGER beginning{};
		if (SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) && SetEndOfFile(file))
		{
			downloadedSize.store(0);
			target = source;
			target.scheme = UpdateUrlScheme::Http;
			target.port = source.explicitPort ? source.port : 80;
			fetched = FetchUpdate(target, headers, kMaxDownloadBytes, writeChunk);
		}
	}
	const bool flushed = fetched && FlushFileBuffers(file);
	const bool closed = CloseHandle(file) != 0;
	if (!flushed || !closed) DeleteFileW(filePath.c_str());
	return flushed && closed;
}
