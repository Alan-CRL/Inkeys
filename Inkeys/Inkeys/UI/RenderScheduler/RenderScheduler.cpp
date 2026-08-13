module;

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

module Inkeys.UI.RenderScheduler;

namespace Inkeys::UI::RenderScheduler
{
	namespace
	{
		constexpr auto ClientCount = static_cast<std::size_t>(Client::Count);
		constexpr auto FrameInterval = std::chrono::nanoseconds(16'666'667);
		constexpr std::array<Client, ClientCount> DispatchOrder{
			Client::Bar,
			Client::PptBottomLeft,
			Client::PptBottomRight,
			Client::PptMiddleLeft,
			Client::PptMiddleRight,
			Client::PptExitShow,
			Client::Settings,
		};

		[[nodiscard]] constexpr std::size_t Index(Client client) noexcept
		{
			return static_cast<std::size_t>(client);
		}

		[[nodiscard]] constexpr ClientMask AllClientMask() noexcept
		{
			return (ClientMask{ 1 } << static_cast<unsigned>(Client::Count)) - 1;
		}
	}

	void DispatchState::Request(ClientMask mask) noexcept
	{
		requested_.fetch_or(mask & AllClientMask(), std::memory_order_release);
	}

	ClientMask DispatchState::TakeRequested() noexcept
	{
		return requested_.exchange(0, std::memory_order_acq_rel);
	}

	DispatchDecision DispatchState::Complete(ClientMask work,
		const std::array<FrameResult, ClientCount>& results) noexcept
	{
		DispatchDecision decision;
		decision.work = work;
		for (const auto client : DispatchOrder)
		{
			const auto bit = Mask(client);
			if ((work & bit) == 0) continue;
			switch (results[Index(client)])
			{
			case FrameResult::Continue:
			case FrameResult::Retry:
				decision.next |= bit;
				break;
			case FrameResult::DeviceLost:
				decision.rebuildSharedDevice = true;
				break;
			case FrameResult::Stop:
				decision.stop = true;
				break;
			case FrameResult::Idle:
			default:
				break;
			}
		}
		if (decision.rebuildSharedDevice) decision.next |= AllClientMask();
		decision.next |= TakeRequested();
		decision.sleep = decision.next == 0 && !decision.stop;
		return decision;
	}

	struct Scheduler::Impl
	{
		Impl()
		{
			wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		}

		~Impl()
		{
			if (wakeEvent) CloseHandle(wakeEvent);
		}

		std::mutex callbackMutex;
		std::condition_variable callbackCondition;
		std::array<RenderCallback, ClientCount> callbacks{};
		std::array<std::size_t, ClientCount> activeCallbacks{};
		DeviceRecoveryCallback deviceRecovery;
		DispatchState dispatch;
		HANDLE wakeEvent = nullptr;
		std::atomic_bool running = false;
	};

	Scheduler::Scheduler() : impl_(new Impl) {}

	Scheduler::~Scheduler()
	{
		delete impl_;
		impl_ = nullptr;
	}

	bool Scheduler::Register(Client client, RenderCallback callback)
	{
		if (!impl_ || !callback || client >= Client::Count) return false;
		{
			std::scoped_lock lock(impl_->callbackMutex);
			impl_->callbacks[Index(client)] = std::move(callback);
		}
		Request(client);
		return true;
	}

	void Scheduler::Unregister(Client client) noexcept
	{
		if (!impl_ || client >= Client::Count) return;
		std::unique_lock lock(impl_->callbackMutex);
		impl_->callbacks[Index(client)] = {};
		impl_->callbackCondition.wait(lock, [this, client]
			{
				return impl_->activeCallbacks[Index(client)] == 0;
			});
	}

	void Scheduler::SetDeviceRecoveryCallback(DeviceRecoveryCallback callback)
	{
		if (!impl_) return;
		std::scoped_lock lock(impl_->callbackMutex);
		impl_->deviceRecovery = std::move(callback);
	}

