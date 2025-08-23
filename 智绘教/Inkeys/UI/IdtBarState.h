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

	struct
	{
		IdtAtomic<bool> mainBar; // false 左侧， true 右侧
		IdtAtomic<bool> primaryBar; // false 上方， true 下方
	}widgetPosition;

	void PositionUpdate(double tarZoom);
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