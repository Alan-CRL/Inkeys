module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module draw3.uink_store;

namespace draw3::uink
{
	namespace
	{
		static_assert(kUInkStoreMaxPageBytes == UInkReadLimits{}.maxTopLevelObjectBytes);
		static_assert(kUInkStoreMaxPageBytes <= (std::numeric_limits<DWORD>::max)());
		static_assert(UInkReadLimits{}.maxFileBytes <= (std::numeric_limits<DWORD>::max)());
		constexpr uint32_t kMaxPathSuffix = 100;

		enum Fault : unsigned
		{
			ShortWrite = 1 << 0,
			FailFlush = 1 << 1,
			FailTruncate = 1 << 2,
			FailMove = 1 << 3,
			FailReopen = 1 << 4,
			FailDeleteTemp = 1 << 5,
		};
		std::atomic<unsigned> gTestFaults{ 0 };

		bool TakeFault(Fault fault) noexcept
		{
			return (gTestFaults.fetch_and(~static_cast<unsigned>(fault),
				std::memory_order_relaxed) & fault) != 0;
		}

		class FileHandle
		{
		public:
			explicit FileHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
			~FileHandle() noexcept { Close(); }
			FileHandle(const FileHandle&) = delete;
			FileHandle& operator=(const FileHandle&) = delete;

			explicit operator bool() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
			HANDLE Get() const noexcept { return handle_; }

			DWORD CreateNew(const std::wstring& path) noexcept
			{
				handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
					CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				return *this ? ERROR_SUCCESS : GetLastError();
			}

			DWORD OpenWriter(const std::wstring& path) noexcept
			{
				if (TakeFault(FailReopen)) return ERROR_SHARING_VIOLATION;
				handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				return *this ? ERROR_SUCCESS : GetLastError();
			}

			DWORD Close() noexcept
			{
				if (!*this) return ERROR_SUCCESS;
				if (!CloseHandle(handle_)) return GetLastError();
				handle_ = INVALID_HANDLE_VALUE;
				return ERROR_SUCCESS;
			}

		private:
			HANDLE handle_;
		};

		bool NameExists(DWORD error) noexcept
		{
			return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
		}

		size_t FileNameStart(const std::wstring& path) noexcept
		{
			const size_t slash = path.find_last_of(L"\\/");
			if (slash != std::wstring::npos) return slash + 1;
			return path.size() >= 2 && path[1] == L':' ? 2 : 0;
		}

		std::wstring WithSuffix(const std::wstring& path, uint32_t suffix)
		{
			std::wstring result = path;
			const size_t dot = path.find_last_of(L'.');
			const size_t insertAt = dot != std::wstring::npos && dot > FileNameStart(path) ?
				dot : path.size();
			result.insert(insertAt, L"_" + std::to_wstring(suffix));
			return result;
		}

		class TempFile
		{
		public:
			~TempFile() noexcept { Cleanup(); }

			DWORD Cleanup() noexcept
			{
				file.Close();
				if (!ownsPath_) return ERROR_SUCCESS;
				ownsPath_ = false;
				if (TakeFault(FailDeleteTemp)) return ERROR_SHARING_VIOLATION;
				return DeleteFileW(path_.c_str()) ? ERROR_SUCCESS : GetLastError();
			}

			DWORD Create(const std::wstring& basePath)
			{
				constexpr std::wstring_view prefix = L".~";
				constexpr std::wstring_view suffix = L".tmp";
				constexpr std::wstring_view hex = L"0123456789abcdef";
				constexpr size_t markedDigits = 12;
				constexpr unsigned maxAttempts = 32;
				const size_t start = FileNameStart(basePath);
				auto target = std::wstring_view(basePath).substr(start);
				if (target.empty()) return ERROR_INVALID_NAME;

				// 叶名不得加长绝对路径。过短的目标只使用十六进制字符。
				const bool marked = target.size() >= prefix.size() + markedDigits + suffix.size();
				const size_t digits = marked ? markedDigits : (std::min)(size_t{ 8 }, target.size());
				while (!target.empty() && (target.back() == L'.' || target.back() == L' ')) target.remove_suffix(1);
				for (unsigned attempt = 0; attempt < maxAttempts; ++attempt)
				{
					const auto guid = CreateUInkGuid();
					if (!guid) return ERROR_GEN_FAILURE;
					path_.assign(basePath, 0, start);
					if (marked) path_ += prefix;
					for (size_t i = 0; i < digits; ++i)
						path_ += hex[(guid->Bytes()[i / 2] >> (i % 2 == 0 ? 4 : 0)) & 0x0f];
					if (marked) path_ += suffix;
					const auto leaf = std::wstring_view(path_).substr(start);
					// 避免短名随机到目标本身；CREATE_NEW 处理已有文件和跨进程碰撞。
					if (leaf.size() == target.size() && CompareStringOrdinal(leaf.data(),
						static_cast<int>(leaf.size()), target.data(), static_cast<int>(target.size()), TRUE) == CSTR_EQUAL)
						continue;
					const DWORD error = file.CreateNew(path_);
					ownsPath_ = error == ERROR_SUCCESS;
					if (!NameExists(error)) return error;
				}
				return ERROR_FILE_EXISTS;
			}

			DWORD Publish(const std::wstring& target, DWORD flags) noexcept
			{
				if (TakeFault(FailMove)) return ERROR_ACCESS_DENIED;
				if (!MoveFileExW(path_.c_str(), target.c_str(), flags | MOVEFILE_WRITE_THROUGH))
					return GetLastError();
				ownsPath_ = false;
				return ERROR_SUCCESS;
			}

			FileHandle file;

