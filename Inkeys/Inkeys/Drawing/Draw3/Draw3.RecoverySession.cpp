#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Draw3.RecoverySession.h"

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <sstream>

namespace Inkeys::Drawing::Draw3::Recovery
{
	namespace
	{
		std::string ToUtf8(const std::wstring& text)
		{
			if (text.empty()) return {};
			const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
				nullptr, 0, nullptr, nullptr);
			if (size <= 0) return {};
			std::string output(static_cast<size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size,
				nullptr, nullptr);
			return output;
		}

		std::wstring FromUtf8(const std::string& text)
		{
			if (text.empty()) return {};
			const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			if (size <= 0) return {};
			std::wstring output(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size);
			return output;
		}

		const char* WorkspaceName(Workspace workspace) noexcept
		{
			switch (workspace)
			{
			case Workspace::Whiteboard: return "whiteboard";
			case Workspace::Presentation: return "presentation";
			default: return "desktop";
			}
		}

		Workspace ParseWorkspace(const std::string& text) noexcept
		{
			if (text == "whiteboard") return Workspace::Whiteboard;
			if (text == "presentation") return Workspace::Presentation;
			return Workspace::Desktop;
		}

		const char* PendingKindName(PendingKind kind) noexcept
		{
			switch (kind)
			{
			case PendingKind::Create: return "create";
			case PendingKind::Relocate: return "relocate";
			case PendingKind::ShapeGroup: return "shapeGroup";
			default: return "page";
			}
		}

		PendingKind ParsePendingKind(const std::string& text) noexcept
		{
			if (text == "create") return PendingKind::Create;
			if (text == "relocate") return PendingKind::Relocate;
			if (text == "shapeGroup") return PendingKind::ShapeGroup;
			return PendingKind::Page;
		}

		Json::Value FileToJson(const FileEntry& file)
		{
			Json::Value value(Json::objectValue);
			value["path"] = ToUtf8(file.path);
			value["fileId"] = file.identity.ToString();
			if (file.pending)
			{
				Json::Value pending(Json::objectValue);
				pending["kind"] = PendingKindName(file.pending->kind);
				pending["start"] = Json::UInt64(file.pending->start);
				value["pending"] = pending;
			}
			else value["pending"] = Json::Value(Json::nullValue);
			return value;
		}

		std::optional<FileEntry> FileFromJson(const Json::Value& value)
		{
			if (!value.isObject() || !value["path"].isString()) return std::nullopt;
			FileEntry file;
			file.path = FromUtf8(value["path"].asString());
			if (value["fileId"].isString()) file.identity = FileIdentity::Parse(value["fileId"].asString());
			const Json::Value& pending = value["pending"];
			// 写前登记只认 start（§13.2）：尾偏移在写前无法得知，旧记录里的 end 一律忽略。
			if (pending.isObject() && pending["start"].isUInt64())
			{
				PendingWrite write;
				write.kind = ParsePendingKind(pending["kind"].isString() ? pending["kind"].asString() : "");
				write.start = pending["start"].asUInt64();
				file.pending = write;
			}
			if (file.path.empty()) return std::nullopt;
			return file;
		}

