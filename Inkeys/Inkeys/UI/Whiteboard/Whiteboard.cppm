module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <windows.h>

export module Inkeys.UI.Whiteboard;

export namespace Inkeys::UI::Whiteboard
{
	// Whiteboard 只暴露业务回调；UI 视觉状态由 BarSurfaceScene 持有。
	struct BusinessCallbacks
	{
		std::function<void()> previousPage;
		std::function<void()> nextPage;
	};

	struct PageState
	{
		int currentPage = 1;
		int totalPage = 1;
		bool previousEnabled = false;
		bool pageEnabled = true;
		bool nextEnabled = true;
		bool nextIsAdd = false;
		bool switching = false;
	};

	[[nodiscard]] inline PageState ResolvePageState(
		int currentPage, int totalPage, bool switching) noexcept
	{
		PageState state;
		state.totalPage = (std::max)(1, totalPage);
		state.currentPage = std::clamp(currentPage, 1, state.totalPage);
		state.switching = switching;
		state.previousEnabled = !switching && state.currentPage > 1;
		state.pageEnabled = !switching;
		state.nextEnabled = !switching;
		state.nextIsAdd = !switching && state.currentPage >= state.totalPage;
		return state;
	}

	bool Initialize(BusinessCallbacks callbacks);
	void Shutdown() noexcept;
	[[nodiscard]] WNDPROC WindowProc() noexcept;
	void PublishActive(bool active) noexcept;
	void PublishPageState(int currentPage, int totalPage, bool switching) noexcept;
	[[nodiscard]] bool Active() noexcept;
	[[nodiscard]] bool BackgroundMatchesActive(bool active) noexcept;
}
