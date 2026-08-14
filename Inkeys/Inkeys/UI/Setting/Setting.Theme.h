#pragma once

namespace Inkeys::UI::Setting
{
	enum class ThemeMode
	{
		Light,
		Dark,
		HighContrast,
	};

	[[nodiscard]] inline constexpr ThemeMode ResolveThemeMode(
		bool highContrast, bool appsUseLightTheme) noexcept
	{
		if (highContrast) return ThemeMode::HighContrast;
		return appsUseLightTheme ? ThemeMode::Light : ThemeMode::Dark;
	}
}
