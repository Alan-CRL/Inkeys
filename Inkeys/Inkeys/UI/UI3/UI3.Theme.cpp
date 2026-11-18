module;

#include <windows.h>
#include "../../Log/Log.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#ifndef RGBA
#define RGBA(r, g, b, a) (COLORREF)(((b) << 16) | ((g) << 8) | (r) | ((a) << 24))
#endif

// L1 主题：状态由产品绑定并实时读取；查询路径检测变更以保持原唤醒时机。
module Inkeys.UI.UI3;

import :Theme;
import Inkeys.UI.UI3.Metrics;
import Inkeys.UI.UI3.Material;

namespace Inkeys::UI::UI3
{
	namespace
	{
		ThemeStyleState* themeStyleSource = nullptr;

		struct ThemeChangeListenerRegistry
		{
			std::mutex mutex;
			ThemeChangeListenerToken nextToken = 1;
			std::vector<std::pair<ThemeChangeListenerToken, std::function<void()>>> listeners;
		};

		ThemeChangeListenerRegistry& GetThemeChangeListenerRegistry()
		{
			// 首次注册时构造，避免产品静态注册器依赖跨模块初始化次序。
			static ThemeChangeListenerRegistry registry;
			return registry;
		}

		void NotifyThemeChangeListeners()
		{
			auto& registry = GetThemeChangeListenerRegistry();
			std::vector<std::function<void()>> callbacks;
			{
				std::lock_guard<std::mutex> lock(registry.mutex);
				callbacks.reserve(registry.listeners.size());
				for (const auto& entry : registry.listeners)
					callbacks.push_back(entry.second);
			}
			// 在锁外回调，允许订阅者安全地注册或注销监听。
			for (const auto& callback : callbacks) callback();
		}
	}

	ThemeChangeListenerToken AddThemeChangeListener(std::function<void()> listener)
	{
		if (!listener) return 0;
		auto& registry = GetThemeChangeListenerRegistry();
		std::lock_guard<std::mutex> lock(registry.mutex);
		const ThemeChangeListenerToken token = registry.nextToken++;
		registry.listeners.emplace_back(token, std::move(listener));
		return token;
	}

	void RemoveThemeChangeListener(ThemeChangeListenerToken token)
	{
		auto& registry = GetThemeChangeListenerRegistry();
		std::lock_guard<std::mutex> lock(registry.mutex);
		for (auto entry = registry.listeners.begin(); entry != registry.listeners.end(); ++entry)
		{
			if (entry->first != token) continue;
			registry.listeners.erase(entry);
			break;
		}
	}

	COLORREF ApplyThemeAlpha(COLORREF color, int alpha)
	{
		if (alpha < 0) alpha = 0;
		if (alpha > 255) alpha = 255;

		return RGBA(GetRValue(color), GetGValue(color), GetBValue(color), alpha);
	}

	COLORREF GetThemeBaseColor(ThemeModeEnum mode, ThemeColorEnum color)
	{
		if (mode == ThemeModeEnum::Light)
		{
			using Role = ThemeMaterial::ColorRole;
			switch (color)
			{
			case ThemeColorEnum::Surface: return ThemeMaterial::LightColor(Role::Surface);
			case ThemeColorEnum::SurfaceFrame: return ThemeMaterial::LightColor(Role::SurfaceFrame);
			case ThemeColorEnum::IconPrimary: return ThemeMaterial::LightColor(Role::IconPrimary);
			case ThemeColorEnum::TextPrimary: return ThemeMaterial::LightColor(Role::TextPrimary);
			case ThemeColorEnum::Accent: return ThemeMaterial::LightColor(Role::Accent);
			case ThemeColorEnum::SelectedFill: return ThemeMaterial::LightColor(Role::SelectedFill);
			case ThemeColorEnum::PressedFill: return ThemeMaterial::LightColor(Role::PressedFill);
			case ThemeColorEnum::SubtleFill: return ThemeMaterial::LightColor(Role::SubtleFill);
			case ThemeColorEnum::SwatchFrame: return ThemeMaterial::LightColor(Role::SwatchFrame);
			case ThemeColorEnum::Divider: return ThemeMaterial::LightColor(Role::Divider);
			case ThemeColorEnum::EdgeLight: return ThemeMaterial::LightColor(Role::EdgeLight);
			case ThemeColorEnum::ShadowKey: return ThemeMaterial::LightColor(Role::ShadowKey);
			case ThemeColorEnum::ShadowAmbient: return ThemeMaterial::LightColor(Role::ShadowAmbient);
			case ThemeColorEnum::TopHighlight: return ThemeMaterial::LightColor(Role::TopHighlight);
			case ThemeColorEnum::DockTarget: return ThemeMaterial::LightColor(Role::DockTarget);
			default: return ThemeMaterial::LightColor(Role::TextPrimary);
			}
		}

		switch (color)
		{
		case ThemeColorEnum::Surface: return RGB(DarkSurfaceColorChannel,
			DarkSurfaceColorChannel, DarkSurfaceColorChannel);
		// 色块边框在深色端沿用表面边框色，保持原观感。
		case ThemeColorEnum::SwatchFrame:
		case ThemeColorEnum::Divider:
		case ThemeColorEnum::EdgeLight:
		case ThemeColorEnum::SurfaceFrame: return RGB(
			DarkSurfaceFrameColorChannel, DarkSurfaceFrameColorChannel,
			DarkSurfaceFrameColorChannel);
		case ThemeColorEnum::IconPrimary:
		case ThemeColorEnum::TopHighlight:
		case ThemeColorEnum::TextPrimary: return RGB(255, 255, 255);
		case ThemeColorEnum::SelectedFill:
		case ThemeColorEnum::Accent: return RGB(88, 255, 236);
		case ThemeColorEnum::PressedFill: return RGB(127, 127, 127);
		case ThemeColorEnum::SubtleFill: return RGB(127, 127, 127);
		case ThemeColorEnum::DockTarget: return RGB(76, 158, 255);
		case ThemeColorEnum::ShadowKey:
		case ThemeColorEnum::ShadowAmbient: return RGB(DarkSurfaceColorChannel,
			DarkSurfaceColorChannel, DarkSurfaceColorChannel);
		default: return RGB(255, 255, 255);
		}
	}

