#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.Product.h"
#include "Draw3.Presentation.h"
#include <bcrypt.h>
#include <json/json.h>
#include "../../Helper/FailedCleanupDeadline.h"
#include "../../Helper/FailedCleanupRealCases.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <bit>
#include <cmath>
#include <cstdio>
#include <io.h>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

import Inkeys.Window;
import Inkeys.Drawing.Draw3.auto_save;
import Inkeys.Drawing.Draw3.presentation_auto_save;
import draw3.uink_file;

namespace Inkeys::Shutdown
{
	namespace
	{
		namespace Draw3 = Inkeys::Drawing::Draw3;
		using Inkeys::Window::WindowRole;
		constexpr std::array<WindowRole, 4> kFixtureRoles{
			WindowRole::MagnifierHost, WindowRole::Freeze,
			WindowRole::DrawpadPresentation, WindowRole::Drawpad };

		LONG ReadFlag(volatile LONG& value) noexcept
		{
			return InterlockedCompareExchange(&value, 0, 0);
		}

		// 仅授权fixture诊断；数字阶段不改POD/算法/等待预算，不等于worker内部阶段。
		std::atomic<DWORD> fixtureFailurePhase = 0;
		std::atomic_bool fixtureHostStarted = false;
		std::array<HANDLE, 3> fixtureIndexProbeEvents{};
		void FixturePhase(DWORD phase) noexcept { fixtureFailurePhase.store(phase, std::memory_order_release); }

		void RecordFixtureFailure(const VerifiedCleanupRealLaunch& launch) noexcept
		{
			try
			{
				const auto value = fixtureHostStarted.load(std::memory_order_acquire) ? Draw3::ProductRuntimeSnapshot() : Draw3::HostRuntimeSnapshot{};
				const auto counts = fixtureHostStarted.load(std::memory_order_acquire) ? Draw3::ProductHost().HiddenPersistenceSnapshot() : std::nullopt;
				const auto bridge = fixtureHostStarted.load(std::memory_order_acquire) ? Draw3::ProductHost().ProductBridge().Snapshot() : Draw3::Bridge::ProductState{};
				const auto wanted = bridge.presentationTarget ? Draw3::Bridge::ReadyIdentityFor(*bridge.presentationTarget) : Draw3::Bridge::PresentationReadyIdentity{};
				const auto ready = value.presentationReady.value_or(Draw3::Bridge::PresentationReadyIdentity{});
				char record[2200]{};
				const int size = sprintf_s(record,
					"case=%lu phase=%lu started=%u running=%u workspace=%u selection=%u page=%zu has_content=%u content=%llu presented=%llu success=%llu partial=%llu present=%llu clear=%llu stroke=%llu sequence=%llu active=%u terminal=%u ingress=%llu/%llu/%llu recycled=%llu output=%u/%llu ready_output=%u/%llu target_pending=%u ready_exists=%u ready_match=%u ui_ready_exists=%u ui_match=%u input_ready=%u wanted=%u/%llu/%llu/%llu/%u ready=%u/%llu/%llu/%llu/%u persistence=%llu/%llu/%llu/%llu ppt=%llu/%llu/%llu/%llu not_found=%llu\n",
					static_cast<DWORD>(launch.scenario), fixtureFailurePhase.load(std::memory_order_acquire), fixtureHostStarted.load() ? 1u : 0u,
					value.running ? 1u : 0u, static_cast<unsigned>(value.workspace), value.selectionMode ? 1u : 0u, value.currentPageIndex,
					value.currentPageHasContent ? 1u : 0u, value.contentRevision, value.presentedContentRevision, value.successfulPresentCount,
					value.partialPresentCount, value.presentCount, value.clearCommandCount, value.pen.strokeId, value.pen.inputSequence,
					value.pen.active ? 1u : 0u, value.pen.terminalLocked ? 1u : 0u, value.inputDownPublished, value.inputMovePublished,
					value.inputTerminalPublished, value.inputRecycled, static_cast<unsigned>(value.requestedOutputTarget), value.requestedOutputRevision,
					static_cast<unsigned>(value.readyOutputTarget), value.readyOutputRevision, value.commandScenePending ? 1u : 0u,
					value.presentationReady ? 1u : 0u, value.presentationReady == wanted ? 1u : 0u,
					value.presentationUiReady ? 1u : 0u, value.presentationUiReady == wanted ? 1u : 0u, value.presentationInputReady ? 1u : 0u,
					wanted.pageIndex, wanted.bindingRevision, wanted.targetRevision, wanted.sessionRevision, static_cast<unsigned>(wanted.pageKind),
					ready.pageIndex, ready.bindingRevision, ready.targetRevision, ready.sessionRevision, static_cast<unsigned>(ready.pageKind),
					counts ? counts->desktopAccepted : 0, counts ? counts->desktopCommitted : 0, counts ? counts->desktopFailed : 0,
					counts ? counts->desktopPending : 0, counts ? counts->presentationAccepted : 0, counts ? counts->presentationCommitted : 0,
					counts ? counts->presentationLoaded : 0, counts ? counts->presentationFailed : 0, counts ? counts->presentationNotFound : 0);
				if (size <= 0) return;
				// 固定owned新文件仅保存诊断，不修改旧运行数据；failure已先发布，I/O不能产生PASS。
				const auto path = launch.directory + L"\\fixture-failure-diagnostics.txt";
				const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
					FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
				if (file != INVALID_HANDLE_VALUE)
				{
					DWORD written = 0; (void)WriteFile(file, record, static_cast<DWORD>(size), &written, nullptr);
					(void)FlushFileBuffers(file); CloseHandle(file);
				}
				char markers[120]{};
				const auto marker = [](HANDLE event) noexcept -> unsigned
				{
					if (!event) return 0;
					const auto result = WaitForSingleObject(event, 0);
					return result == WAIT_OBJECT_0 ? 1u : result == WAIT_TIMEOUT ? 0u : 2u;
				};
				const int markerSize = sprintf_s(markers, "index_probe=%u/%u/%u\n", marker(fixtureIndexProbeEvents[0]),
					marker(fixtureIndexProbeEvents[1]), marker(fixtureIndexProbeEvents[2]));
				const auto markerPath = launch.directory + L"\\fixture-index-probes.txt";
				const HANDLE markerFile = CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
					FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
				if (markerFile != INVALID_HANDLE_VALUE)
				{
					DWORD written = 0; if (markerSize > 0) (void)WriteFile(markerFile, markers, static_cast<DWORD>(markerSize), &written, nullptr);
					(void)FlushFileBuffers(markerFile); CloseHandle(markerFile);
				}
				std::fprintf(stderr, "cleanup-real-failure %s%s", record, markerSize > 0 ? markers : "");
			}
			catch (...) {} // 诊断失败保持原90退场，不回到任何业务对象析构。
		}

		[[noreturn]] void FailFixture(const VerifiedCleanupRealLaunch& launch) noexcept
		{
			// 90 只表示夹具失败；有活 owner 时不返回析构 context/gate，也绝不算产品截止 PASS。
			InterlockedExchange(&launch.packet->header.error, 90);
			InterlockedExchange(&launch.packet->header.result, 2);
			PublishCleanupRealStage(*launch.packet, CleanupRealStage::PrerequisiteFailed);
			RecordFixtureFailure(launch);
			for (;;)
			{
				(void)TerminateProcess(GetCurrentProcess(), 90);
				Sleep(1);
			}
		}

		void PrepareStorageProducerLogs(const VerifiedCleanupRealLaunch& launch) noexcept
		{
			switch (launch.scenario)
			{
			case CleanupRealCase::DesktopHold: case CleanupRealCase::DesktopRelease:
			case CleanupRealCase::PptHold: case CleanupRealCase::PptRelease:
			case CleanupRealCase::NaturalClose:
				break;
			default:
				return;
			}
			try
			{
				const auto redirect = [&launch](const wchar_t* name, FILE* stream)
				{
					const std::wstring path = launch.directory + L"\\" + name;
					FILE* reopened = nullptr;
					// x 对应 CREATE_NEW，N 禁止继承；CRT 重建 GUI 原本无 console 的 FILE。
					if (_wfreopen_s(&reopened, path.c_str(), L"wbxN", stream) != 0 || reopened != stream) return false;
					const int descriptor = _fileno(stream);
					if (descriptor < 0) return false;
					const HANDLE file = reinterpret_cast<HANDLE>(_get_osfhandle(descriptor));
					BY_HANDLE_FILE_INFORMATION info{};
					DWORD flags = 0;
					LARGE_INTEGER length{};
					if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
						(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
						!GetHandleInformation(file, &flags) || (flags & HANDLE_FLAG_INHERIT) ||
						!GetFileSizeEx(file, &length) || length.QuadPart != 0) return false;
					return setvbuf(stream, nullptr, _IONBF, 0) == 0;
				};
				if (!redirect(L"fixture-product.stdout.log", stdout) || !redirect(L"fixture-product.stderr.log", stderr))
					FailFixture(launch);
				// 句柄归 CRT 所有，all-owner 真 join 前不关闭；hold/失败保持到本 child 死亡。
			}
			catch (...) { FailFixture(launch); }
		}

		namespace UInk = ::draw3::uink;
		constexpr DWORD kColorA = 0x2456A8FFu, kColorB = 0xD04832FFu;
		constexpr std::size_t kFixtureBytesLimit = 8u * 1024u * 1024u;

		[[noreturn]] void BadFixtureValue() { throw std::runtime_error("invalid owned fixture value"); }