		private:
			std::wstring path_;
			bool ownsPath_ = false;
		};

		DWORD Seek(HANDLE handle, uint64_t offset) noexcept
		{
			if (offset > static_cast<uint64_t>((std::numeric_limits<LONGLONG>::max)()))
				return ERROR_INVALID_PARAMETER;
			LARGE_INTEGER position{};
			position.QuadPart = static_cast<LONGLONG>(offset);
			return SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) ?
				ERROR_SUCCESS : GetLastError();
		}

		DWORD FileSize(HANDLE handle, uint64_t& size) noexcept
		{
			LARGE_INTEGER length{};
			if (!GetFileSizeEx(handle, &length)) return GetLastError();
			if (length.QuadPart < 0) return ERROR_INVALID_DATA;
			size = static_cast<uint64_t>(length.QuadPart);
			return ERROR_SUCCESS;
		}

		DWORD ReadExact(HANDLE handle, std::span<std::byte> bytes, uint64_t* bytesRead = nullptr) noexcept
		{
			if (bytes.empty()) return ERROR_SUCCESS;
			const auto requested = static_cast<DWORD>(bytes.size());
			DWORD read = 0;
			if (!ReadFile(handle, bytes.data(), requested, &read, nullptr)) return GetLastError();
			if (bytesRead) *bytesRead = read;
			return read == requested ? ERROR_SUCCESS : ERROR_HANDLE_EOF;
		}

		DWORD WriteExact(HANDLE handle, std::span<const std::byte> bytes) noexcept
		{
			const auto requested = static_cast<DWORD>(bytes.size());
			const DWORD length = TakeFault(ShortWrite) ? requested / 2 : requested;
			DWORD written = 0;
			if (!WriteFile(handle, bytes.data(), length, &written, nullptr)) return GetLastError();
			return written == requested ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
		}

		DWORD Flush(HANDLE handle) noexcept
		{
			if (TakeFault(FailFlush)) return ERROR_WRITE_FAULT;
			return FlushFileBuffers(handle) ? ERROR_SUCCESS : GetLastError();
		}

		DWORD Truncate(HANDLE handle, uint64_t offset) noexcept
		{
			if (TakeFault(FailTruncate)) return ERROR_WRITE_FAULT;
			if (const DWORD error = Seek(handle, offset)) return error;
			return SetEndOfFile(handle) ? ERROR_SUCCESS : GetLastError();
		}

		void AddWriteDiagnostic(UInkStoreWriteResult& result, UInkDiagnosticCode code,
			const char* field, DWORD error, uint64_t offset = 0) noexcept
		{
			try
			{
				result.diagnostics.push_back(
					{ code, UInkDiagnosticSeverity::Error, offset, 0, field, error });
			}
			catch (...) {} // 分配失败时仍保留操作状态和 Win32 错误。
		}

		UInkStoreWriteResult WriteFailure(UInkDiagnosticCode code, const char* field,
			DWORD error = ERROR_SUCCESS, uint64_t offset = 0) noexcept
		{
			UInkStoreWriteResult result;
			result.systemError = error;
			result.failureStage = field;
			AddWriteDiagnostic(result, code, field, error, offset);
			return result;
		}

		template <typename Result>
		Result WithTempCleanup(TempFile& temp, Result result) noexcept
		{
			// 清理诊断附在主错误之后，结果中的 Win32 错误仍指向原操作。
			if (const DWORD error = temp.Cleanup())
				AddWriteDiagnostic(result, UInkDiagnosticCode::IoError, "temp.cleanup", error);
			return result;
		}

		UInkStoreWriteResult EncodeFailure(UInkEncodeResult& encoded, const char* stage = nullptr) noexcept
		{
			UInkStoreWriteResult result;
			result.failureStage = stage;
			result.diagnostics = std::move(encoded.diagnostics);
			return result;
		}

		UInkStoreWriteResult CopyRange(HANDLE source, HANDLE target,
			uint64_t offset, uint64_t length) noexcept
		{
			if (length == 0) return { true };
			if (const DWORD error = Seek(source, offset))
				return WriteFailure(UInkDiagnosticCode::IoError, "compact.seek", error, offset);
			std::array<std::byte, 64 * 1024> buffer;
			while (length != 0)
			{
				const auto size = static_cast<size_t>((std::min)(length, uint64_t{ buffer.size() }));
				const auto chunk = std::span(buffer).first(size);
				if (const DWORD error = ReadExact(source, chunk))
					return WriteFailure(UInkDiagnosticCode::IoError, "compact.read", error, offset);
				if (const DWORD error = WriteExact(target, chunk))
					return WriteFailure(UInkDiagnosticCode::WriteFailed, "compact.write", error, offset);
				offset += size;
				length -= size;
			}
			return { true };
		}

		UInkEncodeResult EncodeStorePage(const UInkCanvas& canvas, const UInkStreamContext& context)
		{
			UInkReadLimits limits;
			limits.maxFileBytes = kUInkStoreMaxPageBytes;
			return EncodeUInkPage(canvas, context.usesImplicitWorkspace, context.usesImplicitDevice, limits);
		}

		UInkStoreWriteResult ValidateStorePage(std::span<const std::byte> bytes,
			const UInkStreamContext& context, uint64_t offset, const UInkReadLimits& limits = {}) noexcept
		{
			// 写盘前用页读限制在内存中解码一次，保证写得进去就读得回来。
			uint64_t canvasCount = 0;
			auto decoded = DecodeUInkStream(bytes, limits, context, offset, canvasCount);
			if (decoded.status == UInkReadStatus::Complete && decoded.document &&
				canvasCount == 1 && decoded.document->canvases.size() == 1 &&
				!decoded.provenance.containsInvalidCompleteBlocks)
				return { true };

			UInkStoreWriteResult failure;
			failure.failureStage = "page.decode";
			failure.diagnostics = std::move(decoded.diagnostics);
			AddWriteDiagnostic(failure, UInkDiagnosticCode::SelfValidationFailed,
				failure.failureStage, ERROR_SUCCESS, decoded.failedObjectOffset.value_or(offset));
			return failure;
		}

