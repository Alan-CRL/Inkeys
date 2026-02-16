module;

#include "Setting.Wrap.h"

export module Inkeys.UI.Setting:Widgets;

import :Base;

namespace Widgets
{
	// 一级封装类

	// 二级封装类
	class ToggleClass
	{
	public:
		ToggleClass();

	public:
		void ToggleBool(const char* label, bool state);

	private:
		ImGuiToggleConfig config;
	};
	extern ToggleClass toggle;

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