#pragma once

#include "Setting.Controls.h"
#include "Setting.HomeBrand.h"

struct ImGuiWindow;

namespace Inkeys::UI::Setting::Design
{
	// 数值保留已有内部路由，未迁移页面继续由原 switch 承载。
	enum class PageId : int
	{
		Home = 0, Language = 1, Build = 2, Configuration = 3, General = 4,
		Draw = 5, Plugins = 8, Components = 9,
		HotKeys = 10, Version = 11, Support = 12, Debug = 13, Experimental = 14,
	};

	struct NavigationDestination
	{
		PageId page = PageId::Home;
		GeneralSection section = GeneralSection::Basic;
		bool operator==(const NavigationDestination&) const = default;
	};

	[[nodiscard]] inline NavigationDestination NormalizeDestination(NavigationDestination destination) noexcept
	{
		// 旧入口只重定向内容；保留页面编号，避免复用已退役路由。
		if (destination.page == PageId::Configuration)
			destination = { PageId::General, GeneralSection::Storage };
		else if (destination.page == PageId::HotKeys)
			destination = { PageId::General, GeneralSection::Interaction };
		if (destination.page != PageId::General) destination.section = GeneralSection::Basic;
		return destination;
	}

	[[nodiscard]] inline NavigationDestination DestinationForPage(int page) noexcept
	{
		return NormalizeDestination({ static_cast<PageId>(page), GeneralSection::Basic });
	}

	enum class NavigationActionKind { None, Navigate, Community, Restart, Exit };
	struct NavigationAction
	{
		NavigationActionKind kind = NavigationActionKind::None;
		NavigationDestination destination;
	};

	inline void NotifyNavigationDestination(NavigationState& state, NavigationDestination destination) noexcept
	{
		destination = NormalizeDestination(destination);
		if (destination.page == PageId::General) state.lastGeneralSection = destination.section;
		if (state.contentPage == static_cast<int>(destination.page) && state.contentSection == destination.section) return;
		if (destination.page == PageId::Debug) state.ClearIndicator();
		state.contentPage = static_cast<int>(destination.page);
		state.contentSection = destination.section;
		state.contentReveal.Reset(state.animationsEnabled ? 0.0F : 1.0F);
		state.contentReveal.Target(1.0F);
		state.scrollSelectedGeneral = destination.page == PageId::General;
	}

	enum class HomeAction
	{
		None, Draw, Plugins, General, Language, Support, Website,
		GitHub, Community, Bilibili, Feedback, Contact, Version, License, GitCode,
	};

	struct HomeContent
	{
		ImTextureID tutorialTexture = 0;
		float tutorialAspect = 1.0F;
		const char* version = "";
		ImTextureID avatarTexture = 0;
		ImTextureID feedbackTexture = 0;
		float feedbackAspect = 1.0F;
	};

	struct HomeMotionState
	{
		HomeBrand::DrawCache cache;
		HomeBrand::RippleCache ripple;
		struct ButtonMotion { bool hovered = false; float elapsed = .32F; };
		std::array<ButtonMotion, 2> buttons;
		std::array<ImVec4, 2> buttonBounds;
		std::uint64_t showSerial = 0, entries = 0;
		bool home = false, enabled = true, pointerAllowed = true, brandVisible = false, heroVisible = false;
		float delta = 0, x = 0, y = 0, shineElapsed = .9F;
		ImVec2 brandOrigin, heroOrigin, heroSize;
		float brandWidth = 0;
	};

	// 页面身份和 Hide→Show 序号触发开场；帧缺席、尺寸和主题都不是进入事件。
	inline void ConfigureHomeMotion(HomeMotionState& state, bool home, std::uint64_t showSerial,
		float delta, bool enabled, float rate, bool pointerAllowed = true) noexcept
	{
		const bool enter=home && (!state.home || state.showSerial!=showSerial);
		state.home=home; state.showSerial=showSerial; state.enabled=enabled; state.pointerAllowed=pointerAllowed;
		state.delta=enabled && std::isfinite(delta) && std::isfinite(rate)
			? std::clamp(delta,0.0F,.05F)*std::clamp(rate,.1F,5.0F) : 0;
		if (enter)
		{
			++state.entries; state.shineElapsed=0; state.x=state.y=0;
			state.buttons={};
		}
		if (!home || !enabled)
		{
			state.x=state.y=0; state.shineElapsed=.9F;
			for (auto& button : state.buttons) button.elapsed=.32F;
		}
	}

	struct GeneralDraft
	{
		bool startup;
		float barZoom;
		float settingZoom;
		bool edgeLighting;
		int topInterval;
		bool rightClickClose;
		int safetyMode;
		bool startupPreview = true;
		bool animation = true;
		float animationRate = 1.0F;
		bool dynamicEdgeLighting = false;
	};

	struct GeneralEvents
	{
		bool startupChanged = false;
		bool createShortcut = false;
		bool shortcutOptions = false;
		bool barZoomChanged = false;
		bool barZoomActive = false;
		bool settingZoomActive = false;
		bool edgeLightingChanged = false;
		bool topIntervalChanged = false;
		bool rightClickCloseChanged = false;
		bool safetyModeChanged = false;
		bool startupPreviewChanged = false;
		bool animationChanged = false;
		bool animationRateActive = false;
		bool dynamicEdgeLightingChanged = false;
	};

	void ApplyContentAlpha(ImGuiWindow* window, float alpha);
	HomeAction RenderHome(const HomeContent& content, HomeMotionState* motion = nullptr);
	GeneralEvents RenderGeneral(GeneralDraft& draft, GeneralSection section = GeneralSection::Basic);
	NavigationAction RenderNavigationPane(const ShellGeometry& geometry,
		NavigationState& state, NavigationDestination selected);
	// >=0 是 PageId；-1 无事件，-2 社区，-3 重启，-4 退出。
	int RenderNavigationPane(const ShellGeometry& geometry,
		NavigationState& state, int selectedPage);
	bool RenderDebugButton(const LayoutRect& bounds, bool selected, NavigationState& state, float foregroundAlpha = 1.0F);
	void RenderEdidCard(const char* text);
	void BindTextFonts(ImFont* regular, ImFont* strong);
}
