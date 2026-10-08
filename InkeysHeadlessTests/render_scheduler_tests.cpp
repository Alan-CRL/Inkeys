#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.Presentation.Source.h"

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

	std::int64_t barTestTicks = 150;
	int barTestClockReads = 0;
	std::int64_t ReadBarTestClock() noexcept { ++barTestClockReads; return barTestTicks; }
	std::int64_t ReadInvalidBarClock() noexcept { return 1; }

	void ObserveBarCommit(Inkeys::UI::RenderPipeline::FrameDiagnostics& frame,
		const Inkeys::UI::Bar::BarPresentAttemptResult& attempt,
		std::uint64_t serial, std::uint64_t epoch,
		Inkeys::UI::RenderPipeline::BarCommitClock clock = nullptr)
	{
		using namespace Inkeys::UI::Bar;
		BarPresentDecision decision;
		decision.AddDemand({ true, false, false });
		const auto completion = decision.CompleteAttempt(attempt, epoch, 1, serial);
		frame.barSampled = true;
		frame.epoch = epoch;
		frame.presentAttemptFrameSerial = serial;
		frame.presentAttempted = attempt.lease == BarPresentLeaseOutcome::Acquired;
		frame.presentDeferred = attempt.lease == BarPresentLeaseOutcome::CosmeticSkipped;
		if (frame.presentAttempted)
		{
			frame.getDcResult = attempt.getDcHr;
			frame.releaseDcResult = attempt.releaseDcHr;
			frame.endDrawResult = attempt.endDrawHr;
			frame.ulwAttempted = SUCCEEDED(attempt.getDcHr);
			frame.ulwSucceeded = attempt.updateLayeredWindowResult != FALSE;
			frame.ulwError = frame.ulwAttempted && !frame.ulwSucceeded ? ERROR_GEN_FAILURE : 0;
		}
		// 共用实际决策和戳函数；合成 API 结果只验证软件合同，不代表执行了真实 ULW。
		Inkeys::UI::RenderPipeline::StampBarCommit(&frame, completion.IsCommitted(), serial, epoch, clock);
		frame.presentCommitted = completion.IsCommitted();
		frame.presentFailed = frame.presentAttempted && !completion.IsCommitted();
	}

	Inkeys::UI::Bar::Ui3FiniteSignature FiniteSignature()
	{
		Inkeys::UI::Bar::Ui3FiniteSignature signature;
		signature.flags = 64;
		signature.stateMode = 1;
		signature.penMode = 0;
		signature.penColorRgb = 0x102030;
		signature.penWidthBits = std::bit_cast<std::uint32_t>(3.0f);
		signature.thicknessView = 0;
		signature.dpi = 96;
		signature.toolRevision = 1;
		signature.displaySerial = 2;
		signature.configZoomBits = std::bit_cast<std::uint64_t>(1.0);
		signature.validMask = Inkeys::UI::Bar::Ui3FiniteRequiredMask;
		return signature;
	}

	void PublishFinite(Inkeys::UI::Bar::Ui3FinitePublication& publication, std::uint64_t step,
		Inkeys::UI::Bar::Ui3FiniteScene scene, const Inkeys::UI::Bar::Ui3FiniteSignature& signature)
	{
		auto mutation = publication.BeginMutation(step, scene, step);
		publication.MarkBusinessAccepted(mutation);
		publication.ObserveBusinessWrite(mutation);
		publication.FinishAtRenderRequest(mutation, signature, static_cast<std::int64_t>(100 + step));
	}

	struct FinitePause
	{
		std::mutex mutex;
		std::condition_variable condition;
		Inkeys::UI::Bar::Ui3FiniteTestPoint target;
		bool entered = false, release = false;
		static void Checkpoint(Inkeys::UI::Bar::Ui3FiniteTestPoint point, void* context) noexcept
		{
			auto& self = *static_cast<FinitePause*>(context);
			if (point != self.target) return;
			std::unique_lock lock(self.mutex);
			self.entered = true;
			self.condition.notify_all();
			self.condition.wait(lock, [&] { return self.release; });
		}
		bool Wait()
		{
			std::unique_lock lock(mutex);
			return condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; });
		}
		void Resume()
		{
			{
				std::scoped_lock lock(mutex);
				release = true;
			}
			condition.notify_all();
		}
	};
	int finiteClockReads = 0;
	std::int64_t ReadFiniteTestClock() noexcept { ++finiteClockReads; return 400; }

	struct CapturePause
	{
		std::mutex mutex;
		std::condition_variable condition;
		Inkeys::UI::RenderPipeline::RawCaptureTestPoint target;
		bool entered = false, release = false;

		static void Pause(Inkeys::UI::RenderPipeline::RawCaptureTestPoint point, void* context)
		{
			auto& self = *static_cast<CapturePause*>(context);
			if (point != self.target) return;
			std::unique_lock lock(self.mutex);
			self.entered = true;
			self.condition.notify_all();
			self.condition.wait(lock, [&] { return self.release; });
		}

		bool Wait(std::chrono::milliseconds timeout)
		{
			std::unique_lock lock(mutex);
			return condition.wait_for(lock, timeout, [&] { return entered; });
		}

		void Resume()
		{
			{
				std::scoped_lock lock(mutex);
				release = true;
			}
			condition.notify_all();
		}
	};
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

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int callbacks = 0;
		std::atomic_bool sawSample = false;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				sawSample = CurrentFrameDiagnostics() != nullptr;
				++callbacks;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return callbacks.load() != 0; });
		}
		scheduler.Stop();
		if (!Expect(callbacks.load() == 1 && !sawSample.load() && !scheduler.TakeRawCapture(),
			"R01 default capture stays off and creates no report")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int calls = 0;
		std::atomic_bool sawSample = false;
		const bool configured = scheduler.ConfigureRawCapture(2);
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				if (auto* sample = CurrentFrameDiagnostics())
				{
					sawSample = true;
					sample->barSampled = true;
					sample->animationAdvanced = true;
					sample->presentAttempted = true;
					sample->presentCommitted = true;
					sample->rawDtSeconds = 0.016;
					FrameStageTimer timer(sample, FrameStage::Draw);
					std::this_thread::sleep_for(1ms);
					timer.Stop();
				}
				const auto count = ++calls;
				condition.notify_all();
				return count < 5 ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return calls.load() >= 5; });
		}
		// 运行期间不能读写一半的数组；报告只在原线程 join 后转交。
		const bool liveUnavailable = !scheduler.TakeRawCapture();
		scheduler.Stop();
		auto report = scheduler.TakeRawCapture();
		if (!Expect(configured && sawSample.load() && calls.load() == 5 && liveUnavailable
			&& CurrentFrameDiagnostics() == nullptr,
			"R02 raw-only capture activates callback TLS without anomaly sink")) ++failures;
		if (!Expect(report && report->sealed && report->capacity == 2
			&& report->callbackSeen == 5 && report->callbackRetained == 2
			&& report->callbackDropped == 3 && report->callbacks.size() == 2
			&& report->batchSeen >= 5 && report->batchRetained == 2
			&& report->batchSeen == report->batchRetained + report->batchDropped,
			"R03 bounded callback and batch reports preserve explicit overflow denominators")) ++failures;
		if (!Expect(report && !report->callbacks.empty() && report->callbacks[0].frame.barSampled
			&& report->callbacks[0].frame.animationAdvanced
			&& report->callbacks[0].frame.presentAttempted
			&& report->callbacks[0].frame.presentCommitted
			&& report->callbacks[0].frame.stageMs[static_cast<std::size_t>(FrameStage::Draw)] > 0
			&& report->callbacks[0].startTicks <= report->callbacks[0].endTicks
			&& report->callbacks[0].client == Client::Bar,
			"R04 raw payload copies actual production callback result and numeric stages")) ++failures;
		if (!Expect(!scheduler.TakeRawCapture(), "R07 sealed report transfers exactly once")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		CapturePause pause{ {}, {}, RawCaptureTestPoint::AfterAllocation };
		scheduler.SetRawCaptureTestHookForTests(&CapturePause::Pause, &pause);
		bool obsoleteAccepted = false;
		std::jthread configuring([&] { obsoleteAccepted = scheduler.ConfigureRawCapture(2); });
		const bool paused = pause.Wait(2s);
		const bool duplicateRejected = !scheduler.ConfigureRawCapture(2)
			&& !scheduler.ConfigureRawCapture(0);
		(void)scheduler.Start();
		scheduler.Stop();
		pause.Resume();
		configuring.join();
		scheduler.SetRawCaptureTestHookForTests(nullptr, nullptr);
		const bool freshAccepted = scheduler.ConfigureRawCapture(2);
		if (!Expect(paused && duplicateRejected && !obsoleteAccepted && freshAccepted,
			"R13 Configure reservation rejects duplicate, zero-capacity steal and Start-Stop ABA")) ++failures;
		if (!Expect(scheduler.ConfigureRawCapture(0),
			"R12 zero capacity cancels a prepared capture before any run")) ++failures;
		if (!Expect(!scheduler.ConfigureRawCapture(65537),
			"R12 over-limit capture cannot allocate or change the next run")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		bool finalTaskEntered = false, finalTaskRelease = false;
		std::atomic_int callbacks = 0;
		const bool configured = scheduler.ConfigureRawCapture(4);
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				++callbacks;
				(void)scheduler.PostControl([&]
					{
						std::unique_lock lock(mutex);
						finalTaskEntered = true;
						condition.notify_all();
						condition.wait(lock, [&] { return finalTaskRelease; });
					});
				return FrameResult::Stop;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return finalTaskEntered; });
		}
		const bool unjoinedUnavailable = !scheduler.TakeRawCapture();
		std::jthread stopper([&] { scheduler.Stop(); });
		{
			std::scoped_lock lock(mutex);
			finalTaskRelease = true;
		}
		condition.notify_all();
		stopper.join();
		// 上一次已封口报告在下一次默认未采样启动/停止后仍只能取一次。
		scheduler.Request(Client::Bar);
		(void)scheduler.Start();
		scheduler.Stop();
		auto report = scheduler.TakeRawCapture();
		if (!Expect(configured && finalTaskEntered && unjoinedUnavailable
			&& report && report->sealed && report->callbackSeen == 1
			&& callbacks.load() >= 1 && !scheduler.TakeRawCapture(),
			"R07 final control task must join before one-shot sealed report survives an uninstrumented restart")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		if (scheduler.ConfigureRawCapture(2))
		{
			CapturePause pause{ {}, {}, RawCaptureTestPoint::BeforeRelease };
			scheduler.SetRawCaptureTestHookForTests(&CapturePause::Pause, &pause);
			bool released = false;
			std::jthread releasing([&] { released = scheduler.ConfigureRawCapture(0); });
			const bool paused = pause.Wait(2s);
			const bool overlapRejected = !scheduler.ConfigureRawCapture(2)
				&& !scheduler.ConfigureRawCapture(0);
			pause.Resume();
			releasing.join();
			scheduler.SetRawCaptureTestHookForTests(nullptr, nullptr);
			const bool freshAccepted = scheduler.ConfigureRawCapture(2);
			if (!Expect(paused && overlapRejected && released && freshAccepted,
				"R14 Prepared release keeps reservation until candidate memory is freed")) ++failures;
		}
		else if (!Expect(false, "R14 setup requires real prepared recorder")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int barCalls = 0, settingsCalls = 0;
		const bool configured = scheduler.ConfigureRawCapture(16);
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				const auto call = ++barCalls;
				condition.notify_all();
				return call == 1 ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				const auto call = ++settingsCalls;
				condition.notify_all();
				return call == 1 ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return barCalls.load() >= 2 && settingsCalls.load() >= 2; });
		}
		std::this_thread::sleep_for(30ms);
		scheduler.Request(Client::Bar);
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return barCalls.load() >= 3; });
		}
		scheduler.Stop();
		const auto report = scheduler.TakeRawCapture();
		std::vector<RawCallbackSample> bar;
		if (report) for (const auto& sample : report->callbacks)
			if (sample.client == Client::Bar) bar.push_back(sample);
		if (!Expect(configured && report && bar.size() == 3 && report->idleTransitions >= 1
			&& bar[1].hasPreviousActiveCallback && !bar[2].hasPreviousActiveCallback
			&& bar[2].activitySegment != bar[1].activitySegment
			&& bar[2].schedulerIdleEpoch > bar[1].schedulerIdleEpoch,
			"R05 real Scheduler idle breaks active callback interval without erasing raw time")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int calls = 0;
		std::atomic_uint64_t epoch = 1;
		const bool configured = scheduler.ConfigureRawCapture(4);
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				++calls;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start([&](auto time)
			{
				FrameContext context;
				context.frameTime = time;
				context.epoch.generation = epoch.load();
				return context;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return calls.load() == 1; });
		}
		scheduler.Unregister(Client::Bar);
		epoch = 2;
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				++calls;
				condition.notify_all();
				return FrameResult::Idle;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return calls.load() == 2; });
		}
		scheduler.Stop();
		const auto report = scheduler.TakeRawCapture();
		if (!Expect(configured && report && report->callbacks.size() == 2
			&& report->callbacks[0].callbackGeneration != report->callbacks[1].callbackGeneration
			&& report->callbacks[0].contextEpoch == 1 && report->callbacks[1].contextEpoch == 2
			&& report->callbacks[0].activitySegment != report->callbacks[1].activitySegment,
			"R06 re-registration and device epoch cannot link an old callback activity chain")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler, independent;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int calls = 0, sinkAttempts = 0, otherCalls = 0;
		std::atomic_bool otherHadSample = false;
		const bool configured = scheduler.ConfigureRawCapture(8);
		(void)scheduler.SetDiagnosticsSink([&](std::string_view) -> bool
			{
				++sinkAttempts;
				throw std::runtime_error("reject raw diagnostic sink");
			});
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				const auto call = ++calls;
				if (auto* sample = CurrentFrameDiagnostics())
				{
					sample->barSampled = true;
					if (call == 1) { sample->presentFailed = true; sample->getDcResult = -9; }
					else { sample->presentAttempted = true; sample->presentCommitted = true; }
				}
				condition.notify_all();
				return call == 1 ? FrameResult::Retry : FrameResult::Idle;
			});
		(void)independent.Register(Client::Bar, [&](const auto&)
			{
				otherHadSample = CurrentFrameDiagnostics() != nullptr;
				++otherCalls;
				condition.notify_all();
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		(void)independent.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return calls.load() == 2
				&& sinkAttempts.load() >= 1 && otherCalls.load() == 1; });
		}
		scheduler.Stop();
		independent.Stop();
		const auto report = scheduler.TakeRawCapture();
		if (!Expect(configured && report && report->callbacks.size() == 2
			&& report->clients[0].seen == 2 && report->clients[0].failures == 1
			&& report->clients[0].retries == 1 && report->clients[0].commits == 1
			&& report->callbacks[0].result == FrameResult::Retry
			&& report->callbacks[1].frame.presentCommitted && sinkAttempts.load() >= 1,
			"R08 throwing anomaly sink retains actual failure, legal retry and later raw commit")) ++failures;
		if (!Expect(otherCalls.load() == 1 && !otherHadSample.load() && !independent.TakeRawCapture()
			&& CurrentFrameDiagnostics() == nullptr,
			"R09 raw capture and callback TLS stay within their owning Scheduler")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int barCalls = 0;
		const bool configured = scheduler.ConfigureRawCapture(8);
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				const auto call = ++barCalls;
				condition.notify_all();
				return call == 1 ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Register(Client::Settings, [&](const auto&)
			{
				std::this_thread::sleep_for(30ms);
				return FrameResult::Idle;
			});
		(void)scheduler.Start();
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return barCalls.load() == 2; });
		}
		scheduler.Stop();
		const auto report = scheduler.TakeRawCapture();
		const RawCallbackSample *bar0 = nullptr, *bar1 = nullptr, *settings = nullptr;
		if (report) for (const auto& sample : report->callbacks)
		{
			if (sample.client == Client::Bar) { if (!bar0) bar0 = &sample; else bar1 = &sample; }
			if (sample.client == Client::Settings) settings = &sample;
		}
		if (!Expect(configured && bar0 && bar1 && settings
			&& bar0->batchSerial == settings->batchSerial
			&& settings->startTicks >= bar0->endTicks && bar1->startTicks >= settings->endTicks
			&& settings->endTicks - settings->startTicks
				>= std::chrono::duration_cast<std::chrono::steady_clock::duration>(20ms).count(),
			"R10 slow Settings callback is attributed separately and delays the following Bar callback")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int calls = 0, recoveryCalls = 0;
		std::atomic_uint64_t epoch = 1;
		const bool configured = scheduler.ConfigureRawCapture(8);
		(void)scheduler.Register(Client::Bar, [&](const auto&)
			{
				const auto call = ++calls;
				condition.notify_all();
				return call == 1 ? FrameResult::DeviceLost : FrameResult::Idle;
			});
		(void)scheduler.Start([&](auto time)
			{
				FrameContext context;
				context.frameTime = time;
				context.epoch.generation = epoch.load();
				return context;
			}, [&]
			{
				const auto attempt = ++recoveryCalls;
				if (attempt == 2) epoch = 2;
				return attempt == 2;
			});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, 2s, [&] { return calls.load() == 2 && recoveryCalls.load() == 2; });
		}
		scheduler.Stop();
		const auto report = scheduler.TakeRawCapture();
		bool failedWithoutCallback = false, recoveredWithNewEpoch = false;
		if (report) for (const auto& batch : report->batches)
		{
			failedWithoutCallback |= batch.recoveryAttempted && !batch.recoverySucceeded
				&& !batch.contextValid && batch.executed == 0;
			recoveredWithNewEpoch |= batch.recoveryAttempted && batch.recoverySucceeded
				&& batch.contextValid && batch.contextEpoch == 2 && batch.executed == Mask(Client::Bar);
		}
		if (!Expect(configured && report && calls.load() == 2 && recoveryCalls.load() == 2
			&& failedWithoutCallback && recoveredWithNewEpoch,
			"R11 failed no-callback recovery and later new-epoch callback remain distinct batches")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		using namespace Inkeys::UI::RenderPipeline::DiagnosticsDetail;
		RawCallbackSample callback;
		callback.frameTimeTicks = 100;
		callback.startTicks = 120;
		callback.endTicks = 110;
		RawBatchSample batch;
		batch.frameTimeTicks = 100;
		batch.beginTicks = 120;
		batch.endTicks = 130;
		batch.recoveryAttempted = true;
		batch.recoveryStartTicks = 125;
		batch.recoveryEndTicks = 124;
		if (!Expect(!RawCallbackTimeValid(callback) && !RawBatchTimeValid(batch),
			"R12 production time validator preserves reversed callback and recovery evidence")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		using Inkeys::UI::Bar::BarPresentAttemptResult;
		const RECT bounds{ 0, 0, 20, 10 };
		const auto success = BarPresentAttemptResult::Acquired(S_OK, TRUE, S_OK, S_OK, bounds);
		FrameDiagnostics committed;
		committed.detailedCaptureEnabled = true;
		barTestTicks = 150;
		barTestClockReads = 0;
		ObserveBarCommit(committed, success, 17, 7, &ReadBarTestClock);
		const bool once = committed.hasBarCommitStamp && committed.barCommitTicks == 150
			&& committed.barAttemptSerial == 17 && committed.barCommitEpoch == 7
			&& committed.presentAttemptFrameSerial == 17 && barTestClockReads == 1;
		barTestTicks = 180;
		StampBarCommit(&committed, true, 18, 8, &ReadBarTestClock);
		const bool preserved = committed.barCommitTicks == 150 && committed.barAttemptSerial == 17
			&& committed.barCommitEpoch == 7 && barTestClockReads == 1;
		if (!Expect(once && preserved,
			"B01 production CompleteAttempt stamps success once before later work")) ++failures;
		const int beforeSkipped = barTestClockReads;
		bool skipped = true;
		for (const auto& attempt : std::array<BarPresentAttemptResult, 5>{
			BarPresentAttemptResult::Acquired(E_FAIL, FALSE, S_OK, S_OK, bounds),
			BarPresentAttemptResult::Acquired(S_OK, FALSE, S_OK, S_OK, bounds),
			BarPresentAttemptResult::Acquired(S_OK, TRUE, E_FAIL, S_OK, bounds),
			BarPresentAttemptResult::Acquired(S_OK, TRUE, S_OK, E_FAIL, bounds),
			BarPresentAttemptResult::CosmeticLeaseSkipped() })
		{
			FrameDiagnostics failed;
			failed.detailedCaptureEnabled = true;
			ObserveBarCommit(failed, attempt, 19, 7, &ReadBarTestClock);
			skipped &= !failed.hasBarCommitStamp && failed.barCommitTicks == 0
				&& failed.barAttemptSerial == 0 && failed.barCommitEpoch == 0;
		}
		StampBarCommit(nullptr, true, 20, 7, &ReadBarTestClock);
		if (!Expect(skipped && barTestClockReads == beforeSkipped,
			"B02 any API failure, deferred lease and null diagnostics never read the stamp clock")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		using namespace Inkeys::UI::RenderPipeline::DiagnosticsDetail;
		using Inkeys::UI::Bar::BarPresentAttemptResult;
		RawCallbackSample valid;
		valid.client = Client::Bar;
		valid.contextEpoch = 7;
		valid.frameTimeTicks = 90;
		valid.startTicks = 100;
		valid.endTicks = 200;
		valid.frame.detailedCaptureEnabled = true;
		barTestTicks = 150;
		ObserveBarCommit(valid.frame, BarPresentAttemptResult::Acquired(S_OK, TRUE, S_OK, S_OK, {}),
			5, 7, &ReadBarTestClock);
		// 独立校验器用固定数值 DTO 覆盖非法因果，不复制生产校验逻辑。
		valid.frame.hasBarCommitStamp = true;
		valid.frame.barCommitTicks = 150;
		valid.frame.barAttemptSerial = 5;
		valid.frame.barCommitEpoch = 7;
		bool classified = ClassifyBarCommitStamp(valid) == BarCommitStampStatus::Valid;
		for (int fault = 0; fault < 11; ++fault)
		{
			auto bad = valid;
			switch (fault)
			{
			case 0: bad.frame.barCommitTicks = 99; break;
			case 1: bad.frame.barCommitTicks = 201; break;
			case 2: bad.frame.barCommitEpoch = 8; break;
			case 3: bad.frame.barAttemptSerial = 6; break;
			case 4: bad.frame.presentCommitted = false; break;
			case 5: bad.frame.presentDeferred = true; break;
			case 6: bad.frame.getDcResult = E_FAIL; break;
			case 7: bad.client = Client::Settings; break;
			case 8: bad.frame.backoffSkipped = true; break;
			case 9: bad.hasPreviousTrueBarCommit = true; bad.previousTrueBarCommitTicks = 151; break;
			case 10: bad.frame.ulwAttempted = false; break;
			}
			classified &= ClassifyBarCommitStamp(bad) == BarCommitStampStatus::Invalid;
		}
		auto unknown = valid;
		unknown.frame.hasBarCommitStamp = false;
		classified &= ClassifyBarCommitStamp(unknown) == BarCommitStampStatus::Unverified;
		unknown.frame.presentCommitted = false;
		classified &= ClassifyBarCommitStamp(unknown) == BarCommitStampStatus::Absent;
		if (!Expect(classified,
			"B03 stamp validator rejects time, epoch, attempt, flags and foreign-client contradictions")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		using Inkeys::UI::Bar::BarPresentAttemptResult;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int calls = 0;
		std::atomic_uint64_t epoch = 7;
		const bool configured = scheduler.ConfigureRawCapture(16);
		auto callback = [&](const FrameContext& context)
			{
				const auto call = ++calls;
				if (auto* frame = CurrentFrameDiagnostics())
					ObserveBarCommit(*frame, BarPresentAttemptResult::Acquired(
						S_OK, call == 2 ? FALSE : TRUE, S_OK, S_OK, {}),
						static_cast<std::uint64_t>(call + 10), context.epoch.generation);
				if (call == 3) epoch = 8;
				std::this_thread::sleep_for(1ms);
				condition.notify_all();
				return call >= 5 ? FrameResult::Idle
					: call == 2 ? FrameResult::Retry : FrameResult::Continue;
			};
		const bool barRegistered = scheduler.Register(Client::Bar, callback);
		const bool started = scheduler.Start([&](auto time)
			{
				FrameContext context;
				context.frameTime = time;
				context.epoch.generation = epoch.load();
				return context;
			});
		bool reached = false;
		{
			std::unique_lock lock(mutex);
			reached = condition.wait_for(lock, 2s, [&] { return calls.load() >= 5; });
		}
		std::this_thread::sleep_for(30ms);
		scheduler.Request(Client::Bar);
		{
			std::unique_lock lock(mutex);
			reached &= condition.wait_for(lock, 2s, [&] { return calls.load() >= 6; });
		}
		scheduler.Unregister(Client::Bar);
		const bool reregistered = scheduler.Register(Client::Bar, callback);
		{
			std::unique_lock lock(mutex);
			reached &= condition.wait_for(lock, 2s, [&] { return calls.load() >= 7; });
		}
		// 任一等待失败也先停线程并 join，不能把测试失败变成悬空捕获。
		scheduler.Stop();
		const auto report = scheduler.TakeRawCapture();
		bool chain = configured && barRegistered && started && reregistered && reached
			&& report && report->callbacks.size() == 7;
		if (chain)
		{
			const auto& rows = report->callbacks;
			chain = rows[0].barCommitStatus == BarCommitStampStatus::Valid
				&& rows[0].barSuccessSerial == 1 && !rows[0].hasPreviousTrueBarCommit
				&& rows[1].barCommitStatus == BarCommitStampStatus::Absent && rows[1].barSuccessSerial == 0
				&& rows[2].barSuccessSerial == 2 && rows[2].hasPreviousTrueBarCommit
				&& rows[2].previousTrueBarCommitTicks == rows[0].frame.barCommitTicks
				&& rows[2].previousActiveCommitTicks == rows[0].endTicks
				&& rows[2].previousTrueBarCommitTicks < rows[2].previousActiveCommitTicks
				&& rows[3].barSuccessSerial == 3 && !rows[3].hasPreviousTrueBarCommit
				&& rows[3].contextEpoch == 8 && rows[3].activitySegment != rows[2].activitySegment
				&& rows[4].barSuccessSerial == 4 && rows[4].hasPreviousTrueBarCommit
				&& rows[5].barSuccessSerial == 5 && !rows[5].hasPreviousTrueBarCommit
				&& rows[5].activitySegment != rows[4].activitySegment
				&& rows[6].barSuccessSerial == 6 && !rows[6].hasPreviousTrueBarCommit
				&& rows[6].callbackGeneration != rows[5].callbackGeneration
				&& report->trueBarCommits == 6 && report->unverifiedBarCommits == 0
				&& report->invalidBarCommitStamps == 0;
			for (const auto& row : rows)
				if (row.barCommitStatus == BarCommitStampStatus::Valid)
					chain &= row.startTicks <= row.frame.barCommitTicks && row.frame.barCommitTicks < row.endTicks;
		}
		if (!Expect(chain,
			"B04 real Scheduler keeps true transaction ticks distinct from proxy and cuts idle, epoch and registration chains")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		using Inkeys::UI::Bar::BarPresentAttemptResult;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int calls = 0;
		const bool configured = scheduler.ConfigureRawCapture(2);
		(void)scheduler.Register(Client::Bar, [&](const FrameContext& context)
			{
				const auto call = ++calls;
				if (auto* frame = CurrentFrameDiagnostics())
				{
					ObserveBarCommit(*frame, BarPresentAttemptResult::Acquired(S_OK, TRUE, S_OK, S_OK, {}),
						static_cast<std::uint64_t>(call), context.epoch.generation,
						call == 3 ? &ReadInvalidBarClock : nullptr);
					if (call == 2)
					{
						frame->hasBarCommitStamp = false;
						frame->barCommitTicks = 0;
						frame->barAttemptSerial = frame->barCommitEpoch = 0;
					}
				}
				condition.notify_all();
				return call < 6 ? FrameResult::Continue : FrameResult::Idle;
			});
		(void)scheduler.Register(Client::Settings, [&](const FrameContext& context)
			{
				// 即使另一个 client 提供相同数值，也不能冒充 Bar 的事务。
				if (auto* frame = CurrentFrameDiagnostics())
					ObserveBarCommit(*frame, BarPresentAttemptResult::Acquired(S_OK, TRUE, S_OK, S_OK, {}),
						1, context.epoch.generation);
				return FrameResult::Idle;
			});
		const bool started = scheduler.Start([](auto time)
			{
				FrameContext context;
				context.frameTime = time;
				context.epoch.generation = 7;
				return context;
			});
		bool reached = false;
		{
			std::unique_lock lock(mutex);
			reached = condition.wait_for(lock, 2s, [&] { return calls.load() >= 6; });
		}
		scheduler.Stop();
		const auto report = scheduler.TakeRawCapture();
		if (!Expect(configured && started && reached && report && report->schemaVersion == 2
			&& report->callbackSeen == 7 && report->callbackRetained == 2 && report->callbackDropped == 5
			&& report->allocatedBytes == 2 * (sizeof(RawCallbackSample) + sizeof(RawBatchSample))
			&& report->trueBarCommits == 4 && report->unverifiedBarCommits == 1
			&& report->invalidBarCommitStamps == 2 && report->clients[0].trueBarCommits == 4
			&& report->clients[0].unverifiedBarCommits == 1 && report->clients[0].invalidBarCommitStamps == 1
			&& report->clients[static_cast<std::size_t>(Client::Settings)].trueBarCommits == 0
			&& report->callbacks[1].barCommitStatus == BarCommitStampStatus::Invalid
			&& !report->callbacks[1].hasPreviousTrueBarCommit && report->callbacks[1].barSuccessSerial == 0,
			"B05 bounded recorder retains valid, unknown and invalid stamp denominators after overflow")) ++failures;
	}

	{
		using namespace Inkeys::UI::RenderPipeline;
		using Inkeys::UI::Bar::BarPresentAttemptResult;
		using Inkeys::UI::Bar::BarPresentDecision;
		Scheduler scheduler;
		std::mutex mutex;
		std::condition_variable condition;
		std::atomic_int sinkOnlyCalls = 0;
		bool sinkOnlyTlsSeen = false;
		FrameDiagnostics sinkOnlyObserved;
		barTestClockReads = 0;
		const bool sinkOnlyCaptureDisabled = scheduler.ConfigureRawCapture(0);
		const bool sinkOnlyInstalled = scheduler.SetDiagnosticsSink([](std::string_view) { return true; });
		const bool sinkOnlyBarRegistered = scheduler.Register(Client::Bar, [&](const FrameContext& context)
			{
				if (auto* frame = CurrentFrameDiagnostics())
				{
					sinkOnlyTlsSeen = true;
					BarPresentDecision decision;
					decision.AddDemand({ true, false, false });
					const auto completion = decision.CompleteAttempt(
						BarPresentAttemptResult::Acquired(S_OK, TRUE, S_OK, S_OK, {}), 7, 1, 1);
					// 传测试时钟不能授权 detailed capture；保持实际完整决策来源。
					StampBarCommit(frame, completion.IsCommitted(), 1, context.epoch.generation, &ReadBarTestClock);
					frame->presentAttempted = frame->ulwAttempted = frame->ulwSucceeded = true;
					frame->presentCommitted = completion.IsCommitted();
					FrameStageTimer legacyDraw(frame, FrameStage::Draw);
					for (std::size_t stage = static_cast<std::size_t>(FrameStage::WakeAndSnapshot);
						stage < static_cast<std::size_t>(FrameStage::Count); ++stage)
					{
						FrameStageTimer detailed(frame, static_cast<FrameStage>(stage));
						// 每个新段都有确定等待，避免未门控计时偶然为0造成假通过。
						std::this_thread::sleep_for(2ms);
					}
					legacyDraw.Stop();
					sinkOnlyObserved = *frame;
				}
				++sinkOnlyCalls;
				condition.notify_all();
				return FrameResult::Idle;
			});
		const bool sinkOnlyStarted = scheduler.Start([](auto time)
			{
				FrameContext context;
				context.frameTime = time;
				context.epoch.generation = 7;
				return context;
			});
		bool sinkOnlyReached = false;
		{
			std::unique_lock lock(mutex);
			sinkOnlyReached = condition.wait_for(lock, 2s, [&] { return sinkOnlyCalls.load() == 1; });
		}
		scheduler.Stop();
		bool detailedStagesOff = true;
		for (std::size_t stage = static_cast<std::size_t>(FrameStage::WakeAndSnapshot);
			stage < static_cast<std::size_t>(FrameStage::Count); ++stage)
			detailedStagesOff &= sinkOnlyObserved.stageMs[stage] == 0.0;
		if (!Expect(sinkOnlyCaptureDisabled && sinkOnlyInstalled && sinkOnlyBarRegistered
			&& sinkOnlyStarted && sinkOnlyReached && sinkOnlyTlsSeen
			&& !sinkOnlyObserved.detailedCaptureEnabled && barTestClockReads == 0
			&& !sinkOnlyObserved.hasBarCommitStamp && sinkOnlyObserved.barCommitTicks == 0
			&& sinkOnlyObserved.barAttemptSerial == 0 && sinkOnlyObserved.barCommitEpoch == 0
			&& sinkOnlyObserved.presentAttemptFrameSerial == 0 && detailedStagesOff
			&& sinkOnlyObserved.stageMs[static_cast<std::size_t>(FrameStage::Draw)] > 0.0
			&& !scheduler.TakeRawCapture() && CurrentFrameDiagnostics() == nullptr,
			"B06 product-style anomaly sink keeps legacy TLS and stages but disables detailed clocks and stamps")) ++failures;
	}

	static_assert(static_cast<unsigned>(Inkeys::UI::RenderPipeline::FrameStage::PresentLockWait) == 0);
	static_assert(static_cast<unsigned>(Inkeys::UI::RenderPipeline::FrameStage::EndDraw) == 5);
	static_assert(static_cast<unsigned>(Inkeys::UI::RenderPipeline::FrameStage::WakeAndSnapshot) == 6);
	static_assert(static_cast<unsigned>(Inkeys::UI::RenderPipeline::FrameStage::Resources) == 12);
	static_assert(static_cast<unsigned>(Inkeys::UI::RenderPipeline::FrameStage::Count) == 13);

	{
		using namespace Inkeys::UI::Bar;
		const auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(11, baseline);
		auto accepted = baseline;
		accepted.flags = 65;
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, accepted);
		Ui3FiniteAccepted read;
		const auto counters = publication->CountersAfterOwnerStopped();
		const auto records = publication->RecordsAfterOwnerStopped();
		if (!Expect(publication->TryReadStable(read) && read.runSerial == 11 && read.stepId == 1
			&& read.revision == 1 && read.signature.flags == accepted.flags
			&& read.signature.validMask == Ui3FiniteRequiredMask && (read.publicationSerial & 1ULL) == 0
			&& counters.seen == 1 && counters.retained == 1 && counters.accepted == 1
			&& records.size() == 1 && records[0].accepted.stepId == 1,
			"B201 production finite publication atomically exposes accepted identity and fixed ledger row")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		const auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(12, baseline);
		auto old = baseline;
		old.flags = 65;
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, old);
		// caller传入的就是规范化后的实际签名；不在测试复制PresetHoming/布局算法。
		PublishFinite(*publication, 2, Ui3FiniteScene::DrawAttribute, baseline);
		Ui3FiniteAccepted read;
		if (!Expect(publication->TryReadStable(read) && read.stepId == 2 && read.revision == 2
			&& read.scene == Ui3FiniteScene::DrawAttribute && read.signature.flags == baseline.flags,
			"B202 publication seals the final post-normalization caller signature and new semantic revision")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		const auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(13, baseline);
		auto goal = baseline;
		goal.flags = 65;
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		Ui3FiniteAccepted before, after;
		const bool seeded = publication->TryReadStable(before);
		auto rejected = publication->BeginMutation(2, Ui3FiniteScene::MainFold, 2);
		publication->FinishRejected(rejected, Ui3FiniteStatus::RejectedByBusiness,
			Ui3BusinessWriteWitness::NoBusinessWrite);
		const auto records = publication->RecordsAfterOwnerStopped();
		if (!Expect(seeded && publication->StillSameSemanticGoal(before, after)
			&& after.stepId == 1 && after.revision == before.revision && after.signature.flags == goal.flags
			&& after.publicationSerial > before.publicationSerial && records.size() == 2
			&& records[1].accepted.stepId == 2 && records[1].terminalStatus == Ui3FiniteStatus::RejectedByBusiness
			&& records[0].terminalStatus != Ui3FiniteStatus::Superseded,
			"B203 NoBusinessWrite rejection closes only its request while prior pending semantic goal remains valid")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		const auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(14, baseline);
		auto goal = baseline;
		goal.flags = 65;
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto unknown = publication->BeginMutation(2, Ui3FiniteScene::MainFold, 2);
		publication->FinishRejected(unknown, Ui3FiniteStatus::RejectedByBusiness);
		Ui3FiniteAccepted read;
		const auto records = publication->RecordsAfterOwnerStopped();
		if (!Expect(!publication->TryReadStable(read) && records.size() == 2
			&& records[1].terminalStatus == Ui3FiniteStatus::AmbiguousPublication
			&& publication->CountersAfterOwnerStopped().ambiguous == 1,
			"B204 unknown or partial business exit invalidates semantic proof and retains ambiguous denominator")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		const auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(15, baseline);
		auto goal = baseline;
		goal.flags = 65;
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		PublishFinite(*publication, 2, Ui3FiniteScene::MainFold, goal);
		const auto pending = publication->RecordsAfterOwnerStopped();
		const bool pendingReferenced = pending.size() == 2 && pending[1].reusedRevision == 1
			&& pending[1].accepted.status == Ui3FiniteStatus::AcceptedNoChange
			&& (pending[1].failureFlags & Ui3FiniteReusedPending) != 0
			&& !pending[1].timingValid && pending[1].finalCommitTicks == 0;
		// 这里仅测试未来observer共用的numeric receipt；没有声称真实settled/SVG/ULW已接。
		const bool completed = publication->NoteCompletedGoal(1);
		PublishFinite(*publication, 3, Ui3FiniteScene::MainFold, goal);
		const auto records = publication->RecordsAfterOwnerStopped();
		Ui3FiniteAccepted read;
		if (!Expect(pendingReferenced && completed && records.size() == 3
			&& (records[2].failureFlags & Ui3FiniteReusedCompleted) != 0
			&& records[2].accepted.status == Ui3FiniteStatus::AcceptedNoChange
			&& records[2].reusedRevision == 1 && !records[2].timingValid && records[2].finalCommitTicks == 0
			&& publication->TryReadStable(read) && read.revision == 1 && read.stepId == 1,
			"B205 no-change references pending versus completed old goal without fabricating a new zero-ms commit")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		bool windowsValid = true;
		for (const auto point : std::array<Ui3FiniteTestPoint, 6>{
			Ui3FiniteTestPoint::AfterOddBeforeFence, Ui3FiniteTestPoint::AfterFence,
			Ui3FiniteTestPoint::AfterBusinessWrite, Ui3FiniteTestPoint::AfterPayloadHalf,
			Ui3FiniteTestPoint::BeforeEven, Ui3FiniteTestPoint::BeforeRenderRequest })
		{
			const auto baseline = FiniteSignature();
			auto publication = std::make_unique<Ui3FinitePublication>(16, baseline);
			auto old = baseline;
			old.flags = 65;
			PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, old);
			Ui3FiniteAccepted before, during, after;
			const bool seeded = publication->TryReadStable(before);
			FinitePause pause{ {}, {}, point };
			publication->SetTestHooks({ &FinitePause::Checkpoint, &pause });
			std::atomic_uint businessFlags = old.flags;
			std::jthread writer([&]
				{
					auto mutation = publication->BeginMutation(2, Ui3FiniteScene::MainFold, 2);
					publication->MarkBusinessAccepted(mutation);
					businessFlags = baseline.flags;
					publication->ObserveBusinessWrite(mutation);
					publication->FinishAtRenderRequest(mutation, baseline, 200);
				});
			const bool paused = pause.Wait();
			const bool readable = publication->TryReadStable(during);
			const bool expectedWindow = point == Ui3FiniteTestPoint::BeforeRenderRequest
				? readable && during.stepId == 2 && during.signature.flags == baseline.flags
				: !readable;
			// 超时/断言失败都必须先放行再join，不留悬空publication/回调。
			pause.Resume();
			writer.join();
			publication->SetTestHooks({});
			windowsValid &= seeded && paused && expectedWindow && businessFlags.load() == baseline.flags
				&& publication->TryReadStable(after) && after.stepId == 2
				&& after.signature.flags == baseline.flags && after.signature.penWidthBits == baseline.penWidthBits;
		}
		if (!Expect(windowsValid,
			"B206 production odd-fence-business-payload-even and pre-Request windows never expose a mixed goal")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		const auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(17, baseline);
		auto goal = baseline;
		goal.flags = 65;
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		for (std::size_t step = 2; step <= Ui3FiniteCapacity + 3; ++step)
		{
			auto mutation = publication->BeginMutation(step, Ui3FiniteScene::MainFold, step);
			publication->FinishRejected(mutation, Ui3FiniteStatus::RejectedByBusiness,
				Ui3BusinessWriteWitness::NoBusinessWrite);
		}
		const auto counters = publication->CountersAfterOwnerStopped();
		const auto records = publication->RecordsAfterOwnerStopped();
		if (!Expect(counters.seen == Ui3FiniteCapacity + 3 && counters.retained == Ui3FiniteCapacity
			&& counters.dropped == 3 && records.size() == Ui3FiniteCapacity
			&& records.front().accepted.stepId == 1 && records.back().accepted.stepId == Ui3FiniteCapacity,
			"B207 fixed 512-row production ledger retains prefix and complete dropped request denominator")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		const auto valid = FiniteSignature();
		bool validation = IsUi3FiniteSignatureValid(valid);
		for (int fault = 0; fault < 12; ++fault)
		{
			auto bad = valid;
			switch (fault)
			{
			case 0: bad.validMask &= ~2ULL; break;
			case 1: bad.penWidthBits = std::bit_cast<std::uint32_t>((std::numeric_limits<float>::quiet_NaN)()); break;
			case 2: bad.configZoomBits = std::bit_cast<std::uint64_t>((std::numeric_limits<double>::infinity)()); break;
			case 3: bad.toolRevision = 0; break;
			case 4: bad.mainSide = 2; break;
			case 5: bad.thicknessView = 3; break;
			case 6: bad.flags |= 128; break;
			case 7: bad.penColorRgb = 0x1000000; break;
			case 8: bad.stateMode = 0; break;
			case 9: bad.penMode = 99; break;
			case 10: bad.dpi = 0; break;
			case 11: bad.displaySerial = 0; break;
			}
			validation &= !IsUi3FiniteSignatureValid(bad);
		}
		auto derivedSide = valid;
		derivedSide.mainSide = derivedSide.primarySide = 1;
		validation &= SameUi3FiniteSemanticSignature(valid, derivedSide);
		if (!Expect(validation,
			"B208 production signature validates complete mask, tool version and finite values while derived side is nonsemantic")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		const auto baseline = FiniteSignature();
		bool frozenSourceRejected = true;
		for (int fault = 0; fault < 6; ++fault)
		{
			auto publication = std::make_unique<Ui3FinitePublication>(18, baseline);
			auto changed = baseline;
			changed.flags = 65;
			switch (fault)
			{
			case 0: changed.penWidthBits = std::bit_cast<std::uint32_t>(4.0f); break;
			case 1: changed.penColorRgb = 0xAABBCC; break;
			case 2: changed.toolRevision = 2; break;
			case 3: changed.displaySerial = 4; break;
			case 4: changed.dpi = 120; break;
			case 5: changed.validMask = 0; break;
			}
			PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, changed);
			Ui3FiniteAccepted read;
			const auto records = publication->RecordsAfterOwnerStopped();
			frozenSourceRejected &= !publication->TryReadStable(read) && records.size() == 1
				&& records[0].terminalStatus != Ui3FiniteStatus::Accepted
				&& records[0].terminalStatus != Ui3FiniteStatus::Pending;
		}
		if (!Expect(frozenSourceRejected,
			"B209 missing or changed frozen tool and display provenance is retained unverified rather than confirmed")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		finiteClockReads = 0;
		auto* previous = SetUi3FiniteOwnerContext(nullptr);
		{
			Ui3FiniteMutationScope absent(Ui3FiniteScene::MainFold);
			absent.MarkBusinessAccepted();
			absent.ObserveBusinessWrite();
			FinishCurrentUi3FiniteAtRenderRequest(FiniteSignature());
		}
		Ui3FiniteOwnerContext empty;
		empty.requestAvailable = true;
		empty.clocksEnabled = true;
		empty.readTicks = &ReadFiniteTestClock;
		(void)SetUi3FiniteOwnerContext(&empty);
		{
			Ui3FiniteMutationScope absent(Ui3FiniteScene::MainFold);
			FinishCurrentUi3FiniteAtRenderRequest(FiniteSignature());
		}
		const bool noEffect = CurrentUi3FiniteMutation() == nullptr && empty.requestAvailable && finiteClockReads == 0;
		(void)SetUi3FiniteOwnerContext(previous);
		if (!Expect(noEffect,
			"B210 default absent owner and null publication never read clock or consume a request")) ++failures;
	}

	static_assert(32768 * (sizeof(Inkeys::UI::RenderPipeline::RawCallbackSample)
		+ sizeof(Inkeys::UI::RenderPipeline::RawBatchSample))
		+ sizeof(Inkeys::UI::Bar::Ui3FinitePublication) <= 64ull * 1024 * 1024);

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(31, baseline);
		auto goal = baseline; goal.flags = 65;
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted;
		const bool snapshot = observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4);
		const bool consumed = observer->MarkConsumed(goal, 2, 3, 150);
		for (unsigned role = 1; role <= 6; ++role)
			observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveResources({ 1, 1, 7, 4, 1, 1, 0, 0, true });
		const auto candidate = observer->SettleCandidate(7, 200, 100, 200);
		Ui3FiniteCommitIdentity identity;
		identity.epoch = 1; identity.surfaceSerial = 7; identity.frameAttemptSerial = 4;
		identity.anchorMappingSerial = 2; identity.targetWidth = 200; identity.targetHeight = 100;
		identity.mainAnchorBits[0] = identity.drawAnchorBits[0] = std::bit_cast<std::uint64_t>(100.0);
		identity.mainAnchorBits[1] = identity.drawAnchorBits[1] = std::bit_cast<std::uint64_t>(200.0);
		identity.anchorsValid = true;
		const auto result = observer->CompleteAttempt(candidate, true, true, 250, identity);
		const auto counts = observer->CountersAfterRenderStopped();
		if (!Expect(snapshot && consumed && candidate.stablePublication && candidate.settled
			&& candidate.svgProofComplete && candidate.pendingRoles == 0
			&& candidate.rootBatchRevision == 2 && candidate.drawBatchRevision == 3
			&& result == Ui3FiniteStatus::CompletedLayoutAndSvg && counts.completed == 1,
			"B211 production observer requires consumed signature, complete roles and typed resource proof before completion")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 65;
		auto publication = std::make_unique<Ui3FinitePublication>(32, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveProperty(Ui3PropertyRole::MainRootGeometry, true, false);
		observer->ObserveResources({ 1, 1, 7, 4, 1, 1, 0, 0, true });
		const auto pending = observer->SettleCandidate(7, 200, 100);
		const auto pendingResult = observer->CompleteAttempt(pending, true, false, 0);
		observer->BeginFrame(accepted, 1, 5); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveProperty(Ui3PropertyRole::Feedback, true, false);
		observer->ObserveProperty(Ui3PropertyRole::Lighting, true, false);
		observer->ObserveResources({ 1, 1, 7, 5, 1, 1, 0, 0, true });
		const auto settled = observer->SettleCandidate(7, 200, 100);
		if (!Expect(!pending.settled && (pending.pendingRoles & Ui3FiniteRoleMask(Ui3PropertyRole::MainRootGeometry)) != 0
			&& pendingResult != Ui3FiniteStatus::CompletedLayoutAndSvg && settled.settled && settled.pendingRoles == 0,
			"B212 after-advance active or unequal property remains pending while persistent feedback and lighting do not block layout")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 65;
		auto publication = std::make_unique<Ui3FinitePublication>(33, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
		for (unsigned attempt = 4; attempt <= 5; ++attempt)
		{
			observer->BeginFrame(accepted, 1, attempt); (void)observer->MarkConsumed(goal, 2, 3, 150);
			for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
			observer->ObserveResources({ 1, 1, 7, attempt, 1, 1, 0, 0, true });
			const auto candidate = observer->SettleCandidate(7, 200, 100, 200);
			Ui3FiniteCommitIdentity identity;
			identity.epoch = 1; identity.surfaceSerial = 7; identity.frameAttemptSerial = attempt;
			identity.targetWidth = 200; identity.targetHeight = 100;
			(void)observer->CompleteAttempt(candidate, attempt == 5, true, 250, identity);
		}
		const auto beforeAbsorb = publication->RecordsAfterOwnerStopped();
		const bool renderDidNotWritePlain = beforeAbsorb.size() == 1 && beforeAbsorb[0].terminalStatus == Ui3FiniteStatus::Pending;
		observer->SealAfterRenderStopped();
		publication->AbsorbAfterOwnersStopped(observer->RecordsAfterRenderStopped());
		const auto rows = publication->RecordsAfterOwnerStopped();
		if (!Expect(renderDidNotWritePlain && observer->CountersAfterRenderStopped().completed == 1
			&& observer->CountersAfterRenderStopped().commits == 1 && rows.size() == 1
			&& rows[0].terminalStatus == Ui3FiniteStatus::CompletedLayoutAndSvg && rows[0].finalCommitTicks == 250,
			"B213 final settled failure cannot complete; later real-success input completes and plain publication ledger absorbs only after owners stop")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 65;
		auto publication = std::make_unique<Ui3FinitePublication>(34, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveResources({ 1, 1, 7, 4, 1, 1, 0, 0, true });
		const auto candidate = observer->SettleCandidate(7, 200, 100);
		auto noWrite = publication->BeginMutation(2, Ui3FiniteScene::MainFold, 2);
		publication->FinishRejected(noWrite, Ui3FiniteStatus::RejectedByBusiness, Ui3BusinessWriteWitness::NoBusinessWrite);
		Ui3FiniteCommitIdentity identity;
		identity.epoch = 1; identity.surfaceSerial = 7; identity.frameAttemptSerial = 4;
		identity.targetWidth = 200; identity.targetHeight = 100;
		const auto preserved = observer->CompleteAttempt(candidate, true, false, 0, identity);
		if (!Expect(preserved == Ui3FiniteStatus::CompletedLayoutAndSvg,
			"B214 consumed old goal may complete after pure NoBusinessWrite publication serial change")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 65;
		auto publication = std::make_unique<Ui3FinitePublication>(35, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveResources({ 1, 1, 7, 4, 1, 1, 0, 0, true });
		const auto candidate = observer->SettleCandidate(7, 200, 100);
		PublishFinite(*publication, 2, Ui3FiniteScene::MainFold, baseline);
		const auto superseded = observer->CompleteAttempt(candidate, true, false, 0);
		if (!Expect(superseded == Ui3FiniteStatus::Superseded && observer->CountersAfterRenderStopped().completed == 0,
			"B215 new semantic goal supersedes candidate; late successful transaction cannot complete the old goal")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 65;
		auto publication = std::make_unique<Ui3FinitePublication>(36, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveResources({});
		const auto candidate = observer->SettleCandidate(7, 200, 100);
		const auto result = observer->CompleteAttempt(candidate, true, false, 0);
		if (!Expect(candidate.settled && !candidate.svgProofComplete && result == Ui3FiniteStatus::ResourceUnverified
			&& observer->CountersAfterRenderStopped().completed == 0,
			"B216 absent B3 producer and zero required SVG never masquerade as CompletedLayoutAndSvg after layout commit")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 65;
		auto publication = std::make_unique<Ui3FinitePublication>(37, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		observer->NotifyRegistered(); observer->NotifyInteractionReady();
		Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveResources({ 1, 1, 7, 4, 1, 1, 0, 0, true });
		const auto candidate = observer->SettleCandidate(7, 200, 100);
		Ui3FiniteCommitIdentity identity;
		identity.epoch = 1; identity.surfaceSerial = 7; identity.frameAttemptSerial = 4;
		identity.anchorMappingSerial = 2; identity.targetWidth = 200; identity.targetHeight = 100;
		identity.mainAnchorBits[0] = identity.drawAnchorBits[0] = std::bit_cast<std::uint64_t>(100.0);
		identity.mainAnchorBits[1] = identity.drawAnchorBits[1] = std::bit_cast<std::uint64_t>(200.0);
		identity.anchorsValid = true;
		(void)observer->CompleteAttempt(candidate, true, false, 0, identity);
		Ui3FixtureReadyValue ready;
		if (!Expect(observer->TryReadReady(ready) && ready.generation == 37 && ready.committedCount == 1
			&& (ready.flags & 15u) == 15u && (ready.flags & Ui3FiniteReadyAnchors) != 0
			&& ready.lastCommittedAttempt == 4 && !ready.timingValid && ready.commitTicks == 0
			&& ready.targetWidth == 200 && ready.targetHeight == 100,
			"B217 capture-off pure commit and anchor latch becomes ready without Startup tracker or B1 timing")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(38, baseline);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		observer->NotifyRegistered(); observer->NotifyInteractionReady();
		observer->BeginFrame({}, 1, 1, true); (void)observer->MarkConsumed(baseline, 0, 0);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		const auto candidate = observer->SettleCandidate(1, 200, 100);
		Ui3FiniteCommitIdentity identity;
		identity.epoch = 1; identity.surfaceSerial = 1; identity.frameAttemptSerial = 1;
		identity.targetWidth = 200; identity.targetHeight = 100;
		(void)observer->CompleteAttempt(candidate, true, false, 0, identity);
		Ui3FixtureReadyValue ready;
		if (!Expect(observer->TryReadReady(ready) && (ready.flags & Ui3FiniteReadyLayoutStable) != 0
			&& ready.initialStableSignature.validMask == Ui3FiniteRequiredMask
			&& ready.initialStableSignature.toolRevision == 1 && observer->CountersAfterRenderStopped().goalsSeen == 0,
			"B218 initial real-layout identity latch is separate from measured accepted goals and SVG completion")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 65;
		auto publication = std::make_unique<Ui3FinitePublication>(39, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::MainFold, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 5; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveResources({ 1, 1, 7, 4, 1, 1, 0, 0, true });
		const auto missing = observer->SettleCandidate(7, 200, 100);
		observer->BeginFrame(accepted, 2, 5); (void)observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveResources({ 1, 1, 7, 5, 1, 1, 0, 0, true }); // 旧epoch proof不能借新成功复活。
		const auto changedEpoch = observer->SettleCandidate(8, 200, 100);
		if (!Expect(!missing.settled && changedEpoch.settled && !changedEpoch.svgProofComplete
			&& observer->CompleteAttempt(changedEpoch, true, false, 0) != Ui3FiniteStatus::CompletedLayoutAndSvg,
			"B219 missing role and stale epoch-surface resource proof retain unverified denominator")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature();
		auto publication = std::make_unique<Ui3FinitePublication>(40, baseline);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		for (std::size_t step = 1; step <= Ui3FiniteCapacity + 3; ++step)
		{
			auto goal = baseline; goal.flags = step % 2 ? 65 : 64;
			PublishFinite(*publication, step, Ui3FiniteScene::MainFold, goal);
			Ui3FiniteAccepted accepted; (void)observer->SnapshotAccepted(accepted);
			observer->BeginFrame(accepted, 1, step); (void)observer->MarkConsumed(goal, step, step);
			for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
			observer->ObserveResources({});
			const auto candidate = observer->SettleCandidate(1, 200, 100);
			(void)observer->CompleteAttempt(candidate, true, false, 0);
		}
		observer->SealAfterRenderStopped();
		const auto counts = observer->CountersAfterRenderStopped();
		if (!Expect(counts.goalsSeen == Ui3FiniteCapacity + 3 && counts.retained == Ui3FiniteCapacity
			&& counts.dropped == 3 && observer->RecordsAfterRenderStopped().size() == Ui3FiniteCapacity,
			"B220 bounded observer retains first 512 accepted goals and full drop denominator without expanding")) ++failures;
	}

	{
		using namespace Inkeys::UI::Bar;
		auto* prior = SetActiveUi3FiniteObserver(nullptr);
		NotifyFixtureInteractionReady();
		const bool absent = ActiveUi3FiniteObserver() == nullptr;
		(void)SetActiveUi3FiniteObserver(prior);
		if (!Expect(absent, "B221 ordinary absent private observer has no ready or completion side effect")) ++failures;
	}
	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(); auto goal = baseline; goal.flags = 66;
		auto publication = std::make_unique<Ui3FinitePublication>(40, baseline);
		PublishFinite(*publication, 1, Ui3FiniteScene::DrawAttribute, goal);
		auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
		Ui3FiniteAccepted accepted; const bool snapshot = observer->SnapshotAccepted(accepted);
		observer->BeginFrame(accepted, 1, 4); const bool consumedMoving = observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveProperty(Ui3FinitePngLayoutRole(true, true), true, false);
		observer->ObserveResources({ 1, 1, 7, 4, 1, 1, 0, 0, true });
		const auto moving = observer->SettleCandidate(7, 200, 100);
		observer->BeginFrame(accepted, 1, 5); const bool consumedStationary = observer->MarkConsumed(goal, 2, 3);
		for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
		observer->ObserveProperty(Ui3FinitePngLayoutRole(true, true), false, true);
		observer->ObserveProperty(Ui3FinitePngLayoutRole(false, true), true, false);
		observer->ObserveProperty(Ui3FinitePngLayoutRole(true, false), true, false);
		observer->ObserveResources({ 1, 1, 7, 5, 1, 1, 0, 0, true });
		const auto stationary = observer->SettleCandidate(7, 200, 100);
		if (!Expect(snapshot && consumedMoving && consumedStationary && !moving.settled && (moving.pendingRoles & Ui3FiniteRoleMask(Ui3PropertyRole::AttributePreview)) != 0
			&& stationary.settled && stationary.pendingRoles == 0,
			"B222 visible PNG color wheel geometry remains pending even when other AttributePreview objects are already settled")) ++failures;
	}
	{
		using namespace Inkeys::UI::Bar;
		auto baseline = FiniteSignature(), fitted = baseline; fitted.configZoomBits = std::bit_cast<std::uint64_t>(0.75);
		auto pub = std::make_unique<Ui3FinitePublication>(51, baseline); auto obs = std::make_unique<Ui3FiniteObserver>(*pub);
		const bool enabled = obs->EnableBootstrapBaselineBeforeOwnersStart(); const bool repeated = obs->EnableBootstrapBaselineBeforeOwnersStart(); obs->NotifyRegistered();
		auto frame = [&](const Ui3FiniteAccepted& accepted, const Ui3FiniteSignature& signature, std::uint64_t attempt,
			bool initial, bool pending, bool committed, bool anchors, bool resources, bool timed, std::int64_t ticks)
		{
			obs->BeginFrame(accepted, 1, attempt, initial); (void)obs->MarkConsumed(signature, 2, 3, timed ? 200 : 0);
			for (unsigned role = 1; role <= 6; ++role) obs->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
			if (pending) obs->ObserveLifecycle(Ui3FiniteLifecycleInitialPending);
			obs->ObserveResources({ accepted.revision, 1, 7, attempt, 1, resources ? 1u : 0u, 0, resources ? 0u : 1u, true });
			const auto candidate = obs->SettleCandidate(7, 200, 100, timed ? 250 : 0); Ui3FiniteCommitIdentity id;
			id.epoch = 1; id.surfaceSerial = 7; id.frameAttemptSerial = attempt; id.targetWidth = 200; id.targetHeight = 100; id.anchorMappingSerial = 2;
			id.mainAnchorBits[0] = id.drawAnchorBits[0] = std::bit_cast<std::uint64_t>(100.0);
			id.mainAnchorBits[1] = id.drawAnchorBits[1] = std::bit_cast<std::uint64_t>(200.0); id.anchorsValid = anchors;
			(void)obs->CompleteAttempt(candidate, committed, timed, ticks, id);
		};
		frame({}, fitted, 1, true, true, true, true, false, false, 0); Ui3FixtureReadyValue moving;
		const bool pending = obs->TryReadReady(moving) && (moving.flags & Ui3FiniteReadyLayoutStable) == 0 && pub->InitialStableSignature().configZoomBits == baseline.configZoomBits;
		frame({}, fitted, 2, true, false, false, true, false, false, 0); const bool failed = pub->InitialStableSignature().configZoomBits == baseline.configZoomBits;
		frame({}, fitted, 3, true, false, true, false, false, false, 0); const bool noAnchor = pub->InitialStableSignature().configZoomBits == baseline.configZoomBits;
		frame({}, fitted, 4, true, false, true, true, false, false, 0); Ui3FixtureReadyValue ready;
		const bool frozen = obs->TryReadReady(ready) && (ready.flags & Ui3FiniteReadyLayoutStable) != 0 && ready.initialStableSignature.configZoomBits == fitted.configZoomBits
			&& pub->InitialStableSignature().configZoomBits == fitted.configZoomBits && pub->PublicationSerial() == 0 && pub->CompletedRevision() == 0;
		if (!Expect(enabled && !repeated && pending && failed && noAnchor && frozen, "F201 bootstrap rejects old-zoom Fit transition/failed API/missing anchor and freezes actual consistent initial signature once")) ++failures;
		obs->NotifyInteractionReady(); auto goal = fitted; goal.flags = 65; PublishFinite(*pub, 1, Ui3FiniteScene::MainFold, goal); Ui3FiniteAccepted accepted;
		const bool acceptedGoal = obs->SnapshotAccepted(accepted) && accepted.signature.configZoomBits == fitted.configZoomBits && !obs->EnableBootstrapBaselineBeforeOwnersStart();
		frame(accepted, goal, 5, false, false, true, true, true, true, 300); Ui3FiniteTargetRecord firstCompleted;
		const bool copied = obs->CopyCompletedOutcomeForCurrentOwner(51, 1, 1, accepted.revision, firstCompleted);
		frame(accepted, goal, 6, false, false, true, true, false, true, 350); Ui3FiniteTargetRecord kept;
		const bool retained = obs->TryReadReady(ready) && ready.lastCommittedAttempt == 6 && ready.commitTicks == 350
			&& obs->CopyCompletedOutcomeForCurrentOwner(51, 1, 1, accepted.revision, kept) && kept.trueBarAttemptSerial == 5 && kept.finalCommitTicks == 300;
		if (!Expect(acceptedGoal && copied && firstCompleted.trueBarAttemptSerial == 5 && retained, "F202 exact completed goal retains first commit while later successful unverified-resource frame advances ready")) ++failures;
		Ui3FiniteTargetRecord bad;
		const bool tuple = !obs->CopyCompletedOutcomeForCurrentOwner(52, 1, 1, accepted.revision, bad) && !obs->CopyCompletedOutcomeForCurrentOwner(51, 2, 1, accepted.revision, bad)
			&& !obs->CopyCompletedOutcomeForCurrentOwner(51, 1, 2, accepted.revision, bad) && !obs->CopyCompletedOutcomeForCurrentOwner(51, 1, 1, accepted.revision + 1, bad);
		bool nonOwner = false; std::thread other([&] { Ui3FiniteTargetRecord value; nonOwner = !obs->CopyCompletedOutcomeForCurrentOwner(51, 1, 1, accepted.revision, value); }); other.join();
		PublishFinite(*pub, 2, Ui3FiniteScene::MainFold, goal); const bool noChange = !obs->CopyCompletedOutcomeForCurrentOwner(51, 2, 2, accepted.revision, bad); obs->SealAfterRenderStopped();
		if (!Expect(tuple && nonOwner && noChange && !obs->CopyCompletedOutcomeForCurrentOwner(51, 1, 1, accepted.revision, bad), "F203 exact completed copy rejects wrong tuple, non-owner, no-change reference and sealed state")) ++failures;
		auto ordinaryPub = std::make_unique<Ui3FinitePublication>(52, baseline); auto ordinary = std::make_unique<Ui3FiniteObserver>(*ordinaryPub); ordinary->BeginFrame({}, 1, 1, true);
		const bool unchanged = !ordinary->MarkConsumed(fitted, 0, 0) && ordinaryPub->InitialStableSignature().configZoomBits == baseline.configZoomBits; ordinary->AbortFrame(Ui3FiniteStatus::ResourceUnverified);
		auto guardedPub = std::make_unique<Ui3FinitePublication>(53, baseline); auto guarded = std::make_unique<Ui3FiniteObserver>(*guardedPub); const bool guardedEnabled = guarded->EnableBootstrapBaselineBeforeOwnersStart();
		auto foreign = baseline; foreign.penColorRgb ^= 1; guarded->BeginFrame({}, 1, 1, true); const bool refused = !guarded->MarkConsumed(foreign, 0, 0); guarded->AbortFrame(Ui3FiniteStatus::ResourceUnverified);
		if (!Expect(unchanged && !ordinary->EnableBootstrapBaselineBeforeOwnersStart() && guardedEnabled && refused && guardedPub->InitialStableSignature().penColorRgb == baseline.penColorRgb,
			"F204 ordinary constructor stays frozen; bootstrap cannot absorb foreign tool/color state")) ++failures;
	}
	{
		using namespace Inkeys::UI::Bar;
		const auto mainRows = CompiledUi3FixtureInputs(Ui3FiniteScene::MainFold), drawRows = CompiledUi3FixtureInputs(Ui3FiniteScene::DrawAttribute);
		const auto mainDescriptor = GetCompiledUi3FixtureSourceV1(Ui3FiniteScene::MainFold), drawDescriptor = GetCompiledUi3FixtureSourceV1(Ui3FiniteScene::DrawAttribute);
		bool exact = mainRows.size() == 433 && drawRows.size() == 435 && mainDescriptor.expectedSteps == 216 && drawDescriptor.expectedSteps == 216
			&& mainDescriptor.sourceHash != 0 && drawDescriptor.sourceHash != 0 && mainDescriptor.sourceHash != drawDescriptor.sourceHash;
		for (std::size_t index = 0; index < mainRows.size(); ++index) exact &= IsUi3FixtureInputValid(Ui3FiniteScene::MainFold, mainRows[index], index);
		for (std::size_t index = 0; index < drawRows.size(); ++index) exact &= IsUi3FixtureInputValid(Ui3FiniteScene::DrawAttribute, drawRows[index], index);
		const auto unknown = static_cast<Ui3FiniteScene>(99);
		if (!Expect(exact && CompiledUi3FixtureInputs(unknown).empty() && GetCompiledUi3FixtureSourceV1(unknown).sourceHash == 0,
			"F210 compiled immutable source tables and descriptors have exact planned rows and reject unknown scene")) ++failures;
		std::vector<Ui3FixtureInputV1> changed(mainRows.begin(), mainRows.end());
		changed[0].reserved0 = 1;
		const bool reserved = !IsUi3FixtureInputValid(Ui3FiniteScene::MainFold, changed[0], 0) && HashUi3FixtureInputs(Ui3FiniteScene::MainFold, changed) != mainDescriptor.sourceHash;
		changed[0] = mainRows[0]; changed[0].sourceSequence += 1;
		const bool sequence = !IsUi3FixtureInputValid(Ui3FiniteScene::MainFold, changed[0], 0) && HashUi3FixtureInputs(Ui3FiniteScene::MainFold, changed) != mainDescriptor.sourceHash;
		changed[0] = mainRows[0]; changed[0].expectedBaseCommitSerial = 1;
		const bool base = !IsUi3FixtureInputValid(Ui3FiniteScene::MainFold, changed[0], 0);
		changed[0] = mainRows[0]; changed[0].flags |= 0x80000000u;
		const bool flags = !IsUi3FixtureInputValid(Ui3FiniteScene::MainFold, changed[0], 0) && !IsUi3FixtureActionAllowed(changed[0], Ui3FiniteScene::MainFold, Ui3FixtureActionPoint::Down);
		if (!Expect(reserved && sequence && base && flags && !IsUi3FixtureInputValid(Ui3FiniteScene::MainFold, mainRows[0], mainRows.size()),
			"F211 production source validator/hash reject reserved, sequence, base, flags and out-of-range index changes")) ++failures;
		if (!Expect(IsUi3FixtureActionAllowed(mainRows[0], Ui3FiniteScene::MainFold, Ui3FixtureActionPoint::Down)
			&& !IsUi3FixtureActionAllowed(mainRows[0], Ui3FiniteScene::DrawAttribute, Ui3FixtureActionPoint::Down)
			&& !IsUi3FixtureActionAllowed(mainRows[0], Ui3FiniteScene::MainFold, Ui3FixtureActionPoint::Commit)
			&& IsUi3FixtureActionAllowed(mainRows[1], Ui3FiniteScene::MainFold, Ui3FixtureActionPoint::Commit)
			&& !IsUi3FixtureActionAllowed(mainRows.back(), Ui3FiniteScene::MainFold, Ui3FixtureActionPoint::Commit),
			"F212 source action whitelist distinguishes Down, actual Up, scene and reserved Cancel")) ++failures;
		Ui3FixtureReadyValue ready;
		ready.generation = 7; ready.committedCount = 4; ready.lastCommittedAttempt = 8; ready.epoch = 1; ready.surfaceSerial = 2; ready.anchorMappingSerial = 2;
		ready.targetWidth = 200; ready.targetHeight = 100; ready.flags = Ui3FiniteReadyRegistered | Ui3FiniteReadyInteraction | Ui3FiniteReadyTransaction | Ui3FiniteReadyLayoutStable | Ui3FiniteReadyAnchors;
		ready.initialStableSignature = FiniteSignature(); ready.mainAnchorBits[0] = std::bit_cast<std::uint64_t>(123.0); ready.mainAnchorBits[1] = std::bit_cast<std::uint64_t>(456.0);
		Ui3FixtureBoundPoint point;
		const bool bound = TryBindUi3FixturePoint(mainRows[0], ready, 7, point) && point.x == 123 && point.y == 456 && point.attempt == 8;
		const bool generation = !TryBindUi3FixturePoint(mainRows[0], ready, 8, point);
		ready.anchorMappingSerial = 3; const bool odd = !TryBindUi3FixturePoint(mainRows[0], ready, 7, point); ready.anchorMappingSerial = 2;
		ready.mainAnchorBits[0] = std::bit_cast<std::uint64_t>(40000.0); const bool range = !TryBindUi3FixturePoint(mainRows[0], ready, 7, point);
		ready.mainAnchorBits[0] = std::bit_cast<std::uint64_t>((std::numeric_limits<double>::quiet_NaN)()); const bool finite = !TryBindUi3FixturePoint(mainRows[0], ready, 7, point);
		ready.mainAnchorBits[0] = std::bit_cast<std::uint64_t>(123.0); ready.flags |= Ui3FiniteReadyStopped;
		if (!Expect(bound && generation && odd && range && finite && !TryBindUi3FixturePoint(mainRows[0], ready, 7, point),
			"F213 actual ready point binding rejects wrong generation, odd mapping, short overflow, NaN and stopped lifecycle")) ++failures;
		Ui3FixtureWireValue wire; wire.hwnd = 1; wire.message = WM_LBUTTONDOWN; wire.buttons = 1; wire.category = 2; wire.x = 12; wire.y = 34;
		auto altered = wire; altered.buttons = 0;
		if (!Expect(SameUi3FixtureWire(wire, wire) && !SameUi3FixtureWire(wire, altered), "F214 production wire fingerprint preserves actual button state")) ++failures;
	}
	static_assert(32768 * (sizeof(Inkeys::UI::RenderPipeline::RawCallbackSample)
		+ sizeof(Inkeys::UI::RenderPipeline::RawBatchSample)) + sizeof(Inkeys::UI::Bar::Ui3FinitePublication)
		+ sizeof(Inkeys::UI::Bar::Ui3FiniteObserver) <= 64ull * 1024 * 1024);

	return failures;
}
