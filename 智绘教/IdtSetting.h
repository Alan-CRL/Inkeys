#pragma once
#include "IdtMain.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11")

extern IMAGE SettingSign[5];
extern WNDCLASSEX ImGuiWc;
extern ID3D11ShaderResourceView* TextureSettingSign[5];
extern int SettingMainMode;

extern int SettingWindowX;
extern int SettingWindowY;
extern int SettingWindowWidth;
extern int SettingWindowHeight;

void SettingSeekBar();
int SettingMain();

void FirstSetting(bool info);
bool ReadSetting(bool first);
bool WriteSetting();

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

bool LoadTextureFromFile(unsigned char* image_data, int width, int height, ID3D11ShaderResourceView** out_srv);

LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);