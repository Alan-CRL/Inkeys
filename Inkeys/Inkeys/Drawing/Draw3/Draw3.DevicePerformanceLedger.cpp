#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DevicePerformanceLedger.h"
#include "Draw3.DevicePerformanceJson.h"
#include "Draw3.DiagnosticOutput.h"
#include "../../../PptSettingsPersistence.h"

#include <windows.h>

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		using namespace DevicePerformance;

		// 两次写盘的最小间隔：抬笔很频繁，不需要每次空闲都写。
		constexpr std::int64_t kMinWriteIntervalMs = 30000;

		std::int64_t SteadyMs() noexcept
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::int32_t LocalDateToday() noexcept
		{
			SYSTEMTIME local = {};
			GetLocalTime(&local);
			return MakeDate(local.wYear, local.wMonth, local.wDay);
		}

		std::int64_t UnixSecondsNow() noexcept
		{
			return static_cast<std::int64_t>(std::time(nullptr));
		}

		bool ReadWholeFile(const std::wstring& path, std::string& text, bool& missing)
		{
			missing = false;
			const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				const DWORD error = GetLastError();
				missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
				return false;
			}
			LARGE_INTEGER size = {};
			bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 && size.QuadPart <= 16 * 1024 * 1024;
			if (ok)
			{
				text.resize(static_cast<std::size_t>(size.QuadPart));
				std::size_t offset = 0;
				while (ok && offset < text.size())
				{
					DWORD read = 0;
					ok = ReadFile(file, text.data() + offset, static_cast<DWORD>(text.size() - offset), &read, nullptr) != FALSE &&
						read > 0;
					offset += read;
				}
			}
			CloseHandle(file);
			return ok;
		}
	}

	DevicePerformanceLedger::~DevicePerformanceLedger()
	{
		Stop();
	}

	void DevicePerformanceLedger::Start(std::wstring filePath) noexcept
	{
		std::unique_lock lock(mutex_);
		if (started_) return;
		started_ = true;
		stopRequested_ = false;
		// 同一进程可能 Stop→Start（DComp 失败后以 ULW 重启 Host）：内存数据已含历史，同一路径不再重读，否则翻倍。
		loadRequired_ = !(loadedOnce_ && loadedPath_ == filePath);
		filePath_ = std::move(filePath);
		loaded_ = !loadRequired_;
		loadEpoch_ = clearEpoch_;
		if (filePath_.empty())
		{
			loaded_ = true;
			wake_.notify_all();
			return;
		}
		try
		{
			writerActive_ = true;
			writer_ = std::thread([this] { WriterMain(); });
		}
		catch (const std::exception&)
		{
			// 线程创建失败：退化为只在内存记录，看板仍可用。
			writerActive_ = false;
			loaded_ = true;
			filePath_.clear();
			wake_.notify_all();
			DiagnosticOutput::Text(DiagnosticOutput::OutWarn, "[Draw3.DevicePerformance] writer thread unavailable; memory only\n");
		}
	}

	void DevicePerformanceLedger::Stop() noexcept
	{
		std::thread writer;
		{
			std::scoped_lock lock(mutex_);
			if (!started_) return;
			stopRequested_ = true;
			writer = std::move(writer_);
		}
		wake_.notify_all();
		if (writer.joinable()) writer.join();
		std::scoped_lock lock(mutex_);
		started_ = false;
	}

	void DevicePerformanceLedger::SetCurrentSignature(const Signature& signature) noexcept
	{
		std::scoped_lock lock(mutex_);
		const Signature normalized = NormalizeSignature(signature);
		if (currentSignature_ && *currentSignature_ == normalized) return;
		currentSignature_ = normalized;
		try { RebuildReportLocked(); }
		catch (...) {}
	}

	void DevicePerformanceLedger::Submit(const Signature& signature, const Counters& counters,
		std::uint64_t segmentEpoch) noexcept
	{
		// 移交发生在进入空闲等待前：绘制已停，写盘线程可以落盘。
		busy_.store(false, std::memory_order_relaxed);
		if (counters[0].frames == 0)
		{
			// 在锁内通知：写盘线程在“检查忙碌”与“进入等待”之间持锁，锁外通知可能丢失。
			std::scoped_lock lock(mutex_);
			wake_.notify_all();
			return;
		}
		try
		{
			const std::int32_t date = LocalDateToday();
			const std::int64_t now = UnixSecondsNow();
			std::scoped_lock lock(mutex_);
			// 锁内复查：移交途中关闭了记录，或段开始后用户清除了数据，都整段丢弃。
			if (!recordingEnabled_.load(std::memory_order_relaxed)) return;
			if (segmentEpoch != kAnyEpoch && segmentEpoch != clearEpoch_) return;
			if (!MergeTally(store_, signature, date, now, counters)) return;
			RebuildReportLocked();
			MarkDirtyLocked();
		}
		catch (...)
		{
			// 只在空闲时调用；分配失败等异常丢弃本段，绘制线程不受影响。
			DiagnosticOutput::Text(DiagnosticOutput::OutWarn, "[Draw3.DevicePerformance] submit dropped a segment\n");
		}
	}

	void DevicePerformanceLedger::SetDisplayRefreshHz(std::uint32_t refreshHz) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (displayRefreshHz_ == refreshHz) return;
		displayRefreshHz_ = refreshHz;
		try { RebuildReportLocked(); }
		catch (...) {}
	}

	void DevicePerformanceLedger::SetRecordingEnabled(bool enabled) noexcept
	{
		if (recordingEnabled_.exchange(enabled, std::memory_order_relaxed) == enabled) return;
		{
			std::scoped_lock lock(mutex_);
			try { RebuildReportLocked(); }
			catch (...) {}
		}
		wake_.notify_all(); // 重新开启后让写盘线程处理关闭前留下的脏数据
	}

	void DevicePerformanceLedger::Clear() noexcept
	{
		std::wstring deleteNow;
		{
			std::scoped_lock lock(mutex_);
			store_ = {};
			++clearEpoch_; // 尚未完成的启动加载结果与清除前开始的绘制段都作废
			clearEpochPublished_.store(clearEpoch_, std::memory_order_relaxed);
			dirty_ = false;
			deleteFilePending_ = !filePath_.empty() && writerActive_;
			deleteNow = !filePath_.empty() && !writerActive_ ? filePath_ : std::wstring();
			try { RebuildReportLocked(); }
			catch (...) {}
		}
		// 账本已停止（关机时设置页比 Host 晚关）：没有写盘线程，由调用方线程直接删除。
		if (!deleteNow.empty()) (void)DeleteFileW(deleteNow.c_str());
		wake_.notify_all();
	}

	bool DevicePerformanceLedger::Snapshot(std::uint64_t knownRevision, Report& out) const
	{
		std::scoped_lock lock(mutex_);
		if (report_.revision == knownRevision) return false;
		out = report_;
		return true;
	}

	Store DevicePerformanceLedger::StoreCopy() const
	{
		std::scoped_lock lock(mutex_);
		return store_;
	}

	bool DevicePerformanceLedger::WaitLoaded(int timeoutMs) const
	{
		std::unique_lock lock(mutex_);
		if (timeoutMs < 0)
		{
			wake_.wait(lock, [&] { return loaded_; });
			return true;
		}
		return wake_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return loaded_; });
	}

	void DevicePerformanceLedger::RebuildReportLocked()
	{
		Report next = BuildReport(store_, currentSignature_ ? &*currentSignature_ : nullptr);
		next.recordingEnabled = recordingEnabled_.load(std::memory_order_relaxed);
		next.displayRefreshHz = displayRefreshHz_;
		next.sessionFrameRate = currentSignature_ ? currentSignature_->frameRate : 0;
		next.revision = ++revision_;
		report_ = next;
	}

	void DevicePerformanceLedger::MarkDirtyLocked()
	{
		if (filePath_.empty()) return;
		dirty_ = true;
		wake_.notify_all();
	}

	void DevicePerformanceLedger::WriterMain()
	{
		std::wstring path;
		std::uint64_t loadEpoch = 0;
		bool loadRequired = true;
		{
			std::scoped_lock lock(mutex_);
			path = filePath_;
			// 代次在 Start 时（创建本线程之前）已固定：线程启动后、读取前的清除也能让加载结果作废。
			loadEpoch = loadEpoch_;
			loadRequired = loadRequired_;
			if (loadRequired) writeBlocked_ = false; // 每次重新加载重新判定，瞬时故障后重启 Host 可恢复写盘
		}
		if (loadRequired)
		{
			// 1. 加载历史文件，与启动后已 Submit 的内存数据相加合并。
			try
			{
				std::string text;
				bool missing = false;
				if (ReadWholeFile(path, text, missing))
				{
					Store loaded;
					std::size_t dropped = 0;
					const ParseResult result = ParseStore(text, loaded, &dropped);
					if (result == ParseResult::Ok)
					{
						std::scoped_lock lock(mutex_);
						if (loadEpoch == clearEpoch_)
						{
							MergeStore(store_, loaded);
							if (currentSignature_) PruneSignatures(store_, &*currentSignature_);
							RebuildReportLocked();
						}
						if (dropped > 0)
							DiagnosticOutput::Printf(DiagnosticOutput::OutWarn,
								"[Draw3.DevicePerformance] load dropped %zu invalid item(s)\n", dropped);
					}
					else
					{
						// 坏文件：记录开启时改名为 .bad 保留，从空开始。记录关闭时不动文件（关闭即不写文件）；
						// 两种情况下只要原件没有成功备份，本会话就禁止写盘，避免覆盖未备份的原件。
						const bool recording = recordingEnabled_.load(std::memory_order_relaxed);
						const std::wstring bad = path + L".bad";
						const bool moved = recording && MoveFileExW(path.c_str(), bad.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
						if (!moved)
						{
							std::scoped_lock lock(mutex_);
							writeBlocked_ = true;
						}
						DiagnosticOutput::Printf(DiagnosticOutput::OutWarn,
							"[Draw3.DevicePerformance] unreadable file (%s); renamed=%u\n",
							result == ParseResult::WrongSchema ? "schema" : "malformed", moved ? 1u : 0u);
					}
				}
				else if (!missing)
				{
					// 文件存在却读不出（被占用等瞬时错误）：不能当作空文件，否则写盘会用本会话数据覆盖全部历史。
					std::scoped_lock lock(mutex_);
					writeBlocked_ = true;
					DiagnosticOutput::Text(DiagnosticOutput::OutWarn, "[Draw3.DevicePerformance] read failed; writes disabled this session\n");
				}
			}
			catch (...)
			{
				std::scoped_lock lock(mutex_);
				writeBlocked_ = true;
				DiagnosticOutput::Text(DiagnosticOutput::OutWarn, "[Draw3.DevicePerformance] load failed; writes disabled this session\n");
			}
		}
		{
			std::scoped_lock lock(mutex_);
			loaded_ = true;
			if (!writeBlocked_)
			{
				loadedOnce_ = true;
				loadedPath_ = path;
			}
		}
		wake_.notify_all();

		// 2. 写盘：只在绘制空闲（busy_ 为 false）且距上次写盘至少 30 s 时写；Stop 时把最后的脏数据写完再退出。
		//    绘制中途（新一段已开始累加）绝不写盘，等下一次空闲移交唤醒。
		std::unique_lock lock(mutex_);
		for (;;)
		{
			const bool stopping = stopRequested_;
			if (deleteFilePending_)
			{
				deleteFilePending_ = false;
				lock.unlock();
				const bool deleted = DeleteFileW(path.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
				if (!deleted)
					DiagnosticOutput::Printf(DiagnosticOutput::OutWarn,
						"[Draw3.DevicePerformance] delete failed; error=%lu\n", GetLastError());
				lock.lock();
				continue;
			}
			const bool idle = !busy_.load(std::memory_order_relaxed);
			// 关闭记录时不写文件（包括退出时），重新开启后再写。
			const bool recording = recordingEnabled_.load(std::memory_order_relaxed);
			const std::int64_t sinceWrite = SteadyMs() - lastWriteMs_;
			if (writeBlocked_) dirty_ = false; // 加载失败的会话只在内存记录
			if (dirty_ && recording && (stopping || (idle && sinceWrite >= kMinWriteIntervalMs)))
			{
				dirty_ = false;
				Store copy;
				bool copied = true;
				try { copy = store_; }
				catch (...) { copied = false; }
				lock.unlock();
				bool written = false;
				if (copied)
				{
					try { written = Inkeys::PptSettings::WriteAtomically(path, SerializeStore(copy)); }
					catch (...) {}
				}
				lock.lock();
				lastWriteMs_ = SteadyMs();
				if (!written)
				{
					DiagnosticOutput::Text(DiagnosticOutput::OutWarn, "[Draw3.DevicePerformance] write failed; will retry\n");
					// 失败后至少 30 s 再试；停止时不再重试，避免退出卡住。
					if (!stopping) dirty_ = true;
				}
				continue;
			}
			if (stopping)
			{
				// 仍持锁：之后的 Clear 看到写盘线程已不在，会在调用线程同步删除，不会被本线程写回旧数据。
				writerActive_ = false;
				break;
			}
			if (dirty_ && idle && recording)
				wake_.wait_for(lock, std::chrono::milliseconds((std::max)(std::int64_t{ 1 }, kMinWriteIntervalMs - sinceWrite)));
			else
				wake_.wait(lock); // 不脏、绘制中或记录关闭：等 Submit / Clear / 开关 / Stop 唤醒
		}
	}
}
