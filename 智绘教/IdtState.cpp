#include "IdtState.h"

#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFloating.h"
#include "IdtPlug-in.h"
#include "IdtWindow.h"

#include "IdtConfiguration.h"

StateModeClass stateMode;

bool SetPenWidth(float targetWidth, bool setMemory)
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

	if (setMemory) SetMemory();

	return true;
}
bool SetPenColor(COLORREF targetColor, bool setMemory)
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

	if (setMemory) SetMemory();

	// 临时方案：改变 UI 背景颜色
	if (computeContrast(targetColor, RGB(255, 255, 255)) >= 3) BackgroundColorMode = 0;
	else BackgroundColorMode = 1;

	return true;
}
bool ChangeStateModeToSelection()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtSelection;

	// 标识绘制等待
	{
		unique_lock<shared_mutex> lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = true;
		lockdrawWaitingSm.unlock();
	}
	// 防止绘图时冲突
	{
		shared_lock lockStrokeImageListSm(StrokeImageListSm);
		bool start = !StrokeImageList.empty();
		lockStrokeImageListSm.unlock();

		// 正在绘制则取消操作
		if (start)
		{
			// 取消标识绘制等待
			{
				unique_lock lockdrawWaitingSm(drawWaitingSm);
				drawWaiting = false;
				lockdrawWaitingSm.unlock();
			}
			return false;
		}
	}

	// 切换状态
	{
		if (!FreezeFrame.select || penetrate.select) FreezeFrame.mode = 0, FreezeFrame.select = false;
		if (penetrate.select) penetrate.select = false;

		if (state == 1.1) state = 1;

		stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
	}
	// TODO 临时方案：改变 UI 背景颜色
	{
		BackgroundColorMode = 0;
	}

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
	return true;
}
bool ChangeStateModeToPen()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtPen;

	// 标识绘制等待
	{
		unique_lock<shared_mutex> lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = true;
		lockdrawWaitingSm.unlock();
	}
	// 防止绘图时冲突
	{
		shared_lock lockStrokeImageListSm(StrokeImageListSm);
		bool start = !StrokeImageList.empty();
		lockStrokeImageListSm.unlock();

		// 正在绘制则取消操作
		if (start)
		{
			// 取消标识绘制等待
			{
				unique_lock lockdrawWaitingSm(drawWaitingSm);
				drawWaiting = false;
				lockdrawWaitingSm.unlock();
			}
			return false;
		}
	}

	// 切换状态
	{
		stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
	}
	// TODO 临时方案：改变 UI 背景颜色
	{
		COLORREF targetColor;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
		{
			if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1) targetColor = stateMode.Pen.Brush1.color;
			else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) targetColor = stateMode.Pen.Highlighter1.color;
		}
		else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		{
			if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1) targetColor = stateMode.Pen.Brush1.color;
			else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1) targetColor = stateMode.Pen.Brush1.color;
		}

		if (computeContrast(targetColor, RGB(255, 255, 255)) >= 3) BackgroundColorMode = 0;
		else BackgroundColorMode = 1;
	}

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtPen;
	return true;
}
bool ChangeStateModeToShape()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtShape;

	// 标识绘制等待
	{
		unique_lock<shared_mutex> lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = true;
		lockdrawWaitingSm.unlock();
	}
	// 防止绘图时冲突
	{
		shared_lock lockStrokeImageListSm(StrokeImageListSm);
		bool start = !StrokeImageList.empty();
		lockStrokeImageListSm.unlock();

		// 正在绘制则取消操作
		if (start)
		{
			// 取消标识绘制等待
			{
				unique_lock lockdrawWaitingSm(drawWaitingSm);
				drawWaiting = false;
				lockdrawWaitingSm.unlock();
			}
			return false;
		}
	}

	// 切换状态
	{
		stateMode.StateModeSelect = StateModeSelectEnum::IdtShape;
	}
	// TODO 临时方案：改变 UI 背景颜色
	{
		COLORREF targetColor;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
		{
			if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1) targetColor = stateMode.Pen.Brush1.color;
			else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) targetColor = stateMode.Pen.Highlighter1.color;
		}
		else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		{
			if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1) targetColor = stateMode.Pen.Brush1.color;
			else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1) targetColor = stateMode.Pen.Brush1.color;
		}

		if (computeContrast(targetColor, RGB(255, 255, 255)) >= 3) BackgroundColorMode = 0;
		else BackgroundColorMode = 1;
	}

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtShape;
	return true;
}
bool ChangeStateModeToEraser()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtEraser;

	// 标识绘制等待
	{
		unique_lock<shared_mutex> lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = true;
		lockdrawWaitingSm.unlock();
	}
	// 防止绘图时冲突
	{
		shared_lock lockStrokeImageListSm(StrokeImageListSm);
		bool start = !StrokeImageList.empty();
		lockStrokeImageListSm.unlock();

		// 正在绘制则取消操作
		if (start)
		{
			// 取消标识绘制等待
			{
				unique_lock lockdrawWaitingSm(drawWaitingSm);
				drawWaiting = false;
				lockdrawWaitingSm.unlock();
			}
			return false;
		}
	}

	// 切换状态
	{
		stateMode.StateModeSelect = StateModeSelectEnum::IdtEraser;
	}
	// TODO 临时方案：改变 UI 背景颜色
	{
		BackgroundColorMode = 0;
	}

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtEraser;
	return true;
}
bool ChangeStateModeToTouchTest()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtTouchTest;

	// 标识绘制等待
	{
		unique_lock<shared_mutex> lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = true;
		lockdrawWaitingSm.unlock();
	}
	// 防止绘图时冲突
	{
		shared_lock lockStrokeImageListSm(StrokeImageListSm);
		bool start = !StrokeImageList.empty();
		lockStrokeImageListSm.unlock();

		// 正在绘制则取消操作
		if (start)
		{
			// 取消标识绘制等待
			{
				unique_lock lockdrawWaitingSm(drawWaitingSm);
				drawWaiting = false;
				lockdrawWaitingSm.unlock();
			}
			return false;
		}
	}

	// 切换状态
	{
		stateMode.StateModeSelect = StateModeSelectEnum::IdtTouchTest;
	}

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtTouchTest;
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

		if (stateMode.StateModeSelect == stateMode.StateModeSelectTarget && stateMode.StateModeSelect == stateMode.StateModeSelectEcho) StateMonitoringManipulated = chrono::high_resolution_clock::now();
		if (chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - StateMonitoringManipulated).count() >= 3000 && !offSignal)
		{
			MessageBox(floating_window, L"There is a problem with the state of the whiteboard, click OK to restart 智绘教Inkeys to try to solve the problem.(#6)\n画板状态出现问题，点击确定重启 智绘教Inkeys 以尝试解决问题。(#6)", L"Inkeys Error | 智绘教错误", MB_OK | MB_SYSTEMMODAL);
			RestartProgram();

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