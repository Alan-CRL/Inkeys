#include "IdtSetting.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtHash.h"
#include "IdtHistoricalDrawpad.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

#include "imgui/imconfig.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_toggle.h"
#include "imgui/imgui_toggle_presets.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"
#include <tchar.h>

// 示例
static void HelpMarker(const char* desc, ImVec4 Color);
static void CenteredText(const char* desc, float displacement);

IMAGE SettingSign[5];
WNDCLASSEX ImGuiWc;
ID3D11ShaderResourceView* TextureSettingSign[5];
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
		ImGuiWc = { sizeof(WNDCLASSEX), CS_CLASSDC, ImGuiWndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("Idt ImGui Tool"), NULL };
		::RegisterClassEx(&ImGuiWc);
		setting_window = ::CreateWindow(ImGuiWc.lpszClassName, _T("Idt ImGui Tool"), WS_OVERLAPPEDWINDOW, SettingWindowX, SettingWindowY, SettingWindowWidth, SettingWindowHeight, NULL, NULL, ImGuiWc.hInstance, NULL);

		SetWindowLong(setting_window, GWL_STYLE, GetWindowLong(setting_window, GWL_STYLE) & ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME));
		SetWindowPos(setting_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

		::ShowWindow(setting_window, SW_HIDE);
		::UpdateWindow(setting_window);

		magnificationWindowReady++;

		CreateDeviceD3D(setting_window);

		loadimage(&SettingSign[1], L"PNG", L"sign2");
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
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
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

			bool ret = LoadTextureFromFile(data, width, height, &TextureSettingSign[1]);
			delete[] data;

			IM_ASSERT(ret);
		}
		loadimage(&SettingSign[2], L"PNG", L"sign3");
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
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
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

			bool ret = LoadTextureFromFile(data, width, height, &TextureSettingSign[2]);
			delete[] data;

			IM_ASSERT(ret);
		}
		loadimage(&SettingSign[3], L"PNG", L"sign4");
		loadimage(&SettingSign[4], L"PNG", L"sign5");
		{
			int width = SettingSign[4].getwidth();
			int height = SettingSign[4].getheight();
			DWORD* pMem = GetImageBuffer(&SettingSign[4]);

			unsigned char* data = new unsigned char[width * height * 4];
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					DWORD color = pMem[y * width + x];
					unsigned char alpha = (color & 0xFF000000) >> 24;
					if (alpha != 0)
					{
						data[(y * width + x) * 4 + 0] = unsigned char(((color & 0x00FF0000) >> 16) * 255 / alpha);
						data[(y * width + x) * 4 + 1] = unsigned char(((color & 0x0000FF00) >> 8) * 255 / alpha);
						data[(y * width + x) * 4 + 2] = unsigned char(((color & 0x000000FF) >> 0) * 255 / alpha);
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

			bool ret = LoadTextureFromFile(data, width, height, &TextureSettingSign[4]);
			delete[] data;

			IM_ASSERT(ret);
		}
	}

	bool ShowWindow = false;
	while (!offSignal)
	{
		::ShowWindow(setting_window, SW_HIDE);
		ShowWindow = false;

		while (!test.select && !offSignal) Sleep(100);
		if (offSignal) break;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.IniFilename = nullptr;

		ImGui::StyleColorsLight();

		ImGui_ImplWin32_Init(setting_window);
		ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

		if (_waccess((StringToWstring(globalPath) + L"ttf\\hmossscr.ttf").c_str(), 0) == -1)
		{
			if (_waccess((StringToWstring(globalPath) + L"ttf").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directory(StringToWstring(globalPath) + L"ttf", ec);
			}
			ExtractResource((StringToWstring(globalPath) + L"ttf\\hmossscr.ttf").c_str(), L"TTF", MAKEINTRESOURCE(198));
		}

		ImFont* Font = io.Fonts->AddFontFromFileTTF(ConvertToUtf8(globalPath + "ttf\\hmossscr.ttf").c_str(), 28.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		ImGuiStyle& style = ImGui::GetStyle();
		auto Color = style.Colors;

		style.ChildRounding = 8.0f;
		style.FrameRounding = 8.0f;
		style.GrabRounding = 8.0f;

		style.Colors[ImGuiCol_WindowBg] = ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f);

		//初始化定义变量
		hiex::tDelayFPS recond;
		POINT MoushPos = { 0,0 };
		ImGuiToggleConfig config;
		config.FrameRounding = 0.3f;
		config.KnobRounding = 0.5f;
		config.Size = { 60.0f,30.0f };
		config.Flags = ImGuiToggleFlags_Animated;

		int QuestNumbers = 0;
		int PushStyleColorNum = 0, PushFontNum = 0;
		int QueryWaitingTime = 5;

		bool StartUp = false, CreateLnk = setlist.CreateLnk;
		bool BrushRecover = setlist.BrushRecover, RubberRecover = setlist.RubberRecover;
		int RubberMode = setlist.RubberMode;
		string UpdateChannel = setlist.UpdateChannel;
		bool IntelligentDrawing = setlist.IntelligentDrawing, SmoothWriting = setlist.SmoothWriting;
		int SetSkinMode = setlist.SetSkinMode;

		// 插件参数

		bool DdbEnable = ddbSetList.DdbEnable;
		bool DdbEnhance = ddbSetList.DdbEnhance;

		// ==========

		wstring ppt_LinkTest = LinkTest();
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

			hiex::DelayFPS(recond, 24);

			ImGui_ImplDX11_NewFrame();
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
					tab8
				};
				static int TabPlugInIdx = 0;
				enum TabPlugIn
				{
					tabPlug1,
					tabPlug2
				};

				ImGui::SetNextWindowPos({ 0,0 });//设置窗口位置
				ImGui::SetNextWindowSize({ static_cast<float>(SettingWindowWidth),static_cast<float>(SettingWindowHeight) });//设置窗口大小

				Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
				ImGui::Begin(reinterpret_cast<const char*>(u8"智绘教选项"), &test.select, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);//开始绘制窗口

				{
					ImGui::SetCursorPos({ 10.0f,44.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab1 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab1 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"主页"), { 100.0f,40.0f }))
					{
						Tab = Tab::tab1;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,94.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab2 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab2 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"通用"), { 100.0f,40.0f }))
					{
						Tab = Tab::tab2;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,144.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab3 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab3 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab3) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab3) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"绘制"), { 100.0f,40.0f }))
					{
						Tab = Tab::tab3;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,194.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab4 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab4 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab4) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab4) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"插件"), { 100.0f,40.0f }))
					{
						Tab = Tab::tab4;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,244.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab5 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab5 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab5) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab5) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"快捷键"), { 100.0f,40.0f }))
					{
						Tab = Tab::tab5;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,294.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab6 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab6 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab6) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab6) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"实验室"), { 100.0f,40.0f }))
					{
						//Tab = Tab::tab6;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,344.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab7 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab7 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab7) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab7) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"程序调测"), { 100.0f,40.0f }))
					{
						Tab = Tab::tab7;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,394.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab8 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab8 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
					if (Tab == Tab::tab8) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (Tab == Tab::tab8) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"关于"), { 100.0f,40.0f }))
					{
						Tab = Tab::tab8;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					// --------------------

					ImGui::SetCursorPos({ 10.0f,44.0f + 616.0f - 65.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"重启程序"), { 100.0f,30.0f }))
					{
						test.select = false;
						offSignal = 2;
					}
					ImGui::PopStyleColor(PushStyleColorNum);

					ImGui::SetCursorPos({ 10.0f,44.0f + 616.0f - 30.0f });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
					if (ImGui::Button(reinterpret_cast<const char*>(u8"关闭程序"), { 100.0f,30.0f }))
					{
						test.select = false;
						offSignal = true;
					}
					ImGui::PopStyleColor(PushStyleColorNum);
				}

				ImGui::SetCursorPos({ 120.0f,44.0f });
				switch (Tab)
				{
				case Tab::tab1:
					// 主页
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"主页"), { 770.0f,616.0f }, true);

					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(235 / 255.0f, 10 / 255.0f, 20 / 255.0f, 1.0f));

						ImGui::SetCursorPosY(10.0f);
						int left_x = 10, right_x = 760;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(L"推荐使用 1080P 分辨率，高于此分辨率可能会引发问题并影响体验\n高分辨率屏幕的适配将与 UI3.0 在稍后同步进行开发");

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(ConvertToUtf8(WstringToString(line + ch)).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(ConvertToUtf8(WstringToString(line)));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(ConvertToUtf8(WstringToString(line)));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}

						ImGui::PopStyleColor(PushStyleColorNum);
					}

					ImGui::SetCursorPos({ 35.0f,60.0f });
					ImGui::Image((void*)TextureSettingSign[2], ImVec2((float)SettingSign[2].getwidth(), (float)SettingSign[2].getheight()));

					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));

						ImGui::SetCursorPosY(616.0f - 168.0f);
						std::wstring author = L"软件作者联系方式\nQQ: 2685549821\nEmail: alan-crl@foxmail.com";
						int left_x = 10, right_x = 370;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(author);

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(ConvertToUtf8(WstringToString(line + ch)).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(ConvertToUtf8(WstringToString(line)));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(ConvertToUtf8(WstringToString(line)));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}

						ImGui::PopStyleColor(PushStyleColorNum);
					}
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));

						std::wstring author = L"官方Q群：618720802\n感谢各位一直以来对智绘教的支持~";
						int left_x = 10, right_x = 370;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(author);

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(ConvertToUtf8(WstringToString(line + ch)).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(ConvertToUtf8(WstringToString(line)));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(ConvertToUtf8(WstringToString(line)));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}

						ImGui::PopStyleColor(PushStyleColorNum);
					}

					ImGui::SetCursorPos({ 760.0f - (float)SettingSign[4].getwidth(),606.0f - (float)SettingSign[4].getheight() });
					ImGui::Image((void*)TextureSettingSign[4], ImVec2((float)SettingSign[4].getwidth(), (float)SettingSign[4].getheight()));

					ImGui::EndChild();
					break;

				case Tab::tab2:
					// 通用
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"通用"), { 770.0f,616.0f }, true);

					ImGui::SetCursorPosY(20.0f);

					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"程序环境"), { 730.0f,125.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 查询开机启动状态"), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker(reinterpret_cast<const char*>(u8"调整开机自动启动设置前需要查询当前状态（程序将申请管理员权限）"), ImGui::GetStyleColorVec4(ImGuiCol_Text));

							if (!receivedData.empty())
							{
								string temp, helptemp;
								if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Succe") temp = reinterpret_cast<const char*>(u8"查询状态成功"), helptemp = reinterpret_cast<const char*>(u8"可以调整开机启动设置");
								else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Error") temp = reinterpret_cast<const char*>(u8"查询状态错误 ") + WstringToString(receivedData), helptemp = reinterpret_cast<const char*>(u8"再次点击查询尝试，或重启程序以管理员身份运行");
								else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"TimeO") temp = reinterpret_cast<const char*>(u8"查询状态超时"), helptemp = reinterpret_cast<const char*>(u8"再次点击查询尝试\n同时超时时间将从 5 秒调整为 15 秒"), QueryWaitingTime = 15;
								else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Renew") temp = reinterpret_cast<const char*>(u8"需要重新查询状态"), helptemp = reinterpret_cast<const char*>(u8"调整开机自动启动设置时超时，请再次点击查询\n同时超时时间将从 5 秒调整为 15 秒"), QueryWaitingTime = 15;
								else temp = reinterpret_cast<const char*>(u8"未知错误"), helptemp = reinterpret_cast<const char*>(u8"再次点击查询尝试，或重启程序以管理员身份运行");

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
							if (ImGui::Button(reinterpret_cast<const char*>(u8"查询"), { 60.0f,30.0f }))
							{
								{
									// 检查本地文件完整性
									{
										if (_waccess((StringToWstring(globalPath) + L"api").c_str(), 0) == -1)
										{
											error_code ec;
											filesystem::create_directory(StringToWstring(globalPath) + L"api", ec);
										}

										if (_waccess((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
											ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
										else
										{
											string hash_md5, hash_sha256;
											{
												hashwrapper* myWrapper = new md5wrapper();
												hash_md5 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
												delete myWrapper;
											}
											{
												hashwrapper* myWrapper = new sha256wrapper();
												hash_sha256 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
												delete myWrapper;
											}

											if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
												ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
										}
									}

									ShellExecute(NULL, L"runas", (StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"query" + to_wstring(QuestNumbers) + L"\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);
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
									else Sleep(100);
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
							ImGui::PopStyleColor(PushStyleColorNum);
						}
						{
							ImGui::SetCursorPosY(45.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 开机自动启动"), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine();
							if (setlist.StartUp) HelpMarker(reinterpret_cast<const char*>(u8"程序将申请管理员权限"), ImGui::GetStyleColorVec4(ImGuiCol_Text));
							else HelpMarker(reinterpret_cast<const char*>(u8"调整开机自动启动设置前需要查询当前状态（程序将申请管理员权限）\n请点击上方按钮查询当前状态"), ImGui::GetStyleColorVec4(ImGuiCol_Text));

							if (setlist.StartUp)
							{
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
								ImGui::Toggle(reinterpret_cast<const char*>(u8"##开机自动启动"), &StartUp, config);

								if (setlist.StartUp - 1 != (int)StartUp)
								{
									{
										// 检查本地文件完整性
										{
											if (_waccess((StringToWstring(globalPath) + L"api").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directory(StringToWstring(globalPath) + L"api", ec);
											}

											if (_waccess((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
												ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											else
											{
												string hash_md5, hash_sha256;
												{
													hashwrapper* myWrapper = new md5wrapper();
													hash_md5 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}
												{
													hashwrapper* myWrapper = new sha256wrapper();
													hash_sha256 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}

												if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
													ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											}
										}

										if (StartUp) ShellExecute(NULL, L"runas", (StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"set" + to_wstring(QuestNumbers) + L"\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);
										else ShellExecute(NULL, L"runas", (StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"delete" + to_wstring(QuestNumbers) + L"\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);
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
										else Sleep(100);
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
								}
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}
						{
							ImGui::SetCursorPosY(80.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 启动时创建桌面快捷方式"), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine();
							HelpMarker(reinterpret_cast<const char*>(u8"程序将在每次启动时在桌面创建快捷方式\n后续这项功能将变身成为插件，并拥有更多的自定义功能"), ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle(reinterpret_cast<const char*>(u8"##启动时创建桌面快捷方式"), &CreateLnk, config);

							if (setlist.CreateLnk != CreateLnk)
							{
								setlist.CreateLnk = CreateLnk;
								WriteSetting();

								if (CreateLnk) SetShortcut();
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}

						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"外观调整"), { 730.0f,50.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(8.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 外观皮肤"), 4.0f);

							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 130.0f);
							static const char* items[] = { reinterpret_cast<const char*>(u8"  推荐皮肤"), reinterpret_cast<const char*>(u8"  默认皮肤"), reinterpret_cast<const char*>(u8"  极简时钟"), reinterpret_cast<const char*>(u8"  龙年迎新") };
							ImGui::SetNextItemWidth(120);

							Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							ImGui::Combo(reinterpret_cast<const char*>(u8"##外观皮肤"), &SetSkinMode, items, IM_ARRAYSIZE(items));

							if (setlist.SetSkinMode != SetSkinMode)
							{
								setlist.SetSkinMode = SetSkinMode;
								WriteSetting();

								if (SetSkinMode == 0) setlist.SkinMode = 1;
								else setlist.SkinMode = SetSkinMode;
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}

						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"画笔调整"), { 730.0f,90.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 画笔绘制时自动收起主栏"), 4.0f);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle(reinterpret_cast<const char*>(u8"##画笔绘制时自动收起主栏"), &BrushRecover, config);

							if (setlist.BrushRecover != BrushRecover)
							{
								setlist.BrushRecover = BrushRecover;
								WriteSetting();
							}
						}
						{
							ImGui::SetCursorPosY(45.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 橡皮擦除时自动收起主栏"), 4.0f);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle(reinterpret_cast<const char*>(u8"##橡皮擦除时自动收起主栏"), &RubberRecover, config);

							if (setlist.RubberRecover != RubberRecover)
							{
								setlist.RubberRecover = RubberRecover;
								WriteSetting();
							}
						}

						ImGui::EndChild();
					}

					ImGui::EndChild();
					break;

				case Tab::tab3:
					// 绘制
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"绘制"), { 770.0f,616.0f }, true);

					ImGui::SetCursorPosY(20.0f);
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"智能绘图调整"), { 730.0f,90.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 智能绘图"), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker(reinterpret_cast<const char*>(u8"绘制时停留可以将与直线相似的墨迹拉直\n抬笔时可以将与直线相似的墨迹拉直（精度较高）\n还可以直线吸附和矩形吸附"), ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle(reinterpret_cast<const char*>(u8"##智能绘图"), &IntelligentDrawing, config);

							if (setlist.IntelligentDrawing != IntelligentDrawing)
							{
								setlist.IntelligentDrawing = IntelligentDrawing;
								WriteSetting();
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}
						{
							ImGui::SetCursorPosY(45.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 平滑墨迹"), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker(reinterpret_cast<const char*>(u8"抬笔时自动平滑墨迹"), ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle(reinterpret_cast<const char*>(u8"##平滑墨迹"), &SmoothWriting, config);

							if (setlist.SmoothWriting != SmoothWriting)
							{
								setlist.SmoothWriting = SmoothWriting;
								WriteSetting();
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}

						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"橡皮调整"), { 730.0f,50.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(8.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 橡皮粗细灵敏度"), 4.0f);

							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 130.0f);
							static const char* items[] = { reinterpret_cast<const char*>(u8"  触摸设备"), reinterpret_cast<const char*>(u8"  PC 鼠标") };
							ImGui::SetNextItemWidth(120);

							Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							ImGui::Combo(reinterpret_cast<const char*>(u8"##橡皮粗细灵敏度"), &RubberMode, items, IM_ARRAYSIZE(items));

							if (setlist.RubberMode != RubberMode)
							{
								setlist.RubberMode = RubberMode;
								WriteSetting();
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}

						ImGui::EndChild();
					}

					ImGui::EndChild();
					break;

				case Tab::tab4:
					// 插件
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"插件"), { 770.0f,616.0f }, true);

					ImGui::SetCursorPosY(20.0f);

					// 侧栏按钮
					{
						ImGui::SetCursorPos({ 10.0f,10.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, TabPlugInIdx == TabPlugIn::tabPlug1 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, TabPlugInIdx == TabPlugIn::tabPlug1 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
						if (TabPlugInIdx == TabPlugIn::tabPlug1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						if (TabPlugInIdx == TabPlugIn::tabPlug1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						if (ImGui::Button(reinterpret_cast<const char*>(u8"插件商店"), { 150.0f,40.0f }))
						{
							TabPlugInIdx = TabPlugIn::tabPlug1;
						}
						ImGui::PopStyleColor(PushStyleColorNum);

						if (ddbSetList.DdbEnable)
						{
							ImGui::SetCursorPos({ 10.0f,ImGui::GetCursorPosY() + 6.0f });
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, TabPlugInIdx == TabPlugIn::tabPlug2 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, TabPlugInIdx == TabPlugIn::tabPlug2 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
							if (TabPlugInIdx == TabPlugIn::tabPlug2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							if (TabPlugInIdx == TabPlugIn::tabPlug2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							if (ImGui::Button(reinterpret_cast<const char*>(u8"画板浮窗拦截"), { 150.0f,40.0f }))
							{
								TabPlugInIdx = TabPlugIn::tabPlug2;
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}
					}

					ImGui::SetCursorPos({ 170.0f,10.0f });
					switch (TabPlugInIdx)
					{
					case TabPlugIn::tabPlug1:
						// 插件商店
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"插件商店"), { 590.0f,596.0f }, true);

						// 画板浮窗拦截插件
						{
							ImGui::SetCursorPos({ 20.0f,10.0f });
							ImGui::BeginChild(reinterpret_cast<const char*>(u8"画板浮窗拦截插件"), { 550.0f,120.0f }, true);

							{
								ImGui::SetCursorPos({ 120.0f,10.0f });

								Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								CenteredText(reinterpret_cast<const char*>(u8"画板浮窗拦截插件"), 4.0f);

								Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f));
								ImGui::SameLine(); CenteredText(reinterpret_cast<const char*>(WstringToString(ddbSetList.DdbEdition).c_str()), 8.0f);

								ImGui::NewLine(); ImGui::SetCursorPosX(120.0f);
								CenteredText(reinterpret_cast<const char*>(u8"剔除桌面上 希沃白板桌面悬浮窗 等\n杂乱无章的桌面画板悬浮窗，支持拦截各类\n桌面画板悬浮窗，以及 PPT 小工具等操控栏。"), 10.0f);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::SameLine(); ImGui::SetCursorPos({ 550.0f - 70.0f,10.0f });
								ImGui::Toggle(reinterpret_cast<const char*>(u8"##画板浮窗拦截插件"), &DdbEnable, config);

								if (ddbSetList.DdbEnable != DdbEnable)
								{
									ddbSetList.DdbEnable = DdbEnable;

									if (ddbSetList.DdbEnable)
									{
										ddbSetList.hostPath = GetCurrentExePath();
										if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), 0) == -1)
										{
											if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directories(StringToWstring(globalPath) + L"PlugIn\\DDB", ec);
											}
											ExtractResource((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
										}
										else
										{
											string hash_sha256;
											{
												hashwrapper* myWrapper = new sha256wrapper();
												hash_sha256 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe");
												delete myWrapper;
											}

											if (hash_sha256 != ddbSetList.DdbSHA256)
												ExtractResource((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
										}

										// 创建开机自启标识
										if (ddbSetList.DdbEnhance && _waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == -1)
										{
											std::ofstream file((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str());
											file.close();
										}
										// 启动 DDB
										if (!isProcessRunning((StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str()))
										{
											DdbWriteSetting(true, false);
											ShellExecute(NULL, NULL, (StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
										}

										ddbSetList.DdbEnable = true;
										WriteSetting();
									}
									else
									{
										DdbWriteSetting(true, true);

										// 移除开机自启标识
										if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == 0)
										{
											error_code ec;
											filesystem::remove(StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal", ec);
										}

										ddbSetList.DdbEnable = false;
										WriteSetting();
									}
								}

								ImGui::PopStyleColor(PushStyleColorNum);
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}

							ImGui::EndChild();
						}

						ImGui::EndChild();
						break;

					case TabPlugIn::tabPlug2:
						// 画板浮窗拦截
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"画板浮窗拦截"), { 590.0f,596.0f }, true);

						{
							ImGui::SetCursorPosY(10.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 增强模式"), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker(reinterpret_cast<const char*>(u8"启用增强模式后，画板浮窗拦截程序将开机自动启动，并与智绘教独立运行，互不干扰。\n同时在智绘教关闭状态下，当插件拦截到其他软件的悬浮窗，则会重启智绘教。"), ImGui::GetStyleColorVec4(ImGuiCol_Text));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(590.0f - 70.0f);
							ImGui::Toggle(reinterpret_cast<const char*>(u8"##智能绘图"), &DdbEnhance, config);

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
									if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == -1)
									{
										std::ofstream file((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str());
										file.close();
									}

									// 设置开机自启
									{
										// 检查本地文件完整性
										{
											if (_waccess((StringToWstring(globalPath) + L"api").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directory(StringToWstring(globalPath) + L"api", ec);
											}

											if (_waccess((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
												ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											else
											{
												string hash_md5, hash_sha256;
												{
													hashwrapper* myWrapper = new md5wrapper();
													hash_md5 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}
												{
													hashwrapper* myWrapper = new sha256wrapper();
													hash_sha256 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}

												if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
													ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											}
										}

										ShellExecute(NULL, L"runas", (StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"set0\" /\"" + StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe\" /\"IdtDesktopDrawpadBlockerStartUp\"").c_str(), NULL, SW_SHOWNORMAL);
									}

									DWORD dwBytesRead;
									WCHAR buffer[4096];
									HANDLE hPipe = INVALID_HANDLE_VALUE;
									wstring treceivedData;

									int for_i;
									for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
									{
										if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
										else Sleep(100);
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
									if (_waccess((StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal").c_str(), 0) == 0)
									{
										error_code ec;
										filesystem::remove(StringToWstring(globalPath) + L"PlugIn\\DDB\\start_up.signal", ec);
									}

									// 移除开机自启
									{
										// 检查本地文件完整性
										{
											if (_waccess((StringToWstring(globalPath) + L"api").c_str(), 0) == -1)
											{
												error_code ec;
												filesystem::create_directory(StringToWstring(globalPath) + L"api", ec);
											}

											if (_waccess((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), 0) == -1)
												ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											else
											{
												string hash_md5, hash_sha256;
												{
													hashwrapper* myWrapper = new md5wrapper();
													hash_md5 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}
												{
													hashwrapper* myWrapper = new sha256wrapper();
													hash_sha256 = myWrapper->getHashFromFileW(StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe");
													delete myWrapper;
												}

												if (hash_md5 != StartupItemSettingsMd5 || hash_sha256 != StartupItemSettingsSHA256)
													ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));
											}
										}

										ShellExecute(NULL, L"runas", (StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"delete0\" /\"" + StringToWstring(globalPath) + L"PlugIn\\DDB\\DesktopDrawpadBlocker.exe\" /\"IdtDesktopDrawpadBlockerStartUp\"").c_str(), NULL, SW_SHOWNORMAL);
									}

									DWORD dwBytesRead;
									WCHAR buffer[4096];
									HANDLE hPipe = INVALID_HANDLE_VALUE;
									wstring treceivedData;

									int for_i;
									for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
									{
										if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
										else Sleep(100);
									}

									if (for_i <= QueryWaitingTime * 10)
									{
										hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
										ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL);
									}
								}
							}
							ImGui::PopStyleColor(PushStyleColorNum);
						}

						ImGui::EndChild();
						break;
					}

					ImGui::EndChild();
					break;

				case Tab::tab5:
					// 快捷键
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"快捷键"), { 770.0f,616.0f }, true);

					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
					{
						ImGui::SetCursorPosY(10.0f);

						int left_x = 10, right_x = 760;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(L"按下 Ctrl + Win + Alt 切换选择/画笔\n\n其余快捷键和自定义正在测试，敬请期待");

						while (getline(ss, temp, L'\n'))
						{
							bool flag = false;
							line = L"";

							for (wchar_t ch : temp)
							{
								flag = false;

								float text_width = ImGui::CalcTextSize(ConvertToUtf8(WstringToString(line + ch)).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(ConvertToUtf8(WstringToString(line)));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(ConvertToUtf8(WstringToString(line)));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}
						ImGui::PopStyleColor(PushStyleColorNum);
					}

					ImGui::EndChild();
					break;

				case Tab::tab6:
					// 实验室
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"实验室"), { 770.0f,616.0f }, true);

					ImGui::SetCursorPosY(20.0f);
					{
						ImGui::SetCursorPosX(20.0f);
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"PPT 控件缩放比例"), { 730.0f,50.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(8.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 外观皮肤"), 4.0f);

							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 230.0f);
							ImGui::SetNextItemWidth(220);

							Font->Scale = 0.82f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0 / 255.0f, 131 / 255.0f, 245 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::SliderFloat(reinterpret_cast<const char*>(u8"##PPT 控件缩放比例"), &PPTUIScale, 0.25f, 3.0f, reinterpret_cast<const char*>(u8"%.5f 倍缩放"));

							ImGui::PopStyleColor(PushStyleColorNum);
						}

						ImGui::EndChild();
					}

					ImGui::EndChild();
					break;

				case Tab::tab7:
					// 程序调测
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"程序调测"), { 770.0f,616.0f }, true);

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

							text += L"\n\nPPT COM接口 联动组件 状态：\n";
							text += ppt_LinkTest;

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

								float text_width = ImGui::CalcTextSize(ConvertToUtf8(WstringToString(line + ch)).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(ConvertToUtf8(WstringToString(line)));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(ConvertToUtf8(WstringToString(line)));
						}

						for (const auto& temp : lines)
						{
							float text_width = ImGui::CalcTextSize(temp.c_str()).x;
							float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
							if (text_indentation < 0)  text_indentation = 0;
							ImGui::SetCursorPosX(left_x + text_indentation);
							ImGui::TextUnformatted(temp.c_str());
						}

						ImGui::PopStyleColor(PushStyleColorNum);
					}

					ImGui::EndChild();
					break;

				case Tab::tab8:
					// 关于
					ImGui::BeginChild(reinterpret_cast<const char*>(u8"关于"), { 770.0f,616.0f }, true);

					ImGui::SetCursorPos({ 35.0f,ImGui::GetCursorPosY() + 40.0f });
					ImGui::Image((void*)TextureSettingSign[1], ImVec2((float)SettingSign[1].getwidth(), (float)SettingSign[1].getheight()));

					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
					{
						ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 20.0f });
						ImGui::BeginChild(reinterpret_cast<const char*>(u8"更新通道调整"), { 730.0f,50.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(8.0f);

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(reinterpret_cast<const char*>(u8" 更新通道"), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker(reinterpret_cast<const char*>(u8"正式通道(LTS) 提供经过验证的稳定程序版本\n公测通道(Beta) 提供稳定性一般的程序版本\n非正式通道程序均未提交杀软进行防误报处理\n\n一旦更新，则无法通过自动更新回退版本\n当选择的更新通道不可用时，则会切换回默认通道"), ImGui::GetStyleColorVec4(ImGuiCol_Text));

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

								static const char* items1[] = { reinterpret_cast<const char*>(u8" 正式通道(LTS)"), reinterpret_cast<const char*>(u8" 公测通道(Beta)") };

								ImGui::Combo(reinterpret_cast<const char*>(u8"##更新通道"), &UpdateChannelMode, items1, IM_ARRAYSIZE(items1));
							}
							else
							{
								UpdateChannelMode = 2;

								static const char* items2[] = { reinterpret_cast<const char*>(u8" 正式通道(LTS)"), reinterpret_cast<const char*>(u8" 公测通道(Beta)"), reinterpret_cast<const char*>(u8" 其他通道") };

								ImGui::Combo(reinterpret_cast<const char*>(u8"##更新通道"), &UpdateChannelMode, items2, IM_ARRAYSIZE(items2));
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

						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);
						wstring text;
						{
							text = L"程序版本代号 " + StringToWstring(editionCode);
							text += L"\n程序发布版本 " + StringToWstring(editionDate) + L"(" + StringToWstring(editionChannel) + L")";
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

								float text_width = ImGui::CalcTextSize(ConvertToUtf8(WstringToString(line + ch)).c_str()).x;
								if (text_width > (right_x - left_x))
								{
									lines.emplace_back(ConvertToUtf8(WstringToString(line)));
									line = L"", flag = true;
								}

								line += ch;
							}

							if (!flag) lines.emplace_back(ConvertToUtf8(WstringToString(line)));
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

					ImGui::EndChild();
					break;
				}

				{
					ImGui::SetCursorPos({ 120.0f,44.0f + 616.0f + 5.0f });
					Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
					CenteredText(reinterpret_cast<const char*>(u8"所有设置都会自动保存并立即生效"), 4.0f);
					ImGui::PopStyleColor(PushStyleColorNum);
				}
				{
					if (AutomaticUpdateStep == 0)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新未启动")).x,44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(150 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新未启动"), 4.0f);
					}
					else if (AutomaticUpdateStep == 1)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新载入中")).x,44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(150 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新载入中"), 4.0f);
					}
					else if (AutomaticUpdateStep == 2)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新网络连接错误")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新网络连接错误"), 4.0f);
					}
					else if (AutomaticUpdateStep == 3)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新下载最新版本信息时失败")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新下载最新版本信息时失败"), 4.0f);
					}
					else if (AutomaticUpdateStep == 4)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新下载的最新版本信息不符合规范")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新下载的最新版本信息不符合规范"), 4.0f);
					}
					else if (AutomaticUpdateStep == 5)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新下载的最新版本信息中不包含所选的通道")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新下载的最新版本信息中不包含所选的通道"), 4.0f);
					}
					else if (AutomaticUpdateStep == 6)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"新版本正极速下载中")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"新版本正极速下载中"), 4.0f);
					}
					else if (AutomaticUpdateStep == 7)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新下载最新版本程序失败")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新下载最新版本程序失败"), 4.0f);
					}
					else if (AutomaticUpdateStep == 8)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"程序自动更新下载的最新版本程序损坏")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"程序自动更新下载的最新版本程序损坏"), 4.0f);
					}
					else if (AutomaticUpdateStep == 9)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"重启更新到最新版本")).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(u8"重启更新到最新版本"), 4.0f);
					}
					else if (AutomaticUpdateStep == 10)
					{
						ImGui::SetCursorPos({ 120.0f + 770.0f - ImGui::CalcTextSize(ConvertToUtf8("程序已经是最新版本（" + setlist.UpdateChannel + "通道）").c_str()).x , 44.0f + 616.0f + 5.0f });
						Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(98 / 255.0f, 175 / 255.0f, 82 / 255.0f, 1.0f));
						CenteredText(reinterpret_cast<const char*>(ConvertToUtf8("程序已经是最新版本（" + setlist.UpdateChannel + "通道）").c_str()), 4.0f);
					}

					ImGui::PopStyleColor(PushStyleColorNum);
				}

				ImGui::End();

				ImGui::PopStyleColor(PushStyleColorNum);
				while (PushFontNum) PushFontNum--, ImGui::PopFont();
			}

			// 渲染
			ImGui::Render();
			const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
			g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
			g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			g_pSwapChain->Present(1, 0); // 同步显示
			//g_pSwapChain->Present(0, 0); // 无同步显示

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

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	threadStatus[L"SettingMain"] = false;
	return 0;
}

