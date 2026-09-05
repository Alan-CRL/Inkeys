module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

module draw3.uink_file;

namespace draw3::uink
{
	namespace
	{
		std::atomic<uint64_t> gFailWriteAfterBytes{ UINT64_MAX };
		std::atomic<bool> gFailFlush{ false };
		std::atomic<bool> gFailSelfValidation{ false };
		std::atomic<bool> gFailCommit{ false };
		std::atomic<bool> gFailRollback{ false };
		std::atomic<bool> gFailCommittedRevisionValidation{ false };

		UInkFileTestFaultInjection SnapshotTestFaults() noexcept
		{
			return {
				gFailWriteAfterBytes.load(std::memory_order_relaxed),
				gFailFlush.load(std::memory_order_relaxed),
				gFailSelfValidation.load(std::memory_order_relaxed),
				gFailCommit.load(std::memory_order_relaxed),
				gFailRollback.load(std::memory_order_relaxed),
				gFailCommittedRevisionValidation.load(std::memory_order_relaxed)
			};
		}

		class UniqueHandle
		{
		public:
			UniqueHandle() noexcept = default;
			explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
			~UniqueHandle()
			{
				if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) CloseHandle(value_);
			}
			UniqueHandle(const UniqueHandle&) = delete;
			UniqueHandle& operator=(const UniqueHandle&) = delete;
			UniqueHandle(UniqueHandle&& other) noexcept : value_(other.Release()) {}
			UniqueHandle& operator=(UniqueHandle&& other) noexcept
			{
				if (this != &other)
				{
					if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) CloseHandle(value_);
					value_ = other.Release();
				}
				return *this;
			}
			HANDLE Get() const noexcept { return value_; }
			explicit operator bool() const noexcept
			{
				return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
			}
			HANDLE Release() noexcept
			{
				const HANDLE value = value_;
				value_ = INVALID_HANDLE_VALUE;
				return value;
			}

