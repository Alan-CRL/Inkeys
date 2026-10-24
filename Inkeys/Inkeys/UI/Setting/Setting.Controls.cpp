#include "Setting.Controls.h"
#include "imfluent/imfluent_internal.h"
#include "Setting.Theme.h"
#include "imgui/imgui_internal.h"
#include <cstring>
#include <string_view>
#include <vector>

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
		constexpr Palette lightPalette{
			.appBase = IM_COL32(243, 243, 243, 255),
			.pageSurface = IM_COL32(250, 250, 250, 255),
			.card = IM_COL32(255, 255, 255, 255),
			.cardSecondary = IM_COL32(250, 250, 250, 255),
			.stroke = IM_COL32(229, 229, 229, 255),
			.textPrimary = IM_COL32(32, 32, 32, 255),
			.textSecondary = IM_COL32(97, 97, 97, 255),
			.textDisabled = IM_COL32(154, 154, 154, 255),
			.accent = IM_COL32(0, 103, 192, 255),
			.criticalText = IM_COL32(196, 43, 28, 255),
			.subtleHover = IM_COL32(231, 231, 231, 255),
			.subtlePressed = IM_COL32(224, 224, 224, 255),
			.rowHover = IM_COL32(0, 0, 0, 5),
			.rowPressed = IM_COL32(0, 0, 0, 10),
			.selection = IM_COL32(0, 103, 192, 56),
			.scrollbar = IM_COL32(96, 96, 96, 104),
			.scrollbarHovered = IM_COL32(96, 96, 96, 150),
			.scrollbarActive = IM_COL32(80, 80, 80, 184),
			.notice = { IM_COL32(241, 246, 251, 255), IM_COL32(235, 246, 238, 255),
				IM_COL32(255, 248, 223, 255), IM_COL32(253, 239, 237, 255) },
			.heroFill = IM_COL32(234, 242, 249, 255),
			.heroStroke = IM_COL32(220, 232, 241, 255),
			.heroEyebrow = IM_COL32(73, 101, 120, 255),
			.heroDescription = IM_COL32(78, 96, 112, 255),
			.heroMint = IM_COL32(221, 242, 239, 255),
			.heroLavender = IM_COL32(238, 232, 249, 255),
			.artCool = IM_COL32(220, 234, 247, 255),
			.artWarm = IM_COL32(244, 232, 220, 255),
			.artStroke = IM_COL32(197, 216, 231, 255),
			.artRule = IM_COL32(189, 204, 214, 255),
			.artAxis = IM_COL32(141, 168, 189, 255),
			.artWarmInk = IM_COL32(191, 148, 107, 255),
			.artPenSelection = IM_COL32(226, 240, 252, 255),
			.artOrange = IM_COL32(210, 154, 101, 255),
			.artGreen = IM_COL32(119, 163, 142, 255),
			.featureTint = { IM_COL32(242, 246, 250, 255), IM_COL32(247, 242, 237, 255), IM_COL32(240, 244, 243, 255) },
			.featureInk = { IM_COL32(69, 104, 131, 255), IM_COL32(156, 113, 83, 255), IM_COL32(84, 116, 107, 255) },
			.avatarFill = IM_COL32(240, 240, 240, 255),
		};
		constexpr Palette darkPalette{
			.appBase = IM_COL32(32, 32, 32, 255),
			.pageSurface = IM_COL32(39, 39, 39, 255),
			.card = IM_COL32(45, 45, 45, 255),
			.cardSecondary = IM_COL32(50, 50, 50, 255),
			.stroke = IM_COL32(61, 61, 61, 255),
			.textPrimary = IM_COL32(242, 242, 242, 255),
			.textSecondary = IM_COL32(190, 190, 190, 255),
			.textDisabled = IM_COL32(120, 120, 120, 255),
			.accent = IM_COL32(96, 205, 255, 255),
			.criticalText = IM_COL32(255, 153, 164, 255),
			.subtleHover = IM_COL32(53, 53, 53, 255),
			.subtlePressed = IM_COL32(47, 47, 47, 255),
			.rowHover = IM_COL32(255, 255, 255, 8),
			.rowPressed = IM_COL32(255, 255, 255, 4),
			.selection = IM_COL32(0, 75, 112, 255),
			.scrollbar = IM_COL32(180, 180, 180, 140),
			.scrollbarHovered = IM_COL32(200, 200, 200, 180),
			.scrollbarActive = IM_COL32(222, 222, 222, 210),
			.notice = { IM_COL32(38, 51, 62, 255), IM_COL32(37, 55, 43, 255),
				IM_COL32(64, 56, 31, 255), IM_COL32(68, 42, 43, 255) },
			.heroFill = IM_COL32(29, 44, 58, 255),
			.heroStroke = IM_COL32(48, 66, 82, 255),
			.heroEyebrow = IM_COL32(159, 196, 222, 255),
			.heroDescription = IM_COL32(188, 206, 220, 255),
			.heroMint = IM_COL32(29, 59, 62, 255),
			.heroLavender = IM_COL32(47, 43, 66, 255),
			.artCool = IM_COL32(38, 62, 81, 255),
			.artWarm = IM_COL32(70, 57, 45, 255),
			.artStroke = IM_COL32(74, 98, 117, 255),
			.artRule = IM_COL32(86, 110, 130, 255),
			.artAxis = IM_COL32(119, 150, 172, 255),
			.artWarmInk = IM_COL32(210, 171, 133, 255),
			.artPenSelection = IM_COL32(42, 72, 94, 255),
			.artOrange = IM_COL32(222, 170, 121, 255),
			.artGreen = IM_COL32(146, 188, 168, 255),
			.featureTint = { IM_COL32(40, 53, 66, 255), IM_COL32(59, 49, 42, 255), IM_COL32(40, 55, 50, 255) },
			.featureInk = { IM_COL32(157, 195, 222, 255), IM_COL32(219, 181, 147, 255), IM_COL32(158, 199, 180, 255) },
			.avatarFill = IM_COL32(58, 58, 58, 255),
		};
		Palette activePalette = lightPalette;
		bool activeDarkMode = false;

		float rowAnimationDelta = 0.0F, rowAnimationRate = 1.0F;
		bool rowAnimationsEnabled = false;

		std::string stateOn = "On";
		std::string stateOff = "Off";

		class ControlFontScope
		{
		public:
			ControlFontScope() { ImFluent::PushFont(ImFluentTextStyle_Body, ControlText.size); }
			~ControlFontScope() { ImFluent::PopFont(); }
			ControlFontScope(const ControlFontScope&) = delete;
			ControlFontScope& operator=(const ControlFontScope&) = delete;
		};

		ImVec2 ResolveButtonSize(const char* label, const ImVec2& size)
		{
			// 默认宽度复用行布局的自然测量；显式尺寸及库的自动高度保持原语义。
			return { size.x == 0.0F ? Pixels(ButtonWidth(label)) : size.x, size.y };
		}

		void AdvanceGap(float desiredDip)
		{
			// 用布局项登记空白，空分组后直接结束 child 时同样保持 ImGui 边界合法。
			const float extra = Pixels(desiredDip) - ImGui::GetStyle().ItemSpacing.y * 2.0F;
			ImGui::Dummy({ 0.0F, (std::max)(0.0F, extra) });
		}

		TypographyMetrics Metrics(ImFluentTextStyle style)
		{
			switch (style)
			{
			case ImFluentTextStyle_Caption: return CaptionText;
			case ImFluentTextStyle_Title: return TitleText;
			case ImFluentTextStyle_Subtitle: return { 20.0F, 28.0F };
			default: return BodyText;
			}
		}

		std::vector<std::string_view> Lines(const char* text, float widthPixels)
		{
			std::vector<std::string_view> lines;
			if (!text || !*text) return lines;
			const char* cursor = text;
			const char* end = text + std::strlen(text);
			while (cursor < end)
			{
				const char* paragraphEnd = (std::find)(cursor, end, '\n');
				const char* lineEnd = ImGui::GetFont()->CalcWordWrapPosition(
					ImGui::GetFontSize(), cursor, paragraphEnd, (std::max)(1.0F, widthPixels));
				if (lineEnd == cursor && cursor < paragraphEnd)
				{
					// 最窄可用宽度下仍按完整 UTF-8 字符前进，避免拆出半个汉字。
					unsigned int codepoint;
					lineEnd += (std::max)(1, ImTextCharFromUtf8(&codepoint, cursor, paragraphEnd));
				}
				lines.emplace_back(cursor, lineEnd - cursor);
				cursor = lineEnd;
				if (cursor == paragraphEnd && cursor < end) ++cursor;
				else while (cursor < end && (*cursor == ' ' || *cursor == '\t')) ++cursor;
			}
			return lines;
		}
	}

	const ImU32& AppBase = activePalette.appBase;
	const ImU32& PageSurface = activePalette.pageSurface;
	const ImU32& Card = activePalette.card;
	const ImU32& Stroke = activePalette.stroke;
	const ImU32& TextPrimary = activePalette.textPrimary;
	const ImU32& TextSecondary = activePalette.textSecondary;
	const ImU32& Accent = activePalette.accent;

	const Palette& PaletteForTheme(bool darkMode) noexcept { return darkMode ? darkPalette : lightPalette; }
	const Palette& GetPalette() noexcept { return activePalette; }
	bool IsDarkMode() noexcept { return activeDarkMode; }

	float Pixels(float dip) { return dip * ImGui::GetStyle().FontScaleDpi; }

	float TextHeight(const char* text, float widthPixels, ImFluentTextStyle style)
	{
		ImFluent::PushFont(style);
		const float height = static_cast<float>(Lines(text, widthPixels).size()) * Pixels(Metrics(style).lineHeight);
		ImFluent::PopFont();
		return height;
	}

	float TextWidth(const char* text, ImFluentTextStyle style)
	{
		ImFluent::PushFont(style);
		const float width = ImGui::CalcTextSize(text ? text : "").x;
		ImFluent::PopFont();
		return width;
	}

	void TextAt(const char* text, ImVec2 position, float widthPixels,
		ImFluentTextStyle style, ImU32 color)
	{
		ImFluent::PushFont(style);
		const auto lines = Lines(text, widthPixels);
		const float lineHeight = Pixels(Metrics(style).lineHeight);
		position.y += (lineHeight - ImGui::GetFontSize()) * 0.5F;
		for (const auto line : lines)
		{
			ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
				{ std::round(position.x), std::round(position.y) }, color,
				line.data(), line.data() + line.size());
			position.y += lineHeight;
		}
		ImFluent::PopFont();
	}

	void Text(const char* text, ImFluentTextStyle style, ImU32 color)
	{
		const ImVec2 position = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = TextHeight(text, width, style);
		TextAt(text, position, width, style, color);
		ImGui::Dummy({ width, height });
	}

	namespace
	{
		bool DragEligible(ImGuiWindow* window, const NavigationState& state, bool submitted)
		{
			if (!window || (submitted ? !window->Active : !window->WasActive) || window->Hidden
				|| window->ScrollMax.y <= 0.0F
				|| (window->Flags & (ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_Tooltip))) return false;
			for (auto* ancestor = window; ancestor; ancestor = ancestor->ParentWindow)
				if ((ancestor->Flags & ImGuiWindowFlags_Popup)
					&& !ImGui::IsPopupOpen(ancestor->PopupId, ImGuiPopupFlags_AnyPopupLevel)) return false;
			if (window->ID == state.flyoutWindowId) return state.flyoutOpen && !state.mouseReleaseBarrier;
			return !state.BlockContentInput() || (window->ID == state.navigationScrollWindowId && !state.mouseReleaseBarrier);
		}

		ImGuiID WindowStateId(ImGuiWindow* window, const char* label)
		{
			// 未提交页面经 ImGui 内存回收后 IDStack 为空；窗口状态始终以稳定窗口 ID 为种子。
			return ImHashStr(label, 0, window->ID);
		}

		void CaptureDrag(ImGuiWindow* owner)
		{
			ImGuiContext& g = *GImGui;
			ImGui::SetActiveID(WindowStateId(owner, "##scrolldraggingoverlay"), owner);
			g.ActiveIdMouseButton = ImGuiMouseButton_Left;
			g.MovingWindow = nullptr;
			owner->StateStorage.SetFloat(WindowStateId(owner, "##drag-scroll-position"), owner->Scroll.y);
		}

		ImGuiWindow* ChildWindow(const char* id)
		{
			auto* parent = ImGui::GetCurrentWindow();
			char name[512];
			ImFormatString(name, sizeof(name), "%s/%s_%08X", parent->Name, id, parent->GetID(id));
			return ImGui::FindWindowByName(name);
		}

		// 每个 ImGui window 的 storage 随 session 释放，快速反向不复用旧端点。
		class ScrollbarStyleScope
		{
			ImGuiWindow* window;
			float phase, previousSize, previousPadding;
			bool settled;
			ImVec4 previousColors[3];
		public:
			explicit ScrollbarStyleScope(ImGuiWindow* existing, bool input = true, bool settleLayout = false)
				: window(existing), phase(0.0F), settled(settleLayout)
			{
				previousSize = ImGui::GetStyle().ScrollbarSize;
				previousPadding = ImGui::GetStyle().ScrollbarPadding;
				for (int index = 0; index < 3; ++index) previousColors[index] = ImGui::GetStyle().Colors[ImGuiCol_ScrollbarGrab + index];
				// 只在本帧真正提交此宿主时消费；未提交/关闭的旧 Popup 不留下 target。
				if (input && window && GImGui->ActiveIdWindow == window && GImGui->ActiveId == WindowStateId(window, "##scrolldraggingoverlay")
					&& window->StateStorage.GetInt(WindowStateId(window, "##drag-scroll-frame"), -2) == GImGui->FrameCount)
				{
					if (window->ScrollTarget.y == FLT_MAX)
						ImGui::SetScrollY(window, window->StateStorage.GetFloat(WindowStateId(window, "##drag-scroll-position")));
					else window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-rebase"), 1);
					window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-frame"), -2);
				}
				if (window)
				{
					auto& storage = window->StateStorage;
					NavigationTween tween(storage.GetFloat(WindowStateId(window, "##scrollbar-value")));
					tween.target = storage.GetFloat(WindowStateId(window, "##scrollbar-target"));
					tween.start = storage.GetFloat(WindowStateId(window, "##scrollbar-start"));
					tween.progress = storage.GetFloat(WindowStateId(window, "##scrollbar-progress"));
					const int last = storage.GetInt(WindowStateId(window, "##scrollbar-frame"), -2);
					// 关闭/未提交后重开从不可见状态开始，不复用旧条的退出装饰。
					if (settled) tween.Reset(1.0F);
					else if (last != GImGui->FrameCount && last != GImGui->FrameCount - 1) tween.Reset(0.0F);
					if (!settled && last != GImGui->FrameCount)
						tween.Advance(last == GImGui->FrameCount - 1 ? rowAnimationDelta : 0.0F, 0.160F / rowAnimationRate, rowAnimationsEnabled);
					phase = tween.value;
					storage.SetFloat(WindowStateId(window, "##scrollbar-value"), tween.value);
					storage.SetFloat(WindowStateId(window, "##scrollbar-target"), tween.target);
					storage.SetFloat(WindowStateId(window, "##scrollbar-start"), tween.start);
					storage.SetFloat(WindowStateId(window, "##scrollbar-progress"), tween.progress);
				}
				// 关闭动画时首帧也使用完整 native 轨道；开启时由零宽开始。
				// 入场截图及恢复帧直接使用真实终态轨道，不能把缺席后的零宽补间截进缓存。
				if (settled || !rowAnimationsEnabled) phase = 1.0F;
				ImGui::GetStyle().ScrollbarSize = Pixels(ScrollbarTrackSize) * phase;
				ImGui::GetStyle().ScrollbarPadding = Pixels(ScrollbarPadding) * phase;
				for (auto color : { ImGuiCol_ScrollbarGrab, ImGuiCol_ScrollbarGrabHovered, ImGuiCol_ScrollbarGrabActive })
				{
					ImVec4 value = ImGui::GetStyle().Colors[color]; value.w *= phase;
					ImGui::GetStyle().Colors[color] = value;
				}
			}
			void Finish(ImGuiWindow* actual)
			{
				if (!actual || !actual->Active || actual->SkipItems) return;
				window = actual;
				auto& storage = window->StateStorage;
				const float target = window->ScrollbarY ? 1.0F : 0.0F;
				const float oldTarget = storage.GetFloat(WindowStateId(window, "##scrollbar-target"));
				if (target != oldTarget)
				{
					storage.SetFloat(WindowStateId(window, "##scrollbar-start"), phase);
					storage.SetFloat(WindowStateId(window, "##scrollbar-progress"), 0.0F);
				}
				storage.SetFloat(WindowStateId(window, "##scrollbar-target"), target);
				storage.SetInt(WindowStateId(window, "##scrollbar-frame"), GImGui->FrameCount);
				if (settled)
				{
					// 无溢出仍由 native 决定不显示；清掉退出装饰，嵌套 child 继承本帧稳定策略。
					storage.SetFloat(WindowStateId(window, "##scrollbar-start"), target);
					storage.SetFloat(WindowStateId(window, "##scrollbar-progress"), 1.0F);
				}
				storage.SetInt(WindowStateId(window, "##scrollbar-settled-frame"), settled ? GImGui->FrameCount : -2);
				if (settled || !rowAnimationsEnabled) phase = target;
				storage.SetFloat(WindowStateId(window, "##scrollbar-value"), phase);
				if (window->ScrollbarY && phase > 0.0F)
				{
					// 与本库 ScrollbarEx 的几何保持一致；只缓存退出装饰，不扫描正文顶点。
					ImRect rail = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);
					const float padding = ImTrunc((std::min)(ImGui::GetStyle().ScrollbarPadding, (std::min)(rail.GetWidth(), rail.GetHeight()) * 0.5F));
					rail.Expand(-padding);
					const float visible = static_cast<float>(static_cast<ImS64>(window->InnerRect.GetHeight()));
					const float total = static_cast<float>(static_cast<ImS64>(window->ContentSize.y + window->WindowPadding.y * 2.0F));
					const float height = static_cast<float>(static_cast<int>(std::clamp(rail.GetHeight() * visible / (std::max)(1.0F, (std::max)(visible, total)),
						(std::min)(rail.GetHeight(), ImGui::GetStyle().GrabMinSize), rail.GetHeight())));
					const float ratio = std::clamp(static_cast<float>(static_cast<ImS64>(window->Scroll.y)) / (std::max)(1.0F, total - visible), 0.0F, 1.0F);
					const float top = rail.Min.y + ratio * (rail.GetHeight() - height);
					storage.SetFloat(WindowStateId(window, "##scrollbar-thumb-top"), (top - window->Pos.y) / Pixels(1.0F));
					storage.SetFloat(WindowStateId(window, "##scrollbar-thumb-bottom"), (top + height - window->Pos.y) / Pixels(1.0F));
					const ImGuiID scrollbarId = ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y);
					storage.SetInt(WindowStateId(window, "##scrollbar-thumb-state"), GImGui->ActiveId == scrollbarId ? 2 : GImGui->HoveredId == scrollbarId ? 1 : 0);
				}
				else if (!window->ScrollbarY && phase > 0.0001F)
				{
					const float reserve = Pixels(ScrollbarTrackSize) * phase;
					window->ScrollbarSizes.x = reserve;
					window->DecoOuterSizeX2 += reserve;
					window->InnerRect.Max.x -= reserve;
					if (window->ContentSizeExplicit.x == 0.0F)
					{
						window->WorkRect.Max.x -= reserve;
						window->ParentWorkRect.Max.x -= reserve;
						window->ContentRegionRect.Max.x -= reserve;
					}
					window->InnerClipRect.Max.x = (std::min)(window->InnerClipRect.Max.x,
						ImFloor(window->InnerRect.Max.x - window->WindowBorderSize * 0.5F));
					const float top = window->Pos.y + Pixels(storage.GetFloat(WindowStateId(window, "##scrollbar-thumb-top")));
					const float bottom = (std::min)(window->Pos.y + window->Size.y,
						window->Pos.y + Pixels(storage.GetFloat(WindowStateId(window, "##scrollbar-thumb-bottom"))));
					window->DrawList->PushClipRect(window->OuterRectClipped.Min, window->OuterRectClipped.Max, false);
					if (bottom > top && reserve > 0.0F)
						window->DrawList->AddRectFilled({ window->InnerRect.Max.x + ImTrunc(Pixels(ScrollbarPadding) * phase), top },
							{ window->Pos.x + window->Size.x - ImTrunc(Pixels(ScrollbarPadding) * phase), bottom },
							ImGui::GetColorU32(static_cast<ImGuiCol>(ImGuiCol_ScrollbarGrab + storage.GetInt(WindowStateId(window, "##scrollbar-thumb-state")))), Pixels(2.0F));
					window->DrawList->PopClipRect();
					// Begin 已压入 inner clip，内容绘制与真正 item 可用宽一起过渡。
					if (GImGui->CurrentWindow == window)
					{
						ImGui::PopClipRect(); ImGui::PushClipRect(window->InnerClipRect.Min, window->InnerClipRect.Max, true);
					}
				}
			}
			~ScrollbarStyleScope()
			{
				// Card/Expander 持有自己的 padding/bg 栈；独立滚动字段直接恢复，保持其 LIFO。
				ImGui::GetStyle().ScrollbarSize = previousSize;
				ImGui::GetStyle().ScrollbarPadding = previousPadding;
				for (int index = 0; index < 3; ++index) ImGui::GetStyle().Colors[ImGuiCol_ScrollbarGrab + index] = previousColors[index];
			}
		};
	}

	void AllowContentDrag()
	{
		// 仅登记内容型点击项；越过原生阈值才转抓，滑块/输入等保持原操作。
		auto* window = ImGui::GetCurrentWindow();
		if (GImGui->ActiveId == GImGui->LastItemData.ID)
			window->StateStorage.SetInt(ImHashStr("##content-drag-button", 0, window->ID), static_cast<int>(GImGui->ActiveId));
	}

	void BeginDragScrolling(const NavigationState& state)
	{
		ImGuiContext& g = *GImGui;
		auto* owner = g.ActiveIdWindow;
		if (!g.IO.MouseDown[0] || !g.IO.MouseDownOwned[0]) return;
		if (owner && g.ActiveId && g.ActiveId == static_cast<ImGuiID>(owner->StateStorage.GetInt(ImHashStr("##content-drag-button", 0, owner->ID)))
			&& ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			while (owner && !DragEligible(owner, state, false))
			{
				if (owner->Flags & ImGuiWindowFlags_Popup) { owner = nullptr; break; }
				owner = owner->ParentWindow;
			}
			if (owner) CaptureDrag(owner);
		}
		if (!DragEligible(owner, state, false) || g.ActiveId != WindowStateId(owner, "##scrolldraggingoverlay")) return;
		ImGui::KeepAliveID(g.ActiveId);
		if (g.IO.MouseDelta.y == 0.0F) return;
		const ImGuiID sampledId = WindowStateId(owner, "##drag-scroll-sampled-frame");
		if (owner->StateStorage.GetInt(sampledId, -2) == g.FrameCount) return;
		owner->StateStorage.SetInt(sampledId, g.FrameCount);
		// 原生滚轮/键盘已有目标时先让它消费，本帧不覆盖它，下一帧从真实 Scroll 重基准。
		const ImGuiID rebaseId = WindowStateId(owner, "##drag-scroll-rebase");
		if (owner->ScrollTarget.y < FLT_MAX) { owner->StateStorage.SetInt(rebaseId, 1); return; }
		const ImGuiID positionId = WindowStateId(owner, "##drag-scroll-position");
		float position = owner->StateStorage.GetFloat(positionId, owner->Scroll.y);
		if (owner->StateStorage.GetInt(rebaseId) || std::abs((std::min)(ImRound64(position), owner->ScrollMax.y) - owner->Scroll.y) > 0.01F) position = owner->Scroll.y;
		owner->StateStorage.SetInt(rebaseId, 0);
		position = std::clamp(position - g.IO.MouseDelta.y, 0.0F, owner->ScrollMax.y);
		owner->StateStorage.SetFloat(positionId, position);
		// 暂存单宿主意图；实际首次 Begin 前投递原生目标，不后移光标或顶点。
		owner->StateStorage.SetInt(WindowStateId(owner, "##drag-scroll-frame"), g.FrameCount);
	}

	void ApplyDragScrolling(const NavigationState& state)
	{
		ImGuiContext& g = *GImGui;
		auto* captured = g.ActiveIdWindow;
		if (captured && g.ActiveId == WindowStateId(captured, "##scrolldraggingoverlay"))
		{
			if (!g.IO.MouseDown[0] || !DragEligible(captured, state, true)) ImGui::ClearActiveID();
			else ImGui::KeepAliveID(g.ActiveId);
			return;
		}
		if (!g.IO.MouseClicked[0] || !g.IO.MouseDownOwned[0] || !g.HoveredWindow) return;
		// 弹层外的按下留给 ImGui 原生关闭流程，不能先取得滚动捕获而阻断 EndFrame 的焦点处理。
		if (!ImGui::IsWindowContentHoverable(g.HoveredWindow, ImGuiHoveredFlags_None)) return;
		// 真正业务控件的 ActiveId 保留；child/root 的 MoveId 属于可抓取内容空白。
		if (g.ActiveId && g.ActiveId != g.HoveredWindow->MoveId
			&& g.ActiveId != g.HoveredWindow->RootWindow->MoveId) return;
		auto* owner = g.HoveredWindow;
		while (owner && !DragEligible(owner, state, true))
		{
			// 无滚动范围的浮层不向其背景页面传播拖动。
			if (owner->Flags & ImGuiWindowFlags_Popup) { owner = nullptr; break; }
			owner = owner->ParentWindow;
		}
		if (owner) CaptureDrag(owner);
	}

	bool BeginScrollChild(const char* id, const ImVec2& size, ImGuiChildFlags childFlags, ImGuiWindowFlags flags, bool resetToTop, bool settleScrollbarLayout)
	{
		auto* existing=ChildWindow(id);
		if (resetToTop && existing)
		{
			// 必须在 Begin 消费旧惯性目标之前归零，第一帧布局直接使用顶部坐标。
			existing->Scroll.y=0; existing->ScrollTarget.y=FLT_MAX;
			existing->StateStorage.SetFloat(WindowStateId(existing, "##drag-scroll-position"),0);
			existing->StateStorage.SetInt(WindowStateId(existing, "##drag-scroll-frame"),-2);
			existing->StateStorage.SetInt(WindowStateId(existing, "##drag-scroll-sampled-frame"),-2);
			existing->StateStorage.SetInt(WindowStateId(existing, "##drag-scroll-rebase"),1);
		}
		if (resetToTop) ImGui::SetNextWindowScroll({0,0});
		auto* parent = ImGui::GetCurrentWindow();
		settleScrollbarLayout |= parent->StateStorage.GetInt(WindowStateId(parent, "##scrollbar-settled-frame"), -2) == GImGui->FrameCount;
		ScrollbarStyleScope scrollbar(existing, !(flags & ImGuiWindowFlags_NoMouseInputs), settleScrollbarLayout);
		const bool visible = ImGui::BeginChild(id, size, childFlags, flags);
		scrollbar.Finish(ImGui::GetCurrentWindow());
		return visible;
	}

	bool BeginScrollPopupModal(const char* id, ImGuiWindowFlags flags)
	{
		const ImGuiID popupId = ImGui::GetID(id);
		ImGuiWindow* window = nullptr;
		for (const auto& popup : GImGui->OpenPopupStack) if (popup.PopupId == popupId) window = popup.Window;
		ScrollbarStyleScope scrollbar(window);
		const bool visible = ImGui::BeginPopupModal(id, nullptr, flags);
		if (visible) scrollbar.Finish(ImGui::GetCurrentWindow());
		return visible;
	}

	void RenderEdidCard(const char* text)
	{
		const float width = ImGui::GetContentRegionAvail().x;
		const float padding = Pixels(ImFluent::GetStyle().CardPadding);
		const float textWidth = (std::max)(1.0F, width - padding * 2.0F);
		const float textHeight = TextHeight(text, textWidth);
		const float height = std::ceil(textHeight + padding * 2.0F);
		// 显式约束当前排版的完整高度，避免 AutoResizeY 使用上一帧测量。
		ImGui::SetNextWindowSizeConstraints({ 0.0F, height }, { FLT_MAX, height });
		ImGui::SetNextWindowContentSize({ 0.0F, textHeight });
		if (BeginCard("##display-edid", { width, height })) Text(text);
		EndCard();
	}

	void PageHeader(const char* title, const char* description)
	{
		Text(title, ImFluentTextStyle_Title);
		if (description && *description)
			Text(description, ImFluentTextStyle_Body, TextSecondary);
	}

	void SectionHeader(const char* title)
	{
		AdvanceGap(SectionGap);
		Text(title, ImFluentTextStyle_BodyStrong);
		AdvanceGap(8.0F);
	}

	void PageContentStart()
	{
		AdvanceGap(24.0F);
	}

	float ControlTextWidth(const char* text)
	{
		ControlFontScope font;
		return ImGui::CalcTextSize(text ? text : "", nullptr, true).x;
	}

	float ButtonWidth(const char* label)
	{
		return (std::max)(72.0F, ControlTextWidth(label) / Pixels(1.0F) + 32.0F);
	}

	void ConfigureRowAnimations(float delta, bool enabled, float rate)
	{
		rowAnimationDelta = std::isfinite(delta) ? std::clamp(delta, 0.0F, 0.25F) : 0.0F;
		rowAnimationRate = std::isfinite(rate) ? std::clamp(rate, 0.1F, 5.0F) : 1.0F;
		rowAnimationsEnabled = enabled;
	}

	void SettingRow(const char* id, const char* title, const char* description,
		const char* glyph, float actionWidthDip, float actionHeightDip,
		const std::function<void(const LayoutRect&)>& action, ImU32 fill, ImU32 border, ImTextureID iconTexture)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const auto measure = MeasureRow(width / Pixels(1.0F), actionWidthDip,
			actionHeightDip, iconTexture != 0 || (glyph && *glyph));
		// Tween 保存在当前 ImGui 窗口的稳定行 ID 下，随会话销毁，不残留跨 context 指针。
		ImGui::PushID(id);
		auto* storage = ImGui::GetStateStorage();
		const auto valueId = ImGui::GetID("##description-value");
		const auto targetId = ImGui::GetID("##description-target");
		const auto startId = ImGui::GetID("##description-start");
		const auto progressId = ImGui::GetID("##description-progress");
		const auto frameId = ImGui::GetID("##description-frame");
		NavigationTween tween(storage->GetFloat(valueId));
		tween.target = storage->GetFloat(targetId);
		tween.start = storage->GetFloat(startId);
		tween.progress = storage->GetFloat(progressId);
		const int frame = ImGui::GetFrameCount(), previousFrame = storage->GetInt(frameId, -2);
		const bool hasDescription = description && *description;
		const float threshold = DescriptionMinimumWidth
			+ (tween.target > 0.5F ? DescriptionWidthHysteresis : 0.0F);
		const bool below = hasDescription && !measure.stacked && measure.actionWidth > 0.0F
			&& measure.textWidth < threshold;
		if (previousFrame < 0 || measure.stacked || !hasDescription) tween.Reset(below ? 1.0F : 0.0F);
		tween.Target(below ? 1.0F : 0.0F);
		// 页切换/隐藏期间不计入动画时长；同一帧重复测量不会推进两次。
		tween.Advance(previousFrame == frame - 1 ? rowAnimationDelta : 0.0F,
			0.160F / rowAnimationRate, rowAnimationsEnabled);
		storage->SetFloat(valueId, tween.value); storage->SetFloat(targetId, tween.target);
		storage->SetFloat(startId, tween.start); storage->SetFloat(progressId, tween.progress);
		storage->SetInt(frameId, frame);
		ImGui::PopID();
		const float titleHeight = TextHeight(title, Pixels(measure.textWidth));
		const float inlineDescriptionHeight = TextHeight(description, Pixels(measure.textWidth), ImFluentTextStyle_Caption);
		const float descriptionHeight = TextHeight(description,
			Pixels(DescriptionWidth(measure, tween.value)), ImFluentTextStyle_Caption);
		const auto row = ResolveMultilineRowLayout(measure, titleHeight / Pixels(1.0F),
			descriptionHeight / Pixels(1.0F), inlineDescriptionHeight / Pixels(1.0F), tween.value);
		// 整行占位同时登记尾部间隙，最后一行后直接 EndChild 也不会越过内容边界。
		ImGui::Dummy({ width, Pixels(row.height + RowGap) });
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 bottom(origin.x + width, origin.y + Pixels(row.height));
		draw->AddRectFilled(origin, bottom, fill, Pixels(4.0F));
		draw->AddRect(origin, bottom, border, Pixels(4.0F));
		TextAt(title, { origin.x + Pixels(row.title.left), origin.y + Pixels(row.title.top) },
			Pixels(row.title.Width()));
		TextAt(description, { origin.x + Pixels(row.description.left), origin.y + Pixels(row.description.top) },
			Pixels(row.description.Width()), ImFluentTextStyle_Caption, TextSecondary);
		// PNG 复用原图标槽位和会话纹理，不随浅深主题染色。
		if (iconTexture)
			draw->AddImage(iconTexture,
				{ origin.x + Pixels(row.icon.left), origin.y + Pixels(row.icon.top) },
				{ origin.x + Pixels(row.icon.right), origin.y + Pixels(row.icon.bottom) });
		else if (measure.hasIcon)
			ImFluent::DrawIcon(glyph,
				{ origin.x + Pixels(row.icon.left), origin.y + Pixels(row.icon.top) },
				{ origin.x + Pixels(row.icon.right), origin.y + Pixels(row.icon.bottom) }, RowGlyphSize, TextPrimary);
		const LayoutRect actionPixels{ origin.x + Pixels(row.action.left), origin.y + Pixels(row.action.top),
			origin.x + Pixels(row.action.right), origin.y + Pixels(row.action.bottom) };
		ImGui::PushID(id);
		ImGui::SetCursorScreenPos({ actionPixels.left, actionPixels.top });
		ImGui::PushItemWidth((std::max)(1.0F, actionPixels.Width()));
		action(actionPixels);
		ImGui::PopItemWidth();
		ImGui::PopID();
		// ItemSize 会截到物理像素；结束游标使用同一取整，避免分数 DPI 多出半像素。
		ImGui::SetCursorScreenPos({ origin.x, std::floor(bottom.y + Pixels(RowGap)) });
	}

	bool ToggleAction(const LayoutRect& bounds, bool& value)
	{
		// 状态文本由页面传递到行左侧时也能保持开关末端在同一条轴上。
		ImGui::SetCursorScreenPos({ bounds.right - Pixels(40.0F), bounds.top + (bounds.Height() - Pixels(20.0F)) * 0.5F });
		return ToggleSwitch("##toggle", &value);
	}

	bool SliderAction(const LayoutRect& bounds, float& value, float minimum,
		float maximum, bool& active)
	{
		char percentage[24];
		ImFormatString(percentage, IM_ARRAYSIZE(percentage), "%d%%", static_cast<int>(std::round(value * 100.0F)));
		const float numberWidth = Pixels(40.0F);
		TextAt(percentage, { bounds.left, bounds.top + Pixels(6.0F) }, numberWidth);
		ImGui::SetCursorScreenPos({ bounds.left + numberWidth + Pixels(12.0F), bounds.top });
		ImGui::SetNextItemWidth((std::max)(Pixels(24.0F), bounds.Width() - numberWidth - Pixels(12.0F)));
		const bool changed = Slider("##slider", &value, minimum, maximum, "%.2f");
		active = ImGui::IsItemActive();
		value = std::round(value * 100.0F) / 100.0F;
		return changed;
	}

	bool Button(const char* label, const ImVec2& size)
	{
		ControlFontScope font;
		return ImFluent::Button(label, ResolveButtonSize(label, size));
	}

	bool AccentButton(const char* label, const ImVec2& size)
	{
		ControlFontScope font;
		return ImFluent::AccentButton(label, ResolveButtonSize(label, size));
	}

	bool AccentNavigationItem(const char* id, const char* label, bool selected,
		const char* glyph, bool compact)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(ImGui::GetContentRegionAvail().x,
			Pixels(ImFluent::GetStyle().NavItemHeight));
		const bool pressed = AccentButton(id, size);
		const ImU32 foreground = ImFluent::GetColorU32(ImFluentCol_TextOnAccentPrimary);
		ImFluent::DrawIcon(glyph, { origin.x + Pixels(8.0F), origin.y },
			{ origin.x + Pixels(32.0F), origin.y + size.y }, NavigationGlyphSize, foreground);
		if (!compact)
			TextAt(label, { origin.x + Pixels(44.0F), origin.y + (size.y - Pixels(BodyText.lineHeight)) * 0.5F },
				(std::max)(0.0F, size.x - Pixels(52.0F)), ImFluentTextStyle_Body, foreground);
		else if (ImGui::IsItemHovered())
			ImFluent::SetItemTooltip("%s", label);
		if (selected)
		{
			// 强调按钮的选中标记使用同一前景色，深浅主题都与填充保持对比。
			const float middle = origin.y + size.y * 0.5F;
			ImGui::GetWindowDrawList()->AddRectFilled({ origin.x, middle - Pixels(8.0F) },
				{ origin.x + Pixels(3.0F), middle + Pixels(8.0F) }, foreground, Pixels(1.5F));
		}
		return pressed;
	}

	bool CaptionNavigationButton(const char* id, const char* label, bool selected,
		const ImVec2& size)
	{
		// 调试入口只换文字角色和交互层级，仍沿用真实 Fluent Button 的动画与键盘焦点。
		const std::string stableLabel = std::string(label) + "###" + id;
		ImFluent::PushFont(ImFluentTextStyle_Caption, CaptionText.size);
		ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault,
			selected ? GetPalette().subtleHover : IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ControlFillSecondary, GetPalette().subtleHover);
		ImFluent::PushStyleColor(ImFluentCol_ControlFillTertiary, GetPalette().subtlePressed);
		ImFluent::PushStyleColor(ImFluentCol_ControlStrokeDefault, IM_COL32(0, 0, 0, 0));
		// 上游增加顶部高光边，无边框封装需同时清除上下两条边。
		ImFluent::PushStyleColor(ImFluentCol_ElevationControlTop, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ElevationControlBottom, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_TextPrimary, selected ? TextPrimary : TextSecondary);
		const bool pressed = ImFluent::Button(stableLabel.c_str(), size);
		ImFluent::PopStyleColor(7);
		ImFluent::PopFont();
		return pressed;
	}

	bool HyperlinkButton(const char* label)
	{
		ControlFontScope font;
		return ImFluent::HyperlinkButton(label);
	}

	bool IconButton(const char* id, const char* glyph, const ImVec2& requestedSize)
	{
		const ImVec2 position = ImGui::GetCursorScreenPos();
		const ImVec2 size(requestedSize.x > 0.0F ? requestedSize.x : Pixels(ControlHeight),
			requestedSize.y > 0.0F ? requestedSize.y : Pixels(ControlHeight));
		ImGui::PushID(id);
		const bool pressed = Button("##icon", size);
		ImFluent::DrawIcon(glyph, position, { position.x + size.x, position.y + size.y }, NavigationGlyphSize, TextPrimary);
		ImGui::PopID();
		return pressed;
	}

	bool ToggleButton(const char* label, bool* value, const ImVec2& size)
	{
		ControlFontScope font;
		return ImFluent::ToggleButton(label, value, size);
	}

	bool Checkbox(const char* label, bool* value)
	{
		ControlFontScope font;
		return ImFluent::Checkbox(label, value);
	}

	bool RadioButton(const char* label, int* value, int selectedValue)
	{
		ControlFontScope font;
		return ImFluent::RadioButton(label, value, selectedValue);
	}

	bool RadioButton(const char* label, bool selected)
	{
		ControlFontScope font;
		return ImFluent::RadioButton(label, selected);
	}

	bool ToggleSwitch(const char* label, bool* value, const char* onText, const char* offText)
	{
		ControlFontScope font;
		return ImFluent::ToggleSwitch(label, value, onText, offText);
	}

	// 本地保留当前 Fluent Combo 外观；Popup 在真实条目提交前接入共享滚动机制。
	namespace
	{
		using namespace ImFluent;
		// 与上游保持相同的整像素描边和透明度合成，项目下拉框仍独立接入滚动。
	    static float StrokeWidth()
	    {
	        return ImMax( 1.f, IM_ROUND( FluentDpx( 1.f ) ) );
	    }

	    // A 1 dp outline inside [a, b] on whole pixels (AddRect centres a stroke half a pixel in, which blurs wider strokes).
	    static void StrokeRect( ImDrawList * dl, const ImVec2 & a, const ImVec2 & b, ImU32 col, float rounding, ImDrawFlags flags = 0 )
	    {
	        const float t = StrokeWidth();
	        const float h = t * 0.5f;
	        dl->PathRect( ImVec2( a.x + h, a.y + h ), ImVec2( b.x - h, b.y - h ), ImMax( 0.f, rounding - h ), flags );
	        dl->PathStroke( col, ImDrawFlags_Closed, t );
	    }

	    // Fill inside the 1 dp border (WinUI BackgroundSizing="InnerBorderEdge"), so the border blends with what is behind the control.
	    static void FillInsideBorder( ImDrawList * dl, const ImVec2 & a, const ImVec2 & b, ImU32 col, float rounding, ImDrawFlags flags = 0 )
	    {
	        const float t = StrokeWidth();
	        dl->AddRectFilled( ImVec2( a.x + t, a.y + t ), ImVec2( b.x - t, b.y - t ), col, ImMax( 0.f, rounding - t ), flags );
	    }

	    // The colour that, drawn over `under`, composites to `want` on any background (clamped where no colour can).
	    static ImU32 OverlayToComposite( ImU32 under, ImU32 want )
	    {
	        const float a_under = ( float )( ( under >> IM_COL32_A_SHIFT ) & 0xFF ) / 255.f;
	        const float a_want  = ( float )( ( want >> IM_COL32_A_SHIFT ) & 0xFF ) / 255.f;
	        const float a       = a_under < 1.f ? ImMax( 0.f, ( a_want - a_under ) / ( 1.f - a_under ) ) : 0.f;
	        if ( a <= 0.f )
	            return 0;
	        ImU32 out = ( ImU32 )( a * 255.f + 0.5f ) << IM_COL32_A_SHIFT;
	        for ( int shift : { IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT } )
	        {
	            const float u = ( float )( ( under >> shift ) & 0xFF );
	            const float w = ( float )( ( want >> shift ) & 0xFF );
	            out |= ( ImU32 )ImClamp( ( w * a_want - u * a_under * ( 1.f - a ) ) / a + 0.5f, 0.f, 255.f ) << shift;
	        }
	        return out;
	    }

	    // WinUI elevation border: the side colour with a 1 dp edge of another colour at the bottom (light theme, accent controls)
	    // or at the top (dark theme); the edge strip goes over the side stroke, in the colour that composites to the edge colour.
	    static void RenderElevationBorder( ImDrawList * dl, const ImRect & bb, float rounding, ImU32 sides, ImU32 top, ImU32 bottom, float edge_thickness )
	    {
	        StrokeRect( dl, bb.Min, bb.Max, sides, rounding );

	        const float t = ImMax( StrokeWidth(), IM_ROUND( FluentDpx( edge_thickness > 0.f ? edge_thickness : 1.f ) ) );
	        if ( top != sides )
	            if ( const ImU32 strip = OverlayToComposite( sides, top ) )
	                dl->AddRectFilled( ImVec2( bb.Min.x + rounding, bb.Min.y ), ImVec2( bb.Max.x - rounding, bb.Min.y + t ), strip );
	        if ( bottom != sides )
	            if ( const ImU32 strip = OverlayToComposite( sides, bottom ) )
	                dl->AddRectFilled( ImVec2( bb.Min.x + rounding, bb.Max.y - t ), ImVec2( bb.Max.x - rounding, bb.Max.y ), strip );
	    }


	    static void PushOverlayWindowStyle( float padding_x, float padding_y )
	    {
	        using namespace ImFluent;
	        const ImFluentStyle & style = ImFluent::GetStyle();
	        ImGui::PushStyleColor( ImGuiCol_PopupBg, ImFluent::GetColorU32( ImFluentCol_SolidBgQuarternary ) );
	        ImGui::PushStyleColor( ImGuiCol_Border, ImFluent::GetColorU32( ImFluentCol_SurfaceStrokeFlyout ) );
	        ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, FluentDpx( style.OverlayCornerRadius ) );
	        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( FluentDpx( padding_x ), FluentDpx( padding_y ) ) );
	    }

	    static void PopOverlayWindowStyle()
	    {
	        ImGui::PopStyleVar( 2 );
	        ImGui::PopStyleColor( 2 );
	    }

	    static int ResolveComboPopupMaxItems( ImGuiComboFlags flags )
	    {
	        if ( flags & ImGuiComboFlags_HeightSmall )
	            return 4;
	        if ( flags & ImGuiComboFlags_HeightLarge )
	            return 20;
	        if ( flags & ImGuiComboFlags_HeightLargest )
	            return -1;
	        return 8;
	    }

	    static float CalcComboPopupMaxHeight( int max_items, float row_h )
	    {
	        if ( max_items <= 0 )
	            return FLT_MAX;
	        const ImFluentStyle & style = ImFluent::GetStyle();
	        const float spacing_y       = ImGui::GetStyle().ItemSpacing.y;
	        const float padding_y       = ImFluent::FluentDpx( style.SpacingXSmall );
	        return row_h * max_items + spacing_y * ImMax( 0, max_items - 1 ) + padding_y * 2.f;
	    }

	    static const ImU32 kAnimSeed = 0xF1ECF1ECu;

	    static inline ImU32 LerpColorU32( ImU32 a, ImU32 b, float t )
	    {
	        if ( t <= 0.f )
	            return a;
	        if ( t >= 1.f )
	            return b;
	        // INKEYS PATCH: 先按透明度加权，避免透明黑到浅色的中间帧合成出暗色；输出仍为普通 RGBA。
	        const float aa = ( float )( ( a >> 24 ) & 0xFF );
	        const float ba = ( float )( ( b >> 24 ) & 0xFF );
	        const float wa = aa * ( 1.f - t );
	        const float wb = ba * t;
	        const float alpha = wa + wb;
	        if ( alpha <= 0.f )
	            return 0;
	        ImU32 result = ( ImU32 )( alpha + 0.5f ) << 24;
	        for ( int shift = 0; shift < 24; shift += 8 )
	        {
	            const float channel = ( ( float )( ( a >> shift ) & 0xFF ) * wa
	                + ( float )( ( b >> shift ) & 0xFF ) * wb ) / alpha;
	            result |= ( ImU32 )( channel + 0.5f ) << shift;
	        }
	        return result;
	    }

	    static bool AnimFrameContiguous( ImGuiStorage * s, ImGuiID frame_key )
	    {
	        const int currentFrame = ImGui::GetFrameCount();
	        const bool contiguous  = ( s->GetInt( frame_key, -1 ) == currentFrame - 1 );
	        s->SetInt( frame_key, currentFrame );
	        return contiguous;
	    }

	    static ImGuiID AnimStateKey( ImGuiID id, int channel, bool & out_continuous, ImGuiStorage *& out_storage )
	    {
	        ImGuiStorage * s  = ImGui::GetStateStorage();
	        const ImGuiID kId = id ^ kAnimSeed ^ ( ( ImGuiID )channel * 0x9E3779B9u );
	        out_continuous    = AnimFrameContiguous( s, kId + 1 );
	        out_storage       = s;
	        return kId;
	    }

	    static ImU32 AnimateColorU32( ImGuiID id, ImU32 target, float seconds = 0.083f, int channel = 0 )
	    {
	        bool continuous;
	        ImGuiStorage * s;
	        const ImGuiID kId = AnimStateKey( id, channel, continuous, s );
	        if ( !continuous )
	        {
	            s->SetInt( kId, ( int )target );
	            return target;
	        }
	        ImU32 cur = ( ImU32 )s->GetInt( kId, ( int )target );
	        if ( cur == target )
	            return target;
	        const float dt = ImGui::GetIO().DeltaTime;
	        const float t  = ( seconds <= 0.f ) ? 1.f : 1.f - expf( -dt / seconds );
	        cur            = LerpColorU32( cur, target, t );
	        s->SetInt( kId, ( int )cur );
	        return cur;
	    }

	    static float AnimateFloat( ImGuiID id, float target, float seconds = 0.20f, int channel = 0 )
	    {
	        bool continuous;
	        ImGuiStorage * s;
	        const ImGuiID kId = AnimStateKey( id, channel, continuous, s );
	        if ( !continuous )
	        {
	            s->SetFloat( kId, target );
	            return target;
	        }
	        float cur = s->GetFloat( kId, target );
	        if ( cur == target )
	            return target;
	        const float dt = ImGui::GetIO().DeltaTime;
	        const float tt = ( seconds <= 0.f ) ? 1.f : 1.f - expf( -dt / seconds );
	        cur            = cur + ( target - cur ) * tt;
	        s->SetFloat( kId, cur );
	        return cur;
	    }

	    static void RenderChevronLerp( ImDrawList * dl, float cx, float cy, float L, float t, ImU32 col, float th, bool point_up_at_zero = false )
	    {
	        const float s = point_up_at_zero ? -1.f : 1.f;
	        const ImVec2 a( cx - L, cy + s * ( -L * 0.5f + t * L ) );
	        const ImVec2 b( cx, cy + s * ( L * 0.5f - t * L ) );
	        const ImVec2 c( cx + L, cy + s * ( -L * 0.5f + t * L ) );
	        dl->AddLine( a, b, col, th );
	        dl->AddLine( b, c, col, th );
	    }

	    static void RenderComboChevron( ImDrawList * dl, const ImRect & chev_bb, ImU32 col, ImGuiID anim_id, bool open )
	    {
	        const ImFluentStyle & style = ImFluent::GetStyle();
	        const float L               = ImFluent::FluentDpx( style.ChevronGlyphSize );
	        const float th              = ImFluent::FluentDpx( style.StrokeMedium );
	        const float t               = AnimateFloat( anim_id, open ? 1.f : 0.f, 0.20f );
	        const float cx              = ( chev_bb.Min.x + chev_bb.Max.x ) * 0.5f;
	        const float cy              = ( chev_bb.Min.y + chev_bb.Max.y ) * 0.5f;
	        RenderChevronLerp( dl, cx, cy, L, t, col, th );
	    }

		// 快捷键沿用 ImFluent 的可重绑定动作，并消费方向键，避免同时触发页面导航。
	    static bool ConsumeNavArrow( ImGuiID id, ImGuiDir dir )
	    {
	        ImGuiContext & g = *GImGui;
	        if ( g.NavId != id || g.NavMoveDir != dir || !g.NavMoveSubmitted )
	            return false;
	        ImGui::NavClearPreferredPosForAxis( ( dir == ImGuiDir_Left || dir == ImGuiDir_Right ) ? ImGuiAxis_X : ImGuiAxis_Y );
	        ImGui::NavMoveRequestCancel();
	        g.NavCursorVisible = true;
	        return true;
	    }

	    // Chord press with exact modifiers, wherever focus is. A 0 chord (action turned off) is never pressed.
	    static bool IsActionPressed( ImGuiKeyChord chord )
	    {
	        return chord != 0 && ImGui::IsKeyChordPressed( chord );
	    }

	    // Chord press (exact modifiers) whose key `owner_id` takes, so ImGui's navigation does not also use it (Home/End/PageUp/PageDown scroll).
	    static bool IsChordPressedOwned( ImGuiKeyChord chord, ImGuiID owner_id, bool repeat = true )
	    {
	        if ( chord == 0 )
	            return false;
	        ImGui::SetKeyOwner( ( ImGuiKey )( chord & ~ImGuiMod_Mask_ ), owner_id );
	        return ImGui::IsKeyChordPressed( chord, repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None, owner_id );
	    }

	    // Chord press for the nav-focused item: an arrow chord is taken from the nav move request, other keys are owned by the item.
	    static bool IsFocusedItemChordPressed( ImGuiID id, ImGuiKeyChord chord, bool repeat = true )
	    {
	        ImGuiContext & g = *GImGui;
	        if ( chord == 0 || g.NavId != id || g.ActiveId != 0 )
	            return false;
	        const ImGuiKey key = ( ImGuiKey )( chord & ~ImGuiMod_Mask_ );
	        if ( key == ImGuiKey_LeftArrow || key == ImGuiKey_RightArrow || key == ImGuiKey_UpArrow || key == ImGuiKey_DownArrow )
	        {
	            if ( g.IO.KeyMods != ( chord & ImGuiMod_Mask_ ) )
	                return false;
	            const ImGuiDir dir = key == ImGuiKey_LeftArrow ? ImGuiDir_Left : key == ImGuiKey_RightArrow ? ImGuiDir_Right : key == ImGuiKey_UpArrow ? ImGuiDir_Up : ImGuiDir_Down;
	            return ConsumeNavArrow( id, dir );
	        }
	        return IsChordPressedOwned( chord, id, repeat );
	    }


	    static bool IsItemFocused( ImGuiID id )
	    {
	        ImGuiContext & g = *ImGui::GetCurrentContext();
	        return g.NavId == id && g.NavCursorVisible;
	    }

	    static void RenderNavFocusRing( const ImRect & bb, float rounding )
	    {
	        const ImFluentStyle & style = ImFluent::GetStyle();
	        const float outer           = FluentDpx( style.FocusStrokeThicknessOuter );
	        const float inner           = FluentDpx( style.FocusStrokeThicknessInner );
	        const float pad             = outer;

	        ImDrawList * dl = ImGui::GetForegroundDrawList();

	        dl->AddRect( bb.Min, bb.Max, ImFluent::GetColorU32( ImFluentCol_FocusStrokeInner ), rounding, 0, inner );

	        dl->AddRect( ImVec2( bb.Min.x - pad, bb.Min.y - pad ), ImVec2( bb.Max.x + pad, bb.Max.y + pad ), ImFluent::GetColorU32( ImFluentCol_FocusStrokeOuter ), rounding + pad, 0, outer );
	    }

	    static void RenderComboFocus(ImGuiID id, const ImRect& bounds, float radius)
	    {
	        if (IsItemFocused(id)) RenderNavFocusRing(bounds, radius);
	    }

	}

	bool ComboBox( const char * label, int * current_item, const char * const items[], int items_count, ImGuiComboFlags flags )
	{
	    ControlFontScope font;
	    using namespace ImFluent;
	    ImGuiWindow * w = ImGui::GetCurrentWindow();
	    if ( w->SkipItems )
	    {
	        return false;
	    }

	    IM_ASSERT( ( flags & ( ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview ) ) != ( ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview ) );

	    const ImFluentStyle & style = ImFluent::GetStyle();
	    const float h               = FluentDpx( style.ControlHeight );
	    const float r               = FluentDpx( style.ControlCornerRadius );
	    const float chev_w          = ( flags & ImGuiComboFlags_NoArrowButton ) ? 0.f : h;
	    const char * preview        = ( current_item && *current_item >= 0 && *current_item < items_count && items[ *current_item ] ) ? items[ *current_item ] : "";

	    float full_w;
	    if ( flags & ImGuiComboFlags_NoPreview )
	        full_w = chev_w;
	    else if ( flags & ImGuiComboFlags_WidthFitPreview )
	        full_w = chev_w + ( preview && *preview ? ImGui::CalcTextSize( preview ).x : 0.f ) + FluentDpx( style.SpacingLarge ) * 2.f;
	    else
	        full_w = ImGui::CalcItemWidth();

	    const ImVec2 pos = w->DC.CursorPos;
	    const ImRect bb( pos, ImVec2( pos.x + full_w, pos.y + h ) );

	    const ImGuiID id = w->GetID( label );
	    ImGui::ItemSize( bb );
	    if ( !ImGui::ItemAdd( bb, id ) )
	    {
	        return false;
	    }

	    ImGuiContext & gctx = *ImGui::GetCurrentContext();
	    bool hovered = false, held = false;
	    ImGui::ButtonBehavior( bb, id, &hovered, &held );
	    const bool disabled = ( GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled );

	    const ImGuiID popup_id = ImHashStr( "##fl_cb_popup", 0, id );
	    const bool popup_open = ImGui::IsPopupOpen( popup_id, ImGuiPopupFlags_None );

	    // 仅自身弹层允许入口再次点击收起；实际 HoveredWindow 仍隔离被弹层覆盖的其他控件。
	    const bool click_in_bb  = ImGui::IsMouseClicked( ImGuiMouseButton_Left )
	        && ImGui::IsMouseHoveringRect( bb.Min, bb.Max, false )
	        && ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
	            | (popup_open ? ImGuiHoveredFlags_AllowWhenBlockedByPopup : 0) );
	    const bool nav_activate = ( gctx.NavActivateId == id );
	    const auto& actions = ImFluent::GetActions();
	    const bool key_open = !popup_open && !disabled
	        && ( IsFocusedItemChordPressed( id, actions.TogglePopup, false )
	            || IsFocusedItemChordPressed( id, actions.OpenPopup, false ) );
	    const bool toggle       = ( click_in_bb || nav_activate || key_open ) && !disabled;

	    if ( toggle )
	    {
	        if ( popup_open )
	        {
	            for ( int index = 0; index < gctx.OpenPopupStack.Size; ++index )
	                if ( gctx.OpenPopupStack[index].PopupId == popup_id ) { ImGui::ClosePopupToLevel(index, true); break; }
	        }
	        else
	        {
	            ImGui::OpenPopupEx( popup_id, ImGuiPopupFlags_None );
	        }
	    }

	    ImDrawList * dl = w->DrawList;
	    ImU32 fill_target;
	    if ( disabled )
	        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillDisabled );
	    else if ( held || popup_open )
	        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillTertiary );
	    else if ( hovered )
	        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillSecondary );
	    else
	        fill_target = ImFluent::GetColorU32( ImFluentCol_ControlFillDefault );
	    const ImU32 fill = AnimateColorU32( id, fill_target );
	    FillInsideBorder( dl, bb.Min, bb.Max, fill, r );

	    const ImU32 stroke     = ImFluent::GetColorU32( ImFluentCol_ControlStrokeDefault );
	    const ImU32 stroke_top = ImFluent::GetColorU32( ImFluentCol_ElevationControlTop );
	    const ImU32 stroke_bot = ImFluent::GetColorU32( ImFluentCol_ElevationControlBottom );
	    if ( !held && !popup_open && !disabled )
	        RenderElevationBorder( dl, bb, r, stroke, stroke_top, stroke_bot, 1.f );
	    else
	        StrokeRect( dl, bb.Min, bb.Max, stroke, r );

	    const ImU32 text_col = ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextPrimary );
	    if ( !( flags & ImGuiComboFlags_NoPreview ) && preview && *preview )
	    {
	        const float pad_x = FluentDpx( style.SpacingLarge );
	        ImFluentStackGuard g;
	        g.PushStyleColor( ImGuiCol_Text, text_col );
	        const ImVec2 tmin( bb.Min.x + pad_x, bb.Min.y );
	        const ImVec2 tmax( bb.Max.x - chev_w, bb.Max.y );
	        const ImRect text_clip( tmin, tmax );
	        ImGui::RenderTextClipped( tmin, tmax, preview, ImGui::FindRenderedTextEnd( preview ), NULL, ImVec2( 0.f, 0.5f ), &text_clip );
	    }

	    if ( chev_w > 0.f )
	    {
	        const ImRect chev_bb( ImVec2( bb.Max.x - chev_w, bb.Min.y ), bb.Max );
	        const ImU32 chev_col = ImFluent::GetColorU32( disabled ? ImFluentCol_TextDisabled : ImFluentCol_TextSecondary );
	        RenderComboChevron( dl, chev_bb, chev_col, id ^ 0xCAFEu, popup_open );
	    }

	    RenderComboFocus( id, bb, r );

	    bool changed              = false;
	    const int popup_max_items = ResolveComboPopupMaxItems( flags );
	    const float popup_max_h   = CalcComboPopupMaxHeight( popup_max_items, h );
	    ImGui::SetNextWindowPos( ImVec2( bb.Min.x, bb.Max.y + FluentDpx( style.SpacingXSmall ) ) );
	    ImGui::SetNextWindowSizeConstraints( ImVec2( bb.GetWidth(), 0.f ), ImVec2( bb.GetWidth(), popup_max_h ) );

	    PushOverlayWindowStyle( style.SpacingXSmall, style.SpacingXSmall );
	    const ImGuiWindowFlags cb_wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
	    ImGuiWindow* popup_window = nullptr;
	    for ( const auto& popup : GImGui->OpenPopupStack ) if ( popup.PopupId == popup_id ) popup_window = popup.Window;
	    {
	    ScrollbarStyleScope scrollbar(popup_window);
	    if ( ImGui::BeginPopupEx( popup_id, cb_wflags ) )
	    {
	        auto* popup_host = ImGui::GetCurrentWindow();
	        scrollbar.Finish(popup_host);
	        int focus_only = -1;
	        if ( gctx.NavWindow == popup_host && gctx.NavId != 0 )
	        {
	            if ( IsChordPressedOwned( actions.FirstItem, popup_id, false ) ) focus_only = 0;
	            if ( IsChordPressedOwned( actions.LastItem, popup_id, false ) ) focus_only = items_count - 1;
	        }
	        const float row_advance = h + ImGui::GetStyle().ItemSpacing.y;
	        ImGuiListClipper clipper;
	        clipper.Begin( items_count, row_advance );
	        if ( current_item && *current_item >= 0 && *current_item < items_count )
	            clipper.IncludeItemByIndex( *current_item );
	        if ( focus_only >= 0 ) clipper.IncludeItemByIndex( focus_only );
	        while ( clipper.Step() )
	        {
	            for ( int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i )
	            {
	                if ( !items[ i ] )
	                    continue;
	                ImGui::PushID( i );
	                const bool sel = ( current_item && *current_item == i );
	                if ( ImFluent::Selectable( items[ i ], sel, NULL, h ) )
	                {
	                    if ( current_item )
	                        *current_item = i;
	                    changed = true;
	                    ImGui::CloseCurrentPopup();
	                }
	                const auto item_id = gctx.LastItemData.ID;
	                if ( sel ) ImGui::SetItemDefaultFocus();
	                if ( i == 0 ) ConsumeNavArrow( item_id, ImGuiDir_Up );
	                if ( i == items_count - 1 ) ConsumeNavArrow( item_id, ImGuiDir_Down );
	                if ( i == focus_only )
	                {
	                    ImGui::SetFocusID( item_id, popup_host );
	                    ImGui::SetScrollHereY();
	                    gctx.NavCursorVisible = true;
	                }
	                AllowContentDrag();
	                ImGui::PopID();
	            }
	        }
	        // 只关闭原本已展开的列表，避免本帧 F4 打开后又立即关闭。
	        if ( popup_open && ( IsActionPressed( actions.TogglePopup )
	            || IsActionPressed( actions.ClosePopup ) || ImGui::IsKeyPressed( ImGuiKey_Tab, false ) ) )
	            ImGui::CloseCurrentPopup();
	        ImGui::EndPopup();
	    }
	    }
	    PopOverlayWindowStyle();

	    return changed;
	}

	bool ComboBox(const char* label, int* value, const std::vector<std::string>& items)
	{
		std::vector<const char*> views;
		for (const auto& item : items) views.push_back(item.c_str());
		return ComboBox(label, value, views.data(), static_cast<int>(views.size()));
	}

	bool Slider(const char* label, float* value, float minimum, float maximum,
		const char* format, ImGuiSliderFlags flags)
	{
		ControlFontScope font;
		return ImFluent::Slider(label, value, minimum, maximum, format, flags);
	}

	bool SliderInt(const char* label, int* value, int minimum, int maximum,
		const char* format, ImGuiSliderFlags flags)
	{
		ControlFontScope font;
		return ImFluent::SliderInt(label, value, minimum, maximum, format, flags);
	}

	bool NumberBox(const char* label, double* value, double step, double fastStep,
		const char* format, ImGuiInputTextFlags flags)
	{
		ControlFontScope font;
		return ImFluent::NumberBox(label, value, step, fastStep, format, flags);
	}

	bool TextBox(const char* label, std::string& value, const char* hint, ImGuiInputTextFlags flags)
	{
		ControlFontScope font;
		return ImFluent::TextBox(label, value, hint, flags);
	}

	bool TextBox(const char* label, char* value, size_t capacity, const char* hint, ImGuiInputTextFlags flags)
	{
		ControlFontScope font;
		return ImFluent::TextBox(label, value, capacity, hint, flags);
	}

	bool BeginCard(const char* id, const ImVec2& size, ImFluentCardStyle style)
	{
		ScrollbarStyleScope scrollbar(ChildWindow(id));
		const bool visible = ImFluent::BeginCard(id, size, style);
		scrollbar.Finish(ImGui::GetCurrentWindow());
		return visible;
	}

	void EndCard() { ImFluent::EndCard(); }

	void Details(const char* id, const char* title, const std::function<void()>& content)
	{
		ImGui::PushID(id);
		bool expanded = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##open"));
		const std::string label = std::string(title) + "###details";
		ControlFontScope font;
		const bool visible = [&] {
			ScrollbarStyleScope scrollbar(ChildWindow(label.c_str()));
			const bool open = ImFluent::BeginExpander(label.c_str(), &expanded);
			if (open) scrollbar.Finish(ImGui::GetCurrentWindow());
			return open;
		}();
		if (visible)
		{
			content();
			ImFluent::EndExpander();
		}
		ImGui::GetStateStorage()->SetBool(ImGui::GetID("##open"), expanded);
		ImGui::PopID();
	}

	void Details(const char* id, const char* title, const char* description)
	{
		Details(id, title, [&] { Text(description, ImFluentTextStyle_Caption, TextSecondary); });
	}

	void SetStateLabels(const char* onText, const char* offText)
	{
		stateOn = onText ? onText : "";
		stateOff = offText ? offText : "";
	}

	bool ToggleRow(const char* id, const char* title, const char* description,
		const char* glyph, bool& value)
	{
		const float labelWidth = (std::max)(TextWidth(stateOn.c_str()), TextWidth(stateOff.c_str()));
		bool changed = false;
		SettingRow(id, title, description, glyph, labelWidth / Pixels(1.0F) + 52.0F, 20.0F,
			[&](const LayoutRect& bounds)
			{
				changed = ToggleAction(bounds, value);
				TextAt(value ? stateOn.c_str() : stateOff.c_str(), { bounds.left, bounds.top }, labelWidth);
			});
		return changed;
	}

	bool ComboRow(const char* id, const char* title, const char* description,
		const char* glyph, int& value, const std::vector<std::string>& items)
	{
		float width = 160.0F;
		for (const auto& item : items)
			width = (std::max)(width, ControlTextWidth(item.c_str()) / Pixels(1.0F) + 48.0F);
		bool changed = false;
		SettingRow(id, title, description, glyph, (std::min)(320.0F, width), ControlHeight,
			[&](const LayoutRect&) { changed = ComboBox("##select", &value, items); });
		return changed;
	}

	bool SliderRow(const char* id, const char* title, const char* description,
		const char* glyph, float& value, float minimum, float maximum, bool& active, const char* format)
	{
		bool changed = false;
		active = false;
		SettingRow(id, title, description, glyph, 240.0F, ControlHeight, [&](const LayoutRect& bounds)
			{
				char output[64];
				ImFormatString(output, IM_ARRAYSIZE(output), format, value);
				const float numberWidth = (std::min)(Pixels(72.0F), (std::max)(Pixels(40.0F), TextWidth(output)));
				TextAt(output, { bounds.left, bounds.top + Pixels(6.0F) }, numberWidth);
				ImGui::SetCursorScreenPos({ bounds.left + numberWidth + Pixels(12.0F), bounds.top });
				ImGui::SetNextItemWidth((std::max)(Pixels(24.0F), bounds.Width() - numberWidth - Pixels(12.0F)));
				changed = Slider("##slider", &value, minimum, maximum, format);
				active = ImGui::IsItemActive();
			});
		return changed;
	}

	bool SliderIntRow(const char* id, const char* title, const char* description,
		const char* glyph, int& value, int minimum, int maximum, bool& active, const char* format)
	{
		bool changed = false;
		active = false;
		SettingRow(id, title, description, glyph, 240.0F, ControlHeight, [&](const LayoutRect& bounds)
			{
				char output[64];
				ImFormatString(output, IM_ARRAYSIZE(output), format, value);
				const float numberWidth = (std::min)(Pixels(72.0F), (std::max)(Pixels(40.0F), TextWidth(output)));
				TextAt(output, { bounds.left, bounds.top + Pixels(6.0F) }, numberWidth);
				ImGui::SetCursorScreenPos({ bounds.left + numberWidth + Pixels(12.0F), bounds.top });
				ImGui::SetNextItemWidth((std::max)(Pixels(24.0F), bounds.Width() - numberWidth - Pixels(12.0F)));
				changed = SliderInt("##slider", &value, minimum, maximum, format);
				active = ImGui::IsItemActive();
			});
		return changed;
	}

	bool ButtonRow(const char* id, const char* title, const char* description,
		const char* glyph, const char* label, bool accent)
	{
		bool clicked = false;
		SettingRow(id, title, description, glyph, ButtonWidth(label), ControlHeight,
			[&](const LayoutRect& bounds)
			{
				const std::string stableLabel = std::string(label) + "###action";
				clicked = accent ? AccentButton(stableLabel.c_str(), { bounds.Width(), bounds.Height() })
					: Button(stableLabel.c_str(), { bounds.Width(), bounds.Height() });
			});
		return clicked;
	}

	int ButtonGroupRow(const char* id, const char* title, const char* description,
		const char* glyph, std::initializer_list<ButtonSpec> buttons)
	{
		std::vector<float> widths;
		for (const auto& button : buttons) widths.push_back(ButtonWidth(button.label.c_str()));
		const float available = MeasureRow(ImGui::GetContentRegionAvail().x / Pixels(1.0F),
			FLT_MAX, ControlHeight, glyph && *glyph).actionWidth;
		const auto group = ResolveButtonGroup(widths, (std::min)(ButtonGroupMaximumWidth, available));
		int clicked = -1;
		SettingRow(id, title, description, glyph, group.width, group.height,
			[&](const LayoutRect& bounds)
			{
				size_t index = 0;
				for (const auto& button : buttons)
				{
					const auto& rect = group.items[index];
					ImGui::SetCursorScreenPos({ bounds.left + Pixels(rect.left), bounds.top + Pixels(rect.top) });
					ImGui::PushID(button.id);
					ImFluent::BeginDisabled(button.disabled);
					const std::string label = button.label + "###button";
					const bool pressed = button.accent ? AccentButton(label.c_str(), { Pixels(rect.Width()), Pixels(rect.Height()) })
						: Button(label.c_str(), { Pixels(rect.Width()), Pixels(rect.Height()) });
					ImFluent::EndDisabled();
					ImGui::PopID();
					if (pressed) clicked = static_cast<int>(index);
					++index;
				}
			});
		return clicked;
	}

	bool NavigationRow(const char* id, const char* title, const char* description, const char* glyph, ImTextureID iconTexture)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		SettingRow(id, title, description, glyph, 24.0F, 24.0F, [&](const LayoutRect& bounds)
			{
				ImFluent::DrawIcon("\ue76c", { bounds.left, bounds.top }, { bounds.right, bounds.bottom }, 12.0F, TextSecondary);
			}, Card, Stroke, iconTexture);
		const ImVec2 next = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(origin);
		ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ControlFillSecondary, GetPalette().rowHover);
		ImFluent::PushStyleColor(ImFluentCol_ControlFillTertiary, GetPalette().rowPressed);
		ImFluent::PushStyleColor(ImFluentCol_ControlStrokeDefault, IM_COL32(0, 0, 0, 0));
		// 上游增加顶部高光边，无边框封装需同时清除上下两条边。
		ImFluent::PushStyleColor(ImFluentCol_ElevationControlTop, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ElevationControlBottom, IM_COL32(0, 0, 0, 0));
		ImGui::PushID(id);
		const bool clicked = Button("##navigate", { width, (std::max)(1.0F, next.y - origin.y - Pixels(RowGap)) });
		ImGui::PopID();
		ImFluent::PopStyleColor(6);
		ImGui::SetCursorScreenPos(next);
		return clicked;
	}

	bool Notice(const char* id, const char* title, const char* description,
		ImFluentInfoSeverity severity, const char* actionLabel)
	{
		const ImU32 fill = GetPalette().notice[std::clamp(severity,
			static_cast<int>(ImFluentInfoSeverity_Informational), static_cast<int>(ImFluentInfoSeverity_Critical))];
		bool clicked = false;
		SettingRow(id, title, description, "\ue946", actionLabel ? ButtonWidth(actionLabel) : 0.0F,
			actionLabel ? ControlHeight : 0.0F, [&](const LayoutRect& bounds)
			{
				if (actionLabel)
					clicked = Button((std::string(actionLabel) + "###notice-action").c_str(), { bounds.Width(), bounds.Height() });
			}, fill, Stroke);
		return clicked;
	}

	void ApplyScrollbars()
	{
		// 保留 8-DIP 命中轨道，仅绘制中间细灰 thumb，轨道自身透明。
		auto& style = ImGui::GetStyle();
		style.ScrollbarSize = Pixels(ScrollbarTrackSize);
		style.ScrollbarPadding = Pixels(ScrollbarPadding);
		style.ScrollbarRounding = Pixels(3.0F);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(GetPalette().scrollbar);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(GetPalette().scrollbarHovered);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(GetPalette().scrollbarActive);
	}

	void ApplyPalette(bool darkMode, BackdropMode backdrop)
	{
		// 只在帧边界换色；SetAccentColor 会重建 preset，项目 token 最后覆盖。
		activeDarkMode = ResolveThemeMode(darkMode) == ThemeMode::Dark;
		activePalette = PaletteForTheme(activeDarkMode);
		// 只让原生材质参与大表面合成；纯色回退及前景、描边保持原样。
		if (backdrop != BackdropMode::Solid)
		{
			auto alpha = [](ImU32 value, int opacity) {
				return (value & ~IM_COL32_A_MASK) | (static_cast<ImU32>(opacity) << IM_COL32_A_SHIFT);
			};
			activePalette.appBase = alpha(activePalette.appBase, 204);
			activePalette.pageSurface = alpha(activePalette.pageSurface, 230);
			activePalette.card = alpha(activePalette.card, 245);
			activePalette.cardSecondary = alpha(activePalette.cardSecondary, 245);
		}
		ImFluent::SetThemePreset(activeDarkMode ? ImFluentThemePreset_Dark : ImFluentThemePreset_Light);
		ImFluent::SetAccentColor(ImColor(Accent));
		auto& style = ImFluent::GetStyle();
		style.NavItemHeight = 36.0F;
		style.NavPaneOpenWidth = OpenPaneWidth;
		style.NavPaneCompactWidth = CompactPaneWidth;
		style.ControlHeight = 32.0F;
		style.ControlCornerRadius = 4.0F;
		style.StandardIconSize = NavigationGlyphSize;
		style.ToggleSwitchWidth = 40.0F;
		style.ToggleSwitchHeight = 20.0F;
		const auto& colors = GetPalette();
		auto color = [](ImU32 value) { return ImGui::ColorConvertU32ToFloat4(value); };
		style.Colors[ImFluentCol_TextPrimary] = color(TextPrimary);
		style.Colors[ImFluentCol_TextSecondary] = color(TextSecondary);
		style.Colors[ImFluentCol_TextDisabled] = color(colors.textDisabled);
		style.Colors[ImFluentCol_CardBgDefault] = color(Card);
		style.Colors[ImFluentCol_CardBgSecondary] = color(colors.cardSecondary);
		style.Colors[ImFluentCol_CardStrokeDefault] = color(Stroke);
		style.Colors[ImFluentCol_CardStrokeSolid] = color(Stroke);
		// 浮层和输入控件也复用 LayerFill，保持实色，避免底层文字直接透出。
		const auto& solidColors = PaletteForTheme(activeDarkMode);
		const auto overlay = color(solidColors.cardSecondary);
		style.Colors[ImFluentCol_LayerFillDefault] = overlay;
		style.Colors[ImFluentCol_LayerFillAlt] = overlay;
		style.Colors[ImFluentCol_SolidBgBase] = color(AppBase);
		style.Colors[ImFluentCol_SolidBgQuarternary] = color(solidColors.pageSurface);
		style.Colors[ImFluentCol_SubtleFillSecondary] = color(colors.subtleHover);
		style.Colors[ImFluentCol_SubtleFillTertiary] = color(colors.subtlePressed);
		style.Colors[ImFluentCol_AccentFillSelectedTextBg] = color(colors.selection);

		// ImGui 弹出层与表格同样同步；普通嵌套 child 透明，避免多层叠出亮色底。
		auto* imguiColors = ImGui::GetStyle().Colors;
		imguiColors[ImGuiCol_Text] = color(TextPrimary);
		imguiColors[ImGuiCol_TextDisabled] = color(colors.textDisabled);
		imguiColors[ImGuiCol_WindowBg] = color(AppBase);
		imguiColors[ImGuiCol_ChildBg] = { 0.0F, 0.0F, 0.0F, 0.0F };
		imguiColors[ImGuiCol_PopupBg] = overlay;
		imguiColors[ImGuiCol_TitleBg] = imguiColors[ImGuiCol_TitleBgActive]
			= imguiColors[ImGuiCol_TitleBgCollapsed] = color(solidColors.appBase);
		imguiColors[ImGuiCol_MenuBarBg] = overlay;
		imguiColors[ImGuiCol_Header] = imguiColors[ImGuiCol_HeaderActive] = color(colors.subtleHover);
		imguiColors[ImGuiCol_HeaderHovered] = color(colors.subtlePressed);
		imguiColors[ImGuiCol_TabSelected] = imguiColors[ImGuiCol_TabDimmedSelected] = color(colors.cardSecondary);
		imguiColors[ImGuiCol_TabHovered] = color(colors.subtleHover);
		imguiColors[ImGuiCol_TableHeaderBg] = color(colors.cardSecondary);
		imguiColors[ImGuiCol_TableRowBgAlt] = color(colors.rowHover);
		imguiColors[ImGuiCol_TextSelectedBg] = color(colors.selection);
		ApplyScrollbars();
	}
}
