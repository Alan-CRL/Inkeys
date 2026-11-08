#pragma once

#include "Setting.Controls.h"
#include "Setting.HomeBrand.h"
#include "Setting.SessionState.h"
#include "Setting.EraserDemo.h"
#include "../../Business/DesktopShortcut.h"

struct ImGuiWindow;

namespace Inkeys::UI::Setting::Design
{
	// 数值保留已有内部路由，未迁移页面继续由原 switch 承载。
	enum class PageId : int
	{
		Home = 0, Language = 1, Build = 2, Configuration = 3, General = 4,
		Draw = 5, Plugins = 8, Components = 9,
		HotKeys = 10, Version = 11, Support = 12, Debug = 13, Experimental = 14, Feedback = 15, Performance = 16, Linkage = 17,
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
			destination = { PageId::General, GeneralSection::Shortcuts };
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
		if (destination.page == PageId::Debug || destination.page == PageId::Feedback) state.ClearIndicator();
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
		Appearance, Shortcuts, TermsOfUse,
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
		static constexpr float Wait = .14F, Writing = .90F, Hold = .32F, Dock = .44F, Settle = .18F;
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
		bool fadeOnly = false; // 详情入口只原地渐隐，不向主页横幅归位。
		[[nodiscard]] float EntryTotal() const noexcept { return fadeOnly ? HomeEntryTiming::WritingEnd + HomeEntryTiming::Dock : HomeEntryTiming::Total; }
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

	// 遮盖层完全淡去后才恢复可选标题栏操作，准备帧也不能提前泄露入口。
	[[nodiscard]] inline bool HomeEntryChromeVisible(const HomeMotionState& state) noexcept
	{
		return state.entry != HomeMotionState::Entry::Pending
			&& (!state.EntryActive() || state.entryElapsed >= (state.fadeOnly
				? HomeEntryTiming::WritingEnd + HomeEntryTiming::Dock : HomeEntryTiming::SettleStart));
	}