		private:
			HANDLE value_ = INVALID_HANDLE_VALUE;
		};

		class DeletePathOnExit
		{
		public:
			explicit DeletePathOnExit(std::wstring path) : path_(std::move(path)) {}
			~DeletePathOnExit()
			{
				if (active_ && !path_.empty()) DeleteFileW(path_.c_str());
			}
			void Release() noexcept { active_ = false; }

		private:
			std::wstring path_;
			bool active_ = true;
		};

		class NamedTransactionGuard
		{
		public:
			bool Acquire(const std::wstring& path, DWORD& error) noexcept
			{
				uint64_t hash = 1469598103934665603ull;
				for (const wchar_t raw : path)
				{
					const wchar_t value = static_cast<wchar_t>(std::towupper(raw));
					hash ^= static_cast<uint16_t>(value);
					hash *= 1099511628211ull;
				}
				wchar_t name[64] = {};
				if (_snwprintf_s(name, _TRUNCATE, L"Local\\Inkeys.UInk.%016llx", hash) < 0)
				{
					error = ERROR_INVALID_NAME;
					return false;
				}
				handle_ = UniqueHandle(CreateMutexW(nullptr, FALSE, name));
				if (!handle_)
				{
					error = GetLastError();
					return false;
				}
				const DWORD wait = WaitForSingleObject(handle_.Get(), 5000);
				if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
				{
					error = wait == WAIT_TIMEOUT ? ERROR_LOCK_VIOLATION : GetLastError();
					return false;
				}
				acquired_ = true;
				return true;
			}

			~NamedTransactionGuard()
			{
				if (acquired_) ReleaseMutex(handle_.Get());
			}

		private:
			UniqueHandle handle_;
			bool acquired_ = false;
		};

		bool IsTransientFileError(DWORD error) noexcept
		{
			return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
		}

		UniqueHandle OpenWithRetry(const std::wstring& path, DWORD access, DWORD share,
			DWORD creation, DWORD flags, const UInkFileAccessOptions& options,
			DWORD& error) noexcept
		{
			const uint32_t attempts = std::max<uint32_t>(1, options.transientRetryCount);
			for (uint32_t attempt = 0; attempt < attempts; ++attempt)
			{
				UniqueHandle handle(CreateFileW(path.c_str(), access, share, nullptr,
					creation, flags, nullptr));
				if (handle)
				{
					error = ERROR_SUCCESS;
					return handle;
				}
				error = GetLastError();
				if (!IsTransientFileError(error) || attempt + 1 == attempts) break;
				Sleep(options.transientRetryDelayMs);
			}
			return {};
		}

		std::optional<std::wstring> NormalizePath(const std::wstring& path)
		{
			if (path.empty()) return std::nullopt;
			const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
			if (required == 0) return std::nullopt;
			std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1);
			const DWORD written = GetFullPathNameW(path.c_str(),
				static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
			if (written == 0 || written >= buffer.size()) return std::nullopt;
			return std::wstring(buffer.data(), written);
		}

		bool SamePath(const std::wstring& left, const std::wstring& right) noexcept
		{
			return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
				right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
		}

		bool SameRevisionIdentityAndContent(const UInkSourceRevision& left,
			const UInkSourceRevision& right) noexcept
		{
			return left.volumeSerial == right.volumeSerial &&
				left.fileIndex == right.fileIndex && left.length == right.length &&
				left.sha256 == right.sha256;
		}

		bool HashBytes(std::span<const std::byte> bytes, std::array<uint8_t, 32>& digest,
			DWORD& error, std::span<const std::byte> prefix = {}) noexcept
		{
			BCRYPT_ALG_HANDLE algorithmRaw = nullptr;
			if (BCryptOpenAlgorithmProvider(&algorithmRaw, BCRYPT_SHA256_ALGORITHM,
				nullptr, 0) < 0)
			{
				error = ERROR_NOT_SUPPORTED;
				return false;
			}
			struct AlgorithmCloser
			{
				BCRYPT_ALG_HANDLE value;
				~AlgorithmCloser() { BCryptCloseAlgorithmProvider(value, 0); }
			} algorithm{ algorithmRaw };

			DWORD objectLength = 0;
			DWORD returned = 0;
			if (BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0) < 0)
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			try
			{
				std::vector<uint8_t> object(objectLength);
				BCRYPT_HASH_HANDLE hashRaw = nullptr;
				if (BCryptCreateHash(algorithm.value, &hashRaw, object.data(), objectLength,
					nullptr, 0, 0) < 0)
				{
					error = ERROR_INVALID_DATA;
					return false;
				}
				struct HashCloser
				{
					BCRYPT_HASH_HANDLE value;
					~HashCloser() { BCryptDestroyHash(value); }
				} hash{ hashRaw };
				auto hashData = [&](std::span<const std::byte> data)
				{
					size_t position = 0;
					while (position < data.size())
					{
						const ULONG count = static_cast<ULONG>(std::min<size_t>(
							data.size() - position, std::numeric_limits<ULONG>::max()));
						if (BCryptHashData(hash.value,
							reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data() + position)),
							count, 0) < 0)
						{
							error = ERROR_INVALID_DATA;
							return false;
						}
						position += count;
					}
					return true;
				};
				if (!hashData(prefix) || !hashData(bytes)) return false;
				if (BCryptFinishHash(hash.value, digest.data(),
					static_cast<ULONG>(digest.size()), 0) < 0)
				{
					error = ERROR_INVALID_DATA;
					return false;
				}
				error = ERROR_SUCCESS;
				return true;
			}
			catch (...)
			{
				error = ERROR_NOT_ENOUGH_MEMORY;
				return false;
			}
		}

		bool HashAppendPayload(uint64_t truncateOffset, std::span<const std::byte> bytes,
			std::array<uint8_t, 32>& digest, DWORD& error) noexcept
		{
			std::array<std::byte, sizeof(truncateOffset)> prefix = {};
			for (size_t index = 0; index < prefix.size(); ++index)
			{
				prefix[index] = static_cast<std::byte>((truncateOffset >> (index * 8)) & 0xff);
			}
			return HashBytes(bytes, digest, error, prefix);
		}

		bool HashHandle(HANDLE handle, uint64_t length, std::array<uint8_t, 32>& digest,
			DWORD& error)
		{
			LARGE_INTEGER original = {};
			LARGE_INTEGER zero = {};
			if (!SetFilePointerEx(handle, zero, &original, FILE_CURRENT) ||
				!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN))
			{
				error = GetLastError();
				return false;
			}
			struct PointerRestorer
			{
				HANDLE handle;
				LARGE_INTEGER position;
				~PointerRestorer() { SetFilePointerEx(handle, position, nullptr, FILE_BEGIN); }
			} restorer{ handle, original };

			BCRYPT_ALG_HANDLE algorithmRaw = nullptr;
			if (BCryptOpenAlgorithmProvider(&algorithmRaw, BCRYPT_SHA256_ALGORITHM,
				nullptr, 0) < 0)
			{
				error = ERROR_NOT_SUPPORTED;
				return false;
			}
			struct AlgorithmCloser
			{
				BCRYPT_ALG_HANDLE value;
				~AlgorithmCloser() { BCryptCloseAlgorithmProvider(value, 0); }
			} algorithm{ algorithmRaw };

			DWORD objectLength = 0;
			DWORD returned = 0;
			if (BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0) < 0)
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			try
			{
				std::vector<uint8_t> object(objectLength);
				std::vector<uint8_t> chunk(1024 * 1024);
				BCRYPT_HASH_HANDLE hashRaw = nullptr;
				if (BCryptCreateHash(algorithm.value, &hashRaw, object.data(), objectLength,
					nullptr, 0, 0) < 0)
				{
					error = ERROR_INVALID_DATA;
					return false;
				}
				struct HashCloser
				{
					BCRYPT_HASH_HANDLE value;
					~HashCloser() { BCryptDestroyHash(value); }
				} hash{ hashRaw };
				uint64_t remaining = length;
				while (remaining != 0)
				{
					const DWORD requested = static_cast<DWORD>(std::min<uint64_t>(remaining,
						chunk.size()));
					DWORD read = 0;
					if (!ReadFile(handle, chunk.data(), requested, &read, nullptr) || read == 0)
					{
						error = GetLastError();
						if (error == ERROR_SUCCESS) error = ERROR_HANDLE_EOF;
						return false;
					}
					if (BCryptHashData(hash.value, chunk.data(), read, 0) < 0)
					{
						error = ERROR_INVALID_DATA;
						return false;
					}
					remaining -= read;
				}
				if (BCryptFinishHash(hash.value, digest.data(),
					static_cast<ULONG>(digest.size()), 0) < 0)
				{
					error = ERROR_INVALID_DATA;
					return false;
				}
				error = ERROR_SUCCESS;
				return true;
			}
			catch (...)
			{
				error = ERROR_NOT_ENOUGH_MEMORY;
				return false;
			}
		}

		bool RevisionFromHandle(HANDLE handle, UInkSourceRevision& revision,
			DWORD& error)
		{
			BY_HANDLE_FILE_INFORMATION information = {};
			if (!GetFileInformationByHandle(handle, &information))
			{
				error = GetLastError();
				return false;
			}
			revision.volumeSerial = information.dwVolumeSerialNumber;
			revision.fileIndex = (static_cast<uint64_t>(information.nFileIndexHigh) << 32) |
				information.nFileIndexLow;
			revision.length = (static_cast<uint64_t>(information.nFileSizeHigh) << 32) |
				information.nFileSizeLow;
			revision.lastWriteTime = (static_cast<uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32) |
				information.ftLastWriteTime.dwLowDateTime;
			return HashHandle(handle, revision.length, revision.sha256, error);
		}

		struct FileSnapshot
		{
			std::vector<std::byte> bytes;
			UInkSourceRevision revision;
			DWORD error = ERROR_SUCCESS;
			bool limitExceeded = false;
		};

		std::optional<FileSnapshot> ReadSnapshot(const std::wstring& path,
			uint64_t maxBytes, const UInkFileAccessOptions& access)
		{
			FileSnapshot snapshot;
			UniqueHandle handle = OpenWithRetry(path, GENERIC_READ, FILE_SHARE_READ,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
				access, snapshot.error);
			if (!handle) return snapshot;

			BY_HANDLE_FILE_INFORMATION information = {};
			if (!GetFileInformationByHandle(handle.Get(), &information))
			{
				snapshot.error = GetLastError();
				return snapshot;
			}
			const uint64_t length = (static_cast<uint64_t>(information.nFileSizeHigh) << 32) |
				information.nFileSizeLow;
			if (length > maxBytes || length > std::numeric_limits<size_t>::max())
			{
				snapshot.limitExceeded = true;
				return snapshot;
			}

			try
			{
				snapshot.bytes.resize(static_cast<size_t>(length));
			}
			catch (...)
			{
				snapshot.limitExceeded = true;
				return snapshot;
			}
			size_t position = 0;
			while (position < snapshot.bytes.size())
			{
				const DWORD requested = static_cast<DWORD>(std::min<size_t>(
					snapshot.bytes.size() - position, 1024 * 1024));
				DWORD read = 0;
				if (!ReadFile(handle.Get(), snapshot.bytes.data() + position,
					requested, &read, nullptr) || read == 0)
				{
					snapshot.error = GetLastError();
					if (snapshot.error == ERROR_SUCCESS) snapshot.error = ERROR_HANDLE_EOF;
					return snapshot;
				}
				position += read;
			}

			snapshot.revision.volumeSerial = information.dwVolumeSerialNumber;
			snapshot.revision.fileIndex = (static_cast<uint64_t>(information.nFileIndexHigh) << 32) |
				information.nFileIndexLow;
			snapshot.revision.length = length;
			snapshot.revision.lastWriteTime =
				(static_cast<uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32) |
				information.ftLastWriteTime.dwLowDateTime;
			if (!HashBytes(snapshot.bytes, snapshot.revision.sha256, snapshot.error))
				return snapshot;
			return snapshot;
		}

		bool PathExists(const std::wstring& path) noexcept
		{
			return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
		}

		std::optional<std::wstring> UniqueSiblingPath(const std::wstring& target,
			const wchar_t* suffix)
		{
			for (uint32_t attempt = 0; attempt < 32; ++attempt)
			{
				const std::optional<UInkGuid> guid = CreateUInkGuid();
				if (!guid) return std::nullopt;
				std::string token = FormatUInkGuid(*guid);
				std::wstring wideToken(token.begin(), token.end());
				std::wstring candidate = target + L"." + wideToken + suffix;
				if (!PathExists(candidate)) return candidate;
			}
			return std::nullopt;
		}

		bool WriteNewFile(const std::wstring& path, std::span<const std::byte> bytes,
			bool durable, const UInkFileTestFaultInjection& faults, DWORD& error)
		{
			UniqueHandle handle(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
				0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
				nullptr));
			if (!handle)
			{
				error = GetLastError();
				return false;
			}
			size_t position = 0;
			while (position < bytes.size())
			{
				if (static_cast<uint64_t>(position) >= faults.failWriteAfterBytes)
				{
					error = ERROR_WRITE_FAULT;
					return false;
				}
				size_t requestedSize = std::min<size_t>(bytes.size() - position, 1024 * 1024);
				if (faults.failWriteAfterBytes != UINT64_MAX)
				{
					requestedSize = static_cast<size_t>(std::min<uint64_t>(requestedSize,
						faults.failWriteAfterBytes - static_cast<uint64_t>(position)));
				}
				const DWORD requested = static_cast<DWORD>(requestedSize);
				DWORD written = 0;
				if (!WriteFile(handle.Get(), bytes.data() + position, requested,
					&written, nullptr) || written == 0)
				{
					error = GetLastError();
					if (error == ERROR_SUCCESS) error = ERROR_WRITE_FAULT;
					return false;
				}
				position += written;
			}
			if (durable && faults.failFlush)
			{
				error = ERROR_WRITE_FAULT;
				return false;
			}
			if (durable && !FlushFileBuffers(handle.Get()))
			{
				error = GetLastError();
				return false;
			}
			error = ERROR_SUCCESS;
			return true;
		}

		uint64_t CurrentUnixSeconds() noexcept
		{
			FILETIME fileTime = {};
			GetSystemTimeAsFileTime(&fileTime);
			const uint64_t ticks = (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) |
				fileTime.dwLowDateTime;
			constexpr uint64_t kUnixEpochTicks = 116444736000000000ull;
			return ticks < kUnixEpochTicks ? 0 : (ticks - kUnixEpochTicks) / 10000000ull;
		}

		void AddDiagnostic(std::vector<UInkDiagnostic>& diagnostics,
			UInkDiagnosticCode code, UInkDiagnosticSeverity severity,
			std::string path, DWORD error = ERROR_SUCCESS) noexcept
		{
			try
			{
				diagnostics.push_back({ code, severity, 0, 0, std::move(path), error });
			}
			catch (...) {}
		}

		bool NormalizeForFullSave(UInkDocument& document, UInkSaveMode mode,
			bool normalizeFallbacks, std::vector<UInkDiagnostic>& diagnostics)
		{
			if (mode == UInkSaveMode::SaveAsNewLogicalFile)
			{
				const std::optional<UInkGuid> guid = CreateUInkGuid();
				if (!guid)
				{
					AddDiagnostic(diagnostics, UInkDiagnosticCode::WriteFailed,
						UInkDiagnosticSeverity::Error, "header.guid", ERROR_NOT_SUPPORTED);
					return false;
				}
				document.header.guid = *guid;
			}
			if (document.header.guid.IsZero()) return false;

			document.usesImplicitDevice = !document.headerExtension ||
				document.headerExtension->devices.empty();
			document.usesImplicitWorkspace = !document.headerExtension ||
				document.headerExtension->workspaces.empty();
			document.header.deviceNum = document.usesImplicitDevice ? 1u :
				static_cast<uint32_t>(document.headerExtension->devices.size());
			document.header.workspaceNum = document.usesImplicitWorkspace ? 1u :
				static_cast<uint32_t>(document.headerExtension->workspaces.size());
			if (normalizeFallbacks && document.headerExtension)
			{
				for (UInkWorkspace& workspace : document.headerExtension->workspaces)
				{
					// 未知 Workspace 按规范的通用白板 effective 语义写回。
					if (workspace.workspaceType > 2) workspace.workspaceType = 1;
				}
			}
			std::set<std::array<uint8_t, 16>> pages;
			for (UInkCanvas& canvas : document.canvases)
			{
				pages.insert(canvas.pageGuid.Bytes());
				uint32_t outputUndo = 0;
				uint32_t previousInputUndo = 0;
				for (size_t index = 0; index < canvas.content.size(); ++index)
				{
					std::visit([&](auto& content)
					{
						using T = std::decay_t<decltype(content)>;
						if constexpr (std::is_same_v<T, UInkInk>)
						{
							if (content.declaredInkType < 0 || normalizeFallbacks)
							{
								content.declaredInkType = content.effectiveKind == UInkInkKind::Erase ? 0 :
									content.effectiveKind == UInkInkKind::Highlighter ? 2 :
									content.effectiveKind == UInkInkKind::AdvancedHighlighter ? 3 : 1;
							}
							if (content.declaredTexture < 0 || normalizeFallbacks)
								content.declaredTexture = 0;
						}
						else if constexpr (std::is_same_v<T, UInkShape>)
						{
							if (content.stroke)
							{
								const bool openShape = content.declaredShapeType == 0 ||
									content.declaredShapeType == 1;
								// 闭合图形不支持端点标记，规范化时写回读取阶段的确定性降级结果。
								if (!openShape)
								{
									content.stroke->declaredStartMarker = 0;
									content.stroke->declaredEndMarker = 0;
								}
								else if (normalizeFallbacks)
								{
									content.stroke->declaredStartMarker =
										content.stroke->effectiveStartMarker;
									content.stroke->declaredEndMarker =
										content.stroke->effectiveEndMarker;
								}
								else
								{
									if (content.stroke->declaredStartMarker < 0)
										content.stroke->declaredStartMarker = content.stroke->effectiveStartMarker;
									if (content.stroke->declaredEndMarker < 0)
										content.stroke->declaredEndMarker = content.stroke->effectiveEndMarker;
								}
							}
							if (content.fill &&
								(content.fill->declaredFillType < 0 || normalizeFallbacks))
								content.fill->declaredFillType = 0;
						}
						const uint32_t inputUndo = content.undoId;
						if (index != 0 && inputUndo != previousInputUndo) ++outputUndo;
						content.contentId = static_cast<uint32_t>(index);
						content.undoId = outputUndo;
						previousInputUndo = inputUndo;
					}, canvas.content[index]);
				}
			}
			document.header.pageNum = static_cast<uint32_t>(pages.size());
			document.header.time = CurrentUnixSeconds();
			return true;
		}

		bool IsCreateNewMode(UInkSaveMode mode) noexcept
		{
			return mode == UInkSaveMode::SaveAsNewLogicalFile ||
				mode == UInkSaveMode::CreateNewLogicalFileWithIdentity;
		}

		uint64_t ExpectedObjectCount(const UInkDocument& document) noexcept
		{
			uint64_t count = 1 + (document.headerExtension ? 1 : 0);
			for (const UInkCanvas& canvas : document.canvases)
				count += 1 + canvas.content.size();
			return count;
		}

		bool IsMediaObject(const UInkAppendObject& object) noexcept
		{
			return std::visit([](const auto& value)
			{
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, UInkMedia>) return true;
				else if constexpr (std::is_same_v<T, UInkCanvas>)
				{
					return std::any_of(value.content.begin(), value.content.end(),
						[](const UInkContent& content)
						{
							return std::holds_alternative<UInkMedia>(content);
						});
				}
				else return false;
			}, object);
		}

		uint64_t GeometryPointCount(const UInkContent& content) noexcept
		{
			return std::visit([](const auto& value) -> uint64_t
			{
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, UInkInk>) return value.points.size();
				else if constexpr (std::is_same_v<T, UInkShape>)
				{
					if (const UInkLineGeometry* line = std::get_if<UInkLineGeometry>(&value.geometry))
						return line->points.size();
				}
				return 0;
			}, content);
		}

		uint64_t GeometryPointCount(const UInkAppendObject& object) noexcept
		{
			return std::visit([](const auto& value) -> uint64_t
			{
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, UInkCanvas>)
				{
					uint64_t total = 0;
					for (const UInkContent& content : value.content)
					{
						const uint64_t count = GeometryPointCount(content);
						if (total > std::numeric_limits<uint64_t>::max() - count)
							return std::numeric_limits<uint64_t>::max();
						total += count;
					}
					return total;
				}
				else if constexpr (std::is_same_v<T, UInkInk>) return value.points.size();
				else if constexpr (std::is_same_v<T, UInkShape>)
				{
					if (const UInkLineGeometry* line = std::get_if<UInkLineGeometry>(&value.geometry))
						return line->points.size();
				}
				return 0;
			}, object);
		}

		using OptionalGuidBytes = std::optional<std::array<uint8_t, 16>>;
		using PageKey = std::pair<OptionalGuidBytes, uint32_t>;
		using PageGroup = std::tuple<OptionalGuidBytes, OptionalGuidBytes,
			std::array<uint8_t, 16>>;
		using CanvasIdentity = std::tuple<OptionalGuidBytes, OptionalGuidBytes,
			std::array<uint8_t, 16>, uint32_t>;

		OptionalGuidBytes GuidBytes(const std::optional<UInkGuid>& guid)
		{
			return guid ? OptionalGuidBytes(guid->Bytes()) : std::nullopt;
		}

		bool RegistryContainsWorkspace(const UInkDocument& document,
			const std::optional<UInkGuid>& guid) noexcept
		{
			if (document.usesImplicitWorkspace) return !guid;
			if (!guid || !document.headerExtension) return false;
			return std::any_of(document.headerExtension->workspaces.begin(),
				document.headerExtension->workspaces.end(), [&](const UInkWorkspace& workspace)
				{
					return workspace.usable && workspace.guid == *guid;
				});
		}

		bool RegistryContainsDevice(const UInkDocument& document,
			const std::optional<UInkGuid>& guid) noexcept
		{
			if (document.usesImplicitDevice) return !guid;
			if (!guid || !document.headerExtension) return false;
			return std::any_of(document.headerExtension->devices.begin(),
				document.headerExtension->devices.end(), [&](const UInkDevice& device)
				{
					return device.usable && device.guid == *guid;
				});
		}

		const UInkWorkspace* FindWorkspaceForAppend(const UInkDocument& document,
			const std::optional<UInkGuid>& guid) noexcept
		{
			if (!guid || !document.headerExtension) return nullptr;
			for (const UInkWorkspace& workspace : document.headerExtension->workspaces)
				if (workspace.usable && workspace.guid == *guid) return &workspace;
			return nullptr;
		}

		bool ReadRevisionAtPath(const std::wstring& path,
			const UInkFileAccessOptions& access, UInkSourceRevision& revision,
			DWORD& error)
		{
			UniqueHandle handle = OpenWithRetry(path, GENERIC_READ, 0, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, access, error);
			return handle && RevisionFromHandle(handle.Get(), revision, error);
		}
	}

	UInkReadResult ReadUInkFile(const std::wstring& path,
		const UInkReadLimits& limits, const UInkFileAccessOptions& access)
	{
		UInkReadResult result;
		try
		{
			const std::optional<std::wstring> normalized = NormalizePath(path);
			if (!normalized)
			{
				result.status = UInkReadStatus::IoError;
				result.diagnostics.push_back({ UInkDiagnosticCode::IoError,
					UInkDiagnosticSeverity::Fatal, 0, 0, "path", ERROR_INVALID_NAME });
				return result;
			}
			const std::optional<FileSnapshot> snapshot = ReadSnapshot(*normalized,
				limits.maxFileBytes, access);
			if (!snapshot || snapshot->error != ERROR_SUCCESS || snapshot->limitExceeded)
			{
				result.status = snapshot && snapshot->limitExceeded ?
					UInkReadStatus::LimitExceeded : UInkReadStatus::IoError;
				result.sourcePath = *normalized;
				result.diagnostics.push_back({ snapshot && snapshot->limitExceeded ?
					UInkDiagnosticCode::LimitExceeded : UInkDiagnosticCode::IoError,
					UInkDiagnosticSeverity::Fatal, 0, 0, "file",
					snapshot ? snapshot->error : ERROR_READ_FAULT });
				return result;
			}
			result = DecodeUInk(snapshot->bytes, limits);
			result.sourceRevision = snapshot->revision;
			result.sourcePath = *normalized;
			result.provenance.sourceWasExternal = true;
			return result;
		}
		catch (const std::bad_alloc&)
		{
			result.status = UInkReadStatus::LimitExceeded;
			try
			{
				result.diagnostics.push_back({ UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, 0, 0, "allocation", 0 });
			}
			catch (...) {}
			return result;
		}
	}

	std::optional<UInkEditingSession> CreateUInkEditingSession(
		const UInkReadResult& readResult, UInkEditingSource source)
	{
		if (!readResult.document || readResult.status == UInkReadStatus::RejectedHeader ||
			readResult.status == UInkReadStatus::LimitExceeded ||
			readResult.status == UInkReadStatus::IoError) return std::nullopt;
		UInkLoadProvenance provenance = readResult.provenance;
		// 外部文件必须由调用方明确选择另存为或允许有损覆盖。
		provenance.sourceWasExternal = source == UInkEditingSource::ExternalImport;
		if (source == UInkEditingSource::ApplicationOwnedPrivateWorkspace)
		{
			provenance.usedFieldFallback = false;
			provenance.requiresSaveAs = false;
		}
		if (provenance.sourceWasExternal) provenance.requiresSaveAs = true;
		return UInkEditingSession{ *readResult.document, provenance,
			readResult.diagnostics, readResult.sourceRevision, readResult.sourcePath };
	}

	UInkSaveResult SaveUInkFile(const std::wstring& path,
		const UInkEditingSession& session, const UInkSaveOptions& options)
	{
		UInkSaveResult result;
		try
		{
			const UInkFileTestFaultInjection faults = SnapshotTestFaults();
			const std::optional<std::wstring> target = NormalizePath(path);
			if (!target)
			{
				result.status = UInkSaveStatus::IoError;
				result.systemError = ERROR_INVALID_NAME;
				return result;
			}
			if (HasMedia(session.document))
			{
				result.status = UInkSaveStatus::ResourcePackUnsupported;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::ResourcePackUnsupported,
					UInkDiagnosticSeverity::Error, "media");
				return result;
			}
			if (session.provenance.usedTemporaryIdentity)
			{
				result.status = UInkSaveStatus::InvalidSession;
				return result;
			}
			if (options.mode == UInkSaveMode::SaveExistingLogicalFile &&
				(session.provenance.requiresSaveAs || session.provenance.sourceWasExternal ||
					session.provenance.usedFieldFallback ||
					session.provenance.containsInvalidCompleteBlocks ||
					session.provenance.contentSequenceRecovered ||
					session.provenance.invalidBlockBeforeValidBlock))
			{
				result.status = UInkSaveStatus::InvalidSession;
				return result;
			}
			if (!IsCreateNewMode(options.mode))
			{
				const std::optional<std::wstring> source = NormalizePath(session.sourcePath);
				if (!source || !SamePath(*source, *target) || !session.sourceRevision)
				{
					result.status = UInkSaveStatus::InvalidSession;
					return result;
				}
			}

			UInkDocument document = session.document;
			if (!NormalizeForFullSave(document, options.mode,
				session.provenance.usedFieldFallback, result.diagnostics))
			{
				result.status = UInkSaveStatus::InvalidModel;
				return result;
			}
			UInkEncodeResult encoded = EncodeUInkDocument(document);
			if (encoded.status != UInkEncodeStatus::Success)
			{
				result.status = UInkSaveStatus::InvalidModel;
				result.diagnostics.insert(result.diagnostics.end(),
					encoded.diagnostics.begin(), encoded.diagnostics.end());
				return result;
			}
			std::array<uint8_t, 32> encodedSha256 = {};
			DWORD error = ERROR_SUCCESS;
			if (!HashBytes(encoded.bytes, encodedSha256, error))
			{
				result.status = UInkSaveStatus::IoError;
				result.systemError = error;
				return result;
			}

			const std::optional<std::wstring> tempPath = UniqueSiblingPath(*target, L".tmp");
			if (!tempPath)
			{
				result.status = UInkSaveStatus::IoError;
				result.systemError = ERROR_NOT_ENOUGH_MEMORY;
				return result;
			}
			DeletePathOnExit tempCleanup(*tempPath);
			if (!WriteNewFile(*tempPath, encoded.bytes, true, faults, error))
			{
				result.status = UInkSaveStatus::IoError;
				result.systemError = error;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::WriteFailed,
					UInkDiagnosticSeverity::Error, "temp", error);
				return result;
			}

			const UInkReadResult selfRead = ReadUInkFile(*tempPath);
			if (faults.failSelfValidation ||
				selfRead.status != UInkReadStatus::Complete || !selfRead.document ||
				!selfRead.sourceRevision ||
				selfRead.sourceRevision->length != encoded.bytes.size() ||
				selfRead.sourceRevision->sha256 != encodedSha256 ||
				selfRead.decodedObjectCount != ExpectedObjectCount(document) ||
				selfRead.document->header.guid != document.header.guid ||
				selfRead.provenance.containsInvalidCompleteBlocks ||
				selfRead.provenance.usedTemporaryIdentity ||
				selfRead.provenance.contentSequenceRecovered)
			{
				result.status = UInkSaveStatus::SelfValidationFailed;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SelfValidationFailed,
					UInkDiagnosticSeverity::Error, "temp");
				return result;
			}

			NamedTransactionGuard guard;
			if (!guard.Acquire(*target, error))
			{
				result.status = UInkSaveStatus::IoError;
				result.systemError = error;
				return result;
			}

			std::wstring committedBackupPath;
			if (IsCreateNewMode(options.mode))
			{
				if (PathExists(*target))
				{
					result.status = UInkSaveStatus::SourceChanged;
					result.systemError = ERROR_FILE_EXISTS;
					AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
						UInkDiagnosticSeverity::Error, "target", result.systemError);
					return result;
				}
				const BOOL moved = faults.failCommit ?
					(SetLastError(ERROR_ACCESS_DENIED), FALSE) :
					MoveFileExW(tempPath->c_str(), target->c_str(), MOVEFILE_WRITE_THROUGH);
				if (!moved)
				{
					result.systemError = GetLastError();
					result.status = (result.systemError == ERROR_ALREADY_EXISTS ||
						result.systemError == ERROR_FILE_EXISTS) ? UInkSaveStatus::SourceChanged :
						UInkSaveStatus::IoError;
					if (result.status == UInkSaveStatus::SourceChanged)
					{
						AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
							UInkDiagnosticSeverity::Error, "target", result.systemError);
					}
					return result;
				}
				tempCleanup.Release();
			}
			else
			{
				UInkSourceRevision currentRevision;
				if (!ReadRevisionAtPath(*target, options.access, currentRevision, error))
				{
					result.status = UInkSaveStatus::SourceChanged;
					result.systemError = error;
					AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
						UInkDiagnosticSeverity::Error, "source", error);
					return result;
				}
				if (currentRevision != *session.sourceRevision)
				{
					result.status = UInkSaveStatus::SourceChanged;
					AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
						UInkDiagnosticSeverity::Error, "source");
					return result;
				}

				const std::optional<std::wstring> backupPath = UniqueSiblingPath(*target, L".bak");
				if (!backupPath)
				{
					result.status = UInkSaveStatus::IoError;
					result.systemError = ERROR_NOT_ENOUGH_MEMORY;
					return result;
				}
				const BOOL replaced = faults.failCommit ?
					(SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT), FALSE) :
					ReplaceFileW(target->c_str(), tempPath->c_str(), backupPath->c_str(),
						REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
				if (!replaced)
				{
					result.systemError = GetLastError();
					UInkSourceRevision afterFailure;
					DWORD inspectError = ERROR_SUCCESS;
					if (ReadRevisionAtPath(*target, options.access, afterFailure, inspectError) &&
						afterFailure == *session.sourceRevision)
					{
						result.status = UInkSaveStatus::IoError;
						if (PathExists(*backupPath)) DeleteFileW(backupPath->c_str());
					}
					else
					{
						result.status = UInkSaveStatus::PartialCommitRequiresRecovery;
						result.recoveryPath = PathExists(*backupPath) ? *backupPath : *tempPath;
						if (result.recoveryPath == *tempPath) tempCleanup.Release();
					}
					return result;
				}
				tempCleanup.Release();

				UInkSourceRevision backupRevision;
				if (!ReadRevisionAtPath(*backupPath, options.access, backupRevision, error) ||
					backupRevision != *session.sourceRevision)
				{
					const std::optional<std::wstring> newRecovery =
						UniqueSiblingPath(*target, L".new-recovery");
					if (newRecovery && MoveFileExW(target->c_str(), newRecovery->c_str(),
						MOVEFILE_WRITE_THROUGH) && MoveFileExW(backupPath->c_str(), target->c_str(),
						MOVEFILE_WRITE_THROUGH))
					{
						DeleteFileW(newRecovery->c_str());
						result.status = UInkSaveStatus::SourceChanged;
						AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
							UInkDiagnosticSeverity::Error, "source", error);
					}
					else
					{
						result.status = UInkSaveStatus::PartialCommitRequiresRecovery;
						result.recoveryPath = PathExists(*backupPath) ? *backupPath :
							(newRecovery ? *newRecovery : std::wstring{});
					}
					return result;
				}
				// 直到最终目标字节复核通过前保留 predecessor，供提交期竞争恢复。
				committedBackupPath = *backupPath;
			}

			UInkSourceRevision revision;
			if (!ReadRevisionAtPath(*target, options.access, revision, error))
			{
				result.status = UInkSaveStatus::PartialCommitRequiresRecovery;
				result.systemError = error;
				if (!committedBackupPath.empty() && PathExists(committedBackupPath))
					result.recoveryPath = committedBackupPath;
				else if (PathExists(*target)) result.recoveryPath = *target;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::IoError,
					UInkDiagnosticSeverity::Error, "targetRevision", error);
				return result;
			}
			if (faults.failCommittedRevisionValidation ||
				revision.length != encoded.bytes.size() || revision.sha256 != encodedSha256)
			{
				result.status = UInkSaveStatus::PartialCommitRequiresRecovery;
				result.systemError = ERROR_FILE_INVALID;
				if (!committedBackupPath.empty() && PathExists(committedBackupPath))
					result.recoveryPath = committedBackupPath;
				else if (PathExists(*target)) result.recoveryPath = *target;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
					UInkDiagnosticSeverity::Error, "target", result.systemError);
				return result;
			}
			if (!committedBackupPath.empty() && !DeleteFileW(committedBackupPath.c_str()))
			{
				const DWORD cleanupError = GetLastError();
				result.recoveryPath = committedBackupPath;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::IoError,
					UInkDiagnosticSeverity::Warning, "backup.cleanup", cleanupError);
			}
			result.status = UInkSaveStatus::Committed;
			result.revision = revision;
			return result;
		}
		catch (const std::bad_alloc&)
		{
			result.status = UInkSaveStatus::InvalidModel;
			return result;
		}
	}

	UInkAppendPlan AnalyzeUInkAppend(const UInkReadResult& source,
		const UInkAppendBatch& batch)
	{
		UInkAppendPlan plan;
		try
		{
			if (!source.document || !source.sourceRevision || source.sourcePath.empty() ||
				source.status == UInkReadStatus::RejectedHeader ||
				source.status == UInkReadStatus::LimitExceeded || source.status == UInkReadStatus::IoError)
				return plan;
			plan.expectedRevision = *source.sourceRevision;
			plan.sourcePath = source.sourcePath;
			if (!source.safeAppendOffset)
			{
				plan.status = UInkAppendPlanStatus::RequiresFullSave;
				return plan;
			}
			plan.truncateOffset = *source.safeAppendOffset;
			plan.sourceContainsMedia = HasMedia(*source.document);
			if (source.provenance.invalidBlockBeforeValidBlock ||
				source.provenance.usedTemporaryIdentity ||
				source.provenance.contentSequenceRecovered ||
				plan.truncateOffset > plan.expectedRevision.length)
			{
				plan.status = UInkAppendPlanStatus::RequiresFullSave;
				return plan;
			}
			if (batch.objects.empty())
			{
				plan.status = UInkAppendPlanStatus::InvalidBatch;
				return plan;
			}
			if (std::any_of(batch.objects.begin(), batch.objects.end(), IsMediaObject))
			{
				plan.status = UInkAppendPlanStatus::ResourcePackUnsupported;
				AddDiagnostic(plan.diagnostics, UInkDiagnosticCode::ResourcePackUnsupported,
					UInkDiagnosticSeverity::Error, "append.media");
				return plan;
			}

			const UInkDocument& document = *source.document;
			std::map<std::array<uint8_t, 16>, std::pair<OptionalGuidBytes, uint32_t>> pagesByGuid;
			std::map<PageKey, std::array<uint8_t, 16>> pagesByIndex;
			std::map<OptionalGuidBytes, std::set<uint32_t>> pageIndices;
			std::map<PageGroup, std::set<uint32_t>> layers;
			std::set<CanvasIdentity> canvasKeys;
			std::map<std::array<uint8_t, 16>, int32_t> presentationSlides;
			std::map<std::pair<OptionalGuidBytes, int32_t>, std::array<uint8_t, 16>>
				presentationPagesBySlide;
			for (const UInkCanvas& canvas : document.canvases)
			{
				const OptionalGuidBytes workspace = GuidBytes(canvas.workspaceGuid);
				const OptionalGuidBytes device = GuidBytes(canvas.deviceGuid);
				pagesByGuid.emplace(canvas.pageGuid.Bytes(),
					std::make_pair(workspace, canvas.pageIndex));
				pagesByIndex.emplace(PageKey(workspace, canvas.pageIndex), canvas.pageGuid.Bytes());
				pageIndices[workspace].insert(canvas.pageIndex);
				layers[PageGroup(workspace, device, canvas.pageGuid.Bytes())].insert(canvas.layerIndex);
				canvasKeys.emplace(workspace, device, canvas.pageGuid.Bytes(), canvas.layerIndex);
				const UInkWorkspace* owner = FindWorkspaceForAppend(document, canvas.workspaceGuid);
				if (owner && owner->workspaceType == 2 && canvas.slideId)
				{
					presentationSlides.emplace(canvas.pageGuid.Bytes(), *canvas.slideId);
					presentationPagesBySlide.emplace(
						std::make_pair(workspace, *canvas.slideId), canvas.pageGuid.Bytes());
				}
			}

			bool hasCurrentCanvas = !document.canvases.empty();
			uint32_t nextContentId = hasCurrentCanvas ?
				static_cast<uint32_t>(document.canvases.back().content.size()) : 0;
			uint32_t previousUndo = 0;
			bool hasPreviousContent = hasCurrentCanvas && !document.canvases.back().content.empty();
			if (hasPreviousContent)
				std::visit([&](const auto& content) { previousUndo = content.undoId; },
					document.canvases.back().content.back());

			auto acceptContent = [&](const auto& content) -> bool
			{
				if (!hasCurrentCanvas || content.contentId != nextContentId ||
					(!hasPreviousContent && content.undoId != 0) ||
					(hasPreviousContent && content.undoId < previousUndo)) return false;
				if (!plan.firstContentId) plan.firstContentId = content.contentId;
				plan.lastContentId = content.contentId;
				plan.lastUndoId = content.undoId;
				++nextContentId;
				previousUndo = content.undoId;
				hasPreviousContent = true;
				return true;
			};

			for (const UInkAppendObject& object : batch.objects)
			{
				bool valid = std::visit([&](const auto& value) -> bool
				{
					using T = std::decay_t<decltype(value)>;
					if constexpr (std::is_same_v<T, UInkCanvas>)
					{
						if (!RegistryContainsWorkspace(document, value.workspaceGuid) ||
							!RegistryContainsDevice(document, value.deviceGuid) ||
							value.pageGuid.IsZero() || value.temporaryWorkspace ||
							value.temporaryDevice || value.temporaryPage || value.temporaryLayer ||
							value.presentationUnbound || (value.viewport && value.layerIndex != 0)) return false;
						const UInkWorkspace* workspace = FindWorkspaceForAppend(document, value.workspaceGuid);
						if (workspace && workspace->workspaceType == 2 && !value.slideId) return false;
						const OptionalGuidBytes workspaceKey = GuidBytes(value.workspaceGuid);
						const OptionalGuidBytes deviceKey = GuidBytes(value.deviceGuid);
						if (workspace && workspace->workspaceType == 2)
						{
							const auto slide = presentationSlides.find(value.pageGuid.Bytes());
							if (slide != presentationSlides.end() && slide->second != *value.slideId)
								return false;
							const auto page = presentationPagesBySlide.find(
								std::make_pair(workspaceKey, *value.slideId));
							if (page != presentationPagesBySlide.end() &&
								page->second != value.pageGuid.Bytes()) return false;
							presentationSlides.emplace(value.pageGuid.Bytes(), *value.slideId);
							presentationPagesBySlide.emplace(
								std::make_pair(workspaceKey, *value.slideId), value.pageGuid.Bytes());
						}
						const auto page = pagesByGuid.find(value.pageGuid.Bytes());
						if (page != pagesByGuid.end())
						{
							if (page->second != std::make_pair(workspaceKey, value.pageIndex)) return false;
						}
						else
						{
							const uint32_t expectedPage = static_cast<uint32_t>(pageIndices[workspaceKey].size());
							if (value.pageIndex != expectedPage ||
								pagesByIndex.find(PageKey(workspaceKey, value.pageIndex)) != pagesByIndex.end())
								return false;
							pagesByGuid.emplace(value.pageGuid.Bytes(),
								std::make_pair(workspaceKey, value.pageIndex));
							pagesByIndex.emplace(PageKey(workspaceKey, value.pageIndex), value.pageGuid.Bytes());
							pageIndices[workspaceKey].insert(value.pageIndex);
						}
						const PageGroup group(workspaceKey, deviceKey, value.pageGuid.Bytes());
						const uint32_t expectedLayer = static_cast<uint32_t>(layers[group].size());
						if (value.layerIndex != expectedLayer ||
							!canvasKeys.emplace(workspaceKey, deviceKey, value.pageGuid.Bytes(),
								value.layerIndex).second) return false;
						layers[group].insert(value.layerIndex);
						hasCurrentCanvas = true;
						nextContentId = 0;
						previousUndo = 0;
						hasPreviousContent = false;
						for (const UInkContent& content : value.content)
						{
							const bool accepted = std::visit([&](const auto& item)
							{
								return acceptContent(item);
							}, content);
							if (!accepted) return false;
						}
						return true;
					}
					else return acceptContent(value);
				}, object);
				if (!valid)
				{
					plan.status = UInkAppendPlanStatus::InvalidBatch;
					return plan;
				}
			}
			uint64_t batchObjectCount = 0;
			uint64_t batchGeometryPoints = 0;
			for (const UInkAppendObject& object : batch.objects)
			{
				const uint64_t count = std::visit([](const auto& value) -> uint64_t
				{
					using T = std::decay_t<decltype(value)>;
					if constexpr (std::is_same_v<T, UInkCanvas>)
						return 1 + static_cast<uint64_t>(value.content.size());
					else return 1;
				}, object);
				if (batchObjectCount > std::numeric_limits<uint64_t>::max() - count)
				{
					plan.status = UInkAppendPlanStatus::RequiresFullSave;
					return plan;
				}
				batchObjectCount += count;
				const uint64_t points = GeometryPointCount(object);
				const uint64_t maxPoints = UInkReadLimits{}.maxGeometryPoints;
				if (points > maxPoints || batchGeometryPoints > maxPoints - points)
				{
					plan.status = UInkAppendPlanStatus::RequiresFullSave;
					return plan;
				}
				batchGeometryPoints += points;
			}
			const uint64_t maxObjects = UInkReadLimits{}.maxTopLevelObjects;
			if (source.decodedObjectCount > maxObjects ||
				batchObjectCount > maxObjects - source.decodedObjectCount)
			{
				plan.status = UInkAppendPlanStatus::RequiresFullSave;
				return plan;
			}
			const uint64_t maxPoints = UInkReadLimits{}.maxGeometryPoints;
			if (source.geometryPointCount > maxPoints ||
				batchGeometryPoints > maxPoints - source.geometryPointCount)
			{
				plan.status = UInkAppendPlanStatus::RequiresFullSave;
				return plan;
			}

			UInkEncodeResult encoded = EncodeUInkAppendObjects(batch.objects);
			if (encoded.status != UInkEncodeStatus::Success)
			{
				plan.status = UInkAppendPlanStatus::InvalidBatch;
				plan.diagnostics = std::move(encoded.diagnostics);
				return plan;
			}
			if (encoded.bytes.size() > std::numeric_limits<uint64_t>::max() - plan.truncateOffset ||
				plan.truncateOffset + encoded.bytes.size() > UInkReadLimits{}.maxFileBytes)
			{
				plan.status = UInkAppendPlanStatus::RequiresFullSave;
				return plan;
			}
			DWORD hashError = ERROR_SUCCESS;
			if (!HashAppendPayload(plan.truncateOffset, encoded.bytes,
				plan.bytesSha256, hashError))
			{
				plan.status = UInkAppendPlanStatus::InvalidBatch;
				return plan;
			}
			plan.bytes = std::move(encoded.bytes);
			plan.status = UInkAppendPlanStatus::Ready;
			return plan;
		}
		catch (const std::bad_alloc&)
		{
			plan.status = UInkAppendPlanStatus::InvalidBatch;
			return plan;
		}
	}

	UInkAppendResult ExecuteUInkAppend(const std::wstring& path,
		const UInkAppendPlan& plan, const UInkAppendOptions& options)
	{
		UInkAppendResult result;
		result.lastTrustedBoundary = plan.truncateOffset;
		try
		{
			const UInkFileTestFaultInjection faults = SnapshotTestFaults();
			if (plan.status == UInkAppendPlanStatus::ResourcePackUnsupported)
			{
				result.status = UInkAppendStatus::ResourcePackUnsupported;
				result.diagnostics = plan.diagnostics;
				if (result.diagnostics.empty())
				{
					AddDiagnostic(result.diagnostics, UInkDiagnosticCode::ResourcePackUnsupported,
						UInkDiagnosticSeverity::Error, "media");
				}
				return result;
			}
			if (plan.status != UInkAppendPlanStatus::Ready || plan.bytes.empty()) return result;
			const std::optional<std::wstring> target = NormalizePath(path);
			const std::optional<std::wstring> source = NormalizePath(plan.sourcePath);
			if (!target || !source || !SamePath(*target, *source))
			{
				result.status = UInkAppendStatus::SourceChanged;
				result.systemError = ERROR_INVALID_NAME;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
					UInkDiagnosticSeverity::Error, "sourcePath", result.systemError);
				return result;
			}
			DWORD error = ERROR_SUCCESS;
			std::array<uint8_t, 32> digest = {};
			if (!HashAppendPayload(plan.truncateOffset, plan.bytes, digest, error) ||
				digest != plan.bytesSha256)
				return result;

			NamedTransactionGuard guard;
			if (!guard.Acquire(*target, error))
			{
				result.status = UInkAppendStatus::IoError;
				result.systemError = error;
				return result;
			}
			UniqueHandle handle = OpenWithRetry(*target, GENERIC_READ | GENERIC_WRITE, 0,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
				options.access, error);
			if (!handle)
			{
				result.status = IsTransientFileError(error) ? UInkAppendStatus::SourceChanged :
					UInkAppendStatus::IoError;
				result.systemError = error;
				if (result.status == UInkAppendStatus::SourceChanged)
				{
					AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
						UInkDiagnosticSeverity::Error, "source", error);
				}
				return result;
			}

			UInkSourceRevision currentRevision;
			if (!RevisionFromHandle(handle.Get(), currentRevision, error) ||
				currentRevision != plan.expectedRevision || plan.truncateOffset > currentRevision.length)
			{
				result.status = UInkAppendStatus::SourceChanged;
				result.systemError = error;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
					UInkDiagnosticSeverity::Error, "source", error);
				return result;
			}

			LARGE_INTEGER boundary = {};
			boundary.QuadPart = static_cast<LONGLONG>(plan.truncateOffset);
			if (!SetFilePointerEx(handle.Get(), boundary, nullptr, FILE_BEGIN) ||
				!SetEndOfFile(handle.Get()))
			{
				result.status = UInkAppendStatus::IoError;
				result.systemError = GetLastError();
				return result;
			}

			size_t position = 0;
			while (position < plan.bytes.size())
			{
				const bool injectedWriteFailure =
					static_cast<uint64_t>(position) >= faults.failWriteAfterBytes;
				if (injectedWriteFailure)
				{
					SetLastError(ERROR_WRITE_FAULT);
				}
				size_t requestedSize = injectedWriteFailure ? 0 :
					std::min<size_t>(plan.bytes.size() - position, 1024 * 1024);
				if (faults.failWriteAfterBytes != UINT64_MAX &&
					static_cast<uint64_t>(position) < faults.failWriteAfterBytes)
				{
					requestedSize = static_cast<size_t>(std::min<uint64_t>(requestedSize,
						faults.failWriteAfterBytes - static_cast<uint64_t>(position)));
				}
				const DWORD requested = static_cast<DWORD>(requestedSize);
				DWORD written = 0;
				if (injectedWriteFailure ||
					!WriteFile(handle.Get(), plan.bytes.data() + position, requested,
						&written, nullptr) || written == 0)
				{
					DWORD writeError = GetLastError();
					if (writeError == ERROR_SUCCESS) writeError = ERROR_WRITE_FAULT;
					AddDiagnostic(result.diagnostics, UInkDiagnosticCode::WriteFailed,
						UInkDiagnosticSeverity::Error, "append.write", writeError);
					const bool rolledBack = !faults.failRollback &&
						SetFilePointerEx(handle.Get(), boundary, nullptr, FILE_BEGIN) &&
						SetEndOfFile(handle.Get());
					if (rolledBack)
					{
						result.observedLength = plan.truncateOffset;
						bool rollbackDurable = true;
						DWORD flushError = ERROR_SUCCESS;
						if (options.durability == UInkAppendDurability::Durable)
						{
							if (faults.failFlush)
							{
								rollbackDurable = false;
								flushError = ERROR_WRITE_FAULT;
							}
							else if (!FlushFileBuffers(handle.Get()))
							{
								rollbackDurable = false;
								flushError = GetLastError();
							}
						}
						if (!rollbackDurable)
						{
							result.status = UInkAppendStatus::PartialCommitRequiresRecovery;
							result.systemError = flushError;
							AddDiagnostic(result.diagnostics, UInkDiagnosticCode::FlushFailed,
								UInkDiagnosticSeverity::Error, "append.rollback.flush", flushError);
						}
						else result.status = plan.truncateOffset < currentRevision.length ?
							UInkAppendStatus::TailRepairedNoAppend :
							UInkAppendStatus::WriteFailedRolledBack;
					}
					else
					{
						const DWORD rollbackError = faults.failRollback ?
							ERROR_WRITE_FAULT : GetLastError();
						result.status = UInkAppendStatus::PartialCommitRequiresRecovery;
						LARGE_INTEGER length = {};
						if (GetFileSizeEx(handle.Get(), &length))
							result.observedLength = static_cast<uint64_t>(length.QuadPart);
						AddDiagnostic(result.diagnostics, UInkDiagnosticCode::RollbackFailed,
							UInkDiagnosticSeverity::Error, "append.rollback", rollbackError);
					}
					if (result.systemError == ERROR_SUCCESS) result.systemError = writeError;
					return result;
				}
				position += written;
			}

			result.observedLength = plan.truncateOffset + plan.bytes.size();
			result.lastTrustedBoundary = result.observedLength;
			if (options.durability == UInkAppendDurability::Durable && faults.failFlush)
			{
				result.status = UInkAppendStatus::WrittenNotDurable;
				result.systemError = ERROR_WRITE_FAULT;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::FlushFailed,
					UInkDiagnosticSeverity::Error, "append.flush", result.systemError);
			}
			else if (options.durability == UInkAppendDurability::Durable &&
				!FlushFileBuffers(handle.Get()))
			{
				result.status = UInkAppendStatus::WrittenNotDurable;
				result.systemError = GetLastError();
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::FlushFailed,
					UInkDiagnosticSeverity::Error, "append.flush", result.systemError);
			}
			else result.status = options.durability == UInkAppendDurability::Durable ?
				UInkAppendStatus::CommittedDurable : UInkAppendStatus::CommittedBuffered;

			UInkSourceRevision writtenRevision;
			if (!RevisionFromHandle(handle.Get(), writtenRevision, error))
			{
				result.status = UInkAppendStatus::PartialCommitRequiresRecovery;
				result.systemError = error;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::IoError,
					UInkDiagnosticSeverity::Error, "append.writtenRevision", error);
				return result;
			}

			// 关闭写句柄后再取 revision，并核对内容身份以发现提交后的竞争修改。
			handle = UniqueHandle{};
			UInkSourceRevision revision;
			if (!faults.failCommittedRevisionValidation &&
				ReadRevisionAtPath(*target, options.access, revision, error) &&
				SameRevisionIdentityAndContent(writtenRevision, revision)) result.revision = revision;
			else if (result.status == UInkAppendStatus::CommittedDurable ||
				result.status == UInkAppendStatus::CommittedBuffered ||
				result.status == UInkAppendStatus::WrittenNotDurable)
			{
				if (error == ERROR_SUCCESS) error = ERROR_FILE_INVALID;
				result.status = UInkAppendStatus::PartialCommitRequiresRecovery;
				result.systemError = error;
				AddDiagnostic(result.diagnostics, UInkDiagnosticCode::SourceChanged,
					UInkDiagnosticSeverity::Error, "append.commitRevision", error);
			}
			return result;
		}
		catch (const std::bad_alloc&)
		{
			result.status = UInkAppendStatus::InvalidPlan;
			return result;
		}
	}

	void SetUInkFileTestFaultInjection(const UInkFileTestFaultInjection& faults) noexcept
	{
		gFailWriteAfterBytes.store(faults.failWriteAfterBytes, std::memory_order_relaxed);
		gFailFlush.store(faults.failFlush, std::memory_order_relaxed);
		gFailSelfValidation.store(faults.failSelfValidation, std::memory_order_relaxed);
		gFailCommit.store(faults.failCommit, std::memory_order_relaxed);
		gFailRollback.store(faults.failRollback, std::memory_order_relaxed);
		gFailCommittedRevisionValidation.store(faults.failCommittedRevisionValidation,
			std::memory_order_relaxed);
	}

	void ResetUInkFileTestFaultInjection() noexcept
	{
		SetUInkFileTestFaultInjection({});
	}
}
