#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <json/json.h>

#include "PptSession.h"

#include <memory>
#include <algorithm>

namespace Inkeys::Business
{
	PptSessionParseResult ParsePptSessionSnapshot(std::wstring_view json) noexcept
	{
		PptSessionParseResult result;
		try
		{
			if (json.empty() || json.size() > 1024 * 1024)
			{
				result.error = "session_payload_size";
				return result;
			}
			const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
				json.data(), static_cast<int>(json.size()), nullptr, 0, nullptr, nullptr);
			if (count <= 0 || count > 1024 * 1024)
			{
				result.error = "session_encoding";
				return result;
			}
			std::string bytes(static_cast<std::size_t>(count), '\0');
			if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, json.data(),
				static_cast<int>(json.size()), bytes.data(), count, nullptr, nullptr))
			{
				result.error = "session_encoding";
				return result;
			}
			Json::CharReaderBuilder builder;
			builder["rejectDupKeys"] = true;
			builder["failIfExtra"] = true;
			builder["stackLimit"] = 32;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			Json::Value root;
			if (!reader->parse(bytes.data(), bytes.data() + bytes.size(), &root,
				&result.error) || !root.isObject() || root.size() != 7
				|| !root["schemaVersion"].isInt() || root["schemaVersion"].asInt() != 1)
			{
				result.error = "session_schema";
				return result;
			}
			PptSessionSnapshot snapshot;
			auto ReadRevision = [&](const char* key, std::uint64_t& value)
			{
				const auto& member = root[key];
				if (!member.isUInt64()) return false;
				value = member.asUInt64();
				return value <= static_cast<std::uint64_t>(INT64_MAX);
			};
			if (!ReadRevision("stateRevision", snapshot.stateRevision)
				|| !ReadRevision("showSessionRevision", snapshot.showSessionRevision)
				|| !ReadRevision("bindingRevision", snapshot.bindingRevision)
				|| !root["lifecycle"].isString() || !root["pageStatus"].isString())
			{
				result.error = "session_fields";
				return result;
			}
			const auto lifecycle = root["lifecycle"].asString();
			if (lifecycle == "Active") snapshot.lifecycle = PptLifecycle::Active;
			else if (lifecycle == "Inactive") snapshot.lifecycle = PptLifecycle::Inactive;
			else if (lifecycle != "Unknown") { result.error = "session_lifecycle"; return result; }
			const auto status = root["pageStatus"].asString();
			if (status == "Valid") snapshot.pageStatus = PptPageStatus::Valid;
			else if (status == "EndScreen") snapshot.pageStatus = PptPageStatus::EndScreen;
			else if (status != "Unknown") { result.error = "session_page_status"; return result; }

			// 嵌套 descriptor 仍由现有严格解析器负责，不能建立第二套身份校验。
			Json::StreamWriterBuilder writer;
			writer["indentation"] = "";
			const std::string nested = Json::writeString(writer, root["descriptor"]);
			const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				nested.data(), static_cast<int>(nested.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>((std::max)(0, wideCount)), L'\0');
			if (wideCount <= 0 || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				nested.data(), static_cast<int>(nested.size()), wide.data(), wideCount))
			{
				result.error = "descriptor_encoding";
				return result;
			}
			auto descriptor = Drawing::Draw3::ParsePresentationDescriptorJson(wide);
			if (!descriptor.descriptor)
			{
				result.error = "descriptor:" + descriptor.error;
				return result;
			}
			snapshot.descriptor = std::move(*descriptor.descriptor);
			if (snapshot.descriptor.bindingRevision != snapshot.bindingRevision
				|| (snapshot.lifecycle == PptLifecycle::Active && snapshot.showSessionRevision == 0)
				|| (snapshot.pageStatus != PptPageStatus::Unknown && snapshot.lifecycle != PptLifecycle::Active)
				|| (snapshot.pageStatus == PptPageStatus::Valid
					&& snapshot.descriptor.status != Drawing::Draw3::PresentationDescriptorStatus::StableSlideIds
					&& snapshot.descriptor.status != Drawing::Draw3::PresentationDescriptorStatus::PageIndexFallback))
			{
				result.error = "session_descriptor_mismatch";
				return result;
			}
			result.snapshot = std::move(snapshot);
		}
		catch (...) { result.error = "session_exception"; }
		return result;
	}
}
