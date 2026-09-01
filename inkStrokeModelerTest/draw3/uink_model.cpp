module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "normaliz.lib")

module draw3.uink_model;

namespace draw3::uink
{
	namespace
	{
		int HexValue(char value) noexcept
		{
			if (value >= '0' && value <= '9') return value - '0';
			if (value >= 'a' && value <= 'f') return value - 'a' + 10;
			if (value >= 'A' && value <= 'F') return value - 'A' + 10;
			return -1;
		}

		bool IsControl(unsigned char value) noexcept
		{
			return value < 0x20 || value == 0x7f;
		}
	}

	UInkGuid::UInkGuid(std::array<uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

	const std::array<uint8_t, 16>& UInkGuid::Bytes() const noexcept
	{
		return bytes_;
	}

	bool UInkGuid::IsZero() const noexcept
	{
		return std::all_of(bytes_.begin(), bytes_.end(), [](uint8_t value)
		{
			return value == 0;
		});
	}

	std::optional<UInkGuid> ParseUInkGuid(const std::string& text) noexcept
	{
		if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
			text[18] != '-' || text[23] != '-') return std::nullopt;

		std::array<uint8_t, 16> bytes = {};
		size_t source = 0;
		for (size_t target = 0; target < bytes.size(); ++target)
		{
			if (source == 8 || source == 13 || source == 18 || source == 23) ++source;
			const int high = HexValue(text[source]);
			const int low = HexValue(text[source + 1]);
			if (high < 0 || low < 0) return std::nullopt;
			bytes[target] = static_cast<uint8_t>((high << 4) | low);
			source += 2;
		}
		return UInkGuid(bytes);
	}

	std::string FormatUInkGuid(const UInkGuid& guid)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string result(36, '-');
		size_t target = 0;
		for (size_t source = 0; source < guid.Bytes().size(); ++source)
		{
			if (target == 8 || target == 13 || target == 18 || target == 23) ++target;
			result[target++] = kHex[guid.Bytes()[source] >> 4];
			result[target++] = kHex[guid.Bytes()[source] & 0x0f];
		}
		return result;
	}

	std::optional<UInkGuid> CreateUInkGuid() noexcept
	{
		std::array<uint8_t, 16> bytes = {};
		if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return std::nullopt;
		// 固定 RFC 4122 version/variant 位，避免生成不可互操作的随机标识。
		bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
		bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);
		return UInkGuid(bytes);
	}

	bool IsSafeMediaPath(const std::string& path, uint64_t maxBytes) noexcept
	{
		if (path.empty() || static_cast<uint64_t>(path.size()) > maxBytes ||
			path.size() > static_cast<size_t>(std::numeric_limits<int>::max()) || path.front() == '/' ||
			path.find('\\') != std::string::npos || path.find(':') != std::string::npos)
			return false;

		size_t segmentStart = 0;
		for (size_t index = 0; index <= path.size(); ++index)
		{
			if (index < path.size() && IsControl(static_cast<unsigned char>(path[index])))
				return false;
			if (index != path.size() && path[index] != '/') continue;
			const size_t length = index - segmentStart;
			if (length == 0 || (length == 1 && path[segmentStart] == '.') ||
				(length == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.'))
				return false;
			segmentStart = index + 1;
		}

		try
		{
			const int sourceLength = static_cast<int>(path.size());
			const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				path.data(), sourceLength, nullptr, 0);
			if (wideLength <= 0) return false;
			std::wstring wide(static_cast<size_t>(wideLength), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), sourceLength,
				wide.data(), wideLength) != wideLength) return false;
			for (const wchar_t value : wide)
			{
				if (value <= 0x001f || (value >= 0x007f && value <= 0x009f)) return false;
			}

			// 资源路径必须已经是 NFC，不能在查找 ZIP entry 时静默改名。
			return IsNormalizedString(NormalizationC, wide.data(), wideLength) != FALSE;
		}
		catch (...)
		{
			return false;
		}
	}

	bool HasMedia(const UInkDocument& document) noexcept
	{
		for (const UInkCanvas& canvas : document.canvases)
		{
			for (const UInkContent& content : canvas.content)
			{
				if (std::holds_alternative<UInkMedia>(content)) return true;
			}
		}
		return false;
	}

	std::vector<bool> ComputeUInkLatestVisibility(const UInkCanvas& canvas)
	{
		std::vector<bool> visible(canvas.content.size(), true);
		for (size_t index = 0; index < canvas.content.size(); ++index)
		{
			std::visit([&](const auto& content)
			{
				using T = std::decay_t<decltype(content)>;
				if constexpr (!std::is_same_v<T, UInkMedia>)
					if (content.renderOnlyWhenLatest) visible[index] = false;
			}, canvas.content[index]);
		}

		for (size_t index = canvas.content.size(); index != 0; --index)
		{
			const size_t current = index - 1;
			bool stop = false;
			std::visit([&](const auto& content)
			{
				using T = std::decay_t<decltype(content)>;
				if constexpr (!std::is_same_v<T, UInkMedia>)
				{
					if (content.renderOnlyWhenLatest) visible[current] = true;
					else stop = true;
				}
			}, canvas.content[current]);
			if (stop) break;
		}
		return visible;
	}
}
