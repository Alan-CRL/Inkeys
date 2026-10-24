#include "../Inkeys/Inkeys/Graphics/FreezeCapture.h"
#include "../Inkeys/Inkeys/UI/Freeze/Freeze.MagnifierCoordinator.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using namespace Inkeys::Graphics::FreezeCapture;
	using namespace std::chrono_literals;
	int failures = 0;
	std::atomic<std::int64_t> fakeMilliseconds = 0;
	std::atomic<unsigned> wakes = 0;

	TimePoint FakeNow() noexcept { return TimePoint{} + std::chrono::milliseconds(fakeMilliseconds.load()); }
	void Wake() noexcept { ++wakes; }
	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAIL freeze capture " << name << '\n';
	}
	Geometry TestGeometry()
	{
		return { { 0, 0, 64, 48 }, { 0, 0, 63, 47 }, { 0, 0, 64, 40 }, 9, 64, 47 };
	}

	struct Gate
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool entered = false, released = false;
		void Block()
		{
			std::unique_lock lock(mutex);
			entered = true;
			condition.notify_all();
			condition.wait(lock, [&] { return released; });
		}
		bool Wait()
		{
			std::unique_lock lock(mutex);
			return condition.wait_for(lock, 3s, [&] { return entered; });
		}
		void Release()
		{
			std::scoped_lock lock(mutex);
			released = true;
			condition.notify_all();
		}
	};

	struct FakeBackend final : Backend
	{
		std::vector<std::uint32_t> pixels;
		BackendResult beginResult{ true }, updateResult{ true }, readResult{ true };
		Gate* readGate = nullptr;
		int begins = 0, updates = 0, reads = 0, ends = 0;
		int updateDelay = 0, readDelay = 0;
		bool endAccess = false, endResult = true;
		BackendResult Begin(const Geometry& geometry) noexcept override
		{
			++begins;
			pixels.resize(static_cast<std::size_t>(geometry.width) * geometry.height);
			for (std::size_t index = 0; index < pixels.size(); ++index)
				pixels[index] = 0xFF000000 | ((index * 37) & 255) * 0x010101;
			return beginResult;
		}
		BackendResult Update(const Geometry&) noexcept override { ++updates; fakeMilliseconds += updateDelay; return updateResult; }
		BackendResult Read(const Geometry&, std::span<const std::uint32_t>& result) noexcept override
		{
			++reads;
			if (readGate) readGate->Block();
			fakeMilliseconds += readDelay;
			result = pixels;
			return readResult;
		}
		bool End(bool accessWindows) noexcept override
		{
			++ends;
			endAccess = accessWindows;
			pixels.clear();
			return endResult;
		}
	};

	void Prepare(Session& session, FakeBackend& backend)
	{
		session.Pump(backend);
		session.Pump(backend);
	}
	void Step(Session& session, FakeBackend& backend)
	{
		const auto delay = session.NextDelay();
		if (delay != std::chrono::milliseconds::max()) fakeMilliseconds += delay.count();
		session.Pump(backend);
	}

	void TestQualityAndGeometry()
	{
		auto geometry = TestGeometry();
		Check(IsValidGeometry(geometry), "original source/client geometry accepted");
		geometry.sourceRect.right = 64;
		Check(!IsValidGeometry(geometry), "does not silently replace original source width");
		geometry = TestGeometry(); geometry.width = 63;
		Check(!IsValidGeometry(geometry), "does not silently replace child client width");
		geometry = TestGeometry(); geometry.workArea.right = 65;
		Check(!IsValidGeometry(geometry), "work area outside fixed primary rejected");
		geometry = TestGeometry(); geometry.displayGeneration = 0;
		Check(!IsValidGeometry(geometry), "unknown display generation rejected");
		Check(Analyze({}, 64, 47).status == QualityStatus::InvalidBuffer, "empty buffer rejected");
		std::vector<std::uint32_t> pixels(64 * 47, 0xFF000000);
		Check(Analyze(pixels, 64, 47).status == QualityStatus::NearBlack, "black API success rejected");
		std::fill(pixels.begin(), pixels.end(), 0xFFF0F0F0);
		Check(Analyze(pixels, 64, 47).status == QualityStatus::LowInformation, "constant image retains low-information classification");
		for (std::size_t index = 0; index < pixels.size(); ++index)
			pixels[index] = 0xFF000000 | static_cast<std::uint32_t>(index % 16) * 0x010101;
		Check(Analyze(pixels, 64, 47).status == QualityStatus::Valid, "dark texture stays valid");
		Check(Analyze(pixels, 64, 48).status == QualityStatus::InvalidBuffer, "truncated geometry rejected");
	}

	void TestWarmupStaticFramesAndSkip()
	{
		fakeMilliseconds = 0;
		Session session(&Wake, &FakeNow);
		FakeBackend backend;
		std::uint64_t callbacks = 0;
		const std::uint32_t* firstBuffer = nullptr;
		auto callback = [&](const Frame& frame)
		{
			++callbacks;
			if (!firstBuffer) firstBuffer = frame.bgra.data();
			Check(frame.bgra.data() == firstBuffer, "one borrowed buffer reused");
			Check(frame.sequence == callbacks && frame.session == 1 && frame.geometry == TestGeometry(), "session sequence fixed metadata");
			Check(frame.width == 64 && frame.height == 47 && frame.strideBytes == 256
				&& frame.bgra.size() == 64 * 47 && frame.bgra.front() >> 24 == 255, "top-down BGRA shape");
			Check(frame.sourcePixelsPerPixelX == 1 && frame.sourcePixelsPerPixelY == 1,
				"identity transform stays 1:1 despite narrower source rectangle");
			Check(frame.quality.status == QualityStatus::Valid && frame.statistics.accepted == callbacks
				&& frame.timestamp == FakeNow(), "quality stats read-completion timestamp");
		};
		Check(session.Start(TestGeometry(), callback).status == StartStatus::NotReady, "start before coordinator ready rejected");
		session.SetReady(true);
		Check(session.Start(TestGeometry(), callback).status == StartStatus::Started, "ready start accepted");
		Prepare(session, backend);
		fakeMilliseconds = 799; session.Pump(backend);
		Check(backend.reads == 0, "first wait is bounded without callback dependency");
		Step(session, backend);
		Check(callbacks == 1, "first valid frame after warmup");
		for (int index = 0; index < 3; ++index) { Step(session, backend); Step(session, backend); }
		Check(callbacks == 4 && session.GetSnapshot().statistics.rejected == 0, "identical static frames remain valid");
		backend.readDelay = 170;
		Step(session, backend); Step(session, backend);
		Check(session.GetSnapshot().statistics.skipped >= 3 && session.NextDelay() == 0ms,
			"slow read counts missed periods and immediately permits the next update");
		const int readCount = backend.reads, updateCount = backend.updates;
		session.Pump(backend);
		Check(backend.reads == readCount && backend.updates == updateCount + 1 && session.NextDelay() == 10ms,
			"overrun starts one new update and preserves settling instead of catch-up reads");
		session.Pump(backend);
		Check(backend.reads == readCount && backend.updates == updateCount + 1, "no FIFO catches up before fresh settling");
		session.Stop(); session.Drain(backend);
		Check(!session.GetSnapshot().occupied && backend.ends == 1 && backend.endAccess, "normal cleanup releases original window access");
	}

	void TestStartupSurfaceWarmup()
	{
		fakeMilliseconds = 0; Session session(&Wake, &FakeNow); FakeBackend backend;
		unsigned delivered = 0; session.SetReady(true);
		(void)session.Start(TestGeometry(), [&](const Frame& frame) { ++delivered; Check(frame.sequence == 1, "first published sequence excludes warmup read"); });
		backend.readResult.warmup = true; Prepare(session, backend); Step(session, backend);
		const auto warm = session.GetSnapshot();
		Check(backend.reads == 1 && delivered == 0 && warm.statistics.warmupReads == 1 && warm.sequence == 0
			&& warm.statistics.accepted == 0 && warm.statistics.rejected == 0 && warm.statistics.apiFailures == 0
			&& !warm.lastAcceptedSample.time_since_epoch().count(), "old startup surface is neither delivered nor trusted as activity");
		backend.readResult.warmup = false; Step(session, backend); Step(session, backend);
		Check(delivered == 1 && backend.updates == 2 && backend.reads == 2 && session.GetSnapshot().statistics.accepted == 1,
			"new update/read establishes first delivered reference after priming");
		session.Stop(); session.Drain(backend);
	}

	void TestServiceBudgetAndTimings()
	{
		fakeMilliseconds = 0;
		Session session(&Wake, &FakeNow);
		FakeBackend backend;
		backend.updateDelay = 7; backend.readDelay = 20;
		backend.updateResult.timings.redraw = { 2ms, true };
		backend.readResult.timings.printWindow = { 12ms, true };
		backend.readResult.timings.gdiFlush = { 3ms, true };
		backend.readResult.timings.pixelWork = { 4ms, true };
		int callbackDelay = 5;
		Statistics delivered{};
		session.SetReady(true);
		(void)session.Start(TestGeometry(), [&](const Frame& frame)
		{
			delivered = frame.statistics;
			fakeMilliseconds += callbackDelay;
		});
		Prepare(session, backend); Step(session, backend);
		Check(session.GetSnapshot().lastReadStarted == TimePoint{} + 807ms,
			"warmup remains 800ms after the real seven millisecond update");
		Check(delivered.update.observed && delivered.update.duration == 7ms && delivered.lastReadDuration == 20ms
			&& delivered.backend.redraw.observed && delivered.backend.redraw.duration == 2ms
			&& delivered.backend.printWindow.observed && delivered.backend.printWindow.duration == 12ms
			&& delivered.backend.gdiFlush.observed && delivered.backend.gdiFlush.duration == 3ms
			&& delivered.backend.pixelWork.observed && delivered.backend.pixelWork.duration == 4ms
			&& delivered.analyze.observed && !delivered.callback.observed && delivered.callbackSequence == 0,
			"frame carries reached backend timings and analysis without inventing its unfinished callback");
		const auto first = session.GetSnapshot();
		Check(first.statistics.callback.observed && first.statistics.callback.duration == 5ms
			&& first.statistics.callbackSequence == 1 && first.statistics.skipped == 0,
			"callback timing appears after return and warmup is not missed work");
		const auto cycleStart = FakeNow() + session.NextDelay();
		Step(session, backend); Step(session, backend);
		Check(FakeNow() - cycleStart == 42ms && session.NextDelay() == 8ms,
			"seven update plus ten settle plus twenty read plus five callback share one fifty millisecond period");
		Check(delivered.callback.observed && delivered.callback.duration == 5ms && delivered.callbackSequence == 1
			&& session.GetSnapshot().statistics.callbackSequence == 2,
			"borrowed frame identifies previous completed callback instead of mislabeling current work");
		callbackDelay = 95;
		Step(session, backend); Step(session, backend);
		Check(session.NextDelay() == 0ms && session.GetSnapshot().statistics.callback.duration == 95ms
			&& session.GetSnapshot().statistics.callbackSequence == 3 && session.GetSnapshot().statistics.skipped == 2,
			"slow consumer counts in service budget and never adds another future-slot wait");
		callbackDelay = 5;
		const auto resumed = FakeNow();
		Step(session, backend); Step(session, backend);
		Check(FakeNow() - resumed == 42ms && session.NextDelay() == 8ms,
			"after overload a fresh cycle starts from actual update time without backlog");
		backend.readResult = {};
		Step(session, backend); Step(session, backend);
		const auto failed = session.GetSnapshot();
		Check(failed.statistics.update.observed && !failed.statistics.analyze.observed
			&& !failed.statistics.backend.printWindow.observed && !failed.statistics.backend.gdiFlush.observed
			&& !failed.statistics.backend.pixelWork.observed && failed.statistics.callbackSequence == 4
			&& failed.statistics.apiFailures == 1 && failed.statistics.rejected == 1,
			"unreached read stages clear old timing validity and failure does not invent a callback");
		session.Stop(); session.Drain(backend);
	}

	void TestLowInformationLiveness()
	{
		fakeMilliseconds = 0;
		Session session(&Wake, &FakeNow);
		FakeBackend backend;
		std::uint64_t callbacks = 0;
		session.SetReady(true);
		(void)session.Start(TestGeometry(), [&](const Frame& frame)
		{
			++callbacks;
			Check(frame.quality.status == QualityStatus::LowInformation && frame.sequence == callbacks
				&& frame.statistics.accepted == callbacks && frame.statistics.lowInformation == callbacks,
				"low-information frames retain quality and strictly increasing delivery identity");
		});
		Prepare(session, backend);
		std::fill(backend.pixels.begin(), backend.pixels.end(), 0xFFFFFFFF);
		Step(session, backend);
		for (int index = 0; index < 30; ++index) { Step(session, backend); Step(session, backend); }
		const auto alive = session.GetSnapshot();
		Check(alive.publishing && callbacks == 31 && alive.statistics.accepted == 31
			&& alive.statistics.lowInformation == 31 && alive.statistics.rejected == 0
			&& alive.statistics.apiFailures == 0 && alive.failureStage == FailureStage::None && !alive.failureQualityObserved,
			"continued white-page transport stays live beyond one second without claiming motion");
		fakeMilliseconds += 1000; session.Supervise();
		Check(!session.GetSnapshot().publishing && session.GetSnapshot().reason == StopReason::ResultTimeout,
			"one second without any new delivered image still revokes low-information transport");
		session.Drain(backend);
	}

	void TestInvalidResultsAndDeadlines()
	{
		fakeMilliseconds = 0;
		Session startup(&Wake, &FakeNow);
		FakeBackend black;
		startup.SetReady(true);
		Check(startup.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::Started, "black fixture starts");
		Prepare(startup, black);
		std::fill(black.pixels.begin(), black.pixels.end(), 0);
		for (int index = 0; index < 100 && startup.GetSnapshot().publishing; ++index) Step(startup, black);
		const auto failed = startup.GetSnapshot();
		Check(!failed.publishing && failed.reason == StopReason::StartupTimeout && failed.statistics.accepted == 0
			&& failed.statistics.rejected > 1 && failed.quality.status == QualityStatus::NearBlack,
			"invalid first frames never extend five second startup grace");
		startup.Drain(black);

		fakeMilliseconds = 0;
		Session tracking(&Wake, &FakeNow);
		FakeBackend low;
		tracking.SetReady(true);
		unsigned deliveries = 0;
		(void)tracking.Start(TestGeometry(), [&](const Frame&) { ++deliveries; });
		Prepare(tracking, low); Step(tracking, low);
		std::fill(low.pixels.begin(), low.pixels.end(), 0xFFF0F0F0);
		Step(tracking, low); Step(tracking, low);
		Check(tracking.GetSnapshot().publishing && tracking.GetSnapshot().statistics.rejected == 0
			&& tracking.GetSnapshot().quality.status == QualityStatus::LowInformation
			&& tracking.GetSnapshot().statistics.accepted == 2 && tracking.GetSnapshot().statistics.lowInformation == 1
			&& deliveries == 2, "low-information image is delivered for tracker-owned evidence decisions");
		std::fill(low.pixels.begin(), low.pixels.end(), 0);
		Step(tracking, low); Step(tracking, low);
		const auto blackFailure = tracking.GetSnapshot();
		Check(blackFailure.statistics.rejected == 1 && blackFailure.statistics.accepted == 2
			&& blackFailure.quality.status == QualityStatus::NearBlack && deliveries == 2
			&& blackFailure.failureQualityObserved && blackFailure.failureQuality.status == QualityStatus::NearBlack
			&& blackFailure.failureQuality.samples == blackFailure.quality.samples && blackFailure.failureQuality.blackFraction == 1,
			"near-black remains a severe rejected image after low-information delivery");
		std::fill(low.pixels.begin(), low.pixels.end(), 0xFFF0F0F0);
		Step(tracking, low); Step(tracking, low);
		const auto recoveredTransport = tracking.GetSnapshot();
		Check(recoveredTransport.statistics.accepted == 3 && recoveredTransport.statistics.lowInformation == 2
			&& recoveredTransport.statistics.rejected == 1 && recoveredTransport.failureStage == FailureStage::Quality
			&& recoveredTransport.lastFailureAt == blackFailure.lastFailureAt && deliveries == 3,
			"later low-information delivery never erases earlier severe-failure counters or timestamp");
		Check(recoveredTransport.qualityObserved && recoveredTransport.quality.status == QualityStatus::LowInformation
			&& recoveredTransport.failureQualityObserved && recoveredTransport.failureQuality.status == QualityStatus::NearBlack
			&& recoveredTransport.failureQuality.meanLuma == blackFailure.quality.meanLuma
			&& recoveredTransport.failureQuality.variance == blackFailure.quality.variance
			&& recoveredTransport.failureQuality.blackFraction == blackFailure.quality.blackFraction
			&& recoveredTransport.failureQuality.samples == blackFailure.quality.samples,
			"one snapshot after black then white keeps failure-time quality separate from latest analyzed quality");
		low.pixels.clear(); Step(tracking, low); Step(tracking, low);
		Check(tracking.GetSnapshot().statistics.rejected == 2 && tracking.GetSnapshot().quality.status == QualityStatus::InvalidBuffer
			&& tracking.GetSnapshot().lastAcceptedSample == recoveredTransport.lastAcceptedSample && deliveries == 3
			&& tracking.GetSnapshot().failureQualityObserved
			&& tracking.GetSnapshot().failureQuality.status == QualityStatus::InvalidBuffer
			&& tracking.GetSnapshot().failureQuality.samples == 0,
			"invalid buffer cannot refresh liveness or reach the consumer");
		fakeMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(recoveredTransport.lastAcceptedSample.time_since_epoch()).count() + 999;
		tracking.Supervise(); Check(tracking.GetSnapshot().publishing, "delivery timeout does not fire early");
		++fakeMilliseconds; tracking.Supervise();
		Check(!tracking.GetSnapshot().publishing && tracking.GetSnapshot().reason == StopReason::ResultTimeout
			&& tracking.GetSnapshot().failureStage == FailureStage::Timeout && !tracking.GetSnapshot().failureQualityObserved,
			"severe invalid results do not refresh one second delivery deadline");
		tracking.Drain(low);

		fakeMilliseconds = 0;
		Session readApi(&Wake, &FakeNow);
		FakeBackend failedRead;
		readApi.SetReady(true);
		(void)readApi.Start(TestGeometry(), [](const Frame&) {});
		Prepare(readApi, failedRead); Step(readApi, failedRead);
		failedRead.readResult = {};
		Step(readApi, failedRead); Step(readApi, failedRead);
		Check(readApi.GetSnapshot().statistics.apiFailures == 1 && readApi.GetSnapshot().statistics.rejected == 1
			&& readApi.GetSnapshot().statistics.accepted == 1
			&& readApi.GetSnapshot().quality.status == QualityStatus::InvalidBuffer && !readApi.GetSnapshot().failureQualityObserved,
			"read API failure immediately exposes rejection without reusing valid quality");
		failedRead.updateResult = {};
		Step(readApi, failedRead);
		Check(readApi.GetSnapshot().statistics.apiFailures == 2
			&& readApi.GetSnapshot().quality.status == QualityStatus::InvalidBuffer && !readApi.GetSnapshot().failureQualityObserved,
			"update API failure immediately exposes failure statistics");
		readApi.Stop(); readApi.Drain(failedRead);

		fakeMilliseconds = 0;
		Session api(&Wake, &FakeNow);
		FakeBackend failure;
		failure.updateResult = {};
		api.SetReady(true);
		(void)api.Start(TestGeometry(), [](const Frame&) {});
		Prepare(api, failure);
		Check(api.GetSnapshot().statistics.apiFailures == 1 && failure.reads == 0, "API failure rejected before read");
		fakeMilliseconds = 5000; api.Supervise(); api.Drain(failure);
		Check(api.GetSnapshot().reason == StopReason::StartupTimeout, "API failures cannot prolong startup");
	}

	void TestFreezeCancellationAndRestart()
	{
		fakeMilliseconds = 0;
		Session session(&Wake, &FakeNow);
		FakeBackend backend;
		session.SetReady(true);
		session.SetFreezeState(5, true, true);
		Check(session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::FreezeActive, "active Freeze rejects capture");
		session.SetFreezeState(4, false, true);
		Check(session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::FreezeActive, "late Freeze notification cannot unblock");
		session.SetFreezeState(6, false, true);
		const auto first = session.Start(TestGeometry(), [](const Frame&) {});
		Prepare(session, backend);
		session.SetFreezeState(7, true, true);
		Check(!session.GetSnapshot().publishing && session.GetSnapshot().occupied, "Freeze revokes publication before serial cleanup");
		session.SetFreezeState(8, false, true);
		Check(session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::Busy, "unexited old session rejects restart");
		session.Drain(backend);
		const auto second = session.Start(TestGeometry(), [](const Frame&) {});
		Check(second.status == StartStatus::Started && second.session == first.session + 1, "new session only after real cleanup");
		session.Stop(); session.Drain(backend);
		session.SetFreezeState(9, false, false);
		Check(session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::FreezeActive, "unavailable workspace rejects capture");

		Inkeys::UI::Freeze::MagnifierInternal::Coordinator coordinator;
		coordinator.Wake();
		Check(!coordinator.WaitNextFor(0ms), "external wake does not invent Freeze transaction");
		coordinator.Publish({ 3, true });
		Check(coordinator.WaitNextFor(0ms)->revision == 3, "pending Freeze wins a timed capture wake");
		coordinator.Stop();
		Check(!coordinator.WaitNextFor(0ms), "stop wakes timed coordinator");
	}

	void TestBlockedReadStopAndAbnormalWindowExit()
	{
		fakeMilliseconds = 0;
		Session session(&Wake, &FakeNow);
		FakeBackend backend;
		Gate gate;
		std::atomic<unsigned> delivered = 0;
		session.SetReady(true);
		(void)session.Start(TestGeometry(), [&](const Frame&) { ++delivered; });
		Prepare(session, backend);
		backend.readGate = &gate;
		fakeMilliseconds = 800;
		std::jthread worker([&] { session.Pump(backend); });
		Check(gate.Wait(), "fake blocking backend entered");
		const auto began = Clock::now();
		session.Stop();
		Check(Clock::now() - began < 100ms && session.GetSnapshot().inFlight, "Stop never waits on blocked read");
		Check(session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::Busy
			&& backend.ends == 0 && !backend.pixels.empty(), "blocked read retains buffer and ownership");
		Check(!session.DisableWindowAccess(), "abnormal window exit forbids runtime release during read");
		gate.Release(); worker.join();
		Check(delivered == 0 && session.GetSnapshot().statistics.lateResults == 1, "late read cannot publish after stop");
		session.Drain(backend);
		Check(backend.ends == 1 && !backend.endAccess && !session.GetSnapshot().occupied,
			"late cleanup releases buffer without touching destroyed HWND");
		Check(session.DisableWindowAccess() && session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::NotReady,
			"disabled resource generation cannot restart");
	}

	void TestActualSupervisorAndBorrowedCallback()
	{
		Session session(&Wake);
		FakeBackend backend;
		Gate callback;
		session.SetReady(true);
		(void)session.Start(TestGeometry(), [&](const Frame&) { callback.Block(); });
		std::jthread supervisor([&](std::stop_token stop) { session.RunSupervisor(stop); });
		Prepare(session, backend);
		std::jthread worker([&]
		{
			std::this_thread::sleep_for(session.NextDelay());
			session.Pump(backend);
		});
		Check(callback.Wait(), "borrowed callback entered on worker");
		const auto deadline = Clock::now() + 2500ms;
		while (session.GetSnapshot().publishing && Clock::now() < deadline) std::this_thread::sleep_for(5ms);
		Check(!session.GetSnapshot().publishing && session.GetSnapshot().reason == StopReason::ResultTimeout
			&& session.GetSnapshot().inFlight && session.GetSnapshot().occupied,
			"real independent one second supervision revokes a blocked consumer");
		Check(session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::Busy,
			"callback borrowing keeps old session busy after timeout");
		callback.Release(); worker.join(); session.Drain(backend);
		supervisor.request_stop(); supervisor.join();
		Check(!session.GetSnapshot().occupied && backend.ends == 1, "callback naturally returns before resource drain");
	}

	void TestSupervisorStopWake()
	{
		// 停止可早于线程入口，也可撞上 idle/活动会话的等待建立；均不能丢唤醒。
		for (const bool active : { false, true })
			for (int iteration = 0; iteration < 64; ++iteration)
			{
				Session session(&Wake);
				FakeBackend backend;
				if (active)
				{
					session.SetReady(true);
					(void)session.Start(TestGeometry(), [](const Frame&) {});
				}
				std::jthread supervisor([&](std::stop_token stop) { session.RunSupervisor(stop); });
				if (iteration % 2) std::this_thread::yield();
				const auto began = Clock::now();
				supervisor.request_stop();
				supervisor.join();
				Check(Clock::now() - began < 1s, "supervisor stop wakes idle and active waits");
				session.Stop(); session.Drain(backend);
			}
	}

	void TestFreezeWaitsForReadAndCleanup()
	{
		fakeMilliseconds = 0;
		Session session(&Wake, &FakeNow);
		FakeBackend backend;
		Gate read;
		Inkeys::UI::Freeze::MagnifierInternal::Coordinator coordinator;
		std::atomic<bool> ordinaryFreezeRan = false, releasedBeforeFreeze = false;
		std::atomic<unsigned> delivered = 0;
		session.SetReady(true);
		(void)session.Start(TestGeometry(), [&](const Frame&) { ++delivered; });
		Prepare(session, backend);
		backend.readGate = &read;
		fakeMilliseconds = 800;
		std::jthread serialWorker([&]
		{
			session.Pump(backend);
			session.Drain(backend);
			if (const auto request = coordinator.WaitNextFor(0ms); request && request->active)
			{
				releasedBeforeFreeze = !session.GetSnapshot().occupied && backend.ends == 1;
				ordinaryFreezeRan = true;
			}
		});
		Check(read.Wait(), "read occupies original serial worker before Freeze");
		session.SetFreezeState(1, true, true);
		coordinator.Publish({ 1, true });
		Check(!session.GetSnapshot().publishing && !ordinaryFreezeRan && backend.ends == 0,
			"Freeze cancels publication immediately and cannot race blocked read");
		read.Release(); serialWorker.join();
		Check(delivered == 0 && ordinaryFreezeRan && releasedBeforeFreeze,
			"ordinary Freeze only takes original control after late read and actual cleanup");
	}

	void TestTerminalFailures()
	{
		fakeMilliseconds = 0;
		Session session(&Wake, &FakeNow);
		FakeBackend backend;
		session.SetReady(true);
		Check(session.Start(TestGeometry(), {}).status == StartStatus::InvalidArgument, "empty callback rejected");
		(void)session.Start(TestGeometry(), [](const Frame&) { throw std::runtime_error("injected"); });
		Prepare(session, backend); Step(session, backend);
		Check(session.GetSnapshot().reason == StopReason::CallbackFailure && !session.GetSnapshot().inFlight,
			"consumer exception closes publication without escaping worker");
		session.Drain(backend);
		(void)session.Start(TestGeometry(), [](const Frame&) {});
		backend.updateResult = { false, StopReason::GeometryChanged };
		Prepare(session, backend);
		Check(session.GetSnapshot().reason == StopReason::GeometryChanged, "geometry change cancels instead of remapping live session");
		session.Drain(backend);
		(void)session.Start(TestGeometry(), [](const Frame&) {});
		session.Pump(backend);
		backend.endResult = false;
		session.Stop(); session.Drain(backend);
		Check(!session.GetSnapshot().ready && !session.GetSnapshot().occupied
			&& session.Start(TestGeometry(), [](const Frame&) {}).status == StartStatus::NotReady,
			"cleanup failure disables new capture after old call returns");
	}
}

int RunFreezeCaptureTests()
{
	TestQualityAndGeometry();
	TestWarmupStaticFramesAndSkip();
	TestServiceBudgetAndTimings(); TestStartupSurfaceWarmup();
	TestLowInformationLiveness();
	TestInvalidResultsAndDeadlines();
	TestFreezeCancellationAndRestart();
	TestBlockedReadStopAndAbnormalWindowExit();
	TestActualSupervisorAndBorrowedCallback();
	TestSupervisorStopWake();
	TestFreezeWaitsForReadAndCleanup();
	TestTerminalFailures();
	if (!failures) std::cout << "PASS freeze capture state quality scheduling cancellation supervision (no HWND, fake pixels)\n";
	return failures;
}
