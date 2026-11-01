#pragma once

#include <Windows.h>
#include <array>
#include <string>
#include <string_view>
#include "resource.h"

namespace Inkeys::Localization
{
	struct Locale
	{
		const wchar_t* code;
		const char* codeUtf8;
		const char* nativeName;
		LANGID language;
		UINT resource;
		UINT regularFont;
		UINT boldFont;
		const wchar_t* fontFamily;
	};

	// 数组位置就是旧 SelectLanguage 的持久化值；既有三项不可重排。
	inline constexpr std::array<Locale, 11> Locales{ {
		{ L"en-US", "en-US", "English", 0x0409, IDR_EN_US, IDR_TTF1, IDR_TTF7, L"HarmonyOS Sans SC" },
		{ L"zh-CN", "zh-CN", "简体中文", 0x0804, IDR_ZH_CN, IDR_TTF1, IDR_TTF7, L"HarmonyOS Sans SC" },
		{ L"zh-TW", "zh-TW", "繁體中文（台灣）", 0x0404, IDR_ZH_TW, IDR_TTF3, IDR_TTF8, L"HarmonyOS Sans TC" },
		{ L"zh-HK", "zh-HK", "繁體中文（香港）", 0x0c04, IDR_ZH_HK, IDR_TTF3, IDR_TTF8, L"HarmonyOS Sans TC" },
		{ L"ja-JP", "ja-JP", "日本語", 0x0411, IDR_JA_JP, IDR_NOTO_JP_REGULAR, IDR_NOTO_JP_BOLD, L"Noto Sans JP" },
		{ L"ko-KR", "ko-KR", "한국어", 0x0412, IDR_KO_KR, IDR_NOTO_KR_REGULAR, IDR_NOTO_KR_BOLD, L"Noto Sans KR" },
		{ L"fr-FR", "fr-FR", "Français", 0x040c, IDR_FR_FR, IDR_TTF1, IDR_TTF7, L"HarmonyOS Sans SC" },
		{ L"de-DE", "de-DE", "Deutsch", 0x0407, IDR_DE_DE, IDR_TTF1, IDR_TTF7, L"HarmonyOS Sans SC" },
		{ L"es-ES", "es-ES", "Español", 0x0c0a, IDR_ES_ES, IDR_TTF1, IDR_TTF7, L"HarmonyOS Sans SC" },
		{ L"pt-BR", "pt-BR", "Português (Brasil)", 0x0416, IDR_PT_BR, IDR_TTF1, IDR_TTF7, L"HarmonyOS Sans SC" },
		{ L"ru-RU", "ru-RU", "Русский", 0x0419, IDR_RU_RU, IDR_TTF1, IDR_TTF7, L"HarmonyOS Sans SC" },
	} };

	[[nodiscard]] inline constexpr bool IsValidIndex(int index) noexcept
	{
		return index >= 0 && index < static_cast<int>(Locales.size());
	}

	[[nodiscard]] inline constexpr const Locale& LocaleForIndex(int index) noexcept
	{
		return Locales[IsValidIndex(index) ? index : 0];
	}

	[[nodiscard]] inline const Locale* FindLocale(std::wstring_view code) noexcept
	{
		for (const auto& locale : Locales)
			if (code == locale.code) return &locale;
		return nullptr;
	}

	[[nodiscard]] inline int IndexForSystemLanguage(LANGID language) noexcept
	{
		for (std::size_t index = 0; index < Locales.size(); ++index)
			if (Locales[index].language == language) return static_cast<int>(index);
		// 新语种精确匹配后，保留原有新加坡/澳门中文及英语回退规则。
		if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE)) return 1;
		if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU)) return 2;
		return 0;
	}

	[[nodiscard]] inline std::string NativeLabel(const Locale& locale)
	{
		std::string result = std::string(locale.nativeName) + " · " + locale.codeUtf8;
		if (locale.language != Locales[1].language) result += " · Beta";
		return result;
	}
}
