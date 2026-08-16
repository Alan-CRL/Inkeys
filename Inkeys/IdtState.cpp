#include "IdtState.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtPlug-in.h"
#include "Inkeys/Business/LegacyDrawState.hpp"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

StateModeClass stateMode;

namespace
{
	using Inkeys::Drawing::Draw3::Bridge::ProductState;
	using Inkeys::Drawing::Draw3::Bridge::Tool;

	std::uint32_t ColorRefToRgba(COLORREF color) noexcept
	{
		return (static_cast<std::uint32_t>(GetRValue(color)) << 24) |
			(static_cast<std::uint32_t>(GetGValue(color)) << 16) |
			(static_cast<std::uint32_t>(GetBValue(color)) << 8) | 0xffu;
	}

	Tool CurrentDraw3Tool() noexcept
	{
		if (stateMode.laserActive) return Tool::Laser;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser)
		{
			return Inkeys::Drawing::Draw3::Bridge::NormalizeLegacyEraserMode(
				setlist.eraserSetting.eraserMode) == 1
				? Tool::SpeedEraser
				: Tool::FixedEraser;
		}

		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		{
			switch (stateMode.Shape.ModeSelect)
			{
			case ShapeModeSelectEnum::IdtShapeStraightLine1:
				return Tool::SolidLine;
			case ShapeModeSelectEnum::IdtShapeDashedLine1:
				return Tool::DashedLine;
			case ShapeModeSelectEnum::IdtShapeRectangle1:
				return Tool::OutlineRectangle;
			default:
				return Tool::SolidLine;
			}
		}

		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			return Tool::Highlighter;
		return Tool::Pen;
	}

	void PublishDraw3State() noexcept
	{
		ProductState state{};
		state.tool = CurrentDraw3Tool();
		state.widthDip = (std::max)(0.1f, GetPenWidth());
		state.colorRgba = ColorRefToRgba(GetPenColor());
		state.clickThrough =
			stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection ||
			penetrate.select;
		Inkeys::Drawing::Draw3::PublishProductState(state);
	}
}

void SyncDraw3State()
{
	PublishDraw3State();
}

bool SetPenWidth(float targetWidth, bool setMemory)
{
	if (targetWidth <= 0.0f) return false;
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
			stateMode.Pen.Brush1.width = targetWidth;
		else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			stateMode.Pen.Highlighter1.width = targetWidth;
		else
			return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1 ||
			stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeDashedLine1)
			stateMode.Shape.StraightLine1.width = targetWidth;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			stateMode.Shape.Rectangle1.width = targetWidth;
		else
			return false;
	}
	else
		return false;

	if (setMemory) SetMemory();
	PublishDraw3State();
	return true;
}

bool SetPenColor(COLORREF targetColor, bool setMemory)
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
			stateMode.Pen.Brush1.color = targetColor;
		else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			stateMode.Pen.Highlighter1.color = targetColor;
		else
			return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1 ||
			stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeDashedLine1)
			stateMode.Shape.StraightLine1.color = targetColor;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			stateMode.Shape.Rectangle1.color = targetColor;
		else
			return false;
	}
	else
		return false;

	if (setMemory) SetMemory();
	BackgroundColorMode = computeContrast(targetColor, RGB(255, 255, 255)) >= 3 ? 0 : 1;
	PublishDraw3State();
	return true;
}

float GetPenWidth()
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		return stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
			? stateMode.Pen.Highlighter1.width
			: stateMode.Pen.Brush1.width;
	}
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		return stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1
			? stateMode.Shape.Rectangle1.width
			: stateMode.Shape.StraightLine1.width;
	}
	return stateMode.Pen.Brush1.width;
}

COLORREF GetPenColor()
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		return stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
			? stateMode.Pen.Highlighter1.color
			: stateMode.Pen.Brush1.color;
	}
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		return stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1
			? stateMode.Shape.Rectangle1.color
			: stateMode.Shape.StraightLine1.color;
	}
	return stateMode.Pen.Brush1.color;
}

bool ChangeStateModeToSelection()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
	// selection 必须恢复旧 UI 的定格/穿透状态，同时不再等待 Draw2 绘制全局。
	if (!FreezeFrame.select || penetrate.select)
	{
		FreezeFrame.mode = 0;
		FreezeFrame.select = false;
	}
	if (penetrate.select) penetrate.select = false;
	if (state == 1.1) state = 1;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtSelection;
	stateMode.laserActive = false;
	BackgroundColorMode = 0;
	PublishDraw3State();
	return true;
}

bool ChangeStateModeToPen()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtPen;
	stateMode.laserActive = false;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	PublishDraw3State();
	return true;
}

bool ChangeStateModeToShape()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtShape;
	stateMode.laserActive = false;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	PublishDraw3State();
	return true;
}

bool ChangeStateModeToEraser()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtEraser;
	stateMode.laserActive = false;
	BackgroundColorMode = 0;
	PublishDraw3State();
	return true;
}

bool ChangeStateModeToTouchTest()
{
	// 输入测试尚未接入 Draw3，入口由设置页隐藏。
	return false;
}

void StateMonitoring()
{
	// Draw3 bridge 自己维护状态快照，旧的自动重启监视器不再介入绘制。
	while (!offSignal)
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

bool GetStateMode_Discard(StateModeStruct_Discard* stateModeInfo)
{
	if (!stateModeInfo) return false;
	stateModeInfo->brushWidth = GetPenWidth();
	stateModeInfo->brushColor = GetPenColor();
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
		stateModeInfo->brushMode = stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1 ? 2.0f : 1.0f;
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		stateModeInfo->brushMode = stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1 ? 4.0f : 3.0f;
	else
		return false;
	return true;
}
