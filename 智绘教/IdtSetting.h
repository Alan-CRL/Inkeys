#pragma once
#include "IdtMain.h"

#include <d3d9.h>
#include <C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Include\D3dx9tex.h>
#pragma comment(lib, "d3d9")

extern WNDCLASSEXW ImGuiWc;
extern PDIRECT3DTEXTURE9 TextureSettingSign[11];
extern int SettingMainMode;

void SettingWindowBegin();
void SettingMain();

// Data
extern LPDIRECT3D9 g_pD3D;
extern LPDIRECT3DDEVICE9 g_pd3dDevice;
extern bool g_DeviceLost;
extern UINT g_ResizeWidth, g_ResizeHeight;
extern D3DPRESENT_PARAMETERS g_d3dpp;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool LoadTextureFromMemory(unsigned char* image_data, int width, int height, PDIRECT3DTEXTURE9* out_texture);