	inline void FinishHomeEntry(HomeMotionState& state) noexcept
	{
		state.entry=HomeMotionState::Entry::Finished; state.x=state.y=0;
	}
	// 仅真实 Hide→Show 序号触发；内部返回主页和普通激活不能重播。
	inline void ConfigureHomeMotion(HomeMotionState& state, bool home, std::uint64_t showSerial,
		float delta, bool enabled, float rate, bool pointerAllowed = true, bool entryAllowed = true, bool fadeOnly = false) noexcept
	{
		const bool enter=state.showSerial!=showSerial;
		state.fadeOnly=fadeOnly;
		home |= fadeOnly;
		state.home=home; state.showSerial=showSerial; state.enabled=enabled; state.pointerAllowed=pointerAllowed;
		state.delta=enabled && std::isfinite(delta) && std::isfinite(rate)
			? std::clamp(delta,0.0F,.05F)*std::clamp(rate,.1F,5.0F) : 0;
		if (enter)
		{
			// 开屏策略只控制本次入场，不关闭主页原有的悬停与视差动效。
			state.entry=home && enabled && entryAllowed ? HomeMotionState::Entry::Pending : HomeMotionState::Entry::Skipped;
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
	// 开屏遮罩当前颜色（主题 AppBase RGB，alpha 为遮罩不透明度）；未播放时 alpha 为 0。
	[[nodiscard]] ImU32 HomeEntryMaskColor(const HomeMotionState& state);
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

	struct SettingOpenNavigation
	{
		enum class Action { None, Home, SpeedEraser, SoftwareVersion };
		std::uint64_t consumed = 0, showSerial = 0, hideSerial = 0;
		bool fadeOnly = false;
		// 最近一次直达的目标；开屏覆盖只在仍停留于该目标页时保留。
		SettingOpenTarget target = SettingOpenTarget::Current;

		Action Consume(const SettingOpenRequest& request, bool visible,
			std::uint64_t currentShow, std::uint64_t currentHide, bool entryAllowed) noexcept
		{
			if (!visible || showSerial != currentShow || hideSerial != currentHide) fadeOnly = false;
			if (request.serial == consumed) return Action::None;
			consumed = request.serial; fadeOnly = false;
			if (!visible || request.showSerial != currentShow || request.hideSerial != currentHide
				|| request.target == SettingOpenTarget::Current) return Action::None;
			showSerial = currentShow; hideSerial = currentHide; target = request.target;
			// 首帧就选中目标页，开屏只覆盖其上，不再等待主页动画后跳转。
			fadeOnly = request.opening && entryAllowed;
			return request.target == SettingOpenTarget::SoftwareVersion ? Action::SoftwareVersion : Action::SpeedEraser;
		}
	};

	enum class DrawingSubpage : int { Overview, SpeedEraser, PalmEraser, Cursor };
	DrawingSubpage RenderDrawingEraserEntries(DrawingSubpage current);

	inline void NavigateDrawingSubpage(NavigationState& navigation, DrawingSubpage& current,
		DrawingSubpage requested) noexcept
	{
		if (current == requested) return;
		current = requested;
		// 子页仍属于绘制；独立 child ID 保留滚动，只重播现有正文过渡。
		navigation.contentReveal.Reset(navigation.animationsEnabled ? 0.0F : 1.0F);
		navigation.contentReveal.Target(1.0F);
	}

	struct SpeedEraserDraft
	{
		bool automaticEnabled = true;
		int sensitivity = 1;
		std::array<bool, 5> enabled{};
		std::array<int, 2> penResponses{};
		bool automaticPenSupported = true;
		bool touchArea = false;
		bool physicalSizeWarning = false;
	};

	struct SpeedEraserEvents
	{
		bool back = false, automaticChanged = false, sensitivityChanged = false;
		std::array<bool, 5> enabledChanged{};
		std::array<bool, 2> penResponseChanged{};
		bool touchAreaChanged = false;
	};
	SpeedEraserEvents RenderSpeedEraser(SpeedEraserDraft& draft, const EraserDemoPresenter& presenter = {});

	struct PalmEraserDraft
	{
		bool enabled = false;
		int recognitionWindowMs = 150, thresholdPercent = 100;
		bool parallelWriting = true;
		// 每帧只读的多指偏好，只控制说明提示，不写回手掌设置。
		bool multiTouchEnabled = true, laserMultiTouchEnabled = true;
		std::array<bool, 7> tools{ true, true, true, true, true, true, true };
		// 来自绘制输出已应用标尺的 100% 面积；页面本帧调倍率即可同步预览。
		double referenceAreaPixels = 0, referenceAreaCm2 = 0;
		bool previewEstimated = false, physicalSizeWarning = false;
	};
	struct PalmEraserEvents
	{
		bool back = false, enabledChanged = false, recognitionWindowChanged = false;
		bool thresholdChanged = false, thresholdReset = false, parallelWritingChanged = false;
		std::array<bool, 7> toolsChanged{};
	};
	PalmEraserEvents RenderPalmEraser(PalmEraserDraft& draft);
	// 光标专页草稿：四个设备各自的下拉项索引（触摸首项为“无光标”，其余设备首项为“鼠标”）。
	struct CursorPageDraft
	{
		int mouse = 0, touch = 0, screenPen = 0, tablet = 0;
		int dotHideOnContact = 1;
		bool injectedMouse = false;
		bool injectedAvailable = true;
		// Pen、HighlightPen、Dot、HighlightDot 的独立不透明度。
		std::array<float, 4> opacity{ 0.50F, 0.50F, 0.25F, 0.50F };
	};
	struct CursorPageEvents
	{
		bool back = false;
		bool mouse = false, touch = false, screenPen = false, tablet = false;
		bool injectedMouse = false, dotHideOnContact = false;
		std::array<bool, 4> opacityChanged{}, opacityActive{}, opacityReset{};
	};
	CursorPageEvents RenderCursorPage(CursorPageDraft& draft);

	struct PptIdleFadeDraft
	{
		bool bottomPair = true, middlePair = true, mainBar = false;
		int timeoutIndex = 2;
	};
	struct PptIdleFadeEvents
	{
		bool bottomPair = false, middlePair = false, mainBar = false, timeout = false;
	};
	PptIdleFadeEvents RenderPptIdleFadeRow(PptIdleFadeDraft& draft, const std::vector<std::string>& items);

	struct DesktopShortcutDraft
	{
		Business::DesktopShortcut::NameOptions names;
		int preset = Business::DesktopShortcut::Default;
		std::string customName;
		std::wstring lastValidName;
		std::size_t directoryLength = 0;
		bool desktopAvailable = true;
		bool correct = true, create = false;
		BelowComboMotion namePopup;
		DesktopShortcutDraft() = default;
		explicit DesktopShortcutDraft(Business::DesktopShortcut::Kind kind) : names(kind) {}
	};
	std::wstring ResolveDesktopShortcutDraftName(const DesktopShortcutDraft& draft);
	bool DesktopShortcutDraftNamesCollide(const DesktopShortcutDraft& draft, const DesktopShortcutDraft& other);
	bool IsDesktopShortcutDraftValid(const DesktopShortcutDraft& draft, const DesktopShortcutDraft& other);

	struct GeneralDraft
	{
		bool startup;
		float barZoom;
		bool edgeLighting;
		int topInterval;
		bool rightClickClose;
		int safetyMode;
		bool startupPreview = true;
		bool animation = true;
		float animationRate = 1.0F;
		bool dynamicEdgeLighting = false;
		int startupPlacement = 0;
		int taskbarScaleBase = 0;
		int selectMode = 0, iconOrder = 0, expansionDirection = 1;
		std::array<int, 4> autoCollapse{ 1, 0, 0, 0 };
		std::array<bool, 6> shortcuts{ true, true, true, true, true, true };
		std::string shortcutGlobalStatus, shortcutHookStatus;
		bool shortcutStatusWarning = false;
		int settingEntryAnimation = 1;
		bool allowMultipleInstances = false;
		int themeMode = 0; // 0=深色，1=浅色；配置值映射在 Setting.cpp 完成。
		int settingThemeMode = 1; // 设置窗口自身配色，选项顺序与主栏主题一致：0=深色，1=浅色。
		// 常驻透明度为绝对值（0.30~1.00）；背景默认值随主栏主题变化，供显示与重置。
		float backgroundOpacity = 0.80F, backgroundOpacityDefault = 0.80F;
		float contentOpacity = 1.0F;
		DesktopShortcutDraft desktopShortcut;
		DesktopShortcutDraft whiteboardShortcut{ Business::DesktopShortcut::Kind::Whiteboard };
		int startupEffect = 0;
		// 状态指示器：总开关 + 放映模式/超级置顶/新版本；总开关关闭时隐藏勾选但保留值。
		bool statusIndicator = true;
		std::array<bool, 3> statusIndicatorTypes{ true, true, true };
		// 场景进入策略：掩码 bit0 底栏、bit1 底栏折叠、bit2 悬浮窗、bit3 悬浮窗折叠；方向 0 不改变、1 向右、2 向左。
		int pptDockMask = 1, pptDockDirection = 0;
		int whiteboardDockMask = 1, whiteboardDockDirection = 0;
	};

	struct GeneralEvents
	{
		bool startupChanged = false;
		bool createShortcut = false;
		bool desktopNamePresetChanged = false, desktopNameCommitted = false;
		bool desktopCorrectChanged = false, desktopCreateChanged = false;
		bool createWhiteboardShortcut = false;
		bool whiteboardNamePresetChanged = false, whiteboardNameCommitted = false;
		bool whiteboardCorrectChanged = false, whiteboardCreateChanged = false;
		bool barZoomChanged = false;
		bool barZoomActive = false;
		bool edgeLightingChanged = false;
		bool topIntervalChanged = false;
		bool rightClickCloseChanged = false;
		bool safetyModeChanged = false;
		bool allowMultipleInstancesChanged = false;
		bool startupPreviewChanged = false;
		bool startupEffectChanged = false;
		bool settingEntryAnimationChanged = false;
		bool animationChanged = false;
		bool dynamicEdgeLightingChanged = false;
		bool startupPlacementChanged = false;
		bool pptDockMaskChanged = false, pptDockDirectionChanged = false;
		bool whiteboardDockMaskChanged = false, whiteboardDockDirectionChanged = false;
		bool taskbarScaleBaseChanged = false;
		bool themeModeChanged = false;
		bool settingThemeModeChanged = false;
		bool backgroundOpacityChanged = false, backgroundOpacityActive = false, backgroundOpacityReset = false;
		bool contentOpacityChanged = false, contentOpacityActive = false;
		bool selectModeChanged = false, iconOrderChanged = false, expansionDirectionChanged = false;
		std::array<bool, 4> autoCollapseChanged{};
		std::array<bool, 6> shortcutsChanged{};
		bool statusIndicatorChanged = false;
		std::array<bool, 3> statusIndicatorTypesChanged{};
	};

	struct CicdContent
	{
		std::string url, repoUrl, branch, submitter, buildTime;
		std::string buildOS, buildOSVersion, buildRunnerImageOS, buildRunnerImageVersion, msBuildVersion;
	};
	enum class CicdAction { None, Run, Repository };
	CicdAction RenderCicdDetails(const CicdContent& content);
	void RenderSponsorThanks(const char* text, float widthPixels);
	enum class FeedbackAction { None, Issues, Questionnaire, Community, Contact };
	FeedbackAction RenderFeedback(ImTextureID qrTexture, float qrAspect, ImTextureID githubTexture = 0);

	void ApplyContentAlpha(ImGuiWindow* window, float alpha);
	HomeAction RenderHome(const HomeContent& content, HomeMotionState* motion = nullptr);
	GeneralEvents RenderGeneral(GeneralDraft& draft, GeneralSection section = GeneralSection::Basic);
	// translucentBackdrop 取调用方本帧 backdrop 快照，与根清屏/标题栏/正文同源。
	NavigationAction RenderNavigationPane(const ShellGeometry& geometry,
		NavigationState& state, NavigationDestination selected, bool translucentBackdrop = false);
	// >=0 是 PageId；-1 无事件，-2 社区，-3 重启，-4 退出。
	int RenderNavigationPane(const ShellGeometry& geometry,
		NavigationState& state, int selectedPage);
	bool RenderDebugButton(const LayoutRect& bounds, bool selected, NavigationState& state, float foregroundAlpha = 1.0F);
	bool RenderFeedbackButton(const LayoutRect& bounds, bool selected, NavigationState& state, float foregroundAlpha = 1.0F);
	void RenderEdidCard(const char* text);
	void BindTextFonts(ImFont* regular, ImFont* strong);
}
