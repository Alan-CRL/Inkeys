module;

#include <cstdint>

// 主栏自动收起策略及交互候选状态。
export module Inkeys.UI.Bar.AutoCollapse;

export import Inkeys.UI.Bar.BottomDock;

export namespace Inkeys::UI::Bar
{
	// 多步构造的提示仍需主栏，绘制自动收起在会话期间暂停；橡皮策略保持独立。
	[[nodiscard]] constexpr bool ShouldPauseDrawingAutoCollapse(bool constructionActive, bool erasing) noexcept
	{
		return constructionActive && !erasing;
	}

	// 只按真实竖向模式判断，居中底栏和自由底栏共享 BottomDocked；样式不参与策略。
	[[nodiscard]] constexpr bool ShouldAutoCollapse(int policy, BarBottomDockMode mode) noexcept
	{
		return policy == 2 || (policy == 1 && mode == BarBottomDockMode::Floating);
	}

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
