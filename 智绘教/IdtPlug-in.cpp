/*
 * @file		IdtPlug-in.cpp
 * @brief		IDT plugin linkage | 智绘教插件联动
 * @note		PPT linkage components and other plugins | PPT联动组件和其他插件等
 *
 * @author		AlanCRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// INFO: This source file will take the lead in refactoring the code logic and optimizing the reading experience.
// 提示：这个源文件将率先重构代码逻辑，并优化阅读体验。

// 常问问题：一些注释后带有 '*' 号，它们的解释在下面。
//
// *1
//
// PptInfoStateBuffer 变量是 PptInfoState 变量的缓冲，当 DrawpadDrawing 函数加载完成 PPT 的画板后，缓冲变量中的值才会变为和 PptInfoState 一致。
// 一些函数获取 PptInfoStateBuffer 的值，必须要等到 PPT 画板初始化完毕后才会有所改变，并再做出反应。

#include "IdtPlug-in.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFloating.h"
#include "IdtMagnification.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtWindow.h"
#include "IdtOther.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtImage.h"
#include "IdtState.h"
#include "IdtI18n.h"
#include "Inkeys/Other/IdtInputs.h"

#include <objbase.h>
#include <psapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// --------------------------------------------------
// PPT 联动插件

#import "PptCOM.tlb" // C# 类库 PptCOM 项目库 (PptCOM. cs)
using namespace PptCOM;
IPptCOMServerPtr PptCOMPto;

// -------------------------
// UI 对象

bool PptUiAnimationEnable;

PptUiLineWidgetClass pptUiLineWidget[5], pptUiLineWidgetTarget[5];
PptUiRoundRectWidgetClass pptUiRoundRectWidget[15], pptUiRoundRectWidgetTarget[15];
PptUiImageWidgetClass pptUiImageWidget[9], pptUiImageWidgetTarget[9];
PptUiWordsWidgetClass pptUiWordsWidget[8], pptUiWordsWidgetTarget[8];

ID2D1Bitmap* pptIconBitmap[6];

void PptUiWidgetValueTransformation(float* v, float tv, float s, float e, int num = 1)
{
	if (!s || !e)
	{
		*v = tv;
		return;
	}

	while (num--)
	{
		if (abs(*v - tv) <= e) *v = tv;
		else if (*v < tv) *v = float(*v + max(e, (tv - *v) / s));
		else *v = float(*v + min(-e, (tv - *v) / s));
	}
}
void PptUiWidgetColorTransformation(COLORREF* v, COLORREF tv, float s, float e, int num = 1)
{
	if (!s || !e)
	{
		*v = tv;
		return;
	}

	while (num--)
	{
		float r1 = GetRValue(*v);
		float g1 = GetGValue(*v);
		float b1 = GetBValue(*v);
		float a1 = float(((*v) >> 24) & 0xff);

		float r2 = GetRValue(tv);
		float g2 = GetGValue(tv);
		float b2 = GetBValue(tv);
		float a2 = float((tv >> 24) & 0xff);

		if (abs(r1 - r2) <= e) r1 = r2;
		else if (r1 < r2) r1 = r1 + max(e, (r2 - r1) / s);
		else if (r1 > r2) r1 = r1 + min(-e, (r2 - r1) / s);

		if (abs(g1 - g2) <= e) g1 = g2;
		else if (g1 < g2) g1 = g1 + max(e, (g2 - g1) / s);
		else if (g1 > g2) g1 = g1 + min(-e, (g2 - g1) / s);

		if (abs(b1 - b2) <= e) b1 = b2;
		else if (b1 < b2) b1 = b1 + max(e, (b2 - b1) / s);
		else if (b1 > b2) b1 = b1 + min(-e, (b2 - b1) / s);

		if (abs(a1 - a2) <= e) a1 = a2;
		else if (a1 < a2) a1 = a1 + max(e, (a2 - a1) / s);
		else if (a1 > a2) a1 = a1 + min(-e, (a2 - a1) / s);

		*v = RGBA(max(0, min(255, (int)r1)), max(0, min(255, (int)g1)), max(0, min(255, (int)b1)), max(0, min(255, (int)a1)));
	}
}

bool PptUiIsInEllipse(float x, float y, float x1, float y1, float w, float h)
{
	float x0 = x1 + w / 2.0f;
	float y0 = y1 + h / 2.0f;

	float value = ((x - x0) * (x - x0)) / ((w * w) / 4.0f) + ((y - y0) * (y - y0)) / ((h * h) / 4.0f);

	return value <= 1.0f;
}
bool PptUiIsInRoundRect(float x, float y, PptUiRoundRectWidgetClass pptUiRoundRectWidget)
{
	if (pptUiRoundRectWidget.X.v + pptUiRoundRectWidget.EllipseWidth.v / 2.0f <= x && x <= pptUiRoundRectWidget.X.v + pptUiRoundRectWidget.Width.v - pptUiRoundRectWidget.EllipseWidth.v / 2.0f &&
		pptUiRoundRectWidget.Y.v <= y && y <= pptUiRoundRectWidget.Y.v + pptUiRoundRectWidget.Height.v)
		return true;
	if (pptUiRoundRectWidget.X.v <= x && x <= pptUiRoundRectWidget.X.v + pptUiRoundRectWidget.Width.v &&
		pptUiRoundRectWidget.Y.v + pptUiRoundRectWidget.EllipseHeight.v / 2.0f <= y && y <= pptUiRoundRectWidget.Y.v + pptUiRoundRectWidget.Height.v - pptUiRoundRectWidget.EllipseHeight.v / 2.0f)
		return true;

	if (PptUiIsInEllipse(x, y, pptUiRoundRectWidget.X.v, pptUiRoundRectWidget.Y.v, pptUiRoundRectWidget.EllipseWidth.v, pptUiRoundRectWidget.EllipseHeight.v))
		return true;
	if (PptUiIsInEllipse(x, y, pptUiRoundRectWidget.X.v + pptUiRoundRectWidget.Width.v - pptUiRoundRectWidget.EllipseWidth.v, pptUiRoundRectWidget.Y.v, pptUiRoundRectWidget.EllipseWidth.v, pptUiRoundRectWidget.EllipseHeight.v))
		return true;
	if (PptUiIsInEllipse(x, y, pptUiRoundRectWidget.X.v, pptUiRoundRectWidget.Y.v + pptUiRoundRectWidget.Height.v - pptUiRoundRectWidget.EllipseHeight.v, pptUiRoundRectWidget.EllipseWidth.v, pptUiRoundRectWidget.EllipseHeight.v))
		return true;
	if (PptUiIsInEllipse(x, y, pptUiRoundRectWidget.X.v + pptUiRoundRectWidget.Width.v - pptUiRoundRectWidget.EllipseWidth.v, pptUiRoundRectWidget.Y.v + pptUiRoundRectWidget.Height.v - pptUiRoundRectWidget.EllipseHeight.v, pptUiRoundRectWidget.EllipseWidth.v, pptUiRoundRectWidget.EllipseHeight.v))
		return true;

	return false;
}

IMAGE PptIcon[6]; // PPT 控件的按键图标
IMAGE PptWindowBackground; // PPT 窗口背景画布

bool PptUiChangeSignal;
int PptUiAllReplaceSignal;

// -------------------------
// ppt 信息

PptImgStruct PptImg = { false }; // 其存储幻灯片放映时产生的图像数据。
PptInfoStateStruct PptInfoState = { -1, -1 }; // 其存储幻灯片放映软件当前的状态，First 代表总幻灯片页数，Second 代表当前幻灯片编号。
PptInfoStateStruct PptInfoStateBuffer = { -1, -1 }; // PptInfoState 的缓冲变量。*1

wstring pptComVersion;
wstring pptComExtraWarning;

// -------------------------
// Ppt 状态

PptUiWidgetStateEnum pptUiWidgetState = PptUiWidgetStateEnum::Close;

// -------------------------
// Ppt 主项

bool CheckPptCom()
{
	try
	{
		_com_util::CheckError(PptCOMPto.CreateInstance(_uuidof(PptCOMServer)));
	}
	catch (_com_error err)
	{
		pptComVersion = L"Error: C++端初始化异常：" + wstring(err.ErrorMessage());
		return false;
	}

	try
	{
		pptComVersion = PptCOMPto->CheckCOM();
	}
	catch (_com_error err)
	{
		pptComVersion = L"Error：C++ 端 COM 初始化异常：" + wstring(err.ErrorMessage());
		return false;
	}

	if (pptComVersion.find(L"\n") != pptComVersion.npos)
	{
		pptComExtraWarning = pptComVersion.substr(pptComVersion.find('\n') + 1);
		pptComVersion = pptComVersion.substr(0, pptComVersion.find('\n'));

		//Testw(pptComExtraWarning);
		//Testw(pptComVersion);

		// TODO ？
	}

	return true;
}

LRESULT CALLBACK PptWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
	{
		DWORD flags = 0;
		flags |= (0x00000001);
		flags |= (0x00000008);
		flags |= (0x00000100);
		flags |= (0x00000200);
		flags |= (0x00010000);
		return (LRESULT)flags;
	}

	case WM_TOUCH:
	{
		static DWORD activeTouchId = 0;   // 0表示无活动ID
		static bool isTouchActive = false;

		UINT cInputs = LOWORD(wParam);
		TOUCHINPUT inputs[32];
		if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, inputs, sizeof(TOUCHINPUT)))
		{
			bool touchIdCheck = false; // 检测当前活动ID是否还存在
			short x = 0, y = 0; // 坐标

			for (UINT i = 0; i < cInputs; i++)
			{
				const TOUCHINPUT& ti = inputs[i];

				double xO = static_cast<double>(ti.x) / 100.0;
				double yO = static_cast<double>(ti.y) / 100.0;
				x = static_cast<short>(xO + 0.5);
				y = static_cast<short>(yO + 0.5);

				if (ti.dwFlags & TOUCHEVENTF_DOWN)
				{
					// 如果当前无activeID，则锁定第一个DOWN点
					if (!isTouchActive)
					{
						activeTouchId = ti.dwID;
						isTouchActive = true;

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_LBUTTONDOWN;
							msgMouse.x = x;
							msgMouse.y = y;
							msgMouse.lbutton = true;

							int index = hiex::GetWindowIndex(ppt_window, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}
				}
				if (ti.dwFlags & TOUCHEVENTF_MOVE)
				{
					if (isTouchActive && ti.dwID == activeTouchId)
					{
						ExMessage msgMouse = {};
						msgMouse.message = WM_MOUSEMOVE;
						msgMouse.x = x;
						msgMouse.y = y;
						msgMouse.lbutton = true;

						int index = hiex::GetWindowIndex(ppt_window, false);
						unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
						hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
						lg_vecWindows_vecMessage_sm.unlock();
					}
				}
				if (ti.dwFlags & TOUCHEVENTF_UP)
				{
					if (isTouchActive && ti.dwID == activeTouchId)
					{
						activeTouchId = 0;
						isTouchActive = false;

						{
							ExMessage msgMouse = {};
							msgMouse.message = WM_LBUTTONUP;
							msgMouse.x = x;
							msgMouse.y = y;
							msgMouse.lbutton = false;

							int index = hiex::GetWindowIndex(ppt_window, false);
							unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
							hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
							lg_vecWindows_vecMessage_sm.unlock();
						}
					}
				}

				if (isTouchActive && ti.dwID == activeTouchId) touchIdCheck = true;
			}

			if (isTouchActive && !touchIdCheck)
			{
				activeTouchId = 0;
				isTouchActive = false;

				{
					ExMessage msgMouse = {};
					msgMouse.message = WM_LBUTTONUP;
					msgMouse.x = x;
					msgMouse.y = y;
					msgMouse.lbutton = false;

					int index = hiex::GetWindowIndex(ppt_window, false);
					unique_lock lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
					hiex::g_vecWindows[index].vecMessage.push_back(msgMouse);
					lg_vecWindows_vecMessage_sm.unlock();
				}
			}

			CloseTouchInputHandle((HTOUCHINPUT)lParam);
		}

		return 0;
	}

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MOUSEMOVE:
	{
		// 如果是触摸模拟出来的鼠标消息，就直接丢掉
		DWORD extraInfo = GetMessageExtraInfo();
		if ((extraInfo & 0xFFFFFF00) == 0xFF515700) return 0;

		// 否则当成真正的鼠标消息处理
		// 您的鼠标处理逻辑
		break;
	}

	default:
		return HIWINDOW_DEFAULT_PROC;
	}

	return HIWINDOW_DEFAULT_PROC;
}

wstring GetPptTitle()
{
	wstring ret = L"";

	try
	{
		ret = bstrToWstring(PptCOMPto->SlideNameIndex());
	}
	catch (_com_error)
	{
	}

	return ret;
}
HWND GetPptShow()
{
	HWND hWnd = NULL;

	try
	{
		_variant_t result = PptCOMPto->GetPptHwnd();
		hWnd = (HWND)result.llVal;

		return hWnd;
	}
	catch (_com_error)
	{
	}

	return hWnd;
}
void GetPptState()
{
	threadStatus[L"GetPptState"] = true;

	// 初始化
	{
		bool rel = false;
		rel = CheckPptCom();

		if (rel)
		{
			try
			{
				rel = PptCOMPto->Initialization(reinterpret_cast<long*>(&PptInfoState.TotalPage),
					reinterpret_cast<long*>(&PptInfoState.CurrentPage),
					reinterpret_cast<long*>(&offSignal));
			}
			catch (_com_error err)
			{
				pptComVersion = L"Error: C++端初始化异常：" + wstring(err.ErrorMessage());
				rel = false;
			}
		}
		if (!rel) return;
	}

	while (!offSignal)
	{
		int tmp = -1;

		try
		{
			tmp = PptCOMPto->PptComService();
		}
		catch (_com_error)
		{
		}

		PptInfoState.TotalPage = PptInfoState.CurrentPage = -1;

		if (tmp <= 0)
		{
			for (int i = 0; i <= 20 && !offSignal; i++)
				this_thread::sleep_for(chrono::milliseconds(100));
		}
	}

	threadStatus[L"GetPptState"] = false;
}

void NextPptSlides(int check)
{
	try
	{
		PptCOMPto->NextSlideShow((bool)(check == -1));
	}
	catch (_com_error)
	{
	}

	return;
}
void PreviousPptSlides()
{
	try
	{
		PptCOMPto->PreviousSlideShow();
	}
	catch (_com_error)
	{
	}

	return;
}
void EndPptShow()
{
	try
	{
		FocusPptShow();
		PptCOMPto->EndSlideShow();
	}
	catch (_com_error)
	{
	}

	return;
}
void ViewPptShow()
{
	try
	{
		FocusPptShow();
		PptCOMPto->ViewSlideShow();
	}
	catch (_com_error)
	{
	}

	return;
}
void FocusPptShow()
{
	if (ppt_show != NULL)
	{
		SetForegroundWindow(ppt_show);
	}

	// 都需要保证激活
	{
		try
		{
			PptCOMPto->ActivateSildeShowWindow();
		}
		catch (_com_error)
		{
		}
	}

	return;
}

double PptBottomPageWidgetSeekBar(int firstX, int firstY, bool xReverse)
{
	if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) return 0.0;
	PptUiAllReplaceSignal = 1;

	MonitorInfoStruct PPTMainMonitor;
	shared_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
	PPTMainMonitor = MainMonitor;
	DisplaysInfoLock.unlock();

	double ret = 0.0;
	int firX = static_cast<int>(firstX);
	int firY = static_cast<int>(firstY);

	// 自身数值记录
	float widthFirst = pptComSetlist.bottomBothWidth;
	float heightFirst = pptComSetlist.bottomBothHeight;
	float widthTarget = pptComSetlist.bottomBothWidth;
	float heightTarget = pptComSetlist.bottomBothHeight;
	float widthFeasible = pptComSetlist.bottomBothWidth;
	float heightFeasible = pptComSetlist.bottomBothHeight;

	float LeftWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v - widthFirst;
	float LeftWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + heightFirst;
	float LeftWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Width.v;
	float LeftWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v;
	float RightWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v + widthFirst;
	float RightWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + heightFirst;
	float RightWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Width.v;
	float RightWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height.v;

	float WidgetScale = min(min(pptComSetlist.bottomSideBothWidgetScale, pptComSetlist.middleSideBothWidgetScale), pptComSetlist.bottomSideMiddleWidgetScale);

	// 对象数值记录
	float BottomMiddleWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v;
	float BottomMiddleWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v;
	float BottomMiddleWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Width.v;
	float BottomMiddleWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height.v;
	float MiddleLeftWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v;
	float MiddleLeftWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v;
	float MiddleLeftWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v;
	float MiddleLeftWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Height.v;
	float MiddleRightWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v;
	float MiddleRightWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v;
	float MiddleRightWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v;
	float MiddleRightWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Height.v;

	for (;;)
	{
		if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;

		POINT p;
		GetCursorPos(&p);

		if (xReverse) widthTarget = widthFirst - float(p.x - firstX);
		else widthTarget = widthFirst + float(p.x - firstX);
		heightTarget = heightFirst - float(p.y - firstY);

		// 排斥/吸附边框
		{
			// 左侧
			{
				if (LeftWidgetX + widthTarget <= 5 * WidgetScale) widthTarget = 5 * WidgetScale - LeftWidgetX;
				if (LeftWidgetY - heightTarget <= 5 * WidgetScale) heightTarget = LeftWidgetY - 5 * WidgetScale;
				if (LeftWidgetX + widthTarget + LeftWidgetWidth >= PPTMainMonitor.MonitorWidth / 2.0f - 5 * WidgetScale) widthTarget = PPTMainMonitor.MonitorWidth / 2.0f - 5 * WidgetScale - LeftWidgetX - LeftWidgetWidth;
				if (LeftWidgetY - heightTarget + LeftWidgetHeight >= PPTMainMonitor.MonitorHeight - 5 * WidgetScale) heightTarget = LeftWidgetY + LeftWidgetHeight - PPTMainMonitor.MonitorHeight + 5 * WidgetScale;
			}
			// 右侧
			{
				if (RightWidgetX - widthTarget <= PPTMainMonitor.MonitorWidth / 2.0f + 5 * WidgetScale) widthTarget = RightWidgetX - PPTMainMonitor.MonitorWidth / 2.0f - 5 * WidgetScale;
				if (RightWidgetY - heightTarget <= 5 * WidgetScale) heightTarget = RightWidgetY - 5 * WidgetScale;
				if (RightWidgetX - widthTarget + RightWidgetWidth >= PPTMainMonitor.MonitorWidth - 5 * WidgetScale) widthTarget = RightWidgetX + RightWidgetWidth - PPTMainMonitor.MonitorWidth + 5 * WidgetScale;
				if (RightWidgetY - heightTarget + RightWidgetHeight >= PPTMainMonitor.MonitorHeight - 5 * WidgetScale) heightTarget = RightWidgetY + RightWidgetHeight - PPTMainMonitor.MonitorHeight + 5 * WidgetScale;
			}

			if (abs(widthTarget) <= 10 && abs(heightTarget) <= 10)
			{
				widthTarget = 0;
				heightTarget = 0;
			}
		}

		bool repelSignal = false;
		// 左侧排斥对象
		{
			// 底部中间控件
			if (pptComSetlist.showBottomMiddle)
			{
				if (LeftWidgetY - heightTarget <= BottomMiddleWidgetY + BottomMiddleWidgetHeight + 10 * WidgetScale && LeftWidgetY - heightTarget + LeftWidgetHeight >= BottomMiddleWidgetY - 10 * WidgetScale
					&& LeftWidgetX + widthTarget <= BottomMiddleWidgetX + BottomMiddleWidgetWidth + 10 * WidgetScale && LeftWidgetX + widthTarget + LeftWidgetWidth >= BottomMiddleWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
			// 中部左侧控件
			if (pptComSetlist.showMiddleBoth)
			{
				if (LeftWidgetY - heightTarget <= MiddleLeftWidgetY + MiddleLeftWidgetHeight + 10 * WidgetScale && LeftWidgetY - heightTarget + LeftWidgetHeight >= MiddleLeftWidgetY - 10 * WidgetScale
					&& LeftWidgetX + widthTarget <= MiddleLeftWidgetX + MiddleLeftWidgetWidth + 10 * WidgetScale && LeftWidgetX + widthTarget + LeftWidgetWidth >= MiddleLeftWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
		}
		// 右侧排斥对象
		{
			// 底部中间控件
			if (pptComSetlist.showBottomMiddle)
			{
				if (RightWidgetY - heightTarget <= BottomMiddleWidgetY + BottomMiddleWidgetHeight + 10 * WidgetScale && RightWidgetY - heightTarget + RightWidgetHeight >= BottomMiddleWidgetY - 10 * WidgetScale
					&& RightWidgetX - widthTarget <= BottomMiddleWidgetX + BottomMiddleWidgetWidth + 10 * WidgetScale && RightWidgetX - widthTarget + RightWidgetWidth >= BottomMiddleWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
			// 中部右侧控件
			if (pptComSetlist.showMiddleBoth)
			{
				if (RightWidgetY - heightTarget <= MiddleRightWidgetY + MiddleRightWidgetHeight + 10 * WidgetScale && RightWidgetY - heightTarget + RightWidgetHeight >= MiddleRightWidgetY - 10 * WidgetScale
					&& RightWidgetX - widthTarget <= MiddleRightWidgetX + MiddleRightWidgetWidth + 10 * WidgetScale && RightWidgetX - widthTarget + RightWidgetWidth >= MiddleRightWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
		}
		// 更新可行值
		if (!repelSignal)
		{
			widthFeasible = widthTarget;
			heightFeasible = heightTarget;
		}

		pptComSetlist.bottomBothWidth = widthTarget;
		pptComSetlist.bottomBothHeight = heightTarget;

		ret += sqrt((p.x - firX) * (p.x - firX) + (p.y - firY) * (p.y - firY));
		firX = static_cast<int>(p.x), firY = static_cast<int>(p.y);

		this_thread::sleep_for(chrono::milliseconds(5));
	}
	// 写入文件
	if (pptComSetlist.memoryWidgetPosition && (pptComSetlist.bottomBothWidth != widthFirst || pptComSetlist.bottomBothHeight != heightFirst))
		PptComWriteSetting();

	PptUiAllReplaceSignal = -1;
	return ret;
}
double PptMiddlePageWidgetSeekBar(int firstX, int firstY, bool xReverse)
{
	if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) return 0.0;
	PptUiAllReplaceSignal = 1;

	MonitorInfoStruct PPTMainMonitor;
	shared_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
	PPTMainMonitor = MainMonitor;
	DisplaysInfoLock.unlock();

	double ret = 0.0;
	int firX = static_cast<int>(firstX);
	int firY = static_cast<int>(firstY);

	// 自身数值记录
	float widthFirst = pptComSetlist.middleBothWidth;
	float heightFirst = pptComSetlist.middleBothHeight;
	float widthTarget = pptComSetlist.middleBothWidth;
	float heightTarget = pptComSetlist.middleBothHeight;
	float widthFeasible = pptComSetlist.middleBothWidth;
	float heightFeasible = pptComSetlist.middleBothHeight;

	float LeftWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v - widthFirst;
	float LeftWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v + heightFirst;
	float LeftWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v;
	float LeftWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Height.v;
	float RightWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + widthFirst;
	float RightWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v + heightFirst;
	float RightWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v;
	float RightWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Height.v;

	float WidgetScale = min(min(pptComSetlist.bottomSideBothWidgetScale, pptComSetlist.middleSideBothWidgetScale), pptComSetlist.bottomSideMiddleWidgetScale);

	// 对象数值记录
	float BottomMiddleWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v;
	float BottomMiddleWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v;
	float BottomMiddleWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Width.v;
	float BottomMiddleWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height.v;
	float BottomLeftWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v;
	float BottomLeftWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v;
	float BottomLeftWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Width.v;
	float BottomLeftWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v;
	float BottomRightWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v;
	float BottomRightWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v;
	float BottomRightWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Width.v;
	float BottomRightWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height.v;

	for (;;)
	{
		if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;

		POINT p;
		GetCursorPos(&p);

		if (xReverse) widthTarget = widthFirst - float(p.x - firstX);
		else widthTarget = widthFirst + float(p.x - firstX);
		heightTarget = heightFirst - float(p.y - firstY);

		// 排斥/吸附边框
		{
			// 左侧
			{
				if (LeftWidgetX + widthTarget <= 5 * WidgetScale) widthTarget = 5 * WidgetScale - LeftWidgetX;
				if (LeftWidgetY - heightTarget <= 5 * WidgetScale) heightTarget = LeftWidgetY - 5 * WidgetScale;
				if (LeftWidgetX + widthTarget + LeftWidgetWidth >= PPTMainMonitor.MonitorWidth / 2.0f - 5 * WidgetScale) widthTarget = PPTMainMonitor.MonitorWidth / 2.0f - 5 * WidgetScale - LeftWidgetX - LeftWidgetWidth;
				if (LeftWidgetY - heightTarget + LeftWidgetHeight >= PPTMainMonitor.MonitorHeight - 5 * WidgetScale) heightTarget = LeftWidgetY + LeftWidgetHeight - PPTMainMonitor.MonitorHeight + 5 * WidgetScale;
			}
			// 右侧
			{
				if (RightWidgetX - widthTarget <= PPTMainMonitor.MonitorWidth / 2.0f + 5 * WidgetScale) widthTarget = RightWidgetX - PPTMainMonitor.MonitorWidth / 2.0f - 5 * WidgetScale;
				if (RightWidgetY - heightTarget <= 5 * WidgetScale) heightTarget = RightWidgetY - 5 * WidgetScale;
				if (RightWidgetX - widthTarget + RightWidgetWidth >= PPTMainMonitor.MonitorWidth - 5 * WidgetScale) widthTarget = RightWidgetX + RightWidgetWidth - PPTMainMonitor.MonitorWidth + 5 * WidgetScale;
				if (RightWidgetY - heightTarget + RightWidgetHeight >= PPTMainMonitor.MonitorHeight - 5 * WidgetScale) heightTarget = RightWidgetY + RightWidgetHeight - PPTMainMonitor.MonitorHeight + 5 * WidgetScale;
			}

			if (abs(widthTarget) <= 10 && abs(heightTarget) <= 10)
			{
				widthTarget = 0;
				heightTarget = 0;
			}
		}

		bool repelSignal = false;
		// 左侧排斥对象
		{
			// 底部中间控件
			if (pptComSetlist.showBottomMiddle)
			{
				if (LeftWidgetY - heightTarget <= BottomMiddleWidgetY + BottomMiddleWidgetHeight + 10 * WidgetScale && LeftWidgetY - heightTarget + LeftWidgetHeight >= BottomMiddleWidgetY - 10 * WidgetScale
					&& LeftWidgetX + widthTarget <= BottomMiddleWidgetX + BottomMiddleWidgetWidth + 10 * WidgetScale && LeftWidgetX + widthTarget + LeftWidgetWidth >= BottomMiddleWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
			// 底部左侧控件
			if (pptComSetlist.showBottomBoth)
			{
				if (LeftWidgetY - heightTarget <= BottomLeftWidgetY + BottomLeftWidgetHeight + 10 * WidgetScale && LeftWidgetY - heightTarget + LeftWidgetHeight >= BottomLeftWidgetY - 10 * WidgetScale
					&& LeftWidgetX + widthTarget <= BottomLeftWidgetX + BottomLeftWidgetWidth + 10 * WidgetScale && LeftWidgetX + widthTarget + LeftWidgetWidth >= BottomLeftWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
		}
		// 右侧排斥对象
		{
			// 底部中间控件
			if (pptComSetlist.showBottomMiddle)
			{
				if (RightWidgetY - heightTarget <= BottomMiddleWidgetY + BottomMiddleWidgetHeight + 10 * WidgetScale && RightWidgetY - heightTarget + RightWidgetHeight >= BottomMiddleWidgetY - 10 * WidgetScale
					&& RightWidgetX - widthTarget <= BottomMiddleWidgetX + BottomMiddleWidgetWidth + 10 * WidgetScale && RightWidgetX - widthTarget + RightWidgetWidth >= BottomMiddleWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
			// 底部右侧控件
			if (pptComSetlist.showBottomBoth)
			{
				if (RightWidgetY - heightTarget <= BottomRightWidgetY + BottomRightWidgetHeight + 10 * WidgetScale && RightWidgetY - heightTarget + RightWidgetHeight >= BottomRightWidgetY - 10 * WidgetScale
					&& RightWidgetX - widthTarget <= BottomRightWidgetX + BottomRightWidgetWidth + 10 * WidgetScale && RightWidgetX - widthTarget + RightWidgetWidth >= BottomRightWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
		}
		// 更新可行值
		if (!repelSignal)
		{
			widthFeasible = widthTarget;
			heightFeasible = heightTarget;
		}

		pptComSetlist.middleBothWidth = widthTarget;
		pptComSetlist.middleBothHeight = heightTarget;

		ret += sqrt((p.x - firX) * (p.x - firX) + (p.y - firY) * (p.y - firY));
		firX = static_cast<int>(p.x), firY = static_cast<int>(p.y);

		this_thread::sleep_for(chrono::milliseconds(5));
	}
	// 写入文件
	if (pptComSetlist.memoryWidgetPosition && (pptComSetlist.middleBothWidth != widthFirst || pptComSetlist.middleBothHeight != heightFirst))
		PptComWriteSetting();

	PptUiAllReplaceSignal = -1;
	return ret;
}
void PptBottomMiddleSeekBar(int firstX, int firstY)
{
	if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) return;
	PptUiAllReplaceSignal = 1;

	MonitorInfoStruct PPTMainMonitor;
	shared_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
	PPTMainMonitor = MainMonitor;
	DisplaysInfoLock.unlock();

	POINT p;
	GetCursorPos(&p);

	// 自身数值记录
	float widthFirst = pptComSetlist.bottomMiddleWidth;
	float heightFirst = pptComSetlist.bottomMiddleHeight;
	float widthTarget = pptComSetlist.bottomMiddleWidth;
	float heightTarget = pptComSetlist.bottomMiddleHeight;
	float widthFeasible = pptComSetlist.bottomMiddleWidth;
	float heightFeasible = pptComSetlist.bottomMiddleHeight;

	float WidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v - widthFirst;
	float WidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v + heightFirst;
	float WidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Width.v;
	float WidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height.v;

	float WidgetScale = min(min(pptComSetlist.bottomSideBothWidgetScale, pptComSetlist.middleSideBothWidgetScale), pptComSetlist.bottomSideMiddleWidgetScale);

	// 对象数值记录
	float BottomLeftWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v;
	float BottomLeftWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v;
	float BottomLeftWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Width.v;
	float BottomLeftWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v;
	float BottomRightWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v;
	float BottomRightWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v;
	float BottomRightWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Width.v;
	float BottomRightWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height.v;
	float MiddleLeftWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v;
	float MiddleLeftWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v;
	float MiddleLeftWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v;
	float MiddleLeftWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Height.v;
	float MiddleRightWidgetX = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v;
	float MiddleRightWidgetY = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v;
	float MiddleRightWidgetWidth = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v;
	float MiddleRightWidgetHeight = pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Height.v;

	for (;;)
	{
		if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;

		POINT p;
		GetCursorPos(&p);

		widthTarget = widthFirst + float(p.x - firstX);
		heightTarget = heightFirst - float(p.y - firstY);

		// 排斥/吸附边框
		{
			if (WidgetX + widthTarget <= 5 * WidgetScale) widthTarget = 5 * WidgetScale - WidgetX;
			if (WidgetY - heightTarget <= 5 * WidgetScale) heightTarget = WidgetY - 5 * WidgetScale;
			if (WidgetX + widthTarget + WidgetWidth >= PPTMainMonitor.MonitorWidth - 5 * WidgetScale) widthTarget = PPTMainMonitor.MonitorWidth - 5 * WidgetScale - WidgetX - WidgetWidth;
			if (WidgetY - heightTarget + WidgetHeight >= PPTMainMonitor.MonitorHeight - 5 * WidgetScale) heightTarget = WidgetY + WidgetHeight - PPTMainMonitor.MonitorHeight + 5 * WidgetScale;

			if (abs(widthTarget) <= 10 && abs(heightTarget) <= 10)
			{
				widthTarget = 0;
				heightTarget = 0;
			}
		}

		bool repelSignal = false;
		// 排斥对象
		{
			// 底部左侧控件
			if (pptComSetlist.showBottomBoth)
			{
				if (WidgetY - heightTarget <= BottomLeftWidgetY + BottomLeftWidgetHeight + 10 * WidgetScale && WidgetY - heightTarget + WidgetHeight >= BottomLeftWidgetY - 10 * WidgetScale
					&& WidgetX + widthTarget <= BottomLeftWidgetX + BottomLeftWidgetWidth + 10 * WidgetScale && WidgetX + widthTarget + WidgetWidth >= BottomLeftWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
			// 底部右侧控件
			if (pptComSetlist.showBottomBoth)
			{
				if (WidgetY - heightTarget <= BottomRightWidgetY + BottomRightWidgetHeight + 10 * WidgetScale && WidgetY - heightTarget + WidgetHeight >= BottomRightWidgetY - 10 * WidgetScale
					&& WidgetX + widthTarget <= BottomRightWidgetX + BottomRightWidgetWidth + 10 * WidgetScale && WidgetX + widthTarget + WidgetWidth >= BottomRightWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
			// 中部左侧控件
			if (pptComSetlist.showMiddleBoth)
			{
				if (WidgetY - heightTarget <= MiddleLeftWidgetY + MiddleLeftWidgetHeight + 10 * WidgetScale && WidgetY - heightTarget + WidgetHeight >= MiddleLeftWidgetY - 10 * WidgetScale
					&& WidgetX + widthTarget <= MiddleLeftWidgetX + MiddleLeftWidgetWidth + 10 * WidgetScale && WidgetX + widthTarget + WidgetWidth >= MiddleLeftWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
			// 中部右侧控件
			if (pptComSetlist.showMiddleBoth)
			{
				if (WidgetY - heightTarget <= MiddleRightWidgetY + MiddleRightWidgetHeight + 10 * WidgetScale && WidgetY - heightTarget + WidgetHeight >= MiddleRightWidgetY - 10 * WidgetScale
					&& WidgetX + widthTarget <= MiddleRightWidgetX + MiddleRightWidgetWidth + 10 * WidgetScale && WidgetX + widthTarget + WidgetWidth >= MiddleRightWidgetX - 10 * WidgetScale)
				{
					widthTarget = widthFeasible;
					heightTarget = heightFeasible;
					repelSignal = true;
				}
			}
		}

		// 更新可行值
		if (!repelSignal)
		{
			widthFeasible = widthTarget;
			heightFeasible = heightTarget;
		}

		pptComSetlist.bottomMiddleWidth = widthTarget;
		pptComSetlist.bottomMiddleHeight = heightTarget;

		this_thread::sleep_for(chrono::milliseconds(5));
	}
	// 写入文件
	if (pptComSetlist.memoryWidgetPosition && (pptComSetlist.bottomBothWidth != widthFirst || pptComSetlist.bottomBothHeight != heightFirst))
		PptComWriteSetting();

	PptUiAllReplaceSignal = -1;
	return;
}

void PptUI()
{
	threadStatus[L"PptUI"] = true;

	// 监视器信息初始化
	MonitorInfoStruct PPTMainMonitor;
	shared_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
	PPTMainMonitor = MainMonitor;
	DisplaysInfoLock.unlock();

	// 启用 UI 动画
	PptUiAnimationEnable = true;

	// UI 初始化
	{
		// 底部左侧控件
		{
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FillColor = PptUiWidgetColor(RGBA(225, 225, 225, 0), 25, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);
			}
			{
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].X1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Y1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].X2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Y2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Thickness = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Color = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
			}

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Transparency = PptUiWidgetValue(25, 1);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Img = pptIconBitmap[1];
			}
			{
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsColor = PptUiWidgetColor(RGBA(30, 30, 30, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsContent = L"";

				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsColor = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsContent = L"";
			}
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Transparency = PptUiWidgetValue(20, 1);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Img = pptIconBitmap[2];
			}
		}
		// 底部右侧控件
		{
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FillColor = PptUiWidgetColor(RGBA(225, 225, 225, 0), 25, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);
			}
			{
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].X1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Y1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].X2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Y2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Thickness = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Color = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
			}

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Transparency = PptUiWidgetValue(25, 1);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Img = pptIconBitmap[1];
			}
			{
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsColor = PptUiWidgetColor(RGBA(30, 30, 30, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsContent = L"";

				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsColor = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsContent = L"";
			}
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Transparency = PptUiWidgetValue(25, 1);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Img = pptIconBitmap[2];
			}
		}

		// 中部左侧控件
		{
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FillColor = PptUiWidgetColor(RGBA(225, 225, 225, 0), 25, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);
			}
			{
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].X1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Y1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].X2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Y2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Thickness = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Color = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
			}

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Transparency = PptUiWidgetValue(25, 1);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Img = pptIconBitmap[1];
			}
			{
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsColor = PptUiWidgetColor(RGBA(30, 30, 30, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsContent = L"";

				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsColor = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsContent = L"";
			}
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Transparency = PptUiWidgetValue(20, 1);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Img = pptIconBitmap[2];
			}
		}
		// 中部右侧控件
		{
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FillColor = PptUiWidgetColor(RGBA(225, 225, 225, 0), 25, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);
			}
			{
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].X1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Y1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].X2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Y2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Thickness = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Color = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
			}

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Transparency = PptUiWidgetValue(25, 1);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Img = pptIconBitmap[1];
			}
			{
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsColor = PptUiWidgetColor(RGBA(30, 30, 30, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsContent = L"";

				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Left = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Top = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Right = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Bottom = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsHeight = PptUiWidgetValue(15, 0.1f);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsColor = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
				pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsContent = L"";
			}
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Transparency = PptUiWidgetValue(20, 1);
				pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Img = pptIconBitmap[2];
			}
		}

		// 底部中间控件
		{
			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FillColor = PptUiWidgetColor(RGBA(225, 225, 225, 0), 25, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);
			}
			{
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].X1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Y1 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].X2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Y2 = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Thickness = PptUiWidgetValue(15, 0.1f);
				pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Color = PptUiWidgetColor(RGBA(60, 60, 60, 0), 25, 1);
			}

			{
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].X = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Y = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Width = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Height = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].EllipseWidth = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].EllipseHeight = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FrameThickness = PptUiWidgetValue(15, 0.1f);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor = PptUiWidgetColor(RGBA(250, 250, 250, 0), 10, 1);
				pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FrameColor = PptUiWidgetColor(RGBA(200, 200, 200, 0), 25, 1);

				pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].X = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Y = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Width = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Height = PptUiWidgetValue(15, 0.1f);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Transparency = PptUiWidgetValue(25, 1);
				pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Img = pptIconBitmap[3];
			}
		}

		memcpy(pptUiLineWidgetTarget, pptUiLineWidget, sizeof(pptUiLineWidget));
		memcpy(pptUiRoundRectWidgetTarget, pptUiRoundRectWidget, sizeof(pptUiRoundRectWidget));
		memcpy(pptUiImageWidgetTarget, pptUiImageWidget, sizeof(pptUiImageWidget));
		memcpy(pptUiWordsWidgetTarget, pptUiWordsWidget, sizeof(pptUiWordsWidget));
	}

	PptUiWidgetStateEnum pptUiWidgetThreadStateLast = PptUiWidgetStateEnum::Unknow;
	int tPptUiChangeSignal;
	PptUiChangeSignal = true;

	clock_t tRecord = clock();
	for (; !offSignal;)
	{
		// 监视器信息监测
		shared_lock<shared_mutex> DisplaysInfoLock(DisplaysInfoSm);
		PPTMainMonitor = MainMonitor;
		DisplaysInfoLock.unlock();

		// Ppt 信息监测
		PptUiWidgetStateEnum pptUiWidgetThreadState = pptUiWidgetState;
		int pptTotalSlides = PptInfoState.TotalPage;
		int pptCurrentSlides = PptInfoState.CurrentPage;

		// UI 计算
		{
			// UI 单次修改计算（例如按钮）
			if (pptUiWidgetThreadState != pptUiWidgetThreadStateLast)
			{
				pptUiWidgetThreadStateLast = pptUiWidgetThreadState;
				if (pptUiWidgetThreadState != PptUiWidgetStateEnum::Close)
				{
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 160);

					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FrameColor.v = RGBA(200, 200, 200, 160);

					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 160);
				}
				else
				{
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 0);

					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FrameColor.v = RGBA(200, 200, 200, 0);

					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FrameColor.v = RGBA(200, 200, 200, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 0);
					pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FrameColor.v = RGBA(200, 200, 200, 0);
				}
			}

			// UI 控件实时计算
			{
				// 底部左侧控件
				{
					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v = floor(pptComSetlist.bottomBothWidth + (5) * pptComSetlist.bottomSideBothWidgetScale);
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v = floor(PPTMainMonitor.MonitorHeight + (5) * pptComSetlist.bottomSideBothWidgetScale);
						else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v = floor(PPTMainMonitor.MonitorHeight - pptComSetlist.bottomBothHeight + (-65) * pptComSetlist.bottomSideBothWidgetScale);
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Width.v = (195) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v = (60) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].EllipseWidth.v = (30) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].EllipseHeight.v = (30) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FrameThickness.v = (1) * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close)
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FillColor.v, 0);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FrameColor.v, 0);
						}
						else
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FillColor.v, 160);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FrameColor.v, 160);
						}
					}
					{
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].X1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v + (5 + 3) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Y1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + (15) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].X2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v + (5 + 3) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Y2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v + (-15) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Thickness.v = 2 * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Color.v, 0);
						else SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Color.v, 250);
					}

					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].X.v = pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].X1.v + (2 + 5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Width.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Height.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].EllipseWidth.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].EllipseHeight.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FrameThickness.v = (1) * pptComSetlist.bottomSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].X.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Width.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Height.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Transparency.v = 255;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Img = pptIconBitmap[1];
						}
					}
					{
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Width.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Left.v + (65) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Top.v + (5 + 30) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsHeight.v = (24) * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsContent = pptCurrentSlides < 0 ? L"-" : to_wstring(min(9999, pptCurrentSlides));

						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Width.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + (5 + 25) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Left.v + (65) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Top.v + (55) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsHeight.v = (16) * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsContent = L"/" + ((pptTotalSlides < 0) ? L"-" : to_wstring(min(9999, pptTotalSlides)));
					}
					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].X.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Right.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Width.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Height.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].EllipseWidth.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].EllipseHeight.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FrameThickness.v = (1) * pptComSetlist.bottomSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].X.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].Width.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].Height.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].Transparency.v = 255;
							if (pptCurrentSlides == -1) pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].Img = pptIconBitmap[3];
							else pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_LeftNextPage].Img = pptIconBitmap[2];
						}
					}
				}
				// 底部右侧控件
				{
					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v = floor(PPTMainMonitor.MonitorWidth - pptComSetlist.bottomBothWidth - (200) * pptComSetlist.bottomSideBothWidgetScale);
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v = floor(PPTMainMonitor.MonitorHeight + (5) * pptComSetlist.bottomSideBothWidgetScale);
						else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v = floor(PPTMainMonitor.MonitorHeight - pptComSetlist.bottomBothHeight + (-65) * pptComSetlist.bottomSideBothWidgetScale);
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Width.v = (195) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height.v = (60) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].EllipseWidth.v = (30) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].EllipseHeight.v = (30) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FrameThickness.v = (1) * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close)
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FillColor.v, 0);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FrameColor.v, 0);
						}
						else
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FillColor.v, 160);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FrameColor.v, 160);
						}
					}

					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Width.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Height.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].EllipseWidth.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].EllipseHeight.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FrameThickness.v = (1) * pptComSetlist.bottomSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightPreviousPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].X.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Width.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Height.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Transparency.v = 255;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Img = pptIconBitmap[1];
						}
					}
					{
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Width.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Left.v + (65) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Top.v + (5 + 30) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsHeight.v = (24) * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsContent = pptCurrentSlides < 0 ? L"-" : to_wstring(min(9999, pptCurrentSlides));

						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Width.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + (5 + 25) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Left.v + (65) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Top.v + (55) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsHeight.v = (16) * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsContent = L"/" + ((pptTotalSlides < 0) ? L"-" : to_wstring(min(9999, pptTotalSlides)));
					}
					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].X.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Right.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Width.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Height.v = (50) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].EllipseWidth.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].EllipseHeight.v = (35) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FrameThickness.v = (1) * pptComSetlist.bottomSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].X.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Y.v + (5) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].Width.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].Height.v = (40) * pptComSetlist.bottomSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].Transparency.v = 255;
							if (pptCurrentSlides == -1) pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].Img = pptIconBitmap[3];
							else pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_RightNextPage].Img = pptIconBitmap[2];
						}
					}

					{
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].X1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Width.v + (5 + 2) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Y1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + (15) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].X2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Width.v + (5 + 2) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Y2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v + (-15) * pptComSetlist.bottomSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Thickness.v = 2 * pptComSetlist.bottomSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Color.v, 0);
						else SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Color.v, 250);
					}
				}

				// 中部左侧控件
				{
					{
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v = floor((-65) * pptComSetlist.middleSideBothWidgetScale);
						else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v = floor(pptComSetlist.middleBothWidth + (5) * pptComSetlist.middleSideBothWidgetScale);
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v = floor(PPTMainMonitor.MonitorHeight / 2.0f - pptComSetlist.middleBothHeight - (185 / 2.0f) * pptComSetlist.middleSideBothWidgetScale);
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v = (60) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Height.v = (185) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].EllipseWidth.v = (30) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].EllipseHeight.v = (30) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FrameThickness.v = (1) * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close)
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FillColor.v, 0);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FrameColor.v, 0);
						}
						else
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FillColor.v, 160);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FrameColor.v, 160);
						}
					}
					{
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].X1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + (15) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Y1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v + (5 + 3) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].X2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v + (-15) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Y2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v + (5 + 3) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Thickness.v = 2 * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Color.v, 0);
						else SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Color.v, 250);
					}

					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y.v = pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Y1.v + (2 + 5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Width.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Height.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].EllipseWidth.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].EllipseHeight.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FrameThickness.v = (1) * pptComSetlist.middleSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Width.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Height.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Transparency.v = 255;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Img = pptIconBitmap[4];
						}
					}
					{
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Height.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Left.v + (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Top.v + (5 + 35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsHeight.v = (24) * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsContent = pptCurrentSlides < 0 ? L"-" : to_wstring(min(999, pptCurrentSlides));

						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Height.v + (5 + 30) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Left.v + (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Top.v + (25) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsHeight.v = (16) * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsContent = L"/" + ((pptTotalSlides < 0) ? L"-" : to_wstring(min(999, pptTotalSlides)));
					}
					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Y.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Bottom.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Width.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Height.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].EllipseWidth.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].EllipseHeight.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FrameThickness.v = (1) * pptComSetlist.middleSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Y.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Width.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Height.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Transparency.v = 255;
							if (pptCurrentSlides == -1) pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Img = pptIconBitmap[3];
							else pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Img = pptIconBitmap[5];
						}
					}
				}
				// 中部右侧控件
				{
					{
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v = floor(PPTMainMonitor.MonitorWidth + (5) * pptComSetlist.middleSideBothWidgetScale);
						else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v = floor(PPTMainMonitor.MonitorWidth - pptComSetlist.middleBothWidth + (-65) * pptComSetlist.middleSideBothWidgetScale);
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v = floor(PPTMainMonitor.MonitorHeight / 2.0f - pptComSetlist.middleBothHeight - (185 / 2.0f) * pptComSetlist.middleSideBothWidgetScale);
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v = (60) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Height.v = (185) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].EllipseWidth.v = (30) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].EllipseHeight.v = (30) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FrameThickness.v = (1) * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close)
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FillColor.v, 0);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FrameColor.v, 0);
						}
						else
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FillColor.v, 160);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FrameColor.v, 160);
						}
					}
					{
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].X1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + (15) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Y1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v + (5 + 3) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].X2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v + (-15) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Y2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v + (5 + 3) * pptComSetlist.middleSideBothWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Thickness.v = 2 * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Color.v, 0);
						else SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Color.v, 250);
					}

					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y.v = pptUiLineWidgetTarget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Y1.v + (2 + 5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Width.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Height.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].EllipseWidth.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].EllipseHeight.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FrameThickness.v = (1) * pptComSetlist.middleSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Width.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Height.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Transparency.v = 255;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Img = pptIconBitmap[4];
						}
					}
					{
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Height.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Left.v + (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Top.v + (5 + 35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsHeight.v = (24) * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsContent = pptCurrentSlides < 0 ? L"-" : to_wstring(min(999, pptCurrentSlides));

						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Left.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Top.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Height.v + (5 + 30) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Right.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Left.v + (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Bottom.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Top.v + (25) * pptComSetlist.middleSideBothWidgetScale;
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsHeight.v = (16) * pptComSetlist.middleSideBothWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsColor.v, 0);
						else SetAlpha(pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsColor.v, 255);
						pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsContent = L"/" + ((pptTotalSlides < 0) ? L"-" : to_wstring(min(999, pptTotalSlides)));
					}
					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Y.v = pptUiWordsWidgetTarget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Bottom.v + (5) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Width.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Height.v = (50) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].EllipseWidth.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].EllipseHeight.v = (35) * pptComSetlist.middleSideBothWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FrameThickness.v = (1) * pptComSetlist.middleSideBothWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].X.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Y.v + (5) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].Width.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].Height.v = (40) * pptComSetlist.middleSideBothWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].Transparency.v = 255;
							if (pptCurrentSlides == -1) pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].Img = pptIconBitmap[3];
							else pptUiImageWidgetTarget[PptUiImageWidgetID::MiddleSide_RightNextPage].Img = pptIconBitmap[5];
						}
					}
				}

				// 底部中间控件
				{
					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v = floor(PPTMainMonitor.MonitorWidth / 2 + pptComSetlist.bottomMiddleWidth + (-70 / 2.0f) * pptComSetlist.bottomSideMiddleWidgetScale);
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v = floor(PPTMainMonitor.MonitorHeight + (5) * pptComSetlist.bottomSideMiddleWidgetScale);
						else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v = floor(PPTMainMonitor.MonitorHeight - pptComSetlist.bottomMiddleHeight + (-65) * pptComSetlist.bottomSideMiddleWidgetScale);
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Width.v = (70) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height.v = (60) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].EllipseWidth.v = (30) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].EllipseHeight.v = (30) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FrameThickness.v = (1) * pptComSetlist.bottomSideMiddleWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close)
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FillColor.v, 0);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FrameColor.v, 0);
						}
						else
						{
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FillColor.v, 160);
							SetAlpha(pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FrameColor.v, 160);
						}
					}
					{
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].X1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v + (5 + 3) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Y1.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v + (15) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].X2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v + (5 + 3) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Y2.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v + pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height.v + (-15) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Thickness.v = 2 * pptComSetlist.bottomSideMiddleWidgetScale;
						if (pptUiWidgetState == PptUiWidgetStateEnum::Close) SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Color.v, 0);
						else SetAlpha(pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Color.v, 250);
					}

					{
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].X.v = pptUiLineWidgetTarget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].X1.v + (2 + 5) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v + (5) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Width.v = (50) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Height.v = (50) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].EllipseWidth.v = (35) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].EllipseHeight.v = (35) * pptComSetlist.bottomSideMiddleWidgetScale;
						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FrameThickness.v = (1) * pptComSetlist.bottomSideMiddleWidgetScale;

						{
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_MiddleEndShow].X.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].X.v + (5) * pptComSetlist.bottomSideMiddleWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Y.v = pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Y.v + (5) * pptComSetlist.bottomSideMiddleWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Width.v = (40) * pptComSetlist.bottomSideMiddleWidgetScale;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Height.v = (40) * pptComSetlist.bottomSideMiddleWidgetScale;
							if (pptUiWidgetState == PptUiWidgetStateEnum::Close) pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Transparency.v = 0;
							else pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Transparency.v = 255;
							pptUiImageWidgetTarget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Img = pptIconBitmap[3];
						}
					}
				}
			}

			// UI 变化调整
			{
				tPptUiChangeSignal = false;

				// 直线
				for (int i = 0; i < size(pptUiLineWidgetTarget); i++)
				{
					if (pptUiLineWidget[i].X1.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiLineWidget[i].X1.replace = false;
						pptUiLineWidget[i].X1.v = pptUiLineWidgetTarget[i].X1.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiLineWidget[i].X1.v != pptUiLineWidgetTarget[i].X1.v)
					{
						PptUiWidgetValueTransformation(&pptUiLineWidget[i].X1.v, pptUiLineWidgetTarget[i].X1.v, pptUiLineWidgetTarget[i].X1.s, pptUiLineWidgetTarget[i].X1.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiLineWidget[i].Y1.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiLineWidget[i].Y1.replace = false;
						pptUiLineWidget[i].Y1.v = pptUiLineWidgetTarget[i].Y1.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiLineWidget[i].Y1.v != pptUiLineWidgetTarget[i].Y1.v)
					{
						PptUiWidgetValueTransformation(&pptUiLineWidget[i].Y1.v, pptUiLineWidgetTarget[i].Y1.v, pptUiLineWidgetTarget[i].Y1.s, pptUiLineWidgetTarget[i].Y1.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiLineWidget[i].X2.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiLineWidget[i].X2.replace = false;
						pptUiLineWidget[i].X2.v = pptUiLineWidgetTarget[i].X2.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiLineWidget[i].X2.v != pptUiLineWidgetTarget[i].X2.v)
					{
						PptUiWidgetValueTransformation(&pptUiLineWidget[i].X2.v, pptUiLineWidgetTarget[i].X2.v, pptUiLineWidgetTarget[i].X2.s, pptUiLineWidgetTarget[i].X2.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiLineWidget[i].Y2.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiLineWidget[i].Y2.replace = false;
						pptUiLineWidget[i].Y2.v = pptUiLineWidgetTarget[i].Y2.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiLineWidget[i].Y2.v != pptUiLineWidgetTarget[i].Y2.v)
					{
						PptUiWidgetValueTransformation(&pptUiLineWidget[i].Y2.v, pptUiLineWidgetTarget[i].Y2.v, pptUiLineWidgetTarget[i].Y2.s, pptUiLineWidgetTarget[i].Y2.e);
						tPptUiChangeSignal = true;
					}

					//=====

					if (pptUiLineWidget[i].Thickness.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiLineWidget[i].Thickness.replace = false;
						pptUiLineWidget[i].Thickness.v = pptUiLineWidgetTarget[i].Thickness.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiLineWidget[i].Thickness.v != pptUiLineWidgetTarget[i].Thickness.v)
					{
						PptUiWidgetValueTransformation(&pptUiLineWidget[i].Thickness.v, pptUiLineWidgetTarget[i].Thickness.v, pptUiLineWidgetTarget[i].Thickness.s, pptUiLineWidgetTarget[i].Thickness.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiLineWidget[i].Color.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiLineWidget[i].Color.replace = false;
						pptUiLineWidget[i].Color.v = pptUiLineWidgetTarget[i].Color.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiLineWidget[i].Color.v != pptUiLineWidgetTarget[i].Color.v)
					{
						PptUiWidgetColorTransformation(&pptUiLineWidget[i].Color.v, pptUiLineWidgetTarget[i].Color.v, pptUiLineWidgetTarget[i].Color.s, pptUiLineWidgetTarget[i].Color.e);
						tPptUiChangeSignal = true;
					}
				}
				// 矩形
				for (int i = 0; i < size(pptUiRoundRectWidgetTarget); i++)
				{
					if (pptUiRoundRectWidget[i].X.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].X.replace = false;
						pptUiRoundRectWidget[i].X.v = pptUiRoundRectWidgetTarget[i].X.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].X.v != pptUiRoundRectWidgetTarget[i].X.v)
					{
						PptUiWidgetValueTransformation(&pptUiRoundRectWidget[i].X.v, pptUiRoundRectWidgetTarget[i].X.v, pptUiRoundRectWidget[i].X.s, pptUiRoundRectWidget[i].X.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiRoundRectWidget[i].Y.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].Y.replace = false;
						pptUiRoundRectWidget[i].Y.v = pptUiRoundRectWidgetTarget[i].Y.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].Y.v != pptUiRoundRectWidgetTarget[i].Y.v)
					{
						PptUiWidgetValueTransformation(&pptUiRoundRectWidget[i].Y.v, pptUiRoundRectWidgetTarget[i].Y.v, pptUiRoundRectWidget[i].Y.s, pptUiRoundRectWidget[i].Y.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiRoundRectWidget[i].Width.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].Width.replace = false;
						pptUiRoundRectWidget[i].Width.v = pptUiRoundRectWidgetTarget[i].Width.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].Width.v != pptUiRoundRectWidgetTarget[i].Width.v)
					{
						PptUiWidgetValueTransformation(&pptUiRoundRectWidget[i].Width.v, pptUiRoundRectWidgetTarget[i].Width.v, pptUiRoundRectWidget[i].Width.s, pptUiRoundRectWidget[i].Width.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiRoundRectWidget[i].Height.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].Height.replace = false;
						pptUiRoundRectWidget[i].Height.v = pptUiRoundRectWidgetTarget[i].Height.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].Height.v != pptUiRoundRectWidgetTarget[i].Height.v)
					{
						PptUiWidgetValueTransformation(&pptUiRoundRectWidget[i].Height.v, pptUiRoundRectWidgetTarget[i].Height.v, pptUiRoundRectWidget[i].Height.s, pptUiRoundRectWidget[i].Height.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiRoundRectWidget[i].EllipseWidth.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].EllipseWidth.replace = false;
						pptUiRoundRectWidget[i].EllipseWidth.v = pptUiRoundRectWidgetTarget[i].EllipseWidth.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].EllipseWidth.v != pptUiRoundRectWidgetTarget[i].EllipseWidth.v)
					{
						PptUiWidgetValueTransformation(&pptUiRoundRectWidget[i].EllipseWidth.v, pptUiRoundRectWidgetTarget[i].EllipseWidth.v, pptUiRoundRectWidget[i].EllipseWidth.s, pptUiRoundRectWidget[i].EllipseWidth.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiRoundRectWidget[i].EllipseHeight.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].EllipseHeight.replace = false;
						pptUiRoundRectWidget[i].EllipseHeight.v = pptUiRoundRectWidgetTarget[i].EllipseHeight.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].EllipseHeight.v != pptUiRoundRectWidgetTarget[i].EllipseHeight.v)
					{
						PptUiWidgetValueTransformation(&pptUiRoundRectWidget[i].EllipseHeight.v, pptUiRoundRectWidgetTarget[i].EllipseHeight.v, pptUiRoundRectWidget[i].EllipseHeight.s, pptUiRoundRectWidget[i].EllipseHeight.e);
						tPptUiChangeSignal = true;
					}

					//=====

					if (pptUiRoundRectWidget[i].FrameThickness.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].FrameThickness.replace = false;
						pptUiRoundRectWidget[i].FrameThickness.v = pptUiRoundRectWidgetTarget[i].FrameThickness.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].FrameThickness.v != pptUiRoundRectWidgetTarget[i].FrameThickness.v)
					{
						PptUiWidgetValueTransformation(&pptUiRoundRectWidget[i].FrameThickness.v, pptUiRoundRectWidgetTarget[i].FrameThickness.v, pptUiRoundRectWidget[i].FrameThickness.s, pptUiRoundRectWidget[i].FrameThickness.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiRoundRectWidget[i].FrameColor.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].FrameColor.replace = false;
						pptUiRoundRectWidget[i].FrameColor.v = pptUiRoundRectWidgetTarget[i].FrameColor.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].FrameColor.v != pptUiRoundRectWidgetTarget[i].FrameColor.v)
					{
						PptUiWidgetColorTransformation(&pptUiRoundRectWidget[i].FrameColor.v, pptUiRoundRectWidgetTarget[i].FrameColor.v, pptUiRoundRectWidget[i].FrameColor.s, pptUiRoundRectWidget[i].FrameColor.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiRoundRectWidget[i].FillColor.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiRoundRectWidget[i].FillColor.replace = false;
						pptUiRoundRectWidget[i].FillColor.v = pptUiRoundRectWidgetTarget[i].FillColor.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiRoundRectWidget[i].FillColor.v != pptUiRoundRectWidgetTarget[i].FillColor.v)
					{
						PptUiWidgetColorTransformation(&pptUiRoundRectWidget[i].FillColor.v, pptUiRoundRectWidgetTarget[i].FillColor.v, pptUiRoundRectWidget[i].FillColor.s, pptUiRoundRectWidget[i].FillColor.e);
						tPptUiChangeSignal = true;
					}
				}
				// 图像
				for (int i = 0; i < size(pptUiImageWidgetTarget); i++)
				{
					if (pptUiImageWidget[i].X.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiImageWidget[i].X.replace = false;
						pptUiImageWidget[i].X.v = pptUiImageWidgetTarget[i].X.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiImageWidget[i].X.v != pptUiImageWidgetTarget[i].X.v)
					{
						PptUiWidgetValueTransformation(&pptUiImageWidget[i].X.v, pptUiImageWidgetTarget[i].X.v, pptUiImageWidget[i].X.s, pptUiImageWidget[i].X.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiImageWidget[i].Y.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiImageWidget[i].Y.replace = false;
						pptUiImageWidget[i].Y.v = pptUiImageWidgetTarget[i].Y.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiImageWidget[i].Y.v != pptUiImageWidgetTarget[i].Y.v)
					{
						PptUiWidgetValueTransformation(&pptUiImageWidget[i].Y.v, pptUiImageWidgetTarget[i].Y.v, pptUiImageWidget[i].Y.s, pptUiImageWidget[i].Y.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiImageWidget[i].Width.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiImageWidget[i].Width.replace = false;
						pptUiImageWidget[i].Width.v = pptUiImageWidgetTarget[i].Width.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiImageWidget[i].Width.v != pptUiImageWidgetTarget[i].Width.v)
					{
						PptUiWidgetValueTransformation(&pptUiImageWidget[i].Width.v, pptUiImageWidgetTarget[i].Width.v, pptUiImageWidget[i].Width.s, pptUiImageWidget[i].Width.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiImageWidget[i].Height.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiImageWidget[i].Height.replace = false;
						pptUiImageWidget[i].Height.v = pptUiImageWidgetTarget[i].Height.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiImageWidget[i].Height.v != pptUiImageWidgetTarget[i].Height.v)
					{
						PptUiWidgetValueTransformation(&pptUiImageWidget[i].Height.v, pptUiImageWidgetTarget[i].Height.v, pptUiImageWidget[i].Height.s, pptUiImageWidget[i].Height.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiImageWidget[i].Transparency.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiImageWidget[i].Transparency.replace = false;
						pptUiImageWidget[i].Transparency.v = pptUiImageWidgetTarget[i].Transparency.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiImageWidget[i].Transparency.v != pptUiImageWidgetTarget[i].Transparency.v)
					{
						PptUiWidgetValueTransformation(&pptUiImageWidget[i].Transparency.v, pptUiImageWidgetTarget[i].Transparency.v, pptUiImageWidget[i].Transparency.s, pptUiImageWidget[i].Transparency.e);
						tPptUiChangeSignal = true;
					}

					//=====

					if (pptUiImageWidget[i].Img != pptUiImageWidgetTarget[i].Img)
					{
						pptUiImageWidget[i].Img = pptUiImageWidgetTarget[i].Img;
						tPptUiChangeSignal = true;
					}
				}
				// 文字
				for (int i = 0; i < size(pptUiWordsWidgetTarget); i++)
				{
					if (pptUiWordsWidget[i].Left.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiWordsWidget[i].Left.replace = false;
						pptUiWordsWidget[i].Left.v = pptUiWordsWidgetTarget[i].Left.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiWordsWidget[i].Left.v != pptUiWordsWidgetTarget[i].Left.v)
					{
						PptUiWidgetValueTransformation(&pptUiWordsWidget[i].Left.v, pptUiWordsWidgetTarget[i].Left.v, pptUiWordsWidget[i].Left.s, pptUiWordsWidget[i].Left.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiWordsWidget[i].Top.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiWordsWidget[i].Top.replace = false;
						pptUiWordsWidget[i].Top.v = pptUiWordsWidgetTarget[i].Top.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiWordsWidget[i].Top.v != pptUiWordsWidgetTarget[i].Top.v)
					{
						PptUiWidgetValueTransformation(&pptUiWordsWidget[i].Top.v, pptUiWordsWidgetTarget[i].Top.v, pptUiWordsWidget[i].Top.s, pptUiWordsWidget[i].Top.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiWordsWidget[i].Right.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiWordsWidget[i].Right.replace = false;
						pptUiWordsWidget[i].Right.v = pptUiWordsWidgetTarget[i].Right.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiWordsWidget[i].Right.v != pptUiWordsWidgetTarget[i].Right.v)
					{
						PptUiWidgetValueTransformation(&pptUiWordsWidget[i].Right.v, pptUiWordsWidgetTarget[i].Right.v, pptUiWordsWidget[i].Right.s, pptUiWordsWidget[i].Right.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiWordsWidget[i].Bottom.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiWordsWidget[i].Bottom.replace = false;
						pptUiWordsWidget[i].Bottom.v = pptUiWordsWidgetTarget[i].Bottom.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiWordsWidget[i].Bottom.v != pptUiWordsWidgetTarget[i].Bottom.v)
					{
						PptUiWidgetValueTransformation(&pptUiWordsWidget[i].Bottom.v, pptUiWordsWidgetTarget[i].Bottom.v, pptUiWordsWidget[i].Bottom.s, pptUiWordsWidget[i].Bottom.e);
						tPptUiChangeSignal = true;
					}

					//=====

					if (pptUiWordsWidget[i].WordsHeight.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiWordsWidget[i].WordsHeight.replace = false;
						pptUiWordsWidget[i].WordsHeight.v = pptUiWordsWidgetTarget[i].WordsHeight.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiWordsWidget[i].WordsHeight.v != pptUiWordsWidgetTarget[i].WordsHeight.v)
					{
						PptUiWidgetValueTransformation(&pptUiWordsWidget[i].WordsHeight.v, pptUiWordsWidgetTarget[i].WordsHeight.v, pptUiWordsWidget[i].WordsHeight.s, pptUiWordsWidget[i].WordsHeight.e);
						tPptUiChangeSignal = true;
					}

					if (pptUiWordsWidget[i].WordsColor.replace || PptUiAllReplaceSignal || !PptUiAnimationEnable)
					{
						pptUiWordsWidget[i].WordsColor.replace = false;
						pptUiWordsWidget[i].WordsColor.v = pptUiWordsWidgetTarget[i].WordsColor.v;
						tPptUiChangeSignal = true;
					}
					else if (pptUiWordsWidget[i].WordsColor.v != pptUiWordsWidgetTarget[i].WordsColor.v)
					{
						PptUiWidgetColorTransformation(&pptUiWordsWidget[i].WordsColor.v, pptUiWordsWidgetTarget[i].WordsColor.v, pptUiWordsWidget[i].WordsColor.s, pptUiWordsWidget[i].WordsColor.e);
						tPptUiChangeSignal = true;
					}

					//=====

					if (pptUiWordsWidget[i].WordsContent != pptUiWordsWidgetTarget[i].WordsContent)
					{
						pptUiWordsWidget[i].WordsContent = pptUiWordsWidgetTarget[i].WordsContent;
						tPptUiChangeSignal = true;
					}
				}

				if (tPptUiChangeSignal) PptUiChangeSignal = true;
				if (PptUiAllReplaceSignal == -1) PptUiAllReplaceSignal = 0;
			}
		}

		// 动态平衡帧率
		if (tRecord)
		{
			int delay = 1000 / 60 - (clock() - tRecord);
			if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
		tRecord = clock();
	}

	threadStatus[L"PptUI"] = false;
}
void PptInfo()
{
	threadStatus[L"PptInfo"] = true;

	bool Initialization = false; // 控件初始化完毕
	for (; !offSignal;)
	{
		// Ppt 信息监测 | 控件信息加载
		if (!Initialization && PptInfoState.TotalPage != -1)
		{
			pptUiWidgetState = PptUiWidgetStateEnum::Expand;

			ppt_show = GetPptShow();

			std::wstringstream ss(GetPptTitle());
			getline(ss, ppt_title);
			getline(ss, ppt_software);

			if (ppt_software.find(L"WPS") != ppt_software.npos) ppt_software = L"WPS";
			else ppt_software = L"PowerPoint";

			if (!ppt_title_recond[ppt_title] && pptComSetlist.showLoadingScreen) FreezePPT = true;
			Initialization = true;
		}
		else if (Initialization && PptInfoState.TotalPage == -1)
		{
			pptUiWidgetState = PptUiWidgetStateEnum::Close;

			PptImg.IsSave = false;
			PptImg.IsSaved.clear();
			PptImg.Image.clear();

			ppt_show = NULL, ppt_software = L"";

			// 设置控件归位
			PptComReadSettingPositionOnly();

			FreezePPT = false;
			Initialization = false;
		}

		this_thread::sleep_for(chrono::milliseconds(500));
	}

	threadStatus[L"PptInfo"] = false;
}
void PptDraw()
{
	threadStatus[L"PptDraw"] = true;

	//ppt窗口初始化
	MonitorInfoStruct PPTMainMonitor;
	{
		DisableResizing(ppt_window, true);//禁止窗口拉伸
		SetWindowLong(ppt_window, GWL_STYLE, GetWindowLong(ppt_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
		SetWindowPos(ppt_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME | SWP_NOACTIVATE);
		SetWindowLong(ppt_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

		shared_lock<shared_mutex> DisplaysInfoLock2(DisplaysInfoSm);
		PPTMainMonitor = MainMonitor;
		DisplaysInfoLock2.unlock();

		if (setlist.regularSetting.avoidFullScreen)
		{
			PptWindowBackground.Resize(PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight - 1);
			SetWindowPos(ppt_window, NULL, PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight - 1, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		else
		{
			PptWindowBackground.Resize(PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight);
			SetWindowPos(ppt_window, NULL, PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	// 设置窗口自定义消息回调
	hiex::SetWndProcFunc(ppt_window, PptWindowMsgCallback);

	// 创建 EasyX 兼容的 DC Render Target
	ID2D1DCRenderTarget* DCRenderTarget = nullptr;
	D2DFactory->CreateDCRenderTarget(&D2DProperty, &DCRenderTarget);

	// 绑定 EasyX DC
	RECT PptBackgroundWindowRect = { 0, 0, PptWindowBackground.getwidth(), PptWindowBackground.getheight() };
	DCRenderTarget->BindDC(GetImageHDC(&PptWindowBackground), &PptBackgroundWindowRect);

	// 设置抗锯齿
	DCRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE::D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	//媒体初始化
	{
		idtLoadImage(&PptIcon[1], L"PNG", L"ppt1");
		idtLoadImage(&PptIcon[2], L"PNG", L"ppt2");
		idtLoadImage(&PptIcon[3], L"PNG", L"ppt3");
		idtLoadImage(&PptIcon[4], L"PNG", L"ppt4");
		idtLoadImage(&PptIcon[5], L"PNG", L"ppt5");

		ChangeColor(PptIcon[1], RGB(50, 50, 50));
		ChangeColor(PptIcon[2], RGB(50, 50, 50));
		ChangeColor(PptIcon[3], RGB(50, 50, 50));
		ChangeColor(PptIcon[4], RGB(50, 50, 50));
		ChangeColor(PptIcon[5], RGB(50, 50, 50));

		{
			int width = PptIcon[1].getwidth();
			int height = PptIcon[1].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[1]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &pptIconBitmap[1]);
			delete[] data;
		}
		{
			int width = PptIcon[2].getwidth();
			int height = PptIcon[2].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[2]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &pptIconBitmap[2]);
			delete[] data;
		}
		{
			int width = PptIcon[3].getwidth();
			int height = PptIcon[3].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[3]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &pptIconBitmap[3]);
			delete[] data;
		}
		{
			int width = PptIcon[4].getwidth();
			int height = PptIcon[4].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[4]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &pptIconBitmap[4]);
			delete[] data;
		}
		{
			int width = PptIcon[5].getwidth();
			int height = PptIcon[5].getheight();
			DWORD* pMem = GetImageBuffer(&PptIcon[5]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			DCRenderTarget->CreateBitmap(D2D1::SizeU(width, height), data, width * 4, bitmapProps, &pptIconBitmap[5]);
			delete[] data;
		}
	}

	thread(PptUI).detach();

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	{
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
		blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	}

	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { PptWindowBackground.getwidth(),PptWindowBackground.getheight() };
	POINT ptDst = { PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top };

	// 调用UpdateLayeredWindow函数更新窗口
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	{
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = GetDC(NULL);
		ulwi.hdcSrc = GetImageHDC(&PptWindowBackground);
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;
	}

	while (!(GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_LAYERED))
	{
		SetWindowLong(ppt_window, GWL_EXSTYLE, GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_LAYERED);
		if (GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_LAYERED) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}
	while (!(GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
	{
		SetWindowLong(ppt_window, GWL_EXSTYLE, GetWindowLong(ppt_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
		if (GetWindowLong(ppt_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

		this_thread::sleep_for(chrono::milliseconds(10));
	}

	clock_t tRecord = clock();
	for (bool IsShowWindow = false; !offSignal;)
	{
		// 监视器信息监测
		{
			shared_lock<shared_mutex> DisplaysInfoLock2(DisplaysInfoSm);
			bool MainMonitorDifferent = (PPTMainMonitor != MainMonitor);
			if (MainMonitorDifferent) PPTMainMonitor = MainMonitor;
			DisplaysInfoLock2.unlock();

			if (MainMonitorDifferent)
			{
				if (setlist.regularSetting.avoidFullScreen)
				{
					PptWindowBackground.Resize(PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight - 1);
					SetWindowPos(ppt_window, NULL, PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight - 1, SWP_NOZORDER | SWP_NOACTIVATE);
				}
				else
				{
					PptWindowBackground.Resize(PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight);
					SetWindowPos(ppt_window, NULL, PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight, SWP_NOZORDER | SWP_NOACTIVATE);
				}
				ulwi.hdcSrc = GetImageHDC(&PptWindowBackground);

				sizeWnd = { PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight };
				POINT ptDst = { PPTMainMonitor.rcMonitor.left, PPTMainMonitor.rcMonitor.top };

				RECT PptBackgroundWindowRect = { 0, 0, PPTMainMonitor.MonitorWidth, PPTMainMonitor.MonitorHeight };
				DCRenderTarget->BindDC(GetImageHDC(&PptWindowBackground), &PptBackgroundWindowRect);

				PptUiAllReplaceSignal = -1;
			}
		}

		// 绘制部分
		if (PptUiChangeSignal)
		{
			PptUiChangeSignal = false;
			SetImageColor(PptWindowBackground, RGBA(0, 0, 0, 0), true);

			DCRenderTarget->BeginDraw();

			// 底部左侧控件
			if (pptComSetlist.showBottomBoth)
			{
				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar]
				{
					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Color.v), &pBrush);

					ID2D1StrokeStyle* pStrokeStyle = NULL;
					D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties();
					strokeStyleProperties.startCap = D2D1_CAP_STYLE_ROUND;
					strokeStyleProperties.endCap = D2D1_CAP_STYLE_ROUND;
					D2DFactory->CreateStrokeStyle(&strokeStyleProperties, NULL, 0, &pStrokeStyle);

					DCRenderTarget->DrawLine(
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].X1.v, pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Y1.v),
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].X2.v, pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Y2.v),
						pBrush,
						pptUiLineWidget[PptUiLineWidgetID::BottomSide_LeftPageWidget_SeekBar].Thickness.v,
						pStrokeStyle
					);

					DxObjectSafeRelease(&pBrush);
					DxObjectSafeRelease(&pStrokeStyle);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage]
				if (pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Img, D2D1::RectF(pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].X.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Y.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].X.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Width.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Height.v), pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftPreviousPage].Transparency.v / 255.0f);
				}

				// pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}
				// pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Below].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage]
				if (pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Img, D2D1::RectF(pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].X.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Y.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].X.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Width.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Height.v), pptUiImageWidget[PptUiImageWidgetID::BottomSide_LeftNextPage].Transparency.v / 255.0f);
				}
			}
			// 底部右侧控件
			if (pptComSetlist.showBottomBoth)
			{
				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage]
				if (pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Img, D2D1::RectF(pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].X.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Y.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].X.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Width.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Height.v), pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightPreviousPage].Transparency.v / 255.0f);
				}

				// pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}
				// pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Below].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage]
				if (pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Img, D2D1::RectF(pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].X.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Y.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].X.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Width.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Height.v), pptUiImageWidget[PptUiImageWidgetID::BottomSide_RightNextPage].Transparency.v / 255.0f);
				}

				// pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar]
				{
					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Color.v), &pBrush);

					ID2D1StrokeStyle* pStrokeStyle = NULL;
					D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties();
					strokeStyleProperties.startCap = D2D1_CAP_STYLE_ROUND;
					strokeStyleProperties.endCap = D2D1_CAP_STYLE_ROUND;
					D2DFactory->CreateStrokeStyle(&strokeStyleProperties, NULL, 0, &pStrokeStyle);

					DCRenderTarget->DrawLine(
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].X1.v, pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Y1.v),
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].X2.v, pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Y2.v),
						pBrush,
						pptUiLineWidget[PptUiLineWidgetID::BottomSide_RightPageWidget_SeekBar].Thickness.v,
						pStrokeStyle
					);

					DxObjectSafeRelease(&pBrush);
					DxObjectSafeRelease(&pStrokeStyle);
				}
			}

			// 中部左侧控件
			if (pptComSetlist.showMiddleBoth)
			{
				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar]
				{
					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Color.v), &pBrush);

					ID2D1StrokeStyle* pStrokeStyle = NULL;
					D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties();
					strokeStyleProperties.startCap = D2D1_CAP_STYLE_ROUND;
					strokeStyleProperties.endCap = D2D1_CAP_STYLE_ROUND;
					D2DFactory->CreateStrokeStyle(&strokeStyleProperties, NULL, 0, &pStrokeStyle);

					DCRenderTarget->DrawLine(
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].X1.v, pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Y1.v),
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].X2.v, pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Y2.v),
						pBrush,
						pptUiLineWidget[PptUiLineWidgetID::MiddleSide_LeftPageWidget_SeekBar].Thickness.v,
						pStrokeStyle
					);

					DxObjectSafeRelease(&pBrush);
					DxObjectSafeRelease(&pStrokeStyle);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage]
				if (pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Img, D2D1::RectF(
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].X.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Y.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].X.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Width.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Height.v),
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftPreviousPage].Transparency.v / 255.0f);
				}

				// pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Above].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}
				// pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_LeftPageNum_Below].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage]
				if (pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Img, D2D1::RectF(
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].X.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Y.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].X.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Width.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Height.v),
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_LeftNextPage].Transparency.v / 255.0f);
				}
			}
			// 中部右侧控件
			if (pptComSetlist.showMiddleBoth)
			{
				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar]
				{
					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Color.v), &pBrush);

					ID2D1StrokeStyle* pStrokeStyle = NULL;
					D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties();
					strokeStyleProperties.startCap = D2D1_CAP_STYLE_ROUND;
					strokeStyleProperties.endCap = D2D1_CAP_STYLE_ROUND;
					D2DFactory->CreateStrokeStyle(&strokeStyleProperties, NULL, 0, &pStrokeStyle);

					DCRenderTarget->DrawLine(
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].X1.v, pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Y1.v),
						D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].X2.v, pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Y2.v),
						pBrush,
						pptUiLineWidget[PptUiLineWidgetID::MiddleSide_RightPageWidget_SeekBar].Thickness.v,
						pStrokeStyle
					);

					DxObjectSafeRelease(&pBrush);
					DxObjectSafeRelease(&pStrokeStyle);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage]
				if (pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Img, D2D1::RectF(
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].X.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Y.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].X.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Width.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Height.v),
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightPreviousPage].Transparency.v / 255.0f);
				}

				// pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Above].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}
				// pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below]
				{
					IDWriteTextFormat* textFormat = NULL;

					D2DTextFactory->CreateTextFormat(
						L"HarmonyOS Sans SC",
						D2DFontCollection,
						DWRITE_FONT_WEIGHT_NORMAL,
						DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL,
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsHeight.v,
						L"zh-cn",
						&textFormat
					);

					ID2D1SolidColorBrush* pBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(D2D1::ColorF(ConvertToD2DColor(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsColor.v)), &pBrush);

					textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

					DCRenderTarget->DrawText(
						pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsContent.c_str(),  // 文本
						wcslen(pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].WordsContent.c_str()),  // 文本长度
						textFormat,  // 文本格式
						D2D1::RectF(
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Left.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Top.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Right.v,
							pptUiWordsWidget[PptUiWordsWidgetID::MiddleSide_RightPageNum_Below].Bottom.v
						),
						pBrush
					);

					DxObjectSafeRelease(&textFormat);
					DxObjectSafeRelease(&pBrush);
				}

				// pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage]
				{
					ID2D1SolidColorBrush* pFrameBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FrameColor.v), &pFrameBrush);
					ID2D1SolidColorBrush* pFillBrush = NULL;
					DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v), &pFillBrush);

					D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].X.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Y.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Width.v,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Height.v),
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].EllipseWidth.v / 2.0f,
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].EllipseHeight.v / 2.0f
					);

					DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
					DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FrameThickness.v);

					DxObjectSafeRelease(&pFrameBrush);
					DxObjectSafeRelease(&pFillBrush);
				}
				// pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage]
				if (pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Img != NULL)
				{
					DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Img, D2D1::RectF(
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].X.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Y.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].X.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Width.v,
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Y.v + pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Height.v),
						pptUiImageWidget[PptUiImageWidgetID::MiddleSide_RightNextPage].Transparency.v / 255.0f);
				}
			}

			if (pptComSetlist.showBottomMiddle)
			{
				// 底部中间控件
				{
					// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget]
					{
						ID2D1SolidColorBrush* pFrameBrush = NULL;
						DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FrameColor.v), &pFrameBrush);
						ID2D1SolidColorBrush* pFillBrush = NULL;
						DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FillColor.v), &pFillBrush);

						D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Width.v,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height.v),
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].EllipseWidth.v / 2.0f,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].EllipseHeight.v / 2.0f
						);

						DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
						DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].FrameThickness.v);

						DxObjectSafeRelease(&pFrameBrush);
						DxObjectSafeRelease(&pFillBrush);
					}
					// pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar]
					{
						ID2D1SolidColorBrush* pBrush = NULL;
						DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Color.v), &pBrush);

						ID2D1StrokeStyle* pStrokeStyle = NULL;
						D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties();
						strokeStyleProperties.startCap = D2D1_CAP_STYLE_ROUND;
						strokeStyleProperties.endCap = D2D1_CAP_STYLE_ROUND;
						D2DFactory->CreateStrokeStyle(&strokeStyleProperties, NULL, 0, &pStrokeStyle);

						DCRenderTarget->DrawLine(
							D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].X1.v, pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Y1.v),
							D2D1::Point2F(pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].X2.v, pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Y2.v),
							pBrush,
							pptUiLineWidget[PptUiLineWidgetID::BottomSide_MiddleWidget_SeekBar].Thickness.v,
							pStrokeStyle
						);

						DxObjectSafeRelease(&pBrush);
						DxObjectSafeRelease(&pStrokeStyle);
					}

					// pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow]
					{
						ID2D1SolidColorBrush* pFrameBrush = NULL;
						DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FrameColor.v), &pFrameBrush);
						ID2D1SolidColorBrush* pFillBrush = NULL;
						DCRenderTarget->CreateSolidColorBrush(ConvertToD2DColor(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v), &pFillBrush);

						D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].X.v,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Y.v,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Width.v,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].Height.v),
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].EllipseWidth.v / 2.0f,
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].EllipseHeight.v / 2.0f
						);

						DCRenderTarget->FillRoundedRectangle(&roundedRect, pFillBrush);
						DCRenderTarget->DrawRoundedRectangle(&roundedRect, pFrameBrush, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FrameThickness.v);

						DxObjectSafeRelease(&pFrameBrush);
						DxObjectSafeRelease(&pFillBrush);
					}
					// pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow]
					if (pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Img != NULL)
					{
						DCRenderTarget->DrawBitmap(pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Img, D2D1::RectF(pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].X.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Y.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].X.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Width.v, pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Y.v + pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Height.v), pptUiImageWidget[PptUiImageWidgetID::BottomSide_MiddleEndShow].Transparency.v / 255.0f);
					}
				}
			}

			DCRenderTarget->EndDraw();

			{
				UpdateLayeredWindowIndirect(ppt_window, &ulwi);

				if (!IsShowWindow)
				{
					IdtWindowsIsVisible.pptWindow = true;
					IsShowWindow = true;
				}

				// 动态平衡帧率
				if (tRecord)
				{
					int delay = 1000 / 24 - (clock() - tRecord);
					if (delay > 0) this_thread::sleep_for(chrono::milliseconds(delay));
				}
				tRecord = clock();
			}
		}
		else for (int i = 1; i <= 50; i++)
		{
			if (PptUiChangeSignal) break;
			this_thread::sleep_for(chrono::milliseconds(10));
		}
	}

	for (int r = 0; r < (int)size(pptIconBitmap); r++) DxObjectSafeRelease(&pptIconBitmap[r]);
	DxObjectSafeRelease(&DCRenderTarget);

	for (int i = 1; i <= 5; i++)
	{
		if (!threadStatus[L"PptUI"]) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}
	threadStatus[L"PptDraw"] = false;
}
void PptInteract()
{
	ExMessage m;
	int last_x = -1, last_y = -1;

	while (!offSignal)
	{
		if (PptInfoStateBuffer.TotalPage != -1)
		{
			hiex::getmessage_win32(&m, EM_MOUSE, ppt_window);
			if (PptInfoStateBuffer.TotalPage == -1)
			{
				hiex::flushmessage_win32(EM_MOUSE, ppt_window);
				continue;
			}

			// 滚轮翻页消息
			if (m.message == WM_MOUSEWHEEL)
			{
				// 下一页
				if (m.wheel <= -120)
				{
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
						FocusPptShow();
						NextPptSlides(temp_currentpage);
					}
					hiex::flushmessage_win32(EM_MOUSE, ppt_window);
				}
				// 上一页
				else
				{
					FocusPptShow();
					PreviousPptSlides();

					hiex::flushmessage_win32(EM_MOUSE, ppt_window);
				}
			}

			if (pptComSetlist.showBottomBoth)
			{
				// 底部左侧控件 上一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
						FocusPptShow();

						PreviousPptSlides();
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;
							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								PreviousPptSlides();
								pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
							}

							this_thread::sleep_for(chrono::milliseconds(15));
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
				// 底部左侧控件 下一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
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
							FocusPptShow();

							NextPptSlides(temp_currentpage);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);

							std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
							while (1)
							{
								if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;

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
									else if (temp_currentpage != -1)
									{
										NextPptSlides(temp_currentpage);
										pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
									}
								}

								this_thread::sleep_for(chrono::milliseconds(15));
							}
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

				// 底部左侧全局拖动条
				if (IsInRect(m.x, m.y, { long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Width.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v) }))
				{
					if (m.message == WM_LBUTTONDOWN)
					{
						auto moveDis = PptBottomPageWidgetSeekBar(m.x, m.y, false);
						if (moveDis <= 20)
						{
							if (IsInRect(m.x, m.y, { long(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Left.v + 5.0f * pptComSetlist.bottomSideBothWidgetScale), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v), long(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_LeftPageNum_Above].Right.v - 5.0f * pptComSetlist.bottomSideBothWidgetScale), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Height.v) }))
							{
								ViewPptShow();
							}
						}

						hiex::flushmessage_win32(EM_MOUSE, ppt_window);
					}
				}

				// 底部右侧控件 上一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
						FocusPptShow();

						PreviousPptSlides();
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;
							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								PreviousPptSlides();
								pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
							}

							this_thread::sleep_for(chrono::milliseconds(15));
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
				// 底部右侧控件 下一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
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
							FocusPptShow();

							NextPptSlides(temp_currentpage);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);

							std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
							while (1)
							{
								if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;

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
									else if (temp_currentpage != -1)
									{
										NextPptSlides(temp_currentpage);
										pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
									}
								}

								this_thread::sleep_for(chrono::milliseconds(15));
							}
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

				// 底部右侧全局拖动条
				if (IsInRect(m.x, m.y, { long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Width.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height.v) }))
				{
					if (m.message == WM_LBUTTONDOWN)
					{
						auto moveDis = PptBottomPageWidgetSeekBar(m.x, m.y, true);
						if (moveDis <= 20)
						{
							if (IsInRect(m.x, m.y, { long(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Left.v + 5.0f * pptComSetlist.bottomSideBothWidgetScale), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v), long(pptUiWordsWidget[PptUiWordsWidgetID::BottomSide_RightPageNum_Above].Right.v - 5.0f * pptComSetlist.bottomSideBothWidgetScale), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_RightPageWidget].Height.v) }))
							{
								ViewPptShow();
							}
						}

						hiex::flushmessage_win32(EM_MOUSE, ppt_window);
					}
				}
			}
			if (pptComSetlist.showMiddleBoth)
			{
				// 中部左侧控件 上一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
						FocusPptShow();

						PreviousPptSlides();
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;
							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								PreviousPptSlides();
								pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
							}

							this_thread::sleep_for(chrono::milliseconds(15));
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
				// 中部左侧控件 下一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
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
							FocusPptShow();

							NextPptSlides(temp_currentpage);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);

							std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
							while (1)
							{
								if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;

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
									else if (temp_currentpage != -1)
									{
										NextPptSlides(temp_currentpage);
										pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
									}
								}

								this_thread::sleep_for(chrono::milliseconds(15));
							}
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

				// 中部左侧全局拖动条
				if (IsInRect(m.x, m.y, { long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Y.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_LeftPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Height.v) }))
				{
					if (m.message == WM_LBUTTONDOWN)
					{
						auto moveDis = PptMiddlePageWidgetSeekBar(m.x, m.y, false);
						if (moveDis <= 20)
						{
							if (IsInRect(m.x, m.y, { long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v),
								long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_PreviousPage].Height.v + 5.0f * pptComSetlist.middleSideBothWidgetScale),
								long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget].Width.v),
								long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_LeftPageWidget_NextPage].Y.v - 5.0f * pptComSetlist.middleSideBothWidgetScale) }))
							{
								ViewPptShow();
							}
						}

						hiex::flushmessage_win32(EM_MOUSE, ppt_window);
					}
				}

				// 中部右侧控件 上一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
						FocusPptShow();

						PreviousPptSlides();
						pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);

						std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
						while (1)
						{
							if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;
							if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - KeyboardInteractionManipulated).count() >= 400)
							{
								PreviousPptSlides();
								pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(200, 200, 200, 255);
							}

							this_thread::sleep_for(chrono::milliseconds(15));
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].FillColor.v = RGBA(250, 250, 250, 160);
				// 中部右侧控件 下一页
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
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
							FocusPptShow();

							NextPptSlides(temp_currentpage);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);

							std::chrono::high_resolution_clock::time_point KeyboardInteractionManipulated = std::chrono::high_resolution_clock::now();
							while (1)
							{
								if (!IdtInputs::IsKeyBoardDown(VK_LBUTTON)) break;

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
									else if (temp_currentpage != -1)
									{
										NextPptSlides(temp_currentpage);
										pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(200, 200, 200, 255);
									}
								}

								this_thread::sleep_for(chrono::milliseconds(15));
							}
						}

						pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].FillColor.v = RGBA(250, 250, 250, 160);

				// 中部右侧全局拖动条
				if (IsInRect(m.x, m.y, { long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Height.v) }))
				{
					if (m.message == WM_LBUTTONDOWN)
					{
						auto moveDis = PptMiddlePageWidgetSeekBar(m.x, m.y, true);
						if (moveDis <= 20)
						{
							if (IsInRect(m.x, m.y, { long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v),
								long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_PreviousPage].Height.v + 5.0f * pptComSetlist.middleSideBothWidgetScale),
								long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget].Width.v),
								long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::MiddleSide_RightPageWidget_NextPage].Y.v - 5.0f * pptComSetlist.middleSideBothWidgetScale) }))
							{
								ViewPptShow();
							}
						}

						hiex::flushmessage_win32(EM_MOUSE, ppt_window);
					}
				}
			}
			if (pptComSetlist.showBottomMiddle)
			{
				// 底部中间控件 结束放映
				if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow]))
				{
					if (last_x != m.x || last_y != m.y)
					{
						if (pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v != RGBA(225, 225, 225, 255))
						{
							pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v = RGBA(225, 225, 225, 255);
							pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.replace = true;
						}
					}
					else pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v = RGBA(250, 250, 250, 160);

					if (m.message == WM_LBUTTONDOWN)
					{
						int lx = m.x, ly = m.y;
						while (1)
						{
							ExMessage m = hiex::getmessage_win32(EM_MOUSE, ppt_window);
							if (PptUiIsInRoundRect(m.x, m.y, pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow]))
							{
								if (!m.lbutton)
								{
									pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v = RGBA(200, 200, 200, 255);

									if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && penetrate.select == false)
									{
										if (!CheckEndShow.Check()) break;

										ChangeStateModeToSelection();
									}

									EndPptShow();

									break;
								}
							}
							else break;
						}

						hiex::flushmessage_win32(EM_MOUSE, ppt_window);

						POINT pt;
						GetCursorPos(&pt);
						last_x = pt.x, last_y = pt.y;
					}
				}
				else if (PptInfoStateBuffer.TotalPage != -1) pptUiRoundRectWidgetTarget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget_EndShow].FillColor.v = RGBA(250, 250, 250, 160);

				// 底部全局拖动条
				if (IsInRect(m.x, m.y, { long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].X.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Width.v), long(pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Y.v + pptUiRoundRectWidget[PptUiRoundRectWidgetID::BottomSide_MiddleTabSlideWidget].Height.v) }))
				{
					if (m.message == WM_LBUTTONDOWN)
					{
						PptBottomMiddleSeekBar(m.x, m.y);
						hiex::flushmessage_win32(EM_MOUSE, ppt_window);
					}
				}
			}
		}
		else
		{
			last_x = -1, last_y = -1;

			hiex::flushmessage_win32(EM_MOUSE, ppt_window);
			this_thread::sleep_for(chrono::milliseconds(500));
		}
	}
}
void PPTLinkageMain()
{
	threadStatus[L"PPTLinkageMain"] = true;

	// 读取 ppt 配置
	{
		if (_waccess((globalPath + L"opt\\pptcom_configuration.json").c_str(), 4) == 0) PptComReadSetting();
		PptComWriteSetting();
	}
	// 检查相关注册表项目
	pptComSetlist.setAdmin = IsPowerPointRunAsAdminSet();

	thread(GetPptState).detach();
	thread(PptInfo).detach();

	thread(PptDraw).detach();
	thread(PptInteract).detach();

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

	int i = 1;
	for (; i <= 5; i++)
	{
		if (!threadStatus[L"GetPptState"] && !threadStatus[L"PptDraw"] && !threadStatus[L"PptInfo"]) break;
		this_thread::sleep_for(chrono::milliseconds(500));
	}

	threadStatus[L"PPTLinkageMain"] = false;
}

// 附加
bool IsPowerPointRunAsAdminSet()
{
	const std::wstring subKeys[] = {
		L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers"
	};

	HKEY hRoots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

	for (HKEY hRoot : hRoots)
	{
		for (const std::wstring& subKey : subKeys)
		{
			HKEY hKey;
			if (RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				DWORD valueCount = 0;
				DWORD maxValueNameLen = 0;
				DWORD maxValueDataLen = 0;

				if (RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, &maxValueNameLen, &maxValueDataLen, nullptr, nullptr) == ERROR_SUCCESS)
				{
					maxValueNameLen++;
					maxValueDataLen++;

					std::wstring valueName(maxValueNameLen, L'\0');
					std::vector<BYTE> data(maxValueDataLen);

					for (DWORD i = 0; i < valueCount; ++i)
					{
						DWORD valueNameLen = maxValueNameLen;
						DWORD dataSize = maxValueDataLen;
						DWORD type = 0;

						if (RegEnumValueW(hKey, i, &valueName[0], &valueNameLen, nullptr, &type, data.data(), &dataSize) == ERROR_SUCCESS)
						{
							valueName.resize(valueNameLen);

							std::wstring lowerValueName = valueName;
							std::transform(lowerValueName.begin(), lowerValueName.end(), lowerValueName.begin(), ::towlower);

							if (lowerValueName.find(L"powerpnt.exe") != std::wstring::npos || lowerValueName.find(L"ksolaunch.exe") != std::wstring::npos)
							{
								if (type == REG_SZ)
								{
									std::wstring dataStr(reinterpret_cast<WCHAR*>(data.data()), dataSize / sizeof(WCHAR) - 1);

									std::wstring lowerDataStr = dataStr;
									std::transform(lowerDataStr.begin(), lowerDataStr.end(), lowerDataStr.begin(), ::towlower);

									if (lowerDataStr.find(L"runasadmin") != std::wstring::npos)
									{
										RegCloseKey(hKey);
										return true;
									}
								}
							}

							valueName.assign(maxValueNameLen, L'\0');
							data.assign(maxValueDataLen, 0);
						}
					}
				}
				RegCloseKey(hKey);
			}
		}
	}
	return false;
}

bool CheckEndShowClass::Check()
{
	if (isChecking == true) return false;
	// isChecking = true;

	// 延迟0.5秒后放开键盘
	auto delayed = async(launch::async, [&]() {
		this_thread::sleep_for(std::chrono::milliseconds(500));
		isChecking = true;
		});

	bool ret = (MessageBox(floating_window, L"Currently in drawing mode, continuing to end will clear the canvas.\nAre you sure you want to end the presentation?\n当前处于绘制模式，继续结束放映将会清空画布内容。\n确定结束放映？", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OKCANCEL) == 1);

	isChecking = false;
	return ret;
}
CheckEndShowClass CheckEndShow;

// --------------------------------------------------
// 其他插件

// DesktopDrawpadBlocker 插件
void StartDesktopDrawpadBlocker()
{
	if (ddbInteractionSetList.enable)
	{
		// 配置 json
		{
			// if (_waccess((pluginPath + L"\\DesktopDrawpadBlocker\\interaction_configuration.json").c_str(), 0) == 0) DdbReadInteraction();

			ddbInteractionSetList.hostPath = GetCurrentExePath();

			ddbInteractionSetList.mode = 1;
			ddbInteractionSetList.restartHost = true;
		}

		// 配置 EXE
		if (_waccess((pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), 0) == -1)
		{
			if (_waccess((pluginPath + L"\\DesktopDrawpadBlocker").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directories(pluginPath + L"\\DesktopDrawpadBlocker", ec);
			}
			ExtractResource((pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
		}
		else
		{
			string hash_sha256;
			{
				hashwrapper* myWrapper = new sha256wrapper();
				hash_sha256 = myWrapper->getHashFromFileW(pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe");
				delete myWrapper;
			}

			if (hash_sha256 != ddbInteractionSetList.DdbSHA256)
			{
				if (isProcessRunning((pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
				{
					// 需要关闭旧版 DDB 并更新版本

					DdbWriteInteraction(true, true);
					for (int i = 1; i <= 20; i++)
					{
						if (!isProcessRunning((pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
							break;
						this_thread::sleep_for(chrono::milliseconds(500));
					}
				}
				ExtractResource((pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
			}
		}

		// 启动 DDB
		if (!isProcessRunning((pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
		{
			DdbWriteInteraction(true, false);
			if (ddbInteractionSetList.runAsAdmin) ShellExecuteW(NULL, L"runas", (pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
			else ShellExecuteW(NULL, NULL, (pluginPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
	}
	else if (_waccess((pluginPath + L"\\DesktopDrawpadBlocker").c_str(), 0) == 0)
	{
		error_code ec;
		filesystem::remove_all(pluginPath + L"\\DesktopDrawpadBlocker", ec);
	}
}

// 快捷方式保障助手 插件
ShortcutAssistantClass shortcutAssistant;
void ShortcutAssistantClass::SetShortcut()
{
	wchar_t desktopPath[MAX_PATH];
	wstring DesktopPath;

	if (SHGetSpecialFolderPathW(0, desktopPath, CSIDL_DESKTOP, FALSE)) DesktopPath = wstring(desktopPath) + L"\\";
	else return;

	if (setlist.shortcutAssistant.correctLnk)
	{
		if (_waccess((DesktopPath + IW("Widget/LnkName") + L".lnk").c_str(), 0) == 0)
		{
			// 存在对应名称的 Lnk
			if (!IsShortcutPointingToDirectory(DesktopPath + IW("Widget/LnkName") + L".lnk", GetCurrentExePath()))
			{
				// 不指向当前的程序路径
				error_code ec;
				filesystem::remove(DesktopPath + IW("Widget/LnkName") + L".lnk", ec);

				CreateShortcut(DesktopPath + IW("Widget/LnkName").c_str() + L".lnk", GetCurrentExePath());
			}
		}
		{
			for (const auto& entry : filesystem::directory_iterator(DesktopPath))
			{
				if (filesystem::is_regular_file(entry) && entry.path().extension() == L".lnk")
				{
					if (entry.path().wstring() != DesktopPath + IW("Widget/LnkName").c_str() + L".lnk" && IsShortcutPointingToDirectory(entry.path().wstring(), GetCurrentExePath()))
					{
						// 存在指向当前的程序路径的快捷方式，但是其名称并不正确
						error_code ec;
						filesystem::remove(entry.path().wstring(), ec);

						CreateShortcut(DesktopPath + IW("Widget/LnkName") + L".lnk", GetCurrentExePath());
					}
				}
			}
		}
	}
	if (setlist.shortcutAssistant.createLnk)
	{
		if (_waccess((DesktopPath + IW("Widget/LnkName").c_str() + L".lnk").c_str(), 0) == -1 ||
			!IsShortcutPointingToDirectory((DesktopPath + IW("Widget/LnkName").c_str() + L".lnk"), GetCurrentExePath()))
		{
			CreateShortcut(DesktopPath + IW("Widget/LnkName").c_str() + L".lnk", GetCurrentExePath());
		}
	}

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	return;
}
bool ShortcutAssistantClass::IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory)
{
	IShellLink* psl;
	//CoInitialize(NULL);

	// 创建一个IShellLink对象
	HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
	if (SUCCEEDED(hres))
	{
		IPersistFile* ppf;

		// 获取IShellLink的IPersistFile接口
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
		if (SUCCEEDED(hres))
		{
			// 打开快捷方式文件
			hres = ppf->Load(shortcutPath.c_str(), STGM_READ);
			if (SUCCEEDED(hres))
			{
				WIN32_FIND_DATAW wfd;
				ZeroMemory(&wfd, sizeof(wfd));
				// 获取快捷方式的目标路径
				hres = psl->GetPath(wfd.cFileName, MAX_PATH, NULL, SLGP_RAWPATH);
				if (SUCCEEDED(hres))
				{
					// 检查目标路径是否与指定目录相匹配
					if (std::wstring(wfd.cFileName).find(targetDirectory) != std::wstring::npos)
					{
						return true;
					}
				}
			}
			ppf->Release();
		}
		psl->Release();
	}
	//CoUninitialize();

	return false;
}
bool ShortcutAssistantClass::CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath)
{
	//CoInitialize(NULL);

	// 创建一个IShellLink对象
	IShellLink* psl;
	HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);

	if (SUCCEEDED(hres))
	{
		// 设置快捷方式的目标路径
		psl->SetPath(targetExePath.c_str());

		// 创建一个IShellLink对象的IPersistFile接口
		IPersistFile* ppf;
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

		if (SUCCEEDED(hres))
		{
			// 保存快捷方式
			hres = ppf->Save(shortcutPath.c_str(), TRUE);
			ppf->Release();
		}

		psl->Release();
	}

	//CoUninitialize();

	return SUCCEEDED(hres);
}