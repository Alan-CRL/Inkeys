module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <windows.h>

export module Inkeys.UI.Whiteboard;

import Inkeys.UI.Bar.Metrics;

export namespace Inkeys::UI::Whiteboard
{
	enum class ControlHitTarget : std::uint8_t
	{
		None,
		Previous,
		Page,
		Next,
	};

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

	struct ControlRenderState
	{
		int currentPage = 1;
		int totalPage = 1;
		bool previousEnabled = false;
		bool pageEnabled = true;
		bool nextEnabled = true;
		ControlHitTarget hover = ControlHitTarget::None;
		ControlHitTarget pressed = ControlHitTarget::None;
		POINT pointer{};
		bool pointerKnown = false;
		bool resetVisuals = false;
	};

	struct ControlRenderResult
	{
		bool rendered = false;
		bool animationActive = false;
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
		return state;
	}

	[[nodiscard]] inline ControlLayout ResolveControlLayout(
		RECT primaryBounds, float dpiScale, bool left) noexcept
	{
		if (!std::isfinite(dpiScale) || dpiScale <= 0.0F) dpiScale = 1.0F;
		dpiScale = std::clamp(dpiScale, 0.5F, 4.0F);
		const auto px = [dpiScale](float dip) noexcept
			{ return static_cast<LONG>(std::lround(dip * dpiScale)); };
		const LONG margin = px(static_cast<float>(BarButtonGapDip));
		const LONG button = px(static_cast<float>(BarButtonTwoSideDip));
		const LONG width = px(static_cast<float>(BarButtonTwoSideDip * 3.0
			+ BarButtonGapDip * 4.0));
		const LONG height = px(static_cast<float>(BarMainBarHeightDip));
		const LONG x = left ? primaryBounds.left + margin
			: primaryBounds.right - margin - width;
		const LONG y = primaryBounds.bottom - margin - height;
		ControlLayout layout;
		layout.bounds = { x, y, x + width, y + height };
		const LONG first = margin;
		const LONG second = first + button + margin;
		const LONG third = second + button + margin;
		layout.previous = { first, margin, first + button, margin + button };
		layout.currentPage = { second, margin, second + button, margin + button };
		layout.totalPage = layout.currentPage;
		layout.next = { third, margin, third + button, margin + button };
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
