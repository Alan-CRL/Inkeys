#pragma once

#include <windows.h>
// 传统调用方可能已启用 Win32 min/max 宏；只在此头的依赖边界屏蔽并恢复。
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <algorithm>
#include <cmath>
#include "Bar/Bar.BottomDock.h"
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace Inkeys::UI
{
	enum class StartupPlacement : int
	{
		CenteredBottomBar = 0,
		BottomLeftFloating = 1,
		BottomRightFloating = 2,
	};

	[[nodiscard]] constexpr StartupPlacement NormalizeStartupPlacement(int value) noexcept
	{
		return value == 1 ? StartupPlacement::BottomLeftFloating
			: value == 2 ? StartupPlacement::BottomRightFloating
			: StartupPlacement::CenteredBottomBar;
	}

	inline constexpr double StartupFloatingMarginDip = 24.0;

	// width/height 是包含描边的可见包络，不包含 ULW 透明留白；pixelScale 已含 DPI×zoom。
	[[nodiscard]] inline RECT ResolveStartupPlacementBounds(StartupPlacement placement,
		const RECT& monitorBounds, const RECT& workArea, double visibleWidthDip,
		double visibleHeightDip, double pixelScale, double dpiScale = 1.0) noexcept
	{
		pixelScale = std::isfinite(pixelScale) && pixelScale > 0.0 ? pixelScale : 1.0;
		const LONG width = (std::max)(1L, static_cast<LONG>(std::lround(
			(std::max)(1.0, visibleWidthDip) * pixelScale)));
		const LONG height = (std::max)(1L, static_cast<LONG>(std::lround(
			(std::max)(1.0, visibleHeightDip) * pixelScale)));
		const bool monitorValid = monitorBounds.right > monitorBounds.left
			&& monitorBounds.bottom > monitorBounds.top;
		const RECT monitor = monitorValid ? monitorBounds : workArea;
		const bool workValid = workArea.right > workArea.left && workArea.bottom > workArea.top
			&& workArea.left >= monitor.left && workArea.top >= monitor.top
			&& workArea.right <= monitor.right && workArea.bottom <= monitor.bottom;
		const RECT work = workValid ? workArea : monitor;
		if (placement == StartupPlacement::CenteredBottomBar)
		{
			const LONG left = monitor.left + (monitor.right - monitor.left - width) / 2;
			const LONG bottom = static_cast<LONG>(std::lround(
				Bar::ResolveBarBottomDockLine(monitor, work, 0.0, dpiScale)));
			return { left, bottom - height, left + width, bottom };
		}
		const LONG margin = static_cast<LONG>(std::lround(StartupFloatingMarginDip * pixelScale));
		const LONG left = placement == StartupPlacement::BottomLeftFloating
			? work.left + margin : work.right - margin - width;
		const LONG top = work.bottom - margin - height;
		// 极小工作区仍保留按钮，不让边距把主体推到屏幕外。
		const LONG clampedLeft = std::clamp(left, work.left, (std::max)(work.left, work.right - width));
		const LONG clampedTop = std::clamp(top, work.top, (std::max)(work.top, work.bottom - height));
		return { clampedLeft, clampedTop, clampedLeft + width, clampedTop + height };
	}
}