	ThemeModeEnum GetThemeMode(bool darkStyle)
	{
		return darkStyle ? ThemeModeEnum::Dark : ThemeModeEnum::Light;
	}

	void SetThemeStyleSource(ThemeStyleState* style)
	{
		themeStyleSource = style;
	}

	COLORREF GetThemeColor(ThemeModeEnum mode, ThemeColorEnum color, int alpha)
	{
		return ApplyThemeAlpha(GetThemeBaseColor(mode, color), alpha);
	}

	COLORREF GetThemeColor(bool darkStyle, ThemeColorEnum color, int alpha)
	{
		return GetThemeColor(GetThemeMode(darkStyle), color, alpha);
	}

	COLORREF GetThemeColor(ThemeColorEnum color, int alpha)
	{
		// 默认浅色；绑定样式后由当前已应用状态实时决定。
		if (!themeStyleSource) return GetThemeColor(ThemeModeEnum::Light, color, alpha);

		// 主题与透明度由产品层落地；此处只在已应用值变化时通知订阅者。
		// 显式Dark/Light端点查询不走此钩子，避免材质混色误报主题切换。
		static std::atomic<int> applied{-1};
		const bool dark = themeStyleSource->darkStyle;
		const int background = themeStyleSource->backgroundOpacity;
		const int content = themeStyleSource->contentOpacity;
		const int key = (dark ? 0x10000 : 0) | ((background & 0xFF) << 8) | (content & 0xFF);
		if (applied.exchange(key, std::memory_order_relaxed) != key)
		{
			NotifyThemeChangeListeners();
			Inkeys::Log::TryFormat(Inkeys::Log::Level::Debug, Inkeys::Log::Module::UI3,
				"theme applied dark={} background={} content={}", dark, background, content);
		}
		return GetThemeColor(dark, color, alpha);
	}

	// 用户设置的常驻背景绝对透明度（0~1）；0 表示跟随主题默认。读取渲染线程已应用的值。
	double GetThemeResidentBackgroundOpacityOverride()
	{
		return themeStyleSource
			? static_cast<double>(themeStyleSource->backgroundOpacity) / 100.0 : 0.0;
	}

	// 设置页显示与重置用的主题默认常驻背景透明度：Dark 0.80、Light 0.92。
	double GetThemeDefaultResidentBackgroundOpacity(bool darkStyle)
	{
		return ThemeMaterial::DefaultResidentFillOpacity(darkStyle ? 0.0 : 1.0);
	}

	// 常驻内容（主栏/主按钮/分页上的图标与文字）透明度倍率，弹出面板不使用。
	double GetThemeResidentContentOpacity()
	{
		return themeStyleSource
			? static_cast<double>(themeStyleSource->contentOpacity) / 100.0 : 1.0;
	}

	// 常驻表面（主栏、分页）的填充透明度；浅色端更实以免在深色背景上发灰，弹出面板不使用此函数。
	double GetThemeResidentSurfaceOpacity(double darkOpacity)
	{
		const bool darkStyle = themeStyleSource ? static_cast<bool>(themeStyleSource->darkStyle) : false;
		return darkOpacity * ThemeMaterial::ResidentFillOpacityScale(darkStyle ? 0.0 : 1.0,
			ThemeMaterial::LightResidentSurfaceOpacityScale, GetThemeResidentBackgroundOpacityOverride());
	}

	// 不经材质系统的独立表面（橡皮、撤销、分页）用它取得与材质表面一致的边框透明度。
	double GetThemeSurfaceFrameOpacity(double darkOpacity)
	{
		const bool darkStyle = themeStyleSource ? static_cast<bool>(themeStyleSource->darkStyle) : false;
		return ThemeMaterial::FrameOpacity(darkStyle ? 0.0 : 1.0, darkOpacity);
	}

	// 不经主栏材质动画的客户端（分页）取当前主题端点：Dark 0、Light 1。
	double GetThemeLightWeight()
	{
		const bool darkStyle = themeStyleSource ? static_cast<bool>(themeStyleSource->darkStyle) : false;
		return darkStyle ? 0.0 : 1.0;
	}

	double GetThemeDisabledContentOpacity()
	{
		const bool darkStyle = themeStyleSource ? static_cast<bool>(themeStyleSource->darkStyle) : false;
		return ThemeMaterial::DisabledContentOpacity(darkStyle ? 0.0 : 1.0);
	}
}
