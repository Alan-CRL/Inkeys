#pragma once

#include "Setting.Design.h"
#include "Setting.Motion.h"
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
	bool BeginScrollChild(const char* id, const ImVec2& size, ImGuiChildFlags childFlags = 0, ImGuiWindowFlags flags = 0, bool resetToTop = false, bool settleScrollbarLayout = false, bool stableBodyLayout = false, float visualAlpha = 1.0F, PageMotionState* motion = nullptr);
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
	// 轻量注意事项，与说明同字号；蓝色信息图标及正常前景色文字自动换行。
	void InfoText(const char* text, ImFluentTextStyle style = ImFluentTextStyle_Caption);
	void PageHeader(const char* title, const char* description = nullptr);
	void SectionHeader(const char* title);
	void PageContentStart();
	float ControlTextWidth(const char* text);
	float ButtonWidth(const char* label);
	// 由现有会话在帧边界提供时钟和软件动画偏好，不新增计时器或持久化状态。
	void ConfigureRowAnimations(float delta, bool enabled, float rate);
	struct InlineRowContent
	{
		std::function<float(float)> height; // 可用宽度和返回高度均为物理像素。
		std::function<void(const LayoutRect&)> render;
		const char* visibilityId = nullptr; // 可选：只让内联区复用现有条件显隐，父行保持常驻。
		bool visible = true;
	};
	void SettingRow(const char* id, const char* title, const char* description,
		const char* glyph, float actionWidthDip, float actionHeightDip,
		const std::function<void(const LayoutRect&)>& action,
		ImU32 fill = Card, ImU32 border = Stroke, ImTextureID iconTexture = 0,
		ImU32 iconTint = IM_COL32_WHITE, bool keepDescriptionInline = false, const char* note = nullptr, bool showNoteIcon = true, const InlineRowContent* inlineContent = nullptr);
	bool ToggleAction(const LayoutRect& bounds, bool& value);
	bool SliderAction(const LayoutRect& bounds, float& value, float minimum,
		float maximum, bool& active);
	// 数值 + 滑块 + 末尾“重置默认”图标按钮；format 为空时按百分比显示。
	// 返回滑块是否改变；点击重置只置位 reset，由调用方写回默认值。行宽需比普通滑块多留 44 DIP。
	inline constexpr float SliderResetExtraWidthDip = 44.0F;
	bool SliderResetAction(const LayoutRect& bounds, float& value, float minimum,
		float maximum, bool& active, bool& reset, const char* resetTooltip,
		const char* format = nullptr);

	// 控件字体和自然宽度集中在此；size 是物理像素，行测量参数是 DIP。
	bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));
	// 信息块下方的标准复制按钮；复制完整文本，下一项回到原左边缘。
	bool CopyButton(const char* id, const char* label, const char* text);
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
	// 仅显式传入的动画弹层使用此状态；上下避让共用布局，不保存窗口、绘制列表或字体指针。
	struct BelowComboMotion
	{
		NavigationTween reveal;
		std::uintptr_t context = 0;
		std::uint64_t revision = 0, atlas = 0;
		ImGuiID popup = 0;
		int frame = -1, selected = 0;
		float scale = 0.0F, scroll = 0.0F;
		ImVec2 position{}, size{};
		std::vector<std::string> items;
	};
	bool ComboBox(const char* label, int* value, const std::vector<std::string>& items,
		BelowComboMotion* motion = nullptr, std::uint64_t revision = 0);
	bool ComboBox(const char* label, int* value, const char* const items[], int count, ImGuiComboFlags flags = 0,
		BelowComboMotion* motion = nullptr, std::uint64_t revision = 0);
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
		const char* glyph, bool& value, const char* note = nullptr, const InlineRowContent* inlineContent = nullptr);
	// 父行下方“标签 + 右对齐下拉框”内联区；visibilityId 非空时按 visible 复用条件动画收起/展开，
	// 收起期间禁交互、保留选择且不产生事件。返回值引用的参数须存活到对应行提交结束。
	InlineRowContent InlineCombo(const char* id, const char* label, int& value,
		const std::vector<std::string>& items, bool& changed, const char* visibilityId = nullptr, bool visible = true);
	// 父行下方的一组复选框内联区（宽度足够时两列）；visibilityId 非空时随 visible 用条件动画展开/收起。
	// 每项的 value/changed 指针须存活到对应行提交结束；收起期间不产生事件。
	InlineRowContent InlineCheckboxes(const char* id, std::vector<CheckboxSpec> items,
		const char* visibilityId = nullptr, bool visible = true);
	struct PreviewSelectorItem
	{
		std::string label;
		std::function<void(const LayoutRect&)> preview; // 物理像素区域；调用方提供并缓存预览。
	};
	struct PreviewSelectorGroup
	{
		std::vector<PreviewSelectorItem> items;
		int* selectedIndex = nullptr;
		bool* changed = nullptr;
		bool separatorBefore = false;
	};
	// 每组独立选中索引；72 DIP 按钮与真实标签测高共同换行，可复用“跟随默认”等自定义项。
	InlineRowContent InlinePreviewSelector(const char* id, std::vector<PreviewSelectorGroup> groups);
	bool ComboRow(const char* id, const char* title, const char* description,
		const char* glyph, int& value, const std::vector<std::string>& items, const char* note = nullptr, bool showNoteIcon = true);
	// 与 ComboRow 相同，但在下拉框左侧预留一块预览区；preview 收到该区域的物理像素矩形。
	bool ComboPreviewRow(const char* id, const char* title, const char* description,
		const char* glyph, int& value, const std::vector<std::string>& items,
		const std::function<void(const LayoutRect&)>& preview, const char* note = nullptr);
	bool SliderRow(const char* id, const char* title, const char* description,
		const char* glyph, float& value, float minimum, float maximum,
		bool& active, const char* format = "%.2f", const char* note = nullptr);
	bool SliderIntRow(const char* id, const char* title, const char* description,
		const char* glyph, int& value, int minimum, int maximum,
		bool& active, const char* format = "%d", const char* note = nullptr);
	bool ButtonRow(const char* id, const char* title, const char* description,
		const char* glyph, const char* label, bool accent = false, const char* note = nullptr);
	struct ButtonSpec
	{
		const char* id;
		std::string label;
		bool accent = false;
		bool disabled = false;
	};
	int ButtonGroupRow(const char* id, const char* title, const char* description,
		const char* glyph, std::initializer_list<ButtonSpec> buttons, const char* note = nullptr);
	bool NavigationRow(const char* id, const char* title, const char* description,
		const char* glyph, ImTextureID iconTexture = 0, ImU32 iconTint = IM_COL32_WHITE, const char* note = nullptr);
	// 一行文字标签，选中项下方画强调色短下划线；一行放不下时换行。返回是否切换。
	bool TabStrip(const char* id, const std::vector<std::string>& labels, int& selected);
	struct ComponentCardSpec
	{
		const char* title = nullptr;
		const char* description = nullptr;
		const char* glyph = nullptr;
		const char* actionGlyph = nullptr; // 为空时不显示右上角操作键。
		const char* actionTooltip = nullptr;
		bool actionEnabled = true;
	};
	// 组件库卡片网格的尺寸（DIP）：最小宽度、固定高度、间距。标题最多两行，说明按剩余高度显示一到两行。
	inline constexpr float ComponentCardMinimumWidth = 260.0F;
	inline constexpr float ComponentCardHeight = 96.0F;
	inline constexpr float ComponentCardGap = 8.0F;
	// 给定卡片物理宽度时，标题与说明可用的文字宽度（物理像素）；页面与多语言排版测试共用。
	float ComponentCardTextWidth(float cardWidth);
	// 组件库“全部”页的矩形卡片；size 为物理像素。返回右上角操作键是否被点击；
	// dragStarted 非空时，按住卡片其他区域越过拖动阈值的那一帧置为 true。
	bool ComponentCard(const char* id, const ComponentCardSpec& spec, const ImVec2& size, bool* dragStarted = nullptr);
	// 在已绘制的卡片/行区域上覆盖一个无形按钮作为拖动源，越过阈值（鼠标 4、触摸/笔 10 DIP）时返回 true。
	// skip 为 true（操作键正被悬停）时不提交覆盖层，保证操作键可点击；成为活动项后页面拖动滚动不会接管。
	bool DragSourceOverlay(const char* id, const ImVec2& min, const ImVec2& max, bool skip);
	bool Notice(const char* id, const char* title, const char* description,
		ImFluentInfoSeverity severity = ImFluentInfoSeverity_Informational,
		const char* actionLabel = nullptr);
	void ApplyScrollbars();
	// 在渲染线程帧边界调用，不触碰字体、纹理、设备或持久化。
	void ApplyPalette(bool darkMode = false, BackdropMode backdrop = BackdropMode::Solid);
}
