#include "IdtMagnification.h"
#include <winuser.h>

#include <d3d9.h>
#pragma comment(lib, "d3d9")

HWND hwndHost, hwndMag;
IMAGE MagnificationBackground = CreateImageColor(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), RGBA(255, 255, 255, 255), false);
int magnificationWindowReady;

shared_mutex MagnificationBackgroundSm;
RECT magWindowRect;
RECT hostWindowRect;

HINSTANCE hInst;
BOOL MagImageScaling(HWND hwnd, void* srcdata, MAGIMAGEHEADER srcheader, void* destdata, MAGIMAGEHEADER destheader, RECT unclipped, RECT clipped, HRGN dirty)
{
	int ImageWidth = srcheader.width;
	int ImageHeight = srcheader.height;

	IMAGE SrcImage(ImageWidth, ImageHeight);
	DWORD* SrcBuffer = GetImageBuffer(&SrcImage);
	memcpy(SrcBuffer, (LPBYTE)srcdata + srcheader.offset, ImageWidth * ImageHeight * 4);

	std::unique_lock<std::shared_mutex> LockMagnificationBackgroundSm(MagnificationBackgroundSm);
	MagnificationBackground = SrcImage;
	LockMagnificationBackgroundSm.unlock();

	return 1;
}
void UpdateMagWindow()
{
	RECT sourceRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

	MagSetWindowSource(hwndMag, sourceRect);
	InvalidateRect(hwndMag, NULL, TRUE);
}

