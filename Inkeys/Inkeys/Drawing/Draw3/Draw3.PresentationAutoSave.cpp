module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "Draw3.Presentation.h"

module Inkeys.Drawing.Draw3.presentation_auto_save;

import draw3.uink_file;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		using draw3::uink::CreateUInkEditingSession;
		using draw3::uink::CreateUInkGuid;
		using draw3::uink::Draw3UInkImportBindingMode;
		using draw3::uink::Draw3UInkImportExpectation;
		using draw3::uink::ExportDraw3SnapshotToUInk;
		using draw3::uink::FormatUInkGuid;
		using draw3::uink::ImportApplicationOwnedPresentation;
		using draw3::uink::ParseUInkGuid;
		using draw3::uink::ReadUInkFile;
		using draw3::uink::SaveUInkFile;
		using draw3::uink::UInkEditingSession;
		using draw3::uink::UInkReadStatus;
		using draw3::uink::UInkSaveMode;
		using draw3::uink::UInkSaveOptions;
		using draw3::uink::UInkSaveStatus;
		using draw3::uink::UInkSourceRevision;

		std::mutex testFaultMutex;
		PresentationAutoSaveTestFaultInjection testFaults;

		PresentationAutoSaveTestFaultInjection SnapshotTestFaults()
		{
			std::scoped_lock lock(testFaultMutex);
			return testFaults;
		}

		std::wstring JoinPath(const std::wstring& parent, const std::wstring& child)
		{
			if (parent.empty()) return child;
			if (parent.back() == L'\\' || parent.back() == L'/') return parent + child;
			return parent + L"\\" + child;
		}

		std::wstring WidenAscii(const std::string& value)
		{
			return std::wstring(value.begin(), value.end());
		}

		bool PathExists(const std::wstring& path) noexcept
		{
			return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
		}

		bool EnsureDirectory(const std::wstring& path)
		{
			std::error_code error;
			return std::filesystem::is_directory(path, error) ||
				std::filesystem::create_directories(path, error) ||
				std::filesystem::is_directory(path, error);
		}

		std::optional<std::wstring> ResolveFullPath(const std::wstring& path)
		{
			const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
			if (required == 0 || required > 32768) return std::nullopt;
			std::wstring result(required, L'\0');
			const DWORD written = GetFullPathNameW(path.c_str(), required,
				result.data(), nullptr);
			if (written == 0 || written >= required) return std::nullopt;
			result.resize(written);
			std::replace(result.begin(), result.end(), L'/', L'\\');
			return result;
		}

		std::optional<std::wstring> NormalizeFullPath(const std::wstring& path)
		{
			const auto resolved = ResolveFullPath(path);
			if (!resolved) return std::nullopt;
			std::wstring folded(resolved->size(), L'\0');
			if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
				resolved->data(), static_cast<int>(resolved->size()),
				folded.data(), static_cast<int>(folded.size()),
				nullptr, nullptr, 0) == 0) return std::nullopt;
			return folded;
		}

		std::uint64_t HashPath(const std::wstring& value) noexcept
		{
			std::uint64_t hash = 14695981039346656037ull;
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
			for (std::size_t index = 0; index < value.size() * sizeof(wchar_t); ++index)
			{
				hash ^= bytes[index];
				hash *= 1099511628211ull;
			}
			return hash;
		}

		class NamedMutexGuard
		{
		public:
			bool Acquire(const std::wstring& root)
			{
				const auto normalized = NormalizeFullPath(root);
				if (!normalized) return false;
				wchar_t name[96] = {};
				swprintf_s(name, L"Local\\Inkeys_Draw3_Presentation_Index_%016llx",
					static_cast<unsigned long long>(HashPath(*normalized)));
				handle_ = CreateMutexW(nullptr, FALSE, name);
				if (!handle_) return false;
				const DWORD wait = WaitForSingleObject(handle_, INFINITE);
				acquired_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
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

		std::string BytesToHex(const std::array<std::uint8_t, 32>& bytes)
		{
			static constexpr char digits[] = "0123456789abcdef";
			std::string result(64, '0');
			for (std::size_t index = 0; index < bytes.size(); ++index)
			{
				result[index * 2] = digits[bytes[index] >> 4];
				result[index * 2 + 1] = digits[bytes[index] & 0x0f];
			}
			return result;
		}

		bool HexToBytes(const std::string& value,
			std::array<std::uint8_t, 32>& bytes) noexcept
		{
			if (value.size() != 64) return false;
			auto nibble = [](char character) -> int
			{
				if (character >= '0' && character <= '9') return character - '0';
				if (character >= 'a' && character <= 'f') return character - 'a' + 10;
				return -1;
			};
			for (std::size_t index = 0; index < bytes.size(); ++index)
			{
				const int high = nibble(value[index * 2]);
				const int low = nibble(value[index * 2 + 1]);
				if (high < 0 || low < 0) return false;
				bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
			}
			return true;
		}

		Json::Value EncodeRevision(const UInkSourceRevision& revision)
		{
			Json::Value value(Json::objectValue);
			value["volumeSerial"] = revision.volumeSerial;
			value["fileIndex"] = Json::UInt64(revision.fileIndex);
			value["length"] = Json::UInt64(revision.length);
			value["lastWriteTime"] = Json::UInt64(revision.lastWriteTime);
			value["sha256"] = BytesToHex(revision.sha256);
			return value;
		}

		bool DecodeRevision(const Json::Value& value, UInkSourceRevision& revision)
		{
			return value.isObject() && value.size() == 5 &&
				value["volumeSerial"].isUInt() &&
				value["fileIndex"].isUInt64() && value["length"].isUInt64() &&
				value["lastWriteTime"].isUInt64() && value["sha256"].isString() &&
				((revision.volumeSerial = value["volumeSerial"].asUInt()), true) &&
				((revision.fileIndex = value["fileIndex"].asUInt64()), true) &&
				((revision.length = value["length"].asUInt64()), true) &&
				((revision.lastWriteTime = value["lastWriteTime"].asUInt64()), true) &&
				HexToBytes(value["sha256"].asString(), revision.sha256);
		}

		bool ReadTextFile(const std::wstring& path, std::string& text) noexcept
		{
			HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			LARGE_INTEGER length = {};
			if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
				length.QuadPart > 4 * 1024 * 1024)
			{
				CloseHandle(file);
				return false;
			}
			try { text.resize(static_cast<std::size_t>(length.QuadPart)); }
			catch (...) { CloseHandle(file); return false; }
			std::size_t offset = 0;
			while (offset < text.size())
			{
				DWORD read = 0;
				const DWORD requested = static_cast<DWORD>((std::min)(
					text.size() - offset, std::size_t{ 0x40000000u }));
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

		bool WriteNewTextFileDurable(const std::wstring& path,
			const std::string& text) noexcept
		{
			HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			std::size_t offset = 0;
			bool succeeded = true;
			while (offset < text.size())
			{
				DWORD written = 0;
				const DWORD requested = static_cast<DWORD>((std::min)(
					text.size() - offset, std::size_t{ 0x40000000u }));
				if (!WriteFile(file, text.data() + offset, requested, &written, nullptr) ||
					written == 0) { succeeded = false; break; }
				offset += written;
			}
			if (succeeded) succeeded = FlushFileBuffers(file) != FALSE;
			CloseHandle(file);
			if (!succeeded) DeleteFileW(path.c_str());
			return succeeded;
		}

		struct IndexEntry
		{
			std::string sourceIdentity;
			std::string presentationKey;
			std::string sessionId;
			std::string fileGuid;
			std::string workspaceGuid;
			std::string relativePath;
			std::string bindingMode;
			bool processLocal = false;
			std::uint64_t bindingRevision = 0;
			std::uint64_t mutationRevision = 0;
			std::vector<std::int32_t> slideIds;
			UInkSourceRevision sourceRevision;
		};

		bool IsSafeRelativePath(const std::string& value) noexcept
		{
			return value.starts_with("files/") && value.ends_with(".uink") &&
				value.find("..") == std::string::npos &&
				value.find('\\') == std::string::npos;
		}

		bool DecodeEntry(const Json::Value& value, IndexEntry& entry)
		{
			if (!value.isObject() || value.size() != 12 ||
				!value["sourceIdentity"].isString() ||
				!value["presentationKey"].isString() || !value["sessionId"].isString() ||
				!value["fileGuid"].isString() || !value["workspaceGuid"].isString() ||
				!value["relativePath"].isString() || !value["bindingMode"].isString() ||
				!value["processLocal"].isBool() || !value["bindingRevision"].isUInt64() ||
				!value["mutationRevision"].isUInt64() ||
				!value["slideIds"].isArray() ||
				value["slideIds"].size() > Bridge::kMaximumPresentationPages) return false;
			entry.sourceIdentity = value["sourceIdentity"].asString();
			entry.presentationKey = value["presentationKey"].asString();
			entry.sessionId = value["sessionId"].asString();
			entry.fileGuid = value["fileGuid"].asString();
			entry.workspaceGuid = value["workspaceGuid"].asString();
			entry.relativePath = value["relativePath"].asString();
			entry.bindingMode = value["bindingMode"].asString();
			entry.processLocal = value["processLocal"].asBool();
			entry.bindingRevision = value["bindingRevision"].asUInt64();
			entry.mutationRevision = value["mutationRevision"].asUInt64();
			if (entry.sourceIdentity.empty() || entry.sourceIdentity.size() > 32768 ||
				!ParseUInkGuid(entry.presentationKey) || !ParseUInkGuid(entry.sessionId) ||
				!ParseUInkGuid(entry.fileGuid) || !ParseUInkGuid(entry.workspaceGuid) ||
				!IsSafeRelativePath(entry.relativePath) ||
				entry.mutationRevision == 0 ||
				(entry.bindingMode != "slide-id" && entry.bindingMode != "page-index") ||
				!DecodeRevision(value["sourceRevision"], entry.sourceRevision)) return false;
			if (entry.relativePath != "files/" + entry.fileGuid + ".uink") return false;
			std::set<std::int32_t> ids;
			for (const Json::Value& id : value["slideIds"])
			{
				if (!id.isInt() || id.asInt() <= 0 || !ids.insert(id.asInt()).second)
					return false;
				entry.slideIds.push_back(id.asInt());
			}
			return (entry.bindingMode == "slide-id") == !entry.slideIds.empty();
		}

		Json::Value EncodeEntry(const IndexEntry& entry)
		{
			Json::Value value(Json::objectValue);
			value["sourceIdentity"] = entry.sourceIdentity;
			value["presentationKey"] = entry.presentationKey;
			value["sessionId"] = entry.sessionId;
			value["fileGuid"] = entry.fileGuid;
			value["workspaceGuid"] = entry.workspaceGuid;
			value["relativePath"] = entry.relativePath;
			value["bindingMode"] = entry.bindingMode;
			value["processLocal"] = entry.processLocal;
			value["bindingRevision"] = Json::UInt64(entry.bindingRevision);
			value["mutationRevision"] = Json::UInt64(entry.mutationRevision);
			value["slideIds"] = Json::Value(Json::arrayValue);
			for (std::int32_t id : entry.slideIds) value["slideIds"].append(id);
			value["sourceRevision"] = EncodeRevision(entry.sourceRevision);
			return value;
		}

		enum class IndexState { Missing, Valid, Invalid };

		IndexState ReadIndex(const std::wstring& path,
			std::vector<IndexEntry>& entries)
		{
			if (!PathExists(path)) return IndexState::Missing;
			std::string text;
			if (!ReadTextFile(path, text)) return IndexState::Invalid;
			Json::CharReaderBuilder builder;
			builder["collectComments"] = false;
			builder["failIfExtra"] = true;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			Json::Value root;
			std::string errors;
			if (!reader || !reader->parse(text.data(), text.data() + text.size(),
				&root, &errors) || !root.isObject() || root.size() != 3 ||
				!root["schemaVersion"].isInt() || root["schemaVersion"].asInt() != 1 ||
				!root["scenario"].isString() || root["scenario"].asString() != "presentation" ||
				!root["entries"].isArray()) return IndexState::Invalid;
			std::set<std::string> sources;
			std::set<std::string> keys;
			std::set<std::string> fileGuids;
			std::set<std::string> paths;
			for (const Json::Value& value : root["entries"])
			{
				IndexEntry entry;
				if (!DecodeEntry(value, entry) ||
					!sources.insert(entry.sourceIdentity).second ||
					!keys.insert(entry.presentationKey).second ||
					!fileGuids.insert(entry.fileGuid).second ||
					!paths.insert(entry.relativePath).second) return IndexState::Invalid;
				entries.push_back(std::move(entry));
			}
			return IndexState::Valid;
		}

		bool LoadIndexWithBackup(const std::wstring& root,
			std::vector<IndexEntry>& entries, bool& primaryValid)
		{
			const std::wstring index = JoinPath(root, L"index.json");
			const std::wstring backup = JoinPath(root, L"index.json.bak");
			const IndexState state = ReadIndex(index, entries);
			primaryValid = state == IndexState::Valid;
			if (primaryValid) return true;
			entries.clear();
			const IndexState backupState = ReadIndex(backup, entries);
			if (backupState == IndexState::Valid) return true;
			entries.clear();
			return state == IndexState::Missing && backupState == IndexState::Missing;
		}

		bool CommitIndex(const std::wstring& root,
			const std::vector<IndexEntry>& entries, bool primaryValid)
		{
			Json::Value document(Json::objectValue);
			document["schemaVersion"] = 1;
			document["scenario"] = "presentation";
			document["entries"] = Json::Value(Json::arrayValue);
			for (const IndexEntry& entry : entries)
				document["entries"].append(EncodeEntry(entry));
			Json::StreamWriterBuilder writer;
			writer["indentation"] = "  ";
			writer["commentStyle"] = "None";
			const std::string text = Json::writeString(writer, document);
			const auto temporaryGuid = CreateUInkGuid();
			if (!temporaryGuid) return false;
			const std::wstring index = JoinPath(root, L"index.json");
			const std::wstring backup = JoinPath(root, L"index.json.bak");
			const std::wstring temporary = index + L"." +
				WidenAscii(FormatUInkGuid(*temporaryGuid)) + L".tmp";
			if (!WriteNewTextFileDurable(temporary, text)) return false;
			std::vector<IndexEntry> validated;
			if (ReadIndex(temporary, validated) != IndexState::Valid ||
				validated.size() != entries.size())
			{
				DeleteFileW(temporary.c_str());
				return false;
			}
			BOOL committed = FALSE;
			if (PathExists(index))
				committed = ReplaceFileW(index.c_str(), temporary.c_str(),
					primaryValid ? backup.c_str() : nullptr,
					REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
			else
				committed = MoveFileExW(temporary.c_str(), index.c_str(),
					MOVEFILE_WRITE_THROUGH);
			if (!committed) DeleteFileW(temporary.c_str());
			return committed != FALSE;
		}

		std::string BindingModeName(Bridge::SlideBindingMode mode) noexcept
		{
			return mode == Bridge::SlideBindingMode::StableSlideId
				? "slide-id" : "page-index";
		}

		std::vector<std::int32_t> MergeSlideIds(
			const std::vector<std::int32_t>& existing,
			const std::vector<std::int32_t>& active)
		{
			std::vector<std::int32_t> result = existing;
			for (const auto id : active)
				if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
			return result;
		}

		bool ContainsSlideIdSet(const std::vector<std::int32_t>& known,
			const std::vector<std::int32_t>& active) noexcept
		{
			return std::all_of(active.begin(), active.end(), [&](auto id)
				{ return std::find(known.begin(), known.end(), id) != known.end(); });
		}

		bool RequiresExactFallbackBinding(const IndexEntry& entry,
			const Bridge::PresentationTarget& target) noexcept
		{
			return entry.processLocal || target.processLocalIdentity;
		}

		bool ValidatePresentationTarget(
			const Bridge::PresentationTarget& target) noexcept
		{
			if (target.key.IsZero() || target.sourceIdentity.empty() ||
				target.totalPages == 0 ||
				target.totalPages > Bridge::kMaximumPresentationPages ||
				target.pageIndex >= target.totalPages) return false;
			if (target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
				return target.slideId && target.slideIds.size() == target.totalPages &&
					target.slideIds[target.pageIndex] == *target.slideId;
			return !target.slideId && target.slideIds.empty();
		}

		bool ValidatePresentationSaveRequest(
			const PresentationSaveRequest& request) noexcept
		{
			try
			{
				if (!ValidatePresentationTarget(request.target) ||
					request.mutationRevision == 0 || request.snapshot.fileGuid.IsZero() ||
					request.snapshot.workspaceGuid.IsZero() ||
					request.snapshot.hostId != FormatPresentationKey(request.target.key) ||
					request.snapshot.currentPageIndex != request.target.pageIndex ||
					(request.snapshot.activeCanvases.empty() && request.snapshot.retainedCanvases.empty()
						? request.snapshot.canvases.size() < request.target.totalPages
						: request.snapshot.activeCanvases.size() != request.target.totalPages))
					return false;
				const bool stable = request.target.bindingMode ==
					Bridge::SlideBindingMode::StableSlideId;
				const auto importMode = stable
					? Draw3UInkImportBindingMode::StableSlideId
					: Draw3UInkImportBindingMode::PageIndexFallback;
				if (request.snapshot.workspaceType != (stable ? 2 :
					draw3::uink::kInkeysPageIndexWorkspaceType) ||
					!draw3::uink::HasInkeysBindingExtra(
						request.snapshot.workspaceExtra, importMode)) return false;
				const auto& active = request.snapshot.activeCanvases.empty()
					? request.snapshot.canvases : request.snapshot.activeCanvases;
				for (std::size_t index = 0; index < active.size(); ++index)
				{
					const auto& canvas = active[index];
					if (canvas.pageIndex != index || canvas.pageNumber != index + 1 ||
						!draw3::uink::HasInkeysBindingExtra(canvas.extra, importMode) ||
						(stable && (!canvas.slideId ||
							*canvas.slideId != request.target.slideIds[index])) ||
						(!stable && canvas.slideId)) return false;
				}
				if (!request.snapshot.retainedCanvases.empty())
					for (const auto& canvas : request.snapshot.retainedCanvases)
						if (!canvas.retained || !canvas.slideId ||
							!draw3::uink::HasInkeysPageStateExtra(canvas.extra, true)) return false;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		const char* PersistenceStatusName(PresentationPersistenceStatus status) noexcept
		{
			switch (status)
			{
			case PresentationPersistenceStatus::Committed: return "committed";
			case PresentationPersistenceStatus::Loaded: return "loaded";
			case PresentationPersistenceStatus::NotFound: return "not_found";
			case PresentationPersistenceStatus::CrossProcessConflictDeferred:
				return "cross_process_conflict_deferred";
			case PresentationPersistenceStatus::SourceChanged: return "source_changed";
			case PresentationPersistenceStatus::Invalid: return "invalid";
			case PresentationPersistenceStatus::IoError: return "io_error";
			default: return "unknown";
			}
		}

		Draw3UInkImportExpectation MakeExpectation(const IndexEntry& entry,
			const Bridge::PresentationTarget& target)
		{
			Draw3UInkImportExpectation expectation;
			expectation.fileGuid = *ParseUInkGuid(entry.fileGuid);
			expectation.hostId = entry.presentationKey;
			expectation.bindingMode = entry.bindingMode == "slide-id"
				? Draw3UInkImportBindingMode::StableSlideId
				: Draw3UInkImportBindingMode::PageIndexFallback;
			// 绑定模式以已保存索引为准；fallback 文件升级到 stable 时仍按旧页序读取。
			expectation.slideIds = expectation.bindingMode ==
				Draw3UInkImportBindingMode::StableSlideId ? target.slideIds : std::vector<std::int32_t>{};
			expectation.knownSlideIds = entry.slideIds;
			expectation.pageCount = target.totalPages;
			return expectation;
		}

		PresentationPersistenceStatus SavePresentation(const std::wstring& autoSaveRoot,
			const std::string& sessionId, const PresentationSaveRequest& request,
			std::map<std::string, IndexEntry>& pendingIndexEntries)
		{
			const PresentationAutoSaveTestFaultInjection faults = SnapshotTestFaults();
			if (faults.writeDelayMilliseconds != 0)
				Sleep(faults.writeDelayMilliseconds);
			const std::wstring root = JoinPath(autoSaveRoot, L"presentation");
			const std::wstring files = JoinPath(root, L"files");
			if (!EnsureDirectory(files)) return PresentationPersistenceStatus::IoError;
			NamedMutexGuard mutex;
			if (!mutex.Acquire(root)) return PresentationPersistenceStatus::IoError;
			std::vector<IndexEntry> entries;
			bool primaryValid = false;
			if (!LoadIndexWithBackup(root, entries, primaryValid))
				return PresentationPersistenceStatus::IoError;
			const std::string key = FormatPresentationKey(request.target.key);
			auto found = std::find_if(entries.begin(), entries.end(),
				[&](const IndexEntry& entry)
				{ return entry.sourceIdentity == request.target.sourceIdentity; });
			if (found != entries.end() && (found->sessionId != sessionId ||
				found->presentationKey != key))
				return PresentationPersistenceStatus::CrossProcessConflictDeferred;
			if (found != entries.end() && found->bindingMode == "page-index" &&
				RequiresExactFallbackBinding(*found, request.target) &&
				found->bindingRevision != request.target.bindingRevision)
				return PresentationPersistenceStatus::CrossProcessConflictDeferred;
			if (found != entries.end())
			{
				const auto pending = pendingIndexEntries.find(request.target.sourceIdentity);
				if (pending != pendingIndexEntries.end())
				{
					const std::wstring pendingPath = JoinPath(root,
						WidenAscii(pending->second.relativePath));
					const auto pendingRead = ReadUInkFile(pendingPath);
					if (pendingRead.sourceRevision &&
						*pendingRead.sourceRevision == pending->second.sourceRevision &&
						pending->second.sessionId == sessionId &&
						pending->second.presentationKey == key)
						*found = pending->second;
					else pendingIndexEntries.erase(pending);
				}
			}

			const auto exported = ExportDraw3SnapshotToUInk(request.snapshot);
			if (!exported.document) return PresentationPersistenceStatus::Invalid;
			UInkSourceRevision committedRevision;
			if (found == entries.end())
			{
				IndexEntry entry;
				entry.sourceIdentity = request.target.sourceIdentity;
				entry.presentationKey = key;
				entry.sessionId = sessionId;
				entry.fileGuid = FormatUInkGuid(request.snapshot.fileGuid);
				entry.workspaceGuid = FormatUInkGuid(request.snapshot.workspaceGuid);
				entry.relativePath = "files/" + entry.fileGuid + ".uink";
				entry.bindingMode = BindingModeName(request.target.bindingMode);
				entry.processLocal = request.target.processLocalIdentity;
				entry.bindingRevision = request.target.bindingRevision;
				entry.mutationRevision = request.mutationRevision;
				entry.slideIds = request.target.slideIds;
				const std::wstring path = JoinPath(root, WidenAscii(entry.relativePath));
				if (PathExists(path))
				{
					const auto existing = ReadUInkFile(path);
					if (!existing.document || !existing.sourceRevision ||
						existing.document->header.guid.Bytes() != request.snapshot.fileGuid.Bytes() ||
						ImportApplicationOwnedPresentation(*existing.document,
							MakeExpectation(entry, request.target)).status !=
								draw3::uink::Draw3UInkImportStatus::Success)
						return PresentationPersistenceStatus::SourceChanged;
					auto existingSession = CreateUInkEditingSession(existing,
						request.target.bindingMode == Bridge::SlideBindingMode::StableSlideId
							? draw3::uink::UInkEditingSource::ApplicationOwned
							: draw3::uink::UInkEditingSource::ApplicationOwnedPrivateWorkspace);
					if (!existingSession) return PresentationPersistenceStatus::Invalid;
					existingSession->document = *exported.document;
					UInkSaveOptions options;
					options.mode = UInkSaveMode::SaveExistingLogicalFile;
					const auto saved = SaveUInkFile(path, *existingSession, options);
					if (saved.status != UInkSaveStatus::Committed || !saved.revision)
						return saved.status == UInkSaveStatus::SourceChanged
							? PresentationPersistenceStatus::SourceChanged
							: PresentationPersistenceStatus::IoError;
					committedRevision = *saved.revision;
				}
				else
				{
					UInkEditingSession session;
					session.document = *exported.document;
					UInkSaveOptions options;
					options.mode = UInkSaveMode::CreateNewLogicalFileWithIdentity;
					const auto saved = SaveUInkFile(path, session, options);
					if (saved.status != UInkSaveStatus::Committed || !saved.revision)
						return saved.status == UInkSaveStatus::SourceChanged
							? PresentationPersistenceStatus::SourceChanged
							: PresentationPersistenceStatus::IoError;
					committedRevision = *saved.revision;
				}
				entry.sourceRevision = committedRevision;
				entries.push_back(std::move(entry));
			}
			else
			{
				const bool bindingUpgrade = found->bindingMode == "page-index" &&
					request.target.bindingMode == Bridge::SlideBindingMode::StableSlideId &&
					(!RequiresExactFallbackBinding(*found, request.target) ||
						found->bindingRevision == request.target.bindingRevision) &&
					found->slideIds.empty() && request.target.slideIds.size() ==
						request.target.totalPages;
				if (found->fileGuid != FormatUInkGuid(request.snapshot.fileGuid) ||
					found->workspaceGuid != FormatUInkGuid(request.snapshot.workspaceGuid) ||
					(!bindingUpgrade && (found->bindingMode !=
						BindingModeName(request.target.bindingMode) ||
						!ContainsSlideIdSet(found->slideIds, request.target.slideIds))))
					return PresentationPersistenceStatus::SourceChanged;
				const std::wstring path = JoinPath(root, WidenAscii(found->relativePath));
				const auto read = ReadUInkFile(path);
				if (read.status != UInkReadStatus::Complete || !read.document ||
					!read.sourceRevision || *read.sourceRevision != found->sourceRevision ||
					ImportApplicationOwnedPresentation(*read.document,
						MakeExpectation(*found, request.target)).status !=
							draw3::uink::Draw3UInkImportStatus::Success)
					return PresentationPersistenceStatus::SourceChanged;
				auto session = CreateUInkEditingSession(read,
					found->bindingMode == "slide-id"
						? draw3::uink::UInkEditingSource::ApplicationOwned
						: draw3::uink::UInkEditingSource::ApplicationOwnedPrivateWorkspace);
				if (!session) return PresentationPersistenceStatus::Invalid;
				session->document = *exported.document;
				UInkSaveOptions options;
				options.mode = UInkSaveMode::SaveExistingLogicalFile;
				const auto saved = SaveUInkFile(path, *session, options);
				if (saved.status != UInkSaveStatus::Committed || !saved.revision)
					return saved.status == UInkSaveStatus::SourceChanged
						? PresentationPersistenceStatus::SourceChanged
						: PresentationPersistenceStatus::IoError;
				found->sourceRevision = *saved.revision;
				found->bindingMode = BindingModeName(request.target.bindingMode);
				found->slideIds = MergeSlideIds(found->slideIds, request.target.slideIds);
				found->bindingRevision = request.target.bindingRevision;
				found->processLocal = request.target.processLocalIdentity;
				found->mutationRevision = request.mutationRevision;
			}
			if (faults.failIndexCommit || !CommitIndex(root, entries, primaryValid))
			{
				const auto written = std::find_if(entries.begin(), entries.end(),
					[&](const IndexEntry& entry)
					{ return entry.sourceIdentity == request.target.sourceIdentity; });
				if (written != entries.end())
					pendingIndexEntries[request.target.sourceIdentity] = *written;
				return PresentationPersistenceStatus::IoError;
			}
			pendingIndexEntries.erase(request.target.sourceIdentity);
			return PresentationPersistenceStatus::Committed;
		}

		PresentationPersistenceCompletion LoadPresentation(const std::wstring& autoSaveRoot,
			const std::string& sessionId, const PresentationLoadRequest& request,
			const std::map<std::string, IndexEntry>& pendingIndexEntries)
		{
			PresentationPersistenceCompletion completion;
			completion.operation = PresentationPersistenceOperation::Load;
			completion.target = request.target;
			const std::wstring root = JoinPath(autoSaveRoot, L"presentation");
			NamedMutexGuard mutex;
			if (!mutex.Acquire(root))
			{
				completion.status = PresentationPersistenceStatus::IoError;
				return completion;
			}
			std::vector<IndexEntry> entries;
			bool primaryValid = false;
			if (!LoadIndexWithBackup(root, entries, primaryValid))
			{
				completion.status = PresentationPersistenceStatus::IoError;
				return completion;
			}
			const std::string key = FormatPresentationKey(request.target.key);
			auto found = std::find_if(entries.begin(), entries.end(),
				[&](const IndexEntry& entry)
				{ return entry.sourceIdentity == request.target.sourceIdentity; });
			const auto pending = pendingIndexEntries.find(request.target.sourceIdentity);
			if (found == entries.end())
			{
				if (pending == pendingIndexEntries.end() ||
					pending->second.sessionId != sessionId ||
					pending->second.presentationKey != key)
				{
					completion.status = PresentationPersistenceStatus::NotFound;
					return completion;
				}
				// 首次文件已 durable commit 但 index 失败时，同根 Host 重启仍可严格校验后恢复。
				entries.push_back(pending->second);
				found = std::prev(entries.end());
			}
			else if (pending != pendingIndexEntries.end() &&
				pending->second.sessionId == sessionId &&
				pending->second.presentationKey == key)
				*found = pending->second;
			const bool bindingUpgrade = found->bindingMode == "page-index" &&
				request.target.bindingMode == Bridge::SlideBindingMode::StableSlideId &&
				(!RequiresExactFallbackBinding(*found, request.target) ||
					found->bindingRevision == request.target.bindingRevision) &&
				found->slideIds.empty() && request.target.slideIds.size() ==
					request.target.totalPages;
			if (found->sessionId != sessionId || found->presentationKey != key ||
				(found->bindingMode == "page-index" &&
					RequiresExactFallbackBinding(*found, request.target) &&
					found->bindingRevision != request.target.bindingRevision) ||
				(!bindingUpgrade && (found->bindingMode !=
					BindingModeName(request.target.bindingMode) ||
					!ContainsSlideIdSet(found->slideIds, request.target.slideIds))))
			{
				completion.status = PresentationPersistenceStatus::CrossProcessConflictDeferred;
				return completion;
			}
			const std::wstring path = JoinPath(root, WidenAscii(found->relativePath));
			const auto read = ReadUInkFile(path);
			if (read.status != UInkReadStatus::Complete || !read.document ||
				!read.sourceRevision || *read.sourceRevision != found->sourceRevision)
			{
				completion.status = PresentationPersistenceStatus::SourceChanged;
				return completion;
			}
			const auto imported = ImportApplicationOwnedPresentation(*read.document,
				MakeExpectation(*found, request.target));
			if (!imported.snapshot || FormatUInkGuid(imported.snapshot->workspaceGuid) !=
				found->workspaceGuid)
			{
				completion.status = PresentationPersistenceStatus::Invalid;
				return completion;
			}
			completion.mutationRevision = found->mutationRevision;
			completion.loadedSnapshot = std::make_shared<
				const draw3::uink::Draw3UInkExportSnapshot>(*imported.snapshot);
			completion.status = PresentationPersistenceStatus::Loaded;
			return completion;
		}

		std::string ProcessSessionId()
		{
			static const std::string value = []
			{
				const auto guid = CreateUInkGuid();
				return guid ? FormatUInkGuid(*guid) : std::string{};
			}();
			return value;
		}
	}

	struct PresentationAutoSaveService::Impl
	{
		struct WorkItem
		{
			PresentationPersistenceOperation operation =
				PresentationPersistenceOperation::Save;
			PresentationSaveRequest save;
			PresentationLoadRequest load;
		};

		mutable std::mutex mutex;
		std::condition_variable condition;
		std::deque<WorkItem> queue;
		std::deque<PresentationPersistenceCompletion> completions;
		std::map<std::string, IndexEntry> pendingIndexEntries;
		std::jthread worker;
		std::wstring autoSaveRoot;
		std::wstring autoSaveRootKey;
		std::string sessionId;
		void* wakeContext = nullptr;
		void (*wake)(void*) noexcept = nullptr;
		bool accepting = false;
		PresentationPersistenceDiagnostics diagnostics;

		void PushCompletion(PresentationPersistenceCompletion completion) noexcept
		{
			void* context = nullptr;
			void (*callback)(void*) noexcept = nullptr;
			try
			{
				std::scoped_lock lock(mutex);
				if (completion.status == PresentationPersistenceStatus::Committed)
					++diagnostics.committed;
				else if (completion.status == PresentationPersistenceStatus::Loaded)
					++diagnostics.loaded;
				else ++diagnostics.failed;
				completions.push_back(std::move(completion));
				context = wakeContext;
				callback = wake;
			}
			catch (...)
			{
				std::fputs("[Draw3.Presentation] action=completion result=failed reason=exception\n",
					stderr);
				return;
			}
			if (callback) callback(context);
		}

		void Run()
		{
			for (;;)
			{
				WorkItem item;
				{
					std::unique_lock lock(mutex);
					condition.wait(lock, [this] { return !queue.empty() || !accepting; });
					if (queue.empty() && !accepting) return;
					item = std::move(queue.front());
					queue.pop_front();
				}
				try
				{
					PresentationPersistenceCompletion completion;
					completion.operation = item.operation;
					if (item.operation == PresentationPersistenceOperation::Save)
					{
						completion.target = item.save.target;
						completion.mutationRevision = item.save.mutationRevision;
					}
					else completion.target = item.load.target;
					try
					{
						// 任一工作项异常都必须转换为终态，不能逃出 jthread 触发 terminate。
						if (SnapshotTestFaults().throwWorkerOperation)
							throw std::runtime_error("injected presentation worker exception");
						if (item.operation == PresentationPersistenceOperation::Save)
							completion.status = SavePresentation(
								autoSaveRoot, sessionId, item.save, pendingIndexEntries);
						else
							completion = LoadPresentation(
								autoSaveRoot, sessionId, item.load, pendingIndexEntries);
					}
					catch (...)
					{
						completion.status = PresentationPersistenceStatus::IoError;
					}
					if (completion.operation == PresentationPersistenceOperation::Save &&
						completion.status != PresentationPersistenceStatus::Committed)
						std::fprintf(stderr,
							"[Draw3.Presentation] action=save result=failed status=%s revision=%llu\n",
							PersistenceStatusName(completion.status),
							static_cast<unsigned long long>(completion.mutationRevision));
					else if (completion.operation == PresentationPersistenceOperation::Load &&
						completion.status != PresentationPersistenceStatus::Loaded &&
						completion.status != PresentationPersistenceStatus::NotFound)
						std::fprintf(stderr,
							"[Draw3.Presentation] action=load result=failed status=%s\n",
							PersistenceStatusName(completion.status));
					PushCompletion(std::move(completion));
				}
				catch (...)
				{
					// 连 completion payload 的复制失败也不能逃出 owned worker。
					std::fputs("[Draw3.Presentation] action=worker result=failed reason=exception\n",
						stderr);
				}
			}
		}
	};

	PresentationAutoSaveService::PresentationAutoSaveService()
		: impl_(std::make_unique<Impl>()) {}
	PresentationAutoSaveService::~PresentationAutoSaveService() { CloseAndDrain(); }

	bool PresentationAutoSaveService::Start(std::wstring autoSaveRoot,
		void* wakeContext, void (*wake)(void*) noexcept)
	{
		CloseAndDrain();
		if (autoSaveRoot.empty()) return false;
		const auto resolvedRoot = ResolveFullPath(autoSaveRoot);
		if (!resolvedRoot) return false;
		const auto normalizedRootKey = NormalizeFullPath(*resolvedRoot);
		if (!normalizedRootKey) return false;
		const PresentationAutoSaveTestFaultInjection faults = SnapshotTestFaults();
		const std::string sessionId = faults.sessionIdOverride.empty()
			? ProcessSessionId() : faults.sessionIdOverride;
		if (sessionId.empty()) return false;
		{
			std::scoped_lock lock(impl_->mutex);
			impl_->queue.clear();
			impl_->completions.clear();
			// index-commit 自修复状态只跨同一根目录的 Host generation 保留。
			if (impl_->autoSaveRootKey != *normalizedRootKey)
				impl_->pendingIndexEntries.clear();
			impl_->autoSaveRoot = *resolvedRoot;
			impl_->autoSaveRootKey = *normalizedRootKey;
			impl_->sessionId = sessionId;
			impl_->wakeContext = wakeContext;
			impl_->wake = wake;
			impl_->accepting = true;
		}
		try { impl_->worker = std::jthread([this] { impl_->Run(); }); }
		catch (...)
		{
			std::scoped_lock lock(impl_->mutex);
			impl_->accepting = false;
			return false;
		}
		return true;
	}

	PresentationPersistenceSubmitStatus PresentationAutoSaveService::SubmitSave(
		PresentationSaveRequest request) noexcept
	{
		if (!ValidatePresentationSaveRequest(request))
			return PresentationPersistenceSubmitStatus::Invalid;
		std::scoped_lock lock(impl_->mutex);
		if (!impl_->accepting) return PresentationPersistenceSubmitStatus::Closed;
		for (auto iterator = impl_->queue.rbegin(); iterator != impl_->queue.rend(); ++iterator)
		{
			if (iterator->operation == PresentationPersistenceOperation::Save &&
				iterator->save.target.key == request.target.key)
			{
				iterator->save = std::move(request);
				++impl_->diagnostics.replacedPending;
				return PresentationPersistenceSubmitStatus::ReplacedPending;
			}
		}
		try
		{
			Impl::WorkItem item;
			item.operation = PresentationPersistenceOperation::Save;
			item.save = std::move(request);
			impl_->queue.push_back(std::move(item));
			++impl_->diagnostics.accepted;
		}
		catch (...) { return PresentationPersistenceSubmitStatus::Invalid; }
		impl_->condition.notify_one();
		return PresentationPersistenceSubmitStatus::Accepted;
	}

	PresentationPersistenceSubmitStatus PresentationAutoSaveService::SubmitLoad(
		PresentationLoadRequest request) noexcept
	{
		if (!ValidatePresentationTarget(request.target))
			return PresentationPersistenceSubmitStatus::Invalid;
		std::scoped_lock lock(impl_->mutex);
		if (!impl_->accepting) return PresentationPersistenceSubmitStatus::Closed;
		try
		{
			Impl::WorkItem item;
			item.operation = PresentationPersistenceOperation::Load;
			item.load = std::move(request);
			impl_->queue.push_back(std::move(item));
			++impl_->diagnostics.accepted;
		}
		catch (...) { return PresentationPersistenceSubmitStatus::Invalid; }
		impl_->condition.notify_one();
		return PresentationPersistenceSubmitStatus::Accepted;
	}

	bool PresentationAutoSaveService::TryTakeCompletion(
		PresentationPersistenceCompletion& completion) noexcept
	{
		std::scoped_lock lock(impl_->mutex);
		if (impl_->completions.empty()) return false;
		completion = std::move(impl_->completions.front());
		impl_->completions.pop_front();
		return true;
	}

	void PresentationAutoSaveService::CloseAndDrain() noexcept
	{
		{
			std::scoped_lock lock(impl_->mutex);
			impl_->accepting = false;
		}
		impl_->condition.notify_all();
		if (impl_->worker.joinable()) impl_->worker.join();
	}

	std::string PresentationAutoSaveService::SessionId() const
	{
		std::scoped_lock lock(impl_->mutex);
		return impl_->sessionId;
	}

	PresentationPersistenceDiagnostics PresentationAutoSaveService::Diagnostics() const noexcept
	{
		std::scoped_lock lock(impl_->mutex);
		return impl_->diagnostics;
	}

	void SetPresentationAutoSaveTestFaultInjection(
		const PresentationAutoSaveTestFaultInjection& injection) noexcept
	{
		std::scoped_lock lock(testFaultMutex);
		testFaults = injection;
	}

	void ResetPresentationAutoSaveTestFaultInjection() noexcept
	{
		SetPresentationAutoSaveTestFaultInjection({});
	}
}
