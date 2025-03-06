#include "IdtDrawpad.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtFloating.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
#include "IdtState.h"
#include "IdtText.h"
#include "IdtTime.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

#include <queue>

bool main_open;
bool FirstDraw = true;
bool IdtHotkey;

StrokeImageClass strokeImage;

shared_mutex StrokeImageListSm;
vector<StrokeImageClass*> StrokeImageList;

shared_mutex StrokeBackImageSm;

bool drawWaiting; // 绘制等待：启用标识时暂时停止绘制
shared_mutex drawWaitingSm;

IMAGE drawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //主画板
IMAGE window_background(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

unordered_map<BYTE, bool> KeyBoradDown;
HHOOK DrawpadHookCall;
bool IsHotkeyDown;
LRESULT CALLBACK DrawpadHookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_KEYUP || wParam == WM_SYSKEYDOWN || wParam == WM_SYSKEYUP))
	{
		KBDLLHOOKSTRUCT* pKeyInfo = (KBDLLHOOKSTRUCT*)lParam;

		if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) KeyBoradDown[(BYTE)pKeyInfo->vkCode] = true;
		else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) KeyBoradDown[(BYTE)pKeyInfo->vkCode] = false;

		if (!IsHotkeyDown && (KeyBoradDown[VK_CONTROL] || KeyBoradDown[VK_LCONTROL] || KeyBoradDown[VK_RCONTROL]) && (KeyBoradDown[VK_LWIN] || KeyBoradDown[VK_RWIN]) && (KeyBoradDown[VK_MENU] || KeyBoradDown[VK_LMENU] || KeyBoradDown[VK_RMENU]))
		{
			IsHotkeyDown = true;

			if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) ChangeStateModeToPen();
			else ChangeStateModeToSelection();
		}
		else if (IsHotkeyDown && !(KeyBoradDown[VK_CONTROL] || KeyBoradDown[VK_LCONTROL] || KeyBoradDown[VK_RCONTROL]) && !(KeyBoradDown[VK_LWIN] || KeyBoradDown[VK_RWIN]) && !(KeyBoradDown[VK_MENU] || KeyBoradDown[VK_LMENU] || KeyBoradDown[VK_RMENU])) IsHotkeyDown = false;

		// 按键反馈
		if (ppt_show != NULL && !CheckEndShow.isChecking)
		{
			// 检查按下的键
			switch (pKeyInfo->vkCode)
			{
			case VK_SPACE:  // 空格键
			case VK_NEXT:   // PgDn
			case VK_RIGHT:  // 右箭头
			case VK_DOWN:   // 下箭头
			case VK_RETURN: // Enter
			{
				if (KeyBoradDown[(BYTE)pKeyInfo->vkCode])
				{
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
				}
				break;
			}

			case VK_PRIOR:  // PgUp
			case VK_LEFT:   // 左箭头
			case VK_UP:     // 上箭头
			case VK_BACK:   // Backsapce
			{
				if (KeyBoradDown[(BYTE)pKeyInfo->vkCode])
				{
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
				}
				break;
			}

			case VK_ESCAPE: // ESC
			{
				break;
			}
			}
		}
		// 传递拦截
		if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && !penetrate.select)
		{
			ExMessage msgKey = {};
			msgKey.message = wParam;
			msgKey.vkcode = (BYTE)pKeyInfo->vkCode;

			// 借用结构：是否按下 ctrl
			msgKey.prevdown = (KeyBoradDown[VK_CONTROL] || KeyBoradDown[VK_LCONTROL] || KeyBoradDown[VK_RCONTROL]);

			int index = hiex::GetWindowIndex(drawpad_window, false);
			unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
			hiex::g_vecWindows[index].vecMessage.push_back(msgKey);
			lg_vecWindows_vecMessage_sm.unlock();

			if (ppt_show != NULL && !CheckEndShow.isChecking)
			{
				switch (pKeyInfo->vkCode)
				{
				case 0x30:
				case 0x31:
				case 0x32:
				case 0x33:
				case 0x34:
				case 0x35:
				case 0x36:
				case 0x37:
				case 0x38:
				case 0x39:
				case 0x41:
				case 0x42:
				case 0x43:
				case 0x44:
				case 0x45:
				case 0x46:
				case 0x47:
				case 0x48:
				case 0x49:
				case 0x4A:
				case 0x4B:
				case 0x4C:
				case 0x4D:
				case 0x4E:
				case 0x4F:
				case 0x50:
				case 0x51:
				case 0x52:
				case 0x53:
				case 0x54:
				case 0x55:
				case 0x56:
				case 0x57:
				case 0x58:
				case 0x59:
				case 0x5A:
				case VK_NUMPAD0:
				case VK_NUMPAD1:
				case VK_NUMPAD2:
				case VK_NUMPAD3:
				case VK_NUMPAD4:
				case VK_NUMPAD5:
				case VK_NUMPAD6:
				case VK_NUMPAD7:
				case VK_NUMPAD8:
				case VK_NUMPAD9:

				case VK_NEXT:   // PgDn
				case VK_PRIOR:  // PgUp
				case VK_SPACE:  // 空格键
				case VK_LEFT:   // 左箭头
				case VK_RIGHT:  // 右箭头
				case VK_UP:     // 上箭头
				case VK_DOWN:   // 下箭头
				case VK_BACK:   // Backsapce
				case VK_RETURN: // Enter
				case VK_ESCAPE:	// 退出

				{
					return 1;
				}

				default:
					break;
				}
			}
			else
			{
				switch (pKeyInfo->vkCode)
				{
				case 0x30:
				case 0x31:
				case 0x32:
				case 0x33:
				case 0x34:
				case 0x35:
				case 0x36:
				case 0x37:
				case 0x38:
				case 0x39:
				case 0x41:
				case 0x42:
				case 0x43:
				case 0x44:
				case 0x45:
				case 0x46:
				case 0x47:
				case 0x48:
				case 0x49:
				case 0x4A:
				case 0x4B:
				case 0x4C:
				case 0x4D:
				case 0x4E:
				case 0x4F:
				case 0x50:
				case 0x51:
				case 0x52:
				case 0x53:
				case 0x54:
				case 0x55:
				case 0x56:
				case 0x57:
				case 0x58:
				case 0x59:
				case 0x5A:
				case VK_NUMPAD0:
				case VK_NUMPAD1:
				case VK_NUMPAD2:
				case VK_NUMPAD3:
				case VK_NUMPAD4:
				case VK_NUMPAD5:
				case VK_NUMPAD6:
				case VK_NUMPAD7:
				case VK_NUMPAD8:
				case VK_NUMPAD9:
				{
					return 1;
				}

				default:
					break;
				}
			}
		}

		// 定格所需的额外情况（禁用，否则用户会无法使用 Ctrl + Q 快捷键：后续提供选项，让可以全局指定）
		/*
		else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection && (KeyBoradDown[VK_CONTROL] || KeyBoradDown[VK_LCONTROL] || KeyBoradDown[VK_RCONTROL]) && (BYTE)pKeyInfo->vkCode == (BYTE)0x51)
		{
			ExMessage msgKey = {};
			msgKey.message = wParam;
			msgKey.vkcode = (BYTE)0x51;
			msgKey.prevdown = true; // 借用结构：按下 ctrl

			int index = hiex::GetWindowIndex(drawpad_window, false);
			unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
			hiex::g_vecWindows[index].vecMessage.push_back(msgKey);
			lg_vecWindows_vecMessage_sm.unlock();

			return 1;
		}
		*/
		// 穿透所需的额外情况
		else if (penetrate.select && (KeyBoradDown[VK_CONTROL] || KeyBoradDown[VK_LCONTROL] || KeyBoradDown[VK_RCONTROL]) && (BYTE)pKeyInfo->vkCode == (BYTE)0x45)
		{
			ExMessage msgKey = {};
			msgKey.message = wParam;
			msgKey.vkcode = (BYTE)0x45;
			msgKey.prevdown = true; // 借用结构：按下 ctrl

			int index = hiex::GetWindowIndex(drawpad_window, false);
			unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
			hiex::g_vecWindows[index].vecMessage.push_back(msgKey);
			lg_vecWindows_vecMessage_sm.unlock();

			return 1;
		}
	}

	// 继续传递事件给下一个钩子或目标窗口
	return CallNextHookEx(DrawpadHookCall, nCode, wParam, lParam);
}
void DrawpadInstallHook()
{
	// 安装钩子
	DrawpadHookCall = SetWindowsHookEx(WH_KEYBOARD_LL, DrawpadHookCallback, NULL, 0);
	if (DrawpadHookCall == NULL) return;

	MSG msg;
	while (!offSignal && GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// 卸载钩子
	UnhookWindowsHookEx(DrawpadHookCall);
}
void KeyboardInteraction()
{
	ExMessage m;
	while (!offSignal)
	{
		hiex::getmessage_win32(&m, EM_KEY, drawpad_window);

		if (PptInfoState.TotalPage != -1)
		{
			if (m.message == WM_KEYDOWN && (m.vkcode == VK_DOWN || m.vkcode == VK_RIGHT || m.vkcode == VK_NEXT || m.vkcode == VK_SPACE || m.vkcode == VK_UP || m.vkcode == VK_LEFT || m.vkcode == VK_PRIOR || m.vkcode == VK_BACK || m.vkcode == VK_RETURN))
			{
				auto vkcode = m.vkcode;

				if (vkcode == VK_UP || vkcode == VK_LEFT || vkcode == VK_PRIOR || vkcode == VK_BACK)
				{
					// 上一页
					SetForegroundWindow(ppt_show);

					PreviousPptSlides();

					std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
					while (1)
					{
						if (!KeyBoradDown[vkcode]) break;
						if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400) PreviousPptSlides();

						this_thread::sleep_for(chrono::milliseconds(15));
					}
				}
				else
				{
					// 下一页
					int temp_currentpage = PptInfoState.CurrentPage;
					if (temp_currentpage == -1 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
					{
						if (CheckEndShow.Check())
						{
							ChangeStateModeToSelection();
							EndPptShow();
						}
					}
					else if (temp_currentpage == -1)
					{
						EndPptShow();
					}
					else
					{
						SetForegroundWindow(ppt_show);

						NextPptSlides(temp_currentpage);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!KeyBoradDown[vkcode]) break;

							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								temp_currentpage = PptInfoState.CurrentPage;
								if (temp_currentpage == -1 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
								{
									if (CheckEndShow.Check())
									{
										ChangeStateModeToSelection();
										EndPptShow();
									}
									break;
								}
								else if (temp_currentpage == -1) break;

								NextPptSlides(temp_currentpage);
							}

							this_thread::sleep_for(chrono::milliseconds(15));
						}
					}
				}
			}
			else if (m.message == WM_KEYDOWN && m.vkcode == VK_ESCAPE)
			{
				auto vkcode = m.vkcode;

				while (KeyBoradDown[vkcode]) this_thread::sleep_for(chrono::milliseconds(20));

				if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
				{
					if (CheckEndShow.Check())
					{
						ChangeStateModeToSelection();
						EndPptShow();
					}
				}
				else EndPptShow();
			}
		}

		// 定格 Q
		if (m.vkcode == (BYTE)0x51 && m.prevdown && m.message == WM_KEYDOWN)
		{
			while (1)
			{
				if (!KeyBoradDown[(BYTE)0x51])
				{
					if (FreezeFrame.mode != 1)
					{
						FreezeFrame.mode = 1;
						penetrate.select = false;

						if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection) FreezeFrame.select = true;
					}
					else
					{
						FreezeFrame.mode = 0;
						FreezeFrame.select = false;
					}

					break;
				}

				this_thread::sleep_for(chrono::milliseconds(10));
			}
		}
		// 穿透 E
		if (m.vkcode == (BYTE)0x45 && m.prevdown && m.message == WM_KEYDOWN)
		{
			while (1)
			{
				if (!KeyBoradDown[(BYTE)0x45])
				{
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection)
					{
						if (penetrate.select)
						{
							penetrate.select = false;
							if (FreezeFrame.mode == 2) FreezeFrame.mode = 1;
						}
						else
						{
							if (FreezeFrame.mode == 1) FreezeFrame.mode = 2;
							penetrate.select = true;
						}
					}

					break;
				}

				this_thread::sleep_for(chrono::milliseconds(10));
			}
		}

		// 撤回 Z
		if (m.vkcode == (BYTE)0x5A && m.message == WM_KEYDOWN)
		{
			while (1)
			{
				if (!KeyBoradDown[(BYTE)0x5A])
				{
					if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && (!RecallImage.empty() || (!FirstDraw && RecallImagePeak == 0))) IdtRecall();
					else if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && RecallImage.empty() && current_record_pointer <= total_record_pointer + 1 && practical_total_record_pointer) IdtRecovery();

					break;
				}

				this_thread::sleep_for(chrono::milliseconds(10));
			}
		}

		hiex::flushmessage_win32(EM_KEY, drawpad_window);
	}
}