		UInkStoreWriteResult ResolvePublishPath(const std::wstring& basePath, std::wstring& absolutePath)
		{
			if (FileNameStart(basePath) == basePath.size() || basePath.find(L'\0') != std::wstring::npos)
				return WriteFailure(UInkDiagnosticCode::IoError, "file.path", ERROR_INVALID_NAME);

			// 固定当前目录和相对路径的解析结果，后续文件操作共用这个绝对路径。
			std::array<wchar_t, 32768> fullPath;
			const DWORD length = GetFullPathNameW(basePath.c_str(), static_cast<DWORD>(fullPath.size()),
				fullPath.data(), nullptr);
			if (length == 0 || length >= fullPath.size())
				return WriteFailure(UInkDiagnosticCode::IoError, "file.path",
					length == 0 ? GetLastError() : ERROR_FILENAME_EXCED_RANGE);
			absolutePath.assign(fullPath.data(), length);
			return { true };
		}

		struct InitialFile
		{
			UInkHeader header;
			UInkStreamContext context;
			bool requiresSlideBinding = false;
			UInkEncodeResult prefix;
			UInkEncodeResult page;
		};

		UInkStoreWriteResult PrepareInitialFile(const UInkHeader& header,
			const std::optional<UInkHeaderExtension>& headerExtension, const UInkCanvas& firstPage,
			InitialFile& initial, const UInkReadLimits* limits = nullptr, uint64_t* headerLength = nullptr)
		{
			auto& context = initial.context;
			context.version = kUInkVersion;
			context.usesImplicitDevice = !headerExtension || headerExtension->devices.empty();
			context.usesImplicitWorkspace = !headerExtension || headerExtension->workspaces.empty();

			// 单页文件只接受一组 Workspace/Device；显式注册的 Workspace 首次保存就必须有页。
			if (firstPage.layerIndex != 0 || (!context.usesImplicitWorkspace &&
				(headerExtension->workspaces.size() != 1 ||
					firstPage.workspaceGuid != headerExtension->workspaces.front().guid)) ||
				(!context.usesImplicitDevice && std::none_of(headerExtension->devices.begin(),
					headerExtension->devices.end(), [&](const UInkDevice& device)
					{
						return firstPage.deviceGuid == device.guid;
					})))
				return WriteFailure(UInkDiagnosticCode::InvalidFieldValue, "canvas.identity");

			initial.requiresSlideBinding = !context.usesImplicitWorkspace &&
				headerExtension->workspaces.front().workspaceType == 2;
			if (initial.requiresSlideBinding && !HasValidUInkPresentationBinding(firstPage))
				return WriteFailure(UInkDiagnosticCode::InvalidFieldValue, "canvas.slideId");

			initial.header = header;
			initial.header.pageNum = 1;
			initial.header.deviceNum = context.usesImplicitDevice ? 1 :
				static_cast<uint32_t>(headerExtension->devices.size());
			initial.header.workspaceNum = 1;
			initial.prefix = EncodeUInkHeaderPrefix(initial.header, headerExtension);
			if (initial.prefix.status != UInkEncodeStatus::Success) return EncodeFailure(initial.prefix, "create.encode_header");

			if (headerLength)
			{
				*headerLength = initial.prefix.bytes.size();
				if (headerExtension)
				{
					auto headerBytes = EncodeUInkHeaderPrefix(initial.header, std::nullopt);
					if (headerBytes.status != UInkEncodeStatus::Success) return EncodeFailure(headerBytes, "create.encode_header");
					*headerLength = headerBytes.bytes.size();
				}
			}

			uint64_t pageLimit = kUInkStoreMaxPageBytes;
			if (limits)
			{
				const uint64_t fileLimit = (std::min)(limits->maxFileBytes, UInkReadLimits{}.maxFileBytes);
				if (initial.prefix.bytes.size() >= fileLimit)
					return WriteFailure(UInkDiagnosticCode::LimitExceeded, "file.bytes");

				auto pageLimits = *limits;
				pageLimit = (std::min)(pageLimit, fileLimit - initial.prefix.bytes.size());
				pageLimits.maxFileBytes = pageLimit;
				initial.page = EncodeUInkPage(firstPage, context.usesImplicitWorkspace, context.usesImplicitDevice,
					pageLimits);
			}
			else
				initial.page = EncodeStorePage(firstPage, context);
			if (initial.page.status != UInkEncodeStatus::Success) return EncodeFailure(initial.page, "create.encode_page");
			if (initial.page.bytes.size() > pageLimit)
				return WriteFailure(UInkDiagnosticCode::LimitExceeded, "page.bytes");

			context.headerExtension = headerExtension;
			return ValidateStorePage(initial.page.bytes, context, initial.prefix.bytes.size(),
				limits ? *limits : UInkReadLimits{});
		}

