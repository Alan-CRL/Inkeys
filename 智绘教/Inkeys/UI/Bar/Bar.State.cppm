module;

#include "../../../IdtMain.h"

#include "../../../IdtDisplayManagement.h"
#include "../../../IdtState.h"

export module Inkeys.UI.Bar.State;

export class BarStateClass
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
	void ThicknessDisplayUpdate();
};
export class BarStyleClass
{
public:
	IdtAtomic<bool> darkStyle = true;
	IdtAtomic<double> zoom = 2.0;
};

// ---

export enum class BarWidgetState : int
{
	None,
	Disable, // 禁用
	Selected // 选中
};
export enum class BarWidgetEmphasize : int
{
	None,
	Hover, // 悬停（废弃）
	Pressed, // 按下
};