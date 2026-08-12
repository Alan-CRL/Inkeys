#include "IdtMagnification.h"
#include <winuser.h>

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <d3d9.h>
#pragma comment(lib, "d3d9")

HWND magnifierWindow, magnifierChild;
Inkeys::Graphics::DibSurface MagnificationBackground;

bool magnificationCreateReady;
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

		if (MagnificationBackground.width() != GetSystemMetrics(SM_CXSCREEN) || MagnificationBackground.height() != GetSystemMetrics(SM_CYSCREEN))
			MagnificationBackground.resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

		PrintWindow(magnifierChild, MagnificationBackground.dc(), PW_RENDERFULLCONTENT);
		ForceOpaqueAlpha(&MagnificationBackground);

		LockMagnificationBackgroundSm.unlock();
	}
	*/
}

LRESULT CALLBACK MagnifierHostWindowWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
bool PrepareMagnifierWindow()
{
	// 尝试加载
	HMODULE hMagDll = LoadLibrary(TEXT("Magnification.dll"));
	if (hMagDll == NULL)
	{
		IDTLogger->warn("[放大API线程][MagnifierThread] 本机缺少 Magnification.dll，定格等相关功能将被禁用。");
		return false;
	}
	FreeLibrary(hMagDll);

	// 检测是否是 Wine
	{
		HMODULE hNtdll = ::GetModuleHandleW(L"ntdll.dll");
		if (hNtdll)
		{
			if (GetProcAddress(hNtdll, "wine_get_version") != nullptr)
			{
				IDTLogger->warn("[放大API线程][MagnifierThread] 本机为 Wine 环境，不支持 Magnification.dll 相关功能，定格等相关功能将被禁用。");
				return false;
			}
		}
	}

	// 初始化放大API
	if (!MagInitialize())
	{
		IDTLogger->error("[放大API线程][MagnifierThread] 初始化MagInitialize失败");
		return false;
	}

	return true;
}

void MagnifierHostCreated(HWND hwnd)
{
	magnifierWindow = hwnd;
	SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
}

void MagnifierChildCreated(HWND hwnd)
{
	magnifierChild = hwnd;
	MAGTRANSFORM matrix{};
	matrix.v[0][0] = 1.0f;
	matrix.v[1][1] = 1.0f;
	matrix.v[2][2] = 1.0f;
	if (!MagSetWindowTransform(hwnd, &matrix))
	{
		IDTLogger->error("[放大API线程][MagnifierThread] 启动放大API失败");
		return;
	}

	MAGCOLOREFFECT effect = { {
		{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }
	} };
	if (!MagSetColorEffect(hwnd, &effect))
		IDTLogger->error("[放大API线程][SetupMagnifier] 设置放大API转换矩阵失败");
	else
	{
		magnificationCreateReady = true;
		IDTLogger->info("[放大API线程][SetupMagnifier] 设置放大API转换矩阵完成");
	}
}

void ShutdownMagnifierWindow()
{
	magnificationCreateReady = false;
	magnificationReady = false;
	magnifierChild = nullptr;
	magnifierWindow = nullptr;
	MagUninitialize();
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
}