// 落笔预备
shared_mutex prepareCanvasQueueSm;
queue<IMAGE*> prepareCanvasQueue;
void PrepareCanvas(int width, int height)
{
	for (;;)
	{
		shared_lock<shared_mutex> lockPrepareCanvasQueue1(prepareCanvasQueueSm);
		int queueSize = prepareCanvasQueue.size();
		lockPrepareCanvasQueue1.unlock();
		if (queueSize >= setlist.performanceSetting.preparationQuantity) break;

		IMAGE* canvas = new IMAGE(width, height);
		SetImageColor(*canvas, RGBA(0, 0, 0, 0), true);

		unique_lock<shared_mutex> lockPrepareCanvasQueue2(prepareCanvasQueueSm);
		prepareCanvasQueue.push(canvas);
		lockPrepareCanvasQueue2.unlock();
	}
}
void ResetPrepareCanvas()
{
	shared_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
	int width = MainMonitor.MonitorWidth;
	int height = MainMonitor.MonitorHeight;
	DisplaysInfoLock.unlock();

	unique_lock<shared_mutex> lockPrepareCanvasQueue1(prepareCanvasQueueSm);
	while (prepareCanvasQueue.size() != setlist.performanceSetting.preparationQuantity)
	{
		if (prepareCanvasQueue.size() > setlist.performanceSetting.preparationQuantity)
		{
			delete prepareCanvasQueue.front();
			prepareCanvasQueue.pop();
		}
		else
		{
			IMAGE* canvas = new IMAGE(width, height);
			SetImageColor(*canvas, RGBA(0, 0, 0, 0), true);

			prepareCanvasQueue.push(canvas);
		}
	}
	lockPrepareCanvasQueue1.unlock();
}

