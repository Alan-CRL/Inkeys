#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

import Inkeys.UI.RenderScheduler;

using Inkeys::UI::RenderScheduler::Client;
using Inkeys::UI::RenderScheduler::ClientMask;
using Inkeys::UI::RenderScheduler::DispatchState;
using Inkeys::UI::RenderScheduler::FrameResult;
using Inkeys::UI::RenderScheduler::Mask;

namespace
{
	constexpr auto Count = static_cast<std::size_t>(Client::Count);

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[RenderScheduler] failed: " << name << '\n';
		return condition;
	}
}

int RunRenderSchedulerTests()
{
	using namespace std::chrono_literals;
	using Inkeys::UI::RenderScheduler::Scheduler;
	int failures = 0;
	DispatchState state;
	state.Request(Mask(Client::PptBottomLeft));
	state.Request(Mask(Client::PptMiddleRight));
	const auto first = state.TakeRequested();
	if (!Expect(first == (Mask(Client::PptBottomLeft)
		| Mask(Client::PptMiddleRight)), "request bits merge")) ++failures;
	{
		DispatchState concurrentState;
		std::jthread firstRequester([&]
			{
				for (int index = 0; index < 100; ++index)
					concurrentState.Request(Mask(Client::Bar));
			});
		std::jthread secondRequester([&]
			{
				for (int index = 0; index < 100; ++index)
					concurrentState.Request(Mask(Client::PptExitShow));
			});
		firstRequester.join();
		secondRequester.join();
		if (!Expect(concurrentState.TakeRequested() ==
			(Mask(Client::Bar) | Mask(Client::PptExitShow)),
			"concurrent requests merge without loss")) ++failures;
	}

	std::array<FrameResult, Count> results{};
	results.fill(FrameResult::Idle);
	results[static_cast<std::size_t>(Client::PptBottomLeft)] = FrameResult::Continue;
	results[static_cast<std::size_t>(Client::PptMiddleRight)] = FrameResult::Retry;
	state.Request(Mask(Client::PptExitShow));
	const auto continued = state.Complete(first, results);
	if (!Expect(continued.next == (first | Mask(Client::PptExitShow)),
		"continue retry and concurrent request survive")) ++failures;
	if (!Expect(!continued.sleep, "active clients do not sleep")) ++failures;

	results.fill(FrameResult::Idle);
	const auto idle = state.Complete(continued.next, results);
	if (!Expect(idle.next == 0 && idle.sleep, "all idle sleeps once")) ++failures;

	results.fill(FrameResult::Idle);
	results[static_cast<std::size_t>(Client::PptBottomRight)] = FrameResult::DeviceLost;
	const auto lost = state.Complete(Mask(Client::PptBottomRight), results);
	const ClientMask all = (ClientMask{ 1 } << static_cast<unsigned>(Client::Count)) - 1;
	if (!Expect(lost.rebuildSharedDevice && lost.next == all,
		"device loss requests every registered slot")) ++failures;

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::vector<Client> order;
		std::atomic_bool stop = false;
		auto record = [&](Client client)
			{
				{
					std::scoped_lock lock(mutex);
					order.push_back(client);
				}
				condition.notify_all();
				return FrameResult::Idle;
			};
		(void)scheduler.Register(Client::PptBottomLeft,
			[&] { return record(Client::PptBottomLeft); });
		(void)scheduler.Register(Client::Bar, [&] { return record(Client::Bar); });
		std::jthread renderThread([&] { scheduler.Run([&] { return stop.load(); }); });
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return order.size() == 2; });
		}
		if (!Expect(order == std::vector<Client>{ Client::Bar, Client::PptBottomLeft },
			"runtime dispatch uses stable client order")) ++failures;
		{
			std::scoped_lock lock(mutex);
			order.clear();
		}
		scheduler.Request(Client::PptBottomLeft);
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return order.size() == 1; });
		}
		if (!Expect(order == std::vector<Client>{ Client::PptBottomLeft },
			"single request invokes only its client")) ++failures;
		stop = true;
		scheduler.WakeForStop();
		renderThread.join();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		bool entered = false;
		bool release = false;
		std::atomic_bool stop = false;
		std::atomic_bool unregisterReturned = false;
		(void)scheduler.Register(Client::PptExitShow, [&]
			{
				std::unique_lock lock(mutex);
				entered = true;
				condition.notify_all();
				condition.wait(lock, [&] { return release; });
				return FrameResult::Idle;
			});
		std::jthread renderThread([&] { scheduler.Run([&] { return stop.load(); }); });
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return entered; });
		}
		std::jthread unregisterThread([&]
			{
				scheduler.Unregister(Client::PptExitShow);
				unregisterReturned = true;
			});
		std::this_thread::sleep_for(10ms);
		if (!Expect(!unregisterReturned.load(),
			"unregister drains active callback")) ++failures;
		{
			std::scoped_lock lock(mutex);
			release = true;
		}
		condition.notify_all();
		unregisterThread.join();
		if (!Expect(unregisterReturned.load(),
			"unregister completes after callback")) ++failures;
		stop = true;
		scheduler.WakeForStop();
		renderThread.join();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::vector<std::chrono::steady_clock::time_point> frames;
		std::atomic_bool stop = false;
		(void)scheduler.Register(Client::PptMiddleLeft, [&]
			{
				std::scoped_lock lock(mutex);
				frames.push_back(std::chrono::steady_clock::now());
				condition.notify_all();
				return frames.size() == 2 ? FrameResult::Continue : FrameResult::Idle;
			});
		std::jthread renderThread([&] { scheduler.Run([&] { return stop.load(); }); });
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return frames.size() == 1; });
		}
		std::this_thread::sleep_for(30ms);
		scheduler.Request(Client::PptMiddleLeft);
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return frames.size() == 3; });
		}
		const bool paced = frames.size() == 3 && frames[2] - frames[1] >= 15ms;
		if (!Expect(paced, "continued frames respect 60 fps pacing")) ++failures;
		stop = true;
		scheduler.WakeForStop();
		renderThread.join();
	}

	return failures;
}
