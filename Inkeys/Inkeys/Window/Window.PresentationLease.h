#pragma once
#include <atomic>
#include <cstdint>

namespace Inkeys::Window::Detail
{
	[[nodiscard]] inline bool TryAcquirePresentationBits(std::atomic<std::uint64_t>& active,
		std::uint64_t requested) noexcept
	{
		if (!requested) return false;
		auto previous = active.load(std::memory_order_acquire);
		return !(previous & requested) && active.compare_exchange_strong(previous, previous | requested,
			std::memory_order_acq_rel);
	}
	inline void ReleasePresentationBits(std::atomic<std::uint64_t>& active, std::uint64_t released) noexcept
	{
		active.fetch_and(~released, std::memory_order_release);
	}
}