		std::array<BYTE, 32> HashFixtureBytes(std::span<const BYTE> bytes)
		{
			if (bytes.size() > kFixtureBytesLimit) BadFixtureValue();
			BCRYPT_ALG_HANDLE raw = nullptr;
			if (BCryptOpenAlgorithmProvider(&raw, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) BadFixtureValue();
			struct Algorithm { BCRYPT_ALG_HANDLE value; ~Algorithm() { BCryptCloseAlgorithmProvider(value, 0); } } algorithm{ raw };
			DWORD objectLength = 0, returned = 0;
			if (BCryptGetProperty(raw, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
				sizeof(objectLength), &returned, 0) < 0 || !objectLength || objectLength > 65536) BadFixtureValue();
			std::vector<BYTE> object(objectLength);
			BCRYPT_HASH_HANDLE hashRaw = nullptr;
			if (BCryptCreateHash(raw, &hashRaw, object.data(), objectLength, nullptr, 0, 0) < 0) BadFixtureValue();
			struct Hash { BCRYPT_HASH_HANDLE value; ~Hash() { BCryptDestroyHash(value); } } hash{ hashRaw };
			std::array<BYTE, 32> result{};
			if (BCryptHashData(hashRaw, const_cast<BYTE*>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) < 0 ||
				BCryptFinishHash(hashRaw, result.data(), static_cast<ULONG>(result.size()), 0) < 0) BadFixtureValue();
			return result;
		}

		class FixtureValueEncoder
		{
		public:
			std::vector<BYTE> bytes;
			void Raw(const void* data, std::size_t size)
			{
				if (size > kFixtureBytesLimit - bytes.size()) BadFixtureValue();
				if (!size) return;
				const auto* first = static_cast<const BYTE*>(data);
				bytes.insert(bytes.end(), first, first + size);
			}
			void Byte(BYTE value) { Raw(&value, 1); }
			template <class T> void Integer(T value)
			{
				using Unsigned = std::make_unsigned_t<T>;
				const auto bits = static_cast<Unsigned>(value);
				for (std::size_t i = 0; i < sizeof(T); ++i) Byte(static_cast<BYTE>(bits >> (i * 8)));
			}
			void Float(float value) { if (!std::isfinite(value)) BadFixtureValue(); Integer(std::bit_cast<std::uint32_t>(value)); }
			void Double(double value) { if (!std::isfinite(value)) BadFixtureValue(); Integer(std::bit_cast<std::uint64_t>(value)); }
			void Length(std::size_t size) { if (size > MAXDWORD) BadFixtureValue(); Integer(static_cast<DWORD>(size)); }
			void Text(const std::string& value) { Length(value.size()); Raw(value.data(), value.size()); }
			void Guid(const UInk::UInkGuid& value) { Raw(value.Bytes().data(), 16); }
			template <class T, class Writer> void Optional(const std::optional<T>& value, Writer writer)
			{
				Byte(value.has_value() ? 1 : 0); if (value) writer(*value);
			}
			void Map(const UInk::UInkMessagePackValue::Map& value, unsigned depth)
			{
				if (depth > 32) BadFixtureValue();
				Length(value.size());
				for (const auto& pair : value) { Message(pair.first, depth + 1); Message(pair.second, depth + 1); }
			}
			void Message(const UInk::UInkMessagePackValue& value, unsigned depth)
			{
				if (depth > 32) BadFixtureValue();
				Byte(static_cast<BYTE>(value.value.index())); // 11种真实variant，Map/Array顺序原样，不JSON化。
				std::visit([&](const auto& item)
				{
					using T = std::decay_t<decltype(item)>;
					if constexpr (std::is_same_v<T, std::monostate>) {}
					else if constexpr (std::is_same_v<T, bool>) Byte(item ? 1 : 0);
					else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>) Integer(item);
					else if constexpr (std::is_same_v<T, float>) Float(item);
					else if constexpr (std::is_same_v<T, double>) Double(item);
					else if constexpr (std::is_same_v<T, std::string>) Text(item);
					else if constexpr (std::is_same_v<T, std::vector<std::byte>>) { Length(item.size()); Raw(item.data(), item.size()); }
					else if constexpr (std::is_same_v<T, UInk::UInkMessagePackValue::Array>)
						{ Length(item.size()); for (const auto& child : item) Message(child, depth + 1); }
					else if constexpr (std::is_same_v<T, UInk::UInkMessagePackValue::Map>) Map(item, depth);
					else if constexpr (std::is_same_v<T, UInk::UInkMessagePackExtension>)
						{ Integer(item.type); Length(item.data.size()); Raw(item.data.data(), item.data.size()); }
				}, value.value);
			}
			void Extra(const std::optional<UInk::UInkExtra>& value) { Optional(value, [&](const auto& map) { Map(map, 0); }); }
			void Viewport(const UInk::UInkViewport& value) { Float(value.x); Float(value.y); Float(value.scale); }
			void Stroke(const UInk::Draw3UInkStrokeSnapshot& value)
			{
				Byte(static_cast<BYTE>(value.style.kind)); Integer(value.style.fallbackRgb); Float(value.style.opacity);
				Integer(value.style.texture); Integer(value.undoId); Byte(value.renderOnlyWhenLatest ? 1 : 0);
				Length(value.points.size());
				for (const auto& point : value.points) { Float(point.x); Float(point.y); Float(point.width); }
			}
			void Canvas(const UInk::Draw3UInkCanvasSnapshot& value)
			{
				Optional(value.deviceGuid, [&](const auto& guid) { Guid(guid); }); Guid(value.pageGuid);
				Integer(value.pageIndex); Integer(value.pageNumber); Optional(value.slideId, [&](auto id) { Integer(id); });
				Integer(value.intervalOrdinal); Byte(value.retained ? 1 : 0); Viewport(value.viewport); Extra(value.extra);
				Length(value.strokes.size()); for (const auto& stroke : value.strokes) Stroke(stroke);
				Length(value.operations.size());
				for (const auto& operation : value.operations)
				{
					Byte(static_cast<BYTE>(operation.index()));
					std::visit([&](const auto& item)
					{
						if constexpr (std::is_same_v<std::decay_t<decltype(item)>, UInk::Draw3UInkStrokeSnapshot>) Stroke(item);
						else { Integer(item.undoId); Extra(item.extra); }
					}, operation);
				}
			}
			void Snapshot(const UInk::Draw3UInkExportSnapshot& value)
			{
				Guid(value.workspaceGuid); Guid(value.fileGuid); Integer(value.workspaceType); Integer(value.currentPageIndex);
				Float(value.dpiScale); Byte(value.assignedIndependentUndoGroups ? 1 : 0);
				Optional(value.workspaceName, [&](const auto& text) { Text(text); }); Optional(value.hostId, [&](const auto& text) { Text(text); }); Extra(value.workspaceExtra);
				Length(value.devices.size());
				for (const auto& device : value.devices)
				{
					Guid(device.guid); Integer(device.deviceType); Byte(device.parentResolved ? 1 : 0); Byte(device.usable ? 1 : 0);
					Optional(device.name, [&](const auto& text) { Text(text); }); Extra(device.extra);
					Optional(device.hardware, [&](const auto& hardware)
					{
						Optional(hardware.name, [&](const auto& text) { Text(text); }); Optional(hardware.id, [&](const auto& text) { Text(text); });
						Length(hardware.identifiers.size()); for (const auto& pair : hardware.identifiers) { Text(pair.first); Text(pair.second); }
						Optional(hardware.physicalWidth, [&](auto width) { Integer(width); }); Optional(hardware.physicalHeight, [&](auto height) { Integer(height); });
						Optional(hardware.scaleFactor, [&](float scale) { Float(scale); });
					});
					Byte(static_cast<BYTE>(device.geometry.index()));
					std::visit([&](const auto& geometry)
					{
						using T = std::decay_t<decltype(geometry)>;
						if constexpr (std::is_same_v<T, UInk::UInkDisplayDevice>) { Integer(geometry.x); Integer(geometry.y); Integer(geometry.width); Integer(geometry.height); }
						else if constexpr (std::is_same_v<T, UInk::UInkWindowDevice>) { Guid(geometry.parentDeviceGuid); Float(geometry.x); Float(geometry.y); Float(geometry.width); Float(geometry.height); Integer(geometry.zIndex); }
					}, device.geometry);
				}
				for (const auto* canvases : { &value.canvases, &value.activeCanvases, &value.retainedCanvases })
					{ Length(canvases->size()); for (const auto& canvas : *canvases) Canvas(canvas); }
			}
		};

		class OwnedFixturePaths
		{
		public:
			OwnedFixturePaths() = default;
			OwnedFixturePaths(const OwnedFixturePaths&) = delete;
			OwnedFixturePaths& operator=(const OwnedFixturePaths&) = delete;
			~OwnedFixturePaths() { for (HANDLE handle : handles_) CloseHandle(handle); }
			HANDLE Lease(const std::wstring& path, bool directory)
			{
				const HANDLE handle = CreateFileW(path.c_str(), directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
					FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
				BY_HANDLE_FILE_INFORMATION info{};
				if (handle == INVALID_HANDLE_VALUE) BadFixtureValue();
				if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
					(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
					(((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory))
					{ CloseHandle(handle); BadFixtureValue(); }
				try { handles_.push_back(handle); } catch (...) { CloseHandle(handle); throw; }
				return handle;
			}
			void DirectoryChain(const std::wstring& full)
			{
				if (full.size() < 3 || full[1] != L':' || full[2] != L'\\' || full.find_first_of(L"/:*?\"<>|", 3) != std::wstring::npos) BadFixtureValue();
				Lease(full.substr(0, 3), true);
				for (std::size_t begin = 3; begin < full.size();)
				{
					const auto delimiter = full.find(L'\\', begin);
					const auto end = delimiter == std::wstring::npos ? full.size() : delimiter;
					const auto component = full.substr(begin, end - begin);
					if (component.empty() || component == L"." || component == L".." || component.back() == L'.' || component.back() == L' ') BadFixtureValue();
					Lease(full.substr(0, end), true); begin = end + 1;
				}
			}
			std::string Read(const std::wstring& path)
			{
				const HANDLE handle = Lease(path, false); LARGE_INTEGER length{};
				if (!GetFileSizeEx(handle, &length) || length.QuadPart < 0 || length.QuadPart > kFixtureBytesLimit) BadFixtureValue();
				std::string bytes(static_cast<std::size_t>(length.QuadPart), '\0'); DWORD received = 0;
				if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr) || received != bytes.size()) BadFixtureValue();
				return bytes;
			}
		private:
			std::vector<HANDLE> handles_;
		};

		template <std::size_t N> void CopyAscii(char (&destination)[N], const std::string& value)
		{
			if (value.size() >= N || value.empty() || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c > 0x7e; })) BadFixtureValue();
			std::memcpy(destination, value.data(), value.size());
		}

		CleanupRealReceipt SnapshotReceipt(const UInk::Draw3UInkExportSnapshot& value)
		{
			CleanupRealReceipt result{};
			std::memcpy(result.fileGuid, value.fileGuid.Bytes().data(), 16); std::memcpy(result.workspaceGuid, value.workspaceGuid.Bytes().data(), 16);
			if (value.fileGuid.IsZero() || value.workspaceGuid.IsZero() || value.workspaceType < 0 || value.devices.size() > 4) BadFixtureValue();
			result.workspaceType = static_cast<DWORD>(value.workspaceType); result.deviceCount = static_cast<DWORD>(value.devices.size());
			const auto& active = value.activeCanvases.empty() ? value.canvases : value.activeCanvases;
			if (active.empty() || active.size() > 8 || value.retainedCanvases.size() > 16) BadFixtureValue();
			result.activeCanvasCount = static_cast<DWORD>(active.size()); result.retainedCanvasCount = static_cast<DWORD>(value.retainedCanvases.size());
			for (const auto* canvases : { &active, &value.retainedCanvases })
				for (const auto& canvas : *canvases)
				{
					const auto count = [&](const auto& stroke)
					{
						if (result.strokeCount >= 32 || stroke.points.size() > 4096 - result.pointCount) BadFixtureValue();
						++result.strokeCount; result.pointCount += static_cast<DWORD>(stroke.points.size());
					};
					if (canvas.operations.empty()) { for (const auto& stroke : canvas.strokes) count(stroke); }
					else for (const auto& operation : canvas.operations) if (const auto* stroke = std::get_if<UInk::Draw3UInkStrokeSnapshot>(&operation)) count(*stroke);
				}
			if (!result.strokeCount || !result.pointCount) BadFixtureValue();
			FixtureValueEncoder encoder; encoder.Snapshot(value); const auto digest = HashFixtureBytes(encoder.bytes);
			std::memcpy(result.geometrySha256, digest.data(), digest.size());
			return result;
		}

