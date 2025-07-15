#include "IdtI18n.h"

#include "IdtText.h"
#include "Inkeys/Json/IdtJson.h"

bool I18n::load(int type, wstring path, wstring lang)
{
	string jsonContent;
	if (type == 1)
	{
		int resNum = 246;
		if (lang == L"zh-CN") resNum = 245;
		if (lang == L"en-US") resNum = 246;
		if (lang == L"zh-TW") resNum = 275;

		HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCE(resNum), path.c_str());
		HGLOBAL hMem = LoadResource(NULL, hRes);
		DWORD dwSize = SizeofResource(NULL, hRes);

		jsonContent = string(static_cast<char*>(hMem), dwSize);
		if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	}
	else if (type == 2)
	{
		ifstream ifs(path, ios::binary);
		jsonContent = string((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
		ifs.close();

		if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
	}

	istringstream jsonContentStream(IdtJson::removeJsoncComments(jsonContent));
	Json::CharReaderBuilder readerBuilder;
	Json::Value i18nVal;
	string jsonErr;

	if (Json::parseFromStream(readerBuilder, jsonContentStream, &i18nVal, &jsonErr))
	{
		// 写锁，然后加载 i18n 内容
		unique_lock<mutex> lock(i18nWriteMutex);

		//i18n.clear();
		flattenJson(i18nVal, "", i18n);

		lock.unlock();

		{
			identifying = lang;
			return true;
		}
	}
	return false;
}
string I18n::getA(string x)
{
	return i18n[x];
}
wstring I18n::getW(string x)
{
	return utf8ToUtf16(i18n[x]);
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