void FirstSetting(bool info)
{
	if (info && MessageBox(floating_window, L"欢迎使用智绘教~\n本向导还在建设中，目前较为简陋，请谅解\n\n是否尝试设置智绘教开机自动启动？", L"智绘教首次使用向导alpha", MB_YESNO | MB_SYSTEMMODAL) == 6)
	{
		if (_waccess((StringToWstring(globalPath) + L"api").c_str(), 0) == -1) filesystem::create_directory(StringToWstring(globalPath) + L"api");
		ExtractResource((StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));

		ShellExecute(NULL, L"runas", (StringToWstring(globalPath) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"set\" /\"" + GetCurrentExePath() + L"\" /\"xmg_drawpad_startup\"").c_str(), NULL, SW_SHOWNORMAL);

		DWORD dwBytesRead;
		WCHAR buffer[4096];
		HANDLE hPipe = INVALID_HANDLE_VALUE;
		wstring treceivedData;

		int for_i;
		for (for_i = 0; for_i <= 5000; for_i++)
		{
			if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
			else Sleep(100);
		}

		if (for_i <= 5000)
		{
			hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL)) treceivedData.assign(buffer, dwBytesRead / sizeof(WCHAR));
		}

		CloseHandle(hPipe);
	}

	if (_waccess((StringToWstring(globalPath) + L"opt").c_str(), 0) == -1) filesystem::create_directory(StringToWstring(globalPath) + L"opt");
	Json::StyledWriter outjson;
	Json::Value root;

	root["edition"] = Json::Value(editionDate);
	root["CreateLnk"] = Json::Value(false);
	root["BrushRecover"] = Json::Value(true);
	root["RubberRecover"] = Json::Value(false);
	root["RubberMode"] = Json::Value(0);
	root["IntelligentDrawing"] = Json::Value(true);
	root["SmoothWriting"] = Json::Value(true);
	root["SetSkinMode"] = Json::Value(0);
	root["UpdateChannel"] = Json::Value("LTS");

	root["PlugIn"]["DdbEnable"] = Json::Value(true);
	root["PlugIn"]["DdbEnhance"] = Json::Value(false);

	ofstream writejson;
	writejson.imbue(locale("zh_CN.UTF8"));
	writejson.open(WstringToString(StringToWstring(globalPath) + L"opt\\deploy.json").c_str());
	writejson << outjson.write(root);
	writejson.close();
}
bool ReadSetting(bool first)
{
	Json::Reader reader;
	Json::Value root;

	ifstream readjson;
	readjson.imbue(locale("zh_CN.UTF8"));
	readjson.open(WstringToString(StringToWstring(globalPath) + L"opt\\deploy.json").c_str());

	if (reader.parse(readjson, root))
	{
		if (root.isMember("CreateLnk")) setlist.CreateLnk = root["CreateLnk"].asBool();
		if (root.isMember("BrushRecover")) setlist.BrushRecover = root["BrushRecover"].asBool();
		if (root.isMember("RubberRecover")) setlist.RubberRecover = root["RubberRecover"].asBool();
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
	Json::StyledWriter outjson;
	Json::Value root;

	root["edition"] = Json::Value(editionDate);
	root["CreateLnk"] = Json::Value(setlist.CreateLnk);
	root["BrushRecover"] = Json::Value(setlist.BrushRecover);
	root["RubberRecover"] = Json::Value(setlist.RubberRecover);
	root["RubberMode"] = Json::Value(setlist.RubberMode);
	root["IntelligentDrawing"] = Json::Value(setlist.IntelligentDrawing);
	root["SmoothWriting"] = Json::Value(setlist.SmoothWriting);
	root["SetSkinMode"] = Json::Value(setlist.SetSkinMode);
	root["UpdateChannel"] = Json::Value(setlist.UpdateChannel);

	root["PlugIn"]["DdbEnable"] = Json::Value(ddbSetList.DdbEnable);
	root["PlugIn"]["DdbEnhance"] = Json::Value(ddbSetList.DdbEnhance);

	ofstream writejson;
	writejson.imbue(locale("zh_CN.UTF8"));
	writejson.open(WstringToString(StringToWstring(globalPath) + L"opt\\deploy.json").c_str());
	writejson << outjson.write(root);
	writejson.close();

	return true;
}

// 辅助函数
bool CreateDeviceD3D(HWND hWnd)
{
	// 设置交换链
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	// createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // 如果硬件不可用，请尝试使用高性能的 WARP 软件驱动程序。
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}
void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}
void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

bool LoadTextureFromFile(unsigned char* image_data, int width, int height, ID3D11ShaderResourceView** out_srv)
{
	if (image_data == NULL) return false;

	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = image_data;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
	pTexture->Release();

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
		g_ResizeWidth = (UINT)LOWORD(lParam); // 排队调整大小
		g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // 禁用 ALT 应用程序菜单
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
	ImGui::TextColored(Color, reinterpret_cast<const char*>(u8"(?)"));
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