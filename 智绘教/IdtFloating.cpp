#pragma once
#include "IdtFloating.h"

#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFreezeFrame.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

floating_windowsStruct floating_windows;

IMAGE floating_icon[25], sign;
IMAGE skin[5];

double state;
double target_status;

bool reserve_drawpad = false;
bool smallcard_refresh = true;

//UI 控件

int BackgroundColorMode;
map<wstring, UIControlStruct> UIControl, UIControlTarget;
map<wstring, UIControlStruct>& map<wstring, UIControlStruct>::operator=(const map<wstring, UIControlStruct>& m)
{
	//判断自赋值
	if (this == &m) return *this;
	//清空当前map
	this->clear();
	//遍历参数map，把每个键值对赋值给当前map
	for (auto it = m.begin(); it != m.end(); it++) this->insert(*it);
	//返回当前map的引用
	return *this;
}
map<wstring, UIControlColorStruct> UIControlColor, UIControlColorTarget;
map<wstring, UIControlColorStruct>& map<wstring, UIControlColorStruct>::operator=(const map<wstring, UIControlColorStruct>& m)
{
	//判断自赋值
	if (this == &m) return *this;
	//清空当前map
	this->clear();
	//遍历参数map，把每个键值对赋值给当前map
	for (auto it = m.begin(); it != m.end(); it++) this->insert(*it);
	//返回当前map的引用
	return *this;
}

//选色盘
Color ColorFromHSV(float hue, float saturation, float value) {
	float c = value * saturation;
	float x = c * (1 - (float)abs(fmod(hue / 60.0f, 2) - 1));
	float m = value - c;

	float r, g, b;
	if (hue < 60) {
		r = c; g = x; b = 0;
	}
	else if (hue < 120) {
		r = x; g = c; b = 0;
	}
	else if (hue < 180) {
		r = 0; g = c; b = x;
	}
	else if (hue < 240) {
		r = 0; g = x; b = c;
	}
	else if (hue < 300) {
		r = x; g = 0; b = c;
	}
	else {
		r = c; g = 0; b = x;
	}

	return Color(255, (BYTE)((r + m) * 255), (BYTE)((g + m) * 255), (BYTE)((b + m) * 255));
}
IMAGE DrawHSVWheel(int r, int z, int angle)
{
	IMAGE ret = CreateImageColor(r, r, RGBA(0, 0, 0, 0), true);
	r--;

	Graphics g(GetImageHDC(&ret));
	g.SetSmoothingMode(SmoothingModeHighQuality);

	for (int i = 0; i < 360; i++)
	{
		float currentAngle = float(angle + i);  // 当前段的起始角度
		if (currentAngle >= 360) currentAngle -= 360;  // 保持currentAngle在0到360之间

		Color color = ColorFromHSV(float(i), 1.0f, 1.0f);
		SolidBrush brush(color);
		g.FillPie(&brush, 0, 0, r, r, currentAngle, 4);
	}

	GraphicsPath path;
	path.AddEllipse(0 + z, 0 + z, r - z * 2, r - z * 2);
	PathGradientBrush pgb(&path);
	Color colors[] = { Color(0, 255, 255, 255) };  // 圆边缘
	INT count = 1;
	pgb.SetSurroundColors(colors, &count);
	pgb.SetCenterColor(Color(255, 255, 255, 255));  // 圆心
	pgb.SetCenterPoint(PointF(r / 2.0f, r / 2.0f));

	// 使用径向渐变刷绘制渐变圆形
	g.FillEllipse(&pgb, 0, 0, r, r);

	return ret;
}
//时钟皮肤
pair<double, double> GetPointOnCircle(double x, double y, double r, double angle)
{
	// 将角度转换为弧度
	double radian = (angle * 3.14159265358979323846) / 180.0;

	// 计算圆上点的坐标
	double px = x + r * sin(radian);
	double py = y - r * cos(radian);

	return make_pair(px + 0.5, py + 0.5);
}

