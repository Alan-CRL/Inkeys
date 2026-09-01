module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <deque>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Inkeys.Drawing.Draw3.auto_save;

import draw3.uink_file;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		using draw3::uink::CreateUInkGuid;
		using draw3::uink::Draw3UInkExportSnapshot;
		using draw3::uink::ExportDraw3SnapshotToUInk;
		using draw3::uink::FormatUInkGuid;
		using draw3::uink::ParseUInkGuid;
		using draw3::uink::ReadUInkFile;
		using draw3::uink::SaveUInkFile;
		using draw3::uink::UInkEditingSession;
		using draw3::uink::UInkReadStatus;
		using draw3::uink::UInkSaveMode;
		using draw3::uink::UInkSaveOptions;
		using draw3::uink::UInkSaveStatus;

		std::mutex testFaultMutex;
		DesktopAutoSaveTestFaultInjection testFaults;

		DesktopAutoSaveTestFaultInjection SnapshotTestFaults() noexcept
		{
			std::scoped_lock lock(testFaultMutex);
			return testFaults;
		}

		bool IsAsciiDigit(char value) noexcept
		{
			return value >= '0' && value <= '9';
		}

		bool ParseDecimal(const std::string& value, std::size_t offset,
			std::size_t length, unsigned int& result) noexcept
		{
			if (length == 0 || offset > value.size() ||
				length > value.size() - offset) return false;
			unsigned int parsed = 0;
			for (std::size_t index = 0; index < length; ++index)
			{
				const char character = value[offset + index];
				if (!IsAsciiDigit(character)) return false;
				parsed = parsed * 10u + static_cast<unsigned int>(character - '0');
			}
			result = parsed;
			return true;
		}

		bool IsLeapYear(unsigned int year) noexcept
		{
			return year % 4u == 0u && (year % 100u != 0u || year % 400u == 0u);
		}

		bool IsValidLocalDate(const std::string& value) noexcept
		{
			if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
			for (std::size_t index = 0; index < value.size(); ++index)
				if (index != 4 && index != 7 && !IsAsciiDigit(value[index])) return false;
			unsigned int year = 0;
			unsigned int month = 0;
			unsigned int day = 0;
			if (!ParseDecimal(value, 0, 4, year) ||
				!ParseDecimal(value, 5, 2, month) ||
				!ParseDecimal(value, 8, 2, day) || year == 0 || month == 0 || month > 12)
				return false;
			constexpr std::array<unsigned int, 12> kDaysPerMonth = {
				31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
			};
			unsigned int maximumDay = kDaysPerMonth[month - 1];
			if (month == 2 && IsLeapYear(year)) ++maximumDay;
			return day != 0 && day <= maximumDay;
		}

		bool IsValidFileTime(const std::string& value) noexcept
		{
			if (value.size() != 9 ||
				!std::all_of(value.begin(), value.end(), IsAsciiDigit)) return false;
			unsigned int hour = 0;
			unsigned int minute = 0;
			unsigned int second = 0;
			unsigned int millisecond = 0;
			return ParseDecimal(value, 0, 2, hour) && hour <= 23 &&
				ParseDecimal(value, 2, 2, minute) && minute <= 59 &&
				ParseDecimal(value, 4, 2, second) && second <= 59 &&
				ParseDecimal(value, 6, 3, millisecond) && millisecond <= 999;
		}

		bool IsValidCreatedAtValue(
			const std::string& value, const std::string& localDate) noexcept
		{
			if (!IsValidLocalDate(localDate) || value.size() != 29 ||
				value.compare(0, 10, localDate) != 0 ||
				value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
				value[19] != '.' || (value[23] != '+' && value[23] != '-') ||
				value[26] != ':') return false;
			for (std::size_t index : { 11u, 12u, 14u, 15u, 17u, 18u, 20u, 21u, 22u,
				24u, 25u, 27u, 28u })
				if (!IsAsciiDigit(value[index])) return false;
			unsigned int hour = 0;
			unsigned int minute = 0;
			unsigned int second = 0;
			unsigned int millisecond = 0;
			unsigned int offsetHour = 0;
			unsigned int offsetMinute = 0;
			return ParseDecimal(value, 11, 2, hour) && hour <= 23 &&
				ParseDecimal(value, 14, 2, minute) && minute <= 59 &&
				ParseDecimal(value, 17, 2, second) && second <= 59 &&
				ParseDecimal(value, 20, 3, millisecond) && millisecond <= 999 &&
				ParseDecimal(value, 24, 2, offsetHour) && offsetHour <= 23 &&
				ParseDecimal(value, 27, 2, offsetMinute) && offsetMinute <= 59;
		}

		bool IsValidCreatedAt(const DesktopAutoSaveTimestamp& timestamp) noexcept
		{
			return IsValidCreatedAtValue(timestamp.createdAt, timestamp.localDate) &&
				timestamp.localTimeForFile.size() == 9 &&
				timestamp.createdAt[11] == timestamp.localTimeForFile[0] &&
				timestamp.createdAt[12] == timestamp.localTimeForFile[1] &&
				timestamp.createdAt[14] == timestamp.localTimeForFile[2] &&
				timestamp.createdAt[15] == timestamp.localTimeForFile[3] &&
				timestamp.createdAt[17] == timestamp.localTimeForFile[4] &&
				timestamp.createdAt[18] == timestamp.localTimeForFile[5] &&
				timestamp.createdAt[20] == timestamp.localTimeForFile[6] &&
				timestamp.createdAt[21] == timestamp.localTimeForFile[7] &&
				timestamp.createdAt[22] == timestamp.localTimeForFile[8];
		}

		bool IsCanonicalGuid(const std::string& value)
		{
			const std::optional<draw3::uink::UInkGuid> guid = ParseUInkGuid(value);
			return guid && FormatUInkGuid(*guid) == value;
		}

		bool IsValidTrigger(DesktopAutoSaveTrigger trigger) noexcept
		{
			return trigger == DesktopAutoSaveTrigger::Clear ||
				trigger == DesktopAutoSaveTrigger::Exit;
		}

		std::wstring WidenAscii(const std::string& value)
		{
			return std::wstring(value.begin(), value.end());
		}

		std::string NarrowAscii(const std::wstring& value)
		{
			std::string result;
			result.reserve(value.size());
			for (wchar_t character : value)
			{
				if (character > 0x7f) return {};
				result.push_back(static_cast<char>(character));
			}
			return result;
		}

		std::wstring JoinPath(const std::wstring& parent, const std::wstring& child)
		{
			if (parent.empty()) return child;
			if (parent.back() == L'\\' || parent.back() == L'/') return parent + child;
			return parent + L"\\" + child;
		}

		bool PathExists(const std::wstring& path) noexcept
		{
			return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
		}

		bool IsRegularFile(const std::wstring& path) noexcept
		{
			const DWORD attributes = GetFileAttributesW(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES &&
				(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		bool EnsureDirectory(const std::wstring& path)
		{
			std::error_code error;
			if (std::filesystem::is_directory(path, error)) return true;
			error.clear();
			return std::filesystem::create_directories(path, error) ||
				std::filesystem::is_directory(path, error);
		}

		std::optional<std::wstring> NormalizeFullPath(const std::wstring& path)
		{
			const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
			if (required == 0) return std::nullopt;
			std::wstring result(required, L'\0');
			const DWORD written = GetFullPathNameW(
				path.c_str(), required, result.data(), nullptr);
			if (written == 0 || written >= required) return std::nullopt;
			result.resize(written);
			std::transform(result.begin(), result.end(), result.begin(),
				[](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
			return result;
		}

		std::uint64_t Fnv1aAppend(std::uint64_t hash,
			const void* bytes, std::size_t size) noexcept
		{
			const auto* cursor = static_cast<const std::uint8_t*>(bytes);
			for (std::size_t index = 0; index < size; ++index)
			{
				hash ^= cursor[index];
				hash *= 1099511628211ull;
			}
			return hash;
		}

		std::uint64_t Fnv1aString(std::uint64_t hash, const std::string& value) noexcept
		{
			return Fnv1aAppend(hash, value.data(), value.size());
		}

		class NamedMutexGuard
		{
		public:
			bool Acquire(const std::wstring& root, const std::string& date)
			{
				const std::optional<std::wstring> normalized = NormalizeFullPath(root);
				if (!normalized) return false;
				std::uint64_t hash = 14695981039346656037ull;
				hash = Fnv1aAppend(hash, normalized->data(),
					normalized->size() * sizeof(wchar_t));
				hash = Fnv1aString(hash, date);
				wchar_t name[96] = {};
				swprintf_s(name, L"Local\\Inkeys_Draw3_DesktopAutoSave_Index_%016llx",
					static_cast<unsigned long long>(hash));
				handle_ = CreateMutexW(nullptr, FALSE, name);
				if (!handle_) return false;
				const DWORD wait = WaitForSingleObject(handle_, INFINITE);
				acquired_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
				if (!acquired_)
				{
					CloseHandle(handle_);
					handle_ = nullptr;
				}
				return acquired_;
			}

			~NamedMutexGuard()
			{
				if (acquired_) ReleaseMutex(handle_);
				if (handle_) CloseHandle(handle_);
			}

		private:
			HANDLE handle_ = nullptr;
			bool acquired_ = false;
		};

		bool ReadTextFile(const std::wstring& path, std::string& text) noexcept
		{
			HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			LARGE_INTEGER length = {};
			if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
				static_cast<unsigned long long>(length.QuadPart) >
					static_cast<unsigned long long>((std::numeric_limits<std::size_t>::max)()))
			{
				CloseHandle(file);
				return false;
			}
			try
			{
				text.resize(static_cast<std::size_t>(length.QuadPart));
			}
			catch (...)
			{
				CloseHandle(file);
				return false;
			}
			std::size_t offset = 0;
			while (offset < text.size())
			{
				const DWORD requested = static_cast<DWORD>((std::min)(
					text.size() - offset, static_cast<std::size_t>(0x40000000u)));
				DWORD read = 0;
				if (!ReadFile(file, text.data() + offset, requested, &read, nullptr) || read == 0)
				{
					CloseHandle(file);
					return false;
				}
				offset += read;
			}
			CloseHandle(file);
			return true;
		}

		bool WriteNewTextFileDurable(
			const std::wstring& path, const std::string& text) noexcept
		{
			HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			std::size_t offset = 0;
			bool succeeded = true;
			while (offset < text.size())
			{
				const DWORD requested = static_cast<DWORD>((std::min)(
					text.size() - offset, static_cast<std::size_t>(0x40000000u)));
				DWORD written = 0;
				if (!WriteFile(file, text.data() + offset, requested, &written, nullptr) ||
					written == 0)
				{
					succeeded = false;
					break;
				}
				offset += written;
			}
			if (succeeded) succeeded = FlushFileBuffers(file) != FALSE;
			CloseHandle(file);
			// 只清理本次事务尚未发布的临时文件，不触碰任何历史文件。
			if (!succeeded) DeleteFileW(path.c_str());
			return succeeded;
		}

		bool IsSimpleUInkFileName(const std::string& value) noexcept
		{
			return value.size() > 5 && value.ends_with(".uink") &&
				value.find('/') == std::string::npos &&
				value.find('\\') == std::string::npos &&
				value.find("..") == std::string::npos &&
				std::all_of(value.begin(), value.end(), [](char character)
					{
						const unsigned char byte = static_cast<unsigned char>(character);
						return byte >= 0x20 && byte <= 0x7e;
					});
		}

		bool ValidateIndex(const Json::Value& root, const std::string& date,
			const std::wstring& dateDirectory)
		{
			if (!root.isObject() || !root["schemaVersion"].isIntegral() ||
				root["schemaVersion"].asInt() != 1 ||
				!root["scenario"].isString() || root["scenario"].asString() != "desktop" ||
				!root["date"].isString() || root["date"].asString() != date ||
				!root["entries"].isArray()) return false;
			std::set<std::string> requestIds;
			std::set<std::string> fileGuids;
			std::set<std::string> relativePaths;
			std::set<std::pair<std::string, std::uint64_t>> sessionSequences;
			std::uint64_t previousSequence = 0;
			for (const Json::Value& entry : root["entries"])
			{
				if (!entry.isObject() || !entry["dailySequence"].isUInt64() ||
					!entry["sequenceInSession"].isUInt64() ||
					!entry["saveRequestId"].isString() ||
					!entry["sessionId"].isString() || !entry["trigger"].isString() ||
					!entry["fileGuid"].isString() || !entry["relativePath"].isString() ||
					!entry["createdAt"].isString()) return false;
				const std::uint64_t sequence = entry["dailySequence"].asUInt64();
				if (sequence == 0 || sequence <= previousSequence ||
					entry["sequenceInSession"].asUInt64() == 0) return false;
				previousSequence = sequence;
				const std::string requestId = entry["saveRequestId"].asString();
				const std::string sessionId = entry["sessionId"].asString();
				const std::uint64_t sequenceInSession =
					entry["sequenceInSession"].asUInt64();
				const std::string fileGuid = entry["fileGuid"].asString();
				const std::string relativePath = entry["relativePath"].asString();
				const std::wstring relativePathWide = WidenAscii(relativePath);
				if (!IsCanonicalGuid(requestId) || !requestIds.insert(requestId).second ||
					!IsCanonicalGuid(sessionId) ||
					!sessionSequences.insert({ sessionId, sequenceInSession }).second ||
					!IsCanonicalGuid(fileGuid) || !fileGuids.insert(fileGuid).second ||
					!relativePaths.insert(relativePath).second ||
					!IsSimpleUInkFileName(relativePath) || relativePathWide.empty() ||
					!IsRegularFile(JoinPath(dateDirectory, relativePathWide)) ||
					(entry["trigger"].asString() != "clear" &&
						entry["trigger"].asString() != "exit") ||
					!IsValidCreatedAtValue(
						entry["createdAt"].asString(), date)) return false;
			}
			return true;
		}

		enum class IndexReadState
		{
			Missing,
			Valid,
			Invalid,
		};

		IndexReadState ReadIndex(const std::wstring& path,
			const std::string& date, const std::wstring& dateDirectory,
			Json::Value& root)
		{
			if (!PathExists(path)) return IndexReadState::Missing;
			std::string text;
			if (!ReadTextFile(path, text)) return IndexReadState::Invalid;
			Json::CharReaderBuilder builder;
			builder["collectComments"] = false;
			std::string errors;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			Json::Value parsed;
			if (!reader || !reader->parse(text.data(), text.data() + text.size(),
				&parsed, &errors) || !ValidateIndex(parsed, date, dateDirectory))
				return IndexReadState::Invalid;
			root = std::move(parsed);
			return IndexReadState::Valid;
		}

		Json::Value NewIndex(const std::string& date)
		{
			Json::Value root(Json::objectValue);
			root["schemaVersion"] = 1;
			root["scenario"] = "desktop";
			root["date"] = date;
			root["entries"] = Json::Value(Json::arrayValue);
			return root;
		}

		const char* TriggerName(DesktopAutoSaveTrigger trigger) noexcept
		{
			return trigger == DesktopAutoSaveTrigger::Exit ? "exit" : "clear";
		}

		bool EntryMatchesRequest(const Json::Value& entry,
			const DesktopAutoSaveRequest& request,
			const std::string& relativePath)
		{
			return entry["saveRequestId"].asString() == request.saveRequestId &&
				entry["sessionId"].asString() == request.sessionId &&
				entry["sequenceInSession"].asUInt64() == request.sequenceInSession &&
				entry["trigger"].asString() == TriggerName(request.trigger) &&
				entry["fileGuid"].asString() == FormatUInkGuid(request.snapshot.fileGuid) &&
				entry["relativePath"].asString() == relativePath &&
				entry["createdAt"].asString() == request.timestamp.createdAt;
		}

		bool IndexContainsRequest(const Json::Value& root,
			const std::string& requestId)
		{
			for (const Json::Value& entry : root["entries"])
				if (entry["saveRequestId"].asString() == requestId) return true;
			return false;
		}

		bool CommitIndex(const std::wstring& rootPath,
			const std::wstring& dateDirectory, const DesktopAutoSaveRequest& request,
			const std::string& relativePath)
		{
			NamedMutexGuard mutex;
			if (!mutex.Acquire(rootPath, request.timestamp.localDate)) return false;
			const std::wstring indexPath = JoinPath(dateDirectory, L"index.json");
			const std::wstring backupPath = JoinPath(dateDirectory, L"index.json.bak");
			Json::Value root;
			const IndexReadState primary = ReadIndex(
				indexPath, request.timestamp.localDate, dateDirectory, root);
			bool primaryValid = primary == IndexReadState::Valid;
			if (!primaryValid)
			{
				Json::Value backup;
				const IndexReadState backupState = ReadIndex(
					backupPath, request.timestamp.localDate, dateDirectory, backup);
				if (backupState == IndexReadState::Valid) root = std::move(backup);
				else if (primary == IndexReadState::Missing &&
					backupState == IndexReadState::Missing) root = NewIndex(request.timestamp.localDate);
				else return false;
			}

			std::uint64_t maximumDailySequence = 0;
			for (const Json::Value& entry : root["entries"])
			{
				maximumDailySequence = (std::max)(maximumDailySequence,
					entry["dailySequence"].asUInt64());
				if (entry["saveRequestId"].asString() == request.saveRequestId)
					return EntryMatchesRequest(entry, request, relativePath);
			}
			if (maximumDailySequence == (std::numeric_limits<std::uint64_t>::max)())
				return false;

			Json::Value entry(Json::objectValue);
			entry["dailySequence"] = Json::UInt64(maximumDailySequence + 1);
			entry["saveRequestId"] = request.saveRequestId;
			entry["sessionId"] = request.sessionId;
			entry["sequenceInSession"] = Json::UInt64(request.sequenceInSession);
			entry["trigger"] = TriggerName(request.trigger);
			entry["fileGuid"] = FormatUInkGuid(request.snapshot.fileGuid);
			entry["relativePath"] = relativePath;
			entry["createdAt"] = request.timestamp.createdAt;
			root["entries"].append(std::move(entry));

			Json::StreamWriterBuilder writer;
			writer["indentation"] = "  ";
			writer["commentStyle"] = "None";
			const std::string text = Json::writeString(writer, root);
			const std::optional<draw3::uink::UInkGuid> temporaryGuid = CreateUInkGuid();
			if (!temporaryGuid) return false;
			const std::wstring temporaryPath = indexPath + L"." +
				WidenAscii(FormatUInkGuid(*temporaryGuid)) + L".tmp";
			if (!WriteNewTextFileDurable(temporaryPath, text)) return false;
			Json::Value validated;
			if (ReadIndex(temporaryPath, request.timestamp.localDate,
				dateDirectory, validated) !=
				IndexReadState::Valid || !IndexContainsRequest(validated, request.saveRequestId))
			{
				DeleteFileW(temporaryPath.c_str());
				return false;
			}

			BOOL published = FALSE;
			if (PathExists(indexPath))
			{
				published = ReplaceFileW(indexPath.c_str(), temporaryPath.c_str(),
					primaryValid ? backupPath.c_str() : nullptr,
					REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
			}
			else
			{
				published = MoveFileExW(temporaryPath.c_str(), indexPath.c_str(),
					MOVEFILE_WRITE_THROUGH);
			}
			if (!published)
			{
				DeleteFileW(temporaryPath.c_str());
				return false;
			}
			Json::Value committed;
			return ReadIndex(indexPath, request.timestamp.localDate,
				dateDirectory, committed) ==
				IndexReadState::Valid &&
				IndexContainsRequest(committed, request.saveRequestId);
		}

		bool ExistingUInkMatches(const std::wstring& path,
			const DesktopAutoSaveRequest& request) noexcept
		{
			const auto read = ReadUInkFile(path);
			return read.status == UInkReadStatus::Complete && read.document &&
				read.document->header.guid == request.snapshot.fileGuid;
		}

		bool CommitUInk(const std::wstring& dateDirectory,
			const DesktopAutoSaveRequest& request,
			std::wstring& relativeFileName)
		{
			const auto exported = ExportDraw3SnapshotToUInk(request.snapshot);
			if (!exported.document) return false;
			UInkEditingSession session;
			session.document = *exported.document;
			UInkSaveOptions options;
			options.mode = UInkSaveMode::CreateNewLogicalFileWithIdentity;
			for (std::uint32_t suffix = 0; suffix < 10000; ++suffix)
			{
				relativeFileName = BuildDesktopAutoSaveFileName(
					request.timestamp, request.saveRequestId, suffix);
				if (relativeFileName.empty()) return false;
				const std::wstring path = JoinPath(dateDirectory, relativeFileName);
				if (PathExists(path))
				{
					if (ExistingUInkMatches(path, request)) return true;
					continue;
				}
				const auto saved = SaveUInkFile(path, session, options);
				if (saved.status == UInkSaveStatus::Committed ||
					saved.status == UInkSaveStatus::PartialCommitRequiresRecovery)
				{
					if (ExistingUInkMatches(path, request)) return true;
					return false;
				}
				if (saved.status == UInkSaveStatus::SourceChanged)
				{
					if (ExistingUInkMatches(path, request)) return true;
					continue;
				}
				return false;
			}
			return false;
		}

		bool ProcessRequest(const std::wstring& rootPath,
			const DesktopAutoSaveRequest& request) noexcept
		{
			const auto logFailure = [&request](const char* stage,
				const std::wstring& relativeFileName = {}) noexcept
			{
				char fileName[160] = "<pending>";
				if (!relativeFileName.empty() && relativeFileName.size() < sizeof(fileName))
				{
					std::size_t index = 0;
					for (; index < relativeFileName.size(); ++index)
					{
						if (relativeFileName[index] > 0x7f) break;
						fileName[index] = static_cast<char>(relativeFileName[index]);
					}
					if (index == relativeFileName.size()) fileName[index] = '\0';
					else strcpy_s(fileName, sizeof(fileName), "<non-ascii>");
				}
				std::fprintf(stderr,
					"[Draw3.AutoSave] action=commit request=%s result=failed "
					"stage=%s path=desktop/%s/%s\n",
					request.saveRequestId.c_str(), stage,
					request.timestamp.localDate.c_str(), fileName);
			};
			try
			{
				const DesktopAutoSaveTestFaultInjection faults = SnapshotTestFaults();
				if (faults.writeDelayMilliseconds != 0)
					Sleep(faults.writeDelayMilliseconds);
				const std::wstring desktopRoot = JoinPath(rootPath, L"desktop");
				const std::wstring dateDirectory = JoinPath(
					desktopRoot, WidenAscii(request.timestamp.localDate));
				if (!EnsureDirectory(dateDirectory))
				{
					logFailure("ensure-directory");
					return false;
				}
				if (faults.failUInkWrite)
				{
					logFailure("uink-injected", request.proposedFileName);
					return false;
				}
				std::wstring relativeFileName;
				if (!CommitUInk(dateDirectory, request, relativeFileName))
				{
					logFailure("uink-commit", relativeFileName);
					return false;
				}
				if (faults.failIndexCommit)
				{
					logFailure("index-injected", relativeFileName);
					return false;
				}
				const std::string relativePath = NarrowAscii(relativeFileName);
				if (relativePath.empty() || !CommitIndex(
					rootPath, dateDirectory, request, relativePath))
				{
					logFailure("index-commit", relativeFileName);
					return false;
				}
				return true;
			}
			catch (...)
			{
				logFailure("exception");
				return false;
			}
		}
	}

	void DesktopAutoSavePolicy::ObserveWorkspace(Bridge::Workspace workspace) noexcept
	{
		if (workspace == Bridge::Workspace::Presentation)
			eligibility_ = DesktopAutoSaveEligibility::PptTouched;
	}

	void DesktopAutoSavePolicy::CompleteDesktopClear() noexcept
	{
		eligibility_ = DesktopAutoSaveEligibility::Eligible;
	}

	bool DesktopAutoSavePolicy::ShouldCapture(Bridge::Workspace workspace,
		bool enabled, bool hasVisibleContent) const noexcept
	{
		return enabled && hasVisibleContent && workspace == Bridge::Workspace::Desktop &&
			eligibility_ == DesktopAutoSaveEligibility::Eligible;
	}

	DesktopAutoSaveEligibility DesktopAutoSavePolicy::Eligibility() const noexcept
	{
		return eligibility_;
	}

	DesktopAutoSaveTimestamp CaptureDesktopAutoSaveTimestamp() noexcept
	{
		DesktopAutoSaveTimestamp result;
		SYSTEMTIME local = {};
		GetLocalTime(&local);
		TIME_ZONE_INFORMATION zone = {};
		const DWORD zoneId = GetTimeZoneInformation(&zone);
		LONG bias = zone.Bias;
		if (zoneId == TIME_ZONE_ID_DAYLIGHT) bias += zone.DaylightBias;
		else if (zoneId == TIME_ZONE_ID_STANDARD) bias += zone.StandardBias;
		const int offsetMinutes = static_cast<int>(-bias);
		const char offsetSign = offsetMinutes < 0 ? '-' : '+';
		const int absoluteOffset = std::abs(offsetMinutes);
		char date[16] = {};
		char fileTime[16] = {};
		char createdAt[40] = {};
		_snprintf_s(date, _TRUNCATE, "%04u-%02u-%02u",
			local.wYear, local.wMonth, local.wDay);
		_snprintf_s(fileTime, _TRUNCATE, "%02u%02u%02u%03u",
			local.wHour, local.wMinute, local.wSecond, local.wMilliseconds);
		_snprintf_s(createdAt, _TRUNCATE,
			"%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02d:%02d",
			local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute,
			local.wSecond, local.wMilliseconds, offsetSign,
			absoluteOffset / 60, absoluteOffset % 60);
		try
		{
			result.localDate = date;
			result.localTimeForFile = fileTime;
			result.createdAt = createdAt;
		}
		catch (...)
		{
			return {};
		}
		return result;
	}

	std::wstring BuildDesktopAutoSaveFileName(
		const DesktopAutoSaveTimestamp& timestamp,
		const std::string& saveRequestId, std::uint32_t collisionSuffix)
	{
		if (!IsValidFileTime(timestamp.localTimeForFile)) return {};
		const std::optional<draw3::uink::UInkGuid> guid = ParseUInkGuid(saveRequestId);
		if (!guid) return {};
		std::string token;
		for (char character : FormatUInkGuid(*guid))
			if (character != '-') token.push_back(character);
		if (token.size() < 8) return {};
		std::wstring result = WidenAscii(timestamp.localTimeForFile + "_" + token.substr(0, 8));
		if (collisionSuffix != 0) result += L"_" + std::to_wstring(collisionSuffix);
		return result + L".uink";
	}

	std::uint64_t EstimateDesktopAutoSaveSnapshotBytes(
		const Draw3UInkExportSnapshot& snapshot) noexcept
	{
		std::uint64_t bytes = sizeof(snapshot);
		auto add = [&bytes](std::uint64_t value) noexcept
		{
			const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
			bytes = maximum - bytes < value ? maximum : bytes + value;
		};
		if (snapshot.workspaceName) add(snapshot.workspaceName->size());
		for (const auto& canvas : snapshot.canvases)
		{
			add(sizeof(canvas));
			for (const auto& stroke : canvas.strokes)
			{
				add(sizeof(stroke));
				const std::uint64_t pointCount = static_cast<std::uint64_t>(stroke.points.size());
				const std::uint64_t pointSize = sizeof(draw3::uink::Draw3UInkPoint);
				add(pointCount > (std::numeric_limits<std::uint64_t>::max)() / pointSize
					? (std::numeric_limits<std::uint64_t>::max)()
					: pointCount * pointSize);
			}
		}
		return bytes;
	}

	bool IsValidDesktopAutoSaveRequest(const DesktopAutoSaveRequest& request) noexcept
	{
		try
		{
			if (!IsCanonicalGuid(request.saveRequestId) ||
				!IsCanonicalGuid(request.sessionId) ||
				!IsValidTrigger(request.trigger) ||
				request.sequenceInSession == 0 || !IsValidLocalDate(request.timestamp.localDate) ||
				!IsValidFileTime(request.timestamp.localTimeForFile) ||
				!IsValidCreatedAt(request.timestamp) || request.snapshot.fileGuid.IsZero() ||
				request.snapshot.workspaceGuid.IsZero() || request.snapshot.canvases.empty() ||
				request.proposedFileName != BuildDesktopAutoSaveFileName(
					request.timestamp, request.saveRequestId)) return false;
			bool hasContent = false;
			for (const auto& canvas : request.snapshot.canvases)
				hasContent = hasContent || !canvas.strokes.empty();
			return hasContent;
		}
		catch (...)
		{
			return false;
		}
	}

	void SetDesktopAutoSaveTestFaultInjection(
		const DesktopAutoSaveTestFaultInjection& injection) noexcept
	{
		std::scoped_lock lock(testFaultMutex);
		testFaults = injection;
	}

	void ResetDesktopAutoSaveTestFaultInjection() noexcept
	{
		SetDesktopAutoSaveTestFaultInjection({});
	}

	struct DesktopAutoSaveService::Impl
	{
		enum class RequestState
		{
			Pending,
			Writing,
			Committed,
			Failed,
		};

		struct Record
		{
			DesktopAutoSaveRequest request;
			RequestState state = RequestState::Pending;
		};

		mutable std::mutex mutex;
		std::condition_variable condition;
		std::deque<std::shared_ptr<Record>> queue;
		std::map<std::string, std::shared_ptr<Record>> records;
		std::thread worker;
		std::wstring rootPath;
		std::string sessionId;
		std::uint64_t nextSequence = 1;
		DesktopAutoSaveDiagnostics diagnostics;
		bool accepting = false;

		void WorkerMain() noexcept
		{
			for (;;)
			{
				std::shared_ptr<Record> record;
				std::wstring root;
				bool rootReady = true;
				{
					std::unique_lock lock(mutex);
					condition.wait(lock, [this] { return !queue.empty() || !accepting; });
					if (queue.empty()) break;
					record = queue.front();
					queue.pop_front();
					record->state = RequestState::Writing;
					try
					{
						root = rootPath;
					}
					catch (...)
					{
						rootReady = false;
					}
				}
				if (!rootReady)
					std::fprintf(stderr,
						"[Draw3.AutoSave] action=commit request=%s result=failed "
						"stage=root-copy\n", record->request.saveRequestId.c_str());
				const bool committed = rootReady && ProcessRequest(root, record->request);
				draw3::uink::Draw3UInkExportSnapshot completedSnapshot;
				{
					std::scoped_lock lock(mutex);
					record->state = committed ? RequestState::Committed : RequestState::Failed;
					if (committed) ++diagnostics.committed;
					else ++diagnostics.failed;
					if (diagnostics.pending != 0) --diagnostics.pending;
					if (diagnostics.queuedSnapshotBytes >= record->request.estimatedSnapshotBytes)
						diagnostics.queuedSnapshotBytes -= record->request.estimatedSnapshotBytes;
					else diagnostics.queuedSnapshotBytes = 0;
					// 终态只保留轻量幂等身份；大快照在 worker 线程、锁外释放。
					completedSnapshot = std::move(record->request.snapshot);
					record->request.estimatedSnapshotBytes = 0;
				}
				std::fprintf(committed ? stdout : stderr,
					"[Draw3.AutoSave] action=commit request=%s trigger=%s result=%s\n",
					record->request.saveRequestId.c_str(), TriggerName(record->request.trigger),
					committed ? "committed" : "failed");
			}
		}

		DesktopAutoSaveSubmitStatus SubmitLocked(DesktopAutoSaveRequest request) noexcept
		{
			if (!accepting) return DesktopAutoSaveSubmitStatus::Closed;
			if (!IsValidDesktopAutoSaveRequest(request))
				return DesktopAutoSaveSubmitStatus::Invalid;
			if (const auto existing = records.find(request.saveRequestId);
				existing != records.end())
			{
				++diagnostics.duplicate;
				return DesktopAutoSaveSubmitStatus::Existing;
			}
			try
			{
				auto record = std::make_shared<Record>();
				record->request = std::move(request);
				const auto [iterator, inserted] = records.emplace(
					record->request.saveRequestId, record);
				if (!inserted) return DesktopAutoSaveSubmitStatus::Existing;
				try
				{
					queue.push_back(record);
				}
				catch (...)
				{
					records.erase(iterator);
					throw;
				}
				++diagnostics.accepted;
				++diagnostics.pending;
				const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
				diagnostics.queuedSnapshotBytes = maximum - diagnostics.queuedSnapshotBytes <
					record->request.estimatedSnapshotBytes ? maximum :
					diagnostics.queuedSnapshotBytes + record->request.estimatedSnapshotBytes;
			}
			catch (...)
			{
				return DesktopAutoSaveSubmitStatus::Invalid;
			}
			condition.notify_one();
			return DesktopAutoSaveSubmitStatus::Accepted;
		}
	};

	DesktopAutoSaveService::DesktopAutoSaveService()
		: impl_(std::make_unique<Impl>())
	{
	}

	DesktopAutoSaveService::~DesktopAutoSaveService()
	{
		CloseAndDrain();
	}

	bool DesktopAutoSaveService::Start(std::wstring autoSaveRoot)
	{
		CloseAndDrain();
		if (autoSaveRoot.empty()) return false;
		try
		{
			const std::optional<draw3::uink::UInkGuid> sessionGuid = CreateUInkGuid();
			if (!sessionGuid) return false;
			std::string sessionId = FormatUInkGuid(*sessionGuid);
			{
				std::scoped_lock lock(impl_->mutex);
				impl_->rootPath = std::move(autoSaveRoot);
				impl_->sessionId = std::move(sessionId);
				impl_->nextSequence = 1;
				impl_->diagnostics = {};
				impl_->queue.clear();
				impl_->records.clear();
				impl_->accepting = true;
			}
			impl_->worker = std::thread([implementation = impl_.get()]
				{ implementation->WorkerMain(); });
		}
		catch (...)
		{
			std::scoped_lock lock(impl_->mutex);
			impl_->accepting = false;
			return false;
		}
		return true;
	}

	DesktopAutoSaveSubmitStatus DesktopAutoSaveService::Submit(
		DesktopAutoSaveTrigger trigger, Draw3UInkExportSnapshot snapshot) noexcept
	{
		try
		{
			const std::optional<draw3::uink::UInkGuid> requestGuid = CreateUInkGuid();
			if (!requestGuid) return DesktopAutoSaveSubmitStatus::Invalid;
			DesktopAutoSaveRequest request;
			request.saveRequestId = FormatUInkGuid(*requestGuid);
			request.trigger = trigger;
			request.timestamp = CaptureDesktopAutoSaveTimestamp();
			request.snapshot = std::move(snapshot);
			request.estimatedSnapshotBytes =
				EstimateDesktopAutoSaveSnapshotBytes(request.snapshot);
			std::scoped_lock lock(impl_->mutex);
			if (!impl_->accepting) return DesktopAutoSaveSubmitStatus::Closed;
			request.sessionId = impl_->sessionId;
			request.sequenceInSession = impl_->nextSequence++;
			request.proposedFileName = BuildDesktopAutoSaveFileName(
				request.timestamp, request.saveRequestId);
			return impl_->SubmitLocked(std::move(request));
		}
		catch (...)
		{
			return DesktopAutoSaveSubmitStatus::Invalid;
		}
	}

	DesktopAutoSaveSubmitStatus DesktopAutoSaveService::SubmitPrepared(
		DesktopAutoSaveRequest request) noexcept
	{
		std::scoped_lock lock(impl_->mutex);
		return impl_->SubmitLocked(std::move(request));
	}

	void DesktopAutoSaveService::CloseAndDrain() noexcept
	{
		{
			std::scoped_lock lock(impl_->mutex);
			impl_->accepting = false;
		}
		impl_->condition.notify_all();
		if (impl_->worker.joinable()) impl_->worker.join();
		std::scoped_lock lock(impl_->mutex);
	}

	DesktopAutoSaveDiagnostics DesktopAutoSaveService::Diagnostics() const noexcept
	{
		std::scoped_lock lock(impl_->mutex);
		return impl_->diagnostics;
	}

	std::string DesktopAutoSaveService::SessionId() const
	{
		std::scoped_lock lock(impl_->mutex);
		return impl_->sessionId;
	}
}
