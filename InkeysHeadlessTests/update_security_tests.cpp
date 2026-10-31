#include "../Inkeys/Inkeys/Net/UpdatePathSafety.h"

#include <iostream>
#include <string>
#include <vector>

int RunUpdateSecurityTests()
{
	int failures = 0;
	const auto check = [&](bool condition, const char* name)
		{
			if (condition) return;
			std::cerr << "[UpdateSecurity] failed: " << name << '\n';
			++failures;
		};

	check(IsSafeUpdateExecutableName(L"new_procedure_20260927.exe"), "normal EXE name");
	check(!IsSafeUpdateExecutableName(L"../outside.exe"), "parent traversal");
	check(!IsSafeUpdateExecutableName(L"..\\outside.exe"), "backslash traversal");
	check(!IsSafeUpdateExecutableName(L"C:outside.exe"), "drive/ADS separator");
	check(!IsSafeUpdateExecutableName(L"CON.exe"), "Win32 device name");
	check(!IsSafeUpdateExecutableName(L"outside.exe."), "trailing dot");
	check(IsSafeStagedUpdatePath(L"installer\\new_procedure_20260927.exe"),
		"staged EXE path");
	check(!IsSafeStagedUpdatePath(L"installer\\..\\outside.exe"),
		"staged path traversal");
	check(!IsSafeStagedUpdatePath(L"other\\new_procedure_20260927.exe"),
		"outside installer");
	check(IsSafeUpdateHash("0123456789abcdef0123456789ABCDEF", 32), "MD5 syntax");
	check(!IsSafeUpdateHash("0123456789abcdef0123456789ABCDEG", 32),
		"invalid digest character");
	check(!IsSafeUpdateHash("abcd", 64), "short SHA-256");

	SafeUpdateUrlParts parsed{};
	check(ParseSafeUpdateUrl(
		"https://updates.example.com/releases/package.zip", parsed) &&
		parsed.host == "updates.example.com" && parsed.path == "/releases/package.zip" &&
		parsed.port == 443 && parsed.scheme == UpdateUrlScheme::Https,
		"HTTPS package URL");
	check(ParseSafeUpdateUrl("http://updates.example.com/package.zip", parsed) &&
		parsed.host == "updates.example.com" && parsed.path == "/package.zip" &&
		parsed.port == 80 && parsed.scheme == UpdateUrlScheme::Http,
		"HTTP package URL remains supported");
	check(ParseSafeUpdateUrl("http://updates.example.com:80/package.zip", parsed) &&
		parsed.port == 80, "explicit HTTP default port remains supported");
	check(ParseSafeUpdateUrl("http://updates.example.com:8080/package.zip", parsed) &&
		parsed.port == 8080, "legacy HTTP custom port remains supported");
	check(ParseSafeUpdateUrl("https://updates.example.com:8443/package.zip", parsed) &&
		parsed.port == 8443, "explicit HTTPS custom port remains supported");
	check(ParseSafeUpdateUrl("https://updates.example.com:80/package.zip", parsed) &&
		parsed.port == 80, "explicit HTTPS scheme controls transport even on port 80");
	check(!ParseSafeUpdateUrl("https://user@updates.example.com/package.zip", parsed),
		"reject URL userinfo");
	check(!ParseSafeUpdateUrl("http://updates.example.com:0/package.zip", parsed) &&
		!ParseSafeUpdateUrl("http://updates.example.com:65536/package.zip", parsed) &&
		!ParseSafeUpdateUrl("http://updates.example.com:abc/package.zip", parsed),
		"reject zero, overflow and nondigit ports");
	check(!ParseSafeUpdateUrl("https://updates.example.com\\outside.zip", parsed),
		"reject backslash URL");

	std::string redirected;
	check(ResolveSafeUpdateRedirect(
		"https://updates.example.com/v/version.json", "/v/new.json", redirected) &&
		redirected == "https://updates.example.com/v/new.json",
		"same-origin HTTPS redirect");
	check(ResolveSafeUpdateRedirect(
		"https://updates.example.com/v/version.json", "package.zip", redirected) &&
		redirected == "https://updates.example.com/v/package.zip",
		"relative HTTPS redirect");
	check(ResolveSafeUpdateRedirect(
		"http://updates.example.com/v/version.json", "/v/new.json", redirected) &&
		redirected == "http://updates.example.com/v/new.json",
		"relative HTTP redirect preserves the explicit transport");
	check(ResolveSafeUpdateRedirect(
		"http://updates.example.com:8080/v/version.json", "/v/new.json", redirected) &&
		redirected == "http://updates.example.com:8080/v/new.json",
		"relative HTTP redirect retains the explicit custom port");
	check(ResolveSafeUpdateRedirect(
		"http://updates.example.com/v/version.json",
		"https://updates.example.com/v/new.json", redirected) &&
		redirected == "https://updates.example.com/v/new.json",
		"HTTP source may upgrade to verified HTTPS on redirect");
	check(!ResolveSafeUpdateRedirect(
		"https://updates.example.com/v/version.json",
		"http://attacker.example.com/package.zip", redirected),
		"HTTPS redirect does not silently downgrade to HTTP");
	check(!ParseSafeUpdateUrl("http://user@updates.example.com/package.zip", parsed),
		"reject HTTP URL userinfo");
	check(ParseSafeUpdateUrl("http://updates.example.com:443/package.zip", parsed) &&
		parsed.scheme == UpdateUrlScheme::Http && parsed.port == 443,
		"explicit HTTP scheme controls transport even on port 443");

	std::vector<std::wstring> examined;
	const auto available = [&](std::wstring_view name)
	{
		examined.emplace_back(name);
		return name == L"old-inkeys.exe" || name == L"智绘教.exe";
	};
	check(SelectUpdateRollbackExecutableName(L"old-inkeys.exe", available) ==
		L"old-inkeys.exe" && examined == std::vector<std::wstring>{ L"old-inkeys.exe" },
		"existing safe old_name precedes all fallbacks");
	examined.clear();
	check(SelectUpdateRollbackExecutableName(L"", available) == L"智绘教.exe" &&
		examined == std::vector<std::wstring>{ L"智绘教.exe" },
		"missing old_name prefers legacy Chinese executable");
	examined.clear();
	const auto onlyInkeys = [&](std::wstring_view name)
	{
		examined.emplace_back(name);
		return name == L"Inkeys.exe";
	};
	check(SelectUpdateRollbackExecutableName(L"..\\outside.exe", onlyInkeys) ==
		L"Inkeys.exe" && examined == std::vector<std::wstring>{
			L"智绘教.exe", L"Inkeys.exe" },
		"unsafe old_name is never probed; Inkeys is the last fallback");
	examined.clear();
	check(SelectUpdateRollbackExecutableName(L"old-inkeys.exe", onlyInkeys) ==
		L"Inkeys.exe" && examined == std::vector<std::wstring>{
			L"old-inkeys.exe", L"智绘教.exe", L"Inkeys.exe" },
		"unusable old_name falls through only after caller rejects that file");
	check(SelectUpdateRollbackExecutableName(L"", [](std::wstring_view)
		{ return false; }).empty(), "no executable candidate remains an explicit failure");
	std::vector<std::wstring> attempted;
	const auto nextCandidate = [&]()
	{
		return SelectUpdateRollbackExecutableName(L"智绘教.exe",
			[&](std::wstring_view name)
			{
				for (const auto& prior : attempted)
					if (name == std::wstring_view(prior)) return false;
				return name == L"智绘教.exe" || name == L"Inkeys.exe";
			});
	};
	const std::wstring firstCandidate = nextCandidate();
	attempted.push_back(firstCandidate);
	const std::wstring secondCandidate = nextCandidate();
	attempted.push_back(secondCandidate);
	check(firstCandidate == L"智绘教.exe" && secondCandidate == L"Inkeys.exe" &&
		nextCandidate().empty(),
		"caller exclusion advances from a failed legacy launch without retrying it");
	return failures;
}
