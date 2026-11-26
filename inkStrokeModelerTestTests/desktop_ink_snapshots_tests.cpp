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
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <semaphore>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import draw3.uink_store;
import Inkeys.Drawing.Draw3.desktop_ink_snapshots;

#if defined(DRAW3_TESTING)
void FailNextTestAllocation() noexcept;
bool CancelNextTestAllocation() noexcept;
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
				std::cerr << "DesktopInkSnapshots FAILED " << name << ':' << line << ": " << expression << '\n';
			}
			return condition;
		}
	};

#define DESKTOP_CHECK(...) test.Check(!!(__VA_ARGS__), #__VA_ARGS__, __LINE__)
#define DESKTOP_REQUIRE(...) do { if (!DESKTOP_CHECK(__VA_ARGS__)) return; } while (false)


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

	bool RemoveFixtureTree(const std::wstring& path)
	{
		WIN32_FIND_DATAW entry{};
		const auto find = FindFirstFileW((path + L"\\*").c_str(), &entry);
		if (find == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
		if (find != INVALID_HANDLE_VALUE)
		{
			const std::unique_ptr<void, decltype(&FindClose)> listing(find, FindClose);
			do
			{
				const std::wstring_view name(entry.cFileName);
				if (name == L"." || name == L"..") continue;
				const auto child = path + L"\\" + entry.cFileName;
				if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					// 清理限于测试创建的唯一目录，不跟随重解析点。
					if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
					{
						if (!RemoveDirectoryW(child.c_str())) return false;
					}
					else if (!RemoveFixtureTree(child)) return false;
				}
				else if (!DeleteFileW(child.c_str())) return false;
			} while (FindNextFileW(listing.get(), &entry));
			if (GetLastError() != ERROR_NO_MORE_FILES) return false;
		}
		return RemoveDirectoryW(path.c_str()) != FALSE;
	}

	struct TempRoot
	{
		std::wstring path;

		TempRoot()
		{
			std::array<wchar_t, 32768> temp{};
			const auto length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
			const auto id = CreateUInkGuid();
			if (length == 0 || length >= temp.size() || !id) return;
			const auto name = FormatUInkGuid(*id);
			auto candidate = std::wstring(temp.data(), length) + L"Inkeys-DesktopInkSnapshots-" +
				std::wstring(name.begin(), name.end());
			if (CreateDirectoryW(candidate.c_str(), nullptr)) path.swap(candidate);
		}

		~TempRoot() { (void)Clean(); }

		bool Clean() noexcept
		{
			try
			{
				if (path.empty()) return true;
				if (!RemoveFixtureTree(path)) return false;
				path.clear();
				return true;
			}
			catch (...) { return false; }
		}
	};

	struct WakeCounter
	{
		std::atomic<unsigned> count = 0;
		std::counting_semaphore<64> arrived{ 0 };

		static void Wake(void* context) noexcept
		{
			auto& self = *static_cast<WakeCounter*>(context);
			++self.count;
			self.arrived.release();
		}

		bool Wait(std::chrono::milliseconds timeout = 10s) { return arrived.try_acquire_for(timeout); }
	};

	UInkGuid Guid(uint8_t id)
	{
		return UInkGuid({ 0xab, 0x12, 0xcd, 0x34, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, id });
	}

	const UInkGuid kPage = Guid(0xf0);
	constexpr uint64_t kClear = 11;

	Draw3UInkStrokeSnapshot Pen(float x, uint32_t group = 0)
	{
		Draw3UInkStrokeSnapshot stroke;
		stroke.style.fallbackRgb = 0x123456;
		stroke.points = {{ x, 10, 4 }, { x, 10, 7 }, { x + 1, 10, 5 }};
		stroke.undoId = group;
		return stroke;
	}

	std::shared_ptr<Draw3UInkCanvasSnapshot> Page(float x)
	{
		auto page = std::make_shared<Draw3UInkCanvasSnapshot>();
		page->pageGuid = kPage;
		page->pageNumber = 1;
		page->strokes.push_back(Pen(x));
		return page;
	}

	DesktopInkIdentity Identity(uint8_t request)
	{
		return { Guid(request), kClear, kPage };
	}

	bool IsReceipt(const DesktopInkCompletion& completion, uint8_t id, DesktopInkOperation operation)
	{
		return completion.identity.requestId.Bytes() == Guid(id).Bytes() &&
			completion.identity.pageGuid.Bytes() == kPage.Bytes() &&
			completion.identity.clearGeneration == kClear && completion.operation == operation;
	}

	bool TruncateLastByte(const std::wstring& path)
	{
		const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		const std::unique_ptr<void, decltype(&CloseHandle)> handle(file, CloseHandle);
		LARGE_INTEGER back{};
		back.QuadPart = -1;
		return SetFilePointerEx(handle.get(), back, nullptr, FILE_END) && SetEndOfFile(handle.get());
	}

	bool ReplaceBytes(const std::wstring& path, std::span<const std::byte> bytes)
	{
		const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		const std::unique_ptr<void, decltype(&CloseHandle)> handle(file, CloseHandle);
		DWORD bytesWritten = 0;
		return WriteFile(handle.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, nullptr) &&
			bytesWritten == bytes.size();
	}


	bool BytesMatch(const std::wstring& path, std::span<const std::byte> expected)
	{
		const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		const std::unique_ptr<void, decltype(&CloseHandle)> handle(file, CloseHandle);
		std::vector<std::byte> bytes(expected.size());
		LARGE_INTEGER length{};
		DWORD bytesRead = 0;
		return GetFileSizeEx(handle.get(), &length) && length.QuadPart == static_cast<LONGLONG>(bytes.size()) &&
			ReadFile(handle.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &bytesRead, nullptr) &&
			bytesRead == bytes.size() && std::equal(bytes.begin(), bytes.end(), expected.begin());
	}

	bool ReadPublishedStroke(const std::wstring& path, uint8_t request, float x, size_t pointCount = 3)
	{
		const auto read = ReadUInkStoreFile(path);
		if (read.status != UInkReadStatus::Complete || !read.document ||
			read.document->header.guid != Guid(request) || read.document->canvases.size() != 1) return false;
		const auto& canvas = read.document->canvases.front();
		if (canvas.pageGuid != kPage || canvas.content.size() != 1) return false;
		const auto* ink = std::get_if<UInkInk>(&canvas.content.front());
		return ink && ink->points.size() == pointCount && ink->points.front().x == x;
	}

	DesktopInkSnapshotsTestHook::LocalTime FixedLocalTime(void*, const DesktopInkIdentity&) noexcept
	{
		return { 2026, 9, 1, 12, 34, 56, 789 };
	}

	DesktopInkSnapshotsTestHook::LocalTime MidnightBoundary(void*, const DesktopInkIdentity& identity) noexcept
	{
		const auto request = identity.requestId.Bytes().back();
		if (request <= 2)
			return { 2026, 9, 1, 23, 59, 59, static_cast<uint16_t>(997 + request) };
		return { 2026, 9, 2, 0, 0, 0, static_cast<uint16_t>(request - 3) };
	}

	struct WorkGate
	{
		uint8_t requestId = 2;
		std::binary_semaphore entered{ 0 };
		std::binary_semaphore resume{ 0 };

		static void BeforeWork(void* context, DesktopInkOperation, const DesktopInkIdentity& identity)
		{
			auto& self = *static_cast<WorkGate*>(context);
			if (identity.requestId.Bytes() != Guid(self.requestId).Bytes()) return;
			self.entered.release();
			self.resume.acquire();
		}
	};

	struct ReleaseGate
	{
		WorkGate* gate;
		void Release()
		{
			if (!gate) return;
			gate->resume.release();
			gate = nullptr;
		}
		~ReleaseGate() { Release(); }
	};


	void StartAndRejectEmptyWithoutCreatingDirectories(TestState& test, const TempRoot& root)
	{
		WakeCounter wake;
		DesktopInkSnapshots service;
		const auto missingRoot = root.path + LR"(\not-created)";
		DESKTOP_REQUIRE(service.Start({ missingRoot, 1, WakeCounter::Wake, &wake }));
		DESKTOP_CHECK(GetFileAttributesW(missingRoot.c_str()) == INVALID_FILE_ATTRIBUTES);
		auto empty = Page(1);
		empty->strokes.clear();
		DESKTOP_CHECK(service.Save({ Identity(1), empty }) == DesktopInkAdmission::InvalidRequest);
		DESKTOP_CHECK(service.Save({ Identity(1), {} }) == DesktopInkAdmission::InvalidRequest);
		service.Close();
		const auto stats = service.Diagnostics();
		DESKTOP_CHECK(GetFileAttributesW(missingRoot.c_str()) == INVALID_FILE_ATTRIBUTES &&
			stats.accepted == 0 && stats.pending == 0 && wake.count == 0 && !service.Take());
	}

	void ConcurrentCollision(TestState& test, const TempRoot& root)
	{
		const auto directory = root.path + LR"(\desktop)";
		const auto date = directory + LR"(\2026-09-01)";
		DESKTOP_REQUIRE(CreateDirectoryW(directory.c_str(), nullptr) && CreateDirectoryW(date.c_str(), nullptr));
		const auto stem = date + LR"(\123456789_ab12cd34)";
		const auto occupied = stem + L".uink";
		constexpr std::array sentinel{ std::byte{0x11}, std::byte{0x22}, std::byte{0x33} };
		DESKTOP_REQUIRE(ReplaceBytes(occupied, sentinel));

		WakeCounter firstWake, secondWake;
		WorkGate firstGate{ 2 }, secondGate{ 3 };
		DesktopInkSnapshots first, second;
		ReleaseGate releaseFirst{ &firstGate }, releaseSecond{ &secondGate };
		DESKTOP_REQUIRE(first.Start({ root.path, 1, WakeCounter::Wake, &firstWake },
			{ .beforeWork = WorkGate::BeforeWork, .context = &firstGate, .sampleLocalTime = FixedLocalTime }));
		DESKTOP_REQUIRE(second.Start({ root.path, 1, WakeCounter::Wake, &secondWake },
			{ .beforeWork = WorkGate::BeforeWork, .context = &secondGate, .sampleLocalTime = FixedLocalTime }));
		DESKTOP_REQUIRE(first.Save({ Identity(2), Page(20) }) == DesktopInkAdmission::Accepted &&
			second.Save({ Identity(3), Page(30) }) == DesktopInkAdmission::Accepted);
		DESKTOP_REQUIRE(firstGate.entered.try_acquire_for(10s) && secondGate.entered.try_acquire_for(10s));
		// 两个 worker 使用同一本地时间和 GUID 前缀，已有同名文件必须保留。
		releaseFirst.Release();
		releaseSecond.Release();
		DESKTOP_REQUIRE(firstWake.Wait() && secondWake.Wait());
		const auto a = first.Take();
		const auto b = second.Take();
		DESKTOP_REQUIRE(a && b && IsReceipt(*a, 2, DesktopInkOperation::Save) && IsReceipt(*b, 3, DesktopInkOperation::Save) &&
			a->result == DesktopInkResult::Succeeded && b->result == DesktopInkResult::Succeeded &&
			a->publishedPath && b->publishedPath);
		const auto suffix1 = stem + L"_1.uink";
		const auto suffix2 = stem + L"_2.uink";
		DESKTOP_CHECK((*a->publishedPath == suffix1 && *b->publishedPath == suffix2) ||
			(*a->publishedPath == suffix2 && *b->publishedPath == suffix1));
		DESKTOP_CHECK(BytesMatch(occupied, sentinel) && ReadPublishedStroke(*a->publishedPath, 2, 20) &&
			ReadPublishedStroke(*b->publishedPath, 3, 30));
		DESKTOP_CHECK(first.Diagnostics().committed == 1 && second.Diagnostics().committed == 1);

		// 冷读必须使用发布回执里的实际后缀，不能重新拼接被占用的基础路径。
		DESKTOP_REQUIRE(first.Load({ Identity(4), Guid(2) }) == DesktopInkAdmission::Accepted &&
			second.Load({ Identity(5), Guid(3) }) == DesktopInkAdmission::Accepted &&
			firstWake.Wait() && secondWake.Wait());
		const auto loadedA = first.Take();
		const auto loadedB = second.Take();
		DESKTOP_REQUIRE(loadedA && loadedB && IsReceipt(*loadedA, 4, DesktopInkOperation::Load) &&
			IsReceipt(*loadedB, 5, DesktopInkOperation::Load) &&
			loadedA->result == DesktopInkResult::Succeeded && loadedB->result == DesktopInkResult::Succeeded &&
			loadedA->snapshot && loadedB->snapshot &&
			loadedA->snapshot->strokes.size() == 1 && loadedB->snapshot->strokes.size() == 1 &&
			loadedA->snapshot->strokes.front().points.size() == 3 && loadedB->snapshot->strokes.front().points.size() == 3);
		DESKTOP_CHECK(loadedA->snapshot->strokes.front().points.front().x == 20 &&
			loadedB->snapshot->strokes.front().points.front().x == 30 && !first.Take() && !second.Take());
	}

	SnapshotLifetimeProbe::Range ObservePage(const Draw3UInkCanvasSnapshot& page)
	{
		using namespace SnapshotLifetimeProbe;
		const auto first = Count();
		WatchVector(page.strokes);
		WatchVector(page.operations);
		for (const auto& stroke : page.strokes) WatchVector(stroke.points);
		for (const auto& operation : page.operations)
			if (const auto* stroke = std::get_if<Draw3UInkStrokeSnapshot>(&operation)) WatchVector(stroke->points);
		return { first, Count() - first };
	}

	std::shared_ptr<Draw3UInkCanvasSnapshot> LargePage(float x)
	{
		auto page = Page(x);
		auto& points = page->strokes.front().points;
		points.resize(131072);
		for (size_t i = 0; i < points.size(); ++i)
			points[i] = { x + static_cast<float>(i % 1024), static_cast<float>(i / 1024), 3.0f };
		page->operations.emplace_back(page->strokes.front());
		return page;
	}

	struct ScopedWriteFault
	{
		explicit ScopedWriteFault(bool shortWrite) { SetUInkStoreTestFaultInjection({ .shortWrite = shortWrite }); }
		~ScopedWriteFault() { ResetUInkStoreTestFaultInjection(); }
	};

	void SaveBufferLifetime(TestState& test, const TempRoot& root, bool failWrite)
	{
		using namespace SnapshotLifetimeProbe;
		Scope observation;
		WakeCounter wake;
		WorkGate gate;
		DesktopInkSnapshots service;
		ReleaseGate release{ &gate };
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake },
			{ .beforeWork = WorkGate::BeforeWork, .context = &gate, .sampleLocalTime = FixedLocalTime }));

		// 已发布的文件即使后来撞名并写失败，也必须保持原内容。
		const auto base = root.path + LR"(\desktop\2026-09-01\123456789_ab12cd34)";
		if (failWrite)
		{
			DESKTOP_REQUIRE(service.Save({ Identity(1), Page(10) }) == DesktopInkAdmission::Accepted && wake.Wait());
			const auto initial = service.Take();
			DESKTOP_REQUIRE(initial && initial->result == DesktopInkResult::Succeeded && initial->publishedPath &&
				*initial->publishedPath == base + L".uink");
		}

		auto page = LargePage(12);
		const auto watched = ObservePage(*page);
		DESKTOP_REQUIRE(Read(watched).bytes >= 3 * 1024 * 1024);
		DESKTOP_REQUIRE(service.Save({ Identity(2), std::move(page) }) == DesktopInkAdmission::Accepted);
		DESKTOP_REQUIRE(gate.entered.try_acquire_for(10s));
		DESKTOP_CHECK(Read(watched).freedBuffers == 0);
		{
			const ScopedWriteFault fault(failWrite);
			release.Release();
			DESKTOP_REQUIRE(wake.Wait(30s));
		}

		// 收据仍未取走，服务尚能接受后续请求，此时须释放真实快照缓冲。
		DESKTOP_CHECK(Released(watched) && service.Diagnostics().pending == 0);
		Report(test.name, watched);
		const auto receipt = service.Take();
		DESKTOP_REQUIRE(receipt && IsReceipt(*receipt, 2, DesktopInkOperation::Save) &&
			!receipt->snapshot && !service.Take());
		if (failWrite)
		{
			DESKTOP_CHECK(receipt->result == DesktopInkResult::Failed && receipt->error == DesktopInkError::PublishFailed &&
				receipt->systemError == ERROR_WRITE_FAULT && !receipt->publishedPath &&
				service.Diagnostics().failed == 1 && service.Diagnostics().committed == 1);
			DESKTOP_CHECK(GetFileAttributesW((base + L"_1.uink").c_str()) == INVALID_FILE_ATTRIBUTES &&
				ReadPublishedStroke(base + L".uink", 1, 10));
			DESKTOP_CHECK(service.Load({ Identity(3), Guid(2) }) == DesktopInkAdmission::MissingSave);
			DESKTOP_REQUIRE(service.Save({ Identity(4), Page(40) }) == DesktopInkAdmission::Accepted && wake.Wait());
			const auto retry = service.Take();
			DESKTOP_REQUIRE(retry && IsReceipt(*retry, 4, DesktopInkOperation::Save) &&
				retry->result == DesktopInkResult::Succeeded && retry->publishedPath && *retry->publishedPath == base + L"_1.uink");
			DESKTOP_CHECK(ReadPublishedStroke(*retry->publishedPath, 4, 40) && ReadPublishedStroke(base + L".uink", 1, 10) &&
				!service.Take());
			return;
		}

		DESKTOP_REQUIRE(receipt->result == DesktopInkResult::Succeeded && receipt->publishedPath &&
			*receipt->publishedPath == base + L".uink");
		DESKTOP_REQUIRE(service.Load({ Identity(3), Guid(2) }) == DesktopInkAdmission::Accepted && wake.Wait(30s));
		auto loaded = service.Take();
		DESKTOP_REQUIRE(loaded && IsReceipt(*loaded, 3, DesktopInkOperation::Load) &&
			loaded->result == DesktopInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1 && loaded->snapshot->strokes.front().points.size() == 131072);
		const auto loadedBuffers = ObservePage(*loaded->snapshot);
		{
			// 新回执按值移交快照，最后一个消费者离开作用域时释放冷读缓冲。
			const auto consumer = std::move(*loaded->snapshot);
			loaded.reset();
			DESKTOP_CHECK(!Released(loadedBuffers) && Read(loadedBuffers).freedBuffers == 0 &&
				consumer.strokes.front().points.size() == 131072);
		}
		DESKTOP_CHECK(Released(loadedBuffers) && !service.Take());
		Report("desktop-load-last-consumer", loadedBuffers);
	}

	void RoundTripAndLifetime(TestState& test, const TempRoot& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		WorkGate gate;
		DesktopInkSnapshots service;
		ReleaseGate release{ &gate };
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake },
			{ WorkGate::BeforeWork, &gate }));
		DESKTOP_CHECK(GetFileAttributesW((root.path + L"\\desktop").c_str()) == INVALID_FILE_ATTRIBUTES);
		auto large = Page(12);
		auto& points = large->strokes.front().points;
		for (size_t i = 0; i < 65536; ++i) points.push_back({ 13 + static_cast<float>(i), 10, 5 });
		const std::weak_ptr<const Draw3UInkCanvasSnapshot> lifetime = large;
		DESKTOP_REQUIRE(service.Save({ Identity(2), std::move(large) }) == DesktopInkAdmission::Accepted);
		DESKTOP_REQUIRE(gate.entered.try_acquire_for(10s));
		DESKTOP_REQUIRE(service.Save({ Identity(3), Page(30) }) == DesktopInkAdmission::Accepted);
		DESKTOP_CHECK(!lifetime.expired() && service.Diagnostics().pending == 2);
		DESKTOP_CHECK(service.Save({ Identity(3), Page(31) }) == DesktopInkAdmission::InvalidRequest);

		release.Release();
		DESKTOP_REQUIRE(wake.Wait() && wake.Wait());
		// 两个终态都尚未消费，worker 也已释放头一个任务的大快照。
		DESKTOP_CHECK(lifetime.expired() && service.Diagnostics().committed == 2);
		const auto first = service.Take();
		const auto second = service.Take();
		DESKTOP_REQUIRE(first && second && IsReceipt(*first, 2, DesktopInkOperation::Save) &&
			IsReceipt(*second, 3, DesktopInkOperation::Save));
		DESKTOP_REQUIRE(first->result == DesktopInkResult::Succeeded && second->result == DesktopInkResult::Succeeded &&
			first->publishedPath && second->publishedPath && *first->publishedPath != *second->publishedPath);
		DESKTOP_CHECK(!first->snapshot && !second->snapshot && !first->degraded && !second->degraded);

		const auto prefix = root.path + L"\\desktop\\";
		DESKTOP_REQUIRE(first->publishedPath->starts_with(prefix));
		const auto relative = std::wstring_view(*first->publishedPath).substr(prefix.size());
		DESKTOP_CHECK(relative.size() >= 34 &&
			relative[4] == L'-' && relative[7] == L'-' && relative[10] == L'\\' &&
			relative.substr(20, 9) == L"_ab12cd34" && relative.ends_with(L".uink"));

		uint64_t canvasCount = 0;
		const auto read = ReadUInkStoreFile(*first->publishedPath, {}, &canvasCount);
		DESKTOP_REQUIRE(read.status == UInkReadStatus::Complete && read.document &&
			read.summary.version.value_or(0) == kUInkVersion && canvasCount == 1 && read.document->canvases.size() == 1);
		const auto& canvas = read.document->canvases.front();
		DESKTOP_CHECK(read.document->header.guid.Bytes() == Guid(2).Bytes() &&
			read.document->usesImplicitDevice && read.document->usesImplicitWorkspace &&
			!read.document->headerExtension && canvas.pageGuid.Bytes() == kPage.Bytes() &&
			canvas.pageIndex == 0 && canvas.pageNumber == 1 && canvas.layerIndex == 0 &&
			canvas.content.size() == 1 && std::holds_alternative<UInkInk>(canvas.content.front()));

		DESKTOP_REQUIRE(service.Load({ Identity(4), Guid(2) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto loaded = service.Take();
		DESKTOP_REQUIRE(loaded && IsReceipt(*loaded, 4, DesktopInkOperation::Load) &&
			loaded->result == DesktopInkResult::Succeeded && !loaded->degraded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1);
		DESKTOP_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Debug,
			"[UInkRead] scene=desktop op=load page=1 result=complete skippedBlocks=0") == 1 &&
			log.Count(StoreLog::Level::Debug, " strokes=1 ") == 1);
		const auto& restored = loaded->snapshot->strokes.front().points;
		DESKTOP_REQUIRE(restored.size() == 65539);
		DESKTOP_CHECK(restored[0].x == 12 && restored[0].width == 4 && restored[1].x == 12 &&
			restored[1].width == 7 && restored.back().x == 65548 && !service.Take());
	}

	void RejectFailuresAndCatchExceptions(TestState& test, const TempRoot& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		DesktopInkSnapshots service;
		const DesktopInkSnapshotsTestHook hook{
			[](void*, DesktopInkOperation, const DesktopInkIdentity& identity)
			{
				if (identity.requestId.Bytes() == Guid(2).Bytes() ||
					identity.requestId.Bytes() == Guid(5).Bytes()) throw std::runtime_error("desktop test");
			}
		};
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake }, hook));
		DESKTOP_CHECK(service.Save({ {}, Page(1) }) == DesktopInkAdmission::InvalidRequest);
		DESKTOP_CHECK(service.Load({ Identity(7), Guid(1) }) == DesktopInkAdmission::MissingSave);
