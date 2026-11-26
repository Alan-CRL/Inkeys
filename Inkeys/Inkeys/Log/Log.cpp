#include "Log.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <vector>

// 与 IdtMain.h 保持一致：header-only spdlog 的文件名类型必须在所有包含它的翻译单元中相同。
#ifndef SPDLOG_WCHAR_FILENAMES
#define SPDLOG_WCHAR_FILENAMES
#endif
#include <spdlog/details/log_msg.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace Inkeys::Log
{
	namespace
	{
		// 两块固定前后台缓冲，放在静态存储区：静态初始化后即可写入，不在运行期分配。
		constexpr std::size_t BufferBytes = 2u * 1024u * 1024u;
		// Debug/Info 只可用前 75%，剩余空间留给 Warn 及以上。
		constexpr std::size_t NormalLimit = BufferBytes / 4u * 3u;
		constexpr std::size_t MaxTextBytes = 16u * 1024u;
		constexpr DWORD BatchIntervalMilliseconds = 1000;
		// 正常退出尽量排空到写线程结束：上限取 12 秒，长于任何正常磁盘延迟，又短于 SetOffSignal 的 15 秒退出监督，
		// 监督未能建立（降级分支）时也不会让进程无限卡在退出阶段。
		constexpr DWORD StopWaitMilliseconds = 12000;
		constexpr DWORD ExitGuardWaitMilliseconds = 1000;
		constexpr DWORD EmergencyWaitMilliseconds = 500;

		struct RecordHeader
		{
			std::uint32_t size;      // 含头部与对齐填充的整条记录字节数
			std::uint32_t textSize;
			std::uint8_t level;
			std::uint8_t module;
			std::uint8_t channel;    // 0xFF = 普通分级日志
			std::uint8_t flags;
			std::uint32_t threadId;
			std::int64_t timeTicks;  // system_clock 原始计数，在调用线程采集
		};
		static_assert(sizeof(RecordHeader) % 8 == 0);

		enum class State : int { Buffering, Accepting, Discard, Stopped };

		alignas(16) std::byte bufferStorage[2][BufferBytes];

		// 以下三项只在 bufferLock 内读写；生产者临界区只有容量检查与 memcpy。
		SRWLOCK bufferLock = SRWLOCK_INIT;
		std::byte* frontBuffer = bufferStorage[0];
		std::size_t frontUsed = 0;
		// 后台缓冲只属于写线程。
		std::byte* backBuffer = bufferStorage[1];

		std::atomic<int> state{ static_cast<int>(State::Buffering) };
		std::atomic<int> threshold{ static_cast<int>(Threshold::Info) };
		std::atomic<std::uint32_t> channelMask{ 0 };
		std::atomic<std::uint64_t> dropped[5]{};

		std::atomic<HANDLE> wakeEvent{ nullptr };
		HANDLE writerThread = nullptr;
		std::atomic<DWORD> writerThreadId{ 0 };
		std::atomic<bool> stopRequested{ false };
		// FlushAndStop 与退出兜底可能并发进入，只允许一方执行停止流程。
		std::atomic<bool> stopClaimed{ false };

		std::atomic<int> retentionDays{ 7 };
		std::atomic<int> retentionMaxMb{ 0 };
		std::atomic<bool> retentionPending{ false };

		// 崩溃路径的落盘请求/确认序号；写线程处理完一批并 flush 后推进确认值。
		std::atomic<std::uint64_t> flushRequested{ 0 };
		std::atomic<std::uint64_t> flushCompleted{ 0 };

		// 目录与当前文件在 Start 中写一次，之后只读。
		std::wstring logDirectory;
		std::wstring currentFile;
		std::atomic<bool> directoryReady{ false };

		// 只由写线程使用（Start 写会话头时写线程尚未启动）；由写线程退出时释放，
		// 不交给静态析构，避免退出兜底等待超时后仍在写的线程访问已析构对象。
		spdlog::sinks::basic_file_sink_st* sink = nullptr;
		std::chrono::steady_clock::time_point lastFlush{};

		constexpr const char* ModuleNames[] =
		{
			"Main", "Launch", "Config", "I18n", "Window", "UI3", "Bar", "Setting", "Whiteboard",
			"PageControl", "SelectionOverlay", "Freeze", "Magnifier", "Ppt", "PptCOM",
			"Draw3", "Input", "Haptics", "Update", "Plugin", "State", "Shutdown", "Log", "UInk",
		};
		static_assert(std::size(ModuleNames) == static_cast<std::size_t>(Module::Count));
		constexpr const char* ChannelNames[] = { "TouchArea", "PptCOM", "Draw3", "Haptics", "Cursor", "CanvasPan" };
		static_assert(std::size(ChannelNames) == static_cast<std::size_t>(Channel::Count));
		constexpr const char* LevelNames[] = { "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT " };
		constexpr const char* ThresholdNames[] = { "Debug", "Info", "Warn", "Error", "Off" };

		const char* ModuleName(std::uint8_t module) noexcept
		{
			return module < std::size(ModuleNames) ? ModuleNames[module] : "Unknown";
		}

		spdlog::level::level_enum ToSpdlog(std::uint8_t level) noexcept
		{
			switch (static_cast<Level>(level))
			{
			case Level::Debug: return spdlog::level::debug;
			case Level::Info: return spdlog::level::info;
			case Level::Warn: return spdlog::level::warn;
			case Level::Error: return spdlog::level::err;
			default: return spdlog::level::critical;
			}
		}

		// %* ：大写定宽等级名。
		class LevelFlag final : public spdlog::custom_flag_formatter
		{
		public:
			void format(const spdlog::details::log_msg& message, const std::tm&, spdlog::memory_buf_t& dest) override
			{
				const char* name = LevelNames[4];
				switch (message.level)
				{
				case spdlog::level::debug: name = LevelNames[0]; break;
				case spdlog::level::info: name = LevelNames[1]; break;
				case spdlog::level::warn: name = LevelNames[2]; break;
				case spdlog::level::err: name = LevelNames[3]; break;
				default: break;
				}
				dest.append(name, name + std::strlen(name));
			}
			std::unique_ptr<custom_flag_formatter> clone() const override { return std::make_unique<LevelFlag>(); }
		};

		// %& ：调试通道名。通道名借用 source.funcname 携带（本格式不使用源码位置）。
		class ChannelFlag final : public spdlog::custom_flag_formatter
		{
		public:
			void format(const spdlog::details::log_msg& message, const std::tm&, spdlog::memory_buf_t& dest) override
			{
				if (!message.source.funcname) return;
				dest.push_back('[');
				dest.append(message.source.funcname, message.source.funcname + std::strlen(message.source.funcname));
				dest.push_back(']');
			}
			std::unique_ptr<custom_flag_formatter> clone() const override { return std::make_unique<ChannelFlag>(); }
		};

		std::unique_ptr<spdlog::formatter> MakeLineFormatter()
		{
			auto formatter = std::make_unique<spdlog::pattern_formatter>();
			formatter->add_flag<LevelFlag>('*').add_flag<ChannelFlag>('&')
				.set_pattern("[%Y-%m-%d %H:%M:%S.%e][%*][%n][T%t]%& %v");
			return formatter;
		}

		std::string ToUtf8(std::wstring_view text)
		{
			if (text.empty()) return {};
			const int length = static_cast<int>((std::min)(text.size(), static_cast<std::size_t>(INT_MAX / 4)));
			const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
			if (required <= 0) return {};
			std::string result(static_cast<std::size_t>(required), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), required, nullptr, nullptr);
			return result;
		}

		// 日志入口不得改变调用方的 Win32 LastError（多处业务先失败再记录错误码）。
		struct LastErrorGuard
		{
			DWORD saved = GetLastError();
			~LastErrorGuard() { SetLastError(saved); }
		};

		void DestroySink() noexcept
		{
			delete sink;
			sink = nullptr;
		}

		void WakeWriter() noexcept
		{
			if (const HANDLE event = wakeEvent.load(std::memory_order_acquire)) SetEvent(event);
		}

		void CountDrop(Level level) noexcept
		{
			const auto index = (std::min)(static_cast<std::size_t>(level), std::size(dropped) - 1);
			dropped[index].fetch_add(1, std::memory_order_relaxed);
		}

		bool AcceptingState(int value) noexcept
		{
			return value == static_cast<int>(State::Buffering) || value == static_cast<int>(State::Accepting);
		}

#ifdef DRAW3_TESTING
		constinit std::atomic<UInkTestSink> uinkTestSink{nullptr};
#endif

		// 生产者入口：时间戳/线程 ID 在锁外采集，锁内只有容量检查与 memcpy；不做 I/O、分配或格式化。
		bool Enqueue(Level level, Module module, std::uint8_t channel, std::string_view text, bool tryOnly) noexcept
		{
			if (!AcceptingState(state.load(std::memory_order_acquire))) return false;
			std::size_t textSize = (std::min)(text.size(), MaxTextBytes);
			// 超长截断退回到 UTF-8 字符边界，避免把多字节字符切成非法序列；截断标记由写线程补上。
			if (textSize < text.size())
				while (textSize > 0 && (static_cast<unsigned char>(text[textSize]) & 0xC0u) == 0x80u) --textSize;
#ifdef DRAW3_TESTING
			if (module == Module::UInk)
				if (const auto observe = uinkTestSink.load(std::memory_order_acquire))
					observe(level, text.substr(0, textSize));
#endif
			const std::size_t recordSize = (sizeof(RecordHeader) + textSize + 7u) & ~static_cast<std::size_t>(7u);
			RecordHeader header{};
			header.size = static_cast<std::uint32_t>(recordSize);
			header.textSize = static_cast<std::uint32_t>(textSize);
			header.level = static_cast<std::uint8_t>(level);
			header.module = static_cast<std::uint8_t>(module);
			header.channel = channel;
			header.flags = static_cast<std::uint8_t>(textSize < text.size() ? 1 : 0);
			header.threadId = GetCurrentThreadId();
			header.timeTicks = static_cast<std::int64_t>(std::chrono::system_clock::now().time_since_epoch().count());
			const std::size_t limit = level >= Level::Warn ? BufferBytes : NormalLimit;

			if (tryOnly)
			{
				// 热路径绝不等待也不自旋：拿不到锁直接丢弃计数。
				if (!TryAcquireSRWLockExclusive(&bufferLock))
				{
					CountDrop(level);
					return false;
				}
			}
			else AcquireSRWLockExclusive(&bufferLock);
			// 停止状态在锁内复查：FlushAndStop 在锁内发布 Stopped 后不再有记录进入最终批次之外。
			const bool accepting = AcceptingState(state.load(std::memory_order_acquire));
			const std::size_t before = frontUsed;
			const bool fits = accepting && before + recordSize <= limit;
			if (fits)
			{
				std::byte* target = frontBuffer + before;
				std::memcpy(target, &header, sizeof(header));
				if (textSize) std::memcpy(target + sizeof(header), text.data(), textSize);
				frontUsed = before + recordSize;
			}
			ReleaseSRWLockExclusive(&bufferLock);

			if (!accepting) return false;
			if (!fits)
			{
				CountDrop(level);
				WakeWriter();
				return false;
			}
			// Warn+ 立即唤醒落盘；其余只在越过半满时唤醒，平时靠 1 秒超时批处理，避免逐条线程切换。
			if (level >= Level::Warn || (before < BufferBytes / 2 && before + recordSize >= BufferBytes / 2))
				WakeWriter();
			return true;
		}

		void WriteLine(spdlog::level::level_enum level, std::uint8_t module, const char* channelName,
			DWORD threadId, spdlog::log_clock::time_point time, std::string_view text)
		{
			spdlog::source_loc source{};
			source.funcname = channelName;
			spdlog::details::log_msg message(time, source, ModuleName(module), level,
				spdlog::string_view_t(text.data(), text.size()));
			message.thread_id = threadId;
			sink->log(message);
		}

		// 写线程自身的诊断行直接写 sink，不再回到前台缓冲。
		void WriteDirect(Level level, std::string_view text)
		{
			if (!sink || !Enabled(level)) return;
			WriteLine(ToSpdlog(static_cast<std::uint8_t>(level)), static_cast<std::uint8_t>(Module::Logging), nullptr,
				GetCurrentThreadId(), std::chrono::system_clock::now(), text);
		}

		// 交换前后台缓冲（O(1) 持锁），之后在锁外解析并写文件。返回本批是否含 Warn 及以上。
		bool DrainOnce()
		{
			std::size_t used = 0;
			AcquireSRWLockExclusive(&bufferLock);
			std::swap(frontBuffer, backBuffer);
			used = frontUsed;
			frontUsed = 0;
			ReleaseSRWLockExclusive(&bufferLock);

			bool urgent = false;
			std::uint64_t counts[5]{};
			bool anyDropped = false;
			for (std::size_t index = 0; index < std::size(dropped); ++index)
			{
				counts[index] = dropped[index].exchange(0, std::memory_order_relaxed);
				anyDropped = anyDropped || counts[index] != 0;
			}
			if (sink && anyDropped)
			{
				// 丢弃计数行在批次开头输出，让查看者知道之前的日志不完整；该行不受阈值过滤。
				const auto line = fmt::format("dropped debug={} info={} warn={} error={} critical={}",
					counts[0], counts[1], counts[2], counts[3], counts[4]);
				WriteLine(spdlog::level::warn, static_cast<std::uint8_t>(Module::Logging), nullptr,
					GetCurrentThreadId(), std::chrono::system_clock::now(), line);
				urgent = true;
			}

			std::size_t offset = 0;
			while (sink && offset + sizeof(RecordHeader) <= used)
			{
				RecordHeader header;
				std::memcpy(&header, backBuffer + offset, sizeof(header));
				if (header.size < sizeof(RecordHeader) || offset + header.size > used) break;
				const char* text = reinterpret_cast<const char*>(backBuffer + offset + sizeof(header));
				const char* channelName = header.channel < std::size(ChannelNames) ? ChannelNames[header.channel] : nullptr;
				const auto time = spdlog::log_clock::time_point(spdlog::log_clock::duration(header.timeTicks));
				const auto level = ToSpdlog(header.level);
				std::string_view body(text, header.textSize);
				// 多行正文逐行复用同一条记录的元数据，保证每行都有完整标头、可逐行筛选。
				for (;;)
				{
					const std::size_t newline = body.find('\n');
					std::string_view line = body.substr(0, newline);
					if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
					const bool last = newline == std::string_view::npos;
					if (last && (header.flags & 1u))
					{
						// 被截断的记录在末行显式标注，查看者能识别内容缺失。
						std::string marked(line);
						marked += " ...[truncated]";
						WriteLine(level, header.module, channelName, header.threadId, time, marked);
					}
					else if (!last || !line.empty() || body.empty())
						WriteLine(level, header.module, channelName, header.threadId, time, line);
					if (last) break;
					body.remove_prefix(newline + 1);
					// 末尾换行不再产生空行。
					if (body.empty()) break;
				}
				if (header.level >= static_cast<std::uint8_t>(Level::Warn)) urgent = true;
				offset += header.size;
			}
			return urgent;
		}

		struct LogFileName
		{
			std::int64_t time = 0; // 本地时间按秒编码的可比较键
			unsigned suffix = 0;
		};

		bool Digits(const std::wstring& text, std::size_t begin, std::size_t count, int& value) noexcept
		{
			value = 0;
			for (std::size_t index = begin; index < begin + count; ++index)
			{
				if (text[index] < L'0' || text[index] > L'9') return false;
				value = value * 10 + (text[index] - L'0');
			}
			return true;
		}

		// 只匹配本规范命名：YYYY-MM-DDTHH-MM-SS(_N).log；不使用 std::regex，避免异常与开销。
		bool ParseLogFileName(const std::wstring& name, LogFileName& parsed) noexcept
		{
			constexpr std::size_t baseLength = 19; // YYYY-MM-DDTHH-MM-SS
			if (name.size() < baseLength + 4) return false;
			if (name.compare(name.size() - 4, 4, L".log") != 0) return false;
			if (name[4] != L'-' || name[7] != L'-' || name[10] != L'T' || name[13] != L'-' || name[16] != L'-') return false;
			int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
			if (!Digits(name, 0, 4, year) || !Digits(name, 5, 2, month) || !Digits(name, 8, 2, day)
				|| !Digits(name, 11, 2, hour) || !Digits(name, 14, 2, minute) || !Digits(name, 17, 2, second))
				return false;
			if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return false;
			std::tm local{};
			local.tm_year = year - 1900; local.tm_mon = month - 1; local.tm_mday = day;
			local.tm_hour = hour; local.tm_min = minute; local.tm_sec = second; local.tm_isdst = -1;
			const std::time_t time = std::mktime(&local);
			if (time == static_cast<std::time_t>(-1)) return false;
			parsed.time = static_cast<std::int64_t>(time);
			parsed.suffix = 0;
			const std::size_t tailBegin = baseLength, tailEnd = name.size() - 4;
			if (tailBegin == tailEnd) return true;
			if (name[tailBegin] != L'_' || tailEnd - tailBegin < 2 || tailEnd - tailBegin > 6) return false;
			int suffix = 0;
			if (!Digits(name, tailBegin + 1, tailEnd - tailBegin - 1, suffix)) return false;
			parsed.suffix = static_cast<unsigned>(suffix);
			return true;
		}

		// 清理：排除当前会话文件；先删超期，再按最旧优先删到总量（含当前文件）不超限。
		void RunRetention()
		{
			const int days = retentionDays.load(std::memory_order_relaxed);
			const int maxMb = retentionMaxMb.load(std::memory_order_relaxed);
			if (days <= 0 && maxMb <= 0) return;
			namespace fs = std::filesystem;
			struct Candidate { fs::path path; LogFileName key; std::uintmax_t size; };
			std::vector<Candidate> candidates;
			std::uintmax_t total = 0;
			std::error_code ec;
			for (fs::directory_iterator it(logDirectory, ec), end; !ec && it != end; it.increment(ec))
			{
				std::error_code entryError;
				if (!it->is_regular_file(entryError) || entryError) continue;
				const std::wstring name = it->path().filename().wstring();
				LogFileName key;
				if (!ParseLogFileName(name, key)) continue;
				const auto size = it->file_size(entryError);
				if (entryError) continue;
				total += size;
				if (_wcsicmp(it->path().wstring().c_str(), currentFile.c_str()) == 0) continue;
				candidates.push_back({ it->path(), key, size });
			}
			std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
				{
					return left.key.time != right.key.time ? left.key.time < right.key.time : left.key.suffix < right.key.suffix;
				});
			const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
			const std::int64_t cutoff = days > 0 ? now - static_cast<std::int64_t>(days) * 24 * 60 * 60 : INT64_MIN;
			const std::uintmax_t limit = maxMb > 0 ? static_cast<std::uintmax_t>(maxMb) * 1024u * 1024u : 0;
			for (const auto& candidate : candidates)
			{
				const bool expired = candidate.key.time < cutoff;
				const bool oversize = limit != 0 && total > limit;
				// 列表按时间升序：一旦既未超期又未超限，后续文件只会更新，不必继续。
				if (!expired && !oversize) break;
				std::error_code removeError;
				if (fs::remove(candidate.path, removeError) && !removeError) total -= (std::min)(total, candidate.size);
				else
				{
					// 删除失败（多为被占用）只记 Debug，不重试。
					WriteDirect(Level::Debug, fmt::format("retention remove failed file={} error={}",
						ToUtf8(candidate.path.filename().wstring()), removeError.value()));
				}
			}
		}

		void FlushSink()
		{
			if (!sink) return;
			// 先记时间：flush 抛异常（磁盘错误）时也不能让写线程的剩余等待变成 0 而空转。
			lastFlush = std::chrono::steady_clock::now();
			sink->flush();
		}

		DWORD WINAPI WriterMain(void*)
		{
			writerThreadId.store(GetCurrentThreadId(), std::memory_order_release);
			for (;;)
			{
				const HANDLE event = wakeEvent.load(std::memory_order_acquire);
				// 等待到“上次落盘 + 1 秒”为止，而不是每轮固定 1 秒：中途被唤醒后也不会把普通日志的落盘推迟到近 2 秒。
				const auto sinceFlush = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - lastFlush).count();
				const DWORD wait = sinceFlush >= static_cast<long long>(BatchIntervalMilliseconds) ? 0
					: BatchIntervalMilliseconds - static_cast<DWORD>((std::max)(0LL, static_cast<long long>(sinceFlush)));
				WaitForSingleObject(event, wait);
				const bool stopping = stopRequested.load(std::memory_order_acquire);
				const std::uint64_t flushTarget = flushRequested.load(std::memory_order_acquire);
				try
				{
					bool urgent = DrainOnce();
					if (retentionPending.exchange(false, std::memory_order_acq_rel))
					{
						RunRetention();
						urgent = DrainOnce() || urgent;
					}
					// Warn+ 批次立即落盘；其余至多每 1 秒落盘一次。
					if (urgent || stopping || flushTarget != flushCompleted.load(std::memory_order_relaxed)
						|| std::chrono::steady_clock::now() - lastFlush >= std::chrono::milliseconds(BatchIntervalMilliseconds))
						FlushSink();
				}
				catch (...) {}
				flushCompleted.store(flushTarget, std::memory_order_release);
				if (stopping) break;
			}
			try { DestroySink(); }
			catch (...) {}
			return 0;
		}

		std::wstring TimezoneText(std::time_t now)
		{
			std::tm local{};
			if (localtime_s(&local, &now) != 0) return L"unknown";
			const std::int64_t offsetSeconds = static_cast<std::int64_t>(_mkgmtime(&local)) - static_cast<std::int64_t>(now);
			const std::int64_t absolute = offsetSeconds < 0 ? -offsetSeconds : offsetSeconds;
			wchar_t text[32]{};
			swprintf_s(text, L"UTC%c%02lld:%02lld", offsetSeconds < 0 ? L'-' : L'+', absolute / 3600, absolute / 60 % 60);
			return text;
		}

		std::string ChannelMaskText(std::uint32_t mask)
		{
			std::string text;
			for (std::size_t index = 0; index < std::size(ChannelNames); ++index)
			{
				if (!(mask & (1u << index))) continue;
				if (!text.empty()) text.push_back(',');
				text += ChannelNames[index];
			}
			return text.empty() ? "none" : text;
		}

		void WriteSessionHeader(const SessionInfo& info, const std::tm& local, std::time_t now)
		{
			// 会话头使用纯文本格式，位于文件最前，不受阈值影响。
			sink->set_pattern("%v");
			const auto raw = [](std::string_view line)
				{
					spdlog::details::log_msg message(spdlog::string_view_t{}, spdlog::level::info,
						spdlog::string_view_t(line.data(), line.size()));
					sink->log(message);
				};
			const std::string build = info.buildConfiguration.empty() ? std::string("unknown") : ToUtf8(info.buildConfiguration);
			raw("==== Inkeys session start ====");
			raw(fmt::format("time={:04}-{:02}-{:02}T{:02}:{:02}:{:02} timezone={}",
				local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
				ToUtf8(TimezoneText(now))));
			raw(fmt::format("edition={} version={} build={} buildTime={}",
				ToUtf8(info.editionDate), ToUtf8(info.editionVersion), build, ToUtf8(info.buildTime)));
			raw(fmt::format("arch={} targetArch={} os={} pid={} userId={}",
				ToUtf8(info.architecture), ToUtf8(info.targetArchitecture), ToUtf8(info.osVersion),
				GetCurrentProcessId(), ToUtf8(info.userId)));
			raw(fmt::format("path={} launch={}", ToUtf8(info.rootPath), ToUtf8(info.launch)));
			// 等级/通道/清理参数此时尚未读取配置，由 ApplyConfig 写入 session.config 行补全。
			raw("==============================");
			sink->set_formatter(MakeLineFormatter());
		}

		// 原子预留文件名：CREATE_NEW 保证同秒多进程也不会写同一文件。
		// 不标 noexcept：内部字符串分配失败需交给 Start 的 catch 进入丢弃模式，而不是 terminate。
		bool ReserveLogFile(const std::wstring& directory, const std::tm& local, std::wstring& path)
		{
			wchar_t base[32]{};
			swprintf_s(base, L"%04d-%02d-%02dT%02d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1,
				local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
			for (unsigned suffix = 0; suffix < 100; ++suffix)
			{
				wchar_t name[48]{};
				if (suffix == 0) swprintf_s(name, L"%s.log", base);
				else swprintf_s(name, L"%s_%u.log", base, suffix);
				std::wstring candidate = directory + L"\\" + name;
				const HANDLE file = CreateFileW(candidate.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file != INVALID_HANDLE_VALUE)
				{
					CloseHandle(file);
					path = std::move(candidate);
					return true;
				}
				if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
			}
			return false;
		}

		void EnterDiscard(const wchar_t* reason) noexcept
		{
			AcquireSRWLockExclusive(&bufferLock);
			if (AcceptingState(state.load(std::memory_order_relaxed)))
			{
				state.store(static_cast<int>(State::Discard), std::memory_order_release);
				frontUsed = 0;
			}
			ReleaseSRWLockExclusive(&bufferLock);
			if (reason) OutputDebugStringW(reason);
		}

		void StopWriter(DWORD waitMilliseconds) noexcept
		{
			if (stopClaimed.exchange(true, std::memory_order_acq_rel)) return;
			// 先在锁内发布 Stopped：此后生产者在锁内复查即放弃，写线程的最终批次即全部剩余记录。
			AcquireSRWLockExclusive(&bufferLock);
			const bool hadWriter = writerThread != nullptr;
			if (!hadWriter) frontUsed = 0;
			state.store(static_cast<int>(State::Stopped), std::memory_order_release);
			ReleaseSRWLockExclusive(&bufferLock);
			if (!hadWriter) return;
			stopRequested.store(true, std::memory_order_release);
			WakeWriter();
			// 不能在写线程自身上等待；超时后放弃等待，不强杀线程。
			if (GetCurrentThreadId() != writerThreadId.load(std::memory_order_acquire))
				WaitForSingleObject(writerThread, waitMilliseconds);
			CloseHandle(writerThread);
			writerThread = nullptr;
		}

		// 已确认致命启动失败会直接从 wWinMain 返回；静态析构阶段兜底排空，等待有界。
		struct ExitGuard
		{
			~ExitGuard() { StopWriter(ExitGuardWaitMilliseconds); }
		} exitGuard;
	}

