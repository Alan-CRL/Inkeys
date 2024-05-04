#include "IdtDrawpad.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtFloating.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
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

			if (choose.select)
			{
				brush.select = true;
				rubber.select = false;
				choose.select = false;
				penetrate.select = false;

				if (SeewoCameraIsOpen) FreezeFrame.mode = 1;
			}
			else
			{
				choose.select = true;
				brush.select = false;
				rubber.select = false;
				penetrate.select = false;

				if (!FreezeFrame.select || penetrate.select) FreezeFrame.mode = 0, FreezeFrame.select = false;
			}
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
					PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(200, 200, 200, 255);
					PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(200, 200, 200, 255);
				}

				std::unique_lock<std::shared_mutex> LockPPTManipulatedSm(PPTManipulatedSm);
				PPTManipulated = std::chrono::high_resolution_clock::now();
				LockPPTManipulatedSm.unlock();
				break;
			}

			case VK_PRIOR:  // PgUp
			case VK_LEFT:   // 左箭头
			case VK_UP:     // 上箭头
			{
				if (KeyBoradDown[(BYTE)pKeyInfo->vkCode])
				{
					PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(200, 200, 200, 255);
					PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(200, 200, 200, 255);
				}

				std::unique_lock<std::shared_mutex> LockPPTManipulatedSm(PPTManipulatedSm);
				PPTManipulated = std::chrono::high_resolution_clock::now();
				LockPPTManipulatedSm.unlock();
				break;
			}

			case VK_ESCAPE:   // 退出
			{
				std::unique_lock<std::shared_mutex> LockPPTManipulatedSm(PPTManipulatedSm);
				PPTManipulated = std::chrono::high_resolution_clock::now();
				LockPPTManipulatedSm.unlock();
				break;
			}
			}
		}

		if (!choose.select && !penetrate.select)
		{
			ExMessage msgKey = {};
			msgKey.message = wParam;
			msgKey.vkcode = (BYTE)pKeyInfo->vkCode;
			msgKey.scancode = (BYTE)pKeyInfo->scanCode;
			msgKey.extended = bool(pKeyInfo->flags & LLKHF_EXTENDED);
			msgKey.prevdown = bool((HIWORD(pKeyInfo->flags) & KF_REPEAT) == KF_REPEAT);

			int index = hiex::GetWindowIndex(drawpad_window, false);
			std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
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
					return CallNextHookEx(DrawpadHookCall, nCode, wParam, lParam);
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
					return CallNextHookEx(DrawpadHookCall, nCode, wParam, lParam);
				}
			}
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
	while (!off_signal && GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// 卸载钩子
	UnhookWindowsHookEx(DrawpadHookCall);
}

double EuclideanDistance(POINT a, POINT b)
{
	return std::sqrt(std::pow(a.x - b.x, 2) + std::pow(a.y - b.y, 2));
}

// Temp
vector<wstring> sswindows;
int GetWindowTextSafe(HWND hWnd, LPTSTR lpString, int nMaxCount)
{
	if (NULL == hWnd || FALSE == IsWindow(hWnd) || NULL == lpString || 0 == nMaxCount)
	{
		return GetWindowText(hWnd, lpString, nMaxCount);
	}
	DWORD dwHwndProcessID = 0;
	DWORD dwHwndThreadID = 0;
	dwHwndThreadID = GetWindowThreadProcessId(hWnd, &dwHwndProcessID);		//获取窗口所属的进程和线程ID

	if (dwHwndProcessID != GetCurrentProcessId())		//窗口进程不是当前调用进程时，返回原本调用
	{
		return GetWindowText(hWnd, lpString, nMaxCount);
	}

	//窗口进程是当前进程时：
	if (dwHwndThreadID == GetCurrentThreadId())			//窗口线程就是当前调用线程，返回原本调用
	{
		return GetWindowText(hWnd, lpString, nMaxCount);
	}

#ifndef _UNICODE
	WCHAR* lpStringUnicode = new WCHAR[nMaxCount];
	InternalGetWindowText(hWnd, lpStringUnicode, nMaxCount);
	int size = WideCharToMultiByte(CP_ACP, 0, lpStringUnicode, -1, NULL, 0, NULL, NULL);
	if (size <= nMaxCount)
	{
		size = WideCharToMultiByte(CP_ACP, 0, lpStringUnicode, -1, lpString, size, NULL, NULL);
		if (NULL != lpStringUnicode)
		{
			delete[]lpStringUnicode;
			lpStringUnicode = NULL;
		}
		return size;
	}
	if (NULL != lpStringUnicode)
	{
		delete[]lpStringUnicode;
		lpStringUnicode = NULL;
	}
	return 0;

#else
	return InternalGetWindowText(hWnd, lpString, nMaxCount);
#endif
}
wstring GetWindowTitle(HWND hWnd)
{
	wchar_t title[256];
	GetWindowTextSafe(hWnd, title, sizeof(title) / sizeof(wchar_t));
	return wstring(title);
}
BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	sswindows.push_back(GetWindowTitle(hWnd));

	return TRUE;
}

