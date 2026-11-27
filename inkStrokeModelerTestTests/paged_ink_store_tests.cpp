#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../inkStrokeModelerTest/draw3/shape_core.h"
#include "snapshot_lifetime_probe.h"
#include "../Inkeys/Inkeys/Log/Log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import draw3.uink_store;
import Inkeys.Drawing.Draw3.paged_ink_store;

#if defined(DRAW3_TESTING)
void FailNextTestAllocation() noexcept;
bool CancelNextTestAllocation() noexcept;
uint64_t TestAllocationFailureCount() noexcept;
#endif

namespace
{
	using namespace Inkeys::Drawing::Draw3;
	using namespace draw3::uink;
	using namespace std::chrono_literals;

	struct TestState
	{
		const char* name;
		int failures = 0;

		bool Check(bool condition, const char* expression, int line)
		{
			if (!condition)
			{
				++failures;
				std::cerr << "PagedInkStore FAILED " << name << ':' << line << ": " << expression << '\n';
			}
			return condition;
		}
	};

#define PAGED_CHECK(...) test.Check(!!(__VA_ARGS__), #__VA_ARGS__, __LINE__)
#define PAGED_REQUIRE(...) do { if (!PAGED_CHECK(__VA_ARGS__)) return; } while (false)


	namespace StoreLog = Inkeys::Log;

	struct ScopedUInkLog
	{
		struct Row
		{
			StoreLog::Level level;
			DWORD thread;
			std::string text;
		};
		inline static ScopedUInkLog* active = nullptr;
		std::vector<Row> rows;

		ScopedUInkLog()
		{
			active = this;
			StoreLog::SetThreshold(StoreLog::Threshold::Debug);
			StoreLog::SetUInkTestSink([](StoreLog::Level level, std::string_view text) noexcept
			{
				try { active->rows.push_back({ level, GetCurrentThreadId(), std::string(text) }); }
				catch (...) {} // 缺少记录时由测试断言报告。
			});
		}

		~ScopedUInkLog()
		{
			StoreLog::SetUInkTestSink(nullptr);
			StoreLog::SetThreshold(StoreLog::Threshold::Info);
		}

		size_t Count(StoreLog::Level level, std::string_view text) const
		{
			return std::count_if(rows.begin(), rows.end(), [&](const Row& row)
			{
				return row.level == level && row.thread != GetCurrentThreadId() &&
					row.text.find(text) != std::string::npos;
			});
		}
	};

	struct DirectoryContents
	{
		std::vector<std::wstring> files;
		std::vector<std::wstring> children;
	};

	bool ListDirectory(const std::wstring& path, DirectoryContents& contents)
	{
		WIN32_FIND_DATAW entry{};
		const HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &entry);
		if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
		const std::unique_ptr<void, decltype(&FindClose)> handle(find, FindClose);
		do
		{
			const std::wstring_view name(entry.cFileName);
			if (name == L"." || name == L"..") continue;
			const auto child = path + L"\\" + entry.cFileName;
			if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				contents.children.push_back(child);
				if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
					!ListDirectory(child, contents)) return false;
			}
			else contents.files.push_back(child);
		} while (FindNextFileW(handle.get(), &entry));
		return GetLastError() == ERROR_NO_MORE_FILES;
	}

	class TempDirectory
	{
	public:
		TempDirectory()
		{
			std::array<wchar_t, 32768> temp{};
			const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
			const auto id = CreateUInkGuid();
			if (length == 0 || length >= temp.size() || !id) return;
			const auto suffix = FormatUInkGuid(*id);
			auto path = std::wstring(temp.data(), length) + L"Inkeys-PagedInkStore-" +
				std::wstring(suffix.begin(), suffix.end());
			if (CreateDirectoryW(path.c_str(), nullptr)) path_ = std::move(path);
		}

		~TempDirectory() { (void)Clean(); }
		TempDirectory(const TempDirectory&) = delete;
		TempDirectory& operator=(const TempDirectory&) = delete;
		const std::wstring& Path() const { return path_; }

		bool Clean() noexcept
		{
			if (path_.empty()) return true;
			try
			{
				// 只遍历本用例成功创建的唯一临时目录，不跟随重解析点。
				DirectoryContents contents;
				if (!ListDirectory(path_, contents)) return false;
				for (const auto& path : contents.files)
					if (!DeleteFileW(path.c_str())) return false;
				for (auto path = contents.children.rbegin(); path != contents.children.rend(); ++path)
					if (!RemoveDirectoryW(path->c_str())) return false;
				if (!RemoveDirectoryW(path_.c_str())) return false;
				path_.clear();
				return true;
			}
			catch (...) { return false; }
		}

	private:
		std::wstring path_;
	};

	struct WakeCounter
	{
		static void Wake(void* context) noexcept
		{
			auto& self = *static_cast<WakeCounter*>(context);
			++self.count;
			self.arrived.release();
		}
		bool Wait() { return arrived.try_acquire_for(10s); }

		std::atomic<unsigned> count{ 0 };
		std::counting_semaphore<64> arrived{ 0 };
	};

	struct WorkGate
	{
		uint64_t pauseRevision;
		uint64_t throwRevision = 0;
		std::binary_semaphore entered{ 0 };
		std::binary_semaphore resume{ 0 };
		uint64_t throwLaneGeneration = 0;

		static void BeforeWork(void* context, const PagedInkRequestIdentity& identity)
		{
			auto& self = *static_cast<WorkGate*>(context);
			if ((self.throwRevision != 0 && identity.revision == self.throwRevision) ||
				(self.throwLaneGeneration != 0 && identity.kind == PagedInkRequestKind::LoadLane &&
					identity.loadGeneration == self.throwLaneGeneration))
				throw std::runtime_error("paged ink test worker exception");
			if (identity.revision != self.pauseRevision) return;
			self.entered.release();
			self.resume.acquire();
		}
	};

	struct ReleaseWork
	{
		WorkGate& gate;
		bool released = false;
		~ReleaseWork() { Release(); }
		void Release()
		{
			if (released) return;
			released = true;
			gate.resume.release();
		}
	};

	struct WakeGate
	{
		WorkGate work{ 1 };
		WakeCounter wake;
		std::atomic<bool> pauseBeforeWake{ true };
		std::atomic<bool> pauseInWake{ false };
		std::binary_semaphore beforeWake{ 0 };
		std::binary_semaphore sendWake{ 0 };
		std::binary_semaphore inWake{ 0 };
		std::binary_semaphore finishWake{ 0 };

		static void BeforeWork(void* context, const PagedInkRequestIdentity& identity)
		{
			auto& self = *static_cast<WakeGate*>(context);
			WorkGate::BeforeWork(&self.work, identity);
		}

		static void BeforeWake(void* context) noexcept
		{
			auto& self = *static_cast<WakeGate*>(context);
			if (!self.pauseBeforeWake.exchange(false)) return;
			self.beforeWake.release();
			self.sendWake.acquire();
		}

		static void Wake(void* context) noexcept
		{
			auto& self = *static_cast<WakeGate*>(context);
			if (self.pauseInWake.exchange(false))
			{
				self.inWake.release();
				self.finishWake.acquire();
			}
			WakeCounter::Wake(&self.wake);
		}
	};

	struct ReleaseSignal
	{
		std::binary_semaphore& signal;
		bool released = false;
		~ReleaseSignal() { Release(); }
		void Release()
		{
			if (released) return;
			released = true;
			signal.release();
		}
	};

	struct ScopedFaults
	{
		explicit ScopedFaults(const UInkStoreTestFaultInjection& faults)
		{
			SetUInkStoreTestFaultInjection(faults);
		}
		~ScopedFaults() { ResetUInkStoreTestFaultInjection(); }
	};

	struct FilePatch
	{
		std::string key;
		size_t occurrence;
		size_t byteOffset;
		std::string replacement;
	};

	struct CorruptPageFile
	{
		std::vector<FilePatch> patches;
		bool patched = false;

		static std::optional<std::string> ReadBytes(HANDLE handle)
		{
			LARGE_INTEGER fileSize{};
			if (!GetFileSizeEx(handle, &fileSize) || fileSize.QuadPart <= 0 ||
				static_cast<uint64_t>(fileSize.QuadPart) > kUInkStoreMaxPageBytes) return std::nullopt;
			std::string bytes(static_cast<size_t>(fileSize.QuadPart), '\0');
			DWORD read = 0;
			if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
				read != bytes.size()) return std::nullopt;
			return bytes;
		}

		static std::optional<std::string> ReadBytes(const std::wstring& path)
		{
			const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
			const std::unique_ptr<void, decltype(&CloseHandle)> reader(handle, CloseHandle);
			return ReadBytes(handle);
		}

		bool PatchBytes(std::string& bytes) const
		{
			for (const auto& patch : patches)
			{
				auto position = bytes.find(patch.key);
				for (size_t index = 1; index < patch.occurrence && position != std::string::npos; ++index)
					position = bytes.find(patch.key, position + patch.key.size());
				if (position == std::string::npos || patch.byteOffset > patch.key.size() ||
					patch.replacement.size() > patch.key.size() - patch.byteOffset) return false;
				// 等长替换，保留确认目录中的页段范围。
				bytes.replace(position + patch.byteOffset, patch.replacement.size(), patch.replacement);
			}
			return true;
		}

		static bool WriteBytes(HANDLE handle, const std::string& bytes)
		{
			LARGE_INTEGER start{};
			DWORD written = 0;
			return SetFilePointerEx(handle, start, nullptr, FILE_BEGIN) &&
				WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
				written == bytes.size() && FlushFileBuffers(handle);
		}

		static void BeforeRead(void* context, const std::wstring& path)
		{
			auto& testFile = *static_cast<CorruptPageFile*>(context);
			const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
				0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE) return;
			const std::unique_ptr<void, decltype(&CloseHandle)> file(handle, CloseHandle);
			auto bytes = ReadBytes(handle);
			testFile.patched = bytes && testFile.PatchBytes(*bytes) && WriteBytes(handle, *bytes);
		}

		bool WriteDamagedCopy(const std::wstring& source, const std::wstring& destination) const
		{
			const HANDLE input = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (input == INVALID_HANDLE_VALUE) return false;
			const std::unique_ptr<void, decltype(&CloseHandle)> reader(input, CloseHandle);
			auto bytes = ReadBytes(input);
			if (!bytes || !PatchBytes(*bytes)) return false;
			const HANDLE output = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (output == INVALID_HANDLE_VALUE) return false;
			const std::unique_ptr<void, decltype(&CloseHandle)> writer(output, CloseHandle);
			return WriteBytes(output, *bytes);
		}
	};

	struct OverridePageRead
	{
		std::wstring path;
		unsigned remaining = 0;

		static const std::wstring* GetPath(void* context, const std::wstring&)
		{
			auto& self = *static_cast<OverridePageRead*>(context);
			if (self.remaining == 0) return nullptr;
			--self.remaining;
			return &self.path;
		}
	};

	bool WriterDeniesExternalWrite(const std::wstring& path)
	{
		const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
		CloseHandle(handle);
		return false;
	}

	UInkGuid Guid(uint8_t id)
	{
		return UInkGuid({ 0xab, 0x12, 0xcd, 0x34, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, id });
	}

	PagedInkStoreOptions Options(const std::wstring& root, WakeCounter& wake)
	{
		PagedInkStoreOptions options;
		options.root = root;
		options.pageContext.workspaceGuid = Guid(0xf0);
		options.pageContext.workspaceType = 1;
		options.wake = WakeCounter::Wake;
		options.wakeContext = &wake;
		return options;
	}

	PagedInkStoreOptions Options(const TempDirectory& root, WakeCounter& wake)
	{
		return Options(root.Path(), wake);
	}

	PagedInkStoreOptions PresentationOptions(const std::wstring& root, WakeCounter& wake)
	{
		auto options = Options(root, wake);
		options.naming = { PagedInkFileKind::Presentation, L"Lane test", Guid(0xfe) };
		options.pageContext.workspaceType = 2;
		options.hostId = FormatUInkGuid(options.naming.presentationKey);
		return options;
	}

	PagedInkStoreOptions PresentationOptions(const TempDirectory& root, WakeCounter& wake)
	{
		return PresentationOptions(root.Path(), wake);
	}

	Draw3UInkStrokeSnapshot Pen(float x, uint32_t group = 0)
	{
		Draw3UInkStrokeSnapshot stroke;
		stroke.style.fallbackRgb = 0x123456;
		stroke.style.opacity = 0.75f;
		stroke.points = { { x, 8.0f, 4.0f }, { x + 5.0f, 10.0f, 5.0f } };
		stroke.undoId = group;
		return stroke;
	}

	Draw3UInkCanvasSnapshot Page(uint8_t id, uint32_t pageIndex, float x)
	{
		Draw3UInkCanvasSnapshot page;
		page.pageGuid = Guid(id);
		page.pageIndex = pageIndex;
		page.pageNumber = pageIndex + 1;
		page.strokes.push_back(Pen(x));
		return page;
	}

	Draw3UInkCanvasSnapshot PresentationPage(uint8_t id, uint32_t pageIndex, float x,
		int32_t binding, std::optional<int32_t> slideId = std::nullopt)
	{
		auto page = Page(id, pageIndex, x);
		if (binding == 1) page.binding = 1;
		page.slideId = slideId;
		return page;
	}

	Draw3UInkCanvasSnapshot BoundaryPage(uint8_t id, uint32_t pageIndex, float previous,
		std::optional<float> current = std::nullopt)
	{
		auto page = Page(id, pageIndex, previous);
		page.strokes.clear();
		page.operations = { Pen(previous, 8), Pen(previous + 1, 8), Draw3UInkClearSnapshot{ 9 } };
		page.intervalOrdinal = 1;
		if (current)
		{
			page.strokes = { Pen(*current, 10), Pen(*current + 1, 10) };
			for (const auto& stroke : page.strokes) page.operations.emplace_back(stroke);
		}
		return page;
	}

	std::optional<draw3::shapes::ShapeObject> LineShape(uint8_t id, float x)
	{
		using namespace draw3::shapes;
		Construction construction;
		construction.tool.preset = PresetKind::Line;
		construction.start = { x, 20 };
		construction.end = { x + 30, 35 };
		ShapeStyle style;
		style.strokeRgb = 0x875421;
		style.width = 3.5f;
		return MakeConstructedShape(construction, style, Guid(id).Bytes(), 7);
	}

	std::optional<draw3::shapes::ShapeObject> ArrowShape(uint8_t id)
	{
		using namespace draw3::shapes;
		Construction construction;
		construction.tool.preset = PresetKind::Arrow;
		construction.tool.creationMode = DefaultCreationMode(PresetKind::Arrow);
		construction.start = { 20,30 };
		construction.end = { 140,110 };
		ShapeStyle style;
		style.width = 3.25f;
		return MakeConstructedShape(construction, style, Guid(id).Bytes());
	}

	Draw3UInkStrokeSnapshot ShapeStroke(const draw3::shapes::ShapeObject& shape, uint32_t group)
	{
		Draw3UInkStrokeSnapshot stroke;
		stroke.style.kind = Draw3UInkStrokeKind::Shape;
		stroke.shape = shape;
		stroke.undoId = group;
		return stroke;
	}

	constexpr uint64_t kSlot = 27;

	PagedInkSaveRequest SavePage(Draw3UInkCanvasSnapshot page, uint64_t revision,
		PagedInkRequestKind kind = PagedInkRequestKind::Full,
		std::optional<uint64_t> boundaryGeneration = std::nullopt,
		std::optional<PagedInkBoundaryIdentity> disk = std::nullopt)
	{
		return { { page.pageGuid, revision, kSlot, 0, boundaryGeneration, kind }, std::move(page), disk };
	}

	std::vector<PagedInkCompletion> TakeAll(PagedInkStore& store)
	{
		std::vector<PagedInkCompletion> results;
		while (auto complete = store.Take()) results.push_back(std::move(*complete));
		return results;
	}

	bool HasPen(const Draw3UInkStrokeSnapshot& stroke, float x)
	{
		return stroke.style.kind == Draw3UInkStrokeKind::Pen && stroke.points.size() == 2 &&
			stroke.points[0].x == x && stroke.points[1].x == x + 5;
	}

	bool Digits(std::wstring_view text)
	{
		return std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
	}

		// ---- C1.6b 逐笔续接（Append） ----
	struct RecoveryHookLog
	{
		int files = 0;
		int pendings = 0;
		int clears = 0;
		uint8_t lastKind = 0xff;
		uint64_t lastStart = 0;
		bool failPending = false; // 模拟会话记录写盘失败
		bool failClear = false;

		static bool File(void* context, const std::wstring&) noexcept { ++static_cast<RecoveryHookLog*>(context)->files; return true; }
		static bool Pending(void* context, const std::wstring&, uint64_t start, uint8_t kind) noexcept
		{
			auto& self = *static_cast<RecoveryHookLog*>(context);
			++self.pendings;
			self.lastKind = kind;
			self.lastStart = start;
			return !self.failPending;
		}
		static bool Clear(void* context, const std::wstring&) noexcept
		{
			auto& self = *static_cast<RecoveryHookLog*>(context);
			++self.clears;
			return !self.failClear;
		}
	};

	PagedInkSaveRequest AppendRequest(uint8_t id, uint32_t pageIndex, Draw3UInkStrokeSnapshot stroke,
		uint64_t base, uint64_t revision)
	{
		Draw3UInkCanvasSnapshot page;
		page.pageGuid = Guid(id);
		page.pageIndex = pageIndex;
		page.pageNumber = pageIndex + 1;
		page.strokes.push_back(std::move(stroke));
		PagedInkSaveRequest request{ { page.pageGuid, revision, kSlot, 0, std::nullopt, PagedInkRequestKind::Append },
			std::move(page), std::nullopt };
		request.baseRevision = base;
		return request;
	}

	std::optional<PagedInkCompletion> SaveAndTake(PagedInkStore& store, PagedInkSaveRequest request)
	{
		if (store.Save(std::move(request)) != PagedInkAdmission::Accepted) return std::nullopt;
		store.Drain();
		return store.Take();
	}

	std::optional<Draw3UInkCanvasSnapshot> LoadSnapshot(PagedInkStore& store, uint8_t id, uint64_t generation)
	{
		if (store.Load({ Guid(id), kSlot, generation }) != PagedInkAdmission::Accepted) return std::nullopt;
		store.Drain();
		auto loaded = store.Take();
		if (!loaded || loaded->result != PagedInkResult::Succeeded || !loaded->snapshot) return std::nullopt;
		return std::move(loaded->snapshot);
	}

	void TestAppendStrokeContinuesPage(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		RecoveryHookLog hooks;
		auto options = Options(root, wake);
		options.recoveryContext = &hooks;
		options.registerRecoveryFile = RecoveryHookLog::File;
		options.registerRecoveryPending = RecoveryHookLog::Pending;
		options.clearRecoveryPending = RecoveryHookLog::Clear;
		PagedInkStore store(options);

		// 新页：第一笔建文件（发布前登记候选路径，建好后登记文件身份）。
		auto first = SaveAndTake(store, AppendRequest(1, 0, Pen(1), 0, 1));
		PAGED_REQUIRE(first && first->result == PagedInkResult::Succeeded && first->confirmedRevision == 1);
		PAGED_CHECK(hooks.files == 2 && hooks.pendings == 0);
		// 续接到最后一个 Canvas：单块不登记。
		auto second = SaveAndTake(store, AppendRequest(1, 0, Pen(20), 1, 2));
		PAGED_REQUIRE(second && second->result == PagedInkResult::Succeeded && second->confirmedRevision == 2);
		PAGED_CHECK(hooks.pendings == 0);
		auto loaded = LoadSnapshot(store, 1, 11);
		PAGED_REQUIRE(loaded && loaded->strokes.size() == 2);
		PAGED_CHECK(HasPen(loaded->strokes[0], 1) && HasPen(loaded->strokes[1], 20));
		PAGED_CHECK(loaded->strokes[0].undoId != loaded->strokes[1].undoId);

		// 基准失配：前面有写入没确认，不续接。
		auto mismatch = SaveAndTake(store, AppendRequest(1, 0, Pen(30), 5, 6));
		PAGED_REQUIRE(mismatch && mismatch->result != PagedInkResult::Succeeded);
		PAGED_CHECK(mismatch->error == PagedInkError::AppendBaseMismatch);

		// 第二页成为最后一个 Canvas；回到第一页落笔时先搬页（登记 Relocate）再续接。
		auto other = SaveAndTake(store, AppendRequest(2, 1, Pen(40), 0, 1));
		PAGED_REQUIRE(other && other->result == PagedInkResult::Succeeded);
		auto back = SaveAndTake(store, AppendRequest(1, 0, Pen(50), 2, 3));
		PAGED_REQUIRE(back && back->result == PagedInkResult::Succeeded && back->confirmedRevision == 3);
		PAGED_CHECK(hooks.pendings == 2 && hooks.lastKind == 2 && hooks.clears == 2); // 第二页 AppendPage + 搬页
		loaded = LoadSnapshot(store, 1, 12);
		PAGED_REQUIRE(loaded && loaded->strokes.size() == 3);
		PAGED_CHECK(HasPen(loaded->strokes[2], 50));
		const auto beforeCompact = store.FileState();
		PAGED_CHECK(!beforeCompact.failed);

		// 续接之后的整页版本照常替换；整页准入不越过 Append 合并。
		auto page = Page(1, 0, 1);
		page.strokes.push_back(Pen(20, 1));
		page.strokes.push_back(Pen(50, 2));
		page.strokes.push_back(Pen(60, 3));
		auto full = SaveAndTake(store, SavePage(page, 4));
		PAGED_REQUIRE(full && full->result == PagedInkResult::Succeeded && full->confirmedRevision == 4);
		auto afterFull = SaveAndTake(store, AppendRequest(1, 0, Pen(70), 4, 5));
		PAGED_REQUIRE(afterFull && afterFull->result == PagedInkResult::Succeeded);

		// 正序整理：不受空闲阈值限制，整理后登记新的文件身份。
		const int filesBefore = hooks.files;
		store.RequestOrderedCompact();
		store.Drain();
		PAGED_CHECK(hooks.files == filesBefore + 1);
		const auto wire = ReadUInkStoreFile(store.FilePath());
		PAGED_REQUIRE(wire.status == UInkReadStatus::Complete && wire.document && wire.document->canvases.size() == 2);
		loaded = LoadSnapshot(store, 1, 13);
		PAGED_REQUIRE(loaded && loaded->strokes.size() == 5);
		PAGED_CHECK(HasPen(loaded->strokes[4], 70));

		// 准入校验：目标修订不晚于基准、含多笔或带 Clear 操作都拒绝。
		PAGED_CHECK(store.Save(AppendRequest(1, 0, Pen(1), 5, 5)) == PagedInkAdmission::InvalidRequest);
		auto twoStrokes = AppendRequest(1, 0, Pen(1), 5, 6);
		twoStrokes.snapshot.operations.push_back(Draw3UInkClearSnapshot{});
		PAGED_CHECK(store.Save(std::move(twoStrokes)) == PagedInkAdmission::InvalidRequest);
		(void)TakeAll(store);
	}

	// 写前登记失败拒绝写入；清除失败后，下一次单块追加先重新登记，覆盖盘上残留的旧起点。
	void TestAppendRecoveryRegistrationFailures(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		RecoveryHookLog hooks;
		auto options = Options(root, wake);
		options.recoveryContext = &hooks;
		options.registerRecoveryFile = RecoveryHookLog::File;
		options.registerRecoveryPending = RecoveryHookLog::Pending;
		options.clearRecoveryPending = RecoveryHookLog::Clear;
		PagedInkStore store(options);
		PAGED_REQUIRE(SaveAndTake(store, AppendRequest(1, 0, Pen(1), 0, 1)));
		PAGED_REQUIRE(SaveAndTake(store, AppendRequest(2, 1, Pen(2), 0, 1)));

		// 回翻搬页需要登记，登记失败：不写，修订不推进。
		hooks.failPending = true;
		auto refused = SaveAndTake(store, AppendRequest(1, 0, Pen(3), 1, 2));
		PAGED_REQUIRE(refused && refused->result != PagedInkResult::Succeeded);
		PAGED_CHECK(refused->confirmedRevision != 2 && !store.FileState().failed);
		hooks.failPending = false;

		// 搬页成功但清除失败：之后续接最后一个 Canvas 的单块也须先登记。
		hooks.failClear = true;
		auto moved = SaveAndTake(store, AppendRequest(1, 0, Pen(3), 1, 2));
		PAGED_REQUIRE(moved && moved->result == PagedInkResult::Succeeded);
		hooks.failClear = false;
		const int pendingsBefore = hooks.pendings;
		const int clearsBefore = hooks.clears;
		auto single = SaveAndTake(store, AppendRequest(1, 0, Pen(4), 2, 3));
		PAGED_REQUIRE(single && single->result == PagedInkResult::Succeeded);
		PAGED_CHECK(hooks.pendings == pendingsBefore + 1 && hooks.clears == clearsBefore + 1);
		// 残留已清掉：再续接一笔不再登记。
		auto next = SaveAndTake(store, AppendRequest(1, 0, Pen(5), 3, 4));
		PAGED_REQUIRE(next && next->result == PagedInkResult::Succeeded);
		PAGED_CHECK(hooks.pendings == pendingsBefore + 1);
		auto loaded = LoadSnapshot(store, 1, 31);
		PAGED_REQUIRE(loaded && loaded->strokes.size() == 4);
		(void)TakeAll(store);
	}

	void TestAdoptRecoveredPages(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		std::wstring oldPath;
		{
			PagedInkStore old(Options(root, wake));
			auto first = Page(1, 0, 1);
			first.strokes.push_back(Pen(20, 1));
			PAGED_REQUIRE(SaveAndTake(old, SavePage(first, 1)) && old.FilePath().size());
			PAGED_REQUIRE(SaveAndTake(old, SavePage(Page(2, 3, 40), 1)));
			oldPath = old.FilePath();
		}
		const auto wire = ReadUInkStoreFile(oldPath);
		PAGED_REQUIRE(wire.status == UInkReadStatus::Complete && wire.document && wire.document->canvases.size() == 2);

		// 新进程：worker 先接管旧页（重新编码写进新文件），之后的读取如同本进程写过。
		auto options = Options(root, wake);
		options.adoptPages = wire.document->canvases;
		PagedInkStore adopted(options);
		auto loaded = LoadSnapshot(adopted, 1, 21);
		PAGED_REQUIRE(loaded && loaded->strokes.size() == 2);
		PAGED_CHECK(HasPen(loaded->strokes[0], 1) && HasPen(loaded->strokes[1], 20));
		auto other = LoadSnapshot(adopted, 2, 22);
		PAGED_REQUIRE(other && other->pageIndex == 3 && other->strokes.size() == 1);
		PAGED_CHECK(!adopted.FileState().failed && !adopted.FilePath().empty() && adopted.FilePath() != oldPath);
		// 接管的页以修订 1 确认：基准 1 的续接可以直接接上。
		auto append = SaveAndTake(adopted, AppendRequest(1, 0, Pen(60), 1, 2));
		PAGED_REQUIRE(append && append->result == PagedInkResult::Succeeded);
		loaded = LoadSnapshot(adopted, 1, 23);
		PAGED_REQUIRE(loaded && loaded->strokes.size() == 3);
	}

