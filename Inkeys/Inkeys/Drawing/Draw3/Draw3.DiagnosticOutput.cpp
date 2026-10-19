#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Draw3.DiagnosticOutput.h"
#include <windows.h>
#include <process.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
#include <string>
#include <thread>
#endif

namespace Inkeys::Drawing::Draw3::DiagnosticOutput
{
	namespace
	{
		struct LastErrorGuard
		{
			DWORD saved = GetLastError();
			~LastErrorGuard() { SetLastError(saved); }
		};
		struct Record
		{
			char text[TextCapacity];
			std::size_t length = 0;
			Stream stream = Stream::Output;
			bool mirror = false;
		};
		struct State
		{
			SRWLOCK queueLock = SRWLOCK_INIT;
			std::array<Record, RecordCapacity> records;
			std::size_t head = 0, count = 0;
			HANDLE wake = nullptr, thread = nullptr;
			HANDLE sinks[2]{};
			bool consoles[2]{};
			std::atomic<unsigned> available = 0;
			std::atomic<bool> accepting = true, closing = false, cancel = false;
			std::atomic<std::uint64_t> accepted = 0, written = 0, full = 0, truncated = 0;
			std::atomic<std::uint64_t> errors = 0, writeCalls = 0, cancels = 0, notFound = 0;
			std::atomic<std::size_t> queued = 0, peak = 0;
			std::atomic<DWORD> workerId = 0;
			std::atomic<bool> writing = false;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			// 仅自检借用；测试保持事件存活到真实 worker 已退出。
			HANDLE beforeWrite = nullptr, resumeWrite = nullptr;
			std::atomic<bool> pauseWrite = false;
			std::size_t writeChunkLimit = TextCapacity;
			bool zeroWrite = false;
#endif
		};
		// 路由小状态具有进程寿命；隔离 worker 不持有任何画布、窗口或 CRT 日志对象。
		SRWLOCK routeLock = SRWLOCK_INIT;
		State* active = nullptr;
		State* isolated = nullptr;
		std::atomic<bool> lifecycleBusy = false;
		std::atomic<bool> accepting = false;
		std::atomic<unsigned> available = 0;
		std::atomic<std::uint64_t> contention = 0, closed = 0;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		int failInitialization = 0;
#endif
		unsigned Mask(Stream stream) noexcept { return stream == Stream::Output ? 1u : 2u; }
		std::size_t Index(Stream stream) noexcept { return stream == Stream::Output ? 0 : 1; }
		void MarkTruncated(char* text, std::size_t& length) noexcept
		{
			constexpr char suffix[] = " [truncated]\r\n";
			length = (std::min)(length, TextCapacity - sizeof(suffix));
			std::memcpy(text + length, suffix, sizeof(suffix));
			length += sizeof(suffix) - 1;
		}
		Statistics ReadStatistics(const State* state) noexcept
		{
			Statistics result{};
			if (!state) return result;
			result.accepted = state->accepted.load(std::memory_order_relaxed);
			result.written = state->written.load(std::memory_order_relaxed);
			result.droppedFull = state->full.load(std::memory_order_relaxed);
			result.droppedContention = contention.load(std::memory_order_relaxed);
			result.droppedClosing = closed.load(std::memory_order_relaxed);
			result.truncated = state->truncated.load(std::memory_order_relaxed);
			result.sinkErrors = state->errors.load(std::memory_order_relaxed);
			result.writeCalls = state->writeCalls.load(std::memory_order_relaxed);
			result.cancelAttempts = state->cancels.load(std::memory_order_relaxed);
			result.cancelNotFound = state->notFound.load(std::memory_order_relaxed);
			result.queued = state->queued.load(std::memory_order_relaxed);
			result.peakQueued = state->peak.load(std::memory_order_relaxed);
			result.workerThreadId = state->workerId.load(std::memory_order_relaxed);
			result.writing = state->writing.load(std::memory_order_relaxed);
			return result;
		}
		bool WriteRecord(State& state, const Record& record) noexcept
		{
			const std::size_t index = Index(record.stream);
			HANDLE sink = state.sinks[index];
			if (sink)
			{
				std::size_t offset = 0;
				while (offset < record.length)
				{
					if (state.cancel.load(std::memory_order_acquire)) return false;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
					if (state.pauseWrite.exchange(false, std::memory_order_acq_rel))
					{
						SetEvent(state.beforeWrite);
						WaitForSingleObject(state.resumeWrite, INFINITE);
					}
#endif
					DWORD written = 0;
					DWORD requested = static_cast<DWORD>(record.length - offset);
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
					requested = static_cast<DWORD>((std::min)(static_cast<std::size_t>(requested), state.writeChunkLimit));
#endif
					state.writing.store(true, std::memory_order_release);
					state.writeCalls.fetch_add(1, std::memory_order_relaxed);
					BOOL success;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
					if (state.zeroWrite) success = TRUE;
					else
#endif
					success = state.consoles[index]
						? WriteConsoleA(sink, record.text + offset,
							requested, &written, nullptr)
						: WriteFile(sink, record.text + offset,
							requested, &written, nullptr);
					state.writing.store(false, std::memory_order_release);
					if (!success || written == 0)
					{
						state.errors.fetch_add(1, std::memory_order_relaxed);
						CloseHandle(sink);
						state.sinks[index] = nullptr;
						state.available.fetch_and(~Mask(record.stream), std::memory_order_relaxed);
						available.fetch_and(~Mask(record.stream), std::memory_order_relaxed);
						return false;
					}
					offset += written;
				}
			}
			if (state.cancel.load(std::memory_order_acquire)) return false;
			if (record.mirror) OutputDebugStringA(record.text);
			return sink != nullptr || record.mirror;
		}
		unsigned __stdcall Worker(void* context) noexcept
		{
			auto& state = *static_cast<State*>(context);
			state.workerId.store(GetCurrentThreadId(), std::memory_order_release);
			Record record;
			std::uint64_t reported = 0;
			for (;;)
			{
				if (state.cancel.load(std::memory_order_acquire)) break;
				bool found = false, empty = false;
				if (TryAcquireSRWLockExclusive(&state.queueLock))
				{
					empty = state.count == 0;
					if (!empty)
					{
						const Record& queued = state.records[state.head];
						record.length = queued.length;
						record.stream = queued.stream;
						record.mirror = queued.mirror;
						std::memcpy(record.text, queued.text, queued.length + 1);
						state.head = (state.head + 1) % RecordCapacity;
						--state.count;
						state.queued.store(state.count, std::memory_order_relaxed);
						found = true;
					}
					ReleaseSRWLockExclusive(&state.queueLock);
				}
				if (!found)
				{
					if (empty && state.closing.load(std::memory_order_acquire)) break;
					WaitForSingleObject(state.wake, empty ? INFINITE : 1);
					continue;
				}
				if (WriteRecord(state, record))
				{
					state.written.fetch_add(1, std::memory_order_relaxed);
					const auto stats = ReadStatistics(&state);
					const auto dropped = stats.droppedFull + stats.droppedContention + stats.droppedClosing;
					if (dropped != reported && !state.cancel.load(std::memory_order_acquire))
					{
						reported = dropped;
						Record summary;
						summary.stream = record.stream;
						const int length = std::snprintf(summary.text, sizeof(summary.text),
							"[Draw3DiagnosticOutput] dropped=%llu full=%llu contention=%llu closing=%llu truncated=%llu\r\n",
							static_cast<unsigned long long>(dropped),
							static_cast<unsigned long long>(stats.droppedFull),
							static_cast<unsigned long long>(stats.droppedContention),
							static_cast<unsigned long long>(stats.droppedClosing),
							static_cast<unsigned long long>(stats.truncated));
						if (length > 0)
						{
							summary.length = static_cast<std::size_t>(length);
							(void)WriteRecord(state, summary);
						}
					}
				}
			}
			for (HANDLE& sink : state.sinks)
			{
				if (sink) CloseHandle(sink);
				sink = nullptr;
			}
			return 0;
		}
		HANDLE DuplicateSink(HANDLE source) noexcept
		{
			if (!source || source == INVALID_HANDLE_VALUE) return nullptr;
			SetLastError(ERROR_SUCCESS);
			if (GetFileType(source) == FILE_TYPE_UNKNOWN && GetLastError() != ERROR_SUCCESS) return nullptr;
			HANDLE copy = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &copy,
				0, FALSE, DUPLICATE_SAME_ACCESS)) return nullptr;
			return copy;
		}
	}

	bool Available(Stream stream, bool mirror) noexcept
	{
		return accepting.load(std::memory_order_acquire) &&
			(available.load(std::memory_order_relaxed) & (Mask(stream) | (mirror ? 4u : 0u))) != 0;
	}

	void Write(Stream stream, const char* text, std::size_t length, bool mirror) noexcept
	{
		LastErrorGuard errorGuard;
		if (!text || length == 0) return;
		if (!Available(stream, mirror)) { closed.fetch_add(1, std::memory_order_relaxed); return; }
		if (!TryAcquireSRWLockShared(&routeLock)) { contention.fetch_add(1, std::memory_order_relaxed); return; }
		State* state = active;
		if (!state || !state->accepting.load(std::memory_order_acquire))
			closed.fetch_add(1, std::memory_order_relaxed);
		else if (!TryAcquireSRWLockExclusive(&state->queueLock))
			contention.fetch_add(1, std::memory_order_relaxed);
		else
		{
			if (state->count == RecordCapacity) state->full.fetch_add(1, std::memory_order_relaxed);
			else
			{
				Record& record = state->records[(state->head + state->count) % RecordCapacity];
				record.stream = stream;
				record.mirror = mirror;
				record.length = (std::min)(length, TextCapacity - 1);
				std::memcpy(record.text, text, record.length);
				record.text[record.length] = '\0';
				if (length >= TextCapacity)
				{
					MarkTruncated(record.text, record.length);
					state->truncated.fetch_add(1, std::memory_order_relaxed);
				}
				++state->count;
				state->queued.store(state->count, std::memory_order_relaxed);
				state->peak.store((std::max)(state->count, state->peak.load(std::memory_order_relaxed)), std::memory_order_relaxed);
				state->accepted.fetch_add(1, std::memory_order_relaxed);
				// 路由读保护延续到通知结束，Stop 才能安全回收 event。
				SetEvent(state->wake);
			}
			ReleaseSRWLockExclusive(&state->queueLock);
		}
		ReleaseSRWLockShared(&routeLock);
	}

	void Text(Stream stream, const char* text, bool mirror) noexcept
	{
		if (!text || !Available(stream, mirror)) return;
		Write(stream, text, strnlen_s(text, TextCapacity), mirror);
	}
	void Printf(Stream stream, const char* format, ...) noexcept
	{
		LastErrorGuard errorGuard;
		if (!format || !Available(stream)) return;
		char text[TextCapacity];
		va_list arguments;
		va_start(arguments, format);
		const int result = std::vsnprintf(text, sizeof(text), format, arguments);
		va_end(arguments);
		if (result <= 0) return;
		// Write 按原长度决定截断，但只读取定长缓冲中实际存在的前缀。
		Write(stream, text, static_cast<std::size_t>(result));
	}
	void Wake() noexcept
	{
		LastErrorGuard errorGuard;
		if (!TryAcquireSRWLockShared(&routeLock)) return;
		if (active && active->accepting.load(std::memory_order_acquire)) SetEvent(active->wake);
		ReleaseSRWLockShared(&routeLock);
	}

	Session::~Session() { (void)Stop(); }
	bool Session::Start() noexcept
	{
		LastErrorGuard errorGuard;
		return StartHandles(GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE));
	}
	bool Session::StartHandles(void* output, void* error) noexcept
	{
		LastErrorGuard errorGuard;
		if (state_) return result_ != StopResult::IsolatedPending;
		// 已运行或隔离后直接拒绝，避免再次等待尚未静止的旧路由读者。
		if (lifecycleBusy.load(std::memory_order_acquire)) return false;
		AcquireSRWLockExclusive(&routeLock);
		if (lifecycleBusy.load(std::memory_order_acquire)) { ReleaseSRWLockExclusive(&routeLock); return false; }
		HANDLE sinks[2]{};
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		if (failInitialization != 1)
#endif
		{
			sinks[0] = DuplicateSink(output);
			sinks[1] = DuplicateSink(error);
		}
		unsigned mask = (sinks[0] ? 1u : 0u) | (sinks[1] ? 2u : 0u) | (IsDebuggerPresent() ? 4u : 0u);
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		if (failInitialization == 1) mask = 0;
#endif
		State* state = nullptr;
		if (mask)
		{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (failInitialization != 2)
#endif
				state = new(std::nothrow) State;
		}
		if (state)
		{
			state->sinks[0] = sinks[0]; state->sinks[1] = sinks[1];
			state->available.store(mask, std::memory_order_relaxed);
			for (std::size_t index = 0; index < 2; ++index)
			{
				DWORD mode = 0;
				state->consoles[index] = sinks[index] && GetConsoleMode(sinks[index], &mode);
			}
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
			if (failInitialization != 3)
#endif
				state->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (state->wake)
			{
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
				if (failInitialization != 4)
#endif
					state->thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Worker, state, 0, nullptr));
			}
		}
		if (!state || !state->thread)
		{
			for (HANDLE sink : sinks) if (sink) CloseHandle(sink);
			if (state && state->wake) CloseHandle(state->wake);
			delete state;
			ReleaseSRWLockExclusive(&routeLock);
			return false;
		}
		contention.store(0, std::memory_order_relaxed); closed.store(0, std::memory_order_relaxed);
		state_ = active = state;
		lifecycleBusy.store(true, std::memory_order_release);
		stopped_ = {};
		result_ = StopResult::Stopped;
		available.store(mask, std::memory_order_relaxed);
		accepting.store(true, std::memory_order_release);
		ReleaseSRWLockExclusive(&routeLock);
		return true;
	}

	StopResult Session::Stop() noexcept
	{
		LastErrorGuard errorGuard;
		if (!state_ || result_ == StopResult::IsolatedPending) return result_;
		auto* state = static_cast<State*>(state_);
		const ULONGLONG deadline = GetTickCount64() + 1000;
		accepting.store(false, std::memory_order_release);
		available.store(0, std::memory_order_relaxed);
		state->accepting.store(false, std::memory_order_release);
		bool removed = false;
		while (GetTickCount64() < deadline)
		{
			if (TryAcquireSRWLockExclusive(&routeLock))
			{
				if (active == state) active = nullptr;
				ReleaseSRWLockExclusive(&routeLock);
				removed = true;
				break;
			}
			Sleep(1);
		}
		state->closing.store(true, std::memory_order_release);
		SetEvent(state->wake);
		const auto remaining = [&]() noexcept -> DWORD
		{
			const ULONGLONG now = GetTickCount64();
			return now < deadline ? static_cast<DWORD>(deadline - now) : 0;
		};
		DWORD wait = WaitForSingleObject(state->thread, (std::min)(DWORD{100}, remaining()));
		if (wait != WAIT_OBJECT_0)
		{
			state->cancel.store(true, std::memory_order_release);
			SetEvent(state->wake);
			while (remaining())
			{
				state->cancels.fetch_add(1, std::memory_order_relaxed);
				if (!CancelSynchronousIo(state->thread) && GetLastError() == ERROR_NOT_FOUND)
					state->notFound.fetch_add(1, std::memory_order_relaxed);
				wait = WaitForSingleObject(state->thread, (std::min)(DWORD{10}, remaining()));
				if (wait == WAIT_OBJECT_0) break;
			}
		}
		stopped_ = ReadStatistics(state);
		if (wait == WAIT_OBJECT_0 && removed)
		{
			CloseHandle(state->thread);
			CloseHandle(state->wake);
			delete state;
			state_ = nullptr;
			result_ = StopResult::Stopped;
			lifecycleBusy.store(false, std::memory_order_release);
		}
		else
		{
			// 期限兜底只保留这一份独立状态；进程内不再启动第二 worker，也不等待析构。
			state->cancel.store(true, std::memory_order_release);
			isolated = state;
			result_ = StopResult::IsolatedPending;
		}
		return result_;
	}
	Statistics Session::Snapshot() const noexcept
	{
		return state_ ? ReadStatistics(static_cast<State*>(state_)) : stopped_;
	}

	Line::Line(Stream stream, bool mirror) noexcept
		: previousError_(GetLastError()), stream_(stream), mirror_(mirror), enabled_(Available(stream, mirror)) {}
	Line::~Line()
	{
		if (enabled_ && length_)
		{
			if (truncated_) { MarkTruncated(text_, length_); Write(stream_, text_, TextCapacity, mirror_); }
			else Write(stream_, text_, length_, mirror_);
		}
		SetLastError(previousError_);
	}
	void Line::Append(const char* text, std::size_t length) noexcept
	{
		if (!enabled_ || !text || !length) return;
		const auto size = (std::min)(length, TextCapacity - 1 - length_);
		std::memcpy(text_ + length_, text, size);
		length_ += size;
		text_[length_] = '\0';
		truncated_ = truncated_ || size != length;
	}
	Line& Line::operator<<(const char* text) noexcept
	{
		if (enabled_ && text) Append(text, strnlen_s(text, TextCapacity));
		return *this;
	}
	Line& Line::operator<<(char value) noexcept { Append(&value, 1); return *this; }
	Line& Line::operator<<(std::string_view text) noexcept { Append(text.data(), text.size()); return *this; }
	Line& Line::operator<<(std::ios_base& (*format)(std::ios_base&)) noexcept
	{
		if (format == std::hex) base_ = 16;
		else if (format == std::dec) base_ = 10;
		return *this;
	}
	Line& Line::operator<<(std::ostream& (*format)(std::ostream&)) noexcept
	{
		if (format == static_cast<std::ostream& (*)(std::ostream&)>(std::endl<char, std::char_traits<char>>)) Append("\r\n", 2);
		return *this; // flush 不执行外部 I/O；析构统一提交这一条记录。
	}

