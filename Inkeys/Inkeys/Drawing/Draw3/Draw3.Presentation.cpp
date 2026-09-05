#include "Draw3.Presentation.h"

#include <json/json.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <memory>
#include <set>
#include <vector>
#include <utility>

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr std::size_t kMaximumDescriptorCharacters = 1024 * 1024;
		constexpr std::size_t kMaximumIdentityBytes = 32768;
		constexpr std::size_t kMaximumSlides = 10000;

		std::optional<std::string> WideToUtf8(std::wstring_view value) noexcept
		{
			if (value.empty()) return std::string{};
			if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
				return std::nullopt;
			const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
				value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
			if (required <= 0) return std::nullopt;
			std::string result(static_cast<std::size_t>(required), '\0');
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), result.data(), required, nullptr, nullptr) != required)
				return std::nullopt;
			return result;
		}

		std::optional<std::wstring> Utf8ToWide(std::string_view value) noexcept
		{
			if (value.empty()) return std::wstring{};
			if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
				return std::nullopt;
			const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				value.data(), static_cast<int>(value.size()), nullptr, 0);
			if (required <= 0) return std::nullopt;
			std::wstring result(static_cast<std::size_t>(required), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), result.data(), required) != required)
				return std::nullopt;
			return result;
		}

		bool ReadBoundedString(const Json::Value& root, const char* name,
			std::string& value) noexcept
		{
			const Json::Value& member = root[name];
			if (!member.isString()) return false;
			value = member.asString();
			return value.size() <= kMaximumIdentityBytes;
		}

		bool ReadNonNegativeUInt64(const Json::Value& root, const char* name,
			std::uint64_t& value) noexcept
		{
			const Json::Value& member = root[name];
			if (member.isUInt64())
			{
				value = member.asUInt64();
				return true;
			}
			if (member.isInt64() && member.asInt64() >= 0)
			{
				value = static_cast<std::uint64_t>(member.asInt64());
				return true;
			}
			return false;
		}

		std::optional<PresentationDescriptorStatus> ParseStatus(
			const std::string& value) noexcept
		{
			if (value == "StableSlideIds")
				return PresentationDescriptorStatus::StableSlideIds;
			if (value == "PageIndexFallback")
				return PresentationDescriptorStatus::PageIndexFallback;
			if (value == "TransientBusy")
				return PresentationDescriptorStatus::TransientBusy;
			if (value == "Unavailable")
				return PresentationDescriptorStatus::Unavailable;
			return std::nullopt;
		}

		std::optional<std::string> NormalizeAbsolutePath(
			std::string_view value) noexcept
		{
			const auto wide = Utf8ToWide(value);
			if (!wide || wide->empty()) return std::nullopt;
			const bool driveAbsolute = wide->size() >= 3 &&
				((*wide)[0] >= L'A' && (*wide)[0] <= L'Z' ||
					(*wide)[0] >= L'a' && (*wide)[0] <= L'z') &&
				(*wide)[1] == L':' && ((*wide)[2] == L'\\' || (*wide)[2] == L'/');
			const bool uncAbsolute = wide->size() >= 3 &&
				((*wide)[0] == L'\\' || (*wide)[0] == L'/') &&
				((*wide)[1] == L'\\' || (*wide)[1] == L'/');
			if (!driveAbsolute && !uncAbsolute) return std::nullopt;

			const DWORD required = GetFullPathNameW(wide->c_str(), 0, nullptr, nullptr);
			if (required == 0 || required > 32768) return std::nullopt;
			std::wstring normalized(required, L'\0');
			const DWORD written = GetFullPathNameW(wide->c_str(), required,
				normalized.data(), nullptr);
			if (written == 0 || written >= required) return std::nullopt;
			normalized.resize(written);
			std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
			std::wstring folded(normalized.size(), L'\0');
			if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
				normalized.data(), static_cast<int>(normalized.size()),
				folded.data(), static_cast<int>(folded.size()),
				nullptr, nullptr, 0) == 0) return std::nullopt;
			return WideToUtf8(folded);
		}

		std::uint64_t Fnv1a(std::string_view value, std::uint64_t seed) noexcept
		{
			std::uint64_t result = seed;
			for (unsigned char byte : value)
			{
				result ^= byte;
				result *= 1099511628211ull;
			}
			return result;
		}

		Bridge::PresentationKey KeyForIdentity(std::string_view identity) noexcept
		{
			Bridge::PresentationKey key;
			const std::uint64_t first = Fnv1a(identity, 14695981039346656037ull);
			const std::uint64_t second = Fnv1a(identity, 7809847782465536322ull);
			for (std::size_t index = 0; index < 8; ++index)
			{
				key.bytes[index] = static_cast<std::uint8_t>(first >> (index * 8));
				key.bytes[index + 8] = static_cast<std::uint8_t>(second >> (index * 8));
			}
			// 标准 UUID variant/version 位让 hostId 可被通用工具安全展示。
			key.bytes[6] = static_cast<std::uint8_t>((key.bytes[6] & 0x0f) | 0x50);
			key.bytes[8] = static_cast<std::uint8_t>((key.bytes[8] & 0x3f) | 0x80);
			return key;
		}
	}

	PresentationDescriptorParseResult ParsePresentationDescriptorJson(
		std::wstring_view json) noexcept
	{
		PresentationDescriptorParseResult result;
		try
		{
			if (json.empty() || json.size() > kMaximumDescriptorCharacters)
			{
				result.error = "payload_size";
				return result;
			}
			const auto utf8 = WideToUtf8(json);
			if (!utf8 || utf8->size() > kMaximumDescriptorCharacters)
			{
				result.error = "utf8";
				return result;
			}
			Json::CharReaderBuilder builder;
			builder["collectComments"] = false;
			builder["failIfExtra"] = true;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			Json::Value root;
			std::string errors;
			if (!reader->parse(utf8->data(), utf8->data() + utf8->size(),
				&root, &errors) || !root.isObject() || root.size() != 12)
			{
				result.error = "json";
				return result;
			}
			if (!root["schemaVersion"].isInt() || root["schemaVersion"].asInt() != 1)
			{
				result.error = "schema";
				return result;
			}

			PresentationDescriptor descriptor;
			std::string status;
			if (!ReadBoundedString(root, "status", status) ||
				!ReadBoundedString(root, "provider", descriptor.provider) ||
				!ReadBoundedString(root, "fullName", descriptor.fullName) ||
				!ReadBoundedString(root, "presentationName", descriptor.presentationName) ||
				!ReadNonNegativeUInt64(root, "bindingRevision", descriptor.bindingRevision))
			{
				result.error = "identity";
				return result;
			}
			const auto parsedStatus = ParseStatus(status);
			if (!parsedStatus)
			{
				result.error = "status";
				return result;
			}
			descriptor.status = *parsedStatus;
			if (!root["applicationProcessId"].isInt() ||
				!root["slideShowHwnd"].isInt64() ||
				!root["currentPage"].isInt() || !root["totalPage"].isInt() ||
				root["applicationProcessId"].asInt() < 0 ||
				root["slideShowHwnd"].asInt64() < 0 ||
				root["currentPage"].asInt() < 0 || root["totalPage"].asInt() < 0)
			{
				result.error = "numbers";
				return result;
			}
			descriptor.applicationProcessId = root["applicationProcessId"].asInt();
			descriptor.slideShowHwnd = root["slideShowHwnd"].asInt64();
			descriptor.currentPage = static_cast<std::uint32_t>(root["currentPage"].asInt());
			descriptor.totalPage = static_cast<std::uint32_t>(root["totalPage"].asInt());
			if (!root["currentSlideId"].isNull())
			{
				if (!root["currentSlideId"].isInt() ||
					root["currentSlideId"].asInt() <= 0)
				{
					result.error = "current_slide_id";
					return result;
				}
				descriptor.currentSlideId = root["currentSlideId"].asInt();
			}
			if (!root["slideIds"].isArray() || root["slideIds"].size() > kMaximumSlides)
			{
				result.error = "slide_ids";
				return result;
			}
			std::set<std::int32_t> uniqueIds;
			for (const Json::Value& id : root["slideIds"])
			{
				if (!id.isInt() || id.asInt() <= 0 || !uniqueIds.insert(id.asInt()).second)
				{
					result.error = "slide_ids";
					return result;
				}
				descriptor.slideIds.push_back(id.asInt());
			}

			const bool active = descriptor.status ==
				PresentationDescriptorStatus::StableSlideIds || descriptor.status ==
				PresentationDescriptorStatus::PageIndexFallback;
			if (active && (descriptor.currentPage == 0 || descriptor.totalPage == 0 ||
				descriptor.totalPage > Bridge::kMaximumPresentationPages ||
				descriptor.currentPage > descriptor.totalPage ||
				(descriptor.fullName.empty() && descriptor.presentationName.empty())))
			{
				result.error = "active_fields";
				return result;
			}
			if (descriptor.status == PresentationDescriptorStatus::StableSlideIds &&
				(!descriptor.currentSlideId ||
					descriptor.slideIds.size() != descriptor.totalPage ||
					descriptor.slideIds[descriptor.currentPage - 1] !=
						*descriptor.currentSlideId))
			{
				result.error = "stable_topology";
				return result;
			}
			if (descriptor.status == PresentationDescriptorStatus::PageIndexFallback &&
				(descriptor.currentSlideId || !descriptor.slideIds.empty()))
			{
				result.error = "fallback_topology";
				return result;
			}
			result.descriptor = std::move(descriptor);
			return result;
		}
		catch (...)
		{
			result.error = "exception";
			return result;
		}
	}

	std::optional<Bridge::PresentationTarget> ResolvePresentationTarget(
		const PresentationDescriptor& descriptor) noexcept
	{
		try
		{
			if (descriptor.status != PresentationDescriptorStatus::StableSlideIds &&
				descriptor.status != PresentationDescriptorStatus::PageIndexFallback)
				return std::nullopt;
			Bridge::PresentationTarget target;
			const auto normalizedPath = NormalizeAbsolutePath(descriptor.fullName);
			if (normalizedPath)
				target.sourceIdentity = "path:" + *normalizedPath;
			else
			{
				target.processLocalIdentity = true;
				target.sourceIdentity = "process:" +
					std::to_string(GetCurrentProcessId()) + ":" + descriptor.provider + ":" +
					std::to_string(descriptor.applicationProcessId) + ":" +
					std::to_string(descriptor.slideShowHwnd) + ":" +
					std::to_string(descriptor.bindingRevision) + ":" +
					descriptor.presentationName;
			}
			target.key = KeyForIdentity(target.sourceIdentity);
			target.bindingMode = descriptor.status ==
				PresentationDescriptorStatus::StableSlideIds
				? Bridge::SlideBindingMode::StableSlideId
				: Bridge::SlideBindingMode::PageIndexFallback;
			target.presentationName = descriptor.presentationName;
			target.provider = descriptor.provider;
			target.bindingRevision = descriptor.bindingRevision;
			target.bindingToken = descriptor.provider + ":" +
				std::to_string(descriptor.applicationProcessId) + ":" +
				std::to_string(descriptor.slideShowHwnd) + ":" +
				std::to_string(descriptor.bindingRevision);
			target.pageIndex = descriptor.currentPage - 1;
			target.totalPages = descriptor.totalPage;
			target.slideId = descriptor.currentSlideId;
			target.slideIds = descriptor.slideIds;
			return target;
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	bool CanUpgradePresentationBindingByOrdinal(
		const Bridge::PresentationTarget& previous,
		const Bridge::PresentationTarget& next,
		std::size_t documentPageCount) noexcept
	{
		const bool processLocalBindingMatches =
			previous.processLocalIdentity == next.processLocalIdentity &&
			(!previous.processLocalIdentity || (!previous.bindingToken.empty() &&
				previous.bindingToken == next.bindingToken &&
				previous.bindingRevision == next.bindingRevision));
		return previous.key == next.key &&
			previous.sourceIdentity == next.sourceIdentity &&
			processLocalBindingMatches &&
			previous.bindingMode == Bridge::SlideBindingMode::PageIndexFallback &&
			next.bindingMode == Bridge::SlideBindingMode::StableSlideId &&
			previous.totalPages == next.totalPages &&
			documentPageCount == next.totalPages &&
			next.slideIds.size() == next.totalPages && next.slideId &&
			next.pageIndex < next.slideIds.size() &&
			next.slideIds[next.pageIndex] == *next.slideId;
	}

	bool CanReusePresentationDocumentSlot(
		const Bridge::PresentationTarget& previous,
		const Bridge::PresentationTarget& next,
		std::size_t documentPageCount) noexcept
	{
		if (previous.key != next.key ||
			previous.sourceIdentity != next.sourceIdentity ||
			previous.processLocalIdentity != next.processLocalIdentity) return false;
		if (previous.bindingMode == Bridge::SlideBindingMode::StableSlideId &&
			next.bindingMode == Bridge::SlideBindingMode::StableSlideId)
		{
			if (previous.slideIds.empty() || next.slideIds.empty() ||
			previous.slideIds.size() != previous.totalPages ||
			 next.slideIds.size() != next.totalPages) return false;
			std::set<std::int32_t> previousIds;
			std::set<std::int32_t> nextIds;
			for (const auto id : previous.slideIds)
				if (id <= 0 || !previousIds.insert(id).second) return false;
			for (const auto id : next.slideIds)
				if (id <= 0 || !nextIds.insert(id).second) return false;
			(void)documentPageCount;
			return true;
		}
		if (previous.totalPages != next.totalPages ||
			documentPageCount != next.totalPages) return false;
		if (previous.processLocalIdentity &&
			(previous.bindingToken.empty() ||
				previous.bindingToken != next.bindingToken ||
				previous.bindingRevision != next.bindingRevision)) return false;
		if (previous.bindingMode == Bridge::SlideBindingMode::PageIndexFallback &&
			next.bindingMode == Bridge::SlideBindingMode::PageIndexFallback)
			return true;
		return CanUpgradePresentationBindingByOrdinal(previous, next,
			documentPageCount);
	}

	bool StablePresentationTopologyChanged(
		const Bridge::PresentationTarget& previous,
		const Bridge::PresentationTarget& next) noexcept
	{
		return previous.key == next.key &&
			previous.sourceIdentity == next.sourceIdentity &&
			previous.processLocalIdentity == next.processLocalIdentity &&
			previous.bindingMode == Bridge::SlideBindingMode::StableSlideId &&
			next.bindingMode == Bridge::SlideBindingMode::StableSlideId &&
			previous.slideIds != next.slideIds;
	}

	std::string FormatPresentationKey(const Bridge::PresentationKey& key)
	{
		char buffer[37] = {};
		std::snprintf(buffer, sizeof(buffer),
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			key.bytes[0], key.bytes[1], key.bytes[2], key.bytes[3],
			key.bytes[4], key.bytes[5], key.bytes[6], key.bytes[7],
			key.bytes[8], key.bytes[9], key.bytes[10], key.bytes[11],
			key.bytes[12], key.bytes[13], key.bytes[14], key.bytes[15]);
		return buffer;
	}
}
