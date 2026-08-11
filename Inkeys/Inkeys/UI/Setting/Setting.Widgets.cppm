module;

#include "Setting.Wrap.h"

export module Inkeys.UI.Setting:Widgets;

import :Base;

namespace Widgets
{
	// 一级封装类：集中维护设置界面的 Fluent 2 浅色调色板和全局样式。
	class FluentColor final
	{
	public:
		static constexpr ImU32 Transparent = IM_COL32(0, 0, 0, 0);
		static constexpr ImU32 White = IM_COL32(255, 255, 255, 255);
		static constexpr ImU32 WindowBackground = IM_COL32(243, 243, 243, 255);
		static constexpr ImU32 CardBackground = IM_COL32(251, 251, 251, 255);
		static constexpr ImU32 PopupBackground = White;
		static constexpr ImU32 Divider = IM_COL32(229, 229, 229, 255);
		static constexpr ImU32 WindowBorder = IM_COL32(189, 189, 189, 255);
		static constexpr ImU32 ControlStroke = IM_COL32(0, 0, 0, 15);

		static constexpr ImU32 TextStrong = IM_COL32(0, 0, 0, 255);
		static constexpr ImU32 TextPrimary = IM_COL32(0, 0, 0, 228);
		static constexpr ImU32 TextSecondary = IM_COL32(120, 120, 120, 255);
		static constexpr ImU32 TextDisabled = IM_COL32(0, 0, 0, 155);
		static constexpr ImU32 TextOnAccent = White;

		static constexpr ImU32 Accent = IM_COL32(0, 95, 184, 255);
		static constexpr ImU32 AccentText = IM_COL32(0, 95, 183, 255);
		static constexpr ImU32 AccentHovered = IM_COL32(0, 95, 184, 230);
		static constexpr ImU32 AccentPressed = IM_COL32(0, 95, 184, 204);
		static constexpr ImU32 Danger = IM_COL32(196, 43, 28, 255);
		static constexpr ImU32 DangerPressed = IM_COL32(200, 60, 49, 255);
		static constexpr ImU32 HeroFill = IM_COL32(236, 241, 255, 0);
		static constexpr ImU32 HeroFillHovered = IM_COL32(236, 241, 255, 30);
		static constexpr ImU32 HeroFillPressed = IM_COL32(236, 241, 255, 60);
		static constexpr ImU32 WarningBackground = IM_COL32(255, 244, 206, 255);
		static constexpr ImU32 WarningText = IM_COL32(157, 93, 0, 255);
		static constexpr ImU32 DangerBackground = IM_COL32(253, 231, 233, 255);
		static constexpr ImU32 SuccessBackground = IM_COL32(223, 246, 221, 255);
		static constexpr ImU32 SuccessText = IM_COL32(15, 123, 15, 255);

		static constexpr ImU32 ControlFill = IM_COL32(255, 255, 255, 179);
		static constexpr ImU32 ControlFillHovered = IM_COL32(249, 249, 249, 128);
		static constexpr ImU32 ControlFillPressed = IM_COL32(249, 249, 249, 77);
		static constexpr ImU32 SubtleFill = IM_COL32(0, 0, 0, 10);
		static constexpr ImU32 SubtleFillPressed = IM_COL32(0, 0, 0, 6);
	};

	class StyleClass
	{
	public:
		void ApplyGlobal(float scrollbarWidth) const;
	};
	extern StyleClass style;

	// 二级封装类：保留现有 WinUI 3 风格，只向调用方暴露控件业务参数。
	class ToggleClass
	{
	public:
		bool ToggleBool(const char* label, bool* state) const;
	};
	extern ToggleClass toggle;

	class ButtonClass
	{
	public:
		bool Standard(const char* label, const ImVec2& size, ImU32 textColor = FluentColor::TextPrimary) const;
		bool Navigation(const char* label, const ImVec2& size, bool selected,
			ImU32 textColor = FluentColor::TextPrimary, const ImVec2& alignment = ImVec2(0.0f, 0.5f)) const;
		bool AccentToggle(const char* label, const ImVec2& size, bool selected) const;
		bool HeroIcon(const char* label, const ImVec2& size) const;
		bool TitleBarClose(const char* label, const ImVec2& size) const;
	};
	extern ButtonClass button;

	class ComboClass
	{
	public:
		bool Begin(const char* label, const char* preview, int itemCount) const;
		bool Selectable(const char* label, bool selected) const;
		void End() const;
	};
	extern ComboClass combo;

	class SliderClass
	{
	public:
		bool Float(const char* label, float* value, float minValue, float maxValue, const char* format = "") const;
		bool Int(const char* label, int* value, int minValue, int maxValue, const char* format = "") const;
	};
	extern SliderClass slider;

	// 三级封装类
	using Encapsulation = variant<ToggleClass>;

	class EntryClass
	{
	public:
		void EntryOneLine(const string& line, const vector<Encapsulation>& vec);
		void EntryTwoLines(const string& line1, const string& line2, const vector<Encapsulation>& vec);
		void EntryMultiLines(const string& line1, const string& text, const vector<Encapsulation>& vec);
	};
}
