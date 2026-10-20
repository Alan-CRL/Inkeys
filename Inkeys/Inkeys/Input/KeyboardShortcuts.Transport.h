#pragma once

#include "KeyboardShortcuts.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace Inkeys::Input::KeyboardShortcuts::Transport
{
	static_assert(std::atomic_uint64_t::is_always_lock_free);
	static_assert(std::atomic_size_t::is_always_lock_free);
	inline constexpr std::uint32_t OwnerLeaseMilliseconds = 250;
	inline constexpr std::array<unsigned, 13> BootstrapKeys{
		0xa0U, 0xa1U, 0xa2U, 0xa3U, 0xa4U, 0xa5U, 0x5bU, 0x5cU, 'Z', 'Y', 'Q', 0x2eU, 0x1bU };
	// 同 tick 的新消息宁可舍弃，也不能把配置切换前的全局动作重新认领。
	[[nodiscard]] constexpr bool HotkeyAfterConfiguration(std::uint32_t eventTime, std::uint32_t configuredAt) noexcept
	{ return static_cast<std::int32_t>(eventTime - configuredAt) > 0; }

	template<class T, std::size_t Capacity>
	class SpscRing
	{
		static_assert(Capacity > 1);
		std::array<T, Capacity> entries{};
		std::atomic_size_t read = 0, write = 0;
	public:
		[[nodiscard]] bool TryPush(const T& value) noexcept
		{
			const auto position = write.load(std::memory_order_relaxed);
			const auto next = (position + 1) % Capacity;
			if (next == read.load(std::memory_order_acquire)) return false;
			entries[position] = value;
			write.store(next, std::memory_order_release);
			return true;
		}
		[[nodiscard]] bool TryPop(T& value) noexcept
		{
			const auto position = read.load(std::memory_order_relaxed);
			if (position == write.load(std::memory_order_acquire)) return false;
			value = entries[position];
			read.store((position + 1) % Capacity, std::memory_order_release);
			return true;
		}
	};

	// 载荷也是原子；读方只作一次一致性尝试，绝不等待发布线程。
	class RoutePublisher
	{
		std::atomic_uint64_t serial = 0;
		std::atomic_uint64_t lifecycle = 0, mode = 0, scene = 0, context = 0, foreground = 0;
		std::atomic_uint32_t heartbeat = 0, flags = 0;
	public:
		void Publish(const RouteSnapshot& value) noexcept
		{
			serial.fetch_add(1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			lifecycle.store(value.lifecycle, std::memory_order_relaxed);
			mode.store(value.modeRevision, std::memory_order_relaxed);
			scene.store(value.sceneScope, std::memory_order_relaxed);
			context.store(value.contextGeneration, std::memory_order_relaxed);
			foreground.store(value.foreground, std::memory_order_relaxed);
			heartbeat.store(value.heartbeatTick, std::memory_order_relaxed);
			flags.store((value.annotation ? 1U : 0U) | (value.textInput ? 2U : 0U)
				| (value.modal ? 4U : 0U) | (value.businessReady ? 8U : 0U)
				| (value.whiteboardMenuInput ? 16U : 0U), std::memory_order_relaxed);
			serial.fetch_add(1, std::memory_order_release);
		}
		[[nodiscard]] bool TryRead(RouteSnapshot& value) const noexcept
		{
			const auto before = serial.load(std::memory_order_acquire);
			if (before & 1U) return false;
			value.lifecycle = lifecycle.load(std::memory_order_relaxed);
			value.modeRevision = mode.load(std::memory_order_relaxed);
			value.sceneScope = scene.load(std::memory_order_relaxed);
			value.contextGeneration = context.load(std::memory_order_relaxed);
			value.foreground = foreground.load(std::memory_order_relaxed);
			value.heartbeatTick = heartbeat.load(std::memory_order_relaxed);
			const auto bits = flags.load(std::memory_order_relaxed);
			value.annotation = (bits & 1U) != 0;
			value.textInput = (bits & 2U) != 0;
			value.modal = (bits & 4U) != 0;
			value.businessReady = (bits & 8U) != 0;
			value.whiteboardMenuInput = (bits & 16U) != 0;
			std::atomic_thread_fence(std::memory_order_acquire);
			return before == serial.load(std::memory_order_acquire);
		}
	};

	struct KeyEvent
	{
		std::uint32_t virtualKey = 0, scanCode = 0, flags = 0, time = 0;
		std::uintptr_t extraInfo = 0;
		bool down = false, injected = false, extended = false;
	};
	struct Decision
	{
		bool consume = false, submit = false;
		Intent intent;
	};

	class Matcher
	{
		struct Press
		{
			bool down = false, known = false, owned = false;
			Command command = Command::ToggleAnnotation;
			std::uint64_t id = 0, config = 0, context = 0, mode = 0, scene = 0, foreground = 0;
		};
		std::array<Press, 256> keys{};
		std::array<bool, 256> injectedModifiers{};
		std::uint64_t nextPress = 0, ctrlSession = 0, gesture = 1, interruption = 1, lastForeground = 0;
		[[nodiscard]] bool Down(unsigned key) const noexcept { return keys[key].down && keys[key].known; }
		[[nodiscard]] bool Ctrl() const noexcept { return Down(0xa2) || Down(0xa3); }
		[[nodiscard]] bool Shift() const noexcept { return Down(0xa0) || Down(0xa1); }
		[[nodiscard]] bool Alt() const noexcept { return Down(0xa4) || Down(0xa5); }
		[[nodiscard]] bool Win() const noexcept { return Down(0x5b) || Down(0x5c); }
		[[nodiscard]] static unsigned Normalize(const KeyEvent& event) noexcept
		{
			if (event.virtualKey == 0x11) return event.extended ? 0xa3 : 0xa2;
			if (event.virtualKey == 0x12) return event.extended ? 0xa5 : 0xa4;
			if (event.virtualKey == 0x10) return event.scanCode == 0x36 ? 0xa1 : 0xa0;
			return event.virtualKey;
		}
		[[nodiscard]] static bool Modifier(unsigned key) noexcept
		{
			return (key >= 0xa0 && key <= 0xa5) || key == 0x5b || key == 0x5c;
		}
		[[nodiscard]] bool Match(unsigned key, Command& command) const noexcept
		{
			// 已观察到但尚未见过 Up 的启动修饰键也必须排除，不能漏掉额外 Shift/Alt。
			for (const unsigned modifier : { 0xa0U, 0xa1U, 0xa2U, 0xa3U, 0xa4U, 0xa5U, 0x5bU, 0x5cU })
				if ((keys[modifier].down && !keys[modifier].known) || injectedModifiers[modifier]) return false;
			if (!Ctrl() || Alt() || Win()) return false;
			if (key == 'Z') { command = Shift() ? Command::RedoShiftZ : Command::Undo; return true; }
			if (Shift()) return false;
			if (key == 'Y') command = Command::RedoY;
			else if (key == 0x2e) command = Command::Clear;
			else if (key == 'Q') command = Command::Freeze;
			else return false;
			return true;
		}
		[[nodiscard]] Intent MakeIntent(Command command, const RouteSnapshot& route,
			std::uint64_t config, std::uint64_t service, std::uint64_t press, bool repeat) const noexcept
		{
			Intent intent;
			intent.command = command;
			intent.lifecycle = route.lifecycle;
			intent.modeRevision = route.modeRevision;
			intent.sceneScope = route.sceneScope;
			intent.contextGeneration = route.contextGeneration;
			intent.foreground = route.foreground;
			intent.configGeneration = config;
			intent.serviceGeneration = service;
			intent.pressId = press;
			intent.ctrlSessionId = ctrlSession;
			intent.gestureGeneration = gesture;
			intent.interruptionGeneration = interruption;
			intent.repeat = repeat;
			intent.whiteboardMenuEpoch = route.whiteboardMenuEpoch;
			return intent;
		}
	public:
		void Reset() noexcept { *this = Matcher{}; }
		void SeedInitialState(const std::array<bool, 256>& held) noexcept
		{
			// 仅受保护的启动快照可调用；不覆盖采样期间已由 Hook 观察到的任何边沿。
			for (const auto key : BootstrapKeys) if (!keys[key].known && !keys[key].down)
			{
				keys[key].down = held[key];
				keys[key].known = !held[key]; // 启动时已按住的键仍等真实 Up，不能接管其 repeat。
			}
		}
		[[nodiscard]] bool Synchronized() const noexcept
		{
			for (const auto key : BootstrapKeys) if (!keys[key].known) return false;
			return true;
		}
		[[nodiscard]] GestureState GetGestureState() const noexcept { return { ctrlSession, gesture, interruption, Ctrl(), true }; }
		[[nodiscard]] Decision Handle(const KeyEvent& event, const RouteSnapshot& route, bool trusted,
			bool fresh, std::uint32_t enabled, std::uint64_t config, std::uint64_t service) noexcept
		{
			Decision result;
			if (event.virtualKey >= keys.size()) return result;
			const auto key = Normalize(event);
			if (event.injected)
			{
				// 注入修饰键只能阻止新的精确组合，不建立物理动作、也不清理物理按压所有权。
				if (Modifier(key))
				{
					injectedModifiers[key] = event.down;
					++gesture;
					++interruption;
				}
				return result;
			}
			auto& press = keys[key];
			if (route.foreground != lastForeground) { lastForeground = route.foreground; ++gesture; ++interruption; }
			if (!event.down)
			{
				result.consume = press.down && press.owned;
				const bool wasCtrl = Ctrl();
				press = Press{};
				press.known = true;
				if (wasCtrl && !Ctrl()) ++gesture;
				return result;
			}
			if (press.down)
			{
				result.consume = press.owned;
				if (!press.owned || !fresh || !trusted || !route.businessReady || !route.annotation
					|| route.textInput || route.modal || press.config != config || press.mode != route.modeRevision
					|| press.scene != route.sceneScope || press.context != route.contextGeneration
					|| press.foreground != route.foreground || (enabled & Flag(press.command)) == 0) return result;
				Command current;
				if (!Match(key, current) || current != press.command) return result;
				result.submit = current == Command::Undo || current == Command::RedoY || current == Command::RedoShiftZ;
				result.intent = MakeIntent(current, route, config, service, press.id, true);
				return result;
			}
			const bool wasKnown = press.known;
			const bool wasCtrl = Ctrl();
			press.down = true;
			press.id = ++nextPress;
			if (Modifier(key))
			{
				if (!wasCtrl && Ctrl()) ++ctrlSession;
				if (key != 0xa2 && key != 0xa3) { ++gesture; ++interruption; }
				return result;
			}
			// Delete 本身不退休同 Ctrl 会话的首击，其它真实终键会退休候选。
			if (key != 0x2e) { ++gesture; ++interruption; }
			// Escape 只认领本次新按压；菜单关闭后，已 Owned 的 repeat/Up 仍由上方分支消费。
			if (key == 0x1b)
			{
				if (!wasKnown || !trusted || !fresh || !route.whiteboardMenuInput
					|| !route.whiteboardMenuEpoch || route.textInput) return result;
				for (const unsigned modifier : { 0xa0U, 0xa1U, 0xa2U, 0xa3U, 0xa4U, 0xa5U, 0x5bU, 0x5cU })
					if (keys[modifier].down || injectedModifiers[modifier]) return result;
				press.owned = true; press.command = Command::DismissWhiteboardMenu;
				result.consume = result.submit = true;
				result.intent = MakeIntent(press.command, route, config, service, press.id, false);
				return result;
			}
			Command command;
			if (!wasKnown || !trusted || !route.annotation || route.textInput || route.modal
				|| !Match(key, command) || (enabled & Flag(command)) == 0) return result;
			press.owned = true;
			press.command = command;
			press.config = config;
			press.context = route.contextGeneration;
			press.mode = route.modeRevision;
			press.scene = route.sceneScope;
			press.foreground = route.foreground;
			result.consume = true;
			result.submit = fresh && route.businessReady;
			result.intent = MakeIntent(command, route, config, service, press.id, false);
			return result;
		}
		[[nodiscard]] Decision Hotkey(const RouteSnapshot& route, bool trusted, bool fresh,
			std::uint32_t enabled, std::uint64_t config, std::uint64_t service) noexcept
		{
			Decision result;
			if (!trusted || !fresh || route.modal || !route.businessReady
				|| (enabled & Flag(Command::ToggleAnnotation)) == 0) return result;
			result.submit = true;
			result.intent = MakeIntent(Command::ToggleAnnotation, route, config, service, ++nextPress, false);
			return result;
		}
	};

	class Bridge
	{
		RoutePublisher routes;
		SpscRing<Intent, 256> intents;
		Matcher matcher;
		RouteSnapshot lastRoute;
		std::atomic_uint64_t whiteboardMenuEpoch = 0;
		std::atomic_uint64_t gestureSerial = 0, gestureSession = 0, gestureRevision = 0, interruptionRevision = 0;
		std::atomic_bool controlDown = false, synchronized = false;
		std::atomic_uint64_t submitted = 0, dropped = 0, consumed = 0, paused = 0;
		void PublishGesture() noexcept
		{
			const auto state = matcher.GetGestureState();
			synchronized.store(matcher.Synchronized(), std::memory_order_release);
			gestureSerial.fetch_add(1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			gestureSession.store(state.ctrlSessionId, std::memory_order_relaxed);
			controlDown.store(state.controlDown, std::memory_order_relaxed);
			gestureRevision.store(state.gestureGeneration, std::memory_order_relaxed);
			interruptionRevision.store(state.interruptionGeneration, std::memory_order_relaxed);
			gestureSerial.fetch_add(1, std::memory_order_release);
		}
		[[nodiscard]] bool ReadRoute(RouteSnapshot& route, std::uint64_t foreground,
			std::uint32_t now, bool& fresh) noexcept
		{
			RouteSnapshot next;
			if (routes.TryRead(next) && next.lifecycle && next.foreground == foreground)
			{
				route = next;
				lastRoute = next;
				fresh = now - route.heartbeatTick <= OwnerLeaseMilliseconds;
				return true;
			}
			// 发布碰撞时仅延续同前台最后可信归属，不提交新业务。
			route = lastRoute;
			fresh = false;
			return route.lifecycle && route.foreground == foreground;
		}
		void Submit(const Decision& decision) noexcept
		{
			if (decision.consume) consumed.fetch_add(1, std::memory_order_relaxed);
			if (!decision.submit) { if (decision.consume) paused.fetch_add(1, std::memory_order_relaxed); return; }
			if (intents.TryPush(decision.intent)) submitted.fetch_add(1, std::memory_order_relaxed);
			else dropped.fetch_add(1, std::memory_order_relaxed);
		}
	public:
		void ResetProducer() noexcept
		{
			matcher.Reset();
			lastRoute = {};
			PublishGesture();
		}
		void SeedInitialState(const std::array<bool, 256>& held) noexcept
		{
			matcher.SeedInitialState(held);
			PublishGesture();
		}
		[[nodiscard]] bool Synchronized() const noexcept { return synchronized.load(std::memory_order_acquire); }
		void PublishRoute(const RouteSnapshot& route) noexcept { routes.Publish(route); }
		void PublishWhiteboardMenuEpoch(std::uint64_t epoch) noexcept
		{ whiteboardMenuEpoch.store(epoch, std::memory_order_release); }
		[[nodiscard]] bool TryPop(Intent& intent) noexcept { return intents.TryPop(intent); }
		[[nodiscard]] bool HandleKey(const KeyEvent& event, std::uint64_t foreground, std::uint32_t now,
			std::uint32_t enabled, std::uint64_t config, std::uint64_t service) noexcept
		{
			RouteSnapshot route;
			bool fresh = false;
			const bool trusted = ReadRoute(route, foreground, now, fresh);
			if (!trusted) route.foreground = foreground;
			route.whiteboardMenuEpoch = whiteboardMenuEpoch.load(std::memory_order_acquire);
			const auto decision = matcher.Handle(event, route, trusted, fresh, enabled, config, service);
			PublishGesture();
			Submit(decision);
			return decision.consume;
		}
		void HandleHotkey(std::uint64_t foreground, std::uint32_t now, std::uint32_t enabled,
			std::uint64_t config, std::uint64_t service) noexcept
		{
			RouteSnapshot route;
			bool fresh = false;
			const bool trusted = ReadRoute(route, foreground, now, fresh);
			Submit(matcher.Hotkey(route, trusted, fresh, enabled, config, service));
		}
		[[nodiscard]] GestureState GetGestureState() const noexcept
		{
			GestureState state;
			const auto before = gestureSerial.load(std::memory_order_acquire);
			if (before & 1U) return state;
			state.gestureGeneration = gestureRevision.load(std::memory_order_relaxed);
			state.ctrlSessionId = gestureSession.load(std::memory_order_relaxed);
			state.interruptionGeneration = interruptionRevision.load(std::memory_order_relaxed);
			state.controlDown = controlDown.load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			state.valid = before == gestureSerial.load(std::memory_order_acquire);
			return state;
		}
		void ReadCounters(Status& status) const noexcept
		{
			status.submitted = submitted.load(std::memory_order_relaxed);
			status.dropped = dropped.load(std::memory_order_relaxed);
			status.consumed = consumed.load(std::memory_order_relaxed);
			status.paused = paused.load(std::memory_order_relaxed);
		}
	};
}
