#pragma once

#include "../../../IdtI18nKeys.g.h"
#include <array>
#include <cstddef>
#include <span>

// 设置侧栏的唯一数据源：页面编号、子页、顺序、文案键与图标都在此表中；
// Shell 渲染、旧入口重定向与测试都从这里读取，不再各自维护一份列表。
namespace Inkeys::UI::Setting::Design
{
	// 数值保留已有内部路由，未迁移页面继续由原 switch 承载；已退役编号不复用。
	enum class PageId : int
	{
		Home = 0, Language = 1, Build = 2, Configuration = 3, General = 4,
		Draw = 5, Plugins = 8, Components = 9,
		HotKeys = 10, Version = 11, Support = 12, Debug = 13, Experimental = 14, Feedback = 15, Performance = 16, Linkage = 17,
		Window = 18, Keys = 19, LogsConfig = 20, Appearance = 21,
	};

	// 所有带子页的页面共用一个枚举，数值全局唯一，因此 section 本身即可判定所属页面。
	enum class Section : int
	{
		None = 0,
		Basic, StatusIndicator, DesktopShortcuts,				// 通用
		AppearanceTheme, AppearanceAnimation,					// 外观
		DrawEffects, DrawInteraction, DrawSave,					// 绘制
		WindowLayout, WindowScene, WindowBehavior, WindowDisplay,	// 窗口与显示器
		LogsLog, LogsConfiguration,								// 日志与配置
	};

	struct NavigationDestination
	{
		PageId page = PageId::Home;
		Section section = Section::None;
		bool operator==(const NavigationDestination&) const = default;
	};

	struct NavigationChild { Section section; const char* labelKey; const char* glyph; };
	struct NavigationEntry
	{
		PageId page; const char* labelKey; const char* glyph;
		std::span<const NavigationChild> children; // 空 = 普通单页
	};

	inline constexpr size_t NavigationMaxChildren = 4;

	inline constexpr std::array GeneralChildren{
		NavigationChild{ Section::Basic, I18nKey.SettingsUI.Design.GeneralBasic, "\ue713" },
		NavigationChild{ Section::StatusIndicator, I18nKey.SettingsUI.StatusIndicator.N, "\uea8f" },
		NavigationChild{ Section::DesktopShortcuts, I18nKey.SettingsUI.DesktopShortcut.N, "\ue71b" },
	};
	inline constexpr std::array AppearanceChildren{
		NavigationChild{ Section::AppearanceTheme, I18nKey.SettingsUI.Design.ThemeSection, "\ue790" },
		NavigationChild{ Section::AppearanceAnimation, I18nKey.SettingsUI.Design.AnimationSection, "\ue768" },
	};
	inline constexpr std::array DrawChildren{
		NavigationChild{ Section::DrawEffects, I18nKey.SettingsUI.Design.DrawEffects, "\ue790" },
		NavigationChild{ Section::DrawInteraction, I18nKey.SettingsUI.Design.DrawInteraction, "\ue7c9" },
		NavigationChild{ Section::DrawSave, I18nKey.SettingsUI.Design.DrawSave, "\ue74e" },
	};
	inline constexpr std::array WindowChildren{
		NavigationChild{ Section::WindowLayout, I18nKey.SettingsUI.Design.WindowLayout, "\ue737" },
		NavigationChild{ Section::WindowScene, I18nKey.SettingsUI.Design.WindowScene, "\ue7b3" },
		NavigationChild{ Section::WindowBehavior, I18nKey.SettingsUI.Design.WindowBehavior, "\uf259" },
		NavigationChild{ Section::WindowDisplay, I18nKey.SettingsUI.Design.WindowDisplay, "\ue7f8" },
	};
	inline constexpr std::array LogsConfigChildren{
		NavigationChild{ Section::LogsLog, I18nKey.SettingsUI.Design.LogsLog, "\ue756" },
		NavigationChild{ Section::LogsConfiguration, I18nKey.SettingsUI.Design.LogsConfiguration, "\ue81e" },
	};
	static_assert(GeneralChildren.size() <= NavigationMaxChildren && AppearanceChildren.size() <= NavigationMaxChildren
		&& DrawChildren.size() <= NavigationMaxChildren && WindowChildren.size() <= NavigationMaxChildren && LogsConfigChildren.size() <= NavigationMaxChildren);

