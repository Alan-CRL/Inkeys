module;

#include "../../../IdtMain.h"
#include <condition_variable>

export module Inkeys.UI.Bar:Atomic;

export namespace BarAtomic
{
	// 类似 atomic<bool> + atomic.wait() 的功能实现
	class AtomicWaitClass
	{
	public:
		AtomicWaitClass(bool v = false) : value(v) {}

		// 设置值
		void Store(bool v)
		{
			value.store(v, std::memory_order_release);
			if (v) {
				std::lock_guard<std::mutex> lock(mtx);
				cv.notify_all();
			}
		}

		// 读取
		bool Load() const
		{
			return value.load(std::memory_order_acquire);
		}

		// 类似 atomic.wait(false)
		void WaitFalse()
		{
			if (value.load(std::memory_order_acquire))
				return;

			std::unique_lock<std::mutex> lock(mtx);
			cv.wait(lock, [&] {
				return value.load(std::memory_order_acquire);
				});
		}

	private:
		std::atomic<bool> value;
		std::mutex mtx;
		std::condition_variable cv;
	} wait;

	// 此标识表示 UI 将不检查是否变动，而持续渲染
	IdtAtomic<bool> sustainFlag = false;
}