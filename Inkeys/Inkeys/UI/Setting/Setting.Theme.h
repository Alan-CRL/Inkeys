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

	[[nodiscard]] inline constexpr ThemeMode ResolveThemeMode(bool settingDarkMode) noexcept
	{
		// 只使用设置窗口自己的偏好；系统换色不覆盖用户选择，也不影响悬浮栏。
		return settingDarkMode ? ThemeMode::Dark : ThemeMode::Light;
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
