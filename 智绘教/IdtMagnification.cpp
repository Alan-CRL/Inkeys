#include "IdtMagnification.h"
#include <winuser.h>

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtWindow.h"

#include <d3d9.h>
#pragma comment(lib, "d3d9")

// IDT 定格功能的完整体验需要 Windows 8.1

HWND magnifierWindow, magnifierChild;
IMAGE MagnificationBackground;

bool magnificationReady;

shared_mutex MagnificationBackgroundSm;
RECT hostWindowRect;
int MagTransparency;

void UpdateMagWindow()
{
	RECT sourceRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN) - 1, GetSystemMetrics(SM_CYSCREEN) - 1 };
	MagSetWindowSource(magnifierChild, sourceRect);
	InvalidateRect(magnifierChild, NULL, TRUE);

	/*
	{
		std::unique_lock<std::shared_mutex> LockMagnificationBackgroundSm(MagnificationBackgroundSm);

		if (MagnificationBackground.getwidth() != GetSystemMetrics(SM_CXSCREEN) || MagnificationBackground.getheight() != GetSystemMetrics(SM_CYSCREEN))
			MagnificationBackground.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

		PrintWindow(magnifierChild, GetImageHDC(&MagnificationBackground), PW_RENDERFULLCONTENT);
		hiex::RemoveImageTransparency(&MagnificationBackground);

		LockMagnificationBackgroundSm.unlock();
	}
	*/
}

LRESULT CALLBACK MagnifierHostWindowWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
ATOM RegisterHostWindowClass(HINSTANCE hInstance, wstring className)
{
	WNDCLASSEX wcex = {};

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = MagnifierHostWindowWndProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wcex.lpszClassName = className.c_str();

	return RegisterClassEx(&wcex);
}