void MultiFingerDrawing(LONG pid, POINT pt)
{
	struct Mouse
	{
		int x = 0, y = 0;
		int last_x = 0, last_y = 0;
		int last_length = 0;
	} mouse;
	struct
	{
		bool rubber_choose = rubber.select, brush_choose = brush.select;
		int width = brush.width, mode = brush.mode;
		COLORREF color = brush.color;
	}  draw_info;

	int cxscreen = GetSystemMetrics(SM_CXSCREEN);
	int cyscreen = GetSystemMetrics(SM_CYSCREEN);

	IMAGE Canvas = CreateImageColor(cxscreen, cyscreen, RGBA(0, 0, 0, 0), true);
	IMAGE* BackCanvas = new IMAGE(cxscreen, cyscreen);

	std::chrono::high_resolution_clock::time_point start;
	if (draw_info.rubber_choose == true)
	{
		mouse.last_x = pt.x, mouse.last_y = pt.y;
		double rubbersize = 15, trubbersize = -1;

		//首次绘制
		std::shared_lock<std::shared_mutex> lock1(StrokeBackImageSm);
		Graphics eraser(GetImageHDC(&drawpad));
		lock1.unlock();
		{
			GraphicsPath path;
			path.AddEllipse(float(mouse.last_x - rubbersize / 2.0f), float(mouse.last_y - rubbersize / 2.0f), float(rubbersize), float(rubbersize));

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

			hiex::EasyX_Gdiplus_Ellipse(mouse.last_x - (float)(rubbersize) / 2, mouse.last_y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
		}

		std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
		StrokeImage[pid] = make_pair(&Canvas, 2552);
		lock2.unlock();
		std::unique_lock<std::shared_mutex> lock3(StrokeImageListSm);
		StrokeImageList.emplace_back(pid);
		lock3.unlock();

		while (1)
		{
			std::shared_lock<std::shared_mutex> lock1(PointPosSm);
			bool unfind = TouchPos.find(pid) == TouchPos.end();
			if (unfind)
			{
				lock1.unlock();
				break;
			}
			pt = TouchPos[pid].pt;
			lock1.unlock();

			std::shared_lock<std::shared_mutex> lock2(TouchSpeedSm);
			double speed = TouchSpeed[pid];
			lock2.unlock();

			std::shared_lock<std::shared_mutex> lock3(PointListSm);
			auto it = std::find(TouchList.begin(), TouchList.end(), pid);
			lock3.unlock();

			if (it == TouchList.end()) break;

			mouse.x = pt.x, mouse.y = pt.y;

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

			if ((pt.x == mouse.last_x && pt.y == mouse.last_y))
			{
				GraphicsPath path;
				path.AddEllipse(float(mouse.last_x - rubbersize / 2.0f), float(mouse.last_y - rubbersize / 2.0f), float(rubbersize), float(rubbersize));

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

				std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(mouse.x - (float)(rubbersize) / 2, mouse.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
				lock2.unlock();
			}
			else
			{
				GraphicsPath path;
				path.AddLine(mouse.last_x, mouse.last_y, mouse.x, mouse.y);

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

				std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
				for (const auto& [point, value] : extreme_point)
				{
					if (value == true && region.IsVisible(point.first, point.second))
					{
						extreme_point[{point.first, point.second}] = false;
					}
				}
				LockExtremePointSm.unlock();
				delete widenedPath;

				std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(mouse.x - (float)(rubbersize) / 2, mouse.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
				lock2.unlock();
			}

			mouse.last_x = mouse.x, mouse.last_y = mouse.y;
		}
		SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
	}
	else if (draw_info.brush_choose == true)
	{
		mouse.last_x = pt.x, mouse.last_y = pt.y;

		double writing_distance = 0;
		int instant_writing_distance = 0;
		vector<Point> points = { Point(mouse.last_x, mouse.last_y) };
		RECT circumscribed_rectangle = { -1,-1,-1,-1 };

		//首次绘制
		Graphics graphics(GetImageHDC(&Canvas));
		graphics.SetSmoothingMode(SmoothingModeHighQuality);
		if (draw_info.mode == 1 || draw_info.mode == 2) hiex::EasyX_Gdiplus_SolidEllipse(float((float)mouse.last_x - (float)(draw_info.width) / 2.0), float((float)mouse.last_y - (float)(draw_info.width) / 2.0), (float)draw_info.width, (float)draw_info.width, draw_info.color, false, SmoothingModeHighQuality, &Canvas);

		std::unique_lock<std::shared_mutex> lock1(StrokeImageSm[pid]);
		StrokeImage[pid] = make_pair(&Canvas, draw_info.mode == 2 ? (/*draw_info.color >> 24*/130) * 10 + 0 : 2550);
		lock1.unlock();
		std::unique_lock<std::shared_mutex> lock2(StrokeImageListSm);
		StrokeImageList.emplace_back(pid);
		lock2.unlock();

		bool StopTimingDisable = !setlist.IntelligentDrawing;
		POINT StopTimingPoint = { -1,-1 };
		chrono::high_resolution_clock::time_point StopTiming = std::chrono::high_resolution_clock::now();

		clock_t tRecord = clock();
		while (1)
		{
			// 确认触摸点存在
			{
				std::shared_lock<std::shared_mutex> lock0(PointPosSm);
				bool unfind = TouchPos.find(pid) == TouchPos.end();
				if (unfind)
				{
					lock0.unlock();
					break;
				}
				pt = TouchPos[pid].pt;
				lock0.unlock();

				std::shared_lock<std::shared_mutex> lock2(PointListSm);
				auto it = std::find(TouchList.begin(), TouchList.end(), pid);
				lock2.unlock();

				if (it == TouchList.end()) break;
			}

			// 延迟拉直
			if (draw_info.mode == 1 && !StopTimingDisable && !points.empty())
			{
				if (sqrt((pt.x - StopTimingPoint.x) * (pt.x - StopTimingPoint.x) + (pt.y - StopTimingPoint.y) * (pt.y - StopTimingPoint.y)) > 5)
				{
					StopTimingPoint = pt;
					StopTiming = std::chrono::high_resolution_clock::now();
				}
				else if (chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - StopTiming).count() >= 1000)
				{
					if (sqrt((pt.x - points[0].X) * (pt.x - points[0].X) + (pt.y - points[0].Y) * (pt.y - points[0].Y)) >= 120)
					{
						double redundance = max(GetSystemMetrics(SM_CXSCREEN) / 192, min((GetSystemMetrics(SM_CXSCREEN)) / 76.8, double(GetSystemMetrics(SM_CXSCREEN)) / double((-0.036) * writing_distance + 135)));

						// 5 倍宽松精度
						if (isLine(points, int(redundance * 5.0f), std::chrono::high_resolution_clock::now()))
						{
							draw_info.mode = 3;
							mouse.last_x = points[0].X, mouse.last_y = points[0].Y;
						}
						else StopTimingDisable = true;
					}
				}
			}
			if (pt.x == mouse.last_x && pt.y == mouse.last_y) continue;

			if (draw_info.mode == 3)
			{
				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);
				pen.SetWidth(Gdiplus::REAL(draw_info.width));

				std::unique_lock<std::shared_mutex> LockStrokeImageSm(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				graphics.DrawLine(&pen, mouse.last_x, mouse.last_y, pt.x, pt.y);
				LockStrokeImageSm.unlock();
			}
			else if (draw_info.mode == 4)
			{
				int rectangle_x = min(mouse.last_x, pt.x), rectangle_y = min(mouse.last_y, pt.y);
				int rectangle_heigth = abs(mouse.last_x - pt.x) + 1, rectangle_width = abs(mouse.last_y - pt.y) + 1;

				std::unique_lock<std::shared_mutex> LockStrokeImageSm(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_RoundRect((float)rectangle_x, (float)rectangle_y, (float)rectangle_heigth, (float)rectangle_width, 3, 3, draw_info.color, (float)draw_info.width, false, SmoothingModeHighQuality, &Canvas);
				LockStrokeImageSm.unlock();

				if (points.size() < 2) points.emplace_back(Point(pt.x, pt.y));
				else points[1] = Point(pt.x, pt.y);
			}
			else
			{
				writing_distance += EuclideanDistance({ mouse.last_x, mouse.last_y }, { pt.x, pt.y });

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetEndCap(LineCapRound);
				pen.SetWidth((float)draw_info.width);

				std::unique_lock<std::shared_mutex> lock1(StrokeImageSm[pid]);
				graphics.DrawLine(&pen, mouse.last_x, mouse.last_y, (mouse.x = pt.x), (mouse.y = pt.y));
				lock1.unlock();

				{
					if (mouse.x < circumscribed_rectangle.left || circumscribed_rectangle.left == -1) circumscribed_rectangle.left = mouse.x;
					if (mouse.y < circumscribed_rectangle.top || circumscribed_rectangle.top == -1) circumscribed_rectangle.top = mouse.y;
					if (mouse.x > circumscribed_rectangle.right || circumscribed_rectangle.right == -1) circumscribed_rectangle.right = mouse.x;
					if (mouse.y > circumscribed_rectangle.bottom || circumscribed_rectangle.bottom == -1) circumscribed_rectangle.bottom = mouse.y;

					instant_writing_distance += (int)EuclideanDistance({ mouse.last_x, mouse.last_y }, { pt.x, pt.y });
					if (instant_writing_distance >= 4)
					{
						// if (!points.empty()) points.push_back(Point((points.back().X + mouse.x) / 2.0f, (points.back().Y + mouse.y) / 2.0f));
						points.push_back(Point(mouse.x, mouse.y));
						instant_writing_distance %= 4;
					}
				}

				mouse.last_x = mouse.x, mouse.last_y = mouse.y;
			}

			//防止写锁过快导致无法读锁
			if (draw_info.mode == 3 || draw_info.mode == 4)
			{
				if (tRecord)
				{
					int delay = 1000 / 24 - (clock() - tRecord);
					if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				}
				tRecord = clock();
			}
		}

		start = std::chrono::high_resolution_clock::now();
		std::unique_lock<std::shared_mutex> lock3(StrokeImageSm[pid]);

		ImgCpy(BackCanvas, &Canvas);

		StrokeImage[pid] = make_pair(BackCanvas, draw_info.mode == 2 ? (/*draw_info.color >> 24*/130) * 10 + 0 : 2550);
		lock3.unlock();

		//智能绘图模块
		if (draw_info.mode == 1)
		{
			double redundance = max(GetSystemMetrics(SM_CXSCREEN) / 192, min((GetSystemMetrics(SM_CXSCREEN)) / 76.8, double(GetSystemMetrics(SM_CXSCREEN)) / double((-0.036) * writing_distance + 135)));

			//直线绘制
			if (setlist.IntelligentDrawing && writing_distance >= 120 && (abs(circumscribed_rectangle.left - circumscribed_rectangle.right) >= 120 || abs(circumscribed_rectangle.top - circumscribed_rectangle.bottom) >= 120) && isLine(points, int(redundance), std::chrono::high_resolution_clock::now()))
			{
				Point start(points[0]), end(points[points.size() - 1]);

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

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				pen.SetWidth(Gdiplus::REAL(draw_info.width));
				graphics.DrawLine(&pen, start.X, start.Y, end.X, end.Y);
			}
			//平滑曲线
			else if (setlist.SmoothWriting && points.size() > 2)
			{
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

				Graphics graphics(GetImageHDC(&Canvas));
				graphics.SetSmoothingMode(SmoothingModeHighQuality);

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetLineJoin(LineJoinRound);
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				pen.SetWidth(Gdiplus::REAL(draw_info.width));
				graphics.DrawCurve(&pen, points.data(), points.size(), 0.4f);
			}
		}
		else if (draw_info.mode == 2)
		{
			//平滑曲线
			if (setlist.SmoothWriting && points.size() > 2)
			{
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

				Graphics graphics(GetImageHDC(&Canvas));
				graphics.SetSmoothingMode(SmoothingModeHighQuality);

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetLineJoin(LineJoinRound);
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				pen.SetWidth(Gdiplus::REAL(draw_info.width));
				graphics.DrawCurve(&pen, points.data(), points.size(), 0.4f);
			}
		}
		else if (draw_info.mode == 3); // 直线吸附待实现
		else if (draw_info.mode == 4)
		{
			//端点匹配
			if (setlist.IntelligentDrawing && points.size() == 2)
			{
				Point l1 = points[0];
				Point l2 = Point(points[0].X, points[points.size() - 1].Y);
				Point r1 = Point(points[points.size() - 1].X, points[0].Y);
				Point r2 = points[points.size() - 1];

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

				std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
				extreme_point[{l1.X, l1.Y}] = true;
				extreme_point[{l2.X, l2.Y}] = true;
				extreme_point[{r1.X, r1.Y}] = true;
				extreme_point[{r2.X, r2.Y}] = true;
				LockExtremePointSm.unlock();

				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

				int x = min(l1.X, r2.X);
				int y = min(l1.Y, r2.Y);
				int w = abs(l1.X - r2.X) + 1;
				int h = abs(l1.Y - r2.Y) + 1;

				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_RoundRect((float)x, (float)y, (float)w, (float)h, 3, 3, draw_info.color, (float)draw_info.width, false, SmoothingModeHighQuality, &Canvas);
			}
		}
	}

	std::unique_lock<std::shared_mutex> lock3(PointPosSm);
	TouchPos.erase(pid);
	lock3.unlock();

	std::unique_lock<std::shared_mutex> lock4(StrokeImageSm[pid]);

	ImgCpy(BackCanvas, &Canvas);

	if (draw_info.rubber_choose == true) StrokeImage[pid] = make_pair(BackCanvas, 2553);
	else if (draw_info.brush_choose == true)
	{
		if (draw_info.mode == 2) StrokeImage[pid] = make_pair(BackCanvas, (/*draw_info.color >> 24*/130) * 10 + 1);
		else StrokeImage[pid] = make_pair(BackCanvas, 2551);
	}

	lock4.unlock();
}
void DrawpadDrawing()
{
	thread_status[L"DrawpadDrawing"] = true;

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

	do
	{
		Sleep(10);
		::SetWindowLong(drawpad_window, GWL_EXSTYLE, ::GetWindowLong(drawpad_window, GWL_EXSTYLE) | WS_EX_LAYERED);
	} while (!(::GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_LAYERED));
	do
	{
		Sleep(10);
		::SetWindowLong(drawpad_window, GWL_EXSTYLE, ::GetWindowLong(drawpad_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
	} while (!(::GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE));

	window_background.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	drawpad.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

	SetImageColor(window_background, RGBA(0, 0, 0, 0), true);
	SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
	{
		ulwi.hdcSrc = GetImageHDC(&window_background);
		UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
	}
	ShowWindow(drawpad_window, SW_SHOW);

	chrono::high_resolution_clock::time_point reckon;
	clock_t tRecord = 0;
	for (;;)
	{
		for (int for_i = 1;; for_i = 2)
		{
			if (choose.select == true)
			{
			ChooseEnd:
				{
					SetImageColor(window_background, RGBA(0, 0, 0, 0), true);
					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}

				IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
				if (reference_record_pointer == current_record_pointer && !CompareImagesWithBuffer(&empty_drawpad, &drawpad))
				{
					if (RecallImage.empty())
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

						if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
						{
							thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
							SaveScreenShot_thread.detach();
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

					RecallImage.back().type = 1;
					if (off_signal) SaveScreenShot(RecallImage.back().img, true);
					else
					{
						thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
						SaveScreenShot_thread.detach();
					}

					extreme_point.clear();
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

				if (off_signal) goto DrawpadDrawingEnd;

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

				int ppt_switch_count = 0;
				while (choose.select)
				{
					Sleep(50);

					if (PptInfoStateBuffer.CurrentPage != PptInfoState.CurrentPage) PptInfoStateBuffer.CurrentPage = PptInfoState.CurrentPage, ppt_switch_count++;
					PptInfoStateBuffer.TotalPage = PptInfoState.TotalPage;

					if (off_signal) goto DrawpadDrawingEnd;
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
						if (PptInfoStateBuffer.CurrentPage != temp_currentpage && PptInfoStateBuffer.TotalPage == temp_totalpage)
						{
							IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
							if (reference_record_pointer == current_record_pointer && !CompareImagesWithBuffer(&empty_drawpad, &drawpad))
							{
								if (RecallImage.empty())
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

									if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
									{
										thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
										SaveScreenShot_thread.detach();
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

								RecallImage.back().type = 1;
								thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
								SaveScreenShot_thread.detach();

								PptImg.IsSave = true;
								PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] = true;
								PptImg.Image[PptInfoStateBuffer.CurrentPage] = RecallImage.back().img;

								extreme_point.clear();
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

					Sleep(50);

					if (off_signal) goto DrawpadDrawingEnd;
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
			if (start) break;

			if (off_signal)
			{
				if (!choose.select) goto ChooseEnd;
				goto DrawpadDrawingEnd;
			}

			int temp_currentpage = PptInfoState.CurrentPage, temp_totalpage = PptInfoState.TotalPage;
			if (PptInfoStateBuffer.CurrentPage != temp_currentpage || PptInfoStateBuffer.TotalPage != temp_totalpage)
			{
				if (PptInfoStateBuffer.CurrentPage != temp_currentpage && PptInfoStateBuffer.TotalPage == temp_totalpage)
				{
					IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
					if (reference_record_pointer == current_record_pointer && !CompareImagesWithBuffer(&empty_drawpad, &drawpad))
					{
						if (RecallImage.empty())
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

							if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
							{
								thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
								SaveScreenShot_thread.detach();
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

						RecallImage.back().type = 1;
						thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
						SaveScreenShot_thread.detach();

						PptImg.IsSave = true;
						PptImg.IsSaved[PptInfoStateBuffer.CurrentPage] = true;
						PptImg.Image[PptInfoStateBuffer.CurrentPage] = RecallImage.back().img;

						extreme_point.clear();
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
					choose.select = true;

					brush.select = false;
					rubber.select = false;
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

			Sleep(10);
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
			MessageBox(floating_window, L"智绘教画板显示出现问题，点击确定以重启智绘教\n此方案可能解决该问题", L"智绘教警告", MB_OK | MB_SYSTEMMODAL);

			{
				sswindows.clear();
				EnumWindows(EnumWindowsProc, 0);

				ofstream writejson;
				writejson.imbue(locale("zh_CN.UTF8"));
				writejson.open(wstring_to_string(string_to_wstring(global_path) + L"bug fix 240408.01.log").c_str());

				writejson << "UpdateLayeredWindowIndirect Error" << GetLastError() << endl;
				for (int i = 0; i < (int)sswindows.size(); i++)
				{
					writejson << to_string(i) << " " + wstring_to_string(sswindows[i]) << endl;
				}

				writejson.close();
			}

			off_signal = 2;

			break;
		}

		tRecord = clock();
	}

DrawpadDrawingEnd:
	thread_status[L"DrawpadDrawing"] = false;
	return;
}
int drawpad_main()
{
	thread_status[L"drawpad_main"] = true;

	//画笔初始化
	{
		brush.width = 3;
		brush.color = brush.primary_colour = RGBA(50, 30, 181, 255);
	}
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

	magnificationWindowReady++;

	thread DrawpadDrawing_thread(DrawpadDrawing);
	DrawpadDrawing_thread.detach();
	thread DrawpadInstallHookThread(DrawpadInstallHook);
	DrawpadInstallHookThread.detach();
	{
		SetImageColor(alpha_drawpad, RGBA(0, 0, 0, 0), true);

		//启动绘图库程序
		hiex::Gdiplus_Try_Starup();

		while (!off_signal)
		{
			if (choose.select == true || penetrate.select == true)
			{
				Sleep(100);
				continue;
			}

			while (1)
			{
				std::shared_lock<std::shared_mutex> lock1(PointTempSm);
				bool start = !TouchTemp.empty();
				lock1.unlock();
				//开始绘图
				if (start)
				{
					if (int(state) == 1 && rubber.select && setlist.RubberRecover) target_status = 0;
					else if (int(state) == 1 && brush.select && setlist.BrushRecover) target_status = 0;

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

					thread MultiFingerDrawing_thread(MultiFingerDrawing, pid, pt);
					MultiFingerDrawing_thread.detach();
				}
				else break;
			}

			Sleep(10);
		}
	}

	ShowWindow(drawpad_window, SW_HIDE);

	int i = 1;
	for (; i <= 10; i++)
	{
		if (!thread_status[L"DrawpadDrawing"]) break;
		Sleep(500);
	}
	thread_status[L"drawpad_main"] = false;
	return 0;
}