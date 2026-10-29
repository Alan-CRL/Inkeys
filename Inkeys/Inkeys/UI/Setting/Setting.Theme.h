#pragma once

namespace Inkeys::UI::Setting
{
	enum class ThemeMode
	{
		Light,
		Dark,
		HighContrast,
	};

	// Acrylic 指 accent policy 的动态背景模糊；仅 Win10 RS4+/Win11 尝试，其余为 Solid。
	enum class BackdropMode
	{
		Solid,
		Acrylic,
	};

	[[nodiscard]] inline constexpr ThemeMode ResolveThemeMode(bool settingDarkMode) noexcept
	{
		// 只使用设置窗口自己的偏好；系统换色不覆盖用户选择，也不影响悬浮栏。
		return settingDarkMode ? ThemeMode::Dark : ThemeMode::Light;
	}

	[[nodiscard]] inline constexpr BackdropMode ResolveBackdropMode(
		bool acrylicEnabled) noexcept
	{
		// 不使用系统 backdrop/Mica，也不提供 Aero blur-behind 回退。
		return acrylicEnabled ? BackdropMode::Acrylic : BackdropMode::Solid;
	}
}
