module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwchar>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "../../Log/Log.h"

module Inkeys.Drawing.Draw3.desktop_ink_snapshots;

import draw3.uink_store;

namespace Inkeys::Drawing::Draw3
{
	using namespace draw3::uink;

	namespace
	{
		bool MakeAbsolute(std::wstring& path)
		{
			const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
			if (!required) return false;
			std::wstring absolute(required, L'\0');
			const DWORD written = GetFullPathNameW(path.c_str(), required, absolute.data(), nullptr);
			if (!written || written >= required) return false;
			absolute.resize(written);
			path.swap(absolute);
			return true;
		}

		DWORD EnsureDirectory(const std::wstring& path)
		{
			if (CreateDirectoryW(path.c_str(), nullptr)) return ERROR_SUCCESS;
			const DWORD error = GetLastError();
			if (error == ERROR_ALREADY_EXISTS)
			{
				const DWORD attributes = GetFileAttributesW(path.c_str());
				return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)
					? ERROR_SUCCESS : ERROR_DIRECTORY;
			}
			if (error != ERROR_PATH_NOT_FOUND) return error;
			const auto separator = path.find_last_of(L"\\/");
			if (separator == std::wstring::npos || separator == 0) return error;
			const DWORD parentError = EnsureDirectory(path.substr(0, separator));
			if (parentError != ERROR_SUCCESS) return parentError;
			if (CreateDirectoryW(path.c_str(), nullptr)) return ERROR_SUCCESS;
			const DWORD retryError = GetLastError();
			if (retryError != ERROR_ALREADY_EXISTS) return retryError;
			const DWORD attributes = GetFileAttributesW(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)
				? ERROR_SUCCESS : ERROR_DIRECTORY;
		}

		DesktopInkSnapshotsTestHook::LocalTime SampleLocalTime() noexcept
		{
			SYSTEMTIME local{};
			GetLocalTime(&local);
			return { local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute,
				local.wSecond, local.wMilliseconds };
		}

		std::wstring SnapshotPath(const std::wstring& root, const UInkGuid& requestId,
			DesktopInkSnapshotsTestHook::LocalTime local)
		{
			// 日期与文件时间共用同一次采样，避免跨午夜后文件名落入错误的日期目录。
			wchar_t timestamp[32]{};
			swprintf_s(timestamp, L"%04u-%02u-%02u\\%02u%02u%02u%03u",
				static_cast<unsigned>(local.year), static_cast<unsigned>(local.month),
				static_cast<unsigned>(local.day), static_cast<unsigned>(local.hour),
				static_cast<unsigned>(local.minute), static_cast<unsigned>(local.second),
				static_cast<unsigned>(local.milliseconds));

			auto path = root;
			if (path.back() != L'\\' && path.back() != L'/') path += L'\\';
			path += L"desktop\\";
			path += timestamp;
			path += L'_';
			const auto id = FormatUInkGuid(requestId);
			path.append(id.begin(), id.begin() + 8);
			path += L".uink";
			return path;
		}

		bool HasClear(const UInkCanvas& canvas) noexcept
		{
			return std::any_of(canvas.content.begin(), canvas.content.end(),
				[](const UInkContent& content) { return std::holds_alternative<UInkClear>(content); });
		}