	// 侧栏中部顺序即用户看到的顺序；新增页面或子页只改这里。
	inline constexpr std::array NavigationMiddleEntries{
		NavigationEntry{ PageId::General, I18nKey.SettingsUI.Regular.N, "\ue713", GeneralChildren },
		NavigationEntry{ PageId::Appearance, I18nKey.SettingsUI.Design.AppearanceSection, "\ue790", AppearanceChildren },
		NavigationEntry{ PageId::Draw, I18nKey.SettingsUI.Draw.N, "\uee56", DrawChildren },
		NavigationEntry{ PageId::Window, I18nKey.SettingsUI.Design.NavWindowDisplay, "\ue7f4", WindowChildren },
		NavigationEntry{ PageId::Keys, I18nKey.SettingsUI.Design.NavKeysShortcuts, "\ue765", {} },
		NavigationEntry{ PageId::LogsConfig, I18nKey.SettingsUI.Design.NavLogsConfig, "\ue81c", LogsConfigChildren },
		NavigationEntry{ PageId::Performance, I18nKey.SettingsUI.Performance.N, "\uec4a", {} },
		NavigationEntry{ PageId::Linkage, I18nKey.SettingsUI.Linkage.N, "\ue71b", {} },
		NavigationEntry{ PageId::Components, I18nKey.SettingsUI.Component.N, "\ue70b", {} },
		NavigationEntry{ PageId::Plugins, I18nKey.SettingsUI.PlugIn.N, "\ue74c", {} },
		NavigationEntry{ PageId::Experimental, I18nKey.SettingsUI.Design.Experimental, "\ue9d9", {} },
		NavigationEntry{ PageId::Version, I18nKey.SettingsUI.Version.N, "\ue946", {} },
	};
	inline constexpr size_t NavigationMiddleItemCount = NavigationMiddleEntries.size();

	[[nodiscard]] constexpr size_t CountNavigationGroups() noexcept
	{
		size_t count = 0;
		for (const auto& entry : NavigationMiddleEntries) if (!entry.children.empty()) ++count;
		return count;
	}
	inline constexpr size_t NavigationGroupCount = CountNavigationGroups();

	// 页面在中部列表中的下标；不在中部（主页、语言等）返回 -1。
	[[nodiscard]] constexpr int NavigationEntryIndex(PageId page) noexcept
	{
		for (size_t index = 0; index < NavigationMiddleEntries.size(); ++index)
			if (NavigationMiddleEntries[index].page == page) return static_cast<int>(index);
		return -1;
	}

	// 带子页页面在分组数组中的下标；无子页返回 -1。
	[[nodiscard]] constexpr int NavigationGroupIndex(PageId page) noexcept
	{
		int group = 0;
		for (const auto& entry : NavigationMiddleEntries)
		{
			if (entry.children.empty()) continue;
			if (entry.page == page) return group;
			++group;
		}
		return -1;
	}

	// 分组下标对应的页面；下标越界时回落到第一个分组，调用方不必额外判空。
	[[nodiscard]] constexpr PageId NavigationGroupPage(int groupIndex) noexcept
	{
		int group = 0;
		PageId first = PageId::Home;
		bool hasFirst = false;
		for (const auto& entry : NavigationMiddleEntries)
		{
			if (entry.children.empty()) continue;
			if (!hasFirst) { first = entry.page; hasFirst = true; }
			if (group++ == groupIndex) return entry.page;
		}
		return first;
	}

	[[nodiscard]] constexpr std::span<const NavigationChild> NavigationChildren(PageId page) noexcept
	{
		const int index = NavigationEntryIndex(page);
		return index < 0 ? std::span<const NavigationChild>{} : NavigationMiddleEntries[static_cast<size_t>(index)].children;
	}

	[[nodiscard]] constexpr int NavigationChildIndex(PageId page, Section section) noexcept
	{
		const auto children = NavigationChildren(page);
		for (size_t index = 0; index < children.size(); ++index)
			if (children[index].section == section) return static_cast<int>(index);
		return -1;
	}

	// 子页所属页面；非子页返回 Home。
	[[nodiscard]] constexpr PageId PageForSection(Section section) noexcept
	{
		for (const auto& entry : NavigationMiddleEntries)
			for (const auto& child : entry.children)
				if (child.section == section) return entry.page;
		return PageId::Home;
	}

	[[nodiscard]] constexpr Section DefaultSection(PageId page) noexcept
	{
		const auto children = NavigationChildren(page);
		return children.empty() ? Section::None : children.front().section;
	}

	// 有子页的页面只接受自己的子页，其余页面一律为 None。
	[[nodiscard]] constexpr NavigationDestination NormalizeDestination(NavigationDestination destination) noexcept
	{
		// 旧入口只重定向内容；保留页面编号，避免复用已退役路由。
		if (destination.page == PageId::Configuration)
			destination = { PageId::LogsConfig, Section::LogsConfiguration };
		else if (destination.page == PageId::HotKeys)
			destination = { PageId::Keys, Section::None };
		if (NavigationChildIndex(destination.page, destination.section) < 0)
			destination.section = DefaultSection(destination.page);
		return destination;
	}

	[[nodiscard]] constexpr NavigationDestination DestinationForPage(int page) noexcept
	{
		return NormalizeDestination({ static_cast<PageId>(page), Section::None });
	}
}
