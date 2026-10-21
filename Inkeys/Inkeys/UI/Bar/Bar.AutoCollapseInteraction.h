#pragma once

#include <cstdint>
#include "Bar.AutoCollapse.h"

namespace Inkeys::UI::Bar
{
	class DragCollapseGate
	{
	public:
		[[nodiscard]] bool Take(int policy, BarBottomDockMode mode, bool dragged) noexcept
		{
			if (triggered_ || !dragged || !ShouldAutoCollapse(policy, mode)) return false;
			triggered_ = true;
			return true;
		}
	private:
		bool triggered_ = false;
	};

	// 截止点和场景版本由交互 owner 持有；任何新输入/策略变化都取消旧候选。
	class OutsideCollapsePending
	{
	public:
		void Arm(std::uint64_t tick, std::uint64_t context) noexcept
		{
			deadline_ = tick + 100;
			context_ = context;
			pending_ = true;
		}
		void Cancel() noexcept { pending_ = false; }
		[[nodiscard]] bool Take(std::uint64_t now,
			std::uint64_t currentContext, bool enabled) noexcept
		{
			if (!pending_) return false;
			if (!enabled || context_ != currentContext) { Cancel(); return false; }
			if (now < deadline_) return false;
			Cancel();
			return true;
		}
	private:
		std::uint64_t deadline_ = 0, context_ = 0;
		bool pending_ = false;
	};
}