		bool ValidIdentity(const DesktopInkIdentity& identity) noexcept
		{
			return !identity.pageGuid.IsZero() && !identity.requestId.IsZero();
		}
		using FallbackCount = uint64_t Draw3UInkV11FallbackCounts::*;
		constexpr std::pair<std::string_view, FallbackCount> FallbackFields[] = {
			{ "extendedColor", &Draw3UInkV11FallbackCounts::extendedColor },
			{ "opacityClamped", &Draw3UInkV11FallbackCounts::opacityClamped },
			{ "nib", &Draw3UInkV11FallbackCounts::nib },
			{ "cap", &Draw3UInkV11FallbackCounts::cap },
			{ "advancedHighlighter", &Draw3UInkV11FallbackCounts::advancedHighlighter },
			{ "eraserSize", &Draw3UInkV11FallbackCounts::eraserSize },
			{ "lineCap", &Draw3UInkV11FallbackCounts::lineCap },
			{ "lineJoin", &Draw3UInkV11FallbackCounts::lineJoin },
			{ "dashArray", &Draw3UInkV11FallbackCounts::dashArray },
			{ "marker", &Draw3UInkV11FallbackCounts::marker },
			{ "square", &Draw3UInkV11FallbackCounts::square },
			{ "latestVisibility", &Draw3UInkV11FallbackCounts::latestVisibility },
			{ "viewport", &Draw3UInkV11FallbackCounts::viewport },
			{ "multipleClears", &Draw3UInkV11FallbackCounts::multipleClears },
			{ "multiDevice", &Draw3UInkV11FallbackCounts::multiDevice },
			{ "multiLayer", &Draw3UInkV11FallbackCounts::multiLayer },
			{ "upperLayerEraseSkipped", &Draw3UInkV11FallbackCounts::upperLayerEraseSkipped },
			{ "nestedWorkspace", &Draw3UInkV11FallbackCounts::nestedWorkspace },
			{ "incompleteShapeObject", &Draw3UInkV11FallbackCounts::incompleteShapeObject },
			{ "missingShapePreset", &Draw3UInkV11FallbackCounts::missingShapePreset },
			{ "unknownShapePreset", &Draw3UInkV11FallbackCounts::unknownShapePreset },
			{ "invalidShapeConstruction", &Draw3UInkV11FallbackCounts::invalidShapeConstruction },
			{ "shapePartCountMismatch", &Draw3UInkV11FallbackCounts::shapePartCountMismatch },
			{ "geometryLimit", &Draw3UInkV11FallbackCounts::geometryLimit },
			{ "footprintBudget", &Draw3UInkV11FallbackCounts::footprintBudget },
			{ "legacyRectangle", &Draw3UInkV11FallbackCounts::legacyRectangle },
			{ "invalidInkSkipped", &Draw3UInkV11FallbackCounts::invalidInkSkipped },
			{ "invalidShapeSkipped", &Draw3UInkV11FallbackCounts::invalidShapeSkipped },
			{ "mediaSkipped", &Draw3UInkV11FallbackCounts::mediaSkipped },
		};

		constexpr std::string_view DiagnosticNames[] = {
			"None", "IoError", "InvalidHeader", "UnsupportedVersion", "MalformedMessagePack", "TruncatedTail",
			"LimitExceeded", "DiagnosticsTruncated", "DuplicateKnownKey", "MissingRequiredField", "InvalidFieldType",
			"InvalidFieldValue", "InvalidObjectOrder", "UnknownTopLevelType", "KnownBlockSkipped", "NumericCompatibility",
			"FieldFallback", "TemporaryIdentity", "DuplicateGuid", "ParentCycle", "HeaderSnapshotMismatch",
			"ContentSequenceRecovered", "UnsafeMediaPath", "MediaResourceUnavailable", "WriteFailed", "FlushFailed",
			"RollbackFailed", "SelfValidationFailed", "CoordinateOutOfRange", "DecodeFailed",
		};
		static_assert(std::size(DiagnosticNames) == static_cast<size_t>(UInkDiagnosticCode::DecodeFailed) + 1);

		struct RestoreLogLastError
		{
			DWORD value = GetLastError();
			~RestoreLogLastError() { SetLastError(value); }
		};

		std::string LogFileName(std::wstring_view path)
		{
			// 只记录文件名，避免泄露存储根；限制 UTF-16 长度并避开拆开代理对。
			const auto separator = path.find_last_of(L"\\/");
			if (separator != std::wstring_view::npos) path.remove_prefix(separator + 1);
			if (path.empty()) return "unknown";
			constexpr size_t maxName = 128;
			const bool shortened = path.size() > maxName;
			if (shortened)
			{
				const wchar_t last = path[maxName - 1];
				path = path.substr(0, maxName - (last >= 0xD800 && last <= 0xDBFF));
			}
			char utf8[maxName * 3];
			const int size = WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
				utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
			if (!size) return "unknown";
			std::string name(utf8, size);
			if (shortened) name += "...";
			return name;
		}

