module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
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
		bool previousInteractive = false;
		bool pageInteractive = true;
		bool nextInteractive = true;
		bool nextIsAdd = false;
		bool switching = false;
	};

	[[nodiscard]] inline PageState ResolvePageState(
		int currentPage, int totalPage, bool switching,
		std::optional<bool> latchedNextIsAdd = std::nullopt) noexcept
	{
		PageState state;
		state.totalPage = (std::max)(1, totalPage);
		state.currentPage = std::clamp(currentPage, 1, state.totalPage);
		state.switching = switching;
		// enabled 表示稳定的业务语义；翻页事务只锁住输入，不改变视觉状态。
		state.previousEnabled = state.currentPage > 1;
		state.pageEnabled = true;
		state.nextEnabled = true;
		state.previousInteractive = !switching && state.previousEnabled;
		state.pageInteractive = !switching && state.pageEnabled;
		state.nextInteractive = !switching && state.nextEnabled;
		state.nextIsAdd = state.currentPage >= state.totalPage;
		if (switching && latchedNextIsAdd.has_value())
			state.nextIsAdd = *latchedNextIsAdd;
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
