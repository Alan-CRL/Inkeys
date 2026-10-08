#include "Setting.Pages.h"
#include "imfluent/imfluent_internal.h"
#include "imgui/imgui_internal.h"
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

	namespace
	{
		LayoutRect ItemBounds()
		{
			const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
			return { minimum.x, minimum.y, maximum.x, maximum.y };
		}

		void FocusParent(ImGuiWindow* window, const NavigationState& state)
		{
			if (!window || !state.parentItemId) return;
			ImGui::FocusWindow(window);
			ImGui::SetNavID(state.parentItemId, ImGuiNavLayer_Main, window->NavRootFocusScopeId,
				ImRect(ImVec2(state.parentBounds.left - window->Pos.x, state.parentBounds.top - window->Pos.y),
					ImVec2(state.parentBounds.right - window->Pos.x, state.parentBounds.bottom - window->Pos.y)));
		}

		void FadeVertices(ImDrawList* drawList, int first, float alpha)
		{
			alpha = std::clamp(alpha, 0.0F, 1.0F);
			for (int index = first; index < drawList->VtxBuffer.Size; ++index)
			{
				auto& color = drawList->VtxBuffer[index].col;
				const auto opacity = static_cast<ImU32>(static_cast<float>(color >> IM_COL32_A_SHIFT) * alpha + 0.5F);
				color = (color & ~IM_COL32_A_MASK) | (opacity << IM_COL32_A_SHIFT);
			}
		}
	}

	NavigationAction RenderNavigationPane(const ShellGeometry& geometry,
		NavigationState& state, NavigationDestination selected)
	{
		selected = NormalizeDestination(selected);
		const auto& pane = geometry.pane;
		const float scale = Pixels(1.0F);
		const bool expanded = pane.Width() > CompactPaneWidth + 0.01F;
		const float fraction = std::clamp((pane.Width() - CompactPaneWidth) / (OpenPaneWidth - CompactPaneWidth), 0.0F, 1.0F);
		const float childReveal = state.animationsInitialized ? state.groupReveal.value
			: (state.Expanded() && state.generalExpanded ? 1.0F : 0.0F);
		const std::string debugLabel = IA(I18nKey.SettingsUI.DebugSoftware.N);
		const float debugWidth = TextWidth(debugLabel.c_str(), ImFluentTextStyle_Caption) / scale + 24.0F;
		const auto sections = ResolveNavigationPaneGeometry(pane.Width(), pane.Height(), expanded,
			ImFluent::GetStyle().NavItemHeight, debugWidth, fraction);
		NavigationAction result;
		const bool wasInlineChildFocused = state.inlineChildFocused;
		state.inlineChildFocused = false;
		ImGui::PushID("##settings-navigation-flyout-owner");
		const bool popupExists = ImGui::IsPopupOpen("##settings-general-flyout");
		ImGui::PopID();
		if (state.overlayOpen && !popupExists && !state.IsFlyoutOpen() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			state.DismissOverlay();
		if (!state.Expanded() && state.childFocusId
			&& (GImGui->NavId == state.childFocusId || (wasInlineChildFocused
				&& (GImGui->NavId == 0 || GImGui->NavId == state.childWindowItemId))))
		{
			// NewFrame 可能已按 Esc 清除焦点；只恢复上一帧实际持焦的子项，不接管正文焦点。
			state.restoreParentFocus = true; state.scrollSelectedGeneral = true;
		}
		bool toggled = false;
		bool keepOverlayForGroup = false;
		ImGuiWindow* parentWindow = nullptr;
		ImDrawList* indicatorList = nullptr;
		ImRect indicatorClip;
		LayoutRect indicatorBounds;
		bool indicatorVisible = false;
		state.parentBounds = state.chevronBounds = {};
		state.childBounds = {}; state.flyoutChildBounds = {};
		state.middleBounds = {}; state.footerBounds = {};
		if (state.mouseReleaseBarrier && !ImGui::IsMouseDown(ImGuiMouseButton_Left)
			&& !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) state.mouseReleaseBarrier = false;
		struct Entry { PageId page; const char* key; const char* glyph; };
		const std::array topEntries{
			Entry{ PageId::Home, I18nKey.SettingsUI.Home.N, "\ue80f" },
			Entry{ PageId::Language, I18nKey.SettingsUI.Language.N, "\uf2b7" }
		};
		const std::array middleEntries{
			Entry{ PageId::General, I18nKey.SettingsUI.Regular.N, "\ue713" },
			Entry{ PageId::Draw, I18nKey.SettingsUI.Draw.N, "\uee56" },
			Entry{ PageId::Plugins, I18nKey.SettingsUI.PlugIn.N, "\ue74c" },
			Entry{ PageId::Components, I18nKey.SettingsUI.Component.N, "\ue70b" },
			Entry{ PageId::Experimental, I18nKey.SettingsUI.Design.Experimental, "\uec4a" },
			Entry{ PageId::Version, I18nKey.SettingsUI.Version.N, "\ue946" }
		};
		const std::array childKeys{ I18nKey.SettingsUI.Design.GeneralBasic,
			I18nKey.SettingsUI.Design.GeneralAppearance, I18nKey.SettingsUI.Design.GeneralInteraction,
			I18nKey.SettingsUI.Design.GeneralStorage };
		const std::array childGlyphs{ "\ue713", "\ue790", "\ue72e", "\ue81e" };
		static_assert(middleEntries.size() == NavigationMiddleItemCount);
		auto navigate = [&](NavigationDestination destination, bool dismissOverlay = true) {
			destination = NormalizeDestination(destination);
			if (destination != selected) result = { NavigationActionKind::Navigate, destination };
			if (destination.page == PageId::General) state.lastGeneralSection = destination.section;
			state.CloseFlyout();
			if (dismissOverlay) state.DismissOverlay();
			else keepOverlayForGroup = true;
		};
		auto markSelected = [&](bool isSelected) {
			if (!isSelected) return;
			auto* window = ImGui::GetCurrentWindow();
			const auto bounds = ItemBounds();
			if (bounds.bottom <= window->ClipRect.Min.y || bounds.top >= window->ClipRect.Max.y) return;
			indicatorBounds = bounds;
			indicatorClip = window->ClipRect;
			indicatorList = window->DrawList;
			indicatorVisible = true;
		};
		auto navItem = [&](const char* id, const char* label, bool isSelected, const char* glyph, bool compact) {
			// 保留 ImFluent 的共享颜色动画；选中条单独移动，避免新旧位置出现两条蓝线。
			ImFluent::PushStyleColor(ImFluentCol_AccentFillDefault, IM_COL32(0, 0, 0, 0));
			const bool pressed = ImFluent::NavItemEx(id, label, isSelected, glyph, compact);
			ImFluent::PopStyleColor();
			return pressed;
		};
		auto renderEntry = [&](const Entry& entry) {
			ImGui::PushID(static_cast<int>(entry.page));
			const bool isSelected = selected.page == entry.page
				|| (entry.page == PageId::Version && selected.page == PageId::Build);
			if (navItem("##page", IA(entry.key).c_str(), isSelected, entry.glyph, !expanded))
				navigate({ entry.page });
			markSelected(isSelected);
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
		const auto childFlags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened;
		ImGui::SetCursorPos({ Pixels(pane.left), Pixels(pane.top) });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, AppBase);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0F, 0.0F });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0F, Pixels(RowGap) });
		ImGui::BeginChild("##settings-pane", { Pixels(pane.Width()), Pixels(pane.Height()) },
			ImGuiChildFlags_NavFlattened, fixedFlags);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { Pixels(4.0F), 0.0F });

		// 顶部固定区与底部固定区先测量，中段承担全部树形列表滚动。
		ImGui::SetCursorPos({ 0.0F, 0.0F });
		if (ImGui::BeginChild("##settings-nav-top", { 0.0F, Pixels(sections.top.Height()) }, childFlags, fixedFlags))
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
				setPosition(sections.topItems[index]); renderEntry(topEntries[index]);
			}
			divider(sections.separators[0]);
		}
		ImGui::EndChild();

		if (sections.middle.Height() > 0.0F)
		{
			setPosition(sections.middle);
			if (ImGui::BeginChild("##settings-nav-list", { 0.0F, Pixels(sections.middle.Height()) },
				childFlags, ImGuiWindowFlags_NoScrollbar))
			{
				parentWindow = ImGui::GetCurrentWindow();
				ImGui::PushID(static_cast<int>(PageId::General));
				const ImVec2 origin = ImGui::GetCursorScreenPos();
				const float right = parentWindow->WorkRect.Max.x;
				const float contentRight = parentWindow->ContentRegionRect.Max.x;
				if (expanded)
				{
					// 真实 NavItem 宽度在普通 child 中来自 ContentRegionRect；箭头必须单独命中。
					parentWindow->WorkRect.Max.x = contentRight - Pixels(NavigationRowHeight);
					parentWindow->ContentRegionRect.Max.x = parentWindow->WorkRect.Max.x;
				}
				const bool generalSelected = selected.page == PageId::General;
				const bool parentSelected = generalSelected && (!state.Expanded() || !state.generalExpanded || childReveal < 0.001F);
				const bool suppressTip = !expanded && state.IsFlyoutOpen();
				const bool parentPressed = navItem("##general-parent",
					suppressTip ? "" : IA(I18nKey.SettingsUI.Regular.N).c_str(), parentSelected,
					"\ue713", !expanded && !suppressTip);
				parentWindow->WorkRect.Max.x = right;
				parentWindow->ContentRegionRect.Max.x = contentRight;
				state.parentBounds = ItemBounds(); state.middleBounds[0] = state.parentBounds;
				state.parentItemId = ImGui::GetItemID();
				markSelected(parentSelected);
				const bool parentFocused = ImGui::IsItemFocused();
				if (state.restoreParentFocus && !state.IsFlyoutOpen()
					&& state.parentBounds.top < parentWindow->ClipRect.Max.y
					&& state.parentBounds.bottom > parentWindow->ClipRect.Min.y)
				{
					FocusParent(parentWindow, state); state.restoreParentFocus = false;
				}
				if (parentPressed)
				{
					if (state.Expanded())
					{
						state.generalExpanded = !state.generalExpanded;
						// 纯收起不导航，不重置当前子页及正文淡入；覆盖导航留给父行继续开合。
						if (state.generalExpanded)
						{
							state.scrollSelectedGeneral = true;
							navigate({ PageId::General, state.lastGeneralSection }, false);
						}
						else markSelected(generalSelected);
					}
					else if (generalSelected) state.flyoutOpen = !state.flyoutOpen;
					else navigate({ PageId::General, state.lastGeneralSection });
				}
				if (parentFocused && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
				{
					ImGui::NavMoveRequestCancel();
					if (state.Expanded()) { state.generalExpanded = true; state.scrollSelectedGeneral = true; }
					else state.flyoutOpen = true;
				}
				if (parentFocused && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
				{
					ImGui::NavMoveRequestCancel(); state.generalExpanded = false;
				}
				if (expanded)
				{
					ImGui::SetCursorScreenPos({ contentRight - Pixels(NavigationRowHeight), origin.y });
					ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault, IM_COL32(0, 0, 0, 0));
					ImFluent::PushStyleColor(ImFluentCol_ControlStrokeDefault, IM_COL32(0, 0, 0, 0));
					ImFluent::PushStyleColor(ImFluentCol_ControlStrokeSecondary, IM_COL32(0, 0, 0, 0));
					ImFluent::PushStyleColor(ImFluentCol_ElevationControlBottom, IM_COL32(0, 0, 0, 0));
					const bool arrowPressed = Button("##general-chevron", { Pixels(NavigationRowHeight), Pixels(NavigationRowHeight) });
					ImFluent::PopStyleColor(4);
					state.chevronBounds = ItemBounds();
					ImDrawList* dl = ImGui::GetWindowDrawList();
					const int firstVertex = dl->VtxBuffer.Size;
					const ImVec2 min(state.chevronBounds.left, state.chevronBounds.top), max(state.chevronBounds.right, state.chevronBounds.bottom);
					ImFluent::DrawIcon("\ue70d", min, max, NavigationGlyphSize, TextSecondary);
					const float angle = (childReveal - 1.0F) * 1.5707963F;
					const float cosine = std::cos(angle), sine = std::sin(angle);
					const ImVec2 center((min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F);
					for (int vertex = firstVertex; vertex < dl->VtxBuffer.Size; ++vertex)
					{
						const ImVec2 relative(dl->VtxBuffer[vertex].pos.x - center.x, dl->VtxBuffer[vertex].pos.y - center.y);
						dl->VtxBuffer[vertex].pos = { center.x + relative.x * cosine - relative.y * sine,
							center.y + relative.x * sine + relative.y * cosine };
					}
					if (arrowPressed)
					{
						state.generalExpanded = !state.generalExpanded;
						if (!state.generalExpanded) FocusParent(parentWindow, state);
						if (state.generalExpanded) state.scrollSelectedGeneral = true;
					}
				}
				ImGui::PopID();
				ImGui::SetCursorScreenPos({ origin.x, origin.y + Pixels(NavigationRowHeight + RowGap) });
				if (childReveal > 0.001F && expanded)
				{
					const float fullHeight = NavigationRowHeight * 4.0F + RowGap * 3.0F;
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0F, 0.0F });
					if (ImGui::BeginChild("##settings-general-children", { 0.0F, Pixels(fullHeight * childReveal) },
						ImGuiChildFlags_NavFlattened, fixedFlags))
					{
						ImGui::BeginDisabled(!state.generalExpanded || !state.Expanded());
						for (size_t index = 0; index < childKeys.size(); ++index)
						{
							ImGui::SetCursorPos({ Pixels(24.0F), Pixels(static_cast<float>(index) * (NavigationRowHeight + RowGap)) });
							ImGui::PushID(static_cast<int>(index));
							const bool complete = (index * (NavigationRowHeight + RowGap) + NavigationRowHeight) <= fullHeight * childReveal + 0.01F;
							ImGui::PushItemFlag(ImGuiItemFlags_NoNav, !complete);
							const bool isSelected = generalSelected && state.generalExpanded && state.Expanded()
								&& selected.section == static_cast<GeneralSection>(index);
							const bool pressed = navItem("##general-child", IA(childKeys[index]).c_str(), isSelected, childGlyphs[index], false);
							ImGui::PopItemFlag();
							state.childBounds[index] = ItemBounds();
							markSelected(isSelected);
							if (ImGui::IsItemFocused())
							{
								state.childFocusId = ImGui::GetItemID();
								state.childWindowItemId = ImGui::GetCurrentWindow()->ChildId;
								state.inlineChildFocused = true;
								if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
								{
									ImGui::NavMoveRequestCancel(); state.generalExpanded = false;
									FocusParent(parentWindow, state);
								}
							}
							if (pressed) navigate({ PageId::General, static_cast<GeneralSection>(index) });
							ImGui::PopID();
						}
						ImGui::EndDisabled();
					}
					ImGui::EndChild(); ImGui::PopStyleVar();
					// 完成展开后才滚入选中项，避免展开过程中反复改变用户滚动位置。
					if (state.scrollSelectedGeneral && generalSelected && childReveal >= 0.999F)
					{
						const auto& child = state.childBounds[static_cast<size_t>(selected.section)];
						const float childBottom = child.bottom - parentWindow->Pos.y + parentWindow->Scroll.y;
						if (child.bottom > parentWindow->ClipRect.Max.y) ImGui::SetScrollY(childBottom - parentWindow->InnerRect.GetHeight());
						else if (child.top < parentWindow->ClipRect.Min.y) ImGui::SetScrollY(child.top - parentWindow->Pos.y + parentWindow->Scroll.y);
						state.scrollSelectedGeneral = false;
					}
				}
				if (state.scrollSelectedGeneral && generalSelected && !state.Expanded() && childReveal <= 0.001F)
				{
					// 收起树后让选中的根图标可见，避免沿用子页滚动位置把菜单锚点藏到顶部之外。
					if (state.parentBounds.top < parentWindow->ClipRect.Min.y || state.parentBounds.bottom > parentWindow->ClipRect.Max.y)
						ImGui::SetScrollY(state.parentBounds.top - parentWindow->Pos.y + parentWindow->Scroll.y);
					state.scrollSelectedGeneral = false;
				}
				for (size_t index = 1; index < middleEntries.size(); ++index)
				{
					renderEntry(middleEntries[index]); state.middleBounds[index] = ItemBounds();
				}
				// 分数 DPI 下 ScrollMax 会取整，登记一像素尾部空间，保证最后一行完整进入裁剪区。
				ImGui::SetCursorScreenPos({ state.middleBounds.back().left, std::ceil(state.middleBounds.back().bottom) + 1.0F });
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0F, 0.0F });
				ImGui::Dummy({ 0.0F, 0.0F });
				ImGui::PopStyleVar();
			}
			ImGui::EndChild();
		}

		setPosition(sections.footer);
		if (ImGui::BeginChild("##settings-nav-footer", { 0.0F, Pixels(sections.footer.Height()) }, childFlags, fixedFlags))
		{
			const float footerTop = sections.footer.top;
			divider(sections.separators[1], footerTop);
			setPosition(sections.footerItems[0], footerTop);
			if (navItem("##community", IA(I18nKey.SettingsUI.Community.N).c_str(), false, "\ue716", !expanded))
				result.kind = NavigationActionKind::Community;
			state.footerBounds[0] = ItemBounds();
			setPosition(sections.footerItems[1], footerTop);
			ImGui::PushID(static_cast<int>(PageId::Support));
			if (AccentNavigationItem("##page", IA(I18nKey.SettingsUI.Sponsor.N).c_str(), selected.page == PageId::Support, "\ue789", !expanded))
				navigate({ PageId::Support });
			state.footerBounds[1] = ItemBounds(); ImGui::PopID();
			divider(sections.separators[2], footerTop);
			setPosition(sections.footerItems[2], footerTop);
			if (navItem("##restart", IA(I18nKey.SettingsUI.RestartSoftware.N).c_str(), false, "\ue72c", !expanded))
				result.kind = NavigationActionKind::Restart;
			state.footerBounds[2] = ItemBounds();
			setPosition(sections.footerItems[3], footerTop);
			ImFluent::PushStyleColor(ImFluentCol_TextPrimary, GetPalette().criticalText);
			if (navItem("##exit", IA(I18nKey.SettingsUI.ExitSoftware.N).c_str(), false, "\ue711", !expanded))
				result.kind = NavigationActionKind::Exit;
			state.footerBounds[3] = ItemBounds(); ImFluent::PopStyleColor();
			if (sections.debugVisible)
			{
				setPosition(sections.debug, footerTop); ImGui::PushID(static_cast<int>(PageId::Debug));
				const bool debugSelected = selected.page == PageId::Debug;
				if (CaptionNavigationButton("page", debugLabel.c_str(), debugSelected,
					{ Pixels(sections.debug.Width()), Pixels(sections.debug.Height()) })) navigate({ PageId::Debug });
				markSelected(debugSelected); ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar(); ImGui::EndChild();
		ImGui::PopStyleVar(2); ImGui::PopStyleColor();
		if (toggled) state.Toggle();
		if (result.kind != NavigationActionKind::None)
		{
			state.CloseFlyout();
			if (!keepOverlayForGroup || result.kind != NavigationActionKind::Navigate) state.DismissOverlay();
		}

		if (indicatorVisible)
		{
			const ImVec2 origin = ImGui::GetWindowPos();
			const float nextX = (indicatorBounds.left - origin.x) / scale;
			const float nextY = ((indicatorBounds.top + indicatorBounds.bottom) * 0.5F - origin.y) / scale;
			if (!state.indicatorInitialized)
			{
				state.indicatorX.Reset(nextX); state.indicatorY.Reset(nextY); state.indicatorInitialized = true;
			}
			state.indicatorX.Target(nextX); state.indicatorY.Target(nextY);
			if (!state.animationsEnabled) { state.indicatorX.Reset(nextX); state.indicatorY.Reset(nextY); }
			indicatorList->PushClipRect(indicatorClip.Min, indicatorClip.Max, true);
			const float x = origin.x + Pixels(state.indicatorX.value), y = origin.y + Pixels(state.indicatorY.value);
			indicatorList->AddRectFilled({ x, y - Pixels(8.0F) }, { x + Pixels(3.0F), y + Pixels(8.0F) },
				ImFluent::GetColorU32(ImFluentCol_AccentFillDefault), Pixels(1.5F));
			indicatorList->PopClipRect();
		}
		else state.indicatorInitialized = false;

		const bool anchorVisible = parentWindow && state.parentBounds.bottom > parentWindow->ClipRect.Min.y
			&& state.parentBounds.top < parentWindow->ClipRect.Max.y;
		if (!anchorVisible || state.Expanded()) state.CloseFlyout();
		// 模态浮层只用于输入屏障，透明遮罩不改变正文颜色；淡出完成后释放屏障并回焦父项。
		ImGui::PushID("##settings-navigation-flyout-owner");
		const char* popupId = "##settings-general-flyout";
		if (state.flyoutOpen && !ImGui::IsPopupOpen(popupId)) ImGui::OpenPopup(popupId);
		if (ImGui::IsPopupOpen(popupId))
		{
			const ImVec2 origin = ImGui::GetWindowPos();
			const LayoutRect client{ origin.x, origin.y + Pixels(TitleBarHeightDip),
				origin.x + Pixels(geometry.content.right), origin.y + Pixels(geometry.content.bottom) };
			const auto bounds = ResolveNavigationFlyoutGeometry(state.parentBounds, client, scale);
			const float phase = state.animationsInitialized ? state.flyoutReveal.value : 1.0F;
			const float shift = Pixels(8.0F) * (1.0F - phase);
			const float y = std::clamp(bounds.top + (bounds.top >= state.parentBounds.bottom ? shift : -shift),
				client.top + Pixels(8.0F), (std::max)(client.top + Pixels(8.0F), client.bottom - Pixels(8.0F) - bounds.Height()));
			ImGui::SetNextWindowPos({ bounds.left, y });
			ImGui::SetNextWindowSize({ bounds.Width(), bounds.Height() });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { Pixels(8.0F), Pixels(8.0F) });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Pixels(8.0F));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, Pixels(1.0F));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0F, Pixels(RowGap) });
			ImGui::PushStyleColor(ImGuiCol_PopupBg, GetPalette().cardSecondary);
			ImGui::PushStyleColor(ImGuiCol_Border, Stroke);
			ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 0));
			const auto popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
			if (ImGui::BeginPopupModal(popupId, nullptr, popupFlags))
			{
				auto* popupWindow = ImGui::GetCurrentWindow();
				state.flyoutBounds = { popupWindow->Pos.x, popupWindow->Pos.y,
					popupWindow->Pos.x + popupWindow->Size.x, popupWindow->Pos.y + popupWindow->Size.y };
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !state.flyoutBounds.Contains(mouse.x, mouse.y))
				{
					if (!state.flyoutOpen && state.parentBounds.Contains(mouse.x, mouse.y))
					{ state.flyoutOpen = true; state.restoreParentFocus = false; }
					else state.CloseFlyout();
					state.mouseReleaseBarrier = true;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
				{
					ImGui::NavMoveRequestCancel(); state.CloseFlyout();
				}
				ImGui::BeginDisabled(!state.flyoutOpen);
				for (size_t index = 0; index < childKeys.size(); ++index)
				{
					ImGui::PushID(static_cast<int>(index));
					const bool isSelected = selected.page == PageId::General && selected.section == static_cast<GeneralSection>(index);
					if (ImFluent::NavItemEx("##general-child", IA(childKeys[index]).c_str(), isSelected, childGlyphs[index], false))
						navigate({ PageId::General, static_cast<GeneralSection>(index) });
					state.flyoutChildBounds[index] = ItemBounds();
					if (isSelected) ImGui::SetItemDefaultFocus();
					ImGui::PopID();
				}
				ImGui::EndDisabled();
				FadeVertices(popupWindow->DrawList, 0, phase);
				if (!state.flyoutOpen && (!state.animationsEnabled || state.flyoutReveal.value <= 0.001F))
				{
					ImGui::CloseCurrentPopup(); state.flyoutReveal.Reset(0.0F);
					FocusParent(parentWindow, state); state.restoreParentFocus = false;
				}
				ImGui::EndPopup();
			}
			ImGui::PopStyleColor(3); ImGui::PopStyleVar(4);
		}
		else state.flyoutBounds = {};
		ImGui::PopID();
		return result;
	}

	int RenderNavigationPane(const ShellGeometry& geometry, NavigationState& state, int selectedPage)
	{
		const auto action = RenderNavigationPane(geometry, state, DestinationForPage(selectedPage));
		switch (action.kind)
		{
		case NavigationActionKind::Navigate:
			if (action.destination.page == PageId::General && action.destination.section == GeneralSection::Storage)
				return static_cast<int>(PageId::Configuration);
			if (action.destination.page == PageId::General && action.destination.section == GeneralSection::Interaction)
				return static_cast<int>(PageId::HotKeys);
			return static_cast<int>(action.destination.page);
		case NavigationActionKind::Community: return -2;
		case NavigationActionKind::Restart: return -3;
		case NavigationActionKind::Exit: return -4;
		default: return -1;
		}
	}
}
