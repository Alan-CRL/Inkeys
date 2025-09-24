#pragma once

#include "IdtMain.h"

enum class StateModeSelectEnum
{
	IdtSelection,
	IdtPen,
	IdtEraser,
	IdtShape,

	IdtTouchTest
};
enum class PenModeSelectEnum
{
	IdtPenBrush1,
	IdtPenHighlighter1
};
enum class ShapeModeSelectEnum
{
	IdtShapeStraightLine1,
	IdtShapeDashedLine1,
	IdtShapeTriangle1,
	IdtShapeRectangle1,
	IdtShapeCircle1
};

// 当前情况下 直线 和 矩形 绘制的样式跟随画笔

class StateModeClass
{
public:
	StateModeClass()
	{
		StateModeSelect = StateModeSelectEnum::IdtSelection;
		StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
		StateModeSelectEcho = StateModeSelectEnum::IdtSelection;

		{
			Pen.ModeSelect = PenModeSelectEnum::IdtPenBrush1;
			Pen.Brush1.width = Pen.Brush1.widthPreset = 3;
			Pen.Brush1.color = RGBA(255, 16, 0, 255);
			Pen.Highlighter1.width = Pen.Highlighter1.widthPreset = 35;
			Pen.Highlighter1.color = RGBA(255, 16, 0, 255); // TODO 后续添加透明度选项
		}
		{
			Shape.ModeSelect = ShapeModeSelectEnum::IdtShapeStraightLine1;
			Shape.StraightLine1.width = 3;
			Shape.StraightLine1.color = RGBA(255, 16, 0, 255);
			Shape.Rectangle1.width = 3;
			Shape.Rectangle1.color = RGBA(255, 16, 0, 255);
		}
	}

public:
	StateModeSelectEnum StateModeSelect;
	StateModeSelectEnum StateModeSelectTarget;
	StateModeSelectEnum StateModeSelectEcho;

	struct
	{
		PenModeSelectEnum ModeSelect;

		struct
		{
			float width;
			COLORREF color;

			float widthPreset;
		}Brush1;
		struct
		{
			float width;
			COLORREF color;

			float widthPreset;
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

bool SetPenWidth(float targetWidth, bool setMemory = true);
bool SetPenColor(COLORREF targetColor, bool setMemory = true);
bool ChangeStateModeToSelection();
bool ChangeStateModeToPen();
bool ChangeStateModeToShape();
bool ChangeStateModeToEraser();
bool ChangeStateModeToTouchTest();

void StateMonitoring();

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