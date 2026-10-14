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

	// 入场时间只在这一处定义；准备字体/首帧提交的等待不占用书写时长。
	struct HomeEntryTiming
	{
		static constexpr float Wait = .24F, Writing = 1.06F, Hold = .60F, Dock = .54F, Settle = .22F;
		static constexpr float WritingEnd = Wait + Writing, DockStart = WritingEnd + Hold;
		static constexpr float SettleStart = DockStart + Dock, Total = SettleStart + Settle;
	};
	struct HomeEntryFrameKey
	{
		std::array<float,10> geometry{};
		std::array<float,5> material{1,0,0,0,1};
		std::array<std::uintptr_t,4> fonts{};
		std::uint64_t epoch = 0, fontSerial = 0;
		int language = 0, texture = 0, atlasWidth = 0, atlasHeight = 0;
		bool dark = false;
		int backdrop = 0;
		bool operator==(const HomeEntryFrameKey&) const = default;
	};

	struct HomeMotionState
	{
		HomeBrand::DrawCache cache;
		struct ButtonMotion { bool hovered = false; float elapsed = .32F; };
		std::array<ButtonMotion, 2> buttons;
		std::array<ImVec4, 2> buttonBounds;
		std::uint64_t showSerial = 0, entries = 0;
		enum class Entry { Idle, Pending, Playing, Finished, Skipped };
		Entry entry = Entry::Idle;
		bool home = false, enabled = true, pointerAllowed = true, brandVisible = false, heroVisible = false;
		bool viewportReady = false;
		bool entryLayoutValidated = false;
		std::array<float,17> entryLayoutKey{};
		float delta = 0, x = 0, y = 0, entryElapsed = 0;
		ImVec2 brandOrigin, heroOrigin, heroSize;
		ImVec4 clientContent{}, contentClip{};
		ImVec2 entryStart;
		// 只保存值键和有限计数，不跨帧保存 DrawList/SRV/ImFontBaked 指针。
		struct Preparation
		{
			HomeEntryFrameKey key;
			bool managed = false, keyValid = false, ready = false, revealPending = false, revealReady = false;
			bool fullFrame = true, skipBackground = false, restorePending = false, restoreReady = false, layoutReady = false;
			unsigned warmFrames = 0, revealFrames = 0, restoreFrames = 0, attempts = 0;
			float previousElapsed = 0;
			std::uint64_t frame = 0, submitted = 0, skippedFrames = 0;
		} preparation;
		float brandWidth = 0, entryStartWidth = 0;
		[[nodiscard]] bool EntryActive() const noexcept { return entry==Entry::Playing; }
	};

	inline void FinishHomeEntry(HomeMotionState& state) noexcept
	{
		state.entry=HomeMotionState::Entry::Finished; state.x=state.y=0;
	}
	// 仅真实 Hide→Show 序号触发；内部返回主页和普通激活不能重播。
	inline void ConfigureHomeMotion(HomeMotionState& state, bool home, std::uint64_t showSerial,
		float delta, bool enabled, float rate, bool pointerAllowed = true) noexcept
	{
		const bool enter=state.showSerial!=showSerial;
		state.home=home; state.showSerial=showSerial; state.enabled=enabled; state.pointerAllowed=pointerAllowed;
		state.delta=enabled && std::isfinite(delta) && std::isfinite(rate)
			? std::clamp(delta,0.0F,.05F)*std::clamp(rate,.1F,5.0F) : 0;
		if (enter)
		{
			state.entry=home && enabled ? HomeMotionState::Entry::Pending : HomeMotionState::Entry::Skipped;
			state.entryElapsed=0; state.x=state.y=0;
			state.entryLayoutValidated=false;
			state.preparation={};
			state.buttons={};
		}
		if (!home || !enabled)
		{
			FinishHomeEntry(state);
			for (auto& button : state.buttons) button.elapsed=.32F;
		}
	}

	struct HomeEntryPose { ImVec2 origin; float width = 0, writing = 1, opacity = 0; };
	HomeEntryPose ResolveHomeEntryPose(const HomeMotionState& state);
	void ConfigureHomeEntryViewport(HomeMotionState& state, ImVec4 clientContent, ImVec4 contentClip);
	void RenderHomeEntryOverlay(const HomeMotionState& state);
	bool CanOmitHomeEntryRootBackground(const HomeMotionState& state, bool solidBackdrop,
		float styleAlpha, ImVec4 windowBackground) noexcept;
	HomeEntryFrameKey MakeHomeEntryFrameKey(ImVec4 contentBounds, float gutter, std::uint64_t epoch,
		std::uint64_t fontSerial, int language, ImFont* regular, ImFont* strong, ImFont* icons, int backdrop = 0);
	// 在输入取消之后、正文 Begin 之前调用；不透明遮挡或截图就绪时才暂停页/导航生成。
	bool BeginHomeEntryFrame(HomeMotionState& state, const HomeEntryFrameKey& key, bool allowFreeze = true);
	// 完整正文提交后、outer EndChild 前验证本帧尺寸已被 native 布局消费，不把旧 thumb/高度截入缓存。
	void CompleteHomeEntryLayout(HomeMotionState& state);
	void CompleteHomeEntryFrame(HomeMotionState& state, bool submitted, bool texturesReady, bool captured = false);
	bool HomeEntryTexturesReady(const ImDrawData* data);
	// 仅修改当前帧的提交列表；GPU 页面像素由同设备缓存复原，纹理队列仍交生产后端。
	void FilterHomeEntryDrawData(const HomeMotionState& state, ImDrawData* data,
		ImDrawList* root, ImDrawList* titleBar, ImDrawList* foreground);

	bool CancelHomeEntryOnInput(HomeMotionState& state);
	// Hide 的重置由唯一渲染线程在排布前消费；同帧 Hide→Show 也不会漏掉。
	bool ConsumeSettingHomeReset(std::uint64_t resetSerial, std::uint64_t& consumed,
		NavigationDestination& destination, NavigationState& navigation, HomeMotionState& motion);

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
