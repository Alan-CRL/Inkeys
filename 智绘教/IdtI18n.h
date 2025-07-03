#pragma once

#include "IdtMain.h"

#define IA(x) I18n::getA(x)
#define IW(x) I18n::getW(x)

class I18n
{
public:
	static inline wstring i18nIdentifying;
	static inline unordered_map<string, string> i18n;

	static bool load(int type, wstring path, wstring lang = L"en-US");
	static string getA(string x);
	static wstring getW(string x);

private:
	static inline mutex i18nWriteMutex;
	static void flattenJson(const Json::Value& node, const string& prefix, unordered_map<string, string>& outMap);

	I18n() = delete;
};

namespace IdtTest
{
	void printI18nMap();
}