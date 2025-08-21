#pragma once

#include "../../IdtMain.h"

class BarStateClass
{
public:
	IdtAtomic<bool> fold = true;
	IdtAtomic<bool> drawAttribute = false;

	struct
	{
		IdtAtomic<bool> brush1Press = false;
		IdtAtomic<bool> highlight1Press = false;
	}drawAttributeBar;
};
class BarStyleClass
{
public:
	IdtAtomic<bool> darkStyle = true;
	IdtAtomic<double> zoom = 2.0;
};

// ---

enum class BarWidgetState : int
{
	None,
	Disable, // 禁用
	Selected // 选中
};
enum class BarWidgetEmphasize : int
{
	None,
	Hover, // 悬停（废弃）
	Pressed, // 按下
};