	void Scheduler::Request(Client client) noexcept
	{
		if (client >= Client::Count) return;
		Request(Mask(client));
	}

	void Scheduler::Request(ClientMask mask) noexcept
	{
		if (!impl_) return;
		impl_->dispatch.Request(mask);
		if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	void Scheduler::WakeForStop() noexcept
	{
		if (impl_ && impl_->wakeEvent) SetEvent(impl_->wakeEvent);
	}

	void Scheduler::Run(const std::function<bool()>& shouldStop)
	{
		if (!impl_ || impl_->running.exchange(true, std::memory_order_acq_rel)) return;
		struct RunningGuard
		{
			std::atomic_bool& running;
			~RunningGuard() { running.store(false, std::memory_order_release); }
		} runningGuard{ impl_->running };

		ClientMask pending = impl_->dispatch.TakeRequested();
		auto nextDeadline = std::chrono::steady_clock::now();
		while (!shouldStop || !shouldStop())
		{
			if (pending == 0)
			{
				if (!impl_->wakeEvent) break;
				if (shouldStop && shouldStop()) break;
				ResetEvent(impl_->wakeEvent);
				// reset 后再次交换，覆盖请求恰好落在 idle 边界的竞态。
				pending = impl_->dispatch.TakeRequested();
				if (pending == 0)
				{
					// 停止通知可能早于 reset；等待前重查，不能清掉唯一退出唤醒。
					if (shouldStop && shouldStop()) break;
					WaitForSingleObject(impl_->wakeEvent, INFINITE);
					pending = impl_->dispatch.TakeRequested();
				}
				if (shouldStop && shouldStop()) break;
				if (pending == 0) continue;
			}

			const auto now = std::chrono::steady_clock::now();
			if (now < nextDeadline) std::this_thread::sleep_until(nextDeadline);
			// 以实际帧起点推进期限，休眠很久后唤醒也不会用“补帧”突破 60 FPS。
			nextDeadline = std::chrono::steady_clock::now() + FrameInterval;
			// pacing 等待期间追加的窗口也进入当前批次，但不能提前突破帧期限。
			pending |= impl_->dispatch.TakeRequested();

			DeviceRecoveryCallback deviceRecovery;
			{
				std::scoped_lock lock(impl_->callbackMutex);
				deviceRecovery = impl_->deviceRecovery;
			}
			std::array<FrameResult, ClientCount> results{};
			results.fill(FrameResult::Idle);
			const auto work = std::exchange(pending, 0);
			for (const auto client : DispatchOrder)
			{
				const auto bit = Mask(client);
				if ((work & bit) == 0) continue;
				RenderCallback callback;
				{
					std::scoped_lock lock(impl_->callbackMutex);
					callback = impl_->callbacks[Index(client)];
					if (callback) ++impl_->activeCallbacks[Index(client)];
				}
				if (!callback) continue;
				try { results[Index(client)] = callback(); }
				catch (...) { results[Index(client)] = FrameResult::Retry; }
				{
					std::scoped_lock lock(impl_->callbackMutex);
					--impl_->activeCallbacks[Index(client)];
				}
				impl_->callbackCondition.notify_all();
			}
			const auto decision = impl_->dispatch.Complete(work, results);
			if (decision.stop) break;
			pending = decision.next;
			if (decision.rebuildSharedDevice)
			{
				// 设备代次只能由唯一渲染线程切换，成功后所有客户端重建各自目标。
				if (!deviceRecovery || !deviceRecovery())
					pending |= AllClientMask();
			}
		}
	}

	Scheduler& GetScheduler() noexcept
	{
		static Scheduler scheduler;
		return scheduler;
	}

	void Request(Client client) noexcept
	{
		GetScheduler().Request(client);
	}

	void Request(ClientMask mask) noexcept
	{
		GetScheduler().Request(mask);
	}
}
