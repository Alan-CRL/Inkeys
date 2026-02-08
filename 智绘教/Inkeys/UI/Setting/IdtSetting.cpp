#include "IdtSetting.h"

WNDCLASSEXW ImGuiWc;
ImFont* ImFontMain;
SettingSignStruct settingSign[11];
PDIRECT3DTEXTURE9 TextureSettingSign[11];

float settingGlobalScale = 1.0f;

// Data
LPDIRECT3D9 g_pD3D = nullptr;
LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
bool g_DeviceLost = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
D3DPRESENT_PARAMETERS g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd)
{
	if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
		return false;

	// Create the D3DDevice
	ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
	g_d3dpp.Windowed = TRUE;
	g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
	g_d3dpp.EnableAutoDepthStencil = TRUE;
	g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
	g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
	//g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate

	HRESULT hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice);
	if (FAILED(hr)) return false;

	return true;
}
void CleanupDeviceD3D()
{
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
	if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}
void ResetDevice()
{
	ImGui_ImplDX9_InvalidateDeviceObjects();
	HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
	if (hr == D3DERR_INVALIDCALL)
		IM_ASSERT(0);
	ImGui_ImplDX9_CreateDeviceObjects();
}

// 基础封装（历史遗留）
static void HelpMarker(const char* desc, ImVec4 tmp)
{
	ImFontMain->Scale = 0.45f, ImGui::PushFont(ImFontMain);
	ImGui::TextColored(ImVec4(13 / 255.0f, 83 / 255.0f, 255 / 255.0f, 1.0f), "\ue90a");
	ImGui::PopFont();

	ImFontMain->Scale = 0.7f, ImGui::PushFont(ImFontMain);
	if (ImGui::IsItemHovered())
	{
		ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(236, 241, 255, 200));
		ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(175, 197, 255, 255));
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(13, 83, 255, 255));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(1);
	}
	ImGui::PopFont();
}
static void CenteredText(const char* desc, float displacement)
{
	float temp = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(temp + displacement);
	ImGui::TextUnformatted(desc);
	ImGui::SetCursorPosY(temp);
}
void ScrollWhenDraggingOnVoid(const ImVec2& delta, ImGuiMouseButton mouse_button)
{
	ImGuiContext& g = *ImGui::GetCurrentContext();

	ImGuiWindow* window = g.CurrentWindow;
	bool hovered = false;
	bool held = false;
	ImGuiID id = window->GetID("##scrolldraggingoverlay");
	ImGui::KeepAliveID(id);

	ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, mouse_button);
	if (!held && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDown(mouse_button)) held = true;

	if (held && delta.x != 0.0f)
		ImGui::SetScrollX(window, window->Scroll.x + delta.x);
	if (held && delta.y != 0.0f)
		ImGui::SetScrollY(window, window->Scroll.y + delta.y);
}
bool LoadTextureFromMemory(unsigned char* image_data, int width, int height, PDIRECT3DTEXTURE9* out_texture)
{
	if (image_data == NULL || width <= 0 || height <= 0 || out_texture == NULL)
		return false;

	// 创建纹理，MipLevels 设置为 1 表示不使用 mipmapping
	PDIRECT3DTEXTURE9 texture = NULL;

	HRESULT hr = g_pd3dDevice->CreateTexture(
		width,
		height,
		1, // MipLevels，不生成 mipmap 级别
		0, // Usage，默认值
		D3DFMT_A8R8G8B8, // 像素格式，RGBA8
		D3DPOOL_MANAGED, // 内存池类型
		&texture,
		NULL
	);

	if (FAILED(hr))
		return false;

	// 锁定纹理以访问其内存
	D3DLOCKED_RECT locked_rect;
	hr = texture->LockRect(0, &locked_rect, NULL, 0);
	if (FAILED(hr))
	{
		texture->Release();
		return false;
	}

	// 复制图像数据到纹理
	for (int y = 0; y < height; y++)
	{
		// 计算目标纹理行的起始地址
		BYTE* pDest = static_cast<BYTE*>(locked_rect.pBits) + y * locked_rect.Pitch;
		// 计算源图像行的起始地址
		BYTE* pSrc = image_data + y * width * 4; // 每像素4字节（RGBA）

		memcpy(pDest, pSrc, width * 4);
	}

	// 解锁纹理
	texture->UnlockRect(0);

	*out_texture = texture;
	return true;
}