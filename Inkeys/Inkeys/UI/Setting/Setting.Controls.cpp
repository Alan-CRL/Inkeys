#include "Setting.Controls.h"
#include "imgui/imgui_internal.h"
#include <cstring>
#include <string_view>
#include <vector>

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
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

	void SettingRow(const char* id, const char* title, const char* description,
		const char* glyph, float actionWidthDip, float actionHeightDip,
		const std::function<void(const LayoutRect&)>& action, ImU32 fill, ImU32 border)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const auto measure = MeasureRow(width / Pixels(1.0F), actionWidthDip,
			actionHeightDip, glyph && *glyph);
		const float titleHeight = TextHeight(title, Pixels(measure.textWidth));
		const float descriptionHeight = TextHeight(description, Pixels(measure.textWidth), ImFluentTextStyle_Caption);
		const auto row = ResolveRowLayout(measure,
			(titleHeight + descriptionHeight) / Pixels(1.0F));
		// 整行占位同时登记尾部间隙，最后一行后直接 EndChild 也不会越过内容边界。
		ImGui::Dummy({ width, Pixels(row.height + RowGap) });
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 bottom(origin.x + width, origin.y + Pixels(row.height));
		draw->AddRectFilled(origin, bottom, fill, Pixels(4.0F));
		draw->AddRect(origin, bottom, border, Pixels(4.0F));
		TextAt(title, { origin.x + Pixels(row.text.left), origin.y + Pixels(row.text.top) },
			Pixels(row.text.Width()));
		TextAt(description, { origin.x + Pixels(row.text.left), origin.y + Pixels(row.text.top) + titleHeight },
			Pixels(row.text.Width()), ImFluentTextStyle_Caption, TextSecondary);
		if (measure.hasIcon)
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

	bool ComboBox(const char* label, int* value, const char* const items[], int count, ImGuiComboFlags flags)
	{
		ControlFontScope font;
		return ImFluent::ComboBox(label, value, items, count, flags);
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
		return ImFluent::BeginCard(id, size, style);
	}

	void EndCard() { ImFluent::EndCard(); }

	void Details(const char* id, const char* title, const std::function<void()>& content)
	{
		ImGui::PushID(id);
		bool expanded = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##open"));
		const std::string label = std::string(title) + "###details";
		ControlFontScope font;
		if (ImFluent::BeginExpander(label.c_str(), &expanded))
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

	bool NavigationRow(const char* id, const char* title, const char* description, const char* glyph)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		SettingRow(id, title, description, glyph, 24.0F, 24.0F, [&](const LayoutRect& bounds)
			{
				ImFluent::DrawIcon("\ue76c", { bounds.left, bounds.top }, { bounds.right, bounds.bottom }, 12.0F, TextSecondary);
			});
		const ImVec2 next = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(origin);
		ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ControlFillSecondary, IM_COL32(0, 0, 0, 5));
		ImFluent::PushStyleColor(ImFluentCol_ControlFillTertiary, IM_COL32(0, 0, 0, 10));
		ImFluent::PushStyleColor(ImFluentCol_ControlStrokeDefault, IM_COL32(0, 0, 0, 0));
		ImFluent::PushStyleColor(ImFluentCol_ElevationControlBottom, IM_COL32(0, 0, 0, 0));
		ImGui::PushID(id);
		const bool clicked = Button("##navigate", { width, (std::max)(1.0F, next.y - origin.y - Pixels(RowGap)) });
		ImGui::PopID();
		ImFluent::PopStyleColor(5);
		ImGui::SetCursorScreenPos(next);
		return clicked;
	}

	bool Notice(const char* id, const char* title, const char* description,
		ImFluentInfoSeverity severity, const char* actionLabel)
	{
		const ImU32 fill = severity == ImFluentInfoSeverity_Critical ? IM_COL32(253, 239, 237, 255)
			: severity == ImFluentInfoSeverity_Warning ? IM_COL32(255, 248, 223, 255)
			: severity == ImFluentInfoSeverity_Success ? IM_COL32(235, 246, 238, 255) : IM_COL32(241, 246, 251, 255);
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
		style.Colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(IM_COL32(96, 96, 96, 104));
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(96, 96, 96, 150));
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(80, 80, 80, 184));
	}

	void ApplyPalette()
	{
		// SetAccentColor 会重建整个 preset，必须先调用再覆写项目 token。
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
		style.Colors[ImFluentCol_TextPrimary] = ImGui::ColorConvertU32ToFloat4(TextPrimary);
		style.Colors[ImFluentCol_TextSecondary] = ImGui::ColorConvertU32ToFloat4(TextSecondary);
		style.Colors[ImFluentCol_CardBgDefault] = ImGui::ColorConvertU32ToFloat4(Card);
		style.Colors[ImFluentCol_CardStrokeDefault] = ImGui::ColorConvertU32ToFloat4(Stroke);
		style.Colors[ImFluentCol_SubtleFillSecondary] = ImGui::ColorConvertU32ToFloat4(IM_COL32(231, 231, 231, 255));
		ImGui::GetStyle().Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(TextPrimary);
		ApplyScrollbars();
	}
}
