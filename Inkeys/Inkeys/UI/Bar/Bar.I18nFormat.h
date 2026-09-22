#pragma once

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
