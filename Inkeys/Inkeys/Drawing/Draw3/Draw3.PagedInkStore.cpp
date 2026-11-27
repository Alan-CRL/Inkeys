module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "../../Log/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module Inkeys.Drawing.Draw3.paged_ink_store;

import draw3.uink_store;
import Inkeys.Drawing.Draw3.desktop_ink_snapshots;

namespace Inkeys::Drawing::Draw3
{
	using namespace draw3::uink;

	namespace
	{
		using Clock = std::chrono::steady_clock;

		double ElapsedMs(Clock::time_point started) noexcept
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
		}

		std::wstring JoinPath(const std::wstring& parent, std::wstring_view name)
		{
			auto path = parent;
			if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
			path += name;
			return path;
		}

		std::wstring GuidPrefix(const UInkGuid& guid)
		{
			const auto text = FormatUInkGuid(guid);
			return { text.begin(), text.begin() + 8 };
		}

		std::wstring PresentationName(std::wstring_view name)
		{
			std::wstring result;
			for (const wchar_t ch : name)
			{
				if (ch < L' ' || std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos)
					continue;
				if (result.size() == 40) break;
				result += ch;
			}
			// 截断时保留完整的 UTF-16 字符。
			if (!result.empty() && result.back() >= 0xd800 && result.back() <= 0xdbff)
				result.pop_back();
			return result.empty() ? L"untitled" : result;
		}

		bool IsReservedDeviceName(std::wstring_view component) noexcept
		{
			// Win32 只看最终组件中首个点之前的名称，扩展名及其后的 key 无法消除冲突。
			const auto stem = component.substr(0, component.find(L'.'));
			if (stem.size() != 3 && stem.size() != 4) return false;
			const auto startsWith = [&](const wchar_t* name)
			{
				return CompareStringOrdinal(stem.data(), 3, name, 3, TRUE) == CSTR_EQUAL;
			};
			if (stem.size() == 3)
				return startsWith(L"CON") || startsWith(L"PRN") || startsWith(L"AUX") || startsWith(L"NUL");
			return stem[3] >= L'1' && stem[3] <= L'9' &&
				(startsWith(L"COM") || startsWith(L"LPT"));
		}

		DWORD MakeAbsolute(std::wstring& path)
		{
			const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
			if (!required) return GetLastError();
			std::wstring absolute(required, L'\0');
			const DWORD written = GetFullPathNameW(path.c_str(), required, absolute.data(), nullptr);
			if (!written) return GetLastError();
			if (written >= required) return ERROR_INSUFFICIENT_BUFFER;
			absolute.resize(written);
			path.swap(absolute);
			return ERROR_SUCCESS;
		}

		DWORD CreateDirectoryIfMissing(const std::wstring& path) noexcept
		{
			if (CreateDirectoryW(path.c_str(), nullptr)) return ERROR_SUCCESS;
			const DWORD error = GetLastError();
			if (error == ERROR_ALREADY_EXISTS)
			{
				const DWORD attributes = GetFileAttributesW(path.c_str());
				if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
					return ERROR_SUCCESS;
			}
			return error;
		}

		DWORD EnsureDirectories(const std::wstring& path)
		{
			const DWORD attributes = GetFileAttributesW(path.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES)
				return attributes & FILE_ATTRIBUTE_DIRECTORY ? ERROR_SUCCESS : ERROR_DIRECTORY;
			const DWORD error = CreateDirectoryIfMissing(path);
			if (error != ERROR_PATH_NOT_FOUND) return error;
			const auto separator = path.find_last_of(L"\\/");
			if (separator == std::wstring::npos || separator == 0) return error;
			const DWORD parentError = EnsureDirectories(path.substr(0, separator));
			return parentError == ERROR_SUCCESS ? CreateDirectoryIfMissing(path) : parentError;
		}

		const char* PagedInkErrorName(PagedInkError error) noexcept
		{
			switch (error)
			{
			case PagedInkError::None: return "none";
			case PagedInkError::PendingWrite: return "pending_write";
			case PagedInkError::BoundaryMismatch: return "boundary_mismatch";
			case PagedInkError::InvalidSnapshot: return "invalid_snapshot";
			case PagedInkError::MissingPage: return "missing_page";
			case PagedInkError::ExportFailed: return "export_failed";
			case PagedInkError::SpliceFailed: return "splice_failed";
			case PagedInkError::ReadFailed: return "read_failed";
			case PagedInkError::ImportFailed: return "import_failed";
			case PagedInkError::FileCreateFailed: return "file_create_failed";
			case PagedInkError::FileWriteFailed: return "file_write_failed";
			case PagedInkError::Exception: return "exception";
			case PagedInkError::AppendBaseMismatch: return "append_base_mismatch";
			}
			return "unknown";
		}

		std::string_view ReadOperation(PagedInkRequestKind kind) noexcept
		{
			switch (kind)
			{
			case PagedInkRequestKind::LoadLane: return "load_lane";
			case PagedInkRequestKind::LoadPreviousInterval: return "load_previous";
			default: return "load";
			}
		}

		struct PageReadLog
		{
			bool attempted = false;
			UInkReadResult read;
			UInkStoreReadFacts facts;
			std::string_view error;
			std::string_view stage;
		};

		struct PageWriteLog
		{
			std::wstring createPath;
			std::string_view stage = "before_work";
			UInkWriteLogFacts facts;
			uint64_t coldInvalidShapeSkipped = 0;
			uint64_t coldGeometryLimit = 0;
			uint64_t coldFootprintBudget = 0;
		};

		struct SnapshotLogCounts
		{
			uint64_t strokes = 0;
			uint64_t splitUndo = 0;
		};

		struct SpliceReadLog
		{
			PageReadLog disk;
			SnapshotLogCounts counts;
			Draw3UInkV11FallbackCounts fallbacks;
			double importMs = 0;
			Clock::duration countTime{};
			uint32_t pageNumber = 0;
			bool storedDegraded = false;
			bool stoppedSave = false;
		};

		bool SpliceRejectedPrefix(const Draw3UInkV11SpliceDiagnostics& diagnostics) noexcept
		{
			// 次序错误只由前缀、Clear 或当前区间校验给出。明细因分配失败缺失时按前缀拒绝记录。
			return diagnostics.code == UInkDiagnosticCode::InvalidObjectOrder &&
				(diagnostics.diagnostics.empty() || diagnostics.diagnostics.front().fieldPath != "draw3.v11.splice.current");
		}

		SnapshotLogCounts CountSnapshotForLog(const Draw3UInkCanvasSnapshot& snapshot, bool splitPrevious) noexcept
		{
			SnapshotLogCounts counts{ snapshot.strokes.size() };
			std::optional<uint32_t> previous;
			for (const auto& stroke : snapshot.strokes)
			{
				if (previous == stroke.undoId) ++counts.splitUndo;
				previous = stroke.undoId;
			}
			// strokes 只含当前区间，白板冷页还会安装 Clear 之前的恢复点。
			if (snapshot.intervalOrdinal == 0) return counts;
			previous.reset();
			for (const auto& operation : snapshot.operations)
			{
				const auto* stroke = std::get_if<Draw3UInkStrokeSnapshot>(&operation);
				if (!stroke) break;
				++counts.strokes;
				if (splitPrevious && previous == stroke->undoId) ++counts.splitUndo;
				previous = stroke->undoId;
			}
			return counts;
		}

		bool IsEvictionFileHealthy(const PagedInkFileState& file) noexcept
		{
			return !file.failed && !file.sealed && !file.tailUnconfirmed;
		}

		struct DamagedPageVersion
		{
			std::array<uint8_t, 16> pageGuid;
			uint64_t writeSequence;
			uint64_t offset;
			uint64_t length;

			bool operator==(const DamagedPageVersion&) const = default;

			bool Matches(const UInkPageRange& range) const noexcept
			{
				return writeSequence == range.writeSequence && offset == range.offset && length == range.length;
			}
		};

