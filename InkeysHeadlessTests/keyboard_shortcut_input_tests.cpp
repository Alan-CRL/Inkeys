#include "../Inkeys/Inkeys/Input/KeyboardShortcuts.Transport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
	namespace K = Inkeys::Input::KeyboardShortcuts;
	namespace T = K::Transport;
	T::KeyEvent Key(unsigned key, bool down, bool injected = false)
	{
		T::KeyEvent event;
		event.virtualKey = key;
		event.down = down;
		event.injected = injected;
		return event;
	}
	K::RouteSnapshot Route()
	{
		K::RouteSnapshot route;
		route.lifecycle = 3;
		route.modeRevision = 7;
		route.sceneScope = 11;
		route.contextGeneration = 13;
		route.foreground = 17;
		route.heartbeatTick = 100;
		route.annotation = true;
		route.businessReady = true;
		return route;
	}
	struct Fixture
	{
		T::Bridge bridge;
		K::RouteSnapshot route = Route();
		std::uint32_t enabled = K::AllEnabled;
		std::uint64_t config = 5, service = 9;
		Fixture()
		{
			bridge.PublishRoute(route);
			// 直接调用生产启动校准入口；无窗口测试用已验证的全 Up 值模拟权限保护后的快照。
			bridge.SeedInitialState({});
		}
		bool Send(unsigned key, bool down, bool injected = false)
		{
			return bridge.HandleKey(Key(key, down, injected), route.foreground, route.heartbeatTick,
				enabled, config, service);
		}
		void Publish() { bridge.PublishRoute(route); }
	};
}