		UInkStoreWriteResult ValidateSnapshotFile(const InitialFile& initial, const UInkReadLimits& limits)
		{
			// 整读时 Header、可选扩展和页共用同一份对象数量、模型及字节预算。
			std::vector<std::byte> bytes;
			bytes.reserve(initial.prefix.bytes.size() + initial.page.bytes.size());
			bytes.insert(bytes.end(), initial.prefix.bytes.begin(), initial.prefix.bytes.end());
			bytes.insert(bytes.end(), initial.page.bytes.begin(), initial.page.bytes.end());
			auto decoded = DecodeUInkStream(bytes, limits);
			if (decoded.summary.outcome == UInkReadOutcome::Complete &&
				decoded.document && decoded.document->canvases.size() == 1)
				return { true };

			UInkStoreWriteResult failure;
			failure.failureStage = "file.decode";
			failure.diagnostics = std::move(decoded.diagnostics);
			AddWriteDiagnostic(failure, UInkDiagnosticCode::SelfValidationFailed,
				failure.failureStage, ERROR_SUCCESS, decoded.failedObjectOffset.value_or(0));
			return failure;
		}

		UInkStoreWriteResult PublishEncodedFile(TempFile& temp, const std::wstring& absolutePath,
			std::span<const std::byte> prefix, std::span<const std::byte> page, std::wstring* writerPath = nullptr)
		{
			if (const DWORD error = temp.Create(absolutePath))
				return WriteFailure(UInkDiagnosticCode::IoError, "create.temp", error);
			if (const DWORD error = WriteExact(temp.file.Get(), prefix))
				return WriteFailure(UInkDiagnosticCode::WriteFailed, "create.header", error);
			if (const DWORD error = WriteExact(temp.file.Get(), page))
				return WriteFailure(UInkDiagnosticCode::WriteFailed, "create.page", error, prefix.size());
			if (const DWORD error = Flush(temp.file.Get()))
				return WriteFailure(UInkDiagnosticCode::FlushFailed, "create.flush", error);
			if (const DWORD error = temp.file.Close())
				return WriteFailure(UInkDiagnosticCode::IoError, "create.closeTemp", error);

			for (uint32_t suffix = 0; ; ++suffix)
			{
				// 先准备结果路径，确保发布成功后只需移动字符串。
				auto publishedPath = suffix == 0 ? absolutePath : WithSuffix(absolutePath, suffix);
				if (writerPath) *writerPath = publishedPath;
				const DWORD error = temp.Publish(publishedPath, 0);
				if (error == ERROR_SUCCESS)
				{
					UInkStoreWriteResult result{ true };
					result.published = true;
					result.publishedPath.swap(publishedPath);
					return result;
				}
				if (!NameExists(error) || suffix == kMaxPathSuffix)
					return WriteFailure(UInkDiagnosticCode::IoError, "create.publish", error);
			}
		}

		UInkPageRange MakeRange(const UInkCanvas& canvas, uint64_t offset,
			uint64_t length, uint64_t sequence)
		{
			UInkPageRange range{ offset, length, canvas.pageIndex, sequence };
			if (!canvas.content.empty())
			{
				std::visit([&](const auto& last)
				{
					range.nextContentId = uint64_t{ last.contentId } + 1;
					range.lastUndoId = last.undoId;
				}, canvas.content.back());
			}
			return range;
		}

		void SetLastPage(UInkPageDirectory& directory, const UInkGuid& guid,
			const UInkPageRange& range) noexcept
		{
			directory.lastPageGuid = guid;
			directory.nextContentId = range.nextContentId;
			directory.lastUndoId = range.lastUndoId;
		}

		bool PageOrder(uint32_t leftIndex, uint64_t leftSequence,
			uint32_t rightIndex, uint64_t rightSequence) noexcept
		{
			return std::tie(leftIndex, leftSequence) < std::tie(rightIndex, rightSequence);
		}

		void FailRead(UInkReadResult& result, UInkStoreReadFacts* facts, UInkReadStatus status, UInkDiagnosticCode code,
			const char* field, const UInkReadLimits& limits, DWORD error = ERROR_SUCCESS,
			uint64_t offset = 0) noexcept
		{
			if (facts)
			{
				facts->failureStage = field;
				facts->failureCode = code;
				facts->systemError = error;
			}
			result.status = status;
			result.document.reset();
			result.summary.outcome = UInkReadOutcome::Failed;
			auto& count = result.summary.diagnosticCounts[static_cast<size_t>(code)];
			if (count.count == 0) count = { code, 0, offset, 0 };
			++count.count;
			if (result.diagnostics.size() >= limits.maxDiagnostics) return;
			try
			{
				result.diagnostics.push_back(
					{ code, UInkDiagnosticSeverity::Fatal, offset, 0, field, error });
			}
			catch (...) {} // 诊断汇总在无法分配诊断列表时仍可用。
		}

