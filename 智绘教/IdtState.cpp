#include "IdtState.h"

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
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1) stateMode.Shape.StraightLine1.width = targetWidth;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1) stateMode.Shape.Rectangle1.width = targetWidth;
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
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1) stateMode.Shape.StraightLine1.color = targetColor;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1) stateMode.Shape.Rectangle1.color = targetColor;
		else return false;
	}
	else return false;

	return true;
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
			stateModeInfo->brushWidth = stateMode.Shape.StraightLine1.width;
			stateModeInfo->brushColor = stateMode.Shape.StraightLine1.color;

			stateModeInfo->brushMode = 3;
		}
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
		{
			stateModeInfo->brushWidth = stateMode.Shape.Rectangle1.width;
			stateModeInfo->brushColor = stateMode.Shape.Rectangle1.color;

			stateModeInfo->brushMode = 4;
		}
		else return false;
	}
	else return false;

	return true;
}