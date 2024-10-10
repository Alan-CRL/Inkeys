#include "IdtSetting.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtHash.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtI18n.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

#include "imgui/imgui_internal.h"
#include "imgui/imgui_toggle.h"
#include "imgui/imgui_toggle_presets.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"

#define IMGUI_ENABLE_FREETYPE
#include "imgui/imgui_freetype.h"

#pragma comment(lib, "freetype/freetype.lib")

// 示例
static void HelpMarker(const char* desc, ImVec4 Color);
static void CenteredText(const char* desc, float displacement);

IMAGE SettingSign[10];
WNDCLASSEXW ImGuiWc;
PDIRECT3DTEXTURE9 TextureSettingSign[10];
int SettingMainMode = 1;

int ScreenWidth = GetSystemMetrics(SM_CXSCREEN);//获取显示器的宽
int ScreenHeight = GetSystemMetrics(SM_CYSCREEN);//获取显示器的高

int SettingWindowX = (ScreenWidth - SettingWindowWidth) / 2;
int SettingWindowY = (ScreenHeight - SettingWindowHeight) / 2;
int SettingWindowWidth = 900;
int SettingWindowHeight = 700;

void SettingSeekBar()
{
	if (!KeyBoradDown[VK_LBUTTON]) return;

	POINT p;
	GetCursorPos(&p);

	int pop_x = p.x - SettingWindowX;
	int pop_y = p.y - SettingWindowY;

	while (1)
	{
		if (!KeyBoradDown[VK_LBUTTON]) break;

		POINT p;
		GetCursorPos(&p);

		SetWindowPos(setting_window,
			NULL,
			SettingWindowX = p.x - pop_x,
			SettingWindowY = p.y - pop_y,
			0,
			0,
			SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
	}

	return;
}
int SettingMain()
{
	threadStatus[L"SettingMain"] = true;

	// 初始化部分
	{
		ImGuiWc = { sizeof(WNDCLASSEX), CS_CLASSDC, ImGuiWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, get<wstring>(i18n[i18nEnum::SettingsW]).c_str(), nullptr };
		RegisterClassExW(&ImGuiWc);
		setting_window = CreateWindowW(ImGuiWc.lpszClassName, get<wstring>(i18n[i18nEnum::SettingsW]).c_str(), WS_OVERLAPPEDWINDOW, SettingWindowX, SettingWindowY, SettingWindowWidth, SettingWindowHeight, nullptr, nullptr, ImGuiWc.hInstance, nullptr);

		SetWindowLong(setting_window, GWL_STYLE, GetWindowLong(setting_window, GWL_STYLE) & ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME));
		SetWindowPos(setting_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

		ShowWindow(setting_window, SW_HIDE);
		UpdateWindow(setting_window);

		CreateDeviceD3D(setting_window);

		if (i18nIdentifying == L"zh-CN") loadimage(&SettingSign[1], L"PNG", L"Home2_zh-CN");
		else loadimage(&SettingSign[1], L"PNG", L"Home2_en-US");
		{
			int width = SettingSign[1].getwidth();
			int height = SettingSign[1].getheight();
			DWORD* pMem = GetImageBuffer(&SettingSign[1]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[1]);
			delete[] data;

			IM_ASSERT(ret);
		}

		if (i18nIdentifying == L"zh-CN") loadimage(&SettingSign[2], L"PNG", L"Home1_zh-CN");
		else loadimage(&SettingSign[2], L"PNG", L"Home1_en-US");
		{
			int width = SettingSign[2].getwidth();
			int height = SettingSign[2].getheight();
			DWORD* pMem = GetImageBuffer(&SettingSign[2]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[2]);
			delete[] data;

			IM_ASSERT(ret);
		}

		loadimage(&SettingSign[7], L"PNG", L"sign6");
		{
			int width = SettingSign[7].getwidth();
			int height = SettingSign[7].getheight();
			DWORD* pMem = GetImageBuffer(&SettingSign[7]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[7]);
			delete[] data;

			IM_ASSERT(ret);
		}

		loadimage(&SettingSign[5], L"PNG", L"PluginFlag1", 100, 100, true);
		{
			int width = SettingSign[5].getwidth();
			int height = SettingSign[5].getheight();
			DWORD* pMem = GetImageBuffer(&SettingSign[5]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[5]);
			delete[] data;

			IM_ASSERT(ret);
		}
		loadimage(&SettingSign[6], L"PNG", L"PluginFlag2", 100, 100, true);
		{
			int width = SettingSign[6].getwidth();
			int height = SettingSign[6].getheight();
			DWORD* pMem = GetImageBuffer(&SettingSign[6]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
					}
					else
					{
						data[(y * width + x) * 4 + 0] = 0;
						data[(y * width + x) * 4 + 1] = 0;
						data[(y * width + x) * 4 + 2] = 0;
					}
					data[(y * width + x) * 4 + 3] = alpha;
				}
			}

			bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[6]);
			delete[] data;

			IM_ASSERT(ret);
		}
	}

	bool ShowWindow = false;
	while (!offSignal)
	{
		::ShowWindow(setting_window, SW_HIDE);
		ShowWindow = false;

		while (!test.select && !offSignal) this_thread::sleep_for(chrono::milliseconds(100));
		if (offSignal) break;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.IniFilename = nullptr;

		ImGui::StyleColorsLight();

		ImGui_ImplWin32_Init(setting_window);
		ImGui_ImplDX9_Init(g_pd3dDevice);

		if (_waccess((globalPath + L"ttf\\hmossscr.ttf").c_str(), 0) == -1)
		{
			if (_waccess((globalPath + L"ttf").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directory(globalPath + L"ttf", ec);
			}
			ExtractResource((globalPath + L"ttf\\hmossscr.ttf").c_str(), L"TTF", MAKEINTRESOURCE(198));
		}

		ImFontConfig font_cfg;
		font_cfg.OversampleH = 1;
		font_cfg.OversampleV = 1;
		font_cfg.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_LightHinting;

		ImFont* Font = io.Fonts->AddFontFromFileTTF((utf16ToUtf8(globalPath) + "ttf\\hmossscr.ttf").c_str(), 28.0f, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());

		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		ImGuiStyle& style = ImGui::GetStyle();

		{
			style.Colors[ImGuiCol_WindowBg] = ImVec4(245 / 255.0f, 248 / 255.0f, 255 / 252.0f, 1.0f);
			style.Colors[ImGuiCol_ChildBg] = ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 252.0f, 1.0f);
			style.Colors[ImGuiCol_TitleBgActive] = ImVec4(221 / 255.0f, 223 / 255.0f, 230 / 252.0f, 1.0f);

			style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

			style.FrameBorderSize = 1.0f;
			style.ChildBorderSize = 1.0f;

			style.ChildRounding = 8.0f;
			style.FrameRounding = 8.0f;
			style.GrabRounding = 8.0f;
		}

		//初始化定义变量
		hiex::tDelayFPS recond;
		POINT MoushPos = { 0,0 };
		ImGuiToggleConfig config;
		config.FrameRounding = 0.3f;
		config.KnobRounding = 0.5f;
		config.Size = { 60.0f,30.0f };
		config.Flags = ImGuiToggleFlags_Animated;

		int QuestNumbers = 0;
		int PushStyleColorNum = 0, PushFontNum = 0, PushStyleVarNum = 0;
		int QueryWaitingTime = 5;

		// ---

		int languageMode = 1;
		if (i18nIdentifying == L"en-US") languageMode = 1;
		if (i18nIdentifying == L"zh-CN") languageMode = 2;
		if (i18nIdentifying == L"zh-TW") languageMode = 3;

		bool StartUp = false;
		if (setlist.StartUp == 2) StartUp = true;

		bool CreateLnk = setlist.CreateLnk;
		bool RightClickClose = setlist.RightClickClose;
		bool BrushRecover = setlist.BrushRecover, RubberRecover = setlist.RubberRecover;
		bool CompatibleTaskBarAutoHide = setlist.compatibleTaskBarAutoHide;
		int RubberMode = setlist.RubberMode;
		string UpdateChannel = setlist.UpdateChannel;
		bool IntelligentDrawing = setlist.IntelligentDrawing, SmoothWriting = setlist.SmoothWriting;
		int SetSkinMode = setlist.SetSkinMode;

		// 插件参数

		bool DdbEnable = ddbSetList.DdbEnable;
		bool DdbEnhance = ddbSetList.DdbEnhance;

		float PptUiWidgetScaleWidget = 1.0f, PptUiWidgetScaleWidgetRecord = 1.0f;
		float BottomSideBothWidgetScale = pptComSetlist.bottomSideBothWidgetScale;
		float MiddleSideBothWidgetScale = pptComSetlist.middleSideBothWidgetScale;
		float BottomSideMiddleWidgetScale = pptComSetlist.bottomSideMiddleWidgetScale;

		bool PptComFixedHandWriting = pptComSetlist.fixedHandWriting;
		bool MemoryWidgetPosition = pptComSetlist.memoryWidgetPosition;
		bool ShowBottomBoth = pptComSetlist.showBottomBoth;
		bool ShowMiddleBoth = pptComSetlist.showMiddleBoth;
		bool ShowBottomMiddle = pptComSetlist.showBottomMiddle;
		float BottomBothWidth = pptComSetlist.bottomBothWidth;
		float BottomBothHeight = pptComSetlist.bottomBothHeight;
		float MiddleBothWidth = pptComSetlist.middleBothWidth;
		float MiddleBothHeight = pptComSetlist.middleBothHeight;
		float BottomMiddleWidth = pptComSetlist.bottomMiddleWidth;
		float BottomMiddleHeight = pptComSetlist.bottomMiddleHeight;

		// ==========

		wstring receivedData;
		POINT pt;

		while (!offSignal)
		{
			MSG msg;
			while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);

				switch (msg.message)
				{
				case WM_MOUSEMOVE:
					MoushPos.x = GET_X_LPARAM(msg.lParam);
					MoushPos.y = GET_Y_LPARAM(msg.lParam);

					break;

				case WM_LBUTTONDOWN:
					if (IsInRect(MoushPos.x, MoushPos.y, { 0,0,870,30 })) SettingSeekBar();
					break;
				}
			}

			// Handle lost D3D9 device
			if (g_DeviceLost)
			{
				HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
				if (hr == D3DERR_DEVICELOST)
				{
					this_thread::sleep_for(chrono::milliseconds(10));
					continue;
				}
				if (hr == D3DERR_DEVICENOTRESET) ResetDevice();
				g_DeviceLost = false;
			}

			// Handle window resize (we don't resize directly in the WM_SIZE handler)
			if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
			{
				g_d3dpp.BackBufferWidth = g_ResizeWidth;
				g_d3dpp.BackBufferHeight = g_ResizeHeight;
				g_ResizeWidth = g_ResizeHeight = 0;
				ResetDevice();
			}

			hiex::DelayFPS(recond, 24);

			// Start the Dear ImGui frame
			ImGui_ImplDX9_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			{
				//定义栏操作
				static int Tab = 0;
				enum Tab
				{
					tab1,
					tab2,
					tab3,
					tab4,
					tab5,
					tab6,
					tab7,
					tab8,
					tab9
				};
				static int TabPlugInIdx = 0;
				enum TabPlugIn
				{
					tabPlug1,
					tabPlug2,
					tabPlug3
				};

				ImGui::SetNextWindowPos({ 0,0 });//设置窗口位置
				ImGui::SetNextWindowSize({ static_cast<float>(SettingWindowWidth),static_cast<float>(SettingWindowHeight) });//设置窗口大小

				Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
				ImGui::Begin(get<string>(i18n[i18nEnum::Settings]).c_str(), &test.select, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);//开始绘制窗口

				{
					// 主页
					{
						ImGui::SetCursorPos({ 10.0f,44.0f });

						if (Tab == Tab::tab1)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(228, 237, 252, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(228, 237, 252, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(231, 233, 239, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(245, 248, 255, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(231, 233, 239, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(228, 237, 252, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button(get<string>(i18n[i18nEnum::Settings_Home]).c_str(), { 150.0f,40.0f })) Tab = Tab::tab1;
					}

					// 常规
					{
						ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 10.0f });

						if (Tab == Tab::tab2)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 225, 255));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(225, 225, 225, 255));
						}

						if (ImGui::Button(get<string>(i18n[i18nEnum::Settings_Regular]).c_str(), { 100.0f,40.0f })) Tab = Tab::tab2;
					}

					// 绘制
					{
						ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 10.0f });

						if (Tab == Tab::tab3)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 225, 255));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(225, 225, 225, 255));
						}

						if (ImGui::Button("绘制", { 100.0f,40.0f })) Tab = Tab::tab3;
					}

					// 插件
					{
						ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 10.0f });

						if (Tab == Tab::tab4)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 225, 255));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(225, 225, 225, 255));
						}

						if (ImGui::Button("插件", { 100.0f,40.0f })) Tab = Tab::tab4;
					}

					// 快捷键
					{
						ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 10.0f });

						if (Tab == Tab::tab5)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 225, 255));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(225, 225, 225, 255));
						}

						if (ImGui::Button("快捷键", { 100.0f,40.0f })) Tab = Tab::tab5;
					}

					// 社区名片
					{
						ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 10.0f });

						if (Tab == Tab::tab6)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 225, 255));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(225, 225, 225, 255));
						}

						if (ImGui::Button("感谢墙", { 100.0f,40.0f })) Tab = Tab::tab6;
					}

					// 软件版本
					{
						ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 10.0f });

						if (Tab == Tab::tab7)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 111, 225, 255));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 225, 255));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(225, 225, 225, 255));
						}

						if (ImGui::Button("软件版本", { 100.0f,40.0f })) Tab = Tab::tab7;
					}

					// --------------------

