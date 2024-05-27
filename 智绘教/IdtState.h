#pragma once

#include "IdtMain.h"

enum StateModeSelectEnum
{
	IdtSelection,
	IdtPen,
	IdtEraser,
	IdtShape
};
enum PenModeSelectEnum
{
	IdtPenBrush1,
	IdtPenHighlighter1
};
enum ShapeModeSelectEnum
{
	IdtShapeStraightLine1,
	IdtShapeDashedLine1,
	IdtShapeTriangle1,
	IdtShapeRectangle1,
	IdtShapeCircle1
};

class StateModeClass
{
public:
	StateModeClass()
	{
		StateModeSelect = StateModeSelectEnum::IdtSelection;
		StateModeSelectEcho = StateModeSelectEnum::IdtSelection;

		{
			Pen.ModeSelect = PenModeSelectEnum::IdtPenBrush1;
			Pen.Brush1.width = 3;
			Pen.Brush1.color = RGBA(50, 30, 181, 255);
			Pen.Highlighter1.width = 35;
			Pen.Highlighter1.color = RGBA(255, 16, 0, 255); // TODO
		}
		{
			Shape.ModeSelect = ShapeModeSelectEnum::IdtShapeStraightLine1;
			Shape.StraightLine1.width = 3;
			Shape.StraightLine1.color = RGBA(50, 30, 181, 255);
			Shape.Rectangle1.width = 3;
			Shape.Rectangle1.color = RGBA(50, 30, 181, 255);
		}
	}

public:
	StateModeSelectEnum StateModeSelect;
	StateModeSelectEnum StateModeSelectEcho;

	struct
	{
		PenModeSelectEnum ModeSelect;

		struct
		{
			float width;
			COLORREF color;
		}Brush1;
		struct
		{
			float width;
			COLORREF color;
		}Highlighter1;
	}Pen;
	struct
	{
		ShapeModeSelectEnum ModeSelect;

		struct
		{
			float width;
			COLORREF color;
		}StraightLine1;
		struct
		{
			float width;
			COLORREF color;
		}Rectangle1;
	}Shape;
};
extern StateModeClass stateMode;

bool SetPenWidth(float targetWidth);
bool SetPenColor(COLORREF targetColor);

struct StateModeStruct_Discard
{
	float brushWidth;
	COLORREF brushColor;

	float brushMode;
	/* 过时设定，后续抛弃
	* 1 画笔
	* 2 荧光笔
	* 3 直线
	* 4 矩形
	*/
};
bool GetStateMode_Discard(StateModeStruct_Discard* stateModeInfo);