		std::string LocalTimeText()
		{
			SYSTEMTIME time{};
			GetLocalTime(&time);
			char buffer[40]{};
			sprintf_s(buffer, "%04u-%02u-%02uT%02u:%02u:%02u.%03u", time.wYear, time.wMonth, time.wDay,
				time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
			return buffer;
		}

		std::wstring SessionDirectory(const std::wstring& inkRoot)
		{
			return inkRoot + L"\\session";
		}

		bool EnsureDirectoryTree(const std::wstring& path) noexcept
		{
			// 逐级建立：父目录不存在时 CreateDirectoryW 会失败。
			try
			{
				for (size_t position = 0; ; )
				{
					position = path.find_first_of(L"\\/", position + 1);
					const std::wstring part = path.substr(0, position);
					if (!part.empty() && part.back() != L':')
						if (!CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
							return false;
					if (position == std::wstring::npos) break;
				}
			}
			catch (...) { return false; }
			const DWORD attributes = GetFileAttributesW(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
		}

		HANDLE CreateExclusiveDeleteOnClose(const std::wstring& path) noexcept
		{
			SECURITY_ATTRIBUTES attributes{ sizeof(attributes), nullptr, FALSE }; // 句柄不可继承
			return CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, &attributes, CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		}
	}

	std::string FileIdentity::ToString() const
	{
		if (!valid) return {};
		char buffer[48]{};
		sprintf_s(buffer, "%08lx:%016llx", static_cast<unsigned long>(volumeSerial),
			static_cast<unsigned long long>(fileIndex));
		return buffer;
	}

	FileIdentity FileIdentity::Parse(const std::string& text) noexcept
	{
		FileIdentity identity;
		unsigned long volume = 0;
		unsigned long long index = 0;
		if (sscanf_s(text.c_str(), "%lx:%llx", &volume, &index) == 2)
		{
			identity.volumeSerial = static_cast<uint32_t>(volume);
			identity.fileIndex = index;
			identity.valid = true;
		}
		return identity;
	}

	bool QueryFileIdentity(void* file, FileIdentity& identity) noexcept
	{
		identity = {};
		BY_HANDLE_FILE_INFORMATION info{};
		if (!file || file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(static_cast<HANDLE>(file), &info))
			return false;
		identity.volumeSerial = info.dwVolumeSerialNumber;
		identity.fileIndex = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
		identity.valid = true;
		return true;
	}

	bool QueryFileIdentity(const std::wstring& path, FileIdentity& identity) noexcept
	{
		const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) { identity = {}; return false; }
		const bool queried = QueryFileIdentity(file, identity);
		CloseHandle(file);
		return queried;
	}

	bool WriteSessionRecord(const std::wstring& path, const SessionRecord& record) noexcept
	{
		try
		{
			Json::Value root(Json::objectValue);
			root["version"] = record.version;
			root["pid"] = record.pid;
			root["startTime"] = record.startTime;
			root["recoverInk"] = record.recoverInk;
			root["clean"] = record.clean;
			root["workspace"] = WorkspaceName(record.workspace);
			root["desktop"] = record.desktop ? FileToJson(*record.desktop) : Json::Value(Json::nullValue);
			Json::Value whiteboard = record.whiteboard ? FileToJson(*record.whiteboard) : Json::Value(Json::nullValue);
			if (whiteboard.isObject())
			{
				if (record.whiteboardPageGuid) whiteboard["currentPageGuid"] = *record.whiteboardPageGuid;
				if (record.whiteboardPageIndex) whiteboard["currentPageIndex"] = *record.whiteboardPageIndex;
			}
			root["whiteboard"] = whiteboard;
			// 白板文件尚未建立时也保留当前页，便于诊断。
			if (!record.whiteboard && record.whiteboardPageIndex) root["whiteboardPageIndex"] = *record.whiteboardPageIndex;
			Json::Value presentations(Json::arrayValue);
			for (const auto& entry : record.presentations)
			{
				Json::Value value = FileToJson(entry.file);
				value["key"] = entry.key;
				presentations.append(value);
			}
			root["presentations"] = presentations;
			Json::Value settled(Json::objectValue);
			settled["desktop"] = record.settledDesktop;
			settled["whiteboard"] = record.settledWhiteboard;
			Json::Value settledPresentations(Json::arrayValue);
			for (const auto& key : record.settledPresentations) settledPresentations.append(key);
			settled["presentations"] = settledPresentations;
			root["settled"] = settled;
			root["failedRecoveryAttempts"] = record.failedRecoveryAttempts;
			Json::Value desktopClear(Json::objectValue);
			desktopClear["generation"] = Json::UInt64(record.desktopClearGeneration);
			desktopClear["cleared"] = record.desktopCleared;
			desktopClear["previous"] = record.desktopPreviousInterval ? Json::Value(ToUtf8(*record.desktopPreviousInterval))
				: Json::Value(Json::nullValue);
			desktopClear["previousGeneration"] = Json::UInt64(record.desktopPreviousGeneration);
			root["desktopClear"] = desktopClear;

			Json::StreamWriterBuilder builder;
			builder["indentation"] = "  ";
			builder["emitUTF8"] = true;
			const std::string text = Json::writeString(builder, root);

			const std::wstring temporary = path + L".tmp";
			const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
				written == text.size() && FlushFileBuffers(file);
			CloseHandle(file);
			if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				DeleteFileW(temporary.c_str());
				return false;
			}
			return true;
		}
		catch (...) { return false; }
	}