		void AppendFallbackCounts(fmt::memory_buffer& line, const Draw3UInkV11FallbackCounts& counts)
		{
			for (const auto& [name, field] : FallbackFields)
				if (counts.*field != 0) fmt::format_to(fmt::appender(line), " l3.{}={}", name, counts.*field);
		}
	}

	void WriteUInkReadLog(std::string_view scene, std::string_view operation, std::wstring_view path,
		const UInkGuid& pageGuid, uint32_t pageNumber, const UInkReadResult& read,
		const UInkStoreReadFacts& facts, std::string_view error, uint64_t strokes, uint64_t splitUndo,
		const Draw3UInkV11FallbackCounts& fallbacks, double readMs, double importMs,
		bool storedDegraded, std::string_view failureStage) noexcept
	{
		using Log::Level;
		const bool failed = !error.empty() || read.summary.outcome == UInkReadOutcome::Failed;
		if (!Log::Enabled(failed ? Level::Error : Level::Warn)) return;
		const bool hasFallback = std::any_of(std::begin(FallbackFields), std::end(FallbackFields),
			[&](const auto& entry) { return fallbacks.*entry.second != 0; });
		const auto skipped = read.summary.diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)].count;
		const bool degraded = read.summary.outcome == UInkReadOutcome::Partial || skipped != 0 || hasFallback || splitUndo != 0;
		const auto level = failed ? Level::Error : (degraded || storedDegraded) ? Level::Warn : Level::Debug;
		if (!Log::Enabled(level)) return;
		const RestoreLogLastError lastError;
		try
		{
			// 文件名有独立上限，诊断项来自定长枚举和计数。统一日志再限制正文为 16 KiB。
			fmt::memory_buffer line;
			const auto guid = FormatUInkGuid(pageGuid);
			fmt::format_to(fmt::appender(line),
				"[UInkRead] scene={} op={} page={} result={} skippedBlocks={} file={:?} pageGuid={} bytes={} rangeStart={} strokes={} status={} split_undo={} ioMs={:.3f} decodeMs={:.3f}",
				scene, operation, pageNumber, failed ? "failed" : degraded ? "degraded" : "complete",
				skipped, LogFileName(path), guid.substr(0, 8), facts.bytesRead, facts.rangeStart, strokes,
				static_cast<unsigned>(read.status), splitUndo, facts.ioMs, read.summary.decodeMicroseconds / 1000.0);
			if (storedDegraded) fmt::format_to(fmt::appender(line), " storedDegraded=1");
			if (readMs >= 0) fmt::format_to(fmt::appender(line), " readMs={:.3f}", readMs);
			if (importMs >= 0) fmt::format_to(fmt::appender(line), " importMs={:.3f}", importMs);
			if (read.summary.version) fmt::format_to(fmt::appender(line), " version={}", *read.summary.version);
			if (read.validPrefixLength != 0) fmt::format_to(fmt::appender(line), " validPrefixBytes={}", read.validPrefixLength);
			if (read.safeAppendOffset) fmt::format_to(fmt::appender(line), " safeAppendOffset={}", *read.safeAppendOffset);
			if (read.failedObjectOffset) fmt::format_to(fmt::appender(line), " stop=l1_file:{}", *read.failedObjectOffset);
			if (facts.failureStage)
				fmt::format_to(fmt::appender(line), " stage={} stop_l2=range_start:{} systemError={}",
					facts.failureStage, facts.rangeStart, facts.systemError);
			else if (!failureStage.empty())
				fmt::format_to(fmt::appender(line), " stage={} offset=unknown", failureStage);
			else if (failed)
			{
				const bool decodeFailed = read.summary.outcome == UInkReadOutcome::Failed;
				fmt::format_to(fmt::appender(line), " stage={} offset={}", decodeFailed ? "decode" : error,
					decodeFailed && read.failedObjectOffset ? "l1_file" : "unknown");
			}
			if (failed && !error.empty()) fmt::format_to(fmt::appender(line), " reason={}", error);
			for (const auto& count : read.summary.diagnosticCounts)
			{
				if (count.count == 0) continue;
				// L2 为现有回执补入的失败事件单独报告，余下计数和首个偏移仍来自 L1。
				const auto l1Count = count.count - (count.code == facts.failureCode ? 1 : 0);
				if (l1Count != 0)
					fmt::format_to(fmt::appender(line), " l1.{}={}@{}",
						DiagnosticNames[static_cast<size_t>(count.code)], l1Count, count.firstByteOffset);
			}
			if (hasFallback) fmt::format_to(fmt::appender(line), " l3Offset=unknown");
			AppendFallbackCounts(line, fallbacks);
			Log::Write(level, Log::Module::UInk, "{}", std::string_view(line.data(), line.size()));
		}
		catch (...) { Log::RecordDropped(level); } // 日志失败只计入丢弃统计。
	}

	void WriteUInkWriteLog(std::string_view scene, std::string_view operation, std::wstring_view path,
		const UInkGuid& pageGuid, uint32_t pageNumber, std::string_view stage, std::string_view error,
		uint32_t systemError, const Draw3UInkV11FallbackCounts& fallbacks, double writeMs,
		bool prefixReadDegraded, UInkWriteLogFacts facts) noexcept
	{
		const auto level = error.empty() ? Log::Level::Warn : Log::Level::Error;
		if (!Log::Enabled(level)) return;
		const RestoreLogLastError lastError;
		try
		{
			fmt::memory_buffer line;
			const auto guid = FormatUInkGuid(pageGuid);
			fmt::format_to(fmt::appender(line),
				"[UInkWrite] scene={} op={} page={} result={} stage={} systemError={} file={:?} pageGuid={}",
				scene, operation, pageNumber, error.empty() ? "degraded" : "failed", stage, systemError,
				LogFileName(path), guid.substr(0, 8));
			if (!error.empty())
			{
				fmt::format_to(fmt::appender(line), " reason={} sealed={} tailUnconfirmed={}",
					error, facts.sealed ? 1 : 0, facts.tailUnconfirmed ? 1 : 0);
				if (facts.rollbackStage)
					fmt::format_to(fmt::appender(line), " rollbackStage={} rollbackSystemError={}",
						facts.rollbackStage, facts.rollbackSystemError);
			}
			if (writeMs >= 0) fmt::format_to(fmt::appender(line), " writeMs={:.3f}", writeMs);
			if (prefixReadDegraded) fmt::format_to(fmt::appender(line), " prefixReadDegraded=1");
			AppendFallbackCounts(line, fallbacks);
			Log::Write(level, Log::Module::UInk, "{}", std::string_view(line.data(), line.size()));
		}
		catch (...) { Log::RecordDropped(level); }
	}

	struct DesktopInkSnapshots::Impl
	{
		struct Node
		{
			DesktopInkCompletion completion;
			std::shared_ptr<const Draw3UInkCanvasSnapshot> snapshot;
			UInkGuid saveRequestId;
			std::shared_ptr<const std::wstring> loadPath;
			bool tracksRecovery = false;
		};

		struct Registration
		{
			std::weak_ptr<Impl> owner;
			std::array<uint8_t, 16> requestId{};
			~Registration() noexcept
			{
				if (const auto store = owner.lock())
					store->RetireRegistration(requestId, this);
			}
		};

		struct Saved
		{
			UInkGuid pageGuid;
			uint64_t clearGeneration;
			std::shared_ptr<const std::wstring> path;
			const Registration* registration = nullptr;
			bool retired = false;
		};

		void RetireRegistration(const std::array<uint8_t, 16>& requestId, const Registration* registration) noexcept
		{
			const std::lock_guard lock(mutex);
			const auto record = saved.find(requestId);
			if (record == saved.end() || record->second.registration != registration) return;
			if (record->second.path) saved.erase(record);
			else
			{
				// 保留未完成保存的准入预留，终态只交付回执，不重新登记路径。
				record->second.registration = nullptr;
				record->second.retired = true;
			}
		}

		DesktopInkSnapshotsOptions options;
		DesktopInkSnapshotsTestHook testHook;
		mutable std::mutex mutex;
		std::mutex joinMutex;
		std::condition_variable changed;
		std::list<Node> queued;
		std::list<Node> active;
		std::list<Node> completed;
		std::map<std::array<uint8_t, 16>, Saved> saved;
		DesktopInkSnapshotsDiagnostics diagnostics;
		bool closing = true;
		DesktopInkCloseMode closeMode = DesktopInkCloseMode::Drain;
		std::thread worker;

		void SaveSnapshot(Node& node, std::wstring& basePath, std::string_view& stage)
		{
			auto& completion = node.completion;
			const Draw3UInkV11PageContext context{ .dpiScale = options.dpiScale };
			auto header = MakeDraw3UInkV11FileHeader(completion.identity.requestId, context);
			if (header.status != Draw3UInkExportStatus::Success)
			{
				completion.error = DesktopInkError::ExportFailed;
				completion.diagnostics = std::move(header.diagnostics);
				return;
			}

			auto exported = ExportDraw3PageToUInkV11(*node.snapshot, context);
			completion.diagnostics = std::move(exported.diagnostics);
			if (exported.status != Draw3UInkExportStatus::Success || !exported.canvas)
			{
				completion.error = DesktopInkError::ExportFailed;
				return;
			}
			if (HasClear(*exported.canvas))
			{
				completion.error = DesktopInkError::InvalidSnapshot;
				return;
			}
			completion.fallbackCounts = exported.counts;
			completion.degraded = exported.counts.invalidShapeSkipped != 0;
			if (node.tracksRecovery && !completion.degraded)
			{
				// 冷读会跳过超限项时，仍发布原始文件，Clear 恢复点继续持有内存副本。
				const auto cold = ImportUInkV11Page(*exported.canvas, { options.dpiScale });
				completion.fallbackCounts.footprintBudget = cold.counts.footprintBudget;
				completion.degraded = cold.counts.footprintBudget != 0;
				if (const auto* reason = Draw3UInkV11ColdPreflightFailure(cold))
				{
					completion.degraded = true;
					Log::Warn(Log::Module::UInk,
						"[UInkWrite] scene=desktop op=publish page=1 stage=cold_preflight reason={}", reason);
				}
			}
			stage = "path";
			const auto local = testHook.sampleLocalTime
				? testHook.sampleLocalTime(testHook.context, completion.identity) : SampleLocalTime();
			basePath = SnapshotPath(options.root, completion.identity.requestId, local);
			stage = "directory";
			completion.systemError = EnsureDirectory(basePath.substr(0, basePath.find_last_of(L'\\')));
			if (completion.systemError)
			{
				completion.error = DesktopInkError::DirectoryFailed;
				return;
			}

			// 路径所有权在提交前分配。发布后回执与读取登记共用实际路径。
			auto path = std::make_shared<std::wstring>();
			stage = "publish";
			auto write = PublishUInkSnapshotFile(basePath, header.header, header.headerExtension, *exported.canvas);
			completion.systemError = write.systemError;
			if (!write.success)
			{
				completion.error = DesktopInkError::PublishFailed;
				completion.diagnostics = std::move(write.diagnostics);
				if (write.failureStage) stage = write.failureStage;
				return;
			}

			path->swap(write.publishedPath);
			completion.publishedPath = std::move(path);
			completion.result = DesktopInkResult::Succeeded;
		}

		void LoadSnapshot(Node& node, UInkReadResult& read, UInkStoreReadFacts& facts, bool& attemptedRead,
			double& readMs, double& importMs)
		{
			using Clock = std::chrono::steady_clock;
			using Milliseconds = std::chrono::duration<double, std::milli>;
			auto& completion = node.completion;
			uint64_t canvasCount = 0;
			attemptedRead = true;
			const auto readStarted = Clock::now();
			read = ReadUInkStoreFile(*node.loadPath, {}, &canvasCount, &facts);
			readMs = Milliseconds(Clock::now() - readStarted).count();
			completion.readDiagnostics = std::move(read.diagnostics);
			if (!read.document || read.status == UInkReadStatus::IoError)
			{
				completion.error = DesktopInkError::ReadFailed;
				for (const auto& diagnostic : completion.readDiagnostics)
				{
					if (diagnostic.systemError == 0) continue;
					completion.systemError = diagnostic.systemError;
					break;
				}
				return;
			}

			const auto& document = *read.document;
			if (canvasCount != 1 || document.canvases.size() != 1 ||
				!document.usesImplicitWorkspace || !document.usesImplicitDevice ||
				document.header.pageNum != 1 || document.header.workspaceNum != 1 || document.header.deviceNum != 1 ||
				read.provenance.usedTemporaryIdentity ||
				read.summary.version.value_or(0) != kUInkVersion || document.header.guid.Bytes() != node.saveRequestId.Bytes())
			{
				completion.error = DesktopInkError::InvalidFile;
				return;
			}

			const auto& canvas = document.canvases.front();
			if (canvas.pageGuid.Bytes() != completion.identity.pageGuid.Bytes() ||
				canvas.pageIndex != 0 || canvas.pageNumber != 1 || canvas.layerIndex != 0 ||
				canvas.workspaceGuid || canvas.deviceGuid || canvas.temporaryWorkspace ||
				canvas.temporaryDevice || canvas.temporaryPage || canvas.temporaryLayer || HasClear(canvas))
			{
				completion.error = DesktopInkError::InvalidFile;
				return;
			}

			// L1 可保留损坏项之外的内容，L3 同样跳过无法显示的单项。
			const auto importStarted = Clock::now();
			auto imported = ImportUInkV11Page(canvas, { options.dpiScale });
			importMs = Milliseconds(Clock::now() - importStarted).count();
			completion.fallbackCounts = imported.counts;
			completion.diagnostics = std::move(imported.diagnostics);
			if (imported.status != Draw3UInkV11ImportStatus::Success || !imported.page ||
				imported.page->strokes.empty())
			{
				completion.error = DesktopInkError::ImportFailed;
				return;
			}
			const auto& counts = imported.counts;
			completion.degraded = read.summary.outcome != UInkReadOutcome::Complete ||
				counts.invalidInkSkipped != 0 || counts.invalidShapeSkipped != 0 || counts.mediaSkipped != 0 ||
				counts.incompleteShapeObject != 0 || counts.invalidShapeConstruction != 0 ||
				counts.shapePartCountMismatch != 0 || counts.geometryLimit != 0 || counts.footprintBudget != 0;
			completion.snapshot = std::move(imported.page);
			completion.result = DesktopInkResult::Succeeded;
		}

		void Run() noexcept
		{
			for (;;)
			{
				bool cancel = false;
				{
					std::unique_lock lock(mutex);
					changed.wait(lock, [&] { return closing || !queued.empty(); });
					if (queued.empty()) return;
					cancel = closing && (closeMode == DesktopInkCloseMode::Cancel ||
						queued.front().completion.operation == DesktopInkOperation::Load);
					active.splice(active.end(), queued, queued.begin());
				}

				auto& node = active.front();
				auto& completion = node.completion;
				const bool isSave = completion.operation == DesktopInkOperation::Save;
				std::wstring savePath;
				std::string_view saveStage = "export";
				UInkReadResult read;
				UInkStoreReadFacts readFacts;
				bool attemptedRead = false;
				double readMs = -1.0, importMs = -1.0;
				if (cancel)
				{
					completion.result = DesktopInkResult::Canceled;
					completion.error = DesktopInkError::Closing;
				}
				else
				{
					try
					{
						if (testHook.beforeWork)
							testHook.beforeWork(testHook.context, completion.operation, completion.identity);
						if (isSave) SaveSnapshot(node, savePath, saveStage);
						else LoadSnapshot(node, read, readFacts, attemptedRead, readMs, importMs);
					}
					catch (...)
					{
						completion.result = DesktopInkResult::Failed;
						completion.error = DesktopInkError::Exception;
					}
				}

				// 已导入的快照独立持有笔画，终态发布前释放 L1 文档。
				read.document.reset();
				if (isSave && !cancel && (completion.result != DesktopInkResult::Succeeded || completion.degraded))
				{
					std::string_view reason = "exception";
					switch (completion.error)
					{
					case DesktopInkError::None: reason = {}; break;
					case DesktopInkError::InvalidSnapshot: reason = "invalid_snapshot"; break;
					case DesktopInkError::DirectoryFailed: reason = "directory"; break;
					case DesktopInkError::ExportFailed: reason = "export"; break;
					case DesktopInkError::PublishFailed: reason = "publish"; break;
					default: break;
					}
					const auto& file = completion.publishedPath ? *completion.publishedPath : savePath;
					WriteUInkWriteLog("desktop", "publish", file, completion.identity.pageGuid, 1,
						saveStage, reason, completion.systemError, completion.fallbackCounts, -1.0);
				}
				if (attemptedRead)
				{
					std::string_view error = "exception";
					switch (completion.error)
					{
					case DesktopInkError::None: error = {}; break;
					case DesktopInkError::ReadFailed: error = "read_failed"; break;
					case DesktopInkError::InvalidFile: error = "file_identity_or_structure"; break;
					case DesktopInkError::ImportFailed: error = "import"; break;
					default: break;
					}
					size_t split = 0;
					if (completion.snapshot && Log::Enabled(Log::Level::Warn))
					{
						std::optional<uint32_t> previous;
						for (const auto& stroke : completion.snapshot->strokes)
						{
							if (previous == stroke.undoId) ++split;
							previous = stroke.undoId;
						}
					}
					WriteUInkReadLog("desktop", "load", *node.loadPath, completion.identity.pageGuid, 1,
						read, readFacts, error, completion.snapshot ? completion.snapshot->strokes.size() : 0,
						split, completion.fallbackCounts, readMs, importMs);
				}
				else if (!isSave && !cancel)
					Log::Error(Log::Module::UInk, "[UInkQueue] scene=desktop op=load result=failed reason=exception generation={}",
						completion.identity.clearGeneration);

				// 在终态可见之前释放任务持有的大对象。保存回执只持有路径。
				node.snapshot.reset();
				node.loadPath.reset();
				if (isSave && !cancel && testHook.afterSave)
					testHook.afterSave(testHook.context, completion);

				{
					const std::lock_guard lock(mutex);
					if (isSave)
					{
						const auto record = saved.find(completion.identity.requestId.Bytes());
						if (completion.result == DesktopInkResult::Succeeded)
						{
							if (record->second.retired) saved.erase(record);
							else record->second.path = completion.publishedPath;
							++diagnostics.committed;
						}
						else
						{
							saved.erase(record);
							++diagnostics.failed;
						}
						--diagnostics.pending;
					}
					completed.splice(completed.end(), active, active.begin());
				}

				if (options.wake) options.wake(options.wakeContext);
			}
		}
	};

	DesktopInkSnapshots::DesktopInkSnapshots() : impl_(std::make_shared<Impl>()) {}
	DesktopInkSnapshots::~DesktopInkSnapshots() { Close(); }

	bool DesktopInkSnapshots::Start(DesktopInkSnapshotsOptions options, DesktopInkSnapshotsTestHook testHook) noexcept
	{
		try
		{
			const std::lock_guard joinLock(impl_->joinMutex);
			if (impl_->worker.joinable() || options.root.empty() || options.root.find(L'\0') != std::wstring::npos ||
				!std::isfinite(options.dpiScale) || options.dpiScale <= 0 || !MakeAbsolute(options.root))
				return false;

			const std::lock_guard lock(impl_->mutex);
			if (!impl_->completed.empty()) return false;
			impl_->options = std::move(options);
			impl_->testHook = testHook;
			impl_->saved.clear();
			impl_->diagnostics = {};
			impl_->worker = std::thread([implementation = impl_.get()] { implementation->Run(); });
			impl_->closing = false;
			return true;
		}
		catch (...) { return false; }
	}

	DesktopInkAdmission DesktopInkSnapshots::Save(DesktopInkSaveRequest request) noexcept
	{
		const auto& identity = request.identity;
		if (!ValidIdentity(identity) || !request.snapshot ||
			request.snapshot->pageGuid.Bytes() != identity.pageGuid.Bytes() ||
			request.snapshot->pageIndex != 0 || request.snapshot->pageNumber != 1 ||
			(request.snapshot->strokes.empty() && request.snapshot->operations.empty()) ||
			(request.registration && request.registration->lease_))
			return DesktopInkAdmission::InvalidRequest;

		{
			const std::lock_guard lock(impl_->mutex);
			if (impl_->closing) return DesktopInkAdmission::Closed;
			try
			{
				auto registration = request.registration ? std::make_shared<Impl::Registration>() : nullptr;
				const auto [record, inserted] = impl_->saved.try_emplace(identity.requestId.Bytes(),
					Impl::Saved{ identity.pageGuid, identity.clearGeneration });
				if (!inserted) return DesktopInkAdmission::InvalidRequest;
				try
				{
					// 在准入时同时预留任务节点、终态槽与路径登记项。
					auto& node = impl_->queued.emplace_back();
					node.completion.identity = identity;
					node.snapshot = std::move(request.snapshot);
					node.tracksRecovery = request.registration != nullptr;
				}
				catch (...)
				{
					impl_->saved.erase(record);
					throw;
				}
				if (registration)
				{
					registration->owner = impl_;
					registration->requestId = identity.requestId.Bytes();
					record->second.registration = registration.get();
					request.registration->lease_ = std::move(registration);
				}
				++impl_->diagnostics.accepted;
				++impl_->diagnostics.pending;
			}
			catch (...) { return DesktopInkAdmission::ReservationFailed; }
		}
		impl_->changed.notify_one();
		return DesktopInkAdmission::Accepted;
	}

	DesktopInkAdmission DesktopInkSnapshots::Load(DesktopInkLoadRequest request) noexcept
	{
		const auto& identity = request.identity;
		if (!ValidIdentity(identity) || request.saveRequestId.IsZero() ||
			identity.requestId.Bytes() == request.saveRequestId.Bytes())
			return DesktopInkAdmission::InvalidRequest;
		{
			const std::lock_guard lock(impl_->mutex);
			if (impl_->closing) return DesktopInkAdmission::Closed;
			const auto record = impl_->saved.find(request.saveRequestId.Bytes());
			if (record == impl_->saved.end() || !record->second.path) return DesktopInkAdmission::MissingSave;
			if (record->second.pageGuid.Bytes() != identity.pageGuid.Bytes() ||
				record->second.clearGeneration != identity.clearGeneration)
				return DesktopInkAdmission::InvalidRequest;
			try
			{
				auto& node = impl_->queued.emplace_back();
				node.completion.identity = identity;
				node.completion.operation = DesktopInkOperation::Load;
				node.saveRequestId = request.saveRequestId;
				node.loadPath = record->second.path;
			}
			catch (...) { return DesktopInkAdmission::ReservationFailed; }
		}
		impl_->changed.notify_one();
		return DesktopInkAdmission::Accepted;
	}

	std::optional<DesktopInkCompletion> DesktopInkSnapshots::Take() noexcept
	{
		static_assert(std::is_nothrow_move_constructible_v<DesktopInkCompletion>);
		const std::lock_guard lock(impl_->mutex);
		if (impl_->completed.empty()) return std::nullopt;
		auto completion = std::move(impl_->completed.front().completion);
		impl_->completed.pop_front();
		return completion;
	}

	DesktopInkSnapshotsDiagnostics DesktopInkSnapshots::Diagnostics() const noexcept
	{
		const std::lock_guard lock(impl_->mutex);
		return impl_->diagnostics;
	}

	void DesktopInkSnapshots::Close(DesktopInkCloseMode mode) noexcept
	{
		const std::lock_guard joinLock(impl_->joinMutex);
		{
			const std::lock_guard lock(impl_->mutex);
			if (!impl_->closing)
			{
				impl_->closing = true;
				impl_->closeMode = mode;
			}
		}
		impl_->changed.notify_one();
		if (impl_->worker.joinable()) impl_->worker.join();
		const std::lock_guard lock(impl_->mutex);
		impl_->saved.clear();
	}
}
