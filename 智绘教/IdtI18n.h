#pragma once

#include "IdtMain.h"
#include <shared_mutex>

// i18n 维护说明：
// 1. 新增或调整翻译 key 时，请先修改 `智绘教/src/i18n/zh-CN.jsonc`；
//    `zh-CN` 是 schema 来源，也是运行时默认兜底语言。
// 2. 修改完成后运行 `pwsh ./Scripts/i18n.ps1 sync`，
//    让 `en-US.jsonc` / `zh-TW.jsonc` 同步结构，并重新生成 `智绘教/IdtI18nKeys.g.h`。
// 3. 非默认语言里未完成的项保留为 ""，运行时会自动回退到 `zh-CN`。
// 4. 提交前运行 `pwsh ./Scripts/i18n.ps1 check`，
//    校验 key 集、占位符、换行数量和顺序是否一致。
// 5. C++ 新代码请使用 `IA(I18nKey.A.B.C)` / `IW(I18nKey.A.B.C)`，
//    不要继续手写字符串 key。
// 6. 运行时若可能发生语言热切换，请使用 `I18n::isIdentifying(...)`，
//    不要直接读取 `I18n::identifying`。

#define IA(x) I18n::getA(x)
#define IW(x) I18n::getW(x)

namespace IdtTest
{
	void PrintI18nMap();
}

class I18n
{
public:
	static inline wstring identifying;
	static inline unordered_map<string, string> i18n;

	static bool load(int type, wstring path, wstring lang = L"en-US");
	static string getA(string x);
	static wstring getW(string x);
	static bool isIdentifying(const wchar_t* lang);

private:
	friend void IdtTest::PrintI18nMap();
	static inline shared_mutex i18nMutex;
	static void flattenJson(const Json::Value& node, const string& prefix, unordered_map<string, string>& outMap);

	I18n() = delete;
};