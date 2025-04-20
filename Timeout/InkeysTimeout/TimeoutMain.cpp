#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <windows.h>
#include "resource.h"
#include <list>
#include <string>
#include <stdexcept>
#include "Win32FontManager.h"
#include <mmsystem.h>
#include "i18n.h"
#include <chrono>
#pragma comment (lib,"winmm.lib")

using namespace std;

HWND hWnd;
list<pair<HWND, Win32FontManager*>> Controls;
struct Time {
	int h, m, s;
} timer_time{ 0,1,0 }, countup_time{ 0,0,0 }, countdown_time{ 0,0,0 };
enum TimerEnum { Countdown, Countup };
struct Timer {
	bool start;
	TimerEnum mode;
} timer{false, Countdown};
bool isSettingShow = false;
bool isPaused = false;
float UIZoom = 1.0f;
int TimerDelay = 0;
int TimerBeginTime = 0;  // 计时开始时那一时刻的毫秒
int getNowMillsecond();
#define ID_TIMER 1145

#define TIME_LABEL 1001

#define HOURS_PLUS1 1002
#define HOURS_PLUS5 1003
#define HOURS_MINUS1 1004
#define HOURS_MINUS5 1005

#define MINUTE_PLUS1 1006
#define MINUTE_PLUS5 1007
#define MINUTE_MINUS1 1008
#define MINUTE_MINUS5 1009

#define SECOND_PLUS1 1010
#define SECOND_PLUS5 1011
#define SECOND_MINUS1 1012
#define SECOND_MINUS5 1013

#define SAVE 1014
#define START_CONUTDOWN 1015
#define START_CONUTUP 1016
#define PAUSE 1017
#define RESET 1018
#define STOP 1019

#define UPDATE_TIME SetWindowText(at(Controls, 0).first, ((to_wstring(timer_time.h).length() == 1 ? L"0" : L"") + to_wstring(timer_time.h) + L":" + (to_wstring(timer_time.m).length() == 1 ? L"0" : L"") + to_wstring(timer_time.m) + L":" + (to_wstring(timer_time.s).length() == 1 ? L"0" : L"") + to_wstring(timer_time.s)).c_str());
#define UPDATE_TIME_COUNTUP SetWindowText(at(Controls, 0).first, ((to_wstring(countup_time.h).length() == 1 ? L"0" : L"") + to_wstring(countup_time.h) + L":" + (to_wstring(countup_time.m).length() == 1 ? L"0" : L"") + to_wstring(countup_time.m) + L":" + (to_wstring(countup_time.s).length() == 1 ? L"0" : L"") + to_wstring(countup_time.s)).c_str());
#define UPDATE_TIME_COUNTDOWN SetWindowText(at(Controls, 0).first, ((to_wstring(countdown_time.h).length() == 1 ? L"0" : L"") + to_wstring(countdown_time.h) + L":" + (to_wstring(countdown_time.m).length() == 1 ? L"0" : L"") + to_wstring(countdown_time.m) + L":" + (to_wstring(countdown_time.s).length() == 1 ? L"0" : L"") + to_wstring(countdown_time.s)).c_str());

template<class T>
T at(const list<T>& l, int index) {
	if (index < 0 || index >= static_cast<int>(l.size())) {
		throw out_of_range("Index out of bounds");
	}

	auto it = l.begin();
	advance(it, index);
	return *it;
}

