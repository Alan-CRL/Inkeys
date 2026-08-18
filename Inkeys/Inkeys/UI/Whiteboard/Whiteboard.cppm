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

export namespace Inkeys::UI::Whiteboard
{
	enum class ControlHitTarget : std::uint8_t
	{
		None,
		Previous,
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
		const LONG height = px(70.0F);
		const LONG margin = px(5.0F);
		const LONG x = left ? primaryBounds.left + margin
			: primaryBounds.right - margin - width;
		const LONG y = primaryBounds.bottom - margin - height;
		ControlLayout layout;
		layout.bounds = { x, y, x + width, y + height };
		layout.previous = { px(5.0F), px(5.0F), px(65.0F), px(65.0F) };
		layout.currentPage = { px(65.0F), px(5.0F), px(130.0F), px(41.0F) };
		layout.totalPage = { px(65.0F), px(36.0F), px(130.0F), px(65.0F) };
		layout.next = { px(130.0F), px(5.0F), px(190.0F), px(65.0F) };
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
