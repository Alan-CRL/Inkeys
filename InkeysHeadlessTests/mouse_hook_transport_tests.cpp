#include "../Inkeys/Inkeys/Input/MouseHook.Transport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int RunMouseHookTransportTests()
{
	using namespace Inkeys::Input::HookTransport;
	using namespace Inkeys::Business;
	int failures = 0;
	auto check = [&](bool condition, const char* description)
	{
		if (!condition) { ++failures; std::cerr << "[MouseHookTransport] FAIL " << description << '\n'; }
	};
	Snapshot route;
	route.route = { true, true, 7 };
	route.session = { 11, 12, 13, 14, 15, 16, true, true, 0x123456789ABCDEFull };
	route.generation = 9;
	Event wheel;
	wheel.kind = Kind::Wheel;
	wheel.delta = -120;
	wheel.screen = { -90000, 80000 };
	wheel.time = 42;
	check(BypassHook(-1, true, false) && BypassHook(0, false, false)
		&& BypassHook(0, true, true) && !BypassHook(0, true, false),
		"negative code, stopped hook and ordinary move bypass before reading input");
	{
		Bridge bridge;
		bridge.Publish(route);
		check(bridge.TryCommit(wheel, 100, 100, 3) == Admission::Suppress,
			"admitted wheel suppresses only after queue commit");
		Event event;
		check(bridge.TryPop(event) && event.delta == -120 && event.screen.x == -90000
			&& event.contextRevision == 7 && event.routeGeneration == 9
			&& event.lifecycleGeneration == 3 && MatchesPptSession(event.session, route.session),
			"queue owns copied point, session, source and route identity");
		check(!bridge.TryPop(event), "one committed wheel produces one event");
		check(bridge.TryCommit(wheel, 351, 100, 3) == Admission::Forward
			&& !bridge.TryPop(event), "paused owner lease fails open without a ghost event");
		check(bridge.TryCommit(wheel, 20, 0xFFFFFFF0U, 3) == Admission::Suppress,
			"heartbeat subtraction tolerates DWORD time wrap");
		check(bridge.TryPop(event), "wrapped-clock event is committed once");
		wheel.delta = 0;
		check(bridge.TryCommit(wheel, 100, 100, 3) == Admission::Forward,
			"zero delta is neither swallowed nor queued");
		wheel.delta = -120;
		wheel.extraInfo = 0xFF515780U;
		check(bridge.TryCommit(wheel, 100, 100, 3) == Admission::Forward,
			"promoted pointer wheel preserves local source filtering");
		Event outside = wheel;
		outside.kind = Kind::OutsideCandidate;
		check(bridge.TryCommit(outside, 100, 100, 3) == Admission::Observed
			&& bridge.TryPop(event) && event.extraInfo == outside.extraInfo,
			"promoted outside click is an observed intent and remains chained");
		wheel.extraInfo = 0;
		route.route.wheelEnabled = false;
		bridge.Publish(route);
		check(bridge.TryCommit(wheel, 100, 100, 3) == Admission::Forward
			&& !bridge.TryPop(event), "disabled route never leaves a queued wheel");
		route.route.wheelEnabled = true;
	}
	{
		Bridge bridge;
		bridge.Publish(route);
		unsigned admitted = 0;
		// consumer 完全不运行：固定容量满后，后续每次尝试都立即 fail-open。
		std::vector<long long> elapsed;
		elapsed.reserve(30000);
		for (unsigned index = 0; index < 30000; ++index)
		{
			const auto before = std::chrono::steady_clock::now();
			const auto result = bridge.TryCommit(wheel, 100, 100, 1);
			elapsed.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - before).count());
			if (result == Admission::Suppress) ++admitted;
		}
		check(admitted == 255, "paused consumer admits only fixed capacity and then forwards");
		Event event;
		unsigned consumed = 0;
		while (bridge.TryPop(event)) ++consumed;
		check(consumed == admitted, "failed admission never becomes delayed execution");
		std::sort(elapsed.begin(), elapsed.end());
		std::cout << "[MouseHookTransport] paused-consumer bridge ns p50=" << elapsed[elapsed.size()/2]
			<< " p95=" << elapsed[elapsed.size()*95/100] << " p99=" << elapsed[elapsed.size()*99/100]
			<< " max=" << elapsed.back() << " forwarded=" << 30000-admitted << '\n';
	}
	{
		Bridge bridge;
		bridge.Publish(route);
		std::atomic_bool done = false;
		std::atomic_uint consumed = 0;
		std::jthread consumer([&]
		{
			Event event;
			while (!done.load(std::memory_order_acquire))
			{
				if (bridge.TryPop(event)) consumed.fetch_add(1, std::memory_order_relaxed);
				else std::this_thread::yield();
			}
			while (bridge.TryPop(event)) consumed.fetch_add(1, std::memory_order_relaxed);
		});
		unsigned admitted = 0;
		std::vector<long long> elapsed;
		elapsed.reserve(30000);
		for (unsigned index = 0; index < 30000; ++index)
		{
			const auto before = std::chrono::steady_clock::now();
			if (bridge.TryCommit(wheel, 100, 100, 1) == Admission::Suppress) ++admitted;
			elapsed.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - before).count());
		}
		done.store(true, std::memory_order_release);
		consumer.join();
		check(consumed.load() == admitted, "concurrent consumer receives each admitted event once");
		std::sort(elapsed.begin(), elapsed.end());
		std::cout << "[MouseHookTransport] concurrent-consumer bridge ns p50=" << elapsed[elapsed.size()/2]
			<< " p95=" << elapsed[elapsed.size()*95/100] << " p99=" << elapsed[elapsed.size()*99/100]
			<< " max=" << elapsed.back() << " forwarded=" << 30000-admitted << '\n';
	}
	{
		RoutePublisher publisher;
		std::atomic_bool done = false;
		std::jthread writer([&]
		{
			for (std::uint64_t revision = 1; revision <= 20000; ++revision)
			{
				Snapshot value;
				value.generation = revision;
				value.route = { true, true, revision };
				value.session = { revision, revision, revision, revision,
					static_cast<std::uintptr_t>(revision), static_cast<std::uint32_t>(revision), true, true };
				publisher.Publish(value);
			}
			done.store(true, std::memory_order_release);
		});
		unsigned torn = 0;
		do
		{
			Snapshot value;
			if (publisher.TryRead(value) && value.generation)
				if (value.generation != value.route.contextRevision
					|| value.generation != value.session.localSession
					|| value.generation != value.session.serviceGeneration
					|| value.generation != value.session.bindingRevision
					|| value.generation != value.session.showWindow
					|| value.generation != value.session.processId) ++torn;
		} while (!done.load(std::memory_order_acquire));
		writer.join();
		check(torn == 0, "fixed-attempt route read rejects concurrent torn payloads");
	}
	{
		WheelAccumulator accumulator;
		check(accumulator.Consume(1, 30) == 0 && accumulator.Consume(1, 30) == 0
			&& accumulator.Consume(1, 60) == 1, "fine wheel uses 120-unit accumulated intent");
		check(accumulator.Consume(1, -30) == 0 && accumulator.Consume(1, 30) == 0
			&& accumulator.Consume(1, -360) == -3, "signed reversal and multi-step delta preserve direction");
		check(accumulator.Consume(1, 90) == 0 && accumulator.Consume(2, 30) == 0,
			"route or session generation retires old remainder");
		accumulator.Reset();
		check(accumulator.Consume(2, 90) == 0, "explicit mode retirement clears remainder");
	}
	{
		PptEndShowGate gate;
		const auto first = route.session;
		auto second = first;
		++second.localSession;
		check(gate.TryBegin(first) && !gate.TryBegin(first), "EndShow has at most one pending/in-flight per token");
		check(gate.TryBegin(second), "new session can retire an old EndShow admission");
		gate.Complete(first);
		check(!gate.TryBegin(second), "old completion cannot release new session admission");
		gate.Complete(second);
		check(gate.TryBegin(second), "completion, failure or stale retirement permits a future request");
		gate.Reset();
		check(gate.TryBegin(second), "stop releases EndShow admission");
	}
	return failures;
}