#ifndef IDT_RELEASE
					// 实验室
					{
						ImGui::SetCursorPos({ 10.0f,44.0f + 616.0f - 135.0f });

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(235, 235, 235, 255));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(215, 215, 215, 255));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(195, 195, 195, 255));

						if (ImGui::Button("实验室", { 100.0f,30.0f })) Tab = Tab::tab8;
					}
#endif

					// 程序调测
					{
						ImGui::SetCursorPos({ 10.0f,44.0f + 616.0f - 100.0f });

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(147, 255, 154, 255));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(117, 204, 123, 255));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(86, 149, 86, 255));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

						if (ImGui::Button("程序调测", { 100.0f,30.0f })) Tab = Tab::tab9;
					}

					ImGui::SetCursorPos({ 10.0f,44.0f + 616.0f - 65.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
					if (ImGui::Button("重启程序", { 100.0f,30.0f }))
					{
						test.select = false;
						offSignal = 2;
					}

					ImGui::SetCursorPos({ 10.0f,44.0f + 616.0f - 30.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
					if (ImGui::Button("关闭程序", { 100.0f,30.0f }))
					{
						test.select = false;
						offSignal = true;
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
				}

				ImGui::SetCursorPos({ 120.0f,44.0f });
				switch (Tab)
				{
					// 主页
				case Tab::tab1:
				{
					ImGui::BeginChild("主页", { 770.0f,616.0f }, true);
					{
						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 child_pos = ImGui::GetWindowPos();
						ImVec2 child_size = ImGui::GetWindowSize();

						ImU32 color_top = IM_COL32(245, 248, 255, 255);
						ImU32 color_middle = IM_COL32(255, 255, 255, 255);

						ImVec2 top_left = child_pos;
						ImVec2 bottom_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

						ImVec2 middle_left = ImVec2(child_pos.x, child_pos.y + child_size.y / 3.0f);
						ImVec2 middle_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y / 3.0f);

						draw_list->AddRectFilledMultiColor(top_left, middle_right, color_top, color_top, color_middle, color_middle);
						draw_list->AddRect(child_pos, bottom_right, ImGui::GetColorU32(ImGuiCol_Border), style.ChildRounding, ImDrawFlags_RoundCornersAll, 1.0f);
					}

					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(235 / 255.0f, 10 / 255.0f, 20 / 255.0f, 1.0f));

						ImGui::SetCursorPosY(10.0f);
						int left_x = 10, right_x = 760;

						vector<string> lines;
						string line, temp;
						stringstream ss(get<string>(i18n[i18nEnum::Settings_Home_Prompt]));

						while (getline(ss, temp, '\n'))
						{
							bool flag = false;
							line = "";

							for (char ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize((line + ch).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(line);
									line = "", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(line);
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}
					}

					ImGui::SetCursorPos({ 35.0f,70.0f });
					ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[2], ImVec2((float)SettingSign[2].getwidth(), (float)SettingSign[2].getheight()));

					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));

						ImGui::SetCursorPosY(616.0f - 168.0f);
						string author = get<string>(i18n[i18nEnum::Settings_Home_Feedback]);
						int left_x = 10, right_x = 370;

						vector<string> lines;
						string line, temp;
						stringstream ss(author);

						while (getline(ss, temp, '\n'))
						{
							bool flag = false;
							line = "";

							for (char ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize((line + ch).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(line);
									line = "", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(line);
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}
					}
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));

						string author = get<string>(i18n[i18nEnum::Settings_Home_Thanks]);
						int left_x = 10, right_x = 370;

						vector<string> lines;
						string line, temp;
						stringstream ss(author);

						while (getline(ss, temp, '\n'))
						{
							bool flag = false;
							line = "";

							for (char ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize((line + ch).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back((line));
									line = "", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back((line));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}
					}
					{
						ImGui::SetCursorPos({ 770.0f - 395.0f,606.0f - 160.0f });

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 100 / 255.0f, 203 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 89 / 255.0f, 180 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						if (ImGui::Button(get<string>(i18n[i18nEnum::Settings_Home_Button1]).c_str(), { 190.0f,60.0f }))
						{
						}
					}
					{
						ImGui::SetCursorPos({ 770.0f - 200.0f,606.0f - 160.0f });

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						if (ImGui::Button(get<string>(i18n[i18nEnum::Settings_Home_Button2]).c_str(), { 190.0f,60.0f }))
						{
							Tab = Tab::tab6;
						}
					}
					{
						ImGui::SetCursorPos({ 770.0f - 395.0f,606.0f - 95.0f });

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						if (ImGui::Button(get<string>(i18n[i18nEnum::Settings_Home_Button3]).c_str(), { 190.0f,30.0f }))
						{
							ShellExecute(0, 0, L"https://github.com/Alan-CRL/IDT", 0, 0, SW_SHOW);
						}
					}
					{
						ImGui::SetCursorPos({ 770.0f - 200.0f,606.0f - 95.0f });

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						if (ImGui::Button(get<string>(i18n[i18nEnum::Settings_Home_Button4]).c_str(), { 190.0f,30.0f }))
						{
							ShellExecute(0, 0, L"https://www.inkeys.top", 0, 0, SW_SHOW);
						}
					}
					{
						ImGui::SetCursorPos({ 770.0f - 390.0f,606.0f - 60.0f });

						Font->Scale = 0.65f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Home_Language]).c_str(), 2.0f);

						ImGui::SetCursorPos({ 770.0f - 260.0f,606.0f - 60.0f });
						static const char* items[] = { "  Other Custom Languages ", "  English ", "  Simplified Chinese (简体中文) ", "  Traditional Chinese (正體中文) " };
						ImGui::SetNextItemWidth(250);

						Font->Scale = 0.6f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						ImGui::Combo("##语言选择", &languageMode, items, IM_ARRAYSIZE(items));

						if (languageMode == 1 && i18nIdentifying != L"en-US")
						{
							//WriteSetting();
						}
						if (languageMode == 2 && i18nIdentifying != L"zh-CN")
						{
							//WriteSetting();
						}
						if (languageMode == 3 && i18nIdentifying != L"zh-TW")
						{
							//WriteSetting();
						}
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 常规
				case Tab::tab2:
				{
					ImGui::BeginChild("通用", { 770.0f,616.0f }, true);
					{
						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 child_pos = ImGui::GetWindowPos();
						ImVec2 child_size = ImGui::GetWindowSize();

						ImU32 color_top = IM_COL32(245, 248, 255, 255);
						ImU32 color_middle = IM_COL32(255, 255, 255, 255);

						ImVec2 top_left = child_pos;
						ImVec2 bottom_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

						ImVec2 middle_left = ImVec2(child_pos.x, child_pos.y + child_size.y / 3.0f);
						ImVec2 middle_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y / 3.0f);

						draw_list->AddRectFilledMultiColor(top_left, middle_right, color_top, color_top, color_middle, color_middle);
						draw_list->AddRect(child_pos, bottom_right, ImGui::GetColorU32(ImGuiCol_Border), style.ChildRounding, ImDrawFlags_RoundCornersAll, 1.0f);
					}

					ImGui::SetCursorPosY(20.0f);

					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild("程序环境", { 730.0f,125.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 查询开机启动状态", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker("调整开机自动启动设置前需要查询当前状态（程序将申请管理员权限）", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							if (!receivedData.empty())
							{
								string temp, helptemp;
								if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Succe") temp = "查询状态成功", helptemp = "可以调整开机启动设置";
								else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Error") temp = "查询状态错误 " + utf16ToUtf8(receivedData), helptemp = "再次点击查询尝试，或重启程序以管理员身份运行";
								else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"TimeO") temp = "查询状态超时", helptemp = "再次点击查询尝试\n同时超时时间将从 5 秒调整为 15 秒", QueryWaitingTime = 15;
								else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Renew") temp = "需要重新查询状态", helptemp = "调整开机自动启动设置时超时，请再次点击查询\n同时超时时间将从 5 秒调整为 15 秒", QueryWaitingTime = 15;
								else temp = "未知错误", helptemp = "再次点击查询尝试，或重启程序以管理员身份运行";

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 80.0f - ImGui::CalcTextSize(temp.c_str()).x - 30.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(temp.c_str(), 4.0f);

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); HelpMarker(helptemp.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
							}

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							if (ImGui::Button("查询", { 60.0f,30.0f }))
							{
								{
									// 检查本地文件完整性
									{
										if (_waccess((globalPath + L"api").c_str(), 0) == -1)
										{
											error_code ec;
											filesystem::create_directory(globalPath + L"api", ec);
										}

										if (_waccess((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
											ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
										else
										{
											string hash_md5, hash_sha256;
											{
												hashwrapper* myWrapper = new md5wrapper();
												hash_md5 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
												delete myWrapper;
											}
											{
												hashwrapper* myWrapper = new sha256wrapper();
												hash_sha256 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
												delete myWrapper;
											}

											if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
												ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
										}
									}

									ShellExecute(NULL, L"runas", (globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"query" + to_wstring(QuestNumbers) + L"\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);
								}
								//if (_waccess((StringToWstring(globalPath) + L"api").c_str(), 0) == -1) filesystem::create_directory(StringToWstring(globalPath) + L"api");
								//ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
								//ShellExecute(NULL, L"runas", (StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"query" + to_wstring(QuestNumbers) + L"\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);

								DWORD dwBytesRead;
								WCHAR buffer[4096];
								HANDLE hPipe = INVALID_HANDLE_VALUE;

								int for_i;
								for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
								{
									if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
									else this_thread::sleep_for(chrono::milliseconds(100));
								}

								if (for_i > QueryWaitingTime * 10) receivedData = L"TimeOut";
								else
								{
									hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
									if (ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL))
									{
										receivedData.assign(buffer, dwBytesRead / sizeof(WCHAR));

										if (receivedData == to_wstring(QuestNumbers) + L"fail")
										{
											receivedData = L"Success";
											setlist.StartUp = 1, StartUp = false;
										}
										else if (receivedData == to_wstring(QuestNumbers) + L"success")
										{
											receivedData = L"Success";
											setlist.StartUp = 2, StartUp = true;
										}
										else receivedData = L"Error unknown";
									}
									else receivedData = L"Error" + to_wstring(GetLastError());
								}
								CloseHandle(hPipe);

								QuestNumbers++, QuestNumbers %= 10;
							}
						}
						{
							ImGui::SetCursorPosY(45.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 开机自动启动", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine();
							if (setlist.StartUp) HelpMarker("程序将申请管理员权限", ImGui::GetStyleColorVec4(ImGuiCol_Text));
							else HelpMarker("调整开机自动启动设置前需要查询当前状态（程序将申请管理员权限）\n请点击上方按钮查询当前状态", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							if (setlist.StartUp)
							{
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
								ImGui::Toggle("##开机自动启动", &StartUp, config);

								if (setlist.StartUp - 1 != (int)StartUp)
								{
									{
										// 检查本地文件完整性
										{
											if (_waccess((globalPath + L"api").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directory(globalPath + L"api", ec);
											}

											if (_waccess((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
												ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											else
											{
												string hash_md5, hash_sha256;
												{
													hashwrapper* myWrapper = new md5wrapper();
													hash_md5 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}
												{
													hashwrapper* myWrapper = new sha256wrapper();
													hash_sha256 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}

												if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
													ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											}
										}

										if (StartUp) ShellExecuteW(NULL, L"runas", (globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"set" + to_wstring(QuestNumbers) + L"\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);
										else ShellExecuteW(NULL, L"runas", (globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"delete" + to_wstring(QuestNumbers) + L"\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);
									}
									//if (_waccess((StringToWstring(globalPath) + L"api").c_str(), 0) == -1) filesystem::create_directory(StringToWstring(globalPath) + L"api");
									//ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));

									DWORD dwBytesRead;
									WCHAR buffer[4096];
									HANDLE hPipe = INVALID_HANDLE_VALUE;
									wstring treceivedData;

									int for_i;
									for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
									{
										if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
										else this_thread::sleep_for(chrono::milliseconds(100));
									}

									if (for_i <= QueryWaitingTime * 10)
									{
										hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
										if (ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL))
										{
											treceivedData.assign(buffer, dwBytesRead / sizeof(WCHAR));
											if (treceivedData == to_wstring(QuestNumbers) + L"fail") setlist.StartUp = 1, StartUp = false;
											else if (treceivedData == to_wstring(QuestNumbers) + L"success") setlist.StartUp = 2, StartUp = true;
										}
									}
									else setlist.StartUp = 0, receivedData = L"Renew";

									CloseHandle(hPipe);
									QuestNumbers++, QuestNumbers %= 10;

									if (setlist.StartUp - 1 != (int)StartUp) setlist.StartUp = 0, receivedData = L"Renew";
								}
							}
						}
						{
							ImGui::SetCursorPosY(80.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 启动时创建桌面快捷方式", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine();
							HelpMarker("程序将在每次启动时在桌面创建快捷方式\n后续这项功能将变身成为插件，并拥有更多的自定义功能", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle("##启动时创建桌面快捷方式", &CreateLnk, config);

							if (setlist.CreateLnk != CreateLnk)
							{
								setlist.CreateLnk = CreateLnk;
								WriteSetting();

								if (CreateLnk) SetShortcut();
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild("外观调整", { 730.0f,50.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(8.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 外观皮肤", 4.0f);

							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 130.0f);
							static const char* items[] = { "  推荐皮肤", "  默认皮肤", "  极简时钟", "  龙年迎新" };
							ImGui::SetNextItemWidth(120);

							Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							ImGui::Combo("##外观皮肤", &SetSkinMode, items, IM_ARRAYSIZE(items));

							if (setlist.SetSkinMode != SetSkinMode)
							{
								setlist.SetSkinMode = SetSkinMode;
								WriteSetting();

								if (SetSkinMode == 0) setlist.SkinMode = 1;
								else setlist.SkinMode = SetSkinMode;
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild("画笔调整", { 730.0f,125.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 右键主栏图标时弹窗提示关闭程序", 4.0f);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle("##右键主栏图标时弹窗提示关闭程序", &RightClickClose, config);

							if (setlist.RightClickClose != RightClickClose)
							{
								setlist.RightClickClose = RightClickClose;
								WriteSetting();
							}
						}
						{
							ImGui::SetCursorPosY(45.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 画笔绘制时自动收起主栏", 4.0f);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle("##画笔绘制时自动收起主栏", &BrushRecover, config);

							if (setlist.BrushRecover != BrushRecover)
							{
								setlist.BrushRecover = BrushRecover;
								WriteSetting();
							}
						}
						{
							ImGui::SetCursorPosY(80.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 橡皮擦除时自动收起主栏", 4.0f);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle("##橡皮擦除时自动收起主栏", &RubberRecover, config);

							if (setlist.RubberRecover != RubberRecover)
							{
								setlist.RubberRecover = RubberRecover;
								WriteSetting();
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild("兼容性调整", { 730.0f,105.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 兼容自动隐藏的任务栏", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker("如果您设置了“自动隐藏任务栏”选项，则建议打开此项\n\n开启后：绘制效果可能会收到影响\n关闭后：有其他最大化应用时，则无法使用鼠标升起任务栏", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle("##兼容自动隐藏的任务栏", &CompatibleTaskBarAutoHide, config);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							{
								string temp = "软件只在启动时做出调整，修改此项需要重启智绘教才能生效。";
								float text_width = ImGui::CalcTextSize(temp.c_str()).x;
								float text_indentation = ((730 - 0) - text_width) * 0.5f;
								if (text_indentation < 0)  text_indentation = 0;
								ImGui::SetCursorPosX(0 + text_indentation);
								ImGui::TextUnformatted(temp.c_str());
							}
							{
								string temp = "开启此项时，如果您调整“自动隐藏任务栏”选项或改变任务栏方位，则需要重启智绘教。";
								float text_width = ImGui::CalcTextSize(temp.c_str()).x;
								float text_indentation = ((730 - 0) - text_width) * 0.5f;
								if (text_indentation < 0)  text_indentation = 0;
								ImGui::SetCursorPosX(0 + text_indentation);
								ImGui::TextUnformatted(temp.c_str());
							}

							if (setlist.compatibleTaskBarAutoHide != CompatibleTaskBarAutoHide)
							{
								setlist.compatibleTaskBarAutoHide = CompatibleTaskBarAutoHide;
								WriteSetting();
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					while (PushFontNum) PushFontNum--, ImGui::PopFont();
					ImGui::EndChild();
					break;
				}

				// 绘制
				case Tab::tab3:
				{
					ImGui::BeginChild("绘制", { 770.0f,616.0f }, true);
					{
						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 child_pos = ImGui::GetWindowPos();
						ImVec2 child_size = ImGui::GetWindowSize();

						ImU32 color_top = IM_COL32(245, 248, 255, 255);
						ImU32 color_middle = IM_COL32(255, 255, 255, 255);

						ImVec2 top_left = child_pos;
						ImVec2 bottom_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

						ImVec2 middle_left = ImVec2(child_pos.x, child_pos.y + child_size.y / 3.0f);
						ImVec2 middle_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y / 3.0f);

						draw_list->AddRectFilledMultiColor(top_left, middle_right, color_top, color_top, color_middle, color_middle);
						draw_list->AddRect(child_pos, bottom_right, ImGui::GetColorU32(ImGuiCol_Border), style.ChildRounding, ImDrawFlags_RoundCornersAll, 1.0f);
					}

					ImGui::SetCursorPosY(20.0f);
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild("智能绘图调整", { 730.0f,90.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 智能绘图", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker("绘制时停留可以将与直线相似的墨迹拉直\n抬笔时可以将与直线相似的墨迹拉直（精度较高）\n还可以直线吸附和矩形吸附", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle("##智能绘图", &IntelligentDrawing, config);

							if (setlist.IntelligentDrawing != IntelligentDrawing)
							{
								setlist.IntelligentDrawing = IntelligentDrawing;
								WriteSetting();
							}
						}
						{
							ImGui::SetCursorPosY(45.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 平滑墨迹", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker("抬笔时自动平滑墨迹", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle("##平滑墨迹", &SmoothWriting, config);

							if (setlist.SmoothWriting != SmoothWriting)
							{
								setlist.SmoothWriting = SmoothWriting;
								WriteSetting();
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild("橡皮调整", { 730.0f,50.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(8.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 橡皮粗细灵敏度", 4.0f);

							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 130.0f);
							static const char* items[] = { "  触摸设备", "  PC 鼠标" };
							ImGui::SetNextItemWidth(120);

							Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							ImGui::Combo("##橡皮粗细灵敏度", &RubberMode, items, IM_ARRAYSIZE(items));

							if (setlist.RubberMode != RubberMode)
							{
								setlist.RubberMode = RubberMode;
								WriteSetting();
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					ImGui::EndChild();
					break;
				}

				// 插件
				case Tab::tab4:
				{
					ImGui::BeginChild("插件", { 770.0f,616.0f }, true);
					{
						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 child_pos = ImGui::GetWindowPos();
						ImVec2 child_size = ImGui::GetWindowSize();

						ImU32 color_top = IM_COL32(245, 248, 255, 255);
						ImU32 color_middle = IM_COL32(255, 255, 255, 255);

						ImVec2 top_left = child_pos;
						ImVec2 bottom_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

						ImVec2 middle_left = ImVec2(child_pos.x, child_pos.y + child_size.y / 3.0f);
						ImVec2 middle_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y / 3.0f);

						draw_list->AddRectFilledMultiColor(top_left, middle_right, color_top, color_top, color_middle, color_middle);
						draw_list->AddRect(child_pos, bottom_right, ImGui::GetColorU32(ImGuiCol_Border), style.ChildRounding, ImDrawFlags_RoundCornersAll, 1.0f);
					}

					ImGui::SetCursorPosY(20.0f);

					// 侧栏按钮
					{
						ImGui::SetCursorPos({ 10.0f,10.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, TabPlugInIdx == TabPlugIn::tabPlug1 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, TabPlugInIdx == TabPlugIn::tabPlug1 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
						if (TabPlugInIdx == TabPlugIn::tabPlug1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						if (TabPlugInIdx == TabPlugIn::tabPlug1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						if (ImGui::Button("插件商店", { 150.0f,40.0f }))
						{
							TabPlugInIdx = TabPlugIn::tabPlug1;
						}

						{
							ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 6.0f });
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, TabPlugInIdx == TabPlugIn::tabPlug2 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, TabPlugInIdx == TabPlugIn::tabPlug2 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
							if (TabPlugInIdx == TabPlugIn::tabPlug2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							if (TabPlugInIdx == TabPlugIn::tabPlug2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							if (ImGui::Button("PPT 演示联动", { 150.0f,40.0f }))
							{
								TabPlugInIdx = TabPlugIn::tabPlug2;
							}
						}

						if (ddbSetList.DdbEnable)
						{
							ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 6.0f });
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, TabPlugInIdx == TabPlugIn::tabPlug3 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, TabPlugInIdx == TabPlugIn::tabPlug3 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
							if (TabPlugInIdx == TabPlugIn::tabPlug3) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							if (TabPlugInIdx == TabPlugIn::tabPlug3) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							if (ImGui::Button("画板浮窗拦截", { 150.0f,40.0f }))
							{
								TabPlugInIdx = TabPlugIn::tabPlug3;
							}
						}
					}

					ImGui::SetCursorPos({ 170.0f,10.0f });
					switch (TabPlugInIdx)
					{
					case TabPlugIn::tabPlug1:
						// 插件商店
						ImGui::BeginChild("插件商店", { 590.0f,596.0f }, true);

						// PPT 演示联动插件
						{
							ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 10.0f });
							ImGui::BeginChild("PPT 演示联动插件", { 550.0f,120.0f }, true);

							{
								ImGui::SetCursorPos({ 10.0f,10.0f });
								ImGui::Image((void*)TextureSettingSign[5], ImVec2((float)SettingSign[5].getwidth(), (float)SettingSign[5].getheight()));

								ImGui::SetCursorPos({ 120.0f,10.0f });

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText("PPT 演示联动插件", 4.0f);

								// 版本号
								{
									wstring version;
									if (pptComVersion.substr(0, 7) == L"Error: ") version = L"插件发生错误 版本号未知";
									else version = pptComVersion;

									Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f));
									ImGui::SameLine(); CenteredText(utf16ToUtf8(version).c_str(), 8.0f);
								}

								ImGui::NewLine(); ImGui::SetCursorPosX(120.0f);
								CenteredText("支持 Microsoft PowerPoint 和 WPS 等演示软件，\n可以让笔迹固定在页面上，同时提供快捷底栏交互控件。\n绘制模式下可以自由翻页，对 PPT 原有功能无限制。", 10.0f);
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						// 画板浮窗拦截插件
						{
							ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 10.0f });
							ImGui::BeginChild("画板浮窗拦截插件", { 550.0f,120.0f }, true);

							{
								ImGui::SetCursorPos({ 10.0f,10.0f });
								ImGui::Image((void*)TextureSettingSign[6], ImVec2((float)SettingSign[6].getwidth(), (float)SettingSign[6].getheight()));

								ImGui::SetCursorPos({ 120.0f,10.0f });

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText("画板浮窗拦截插件", 4.0f);

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f));
								ImGui::SameLine(); CenteredText(utf16ToUtf8(ddbSetList.DdbEdition).c_str(), 8.0f);

								ImGui::NewLine(); ImGui::SetCursorPosX(120.0f);
								CenteredText("剔除桌面上 希沃白板桌面悬浮窗 等\n杂乱无章的桌面画板悬浮窗，支持拦截各类\n桌面画板悬浮窗，以及 PPT 小工具等操控栏。", 10.0f);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPos({ 550.0f - 70.0f,10.0f });
								ImGui::Toggle("##画板浮窗拦截插件", &DdbEnable, config);

								if (ddbSetList.DdbEnable != DdbEnable)
								{
									ddbSetList.DdbEnable = DdbEnable;

									if (ddbSetList.DdbEnable)
									{
										ddbSetList.hostPath = GetCurrentExePath();
										if (_waccess((globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), 0) == -1)
										{
											if (_waccess((globalPath + L"PlugIn\\DDB").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directories(globalPath + L"PlugIn\\DDB", ec);
											}
											ExtractResource((globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
										}
										else
										{
											string hash_sha256;
											{
												hashwrapper* myWrapper = new sha256wrapper();
												hash_sha256 = myWrapper->getHashFromFileW(globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe");
												delete myWrapper;
											}

											if (hash_sha256 != ddbSetList.DdbSHA256)
												ExtractResource((globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
										}

										// 创建开机自启标识
										if (ddbSetList.DdbEnhance && _waccess((globalPath + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == -1)
										{
											std::ofstream file((globalPath + L"PlugIn\\DDB\\start_up.signal").c_str());
											file.close();
										}
										// 启动 DDB
										if (!isProcessRunning((globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str()))
										{
											DdbWriteSetting(true, false);
											ShellExecute(NULL, NULL, (globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
										}

										ddbSetList.DdbEnable = true;
										WriteSetting();
									}
									else
									{
										DdbWriteSetting(true, true);

										// 移除开机自启标识
										if (_waccess((globalPath + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == 0)
										{
											error_code ec;
											filesystem::remove(globalPath + L"PlugIn\\DDB\\start_up.signal", ec);
										}

										ddbSetList.DdbEnable = false;
										WriteSetting();
									}
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;

					case TabPlugIn::tabPlug2:
						// PPT 演示联动
						ImGui::BeginChild("PPT 演示联动", { 590.0f,596.0f }, true);

						{
							ImGui::SetCursorPos({ 20.0f,20.0f });
							ImGui::BeginChild("PPT 演示联动行为调整", { 550.0f,60.0f }, true, ImGuiWindowFlags_NoScrollbar);

							{
								ImGui::SetCursorPosY(10.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 墨迹固定在对应页面上", 4.0f);

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); HelpMarker("每个 PPT 页面都将拥有各自独立的画布\n（翻页不会清空之前页面所绘制的墨迹，可以返回前一页面继续绘制）", ImGui::GetStyleColorVec4(ImGuiCol_Text));

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								ImGui::Toggle("##墨迹固定在对应页面上", &PptComFixedHandWriting, config);

								if (pptComSetlist.fixedHandWriting != PptComFixedHandWriting)
								{
									pptComSetlist.fixedHandWriting = PptComFixedHandWriting;
									PptComWriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 5.0f });
							ImGui::BeginChild("PPT 演示联动控件显示调整", { 550.0f,125.0f }, true, ImGuiWindowFlags_NoScrollbar);

							{
								ImGui::SetCursorPosY(10.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 显示底部两侧控件", 4.0f);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								ImGui::Toggle("##显示底部两侧控件", &ShowBottomBoth, config);

								if (pptComSetlist.showBottomBoth != ShowBottomBoth)
								{
									pptComSetlist.showBottomBoth = ShowBottomBoth;
									PptComWriteSetting();
								}
							}
							{
								ImGui::SetCursorPosY(45.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 显示中部两侧控件", 4.0f);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								ImGui::Toggle("##显示中部两侧控件", &ShowMiddleBoth, config);

								if (pptComSetlist.showMiddleBoth != ShowMiddleBoth)
								{
									pptComSetlist.showMiddleBoth = ShowMiddleBoth;
									PptComWriteSetting();
								}
							}
							{
								ImGui::SetCursorPosY(80.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 显示底部主栏控件", 4.0f);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								ImGui::Toggle("##显示底部主栏控件", &ShowBottomMiddle, config);

								if (pptComSetlist.showBottomMiddle != ShowBottomMiddle)
								{
									pptComSetlist.showBottomMiddle = ShowBottomMiddle;
									PptComWriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 5.0f });
							ImGui::BeginChild("PPT 演示联动控件位置调整", { 550.0f,90.0f }, true, ImGuiWindowFlags_NoScrollbar);

							{
								ImGui::SetCursorPosY(10.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 控件位置", 4.0f);

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								if (ImGui::Button("重置", { 60.0f,30.0f }))
								{
									pptComSetlist.bottomBothWidth = BottomBothWidth = 0;
									pptComSetlist.bottomBothHeight = BottomBothHeight = 0;
									pptComSetlist.middleBothWidth = MiddleBothWidth = 0;
									pptComSetlist.middleBothHeight = MiddleBothHeight = 0;
									pptComSetlist.bottomMiddleWidth = BottomMiddleWidth = 0;
									pptComSetlist.bottomMiddleHeight = BottomMiddleHeight = 0;

									PptComWriteSetting();
								}
							}
							{
								ImGui::SetCursorPosY(45.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 记忆控件位置", 4.0f);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								ImGui::Toggle("##记忆控件位置", &MemoryWidgetPosition, config);

								if (pptComSetlist.memoryWidgetPosition != MemoryWidgetPosition)
								{
									pptComSetlist.memoryWidgetPosition = MemoryWidgetPosition;
									PptComWriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 5.0f });
							ImGui::BeginChild("PPT 演示联动控件缩放调整", { 550.0f,195.0f }, true, ImGuiWindowFlags_NoScrollbar);

							{
								ImGui::SetCursorPosY(10.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 控件缩放", 4.0f);

								Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::PushItemWidth(300.0f);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f - 305.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(175 / 255.0f, 175 / 255.0f, 175 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(155 / 255.0f, 155 / 255.0f, 155 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								ImGui::SliderFloat("##PPT 控件缩放总控", &PptUiWidgetScaleWidget, 0.25f, 3.0f, "%.15f 倍缩放");
								ImGui::PopItemWidth();
								if (PptUiWidgetScaleWidget != PptUiWidgetScaleWidgetRecord)
								{
									PptUiWidgetScaleWidgetRecord = PptUiWidgetScaleWidget;
									pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = PptUiWidgetScaleWidget;
									pptComSetlist.bottomSideBothWidgetScale = MiddleSideBothWidgetScale = PptUiWidgetScaleWidget;
									pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = PptUiWidgetScaleWidget;
								}

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								if (ImGui::Button("重置##PPT 控件缩放总控", { 60.0f,30.0f }))
								{
									PptUiWidgetScaleWidgetRecord = PptUiWidgetScaleWidget = 1.0f;
									pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = 1.0f;
									pptComSetlist.bottomSideBothWidgetScale = MiddleSideBothWidgetScale = 1.0f;
									pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = 1.0f;

									PptComWriteSetting();
								}
							}
							{
								ImGui::SetCursorPosY(45.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 底部两侧控件", 4.0f);

								Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::PushItemWidth(300.0f);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f - 305.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(175 / 255.0f, 175 / 255.0f, 175 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(155 / 255.0f, 155 / 255.0f, 155 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								ImGui::SliderFloat("##PPT 底部两侧控件", &BottomSideBothWidgetScale, 0.25f, 3.0f, "%.15f 倍缩放");
								ImGui::PopItemWidth();
								if (pptComSetlist.bottomSideBothWidgetScale != BottomSideBothWidgetScale)
								{
									pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
								}

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								if (ImGui::Button("重置##PPT 底部两侧控件", { 60.0f,30.0f }))
								{
									pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = 1.0f;
									PptComWriteSetting();
								}
							}
							{
								ImGui::SetCursorPosY(80.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 中部两侧控件", 4.0f);

								Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::PushItemWidth(300.0f);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f - 305.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(175 / 255.0f, 175 / 255.0f, 175 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(155 / 255.0f, 155 / 255.0f, 155 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								ImGui::SliderFloat("##PPT 中部两侧控件", &MiddleSideBothWidgetScale, 0.25f, 3.0f, "%.15f 倍缩放");
								ImGui::PopItemWidth();
								if (pptComSetlist.middleSideBothWidgetScale != MiddleSideBothWidgetScale)
								{
									pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
								}

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								if (ImGui::Button("重置##PPT 底部两侧控件", { 60.0f,30.0f }))
								{
									pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = 1.0f;
									PptComWriteSetting();
								}
							}
							{
								ImGui::SetCursorPosY(115.0f);

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 底部主栏控件", 4.0f);

								Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::PushItemWidth(300.0f);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f - 305.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(175 / 255.0f, 175 / 255.0f, 175 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(155 / 255.0f, 155 / 255.0f, 155 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								ImGui::SliderFloat("##PPT 底部主栏控件", &BottomSideMiddleWidgetScale, 0.25f, 3.0f, "%.15f 倍缩放");
								ImGui::PopItemWidth();
								if (pptComSetlist.bottomSideMiddleWidgetScale != BottomSideMiddleWidgetScale)
								{
									pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale;
								}

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SameLine(); ImGui::SetCursorPosX(550.0f - 70.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								if (ImGui::Button("重置##PPT 底部主栏控件", { 60.0f,30.0f }))
								{
									pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = 1.0f;
									PptComWriteSetting();
								}
							}

							{
								ImGui::SetCursorPosY(155.0f);
								Font->Scale = 0.8f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(235 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(" 修改滑块后需要点击保存才能将配置保存本地", 4.0f);

								ImGui::SetCursorPosY(155.0f);
								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								ImGui::SetCursorPosX(550.0f - 70.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								if (ImGui::Button("保存##PPT 控件缩放总控", { 60.0f,30.0f }))
								{
									PptComWriteSetting();
								}
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;

					case TabPlugIn::tabPlug3:
						// 画板浮窗拦截
						ImGui::BeginChild("画板浮窗拦截", { 590.0f,596.0f }, true);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 增强模式", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker("启用增强模式后，画板浮窗拦截程序将开机自动启动，并与智绘教独立运行，互不干扰。\n同时在智绘教关闭状态下，当插件拦截到其他软件的悬浮窗，则会重启智绘教。", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(590.0f - 70.0f);
							ImGui::Toggle("##智能绘图", &DdbEnhance, config);

							if (ddbSetList.DdbEnhance != DdbEnhance)
							{
								ddbSetList.DdbEnhance = DdbEnhance;
								WriteSetting();

								if (ddbSetList.DdbEnhance)
								{
									ddbSetList.mode = 0;
									ddbSetList.restartHost = true;
									DdbWriteSetting(true, false);

									// 创建开机自启标识
									if (_waccess((globalPath + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == -1)
									{
										std::ofstream file(globalPath + L"PlugIn\\DDB\\start_up.signal");
										file.close();
									}

									// 设置开机自启
									{
										// 检查本地文件完整性
										{
											if (_waccess((globalPath + L"api").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directory(globalPath + L"api", ec);
											}

											if (_waccess((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
												ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											else
											{
												string hash_md5, hash_sha256;
												{
													hashwrapper* myWrapper = new md5wrapper();
													hash_md5 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}
												{
													hashwrapper* myWrapper = new sha256wrapper();
													hash_sha256 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}

												if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
													ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											}
										}

										ShellExecuteW(NULL, L"runas", (globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"set0\" /\"" + globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe\" /\"IdtDesktopDrawpadBlockerStartUp\"").c_str(), NULL, SW_SHOWNORMAL);
									}

									DWORD dwBytesRead;
									WCHAR buffer[4096];
									HANDLE hPipe = INVALID_HANDLE_VALUE;
									wstring treceivedData;

									int for_i;
									for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
									{
										if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
										else this_thread::sleep_for(chrono::milliseconds(100));
									}

									if (for_i <= QueryWaitingTime * 10)
									{
										hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
										ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL);
									}
								}
								else
								{
									ddbSetList.mode = 1;
									ddbSetList.restartHost = true;
									DdbWriteSetting(true, false);

									// 移除开机自启标识
									if (_waccess((globalPath + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == 0)
									{
										error_code ec;
										filesystem::remove(globalPath + L"PlugIn\\DDB\\start_up.signal", ec);
									}

									// 移除开机自启
									{
										// 检查本地文件完整性
										{
											if (_waccess((globalPath + L"api").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directory(globalPath + L"api", ec);
											}

											if (_waccess((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
												ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											else
											{
												string hash_md5, hash_sha256;
												{
													hashwrapper* myWrapper = new md5wrapper();
													hash_md5 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}
												{
													hashwrapper* myWrapper = new sha256wrapper();
													hash_sha256 = myWrapper->getHashFromFileW(globalPath + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}

												if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
													ExtractResource((globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											}
										}

										ShellExecuteW(NULL, L"runas", (globalPath + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"delete0\" /\"" + globalPath + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe\" /\"IdtDesktopDrawpadBlockerStartUp\"").c_str(), NULL, SW_SHOWNORMAL);
									}

									DWORD dwBytesRead;
									WCHAR buffer[4096];
									HANDLE hPipe = INVALID_HANDLE_VALUE;
									wstring treceivedData;

									int for_i;
									for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
									{
										if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
										else this_thread::sleep_for(chrono::milliseconds(100));
									}

									if (for_i <= QueryWaitingTime * 10)
									{
										hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
										ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL);
									}
								}
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 快捷键
				case Tab::tab5:
				{
					ImGui::BeginChild("快捷键", { 770.0f,616.0f }, true);
					{
						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 child_pos = ImGui::GetWindowPos();
						ImVec2 child_size = ImGui::GetWindowSize();

						ImU32 color_top = IM_COL32(245, 248, 255, 255);
						ImU32 color_middle = IM_COL32(255, 255, 255, 255);

						ImVec2 top_left = child_pos;
						ImVec2 bottom_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

						ImVec2 middle_left = ImVec2(child_pos.x, child_pos.y + child_size.y / 3.0f);
						ImVec2 middle_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y / 3.0f);

						draw_list->AddRectFilledMultiColor(top_left, middle_right, color_top, color_top, color_middle, color_middle);
						draw_list->AddRect(child_pos, bottom_right, ImGui::GetColorU32(ImGuiCol_Border), style.ChildRounding, ImDrawFlags_RoundCornersAll, 1.0f);
					}

					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
					{
						ImGui::SetCursorPosY(10.0f);

						int left_x = 10, right_x = 760;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(L"按下 Ctrl + Win + Alt 切换选择/画笔\n\nCtrl + Q 定格\nCtrl + E 穿透\n\nZ（绘制模式下） 撤回/超级恢复\n\n其余快捷键和自定义正在测试，敬请期待");

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(utf16ToUtf8(line));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(utf16ToUtf8(line));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 感谢墙
				case Tab::tab6:
				{
					ImGui::BeginChild("感谢墙", { 770.0f,616.0f }, true);
					{
						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 child_pos = ImGui::GetWindowPos();
						ImVec2 child_size = ImGui::GetWindowSize();

						ImU32 color_top = IM_COL32(245, 248, 255, 255);
						ImU32 color_middle = IM_COL32(255, 255, 255, 255);

						ImVec2 top_left = child_pos;
						ImVec2 bottom_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

						ImVec2 middle_left = ImVec2(child_pos.x, child_pos.y + child_size.y / 3.0f);
						ImVec2 middle_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y / 3.0f);

						draw_list->AddRectFilledMultiColor(top_left, middle_right, color_top, color_top, color_middle, color_middle);
						draw_list->AddRect(child_pos, bottom_right, ImGui::GetColorU32(ImGuiCol_Border), style.ChildRounding, ImDrawFlags_RoundCornersAll, 1.0f);
					}

					ImGui::SetCursorPos({ 10.0f,10.0f });
					ImGui::Image((void*)TextureSettingSign[7], ImVec2((float)SettingSign[7].getwidth(), (float)SettingSign[7].getheight()));

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 程序版本
				case Tab::tab7:
				{
					ImGui::BeginChild("关于", { 770.0f,616.0f }, true);
					{
						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 child_pos = ImGui::GetWindowPos();
						ImVec2 child_size = ImGui::GetWindowSize();

						ImU32 color_top = IM_COL32(245, 248, 255, 255);
						ImU32 color_middle = IM_COL32(255, 255, 255, 255);

						ImVec2 top_left = child_pos;
						ImVec2 bottom_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

						ImVec2 middle_left = ImVec2(child_pos.x, child_pos.y + child_size.y / 3.0f);
						ImVec2 middle_right = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y / 3.0f);

						draw_list->AddRectFilledMultiColor(top_left, middle_right, color_top, color_top, color_middle, color_middle);
						draw_list->AddRect(child_pos, bottom_right, ImGui::GetColorU32(ImGuiCol_Border), style.ChildRounding, ImDrawFlags_RoundCornersAll, 1.0f);
					}

					ImGui::SetCursorPos({ 35.0f,70.0f });
					ImGui::Image((void*)TextureSettingSign[1], ImVec2((float)SettingSign[1].getwidth(), (float)SettingSign[1].getheight()));

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);
						wstring text;
						{
							text = L"程序版本代号 " + editionCode;
							text += L"\n程序发布版本 " + editionDate + L"(" + editionChannel + L")";
							text += L"\n程序构建时间 " + buildTime;

#ifdef IDT_RELEASE
							text += L"\n程序构建模式 IDT_RELEASE ";
#else
							text += L"\n程序构建模式 IDT_DEBUG（非发布调测版本）";
#endif

							if (userId == L"Error") text += L"\n用户ID无法正确识别";
							else text += L"\n用户ID " + userId;

							text += L"\n\n在此版本中，您的所有数据都将在本地进行处理";
						}

						int left_x = 10, right_x = 760;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(text);

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(utf16ToUtf8(line));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(utf16ToUtf8(line));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}
					}

					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
					{
						ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 30.0f });
						ImGui::BeginChild("更新通道调整", { 730.0f,500.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 更新通道", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker("正式通道(LTS) 提供经过验证的稳定程序版本\n公测通道(Beta) 提供稳定性一般的程序版本\n非正式通道程序均未提交杀软进行防误报处理\n\n一旦更新，则无法通过自动更新回退版本\n当选择的更新通道不可用时，则会切换回默认通道", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 180.0f);
							ImGui::SetNextItemWidth(170);

							Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

							int UpdateChannelMode;
							if (UpdateChannel == "LTS" || UpdateChannel == "Beta")
							{
								if (UpdateChannel == "LTS") UpdateChannelMode = 0;
								else if (UpdateChannel == "Beta") UpdateChannelMode = 1;

								static const char* items1[] = { " 正式通道(LTS)", " 公测通道(Beta)" };

								ImGui::Combo("##更新通道", &UpdateChannelMode, items1, IM_ARRAYSIZE(items1));
							}
							else
							{
								UpdateChannelMode = 2;

								static const char* items2[] = { " 正式通道(LTS)", " 公测通道(Beta)", " 其他通道" };

								ImGui::Combo("##更新通道", &UpdateChannelMode, items2, IM_ARRAYSIZE(items2));
							}

							bool flag = false;
							if (UpdateChannelMode == 0 && UpdateChannel != "LTS") UpdateChannel = "LTS", flag = true;
							else if (UpdateChannelMode == 1 && UpdateChannel != "Beta") UpdateChannel = "Beta", flag = true;

							if (flag && setlist.UpdateChannel != UpdateChannel)
							{
								AutomaticUpdateStep = 1;
								setlist.UpdateChannel = UpdateChannel;
								WriteSetting();
							}
							else if (setlist.UpdateChannel != UpdateChannel) UpdateChannel = setlist.UpdateChannel;
						}
						{
							ImGui::SetCursorPosY(45.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 更新日志", 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); CenteredText(("当前版本" + utf16ToUtf8(editionDate)).c_str(), 8.0f);

							ImGui::SetCursorPos({ 20.0f,90.0f });
							Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
							{
								ImGui::BeginChild("更新日志", { 690.0f,400.0f }, true, ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

								ImGui::SetCursorPosY(10.0f);
								wstring text;
								{
									text +=
										L"智绘教20240619a --------------------\n"
										L"+ 在系统保护的目录下运行程序会提示用户\n"
										L"= 更新后软件文件名称将添加版本号\n"
										L"- 修复了 ppt 控件点击和长按异常的问题\n"
										L"- 去除了没有太大用处的 智绘教CrashedHandlerClose 子程序\n"
										L"\n"
										L"智绘教20240604c --------------------\n"
										L"+ 新增软件感谢墙\n"
										L"+ 新增更新日志\n"
										L"= 优化设置布局和样式\n"
										L"	智绘教20240601b(Beta)\n"
										L"		+ 新增插件图标\n"
										L"		+ 新增智绘教状态监控模块\n"
										L"		+ 新增穿透和定格的快捷键\n"
										L"		+ 新增右键主栏图标时弹窗提示关闭程序开关\n"
										L"		- 修复 PPT 联动插件版本号设定错误\n"
										L"		- 修复开机自启动被错误设置\n"
										L"	智绘教20240519b(Beta)\n"
										L"		+ 新增 希沃白板3 鸿合桌面白板 窗口拦截功能\n"
										L"		+ 新增 PPT 翻页时不更新笔迹选项\n"
										L"		= 优化逻辑，多屏用户解除限制，但只能在主监视器上绘图\n"
										L"		= 优化逻辑，禁用窗口定格可以正常使用超级恢复\n"
										L"		- 修复了部分系统下窗口无法连续置顶的问题\n"
										L"		- 修复了 PPT 下页码显示区域不正常的问题\n"
										L"		- 修复了 PPT 状态显示错位的问题\n"
										L"		- 修复了 PPT 放映结束页页码错误的问题\n"
										L"		- 修复了在窗口定格不可用时定格窗口异常的问题\n"
										L"		- 修复定格下智绘教窗口被错误包含的问题\n"
										L"		- 修复部分 win7 下窗口定格模块中放大API异常的问题\n"
										L"		- 修复了DDB在增强模式下无法被更新的问题\n"
										L"		- 修复了 64 位 ms office 无法使用联动的问题\n"
										L"		- 修复了部分 win7 下窗口定格模块导致程序崩溃的问题\n"
										L"		- 修复了 PPT 控件反应过慢的问题\n"
										L"	智绘教20240513b(Beta)\n"
										L"		+ 新增插件模块\n"
										L"		+ 新增 DDB 插件可拦截桌面画板悬浮窗\n"
										L"		= 保存的历史图片名称改为使用时间戳\n"
										L"		= 优化 PPT 控件加载延迟\n"
										L"		= 智绘教调整为不能重复启动\n"
										L"		- 修复程序名不为智绘教时无法使用崩溃助手\n"
										L"		- 修复 PPT 模式下笔迹被多次重复保存到本地的情况\n"
										L"		- 修复多次弹出是否关闭智绘教弹窗\n"
										L"		- 修复点击按钮结束放映后，鼠标翻页反应不能重置的问题\n"
										L"		- 修复非放映模式下错误的出现PPT控件的问题\n"
										L"\n"
										L"智绘教20240504a --------------------\n"
										L"+ 发布智绘教更新模块2.0 支持选择更新通道 LTS / Beta（更快，更安全 + 双次双码校验）\n"
										L"+ 支持日志文件 7 天自动清理和总日志文件大小限制 10MB\n"
										L"+ 继续完善日志系统\n"
										L"+ 增加PPT鼠标滚轮翻页支持\n"
										L"+ 启用程序新标志和名称\n"
										L"+ 重写用户ID模块，统一ID格式\n"
										L"= 调整开机启动项程序，为插件系统铺路\n"
										L"= 优化定格模块窗口管理\n"
										L"- 修复程序文件名不为 \"智绘教.exe\" 时不能自动更新的问题\n"
										L"- 修复焦点被占用情况下无法拖动窗口问题\n"
										L"- 修复了 PPT 控件中文字字体不正确的问题\n"
										L"\n"
										L"智绘教20240427a --------------------\n"
										L"+ 新增 4K 及以上分辨率监视器支持\n"
										L"+ 完善部分日志记录模块\n"
										L"+ 在 win7 发生窗口穿透问题时会提示重启\n"
										L"+ 更新时系统通知提示（在 Win8 及以上系统中才会启用；自 智绘教20240421a - 智绘教20240427a）\n"
										L"= 大幅降低运行内存占用（自 智绘教20240421a - 智绘教20240427a）\n"
										L"- 修复稳定性相关问题\n"
										L"- 优化代码逻辑，消灭编译警告\n"
										L"\n"
										L"智绘教20240421a --------------------\n"
										L"+ 新增重启程序按钮\n"
										L"+ 新增在画板无法绘制时会提示重启\n"
										L"+ 试用日志记录系统\n"
										L"- 修复窗口置顶中可能存在的问题\n"
										L"- 修复关闭程序后弹窗崩溃重启助手提示的问题\n"
										L"\n"
										L"智绘教20240317a --------------------\n"
										L"+ PPT 联动部分 UI 启动 D2D 显卡加速绘制\n"
										L"- 修复 PPT 载入界面重复出现 BUG\n"
										L"- 修复 PPT 组件显示效果 BUG\n"
										L"\n"
										L"智绘教20240309b --------------------\n"
										L"+ 重写 PPT 联动部分 UI，并增加动画\n"
										L"+ 测试性地启用了全局快捷键 Ctrl + Win + Alt\n"
										L"- 修复了 PPT 组件下按钮失效的问题\n"
										L"\n"
										L"智绘教20240228a --------------------\n"
										L"+ 新增了对 WPS 的联动支持\n"
										L"+ PPT 联动控件底层重写，新的按键消息处理机制\n"
										L"+ 支持键盘和界面按钮长按翻页\n"
										L"+ 新增 PC鼠标 橡皮粗细灵敏度预设\n"
										L"= 选项界面增加更多丰富选项\n"
										L"\n"
										L"智绘教20240220c --------------------\n"
										L"+ 新增皮肤选择功能\n"
										L"+ 新增极简时钟皮肤\n"
										L"+ 新增程序更新提示\n"
										L"+ 启用程序更新高速服务器\n"
										L"= 移除智绘教后台统计模块\n"
										L"= 降低界面帧率，降低CPU占用率\n"
										L"- 修复开机启动查询异常问题\n"
										L"- 修复快捷快捷方式设置异常问题\n"
										L"- 修复CPU占用过大问题\n"
										L"\n"
										L"智绘教20240217a --------------------\n"
										L"+ 新增全新选项界面\n"
										L"+ 新增开机启动管理程序\n"
										L"+ 新增欢迎向导demo\n"
										L"= 优化程序逻辑提示体验\n"
										L"= 优化了程序选项本地保存方式\n"
										L"- 修复了历史画板报错图片半透明像素异常的问题\n"
										L"- 修复了COM类库连接导致的内存泄露问题\n"
										L"- 修复了超级恢复和撤回不连续的问题\n"
										L"- 修复了部分系统下开启白屏的问题\n"
										L"- 修复了窗口置顶冲突的问题\n"
										L"\n"
										L"智绘教20240128a --------------------\n"
										L"+ 新增：多指绘图（多指书写、直线、矩形与橡皮）\n"
										L"+ 新增：超级恢复功能，可恢复历史画板\n"
										L"+ 新增：撤回功能全面重做，二阶恢复功能\n"
										L"+ 新增：延迟记录撤回步骤，一秒内的书写笔迹算为一步\n"
										L"+ 新增：撤回记录自动保存到本地\n"
										L"+ 新增：龙年限时皮肤\n"
										L"= 优化：当桌面快捷方式已经创建时，则不刷新桌面\n"
										L"= 优化：在程序关闭时，会有提示窗\n"
										L"= 优化：在PPT结束放映时，会有提示窗提示结束放映会丢失绘制笔迹\n"
										L"= 优化：快速点击按钮时，仅会当做单击处理\n"
										L"= 优化：绘制效果，效率增高\n"
										L"= 优化：进入同一PPT时，仅展示一次加载动画\n"
										L"= 优化：对多拓展显示器的设备有弹窗提示，多拓展显示器的设备可使用智绘教基础功能\n"
										L"- 修复：窗口移动进度异常的问题\n"
										L"- 修复：在PPT放映时，点击结束放映按钮后，响应速度有延迟的问题\n"
										L"- 修复：在PPT放映时，控件不置顶和无法绘制的问题\n"
										L"- 修复：在PPT放映时，并缩放后无法切换页面的问题\n"
										L"- 修复：在PPT加载动画展示时打开穿透导致控件闪烁的问题\n"
										L"- 修复：橡皮按下但不能擦除的问题\n"
										L"- 修复：橡皮擦除时是六边形的问题\n"
										L"- 修复：橡皮擦除时粗细变化不均匀的问题\n"
										L"- 修复：刷新帧率不均匀的问题\n"
										L"- 修复：未绘制内容也显示恢复按钮的问题\n"
										L"- 修复：画板内容被误清空的问题\n"
										L"- 修复：Windows7下启动程序需要管理员权限的问题\n"
										L"- 修复：开启窗口定格时主窗口会闪烁的问题\n"
										L"- 修复：波浪线被识别成直线精度异常的问题\n"
										L"- 修复：在关闭实验性新功能的情况下，程序卡死的问题\n"
										L"\n"
										L"智绘教20231106a --------------------\n"
										L"+ 新增程序启动会自动在桌面创建快捷方式\n"
										L"= 优化代码逻辑，大幅度提高了书写流畅度\n"
										L"= 优化PPT界面切换时响应速度\n"
										L"= 优化智能橡皮粗细，符合一体机操作习惯\n"
										L"= 优化了智能绘图的代码，提示响应速度\n"
										L"= 修改放大API工作方式，减少内存和CPU占用\n"
										L"- 修复了在有未完成绘制任务时点击撤回导致崩溃的情况\n"
										L"- 修复了PPT界面穿透离开后，未监测到PPT已结束放映的问题\n"
										L"\n"
										L"智绘教20231013a --------------------\n"
										L"+ 使用上下文API，在补全 DLL 后再加载清单。这是为了程序单文件也可以启动。\n"
										L"\n"
										L"智绘教20231009a --------------------\n"
										L"+ 转换 PPT联动插件 类库，并启用免注册 COM 组件\n"
										L"\n"
										L"智绘教20231008a --------------------\n"
										L"+ 新增窗口定格\n"
										L"+ 新增智能粗细橡皮擦\n"
										L"+ 新增画笔粗细条件\n"
										L"+ 新增选色轮\n"
										L"+ 新增 3 种配色，并优化部分颜色\n"
										L"+ 智能绘图模块：矩形绘制新增四点吸附\n"
										L"+ 智能绘图模块：自动直线新增吸附\n"
										L"+ 新增崩溃保护\n"
										L"+ 在 希沃视频展台 开启绘制时，会自动打开窗口定格\n"
										L"+ 在 MsPPT 开始放映时，会显示加载画面\n"
										L"= 调整窗口穿透按钮窗口位置\n"
										L"= 调整历史画板保存API\n"
										L"= 在 win7 下新增对 随机点名 插件的支持\n"
										L"= 触控调测 改版变为 选项\n"
										L"- 修复自动更新错误识别成 Https ://\n"
										L"\n"
										L"智绘教20230920b --------------------\n"
										L"智绘教正式发布！";
								}

								vector<string> lines;
								wstring temp;
								wstringstream ss(text);

								while (getline(ss, temp, L'\n'))
								{
									lines.emplace_back(utf16ToUtf8(temp));
								}

								for (const auto& temp : lines)
								{
									ImGui::SetCursorPosX(13);
									ImGui::TextUnformatted(temp.c_str());
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// ---------------------

				// 实验室
				case Tab::tab8:
				{
					ImGui::BeginChild("实验室", { 770.0f,616.0f }, true);

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// 程序调测
				case Tab::tab9:
				{
					ImGui::BeginChild("程序调测", { 770.0f,616.0f }, true);

					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
					{
						ImGui::SetCursorPosY(10.0f);
						wstring text;
						{
							GetCursorPos(&pt);

							text = L"鼠标左键按下：";
							text += KeyBoradDown[VK_LBUTTON] ? L"是" : L"否";
							text += L"\n光标坐标 " + to_wstring(pt.x) + L"," + to_wstring(pt.y);

							if (uRealTimeStylus == 2) text += L"\n\n触控库消息：按下";
							else if (uRealTimeStylus == 3) text += L"\n\n触控库消息：抬起";
							else if (uRealTimeStylus == 4) text += L"\n\n触控库消息：移动";
							else text += L"\n\n触控库消息：就绪";

							text += L"\n触控按下：";
							text += touchDown ? L"是" : L"否";
							text += L"\n按下触控点数量：";
							text += to_wstring(touchNum);

							for (int i = 0; i < touchNum; i++)
							{
								std::shared_lock<std::shared_mutex> lock1(PointPosSm);
								POINT pt = TouchPos[TouchList[i]].pt;
								lock1.unlock();

								std::shared_lock<std::shared_mutex> lock2(TouchSpeedSm);
								double speed = TouchSpeed[TouchList[i]];
								lock2.unlock();

								text += L"\n触控点" + to_wstring(i + 1) + L" pid" + to_wstring(TouchList[i]) + L" 坐标" + to_wstring(pt.x) + L"," + to_wstring(pt.y) + L" 速度" + to_wstring(speed);
							}

							text += L"\n\nTouchList ";
							for (const auto& val : TouchList)
							{
								text += to_wstring(val) + L" ";
							}
							text += L"\nTouchTemp ";
							for (size_t i = 0; i < TouchTemp.size(); ++i)
							{
								text += to_wstring(TouchTemp[i].pid) + L" ";
							}

							text += L"\n\n撤回库当前大小：" + to_wstring(RecallImage.size()) + L"(峰值" + to_wstring(RecallImagePeak) + L")";
							text += L"\n撤回库 recall_image_recond：" + to_wstring(recall_image_recond);
							text += L"\n撤回库 reference_record_pointer：" + to_wstring(reference_record_pointer);
							text += L"\n撤回库 practical_total_record_pointer：" + to_wstring(practical_total_record_pointer);
							text += L"\n撤回库 total_record_pointer：" + to_wstring(total_record_pointer);
							text += L"\n撤回库 current_record_pointer：" + to_wstring(current_record_pointer);
							text += L"\n\n首次绘制状态：", text += (FirstDraw == true) ? L"是" : L"否";

							{
								wstring ppt_LinkTest;
								if (pptComVersion.substr(0, 7) == L"Error: ") ppt_LinkTest = L"发生错误 " + pptComVersion;
								else ppt_LinkTest = L"连接成功，版本 " + pptComVersion;

								text += L"\n\nPPT COM接口 联动组件 状态：";
								text += ppt_LinkTest;
							}

							text += L"\n\nPPT 状态：";
							text += PptInfoState.TotalPage != -1 ? L"正在播放" : L"未播放";
							text += L"\nPPT 总页面数：";
							text += to_wstring(PptInfoState.TotalPage);
							text += L"\nPPT 当前页序号：";
							text += to_wstring(PptInfoState.CurrentPage);
						}

						int left_x = 10, right_x = 760;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(text);

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(utf16ToUtf8(line + ch).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(utf16ToUtf8(line));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(utf16ToUtf8(line));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}

						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}
				}

				{
					if (AutomaticUpdateStep == 0)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip0]).c_str()).x,44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(150 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip0]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 1)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip1]).c_str()).x,44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(150 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip1]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 2)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip2]).c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip2]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 3)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip3]).c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip3]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 4)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip4]).c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip4]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 5)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip5]).c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip5]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 6)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip6]).c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip6]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 7)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip7]).c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip7]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 8)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip8]).c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip8]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 9)
					{
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize((get<string>(i18n[i18nEnum::Settings_Update_Tip9]) + "(" + setlist.UpdateChannel + ")").c_str()).x , 44.0f + 616.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(98 / 255.0f, 175 / 255.0f, 82 / 255.0f, 1.0f));
						CenteredText((get<string>(i18n[i18nEnum::Settings_Update_Tip9]) + "(" + setlist.UpdateChannel + ")").c_str(), 4.0f);
					}
				}

				{
					if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
					if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
					while (PushFontNum) PushFontNum--, ImGui::PopFont();
				}
				ImGui::End();
			}

			// 渲染
			ImGui::EndFrame();
			g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
			g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
			g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
			D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
			g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
			if (g_pd3dDevice->BeginScene() >= 0)
			{
				ImGui::Render();
				ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
				g_pd3dDevice->EndScene();
			}
			HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
			if (result == D3DERR_DEVICELOST) g_DeviceLost = true;

			if (!test.select) break;
			if (!ShowWindow && !IsWindowVisible(setting_window))
			{
				::ShowWindow(setting_window, SW_SHOW);
				::SetForegroundWindow(setting_window);
				ShowWindow = true;
			}
		}

		//::ShowWindow(setting_window, SW_HIDE);

		io.Fonts->Clear();

		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	threadStatus[L"SettingMain"] = false;
	return 0;
}

bool ReadSetting(bool first)
{
	Json::Reader reader;
	Json::Value root;

	ifstream readjson;
	readjson.imbue(locale("zh_CN.UTF8"));
	readjson.open(globalPath + L"opt\\deploy.json");

	if (reader.parse(readjson, root))
	{
		if (root.isMember("CreateLnk")) setlist.CreateLnk = root["CreateLnk"].asBool();
		if (root.isMember("RightClickClose")) setlist.RightClickClose = root["RightClickClose"].asBool();
		if (root.isMember("BrushRecover")) setlist.BrushRecover = root["BrushRecover"].asBool();
		if (root.isMember("RubberRecover")) setlist.RubberRecover = root["RubberRecover"].asBool();
		if (root.isMember("CompatibleTaskBarAutoHide")) setlist.compatibleTaskBarAutoHide = root["CompatibleTaskBarAutoHide"].asBool();
		if (root.isMember("RubberMode")) setlist.RubberMode = root["RubberMode"].asBool();
		if (root.isMember("IntelligentDrawing")) setlist.IntelligentDrawing = root["IntelligentDrawing"].asBool();
		if (root.isMember("SmoothWriting")) setlist.SmoothWriting = root["SmoothWriting"].asBool();
		if (root.isMember("SetSkinMode")) setlist.SetSkinMode = root["SetSkinMode"].asInt();
		if (root.isMember("UpdateChannel")) setlist.UpdateChannel = root["UpdateChannel"].asString();

		if (root.isMember("PlugIn"))
		{
			if (root["PlugIn"].isMember("DdbEnable")) ddbSetList.DdbEnable = root["PlugIn"]["DdbEnable"].asBool();
			if (root["PlugIn"].isMember("DdbEnhance")) ddbSetList.DdbEnhance = root["PlugIn"]["DdbEnhance"].asBool();
		}

		//预处理
		if (first)
		{
			if (setlist.SetSkinMode == 0) setlist.SkinMode = 1;
			else setlist.SkinMode = setlist.SetSkinMode;
		}
	}

	readjson.close();

	return true;
}
bool WriteSetting()
{
	if (_waccess((globalPath + L"opt").c_str(), 0) == -1)
		filesystem::create_directory(globalPath + L"opt");

	Json::Value root;

	root["edition"] = Json::Value(utf16ToUtf8(editionDate));
	root["CreateLnk"] = Json::Value(setlist.CreateLnk);
	root["RightClickClose"] = Json::Value(setlist.RightClickClose);
	root["BrushRecover"] = Json::Value(setlist.BrushRecover);
	root["RubberRecover"] = Json::Value(setlist.RubberRecover);
	root["CompatibleTaskBarAutoHide"] = Json::Value(setlist.compatibleTaskBarAutoHide);
	root["RubberMode"] = Json::Value(setlist.RubberMode);
	root["IntelligentDrawing"] = Json::Value(setlist.IntelligentDrawing);
	root["SmoothWriting"] = Json::Value(setlist.SmoothWriting);
	root["SetSkinMode"] = Json::Value(setlist.SetSkinMode);
	root["UpdateChannel"] = Json::Value(setlist.UpdateChannel);

	root["PlugIn"]["DdbEnable"] = Json::Value(ddbSetList.DdbEnable);
	root["PlugIn"]["DdbEnhance"] = Json::Value(ddbSetList.DdbEnhance);

	Json::StreamWriterBuilder outjson;
	outjson.settings_["emitUTF8"] = true;
	std::unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
	ofstream writejson;
	writejson.imbue(locale("zh_CN.UTF8"));
	writejson.open(globalPath + L"opt\\deploy.json");
	writer->write(root, &writejson);
	writejson.close();

	return true;
}

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
	if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
		return false;

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

// 从 imgui_impl_win32.cpp 中前向声明消息处理器
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// Win32 消息处理器
// 您可以阅读 io.WantCaptureMouse、io.WantCaptureKeyboard 标志，以了解 dear imgui 是否想使用您的输入。
// - 当 io.WantCaptureMouse 为 true 时，请勿将鼠标输入数据发送到主应用程序，或者清除/覆盖鼠标数据的副本。
// - 当 io.WantCaptureKeyboard 为 true 时，请勿将键盘输入数据发送到主应用程序，或者清除/覆盖键盘数据的副本。
// 通常，您可以始终将所有输入传递给 dear imgui，并根据这两个标志在应用程序中隐藏它们。
LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
		g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 示例
static void HelpMarker(const char* desc, ImVec4 Color)
{
	ImGui::TextColored(Color, "(?)");
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