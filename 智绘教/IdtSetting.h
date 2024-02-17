#pragma once
#include "IdtMain.h"

#include "IdtConfiguration.h"
#include "IdtDrawpad.h"
#include "IdtText.h"
#include "IdtWindow.h"

#include "imgui/imconfig.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"
#include "imgui/imgui_toggle.h"
#include "imgui/imgui_toggle_presets.h"
#include <tchar.h>

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

// Ê¾Àý
static void HelpMarker(const char* desc, ImVec4 Color)
{
	ImGui::TextColored(Color, u8"(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}
static void CenteredText(const char* desc, float displacement)
{
	float temp = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(temp + displacement);
	ImGui::TextUnformatted(desc);
	ImGui::SetCursorPosY(temp);
}