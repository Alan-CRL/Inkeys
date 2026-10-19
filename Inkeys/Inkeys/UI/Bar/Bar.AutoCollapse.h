#pragma once

#include "Bar.BottomDock.h"

namespace Inkeys::UI::Bar
{
	// 只按真实竖向模式判断，居中底栏和自由底栏共享 BottomDocked；样式不参与策略。
	[[nodiscard]] constexpr bool ShouldAutoCollapse(int policy, BarBottomDockMode mode) noexcept
	{
		return policy == 2 || (policy == 1 && mode == BarBottomDockMode::Floating);
	}
}