#if !INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	int RunProductionTest(void (*)() noexcept) noexcept { return 2; }
#else
	int RunProductionTest(void (*helpers)() noexcept) noexcept
	{
		int failures = 0;
		const auto check = [&](bool value, const char* name)
		{
			if (!value) { ++failures; std::fprintf(stderr, "[Draw3DiagnosticOutput] FAIL %s\n", name); }
		};
		const auto waitUntil = [](auto&& condition, DWORD budget = 3000) noexcept
		{
			const ULONGLONG until = GetTickCount64() + budget;
			do { if (condition()) return true; Sleep(1); } while (GetTickCount64() < until);
			return condition();
		};
		struct Pipe
		{
			HANDLE read = nullptr, write = nullptr;
			Pipe() { (void)CreatePipe(&read, &write, nullptr, 4096); }
			~Pipe() { if (read) CloseHandle(read); if (write) CloseHandle(write); }
			void CloseWriter() { if (write) CloseHandle(write); write = nullptr; }
			void CloseReader() { if (read) CloseHandle(read); read = nullptr; }
		};
		const auto readAvailable = [](HANDLE pipe)
		{
			std::string result;
			DWORD bytes = 0;
			while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes, nullptr) && bytes)
			{
				char text[4096]; DWORD read = 0;
				if (!ReadFile(pipe, text, (std::min)(bytes, static_cast<DWORD>(sizeof(text))), &read, nullptr) || !read) break;
				result.append(text, read);
			}
			return result;
		};
		try
		{
			// 所有测试调用同一生产路由、队列、writer 和 Stop；报告输出不走被测管道。
			{
				Session empty;
				const bool debugger = IsDebuggerPresent() != FALSE;
				check(empty.StartHandles(nullptr, nullptr) == debugger, "no-sink has no worker without debugger mirror");
				Text(Stream::Output, "disabled");
				check(empty.Stop() == (debugger ? StopResult::Stopped : StopResult::Disabled) && !Available(), "disabled repeat stop");
			}
			for (int failure = 1; failure <= 4; ++failure)
			{
				Pipe pipe;
				failInitialization = failure;
				Session session;
				check(!session.StartHandles(pipe.write, pipe.write), "injected initialization failure closes state");
				check(session.Stop() == StopResult::Disabled && !Available(), "failed start does not route");
				failInitialization = 0;
			}
			{
				Pipe output, error;
				Session session;
				check(session.StartHandles(output.write, error.write), "normal pipe start");
				// 实际 WriteFile 限成7字节，检验生产循环能正确完成部分写。
				auto* state = static_cast<State*>(session.state_);
				state->writeChunkLimit = 7;
				const HANDLE before = state->beforeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				const HANDLE resume = state->resumeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				state->pauseWrite.store(true, std::memory_order_release);
				SetLastError(0x1234);
				Printf(Stream::Output, "[normal] text=中文 value=%d\n", 42);
				check(GetLastError() == 0x1234, "printf preserves LastError");
				check(WaitForSingleObject(before, 3000) == WAIT_OBJECT_0, "normal writer outside queue lock");
				Line(Stream::Error) << "[error] hex=0x" << std::hex << 255u << std::dec << " value=" << 1.25 << std::endl;
				SetEvent(resume);
				check(waitUntil([&] { return session.Snapshot().written == 2; }), "normal stdout stderr complete");
				check(session.Stop() == StopResult::Stopped && session.Stop() == StopResult::Stopped, "normal repeated stop");
				check(readAvailable(output.read) == "[normal] text=中文 value=42\n", "stdout bytes unchanged");
				check(readAvailable(error.read) == "[error] hex=0xff value=1.25\r\n", "stderr bytes unchanged");
				CloseHandle(before); CloseHandle(resume);
			}
			{
				wchar_t root[MAX_PATH]{}, path[MAX_PATH]{};
				check(GetTempPathW(MAX_PATH, root) && GetTempFileNameW(root, L"d3o", 0, path), "temp file create");
				HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
				if (file != INVALID_HANDLE_VALUE)
				{
					DWORD bytes = 0;
					WriteFile(file, "prefix\n", 7, &bytes, nullptr);
					Session session;
					check(session.StartHandles(file, file), "file start");
					auto* state = static_cast<State*>(session.state_);
					const HANDLE before = state->beforeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
					const HANDLE resume = state->resumeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
					state->pauseWrite.store(true, std::memory_order_release);
					Text(Stream::Output, "suffix\n");
					check(WaitForSingleObject(before, 3000) == WAIT_OBJECT_0, "file writer outside queue lock");
					if (helpers) helpers();
					const std::string longText(8192, 't');
					Printf(Stream::Output, "%s", longText.c_str());
					Line() << std::string_view(longText);
					SetEvent(resume);
					check(waitUntil([&] { const auto stats = session.Snapshot(); return stats.written == stats.accepted; }), "file and real diagnostic helpers written");
					check(session.Stop() == StopResult::Stopped && session.Snapshot().truncated == 2, "file stop and explicit truncation counts");
					CloseHandle(before); CloseHandle(resume);
					SetFilePointer(file, 0, nullptr, FILE_BEGIN);
					std::string text(65536, '\0');
					ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &bytes, nullptr);
					text.resize(bytes);
					check(text.starts_with("prefix\nsuffix\n") && text.find("[truncated]") != std::string::npos, "inherited file position and truncation marker retained");
					if (helpers) check(text.find("[CURSOR_TRACE]") != std::string::npos &&
						text.find("[diagnostic-helper] failed. HRESULT=0x80004005") != std::string::npos &&
						text.find("[PptSync] stage=diagnostic-helper") != std::string::npos, "real Cursor HRESULT stderr helper bytes");
					CloseHandle(file);
				}
				else check(false, "temp file open");
				if (path[0]) DeleteFileW(path);
			}
			for (const Stream stream : { Stream::Output, Stream::Error })
			{
				Pipe pipe;
				Session session;
				std::array<char, 4096> fill{}; fill.fill('p');
				DWORD bytes = 0;
				check(WriteFile(pipe.write, fill.data(), static_cast<DWORD>(fill.size()), &bytes, nullptr) && bytes == fill.size(), "fill real pipe");
				check(session.StartHandles(stream == Stream::Output ? pipe.write : nullptr,
					stream == Stream::Error ? pipe.write : nullptr), "backpressure start");
				Text(stream, "[blocked] real OS write\n");
				check(waitUntil([&] { return session.Snapshot().writing; }), "real pipe write entered");
				Sleep(30);
				check(session.Snapshot().writing && session.Snapshot().written == 0, "write still pending with live reader");
				const ULONGLONG begin = GetTickCount64();
				std::array<std::thread, 4> producers;
				for (auto& producer : producers) producer = std::thread([&]
				{
					for (unsigned index = 0; index < 2048; ++index) Printf(stream, "[flood] i=%u\n", index);
				});
				for (auto& producer : producers) producer.join();
				const auto saturated = session.Snapshot();
				check(GetTickCount64() - begin < 5000 && saturated.droppedFull > 0 &&
					saturated.peakQueued == RecordCapacity, "producer returns with bounded saturated queue");
				const ULONGLONG stop = GetTickCount64();
				check(session.Stop() == StopResult::Stopped, "real pending pipe cancelled and worker signaled");
				const auto stopped = session.Snapshot();
				check(GetTickCount64() - stop < 1500 && stopped.cancelAttempts > 0 &&
					stopped.workerThreadId != GetCurrentThreadId(), "stop absolute budget and worker identity");
				std::fprintf(stderr, "[Draw3DiagnosticOutput] backpressure stream=%s accepted=%llu full=%llu contention=%llu cancel=%llu stop_ms=%llu\n",
					stream == Stream::Output ? "stdout" : "stderr",
					static_cast<unsigned long long>(stopped.accepted), static_cast<unsigned long long>(stopped.droppedFull),
					static_cast<unsigned long long>(stopped.droppedContention), static_cast<unsigned long long>(stopped.cancelAttempts),
					static_cast<unsigned long long>(GetTickCount64() - stop));
			}
			{
				Pipe pipe;
				Session session;
				check(session.StartHandles(pipe.write, nullptr), "recovery start");
				std::array<char, 4095> large{}; large.fill('r'); large.back() = '\n';
				for (unsigned index = 0; index < 2048; ++index) Write(Stream::Output, large.data(), large.size());
				check(waitUntil([&] { return session.Snapshot().writing && session.Snapshot().droppedFull > 0; }), "recovery has real pending output");
				std::string received;
				std::thread reader([&]
				{
					char text[4096]; DWORD count = 0;
					while (ReadFile(pipe.read, text, sizeof(text), &count, nullptr) && count) received.append(text, count);
				});
				check(waitUntil([&] { return session.Snapshot().queued == 0; }), "reader resumes worker");
				Text(Stream::Output, "[recovery] new-record\n");
				const auto accepted = session.Snapshot().accepted;
				check(waitUntil([&] { return session.Snapshot().written == accepted; }), "new record after recovery written");
				check(session.Stop() == StopResult::Stopped, "resumed reader drains normally");
				pipe.CloseWriter(); reader.join();
				check(received.find("[recovery] new-record\n") != std::string::npos &&
					received.find("[Draw3DiagnosticOutput] dropped=") != std::string::npos, "new bytes and drop summary visible");
			}
			{
				Pipe broken, normal;
				Session session;
				check(session.StartHandles(broken.write, normal.write), "broken pipe start");
				broken.CloseReader(); Text(Stream::Output, "broken\n");
				check(waitUntil([&] { return session.Snapshot().sinkErrors == 1; }), "real broken pipe handled once");
				Text(Stream::Error, "other-stream\n");
				check(waitUntil([&] { return session.Snapshot().written == 1; }), "broken sink leaves other stream alive");
				check(session.Stop() == StopResult::Stopped && readAvailable(normal.read) == "other-stream\n", "broken sink stop");
			}
			{
				Pipe pipe;
				Session session;
				check(session.StartHandles(pipe.write, nullptr), "zero progress start");
				static_cast<State*>(session.state_)->zeroWrite = true;
				Text(Stream::Output, "zero-progress fault injection\n");
				check(waitUntil([&] { return session.Snapshot().sinkErrors == 1; }), "zero progress closes sink without spinning");
				check(session.Stop() == StopResult::Stopped && session.Snapshot().writeCalls == 1, "zero progress bounded stop");
			}
			{
				Pipe pipe;
				Session session;
				check(session.StartHandles(pipe.write, nullptr), "producer stop race start");
				std::atomic<bool> run = true;
				std::array<std::thread, 4> producers;
				for (auto& producer : producers) producer = std::thread([&]
				{
					while (run.load(std::memory_order_acquire)) Printf(Stream::Output, "race tid=%lu\n", GetCurrentThreadId());
				});
				Sleep(30);
				check(session.Stop() == StopResult::Stopped, "concurrent producer stop closes route before freeing event");
				run.store(false, std::memory_order_release);
				for (auto& producer : producers) producer.join();
				check(session.Stop() == StopResult::Stopped && !Available(), "stopped route remains closed");
			}
			{
				Pipe pipe;
				std::array<char, 4096> fill{}; DWORD bytes = 0;
				WriteFile(pipe.write, fill.data(), static_cast<DWORD>(fill.size()), &bytes, nullptr);
				Session session;
				check(session.StartHandles(pipe.write, nullptr), "cancel submission race start");
				auto* state = static_cast<State*>(session.state_);
				state->beforeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				state->resumeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				state->pauseWrite.store(true, std::memory_order_release);
				Text(Stream::Output, "after-last-check\n");
				check(WaitForSingleObject(state->beforeWrite, 3000) == WAIT_OBJECT_0, "worker paused after last stop check");
				StopResult result = StopResult::Disabled;
				std::thread stopper([&] { result = session.Stop(); });
				check(waitUntil([&] { return state->notFound.load(std::memory_order_acquire) > 0; }, 500), "initial cancel has ERROR_NOT_FOUND");
				const HANDLE before = state->beforeWrite, resume = state->resumeWrite;
				SetEvent(resume); stopper.join();
				check(result == StopResult::Stopped && session.Snapshot().cancelNotFound > 0 &&
					session.Snapshot().writeCalls > 0, "repeated cancel catches late real WriteFile");
				CloseHandle(before); CloseHandle(resume);
			}
			{
				// 不可取消等待只验证隔离寿命，不冒充产品自然故障或 pipe 取消证据。
				Pipe pipe;
				Session session;
				check(session.StartHandles(pipe.write, nullptr), "deadline isolation start");
				auto* state = static_cast<State*>(session.state_);
				state->beforeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				state->resumeWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				state->pauseWrite.store(true, std::memory_order_release);
				Text(Stream::Output, "isolated state stays valid\n");
				check(WaitForSingleObject(state->beforeWrite, 3000) == WAIT_OBJECT_0, "uncancellable seam reached");
				const ULONGLONG start = GetTickCount64();
				check(session.Stop() == StopResult::IsolatedPending && GetTickCount64() - start < 1500, "deadline returns isolated pending");
				Session second;
				check(!second.StartHandles(pipe.write, nullptr) && !Available(), "isolated state prevents second worker");
				SetEvent(state->resumeWrite);
				check(WaitForSingleObject(state->thread, 3000) == WAIT_OBJECT_0, "isolated worker finishes after owner stop without UAF");
				CloseHandle(state->beforeWrite); CloseHandle(state->resumeWrite);
				check(session.Stop() == StopResult::IsolatedPending, "isolated repeat stop never joins");
			}
		}
		catch (...)
		{
			failInitialization = 0;
			check(false, "unexpected test exception");
		}
		std::fprintf(stderr, "[Draw3DiagnosticOutput] result=%s failures=%d production_queue=1 real_pipe=1 hwnd=0\n",
			failures ? "FAIL" : "PASS", failures);
		return failures ? 1 : 0;
	}
#endif
}
