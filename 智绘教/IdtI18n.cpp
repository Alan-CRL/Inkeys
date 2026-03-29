import Inkeys.Other.Json;
import Inkeys.Conv.Text;

#include "IdtI18n.h"
#include <filesystem>

namespace
{
	constexpr const wchar_t* DefaultI18nLanguage = L"zh-CN";

	void stripUtf8Bom(string& jsonContent)
	{
		if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	}

	bool loadJsonContentFromResource(const wstring& path, const wstring& lang, string& jsonContent)
	{
		int resNum = 245;
		if (lang == L"en-US") resNum = 246;
		else if (lang == L"zh-TW") resNum = 275;

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
			if (!value.empty()) nextI18n[key] = move(value);
		}
	}

	unique_lock<mutex> lock(i18nWriteMutex);
	i18n = move(nextI18n);
	identifying = lang;
	return true;
}
string I18n::getA(string x)
{
	auto it = i18n.find(x);
	if (it == i18n.end()) return {};
	return it->second;
}
wstring I18n::getW(string x)
{
	auto it = i18n.find(x);
	if (it == i18n.end()) return {};
	return utf8ToUtf16(it->second);
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
	cout << "-------- I18n所有Key-Value --------" << endl;

	for (const auto& [key, val] : I18n::i18n)
	{
		cout << key << ": " << val << endl;
	}

	cout << "-------- End --------" << endl;
}