LRESULT CALLBACK MagnifierWindowWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hWnd, message, wParam, lParam);
}
ATOM RegisterHostWindowClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex = {};

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = MagnifierWindowWndProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wcex.lpszClassName = TEXT("MagnifierWindow");

	return RegisterClassEx(&wcex);
}
BOOL SetupMagnifier(HINSTANCE hinst)
{
	hostWindowRect.top = 0;
	hostWindowRect.bottom = GetSystemMetrics(SM_CYSCREEN);
	hostWindowRect.left = 0;
	hostWindowRect.right = GetSystemMetrics(SM_CXSCREEN);

	IDTLogger->info("[放大API线程][SetupMagnifier] 注册放大API窗口");
	RegisterHostWindowClass(hinst);
	IDTLogger->info("[放大API线程][SetupMagnifier] 注册放大API窗口完成");

	IDTLogger->info("[放大API线程][SetupMagnifier] 创建放大API主机窗口");
	hwndHost = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED, TEXT("MagnifierWindow"), TEXT("Idt Screen Magnifier"), WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CAPTION | WS_MAXIMIZEBOX, 0, 0, hostWindowRect.right, hostWindowRect.bottom, NULL, NULL, hInst, NULL);
	if (!hwndHost)
	{
		IDTLogger->error("[放大API线程][SetupMagnifier] 创建放大API主机窗口失败" + to_string(GetLastError()));
		return FALSE;
	}
	else IDTLogger->info("[放大API线程][SetupMagnifier] 创建放大API主机窗口完成");

	IDTLogger->info("[放大API线程][SetupMagnifier] 创建放大API窗口");
	hwndMag = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"), WS_CHILD | MS_CLIPAROUNDCURSOR | WS_VISIBLE, magWindowRect.left, magWindowRect.top, magWindowRect.right, magWindowRect.bottom, hwndHost, NULL, hInst, NULL);
	if (!hwndMag)
	{
		IDTLogger->error("[放大API线程][SetupMagnifier] 创建放大API窗口失败" + to_string(GetLastError()));
		return FALSE;
	}
	else IDTLogger->info("[放大API线程][SetupMagnifier] 创建放大API窗口完成");

	IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API窗口样式");
	SetWindowLong(hwndMag, GWL_STYLE, GetWindowLong(hwndMag, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
	SetWindowLong(hwndMag, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏
	SetWindowPos(hwndMag, NULL, hostWindowRect.left, hostWindowRect.top, hostWindowRect.right - hostWindowRect.left, hostWindowRect.bottom - hostWindowRect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
	IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API窗口完成");

	IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API工厂");
	MAGTRANSFORM matrix;
	memset(&matrix, 0, sizeof(matrix));
	matrix.v[0][0] = 1.0f;
	matrix.v[1][1] = 1.0f;
	matrix.v[2][2] = 1.0f;
	IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API工厂完成");

	IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API转换矩阵");
	BOOL ret = MagSetWindowTransform(hwndMag, &matrix);
	if (ret)
	{
		MAGCOLOREFFECT magEffectInvert =
		{ {
			{  1.0f,  0.0f,  0.0f,  0.0f,  0.0f },
			{  0.0f,  1.0f,  0.0f,  0.0f,  0.0f },
			{  0.0f,  0.0f,  1.0f,  0.0f,  0.0f },
			{  0.0f,  0.0f,  0.0f,  1.0f,  0.0f },
			{  1.0f,  1.0f,  1.0f,  0.0f,  1.0f }
		} };

		ret = MagSetColorEffect(hwndMag, NULL);

		IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API转换矩阵完成");
	}
	else IDTLogger->error("[放大API线程][SetupMagnifier] 设置放大API转换矩阵失败");

	IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API回调函数");
	MagSetImageScalingCallback(hwndMag, (MagImageScalingCallback)MagImageScaling);
	IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API回调函数完成");

	return ret;
}

bool RequestUpdateMagWindow;
void MagnifierThread()
{
	IDTLogger->info("[放大API线程][MagnifierThread] 初始化MagInitialize");
	if (!MagInitialize())
	{
		IDTLogger->error("[放大API线程][MagnifierThread] 初始化MagInitialize失败");
		return;
	}
	else IDTLogger->info("[放大API线程][MagnifierThread] 初始化MagInitialize完成");

	IDTLogger->info("[放大API线程][MagnifierThread] 启动放大API");
	if (!SetupMagnifier(GetModuleHandle(0)))
	{
		IDTLogger->error("[放大API线程][MagnifierThread] 启动放大API失败");
		return;
	}
	else IDTLogger->info("[放大API线程][MagnifierThread] 启动放大API完成");

	IDTLogger->info("[放大API线程][SetupMagnifier] 更新放大API窗口");
	ShowWindow(hwndHost, SW_HIDE);
	UpdateWindow(hwndHost);
	IDTLogger->info("[放大API线程][SetupMagnifier] 更新放大API窗口完成");

	IDTLogger->info("[放大API线程][MagnifierThread] 等待穿透窗口创建");
	while (!off_signal)
	{
		if (magnificationWindowReady >= 4)
		{
			std::vector<HWND> hwndList;
			hwndList.emplace_back(floating_window);
			hwndList.emplace_back(drawpad_window);
			hwndList.emplace_back(ppt_window);
			hwndList.emplace_back(setting_window);

			IDTLogger->info("[放大API线程][MagnifierThread] 设置穿透窗口列表");

			if (MagSetWindowFilterList(hwndMag, MW_FILTERMODE_EXCLUDE, hwndList.size(), hwndList.data()) == FALSE)
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
			else IDTLogger->info("[放大API线程][MagnifierThread] 设置穿透窗口列表完成");

			magnificationWindowReady = -1;

			break;
		}

		Sleep(500);
	}
	IDTLogger->info("[放大API线程][MagnifierThread] 等待穿透窗口创建完成");

	RequestUpdateMagWindow = true;
	while (!off_signal)
	{
		while (!RequestUpdateMagWindow && !off_signal) Sleep(100);
		if (off_signal) break;

		{
			IDTLogger->info("[放大API线程][MagnifierThread] 更新API图像");
			UpdateMagWindow();
			IDTLogger->info("[放大API线程][MagnifierThread] 更新API图像完成");

			RequestUpdateMagWindow = false;
		}
	}

	IDTLogger->info("[放大API线程][MagnifierThread] 反向初始化MagUninitialize");
	MagUninitialize();
	IDTLogger->info("[放大API线程][MagnifierThread] 反向初始化MagUninitialize完成");
}