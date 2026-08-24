#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

import Inkeys.UI.RenderPipeline;

using Inkeys::UI::RenderPipeline::Client;
using Inkeys::UI::RenderPipeline::ClientMask;
using Inkeys::UI::RenderPipeline::DispatchState;
using Inkeys::UI::RenderPipeline::FrameResult;
using Inkeys::UI::RenderPipeline::Mask;

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
	using Inkeys::UI::RenderPipeline::Scheduler;
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
					concurrentState.Request(Mask(Client::WhiteboardFreeze));
			});
		firstRequester.join();
		secondRequester.join();
		if (!Expect(concurrentState.TakeRequested() ==
			(Mask(Client::Bar) | Mask(Client::WhiteboardFreeze)),
			"concurrent requests merge without loss")) ++failures;
	}

	std::array<FrameResult, Count> results{};
	results.fill(FrameResult::Idle);
	results[static_cast<std::size_t>(Client::PptBottomLeft)] = FrameResult::Continue;
	results[static_cast<std::size_t>(Client::PptMiddleRight)] = FrameResult::Retry;
	state.Request(Mask(Client::WhiteboardFreeze));
	const ClientMask all = (ClientMask{ 1 } << static_cast<unsigned>(Client::Count)) - 1;
	const auto continued = state.Complete(first, all, results);
	if (!Expect(continued.next == (first | Mask(Client::WhiteboardFreeze)),
		"continue retry and concurrent request survive")) ++failures;
	if (!Expect(!continued.sleep, "active clients do not sleep")) ++failures;

	results.fill(FrameResult::Idle);
	const auto idle = state.Complete(continued.next, all, results);
	if (!Expect(idle.next == 0 && idle.sleep, "all idle sleeps once")) ++failures;
	state.Request(Mask(Client::Settings));
	const auto registrationRace = state.Complete(0, Mask(Client::Bar), results);
	if (!Expect(registrationRace.next == Mask(Client::Settings),
		"explicit request survives stale registered snapshot")) ++failures;

	results.fill(FrameResult::Idle);
	results[static_cast<std::size_t>(Client::PptBottomRight)] = FrameResult::DeviceLost;
	const ClientMask registered = Mask(Client::Bar)
		| Mask(Client::PptBottomRight) | Mask(Client::Settings);
	const auto lost = state.Complete(
		Mask(Client::PptBottomRight), registered, results);
	if (!Expect(lost.rebuildSharedDevice && lost.next == registered,
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
			[&](const auto&) { return record(Client::PptBottomLeft); });
		(void)scheduler.Register(Client::Bar,
			[&](const auto&) { return record(Client::Bar); });
		(void)scheduler.Start();
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
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		bool entered = false;
		bool release = false;
		std::atomic_bool stop = false;
		std::atomic_bool unregisterReturned = false;
		(void)scheduler.Register(Client::WhiteboardFreeze, [&](const auto&)
			{
				std::unique_lock lock(mutex);
				entered = true;
				condition.notify_all();
				condition.wait(lock, [&] { return release; });
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return entered; });
		}
		std::jthread unregisterThread([&]
			{
				scheduler.Unregister(Client::WhiteboardFreeze);
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
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::vector<std::chrono::steady_clock::time_point> frames;
		std::atomic_bool stop = false;
		(void)scheduler.Register(Client::PptMiddleLeft, [&](const auto&)
			{
				std::scoped_lock lock(mutex);
				frames.push_back(std::chrono::steady_clock::now());
				condition.notify_all();
				return frames.size() == 2 ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Start();
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
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		bool barEntered = false;
		bool releaseBar = false;
		std::atomic_int settingsCallbacks = 0;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				std::unique_lock lock(mutex);
				barEntered = true;
				condition.notify_all();
				condition.wait(lock, [&] { return releaseBar; });
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return barEntered; });
		}
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				++settingsCallbacks;
				condition.notify_all();
				return FrameResult::Idle;
			});
		{
			std::scoped_lock lock(mutex);
			releaseBar = true;
		}
		condition.notify_all();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s,
				[&] { return settingsCallbacks.load() == 1; });
		}
		if (!Expect(settingsCallbacks.load() == 1,
			"registration during callback keeps initial request")) ++failures;
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::vector<std::chrono::steady_clock::time_point> settingsFrames;
		std::atomic_int barFrames = 0;
		std::atomic_bool visible = true;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				++barFrames;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				std::scoped_lock lock(mutex);
				settingsFrames.push_back(std::chrono::steady_clock::now());
				condition.notify_all();
				return visible.load() ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return settingsFrames.size() == 4; });
		}
		visible = false;
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return settingsFrames.size() >= 5; });
		}
		std::vector<std::chrono::steady_clock::time_point> stoppedFrames;
		{
			std::scoped_lock lock(mutex);
			stoppedFrames = settingsFrames;
		}
		const bool settingsPaced = stoppedFrames.size() >= 4
			&& stoppedFrames[1] - stoppedFrames[0] >= 15ms
			&& stoppedFrames[2] - stoppedFrames[1] >= 15ms
			&& stoppedFrames[3] - stoppedFrames[2] >= 15ms;
		if (!Expect(settingsPaced, "settings continuous frames respect 60 fps"))
			++failures;
		if (!Expect(barFrames.load() == 1,
			"settings continuous frames do not invoke idle bar")) ++failures;
		const auto hiddenCount = stoppedFrames.size();
		std::this_thread::sleep_for(40ms);
		{
			std::scoped_lock lock(mutex);
			if (!Expect(settingsFrames.size() == hiddenCount,
				"hidden settings leaves scheduler idle")) ++failures;
		}
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int barCallbacks = 0;
		std::atomic_int settingsCallbacks = 0;
		bool recoveryEntered = false;
		bool releaseRecovery = false;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				const auto frame = ++barCallbacks;
				condition.notify_all();
				return frame == 1 ? FrameResult::DeviceLost : FrameResult::Idle;
			});
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				++settingsCallbacks;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start({}, [&]
			{
				std::unique_lock lock(mutex);
				recoveryEntered = true;
				condition.notify_all();
				condition.wait(lock, [&] { return releaseRecovery; });
				return true;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return recoveryEntered; });
		}
		if (!Expect(settingsCallbacks.load() == 0,
			"device loss skips later clients before recovery")) ++failures;
		{
			std::scoped_lock lock(mutex);
			releaseRecovery = true;
		}
		condition.notify_all();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s,
				[&] { return settingsCallbacks.load() == 1; });
		}
		if (!Expect(barCallbacks.load() == 2 && settingsCallbacks.load() == 1,
			"recovery retries all registered clients with new frame")) ++failures;
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::thread::id controlThread;
		std::thread::id callbackThread;
		std::atomic_int generation = 1;
		std::atomic_int observedGeneration = 0;
		std::atomic_int callbacks = 0;
		(void)scheduler.Register(Client::Settings, [&](const auto& context)
			{
				{
					std::scoped_lock lock(mutex);
					callbackThread = std::this_thread::get_id();
				}
				observedGeneration = static_cast<int>(context.epoch.generation);
				++callbacks;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start(
			[&](auto frameTime)
			{
				Inkeys::UI::RenderPipeline::FrameContext context;
				context.frameTime = frameTime;
				context.epoch.generation = generation.load();
				return context;
			}, {}, [&]
			{
				{
					std::scoped_lock lock(mutex);
					controlThread = std::this_thread::get_id();
				}
				generation = 2;
				return true;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return callbacks.load() == 1; });
		}
		scheduler.RequestControl();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return callbacks.load() == 2; });
		}
		if (!Expect(callbacks.load() == 2 && observedGeneration.load() == 2,
			"control publish requests registered clients with new epoch")) ++failures;
		bool sameThread = false;
		{
			std::scoped_lock lock(mutex);
			sameThread = controlThread == callbackThread;
		}
		if (!Expect(sameThread,
			"control publish runs on render thread")) ++failures;
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		(void)scheduler.Start();
		const auto stopStart = std::chrono::steady_clock::now();
		scheduler.Stop();
		if (!Expect(std::chrono::steady_clock::now() - stopStart < 500ms,
			"stop wakes infinite idle wait")) ++failures;
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_bool callbackFinished = false;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				callbackFinished = true;
				condition.notify_all();
				return FrameResult::Stop;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return callbackFinished.load(); });
		}
		std::this_thread::sleep_for(40ms);
		if (!Expect(!scheduler.PostControl([] {}),
			"natural scheduler exit rejects control tasks")) ++failures;
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int callbacks = 0;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				++callbacks;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return callbacks.load() == 1; });
		}
		scheduler.Stop();
		(void)scheduler.Start();
		std::this_thread::sleep_for(40ms);
		if (!Expect(callbacks.load() == 1,
			"restart does not replay stale requests")) ++failures;
		scheduler.Request(Client::Bar);
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return callbacks.load() == 2; });
		}
		if (!Expect(callbacks.load() == 2,
			"restart accepts new requests")) ++failures;
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int callbacks = 0;
		std::atomic_int recoveryAttempts = 0;
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				const auto frame = ++callbacks;
				condition.notify_all();
				return frame == 1 ? FrameResult::DeviceLost : FrameResult::Idle;
			});
		(void)scheduler.Start({}, [&]
			{
				const auto attempt = ++recoveryAttempts;
				condition.notify_all();
				return attempt >= 2;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&]
				{ return recoveryAttempts.load() >= 2 && callbacks.load() >= 2; });
		}
		if (!Expect(recoveryAttempts.load() >= 2 && callbacks.load() == 2,
			"failed device recovery retries before client callback")) ++failures;
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::thread::id renderThread;
		std::thread::id controlThread;
		std::atomic_int callbacks = 0;
		std::atomic_int recoveryAttempts = 0;
		std::atomic_int attemptsAtControl = 0;
		std::atomic_bool controlExecuted = false;
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				{
					std::scoped_lock lock(mutex);
					renderThread = std::this_thread::get_id();
				}
				++callbacks;
				condition.notify_all();
				return FrameResult::DeviceLost;
			});
		(void)scheduler.Start({}, [&]
			{
				++recoveryAttempts;
				condition.notify_all();
				return false;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return recoveryAttempts.load() >= 1; });
		}
		const bool posted = scheduler.PostControl([&]
			{
				{
					std::scoped_lock lock(mutex);
					controlThread = std::this_thread::get_id();
				}
				attemptsAtControl = recoveryAttempts.load();
				controlExecuted = true;
				condition.notify_all();
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return controlExecuted.load(); });
		}
		bool sameThread = false;
		{
			std::scoped_lock lock(mutex);
			sameThread = renderThread == controlThread;
		}
		if (!Expect(posted && controlExecuted.load() && attemptsAtControl.load() >= 1,
			"control task runs while device recovery remains pending")) ++failures;
		if (!Expect(sameThread && callbacks.load() == 1,
			"control task uses render thread without invoking old epoch clients")) ++failures;
		scheduler.Stop();
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		const HRESULT initializeResult = Initialize();
		const auto epoch = GetDeviceEpoch();
		const auto assets = GetSharedAssets();
		const bool valid = initializeResult >= 0
			&& epoch.backend == Backend::Warp
			&& epoch.featureLevel >= D3D_FEATURE_LEVEL_11_0
			&& epoch.d3dDevice && epoch.immediateContext
			&& epoch.dxgiDevice && epoch.dxgiFactory && epoch.d2dDevice
			&& assets.d2dFactory && assets.dwriteFactory;
		if (!Expect(valid, "headless WARP shared assets initialize")) ++failures;
		Shutdown();
	}

	return failures;
}
