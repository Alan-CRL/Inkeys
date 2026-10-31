#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

enum class UpdateUrlScheme : unsigned char
{
	Http,
	Https,
};

struct SafeUpdateUrlParts
{
	std::string_view host;
	std::string_view path;
	int port = 443;
	UpdateUrlScheme scheme = UpdateUrlScheme::Https;
	bool explicitPort = false;
};

inline bool ParseSafeUpdateUrl(std::string_view url,
	SafeUpdateUrlParts& parsed) noexcept
{
	parsed = {};
	if (url.size() < 12 || url.size() > 4096) return false;
	const auto matchesScheme = [&](std::string_view expected) noexcept
	{
		if (url.size() < expected.size()) return false;
		for (std::size_t i = 0; i < expected.size(); ++i)
		{
			const char ch = url[i] >= 'A' && url[i] <= 'Z' ?
				static_cast<char>(url[i] + ('a' - 'A')) : url[i];
			if (ch != expected[i]) return false;
		}
		return true;
	};
	const bool https = matchesScheme("https://");
	if (!https && !matchesScheme("http://")) return false;
	const std::size_t schemeSize = https ? 8 : 7;
	const int defaultPort = https ? 443 : 80;
	const std::size_t pathStart = url.find('/', schemeSize);
	if (pathStart == std::string_view::npos) return false;
	const auto authority = url.substr(schemeSize, pathStart - schemeSize);
	if (authority.empty() || authority.find('@') != std::string_view::npos ||
		authority.find_first_of("[]\\?#") != std::string_view::npos) return false;
	const std::size_t colon = authority.find(':');
	if (colon != std::string_view::npos &&
		authority.find(':', colon + 1) != std::string_view::npos) return false;
	const auto host = authority.substr(0, colon);
	if (host.size() < 4 || host.size() > 253 ||
		host.find('.') == std::string_view::npos) return false;
	std::size_t labelStart = 0;
	for (std::size_t i = 0; i <= host.size(); ++i)
	{
		if (i != host.size() && host[i] != '.')
		{
			const char ch = host[i] >= 'A' && host[i] <= 'Z' ?
				static_cast<char>(host[i] + ('a' - 'A')) : host[i];
			if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
				ch == '-')) return false;
			continue;
		}
		if (i == labelStart || i - labelStart > 63 ||
			host[labelStart] == '-' || host[i - 1] == '-') return false;
		labelStart = i + 1;
	}
	bool alphabeticTld = false;
	for (std::size_t i = host.rfind('.') + 1; i < host.size(); ++i)
		if ((host[i] >= 'a' && host[i] <= 'z') ||
			(host[i] >= 'A' && host[i] <= 'Z')) alphabeticTld = true;
	if (!alphabeticTld) return false; // 拒绝数字 IP 主机。
	int port = defaultPort;
	const bool explicitPort = colon != std::string_view::npos;
	if (explicitPort)
	{
		const auto text = authority.substr(colon + 1);
		if (text.empty() || text.size() > 5) return false;
		unsigned value = 0;
		for (const char ch : text)
		{
			if (ch < '0' || ch > '9') return false;
			value = value * 10 + static_cast<unsigned>(ch - '0');
			if (value > 65535) return false;
		}
		if (value == 0) return false;
		port = static_cast<int>(value);
	}
	const auto path = url.substr(pathStart);
	if (path.empty() || path[0] != '/') return false;
	for (const unsigned char ch : path)
		if (ch <= 32 || ch == 127 || ch == '\\' || ch == '#') return false;
	parsed = { host, path, port,
		https ? UpdateUrlScheme::Https : UpdateUrlScheme::Http, explicitPort };
	return true;
}

inline bool ResolveSafeUpdateRedirect(std::string_view currentUrl,
	std::string_view location, std::string& resolved)
{
	SafeUpdateUrlParts current;
	if (!ParseSafeUpdateUrl(currentUrl, current) ||
		location.empty() || location.size() > 4096 ||
		location.find('#') != std::string_view::npos) return false;
	const std::string_view prefix = current.scheme == UpdateUrlScheme::Https
		? "https://" : "http://";
	std::string authority(current.host);
	if (current.explicitPort) authority += ":" + std::to_string(current.port);
	std::string candidate;
	if (location.find("://") != std::string_view::npos)
		candidate.assign(location);
	else if (location.substr(0, 2) == "//")
		candidate = std::string(prefix.substr(0, prefix.size() - 2)) +
			std::string(location);
	else if (location.front() == '/')
		candidate = std::string(prefix) + authority +
			std::string(location);
	else
	{
		if (location.find(':') != std::string_view::npos) return false;
		const auto base = current.path.substr(0, current.path.find('?'));
		const std::size_t slash = base.rfind('/');
		if (slash == std::string_view::npos) return false;
		const auto directory = location.front() == '?' ? base :
			base.substr(0, slash + 1);
		candidate = std::string(prefix) + authority +
			std::string(directory) + std::string(location);
	}
	SafeUpdateUrlParts next;
	if (!ParseSafeUpdateUrl(candidate, next) ||
		(current.scheme == UpdateUrlScheme::Https &&
			next.scheme == UpdateUrlScheme::Http)) return false;
	resolved = std::move(candidate);
	return true;
}