void SeekBar(ExMessage m)
{
	if (!KeyBoradDown[VK_LBUTTON]) return;

	POINT p;
	GetCursorPos(&p);

	int pop_x = p.x;
	int pop_y = p.y;

	while (1)
	{
		if (!KeyBoradDown[VK_LBUTTON]) break;

		POINT p;
		GetCursorPos(&p);

		pop_x = p.x;
		pop_y = p.y;

		SetWindowPos(floating_window, NULL,
			floating_windows.x = min(GetSystemMetrics(SM_CXSCREEN) - floating_windows.width, max(1, p.x - m.x)),
			floating_windows.y = min(GetSystemMetrics(SM_CYSCREEN) - floating_windows.height, max(1, p.y - m.y)),
			0,
			0,
			SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
	}

	return;
}

HHOOK FloatingHookCall;
LRESULT CALLBACK FloatingHookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0)
	{
		if (wParam == WM_LBUTTONDOWN) KeyBoradDown[VK_LBUTTON] = true;
		else if (wParam == WM_MBUTTONDOWN) KeyBoradDown[VK_MBUTTON] = true;
		else if (wParam == WM_RBUTTONDOWN) KeyBoradDown[VK_RBUTTON] = true;
		else if (wParam == WM_LBUTTONUP) KeyBoradDown[VK_LBUTTON] = false;
		else if (wParam == WM_MBUTTONUP) KeyBoradDown[VK_MBUTTON] = false;
		else if (wParam == WM_RBUTTONUP) KeyBoradDown[VK_RBUTTON] = false;

		if (wParam == WM_MOUSEWHEEL && !choose.select && !penetrate.select && ppt_show != NULL)
		{
			MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

			ExMessage msgKey = {};
			msgKey.message = WM_MOUSEWHEEL;
			msgKey.wheel = GET_WHEEL_DELTA_WPARAM(pMouseStruct->mouseData);

			if (msgKey.wheel <= -120)
			{
				PPTUIControlColor[L"RoundRect/RoundRectLeft2/fill"].v = RGBA(200, 200, 200, 255);
				PPTUIControlColor[L"RoundRect/RoundRectRight2/fill"].v = RGBA(200, 200, 200, 255);

				std::unique_lock<std::shared_mutex> LockPPTManipulatedSm(PPTManipulatedSm);
				PPTManipulated = std::chrono::high_resolution_clock::now();
				LockPPTManipulatedSm.unlock();
			}
			else
			{
				PPTUIControlColor[L"RoundRect/RoundRectLeft1/fill"].v = RGBA(200, 200, 200, 255);
				PPTUIControlColor[L"RoundRect/RoundRectRight1/fill"].v = RGBA(200, 200, 200, 255);

				std::unique_lock<std::shared_mutex> LockPPTManipulatedSm(PPTManipulatedSm);
				PPTManipulated = std::chrono::high_resolution_clock::now();
				LockPPTManipulatedSm.unlock();
			}

			int index = hiex::GetWindowIndex(ppt_window, false);
			std::unique_lock<std::shared_mutex> lg_vecWindows_vecMessage_sm(hiex::g_vecWindows_vecMessage_sm[index]);
			hiex::g_vecWindows[index].vecMessage.push_back(msgKey);
			lg_vecWindows_vecMessage_sm.unlock();

			return 1;
		}
	}
	// 继续传递事件给下一个钩子或目标窗口
	return CallNextHookEx(FloatingHookCall, nCode, wParam, lParam);
}
void FloatingInstallHook()
{
	// 安装钩子
	FloatingHookCall = SetWindowsHookEx(WH_MOUSE_LL, FloatingHookCallback, NULL, 0);
	if (FloatingHookCall == NULL) return;

	MSG msg;
	while (!off_signal && GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// 卸载钩子
	UnhookWindowsHookEx(FloatingHookCall);
}

//绘制屏幕
void DrawScreen()
{
	Bitmap* bskin3;

	thread_status[L"DrawScreen"] = true;
	//初始化
	{
		//模式配置初始化
		{
			choose.select = true;
		}
		//媒体资源读取
		{
			loadimage(&floating_icon[0], L"PNG", L"icon0", 40, 40, true);
			loadimage(&floating_icon[19], L"PNG", L"icon19", 15, 15, true);

			loadimage(&floating_icon[1], L"PNG", L"icon1", 40, 40, true);
			loadimage(&floating_icon[2], L"PNG", L"icon2", 40, 40, true);
			loadimage(&floating_icon[3], L"PNG", L"icon3", 30, 30, true);
			loadimage(&floating_icon[4], L"PNG", L"icon4");
			loadimage(&floating_icon[5], L"PNG", L"icon5");

			loadimage(&floating_icon[7], L"PNG", L"icon7");
			loadimage(&floating_icon[10], L"PNG", L"icon10");

			loadimage(&floating_icon[6], L"PNG", L"icon6", 30, 30, true);
			loadimage(&floating_icon[8], L"PNG", L"icon8", 30, 30, true);

			loadimage(&floating_icon[11], L"PNG", L"icon11", 20, 20, true);
			loadimage(&floating_icon[12], L"PNG", L"icon12", 20, 20, true);
			loadimage(&floating_icon[13], L"PNG", L"icon13", 20, 20, true);
			loadimage(&floating_icon[14], L"PNG", L"icon14", 20, 20, true);
			loadimage(&floating_icon[15], L"PNG", L"icon15", 20, 20, true);

			loadimage(&floating_icon[16], L"PNG", L"icon16", 25, 25, true);
			loadimage(&floating_icon[18], L"PNG", L"icon18", 20, 20, true);
			loadimage(&floating_icon[17], L"PNG", L"icon17", 20, 20, true);
			loadimage(&floating_icon[20], L"PNG", L"icon20", 20, 20, true);

			loadimage(&sign, L"PNG", L"sign1", 30, 30, true);

			loadimage(&skin[1], L"PNG", L"skin1");
			loadimage(&skin[2], L"PNG", L"skin1-2");
			loadimage(&skin[3], L"PNG", L"skin1-3");

			bskin3 = IMAGEToBitmap(&skin[3]);
		}

		//窗口初始化
		{
			//SetWindowTransparent(true, 0);

			setbkmode(TRANSPARENT);
			setbkcolor(RGB(255, 255, 255));

			DisableResizing(floating_window, true);//禁止窗口拉伸
			SetWindowLong(floating_window, GWL_STYLE, GetWindowLong(floating_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
			SetWindowPos(floating_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
			MoveWindow(floating_window, floating_windows.x = GetSystemMetrics(SM_CXSCREEN) - 594, floating_windows.y = GetSystemMetrics(SM_CYSCREEN) - 557, floating_windows.width = background.getwidth(), floating_windows.height = background.getheight(), SWP_NOSIZE);
			SetWindowLong(floating_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

			// 禁用点击手势
			//GESTURECONFIG gc = { 0 };
			//gc.dwID = 1;
			//gc.dwWant = 0;
			//SetGestureConfig(floating_window, 0, 1, &gc, sizeof(GESTURECONFIG));
		}
		SetImageColor(background, RGBA(0, 0, 0, 0), true);

		//UI 初始化
		{
			//主栏
			{
				//圆形
				{
					UIControl[L"Ellipse/Ellipse1/x"] = { (float)floating_windows.width - 96, 5, 1 };
					UIControl[L"Ellipse/Ellipse1/y"] = { (float)floating_windows.height - 155, 5, 1 };
					UIControl[L"Ellipse/Ellipse1/width"] = { 94, 5, 1 };
					UIControl[L"Ellipse/Ellipse1/height"] = { 94, 5, 1 };
					UIControl[L"Image/Sign1/frame_transparency"] = { 255, 300, 1 };

					UIControlColor[L"Ellipse/Ellipse1/fill"] = { RGBA(0, 0, 0, 150), 10, 1 };
					UIControlColor[L"Ellipse/Ellipse1/frame"] = { RGB(255, 255, 255), 10, 1 };
				}
				//圆角矩形
				{
					{
						UIControl[L"RoundRect/RoundRect1/x"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 26, 5, 1 };
						UIControl[L"RoundRect/RoundRect1/y"] = { (float)floating_windows.height - 155, 5, 1 };
						UIControl[L"RoundRect/RoundRect1/width"] = { 30, 5, 1 };
						UIControl[L"RoundRect/RoundRect1/height"] = { 94, 5, 1 };
						UIControl[L"RoundRect/RoundRect1/ellipseheight"] = { 25, 5, 1 };
						UIControl[L"RoundRect/RoundRect1/ellipsewidth"] = { 25, 5, 1 };

						UIControlColor[L"RoundRect/RoundRect1/fill"] = { RGBA(255, 255, 255, 0), 10, 1 };
						UIControlColor[L"RoundRect/RoundRect1/frame"] = { RGBA(150, 150, 150, 0), 10, 1 };
					}
					{
						UIControl[L"RoundRect/RoundRect2/x"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33, 5, 1 };
						UIControl[L"RoundRect/RoundRect2/y"] = { (float)floating_windows.height - 156 + 8, 5, 1 };
						UIControl[L"RoundRect/RoundRect2/width"] = { 80, 5, 1 };
						UIControl[L"RoundRect/RoundRect2/height"] = { 80, 5, 1 };
						UIControl[L"RoundRect/RoundRect2/ellipseheight"] = { 25, 5, 1 };
						UIControl[L"RoundRect/RoundRect2/ellipsewidth"] = { 25, 5, 1 };

						UIControlColor[L"RoundRect/RoundRect2/frame"] = { RGBA(98, 175, 82, 0), 10, 1 };
					}

					{
						UIControl[L"RoundRect/BrushTop/x"] = { (float)floating_windows.width - 48, 3, 1 };
						UIControl[L"RoundRect/BrushTop/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v, 3, 1 };
						UIControl[L"RoundRect/BrushTop/width"] = { 80, 3, 1 };
						UIControl[L"RoundRect/BrushTop/height"] = { 90, 3, 1 };
						UIControl[L"RoundRect/BrushTop/ellipseheight"] = { 25, 3, 1 };
						UIControl[L"RoundRect/BrushTop/ellipsewidth"] = { 25, 3, 1 };

						UIControlColor[L"RoundRect/BrushTop/fill"] = { RGBA(255, 255, 255, 0), 10, 1 };
						UIControlColor[L"RoundRect/BrushTop/frame"] = { RGBA(150, 150, 150, 0), 10, 1 };
					}
					{
						{
							UIControl[L"RoundRect/BrushColor1/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor1/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor1/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor1/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor1/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor1/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor1/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor1/fill"] = { RGB(255, 255, 255), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame1/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame1/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame1/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame1/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame1/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame1/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame1/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame1/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor1/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor1/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor1/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor2/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor2/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor2/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor2/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor2/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor2/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor2/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor2/fill"] = { RGB(0, 0, 0), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame2/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame2/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame2/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame2/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame2/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame2/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame2/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame2/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor2/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor2/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor2/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor3/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor3/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor3/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor3/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor3/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor3/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor3/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor3/fill"] = { RGB(255, 139, 0), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame3/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame3/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame3/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame3/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame3/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame3/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame3/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame3/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor3/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor3/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor3/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor4/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor4/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor4/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor4/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor4/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor4/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor4/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor4/fill"] = { RGB(50, 30, 181), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame4/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame4/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame4/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame4/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame4/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame4/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame4/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame4/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor4/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor4/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor4/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor5/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor5/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor5/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor5/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor5/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor5/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor5/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor5/fill"] = { RGB(255, 197, 16), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame5/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame5/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame5/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame5/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame5/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame5/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame5/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame5/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor5/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor5/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor5/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor6/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor6/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor6/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor6/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor6/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor6/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor6/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor6/fill"] = { RGB(255, 16, 0), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame6/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame6/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame6/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame6/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame6/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame6/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame6/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame6/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor6/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor6/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor6/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor7/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor7/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor7/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor7/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor7/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor7/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor7/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor7/fill"] = { RGB(78,161,183), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame7/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame7/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame7/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame7/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame7/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame7/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame7/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame7/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor7/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor7/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor7/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor8/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor8/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor8/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor8/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor8/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor8/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor8/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor8/fill"] = { RGB(50, 110, 217), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame8/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame8/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame8/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame8/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame8/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame8/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame8/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame8/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor8/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor8/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor8/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor9/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor9/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor9/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor9/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor9/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor9/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor9/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor9/fill"] = { RGB(102, 213, 82), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame9/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame9/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame9/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame9/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame9/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame9/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame9/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame9/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor9/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor9/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor9/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor10/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor10/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor10/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor10/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor10/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor10/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor10/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor10/fill"] = { RGB(48, 108, 0), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame10/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame10/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame10/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame10/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame10/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame10/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame10/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame10/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor10/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor10/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor10/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor11/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor11/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor11/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor11/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor11/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor11/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColor11/transparency"] = { 0, 10, 1 };
							UIControlColor[L"RoundRect/BrushColor11/fill"] = { RGB(255, 30, 207), 5, 1 };

							UIControl[L"RoundRect/BrushColorFrame11/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame11/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame11/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame11/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame11/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame11/ellipsewidth"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame11/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame11/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"Image/BrushColor11/x"] = { (float)floating_windows.width - 48 + 10, 3, 1 };
							UIControl[L"Image/BrushColor11/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10, 3, 1 };
							UIControl[L"Image/BrushColor11/transparency"] = { 0, 10, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColor12/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColor12/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColor12/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor12/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColor12/transparency"] = { 0, 10, 1 };

							UIControl[L"RoundRect/BrushColorFrame12/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame12/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame12/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame12/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame12/ellipseheight"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame12/ellipsewidth"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorFrame12/angle"] = { 0, 1, 0.1f };
							UIControl[L"RoundRect/BrushColorFrame12/thickness"] = { 1, 20, 0.1f };
							UIControlColor[L"RoundRect/BrushColorFrame12/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };
						}
					}
					{
						UIControl[L"RoundRect/PaintThickness/x"] = { (float)floating_windows.width - 38, 3, 1 };
						UIControl[L"RoundRect/PaintThickness/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 10, 3, 1 };
						UIControl[L"RoundRect/PaintThickness/width"] = { 28, 3, 1 };
						UIControl[L"RoundRect/PaintThickness/height"] = { 70, 3, 1 };
						UIControl[L"RoundRect/PaintThickness/ellipseheight"] = { 25, 3, 1 };
						UIControl[L"RoundRect/PaintThickness/ellipsewidth"] = { 25, 3, 1 };

						UIControlColor[L"RoundRect/PaintThickness/fill"] = { RGBA(230, 230, 230, 0), 5, 1 };

						UIControl[L"RoundRect/PaintThicknessPrompt/x"] = { (float)floating_windows.width - 10, 3, 1 };
						UIControl[L"RoundRect/PaintThicknessPrompt/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 10, 3, 1 };
						UIControl[L"RoundRect/PaintThicknessPrompt/width"] = { 5, 3, 1 };
						UIControl[L"RoundRect/PaintThicknessPrompt/height"] = { 5, 3, 1 };
						UIControl[L"RoundRect/PaintThicknessPrompt/ellipseheight"] = { 5, 3, 1 };
						UIControl[L"RoundRect/PaintThicknessPrompt/ellipsewidth"] = { 5, 3, 1 };

						UIControlColor[L"RoundRect/PaintThicknessPrompt/fill"] = { RGBA(255, 255, 255, 0), 5, 1 };

						{
							UIControl[L"RoundRect/PaintThicknessAdjust/x"] = { (float)floating_windows.width - 38, 3, 1 };
							UIControl[L"RoundRect/PaintThicknessAdjust/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 10, 3, 1 };
							UIControl[L"RoundRect/PaintThicknessAdjust/width"] = { 28, 3, 1 };
							UIControl[L"RoundRect/PaintThicknessAdjust/height"] = { 70, 3, 1 };
							UIControl[L"RoundRect/PaintThicknessAdjust/ellipseheight"] = { 25, 3, 1 };
							UIControl[L"RoundRect/PaintThicknessAdjust/ellipsewidth"] = { 25, 3, 1 };

							UIControlColor[L"RoundRect/PaintThicknessAdjust/fill"] = { RGBA(255, 255, 255, 0), 5, 1 };
							UIControlColor[L"RoundRect/PaintThicknessAdjust/frame"] = { RGBA(150, 150, 150, 0), 5, 1 };

							//调节底
							{
								UIControl[L"RoundRect/PaintThicknessSchedule1/x"] = { (float)floating_windows.width - 38, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule1/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 42, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule1/width"] = { 28, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule1/height"] = { 6, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule1/ellipseheight"] = { 6, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule1/ellipsewidth"] = { 6, 3, 1 };
								UIControlColor[L"RoundRect/PaintThicknessSchedule1/fill"] = { RGBA(230, 230, 230, 0), 10, 1 };

								UIControl[L"RoundRect/PaintThicknessSchedule2/x"] = { (float)floating_windows.width - 38, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule2/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 42, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule2/width"] = { 28, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule2/height"] = { 6, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule2/ellipseheight"] = { 6, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule2/ellipsewidth"] = { 6, 3, 1 };
								UIControlColor[L"RoundRect/PaintThicknessSchedule2/fill"] = { SET_ALPHA(brush.color, 0), 10, 1 };

								UIControl[L"RoundRect/PaintThicknessSchedule3/x"] = { (float)floating_windows.width - 32, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule3/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 25, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule3/width"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule3/height"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule3/ellipseheight"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule3/ellipsewidth"] = { 20, 3, 1 };
								UIControlColor[L"RoundRect/PaintThicknessSchedule3/fill"] = { SET_ALPHA(brush.color, 0), 10, 1 };

								UIControl[L"RoundRect/PaintThicknessSchedule4a/x"] = { (float)floating_windows.width - 32, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule4a/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 25, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule4a/width"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule4a/height"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule4a/ellipse"] = { 20, 3, 1 };
								UIControlColor[L"RoundRect/PaintThicknessSchedule4a/fill"] = { SET_ALPHA(brush.color, 0), 3, 1 };

								UIControl[L"RoundRect/PaintThicknessSchedule5a/x"] = { (float)floating_windows.width - 32, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule5a/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 25, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule5a/width"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule5a/height"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule5a/ellipse"] = { 20, 3, 1 };
								UIControlColor[L"RoundRect/PaintThicknessSchedule5a/fill"] = { SET_ALPHA(brush.color, 0), 3, 1 };

								UIControl[L"RoundRect/PaintThicknessSchedule6a/x"] = { (float)floating_windows.width - 32, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule6a/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 25, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule6a/width"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule6a/height"] = { 20, 3, 1 };
								UIControl[L"RoundRect/PaintThicknessSchedule6a/ellipse"] = { 20, 3, 1 };
								UIControlColor[L"RoundRect/PaintThicknessSchedule6a/fill"] = { SET_ALPHA(brush.color, 0), 3, 1 };
							}
						}
					}
					{
						{
							UIControl[L"RoundRect/BrushColorChoose/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorChoose/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorChoose/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChoose/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChoose/ellipseheight"] = { 5, 3, 1 };
							UIControl[L"RoundRect/BrushColorChoose/ellipsewidth"] = { 5, 3, 1 };

							UIControlColor[L"RoundRect/BrushColorChoose/fill"] = { RGBA(255, 255, 255, 0), 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChoose/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };
						}
						{
							UIControl[L"RoundRect/BrushColorChooseWheel/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseWheel/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseWheel/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseWheel/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseWheel/transparency"] = { 0, 10, 1 };

							UIControl[L"RoundRect/BrushColorChooseMark/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMark/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMark/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMark/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMark/ellipseheight"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMark/ellipsewidth"] = { 40, 3, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMark/fill"] = { SET_ALPHA(brush.color,0), 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMark/frame"] = { RGBA(130, 130, 130, 0), 5, 1 };

							UIControl[L"RoundRect/BrushColorChooseMarkR/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkR/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkR/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkR/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkR/ellipseheight"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkR/ellipsewidth"] = { 40, 3, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkR/fill"] = { RGBA(255, 0, 0, 0) , 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkR/frame"] = { RGBA(255, 0, 0, 0) , 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkR/text"] = { RGBA(255, 0, 0, 0) , 5, 1 };

							UIControl[L"RoundRect/BrushColorChooseMarkG/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkG/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkG/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkG/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkG/ellipseheight"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkG/ellipsewidth"] = { 40, 3, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkG/fill"] = { RGBA(0, 255, 0, 0) , 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkG/frame"] = { RGBA(0, 255, 0, 0) , 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkG/text"] = { RGBA(0, 255, 0, 0) , 5, 1 };

							UIControl[L"RoundRect/BrushColorChooseMarkB/x"] = { (float)floating_windows.width - 48, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkB/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + 30, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkB/width"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkB/height"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkB/ellipseheight"] = { 40, 3, 1 };
							UIControl[L"RoundRect/BrushColorChooseMarkB/ellipsewidth"] = { 40, 3, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkB/fill"] = { RGBA(0, 0, 255, 0) , 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkB/frame"] = { RGBA(0, 0, 255, 0) , 5, 1 };
							UIControlColor[L"RoundRect/BrushColorChooseMarkB/text"] = { RGBA(0, 0,255, 0) , 5, 1 };
						}
					}

					{
						UIControl[L"RoundRect/BrushBottom/x"] = { (float)floating_windows.width - 48, 3, 1 };
						UIControl[L"RoundRect/BrushBottom/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 40, 3, 1 };
						UIControl[L"RoundRect/BrushBottom/width"] = { 48, 3, 1 };
						UIControl[L"RoundRect/BrushBottom/height"] = { 40, 3, 1 };
						UIControl[L"RoundRect/BrushBottom/ellipseheight"] = { 25, 3, 1 };
						UIControl[L"RoundRect/BrushBottom/ellipsewidth"] = { 25, 3, 1 };

						UIControlColor[L"RoundRect/BrushBottom/fill"] = { RGBA(255, 255, 255, 0), 10, 1 };
						UIControlColor[L"RoundRect/BrushBottom/frame"] = { RGBA(150, 150, 150, 0), 10, 1 };
					}
					{
						UIControl[L"RoundRect/BrushChoose/x"] = { (float)floating_windows.width - 48, 3, 1 };
						UIControl[L"RoundRect/BrushChoose/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 35, 3, 1 };
						UIControl[L"RoundRect/BrushChoose/width"] = { 48, 3, 1 };
						UIControl[L"RoundRect/BrushChoose/height"] = { 30, 3, 1 };
						UIControl[L"RoundRect/BrushChoose/ellipseheight"] = { 15, 3, 1 };
						UIControl[L"RoundRect/BrushChoose/ellipsewidth"] = { 15, 3, 1 };

						UIControlColor[L"RoundRect/BrushChoose/frame"] = { SET_ALPHA(brush.color,0), 5, 1 };
					}
					{
						UIControl[L"RoundRect/BrushMode/x"] = { (float)floating_windows.width - 48, 3, 1 };
						UIControl[L"RoundRect/BrushMode/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 35, 3, 1 };
						UIControl[L"RoundRect/BrushMode/width"] = { 48, 3, 1 };
						UIControl[L"RoundRect/BrushMode/height"] = { 30, 3, 1 };
						UIControl[L"RoundRect/BrushMode/ellipseheight"] = { 15, 3, 1 };
						UIControl[L"RoundRect/BrushMode/ellipsewidth"] = { 15, 3, 1 };

						UIControlColor[L"RoundRect/BrushMode/frame"] = { SET_ALPHA(brush.color,0), 5, 1 };
					}
					{
						UIControl[L"RoundRect/BrushInterval/x"] = { (float)floating_windows.width - 48 + 35, 3, 1 };
						UIControl[L"RoundRect/BrushInterval/y"] = { UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 35 + 10, 3, 1 };
						UIControl[L"RoundRect/BrushInterval/width"] = { 3, 3, 1 };
						UIControl[L"RoundRect/BrushInterval/height"] = { 20, 3, 1 };

						UIControlColor[L"RoundRect/BrushInterval/fill"] = { RGBA(150,150,150,0), 5, 1 };
					}
				}
				//图像
				{
					{
						UIControl[L"Image/Sign1/x"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33, 5, 1 };
						UIControl[L"Image/Sign1/y"] = { UIControl[L"Ellipse/Ellipse1/y"].v + 33, 5, 1 };
						UIControl[L"Image/Sign1/transparency"] = { 255, 300, 1 };
					}
					//选择
					{
						UIControl[L"Image/choose/x"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 53, 5, 1 };
						UIControl[L"Image/choose/y"] = { (float)floating_windows.height - 140, 5, 1 };
						UIControlColor[L"Image/choose/fill"] = { RGBA(98, 175, 82, 0), 5, 1 };
					}
					//画笔
					{
						UIControl[L"Image/brush/x"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 53, 5, 1 };
						UIControl[L"Image/brush/y"] = { (float)floating_windows.height - 140, 5, 1 };
						UIControlColor[L"Image/brush/fill"] = { RGBA(130, 130, 130, 0), 5, 1 };

						//画笔底部栏
						{
							{
								UIControl[L"Image/PaintBrush/x"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 10, 3, 1 };
								UIControl[L"Image/PaintBrush/y"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 10, 3, 1 };
								UIControlColor[L"Image/PaintBrush/words_color"] = { SET_ALPHA(brush.color, 0), 5, 1 };
							}
							{
								UIControl[L"Image/FluorescentBrush/x"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 10, 3, 1 };
								UIControl[L"Image/FluorescentBrush/y"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 10, 3, 1 };
								UIControlColor[L"Image/FluorescentBrush/words_color"] = { SET_ALPHA(brush.color, 0), 5, 1 };
							}

							{
								UIControl[L"Image/WriteBrush/x"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 10, 3, 1 };
								UIControl[L"Image/WriteBrush/y"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 10, 3, 1 };
								UIControlColor[L"Image/WriteBrush/words_color"] = { SET_ALPHA(brush.color, 0), 5, 1 };
							}
							{
								UIControl[L"Image/LineBrush/x"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 10, 3, 1 };
								UIControl[L"Image/LineBrush/y"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 10, 3, 1 };
								UIControlColor[L"Image/LineBrush/words_color"] = { SET_ALPHA(brush.color, 0), 5, 1 };
							}
							{
								UIControl[L"Image/RectangleBrush/x"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 10, 3, 1 };
								UIControl[L"Image/RectangleBrush/y"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 10, 3, 1 };
								UIControlColor[L"Image/RectangleBrush/words_color"] = { SET_ALPHA(brush.color, 0), 5, 1 };
							}
						}
					}
					//橡皮
					{
						UIControl[L"Image/rubber/x"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 53, 5, 1 };
						UIControl[L"Image/rubber/y"] = { (float)floating_windows.height - 140, 5, 1 };
						UIControlColor[L"Image/rubber/fill"] = { RGBA(130, 130, 130, 0), 5, 1 };
					}
					//程序调测
					{
						UIControl[L"Image/test/x"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 53, 5, 1 };
						UIControl[L"Image/test/y"] = { (float)floating_windows.height - 140, 5, 1 };
						UIControlColor[L"Image/test/fill"] = { RGBA(130, 130, 130, 0), 5, 1 };
					}
				}
				//文字
				{
					//选择
					{
						UIControl[L"Words/choose/height"] = { 18, 5, 1 };
						UIControl[L"Words/choose/left"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33, 5, 1 };
						UIControl[L"Words/choose/top"] = { (float)floating_windows.height - 155 + 48, 5, 1 };
						UIControl[L"Words/choose/right"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83, 5, 1 };
						UIControl[L"Words/choose/bottom"] = { (float)floating_windows.height - 155 + 48 + 48, 5, 1 };
						UIControlColor[L"Words/choose/words_color"] = { RGBA(98, 175, 82, 0), 5, 1 };
					}
					//画笔
					{
						UIControl[L"Words/brush/height"] = { 18, 5, 1 };
						UIControl[L"Words/brush/left"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33, 5, 1 };
						UIControl[L"Words/brush/top"] = { (float)floating_windows.height - 155 + 48, 5, 1 };
						UIControl[L"Words/brush/right"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83, 5, 1 };
						UIControl[L"Words/brush/bottom"] = { (float)floating_windows.height - 155 + 48 + 48, 5, 1 };
						UIControlColor[L"Words/brush/words_color"] = { RGBA(130, 130, 130, 0), 5, 1 };

						{
							UIControl[L"Words/brushSize/left"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 45, 5, 1 };
							UIControl[L"Words/brushSize/top"] = { (float)floating_windows.height - 155 + 48 - 12, 5, 1 };

							UIControlColor[L"Words/brushSize/words_color"] = { SET_ALPHA(brush.color, 0), 5, 1 };
						}

						//画笔顶部栏
						{
							//画笔粗细
							{
								UIControl[L"Words/PaintThickness/size"] = { 20, 3, 1 };
								UIControl[L"Words/PaintThickness/left"] = { (float)floating_windows.width - 48, 3, 1 };
								UIControl[L"Words/PaintThickness/top"] = { UIControl[L"RoundRect/BrushTop/y"].v , 3, 1 };
								UIControl[L"Words/PaintThickness/width"] = { 50, 3, 1 };
								UIControl[L"Words/PaintThickness/height"] = { 96, 3, 1 };
								UIControlColor[L"Words/PaintThickness/words_color"] = { SET_ALPHA(brush.color, 255), 5, 1 };

								UIControl[L"Words/PaintThicknessValue/size"] = { 20, 3, 1 };
								UIControl[L"Words/PaintThicknessValue/left"] = { (float)floating_windows.width - 48, 3, 1 };
								UIControl[L"Words/PaintThicknessValue/top"] = { UIControl[L"RoundRect/BrushTop/y"].v , 3, 1 };
								UIControl[L"Words/PaintThicknessValue/width"] = { 50, 3, 1 };
								UIControl[L"Words/PaintThicknessValue/height"] = { 96, 3, 1 };
								UIControlColor[L"Words/PaintThicknessValue/words_color"] = { SET_ALPHA(brush.color, 255), 5, 1 };
							}
						}
						//画笔底部栏
						{
							{
								UIControl[L"Words/PaintBrush/size"] = { 18, 3, 1 };
								UIControl[L"Words/PaintBrush/left"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 20, 3, 1 };
								UIControl[L"Words/PaintBrush/top"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 5, 3, 1 };
								UIControl[L"Words/PaintBrush/width"] = { 65, 3, 1 };
								UIControl[L"Words/PaintBrush/height"] = { 33, 3, 1 };
								UIControlColor[L"Words/PaintBrush/words_color"] = { SET_ALPHA(brush.color,0), 5, 1 };
							}
							{
								UIControl[L"Words/FluorescentBrush/size"] = { 18, 3, 1 };
								UIControl[L"Words/FluorescentBrush/left"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 20, 3, 1 };
								UIControl[L"Words/FluorescentBrush/top"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 5, 3, 1 };
								UIControl[L"Words/FluorescentBrush/width"] = { 65, 3, 1 };
								UIControl[L"Words/FluorescentBrush/height"] = { 33, 3, 1 };
								UIControlColor[L"Words/FluorescentBrush/words_color"] = { SET_ALPHA(brush.color,0), 5, 1 };
							}

							{
								UIControl[L"Words/WriteBrush/size"] = { 18, 3, 1 };
								UIControl[L"Words/WriteBrush/left"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 20, 3, 1 };
								UIControl[L"Words/WriteBrush/top"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 5, 3, 1 };
								UIControl[L"Words/WriteBrush/width"] = { 65, 3, 1 };
								UIControl[L"Words/WriteBrush/height"] = { 33, 3, 1 };
								UIControlColor[L"Words/WriteBrush/words_color"] = { SET_ALPHA(brush.color,0), 5, 1 };
							}
							{
								UIControl[L"Words/LineBrush/size"] = { 18, 3, 1 };
								UIControl[L"Words/LineBrush/left"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 20, 3, 1 };
								UIControl[L"Words/LineBrush/top"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 5, 3, 1 };
								UIControl[L"Words/LineBrush/width"] = { 65, 3, 1 };
								UIControl[L"Words/LineBrush/height"] = { 33, 3, 1 };
								UIControlColor[L"Words/LineBrush/words_color"] = { SET_ALPHA(brush.color,0), 5, 1 };
							}
							{
								UIControl[L"Words/RectangleBrush/size"] = { 18, 3, 1 };
								UIControl[L"Words/RectangleBrush/left"] = { UIControl[L"RoundRect/BrushChoose/x"].v + 20, 3, 1 };
								UIControl[L"Words/RectangleBrush/top"] = { UIControl[L"RoundRect/BrushChoose/y"].v + 5, 3, 1 };
								UIControl[L"Words/RectangleBrush/width"] = { 65, 3, 1 };
								UIControl[L"Words/RectangleBrush/height"] = { 33, 3, 1 };
								UIControlColor[L"Words/RectangleBrush/words_color"] = { SET_ALPHA(brush.color,0), 5, 1 };
							}
						}
					}
					//橡皮
					{
						UIControl[L"Words/rubber/height"] = { 18, 5, 1 };
						UIControl[L"Words/rubber/left"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33, 5, 1 };
						UIControl[L"Words/rubber/top"] = { (float)floating_windows.height - 155 + 48, 5, 1 };
						UIControl[L"Words/rubber/right"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83, 5, 1 };
						UIControl[L"Words/rubber/bottom"] = { (float)floating_windows.height - 155 + 48 + 48, 5, 1 };
						UIControlColor[L"Words/rubber/words_color"] = { RGBA(98, 175, 82, 0), 5, 1 };
					}
					//程序调测
					{
						UIControl[L"Words/test/height"] = { 18, 5, 1 };
						UIControl[L"Words/test/left"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33, 5, 1 };
						UIControl[L"Words/test/top"] = { (float)floating_windows.height - 155 + 48, 5, 1 };
						UIControl[L"Words/test/right"] = { UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83, 5, 1 };
						UIControl[L"Words/test/bottom"] = { (float)floating_windows.height - 155 + 48 + 48, 5, 1 };
						UIControlColor[L"Words/test/words_color"] = { RGBA(98, 175, 82, 0), 5, 1 };
					}
				}
			}

			UIControlTarget = UIControl;
			UIControlColorTarget = UIControlColor;
		}
		//插件加载
		{
			if (_waccess((string_to_wstring(global_path) + L"plug-in\\随机点名\\随机点名.exe").c_str(), 4) == 0) plug_in_RandomRollCall.select = 1;
		}
	}

	//Testw(L"悬浮窗窗口绘制线程 初始化完成");
	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
	blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	HDC hdcScreen = GetDC(NULL);
	// 调用UpdateLayeredWindow函数更新窗口
	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { background.getwidth(),background.getheight() };
	POINT ptDst = { 0,0 }; // 设置窗口位置
	UPDATELAYEREDWINDOWINFO ulwi = { 0 };
	ulwi.cbSize = sizeof(ulwi);
	ulwi.hdcDst = hdcScreen;
	ulwi.pptDst = &ptDst;
	ulwi.psize = &sizeWnd;
	ulwi.pptSrc = &ptSrc;
	ulwi.crKey = RGB(255, 255, 255);
	ulwi.pblend = &blend;
	ulwi.dwFlags = ULW_ALPHA;

	do
	{
		Sleep(10);
		::SetWindowLong(floating_window, GWL_EXSTYLE, ::GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_LAYERED);
	} while (!(::GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_LAYERED));
	do
	{
		Sleep(10);
		::SetWindowLong(floating_window, GWL_EXSTYLE, ::GetWindowLong(floating_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
	} while (!(::GetWindowLong(floating_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE));

	graphics.SetSmoothingMode(SmoothingModeHighQuality);

	already = true;
	magnificationWindowReady++;

	//LOG(INFO) << "成功初始化悬浮窗窗口绘制模块";
	clock_t tRecord = clock();
	for (int for_num = 1; !off_signal; for_num = 2)
	{
		//UI计算部分
		{
			if ((int)state == 0)
			{
				//圆形
				{
					if (setlist.SkinMode == 1 || setlist.SkinMode == 2) UIControlColorTarget[L"Ellipse/Ellipse1/fill"].v = RGBA(0, 0, 0, 150);
					else if (setlist.SkinMode == 3) UIControlColorTarget[L"Ellipse/Ellipse1/fill"].v = RGBA(0, 0, 0, 180);

					if (!choose.select && !rubber.select) UIControlColorTarget[L"Ellipse/Ellipse1/frame"].v = brush.color;
					else
					{
						if (setlist.SkinMode == 1 || setlist.SkinMode == 2) UIControlColorTarget[L"Ellipse/Ellipse1/frame"].v = RGBA(255, 255, 225, 255);
						else if (setlist.SkinMode == 3) UIControlColorTarget[L"Ellipse/Ellipse1/frame"].v = RGBA(235, 151, 39, 255);
					}

					UIControlTarget[L"Image/Sign1/frame_transparency"].v = float(255);
				}
				//圆角矩形
				{
					{
						UIControlTarget[L"RoundRect/RoundRect1/x"].v = float(float(UIControl[L"Ellipse/Ellipse1/x"].v + 26));
						UIControlTarget[L"RoundRect/RoundRect1/y"].v = float(float(floating_windows.height - 155));
						UIControlTarget[L"RoundRect/RoundRect1/width"].v = float(30);
						UIControlTarget[L"RoundRect/RoundRect1/height"].v = float(94);

						if (BackgroundColorMode == 0)
						{
							UIControlColorTarget[L"RoundRect/RoundRect1/fill"].v = RGBA(255, 255, 255, 0);
							UIControlColorTarget[L"RoundRect/RoundRect1/frame"].v = RGBA(150, 150, 150, 0);
						}
						else if (BackgroundColorMode == 1)
						{
							UIControlColorTarget[L"RoundRect/RoundRect1/fill"].v = RGBA(30, 33, 41, 0);
							UIControlColorTarget[L"RoundRect/RoundRect1/frame"].v = RGBA(150, 150, 150, 0);
						}
					}
					{
						UIControlTarget[L"RoundRect/RoundRect2/x"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33);
						UIControlTarget[L"RoundRect/RoundRect2/y"].v = float(floating_windows.height - 156 + 8);
						UIControlTarget[L"RoundRect/RoundRect2/width"].v = float(80);
						UIControlTarget[L"RoundRect/RoundRect2/height"].v = float(80);
						UIControlTarget[L"RoundRect/RoundRect2/ellipseheight"].v = float(25);
						UIControlTarget[L"RoundRect/RoundRect2/ellipsewidth"].v = float(25);

						UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = SET_ALPHA(UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v, 0);
					}

					{
						UIControl[L"RoundRect/BrushTop/x"].s = float(5);

						UIControlTarget[L"RoundRect/BrushTop/x"].v = float(floating_windows.width - 48);
						UIControlTarget[L"RoundRect/BrushTop/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v);
						UIControlTarget[L"RoundRect/BrushTop/width"].v = float(80);
						UIControlTarget[L"RoundRect/BrushTop/height"].v = float(90);
						UIControlTarget[L"RoundRect/BrushTop/ellipseheight"].v = float(25);
						UIControlTarget[L"RoundRect/BrushTop/ellipsewidth"].v = float(25);

						if (BackgroundColorMode == 0)
						{
							UIControlColorTarget[L"RoundRect/BrushTop/fill"].v = RGBA(255, 255, 255, 0);
							UIControlColorTarget[L"RoundRect/BrushTop/frame"].v = RGBA(150, 150, 150, 0);
						}
						else if (BackgroundColorMode == 1)
						{
							UIControlColorTarget[L"RoundRect/BrushTop/fill"].v = RGBA(30, 33, 41, 0);
							UIControlColorTarget[L"RoundRect/BrushTop/frame"].v = RGBA(150, 150, 150, 0);
						}
					}
					{
						{
							UIControl[L"RoundRect/BrushColor1/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor1/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor1/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor1/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor1/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor1/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor1/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor1/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame1/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame1/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame1/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame1/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame1/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame1/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame1/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame1/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame1/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor1/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor1/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor1/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor1/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor2/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor2/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor2/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor2/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor2/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor2/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor2/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor2/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame2/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame2/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame2/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame2/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame2/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame2/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame2/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame2/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame2/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor2/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor2/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor2/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor2/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor3/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor3/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor3/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor3/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor3/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor3/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor3/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor3/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame3/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame3/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame3/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame3/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame3/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame3/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame3/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame3/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame3/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor3/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor3/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor3/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor3/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor4/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor4/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor4/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor4/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor4/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor4/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor4/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor4/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame4/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame4/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame4/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame4/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame4/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame4/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame4/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame4/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame4/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor4/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor4/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor4/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor4/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor5/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor5/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor5/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor5/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor5/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor5/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor5/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor5/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame5/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame5/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame5/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame5/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame5/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame5/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame5/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame5/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame5/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor5/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor5/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor5/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor5/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor6/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor6/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor6/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor6/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor6/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor6/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor6/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor6/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame6/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame6/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame6/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame6/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame6/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame6/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame6/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame6/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame6/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor6/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor6/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor6/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor6/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor7/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor7/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor7/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor7/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor7/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor7/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor7/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor7/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame7/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame7/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame7/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame7/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame7/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame7/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame7/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame7/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame7/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor7/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor7/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor7/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor7/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor8/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor8/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor8/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor8/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor8/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor8/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor8/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor8/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame8/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame8/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame8/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame8/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame8/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame8/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame8/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame8/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame8/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor8/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor8/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor8/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor8/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor9/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor9/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor9/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor9/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor9/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor9/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor9/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor9/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame9/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame9/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame9/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame9/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame9/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame9/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame9/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame9/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame9/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor9/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor9/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor9/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor9/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor10/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor10/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor10/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor10/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor10/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor10/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor10/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor10/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame10/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame10/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame10/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame10/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame10/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame10/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame10/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame10/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame10/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor10/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor10/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor10/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor10/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor11/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor11/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor11/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor11/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor11/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor11/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor11/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColor11/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame11/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame11/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame11/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame11/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame11/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame11/ellipseheight"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame11/ellipsewidth"].v = float(10);
							UIControlTarget[L"RoundRect/BrushColorFrame11/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame11/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"Image/BrushColor11/x"].s = float(5);
							UIControlTarget[L"Image/BrushColor11/x"].v = float(floating_windows.width - 48 + 10);
							UIControlTarget[L"Image/BrushColor11/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 30 + 10);
							UIControlTarget[L"Image/BrushColor11/transparency"].v = float(0);
						}
						{
							UIControl[L"RoundRect/BrushColor12/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColor12/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColor12/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColor12/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor12/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColor12/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorFrame12/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorFrame12/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorFrame12/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorFrame12/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame12/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame12/ellipseheight"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame12/ellipsewidth"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorFrame12/thickness"].v = float(1);
							UIControlColorTarget[L"RoundRect/BrushColorFrame12/frame"].v = RGBA(130, 130, 130, 0);
						}
					}
					{
						UIControl[L"RoundRect/PaintThickness/x"].s = float(5);

						UIControlTarget[L"RoundRect/PaintThickness/x"].v = float(floating_windows.width - 38);
						UIControlTarget[L"RoundRect/PaintThickness/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 10);
						UIControlTarget[L"RoundRect/PaintThickness/width"].v = float(60);
						UIControlTarget[L"RoundRect/PaintThickness/height"].v = float(70);
						UIControlTarget[L"RoundRect/PaintThickness/ellipseheight"].v = float(25);
						UIControlTarget[L"RoundRect/PaintThickness/ellipsewidth"].v = float(25);

						if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThickness/fill"].v = RGBA(230, 230, 230, 0);
						else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThickness/fill"].v = RGBA(70, 70, 70, 0);

						UIControl[L"RoundRect/PaintThicknessPrompt/x"].s = float(5);

						UIControlTarget[L"RoundRect/PaintThicknessPrompt/x"].v = float(floating_windows.width - 10);
						UIControlTarget[L"RoundRect/PaintThicknessPrompt/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 10);
						UIControlTarget[L"RoundRect/PaintThicknessPrompt/width"].v = float(5);
						UIControlTarget[L"RoundRect/PaintThicknessPrompt/height"].v = float(5);
						UIControlTarget[L"RoundRect/PaintThicknessPrompt/ellipseheight"].v = float(5);
						UIControlTarget[L"RoundRect/PaintThicknessPrompt/ellipsewidth"].v = float(5);

						UIControlColorTarget[L"RoundRect/PaintThicknessPrompt/fill"].v = SET_ALPHA(brush.color, 0);

						{
							UIControl[L"RoundRect/PaintThicknessAdjust/x"].s = float(5);

							UIControlTarget[L"RoundRect/PaintThicknessAdjust/x"].v = float(floating_windows.width - 38);
							UIControlTarget[L"RoundRect/PaintThicknessAdjust/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 10);
							UIControlTarget[L"RoundRect/PaintThicknessAdjust/width"].v = float(60);
							UIControlTarget[L"RoundRect/PaintThicknessAdjust/height"].v = float(70);
							UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(255, 255, 255, 0);
							else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(30, 33, 41, 0);
							UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/frame"].v = RGBA(150, 150, 150, 0);

							{
								UIControl[L"RoundRect/PaintThicknessSchedule1/x"].s = float(5);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule1/x"].v = float(floating_windows.width - 38);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule1/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 42);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule1/width"].v = float(60);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule1/height"].v = float(6);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipseheight"].v = float(6);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipsewidth"].v = float(6);
								if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(230, 230, 230, 0);
								else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(70, 70, 70, 0);

								UIControl[L"RoundRect/PaintThicknessSchedule2/x"].s = float(5);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule2/x"].v = float(floating_windows.width - 38);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule2/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 42);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule2/width"].v = float(60);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule2/height"].v = float(6);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipseheight"].v = float(6);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipsewidth"].v = float(6);
								UIControlColorTarget[L"RoundRect/PaintThicknessSchedule2/fill"].v = SET_ALPHA(brush.color, 0);

								UIControl[L"RoundRect/PaintThicknessSchedule3/x"].s = float(5);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule3/x"].v = float(floating_windows.width - 8 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule3/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2);
								UIControlColorTarget[L"RoundRect/PaintThicknessSchedule3/fill"].v = SET_ALPHA(brush.color, 0);

								UIControl[L"RoundRect/PaintThicknessSchedule4a/x"].s = float(5);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/x"].v = float(floating_windows.width - 8 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v = float(20);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/height"].v = float(20);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v = float(20);
								UIControlColorTarget[L"RoundRect/PaintThicknessSchedule4a/fill"].v = SET_ALPHA(brush.color, 0);

								UIControl[L"RoundRect/PaintThicknessSchedule5a/x"].s = float(5);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/x"].v = float(floating_windows.width - 8 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v = float(20);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/height"].v = float(20);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v = float(20);
								UIControlColorTarget[L"RoundRect/PaintThicknessSchedule5a/fill"].v = SET_ALPHA(brush.color, 0);

								UIControl[L"RoundRect/PaintThicknessSchedule6a/x"].s = float(5);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/x"].v = float(floating_windows.width - 8 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v / 2);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v / 2);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v = float(20);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/height"].v = float(20);
								UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v = float(20);
								UIControlColorTarget[L"RoundRect/PaintThicknessSchedule6a/fill"].v = SET_ALPHA(brush.color, 0);
							}
						}
					}
					{
						{
							UIControl[L"RoundRect/BrushColorChoose/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorChoose/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorChoose/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorChoose/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChoose/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChoose/ellipseheight"].v = float(5);
							UIControlTarget[L"RoundRect/BrushColorChoose/ellipsewidth"].v = float(5);

							if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(255, 255, 255, 0);
							else UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(30, 33, 41, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChoose/frame"].v = RGBA(130, 130, 130, 0);
						}
						{
							UIControl[L"RoundRect/BrushColorChooseWheel/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorChooseWheel/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseWheel/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseWheel/transparency"].v = float(0);

							UIControl[L"RoundRect/BrushColorChooseMark/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorChooseMark/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorChooseMark/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorChooseMark/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMark/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipseheight"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipsewidth"].v = float(40);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMark/fill"].v = SET_ALPHA(brush.color, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMark/frame"].v = RGBA(130, 130, 130, 0);

							UIControl[L"RoundRect/BrushColorChooseMarkR/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkR/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkR/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkR/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkR/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipseheight"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipsewidth"].v = float(40);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/fill"].v = RGBA(255, 0, 0, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/frame"].v = RGBA(255, 0, 0, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/text"].v = RGBA(255, 0, 0, 0);

							UIControl[L"RoundRect/BrushColorChooseMarkG/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkG/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkG/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkG/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkG/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipseheight"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipsewidth"].v = float(40);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/fill"].v = RGBA(0, 255, 0, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/frame"].v = RGBA(0, 255, 0, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/text"].v = RGBA(0, 255, 0, 0);

							UIControl[L"RoundRect/BrushColorChooseMarkB/x"].s = float(5);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkB/x"].v = float(floating_windows.width - 48 + 20);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkB/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkB/width"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkB/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipseheight"].v = float(40);
							UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipsewidth"].v = float(40);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/fill"].v = RGBA(0, 0, 255, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/frame"].v = RGBA(0, 0, 255, 0);
							UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/text"].v = RGBA(0, 0, 255, 0);
						}
					}

					{
						UIControl[L"RoundRect/BrushBottom/x"].s = float(5);

						UIControlTarget[L"RoundRect/BrushBottom/x"].v = float(floating_windows.width - 48);
						UIControlTarget[L"RoundRect/BrushBottom/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + UIControlTarget[L"RoundRect/RoundRect1/height"].v - 40);
						UIControlTarget[L"RoundRect/BrushBottom/width"].v = float(80);
						UIControlTarget[L"RoundRect/BrushBottom/height"].v = float(40);
						UIControlTarget[L"RoundRect/BrushBottom/ellipseheight"].v = float(25);
						UIControlTarget[L"RoundRect/BrushBottom/ellipsewidth"].v = float(25);

						if (BackgroundColorMode == 0)
						{
							UIControlColorTarget[L"RoundRect/BrushBottom/fill"].v = RGBA(255, 255, 255, 0);
							UIControlColorTarget[L"RoundRect/BrushBottom/frame"].v = RGBA(150, 150, 150, 0);
						}
						else if (BackgroundColorMode == 1)
						{
							UIControlColorTarget[L"RoundRect/BrushBottom/fill"].v = RGBA(30, 33, 41, 0);
							UIControlColorTarget[L"RoundRect/BrushBottom/frame"].v = RGBA(150, 150, 150, 0);
						}
					}
					{
						UIControl[L"RoundRect/BrushChoose/x"].s = float(5);

						UIControlTarget[L"RoundRect/BrushChoose/x"].v = float(floating_windows.width - 48);
						UIControlTarget[L"RoundRect/BrushChoose/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 35);
						UIControlTarget[L"RoundRect/BrushChoose/width"].v = float(70);
						UIControlTarget[L"RoundRect/BrushChoose/height"].v = float(30);
						UIControlTarget[L"RoundRect/BrushChoose/ellipseheight"].v = float(15);
						UIControlTarget[L"RoundRect/BrushChoose/ellipsewidth"].v = float(15);

						UIControlColorTarget[L"RoundRect/BrushChoose/frame"].v = SET_ALPHA(brush.color, 0);
					}
					{
						UIControl[L"RoundRect/BrushMode/x"].s = float(5);

						UIControlTarget[L"RoundRect/BrushMode/x"].v = float(floating_windows.width - 48);
						UIControlTarget[L"RoundRect/BrushMode/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 35);
						UIControlTarget[L"RoundRect/BrushMode/width"].v = float(70);
						UIControlTarget[L"RoundRect/BrushMode/height"].v = float(30);
						UIControlTarget[L"RoundRect/BrushMode/ellipseheight"].v = float(15);
						UIControlTarget[L"RoundRect/BrushMode/ellipsewidth"].v = float(15);

						UIControlColorTarget[L"RoundRect/BrushMode/frame"].v = SET_ALPHA(brush.color, 0);
					}
					{
						UIControl[L"RoundRect/BrushInterval/x"].s = float(5);

						UIControlTarget[L"RoundRect/BrushInterval/x"].v = float((float)floating_windows.width - 48 + 34);
						UIControlTarget[L"RoundRect/BrushInterval/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 40 + 10);
						UIControlTarget[L"RoundRect/BrushInterval/width"].v = float(3);
						UIControlTarget[L"RoundRect/BrushInterval/height"].v = float(20);
						UIControlColorTarget[L"RoundRect/BrushInterval/fill"].v = RGBA(150, 150, 150, 0);
					}
				}
				//图像
				{
					{
						UIControlTarget[L"Image/Sign1/transparency"].v = float(255);

						//if (UIControl[L"Image/Sign1/transparency"].v <= 150) UIControlTarget[L"Image/Sign1/transparency"].v = float(255);
						//else if (UIControl[L"Image/Sign1/transparency"].v >= 255) UIControlTarget[L"Image/Sign1/transparency"].v = float(150);
					}
					//选择
					{
						UIControlTarget[L"Image/choose/x"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 53);
						UIControlTarget[L"Image/choose/y"].v = float(floating_windows.height - 140);
						UIControlColorTarget[L"Image/choose/fill"].v = RGBA(98, 175, 82, 0);
					}
					//画笔
					{
						UIControlTarget[L"Image/brush/x"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 53);
						UIControlTarget[L"Image/brush/y"].v = float(floating_windows.height - 140);
						UIControlColorTarget[L"Image/brush/fill"].v = RGBA(130, 130, 130, 0);

						//画笔底部栏
						{
							{
								UIControl[L"Image/PaintBrush/x"].s = float(5);

								UIControlTarget[L"Image/PaintBrush/x"].v = float(floating_windows.width - 48 + 10);
								UIControlTarget[L"Image/PaintBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

								if (brush.mode == 1 || brush.mode == 3 || brush.mode == 4) UIControlColorTarget[L"Image/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Image/PaintBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
							{
								UIControl[L"Image/FluorescentBrush/x"].s = float(5);

								UIControlTarget[L"Image/FluorescentBrush/x"].v = float(floating_windows.width - 48 + 10);
								UIControlTarget[L"Image/FluorescentBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

								if (brush.mode == 2) UIControlColorTarget[L"Image/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Image/FluorescentBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}

							{
								UIControl[L"Image/WriteBrush/x"].s = float(5);

								UIControlTarget[L"Image/WriteBrush/x"].v = float(floating_windows.width - 48 + 10);
								UIControlTarget[L"Image/WriteBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

								if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Image/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Image/WriteBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
							{
								UIControl[L"Image/LineBrush/x"].s = float(5);

								UIControlTarget[L"Image/LineBrush/x"].v = float(floating_windows.width - 48 + 10);
								UIControlTarget[L"Image/LineBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

								if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Image/LineBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Image/LineBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
							{
								UIControl[L"Image/RectangleBrush/x"].s = float(5);

								UIControlTarget[L"Image/RectangleBrush/x"].v = float(floating_windows.width - 48 + 10);
								UIControlTarget[L"Image/RectangleBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

								if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Image/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Image/RectangleBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
						}
					}
					//橡皮
					{
						UIControlTarget[L"Image/rubber/x"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 53);
						UIControlTarget[L"Image/rubber/y"].v = float(floating_windows.height - 140);
						UIControlColorTarget[L"Image/rubber/fill"].v = RGBA(130, 130, 130, 0);
					}
					//程序调测
					{
						UIControlTarget[L"Image/test/x"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 53);
						UIControlTarget[L"Image/test/y"].v = float(floating_windows.height - 140);
						UIControlColorTarget[L"Image/test/fill"].v = RGBA(130, 130, 130, 0);
					}
				}
				//文字
				{
					//选择
					{
						UIControlTarget[L"Words/choose/height"].v = float(18);
						UIControlTarget[L"Words/choose/left"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33);
						UIControlTarget[L"Words/choose/top"].v = float(floating_windows.height - 155 + 48);
						UIControlTarget[L"Words/choose/right"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83);
						UIControlTarget[L"Words/choose/bottom"].v = float(floating_windows.height - 155 + 48 + 48);
						UIControlColorTarget[L"Words/choose/words_color"].v = SET_ALPHA(UIControlColorTarget[L"Words/choose/words_color"].v, 0);
					}
					//画笔
					{
						UIControlTarget[L"Words/brush/height"].v = float(18);
						UIControlTarget[L"Words/brush/left"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33);
						UIControlTarget[L"Words/brush/top"].v = float(floating_windows.height - 155 + 48);
						UIControlTarget[L"Words/brush/right"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83);
						UIControlTarget[L"Words/brush/bottom"].v = float(floating_windows.height - 155 + 48 + 48);
						UIControlColorTarget[L"Words/brush/words_color"].v = SET_ALPHA(UIControlColorTarget[L"Words/brush/words_color"].v, 0);

						{
							UIControlTarget[L"Words/brushSize/left"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 45);
							UIControlTarget[L"Words/brushSize/top"].v = float(floating_windows.height - 155 + 48 - 12);

							UIControlColorTarget[L"Words/brushSize/words_color"].v = SET_ALPHA(brush.color, 0);
						}

						//画笔顶部栏
						{
							//画笔粗细
							{
								UIControlTarget[L"Words/PaintThickness/left"].s = float(5);

								UIControlTarget[L"Words/PaintThickness/size"].v = float(20);
								UIControlTarget[L"Words/PaintThickness/left"].v = float(floating_windows.width - 48);
								UIControlTarget[L"Words/PaintThickness/top"].v = float(UIControlTarget[L"RoundRect/BrushTop/y"].v);
								UIControlTarget[L"Words/PaintThickness/width"].v = float(50);
								UIControlTarget[L"Words/PaintThickness/height"].v = float(96);
								UIControlColorTarget[L"Words/PaintThickness/words_color"].v = SET_ALPHA(brush.color, 255);

								UIControlTarget[L"Words/PaintThicknessValue/left"].s = float(5);

								UIControlTarget[L"Words/PaintThicknessValue/size"].v = float(20);
								UIControlTarget[L"Words/PaintThicknessValue/left"].v = float(floating_windows.width - 48);
								UIControlTarget[L"Words/PaintThicknessValue/top"].v = float(UIControlTarget[L"RoundRect/BrushTop/y"].v);
								UIControlTarget[L"Words/PaintThicknessValue/width"].v = float(50);
								UIControlTarget[L"Words/PaintThicknessValue/height"].v = float(96);
								UIControlColorTarget[L"Words/PaintThicknessValue/words_color"].v = SET_ALPHA(brush.color, 255);
							}
						}
						//画笔底部栏
						{
							{
								UIControlTarget[L"Words/PaintBrush/left"].s = float(5);

								UIControlTarget[L"Words/PaintBrush/size"].v = float(18);
								UIControlTarget[L"Words/PaintBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushChoose/x"].v + 20);
								UIControlTarget[L"Words/PaintBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushChoose/y"].v);
								UIControlTarget[L"Words/PaintBrush/width"].v = float(60);
								UIControlTarget[L"Words/PaintBrush/height"].v = float(33);

								if (brush.mode == 1 || brush.mode == 3 || brush.mode == 4) UIControlColorTarget[L"Words/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Words/PaintBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
							{
								UIControlTarget[L"Words/FluorescentBrush/left"].s = float(5);

								UIControlTarget[L"Words/FluorescentBrush/size"].v = float(18);
								UIControlTarget[L"Words/FluorescentBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushChoose/x"].v + 20);
								UIControlTarget[L"Words/FluorescentBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushChoose/y"].v);
								UIControlTarget[L"Words/FluorescentBrush/width"].v = float(65);
								UIControlTarget[L"Words/FluorescentBrush/height"].v = float(33);

								if (brush.mode == 2) UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}

							{
								UIControlTarget[L"Words/WriteBrush/left"].s = float(5);

								UIControlTarget[L"Words/WriteBrush/size"].v = float(18);
								UIControlTarget[L"Words/WriteBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushMode/x"].v + 20);
								UIControlTarget[L"Words/WriteBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushMode/y"].v);
								UIControlTarget[L"Words/WriteBrush/width"].v = float(60);
								UIControlTarget[L"Words/WriteBrush/height"].v = float(33);

								if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Words/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Words/WriteBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
							{
								UIControlTarget[L"Words/LineBrush/left"].s = float(5);

								UIControlTarget[L"Words/LineBrush/size"].v = float(18);
								UIControlTarget[L"Words/LineBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushMode/x"].v + 20);
								UIControlTarget[L"Words/LineBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushMode/y"].v);
								UIControlTarget[L"Words/LineBrush/width"].v = float(60);
								UIControlTarget[L"Words/LineBrush/height"].v = float(33);

								if (brush.mode == 3) UIControlColorTarget[L"Words/LineBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Words/LineBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
							{
								UIControlTarget[L"Words/RectangleBrush/left"].s = float(5);

								UIControlTarget[L"Words/RectangleBrush/size"].v = float(18);
								UIControlTarget[L"Words/RectangleBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushMode/x"].v + 20);
								UIControlTarget[L"Words/RectangleBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushMode/y"].v);
								UIControlTarget[L"Words/RectangleBrush/width"].v = float(60);
								UIControlTarget[L"Words/RectangleBrush/height"].v = float(33);

								if (brush.mode == 3) UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 0);
								else UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = RGBA(130, 130, 130, 0);
							}
						}
					}
					//橡皮
					{
						UIControlTarget[L"Words/rubber/height"].v = float(18);
						UIControlTarget[L"Words/rubber/left"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33);
						UIControlTarget[L"Words/rubber/top"].v = float(floating_windows.height - 155 + 48);
						UIControlTarget[L"Words/rubber/right"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83);
						UIControlTarget[L"Words/rubber/bottom"].v = float(floating_windows.height - 155 + 48 + 48);
						UIControlColorTarget[L"Words/rubber/words_color"].v = SET_ALPHA(UIControlColorTarget[L"Words/rubber/words_color"].v, 0);
					}
					//程序调测
					{
						UIControlTarget[L"Words/test/height"].v = float(18);
						UIControlTarget[L"Words/test/left"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33);
						UIControlTarget[L"Words/test/top"].v = float(floating_windows.height - 155 + 48);
						UIControlTarget[L"Words/test/right"].v = float(UIControl[L"Ellipse/Ellipse1/x"].v + 33 + 83);
						UIControlTarget[L"Words/test/bottom"].v = float(floating_windows.height - 155 + 48 + 48);
						UIControlColorTarget[L"Words/test/words_color"].v = SET_ALPHA(UIControlColorTarget[L"Words/test/words_color"].v, 0);
					}
				}
			}
			else if ((int)state == 1)
			{
				//圆形
				{
					UIControlColorTarget[L"Ellipse/Ellipse1/fill"].v = RGBA(0, 111, 225, 255);

					if (setlist.SkinMode == 1 || setlist.SkinMode == 2) UIControlColorTarget[L"Ellipse/Ellipse1/frame"].v = RGBA(0, 111, 225, 255);
					else if (setlist.SkinMode == 3)
					{
						if (!choose.select && !rubber.select) UIControlColorTarget[L"Ellipse/Ellipse1/frame"].v = brush.color;
						else UIControlColorTarget[L"Ellipse/Ellipse1/frame"].v = RGBA(235, 151, 39, 255);
					}

					UIControlTarget[L"Image/Sign1/frame_transparency"].v = float(255);
				}
				//圆角矩形
				{
					{
						UIControlTarget[L"RoundRect/RoundRect1/x"].v = float(1);
						UIControlTarget[L"RoundRect/RoundRect1/y"].v = float(floating_windows.height - 155);
						UIControlTarget[L"RoundRect/RoundRect1/width"].v = float(floating_windows.width - 48 + 8);
						UIControlTarget[L"RoundRect/RoundRect1/height"].v = float(94);

						if (BackgroundColorMode == 0)
						{
							UIControlColorTarget[L"RoundRect/RoundRect1/fill"].v = RGBA(255, 255, 255, 255);
							UIControlColorTarget[L"RoundRect/RoundRect1/frame"].v = RGBA(150, 150, 150, 255);
						}
						else if (BackgroundColorMode == 1)
						{
							UIControlColorTarget[L"RoundRect/RoundRect1/fill"].v = RGBA(30, 33, 41, 255);
							UIControlColorTarget[L"RoundRect/RoundRect1/frame"].v = RGBA(150, 150, 150, 255);
						}
					}
					{
						if (choose.select == true)
						{
							UIControlTarget[L"RoundRect/RoundRect2/x"].v = float(0 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/y"].v = float(floating_windows.height - 156 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/width"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/height"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/RoundRect2/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = RGBA(98, 175, 82, 255);
							else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = RGBA(98, 175, 82, 255);
						}
						else if (brush.select == true)
						{
							UIControlTarget[L"RoundRect/RoundRect2/x"].v = float(96 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/y"].v = float(floating_windows.height - 156 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/width"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/height"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/RoundRect2/ellipsewidth"].v = float(25);

							UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = SET_ALPHA(brush.color, 255);
						}
						else if (rubber.select == true)
						{
							UIControlTarget[L"RoundRect/RoundRect2/x"].v = float(192 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/y"].v = float(floating_windows.height - 156 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/width"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/height"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/RoundRect2/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = RGBA(98, 175, 82, 255);
							else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = RGBA(98, 175, 82, 255);
						}
						else if (test.select == true)
						{
							UIControlTarget[L"RoundRect/RoundRect2/x"].v = float(288 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/y"].v = float(floating_windows.height - 156 + 8);
							UIControlTarget[L"RoundRect/RoundRect2/width"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/height"].v = float(80);
							UIControlTarget[L"RoundRect/RoundRect2/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/RoundRect2/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = RGBA(98, 175, 82, 255);
							else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/RoundRect2/frame"].v = RGBA(98, 175, 82, 255);
						}
					}

					{
						UIControl[L"RoundRect/BrushTop/x"].s = float(3);

						if (state == 1.1 || state == 1.11 || state == 1.12)
						{
							UIControlTarget[L"RoundRect/BrushTop/x"].v = float(1);
							UIControlTarget[L"RoundRect/BrushTop/y"].v = float(floating_windows.height - 257);
							UIControlTarget[L"RoundRect/BrushTop/width"].v = float(floating_windows.width - 106);
							UIControlTarget[L"RoundRect/BrushTop/height"].v = float(96);
							UIControlTarget[L"RoundRect/BrushTop/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/BrushTop/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0)
							{
								UIControlColorTarget[L"RoundRect/BrushTop/fill"].v = RGBA(255, 255, 255, 255);
								UIControlColorTarget[L"RoundRect/BrushTop/frame"].v = RGBA(150, 150, 150, 255);
							}
							else if (BackgroundColorMode == 1)
							{
								UIControlColorTarget[L"RoundRect/BrushTop/fill"].v = RGBA(30, 33, 41, 255);
								UIControlColorTarget[L"RoundRect/BrushTop/frame"].v = RGBA(150, 150, 150, 255);
							}
						}
						else
						{
							UIControlTarget[L"RoundRect/BrushTop/x"].v = float(96 + 8);
							UIControlTarget[L"RoundRect/BrushTop/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v);
							UIControlTarget[L"RoundRect/BrushTop/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v);
							UIControlTarget[L"RoundRect/BrushTop/height"].v = float(90);
							UIControlTarget[L"RoundRect/BrushTop/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/BrushTop/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0)
							{
								UIControlColorTarget[L"RoundRect/BrushTop/fill"].v = RGBA(255, 255, 255, 0);
								UIControlColorTarget[L"RoundRect/BrushTop/frame"].v = RGBA(150, 150, 150, 0);
							}
							else if (BackgroundColorMode == 1)
							{
								UIControlColorTarget[L"RoundRect/BrushTop/fill"].v = RGBA(30, 33, 41, 0);
								UIControlColorTarget[L"RoundRect/BrushTop/frame"].v = RGBA(150, 150, 150, 0);
							}
						}
					}
					{
						{
							UIControl[L"RoundRect/BrushColor1/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor1/x"].v = float(1 + 10);
								UIControlTarget[L"RoundRect/BrushColor1/y"].v = float((floating_windows.height - 257 + 6));
								UIControlTarget[L"RoundRect/BrushColor1/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor1/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor1/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor1/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor1/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor1/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor1/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor1/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor1/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor1/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor1/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor1/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame1/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame1/x"].v = float(UIControlTarget[L"RoundRect/BrushColor1/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame1/y"].v = float(UIControlTarget[L"RoundRect/BrushColor1/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame1/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame1/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame1/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame1/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor1/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame1/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame1/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame1/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame1/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame1/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame1/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame1/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame1/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame1/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame1/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame1/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame1/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor1/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor1/x"].v = float(UIControlTarget[L"RoundRect/BrushColor1/x"].v + 10);
								UIControlTarget[L"Image/BrushColor1/y"].v = float(UIControlTarget[L"RoundRect/BrushColor1/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor1/fill"].v, 255)) UIControlTarget[L"Image/BrushColor1/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor1/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor1/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor1/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor1/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor2/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor2/x"].v = float(1 + 10);
								UIControlTarget[L"RoundRect/BrushColor2/y"].v = float((floating_windows.height - 257 + 6) + 44);
								UIControlTarget[L"RoundRect/BrushColor2/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor2/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor2/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor2/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor2/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor2/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor2/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor2/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor2/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor2/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor2/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor2/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame2/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame2/x"].v = float(UIControlTarget[L"RoundRect/BrushColor2/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame2/y"].v = float(UIControlTarget[L"RoundRect/BrushColor2/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame2/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame2/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame2/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame2/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor2/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame2/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame2/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame2/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame2/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame2/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame2/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame2/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame2/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame2/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame2/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame2/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame2/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor2/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor2/x"].v = float(UIControlTarget[L"RoundRect/BrushColor2/x"].v + 10);
								UIControlTarget[L"Image/BrushColor2/y"].v = float(UIControlTarget[L"RoundRect/BrushColor2/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor2/fill"].v, 255)) UIControlTarget[L"Image/BrushColor2/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor2/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor2/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor2/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor2/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor3/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor3/x"].v = float(11 + 44);
								UIControlTarget[L"RoundRect/BrushColor3/y"].v = float((floating_windows.height - 257 + 6));
								UIControlTarget[L"RoundRect/BrushColor3/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor3/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor3/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor3/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor3/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor3/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor3/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor3/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor3/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor3/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor3/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor3/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame3/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame3/x"].v = float(UIControlTarget[L"RoundRect/BrushColor3/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame3/y"].v = float(UIControlTarget[L"RoundRect/BrushColor3/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame3/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame3/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame3/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame3/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor3/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame3/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame3/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame3/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame3/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame3/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame3/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame3/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame3/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame3/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame3/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame3/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame3/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor3/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor3/x"].v = float(UIControlTarget[L"RoundRect/BrushColor3/x"].v + 10);
								UIControlTarget[L"Image/BrushColor3/y"].v = float(UIControlTarget[L"RoundRect/BrushColor3/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor3/fill"].v, 255)) UIControlTarget[L"Image/BrushColor3/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor3/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor3/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor3/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor3/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor4/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor4/x"].v = float(11 + 44);
								UIControlTarget[L"RoundRect/BrushColor4/y"].v = float((floating_windows.height - 257 + 6) + 44);
								UIControlTarget[L"RoundRect/BrushColor4/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor4/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor4/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor4/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor4/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor4/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor4/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor4/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor4/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor4/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor4/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor4/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame4/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame4/x"].v = float(UIControlTarget[L"RoundRect/BrushColor4/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame4/y"].v = float(UIControlTarget[L"RoundRect/BrushColor4/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame4/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame4/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame4/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame4/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor4/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame4/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame4/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame4/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame4/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame4/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame4/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame4/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame4/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame4/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame4/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame4/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame4/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor4/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor4/x"].v = float(UIControlTarget[L"RoundRect/BrushColor4/x"].v + 10);
								UIControlTarget[L"Image/BrushColor4/y"].v = float(UIControlTarget[L"RoundRect/BrushColor4/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor4/fill"].v, 255)) UIControlTarget[L"Image/BrushColor4/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor4/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor4/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor4/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor4/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor5/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor5/x"].v = float(11 + 44 * 2);
								UIControlTarget[L"RoundRect/BrushColor5/y"].v = float((floating_windows.height - 257 + 6));
								UIControlTarget[L"RoundRect/BrushColor5/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor5/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor5/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor5/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor5/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor5/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor5/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor5/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor5/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor5/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor5/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor5/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame5/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame5/x"].v = float(UIControlTarget[L"RoundRect/BrushColor5/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame5/y"].v = float(UIControlTarget[L"RoundRect/BrushColor5/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame5/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame5/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame5/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame5/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor5/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame5/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame5/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame5/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame5/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame5/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame5/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame5/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame5/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame5/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame5/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame5/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame5/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor5/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor5/x"].v = float(UIControlTarget[L"RoundRect/BrushColor5/x"].v + 10);
								UIControlTarget[L"Image/BrushColor5/y"].v = float(UIControlTarget[L"RoundRect/BrushColor5/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor5/fill"].v, 255)) UIControlTarget[L"Image/BrushColor5/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor5/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor5/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor5/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor5/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor6/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor6/x"].v = float(11 + 44 * 2);
								UIControlTarget[L"RoundRect/BrushColor6/y"].v = float((floating_windows.height - 257 + 6) + 44);
								UIControlTarget[L"RoundRect/BrushColor6/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor6/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor6/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor6/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor6/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor6/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor6/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor6/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor6/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor6/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor6/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor6/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame6/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame6/x"].v = float(UIControlTarget[L"RoundRect/BrushColor6/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame6/y"].v = float(UIControlTarget[L"RoundRect/BrushColor6/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame6/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame6/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame6/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame6/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor6/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame6/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame6/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame6/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame6/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame6/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame6/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame6/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame6/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame6/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame6/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame6/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame6/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor6/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor6/x"].v = float(UIControlTarget[L"RoundRect/BrushColor6/x"].v + 10);
								UIControlTarget[L"Image/BrushColor6/y"].v = float(UIControlTarget[L"RoundRect/BrushColor6/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor6/fill"].v, 255)) UIControlTarget[L"Image/BrushColor6/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor6/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor6/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor6/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor6/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor7/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor7/x"].v = float(11 + 44 * 3);
								UIControlTarget[L"RoundRect/BrushColor7/y"].v = float((floating_windows.height - 257 + 6));
								UIControlTarget[L"RoundRect/BrushColor7/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor7/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor7/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor7/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor7/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor7/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor7/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor7/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor7/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor7/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor7/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor7/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame7/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame7/x"].v = float(UIControlTarget[L"RoundRect/BrushColor7/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame7/y"].v = float(UIControlTarget[L"RoundRect/BrushColor7/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame7/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame7/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame7/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame7/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor7/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame7/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame7/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame7/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame7/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame7/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame7/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame7/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame7/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame7/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame7/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame7/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame7/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor7/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor7/x"].v = float(UIControlTarget[L"RoundRect/BrushColor7/x"].v + 10);
								UIControlTarget[L"Image/BrushColor7/y"].v = float(UIControlTarget[L"RoundRect/BrushColor7/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor7/fill"].v, 255)) UIControlTarget[L"Image/BrushColor7/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor7/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor7/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor7/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor7/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor8/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor8/x"].v = float(11 + 44 * 3);
								UIControlTarget[L"RoundRect/BrushColor8/y"].v = float((floating_windows.height - 257 + 6) + 44);
								UIControlTarget[L"RoundRect/BrushColor8/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor8/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor8/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor8/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor8/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor8/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor8/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor8/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor8/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor8/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor8/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor8/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame8/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame8/x"].v = float(UIControlTarget[L"RoundRect/BrushColor8/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame8/y"].v = float(UIControlTarget[L"RoundRect/BrushColor8/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame8/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame8/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame8/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame8/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor8/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame8/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame8/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame8/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame8/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame8/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame8/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame8/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame8/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame8/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame8/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame8/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame8/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor8/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor8/x"].v = float(UIControlTarget[L"RoundRect/BrushColor8/x"].v + 10);
								UIControlTarget[L"Image/BrushColor8/y"].v = float(UIControlTarget[L"RoundRect/BrushColor8/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor8/fill"].v, 255)) UIControlTarget[L"Image/BrushColor8/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor8/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor8/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor8/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor8/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor9/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor9/x"].v = float(11 + 44 * 4);
								UIControlTarget[L"RoundRect/BrushColor9/y"].v = float((floating_windows.height - 257 + 6));
								UIControlTarget[L"RoundRect/BrushColor9/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor9/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor9/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor9/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor9/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor9/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor9/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor9/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor9/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor9/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor9/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor9/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame9/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame9/x"].v = float(UIControlTarget[L"RoundRect/BrushColor9/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame9/y"].v = float(UIControlTarget[L"RoundRect/BrushColor9/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame9/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame9/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame9/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame9/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor9/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame9/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame9/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame9/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame9/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame9/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame9/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame9/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame9/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame9/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame9/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame9/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame9/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor9/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor9/x"].v = float(UIControlTarget[L"RoundRect/BrushColor9/x"].v + 10);
								UIControlTarget[L"Image/BrushColor9/y"].v = float(UIControlTarget[L"RoundRect/BrushColor9/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor9/fill"].v, 255)) UIControlTarget[L"Image/BrushColor9/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor9/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor9/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor9/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor9/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor10/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor10/x"].v = float(11 + 44 * 4);
								UIControlTarget[L"RoundRect/BrushColor10/y"].v = float((floating_windows.height - 257 + 6) + 44);
								UIControlTarget[L"RoundRect/BrushColor10/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor10/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor10/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor10/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor10/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor10/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor10/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor10/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor10/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor10/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor10/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor10/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame10/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame10/x"].v = float(UIControlTarget[L"RoundRect/BrushColor10/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame10/y"].v = float(UIControlTarget[L"RoundRect/BrushColor10/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame10/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame10/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame10/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame10/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor10/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame10/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame10/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame10/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame10/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame10/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame10/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame10/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame10/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame10/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame10/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame10/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame10/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor10/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor10/x"].v = float(UIControlTarget[L"RoundRect/BrushColor10/x"].v + 10);
								UIControlTarget[L"Image/BrushColor10/y"].v = float(UIControlTarget[L"RoundRect/BrushColor10/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor10/fill"].v, 255)) UIControlTarget[L"Image/BrushColor10/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor10/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor10/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor10/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor10/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor11/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor11/x"].v = float(11 + 44 * 5);
								UIControlTarget[L"RoundRect/BrushColor11/y"].v = float((floating_windows.height - 257 + 6));
								UIControlTarget[L"RoundRect/BrushColor11/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor11/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor11/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor11/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor11/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor11/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor11/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor11/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor11/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor11/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor11/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColor11/transparency"].v = float(0);
							}

							UIControl[L"RoundRect/BrushColorFrame11/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame11/x"].v = float(UIControlTarget[L"RoundRect/BrushColor11/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame11/y"].v = float(UIControlTarget[L"RoundRect/BrushColor11/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame11/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame11/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame11/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame11/ellipsewidth"].v = float(10);
								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor11/fill"].v, 255))
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame11/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame11/thickness"].v = float(3);
								}
								else
								{
									UIControlColorTarget[L"RoundRect/BrushColorFrame11/frame"].v = RGBA(130, 130, 130, 255);
									UIControlTarget[L"RoundRect/BrushColorFrame11/thickness"].v = float(1);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame11/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame11/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame11/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame11/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame11/ellipseheight"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame11/ellipsewidth"].v = float(10);
								UIControlTarget[L"RoundRect/BrushColorFrame11/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame11/frame"].v = RGBA(130, 130, 130, 0);
							}

							UIControl[L"Image/BrushColor11/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"Image/BrushColor11/x"].v = float(UIControlTarget[L"RoundRect/BrushColor11/x"].v + 10);
								UIControlTarget[L"Image/BrushColor11/y"].v = float(UIControlTarget[L"RoundRect/BrushColor11/y"].v + 10);

								if (SET_ALPHA(brush.color, 255) == SET_ALPHA((int)UIControlColor[L"RoundRect/BrushColor11/fill"].v, 255)) UIControlTarget[L"Image/BrushColor11/transparency"].v = float(255);
								else UIControlTarget[L"Image/BrushColor11/transparency"].v = float(0);
							}
							else
							{
								UIControlTarget[L"Image/BrushColor11/x"].v = float(96 + 8 + 20 + 10);
								UIControlTarget[L"Image/BrushColor11/y"].v = float(UIControl[L"RoundRect/RoundRect2/y"].v + 25 + 10);
								UIControlTarget[L"Image/BrushColor11/transparency"].v = float(0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColor12/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColor12/x"].v = float(11 + 44 * 5);
								UIControlTarget[L"RoundRect/BrushColor12/y"].v = float((floating_windows.height - 257 + 6) + 44);
								UIControlTarget[L"RoundRect/BrushColor12/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor12/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor12/transparency"].v = float(255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColor12/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColor12/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColor12/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor12/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColor12/transparency"].v = float(0);
							}
							UIControlTarget[L"RoundRect/BrushColor12/angle"].v = float(int(UIControlTarget[L"RoundRect/BrushColor12/angle"].v + 1) % 360);

							UIControl[L"RoundRect/BrushColorFrame12/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								UIControlTarget[L"RoundRect/BrushColorFrame12/x"].v = float(UIControlTarget[L"RoundRect/BrushColor12/x"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame12/y"].v = float(UIControlTarget[L"RoundRect/BrushColor12/y"].v);
								UIControlTarget[L"RoundRect/BrushColorFrame12/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame12/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame12/ellipseheight"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame12/ellipsewidth"].v = float(40);

								if (BrushColorChoose.x && BrushColorChoose.y) UIControlTarget[L"RoundRect/BrushColorFrame12/thickness"].v = float(3);
								else UIControlTarget[L"RoundRect/BrushColorFrame12/thickness"].v = float(1);

								UIControlColorTarget[L"RoundRect/BrushColorFrame12/frame"].v = RGBA(130, 130, 130, 255);
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorFrame12/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorFrame12/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorFrame12/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame12/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame12/ellipseheight"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame12/ellipsewidth"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorFrame12/thickness"].v = float(1);
								UIControlColorTarget[L"RoundRect/BrushColorFrame12/frame"].v = RGBA(130, 130, 130, 0);
							}
						}
					}
					{
						UIControl[L"RoundRect/PaintThickness/x"].s = float(3);

						if (state == 1.1 || state == 1.11 || state == 1.12)
						{
							UIControlTarget[L"RoundRect/PaintThickness/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360);
							UIControlTarget[L"RoundRect/PaintThickness/y"].v = float(floating_windows.height - 257 + 6);
							UIControlTarget[L"RoundRect/PaintThickness/width"].v = float(103);
							UIControlTarget[L"RoundRect/PaintThickness/height"].v = float(84);
							UIControlTarget[L"RoundRect/PaintThickness/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/PaintThickness/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThickness/fill"].v = RGBA(230, 230, 230, 255);
							else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThickness/fill"].v = RGBA(70, 70, 70, 255);
						}
						else
						{
							UIControlTarget[L"RoundRect/PaintThickness/x"].v = float(96 + 8 + 10);
							UIControlTarget[L"RoundRect/PaintThickness/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 10);
							UIControlTarget[L"RoundRect/PaintThickness/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v - 20);
							UIControlTarget[L"RoundRect/PaintThickness/height"].v = float(70);
							UIControlTarget[L"RoundRect/PaintThickness/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/PaintThickness/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThickness/fill"].v = RGBA(230, 230, 230, 0);
							else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThickness/fill"].v = RGBA(70, 70, 70, 0);
						}

						UIControl[L"RoundRect/PaintThicknessPrompt/x"].s = float(3);
						if (state == 1.1 || state == 1.11 || state == 1.12)
						{
							if (brush.width < 40)
							{
								UIControlTarget[L"RoundRect/PaintThicknessPrompt/x"].v = float(UIControlTarget[L"RoundRect/PaintThickness/x"].v + 32 + 35 - (brush.width + 1) / 2 - brush.width / 8 - 5);
								UIControlTarget[L"RoundRect/PaintThicknessPrompt/y"].v = float(UIControlTarget[L"RoundRect/PaintThickness/y"].v + 42 - (brush.width + 1) / 2 - brush.width / 5 - 5);
							}
							else
							{
								if (brush.width > 60)
								{
									UIControlTarget[L"RoundRect/PaintThicknessPrompt/x"].v = float(UIControlTarget[L"RoundRect/PaintThickness/x"].v + 32 + 35 - 30);
									UIControlTarget[L"RoundRect/PaintThicknessPrompt/y"].v = float(UIControlTarget[L"RoundRect/PaintThickness/y"].v + 42 - 30);
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessPrompt/x"].v = float(UIControlTarget[L"RoundRect/PaintThickness/x"].v + 32 + 35 - (brush.width + 1) / 2);
									UIControlTarget[L"RoundRect/PaintThicknessPrompt/y"].v = float(UIControlTarget[L"RoundRect/PaintThickness/y"].v + 42 - (brush.width + 1) / 2);
								}
							}
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/width"].v = float((brush.width + 1));
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/height"].v = float((brush.width + 1));
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/ellipseheight"].v = float((brush.width + 1));
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/ellipsewidth"].v = float((brush.width + 1));

							UIControlColorTarget[L"RoundRect/PaintThicknessPrompt/fill"].v = SET_ALPHA(brush.color, 255);
						}
						else
						{
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/x"].v = float(96 + 8 + (48 - min(45, brush.width / 2)) + 6);
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 10 + 35 - min(45, brush.width / 2) + 6);
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/width"].v = float(min(90, brush.width));
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/height"].v = float(min(90, brush.width));
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/ellipseheight"].v = float(min(90, brush.width));
							UIControlTarget[L"RoundRect/PaintThicknessPrompt/ellipsewidth"].v = float(min(90, brush.width));

							UIControlColorTarget[L"RoundRect/PaintThicknessPrompt/fill"].v = SET_ALPHA(brush.color, 0);
						}

						{
							UIControl[L"RoundRect/PaintThicknessAdjust/x"].s = float(3);

							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								if (state == 1.11)
								{
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/x"].v = float(1);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/y"].v = float(floating_windows.height - 312);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/width"].v = float(floating_windows.width - 106);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/height"].v = float(50);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipseheight"].v = float(25);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipsewidth"].v = float(25);

									if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(255, 255, 255, 255);
									else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(30, 33, 41, 255);
									UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/frame"].v = RGBA(150, 150, 150, 255);
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/y"].v = float(floating_windows.height - 257 + 6);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/width"].v = float(103);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/height"].v = float(84);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipseheight"].v = float(25);
									UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipsewidth"].v = float(25);

									if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(255, 255, 255, 0);
									else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(30, 33, 41, 0);
									UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/frame"].v = RGBA(150, 150, 150, 0);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/PaintThicknessAdjust/x"].v = float(96 + 8 + 10);
								UIControlTarget[L"RoundRect/PaintThicknessAdjust/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 10);
								UIControlTarget[L"RoundRect/PaintThicknessAdjust/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v - 20);
								UIControlTarget[L"RoundRect/PaintThicknessAdjust/height"].v = float(70);
								UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipseheight"].v = float(25);
								UIControlTarget[L"RoundRect/PaintThicknessAdjust/ellipsewidth"].v = float(25);

								if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(255, 255, 255, 0);
								else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/fill"].v = RGBA(30, 33, 41, 0);
								UIControlColorTarget[L"RoundRect/PaintThicknessAdjust/frame"].v = RGBA(150, 150, 150, 0);
							}

							{
								UIControl[L"RoundRect/PaintThicknessSchedule1/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.11)
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/x"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/y"].v = float(floating_windows.height - 312 + 22);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/width"].v = float(330);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/height"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipseheight"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipsewidth"].v = float(6);

										if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(230, 230, 230, 255);
										else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(70, 70, 70, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/y"].v = float(floating_windows.height - 257 + 6 + 39);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/width"].v = float(103);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/height"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipseheight"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipsewidth"].v = float(6);

										if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(230, 230, 230, 0);
										else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(70, 70, 70, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessSchedule1/x"].v = float(96 + 8 + 10);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule1/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 42);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule1/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v - 20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule1/height"].v = float(6);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipseheight"].v = float(6);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule1/ellipsewidth"].v = float(6);

									if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(230, 230, 230, 0);
									else if (BackgroundColorMode == 1) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule1/fill"].v = RGBA(70, 70, 70, 0);
								}

								UIControl[L"RoundRect/PaintThicknessSchedule2/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.11)
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/x"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/y"].v = float(floating_windows.height - 312 + 22);

										if (brush.width <= 50) UIControlTarget[L"RoundRect/PaintThicknessSchedule2/width"].v = float(10 + 190.0 * double(brush.width - 1) / 49.0);
										else if (brush.width <= 100) UIControlTarget[L"RoundRect/PaintThicknessSchedule2/width"].v = float(200.0 + 60.0 * double(brush.width - 51) / 49.0);
										else UIControlTarget[L"RoundRect/PaintThicknessSchedule2/width"].v = float(260.0 + 60.0 * double(brush.width - 101) / 399.0);

										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/height"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipseheight"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipsewidth"].v = float(6);

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule2/fill"].v = SET_ALPHA(brush.color, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/y"].v = float(floating_windows.height - 257 + 6 + 39);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/width"].v = float(103);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/height"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipseheight"].v = float(6);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipsewidth"].v = float(6);

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule2/fill"].v = SET_ALPHA(brush.color, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessSchedule2/x"].v = float(96 + 8 + 10);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule2/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 42);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule2/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v - 20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule2/height"].v = float(6);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipseheight"].v = float(6);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule2/ellipsewidth"].v = float(6);

									UIControlColorTarget[L"RoundRect/PaintThicknessSchedule2/fill"].v = SET_ALPHA(brush.color, 0);
								}

								UIControl[L"RoundRect/PaintThicknessSchedule3/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.11)
									{
										if (brush.width <= 50) UIControlTarget[L"RoundRect/PaintThicknessSchedule3/x"].v = float(30 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2 + 190.0 * double(brush.width - 1) / 49.0);
										else if (brush.width <= 100) UIControlTarget[L"RoundRect/PaintThicknessSchedule3/x"].v = float(20 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2 + 200.0 + 60.0 * double(brush.width - 51) / 49.0);
										else UIControlTarget[L"RoundRect/PaintThicknessSchedule3/x"].v = float(20 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2 + 260.0 + 60.0 * double(brush.width - 101) / 399.0);

										UIControlTarget[L"RoundRect/PaintThicknessSchedule3/y"].v = float(floating_windows.height - 312 + 25 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2);

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule3/fill"].v = SET_ALPHA(brush.color, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule3/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360 + 50 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule3/y"].v = float(floating_windows.height - 257 + 6 + 32 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2);

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule3/fill"].v = SET_ALPHA(brush.color, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessSchedule3/x"].v = float(96 + 8 + 48 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule3/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v / 2);

									UIControlColorTarget[L"RoundRect/PaintThicknessSchedule3/fill"].v = SET_ALPHA(brush.color, 0);
								}

								UIControl[L"RoundRect/PaintThicknessSchedule4a/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.11)
									{
										if (brush.mode == 2)
										{
											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/x"].v = float(385 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/y"].v = float(floating_windows.height - 312 + 25 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/height"].v / 2);

											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v = float(35);
											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/height"].v = float(35);
											if (UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v == UIControl[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v) UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v = float(35);
										}
										else
										{
											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/x"].v = float(380 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/y"].v = float(floating_windows.height - 312 + 25 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/height"].v / 2);

											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v = float(4);
											UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/height"].v = float(4);
											if (UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v == UIControl[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v) UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v = float(3);
										}

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule4a/fill"].v = SET_ALPHA(brush.color, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360 + 50 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/y"].v = float(floating_windows.height - 257 + 6 + 32 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/height"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v = float(20);

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule4a/fill"].v = SET_ALPHA(brush.color, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/x"].v = float(96 + 8 + 48 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v / 2);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/width"].v = float(20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/height"].v = float(20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v = float(20);

									UIControlColorTarget[L"RoundRect/PaintThicknessSchedule4a/fill"].v = SET_ALPHA(brush.color, 0);
								}

								UIControl[L"RoundRect/PaintThicknessSchedule5a/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.11)
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/x"].v = float(410 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/y"].v = float(floating_windows.height - 312 + 25 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v = float(10);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/height"].v = float(10);
										if (UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v == UIControl[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v) UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v = float(10);

										if (brush.mode == 2) UIControlColorTarget[L"RoundRect/PaintThicknessSchedule5a/fill"].v = SET_ALPHA(brush.color, 0);
										else UIControlColorTarget[L"RoundRect/PaintThicknessSchedule5a/fill"].v = SET_ALPHA(brush.color, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360 + 50 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/y"].v = float(floating_windows.height - 257 + 6 + 32 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/height"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v = float(20);

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule5a/fill"].v = SET_ALPHA(brush.color, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/x"].v = float(96 + 8 + 48 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v / 2);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/width"].v = float(20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/height"].v = float(20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v = float(20);

									UIControlColorTarget[L"RoundRect/PaintThicknessSchedule5a/fill"].v = SET_ALPHA(brush.color, 0);
								}

								UIControl[L"RoundRect/PaintThicknessSchedule6a/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.11)
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/x"].v = float(440 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/y"].v = float(floating_windows.height - 312 + 25 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/height"].v / 2);

										if (brush.mode == 2)
										{
											UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v = float(50);
											UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/height"].v = float(40);
											if (UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v == UIControl[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v) UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v = float(40);

											UIControlColorTarget[L"RoundRect/PaintThicknessSchedule6a/fill"].v = SET_ALPHA(brush.color, 255);
										}
										else
										{
											UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v = float(20);
											UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/height"].v = float(20);
											if (UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v == UIControl[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v) UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v = float(20);

											UIControlColorTarget[L"RoundRect/PaintThicknessSchedule6a/fill"].v = SET_ALPHA(brush.color, 255);
										}
									}
									else
									{
										UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/x"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 360 + 50 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/y"].v = float(floating_windows.height - 257 + 6 + 32 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v / 2);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/height"].v = float(20);
										UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v = float(20);

										UIControlColorTarget[L"RoundRect/PaintThicknessSchedule6a/fill"].v = SET_ALPHA(brush.color, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/x"].v = float(96 + 8 + 48 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v / 2);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + 35 - UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v / 2);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/width"].v = float(20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/height"].v = float(20);
									UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v = float(20);

									UIControlColorTarget[L"RoundRect/PaintThicknessSchedule6a/fill"].v = SET_ALPHA(brush.color, 0);
								}
							}
						}
					}
					{
						{
							UIControl[L"RoundRect/BrushColorChoose/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								if (state == 1.12)
								{
									UIControlTarget[L"RoundRect/BrushColorChoose/x"].v = float(11 + 44 * 5 - 120);
									UIControlTarget[L"RoundRect/BrushColorChoose/y"].v = float(floating_windows.height - 382);
									UIControlTarget[L"RoundRect/BrushColorChoose/width"].v = float(280);
									UIControlTarget[L"RoundRect/BrushColorChoose/height"].v = float(120);
									UIControlTarget[L"RoundRect/BrushColorChoose/ellipseheight"].v = float(25);
									UIControlTarget[L"RoundRect/BrushColorChoose/ellipsewidth"].v = float(25);

									if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(255, 255, 255, 255);
									else UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(30, 33, 41, 255);
									UIControlColorTarget[L"RoundRect/BrushColorChoose/frame"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"RoundRect/BrushColorChoose/x"].v = float(11 + 44 * 5);
									UIControlTarget[L"RoundRect/BrushColorChoose/y"].v = float((floating_windows.height - 257 + 6) + 44);
									UIControlTarget[L"RoundRect/BrushColorChoose/width"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChoose/height"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChoose/ellipseheight"].v = float(5);
									UIControlTarget[L"RoundRect/BrushColorChoose/ellipsewidth"].v = float(5);

									if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(255, 255, 255, 0);
									else UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(30, 33, 41, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChoose/frame"].v = RGBA(130, 130, 130, 0);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorChoose/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorChoose/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorChoose/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorChoose/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorChoose/ellipseheight"].v = float(5);
								UIControlTarget[L"RoundRect/BrushColorChoose/ellipsewidth"].v = float(5);

								if (BackgroundColorMode == 0) UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(255, 255, 255, 0);
								else UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v = RGBA(30, 33, 41, 0);
								UIControlColorTarget[L"RoundRect/BrushColorChoose/frame"].v = RGBA(130, 130, 130, 0);
							}
						}
						{
							UIControl[L"RoundRect/BrushColorChooseWheel/x"].s = float(3);
							if (state == 1.1 || state == 1.11 || state == 1.12)
							{
								if (state == 1.12)
								{
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v = float(11 + 44 * 5 - 110);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v = float(floating_windows.height - 372);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/width"].v = float(100);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/height"].v = float(100);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/transparency"].v = float(255);
								}
								else
								{
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v = float(11 + 44 * 5);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v = float((floating_windows.height - 257 + 6) + 44);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/width"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/height"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseWheel/transparency"].v = float(0);
								}
							}
							else
							{
								UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v = float(96 + 8 + 20);
								UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
								UIControlTarget[L"RoundRect/BrushColorChooseWheel/width"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorChooseWheel/height"].v = float(40);
								UIControlTarget[L"RoundRect/BrushColorChooseWheel/transparency"].v = float(0);
							}

							{
								UIControl[L"RoundRect/BrushColorChooseMark/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.12 && BrushColorChoose.x && BrushColorChoose.y)
									{
										if (BrushColorChoose.x == BrushColorChoose.last_x && BrushColorChoose.y == BrushColorChoose.last_y)
										{
											UIControlTarget[L"RoundRect/BrushColorChooseMark/x"].v = float(BrushColorChoose.x + UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v - 7);
											UIControlTarget[L"RoundRect/BrushColorChooseMark/y"].v = float(BrushColorChoose.y + UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v - 7);
										}
										UIControlTarget[L"RoundRect/BrushColorChooseMark/width"].v = float(15);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/height"].v = float(15);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipseheight"].v = float(15);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipsewidth"].v = float(15);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMark/fill"].v = SET_ALPHA(brush.color, 255);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMark/frame"].v = RGBA(130, 130, 130, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/BrushColorChooseMark/x"].v = float(11 + 44 * 5);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/y"].v = float((floating_windows.height - 257 + 6) + 44);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/width"].v = float(15);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/height"].v = float(15);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipseheight"].v = float(15);
										UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipsewidth"].v = float(15);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMark/fill"].v = SET_ALPHA(brush.color, 0);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMark/frame"].v = RGBA(130, 130, 130, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/BrushColorChooseMark/x"].v = float(96 + 8 + 20);
									UIControlTarget[L"RoundRect/BrushColorChooseMark/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
									UIControlTarget[L"RoundRect/BrushColorChooseMark/width"].v = float(15);
									UIControlTarget[L"RoundRect/BrushColorChooseMark/height"].v = float(15);
									UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipseheight"].v = float(15);
									UIControlTarget[L"RoundRect/BrushColorChooseMark/ellipsewidth"].v = float(15);

									UIControlColorTarget[L"RoundRect/BrushColorChooseMark/fill"].v = SET_ALPHA(brush.color, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChooseMark/frame"].v = RGBA(130, 130, 130, 0);
								}

								UIControl[L"RoundRect/BrushColorChooseMarkR/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.12)
									{
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/x"].v = float(11 + 44 * 5);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/y"].v = float(floating_windows.height - 372);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/width"].v = float(45);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/height"].v = float(35);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipseheight"].v = float(20);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipsewidth"].v = float(20);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/fill"].v = RGBA(255, 0, 0, GetRValue(brush.color));
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/frame"].v = RGBA(255, 0, 0, 255);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/text"].v = GetRValue(brush.color) <= 127 ? RGBA(255, 0, 0, 255) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/x"].v = float(11 + 44 * 5);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/y"].v = float((floating_windows.height - 257 + 6) + 44);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/width"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/height"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipseheight"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipsewidth"].v = float(40);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/fill"].v = RGBA(255, 0, 0, 0);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/frame"].v = RGBA(255, 0, 0, 0);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/text"].v = GetRValue(brush.color) <= 127 ? RGBA(255, 0, 0, 0) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/BrushColorChooseMarkR/x"].v = float(96 + 8 + 20);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkR/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkR/width"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkR/height"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipseheight"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkR/ellipsewidth"].v = float(40);

									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/fill"].v = RGBA(255, 0, 0, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/frame"].v = RGBA(255, 0, 0, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkR/text"].v = GetRValue(brush.color) <= 127 ? RGBA(255, 0, 0, 0) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 0);
								}

								UIControl[L"RoundRect/BrushColorChooseMarkG/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.12)
									{
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/x"].v = float(11 + 44 * 5 + 52);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/y"].v = float(floating_windows.height - 372);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/width"].v = float(45);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/height"].v = float(35);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipseheight"].v = float(20);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipsewidth"].v = float(20);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/fill"].v = RGBA(0, 255, 0, GetGValue(brush.color));
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/frame"].v = RGBA(0, 255, 0, 255);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/text"].v = GetGValue(brush.color) <= 127 ? RGBA(0, 255, 0, 255) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/x"].v = float(11 + 44 * 5);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/y"].v = float((floating_windows.height - 257 + 6) + 44);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/width"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/height"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipseheight"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipsewidth"].v = float(40);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/fill"].v = RGBA(0, 255, 0, 0);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/frame"].v = RGBA(0, 255, 0, 0);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/text"].v = GetGValue(brush.color) <= 127 ? RGBA(0, 255, 0, 0) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/BrushColorChooseMarkG/x"].v = float(96 + 8 + 20);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkG/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkG/width"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkG/height"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipseheight"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkG/ellipsewidth"].v = float(40);

									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/fill"].v = RGBA(0, 255, 0, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/frame"].v = RGBA(0, 255, 0, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkG/text"].v = GetGValue(brush.color) <= 127 ? RGBA(0, 255, 0, 0) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 0);
								}

								UIControl[L"RoundRect/BrushColorChooseMarkB/x"].s = float(3);
								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									if (state == 1.12)
									{
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/x"].v = float(11 + 44 * 5 + 52 * 2);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/y"].v = float(floating_windows.height - 372);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/width"].v = float(45);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/height"].v = float(35);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipseheight"].v = float(20);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipsewidth"].v = float(20);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/fill"].v = RGBA(0, 0, 255, GetBValue(brush.color));
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/frame"].v = RGBA(0, 0, 255, 255);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/text"].v = GetBValue(brush.color) <= 127 ? RGBA(0, 0, 255, 255) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 255);
									}
									else
									{
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/x"].v = float(11 + 44 * 5);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/y"].v = float((floating_windows.height - 257 + 6) + 44);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/width"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/height"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipseheight"].v = float(40);
										UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipsewidth"].v = float(40);

										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/fill"].v = RGBA(0, 0, 255, 0);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/frame"].v = RGBA(0, 0, 255, 0);
										UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/text"].v = GetBValue(brush.color) <= 127 ? RGBA(0, 0, 255, 0) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 0);
									}
								}
								else
								{
									UIControlTarget[L"RoundRect/BrushColorChooseMarkB/x"].v = float(96 + 8 + 20);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkB/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + 25);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkB/width"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkB/height"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipseheight"].v = float(40);
									UIControlTarget[L"RoundRect/BrushColorChooseMarkB/ellipsewidth"].v = float(40);

									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/fill"].v = RGBA(0, 0, 255, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/frame"].v = RGBA(0, 0, 255, 0);
									UIControlColorTarget[L"RoundRect/BrushColorChooseMarkB/text"].v = GetBValue(brush.color) <= 127 ? RGBA(0, 0, 255, 0) : SET_ALPHA(UIControlColorTarget[L"RoundRect/BrushColorChoose/fill"].v, 0);
								}
							}
						}
					}

					{
						UIControl[L"RoundRect/BrushBottom/x"].s = float(3);

						if (state == 1.1 || state == 1.11 || state == 1.12)
						{
							UIControlTarget[L"RoundRect/BrushBottom/x"].v = float(1);
							UIControlTarget[L"RoundRect/BrushBottom/y"].v = float(floating_windows.height - 55);
							UIControlTarget[L"RoundRect/BrushBottom/width"].v = float(floating_windows.width - 106);
							UIControlTarget[L"RoundRect/BrushBottom/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushBottom/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/BrushBottom/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0)
							{
								UIControlColorTarget[L"RoundRect/BrushBottom/fill"].v = RGBA(255, 255, 255, 255);
								UIControlColorTarget[L"RoundRect/BrushBottom/frame"].v = RGBA(150, 150, 150, 255);
							}
							else if (BackgroundColorMode == 1)
							{
								UIControlColorTarget[L"RoundRect/BrushBottom/fill"].v = RGBA(30, 33, 41, 255);
								UIControlColorTarget[L"RoundRect/BrushBottom/frame"].v = RGBA(150, 150, 150, 255);
							}
						}
						else
						{
							UIControlTarget[L"RoundRect/BrushBottom/x"].v = float(96 + 8);
							UIControlTarget[L"RoundRect/BrushBottom/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + UIControlTarget[L"RoundRect/RoundRect1/height"].v - 40);
							UIControlTarget[L"RoundRect/BrushBottom/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v);
							UIControlTarget[L"RoundRect/BrushBottom/height"].v = float(40);
							UIControlTarget[L"RoundRect/BrushBottom/ellipseheight"].v = float(25);
							UIControlTarget[L"RoundRect/BrushBottom/ellipsewidth"].v = float(25);

							if (BackgroundColorMode == 0)
							{
								UIControlColorTarget[L"RoundRect/BrushBottom/fill"].v = RGBA(255, 255, 255, 0);
								UIControlColorTarget[L"RoundRect/BrushBottom/frame"].v = RGBA(150, 150, 150, 0);
							}
							else if (BackgroundColorMode == 1)
							{
								UIControlColorTarget[L"RoundRect/BrushBottom/fill"].v = RGBA(30, 33, 41, 0);
								UIControlColorTarget[L"RoundRect/BrushBottom/frame"].v = RGBA(150, 150, 150, 0);
							}
						}
					}
					{
						UIControl[L"RoundRect/BrushChoose/x"].s = float(3);

						if (state == 1.1 || state == 1.11 || state == 1.12)
						{
							if (brush.mode == 2) UIControlTarget[L"RoundRect/BrushChoose/x"].v = float(95);
							else UIControlTarget[L"RoundRect/BrushChoose/x"].v = float(5);

							UIControlTarget[L"RoundRect/BrushChoose/y"].v = float(floating_windows.height - 55 + 5);
							UIControlTarget[L"RoundRect/BrushChoose/width"].v = float(90);
							UIControlTarget[L"RoundRect/BrushChoose/height"].v = float(30);
							UIControlTarget[L"RoundRect/BrushChoose/ellipseheight"].v = float(15);
							UIControlTarget[L"RoundRect/BrushChoose/ellipsewidth"].v = float(15);

							UIControlColorTarget[L"RoundRect/BrushChoose/frame"].v = SET_ALPHA(brush.color, 255);
						}
						else
						{
							UIControlTarget[L"RoundRect/BrushChoose/x"].v = float(96 + 8 + 5);
							UIControlTarget[L"RoundRect/BrushChoose/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + UIControlTarget[L"RoundRect/RoundRect1/height"].v - 35);
							UIControlTarget[L"RoundRect/BrushChoose/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v - 10);
							UIControlTarget[L"RoundRect/BrushChoose/height"].v = float(30);
							UIControlTarget[L"RoundRect/BrushChoose/ellipseheight"].v = float(15);
							UIControlTarget[L"RoundRect/BrushChoose/ellipsewidth"].v = float(15);

							UIControlColorTarget[L"RoundRect/BrushChoose/frame"].v = SET_ALPHA(brush.color, 0);
						}
					}
					{
						UIControl[L"RoundRect/BrushMode/x"].s = float(3);

						if (state == 1.1 || state == 1.11 || state == 1.12)
						{
							if (brush.mode == 3) UIControlTarget[L"RoundRect/BrushMode/x"].v = float(285);
							else if (brush.mode == 4) UIControlTarget[L"RoundRect/BrushMode/x"].v = float(375);
							else UIControlTarget[L"RoundRect/BrushMode/x"].v = float(195);

							UIControlTarget[L"RoundRect/BrushMode/y"].v = float(floating_windows.height - 55 + 5);
							UIControlTarget[L"RoundRect/BrushMode/width"].v = float(90);
							UIControlTarget[L"RoundRect/BrushMode/height"].v = float(30);
							UIControlTarget[L"RoundRect/BrushMode/ellipseheight"].v = float(15);
							UIControlTarget[L"RoundRect/BrushMode/ellipsewidth"].v = float(15);

							UIControlColorTarget[L"RoundRect/BrushMode/frame"].v = SET_ALPHA(brush.color, 255);
						}
						else
						{
							UIControlTarget[L"RoundRect/BrushMode/x"].v = float(96 + 8 + 5);
							UIControlTarget[L"RoundRect/BrushMode/y"].v = float(UIControlTarget[L"RoundRect/RoundRect1/y"].v + UIControlTarget[L"RoundRect/RoundRect1/height"].v - 35);
							UIControlTarget[L"RoundRect/BrushMode/width"].v = float(UIControl[L"RoundRect/RoundRect2/width"].v - 10);
							UIControlTarget[L"RoundRect/BrushMode/height"].v = float(30);
							UIControlTarget[L"RoundRect/BrushMode/ellipseheight"].v = float(15);
							UIControlTarget[L"RoundRect/BrushMode/ellipsewidth"].v = float(15);

							UIControlColorTarget[L"RoundRect/BrushMode/frame"].v = SET_ALPHA(brush.color, 0);
						}
					}
					{
						UIControl[L"RoundRect/BrushInterval/x"].s = float(3);

						if (state == 1.1 || state == 1.11 || state == 1.12)
						{
							UIControlTarget[L"RoundRect/BrushInterval/x"].v = float(189);
							UIControlTarget[L"RoundRect/BrushInterval/y"].v = float(floating_windows.height - 55 + 10);
							UIControlTarget[L"RoundRect/BrushInterval/width"].v = float(3);
							UIControlTarget[L"RoundRect/BrushInterval/height"].v = float(20);
							UIControlColorTarget[L"RoundRect/BrushInterval/fill"].v = RGBA(150, 150, 150, 255);
						}
						else
						{
							UIControlTarget[L"RoundRect/BrushInterval/x"].v = float(UIControlTarget[L"RoundRect/BrushChoose/x"].v + 34);
							UIControlTarget[L"RoundRect/BrushInterval/y"].v = float(UIControl[L"RoundRect/RoundRect1/y"].v + UIControl[L"RoundRect/RoundRect1/height"].v - 40 + 10);
							UIControlTarget[L"RoundRect/BrushInterval/width"].v = float(3);
							UIControlTarget[L"RoundRect/BrushInterval/height"].v = float(20);
							UIControlColorTarget[L"RoundRect/BrushInterval/fill"].v = RGBA(150, 150, 150, 0);
						}
					}
				}
				//图像
				{
					{
						UIControlTarget[L"Image/Sign1/transparency"].v = float(255);
					}
					//选择
					{
						UIControlTarget[L"Image/choose/x"].v = float(0 + 28);
						UIControlTarget[L"Image/choose/y"].v = float(floating_windows.height - 140);
						if (choose.select) UIControlColorTarget[L"Image/choose/fill"].v = RGBA(98, 175, 82, 255);
						else UIControlColorTarget[L"Image/choose/fill"].v = RGBA(130, 130, 130, 255);
					}
					//画笔
					{
						if (brush.width >= 100 && brush.select == true) UIControlTarget[L"Image/brush/x"].v = float(96 + 23);
						else UIControlTarget[L"Image/brush/x"].v = float(96 + 28);
						UIControlTarget[L"Image/brush/y"].v = float(floating_windows.height - 140);

						if (brush.select == true) UIControlColorTarget[L"Image/brush/fill"].v = SET_ALPHA(brush.color, 255);
						else UIControlColorTarget[L"Image/brush/fill"].v = RGBA(130, 130, 130, 255);

						//画笔底部栏
						{
							{
								UIControl[L"Image/PaintBrush/x"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Image/PaintBrush/x"].v = float(5 + 10);
									UIControlTarget[L"Image/PaintBrush/y"].v = float(floating_windows.height - 55 + 10);

									if (brush.mode == 1 || brush.mode == 3 || brush.mode == 4) UIControlColorTarget[L"Image/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Image/PaintBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Image/PaintBrush/x"].v = float(UIControlTarget[L"RoundRect/BrushBottom/x"].v + 10);
									UIControlTarget[L"Image/PaintBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

									if (brush.mode == 1 || brush.mode == 3 || brush.mode == 4) UIControlColorTarget[L"Image/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Image/PaintBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
							{
								UIControl[L"Image/FluorescentBrush/x"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Image/FluorescentBrush/x"].v = float(95 + 5);
									UIControlTarget[L"Image/FluorescentBrush/y"].v = float(floating_windows.height - 55 + 10);

									if (brush.mode == 2) UIControlColorTarget[L"Image/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Image/FluorescentBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Image/FluorescentBrush/x"].v = float(UIControlTarget[L"RoundRect/BrushBottom/x"].v + 10);
									UIControlTarget[L"Image/FluorescentBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

									if (brush.mode == 2) UIControlColorTarget[L"Image/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Image/FluorescentBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}

							{
								UIControl[L"Image/WriteBrush/x"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Image/WriteBrush/x"].v = float(195 + 10);
									UIControlTarget[L"Image/WriteBrush/y"].v = float(floating_windows.height - 55 + 10);

									if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Image/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Image/WriteBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Image/WriteBrush/x"].v = float(UIControlTarget[L"RoundRect/BrushBottom/x"].v + 10);
									UIControlTarget[L"Image/WriteBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

									if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Image/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Image/WriteBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
							{
								UIControl[L"Image/LineBrush/x"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Image/LineBrush/x"].v = float(285 + 10);
									UIControlTarget[L"Image/LineBrush/y"].v = float(floating_windows.height - 55 + 10);

									if (brush.mode == 3) UIControlColorTarget[L"Image/LineBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Image/LineBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Image/LineBrush/x"].v = float(UIControlTarget[L"RoundRect/BrushBottom/x"].v + 10);
									UIControlTarget[L"Image/LineBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

									if (brush.mode == 3) UIControlColorTarget[L"Image/LineBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Image/LineBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
							{
								UIControl[L"Image/RectangleBrush/x"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Image/RectangleBrush/x"].v = float(375 + 10);
									UIControlTarget[L"Image/RectangleBrush/y"].v = float(floating_windows.height - 55 + 10);

									if (brush.mode == 4) UIControlColorTarget[L"Image/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Image/RectangleBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Image/RectangleBrush/x"].v = float(UIControlTarget[L"RoundRect/BrushBottom/x"].v + 10);
									UIControlTarget[L"Image/RectangleBrush/y"].v = float(UIControlTarget[L"RoundRect/BrushBottom/y"].v + 10);

									if (brush.mode == 4) UIControlColorTarget[L"Image/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Image/RectangleBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
						}
					}
					//橡皮
					{
						UIControlTarget[L"Image/rubber/x"].v = float(192 + 28);
						UIControlTarget[L"Image/rubber/y"].v = float(floating_windows.height - 140);
						if (rubber.select) UIControlColorTarget[L"Image/rubber/fill"].v = RGBA(98, 175, 82, 255);
						else UIControlColorTarget[L"Image/rubber/fill"].v = RGBA(130, 130, 130, 255);
					}
					//程序调测
					{
						UIControlTarget[L"Image/test/x"].v = float(288 + 28);
						UIControlTarget[L"Image/test/y"].v = float(floating_windows.height - 140);
						if (test.select) UIControlColorTarget[L"Image/test/fill"].v = RGBA(98, 175, 82, 255);
						else UIControlColorTarget[L"Image/test/fill"].v = RGBA(130, 130, 130, 255);
					}
				}
				//文字
				{
					//选择
					{
						UIControlTarget[L"Words/choose/height"].v = float(18);
						UIControlTarget[L"Words/choose/top"].v = float(floating_windows.height - 155 + 48);
						if (choose.select)
						{
							UIControlTarget[L"Words/choose/left"].v = float(0 + 14);
							UIControlTarget[L"Words/choose/right"].v = float(0 + 0 + 83);
						}
						else
						{
							UIControlTarget[L"Words/choose/left"].v = float(-16);
							UIControlTarget[L"Words/choose/right"].v = float(0 + 32 + 83);
						}
						UIControlTarget[L"Words/choose/bottom"].v = float(floating_windows.height - 155 + 48 + 48);

						if (choose.select) UIControlColorTarget[L"Words/choose/words_color"].v = RGBA(98, 175, 82, 255);
						else UIControlColorTarget[L"Words/choose/words_color"].v = RGBA(130, 130, 130, 255);
					}
					//画笔
					{
						UIControlTarget[L"Words/brush/height"].v = float(18);
						UIControlTarget[L"Words/brush/left"].v = float(96 + 7);
						UIControlTarget[L"Words/brush/top"].v = float(floating_windows.height - 155 + 48);
						UIControlTarget[L"Words/brush/right"].v = float(96 + 7 + 83);
						UIControlTarget[L"Words/brush/bottom"].v = float(floating_windows.height - 155 + 48 + 48);
						if (brush.select) UIControlColorTarget[L"Words/brush/words_color"].v = SET_ALPHA(brush.color, 255);
						else UIControlColorTarget[L"Words/brush/words_color"].v = RGBA(130, 130, 130, 255);

						{
							if (brush.width >= 100) UIControlTarget[L"Words/brushSize/left"].v = float(96 + 7 + 40);
							else UIControlTarget[L"Words/brushSize/left"].v = float(96 + 7 + 45);
							UIControlTarget[L"Words/brushSize/top"].v = float(floating_windows.height - 156 + 35);

							if (brush.select) UIControlColorTarget[L"Words/brushSize/words_color"].v = SET_ALPHA(brush.color, 255);
							else UIControlColorTarget[L"Words/brushSize/words_color"].v = SET_ALPHA(brush.color, 0);
						}

						//画笔顶部栏
						{
							//画笔粗细
							{
								UIControlTarget[L"Words/PaintThickness/left"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Words/PaintThickness/size"].v = float(20);
									UIControlTarget[L"Words/PaintThickness/left"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 365);
									UIControlTarget[L"Words/PaintThickness/top"].v = float(UIControlTarget[L"RoundRect/BrushTop/y"].v);
									UIControlTarget[L"Words/PaintThickness/width"].v = float(35);
									UIControlTarget[L"Words/PaintThickness/height"].v = float(100);
									UIControlColorTarget[L"Words/PaintThickness/words_color"].v = SET_ALPHA(brush.color, 255);
								}
								else
								{
									UIControlTarget[L"Words/PaintThickness/size"].v = float(20);
									UIControlTarget[L"Words/PaintThickness/left"].v = float(96 + 8 + 15);
									UIControlTarget[L"Words/PaintThickness/top"].v = float(UIControlTarget[L"RoundRect/BrushTop/y"].v);
									UIControlTarget[L"Words/PaintThickness/width"].v = float(50);
									UIControlTarget[L"Words/PaintThickness/height"].v = float(100);
									UIControlColorTarget[L"Words/PaintThickness/words_color"].v = SET_ALPHA(brush.color, 255);
								}

								UIControlTarget[L"Words/PaintThicknessValue/left"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Words/PaintThicknessValue/size"].v = float(20);
									if (brush.width < 40)
									{
										UIControlTarget[L"Words/PaintThicknessValue/left"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 390 + brush.width / 6 + 5);
										UIControlTarget[L"Words/PaintThicknessValue/top"].v = float(UIControlTarget[L"RoundRect/BrushTop/y"].v + brush.width / 3 + 10);

										UIControlColorTarget[L"Words/PaintThicknessValue/words_color"].v = SET_ALPHA(brush.color, 255);
									}
									else
									{
										UIControlTarget[L"Words/PaintThicknessValue/left"].v = float(UIControlTarget[L"RoundRect/BrushTop/x"].v + 390);
										UIControlTarget[L"Words/PaintThicknessValue/top"].v = float(UIControlTarget[L"RoundRect/BrushTop/y"].v);

										if (BackgroundColorMode == 0) UIControlColorTarget[L"Words/PaintThicknessValue/words_color"].v = RGBA(255, 255, 255, 255);
										if (BackgroundColorMode == 1) UIControlColorTarget[L"Words/PaintThicknessValue/words_color"].v = RGBA(30, 33, 41, 255);
									}
									UIControlTarget[L"Words/PaintThicknessValue/width"].v = float(75);
									UIControlTarget[L"Words/PaintThicknessValue/height"].v = float(100);
								}
								else
								{
									UIControlTarget[L"Words/PaintThicknessValue/size"].v = float(20);
									UIControlTarget[L"Words/PaintThicknessValue/left"].v = float(96 + 8 + 15);
									UIControlTarget[L"Words/PaintThicknessValue/top"].v = float(UIControlTarget[L"RoundRect/BrushTop/y"].v);
									UIControlTarget[L"Words/PaintThicknessValue/width"].v = float(50);
									UIControlTarget[L"Words/PaintThicknessValue/height"].v = float(100);
									UIControlColorTarget[L"Words/PaintThicknessValue/words_color"].v = SET_ALPHA(brush.color, 255);
								}
							}
						}

						//画笔底部栏
						{
							{
								UIControl[L"Words/PaintBrush/left"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Words/PaintBrush/size"].v = float(18);
									UIControlTarget[L"Words/PaintBrush/left"].v = float(5 + 25);
									UIControlTarget[L"Words/PaintBrush/top"].v = float(floating_windows.height - 55 + 5);
									UIControlTarget[L"Words/PaintBrush/width"].v = float(65);
									UIControlTarget[L"Words/PaintBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 1 || brush.mode == 3 || brush.mode == 4) UIControlColorTarget[L"Words/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Words/PaintBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Words/PaintBrush/size"].v = float(18);
									UIControlTarget[L"Words/PaintBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushChoose/x"].v + 20);
									UIControlTarget[L"Words/PaintBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushChoose/y"].v);
									UIControlTarget[L"Words/PaintBrush/width"].v = float(60);
									UIControlTarget[L"Words/PaintBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 1 || brush.mode == 3 || brush.mode == 4) UIControlColorTarget[L"Words/PaintBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Words/PaintBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
							{
								UIControl[L"Words/FluorescentBrush/left"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Words/FluorescentBrush/size"].v = float(18);
									UIControlTarget[L"Words/FluorescentBrush/left"].v = float(95 + 25);
									UIControlTarget[L"Words/FluorescentBrush/top"].v = float(floating_windows.height - 55 + 5);
									UIControlTarget[L"Words/FluorescentBrush/width"].v = float(65);
									UIControlTarget[L"Words/FluorescentBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 2) UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Words/FluorescentBrush/size"].v = float(18);
									UIControlTarget[L"Words/FluorescentBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushChoose/x"].v + 20);
									UIControlTarget[L"Words/FluorescentBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushChoose/y"].v);
									UIControlTarget[L"Words/FluorescentBrush/width"].v = float(65);
									UIControlTarget[L"Words/FluorescentBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 2) UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Words/FluorescentBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}

							{
								UIControl[L"Words/WriteBrush/left"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Words/WriteBrush/size"].v = float(18);
									UIControlTarget[L"Words/WriteBrush/left"].v = float(195 + 25);
									UIControlTarget[L"Words/WriteBrush/top"].v = float(floating_windows.height - 55 + 5);
									UIControlTarget[L"Words/WriteBrush/width"].v = float(65);
									UIControlTarget[L"Words/WriteBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Words/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Words/WriteBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Words/WriteBrush/size"].v = float(18);
									UIControlTarget[L"Words/WriteBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushMode/x"].v + 20);
									UIControlTarget[L"Words/WriteBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushMode/y"].v);
									UIControlTarget[L"Words/WriteBrush/width"].v = float(60);
									UIControlTarget[L"Words/WriteBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 1 || brush.mode == 2) UIControlColorTarget[L"Words/WriteBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Words/WriteBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
							{
								UIControl[L"Words/LineBrush/left"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Words/LineBrush/size"].v = float(18);
									UIControlTarget[L"Words/LineBrush/left"].v = float(285 + 25);
									UIControlTarget[L"Words/LineBrush/top"].v = float(floating_windows.height - 55 + 5);
									UIControlTarget[L"Words/LineBrush/width"].v = float(65);
									UIControlTarget[L"Words/LineBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/LineBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 3) UIControlColorTarget[L"Words/LineBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Words/LineBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Words/LineBrush/size"].v = float(18);
									UIControlTarget[L"Words/LineBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushMode/x"].v + 20);
									UIControlTarget[L"Words/LineBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushMode/y"].v);
									UIControlTarget[L"Words/LineBrush/width"].v = float(60);
									UIControlTarget[L"Words/LineBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/LineBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 3) UIControlColorTarget[L"Words/LineBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Words/LineBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
							{
								UIControl[L"Words/RectangleBrush/left"].s = float(3);

								if (state == 1.1 || state == 1.11 || state == 1.12)
								{
									UIControlTarget[L"Words/RectangleBrush/size"].v = float(18);
									UIControlTarget[L"Words/RectangleBrush/left"].v = float(375 + 25);
									UIControlTarget[L"Words/RectangleBrush/top"].v = float(floating_windows.height - 55 + 5);
									UIControlTarget[L"Words/RectangleBrush/width"].v = float(65);
									UIControlTarget[L"Words/RectangleBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 4) UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 255);
									else UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = RGBA(130, 130, 130, 255);
								}
								else
								{
									UIControlTarget[L"Words/RectangleBrush/size"].v = float(18);
									UIControlTarget[L"Words/RectangleBrush/left"].v = float(UIControlTarget[L"RoundRect/BrushMode/x"].v + 20);
									UIControlTarget[L"Words/RectangleBrush/top"].v = float(UIControlTarget[L"RoundRect/BrushMode/y"].v);
									UIControlTarget[L"Words/RectangleBrush/width"].v = float(60);
									UIControlTarget[L"Words/RectangleBrush/height"].v = float(33);
									UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 255);

									if (brush.mode == 4) UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = SET_ALPHA(brush.color, 0);
									else UIControlColorTarget[L"Words/RectangleBrush/words_color"].v = RGBA(130, 130, 130, 0);
								}
							}
						}
					}
					//橡皮
					{
						UIControlTarget[L"Words/rubber/height"].v = float(18);
						UIControlTarget[L"Words/rubber/left"].v = float(192 + 7);
						UIControlTarget[L"Words/rubber/top"].v = float(floating_windows.height - 155 + 48);
						UIControlTarget[L"Words/rubber/right"].v = float(192 + 7 + 83);
						UIControlTarget[L"Words/rubber/bottom"].v = float(floating_windows.height - 155 + 48 + 48);
						if (rubber.select) UIControlColorTarget[L"Words/rubber/words_color"].v = RGBA(98, 175, 82, 255);
						else UIControlColorTarget[L"Words/rubber/words_color"].v = RGBA(130, 130, 130, 255);
					}
					//程序调测
					{
						UIControlTarget[L"Words/test/height"].v = float(18);
						UIControlTarget[L"Words/test/left"].v = float(288 + 7);
						UIControlTarget[L"Words/test/top"].v = float(floating_windows.height - 155 + 48);
						UIControlTarget[L"Words/test/right"].v = float(288 + 7 + 83);
						UIControlTarget[L"Words/test/bottom"].v = float(floating_windows.height - 155 + 48 + 48);
						if (test.select) UIControlColorTarget[L"Words/test/words_color"].v = RGBA(98, 175, 82, 255);
						else UIControlColorTarget[L"Words/test/words_color"].v = RGBA(130, 130, 130, 255);
					}
				}
			}

			for (const auto& [key, value] : UIControl)
			{
				if (UIControl[key].s && UIControl[key].v != UIControlTarget[key].v)
				{
					if (abs(UIControl[key].v - UIControlTarget[key].v) <= UIControl[key].e) UIControl[key].v = float(UIControlTarget[key].v);

					else if (UIControl[key].v < UIControlTarget[key].v) UIControl[key].v = float(UIControl[key].v + max(UIControl[key].e, (UIControlTarget[key].v - UIControl[key].v) / UIControl[key].s * 2));
					else UIControl[key].v = float(UIControl[key].v + min(-UIControl[key].e, (UIControlTarget[key].v - UIControl[key].v) / UIControl[key].s * 2));
				}
			}
			for (const auto& [key, value] : UIControlColor)
			{
				if (UIControlColor[key].s && UIControlColor[key].v != UIControlColorTarget[key].v)
				{
					//TODO
					float r1 = GetRValue(UIControlColor[key].v);
					float g1 = GetGValue(UIControlColor[key].v);
					float b1 = GetBValue(UIControlColor[key].v);
					float a1 = float((UIControlColor[key].v >> 24) & 0xff);

					float r2 = GetRValue(UIControlColorTarget[key].v);
					float g2 = GetGValue(UIControlColorTarget[key].v);
					float b2 = GetBValue(UIControlColorTarget[key].v);
					float a2 = float((UIControlColorTarget[key].v >> 24) & 0xff);

					if (abs(r1 - r2) <= UIControlColor[key].e) r1 = r2;

					else if (r1 < r2) r1 = r1 + max(UIControlColor[key].e, (r2 - r1) / UIControlColor[key].s * 2);
					else if (r1 > r2) r1 = r1 + min(-UIControlColor[key].e, (r2 - r1) / UIControlColor[key].s * 2);

					if (abs(g1 - g2) <= UIControlColor[key].e) g1 = g2;

					else if (g1 < g2) g1 = g1 + max(UIControlColor[key].e, (g2 - g1) / UIControlColor[key].s * 2);
					else if (g1 > g2) g1 = g1 + min(-UIControlColor[key].e, (g2 - g1) / UIControlColor[key].s * 2);

					if (abs(b1 - b2) <= UIControlColor[key].e) b1 = b2;

					else if (b1 < b2) b1 = b1 + max(UIControlColor[key].e, (b2 - b1) / UIControlColor[key].s * 2);
					else if (b1 > b2) b1 = b1 + min(-UIControlColor[key].e, (b2 - b1) / UIControlColor[key].s * 2);

					if (abs(a1 - a2) <= UIControlColor[key].e) a1 = a2;

					else if (a1 < a2) a1 = a1 + max(UIControlColor[key].e, (a2 - a1) / UIControlColor[key].s * 2);
					else if (a1 > a2) a1 = a1 + min(-UIControlColor[key].e, (a2 - a1) / UIControlColor[key].s * 2);

					UIControlColor[key].v = RGBA(max(0, min(255, (int)r1)), max(0, min(255, (int)g1)), max(0, min(255, (int)b1)), max(0, min(255, (int)a1)));
				}
			}
		}

		SetImageColor(background, RGBA(0, 0, 0, 0), true);
		if ((int)state == target_status)
		{
			{
				{
					//画笔粗细调整
					{
						if (setlist.SkinMode == 1 || setlist.SkinMode == 2) hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/PaintThicknessAdjust/x"].v, UIControl[L"RoundRect/PaintThicknessAdjust/y"].v, UIControl[L"RoundRect/PaintThicknessAdjust/width"].v, UIControl[L"RoundRect/PaintThicknessAdjust/height"].v, UIControl[L"RoundRect/PaintThicknessAdjust/ellipsewidth"].v, UIControl[L"RoundRect/PaintThicknessAdjust/ellipseheight"].v, UIControlColor[L"RoundRect/PaintThicknessAdjust/frame"].v, UIControlColor[L"RoundRect/PaintThicknessAdjust/fill"].v, 2, true, SmoothingModeHighQuality, &background);
						else if (setlist.SkinMode == 3)
						{
							Gdiplus::ImageAttributes imageAttributes;

							float alpha = (float)((UIControlColor[L"RoundRect/PaintThicknessAdjust/fill"].v >> 24) & 0xFF) / 255.0f * 0.4f;
							Gdiplus::ColorMatrix colorMatrix = {
								1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 0.0f, alpha, 0.0f,
								0.0f, 0.0f, 0.0f, 0.0f, 1.0f
							};
							imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

							float x = UIControl[L"RoundRect/PaintThicknessAdjust/x"].v;
							float y = UIControl[L"RoundRect/PaintThicknessAdjust/y"].v;
							float width = UIControl[L"RoundRect/PaintThicknessAdjust/width"].v;
							float height = UIControl[L"RoundRect/PaintThicknessAdjust/height"].v;
							float ellipsewidth = UIControl[L"RoundRect/PaintThicknessAdjust/ellipsewidth"].v;
							float ellipseheight = UIControl[L"RoundRect/PaintThicknessAdjust/ellipseheight"].v;

							hiex::EasyX_Gdiplus_SolidRoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/PaintThicknessAdjust/fill"].v, true, SmoothingModeHighQuality, &background);

							Graphics graphics(GetImageHDC(&background));
							GraphicsPath path;

							path.AddArc(x, y, ellipsewidth, ellipseheight, 180, 90);
							path.AddArc(x + width - ellipsewidth - 1, y, ellipsewidth, ellipseheight, 270, 90);
							path.AddArc(x + width - ellipsewidth - 1, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 0, 90);
							path.AddArc(x, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 90, 90);
							path.CloseFigure();

							Region region(&path);
							graphics.SetClip(&region, CombineModeReplace);

							graphics.DrawImage(bskin3, Gdiplus::Rect((int)x, (int)y, (int)width, (int)height), (int)x, (int)y, (int)width, (int)height, Gdiplus::UnitPixel, &imageAttributes);

							graphics.ResetClip();

							hiex::EasyX_Gdiplus_RoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/PaintThicknessAdjust/frame"].v, 2, true, SmoothingModeHighQuality, &background);
						}
					}

					hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/PaintThicknessSchedule1/x"].v, UIControl[L"RoundRect/PaintThicknessSchedule1/y"].v, UIControl[L"RoundRect/PaintThicknessSchedule1/width"].v, UIControl[L"RoundRect/PaintThicknessSchedule1/height"].v, UIControl[L"RoundRect/PaintThicknessSchedule1/ellipsewidth"].v, UIControl[L"RoundRect/PaintThicknessSchedule1/ellipseheight"].v, UIControlColor[L"RoundRect/PaintThicknessSchedule1/fill"].v, true, SmoothingModeHighQuality, &background);
					hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/PaintThicknessSchedule2/x"].v, UIControl[L"RoundRect/PaintThicknessSchedule2/y"].v, UIControl[L"RoundRect/PaintThicknessSchedule2/width"].v, UIControl[L"RoundRect/PaintThicknessSchedule2/height"].v, UIControl[L"RoundRect/PaintThicknessSchedule2/ellipsewidth"].v, UIControl[L"RoundRect/PaintThicknessSchedule2/ellipseheight"].v, UIControlColor[L"RoundRect/PaintThicknessSchedule2/fill"].v, true, SmoothingModeHighQuality, &background);
					hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/PaintThicknessSchedule3/x"].v, UIControl[L"RoundRect/PaintThicknessSchedule3/y"].v, UIControl[L"RoundRect/PaintThicknessSchedule3/width"].v, UIControl[L"RoundRect/PaintThicknessSchedule3/height"].v, UIControl[L"RoundRect/PaintThicknessSchedule3/ellipsewidth"].v, UIControl[L"RoundRect/PaintThicknessSchedule3/ellipseheight"].v, UIControlColor[L"RoundRect/PaintThicknessSchedule3/fill"].v, true, SmoothingModeHighQuality, &background);

					hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/PaintThicknessSchedule4a/x"].v, UIControl[L"RoundRect/PaintThicknessSchedule4a/y"].v, UIControl[L"RoundRect/PaintThicknessSchedule4a/width"].v, UIControl[L"RoundRect/PaintThicknessSchedule4a/height"].v, UIControl[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v, UIControl[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v, UIControlColor[L"RoundRect/PaintThicknessSchedule4a/fill"].v, true, SmoothingModeHighQuality, &background);
					hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/PaintThicknessSchedule5a/x"].v, UIControl[L"RoundRect/PaintThicknessSchedule5a/y"].v, UIControl[L"RoundRect/PaintThicknessSchedule5a/width"].v, UIControl[L"RoundRect/PaintThicknessSchedule5a/height"].v, UIControl[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v, UIControl[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v, UIControlColor[L"RoundRect/PaintThicknessSchedule5a/fill"].v, true, SmoothingModeHighQuality, &background);
					hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/PaintThicknessSchedule6a/x"].v, UIControl[L"RoundRect/PaintThicknessSchedule6a/y"].v, UIControl[L"RoundRect/PaintThicknessSchedule6a/width"].v, UIControl[L"RoundRect/PaintThicknessSchedule6a/height"].v, UIControl[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v, UIControl[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v, UIControlColor[L"RoundRect/PaintThicknessSchedule6a/fill"].v, true, SmoothingModeHighQuality, &background);

					//画笔颜色选择
					{
						if (setlist.SkinMode == 1 || setlist.SkinMode == 2)hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/BrushColorChoose/x"].v, UIControl[L"RoundRect/BrushColorChoose/y"].v, UIControl[L"RoundRect/BrushColorChoose/width"].v, UIControl[L"RoundRect/BrushColorChoose/height"].v, UIControl[L"RoundRect/BrushColorChoose/ellipsewidth"].v, UIControl[L"RoundRect/BrushColorChoose/ellipseheight"].v, UIControlColor[L"RoundRect/BrushColorChoose/frame"].v, UIControlColor[L"RoundRect/BrushColorChoose/fill"].v, 1, true, SmoothingModeHighQuality, &background);
						else if (setlist.SkinMode == 3)
						{
							Gdiplus::ImageAttributes imageAttributes;

							float alpha = (float)((UIControlColor[L"RoundRect/BrushColorChoose/fill"].v >> 24) & 0xFF) / 255.0f * 0.4f;
							Gdiplus::ColorMatrix colorMatrix = {
								1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 0.0f, alpha, 0.0f,
								0.0f, 0.0f, 0.0f, 0.0f, 1.0f
							};
							imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

							float x = UIControl[L"RoundRect/BrushColorChoose/x"].v;
							float y = UIControl[L"RoundRect/BrushColorChoose/y"].v;
							float width = UIControl[L"RoundRect/BrushColorChoose/width"].v;
							float height = UIControl[L"RoundRect/BrushColorChoose/height"].v;
							float ellipsewidth = UIControl[L"RoundRect/BrushColorChoose/ellipsewidth"].v;
							float ellipseheight = UIControl[L"RoundRect/BrushColorChoose/ellipseheight"].v;

							hiex::EasyX_Gdiplus_SolidRoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/BrushColorChoose/fill"].v, true, SmoothingModeHighQuality, &background);

							Graphics graphics(GetImageHDC(&background));
							GraphicsPath path;

							path.AddArc(x, y, ellipsewidth, ellipseheight, 180, 90);
							path.AddArc(x + width - ellipsewidth - 1, y, ellipsewidth, ellipseheight, 270, 90);
							path.AddArc(x + width - ellipsewidth - 1, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 0, 90);
							path.AddArc(x, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 90, 90);
							path.CloseFigure();

							Region region(&path);
							graphics.SetClip(&region, CombineModeReplace);

							graphics.DrawImage(bskin3, Gdiplus::Rect((int)x, (int)y, (int)width, (int)height), (int)x, (int)y, (int)width, (int)height, Gdiplus::UnitPixel, &imageAttributes);

							graphics.ResetClip();

							hiex::EasyX_Gdiplus_RoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/BrushColorChoose/frame"].v, 2, true, SmoothingModeHighQuality, &background);
						}
					}
					if (UIControl[L"RoundRect/BrushColorChooseWheel/transparency"].v != 0)
					{
						std::unique_lock<std::shared_mutex> lock(ColorPaletteSm);
						ColorPaletteImg = DrawHSVWheel((int)UIControl[L"RoundRect/BrushColorChooseWheel/width"].v, 40);
						lock.unlock();

						hiex::TransparentImage(&background, (int)UIControl[L"RoundRect/BrushColorChooseWheel/x"].v, (int)UIControl[L"RoundRect/BrushColorChooseWheel/y"].v, &ColorPaletteImg, (int)UIControl[L"RoundRect/BrushColorChooseWheel/transparency"].v);

						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorChooseWheel/x"].v, UIControl[L"RoundRect/BrushColorChooseWheel/y"].v, UIControl[L"RoundRect/BrushColorChooseWheel/width"].v, UIControl[L"RoundRect/BrushColorChooseWheel/height"].v, UIControl[L"RoundRect/BrushColorChooseWheel/width"].v, UIControl[L"RoundRect/BrushColorChooseWheel/height"].v, RGBA(130, 130, 130, (int)UIControl[L"RoundRect/BrushColorChooseWheel/transparency"].v), 2, true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/BrushColorChooseMark/x"].v, UIControl[L"RoundRect/BrushColorChooseMark/y"].v, UIControl[L"RoundRect/BrushColorChooseMark/width"].v, UIControl[L"RoundRect/BrushColorChooseMark/height"].v, UIControl[L"RoundRect/BrushColorChooseMark/ellipsewidth"].v, UIControl[L"RoundRect/BrushColorChooseMark/ellipseheight"].v, UIControlColor[L"RoundRect/BrushColorChooseMark/frame"].v, UIControlColor[L"RoundRect/BrushColorChooseMark/fill"].v, 2, true, SmoothingModeHighQuality, &background);

						{
							hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/BrushColorChooseMarkR/x"].v, UIControl[L"RoundRect/BrushColorChooseMarkR/y"].v, UIControl[L"RoundRect/BrushColorChooseMarkR/width"].v, UIControl[L"RoundRect/BrushColorChooseMarkR/height"].v, UIControl[L"RoundRect/BrushColorChooseMarkR/ellipsewidth"].v, UIControl[L"RoundRect/BrushColorChooseMarkR/ellipseheight"].v, UIControlColor[L"RoundRect/BrushColorChooseMarkR/frame"].v, UIControlColor[L"RoundRect/BrushColorChooseMarkR/fill"].v, 2, true, SmoothingModeHighQuality, &background);
							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"RoundRect/BrushColorChooseMarkR/text"].v, true));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = LONG(UIControl[L"RoundRect/BrushColorChooseMarkR/x"].v);
								words_rect.top = LONG(UIControl[L"RoundRect/BrushColorChooseMarkR/y"].v);
								words_rect.right = LONG(UIControl[L"RoundRect/BrushColorChooseMarkR/x"].v + UIControl[L"RoundRect/BrushColorChooseMarkR/width"].v);
								words_rect.bottom = LONG(UIControl[L"RoundRect/BrushColorChooseMarkR/y"].v + UIControl[L"RoundRect/BrushColorChooseMarkR/height"].v + 3);
							}
							graphics.DrawString(to_wstring(GetRValue(brush.color)).c_str(), -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
						{
							hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/BrushColorChooseMarkG/x"].v, UIControl[L"RoundRect/BrushColorChooseMarkG/y"].v, UIControl[L"RoundRect/BrushColorChooseMarkG/width"].v, UIControl[L"RoundRect/BrushColorChooseMarkG/height"].v, UIControl[L"RoundRect/BrushColorChooseMarkG/ellipsewidth"].v, UIControl[L"RoundRect/BrushColorChooseMarkG/ellipseheight"].v, UIControlColor[L"RoundRect/BrushColorChooseMarkG/frame"].v, UIControlColor[L"RoundRect/BrushColorChooseMarkG/fill"].v, 2, true, SmoothingModeHighQuality, &background);
							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"RoundRect/BrushColorChooseMarkG/text"].v, true));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = LONG(UIControl[L"RoundRect/BrushColorChooseMarkG/x"].v);
								words_rect.top = LONG(UIControl[L"RoundRect/BrushColorChooseMarkG/y"].v);
								words_rect.right = LONG(UIControl[L"RoundRect/BrushColorChooseMarkG/x"].v + UIControl[L"RoundRect/BrushColorChooseMarkG/width"].v);
								words_rect.bottom = LONG(UIControl[L"RoundRect/BrushColorChooseMarkG/y"].v + UIControl[L"RoundRect/BrushColorChooseMarkG/height"].v + 3);
							}
							graphics.DrawString(to_wstring(GetGValue(brush.color)).c_str(), -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
						{
							hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/BrushColorChooseMarkB/x"].v, UIControl[L"RoundRect/BrushColorChooseMarkB/y"].v, UIControl[L"RoundRect/BrushColorChooseMarkB/width"].v, UIControl[L"RoundRect/BrushColorChooseMarkB/height"].v, UIControl[L"RoundRect/BrushColorChooseMarkB/ellipsewidth"].v, UIControl[L"RoundRect/BrushColorChooseMarkB/ellipseheight"].v, UIControlColor[L"RoundRect/BrushColorChooseMarkB/frame"].v, UIControlColor[L"RoundRect/BrushColorChooseMarkB/fill"].v, 2, true, SmoothingModeHighQuality, &background);
							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"RoundRect/BrushColorChooseMarkB/text"].v, true));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = LONG(UIControl[L"RoundRect/BrushColorChooseMarkB/x"].v);
								words_rect.top = LONG(UIControl[L"RoundRect/BrushColorChooseMarkB/y"].v);
								words_rect.right = LONG(UIControl[L"RoundRect/BrushColorChooseMarkB/x"].v + UIControl[L"RoundRect/BrushColorChooseMarkB/width"].v);
								words_rect.bottom = LONG(UIControl[L"RoundRect/BrushColorChooseMarkB/y"].v + UIControl[L"RoundRect/BrushColorChooseMarkB/height"].v + 3);
							}
							graphics.DrawString(to_wstring(GetBValue(brush.color)).c_str(), -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
					}
				}

				{
					if (setlist.SkinMode == 1 || setlist.SkinMode == 2) hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/BrushTop/x"].v, UIControl[L"RoundRect/BrushTop/y"].v, UIControl[L"RoundRect/BrushTop/width"].v, UIControl[L"RoundRect/BrushTop/height"].v, UIControl[L"RoundRect/BrushTop/ellipseheight"].v, UIControl[L"RoundRect/BrushTop/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushTop/frame"].v, UIControlColor[L"RoundRect/BrushTop/fill"].v, 2, true, SmoothingModeHighQuality, &background);
					else if (setlist.SkinMode == 3)
					{
						Gdiplus::ImageAttributes imageAttributes;

						float alpha = (float)((UIControlColor[L"RoundRect/BrushTop/fill"].v >> 24) & 0xFF) / 255.0f * 0.4f;
						Gdiplus::ColorMatrix colorMatrix = {
							1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
							0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
							0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
							0.0f, 0.0f, 0.0f, alpha, 0.0f,
							0.0f, 0.0f, 0.0f, 0.0f, 1.0f
						};
						imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

						float x = UIControl[L"RoundRect/BrushTop/x"].v;
						float y = UIControl[L"RoundRect/BrushTop/y"].v;
						float width = UIControl[L"RoundRect/BrushTop/width"].v;
						float height = UIControl[L"RoundRect/BrushTop/height"].v;
						float ellipsewidth = UIControl[L"RoundRect/BrushTop/ellipsewidth"].v;
						float ellipseheight = UIControl[L"RoundRect/BrushTop/ellipseheight"].v;

						hiex::EasyX_Gdiplus_SolidRoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/BrushTop/fill"].v, true, SmoothingModeHighQuality, &background);

						Graphics graphics(GetImageHDC(&background));
						GraphicsPath path;

						path.AddArc(x, y, ellipsewidth, ellipseheight, 180, 90);
						path.AddArc(x + width - ellipsewidth - 1, y, ellipsewidth, ellipseheight, 270, 90);
						path.AddArc(x + width - ellipsewidth - 1, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 0, 90);
						path.AddArc(x, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 90, 90);
						path.CloseFigure();

						Region region(&path);
						graphics.SetClip(&region, CombineModeReplace);

						graphics.DrawImage(bskin3, Gdiplus::Rect((int)x, (int)y, (int)width, (int)height), (int)x, (int)y, (int)width, (int)height, Gdiplus::UnitPixel, &imageAttributes);

						graphics.ResetClip();

						hiex::EasyX_Gdiplus_RoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/BrushTop/frame"].v, 2, true, SmoothingModeHighQuality, &background);
					}
				}
				{
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor1/x"].v, UIControl[L"RoundRect/BrushColor1/y"].v, UIControl[L"RoundRect/BrushColor1/width"].v, UIControl[L"RoundRect/BrushColor1/height"].v, UIControl[L"RoundRect/BrushColor1/ellipseheight"].v, UIControl[L"RoundRect/BrushColor1/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor1/fill"].v, (int)UIControl[L"RoundRect/BrushColor1/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame1/x"].v, UIControl[L"RoundRect/BrushColorFrame1/y"].v, UIControl[L"RoundRect/BrushColorFrame1/width"].v, UIControl[L"RoundRect/BrushColorFrame1/height"].v, UIControl[L"RoundRect/BrushColorFrame1/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame1/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame1/frame"].v, UIControl[L"RoundRect/BrushColorFrame1/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor1/x"].v), int(UIControl[L"Image/BrushColor1/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor1/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor2/x"].v, UIControl[L"RoundRect/BrushColor2/y"].v, UIControl[L"RoundRect/BrushColor2/width"].v, UIControl[L"RoundRect/BrushColor2/height"].v, UIControl[L"RoundRect/BrushColor2/ellipseheight"].v, UIControl[L"RoundRect/BrushColor2/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor2/fill"].v, (int)UIControl[L"RoundRect/BrushColor2/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame2/x"].v, UIControl[L"RoundRect/BrushColorFrame2/y"].v, UIControl[L"RoundRect/BrushColorFrame2/width"].v, UIControl[L"RoundRect/BrushColorFrame2/height"].v, UIControl[L"RoundRect/BrushColorFrame2/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame2/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame2/frame"].v, UIControl[L"RoundRect/BrushColorFrame2/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor2/x"].v), int(UIControl[L"Image/BrushColor2/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor2/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor3/x"].v, UIControl[L"RoundRect/BrushColor3/y"].v, UIControl[L"RoundRect/BrushColor3/width"].v, UIControl[L"RoundRect/BrushColor3/height"].v, UIControl[L"RoundRect/BrushColor3/ellipseheight"].v, UIControl[L"RoundRect/BrushColor3/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor3/fill"].v, (int)UIControl[L"RoundRect/BrushColor3/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame3/x"].v, UIControl[L"RoundRect/BrushColorFrame3/y"].v, UIControl[L"RoundRect/BrushColorFrame3/width"].v, UIControl[L"RoundRect/BrushColorFrame3/height"].v, UIControl[L"RoundRect/BrushColorFrame3/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame3/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame3/frame"].v, UIControl[L"RoundRect/BrushColorFrame3/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor3/x"].v), int(UIControl[L"Image/BrushColor3/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor3/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor4/x"].v, UIControl[L"RoundRect/BrushColor4/y"].v, UIControl[L"RoundRect/BrushColor4/width"].v, UIControl[L"RoundRect/BrushColor4/height"].v, UIControl[L"RoundRect/BrushColor4/ellipseheight"].v, UIControl[L"RoundRect/BrushColor4/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor4/fill"].v, (int)UIControl[L"RoundRect/BrushColor4/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame4/x"].v, UIControl[L"RoundRect/BrushColorFrame4/y"].v, UIControl[L"RoundRect/BrushColorFrame4/width"].v, UIControl[L"RoundRect/BrushColorFrame4/height"].v, UIControl[L"RoundRect/BrushColorFrame4/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame4/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame4/frame"].v, UIControl[L"RoundRect/BrushColorFrame4/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor4/x"].v), int(UIControl[L"Image/BrushColor4/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor4/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor5/x"].v, UIControl[L"RoundRect/BrushColor5/y"].v, UIControl[L"RoundRect/BrushColor5/width"].v, UIControl[L"RoundRect/BrushColor5/height"].v, UIControl[L"RoundRect/BrushColor5/ellipseheight"].v, UIControl[L"RoundRect/BrushColor5/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor5/fill"].v, (int)UIControl[L"RoundRect/BrushColor5/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame5/x"].v, UIControl[L"RoundRect/BrushColorFrame5/y"].v, UIControl[L"RoundRect/BrushColorFrame5/width"].v, UIControl[L"RoundRect/BrushColorFrame5/height"].v, UIControl[L"RoundRect/BrushColorFrame5/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame5/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame5/frame"].v, UIControl[L"RoundRect/BrushColorFrame5/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor5/x"].v), int(UIControl[L"Image/BrushColor5/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor5/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor6/x"].v, UIControl[L"RoundRect/BrushColor6/y"].v, UIControl[L"RoundRect/BrushColor6/width"].v, UIControl[L"RoundRect/BrushColor6/height"].v, UIControl[L"RoundRect/BrushColor6/ellipseheight"].v, UIControl[L"RoundRect/BrushColor6/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor6/fill"].v, (int)UIControl[L"RoundRect/BrushColor6/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame6/x"].v, UIControl[L"RoundRect/BrushColorFrame6/y"].v, UIControl[L"RoundRect/BrushColorFrame6/width"].v, UIControl[L"RoundRect/BrushColorFrame6/height"].v, UIControl[L"RoundRect/BrushColorFrame6/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame6/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame6/frame"].v, UIControl[L"RoundRect/BrushColorFrame6/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor6/x"].v), int(UIControl[L"Image/BrushColor6/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor6/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor7/x"].v, UIControl[L"RoundRect/BrushColor7/y"].v, UIControl[L"RoundRect/BrushColor7/width"].v, UIControl[L"RoundRect/BrushColor7/height"].v, UIControl[L"RoundRect/BrushColor7/ellipseheight"].v, UIControl[L"RoundRect/BrushColor7/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor7/fill"].v, (int)UIControl[L"RoundRect/BrushColor7/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame7/x"].v, UIControl[L"RoundRect/BrushColorFrame7/y"].v, UIControl[L"RoundRect/BrushColorFrame7/width"].v, UIControl[L"RoundRect/BrushColorFrame7/height"].v, UIControl[L"RoundRect/BrushColorFrame7/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame7/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame7/frame"].v, UIControl[L"RoundRect/BrushColorFrame7/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor7/x"].v), int(UIControl[L"Image/BrushColor7/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor7/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor8/x"].v, UIControl[L"RoundRect/BrushColor8/y"].v, UIControl[L"RoundRect/BrushColor8/width"].v, UIControl[L"RoundRect/BrushColor8/height"].v, UIControl[L"RoundRect/BrushColor8/ellipseheight"].v, UIControl[L"RoundRect/BrushColor8/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor8/fill"].v, (int)UIControl[L"RoundRect/BrushColor8/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame8/x"].v, UIControl[L"RoundRect/BrushColorFrame8/y"].v, UIControl[L"RoundRect/BrushColorFrame8/width"].v, UIControl[L"RoundRect/BrushColorFrame8/height"].v, UIControl[L"RoundRect/BrushColorFrame8/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame8/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame8/frame"].v, UIControl[L"RoundRect/BrushColorFrame8/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor8/x"].v), int(UIControl[L"Image/BrushColor8/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor8/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor9/x"].v, UIControl[L"RoundRect/BrushColor9/y"].v, UIControl[L"RoundRect/BrushColor9/width"].v, UIControl[L"RoundRect/BrushColor9/height"].v, UIControl[L"RoundRect/BrushColor9/ellipseheight"].v, UIControl[L"RoundRect/BrushColor9/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor9/fill"].v, (int)UIControl[L"RoundRect/BrushColor9/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame9/x"].v, UIControl[L"RoundRect/BrushColorFrame9/y"].v, UIControl[L"RoundRect/BrushColorFrame9/width"].v, UIControl[L"RoundRect/BrushColorFrame9/height"].v, UIControl[L"RoundRect/BrushColorFrame9/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame9/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame9/frame"].v, UIControl[L"RoundRect/BrushColorFrame9/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor9/x"].v), int(UIControl[L"Image/BrushColor9/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor9/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor10/x"].v, UIControl[L"RoundRect/BrushColor10/y"].v, UIControl[L"RoundRect/BrushColor10/width"].v, UIControl[L"RoundRect/BrushColor10/height"].v, UIControl[L"RoundRect/BrushColor10/ellipseheight"].v, UIControl[L"RoundRect/BrushColor10/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor10/fill"].v, (int)UIControl[L"RoundRect/BrushColor10/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame10/x"].v, UIControl[L"RoundRect/BrushColorFrame10/y"].v, UIControl[L"RoundRect/BrushColorFrame10/width"].v, UIControl[L"RoundRect/BrushColorFrame10/height"].v, UIControl[L"RoundRect/BrushColorFrame10/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame10/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame10/frame"].v, UIControl[L"RoundRect/BrushColorFrame10/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor10/x"].v), int(UIControl[L"Image/BrushColor10/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor10/transparency"].v));
					}
					{
						hiex::EasyX_Gdiplus_SolidRoundRect(UIControl[L"RoundRect/BrushColor11/x"].v, UIControl[L"RoundRect/BrushColor11/y"].v, UIControl[L"RoundRect/BrushColor11/width"].v, UIControl[L"RoundRect/BrushColor11/height"].v, UIControl[L"RoundRect/BrushColor11/ellipseheight"].v, UIControl[L"RoundRect/BrushColor11/ellipsewidth"].v, SET_ALPHA(UIControlColor[L"RoundRect/BrushColor11/fill"].v, (int)UIControl[L"RoundRect/BrushColor11/transparency"].v), true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame11/x"].v, UIControl[L"RoundRect/BrushColorFrame11/y"].v, UIControl[L"RoundRect/BrushColorFrame11/width"].v, UIControl[L"RoundRect/BrushColorFrame11/height"].v, UIControl[L"RoundRect/BrushColorFrame11/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame11/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame11/frame"].v, UIControl[L"RoundRect/BrushColorFrame11/thickness"].v, true, SmoothingModeHighQuality, &background);

						hiex::TransparentImage(&background, int(UIControl[L"Image/BrushColor11/x"].v), int(UIControl[L"Image/BrushColor11/y"].v), &floating_icon[17], int(UIControl[L"Image/BrushColor11/transparency"].v));
					}
					{
						IMAGE img = DrawHSVWheel(int(UIControl[L"RoundRect/BrushColor12/width"].v), int(UIControl[L"RoundRect/BrushColor12/width"].v / 2 - 10), int(UIControlTarget[L"RoundRect/BrushColor12/angle"].v));
						hiex::TransparentImage(&background, int(UIControl[L"RoundRect/BrushColor12/x"].v), int(UIControl[L"RoundRect/BrushColor12/y"].v), &img, int(UIControl[L"RoundRect/BrushColor12/transparency"].v));
						hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/BrushColorFrame12/x"].v, UIControl[L"RoundRect/BrushColorFrame12/y"].v, UIControl[L"RoundRect/BrushColorFrame12/width"].v, UIControl[L"RoundRect/BrushColorFrame12/height"].v, UIControl[L"RoundRect/BrushColorFrame12/ellipseheight"].v, UIControl[L"RoundRect/BrushColorFrame12/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushColorFrame12/frame"].v, UIControl[L"RoundRect/BrushColorFrame12/thickness"].v, true, SmoothingModeHighQuality, &background);
					}

					//画笔粗细
					{
						hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/PaintThickness/x"].v, UIControl[L"RoundRect/PaintThickness/y"].v, UIControl[L"RoundRect/PaintThickness/width"].v, UIControl[L"RoundRect/PaintThickness/height"].v, UIControl[L"RoundRect/PaintThickness/ellipseheight"].v, UIControl[L"RoundRect/PaintThickness/ellipsewidth"].v, UIControlColor[L"RoundRect/PaintThickness/frame"].v, UIControlColor[L"RoundRect/PaintThickness/fill"].v, 2, true, SmoothingModeHighQuality, &background);
						{
							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/PaintThickness/size"].v, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/PaintThickness/words_color"].v, true));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = LONG(UIControl[L"Words/PaintThickness/left"].v);
								words_rect.top = LONG(UIControl[L"Words/PaintThickness/top"].v);
								words_rect.right = LONG(words_rect.left + UIControl[L"Words/PaintThickness/width"].v);
								words_rect.bottom = LONG(words_rect.top + UIControl[L"Words/PaintThickness/height"].v);
							}
							graphics.DrawString(L"粗\n细", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}

						hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/PaintThicknessPrompt/x"].v, UIControl[L"RoundRect/PaintThicknessPrompt/y"].v, min(UIControl[L"RoundRect/PaintThickness/x"].v + 97 - UIControl[L"RoundRect/PaintThicknessPrompt/x"].v, UIControl[L"RoundRect/PaintThicknessPrompt/width"].v), min(UIControl[L"RoundRect/PaintThickness/y"].v + 72 - UIControl[L"RoundRect/PaintThicknessPrompt/y"].v, UIControl[L"RoundRect/PaintThicknessPrompt/height"].v), max(10, UIControl[L"RoundRect/PaintThicknessPrompt/ellipseheight"].v - max(0, UIControl[L"RoundRect/PaintThicknessPrompt/width"].v - 60) * 2), max(10, UIControl[L"RoundRect/PaintThicknessPrompt/ellipseheight"].v - max(0, UIControl[L"RoundRect/PaintThicknessPrompt/height"].v - 60) * 2), UIControlColor[L"RoundRect/PaintThicknessPrompt/frame"].v, UIControlColor[L"RoundRect/PaintThicknessPrompt/fill"].v, 2, true, SmoothingModeHighQuality, &background);
						{
							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/PaintThicknessValue/size"].v, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/PaintThicknessValue/words_color"].v, true));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = LONG(UIControl[L"Words/PaintThicknessValue/left"].v);
								words_rect.top = LONG(UIControl[L"Words/PaintThicknessValue/top"].v);
								words_rect.right = LONG(words_rect.left + UIControl[L"Words/PaintThicknessValue/width"].v);
								words_rect.bottom = LONG(words_rect.top + UIControl[L"Words/PaintThicknessValue/height"].v);
							}
							graphics.DrawString(to_wstring(brush.width).c_str(), -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
					}
				}

				{
					if (setlist.SkinMode == 1 || setlist.SkinMode == 2) hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/BrushBottom/x"].v, UIControl[L"RoundRect/BrushBottom/y"].v, UIControl[L"RoundRect/BrushBottom/width"].v, UIControl[L"RoundRect/BrushBottom/height"].v, UIControl[L"RoundRect/BrushBottom/ellipseheight"].v, UIControl[L"RoundRect/BrushBottom/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushBottom/frame"].v, UIControlColor[L"RoundRect/BrushBottom/fill"].v, 2, true, SmoothingModeHighQuality, &background);
					else if (setlist.SkinMode == 3)
					{
						Gdiplus::ImageAttributes imageAttributes;

						float alpha = (float)((UIControlColor[L"RoundRect/BrushBottom/fill"].v >> 24) & 0xFF) / 255.0f * 0.4f;
						Gdiplus::ColorMatrix colorMatrix = {
							1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
							0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
							0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
							0.0f, 0.0f, 0.0f, alpha, 0.0f,
							0.0f, 0.0f, 0.0f, 0.0f, 1.0f
						};
						imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

						float x = UIControl[L"RoundRect/BrushBottom/x"].v;
						float y = UIControl[L"RoundRect/BrushBottom/y"].v;
						float width = UIControl[L"RoundRect/BrushBottom/width"].v;
						float height = UIControl[L"RoundRect/BrushBottom/height"].v;
						float ellipsewidth = UIControl[L"RoundRect/BrushBottom/ellipsewidth"].v;
						float ellipseheight = UIControl[L"RoundRect/BrushBottom/ellipseheight"].v;

						hiex::EasyX_Gdiplus_SolidRoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/BrushBottom/fill"].v, true, SmoothingModeHighQuality, &background);

						Graphics graphics(GetImageHDC(&background));
						GraphicsPath path;

						path.AddArc(x, y, ellipsewidth, ellipseheight, 180, 90);
						path.AddArc(x + width - ellipsewidth - 1, y, ellipsewidth, ellipseheight, 270, 90);
						path.AddArc(x + width - ellipsewidth - 1, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 0, 90);
						path.AddArc(x, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 90, 90);
						path.CloseFigure();

						Region region(&path);
						graphics.SetClip(&region, CombineModeReplace);

						graphics.DrawImage(bskin3, Gdiplus::Rect((int)x, (int)y, (int)width, (int)height), (int)x, (int)y, (int)width, (int)height, Gdiplus::UnitPixel, &imageAttributes);

						graphics.ResetClip();

						hiex::EasyX_Gdiplus_RoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/BrushBottom/frame"].v, 2, true, SmoothingModeHighQuality, &background);
					}
				}
				{
					hiex::EasyX_Gdiplus_SolidRectangle(UIControl[L"RoundRect/BrushInterval/x"].v, UIControl[L"RoundRect/BrushInterval/y"].v, UIControl[L"RoundRect/BrushInterval/width"].v, UIControl[L"RoundRect/BrushInterval/height"].v, UIControlColor[L"RoundRect/BrushInterval/fill"].v, true, SmoothingModeHighQuality, &background);

					{
						ChangeColor(floating_icon[12], UIControlColor[L"Image/PaintBrush/words_color"].v);
						hiex::TransparentImage(&background, int(UIControl[L"Image/PaintBrush/x"].v), int(UIControl[L"Image/PaintBrush/y"].v), &floating_icon[12], int((UIControlColor[L"Image/PaintBrush/words_color"].v >> 24) & 0xff));

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/PaintBrush/size"].v, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/PaintBrush/words_color"].v, true));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							if ((int)state == 1) words_rect.left = LONG(UIControl[L"Words/PaintBrush/left"].v);
							else words_rect.left = LONG(min(UIControl[L"Words/PaintBrush/left"].v, UIControl[L"RoundRect/BrushBottom/x"].v + UIControl[L"RoundRect/BrushBottom/width"].v - 55));

							words_rect.top = LONG(UIControl[L"Words/PaintBrush/top"].v);
							words_rect.right = LONG(words_rect.left + UIControl[L"Words/PaintBrush/width"].v);
							words_rect.bottom = LONG(words_rect.top + UIControl[L"Words/PaintBrush/height"].v);
						}
						graphics.DrawString(L"画笔", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
					}
					{
						ChangeColor(floating_icon[11], UIControlColor[L"Image/FluorescentBrush/words_color"].v);
						hiex::TransparentImage(&background, int(UIControl[L"Image/FluorescentBrush/x"].v), int(UIControl[L"Image/FluorescentBrush/y"].v), &floating_icon[11], int((UIControlColor[L"Image/FluorescentBrush/words_color"].v >> 24) & 0xff));

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/FluorescentBrush/size"].v, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/FluorescentBrush/words_color"].v, true));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							if ((int)state == 1) words_rect.left = LONG(UIControl[L"Words/FluorescentBrush/left"].v);
							else words_rect.left = LONG(min(UIControl[L"Words/FluorescentBrush/left"].v, UIControl[L"RoundRect/BrushBottom/x"].v + UIControl[L"RoundRect/BrushBottom/width"].v - 55));

							words_rect.top = LONG(UIControl[L"Words/FluorescentBrush/top"].v);
							words_rect.right = LONG(words_rect.left + UIControl[L"Words/FluorescentBrush/width"].v);
							words_rect.bottom = LONG(words_rect.top + UIControl[L"Words/FluorescentBrush/height"].v);
						}
						graphics.DrawString(L"荧光笔", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
					}

					{
						ChangeColor(floating_icon[13], UIControlColor[L"Image/WriteBrush/words_color"].v);
						hiex::TransparentImage(&background, int(UIControl[L"Image/WriteBrush/x"].v), int(UIControl[L"Image/WriteBrush/y"].v), &floating_icon[13], int((UIControlColor[L"Image/WriteBrush/words_color"].v >> 24) & 0xff));

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/WriteBrush/size"].v, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/WriteBrush/words_color"].v, true));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							if ((int)state == 1) words_rect.left = LONG(UIControl[L"Words/WriteBrush/left"].v);
							else words_rect.left = LONG(min(UIControl[L"Words/LineBrush/left"].v, UIControl[L"RoundRect/BrushBottom/x"].v + UIControl[L"RoundRect/BrushBottom/width"].v - 55));

							words_rect.top = LONG(UIControl[L"Words/WriteBrush/top"].v);
							words_rect.right = LONG(words_rect.left + UIControl[L"Words/WriteBrush/width"].v);
							words_rect.bottom = LONG(words_rect.top + UIControl[L"Words/WriteBrush/height"].v);
						}
						graphics.DrawString(L"书写", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
					}
					{
						ChangeColor(floating_icon[14], UIControlColor[L"Image/LineBrush/words_color"].v);
						hiex::TransparentImage(&background, int(UIControl[L"Image/LineBrush/x"].v), int(UIControl[L"Image/LineBrush/y"].v), &floating_icon[14], int((UIControlColor[L"Image/LineBrush/words_color"].v >> 24) & 0xff));

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/LineBrush/size"].v, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/LineBrush/words_color"].v, true));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							if ((int)state == 1) words_rect.left = LONG(UIControl[L"Words/LineBrush/left"].v);
							else words_rect.left = LONG(min(UIControl[L"Words/LineBrush/left"].v, UIControl[L"RoundRect/BrushBottom/x"].v + UIControl[L"RoundRect/BrushBottom/width"].v - 55));

							words_rect.top = LONG(UIControl[L"Words/LineBrush/top"].v);
							words_rect.right = LONG(words_rect.left + UIControl[L"Words/LineBrush/width"].v);
							words_rect.bottom = LONG(words_rect.top + UIControl[L"Words/LineBrush/height"].v);
						}
						graphics.DrawString(L"直线", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
					}
					{
						ChangeColor(floating_icon[15], UIControlColor[L"Image/RectangleBrush/words_color"].v);
						hiex::TransparentImage(&background, int(UIControl[L"Image/RectangleBrush/x"].v), int(UIControl[L"Image/RectangleBrush/y"].v), &floating_icon[15], int((UIControlColor[L"Image/RectangleBrush/words_color"].v >> 24) & 0xff));

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/RectangleBrush/size"].v, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/RectangleBrush/words_color"].v, true));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							if ((int)state == 1) words_rect.left = LONG(UIControl[L"Words/RectangleBrush/left"].v);
							else words_rect.left = LONG(min(UIControl[L"Words/RectangleBrush/left"].v, UIControl[L"RoundRect/BrushBottom/x"].v + UIControl[L"RoundRect/BrushBottom/width"].v - 55));

							words_rect.top = LONG(UIControl[L"Words/RectangleBrush/top"].v);
							words_rect.right = LONG(words_rect.left + UIControl[L"Words/RectangleBrush/width"].v);
							words_rect.bottom = LONG(words_rect.top + UIControl[L"Words/RectangleBrush/height"].v);
						}
						graphics.DrawString(L"矩形", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
					}

					hiex::EasyX_Gdiplus_RoundRect(max(UIControl[L"RoundRect/BrushChoose/x"].v, UIControl[L"RoundRect/BrushBottom/x"].v + 5), UIControl[L"RoundRect/BrushChoose/y"].v, UIControl[L"RoundRect/BrushChoose/width"].v, UIControl[L"RoundRect/BrushChoose/height"].v, UIControl[L"RoundRect/BrushChoose/ellipseheight"].v, UIControl[L"RoundRect/BrushChoose/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushChoose/frame"].v, 2, true, SmoothingModeHighQuality, &background);
					hiex::EasyX_Gdiplus_RoundRect(min(UIControl[L"RoundRect/BrushMode/x"].v, UIControl[L"RoundRect/BrushBottom/x"].v + UIControl[L"RoundRect/BrushBottom/width"].v - UIControl[L"RoundRect/BrushMode/width"].v - 5), UIControl[L"RoundRect/BrushMode/y"].v, UIControl[L"RoundRect/BrushMode/width"].v, UIControl[L"RoundRect/BrushMode/height"].v, UIControl[L"RoundRect/BrushMode/ellipseheight"].v, UIControl[L"RoundRect/BrushMode/ellipsewidth"].v, UIControlColor[L"RoundRect/BrushMode/frame"].v, 2, true, SmoothingModeHighQuality, &background);
				}
			}

			{
				Graphics eraser(GetImageHDC(&background));
				GraphicsPath path;

				path.AddArc(1, floating_windows.height - 155 - 2, 25, 25, 180, 90);
				path.AddArc(1 + floating_windows.width - 48 + 8 - 25 - 1, floating_windows.height - 155 - 2, 25, 25, 270, 90);
				path.AddArc(1 + floating_windows.width - 48 + 8 - 25 - 1, floating_windows.height - 155 - 2 + 94 + 4 - 25 - 1, 25, 25, 0, 90);
				path.AddArc(1, floating_windows.height - 155 - 2 + 94 + 4 - 25 - 1, 25, 25, 90, 90);
				path.CloseFigure();

				Region region(&path);
				eraser.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
				eraser.SetClip(&region, CombineModeReplace);
				eraser.Clear(Color(0, 0, 0, 0));
				eraser.ResetClip();
			}

			//皮肤背景绘制
			{
				//默认皮肤
				if (setlist.SkinMode == 1 || setlist.SkinMode == 2)
				{
					hiex::EasyX_Gdiplus_FillRoundRect(UIControl[L"RoundRect/RoundRect1/x"].v, UIControl[L"RoundRect/RoundRect1/y"].v, UIControl[L"RoundRect/RoundRect1/width"].v, UIControl[L"RoundRect/RoundRect1/height"].v, UIControl[L"RoundRect/RoundRect1/ellipsewidth"].v, UIControl[L"RoundRect/RoundRect1/ellipseheight"].v, UIControlColor[L"RoundRect/RoundRect1/frame"].v, UIControlColor[L"RoundRect/RoundRect1/fill"].v, 2, true, SmoothingModeHighQuality, &background);
				}

				//龙年迎新皮肤
				else if (setlist.SkinMode == 3)
				{
					Gdiplus::ImageAttributes imageAttributes;

					float alpha = (float)((UIControlColor[L"RoundRect/RoundRect1/fill"].v >> 24) & 0xFF) / 255.0f * 0.4f;
					Gdiplus::ColorMatrix colorMatrix = {
						1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
						0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
						0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
						0.0f, 0.0f, 0.0f, alpha, 0.0f,
						0.0f, 0.0f, 0.0f, 0.0f, 1.0f
					};
					imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

					float x = UIControl[L"RoundRect/RoundRect1/x"].v;
					float y = UIControl[L"RoundRect/RoundRect1/y"].v;
					float width = UIControl[L"RoundRect/RoundRect1/width"].v;
					float height = UIControl[L"RoundRect/RoundRect1/height"].v;
					float ellipsewidth = UIControl[L"RoundRect/RoundRect1/ellipsewidth"].v;
					float ellipseheight = UIControl[L"RoundRect/RoundRect1/ellipseheight"].v;

					hiex::EasyX_Gdiplus_SolidRoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/RoundRect1/fill"].v, true, SmoothingModeHighQuality, &background);

					Graphics graphics(GetImageHDC(&background));
					graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

					GraphicsPath path;

					path.AddArc(x, y, ellipsewidth, ellipseheight, 180, 90);
					path.AddArc(x + width - ellipsewidth - 1, y, ellipsewidth, ellipseheight, 270, 90);
					path.AddArc(x + width - ellipsewidth - 1, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 0, 90);
					path.AddArc(x, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 90, 90);
					path.CloseFigure();

					Region region(&path);
					graphics.SetClip(&region, CombineModeReplace);

					graphics.DrawImage(bskin3, Gdiplus::Rect((int)x, (int)y, (int)width, (int)height), (int)x, (int)y, (int)width, (int)height, Gdiplus::UnitPixel, &imageAttributes);

					graphics.ResetClip();

					hiex::EasyX_Gdiplus_RoundRect(x, y, width, height, ellipsewidth, ellipseheight, UIControlColor[L"RoundRect/RoundRect1/frame"].v, 2, true, SmoothingModeHighQuality, &background);
				}
			}

			//选择
			{
				ChangeColor(floating_icon[0], UIControlColor[L"Image/choose/fill"].v);
				hiex::TransparentImage(&background, int(UIControl[L"Image/choose/x"].v), int(UIControl[L"Image/choose/y"].v), &floating_icon[0], int((UIControlColor[L"Image/choose/fill"].v >> 24) & 0xff));

				Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/choose/height"].v, FontStyleRegular, UnitPixel);
				SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/choose/words_color"].v, true));
				graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
				{
					words_rect.left = LONG(UIControl[L"Words/choose/left"].v);
					words_rect.top = LONG(UIControl[L"Words/choose/top"].v);
					words_rect.right = LONG(UIControl[L"Words/choose/right"].v);
					words_rect.bottom = LONG(UIControl[L"Words/choose/bottom"].v);
				}
				if (choose.select) graphics.DrawString(L"选择", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
				else graphics.DrawString(L"选择(清空)", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
			}
			//画笔
			{
				ChangeColor(floating_icon[1], UIControlColor[L"Image/brush/fill"].v);
				hiex::TransparentImage(&background, int(UIControl[L"Image/brush/x"].v), int(UIControl[L"Image/brush/y"].v), &floating_icon[1], int((UIControlColor[L"Image/brush/fill"].v >> 24) & 0xff));

				Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/brush/height"].v, FontStyleRegular, UnitPixel);
				SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/brush/words_color"].v, true));
				graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
				{
					words_rect.left = LONG(UIControl[L"Words/brush/left"].v);
					words_rect.top = LONG(UIControl[L"Words/brush/top"].v);
					words_rect.right = LONG(UIControl[L"Words/brush/right"].v);
					words_rect.bottom = LONG(UIControl[L"Words/brush/bottom"].v);
				}
				graphics.DrawString(L"画笔", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);

				WordBrush.SetColor(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/brushSize/words_color"].v, true));
				Gdiplus::Font gp_font_02(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
				graphics.DrawString(to_wstring(brush.width).c_str(), -1, &gp_font_02, { UIControl[L"Words/brushSize/left"].v ,UIControl[L"Words/brushSize/top"].v }, &WordBrush);

				/*
				if (state == 1.1 || state == 1.11 || state == 1.12)
				{
					hiex::EasyX_Gdiplus_FillRoundRect(0, floating_windows.height - 256, floating_windows.width - 106, 90, 25, 25, RGB(150, 150, 150), RGB(255, 255, 255), 1, false, SmoothingModeHighQuality, &background);

					//白
					hiex::EasyX_Gdiplus_FillEllipse(20, floating_windows.height - 246, 40, 40, RGB(150, 150, 150), color_preinstall[WHITE], 2, false, SmoothingModeHighQuality, &background);
					if (brush.color == color_preinstall[WHITE]) hiex::EasyX_Gdiplus_Ellipse(15, floating_windows.height - 251, 50, 50, RGB(98, 175, 82), 2, false, SmoothingModeHighQuality, &background);
					//黑
					hiex::EasyX_Gdiplus_FillEllipse(60, floating_windows.height - 216, 40, 40, RGB(150, 150, 150), color_preinstall[BLACK], 2, false, SmoothingModeHighQuality, &background);
					if (brush.color == color_preinstall[BLACK]) hiex::EasyX_Gdiplus_Ellipse(55, floating_windows.height - 221, 50, 50, RGB(98, 175, 82), 2, false, SmoothingModeHighQuality, &background);
					//黄
					hiex::EasyX_Gdiplus_FillEllipse(100, floating_windows.height - 246, 40, 40, RGB(150, 150, 150), color_preinstall[YELLOW], 2, false, SmoothingModeHighQuality, &background);
					if (brush.color == color_preinstall[YELLOW]) hiex::EasyX_Gdiplus_Ellipse(95, floating_windows.height - 251, 50, 50, RGB(98, 175, 82), 2, false, SmoothingModeHighQuality, &background);
					//蓝
					hiex::EasyX_Gdiplus_FillEllipse(140, floating_windows.height - 216, 40, 40, RGB(150, 150, 150), color_preinstall[BLUE], 2, false, SmoothingModeHighQuality, &background);
					if (brush.color == color_preinstall[BLUE]) hiex::EasyX_Gdiplus_Ellipse(135, floating_windows.height - 221, 50, 50, RGB(98, 175, 82), 2, false, SmoothingModeHighQuality, &background);
					//绿
					hiex::EasyX_Gdiplus_FillEllipse(180, floating_windows.height - 246, 40, 40, RGB(150, 150, 150), color_preinstall[GREEN], 2, false, SmoothingModeHighQuality, &background);
					if (brush.color == color_preinstall[GREEN]) hiex::EasyX_Gdiplus_Ellipse(175, floating_windows.height - 251, 50, 50, RGB(98, 175, 82), 2, false, SmoothingModeHighQuality, &background);
					//红
					hiex::EasyX_Gdiplus_FillEllipse(220, floating_windows.height - 216, 40, 40, RGB(150, 150, 150), color_preinstall[RED], 2, false, SmoothingModeHighQuality, &background);
					if (brush.color == color_preinstall[RED]) hiex::EasyX_Gdiplus_Ellipse(215, floating_windows.height - 221, 50, 50, RGB(98, 175, 82), 2, false, SmoothingModeHighQuality, &background);

					//画板模式
					{
						hiex::EasyX_Gdiplus_FillRoundRect(275, floating_windows.height - 246, 90, 70, 25, 25, RGB(150, 150, 150), color_distance(WHITE, brush.color) >= 120 ? WHITE : RGB(130, 130, 130), 2, false, SmoothingModeHighQuality, &background);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(brush.color, false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							words_rect.left = 275;
							words_rect.top = floating_windows.height - 246 + 45;
							words_rect.right = 275 + 90;
							words_rect.bottom = floating_windows.height - 246 + 70;
						}
						graphics.DrawString(L"标准笔迹", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);

						ChangeColor(floating_icon[10], brush.color);
						hiex::TransparentImage(&background, 275 + 25, floating_windows.height - 242, &floating_icon[10]);
					}
					//画笔粗细
					{
						if (brush.mode == 1)
						{
							hiex::EasyX_Gdiplus_FillRoundRect(370, floating_windows.height - 246, 90, 70, 25, 25, RGB(150, 150, 150), color_distance(WHITE, brush.color) >= 120 ? WHITE : RGB(130, 130, 130), 2, false, SmoothingModeHighQuality, &background);
							{
								Gdiplus::Graphics graphics(GetImageHDC(&background));
								Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(brush.color, false), min(10, brush.width));
								pen.SetStartCap(LineCapRound);
								pen.SetEndCap(LineCapRound);

								graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
								graphics.SetSmoothingMode(SmoothingModeHighQuality);
								graphics.DrawLine(&pen, 380, floating_windows.height - 236, 380 + 70, floating_windows.height - 236 + 30);
							}

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(brush.color, false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = 370;
								words_rect.top = floating_windows.height - 246 + 45;
								words_rect.right = 370 + 90;
								words_rect.bottom = floating_windows.height - 246 + 70;
							}
							graphics.DrawString(L"画笔粗细", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);

							Gdiplus::Font gp_font_2(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							{
								words_rect.left = 360 + 62;
								words_rect.top = floating_windows.height - 246;
								words_rect.right = 360 + 100;
								words_rect.bottom = floating_windows.height - 246 + 30;
							}
							graphics.DrawString(to_wstring(brush.width).c_str(), -1, &gp_font_2, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
						else if (brush.mode == 2)
						{
							hiex::EasyX_Gdiplus_FillRoundRect(370, floating_windows.height - 246, 90, 70, 25, 25, RGB(150, 150, 150), color_distance(WHITE, brush.color) >= 120 ? WHITE : RGB(130, 130, 130), 2, false, SmoothingModeHighQuality, &background);
							{
								Gdiplus::Graphics graphics(GetImageHDC(&background));
								Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(brush.color, false), min(10, brush.width));
								pen.SetStartCap(LineCapRound);
								pen.SetEndCap(LineCapRound);

								graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
								graphics.SetSmoothingMode(SmoothingModeHighQuality);
								graphics.DrawLine(&pen, 380, floating_windows.height - 236, 380 + 70, floating_windows.height - 236 + 30);
							}

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(brush.color, false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = 370;
								words_rect.top = floating_windows.height - 246 + 45;
								words_rect.right = 370 + 90;
								words_rect.bottom = floating_windows.height - 246 + 70;
							}
							graphics.DrawString(L"画笔粗细", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);

							Gdiplus::Font gp_font_2(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							{
								words_rect.left = 360 + 62;
								words_rect.top = floating_windows.height - 246;
								words_rect.right = 360 + 100;
								words_rect.bottom = floating_windows.height - 246 + 30;
							}
							graphics.DrawString(to_wstring(brush.width * 10).c_str(), -1, &gp_font_2, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
						else if (brush.mode == 3)
						{
							hiex::EasyX_Gdiplus_FillRoundRect(370, floating_windows.height - 246, 90, 70, 25, 25, RGB(150, 150, 150), color_distance(WHITE, brush.color) >= 120 ? WHITE : RGB(130, 130, 130), 2, false, SmoothingModeHighQuality, &background);
							{
								Gdiplus::Graphics graphics(GetImageHDC(&background));
								Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(brush.color, false), min(10, brush.width));
								pen.SetStartCap(LineCapRound);
								pen.SetEndCap(LineCapRound);

								graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
								graphics.SetSmoothingMode(SmoothingModeHighQuality);
								graphics.DrawLine(&pen, 380, floating_windows.height - 236, 380 + 70, floating_windows.height - 236 + 30);
							}

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(brush.color, false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = 370;
								words_rect.top = floating_windows.height - 246 + 45;
								words_rect.right = 370 + 90;
								words_rect.bottom = floating_windows.height - 246 + 70;
							}
							graphics.DrawString(L"直线粗细", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);

							Gdiplus::Font gp_font_2(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							{
								words_rect.left = 360 + 62;
								words_rect.top = floating_windows.height - 246;
								words_rect.right = 360 + 100;
								words_rect.bottom = floating_windows.height - 246 + 30;
							}
							graphics.DrawString(to_wstring(brush.width).c_str(), -1, &gp_font_2, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
						else if (brush.mode == 4)
						{
							hiex::EasyX_Gdiplus_FillRoundRect(370, floating_windows.height - 246, 90, 70, 25, 25, RGB(150, 150, 150), color_distance(WHITE, brush.color) >= 120 ? WHITE : RGB(130, 130, 130), 2, false, SmoothingModeHighQuality, &background);
							{
								Gdiplus::Graphics graphics(GetImageHDC(&background));
								Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(brush.color, false), min(10, brush.width));
								pen.SetStartCap(LineCapRound);
								pen.SetEndCap(LineCapRound);

								graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
								graphics.SetSmoothingMode(SmoothingModeHighQuality);
								graphics.DrawLine(&pen, 380, floating_windows.height - 236, 380 + 70, floating_windows.height - 236 + 30);
							}

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(brush.color, false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = 370;
								words_rect.top = floating_windows.height - 246 + 45;
								words_rect.right = 370 + 90;
								words_rect.bottom = floating_windows.height - 246 + 70;
							}
							graphics.DrawString(L"边框粗细", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);

							Gdiplus::Font gp_font_2(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							{
								words_rect.left = 360 + 62;
								words_rect.top = floating_windows.height - 246;
								words_rect.right = 360 + 100;
								words_rect.bottom = floating_windows.height - 246 + 30;
							}
							graphics.DrawString(to_wstring(brush.width).c_str(), -1, &gp_font_2, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
					}

	*/
			}
			//橡皮
			{
				ChangeColor(floating_icon[2], UIControlColor[L"Image/rubber/fill"].v);
				hiex::TransparentImage(&background, int(UIControl[L"Image/rubber/x"].v), int(UIControl[L"Image/rubber/y"].v), &floating_icon[2], int((UIControlColor[L"Image/rubber/fill"].v >> 24) & 0xff));

				Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/rubber/height"].v, FontStyleRegular, UnitPixel);
				SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/rubber/words_color"].v, true));
				graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
				{
					words_rect.left = LONG(UIControl[L"Words/rubber/left"].v);
					words_rect.top = LONG(UIControl[L"Words/rubber/top"].v);
					words_rect.right = LONG(UIControl[L"Words/rubber/right"].v);
					words_rect.bottom = LONG(UIControl[L"Words/rubber/bottom"].v);
				}
				graphics.DrawString(L"橡皮", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
			}
			//选项
			{
				ChangeColor(floating_icon[7], UIControlColor[L"Image/test/fill"].v);
				hiex::TransparentImage(&background, int(UIControl[L"Image/test/x"].v), int(UIControl[L"Image/test/y"].v), &floating_icon[7], int((UIControlColor[L"Image/test/fill"].v >> 24) & 0xff));
				if (AutomaticUpdateStep == 9) hiex::EasyX_Gdiplus_SolidEllipse(UIControl[L"Image/test/x"].v + 30, UIControl[L"Image/test/y"].v, 10, 10, RGBA(228, 55, 66, 255), false, SmoothingModeHighQuality, &background);

				Gdiplus::Font gp_font(&HarmonyOS_fontFamily, UIControl[L"Words/test/height"].v, FontStyleRegular, UnitPixel);
				SolidBrush WordBrush(hiex::ConvertToGdiplusColor(UIControlColor[L"Words/test/words_color"].v, true));
				graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
				{
					words_rect.left = LONG(UIControl[L"Words/test/left"].v);
					words_rect.top = LONG(UIControl[L"Words/test/top"].v);
					words_rect.right = LONG(UIControl[L"Words/test/right"].v);
					words_rect.bottom = LONG(UIControl[L"Words/test/bottom"].v);
				}
				graphics.DrawString(L"选项", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
			}

			//插件空位1：随机点名
			{
				if (UIControl[L"RoundRect/RoundRect2/x"].v == 8 && plug_in_RandomRollCall.select)
				{
					hiex::EasyX_Gdiplus_FillRoundRect(2, (float)floating_windows.height - 55, 100, 40, 20, 20, RGB(130, 130, 130), BackgroundColorMode == 0 ? RGB(255, 255, 255) : RGB(30, 33, 41), 1, false, SmoothingModeHighQuality, &background);
					hiex::EasyX_Gdiplus_RoundRect(2 + 3, (float)floating_windows.height - 55 + 3, 100 - 6, 40 - 6, 20, 20, RGB(200, 200, 200), 2, false, SmoothingModeHighQuality, &background);

					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 19, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGB(110, 110, 110), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						words_rect.left = LONG(2);
						words_rect.top = LONG((floating_windows.height - 55));
						words_rect.right = LONG(2 + 100);
						words_rect.bottom = LONG((floating_windows.height - 55) + 43);
					}
					graphics.DrawString(L"随机点名", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);

					plug_in_RandomRollCall.select = 2;
				}
				else if (plug_in_RandomRollCall.select) plug_in_RandomRollCall.select = 1;
			}

			hiex::EasyX_Gdiplus_RoundRect(UIControl[L"RoundRect/RoundRect2/x"].v, UIControl[L"RoundRect/RoundRect2/y"].v, UIControl[L"RoundRect/RoundRect2/width"].v, UIControl[L"RoundRect/RoundRect2/height"].v, UIControl[L"RoundRect/RoundRect2/ellipsewidth"].v, UIControl[L"RoundRect/RoundRect2/ellipseheight"].v, UIControlColor[L"RoundRect/RoundRect2/frame"].v, 2.5, true, SmoothingModeHighQuality, &background);

			//主按钮
			{
				{
					Graphics eraser(GetImageHDC(&background));
					GraphicsPath path;

					path.AddArc(UIControl[L"Ellipse/Ellipse1/x"].v, UIControl[L"Ellipse/Ellipse1/y"].v, (UIControl[L"Ellipse/Ellipse1/width"].v + 2), UIControl[L"Ellipse/Ellipse1/height"].v, 270, -180);
					path.AddLine((UIControl[L"Ellipse/Ellipse1/x"].v + 2) + UIControl[L"Ellipse/Ellipse1/width"].v / 2, (UIControl[L"Ellipse/Ellipse1/y"].v + 1) + UIControl[L"Ellipse/Ellipse1/height"].v, (float)floating_windows.width, (UIControl[L"Ellipse/Ellipse1/y"].v + 1) + UIControl[L"Ellipse/Ellipse1/height"].v);
					path.AddLine((float)floating_windows.width, (UIControl[L"Ellipse/Ellipse1/y"].v + 1) + UIControl[L"Ellipse/Ellipse1/height"].v, (float)floating_windows.width, (UIControl[L"Ellipse/Ellipse1/y"].v - 2));
					path.AddLine((float)floating_windows.width, (UIControl[L"Ellipse/Ellipse1/y"].v - 2), (UIControl[L"Ellipse/Ellipse1/x"].v + 2) + UIControl[L"Ellipse/Ellipse1/width"].v / 2, (UIControl[L"Ellipse/Ellipse1/y"].v - 2));
					path.CloseFigure();

					Region region(&path);
					eraser.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
					eraser.SetClip(&region, CombineModeReplace);
					eraser.Clear(Color(0, 0, 0, 255));
					eraser.ResetClip();
				}

				//主按钮皮肤绘制
				{
					//默认皮肤
					if (setlist.SkinMode == 1)
					{
						hiex::EasyX_Gdiplus_FillEllipse(UIControl[L"Ellipse/Ellipse1/x"].v, UIControl[L"Ellipse/Ellipse1/y"].v, UIControl[L"Ellipse/Ellipse1/width"].v, UIControl[L"Ellipse/Ellipse1/height"].v, SET_ALPHA(UIControlColor[L"Ellipse/Ellipse1/frame"].v, (int)UIControl[L"Image/Sign1/frame_transparency"].v), UIControlColor[L"Ellipse/Ellipse1/fill"].v, 2, true, SmoothingModeHighQuality, &background);

						//模式图标
						{
							if (PptInfoState.TotalPage != -1)
							{
								if (ppt_software == L"PowerPoint") hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 35), int(UIControl[L"Ellipse/Ellipse1/y"].v + 63), &floating_icon[16], 255);
								else if (ppt_software == L"WPS") hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 38), int(UIControl[L"Ellipse/Ellipse1/y"].v + 67), &floating_icon[20], 255);
							}
							else if (SeewoCameraIsOpen == true) hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 38), int(UIControl[L"Ellipse/Ellipse1/y"].v + 66), &floating_icon[18], 255);
						}

						hiex::TransparentImage(&background, int(UIControl[L"Image/Sign1/x"].v), int(UIControl[L"Image/Sign1/y"].v), &sign, int(UIControl[L"Image/Sign1/transparency"].v));
					}

					//时钟表盘
					else if (setlist.SkinMode == 2)
					{
						hiex::EasyX_Gdiplus_FillEllipse(UIControl[L"Ellipse/Ellipse1/x"].v, UIControl[L"Ellipse/Ellipse1/y"].v, UIControl[L"Ellipse/Ellipse1/width"].v, UIControl[L"Ellipse/Ellipse1/height"].v, SET_ALPHA(UIControlColor[L"Ellipse/Ellipse1/frame"].v, (int)UIControl[L"Image/Sign1/frame_transparency"].v), UIControlColor[L"Ellipse/Ellipse1/fill"].v, 2, true, SmoothingModeHighQuality, &background);

						//模式图标
						{
							if (PptInfoState.TotalPage != -1)
							{
								if (ppt_software == L"PowerPoint") hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 35), int(UIControl[L"Ellipse/Ellipse1/y"].v + 63), &floating_icon[16], 255);
								else if (ppt_software == L"WPS") hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 38), int(UIControl[L"Ellipse/Ellipse1/y"].v + 67), &floating_icon[20], 255);
							}
							else if (SeewoCameraIsOpen == true) hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 38), int(UIControl[L"Ellipse/Ellipse1/y"].v + 63), &floating_icon[18], 200);
						}

						//时钟
						{
							//刻度
							{
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 0);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 0);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 30);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 30);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 60);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 60);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 40, 90);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 45, 90);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 41, 120);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 45, 120);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 41, 150);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 45, 150);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 40, 180);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 45, 180);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 210);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 210);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 240);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 240);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 270);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 270);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 300);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 300);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
								{
									pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 42, 330);
									pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 47.5, UIControl[L"Ellipse/Ellipse1/y"].v + 47.5, 47, 330);

									Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
									Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

									Gdiplus::Pen pen(hiex::ConvertToGdiplusColor(UIControlColor[L"Ellipse/Ellipse1/frame"].v, false), 2.0f);

									graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
								}
							}

							tm time = GetCurrentLocalTime();
							//时针
							{
								pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 48, UIControl[L"Ellipse/Ellipse1/y"].v + 48, 30, 30 * (time.tm_hour % 12) + 0.5 * time.tm_min);
								pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 48, UIControl[L"Ellipse/Ellipse1/y"].v + 48, 0, 30 * (time.tm_hour % 12) + 0.5 * time.tm_min);

								Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
								Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

								Gdiplus::Color color1(255, 255, 255, 255);
								Gdiplus::Color color2(0, 255, 255, 255);
								Gdiplus::Color color3(0, 255, 255, 255);
								Gdiplus::Color colors[] = { color1, color2, color3 };
								Gdiplus::REAL positions[] = { 0.0f, 0.8f, 1.0f };

								Gdiplus::LinearGradientBrush brush(startPoint, endPoint, color1, color3);
								brush.SetInterpolationColors(colors, positions, 3);
								brush.SetWrapMode(WrapModeTileFlipX);

								Gdiplus::Pen pen(&brush, 5.0f);
								pen.SetStartCap(LineCapRound);
								pen.SetEndCap(LineCapRound);

								graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
							}
							//分针
							{
								pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 48, UIControl[L"Ellipse/Ellipse1/y"].v + 48, 38, 6 * time.tm_min + 0.1 * time.tm_sec);
								pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 48, UIControl[L"Ellipse/Ellipse1/y"].v + 48, 0, 6 * time.tm_min + 0.1 * time.tm_sec);

								Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
								Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

								Gdiplus::Color color1(255, 255, 255, 255);
								Gdiplus::Color color2(0, 255, 255, 255);
								Gdiplus::Color color3(0, 255, 255, 255);
								Gdiplus::Color colors[] = { color1, color2, color3 };
								Gdiplus::REAL positions[] = { 0.0f, 0.8f, 1.0f };

								Gdiplus::LinearGradientBrush brush(startPoint, endPoint, color1, color3);
								brush.SetInterpolationColors(colors, positions, 3);
								brush.SetWrapMode(WrapModeTileFlipX);

								Gdiplus::Pen pen(&brush, 3.0f);
								pen.SetStartCap(LineCapRound);
								pen.SetEndCap(LineCapRound);

								graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
							}
							//秒针
							{
								pair<double, double> direction1 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 48, UIControl[L"Ellipse/Ellipse1/y"].v + 48, 42, 6 * time.tm_sec);
								pair<double, double> direction2 = GetPointOnCircle(UIControl[L"Ellipse/Ellipse1/x"].v + 48, UIControl[L"Ellipse/Ellipse1/y"].v + 48, 0, 6 * time.tm_sec);

								Gdiplus::Point startPoint((INT)direction1.first, (INT)direction1.second);
								Gdiplus::Point endPoint((INT)direction2.first, (INT)direction2.second);

								Gdiplus::Color color1(255, 255, 0, 0);
								Gdiplus::Color color2(0, 255, 0, 0);
								Gdiplus::Color color3(0, 255, 0, 0);
								Gdiplus::Color colors[] = { color1, color2, color3 };
								Gdiplus::REAL positions[] = { 0.0f, 0.7f, 1.0f };

								Gdiplus::LinearGradientBrush brush(startPoint, endPoint, color1, color3);
								brush.SetInterpolationColors(colors, positions, 3);
								brush.SetWrapMode(WrapModeTileFlipX);

								Gdiplus::Pen pen(&brush, 2.0f);
								pen.SetStartCap(LineCapRound);
								pen.SetEndCap(LineCapRound);

								graphics.DrawLine(&pen, (INT)direction1.first, (INT)direction1.second, (INT)direction2.first, (INT)direction2.second);
							}
						}

						hiex::TransparentImage(&background, int(UIControl[L"Image/Sign1/x"].v - 18), int(UIControl[L"Image/Sign1/y"].v - 18), &skin[2]);
					}

					//龙年迎新
					else if (setlist.SkinMode == 3)
					{
						hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v), int(UIControl[L"Ellipse/Ellipse1/y"].v), &skin[1], (UIControlColor[L"Ellipse/Ellipse1/fill"].v >> 24) & 0xFF);
						hiex::EasyX_Gdiplus_Ellipse(UIControl[L"Ellipse/Ellipse1/x"].v, UIControl[L"Ellipse/Ellipse1/y"].v, UIControl[L"Ellipse/Ellipse1/width"].v, UIControl[L"Ellipse/Ellipse1/height"].v, SET_ALPHA(UIControlColor[L"Ellipse/Ellipse1/frame"].v, (int)UIControl[L"Image/Sign1/frame_transparency"].v), 3, true, SmoothingModeHighQuality, &background);
						hiex::EasyX_Gdiplus_Ellipse(UIControl[L"Ellipse/Ellipse1/x"].v + 1, UIControl[L"Ellipse/Ellipse1/y"].v + 1, UIControl[L"Ellipse/Ellipse1/width"].v - 2, UIControl[L"Ellipse/Ellipse1/height"].v - 2, SET_ALPHA(RGBA(235, 151, 39, 255), (int)((UIControlColor[L"Ellipse/Ellipse1/fill"].v >> 24) & 0xFF)), 2, true, SmoothingModeHighQuality, &background);

						//模式图标
						{
							if (PptInfoState.TotalPage != -1)
							{
								if (ppt_software == L"PowerPoint") hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 35), int(UIControl[L"Ellipse/Ellipse1/y"].v + 63), &floating_icon[16], 255);
								else if (ppt_software == L"WPS") hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 38), int(UIControl[L"Ellipse/Ellipse1/y"].v + 67), &floating_icon[20], 255);
							}
							else if (SeewoCameraIsOpen == true) hiex::TransparentImage(&background, int(UIControl[L"Ellipse/Ellipse1/x"].v + 38), int(UIControl[L"Ellipse/Ellipse1/y"].v + 66), &floating_icon[18], 255);
						}

						hiex::TransparentImage(&background, int(UIControl[L"Image/Sign1/x"].v - 18), int(UIControl[L"Image/Sign1/y"].v - 18), &skin[2]);
					}
				}

				if (choose.select != true && (int)state == 1)
				{
					{
						if (setlist.SkinMode == 1 || setlist.SkinMode == 2) hiex::EasyX_Gdiplus_FillRoundRect((float)floating_windows.width - 96, (float)floating_windows.height - 257, 96, 96, 25, 25, RGB(150, 150, 150), BackgroundColorMode == 0 ? RGB(255, 255, 255) : RGB(30, 33, 41), 2, false, SmoothingModeHighQuality, &background);
						else if (setlist.SkinMode == 3)
						{
							Gdiplus::ImageAttributes imageAttributes;

							float alpha = 0.4f;
							Gdiplus::ColorMatrix colorMatrix = {
								1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 0.0f, alpha, 0.0f,
								0.0f, 0.0f, 0.0f, 0.0f, 1.0f
							};
							imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

							float x = (float)floating_windows.width - 96;
							float y = (float)floating_windows.height - 257;
							float width = 96;
							float height = 96;
							float ellipsewidth = 25;
							float ellipseheight = 25;

							hiex::EasyX_Gdiplus_SolidRoundRect(x, y, width, height, ellipsewidth, ellipseheight, BackgroundColorMode == 0 ? RGBA(255, 255, 255, 255) : RGBA(30, 33, 41, 255), true, SmoothingModeHighQuality, &background);

							Graphics graphics(GetImageHDC(&background));
							GraphicsPath path;

							path.AddArc(x, y, ellipsewidth, ellipseheight, 180, 90);
							path.AddArc(x + width - ellipsewidth - 1, y, ellipsewidth, ellipseheight, 270, 90);
							path.AddArc(x + width - ellipsewidth - 1, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 0, 90);
							path.AddArc(x, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 90, 90);
							path.CloseFigure();

							Region region(&path);
							graphics.SetClip(&region, CombineModeReplace);

							graphics.DrawImage(bskin3, Gdiplus::Rect((int)x, (int)y, (int)width, (int)height), (int)x, (int)y, (int)width, (int)height, Gdiplus::UnitPixel, &imageAttributes);

							graphics.ResetClip();

							hiex::EasyX_Gdiplus_RoundRect(x, y, width, height, ellipsewidth, ellipseheight, RGBA(150, 150, 150, 255), 2, true, SmoothingModeHighQuality, &background);
						}
					}

					if (penetrate.select == true)
					{
						hiex::EasyX_Gdiplus_RoundRect((float)floating_windows.width - 96 + 4, (float)floating_windows.height - 256 + 8, 88, 40, 20, 20, RGBA(98, 175, 82, 255), 2.5, false, SmoothingModeHighQuality, &background);

						ChangeColor(floating_icon[6], RGBA(98, 175, 82, 255));

						hiex::TransparentImage(&background, floating_windows.width - 96 + 10, floating_windows.height - 256 + 12, &floating_icon[6]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(98, 175, 82, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							words_rect.left = floating_windows.width - 96 + 40;
							words_rect.top = floating_windows.height - 256 + 8;
							words_rect.right = floating_windows.width - 96 + 40 + 56;
							words_rect.bottom = floating_windows.height - 256 + 8 + 40;
						}
						graphics.DrawString(L"穿透", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
					}
					else
					{
						ChangeColor(floating_icon[6], RGBA(130, 130, 130, 255));

						hiex::TransparentImage(&background, floating_windows.width - 96 + 10, floating_windows.height - 256 + 12, &floating_icon[6]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							words_rect.left = floating_windows.width - 96 + 40;
							words_rect.top = floating_windows.height - 256 + 8;
							words_rect.right = floating_windows.width - 96 + 40 + 56;
							words_rect.bottom = floating_windows.height - 256 + 8 + 40;
						}
						graphics.DrawString(L"穿透", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
					}
				}
				if ((int)state == 1)
				{
					if (ppt_show == NULL && choose.select == true)
					{
						if (setlist.SkinMode == 1 || setlist.SkinMode == 2) hiex::EasyX_Gdiplus_FillRoundRect((float)floating_windows.width - 96, (float)floating_windows.height - 256 + 44, 96, 51, 25, 25, RGB(150, 150, 150), BackgroundColorMode == 0 ? RGB(255, 255, 255) : RGB(30, 33, 41), 2, false, SmoothingModeHighQuality, &background);
						else if (setlist.SkinMode == 3)
						{
							Gdiplus::ImageAttributes imageAttributes;

							float alpha = 0.4f;
							Gdiplus::ColorMatrix colorMatrix = {
								1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
								0.0f, 0.0f, 0.0f, alpha, 0.0f,
								0.0f, 0.0f, 0.0f, 0.0f, 1.0f
							};
							imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

							float x = (float)floating_windows.width - 96;
							float y = (float)floating_windows.height - 256 + 44;
							float width = 96;
							float height = 51;
							float ellipsewidth = 25;
							float ellipseheight = 25;

							hiex::EasyX_Gdiplus_SolidRoundRect(x, y, width, height, ellipsewidth, ellipseheight, BackgroundColorMode == 0 ? RGBA(255, 255, 255, 255) : RGBA(30, 33, 41, 255), true, SmoothingModeHighQuality, &background);

							Graphics graphics(GetImageHDC(&background));
							GraphicsPath path;

							path.AddArc(x, y, ellipsewidth, ellipseheight, 180, 90);
							path.AddArc(x + width - ellipsewidth - 1, y, ellipsewidth, ellipseheight, 270, 90);
							path.AddArc(x + width - ellipsewidth - 1, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 0, 90);
							path.AddArc(x, y + height - ellipseheight - 1, ellipsewidth, ellipseheight, 90, 90);
							path.CloseFigure();

							Region region(&path);
							graphics.SetClip(&region, CombineModeReplace);

							graphics.DrawImage(bskin3, Gdiplus::Rect((int)x, (int)y, (int)width, (int)height), (int)x, (int)y, (int)width, (int)height, Gdiplus::UnitPixel, &imageAttributes);

							graphics.ResetClip();

							hiex::EasyX_Gdiplus_RoundRect(x, y, width, height, ellipsewidth, ellipseheight, RGBA(150, 150, 150, 255), 2, true, SmoothingModeHighQuality, &background);
						}

						if (FreezeFrame.mode == 1)
						{
							hiex::EasyX_Gdiplus_RoundRect((float)floating_windows.width - 96 + 4, (float)floating_windows.height - 256 + 50, 88, 40, 20, 20, RGBA(98, 175, 82, 255), 2.5, false, SmoothingModeHighQuality, &background);

							ChangeColor(floating_icon[8], RGBA(98, 175, 82, 255));

							hiex::TransparentImage(&background, floating_windows.width - 96 + 10, floating_windows.height - 256 + 54, &floating_icon[8]);

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(98, 175, 82, 255), false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = floating_windows.width - 96 + 40;
								words_rect.top = floating_windows.height - 256 + 50;
								words_rect.right = floating_windows.width - 96 + 40 + 56;
								words_rect.bottom = floating_windows.height - 256 + 50 + 40;
							}
							graphics.DrawString(L"定格", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
						else
						{
							ChangeColor(floating_icon[8], RGBA(130, 130, 130, 255));

							hiex::TransparentImage(&background, floating_windows.width - 96 + 10, floating_windows.height - 256 + 54, &floating_icon[8]);

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = floating_windows.width - 96 + 40;
								words_rect.top = floating_windows.height - 256 + 50;
								words_rect.right = floating_windows.width - 96 + 40 + 56;
								words_rect.bottom = floating_windows.height - 256 + 50 + 40;
							}
							graphics.DrawString(L"定格", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
					}
					else if (choose.select == false)
					{
						if (FreezeFrame.mode == 1)
						{
							hiex::EasyX_Gdiplus_RoundRect((float)floating_windows.width - 96 + 4, (float)floating_windows.height - 256 + 50, 88, 40, 20, 20, RGBA(98, 175, 82, 255), 2.5, false, SmoothingModeHighQuality, &background);

							ChangeColor(floating_icon[8], RGBA(98, 175, 82, 255));

							hiex::TransparentImage(&background, floating_windows.width - 96 + 10, floating_windows.height - 256 + 54, &floating_icon[8]);

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(98, 175, 82, 255), false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = floating_windows.width - 96 + 40;
								words_rect.top = floating_windows.height - 256 + 50;
								words_rect.right = floating_windows.width - 96 + 40 + 56;
								words_rect.bottom = floating_windows.height - 256 + 50 + 40;
							}
							graphics.DrawString(L"定格", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
						else
						{
							ChangeColor(floating_icon[8], RGBA(130, 130, 130, 255));

							hiex::TransparentImage(&background, floating_windows.width - 96 + 10, floating_windows.height - 256 + 54, &floating_icon[8]);

							Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
							SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
							graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
							{
								words_rect.left = floating_windows.width - 96 + 40;
								words_rect.top = floating_windows.height - 256 + 50;
								words_rect.right = floating_windows.width - 96 + 40 + 56;
								words_rect.bottom = floating_windows.height - 256 + 50 + 40;
							}
							graphics.DrawString(L"定格", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
						}
					}
				}

				if ((!choose.select || (int)state == 1) && (!RecallImage.empty() || (!FirstDraw && RecallImagePeak == 0)))
				{
					hiex::EasyX_Gdiplus_FillRoundRect((float)floating_windows.width - 96, (float)floating_windows.height - 55, 96, 40, 25, 25, (!choose.select && !rubber.select) ? brush.color : RGBA(255, 255, 255, 255), RGBA(0, 0, 0, 150), 2, true, SmoothingModeHighQuality, &background);
					ChangeColor(floating_icon[3], RGB(255, 255, 255));
					hiex::TransparentImage(&background, floating_windows.width - 86, floating_windows.height - 50, &floating_icon[3]);

					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGB(255, 255, 255), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						words_rect.left = (floating_windows.width - 96 + 30);
						words_rect.top = (floating_windows.height - 55);
						words_rect.right = (floating_windows.width - 96 + 30) + 66;
						words_rect.bottom = (floating_windows.height - 55) + 42;
					}
					graphics.DrawString(L"撤回", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
				}
				else if ((!choose.select || (int)state == 1) && RecallImage.empty() && current_record_pointer <= total_record_pointer + 1 && practical_total_record_pointer)
				{
					hiex::EasyX_Gdiplus_FillRoundRect((float)floating_windows.width - 96, (float)floating_windows.height - 55, 96, 40, 25, 25, (!choose.select && !rubber.select) ? brush.color : RGBA(255, 255, 255, 255), RGBA(0, 0, 0, 150), 2, true, SmoothingModeHighQuality, &background);

					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGB(255, 255, 255), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						words_rect.left = (floating_windows.width - 96);
						words_rect.top = (floating_windows.height - 55);
						words_rect.right = (floating_windows.width - 96 + 30) + 66;
						words_rect.bottom = (floating_windows.height - 55) + 42;
					}
					graphics.DrawString(L"超级恢复", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
				}
			}

			//更新窗口
			{
				// 设置窗口位置
				POINT ptDst = { floating_windows.x, floating_windows.y };

				ulwi.pptDst = &ptDst;
				ulwi.hdcSrc = GetImageHDC(&background);
				UpdateLayeredWindowIndirect(floating_window, &ulwi);
			}
		}
		else state = target_status;

		if (for_num == 1) ShowWindow(floating_window, SW_SHOW);
		if (tRecord)
		{
			int delay = 1000 / 24 - (clock() - tRecord);
			if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
		tRecord = clock();
	}

	ShowWindow(floating_window, SW_HIDE);
	thread_status[L"DrawScreen"] = false;
}
void MouseInteraction()
{
	thread_status[L"MouseInteraction"] = true;

	int brush_connect = -1;

	ExMessage m;
	int lx, ly;

	std::chrono::high_resolution_clock::time_point MouseInteractionManipulated;
	while (!off_signal)
	{
		hiex::getmessage_win32(&m, EM_MOUSE, floating_window);

		if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - MouseInteractionManipulated).count() >= 180)
		{
			if ((int)state == 0)
			{
				if (IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 156, floating_windows.width - 96 + 96, floating_windows.height - 156 + 96 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (abs(m.x - lx) <= 5 && abs(m.y - ly) <= 5)
							{
								if (!m.lbutton)
								{
									target_status = 1;

									break;
								}
							}
							else
							{
								SeekBar(m);

								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}

						MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
					}

					if (m.rbutton)
					{
						if (MessageBox(floating_window, L"是否关闭 智绘教 ？", L"智绘教提示", MB_OKCANCEL | MB_SYSTEMMODAL) == 1) off_signal = true;
					}
				}

				if (!choose.select && (!RecallImage.empty() || (!FirstDraw && RecallImagePeak == 0)) && IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							hiex::getmessage_win32(&m, EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
							{
								if (!m.lbutton)
								{
									std::shared_lock<std::shared_mutex> lock1(PointTempSm);
									bool start = !TouchTemp.empty();
									lock1.unlock();
									if (start) break;

									pair<int, int> tmp_recond = make_pair(0, 0);
									int tmp_recall_image_type = 0;
									if (!RecallImage.empty())
									{
										tmp_recond = RecallImage.back().recond;
										tmp_recall_image_type = RecallImage.back().type;

										if (RecallImage.back().type == 2 && !choose.select && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img));
										else RecallImage.pop_back();
										deque<RecallStruct>(RecallImage).swap(RecallImage); // 使用swap技巧来释放未使用的内存
									}

									if (!RecallImage.empty())
									{
										drawpad = RecallImage.back().img;
										extreme_point = RecallImage.back().extreme_point;
										recall_image_recond = RecallImage.back().recond.first;
									}
									else if (tmp_recond.first > 10) goto SuperRecovery1;
									else
									{
										if (tmp_recall_image_type == 2) goto SuperRecovery1;
										SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
										extreme_point.clear();
										recall_image_recond = 0;
										FirstDraw = true;
									}
									SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
									hiex::TransparentImage(&window_background, 0, 0, &drawpad);

									if (!choose.select)
									{
										// 设置BLENDFUNCTION结构体
										BLENDFUNCTION blend;
										blend.BlendOp = AC_SRC_OVER;
										blend.BlendFlags = 0;
										blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
										blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
										HDC hdcScreen = GetDC(NULL);
										// 调用UpdateLayeredWindow函数更新窗口
										POINT ptSrc = { 0,0 };
										SIZE sizeWnd = { drawpad.getwidth(),drawpad.getheight() };
										POINT ptDst = { 0,0 }; // 设置窗口位置
										UPDATELAYEREDWINDOWINFO ulwi = { 0 };
										ulwi.cbSize = sizeof(ulwi);
										ulwi.hdcDst = hdcScreen;
										ulwi.pptDst = &ptDst;
										ulwi.psize = &sizeWnd;
										ulwi.pptSrc = &ptSrc;
										ulwi.crKey = RGB(255, 255, 255);
										ulwi.pblend = &blend;
										ulwi.dwFlags = ULW_ALPHA;

										// 定义要更新的矩形区域
										ulwi.hdcSrc = GetImageHDC(&window_background);
										UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
									}
									else
									{
										reserve_drawpad = true;

										brush.select = true;
										rubber.select = false;
										choose.select = false;
									}

									break;
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
				else if (!choose.select && RecallImage.empty() && current_record_pointer <= total_record_pointer + 1 && practical_total_record_pointer && IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							hiex::getmessage_win32(&m, EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
							{
								if (!m.lbutton)
								{
								SuperRecovery1:

									if (current_record_pointer == total_record_pointer + 1)
									{
										choose.select = true;
										brush.select = false;
										rubber.select = false;

										reference_record_pointer = 1;
										break;
									}
									//Testw(string_to_wstring(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString())).c_str());
									if (_access(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString()).c_str(), 4) == -1) break;

									filesystem::path pathObj(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString()));
									wstring file_name1 = pathObj.parent_path().filename().wstring();
									wstring file_name2 = pathObj.stem().wstring();

									std::wistringstream temp_wiss(file_name1 + L" " + file_name2);
									temp_wiss >> std::get_time(&RecallImageTm, L"%Y-%m-%d %H-%M-%S");

									FreezeRecall = 500;

									std::shared_lock<std::shared_mutex> lock1(PointTempSm);
									bool start = !TouchTemp.empty();
									lock1.unlock();
									if (start) break;

									IMAGE temp;
									loadimage(&temp, string_to_wstring(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString())).c_str(), drawpad.getwidth(), drawpad.getheight(), true);
									drawpad = temp, extreme_point = map<pair<int, int>, bool>();

									current_record_pointer++;

									SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
									hiex::TransparentImage(&window_background, 0, 0, &drawpad);

									if (brush.select)
									{
										// 设置BLENDFUNCTION结构体
										BLENDFUNCTION blend;
										blend.BlendOp = AC_SRC_OVER;
										blend.BlendFlags = 0;
										blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
										blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
										HDC hdcScreen = GetDC(NULL);
										// 调用UpdateLayeredWindow函数更新窗口
										POINT ptSrc = { 0,0 };
										SIZE sizeWnd = { drawpad.getwidth(),drawpad.getheight() };
										POINT ptDst = { 0,0 }; // 设置窗口位置
										UPDATELAYEREDWINDOWINFO ulwi = { 0 };
										ulwi.cbSize = sizeof(ulwi);
										ulwi.hdcDst = hdcScreen;
										ulwi.pptDst = &ptDst;
										ulwi.psize = &sizeWnd;
										ulwi.pptSrc = &ptSrc;
										ulwi.crKey = RGB(255, 255, 255);
										ulwi.pblend = &blend;
										ulwi.dwFlags = ULW_ALPHA;

										// 定义要更新的矩形区域
										ulwi.hdcSrc = GetImageHDC(&window_background);
										UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
									}
									else
									{
										reserve_drawpad = true;

										brush.select = true;
										rubber.select = false;
										choose.select = false;
									}

									break;
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
			}
			if ((int)state == 1)
			{
				if (IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 156, floating_windows.width - 96 + 96, floating_windows.height - 156 + 96 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (abs(m.x - lx) <= 5 && abs(m.y - ly) <= 5)
							{
								if (!m.lbutton)
								{
									state = 1;

									target_status = 0;

									break;
								}
							}
							else
							{
								SeekBar(m);

								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}

						MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
					}

					if (m.rbutton)
					{
						if (MessageBox(floating_window, L"是否关闭 智绘教 ？", L"智绘教提示", MB_OKCANCEL | MB_SYSTEMMODAL) == 1) off_signal = true;
					}
				}

				//窗口穿透
				if (choose.select == false && IsInRect(m.x, m.y, { floating_windows.width - 96 + 4, floating_windows.height - 256 + 8, floating_windows.width - 96 + 4 + 88, floating_windows.height - 256 + 8 + 40 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { floating_windows.width - 96 + 4, floating_windows.height - 256 + 8, floating_windows.width - 96 + 4 + 88, floating_windows.height - 256 + 8 + 40 }))
							{
								if (!m.lbutton)
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

									break;
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);

						MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
					}
				}
				//窗口定格
				if (ppt_show == NULL && IsInRect(m.x, m.y, { floating_windows.width - 96 + 4, floating_windows.height - 256 + 50, floating_windows.width - 96 + 4 + 88, floating_windows.height - 256 + 50 + 40 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { floating_windows.width - 96 + 4, floating_windows.height - 256 + 50, floating_windows.width - 96 + 4 + 88, floating_windows.height - 256 + 50 + 40 }))
							{
								if (!m.lbutton)
								{
									if (FreezeFrame.mode != 1)
									{
										penetrate.select = false;

										if (choose.select == true) FreezeFrame.select = true;
										FreezeFrame.mode = 1;
									}
									else FreezeFrame.mode = 0, FreezeFrame.select = false;

									break;
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);

						MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
					}
				}

				//选择
				if (IsInRect(m.x, m.y, { 0 + 8, floating_windows.height - 156 + 8, 0 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { 0 + 8, floating_windows.height - 156 + 8, 0 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
							{
								if (!m.lbutton)
								{
									if (choose.select == false)
									{
										state = 1;
										if (!FreezeFrame.select || penetrate.select) FreezeFrame.mode = 0, FreezeFrame.select = false;

										brush.select = false;
										rubber.select = false;
										choose.select = true;
										penetrate.select = false;
									}
									else if (choose.mode == true)
									{
										choose.mode = false;
									}
									else if (choose.mode == false)
									{
										choose.mode = true;
									}

									break;
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
				//画笔
				if (IsInRect(m.x, m.y, { 96 + 8, floating_windows.height - 156 + 8, 96 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						brush_connect = false;

						while (1)
						{
							ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { 96 + 8, floating_windows.height - 156 + 8, 96 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
							{
								if (abs(ly - m.y) >= 20 && state == 1)
								{
									brush.select = true;
									rubber.select = false;
									choose.select = false;
									state = 1.1, brush_connect = true;

									if (SeewoCameraIsOpen)
									{
										penetrate.select = false;
										FreezeFrame.mode = 1;
									}
								}
								else if (abs(ly - m.y) >= 20) brush_connect = true;
								else
								{
									if (!m.lbutton)
									{
										if (brush.select == false)
										{
											state = 1;
											brush.select = true;
											rubber.select = false;
											choose.select = false;

											if (SeewoCameraIsOpen)
											{
												penetrate.select = false;
												FreezeFrame.mode = 1;
											}
										}
										else if (state == 1)
										{
											state = 1.1;

											MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
										}
										else if ((state == 1.1 || state == 1.11 || state == 1.12) && ly - m.y < 20)
										{
											state = 1;

											MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
										}

										break;
									}
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
				//画笔选项1
				else if (state == 1.1 || state == 1.11 || state == 1.12)
				{
					if ((state == 1.1 || state == 1.11 || state == 1.12) && IsInRect(m.x, m.y, { int(UIControl[L"RoundRect/PaintThickness/x"].v), int(UIControl[L"RoundRect/PaintThickness/y"].v), int(UIControl[L"RoundRect/PaintThickness/x"].v + UIControl[L"RoundRect/PaintThickness/width"].v), int(UIControl[L"RoundRect/PaintThickness/y"].v + UIControl[L"RoundRect/PaintThickness/height"].v) }))
					{
						if (m.lbutton)
						{
							lx = m.x, ly = m.y;
							while (1)
							{
								ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
								if (IsInRect(m.x, m.y, { int(UIControl[L"RoundRect/PaintThickness/x"].v), int(UIControl[L"RoundRect/PaintThickness/y"].v), int(UIControl[L"RoundRect/PaintThickness/x"].v + UIControl[L"RoundRect/PaintThickness/width"].v), int(UIControl[L"RoundRect/PaintThickness/y"].v + UIControl[L"RoundRect/PaintThickness/height"].v) }))
								{
									if (!m.lbutton)
									{
										if (state == 1.1 || state == 1.12) state = 1.11;
										else state = 1.1;

										break;
									}
								}
								else
								{
									hiex::flushmessage_win32(EM_MOUSE, floating_window);

									break;
								}
							}
							hiex::flushmessage_win32(EM_MOUSE, floating_window);

							MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
						}
					}
					else if (state == 1.11 && IsInRect(m.x, m.y, { 15, floating_windows.height - 312 + 10, 355, floating_windows.height - 312 + 40 }))
					{
						if (m.lbutton)
						{
							POINT pt;

							while (1)
							{
								UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v = UIControlTarget[L"RoundRect/PaintThicknessSchedule3/height"].v = 10;

								GetCursorPos(&pt);

								int idx = max(10, min(320, pt.x - floating_windows.x - 17));

								if (idx <= 200) brush.width = 1 + int(double(idx - 10) / 190.0 * 49.0);
								else if (idx <= 260) brush.width = 51 + int(double(idx - 200) / 60.0 * 49.0);
								else brush.width = 101 + int(double(idx - 260) / 60.0 * 399.0);

								if (!KeyBoradDown[VK_LBUTTON]) break;
							}
							UIControlTarget[L"RoundRect/PaintThicknessSchedule3/width"].v = UIControlTarget[L"RoundRect/PaintThicknessSchedule3/height"].v = 20;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (state == 1.11 && IsInRect(m.x, m.y, { 355, floating_windows.height - 312 + 5, 455, floating_windows.height - 312 + 45 }))
					{
						if (brush.mode != 2)
						{
							if (IsInRect(m.x, m.y, { 365, floating_windows.height - 312 + 5, 395, floating_windows.height - 312 + 45 }))
							{
								if (m.lbutton)
								{
									int lx = m.x, ly = m.y;
									while (1)
									{
										ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
										if (IsInRect(m.x, m.y, { 365, floating_windows.height - 312 + 5, 395, floating_windows.height - 312 + 45 }))
										{
											if (!m.lbutton)
											{
												brush.width = 4;
												UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v = 1;

												break;
											}
										}
										else
										{
											hiex::flushmessage_win32(EM_MOUSE, floating_window);

											break;
										}
									}
									hiex::flushmessage_win32(EM_MOUSE, floating_window);
								}
							}
							if (IsInRect(m.x, m.y, { 395, floating_windows.height - 312 + 5, 425, floating_windows.height - 312 + 45 }))
							{
								if (m.lbutton)
								{
									int lx = m.x, ly = m.y;
									while (1)
									{
										ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
										if (IsInRect(m.x, m.y, { 395, floating_windows.height - 312 + 5, 425, floating_windows.height - 312 + 45 }))
										{
											if (!m.lbutton)
											{
												brush.width = 10;
												UIControlTarget[L"RoundRect/PaintThicknessSchedule5a/ellipse"].v = 2;

												break;
											}
										}
										else
										{
											hiex::flushmessage_win32(EM_MOUSE, floating_window);

											break;
										}
									}
									hiex::flushmessage_win32(EM_MOUSE, floating_window);
								}
							}
							if (IsInRect(m.x, m.y, { 425, floating_windows.height - 312 + 5, 455, floating_windows.height - 312 + 45 }))
							{
								if (m.lbutton)
								{
									int lx = m.x, ly = m.y;
									while (1)
									{
										ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
										if (IsInRect(m.x, m.y, { 425, floating_windows.height - 312 + 5, 455, floating_windows.height - 312 + 45 }))
										{
											if (!m.lbutton)
											{
												brush.width = 20;
												UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v = 10;

												break;
											}
										}
										else
										{
											hiex::flushmessage_win32(EM_MOUSE, floating_window);

											break;
										}
									}
									hiex::flushmessage_win32(EM_MOUSE, floating_window);
								}
							}
						}
						else
						{
							if (IsInRect(m.x, m.y, { 355, floating_windows.height - 312 + 5, 405, floating_windows.height - 312 + 45 }))
							{
								if (m.lbutton)
								{
									int lx = m.x, ly = m.y;
									while (1)
									{
										ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
										if (IsInRect(m.x, m.y, { 355, floating_windows.height - 312 + 5, 405, floating_windows.height - 312 + 45 }))
										{
											if (!m.lbutton)
											{
												brush.width = 35;
												UIControlTarget[L"RoundRect/PaintThicknessSchedule4a/ellipse"].v = 20;

												break;
											}
										}
										else
										{
											hiex::flushmessage_win32(EM_MOUSE, floating_window);

											break;
										}
									}
									hiex::flushmessage_win32(EM_MOUSE, floating_window);
								}
							}
							if (IsInRect(m.x, m.y, { 405, floating_windows.height - 312 + 5, 455, floating_windows.height - 312 + 45 }))
							{
								if (m.lbutton)
								{
									int lx = m.x, ly = m.y;
									while (1)
									{
										ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
										if (IsInRect(m.x, m.y, { 405, floating_windows.height - 312 + 5, 455, floating_windows.height - 312 + 45 }))
										{
											if (!m.lbutton)
											{
												brush.width = 50;
												UIControlTarget[L"RoundRect/PaintThicknessSchedule6a/ellipse"].v = 20;

												break;
											}
										}
										else
										{
											hiex::flushmessage_win32(EM_MOUSE, floating_window);

											break;
										}
									}
									hiex::flushmessage_win32(EM_MOUSE, floating_window);
								}
							}
						}
					}
					else if (state == 1.12 && IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColorChoose/x"].v, (int)UIControlTarget[L"RoundRect/BrushColorChoose/y"].v, (int)UIControlTarget[L"RoundRect/BrushColorChoose/x"].v + (int)UIControlTarget[L"RoundRect/BrushColorChoose/width"].v, (int)UIControlTarget[L"RoundRect/BrushColorChoose/y"].v + (int)UIControlTarget[L"RoundRect/BrushColorChoose/height"].v }))
					{
						if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v, (int)UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v, (int)UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v + (int)UIControlTarget[L"RoundRect/BrushColorChooseWheel/width"].v, (int)UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v + (int)UIControlTarget[L"RoundRect/BrushColorChooseWheel/height"].v }))
						{
							if (m.lbutton)
							{
								POINT center{ int(UIControlTarget[L"RoundRect/BrushColorChooseWheel/x"].v + UIControlTarget[L"RoundRect/BrushColorChooseWheel/width"].v / 2), int(UIControlTarget[L"RoundRect/BrushColorChooseWheel/y"].v + UIControlTarget[L"RoundRect/BrushColorChooseWheel/height"].v / 2) };
								POINT pt;

								BrushColorChoose.last_x = -1, BrushColorChoose.last_y = -1;
								while (1)
								{
									GetCursorPos(&pt);

									pt.x -= floating_windows.x;
									pt.y -= floating_windows.y;

									int len = min(int(UIControlTarget[L"RoundRect/BrushColorChooseWheel/width"].v / 2 - 2), int(sqrt(pow(center.x - pt.x, 2) + pow(center.y - pt.y, 2))));
									double length = sqrt(pow(center.x - pt.x, 2) + pow(center.y - pt.y, 2));

									POINT result;
									result.x = LONG(center.x + len * (pt.x - center.x) / length - UIControl[L"RoundRect/BrushColorChooseWheel/x"].v);
									result.y = LONG(center.y + len * (pt.y - center.y) / length - UIControl[L"RoundRect/BrushColorChooseWheel/y"].v);

									std::shared_lock<std::shared_mutex> lock(ColorPaletteSm);

									int width = ColorPaletteImg.getwidth();
									DWORD* pBuffer = GetImageBuffer(&ColorPaletteImg);
									DWORD colorValue = pBuffer[result.y * width + result.x];
									int blue = (colorValue & 0xFF);
									int green = (colorValue >> 8) & 0xFF;
									int red = (colorValue >> 16) & 0xFF;

									lock.unlock();

									brush.color = brush.primary_colour = RGBA(red, green, blue, (brush.color >> 24) & 0xFF);
									if (computeContrast(RGB(red, green, blue), RGB(255, 255, 255)) >= 3) BackgroundColorMode = 0;
									else BackgroundColorMode = 1;

									BrushColorChoose.x = result.x, BrushColorChoose.y = result.y;
									UIControlTarget[L"RoundRect/BrushColorChooseMark/x"].v = UIControl[L"RoundRect/BrushColorChooseMark/x"].v = result.x + UIControl[L"RoundRect/BrushColorChooseWheel/x"].v - 7;
									UIControlTarget[L"RoundRect/BrushColorChooseMark/y"].v = UIControl[L"RoundRect/BrushColorChooseMark/y"].v = result.y + UIControl[L"RoundRect/BrushColorChooseWheel/y"].v - 7;

									if (!KeyBoradDown[VK_LBUTTON]) break;
								}
								BrushColorChoose.last_x = BrushColorChoose.x, BrushColorChoose.last_y = BrushColorChoose.y;

								hiex::flushmessage_win32(EM_MOUSE, floating_window);
							}
						}
					}

					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor1/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor1/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor1/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor1/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor1/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor1/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor1/fill"].v, 255);
							BackgroundColorMode = 1;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor2/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor2/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor2/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor2/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor2/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor2/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor2/fill"].v, 255);
							BackgroundColorMode = 0;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor3/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor3/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor3/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor3/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor3/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor3/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor3/fill"].v, 255);
							BackgroundColorMode = 1;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor4/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor4/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor4/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor4/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor4/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor4/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor4/fill"].v, 255);
							BackgroundColorMode = 0;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor5/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor5/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor5/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor5/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor5/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor5/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor5/fill"].v, 255);
							BackgroundColorMode = 1;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor6/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor6/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor6/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor6/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor6/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor6/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor6/fill"].v, 255);
							BackgroundColorMode = 0;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor7/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor7/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor7/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor7/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor7/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor7/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor7/fill"].v, 255);
							BackgroundColorMode = 1;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor8/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor8/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor8/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor8/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor8/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor8/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor8/fill"].v, 255);
							BackgroundColorMode = 0;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor9/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor9/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor9/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor9/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor9/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor9/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor9/fill"].v, 255);
							BackgroundColorMode = 1;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor10/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor10/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor10/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor10/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor10/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor10/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor10/fill"].v, 255);
							BackgroundColorMode = 0;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor11/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor11/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor11/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor11/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor11/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor11/height"].v }))
					{
						if (m.lbutton)
						{
							brush.color = brush.primary_colour = SET_ALPHA(UIControlColor[L"RoundRect/BrushColor11/fill"].v, 255);
							BackgroundColorMode = 1;

							BrushColorChoose.x = BrushColorChoose.y = 0;
							if (state == 1.11 || state == 1.12) state = 1.1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor12/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor12/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor12/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor12/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor12/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor12/height"].v }))
					{
						if (m.lbutton)
						{
							int lx = m.x, ly = m.y;
							while (1)
							{
								ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
								if (IsInRect(m.x, m.y, { (int)UIControlTarget[L"RoundRect/BrushColor12/x"].v, (int)UIControlTarget[L"RoundRect/BrushColor12/y"].v, (int)UIControlTarget[L"RoundRect/BrushColor12/x"].v + (int)UIControlTarget[L"RoundRect/BrushColor12/width"].v, (int)UIControlTarget[L"RoundRect/BrushColor12/y"].v + (int)UIControlTarget[L"RoundRect/BrushColor12/height"].v }))
								{
									if (!m.lbutton)
									{
										if (state == 1.12) state = 1.1;
										else state = 1.12;

										break;
									}
								}
								else
								{
									hiex::flushmessage_win32(EM_MOUSE, floating_window);

									break;
								}
							}
							hiex::flushmessage_win32(EM_MOUSE, floating_window);

							MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
						}
					}

					if (IsInRect(m.x, m.y, { 5, floating_windows.height - 55 + 5, 5 + 90, floating_windows.height - 55 + 5 + 30 }))
					{
						if (m.lbutton)
						{
							if (brush.mode == 2)
							{
								brush.HighlighterWidthHistory = brush.width;
								brush.width = brush.PenWidthHistory;
							}
							brush.mode = 1;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { 95, floating_windows.height - 55 + 5, 95 + 90, floating_windows.height - 55 + 5 + 30 }))
					{
						if (m.lbutton)
						{
							if (brush.mode != 2)
							{
								brush.PenWidthHistory = brush.width;
								brush.width = brush.HighlighterWidthHistory;
							}
							brush.mode = 2;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { 195, floating_windows.height - 55 + 5,195 + 90, floating_windows.height - 55 + 5 + 30 }))
					{
						if (m.lbutton)
						{
							if (brush.mode != 1 && brush.mode != 2)
							{
								brush.mode = 1;
							}

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { 285, floating_windows.height - 55 + 5,285 + 90, floating_windows.height - 55 + 5 + 30 }))
					{
						if (m.lbutton)
						{
							if (brush.mode == 2)
							{
								brush.HighlighterWidthHistory = brush.width;
								brush.width = brush.PenWidthHistory;
							}
							brush.mode = 3;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (IsInRect(m.x, m.y, { 375, floating_windows.height - 55 + 5,375 + 90, floating_windows.height - 55 + 5 + 30 }))
					{
						if (m.lbutton)
						{
							if (brush.mode == 2)
							{
								brush.HighlighterWidthHistory = brush.width;
								brush.width = brush.PenWidthHistory;
							}
							brush.mode = 4;

							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}

					if (!m.lbutton && (IsInRect(m.x, m.y, { 1, floating_windows.height - 256, 1 + floating_windows.width - 106, floating_windows.height - 256 + 90 }) || IsInRect(m.x, m.y, { 0, floating_windows.height - 50, 0 + floating_windows.width, floating_windows.height - 50 + 50 })) && brush_connect) state = 1;
				}
				//橡皮
				if (rubber.select == false && IsInRect(m.x, m.y, { 192 + 8, floating_windows.height - 156 + 8, 192 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { 192 + 8, floating_windows.height - 156 + 8, 192 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
							{
								if (!m.lbutton)
								{
									state = 1;
									rubber.select = true;
									brush.select = false;
									choose.select = false;

									if (SeewoCameraIsOpen)
									{
										penetrate.select = false;
										FreezeFrame.mode = 1;
									}

									break;
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);
					}
				}
				//撤回画板
				{
					if ((!RecallImage.empty() || (!FirstDraw && RecallImagePeak == 0)) && IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
					{
						if (m.lbutton)
						{
							lx = m.x, ly = m.y;
							while (1)
							{
								hiex::getmessage_win32(&m, EM_MOUSE, floating_window);
								if (IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
								{
									if (!m.lbutton)
									{
										std::shared_lock<std::shared_mutex> lock1(PointTempSm);
										bool start = !TouchTemp.empty();
										lock1.unlock();
										if (start) break;

										pair<int, int> tmp_recond = make_pair(0, 0);
										int tmp_recall_image_type = 0;
										if (!RecallImage.empty())
										{
											tmp_recond = RecallImage.back().recond;
											tmp_recall_image_type = RecallImage.back().type;

											if (RecallImage.back().type == 2 && !choose.select && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img));
											else RecallImage.pop_back();
											deque<RecallStruct>(RecallImage).swap(RecallImage); // 使用swap技巧来释放未使用的内存
										}

										if (!RecallImage.empty())
										{
											drawpad = RecallImage.back().img;
											extreme_point = RecallImage.back().extreme_point;
											recall_image_recond = RecallImage.back().recond.first;
										}
										else if (tmp_recond.first > 10) goto SuperRecovery2;
										else
										{
											if (tmp_recall_image_type == 2) goto SuperRecovery2;
											SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
											extreme_point.clear();
											recall_image_recond = 0;
											FirstDraw = true;
										}
										SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
										hiex::TransparentImage(&window_background, 0, 0, &drawpad);

										if (!choose.select)
										{
											// 设置BLENDFUNCTION结构体
											BLENDFUNCTION blend;
											blend.BlendOp = AC_SRC_OVER;
											blend.BlendFlags = 0;
											blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
											blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
											HDC hdcScreen = GetDC(NULL);
											// 调用UpdateLayeredWindow函数更新窗口
											POINT ptSrc = { 0,0 };
											SIZE sizeWnd = { drawpad.getwidth(),drawpad.getheight() };
											POINT ptDst = { 0,0 }; // 设置窗口位置
											UPDATELAYEREDWINDOWINFO ulwi = { 0 };
											ulwi.cbSize = sizeof(ulwi);
											ulwi.hdcDst = hdcScreen;
											ulwi.pptDst = &ptDst;
											ulwi.psize = &sizeWnd;
											ulwi.pptSrc = &ptSrc;
											ulwi.crKey = RGB(255, 255, 255);
											ulwi.pblend = &blend;
											ulwi.dwFlags = ULW_ALPHA;

											// 定义要更新的矩形区域
											ulwi.hdcSrc = GetImageHDC(&window_background);
											UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
										}
										else
										{
											reserve_drawpad = true;

											brush.select = true;
											rubber.select = false;
											choose.select = false;
										}

										break;
									}
								}
								else
								{
									hiex::flushmessage_win32(EM_MOUSE, floating_window);

									break;
								}
							}
							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
					else if (RecallImage.empty() && current_record_pointer <= total_record_pointer + 1 && practical_total_record_pointer && IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
					{
						if (m.lbutton)
						{
							lx = m.x, ly = m.y;
							while (1)
							{
								hiex::getmessage_win32(&m, EM_MOUSE, floating_window);
								if (IsInRect(m.x, m.y, { floating_windows.width - 96, floating_windows.height - 55, floating_windows.width - 96 + 96, floating_windows.height - 50 + 40 }))
								{
									if (!m.lbutton)
									{
									SuperRecovery2:

										if (current_record_pointer == total_record_pointer + 1)
										{
											choose.select = true;
											brush.select = false;
											rubber.select = false;

											reference_record_pointer = 1;
											break;
										}
										//Testw(string_to_wstring(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString())).c_str());
										if (_access(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString()).c_str(), 4) == -1) break;

										filesystem::path pathObj(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString()));
										wstring file_name1 = pathObj.parent_path().filename().wstring();
										wstring file_name2 = pathObj.stem().wstring();

										std::wistringstream temp_wiss(file_name1 + L" " + file_name2);
										temp_wiss >> std::get_time(&RecallImageTm, L"%Y-%m-%d %H-%M-%S");

										FreezeRecall = 500;

										std::shared_lock<std::shared_mutex> lock1(PointTempSm);
										bool start = !TouchTemp.empty();
										lock1.unlock();
										if (start) break;

										IMAGE temp;
										loadimage(&temp, string_to_wstring(convert_to_gbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString())).c_str(), drawpad.getwidth(), drawpad.getheight(), true);
										drawpad = temp, extreme_point = map<pair<int, int>, bool>();

										current_record_pointer++;

										SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
										hiex::TransparentImage(&window_background, 0, 0, &drawpad);

										if (brush.select)
										{
											// 设置BLENDFUNCTION结构体
											BLENDFUNCTION blend;
											blend.BlendOp = AC_SRC_OVER;
											blend.BlendFlags = 0;
											blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
											blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
											HDC hdcScreen = GetDC(NULL);
											// 调用UpdateLayeredWindow函数更新窗口
											POINT ptSrc = { 0,0 };
											SIZE sizeWnd = { drawpad.getwidth(),drawpad.getheight() };
											POINT ptDst = { 0,0 }; // 设置窗口位置
											UPDATELAYEREDWINDOWINFO ulwi = { 0 };
											ulwi.cbSize = sizeof(ulwi);
											ulwi.hdcDst = hdcScreen;
											ulwi.pptDst = &ptDst;
											ulwi.psize = &sizeWnd;
											ulwi.pptSrc = &ptSrc;
											ulwi.crKey = RGB(255, 255, 255);
											ulwi.pblend = &blend;
											ulwi.dwFlags = ULW_ALPHA;

											// 定义要更新的矩形区域
											ulwi.hdcSrc = GetImageHDC(&window_background);
											UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
										}
										else
										{
											reserve_drawpad = true;

											brush.select = true;
											rubber.select = false;
											choose.select = false;
										}

										break;
									}
								}
								else
								{
									hiex::flushmessage_win32(EM_MOUSE, floating_window);

									break;
								}
							}
							hiex::flushmessage_win32(EM_MOUSE, floating_window);
						}
					}
				}
				//选项
				if (IsInRect(m.x, m.y, { 288 + 8, floating_windows.height - 156 + 8, 288 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
				{
					if (m.lbutton)
					{
						lx = m.x, ly = m.y;
						while (1)
						{
							ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
							if (IsInRect(m.x, m.y, { 288 + 8, floating_windows.height - 156 + 8, 288 + 8 + 80, floating_windows.height - 156 + 8 + 80 }))
							{
								if (!m.lbutton)
								{
									if (test.select) test.select = false;
									else test.select = true;

									break;
								}
							}
							else
							{
								hiex::flushmessage_win32(EM_MOUSE, floating_window);

								break;
							}
						}
						hiex::flushmessage_win32(EM_MOUSE, floating_window);

						MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
					}
				}

				//插件：随机点名
				if (plug_in_RandomRollCall.select == 2)
				{
					if (IsInRect(m.x, m.y, { 2, floating_windows.height - 55, 2 + 100, floating_windows.height - 55 + 40 }))
					{
						if (m.lbutton)
						{
							lx = m.x, ly = m.y;
							while (1)
							{
								ExMessage m = hiex::getmessage_win32(EM_MOUSE, floating_window);
								if (IsInRect(m.x, m.y, { 2, floating_windows.height - 55, 2 + 100, floating_windows.height - 55 + 40 }))
								{
									if (!m.lbutton)
									{
										if (_waccess((string_to_wstring(global_path) + L"plug-in\\随机点名\\随机点名.exe").c_str(), 0) == 0 && !isProcessRunning((string_to_wstring(global_path) + L"plug-in\\随机点名\\随机点名.exe").c_str()))
										{
											STARTUPINFOA si = { 0 };
											si.cb = sizeof(si);
											PROCESS_INFORMATION pi = { 0 };
											CreateProcessA(NULL, (global_path + "plug-in\\随机点名\\随机点名.exe").data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
											CloseHandle(pi.hProcess);
											CloseHandle(pi.hThread);
										}

										break;
									}
								}
								else
								{
									hiex::flushmessage_win32(EM_MOUSE, floating_window);

									break;
								}
							}
							hiex::flushmessage_win32(EM_MOUSE, floating_window);

							MouseInteractionManipulated = std::chrono::high_resolution_clock::now();
						}
					}
				}
			}
		}
		else hiex::flushmessage_win32(EM_MOUSE, floating_window);
	}

	thread_status[L"MouseInteraction"] = false;
}

int floating_main()
{
	thread_status[L"floating_main"] = true;
	GetLocalTime(&sys_time);

	thread FloatingInstallHookThread(FloatingInstallHook);
	FloatingInstallHookThread.detach();
	thread PPTLinkageMainThread(PPTLinkageMain);
	PPTLinkageMainThread.detach();

	//thread GetTime_thread(GetTime);
	//GetTime_thread.detach();
	//LOG(INFO) << "尝试启动悬浮窗窗口绘制线程";
	thread DrawScreen_thread(DrawScreen);
	DrawScreen_thread.detach();
	//LOG(INFO) << "成功启动悬浮窗窗口绘制线程";

	//LOG(INFO) << "尝试启动黑名单窗口拦截线程";
	thread BlackBlock_thread(BlackBlock);
	BlackBlock_thread.detach();
	//LOG(INFO) << "成功启动黑名单窗口拦截线程";

#ifdef IDT_RELEASE

	//LOG(INFO) << "尝试检查并补齐本地文件";
	if (_waccess((string_to_wstring(global_path) + L"api").c_str(), 0) == -1)
	{
		//创建路径
		filesystem::create_directory(string_to_wstring(global_path) + L"api");

		//if (_waccess((string_to_wstring(global_path) + L"api\\智绘教CrashedHandler.exe").c_str(), 0) == -1 || _waccess((string_to_wstring(global_path) + L"api\\智绘教CrashedHandlerClose.exe").c_str(), 0) == -1)
		//{
		//	ExtractResource((string_to_wstring(global_path) + L"api\\智绘教CrashedHandler.exe").c_str(), L"EXE", MAKEINTRESOURCE(201));
		//	ExtractResource((string_to_wstring(global_path) + L"api\\智绘教CrashedHandlerClose.exe").c_str(), L"EXE", MAKEINTRESOURCE(202));
		//}
	}
	ExtractResource((string_to_wstring(global_path) + L"api\\智绘教CrashedHandler.exe").c_str(), L"EXE", MAKEINTRESOURCE(201));
	ExtractResource((string_to_wstring(global_path) + L"api\\智绘教CrashedHandlerClose.exe").c_str(), L"EXE", MAKEINTRESOURCE(202));

	//LOG(INFO) << "成功检查并补齐本地文件";

	/*
	//注册icu
	if (_waccess((string_to_wstring(global_path) + L"icudt73.dll").c_str(), 0) == -1 || _waccess((string_to_wstring(global_path) + L"icuin73.dll").c_str(), 0) == -1 || _waccess((string_to_wstring(global_path) + L"icuuc73.dll").c_str(), 0) == -1)
	{
		ExtractResource((string_to_wstring(global_path) + L"icudt73.dll").c_str(), L"DLL", MAKEINTRESOURCE(207));
		ExtractResource((string_to_wstring(global_path) + L"icuin73.dll").c_str(), L"DLL", MAKEINTRESOURCE(208));
		ExtractResource((string_to_wstring(global_path) + L"icuuc73.dll").c_str(), L"DLL", MAKEINTRESOURCE(209));
	}
	*/

	//LOG(INFO) << "尝试启动程序崩溃反馈线程";
	thread CrashedHandler_thread(CrashedHandler);
	CrashedHandler_thread.detach();
	//LOG(INFO) << "成功启动程序崩溃反馈线程";
	//LOG(INFO) << "尝试启动程序自动更新线程";

	thread AutomaticUpdate_thread(AutomaticUpdate);
	AutomaticUpdate_thread.detach();

	//LOG(INFO) << "成功启动程序自动更新线程";
#endif

	//LOG(INFO) << "进入悬浮窗窗口交互线程";
	thread MouseInteractionThread(MouseInteraction);
	MouseInteractionThread.detach();

	while (!off_signal) Sleep(500);

	int i = 1;
	for (; i <= 10; i++)
	{
		if (!thread_status[L"CrashedHandler"] && !thread_status[L"PPTLinkageMain"]/*&& !thread_status[L"GetPptState"] && !thread_status[L"ControlManipulation"] */ && !thread_status[L"GetTime"] && !thread_status[L"DrawScreen"] && !thread_status[L"api_read_pipe"] && !thread_status[L"BlackBlock"]) break;
		Sleep(500);
	}

	thread_status[L"floating_main"] = false;
	return 0;
}