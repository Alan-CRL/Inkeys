#include "IdtSetting.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
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

// 示例
static void HelpMarker(const char* desc, ImVec4 tmp);
static void CenteredText(const char* desc, float displacement);
ImFont* ImFontMain;

IMAGE SettingSign[10];
WNDCLASSEXW ImGuiWc;
PDIRECT3DTEXTURE9 TextureSettingSign[10];
int SettingMainMode = 1;

int SettingWindowX;
int SettingWindowY;
int SettingWindowWidth;
int SettingWindowHeight;

float settingGlobalScale = 1.0f;

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
void SettingWindow(promise<void>& promise)
{
	threadStatus[L"SettingMain"] = true;

	// 创建窗口
	{
		ImGuiWc = { sizeof(WNDCLASSEX), CS_CLASSDC, ImGuiWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, get<wstring>(i18n[i18nEnum::SettingsW]).c_str(), nullptr };
		RegisterClassExW(&ImGuiWc);
		setting_window = CreateWindowW(ImGuiWc.lpszClassName, get<wstring>(i18n[i18nEnum::SettingsW]).c_str(), WS_OVERLAPPEDWINDOW, SettingWindowX, SettingWindowY, SettingWindowWidth, SettingWindowHeight, nullptr, nullptr, ImGuiWc.hInstance, nullptr);
	}
	promise.set_value();

	MSG msg;
	POINT MoushPos = { 0,0 };

	while (!offSignal && GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		switch (msg.message)
		{
		case WM_MOUSEMOVE:
			MoushPos.x = GET_X_LPARAM(msg.lParam);
			MoushPos.y = GET_Y_LPARAM(msg.lParam);

			break;

		case WM_LBUTTONDOWN:
			if (IsInRect(MoushPos.x, MoushPos.y, { 0,0,int(904.0 * settingGlobalScale),int(32.0 * settingGlobalScale) })) SettingSeekBar();
			break;
		}
	}

	threadStatus[L"SettingMain"] = false;
}

