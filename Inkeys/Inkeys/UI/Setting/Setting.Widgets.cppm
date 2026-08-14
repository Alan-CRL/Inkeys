module;

#include "Setting.Wrap.h"

export module Inkeys.UI.Setting:Widgets;

import :Base;

namespace Widgets
{
	ImU32 Color(ImFluentCol color);
	float Dip(float value);
	void PageHeader(const char* title, const char* description = nullptr,
		bool hero = false);
	void PageContentStart();
	void SectionHeader(const char* title);
	bool BeginSettingsCard(const char* id, const char* header,
		const char* description = nullptr, const char* glyph = nullptr);
	void EndSettingsCard();

	class StyleClass
	{
	public:
		void ApplyGlobal(float scrollbarWidth) const;
	};
	extern StyleClass style;

	class ButtonClass
	{
	public:
		bool TitleBar(const char* label, const ImVec2& size, bool critical = false) const;
	};
	extern ButtonClass button;

	class ComboClass
	{
	public:
		bool Select(const char* label, int* currentItem, const vector<string>& items) const;
	};
	extern ComboClass combo;
}
