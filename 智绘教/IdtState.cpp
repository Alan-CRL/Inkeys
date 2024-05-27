#include "IdtState.h"

#include "IdtDraw.h"
#include "IdtWindow.h"

StateModeClass stateMode;

bool SetPenWidth(float targetWidth)
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1) stateMode.Pen.Brush1.width = targetWidth;
		else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) stateMode.Pen.Highlighter1.width = targetWidth;
		else return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1) stateMode.Pen.Brush1.width = targetWidth;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1) stateMode.Pen.Brush1.width = targetWidth;
		else return false;
	}
	else return false;

	return true;
}
bool SetPenColor(COLORREF targetColor)
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1) stateMode.Pen.Brush1.color = targetColor;
		else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) stateMode.Pen.Highlighter1.color = targetColor;
		else return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1) stateMode.Pen.Brush1.color = targetColor;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1) stateMode.Pen.Brush1.color = targetColor;
		else return false;
	}
	else return false;

	return true;
}

// 状态监测
void StateMonitoring()
{
	// 监测代码暂时不加入安全退出

	chrono::high_resolution_clock::time_point StateMonitoringManipulated = chrono::high_resolution_clock::now();
	while (!offSignal)
	{
		if (penetrate.select)
		{
			while (penetrate.select) this_thread::sleep_for(chrono::milliseconds(100));
			StateMonitoringManipulated = chrono::high_resolution_clock::now();
		}

		if (stateMode.StateModeSelect == stateMode.StateModeSelectEcho) StateMonitoringManipulated = chrono::high_resolution_clock::now();
		if (chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - StateMonitoringManipulated).count() >= 3000 && !offSignal)
		{
			MessageBox(floating_window, L"智绘教画板状态出现问题，点击确定以重启智绘教\n此方案可能解决该问题", L"智绘教状态监测助手", MB_OK | MB_SYSTEMMODAL);
			offSignal = 2;

			return;
		}

		this_thread::sleep_for(chrono::milliseconds(500));
	}
}

bool GetStateMode_Discard(StateModeStruct_Discard* stateModeInfo)
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
		{
			stateModeInfo->brushWidth = stateMode.Pen.Brush1.width;
			stateModeInfo->brushColor = stateMode.Pen.Brush1.color;

			stateModeInfo->brushMode = 1;
		}
		else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
		{
			stateModeInfo->brushWidth = stateMode.Pen.Highlighter1.width;
			stateModeInfo->brushColor = stateMode.Pen.Highlighter1.color;

			stateModeInfo->brushMode = 2;
		}
		else return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1)
		{
			stateModeInfo->brushWidth = stateMode.Pen.Brush1.width;
			stateModeInfo->brushColor = stateMode.Pen.Brush1.color;

			stateModeInfo->brushMode = 3;
		}
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
		{
			stateModeInfo->brushWidth = stateMode.Pen.Brush1.width;
			stateModeInfo->brushColor = stateMode.Pen.Brush1.color;

			stateModeInfo->brushMode = 4;
		}
		else return false;
	}
	else return false;

	return true;
}