		CleanupRealReceipt DesktopReceipt(const Draw3::DesktopAutoSaveFixtureReadReceipt& read)
		{
			if (read.status != Draw3::DesktopPersistenceStatus::Loaded || !read.loadedSnapshot || !read.sourceRevision ||
				read.fileGuid != read.loadedSnapshot->fileGuid || read.loadedSnapshot->workspaceType != 0 ||
				read.loadedSnapshot->canvases.size() != 1 || read.loadedSnapshot->currentPageIndex != 0) BadFixtureValue();
			auto result = SnapshotReceipt(*read.loadedSnapshot); const auto& page = read.loadedSnapshot->canvases.front();
			if (page.pageGuid.IsZero() || page.pageIndex != 0 || page.slideId || !read.dailySequence || !read.sequenceInSession) BadFixtureValue();
			std::memcpy(result.pageGuid, page.pageGuid.Bytes().data(), 16); result.pageIndex = page.pageIndex; result.totalPages = 1;
			result.intervalOrdinal = page.intervalOrdinal; result.dailySequence = read.dailySequence; result.sequenceInSession = read.sequenceInSession;
			result.desktopTrigger = static_cast<DWORD>(read.trigger); CopyAscii(result.localDate, read.localDate); CopyAscii(result.storageSession, read.storageSession);
			result.uinkLength = read.sourceRevision->length; std::memcpy(result.uinkSha256, read.sourceRevision->sha256.data(), 32);
			const auto index = HashFixtureBytes({ reinterpret_cast<const BYTE*>(read.indexBytes.data()), read.indexBytes.size() });
			std::memcpy(result.indexSha256, index.data(), 32); return result;
		}

		struct PptDiskView
		{
			OwnedFixturePaths leases;
			std::string indexBytes, storageSession;
			UInk::UInkGuid fileGuid, workspaceGuid;
			std::wstring uinkPath;
			UInk::UInkSourceRevision revision;
			std::uint64_t mutationRevision = 0;
		};

