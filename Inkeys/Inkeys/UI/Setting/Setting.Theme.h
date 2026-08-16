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
		bool /*highContrast*/, bool /*appsUseLightTheme*/) noexcept
	{
		// 浅色样式调整完成前，Settings 暂不跟随系统主题切换。
		return ThemeMode::Light;
	}
}
