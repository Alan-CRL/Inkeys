#pragma once

#include "Setting.Design.h"
#include "Setting.Theme.h"
#include "Setting.Typography.h"
#include "imfluent/imfluent.h"
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace Inkeys::UI::Setting::Design
{
	// 入场完整准备帧可固定到 native 终态条宽；当前帧嵌套 child 同步，普通页面仍保留出现/消失动效。
	bool BeginScrollChild(const char* id, const ImVec2& size, ImGuiChildFlags childFlags = 0, ImGuiWindowFlags flags = 0, bool resetToTop = false, bool settleScrollbarLayout = false);
	bool BeginScrollPopupModal(const char* id, ImGuiWindowFlags flags);

	// 主题语义色集中在同一张表；页面只选择角色，不自行判断明暗。
	struct Palette
	{
		ImU32 appBase, pageSurface, card, cardSecondary, stroke;
		ImU32 textPrimary, textSecondary, textDisabled, accent, criticalText;
		ImU32 subtleHover, subtlePressed, rowHover, rowPressed, selection;
		ImU32 scrollbar, scrollbarHovered, scrollbarActive;
		ImU32 notice[ImFluentInfoSeverity_Critical + 1];
		ImU32 heroFill, heroStroke, heroEyebrow, heroDescription, heroMint, heroLavender;
		ImU32 artCool, artWarm, artStroke, artRule, artAxis, artWarmInk;
		ImU32 artPenSelection, artOrange, artGreen;
		ImU32 featureTint[3], featureInk[3], avatarFill;
	};

	[[nodiscard]] const Palette& PaletteForTheme(bool darkMode) noexcept;
	[[nodiscard]] const Palette& GetPalette() noexcept;
	[[nodiscard]] bool IsDarkMode() noexcept;

	// 别名绑定同一份稳定存储，既有行 API 的默认参数也会读取当前主题。
	extern const ImU32& AppBase;
	extern const ImU32& PageSurface;
	extern const ImU32& Card;
	extern const ImU32& Stroke;
	extern const ImU32& TextPrimary;
	extern const ImU32& TextSecondary;
	extern const ImU32& Accent;

	float Pixels(float dip);
	float TextHeight(const char* text, float widthPixels,
		ImFluentTextStyle style = ImFluentTextStyle_Body);
	float TextWidth(const char* text, ImFluentTextStyle style = ImFluentTextStyle_Body);
	void TextAt(const char* text, ImVec2 position, float widthPixels,
		ImFluentTextStyle style = ImFluentTextStyle_Body, ImU32 color = TextPrimary);
	void Text(const char* text, ImFluentTextStyle style = ImFluentTextStyle_Body,
		ImU32 color = TextPrimary);
	void PageHeader(const char* title, const char* description = nullptr);
	void SectionHeader(const char* title);
	void PageContentStart();
	float ControlTextWidth(const char* text);
	float ButtonWidth(const char* label);
	// 由现有会话在帧边界提供时钟和软件动画偏好，不新增计时器或持久化状态。
	void ConfigureRowAnimations(float delta, bool enabled, float rate);
	void SettingRow(const char* id, const char* title, const char* description,
		const char* glyph, float actionWidthDip, float actionHeightDip,
		const std::function<void(const LayoutRect&)>& action,
		ImU32 fill = Card, ImU32 border = Stroke, ImTextureID iconTexture = 0,
		ImU32 iconTint = IM_COL32_WHITE, bool keepDescriptionInline = false);
	bool ToggleAction(const LayoutRect& bounds, bool& value);
	bool SliderAction(const LayoutRect& bounds, float& value, float minimum,
		float maximum, bool& active);

	// 控件字体和自然宽度集中在此；size 是物理像素，行测量参数是 DIP。
	bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));
	bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));
	bool AccentNavigationItem(const char* id, const char* label, bool selected,
		const char* glyph, bool compact);
	bool CaptionNavigationButton(const char* id, const char* label, bool selected,
		const ImVec2& size);
	bool HyperlinkButton(const char* label);
	bool CopyHyperlink(const char* id, const char* label, const char* value);
	bool IconButton(const char* id, const char* glyph, const ImVec2& size = ImVec2(0, 0));
	bool ToggleButton(const char* label, bool* value, const ImVec2& size = ImVec2(0, 0));
	bool Checkbox(const char* label, bool* value);
	bool RadioButton(const char* label, int* value, int selectedValue);
	bool RadioButton(const char* label, bool selected);
	bool ToggleSwitch(const char* label, bool* value, const char* onText = "", const char* offText = "");
	bool ComboBox(const char* label, int* value, const std::vector<std::string>& items);
	bool ComboBox(const char* label, int* value, const char* const items[], int count, ImGuiComboFlags flags = 0);
	bool Slider(const char* label, float* value, float minimum, float maximum,
		const char* format = "%.2f", ImGuiSliderFlags flags = 0);
	bool SliderInt(const char* label, int* value, int minimum, int maximum,
		const char* format = "%d", ImGuiSliderFlags flags = 0);
	bool NumberBox(const char* label, double* value, double step = 1.0,
		double fastStep = 10.0, const char* format = "%.3f", ImGuiInputTextFlags flags = 0);
	bool TextBox(const char* label, std::string& value, const char* hint = nullptr,
		ImGuiInputTextFlags flags = 0);
	bool TextBox(const char* label, char* value, size_t capacity, const char* hint = nullptr,
		ImGuiInputTextFlags flags = 0);
	bool BeginCard(const char* id, const ImVec2& size = ImVec2(0, 0),
		ImFluentCardStyle style = ImFluentCardStyle_Filled);
	void EndCard();
	struct CheckboxSpec
	{
		std::string label;
		bool* value;
		bool* changed;
	};
	void CheckboxGroup(const char* id, const char* title, const char* glyph,
		std::initializer_list<CheckboxSpec> items);
	void Details(const char* id, const char* title, const char* description);
	void Details(const char* id, const char* title, const std::function<void()>& content);
	void SetStateLabels(const char* onText, const char* offText);
	void SetCopyLabels(const char* prompt, const char* copied);
	bool ToggleRow(const char* id, const char* title, const char* description,
		const char* glyph, bool& value);
	bool ComboRow(const char* id, const char* title, const char* description,
		const char* glyph, int& value, const std::vector<std::string>& items);
	bool SliderRow(const char* id, const char* title, const char* description,
		const char* glyph, float& value, float minimum, float maximum,
		bool& active, const char* format = "%.2f");
	bool SliderIntRow(const char* id, const char* title, const char* description,
		const char* glyph, int& value, int minimum, int maximum,
		bool& active, const char* format = "%d");
	bool ButtonRow(const char* id, const char* title, const char* description,
		const char* glyph, const char* label, bool accent = false);
	struct ButtonSpec
	{
		const char* id;
		std::string label;
		bool accent = false;
		bool disabled = false;
	};
	int ButtonGroupRow(const char* id, const char* title, const char* description,
		const char* glyph, std::initializer_list<ButtonSpec> buttons);
	bool NavigationRow(const char* id, const char* title, const char* description,
		const char* glyph, ImTextureID iconTexture = 0, ImU32 iconTint = IM_COL32_WHITE);
	bool Notice(const char* id, const char* title, const char* description,
		ImFluentInfoSeverity severity = ImFluentInfoSeverity_Informational,
		const char* actionLabel = nullptr);
	void ApplyScrollbars();
	// 在渲染线程帧边界调用，不触碰字体、纹理、设备或持久化。
	void ApplyPalette(bool darkMode = false, BackdropMode backdrop = BackdropMode::Solid);
}
