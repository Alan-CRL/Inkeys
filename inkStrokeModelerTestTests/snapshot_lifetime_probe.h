#pragma once

#include <atomic>
#include <chrono>
#include <windows.h>
#include <cstddef>
#include <iostream>
#include <thread>
#include <variant>

namespace SnapshotLifetimeProbe
{
	struct Range { std::size_t first = 0, count = 0; };
	struct Statistics
	{
		std::size_t buffers = 0, bytes = 0, freedBuffers = 0, freedBytes = 0;
		bool overflow = false;
	};

	void Begin() noexcept;
	void End() noexcept;
	std::size_t Count() noexcept;
	void Watch(const void* memory, std::size_t bytes) noexcept;
	Statistics Read(Range range) noexcept;
	void Free(void* memory) noexcept;

	// 仅观察提交前登记的真实缓冲地址；不把 allocator/OS 提交量当作对象存活。
	class Scope
	{
	public:
		Scope() noexcept { Begin(); }
		~Scope() { End(); }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	};

	// 借用普通 worker wake 或既有测试 hook；用事件确认实际持有阶段，不靠固定 Sleep。
	class Gate
	{
	public:
		Gate() noexcept : entered_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
			resume_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
		~Gate()
		{
			Release();
			if (entered_) CloseHandle(entered_);
			if (resume_) CloseHandle(resume_);
		}
		bool IsValid() const noexcept { return entered_ && resume_; }
		bool WaitEntered() const noexcept { return WaitForSingleObject(entered_, 30000) == WAIT_OBJECT_0; }
		void Release() const noexcept { if (resume_) SetEvent(resume_); }
		bool TimedOut() const noexcept { return timedOut_.load(std::memory_order_acquire); }
		void BlockOnce() noexcept
		{
			if (calls_.fetch_add(1, std::memory_order_relaxed) != 0) return;
			SetEvent(entered_);
			if (WaitForSingleObject(resume_, 30000) != WAIT_OBJECT_0) timedOut_.store(true, std::memory_order_release);
		}
		static void Wake(void* context) noexcept { static_cast<Gate*>(context)->BlockOnce(); }
	private:
		HANDLE entered_ = nullptr, resume_ = nullptr;
		std::atomic<unsigned> calls_ = 0;
		std::atomic<bool> timedOut_ = false;
	};

	template<class Vector>
	void WatchVector(const Vector& vector) noexcept
	{
		if (vector.capacity()) Watch(vector.data(), vector.capacity() * sizeof(typename Vector::value_type));
	}

	template<class Snapshot>
	Range ObserveSnapshot(const Snapshot& snapshot) noexcept
	{
		const auto first = Count();
		auto observeCanvases = [](const auto& canvases)
		{
			WatchVector(canvases);
			for (const auto& canvas : canvases)
			{
				WatchVector(canvas.strokes);
				WatchVector(canvas.operations);
				for (const auto& stroke : canvas.strokes) WatchVector(stroke.points);
				for (const auto& operation : canvas.operations)
					std::visit([](const auto& value)
					{
						if constexpr (requires { value.points; }) WatchVector(value.points);
					}, operation);
			}
		};
		observeCanvases(snapshot.canvases);
		observeCanvases(snapshot.activeCanvases);
		observeCanvases(snapshot.retainedCanvases);
		return { first, Count() - first };
	}

	template<class Predicate>
	bool Wait(Predicate condition)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		do
		{
			if (condition()) return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		} while (std::chrono::steady_clock::now() < deadline);
		return condition();
	}

	inline bool Released(Range range) noexcept
	{
		const auto result = Read(range);
		return !result.overflow && result.buffers != 0 && result.buffers == result.freedBuffers;
	}

	inline void Report(const char* name, Range range)
	{
		const auto result = Read(range);
		std::cout << "[SnapshotLifetime] case=" << name << " watched_buffers=" << result.buffers
			<< " released_buffers=" << result.freedBuffers << " watched_bytes=" << result.bytes
			<< " released_bytes=" << result.freedBytes << " overflow=" << result.overflow << '\n';
	}
}
