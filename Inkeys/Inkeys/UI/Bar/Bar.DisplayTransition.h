#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace Inkeys::UI::Bar
{
	struct BarDisplayPlacement
	{
		double startLocalCenterX = 0.0;
		double startLocalCenterY = 0.0;
		double targetLocalCenterX = 0.0;
		double targetLocalCenterY = 0.0;
	};

	[[nodiscard]] inline BarDisplayPlacement ResolveBarDisplayPlacement(
		const RECT& previousMonitor,
		const RECT& targetMonitor,
		double previousLocalCenterX,
		double previousLocalCenterY,
		double targetHalfWidth,
		double targetHalfHeight) noexcept
	{
		const double screenX = previousMonitor.left + previousLocalCenterX;
		const double screenY = previousMonitor.top + previousLocalCenterY;
		const double width = static_cast<double>((std::max)(1L,
			targetMonitor.right - targetMonitor.left));
		const double height = static_cast<double>((std::max)(1L,
			targetMonitor.bottom - targetMonitor.top));
		targetHalfWidth = (std::max)(0.0, targetHalfWidth);
		targetHalfHeight = (std::max)(0.0, targetHalfHeight);
		return {
			screenX - targetMonitor.left,
			screenY - targetMonitor.top,
			std::clamp(previousLocalCenterX, targetHalfWidth,
				(std::max)(targetHalfWidth, width - targetHalfWidth)),
			std::clamp(previousLocalCenterY, targetHalfHeight,
				(std::max)(targetHalfHeight, height - targetHalfHeight)),
		};
	}

	// 仅稳定底栏允许任务栏动画写 Y；拖动、恢复及场景动画持有原所有权。
	[[nodiscard]] inline bool CanAnimateBarTaskbarDockY(bool docked, bool dragging,
		bool recovering, bool desktop, bool placementPending, bool sceneAnimating) noexcept
	{
		return docked && !dragging && !recovering && desktop
			&& !placementPending && !sceneAnimating;
	}

	[[nodiscard]] inline bool ShouldRetargetBarTaskbarDockY(
		double previousTarget, double nextTarget) noexcept
	{
		return std::isfinite(nextTarget)
			&& (!std::isfinite(previousTarget) || std::abs(previousTarget - nextTarget) > 0.000001);
	}

	template<class Value>
	[[nodiscard]] inline bool RetargetBarTaskbarDockY(Value& y,
		double nextTarget, double presentedY) noexcept
	{
		if (!ShouldRetargetBarTaskbarDockY(y.tar, nextTarget)) return false;
		if (std::isfinite(presentedY)) y.SetDirect(presentedY);
		y.SetTar(nextTarget, 0.4);
		return !y.IsSame();
	}

	[[nodiscard]] inline double EaseBarDisplayTransition(double progress) noexcept
	{
		progress = std::clamp(progress, 0.0, 1.0);
		return progress < 0.5
			? 4.0 * progress * progress * progress
			: 1.0 - std::pow(-2.0 * progress + 2.0, 3.0) / 2.0;
	}
}