		bool HasStoredPageDamage(const UInkPageRange& range, const PageReadLog& disk) noexcept
		{
			const std::string_view stage = disk.facts.failureStage ? disk.facts.failureStage : "";
			if (stage == "page.range" || stage == "page.canvas") return true;

			const auto& read = disk.read;
			if (read.provenance.usedTemporaryIdentity) return true;
			const auto& counts = read.summary.diagnosticCounts;
			if (read.failedObjectOffset && *read.failedObjectOffset >= range.offset &&
				*read.failedObjectOffset - range.offset < range.length &&
				(counts[static_cast<size_t>(UInkDiagnosticCode::MalformedMessagePack)].count != 0 ||
					counts[static_cast<size_t>(UInkDiagnosticCode::TruncatedTail)].count != 0 ||
					(read.status == UInkReadStatus::StoppedAtCorruptTail &&
						counts[static_cast<size_t>(UInkDiagnosticCode::InvalidObjectOrder)].count != 0)))
				return true;
			if (!read.provenance.containsInvalidCompleteBlocks) return false;
			if (read.status != UInkReadStatus::LimitExceeded) return true;

			// L1 的预算跳过也会置坏块标记，模型限额还可能在同一对象上报告 KnownBlockSkipped。
			const auto& skipped = counts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)];
			const auto& limited = counts[static_cast<size_t>(UInkDiagnosticCode::LimitExceeded)];
			if (!read.failedObjectOffset && read.document && read.document->canvases.size() == 1)
			{
				// 扫描到段尾时，每个超限跳块至多解释一个未交付的完整对象，明细截断也可比较总数。
				const auto unknown = counts[static_cast<size_t>(UInkDiagnosticCode::UnknownTopLevelType)].count;
				const auto decoded = 1 + read.document->canvases.front().content.size() + unknown;
				if (read.decodedObjectCount > decoded + limited.count) return true;
			}
			if (skipped.count > limited.count ||
				(skipped.count != 0 && skipped.firstByteOffset < limited.firstByteOffset)) return true;
			std::optional<uint64_t> limitedOffset;
			for (const auto& diagnostic : read.diagnostics)
			{
				if (diagnostic.code == UInkDiagnosticCode::LimitExceeded) limitedOffset = diagnostic.byteOffset;
				else if (diagnostic.code == UInkDiagnosticCode::KnownBlockSkipped &&
					limitedOffset != diagnostic.byteOffset) return true;
			}
			return false;
		}

		std::optional<std::size_t> LastClear(const UInkCanvas& canvas, std::size_t end) noexcept
		{
			while (end != 0)
			{
				--end;
				if (std::holds_alternative<UInkClear>(canvas.content[end])) return end;
			}
			return std::nullopt;
		}
	}

	std::wstring MakePagedInkPath(const std::wstring& root, const PagedInkFileNaming& naming,
		const PagedInkLocalTime& time, const UInkGuid& fileGuid)
	{
		wchar_t stamp[32]{};
		swprintf_s(stamp, L"%02u%02u%02u%03u", static_cast<unsigned>(time.hour),
			static_cast<unsigned>(time.minute), static_cast<unsigned>(time.second),
			static_cast<unsigned>(time.millisecond));
		const auto filename = std::wstring(stamp) + L"_" + GuidPrefix(fileGuid) + L".uink";
		wchar_t date[32]{};
		if (naming.kind == PagedInkFileKind::Presentation)
		{
			swprintf_s(date, L"%04u%02u%02u", static_cast<unsigned>(time.year),
				static_cast<unsigned>(time.month), static_cast<unsigned>(time.day));
			auto document = PresentationName(naming.presentationName) + L"_" +
				GuidPrefix(naming.presentationKey);
			if (IsReservedDeviceName(document)) document.insert(0, 1, L'_');
			return JoinPath(JoinPath(JoinPath(root, L"presentation"), document),
				std::wstring(date) + L"-" + filename);
		}
		swprintf_s(date, L"%04u-%02u-%02u", static_cast<unsigned>(time.year),
			static_cast<unsigned>(time.month), static_cast<unsigned>(time.day));
		// 桌面活动区间文件与 C1.4 的 Clear 快照（desktop\）分目录，互不混入对方的读取。
		return JoinPath(JoinPath(JoinPath(root, naming.kind == PagedInkFileKind::Desktop ? L"desktop-live" : L"whiteboard"),
			date), filename);
	}

	const char* PagedInkWriteDiagnosticStage(const PagedInkCompletion& completion) noexcept
	{
		if (completion.result == PagedInkResult::Merged) return "merged";
		if (completion.result == PagedInkResult::Canceled) return "canceled";
		if (completion.result == PagedInkResult::FileFailed && !completion.saveDequeued) return "file_failed";
		return "written"; // 已执行的保存由调用方另报是否成功。
	}

	bool IsPageEvictable(const PagedInkEvictionState& page, const PagedInkFileState& file) noexcept
	{
		return page.confirmedRevision == page.currentRevision && !page.isCurrentPage &&
			!page.isLoadTarget && !page.degraded && IsEvictionFileHealthy(file) && !file.compacting;
	}

	struct PagedInkStore::Impl
	{
		struct Node
		{
			Node(PagedInkRequestIdentity identity, std::optional<Draw3UInkCanvasSnapshot> page,
				std::optional<PagedInkBoundaryIdentity> boundary, uint64_t base = 0) noexcept
				: snapshot(std::move(page)), expectedBoundary(boundary), baseRevision(base)
			{
				completion.identity = identity;
			}

			PagedInkCompletion completion;
			std::optional<Draw3UInkCanvasSnapshot> snapshot;
			std::optional<PagedInkBoundaryIdentity> expectedBoundary;
			uint64_t baseRevision = 0; // 仅 Append
			Clock::time_point queuedAt = Clock::now();
		};

		struct ConfirmedPage
		{
			std::optional<PagedInkBoundaryIdentity> boundary;
			uint32_t clearContentId = 0; // 仅 boundary 有值时使用，也是写入时的前缀块数。
			bool prefixDegraded = false;
			bool degraded = false;
			uint64_t revision = 0;
			uint32_t pageNumber = 0;
			std::optional<int32_t> slideId;
			int32_t binding = 0;
			std::optional<int32_t> pageState;
			std::optional<int32_t> pageKind;
			// C1.6b：盘上整页的冷读预算用量。整页写入时由整页冷读给出，逐笔续接累加；未知时续接先补一次整页。
			struct ColdUsage { uint64_t footprint = 0; uint64_t tiles = 0; uint64_t geometry = 0; };
			std::optional<ColdUsage> coldUsage;
		};

		using Queue = std::list<Node>;

		explicit Impl(PagedInkStoreOptions settings, PagedInkStoreTestHook hook)
			: options(std::move(settings)), testHook(hook), worker(&Impl::Run, this) {}

		std::size_t RegisterWakes(std::size_t count) noexcept
		{
			// 与回执投递在同一临界区登记，关闭方随后必须等待锁外的回调完成。
			if (!options.wake) return 0;
			pendingWakes += count;
			return count;
		}

		void Wake(std::size_t count) noexcept
		{
			if (count == 0) return;
			if (testHook.beforeWake) testHook.beforeWake(testHook.context);
			for (std::size_t index = 0; index < count; ++index) options.wake(options.wakeContext);
			const std::lock_guard lock(mutex);
			pendingWakes -= count;
			changed.notify_all();
		}

		void Complete(Queue& source, Queue::iterator node) noexcept
		{
			node->snapshot.reset();
			if (&source != &active) node->completion.queueMs = ElapsedMs(node->queuedAt);
			node->completion.file = file;
			completed.splice(completed.end(), source, node);
		}

		std::size_t FinishQueue(Queue& queue, PagedInkResult result) noexcept
		{
			const auto count = queue.size();
			while (!queue.empty())
			{
				queue.front().completion.result = result;
				Complete(queue, queue.begin());
			}
			return count;
		}

		bool HasPendingSave(const UInkGuid& pageGuid) const noexcept
		{
			return activeSave == pageGuid ||
				std::any_of(saves.begin(), saves.end(), [&](const Node& node)
					{ return node.completion.identity.pageGuid == pageGuid; });
		}

		bool HasPendingLoadDependency(const PagedInkRequestIdentity& identity) const noexcept
		{
			if (!identity.laneBinding) return HasPendingSave(identity.pageGuid);

			const int32_t binding = *identity.laneBinding;
			const auto affectsLane = [&](const Node& node)
			{
				const auto queuedBinding = options.pageContext.workspaceType == 2 ?
					node.snapshot->binding.value_or(0) : 0;
				const auto record = confirmed.find(node.completion.identity.pageGuid.Bytes());
				// 若一次替代涉及不同的绑定，旧 lane 与目标 lane 都依赖这次写入。
				return queuedBinding == binding ||
					(record != confirmed.end() && record->second.binding == binding);
			};
			return (activeSave && affectsLane(active.front())) ||
				std::any_of(saves.begin(), saves.end(), affectsLane);
		}

		Queue::iterator MergeCandidate(const PagedInkRequestIdentity& identity) noexcept
		{
			if (identity.kind != PagedInkRequestKind::Full) return saves.end();
			// 只替换同页、同槽、同边界分段内最后一项尚未执行的 Full。
			for (auto next = saves.end(); next != saves.begin();)
			{
				--next;
				const auto& queued = next->completion.identity;
				if (queued.pageGuid != identity.pageGuid) continue;
				// Append 续接的是它准入时的盘上状态，后面的 Full 不能越过它合并到更早的 Full。
				if (queued.kind == PagedInkRequestKind::Clear || queued.kind == PagedInkRequestKind::Append ||
					queued.kind == PagedInkRequestKind::Rotate ||
					queued.slotGeneration != identity.slotGeneration ||
					queued.boundaryGeneration != identity.boundaryGeneration)
					break;
				return next;
			}
			return saves.end();
		}

		PagedInkAdmission Submit(PagedInkRequestIdentity identity,
			std::optional<Draw3UInkCanvasSnapshot> snapshot = std::nullopt,
			std::optional<PagedInkBoundaryIdentity> expectedBoundary = std::nullopt, uint64_t baseRevision = 0) noexcept
		{
			if (identity.pageGuid.IsZero() && identity.kind != PagedInkRequestKind::LoadLane)
				return PagedInkAdmission::InvalidRequest;
			std::size_t ready = 0;
			{
				const std::lock_guard lock(mutex);
				if (closing) return PagedInkAdmission::Closed;
				const bool save = snapshot.has_value();
				// Rotate 换掉已失败的文件，正是失败后恢复写入的途径，不受文件失败准入限制。
				if (save && file.failed && identity.kind != PagedInkRequestKind::Rotate) return PagedInkAdmission::FileFailed;
				const bool pendingSave = !save && HasPendingLoadDependency(identity);
				const auto merged = save ? MergeCandidate(identity) : saves.end();
				auto& queue = save ? saves : pendingSave ? completed : loads;
				try
				{
					// 一个节点同时预留任务和完成槽。准入后只转移节点，完成投递无需分配。
					queue.emplace_back(identity, std::move(snapshot), expectedBoundary, baseRevision);
				}
				catch (...)
				{
					return PagedInkAdmission::ReservationFailed;
				}
				// 先登记准入，再允许 Take 或 worker 看到终态（包括立即完成的 PendingWrite）。
				if (testHook.recordAccepted)
					queue.back().completion.requestId = testHook.recordAccepted(testHook.context);
				if (pendingSave)
				{
					queue.back().completion.error = PagedInkError::PendingWrite;
					queue.back().completion.file = file;
					++ready;
				}
				if (merged != saves.end())
				{
					merged->completion.result = PagedInkResult::Merged;
					Complete(saves, merged);
					++ready;
				}
				ready = RegisterWakes(ready);
			}
			changed.notify_all();
			Wake(ready);
			return PagedInkAdmission::Accepted;
		}

		std::string_view LogScene() const noexcept
		{
			return options.naming.kind == PagedInkFileKind::Presentation ? "ppt" : "whiteboard";
		}

		void LogReadQueueFailure(std::string_view operation, const UInkGuid& guid,
			uint32_t pageNumber, PagedInkError error) const noexcept
		{
			const auto& id = guid.Bytes();
			::Inkeys::Log::Error(::Inkeys::Log::Module::UInk,
				"[UInkQueue] scene={} op={} page={} pageGuid={:02x}{:02x}{:02x}{:02x} result=failed reason={}",
				LogScene(), operation, pageNumber, id[0], id[1], id[2], id[3], PagedInkErrorName(error));
		}

		void LogRead(std::string_view operation, const UInkGuid& guid, uint32_t pageNumber,
			const PageReadLog& disk, SnapshotLogCounts counts, const Draw3UInkV11FallbackCounts& fallbacks,
			double readMs, double importMs, bool storedDegraded) const noexcept
		{
			const auto& sourcePath = disk.read.sourcePath.empty() ? writer->Path() : disk.read.sourcePath;
			WriteUInkReadLog(LogScene(), operation, sourcePath, guid, pageNumber,
				disk.read, disk.facts, disk.error, counts.strokes, counts.splitUndo, fallbacks, readMs, importMs,
				storedDegraded, disk.stage);
		}

		void RecordDamage(const UInkGuid& pageGuid, const UInkPageRange& range) noexcept
		{
			const DamagedPageVersion version{ pageGuid.Bytes(), range.writeSequence, range.offset, range.length };
			if (std::find(damagedVersions.begin(), damagedVersions.end(), version) != damagedVersions.end()) return;
			try
			{
				damagedVersions.push_back(version);
			}
			catch (...)
			{
				// 损坏追踪不影响业务读取；无法完整追踪时，本次会话停用整理。
				damageTrackingIncomplete = true;
			}
		}

		std::optional<UInkCanvas> ReadPage(const UInkGuid& pageGuid, const ConfirmedPage* record,
			PagedInkPageResult& result, PageReadLog& disk, PagedInkPageMetadata* metadata = nullptr, bool prefixOnly = false)
		{
			if (!writer)
			{
				result.error = PagedInkError::MissingPage;
				return std::nullopt;
			}
			const auto& directory = writer->Directory();
			const auto page = directory.pages.find(pageGuid.Bytes());
			if (page == directory.pages.end())
			{
				result.error = PagedInkError::MissingPage;
				return std::nullopt;
			}
			if (metadata) metadata->pageIndex = page->second.pageIndex;
			result.pageBytes = page->second.length;
			if (testHook.beforeReadAfterClosingWriter && !writer->IsSealed())
			{
				writer->Close();
				testHook.beforeReadAfterClosingWriter(testHook.context, writer->Path());
			}
#ifdef DRAW3_TESTING
			if (testHook.corruptOffsetBeforeRead)
				if (const auto offset = testHook.corruptOffsetBeforeRead(testHook.context, pageGuid))
					(void)writer->OverwriteByteForTesting(*offset, std::byte{ 0xc1 });
#endif
			const auto* readPath = testHook.readPathOverride ?
				testHook.readPathOverride(testHook.context, writer->Path()) : nullptr;
			disk.attempted = true;
			// 保存原始 L1 汇总，prefixOnly 只裁剪给业务使用的区间报告。
			auto& read = disk.read;
			const auto readStarted = Clock::now();
			read = ReadUInkPage(readPath ? *readPath : writer->Path(), directory, pageGuid, {}, &disk.facts);
			result.readMs = ElapsedMs(readStarted);
			result.ioMs = disk.facts.ioMs;
			result.readStatus = read.status;
			result.readSummary = read.summary;
			// 使用完整页段的损坏证据，避免仅取前缀时漏掉会被替换的尾部坏块。
			const auto& range = page->second;
			const bool storedDamage = HasStoredPageDamage(range, disk);
			if (storedDamage) RecordDamage(pageGuid, range);
			result.readDiagnostics = std::move(read.diagnostics);

			// L2 已拒绝范围越界、第二个 Canvas 和 GUID 不符。
			if (read.summary.outcome == UInkReadOutcome::Failed || !read.document ||
				read.document->canvases.size() != 1 || !record)
			{
				result.error = PagedInkError::ReadFailed;
				disk.error = "read_failed";
				return std::nullopt;
			}
			auto& canvas = read.document->canvases.front();
			// L1 回填的身份可能恰好等于已确认值，仍然必须拒绝。
			const bool stableSlide = options.pageContext.workspaceType == 2 &&
				record->binding == 0 && record->slideId.has_value();
			if (read.provenance.usedTemporaryIdentity || canvas.temporaryPage || canvas.temporaryLayer ||
				canvas.temporaryDevice || canvas.temporaryWorkspace ||
				canvas.workspaceGuid != options.pageContext.workspaceGuid || canvas.layerIndex != 0 ||
				canvas.pageIndex != range.pageIndex || EffectiveUInkBinding(canvas) != record->binding ||
				(stableSlide && canvas.slideId != record->slideId))
			{
				result.error = PagedInkError::ReadFailed;
				disk.error = "identity_mismatch";
				RecordDamage(pageGuid, range);
				return std::nullopt;
			}
			const auto clear = LastClear(canvas, canvas.content.size());
			if (clear.has_value() != record->boundary.has_value() ||
				(clear && (std::get<UInkClear>(canvas.content[*clear]).contentId != record->clearContentId ||
					LastClear(canvas, *clear))))
			{
				result.error = PagedInkError::BoundaryMismatch;
				disk.error = "boundary_mismatch";
				// 限额可能跳过 Clear，但已经读到的 Clear 仍能证明结构失配。
				if (clear || read.status != UInkReadStatus::LimitExceeded) RecordDamage(pageGuid, range);
				return std::nullopt;
			}

			// 确认版本的 contentId 连续；即使 L1 明细达到上限，仍可按边界计算丢失块数。
			const auto prefixCount = clear ? record->clearContentId : 0;
			const auto tailStart = clear ? uint64_t{prefixCount} + 1 : 0;
			const auto tailCount = page->second.nextContentId - tailStart;
			const auto readableTail = canvas.content.size() - (clear ? *clear + 1 : 0);
			if (clear && *clear < prefixCount) result.prefixSkippedBlocks = prefixCount - *clear;
			if (readableTail < tailCount) result.tailSkippedBlocks = tailCount - readableTail;
			result.prefixReadDegraded = result.prefixSkippedBlocks != 0;
			result.tailReadDegraded = result.tailSkippedBlocks != 0;
			if (!storedDamage && read.status != UInkReadStatus::LimitExceeded &&
				(result.prefixReadDegraded || result.tailReadDegraded)) RecordDamage(pageGuid, range);

			// 页段从 Canvas 开始（对象序号 0），Clear 的对象序号为 contentId + 1。
			const auto isPrefix = [clear, tailStart](const UInkDiagnostic& diagnostic)
			{
				return clear && diagnostic.objectIndex <= tailStart;
			};
			if (read.summary.outcome == UInkReadOutcome::Partial)
				for (const auto& diagnostic : result.readDiagnostics)
				{
					if (diagnostic.severity == UInkDiagnosticSeverity::Info) continue;
					if (isPrefix(diagnostic)) result.prefixReadDegraded = true;
					else result.tailReadDegraded = true;
				}

			if (prefixOnly)
			{
				// 边界已核验。撤回和拼接只用可读前缀，丢弃会被替换的旧当前区间。
				canvas.content.resize(*clear + 1);
				result.tailReadDegraded = false;
				result.tailSkippedBlocks = 0;
				result.readStatus = UInkReadStatus::Complete;
				std::erase_if(result.readDiagnostics, [&](const UInkDiagnostic& diagnostic) { return !isPrefix(diagnostic); });
				auto& summary = *result.readSummary;
				summary.outcome = result.prefixReadDegraded ? UInkReadOutcome::Partial : UInkReadOutcome::Complete;
				summary.diagnosticCounts = {};
				for (const auto& diagnostic : result.readDiagnostics)
				{
					auto& count = summary.diagnosticCounts[static_cast<size_t>(diagnostic.code)];
					if (count.count++ == 0)
					{
						count.code = diagnostic.code;
						count.firstByteOffset = diagnostic.byteOffset;
						count.firstCanvasOrdinal = diagnostic.canvasOrdinal;
					}
				}
			}
			result.readDegraded = result.prefixReadDegraded || result.tailReadDegraded ||
				(!prefixOnly && read.summary.outcome == UInkReadOutcome::Partial);
			return std::move(canvas);
		}

		void LoadPage(const UInkGuid& pageGuid, const ConfirmedPage* record, PagedInkPageResult& result,
			PageReadLog& disk, const std::optional<PagedInkBoundaryIdentity>& expectedBoundary,
			PagedInkPageMetadata* metadata)
		{
			if (expectedBoundary && (!record || record->boundary != expectedBoundary))
			{
				result.error = PagedInkError::BoundaryMismatch;
				return;
			}
			if (record)
			{
				result.confirmedBoundary = record->boundary;
				result.confirmedRevision = record->revision;
			}
			auto canvas = ReadPage(pageGuid, record, result, disk, metadata, expectedBoundary.has_value());
			if (!canvas) return;
			if (expectedBoundary) canvas->content.pop_back(); // 返回边界之前的区间。
			const auto importStarted = Clock::now();
			auto imported = ImportUInkV11Page(*canvas, options.importOptions);
			result.importMs = ElapsedMs(importStarted);
			result.fallbackCounts = imported.counts;
			result.diagnostics = std::move(imported.diagnostics);
			if (imported.status != Draw3UInkV11ImportStatus::Success || !imported.page)
			{
				result.error = PagedInkError::ImportFailed;
				disk.error = "import_failed";
				return;
			}
			result.snapshot = std::move(imported.page);
			// 跳过笔画的读回保留热页，与读取缺块的处理一致。
			result.readDegraded |= imported.counts.footprintBudget != 0;
			result.degraded = expectedBoundary ? record->prefixDegraded :
				imported.counts.invalidShapeSkipped > 0 || record->degraded;
			result.result = PagedInkResult::Succeeded;
		}

		void LoadAndLogPage(const UInkGuid& pageGuid, const ConfirmedPage* record, PagedInkPageResult& result,
			std::string_view operation, const std::optional<PagedInkBoundaryIdentity>& expectedBoundary = std::nullopt,
			PagedInkPageMetadata* metadata = nullptr)
		{
			PageReadLog disk;
			try
			{
				LoadPage(pageGuid, record, result, disk, expectedBoundary, metadata);
			}
			catch (...)
			{
				if (!disk.attempted) throw;
				result.result = PagedInkResult::Failed;
				result.error = PagedInkError::Exception;
				disk.error = "exception";
			}
			if (!disk.attempted)
			{
				LogReadQueueFailure(operation, pageGuid, record ? record->pageNumber : 0, result.error);
				return;
			}
			const auto counts = result.snapshot && ::Inkeys::Log::Enabled(::Inkeys::Log::Level::Warn) ?
				CountSnapshotForLog(*result.snapshot, options.naming.kind == PagedInkFileKind::Whiteboard) :
				SnapshotLogCounts{};
			const bool storedDegraded = record && (expectedBoundary ? record->prefixDegraded : record->degraded);
			LogRead(operation, pageGuid, record ? record->pageNumber : 0, disk, counts, result.fallbackCounts,
				result.readMs, result.importMs, storedDegraded);
		}

		void LoadLane(PagedInkCompletion& completion)
		{
			for (const auto& [key, record] : confirmed)
			{
				if (record.binding != *completion.identity.laneBinding) continue;
				const UInkGuid pageGuid(key);
				auto& page = completion.lanePages.emplace_back();
				page.metadata = { pageGuid, 0, record.pageNumber, record.slideId,
					record.binding, record.pageState, record.pageKind };
				try
				{
					LoadAndLogPage(pageGuid, &record, page, ReadOperation(PagedInkRequestKind::LoadLane), std::nullopt, &page.metadata);
				}
				catch (...)
				{
					// 单页执行失败仍保留其确认信息，其余页继续读取。
					page.result = PagedInkResult::Failed;
					page.error = PagedInkError::Exception;
					LogReadQueueFailure(ReadOperation(PagedInkRequestKind::LoadLane), pageGuid, record.pageNumber, page.error);
				}
			}
			completion.result = PagedInkResult::Succeeded;
		}

		UInkStoreWriteResult CreatePagedFile(const UInkCanvas& canvas, PagedInkCompletion& completion, PageWriteLog& log)
		{
			log.stage = "create.guid";
			const auto guid = CreateUInkGuid();
			if (!guid)
			{
				completion.systemError = ERROR_GEN_FAILURE;
				return {};
			}
			log.stage = "create.context";
			const auto workspaceType = options.naming.kind == PagedInkFileKind::Presentation ? 2 : 1;
			if (options.naming.kind == PagedInkFileKind::Desktop)
			{
				// 桌面：隐式 Workspace，页上下文不得声明显式 Workspace。
				if (options.pageContext.workspaceType || options.pageContext.workspaceGuid)
				{
					completion.error = PagedInkError::InvalidSnapshot;
					return {};
				}
			}
			else if (options.pageContext.workspaceType != workspaceType ||
				(workspaceType == 2 && (options.naming.presentationKey.IsZero() ||
					!options.hostId || ParseUInkGuid(*options.hostId) != options.naming.presentationKey)))
			{
				completion.error = PagedInkError::InvalidSnapshot;
				return {};
			}
			log.stage = "create.header";
			auto header = MakeDraw3UInkV11FileHeader(*guid, options.pageContext, options.hostId);
			if (header.status != Draw3UInkExportStatus::Success)
			{
				completion.diagnostics = std::move(header.diagnostics);
				return {};
			}
			SYSTEMTIME local{};
			GetLocalTime(&local);
			log.stage = "create.path";
			log.createPath = MakePagedInkPath(options.root, options.naming, { local.wYear, local.wMonth,
				local.wDay, local.wHour, local.wMinute, local.wSecond, local.wMilliseconds }, *guid);
			completion.systemError = MakeAbsolute(log.createPath);
			if (completion.systemError != ERROR_SUCCESS) return {};
			log.stage = "create.directory";
			completion.systemError = EnsureDirectories(log.createPath.substr(0, log.createPath.find_last_of(L"\\/")));
			if (completion.systemError != ERROR_SUCCESS) return {};
			// 发布前先登记候选路径（尚无文件身份，恢复时按完整文件读）：发布后、身份登记前崩溃也不会漏掉这个文件。
			log.stage = "create.recovery_register";
			if (!NoteRecoveryFile(log.createPath))
			{
				completion.systemError = ERROR_WRITE_FAULT;
				return {};
			}
			log.stage = "create.prepare";
			UInkStoreWriteResult result;
			writer = UInkPagedFileWriter::Create(log.createPath, header.header, header.headerExtension, canvas, result);
			return result;
		}

		void SavePage(Node& node, std::wstring& publishedPath, PageWriteLog& log, std::optional<SpliceReadLog>& spliceRead)
		{
			auto& completion = node.completion;
			const auto& identity = completion.identity;
			const auto key = identity.pageGuid.Bytes();
			const auto record = confirmed.find(key);
			log.stage = "preflight.boundary";
			// 只读同一页最后确认的边界版本，拒绝把旧 Clear 的前缀拼到新边界。
			if (node.expectedBoundary && (record == confirmed.end() ||
				record->second.boundary != node.expectedBoundary))
			{
				completion.error = PagedInkError::BoundaryMismatch;
				return;
			}
			log.stage = "preflight.export";
			auto exported = ExportDraw3PageToUInkV11(*node.snapshot, options.pageContext);
			completion.fallbackCounts = exported.counts;
			completion.diagnostics = std::move(exported.diagnostics);
			if (exported.status != Draw3UInkExportStatus::Success || !exported.canvas)
			{
				completion.error = PagedInkError::ExportFailed;
				return;
			}

			log.stage = "preflight.snapshot";
			auto& canvas = *exported.canvas;
			auto clear = LastClear(canvas, canvas.content.size());
			const bool expectClear = identity.boundaryGeneration.has_value() && !node.expectedBoundary;
			if (clear.has_value() != expectClear ||
				(identity.kind == PagedInkRequestKind::Clear && (!clear || *clear + 1 != canvas.content.size())))
			{
				completion.error = PagedInkError::InvalidSnapshot;
				return;
			}
			const bool exportSkipped = exported.counts.invalidShapeSkipped > 0;
			// Clear 的导出只含上一区间。同一边界的后续写入沿用该区间的降级记录。
			bool prefixDegraded = identity.kind == PagedInkRequestKind::Clear ? exportSkipped :
				identity.boundaryGeneration && record != confirmed.end() && record->second.boundary &&
				record->second.boundary->generation == *identity.boundaryGeneration && record->second.prefixDegraded;
			if (node.expectedBoundary)
			{
				log.stage = "preflight.read_prefix";
				auto& prefix = spliceRead.emplace();
				auto& disk = prefix.disk;
				// 成功保存会更新原确认项，先保留本次实际读到的版本信息。
				prefix.pageNumber = record->second.pageNumber;
				prefix.storedDegraded = record->second.prefixDegraded;
				try
				{
					auto previous = ReadPage(identity.pageGuid, &record->second, completion, disk, nullptr, true);
					if (!previous)
					{
						prefix.stoppedSave = disk.attempted;
						return;
					}
					disk.stage = "prefix_validate";
					if (testHook.afterSplicePrefixRead) testHook.afterSplicePrefixRead(testHook.context);
					if (::Inkeys::Log::Enabled(::Inkeys::Log::Level::Error))
					{
						const auto countStarted = Clock::now();
						prefix.counts.strokes = std::count_if(previous->content.begin(), previous->content.end(), [](const auto& block)
						{
							// 部件引用跟随图形根计为同一笔，Clear 不计入笔数。
							const auto* shape = std::get_if<UInkShape>(&block);
							return std::holds_alternative<UInkInk>(block) ||
								(shape && (!shape->object || std::holds_alternative<UInkShapeObject>(*shape->object)));
						});
						prefix.countTime = Clock::now() - countStarted;
					}
					log.stage = "preflight.splice";
					if (completion.prefixReadDegraded)
					{
						// 跳过块后，L1 页段仍保留旧编号。先消除空洞，再交给严格拼接校验。
						uint32_t previousUndo = 0, recoveredUndo = 0;
						for (size_t index = 0; index < previous->content.size(); ++index)
							std::visit([&](auto& block)
							{
								if (index != 0 && block.undoId != previousUndo) ++recoveredUndo;
								previousUndo = block.undoId;
								block.contentId = static_cast<uint32_t>(index);
								block.undoId = recoveredUndo;
							}, previous->content[index]);
					}
					Draw3UInkV11SpliceDiagnostics diagnostics;
					auto spliced = SpliceUInkV11PreviousInterval(*previous, canvas, diagnostics);
					if (!SpliceRejectedPrefix(diagnostics)) disk.stage = {};
					if (!spliced && completion.prefixReadDegraded)
					{
						// 缺件图形可能仍有可展示的几何，由 L3 恢复成可重新写出的前缀。
						if (!disk.stage.empty()) disk.stage = "prefix_import";
						const auto importStarted = Clock::now();
						const auto imported = ImportUInkV11Page(*previous, options.importOptions);
						prefix.importMs = ElapsedMs(importStarted);
						prefix.fallbacks = imported.counts;
						if (imported.page)
						{
							// operations 每笔一项，也含 Clear；strokes 只保留 Clear 之后的区间。
							prefix.counts.strokes = imported.page->operations.size() - imported.page->intervalOrdinal;
						}
						if (imported.status == Draw3UInkV11ImportStatus::Success && imported.page)
						{
							const auto recovered = ExportDraw3PageToUInkV11(*imported.page, options.pageContext);
							if (recovered.status == Draw3UInkExportStatus::Success && recovered.canvas)
							{
								spliced = SpliceUInkV11PreviousInterval(*recovered.canvas, canvas, diagnostics);
								if (!SpliceRejectedPrefix(diagnostics)) disk.stage = {};
							}
						}
					}
					if (!disk.stage.empty()) disk.error = "splice_failed";
					if (!spliced)
					{
						completion.diagnostics = std::move(diagnostics.diagnostics);
						completion.error = PagedInkError::SpliceFailed;
						return;
					}
					canvas = std::move(*spliced);
					clear = LastClear(canvas, canvas.content.size());
					prefixDegraded |= completion.prefixReadDegraded;
				}
				catch (...)
				{
					// 前缀已可用后的异常由保存行报告，读取行仍保留原始汇总。
					if (disk.attempted && (!disk.stage.empty() || log.stage == "preflight.read_prefix"))
					{
						disk.error = "exception";
						prefix.stoppedSave = true;
					}
					throw;
				}
			}
			else if (exportSkipped && !canvas.content.empty())
			{
				// 导出跳过开头图形时，首个保留下来的组也须从 0 开始。
				const auto firstGroup = std::visit([](const auto& block) { return block.undoId; },
					canvas.content.front());
				if (firstGroup != 0)
					for (auto& block : canvas.content)
						std::visit([&](auto& value) { value.undoId -= firstGroup; }, block);
			}

			// 前缀和当前区间各自导出时可能都符合预算，整页冷读时则共用一份预算。
			bool degraded = prefixDegraded || exportSkipped;
			std::optional<ConfirmedPage::ColdUsage> coldUsage;
			{
				const auto cold = ImportUInkV11Page(canvas, options.importOptions);
				coldUsage = ConfirmedPage::ColdUsage{ cold.footprintUsed, cold.pageTilesUsed, cold.geometryUsed };
				log.coldInvalidShapeSkipped = cold.counts.invalidShapeSkipped;
				log.coldGeometryLimit = cold.counts.geometryLimit;
				log.coldFootprintBudget = cold.counts.footprintBudget;
				degraded = degraded || log.coldInvalidShapeSkipped != 0 ||
					log.coldGeometryLimit != 0 || log.coldFootprintBudget != 0;
				if (const auto* reason = Draw3UInkV11ColdPreflightFailure(cold))
				{
					// 磁盘提交继续，回执按降级结算以保留驻留页及 Clear 前缀。
					degraded = true;
					::Inkeys::Log::Warn(::Inkeys::Log::Module::UInk,
						"[UInkWrite] scene={} op={} page={} stage=cold_preflight reason={}",
						LogScene(), writer ? "append" : "create", node.snapshot->pageNumber, reason);
				}
			}

			// 写盘前分配确认目录项，Flush 后只更新已有值或插入已分配的节点。
			decltype(confirmed)::node_type stagedNode;
			if (record == confirmed.end())
			{
				decltype(confirmed) staged;
				stagedNode = staged.extract(staged.emplace(key, ConfirmedPage{}).first);
			}
			completion.error = writer ? PagedInkError::FileWriteFailed : PagedInkError::FileCreateFailed;
			log.stage = "append.prepare";
			const bool hadWriter = writer != nullptr;
			// 整页写入可能停在对象边界上冒充完整页：写前登记起点，写完清除（C1.6b §5）。
			if (hadWriter && !NoteRecoveryPending(kRecoveryPendingPage))
			{
				log.stage = "append.recovery_register";
				return;
			}
			const auto writeStarted = Clock::now();
			auto write = writer ? writer->AppendPage(canvas) : CreatePagedFile(canvas, completion, log);
			completion.writeMs = ElapsedMs(writeStarted);
			publishedPath.swap(write.publishedPath);
			if (!write.success)
			{
				if (write.failureStage) log.stage = write.failureStage;
				log.facts.rollbackStage = write.rollbackStage;
				log.facts.rollbackSystemError = write.rollbackSystemError;
				if (write.systemError != 0) completion.systemError = write.systemError;
				if (!write.diagnostics.empty()) completion.diagnostics = std::move(write.diagnostics);
				return;
			}
			if (hadWriter) ClearRecoveryPending();
			else (void)NoteRecoveryFile(writer->Path());

			const auto& pages = writer->Directory().pages;
			const auto page = pages.find(key);
			if (page != pages.end()) completion.pageBytes = page->second.length;

			ConfirmedPage saved;
			saved.revision = identity.revision;
			saved.prefixDegraded = prefixDegraded;
			saved.degraded = degraded;
			saved.coldUsage = coldUsage;
			if (clear)
			{
				saved.boundary = PagedInkBoundaryIdentity{ *identity.boundaryGeneration, identity.revision };
				saved.clearContentId = std::get<UInkClear>(canvas.content[*clear]).contentId;
			}
			saved.pageNumber = canvas.pageNumber;
			saved.slideId = canvas.slideId;
			saved.binding = EffectiveUInkBinding(canvas);
			saved.pageState = canvas.pageState;
			saved.pageKind = canvas.pageKind;
			{
				// 准入时也要查询绑定，目录记录仅在 Flush 成功后持锁发布。
				const std::lock_guard lock(mutex);
				if (record != confirmed.end()) record->second = saved;
				else
				{
					stagedNode.mapped() = saved;
					confirmed.insert(std::move(stagedNode));
				}
			}
			completion.confirmedBoundary = saved.boundary;
			completion.confirmedRevision = saved.revision;
			completion.degraded = saved.degraded;
			completion.result = PagedInkResult::Succeeded;
			completion.error = PagedInkError::None;
		}

		// ---- C1.6b 崩溃恢复：写前登记与逐笔续接 ----
		static constexpr uint8_t kRecoveryPendingPage = 0;
		static constexpr uint8_t kRecoveryPendingRelocate = 2;
		static constexpr uint8_t kRecoveryPendingShapeGroup = 3;

		// 返回 false 时调用方拒绝这次写入。登记文件同时清除该槽位的写前登记。
		bool NoteRecoveryFile(const std::wstring& filePath) noexcept
		{
			if (!options.registerRecoveryFile || filePath.empty()) return true;
			bool registered = false;
			try { registered = options.registerRecoveryFile(options.recoveryContext, filePath); }
			catch (...) {}
			if (registered) recoveryPendingStale = false;
			else ::Inkeys::Log::Warn(::Inkeys::Log::Module::UInk, "[UInkRecovery] scene={} event=register_file_failed", LogScene());
			return registered;
		}

		bool NoteRecoveryPending(uint8_t kind) noexcept
		{
			if (!options.registerRecoveryPending || !writer) return true;
			bool registered = false;
			try { registered = options.registerRecoveryPending(options.recoveryContext, writer->Path(), writer->FileBytes(), kind); }
			catch (...) {}
			// 登记成功后，写入失败（即使已回截）也会把起点留在记录里，直到确认清除。
			if (registered) recoveryPendingStale = true;
			else ::Inkeys::Log::Warn(::Inkeys::Log::Module::UInk, "[UInkRecovery] scene={} event=register_pending_failed", LogScene());
			return registered;
		}

		void ClearRecoveryPending() noexcept
		{
			if (!options.clearRecoveryPending || !writer) return;
			bool cleared = false;
			try { cleared = options.clearRecoveryPending(options.recoveryContext, writer->Path()); }
			catch (...) {}
			// 清除没落盘：盘上仍是旧起点，之后的单块追加若不先重新登记，恢复时会被截掉。
			recoveryPendingStale = !cleared;
			recoveryClearRetryPending = !cleared;
			if (!cleared)
				::Inkeys::Log::Warn(::Inkeys::Log::Module::UInk, "[UInkRecovery] scene={} event=clear_pending_failed", LogScene());
		}

		// 把这一笔续接到盘上已确认版本之后（design §13.1）。整页导出按可见历史顺序重编 contentId / undoId，
		// 所以盘上版本等于改动前的内存页时，单笔导出平移编号后的追加结果与整页导出一致。
		void AppendStroke(Node& node, std::wstring& publishedPath, PageWriteLog& log)
		{
			auto& completion = node.completion;
			const auto& identity = completion.identity;
			const auto key = identity.pageGuid.Bytes();
			const auto record = confirmed.find(key);
			const bool onDisk = record != confirmed.end();
			log.stage = "append_stroke.base";
			if (onDisk ? (record->second.revision != node.baseRevision || record->second.degraded ||
				record->second.prefixDegraded) : node.baseRevision != 0)
			{
				completion.error = PagedInkError::AppendBaseMismatch;
				return;
			}
			log.stage = "append_stroke.export";
			auto exported = ExportDraw3PageToUInkV11(*node.snapshot, options.pageContext);
			completion.fallbackCounts = exported.counts;
			completion.diagnostics = std::move(exported.diagnostics);
			if (exported.status != Draw3UInkExportStatus::Success || !exported.canvas)
			{
				completion.error = PagedInkError::ExportFailed;
				return;
			}
			auto& canvas = *exported.canvas;
			if (canvas.content.empty() || exported.counts.invalidShapeSkipped != 0)
			{
				// 被跳过的图形会让整页版本降级，交给整页写入按原规则处理。
				completion.error = PagedInkError::AppendBaseMismatch;
				return;
			}
			ConfirmedPage::ColdUsage coldUsage;
			{
				// 这一笔本身须能冷读，且累加到盘上整页的用量后仍在页预算内；否则交给整页写入按原规则判定降级。
				const auto cold = ImportUInkV11Page(canvas, options.importOptions);
				if (Draw3UInkV11ColdPreflightFailure(cold) || cold.counts.invalidShapeSkipped != 0 ||
					cold.counts.geometryLimit != 0 || cold.counts.footprintBudget != 0)
				{
					completion.error = PagedInkError::AppendBaseMismatch;
					return;
				}
				coldUsage = { cold.footprintUsed, cold.pageTilesUsed, cold.geometryUsed };
				if (onDisk)
				{
					const auto& used = record->second.coldUsage;
					const auto& importOptions = options.importOptions;
					const auto within = [](uint64_t base, uint64_t add, uint64_t budget)
					{
						return base <= budget && add <= budget - base;
					};
					if (!used || (importOptions.footprintBudget &&
						!within(used->footprint, coldUsage.footprint, *importOptions.footprintBudget)) ||
						!within(used->tiles, coldUsage.tiles, importOptions.footprintPageMaxTiles) ||
						!within(used->geometry, coldUsage.geometry, kDraw3UInkV11PageGeometryBudget))
					{
						completion.error = PagedInkError::AppendBaseMismatch;
						return;
					}
					coldUsage.footprint += used->footprint;
					coldUsage.tiles += used->tiles;
					coldUsage.geometry += used->geometry;
				}
			}

			const bool hadWriter = writer != nullptr;
			bool pageInFile = false;
			uint64_t nextContentId = 0;
			uint32_t lastUndoId = 0;
			bool atTail = false;
			if (hadWriter)
			{
				const auto& directory = writer->Directory();
				if (const auto page = directory.pages.find(key); page != directory.pages.end())
				{
					pageInFile = true;
					nextContentId = page->second.nextContentId;
					lastUndoId = page->second.lastUndoId;
					atTail = directory.lastPageGuid && *directory.lastPageGuid == identity.pageGuid &&
						page->second.offset + page->second.length == writer->FileBytes();
				}
			}
			if (onDisk && !pageInFile)
			{
				// 确认目录里有、当前文件里却没有（不应出现）：单笔写成新页会丢掉该页已有内容，交给整页写入。
				completion.error = PagedInkError::AppendBaseMismatch;
				return;
			}
			completion.error = hadWriter ? PagedInkError::FileWriteFailed : PagedInkError::FileCreateFailed;
			const auto writeStarted = Clock::now();
			UInkStoreWriteResult write;
			if (!onDisk)
			{
				// 新页：这一笔就是该页第一个完整版本（SAVE-13）。
				log.stage = "append_stroke.page";
				if (hadWriter && !NoteRecoveryPending(kRecoveryPendingPage))
				{
					log.stage = "append_stroke.recovery_register";
					return;
				}
				write = hadWriter ? writer->AppendPage(canvas) : CreatePagedFile(canvas, completion, log);
				if (write.success)
				{
					if (hadWriter) ClearRecoveryPending();
					else (void)NoteRecoveryFile(writer->Path());
				}
			}
			else
			{
				const uint32_t undoId = nextContentId == 0 ? 0 : lastUndoId + 1;
				uint64_t contentId = nextContentId;
				for (auto& block : canvas.content)
					std::visit([&](auto& value)
					{
						value.contentId = static_cast<uint32_t>(contentId++);
						value.undoId = undoId; // 单笔（含多部件图形）同属一个撤回组
					}, block);
				std::vector<UInkContent> blocks = std::move(canvas.content);
				canvas.content.clear();
				const bool relocate = !atTail;
				// 单块追加通常不登记（残缺块由解码器丢弃）；上次清除失败时须先登记，覆盖盘上残留的旧起点。
				const bool registered = relocate || blocks.size() > 1 || recoveryPendingStale;
				if (registered && !NoteRecoveryPending(relocate ? kRecoveryPendingRelocate : kRecoveryPendingShapeGroup))
				{
					log.stage = "append_stroke.recovery_register";
					return;
				}
				write.success = true;
				if (relocate)
				{
					// 回翻页上的落笔：先把这一页搬到文件末尾成为最后一个 Canvas。
					log.stage = "append_stroke.relocate";
					write = writer->RelocatePage(identity.pageGuid);
				}
				if (write.success)
				{
					log.stage = "append_stroke.content";
					write = writer->AppendContent(canvas, blocks);
				}
				if (write.success && registered) ClearRecoveryPending();
			}
			completion.writeMs = ElapsedMs(writeStarted);
			publishedPath.swap(write.publishedPath);
			if (!write.success)
			{
				if (write.failureStage) log.stage = write.failureStage;
				log.facts.rollbackStage = write.rollbackStage;
				log.facts.rollbackSystemError = write.rollbackSystemError;
				if (write.systemError != 0) completion.systemError = write.systemError;
				if (!write.diagnostics.empty()) completion.diagnostics = std::move(write.diagnostics);
				return;
			}
			if (writer)
			{
				const auto& pages = writer->Directory().pages;
				if (const auto page = pages.find(key); page != pages.end()) completion.pageBytes = page->second.length;
			}
			{
				// 只推进确认修订，不改 Clear 前缀身份（boundary / clearContentId / prefixDegraded）。
				const std::lock_guard lock(mutex);
				auto& entry = confirmed[key];
				if (!onDisk)
				{
					entry.pageNumber = canvas.pageNumber;
					entry.slideId = canvas.slideId;
					entry.binding = EffectiveUInkBinding(canvas);
					entry.pageState = canvas.pageState;
					entry.pageKind = canvas.pageKind;
				}
				entry.revision = identity.revision;
				entry.coldUsage = coldUsage;
				completion.confirmedBoundary = entry.boundary;
				completion.confirmedRevision = entry.revision;
				completion.degraded = entry.degraded;
			}
			completion.result = PagedInkResult::Succeeded;
			completion.error = PagedInkError::None;
		}


		void PublishWriterState() noexcept
		{
			// 发布后即使重开失败，该服务也只认这个文件。
			file.sealed = writer ? writer->IsSealed() : !path.empty();
			file.tailUnconfirmed = writer && writer->IsTailUnconfirmed();
			file.failed = file.sealed || file.tailUnconfirmed;
		}

		bool PhysicallyOrdered() const noexcept
		{
			try
			{
				std::vector<std::pair<uint64_t, uint32_t>> order;
				order.reserve(writer->Directory().pages.size());
				for (const auto& [guid, page] : writer->Directory().pages) order.emplace_back(page.offset, page.pageIndex);
				std::sort(order.begin(), order.end());
				for (std::size_t index = 1; index < order.size(); ++index)
					if (order[index].second < order[index - 1].second) return false;
			}
			catch (...) {} // 分配失败：按已有序处理，跳过这次重排
			return true;
		}

		void CompactIfNeeded(std::unique_lock<std::mutex>& lock)
		{
			// 每次确认写入最多安排一次空闲检查。失败后等下次成功写入再试。
			compactCheckPending = false;
			if (!writer || file.failed || file.sealed || file.tailUnconfirmed || writer->IsSealed() ||
				writer->IsTailUnconfirmed() || damageTrackingIncomplete) return;

			const auto beforeBytes = writer->FileBytes();
			const auto supersededBytes = writer->SupersededBytes();
			// 离开白板 / 结束放映时的正序整理不受空闲阈值限制（C1.6b）。没有被替代字节但页的物理顺序
			// 与页号不一致（先写高页号再首次写低页号、回翻搬页）时同样整理。
			const bool forced = std::exchange(forceCompact, false);
			if (supersededBytes == 0 && !(forced && !PhysicallyOrdered())) return;
			const auto percent = testHook.compactMinSupersededPercent;
			// 用向上取整的整数阈值，避免把整文件字节乘以百分数。
			const bool overThreshold = forced ||
				(supersededBytes >= testHook.compactMinSupersededBytes &&
				percent <= 100 && supersededBytes >=
				beforeBytes / 100 * percent + (beforeBytes % 100 * percent + 99) / 100);

			const auto& pages = writer->Directory().pages;
			size_t currentDamage = 0;
			for (const auto& version : damagedVersions)
			{
				const auto page = pages.find(version.pageGuid);
				if (page != pages.end() && version.Matches(page->second)) ++currentDamage;
			}
			if (!overThreshold && currentDamage == damagedVersions.size()) return;

			// 队列选择与设置驱逐屏障共用同一把锁。整理本身在锁外进行，准入照常。
			busy = true;
			compacting = currentDamage == 0;
			lock.unlock();
			if (currentDamage != 0)
			{
				::Inkeys::Log::Debug(::Inkeys::Log::Module::UInk,
					"[UInkCompact] scene={} event=deferred reason=current_page_damaged pages={}",
					LogScene(), currentDamage);
			}
			else
			{
				::Inkeys::Log::Debug(::Inkeys::Log::Module::UInk,
					"[UInkCompact] scene={} event=start beforeBytes={} afterBytes={} supersededBytes={} pages={} elapsedMs=0",
					LogScene(), beforeBytes, beforeBytes, supersededBytes, pages.size());
				if (testHook.beforeCompact) testHook.beforeCompact(testHook.context);
				const auto started = Clock::now();
				const auto result = writer->Compact();
				const auto elapsedMs = ElapsedMs(started);
				const auto afterBytes = writer->FileBytes();
				// 最近的坏版本会阻止整理，因此一旦提交，记录的坏段都已随替代段移除。
				if (result.replacementCommitted) damagedVersions.clear();
				// 整理用新文件替换旧文件：偏移全部变化，先登记新的文件身份（同时清除写前登记）再处理后续请求。
				if (result.replacementCommitted && writer) (void)NoteRecoveryFile(writer->Path());
				if (result.success)
				{
					::Inkeys::Log::Debug(::Inkeys::Log::Module::UInk,
						"[UInkCompact] scene={} event=finish result=succeeded beforeBytes={} afterBytes={} supersededBytes={} pages={} elapsedMs={:.3f}",
						LogScene(), beforeBytes, afterBytes, supersededBytes, pages.size(), elapsedMs);
				}
				else
				{
					::Inkeys::Log::Error(::Inkeys::Log::Module::UInk,
						"[UInkCompact] scene={} event=finish result=failed beforeBytes={} afterBytes={} supersededBytes={} pages={} elapsedMs={:.3f} stage={} systemError={} reopenSystemError={} replacementCommitted={} sealed={}",
						LogScene(), beforeBytes, afterBytes, supersededBytes, pages.size(), elapsedMs,
						result.failureStage ? result.failureStage : "compact", result.systemError,
						result.reopenSystemError, static_cast<unsigned>(result.replacementCommitted),
						static_cast<unsigned>(writer->IsSealed()));
				}
				lock.lock();
				PublishWriterState();
				compacting = false;
				auto ready = file.failed ? FinishQueue(saves, PagedInkResult::FileFailed) : 0;
				// 整理没有业务回执。若曾挡住驱逐，复用控制唤醒来报告屏障解除。
				if (ready == 0 && evictionRetryPending) ready = 1;
				ready = RegisterWakes(ready);
				lock.unlock();
				Wake(ready);
			}
			lock.lock();
			busy = false;
		}

		void AdoptRecoveredPages() noexcept
		{
			if (options.adoptPages.empty()) return;
			{
				const std::lock_guard lock(mutex);
				busy = true;
			}
			std::size_t adopted = 0;
			try
			{
				PageWriteLog log;
				for (const auto& canvas : options.adoptPages)
				{
					PagedInkCompletion scratch;
					std::wstring published;
					// 与普通写入同一协议：首页建文件前登记候选路径，后续整页前登记起点。
					const bool hadWriter = writer != nullptr;
					if (hadWriter && !NoteRecoveryPending(kRecoveryPendingPage)) break;
					auto write = hadWriter ? writer->AppendPage(canvas) : CreatePagedFile(canvas, scratch, log);
					if (!write.success) break;
					if (hadWriter) ClearRecoveryPending();
					else (void)NoteRecoveryFile(writer->Path());
					ConfirmedPage saved;
					saved.revision = 1;
					{
						// 接管的页同样记下整页冷读用量，之后的逐笔续接才能按页预算累计。
						const auto cold = ImportUInkV11Page(canvas, options.importOptions);
						saved.coldUsage = ConfirmedPage::ColdUsage{ cold.footprintUsed, cold.pageTilesUsed, cold.geometryUsed };
						saved.degraded = Draw3UInkV11ColdPreflightFailure(cold) || cold.counts.invalidShapeSkipped != 0 ||
							cold.counts.geometryLimit != 0 || cold.counts.footprintBudget != 0;
					}
					if (const auto clear = LastClear(canvas, canvas.content.size()))
					{
						saved.boundary = PagedInkBoundaryIdentity{ 1, 1 };
						saved.clearContentId = std::get<UInkClear>(canvas.content[*clear]).contentId;
					}
					saved.pageNumber = canvas.pageNumber;
					saved.slideId = canvas.slideId;
					saved.binding = EffectiveUInkBinding(canvas);
					saved.pageState = canvas.pageState;
					saved.pageKind = canvas.pageKind;
					const std::lock_guard lock(mutex);
					confirmed[canvas.pageGuid.Bytes()] = saved;
					if (writer) path = writer->Path();
					++adopted;
				}
			}
			catch (...) {}
			::Inkeys::Log::Info(::Inkeys::Log::Module::UInk, "[UInkRecovery] scene={} event=adopt pages={} adopted={}",
				LogScene(), options.adoptPages.size(), adopted);
			std::vector<UInkCanvas>{}.swap(options.adoptPages);
			if (adopted != 0) adoptedNotice.store(true, std::memory_order_release);
			{
				const std::lock_guard lock(mutex);
				PublishWriterState();
				busy = false;
			}
			changed.notify_all();
		}

		void Run()
		{
			AdoptRecoveredPages();
			for (;;)
			{
				bool save = false;
				bool pendingSave = false;
				Clock::time_point dequeuedAt;
				{
					std::unique_lock lock(mutex);
					changed.wait(lock, [&]
					{
						if (!loads.empty()) return true;
						if (!saves.empty())
						{
							if (activeEvictions == 0) return true;
							if (testHook.onSaveBlockedByEviction)
								testHook.onSaveBlockedByEviction(testHook.context);
							return false;
						}
						if (closing) return true;
						if (!compactCheckPending) return false;
						// 保留挂起检查，最后一份驱逐许可归还时唤醒；期间仍优先处理业务请求。
						if (activeEvictions == 0) return true;
						if (testHook.onCompactBlockedByEviction)
							testHook.onCompactBlockedByEviction(testHook.context);
						return false;
					});
					if (loads.empty() && saves.empty())
					{
						if (closing) break;
						if (std::exchange(recoveryClearRetryPending, false) && recoveryPendingStale && writer)
						{
							// 清除写前登记失败后空闲重试一次（锁外写会话记录）；之后重新进入循环判断队列。
							lock.unlock();
							ClearRecoveryPending();
							recoveryClearRetryPending = false;
							// 清掉了：唤醒调用方重查恢复结算（此时可能没有别的回执会唤醒它）。
							if (!recoveryPendingStale && options.wake)
							{
								try { options.wake(options.wakeContext); }
								catch (...) {}
							}
							continue;
						}
						CompactIfNeeded(lock);
						lock.unlock();
						changed.notify_all();
						continue;
					}
					save = loads.empty();
					auto& queue = save ? saves : loads;
					active.splice(active.end(), queue, queue.begin());
					auto& next = active.front();
					dequeuedAt = Clock::now();
					next.completion.saveDequeued = save;
					next.completion.queueMs = std::chrono::duration<double, std::milli>(dequeuedAt - next.queuedAt).count();
					const auto& pageGuid = next.completion.identity.pageGuid;
					// 检查驱逐许可与标记在同一临界区完成，覆盖创建、追加及其回滚。
					if (save) activeSave = pageGuid;
					// 排队后才收到相关保存时，同样把整次加载交给调用方使用内存数据。
					else pendingSave = HasPendingLoadDependency(next.completion.identity);
					busy = true;
				}

				auto& node = active.front();
				publishedPath.clear();
				if (pendingSave) node.completion.error = PagedInkError::PendingWrite;
				else
				{
					const auto workStarted = Clock::now();
					const auto operation = save ? (writer ? "append" : "create") : ReadOperation(node.completion.identity.kind);
					PageWriteLog writeLog;
					std::optional<SpliceReadLog> spliceRead;
					try
					{
						if (testHook.beforeWork) testHook.beforeWork(testHook.context, node.completion.identity);
						if (save && node.completion.identity.kind == PagedInkRequestKind::Rotate)
						{
							// 换新文件：之前的写入都已落盘；关闭写入器，下一次写入按首次写入建新文件。
							// 先持久化“无活动区间文件”，否则崩溃恢复仍会读到清前的旧区间（失败时由绘制线程重试 Rotate）。
							bool fileCleared = true;
							if (options.clearRecoveryFile)
							{
								try { fileCleared = options.clearRecoveryFile(options.recoveryContext, node.baseRevision); }
								catch (...) { fileCleared = false; }
							}
							if (fileCleared) recoveryPendingStale = false;
							writer.reset();
							rotated = true;
							node.completion.result = fileCleared ? PagedInkResult::Succeeded : PagedInkResult::Failed;
						}
						else if (save && node.completion.identity.kind == PagedInkRequestKind::Append)
							AppendStroke(node, publishedPath, writeLog);
						else if (save) SavePage(node, publishedPath, writeLog, spliceRead);
						else if (node.completion.identity.kind == PagedInkRequestKind::LoadLane)
							LoadLane(node.completion);
						else
						{
							const auto& guid = node.completion.identity.pageGuid;
							const auto record = confirmed.find(guid.Bytes());
							LoadAndLogPage(guid, record == confirmed.end() ? nullptr : &record->second,
								node.completion, operation, node.expectedBoundary);
						}
					}
					catch (...)
					{
						node.completion.result = PagedInkResult::Failed;
						node.completion.error = PagedInkError::Exception;
						node.completion.lanePages.clear();
						if (!save) LogReadQueueFailure(operation, node.completion.identity.pageGuid, 0, node.completion.error);
					}
					// 前缀仍在手中时采集笔数，排除该扫描后再结束 workMs 采样。
					const auto countTime = spliceRead ? spliceRead->countTime : Clock::duration{};
					node.completion.workMs = ElapsedMs(workStarted + countTime);
					const auto& completion = node.completion;
					// 转码、格式化和提交放在采样之外。
					if (spliceRead && spliceRead->disk.attempted)
						LogRead("splice_prefix", completion.identity.pageGuid, spliceRead->pageNumber, spliceRead->disk,
							spliceRead->counts, spliceRead->fallbacks, completion.readMs, spliceRead->importMs,
							spliceRead->storedDegraded);
					if (save && (!spliceRead || !spliceRead->stoppedSave) &&
						(completion.result != PagedInkResult::Succeeded || completion.degraded))
					{
						// 使用已发布路径或本次 Create 的候选路径，输出后才进入回执临界区。
						const auto& filePath = writer ? writer->Path() :
							!publishedPath.empty() ? publishedPath : writeLog.createPath;
						const std::string_view error = completion.result == PagedInkResult::Succeeded ?
							std::string_view{} : PagedInkErrorName(completion.error);
						// 只补日志计数，回执仍保留原来的导出统计。
						auto counts = completion.fallbackCounts;
						counts.invalidShapeSkipped += writeLog.coldInvalidShapeSkipped;
						counts.geometryLimit += writeLog.coldGeometryLimit;
						counts.footprintBudget += writeLog.coldFootprintBudget;
						// 重开失败时还未发布 path，封闭状态也要考虑本次已发布路径。
						writeLog.facts.sealed = writer ? writer->IsSealed() : !publishedPath.empty() || !path.empty();
						writeLog.facts.tailUnconfirmed = writer && writer->IsTailUnconfirmed();
						WriteUInkWriteLog(LogScene(), operation, filePath, completion.identity.pageGuid,
							node.snapshot->pageNumber, error.empty() ? "committed" : writeLog.stage, error,
							completion.systemError, counts, completion.writeMs, completion.prefixReadDegraded, writeLog.facts);
					}
				}
				if (writer) node.completion.fileBytes = writer->FileBytes();

				std::size_t ready = 1;
				{
					const std::lock_guard lock(mutex);
					if (std::exchange(rotated, false))
					{
						path.clear();
						confirmed.clear();
						damagedVersions.clear();
						damageTrackingIncomplete = false;
					}
					if (!publishedPath.empty()) path.swap(publishedPath);
					if (save)
					{
						// 先发布封闭状态再撤下写入屏障，驱逐线程不会看到过期的健康状态。
						PublishWriterState();
						if (file.failed) node.completion.result = PagedInkResult::FileFailed;
						if (!closing && node.completion.result == PagedInkResult::Succeeded) compactCheckPending = true;
						// 写回执只报告确认写入的范围，失败时不能带上拼接前缀的旧页大小。
						if (node.completion.result != PagedInkResult::Succeeded) node.completion.pageBytes = 0;
					}
					Complete(active, active.begin());
					activeSave.reset();
					if (file.failed) ready += FinishQueue(saves, PagedInkResult::FileFailed);
					ready = RegisterWakes(ready);
					// 结果在解锁后可被取走，以同一个出队时刻衔接 queueMs。
					node.completion.workerMs = ElapsedMs(dequeuedAt);
				}
				Wake(ready);
				{
					const std::lock_guard lock(mutex);
					busy = false;
				}
				changed.notify_all();
			}
			writer.reset();
		}

		PagedInkStoreOptions options;
		PagedInkStoreTestHook testHook;
		std::mutex mutex;
		std::mutex joinMutex;
		std::condition_variable changed;
		Queue loads;
		Queue saves;
		Queue active;
		Queue completed;
		std::optional<UInkGuid> activeSave;
		bool closing = false;
		bool busy = false;
		bool compactCheckPending = false;
		bool forceCompact = false; // C1.6b 正序整理请求，下一次整理检查忽略空闲阈值
		bool rotated = false; // 仅 worker 线程：本次请求是换新文件
		// 会话记录里可能留有未清的写前登记：登记成功即置位，确认清除（或登记文件、Rotate 清条目）后复位。
		// 置位期间单块追加也先重新登记，覆盖旧起点。worker 写，绘制线程经 RecoveryPendingSettled 只读。
		std::atomic<bool> recoveryPendingStale{ false };
		bool recoveryClearRetryPending = false; // 仅 worker：清除失败后在下一次空闲时重试一次
		std::atomic<bool> adoptedNotice{ false };
		bool compacting = false;
		bool evictionRetryPending = false;
		std::size_t activeEvictions = 0;
		std::size_t pendingWakes = 0;
		std::wstring path;
		std::wstring publishedPath;
		PagedInkFileState file;
		// GUID 作键，范围和页序来自 L2。确认修订、边界与其余元数据共享一条记录。
		std::map<std::array<uint8_t, 16>, ConfirmedPage> confirmed;
		std::unique_ptr<UInkPagedFileWriter> writer;
		// 仅 worker 记录和检查物理版本，与业务层的确认修订、降级状态独立。
		std::vector<DamagedPageVersion> damagedVersions;
		bool damageTrackingIncomplete = false;
		std::thread worker;
	};

	PagedInkStore::PagedInkStore(PagedInkStoreOptions options, PagedInkStoreTestHook testHook)
		: impl_(std::make_unique<Impl>(std::move(options), testHook)) {}

	PagedInkStore::~PagedInkStore()
	{
		Close();
	}

	PagedInkAdmission PagedInkStore::Save(PagedInkSaveRequest request) noexcept
	{
		const auto& identity = request.identity;
		if (identity.kind == PagedInkRequestKind::Append)
		{
			// 只含这一笔、不带 Clear 与上一区间；目标修订必须晚于基准修订。
			if (impl_->options.root.empty() || identity.revision <= request.baseRevision ||
				identity.pageGuid != request.snapshot.pageGuid || request.previousIntervalOnDisk ||
				request.snapshot.strokes.empty() || !request.snapshot.operations.empty())
				return PagedInkAdmission::InvalidRequest;
			return impl_->Submit(identity, std::move(request.snapshot), std::nullopt, request.baseRevision);
		}
		if (impl_->options.root.empty() || identity.revision == 0 || identity.pageGuid != request.snapshot.pageGuid ||
			(identity.kind != PagedInkRequestKind::Clear && identity.kind != PagedInkRequestKind::Full) ||
			(identity.kind == PagedInkRequestKind::Clear &&
				(!identity.boundaryGeneration || request.previousIntervalOnDisk ||
					request.snapshot.operations.empty() ||
					!std::holds_alternative<Draw3UInkClearSnapshot>(request.snapshot.operations.back()))) ||
			(request.previousIntervalOnDisk && (request.previousIntervalOnDisk->revision == 0 ||
				identity.boundaryGeneration != request.previousIntervalOnDisk->generation)))
			return PagedInkAdmission::InvalidRequest;
		return impl_->Submit(identity, std::move(request.snapshot), request.previousIntervalOnDisk);
	}

	PagedInkAdmission PagedInkStore::Load(PagedInkLoadRequest request) noexcept
	{
		return impl_->Submit({ request.pageGuid, 0, request.slotGeneration, request.loadGeneration,
			std::nullopt, PagedInkRequestKind::Load });
	}

	PagedInkAdmission PagedInkStore::LoadLane(PagedInkLoadLaneRequest request) noexcept
	{
		if (request.binding != 0 && request.binding != 1) return PagedInkAdmission::InvalidRequest;
		return impl_->Submit({ {}, 0, request.slotGeneration, request.loadGeneration,
			std::nullopt, PagedInkRequestKind::LoadLane, request.binding });
	}

	PagedInkAdmission PagedInkStore::LoadPreviousInterval(PagedInkLoadPreviousRequest request) noexcept
	{
		if (request.boundary.revision == 0) return PagedInkAdmission::InvalidRequest;
		return impl_->Submit({ request.load.pageGuid, request.boundary.revision, request.load.slotGeneration,
			request.load.loadGeneration, request.boundary.generation, PagedInkRequestKind::LoadPreviousInterval },
			std::nullopt, request.boundary);
	}

	std::optional<PagedInkCompletion> PagedInkStore::Take() noexcept
	{
		static_assert(std::is_nothrow_move_constructible_v<PagedInkCompletion>);
		const std::lock_guard lock(impl_->mutex);
		if (impl_->completed.empty()) return std::nullopt;
		auto completion = std::move(impl_->completed.front().completion);
		impl_->completed.pop_front();
		return completion;
	}

	void PagedInkStore::Drain()
	{
		std::unique_lock lock(impl_->mutex);
		impl_->changed.wait(lock, [&]
			{ return impl_->loads.empty() && impl_->saves.empty() && !impl_->busy && !impl_->compactCheckPending; });
	}

	PagedInkAdmission PagedInkStore::RotateFile(const draw3::uink::UInkGuid& pageGuid, uint64_t loadGeneration,
		uint64_t clearGeneration) noexcept
	{
		if (impl_->options.naming.kind != PagedInkFileKind::Desktop || pageGuid.IsZero())
			return PagedInkAdmission::InvalidRequest;
		Draw3UInkCanvasSnapshot marker;
		marker.pageGuid = pageGuid;
		// Rotate 不用基准修订，借该字段把 Clear 代次带给 worker。
		return impl_->Submit({ pageGuid, 1, 0, loadGeneration, std::nullopt, PagedInkRequestKind::Rotate },
			std::move(marker), std::nullopt, clearGeneration);
	}

	void PagedInkStore::RequestOrderedCompact() noexcept
	{
		{
			const std::lock_guard lock(impl_->mutex);
			if (impl_->closing) return;
			// 排在已准入写入之后：worker 只在两条队列都空时做整理检查。
			impl_->forceCompact = true;
			impl_->compactCheckPending = true;
		}
		impl_->changed.notify_all();
	}

	void PagedInkStore::Close(PagedInkCloseMode mode)
	{
		std::size_t ready = 0;
		{
			const std::lock_guard lock(impl_->mutex);
			impl_->closing = true;
			impl_->evictionRetryPending = false;
			impl_->compactCheckPending = false;
			if (mode == PagedInkCloseMode::Cancel)
			{
				ready += impl_->FinishQueue(impl_->loads, PagedInkResult::Canceled);
				ready += impl_->FinishQueue(impl_->saves, PagedInkResult::Canceled);
			}
			ready = impl_->RegisterWakes(ready);
		}
		impl_->changed.notify_all();
		impl_->Wake(ready);
		{
			const std::lock_guard lock(impl_->joinMutex);
			if (impl_->worker.joinable()) impl_->worker.join();
		}
		// join 只覆盖后台线程，还要等提交线程与其他 Close 投递的唤醒完成。
		std::unique_lock lock(impl_->mutex);
		impl_->changed.wait(lock, [&] { return impl_->pendingWakes == 0; });
	}

	std::wstring PagedInkStore::FilePath() const
	{
		const std::lock_guard lock(impl_->mutex);
		return impl_->path;
	}

	bool PagedInkStore::TakeAdoptedNotice() noexcept
	{
		return impl_->adoptedNotice.exchange(false, std::memory_order_acq_rel);
	}

	bool PagedInkStore::RecoveryPendingSettled() const noexcept
	{
		return !impl_->recoveryPendingStale.load(std::memory_order_acquire);
	}

	PagedInkFileState PagedInkStore::FileState() const noexcept
	{
		const std::lock_guard lock(impl_->mutex);
		auto file = impl_->file;
		file.compacting = impl_->compacting;
		return file;
	}

	bool PagedInkStore::TryBeginEviction(bool requiresHealthyFile) noexcept
	{
		const std::lock_guard lock(impl_->mutex);
		if (impl_->closing) return false;
		if (impl_->activeSave || impl_->compacting)
		{
			impl_->evictionRetryPending = true;
			return false;
		}
		if (requiresHealthyFile && !IsEvictionFileHealthy(impl_->file)) return false;
		++impl_->activeEvictions;
		return true;
	}

	void PagedInkStore::RequestEvictionRetry() noexcept
	{
		const std::lock_guard lock(impl_->mutex);
		if (!impl_->closing) impl_->evictionRetryPending = true;
	}

	bool PagedInkStore::TakeEvictionRetry() noexcept
	{
		const std::lock_guard lock(impl_->mutex);
		return !impl_->closing && !impl_->activeSave && !impl_->compacting &&
			std::exchange(impl_->evictionRetryPending, false);
	}

	void PagedInkStore::EndEviction() noexcept
	{
		{
			const std::lock_guard lock(impl_->mutex);
			if (--impl_->activeEvictions != 0 || (impl_->saves.empty() && !impl_->compactCheckPending)) return;
		}
		impl_->changed.notify_all();
	}
}
