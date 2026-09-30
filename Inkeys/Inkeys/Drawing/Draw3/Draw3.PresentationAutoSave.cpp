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
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
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
		using draw3::uink::Draw3UInkCanvasSnapshot;
		using draw3::uink::Draw3UInkExportSnapshot;
		using draw3::uink::ExportDraw3SnapshotToUInk;
		using draw3::uink::FormatUInkGuid;
		using draw3::uink::ImportApplicationOwnedPresentation;
		using draw3::uink::MergeDraw3UInkCanvasTail;
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

		std::string FoldAsciiKey(std::string value)
		{
			for (char& character : value)
				if (character >= 'A' && character <= 'Z')
					character = static_cast<char>(character - 'A' + 'a');
			return value;
		}

		bool IsSafeRelativePath(const std::string& value,
			const std::string& fileGuid, int schemaVersion)
		{
			const std::string legacy = "files/" + fileGuid + ".uink";
			if (value == legacy) return true;
			if (schemaVersion != 2) return false;
			const auto logicalGuid = ParseUInkGuid(fileGuid);
			if (!logicalGuid || FormatUInkGuid(*logicalGuid) != fileGuid) return false;
			const std::string prefix = "files/" + fileGuid + "_";
			if (!value.starts_with(prefix) || !value.ends_with(".uink") ||
				value.size() != prefix.size() + 36 + 5) return false;
			const std::string transactionGuid = value.substr(prefix.size(), 36);
			const auto parsed = ParseUInkGuid(transactionGuid);
			return parsed && FormatUInkGuid(*parsed) == transactionGuid;
		}

		bool DecodeEntry(const Json::Value& value, int schemaVersion,
			IndexEntry& entry)
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
				!IsSafeRelativePath(entry.relativePath, entry.fileGuid, schemaVersion) ||
				entry.mutationRevision == 0 ||
				(entry.bindingMode != "slide-id" && entry.bindingMode != "page-index") ||
				!DecodeRevision(value["sourceRevision"], entry.sourceRevision)) return false;
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
				!root["schemaVersion"].isInt() ||
				(root["schemaVersion"].asInt() != 1 &&
					root["schemaVersion"].asInt() != 2) ||
				!root["scenario"].isString() || root["scenario"].asString() != "presentation" ||
				!root["entries"].isArray()) return IndexState::Invalid;
			const int schemaVersion = root["schemaVersion"].asInt();
			std::set<std::string> sources;
			std::set<std::string> keys;
			std::set<std::string> fileGuids;
			std::set<std::string> paths;
			for (const Json::Value& value : root["entries"])
			{
				IndexEntry entry;
				if (!DecodeEntry(value, schemaVersion, entry) ||
					!sources.insert(entry.sourceIdentity).second ||
					!keys.insert(entry.presentationKey).second ||
					!fileGuids.insert(FoldAsciiKey(entry.fileGuid)).second ||
					!paths.insert(FoldAsciiKey(entry.relativePath)).second)
					return IndexState::Invalid;
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
			document["schemaVersion"] = 2;
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

		struct VersionedUInkSave
		{
			UInkSaveStatus status = UInkSaveStatus::IoError;
			std::optional<UInkSourceRevision> revision;
			std::string relativePath;
		};

		VersionedUInkSave SaveVersionedUInk(const std::wstring& root,
			const std::string& fileGuid, const UInkEditingSession& session)
		{
			UInkSaveOptions options;
			options.mode = UInkSaveMode::CreateNewLogicalFileWithIdentity;
			for (int attempt = 0; attempt != 8; ++attempt)
			{
				const auto transactionGuid = CreateUInkGuid();
				if (!transactionGuid) return {};
				VersionedUInkSave result;
				result.relativePath = "files/" + fileGuid + "_" +
					FormatUInkGuid(*transactionGuid) + ".uink";
				const auto saved = SaveUInkFile(JoinPath(root,
					WidenAscii(result.relativePath)), session, options);
				if (saved.status == UInkSaveStatus::SourceChanged) continue;
				result.status = saved.status;
				result.revision = saved.revision;
				return result;
			}
			return { UInkSaveStatus::SourceChanged };
		}

		std::string BindingModeName(Bridge::SlideBindingMode mode) noexcept
		{
			return mode == Bridge::SlideBindingMode::StableSlideId
				? "slide-id" : "page-index";
		}

		using PendingKey = std::tuple<PresentationStorageTrack,
			Bridge::SlideBindingMode, std::string, std::string, std::string,
			std::uint64_t>;
		using PendingEntries = std::map<PendingKey, IndexEntry>;

		PendingKey MakePendingKey(PresentationStorageTrack track,
			const PresentationSaveRequest& request)
		{
			return { track, request.target.bindingMode,
				request.target.sourceIdentity, FormatUInkGuid(request.snapshot.fileGuid),
				FormatUInkGuid(request.snapshot.workspaceGuid), request.slotGeneration };
		}

		struct IndexedTrack
		{
			PresentationStorageTrack track = PresentationStorageTrack::Unresolved;
			std::wstring root;
			std::vector<IndexEntry> entries;
			bool primaryValid = false;
		};

		struct StorageSelection
		{
			std::array<IndexedTrack, 3> tracks;
			std::size_t selected = 0;
			IndexedTrack& Current() noexcept { return tracks[selected]; }
			const IndexedTrack& Current() const noexcept { return tracks[selected]; }
		};

		bool SelectStorageTrack(const std::wstring& baseRoot,
			const Bridge::PresentationTarget& target, const std::string& key,
			const std::string& sessionId, const PendingEntries& pending,
			StorageSelection& selection, PresentationPersistenceStatus& failure)
		{
			selection.tracks = { IndexedTrack{ PresentationStorageTrack::Base, baseRoot },
				IndexedTrack{ PresentationStorageTrack::SlideIdSidecar,
					JoinPath(baseRoot, L"slide-id") },
				IndexedTrack{ PresentationStorageTrack::PageIndexSidecar,
					JoinPath(baseRoot, L"page-index") } };
			std::array<const IndexEntry*, 3> found = {};
			for (std::size_t index = 0; index < selection.tracks.size(); ++index)
			{
				auto& track = selection.tracks[index];
				if (!LoadIndexWithBackup(track.root, track.entries, track.primaryValid))
				{
					failure = PresentationPersistenceStatus::IoError;
					return false;
				}
				for (const auto& entry : track.entries)
				{
					if ((index == 1 && entry.bindingMode != "slide-id") ||
						(index == 2 && entry.bindingMode != "page-index"))
					{
						failure = PresentationPersistenceStatus::IoError;
						return false;
					}
					if ((entry.sourceIdentity == target.sourceIdentity &&
							entry.presentationKey != key) ||
						(entry.presentationKey == key &&
							entry.sourceIdentity != target.sourceIdentity))
					{
						failure = PresentationPersistenceStatus::CrossProcessConflictDeferred;
						return false;
					}
					if (entry.sourceIdentity == target.sourceIdentity) found[index] = &entry;
				}
			}
			if (found[0] && ((found[1] && found[0]->bindingMode == "slide-id") ||
				(found[2] && found[0]->bindingMode == "page-index")))
			{
				failure = PresentationPersistenceStatus::IoError;
				return false; // 两处同 mode 不能猜哪个是权威。
			}
			bool pendingBaseStable = false;
			bool pendingBaseFallback = false;
			bool pendingSlideSidecar = false;
			bool pendingPageSidecar = false;
			for (const auto& [pendingKey, entry] : pending)
			{
				if (entry.sourceIdentity != target.sourceIdentity ||
					entry.sessionId != sessionId) continue;
				if (entry.presentationKey != key)
				{
					failure = PresentationPersistenceStatus::CrossProcessConflictDeferred;
					return false;
				}
				const auto track = std::get<0>(pendingKey);
				if (track == PresentationStorageTrack::Base)
				{
					pendingBaseStable |= entry.bindingMode == "slide-id";
					pendingBaseFallback |= entry.bindingMode == "page-index";
				}
				else if (track == PresentationStorageTrack::SlideIdSidecar)
				{
					if (entry.bindingMode != "slide-id")
					{
						failure = PresentationPersistenceStatus::IoError;
						return false;
					}
					pendingSlideSidecar = true;
				}
				else if (track == PresentationStorageTrack::PageIndexSidecar)
				{
					if (entry.bindingMode != "page-index")
					{
						failure = PresentationPersistenceStatus::IoError;
						return false;
					}
					pendingPageSidecar = true;
				}
			}
			if ((pendingBaseStable && pendingBaseFallback) ||
				((pendingBaseStable || (found[0] && found[0]->bindingMode == "slide-id")) &&
					(found[1] || pendingSlideSidecar)) ||
				((pendingBaseFallback || (found[0] && found[0]->bindingMode == "page-index")) &&
					(found[2] || pendingPageSidecar)) ||
				(found[0] && ((pendingBaseStable && found[0]->bindingMode != "slide-id") ||
					(pendingBaseFallback && found[0]->bindingMode != "page-index"))))
			{
				failure = PresentationPersistenceStatus::IoError;
				return false;
			}
			const bool stable = target.bindingMode == Bridge::SlideBindingMode::StableSlideId;
			const std::size_t sidecar = stable ? 1 : 2;
			if (found[0] && found[0]->bindingMode == BindingModeName(target.bindingMode))
				selection.selected = 0;
			else if (found[sidecar] || (stable ? pendingSlideSidecar : pendingPageSidecar))
				selection.selected = sidecar;
			else if (found[0] || (stable ? pendingBaseFallback : pendingBaseStable))
				selection.selected = sidecar;
			else selection.selected = 0;
			return true;
		}

		bool ConflictsWithOtherLogicalFile(const StorageSelection& selection,
			const PendingEntries& pending, const PresentationSaveRequest& request)
		{
			const std::string fileGuid = FormatUInkGuid(request.snapshot.fileGuid);
			const std::string workspaceGuid = FormatUInkGuid(request.snapshot.workspaceGuid);
			for (std::size_t index = 0; index < selection.tracks.size(); ++index)
				for (const auto& entry : selection.tracks[index].entries)
				{
					if (index == selection.selected &&
						entry.sourceIdentity == request.target.sourceIdentity) continue;
					if (FoldAsciiKey(entry.fileGuid) == fileGuid ||
						FoldAsciiKey(entry.workspaceGuid) == workspaceGuid) return true;
				}
			for (const auto& [key, entry] : pending)
			{
				// 同 GUID 的旧槽未发布版本仍是独立身份，不能跨代继承。
				const bool sameLogicalFile = std::get<0>(key) ==
					selection.Current().track &&
					std::get<1>(key) == request.target.bindingMode &&
					entry.sourceIdentity == request.target.sourceIdentity &&
					FoldAsciiKey(entry.fileGuid) == fileGuid &&
					FoldAsciiKey(entry.workspaceGuid) == workspaceGuid &&
					std::get<5>(key) == request.slotGeneration;
				if (!sameLogicalFile && (FoldAsciiKey(entry.fileGuid) == fileGuid ||
					FoldAsciiKey(entry.workspaceGuid) == workspaceGuid)) return true;
			}
			return false;
		}

		bool CompatibleSlideIdSet(const std::vector<std::int32_t>& known,
			const std::vector<std::int32_t>& active) noexcept
		{
			if (known.empty() || active.empty()) return known.empty() && active.empty();
			// 同一路径新增幻灯片时，至少一个稳定 SlideID 连续即可保持原文件的结束页。
			return std::any_of(active.begin(), active.end(), [&](auto id)
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
			return !target.key.IsZero() && !target.sourceIdentity.empty() &&
				Bridge::ValidPresentationPage(target);
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
						: request.snapshot.activeCanvases.size() < request.target.totalPages))
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
				if (active.size() > Bridge::PresentationDocumentPageCount(request.target) ||
					(request.target.pageKind == Bridge::PresentationPageKind::EndScreen &&
						active.size() != Bridge::PresentationDocumentPageCount(request.target)))
					return false;
				if (request.clearPageGuid.has_value() !=
					request.clearIntervalOrdinal.has_value()) return false;
				bool clearPageFound = !request.clearPageGuid;
				bool clearOrdinalMatched = !request.clearIntervalOrdinal;
				for (std::size_t index = 0; index < active.size(); ++index)
				{
					const auto& canvas = active[index];
					const bool endScreen = index == request.target.totalPages;
					const auto pageKind = draw3::uink::InkeysPageKind(canvas.extra);
					if (canvas.pageIndex != index || canvas.pageNumber != index + 1 ||
						!draw3::uink::HasInkeysBindingExtra(canvas.extra, importMode) ||
						pageKind == draw3::uink::UInkInkeysPageKind::Invalid ||
						(pageKind == draw3::uink::UInkInkeysPageKind::EndScreen) != endScreen ||
						(stable && !endScreen && (!canvas.slideId ||
							*canvas.slideId != request.target.slideIds[index])) ||
						(endScreen && canvas.slideId) || (!stable && canvas.slideId)) return false;
					clearPageFound = clearPageFound ||
						canvas.pageGuid == *request.clearPageGuid;
					if (request.clearPageGuid && canvas.pageGuid == *request.clearPageGuid)
						clearOrdinalMatched = canvas.intervalOrdinal ==
							*request.clearIntervalOrdinal;
				}
				if (!request.snapshot.retainedCanvases.empty())
					for (const auto& canvas : request.snapshot.retainedCanvases)
					{
						if (!canvas.retained || !canvas.slideId ||
							!draw3::uink::HasInkeysPageStateExtra(canvas.extra, true)) return false;
						clearPageFound = clearPageFound ||
							canvas.pageGuid == *request.clearPageGuid;
						if (request.clearPageGuid && canvas.pageGuid == *request.clearPageGuid)
							clearOrdinalMatched = canvas.intervalOrdinal ==
								*request.clearIntervalOrdinal;
					}
				return clearPageFound && clearOrdinalMatched;
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
			expectation.allowEndScreen = true;
			return expectation;
		}

		std::optional<Draw3UInkExportSnapshot> MergePresentationSnapshot(
			const PresentationSaveRequest& request,
			const Draw3UInkExportSnapshot* canonical)
		{
			Draw3UInkExportSnapshot merged = request.snapshot;
			const auto canonicalActive = canonical
				? (canonical->activeCanvases.empty()
					? std::span<const Draw3UInkCanvasSnapshot>(canonical->canvases)
					: std::span<const Draw3UInkCanvasSnapshot>(canonical->activeCanvases))
				: std::span<const Draw3UInkCanvasSnapshot>{};
			auto findCanonical = [&](const Draw3UInkCanvasSnapshot& requested)
				-> const Draw3UInkCanvasSnapshot*
			{
				for (const auto& canvas : canonicalActive)
					if (canvas.pageGuid == requested.pageGuid) return &canvas;
				if (canonical)
					for (const auto& canvas : canonical->retainedCanvases)
						if (canvas.pageGuid == requested.pageGuid) return &canvas;
				return nullptr;
			};
			bool sealed = !request.clearPageGuid;
			auto mergeCanvases = [&](std::vector<Draw3UInkCanvasSnapshot>& canvases)
				-> bool
			{
				for (auto& canvas : canvases)
				{
					const bool seal = request.clearPageGuid &&
						canvas.pageGuid == *request.clearPageGuid;
					auto value = MergeDraw3UInkCanvasTail(
						findCanonical(canvas), canvas, seal);
					if (!value) return false;
					canvas = std::move(*value);
					sealed = sealed || seal;
				}
				return true;
			};
			if (merged.activeCanvases.empty() && merged.retainedCanvases.empty())
			{
				if (!mergeCanvases(merged.canvases)) return std::nullopt;
			}
			else
			{
				if (!mergeCanvases(merged.activeCanvases) ||
					!mergeCanvases(merged.retainedCanvases)) return std::nullopt;
				merged.canvases = merged.activeCanvases;
			}
			if (!sealed) return std::nullopt;
			return merged;
		}

		PresentationPersistenceStatus SavePresentation(const std::wstring& autoSaveRoot,
			const std::string& sessionId, const PresentationSaveRequest& request,
			PendingEntries& pendingIndexEntries,
			PresentationStorageTrack& selectedTrack)
		{
			const PresentationAutoSaveTestFaultInjection faults = SnapshotTestFaults();
			if (faults.writeDelayMilliseconds != 0)
				Sleep(faults.writeDelayMilliseconds);
			const std::wstring baseRoot = JoinPath(autoSaveRoot, L"presentation");
			NamedMutexGuard mutex;
			if (!mutex.Acquire(baseRoot)) return PresentationPersistenceStatus::IoError;
			const std::string key = FormatPresentationKey(request.target.key);
			StorageSelection selection;
			PresentationPersistenceStatus selectFailure = PresentationPersistenceStatus::IoError;
			if (!SelectStorageTrack(baseRoot, request.target, key, sessionId,
				pendingIndexEntries, selection, selectFailure)) return selectFailure;
			selectedTrack = selection.Current().track;
			if (ConflictsWithOtherLogicalFile(selection, pendingIndexEntries, request))
				return PresentationPersistenceStatus::SourceChanged;
			const std::wstring& root = selection.Current().root;
			if (!EnsureDirectory(JoinPath(root, L"files")))
				return PresentationPersistenceStatus::IoError;
			auto& entries = selection.Current().entries;
			const bool primaryValid = selection.Current().primaryValid;
			const PendingKey pendingKey = MakePendingKey(selectedTrack, request);
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
				const auto pending = pendingIndexEntries.find(pendingKey);
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

			struct PreparedDocument
			{
				draw3::uink::UInkDocument document;
				std::vector<std::int32_t> knownSlideIds;
			};
			auto buildDocument = [&](const Draw3UInkExportSnapshot* canonical,
				const std::vector<std::int32_t>& priorKnown)
				-> std::optional<PreparedDocument>
			{
				auto merged = MergePresentationSnapshot(request, canonical);
				if (!merged) return std::nullopt;
				std::vector<std::int32_t> known = priorKnown;
				std::set<std::int32_t> membership(known.begin(), known.end());
				// 保持旧 known→当前 target→实际写出页的索引顺序，集合只负责低成本判重。
				for (const auto id : request.target.slideIds)
					if (membership.insert(id).second) known.push_back(id);
				std::set<std::int32_t> written;
				auto include = [&](std::span<const Draw3UInkCanvasSnapshot> canvases)
				{
					for (const auto& canvas : canvases)
					{
						if (!canvas.slideId) continue; // EndScreen 不属于 Office SlideID 集合。
						const std::int32_t id = *canvas.slideId;
						if (id <= 0 || !written.insert(id).second) return false;
						if (membership.insert(id).second)
							known.push_back(id);
					}
					return true;
				};
				const auto& active = merged->activeCanvases.empty() &&
					merged->retainedCanvases.empty()
					? merged->canvases : merged->activeCanvases;
				if (!include(std::span<const Draw3UInkCanvasSnapshot>(active)) ||
					!include(std::span<const Draw3UInkCanvasSnapshot>(
						merged->retainedCanvases)) ||
					known.size() > Bridge::kMaximumPresentationPages) return std::nullopt;
				auto exported = ExportDraw3SnapshotToUInk(*merged);
				if (!exported.document) return std::nullopt;
				return PreparedDocument{ std::move(*exported.document), std::move(known) };
			};
			if (found == entries.end())
			{
				IndexEntry entry;
				entry.sourceIdentity = request.target.sourceIdentity;
				entry.presentationKey = key;
				entry.sessionId = sessionId;
				entry.fileGuid = FormatUInkGuid(request.snapshot.fileGuid);
				entry.workspaceGuid = FormatUInkGuid(request.snapshot.workspaceGuid);
				entry.bindingMode = BindingModeName(request.target.bindingMode);
				entry.processLocal = request.target.processLocalIdentity;
				entry.bindingRevision = request.target.bindingRevision;
				entry.mutationRevision = request.mutationRevision;
				entry.slideIds = request.target.slideIds;
				// 无索引的旧版固定路径只作为严格校验后的 canonical 来源，绝不原位覆盖。
				const std::wstring legacyPath = JoinPath(root,
					WidenAscii("files/" + entry.fileGuid + ".uink"));
				const auto pending = pendingIndexEntries.find(pendingKey);
				const IndexEntry* canonicalEntry = &entry;
				std::wstring canonicalPath = legacyPath;
				if (pending != pendingIndexEntries.end())
				{
					if (pending->second.sessionId != sessionId ||
						pending->second.presentationKey != key ||
						pending->second.fileGuid != entry.fileGuid ||
						pending->second.workspaceGuid != entry.workspaceGuid ||
						!IsSafeRelativePath(pending->second.relativePath,
							entry.fileGuid, 2))
						return PresentationPersistenceStatus::SourceChanged;
					canonicalEntry = &pending->second;
					canonicalPath = JoinPath(root,
						WidenAscii(canonicalEntry->relativePath));
				}
				std::optional<UInkEditingSession> session;
				if (PathExists(canonicalPath))
				{
					const auto existing = ReadUInkFile(canonicalPath);
					const auto imported = existing.document
						? ImportApplicationOwnedPresentation(*existing.document,
							MakeExpectation(*canonicalEntry, request.target))
						: draw3::uink::Draw3UInkImportResult{};
					if (existing.status != UInkReadStatus::Complete ||
						!existing.document || !existing.sourceRevision ||
						(pending != pendingIndexEntries.end() &&
							*existing.sourceRevision != canonicalEntry->sourceRevision) ||
						(existing.provenance.containsInvalidCompleteBlocks ||
							existing.provenance.contentSequenceRecovered) ||
						existing.document->header.guid.Bytes() != request.snapshot.fileGuid.Bytes() ||
						!imported.snapshot)
						return PresentationPersistenceStatus::SourceChanged;
					auto outputDocument = buildDocument(
						&*imported.snapshot, canonicalEntry->slideIds);
					if (!outputDocument) return PresentationPersistenceStatus::Invalid;
					auto existingSession = CreateUInkEditingSession(existing,
						canonicalEntry->bindingMode == "slide-id"
							? draw3::uink::UInkEditingSource::ApplicationOwned
							: draw3::uink::UInkEditingSource::ApplicationOwnedPrivateWorkspace);
					if (!existingSession) return PresentationPersistenceStatus::Invalid;
					existingSession->document = std::move(outputDocument->document);
					entry.slideIds = std::move(outputDocument->knownSlideIds);
					session = std::move(*existingSession);
				}
				else
				{
					if (pending != pendingIndexEntries.end())
						return PresentationPersistenceStatus::SourceChanged;
					auto outputDocument = buildDocument(nullptr, entry.slideIds);
					if (!outputDocument) return PresentationPersistenceStatus::Invalid;
					session.emplace();
					session->document = std::move(outputDocument->document);
					entry.slideIds = std::move(outputDocument->knownSlideIds);
				}
				const auto saved = SaveVersionedUInk(root, entry.fileGuid, *session);
				if (saved.status != UInkSaveStatus::Committed || !saved.revision)
					return saved.status == UInkSaveStatus::SourceChanged
						? PresentationPersistenceStatus::SourceChanged
						: PresentationPersistenceStatus::IoError;
				entry.relativePath = saved.relativePath;
				entry.sourceRevision = *saved.revision;
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
				const auto indexedFileGuid = ParseUInkGuid(found->fileGuid);
				const auto indexedWorkspaceGuid = ParseUInkGuid(found->workspaceGuid);
				if (!indexedFileGuid || *indexedFileGuid != request.snapshot.fileGuid ||
					!indexedWorkspaceGuid || *indexedWorkspaceGuid !=
						request.snapshot.workspaceGuid ||
					(!bindingUpgrade && (found->bindingMode !=
						BindingModeName(request.target.bindingMode) ||
						!CompatibleSlideIdSet(found->slideIds, request.target.slideIds))))
					return PresentationPersistenceStatus::SourceChanged;
				const std::wstring path = JoinPath(root, WidenAscii(found->relativePath));
				const auto read = ReadUInkFile(path);
				const auto imported = read.document
					? ImportApplicationOwnedPresentation(*read.document,
						MakeExpectation(*found, request.target))
					: draw3::uink::Draw3UInkImportResult{};
				if (read.status != UInkReadStatus::Complete || !read.document ||
					(read.provenance.containsInvalidCompleteBlocks ||
						read.provenance.contentSequenceRecovered) ||
					!read.sourceRevision || *read.sourceRevision != found->sourceRevision ||
					!imported.snapshot)
					return PresentationPersistenceStatus::SourceChanged;
				auto outputDocument = buildDocument(
					&*imported.snapshot, found->slideIds);
				if (!outputDocument) return PresentationPersistenceStatus::Invalid;
				auto writtenKnown = std::move(outputDocument->knownSlideIds);
				auto session = CreateUInkEditingSession(read,
					found->bindingMode == "slide-id"
						? draw3::uink::UInkEditingSource::ApplicationOwned
						: draw3::uink::UInkEditingSource::ApplicationOwnedPrivateWorkspace);
				if (!session) return PresentationPersistenceStatus::Invalid;
				session->document = std::move(outputDocument->document);
				// 旧 v1 路径按原样读取；新 v2 文件和索引使用规范 GUID 文本。
				const std::string canonicalFileGuid =
					FormatUInkGuid(request.snapshot.fileGuid);
				const auto saved = SaveVersionedUInk(root, canonicalFileGuid, *session);
				if (saved.status != UInkSaveStatus::Committed || !saved.revision)
					return saved.status == UInkSaveStatus::SourceChanged
						? PresentationPersistenceStatus::SourceChanged
						: PresentationPersistenceStatus::IoError;
				found->fileGuid = canonicalFileGuid;
				found->workspaceGuid = FormatUInkGuid(request.snapshot.workspaceGuid);
				found->relativePath = saved.relativePath;
				found->sourceRevision = *saved.revision;
				found->bindingMode = BindingModeName(request.target.bindingMode);
				found->slideIds = std::move(writtenKnown);
				found->bindingRevision = request.target.bindingRevision;
				found->processLocal = request.target.processLocalIdentity;
				found->mutationRevision = request.mutationRevision;
			}
			if (faults.afterUInkCommittedEvent && faults.continueIndexCommitEvent)
			{
				if (!SetEvent(static_cast<HANDLE>(faults.afterUInkCommittedEvent)) ||
					WaitForSingleObject(static_cast<HANDLE>(
						faults.continueIndexCommitEvent), 30000) != WAIT_OBJECT_0)
					return PresentationPersistenceStatus::IoError;
			}
			if (faults.failIndexCommit || !CommitIndex(root, entries, primaryValid))
			{
				const auto written = std::find_if(entries.begin(), entries.end(),
					[&](const IndexEntry& entry)
					{ return entry.sourceIdentity == request.target.sourceIdentity; });
				if (written != entries.end())
					pendingIndexEntries[pendingKey] = *written;
				return PresentationPersistenceStatus::IoError;
			}
			pendingIndexEntries.erase(pendingKey);
			// 不按路径回收旧版本：外部进程可在校验后替换目录或文件，误删未知数据。
			return PresentationPersistenceStatus::Committed;
		}

		PresentationPersistenceCompletion LoadPresentation(const std::wstring& autoSaveRoot,
			const std::string& sessionId, const PresentationLoadRequest& request,
			const PendingEntries& pendingIndexEntries,
			PresentationStorageTrack& selectedTrack)
		{
			PresentationPersistenceCompletion completion;
			completion.operation = PresentationPersistenceOperation::Load;
			completion.target = request.target;
			completion.slotGeneration = request.slotGeneration;
			completion.loadKind = request.kind;
			completion.pageGuid = request.pageGuid;
			completion.intervalOrdinal = request.intervalOrdinal;
			const std::wstring baseRoot = JoinPath(autoSaveRoot, L"presentation");
			NamedMutexGuard mutex;
			if (!mutex.Acquire(baseRoot))
			{
				completion.status = PresentationPersistenceStatus::IoError;
				return completion;
			}
			const std::string key = FormatPresentationKey(request.target.key);
			StorageSelection selection;
			PresentationPersistenceStatus selectFailure = PresentationPersistenceStatus::IoError;
			if (!SelectStorageTrack(baseRoot, request.target, key, sessionId,
				pendingIndexEntries, selection, selectFailure))
			{
				completion.status = selectFailure;
				return completion;
			}
			selectedTrack = selection.Current().track;
			completion.storageTrack = selectedTrack;
			const std::wstring& root = selection.Current().root;
		auto& entries = selection.Current().entries;
			auto found = std::find_if(entries.begin(), entries.end(),
				[&](const IndexEntry& entry)
				{ return entry.sourceIdentity == request.target.sourceIdentity; });
			auto pending = pendingIndexEntries.end();
			for (auto iterator = pendingIndexEntries.begin();
				iterator != pendingIndexEntries.end(); ++iterator)
			{
				const auto& pendingKey = iterator->first;
				if (std::get<0>(pendingKey) != selectedTrack ||
					std::get<1>(pendingKey) != request.target.bindingMode ||
					std::get<2>(pendingKey) != request.target.sourceIdentity ||
					std::get<5>(pendingKey) != request.slotGeneration) continue;
				if (found != entries.end())
				{
					// v1 GUID 大小写不改变身份；同代 pending 按解析值与旧索引比对。
					const auto pendingFileGuid =
						ParseUInkGuid(iterator->second.fileGuid);
					const auto pendingWorkspaceGuid =
						ParseUInkGuid(iterator->second.workspaceGuid);
					const auto indexedFileGuid = ParseUInkGuid(found->fileGuid);
					const auto indexedWorkspaceGuid = ParseUInkGuid(found->workspaceGuid);
					if (!pendingFileGuid || !pendingWorkspaceGuid ||
						!indexedFileGuid || !indexedWorkspaceGuid ||
						*pendingFileGuid != *indexedFileGuid ||
						*pendingWorkspaceGuid != *indexedWorkspaceGuid) continue;
				}
				if (pending != pendingIndexEntries.end())
				{
					completion.status = PresentationPersistenceStatus::IoError;
					return completion; // 同槽存在多个未发布身份时不得猜测最新。
				}
				pending = iterator;
			}
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
					!CompatibleSlideIdSet(found->slideIds, request.target.slideIds))))
			{
				completion.status = PresentationPersistenceStatus::CrossProcessConflictDeferred;
				return completion;
			}
			const std::wstring path = JoinPath(root, WidenAscii(found->relativePath));
			const auto read = ReadUInkFile(path);
			if (read.status != UInkReadStatus::Complete || !read.document ||
				(read.provenance.containsInvalidCompleteBlocks ||
					read.provenance.contentSequenceRecovered) ||
				!read.sourceRevision || *read.sourceRevision != found->sourceRevision)
			{
				completion.status = PresentationPersistenceStatus::SourceChanged;
				return completion;
			}
			const auto imported = ImportApplicationOwnedPresentation(*read.document,
				MakeExpectation(*found, request.target));
			const auto indexedWorkspaceGuid = ParseUInkGuid(found->workspaceGuid);
			if (!imported.snapshot || !indexedWorkspaceGuid ||
				imported.snapshot->workspaceGuid != *indexedWorkspaceGuid)
			{
				completion.status = PresentationPersistenceStatus::Invalid;
				return completion;
			}
			Draw3UInkExportSnapshot projected = *imported.snapshot;
			if (request.kind == PresentationLoadKind::PreviousInterval)
			{
				if (!request.pageGuid)
				{
					completion.status = PresentationPersistenceStatus::Invalid;
					return completion;
				}
				const auto& active = projected.activeCanvases.empty()
					? projected.canvases : projected.activeCanvases;
				const Draw3UInkCanvasSnapshot* source = nullptr;
				for (const auto& canvas : active)
					if (canvas.pageGuid == *request.pageGuid) source = &canvas;
				if (!source)
					for (const auto& canvas : projected.retainedCanvases)
						if (canvas.pageGuid == *request.pageGuid) source = &canvas;
				const auto interval = source
					? draw3::uink::ProjectDraw3UInkCanvasInterval(
						*source, request.intervalOrdinal) : std::nullopt;
				if (!interval)
				{
					completion.status = PresentationPersistenceStatus::Invalid;
					return completion;
				}
				projected.canvases = { *interval };
				projected.activeCanvases = { *interval };
				projected.retainedCanvases.clear();
			}
			else
			{
				auto projectAll = [](std::vector<Draw3UInkCanvasSnapshot>& canvases)
					-> bool
				{
					for (auto& canvas : canvases)
					{
						auto current = draw3::uink::ProjectDraw3UInkCanvasInterval(
							canvas, canvas.intervalOrdinal);
						if (!current) return false;
						canvas = std::move(*current);
					}
					return true;
				};
				if (projected.activeCanvases.empty() &&
					projected.retainedCanvases.empty())
				{
					if (!projectAll(projected.canvases))
					{
						completion.status = PresentationPersistenceStatus::Invalid;
						return completion;
					}
				}
				else
				{
					if (!projectAll(projected.activeCanvases) ||
						!projectAll(projected.retainedCanvases))
					{
						completion.status = PresentationPersistenceStatus::Invalid;
						return completion;
					}
					projected.canvases = projected.activeCanvases;
				}
			}
			completion.mutationRevision = found->mutationRevision;
			completion.fileGuid = *ParseUInkGuid(found->fileGuid);
			completion.loadedSnapshot = std::make_shared<
				const draw3::uink::Draw3UInkExportSnapshot>(std::move(projected));
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
		PendingEntries pendingIndexEntries;
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
						completion.slotGeneration = item.save.slotGeneration;
						completion.fileGuid = item.save.snapshot.fileGuid;
						completion.mutationRevision = item.save.mutationRevision;
						completion.clearPageGuid = item.save.clearPageGuid;
						completion.clearIntervalOrdinal =
							item.save.clearIntervalOrdinal;
					}
					else
					{
						completion.target = item.load.target;
						completion.slotGeneration = item.load.slotGeneration;
						// 失败回执保留区间加载身份，避免清屏撤销一直等待。
						completion.loadKind = item.load.kind;
						completion.pageGuid = item.load.pageGuid;
						completion.intervalOrdinal = item.load.intervalOrdinal;
					}
					try
					{
						// 任一工作项异常都必须转换为终态，不能逃出 jthread 触发 terminate。
						if (SnapshotTestFaults().throwWorkerOperation)
							throw std::runtime_error("injected presentation worker exception");
						if (item.operation == PresentationPersistenceOperation::Save)
							completion.status = SavePresentation(
								autoSaveRoot, sessionId, item.save, pendingIndexEntries,
								completion.storageTrack);
						else
							completion = LoadPresentation(
								autoSaveRoot, sessionId, item.load, pendingIndexEntries,
								completion.storageTrack);
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
		for (auto iterator = impl_->queue.rbegin();
			!request.clearPageGuid && iterator != impl_->queue.rend(); ++iterator)
		{
			if (iterator->operation == PresentationPersistenceOperation::Save &&
				iterator->save.target.key == request.target.key &&
				iterator->save.clearPageGuid) break; // 同文稿任一轨的 Clear 边界均保持 FIFO。
			if (iterator->operation == PresentationPersistenceOperation::Save &&
				iterator->save.target.key == request.target.key &&
				iterator->save.target.sourceIdentity == request.target.sourceIdentity &&
				iterator->save.target.bindingMode == request.target.bindingMode &&
				iterator->save.snapshot.fileGuid == request.snapshot.fileGuid &&
				iterator->save.snapshot.workspaceGuid == request.snapshot.workspaceGuid &&
				iterator->save.slotGeneration == request.slotGeneration)
			{
				// 普通 tail 只在同轨、同文件、同槽代次内合并。
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
		if (!ValidatePresentationTarget(request.target) ||
			(request.kind == PresentationLoadKind::Current &&
				(request.pageGuid || request.intervalOrdinal != 0)) ||
			(request.kind == PresentationLoadKind::PreviousInterval &&
				(!request.pageGuid || request.intervalOrdinal >= UINT32_MAX)))
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
