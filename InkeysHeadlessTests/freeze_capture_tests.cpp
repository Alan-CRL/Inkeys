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
		int readDelay = 0;
		bool endAccess = false, endResult = true;
		BackendResult Begin(const Geometry& geometry) noexcept override
		{
			++begins;
			pixels.resize(static_cast<std::size_t>(geometry.width) * geometry.height);
			for (std::size_t index = 0; index < pixels.size(); ++index)
				pixels[index] = 0xFF000000 | ((index * 37) & 255) * 0x010101;
			return beginResult;
		}
		BackendResult Update(const Geometry&) noexcept override { ++updates; return updateResult; }
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
		Check(Analyze(pixels, 64, 47).status == QualityStatus::LowInformation, "constant API success rejected");
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
		Check(session.GetSnapshot().statistics.skipped >= 3 && session.NextDelay() > 0ms,
			"slow read skips absolute slots without backlog");
		const int readCount = backend.reads;
		session.Pump(backend);
		Check(backend.reads == readCount, "no FIFO catches up after slow read");
		session.Stop(); session.Drain(backend);
		Check(!session.GetSnapshot().occupied && backend.ends == 1 && backend.endAccess, "normal cleanup releases original window access");
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
		(void)tracking.Start(TestGeometry(), [](const Frame&) {});
		Prepare(tracking, low); Step(tracking, low);
		std::fill(low.pixels.begin(), low.pixels.end(), 0xFFF0F0F0);
		Step(tracking, low); Step(tracking, low);
		Check(tracking.GetSnapshot().publishing && tracking.GetSnapshot().statistics.rejected == 1
			&& tracking.GetSnapshot().quality.status == QualityStatus::LowInformation,
			"invalid tracking result immediately visible in snapshot");
		fakeMilliseconds = 1800; tracking.Supervise();
		Check(!tracking.GetSnapshot().publishing && tracking.GetSnapshot().reason == StopReason::ResultTimeout,
			"invalid results do not refresh one second valid-result deadline");
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
			&& readApi.GetSnapshot().quality.status == QualityStatus::InvalidBuffer,
			"read API failure immediately exposes rejection without reusing valid quality");
		failedRead.updateResult = {};
		Step(readApi, failedRead);
		Check(readApi.GetSnapshot().statistics.apiFailures == 2
			&& readApi.GetSnapshot().quality.status == QualityStatus::InvalidBuffer,
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