void TestAppendReplaceAndLoad(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		DirectoryContents initial;
		PAGED_REQUIRE(store.FilePath().empty() && ListDirectory(root.Path(), initial) && initial.files.empty());
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto first = store.Take();
		PAGED_REQUIRE(first && first->result == PagedInkResult::Succeeded && !first->file.failed);
		PAGED_CHECK(first->identity.pageGuid == Guid(1) && first->identity.revision == 1 &&
			first->identity.slotGeneration == kSlot && first->identity.kind == PagedInkRequestKind::Full);
		const auto path = store.FilePath();
		PAGED_REQUIRE(!path.empty());
		const auto wire = ReadUInkStoreFile(path);
		PAGED_REQUIRE(wire.status == UInkReadStatus::Complete && wire.document && wire.document->headerExtension);
		const auto& extension = *wire.document->headerExtension;
		PAGED_REQUIRE(extension.workspaces.size() == 1 && wire.document->canvases.size() == 1);
		PAGED_CHECK(extension.workspaces[0].guid == Guid(0xf0) && extension.workspaces[0].workspaceType == 1 &&
			extension.workspaces[0].currentPageIndex == 0);

		// 外来 Workspace 不能借本白板的 Header 发布文件。
		auto foreignPage = wire.document->canvases.front();
		foreignPage.workspaceGuid = Guid(0xef);
		const auto foreignPath = root.Path() + L"\\foreign-workspace.uink";
		UInkStoreWriteResult foreignResult;
		PAGED_CHECK(!UInkPagedFileWriter::Create(foreignPath, wire.document->header,
			wire.document->headerExtension, foreignPage, foreignResult));
		PAGED_CHECK(!foreignResult.success && !foreignResult.published &&
			foreignResult.diagnostics.size() == 1 &&
			foreignResult.diagnostics.front().code == UInkDiagnosticCode::InvalidFieldValue &&
			foreignResult.diagnostics.front().fieldPath == "canvas.identity");
		PAGED_CHECK(GetFileAttributesW(foreignPath.c_str()) == INVALID_FILE_ATTRIBUTES);

		// 写入器持有文件期间，外部句柄不能改写或替换正在使用的版本。
		for (const DWORD access : { static_cast<DWORD>(GENERIC_WRITE), static_cast<DWORD>(DELETE) })
		{
			const HANDLE foreign = CreateFileW(path.c_str(), access,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			const DWORD error = foreign == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
			if (foreign != INVALID_HANDLE_VALUE) CloseHandle(foreign);
			PAGED_CHECK(error == ERROR_SHARING_VIOLATION);
		}

		PAGED_REQUIRE(!wire.document->header.guid.IsZero());
		const auto guid = FormatUInkGuid(wire.document->header.guid).substr(0, 8);
		const auto prefix = root.Path() + L"\\whiteboard\\";
		const auto suffix = L"_" + std::wstring(guid.begin(), guid.end()) + L".uink";
		PAGED_REQUIRE(path.starts_with(prefix) && path.ends_with(suffix));
		const auto stamp = std::wstring_view(path).substr(prefix.size(), path.size() - prefix.size() - suffix.size());
		PAGED_REQUIRE(stamp.size() == 20);
		PAGED_CHECK(Digits(stamp.substr(0, 4)) && stamp[4] == L'-' && Digits(stamp.substr(5, 2)) &&
			stamp[7] == L'-' && Digits(stamp.substr(8, 2)) && stamp[10] == L'\\' && Digits(stamp.substr(11)));

		const auto shape = LineShape(0xf1, 12);
		PAGED_REQUIRE(shape);
		auto replacement = Page(1, 0, 99);
		replacement.background = Draw3UInkBackground{ 0xd0e0c0, 2 };
		replacement.viewport = { -120.5f, 31.75f, 1 };
		replacement.strokes.push_back(ShapeStroke(*shape, 1));
		auto eraser = Pen(55, 2);
		eraser.style.kind = Draw3UInkStrokeKind::Eraser;
		eraser.style.eraserShape = 1;
		replacement.strokes.push_back(eraser);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 7, 27), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(replacement, 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saves = TakeAll(store);
		PAGED_REQUIRE(saves.size() == 2);
		PAGED_CHECK(saves[0].identity.pageGuid == Guid(2) && saves[0].result == PagedInkResult::Succeeded);
		PAGED_CHECK(saves[1].identity.pageGuid == Guid(1) && saves[1].identity.revision == 2 &&
			saves[1].result == PagedInkResult::Succeeded && saves[1].fallbackCounts.invalidShapeSkipped == 0);
		DirectoryContents final;
		PAGED_REQUIRE(ListDirectory(root.Path(), final));
		PAGED_CHECK(store.FilePath() == path && final.files == std::vector{ path });

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 31 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(2), kSlot, 32 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loads = TakeAll(store);
		PAGED_REQUIRE(loads.size() == 2 && loads[0].result == PagedInkResult::Succeeded && loads[0].snapshot &&
			loads[1].result == PagedInkResult::Succeeded && loads[1].snapshot);
		PAGED_CHECK(loads[0].identity.kind == PagedInkRequestKind::Load && loads[0].identity.slotGeneration == kSlot &&
			loads[0].identity.loadGeneration == 31 && loads[1].identity.loadGeneration == 32);
		PAGED_REQUIRE(loads[0].readSummary && loads[0].readStatus == UInkReadStatus::Complete);
		PAGED_CHECK(loads[0].readSummary->outcome == UInkReadOutcome::Complete &&
			loads[0].fallbackCounts.invalidShapeSkipped == 0 && loads[0].fallbackCounts.eraserSize == 0);
		const auto& actual = *loads[0].snapshot;
		PAGED_REQUIRE(actual.background && actual.strokes.size() == 3);
		PAGED_CHECK(actual.pageGuid == Guid(1) && actual.pageIndex == replacement.pageIndex &&
			actual.pageNumber == replacement.pageNumber && actual.intervalOrdinal == 0);
		PAGED_CHECK(actual.background->rgb == replacement.background->rgb &&
			actual.background->pattern == replacement.background->pattern);
		PAGED_CHECK(actual.viewport.x == replacement.viewport.x && actual.viewport.y == replacement.viewport.y &&
			actual.viewport.scale == replacement.viewport.scale);
		PAGED_CHECK(HasPen(actual.strokes[0], 99) && actual.strokes[0].style.fallbackRgb == 0x123456 &&
			actual.strokes[0].style.opacity == 0.75f);
		PAGED_CHECK(actual.strokes[1].style.kind == Draw3UInkStrokeKind::Shape && actual.strokes[1].shape == shape);
		const auto& actualEraser = actual.strokes[2];
		PAGED_REQUIRE(actualEraser.points.size() == 2);
		PAGED_CHECK(actualEraser.style.kind == Draw3UInkStrokeKind::Eraser && actualEraser.style.eraserShape == 1 &&
			actualEraser.points[0].x == 55 && std::abs(actualEraser.points[0].width - 4.0f) < 0.001f);
		const auto& second = *loads[1].snapshot;
		PAGED_REQUIRE(second.strokes.size() == 1);
		PAGED_CHECK(second.pageGuid == Guid(2) && second.pageIndex == 7 && HasPen(second.strokes[0], 27));
		PAGED_CHECK(wake.count == 5 && !store.Take());
	}

	void CheckIntervals(TestState& test, const Draw3UInkCanvasSnapshot& page, float previous,
		std::optional<float> current)
	{
		PAGED_REQUIRE(page.operations.size() == (current ? 5u : 3u) && page.strokes.size() == (current ? 2u : 0u));
		PAGED_CHECK(page.intervalOrdinal == 1);
		const auto* first = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[0]);
		const auto* second = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[1]);
		const auto* clear = std::get_if<Draw3UInkClearSnapshot>(&page.operations[2]);
		PAGED_REQUIRE(first && second && clear);
		PAGED_CHECK(HasPen(*first, previous) && HasPen(*second, previous + 1));
		PAGED_CHECK(first->undoId == 0 && second->undoId == 0 && clear->undoId == 1);
		if (current)
		{
			const auto* a = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[3]);
			const auto* b = std::get_if<Draw3UInkStrokeSnapshot>(&page.operations[4]);
			PAGED_REQUIRE(a && b);
			PAGED_CHECK(HasPen(*a, *current) && HasPen(*b, *current + 1) && a->undoId == 2 && b->undoId == 2);
			PAGED_CHECK(HasPen(page.strokes[0], *current) && HasPen(page.strokes[1], *current + 1) &&
				page.strokes[0].undoId == 2 && page.strokes[1].undoId == 2);
		}
	}

	void TestClearMemoryAndDiskIntervals(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		constexpr uint64_t boundary = 6;
		const auto pageGuid = Guid(1);
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 10), 1,
			PagedInkRequestKind::Clear, boundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded && clear->confirmedBoundary);
		PAGED_CHECK(clear->identity.kind == PagedInkRequestKind::Clear && clear->identity.boundaryGeneration == boundary &&
			*clear->confirmedBoundary == PagedInkBoundaryIdentity{ boundary, 1 });
		PAGED_REQUIRE(store.Load({ pageGuid, kSlot, 41 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loadedClear = store.Take();
		PAGED_REQUIRE(loadedClear && loadedClear->result == PagedInkResult::Succeeded && loadedClear->snapshot);
		CheckIntervals(test, *loadedClear->snapshot, 10, std::nullopt);

		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 10, 20.0f), 2,
			PagedInkRequestKind::Full, boundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto memory = store.Take();
		PAGED_REQUIRE(memory && memory->result == PagedInkResult::Succeeded && memory->confirmedBoundary);
		PAGED_CHECK(*memory->confirmedBoundary == PagedInkBoundaryIdentity{ boundary, 2 });
		PAGED_REQUIRE(store.Load({ pageGuid, kSlot, 42 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loadedMemory = store.Take();
		PAGED_REQUIRE(loadedMemory && loadedMemory->result == PagedInkResult::Succeeded && loadedMemory->snapshot);
		CheckIntervals(test, *loadedMemory->snapshot, 10, 20.0f);

		auto tail = Page(1, 0, 30);
		tail.strokes = { Pen(30, 8), Pen(31, 8) };
		tail.background = Draw3UInkBackground{ 0xabcdef, 1 };
		tail.viewport = { 7, -11, 1 };
		PAGED_REQUIRE(store.Save(SavePage(tail, 3, PagedInkRequestKind::Full, boundary,
			memory->confirmedBoundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto disk = store.Take();
		PAGED_REQUIRE(disk && disk->result == PagedInkResult::Succeeded && disk->confirmedBoundary);
		PAGED_CHECK(*disk->confirmedBoundary == PagedInkBoundaryIdentity{ boundary, 3 });
		PAGED_REQUIRE(store.Load({ pageGuid, kSlot, 43 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { pageGuid, kSlot, 44 }, *disk->confirmedBoundary }) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto loadedDisk = store.Take();
		const auto loadedPrevious = store.Take();
		PAGED_REQUIRE(loadedDisk && loadedDisk->result == PagedInkResult::Succeeded && loadedDisk->snapshot);
		CheckIntervals(test, *loadedDisk->snapshot, 10, 30.0f);
		PAGED_REQUIRE(loadedDisk->snapshot->background);
		PAGED_CHECK(loadedDisk->snapshot->background->rgb == 0xabcdef && loadedDisk->snapshot->background->pattern == 1 &&
			loadedDisk->snapshot->viewport.x == 7 && loadedDisk->snapshot->viewport.y == -11);
		PAGED_REQUIRE(loadedPrevious && loadedPrevious->result == PagedInkResult::Succeeded && loadedPrevious->snapshot);
		PAGED_CHECK(loadedPrevious->identity.kind == PagedInkRequestKind::LoadPreviousInterval &&
			loadedPrevious->identity.loadGeneration == 44 && loadedPrevious->identity.boundaryGeneration == boundary);
		const auto& previous = *loadedPrevious->snapshot;
		PAGED_REQUIRE(previous.strokes.size() == 2 && previous.operations.size() == 2);
		PAGED_CHECK(previous.intervalOrdinal == 0 && HasPen(previous.strokes[0], 10) && HasPen(previous.strokes[1], 11) &&
			previous.strokes[0].undoId == 0 && previous.strokes[1].undoId == 0 &&
			std::holds_alternative<Draw3UInkStrokeSnapshot>(previous.operations[0]) &&
			std::holds_alternative<Draw3UInkStrokeSnapshot>(previous.operations[1]));
		PAGED_CHECK(wake.count == 7 && !store.Take());
		PAGED_CHECK(log.Count(StoreLog::Level::Debug, "[UInkRead] scene=whiteboard op=splice_prefix page=1 result=complete") == 1);
		PAGED_CHECK(log.Count(StoreLog::Level::Warn, "[UInkRead] scene=whiteboard op=load_previous page=1 result=degraded") == 1 &&
			log.Count(StoreLog::Level::Warn, " split_undo=2 ") == 2);
	}


	void TestUInkLogThreshold(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded);

		StoreLog::SetThreshold(StoreLog::Threshold::Off);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 1 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto muted = store.Take();
		PAGED_REQUIRE(muted && muted->result == PagedInkResult::Succeeded && muted->snapshot);
		PAGED_CHECK(muted->ioMs > 0 && muted->readMs >= muted->ioMs && muted->workerMs >= muted->readMs);
		PAGED_CHECK(log.rows.empty());

		StoreLog::SetThreshold(StoreLog::Threshold::Debug);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 2 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto visible = store.Take();
		PAGED_REQUIRE(visible && visible->result == PagedInkResult::Succeeded && visible->snapshot);
		PAGED_CHECK(log.rows.size() == 1 &&
			log.Count(StoreLog::Level::Debug, "[UInkRead] scene=whiteboard op=load page=1 result=complete skippedBlocks=0") == 1 &&
			log.Count(StoreLog::Level::Debug, " strokes=1 ") == 1 &&
			log.Count(StoreLog::Level::Debug, " ioMs=") == 1 &&
			log.Count(StoreLog::Level::Debug, " decodeMs=") == 1 &&
			log.Count(StoreLog::Level::Debug, " importMs=") == 1);
	}

	void TestBoundaryMismatch(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 10, 20.0f), 3,
			PagedInkRequestKind::Full, 5)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto initial = store.Take();
		PAGED_REQUIRE(initial && initial->result == PagedInkResult::Succeeded && initial->confirmedBoundary);
		PAGED_CHECK(*initial->confirmedBoundary == PagedInkBoundaryIdentity{ 5, 3 });
		const auto tail = Page(1, 0, 90);
		constexpr std::array wrongBoundaries{ PagedInkBoundaryIdentity{ 4, 3 }, PagedInkBoundaryIdentity{ 5, 2 } };
		for (const auto& wrong : wrongBoundaries)
		{
			PAGED_REQUIRE(store.Save(SavePage(tail, 4, PagedInkRequestKind::Full, wrong.generation, wrong)) ==
				PagedInkAdmission::Accepted);
			store.Drain();
			const auto rejected = store.Take();
			PAGED_REQUIRE(rejected);
			PAGED_CHECK(rejected->result == PagedInkResult::Failed && rejected->error == PagedInkError::BoundaryMismatch &&
				rejected->identity.revision == 4 && !rejected->file.failed);
		}
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 53 }, { 5, 2 } }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 54 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto rejected = store.Take();
		const auto reloaded = store.Take();
		PAGED_REQUIRE(rejected && reloaded && reloaded->snapshot);
		PAGED_CHECK(rejected->result == PagedInkResult::Failed && rejected->error == PagedInkError::BoundaryMismatch &&
			!rejected->snapshot && rejected->identity.loadGeneration == 53);
		PAGED_CHECK(reloaded->result == PagedInkResult::Succeeded);
		CheckIntervals(test, *reloaded->snapshot, 10, 20.0f);
		PAGED_CHECK(wake.count == 5 && !store.Take());
	}

	void TestStaleBoundaryBeforeTake(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		constexpr uint64_t boundary = 5;
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 10), 1,
			PagedInkRequestKind::Clear, boundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded &&
			clear->confirmedBoundary == PagedInkBoundaryIdentity{ boundary, 1 });

		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 20), 2, PagedInkRequestKind::Full, boundary,
			clear->confirmedBoundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		// 保留修订 2 的回执在队列中，以调用方仍知道的修订 1 提交下一笔整页写入。
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 30), 3, PagedInkRequestKind::Full, boundary,
			clear->confirmedBoundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		const auto rejected = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded &&
			saved->confirmedBoundary == PagedInkBoundaryIdentity{ boundary, 2 });
		PAGED_REQUIRE(rejected && rejected->identity.revision == 3 &&
			rejected->result == PagedInkResult::Failed && rejected->error == PagedInkError::BoundaryMismatch);
		PAGED_CHECK(!rejected->confirmedBoundary && !rejected->file.failed &&
			!IsPageEvictable({ saved->identity.revision, rejected->identity.revision }, rejected->file));

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 1 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = store.Take();
		PAGED_REQUIRE(loaded && loaded->result == PagedInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1);
		PAGED_CHECK(loaded->confirmedBoundary == saved->confirmedBoundary &&
			HasPen(loaded->snapshot->strokes[0], 20) && !store.Take());
	}

	void TestQueueOrdering(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		WorkGate gate{ 100 };
		PagedInkStore store(Options(root, wake), { WorkGate::BeforeWork, &gate });
		ReleaseWork release{ gate };
		PAGED_REQUIRE(store.Save(SavePage(Page(4, 0, 4), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(5, 3, 5), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 2 && seeds[0].result == PagedInkResult::Succeeded &&
			seeds[1].result == PagedInkResult::Succeeded);
		PAGED_REQUIRE(store.Save(SavePage(Page(3, 1, 100), 100)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 2, 1), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 2, 2), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 2, 10), 3, PagedInkRequestKind::Clear, 11)) ==
			PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 2, 10, 20.0f), 4, PagedInkRequestKind::Full, 11)) ==
			PagedInkAdmission::Accepted);
		// 同一边界的 Clear 重试仍分隔两次 Full，不能只按边界代次判断。
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 2, 10), 5, PagedInkRequestKind::Clear, 11)) ==
			PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 2, 10, 30.0f), 6, PagedInkRequestKind::Full, 11)) ==
			PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 61 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(3), kSlot, 62 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(4), kSlot, 63 }) == PagedInkAdmission::Accepted);
		// 此加载先入队，轮到它执行前同页已有保存任务，不能读回旧版本。
		PAGED_REQUIRE(store.Load({ Guid(5), kSlot, 64 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(5, 3, 55), 2)) == PagedInkAdmission::Accepted);
		release.Release();
		store.Drain();
		const auto results = TakeAll(store);
		PAGED_REQUIRE(results.size() == 12);
		PAGED_CHECK(results[0].identity.pageGuid == Guid(1) && results[0].identity.revision == 1 &&
			results[0].result == PagedInkResult::Merged);
		PAGED_CHECK(std::string_view(PagedInkWriteDiagnosticStage(results[0])) == "merged" &&
			results[0].pageBytes == 0);
		for (size_t i : { 1u, 2u })
		{
			PAGED_CHECK(results[i].identity.loadGeneration == 60 + i && results[i].result == PagedInkResult::Failed &&
				results[i].error == PagedInkError::PendingWrite && !results[i].snapshot);
		}
		PAGED_CHECK(results[3].identity.revision == 100 && results[3].result == PagedInkResult::Succeeded);
		PAGED_CHECK(std::string_view(PagedInkWriteDiagnosticStage(results[3])) == "written" &&
			results[3].pageBytes > 0);
		PAGED_REQUIRE(results[4].identity.loadGeneration == 63 && results[4].result == PagedInkResult::Succeeded &&
			results[4].snapshot && results[4].snapshot->strokes.size() == 1);
		PAGED_CHECK(HasPen(results[4].snapshot->strokes[0], 4));
		PAGED_CHECK(results[5].identity.pageGuid == Guid(5) && results[5].identity.loadGeneration == 64 &&
			results[5].result == PagedInkResult::Failed && results[5].error == PagedInkError::PendingWrite &&
			!results[5].snapshot);
		for (size_t i = 6; i < 11; ++i)
		{
			const auto expectedKind = i == 7 || i == 9 ? PagedInkRequestKind::Clear : PagedInkRequestKind::Full;
			PAGED_CHECK(results[i].identity.pageGuid == Guid(1) && results[i].identity.revision == i - 4 &&
				results[i].identity.kind == expectedKind && results[i].result == PagedInkResult::Succeeded);
		}
		PAGED_CHECK(results.back().identity.pageGuid == Guid(5) && results.back().identity.revision == 2 &&
			results.back().result == PagedInkResult::Succeeded);
		PAGED_CHECK(wake.count == 14);
	}

	void TestCompletionAndCancellation(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		WorkGate gate{ 2, 1 };
		PagedInkStore store(Options(root, wake), { WorkGate::BeforeWork, &gate });
		ReleaseWork release{ gate };
		auto invalid = SavePage(Page(1, 0, 1), 1);
		invalid.identity.pageGuid = UInkGuid{};
		PAGED_CHECK(store.Save(std::move(invalid)) == PagedInkAdmission::InvalidRequest);
		PAGED_CHECK(store.Load({ {}, kSlot, 71 }) == PagedInkAdmission::InvalidRequest);
		PAGED_CHECK(!store.Take() && wake.count == 0);

		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto failed = store.Take();
		PAGED_REQUIRE(failed && wake.Wait());
		PAGED_CHECK(failed->result == PagedInkResult::Failed && failed->error == PagedInkError::Exception &&
			failed->identity.pageGuid == Guid(1) && failed->identity.revision == 1 && store.FilePath().empty());

		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 2), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 3), 3)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(3), kSlot, 72 }) == PagedInkAdmission::Accepted);
		std::thread cancel([&] { store.Close(PagedInkCloseMode::Cancel); });
		// 先观察排队任务已结算，再放行活动任务，避免以调度时序推断取消成功。
		const bool canceledWhileActive = wake.Wait();
		release.Release();
		cancel.join();
		PAGED_REQUIRE(canceledWhileActive);
		PAGED_CHECK(store.Save(SavePage(Page(4, 3, 4), 4)) == PagedInkAdmission::Closed);
		const auto results = TakeAll(store);
		PAGED_REQUIRE(results.size() == 3);
		PAGED_CHECK(std::count_if(results.begin(), results.end(), [](const auto& completion) {
			return completion.identity.revision == 3 && completion.identity.pageGuid == Guid(2) &&
				completion.result == PagedInkResult::Canceled;
		}) == 1);
		PAGED_CHECK(std::count_if(results.begin(), results.end(), [](const auto& completion) {
			return completion.identity.revision == 3 && completion.identity.pageGuid == Guid(2) &&
				std::string_view(PagedInkWriteDiagnosticStage(completion)) == "canceled" && completion.pageBytes == 0;
		}) == 1);
		PAGED_CHECK(std::count_if(results.begin(), results.end(), [](const auto& completion) {
			return completion.identity.loadGeneration == 72 && completion.identity.pageGuid == Guid(3) &&
				completion.result == PagedInkResult::Canceled;
		}) == 1);
		PAGED_CHECK(std::count_if(results.begin(), results.end(), [](const auto& completion) {
			return completion.identity.revision == 2 && completion.identity.pageGuid == Guid(1) &&
				completion.result == PagedInkResult::Succeeded;
		}) == 1);
		PAGED_CHECK(store.Load({ Guid(1), kSlot, 73 }) == PagedInkAdmission::Closed);
		PAGED_CHECK(wake.count == 4 && !store.Take());
	}

	void TestLoadWorkerException(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		constexpr uint64_t failedGeneration = 74;
		PagedInkStoreTestHook hook{
			[](void*, const PagedInkRequestIdentity& identity)
			{
				if (identity.loadGeneration != failedGeneration) return;
#if defined(DRAW3_TESTING)
				try
				{
					FailNextTestAllocation();
					::operator delete(::operator new(1));
				}
				catch (const std::bad_alloc&)
				{
					// 再次布置故障，确认 worker 发布已预留的失败回执时无需分配。
					FailNextTestAllocation();
					throw;
				}
#endif
				throw std::bad_alloc();
			}
		};
#if defined(DRAW3_TESTING)
		unsigned allocationFreeWakes = 0;
		const auto allocationFailures = TestAllocationFailureCount();
		hook.context = &allocationFreeWakes;
		hook.beforeWake = [](void* context) noexcept
		{
			if (CancelNextTestAllocation()) ++*static_cast<unsigned*>(context);
		};
#endif
		PagedInkStore store(Options(root, wake), hook);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, failedGeneration }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(2), kSlot + 1, failedGeneration }, { 11, 7 } }) ==
			PagedInkAdmission::Accepted);
		store.Close();
		const auto results = TakeAll(store);
		PAGED_REQUIRE(results.size() == 2 && wake.count == 2);
