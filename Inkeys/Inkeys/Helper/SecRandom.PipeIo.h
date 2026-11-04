#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

namespace Inkeys::SecRandom::Detail
{
	using PipeDeadline = std::chrono::steady_clock::time_point;
	inline DWORD RemainingPipeWait(PipeDeadline deadline) noexcept
	{
		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline) return 0;
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
		return static_cast<DWORD>((std::min)((std::max)(remaining, 1LL), static_cast<long long>(INFINITE - 1)));
	}

	class PipeCancellation
	{
		struct Signal { HANDLE event; void operator()() const noexcept { SetEvent(event); } };
		HANDLE event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		std::stop_token token_;
		std::optional<std::stop_callback<Signal>> callback_;
	public:
		explicit PipeCancellation(std::stop_token token) : token_(token)
		{ if (event_) callback_.emplace(token, Signal{ event_ }); }
		~PipeCancellation() { callback_.reset(); if (event_) CloseHandle(event_); }
		bool Valid() const noexcept { return event_ != nullptr; }
		bool Requested() const noexcept { return token_.stop_requested(); }
		HANDLE Event() const noexcept { return event_; }
	};

	struct PipeOperation
	{
		HANDLE pipe = INVALID_HANDLE_VALUE, event = nullptr;
		OVERLAPPED overlapped{};
		std::vector<char> buffer;
		bool pending = false, abandoned = false, cancellationIssued = false;
		~PipeOperation()
		{
			if (event) CloseHandle(event);
			if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
		}
		bool Completed(DWORD& bytes, DWORD& error) noexcept
		{
			if (GetOverlappedResult(pipe, &overlapped, &bytes, FALSE)) error = ERROR_SUCCESS;
			else error = GetLastError();
			return error != ERROR_IO_INCOMPLETE && error != ERROR_IO_PENDING;
		}
	};

	class PipeOperationRegistry
	{
		std::mutex mutex_;
		std::array<std::shared_ptr<PipeOperation>, 4> slots_;
		void Reap() noexcept
		{
			for (auto& slot : slots_)
			{
				DWORD bytes = 0, error = 0;
				if (slot && slot->abandoned && slot->Completed(bytes, error)) slot.reset();
			}
		}
	public:
		static PipeOperationRegistry& Instance()
		{
			// 驱动未完成取消时内核仍借用 OVERLAPPED/缓冲区；固定容量记录保留到进程结束。
			// 不运行静态析构，不能在内核真正完成前释放这些地址。
			static auto* registry = new PipeOperationRegistry;
			return *registry;
		}
		std::shared_ptr<PipeOperation> Acquire(HANDLE pipe, size_t size, DWORD& error)
		{
			std::lock_guard lock(mutex_);
			Reap();
			for (const auto& slot : slots_)
				if (slot && slot->abandoned) { error = ERROR_BUSY; return {}; }
			for (auto& slot : slots_)
			{
				if (slot) continue;
				auto operation = std::make_shared<PipeOperation>();
				if (!DuplicateHandle(GetCurrentProcess(), pipe, GetCurrentProcess(), &operation->pipe,
					0, FALSE, DUPLICATE_SAME_ACCESS)) { error = GetLastError(); return {}; }
				operation->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				if (!operation->event) { error = GetLastError(); return {}; }
				operation->overlapped.hEvent = operation->event;
				operation->buffer.resize(size);
				slot = operation;
				return operation;
			}
			error = ERROR_BUSY;
			return {};
		}
		void Retire(const std::shared_ptr<PipeOperation>& operation) noexcept
		{
			std::lock_guard lock(mutex_);
			DWORD bytes = 0, error = 0;
			if (operation->pending && !operation->Completed(bytes, error))
			{
				if (!operation->cancellationIssued) CancelIoEx(operation->pipe, &operation->overlapped);
				operation->abandoned = true;
				return;
			}
			for (auto& slot : slots_) if (slot == operation) { slot.reset(); break; }
		}
		size_t Outstanding() noexcept
		{
			std::lock_guard lock(mutex_);
			Reap();
			size_t count = 0;
			for (const auto& slot : slots_) if (slot) ++count;
			return count;
		}
	};

	struct PipeIoResult
	{
		DWORD error = ERROR_SUCCESS, transferred = 0;
		bool quarantined = false;
		std::vector<char> data;
	};
	struct PipeIoCancellationPolicy
	{
		DWORD graceMs = 250;
		BOOL (WINAPI* cancel)(HANDLE, LPOVERLAPPED) = CancelIoEx;
	};

	inline PipeIoResult TransferPipe(HANDLE pipe, const char* writeData, size_t size,
		PipeDeadline deadline, const PipeCancellation& cancellation,
		PipeIoCancellationPolicy policy = {})
	{
		PipeIoResult result;
		if (!cancellation.Valid()) { result.error = ERROR_NOT_ENOUGH_MEMORY; return result; }
		if (cancellation.Requested()) { result.error = ERROR_OPERATION_ABORTED; return result; }
		if (!RemainingPipeWait(deadline)) { result.error = ERROR_TIMEOUT; return result; }
		// 单次写入上限也限制隔离时保留的内存；同时在途最多四条，有隔离项则拒绝新请求。
		size = (std::min)(size, size_t{ 64 * 1024 });
		auto operation = PipeOperationRegistry::Instance().Acquire(pipe, size, result.error);
		if (!operation) return result;
		struct Lease
		{
			std::shared_ptr<PipeOperation> operation;
			~Lease() { PipeOperationRegistry::Instance().Retire(operation); }
		} lease{ operation };
		if (writeData) std::copy_n(writeData, size, operation->buffer.data());
		operation->pending = true;
		const BOOL immediate = writeData
			? WriteFile(operation->pipe, operation->buffer.data(), static_cast<DWORD>(size),
				nullptr, &operation->overlapped)
			: ReadFile(operation->pipe, operation->buffer.data(), static_cast<DWORD>(size),
				nullptr, &operation->overlapped);
		result.error = immediate ? ERROR_SUCCESS : GetLastError();
		if (immediate || result.error == ERROR_MORE_DATA)
		{
			if (operation->Completed(result.transferred, result.error)) operation->pending = false;
			else result.error = ERROR_IO_PENDING;
		}
		if (result.error == ERROR_IO_PENDING)
		{
			HANDLE events[]{ operation->event, cancellation.Event() };
			const DWORD wait = WaitForMultipleObjects(2, events, FALSE, RemainingPipeWait(deadline));
			if (wait == WAIT_OBJECT_0 && operation->Completed(result.transferred, result.error))
				operation->pending = false;
			else
			{
				const DWORD cancelledError = cancellation.Requested() ? ERROR_OPERATION_ABORTED
					: wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : wait == WAIT_FAILED ? GetLastError() : ERROR_OPERATION_ABORTED;
				policy.cancel(operation->pipe, &operation->overlapped);
				operation->cancellationIssued = true;
				WaitForSingleObject(operation->event, (std::min)(policy.graceMs, DWORD{ 250 }));
				DWORD ignored = 0, completionError = 0;
				operation->pending = !operation->Completed(ignored, completionError);
				result.quarantined = operation->pending;
				result.error = cancelledError;
			}
		}
		else operation->pending = false;
		if (!writeData && !operation->pending &&
			(result.error == ERROR_SUCCESS || result.error == ERROR_MORE_DATA))
			result.data.assign(operation->buffer.begin(), operation->buffer.begin() + result.transferred);
		return result;
	}
}
