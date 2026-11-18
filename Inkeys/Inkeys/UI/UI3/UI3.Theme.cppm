module;

#include <windows.h>
#include "../../../IdtAtomic.h"
#include <cstdint>
#include <functional>

#ifndef RGBA
#define RGBA(r, g, b, a) (COLORREF)(((b) << 16) | ((g) << 8) | (r) | ((a) << 24))
#endif

// L1 主题：产品绑定状态，查询实时读取已应用值。
// 变更检测留在默认查询路径，保持现有渲染唤醒的线程与时机。
export module Inkeys.UI.UI3:Theme;

export namespace Inkeys::UI::UI3
{
	// Bar 主题模式，当前先只区分深浅色。
	enum class ThemeModeEnum : int
	{
		Dark,
		Light,
	};

	// Bar 常用角色色，避免绘制逻辑里散落具体 RGB。
	enum class ThemeColorEnum : int
	{
		Surface,
		SurfaceFrame,
		TextPrimary,
		Accent,
		PressedFill,
		SubtleFill,
		SwatchFrame,
		DockTarget,
		IconPrimary,
		SelectedFill,
		Divider,
		EdgeLight,
		ShadowKey,
		ShadowAmbient,
		TopHighlight,
	};

	// 产品层的样式对象继承它并绑定进来；UI3 只读这三项「已应用」值。
	struct ThemeStyleState
	{
		IdtAtomic<bool> darkStyle = true;
		IdtAtomic<int> backgroundOpacity = 0;
		IdtAtomic<int> contentOpacity = 100;
	};

	void SetThemeStyleSource(ThemeStyleState* style);

	using ThemeChangeListenerToken = std::uint64_t;
	// 空回调返回 0；注销只移除后续通知，已复制的回调仍可执行。
	ThemeChangeListenerToken AddThemeChangeListener(std::function<void()> listener);
	void RemoveThemeChangeListener(ThemeChangeListenerToken token);

	COLORREF ApplyThemeAlpha(COLORREF color, int alpha);
	COLORREF GetThemeBaseColor(ThemeModeEnum mode, ThemeColorEnum color);
	ThemeModeEnum GetThemeMode(bool darkStyle);
	COLORREF GetThemeColor(ThemeModeEnum mode, ThemeColorEnum color, int alpha = 255);
	COLORREF GetThemeColor(bool darkStyle, ThemeColorEnum color, int alpha = 255);
	COLORREF GetThemeColor(ThemeColorEnum color, int alpha = 255);
	double GetThemeResidentBackgroundOpacityOverride();
	double GetThemeDefaultResidentBackgroundOpacity(bool darkStyle);
	double GetThemeResidentContentOpacity();
	double GetThemeResidentSurfaceOpacity(double darkOpacity);
	double GetThemeSurfaceFrameOpacity(double darkOpacity);
	double GetThemeLightWeight();
	double GetThemeDisabledContentOpacity();
}