void SettingMain()
{
	threadStatus[L"SettingMain"] = true;

	// 初始化部分
	{
		// 缩放计算
		{
			//settingGlobalScale = min((float)MainMonitor.MonitorWidth / 1920.0f, (float)MainMonitor.MonitorHeight / 1080.0f);

			SettingWindowWidth = 950 * settingGlobalScale;
			SettingWindowHeight = 700 * settingGlobalScale;
			SettingWindowX = max(0, (MainMonitor.MonitorWidth - SettingWindowWidth) / 2);
			SettingWindowY = max(0, (MainMonitor.MonitorHeight - SettingWindowHeight) / 2);
		}
		// 窗口初始化
		{
			promise<void> promise;
			future<void> future = promise.get_future();

			thread(SettingWindow, ref(promise)).detach();
			future.get();
		}

		SetWindowLong(setting_window, GWL_STYLE, GetWindowLong(setting_window, GWL_STYLE) & ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME));
		SetWindowPos(setting_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

		ShowWindow(setting_window, SW_HIDE);
		UpdateWindow(setting_window);

		CreateDeviceD3D(setting_window);

		// 图像加载
		{
			if (i18nIdentifying == L"zh-CN") loadimage(&SettingSign[1], L"PNG", L"Home1_zh-CN", 700 * settingGlobalScale, 192 * settingGlobalScale, true);
			else loadimage(&SettingSign[1], L"PNG", L"Home1_en-US", 700 * settingGlobalScale, 192 * settingGlobalScale, true);
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

			if (i18nIdentifying == L"zh-CN") loadimage(&SettingSign[2], L"PNG", L"Home2_zh-CN", 770 * settingGlobalScale, 390 * settingGlobalScale, true);
			else loadimage(&SettingSign[2], L"PNG", L"Home2_en-US", 770 * settingGlobalScale, 390 * settingGlobalScale, true);
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

			loadimage(&SettingSign[4], L"PNG", L"Home_Backgroung", 970 * settingGlobalScale, 775 * settingGlobalScale, true);
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

				bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[4]);
				delete[] data;

				IM_ASSERT(ret);
			}

			loadimage(&SettingSign[3], L"PNG", L"Profile_Picture", 45 * settingGlobalScale, 45 * settingGlobalScale, true);
			{
				int width = SettingSign[3].getwidth();
				int height = SettingSign[3].getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign[3]);

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

				bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[3]);
				delete[] data;

				IM_ASSERT(ret);
			}

			loadimage(&SettingSign[5], L"PNG", L"PluginFlag1", 30 * settingGlobalScale, 30 * settingGlobalScale, true);
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
			loadimage(&SettingSign[6], L"PNG", L"PluginFlag2", 30 * settingGlobalScale, 30 * settingGlobalScale, true);
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
			loadimage(&SettingSign[8], L"PNG", L"PluginFlag3", 30 * settingGlobalScale, 30 * settingGlobalScale, true);
			{
				int width = SettingSign[8].getwidth();
				int height = SettingSign[8].getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign[8]);

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

				bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[8]);
				delete[] data;

				IM_ASSERT(ret);
			}

			loadimage(&SettingSign[7], L"PNG", L"Home_Feedback", 100 * settingGlobalScale, 100 * settingGlobalScale, true);
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
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.IniFilename = nullptr;

		ImGui::StyleColorsLight();

		ImGui_ImplWin32_Init(setting_window);
		ImGui_ImplDX9_Init(g_pd3dDevice);

		{
			ImFontConfig font_cfg;
			font_cfg.OversampleH = 1;
			font_cfg.OversampleV = 1;
			font_cfg.FontDataOwnedByAtlas = false;

			HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(198), L"TTF");
			HGLOBAL hMem = LoadResource(NULL, hRes);
			void* pLock = LockResource(hMem);
			DWORD dwSize = SizeofResource(NULL, hRes);

			ImFontMain = io.Fonts->AddFontFromMemoryTTF(pLock, dwSize, 30.0f * settingGlobalScale, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
			// 字体自身偏小，假设是 28px 字高
		}
		{
			ImWchar icons_ranges[] =
			{
				0xe713, 0xe713, // 设置
				0xe8bb, 0xe8bb, // 关闭
				0xe72b, 0xe72b, // 返回

				0xf167,0xf167, // INFO

				0xe80f, 0xe80f, // 主页
				0xe7b8, 0xe7b8, // 常规
				0xee56, 0xee56, // 绘制
				0xe74c, 0xe74c, // 插件
				0xe765, 0xe765, // 快捷键
				0xe946, 0xe946, // 软件版本
				0xe716, 0xe716, // 社区名片
				0xe789, 0xe789, // 赞助我们

				0xe711,0xe711, // 关闭程序
				0xe72c,0xe72c, // 重启程序

				0
			};

			ImFontConfig font_cfg;
			font_cfg.OversampleH = 1;
			font_cfg.OversampleV = 1;
			font_cfg.FontDataOwnedByAtlas = false;
			font_cfg.MergeMode = true;
			font_cfg.GlyphOffset.y = 10.0f * settingGlobalScale;
			font_cfg.PixelSnapH = true;

			HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(257), L"TTF");
			HGLOBAL hMem = LoadResource(NULL, hRes);
			void* pLock = LockResource(hMem);
			DWORD dwSize = SizeofResource(NULL, hRes);

			ImFontMain = io.Fonts->AddFontFromMemoryTTF(pLock, dwSize, 36.0f * settingGlobalScale, &font_cfg, icons_ranges);
		}
		{
			ImWchar icons_ranges[] =
			{
				0xf900, 0xf907,
				0
			};

			ImFontConfig font_cfg;
			font_cfg.OversampleH = 1;
			font_cfg.OversampleV = 1;
			font_cfg.FontDataOwnedByAtlas = false;
			font_cfg.MergeMode = true;
			font_cfg.GlyphOffset.y = 4.0f * settingGlobalScale;
			font_cfg.PixelSnapH = true;

			HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(262), L"TTF");
			HGLOBAL hMem = LoadResource(NULL, hRes);
			void* pLock = LockResource(hMem);
			DWORD dwSize = SizeofResource(NULL, hRes);

			ImFontMain = io.Fonts->AddFontFromMemoryTTF(pLock, dwSize, 32.0f * settingGlobalScale, &font_cfg, icons_ranges);
		}

		io.Fonts->Build();

		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style.Colors[ImGuiCol_WindowBg] = ImVec4(243 / 255.0f, 243 / 255.0f, 243 / 255.0f, 1.0f);
			style.Colors[ImGuiCol_ChildBg] = ImVec4(251 / 255.0f, 251 / 255.0f, 251 / 255.0f, 1.0f);
			style.Colors[ImGuiCol_TitleBgActive] = ImVec4(243 / 255.0f, 243 / 255.0f, 243 / 255.0f, 1.0f);
			style.Colors[ImGuiCol_Border] = ImVec4(229 / 255.0f, 229 / 255.0f, 229 / 255.0f, 1.0f);

			style.ItemSpacing.y = 0;

			style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
			style.WindowPadding = ImVec2(0.0f, 0.0f);
			style.FramePadding = ImVec2(0.0f, 0.0f);

			style.FrameBorderSize = 1.0f * settingGlobalScale;
			style.ChildBorderSize = 1.0f * settingGlobalScale;

			style.ChildRounding = 4.0f * settingGlobalScale;
			style.FrameRounding = 4.0f * settingGlobalScale;
			style.PopupRounding = 4.0f * settingGlobalScale;

			style.GrabRounding = 4.0f * settingGlobalScale;
		}

		//初始化定义变量
		hiex::tDelayFPS recond;

		ImGuiToggleConfig config;
		config.Size = { 40.0f * settingGlobalScale,20.0f * settingGlobalScale };
		config.Flags = ImGuiToggleFlags_Animated | ImGuiToggleFlags_ShadowedFrame;

		int QuestNumbers = 0;
		int PushStyleColorNum = 0, PushFontNum = 0, PushStyleVarNum = 0;
		int QueryWaitingTime = 5;

		// 设置参数

		int SelectLanguage = setlist.selectLanguage;
		bool StartUp = setlist.startUp;

		int SetSkinMode = setlist.SetSkinMode;
		bool CreateLnk = setlist.CreateLnk;
		bool RightClickClose = setlist.RightClickClose;
		bool BrushRecover = setlist.BrushRecover, RubberRecover = setlist.RubberRecover;
		bool CompatibleTaskBarAutoHide = setlist.compatibleTaskBarAutoHide;
		bool ForceTop = setlist.forceTop;
		int PaintDevice = setlist.paintDevice;
		string UpdateChannel = setlist.UpdateChannel;

		bool LiftStraighten = setlist.liftStraighten, WaitStraighten = setlist.waitStraighten;
		bool PointAdsorption = setlist.pointAdsorption;
		bool SmoothWriting = setlist.smoothWriting;
		bool SmartEraser = setlist.smartEraser;

		// 插件参数

		bool DdbEnable = ddbInteractionSetList.DdbEnable;
		bool DdbEnhance = ddbInteractionSetList.DdbEnhance;

		float PptUiWidgetScale = 1.0f, PptUiWidgetScaleRecord = 1.0f;
		float BottomSideBothWidgetScale = pptComSetlist.bottomSideBothWidgetScale, BottomSideBothWidgetScaleRecord = pptComSetlist.bottomSideBothWidgetScale;
		float MiddleSideBothWidgetScale = pptComSetlist.middleSideBothWidgetScale, MiddleSideBothWidgetScaleRecord = pptComSetlist.middleSideBothWidgetScale;
		float BottomSideMiddleWidgetScale = pptComSetlist.bottomSideMiddleWidgetScale, BottomSideMiddleWidgetScaleRecord = pptComSetlist.bottomSideMiddleWidgetScale;

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

		int settingTab = 0;
		int settingPlugInTab = 0;

		while (!offSignal)
		{
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
				enum settingTabEnum
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
				enum settingPlugInTabEnum
				{
					tabPlug1,
					tabPlug2,
					tabPlug3,
					tabPlug4
				};

				ImGui::SetNextWindowPos({ 0,0 });//设置窗口位置
				ImGui::SetNextWindowSize({ static_cast<float>(SettingWindowWidth),static_cast<float>(SettingWindowHeight) });//设置窗口大小

				string settingTitle = get<string>(i18n[i18nEnum::Settings]);
				{
#if !__has_include("IdtInsider.h")
					if (i18nIdentifying == L"zh-CN") settingTitle += "（非官方构建版本）";
					else if (i18nIdentifying == L"zh-TW") settingTitle += "（非官方構建版本）";
					else settingTitle += " (Unofficial build version)";
#endif
				}
				ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(189, 189, 189, 255));
				ImGui::Begin("主窗口", &test.select, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);//开始绘制窗口
				ImGui::PopStyleColor();

				// 标题栏高 32px
				{
					ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
					ImGui::SetCursorPos({ 20.0f * settingGlobalScale,14.0f * settingGlobalScale });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
					ImGui::TextUnformatted(("\ue713   " + settingTitle).c_str());

					ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
					ImGui::SetCursorPos({ 904 * settingGlobalScale,0.0f * settingGlobalScale });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(196, 43, 28, 255));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(200, 60, 49, 255));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 0));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
					PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
					if (ImGui::Button("\ue8bb", { 46.0f * settingGlobalScale,32.0f * settingGlobalScale }))
						test.select = false;

					if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
					if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
					while (PushFontNum) PushFontNum--, ImGui::PopFont();
				}

				{
					ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

					// 主页
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,42.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab1)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button(("   \ue80f   " + get<string>(i18n[i18nEnum::Settings_Home])).c_str(), { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab1;
					}

					// 常规
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,ImGui::GetCursorPosY() + 4.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab2)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button(("   \ue7b8   " + get<string>(i18n[i18nEnum::Settings_Regular])).c_str(), { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab2;
					}

					// 绘制
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,ImGui::GetCursorPosY() + 4.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab3)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \uee56   绘制", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab3;
					}

					// 插件
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,ImGui::GetCursorPosY() + 4.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab4)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue74c   插件", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale }))
						{
							if (settingTab != settingTabEnum::tab4) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							settingTab = settingTabEnum::tab4;
						}
					}

					// 快捷键
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,ImGui::GetCursorPosY() + 4.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab5)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue765   快捷键", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab5;
					}

					// 软件版本
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,ImGui::GetCursorPosY() + 4.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab6)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue946   软件版本", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab6;
					}

					// --------------------

					{
						ImGui::SetCursorPosY(493 * settingGlobalScale);

						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 p1 = ImVec2(35, ImGui::GetCursorPosY() - 1.0f * settingGlobalScale);
						ImVec2 p2 = ImVec2(135, ImGui::GetCursorPosY());
						ImU32 color = IM_COL32(229, 229, 229, 255);

						draw_list->AddRectFilled(p1, p2, color, 2.0f);
					}

					// 社区名片
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,496.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab8)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue716   社区名片", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })); //settingTab = settingTabEnum::tab7;
					}

					// 赞助我们
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,536.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab8)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue789   赞助我们", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })); //settingTab = settingTabEnum::tab8;
					}

					// --------------------

					{
						ImGui::SetCursorPosY(577.0f * settingGlobalScale);

						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 p1 = ImVec2(35, ImGui::GetCursorPosY() - 1.0f * settingGlobalScale);
						ImVec2 p2 = ImVec2(135, ImGui::GetCursorPosY());
						ImU32 color = IM_COL32(229, 229, 229, 255);

						draw_list->AddRectFilled(p1, p2, color, 2.0f);
					}

					// 重启程序
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,580.0f * settingGlobalScale });

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue72c   重启程序", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale }))
						{
							test.select = false;
							offSignal = 2;
						}
					}

					// 关闭程序
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,620.0f * settingGlobalScale });

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 43, 28, 255));

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue711   关闭程序", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale }))
						{
							test.select = false;
							offSignal = true;
						}
					}

					// 程序调测
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,660.0f * settingGlobalScale });
						ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

						if (settingTab == settingTabEnum::tab9)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
						if (ImGui::Button("程序调测", { 150.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingTab = settingTabEnum::tab9;
					}

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
				}

				ImGui::SetCursorPos({ 170.0f * settingGlobalScale,42.0f * settingGlobalScale });

				// 主版块 765<-750(770 主页) * 608
				switch (settingTab)
				{
					// 主页
				case settingTabEnum::tab1:
				{
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(236, 241, 255, 255));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(175, 197, 255, 255));

						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::BeginChild("主页0", { 770.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						string temp = get<string>(i18n[i18nEnum::Settings_Home_Prompt]).c_str();
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(13, 83, 255, 255));

						float text_width = ImGui::CalcTextSize(temp.c_str()).x;
						float text_indentation = (770 * settingGlobalScale - text_width) * 0.5f;
						if (text_indentation < 0)  text_indentation = 0;
						ImGui::SetCursorPos({ text_indentation,8.0f * settingGlobalScale });
						ImGui::TextUnformatted(temp.c_str());

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale,ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });
						float Cx = ImGui::GetCursorPosX(), Cy = ImGui::GetCursorPosY();

						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 p_min = ImVec2(Cx - 4.0f * settingGlobalScale, Cy - 4.0f * settingGlobalScale);
						ImVec2 p_max = ImVec2(Cx + 770 * settingGlobalScale + 4.0f * settingGlobalScale, Cy + 575 * settingGlobalScale + 4.0f * settingGlobalScale);
						ImGui::PushClipRect(p_min, p_max, true);

						{
							// 计算图像中心点
							float Mx = SettingWindowX + Cx + 770.0f / 2.0f;
							float My = SettingWindowY + Cy + 575.0f / 2.0f;

							// 获取鼠标坐标
							POINT Pt;
							GetCursorPos(&Pt);

							float Hx = Pt.x - Mx;
							float Hy = Pt.y - My;

							if (Hx < 0) Hx = min(1000, -Hx);
							else Hx = min(1000, Hx);
							if (Hy < 0) Hy = min(1000, -Hy);
							else Hy = min(1000, Hy);

							// 计算横向位移
							float Sx = (Hx * (-0.5 / (1000.0f * settingGlobalScale)) + 1) * Hx * 0.2;
							float Sy = (Hy * (-0.5 / (1000.0f * settingGlobalScale)) + 1) * Hy * 0.2;

							ImGui::SetCursorPosX(Cx + Sx * (Pt.x >= Mx ? -1 : 1) - 100.0f * settingGlobalScale);
							ImGui::SetCursorPosY(Cy + Sy * (Pt.y >= My ? -1 : 1) - 100.0f * settingGlobalScale);
							ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[4], ImVec2((float)SettingSign[4].getwidth(), (float)SettingSign[4].getheight()));
						}
						{
							ImGui::SetCursorPos({ Cx,Cy });
							ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[2], ImVec2((float)SettingSign[2].getwidth(), (float)SettingSign[2].getheight()));
						}

						ImU32 color = IM_COL32(245, 248, 255, 255);
						float rounding = 12.0f * settingGlobalScale;
						float thickness = 9.0f * settingGlobalScale;

						draw_list->AddRect(p_min, p_max, color, rounding, ImDrawFlags_None, thickness);

						ImGui::PopClipRect();

						{
							ImGui::SetCursorPos({ Cx + 20.0f * settingGlobalScale,Cy + 20.0f * settingGlobalScale });

							{
								ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(236, 241, 255, 0));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(236, 241, 255, 30));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(236, 241, 255, 60));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
							}
							if (ImGui::Button("\uf900", { 50.0f * settingGlobalScale,50.0f * settingGlobalScale }))
							{
								ShellExecuteW(0, 0, L"https://www.inkeys.top", 0, 0, SW_SHOW);
							}
						}
						{
							ImGui::SetCursorPos({ Cx + 80.0f * settingGlobalScale,Cy + 20.0f * settingGlobalScale });

							{
								ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(236, 241, 255, 0));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(236, 241, 255, 30));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(236, 241, 255, 60));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
							}
							if (ImGui::Button("\uf901", { 50.0f * settingGlobalScale,50.0f * settingGlobalScale }))
							{
								ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/Inkeys", 0, 0, SW_SHOW);
							}
						}

						{
							ImGui::SetCursorPos({ Cx + 100.0f * settingGlobalScale,Cy + 390.0f * settingGlobalScale });
							ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[3], ImVec2((float)SettingSign[3].getwidth(), (float)SettingSign[3].getheight()));

							ImGui::SetCursorPos({ Cx + 160.0f * settingGlobalScale,Cy + 390.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("AlanCRL");
							}
							ImGui::SetCursorPos({ Cx + 160.0f * settingGlobalScale,Cy + 418.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 0.7f));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Home_1]).c_str());
							}

							{
								ImGui::SetCursorPos({ Cx + 106.0f * settingGlobalScale,Cy + 460.0f * settingGlobalScale });
								ImFontMain->Scale = 0.9f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf902");

								ImGui::SetCursorPos({ Cx + 160.0f * settingGlobalScale,Cy + 465.0f * settingGlobalScale });
								ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("2685549821");
							}

							{
								ImGui::SetCursorPos({ Cx + 106.0f * settingGlobalScale,Cy + 512.0f * settingGlobalScale });
								ImFontMain->Scale = 0.9f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf903");

								ImGui::SetCursorPos({ Cx + 160.0f * settingGlobalScale,Cy + 515.0f * settingGlobalScale });
								ImFontMain->Scale = 0.75f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("alan-crl@foxmail.com");
							}
						}
						{
							{
								ImGui::SetCursorPos({ Cx + 468.0f * settingGlobalScale,Cy + 400.0f * settingGlobalScale });
								ImFontMain->Scale = 0.95f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf904");

								ImGui::SetCursorPos({ Cx + 520.0f * settingGlobalScale,Cy + 393.0f * settingGlobalScale });
								ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Home_2]).c_str());

								ImGui::SetCursorPos({ Cx + 520.0f * settingGlobalScale,Cy + 418.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 0.7f));
								if (ImGui::TextLink("618720802"))
								{
									ShellExecuteW(0, 0, L"https://qm.qq.com/cgi-bin/qm/qr?k=9V2l83dc0yP4UYeDF-NkTX0o7_TcYqlh&jump_from=webapi&authKey=LsLLUhb1KSzHYbc8k5nCQDqTtRcRUCEE3j+DdR9IgHaF/7JF7LLpY191hsiYEBz6", 0, 0, SW_SHOW);
								}
							}

							{
								ImGui::SetCursorPos({ Cx + 467.0f * settingGlobalScale,Cy + 460.0f * settingGlobalScale });
								ImFontMain->Scale = 0.95f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf905");

								ImGui::SetCursorPos({ Cx + 520.0f * settingGlobalScale,Cy + 462.0f * settingGlobalScale });
								ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								if (ImGui::TextLink(get<string>(i18n[i18nEnum::Settings_Home_3]).c_str()))
								{
									ShellExecuteW(0, 0, L"https://space.bilibili.com/1330313497", 0, 0, SW_SHOW);
								}
							}

							{
								ImGui::SetCursorPos({ Cx + 467.0f * settingGlobalScale,Cy + 510.0f * settingGlobalScale });
								ImFontMain->Scale = 0.95f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf906");

								ImGui::SetCursorPos({ Cx + 520.0f * settingGlobalScale,Cy + 512.0f * settingGlobalScale });
								ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_TextLink, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								if (ImGui::TextLink("问题/建议反馈"))
								{
									ShellExecuteW(0, 0, L"https://www.wjx.cn/vm/mqNTTRL.aspx#", 0, 0, SW_SHOW);
								}

								ImGui::SameLine(); ImGui::TextUnformatted("  \uf907");
								if (ImGui::IsItemHovered())
								{
									PushFontNum++, ImFontMain->Scale = 0.7f, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(130, 130, 130, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(13, 83, 255, 255));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * settingGlobalScale);
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * settingGlobalScale, 5.0f * settingGlobalScale));

									ImGui::BeginTooltip();
									ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[7], ImVec2((float)SettingSign[7].getwidth(), (float)SettingSign[7].getheight()));
									ImGui::EndTooltip();
								}
							}
						}
						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
					}

					break;
				}

				// 常规
				case settingTabEnum::tab2:
				{
					ImGui::SetCursorPos({ 180.0f * settingGlobalScale,40.0f * settingGlobalScale });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
					ImGui::BeginChild("常规", { 765.0f * settingGlobalScale,608.0f * settingGlobalScale }, false);

					ImGui::SetCursorPosY(20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular]).c_str());
					}

					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_1]).c_str());

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
						ImGui::BeginChild("语言和国际化", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_1_1]).c_str());
							float markY = ImGui::GetCursorPosY();

							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 230.0f) * settingGlobalScale);
							static const char* items[] = { "  English(en-US)", "  简体中文(zh-CN)", "  正體中文(zh-TW)" };
							ImGui::SetNextItemWidth(200 * settingGlobalScale);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 3.75f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 9.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.35f, 0.5f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 10.0f));
							if (ImGui::BeginCombo("##语言", items[SelectLanguage]))
							{
								for (int i = 0; i < IM_ARRAYSIZE(items); i++)
								{
									const bool is_selected = (SelectLanguage == i);
									if (ImGui::Selectable(items[i], is_selected))
									{
										SelectLanguage = i;
									}

									if (is_selected) ImGui::SetItemDefaultFocus();
								}

								ImGui::EndCombo();
							}

							if (setlist.selectLanguage != SelectLanguage)
							{
								setlist.selectLanguage = SelectLanguage;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_1_2]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_2]).c_str());

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
						ImGui::BeginChild("开机自启及启动行为", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_2_1]).c_str());
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##开机自动启动", &StartUp, config);

							if (setlist.startUp != StartUp)
							{
								SetStartupState(StartUp, GetCurrentExePath(), L"$Inkeys");

								setlist.startUp = StartUp;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_2_2]).c_str());
						}

						// TODO
						/*
						{
							ImGui::SetCursorPosY(80.0f);

							ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 启动时创建桌面快捷方式", 4.0f);

							ImFontMain->Scale = 0.7f, PushFontNum++, ImGui::PushFont(ImFontMain);
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
						}*/

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(251, 251, 251, 255));
						ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_3]).c_str());

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::BeginChild("外观样式", { 750.0f * settingGlobalScale,110.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_3_1]).c_str());
							float markY = ImGui::GetCursorPosY();

							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 230.0f) * settingGlobalScale);
							static const char* items[] = { get<string>(i18n[i18nEnum::Settings_Regular_3_3]).c_str(), get<string>(i18n[i18nEnum::Settings_Regular_3_4]).c_str(), get<string>(i18n[i18nEnum::Settings_Regular_3_5]).c_str(), get<string>(i18n[i18nEnum::Settings_Regular_3_6]).c_str() };
							ImGui::SetNextItemWidth(200 * settingGlobalScale);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 3.75f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 9.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 10.0f));
							if (ImGui::BeginCombo("##外观皮肤", items[SetSkinMode]))
							{
								for (int i = 0; i < IM_ARRAYSIZE(items); i++)
								{
									const bool is_selected = (SetSkinMode == i);
									if (ImGui::Selectable(items[i], is_selected))
									{
										SetSkinMode = i;
									}

									if (is_selected) ImGui::SetItemDefaultFocus();
								}

								ImGui::EndCombo();
							}

							if (setlist.SetSkinMode != SetSkinMode)
							{
								setlist.SetSkinMode = SetSkinMode;
								WriteSetting();

								if (SetSkinMode == 0) setlist.SkinMode = 1;
								else setlist.SkinMode = SetSkinMode;
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 15.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_3_2]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_4]).c_str());

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
						ImGui::BeginChild("其他行为", { 750.0f * settingGlobalScale,190.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_4_1]).c_str());
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##右键点击主图标关闭程序", &RightClickClose, config);

							if (setlist.RightClickClose != RightClickClose)
							{
								setlist.RightClickClose = RightClickClose;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_4_2]).c_str());
						}
						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 20.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_4_3]).c_str());
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##画笔绘制时收起主栏", &BrushRecover, config);

							if (setlist.BrushRecover != BrushRecover)
							{
								setlist.BrushRecover = BrushRecover;
								WriteSetting();
							}
						}
						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 20.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_4_4]).c_str());
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##橡皮擦除时收起主栏", &RubberRecover, config);

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
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_5]).c_str());

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
						ImGui::BeginChild("实验选项", { 750.0f * settingGlobalScale,170.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_5_1]).c_str());
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##兼容“自动隐藏任务栏”", &CompatibleTaskBarAutoHide, config);

							if (setlist.compatibleTaskBarAutoHide != CompatibleTaskBarAutoHide)
							{
								setlist.compatibleTaskBarAutoHide = CompatibleTaskBarAutoHide;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_5_2]).c_str());
						}
						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_5_3]).c_str());
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##允许尝试窗口强制置顶", &ForceTop, config);

							if (setlist.forceTop != ForceTop)
							{
								setlist.forceTop = ForceTop;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::Settings_Regular_5_4]).c_str());
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
				case settingTabEnum::tab3:
				{
					ImGui::SetCursorPos({ 180.0f * settingGlobalScale,40.0f * settingGlobalScale });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
					ImGui::BeginChild("绘制", { 765.0f * settingGlobalScale,608.0f * settingGlobalScale }, false);

					ImGui::SetCursorPosY(20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						ImGui::TextUnformatted("绘制");
					}

					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted("绘图效果优化");

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::BeginChild("绘图设备", { 750.0f * settingGlobalScale,110.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted("绘图设备");
							float markY = ImGui::GetCursorPosY();

							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 230.0f) * settingGlobalScale);
							static const char* items[] = { "  触控屏幕", "  鼠标或手写笔" };
							ImGui::SetNextItemWidth(200 * settingGlobalScale);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 3.75f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 9.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 10.0f));
							if (ImGui::BeginCombo("##绘图设备", items[PaintDevice]))
							{
								for (int i = 0; i < IM_ARRAYSIZE(items); i++)
								{
									const bool is_selected = (PaintDevice == i);
									if (ImGui::Selectable(items[i], is_selected))
									{
										PaintDevice = i;
									}

									if (is_selected) ImGui::SetItemDefaultFocus();
								}

								ImGui::EndCombo();
							}

							if (setlist.paintDevice != PaintDevice)
							{
								setlist.paintDevice = PaintDevice;
								WriteSetting();

								drawingScale = GetDrawingScale();
								stopTimingError = GetStopTimingError();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 15.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted("我们将根据不同设备类型优化使用体验。");
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted("智能绘图");

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::BeginChild("智能绘图", { 750.0f * settingGlobalScale,250.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted("抬笔拉直直线");
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##抬笔拉直直线", &LiftStraighten, config);

							if (setlist.liftStraighten != LiftStraighten)
							{
								setlist.liftStraighten = LiftStraighten;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted("现阶段只推荐在教学一体机上使用。");
						}
						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted("停留拉直直线");
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##停留拉直直线", &WaitStraighten, config);

							if (setlist.waitStraighten != WaitStraighten)
							{
								setlist.waitStraighten = WaitStraighten;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted("绘制直线完成后按住一秒，直线将被拉直。");
						}
						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted("端点吸附");
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##端点吸附", &PointAdsorption, config);

							if (setlist.pointAdsorption != PointAdsorption)
							{
								setlist.pointAdsorption = PointAdsorption;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted("直线和矩形的端点将会在抬笔时吸附。");
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted("绘制行为");

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::BeginChild("绘制行为", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted("抬笔平滑笔迹");
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##抬笔平滑笔迹", &SmoothWriting, config);

							if (setlist.smoothWriting != SmoothWriting)
							{
								setlist.smoothWriting = SmoothWriting;
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
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
						ImGui::TextUnformatted("擦除行为");

						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::BeginChild("擦除行为", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, ImGui::GetCursorPosY() + 25.0f * settingGlobalScale });

							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							ImGui::TextUnformatted("智能粗细橡皮擦");
							float markY = ImGui::GetCursorPosY();

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX((750.0f - 65.0f) * settingGlobalScale);
							ImGui::Toggle("##智能粗细橡皮擦", &SmartEraser, config);

							if (setlist.smartEraser != SmartEraser)
							{
								setlist.smartEraser = SmartEraser;
								WriteSetting();
							}

							ImGui::SetCursorPos({ 30.0f * settingGlobalScale, markY + 5.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
							ImGui::TextUnformatted("根据擦除速度智能调整橡皮粗细。");
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
				case settingTabEnum::tab4:
				{
					switch (settingPlugInTab)
					{
					case settingPlugInTabEnum::tabPlug1:
					{
						ImGui::SetCursorPos({ 180.0f * settingGlobalScale,42.0f * settingGlobalScale });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
						ImGui::BeginChild("插件", { (750.0f + 15.0f) * settingGlobalScale,608.0f * settingGlobalScale }, false);

						ImGui::SetCursorPosY(10.0f * settingGlobalScale);
						{
							ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("插件");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("PPT演示助手", { 750.0f * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((void*)TextureSettingSign[5], ImVec2((float)SettingSign[5].getwidth(), (float)SettingSign[5].getheight()));
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("PPT演示助手");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									{
										if (pptComVersion.substr(0, 7) == L"Error: ") ImGui::TextUnformatted("版本号未知（插件发生错误）");
										else ImGui::TextUnformatted(utf16ToUtf8(pptComVersion).c_str());
									}
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug2;
								}

								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("PPT演示助手-介绍", { 710.0f * settingGlobalScale,35.0f * settingGlobalScale });

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

										ImGui::TextWrapped("在幻灯片演示时提供演示控制按钮和画笔控制按钮。每页拥有独立画板，可以让笔迹固定在页面上。不影响原有功能和外接设备的使用，支持 Microsoft PowerPoint 和WPS。");
										//ImGui::TextWrapped("Provides presentation control buttons and pen control buttons during slideshow presentations. Each slide has an independent drawing board, allowing ink strokes to be fixed on the page. Does not affect existing functions and the use of external devices, supports both Microsoft PowerPoint and WPS.");
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("快捷方式保障助手", { 750.0f * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((void*)TextureSettingSign[6], ImVec2((float)SettingSign[6].getwidth(), (float)SettingSign[6].getheight()));
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("快捷方式保障助手");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("20231008a");
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })); // settingPlugInTab = settingPlugInTabEnum::tabPlug3;
								}

								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("快捷方式保障助手-介绍", { 710.0f * settingGlobalScale,35.0f * settingGlobalScale });

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

										ImGui::TextWrapped("在程序启动时确保已经创建的智绘教Inkeys快捷方式是有效的。因为程序更新会修改文件名称，这会导致快捷方式失效。可选的，如果桌面没有智绘教Inkeys的快捷方式，则创建一个。");
										//ImGui::TextWrapped("Ensure that the created shortcut for 智绘教Inkeys is valid at program startup. Because program updates modify file names, this can cause shortcuts to become invalid. Optionally, if there is no shortcut for 智绘教Inkeys on the desktop, create one.");
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("同类软件悬浮窗拦截助手", { 750.0f * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((void*)TextureSettingSign[8], ImVec2((float)SettingSign[8].getwidth(), (float)SettingSign[8].getheight()));
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("同类软件悬浮窗拦截助手");
								}
								{
									ImGui::SetCursorPos({ 60.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted(utf16ToUtf8(ddbInteractionSetList.DdbEdition).c_str());
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug4;
								}

								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("同类软件悬浮窗拦截助手-介绍", { 710.0f * settingGlobalScale,35.0f * settingGlobalScale });

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

										ImGui::TextWrapped("拦截屏幕上 希沃白板桌面悬浮窗 等同类软件悬浮窗。支持拦截常见同类软件悬浮窗，以及 PPT 小工具等 PPT 操控栏。");
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

					case settingPlugInTabEnum::tabPlug2:
					{
						ImGui::SetCursorPos({ 180.0f * settingGlobalScale,42.0f * settingGlobalScale });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
						ImGui::BeginChild("PPT演示助手", { 765.0f * settingGlobalScale,608.0f * settingGlobalScale }, false);

						{
							ImGui::SetCursorPos({ 0,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								if (ImGui::Button("\ue72b", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							}

							ImGui::SetCursorPos({ 40.0f * settingGlobalScale ,10.0f * settingGlobalScale });
							{
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("PPT演示助手");
								}
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									{
										if (pptComVersion.substr(0, 7) == L"Error: ") ImGui::TextUnformatted("版本号未知（插件发生错误）");
										else ImGui::TextUnformatted(utf16ToUtf8(pptComVersion).c_str());
									}
								}
							}
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("PPT演示助手#1", { 750.0f * settingGlobalScale,80.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 95, 183, 255));
								ImGui::TextUnformatted("\uf167");
							}
							{
								ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("PPT演示助手-提示", { 560.0f * settingGlobalScale,40.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextWrapped("使用插件时需要保证智绘教Inkeys的程序权限和PPT程序是一致的才能正确识别。如果依然存在问题，可以参考解决方案。");
									//ImGui::TextWrapped("When using the plugin, you need to make sure that the program permissions of 智绘教Inkeys and the slideshow program are the same in order to recognize it correctly. If you still have problems, you can refer to the solution.");
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								if (ImGui::Button("解决方案", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									ShellExecuteW(0, 0, L"https://blog.csdn.net/alan16356/article/details/143618256?fromshare=blogdetail&sharetype=blogdetail&sharerId=143618256&sharerefer=PC&sharesource=alan16356&sharefrom=from_link", 0, 0, SW_SHOW);
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("PPT演示助手#2", { 750.0f * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("基础逻辑");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("墨迹固定在对应页面上", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("墨迹固定在对应页面上");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!PptComFixedHandWriting)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##墨迹固定在对应页面上", &PptComFixedHandWriting, config);

									if (pptComSetlist.fixedHandWriting != PptComFixedHandWriting)
									{
										pptComSetlist.fixedHandWriting = PptComFixedHandWriting;
										PptComWriteSetting();
									}
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("墨迹固定在对应页面上-介绍", { 710.0f * settingGlobalScale,30.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

										ImGui::TextWrapped("每个PPT页面都将拥有各自独立的画布，并可以实现类似PPT自带画笔的效果。翻页不会清空之前页面所绘制的墨迹，可以返回之前的页面继续绘制。");
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
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("PPT演示助手#3", { 750.0f * settingGlobalScale,210.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("控件显示");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("控件显示", { 750.0f * settingGlobalScale,180.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("显示底部两侧控件");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!ShowBottomBoth)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##显示底部两侧控件", &ShowBottomBoth, config);

									if (pptComSetlist.showBottomBoth != ShowBottomBoth)
									{
										pptComSetlist.showBottomBoth = ShowBottomBoth;
										PptComWriteSetting();
									}
								}

								// Separator
								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPosY(cursosPosY + 20.0f * settingGlobalScale);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(229, 229, 229, 255));
									ImGui::Separator();
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("显示中部两侧控件");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!ShowMiddleBoth)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##显示中部两侧控件", &ShowMiddleBoth, config);

									if (pptComSetlist.showMiddleBoth != ShowMiddleBoth)
									{
										pptComSetlist.showMiddleBoth = ShowMiddleBoth;
										PptComWriteSetting();
									}
								}

								// Separator
								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPosY(cursosPosY + 20.0f * settingGlobalScale);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(229, 229, 229, 255));
									ImGui::Separator();
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("显示底部主栏控件");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!ShowBottomMiddle)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
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
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("PPT演示助手#4", { 750.0f * settingGlobalScale,155.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("控件位置");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("重置控件位置", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("重置控件位置");
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("重置", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
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
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("记忆控件位置", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("记忆控件位置");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!MemoryWidgetPosition)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
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
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("PPT演示助手#5", { 750.0f * settingGlobalScale,215.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("控件缩放");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("翻页控件缩放", { 750.0f * settingGlobalScale,120.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("底部两侧控件缩放");
								}
								{
									ImGui::SetCursorPos({ 325.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImGui::PushItemWidth(300.0f);

									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 95, 184, 230));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f * settingGlobalScale));
									ImGui::SliderFloat("##底部两侧控件缩放", &BottomSideBothWidgetScale, 0.5f, 3.0f, "");
									BottomSideBothWidgetScale = round(BottomSideBothWidgetScale * 100) / 100;

									ImGui::PopItemWidth();

									bool isItemHovered = ImGui::IsItemHovered();
									bool isItemActive = ImGui::IsItemActive();

									if (isItemHovered)
									{
										PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

										ImGui::BeginTooltip();

										ImGui::TextUnformatted(format("{:.2f} 倍缩放", BottomSideBothWidgetScale).c_str());

										ImGui::EndTooltip();
									}
									if (!isItemActive && BottomSideBothWidgetScale != BottomSideBothWidgetScaleRecord)
									{
										BottomSideBothWidgetScaleRecord = BottomSideBothWidgetScale;
										pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
										PptComWriteSetting();
									}
									if (BottomSideBothWidgetScale != BottomSideBothWidgetScaleRecord)
									{
										pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale;
									}
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("重置##1", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
									{
										pptComSetlist.bottomSideBothWidgetScale = BottomSideBothWidgetScale = 1.0f;
										PptComWriteSetting();
									}
								}

								// Separator
								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPosY(cursosPosY + 15.0f * settingGlobalScale);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(229, 229, 229, 255));
									ImGui::Separator();
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("中部两侧控件缩放");
								}
								{
									ImGui::SetCursorPos({ 325.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImGui::PushItemWidth(300.0f);

									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 95, 184, 230));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f * settingGlobalScale));
									ImGui::SliderFloat("##中部两侧控件缩放", &MiddleSideBothWidgetScale, 0.5f, 3.0f, "");
									MiddleSideBothWidgetScale = round(MiddleSideBothWidgetScale * 100) / 100;

									ImGui::PopItemWidth();

									bool isItemHovered = ImGui::IsItemHovered();
									bool isItemActive = ImGui::IsItemActive();

									if (isItemHovered)
									{
										PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

										ImGui::BeginTooltip();

										ImGui::TextUnformatted(format("{:.2f} 倍缩放", MiddleSideBothWidgetScale).c_str());

										ImGui::EndTooltip();
									}
									if (!isItemActive && MiddleSideBothWidgetScale != MiddleSideBothWidgetScaleRecord)
									{
										MiddleSideBothWidgetScaleRecord = MiddleSideBothWidgetScale;
										pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
										PptComWriteSetting();
									}
									if (MiddleSideBothWidgetScale != MiddleSideBothWidgetScaleRecord)
									{
										pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale;
									}
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("重置##2", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
									{
										pptComSetlist.middleSideBothWidgetScale = MiddleSideBothWidgetScale = 1.0f;
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
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("状态控件缩放", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("底部主栏控件缩放");
								}
								{
									ImGui::SetCursorPos({ 325.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImGui::PushItemWidth(300.0f);

									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 95, 184, 230));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f * settingGlobalScale));
									ImGui::SliderFloat("##底部主栏控件缩放", &BottomSideMiddleWidgetScale, 0.5f, 3.0f, "");
									BottomSideMiddleWidgetScale = round(BottomSideMiddleWidgetScale * 100) / 100;

									ImGui::PopItemWidth();

									bool isItemHovered = ImGui::IsItemHovered();
									bool isItemActive = ImGui::IsItemActive();

									if (ImGui::IsItemHovered())
									{
										PushFontNum++, ImFontMain->Scale = 0.5f, ImGui::PushFont(ImFontMain);

										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * settingGlobalScale);
										PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * settingGlobalScale, 8.0f * settingGlobalScale));

										ImGui::BeginTooltip();

										ImGui::TextUnformatted(format("{:.2f} 倍缩放", BottomSideMiddleWidgetScale).c_str());

										ImGui::EndTooltip();
									}
									if (!isItemActive && BottomSideMiddleWidgetScale != BottomSideMiddleWidgetScaleRecord)
									{
										BottomSideMiddleWidgetScaleRecord = BottomSideMiddleWidgetScale;
										pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale;
										PptComWriteSetting();
									}
									if (BottomSideMiddleWidgetScale != BottomSideMiddleWidgetScaleRecord)
									{
										pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale;
									}
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("重置##1", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
									{
										pptComSetlist.bottomSideMiddleWidgetScale = BottomSideMiddleWidgetScale = 1.0f;
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
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}

					case settingPlugInTabEnum::tabPlug4:
					{
						ImGui::SetCursorPos({ 180.0f * settingGlobalScale,42.0f * settingGlobalScale });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
						ImGui::BeginChild("同类软件悬浮窗拦截助手", { 765.0f * settingGlobalScale,608.0f * settingGlobalScale }, false);

						{
							ImGui::SetCursorPos({ 0,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								if (ImGui::Button("\ue72b", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug1;
							}

							ImGui::SetCursorPos({ 40.0f * settingGlobalScale ,10.0f * settingGlobalScale });
							{
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("同类软件悬浮窗拦截助手");
								}
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted(utf16ToUtf8(ddbInteractionSetList.DdbEdition).c_str());
								}
							}

							ImGui::SetCursorPos({ 720.0f * settingGlobalScale,10.0f * settingGlobalScale });
							{
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								if (ImGui::Button("\uf901", { 30.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									ShellExecuteW(0, 0, L"https://github.com/Alan-CRL/DesktopDrawpadBlocker", 0, 0, SW_SHOW);
								}
							}
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("同类软件悬浮窗拦截助手#1", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 0.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("启用插件", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("启用插件");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("默认模式下插件随主程序开启和关闭。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!DdbEnable)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##启用插件", &DdbEnable, config);

									if (ddbInteractionSetList.DdbEnable != DdbEnable)
									{
										ddbInteractionSetList.DdbEnable = DdbEnable;
										if (!ddbInteractionSetList.DdbEnable) ddbInteractionSetList.DdbEnhance = DdbEnhance = false;

										WriteSetting();

										if (ddbInteractionSetList.DdbEnable)
										{
											ddbInteractionSetList.hostPath = GetCurrentExePath();
											if (_waccess((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), 0) == -1)
											{
												if (_waccess((dataPath + L"\\DesktopDrawpadBlocker").c_str(), 0) == -1)
												{
													error_code ec;
													filesystem::create_directories(dataPath + L"\\DesktopDrawpadBlocker", ec);
												}
												ExtractResource((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
											}
											else
											{
												string hash_sha256;
												{
													hashwrapper* myWrapper = new sha256wrapper();
													hash_sha256 = myWrapper->getHashFromFileW(dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe");
													delete myWrapper;
												}

												if (hash_sha256 != ddbInteractionSetList.DdbSHA256)
													ExtractResource((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
											}

											// 启动 DDB
											if (!isProcessRunning((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
											{
												DdbWriteInteraction(true, false);
												ShellExecute(NULL, NULL, (dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
											}
										}
										else
										{
											DdbWriteInteraction(true, true);

											// 取消开机自动启动
											SetStartupState(false, dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe", L"$Inkeys_DesktopDrawpadBlocker");

											// 移除开机自启标识
											if (_waccess((dataPath + L"\\DesktopDrawpadBlocker\\start_up.signal").c_str(), 0) == 0)
											{
												error_code ec;
												filesystem::remove(dataPath + L"\\DesktopDrawpadBlocker\\start_up.signal", ec);
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
							}

							{
								if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
								if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
								while (PushFontNum) PushFontNum--, ImGui::PopFont();
							}
							ImGui::EndChild();
						}
						if (ddbInteractionSetList.DdbEnable)
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("同类软件悬浮窗拦截助手#2", { 750.0f * settingGlobalScale,155.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("拓展选项");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("独立增强模式", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("独立增强模式");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!DdbEnhance)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##独立增强模式", &DdbEnhance, config);

									if (ddbInteractionSetList.DdbEnhance != DdbEnhance)
									{
										ddbInteractionSetList.DdbEnhance = DdbEnhance;
										WriteSetting();

										if (ddbInteractionSetList.DdbEnhance)
										{
											ddbInteractionSetList.mode = 0;
											ddbInteractionSetList.restartHost = true;
											DdbWriteInteraction(true, false);

											// 设置开机自动启动
											SetStartupState(true, dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe", L"$Inkeys_DesktopDrawpadBlocker");

											// 创建开机自启标识
											if (_waccess((dataPath + L"\\DesktopDrawpadBlocker\\start_up.signal").c_str(), 0) == -1)
											{
												std::ofstream file((dataPath + L"\\DesktopDrawpadBlocker\\start_up.signal").c_str());
												file.close();
											}
										}
										else
										{
											ddbInteractionSetList.mode = 1;
											ddbInteractionSetList.restartHost = true;
											DdbWriteInteraction(true, false);

											// 取消开机自动启动
											SetStartupState(false, dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe", L"$Inkeys_DesktopDrawpadBlocker");

											// 移除开机自启标识
											if (_waccess((dataPath + L"\\DesktopDrawpadBlocker\\start_up.signal").c_str(), 0) == 0)
											{
												error_code ec;
												filesystem::remove(dataPath + L"\\DesktopDrawpadBlocker\\start_up.signal", ec);
											}
										}
									}
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("独立增强模式-介绍", { 710.0f * settingGlobalScale,30.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

										ImGui::TextWrapped("同类软件悬浮窗拦截助手将开机自动启动，并与智绘教独立运行。同时在智绘教关闭状态下，当插件拦截到其他软件的悬浮窗，则会重启智绘教。");
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
					}
					break;
				}

				// 快捷键
				case settingTabEnum::tab5:
				{
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					ImGui::BeginChild("快捷键", { 765.0f,608.0f }, true);

					ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);
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

				// 程序版本
				case settingTabEnum::tab6:
				{
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					ImGui::BeginChild("关于", { 765.0f,608.0f }, true);

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

					ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);
					{
						ImGui::SetCursorPos({ 20.0f,ImGui::GetCursorPosY() + 30.0f });
						ImGui::BeginChild("更新通道调整", { 730.0f,500.0f }, true, ImGuiWindowFlags_NoScrollbar);

						{
							ImGui::SetCursorPosY(10.0f);

							ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 更新通道", 4.0f);

							ImFontMain->Scale = 0.7f, PushFontNum++, ImGui::PushFont(ImFontMain);
							ImGui::SameLine(); HelpMarker("正式通道(LTS) 提供经过验证的稳定程序版本\n公测通道(Beta) 提供稳定性一般的程序版本\n非正式通道程序均未提交杀软进行防误报处理\n\n一旦更新，则无法通过自动更新回退版本\n当选择的更新通道不可用时，则会切换回默认通道", ImGui::GetStyleColorVec4(ImGuiCol_Text));

							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 180.0f);
							ImGui::SetNextItemWidth(170);

							ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
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

							ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(" 更新日志", 4.0f);

							ImFontMain->Scale = 0.7f, PushFontNum++, ImGui::PushFont(ImFontMain);
							ImGui::SameLine(); CenteredText(("当前版本" + utf16ToUtf8(editionDate)).c_str(), 8.0f);

							ImGui::SetCursorPos({ 20.0f,90.0f });
							ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);
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

				// 社区名片
				case settingTabEnum::tab7:
				{
				}

				// 赞助名片
				case settingTabEnum::tab8:
				{
				}

				// ---------------------

				// 程序调测
				case settingTabEnum::tab9:
				{
					ImGui::BeginChild("程序调测", { 765.0f,608.0f }, true);

					ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);
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

				/*
				{
					if (AutomaticUpdateStep == 0)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip0]).c_str()).x,45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(150 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip0]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 1)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip1]).c_str()).x,45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(150 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip1]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 2)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip2]).c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip2]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 3)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip3]).c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip3]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 4)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip4]).c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip4]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 5)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip5]).c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip5]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 6)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip6]).c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip6]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 7)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip7]).c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip7]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 8)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize(get<string>(i18n[i18nEnum::Settings_Update_Tip8]).c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(229 / 255.0f, 55 / 255.0f, 66 / 255.0f, 1.0f));
						CenteredText(get<string>(i18n[i18nEnum::Settings_Update_Tip8]).c_str(), 4.0f);
					}
					else if (AutomaticUpdateStep == 9)
					{
						ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);

						ImGui::SetCursorPos({ 170.0f + 770.0f - ImGui::CalcTextSize((get<string>(i18n[i18nEnum::Settings_Update_Tip9]) + "(" + setlist.UpdateChannel + ")").c_str()).x , 45.0f + 615.0f + 5.0f });
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(98 / 255.0f, 175 / 255.0f, 82 / 255.0f, 1.0f));
						CenteredText((get<string>(i18n[i18nEnum::Settings_Update_Tip9]) + "(" + setlist.UpdateChannel + ")").c_str(), 4.0f);
					}
				}*/

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
	return;
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
	case WM_MOVE:
		RECT rect;
		GetWindowRect(hWnd, &rect);

		SettingWindowX = rect.left;
		SettingWindowY = rect.top;

		break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 示例
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