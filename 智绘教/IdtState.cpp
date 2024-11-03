#include "IdtState.h"

#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtPlug-in.h"
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
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1) stateMode.Shape.Rectangle1.color = stateMode.Pen.Brush1.color = targetColor;
		else return false;
	}
	else return false;

	return true;
}
bool ChangeStateModeToSelection()
{
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

		stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
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

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
	return true;
}
bool ChangeStateModeToShape()
{
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

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
	return true;
}
bool ChangeStateModeToEraser()
{
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

	// 取消标识绘制等待
	{
		unique_lock lockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		lockdrawWaitingSm.unlock();
	}
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