import Inkeys.Other.Json;
import Inkeys.Conv.Text;

#include "IdtI18n.h"
#include <filesystem>
#include <format>
#include <iostream>
#include "IdtI18nKeys.g.h"

namespace
{
	constexpr const wchar_t* DefaultI18nLanguage = L"zh-CN";

    bool isPendingTranslationValue(const string& value)
    {
        return value.empty() || value.find("[[Need translation: ") == 0;
    }

	void stripUtf8Bom(string& jsonContent)
	{
		if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	}

	bool loadJsonContentFromResource(const wstring& path, const wstring& lang, string& jsonContent)
	{
		const auto* locale = Inkeys::Localization::FindLocale(lang);
		if (!locale) return false;
		const UINT resNum = locale->resource;

		HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCE(resNum), path.c_str());
		if (!hRes) return false;

		HGLOBAL hMem = LoadResource(NULL, hRes);
		if (!hMem) return false;

		DWORD dwSize = SizeofResource(NULL, hRes);
		void* pRes = LockResource(hMem);
		if (!pRes && dwSize != 0) return false;

		jsonContent.assign(static_cast<const char*>(pRes), dwSize);
		stripUtf8Bom(jsonContent);
		return true;
	}

	bool loadJsonContentFromFile(const wstring& path, const wstring& lang, string& jsonContent)
	{
		filesystem::path jsonPath(path);
		if (!jsonPath.has_extension()) jsonPath /= wstring(lang) + L".jsonc";
		else jsonPath = jsonPath.parent_path() / (wstring(lang) + jsonPath.extension().wstring());

		ifstream ifs(jsonPath, ios::binary);
		if (!ifs) return false;

		jsonContent = string((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
		ifs.close();

		stripUtf8Bom(jsonContent);
		return true;
	}

	bool loadJsonContent(int type, const wstring& path, const wstring& lang, string& jsonContent)
	{
		if (type == 1) return loadJsonContentFromResource(path, lang, jsonContent);
		if (type == 2) return loadJsonContentFromFile(path, lang, jsonContent);
		return false;
	}

	LANGID toLanguageId(const wstring& language) noexcept
	{
		const auto* locale = Inkeys::Localization::FindLocale(language);
		return locale ? locale->language : Inkeys::Localization::Locales[0].language;
	}
}

bool I18n::load(int type, wstring path, wstring lang)
{
	auto loadFlatMap = [&](const wstring& langToLoad, unordered_map<string, string>& outMap) -> bool
		{
			string jsonContent;
			if (!loadJsonContent(type, path, langToLoad, jsonContent)) return false;

			istringstream jsonContentStream(Inkeys::Json::removeJsoncComments(jsonContent));
			Json::CharReaderBuilder readerBuilder;
			Json::Value i18nVal;
			string jsonErr;

			if (!Json::parseFromStream(readerBuilder, jsonContentStream, &i18nVal, &jsonErr)) return false;

			flattenJson(i18nVal, "", outMap);
			return true;
		};

	unordered_map<string, string> nextI18n;
	if (!loadFlatMap(DefaultI18nLanguage, nextI18n)) return false;

	if (lang != DefaultI18nLanguage)
	{
		unordered_map<string, string> overlayI18n;
		if (!loadFlatMap(lang, overlayI18n)) return false;

		for (auto& [key, value] : overlayI18n)
		{
            if (!isPendingTranslationValue(value)) nextI18n[key] = move(value);
		}
	}

	unique_lock<shared_mutex> lock(i18nMutex);
	i18n = move(nextI18n);
	identifying = lang;
	return true;
}
string I18n::getA(string x)
{
	shared_lock<shared_mutex> lock(i18nMutex);
	auto it = i18n.find(x);
	if (it == i18n.end()) return {};
	return it->second;
}
wstring I18n::getW(string x)
{
	string value;
	{
		shared_lock<shared_mutex> lock(i18nMutex);
		auto it = i18n.find(x);
		if (it == i18n.end()) return {};
		value = it->second;
	}
	return utf8ToUtf16(value);
}

wstring I18n::getWOr(const char* key, const wchar_t* fallback)
{
	wstring value = key ? getW(key) : wstring{};
	return value.empty() ? wstring(fallback ? fallback : L"") : value;
}

bool I18n::tryGetW(const char* key, wstring& output) noexcept
{
	if (!key) return false;
	try
	{
		string value;
		{
			shared_lock<shared_mutex> lock(i18nMutex, try_to_lock);
			if (!lock.owns_lock()) return false;
			auto it = i18n.find(key);
			if (it == i18n.end() || isPendingTranslationValue(it->second))
				return false;
			value = it->second;
		}
		wstring converted = utf8ToUtf16(value);
		if (converted.empty()) return false;
		output = move(converted);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

LANGID I18n::languageId() noexcept
{
	try
	{
		shared_lock<shared_mutex> lock(i18nMutex);
		return toLanguageId(identifying);
	}
	catch (...)
	{
		return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	}
}

const Inkeys::Localization::Locale& I18n::locale() noexcept
{
	try
	{
		shared_lock<shared_mutex> lock(i18nMutex);
		if (const auto* locale = Inkeys::Localization::FindLocale(identifying)) return *locale;
	}
	catch (...) {}
	return Inkeys::Localization::Locales[0];
}

bool I18n::tryLanguageId(LANGID& output) noexcept
{
	try
	{
		shared_lock<shared_mutex> lock(i18nMutex, try_to_lock);
		if (!lock.owns_lock()) return false;
		output = toLanguageId(identifying);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool I18n::isIdentifying(const wchar_t* lang)
{
	shared_lock<shared_mutex> lock(i18nMutex);
	return identifying == lang;
}

void I18n::flattenJson(const Json::Value& node, const string& prefix, unordered_map<string, string>& outMap)
{
	if (node.isString()) outMap[prefix] = node.asString();
	else if (node.isObject())
	{
		for (const auto& key : node.getMemberNames())
		{
			string fullKey = prefix.empty() ? key : (prefix + "/" + key);
			flattenJson(node[key], fullKey, outMap);
		}
	}
}
void IdtTest::PrintI18nMap()
{
	shared_lock<shared_mutex> lock(I18n::i18nMutex);
	cout << "-------- I18n所有Key-Value --------" << endl;

	for (const auto& [key, val] : I18n::i18n)
	{
		cout << key << ": " << val << endl;
	}

	cout << "-------- End --------" << endl;
}

#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
int IdtTest::RunI18nLocaleTests()
{
	// 无窗口入口直接验证内嵌资源和真实格式化，不能经过生产的异常兜底。
	int failures = 0;
	auto expect = [&](bool ok, const char* why)
		{
			if (!ok) { ++failures; std::cerr << "[I18n] FAIL " << why << '\n'; }
		};
	using namespace Inkeys::Localization;
	expect(std::wstring_view(LocaleForIndex(0).code) == L"en-US"
		&& std::wstring_view(LocaleForIndex(1).code) == L"zh-CN"
		&& std::wstring_view(LocaleForIndex(2).code) == L"zh-TW", "saved legacy language indices keep their meaning");
	expect(!IsValidIndex(-1) && !IsValidIndex(11) && &LocaleForIndex(99) == &Locales[0], "invalid saved index uses English without rewriting configuration");
	expect(IndexForSystemLanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE)) == 1
		&& IndexForSystemLanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU)) == 2
		&& IndexForSystemLanguage(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH_CANADIAN)) == 0,
		"uncovered regions keep the previous startup fallback");
	for (std::size_t index = 0; index < Locales.size(); ++index)
	{
		const auto& locale = Locales[index];
		expect(IndexForSystemLanguage(locale.language) == static_cast<int>(index), "first-install system language maps to its exact supported region");
		const auto label = NativeLabel(locale);
		expect(label.find(locale.codeUtf8) != std::string::npos
			&& (label.find("Beta") != std::string::npos) == (index != 1), "native language label includes code and the required Beta status");
		if (!I18n::load(1, L"JSON", locale.code))
		{
			expect(false, locale.codeUtf8); continue;
		}
		expect(I18n::languageId() == locale.language && I18n::isIdentifying(locale.code), "actual resource load publishes the matching LANGID and language");
		std::string json;
		expect(loadJsonContentFromResource(L"JSON", locale.code, json), "embedded language resource is present");
		Json::CharReaderBuilder reader;
		Json::Value root;
		std::string errors;
		std::istringstream stream(Inkeys::Json::removeJsoncComments(json));
		expect(Json::parseFromStream(reader, stream, &root, &errors), "embedded JSONC parses");
		expect(root["Language"].isString() && root["Language"].asString() == locale.codeUtf8
			&& IA(I18nKey.Language) == locale.codeUtf8,
			"resource contents and published translation identify the actual requested locale");
		auto complete = [&](auto&& self, const Json::Value& node) -> bool
			{
				if (node.isString()) return node.asString().find("[[Need translation: ") != 0;
				for (const auto& member : node.getMemberNames()) if (!self(self, node[member])) return false;
				return node.isObject();
			};
		expect(complete(complete, root), "embedded translation has no unfinished markers");
		auto formatted = [&](const char* key, const char* number, auto&... arguments)
			{
				try
				{
					const auto text = I18n::getA(key);
					expect(!text.empty(), key);
					const auto result = std::vformat(text, std::make_format_args(arguments...));
					expect(result.find(number) != std::string::npos, key);
				}
				catch (const std::format_error&) { expect(false, key); }
			};
		int thickness = 17;
		std::string number = "17";
		double actual = 12.5, unlimited = 63.25, width = 2.5, height = 3.75, pxWidth = 125, pxHeight = 225;
		float scale = 1.25F;
		IdtAtomic<int> downloadSourceLine{ 42 };
		formatted(I18nKey.UI.Bar.DrawAttributes.ThicknessFormat, "17", thickness);
		formatted(I18nKey.UI.Bar.DrawAttributes.ThicknessFormat, "17", number);
		formatted(I18nKey.UI.Bar.Diagnostics.FrameRateFormat, "12.50", actual, unlimited);
		formatted(I18nKey.SettingsUI.Regular.Appearance.BarZoom.Ind, "1.25", scale);
		formatted(I18nKey.SettingsUI.Draw.Eraser.PalmEraser.ThresholdSizePhysical, "2.50", width, height, pxWidth, pxHeight);
		formatted(I18nKey.SettingsUI.Draw.Eraser.PalmEraser.ThresholdSizePixels, "125", pxWidth, pxHeight);
		formatted(I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind, "1.25", scale);
		formatted(I18nKey.SettingsUI.Update.Downloading, "42", downloadSourceLine);
		try
		{
			const std::wstring wideNumber = L"17";
			expect(std::vformat(IW(I18nKey.UI.Bar.DrawAttributes.ThicknessFormat), std::make_wformat_args(wideNumber)).find(wideNumber) != std::wstring::npos,
				"actual wide string width formatting preserves its argument");
			const auto frameRate = std::vformat(IW(I18nKey.UI.Bar.Diagnostics.FrameRateFormat), std::make_wformat_args(actual, unlimited));
			expect(frameRate.find(L"12.50") != std::wstring::npos && frameRate.find(L"63.25") != std::wstring::npos,
				"actual wide frame-rate formatting preserves both typed numeric arguments");
		}
		catch (const std::format_error&) { expect(false, "wide UI format syntax and argument types"); }
		const auto recall = IW(I18nKey.UI.Operate.Recall);
		expect(recall.find(L'%') == std::wstring::npos && recall.size() + 18 < 100,
			"legacy swprintf label cannot introduce conversions or overflow its fixed timestamp buffer");
		std::cout << "[I18n] checked " << locale.codeUtf8 << '\n';
	}
	expect(!I18n::load(1, L"JSON", L"unsupported-test-language"), "unknown embedded locale fails instead of silently labeling Chinese as another language");
	std::cout << "[I18n] failures=" << failures << '\n';
	return failures ? 1 : 0;
}
#endif