inline bool IsSafeUpdateHash(std::string_view hash, std::size_t expectedLength) noexcept
{
	if ((expectedLength != 32 && expectedLength != 64) ||
		hash.size() != expectedLength) return false;
	for (const char ch : hash)
		if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
			(ch >= 'A' && ch <= 'F'))) return false;
	return true;
}

inline bool IsSafeUpdateExecutableName(std::wstring_view name) noexcept
{
	if (name.size() < 5 || name.size() > 128 || name.front() == L' ' ||
		name.back() == L' ' || name.back() == L'.' ||
		name.find(L"..") != std::wstring_view::npos) return false;
	const auto lowerAscii = [](wchar_t ch) noexcept
		{ return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch; };
	if (name[name.size() - 4] != L'.' ||
		lowerAscii(name[name.size() - 3]) != L'e' ||
		lowerAscii(name[name.size() - 2]) != L'x' ||
		lowerAscii(name[name.size() - 1]) != L'e') return false;
	for (const wchar_t ch : name)
	{
		if (ch < 32 || ch == 127 || ch == L'<' || ch == L'>' || ch == L':' ||
			ch == L'"' || ch == L'/' || ch == L'\\' || ch == L'|' ||
			ch == L'?' || ch == L'*' ||
			(ch >= 0x202A && ch <= 0x202E) ||
			(ch >= 0x2066 && ch <= 0x2069)) return false;
	}
	// Win32 设备名即使带 .exe 扩展名也不是普通文件。
	auto stem = name.substr(0, name.find(L'.'));
	while (!stem.empty() && stem.back() == L' ') stem.remove_suffix(1);
	if (stem.empty()) return false;
	const auto equalsAscii = [&](std::wstring_view expected) noexcept
	{
		if (stem.size() != expected.size()) return false;
		for (std::size_t i = 0; i < stem.size(); ++i)
			if (lowerAscii(stem[i]) != expected[i]) return false;
		return true;
	};
	if (equalsAscii(L"con") || equalsAscii(L"prn") ||
		equalsAscii(L"aux") || equalsAscii(L"nul") ||
		equalsAscii(L"conin$") || equalsAscii(L"conout$") ||
		equalsAscii(L"clock$")) return false;
	if (stem.size() == 4 &&
		((lowerAscii(stem[0]) == L'c' && lowerAscii(stem[1]) == L'o' &&
			lowerAscii(stem[2]) == L'm') ||
			(lowerAscii(stem[0]) == L'l' && lowerAscii(stem[1]) == L'p' &&
				lowerAscii(stem[2]) == L't')) &&
		((stem[3] >= L'0' && stem[3] <= L'9') || stem[3] == 0x00B9 ||
			stem[3] == 0x00B2 || stem[3] == 0x00B3)) return false;
	return true;
}

inline bool IsSafeStagedUpdatePath(std::wstring_view path) noexcept
{
	constexpr std::wstring_view prefix = L"installer\\";
	return path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix &&
		IsSafeUpdateExecutableName(path.substr(prefix.size()));
}

template<class IsRegularNonReparse>
inline std::wstring SelectUpdateRollbackExecutableName(
	std::wstring_view oldName, IsRegularNonReparse&& isRegularNonReparse)
{
	const auto available = [&](std::wstring_view candidate)
	{
		// 只把安全 basename 交给调用方的安装目录/非 reparse 文件校验。
		return IsSafeUpdateExecutableName(candidate) &&
			isRegularNonReparse(candidate);
	};
	if (available(oldName)) return std::wstring(oldName);
	if (available(L"智绘教.exe")) return L"智绘教.exe";
	if (available(L"Inkeys.exe")) return L"Inkeys.exe";
	return {};
}