		std::string Utf8Path(const std::wstring& path)
		{
			if (path.size() > INT_MAX) BadFixtureValue();
			const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
			if (!size) BadFixtureValue();
			std::string result(size, '\0');
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), result.data(), size, nullptr, nullptr) != size) BadFixtureValue();
			return result;
		}

		std::string AuthorizedStorageSession(const VerifiedCleanupRealLaunch& launch, bool foreign = false)
		{
			std::array<std::uint8_t, 16> bytes{}; std::memcpy(bytes.data(), launch.packet->header.nonce, 16);
			if (foreign) bytes[15] ^= 0x80;
			const UInk::UInkGuid guid(bytes); if (guid.IsZero()) BadFixtureValue(); return UInk::FormatUInkGuid(guid);
		}

		void PopulatePptDisk(PptDiskView& disk, const std::wstring& root, const Draw3::Bridge::PresentationTarget& target)
		{
			const auto directory = root + L"\\presentation";
			disk.leases.DirectoryChain(directory);
			disk.indexBytes = disk.leases.Read(directory + L"\\index.json");
			Json::CharReaderBuilder builder; builder["collectComments"] = false; builder["failIfExtra"] = true; builder["stackLimit"] = 64;
			std::unique_ptr<Json::CharReader> parser(builder.newCharReader()); Json::Value index; std::string errors;
			if (!parser || !parser->parse(disk.indexBytes.data(), disk.indexBytes.data() + disk.indexBytes.size(), &index, &errors) ||
				!index.isObject() || index["scenario"].asString() != "presentation" || !index["entries"].isArray() || index["entries"].size() != 1) BadFixtureValue();
			// 只提取本夹具唯一entry；完整合法性仍由后续production Load复核，不复制选路算法。
			const auto& entry = index["entries"][0];
			if (entry["sourceIdentity"].asString() != target.sourceIdentity || entry["presentationKey"].asString() != Draw3::FormatPresentationKey(target.key) ||
				entry["bindingMode"].asString() != "slide-id" || !entry["bindingRevision"].isUInt64() || entry["bindingRevision"].asUInt64() != target.bindingRevision ||
				entry["processLocal"].asBool() != target.processLocalIdentity || !entry["mutationRevision"].isUInt64() || !entry["mutationRevision"].asUInt64() ||
				!entry["slideIds"].isArray() || entry["slideIds"].size() != 2 || entry["slideIds"][0].asInt() != 601 || entry["slideIds"][1].asInt() != 602) BadFixtureValue();
			const auto file = UInk::ParseUInkGuid(entry["fileGuid"].asString()), workspace = UInk::ParseUInkGuid(entry["workspaceGuid"].asString());
			if (!file || !workspace || file->IsZero() || workspace->IsZero()) BadFixtureValue();
			disk.fileGuid = *file; disk.workspaceGuid = *workspace; disk.storageSession = entry["sessionId"].asString(); disk.mutationRevision = entry["mutationRevision"].asUInt64();
			const auto relative = entry["relativePath"].asString(); const auto guid = UInk::FormatUInkGuid(*file);
			const auto prefix = "files/" + guid + "_";
			if (relative != "files/" + guid + ".uink")
			{
				if (!relative.starts_with(prefix) || relative.size() != prefix.size() + 36 + 5 || !relative.ends_with(".uink")) BadFixtureValue();
				const auto transaction = UInk::ParseUInkGuid(relative.substr(prefix.size(), 36));
				if (!transaction || UInk::FormatUInkGuid(*transaction) != relative.substr(prefix.size(), 36)) BadFixtureValue();
			}
			disk.leases.DirectoryChain(directory + L"\\files");
			disk.uinkPath = directory + L"\\" + std::wstring(relative.begin(), relative.end());
			disk.leases.Lease(disk.uinkPath, false); // 在任何production Load/ReadUInk之前保住真实文件。
			const auto read = UInk::ReadUInkFile(disk.uinkPath);
			if (read.status != UInk::UInkReadStatus::Complete || !read.document || !read.sourceRevision ||
				read.provenance.contentSequenceRecovered || read.provenance.containsInvalidCompleteBlocks) BadFixtureValue();
			disk.revision = *read.sourceRevision;
		}

		struct FixtureChildState
		{
			explicit FixtureChildState(const VerifiedCleanupRealLaunch& value) : launch(value) {}
			VerifiedCleanupRealLaunch launch;
			std::array<std::atomic<HWND>, 4> windows{};
			std::array<std::atomic_uint32_t, 4> created{}, destroyed{};
			std::atomic_uint32_t beforeInvoked = 0, createdFailureInvoked = 0;
			std::atomic_bool graphicsReady = false, firstFrameCommitted = false;
			std::atomic_uint32_t styleRejected = 0, styleSetMask = 0, styleClearMask = 0;
			std::atomic_bool startSucceeded = false, closeSucceeded = false;
			HANDLE reached = nullptr, proceed = nullptr;
			HANDLE starter = nullptr, closer = nullptr;
			std::array<HANDLE, 3> indexProbeEvents{};
			FailedCleanupSignal signal;
			Draw3::HostStartOptions options;
			Draw3::HostRuntimeSnapshot beforeStroke;
			OwnedFixturePaths artifactDirectories;
			CleanupRealReceipt baseline{};
			bool observedSealed = false;
			std::unique_ptr<Draw3::PresentationAutoSaveService> freshReader;
			std::unique_ptr<PptDiskView> disk;
			Draw3::Bridge::PresentationTarget target;
			std::uint64_t initialPptFailed = 0, initialPptNotFound = 0;

			~FixtureChildState()
			{
				// 仅完整 Stop/Window join/所有辅助线程真 join 后，runner 才允许走到析构。
				if (reached) CloseHandle(reached);
				if (proceed) CloseHandle(proceed);
				for (HANDLE event : indexProbeEvents) if (event) CloseHandle(event);
			}
		};

		template <class Predicate>
		bool WaitUntil(Predicate predicate, DWORD milliseconds = 5000)
		{
			const ULONGLONG until = GetTickCount64() + milliseconds;
			do
			{
				if (predicate()) return true;
				Sleep(5);
			} while (GetTickCount64() < until);
			return predicate();
		}

		void CreateGates(const std::shared_ptr<FixtureChildState>& state)
		{
			state->reached = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			state->proceed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!state->reached || !state->proceed) FailFixture(state->launch);
		}

		bool IsOwnedHidden(HWND hwnd) noexcept
		{
			DWORD process = 0;
			return hwnd && IsWindow(hwnd) && GetWindowThreadProcessId(hwnd, &process) != 0 &&
				process == GetCurrentProcessId() && !IsWindowVisible(hwnd);
		}

		std::vector<Inkeys::Window::WindowSpec> BuildHiddenSpecs(
			const std::shared_ptr<FixtureChildState>& state)
		{
			std::vector<Inkeys::Window::WindowSpec> specs;
			for (std::size_t index = 0; index < kFixtureRoles.size(); ++index)
			{
				Inkeys::Window::WindowSpec spec;
				spec.role = kFixtureRoles[index];
				spec.className = L"Inkeys.CleanupReal." + std::to_wstring(index);
				spec.title = L"Inkeys owned cleanup fixture";
				spec.x = -32000; spec.y = -32000;
				spec.width = 640; spec.height = 480;
				spec.style = WS_POPUP | WS_CLIPCHILDREN;
				spec.exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
				if (index == 2) spec.exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
				else if (index != 3) spec.exStyle |= WS_EX_TRANSPARENT;
				spec.windowProc = index == 3 ? &DrawpadMsgCallback : &DefWindowProcW;
				spec.bindMessages = false;
				spec.visible = false;
				if (index == 3)
				{
					spec.beforeCreate = [state]
					{
						const auto scenario = state->launch.scenario;
						state->launch.packet->trace.ownerThreadIds[3] = GetCurrentThreadId();
						state->beforeInvoked.fetch_add(1, std::memory_order_release);
						if (scenario == CleanupRealCase::WindowBeforeHold || scenario == CleanupRealCase::WindowBeforeRelease)
						{
							state->launch.packet->trace.failureTick = GetTickCount64();
							return false;
						}
						return true;
					};
				}
				spec.created = [state, index](HWND hwnd)
				{
					state->windows[index].store(hwnd, std::memory_order_release);
					state->created[index].fetch_add(1, std::memory_order_release);
					auto& trace = state->launch.packet->trace;
					trace.ownerThreadIds[index] = GetCurrentThreadId();
					if (index == 0) trace.oldGeneration = state->created[0].load(std::memory_order_acquire);
					if (index == 2) trace.oldPresentation = reinterpret_cast<UINT_PTR>(hwnd);
					if (index == 3)
					{
						trace.oldDrawpad = reinterpret_cast<UINT_PTR>(hwnd);
						const auto scenario = state->launch.scenario;
						if (scenario == CleanupRealCase::WindowCreatedHold || scenario == CleanupRealCase::WindowCreatedRelease)
						{
							trace.failureTick = GetTickCount64();
							state->createdFailureInvoked.fetch_add(1, std::memory_order_release);
							throw std::runtime_error("owned Drawpad created failure");
						}
					}
				};
				spec.destroyed = [state, index]
				{
					state->destroyed[index].fetch_add(1, std::memory_order_release);
				};
				specs.push_back(std::move(spec));
			}
			return specs;
		}

		bool CheckHiddenChain(const std::shared_ptr<FixtureChildState>& state, std::size_t count)
		{
			for (std::size_t index = 0; index < count; ++index)
			{
				const HWND hwnd = state->windows[index].load(std::memory_order_acquire);
				if (!IsOwnedHidden(hwnd) || state->created[index].load(std::memory_order_acquire) != 1 ||
					state->destroyed[index].load(std::memory_order_acquire) != 0) return false;
				if (index && GetWindow(hwnd, GW_OWNER) != state->windows[index - 1].load(std::memory_order_acquire))
					return false;
			}
			return true;
		}

		bool StyleCallback(void* context, DWORD setMask, DWORD clearMask)
		{
			auto* state = static_cast<FixtureChildState*>(context);
			if (!state) return false;
			if (state->launch.scenario == CleanupRealCase::PresenterHold ||
				state->launch.scenario == CleanupRealCase::PresenterRelease)
			{
				state->launch.packet->trace.failureTick = GetTickCount64();
				state->styleSetMask.store(setMask, std::memory_order_relaxed);
				state->styleClearMask.store(clearMask, std::memory_order_relaxed);
				state->styleRejected.fetch_add(1, std::memory_order_release);
				return false; // 真 ConfigureWindow 请求被拒绝，未伪造 graphics/Host bool。
			}
			return Inkeys::Window::GetService().SetExtendedStyleFlags(WindowRole::Drawpad, setMask, clearMask);
		}

		void StartupMilestone(void* context, Draw3::HostStartupStage stage) noexcept
		{
			auto* state = static_cast<FixtureChildState*>(context);
			if (stage == Draw3::HostStartupStage::GraphicsReady)
				state->graphicsReady.store(true, std::memory_order_release);
			if (stage == Draw3::HostStartupStage::FirstFrameCommitted)
				state->firstFrameCommitted.store(true, std::memory_order_release);
		}

		enum class ThreadOperation { StartWindow, StartHost, Close };
		struct ThreadEnvelope
		{
			std::shared_ptr<FixtureChildState> state;
			ThreadOperation operation;
		};

		DWORD WINAPI FixtureThread(void* parameter) noexcept
		{
			auto* envelope = static_cast<ThreadEnvelope*>(parameter);
			const auto state = std::move(envelope->state);
			const auto operation = envelope->operation;
			delete envelope;
			try
			{
				if (operation == ThreadOperation::Close)
				{
					state->closeSucceeded.store(RunAuthorizedCleanupClose(state->launch), std::memory_order_release);
					return 0;
				}
				const bool started = operation == ThreadOperation::StartWindow
					? Inkeys::Window::GetService().Start(BuildHiddenSpecs(state), state->signal)
					: Draw3::StartProduct(state->windows[3].load(std::memory_order_acquire),
						state->windows[2].load(std::memory_order_acquire),
						{ state.get(), &StyleCallback }, state->options);
				state->startSucceeded.store(started, std::memory_order_release);
				if (!started) state->launch.packet->trace.firstStartFalseTick = GetTickCount64();
				InterlockedExchange(&state->launch.packet->trace.startReturned, 1);
				return 0;
			}
			catch (...) { FailFixture(state->launch); }
		}

		HANDLE StartFixtureThread(const std::shared_ptr<FixtureChildState>& state, ThreadOperation operation)
		{
			auto* envelope = new (std::nothrow) ThreadEnvelope{ state, operation };
			if (!envelope) FailFixture(state->launch);
			const HANDLE thread = CreateThread(nullptr, 0, &FixtureThread, envelope, 0, nullptr);
			if (!thread) FailFixture(state->launch);
			return thread;
		}

		void JoinFixtureThread(const std::shared_ptr<FixtureChildState>& state, HANDLE& thread)
		{
			if (!thread || WaitForSingleObject(thread, 20000) != WAIT_OBJECT_0) FailFixture(state->launch);
			DWORD code = 0;
			if (!GetExitCodeThread(thread, &code) || code != 0 || !CloseHandle(thread)) FailFixture(state->launch);
			thread = nullptr;
		}

		bool CheckDestroyedChain(const std::shared_ptr<FixtureChildState>& state, std::size_t count)
		{
			for (std::size_t index = 0; index < count; ++index)
				if (IsWindow(state->windows[index].load(std::memory_order_acquire)) ||
					state->created[index].load(std::memory_order_acquire) != 1 ||
					state->destroyed[index].load(std::memory_order_acquire) != 1) return false;
			return true;
		}

		void WaitPastGrace(const std::shared_ptr<FixtureChildState>& state)
		{
			const ULONGLONG until = state->launch.packet->trace.graceDeadline + 1000;
			while (GetTickCount64() < until) Sleep(10);
			if (state->launch.packet->trace.fatalPublishTick || ReadFlag(state->launch.packet->trace.intentObserved))
				FailFixture(state->launch);
		}

		int Finish(const std::shared_ptr<FixtureChildState>& state)
		{
			InterlockedExchange(&state->launch.packet->header.result, 1);
			PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::Finished);
			return 0;
		}

		int RunStartupFailure(const std::shared_ptr<FixtureChildState>& state)
		{
			const auto scenario = state->launch.scenario;
			const bool presenter = scenario == CleanupRealCase::PresenterHold || scenario == CleanupRealCase::PresenterRelease;
			const bool before = scenario == CleanupRealCase::WindowBeforeHold || scenario == CleanupRealCase::WindowBeforeRelease;
			const bool hold = scenario == CleanupRealCase::PresenterHold || scenario == CleanupRealCase::WindowBeforeHold ||
				scenario == CleanupRealCase::WindowCreatedHold;
			CreateGates(state);
			if (presenter)
			{
				FailedCleanupDeadline windowCleanup(&PublishAuthorizedCleanupFatal);
				windowCleanup.PrepareOrFatal();
				if (!Inkeys::Window::GetService().Start(BuildHiddenSpecs(state), windowCleanup.Signal())) FailFixture(state->launch);
				windowCleanup.CompleteOrFatal();
				if (!CheckHiddenChain(state, 4)) FailFixture(state->launch);
				PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::WindowReady);
			}
			FailedCleanupTestGates gates{};
			gates.beginGateAfterWake = true;
			gates.afterBeginClaimedEvent = state->reached;
			gates.continueBeginEvent = state->proceed;
			FailedCleanupDeadline cleanup(&PublishAuthorizedCleanupFatal, gates);
			cleanup.PrepareOrFatal();
			state->signal = cleanup.Signal();
			state->options.requiredPresentationMode = Draw3::HostPresentationMode::UlwDirtyRect;
			state->options.allowDirectComposition = false;
			state->options.failedCleanup = state->signal;
			state->options.startupContext = state.get();
			state->options.startupMilestone = &StartupMilestone;
			state->starter = StartFixtureThread(state, presenter ? ThreadOperation::StartHost : ThreadOperation::StartWindow);
			if (WaitForSingleObject(state->reached, 10000) != WAIT_OBJECT_0) FailFixture(state->launch);
			auto& trace = state->launch.packet->trace;
			trace.gateTick = GetTickCount64();
			trace.graceDeadline = state->signal.BeginKnownFailure(); // 已 Armed：只读同一 tick，不重置、不再经过门。
			if (!trace.failureTick || !trace.graceDeadline || ReadFlag(trace.startReturned) || ReadFlag(trace.stopReturned))
				FailFixture(state->launch);
			if (presenter)
			{
				if (!CheckHiddenChain(state, 4) || !state->graphicsReady.load(std::memory_order_acquire) ||
					state->firstFrameCommitted.load(std::memory_order_acquire) ||
					state->styleRejected.load(std::memory_order_acquire) != 1 ||
					state->styleSetMask.load(std::memory_order_acquire) != WS_EX_LAYERED ||
					state->styleClearMask.load(std::memory_order_acquire) != WS_EX_NOREDIRECTIONBITMAP ||
					Draw3::ProductFirstFrameReady() || !Draw3::ProductRunning()) FailFixture(state->launch);
			}
			else if (!CheckHiddenChain(state, before ? 3 : 4) ||
				state->beforeInvoked.load(std::memory_order_acquire) != 1 ||
				(before ? trace.oldDrawpad != 0 : state->createdFailureInvoked.load(std::memory_order_acquire) != 1))
				FailFixture(state->launch);
			InterlockedExchange(&trace.faultReached, 1);
			PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::FaultReached);
			if (hold)
			{
				// 已唤醒的真实 monitor 专职守原 tick；此处不发布额外 Close、不让 scope 析构。
				for (;;) Sleep(INFINITE);
			}
			Sleep(250);
			if (!SetEvent(state->proceed)) FailFixture(state->launch);
			JoinFixtureThread(state, state->starter);
			if (state->startSucceeded.load(std::memory_order_acquire)) FailFixture(state->launch);
			trace.stopEnterTick = GetTickCount64();
			if (presenter) Draw3::StopProduct();
			Inkeys::Window::GetService().StopAndJoin();
			trace.stopReturnTick = GetTickCount64();
			InterlockedExchange(&trace.stopReturned, 1);
			if (!CheckDestroyedChain(state, before ? 3 : 4) ||
				(before && state->destroyed[3].load(std::memory_order_acquire) != 0)) FailFixture(state->launch);
			InterlockedExchange(&trace.oldChainDestroyed, 1);
			cleanup.CompleteOrFatal();
			if (state->signal.BeginKnownFailure() != 0) FailFixture(state->launch);
			InterlockedExchange(&trace.oldSignalCancelled, 1);
			WaitPastGrace(state);
			return Finish(state);
		}

		bool PostContact(const std::shared_ptr<FixtureChildState>& state,
			Draw3::HiddenTestContactPhase phase, int x, int y)
		{
			const HWND hwnd = state->windows[3].load(std::memory_order_acquire);
			if (!IsOwnedHidden(hwnd) || Inkeys::Window::GetService().Handle(WindowRole::Drawpad) != hwnd) return false;
			return PostMessageW(hwnd, Draw3::kDraw3HiddenTestContactMessage,
				static_cast<WPARAM>(phase) | Draw3::kHiddenTestMouseFlag, MAKELPARAM(x, y)) != FALSE;
		}

		bool WriteStoredStroke(const std::shared_ptr<FixtureChildState>& state, bool pauseAtPresent)
		{
			state->beforeStroke = Draw3::ProductRuntimeSnapshot();
			if (!PostContact(state, Draw3::HiddenTestContactPhase::Down, 60, 80) || !WaitUntil([&]
			{
				const auto value = Draw3::ProductRuntimeSnapshot();
				return value.pen.active && value.pen.strokeId != state->beforeStroke.pen.strokeId &&
					value.pen.realPointCount && value.inputDownPublished == state->beforeStroke.inputDownPublished + 1;
			})) return false;
			const auto stroke = Draw3::ProductRuntimeSnapshot().pen.strokeId;
			for (int index = 1; index <= 6; ++index)
			{
				const auto before = Draw3::ProductRuntimeSnapshot().pen.inputSequence;
				if (!PostContact(state, Draw3::HiddenTestContactPhase::Move, 60 + index * 12, 80 + index * 4) ||
					!WaitUntil([&]
					{
						const auto pen = Draw3::ProductRuntimeSnapshot().pen;
						return pen.active && pen.strokeId == stroke && pen.inputSequence > before;
					}, 2000)) return false;
			}
			if (state->beforeStroke.currentPageHasContent && !pauseAtPresent)
			{
				// contentRevision 只表示hasContent边沿；已有A再写B不能借用每笔递增语义。
				const auto sequenceBeforeUp = Draw3::ProductRuntimeSnapshot().pen.inputSequence;
				if (!PostContact(state, Draw3::HiddenTestContactPhase::Up, 140, 108) || !WaitUntil([&]
				{
					const auto value = Draw3::ProductRuntimeSnapshot();
					return value.currentPageHasContent && !value.pen.active && value.pen.strokeId == stroke && value.pen.terminalLocked &&
						value.pen.inputSequence > sequenceBeforeUp && value.inputRecycled == state->beforeStroke.inputRecycled + 1 &&
						value.inputMovePublished >= state->beforeStroke.inputMovePublished + 6 &&
						value.inputTerminalPublished == state->beforeStroke.inputTerminalPublished + 1 &&
						value.completedStrokeKind == Draw3::Bridge::CompletedStrokeKind::Drawing;
				}, 2000)) return false;
				// terminal/回收可见后再取一次计数，避免旧live计数与较新pen快照拼成假成功。
				const auto afterTerminal = Draw3::ProductRuntimeSnapshot();
				if (afterTerminal.presentCount < afterTerminal.partialPresentCount) return false;
				const auto fullAttempts = afterTerminal.presentCount - afterTerminal.partialPresentCount;
				const HWND hwnd = state->windows[3].load(std::memory_order_acquire);
				if (!IsOwnedHidden(hwnd) || Inkeys::Window::GetService().Handle(WindowRole::Drawpad) != hwnd) return false;
				DWORD_PTR reply = 0;
				// 无payload的真实own入口：composition request→Controller→FullPresent，ULW也执行。
				// 仅正确性刷新佐证；不冒充DPI变更、落笔时刻或性能改进，后续仍核whole A+B文件。
				if (!SendMessageTimeoutW(hwnd, WM_DWMCOMPOSITIONCHANGED, 0, 0,
					SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &reply)) return false;
				return WaitUntil([&]
				{
					const auto value = Draw3::ProductRuntimeSnapshot();
					return !value.pen.active && value.pen.strokeId == stroke && value.pen.terminalLocked &&
						value.inputTerminalPublished == state->beforeStroke.inputTerminalPublished + 1 &&
						value.inputRecycled == state->beforeStroke.inputRecycled + 1 && value.currentPageHasContent &&
						value.successfulPresentCount > afterTerminal.successfulPresentCount && value.lastPresentSucceeded &&
						value.presentCount >= value.partialPresentCount && value.presentCount - value.partialPresentCount > fullAttempts &&
						value.workspace == state->beforeStroke.workspace && value.currentPageIndex == state->beforeStroke.currentPageIndex &&
						value.presentationReady == state->beforeStroke.presentationReady &&
						value.presentationUiReady == state->beforeStroke.presentationUiReady && value.presentationInputReady == state->beforeStroke.presentationInputReady &&
						value.presentationMode == Draw3::HostPresentationMode::UlwDirtyRect &&
						value.presentedContentRevision == value.contentRevision &&
						value.requestedOutputTarget == Draw3::HostOutputTarget::PrimaryDrawpad &&
						value.readyOutputTarget == value.requestedOutputTarget && value.readyOutputRevision == value.requestedOutputRevision;
				}, 2000);
			}
			if (!PostContact(state, Draw3::HiddenTestContactPhase::Up, 140, 108)) return false;
			if (pauseAtPresent && WaitForSingleObject(state->reached, 5000) != WAIT_OBJECT_0) return false;
			return WaitUntil([&]
			{
				const auto value = Draw3::ProductRuntimeSnapshot();
				return value.currentPageHasContent && !value.pen.active && value.pen.strokeId == stroke &&
					value.completedStrokeKind == Draw3::Bridge::CompletedStrokeKind::Drawing &&
					value.inputMovePublished >= state->beforeStroke.inputMovePublished + 6 &&
					value.inputTerminalPublished == state->beforeStroke.inputTerminalPublished + 1 &&
					value.contentRevision != state->beforeStroke.contentRevision &&
					value.successfulPresentCount > state->beforeStroke.successfulPresentCount &&
					value.lastPresentSucceeded && value.presentedContentRevision == value.contentRevision &&
					value.requestedOutputTarget == Draw3::HostOutputTarget::PrimaryDrawpad;
			}, 2000);
		}

		void StartHiddenProduct(const std::shared_ptr<FixtureChildState>& state, bool renderGate)
		{
			CreateGates(state);
			FailedCleanupDeadline windowCleanup(&PublishAuthorizedCleanupFatal);
			windowCleanup.PrepareOrFatal();
			if (!Inkeys::Window::GetService().Start(BuildHiddenSpecs(state), windowCleanup.Signal())) FailFixture(state->launch);
			windowCleanup.CompleteOrFatal();
			if (!CheckHiddenChain(state, 4)) FailFixture(state->launch);
			PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::WindowReady);
			FailedCleanupDeadline hostCleanup(&PublishAuthorizedCleanupFatal);
			hostCleanup.PrepareOrFatal();
			state->options.requiredPresentationMode = Draw3::HostPresentationMode::UlwDirtyRect;
			state->options.allowDirectComposition = false;
			state->options.enableHiddenTestContactInjection = true;
			state->options.failedCleanup = hostCleanup.Signal();
			state->options.startupContext = state.get();
			state->options.startupMilestone = &StartupMilestone;
			if (renderGate)
			{
				state->options.successfulPresentReachedEvent = state->reached;
				state->options.continueSuccessfulPresentEvent = state->proceed;
			}
			if (!Draw3::StartProduct(state->windows[3].load(std::memory_order_acquire),
				state->windows[2].load(std::memory_order_acquire), { state.get(), &StyleCallback }, state->options))
				FailFixture(state->launch);
			fixtureHostStarted.store(true, std::memory_order_release);
			InterlockedExchange(&state->launch.packet->trace.startReturned, 1);
			hostCleanup.CompleteOrFatal();
			const auto value = Draw3::ProductRuntimeSnapshot();
			const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(state->windows[3].load(), GWL_EXSTYLE));
			if (!value.firstFrameReady || !value.lastPresentSucceeded || !value.successfulPresentCount ||
				value.presentationMode != Draw3::HostPresentationMode::UlwDirtyRect ||
				(style & (WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT)) || !(style & WS_EX_LAYERED))
				FailFixture(state->launch);
			state->launch.packet->trace.firstSuccessTick = GetTickCount64();
			state->launch.packet->trace.baselineSuccessPresents = value.successfulPresentCount;
			PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::HostReady);
			Draw3::Bridge::ProductState pen;
			pen.tool = Draw3::Bridge::Tool::Pen;
			pen.selectionMode = false; pen.widthDip = 6.0f; pen.colorRgba = kColorA;
			pen.autoSaveEnabled = !state->options.autoSaveRoot.empty();
			Draw3::PublishProductState(pen);
			if (!WaitUntil([]
			{
				const auto current = Draw3::ProductRuntimeSnapshot();
				return !current.selectionMode && current.workspace == Draw3::Bridge::Workspace::Desktop;
			})) FailFixture(state->launch);
		}

		std::wstring StorageRoot(const std::shared_ptr<FixtureChildState>& state, bool ppt, bool create)
		{
			std::wstring path = state->launch.directory;
			state->artifactDirectories.DirectoryChain(path);
			for (const wchar_t* component : { L"artifacts", ppt ? L"C10" : L"C09", L"AutoSave" })
			{
				path += L"\\"; path += component;
				if (create && !CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) FailFixture(state->launch);
				state->artifactDirectories.Lease(path, true);
			}
			return path;
		}

		void PublishReceipt(const std::shared_ptr<FixtureChildState>& state, const CleanupRealReceipt& receipt, CleanupRealStage stage)
		{
			if (state->observedSealed) FailFixture(state->launch);
			state->launch.packet->observed = receipt; state->observedSealed = true;
			PublishCleanupRealStage(*state->launch.packet, stage);
		}

		Draw3::Bridge::PresentationTarget ResolveOwnedPptTarget(const std::shared_ptr<FixtureChildState>& state, DWORD originPid, ULONGLONG originDrawpad,
			DWORD page, std::uint64_t targetRevision = 0, std::uint64_t sessionRevision = 1)
		{
			if (!originPid || originPid > INT_MAX || !originDrawpad || originDrawpad > UINTPTR_MAX || page > 2) BadFixtureValue();
			Draw3::PresentationDescriptor descriptor;
			descriptor.status = Draw3::PresentationDescriptorStatus::StableSlideIds; descriptor.provider = "PowerPoint";
			descriptor.fullName = Utf8Path(state->launch.directory + L"\\artifacts\\C10\\fixture-source.pptx");
			descriptor.presentationName = "fixture-source.pptx"; descriptor.applicationProcessId = static_cast<std::int32_t>(originPid);
			descriptor.slideShowHwnd = static_cast<std::int64_t>(originDrawpad); descriptor.bindingRevision = 1;
			descriptor.totalPage = 2; descriptor.slideIds = { 601, 602 }; descriptor.currentPage = page + 1;
			if (page < 2) descriptor.currentSlideId = descriptor.slideIds[page];
			auto target = page == 2 ? Draw3::ResolveEndScreenTarget(descriptor) : Draw3::ResolvePresentationTarget(descriptor);
			if (!target || target->processLocalIdentity || target->bindingMode != Draw3::Bridge::SlideBindingMode::StableSlideId) BadFixtureValue();
			target->targetRevision = targetRevision; target->sessionRevision = sessionRevision;
			return *target;
		}

		CleanupRealReceipt ReadPptReceipt(const std::shared_ptr<FixtureChildState>& state, const Draw3::Bridge::PresentationTarget& target, bool foreign = false)
		{
			state->disk = std::make_unique<PptDiskView>(); PopulatePptDisk(*state->disk, state->options.autoSaveRoot, target);
			const auto storedSession = AuthorizedStorageSession(state->launch);
			if (state->disk->storageSession != storedSession) BadFixtureValue();
			Draw3::PresentationAutoSaveTestFaultInjection faults;
			faults.sessionIdOverride = AuthorizedStorageSession(state->launch, foreign);
			Draw3::SetPresentationAutoSaveTestFaultInjection(faults);
			state->freshReader = std::make_unique<Draw3::PresentationAutoSaveService>();
			if (!state->freshReader->Start(state->options.autoSaveRoot)) FailFixture(state->launch);
			const auto actualSession = state->freshReader->SessionId();
			Draw3::PresentationLoadRequest request; request.target = target; request.kind = Draw3::PresentationLoadKind::Current;
			if (state->freshReader->SubmitLoad(std::move(request)) != Draw3::PresentationPersistenceSubmitStatus::Accepted) FailFixture(state->launch);
			state->freshReader->CloseAndDrain();
			Draw3::PresentationPersistenceCompletion completion;
			if (!state->freshReader->TryTakeCompletion(completion) || completion.operation != Draw3::PresentationPersistenceOperation::Load ||
				completion.target != target || completion.storageTrack != Draw3::PresentationStorageTrack::Base) FailFixture(state->launch);
			state->freshReader.reset();
			if (foreign)
			{
				if (actualSession == storedSession || completion.status != Draw3::PresentationPersistenceStatus::CrossProcessConflictDeferred || completion.loadedSnapshot || completion.fileGuid)
					FailFixture(state->launch);
				state->disk.reset(); return {};
			}
			if (actualSession != storedSession || completion.status != Draw3::PresentationPersistenceStatus::Loaded || !completion.loadedSnapshot || !completion.fileGuid ||
				*completion.fileGuid != state->disk->fileGuid || completion.loadedSnapshot->fileGuid != state->disk->fileGuid ||
				completion.loadedSnapshot->workspaceGuid != state->disk->workspaceGuid || completion.mutationRevision != state->disk->mutationRevision) FailFixture(state->launch);
			auto receipt = SnapshotReceipt(*completion.loadedSnapshot);
			if (receipt.workspaceType != 2 || receipt.activeCanvasCount != 3 || receipt.retainedCanvasCount) FailFixture(state->launch);
			const auto& pages = completion.loadedSnapshot->activeCanvases.empty() ? completion.loadedSnapshot->canvases : completion.loadedSnapshot->activeCanvases;
			if (pages[0].slideId != 601 || pages[1].slideId != 602 || pages[2].slideId || pages[2].pageIndex != 2 ||
				UInk::InkeysPageKind(pages[2].extra) != UInk::UInkInkeysPageKind::EndScreen ||
				pages[0].pageGuid == pages[1].pageGuid || pages[0].pageGuid == pages[2].pageGuid || pages[1].pageGuid == pages[2].pageGuid) FailFixture(state->launch);
			if (pages[0].strokes.size() != receipt.strokeCount || receipt.strokeCount > 2) FailFixture(state->launch);
			for (std::size_t i = 0; i < pages[0].strokes.size(); ++i)
				if (pages[0].strokes[i].style.kind != UInk::Draw3UInkStrokeKind::Pen ||
					pages[0].strokes[i].style.fallbackRgb != ((i == 0 ? kColorA : kColorB) >> 8)) FailFixture(state->launch);
			const auto& page = pages.at(completion.target.pageIndex);
			std::memcpy(receipt.pageGuid, page.pageGuid.Bytes().data(), 16); std::memcpy(receipt.presentationKey, completion.target.key.bytes.data(), 16);
			receipt.mutationRevision = completion.mutationRevision; receipt.bindingRevision = completion.target.bindingRevision;
			receipt.targetRevision = completion.target.targetRevision; receipt.sessionRevision = completion.target.sessionRevision;
			receipt.pageIndex = completion.target.pageIndex; receipt.totalPages = completion.target.totalPages;
			receipt.bindingMode = static_cast<DWORD>(completion.target.bindingMode); receipt.pageKind = static_cast<DWORD>(completion.target.pageKind);
			receipt.processLocalIdentity = completion.target.processLocalIdentity ? 1 : 0; receipt.intervalOrdinal = page.intervalOrdinal;
			for (std::size_t i = 0; i < completion.target.slideIds.size(); ++i) receipt.slideIds[i] = completion.target.slideIds[i];
			CopyAscii(receipt.storageSession, state->disk->storageSession); receipt.uinkLength = state->disk->revision.length;
			std::memcpy(receipt.uinkSha256, state->disk->revision.sha256.data(), 32);
			const auto digest = HashFixtureBytes({ reinterpret_cast<const BYTE*>(state->disk->indexBytes.data()), state->disk->indexBytes.size() });
			std::memcpy(receipt.indexSha256, digest.data(), 32); state->disk.reset(); return receipt;
		}

		Draw3::HostHiddenPersistenceSnapshot Persistence(const std::shared_ptr<FixtureChildState>& state)
		{
			const auto value = Draw3::ProductHost().HiddenPersistenceSnapshot(); if (!value) FailFixture(state->launch); return *value;
		}

		void TracePersistence(const std::shared_ptr<FixtureChildState>& state, bool ppt)
		{
			const auto value = Persistence(state); auto& trace = state->launch.packet->trace;
			trace.saveAccepted = ppt ? value.presentationAccepted : value.desktopAccepted;
			trace.saveCommitted = ppt ? value.presentationCommitted : value.desktopCommitted;
			trace.saveFailed = ppt ? value.presentationFailed : value.desktopFailed;
			trace.savePending = ppt ? 0 : value.desktopPending; // PPT未暴露pending，不能拿accepted推算Save。
		}

		void SetPenColor(DWORD rgba)
		{
			// 只改笔参数，保留当前真实workspace/target；默认快照会错误切回Desktop并清PPT目标。
			auto pen = Draw3::ProductHost().ProductBridge().Snapshot();
			pen.tool = Draw3::Bridge::Tool::Pen; pen.selectionMode = false;
			pen.widthDip = 6.0f; pen.colorRgba = rgba; pen.autoSaveEnabled = true; Draw3::PublishProductState(pen);
		}

		bool CheckStrokeColors(const UInk::Draw3UInkExportSnapshot& value, bool ppt, bool second)
		{
			const auto& pages = value.activeCanvases.empty() ? value.canvases : value.activeCanvases;
			const auto& strokes = pages.at(0).strokes;
			if (strokes.size() != (ppt && second ? 2u : 1u)) return false;
			for (std::size_t i = 0; i < strokes.size(); ++i)
				if (strokes[i].style.kind != UInk::Draw3UInkStrokeKind::Pen || strokes[i].points.empty() ||
					strokes[i].style.fallbackRgb != ((second && (!ppt || i == 1)) ? kColorB : kColorA) >> 8) return false;
			return true;
		}

		void ActualCloseAndStop(const std::shared_ptr<FixtureChildState>& state, bool hold, bool ppt)
		{
			state->closer = StartFixtureThread(state, ThreadOperation::Close); JoinFixtureThread(state, state->closer);
			if (!state->closeSucceeded.load(std::memory_order_acquire)) FailFixture(state->launch);
			auto& trace = state->launch.packet->trace; trace.stopEnterTick = GetTickCount64();
			Draw3::StopProduct(); // 必须让真实final-capture/实际worker drain承担等待，不用standalone worker代替。
			if (hold) FailFixture(state->launch); // 非预期提前返回不能算普通截止通过。
			Inkeys::Window::GetService().StopAndJoin(); trace.stopReturnTick = GetTickCount64();
			InterlockedExchange(&trace.stopReturned, 1);
			if (!CheckDestroyedChain(state, 4)) FailFixture(state->launch);
			InterlockedExchange(&trace.oldChainDestroyed, 1); TracePersistence(state, ppt);
			PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::StopReturned);
		}

		Draw3::DesktopAutoSaveFixtureReadReceipt ReadDesktopCommitted(const std::shared_ptr<FixtureChildState>& state, const std::string& date)
		{
			auto read = Draw3::ReadLastCommittedDesktopAutoSaveFixture(state->options.autoSaveRoot, date);
			if (read.status == Draw3::DesktopPersistenceStatus::NotFound)
			{
				const auto now = Draw3::CaptureDesktopAutoSaveTimestamp().localDate;
				if (now != date) read = Draw3::ReadLastCommittedDesktopAutoSaveFixture(state->options.autoSaveRoot, now);
			}
			if (read.status != Draw3::DesktopPersistenceStatus::Loaded) FailFixture(state->launch); return read;
		}

		std::string ClearDesktop(const std::shared_ptr<FixtureChildState>& state)
		{
			const auto before = Draw3::ProductRuntimeSnapshot().clearCommandCount;
			const auto date = Draw3::CaptureDesktopAutoSaveTimestamp().localDate;
			if (Draw3::PublishProductCommand(Draw3::Bridge::CommandType::Clear) != Draw3::Bridge::CommandResult::Accepted ||
				!WaitUntil([&] { const auto current = Draw3::ProductRuntimeSnapshot(); return current.clearCommandCount == before + 1 && !current.currentPageHasContent; }))
				FailFixture(state->launch);
			return date;
		}

		int RunDesktopStorage(const std::shared_ptr<FixtureChildState>& state)
		{
			const bool hold = state->launch.scenario == CleanupRealCase::DesktopHold;
			const bool natural = state->launch.scenario == CleanupRealCase::NaturalClose;
			FixturePhase(201); state->options.autoSaveRoot = StorageRoot(state, false, true);
			Draw3::DesktopAutoSaveTestFaultInjection faults; faults.logIndexCommitDiagnostics = true;
			Draw3::SetDesktopAutoSaveTestFaultInjection(faults); // 只在这三个 auth Desktop producer 开诊断。
			StartHiddenProduct(state, false);
			if (!WriteStoredStroke(state, false)) FailFixture(state->launch);
			FixturePhase(202); const auto dateA = ClearDesktop(state);
			FixturePhase(203);
			if (!WaitUntil([&] { const auto d = Persistence(state); return d.desktopAccepted == 1 && d.desktopCommitted == 1 && !d.desktopFailed && !d.desktopPending; })) FailFixture(state->launch);
			FixturePhase(204); const auto readA = ReadDesktopCommitted(state, dateA);
			if (!CheckStrokeColors(*readA.loadedSnapshot, false, false)) FailFixture(state->launch);
			state->baseline = DesktopReceipt(readA); TracePersistence(state, false);
			if (hold) PublishReceipt(state, state->baseline, CleanupRealStage::BaselineReady);
			else PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::BaselineReady);
			for (auto& event : state->indexProbeEvents)
			{
				event = CreateEventW(nullptr, TRUE, FALSE, nullptr); if (!event) FailFixture(state->launch);
			}
			fixtureIndexProbeEvents = state->indexProbeEvents; // 借用alias，State持有到worker真join/死亡。
			faults.enteringIndexMutexEvent = state->indexProbeEvents[0];
			faults.indexMutexAcquiredEvent = state->indexProbeEvents[1];
			faults.indexReadCompletedEvent = state->indexProbeEvents[2];
			if (!natural) { faults.writeDelayMilliseconds = hold ? 60000 : 400; faults.enteringWriteDelayEvent = state->reached; }
			FixturePhase(205); Draw3::SetDesktopAutoSaveTestFaultInjection(faults); SetPenColor(kColorB);
			if (!WriteStoredStroke(state, false)) FailFixture(state->launch);
			FixturePhase(206); const auto dateB = ClearDesktop(state);
			if (!natural)
			{
				FixturePhase(207); if (WaitForSingleObject(state->reached, 5000) != WAIT_OBJECT_0) FailFixture(state->launch);
				state->launch.packet->trace.gateTick = GetTickCount64();
				state->launch.packet->trace.gateSuccessPresents = Draw3::ProductRuntimeSnapshot().successfulPresentCount;
				const auto d = Persistence(state);
				if (d.desktopAccepted != 2 || d.desktopFailed || (hold && (d.desktopCommitted != 1 || !d.desktopPending))) FailFixture(state->launch);
				InterlockedExchange(&state->launch.packet->trace.faultReached, 1); PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::FaultReached);
			}
			TracePersistence(state, false);
			if (hold) ActualCloseAndStop(state, true, false);
			FixturePhase(208); if (!WaitUntil([&] { const auto d = Persistence(state); return d.desktopAccepted == 2 && d.desktopCommitted == 2 && !d.desktopFailed && !d.desktopPending; })) FailFixture(state->launch);
			FixturePhase(209); ActualCloseAndStop(state, false, false); Draw3::ResetDesktopAutoSaveTestFaultInjection();
			FixturePhase(210); const auto readB = ReadDesktopCommitted(state, dateB);
			if (!CheckStrokeColors(*readB.loadedSnapshot, false, true)) FailFixture(state->launch);
			const auto receipt = DesktopReceipt(readB);
			if (std::memcmp(receipt.fileGuid, state->baseline.fileGuid, 16) == 0 || receipt.sequenceInSession <= state->baseline.sequenceInSession) FailFixture(state->launch);
			PublishReceipt(state, receipt, CleanupRealStage::Finished); return Finish(state);
		}

		void SelectPptPage(const std::shared_ptr<FixtureChildState>& state, DWORD page, bool content)
		{
			FixturePhase(320 + page * 10); auto target = ResolveOwnedPptTarget(state, GetCurrentProcessId(), state->launch.packet->trace.oldDrawpad, page);
			const auto revision = Draw3::PublishProductPresentationTarget(target); if (!revision) FailFixture(state->launch); target.targetRevision = *revision;
			FixturePhase(321 + page * 10); const auto identity = Draw3::Bridge::ReadyIdentityFor(target);
			if (!WaitUntil([&]
			{
				const auto current = Draw3::ProductRuntimeSnapshot();
				return current.workspace == Draw3::Bridge::Workspace::Presentation && current.presentationReady == identity &&
					current.currentPageHasContent == content && current.lastPresentSucceeded && current.presentedContentRevision == current.contentRevision;
			}, 6000)) FailFixture(state->launch);
			FixturePhase(322 + page * 10);
			if (!Draw3::PublishProductPresentationUiReady(identity)) FailFixture(state->launch);
			FixturePhase(323 + page * 10);
			if (!WaitUntil([&] { return Draw3::ProductRuntimeSnapshot().presentationInputReady; })) FailFixture(state->launch);
			state->target = std::move(target);
		}

		int RunPptStorage(const std::shared_ptr<FixtureChildState>& state)
		{
			const bool hold = state->launch.scenario == CleanupRealCase::PptHold;
			FixturePhase(301); state->options.autoSaveRoot = StorageRoot(state, true, true);
			Draw3::PresentationAutoSaveTestFaultInjection faults; faults.logSaveIoDiagnostics = true;
			faults.sessionIdOverride = AuthorizedStorageSession(state->launch); // 仅这两个 auth PPT producer。
			Draw3::SetPresentationAutoSaveTestFaultInjection(faults); FixturePhase(302); StartHiddenProduct(state, false); SelectPptPage(state, 0, false);
			FixturePhase(303); const auto empty = Persistence(state);
			// 当前真实service把NotFound累入failed；明确actual终态见证，不假成Save失败或重置计数。
			if (!empty.presentationNotFound || empty.presentationNotFound != empty.presentationFailed || empty.presentationCommitted) FailFixture(state->launch);
			state->initialPptFailed = empty.presentationFailed; state->initialPptNotFound = empty.presentationNotFound;
			std::printf("cleanup-real-ppt-empty not_found=%llu failed_baseline=%llu\n", static_cast<unsigned long long>(empty.presentationNotFound), static_cast<unsigned long long>(empty.presentationFailed));
			if (!WriteStoredStroke(state, false)) FailFixture(state->launch); SelectPptPage(state, 1, false);
			if (!WaitUntil([&] { const auto d = Persistence(state); return d.presentationCommitted == 1 && d.presentationFailed == state->initialPptFailed; })) FailFixture(state->launch);
			FixturePhase(305); state->baseline = ReadPptReceipt(state, state->target);
			if (state->baseline.strokeCount != 1) FailFixture(state->launch); TracePersistence(state, true);
			if (hold) PublishReceipt(state, state->baseline, CleanupRealStage::BaselineReady); else PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::BaselineReady);
			SelectPptPage(state, 0, true); SetPenColor(kColorB);
			if (!WriteStoredStroke(state, false)) FailFixture(state->launch);
			faults.afterUInkCommittedEvent = state->reached; faults.continueIndexCommitEvent = state->proceed;
			Draw3::SetPresentationAutoSaveTestFaultInjection(faults); SelectPptPage(state, 1, false);
			if (WaitForSingleObject(state->reached, 5000) != WAIT_OBJECT_0) FailFixture(state->launch);
			auto& trace = state->launch.packet->trace; trace.gateTick = GetTickCount64(); trace.gateSuccessPresents = Draw3::ProductRuntimeSnapshot().successfulPresentCount;
			const auto pending = Persistence(state);
			if (pending.presentationCommitted != 1 || pending.presentationFailed != state->initialPptFailed || pending.presentationNotFound != state->initialPptNotFound) FailFixture(state->launch);
			{
				OwnedFixturePaths indexLease; indexLease.DirectoryChain(state->options.autoSaveRoot + L"\\presentation");
				const auto indexBytes = indexLease.Read(state->options.autoSaveRoot + L"\\presentation\\index.json");
				const auto indexHash = HashFixtureBytes({ reinterpret_cast<const BYTE*>(indexBytes.data()), indexBytes.size() });
				if (std::memcmp(indexHash.data(), state->baseline.indexSha256, 32) != 0) FailFixture(state->launch);
			} // 放行worker前先解除只读file lease，否则会自己阻止其正常index commit。
			TracePersistence(state, true); InterlockedExchange(&trace.faultReached, 1); PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::FaultReached);
			if (hold) ActualCloseAndStop(state, true, true);
			Sleep(250); if (!SetEvent(state->proceed)) FailFixture(state->launch);
			if (!WaitUntil([&] { const auto d = Persistence(state); return d.presentationCommitted == 2 && d.presentationFailed == state->initialPptFailed; })) FailFixture(state->launch);
			ActualCloseAndStop(state, false, true); faults.afterUInkCommittedEvent = nullptr; faults.continueIndexCommitEvent = nullptr;
			Draw3::SetPresentationAutoSaveTestFaultInjection(faults);
			const auto receipt = ReadPptReceipt(state, state->target);
			if (receipt.strokeCount != 2 || receipt.mutationRevision <= state->baseline.mutationRevision || std::memcmp(receipt.indexSha256, state->baseline.indexSha256, 32) == 0) FailFixture(state->launch);
			PublishReceipt(state, receipt, CleanupRealStage::Finished); Draw3::ResetPresentationAutoSaveTestFaultInjection(); return Finish(state);
		}

		void WriteOwnedFixtureFile(const std::wstring& path, const std::string& bytes)
		{
			const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
				FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (file == INVALID_HANDLE_VALUE) BadFixtureValue();
			struct File { HANDLE value; ~File() { CloseHandle(value); } } owner{ file };
			DWORD written = 0;
			if (bytes.size() > kFixtureBytesLimit || !WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
				written != bytes.size() || !FlushFileBuffers(file)) BadFixtureValue();
		}

		void CheckDesktopReaderBoundaries(const std::shared_ptr<FixtureChildState>& state, const Draw3::DesktopAutoSaveFixtureReadReceipt& original)
		{
			// 只从本run真实committed数据克隆到固定子根；这些不是新的Saved请求/业务恢复点。
			const auto base = state->options.autoSaveRoot + L"\\_fixture-reader-boundaries";
			if (!CreateDirectoryW(base.c_str(), nullptr)) FailFixture(state->launch);
			state->artifactDirectories.Lease(base, true);
			Json::CharReaderBuilder builder; builder["stackLimit"] = 64; builder["failIfExtra"] = true;
			std::unique_ptr<Json::CharReader> parser(builder.newCharReader()); Json::Value index; std::string errors;
			if (!parser || !parser->parse(original.indexBytes.data(), original.indexBytes.data() + original.indexBytes.size(), &index, &errors) ||
				!index["entries"].isArray() || index["entries"].size() > 2) FailFixture(state->launch);
			const std::wstring date(original.localDate.begin(), original.localDate.end());
			const auto sourceDirectory = state->options.autoSaveRoot + L"\\desktop\\" + date;
			OwnedFixturePaths sourceLease; sourceLease.DirectoryChain(sourceDirectory);
			auto create = [&](const wchar_t* label, bool copyFiles)
			{
				const auto root = base + L"\\" + label;
				if (!CreateDirectoryW(root.c_str(), nullptr) || !CreateDirectoryW((root + L"\\desktop").c_str(), nullptr) ||
					!CreateDirectoryW((root + L"\\desktop\\" + date).c_str(), nullptr)) FailFixture(state->launch);
				state->artifactDirectories.DirectoryChain(root + L"\\desktop\\" + date);
				if (copyFiles)
					for (const auto& entry : index["entries"])
					{
						const auto name = entry["relativePath"].asString();
						if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos) FailFixture(state->launch);
						const std::wstring leaf(name.begin(), name.end()); sourceLease.Lease(sourceDirectory + L"\\" + leaf, false);
						if (!CopyFileW((sourceDirectory + L"\\" + leaf).c_str(), (root + L"\\desktop\\" + date + L"\\" + leaf).c_str(), TRUE)) FailFixture(state->launch);
					}
				return root;
			};
			auto loaded = [&](const std::wstring& root)
			{
				const auto read = Draw3::ReadLastCommittedDesktopAutoSaveFixture(root, original.localDate);
				return read.status == Draw3::DesktopPersistenceStatus::Loaded && read.loadedSnapshot && read.fileGuid == original.fileGuid &&
					std::memcmp(DesktopReceipt(read).geometrySha256, DesktopReceipt(original).geometrySha256, 32) == 0;
			};
			const auto valid = create(L"valid-primary", true);
			WriteOwnedFixtureFile(valid + L"\\desktop\\" + date + L"\\index.json", original.indexBytes);
			if (!loaded(valid)) FailFixture(state->launch);
			const auto backup = create(L"valid-backup", true);
			WriteOwnedFixtureFile(backup + L"\\desktop\\" + date + L"\\index.json.bak", original.indexBytes);
			if (!loaded(backup)) FailFixture(state->launch);
			const auto recovered = create(L"corrupt-primary-backup", true);
			WriteOwnedFixtureFile(recovered + L"\\desktop\\" + date + L"\\index.json", "invalid");
			WriteOwnedFixtureFile(recovered + L"\\desktop\\" + date + L"\\index.json.bak", original.indexBytes);
			if (!loaded(recovered)) FailFixture(state->launch);
			const auto missing = create(L"missing-index", false);
			if (Draw3::ReadLastCommittedDesktopAutoSaveFixture(missing, original.localDate).status != Draw3::DesktopPersistenceStatus::NotFound) FailFixture(state->launch);
			const auto corrupt = create(L"corrupt-only", true);
			WriteOwnedFixtureFile(corrupt + L"\\desktop\\" + date + L"\\index.json", "invalid");
			const auto corruptRead = Draw3::ReadLastCommittedDesktopAutoSaveFixture(corrupt, original.localDate);
			if ((corruptRead.status != Draw3::DesktopPersistenceStatus::Invalid && corruptRead.status != Draw3::DesktopPersistenceStatus::IoError) ||
				corruptRead.loadedSnapshot) FailFixture(state->launch);
			std::string status = "valid-primary/backup/corrupt-primary-backup/missing/corrupt-only verified\n";
			auto symlink = [&](const std::wstring& link, const std::wstring& target, DWORD flags, const std::wstring& testRoot, const char* name)
			{
				if (!CreateSymbolicLinkW(link.c_str(), target.c_str(), flags))
				{
					const DWORD error = GetLastError();
					if (error != ERROR_PRIVILEGE_NOT_HELD && error != ERROR_ACCESS_DENIED && error != ERROR_NOT_SUPPORTED) FailFixture(state->launch);
					status += std::string(name) + " NOT VERIFIED symlink_error=" + std::to_string(error) + "\n"; return;
				}
				const auto read = Draw3::ReadLastCommittedDesktopAutoSaveFixture(testRoot, original.localDate);
				if ((read.status != Draw3::DesktopPersistenceStatus::Invalid && read.status != Draw3::DesktopPersistenceStatus::IoError) ||
					read.loadedSnapshot) FailFixture(state->launch);
				status += std::string(name) + " reparse-rejected\n";
			};
			const auto indexLink = create(L"index-link", true);
			symlink(indexLink + L"\\desktop\\" + date + L"\\index.json", sourceDirectory + L"\\index.json", 0, indexLink, "index-link");
			const auto fileLink = create(L"uink-link", false);
			WriteOwnedFixtureFile(fileLink + L"\\desktop\\" + date + L"\\index.json", original.indexBytes);
			for (const auto& entry : index["entries"])
			{
				const auto name = entry["relativePath"].asString();
				if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos) FailFixture(state->launch);
				const std::wstring leaf(name.begin(), name.end());
				if (name != std::string(original.relativePath.begin(), original.relativePath.end()))
					if (!CopyFileW((sourceDirectory + L"\\" + leaf).c_str(), (fileLink + L"\\desktop\\" + date + L"\\" + leaf).c_str(), TRUE)) FailFixture(state->launch);
			}
			symlink(fileLink + L"\\desktop\\" + date + L"\\" + original.relativePath, sourceDirectory + L"\\" + original.relativePath, 0, fileLink, "uink-link");
			const auto directoryLink = base + L"\\directory-link";
			if (!CreateDirectoryW(directoryLink.c_str(), nullptr)) FailFixture(state->launch);
			state->artifactDirectories.Lease(directoryLink, true);
			symlink(directoryLink + L"\\desktop", state->options.autoSaveRoot + L"\\desktop", SYMBOLIC_LINK_FLAG_DIRECTORY, directoryLink, "directory-link");
			WriteOwnedFixtureFile(base + L"\\status.txt", status); // 固定owned证据，权限缺口独立NOT VERIFIED，不计为reparse通过。
			std::printf("cleanup-real-reader-boundaries %s", status.c_str());
		}

		int RunCommittedReader(const std::shared_ptr<FixtureChildState>& state)
		{
			const bool desktop = state->launch.scenario == CleanupRealCase::ReadDesktopCommitted;
			state->options.autoSaveRoot = StorageRoot(state, !desktop, false);
			CleanupRealReceipt receipt{};
			if (desktop)
			{
				const auto read = Draw3::ReadLastCommittedDesktopAutoSaveFixture(state->options.autoSaveRoot, state->launch.packet->expected.localDate);
				receipt = DesktopReceipt(read);
				CheckDesktopReaderBoundaries(state, read);
			}
			else
			{
				const auto& expected = state->launch.packet->expected;
				const auto target = ResolveOwnedPptTarget(state, state->launch.packet->header.originChildPid, state->launch.packet->trace.oldDrawpad,
					expected.pageIndex, expected.targetRevision, expected.sessionRevision);
				if (std::memcmp(target.key.bytes.data(), expected.presentationKey, 16) != 0 || target.bindingRevision != expected.bindingRevision ||
					static_cast<DWORD>(target.bindingMode) != expected.bindingMode || static_cast<DWORD>(target.pageKind) != expected.pageKind) FailFixture(state->launch);
				receipt = ReadPptReceipt(state, target, state->launch.scenario == CleanupRealCase::ReadPptForeignSession);
				Draw3::ResetPresentationAutoSaveTestFaultInjection();
			}
			if (state->launch.scenario != CleanupRealCase::ReadPptForeignSession && std::memcmp(&receipt, &state->launch.packet->expected, sizeof(receipt)) != 0) FailFixture(state->launch);
			InterlockedExchange(&state->launch.packet->trace.readerSucceeded, 1); PublishReceipt(state, receipt, CleanupRealStage::ReaderDone);
			return Finish(state);
		}

		int RunRenderFailure(const std::shared_ptr<FixtureChildState>& state)
		{
			StartHiddenProduct(state, true);
			if (!WriteStoredStroke(state, true)) FailFixture(state->launch);
			auto& trace = state->launch.packet->trace;
			trace.gateTick = GetTickCount64();
			trace.gateSuccessPresents = Draw3::ProductRuntimeSnapshot().successfulPresentCount;
			if (trace.gateSuccessPresents <= trace.baselineSuccessPresents) FailFixture(state->launch);
			InterlockedExchange(&trace.faultReached, 1);
			PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::FaultReached);
			if (state->launch.scenario == CleanupRealCase::RenderRelease)
			{
				Sleep(250);
				if (!SetEvent(state->proceed)) FailFixture(state->launch);
			}
			state->closer = StartFixtureThread(state, ThreadOperation::Close);
			JoinFixtureThread(state, state->closer);
			if (!state->closeSucceeded.load(std::memory_order_acquire)) FailFixture(state->launch);
			trace.stopEnterTick = GetTickCount64();
			Draw3::StopProduct(); // hold 真停在原 controller final-save 屏障，不安装 startup cleanup grace。
			Inkeys::Window::GetService().StopAndJoin();
			trace.stopReturnTick = GetTickCount64();
			InterlockedExchange(&trace.stopReturned, 1);
			if (state->launch.scenario == CleanupRealCase::RenderHold || !CheckDestroyedChain(state, 4))
				FailFixture(state->launch);
			InterlockedExchange(&trace.oldChainDestroyed, 1);
			PublishCleanupRealStage(*state->launch.packet, CleanupRealStage::StopReturned);
			return Finish(state);
		}
	}

	int RunAuthorizedFailedCleanupRealFixture(const VerifiedCleanupRealLaunch& launch) noexcept
	{
		// 第一动作必须验证 root capability；未鉴权不得创建窗口/事件/worker 或启用 fault。
		if (!IsAuthorizedCleanupRealLaunch(launch)) return 83;
		PrepareStorageProducerLogs(launch); // 授权之后、任何 State/Host/Window/fault 之前。
		std::shared_ptr<FixtureChildState> state; // 异常也保留worker/context到明确失败死亡。
		try
		{
			if (launch.scenario == CleanupRealCase::MainUlw) return RunMainFailedCleanupUlwCounterexample(launch);
			state = std::make_shared<FixtureChildState>(launch);
			launch.packet->trace.childStartTick = GetTickCount64();
			switch (launch.scenario)
			{
			case CleanupRealCase::PresenterHold: case CleanupRealCase::PresenterRelease:
			case CleanupRealCase::WindowBeforeHold: case CleanupRealCase::WindowBeforeRelease:
			case CleanupRealCase::WindowCreatedHold: case CleanupRealCase::WindowCreatedRelease:
				return RunStartupFailure(state);
			case CleanupRealCase::RenderHold: case CleanupRealCase::RenderRelease:
				return RunRenderFailure(state);
			case CleanupRealCase::DesktopHold: case CleanupRealCase::DesktopRelease: case CleanupRealCase::NaturalClose:
				return RunDesktopStorage(state);
			case CleanupRealCase::PptHold: case CleanupRealCase::PptRelease:
				return RunPptStorage(state);
			case CleanupRealCase::ReadDesktopCommitted: case CleanupRealCase::ReadPptCommitted: case CleanupRealCase::ReadPptForeignSession:
				return RunCommittedReader(state);
			default:
				FailFixture(launch);
			}
		}
		catch (...) { FailFixture(launch); }
	}
}
