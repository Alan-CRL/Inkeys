module;
#include <Windows.h>
export module Inkeys.UI.SelectionOverlay;
import Inkeys.Window;
export namespace Inkeys::UI::SelectionOverlay
{
	Inkeys::Window::WindowSpec MakeWindowSpec();
	int RunNativeInputRoutingTests() noexcept;
	bool Acquire();
	void Release() noexcept;
	void NotifyStateChanged() noexcept;
}