		bool ReadRange(const std::wstring& path, const UInkPageRange* range, uint64_t maxBytes,
			const UInkReadLimits& limits, std::vector<std::byte>& bytes, UInkReadResult& result, UInkStoreReadFacts* facts)
		{
			using Clock = std::chrono::steady_clock;
			// 先构造计时器，让文件句柄在结束采样前析构，异常和早退也同样计时。
			struct ReadTimer
			{
				UInkStoreReadFacts* facts;
				Clock::time_point started;
				~ReadTimer() noexcept
				{
					if (facts) facts->ioMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
				}
			} timer{ facts, facts ? Clock::now() : Clock::time_point{} };
			const FileHandle file(CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			const uint64_t offset = range ? range->offset : 0;
			if (!file)
			{
				FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::IoError,
					"file.open", limits, GetLastError(), offset);
				return false;
			}
			uint64_t fileSize = 0;
			if (const DWORD error = FileSize(file.Get(), fileSize))
			{
				FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::IoError,
					"file.size", limits, error, offset);
				return false;
			}
			const uint64_t count = range ? range->length : fileSize;
			if (offset > fileSize || count > fileSize - offset)
			{
				FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::IoError,
					"page.range", limits, ERROR_HANDLE_EOF, offset);
				return false;
			}
			if (count > maxBytes)
			{
				FailRead(result, facts, UInkReadStatus::LimitExceeded, UInkDiagnosticCode::LimitExceeded,
					range ? "page.bytes" : "file.bytes", limits, ERROR_SUCCESS, offset);
				return false;
			}
			if (const DWORD error = Seek(file.Get(), offset))
			{
				FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::IoError,
					"file.seek", limits, error, offset);
				return false;
			}
			bytes.resize(static_cast<size_t>(count));
			if (const DWORD error = ReadExact(file.Get(), bytes, facts ? &facts->bytesRead : nullptr))
			{
				FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::IoError,
					"file.read", limits, error, offset);
				return false;
			}
			return true;
		}

		using GuidBytes = std::array<uint8_t, 16>;

		std::optional<GuidBytes> GuidKey(const std::optional<UInkGuid>& guid)
		{
			return guid ? std::optional<GuidBytes>{ guid->Bytes() } : std::nullopt;
		}

		void KeepLatestPages(std::vector<UInkCanvas>& canvases)
		{
			using CanvasKey = std::tuple<std::optional<GuidBytes>, std::optional<GuidBytes>,
				GuidBytes, uint32_t>;
			std::map<CanvasKey, size_t> last;
			std::vector<size_t> order;
			for (size_t index = 0; index < canvases.size(); ++index)
			{
				const auto& canvas = canvases[index];
				// 缺少有效唯一键的临时画布不能用默认值互相覆盖。
				if (canvas.pageGuid.IsZero() || canvas.temporaryLayer ||
					(canvas.temporaryWorkspace && !canvas.workspaceGuid) ||
					(canvas.temporaryDevice && !canvas.deviceGuid))
				{
					order.push_back(index);
					continue;
				}
				last.insert_or_assign(CanvasKey{ GuidKey(canvas.workspaceGuid), GuidKey(canvas.deviceGuid),
					canvas.pageGuid.Bytes(), canvas.layerIndex }, index);
			}

			order.reserve(order.size() + last.size());
			for (const auto& entry : last) order.push_back(entry.second);
			std::sort(order.begin(), order.end(), [&](size_t left, size_t right)
			{
				return PageOrder(canvases[left].pageIndex, left, canvases[right].pageIndex, right);
			});

			std::vector<UInkCanvas> unique;
			unique.reserve(order.size());
			for (const size_t index : order) unique.push_back(std::move(canvases[index]));
			canvases.swap(unique);
		}
	}

	struct UInkPagedFileWriter::Impl
	{
		std::wstring path;
		FileHandle file;
		UInkHeader header;
		UInkPageDirectory directory;
		std::optional<UInkGuid> workspaceGuid;
		std::optional<UInkGuid> deviceGuid;
		uint64_t headerLength = 0;
		uint64_t extensionLength = 0;
		uint64_t fileBytes = 0;
		uint64_t nextSequence = 0;
		bool requiresSlideBinding = false;
		bool tailUnconfirmed = false;
	};

	UInkPagedFileWriter::UInkPagedFileWriter() : impl_(std::make_unique<Impl>()) {}
	UInkPagedFileWriter::~UInkPagedFileWriter() noexcept { Close(); }

	UInkStoreWriteResult PublishUInkSnapshotFile(const std::wstring& basePath, const UInkHeader& header,
		const std::optional<UInkHeaderExtension>& headerExtension, const UInkCanvas& canvas,
		const UInkReadLimits& limits) noexcept
	{
		TempFile temp;
		try
		{
			std::wstring absolutePath;
			auto result = ResolvePublishPath(basePath, absolutePath);
			if (!result.success) return result;

			InitialFile initial;
			result = PrepareInitialFile(header, headerExtension, canvas, initial, &limits);
			if (!result.success) return result;
			result = ValidateSnapshotFile(initial, limits);
			if (!result.success) return result;
			return WithTempCleanup(temp, PublishEncodedFile(temp, absolutePath, initial.prefix.bytes, initial.page.bytes));
		}
		catch (const std::bad_alloc&)
		{
			return WithTempCleanup(temp,
				WriteFailure(UInkDiagnosticCode::LimitExceeded, "allocation", ERROR_NOT_ENOUGH_MEMORY));
		}
		catch (...)
		{
			return WithTempCleanup(temp, WriteFailure(UInkDiagnosticCode::WriteFailed, "create.snapshot"));
		}
	}

	std::unique_ptr<UInkPagedFileWriter> UInkPagedFileWriter::Create(const std::wstring& basePath,
		const UInkHeader& header, const std::optional<UInkHeaderExtension>& headerExtension,
		const UInkCanvas& firstPage, UInkStoreWriteResult& result) noexcept
	{
		TempFile temp;
		try
		{
			std::wstring absolutePath;
			result = ResolvePublishPath(basePath, absolutePath);
			if (!result.success) return {};

			auto writer = std::unique_ptr<UInkPagedFileWriter>(new UInkPagedFileWriter());
			auto& state = *writer->impl_;
			InitialFile initial;
			result = PrepareInitialFile(header, headerExtension, firstPage, initial, nullptr, &state.headerLength);
			if (!result.success) return {};

			state.header = std::move(initial.header);
			state.directory.context = std::move(initial.context);
			state.requiresSlideBinding = initial.requiresSlideBinding;
			state.extensionLength = initial.prefix.bytes.size() - state.headerLength;

			state.workspaceGuid = firstPage.workspaceGuid;
			state.deviceGuid = firstPage.deviceGuid;
			const auto range = MakeRange(firstPage, initial.prefix.bytes.size(), initial.page.bytes.size(), 0);
			state.directory.pages.emplace(firstPage.pageGuid.Bytes(), range);
			SetLastPage(state.directory, firstPage.pageGuid, range);
			state.fileBytes = range.offset + range.length;
			state.nextSequence = 1;

			result = WithTempCleanup(temp,
				PublishEncodedFile(temp, absolutePath, initial.prefix.bytes, initial.page.bytes, &state.path));
			if (!result.success) return {};

			// 分页写入器仍需重开追加句柄。重开失败也须报告已经提交的路径。
			if (const DWORD openError = state.file.OpenWriter(result.publishedPath))
			{
				auto failure = WriteFailure(UInkDiagnosticCode::IoError, "create.reopen", openError);
				failure.published = true;
				failure.publishedPath.swap(result.publishedPath);
				result = std::move(failure);
				return {};
			}
			return writer;
		}
		catch (const std::bad_alloc&)
		{
			result = WithTempCleanup(temp,
				WriteFailure(UInkDiagnosticCode::LimitExceeded, "allocation", ERROR_NOT_ENOUGH_MEMORY));
			return {};
		}
		catch (...)
		{
			result = WithTempCleanup(temp, WriteFailure(UInkDiagnosticCode::WriteFailed, "create"));
			return {};
		}
	}

	UInkStoreWriteResult UInkPagedFileWriter::AppendPage(const UInkCanvas& canvas) noexcept
	{
		auto& state = *impl_;
		if (IsSealed()) return WriteFailure(UInkDiagnosticCode::IoError, "writer.sealed", ERROR_INVALID_HANDLE);
		try
		{
			if (canvas.layerIndex != 0 || canvas.workspaceGuid != state.workspaceGuid ||
				canvas.deviceGuid != state.deviceGuid)
				return WriteFailure(UInkDiagnosticCode::InvalidFieldValue, "canvas.identity");
			if (state.requiresSlideBinding && !HasValidUInkPresentationBinding(canvas))
				return WriteFailure(UInkDiagnosticCode::InvalidFieldValue, "canvas.slideId");

			auto page = EncodeStorePage(canvas, state.directory.context);
			if (page.status != UInkEncodeStatus::Success) return EncodeFailure(page, "append.encode_page");
			if (page.bytes.size() > kUInkStoreMaxPageBytes)
				return WriteFailure(UInkDiagnosticCode::LimitExceeded, "page.bytes");

			if (auto validation = ValidateStorePage(page.bytes, state.directory.context, state.fileBytes);
				!validation.success) return validation;

			const auto range = MakeRange(canvas, state.fileBytes, page.bytes.size(), state.nextSequence);
			auto& pages = state.directory.pages;
			const auto previous = pages.find(canvas.pageGuid.Bytes());
			// 先分配新目录项，写入后仅转移节点，避免落盘成功却无法登记。
			decltype(state.directory.pages) staged;
			if (previous == pages.end()) staged.emplace(canvas.pageGuid.Bytes(), range);

			const HANDLE file = state.file.Get();
			if (const DWORD error = Seek(file, range.offset))
				return WriteFailure(UInkDiagnosticCode::IoError, "append.seek", error, range.offset);
			DWORD error = WriteExact(file, page.bytes);
			auto code = UInkDiagnosticCode::WriteFailed;
			if (error == ERROR_SUCCESS)
			{
				code = UInkDiagnosticCode::FlushFailed;
				error = Flush(file);
			}
			if (error != ERROR_SUCCESS)
			{
				auto failure = WriteFailure(code, "append.page", error, range.offset);
				if (code == UInkDiagnosticCode::FlushFailed) failure.failureStage = "append.flush";
				// 截断后立即刷盘，确认回退的末尾已持久化。
				const DWORD truncateError = Truncate(file, range.offset);
				const DWORD flushError = truncateError == ERROR_SUCCESS ? Flush(file) : ERROR_SUCCESS;
				if (truncateError != ERROR_SUCCESS || flushError != ERROR_SUCCESS)
				{
					state.tailUnconfirmed = true;
					// 单独保留回滚错误，日志不依赖明细分配，也不覆盖主写入错误。
					failure.rollbackStage = truncateError ? "append.truncate" : "append.rollbackFlush";
					failure.rollbackSystemError = truncateError ? truncateError : flushError;
					AddWriteDiagnostic(failure, UInkDiagnosticCode::RollbackFailed,
						failure.rollbackStage, failure.rollbackSystemError, range.offset);
					// 实测失败时 FileSize 保留原来的已确认长度。
					if (const DWORD sizeError = FileSize(file, state.fileBytes))
						AddWriteDiagnostic(failure, UInkDiagnosticCode::IoError,
							"append.sizeAfterRollback", sizeError, range.offset);
					Close();
				}
				return failure;
			}

			if (previous != pages.end())
			{
				state.directory.supersededBytes += previous->second.length;
				previous->second = range;
			}
			else pages.insert(staged.extract(staged.begin()));
			SetLastPage(state.directory, canvas.pageGuid, range);
			state.fileBytes += range.length;
			++state.nextSequence;
			return { true };
		}
		catch (const std::bad_alloc&)
		{
			return WriteFailure(UInkDiagnosticCode::LimitExceeded, "allocation", ERROR_NOT_ENOUGH_MEMORY);
		}
		catch (...)
		{
			return WriteFailure(UInkDiagnosticCode::WriteFailed, "append");
		}
	}

	UInkStoreCompactResult UInkPagedFileWriter::Compact() noexcept
	{
		auto& state = *impl_;
		if (IsSealed()) return { WriteFailure(UInkDiagnosticCode::IoError, "writer.sealed", ERROR_INVALID_HANDLE) };
		TempFile temp;
		try
		{
			if (state.directory.pages.size() > (std::numeric_limits<uint32_t>::max)())
				return { WriteFailure(UInkDiagnosticCode::LimitExceeded, "header.pageNum") };
			UInkHeader header = state.header;
			header.pageNum = static_cast<uint32_t>(state.directory.pages.size());
			const auto unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			header.time = unixSeconds > 0 ? static_cast<uint64_t>(unixSeconds) : 0;
			// Device 和 Workspace 注册表始终固定，创建时已计算其统计。
			auto prefix = EncodeUInkHeaderPrefix(header, std::nullopt);
			if (prefix.status != UInkEncodeStatus::Success) return { EncodeFailure(prefix, "compact.encode_header") };

			UInkPageDirectory next;
			std::vector<decltype(state.directory.pages)::const_iterator> order;
			order.reserve(state.directory.pages.size());
			for (auto page = state.directory.pages.cbegin(); page != state.directory.pages.cend(); ++page)
				order.push_back(page);
			std::sort(order.begin(), order.end(), [](const auto left, const auto right)
			{
				return PageOrder(left->second.pageIndex, left->second.writeSequence,
					right->second.pageIndex, right->second.writeSequence);
			});

			if (const DWORD error = temp.Create(state.path))
				return { WithTempCleanup(temp,
					WriteFailure(UInkDiagnosticCode::IoError, "compact.temp", error)) };
			if (const DWORD error = WriteExact(temp.file.Get(), prefix.bytes))
				return { WithTempCleanup(temp,
					WriteFailure(UInkDiagnosticCode::WriteFailed, "compact.header", error)) };
			const HANDLE source = state.file.Get();
			const HANDLE target = temp.file.Get();
			// 整理层没有活动页来源，保留原扩展块的 currentPageIndex。
			auto extension = CopyRange(source, target, state.headerLength, state.extensionLength);
			if (!extension.success) return { WithTempCleanup(temp, std::move(extension)) };

			uint64_t offset = prefix.bytes.size() + state.extensionLength;
			uint64_t sequence = 0;
			for (const auto page : order)
			{
				// 原样复制并保留 pageIndex：PPT 顺序绑定用它作幻灯片序号，连续性由 L4 负责。
				UInkPageRange range = page->second;
				auto copy = CopyRange(source, target, range.offset, range.length);
				if (!copy.success) return { WithTempCleanup(temp, std::move(copy)) };
				range.offset = offset;
				range.writeSequence = sequence++;
				next.pages.emplace(page->first, range);
				SetLastPage(next, UInkGuid(page->first), range);
				offset += range.length;
			}
			if (const DWORD error = Flush(target))
				return { WithTempCleanup(temp,
					WriteFailure(UInkDiagnosticCode::FlushFailed, "compact.flush", error)) };
			if (const DWORD error = temp.file.Close())
				return { WithTempCleanup(temp,
					WriteFailure(UInkDiagnosticCode::IoError, "compact.closeTemp", error)) };
			// 写句柄未共享删除权限，必须先关闭旧句柄，再替换并重新打开。
			if (const DWORD error = state.file.Close())
				return { WithTempCleanup(temp,
					WriteFailure(UInkDiagnosticCode::IoError, "compact.closeSource", error)) };
			const DWORD moveError = temp.Publish(state.path, MOVEFILE_REPLACE_EXISTING);
			if (moveError != ERROR_SUCCESS)
			{
				UInkStoreCompactResult failure{ WriteFailure(UInkDiagnosticCode::IoError, "compact.publish", moveError) };
				if (const DWORD openError = state.file.OpenWriter(state.path))
				{
					failure.reopen = UInkStoreCompactReopen::Failed;
					failure.reopenSystemError = openError;
					AddWriteDiagnostic(failure, UInkDiagnosticCode::IoError, "compact.reopenSource", openError);
				}
				else
				{
					failure.reopen = UInkStoreCompactReopen::Succeeded;
				}
				return WithTempCleanup(temp, std::move(failure));
			}

			// 替换成功是提交点。即使重开失败并封闭写入器，目录也必须指向新文件。
			state.directory.pages.swap(next.pages);
			state.directory.supersededBytes = 0;
			state.directory.lastPageGuid = next.lastPageGuid;
			state.directory.nextContentId = next.nextContentId;
			state.directory.lastUndoId = next.lastUndoId;
			state.header = header;
			state.headerLength = prefix.bytes.size();
			state.fileBytes = offset;
			state.nextSequence = sequence;
			if (const DWORD error = state.file.OpenWriter(state.path))
				return { WriteFailure(UInkDiagnosticCode::IoError, "compact.reopen", error),
					true, UInkStoreCompactReopen::Failed, error };
			return { { true }, true, UInkStoreCompactReopen::Succeeded };
		}
		catch (const std::bad_alloc&)
		{
			return { WithTempCleanup(temp, WriteFailure(UInkDiagnosticCode::LimitExceeded, "allocation", ERROR_NOT_ENOUGH_MEMORY)) };
		}
		catch (...)
		{
			return { WithTempCleanup(temp, WriteFailure(UInkDiagnosticCode::WriteFailed, "compact")) };
		}
	}