int RunKeyboardShortcutInputTests()
{
	int failures = 0;
	auto check = [&](bool condition, const char* description)
	{
		if (!condition) { ++failures; std::cerr << "[KeyboardShortcutInput] FAIL " << description << '\n'; }
	};
	check(T::HotkeyAfterConfiguration(101, 100) && !T::HotkeyAfterConfiguration(100, 100)
		&& !T::HotkeyAfterConfiguration(99, 100) && T::HotkeyAfterConfiguration(20, 0xfffffff0U),
		"global hotkey gate rejects old and same-tick messages across configuration and tick wrap");
	{
		Fixture f;
		check(f.bridge.Synchronized(), "guarded all-Up bootstrap makes first-use functional keys ready");
		(void)f.Send(0xa2, true);
		check(f.Send('Z', true), "first CtrlZ after trusted bootstrap is owned without sacrificial keypress");
		K::Intent intent;
		check(f.bridge.TryPop(intent) && intent.command == K::Command::Undo, "first CtrlZ executes Inkeys Undo");
	}
	{
		T::Bridge bridge;
		bridge.PublishRoute(Route());
		std::array<bool, 256> held{};
		held[0xa2] = held['Z'] = true;
		bridge.SeedInitialState(held);
		check(!bridge.Synchronized(), "startup held keys remain pending until actual Up");
		check(!bridge.HandleKey(Key(0xa2, true), 17, 100, K::AllEnabled, 5, 9)
			&& !bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9),
			"guarded positive startup held keys stay Forward through repeat");
		(void)bridge.HandleKey(Key('Z', false), 17, 100, K::AllEnabled, 5, 9);
		(void)bridge.HandleKey(Key(0xa2, false), 17, 100, K::AllEnabled, 5, 9);
		check(bridge.Synchronized(), "physical Up completes held startup synchronization");
		(void)bridge.HandleKey(Key(0xa2, true), 17, 100, K::AllEnabled, 5, 9);
		check(bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9), "fresh press after held bootstrap works");
	}
	{
		T::Bridge bridge;
		bridge.PublishRoute(Route());
		(void)bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9);
		(void)bridge.HandleKey(Key(0xa2, false), 17, 100, K::AllEnabled, 5, 9);
		std::array<bool, 256> held{};
		held[0xa2] = true;
		bridge.SeedInitialState(held);
		(void)bridge.HandleKey(Key(0xa2, true), 17, 100, K::AllEnabled, 5, 9);
		check(!bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9),
			"bootstrap cannot seize Forward key already observed during sampling");
		(void)bridge.HandleKey(Key('Z', false), 17, 100, K::AllEnabled, 5, 9);
		check(bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9),
			"bootstrap cannot overwrite actual Ctrl Up observed during sampling");
	}
	for (const unsigned injectedModifier : { 0xa0U, 0xa2U, 0xa4U, 0x5bU })
	{
		Fixture f;
		(void)f.Send(0xa2, true);
		(void)f.Send(injectedModifier, true, true);
		check(!f.Send('Z', true), "injected modifier blocks new physical shortcut without synthesizing one");
		(void)f.Send(injectedModifier, false, true);
		check(!f.Send('Z', true), "injected modifier release cannot seize already Forward terminal");
		(void)f.Send('Z', false);
		check(f.Send('Z', true), "physical Ctrl state survives injected Ctrl Up and permits fresh terminal");
		K::Intent intent;
		check(f.bridge.TryPop(intent) && intent.command == K::Command::Undo, "fresh terminal after injected interleave emits physical Undo");
	}
	for (const auto command : { K::Command::Undo, K::Command::RedoY, K::Command::RedoShiftZ,
		K::Command::Clear, K::Command::Freeze })
	{
		Fixture f;
		const unsigned terminal = command == K::Command::RedoY ? 'Y' : command == K::Command::Clear ? 0x2e
			: command == K::Command::Freeze ? 'Q' : 'Z';
		check(!f.Send(0xa2, true), "control itself always forwards");
		if (command == K::Command::RedoShiftZ) check(!f.Send(0xa1, true), "right shift forwards");
		check(f.Send(terminal, true), "exact enabled combination owns terminal Down");
		K::Intent intent;
		check(f.bridge.TryPop(intent) && intent.command == command && !intent.repeat
			&& intent.sceneScope == 11 && intent.lifecycle == 3 && intent.configGeneration == 5
			&& intent.serviceGeneration == 9 && intent.pressId && intent.ctrlSessionId,
			"intent captures exact identity and selected command");
		check(f.Send(terminal, true), "owned repeat stays consumed");
		const bool repeats = command == K::Command::Undo || command == K::Command::RedoY || command == K::Command::RedoShiftZ;
		const bool hasRepeat = f.bridge.TryPop(intent);
		check(hasRepeat == repeats && (!hasRepeat || intent.repeat), "only Undo/Redo follow system repeat");
		check(f.Send(terminal, false), "owned Up stays consumed");
		check(!f.Send(terminal, false), "duplicate Up is never owned");
	}
	{
		Fixture f;
		(void)f.Send(0xa3, true);
		(void)f.Send('Z', true);
		K::Intent intent;
		check(f.bridge.TryPop(intent) && intent.command == K::Command::Undo, "right Ctrl supports Undo");
		f.route.annotation = false;
		++f.route.modeRevision;
		f.enabled = 0;
		++f.config;
		f.Publish();
		(void)f.Send(0xa3, false);
		check(f.Send('Z', true) && !f.bridge.TryPop(intent), "config/mode/Ctrl change pauses business but preserves repeat ownership");
		check(f.Send('Z', false), "owned Up remains consumed after all route changes");
	}
	{
		Fixture f;
		check(!f.Send('Z', true), "terminal before Ctrl forwards");
		(void)f.Send(0xa2, true);
		check(!f.Send('Z', true), "Forward press cannot be seized halfway through repeat");
		check(!f.Send('Z', false), "Forward Up remains forwarded");
		check(f.Send('Z', true), "new physical press can then acquire ownership");
		K::Intent intent;
		check(f.bridge.TryPop(intent), "new ownership creates exactly one intent");
		check(!f.Send('Z', false, true), "injected Up forwards");
		check(f.Send('Z', true) && f.bridge.TryPop(intent) && intent.repeat,
			"injected Up cannot clear physical ownership");
		check(f.Send('Z', false), "physical Up after injected interleave remains consumed");
		check(!f.Send('Q', true, true) && !f.bridge.TryPop(intent), "injected Down never creates a shortcut");
	}
	for (const unsigned extra : { 0xa0U, 0xa4U, 0xa5U, 0x5bU, 0x5cU })
	{
		Fixture f;
		(void)f.Send(0xa2, true);
		(void)f.Send(extra, true);
		check(!f.Send('Y', true), "additional Shift/Alt/Win prevents CtrlY matching");
		K::Intent intent;
		check(!f.bridge.TryPop(intent), "extra modifiers produce no intent");
	}
	for (const unsigned gate : { 0U, 1U, 2U, 3U })
	{
		Fixture f;
		if (gate == 0) f.route.annotation = false;
		if (gate == 1) f.route.textInput = true;
		if (gate == 2) f.route.modal = true;
		if (gate == 3) f.enabled &= ~K::Flag(K::Command::Undo);
		f.Publish();
		(void)f.Send(0xa2, true);
		check(!f.Send('Z', true) && !f.Send('Z', false), "normal input, modal, Selection or disabled combination forwards whole press");
		K::Intent intent;
		check(!f.bridge.TryPop(intent), "priority gates cannot leave a business copy");
	}
	{
		Fixture f;
		(void)f.Send(0xa2, true);
		(void)f.Send(0x2e, true);
		K::Intent first, second;
		check(f.bridge.TryPop(first), "first Delete intent committed");
		for (unsigned repeat = 0; repeat < 4; ++repeat) check(f.Send(0x2e, true), "Delete auto-repeat remains consumed");
		check(!f.bridge.TryPop(second), "Delete repeats never become second press");
		(void)f.Send(0x2e, false);
		(void)f.Send(0xa3, true);
		(void)f.Send(0xa2, false);
		(void)f.Send(0x2e, true);
		check(f.bridge.TryPop(second) && second.pressId != first.pressId
			&& second.ctrlSessionId == first.ctrlSessionId && second.gestureGeneration == first.gestureGeneration,
			"two physical Deletes share continuous Ctrl session even when changing sides");
		(void)f.Send(0xa3, false);
		const auto afterControlUp = f.bridge.GetGestureState();
		check(afterControlUp.valid && !afterControlUp.controlDown
			&& afterControlUp.gestureGeneration != second.gestureGeneration
			&& afterControlUp.interruptionGeneration == second.interruptionGeneration,
			"CtrlUp cancels candidate but is distinguishable from an interruption of formed second step");
		(void)f.Send('Q', true);
		const auto afterOtherKey = f.bridge.GetGestureState();
		check(afterOtherKey.interruptionGeneration != second.interruptionGeneration,
			"other terminal invalidates formed gesture without relying on timers");
	}
	{
		Fixture f;
		(void)f.Send(0xa2, true);
		K::Intent intent;
		check(f.bridge.HandleKey(Key('Z', true), f.route.foreground, 351, f.enabled, 5, 9)
			&& !f.bridge.TryPop(intent), "stale known annotation keeps ownership but pauses new business");
		(void)f.Send('Z', false);
		check(!f.bridge.HandleKey(Key('Z', true), 99, 100, f.enabled, 5, 9),
			"different foreground without trusted route cannot acquire ownership");
		(void)f.Send('Z', false);
		f.route.heartbeatTick = 0xfffffff0U;
		f.Publish();
		check(f.bridge.HandleKey(Key('Z', true), f.route.foreground, 20, f.enabled, 5, 9)
			&& f.bridge.TryPop(intent), "DWORD heartbeat wrap preserves valid lease");
	}
	{
		T::Bridge bridge;
		auto route = Route();
		bridge.PublishRoute(route);
		check(!bridge.HandleKey(Key(0xa2, true), 17, 100, K::AllEnabled, 5, 9), "unknown startup Ctrl Down forwards");
		check(!bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9), "unknown startup terminal is never mistaken for fresh Down");
		check(!bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9), "held-before-start repeated Z remains forwarded");
		(void)bridge.HandleKey(Key('Z', false), 17, 100, K::AllEnabled, 5, 9);
		(void)bridge.HandleKey(Key(0xa2, false), 17, 100, K::AllEnabled, 5, 9);
		(void)bridge.HandleKey(Key(0xa2, true), 17, 100, K::AllEnabled, 5, 9);
		check(bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9), "observed Up/newDown enables startup key safely");
		K::Intent intent;
		check(bridge.TryPop(intent) && !bridge.TryPop(intent), "startup held keys leave no delayed shortcut");
	}
	{
		Fixture f;
		f.route.annotation = false;
		f.route.textInput = true;
		f.Publish();
		check(!f.Send('X', true), "LL never owns global X");
		K::Intent intent;
		check(!f.bridge.TryPop(intent), "LL X never duplicates registered hotkey");
		f.bridge.HandleHotkey(17, 100, f.enabled, 5, 9);
		check(f.bridge.TryPop(intent) && intent.command == K::Command::ToggleAnnotation,
			"global X works immediately and during ordinary setting input");
		f.route.modal = true;
		f.Publish();
		f.bridge.HandleHotkey(17, 100, f.enabled, 5, 9);
		check(!f.bridge.TryPop(intent), "modal blocks hotkey business without claiming system passthrough");
	}
	{
		Fixture f;
		(void)f.Send(0xa2, true);
		(void)f.Send('Z', true);
		K::Intent intent;
		check(f.bridge.TryPop(intent), "owned press starts before foreground change");
		check(f.bridge.HandleKey(Key('Z', true), 99, 100, f.enabled, 5, 9)
			&& !f.bridge.TryPop(intent), "owned repeat after foreground change is consumed without execution");
		check(f.bridge.HandleKey(Key('Z', false), 99, 100, f.enabled, 5, 9),
			"owned Up follows press identity across foreground changes");
	}
	{
		T::Bridge bridge;
		bridge.PublishRoute(Route());
		(void)bridge.HandleKey(Key(0xa0, true), 17, 100, K::AllEnabled, 5, 9);
		(void)bridge.HandleKey(Key(0xa2, false), 17, 100, K::AllEnabled, 5, 9);
		(void)bridge.HandleKey(Key(0xa2, true), 17, 100, K::AllEnabled, 5, 9);
		(void)bridge.HandleKey(Key('Z', false), 17, 100, K::AllEnabled, 5, 9);
		check(!bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9),
			"observed unknown startup Shift prevents pretending modifiers are exactly Ctrl");
		(void)bridge.HandleKey(Key(0xa0, false), 17, 100, K::AllEnabled, 5, 9);
		check(!bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9),
			"releasing unknown modifier cannot seize an already Forward terminal");
		(void)bridge.HandleKey(Key('Z', false), 17, 100, K::AllEnabled, 5, 9);
		check(bridge.HandleKey(Key('Z', true), 17, 100, K::AllEnabled, 5, 9),
			"fresh terminal works once unknown held modifier has released");
	}
	{
		Fixture f;
		(void)f.Send(0xa2, true);
		std::vector<long long> elapsed;
		elapsed.reserve(30000);
		for (unsigned index = 0; index < 30000; ++index)
		{
			const auto before = std::chrono::steady_clock::now();
			const bool consumed = f.Send('Z', true);
			elapsed.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - before).count());
			check(consumed, "saturated owned Undo does not leak into foreground document");
		}
		K::Status status;
		f.bridge.ReadCounters(status);
		check(status.submitted == 255 && status.dropped == 30000 - 255 && status.consumed == 30000,
			"consumer pause uses exactly 255 fixed slots and accounts drops");
		K::Intent intent;
		unsigned count = 0;
		while (f.bridge.TryPop(intent)) ++count;
		check(count == 255, "failed enqueue never becomes a ghost command");
		std::sort(elapsed.begin(), elapsed.end());
		std::cout << "[KeyboardShortcutInput] paused consumer ns P50=" << elapsed[elapsed.size() / 2]
			<< " P95=" << elapsed[elapsed.size() * 95 / 100] << " P99=" << elapsed[elapsed.size() * 99 / 100]
			<< " max=" << elapsed.back() << " submitted=" << status.submitted << " dropped=" << status.dropped << '\n';
	}
	{
		T::RoutePublisher publisher;
		std::atomic_bool stop = false;
		std::thread writer([&]
		{
			for (std::uint64_t id = 1; id <= 30000; ++id)
			{
				auto route = Route();
				route.lifecycle = route.modeRevision = route.sceneScope = route.contextGeneration = route.foreground = id;
				publisher.Publish(route);
			}
			stop.store(true, std::memory_order_release);
		});
		bool coherent = true;
		do
		{
			K::RouteSnapshot route;
			if (publisher.TryRead(route)) coherent = coherent && route.lifecycle == route.modeRevision
				&& route.lifecycle == route.sceneScope && route.lifecycle == route.contextGeneration
				&& route.lifecycle == route.foreground;
		} while (!stop.load(std::memory_order_acquire));
		writer.join();
		check(coherent, "single-attempt atomic route cannot publish torn identities");
	}
	{
		T::SpscRing<unsigned, 8> ring;
		std::thread producer([&]
		{
			for (unsigned value = 0; value < 20000; ++value)
				while (!ring.TryPush(value)) std::this_thread::yield();
		});
		bool ordered = true;
		for (unsigned expected = 0; expected < 20000; ++expected)
		{
			unsigned value = 0;
			while (!ring.TryPop(value)) std::this_thread::yield();
			ordered = ordered && value == expected;
		}
		producer.join();
		check(ordered, "single producer/consumer wrap preserves FIFO under concurrent pressure");
	}
	return failures;
}
