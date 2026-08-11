module;

#include "../../../IdtMain.h"

export module Inkeys.UI.Bar:Atomic;

import Inkeys.UI.Bar.WakeSignal;

export namespace BarAtomic
{
	Inkeys::UI::Bar::WakeSignal wait;

	// 此标识表示 UI 将不检查是否变动，而持续渲染
	IdtAtomic<bool> sustainFlag = false;

	// 此标识表示 UI 下一帧即使没有计算值变化也需要渲染一次
	IdtAtomic<bool> renderOnceFlag = false;
}
