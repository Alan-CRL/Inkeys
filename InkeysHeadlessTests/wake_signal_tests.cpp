#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

import Inkeys.UI.Bar.WakeSignal;

namespace
{
	using namespace std::chrono_literals;
	using Inkeys::UI::Bar::WakeSignal;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	void TestPreNotification()
	{
		WakeSignal signal;
		signal.Notify();
		auto waiter = std::async(std::launch::async,
			[&] { return signal.WaitAndConsume(); });
		bool ready = waiter.wait_for(1s) == std::future_status::ready;
		Check(ready, "wake pre-notification is retained");
		if (!ready) signal.Notify();
		Check(waiter.get() == 1, "wake pre-notification generation");
	}

	void TestTimedWaitPreNotification()
	{
		WakeSignal signal;
		const auto observedGeneration = signal.CurrentGeneration();
		signal.Notify();

		const auto waitStart = std::chrono::steady_clock::now();
		const auto generation = signal.WaitUntilGenerationChange(
			observedGeneration, waitStart + 1s);
		Check(generation == 1,
			"timed wake retains notification before wait");
		Check(std::chrono::steady_clock::now() - waitStart < 250ms,
			"timed wake pre-notification returns promptly");
	}

	void TestTimedWaitDeadline()
	{
		WakeSignal signal;
		const auto observedGeneration = signal.CurrentGeneration();
		const auto waitStart = std::chrono::steady_clock::now();
		const auto generation = signal.WaitUntilGenerationChange(
			observedGeneration, waitStart + 40ms);
		const auto elapsed = std::chrono::steady_clock::now() - waitStart;

		Check(generation == observedGeneration,
			"timed wake deadline returns unchanged generation");
		Check(elapsed >= 20ms,
			"timed wake does not return before deadline");
		Check(elapsed < 1s,
			"timed wake deadline completes promptly");
	}

	void TestNotificationCoalescing()
	{
		WakeSignal signal;
		signal.Notify();
		signal.Notify();
		signal.Notify();
		Check(signal.WaitAndConsume() == 3,
			"wake notifications coalesce into one consumption");

		auto waiter = std::async(std::launch::async,
			[&] { return signal.WaitAndConsume(); });
		Check(waiter.wait_for(30ms) == std::future_status::timeout,
			"wake consumer blocks after consuming current generation");
		signal.Notify();
		Check(waiter.wait_for(1s) == std::future_status::ready,
			"wake consumer resumes after next notification");
		Check(waiter.get() == 4, "wake generation advances after coalescing");
	}

	void TestConsumerClearWindow()
	{
		WakeSignal signal;
		signal.Notify();
		Check(signal.WaitAndConsume() == 1, "wake initial generation consumed");

		// 请求恰好落在两次消费之间，也不能被消费者的清空动作覆盖。
		signal.Notify();
		auto waiter = std::async(std::launch::async,
			[&] { return signal.WaitAndConsume(); });
		bool ready = waiter.wait_for(1s) == std::future_status::ready;
		Check(ready, "wake request survives consumer clear window");
		if (!ready) signal.Notify();
		Check(waiter.get() == 2, "wake clear-window generation");
	}

	void TestProducerStorm()
	{
		WakeSignal signal;
		constexpr std::uint64_t producerCount = 4;
		constexpr std::uint64_t notificationsPerProducer = 5000;
		constexpr std::uint64_t expectedGeneration =
			producerCount * notificationsPerProducer;
		std::atomic<bool> start = false;

		auto consumer = std::async(std::launch::async, [&]
			{
				WakeSignal::Generation observed = 0;
				while (observed < expectedGeneration)
					observed = signal.WaitAndConsume();
				return observed;
			});

		std::vector<std::thread> producers;
		producers.reserve(producerCount);
		for (std::uint64_t producer = 0; producer < producerCount; ++producer)
		{
			producers.emplace_back([&]
				{
					while (!start.load(std::memory_order_acquire))
						std::this_thread::yield();
					for (std::uint64_t i = 0; i < notificationsPerProducer; ++i)
						signal.Notify();
				});
		}
		start.store(true, std::memory_order_release);
		for (auto& producer : producers) producer.join();

		bool ready = consumer.wait_for(2s) == std::future_status::ready;
		Check(ready, "wake producer storm reaches final generation");
		if (!ready) signal.Notify();
		Check(consumer.get() == expectedGeneration,
			"wake producer storm loses no generation");
	}

