#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

import Inkeys.UI.RenderPipeline;

#include "../Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h"

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
		std::vector<Client> order;
		auto record = [&](Client client)
			{
				{
					std::scoped_lock lock(mutex);
					order.push_back(client);
				}
				condition.notify_all();
				return FrameResult::Idle;
			};
		(void)scheduler.Register(Client::StartupPreview,
			[&](const auto&) { return record(Client::StartupPreview); });
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return order.size() == 1; });
		}
		if (!Expect(order == std::vector<Client>{ Client::StartupPreview },
			"preview can render as the only registered client")) ++failures;
		scheduler.Stop();
	}

	{
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::vector<Client> order;
		auto record = [&](Client client)
			{
				{
					std::scoped_lock lock(mutex);
					order.push_back(client);
				}
				condition.notify_all();
				return FrameResult::Idle;
			};
		(void)scheduler.Register(Client::StartupPreview,
			[&](const auto&) { return record(Client::StartupPreview); });
		(void)scheduler.Register(Client::Bar,
			[&](const auto&) { return record(Client::Bar); });
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return order.size() == 2; });
		}
		if (!Expect(order == std::vector<Client>{ Client::Bar, Client::StartupPreview },
			"Bar dispatches before startup preview")) ++failures;
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

	{
		using namespace Inkeys::UI::RenderPipeline;
		using namespace Inkeys::UI::RenderPipeline::DiagnosticsDetail;
		DiagnosticsAccumulator diagnostics;
		const auto origin = DiagnosticClock::time_point{};
		FrameDiagnostics sample;
		sample.barSampled = true;
		sample.animationAdvanced = true;
		sample.rawDtSeconds = sample.animationDtSeconds = 0.016;
		sample.presentAttempted = sample.presentCommitted = true;
		bool healthySilent = true;
		for (int frame = 0; frame < 130; ++frame)
		{
			const auto time = origin + frame * 16ms;
			diagnostics.BeginBatch(time);
			diagnostics.AddClient(Client::Bar, FrameResult::Continue, sample, time, time + 1ms,
				frame == 0 ? Mask(Client::Bar) : 0, frame == 0 ? 0 : Mask(Client::Bar), 0);
			diagnostics.EndBatch(time + 1ms, Mask(Client::Bar), 0, 0);
			healthySilent &= !diagnostics.Ready(time + 1ms);
		}
		if (!Expect(healthySilent, "healthy sustained frames never request default diagnostic output")) ++failures;
		if (!Expect(diagnostics.WaitDelay(origin + 3s) == (std::chrono::milliseconds::max)(),
			"healthy scheduler diagnostics keep infinite idle wait")) ++failures;

		diagnostics.Reset();
		diagnostics.BeginBatch(origin);
		diagnostics.AddClient(Client::Bar, FrameResult::Continue, sample, origin, origin + 1ms,
			Mask(Client::Bar), 0, 0);
		diagnostics.AddClient(Client::Settings, FrameResult::Idle, {}, origin + 1ms, origin + 81ms,
			Mask(Client::Settings), 0, 0);
		diagnostics.EndBatch(origin + 81ms, Mask(Client::Bar) | Mask(Client::Settings), 0, 0);
		const auto& slow = diagnostics.Summary();
		if (!Expect(diagnostics.Ready(origin + 81ms) && slow.longBatches == 1
			&& slow.clients[static_cast<std::size_t>(Client::Bar)].maxMs == 1.0
			&& slow.clients[static_cast<std::size_t>(Client::Settings)].maxMs == 80.0,
			"shared batch attributes a long Settings callback separately from fast Bar")) ++failures;
		const auto firstAttempt = origin + 81ms;
		diagnostics.BeginAttempt(firstAttempt);
		diagnostics.CompleteAttempt(true, 0.2, 0.3);
		diagnostics.BeginBatch(origin + 82ms);
		diagnostics.AddClient(Client::Bar, FrameResult::Continue, sample, origin + 82ms, origin + 83ms,
			0, Mask(Client::Bar), 0);
		diagnostics.EndBatch(origin + 83ms, 0, Mask(Client::Bar), 0);
		if (!Expect(diagnostics.Summary().clients[0].activeGapMaxMs == 82.0
			&& diagnostics.Summary().longPeriods == 1 && !diagnostics.Ready(origin + 83ms),
			"next Bar callback exposes previous other-client delay while output stays rate limited")) ++failures;

		FrameDiagnostics failure = sample;
		failure.animationAdvanced = false;
		failure.presentCommitted = false;
		failure.presentFailed = true;
		failure.getDcResult = -7;
		failure.callbackException = true;
		failure.rawDtSeconds = 0.5;
		failure.animationDtSeconds = 0.05;
		diagnostics.BeginBatch(origin + 100ms);
		diagnostics.AddClient(Client::Bar, FrameResult::Retry, failure, origin + 100ms, origin + 101ms,
			Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 101ms, Mask(Client::Bar), 0, 0);
		diagnostics.BeginBatch(origin + 116ms);
		diagnostics.AddClient(Client::Bar, FrameResult::Idle, sample, origin + 116ms, origin + 117ms,
			0, 0, Mask(Client::Bar));
		diagnostics.EndBatch(origin + 117ms, 0, 0, Mask(Client::Bar));
		diagnostics.MarkIdle();
		const auto& retained = diagnostics.Summary().clients[0];
		if (!Expect(retained.failures == 1 && retained.recoveries == 1 && retained.exceptions == 1
			&& retained.latest.getDcResult == 0 && retained.lastFailure.getDcResult == -7
			&& retained.advanced == 2 && retained.usedDtSeconds == 0.032,
			"late failure exception and recovery survive healthy samples; skipped dt is not animation time")) ++failures;
		if (!Expect(!diagnostics.Ready(firstAttempt + 999ms)
			&& diagnostics.Ready(firstAttempt + 1s)
			&& diagnostics.WaitDelay(firstAttempt + 999ms) == 1ms,
			"diagnostic limit includes the exact one-second boundary")) ++failures;
		diagnostics.BeginAttempt(firstAttempt + 1s);
		diagnostics.CompleteAttempt(false, 0.1, 0.2);
		if (!Expect(!diagnostics.Ready(firstAttempt + 1999ms)
			&& diagnostics.Ready(firstAttempt + 2s) && diagnostics.SinkRejected() == 1
			&& diagnostics.Summary().clients[0].lastFailure.getDcResult == -7,
			"rejected sink retains events and consumes one-second attempt budget")) ++failures;
		diagnostics.BeginAttempt(firstAttempt + 2s);
		diagnostics.CompleteAttempt(true, 0.1, 0.2);
		if (!Expect(!diagnostics.Ready(firstAttempt + 10s)
			&& diagnostics.Summary().batches == 0,
			"accepted anomaly does not keep producing healthy summaries")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		using namespace Inkeys::UI::RenderPipeline::DiagnosticsDetail;
		DiagnosticsAccumulator diagnostics;
		const auto origin = DiagnosticClock::time_point{};
		FrameDiagnostics committed;
		committed.barSampled = true;
		committed.presentCommitted = true;
		diagnostics.BeginBatch(origin);
		diagnostics.AddClient(Client::Bar, FrameResult::Continue, committed, origin, origin + 1ms, Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 1ms, Mask(Client::Bar), 0, 0);
		diagnostics.MarkIdle();
		diagnostics.BeginBatch(origin + 10s);
		diagnostics.AddClient(Client::Bar, FrameResult::Continue, committed, origin + 10s, origin + 10s + 1ms,
			Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 10s + 1ms, Mask(Client::Bar), 0, 0);
		if (!Expect(!diagnostics.Ready(origin + 11s)
			&& diagnostics.Summary().clients[0].commitRawGapMaxMs == 10000.0
			&& diagnostics.Summary().clients[0].commitActiveGapMaxMs == 0.0
			&& diagnostics.Summary().clients[0].activeGapMaxMs == 0.0,
			"real scheduler idle is excluded while raw successful-present gap remains observable")) ++failures;

		diagnostics.Reset();
		diagnostics.BeginBatch(origin);
		diagnostics.AddClient(Client::Bar, FrameResult::Idle, committed, origin, origin + 1ms, Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 1ms, Mask(Client::Bar), 0, 0);
		for (int frame = 1; frame < 10; ++frame)
		{
			const auto time = origin + frame * 20ms;
			diagnostics.BeginBatch(time);
			diagnostics.AddClient(Client::Settings, FrameResult::Continue, {}, time, time + 1ms, 0, Mask(Client::Settings), 0);
			diagnostics.EndBatch(time + 1ms, 0, Mask(Client::Settings), 0);
		}
		diagnostics.BeginBatch(origin + 200ms);
		diagnostics.AddClient(Client::Bar, FrameResult::Continue, committed, origin + 200ms, origin + 201ms,
			Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 201ms, Mask(Client::Bar), 0, 0);
		if (!Expect(!diagnostics.Ready(origin + 201ms)
			&& diagnostics.Summary().clients[0].commitRawGapMaxMs == 200.0
			&& diagnostics.Summary().clients[0].commitActiveGapMaxMs == 0.0,
			"an idle Bar is not diagnosed as stalled while another client continues")) ++failures;

		diagnostics.ResetClientActivity(Client::Bar);
		diagnostics.BeginBatch(origin + 220ms);
		diagnostics.AddClient(Client::Bar, FrameResult::Idle, committed, origin + 220ms, origin + 221ms,
			Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 221ms, Mask(Client::Bar), 0, 0);
		if (!Expect(!diagnostics.Ready(origin + 221ms) && diagnostics.Summary().clients[0].activeGapMaxMs == 0.0,
			"new registration in an existing slot does not inherit an old activity chain")) ++failures;

		diagnostics.Reset();
		FrameDiagnostics deferred;
		deferred.barSampled = deferred.presentDeferred = true;
		diagnostics.BeginBatch(origin);
		diagnostics.AddClient(Client::Bar, FrameResult::Retry, deferred, origin, origin + 1ms, Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 1ms, Mask(Client::Bar), 0, 0);
		if (!Expect(!diagnostics.Ready(origin + 1s) && diagnostics.Summary().clients[0].results[2] == 1
			&& diagnostics.Summary().clients[0].deferred == 1,
			"legal Retry is counted without inventing a presentation failure")) ++failures;
		diagnostics.Reset();
		FrameDiagnostics degraded = committed;
		degraded.light.roundedParentFailure = 1;
		degraded.light.geometryParentFailure = 1;
		degraded.light.exactFallback[static_cast<std::size_t>(ExactFallback::CreateFailure)] = 1;
		diagnostics.BeginBatch(origin);
		diagnostics.AddClient(Client::Bar, FrameResult::Continue, degraded, origin, origin + 1ms, Mask(Client::Bar), 0, 0);
		diagnostics.EndBatch(origin + 1ms, Mask(Client::Bar), 0, 0);
		diagnostics.BeginBatch(origin + 16ms);
		diagnostics.AddClient(Client::Bar, FrameResult::Idle, committed, origin + 16ms, origin + 17ms, 0, Mask(Client::Bar), 0);
		diagnostics.EndBatch(origin + 17ms, 0, Mask(Client::Bar), 0);
		if (!Expect(diagnostics.Ready(origin + 17ms) && diagnostics.Summary().clients[0].lightFailureFrames == 1
			&& diagnostics.Summary().clients[0].failures == 0 && diagnostics.Summary().clients[0].recoveries == 0,
			"lighting allocation degradation is reported separately from present failure and recovery")) ++failures;
		diagnostics.Reset();
		FrameDiagnostics pageFailure;
		pageFailure.presentAttempted = pageFailure.presentFailed = true;
		pageFailure.getDcResult = -8;
		diagnostics.BeginBatch(origin);
		diagnostics.AddClient(Client::PptBottomLeft, FrameResult::Retry, pageFailure, origin, origin + 1ms,
			Mask(Client::PptBottomLeft), 0, 0);
		diagnostics.EndBatch(origin + 1ms, Mask(Client::PptBottomLeft), 0, 0);
		diagnostics.BeginBatch(origin + 16ms);
		diagnostics.AddClient(Client::PptBottomLeft, FrameResult::Idle, {}, origin + 16ms, origin + 17ms,
			0, 0, Mask(Client::PptBottomLeft));
		diagnostics.EndBatch(origin + 17ms, 0, 0, Mask(Client::PptBottomLeft));
		if (!Expect(diagnostics.Summary().clients[static_cast<std::size_t>(Client::PptBottomLeft)].recoveries == 0,
			"PageControl hidden after failure is not mistaken for a successful present")) ++failures;
		FrameDiagnostics pageCommit;
		pageCommit.presentCommitted = true;
		diagnostics.BeginBatch(origin + 32ms);
		diagnostics.AddClient(Client::PptBottomLeft, FrameResult::Idle, pageCommit, origin + 32ms, origin + 33ms,
			Mask(Client::PptBottomLeft), 0, 0);
		diagnostics.EndBatch(origin + 33ms, Mask(Client::PptBottomLeft), 0, 0);
		if (!Expect(diagnostics.Summary().clients[static_cast<std::size_t>(Client::PptBottomLeft)].recoveries == 1,
			"present failure recovers only after an actual successful commit")) ++failures;
		diagnostics.AddRecovery(false, 4.0);
		diagnostics.AddRecovery(true, 3.0);
		if (!Expect(diagnostics.Ready(origin + 1s) && diagnostics.Summary().recoveryAttempts == 2
			&& diagnostics.Summary().recoveryFailures == 1 && diagnostics.Summary().recoverySuccesses == 1,
			"shared device recovery failures and success are retained independently")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::vector<std::string> messages;
		std::vector<std::chrono::steady_clock::time_point> sent;
		std::atomic_int barCalls = 0, settingsCalls = 0, continuation = 0;
		std::atomic_bool slowSettings = false, noSinkSawSample = false, installed = false;
		std::atomic_bool sinkHadSample = false, sinkReentered = false;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				const auto call = ++barCalls;
				if (call == 1) noSinkSawSample = CurrentFrameDiagnostics() != nullptr;
				if (auto* sample = CurrentFrameDiagnostics())
				{
					sample->barSampled = sample->animationAdvanced = sample->presentCommitted = sample->ulwSucceeded = true;
					sample->rawDtSeconds = sample->animationDtSeconds = 0.016;
				}
				condition.notify_all();
				return continuation.exchange(0) != 0 ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				++settingsCalls;
				if (slowSettings.exchange(false)) std::this_thread::sleep_for(70ms);
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return barCalls.load() == 1 && settingsCalls.load() == 1; });
		}
		const bool sinkAccepted = scheduler.SetDiagnosticsSink([&](std::string_view message)
			{
				sinkHadSample = CurrentFrameDiagnostics() != nullptr;
				// PostControl 取得调度锁，验证 sink 确实在内部锁外调用。
				sinkReentered = scheduler.PostControl([] {});
				{
					std::scoped_lock lock(mutex);
					messages.emplace_back(message);
					sent.push_back(std::chrono::steady_clock::now());
				}
				condition.notify_all();
				return true;
			});
		(void)scheduler.PostControl([&] { installed = true; condition.notify_all(); });
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return installed.load(); });
		}
		continuation = 1;
		slowSettings = true;
		scheduler.Request(Mask(Client::Bar) | Mask(Client::Settings));
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 3s, [&] { return messages.size() >= 2; });
		}
		scheduler.Stop();
		if (!Expect(sinkAccepted && installed.load() && !noSinkSawSample.load() && !sinkHadSample.load()
			&& sinkReentered.load() && CurrentFrameDiagnostics() == nullptr,
			"runtime sink can install after Start, stays outside locks and callback TLS")) ++failures;
		if (!Expect(messages.size() == 2 && sent[1] - sent[0] >= 990ms
			&& barCalls.load() == 3 && settingsCalls.load() == 2,
			"pending idle diagnostics flush at the limit without extra client callbacks")) ++failures;
		if (!Expect(!messages.empty() && messages[0].find("Bar{calls=1") != std::string::npos
			&& messages[0].find("Settings{calls=1") != std::string::npos
			&& messages[0].find("longBatches=1") != std::string::npos
			&& messages[0].find("ulwSuccess=1") != std::string::npos,
			"real scheduler log contains separate fast-Bar and slow-Settings work")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		Scheduler independent;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int callbacks = 0, attempts = 0;
		std::atomic_bool isolated = false;
		std::string acceptedMessage;
		(void)scheduler.SetDiagnosticsSink([&](std::string_view message)
			{
				if (++attempts == 1) throw std::runtime_error("diagnostic sink rejected");
				{
					std::scoped_lock lock(mutex);
					acceptedMessage.assign(message);
				}
				condition.notify_all();
				return true;
			});
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				if (auto* sample = CurrentFrameDiagnostics())
				{
					sample->barSampled = true;
					sample->presentCommitted = callbacks.load() != 0;
				}
				if (++callbacks == 1) throw std::runtime_error("render callback failed");
				return FrameResult::Idle;
			});
		(void)independent.Register(Client::Bar, [&](const auto&)
			{
				isolated = CurrentFrameDiagnostics() == nullptr;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		(void)independent.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 3s, [&] { return !acceptedMessage.empty() && isolated.load(); });
		}
		scheduler.Stop();
		independent.Stop();
		if (!Expect(attempts.load() == 2 && callbacks.load() == 2 && isolated.load()
			&& acceptedMessage.find("exception=1") != std::string::npos
			&& acceptedMessage.find("recovered=1") != std::string::npos
			&& acceptedMessage.find("sinkRejected=1") != std::string::npos,
			"sink exceptions preserve real callback failure and recovery without leaking across schedulers")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		FrameDiagnostics sample;
		double stoppedMs = 0.0;
		{
			FrameStageTimer timer(&sample, FrameStage::Draw);
			std::this_thread::sleep_for(1ms);
			timer.Stop();
			stoppedMs = sample.stageMs[static_cast<std::size_t>(FrameStage::Draw)];
			// 显式结算后重复 Stop 和析构都不能把同一阶段再计一次。
			timer.Stop();
		}
		if (!Expect(stoppedMs > 0.0
			&& sample.stageMs[static_cast<std::size_t>(FrameStage::Draw)] == stoppedMs,
			"stage timer Stop and destruction accumulate the elapsed interval exactly once")) ++failures;
		FrameStageTimer disabled(nullptr, FrameStage::Draw);
		disabled.Stop();
		disabled.Stop();
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int callbacks = 0;
		std::atomic_bool restartedWithoutSample = false;
		int rejectedAttempts = 0;
		std::chrono::steady_clock::time_point rejectedAt{}, acceptedAt{};
		std::string acceptedMessage;
		(void)scheduler.SetDiagnosticsSink([&](std::string_view)
			{
				{
					std::scoped_lock lock(mutex);
					++rejectedAttempts;
					rejectedAt = std::chrono::steady_clock::now();
				}
				condition.notify_all();
				return false;
			});
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				const auto call = ++callbacks;
				auto* sample = CurrentFrameDiagnostics();
				if (call == 1 && sample)
				{
					sample->barSampled = sample->presentFailed = true;
					sample->getDcResult = -9;
				}
				if (call == 2) restartedWithoutSample = sample == nullptr;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return rejectedAttempts != 0; });
		}
		// 非空 sink 的替换继续交付旧聚合，并保留旧尝试的限频起点。
		const bool replaced = scheduler.SetDiagnosticsSink([&](std::string_view message)
			{
				{
					std::scoped_lock lock(mutex);
					acceptedMessage.assign(message);
					acceptedAt = std::chrono::steady_clock::now();
				}
				condition.notify_all();
				return true;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 3s, [&] { return !acceptedMessage.empty(); });
		}
		scheduler.Stop();
		if (!Expect(replaced && rejectedAttempts != 0 && callbacks.load() == 1
			&& acceptedAt - rejectedAt >= 990ms
			&& acceptedMessage.find("fail=1") != std::string::npos
			&& acceptedMessage.find("lastFailure[") != std::string::npos
			&& acceptedMessage.find("sinkRejected=0") == std::string::npos,
			"replacing a nonempty sink preserves rejected failure evidence and its rate limit")) ++failures;
		// Stop 应清理已安装 sink；同一个客户端在下一轮只能收到新的请求。
		scheduler.Stop();
		scheduler.Request(Client::Bar);
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return callbacks.load() == 2; });
		}
		scheduler.Stop();
		if (!Expect(callbacks.load() == 2 && restartedWithoutSample.load()
			&& CurrentFrameDiagnostics() == nullptr,
			"Stop is repeatable and restart does not retain the previous diagnostics sink or TLS")) ++failures;
	}

	return failures;
}