#if defined(DRAW3_TESTING)
		auto unaccepted = Page(1);
		FailNextTestAllocation();
		const auto failedAdmission = service.Save({ Identity(7), std::move(unaccepted) });
		const bool allocationFailed = !CancelNextTestAllocation();
		DESKTOP_CHECK(failedAdmission == DesktopInkAdmission::ReservationFailed && allocationFailed &&
			wake.count == 0 && !service.Take() && service.Diagnostics().accepted == 0);
#endif

		const auto retained = Page(1);
		SetUInkStoreTestFaultInjection({ .shortWrite = true });
		const bool writeFinished = service.Save({ Identity(1), retained }) == DesktopInkAdmission::Accepted && wake.Wait();
		ResetUInkStoreTestFaultInjection();
		DESKTOP_REQUIRE(writeFinished);
		const auto failed = service.Take();
		DESKTOP_REQUIRE(failed && IsReceipt(*failed, 1, DesktopInkOperation::Save));
		DESKTOP_CHECK(failed->result == DesktopInkResult::Failed && failed->error == DesktopInkError::PublishFailed &&
			failed->systemError != 0 && !failed->publishedPath && !failed->snapshot && retained.use_count() == 1);
		DESKTOP_CHECK(service.Load({ Identity(7), Guid(1) }) == DesktopInkAdmission::MissingSave);
		DESKTOP_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Error,
			"[UInkWrite] scene=desktop op=publish page=1 result=failed stage=create.header systemError=29") == 1);
		DESKTOP_CHECK(log.Count(StoreLog::Level::Error, " sealed=0 tailUnconfirmed=0") == 1);

		DESKTOP_REQUIRE(service.Save({ Identity(2), Page(2) }) == DesktopInkAdmission::Accepted &&
			service.Save({ Identity(3), Page(3) }) == DesktopInkAdmission::Accepted && wake.Wait() && wake.Wait());
		const auto thrown = service.Take();
		const auto saved = service.Take();
		DESKTOP_REQUIRE(thrown && saved && IsReceipt(*thrown, 2, DesktopInkOperation::Save) &&
			IsReceipt(*saved, 3, DesktopInkOperation::Save) && saved->result == DesktopInkResult::Succeeded);
		DESKTOP_CHECK(thrown->result == DesktopInkResult::Failed && thrown->error == DesktopInkError::Exception);
