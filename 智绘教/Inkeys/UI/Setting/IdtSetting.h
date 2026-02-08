#pragma once
#include "../../../IdtMain.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

#include "imgui/imgui_internal.h"
#include "imgui/imgui_toggle.h"
#include "imgui/imgui_toggle_presets.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"

#include <d3d9.h>
#pragma comment(lib, "d3d9")

// D3dx9tex.h 副本位于项目中
// 如果出现问题也可以选用 SDK 所属的默认路径
#include "Microsoft DirectX SDK (June 2010)/Include/D3dx9tex.h"

extern WNDCLASSEXW ImGuiWc;
extern ImFont* ImFontMain;
struct SettingSignStruct
{
	int width;
	int height;
};
extern SettingSignStruct settingSign[11];
extern PDIRECT3DTEXTURE9 TextureSettingSign[11];

extern float settingGlobalScale;

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

// 基础封装（历史遗留）
static void HelpMarker(const char* desc, ImVec4 tmp);
static void CenteredText(const char* desc, float displacement);
void ScrollWhenDraggingOnVoid(const ImVec2& delta, ImGuiMouseButton mouse_button);
bool LoadTextureFromMemory(unsigned char* image_data, int width, int height, PDIRECT3DTEXTURE9* out_texture);