#ifdef DRAW3_TESTING
	void SetUInkTestSink(UInkTestSink observe) noexcept
	{
		uinkTestSink.store(observe, std::memory_order_release);
	}
#endif

	bool Enabled(Level level) noexcept
	{
		if (level >= Level::Critical) return true;
		return static_cast<int>(level) >= threshold.load(std::memory_order_relaxed);
	}

	bool ChannelEnabled(Channel channel) noexcept
	{
		const auto index = static_cast<std::uint32_t>(channel);
		return index < 32 && (channelMask.load(std::memory_order_relaxed) & (1u << index)) != 0;
	}

	void SetThreshold(Threshold value) noexcept
	{
		threshold.store((std::clamp)(static_cast<int>(value), 0, 4), std::memory_order_relaxed);
	}

	void SetChannelMask(std::uint32_t mask) noexcept
	{
		channelMask.store(mask, std::memory_order_relaxed);
	}

	bool TryWrite(Level level, Module module, std::optional<Channel> channel, std::string_view text) noexcept
	{
		if (channel ? !ChannelEnabled(*channel) : !Enabled(level)) return false;
		const LastErrorGuard lastError;
		return Enqueue(level, module, channel ? static_cast<std::uint8_t>(*channel) : Detail::NoChannel, text, true);
	}

	void RecordDropped(Level level) noexcept
	{
		CountDrop(level);
	}

	namespace Detail
	{
		void Submit(Level level, Module module, std::uint8_t channel, bool tryOnly,
			fmt::string_view format, fmt::format_args args) noexcept
		{
			const LastErrorGuard lastError;
			try
			{
				// 栈上 500B 内联缓冲；超长消息才在调用线程分配，仍在取锁之前完成。
				fmt::memory_buffer buffer;
				fmt::vformat_to(fmt::appender(buffer), format, args);
				(void)Enqueue(level, module, channel, std::string_view(buffer.data(), buffer.size()), tryOnly);
			}
			catch (...) { CountDrop(level); }
		}

		void SubmitWide(Level level, Module module, std::uint8_t channel, bool tryOnly,
			std::wstring_view text) noexcept
		{
			const LastErrorGuard lastError;
			try
			{
				const std::string utf8 = ToUtf8(text);
				(void)Enqueue(level, module, channel, utf8, tryOnly);
			}
			catch (...) { CountDrop(level); }
		}
	}

	bool Start(const SessionInfo& info) noexcept
	{
		if (state.load(std::memory_order_acquire) != static_cast<int>(State::Buffering)) return false;
		try
		{
			SetThreshold(info.threshold);

			std::wstring directory = info.rootPath;
			if (!directory.empty() && directory.back() != L'\\' && directory.back() != L'/') directory.push_back(L'\\');
			directory += L"Inkeys\\Logs";
			std::error_code ec;
			std::filesystem::create_directories(directory, ec);
			logDirectory = directory;
			directoryReady.store(true, std::memory_order_release);

			const std::time_t now = std::time(nullptr);
			std::tm local{};
			if (localtime_s(&local, &now) != 0)
			{
				EnterDiscard(L"Inkeys log: localtime failed, logging disabled\n");
				return false;
			}
			std::wstring path;
			if (!ReserveLogFile(directory, local, path))
			{
				EnterDiscard(L"Inkeys log: cannot create log file, logging disabled\n");
				return false;
			}
			currentFile = path;
			sink = new spdlog::sinks::basic_file_sink_st(path, false);
			WriteSessionHeader(info, local, now);
			sink->flush();
			lastFlush = std::chrono::steady_clock::now();

			const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (!event)
			{
				DestroySink();
				EnterDiscard(L"Inkeys log: CreateEvent failed, logging disabled\n");
				return false;
			}
			// 唤醒事件为进程寿命对象：生产者可能在任何时刻 SetEvent，因此不关闭。
			wakeEvent.store(event, std::memory_order_release);
			// 启动清理不在此投递：用户可能配置了“永不清理”或总量上限，须等 ApplyConfig 取得真实参数。

			// 线程在锁外创建；写线程先于 Accepting 运行也只会交换到 Buffering 期间的记录。
			const HANDLE thread = CreateThread(nullptr, 0, &WriterMain, nullptr, 0, nullptr);
			if (!thread)
			{
				DestroySink();
				EnterDiscard(L"Inkeys log: CreateThread failed, logging disabled\n");
				return false;
			}
			AcquireSRWLockExclusive(&bufferLock);
			writerThread = thread;
			state.store(static_cast<int>(State::Accepting), std::memory_order_release);
			ReleaseSRWLockExclusive(&bufferLock);
			// Start 之前缓冲的早期日志由写线程首批一并落盘。
			WakeWriter();
			return true;
		}
		catch (...)
		{
			DestroySink();
			EnterDiscard(L"Inkeys log: start failed, logging disabled\n");
			return false;
		}
	}

	void FlushAndStop() noexcept
	{
		StopWriter(StopWaitMilliseconds);
	}

	void EmergencyFlush() noexcept
	{
		const LastErrorGuard lastError;
		if (state.load(std::memory_order_acquire) != static_cast<int>(State::Accepting)) return;
		// 崩溃发生在写线程自身时无法排空，直接放弃。
		if (GetCurrentThreadId() == writerThreadId.load(std::memory_order_acquire)) return;
		const std::uint64_t target = flushRequested.fetch_add(1, std::memory_order_acq_rel) + 1;
		WakeWriter();
		// 有界轮询确认：写线程可能卡在被崩溃线程持有的锁或磁盘上，超时即放弃，绝不在崩溃路径死等。
		const ULONGLONG deadline = GetTickCount64() + EmergencyWaitMilliseconds;
		while (flushCompleted.load(std::memory_order_acquire) < target && GetTickCount64() < deadline)
			Sleep(10);
	}

	void ApplyConfig(Threshold value, int days, int maxSizeMb, std::uint32_t mask) noexcept
	{
		const LastErrorGuard lastError;
		SetThreshold(value);
		SetChannelMask(mask);
		retentionDays.store((std::max)(days, 0), std::memory_order_relaxed);
		retentionMaxMb.store((std::max)(maxSizeMb, 0), std::memory_order_relaxed);
		try
		{
			// 补全会话头的配置信息；直接入队，不受阈值（包括“关闭”）过滤。
			const int thresholdIndex = (std::clamp)(threshold.load(std::memory_order_relaxed), 0, 4);
			const auto line = fmt::format("session.config threshold={} channels={} retention.days={} retention.maxMB={}",
				ThresholdNames[thresholdIndex], ChannelMaskText(mask),
				retentionDays.load(std::memory_order_relaxed), retentionMaxMb.load(std::memory_order_relaxed));
			(void)Enqueue(Level::Info, Module::Logging, Detail::NoChannel, line, false);
		}
		catch (...) { CountDrop(Level::Info); }
		// 启动清理按真实配置执行一次；写线程未运行时仅保存参数。
		retentionPending.store(true, std::memory_order_release);
		WakeWriter();
	}

	void ApplyRetention(int days, int maxSizeMb) noexcept
	{
		retentionDays.store((std::max)(days, 0), std::memory_order_relaxed);
		retentionMaxMb.store((std::max)(maxSizeMb, 0), std::memory_order_relaxed);
		// 文件遍历只在写线程执行；写线程未运行时仅保存参数。
		retentionPending.store(true, std::memory_order_release);
		WakeWriter();
	}

	void EnterDiscardMode() noexcept
	{
		EnterDiscard(nullptr);
	}

	bool Started() noexcept
	{
		return state.load(std::memory_order_acquire) == static_cast<int>(State::Accepting);
	}

	std::wstring Directory()
	{
		if (!directoryReady.load(std::memory_order_acquire)) return {};
		return logDirectory;
	}
}
