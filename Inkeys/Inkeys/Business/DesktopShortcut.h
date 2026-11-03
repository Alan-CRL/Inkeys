#pragma once

#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace Inkeys::Business::DesktopShortcut
{
	inline constexpr int Default = 0, Brand = 1, Annotation = 2, AnnotationTool = 3, Custom = 4;
	struct NameOptions
	{
		bool simplifiedChinese = false;
		std::array<std::wstring, 4> presets{ L"Inkeys", L"智绘教Inkeys", L"Inkeys(Annotation)", L"Inkeys(AnnotationTool)" };
	};

	inline bool IsValidName(std::wstring_view name, std::size_t directoryLength = 0) noexcept
	{
		// ShellLink 沿用 MAX_PATH；同时为自动添加的 .lnk 保留文件名和路径空间。
		if (name.empty() || name.size() > 251 || name.back() == L' ' || name.back() == L'.'
			|| (directoryLength && directoryLength + 1 + name.size() + 4 >= 260)) return false;
		bool nonWhitespace = false;
		for (wchar_t value : name)
		{
			if (value < 32 || std::wstring_view(L"<>:\"/\\|?*").find(value) != std::wstring_view::npos) return false;
			if (!std::iswspace(value) && value != L'\u3000') nonWhitespace = true;
		}
		if (!nonWhitespace) return false;
		auto stem = name.substr(0, name.find(L'.'));
		while (!stem.empty() && stem.back() == L' ') stem.remove_suffix(1);
		const auto equals = [&](std::wstring_view reserved)
		{
			if (stem.size() != reserved.size()) return false;
			for (std::size_t i = 0; i < stem.size(); ++i)
				if (std::towupper(stem[i]) != reserved[i]) return false;
			return true;
		};
		if (equals(L"CON") || equals(L"PRN") || equals(L"AUX") || equals(L"NUL")) return false;
		// 设备保留名即使带扩展名也无效，Windows 还将三个上标数字识别为端口编号。
		if (stem.size() == 4)
		{
			const wchar_t digit = stem[3];
			const auto prefix = stem.substr(0, 3);
			if ((digit >= L'1' && digit <= L'9') || digit == L'¹' || digit == L'²' || digit == L'³')
			{
				if ((std::towupper(prefix[0]) == L'C' && std::towupper(prefix[1]) == L'O' && std::towupper(prefix[2]) == L'M')
					|| (std::towupper(prefix[0]) == L'L' && std::towupper(prefix[1]) == L'P' && std::towupper(prefix[2]) == L'T')) return false;
			}
		}
		return true;
	}

	inline int NormalizePreset(int preset, std::wstring_view customName, bool simplifiedChinese = true, std::size_t directoryLength = 0) noexcept
	{
		if (preset < Default || preset > Custom || (preset == Brand && !simplifiedChinese)
			|| (preset == Custom && !IsValidName(customName, directoryLength))) return Default;
		return preset;
	}

	inline std::wstring ResolveName(int preset, std::wstring_view customName, const NameOptions& options)
	{
		preset = NormalizePreset(preset, customName, options.simplifiedChinese);
		return preset == Custom ? std::wstring(customName) : options.presets[preset];
	}

	inline int PresetForIndex(int index, bool simplifiedChinese) noexcept
	{
		constexpr std::array<int, 5> chinese{ Brand, Default, Annotation, AnnotationTool, Custom };
		constexpr std::array<int, 4> other{ Default, Annotation, AnnotationTool, Custom };
		if (index < 0 || index >= (simplifiedChinese ? 5 : 4)) return Default;
		return simplifiedChinese ? chinese[index] : other[index];
	}
	inline int IndexForPreset(int preset, bool simplifiedChinese) noexcept
	{
		for (int index = 0; index < (simplifiedChinese ? 5 : 4); ++index)
			if (PresetForIndex(index, simplifiedChinese) == preset) return index;
		return simplifiedChinese ? 1 : 0;
	}
}