#if defined(DRAW3_TESTING)
		FailNextTestAllocation();
		const auto failedLoad = service.Load({ Identity(7), Guid(3) });
		const bool allocationFailedForLoad = !CancelNextTestAllocation();
		DESKTOP_CHECK(failedLoad == DesktopInkAdmission::ReservationFailed && allocationFailedForLoad &&
			wake.count == 3 && !service.Take());
#endif
		auto wrongGeneration = Identity(5);
		++wrongGeneration.clearGeneration;
		DESKTOP_CHECK(service.Load({ wrongGeneration, Guid(3) }) == DesktopInkAdmission::InvalidRequest);
		DESKTOP_REQUIRE(service.Load({ Identity(5), Guid(3) }) == DesktopInkAdmission::Accepted &&
			service.Load({ Identity(6), Guid(3) }) == DesktopInkAdmission::Accepted && wake.Wait() && wake.Wait());
		const auto thrownLoad = service.Take();
#if defined(DRAW3_TESTING)
		FailNextTestAllocation();
#endif
		const auto loaded = service.Take();
#if defined(DRAW3_TESTING)
		DESKTOP_CHECK(CancelNextTestAllocation());
#endif
		DESKTOP_CHECK(thrownLoad && IsReceipt(*thrownLoad, 5, DesktopInkOperation::Load) &&
			thrownLoad->result == DesktopInkResult::Failed && thrownLoad->error == DesktopInkError::Exception);
		DESKTOP_CHECK(loaded && IsReceipt(*loaded, 6, DesktopInkOperation::Load) &&
			loaded->result == DesktopInkResult::Succeeded && loaded->snapshot && !service.Take());
		const auto stats = service.Diagnostics();
		DESKTOP_CHECK(stats.accepted == 3 && stats.committed == 1 && stats.failed == 2 && stats.pending == 0);
	}

	void DegradedAndInvalidReads(TestState& test, const TempRoot& root)
	{
		ScopedUInkLog log;
		WakeCounter wake;
		DesktopInkSnapshots service;
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake }));
		auto page = Page(10);
		page->strokes.push_back(Pen(80, 1));
		DESKTOP_REQUIRE(service.Save({ Identity(1), page }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto saved = service.Take();
		DESKTOP_REQUIRE(saved && saved->result == DesktopInkResult::Succeeded && saved->publishedPath);
		const auto& path = *saved->publishedPath;

		DESKTOP_REQUIRE(TruncateLastByte(path));
		DESKTOP_REQUIRE(service.Load({ Identity(2), Guid(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto partial = service.Take();
		DESKTOP_REQUIRE(partial && IsReceipt(*partial, 2, DesktopInkOperation::Load) &&
			partial->result == DesktopInkResult::Succeeded && partial->snapshot && partial->degraded);
		DESKTOP_CHECK(partial->snapshot->strokes.size() == 1 &&
			partial->snapshot->strokes.front().points.front().x == 10 && !partial->readDiagnostics.empty());

		const auto header = MakeDraw3UInkV11FileHeader(Guid(1));
		auto exportPage = ExportDraw3PageToUInkV11(*page);
		DESKTOP_REQUIRE(header.status == Draw3UInkExportStatus::Success &&
			exportPage.status == Draw3UInkExportStatus::Success && exportPage.canvas);
		auto replacement = header.header;
		replacement.pageNum = 1;
		auto prefix = EncodeUInkHeaderPrefix(replacement, header.headerExtension);
		DESKTOP_REQUIRE(!exportPage.canvas->content.empty());
		const auto canvasBytes = EncodeUInkPage(*exportPage.canvas, true, true);
		DESKTOP_REQUIRE(prefix.status == UInkEncodeStatus::Success && canvasBytes.status == UInkEncodeStatus::Success);

		// 非圆端帽是合法的 UInk，Draw3 的近似只影响日志等级。
		auto withCap = *exportPage.canvas;
		auto* firstInk = std::get_if<UInkInk>(&withCap.content.front());
		DESKTOP_REQUIRE(firstInk);
		firstInk->cap = 1;
		const auto withCapBytes = EncodeUInkPage(withCap, true, true);
		DESKTOP_REQUIRE(withCapBytes.status == UInkEncodeStatus::Success);
		auto withCapFile = prefix.bytes;
		withCapFile.insert(withCapFile.end(), withCapBytes.bytes.begin(), withCapBytes.bytes.end());
		DESKTOP_REQUIRE(ReplaceBytes(path, withCapFile));
		const auto exactRead = ReadUInkStoreFile(path);
		DESKTOP_REQUIRE(exactRead.summary.outcome == UInkReadOutcome::Complete);
		DESKTOP_REQUIRE(service.Load({ Identity(7), Guid(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto approximated = service.Take();
		DESKTOP_REQUIRE(approximated && approximated->result == DesktopInkResult::Succeeded &&
			approximated->snapshot && !approximated->degraded && approximated->fallbackCounts.cap == 1);
		DESKTOP_CHECK(log.Count(StoreLog::Level::Warn, " l3Offset=unknown l3.cap=1") == 1);

		prefix.bytes.insert(prefix.bytes.end(), canvasBytes.bytes.begin(), canvasBytes.bytes.end());
		const auto singlePageSize = prefix.bytes.size();
		prefix.bytes.insert(prefix.bytes.end(), canvasBytes.bytes.begin(), canvasBytes.bytes.end());
		DESKTOP_REQUIRE(ReplaceBytes(path, prefix.bytes));

		// 同一 Canvas 的两个版本会被通用读者去重，但桌面快照必须只有一个版本。
		DESKTOP_REQUIRE(service.Load({ Identity(3), Guid(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto duplicate = service.Take();
		DESKTOP_CHECK(duplicate && duplicate->result == DesktopInkResult::Failed &&
			duplicate->error == DesktopInkError::InvalidFile && !duplicate->snapshot);

		// 完整的 MessagePack 对象，type 可识别，但 Canvas 的 extra 字段不是 map。
		constexpr std::byte invalidCanvas[] = {
			std::byte{0x82}, std::byte{0xa4}, std::byte{'t'}, std::byte{'y'}, std::byte{'p'}, std::byte{'e'},
			std::byte{0xcd}, static_cast<std::byte>(kCanvasType >> 8), static_cast<std::byte>(kCanvasType & 0xff),
			std::byte{0xa5}, std::byte{'e'}, std::byte{'x'}, std::byte{'t'}, std::byte{'r'}, std::byte{'a'}, std::byte{1},
		};
		prefix.bytes.resize(singlePageSize);
		prefix.bytes.insert(prefix.bytes.end(), std::begin(invalidCanvas), std::end(invalidCanvas));
		DESKTOP_REQUIRE(ReplaceBytes(path, prefix.bytes));
		uint64_t recognizedCanvasCount = 0;
		const auto read = ReadUInkStoreFile(path, {}, &recognizedCanvasCount);
		DESKTOP_CHECK(read.document && read.document->canvases.size() == 1 && recognizedCanvasCount == 2);
		DESKTOP_REQUIRE(service.Load({ Identity(6), Guid(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto invalid = service.Take();
		DESKTOP_CHECK(invalid && IsReceipt(*invalid, 6, DesktopInkOperation::Load) &&
			invalid->result == DesktopInkResult::Failed && invalid->error == DesktopInkError::InvalidFile && !invalid->snapshot);

		DESKTOP_REQUIRE(DeleteFileW(path.c_str()));
		exportPage.canvas->pageGuid = Guid(99);
		const auto wrongPage = PublishUInkSnapshotFile(path, header.header, header.headerExtension, *exportPage.canvas);
		DESKTOP_REQUIRE(wrongPage.success && wrongPage.publishedPath == path);
		DESKTOP_REQUIRE(service.Load({ Identity(4), Guid(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto wrong = service.Take();
		DESKTOP_CHECK(wrong && wrong->result == DesktopInkResult::Failed && wrong->error == DesktopInkError::InvalidFile &&
			!wrong->snapshot);

		DESKTOP_REQUIRE(DeleteFileW(path.c_str()));
		DESKTOP_REQUIRE(service.Load({ Identity(5), Guid(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto missing = service.Take();
		DESKTOP_CHECK(missing && missing->result == DesktopInkResult::Failed && missing->error == DesktopInkError::ReadFailed &&
			missing->systemError == ERROR_FILE_NOT_FOUND && !missing->snapshot && !service.Take());
		DESKTOP_CHECK(log.Count(StoreLog::Level::Warn, " readMs=") == 2 &&
			log.Count(StoreLog::Level::Warn, " importMs=") == 2 &&
			log.Count(StoreLog::Level::Error, " readMs=") == 4 &&
			log.Count(StoreLog::Level::Error, " importMs=") == 0);
		DESKTOP_CHECK(log.rows.size() == 6 && log.Count(StoreLog::Level::Warn,
			"[UInkRead] scene=desktop op=load page=1 result=degraded") == 2 &&
			log.Count(StoreLog::Level::Warn, " stop=l1_file:") == 1 &&
			log.Count(StoreLog::Level::Error, " reason=file_identity_or_structure") == 3 &&
			log.Count(StoreLog::Level::Error, " stage=file.open stop_l2=range_start:0 systemError=2") == 1);
	}

	void FootprintBudgetPreservesDesktopRecovery(TestState& test, const TempRoot& root)
	{
		auto page = Page(10);
		auto wide = page->strokes.front();
		wide.undoId = 1;
		wide.points.front().width = 10'000'000;
		page->strokes.push_back(std::move(wide));
		ScopedUInkLog log;
		WakeCounter wake;
		DesktopInkSnapshots service;
		DesktopInkRecoveryRegistration registration;
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake }));
		DESKTOP_REQUIRE(service.Save({ Identity(1), page, &registration }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto saved = service.Take();
		DESKTOP_REQUIRE(saved && saved->result == DesktopInkResult::Succeeded && saved->publishedPath);
		DESKTOP_CHECK(saved->degraded && saved->fallbackCounts.footprintBudget == 1);
		const auto disk = ReadUInkStoreFile(*saved->publishedPath);
		DESKTOP_REQUIRE(disk.status == UInkReadStatus::Complete && disk.document &&
			disk.document->canvases.size() == 1 && disk.document->canvases.front().content.size() == 2);
		DESKTOP_REQUIRE(service.Load({ Identity(2), Guid(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto loaded = service.Take();
		DESKTOP_REQUIRE(loaded && loaded->result == DesktopInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1 && !loaded->snapshot->strokes.front().points.empty());
		DESKTOP_CHECK(loaded->degraded && loaded->fallbackCounts.footprintBudget == 1 &&
			loaded->snapshot->strokes.front().points.front().x == 10);
		DESKTOP_CHECK(log.Count(StoreLog::Level::Warn, " l3.footprintBudget=1") == 2);
	}

	void RetireRecoveryPaths(TestState& test, const TempRoot& root)
	{
		WakeCounter wake;
		WorkGate gate{ 2 };
		DesktopInkSnapshots service;
		ReleaseGate releaseLoad{ &gate };
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake },
			{ .beforeWork = WorkGate::BeforeWork, .context = &gate, .sampleLocalTime = FixedLocalTime }));

		DesktopInkRecoveryRegistration registration;
		DESKTOP_REQUIRE(service.Save({ Identity(1), Page(10), &registration }) == DesktopInkAdmission::Accepted && wake.Wait());
		auto saved = service.Take();
		DESKTOP_REQUIRE(saved && saved->result == DesktopInkResult::Succeeded && saved->publishedPath);
		const auto diskPath = *saved->publishedPath;
		const std::weak_ptr<const std::wstring> sharedPath = saved->publishedPath;
		saved.reset();
		DESKTOP_REQUIRE(service.Load({ Identity(2), Guid(1) }) == DesktopInkAdmission::Accepted &&
			gate.entered.try_acquire_for(10s));
		registration.Reset();
		DESKTOP_CHECK(service.Load({ Identity(3), Guid(1) }) == DesktopInkAdmission::MissingSave && !sharedPath.expired());
		releaseLoad.Release();
		DESKTOP_REQUIRE(wake.Wait());
		const auto loaded = service.Take();
		DESKTOP_REQUIRE(loaded && IsReceipt(*loaded, 2, DesktopInkOperation::Load) &&
			loaded->result == DesktopInkResult::Succeeded && loaded->snapshot &&
			loaded->snapshot->strokes.size() == 1 && !loaded->snapshot->strokes.front().points.empty());
		DESKTOP_CHECK(loaded->snapshot->strokes.front().points.front().x == 10 &&
			sharedPath.expired() && ReadPublishedStroke(diskPath, 1, 10));

		// 第二次 Clear 可以先退休尚未完成的保存，旧回执依然按原合同交付。
		gate.requestId = 3;
		ReleaseGate releaseSave{ &gate };
		DESKTOP_REQUIRE(service.Save({ Identity(3), Page(30), &registration }) == DesktopInkAdmission::Accepted &&
			gate.entered.try_acquire_for(10s));
		registration.Reset();
		DESKTOP_CHECK(service.Save({ Identity(3), Page(31) }) == DesktopInkAdmission::InvalidRequest);
		releaseSave.Release();
		DESKTOP_REQUIRE(wake.Wait());
		saved = service.Take();
		DESKTOP_REQUIRE(saved && IsReceipt(*saved, 3, DesktopInkOperation::Save) &&
			saved->result == DesktopInkResult::Succeeded && saved->publishedPath);
		DESKTOP_CHECK(service.Load({ Identity(4), Guid(3) }) == DesktopInkAdmission::MissingSave &&
			ReadPublishedStroke(*saved->publishedPath, 3, 30));

		DESKTOP_REQUIRE(service.Save({ Identity(4), Page(40), &registration }) == DesktopInkAdmission::Accepted && wake.Wait());
		saved = service.Take();
		DESKTOP_REQUIRE(saved && saved->result == DesktopInkResult::Succeeded && saved->publishedPath);
		const std::weak_ptr<const std::wstring> closePath = saved->publishedPath;
		saved.reset();
		DESKTOP_CHECK(!closePath.expired());
		service.Close();
		DESKTOP_CHECK(closePath.expired() && !service.Take());
	}

	void DegradedSave(TestState& test, const TempRoot& root)
	{
		using namespace draw3::shapes;
		Construction construction;
		construction.tool.preset = PresetKind::Line;
		const float x = static_cast<float>(kUInkMaxCoordinate) + 1024;
		construction.start = { x, 20 };
		construction.end = { x + 30, 35 };
		ShapeStyle style;
		style.strokeRgb = 0x875421;
		style.width = 3.5f;
		const auto shape = MakeConstructedShape(construction, style, Guid(2).Bytes(), 7);
		DESKTOP_REQUIRE(shape);
		Draw3UInkStrokeSnapshot stroke;
		stroke.style.kind = Draw3UInkStrokeKind::Shape;
		stroke.shape = shape;
		stroke.undoId = 1;
		auto page = Page(9);
		page->strokes.push_back(std::move(stroke));

		ScopedUInkLog log;
		WakeCounter wake;
		DesktopInkSnapshots service;
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake }));
		DESKTOP_REQUIRE(service.Save({ Identity(1), page }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto saved = service.Take();
		DESKTOP_REQUIRE(saved && saved->result == DesktopInkResult::Succeeded && saved->publishedPath);
		DESKTOP_CHECK(saved->degraded && saved->fallbackCounts.invalidShapeSkipped == 1 && page->strokes.size() == 2);
		DESKTOP_CHECK(log.rows.size() == 1 && log.Count(StoreLog::Level::Warn,
			"[UInkWrite] scene=desktop op=publish page=1 result=degraded stage=publish systemError=0") == 1 &&
			log.Count(StoreLog::Level::Warn, " l3.invalidShapeSkipped=1") == 1);
		const auto read = ReadUInkStoreFile(*saved->publishedPath);
		DESKTOP_REQUIRE(read.status == UInkReadStatus::Complete && read.document &&
			read.document->canvases.size() == 1);
		DESKTOP_CHECK(read.document->canvases.front().content.size() == 1 &&
			std::holds_alternative<UInkInk>(read.document->canvases.front().content.front()));

		// 图形位于首撤回组时，发布仍须成功，并把幸存笔迹的撤回组归零。
		std::swap(page->strokes[0], page->strokes[1]);
		page->strokes[0].undoId = 0;
		page->strokes[1].undoId = 1;
		const std::shared_ptr<const Draw3UInkCanvasSnapshot> cpuRecovery = std::move(page);
		DESKTOP_REQUIRE(service.Save({ Identity(2), cpuRecovery }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto savedAfterSkip = service.Take();
		DESKTOP_REQUIRE(savedAfterSkip && IsReceipt(*savedAfterSkip, 2, DesktopInkOperation::Save) &&
			savedAfterSkip->result == DesktopInkResult::Succeeded && savedAfterSkip->publishedPath);
		DESKTOP_CHECK(savedAfterSkip->degraded && savedAfterSkip->fallbackCounts.invalidShapeSkipped == 1 &&
			cpuRecovery.use_count() == 1 && cpuRecovery->strokes.size() == 2 &&
			cpuRecovery->strokes.front().shape && cpuRecovery->strokes.back().undoId == 1);
		const auto afterSkip = ReadUInkStoreFile(*savedAfterSkip->publishedPath);
		DESKTOP_REQUIRE(afterSkip.status == UInkReadStatus::Complete && afterSkip.document &&
			afterSkip.document->canvases.size() == 1);
		const auto& content = afterSkip.document->canvases.front().content;
		DESKTOP_REQUIRE(content.size() == 1 && std::holds_alternative<UInkInk>(content.front()));
		DESKTOP_CHECK(std::get<UInkInk>(content.front()).undoId == 0 && !service.Take());
	}

	void RestartRequiresTakingOldCompletion(TestState& test, const TempRoot& root)
	{
		WakeCounter wake;
		DesktopInkSnapshots service;
		const DesktopInkSnapshotsOptions options{ root.path, 1, WakeCounter::Wake, &wake };
		DESKTOP_REQUIRE(service.Start(options));
		DESKTOP_REQUIRE(service.Save({ Identity(1), Page(10) }) == DesktopInkAdmission::Accepted);
		service.Close();

		DESKTOP_CHECK(!service.Start(options) && wake.count == 1);
		const auto completion = service.Take();
		DESKTOP_REQUIRE(completion && IsReceipt(*completion, 1, DesktopInkOperation::Save));
		DESKTOP_CHECK(completion->result == DesktopInkResult::Succeeded && completion->publishedPath &&
			!service.Take());
		DESKTOP_REQUIRE(service.Start(options));
		DESKTOP_REQUIRE(service.Save({ Identity(2), Page(20) }) == DesktopInkAdmission::Accepted);
		service.Close();
		const auto afterRestart = service.Take();
		DESKTOP_CHECK(afterRestart && IsReceipt(*afterRestart, 2, DesktopInkOperation::Save) &&
			afterRestart->result == DesktopInkResult::Succeeded && wake.count == 2 && !service.Take());
	}

	void CloseQueued(TestState& test, const TempRoot& root, DesktopInkCloseMode mode)
	{
		using namespace SnapshotLifetimeProbe;
		Scope observation;
		WakeCounter wake;
		WorkGate gate;
		DesktopInkSnapshots service;
		std::jthread close;
		ReleaseGate release{ &gate };
		DESKTOP_REQUIRE(service.Start({ root.path, 1, WakeCounter::Wake, &wake },
			{ .beforeWork = WorkGate::BeforeWork, .context = &gate, .sampleLocalTime = MidnightBoundary }));
		DESKTOP_REQUIRE(service.Save({ Identity(1), Page(1) }) == DesktopInkAdmission::Accepted && wake.Wait());
		const auto seed = service.Take();
		DESKTOP_REQUIRE(seed && seed->result == DesktopInkResult::Succeeded && seed->publishedPath &&
			*seed->publishedPath == root.path + LR"(\desktop\2026-09-01\235959998_ab12cd34.uink)" &&
			ReadPublishedStroke(*seed->publishedPath, 1, 1));
		DESKTOP_REQUIRE(service.Save({ Identity(2), Page(2) }) == DesktopInkAdmission::Accepted);
		DESKTOP_REQUIRE(gate.entered.try_acquire_for(10s));
		auto large = LargePage(3);
		const auto watched = ObservePage(*large);
		DESKTOP_REQUIRE(Read(watched).bytes >= 3 * 1024 * 1024);
		DESKTOP_REQUIRE(service.Save({ Identity(3), std::move(large) }) == DesktopInkAdmission::Accepted &&
			service.Save({ Identity(4), Page(4) }) == DesktopInkAdmission::Accepted &&
			service.Load({ Identity(5), Guid(1) }) == DesktopInkAdmission::Accepted);

		close = std::jthread([&] { service.Close(mode); });
		const DesktopInkSaveRequest duplicate{ Identity(2), Page(2) };
		const auto deadline = std::chrono::steady_clock::now() + 10s;
		auto admission = service.Save(duplicate);
		while (admission != DesktopInkAdmission::Closed && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(1ms);
			admission = service.Save(duplicate);
		}
		DESKTOP_REQUIRE(admission == DesktopInkAdmission::Closed);
		DESKTOP_CHECK(wake.count == 1 && !service.Take() && Read(watched).freedBuffers == 0);

		// 关闭已开始、午夜后的两笔仍在队列内；释放闸门才能完成排空。
		release.Release();
		close.join();
		DESKTOP_CHECK(Released(watched));
		Report(test.name, watched);
		const auto active = service.Take();
		const auto queued = service.Take();
		const auto last = service.Take();
		const auto load = service.Take();
		DESKTOP_REQUIRE(active && queued && last && load && !service.Take());
		DESKTOP_CHECK(IsReceipt(*active, 2, DesktopInkOperation::Save) &&
			active->result == DesktopInkResult::Succeeded && active->publishedPath &&
			*active->publishedPath == root.path + LR"(\desktop\2026-09-01\235959999_ab12cd34.uink)" &&
			ReadPublishedStroke(*active->publishedPath, 2, 2));
		const auto queuedResult = mode == DesktopInkCloseMode::Drain ? DesktopInkResult::Succeeded : DesktopInkResult::Canceled;
		DESKTOP_CHECK(IsReceipt(*queued, 3, DesktopInkOperation::Save) && queued->result == queuedResult);
		DESKTOP_CHECK(IsReceipt(*last, 4, DesktopInkOperation::Save) && last->result == queuedResult);
		if (mode == DesktopInkCloseMode::Drain)
		{
			DESKTOP_REQUIRE(queued->publishedPath && last->publishedPath);
			DESKTOP_CHECK(*queued->publishedPath == root.path + LR"(\desktop\2026-09-02\000000000_ab12cd34.uink)" &&
				*last->publishedPath == root.path + LR"(\desktop\2026-09-02\000000001_ab12cd34.uink)" &&
				ReadPublishedStroke(*queued->publishedPath, 3, 3, 131072) && ReadPublishedStroke(*last->publishedPath, 4, 4));
		}
		else
			DESKTOP_CHECK(!queued->publishedPath && !last->publishedPath);

		DESKTOP_CHECK(IsReceipt(*load, 5, DesktopInkOperation::Load) &&
			load->result == DesktopInkResult::Canceled && load->error == DesktopInkError::Closing && !load->snapshot);
		const auto stats = service.Diagnostics();
		DESKTOP_CHECK(wake.count == 5 && stats.accepted == 4 && stats.pending == 0 &&
			stats.committed == (mode == DesktopInkCloseMode::Drain ? 4 : 2) &&
			service.Load({ Identity(6), Guid(1) }) == DesktopInkAdmission::Closed &&
			service.Save({ Identity(6), Page(6) }) == DesktopInkAdmission::Closed);
		service.Close();
		DESKTOP_CHECK(wake.count == 5 && !service.Take());
	}

	using TestCase = std::pair<const char*, void (*)(TestState&, const TempRoot&)>;
	constexpr TestCase kLifetimeCases[] = {
		{ "desktop-success-unpolled", [](TestState& test, const TempRoot& root) { SaveBufferLifetime(test, root, false); } },
		{ "desktop-write-failure-unpolled", [](TestState& test, const TempRoot& root) { SaveBufferLifetime(test, root, true); } },
		{ "desktop-stop-drain", [](TestState& test, const TempRoot& root) { CloseQueued(test, root, DesktopInkCloseMode::Drain); } },
	};

	int RunCases(std::span<const TestCase> cases)
	{
		int failures = 0;
		for (const auto& [name, run] : cases)
		{
			TestState test{ name };
			TempRoot root;
			if (DESKTOP_CHECK(!root.path.empty())) run(test, root);
			DESKTOP_CHECK(root.Clean());
			failures += test.failures;
		}
		return failures;
	}
}

int RunDesktopInkSnapshotLifetimeTests()
{
	return RunCases(kLifetimeCases);
}

int RunDesktopInkSnapshotsTests()
{
	constexpr TestCase cases[] = {
		{ "StartAndRejectEmptyWithoutCreatingDirectories", StartAndRejectEmptyWithoutCreatingDirectories },
		{ "ConcurrentCollision", ConcurrentCollision },
		{ "RoundTripAndLifetime", RoundTripAndLifetime },
		{ "RejectFailuresAndCatchExceptions", RejectFailuresAndCatchExceptions },
		{ "DegradedAndInvalidReads", DegradedAndInvalidReads },
		{ "FootprintBudgetPreservesDesktopRecovery", FootprintBudgetPreservesDesktopRecovery },
		{ "RetireRecoveryPaths", RetireRecoveryPaths },
		{ "DegradedSave", DegradedSave },
		{ "RestartRequiresTakingOldCompletion", RestartRequiresTakingOldCompletion },
		{ "CloseCancel", [](TestState& test, const TempRoot& root) { CloseQueued(test, root, DesktopInkCloseMode::Cancel); } },
	};
	const auto failures = RunCases(cases) + RunDesktopInkSnapshotLifetimeTests();
	std::cout << "DesktopInkSnapshots tests: " << failures << " failures\n";
	return failures;
}
