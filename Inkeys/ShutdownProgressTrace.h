#pragma once

#include "PptNavigationDiagnostics.h"

namespace Inkeys::Shutdown::ProgressTrace
{
	using Recorder = Inkeys::Business::PptPerf::Recorder;
	using Clock = Inkeys::Business::PptPerf::Clock;
	using Stamp = Inkeys::Business::PptPerf::Stamp;
	using Event = Inkeys::Business::PptPerf::Event;

	// 首次读开关、取时钟和写 stderr 都不能覆盖业务/Arm 刚产生的 Win32 错误。
	class LastErrorGuard
	{
	public:
		LastErrorGuard() noexcept : value_(GetLastError()) {}
		~LastErrorGuard() noexcept { SetLastError(value_); }
		LastErrorGuard(const LastErrorGuard&) = delete;
		LastErrorGuard& operator=(const LastErrorGuard&) = delete;
	private:
		DWORD value_;
	};

	inline bool Enabled() noexcept
	{
		LastErrorGuard error;
		static const bool enabled = Inkeys::Business::PptPerf::Enabled();
		return enabled;
	}

	inline int Format(char* buffer, std::size_t size, const Event& value) noexcept
	{
		LastErrorGuard error;
		const auto& stamp = value.stamp;
		const int count = std::snprintf(buffer, size,
			"[ShutdownTrace] v=1 source=native pid=%lu tid=%lu qpc=%lld frequency=%lld span=%llu event=%s stage=%s result=%s error=%ld\n",
			stamp.process, stamp.thread, static_cast<long long>(stamp.qpc),
			static_cast<long long>(stamp.frequency), static_cast<unsigned long long>(stamp.span),
			value.event, stamp.stage, value.result, value.error);
		return count > 0 && static_cast<std::size_t>(count) < size ? count : 0;
	}

	inline void WriteRaw(const Event& value) noexcept
	{
		LastErrorGuard error;
		char line[512]{};
		const int count = Inkeys::Shutdown::ProgressTrace::Format(line, sizeof(line), value);
		if (!count) return;
		const HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
		if (!output || output == INVALID_HANDLE_VALUE) return;
		DWORD written = 0;
		(void)WriteFile(output, line, static_cast<DWORD>(count), &written, nullptr);
	}

	inline Recorder& DefaultRecorder() noexcept
	{
		LastErrorGuard error;
		// 此 inline 局部静态跨普通 TU 共享，只使用退出前缀，不复用导航 Recorder 的序号。
		static Recorder recorder(Enabled(), WriteRaw);
		return recorder;
	}

	// 正常入口在启动线程前预热；Arm 前只读此指针，不触发局部静态初始化锁。
	inline constinit std::atomic<Recorder*> preparedRecorder{ nullptr };
	inline void WarmUp() noexcept
	{
		LastErrorGuard error;
		preparedRecorder.store(&DefaultRecorder(), std::memory_order_release);
	}
	inline Recorder* PreparedRecorder() noexcept
	{
		LastErrorGuard error;
		return preparedRecorder.load(std::memory_order_acquire);
	}

	inline Stamp Capture(Recorder& recorder, const char* stage) noexcept
	{
		LastErrorGuard error;
		return recorder.Capture(stage, { 0, 0, 0, "Shutdown" });
	}
	inline Event CaptureEnd(Recorder& recorder, const Stamp& begin,
		const char* result = "scope-exit", long code = 0) noexcept
	{
		LastErrorGuard error;
		return recorder.CaptureEnd(begin, result, code);
	}
	inline Stamp Capture(Recorder* recorder, const char* stage) noexcept
	{
		LastErrorGuard error;
		return recorder ? Capture(*recorder, stage) : Stamp{};
	}
	inline Event CaptureEnd(Recorder* recorder, const Stamp& begin,
		const char* result = "scope-exit", long code = 0) noexcept
	{
		LastErrorGuard error;
		return recorder ? CaptureEnd(*recorder, begin, result, code) : Event{};
	}
	inline void Begin(Recorder& recorder, const Stamp& begin) noexcept
	{
		LastErrorGuard error;
		recorder.Begin(begin);
	}
	inline void Emit(Recorder& recorder, const Event& value) noexcept
	{
		LastErrorGuard error;
		recorder.Emit(value);
	}

	class Scope
	{
	public:
		explicit Scope(const char* stage) noexcept : Scope(DefaultRecorder(), stage) {}
		Scope(Recorder& recorder, const char* stage) noexcept
			: recorder_(recorder), stamp_(Capture(recorder, stage))
		{
			LastErrorGuard error;
			exceptions_ = stamp_.span ? std::uncaught_exceptions() : 0;
			Begin(recorder_, stamp_);
		}
		// pre-Arm 仅保存 POD，已在 Arm 返回后输出 begin，随后接续相同 span。
		Scope(Recorder& recorder, const Stamp& resumed) noexcept
			: recorder_(recorder), stamp_(resumed)
		{
			LastErrorGuard error;
			exceptions_ = stamp_.span ? std::uncaught_exceptions() : 0;
		}
		~Scope() noexcept
		{
			LastErrorGuard error;
			if (stamp_.span)
				Emit(recorder_, CaptureEnd(recorder_, stamp_,
					std::uncaught_exceptions() > exceptions_ ? "exception" : result_));
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		void Result(const char* value) noexcept { result_ = value; }
	private:
		Recorder& recorder_;
		Stamp stamp_;
		int exceptions_ = 0;
		const char* result_ = "scope-exit";
	};
}
