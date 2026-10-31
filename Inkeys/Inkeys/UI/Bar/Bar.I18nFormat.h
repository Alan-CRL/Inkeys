#pragma once

#include "../../Business/PenWidthPolicy.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace Inkeys::UI::Bar::Detail
{
	[[nodiscard]] inline std::wstring FormatThicknessText(
		std::wstring_view format, int thickness)
	{
		try
		{
			return std::vformat(format, std::make_wformat_args(thickness));
		}
		catch (const std::format_error&)
		{
			// 本地化格式串损坏时仍保留数值，避免异常逃逸到渲染线程。
			return std::to_wstring(thickness);
		}
	}

	[[nodiscard]] inline std::wstring FormatWidthDipNumber(double widthDip)
	{
		using namespace Inkeys::Business::PenWidth;
		const double safeDip = IsValidDip(widthDip) ? widthDip : 1.0;
		const auto displayed = RoundPositiveCanvasPx(safeDip);
		// 整数仅用于展示，不回写业务粗细；有效的小于 1 DIP 值仍显示 1。
		return std::to_wstring(displayed);
	}

	[[nodiscard]] inline std::wstring FormatWidthDipText(
		std::wstring_view format, double widthDip)
	{
		const auto number = FormatWidthDipNumber(widthDip);
		try
		{
			return std::vformat(format, std::make_wformat_args(number));
		}
		catch (const std::format_error&)
		{
			return number;
		}
	}

	[[nodiscard]] inline double StepFineDialDip(
		double originDip, double requestedDip, double minimum, double maximum)
	{
		using namespace Inkeys::Business::PenWidth;
		if (!IsValidDip(originDip)) return 1.0;
		if (!std::isfinite(requestedDip) || !IsValidDip(minimum)
			|| !IsValidDip(maximum) || minimum > maximum) return originDip;
		const double delta = requestedDip - originDip;
		// 步数按绝对值半整数向上，保留进入时的小数基点，正反拨动对称。
		const double steps = std::copysign(std::floor(std::abs(delta) + 0.5), delta);
		return (std::clamp)(originDip + steps, minimum, maximum);
	}

	[[nodiscard]] inline std::wstring FormatFrameRateText(
		std::wstring_view format, double actualFramesPerSecond,
		double unlimitedFramesPerSecond, std::wstring_view fallback)
	{
		try
		{
			return std::vformat(format, std::make_wformat_args(
				actualFramesPerSecond, unlimitedFramesPerSecond));
		}
		catch (const std::format_error&)
		{
			return std::wstring(fallback);
		}
	}
}
