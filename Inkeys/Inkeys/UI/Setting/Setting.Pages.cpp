#include "Setting.Pages.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include <array>

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
		void WritingIllustration(ImVec2 origin, float width, float height)
		{
			// 首页静态矢量示意直接进入现有 ImDrawData，换色读取同一主题表。
			const auto& palette = GetPalette();
			const float scale = (std::min)(width / 310.0F, height / 212.0F);
			auto point = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->PushClipRect(origin, { origin.x + width, origin.y + height }, true);
			draw->AddCircleFilled(point(216, 71), 110 * scale, palette.artCool);
			draw->AddCircleFilled(point(279, 206), 62 * scale, palette.artWarm);
			draw->AddRectFilled(point(34, 40), point(273, 181), Card, 8 * scale);
			draw->AddRect(point(34, 40), point(273, 181), palette.artStroke, 8 * scale);
			for (int i = 0; i < 2; ++i)
				draw->AddLine(point(58, 68 + i * 10.0F), point(i ? 89.0F : 110.0F, 68 + i * 10.0F), palette.artRule, 3 * scale);
			draw->AddLine(point(58, 151), point(179, 151), palette.artAxis, 1.5F * scale);
			draw->AddLine(point(76, 154), point(76, 98), palette.artAxis, 1.5F * scale);
			draw->AddBezierCubic(point(82, 136), point(103, 136), point(99, 99), point(120, 110), Accent, 3 * scale);
			draw->AddBezierCubic(point(120, 110), point(142, 142), point(157, 137), point(178, 94), Accent, 3 * scale);
			draw->AddTriangle(point(197, 80), point(235, 116), point(187, 116), palette.artWarmInk, 2 * scale);
			draw->AddRectFilled(point(113, 166), point(279, 197), Card, 15.5F * scale);
			draw->AddRect(point(113, 166), point(279, 197), palette.artStroke, 15.5F * scale);
			draw->AddCircleFilled(point(134, 181), 8 * scale, palette.artPenSelection);
			draw->AddLine(point(130, 184), point(138, 176), Accent, 3 * scale);
			draw->AddLine(point(156, 178), point(163, 185), TextSecondary, 4 * scale);
			draw->AddLine(point(182, 181), point(190, 181), TextSecondary, scale);
			draw->AddLine(point(186, 177), point(186, 185), TextSecondary, scale);
			draw->AddCircleFilled(point(214, 181), 4 * scale, Accent);
			draw->AddCircleFilled(point(232, 181), 4 * scale, palette.artOrange);
			draw->AddCircleFilled(point(250, 181), 4 * scale, palette.artGreen);
			draw->PopClipRect();
		}

		void Link(const char* /*id*/, const char* label, HomeAction kind, HomeAction& action)
		{
			ImGui::PushID(static_cast<int>(kind));
			const std::string stableLabel = std::string(label) + "###link";
			if (HyperlinkButton(stableLabel.c_str())) action = kind;
			ImGui::PopID();
		}
	}

	GeneralEvents RenderGeneral(GeneralDraft& draft)
	{
		GeneralEvents events;
		PageHeader(IA(I18nKey.SettingsUI.Regular.N).c_str(), IA(I18nKey.SettingsUI.Design.GeneralSubtitle).c_str());
		SectionHeader(IA(I18nKey.SettingsUI.Design.StartupSection).c_str());
		events.startupChanged = ToggleRow("startup", IA(I18nKey.SettingsUI.Regular.StartUp.AutoStart).c_str(),
			IA(I18nKey.SettingsUI.Regular.StartUp.AutoStartE).c_str(), "\ue7e8", draft.startup);
		const std::string more = IA(I18nKey.SettingsUI.Regular.StartUp.Link.More);
		const std::string create = IA(I18nKey.Operate.Create);
		const int shortcutAction = ButtonGroupRow("shortcut", IA(I18nKey.SettingsUI.Regular.StartUp.Link.N).c_str(),
			IA(I18nKey.SettingsUI.Regular.StartUp.Link.E).c_str(), "\ue71b",
			{ { "more", more }, { "create", create } });
		events.shortcutOptions = shortcutAction == 0;
		events.createShortcut = shortcutAction == 1;

		SectionHeader(IA(I18nKey.SettingsUI.Design.AppearanceSection).c_str());
		const float sliderWidth = ImGui::GetContentRegionAvail().x >= Pixels(740.0F) ? 240.0F : 196.0F;
		SettingRow("bar-zoom", IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.N).c_str(),
			IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.E).c_str(), "\ue9a6", sliderWidth, 32.0F,
			[&](const LayoutRect& bounds) { events.barZoomChanged = SliderAction(bounds, draft.barZoom, 0.50F, 2.00F, events.barZoomActive); });
		SettingRow("setting-zoom", IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.N).c_str(),
			IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.E).c_str(), "\ue8a9", sliderWidth, 32.0F,
			[&](const LayoutRect& bounds) { SliderAction(bounds, draft.settingZoom, 1.00F, 2.00F, events.settingZoomActive); });
		events.edgeLightingChanged = ToggleRow("edge-lighting", IA(I18nKey.SettingsUI.Design.EdgeLighting).c_str(),
			IA(I18nKey.SettingsUI.Design.EdgeLightingDescription).c_str(), "\ue706", draft.edgeLighting);

		SectionHeader(IA(I18nKey.SettingsUI.Design.BehaviorSection).c_str());
		events.topIntervalChanged = ComboRow("top-interval", IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.N).c_str(),
			IA(I18nKey.SettingsUI.Design.TopWindowSummary).c_str(), "\ue922", draft.topInterval,
			{ IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_100ms), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_500ms),
			IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_1s), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_3s),
			IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_5s), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_10s),
			IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_30s) });
		Details("top-interval-details", IA(I18nKey.SettingsUI.Design.TopWindowDetails).c_str(),
			IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.E).c_str());
		events.rightClickCloseChanged = ToggleRow("right-click-close", IA(I18nKey.SettingsUI.Regular.Behavior.RightClickClose).c_str(),
			IA(I18nKey.SettingsUI.Regular.Behavior.RightClickCloseE).c_str(), "\ue8b2", draft.rightClickClose);

		SectionHeader(IA(I18nKey.SettingsUI.Design.CompatibilitySection).c_str());
		events.avoidFullscreenChanged = ToggleRow("avoid-fullscreen", IA(I18nKey.SettingsUI.Regular.Tentative.AvoidFulScreen).c_str(),
			IA(I18nKey.SettingsUI.Design.AvoidFullscreenSummary).c_str(), "\ue7f4", draft.avoidFullscreen);
		Details("avoid-fullscreen-details", IA(I18nKey.SettingsUI.Design.AvoidFullscreenDetails).c_str(),
			IA(I18nKey.SettingsUI.Regular.Tentative.AvoidFulScreenE).c_str());
		events.safetyModeChanged = ComboRow("teaching-safety", IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.N).c_str(),
			IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.E).c_str(), "\ue72e", draft.safetyMode,
			{ IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode1), IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode2),
			IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode3), IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode4) });
		return events;
	}

	HomeAction RenderHome(const HomeContent& content)
	{
		HomeAction action = HomeAction::None;
		const auto& palette = GetPalette();
		PageHeader(IA(I18nKey.SettingsUI.Home.N).c_str(), IA(I18nKey.SettingsUI.Design.HomeWelcome).c_str());
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(20.0F));
		const ImVec2 heroOrigin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float inset = Pixels(width >= Pixels(700.0F) ? 28.0F : 20.0F);
		const float artWidth = width >= Pixels(700.0F) ? Pixels(310.0F) : width >= Pixels(580.0F) ? Pixels(210.0F) : 0.0F;
		const float copyWidth = (std::max)(1.0F, width - artWidth - inset * 2.0F);
		const std::string heroTitle = IA(I18nKey.SettingsUI.Design.HeroTitle);
		const std::string heroDescription = IA(I18nKey.SettingsUI.Design.HeroDescription);
		const std::string drawAction = IA(I18nKey.SettingsUI.Design.DrawAction);
		const std::string tutorial = IA(I18nKey.SettingsUI.Design.Tutorial);
		const float titleHeight = TextHeight(heroTitle.c_str(), copyWidth, ImFluentTextStyle_Title);
		const float descriptionHeight = TextHeight(heroDescription.c_str(), copyWidth);
		const float primaryWidth = (std::min)(copyWidth, Pixels(ButtonWidth(drawAction.c_str())));
		const float tutorialWidth = ControlTextWidth(tutorial.c_str());
		const bool wrapHeroButtons = primaryWidth + Pixels(16.0F) + tutorialWidth > copyWidth;
		const float heroHeight = (std::max)(Pixels(212.0F), inset * 2.0F + Pixels(24.0F) + titleHeight
			+ Pixels(10.0F) + descriptionHeight + Pixels(20.0F) + Pixels(wrapHeroButtons ? 72.0F : 32.0F));
		ImGui::Dummy({ width, heroHeight });
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(heroOrigin, { heroOrigin.x + width, heroOrigin.y + heroHeight }, palette.heroFill, Pixels(8.0F));
		draw->AddRect(heroOrigin, { heroOrigin.x + width, heroOrigin.y + heroHeight }, palette.heroStroke, Pixels(8.0F));
		if (artWidth > 0.0F)
			WritingIllustration({ heroOrigin.x + width - artWidth, heroOrigin.y + (heroHeight - Pixels(212.0F)) * 0.5F }, artWidth, Pixels(212.0F));
		float y = heroOrigin.y + inset;
		TextAt("INKEYS", { heroOrigin.x + inset, y }, copyWidth, ImFluentTextStyle_Caption, palette.heroEyebrow);
		y += Pixels(24.0F);
		TextAt(heroTitle.c_str(), { heroOrigin.x + inset, y }, copyWidth, ImFluentTextStyle_Title);
		y += titleHeight + Pixels(10.0F);
		TextAt(heroDescription.c_str(), { heroOrigin.x + inset, y }, copyWidth, ImFluentTextStyle_Body, palette.heroDescription);
		y += descriptionHeight + Pixels(20.0F);
		ImGui::SetCursorScreenPos({ heroOrigin.x + inset, y });
		if (AccentButton((drawAction + "###home-customize").c_str(), { primaryWidth, Pixels(32.0F) })) action = HomeAction::Draw;
		ImGui::SetCursorScreenPos({ heroOrigin.x + inset + (wrapHeroButtons ? 0.0F : primaryWidth + Pixels(16.0F)), y + (wrapHeroButtons ? Pixels(40.0F) : Pixels(4.0F)) });
		bool showTutorial = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##home-tutorial-visible"));
		if (HyperlinkButton((tutorial + "###home-tutorial").c_str())) showTutorial = !showTutorial;
		ImGui::GetStateStorage()->SetBool(ImGui::GetID("##home-tutorial-visible"), showTutorial);
		ImGui::SetCursorScreenPos({ heroOrigin.x, heroOrigin.y + heroHeight + Pixels(4.0F) });

		SectionHeader(IA(I18nKey.SettingsUI.Design.Explore).c_str());
		struct Feature { const char* title; const char* description; const char* icon; HomeAction action; ImU32 tint; ImU32 ink; };
		const std::array features{
			Feature{ I18nKey.SettingsUI.Draw.N, I18nKey.SettingsUI.Design.DrawDescription, "\uedc6", HomeAction::Draw, palette.featureTint[0], palette.featureInk[0] },
			Feature{ I18nKey.SettingsUI.Preset.N, I18nKey.SettingsUI.Design.PresetDescription, "\uf259", HomeAction::Preset, palette.featureTint[1], palette.featureInk[1] },
			Feature{ I18nKey.SettingsUI.PlugIn.N, I18nKey.SettingsUI.Design.PluginDescription, "\uea86", HomeAction::Plugins, palette.featureTint[2], palette.featureInk[2] }
		};
		const bool featureColumns = width >= Pixels(560.0F);
		const float featureWidth = featureColumns ? (width - Pixels(24.0F)) / 3.0F : width;
		float featureHeight = Pixels(164.0F);
		for (const auto& feature : features)
			featureHeight = (std::max)(featureHeight, Pixels(128.0F) + TextHeight(IA(feature.description).c_str(), featureWidth - Pixels(40.0F), ImFluentTextStyle_Caption));
		const ImVec2 featureOrigin = ImGui::GetCursorScreenPos();
		for (size_t index = 0; index < features.size(); ++index)
		{
			const auto& feature = features[index];
			const ImVec2 origin(featureOrigin.x + (featureColumns ? static_cast<float>(index) * (featureWidth + Pixels(12.0F)) : 0.0F),
				featureOrigin.y + (featureColumns ? 0.0F : static_cast<float>(index) * (featureHeight + Pixels(8.0F))));
			ImGui::PushID(static_cast<int>(index));
			ImGui::SetCursorScreenPos(origin);
			if (Button("##feature", { featureWidth, featureHeight })) action = feature.action;
			draw->AddRectFilled({ origin.x + 1.0F, origin.y + 1.0F }, { origin.x + featureWidth - 1.0F, origin.y + Pixels(84.0F) }, feature.tint, Pixels(4.0F));
			ImFluent::DrawIcon(feature.icon, origin, { origin.x + featureWidth, origin.y + Pixels(84.0F) }, 36.0F, feature.ink);
			TextAt(IA(feature.title).c_str(), { origin.x + Pixels(16.0F), origin.y + Pixels(100.0F) }, featureWidth - Pixels(48.0F), ImFluentTextStyle_BodyStrong);
			TextAt(IA(feature.description).c_str(), { origin.x + Pixels(16.0F), origin.y + Pixels(124.0F) }, featureWidth - Pixels(40.0F), ImFluentTextStyle_Caption, TextSecondary);
			ImFluent::DrawIcon("\ue76c", { origin.x + featureWidth - Pixels(32.0F), origin.y + Pixels(100.0F) }, { origin.x + featureWidth - Pixels(12.0F), origin.y + Pixels(120.0F) }, 12.0F, TextSecondary);
			ImGui::PopID();
		}
		ImGui::SetCursorScreenPos({ featureOrigin.x, featureOrigin.y + (featureColumns ? featureHeight : featureHeight * 3.0F + Pixels(16.0F)) + Pixels(4.0F) });

		// 宽窗常用设置与作者卡并排；窄窗按阅读顺序纵向排列。
		const bool bottomColumns = width >= Pixels(740.0F);
		const ImVec2 bottomOrigin(featureOrigin.x, ImGui::GetCursorScreenPos().y + Pixels(24.0F));
		const float commonWidth = bottomColumns ? (width - Pixels(20.0F)) * (1.2F / 2.2F) : width;
		ImGui::SetCursorScreenPos(bottomOrigin);
		ImGui::BeginChild("##home-common", { commonWidth, 0.0F }, ImGuiChildFlags_AutoResizeY);
		Text(IA(I18nKey.SettingsUI.Design.CommonSettings).c_str(), ImFluentTextStyle_BodyStrong);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(4.0F));
		auto entry = [&](const char* id, const char* title, const char* description, const char* icon, HomeAction target)
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float entryWidth = ImGui::GetContentRegionAvail().x;
			const float copyWidth = (std::max)(1.0F, entryWidth - Pixels(92.0F));
			const float titleHeight = TextHeight(title, copyWidth);
			const float descriptionHeight = TextHeight(description, copyWidth, ImFluentTextStyle_Caption);
			const float height = (std::max)(Pixels(60.0F), titleHeight + descriptionHeight + Pixels(24.0F));
			ImGui::PushID(id);
			if (Button("##entry", { entryWidth, height })) action = target;
			ImFluent::DrawIcon(icon, { origin.x + Pixels(12.0F), origin.y }, { origin.x + Pixels(40.0F), origin.y + height }, 20.0F, TextPrimary);
			const float textY = origin.y + (height - titleHeight - descriptionHeight) * 0.5F;
			TextAt(title, { origin.x + Pixels(52.0F), textY }, copyWidth);
			TextAt(description, { origin.x + Pixels(52.0F), textY + titleHeight }, copyWidth, ImFluentTextStyle_Caption, TextSecondary);
			ImFluent::DrawIcon("\ue76c", { origin.x + entryWidth - Pixels(36.0F), origin.y }, { origin.x + entryWidth - Pixels(12.0F), origin.y + height }, 12.0F, TextSecondary);
			ImGui::PopID();
		};
		entry("home-general", IA(I18nKey.SettingsUI.Regular.N).c_str(), IA(I18nKey.SettingsUI.Design.GeneralDescription).c_str(), "\ue713", HomeAction::General);
		const char* languageKey = I18n::isIdentifying(L"zh-CN") ? I18nKey.SettingsUI.Language.UI.Language.zh_CN
			: I18n::isIdentifying(L"zh-TW") ? I18nKey.SettingsUI.Language.UI.Language.zh_TW : I18nKey.SettingsUI.Language.UI.Language.en_US;
		entry("home-language", IA(I18nKey.SettingsUI.Design.DisplayLanguage).c_str(), IA(languageKey).c_str(), "\ue774", HomeAction::Language);
		ImGui::EndChild();
		const float commonBottom = ImGui::GetItemRectMax().y;
		ImGui::SetCursorScreenPos({ bottomColumns ? bottomOrigin.x + commonWidth + Pixels(20.0F) : bottomOrigin.x,
			bottomColumns ? bottomOrigin.y : commonBottom + Pixels(24.0F) });
		const float authorWidth = bottomColumns ? width - commonWidth - Pixels(20.0F) : width;
		ImGui::BeginChild("##home-author-section", { authorWidth, 0.0F }, ImGuiChildFlags_AutoResizeY);
		Text(IA(I18nKey.SettingsUI.Design.Improve).c_str(), ImFluentTextStyle_BodyStrong);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(4.0F));
		const bool authorVisible = BeginCard("##author");
		if (authorVisible)
		{
			const ImVec2 authorOrigin = ImGui::GetCursorScreenPos();
			const float authorCopyWidth = (std::max)(1.0F, ImGui::GetContentRegionAvail().x - Pixels(48.0F));
			const std::string developer = IA(I18nKey.SettingsUI.Home.Developer);
			const float bioHeight = (std::max)(Pixels(36.0F), Pixels(20.0F) + TextHeight(developer.c_str(), authorCopyWidth, ImFluentTextStyle_Caption));
			ImGui::Dummy({ ImGui::GetContentRegionAvail().x, bioHeight });
			ImGui::GetWindowDrawList()->AddCircleFilled({ authorOrigin.x + Pixels(18.0F), authorOrigin.y + Pixels(18.0F) }, Pixels(18.0F), palette.avatarFill);
			ImFluent::DrawIcon("\ue77b", authorOrigin, { authorOrigin.x + Pixels(36.0F), authorOrigin.y + Pixels(36.0F) }, 20.0F, TextSecondary);
			TextAt("AlanCRL", { authorOrigin.x + Pixels(48.0F), authorOrigin.y }, authorCopyWidth);
			TextAt(developer.c_str(), { authorOrigin.x + Pixels(48.0F), authorOrigin.y + Pixels(20.0F) }, authorCopyWidth, ImFluentTextStyle_Caption, TextSecondary);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(10.0F));
			ImFluent::BeginWrapPanel(Pixels(16.0F), Pixels(8.0F));
			const std::array links{
				std::pair{ I18nKey.SettingsUI.Design.Contact, HomeAction::Contact },
				std::pair{ I18nKey.SettingsUI.Home.FeedBack, HomeAction::Feedback },
				std::pair{ I18nKey.SettingsUI.Sponsor.N, HomeAction::Support }
			};
			for (const auto& link : links)
			{
				const std::string label = IA(link.first);
				ImFluent::WrapPanelNextItem(ControlTextWidth(label.c_str()) + Pixels(16.0F));
				Link(link.first, label.c_str(), link.second, action);
			}
			ImFluent::EndWrapPanel();
		}
		EndCard();
		ImGui::EndChild();
		const float authorBottom = ImGui::GetItemRectMax().y;
		ImGui::SetCursorScreenPos({ bottomOrigin.x, (std::max)(commonBottom, authorBottom) + Pixels(20.0F) });

		ImFluent::BeginWrapPanel(Pixels(16.0F), Pixels(8.0F));
		const std::array footer{
			std::pair{ IA(I18nKey.SettingsUI.Design.Website), HomeAction::Website },
			std::pair{ std::string("GitHub"), HomeAction::GitHub },
			std::pair{ IA(I18nKey.SettingsUI.Community.N), HomeAction::Community },
			std::pair{ std::string("Bilibili"), HomeAction::Bilibili },
			std::pair{ std::string(content.version), HomeAction::Version }
		};
		for (const auto& link : footer)
		{
			ImFluent::WrapPanelNextItem(ControlTextWidth(link.first.c_str()) + Pixels(16.0F));
			Link(link.first.c_str(), link.first.c_str(), link.second, action);
		}
		ImFluent::EndWrapPanel();
		Text(IA(I18nKey.SettingsUI.Home.Prompt).c_str(), ImFluentTextStyle_Caption, TextSecondary);
		if (showTutorial && content.tutorialTexture)
		{
			SectionHeader(tutorial.c_str());
			ImGui::Image(content.tutorialTexture, { width, width * content.tutorialAspect });
		}
		return action;
	}
}
