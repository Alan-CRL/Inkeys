module;

#include "../../../IdtMain.h"

#include "../../../IdtDisplayManagement.h"
#include "../../../IdtState.h"

export module Inkeys.UI.Bar:State;

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
		// 特别的，左侧和右侧上时，主栏上的控件块会反向

		IdtAtomic<bool> primaryBar; // false 上方， true 下方
		// 特别的，左侧和右侧上时，绘制属性上的控件块会上下反向
	}widgetPosition;

	void PositionUpdate(double tarZoom);
	void ThicknessDisplayUpdate();
};
class BarStyleClass
{
public:
	IdtAtomic<bool> darkStyle = true;
	IdtAtomic<double> zoom = 1.0;
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