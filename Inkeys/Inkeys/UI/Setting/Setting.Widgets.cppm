module;

#include "Setting.Wrap.h"

export module Inkeys.UI.Setting:Widgets;

import :Base;

namespace Widgets
{
	ImU32 Color(ImFluentCol color);
	float Dip(float value);

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

}