#if defined(DRAW3_TESTING)
		PAGED_CHECK(allocationFreeWakes == 2 && TestAllocationFailureCount() == allocationFailures + 2);
#endif
		for (const auto& receipt : results)
		{
			PAGED_CHECK(receipt.result == PagedInkResult::Failed && receipt.error == PagedInkError::Exception &&
				receipt.identity.loadGeneration == failedGeneration && !receipt.snapshot);
		}
		PAGED_CHECK(results[0].identity.kind == PagedInkRequestKind::Load &&
			results[0].identity.pageGuid == Guid(1) && results[0].identity.slotGeneration == kSlot &&
			!results[0].identity.boundaryGeneration);
		PAGED_CHECK(results[1].identity.kind == PagedInkRequestKind::LoadPreviousInterval &&
			results[1].identity.pageGuid == Guid(2) && results[1].identity.slotGeneration == kSlot + 1 &&
			results[1].identity.boundaryGeneration == 11 && results[1].identity.revision == 7);
	}

#if defined(DRAW3_TESTING)
	void TestCompletionReservationAllocationFailure(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		uint64_t accepted = 0;
		PagedInkStoreTestHook hook;
		hook.context = &accepted;
		hook.recordAccepted = [](void* context) noexcept { return ++*static_cast<uint64_t*>(context); };
		PagedInkStore store(PresentationOptions(root, wake), hook);
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 0, 10, 0, 101), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto first = store.Take();
		PAGED_REQUIRE(first && first->result == PagedInkResult::Succeeded && first->requestId == 1);
		const auto path = store.FilePath();
		const auto original = CorruptPageFile::ReadBytes(path);
		PAGED_REQUIRE(original && accepted == 1 && wake.count == 1);

		auto request = SavePage(PresentationPage(1, 0, 20, 0, 101), 2);
		const auto failures = TestAllocationFailureCount();
		FailNextTestAllocation();
		const auto save = store.Save(std::move(request));
		const bool saveAllocated = !CancelNextTestAllocation();
		FailNextTestAllocation();
		const auto load = store.LoadLane({ 0, kSlot, 76 });
		const bool loadAllocated = !CancelNextTestAllocation();
		store.Drain();
		// 预留失败不登记请求、不投递回执，也不能动已确认的页或文件。
		PAGED_CHECK(saveAllocated && loadAllocated && TestAllocationFailureCount() == failures + 2 &&
			save == PagedInkAdmission::ReservationFailed && load == PagedInkAdmission::ReservationFailed &&
			accepted == 1 && wake.count == 1 && !store.Take() && CorruptPageFile::ReadBytes(path) == original);

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 77 }) == PagedInkAdmission::Accepted);
		store.Drain();
		FailNextTestAllocation();
		const auto loaded = store.Take();
		const bool takeDidNotAllocate = CancelNextTestAllocation();
		PAGED_CHECK(takeDidNotAllocate && TestAllocationFailureCount() == failures + 2 &&
			accepted == 2 && wake.count == 2 && !store.Take());
		PAGED_REQUIRE(loaded && loaded->requestId == 2 && loaded->result == PagedInkResult::Succeeded &&
			loaded->confirmedRevision == 1 && loaded->identity.loadGeneration == 77 &&
			loaded->snapshot && loaded->snapshot->strokes.size() == 1);
		PAGED_CHECK(HasPen(loaded->snapshot->strokes.front(), 10) && CorruptPageFile::ReadBytes(path) == original);
	}
