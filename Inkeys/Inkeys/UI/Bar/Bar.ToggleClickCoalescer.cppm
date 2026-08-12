module;

#include <array>
#include <chrono>
#include <cstddef>

export module Inkeys.UI.Bar.ToggleClickCoalescer;

export namespace Inkeys::UI::Bar
{
	enum class BarToggleChannel : std::size_t
	{
		Main,
		DrawAttribute,
		GeometryAttribute,
		More,
		ThicknessAdjust,
		PenTypeMenu,
		Count,
	};

	class BarToggleClickCoalescer
	{
	public:
		using Clock = std::chrono::steady_clock;
		using Duration = Clock::duration;

		explicit BarToggleClickCoalescer(
			Duration mergeWindow = std::chrono::milliseconds(300)) noexcept
			: mergeWindow_(mergeWindow)
		{
		}

		bool TryBegin(
			BarToggleChannel channel,
			Clock::time_point now = Clock::now()) noexcept
		{
			const auto index = static_cast<std::size_t>(channel);
			if (index >= entries_.size()) return false;

			auto& entry = entries_[index];
			if (entry.active && now >= entry.lastToggle
				&& now - entry.lastToggle < mergeWindow_)
			{
				return false;
			}

			entry.lastToggle = now;
			entry.active = true;
			return true;
		}

	private:
		struct Entry
		{
			Clock::time_point lastToggle{};
			bool active = false;
		};

		Duration mergeWindow_;
		std::array<Entry, static_cast<std::size_t>(BarToggleChannel::Count)>
			entries_{};
	};
}