void MultiFingerDrawing(LONG pid, TouchMode initialMode, StateModeClass stateInfo, StateModeSelectEnum stateModeSelect)
{
	struct
	{
		int width;
		int height;
	} screenInfo;
	{
		shared_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
		screenInfo.width = MainMonitor.MonitorWidth;
		screenInfo.height = MainMonitor.MonitorHeight;
		DisplaysInfoLock.unlock();
	}

	struct
	{
		int x, y;
		int previousX, previousY;
	} pointInfo;
	{
		pointInfo.x = 0;
		pointInfo.y = 0;
		pointInfo.previousX = initialMode.pt.x;
		pointInfo.previousY = initialMode.pt.y;
	}

	IMAGE* Canvas = nullptr;
	{
		unique_lock<shared_mutex> lockPrepareCanvasQueue1(prepareCanvasQueueSm);
		if (!prepareCanvasQueue.empty())
		{
			Canvas = prepareCanvasQueue.front();
			prepareCanvasQueue.pop();
			lockPrepareCanvasQueue1.unlock();

			if (Canvas->getwidth() != screenInfo.width || Canvas->getheight() != screenInfo.height)
			{
				Canvas->Resize(screenInfo.width, screenInfo.height);
				SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
			}
		}
		else
		{
			lockPrepareCanvasQueue1.unlock();

			Canvas = new IMAGE(screenInfo.width, screenInfo.height);
			SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
		}
		thread(PrepareCanvas, screenInfo.width, screenInfo.height).detach();
		if (Canvas == nullptr) return;
	}

	// 绘制画布定义
	TouchMode mode;
	Graphics graphics(GetImageHDC(Canvas));
	graphics.SetSmoothingMode(SmoothingModeHighQuality);

	// 绘制队列
	StrokeImageClass* multiStrokeImage = new StrokeImageClass;
	multiStrokeImage->canvas = Canvas;

	if (stateModeSelect == StateModeSelectEnum::IdtPen)
	{
		double accurateWritingDistance = 0;
		int instantWritingDistance = 0;
		RECT inkTangentRectangle = { -1,-1,-1,-1 };

		vector<Point> actualPoints = { Point(pointInfo.previousX, pointInfo.previousY) }; // 实际点集

		//首次绘制（后续修改，即画点）
		{
			if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
			{
				hiex::EasyX_Gdiplus_SolidEllipse(float((float)pointInfo.previousX - stateInfo.Pen.Brush1.width / 2.0), float((float)pointInfo.previousY - stateInfo.Pen.Brush1.width / 2.0), stateInfo.Pen.Brush1.width, stateInfo.Pen.Brush1.width, stateInfo.Pen.Brush1.color, false, SmoothingModeHighQuality, Canvas);
			}
			else if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			{
				hiex::EasyX_Gdiplus_SolidEllipse(float((float)pointInfo.previousX - stateInfo.Pen.Highlighter1.width / 2.0), float((float)pointInfo.previousY - stateInfo.Pen.Highlighter1.width / 2.0), stateInfo.Pen.Highlighter1.width, stateInfo.Pen.Highlighter1.width, stateInfo.Pen.Highlighter1.color, false, SmoothingModeHighQuality, Canvas);
			}
		}

		// 进入绘制刷新队列
		{
			if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) multiStrokeImage->alpha = 130;
			else multiStrokeImage->alpha = 255;

			unique_lock lockStrokeImageListSm(StrokeImageListSm);
			StrokeImageList.emplace_back(multiStrokeImage);
			lockStrokeImageListSm.unlock();
		}

		// 待修改
		POINT StopTimingPoint = { -1,-1 };
		bool StopTimingDisable = !setlist.waitStraighten;
		chrono::high_resolution_clock::time_point StopTiming = std::chrono::high_resolution_clock::now();

		clock_t tRecord = clock();
		while (1)
		{
			//auto start = std::chrono::high_resolution_clock::now();

			// 确认触摸点存在
			{
				shared_lock<shared_mutex> lockPointPosSm(touchPosSm);
				{
					if (TouchPos.find(pid) == TouchPos.end())
					{
						lockPointPosSm.unlock();
						break;
					}
					mode = TouchPos[pid];
				}
				lockPointPosSm.unlock();

				shared_lock lockPointListSm(pointListSm);
				auto it = std::find(TouchList.begin(), TouchList.end(), pid);
				lockPointListSm.unlock();
				if (it == TouchList.end()) break;
			}
			// 停留拉直
			if (!StopTimingDisable && stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1 && !actualPoints.empty())
			{
				if (sqrt((mode.pt.x - StopTimingPoint.x) * (mode.pt.x - StopTimingPoint.x) + (mode.pt.y - StopTimingPoint.y) * (mode.pt.y - StopTimingPoint.y)) > stopTimingError)
				{
					StopTimingPoint = mode.pt;
					StopTiming = chrono::high_resolution_clock::now();
				}
				else if (chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - StopTiming).count() >= 600)
				{
					double distance = EuclideanDistanceP(actualPoints.front(), actualPoints.back());

					double redundance = max(15.0 * drawingScale, (distance / drawingScale * 0.03409 + 10.9092) * drawingScale) * 2.0f; // 降低精度
					if (distance / drawingScale >= 120 && (abs(inkTangentRectangle.left - inkTangentRectangle.right) / drawingScale >= 120 || abs(inkTangentRectangle.top - inkTangentRectangle.bottom) / drawingScale >= 120) && isLine(actualPoints, redundance, drawingScale, std::chrono::high_resolution_clock::now()))
					{
						stateInfo.StateModeSelect = StateModeSelectEnum::IdtShape;
						stateInfo.Shape.ModeSelect = ShapeModeSelectEnum::IdtShapeStraightLine1;
						stateInfo.Shape.StraightLine1.width = stateInfo.Pen.Brush1.width;
						stateInfo.Shape.StraightLine1.color = stateInfo.Pen.Brush1.color;
						pointInfo.previousX = actualPoints[0].X, pointInfo.previousY = actualPoints[0].Y;

						goto DelayStraighteningTarget;
					}
					else StopTimingDisable = true;
				}
			}

			// 过滤未动触摸点
			{
				if (mode.pt.x == pointInfo.previousX && mode.pt.y == pointInfo.previousY)
				{
					this_thread::sleep_for(chrono::milliseconds(1));
					continue;
				}
			}

			// 绘制
			{
				Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
				pen.SetEndCap(LineCapRound);
				if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
				{
					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
					pen.SetWidth(stateInfo.Pen.Brush1.width);
				}
				else if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
				{
					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Highlighter1.color, false));
					pen.SetWidth(stateInfo.Pen.Highlighter1.width);
				}

				// 绘制
				//unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
				graphics.DrawLine(&pen, pointInfo.previousX, pointInfo.previousY, (pointInfo.x = mode.pt.x), (pointInfo.y = mode.pt.y));
				//lockMultiStrokeImage.unlock();

				// 绘制计算
				{
					accurateWritingDistance += EuclideanDistance({ pointInfo.previousX, pointInfo.previousY }, { mode.pt.x, mode.pt.y });

					if (pointInfo.x < inkTangentRectangle.left || inkTangentRectangle.left == -1) inkTangentRectangle.left = pointInfo.x;
					if (pointInfo.y < inkTangentRectangle.top || inkTangentRectangle.top == -1) inkTangentRectangle.top = pointInfo.y;
					if (pointInfo.x > inkTangentRectangle.right || inkTangentRectangle.right == -1) inkTangentRectangle.right = pointInfo.x;
					if (pointInfo.y > inkTangentRectangle.bottom || inkTangentRectangle.bottom == -1) inkTangentRectangle.bottom = pointInfo.y;

					instantWritingDistance += (int)EuclideanDistance({ pointInfo.previousX, pointInfo.previousY }, { mode.pt.x, mode.pt.y });
					if (instantWritingDistance >= 4)
					{
						actualPoints.push_back(Point(pointInfo.x, pointInfo.y));
						instantWritingDistance %= 4;
					}

					pointInfo.previousX = pointInfo.x, pointInfo.previousY = pointInfo.y;
				}
			}

			//auto end = std::chrono::high_resolution_clock::now();
			//auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
			//std::cout << duration.count() << "ns" << std::endl;
		}

		//智能绘图模块
		{
			if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
			{
				//直线绘制
				double distance = EuclideanDistanceP(actualPoints.front(), actualPoints.back());

				double redundance = max(15.0 * drawingScale, (distance / drawingScale * 0.03409 + 10.9092) * drawingScale);
				if (setlist.liftStraighten && distance / drawingScale >= 120 && (abs(inkTangentRectangle.left - inkTangentRectangle.right) / drawingScale >= 120 || abs(inkTangentRectangle.top - inkTangentRectangle.bottom) / drawingScale >= 120) && isLine(actualPoints, redundance, drawingScale, std::chrono::high_resolution_clock::now()))
				{
					Point start(actualPoints[0]), end(actualPoints[actualPoints.size() - 1]);

					// 智能绘图：端点匹配
					if (setlist.pointAdsorption)
					{
						//起点匹配
						{
							Point start_target = start;
							double distance = 10 * drawingScale;

							std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { start.X,start.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { start.X,start.Y });
										start_target = { point.first,point.second };
									}
								}
							}
							LockExtremePointSm.unlock();

							start = start_target;
						}
						//终点匹配
						{
							Point end_target = end;
							double distance = 10 * drawingScale;

							std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { end.X,end.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { end.X,end.Y });
										end_target = { point.first,point.second };
									}
								}
							}
							LockExtremePointSm.unlock();

							end = end_target;
						}
					}

					std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
					extreme_point[{start.X, start.Y}] = extreme_point[{end.X, end.Y}] = true;
					LockExtremePointSm.unlock();

					Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);

					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
					pen.SetWidth(stateInfo.Pen.Brush1.width);

					unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
					SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
					graphics.DrawLine(&pen, start.X, start.Y, end.X, end.Y);
					lockMultiStrokeImage.unlock();
				}

				//平滑曲线
				else if (setlist.smoothWriting && actualPoints.size() > 2)
				{
					Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
					pen.SetLineJoin(LineJoinRound);
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);

					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
					pen.SetWidth(stateInfo.Pen.Brush1.width);

					unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
					SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
					graphics.DrawCurve(&pen, actualPoints.data(), actualPoints.size(), 0.4f);
					lockMultiStrokeImage.unlock();
				}
			}
			else if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			{
				//平滑曲线
				if (setlist.smoothWriting && actualPoints.size() > 2)
				{
					Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
					pen.SetLineJoin(LineJoinRound);
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);

					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Highlighter1.color, false));
					pen.SetWidth(stateInfo.Pen.Highlighter1.width);

					unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
					SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
					graphics.DrawCurve(&pen, actualPoints.data(), actualPoints.size(), 0.4f);
					lockMultiStrokeImage.unlock();
				}
			}
		}
	}
	else if (stateModeSelect == StateModeSelectEnum::IdtEraser)
	{
		double speed;
		double rubbersize, trubbersize = -1;
		int eraserMode = 2;

		// 设定画布
		shared_lock lockStrokeBackImageSm(StrokeBackImageSm);
		Graphics eraser(GetImageHDC(&drawpad));
		lockStrokeBackImageSm.unlock();

		// 初始智能橡皮粗细
		{
			if (setlist.eraserSetting.eraserMode <= 0 && (initialMode.pressure != 0.0 && !initialMode.isInvertedCursor)) eraserMode = 0; // 压感粗细
			else if (setlist.eraserSetting.eraserMode <= 1) eraserMode = 1; // 笔速粗细
			else eraserMode = 2; // 固定粗细

			if (eraserMode == 0) rubbersize = setlist.eraserSetting.eraserSize * 1.5 * initialMode.pressure + drawingScale * 20.0;
			else if (eraserMode == 1) rubbersize = drawingScale * 20.0;
			else rubbersize = setlist.eraserSetting.eraserSize;
		}
		// TODO 橡皮 OC 平滑和全套橡皮粗细管理模块

		//首次绘制
		{
			GraphicsPath path;
			path.AddEllipse(float(pointInfo.previousX - rubbersize / 2.0f), float(pointInfo.previousY - rubbersize / 2.0f), float(rubbersize), float(rubbersize));

			Region region(&path);
			eraser.SetClip(&region, CombineModeReplace);

			std::unique_lock<std::shared_mutex> lock1(StrokeBackImageSm);
			eraser.Clear(Color(0, 0, 0, 0));
			lock1.unlock();

			eraser.ResetClip();

			std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
			for (const auto& [point, value] : extreme_point)
			{
				if (value == true && region.IsVisible(point.first, point.second))
				{
					extreme_point[{point.first, point.second}] = false;
				}
			}
			LockExtremePointSm.unlock();

			hiex::EasyX_Gdiplus_Ellipse(pointInfo.previousX - (float)(rubbersize) / 2, pointInfo.previousY - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, Canvas);
		}

		// 进入绘制刷新队列
		{
			multiStrokeImage->alpha = 255;

			unique_lock lockStrokeImageListSm(StrokeImageListSm);
			StrokeImageList.emplace_back(multiStrokeImage);
			lockStrokeImageListSm.unlock();
		}

		while (1)
		{
			// 确认触摸点存在
			{
				shared_lock<shared_mutex> lockPointPosSm(touchPosSm);
				{
					if (TouchPos.find(pid) == TouchPos.end())
					{
						lockPointPosSm.unlock();
						break;
					}
					mode = TouchPos[pid];
				}
				lockPointPosSm.unlock();

				shared_lock lockPointListSm(pointListSm);
				auto it = std::find(TouchList.begin(), TouchList.end(), pid);
				lockPointListSm.unlock();
				if (it == TouchList.end()) break;
			}
			// 获取书写速度
			{
				shared_lock lockTouchSpeedSm(touchSpeedSm);
				speed = TouchSpeed[pid];
				lockTouchSpeedSm.unlock();
			}

			pointInfo.x = mode.pt.x, pointInfo.y = mode.pt.y;

			// 过程智能橡皮粗细
			{
				if (eraserMode == 0) rubbersize = setlist.eraserSetting.eraserSize * 1.5 * mode.pressure + drawingScale * 20.0;
				else if (eraserMode == 1)
				{
					if (setlist.paintDevice == 1)
					{
						// 鼠标和手写笔
						if (speed <= 30) trubbersize = max(25, speed * 2.33 + 2.33) * drawingScale;
						else trubbersize = min(200, speed + 30) * drawingScale;
					}
					else
					{
						// 触摸设备
						if (speed <= 20) trubbersize = max(25, speed * 2.33 + 13.33) * drawingScale;
						else trubbersize = min(200, 3 * speed) * drawingScale;
					}
				}
				else rubbersize = setlist.eraserSetting.eraserSize;
			}

			if (trubbersize == -1) trubbersize = rubbersize;
			if (rubbersize < trubbersize) rubbersize = rubbersize + max(0.1, (trubbersize - rubbersize) / 50);
			else if (rubbersize > trubbersize) rubbersize = rubbersize + min(-0.1, (trubbersize - rubbersize) / 50);

			if ((mode.pt.x == pointInfo.previousX && mode.pt.y == pointInfo.previousY))
			{
				// 擦除
				GraphicsPath path;
				path.AddEllipse(float(pointInfo.previousX - rubbersize / 2.0f), float(pointInfo.previousY - rubbersize / 2.0f), float(rubbersize), float(rubbersize));

				Region region(&path);
				eraser.SetClip(&region, CombineModeReplace);

				unique_lock lockStrokeBackImageSm(StrokeBackImageSm);
				eraser.Clear(Color(0, 0, 0, 0));
				lockStrokeBackImageSm.unlock();

				eraser.ResetClip();

				// 去除智能绘图吸附点（后续修改）
				unique_lock lockExtremePointSm(ExtremePointSm);
				for (const auto& [point, value] : extreme_point)
				{
					if (value == true && region.IsVisible(point.first, point.second))
					{
						extreme_point[{point.first, point.second}] = false;
					}
				}
				lockExtremePointSm.unlock();

				// 绘制橡皮外框
				unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
				SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(pointInfo.x - (float)(rubbersize) / 2, pointInfo.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, Canvas);
				lockMultiStrokeImage.unlock();
			}
			else
			{
				// 擦除
				GraphicsPath path;
				path.AddLine(pointInfo.previousX, pointInfo.previousY, pointInfo.x, pointInfo.y);

				Pen pen(Color(0, 0, 0, 0), Gdiplus::REAL(rubbersize));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				GraphicsPath* widenedPath = path.Clone();
				widenedPath->Widen(&pen);
				Region region(widenedPath);
				eraser.SetClip(&region, CombineModeReplace);

				std::unique_lock<std::shared_mutex> lock1(StrokeBackImageSm);
				eraser.Clear(Color(0, 0, 0, 0));
				lock1.unlock();

				eraser.ResetClip();
				delete widenedPath;

				// 去除智能绘图吸附点（后续修改）
				unique_lock lockExtremePointSm(ExtremePointSm);
				for (const auto& [point, value] : extreme_point)
				{
					if (value == true && region.IsVisible(point.first, point.second))
					{
						extreme_point[{point.first, point.second}] = false;
					}
				}
				lockExtremePointSm.unlock();

				// 绘制橡皮外框
				unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
				SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(pointInfo.x - (float)(rubbersize) / 2, pointInfo.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, Canvas);
				lockMultiStrokeImage.unlock();
			}

			pointInfo.previousX = pointInfo.x, pointInfo.previousY = pointInfo.y;
		}

		unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
		SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
		lockMultiStrokeImage.unlock();
	}
	else if (stateModeSelect == StateModeSelectEnum::IdtShape)
	{
		pointInfo.x = pointInfo.previousX, pointInfo.y = pointInfo.previousY;

		// 进入绘制刷新队列
		{
			multiStrokeImage->alpha = 255;

			unique_lock lockStrokeImageListSm(StrokeImageListSm);
			StrokeImageList.emplace_back(multiStrokeImage);
			lockStrokeImageListSm.unlock();
		}

	DelayStraighteningTarget:

		clock_t tRecord = clock();
		while (1)
		{
			// 确认触摸点存在
			{
				shared_lock<shared_mutex> lockPointPosSm(touchPosSm);
				{
					if (TouchPos.find(pid) == TouchPos.end())
					{
						lockPointPosSm.unlock();
						break;
					}
					mode = TouchPos[pid];
				}
				lockPointPosSm.unlock();

				shared_lock lockPointListSm(pointListSm);
				auto it = std::find(TouchList.begin(), TouchList.end(), pid);
				lockPointListSm.unlock();
				if (it == TouchList.end()) break;
			}
			// 过滤未动触摸点
			{
				if (mode.pt.x == pointInfo.previousX && mode.pt.y == pointInfo.previousY)
				{
					this_thread::sleep_for(chrono::milliseconds(1));
					continue;
				}
			}

			if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1)
			{
				pointInfo.x = mode.pt.x, pointInfo.y = mode.pt.y;

				Pen pen(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
				pen.SetWidth(Gdiplus::REAL(stateInfo.Pen.Brush1.width));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				// 绘制直线
				unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
				SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
				graphics.DrawLine(&pen, pointInfo.previousX, pointInfo.previousY, pointInfo.x, pointInfo.y);
				lockMultiStrokeImage.unlock();
			}
			else if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			{
				pointInfo.x = mode.pt.x, pointInfo.y = mode.pt.y;

				int rectangle_x = min(pointInfo.previousX, pointInfo.x), rectangle_y = min(pointInfo.previousY, pointInfo.y);
				int rectangle_heigth = abs(pointInfo.previousX - pointInfo.x) + 1, rectangle_width = abs(pointInfo.previousY - pointInfo.y) + 1;

				// 绘制矩形
				unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
				SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_RoundRect((float)rectangle_x, (float)rectangle_y, (float)rectangle_heigth, (float)rectangle_width, 3, 3, stateInfo.Pen.Brush1.color, stateInfo.Pen.Brush1.width, false, SmoothingModeHighQuality, Canvas);
				lockMultiStrokeImage.unlock();
			}

			// 防止写锁过快导致无法读锁（待修改）
			{
				if (tRecord)
				{
					int delay = 1000 / 24 - (clock() - tRecord);
					if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				}
				tRecord = clock();
			}
		}

		//智能绘图：端点吸附
		{
			if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1)
			{
				if (EuclideanDistance({ pointInfo.previousX, pointInfo.previousY }, { pointInfo.x, pointInfo.y }) / drawingScale >= 120)
				{
					Point start(pointInfo.previousX, pointInfo.previousY), end(pointInfo.x, pointInfo.y);

					//端点匹配
					if (setlist.pointAdsorption)
					{
						//起点匹配
						{
							Point start_target = start;
							double distance = 10 * drawingScale;

							std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { start.X,start.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { start.X,start.Y });
										start_target = { point.first,point.second };
									}
								}
							}
							LockExtremePointSm.unlock();

							start = start_target;
						}
						//终点匹配
						{
							Point end_target = end;
							double distance = 10 * drawingScale;

							std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { end.X,end.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { end.X,end.Y });
										end_target = { point.first,point.second };
									}
								}
							}
							LockExtremePointSm.unlock();

							end = end_target;
						}
					}

					std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
					extreme_point[{start.X, start.Y}] = extreme_point[{end.X, end.Y}] = true;
					LockExtremePointSm.unlock();

					Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);

					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
					pen.SetWidth(stateInfo.Pen.Brush1.width);

					unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
					SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
					graphics.DrawLine(&pen, start.X, start.Y, end.X, end.Y);
					lockMultiStrokeImage.unlock();
				}
			}
			else if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			{
				//端点匹配
				if ((pointInfo.x != pointInfo.previousX || pointInfo.y != pointInfo.previousY))
				{
					Point l1 = Point(pointInfo.previousX, pointInfo.previousY);
					Point l2 = Point(pointInfo.previousX, pointInfo.y);
					Point r1 = Point(pointInfo.x, pointInfo.previousY);
					Point r2 = Point(pointInfo.x, pointInfo.y);

					//端点匹配
					if (setlist.pointAdsorption)
					{
						{
							Point idx = l1;
							double distance = 10 * drawingScale;

							std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { l1.X,l1.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { l1.X,l1.Y });
										idx = { point.first,point.second };
									}
								}
							}
							LockExtremePointSm.unlock();

							l1 = idx;

							l2.X = l1.X;
							r1.Y = l1.Y;
						}
						{
							Point idx = l2;
							double distance = 10 * drawingScale;

							std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { l2.X,l2.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { l2.X,l2.Y });
										idx = { point.first,point.second };
									}
								}
							}
							LockExtremePointSm.unlock();

							l2 = idx;

							l1.X = l2.X;
							r2.Y = l2.Y;
						}
						{
							Point idx = r1;
							double distance = 10 * drawingScale;
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { r1.X,r1.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { r1.X,r1.Y });
										idx = { point.first,point.second };
									}
								}
							}
							r1 = idx;

							r2.X = r1.X;
							l1.Y = r1.Y;
						}
						{
							Point idx = r2;
							double distance = 10 * drawingScale;
							for (const auto& [point, value] : extreme_point)
							{
								if (value == true)
								{
									if (EuclideanDistance({ point.first,point.second }, { r2.X,r2.Y }) <= distance)
									{
										distance = EuclideanDistance({ point.first,point.second }, { r2.X,r2.Y });
										idx = { point.first,point.second };
									}
								}
							}
							r2 = idx;

							r1.X = r2.X;
							r2.Y = r2.Y;
						}
					}

					unique_lock lockExtremePointSm(ExtremePointSm);
					extreme_point[{l1.X, l1.Y}] = true;
					extreme_point[{l2.X, l2.Y}] = true;
					extreme_point[{r1.X, r1.Y}] = true;
					extreme_point[{r2.X, r2.Y}] = true;
					lockExtremePointSm.unlock();

					int x = min(l1.X, r2.X);
					int y = min(l1.Y, r2.Y);
					int w = abs(l1.X - r2.X) + 1;
					int h = abs(l1.Y - r2.Y) + 1;

					unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
					SetImageColor(*Canvas, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_RoundRect((float)x, (float)y, (float)w, (float)h, 3, 3, stateInfo.Pen.Brush1.color, stateInfo.Pen.Brush1.width, false, SmoothingModeHighQuality, Canvas);
					lockMultiStrokeImage.unlock();
				}
			}
		}
	}

	// 退出绘制刷新队列
	unique_lock lockMultiStrokeImage(multiStrokeImage->sm);
	{
		if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtPen) multiStrokeImage->endMode = 1;
		else if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtEraser) multiStrokeImage->endMode = 2;
		else if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtShape) multiStrokeImage->endMode = 1;
	}
	lockMultiStrokeImage.unlock();

	// 删除触摸点
	unique_lock lockPointPosSm(touchPosSm);
	if (TouchPos.find(pid) != TouchPos.end()) TouchPos.erase(pid);
	lockPointPosSm.unlock();
}
void DrawpadDrawing()
{
	threadStatus[L"DrawpadDrawing"] = true;

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
	blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	HDC hdcScreen = GetDC(NULL);
	// 调用UpdateLayeredWindow函数更新窗口
	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
	POINT ptDst = { 0,0 }; // 设置窗口位置
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	ulwi.cbSize = sizeof(ulwi);
	ulwi.hdcDst = hdcScreen;
	ulwi.pptDst = &ptDst;
	ulwi.psize = &sizeWnd;
	ulwi.pptSrc = &ptSrc;
	ulwi.crKey = 0;
	ulwi.pblend = &blend;
	ulwi.dwFlags = ULW_ALPHA;

	while (!(GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(drawpad_window, GWL_EXSTYLE, GetWindowLong(drawpad_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(drawpad_window, GWL_EXSTYLE, GetWindowLong(drawpad_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}

	window_background.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	drawpad.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

	SetImageColor(window_background, RGBA(0, 0, 0, 0), true);
	SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
	{
		ulwi.hdcSrc = GetImageHDC(&window_background);
		UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
	}
	IdtWindowsIsVisible.drawpadWindow = true;

	chrono::high_resolution_clock::time_point reckon;
	clock_t tRecord = 0;
	for (;;)
	{
		for (;;)
		{
			if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection)
			{
			ChooseEnd:
				{
					SetImageColor(window_background, RGBA(0, 0, 0, 0), true);

					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);

					if (setlist.avoidFullScreen)
						SetWindowPos(drawpad_window, NULL, MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight - 1, SWP_NOZORDER | SWP_NOACTIVATE);
				}
				bool saveImage = true;

				IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
				if (reference_record_pointer == current_record_pointer && !CompareImagesWithBuffer(&empty_drawpad, &drawpad))
				{
					if (RecallImage.empty())
					{
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1;

						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });
						RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}
					else if (!RecallImage.empty() && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img))
					{
						bool save_recond = false;
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });
						RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

						if (RecallImage.size() > 10)
						{
							while (RecallImage.size() > 10)
							{
								if (RecallImage.front().type == 1)
								{
									current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
								}
								RecallImage.pop_front();
							}
						}

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}
				}

				if (!RecallImage.empty() && !CompareImagesWithBuffer(&empty_drawpad, &RecallImage.back().img))
				{
					if (PptInfoStateBuffer.TotalPage != -1)
					{
						if (PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] && CompareImagesWithBuffer(&drawpad, &PptImg.Image[PptInfoStateBuffer.CurrentPage]))
							saveImage = false;
					}
					if (recall_image_reference <= recall_image_recond && recall_image_recond % 10 == 0 && recall_image_recond >= 20)
						saveImage = false;

					std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
					std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

					if (saveImage)
					{
						if (offSignal) SaveScreenShot(RecallImage.back().img, true);
						else
						{
							thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
							SaveScreenShot_thread.detach();
						}
					}

					extreme_point.clear();
					RecallImage.back().type = 1;
					RecallImage.push_back({ empty_drawpad, extreme_point, 2, make_pair(0,0) });
					RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

					if (RecallImage.size() > 10)
					{
						while (RecallImage.size() > 10)
						{
							if (RecallImage.front().type == 1)
							{
								current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
							}
							RecallImage.pop_front();
						}
					}

					LockExtremePointSm.unlock();
					LockStrokeBackImageSm.unlock();
				}
				else if (!RecallImage.empty())
				{
					std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);

					RecallImage.back().recond = make_pair(0, 0);

					LockStrokeBackImageSm.unlock();
				}

				if (offSignal) goto DrawpadDrawingEnd;

				if (RecallImage.size() >= 1 && PptInfoStateBuffer.TotalPage != -1)
				{
					if (!CompareImagesWithBuffer(&empty_drawpad, &drawpad))
					{
						PptImg.IsSave = true;
						PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] = true;

						PptImg.Image[PptInfoStateBuffer.CurrentPage] = drawpad;
					}
				}
				recall_image_recond = 0, FirstDraw = true;
				current_record_pointer = reference_record_pointer;
				RecallImageManipulated = std::chrono::high_resolution_clock::time_point();

				stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtSelection;

				int ppt_switch_count = 0;
				while (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection)
				{
					this_thread::sleep_for(chrono::milliseconds(50));

					if (PptInfoStateBuffer.CurrentPage != PptInfoState.CurrentPage) PptInfoStateBuffer.CurrentPage = PptInfoState.CurrentPage, ppt_switch_count++;
					PptInfoStateBuffer.TotalPage = PptInfoState.TotalPage;

					if (offSignal) goto DrawpadDrawingEnd;
				}

				{
					if (PptInfoStateBuffer.TotalPage != -1 && ppt_switch_count != 0 && PptImg.IsSaved[PptInfoStateBuffer.CurrentPage])
					{
						drawpad = PptImg.Image[PptInfoStateBuffer.CurrentPage];
					}
					else if (!reserve_drawpad) SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
					reserve_drawpad = false;

					SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
					hiex::TransparentImage(&window_background, 0, 0, &drawpad);

					if (setlist.avoidFullScreen) SetWindowPos(drawpad_window, NULL, MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);

					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}

				unique_lock<shared_mutex> lockPointPosSm(touchPosSm);
				TouchPos.clear();
				lockPointPosSm.unlock();
				unique_lock lockPointListSm(pointListSm);
				TouchList.clear();
				lockPointListSm.unlock();
				unique_lock<shared_mutex> lockPointTempSm(touchTempSm);
				TouchTemp.clear();
				lockPointTempSm.unlock();
			}
			if (penetrate.select == true)
			{
				if (setlist.avoidFullScreen) SetWindowPos(drawpad_window, NULL, MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight - 1, SWP_NOZORDER | SWP_NOACTIVATE);
				topWindowNow = true;

				LONG nRet = ::GetWindowLong(drawpad_window, GWL_EXSTYLE);
				nRet |= WS_EX_TRANSPARENT;
				::SetWindowLong(drawpad_window, GWL_EXSTYLE, nRet);

				while (1)
				{
					int temp_currentpage = PptInfoState.CurrentPage, temp_totalpage = PptInfoState.TotalPage;
					if (PptInfoStateBuffer.CurrentPage != temp_currentpage || PptInfoStateBuffer.TotalPage != temp_totalpage)
					{
						if (pptComSetlist.fixedHandWriting && PptInfoStateBuffer.CurrentPage != temp_currentpage && PptInfoStateBuffer.TotalPage == temp_totalpage)
						{
							IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
							if (reference_record_pointer == current_record_pointer && !CompareImagesWithBuffer(&empty_drawpad, &drawpad))
							{
								if (RecallImage.empty())
								{
									bool save_recond = false;
									if (recall_image_reference > recall_image_recond) recall_image_recond++;
									else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

									std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
									std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

									RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });
									RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

									LockExtremePointSm.unlock();
									LockStrokeBackImageSm.unlock();
								}
								else if (!RecallImage.empty() && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img))
								{
									if (!PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] || !CompareImagesWithBuffer(&drawpad, &PptImg.Image[PptInfoStateBuffer.CurrentPage]))
									{
										bool save_recond = false;
										if (recall_image_reference > recall_image_recond) recall_image_recond++;
										else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;
										if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
										{
											thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
											SaveScreenShot_thread.detach();
										}
									}

									std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
									std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

									RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });
									RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

									if (RecallImage.size() > 10)
									{
										while (RecallImage.size() > 10)
										{
											if (RecallImage.front().type == 1)
											{
												current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
											}
											RecallImage.pop_front();
										}
									}

									LockExtremePointSm.unlock();
									LockStrokeBackImageSm.unlock();
								}
							}

							if (!RecallImage.empty() && !CompareImagesWithBuffer(&empty_drawpad, &RecallImage.back().img))
							{
								std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
								std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

								if (!PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] || !CompareImagesWithBuffer(&RecallImage.back().img, &PptImg.Image[PptInfoStateBuffer.CurrentPage]))
								{
									thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
									SaveScreenShot_thread.detach();
								}

								PptImg.IsSave = true;
								PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] = true;
								PptImg.Image[PptInfoStateBuffer.CurrentPage] = RecallImage.back().img;

								extreme_point.clear();
								RecallImage.back().type = 1;
								RecallImage.push_back({ empty_drawpad, extreme_point, 0, make_pair(0,0) });
								RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

								if (RecallImage.size() > 10)
								{
									while (RecallImage.size() > 10)
									{
										if (RecallImage.front().type == 1)
										{
											current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
										}
										RecallImage.pop_front();
									}
								}

								LockExtremePointSm.unlock();
								LockStrokeBackImageSm.unlock();
							}

							if (PptImg.IsSaved[temp_currentpage] == true)
							{
								drawpad = PptImg.Image[temp_currentpage];
							}
							else
							{
								if (temp_totalpage != -1) SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
							}
						}
						PptInfoStateBuffer.CurrentPage = temp_currentpage;
						PptInfoStateBuffer.TotalPage = temp_totalpage;

						{
							SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
							hiex::TransparentImage(&window_background, 0, 0, &drawpad);

							ulwi.hdcSrc = GetImageHDC(&window_background);
							UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
						}
					}

					this_thread::sleep_for(chrono::milliseconds(50));

					if (offSignal) goto DrawpadDrawingEnd;
					if (penetrate.select == true) continue;
					break;
				}

				if (setlist.avoidFullScreen) SetWindowPos(drawpad_window, NULL, MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);

				nRet = ::GetWindowLong(drawpad_window, GWL_EXSTYLE);
				nRet &= ~WS_EX_TRANSPARENT;
				::SetWindowLong(drawpad_window, GWL_EXSTYLE, nRet);

				TouchTemp.clear();
			}

			std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
			bool start = !StrokeImageList.empty();
			lock1.unlock();

			stateMode.StateModeSelectEcho = stateMode.StateModeSelect;
			if (start) break;

			if (offSignal)
			{
				if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection) goto ChooseEnd;
				goto DrawpadDrawingEnd;
			}

			int temp_currentpage = PptInfoState.CurrentPage, temp_totalpage = PptInfoState.TotalPage;
			if (PptInfoStateBuffer.CurrentPage != temp_currentpage || PptInfoStateBuffer.TotalPage != temp_totalpage)
			{
				if (pptComSetlist.fixedHandWriting && PptInfoStateBuffer.CurrentPage != temp_currentpage && PptInfoStateBuffer.TotalPage == temp_totalpage)
				{
					IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
					if (reference_record_pointer == current_record_pointer && !CompareImagesWithBuffer(&empty_drawpad, &drawpad))
					{
						if (RecallImage.empty())
						{
							bool save_recond = false;
							if (recall_image_reference > recall_image_recond) recall_image_recond++;
							else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

							std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
							std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

							RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });
							RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

							LockExtremePointSm.unlock();
							LockStrokeBackImageSm.unlock();
						}
						else if (!RecallImage.empty() && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img))
						{
							if (!PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] || !CompareImagesWithBuffer(&drawpad, &PptImg.Image[PptInfoStateBuffer.CurrentPage]))
							{
								bool save_recond = false;
								if (recall_image_reference > recall_image_recond) recall_image_recond++;
								else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;
								if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
								{
									thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
									SaveScreenShot_thread.detach();
								}
							}

							std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
							std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

							RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });
							RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

							if (RecallImage.size() > 10)
							{
								while (RecallImage.size() > 10)
								{
									if (RecallImage.front().type == 1)
									{
										current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
									}
									RecallImage.pop_front();
								}
							}

							LockExtremePointSm.unlock();
							LockStrokeBackImageSm.unlock();
						}
					}

					if (!RecallImage.empty() && !CompareImagesWithBuffer(&empty_drawpad, &RecallImage.back().img))
					{
						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						if (!PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] || !CompareImagesWithBuffer(&RecallImage.back().img, &PptImg.Image[PptInfoStateBuffer.CurrentPage]))
						{
							thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
							SaveScreenShot_thread.detach();
						}

						PptImg.IsSave = true;
						PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] = true;
						PptImg.Image[PptInfoStateBuffer.CurrentPage] = RecallImage.back().img;

						extreme_point.clear();
						RecallImage.back().type = 1;
						RecallImage.push_back({ empty_drawpad, extreme_point, 0, make_pair(0,0) });
						RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

						if (RecallImage.size() > 10)
						{
							while (RecallImage.size() > 10)
							{
								if (RecallImage.front().type == 1)
								{
									current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
								}
								RecallImage.pop_front();
							}
						}

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}

					if (PptImg.IsSaved[temp_currentpage] == true)
					{
						drawpad = PptImg.Image[temp_currentpage];
					}
					else
					{
						if (temp_totalpage != -1) SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
					}
				}
				else if (PptInfoStateBuffer.TotalPage != temp_totalpage && temp_totalpage == -1)
				{
					stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
					//choose.select = true;

					//brush.select = false;
					//rubber.select = false;
					penetrate.select = false;
				}
				PptInfoStateBuffer.CurrentPage = temp_currentpage;
				PptInfoStateBuffer.TotalPage = temp_totalpage;

				{
					SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
					hiex::TransparentImage(&window_background, 0, 0, &drawpad);

					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}
			}

			this_thread::sleep_for(chrono::milliseconds(10));
		}
		reckon = std::chrono::high_resolution_clock::now();

		SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
		std::shared_lock<std::shared_mutex> lock1(StrokeBackImageSm);
		hiex::TransparentImage(&window_background, 0, 0, &drawpad);
		lock1.unlock();

		std::shared_lock<std::shared_mutex> lock2(StrokeImageListSm);
		int siz = StrokeImageList.size();
		lock2.unlock();

		for (int currentId = 0; currentId < siz; currentId++)
		{
			std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
			StrokeImageClass* currentStrokeImage = StrokeImageList[currentId];
			lock1.unlock();

			std::shared_lock<std::shared_mutex> lock2(currentStrokeImage->sm);

			int info = currentStrokeImage->endMode;
			hiex::TransparentImage(&window_background, 0, 0, currentStrokeImage->canvas, currentStrokeImage->alpha);

			lock2.unlock();

			if (info)
			{
				if (info == 1)
				{
					// currentStrokeImage 已经保证不会再占用了，则不需要使用同步锁了

					std::shared_lock<std::shared_mutex> lockStrokeBackImageSm(StrokeBackImageSm);
					hiex::TransparentImage(&drawpad, 0, 0, currentStrokeImage->canvas, currentStrokeImage->alpha);
					lockStrokeBackImageSm.unlock();

					delete currentStrokeImage;

					std::unique_lock<std::shared_mutex> lockStrokeImageListSm(StrokeImageListSm);
					StrokeImageList.erase(StrokeImageList.begin() + currentId--);
					lockStrokeImageListSm.unlock();

					std::shared_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
					bool free = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - RecallImageManipulated).count() >= 1000;
					LockRecallImageManipulatedSm.unlock();
					if (free)
					{
						bool save_recond = false;
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

						if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
						{
							thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
							SaveScreenShot_thread.detach();
						}

						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond,recall_image_reference) });
						RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);

						if (RecallImage.size() > 10)
						{
							while (RecallImage.size() > 10)
							{
								if (RecallImage.front().type == 1)
								{
									current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
								}
								RecallImage.pop_front();
							}
						}

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();

						std::unique_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
						RecallImageManipulated = std::chrono::high_resolution_clock::now();
						LockRecallImageManipulatedSm.unlock();
					}
				}
				else if (info == 2)
				{
					delete currentStrokeImage;

					std::unique_lock<std::shared_mutex> lockStrokeImageListSm(StrokeImageListSm);
					StrokeImageList.erase(StrokeImageList.begin() + currentId--);
					lockStrokeImageListSm.unlock();

					std::shared_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
					bool free = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - RecallImageManipulated).count() >= 1000;
					LockRecallImageManipulatedSm.unlock();
					if (free)
					{
						bool save_recond = false;
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

						bool save = false;
						if (!RecallImage.empty())
						{
							std::shared_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
							save = !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img);
							LockStrokeBackImageSm.unlock();
						}
						else
						{
							IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
							save = !CompareImagesWithBuffer(&drawpad, &empty_drawpad);
						}
						if (save)
						{
							if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
							{
								thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
								SaveScreenShot_thread.detach();
							}

							std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
							std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

							RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond,recall_image_reference) });
							RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);
							if (RecallImage.size() > 10)
							{
								while (RecallImage.size() > 10)
								{
									if (RecallImage.front().type == 1)
									{
										current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
									}
									RecallImage.pop_front();
								}
							}

							LockExtremePointSm.unlock();
							LockStrokeBackImageSm.unlock();

							std::unique_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
							RecallImageManipulated = std::chrono::high_resolution_clock::now();
							LockRecallImageManipulatedSm.unlock();
						}
					}
				}
			}

			std::shared_lock<std::shared_mutex> lock3(StrokeImageListSm);
			siz = StrokeImageList.size();
			lock3.unlock();
		}

		//帧率锁
		{
			clock_t tNow = clock();
			if (tRecord)
			{
				int delay = 1000 / 72 - (tNow - tRecord);
				if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			}
		}

		ulwi.hdcSrc = GetImageHDC(&window_background);
		UpdateLayeredWindowIndirect(drawpad_window, &ulwi);

		tRecord = clock();
	}

