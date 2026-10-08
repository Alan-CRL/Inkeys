#include "Setting.Pages.h"
#include "imfluent/imfluent_internal.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include <array>

namespace Inkeys::UI::Setting::Design
{
	void BindTextFonts(ImFont* regular, ImFont* strong)
	{
		SetStateLabels(IA(I18nKey.SettingsUI.Design.Enabled).c_str(), IA(I18nKey.SettingsUI.Design.Disabled).c_str());
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Caption, regular, 12.0F);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body, regular, 14.0F);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_BodyStrong, strong, 14.0F);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Subtitle, strong, 20.0F);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Title, strong, 28.0F);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_TitleLarge, strong, 40.0F);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Display, strong, 68.0F);
	}

	int RenderNavigationPane(const ShellGeometry& geometry,
		NavigationState& state, int selectedPage)
	{
		const auto& pane = geometry.pane;
		const bool expanded = state.Expanded();
		const std::string debugLabel = IA(I18nKey.SettingsUI.DebugSoftware.N);
		const float debugWidth = TextWidth(debugLabel.c_str(), ImFluentTextStyle_Caption)
			/ Pixels(1.0F) + 24.0F;
		const auto sections = ResolveNavigationPaneGeometry(pane.Width(), pane.Height(),
			expanded, ImFluent::GetStyle().NavItemHeight, debugWidth);
		ImGui::SetCursorPos({ Pixels(pane.left), Pixels(pane.top) });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, AppBase);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0F, 0.0F });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0F, Pixels(RowGap) });
		ImGui::BeginChild("##settings-pane", { Pixels(pane.Width()), Pixels(pane.Height()) },
			ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { Pixels(4.0F), 0.0F });
		int result = -1;
		bool toggled = false;
		struct Entry { PageId page; const char* key; const char* glyph; };
		const std::array topEntries{
			Entry{ PageId::Home, I18nKey.SettingsUI.Home.N, "\ue80f" },
			Entry{ PageId::Language, I18nKey.SettingsUI.Language.N, "\uf2b7" }
		};
		const std::array middleEntries{
			Entry{ PageId::General, I18nKey.SettingsUI.Regular.N, "\ue7b8" },
			Entry{ PageId::Draw, I18nKey.SettingsUI.Draw.N, "\uee56" },
			Entry{ PageId::Plugins, I18nKey.SettingsUI.PlugIn.N, "\ue74c" },
			Entry{ PageId::Components, I18nKey.SettingsUI.Component.N, "\ue70b" },
			Entry{ PageId::HotKeys, I18nKey.SettingsUI.HotKey.N, "\ue765" },
			Entry{ PageId::Experimental, I18nKey.SettingsUI.Design.Experimental, "\uec4a" },
			Entry{ PageId::Configuration, I18nKey.SettingsUI.Configuration.N, "\ue81e" },
			Entry{ PageId::Version, I18nKey.SettingsUI.Version.N, "\ue946" }
		};
		static_assert(middleEntries.size() == NavigationMiddleItemCount);
		auto renderEntry = [&](const Entry& entry) {
			ImGui::PushID(static_cast<int>(entry.page));
			const bool selected = selectedPage == static_cast<int>(entry.page)
				|| (entry.page == PageId::Version && selectedPage == static_cast<int>(PageId::Build));
			if (ImFluent::NavItemEx("##page", IA(entry.key).c_str(), selected, entry.glyph, !expanded))
				result = static_cast<int>(entry.page);
			ImGui::PopID();
		};
		auto setPosition = [](const LayoutRect& bounds, float sectionTop = 0.0F) {
			ImGui::SetCursorPos({ Pixels(bounds.left), Pixels(bounds.top - sectionTop) });
		};
		auto divider = [](const LayoutRect& bounds, float sectionTop = 0.0F) {
			const ImVec2 origin = ImGui::GetWindowPos();
			ImGui::GetWindowDrawList()->AddRectFilled(
				{ origin.x + Pixels(bounds.left), origin.y + Pixels(bounds.top - sectionTop) },
				{ origin.x + Pixels(bounds.right), origin.y + Pixels(bounds.bottom - sectionTop) },
				ImFluent::GetColorU32(ImFluentCol_DividerStrokeDefault));
		};
		const auto fixedFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

		// 顶部的汉堡、主页和语言单独固定；中段滚动不会改变它们的位置。
		ImGui::SetCursorPos({ 0.0F, 0.0F });
		if (ImGui::BeginChild("##settings-nav-top", { 0.0F, Pixels(sections.top.Height()) },
			ImGuiChildFlags_AlwaysUseWindowPadding, fixedFlags))
		{
			setPosition(sections.menu);
			const ImVec2 menuOrigin = ImGui::GetCursorScreenPos();
			ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault, IM_COL32(0, 0, 0, 0));
			ImFluent::PushStyleColor(ImFluentCol_ControlStrokeDefault, IM_COL32(0, 0, 0, 0));
			ImFluent::PushStyleColor(ImFluentCol_ControlStrokeSecondary, IM_COL32(0, 0, 0, 0));
			ImFluent::PushStyleColor(ImFluentCol_ElevationControlBottom, IM_COL32(0, 0, 0, 0));
			toggled = Button("##pane-toggle", { Pixels(sections.menu.Width()), Pixels(sections.menu.Height()) });
			ImFluent::PopStyleColor(4);
			ImFluent::DrawIcon("\ue700", menuOrigin,
				{ menuOrigin.x + Pixels(sections.menu.Width()), menuOrigin.y + Pixels(sections.menu.Height()) },
				NavigationGlyphSize, TextPrimary);
			if (expanded)
				TextAt(IA(I18nKey.SettingsUI.Design.NavigationTitle).c_str(),
					{ menuOrigin.x + Pixels(44.0F), menuOrigin.y + Pixels(8.0F) },
					Pixels(pane.Width() - 56.0F), ImFluentTextStyle_BodyStrong);
			for (size_t index = 0; index < topEntries.size(); ++index)
			{
				setPosition(sections.topItems[index]);
				renderEntry(topEntries[index]);
			}
			divider(sections.separators[0]);
		}
		ImGui::EndChild();

		// 只有此 child 接受滚动；隐藏滚动条保留完整行宽与 x=24 的图标轴。
		if (sections.middle.Height() > 0.0F)
		{
			setPosition(sections.middle);
			if (ImGui::BeginChild("##settings-nav-list", { 0.0F, Pixels(sections.middle.Height()) },
				ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar))
			{
				for (const auto& entry : middleEntries) renderEntry(entry);
			}
			ImGui::EndChild();
		}

		setPosition(sections.footer);
		if (ImGui::BeginChild("##settings-nav-footer", { 0.0F, Pixels(sections.footer.Height()) },
			ImGuiChildFlags_AlwaysUseWindowPadding, fixedFlags))
		{
			const float footerTop = sections.footer.top;
			divider(sections.separators[1], footerTop);
			setPosition(sections.footerItems[0], footerTop);
			if (ImFluent::NavItemEx("##community", IA(I18nKey.SettingsUI.Community.N).c_str(),
				false, "\ue716", !expanded)) result = -2;
			setPosition(sections.footerItems[1], footerTop);
			ImGui::PushID(static_cast<int>(PageId::Support));
			if (AccentNavigationItem("##page", IA(I18nKey.SettingsUI.Sponsor.N).c_str(),
				selectedPage == static_cast<int>(PageId::Support), "\ue789", !expanded))
				result = static_cast<int>(PageId::Support);
			ImGui::PopID();
			divider(sections.separators[2], footerTop);
			setPosition(sections.footerItems[2], footerTop);
			if (ImFluent::NavItemEx("##restart", IA(I18nKey.SettingsUI.RestartSoftware.N).c_str(),
				false, "\ue72c", !expanded)) result = -3;
			setPosition(sections.footerItems[3], footerTop);
			ImFluent::PushStyleColor(ImFluentCol_TextPrimary, GetPalette().criticalText);
			if (ImFluent::NavItemEx("##exit", IA(I18nKey.SettingsUI.ExitSoftware.N).c_str(),
				false, "\ue711", !expanded)) result = -4;
			ImFluent::PopStyleColor();
			if (sections.debugVisible)
			{
				setPosition(sections.debug, footerTop);
				ImGui::PushID(static_cast<int>(PageId::Debug));
				if (CaptionNavigationButton("page", debugLabel.c_str(),
					selectedPage == static_cast<int>(PageId::Debug),
					{ Pixels(sections.debug.Width()), Pixels(sections.debug.Height()) }))
					result = static_cast<int>(PageId::Debug);
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		if (toggled) state.Toggle();
		if (result != -1) state.DismissOverlay();
		return result;
	}
}