LRESULT CALLBACK WndProc(HWND hwNd, UINT Message, WPARAM wParam, LPARAM lParam) {
	switch (Message) {
	case WM_CREATE: {
		SetWindowPos(hwNd, HWND_TOPMOST, 0, 0, 100, 100, SWP_NOMOVE | SWP_NOSIZE);
		{
			HWND hwnd = CreateWindow(L"static", L"00:01:00", WS_CHILD | WS_VISIBLE | BS_CENTER | SS_CENTER, 0 * UIZoom, 185 * UIZoom, 640 * UIZoom, 70 * UIZoom, hwNd, (HMENU)TIME_LABEL, GetModuleHandle(0), NULL);
			Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 75 * UIZoom)));
		}
		{
			// 小时
			{
				HWND hwnd = CreateWindow(L"button", L"+1", WS_CHILD | BS_CENTER | SS_CENTER, 170 * UIZoom, 150 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)HOURS_PLUS1, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"+5", WS_CHILD | BS_CENTER | SS_CENTER, 170 * UIZoom, 110 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)HOURS_PLUS5, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"-1", WS_CHILD | BS_CENTER | SS_CENTER, 170 * UIZoom, 260 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)HOURS_MINUS1, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"-5", WS_CHILD | BS_CENTER | SS_CENTER, 170 * UIZoom, 300 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)HOURS_MINUS5, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
		}
		{
			// 分钟
			{
				HWND hwnd = CreateWindow(L"button", L"+1", WS_CHILD | BS_CENTER | SS_CENTER, 280 * UIZoom, 150 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)MINUTE_PLUS1, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"+5", WS_CHILD | BS_CENTER | SS_CENTER, 280 * UIZoom, 110 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)MINUTE_PLUS5, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"-1", WS_CHILD | BS_CENTER | SS_CENTER, 280 * UIZoom, 260 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)MINUTE_MINUS1, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"-5", WS_CHILD | BS_CENTER | SS_CENTER, 280 * UIZoom, 300 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)MINUTE_MINUS5, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
		}
		{
			// 秒钟
			{
				HWND hwnd = CreateWindow(L"button", L"+1", WS_CHILD | BS_CENTER | SS_CENTER, 390 * UIZoom, 150 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)SECOND_PLUS1, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"+5", WS_CHILD | BS_CENTER | SS_CENTER, 390 * UIZoom, 110 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)SECOND_PLUS5, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"-1", WS_CHILD | BS_CENTER | SS_CENTER, 390 * UIZoom, 260 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)SECOND_MINUS1, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
			{
				HWND hwnd = CreateWindow(L"button", L"-5", WS_CHILD | BS_CENTER | SS_CENTER, 390 * UIZoom, 300 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)SECOND_MINUS5, GetModuleHandle(0), NULL);
				Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
			}
		}
		{
			HWND hwnd = CreateWindowA("button", i18n_get("save").c_str(), WS_CHILD | BS_CENTER | SS_CENTER, 280 * UIZoom, 360 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)SAVE, GetModuleHandle(0), NULL);
			Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
		}
		{
			HWND hwnd = CreateWindowA("button", i18n_get("start_countdown").c_str(), WS_CHILD | WS_VISIBLE | BS_CENTER | SS_CENTER, 190 * UIZoom, 360 * UIZoom, 120 * UIZoom, 35 * UIZoom, hwNd, (HMENU)START_CONUTDOWN, GetModuleHandle(0), NULL);
			Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
		}
		{
			HWND hwnd = CreateWindowA("button", i18n_get("start_countup").c_str(), WS_CHILD | WS_VISIBLE | BS_CENTER | SS_CENTER, 330 * UIZoom, 360 * UIZoom, 120 * UIZoom, 35 * UIZoom, hwNd, (HMENU)START_CONUTUP, GetModuleHandle(0), NULL);
			Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
		}
		{
			HWND hwnd = CreateWindowA("button", i18n_get("pause").c_str(), WS_CHILD | BS_CENTER | SS_CENTER, 170 * UIZoom, 360 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)PAUSE, GetModuleHandle(0), NULL);
			Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
		}
		{
			HWND hwnd = CreateWindowA("button", i18n_get("reset").c_str(), WS_CHILD | BS_CENTER | SS_CENTER, 280 * UIZoom, 360 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)RESET, GetModuleHandle(0), NULL);
			Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
		}
		{
			HWND hwnd = CreateWindowA("button", i18n_get("stop").c_str(), WS_CHILD | BS_CENTER | SS_CENTER, 390 * UIZoom, 360 * UIZoom, 80 * UIZoom, 35 * UIZoom, hwNd, (HMENU)STOP, GetModuleHandle(0), NULL);
			Controls.push_back(make_pair(hwnd, new Win32FontManager(hwnd, 15 * UIZoom)));
		}
		break;
	}
	case WM_COMMAND: {
		switch (LOWORD(wParam)) {
		case TIME_LABEL:
		case SAVE: {
			if (!timer.start) {
				isSettingShow = !isSettingShow;
				for (int i = 0; i < 13; i++)
					ShowWindow(at(Controls, i + 1).first, isSettingShow ? SW_SHOW : SW_HIDE);
				for (int i = 13; i < 15; i++)
					ShowWindow(at(Controls, i + 1).first, !isSettingShow ? SW_SHOW : SW_HIDE);
				if (!(timer_time.h || timer_time.m || timer_time.s)) timer_time.s = 1;
			}
			break;
		}
		case HOURS_PLUS1:
			timer_time.h++;
			break;
		case HOURS_PLUS5:
			timer_time.h += 5;
			break;
		case HOURS_MINUS1:
			timer_time.h--;
			break;
		case HOURS_MINUS5:
			timer_time.m -= 5;
			break;
		case MINUTE_PLUS1:
			timer_time.m++;
			break;
		case MINUTE_PLUS5:
			timer_time.m += 5;
			break;
		case MINUTE_MINUS1:
			timer_time.m--;
			break;
		case MINUTE_MINUS5:
			timer_time.m -= 5;
			break;
		case SECOND_PLUS1:
			timer_time.s++;
			break;
		case SECOND_PLUS5:
			timer_time.s += 5;
			break;
		case SECOND_MINUS1:
			timer_time.s--;
			break;
		case SECOND_MINUS5:
			timer_time.s -= 5;
			break;
		case START_CONUTDOWN:
			if (!isSettingShow && !timer.start) {
				countdown_time = timer_time;
				timer.start = true;
				isPaused = false;
				TimerDelay = 0;
				TimerBeginTime = getNowMillsecond();
				SetWindowTextA(at(Controls, 16).first, i18n_get("pause").c_str());
				timer.mode = Countdown;
				SetTimer(hwNd, ID_TIMER, 1000, NULL);
				for (int i = 13; i < 15; i++)
					ShowWindow(at(Controls, i + 1).first, SW_HIDE);
				for (int i = 15; i < 18; i++)
					ShowWindow(at(Controls, i + 1).first, SW_SHOW);
			}
			break;
		case START_CONUTUP:
			if (!isSettingShow && !timer.start) {
				countup_time = { 0,0,0 };
				timer.start = true;
				isPaused = false;
				TimerDelay = 0;
				TimerBeginTime = getNowMillsecond();
				SetWindowTextA(at(Controls, 16).first, i18n_get("pause").c_str());
				timer.mode = Countup;
				UPDATE_TIME_COUNTUP;
				SetTimer(hwNd, ID_TIMER, 1000, NULL);
				for (int i = 13; i < 15; i++)
					ShowWindow(at(Controls, i + 1).first, SW_HIDE);
				for (int i = 15; i < 18; i++)
					ShowWindow(at(Controls, i + 1).first, SW_SHOW);
			}
			break;
		case STOP:
			if (!isSettingShow && timer.start) {
				timer.start = false;
				isPaused = false;
				SetWindowTextA(at(Controls, 16).first, i18n_get("pause").c_str());
				KillTimer(hwNd, ID_TIMER);
				for (int i = 13; i < 15; i++)
					ShowWindow(at(Controls, i + 1).first, SW_SHOW);
				for (int i = 15; i < 18; i++)
					ShowWindow(at(Controls, i + 1).first, SW_HIDE);
				UPDATE_TIME;
			}
			break;
	    case RESET:
			if (!isSettingShow && timer.start) {
				if (timer.mode == Countdown) { countdown_time = timer_time; UPDATE_TIME_COUNTDOWN; }
				else { countup_time = { 0,0,0 }; UPDATE_TIME_COUNTUP; }
			}
			break;
	    case PAUSE: 
			isPaused = !isPaused;
			if (isPaused) 
			{
				SetWindowTextA(at(Controls, 16).first, i18n_get("continue").c_str()); 
				TimerDelay = (getNowMillsecond() - TimerBeginTime + 1000) % 1000;
				KillTimer(hwNd, ID_TIMER);
			}
			else { 
				SetWindowTextA(at(Controls, 16).first, i18n_get("pause").c_str());
				SetTimer(hwNd, ID_TIMER, TimerDelay, NULL);
			}
			break;
		}

		if (timer_time.h < 0) timer_time.h = 100 + timer_time.h % 100;
		if (timer_time.h >= 100) timer_time.h %= 100;
		if (timer_time.m < 0) timer_time.m = 60 + timer_time.m % 60;
		if (timer_time.m >= 60) timer_time.m %= 60;
		if (timer_time.s < 0) timer_time.s = 60 + timer_time.s % 60;
		if (timer_time.s >= 60) timer_time.s %= 60;

		if (!timer.start) { UPDATE_TIME; }
		break;
	}
	case WM_CTLCOLORSTATIC: {
		if (timer.start && timer.mode == Countdown && countdown_time.h == 0 && countdown_time.m == 0 && countdown_time.s <= 10) {
		    HDC hdc = (HDC)wParam;
			SetTextColor(hdc, RGB(255, 0, 0));
		}
		break;
	}
	case WM_TIMER: {
		if (LOWORD(wParam) == ID_TIMER && timer.start && !isPaused) {
			if (TimerDelay != 0) {
				KillTimer(hwNd, ID_TIMER);
				TimerDelay = 0;
				SetTimer(hwNd, ID_TIMER, 1000, NULL);
			}
			if(timer.mode == Countdown) {
				countdown_time.s--;
				if (countdown_time.s < 0) {
					countdown_time.m--;
					countdown_time.s += 60;
				}
				if (countdown_time.m < 0) {
					countdown_time.h--;
					countdown_time.m += 60;
				}
				
				if (countdown_time.h == 0 && countdown_time.m == 0 && countdown_time.s == 0) {
					// 时间到
					timer.start = false;
					KillTimer(hwNd, ID_TIMER);
					for (int i = 13; i < 15; i++)
						ShowWindow(at(Controls, i + 1).first, SW_SHOW);
					for (int i = 15; i < 18; i++)
						ShowWindow(at(Controls, i + 1).first, SW_HIDE);
					if (!PlaySound(MAKEINTRESOURCE(IDR_WAVE1), NULL, SND_RESOURCE | SND_ASYNC)) {
						// 播放失败, 只能用MessageBox提醒了, QAQ
						MessageBoxA(hwNd, i18n_get("timeout_tip").c_str(), i18n_get("tip").c_str(), MB_ICONINFORMATION);
					}
					UPDATE_TIME;
					break;
				}
				UPDATE_TIME_COUNTDOWN;
			}
			else if (timer.mode == Countup) {
				countup_time.s++;
				if (countup_time.s >= 60) {
					countup_time.m++;
					countup_time.s = 0;
				}
				if (countup_time.m >= 60) {
					countup_time.h++;
					countup_time.m = 0;
				}
				UPDATE_TIME_COUNTUP;
			}
		}
		break;
	}
	case WM_ERASEBKGND:{
		HDC hdc;
		RECT rect;
		HBRUSH hBrush;
		hdc = (HDC)wParam;
		GetClientRect(hwNd, &rect);
		hBrush = CreateSolidBrush(RGB(255, 255, 255));
		FillRect(hdc, &rect, hBrush);
		DeleteObject(hBrush);
		return 1;
	}
	case WM_CLOSE:
		if (timer.start) {
			if (MessageBoxA(hwNd, i18n_get("exit_tip").c_str(), i18n_get("tip").c_str(), MB_ICONQUESTION | MB_YESNO) == IDNO) return 0;
			else DestroyWindow(hwNd);
		} else DestroyWindow(hwNd);
		break;
	case WM_DESTROY: {
		PostQuitMessage(0);
		break;
	}
	default:
		return DefWindowProc(hwNd, Message, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	list<wstring> args;
	{
		LPWSTR* szArgList;
		int argCount;


		szArgList = CommandLineToArgvW(GetCommandLine(), &argCount);

		if (szArgList != NULL)
			for (int i = 1; i < argCount; i++)
			{
				args.push_back(wstring(szArgList[i]));
			}
		LocalFree(szArgList);
	}

	{
		auto it = std::find(args.begin(), args.end(), L"/Z");
		if (it != args.end() && std::next(it) != args.end()) {
			std::wstring zoom = *std::next(it);
			int Zoom = 0;
			for (int i = 0; i < zoom.length(); i++) {
				if (zoom[i] > '9' || zoom[i] < '0')
					break;
				else {
					Zoom *= 10;
					Zoom += zoom[i] - '0';
				}
			}
		    UIZoom = 1.0 * Zoom / 100.0;
			if (UIZoom > 2.0) UIZoom = 2.0;
			if (UIZoom < 1.0) UIZoom = 1.0;
			//MessageBox(NULL, to_wstring(UIZoom).c_str(), L"提示", NULL);
		}
	}
	{
		auto it = std::find(args.begin(), args.end(), L"/L");
		if (it != args.end() && std::next(it) != args.end()) {
			std::wstring language = *std::next(it);
			if (language == L"cn") loadI18n(IDR_JSON1);       // 中文简体
			else if (language == L"tw") loadI18n(IDR_JSON2);  // 中文繁体
			else loadI18n(IDR_JSON3);                         // 指定语言不存在或指定的就是英文，默认加载英文
		}
		else loadI18n(IDR_JSON1);
	}

	WNDCLASSEX wc;
	MSG Msg;

	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = L"InkeysApp";

	if (!RegisterClassEx(&wc)) {
		MessageBox(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}
	int Width = 640 * UIZoom, Height = 480 * UIZoom, x, y;
	{
		int ScreenWidth = GetSystemMetrics(SM_CXFULLSCREEN), ScreenHeight = GetSystemMetrics(SM_CYFULLSCREEN);
		x = (ScreenWidth - Width) / 2;
		y = (ScreenHeight - Height) / 2;
	}
	hWnd = CreateWindowEx(WS_EX_CLIENTEDGE, wc.lpszClassName, L"InkeysApp", WS_VISIBLE | WS_CAPTION | WS_POPUPWINDOW, x, y, Width, Height, NULL, NULL, hInstance, NULL);
	if (hWnd == NULL) {
		MessageBox(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}
	SetWindowTextA(hWnd, i18n_get("title").c_str());   // 千万不要把这行代码合并到CreateWindowEx那行，否则会乱码（）

	while (GetMessage(&Msg, NULL, 0, 0) > 0) {
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
	return Msg.wParam;
}

int getNowMillsecond()
{
	auto now = std::chrono::system_clock::now();
	return (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000).count();
}
