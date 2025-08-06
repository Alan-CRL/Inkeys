#pragma once

#include "../../IdtMain.h"

class BarStateClass
{
public:
	IdtAtomic<bool> fold = true;

public:
	void CalcButtomState();
};
extern BarStateClass barState;

class BarStyleClass
{
public:
	IdtAtomic<double> zoom = 2.0;
};
extern BarStyleClass barStyle;

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

class BarButtomStateClass
{
public:
	BarButtomStateClass() {}
public:
	BarWidgetState state = BarWidgetState::None;
	BarWidgetEmphasize emph = BarWidgetEmphasize::None;
};
extern map<int, BarButtomStateClass> barButtomState;