void MagnifierWindow(HINSTANCE hinst, promise<void>& promise)
{
	if (!MagInitialize()) // 一系列操作必须在同一线程中完成
	{
		IDTLogger->error("[放大API线程][MagnifierThread] 初始化MagInitialize失败");
		return;
	}

	// 窗口创建
	{
		hostWindowRect.left = 0;
		hostWindowRect.top = 0;
		hostWindowRect.right = GetSystemMetrics(SM_CXSCREEN);
		if (setlist.avoidFullScreen) hostWindowRect.bottom = GetSystemMetrics(SM_CYSCREEN) - 1;
		else hostWindowRect.bottom = GetSystemMetrics(SM_CYSCREEN);

		wstring ClassName;
		if (userId == L"Error") ClassName = L"Inkeys6;HiEasyX041";
		else ClassName = L"Inkeys6;" + userId;
		if (!RegisterHostWindowClass(hinst, ClassName)) IDTLogger->warn("[放大API线程][SetupMagnifier] 注册放大API主机窗口失败");

		magnifierWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, ClassName.c_str(), L"Inkeys6 MagnifierHostWindow",
			WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_MAXIMIZEBOX,
			0, 0, hostWindowRect.right, hostWindowRect.bottom, NULL, NULL, hinst, NULL);
		if (!magnifierWindow) IDTLogger->error("[放大API线程][SetupMagnifier] 创建放大API主机窗口失败" + to_string(GetLastError()));

		SetLayeredWindowAttributes(magnifierWindow, 0, 0, LWA_ALPHA);

		magnifierChild = CreateWindowEx(WS_EX_NOACTIVATE, WC_MAGNIFIER, TEXT("IdtScreenMagnifierMag"),
			WS_CHILD | MS_CLIPAROUNDCURSOR | WS_VISIBLE, // 光标一并放大 MS_SHOWMAGNIFIEDCURSOR
			hostWindowRect.left, hostWindowRect.top, hostWindowRect.right, hostWindowRect.bottom, magnifierWindow, NULL, hinst, NULL);
		if (!magnifierChild) IDTLogger->error("[放大API线程][SetupMagnifier] 创建放大API窗口失败" + to_string(GetLastError()));
	}

	// 样式设置
	{
		SetWindowLong(magnifierWindow, GWL_STYLE, GetWindowLong(magnifierWindow, GWL_STYLE) & ~WS_CAPTION); // 隐藏标题栏
		SetWindowLong(magnifierWindow, GWL_STYLE, GetWindowLong(magnifierWindow, GWL_STYLE) & ~WS_THICKFRAME); // 禁止窗口拉伸
		SetWindowPos(magnifierWindow, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_FRAMECHANGED);
		SetWindowLong(magnifierWindow, GWL_EXSTYLE, (GetWindowLong(magnifierWindow, GWL_EXSTYLE) | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW); // 隐藏任务栏图标
	}
	// 注册变换矩形
	{
		MAGTRANSFORM matrix;
		memset(&matrix, 0, sizeof(matrix));
		matrix.v[0][0] = 1.0f;
		matrix.v[1][1] = 1.0f;
		matrix.v[2][2] = 1.0f;

		BOOL ret = MagSetWindowTransform(magnifierChild, &matrix);
		if (ret)
		{
			MAGCOLOREFFECT magEffectInvert =
			{ {
				{  1.0f,  0.0f,  0.0f,  0.0f,  0.0f },
				{  0.0f,  1.0f,  0.0f,  0.0f,  0.0f },
				{  0.0f,  0.0f,  1.0f,  0.0f,  0.0f },
				{  0.0f,  0.0f,  0.0f,  1.0f,  0.0f },
				{  0.0f,  0.0f,  0.0f,  0.0f,  1.0f }
			} };

			if (!MagSetColorEffect(magnifierChild, &magEffectInvert)) IDTLogger->error("[放大API线程][SetupMagnifier] 设置放大API转换矩阵失败");
			else IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API转换矩阵完成");
		}
		else IDTLogger->error("[放大API线程][MagnifierThread] 启动放大API失败");
	}
	// 更新状态
	{
		ShowWindow(magnifierWindow, SW_SHOWNOACTIVATE);
		UpdateWindow(magnifierWindow);
	}

	promise.set_value();

	MSG msg;
	while (!offSignal && GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}
void CreateMagnifierWindow()
{
	promise<void> promise;
	future<void> future = promise.get_future();

	thread(MagnifierWindow, GetModuleHandle(0), ref(promise)).detach();
	future.get();
}

int RequestUpdateMagWindow;
/*
* RequestUpdateMagWindow 管理 IDT 放大行为
* 0 隐藏窗口
* 1 显示窗口并定格
* 2 显示窗口并实时
*/

void MagnifierThread()
{
	IDTLogger->info("[放大API线程][MagnifierThread] 等待穿透窗口创建");
	while (!offSignal)
	{
		if (IdtWindowsIsVisible.allCompleted)
		{
			std::vector<HWND> hwndList;
			hwndList.emplace_back(floating_window);
			hwndList.emplace_back(ppt_window);
			hwndList.emplace_back(drawpad_window);
			hwndList.emplace_back(freeze_window);
			hwndList.emplace_back(setting_window);

			IDTLogger->info("[放大API线程][MagnifierThread] 设置穿透窗口列表");

			if (MagSetWindowFilterList(magnifierChild, MW_FILTERMODE_EXCLUDE, hwndList.size(), hwndList.data()) == FALSE)
			{
				IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
				if (pD3D != nullptr)
				{
					D3DCAPS9 caps;
					HRESULT hr = pD3D->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
					if (SUCCEEDED(hr))
					{
						if (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) IDTLogger->error("[放大API线程][MagnifierThread] 设置穿透窗口列表失败（设备支持 WDDM 1.0 版本但原因未知）");
						else IDTLogger->error("[放大API线程][MagnifierThread] 设置穿透窗口列表失败（设备不支持 WDDM 1.0 版本）");
					}
					else IDTLogger->error("[放大API线程][MagnifierThread] 设置穿透窗口列表失败（无法 GetDeviceCaps 并查询是否支持 WDDM）");

					pD3D->Release();
				}
				else IDTLogger->error("[放大API线程][MagnifierThread] 设置穿透窗口列表失败（无法初始化 IDirect3D9 并查询是否支持 WDDM）");
			}
			else magnificationReady = true;

			break;
		}

		this_thread::sleep_for(chrono::milliseconds(500));
	}
	IDTLogger->info("[放大API线程][MagnifierThread] 等待穿透窗口创建完成");

	while (!offSignal)
	{
		if (RequestUpdateMagWindow == 1)
		{
			if (MagTransparency == 0)
			{
				UpdateMagWindow();

				SetLayeredWindowAttributes(magnifierWindow, 0, 255, LWA_ALPHA);
				MagTransparency = 255;
			}

			while (RequestUpdateMagWindow == 1) this_thread::sleep_for(chrono::milliseconds(100));
			/*for (int i = 0; RequestUpdateMagWindow == 1; i++, i %= 10)
			{
				if (!i) SetWindowPos(magnifierWindow, freeze_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

				this_thread::sleep_for(chrono::milliseconds(100));
			}*/
		}
		else if (RequestUpdateMagWindow == 0)
		{
			if (MagTransparency == 255)
			{
				SetLayeredWindowAttributes(magnifierWindow, 0, 0, LWA_ALPHA);
				MagTransparency = 0;
			}

			while (RequestUpdateMagWindow == 0) this_thread::sleep_for(chrono::milliseconds(100));
		}
	}

	MagUninitialize();
}