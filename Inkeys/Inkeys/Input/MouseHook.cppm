module;

#include <functional>

export module Inkeys.Input.MouseHook;

export namespace Inkeys::Input::MouseHook
{
	using CollapseCallback = std::function<void()>;

	[[nodiscard]] bool Start(CollapseCallback collapseCallback);
	void Stop() noexcept;
	void CancelPendingCollapse() noexcept;
	void CancelPendingMouseUp() noexcept;
}
