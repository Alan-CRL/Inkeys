#pragma once

#include "Bar.BottomDock.h"

namespace Inkeys::UI::Bar
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
}