	void TestGenerationSnapshot()
	{
		WakeSignal signal;
		constexpr std::uint64_t producerCount = 2;
		constexpr std::uint64_t notificationsPerProducer = 2000;
		std::atomic<bool> start = false;
		std::atomic<std::uint64_t> completedProducers = 0;
		std::vector<std::thread> producers;
		producers.reserve(producerCount);
		for (std::uint64_t producer = 0; producer < producerCount; ++producer)
		{
			producers.emplace_back([&]
				{
					while (!start.load(std::memory_order_acquire))
						std::this_thread::yield();
					for (std::uint64_t i = 0; i < notificationsPerProducer; ++i)
						signal.Notify();
					completedProducers.fetch_add(1, std::memory_order_release);
				});
		}

		WakeSignal::Generation previous = signal.CurrentGeneration();
		Check(previous == 0, "wake generation starts at zero");
		start.store(true, std::memory_order_release);
		while (completedProducers.load(std::memory_order_acquire) < producerCount)
		{
			auto current = signal.CurrentGeneration();
			Check(current >= previous, "wake generation snapshot is monotonic");
			previous = current;
			std::this_thread::yield();
		}
		for (auto& producer : producers) producer.join();

		auto finalGeneration = signal.CurrentGeneration();
		Check(finalGeneration >= previous,
			"wake final generation does not move backwards");
		Check(finalGeneration == producerCount * notificationsPerProducer,
			"wake generation snapshot observes every notification");
	}

	void TestIdleShutdownWake()
	{
		WakeSignal signal;
		std::atomic<bool> offSignal = false;
		std::promise<void> enteredWait;
		auto entered = enteredWait.get_future();
		auto renderer = std::async(std::launch::async, [&]
			{
				enteredWait.set_value();
				(void)signal.WaitAndConsume();
				return offSignal.load(std::memory_order_acquire);
			});
		entered.wait();
		Check(renderer.wait_for(30ms) == std::future_status::timeout,
			"wake idle renderer is actually blocked");

		auto notifyTime = std::chrono::steady_clock::now();
		offSignal.store(true, std::memory_order_release);
		signal.Notify();
		bool ready = renderer.wait_for(1s) == std::future_status::ready;
		Check(ready, "wake idle renderer observes shutdown notification");
		if (!ready) signal.Notify();
		Check(renderer.get(), "wake idle renderer observes shutdown state");
		Check(std::chrono::steady_clock::now() - notifyTime < 250ms,
			"wake idle shutdown completes promptly");
	}

	void TestTimedWaitShutdownWake()
	{
		WakeSignal signal;
		std::atomic<bool> offSignal = false;
		const auto observedGeneration = signal.CurrentGeneration();
		std::promise<void> enteredWait;
		auto entered = enteredWait.get_future();
		auto renderer = std::async(std::launch::async, [&]
			{
				enteredWait.set_value();
				const auto generation = signal.WaitUntilGenerationChange(
					observedGeneration,
					std::chrono::steady_clock::now() + 5s);
				return generation != observedGeneration
					&& offSignal.load(std::memory_order_acquire);
			});
		entered.wait();
		Check(renderer.wait_for(30ms) == std::future_status::timeout,
			"timed wake shutdown renderer is blocked");

		const auto notifyTime = std::chrono::steady_clock::now();
		offSignal.store(true, std::memory_order_release);
		signal.Notify();
		const bool ready = renderer.wait_for(1s) == std::future_status::ready;
		Check(ready, "timed wake shutdown notification beats deadline");
		if (!ready) signal.Notify();
		Check(renderer.get(), "timed wake shutdown observes state and generation");
		Check(std::chrono::steady_clock::now() - notifyTime < 250ms,
			"timed wake shutdown completes promptly");
	}
}

int RunWakeSignalTests()
{
	TestPreNotification();
	TestTimedWaitPreNotification();
	TestTimedWaitDeadline();
	TestNotificationCoalescing();
	TestConsumerClearWindow();
	TestProducerStorm();
	TestGenerationSnapshot();
	TestIdleShutdownWake();
	TestTimedWaitShutdownWake();
	return failureCount;
}
