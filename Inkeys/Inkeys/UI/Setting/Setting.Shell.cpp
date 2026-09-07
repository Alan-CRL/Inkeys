#include "Setting.Pages.h"
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
		ImGui::SetCursorPos({ Pixels(pane.left), Pixels(pane.top) });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, AppBase);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0F, 0.0F });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0F, Pixels(4.0F) });
		ImGui::BeginChild("##settings-pane", { Pixels(pane.Width()), Pixels(pane.Height()) },
			ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::SetCursorPos({ Pixels(4.0F), Pixels(8.0F) });
		const ImVec2 menuOrigin = ImGui::GetCursorScreenPos();
		ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ControlStrokeDefault, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ControlStrokeSecondary, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ElevationControlBottom, IM_COL32(0, 0, 0, 0));
		const bool toggled = Button("##pane-toggle", { Pixels(40.0F), Pixels(36.0F) });
		ImFluent::PopStyleColor(4);
		ImFluent::DrawIcon("\ue700", menuOrigin,
			{ menuOrigin.x + Pixels(40.0F), menuOrigin.y + Pixels(36.0F) }, NavigationGlyphSize, TextPrimary);
		if (state.Expanded())
			TextAt(IA(I18nKey.SettingsUI.Design.NavigationTitle).c_str(),
				{ menuOrigin.x + Pixels(44.0F), menuOrigin.y + Pixels(8.0F) },
				Pixels(pane.Width() - 56.0F), ImFluentTextStyle_BodyStrong);

		// 固定底栏与独立滚动菜单使用相同 4-DIP inset，收起时保留分组占位。
		constexpr float headerHeight = 56.0F;
		constexpr float footerHeight = 128.0F;
		const float listHeight = (std::max)(1.0F, pane.Height() - headerHeight - footerHeight);
		ImGui::SetCursorPos({ 0.0F, Pixels(headerHeight) });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { Pixels(4.0F), 0.0F });
		ImGui::BeginChild("##settings-nav-list", { 0.0F, Pixels(listHeight) },
			ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
		int result = -1;
		struct Entry { PageId page; const char* key; const char* glyph; };
		const std::array entries{
			Entry{ PageId::Home, I18nKey.SettingsUI.Home.N, "\ue80f" },
			Entry{ PageId::General, I18nKey.SettingsUI.Regular.N, "\ue713" },
			Entry{ PageId::Draw, I18nKey.SettingsUI.Draw.N, "\uedc6" },
			Entry{ PageId::Preset, I18nKey.SettingsUI.Preset.N, "\uf259" },
			Entry{ PageId::Plugins, I18nKey.SettingsUI.PlugIn.N, "\uea86" },
			Entry{ PageId::Components, I18nKey.SettingsUI.Component.N, "\ue74c" },
			Entry{ PageId::HotKeys, I18nKey.SettingsUI.HotKey.N, "\ue765" },
			Entry{ PageId::Language, I18nKey.SettingsUI.Language.N, "\ue774" },
			Entry{ PageId::Configuration, I18nKey.SettingsUI.Configuration.N, "\ue8b7" },
			Entry{ PageId::Version, I18nKey.SettingsUI.Version.N, "\ue946" },
			Entry{ PageId::Experimental, I18nKey.SettingsUI.Design.Experimental, "\uec4a" },
			Entry{ PageId::Support, I18nKey.SettingsUI.Sponsor.N, "\ue734" },
			Entry{ PageId::Debug, I18nKey.SettingsUI.DebugSoftware.N, "\ue90f" }
		};
		for (size_t index = 0; index < entries.size(); ++index)
		{
			if (index == 1 || index == 9)
			{
				const ImVec2 origin = ImGui::GetCursorScreenPos();
				if (state.Expanded())
					TextAt(IA(index == 1 ? I18nKey.SettingsUI.Design.Preferences : I18nKey.SettingsUI.Design.SupportGroup).c_str(),
						{ origin.x + Pixels(12.0F), origin.y + Pixels(6.0F) },
						Pixels(pane.Width() - 28.0F), ImFluentTextStyle_Caption, TextSecondary);
				ImGui::Dummy({ 0.0F, Pixels(24.0F) });
			}
			const auto& entry = entries[index];
			ImGui::PushID(static_cast<int>(entry.page));
			const bool selected = selectedPage == static_cast<int>(entry.page)
				|| (entry.page == PageId::Version && selectedPage == static_cast<int>(PageId::Build));
			if (ImFluent::NavItemEx("##page", IA(entry.key).c_str(), selected, entry.glyph, !state.Expanded()))
				result = static_cast<int>(entry.page);
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::SetCursorPos({ 0.0F, Pixels(headerHeight + listHeight) });
		ImGui::BeginChild("##settings-nav-footer", { 0.0F, Pixels(footerHeight) },
			ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::Dummy({ 0.0F, Pixels(4.0F) });
		if (ImFluent::NavItemEx("##community", IA(I18nKey.SettingsUI.Community.N).c_str(), false, "\ue716", !state.Expanded())) result = -2;
		if (ImFluent::NavItemEx("##restart", IA(I18nKey.SettingsUI.RestartSoftware.N).c_str(), false, "\ue72c", !state.Expanded())) result = -3;
		if (ImFluent::NavItemEx("##exit", IA(I18nKey.SettingsUI.ExitSoftware.N).c_str(), false, "\ue711", !state.Expanded())) result = -4;
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