#endif

	void TestCloseWaitsForSubmitWake(TestState& test, const TempDirectory& root)
	{
		WakeGate gate;
		auto options = Options(root, gate.wake);
		options.wake = WakeGate::Wake;
		options.wakeContext = &gate;
		PagedInkStore store(std::move(options), { WakeGate::BeforeWork, &gate, WakeGate::BeforeWake });
		PagedInkAdmission submitted = PagedInkAdmission::InvalidRequest;
		std::binary_semaphore closeStarted{ 0 }, closeDone{ 0 };
		std::jthread submit, close;
		ReleaseWork releaseWork{ gate.work };
		ReleaseSignal releaseWake{ gate.sendWake }, finishWake{ gate.finishWake };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.work.entered.try_acquire_for(10s));
		submit = std::jthread([&] { submitted = store.Load({ Guid(1), kSlot, 1 }); });
		PAGED_REQUIRE(gate.beforeWake.try_acquire_for(10s));
		const auto pending = store.Take();
		PAGED_REQUIRE(pending && pending->error == PagedInkError::PendingWrite && gate.wake.count == 0);

		// 先让后台工作完成，Close 剩下的唯一阻塞点就是提交线程的唤醒。
		releaseWork.Release();
		store.Drain();
		PAGED_REQUIRE(gate.wake.count == 1);
		gate.pauseInWake = true;
		close = std::jthread([&] {
			closeStarted.release();
			store.Close(PagedInkCloseMode::Drain);
			closeDone.release();
		});
		PAGED_REQUIRE(closeStarted.try_acquire_for(10s));
		PAGED_CHECK(!closeDone.try_acquire_for(100ms));
		releaseWake.Release();
		PAGED_REQUIRE(gate.inWake.try_acquire_for(10s));
		PAGED_CHECK(!closeDone.try_acquire_for(100ms));
		finishWake.Release();
		close.join();
		submit.join();
		PAGED_CHECK(submitted == PagedInkAdmission::Accepted && gate.wake.count == 2);
	}

	void TestConcurrentCloseWaitsForCanceledWake(TestState& test, const TempDirectory& root)
	{
		WakeGate gate;
		PagedInkStore store(Options(root, gate.wake), { WakeGate::BeforeWork, &gate, WakeGate::BeforeWake });
		std::binary_semaphore drainStarted{ 0 }, drainDone{ 0 };
		std::jthread cancel, drain;
		ReleaseWork releaseWork{ gate.work };
		ReleaseSignal releaseWake{ gate.sendWake };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.work.entered.try_acquire_for(10s));
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 2), 2)) == PagedInkAdmission::Accepted);
		cancel = std::jthread([&] { store.Close(PagedInkCloseMode::Cancel); });
		PAGED_REQUIRE(gate.beforeWake.try_acquire_for(10s));
		const auto canceled = store.Take();
		PAGED_REQUIRE(canceled && canceled->result == PagedInkResult::Canceled &&
			canceled->identity.revision == 2 && gate.wake.count == 0);

		// Drain 关闭与 Cancel 关闭重叠，后台线程完成后仍须等 Cancel 的唤醒。
		drain = std::jthread([&] {
			drainStarted.release();
			store.Close(PagedInkCloseMode::Drain);
			drainDone.release();
		});
		PAGED_REQUIRE(drainStarted.try_acquire_for(10s));
		releaseWork.Release();
		store.Drain();
		PAGED_REQUIRE(gate.wake.count == 1);
		PAGED_CHECK(!drainDone.try_acquire_for(100ms));
		releaseWake.Release();
		drain.join();
		cancel.join();
		const auto active = store.Take();
		PAGED_REQUIRE(active && active->result == PagedInkResult::Succeeded && active->identity.revision == 1);
		PAGED_CHECK(gate.wake.count == 2 && !store.Take());
	}

	enum class SaveSnapshotOutcome { Success, FlushFailure, WorkerException };

	void TestSaveSnapshotLifetime(TestState& test, const TempDirectory& root, SaveSnapshotOutcome outcome)
	{
		SnapshotLifetimeProbe::Scope observation;
		WakeCounter wake;
		WorkGate gate{ 1 };
		const ScopedFaults fault({ .failFlush = outcome == SaveSnapshotOutcome::FlushFailure });
		PagedInkStoreTestHook hook{ WorkGate::BeforeWork, &gate };
		if (outcome == SaveSnapshotOutcome::WorkerException)
		{
			hook.beforeWork = [](void* context, const PagedInkRequestIdentity& identity)
			{
				WorkGate::BeforeWork(context, identity);
				throw std::runtime_error("paged ink snapshot worker exception");
			};
		}
		PagedInkStore store(Options(root, wake), hook);
		ReleaseWork release{ gate };

		auto request = SavePage(Page(1, 0, 1), 1);
		auto& page = request.snapshot;
		auto& points = page.strokes.front().points;
		points.resize(131072);
		for (std::size_t index = 0; index < points.size(); ++index)
			points[index] = { static_cast<float>(index % 1024), static_cast<float>(index / 1024), 3.0f };
		page.operations.emplace_back(page.strokes.front());
		const auto first = SnapshotLifetimeProbe::Count();
		SnapshotLifetimeProbe::WatchVector(page.strokes);
		SnapshotLifetimeProbe::WatchVector(page.operations);
		SnapshotLifetimeProbe::WatchVector(points);
		SnapshotLifetimeProbe::WatchVector(std::get<Draw3UInkStrokeSnapshot>(page.operations.front()).points);
		const SnapshotLifetimeProbe::Range watched{ first, SnapshotLifetimeProbe::Count() - first };
		PAGED_REQUIRE(SnapshotLifetimeProbe::Read(watched).bytes >= 3 * 1024 * 1024);

		PAGED_REQUIRE(store.Save(std::move(request)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		// 先确认 worker 持有的仍是登记的原始缓冲。
		PAGED_CHECK(SnapshotLifetimeProbe::Read(watched).freedBuffers == 0);
		release.Release();
		PAGED_REQUIRE(wake.Wait());
		store.Drain();
		// worker 已结束写入，Store 仍运行且回执尚未取走，此时原始缓冲应已真正释放。
		PAGED_CHECK(SnapshotLifetimeProbe::Released(watched));
		SnapshotLifetimeProbe::Report(test.name, watched);

		const auto completion = store.Take();
		PAGED_REQUIRE(completion && completion->identity.pageGuid == Guid(1) &&
			completion->identity.revision == 1 && completion->identity.slotGeneration == kSlot &&
			completion->identity.kind == PagedInkRequestKind::Full && !completion->snapshot);
		if (outcome == SaveSnapshotOutcome::FlushFailure)
		{
			PAGED_CHECK(completion->result == PagedInkResult::Failed &&
				completion->error == PagedInkError::FileCreateFailed && completion->systemError == ERROR_WRITE_FAULT);
		}
		else if (outcome == SaveSnapshotOutcome::WorkerException)
		{
			PAGED_CHECK(completion->result == PagedInkResult::Failed && completion->error == PagedInkError::Exception);
		}
		else
		{
			PAGED_CHECK(completion->result == PagedInkResult::Succeeded &&
				completion->error == PagedInkError::None && completion->confirmedRevision == 1);
		}
		PAGED_CHECK(wake.count == 1 && !store.Take());
	}

	SnapshotLifetimeProbe::Range ObservePageStrokes(const Draw3UInkCanvasSnapshot& page)
	{
		const auto first = SnapshotLifetimeProbe::Count();
		SnapshotLifetimeProbe::WatchVector(page.strokes);
		for (const auto& stroke : page.strokes) SnapshotLifetimeProbe::WatchVector(stroke.points);
		return { first, SnapshotLifetimeProbe::Count() - first };
	}

	void TestMergedSaveSnapshotLifetime(TestState& test, const TempDirectory& root)
	{
		SnapshotLifetimeProbe::Scope observation;
		WakeCounter wake;
		WorkGate gate{ 1 };
		PagedInkStore store(PresentationOptions(root, wake), { WorkGate::BeforeWork, &gate });
		ReleaseWork release{ gate };
		auto first = SavePage(PresentationPage(1, 0, 10, 0, 101), 1);
		const auto firstBuffers = ObservePageStrokes(first.snapshot);
		PAGED_REQUIRE(store.Save(std::move(first)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));

		auto pending = SavePage(PresentationPage(1, 0, 20, 0, 101), 2);
		const auto pendingBuffers = ObservePageStrokes(pending.snapshot);
		PAGED_REQUIRE(store.Save(std::move(pending)) == PagedInkAdmission::Accepted);
		auto latest = SavePage(PresentationPage(1, 0, 30, 0, 101), 3);
		const auto latestBuffers = ObservePageStrokes(latest.snapshot);
		PAGED_REQUIRE(store.Save(std::move(latest)) == PagedInkAdmission::Accepted);
		// 活动请求还在阻塞时，被合并的排队快照已经释放，但回执仍待取。
		PAGED_CHECK(SnapshotLifetimeProbe::Released(pendingBuffers));
		PAGED_CHECK(SnapshotLifetimeProbe::Read(firstBuffers).freedBuffers == 0 &&
			SnapshotLifetimeProbe::Read(latestBuffers).freedBuffers == 0);
		release.Release();
		store.Drain();
		PAGED_CHECK(SnapshotLifetimeProbe::Released(firstBuffers) && SnapshotLifetimeProbe::Released(latestBuffers));

		const auto saved = TakeAll(store);
		PAGED_REQUIRE(saved.size() == 3);
		PAGED_CHECK(saved[0].identity.revision == 2 && saved[0].result == PagedInkResult::Merged &&
			!saved[0].snapshot);
		PAGED_CHECK(saved[1].identity.revision == 1 && saved[1].result == PagedInkResult::Succeeded &&
			!saved[1].snapshot);
		PAGED_CHECK(saved[2].identity.revision == 3 && saved[2].result == PagedInkResult::Succeeded &&
			!saved[2].snapshot);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 75 }) == PagedInkAdmission::Accepted);
		store.Drain();
		auto loaded = store.Take();
		PAGED_REQUIRE(loaded && loaded->result == PagedInkResult::Succeeded && loaded->confirmedRevision == 3 &&
			loaded->snapshot && loaded->snapshot->strokes.size() == 1);
		const auto loadedBuffers = ObservePageStrokes(*loaded->snapshot);
		auto consumer = std::move(loaded->snapshot);
		loaded.reset();
		store.Close();
		// 回执移交后只有接收者拥有解码缓冲，关闭 Store 不应影响它。
		PAGED_CHECK(HasPen(consumer->strokes.front(), 30) &&
			SnapshotLifetimeProbe::Read(loadedBuffers).freedBuffers == 0);
		consumer.reset();
		PAGED_CHECK(SnapshotLifetimeProbe::Released(loadedBuffers) && wake.count == 4 && !store.Take());
	}

	void TestFirstCreateFlushFailureCanRetry(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		{
			const ScopedFaults fault({ .failFlush = true });
			PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
			store.Drain();
		}
		const auto failed = store.Take();
		PAGED_REQUIRE(failed && failed->result == PagedInkResult::Failed &&
			failed->error == PagedInkError::FileCreateFailed && failed->systemError == ERROR_WRITE_FAULT);
		PAGED_CHECK(!failed->file.failed && !failed->file.sealed && !failed->file.tailUnconfirmed);
		DirectoryContents beforeRetry;
		PAGED_REQUIRE(store.FilePath().empty() && ListDirectory(root.Path(), beforeRetry) && beforeRetry.files.empty());

		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 2), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && !saved->file.failed);
		const auto path = store.FilePath();
		PAGED_REQUIRE(!path.empty() && ReadUInkStoreFile(path).status == UInkReadStatus::Complete);
		DirectoryContents afterRetry;
		PAGED_REQUIRE(ListDirectory(root.Path(), afterRetry));
		PAGED_CHECK(afterRetry.files == std::vector{ path } && wake.count == 2 && !store.Take());
		PAGED_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Error,
			"[UInkWrite] scene=whiteboard op=create page=1 result=failed stage=create.flush systemError=29") == 1);
		PAGED_CHECK(log.Count(StoreLog::Level::Error, " sealed=0 tailUnconfirmed=0") == 1);
	}

	void TestPublishedReopenFailure(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		{
			const ScopedFaults fault({ .failReopen = true });
			PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
			store.Drain();
		}
		const auto receipt = store.Take();
		PAGED_REQUIRE(receipt && receipt->result == PagedInkResult::FileFailed);
		PAGED_CHECK(receipt->file.failed && receipt->file.sealed && !receipt->file.tailUnconfirmed && receipt->systemError != 0);
		const auto path = store.FilePath();
		PAGED_REQUIRE(!path.empty());
		PAGED_CHECK(ReadUInkStoreFile(path).status == UInkReadStatus::Complete);
		PAGED_CHECK(store.Save(SavePage(Page(2, 1, 2), 2)) == PagedInkAdmission::FileFailed);
		store.Drain();
		DirectoryContents contents;
		PAGED_REQUIRE(ListDirectory(root.Path(), contents));
		PAGED_CHECK(store.FilePath() == path && contents.files == std::vector{ path });
		PAGED_CHECK(store.FileState().failed && store.FileState().sealed && !store.FileState().tailUnconfirmed);
		PAGED_CHECK(wake.count == 1 && !store.Take());
		PAGED_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Error,
			"[UInkWrite] scene=whiteboard op=create page=1 result=failed stage=create.reopen systemError=32") == 1 &&
			log.Count(StoreLog::Level::Error, " sealed=1 tailUnconfirmed=0") == 1);
	}

	void CheckAppendRollbackFailure(TestState& test, const TempDirectory& root, bool failTruncate)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		WorkGate gate{ 2 };
		PagedInkStore store(Options(root, wake), { WorkGate::BeforeWork, &gate });
		ReleaseWork release{ gate };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 1), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto first = store.Take();
		PAGED_REQUIRE(first && first->result == PagedInkResult::Succeeded);
		const auto path = store.FilePath();
		PAGED_REQUIRE(!path.empty());
		PAGED_CHECK(IsPageEvictable({ 1, 1 }, first->file));
		{
			// 短写后才进入回滚，故 failFlush 只会由回滚刷盘消费。
			const ScopedFaults fault({ .shortWrite = true, .failFlush = !failTruncate, .failTruncate = failTruncate });
			PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 2), 2)) == PagedInkAdmission::Accepted);
			PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
			PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 3), 3)) == PagedInkAdmission::Accepted);
			release.Release();
			store.Drain();
		}
		const auto results = TakeAll(store);
		PAGED_REQUIRE(results.size() == 2);
		PAGED_CHECK(results[0].identity.revision == 2 && results[0].result == PagedInkResult::FileFailed &&
			results[0].file.failed && results[0].file.sealed && results[0].file.tailUnconfirmed && results[0].systemError != 0);
		PAGED_CHECK(results[1].identity.revision == 3 && results[1].result == PagedInkResult::FileFailed &&
			results[1].file.failed && results[1].file.sealed && results[1].file.tailUnconfirmed);
		PAGED_CHECK(std::string_view(PagedInkWriteDiagnosticStage(results[0])) == "written" &&
			results[0].pageBytes == 0);
		PAGED_CHECK(std::string_view(PagedInkWriteDiagnosticStage(results[1])) == "file_failed" &&
			results[1].pageBytes == 0);
		PAGED_CHECK(store.Save(SavePage(Page(3, 2, 4), 4)) == PagedInkAdmission::FileFailed);
		store.Drain();
		const auto state = store.FileState();
		PAGED_CHECK(state.failed && state.sealed && state.tailUnconfirmed && !IsPageEvictable({ 1, 1 }, state));
		DirectoryContents contents;
		PAGED_REQUIRE(ListDirectory(root.Path(), contents));
		PAGED_CHECK(store.FilePath() == path && contents.files == std::vector{ path });
		PAGED_CHECK(wake.count == 3 && !store.Take());

		PAGED_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Error,
			"[UInkWrite] scene=whiteboard op=append page=8 result=failed stage=append.page systemError=29") == 1);
		PAGED_CHECK(log.Count(StoreLog::Level::Error, failTruncate
			? " sealed=1 tailUnconfirmed=1 rollbackStage=append.truncate rollbackSystemError=29"
			: " sealed=1 tailUnconfirmed=1 rollbackStage=append.rollbackFlush rollbackSystemError=29") == 1);

		// 回滚未确认时仍从最后确认的目录读取，失败修订的元数据和排队的新页均不得出现。
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 79 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto lane = store.Take();
		PAGED_REQUIRE(lane && lane->result == PagedInkResult::Succeeded && lane->lanePages.size() == 1);
		const auto& confirmed = lane->lanePages.front();
		PAGED_REQUIRE(confirmed.result == PagedInkResult::Succeeded && confirmed.snapshot &&
			confirmed.snapshot->strokes.size() == 1);
		PAGED_CHECK(confirmed.metadata == PagedInkPageMetadata{ Guid(1), 0, 1 } &&
			confirmed.confirmedRevision == 1 && !confirmed.confirmedBoundary &&
			HasPen(confirmed.snapshot->strokes.front(), 1));
		PAGED_CHECK(lane->file.failed && lane->file.sealed && lane->file.tailUnconfirmed &&
			wake.count == 4 && !store.Take());
	}

	void TestTruncateFailure(TestState& test, const TempDirectory& root)
	{
		CheckAppendRollbackFailure(test, root, true);
	}

	void TestRollbackFlushFailure(TestState& test, const TempDirectory& root)
	{
		CheckAppendRollbackFailure(test, root, false);
	}

	void TestDegradedPage(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		const auto options = Options(root, wake);
		PagedInkStore store(options);
		// Draw3 可构建此直线，但它超出 UInk 冷读的坐标预算。
		const auto shape = LineShape(0xf2, static_cast<float>(kUInkMaxCoordinate) + 1024.0f);
		PAGED_REQUIRE(shape);
		auto page = Page(1, 0, 4);
		page.strokes = { ShapeStroke(*shape, 0), Pen(4, 1) };
		const auto exported = ExportDraw3PageToUInkV11(page, options.pageContext);
		PAGED_REQUIRE(exported.status == Draw3UInkExportStatus::Success && exported.canvas &&
			exported.canvas->content.size() == 1 && exported.counts.invalidShapeSkipped == 1);
		PAGED_REQUIRE(store.Save(SavePage(std::move(page), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded);
		PAGED_CHECK(saved->degraded && saved->fallbackCounts.invalidShapeSkipped == 1 && !saved->file.failed);
		PAGED_CHECK(!IsPageEvictable({ 1, 1, false, false, saved->degraded }, store.FileState()));
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 81 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = store.Take();
		PAGED_REQUIRE(loaded && loaded->result == PagedInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1);
		PAGED_CHECK(HasPen(loaded->snapshot->strokes.front(), 4));
		PAGED_CHECK(wake.count == 2 && !store.Take());
		PAGED_CHECK(log.Count(StoreLog::Level::Warn,
			"[UInkWrite] scene=whiteboard op=create page=1 result=degraded stage=committed") == 1 &&
			log.Count(StoreLog::Level::Warn, " l3.invalidShapeSkipped=1") == 1);
	}

	void TestStoredDegradedLaneLog(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		PagedInkStore store(PresentationOptions(root, wake));
		const auto shape = LineShape(0xf2, static_cast<float>(kUInkMaxCoordinate) + 1024.0f);
		PAGED_REQUIRE(shape);
		auto page = PresentationPage(1, 0, 4, 1);
		page.strokes = { ShapeStroke(*shape, 0), Pen(4, 1) };
		PAGED_REQUIRE(store.Save(SavePage(std::move(page), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->degraded);

		log.rows.clear();
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 81 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = store.Take();
		PAGED_REQUIRE(loaded && loaded->result == PagedInkResult::Succeeded && loaded->lanePages.size() == 1);
		const auto& read = loaded->lanePages.front();
		PAGED_REQUIRE(read.result == PagedInkResult::Succeeded && read.snapshot &&
			read.readSummary && read.readSummary->outcome == UInkReadOutcome::Complete);
		PAGED_CHECK(read.degraded && !read.readDegraded && read.fallbackCounts.invalidShapeSkipped == 0);
		PAGED_CHECK(!IsPageEvictable({ read.confirmedRevision, 1, false, false, read.degraded }, loaded->file));
		PAGED_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Warn,
			"[UInkRead] scene=ppt op=load_lane page=1 result=complete skippedBlocks=0") == 1 &&
			log.Count(StoreLog::Level::Warn, " storedDegraded=1 ") == 1);
	}

	void TestDegradedCurrentIntervalDoesNotAffectPrefix(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		constexpr uint64_t boundary = 6;
		const auto shape = LineShape(0xf2, static_cast<float>(kUInkMaxCoordinate) + 1024.0f);
		PAGED_REQUIRE(shape);
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 10), 1,
			PagedInkRequestKind::Clear, boundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded && clear->confirmedBoundary &&
			!clear->degraded && clear->fallbackCounts.invalidShapeSkipped == 0);

		auto tail = Page(1, 0, 20);
		tail.strokes.push_back(ShapeStroke(*shape, 1));
		PAGED_REQUIRE(store.Save(SavePage(tail, 2, PagedInkRequestKind::Full, boundary,
			clear->confirmedBoundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto degraded = store.Take();
		PAGED_REQUIRE(degraded && degraded->result == PagedInkResult::Succeeded && degraded->confirmedBoundary &&
			degraded->fallbackCounts.invalidShapeSkipped == 1 && degraded->degraded);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 1 }, *degraded->confirmedBoundary }) ==
			PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 2 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto previous = store.Take();
		const auto full = store.Take();
		PAGED_REQUIRE(previous && previous->result == PagedInkResult::Succeeded && previous->snapshot &&
			previous->snapshot->strokes.size() == 2);
		PAGED_CHECK(!previous->degraded && HasPen(previous->snapshot->strokes[0], 10));
		PAGED_REQUIRE(full && full->result == PagedInkResult::Succeeded);
		PAGED_CHECK(full->degraded);

		// 撤掉被跳过的图形，并用最新确认边界替换盘上的整个当前区间。
		tail.strokes.pop_back();
		PAGED_REQUIRE(store.Save(SavePage(tail, 3, PagedInkRequestKind::Full, boundary,
			degraded->confirmedBoundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded &&
			saved->confirmedBoundary == PagedInkBoundaryIdentity{ boundary, 3 });
		PAGED_CHECK(saved->fallbackCounts.invalidShapeSkipped == 0 && !saved->degraded &&
			IsPageEvictable({ 3, 3, false, false, saved->degraded }, saved->file));
	}

	void TestClearPrefixDegradationSurvivesFullSave(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(Options(root, wake));
		constexpr uint64_t boundary = 6;
		const auto shape = LineShape(0xf2, static_cast<float>(kUInkMaxCoordinate) + 1024.0f);
		PAGED_REQUIRE(shape);
		auto prefix = BoundaryPage(1, 0, 10);
		prefix.operations.insert(prefix.operations.begin(), ShapeStroke(*shape, 7));
		PAGED_REQUIRE(store.Save(SavePage(prefix, 1, PagedInkRequestKind::Clear, boundary)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded && clear->confirmedBoundary &&
			clear->degraded && clear->fallbackCounts.invalidShapeSkipped == 1);

		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 10, 20.0f), 2,
			PagedInkRequestKind::Full, boundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto full = store.Take();
		PAGED_REQUIRE(full && full->result == PagedInkResult::Succeeded && full->confirmedBoundary &&
			full->fallbackCounts.invalidShapeSkipped == 0);
		PAGED_CHECK(full->degraded);
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 30), 3, PagedInkRequestKind::Full, boundary,
			full->confirmedBoundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto spliced = store.Take();
		PAGED_REQUIRE(spliced && spliced->result == PagedInkResult::Succeeded && spliced->confirmedBoundary &&
			spliced->fallbackCounts.invalidShapeSkipped == 0);
		PAGED_CHECK(spliced->degraded);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 1 }, *spliced->confirmedBoundary }) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = store.Take();
		PAGED_REQUIRE(loaded && loaded->result == PagedInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 2);
		PAGED_CHECK(loaded->degraded && loaded->fallbackCounts.invalidShapeSkipped == 0 &&
			HasPen(loaded->snapshot->strokes[0], 10));
	}

	draw3::shapes::ShapeObject BudgetPolygon(uint8_t id)
	{
		using namespace draw3::shapes;
		ShapeObject shape;
		shape.id = Guid(id).Bytes();
		shape.kind = Kind::Polygon;
		shape.style.fill = true;
		// 330 个顶点的填充多边形各用掉超过一半的页级几何工作预算。
		constexpr unsigned vertices = 330;
		const double step = 2 * std::acos(-1.0) / vertices;
		for (unsigned i = 0; i < vertices; ++i)
		{
			const double angle = step * i;
			shape.geometry.vertices.push_back({ static_cast<float>(1000 * std::cos(angle)),
				static_cast<float>(1000 * std::sin(angle)) });
		}
		return shape;
	}

	void TestSpliceExceedsSharedGeometryBudget(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		const auto options = Options(root, wake);
		PagedInkStore store(options);
		constexpr uint64_t boundary = 6;
		auto prefix = BoundaryPage(1, 0, 10);
		prefix.operations = { ShapeStroke(BudgetPolygon(0xf2), 0), Draw3UInkClearSnapshot{ 1 } };
		auto tail = Page(1, 0, 20);
		tail.strokes = { ShapeStroke(BudgetPolygon(0xf3), 0) };
		const auto exportedPrefix = ExportDraw3PageToUInkV11(prefix, options.pageContext);
		const auto exportedTail = ExportDraw3PageToUInkV11(tail, options.pageContext);
		PAGED_REQUIRE(exportedPrefix.status == Draw3UInkExportStatus::Success && exportedPrefix.canvas &&
			exportedPrefix.canvas->content.size() == 2 && exportedPrefix.counts.invalidShapeSkipped == 0 &&
			exportedPrefix.counts.geometryLimit == 0);
		PAGED_REQUIRE(exportedTail.status == Draw3UInkExportStatus::Success && exportedTail.canvas &&
			exportedTail.canvas->content.size() == 1 && exportedTail.counts.invalidShapeSkipped == 0 &&
			exportedTail.counts.geometryLimit == 0);
		const auto coldTail = ImportUInkV11Page(*exportedTail.canvas);
		PAGED_REQUIRE(coldTail.status == Draw3UInkV11ImportStatus::Success && coldTail.page &&
			coldTail.page->strokes.size() == 1 && coldTail.counts.invalidShapeSkipped == 0 &&
			coldTail.counts.geometryLimit == 0);

		PAGED_REQUIRE(store.Save(SavePage(prefix, 1, PagedInkRequestKind::Clear, boundary)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded && clear->confirmedBoundary &&
			!clear->degraded && clear->fallbackCounts.invalidShapeSkipped == 0);
		PAGED_REQUIRE(store.Save(SavePage(tail, 2, PagedInkRequestKind::Full, boundary,
			clear->confirmedBoundary)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedBoundary &&
			saved->fallbackCounts.invalidShapeSkipped == 0 && saved->fallbackCounts.geometryLimit == 0);
		PAGED_CHECK(saved->degraded && !IsPageEvictable({ 2, 2, false, false, saved->degraded }, saved->file));
		PAGED_CHECK(log.Count(StoreLog::Level::Warn,
			"[UInkWrite] scene=whiteboard op=append page=1 result=degraded stage=committed") == 1 &&
			log.Count(StoreLog::Level::Warn, " l3.geometryLimit=") == 1);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 1 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 2 }, *saved->confirmedBoundary }) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto full = store.Take();
		const auto previous = store.Take();
		PAGED_REQUIRE(full && full->result == PagedInkResult::Succeeded && full->snapshot);
		PAGED_CHECK(full->degraded && full->fallbackCounts.geometryLimit > 0 &&
			full->fallbackCounts.invalidShapeSkipped == 1 && full->snapshot->strokes.empty());
		PAGED_REQUIRE(previous && previous->result == PagedInkResult::Succeeded && previous->snapshot);
		PAGED_CHECK(!previous->degraded && previous->fallbackCounts.geometryLimit == 0 &&
			previous->fallbackCounts.invalidShapeSkipped == 0 && previous->snapshot->strokes.size() == 1);
	}

	void TestFootprintBudgetKeepsReadableClearPrefix(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		auto options = Options(root, wake);
		options.importOptions.footprintBudget = 64;
		PagedInkStore store(std::move(options));
		auto page = BoundaryPage(1, 0, 32);
		for (auto& point : std::get<Draw3UInkStrokeSnapshot>(page.operations[1]).points) point.width = 10000;
		PAGED_REQUIRE(store.Save(SavePage(std::move(page), 1, PagedInkRequestKind::Clear, 6)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedBoundary);
		PAGED_CHECK(saved->degraded && saved->fallbackCounts.footprintBudget == 0);
		PAGED_CHECK(!IsPageEvictable({ 1, 1, false, false, saved->degraded }, saved->file));

		const auto raw = ReadUInkStoreFile(store.FilePath());
		PAGED_REQUIRE(raw.status == UInkReadStatus::Complete && raw.document && raw.document->canvases.size() == 1);
		PAGED_CHECK(raw.document->canvases.front().content.size() == 3);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 1 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 2 }, *saved->confirmedBoundary }) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto pageRead = store.Take();
		const auto prefixRead = store.Take();
		PAGED_REQUIRE(pageRead && pageRead->result == PagedInkResult::Succeeded && pageRead->snapshot);
		PAGED_REQUIRE(prefixRead && prefixRead->result == PagedInkResult::Succeeded && prefixRead->snapshot);
		PAGED_CHECK(pageRead->readDegraded && prefixRead->readDegraded &&
			pageRead->fallbackCounts.footprintBudget == 1 && prefixRead->fallbackCounts.footprintBudget == 1);
		const auto& operations = pageRead->snapshot->operations;
		PAGED_REQUIRE(operations.size() == 2 && std::holds_alternative<Draw3UInkStrokeSnapshot>(operations.front()));
		PAGED_CHECK(HasPen(std::get<Draw3UInkStrokeSnapshot>(operations.front()), 32) &&
			std::holds_alternative<Draw3UInkClearSnapshot>(operations.back()) &&
			pageRead->snapshot->intervalOrdinal == 1 && pageRead->snapshot->strokes.empty());
		PAGED_REQUIRE(prefixRead->snapshot->strokes.size() == 1);
		PAGED_CHECK(HasPen(prefixRead->snapshot->strokes.front(), 32));
		PAGED_CHECK(log.Count(StoreLog::Level::Warn, "[UInkRead] scene=whiteboard op=load") == 2 &&
			log.Count(StoreLog::Level::Warn, " l3.footprintBudget=1") == 3 && !store.Take());
	}

	void TestFailedColdReadPreflight(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		auto options = Options(root, wake);
		options.importOptions.footprintTileSize = 0;
		PagedInkStore store(std::move(options));
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 32), 1, PagedInkRequestKind::Clear, 6)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedRevision == 1 &&
			saved->confirmedBoundary == PagedInkBoundaryIdentity{ 6, 1 });
		PAGED_CHECK(saved->degraded && !saved->readDegraded && !saved->file.failed &&
			!IsPageEvictable({ 1, 1, false, false, saved->degraded }, saved->file));
		PAGED_CHECK(log.Count(StoreLog::Level::Warn, "stage=cold_preflight reason=invalid_page") == 1 &&
			log.Count(StoreLog::Level::Warn, "result=degraded stage=committed") == 1);

		const auto raw = ReadUInkStoreFile(store.FilePath());
		PAGED_REQUIRE(raw.status == UInkReadStatus::Complete && raw.document && raw.document->canvases.size() == 1);
		PAGED_CHECK(raw.document->canvases.front().content.size() == 3);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 1 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = store.Take();
		PAGED_CHECK(loaded && loaded->result == PagedInkResult::Failed &&
			loaded->error == PagedInkError::ImportFailed && !loaded->snapshot && !store.Take());
	}

	struct SaveEvictionGate
	{
		WorkGate work{ 2 };
		std::binary_semaphore blocked{ 0 };
		bool blockSignaled = false;

		static void BeforeWork(void* context, const PagedInkRequestIdentity& identity)
		{
			WorkGate::BeforeWork(&static_cast<SaveEvictionGate*>(context)->work, identity);
		}

		static void OnSaveBlockedByEviction(void* context) noexcept
		{
			auto& gate = *static_cast<SaveEvictionGate*>(context);
			if (!std::exchange(gate.blockSignaled, true)) gate.blocked.release();
		}
	};

	void TestAppendAndEvictionAreMutuallyExclusive(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		SaveEvictionGate gate;
		PagedInkStoreTestHook hook{ SaveEvictionGate::BeforeWork, &gate };
		hook.onSaveBlockedByEviction = SaveEvictionGate::OnSaveBlockedByEviction;
		PagedInkStore store(Options(root, wake), hook);
		ReleaseWork releaseSave{ gate.work };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 10), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(wake.Wait());
		const auto seed = store.Take();
		PAGED_REQUIRE(seed && seed->result == PagedInkResult::Succeeded && seed->confirmedRevision == 1);
		const PagedInkEvictionState resident{ seed->confirmedRevision, 1 };
		PAGED_CHECK(IsPageEvictable(resident, store.FileState()));

		PAGED_REQUIRE(store.TryBeginEviction());
		const auto endEviction = [](PagedInkStore* store) { store->EndEviction(); };
		std::unique_ptr<PagedInkStore, decltype(endEviction)> permit(&store, endEviction);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 20), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.blocked.try_acquire_for(10s));
		// 许可先到时，另一页的读请求仍能越过等待中的保存。
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 10 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(wake.Wait());
		const auto loaded = store.Take();
		PAGED_REQUIRE(loaded && loaded->identity.kind == PagedInkRequestKind::Load &&
			loaded->result == PagedInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1);
		PAGED_CHECK(HasPen(loaded->snapshot->strokes.front(), 10) && !store.Take());

		// 归还许可后让保存先进入工作阶段，再请求驱逐。
		permit.reset();
		PAGED_REQUIRE(gate.work.entered.try_acquire_for(10s));
		PAGED_CHECK(IsPageEvictable(resident, store.FileState()));
		const auto tryEviction = [&](bool requiresHealthyFile)
		{
			const bool allowed = store.TryBeginEviction(requiresHealthyFile);
			if (allowed) store.EndEviction();
			return allowed;
		};
		PAGED_CHECK(!tryEviction(true));
		PAGED_CHECK(!tryEviction(false));

		{
			const ScopedFaults fault({ .shortWrite = true, .failTruncate = true });
			releaseSave.Release();
			store.Drain();
		}
		const auto failed = store.Take();
		PAGED_REQUIRE(failed && failed->identity.kind == PagedInkRequestKind::Full && failed->saveDequeued &&
			failed->result == PagedInkResult::FileFailed && failed->file.sealed && failed->file.tailUnconfirmed);
		// 即使用先前取得的健康快照通过预检，释放驻留页所需的实时许可也会失败。
		PAGED_CHECK(IsPageEvictable(resident, seed->file) && !tryEviction(true));
		PAGED_CHECK(!IsPageEvictable(resident, store.FileState()) && !store.Take());
	}

	void TestEvictionTruthTable(TestState& test, const TempDirectory&)
	{
		const PagedInkEvictionState ready{ 14, 14 };
		const PagedInkFileState healthy{};
		PAGED_CHECK(IsPageEvictable(ready, healthy));
		PAGED_CHECK(!IsPageEvictable({ std::nullopt, 14 }, healthy));
		PAGED_CHECK(!IsPageEvictable({ 13, 14 }, healthy));
		PAGED_CHECK(!IsPageEvictable({ 15, 14 }, healthy));
		PAGED_CHECK(!IsPageEvictable({ 14, 14, true, false, false }, healthy));
		PAGED_CHECK(!IsPageEvictable({ 14, 14, false, true, false }, healthy));
		PAGED_CHECK(!IsPageEvictable({ 14, 14, false, false, true }, healthy));
		PAGED_CHECK(!IsPageEvictable(ready, { .failed = true }));
		PAGED_CHECK(!IsPageEvictable(ready, { .sealed = true }));
		PAGED_CHECK(!IsPageEvictable(ready, { .tailUnconfirmed = true }));
	}

	const PagedInkLanePage* FindLanePage(const PagedInkCompletion& lane, const UInkGuid& pageGuid)
	{
		const auto page = std::find_if(lane.lanePages.begin(), lane.lanePages.end(),
			[&](const auto& entry) { return entry.metadata.pageGuid == pageGuid; });
		return page == lane.lanePages.end() ? nullptr : &*page;
	}

	void CheckLanePage(TestState& test, const PagedInkCompletion& lane, const PagedInkPageMetadata& metadata,
		uint64_t revision, float x)
	{
		const auto* page = FindLanePage(lane, metadata.pageGuid);
		PAGED_REQUIRE(page && page->result == PagedInkResult::Succeeded &&
			page->snapshot && page->snapshot->strokes.size() == 1);
		PAGED_CHECK(page->metadata == metadata && page->confirmedRevision == revision &&
			!page->confirmedBoundary && page->error == PagedInkError::None && !page->readDegraded &&
			HasPen(page->snapshot->strokes.front(), x));
		PAGED_CHECK(page->snapshot->pageGuid == metadata.pageGuid && page->snapshot->pageIndex == metadata.pageIndex &&
			page->snapshot->pageNumber == metadata.pageNumber && page->snapshot->slideId == metadata.slideId &&
			page->snapshot->binding.value_or(0) == metadata.binding &&
			page->snapshot->retained == (metadata.pageState == 1) && page->snapshot->endScreen == (metadata.pageKind == 1));
	}

	void TestLoadLaneConfirmedMetadata(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		auto options = PresentationOptions(root, wake);
		const auto hostId = options.hostId;
		PagedInkStore store(std::move(options));
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 80 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto empty = TakeAll(store);
		PAGED_REQUIRE(empty.size() == 1 && empty.front().result == PagedInkResult::Succeeded);
		PAGED_CHECK(empty.front().identity.kind == PagedInkRequestKind::LoadLane &&
			empty.front().identity.slotGeneration == kSlot && empty.front().identity.loadGeneration == 80 &&
			empty.front().identity.laneBinding == 0 && empty.front().lanePages.empty() && store.FilePath().empty());
		PAGED_CHECK(log.rows.empty());

		auto retained = PresentationPage(2, 3, 20, 0, 22);
		retained.retained = true;
		auto stableEnd = PresentationPage(3, 2, 30, 0);
		stableEnd.endScreen = true;
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 0, 10, 0, 21), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(retained, 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(stableEnd, 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto stableSaves = TakeAll(store);
		PAGED_REQUIRE(stableSaves.size() == 3 && std::all_of(stableSaves.begin(), stableSaves.end(),
			[](const auto& receipt) { return receipt.result == PagedInkResult::Succeeded; }));
		const auto path = store.FilePath();
		PAGED_REQUIRE(!path.empty());
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 81 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto otherEmpty = TakeAll(store);
		PAGED_REQUIRE(otherEmpty.size() == 1 && otherEmpty.front().result == PagedInkResult::Succeeded);
		PAGED_CHECK(otherEmpty.front().identity.laneBinding == 1 && otherEmpty.front().lanePages.empty());
		PAGED_CHECK(log.rows.empty());

		auto orderRetained = PresentationPage(5, 3, 50, 1);
		orderRetained.retained = true;
		auto orderEnd = PresentationPage(6, 2, 60, 1);
		orderEnd.endScreen = true;
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(4, 0, 40, 1), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(orderRetained, 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(orderEnd, 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto orderSaves = TakeAll(store);
		PAGED_REQUIRE(orderSaves.size() == 3 && std::all_of(orderSaves.begin(), orderSaves.end(),
			[](const auto& receipt) { return receipt.result == PagedInkResult::Succeeded; }));
		DirectoryContents files;
		PAGED_REQUIRE(ListDirectory(root.Path(), files) && store.FilePath() == path && files.files == std::vector{ path });
		const auto wire = ReadUInkStoreFile(path);
		PAGED_REQUIRE(wire.document && wire.document->headerExtension &&
			wire.document->headerExtension->workspaces.size() == 1 && wire.document->canvases.size() == 6);
		PAGED_CHECK(wire.document->headerExtension->workspaces.front().workspaceType == 2 &&
			wire.document->headerExtension->workspaces.front().hostId == hostId);

		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 82 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 83 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = TakeAll(store);
		PAGED_REQUIRE(loaded.size() == 2 && std::all_of(loaded.begin(), loaded.end(), [](const auto& receipt)
			{ return receipt.result == PagedInkResult::Succeeded && receipt.lanePages.size() == 3; }));
		PAGED_CHECK(loaded[0].identity.laneBinding == 0 && loaded[0].identity.loadGeneration == 82 &&
			loaded[1].identity.laneBinding == 1 && loaded[1].identity.loadGeneration == 83);
		CheckLanePage(test, loaded[0], { Guid(1), 0, 1, 21, 0 }, 1, 10);
		CheckLanePage(test, loaded[0], { Guid(2), 3, 4, 22, 0, 1 }, 1, 20);
		CheckLanePage(test, loaded[0], { Guid(3), 2, 3, std::nullopt, 0, std::nullopt, 1 }, 1, 30);
		CheckLanePage(test, loaded[1], { Guid(4), 0, 1, std::nullopt, 1 }, 1, 40);
		CheckLanePage(test, loaded[1], { Guid(5), 3, 4, std::nullopt, 1, 1 }, 1, 50);
		CheckLanePage(test, loaded[1], { Guid(6), 2, 3, std::nullopt, 1, std::nullopt, 1 }, 1, 60);

		PAGED_CHECK(log.rows.size() == 6);
		for (const auto page : { 1, 3, 4 })
			PAGED_CHECK(log.Count(StoreLog::Level::Debug, "[UInkRead] scene=ppt op=load_lane page=" +
				std::to_string(page) + " result=complete") == 2);

		struct Replacement
		{
			uint64_t revision;
			uint32_t index;
			bool retained;
			float x;
		};
		constexpr Replacement replacements[] = {
			{ 2, 1, false, 12 }, { 3, 4, true, 13 }, { 4, 0, false, 14 },
		};
		for (const auto& replacement : replacements)
		{
			auto page = PresentationPage(1, replacement.index, replacement.x, 0, 21);
			page.retained = replacement.retained;
			PAGED_REQUIRE(store.Save(SavePage(page, replacement.revision)) == PagedInkAdmission::Accepted);
			store.Drain();
			const auto saved = TakeAll(store);
			PAGED_REQUIRE(saved.size() == 1 && saved.front().result == PagedInkResult::Succeeded &&
				saved.front().confirmedRevision == replacement.revision);
			PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 83 + replacement.revision }) == PagedInkAdmission::Accepted);
			store.Drain();
			const auto lane = TakeAll(store);
			PAGED_REQUIRE(lane.size() == 1 && lane.front().result == PagedInkResult::Succeeded &&
				lane.front().lanePages.size() == 3);
			CheckLanePage(test, lane.front(), { Guid(1), page.pageIndex, page.pageNumber, 21, 0,
				page.retained ? std::optional<int32_t>{ 1 } : std::nullopt }, replacement.revision, replacement.x);
		}
		{
			ScopedFaults faults({ .failFlush = true });
			PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 1, 15, 0, 21), 5)) == PagedInkAdmission::Accepted);
			store.Drain();
			const auto failed = TakeAll(store);
			PAGED_REQUIRE(failed.size() == 1 && failed.front().result == PagedInkResult::Failed &&
				failed.front().error == PagedInkError::FileWriteFailed && !failed.front().file.failed);
		}
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 88 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto unchanged = TakeAll(store);
		PAGED_REQUIRE(unchanged.size() == 1 && unchanged.front().result == PagedInkResult::Succeeded &&
			unchanged.front().lanePages.size() == 3);
		CheckLanePage(test, unchanged.front(), { Guid(1), 0, 1, 21, 0 }, 4, 14);
		PAGED_CHECK(wake.count == 18);
	}

	void TestFallbackPageOverwrite(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(PresentationOptions(root, wake));
		auto page = PresentationPage(4, 1, 40, 1);
		PAGED_REQUIRE(store.Save(SavePage(page, 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto first = store.Take();
		PAGED_REQUIRE(first && first->result == PagedInkResult::Succeeded && first->confirmedRevision == 1);
		const auto path = store.FilePath();

		// 页序绑定的同一 GUID 可以明确保存为空页，不能回显旧版本的墨迹。
		page.strokes.clear();
		PAGED_REQUIRE(store.Save(SavePage(page, 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto replaced = store.Take();
		PAGED_REQUIRE(replaced && replaced->result == PagedInkResult::Succeeded && replaced->confirmedRevision == 2);
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 89 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto lane = store.Take();
		PAGED_REQUIRE(lane && lane->result == PagedInkResult::Succeeded && lane->lanePages.size() == 1);
		const auto& loaded = lane->lanePages.front();
		PAGED_CHECK(loaded.metadata == PagedInkPageMetadata{ Guid(4), 1, 2, std::nullopt, 1 } &&
			loaded.result == PagedInkResult::Succeeded && loaded.confirmedRevision == 2 &&
			loaded.snapshot && loaded.snapshot->strokes.empty() && loaded.snapshot->operations.empty());
		PAGED_CHECK(!path.empty() && store.FilePath() == path && wake.count == 3 && !store.Take());
	}

	void TestPresentationEndScreenClearRoundTrip(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(PresentationOptions(root, wake));
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 0, 10, 0, 101), 1)) == PagedInkAdmission::Accepted);
		auto prior = BoundaryPage(2, 1, 20);
		prior.endScreen = true;
		PAGED_REQUIRE(store.Save(SavePage(std::move(prior), 1, PagedInkRequestKind::Clear, 11)) ==
			PagedInkAdmission::Accepted);
		auto latest = BoundaryPage(2, 1, 30);
		latest.endScreen = true;
		PAGED_REQUIRE(store.Save(SavePage(std::move(latest), 2, PagedInkRequestKind::Clear, 12)) ==
			PagedInkAdmission::Accepted);
		auto full = BoundaryPage(2, 1, 30, 40.0f);
		full.endScreen = true;
		std::get<Draw3UInkStrokeSnapshot>(full.operations.front()).style.fallbackRgb = 0xabcdef;
		full.strokes.front().style.fallbackRgb = 0x987654;
		std::get<Draw3UInkStrokeSnapshot>(full.operations[3]).style.fallbackRgb = 0x987654;
		PAGED_REQUIRE(store.Save(SavePage(std::move(full), 3, PagedInkRequestKind::Full, 12)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = TakeAll(store);
		PAGED_REQUIRE(saved.size() == 4 && std::all_of(saved.begin(), saved.end(),
			[](const auto& receipt) { return receipt.result == PagedInkResult::Succeeded; }));
		PAGED_CHECK(saved[1].confirmedBoundary == PagedInkBoundaryIdentity{ 11, 1 } &&
			saved[2].confirmedBoundary == PagedInkBoundaryIdentity{ 12, 2 });
		PAGED_REQUIRE(saved[3].confirmedBoundary == PagedInkBoundaryIdentity{ 12, 3 });

		const auto path = store.FilePath();
		DirectoryContents files;
		PAGED_REQUIRE(!path.empty() && ListDirectory(root.Path(), files) && files.files == std::vector{ path });
		const auto wire = ReadUInkStoreFile(path);
		PAGED_REQUIRE(wire.status == UInkReadStatus::Complete && wire.document && wire.document->canvases.size() == 2);
		const auto& canvases = wire.document->canvases;
		const auto endWire = std::find_if(canvases.begin(), canvases.end(),
			[](const auto& page) { return page.pageGuid == Guid(2); });
		PAGED_REQUIRE(endWire != canvases.end());
		PAGED_CHECK(endWire->pageKind == 1 && !endWire->slideId &&
			std::count_if(endWire->content.begin(), endWire->content.end(), [](const auto& content)
				{ return std::holds_alternative<UInkClear>(content); }) == 1);

		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 90 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(2), kSlot, 91 }, *saved[3].confirmedBoundary }) ==
			PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(2), kSlot, 92 }, { 11, 1 } }) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto reads = TakeAll(store);
		PAGED_REQUIRE(reads.size() == 3 && reads[0].result == PagedInkResult::Succeeded &&
			reads[0].lanePages.size() == 2);
		CheckLanePage(test, reads[0], { Guid(1), 0, 1, 101, 0 }, 1, 10);
		const auto* end = FindLanePage(reads[0], Guid(2));
		PAGED_REQUIRE(end && end->result == PagedInkResult::Succeeded && end->confirmedRevision == 3 &&
			end->snapshot && end->snapshot->strokes.size() == 2);
		PAGED_CHECK(end->metadata == PagedInkPageMetadata{ Guid(2), 1, 2, std::nullopt, 0, std::nullopt, 1 } &&
			end->snapshot->endScreen && end->snapshot->strokes.front().style.fallbackRgb == 0x987654);
		CheckIntervals(test, *end->snapshot, 30, 40.0f);
		// 只恢复最近一次 Clear 的前缀，旧边界不能作为另一个可加载区间。
		PAGED_REQUIRE(reads[1].result == PagedInkResult::Succeeded && reads[1].snapshot &&
			reads[1].snapshot->strokes.size() == 2);
		PAGED_CHECK(HasPen(reads[1].snapshot->strokes.front(), 30) &&
			reads[1].snapshot->strokes.front().style.fallbackRgb == 0xabcdef);
		PAGED_CHECK(reads[2].result == PagedInkResult::Failed && reads[2].error == PagedInkError::BoundaryMismatch &&
			!reads[2].snapshot && wake.count == 7);
	}

	void TestLoadLanePendingWrite(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		WorkGate gate{ 100 };
		PagedInkStore store(PresentationOptions(root, wake), { WorkGate::BeforeWork, &gate });
		ReleaseWork release{ gate };
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 0, 10, 0, 21), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeded = TakeAll(store);
		PAGED_REQUIRE(seeded.size() == 1 && seeded.front().result == PagedInkResult::Succeeded);
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(3, 0, 30, 1), 100)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 91 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 92 }) == PagedInkAdmission::Accepted);
		const auto active = TakeAll(store);
		PAGED_REQUIRE(active.size() == 1);
		PAGED_CHECK(active.front().identity.loadGeneration == 91 && active.front().result == PagedInkResult::Failed &&
			active.front().error == PagedInkError::PendingWrite && active.front().lanePages.empty());
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(2, 1, 20, 0, 22), 1)) == PagedInkAdmission::Accepted);
		// 不同 lane 的相同页序各有自己的 GUID，两个排队保存均不得被合并。
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(6, 1, 60, 1), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 93 }) == PagedInkAdmission::Accepted);
		const auto queued = TakeAll(store);
		PAGED_REQUIRE(queued.size() == 1);
		PAGED_CHECK(queued.front().identity.loadGeneration == 93 && queued.front().result == PagedInkResult::Failed &&
			queued.front().error == PagedInkError::PendingWrite && queued.front().lanePages.empty());
		release.Release();
		store.Drain();
		const auto after = TakeAll(store);
		PAGED_REQUIRE(after.size() == 4 && after[0].result == PagedInkResult::Succeeded &&
			after[2].result == PagedInkResult::Succeeded && after[3].result == PagedInkResult::Succeeded);
		PAGED_CHECK(after[0].identity.pageGuid == Guid(3) && after[2].identity.pageGuid == Guid(2) &&
			after[3].identity.pageGuid == Guid(6));
		PAGED_CHECK(after[1].identity.loadGeneration == 92 && after[1].result == PagedInkResult::Failed &&
			after[1].error == PagedInkError::PendingWrite && after[1].lanePages.empty());

		gate.pauseRevision = 101;
		ReleaseWork nextRelease{ gate };
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(3, 0, 31, 1), 101)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(4, 2, 40, 1), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 94 }) == PagedInkAdmission::Accepted);
		nextRelease.Release();
		store.Drain();
		const auto independent = TakeAll(store);
		PAGED_REQUIRE(independent.size() == 3 && std::all_of(independent.begin(), independent.end(),
			[](const auto& receipt) { return receipt.result == PagedInkResult::Succeeded; }));
		PAGED_REQUIRE(independent[0].identity.pageGuid == Guid(3) && independent[1].identity.loadGeneration == 94 &&
			independent[1].lanePages.size() == 2 && independent[2].identity.pageGuid == Guid(4));
		CheckLanePage(test, independent[1], { Guid(1), 0, 1, 21, 0 }, 1, 10);
		CheckLanePage(test, independent[1], { Guid(2), 1, 2, 22, 0 }, 1, 20);
		PAGED_CHECK(wake.count == 10);
	}

	void TestLoadLaneCompletionAndCancellation(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		WorkGate gate{ 100 };
		gate.throwLaneGeneration = 101;
		PagedInkStore store(PresentationOptions(root, wake), { WorkGate::BeforeWork, &gate });
		ReleaseWork release{ gate };
		PAGED_CHECK(store.LoadLane({ 2, kSlot, 100 }) == PagedInkAdmission::InvalidRequest);
		PAGED_CHECK(!store.Take() && wake.count == 0);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 101 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto failed = TakeAll(store);
		PAGED_REQUIRE(failed.size() == 1 && wake.Wait());
		PAGED_CHECK(failed.front().identity.kind == PagedInkRequestKind::LoadLane && failed.front().identity.laneBinding == 0 &&
			failed.front().identity.slotGeneration == kSlot && failed.front().identity.loadGeneration == 101 &&
			failed.front().result == PagedInkResult::Failed && failed.front().error == PagedInkError::Exception &&
			failed.front().lanePages.empty() && !store.Take() && wake.count == 1);
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 0, 10, 0, 21), 100)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot + 1, 102 }) == PagedInkAdmission::Accepted);
		std::thread cancel([&] { store.Close(PagedInkCloseMode::Cancel); });
		const bool canceledWhileActive = wake.Wait();
		release.Release();
		cancel.join();
		PAGED_REQUIRE(canceledWhileActive);
		const auto results = TakeAll(store);
		PAGED_REQUIRE(results.size() == 2);
		PAGED_CHECK(results[0].identity.kind == PagedInkRequestKind::LoadLane && results[0].identity.laneBinding == 1 &&
			results[0].identity.slotGeneration == kSlot + 1 && results[0].identity.loadGeneration == 102 &&
			results[0].result == PagedInkResult::Canceled && results[0].lanePages.empty());
		PAGED_CHECK(results[1].identity.pageGuid == Guid(1) && results[1].result == PagedInkResult::Succeeded);
		PAGED_CHECK(store.LoadLane({ 1, kSlot, 103 }) == PagedInkAdmission::Closed && !store.Take() && wake.count == 3);
	}

	void TestReadDegradedPageAndLane(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		CorruptPageFile file{ { { "inkType", 2, 0, "X" } } };
		PagedInkStore store(PresentationOptions(root, wake), { nullptr, &file, nullptr, CorruptPageFile::BeforeRead });
		auto page = PresentationPage(1, 0, 40, 0, 21);
		page.intervalOrdinal = 1;
		page.operations = { Pen(10, 0), Pen(20, 1), Pen(30, 2), Draw3UInkClearSnapshot{ 3 }, Pen(40, 4) };
		PAGED_REQUIRE(store.Save(SavePage(page, 1, PagedInkRequestKind::Full, 13)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(2, 1, 50, 0, 22), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = TakeAll(store);
		const PagedInkBoundaryIdentity boundary{ 13, 1 };
		PAGED_REQUIRE(saved.size() == 2 && saved[0].result == PagedInkResult::Succeeded &&
			saved[0].confirmedBoundary == boundary && saved[1].result == PagedInkResult::Succeeded);
		const auto path = store.FilePath();
		PAGED_REQUIRE(!path.empty());
		const auto before = ReadUInkStoreFile(path);
		PAGED_REQUIRE(before.summary.outcome == UInkReadOutcome::Complete && before.document &&
			before.document->canvases.size() == 2 && before.document->canvases[0].pageGuid == Guid(1) &&
			before.document->canvases[0].content.size() == 5);

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 111 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 112 }, boundary }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 113 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto reads = TakeAll(store);
		PAGED_REQUIRE(file.patched && reads.size() == 3 && reads[0].identity.loadGeneration == 111 &&
			reads[1].identity.loadGeneration == 112 && reads[2].identity.loadGeneration == 113 &&
			reads[2].result == PagedInkResult::Succeeded && reads[2].lanePages.size() == 2);
		const auto* lanePage = FindLanePage(reads[2], Guid(1));
		PAGED_REQUIRE(lanePage && lanePage->metadata == PagedInkPageMetadata{ Guid(1), 0, 1, 21, 0 });
		const PagedInkPageResult* wholePageReads[] = { &reads[0], lanePage };
		for (const auto* read : wholePageReads)
		{
			PAGED_REQUIRE(read->result == PagedInkResult::Succeeded && read->readDegraded && read->readSummary &&
				read->snapshot && read->snapshot->operations.size() == 4 && read->snapshot->strokes.size() == 1);
			PAGED_CHECK(read->prefixReadDegraded && !read->tailReadDegraded);
			PAGED_CHECK(read->confirmedRevision == 1 && read->confirmedBoundary == boundary &&
				read->readSummary->outcome == UInkReadOutcome::Partial &&
				read->readSummary->diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)].count == 1);
			const auto* first = std::get_if<Draw3UInkStrokeSnapshot>(&read->snapshot->operations[0]);
			const auto* last = std::get_if<Draw3UInkStrokeSnapshot>(&read->snapshot->operations[1]);
			const auto* current = std::get_if<Draw3UInkStrokeSnapshot>(&read->snapshot->operations[3]);
			PAGED_REQUIRE(first && last && std::holds_alternative<Draw3UInkClearSnapshot>(read->snapshot->operations[2]) &&
				current);
			PAGED_CHECK(HasPen(*first, 10) && HasPen(*last, 30) && HasPen(*current, 40) &&
				HasPen(read->snapshot->strokes.front(), 40) && read->snapshot->intervalOrdinal == 1);
		}
		const auto& previous = reads[1];
		PAGED_REQUIRE(previous.result == PagedInkResult::Succeeded && previous.readDegraded && previous.readSummary &&
			previous.snapshot && previous.snapshot->strokes.size() == 2 && previous.snapshot->operations.size() == 2);
		PAGED_CHECK(previous.prefixReadDegraded && !previous.tailReadDegraded);
		PAGED_CHECK(previous.readSummary->outcome == UInkReadOutcome::Partial &&
			previous.readSummary->diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)].count == 1 &&
			previous.confirmedBoundary == boundary && previous.confirmedRevision == 1 &&
			previous.snapshot->intervalOrdinal == 0 && HasPen(previous.snapshot->strokes[0], 10) &&
			HasPen(previous.snapshot->strokes[1], 30));
		CheckLanePage(test, reads[2], { Guid(2), 1, 2, 22, 0 }, 1, 50);
		PAGED_CHECK(wake.count == 5);
		PAGED_CHECK(log.rows.size() == 4 && log.Count(StoreLog::Level::Warn, " l1.KnownBlockSkipped=1@") == 3);
		for (const std::string_view operation : { "load", "load_previous", "load_lane" })
			PAGED_CHECK(log.Count(StoreLog::Level::Warn, "[UInkRead] scene=ppt op=" + std::string(operation) +
				" page=1 result=degraded skippedBlocks=1") == 1);
		PAGED_CHECK(log.Count(StoreLog::Level::Debug, "[UInkRead] scene=ppt op=load_lane page=2 result=complete") == 1);
	}


	void TestReadLogWithClippedDiagnostics(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		constexpr auto detailLimit = UInkReadLimits{}.maxDiagnostics;
		constexpr auto skipped = detailLimit + 1;
		bool patched = false;
		PagedInkStoreTestHook hook;
		hook.context = &patched;
		hook.beforeReadAfterClosingWriter = [](void* context, const std::wstring& path)
		{
			const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
				0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE) return;
			const std::unique_ptr<void, decltype(&CloseHandle)> file(handle, CloseHandle);
			auto bytes = CorruptPageFile::ReadBytes(handle);
			if (!bytes) return;
			constexpr std::string_view key = "inkType";
			auto offset = bytes->find(key);
			for (uint32_t i = 0; i < skipped; ++i)
			{
				if (offset == std::string::npos) return;
				offset = bytes->find(key, offset + key.size());
				if (offset == std::string::npos) return;
				// 保留首末笔和对象边界，仅使中间的必填键失效。
				(*bytes)[offset] = 'X';
			}
			if (bytes->find(key, offset + key.size()) == std::string::npos) return;
			*static_cast<bool*>(context) = CorruptPageFile::WriteBytes(handle, *bytes);
		};
		PagedInkStore store(Options(root, wake), hook);
		auto page = Page(1, 0, 1);
		for (uint32_t i = 1; i <= skipped + 1; ++i) page.strokes.push_back(Pen(1, i));
		PAGED_REQUIRE(store.Save(SavePage(std::move(page), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && !saved->degraded);

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 1 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto recovered = store.Take();
		PAGED_REQUIRE(patched && recovered && recovered->result == PagedInkResult::Succeeded &&
			recovered->snapshot && recovered->snapshot->strokes.size() == 2 && recovered->readSummary);
		PAGED_CHECK(recovered->readDiagnostics.size() == detailLimit &&
			recovered->readSummary->diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)].count == skipped);
		PAGED_CHECK(log.rows.size() == 1 &&
			log.Count(StoreLog::Level::Warn, " skippedBlocks=" + std::to_string(skipped) + " ") == 1 &&
			log.Count(StoreLog::Level::Warn, " l1.KnownBlockSkipped=" + std::to_string(skipped) + "@") == 1);
	}

	void TestLoadRejectsMissingConfirmedClear(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		constexpr char clearKey[] = "\x83\xa4" "type" "\xcd\0\x06\xa9" "contentId";
		CorruptPageFile file{ { { std::string(clearKey, sizeof(clearKey) - 1), 1,
			sizeof(clearKey) - sizeof("contentId"), "X" } } };
		PagedInkStore store(PresentationOptions(root, wake), { nullptr, &file, nullptr, CorruptPageFile::BeforeRead });
		auto page = BoundaryPage(1, 0, 10, 30.0f);
		page.slideId = 21;
		const PagedInkBoundaryIdentity boundary{ 14, 1 };
		PAGED_REQUIRE(store.Save(SavePage(std::move(page), 1, PagedInkRequestKind::Full, boundary.generation)) ==
			PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(2, 1, 50, 0, 22), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = TakeAll(store);
		PAGED_REQUIRE(saved.size() == 2 && saved[0].result == PagedInkResult::Succeeded &&
			saved[0].confirmedBoundary == boundary && saved[1].result == PagedInkResult::Succeeded);

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 131 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 132 }, boundary }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 133 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(2), kSlot, 134 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto reads = TakeAll(store);
		PAGED_REQUIRE(file.patched && reads.size() == 4 && reads[0].identity.loadGeneration == 131 &&
			reads[1].identity.loadGeneration == 132 && reads[2].identity.loadGeneration == 133 &&
			reads[3].identity.loadGeneration == 134 && reads[2].result == PagedInkResult::Succeeded &&
			reads[2].lanePages.size() == 2);
		PAGED_CHECK(log.rows.size() == 5 && log.Count(StoreLog::Level::Error, " reason=boundary_mismatch") == 3);
		for (const std::string_view operation : { "load", "load_previous", "load_lane" })
			PAGED_CHECK(log.Count(StoreLog::Level::Error, "[UInkRead] scene=ppt op=" + std::string(operation) +
				" page=1 result=failed skippedBlocks=1") == 1);

		const auto* lanePage = FindLanePage(reads[2], Guid(1));
		PAGED_REQUIRE(lanePage && lanePage->metadata == PagedInkPageMetadata{ Guid(1), 0, 1, 21, 0 });
		const PagedInkPageResult* failedReads[] = { &reads[0], &reads[1], lanePage };
		for (const auto* read : failedReads)
		{
			PAGED_CHECK(read->result == PagedInkResult::Failed && read->error == PagedInkError::BoundaryMismatch &&
				read->confirmedRevision == 1 && read->confirmedBoundary == boundary &&
				!read->snapshot && !read->readDegraded);
			PAGED_REQUIRE(read->readSummary);
			PAGED_CHECK(read->readSummary->outcome != UInkReadOutcome::Failed &&
				read->readSummary->diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)]
					.count == 1);
		}
		CheckLanePage(test, reads[2], { Guid(2), 1, 2, 22, 0 }, 1, 50);
		PAGED_REQUIRE(reads[3].result == PagedInkResult::Succeeded && reads[3].snapshot &&
			reads[3].snapshot->strokes.size() == 1);
		PAGED_CHECK(!reads[3].readDegraded && HasPen(reads[3].snapshot->strokes.front(), 50));
	}

	void TestLoadRejectsUnexpectedClear(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		// Ink 和 Clear 共用 contentId、undoId。只改 type，L1 会把其余键当作扩展键跳过。
		constexpr char inkType[] = "\xa4" "type" "\xcd\0\x03";
		CorruptPageFile file{ { { std::string(inkType, sizeof(inkType) - 1), 1, sizeof(inkType) - 2, "\x06" } } };
		PagedInkStore store(Options(root, wake), { nullptr, &file, nullptr, CorruptPageFile::BeforeRead });
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 10), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && !saved->confirmedBoundary);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 135 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 136 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto reads = TakeAll(store);
		PAGED_REQUIRE(file.patched && reads.size() == 2 && reads[1].result == PagedInkResult::Succeeded &&
			reads[1].lanePages.size() == 1);
		const auto wire = ReadUInkStoreFile(store.FilePath());
		PAGED_REQUIRE(wire.document && wire.document->canvases.size() == 1 &&
			wire.document->canvases[0].content.size() == 1 &&
			std::holds_alternative<UInkClear>(wire.document->canvases[0].content.front()));
		const PagedInkPageResult* failedReads[] = { &reads[0], &reads[1].lanePages.front() };
		for (const auto* read : failedReads)
			PAGED_CHECK(read->result == PagedInkResult::Failed && read->error == PagedInkError::BoundaryMismatch &&
				read->confirmedRevision == 1 && !read->confirmedBoundary && !read->snapshot);
	}

	void TestLoadRejectsTemporaryIdentity(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		// 用一个同长的 deviceGuid 键值对替代背景字段和层号的填充字节，保持 Canvas 与相邻页的坐标。
		constexpr char background[] = "\xab" "layerNumber" "\xce\0\0\0\0"
			"\xaa" "background" "\x82\xa5" "color" "\x81\xa8" "fallback" "\xce\0\x12\x34\x56"
			"\xa7" "pattern" "\xd2\0\0\0\x02";
		constexpr char device[] = "\xab" "layerNumber" "\xcc\0\xaa" "deviceGuid" "\xd9\x24";
		const auto replacement = std::string(device, sizeof(device) - 1) + FormatUInkGuid(Guid(99));
		PAGED_REQUIRE(replacement.size() == sizeof(background) - 1);
		CorruptPageFile file{ { { "layerIndex", 1, 0, "X" },
			{ std::string(background, sizeof(background) - 1), 1, 0, replacement } } };
		PagedInkStore store(Options(root, wake), { nullptr, &file, nullptr, CorruptPageFile::BeforeRead });
		auto devicePage = Page(2, 1, 20);
		devicePage.background = Draw3UInkBackground{ 0x123456, 2 };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 10), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(std::move(devicePage), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(3, 2, 30), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = TakeAll(store);
		PAGED_REQUIRE(saved.size() == 3 && std::all_of(saved.begin(), saved.end(), [](const auto& receipt)
			{ return receipt.result == PagedInkResult::Succeeded; }));

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 141 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(2), kSlot, 142 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 143 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto reads = TakeAll(store);
		PAGED_REQUIRE(file.patched && reads.size() == 3 && reads[0].identity.loadGeneration == 141 &&
			reads[1].identity.loadGeneration == 142 && reads[2].identity.loadGeneration == 143 &&
			reads[2].result == PagedInkResult::Succeeded && reads[2].lanePages.size() == 3);
		const auto* layer = FindLanePage(reads[2], Guid(1));
		const auto* implicitDevice = FindLanePage(reads[2], Guid(2));
		PAGED_REQUIRE(layer && implicitDevice);
		const PagedInkPageResult* failedReads[] = { &reads[0], &reads[1], layer, implicitDevice };
		for (const auto* read : failedReads)
		{
			PAGED_CHECK(read->result == PagedInkResult::Failed && read->error == PagedInkError::ReadFailed &&
				read->confirmedRevision == 1 && !read->snapshot && !read->readDegraded);
			PAGED_REQUIRE(read->readSummary);
			PAGED_CHECK(read->readSummary->outcome != UInkReadOutcome::Failed &&
				read->readSummary->diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::TemporaryIdentity)].count == 1);
		}
		const auto wire = ReadUInkStoreFile(store.FilePath());
		PAGED_REQUIRE(wire.document && wire.document->usesImplicitDevice && wire.provenance.usedTemporaryIdentity &&
			wire.document->canvases.size() == 3);
		PAGED_CHECK(wire.document->canvases[0].pageGuid == Guid(1) &&
			wire.document->canvases[0].layerIndex == 0 && wire.document->canvases[0].temporaryLayer &&
			wire.document->canvases[1].pageGuid == Guid(2) &&
			!wire.document->canvases[1].temporaryLayer && !wire.document->canvases[1].deviceGuid);
		CheckLanePage(test, reads[2], { Guid(3), 2, 3, std::nullopt, 0 }, 1, 30);
	}

	enum class SpliceReadFailure { Open, Validate, Exception, Import };

	void TestSpliceReadFailure(TestState& test, const TempDirectory& root, SpliceReadFailure failure)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		OverridePageRead damaged{ root.Path() + L"\\prefix-copy.uink" };
		auto options = Options(root, wake);
		PagedInkStoreTestHook hook{ .context = &damaged, .readPathOverride = OverridePageRead::GetPath };
		if (failure == SpliceReadFailure::Exception)
			hook.afterSplicePrefixRead = [](void*) { throw std::runtime_error("after prefix read"); };
		if (failure == SpliceReadFailure::Import)
			options.importOptions.dpiScale = 0;
		PagedInkStore store(options, hook);
		constexpr uint64_t generation = 15;
		const PagedInkBoundaryIdentity boundary{ generation, 1 };
		auto prefix = BoundaryPage(1, 0, 10);
		if (failure == SpliceReadFailure::Import)
		{
			const auto arrow = ArrowShape(0xf2);
			PAGED_REQUIRE(arrow && arrow->parts.size() == 2);
			prefix.operations.insert(prefix.operations.begin(), ShapeStroke(*arrow, 7));
		}
		PAGED_REQUIRE(store.Save(SavePage(std::move(prefix), 1, PagedInkRequestKind::Clear, generation)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded && clear->confirmedBoundary == boundary);
		if (failure == SpliceReadFailure::Validate)
		{
			// 页段解码保留撤回组编号，严格拼接要求前缀的首组从 0 开始。
			constexpr char firstUndo[] = "undoId\xce\x00\x00\x00\x00";
			const CorruptPageFile file{ { { std::string(firstUndo, sizeof(firstUndo) - 1), 1,
				sizeof(firstUndo) - 2, "\x01" } } };
			PAGED_REQUIRE(file.WriteDamagedCopy(store.FilePath(), damaged.path));
		}
		else if (failure == SpliceReadFailure::Import)
		{
			// 跳过箭头第二个部件，严格拼接拒绝缺件前缀，再由无效缩放触发 L3 恢复失败。
			const CorruptPageFile file{ { { "shapeType", 2, 0, "X" } } };
			PAGED_REQUIRE(file.WriteDamagedCopy(store.FilePath(), damaged.path));
		}
		damaged.remaining = failure == SpliceReadFailure::Exception ? 0 : 1;
		log.rows.clear();
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 30), 2, PagedInkRequestKind::Full, generation, boundary)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto failed = store.Take();
		PAGED_REQUIRE(failed && failed->result == PagedInkResult::Failed && !failed->file.failed && !store.Take());
		PAGED_CHECK(std::string_view(PagedInkWriteDiagnosticStage(*failed)) == "written" && failed->pageBytes == 0);
		PAGED_CHECK(damaged.remaining == 0);
		std::string_view expectedStage;
		std::string_view expectedReason;
		switch (failure)
		{
		case SpliceReadFailure::Open:
			PAGED_CHECK(failed->error == PagedInkError::ReadFailed);
			expectedStage = " stage=file.open ";
			expectedReason = " reason=read_failed";
			break;
		case SpliceReadFailure::Exception:
			PAGED_CHECK(failed->error == PagedInkError::Exception);
			expectedStage = " stage=prefix_validate ";
			expectedReason = " reason=exception";
			break;
		case SpliceReadFailure::Validate:
		case SpliceReadFailure::Import:
			PAGED_CHECK(failed->error == PagedInkError::SpliceFailed &&
				failed->prefixReadDegraded == (failure == SpliceReadFailure::Import));
			expectedStage = failure == SpliceReadFailure::Import ? " stage=prefix_import " : " stage=prefix_validate ";
			expectedReason = " reason=splice_failed";
			break;
		}
		if (failure == SpliceReadFailure::Validate || failure == SpliceReadFailure::Exception)
			PAGED_CHECK(failed->readSummary && failed->readSummary->outcome == UInkReadOutcome::Complete);

		constexpr std::string_view readTag = "[UInkRead] scene=whiteboard op=splice_prefix page=1 ";
		PAGED_CHECK(std::count_if(log.rows.begin(), log.rows.end(), [&](const auto& row)
			{ return std::string_view(row.text).starts_with(readTag); }) == 1);
		PAGED_CHECK(std::count_if(log.rows.begin(), log.rows.end(), [&](const auto& row)
			{
				return row.level == StoreLog::Level::Error && row.thread != GetCurrentThreadId() &&
					std::string_view(row.text).starts_with(readTag) && row.text.find(" result=failed ") != std::string::npos &&
					row.text.find(expectedStage) != std::string::npos && row.text.find(expectedReason) != std::string::npos;
			}) == 1);
	}

	void TestSpliceCompoundPrefixLog(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		const auto options = Options(root, wake);
		PagedInkStore store(options);
		constexpr uint64_t generation = 15;
		const PagedInkBoundaryIdentity boundary{ generation, 1 };
		const auto arrow = ArrowShape(0xf2);
		PAGED_REQUIRE(arrow && arrow->parts.size() == 2);
		auto prefix = BoundaryPage(1, 0, 10);
		prefix.operations.insert(prefix.operations.begin(), ShapeStroke(*arrow, 7));
		const auto exported = ExportDraw3PageToUInkV11(prefix, options.pageContext);
		PAGED_REQUIRE(exported.canvas && exported.canvas->content.size() == 5);
		PAGED_REQUIRE(store.Save(SavePage(std::move(prefix), 1, PagedInkRequestKind::Clear, generation)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded && clear->confirmedBoundary == boundary &&
			!clear->degraded);

		constexpr std::string_view completeRead = "[UInkRead] scene=whiteboard op=splice_prefix page=1 result=complete ";
		log.rows.clear();
		// 当前区间重复使用前缀的图形 ID，拼接失败应由写入行报告。
		auto tail = Page(1, 0, 30);
		tail.strokes = { ShapeStroke(*arrow, 0) };
		PAGED_REQUIRE(store.Save(SavePage(std::move(tail), 2, PagedInkRequestKind::Full, generation, boundary)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto failed = store.Take();
		PAGED_REQUIRE(failed && failed->result == PagedInkResult::Failed && failed->error == PagedInkError::SpliceFailed);
		PAGED_CHECK(log.rows.size() == 2 && log.Count(StoreLog::Level::Debug, completeRead) == 1 &&
			log.Count(StoreLog::Level::Debug, " strokes=3 ") == 1 &&
			log.Count(StoreLog::Level::Error,
				"[UInkWrite] scene=whiteboard op=append page=1 result=failed stage=preflight.splice") == 1);

		log.rows.clear();
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 30), 3, PagedInkRequestKind::Full, generation, boundary)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded);
		PAGED_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Debug, completeRead) == 1 &&
			log.Count(StoreLog::Level::Debug, " strokes=3 ") == 1);
	}

	void TestSpliceAfterDiskDamage(TestState& test, const TempDirectory& root, bool prefixDamaged)
	{
		WakeCounter wake;
		OverridePageRead damaged{ root.Path() + L"\\damaged-page.uink" };
		PagedInkStore store(Options(root, wake), { .context = &damaged, .readPathOverride = OverridePageRead::GetPath });
		constexpr uint64_t generation = 15;
		PagedInkBoundaryIdentity boundary{ generation, 1 };
		PAGED_REQUIRE(store.Save(SavePage(BoundaryPage(1, 0, 10), 1, PagedInkRequestKind::Clear, generation)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto clear = store.Take();
		PAGED_REQUIRE(clear && clear->result == PagedInkResult::Succeeded && clear->confirmedBoundary == boundary &&
			!clear->snapshot && !clear->degraded && !clear->file.sealed);
		if (!prefixDamaged)
		{
			auto oldTail = Page(1, 0, 20);
			oldTail.strokes.push_back(Pen(21, 1));
			PAGED_REQUIRE(store.Save(SavePage(std::move(oldTail), 2, PagedInkRequestKind::Full, generation, boundary)) ==
				PagedInkAdmission::Accepted);
			store.Drain();
			const auto full = store.Take();
			boundary.revision = 2;
			PAGED_REQUIRE(full && full->result == PagedInkResult::Succeeded && full->confirmedBoundary == boundary &&
				!full->degraded && !full->file.sealed);
		}

		// 写句柄独占原文件的写入权，等长损坏副本保留真实目录坐标，由同一个后台读取路径解码。
		const auto path = store.FilePath();
		CorruptPageFile file{ { { "inkType", prefixDamaged ? 1u : 5u, 0, "X" } } };
		PAGED_REQUIRE(!path.empty() && WriterDeniesExternalWrite(path) && file.WriteDamagedCopy(path, damaged.path));
		if (!prefixDamaged)
		{
			damaged.remaining = 3;
			PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 151 }) == PagedInkAdmission::Accepted);
			PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 152 }, boundary }) ==
				PagedInkAdmission::Accepted);
			PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 153 }) == PagedInkAdmission::Accepted);
			store.Drain();
			const auto reads = TakeAll(store);
			PAGED_REQUIRE(damaged.remaining == 0 && reads.size() == 3 && reads[0].identity.loadGeneration == 151 &&
				reads[1].identity.loadGeneration == 152 && reads[2].identity.loadGeneration == 153 &&
				reads[2].result == PagedInkResult::Succeeded && reads[2].lanePages.size() == 1);
			const PagedInkPageResult* wholePageReads[] = { &reads[0], &reads[2].lanePages.front() };
			for (const auto* read : wholePageReads)
			{
				PAGED_REQUIRE(read->result == PagedInkResult::Succeeded && read->snapshot &&
					read->snapshot->strokes.size() == 1);
				PAGED_CHECK(read->readDegraded && !read->prefixReadDegraded && read->tailReadDegraded &&
					HasPen(read->snapshot->strokes[0], 21));
			}
			const auto& previous = reads[1];
			PAGED_REQUIRE(previous.result == PagedInkResult::Succeeded && previous.snapshot && previous.readSummary &&
				previous.snapshot->strokes.size() == 2);
			PAGED_CHECK(!previous.readDegraded && !previous.prefixReadDegraded && !previous.tailReadDegraded &&
				previous.readSummary->outcome == UInkReadOutcome::Complete &&
				previous.readSummary->diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)]
					.count == 0 &&
				HasPen(previous.snapshot->strokes[0], 10) && HasPen(previous.snapshot->strokes[1], 11));
		}

		// 离页仅提交内存中的 B，A 只能从盘上已确认的边界恢复。
		damaged.remaining = 1;
		const auto revision = boundary.revision + 1;
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 30), revision, PagedInkRequestKind::Full, generation, boundary)) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		const PagedInkBoundaryIdentity updated{ generation, revision };
		PAGED_REQUIRE(damaged.remaining == 0 && saved && saved->result == PagedInkResult::Succeeded &&
			saved->error == PagedInkError::None && saved->confirmedRevision == revision &&
			saved->confirmedBoundary == updated && !saved->file.failed && !saved->file.sealed);
		PAGED_CHECK(saved->degraded == prefixDamaged && saved->readDegraded == prefixDamaged &&
			saved->prefixReadDegraded == prefixDamaged && !saved->tailReadDegraded);
		PAGED_CHECK(IsPageEvictable({ revision, revision, false, false, saved->degraded }, saved->file) ==
			!prefixDamaged);
		PAGED_CHECK(store.FilePath() == path && WriterDeniesExternalWrite(path));

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 154 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = store.Take();
		const size_t prefixCount = prefixDamaged ? 1 : 2;
		PAGED_REQUIRE(loaded && loaded->result == PagedInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1 && loaded->snapshot->operations.size() == prefixCount + 2);
		PAGED_CHECK(!loaded->readDegraded && loaded->degraded == prefixDamaged && loaded->confirmedBoundary == updated);
		const auto& operations = loaded->snapshot->operations;
		const auto* first = std::get_if<Draw3UInkStrokeSnapshot>(&operations.front());
		const auto* current = std::get_if<Draw3UInkStrokeSnapshot>(&operations.back());
		PAGED_REQUIRE(first && current && std::holds_alternative<Draw3UInkClearSnapshot>(operations[prefixCount]));
		PAGED_CHECK(HasPen(*first, prefixDamaged ? 11.0f : 10.0f) && HasPen(*current, 30) &&
			HasPen(loaded->snapshot->strokes[0], 30));
		if (!prefixDamaged)
		{
			const auto* second = std::get_if<Draw3UInkStrokeSnapshot>(&operations[1]);
			PAGED_CHECK(second && HasPen(*second, 11));
		}
	}

	void TestSpliceRecoversDamagedPrefix(TestState& test, const TempDirectory& root)
	{
		TestSpliceAfterDiskDamage(test, root, true);
	}

	void TestSpliceReplacesDamagedTail(TestState& test, const TempDirectory& root)
	{
		TestSpliceAfterDiskDamage(test, root, false);
	}

	void TestLoadLaneIdentityMismatch(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		const auto changedPage = FormatUInkGuid(Guid(1));
		PAGED_REQUIRE(!changedPage.empty() && changedPage.back() == '1');
		CorruptPageFile file{ { { changedPage, 1, changedPage.size() - 1, "9" }, { "binding", 1, 0, "X" } } };
		PagedInkStore store(PresentationOptions(root, wake), { nullptr, &file, nullptr, CorruptPageFile::BeforeRead });
		auto end = PresentationPage(3, 1, 30, 1);
		end.endScreen = true;
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 0, 10, 0, 21), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(2, 1, 20, 0, 22), 1)) == PagedInkAdmission::Accepted);
		// 结束页即使失去 binding 仍可被 L1 解码，服务必须按确认 lane 拒绝。
		PAGED_REQUIRE(store.Save(SavePage(end, 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(4, 0, 40, 1), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 4 && std::all_of(seeds.begin(), seeds.end(), [](const auto& receipt)
			{ return receipt.result == PagedInkResult::Succeeded; }));

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 121 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(3), kSlot, 122 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 123 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 124 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto receipts = TakeAll(store);
		PAGED_REQUIRE(file.patched && receipts.size() == 4 && receipts[0].identity.loadGeneration == 121 &&
			receipts[1].identity.loadGeneration == 122 && receipts[2].identity.loadGeneration == 123 &&
			receipts[3].identity.loadGeneration == 124);
		PAGED_REQUIRE(receipts[2].result == PagedInkResult::Succeeded && receipts[2].lanePages.size() == 2 &&
			receipts[3].result == PagedInkResult::Succeeded && receipts[3].lanePages.size() == 2);
		const auto* badGuid = FindLanePage(receipts[2], Guid(1));
		const auto* badBinding = FindLanePage(receipts[3], Guid(3));
		PAGED_REQUIRE(badGuid && badBinding);
		PAGED_CHECK(badGuid->metadata == PagedInkPageMetadata{ Guid(1), 0, 1, 21, 0 });
		PAGED_CHECK(badBinding->metadata == PagedInkPageMetadata{ Guid(3), 1, 2, std::nullopt, 1, std::nullopt, 1 });
		const PagedInkPageResult* failedReads[] = { &receipts[0], &receipts[1], badGuid, badBinding };
		for (const auto* read : failedReads)
			PAGED_CHECK(read->result == PagedInkResult::Failed && read->error == PagedInkError::ReadFailed &&
				read->confirmedRevision == 1 && !read->snapshot && !read->readDegraded);
		PAGED_REQUIRE(badGuid->readSummary && badBinding->readSummary);
		PAGED_CHECK(badGuid->readSummary->outcome == UInkReadOutcome::Failed &&
			badBinding->readSummary->outcome != UInkReadOutcome::Failed);
		CheckLanePage(test, receipts[2], { Guid(2), 1, 2, 22, 0 }, 1, 20);
		CheckLanePage(test, receipts[3], { Guid(4), 0, 1, std::nullopt, 1 }, 1, 40);
		PAGED_CHECK(wake.count == 8);
	}

	struct CompactGate
	{
		bool pauseFirst = false;
		UInkStoreTestFaultInjection firstFault;
		std::atomic<unsigned> attempts{ 0 };
		std::binary_semaphore entered{ 0 };
		std::binary_semaphore resume{ 0 };
		std::binary_semaphore blockedByEviction{ 0 };
		bool evictionBlockSignaled = false;
		uint64_t accepted = 0;
		std::atomic<bool> pauseBeforeWake{ false };
		std::binary_semaphore wakePaused{ 0 };
		std::binary_semaphore resumeWake{ 0 };

		static void BeforeWake(void* context) noexcept
		{
			auto& gate = *static_cast<CompactGate*>(context);
			if (!gate.pauseBeforeWake.exchange(false)) return;
			gate.wakePaused.release();
			gate.resumeWake.acquire();
		}

		static void OnCompactBlockedByEviction(void* context) noexcept
		{
			auto& gate = *static_cast<CompactGate*>(context);
			if (!std::exchange(gate.evictionBlockSignaled, true)) gate.blockedByEviction.release();
		}

		static void BeforeCompact(void* context) noexcept
		{
			auto& gate = *static_cast<CompactGate*>(context);
			if (++gate.attempts != 1) return;
			if (gate.pauseFirst)
			{
				gate.entered.release();
				gate.resume.acquire();
			}
			SetUInkStoreTestFaultInjection(gate.firstFault);
		}

		static uint64_t RecordAccepted(void* context) noexcept
		{
			return ++static_cast<CompactGate*>(context)->accepted;
		}

		PagedInkStoreTestHook Hook(uint64_t minBytes, uint32_t minPercent) noexcept
		{
			PagedInkStoreTestHook hook;
			hook.context = this;
			hook.recordAccepted = RecordAccepted;
			hook.compactMinSupersededBytes = minBytes;
			hook.compactMinSupersededPercent = minPercent;
			hook.beforeCompact = BeforeCompact;
			return hook;
		}
	};

	void TestCompactThresholds(TestState& test, const TempDirectory& root, uint64_t minBytes,
		uint32_t minPercent, bool expected)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		CompactGate gate;
		gate.pauseFirst = expected;
		PagedInkStore store(Options(root, wake), gate.Hook(minBytes, minPercent));
		ReleaseSignal release{ gate.resume };

		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 10), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 2, 20), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 2 && seeds[0].result == PagedInkResult::Succeeded &&
			seeds[1].result == PagedInkResult::Succeeded);
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 30), 2)) == PagedInkAdmission::Accepted);
		if (expected) PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		else store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedRevision == 2);
		const auto superseded = seeds[0].pageBytes;
		const bool sizeMet = superseded >= minBytes;
		const bool shareMet = superseded * 100 >= saved->fileBytes * minPercent;
		PAGED_CHECK(expected ? sizeMet && shareMet : sizeMet != shareMet);

		const auto path = store.FilePath();
		const auto before = CorruptPageFile::ReadBytes(path);
		PAGED_REQUIRE(before && before->size() == saved->fileBytes);
		if (!expected)
		{
			PAGED_CHECK(gate.attempts == 0 && !store.FileState().compacting);
			PAGED_CHECK(log.Count(StoreLog::Level::Debug, "[UInkCompact]") == 0 && !store.Take());
			return;
		}

		const auto fullBefore = ReadUInkStoreFile(path);
		PAGED_REQUIRE(fullBefore.status == UInkReadStatus::Complete && fullBefore.document &&
			fullBefore.document->headerExtension);
		UInkPageDirectory directory;
		directory.context.headerExtension = fullBefore.document->headerExtension;
		directory.context.usesImplicitDevice = directory.context.headerExtension->devices.empty();
		directory.context.usesImplicitWorkspace = directory.context.headerExtension->workspaces.empty();
		const auto readPages = [&](uint64_t secondPageOffset)
		{
			directory.pages.insert_or_assign(Guid(2).Bytes(), UInkPageRange{ secondPageOffset, seeds[1].pageBytes, 2 });
			directory.pages.insert_or_assign(Guid(1).Bytes(),
				UInkPageRange{ secondPageOffset + seeds[1].pageBytes, saved->pageBytes, 7 });
			return std::array{ ReadUInkPage(path, directory, Guid(2)), ReadUInkPage(path, directory, Guid(1)) };
		};
		const auto pagesBefore = readPages(seeds[0].fileBytes);
		release.Release();
		store.Drain();
		const auto after = CorruptPageFile::ReadBytes(path);
		const auto retainedBytes = seeds[1].pageBytes + saved->pageBytes;
		PAGED_REQUIRE(after && after->size() >= retainedBytes);
		PAGED_CHECK(after->size() < before->size() && before->size() - after->size() == superseded);
		PAGED_CHECK(before->substr(seeds[0].fileBytes) == after->substr(after->size() - retainedBytes));
		const auto pagesAfter = readPages(after->size() - retainedBytes);
		for (size_t i = 0; i < pagesBefore.size(); ++i)
		{
			const auto& oldPage = pagesBefore[i];
			const auto& newPage = pagesAfter[i];
			PAGED_REQUIRE(oldPage.status == UInkReadStatus::Complete && oldPage.document &&
				oldPage.document->canvases.size() == 1 && newPage.status == UInkReadStatus::Complete &&
				newPage.document && newPage.document->canvases.size() == 1);
			const auto& oldCanvas = oldPage.document->canvases.front();
			const auto& newCanvas = newPage.document->canvases.front();
			PAGED_REQUIRE(oldCanvas.content.size() == 1 && newCanvas.content.size() == 1);
			const auto* oldInk = std::get_if<UInkInk>(&oldCanvas.content.front());
			const auto* newInk = std::get_if<UInkInk>(&newCanvas.content.front());
			PAGED_REQUIRE(oldInk && newInk && oldInk->points.size() == 2 && newInk->points.size() == 2);
			PAGED_CHECK(oldCanvas.pageGuid == Guid(i == 0 ? 2 : 1) && newCanvas.pageGuid == oldCanvas.pageGuid &&
				oldCanvas.pageIndex == (i == 0 ? 2u : 7u) && newCanvas.pageIndex == oldCanvas.pageIndex &&
				oldInk->points.front().x == (i == 0 ? 20.0f : 30.0f) &&
				oldInk->points.front().x == newInk->points.front().x &&
				oldInk->points.back().x == newInk->points.back().x);
		}
		PAGED_CHECK(gate.attempts == 1 && wake.count == 3 && !store.Take() && !store.FileState().compacting);
		PAGED_CHECK(log.Count(StoreLog::Level::Debug, "[UInkCompact] scene=whiteboard event=start") == 1 &&
			log.Count(StoreLog::Level::Debug, "[UInkCompact] scene=whiteboard event=finish result=succeeded") == 1 &&
			log.Count(StoreLog::Level::Debug, " supersededBytes=" + std::to_string(superseded) + " ") == 2 &&
			log.Count(StoreLog::Level::Debug, " elapsedMs=") == 2);
	}

	void TestCompactLanesAndClear(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		CompactGate gate;
		PagedInkStore store(PresentationOptions(root, wake), gate.Hook(1, 1));
		constexpr PagedInkBoundaryIdentity boundary{ 23, 2 };
		auto clear = BoundaryPage(1, 7, 10);
		clear.slideId = 101;
		auto order = PresentationPage(2, 7, 20, 1);
		const auto shape = LineShape(0xf2, static_cast<float>(kUInkMaxCoordinate) + 1024.0f);
		PAGED_REQUIRE(shape);
		order.strokes.push_back(ShapeStroke(*shape, 1));
		PAGED_REQUIRE(store.Save(SavePage(std::move(clear), 1, PagedInkRequestKind::Clear, boundary.generation)) ==
			PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(std::move(order), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 2 && seeds[0].result == PagedInkResult::Succeeded &&
			seeds[0].confirmedBoundary == PagedInkBoundaryIdentity{ boundary.generation, 1 } &&
			seeds[1].result == PagedInkResult::Succeeded && seeds[1].degraded);
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 51 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto orderBefore = store.Take();
		PAGED_REQUIRE(orderBefore && orderBefore->result == PagedInkResult::Succeeded &&
			orderBefore->lanePages.size() == 1 && orderBefore->lanePages.front().degraded &&
			!orderBefore->lanePages.front().readDegraded);

		auto full = BoundaryPage(1, 7, 10, 30.0f);
		full.slideId = 101;
		PAGED_REQUIRE(store.Save(SavePage(std::move(full), boundary.revision, PagedInkRequestKind::Full,
			boundary.generation)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedBoundary == boundary);
		PAGED_REQUIRE(gate.attempts == 1);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, 52 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadLane({ 1, kSlot, 53 }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 54 }, boundary }) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.LoadPreviousInterval({ { Guid(1), kSlot, 55 }, *seeds[0].confirmedBoundary }) ==
			PagedInkAdmission::Accepted);
		store.Drain();
		const auto reads = TakeAll(store);
		PAGED_REQUIRE(reads.size() == 4 && reads[0].identity.laneBinding == 0 && reads[1].identity.laneBinding == 1 &&
			reads[0].result == PagedInkResult::Succeeded && reads[0].lanePages.size() == 1 &&
			reads[1].result == PagedInkResult::Succeeded && reads[1].lanePages.size() == 1);
		const auto& stable = reads[0].lanePages.front();
		const auto& ordered = reads[1].lanePages.front();
		PAGED_REQUIRE(stable.result == PagedInkResult::Succeeded && stable.snapshot &&
			ordered.result == PagedInkResult::Succeeded && ordered.snapshot && ordered.snapshot->strokes.size() == 1);
		PAGED_CHECK(stable.metadata == PagedInkPageMetadata{ Guid(1), 7, 8, 101, 0 } &&
			stable.confirmedBoundary == boundary && stable.confirmedRevision == boundary.revision);
		CheckIntervals(test, *stable.snapshot, 10, 30.0f);
		PAGED_CHECK(ordered.metadata == PagedInkPageMetadata{ Guid(2), 7, 8, std::nullopt, 1 } &&
			ordered.confirmedRevision == 1 && !ordered.confirmedBoundary && ordered.degraded && !ordered.readDegraded &&
			HasPen(ordered.snapshot->strokes.front(), 20) &&
			!IsPageEvictable({ 1, 1, false, false, ordered.degraded }, store.FileState()));
		const auto& previous = reads[2];
		PAGED_REQUIRE(previous.result == PagedInkResult::Succeeded && previous.snapshot &&
			previous.snapshot->strokes.size() == 2);
		PAGED_CHECK(previous.identity.loadGeneration == 54 && previous.confirmedBoundary == boundary &&
			previous.confirmedRevision == boundary.revision && previous.snapshot->pageGuid == Guid(1) &&
			previous.snapshot->pageIndex == 7 && previous.snapshot->intervalOrdinal == 0 &&
			HasPen(previous.snapshot->strokes[0], 10) && HasPen(previous.snapshot->strokes[1], 11));
		PAGED_CHECK(reads[3].identity.loadGeneration == 55 && reads[3].result == PagedInkResult::Failed &&
			reads[3].error == PagedInkError::BoundaryMismatch && !reads[3].snapshot && wake.count == 8);
	}

	void TestCompactAndEvictionAreMutuallyExclusive(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		CompactGate gate;
		gate.pauseFirst = true;
		auto hook = gate.Hook(1, 1);
		hook.onCompactBlockedByEviction = CompactGate::OnCompactBlockedByEviction;
		hook.beforeWake = CompactGate::BeforeWake;
		PagedInkStore store(Options(root, wake), hook);
		ReleaseSignal releaseCompact{ gate.resume };
		ReleaseSignal releaseWake{ gate.resumeWake };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 10), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seed = store.Take();
		PAGED_REQUIRE(seed && seed->result == PagedInkResult::Succeeded);

		// 保存完成后、空闲整理开始前暂停 worker，使驱逐许可先于整理取得。
		gate.pauseBeforeWake = true;
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 20), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.wakePaused.try_acquire_for(10s));
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedRevision == 2);
		PAGED_REQUIRE(store.TryBeginEviction());
		const auto endEviction = [](PagedInkStore* store) { store->EndEviction(); };
		std::unique_ptr<PagedInkStore, decltype(endEviction)> permit(&store, endEviction);
		releaseWake.Release();
		PAGED_REQUIRE(gate.blockedByEviction.try_acquire_for(10s));
		PAGED_CHECK(gate.attempts == 0 && !store.FileState().compacting);

		permit.reset();
		// 不提交额外业务，整理应由归还许可唤醒。门闩固定整理先开始的反向交错。
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		PAGED_CHECK(store.FileState().compacting);
		const bool acquiredWhileCompacting = store.TryBeginEviction();
		if (acquiredWhileCompacting) store.EndEviction();
		PAGED_CHECK(!acquiredWhileCompacting && !store.TakeEvictionRetry() && wake.count == 2);

		releaseCompact.Release();
		store.Drain();
		const auto bytes = CorruptPageFile::ReadBytes(store.FilePath());
		PAGED_REQUIRE(bytes);
		PAGED_CHECK(gate.attempts == 1 && bytes->size() < saved->fileBytes &&
			!store.FileState().compacting && !store.Take() && wake.count == 3);
		PAGED_CHECK(store.TakeEvictionRetry() && !store.TakeEvictionRetry());
	}

	void TestCompactBusyAndClose(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		CompactGate gate;
		gate.pauseFirst = true;
		PagedInkStore store(Options(root, wake), gate.Hook(1, 1));
		std::binary_semaphore drainStarted{ 0 }, drainDone{ 0 }, closeStarted{ 0 }, closeDone{ 0 };
		std::jthread drain, close;
		ReleaseSignal release{ gate.resume };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 10), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 2, 20), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 2 && seeds[0].result == PagedInkResult::Succeeded &&
			seeds[1].result == PagedInkResult::Succeeded);
		const PagedInkEvictionState ready{ 1, 1 };
		PAGED_CHECK(IsPageEvictable(ready, store.FileState()));
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 30), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->requestId == 3 &&
			saved->confirmedRevision == 2);
		PAGED_CHECK(IsPageEvictable(ready, saved->file) && !IsPageEvictable(ready, store.FileState()) &&
			store.FileState().compacting && !store.FileState().failed);
		const auto tryEviction = [&](bool requiresHealthyFile)
		{
			const bool allowed = store.TryBeginEviction(requiresHealthyFile);
			if (allowed) store.EndEviction();
			return allowed;
		};
		// 在关闭前留下一个整理结束时本会发出控制唤醒的重试标记。
		PAGED_CHECK(!tryEviction(true));

		// 队列暂空，Drain 的唯一等待对象是已开始的整理。
		drain = std::jthread([&] { drainStarted.release(); store.Drain(); drainDone.release(); });
		PAGED_REQUIRE(drainStarted.try_acquire_for(10s));
		PAGED_CHECK(!drainDone.try_acquire_for(100ms));
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 2, 40), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 61 }) == PagedInkAdmission::Accepted);
		PAGED_CHECK(!store.Take() && wake.count == 3);

		close = std::jthread([&] { closeStarted.release(); store.Close(); closeDone.release(); });
		PAGED_REQUIRE(closeStarted.try_acquire_for(10s));
		unsigned probes = 0;
		const auto closeDeadline = std::chrono::steady_clock::now() + 10s;
		for (;;)
		{
			const auto admission = store.Load({ Guid(2), kSlot, 62u + probes });
			if (admission == PagedInkAdmission::Closed) break;
			PAGED_REQUIRE(admission == PagedInkAdmission::Accepted);
			// 若关闭线程尚未取得锁，同页待写请求让探测立即结算，不会改变后台队列。
			const auto probe = store.Take();
			PAGED_REQUIRE(probe && probe->requestId == 6u + probes && probe->identity.loadGeneration == 62u + probes &&
				probe->result == PagedInkResult::Failed && probe->error == PagedInkError::PendingWrite && wake.Wait());
			++probes;
			PAGED_REQUIRE(std::chrono::steady_clock::now() < closeDeadline);
			std::this_thread::sleep_for(1ms);
		}
		PAGED_CHECK(!closeDone.try_acquire_for(100ms));
		PAGED_CHECK(!tryEviction(false));
		PAGED_CHECK(!store.TakeEvictionRetry());
		release.Release();
		close.join();
		drain.join();

		PAGED_CHECK(!tryEviction(true));
		PAGED_CHECK(!tryEviction(false));
		PAGED_CHECK(!store.TakeEvictionRetry());
		const auto receipts = TakeAll(store);
		PAGED_REQUIRE(receipts.size() == 2 && receipts[0].snapshot && receipts[0].snapshot->strokes.size() == 1);
		PAGED_CHECK(receipts[0].requestId == 5 && receipts[0].identity.kind == PagedInkRequestKind::Load &&
			receipts[0].identity.loadGeneration == 61 && receipts[0].result == PagedInkResult::Succeeded &&
			receipts[0].confirmedRevision == 2 && HasPen(receipts[0].snapshot->strokes.front(), 30));
		PAGED_CHECK(receipts[1].requestId == 4 && receipts[1].identity.kind == PagedInkRequestKind::Full &&
			receipts[1].identity.pageGuid == Guid(2) && receipts[1].result == PagedInkResult::Succeeded &&
			receipts[1].confirmedRevision == 2);
		// 关闭期间最后一次保存会产生新的旧段，但不启动第二轮整理。
		PAGED_CHECK(wake.count == 5u + probes && gate.attempts == 1 && !store.FileState().compacting);
	}

	void TestCompactReopenFailure(TestState& test, const TempDirectory& root, bool failMove)
	{
		ScopedUInkLog log;
		ScopedFaults reset(UInkStoreTestFaultInjection{});
		WakeCounter wake;
		CompactGate gate;
		gate.pauseFirst = true;
		gate.firstFault = { .failMove = failMove, .failReopen = true };
		PagedInkStore store(Options(root, wake), gate.Hook(1, 1));
		ReleaseSignal release{ gate.resume };
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 10), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 2, 20), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 2 && seeds[0].result == PagedInkResult::Succeeded &&
			seeds[1].result == PagedInkResult::Succeeded);
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 30), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(gate.entered.try_acquire_for(10s));
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedRevision == 2 &&
			!saved->file.failed && !saved->file.sealed && !saved->file.tailUnconfirmed);
		PAGED_CHECK(!IsPageEvictable({ 2, 2 }, store.FileState()));
		PAGED_REQUIRE(store.Save(SavePage(Page(3, 3, 40), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 71 }) == PagedInkAdmission::Accepted);
		release.Release();
		store.Drain();
		const auto file = store.FileState();
		PAGED_CHECK(file.failed && file.sealed && !file.tailUnconfirmed && !file.compacting &&
			!IsPageEvictable({ 2, 2 }, file));
		PAGED_CHECK(store.Save(SavePage(Page(4, 4, 50), 1)) == PagedInkAdmission::FileFailed);
		const auto completed = TakeAll(store);
		PAGED_REQUIRE(completed.size() == 2);
		const auto queuedSave = std::find_if(completed.begin(), completed.end(),
			[](const auto& receipt) { return receipt.requestId == 4; });
		const auto queuedLoad = std::find_if(completed.begin(), completed.end(),
			[](const auto& receipt) { return receipt.requestId == 5; });
		PAGED_REQUIRE(queuedSave != completed.end() && queuedLoad != completed.end() &&
			queuedLoad->snapshot && queuedLoad->snapshot->strokes.size() == 1);
		PAGED_CHECK(queuedSave->result == PagedInkResult::FileFailed && !queuedSave->saveDequeued &&
			queuedSave->file.failed && queuedSave->file.sealed && !queuedSave->file.tailUnconfirmed &&
			!queuedSave->confirmedRevision && queuedSave->pageBytes == 0);
		PAGED_CHECK(queuedLoad->result == PagedInkResult::Succeeded && queuedLoad->confirmedRevision == 2 &&
			queuedLoad->file.failed && queuedLoad->snapshot->pageIndex == 7 &&
			HasPen(queuedLoad->snapshot->strokes.front(), 30));

		// 新旧两种目录都保留原来确认的其他页，且偏移必须与实际提交状态一致。
		PAGED_REQUIRE(store.Load({ Guid(2), kSlot, 72 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto other = store.Take();
		PAGED_REQUIRE(other && other->result == PagedInkResult::Succeeded && other->confirmedRevision == 1 &&
			other->snapshot && other->snapshot->strokes.size() == 1);
		PAGED_CHECK(other->snapshot->pageIndex == 2 && HasPen(other->snapshot->strokes.front(), 20));
		const auto bytes = CorruptPageFile::ReadBytes(store.FilePath());
		PAGED_REQUIRE(bytes);
		PAGED_CHECK(bytes->size() == saved->fileBytes - (failMove ? 0 : seeds[0].pageBytes));
		PAGED_CHECK(gate.attempts == 1 && wake.count == 6 && !store.Take());
		PAGED_CHECK(log.Count(StoreLog::Level::Error,
			"[UInkCompact] scene=whiteboard event=finish result=failed") == 1 &&
			log.Count(StoreLog::Level::Error, failMove ? " stage=compact.publish " : " stage=compact.reopen ") == 1 &&
			log.Count(StoreLog::Level::Error, failMove ? " replacementCommitted=0 " : " replacementCommitted=1 ") == 1 &&
			log.Count(StoreLog::Level::Error, " sealed=1") == 1);
	}

	void TestCompactRetryAfterSuccessfulWrite(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		ScopedFaults reset(UInkStoreTestFaultInjection{});
		WakeCounter wake;
		CompactGate gate;
		gate.firstFault = { .failFlush = true };
		PagedInkStore store(Options(root, wake), gate.Hook(1, 1));
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 10), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seed = store.Take();
		PAGED_REQUIRE(seed && seed->result == PagedInkResult::Succeeded);
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 30), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedRevision == 2);
		PAGED_CHECK(gate.attempts == 1 && IsPageEvictable({ 2, 2 }, store.FileState()));
		const auto original = CorruptPageFile::ReadBytes(store.FilePath());
		PAGED_REQUIRE(original && original->size() == saved->fileBytes);

		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 81 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto read = store.Take();
		PAGED_REQUIRE(read && read->result == PagedInkResult::Succeeded && read->snapshot &&
			read->snapshot->strokes.size() == 1 && HasPen(read->snapshot->strokes.front(), 30));
		PAGED_CHECK(gate.attempts == 1);
		{
			const ScopedFaults fault({ .failFlush = true });
			PAGED_REQUIRE(store.Save(SavePage(Page(1, 7, 40), 3)) == PagedInkAdmission::Accepted);
			store.Drain();
			const auto failed = store.Take();
			PAGED_REQUIRE(failed && failed->result == PagedInkResult::Failed && !failed->file.failed);
		}
		PAGED_CHECK(gate.attempts == 1 &&
			log.Count(StoreLog::Level::Error, "[UInkCompact] scene=whiteboard event=finish result=failed") == 1);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 2, 20), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto recovered = store.Take();
		PAGED_REQUIRE(recovered && recovered->result == PagedInkResult::Succeeded);
		const auto compacted = CorruptPageFile::ReadBytes(store.FilePath());
		PAGED_REQUIRE(compacted);
		PAGED_CHECK(gate.attempts == 2 && compacted->size() == recovered->fileBytes - seed->pageBytes &&
			wake.count == 5 && !store.Take());
	}


	void TestCompactRepairsPageIdentityMismatch(TestState& test, const TempDirectory& root, bool ppt)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		OverridePageRead readOverride{ root.Path() + L"\\identity-copy.uink" };
		PagedInkStoreTestHook hook{ .context = &readOverride, .readPathOverride = OverridePageRead::GetPath };
		PagedInkStore store(ppt ? PresentationOptions(root, wake) : Options(root, wake), hook);
		const auto page = ppt ? PresentationPage(1, 7, 10, 0, 21) : Page(1, 7, 10);
		PAGED_REQUIRE(store.Save(SavePage(page, 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seed = store.Take();
		PAGED_REQUIRE(seed && seed->result == PagedInkResult::Succeeded &&
			seed->pageBytes > 0 && seed->pageBytes < hook.compactMinSupersededBytes);

		// 仅改固定宽度整数的最低字节，页段长度和其余字段保持原样。
		constexpr char pageIndex[] = "pageIndex\xce\x00\x00\x00\x07";
		constexpr char slideId[] = "slideId\xd2\x00\x00\x00\x15";
		const auto field = ppt ? std::string(slideId, sizeof(slideId) - 1) :
			std::string(pageIndex, sizeof(pageIndex) - 1);
		const CorruptPageFile patch{ { { field, 1, field.size() - 1, ppt ? "\x16" : "\x08" } } };
		PAGED_REQUIRE(patch.WriteDamagedCopy(store.FilePath(), readOverride.path));
		const auto decoded = ReadUInkStoreFile(readOverride.path);
		PAGED_REQUIRE(decoded.document && decoded.document->canvases.size() == 1);
		const auto& canvas = decoded.document->canvases.front();
		PAGED_CHECK(canvas.pageGuid == page.pageGuid && canvas.pageIndex == (ppt ? 7 : 8) &&
			canvas.slideId == (ppt ? std::optional<int32_t>{ 22 } : std::nullopt));

		readOverride.remaining = 1;
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 101 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto read = store.Take();
		PAGED_REQUIRE(read && read->readSummary);
		PAGED_CHECK(read->readSummary->outcome == UInkReadOutcome::Complete &&
			read->result == PagedInkResult::Failed && read->error == PagedInkError::ReadFailed &&
			read->confirmedRevision == 1 && !read->snapshot && !read->readDegraded);

		PAGED_REQUIRE(store.Save(SavePage(page, 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded);
		const auto compacted = CorruptPageFile::ReadBytes(store.FilePath());
		PAGED_REQUIRE(compacted);
		PAGED_CHECK(compacted->size() < saved->fileBytes && log.Count(StoreLog::Level::Debug,
			"event=finish result=succeeded") == 1);
	}

	void TestCompactDistinguishesReadLimitAndBadBlock(TestState& test, const TempDirectory& root,
		bool limit, bool badBlock)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		OverridePageRead readOverride{ root.Path() + L"\\limit-copy.uink" };
		PagedInkStoreTestHook hook{ .context = &readOverride, .readPathOverride = OverridePageRead::GetPath };
		PagedInkStore store(Options(root, wake), hook);
		auto page = Page(1, 0, 10);
		auto wideStroke = Pen(20, 1);
		for (int i = 0; i < 8; ++i) wideStroke.points.push_back({ 30.0f + i, 10.0f, 5.0f });
		page.strokes.push_back(std::move(wideStroke));
		page.strokes.push_back(Pen(40, 2));
		PAGED_REQUIRE(store.Save(SavePage(page, 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seed = store.Take();
		PAGED_REQUIRE(seed && seed->result == PagedInkResult::Succeeded &&
			seed->pageBytes > 0 && seed->pageBytes < hook.compactMinSupersededBytes);

		CorruptPageFile patch;
		if (limit)
		{
			const auto bytes = CorruptPageFile::ReadBytes(store.FilePath());
			PAGED_REQUIRE(bytes);
			const std::string inkPrefix("\xa4" "type\xcd\x00\x03", 8);
			const auto first = bytes->find(inkPrefix);
			PAGED_REQUIRE(first != std::string::npos && first > 0);
			const auto second = bytes->find(inkPrefix, first + inkPrefix.size());
			PAGED_REQUIRE(second != std::string::npos);
			const auto third = bytes->find(inkPrefix, second + inkPrefix.size());
			PAGED_REQUIRE(third != std::string::npos);

			// 用第一笔的合法字段做等长替换；未知键的嵌套值只超出默认 wire 深度。
			auto limited = bytes->substr(first - 1, second - first);
			const auto mapByte = static_cast<unsigned char>(limited.front());
			PAGED_REQUIRE(mapByte >= 0x80 && mapByte < 0x8f);
			limited.front() = static_cast<char>(mapByte + 1);
			for (const auto* key : { "contentId", "undoId" })
			{
				const auto offset = limited.find(key);
				PAGED_REQUIRE(offset != std::string::npos);
				const auto value = offset + std::string_view(key).size();
				PAGED_REQUIRE(value + 5 <= limited.size() &&
					limited.compare(value, 5, "\xce\x00\x00\x00\x00", 5) == 0);
				limited[value + 4] = '\x01';
			}
			limited += "\xa1x";
			limited.append(UInkReadLimits{}.maxDepth + 1, '\x91');
			const auto wireLength = third - second;
			PAGED_REQUIRE(wireLength >= limited.size() + 2 && wireLength - limited.size() - 2 <= 255);
			const auto padding = wireLength - limited.size() - 2;
			limited += '\xd9';
			limited += static_cast<char>(padding);
			limited.append(padding, 'x');
			patch.patches.push_back({ bytes->substr(second - 1, wireLength), 1, 0, std::move(limited) });
		}
		if (badBlock) patch.patches.push_back({ "inkType", 3, 0, "X" });
		PAGED_REQUIRE(patch.WriteDamagedCopy(store.FilePath(), readOverride.path));
		if (limit && !badBlock)
		{
			const auto limited = ReadUInkStoreFile(readOverride.path);
			PAGED_CHECK(limited.status == UInkReadStatus::LimitExceeded &&
				limited.provenance.containsInvalidCompleteBlocks);
			UInkReadLimits relaxed;
			relaxed.maxDepth += 8;
			const auto decoded = ReadUInkStoreFile(readOverride.path, relaxed);
			PAGED_CHECK(decoded.status == UInkReadStatus::Complete &&
				!decoded.provenance.containsInvalidCompleteBlocks && decoded.document &&
				decoded.document->canvases.size() == 1 && decoded.document->canvases.front().content.size() == 3);
		}

		readOverride.remaining = 1;
		PAGED_REQUIRE(store.Load({ Guid(1), kSlot, 101 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto read = store.Take();
		PAGED_REQUIRE(read && read->result == PagedInkResult::Succeeded && read->snapshot && read->readSummary);
		const auto& counts = read->readSummary->diagnosticCounts;
		PAGED_CHECK(read->readDegraded && read->readSummary->outcome == UInkReadOutcome::Partial &&
			read->snapshot->strokes.size() == 3u - limit - badBlock &&
			(read->readStatus == UInkReadStatus::LimitExceeded) == limit &&
			counts[static_cast<size_t>(UInkDiagnosticCode::LimitExceeded)].count == static_cast<unsigned>(limit) &&
			counts[static_cast<size_t>(UInkDiagnosticCode::KnownBlockSkipped)].count == static_cast<unsigned>(badBlock));

		PAGED_REQUIRE(store.Save(SavePage(std::move(page), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded);
		const auto bytes = CorruptPageFile::ReadBytes(store.FilePath());
		PAGED_REQUIRE(bytes);
		PAGED_CHECK(log.Count(StoreLog::Level::Debug, "event=finish result=succeeded") ==
			static_cast<unsigned>(badBlock));
		PAGED_CHECK(badBlock ? bytes->size() < saved->fileBytes : bytes->size() == saved->fileBytes);
	}

	void TestCompactIgnoresTemporaryReadFailureAndL3Fallback(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		OverridePageRead readOverride{ root.Path() + L"\\missing.uink" };
		PagedInkStoreTestHook hook;
		hook.context = &readOverride;
		hook.readPathOverride = OverridePageRead::GetPath;
		hook.compactMinSupersededBytes = 1024;
		hook.compactMinSupersededPercent = 1;
		PagedInkStore store(Options(root, wake), hook);
		auto approximated = Page(2, 1, 20);
		approximated.strokes.front().renderOnlyWhenLatest = true;
		auto large = Page(3, 2, 30);
		for (uint32_t i = 1; i <= 32; ++i) large.strokes.push_back(Pen(30.0f + i, i));
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 10), 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(approximated, 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(std::move(large), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 3 && std::all_of(seeds.begin(), seeds.end(),
			[](const auto& receipt) { return receipt.result == PagedInkResult::Succeeded; }));
		PAGED_REQUIRE(seeds[0].pageBytes + seeds[1].pageBytes < hook.compactMinSupersededBytes &&
			seeds[2].pageBytes >= hook.compactMinSupersededBytes);

		const auto readBoth = [&](uint64_t generation)
		{
			readOverride.remaining = 1;
			if (!PAGED_CHECK(store.Load({ Guid(1), kSlot, generation }) == PagedInkAdmission::Accepted &&
				store.Load({ Guid(2), kSlot, generation + 1 }) == PagedInkAdmission::Accepted)) return false;
			store.Drain();
			const auto reads = TakeAll(store);
			return PAGED_CHECK(reads.size() == 2 && reads[0].result == PagedInkResult::Failed &&
				reads[0].readStatus == UInkReadStatus::IoError && reads[1].result == PagedInkResult::Succeeded &&
				reads[1].readStatus == UInkReadStatus::Complete && reads[1].fallbackCounts.latestVisibility == 1 &&
				!reads[1].readDegraded);
		};
		PAGED_REQUIRE(readBoth(101));
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 11), 2)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(std::move(approximated), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto replaced = TakeAll(store);
		PAGED_REQUIRE(replaced.size() == 2 && replaced[0].result == PagedInkResult::Succeeded &&
			replaced[1].result == PagedInkResult::Succeeded);
		PAGED_CHECK(log.Count(StoreLog::Level::Debug, "[UInkCompact]") == 0);

		// 两种读取结果对应的物理版本都仍是最新；只替换另一页也必须按常规阈值整理。
		PAGED_REQUIRE(readBoth(103));
		PAGED_REQUIRE(store.Save(SavePage(Page(3, 2, 31), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded);
		PAGED_CHECK((seeds[0].pageBytes + seeds[1].pageBytes + seeds[2].pageBytes) * 100 >= saved->fileBytes);
		const auto bytes = CorruptPageFile::ReadBytes(store.FilePath());
		PAGED_REQUIRE(bytes);
		PAGED_CHECK(bytes->size() < saved->fileBytes && wake.count == 10 && !store.Take() &&
			log.Count(StoreLog::Level::Debug, "[UInkCompact] scene=whiteboard event=finish result=succeeded") == 1 &&
			log.Count(StoreLog::Level::Debug, "event=deferred reason=current_page_damaged") == 0);
	}

#if defined(DRAW3_TESTING)
	struct CorruptCompactPages
	{
		CompactGate gate;
		std::array<std::optional<uint64_t>, 2> offsets;

		static std::optional<uint64_t> TakeOffset(void* context, const UInkGuid& guid) noexcept
		{
			auto& fixture = *static_cast<CorruptCompactPages*>(context);
			if (guid == Guid(2)) return std::exchange(fixture.offsets[0], std::nullopt);
			if (guid == Guid(3)) return std::exchange(fixture.offsets[1], std::nullopt);
			return std::nullopt;
		}

		PagedInkStoreTestHook Hook() noexcept
		{
			PagedInkStoreTestHook hook;
			hook.context = this;
			hook.beforeCompact = [](void* context) noexcept
				{ CompactGate::BeforeCompact(&static_cast<CorruptCompactPages*>(context)->gate); };
			hook.corruptOffsetBeforeRead = TakeOffset;
			return hook;
		}
	};

	void TestCompactRepairsSupersededDamage(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		CorruptCompactPages fixture;
		PagedInkStore store(Options(root, wake), fixture.Hook());
		for (uint8_t id = 1; id <= 4; ++id)
			PAGED_REQUIRE(store.Save(SavePage(Page(id, id - 1u, 10.0f * id), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 4 && std::all_of(seeds.begin(), seeds.end(),
			[](const auto& receipt) { return receipt.result == PagedInkResult::Succeeded && receipt.pageBytes > 0; }));
		const auto path = store.FilePath();
		PAGED_REQUIRE(!path.empty() && ReadUInkStoreFile(path).status == UInkReadStatus::Complete);
		// 在实际写入文件中破坏两个 Canvas 的对象头，保留各后续页段的物理范围。
		fixture.offsets[0] = seeds[1].fileBytes - seeds[1].pageBytes;
		PAGED_REQUIRE(store.Load({ Guid(2), kSlot, 91 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto damaged = store.Take();
		PAGED_REQUIRE(damaged && damaged->result == PagedInkResult::Failed && damaged->error == PagedInkError::ReadFailed);
		PAGED_CHECK(fixture.gate.attempts == 0 && WriterDeniesExternalWrite(path));

		{
			const ScopedFaults fault({ .failFlush = true });
			PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 21), 2)) == PagedInkAdmission::Accepted);
			store.Drain();
			const auto failed = store.Take();
			PAGED_REQUIRE(failed && failed->result == PagedInkResult::Failed && !failed->file.failed);
		}
		PAGED_CHECK(fixture.gate.attempts == 0);
		PAGED_REQUIRE(store.Save(SavePage(Page(4, 3, 41), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto unrelated = store.Take();
		PAGED_REQUIRE(unrelated && unrelated->result == PagedInkResult::Succeeded && unrelated->confirmedRevision == 2);
		PAGED_CHECK(fixture.gate.attempts == 0);

		fixture.offsets[1] = seeds[2].fileBytes - seeds[2].pageBytes;
		PAGED_REQUIRE(store.Load({ Guid(3), kSlot, 92 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto secondDamaged = store.Take();
		PAGED_REQUIRE(secondDamaged && secondDamaged->result == PagedInkResult::Failed &&
			secondDamaged->error == PagedInkError::ReadFailed);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 22), 3)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto replaced = store.Take();
		PAGED_REQUIRE(replaced && replaced->result == PagedInkResult::Succeeded && replaced->confirmedRevision == 3);
		PAGED_CHECK(fixture.gate.attempts == 0 &&
			log.Count(StoreLog::Level::Debug, "[UInkCompact] scene=whiteboard event=deferred reason=current_page_damaged") == 1);
		const auto before = ReadUInkStoreFile(path);
		PAGED_REQUIRE(before.status != UInkReadStatus::Complete && before.document);
		PAGED_CHECK(before.validPrefixLength == seeds[0].fileBytes && before.document->canvases.size() == 1 &&
			before.document->canvases.front().pageGuid == Guid(1));
		PAGED_CHECK(seeds[1].pageBytes + seeds[2].pageBytes + seeds[3].pageBytes < 8ull * 1024 * 1024);

		PAGED_REQUIRE(store.Save(SavePage(Page(3, 2, 32), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto restored = store.Take();
		PAGED_REQUIRE(restored && restored->result == PagedInkResult::Succeeded && restored->confirmedRevision == 2);
		PAGED_REQUIRE(fixture.gate.attempts == 1);
		const auto full = ReadUInkStoreFile(path);
		PAGED_REQUIRE(full.status == UInkReadStatus::Complete && full.document && full.document->canvases.size() == 4);
		for (uint8_t id = 1; id <= 4; ++id)
		{
			const auto& page = full.document->canvases[id - 1];
			PAGED_CHECK(page.pageGuid == Guid(id) && page.pageIndex == id - 1u && page.content.size() == 1);
			PAGED_REQUIRE(store.Load({ Guid(id), kSlot, 92u + id }) == PagedInkAdmission::Accepted);
		}
		store.Drain();
		const auto reads = TakeAll(store);
		constexpr std::array expectedX{ 10.0f, 22.0f, 32.0f, 41.0f };
		constexpr std::array<uint64_t, 4> revisions{ 1, 3, 2, 2 };
		PAGED_REQUIRE(reads.size() == expectedX.size());
		for (size_t i = 0; i < reads.size(); ++i)
		{
			const auto& read = reads[i];
			PAGED_REQUIRE(read.result == PagedInkResult::Succeeded && read.snapshot && read.snapshot->strokes.size() == 1);
			PAGED_CHECK(read.confirmedRevision == revisions[i] && HasPen(read.snapshot->strokes.front(), expectedX[i]));
		}

		// 整理重排物理版本后，已移除的坏段不得继续触发后续整理。
		PAGED_REQUIRE(store.Save(SavePage(Page(1, 0, 11), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto later = store.Take();
		PAGED_REQUIRE(later && later->result == PagedInkResult::Succeeded);
		PAGED_CHECK(fixture.gate.attempts == 1 && wake.count == 15 && !store.Take());
		// 覆写所有曾损坏的 GUID，使残留记录与最新版本的物理键全部失配。
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 23), 4)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto laterSecond = store.Take();
		PAGED_REQUIRE(laterSecond && laterSecond->result == PagedInkResult::Succeeded &&
			laterSecond->confirmedRevision == 4);
		PAGED_REQUIRE(store.Save(SavePage(Page(3, 2, 33), 3)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto laterThird = store.Take();
		PAGED_REQUIRE(laterThird && laterThird->result == PagedInkResult::Succeeded &&
			laterThird->confirmedRevision == 3);
		const auto supersededAfterCompact = seeds[0].pageBytes + replaced->pageBytes + restored->pageBytes;
		PAGED_CHECK(supersededAfterCompact < 8ull * 1024 * 1024);
		const auto after = CorruptPageFile::ReadBytes(path);
		PAGED_REQUIRE(after);
		PAGED_CHECK(fixture.gate.attempts == 1 && after->size() == laterThird->fileBytes &&
			wake.count == 17 && !store.Take());
	}

	void TestCompactClearsReusedDamageKey(TestState& test, const TempDirectory& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		CorruptCompactPages fixture;
		auto hook = fixture.Hook();
		hook.compactMinSupersededBytes = 1024;
		hook.compactMinSupersededPercent = 1;
		PagedInkStore store(Options(root, wake), hook);

		auto large = Page(1, 0, 10);
		for (uint32_t i = 1; i <= 32; ++i) large.strokes.push_back(Pen(10.0f + i, i));
		PAGED_REQUIRE(store.Save(SavePage(large, 1)) == PagedInkAdmission::Accepted);
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 20), 1)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto seeds = TakeAll(store);
		PAGED_REQUIRE(seeds.size() == 2 && seeds[0].result == PagedInkResult::Succeeded &&
			seeds[1].result == PagedInkResult::Succeeded &&
			seeds[0].pageBytes >= hook.compactMinSupersededBytes &&
			seeds[1].pageBytes > 0 && seeds[1].pageBytes < hook.compactMinSupersededBytes);
		const auto path = store.FilePath();
		const auto original = CorruptPageFile::ReadBytes(path);
		PAGED_REQUIRE(original && original->size() == seeds[1].fileBytes &&
			seeds[0].fileBytes + seeds[1].pageBytes == seeds[1].fileBytes);

		fixture.offsets[0] = seeds[0].fileBytes;
		PAGED_REQUIRE(store.Load({ Guid(2), kSlot, 100 }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto damaged = store.Take();
		PAGED_REQUIRE(damaged && damaged->result == PagedInkResult::Failed && damaged->error == PagedInkError::ReadFailed);
		PAGED_CHECK(fixture.gate.attempts == 0);

		// 按页序写入等长替代页，整理后页 2 仍在同一偏移，序号重新成为 1。
		PAGED_REQUIRE(store.Save(SavePage(Page(2, 1, 20), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto repaired = store.Take();
		PAGED_REQUIRE(repaired && repaired->result == PagedInkResult::Succeeded && repaired->confirmedRevision == 2 &&
			repaired->pageBytes == seeds[1].pageBytes);
		const auto compacted = CorruptPageFile::ReadBytes(path);
		PAGED_REQUIRE(compacted && compacted->size() == original->size() && fixture.gate.attempts == 1);
		const auto pageOffset = static_cast<size_t>(seeds[0].fileBytes);
		PAGED_CHECK(compacted->compare(pageOffset, std::string::npos, *original, pageOffset, std::string::npos) == 0);

		// 只覆写健康的大页，使常规整理达标，同时保留页 2 复用的物理键。
		PAGED_REQUIRE(store.Save(SavePage(std::move(large), 2)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedRevision == 2);
		PAGED_CHECK(seeds[0].pageBytes * 100 >= saved->fileBytes * hook.compactMinSupersededPercent);
		const auto finalBytes = CorruptPageFile::ReadBytes(path);
		PAGED_REQUIRE(finalBytes);
		PAGED_CHECK(fixture.gate.attempts == 2 && finalBytes->size() + seeds[0].pageBytes == saved->fileBytes &&
			log.Count(StoreLog::Level::Debug, "[UInkCompact] scene=whiteboard event=finish result=succeeded") == 2 &&
			log.Count(StoreLog::Level::Debug, "event=deferred reason=current_page_damaged") == 0 &&
			wake.count == 5 && !store.Take());
	}
#endif

	void SaveAndReadIndependentPage(TestState& test, PagedInkStore& store, uint64_t revision, float x)
	{
		PAGED_REQUIRE(store.Save(SavePage(PresentationPage(1, 0, x, 0, 101), revision)) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto saved = store.Take();
		PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && saved->confirmedRevision == revision);
		PAGED_REQUIRE(store.LoadLane({ 0, kSlot, revision }) == PagedInkAdmission::Accepted);
		store.Drain();
		const auto loaded = store.Take();
		PAGED_REQUIRE(loaded && loaded->result == PagedInkResult::Succeeded && loaded->lanePages.size() == 1);
		CheckLanePage(test, *loaded, { Guid(1), 0, 1, 101, 0 }, revision, x);
		PAGED_CHECK(!store.Take());
	}

	void LaunchPresentationWriter(TestState& test, const std::wstring& root)
	{
		std::array<wchar_t, 32768> executable{};
		const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
		PAGED_REQUIRE(length > 0 && length < executable.size());
		std::wstring command = L"\"" + std::wstring(executable.data(), length) +
			L"\" --paged-ink-store-presentation-child \"" + root + L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		PAGED_REQUIRE(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
		const std::unique_ptr<void, decltype(&CloseHandle)> child(process.hProcess, CloseHandle);
		CloseHandle(process.hThread);
		const auto waited = WaitForSingleObject(child.get(), 30000);
		if (waited != WAIT_OBJECT_0)
		{
			// 只终止本用例创建的子进程，之后临时目录才能安全清理。
			TerminateProcess(child.get(), 1);
			PAGED_CHECK(WaitForSingleObject(child.get(), 5000) == WAIT_OBJECT_0);
		}
		PAGED_REQUIRE(waited == WAIT_OBJECT_0);
		DWORD exitCode = 1;
		PAGED_CHECK(GetExitCodeProcess(child.get(), &exitCode) && exitCode == 0);
	}

	void TestPresentationIndependentWriters(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		PagedInkStore store(PresentationOptions(root, wake));
		SaveAndReadIndependentPage(test, store, 1, 10);
		const auto path = store.FilePath();
		const auto original = CorruptPageFile::ReadBytes(path);
		PAGED_REQUIRE(!path.empty() && original);

		// 父进程继续持有同一文稿的写入器，子进程以相同的页标识独立保存。
		LaunchPresentationWriter(test, root.Path());
		DirectoryContents files;
		PAGED_REQUIRE(ListDirectory(root.Path(), files) && files.files.size() == 2);
		const auto own = std::find(files.files.begin(), files.files.end(), path);
		PAGED_REQUIRE(own != files.files.end() && CorruptPageFile::ReadBytes(path) == original);
		const auto& other = files.files.front() == path ? files.files.back() : files.files.front();
		const auto foreign = CorruptPageFile::ReadBytes(other);
		const auto ownDocument = ReadUInkStoreFile(path);
		const auto otherDocument = ReadUInkStoreFile(other);
		PAGED_REQUIRE(foreign && ownDocument.status == UInkReadStatus::Complete && ownDocument.document &&
			otherDocument.status == UInkReadStatus::Complete && otherDocument.document);
		PAGED_CHECK(!ownDocument.document->header.guid.IsZero() && !otherDocument.document->header.guid.IsZero() &&
			ownDocument.document->header.guid != otherDocument.document->header.guid);
		SaveAndReadIndependentPage(test, store, 2, 30);
		PAGED_CHECK(store.FilePath() == path && CorruptPageFile::ReadBytes(other) == foreign);
		DirectoryContents final;
		PAGED_CHECK(ListDirectory(root.Path(), final) && final.files.size() == 2);
	}

	void TestPresentationNaming(TestState& test, const TempDirectory& root)
	{
		const PagedInkLocalTime time{ 2026, 1, 2, 3, 4, 5, 6 };
		const UInkGuid file({ 0x09, 0x87, 0x65, 0x43, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 8 });
		const auto prefix = root.Path() + L"\\presentation\\";
		const std::wstring suffix = L"_ab12cd34\\20260102-030405006_09876543.uink";
		const PagedInkFileNaming clean{ PagedInkFileKind::Presentation, L"甲<>:\"/\\|?*\x1f乙", Guid(9) };
		PAGED_CHECK(MakePagedInkPath(root.Path(), clean, time, file) == prefix + L"甲乙" + suffix);
		auto longName = clean;
		longName.presentationName = L"<" + std::wstring(55, L'甲');
		PAGED_CHECK(MakePagedInkPath(root.Path(), longName, time, file) == prefix + std::wstring(40, L'甲') + suffix);
		auto emptyName = clean;
		emptyName.presentationName.clear();
		PAGED_CHECK(MakePagedInkPath(root.Path(), emptyName, time, file) == prefix + L"untitled" + suffix);
		emptyName.presentationName = L"<>?*";
		PAGED_CHECK(MakePagedInkPath(root.Path(), emptyName, time, file) == prefix + L"untitled" + suffix);
		PAGED_CHECK(MakePagedInkPath(root.Path(), {}, time, file) ==
			root.Path() + L"\\whiteboard\\2026-01-02\\030405006_09876543.uink");
	}

	void TestPresentationReservedDeviceNames(TestState& test, const TempDirectory& root)
	{
		WakeCounter wake;
		const PagedInkLocalTime time{ 2026, 1, 2, 3, 4, 5, 6 };
		const auto prefix = root.Path() + L"\\presentation\\";
		constexpr std::pair<const wchar_t*, const wchar_t*> names[] = {
			{ L"NUL.deck", L"_NUL.deck_ab12cd34" },
			// key 落在首个点之前，最终组件 con_ab12cd34 已避开设备名。
			{ L"con", L"con_ab12cd34" },
			{ L"Com3.x", L"_Com3.x_ab12cd34" },
		};
		for (const auto& [name, directory] : names)
		{
			auto options = Options(root, wake);
			options.naming = { PagedInkFileKind::Presentation, name, Guid(9) };
			options.pageContext.workspaceType = 2;
			options.hostId = FormatUInkGuid(options.naming.presentationKey);
			const auto document = prefix + directory + L"\\";
			PAGED_CHECK(MakePagedInkPath(root.Path(), options.naming, time, Guid(8)) ==
				document + L"20260102-030405006_ab12cd34.uink");

			PagedInkStore store(std::move(options));
			auto page = Page(1, 0, 10);
			page.binding = 1;
			PAGED_REQUIRE(store.Save(SavePage(std::move(page), 1)) == PagedInkAdmission::Accepted);
			store.Drain();
			const auto saved = store.Take();
			PAGED_REQUIRE(saved && saved->result == PagedInkResult::Succeeded && !saved->file.failed);
			const auto path = store.FilePath();
			PAGED_REQUIRE(path.starts_with(document) && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
			PAGED_CHECK(ReadUInkStoreFile(path).status == UInkReadStatus::Complete);
		}
		PAGED_CHECK(wake.count == 3);
	}
}

int RunPagedInkStorePresentationChild(const wchar_t* root)
{
	TestState test{ "PresentationIndependentWriterChild" };
	WakeCounter wake;
	PagedInkStore store(PresentationOptions(std::wstring(root), wake));
	if (!PAGED_CHECK(store.LoadLane({ 0, kSlot, 1 }) == PagedInkAdmission::Accepted)) return 1;
	store.Drain();
	const auto empty = store.Take();
	if (!PAGED_CHECK(empty && empty->result == PagedInkResult::Succeeded && empty->lanePages.empty() &&
		store.FilePath().empty())) return 1;
	SaveAndReadIndependentPage(test, store, 1, 20);
	return test.failures == 0 ? 0 : 1;
}

int RunPagedInkSaveSnapshotLifetimeTests()
{
	constexpr std::pair<const char*, SaveSnapshotOutcome> cases[] = {
		{ "SaveSuccessSnapshotLifetime", SaveSnapshotOutcome::Success },
		{ "SaveFailureSnapshotLifetime", SaveSnapshotOutcome::FlushFailure },
		{ "SaveExceptionSnapshotLifetime", SaveSnapshotOutcome::WorkerException },
	};
	int failures = 0;
	for (const auto& [name, outcome] : cases)
	{
		TestState test{ name };
		TempDirectory root;
		if (PAGED_CHECK(!root.Path().empty())) TestSaveSnapshotLifetime(test, root, outcome);
		PAGED_CHECK(root.Clean());
		failures += test.failures;
	}
	TestState test{ "MergedSaveSnapshotLifetime" };
	TempDirectory root;
	if (PAGED_CHECK(!root.Path().empty())) TestMergedSaveSnapshotLifetime(test, root);
	PAGED_CHECK(root.Clean());
	return failures + test.failures;
}

int RunPagedInkStoreTests()
{
	constexpr std::pair<const char*, void (*)(TestState&, const TempDirectory&)> cases[] = {
		{ "AppendReplaceAndLoad", TestAppendReplaceAndLoad },
		{ "AppendStrokeContinuesPage", TestAppendStrokeContinuesPage },
		{ "AdoptRecoveredPages", TestAdoptRecoveredPages },
		{ "AppendRecoveryRegistrationFailures", TestAppendRecoveryRegistrationFailures },
		{ "ClearMemoryAndDiskIntervals", TestClearMemoryAndDiskIntervals },
		{ "UInkLogThreshold", TestUInkLogThreshold },
		{ "BoundaryMismatch", TestBoundaryMismatch },
		{ "StaleBoundaryBeforeTake", TestStaleBoundaryBeforeTake },
		{ "QueueOrdering", TestQueueOrdering },
		{ "CompletionAndCancellation", TestCompletionAndCancellation },
		{ "LoadWorkerException", TestLoadWorkerException },
#if defined(DRAW3_TESTING)
		{ "CompletionReservationAllocationFailure", TestCompletionReservationAllocationFailure },
#endif
		{ "CloseWaitsForSubmitWake", TestCloseWaitsForSubmitWake },
		{ "ConcurrentCloseWaitsForCanceledWake", TestConcurrentCloseWaitsForCanceledWake },
		{ "FirstCreateFlushFailureCanRetry", TestFirstCreateFlushFailureCanRetry },
		{ "PublishedReopenFailure", TestPublishedReopenFailure },
		{ "TruncateFailure", TestTruncateFailure },
		{ "RollbackFlushFailure", TestRollbackFlushFailure },
		{ "DegradedPage", TestDegradedPage },
		{ "StoredDegradedLaneLog", TestStoredDegradedLaneLog },
		{ "DegradedCurrentIntervalDoesNotAffectPrefix", TestDegradedCurrentIntervalDoesNotAffectPrefix },
		{ "ClearPrefixDegradationSurvivesFullSave", TestClearPrefixDegradationSurvivesFullSave },
		{ "SpliceExceedsSharedGeometryBudget", TestSpliceExceedsSharedGeometryBudget },
		{ "FootprintBudgetKeepsReadableClearPrefix", TestFootprintBudgetKeepsReadableClearPrefix },
		{ "FailedColdReadPreflight", TestFailedColdReadPreflight },
		{ "AppendAndEvictionAreMutuallyExclusive", TestAppendAndEvictionAreMutuallyExclusive },
		{ "EvictionTruthTable", TestEvictionTruthTable },
		{ "LoadLaneConfirmedMetadata", TestLoadLaneConfirmedMetadata },
		{ "FallbackPageOverwrite", TestFallbackPageOverwrite },
		{ "PresentationEndScreenClearRoundTrip", TestPresentationEndScreenClearRoundTrip },
		{ "LoadLanePendingWrite", TestLoadLanePendingWrite },
		{ "LoadLaneCompletionAndCancellation", TestLoadLaneCompletionAndCancellation },
		{ "ReadDegradedPageAndLane", TestReadDegradedPageAndLane },
		{ "ReadLogWithClippedDiagnostics", TestReadLogWithClippedDiagnostics },
		{ "LoadLaneIdentityMismatch", TestLoadLaneIdentityMismatch },
		{ "LoadRejectsMissingConfirmedClear", TestLoadRejectsMissingConfirmedClear },
		{ "LoadRejectsUnexpectedClear", TestLoadRejectsUnexpectedClear },
		{ "LoadRejectsTemporaryIdentity", TestLoadRejectsTemporaryIdentity },
		{ "SplicePrefixReadFailure", [](TestState& test, const TempDirectory& root)
			{ TestSpliceReadFailure(test, root, SpliceReadFailure::Open); } },
		{ "SplicePrefixValidationFailure", [](TestState& test, const TempDirectory& root)
			{ TestSpliceReadFailure(test, root, SpliceReadFailure::Validate); } },
		{ "SplicePrefixPostReadException", [](TestState& test, const TempDirectory& root)
			{ TestSpliceReadFailure(test, root, SpliceReadFailure::Exception); } },
		{ "SplicePrefixImportFailure", [](TestState& test, const TempDirectory& root)
			{ TestSpliceReadFailure(test, root, SpliceReadFailure::Import); } },
		{ "SpliceCompoundPrefixLog", TestSpliceCompoundPrefixLog },
		{ "SpliceRecoversDamagedPrefix", TestSpliceRecoversDamagedPrefix },
		{ "SpliceReplacesDamagedTail", TestSpliceReplacesDamagedTail },
		{ "CompactNeedsAbsoluteThreshold", [](TestState& test, const TempDirectory& root)
			{ TestCompactThresholds(test, root, 8ull * 1024 * 1024, 1, false); } },
		{ "CompactNeedsRelativeThreshold", [](TestState& test, const TempDirectory& root)
			{ TestCompactThresholds(test, root, 1, 100, false); } },
		{ "CompactMeetsBothThresholds", [](TestState& test, const TempDirectory& root)
			{ TestCompactThresholds(test, root, 1, 1, true); } },
		{ "CompactLanesAndClear", TestCompactLanesAndClear },
		{ "CompactAndEvictionAreMutuallyExclusive", TestCompactAndEvictionAreMutuallyExclusive },
		{ "CompactBusyAndClose", TestCompactBusyAndClose },
		{ "CompactOriginalReopenFailure", [](TestState& test, const TempDirectory& root)
			{ TestCompactReopenFailure(test, root, true); } },
		{ "CompactReplacementReopenFailure", [](TestState& test, const TempDirectory& root)
			{ TestCompactReopenFailure(test, root, false); } },
		{ "CompactRetryAfterSuccessfulWrite", TestCompactRetryAfterSuccessfulWrite },
		{ "CompactRepairsWhiteboardPageIndexMismatch", [](TestState& test, const TempDirectory& root)
			{ TestCompactRepairsPageIdentityMismatch(test, root, false); } },
		{ "CompactRepairsStableSlideIdMismatch", [](TestState& test, const TempDirectory& root)
			{ TestCompactRepairsPageIdentityMismatch(test, root, true); } },
		{ "CompactIgnoresLimitOnly", [](TestState& test, const TempDirectory& root)
			{ TestCompactDistinguishesReadLimitAndBadBlock(test, root, true, false); } },
		{ "CompactRepairsBadBlock", [](TestState& test, const TempDirectory& root)
			{ TestCompactDistinguishesReadLimitAndBadBlock(test, root, false, true); } },
		{ "CompactRepairsBadBlockDespiteLimit", [](TestState& test, const TempDirectory& root)
			{ TestCompactDistinguishesReadLimitAndBadBlock(test, root, true, true); } },
		{ "CompactIgnoresTemporaryReadFailureAndL3Fallback", TestCompactIgnoresTemporaryReadFailureAndL3Fallback },
#if defined(DRAW3_TESTING)
		{ "CompactRepairsSupersededDamage", TestCompactRepairsSupersededDamage },
		{ "CompactClearsReusedDamageKey", TestCompactClearsReusedDamageKey },
#endif
		{ "PresentationIndependentWriters", TestPresentationIndependentWriters },
		{ "PresentationNaming", TestPresentationNaming },
		{ "PresentationReservedDeviceNames", TestPresentationReservedDeviceNames },
	};
	int failures = RunPagedInkSaveSnapshotLifetimeTests();
	for (const auto& [name, run] : cases)
	{
		TestState test{ name };
		TempDirectory root;
		if (PAGED_CHECK(!root.Path().empty())) run(test, root);
		PAGED_CHECK(root.Clean());
		failures += test.failures;
	}
	std::cout << "PagedInkStore tests: " << failures << " failures\n";
	return failures;
}