	std::optional<SessionRecord> ReadSessionRecord(const std::wstring& path) noexcept
	{
		try
		{
			const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return std::nullopt;
			LARGE_INTEGER size{};
			std::string text;
			bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 4 * 1024 * 1024;
			if (ok)
			{
				text.resize(static_cast<size_t>(size.QuadPart));
				DWORD read = 0;
				ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) && read == text.size();
			}
			CloseHandle(file);
			if (!ok) return std::nullopt;

			Json::Value root;
			Json::CharReaderBuilder builder;
			std::string errors;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors) || !root.isObject())
				return std::nullopt;
			SessionRecord record;
			if (!root["version"].isUInt() || root["version"].asUInt() != 1) return std::nullopt;
			record.pid = root["pid"].isUInt() ? root["pid"].asUInt() : 0;
			record.startTime = root["startTime"].isString() ? root["startTime"].asString() : std::string{};
			record.recoverInk = !root["recoverInk"].isBool() || root["recoverInk"].asBool();
			record.clean = root["clean"].isBool() && root["clean"].asBool();
			record.workspace = ParseWorkspace(root["workspace"].isString() ? root["workspace"].asString() : "");
			record.desktop = FileFromJson(root["desktop"]);
			record.whiteboard = FileFromJson(root["whiteboard"]);
			const Json::Value& whiteboard = root["whiteboard"];
			if (whiteboard.isObject())
			{
				if (whiteboard["currentPageGuid"].isString()) record.whiteboardPageGuid = whiteboard["currentPageGuid"].asString();
				if (whiteboard["currentPageIndex"].isUInt()) record.whiteboardPageIndex = whiteboard["currentPageIndex"].asUInt();
			}
			for (const auto& value : root["presentations"])
			{
				auto file = FileFromJson(value);
				if (!file || !value["key"].isString()) continue;
				record.presentations.push_back({ value["key"].asString(), std::move(*file) });
			}
			const Json::Value& settled = root["settled"];
			if (settled.isObject())
			{
				record.settledDesktop = settled["desktop"].isBool() && settled["desktop"].asBool();
				record.settledWhiteboard = settled["whiteboard"].isBool() && settled["whiteboard"].asBool();
				for (const auto& key : settled["presentations"])
					if (key.isString()) record.settledPresentations.push_back(key.asString());
			}
			record.failedRecoveryAttempts = root["failedRecoveryAttempts"].isUInt() ? root["failedRecoveryAttempts"].asUInt() : 0;
			const Json::Value& desktopClear = root["desktopClear"];
			if (desktopClear.isObject())
			{
				record.desktopClearGeneration = desktopClear["generation"].isUInt64() ? desktopClear["generation"].asUInt64() : 0;
				record.desktopCleared = desktopClear["cleared"].isBool() && desktopClear["cleared"].asBool();
				if (desktopClear["previous"].isString()) record.desktopPreviousInterval = FromUtf8(desktopClear["previous"].asString());
				record.desktopPreviousGeneration = desktopClear["previousGeneration"].isUInt64()
					? desktopClear["previousGeneration"].asUInt64() : 0;
			}
			return record;
		}
		catch (...) { return std::nullopt; }
	}

	Session::~Session()
	{
		Stop(false);
	}

	bool Session::Start(const std::wstring& inkRoot, bool recoverInk) noexcept
	{
		if (active_.load(std::memory_order_acquire)) return true;
		try
		{
			directory_ = SessionDirectory(inkRoot);
			if (!EnsureDirectoryTree(directory_)) return false;
			SYSTEMTIME time{};
			GetLocalTime(&time);
			GUID guid{};
			(void)CoCreateGuid(&guid);
			wchar_t name[96]{};
			swprintf_s(name, L"%04u%02u%02u-%02u%02u%02u%03u_%lu_%08lx", time.wYear, time.wMonth, time.wDay,
				time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
				static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long>(guid.Data1));
			name_ = name;
			// 先拿存活锁再发布记录：扫描方只要看到记录，就能可靠探测本进程是否存活。
			const HANDLE lock = CreateExclusiveDeleteOnClose(directory_ + L"\\" + name_ + L".lock");
			if (lock == INVALID_HANDLE_VALUE) return false;
			lock_ = lock;
			recordPath_ = directory_ + L"\\" + name_ + L".json";
			{
				std::scoped_lock guard(mutex_);
				record_ = {};
				record_.pid = GetCurrentProcessId();
				record_.startTime = LocalTimeText();
				record_.recoverInk = recoverInk;
				if (!WriteLocked())
				{
					CloseHandle(static_cast<HANDLE>(lock_));
					lock_ = nullptr;
					return false;
				}
			}
			recoverInk_.store(recoverInk ? 1 : 0, std::memory_order_release);
			workerStop_ = false;
			worker_ = std::thread([this] { WorkerLoop(); });
			active_.store(true, std::memory_order_release);
			return true;
		}
		catch (...)
		{
			if (lock_) { CloseHandle(static_cast<HANDLE>(lock_)); lock_ = nullptr; }
			return false;
		}
	}

	void Session::Stop(bool clean) noexcept
	{
		if (!active_.exchange(false, std::memory_order_acq_rel)) return;
		{
			std::scoped_lock guard(workerMutex_);
			workerStop_ = true;
		}
		workerCondition_.notify_all();
		if (worker_.joinable()) worker_.join();
		{
			std::scoped_lock guard(mutex_);
			if (clean)
			{
				record_.clean = true;
				(void)WriteLocked();
			}
		}
		// 释放存活锁（DELETE_ON_CLOSE 随之删除）；被强退的进程写不到 clean，下次启动按非正常结束处理。
		if (lock_) { CloseHandle(static_cast<HANDLE>(lock_)); lock_ = nullptr; }
	}

	bool Session::WriteLocked() noexcept
	{
		return !recordPath_.empty() && WriteSessionRecord(recordPath_, record_);
	}

	FileEntry* Session::SlotLocked(Workspace slot, const std::string& key) noexcept
	{
		switch (slot)
		{
		case Workspace::Desktop: return record_.desktop ? &*record_.desktop : nullptr;
		case Workspace::Whiteboard: return record_.whiteboard ? &*record_.whiteboard : nullptr;
		default:
			for (auto& entry : record_.presentations)
				if (entry.key == key) return &entry.file;
			return nullptr;
		}
	}

	bool Session::RegisterDesktopFile(const std::wstring& path, const FileIdentity& identity) noexcept
	{
		if (!Active()) return false;
		try
		{
			std::scoped_lock guard(mutex_);
			record_.desktop = FileEntry{ path, identity, std::nullopt };
			record_.desktopCleared = false;
			return WriteLocked();
		}
		catch (...) { return false; }
	}

	bool Session::RegisterWhiteboardFile(const std::wstring& path, const FileIdentity& identity) noexcept
	{
		if (!Active()) return false;
		try
		{
			std::scoped_lock guard(mutex_);
			record_.whiteboard = FileEntry{ path, identity, std::nullopt };
			return WriteLocked();
		}
		catch (...) { return false; }
	}

	bool Session::RegisterPresentationFile(const std::string& key, const std::wstring& path,
		const FileIdentity& identity) noexcept
	{
		if (!Active()) return false;
		try
		{
			std::scoped_lock guard(mutex_);
			if (FileEntry* existing = SlotLocked(Workspace::Presentation, key))
				*existing = FileEntry{ path, identity, std::nullopt };
			else record_.presentations.push_back({ key, FileEntry{ path, identity, std::nullopt } });
			return WriteLocked();
		}
		catch (...) { return false; }
	}

	bool Session::ClearDesktopFile(uint64_t clearGeneration) noexcept
	{
		if (!Active()) return false;
		try
		{
			std::scoped_lock guard(mutex_);
			record_.desktop.reset();
			record_.desktopCleared = true;
			if (clearGeneration > record_.desktopClearGeneration) record_.desktopClearGeneration = clearGeneration;
			// 上一区间快照只对紧邻的 Clear 有效；该次 Clear 的快照可能已先登记（两个 worker 无先后保证）。
			if (record_.desktopPreviousInterval && record_.desktopPreviousGeneration != record_.desktopClearGeneration)
				record_.desktopPreviousInterval.reset();
			return WriteLocked();
		}
		catch (...) { return false; }
	}

	// 返回 true 表示记录有变化、需要写盘。
	bool Session::ApplyDesktopPreviousLocked(const std::wstring& path, std::optional<uint64_t> clearGeneration)
	{
		if (!clearGeneration)
		{
			if (record_.desktopPreviousInterval && record_.desktopPreviousGeneration == record_.desktopClearGeneration)
				return false;
			clearGeneration = record_.desktopClearGeneration;
		}
		if (*clearGeneration < record_.desktopClearGeneration) return false; // 更早一次 Clear 的快照，已过时
		// 快照登记可能先于该次 Clear 的 Rotate 落盘：代次前进说明活动区间文件是清前的，同一次写盘里一并撤下，
		// 否则两次写盘之间崩溃会把已清掉的墨迹当作当前区间恢复。
		if (*clearGeneration > record_.desktopClearGeneration)
		{
			record_.desktop.reset();
			record_.desktopCleared = true;
		}
		record_.desktopClearGeneration = *clearGeneration;
		record_.desktopPreviousInterval = path;
		record_.desktopPreviousGeneration = *clearGeneration;
		return true;
	}

	bool Session::RegisterDesktopPreviousInterval(const std::wstring& path, std::optional<uint64_t> clearGeneration) noexcept
	{
		if (!Active() || path.empty()) return false;
		try
		{
			std::scoped_lock guard(mutex_);
			const auto before = record_;
			if (!ApplyDesktopPreviousLocked(path, clearGeneration)) return true;
			if (WriteLocked()) return true;
			record_ = before;
			return false;
		}
		catch (...) { return false; }
	}

	void Session::PublishDesktopPreviousInterval(const std::wstring& path, uint64_t clearGeneration) noexcept
	{
		if (!Active() || path.empty()) return;
		try
		{
			std::scoped_lock guard(previousMutex_);
			pendingPrevious_.emplace(path, clearGeneration);
		}
		catch (...) { return; }
		publishSerial_.fetch_add(1, std::memory_order_acq_rel);
		workerCondition_.notify_one();
	}

	uint64_t Session::DesktopClearGeneration() noexcept
	{
		try
		{
			std::scoped_lock guard(mutex_);
			return record_.desktopClearGeneration;
		}
		catch (...) { return 0; }
	}

	bool Session::RegisterPending(Workspace slot, const std::string& presentationKey,
		const FileIdentity& identity, const PendingWrite& pending) noexcept
	{
		if (!Active()) return false;
		try
		{
			std::scoped_lock guard(mutex_);
			FileEntry* file = SlotLocked(slot, presentationKey);
			if (!file) return false;
			// 写盘失败时内存回到写前状态，与盘上一致（之后的整份重写不会带出未落盘的登记）。
			const auto previousIdentity = file->identity;
			const auto previousPending = file->pending;
			file->identity = identity;
			file->pending = pending;
			if (WriteLocked()) return true;
			file->identity = previousIdentity;
			file->pending = previousPending;
			return false;
		}
		catch (...) { return false; }
	}

	bool Session::ClearPending(Workspace slot, const std::string& presentationKey, const FileIdentity& identity) noexcept
	{
		if (!Active()) return false;
		try
		{
			std::scoped_lock guard(mutex_);
			FileEntry* file = SlotLocked(slot, presentationKey);
			if (!file) return false;
			// 没有登记、身份也没变：不重复写盘（开关关闭时每次整页写入都会走到这里）。
			if (!file->pending && file->identity == identity) return true;
			// 写盘失败时保留内存中的登记：盘上仍有旧起点，重试不能被上面的“无登记”短路误报成功。
			const auto previousIdentity = file->identity;
			const auto previousPending = file->pending;
			file->identity = identity;
			file->pending.reset();
			if (WriteLocked()) return true;
			file->identity = previousIdentity;
			file->pending = previousPending;
			return false;
		}
		catch (...) { return false; }
	}

	void Session::PublishWorkspace(Workspace workspace) noexcept
	{
		if (!Active()) return;
		workspace_.store(static_cast<uint8_t>(workspace), std::memory_order_release);
		publishSerial_.fetch_add(1, std::memory_order_acq_rel);
		workerCondition_.notify_one();
	}

	void Session::PublishWhiteboardPage(const std::string& pageGuid, uint32_t pageIndex) noexcept
	{
		if (!Active()) return;
		whiteboardPageIndex_.store(pageIndex, std::memory_order_release);
		// 字符串槽只在拿得到锁时更新；拿不到时下一次翻页或 worker 读到的索引仍正确，GUID 稍后补上。
		if (pageMutex_.try_lock())
		{
			try { pendingPageGuid_ = pageGuid; pendingPageGuidIndex_ = pageIndex; }
			catch (...) { pendingPageGuid_.clear(); }
			pageMutex_.unlock();
		}
		publishSerial_.fetch_add(1, std::memory_order_acq_rel);
		workerCondition_.notify_one();
	}

	void Session::PublishRecoverInk(bool enabled) noexcept
	{
		if (!Active()) return;
		recoverInk_.store(enabled ? 1 : 0, std::memory_order_release);
		publishSerial_.fetch_add(1, std::memory_order_acq_rel);
		workerCondition_.notify_one();
	}

	void Session::WorkerLoop() noexcept
	{
		bool lastWriteFailed = false;
		for (;;)
		{
			{
				std::unique_lock guard(workerMutex_);
				// 上次写盘失败时等满 500 ms 再重试，不因序号未推进而空转。
				workerCondition_.wait_for(guard, std::chrono::milliseconds(500), [&]
				{
					return workerStop_ || (!lastWriteFailed &&
						publishSerial_.load(std::memory_order_acquire) != writtenSerial_);
				});
				if (workerStop_) return;
			}
			const uint64_t serial = publishSerial_.load(std::memory_order_acquire);
			if (serial == writtenSerial_) continue;
			try
			{
				std::string pageGuid;
				uint32_t pageGuidIndex = UINT32_MAX;
				{
					std::scoped_lock pageGuard(pageMutex_);
					pageGuid = pendingPageGuid_;
					pageGuidIndex = pendingPageGuidIndex_;
				}
				std::optional<std::pair<std::wstring, uint64_t>> previous;
				{
					std::scoped_lock previousGuard(previousMutex_);
					previous.swap(pendingPrevious_);
				}
				std::scoped_lock guard(mutex_);
				if (previous) (void)ApplyDesktopPreviousLocked(previous->first, previous->second);
				record_.workspace = static_cast<Workspace>(workspace_.load(std::memory_order_acquire));
				const uint32_t pageIndex = whiteboardPageIndex_.load(std::memory_order_acquire);
				record_.whiteboardPageIndex = pageIndex;
				// GUID 槽没跟上索引（绘制线程 try_lock 失败）时不写 GUID，避免与索引错配。
				if (!pageGuid.empty() && pageGuidIndex == pageIndex) record_.whiteboardPageGuid = pageGuid;
				else record_.whiteboardPageGuid.reset();
				const int8_t recover = recoverInk_.load(std::memory_order_acquire);
				if (recover >= 0) record_.recoverInk = recover != 0;
				// 写盘失败不推进序号，500 ms 后重试。
				lastWriteFailed = !WriteLocked();
				if (!lastWriteFailed) writtenSerial_ = serial;
			}
			catch (...) { lastWriteFailed = true; }
		}
	}

	Liveness ProbeSessionLock(const std::wstring& lockPath) noexcept
	{
		// 持有者只共享 DELETE；以读权限打开：被占用即存活，不存在即已结束，其他错误为不确定。
		const HANDLE probe = CreateFileW(lockPath.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (probe != INVALID_HANDLE_VALUE)
		{
			// 文件存在但无人独占（持有者已不在、锁文件残留）：视为已结束。
			CloseHandle(probe);
			return Liveness::Dead;
		}
		const DWORD error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return Liveness::Dead;
		if (error == ERROR_SHARING_VIOLATION) return Liveness::Alive;
		return Liveness::Unknown;
	}

	std::vector<AbandonedSession> ScanAbandonedSessions(const std::wstring& inkRoot,
		const std::wstring& ownName) noexcept
	{
		std::vector<AbandonedSession> sessions;
		try
		{
			const std::wstring directory = SessionDirectory(inkRoot);
			WIN32_FIND_DATAW entry{};
			const HANDLE listing = FindFirstFileW((directory + L"\\*.json").c_str(), &entry);
			if (listing == INVALID_HANDLE_VALUE) return sessions;
			std::vector<std::wstring> names;
			do
			{
				if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				std::wstring name = entry.cFileName;
				if (name.size() <= 5) continue;
				names.push_back(name.substr(0, name.size() - 5));
			} while (FindNextFileW(listing, &entry));
			FindClose(listing);

			for (const auto& base : names)
			{
				if (base == ownName) continue;
				const std::wstring recordPath = directory + L"\\" + base + L".json";
				if (ProbeSessionLock(directory + L"\\" + base + L".lock") != Liveness::Dead) continue;
				auto record = ReadSessionRecord(recordPath);
				if (!record) continue;
				const bool allPresentationsSettled = std::all_of(record->presentations.begin(),
					record->presentations.end(), [&](const PresentationEntry& presentation)
					{
						return std::find(record->settledPresentations.begin(), record->settledPresentations.end(),
							presentation.key) != record->settledPresentations.end();
					});
				// 已结算的桌面状态也参与排序，避免越过 Clear 或已处理的区间借回更旧的墨迹。
				const bool hasDesktopState = record->desktop || record->desktopCleared || record->desktopClearGeneration != 0 ||
					(record->desktopPreviousInterval && record->desktopPreviousGeneration == record->desktopClearGeneration);
				if (record->clean || !record->recoverInk ||
					(!hasDesktopState && (!record->whiteboard || record->settledWhiteboard) && allPresentationsSettled))
				{
					// 已结束且无需恢复的旧记录直接清理；被别的进程认领中的记录跳过删除。
					if (ProbeSessionLock(directory + L"\\" + base + L".claim") == Liveness::Dead)
						DeleteFileW(recordPath.c_str());
					continue;
				}
				sessions.push_back({ recordPath, base, std::move(*record) });
			}
			// 文件名以本地时间开头，按名称倒序即从新到旧。
			std::sort(sessions.begin(), sessions.end(), [](const AbandonedSession& left, const AbandonedSession& right)
			{
				return left.baseName > right.baseName;
			});
		}
		catch (...) {}
		return sessions;
	}

	void* ClaimSession(const std::wstring& inkRoot, const std::wstring& baseName) noexcept
	{
		const HANDLE claim = CreateExclusiveDeleteOnClose(SessionDirectory(inkRoot) + L"\\" + baseName + L".claim");
		return claim == INVALID_HANDLE_VALUE ? nullptr : claim;
	}
}
