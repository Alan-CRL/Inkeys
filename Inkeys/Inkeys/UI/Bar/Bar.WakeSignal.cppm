module;

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

export module Inkeys.UI.Bar.WakeSignal;

export namespace Inkeys::UI::Bar
{
	// 多生产者只递增代次；单一渲染消费者一次合并已到达的全部通知。
	class WakeSignal final
	{
	public:
		using Generation = std::uint64_t;

		void Notify()
		{
			{
				std::lock_guard lock(mutex);
				++generation;
			}
			condition.notify_one();
		}

		Generation WaitAndConsume()
		{
			std::unique_lock lock(mutex);
			condition.wait(lock, [&]
				{
					return generation != consumedGeneration;
				});
			consumedGeneration = generation;
			return consumedGeneration;
		}

		Generation WaitUntilGenerationChange(Generation observedGeneration,
			std::chrono::steady_clock::time_point deadline)
		{
			std::unique_lock lock(mutex);
			// 通知先于阻塞到达时，谓词会立即命中，避免快照与等待之间丢失唤醒。
			(void)condition.wait_until(lock, deadline, [&]
				{
					return generation != observedGeneration;
				});
			consumedGeneration = generation;
			return consumedGeneration;
		}

		Generation CurrentGeneration() const
		{
			std::lock_guard lock(mutex);
			return generation;
		}

	private:
		mutable std::mutex mutex;
		std::condition_variable condition;
		Generation generation = 0;
		Generation consumedGeneration = 0;
	};
}
