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

	[[nodiscard]] inline double EaseBarDisplayTransition(double progress) noexcept
	{
		progress = std::clamp(progress, 0.0, 1.0);
		return progress < 0.5
			? 4.0 * progress * progress * progress
			: 1.0 - std::pow(-2.0 * progress + 2.0, 3.0) / 2.0;
	}
}
