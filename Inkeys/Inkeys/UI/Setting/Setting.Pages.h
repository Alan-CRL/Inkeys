#pragma once

#include "Setting.Controls.h"

namespace Inkeys::UI::Setting::Design
{
	// 数值保留已有内部路由，未迁移页面继续由原 switch 承载。
	enum class PageId : int
	{
		Home = 0, Language = 1, Build = 2, Configuration = 3, General = 4,
		Draw = 5, Plugins = 8, Components = 9,
		HotKeys = 10, Version = 11, Support = 12, Debug = 13, Experimental = 14,
	};

	enum class HomeAction
	{
		None, Draw, Plugins, General, Language, Support, Website,
		GitHub, Community, Bilibili, Feedback, Contact, Version,
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

	HomeAction RenderHome(const HomeContent& content);
	GeneralEvents RenderGeneral(GeneralDraft& draft);
	// >=0 是 PageId；-1 无事件，-2 社区，-3 重启，-4 退出。
	int RenderNavigationPane(const ShellGeometry& geometry,
		NavigationState& state, int selectedPage);
	void BindTextFonts(ImFont* regular, ImFont* strong);
}
