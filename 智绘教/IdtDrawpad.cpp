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

// 实时平滑测试
#pragma comment(lib, "absl/AbslWin32MT.lib")

bool main_open;
bool FirstDraw = true;
bool IdtHotkey;

unordered_map<LONG, shared_mutex> StrokeImageSm;
shared_mutex StrokeImageListSm;
map<LONG, pair<IMAGE*, int>> StrokeImage; // second 表示绘制状态 01画笔 23橡皮 （单绘制/双停止）
vector<LONG> StrokeImageList;
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

		if (ppt_show != NULL)
		{
			// 检查按下的键
			switch (pKeyInfo->vkCode)
			{
			case VK_SPACE:  // 空格键
			case VK_NEXT:   // PgDn
			case VK_RIGHT:  // 右箭头
			case VK_DOWN:   // 下箭头
			{
				if (KeyBoradDown[(BYTE)pKeyInfo->vkCode])
				{
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
				}
				break;
			}

			case VK_PRIOR:  // PgUp
			case VK_LEFT:   // 左箭头
			case VK_UP:     // 上箭头
			{
				if (KeyBoradDown[(BYTE)pKeyInfo->vkCode])
				{
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::LeftSide_PageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
					pptUiRoundRectWidget[PptUiRoundRectWidgetID::RightSide_PageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
				}
				break;
			}

			case VK_ESCAPE:   // ESC
			{
				break;
			}
			}
		}

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

			if (ppt_show != NULL)
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
				case VK_ESCAPE:   // 退出

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

		// 定格所需的额外情况
		else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection && (KeyBoradDown[VK_CONTROL] || KeyBoradDown[VK_LCONTROL] || KeyBoradDown[VK_RCONTROL]) && (BYTE)pKeyInfo->vkCode == (BYTE)0x51)
		{
			ExMessage msgKey = {};
			msgKey.message = wParam;
			msgKey.vkcode = (BYTE)0x51;
			msgKey.ctrl = true;

			int index = hiex::GetWindowIndex(drawpad_window, false);
			unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
			hiex::g_vecWindows[index].vecMessage.push_back(msgKey);
			lg_vecWindows_vecMessage_sm.unlock();

			return 1;
		}
		// 穿透所需的额外情况
		else if (penetrate.select && (KeyBoradDown[VK_CONTROL] || KeyBoradDown[VK_LCONTROL] || KeyBoradDown[VK_RCONTROL]) && (BYTE)pKeyInfo->vkCode == (BYTE)0x45)
		{
			ExMessage msgKey = {};
			msgKey.message = wParam;
			msgKey.vkcode = (BYTE)0x45;
			msgKey.ctrl = true;

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
			if (m.message == WM_KEYDOWN && (m.vkcode == VK_DOWN || m.vkcode == VK_RIGHT || m.vkcode == VK_NEXT || m.vkcode == VK_SPACE || m.vkcode == VK_UP || m.vkcode == VK_LEFT || m.vkcode == VK_PRIOR))
			{
				auto vkcode = m.vkcode;
				cout << "pk " << (int)vkcode << endl;

				if (vkcode == VK_UP || vkcode == VK_LEFT || vkcode == VK_PRIOR)
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
						if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
						{
							EndPptShow();

							//brush.select = false;
							//rubber.select = false;
							penetrate.select = false;
							//choose.select = true;
							stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
						}
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
									if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
									{
										EndPptShow();

										//brush.select = false;
										//rubber.select = false;
										penetrate.select = false;
										//choose.select = true;
										stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
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
					if (MessageBox(floating_window, L"当前处于画板模式，结束放映将会清空画板内容。\n\n结束放映？", L"智绘教警告", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) == 1)
					{
						EndPptShow();

						//brush.select = false;
						//rubber.select = false;
						penetrate.select = false;
						//choose.select = true;
						stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
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

void MultiFingerDrawing(LONG pid, POINT pt, StateModeClass stateInfo)
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
		pointInfo.previousX = pt.x;
		pointInfo.previousY = pt.y;
	}

	IMAGE Canvas = CreateImageColor(screenInfo.width, screenInfo.height, RGBA(0, 0, 0, 0), true);
	IMAGE* BackCanvas = new IMAGE(screenInfo.width, screenInfo.height);

	// 绘制画布定义
	Graphics graphics(GetImageHDC(&Canvas));
	graphics.SetSmoothingMode(SmoothingModeHighQuality);

	if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		double accurateWritingDistance = 0;
		int instantWritingDistance = 0;
		RECT inkTangentRectangle = { -1,-1,-1,-1 };

		vector<Point> actualPoints = { Point(pointInfo.previousX, pointInfo.previousY) }; // 实际点集

		//首次绘制（后续修改，即画点）
		{
			if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
			{
				hiex::EasyX_Gdiplus_SolidEllipse(float((float)pointInfo.previousX - stateInfo.Pen.Brush1.width / 2.0), float((float)pointInfo.previousY - stateInfo.Pen.Brush1.width / 2.0), stateInfo.Pen.Brush1.width, stateInfo.Pen.Brush1.width, stateInfo.Pen.Brush1.color, false, SmoothingModeHighQuality, &Canvas);
			}
			else if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			{
				hiex::EasyX_Gdiplus_SolidEllipse(float((float)pointInfo.previousX - stateInfo.Pen.Highlighter1.width / 2.0), float((float)pointInfo.previousY - stateInfo.Pen.Highlighter1.width / 2.0), stateInfo.Pen.Highlighter1.width, stateInfo.Pen.Highlighter1.width, stateInfo.Pen.Highlighter1.color, false, SmoothingModeHighQuality, &Canvas);
			}
		}

		// 进入绘制刷新队列
		{
			unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
			{
				// 前三位为透明度，后一位为操作状态
				if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) StrokeImage[pid] = make_pair(&Canvas, 1300);
				else StrokeImage[pid] = make_pair(&Canvas, 2550);
			}
			lockStrokeImageSm.unlock();

			unique_lock lockStrokeImageListSm(StrokeImageListSm);
			StrokeImageList.emplace_back(pid);
			lockStrokeImageListSm.unlock();
		}

		// 待修改
		POINT StopTimingPoint = { -1,-1 };
		bool StopTimingDisable = !setlist.IntelligentDrawing;
		chrono::high_resolution_clock::time_point StopTiming = std::chrono::high_resolution_clock::now();

		clock_t tRecord = clock();
		while (1)
		{
			// 确认触摸点存在
			{
				shared_lock<shared_mutex> lockPointPosSm(PointPosSm);
				{
					if (TouchPos.find(pid) == TouchPos.end())
					{
						lockPointPosSm.unlock();
						break;
					}
					pt = TouchPos[pid].pt;
				}
				lockPointPosSm.unlock();

				shared_lock lockPointListSm(PointListSm);
				auto it = std::find(TouchList.begin(), TouchList.end(), pid);
				lockPointListSm.unlock();
				if (it == TouchList.end()) break;
			}
			// 延迟拉直（Beta 待修改）
			if (!StopTimingDisable && stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1 && !actualPoints.empty())
			{
				if (sqrt((pt.x - StopTimingPoint.x) * (pt.x - StopTimingPoint.x) + (pt.y - StopTimingPoint.y) * (pt.y - StopTimingPoint.y)) > 5)
				{
					StopTimingPoint = pt;
					StopTiming = chrono::high_resolution_clock::now();
				}
				else if (chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - StopTiming).count() >= 1000)
				{
					if (sqrt((pt.x - actualPoints[0].X) * (pt.x - actualPoints[0].X) + (pt.y - actualPoints[0].Y) * (pt.y - actualPoints[0].Y)) >= 120)
					{
						double redundance = max(GetSystemMetrics(SM_CXSCREEN) / 192, min((GetSystemMetrics(SM_CXSCREEN)) / 76.8, double(GetSystemMetrics(SM_CXSCREEN)) / double((-0.036) * accurateWritingDistance + 135)));

						// 5 倍宽松精度
						if (isLine(actualPoints, int(redundance * 5.0f), chrono::high_resolution_clock::now()))
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
			}

			// 过滤未动触摸点
			{
				if (pt.x == pointInfo.previousX && pt.y == pointInfo.previousY)
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
				unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
				graphics.DrawLine(&pen, pointInfo.previousX, pointInfo.previousY, (pointInfo.x = pt.x), (pointInfo.y = pt.y));
				lockStrokeImageSm.unlock();

				// 绘制计算
				{
					accurateWritingDistance += EuclideanDistance({ pointInfo.previousX, pointInfo.previousY }, { pt.x, pt.y });

					if (pointInfo.x < inkTangentRectangle.left || inkTangentRectangle.left == -1) inkTangentRectangle.left = pointInfo.x;
					if (pointInfo.y < inkTangentRectangle.top || inkTangentRectangle.top == -1) inkTangentRectangle.top = pointInfo.y;
					if (pointInfo.x > inkTangentRectangle.right || inkTangentRectangle.right == -1) inkTangentRectangle.right = pointInfo.x;
					if (pointInfo.y > inkTangentRectangle.bottom || inkTangentRectangle.bottom == -1) inkTangentRectangle.bottom = pointInfo.y;

					instantWritingDistance += (int)EuclideanDistance({ pointInfo.previousX, pointInfo.previousY }, { pt.x, pt.y });
					if (instantWritingDistance >= 4)
					{
						actualPoints.push_back(Point(pointInfo.x, pointInfo.y));
						instantWritingDistance %= 4;
					}

					pointInfo.previousX = pointInfo.x, pointInfo.previousY = pointInfo.y;
				}
			}
		}

		// 定格绘制刷新队列
		unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
		{
			ImgCpy(BackCanvas, &Canvas);
			if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) StrokeImage[pid] = make_pair(BackCanvas, 1300);
			else StrokeImage[pid] = make_pair(BackCanvas, 2550);
		}
		lockStrokeImageSm.unlock();

		//智能绘图模块
		{
			if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
			{
				//直线绘制
				double redundance = max(GetSystemMetrics(SM_CXSCREEN) / 192, min((GetSystemMetrics(SM_CXSCREEN)) / 76.8, double(GetSystemMetrics(SM_CXSCREEN)) / double((-0.036) * accurateWritingDistance + 135)));
				if (setlist.IntelligentDrawing && accurateWritingDistance >= 120 && (abs(inkTangentRectangle.left - inkTangentRectangle.right) >= 120 || abs(inkTangentRectangle.top - inkTangentRectangle.bottom) >= 120) && isLine(actualPoints, int(redundance), std::chrono::high_resolution_clock::now()))
				{
					Point start(actualPoints[0]), end(actualPoints[actualPoints.size() - 1]);

					//端点匹配
					{
						//起点匹配
						{
							Point start_target = start;
							double distance = 10;

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
							double distance = 10;

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

					SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

					Graphics graphics(GetImageHDC(&Canvas));
					graphics.SetSmoothingMode(SmoothingModeHighQuality);

					Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);

					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
					pen.SetWidth(stateInfo.Pen.Brush1.width);

					graphics.DrawLine(&pen, start.X, start.Y, end.X, end.Y);
				}

				//平滑曲线
				else if (setlist.SmoothWriting && actualPoints.size() > 2)
				{
					SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

					Graphics graphics(GetImageHDC(&Canvas));
					graphics.SetSmoothingMode(SmoothingModeHighQuality);

					Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
					pen.SetLineJoin(LineJoinRound);
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);

					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
					pen.SetWidth(stateInfo.Pen.Brush1.width);

					graphics.DrawCurve(&pen, actualPoints.data(), actualPoints.size(), 0.4f);
				}
			}
			else if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			{
				//平滑曲线
				if (setlist.SmoothWriting && actualPoints.size() > 2)
				{
					SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

					Graphics graphics(GetImageHDC(&Canvas));
					graphics.SetSmoothingMode(SmoothingModeHighQuality);

					Pen pen(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
					pen.SetLineJoin(LineJoinRound);
					pen.SetStartCap(LineCapRound);
					pen.SetEndCap(LineCapRound);

					pen.SetColor(hiex::ConvertToGdiplusColor(stateInfo.Pen.Highlighter1.color, false));
					pen.SetWidth(stateInfo.Pen.Highlighter1.width);

					graphics.DrawCurve(&pen, actualPoints.data(), actualPoints.size(), 0.4f);
				}
			}
		}
	}
	else if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtEraser)
	{
		double speed;
		double rubbersize = 15, trubbersize = -1;

		// 设定画布
		shared_lock lockStrokeBackImageSm(StrokeBackImageSm);
		Graphics eraser(GetImageHDC(&drawpad));
		lockStrokeBackImageSm.unlock();

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

			hiex::EasyX_Gdiplus_Ellipse(pointInfo.previousX - (float)(rubbersize) / 2, pointInfo.previousY - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
		}

		// 进入绘制刷新队列
		{
			unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
			StrokeImage[pid] = make_pair(&Canvas, 2552);
			lockStrokeImageSm.unlock();

			unique_lock lockStrokeImageListSm(StrokeImageListSm);
			StrokeImageList.emplace_back(pid);
			lockStrokeImageListSm.unlock();
		}

		while (1)
		{
			// 确认触摸点存在
			{
				shared_lock<shared_mutex> lockPointPosSm(PointPosSm);
				{
					if (TouchPos.find(pid) == TouchPos.end())
					{
						lockPointPosSm.unlock();
						break;
					}
					pt = TouchPos[pid].pt;
				}
				lockPointPosSm.unlock();

				shared_lock lockPointListSm(PointListSm);
				auto it = std::find(TouchList.begin(), TouchList.end(), pid);
				lockPointListSm.unlock();
				if (it == TouchList.end()) break;
			}
			// 获取书写速度
			{
				shared_lock lockTouchSpeedSm(TouchSpeedSm);
				speed = TouchSpeed[pid];
				lockTouchSpeedSm.unlock();
			}

			pointInfo.x = pt.x, pointInfo.y = pt.y;

			// 计算智能橡皮大小
			if (setlist.RubberMode == 1)
			{
				// PC 鼠标
				if (speed <= 30) trubbersize = max(25, speed * 2.33 + 2.33);
				else trubbersize = min(200, speed + 30);
			}
			else
			{
				// 触摸设备
				if (speed <= 20) trubbersize = max(25, speed * 2.33 + 13.33);
				else trubbersize = min(200, 3 * speed);
			}

			if (trubbersize == -1) trubbersize = rubbersize;
			if (rubbersize < trubbersize) rubbersize = rubbersize + max(0.1, (trubbersize - rubbersize) / 50);
			else if (rubbersize > trubbersize) rubbersize = rubbersize + min(-0.1, (trubbersize - rubbersize) / 50);

			if ((pt.x == pointInfo.previousX && pt.y == pointInfo.previousY))
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
				unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(pointInfo.x - (float)(rubbersize) / 2, pointInfo.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
				lockStrokeImageSm.unlock();
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
				unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(pointInfo.x - (float)(rubbersize) / 2, pointInfo.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
				lockStrokeImageSm.unlock();
			}

			pointInfo.previousX = pointInfo.x, pointInfo.previousY = pointInfo.y;
		}

		// 考虑不需要操作，所以不需要 定格绘制刷新队列，只需要擦除橡皮边框
		unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
		SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
		lockStrokeImageSm.unlock();
	}
	else if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		// 进入绘制刷新队列
		{
			unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
			StrokeImage[pid] = make_pair(&Canvas, 2550);
			lockStrokeImageSm.unlock();

			unique_lock lockStrokeImageListSm(StrokeImageListSm);
			StrokeImageList.emplace_back(pid);
			lockStrokeImageListSm.unlock();
		}

	DelayStraighteningTarget:

		clock_t tRecord = clock();
		while (1)
		{
			// 确认触摸点存在
			{
				shared_lock<shared_mutex> lockPointPosSm(PointPosSm);
				{
					if (TouchPos.find(pid) == TouchPos.end())
					{
						lockPointPosSm.unlock();
						break;
					}
					pt = TouchPos[pid].pt;
				}
				lockPointPosSm.unlock();

				shared_lock lockPointListSm(PointListSm);
				auto it = std::find(TouchList.begin(), TouchList.end(), pid);
				lockPointListSm.unlock();
				if (it == TouchList.end()) break;
			}
			// 过滤未动触摸点
			{
				if (pt.x == pointInfo.previousX && pt.y == pointInfo.previousY)
				{
					this_thread::sleep_for(chrono::milliseconds(1));
					continue;
				}
			}

			if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1)
			{
				pointInfo.x = pt.x, pointInfo.y = pt.y;

				Pen pen(hiex::ConvertToGdiplusColor(stateInfo.Pen.Brush1.color, false));
				pen.SetWidth(Gdiplus::REAL(stateInfo.Pen.Brush1.width));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				// 绘制直线
				unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
				{
					SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
					graphics.DrawLine(&pen, pointInfo.previousX, pointInfo.previousY, pointInfo.x, pointInfo.y);
				}
				lockStrokeImageSm.unlock();
			}
			else if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			{
				pointInfo.x = pt.x, pointInfo.y = pt.y;

				int rectangle_x = min(pointInfo.previousX, pointInfo.x), rectangle_y = min(pointInfo.previousY, pointInfo.y);
				int rectangle_heigth = abs(pointInfo.previousX - pointInfo.x) + 1, rectangle_width = abs(pointInfo.previousY - pointInfo.y) + 1;

				// 绘制矩形
				unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
				{
					SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_RoundRect((float)rectangle_x, (float)rectangle_y, (float)rectangle_heigth, (float)rectangle_width, 3, 3, stateInfo.Pen.Brush1.color, stateInfo.Pen.Brush1.width, false, SmoothingModeHighQuality, &Canvas);
				}
				lockStrokeImageSm.unlock();
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

		// 定格绘制刷新队列
		unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
		{
			ImgCpy(BackCanvas, &Canvas);
			StrokeImage[pid] = make_pair(BackCanvas, 2550);
		}
		lockStrokeImageSm.unlock();

		//智能绘图模块
		{
			if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1); // 直线吸附待实现
			else if (stateInfo.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			{
				//端点匹配
				if (setlist.IntelligentDrawing && (pointInfo.x != pointInfo.previousX || pointInfo.y != pointInfo.previousY))
				{
					Point l1 = Point(pointInfo.previousX, pointInfo.previousY);
					Point l2 = Point(pointInfo.previousX, pointInfo.y);
					Point r1 = Point(pointInfo.x, pointInfo.previousY);
					Point r2 = Point(pointInfo.x, pointInfo.y);

					//端点匹配
					{
						{
							Point idx = l1;
							double distance = 10;

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
							double distance = 10;

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
							double distance = 10;
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
							double distance = 10;
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

					SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_RoundRect((float)x, (float)y, (float)w, (float)h, 3, 3, stateInfo.Shape.Rectangle1.color, stateInfo.Shape.Rectangle1.width, false, SmoothingModeHighQuality, &Canvas);
				}
			}
		}
	}

	// 删除触摸点
	unique_lock lockPointPosSm(PointPosSm);
	{
		TouchPos.erase(pid);
	}
	lockPointPosSm.unlock();

	// 退出绘制刷新队列
	unique_lock lockStrokeImageSm(StrokeImageSm[pid]);
	{
		ImgCpy(BackCanvas, &Canvas);
		if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtPen)
		{
			if (stateInfo.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1) StrokeImage[pid] = make_pair(BackCanvas, (/*draw_info.color >> 24*/130) * 10 + 1);
			else StrokeImage[pid] = make_pair(BackCanvas, 2551);
		}
		else if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtEraser)
		{
			StrokeImage[pid] = make_pair(BackCanvas, 2553);
		}
		else if (stateInfo.StateModeSelect == StateModeSelectEnum::IdtShape)
		{
			StrokeImage[pid] = make_pair(BackCanvas, 2551);
		}
	}
	lockStrokeImageSm.unlock();
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

					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}
				TouchTemp.clear();
			}
			if (penetrate.select == true)
			{
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

		// 开始绘制（前期实现FPS显示）
		SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
		std::shared_lock<std::shared_mutex> lock1(StrokeBackImageSm);
		hiex::TransparentImage(&window_background, 0, 0, &drawpad);
		lock1.unlock();

		std::shared_lock<std::shared_mutex> lock2(StrokeImageListSm);
		int siz = StrokeImageList.size();
		lock2.unlock();

		int t1 = 0, t2 = 0;
		for (int i = 0; i < siz; i++)
		{
			std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
			int pid = StrokeImageList[i];
			lock1.unlock();

			std::chrono::high_resolution_clock::time_point s1 = std::chrono::high_resolution_clock::now();
			std::shared_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
			t1 += (int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - s1).count();

			s1 = std::chrono::high_resolution_clock::now();
			int info = StrokeImage[pid].second;
			hiex::TransparentImage(&window_background, 0, 0, StrokeImage[pid].first, (info / 10));
			t2 += (int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - s1).count();

			lock2.unlock();
			if ((info % 10) % 2 == 1)
			{
				if ((info % 10) == 1)
				{
					std::shared_lock<std::shared_mutex> lock1(StrokeBackImageSm);
					std::shared_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
					hiex::TransparentImage(&drawpad, 0, 0, StrokeImage[pid].first, (info / 10));
					lock2.unlock();
					lock1.unlock();

					std::unique_lock<std::shared_mutex> lock3(StrokeImageSm[pid]);
					delete StrokeImage[pid].first;
					StrokeImage[pid].first = nullptr;

					StrokeImage.erase(pid);
					lock3.unlock();
					StrokeImageSm.erase(pid);

					std::unique_lock<std::shared_mutex> lock4(StrokeImageListSm);
					StrokeImageList.erase(StrokeImageList.begin() + i);
					lock4.unlock();
					i--;

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
				else if ((info % 10) == 3)
				{
					std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
					delete StrokeImage[pid].first;
					StrokeImage[pid].first = nullptr;

					StrokeImage.erase(pid);
					lock2.unlock();
					StrokeImageSm.erase(pid);

					std::unique_lock<std::shared_mutex> lock3(StrokeImageListSm);
					StrokeImageList.erase(StrokeImageList.begin() + i);
					lock3.unlock();
					i--;

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
		if (!UpdateLayeredWindowIndirect(drawpad_window, &ulwi))
		{
			MessageBox(floating_window, L"智绘教画板显示出现问题，点击确定以重启智绘教\n此方案可能解决该问题", L"智绘教状态监测助手", MB_OK | MB_SYSTEMMODAL);
			offSignal = 2;

			break;
		}

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

		setbkmode(TRANSPARENT);
		setbkcolor(RGB(255, 255, 255));

		BEGIN_TASK_WND(drawpad_window);
		cleardevice();

		Gdiplus::Graphics graphics(GetImageHDC(hiex::GetWindowImage(drawpad_window)));
		Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 50, FontStyleRegular, UnitPixel);
		Gdiplus::SolidBrush WordBrush(Color(255, 0, 0, 0));
		graphics.DrawString(L"Drawpad 预备窗口", -1, &gp_font, PointF(10.0f, 10.0f), &stringFormat_left, &WordBrush);

		END_TASK();
		REDRAW_WINDOW(drawpad_window);

		DisableResizing(drawpad_window, true);//禁止窗口拉伸
		SetWindowLong(drawpad_window, GWL_STYLE, GetWindowLong(drawpad_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
		SetWindowPos(drawpad_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
		SetWindowLong(drawpad_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏
	}

	//初始化数值
	{
		//屏幕快照处理
		LoadDrawpad();
	}

	thread DrawpadDrawing_thread(DrawpadDrawing);
	DrawpadDrawing_thread.detach();
	thread DrawpadInstallHookThread(DrawpadInstallHook);
	DrawpadInstallHookThread.detach();
	thread(KeyboardInteraction).detach();
	{
		SetImageColor(alpha_drawpad, RGBA(0, 0, 0, 0), true);

		//启动绘图库程序
		hiex::Gdiplus_Try_Starup();

		while (!offSignal)
		{
			if (stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection || penetrate.select == true)
			{
				this_thread::sleep_for(chrono::milliseconds(100));
				continue;
			}

			while (1)
			{
				std::shared_lock<std::shared_mutex> LockPointTempSm(PointTempSm);
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
					if (int(state) == 1 && stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser && setlist.RubberRecover) target_status = 0;
					else if (int(state) == 1 && stateMode.StateModeSelect == StateModeSelectEnum::IdtPen && setlist.BrushRecover) target_status = 0;

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

					std::shared_lock<std::shared_mutex> lock1(PointTempSm);
					LONG pid = TouchTemp.front().pid;
					POINT pt = TouchTemp.front().pt;
					lock1.unlock();

					std::unique_lock<std::shared_mutex> lock2(PointTempSm);
					TouchTemp.pop_front();
					lock2.unlock();

					//hiex::PreSetWindowShowState(SW_HIDE);
					//HWND draw_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

					thread MultiFingerDrawing_thread(MultiFingerDrawing, pid, pt, stateMode);
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