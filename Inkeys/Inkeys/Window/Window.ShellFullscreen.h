#pragma once

#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Inkeys::Window::Detail
{
	inline constexpr wchar_t WindowGenerationProperty[] = L"Inkeys.Window.Generation";
	inline std::atomic<std::uintptr_t> NextWindowGeneration{ 0 };

	struct ShellFullscreenState
	{
		struct Target
		{
			HWND hwnd = nullptr;
			std::uintptr_t generation = 0;
			DWORD thread = 0;
			DWORD process = 0;
			bool enabled = false;
			bool operator==(const Target&) const noexcept = default;
		};
		using Targets = std::array<Target, 3>;
		struct Request
		{
			Targets targets{};
			std::uint64_t revision = 0;
		};
		std::mutex mutex;
		Request desired;
		std::atomic_bool stopping = false;
		std::atomic_bool calling = false;
		std::atomic<DWORD> workerId = 0;
		std::atomic<ULONGLONG> callDeadline = 0;
		std::atomic<std::uint64_t> callRevision = 0;
		std::atomic<std::uint64_t> confirmedRevision = 0;
		std::atomic<HRESULT> lastResult = E_PENDING;
		HANDLE wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE cancelWake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		~ShellFullscreenState() { if (wake) CloseHandle(wake); if (cancelWake) CloseHandle(cancelWake); }

		[[nodiscard]] static Targets ResolveTargets(Target root, Target freeze, Target drawpad,
			bool whiteboard, bool enabled, bool minimized) noexcept
		{
			enabled &= !minimized;
			root.enabled = enabled && !whiteboard;
			freeze.enabled = drawpad.enabled = enabled && whiteboard;
			Targets targets{ root, freeze, drawpad };
			// root 可能就是 Freeze/Drawpad；同一代 HWND 只通知一次，不能用随后 FALSE 抹掉 TRUE。
			for (std::size_t i = 0; i < targets.size(); ++i)
				for (std::size_t j = 0; targets[i].hwnd && j < i; ++j)
					if (targets[i].hwnd == targets[j].hwnd && targets[i].generation == targets[j].generation
						&& targets[i].thread == targets[j].thread && targets[i].process == targets[j].process)
					{
						targets[j].enabled |= targets[i].enabled;
						targets[i] = {};
					}
			return targets;
		}
		void Publish(const Targets& targets, bool invalidate = false) noexcept
		{
			{
				std::scoped_lock lock(mutex);
				if (stopping.load(std::memory_order_acquire)) return;
				if (!invalidate && desired.targets == targets) return;
				desired = { targets, desired.revision + 1 };
			}
			if (wake) SetEvent(wake);
			if (cancelWake) SetEvent(cancelWake);
		}
		void Invalidate() noexcept
		{
			{
				std::scoped_lock lock(mutex);
				if (stopping.load(std::memory_order_acquire)) return;
				++desired.revision;
			}
			if (wake) SetEvent(wake);
			if (cancelWake) SetEvent(cancelWake);
		}
		[[nodiscard]] Request Snapshot() noexcept
		{
			std::scoped_lock lock(mutex);
			return desired;
		}
		[[nodiscard]] bool TargetAlive(const Target& target) const noexcept
		{
			DWORD process = 0;
			return target.hwnd && target.generation && IsWindow(target.hwnd)
				&& GetWindowThreadProcessId(target.hwnd, &process) == target.thread && process == target.process
				&& reinterpret_cast<std::uintptr_t>(GetPropW(target.hwnd, WindowGenerationProperty)) == target.generation;
		}
		[[nodiscard]] bool Current(const Request& request) noexcept
		{
			std::scoped_lock lock(mutex);
			return !stopping.load(std::memory_order_acquire) && desired.revision == request.revision;
		}
		[[nodiscard]] bool Complete(const Request& request, HRESULT result) noexcept
		{
			// 整批版本检查与确认共用邮箱锁，不能让迟到 TRUE 替随后全组 FALSE 记账。
			std::scoped_lock lock(mutex);
			if (stopping.load(std::memory_order_acquire) || desired.revision != request.revision) return false;
			lastResult.store(result, std::memory_order_release);
			if (SUCCEEDED(result)) confirmedRevision.store(request.revision, std::memory_order_release);
			return true;
		}

	};

	// 只含 owning 状态与线程句柄，无 Service/HWND owner 栈借用；进程内至多一组 COM/取消线程。
	struct ShellFullscreenSlot
	{
		std::mutex mutex;
		std::shared_ptr<ShellFullscreenState> state;
		HANDLE worker = nullptr;
		HANDLE canceller = nullptr;
	};
	inline ShellFullscreenSlot& FullscreenSlot()
	{
		// 不可取消的系统 COM 调用只隔离这一份，留给既有进程退出监督；不扩增后台线程。
		static auto* slot = new ShellFullscreenSlot;
		return *slot;
	}

	inline DWORD WINAPI ShellFullscreenWorker(void* context) noexcept
	{
		std::unique_ptr<std::shared_ptr<ShellFullscreenState>> holder(
			static_cast<std::shared_ptr<ShellFullscreenState>*>(context));
		const auto state = *holder;
		state->workerId.store(GetCurrentThreadId(), std::memory_order_release);
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(initialized))
		{
			state->stopping.store(true, std::memory_order_release);
			SetEvent(state->cancelWake);
			return 0;
		}
		const bool cancellable = SUCCEEDED(CoEnableCallCancellation(nullptr));
		std::uint64_t applied = 0;
		unsigned failures = 0;
		DWORD delay = INFINITE;
		while (!state->stopping.load(std::memory_order_acquire))
		{
			(void)WaitForSingleObject(state->wake, delay);
			ResetEvent(state->wake);
			const auto request = state->Snapshot();
			if (state->stopping.load(std::memory_order_acquire)) break;
			if (!request.revision || applied == request.revision || !state->Current(request)) { delay = INFINITE; continue; }
			state->callDeadline.store(GetTickCount64() + 1000, std::memory_order_release);
			state->callRevision.store(request.revision, std::memory_order_release);
			state->calling.store(true, std::memory_order_release);
			SetEvent(state->cancelWake);
			ITaskbarList2* taskbar = nullptr;
			HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
				__uuidof(ITaskbarList2), reinterpret_cast<void**>(&taskbar));
			if (SUCCEEDED(result) && !taskbar) result = E_NOINTERFACE;
			if (SUCCEEDED(result) && taskbar && state->Current(request)) result = taskbar->HrInit();
			if (SUCCEEDED(result) && taskbar)
				for (const auto& target : request.targets)
				{
					if (!state->Current(request)) break;
					if (!state->TargetAlive(target)) continue;
					result = taskbar->MarkFullscreenWindow(target.hwnd, target.enabled ? TRUE : FALSE);
					if (FAILED(result)) break;
					if (!state->TargetAlive(target)) { result = HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE); break; }
				}
			if (taskbar) taskbar->Release();
			state->calling.store(false, std::memory_order_release);
			SetEvent(state->cancelWake);
			if (state->Complete(request, result))
			{
				if (SUCCEEDED(result)) { applied = request.revision; failures = 0; delay = INFINITE; }
				else { if (failures < 3) ++failures; delay = 250u << failures; }
			}
			else
			{
				// 迟到旧回执不确认新状态；下一轮必须重发最新绝对值。
				failures = 0;
				delay = 0;
			}
		}
		if (cancellable) (void)CoDisableCallCancellation(nullptr);
		CoUninitialize();
		return 0;
	}

	inline DWORD WINAPI ShellFullscreenCanceller(void* context) noexcept
	{
		std::unique_ptr<std::shared_ptr<ShellFullscreenState>> holder(
			static_cast<std::shared_ptr<ShellFullscreenState>*>(context));
		const auto state = *holder;
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		for (;;)
		{
			const bool calling = state->calling.load(std::memory_order_acquire);
			if (state->stopping.load(std::memory_order_acquire) && !calling) break;
			(void)WaitForSingleObject(state->cancelWake, calling ? 100 : INFINITE);
			ResetEvent(state->cancelWake);
			if (!state->calling.load(std::memory_order_acquire)) continue;
			bool obsolete = state->stopping.load(std::memory_order_acquire)
				|| GetTickCount64() >= state->callDeadline.load(std::memory_order_acquire);
			{ std::scoped_lock lock(state->mutex); obsolete |= state->desired.revision != state->callRevision.load(std::memory_order_acquire); }
			if (obsolete && SUCCEEDED(initialized)) (void)CoCancelCall(state->workerId.load(std::memory_order_acquire), 0);
		}
		if (SUCCEEDED(initialized)) CoUninitialize();
		return 0;
	}

	inline void StopShellFullscreen(const std::shared_ptr<ShellFullscreenState>& state) noexcept
	{
		if (!state) return;
		state->stopping.store(true, std::memory_order_release);
		SetEvent(state->wake);
		SetEvent(state->cancelWake);
		auto& slot = FullscreenSlot();
		std::scoped_lock lock(slot.mutex);
		if (slot.state != state) return;
		HANDLE handles[2]{};
		DWORD count = 0;
		if (slot.worker) handles[count++] = slot.worker;
		if (slot.canceller) handles[count++] = slot.canceller;
		if (count && WaitForMultipleObjects(count, handles, TRUE, 1000) != WAIT_OBJECT_0) return;
		if (slot.worker) CloseHandle(slot.worker);
		if (slot.canceller) CloseHandle(slot.canceller);
		slot.worker = slot.canceller = nullptr;
		slot.state.reset();
	}

	[[nodiscard]] inline std::shared_ptr<ShellFullscreenState> StartShellFullscreen() noexcept
	{
		std::shared_ptr<ShellFullscreenState> cleanup;
		try
		{
			auto& slot = FullscreenSlot();
			std::unique_lock lock(slot.mutex);
			if (slot.state)
			{
				if ((slot.worker && WaitForSingleObject(slot.worker, 0) != WAIT_OBJECT_0)
					|| (slot.canceller && WaitForSingleObject(slot.canceller, 0) != WAIT_OBJECT_0)) return {};
				if (slot.worker) CloseHandle(slot.worker);
				if (slot.canceller) CloseHandle(slot.canceller);
				slot.worker = slot.canceller = nullptr;
				slot.state.reset();
			}
			auto state = std::make_shared<ShellFullscreenState>();
			if (!state->wake || !state->cancelWake) return {};
			slot.state = state;
			cleanup = state;
			auto cancelContext = std::make_unique<std::shared_ptr<ShellFullscreenState>>(state);
			slot.canceller = CreateThread(nullptr, 0, ShellFullscreenCanceller, cancelContext.get(), 0, nullptr);
			if (slot.canceller) cancelContext.release();
			auto workerContext = std::make_unique<std::shared_ptr<ShellFullscreenState>>(state);
			slot.worker = slot.canceller ? CreateThread(nullptr, 0, ShellFullscreenWorker, workerContext.get(), 0, nullptr) : nullptr;
			if (slot.worker) workerContext.release();
			if (!slot.worker)
			{
				lock.unlock();
				StopShellFullscreen(state);
				return {};
			}
			return state;
		}
		catch (...)
		{
			// 只清理本次已发布的状态；分配 slot 本身失败时不再次分配或影响已有 worker。
			if (cleanup) StopShellFullscreen(cleanup);
			return {};
		}
	}
}
