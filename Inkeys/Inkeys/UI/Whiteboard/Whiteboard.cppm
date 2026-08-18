module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <windows.h>

export module Inkeys.UI.Whiteboard;

export namespace Inkeys::UI::Whiteboard
{
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
		bool nextEnabled = true;
		bool switching = false;
	};

	struct ControlLayout
	{
		RECT bounds{};
		RECT previous{};
		RECT currentPage{};
		RECT totalPage{};
		RECT next{};
	};

	[[nodiscard]] inline PageState ResolvePageState(
		int currentPage, int totalPage, bool switching) noexcept
	{
		PageState state;
		state.totalPage = (std::max)(1, totalPage);
		state.currentPage = std::clamp(currentPage, 1, state.totalPage);
		state.switching = switching;
		state.previousEnabled = !switching && state.currentPage > 1;
		state.nextEnabled = !switching;
		return state;
	}

	[[nodiscard]] inline ControlLayout ResolveControlLayout(
		RECT primaryBounds, float dpiScale, bool left) noexcept
	{
		if (!std::isfinite(dpiScale) || dpiScale <= 0.0F) dpiScale = 1.0F;
		dpiScale = std::clamp(dpiScale, 0.5F, 4.0F);
		const auto px = [dpiScale](float dip) noexcept
			{ return static_cast<LONG>(std::lround(dip * dpiScale)); };
		const LONG width = px(195.0F);
		const LONG height = px(60.0F);
		const LONG margin = px(5.0F);
		const LONG x = left ? primaryBounds.left + margin : primaryBounds.right - margin - width;
		const LONG y = primaryBounds.bottom - margin - height;
		ControlLayout layout;
		layout.bounds = { x, y, x + width, y + height };
		layout.previous = { px(5.0F), px(5.0F), px(55.0F), px(55.0F) };
		layout.currentPage = { px(60.0F), px(4.0F), px(135.0F), px(38.0F) };
		layout.totalPage = { px(60.0F), px(31.0F), px(135.0F), px(58.0F) };
		layout.next = { px(140.0F), px(5.0F), px(190.0F), px(55.0F) };
		return layout;
	}

	bool Initialize(BusinessCallbacks callbacks);
	void Shutdown() noexcept;
	[[nodiscard]] WNDPROC WindowProc() noexcept;
	void PublishActive(bool active) noexcept;
	void PublishPageState(int currentPage, int totalPage, bool switching) noexcept;
	[[nodiscard]] bool Active() noexcept;
	[[nodiscard]] bool BackgroundMatchesActive(bool active) noexcept;
}
