#pragma once

#include "../Business/PptSessionToken.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Inkeys::Input::HookTransport
{
	static_assert(std::atomic_uint64_t::is_always_lock_free);
	static_assert(std::atomic_uintptr_t::is_always_lock_free);
	static_assert(std::atomic_size_t::is_always_lock_free);
	struct Route
	{
		bool wheelEnabled = false;
		bool outsideClickEnabled = false;
		std::uint64_t contextRevision = 0;
	};
	enum class Kind : unsigned char { Wheel, OutsideCandidate };
	struct Event
	{
		Kind kind = Kind::OutsideCandidate;
		struct { long x = 0; long y = 0; } screen;
		std::uint32_t time = 0;
		unsigned button = 0;
		short delta = 0;
		std::uint32_t flags = 0;
		std::uintptr_t extraInfo = 0;
		std::uint64_t routeGeneration = 0;
		std::uint64_t contextRevision = 0;
		std::uint64_t lifecycleGeneration = 0;
		Business::PptSessionToken session;
	};

	// 单 producer/单 consumer；满队列直接拒绝，消费者暂停不反压系统 Hook。
	template<class T, std::size_t Capacity>
	class SpscRing
	{
		static_assert(Capacity > 1);
		std::array<T, Capacity> entries{};
		std::atomic_size_t read = 0;
		std::atomic_size_t write = 0;
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

	struct Snapshot
	{
		Route route;
		Business::PptSessionToken session;
		std::uint64_t generation = 0;
	};

	// 载荷也全部为原子，避免无锁双缓冲读写碰撞。读方只尝试一次，不重试等待写者。
	class RoutePublisher
	{
		std::atomic_uint64_t serial = 0;
		std::atomic_uint64_t context = 0, generation = 0;
		std::atomic_uint64_t localSession = 0, service = 0, show = 0, binding = 0;
		std::atomic_uintptr_t window = 0;
		std::atomic_uint32_t process = 0;
		std::atomic_uint32_t flags = 0;
	public:
		void Publish(const Snapshot& value) noexcept
		{
			serial.fetch_add(1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			context.store(value.route.contextRevision, std::memory_order_relaxed);
			generation.store(value.generation, std::memory_order_relaxed);
			localSession.store(value.session.localSession, std::memory_order_relaxed);
			service.store(value.session.serviceGeneration, std::memory_order_relaxed);
			show.store(value.session.showSessionRevision, std::memory_order_relaxed);
			binding.store(value.session.bindingRevision, std::memory_order_relaxed);
			window.store(value.session.showWindow, std::memory_order_relaxed);
			process.store(value.session.processId, std::memory_order_relaxed);
			flags.store((value.route.wheelEnabled ? 1U : 0U)
				| (value.route.outsideClickEnabled ? 2U : 0U)
				| (value.session.active ? 4U : 0U)
				| (value.session.guardedExitAvailable ? 8U : 0U), std::memory_order_relaxed);
			serial.fetch_add(1, std::memory_order_release);
		}
		[[nodiscard]] bool TryRead(Snapshot& value) const noexcept
		{
			const auto before = serial.load(std::memory_order_acquire);
			if (before & 1U) return false;
			value.route.contextRevision = context.load(std::memory_order_relaxed);
			value.generation = generation.load(std::memory_order_relaxed);
			value.session.localSession = localSession.load(std::memory_order_relaxed);
			value.session.serviceGeneration = service.load(std::memory_order_relaxed);
			value.session.showSessionRevision = show.load(std::memory_order_relaxed);
			value.session.bindingRevision = binding.load(std::memory_order_relaxed);
			value.session.showWindow = window.load(std::memory_order_relaxed);
			value.session.processId = process.load(std::memory_order_relaxed);
			const auto bits = flags.load(std::memory_order_relaxed);
			value.route.wheelEnabled = (bits & 1U) != 0;
			value.route.outsideClickEnabled = (bits & 2U) != 0;
			value.session.active = (bits & 4U) != 0;
			value.session.guardedExitAvailable = (bits & 8U) != 0;
			std::atomic_thread_fence(std::memory_order_acquire);
			return before == serial.load(std::memory_order_acquire);
		}
	};

	// 唯一 owner 累计细分滚轮；完整意图即使业务节流拒绝也不留下欠账。
	class WheelAccumulator
	{
		std::uint64_t generation = 0;
		int remainder = 0;
	public:
		void Reset() noexcept { generation = 0; remainder = 0; }
		[[nodiscard]] int Consume(std::uint64_t routeGeneration, short delta) noexcept
		{
			if (generation != routeGeneration) { generation = routeGeneration; remainder = 0; }
			const int accumulated = remainder + static_cast<int>(delta);
			const int steps = accumulated / 120;
			remainder = accumulated % 120;
			return steps;
		}
	};

	[[nodiscard]] constexpr bool BypassHook(int code, bool running, bool mouseMove) noexcept
	{
		return code < 0 || !running || mouseMove;
	}
	enum class Admission { Forward, Observed, Suppress };
	class Bridge
	{
		RoutePublisher route;
		SpscRing<Event, 256> events;
	public:
		void Publish(const Snapshot& snapshot) noexcept { route.Publish(snapshot); }
		[[nodiscard]] bool TryPop(Event& event) noexcept { return events.TryPop(event); }
		[[nodiscard]] Admission TryCommit(Event event, std::uint32_t now,
			std::uint32_t heartbeat, std::uint64_t lifecycle) noexcept
		{
			Snapshot snapshot;
			const bool wheel = event.kind == Kind::Wheel;
			if (now - heartbeat > 250U || !route.TryRead(snapshot)
				|| (wheel ? !snapshot.route.wheelEnabled : !snapshot.route.outsideClickEnabled)
				|| (wheel && (!event.delta
					|| (event.extraInfo & 0xFFFFFF00U) == 0xFF515700U))) return Admission::Forward;
			event.routeGeneration = snapshot.generation;
			event.contextRevision = snapshot.route.contextRevision;
			event.lifecycleGeneration = lifecycle;
			event.session = snapshot.session;
			// 提交前失败直接链传；提交后 wheel 一定抑制，不存在通知失败导致的幽灵副本。
			if (!events.TryPush(event)) return Admission::Forward;
			return wheel ? Admission::Suppress : Admission::Observed;
		}
	};
}
