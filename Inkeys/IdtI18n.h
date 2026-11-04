#pragma once

#include "IdtMain.h"
#include <shared_mutex>
#include <atomic>
#include "IdtI18nLocales.h"

// i18n 维护说明：
// 1. 新增或调整翻译 key 时，请先修改 `Inkeys/src/i18n/zh-CN.jsonc`；
//    `zh-CN` 是 schema 来源，也是运行时默认兜底语言。
// 2. 修改完成后运行 `pwsh ./Scripts/i18n.ps1 sync`，
//    让所有非基准语言 JSONC 同步结构、补上 `[[Need translation: ...]]` 标记，并更新 `Scripts/i18n.zh-CN.snapshot.jsonc` 与 `Inkeys/IdtI18nKeys.g.h`。
// 3. 非默认语言里未完成的项使用 "" 或 `[[Need translation: ...]]` 标记，运行时会自动回退到 `zh-CN`。
// 4. 提交前运行 `pwsh ./Scripts/i18n.ps1 check`，
//    校验未完成翻译进度、key 集、占位符、换行数量和顺序是否一致。
// 5. C++ 新代码请使用 `IA(I18nKey.A.B.C)` / `IW(I18nKey.A.B.C)`，
//    不要继续手写字符串 key。
// 6. 运行时若可能发生语言热切换，请使用 `I18n::isIdentifying(...)`，
//    不要直接读取 `I18n::identifying`。

#define IA(x) I18n::getA(x)
#define IW(x) I18n::getW(x)

namespace IdtTest
{
	void PrintI18nMap();
#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	int RunI18nLocaleTests();
#endif
}

class I18n
{
public:
	static inline wstring identifying;
	static inline unordered_map<string, string> i18n;

	struct PreparedLanguage
	{
		wstring language;
		unordered_map<string, string> values;
		std::uint64_t baseRevision = 0;
	};
	// 准备阶段不发布，字体准备成功后再以版本校验提交，失败保留当前语言。
	static bool prepare(int type, wstring path, wstring lang, PreparedLanguage& output);
	static bool commit(PreparedLanguage&& prepared) noexcept;
	static std::uint64_t revision() noexcept { return languageRevision.load(std::memory_order_acquire); }
	static bool load(int type, wstring path, wstring lang = L"en-US");
	struct TextSnapshot
	{
		LANGID language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
		vector<wstring> text;
	};
	// 单次读锁固定多个文案与语言；崩溃路径可选择非阻塞读取。
	static bool capture(std::initializer_list<const char*> keys, TextSnapshot& output, bool nonblocking = false) noexcept;
	static string getA(string x);
	static wstring getW(string x);
	static wstring getWOr(const char* key, const wchar_t* fallback);
	static bool tryGetW(const char* key, wstring& output) noexcept;
	static LANGID languageId() noexcept;
	static const Inkeys::Localization::Locale& locale() noexcept;
	static bool tryLanguageId(LANGID& output) noexcept;
	static bool isIdentifying(const wchar_t* lang);

private:
	friend void IdtTest::PrintI18nMap();
	static inline shared_mutex i18nMutex;
	static inline std::atomic<std::uint64_t> languageRevision{0};
	static void flattenJson(const Json::Value& node, const string& prefix, unordered_map<string, string>& outMap);

	I18n() = delete;
};
