#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <windows.h>

namespace Inkeys::Business::PptPerf
{
	struct Context
	{
		std::uint64_t parent = 0;
		std::uint64_t localSession = 0;
		std::uint64_t epoch = 0;
		const char* operation = "None";
	};
	struct Stamp
	{
		std::uint64_t span = 0;
		Context context;
		const char* stage = "unknown";
		std::int64_t qpc = 0;
		std::int64_t frequency = 0;
		unsigned long process = 0;
		unsigned long thread = 0;
	};
	struct Event
	{
		Stamp stamp;
		const char* event = "instant";
		const char* result = "unknown";
		long error = 0;
	};
	struct Clock
	{
		std::int64_t (*now)();
		std::int64_t (*frequency)();
		unsigned long (*process)();
		unsigned long (*thread)();
	};
	inline std::int64_t Now() { LARGE_INTEGER value{}; return QueryPerformanceCounter(&value) ? value.QuadPart : 0; }
	inline std::int64_t Frequency() { LARGE_INTEGER value{}; return QueryPerformanceFrequency(&value) ? value.QuadPart : 0; }
	inline unsigned long Process() { return GetCurrentProcessId(); }
	inline unsigned long Thread() { return GetCurrentThreadId(); }
	inline bool Enabled() noexcept
	{
		wchar_t value[8]{};
		return GetEnvironmentVariableW(L"INKEYS_PPT_PERF", value, 8) == 1 && value[0] == L'1';
	}
	inline int Format(char* buffer, std::size_t size, const Event& value) noexcept
	{
		const auto& s = value.stamp;
		const int count = std::snprintf(buffer, size,
			"[PptPerf] v=1 source=native pid=%lu tid=%lu qpc=%lld frequency=%lld span=%llu parent=%llu event=%s stage=%s op=%s local_session=%llu epoch=%llu result=%s hr=%ld",
			s.process, s.thread, static_cast<long long>(s.qpc), static_cast<long long>(s.frequency),
			static_cast<unsigned long long>(s.span), static_cast<unsigned long long>(s.context.parent),
			value.event, s.stage, s.context.operation, static_cast<unsigned long long>(s.context.localSession),
			static_cast<unsigned long long>(s.context.epoch), value.result, value.error);
		return count > 0 && static_cast<std::size_t>(count) < size ? count : 0;
	}
	class Recorder
	{
	public:
		using Sink = void (*)(const Event&);
		Recorder(bool enabled, Sink sink, Clock clock = { Now, Frequency, Process, Thread }) noexcept
			: enabled_(enabled), sink_(sink), clock_(clock) {}
		Stamp Capture(const char* stage, Context context, bool eligible = true) noexcept
		{
			if (!eligible || !enabled_ || !sink_) return {};
			try
			{
				Stamp value;
				value.qpc = clock_.now(); value.frequency = clock_.frequency();
				if (value.qpc <= 0 || value.frequency <= 0) return {};
				value.span = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
				value.context = context; value.stage = stage;
				value.process = clock_.process(); value.thread = clock_.thread();
				return value;
			}
			catch (...) { return {}; }
		}
		Event CaptureEnd(const Stamp& begin, const char* result, long error = 0) noexcept
		{
			if (!begin.span) return {};
			try
			{
				Event value{ begin, "end", result, error };
				value.stamp.qpc = clock_.now(); value.stamp.thread = clock_.thread();
				if (value.stamp.qpc <= 0) return {};
				return value;
			}
			catch (...) { return {}; }
		}
		void Emit(const Event& value) noexcept
		{
			if (!value.stamp.span) return;
			// sink 不是 noexcept：诊断写入抛错也必须在这里截断，不得打断业务/COM清理。
			try { sink_(value); } catch (...) {}
		}
		void Begin(const Stamp& value) noexcept { Emit({ value, "begin", "pending", 0 }); }
		void End(const Stamp& value, const char* result, long error = 0) noexcept { Emit(CaptureEnd(value, result, error)); }
		void Instant(const char* stage, Context context, const char* result, long error = 0) noexcept
		{ Emit({ Capture(stage, context), "instant", result, error }); }
	private:
		bool enabled_;
		Sink sink_;
		Clock clock_;
		std::atomic_uint64_t sequence_ = 0;
	};
	class Scope
	{
	public:
		Scope(Recorder& recorder, const char* stage, Context context, bool eligible = true) noexcept
			: recorder_(recorder), stamp_(recorder.Capture(stage, context, eligible)),
			  exceptions_(stamp_.span ? std::uncaught_exceptions() : 0)
		{ recorder_.Begin(stamp_); }
		// 入队线程已经发布 begin；业务线程只接续相同纯值 span，不能重新取 begin 时刻。
		Scope(Recorder& recorder, const Stamp& resumed) noexcept
			: recorder_(recorder), stamp_(resumed), exceptions_(resumed.span ? std::uncaught_exceptions() : 0) {}
		~Scope() noexcept
		{
			if (stamp_.span) recorder_.End(stamp_, std::uncaught_exceptions() > exceptions_ ? "exception" : result_);
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		std::uint64_t Id() const noexcept { return stamp_.span; }
		void Result(const char* result) noexcept { result_ = result; }
	private:
		Recorder& recorder_;
		Stamp stamp_;
		int exceptions_;
		const char* result_ = "scope-exit";
	};
	inline thread_local Context current;
	class ContextScope
	{
	public:
		explicit ContextScope(Context value) noexcept : previous_(current) { current = value; }
		~ContextScope() noexcept { current = previous_; }
		ContextScope(const ContextScope&) = delete;
		ContextScope& operator=(const ContextScope&) = delete;
	private:
		Context previous_;
	};
}