#ifdef DRAW3_TESTING
	bool UInkPagedFileWriter::OverwriteByteForTesting(uint64_t offset, std::byte value) noexcept
	{
		if (IsSealed() || offset >= impl_->fileBytes) return false;
		const HANDLE handle = impl_->file.Get();
		return Seek(handle, offset) == ERROR_SUCCESS &&
			WriteExact(handle, std::span(&value, 1)) == ERROR_SUCCESS &&
			Flush(handle) == ERROR_SUCCESS;
	}
#endif

	void UInkPagedFileWriter::Close() noexcept { (void)impl_->file.Close(); }
	const std::wstring& UInkPagedFileWriter::Path() const noexcept { return impl_->path; }
	const UInkPageDirectory& UInkPagedFileWriter::Directory() const noexcept { return impl_->directory; }
	bool UInkPagedFileWriter::IsSealed() const noexcept { return impl_->tailUnconfirmed || !impl_->file; }
	bool UInkPagedFileWriter::IsTailUnconfirmed() const noexcept { return impl_->tailUnconfirmed; }
	uint64_t UInkPagedFileWriter::SupersededBytes() const noexcept { return impl_->directory.supersededBytes; }
	uint64_t UInkPagedFileWriter::FileBytes() const noexcept { return impl_->fileBytes; }

	UInkReadResult ReadUInkPage(const std::wstring& path, const UInkPageDirectory& directory,
		const UInkGuid& pageGuid, const UInkReadLimits& limits, UInkStoreReadFacts* facts) noexcept
	{
		if (facts) *facts = {};
		UInkReadResult result;
		uint64_t offset = 0;
		try
		{
			result.sourcePath = path;
			const auto page = directory.pages.find(pageGuid.Bytes());
			if (page == directory.pages.end())
			{
				FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::IoError,
					"page.directory", limits, ERROR_NOT_FOUND);
				return result;
			}
			const auto& range = page->second;
			offset = range.offset;
			if (facts) facts->rangeStart = offset;
			std::vector<std::byte> bytes;
			if (!ReadRange(path, &range, (std::min)(limits.maxFileBytes, kUInkStoreMaxPageBytes),
				limits, bytes, result, facts)) return result;

			uint64_t canvasCount = 0;
			result = DecodeUInkStream(bytes, limits, directory.context, offset, canvasCount);
			result.sourcePath = path;
			if (result.document && (canvasCount != 1 || result.document->canvases.size() != 1 ||
				result.document->canvases.front().pageGuid != pageGuid))
			{
				const auto status = result.status == UInkReadStatus::Complete ?
					UInkReadStatus::RecoveredInvalidTail : result.status;
				FailRead(result, facts, status, UInkDiagnosticCode::InvalidObjectOrder,
					"page.canvas", limits, ERROR_SUCCESS, offset);
			}
			return result;
		}
		catch (const std::bad_alloc&)
		{
			FailRead(result, facts, UInkReadStatus::LimitExceeded, UInkDiagnosticCode::LimitExceeded,
				"allocation", limits, ERROR_NOT_ENOUGH_MEMORY, offset);
			return result;
		}
		catch (...)
		{
			FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::DecodeFailed,
				"page.read", limits, ERROR_SUCCESS, offset);
			return result;
		}
	}

	UInkReadResult ReadUInkStoreFile(const std::wstring& path, const UInkReadLimits& limits,
		uint64_t* canvasCountBeforeDeduplication, UInkStoreReadFacts* facts) noexcept
	{
		if (facts) *facts = {};
		if (canvasCountBeforeDeduplication) *canvasCountBeforeDeduplication = 0;
		UInkReadResult result;
		try
		{
			result.sourcePath = path;
			std::vector<std::byte> bytes;
			if (!ReadRange(path, nullptr, (std::min)(limits.maxFileBytes, UInkReadLimits{}.maxFileBytes),
				limits, bytes, result, facts)) return result;

			uint64_t recognizedCanvases = 0;
			result = DecodeUInkStream(bytes, limits, recognizedCanvases);
			result.sourcePath = path;
			if (!result.document) return result;
			if (result.document->canvases.empty())
			{
				const auto status = result.status == UInkReadStatus::Complete ?
					UInkReadStatus::RecoveredInvalidTail : result.status;
				FailRead(result, facts, status, UInkDiagnosticCode::InvalidObjectOrder, "file.canvas", limits);
				return result;
			}
			// 报告解码器识别到的 Canvas 数（含字段无效被丢弃的），调用方据此区分结构错误与单项损坏。
			if (canvasCountBeforeDeduplication)
				*canvasCountBeforeDeduplication = recognizedCanvases;
			KeepLatestPages(result.document->canvases);
			return result;
		}
		catch (const std::bad_alloc&)
		{
			FailRead(result, facts, UInkReadStatus::LimitExceeded, UInkDiagnosticCode::LimitExceeded,
				"allocation", limits, ERROR_NOT_ENOUGH_MEMORY);
			return result;
		}
		catch (...)
		{
			FailRead(result, facts, UInkReadStatus::IoError, UInkDiagnosticCode::DecodeFailed, "file.read", limits);
			return result;
		}
	}

	void SetUInkStoreTestFaultInjection(const UInkStoreTestFaultInjection& faults) noexcept
	{
		gTestFaults.store((faults.shortWrite ? ShortWrite : 0u) |
			(faults.failFlush ? FailFlush : 0u) |
			(faults.failTruncate ? FailTruncate : 0u) |
			(faults.failMove ? FailMove : 0u) |
			(faults.failReopen ? FailReopen : 0u) |
			(faults.failDeleteTemp ? FailDeleteTemp : 0u), std::memory_order_relaxed);
	}

	void ResetUInkStoreTestFaultInjection() noexcept
	{
		SetUInkStoreTestFaultInjection({});
	}
}