DrawpadDrawingEnd:
	threadStatus[L"DrawpadDrawing"] = false;
	return;
}
int drawpad_main()
{
	threadStatus[L"drawpad_main"] = true;

	//窗口初始化
	{
		{
			/*
			if (IsWindows8OrGreater())
			{
				HMODULE hUser32 = LoadLibrary(TEXT("user32.dll"));
				if (hUser32)
				{
					pGetPointerFrameTouchInfo = (GetPointerFrameTouchInfoType)GetProcAddress(hUser32, "GetPointerFrameTouchInfo");
					uGetPointerFrameTouchInfo = true;
					//当完成使用时, 可以调用 FreeLibrary(hUser32);
				}
			}
			*/

			//hiex::SetWndProcFunc(drawpad_window, WndProc);
		}

		DisableResizing(drawpad_window, true);//禁止窗口拉伸
		SetWindowLong(drawpad_window, GWL_STYLE, GetWindowLong(drawpad_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
		SetWindowPos(drawpad_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
		SetWindowLong(drawpad_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

		SetWindowPos(drawpad_window, NULL, MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top, MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	}

	//初始化数值
	{
		//屏幕快照处理
		LoadDrawpad();

		drawingScale = GetDrawingScale();
		stopTimingError = GetStopTimingError();

		stateMode.Pen.Brush1.width = 3 * drawingScale;
		stateMode.Pen.Highlighter1.width = 35 * drawingScale;
		stateMode.Shape.StraightLine1.width = 3 * drawingScale;
		stateMode.Shape.Rectangle1.width = 3 * drawingScale;
	}

	thread DrawpadDrawing_thread(DrawpadDrawing);
	DrawpadDrawing_thread.detach();
	thread DrawpadInstallHookThread(DrawpadInstallHook);
	DrawpadInstallHookThread.detach();
	thread(KeyboardInteraction).detach();
	{
		SetImageColor(alpha_drawpad, RGBA(0, 0, 0, 0), true);

		// 启动绘图库程序
		hiex::Gdiplus_Try_Starup();

		// 落笔预备
		ResetPrepareCanvas();

		while (!offSignal)
		{
			if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection || penetrate.select == true)
			{
				this_thread::sleep_for(chrono::milliseconds(100));
				continue;
			}

			while (1)
			{
				std::shared_lock<std::shared_mutex> LockPointTempSm(touchTempSm);
				bool start = !TouchTemp.empty();
				LockPointTempSm.unlock();
				if (start)
				{
					shared_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
					bool start = !drawWaiting;
					LockdrawWaitingSm.unlock();
				}

				//开始绘图
				if (start)
				{
					StateModeSelectEnum nextPointMode = stateMode.StateModeSelect;

					shared_lock<shared_mutex> lock1(touchTempSm);
					TouchInfo touchPoint = TouchTemp.front();
					lock1.unlock();
					if (touchPoint.mode.isInvertedCursor || touchPoint.type == 3) nextPointMode = StateModeSelectEnum::IdtEraser;

					if (int(state) == 1 && nextPointMode == StateModeSelectEnum::IdtEraser && setlist.RubberRecover) target_status = 0;
					else if (int(state) == 1 && nextPointMode == StateModeSelectEnum::IdtPen && setlist.BrushRecover) target_status = 0;

					if (current_record_pointer != reference_record_pointer)
					{
						current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);

						shared_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
						RecallImage.push_back({ drawpad, extreme_point, 0 });
						RecallImagePeak = max((int)RecallImage.size(), RecallImagePeak);
						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}
					if (FirstDraw) RecallImageManipulated = std::chrono::high_resolution_clock::now();
					FirstDraw = false;

					unique_lock<shared_mutex> lock2(touchTempSm);
					TouchTemp.pop_front();
					lock2.unlock();

					thread MultiFingerDrawing_thread(MultiFingerDrawing, touchPoint.pid, touchPoint.mode, stateMode, nextPointMode);
					MultiFingerDrawing_thread.detach();
				}
				else break;
			}

			this_thread::sleep_for(chrono::milliseconds(10));
		}
	}

	int i = 1;
	for (; i <= 10; i++)
	{
		if (!threadStatus[L"DrawpadDrawing"]) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}
	threadStatus[L"drawpad_main"] = false;
	return 0;
}