module;

#include <atomic>
#include <cstdint>

export module Inkeys.UI.Freeze;

export namespace Inkeys::UI::Freeze
{
	struct StateSnapshot
	{
		std::uint64_t revision = 0;
		bool active = false;
		bool available = true;
	};

	using StateObserver = void(*)(StateSnapshot) noexcept;

	// 定格只由用户切换或进入工作区时改变，绘制/选择工具切换不参与状态机。
	[[nodiscard]] bool IsActive() noexcept;
	[[nodiscard]] bool IsAvailable() noexcept;
	[[nodiscard]] StateSnapshot Snapshot() noexcept;
	void SetStateObserver(StateObserver observer) noexcept;
	[[nodiscard]] bool DeactivateIfRevision(std::uint64_t revision) noexcept;
	void Toggle() noexcept;
	void SetPresentationActive(bool active) noexcept;
	void SetWhiteboardActive(bool active) noexcept;
}

namespace
{
	using Inkeys::UI::Freeze::StateObserver;
	using Inkeys::UI::Freeze::StateSnapshot;

	// 标志与版本共用一个原子字，保证迟到通知能按版本丢弃。
	constexpr std::uint64_t freezeBit = 0x01;
	constexpr std::uint64_t presentationBit = 0x02;
	constexpr std::uint64_t whiteboardBit = 0x04;
	constexpr std::uint64_t flagsMask = 0xFF;
	constexpr std::uint64_t revisionStep = 0x100;
	std::atomic<std::uint64_t> freezeState = 0;
	std::atomic<StateObserver> stateObserver = nullptr;

	[[nodiscard]] StateSnapshot DecodeState(std::uint64_t state) noexcept
	{
		return {
			state / revisionStep,
			(state & freezeBit) != 0,
			(state & (presentationBit | whiteboardBit)) == 0,
		};
	}

	[[nodiscard]] std::uint64_t NextState(
		std::uint64_t current, std::uint64_t flags) noexcept
	{
		return ((current & ~flagsMask) + revisionStep) | (flags & flagsMask);
	}

	void PublishState(std::uint64_t state) noexcept
	{
		if (const StateObserver observer = stateObserver.load(std::memory_order_acquire))
			observer(DecodeState(state));
	}

	void SetWorkspaceBit(std::uint64_t bit, bool active) noexcept
	{
		std::uint64_t current = freezeState.load(std::memory_order_acquire);
		for (;;)
		{
			const std::uint64_t currentFlags = current & flagsMask;
			const std::uint64_t desiredFlags = active ?
				(currentFlags | bit) & ~freezeBit : currentFlags & ~bit;
			if (desiredFlags == currentFlags) return;
			const std::uint64_t desired = NextState(current, desiredFlags);
			if (freezeState.compare_exchange_weak(current, desired,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				PublishState(desired);
				return;
			}
		}
	}
}

namespace Inkeys::UI::Freeze
{
	bool IsActive() noexcept
	{
		return (freezeState.load(std::memory_order_acquire) & freezeBit) != 0;
	}

	bool IsAvailable() noexcept
	{
		return (freezeState.load(std::memory_order_acquire)
			& (presentationBit | whiteboardBit)) == 0;
	}

	StateSnapshot Snapshot() noexcept
	{
		return DecodeState(freezeState.load(std::memory_order_acquire));
	}

	void SetStateObserver(StateObserver observer) noexcept
	{
		stateObserver.store(observer, std::memory_order_release);
		// 注册与状态切换并发时可能产生同版本通知；消费者按 revision 去重。
		if (observer) observer(Snapshot());
	}

	bool DeactivateIfRevision(std::uint64_t revision) noexcept
	{
		std::uint64_t current = freezeState.load(std::memory_order_acquire);
		for (;;)
		{
			const StateSnapshot snapshot = DecodeState(current);
			if (snapshot.revision != revision || !snapshot.active) return false;
			const std::uint64_t desired = NextState(current,
				(current & flagsMask) & ~freezeBit);
			if (freezeState.compare_exchange_weak(current, desired,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				PublishState(desired);
				return true;
			}
		}
	}

	void Toggle() noexcept
	{
		std::uint64_t current = freezeState.load(std::memory_order_acquire);
		for (;;)
		{
			if ((current & (presentationBit | whiteboardBit)) != 0) return;
			const std::uint64_t desired = NextState(current,
				(current & flagsMask) ^ freezeBit);
			if (freezeState.compare_exchange_weak(current, desired,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				PublishState(desired);
				return;
			}
		}
	}

	void SetPresentationActive(bool active) noexcept
	{
		SetWorkspaceBit(presentationBit, active);
	}

	void SetWhiteboardActive(bool active) noexcept
	{
		SetWorkspaceBit(whiteboardBit, active);
	}
}
