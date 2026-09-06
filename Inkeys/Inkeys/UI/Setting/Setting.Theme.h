#pragma once

namespace Inkeys::UI::Setting
{
	enum class ThemeMode
	{
		Light,
		Dark,
		HighContrast,
	};

	enum class BackdropMode
	{
		Solid,
		Mica,
		Acrylic,
	};

	[[nodiscard]] inline constexpr ThemeMode ResolveThemeMode(
		bool /*highContrast*/, bool /*appsUseLightTheme*/) noexcept
	{
		// 浅色样式调整完成前，Settings 暂不跟随系统主题切换。
		return ThemeMode::Light;
	}

	[[nodiscard]] inline constexpr BackdropMode ResolveBackdropMode(
		bool systemBackdropEnabled,
		bool legacyMicaEnabled,
		bool acrylicEnabled) noexcept
	{
		if (systemBackdropEnabled || legacyMicaEnabled)
			return BackdropMode::Mica;
		return acrylicEnabled ? BackdropMode::Acrylic : BackdropMode::Solid;
	}
}
