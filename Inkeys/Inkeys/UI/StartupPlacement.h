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
		// 以下为后加项；0..2 的数值含义保持不变以兼容旧配置。
		CenteredBottomBarLeft = 3,
		CenteredTopBarRight = 4,
		CenteredTopBarLeft = 5,
		FloatingBottomCenter = 6,
	};

	[[nodiscard]] constexpr StartupPlacement NormalizeStartupPlacement(int value) noexcept
	{
		return value >= 1 && value <= 6 ? static_cast<StartupPlacement>(value)
			: StartupPlacement::CenteredBottomBar;
	}

	// 折叠为单个主按钮的悬浮落点；其余为完整展开的主栏。
	[[nodiscard]] constexpr bool StartupPlacementIsFolded(StartupPlacement p) noexcept
	{
		return p == StartupPlacement::BottomLeftFloating || p == StartupPlacement::BottomRightFloating
			|| p == StartupPlacement::FloatingBottomCenter;
	}
	// 启动即进入底栏吸附（居中）；顶栏与悬浮窗均为 Floating 形态，不参与吸附。
	[[nodiscard]] constexpr bool StartupPlacementDocksToBottom(StartupPlacement p) noexcept
	{
		return p == StartupPlacement::CenteredBottomBar || p == StartupPlacement::CenteredBottomBarLeft;
	}
	[[nodiscard]] constexpr bool StartupPlacementAtTop(StartupPlacement p) noexcept
	{
		return p == StartupPlacement::CenteredTopBarRight || p == StartupPlacement::CenteredTopBarLeft;
	}
	[[nodiscard]] constexpr bool StartupPlacementOpensRight(StartupPlacement p) noexcept
	{
		return p == StartupPlacement::CenteredBottomBar || p == StartupPlacement::BottomLeftFloating
			|| p == StartupPlacement::CenteredTopBarRight || p == StartupPlacement::FloatingBottomCenter;
	}

	// 设置页的“类型 + 位置”只是同一枚举的展示拆分。
	// 类型：0 居中底栏、1 居中顶栏、2 悬浮窗；位置：底栏/顶栏 0 向右展开、1 向左展开；悬浮窗 0 左下角、1 右下角、2 底栏居中。
	[[nodiscard]] constexpr int StartupPlacementType(StartupPlacement p) noexcept
	{
		return StartupPlacementAtTop(p) ? 1 : StartupPlacementDocksToBottom(p) ? 0 : 2;
	}
	[[nodiscard]] constexpr int StartupPlacementPosition(StartupPlacement p) noexcept
	{
		switch (p)
		{
		case StartupPlacement::CenteredBottomBarLeft: case StartupPlacement::CenteredTopBarLeft: return 1;
		case StartupPlacement::BottomRightFloating: return 1;
		case StartupPlacement::FloatingBottomCenter: return 2;
		default: return 0;
		}
	}
	[[nodiscard]] constexpr int StartupPlacementPositionCount(int type) noexcept { return type == 2 ? 3 : 2; }
	[[nodiscard]] constexpr StartupPlacement StartupPlacementFromSelection(int type, int position) noexcept
	{
		if (type == 1) return position == 1 ? StartupPlacement::CenteredTopBarLeft : StartupPlacement::CenteredTopBarRight;
		if (type == 2)
			return position == 1 ? StartupPlacement::BottomRightFloating
				: position == 2 ? StartupPlacement::FloatingBottomCenter : StartupPlacement::BottomLeftFloating;
		return position == 1 ? StartupPlacement::CenteredBottomBarLeft : StartupPlacement::CenteredBottomBar;
	}

	inline constexpr double StartupFloatingMarginDip = 24.0;

	// width/height 是包含描边的可见包络，不包含 ULW 透明留白；pixelScale 已含 DPI×zoom。
	[[nodiscard]] inline RECT ResolveStartupPlacementBounds(StartupPlacement placement,
		const RECT& monitorBounds, const RECT& workArea, double visibleWidthDip,
		double visibleHeightDip, double pixelScale, double dpiScale = 1.0,
		double desktopDockLine = std::numeric_limits<double>::quiet_NaN()) noexcept
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
		const LONG centeredLeft = monitor.left + (monitor.right - monitor.left - width) / 2;
		if (StartupPlacementDocksToBottom(placement))
		{
			const LONG bottom = static_cast<LONG>(std::lround(
				Bar::ResolveBarBottomDockLine(monitor, work, 0.0, dpiScale, desktopDockLine)));
			return { centeredLeft, bottom - height, centeredLeft + width, bottom };
		}
		const LONG margin = static_cast<LONG>(std::lround(StartupFloatingMarginDip * pixelScale));
		if (StartupPlacementAtTop(placement))
		{
			// 顶栏仅是初始落点：贴工作区上沿，上方有任务栏时工作区已让出该高度。
			const LONG left = std::clamp(centeredLeft, monitor.left, (std::max)(monitor.left, monitor.right - width));
			const LONG top = std::clamp(work.top, work.top, (std::max)(work.top, work.bottom - height));
			return { left, top, left + width, top + height };
		}
		const LONG left = placement == StartupPlacement::BottomLeftFloating ? work.left + margin
			: placement == StartupPlacement::FloatingBottomCenter ? centeredLeft : work.right - margin - width;
		const LONG top = work.bottom - margin - height;
		// 极小工作区仍保留按钮，不让边距把主体推到屏幕外。
		const LONG clampedLeft = std::clamp(left, work.left, (std::max)(work.left, work.right - width));
		const LONG clampedTop = std::clamp(top, work.top, (std::max)(work.top, work.bottom - height));
		return { clampedLeft, clampedTop, clampedLeft + width, clampedTop + height };
	}
}
