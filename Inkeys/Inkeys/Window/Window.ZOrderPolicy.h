#pragma once

#include <array>
#include <cstddef>
#include <limits>

namespace Inkeys::Window::Detail
{
	inline constexpr std::size_t MissingZOrder = (std::numeric_limits<std::size_t>::max)();

	template<std::size_t Count>
	struct ZOrderSnapshot
	{
		std::array<HWND, Count> windows{};
		std::array<HWND, Count> owners{};
		std::array<std::size_t, Count> ordinal{};
		std::array<bool, Count> topmost{};
		bool continuous = true;
		ZOrderSnapshot() { ordinal.fill(MissingZOrder); }
		[[nodiscard]] bool Above(std::size_t above, std::size_t below) const noexcept
		{
			return windows[above] && windows[below]
				&& ordinal[above] != MissingZOrder && ordinal[below] != MissingZOrder
				&& ordinal[above] < ordinal[below];
		}
		[[nodiscard]] bool Matches(const std::array<HWND, Count>& expectedOwners,
			bool expectedTopmost) const noexcept
		{
			if (!continuous) return false;
			for (std::size_t i = 0; i < Count; ++i)
			{
				if (!windows[i]) continue;
				if (ordinal[i] == MissingZOrder || owners[i] != expectedOwners[i]
					|| topmost[i] != expectedTopmost) return false;
				for (std::size_t owner = 0; owner < Count; ++owner)
					if (windows[owner] && windows[owner] == expectedOwners[i] && !Above(i, owner)) return false;
			}
			return true;
		}
	};
}
