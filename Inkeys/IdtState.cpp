﻿#include "IdtState.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtPlug-in.h"
#include "Inkeys/Business/LegacyDrawState.hpp"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"
#include "Inkeys/Drawing/Draw3/Draw3.PresentationState.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

import Inkeys.UI.Bar;
import Inkeys.Window;

StateModeClass stateMode;

namespace
{
	using Inkeys::Drawing::Draw3::Bridge::ProductState;
	using Inkeys::Drawing::Draw3::Bridge::Tool;
	std::mutex draw3PresentationMutex;

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
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
			return Tool::HardPen;
		return Tool::Pen;
	}

	void PublishDraw3State() noexcept
	{
		ProductState state{};
		state.tool = CurrentDraw3Tool();
		state.widthDip = (std::max)(0.1f, GetPenWidth());
		state.colorRgba = ColorRefToRgba(GetPenColor());
		state.selectionMode =
			stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection;
		Inkeys::Drawing::Draw3::PublishProductState(state);
	}
}

void SyncDraw3State()
{
	PublishDraw3State();
	ReconcileDraw3Presentation();
}

void ReconcileDraw3Presentation()
{
	std::scoped_lock lock(draw3PresentationMutex);
	const auto runtime = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
	const bool selectionMode = runtime.selectionMode;
	auto& service = Inkeys::Window::GetService();
	using Inkeys::Window::WindowRole;

	Inkeys::UI::Bar::SetCurrentPageHasContent(runtime.currentPageHasContent);
	const auto expectedTarget = selectionMode
		? Inkeys::Drawing::Draw3::HostOutputTarget::SelectionUlw
		: Inkeys::Drawing::Draw3::HostOutputTarget::PrimaryDrawpad;
	const bool targetReady = runtime.requestedOutputTarget == expectedTarget &&
		runtime.readyOutputTarget == expectedTarget &&
		runtime.readyOutputRevision == runtime.requestedOutputRevision &&
		runtime.presentedContentRevision == runtime.contentRevision;
	if (!runtime.firstFrameReady) return;
	// 离开选择后先让主 Drawpad 接管输入，避免等待预热帧时首个 Down 穿到下层窗口。
	// 选择态仍必须等待辅助 ULW 内容就绪，防止旧帧或双窗 alpha 叠加。
	if (selectionMode && !targetReady) return;

	Inkeys::Window::DrawpadSurfaceVisibility visibility;
	switch (Inkeys::Drawing::Draw3::ResolveDrawpadPresentationSurface(
		selectionMode, runtime.currentPageHasContent,
		runtime.auxiliaryFullFrameClean))
	{
	case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Primary:
		visibility = Inkeys::Window::DrawpadSurfaceVisibility::Primary; break;
	case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Presentation:
		visibility = Inkeys::Window::DrawpadSurfaceVisibility::Presentation; break;
	case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Hidden:
	default:
		visibility = Inkeys::Window::DrawpadSurfaceVisibility::Hidden; break;
	}
	(void)service.SetDrawpadSurfaceVisibility(visibility);

	const HWND drawpad = service.Handle(WindowRole::Drawpad);
	const HWND presentation = service.Handle(WindowRole::DrawpadPresentation);
	IdtWindowsIsVisible.drawpadWindow = drawpad && presentation &&
		service.Ready(WindowRole::Drawpad) &&
		service.Ready(WindowRole::DrawpadPresentation) &&
		Inkeys::Drawing::Draw3::ProductFirstFrameReady();
}

bool SetPenWidth(float targetWidth, bool setMemory)
{
	if (targetWidth <= 0.0f) return false;
	// laserActive 覆盖残留 Pen.ModeSelect，避免改写底层笔型的记忆粗细。
	if (stateMode.laserActive)
		stateMode.Pen.Laser.width = targetWidth;
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenSoftPen ||
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
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
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenSoftPen ||
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
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
	if (stateMode.laserActive) return stateMode.Pen.Laser.width;
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

float GetEffectivePenOpacity()
{
	// Laser 不复用已记忆的 Pen.ModeSelect；当前只有荧光笔使用固定半透明合成。
	return !stateMode.laserActive &&
		stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
		stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
		? Inkeys::Drawing::Draw3::Bridge::kHighlighterCompositeOpacity : 1.0f;
}

bool ChangeStateModeToSelection()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
	// selection 保留由定格按钮显式进入的定格状态。
	if (!FreezeFrame.select)
	{
		FreezeFrame.mode = 0;
		FreezeFrame.select = false;
	}
	if (state == 1.1) state = 1;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtSelection;
	stateMode.laserActive = false;
	BackgroundColorMode = 0;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToPen()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtPen;
	stateMode.laserActive = false;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToShape()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtShape;
	stateMode.laserActive = false;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToEraser()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtEraser;
	stateMode.laserActive = false;
	BackgroundColorMode = 0;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToTouchTest()
{
	// 输入测试尚未接入 Draw3，入口由设置页隐藏。
	return false;
}

void StateMonitoring()
{
	auto snapshot = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
	std::uint64_t revision = snapshot.runtimeRevision;
	ReconcileDraw3Presentation();
	while (!offSignal)
	{
		// 内容、目标就绪和全帧 clean 共享一个 revision，避免快速切换漏掉握手。
		(void)Inkeys::Drawing::Draw3::WaitForProductRuntimeRevision(revision, 250);
		if (offSignal) break;
		snapshot = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
		if (snapshot.runtimeRevision == revision) continue;
		revision = snapshot.runtimeRevision;
		ReconcileDraw3Presentation();
	}
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
