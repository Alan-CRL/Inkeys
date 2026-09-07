#include "Setting.Controls.h"
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
		ImFluent::PushStyleColor(ImFluentCol_ControlFillSecondary, GetPalette().rowHover);
		ImFluent::PushStyleColor(ImFluentCol_ControlFillTertiary, GetPalette().rowPressed);
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

	void ApplyPalette(bool darkMode)
	{
		// 只在帧边界换色；SetAccentColor 会重建 preset，项目 token 最后覆盖。
		activeDarkMode = ResolveThemeMode(darkMode) == ThemeMode::Dark;
		activePalette = PaletteForTheme(activeDarkMode);
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
		style.Colors[ImFluentCol_LayerFillDefault] = color(colors.cardSecondary);
		style.Colors[ImFluentCol_LayerFillAlt] = color(colors.cardSecondary);
		style.Colors[ImFluentCol_SolidBgBase] = color(AppBase);
		style.Colors[ImFluentCol_SolidBgQuarternary] = color(PageSurface);
		style.Colors[ImFluentCol_SubtleFillSecondary] = color(colors.subtleHover);
		style.Colors[ImFluentCol_SubtleFillTertiary] = color(colors.subtlePressed);
		style.Colors[ImFluentCol_AccentFillSelectedTextBg] = color(colors.selection);

		// ImGui 弹出层与表格同样同步；普通嵌套 child 透明，避免多层叠出亮色底。
		auto* imguiColors = ImGui::GetStyle().Colors;
		imguiColors[ImGuiCol_Text] = color(TextPrimary);
		imguiColors[ImGuiCol_TextDisabled] = color(colors.textDisabled);
		imguiColors[ImGuiCol_WindowBg] = color(AppBase);
		imguiColors[ImGuiCol_ChildBg] = { 0.0F, 0.0F, 0.0F, 0.0F };
		imguiColors[ImGuiCol_PopupBg] = color(colors.cardSecondary);
		imguiColors[ImGuiCol_TitleBg] = imguiColors[ImGuiCol_TitleBgActive]
			= imguiColors[ImGuiCol_TitleBgCollapsed] = color(AppBase);
		imguiColors[ImGuiCol_MenuBarBg] = color(colors.cardSecondary);
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
