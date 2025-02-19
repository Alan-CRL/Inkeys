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

WNDCLASSEXW ImGuiWc;
struct
{
	int width;
	int height;
} settingSign[10];
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
	// 创建窗口
	{
		wstring ClassName;
		if (userId == L"Error") ClassName = L"Inkeys3;HiEasyX041";
		else ClassName = L"Inkeys3;" + userId;

		ImGuiWc = { sizeof(WNDCLASSEX), CS_CLASSDC, ImGuiWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, ClassName.c_str(), nullptr };
		RegisterClassExW(&ImGuiWc);
		setting_window = CreateWindowEx(WS_EX_NOACTIVATE, ImGuiWc.lpszClassName, L"Inkeys3 SettingWindow", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, SettingWindowX, SettingWindowY, SettingWindowWidth, SettingWindowHeight, drawpad_window, nullptr, ImGuiWc.hInstance, nullptr);
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
}
void SettingWindowBegin()
{
	// 尺寸计算
	{
		//settingGlobalScale = min((float)MainMonitor.MonitorWidth / 1920.0f, (float)MainMonitor.MonitorHeight / 1080.0f);
		settingGlobalScale = setlist.settingGlobalScale;

		SettingWindowWidth = 960 * settingGlobalScale;
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

	SetWindowLongW(setting_window, GWL_STYLE, GetWindowLong(setting_window, GWL_STYLE) & ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME));
	SetWindowPos(setting_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	ShowWindow(setting_window, SW_HIDE);
	UpdateWindow(setting_window);
}

void SettingMain()
{
	threadStatus[L"SettingMain"] = true;

	// 初始化部分
	{
		CreateDeviceD3D(setting_window);

		// 图像加载
		{
			IMAGE SettingSign;

			if (i18nIdentifying == L"zh-CN") idtLoadImage(&SettingSign, L"PNG", L"Home1_zh-CN", 700 * settingGlobalScale, 215 * settingGlobalScale, true);
			else idtLoadImage(&SettingSign, L"PNG", L"Home1_en-US", 700 * settingGlobalScale, 240 * settingGlobalScale, true);
			{
				int width = settingSign[1].width = SettingSign.getwidth();
				int height = settingSign[1].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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

			if (i18nIdentifying == L"zh-CN") idtLoadImage(&SettingSign, L"PNG", L"Home2_zh-CN", 770 * settingGlobalScale, 390 * settingGlobalScale, true);
			else idtLoadImage(&SettingSign, L"PNG", L"Home2_en-US", 770 * settingGlobalScale, 390 * settingGlobalScale, true);
			{
				int width = settingSign[2].width = SettingSign.getwidth();
				int height = settingSign[2].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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

			idtLoadImage(&SettingSign, L"PNG", L"PluginFlag1", 30 * settingGlobalScale, 30 * settingGlobalScale, true);
			{
				int width = settingSign[5].width = SettingSign.getwidth();
				int height = settingSign[5].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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
			idtLoadImage(&SettingSign, L"PNG", L"PluginFlag2", 30 * settingGlobalScale, 30 * settingGlobalScale, true);
			{
				int width = settingSign[6].width = SettingSign.getwidth();
				int height = settingSign[6].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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
			idtLoadImage(&SettingSign, L"PNG", L"PluginFlag3", 30 * settingGlobalScale, 30 * settingGlobalScale, true);
			{
				int width = settingSign[8].width = SettingSign.getwidth();
				int height = settingSign[8].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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

			idtLoadImage(&SettingSign, L"PNG", L"Home_Backgroung", 980 * settingGlobalScale, 768 * settingGlobalScale, true);
			{
				int width = settingSign[4].width = SettingSign.getwidth();
				int height = settingSign[4].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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
			idtLoadImage(&SettingSign, L"PNG", L"Profile_Picture", 45 * settingGlobalScale, 45 * settingGlobalScale, true);
			{
				int width = settingSign[3].width = SettingSign.getwidth();
				int height = settingSign[3].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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
			idtLoadImage(&SettingSign, L"PNG", L"Home_Feedback", 100 * settingGlobalScale, 100 * settingGlobalScale, true);
			{
				int width = settingSign[7].width = SettingSign.getwidth();
				int height = settingSign[7].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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

			idtLoadImage(&SettingSign, L"PNG", L"SettingSponsor", 650 * settingGlobalScale, 460 * settingGlobalScale, true);
			{
				int width = settingSign[9].width = SettingSign.getwidth();
				int height = settingSign[9].height = SettingSign.getheight();
				DWORD* pMem = GetImageBuffer(&SettingSign);

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

				bool ret = LoadTextureFromMemory(data, width, height, &TextureSettingSign[9]);
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

			HRSRC hRes;
			if (i18nIdentifying == L"zh-TW") hRes = FindResource(NULL, MAKEINTRESOURCE(258), L"TTF");
			else hRes = FindResource(NULL, MAKEINTRESOURCE(198), L"TTF");
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

				0xf167, 0xf167, // 信息
				0xec61, 0xec61, // 完成
				0xe814, 0xe814, // 警告
				0xeb90, 0xeb90, // 错误

				0xe80f, 0xe80f, // 主页
				0xe7b8, 0xe7b8, // 常规
				0xee56, 0xee56, // 绘制
				0xec4a, 0xec4a, // 性能
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
			style.ScrollbarSize = 20.0f * settingGlobalScale;

			style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
			style.WindowPadding = ImVec2(0.0f, 0.0f);
			style.FramePadding = ImVec2(0.0f, 0.0f);

			style.FrameBorderSize = 1.0f * settingGlobalScale;
			style.ChildBorderSize = 1.0f * settingGlobalScale;

			style.ChildRounding = 4.0f * settingGlobalScale;
			style.FrameRounding = 4.0f * settingGlobalScale;
			style.PopupRounding = 4.0f * settingGlobalScale;

			style.GrabRounding = 4.0f * settingGlobalScale;
			style.GrabMinSize = 12.0f * settingGlobalScale;
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

		bool EnableAutoUpdate = setlist.enableAutoUpdate;

		int SelectLanguage = setlist.selectLanguage;
		bool StartUp = setlist.startUp;
		bool CreateLnk = setlist.shortcutAssistant.createLnk;
		bool CorrectLnk = setlist.shortcutAssistant.correctLnk;

		int SetSkinMode = setlist.SetSkinMode;
		float SettingGlobalScale = settingGlobalScale;

		int TopSleepTime = setlist.topSleepTime;
		bool RightClickClose = setlist.RightClickClose;
		bool BrushRecover = setlist.BrushRecover, RubberRecover = setlist.RubberRecover;
		bool AvoidFullScreen = setlist.avoidFullScreen;
		int PaintDevice = setlist.paintDevice;

		bool LiftStraighten = setlist.liftStraighten, WaitStraighten = setlist.waitStraighten;
		bool PointAdsorption = setlist.pointAdsorption;
		bool SmoothWriting = setlist.smoothWriting;
		int EraserMode = setlist.eraserSetting.eraserMode;

		int PreparationQuantity = setlist.performanceSetting.preparationQuantity;

		// 插件参数

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

		bool AutoKillWpsProcess = pptComSetlist.autoKillWpsProcess;

		struct
		{
			bool Enable = ddbInteractionSetList.enable;
			bool RunAsAdmin = ddbInteractionSetList.runAsAdmin;

			struct
			{
				bool SeewoWhiteboard3Floating = ddbInteractionSetList.intercept.seewoWhiteboard3Floating;
				bool SeewoWhiteboard5Floating = ddbInteractionSetList.intercept.seewoWhiteboard5Floating;
				bool SeewoWhiteboard5CFloating = ddbInteractionSetList.intercept.seewoWhiteboard5CFloating;
				bool SeewoPincoSideBarFloating = ddbInteractionSetList.intercept.seewoPincoSideBarFloating;
				bool SeewoPincoDrawingFloating = ddbInteractionSetList.intercept.seewoPincoDrawingFloating;
				bool SeewoPPTFloating = ddbInteractionSetList.intercept.seewoPPTFloating;
				bool AiClassFloating = ddbInteractionSetList.intercept.aiClassFloating;
				bool HiteAnnotationFloating = ddbInteractionSetList.intercept.hiteAnnotationFloating;
				bool ChangYanFloating = ddbInteractionSetList.intercept.changYanFloating;
				bool ChangYanPptFloating = ddbInteractionSetList.intercept.changYanPptFloating;
				bool IntelligentClassFloating = ddbInteractionSetList.intercept.intelligentClassFloating;
				bool SeewoDesktopAnnotationFloating = ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating;
				bool SeewoDesktopSideBarFloating = ddbInteractionSetList.intercept.seewoDesktopSideBarFloating;
			}intercept;
		} Ddb;

		// ==========

		wstring receivedData;

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
					tabPerformance,
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
					tabPlugDDB
				};

				ImGui::SetNextWindowPos({ 0,0 });//设置窗口位置
				ImGui::SetNextWindowSize({ static_cast<float>(SettingWindowWidth),static_cast<float>(SettingWindowHeight) });//设置窗口大小

				ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(189, 189, 189, 255));
				ImGui::Begin("主窗口", &test.select, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);//开始绘制窗口
				ImGui::PopStyleColor();

				// 标题栏高 32px
				{
					ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
					ImGui::SetCursorPos({ 20.0f * settingGlobalScale,14.0f * settingGlobalScale });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
					ImGui::TextUnformatted(("\ue713   " + get<string>(i18n[i18nEnum::Settings])).c_str());

					ImFontMain->Scale = 0.3f, PushFontNum++, ImGui::PushFont(ImFontMain);
					ImGui::SetCursorPos({ 914 * settingGlobalScale,0.0f * settingGlobalScale });
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

				// 左侧导航栏
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
						if (ImGui::Button(("   \ue80f   " + get<string>(i18n[i18nEnum::SettingsHome])).c_str(), { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab1;
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
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * settingGlobalScale);

						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 p1 = ImVec2(35 * settingGlobalScale, ImGui::GetCursorPosY() - 1.0f * settingGlobalScale);
						ImVec2 p2 = ImVec2(135 * settingGlobalScale, ImGui::GetCursorPosY());
						ImU32 color = IM_COL32(229, 229, 229, 255);

						draw_list->AddRectFilled(p1, p2, color, 2.0f * settingGlobalScale);
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
						if (ImGui::Button(("   \ue7b8   " + get<string>(i18n[i18nEnum::SettingsRegular])).c_str(), { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab2;
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

					// 性能
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,ImGui::GetCursorPosY() + 4.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tabPerformance)
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
						if (ImGui::Button("   \uec4a   性能", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tabPerformance;
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
							settingPlugInTab = settingPlugInTabEnum::tabPlug1;
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

					// --------------------
					{
						ImGui::SetCursorPosY(486 * settingGlobalScale);

						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 p1 = ImVec2(35 * settingGlobalScale, ImGui::GetCursorPosY() - 1.0f * settingGlobalScale);
						ImVec2 p2 = ImVec2(135 * settingGlobalScale, ImGui::GetCursorPosY());
						ImU32 color = IM_COL32(229, 229, 229, 255);

						draw_list->AddRectFilled(p1, p2, color, 2.0f * settingGlobalScale);
					}

					// 社区名片
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,490.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab7)
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
						if (ImGui::Button("   \ue716   社区名片", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab7;
					}

					// 赞助我们
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,530.0f * settingGlobalScale });

						if (settingTab == settingTabEnum::tab8)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 95, 183, 255));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}
						else
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 0));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 10));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 6));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 95, 183, 255));

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
						}

						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
						if (ImGui::Button("   \ue789   赞助我们", { 150.0f * settingGlobalScale,36.0f * settingGlobalScale })) settingTab = settingTabEnum::tab8;
					}

					// --------------------
					{
						ImGui::SetCursorPosY(570.0f * settingGlobalScale);

						ImDrawList* draw_list = ImGui::GetWindowDrawList();

						ImVec2 p1 = ImVec2(35 * settingGlobalScale, ImGui::GetCursorPosY() - 1.0f * settingGlobalScale);
						ImVec2 p2 = ImVec2(135 * settingGlobalScale, ImGui::GetCursorPosY());
						ImU32 color = IM_COL32(229, 229, 229, 255);

						draw_list->AddRectFilled(p1, p2, color, 2.0f * settingGlobalScale);
					}

					// 重启程序
					{
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,574.0f * settingGlobalScale });

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
						ImGui::SetCursorPos({ 10.0f * settingGlobalScale,614.0f * settingGlobalScale });

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

				// 主版块 765<-750(770 主页) * 608
				switch (settingTab)
				{
					// 主页
				case settingTabEnum::tab1:
				{
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 42.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
						ImGui::BeginChild("主页-提示", { 780.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 95, 183, 255));
							ImGui::TextUnformatted("\uf167");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsHomePrompt]).c_str());
						}

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
						ImVec2 p_max = ImVec2(Cx + 780 * settingGlobalScale + 4.0f * settingGlobalScale, Cy + 568 * settingGlobalScale + 4.0f * settingGlobalScale);

						ImVec2 reg_min = ImVec2(Cx, Cy);
						ImVec2 reg_max = ImVec2(Cx + 780 * settingGlobalScale, Cy + 568 * settingGlobalScale);
						ImGui::PushClipRect(reg_min, reg_max, true);

						{
							// 计算图像中心点
							float Mx = SettingWindowX + Cx + 780.0f / 2.0f;
							float My = SettingWindowY + Cy + 568.0f / 2.0f;

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
							ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[4], ImVec2((float)settingSign[4].width, (float)settingSign[4].height));
						}
						{
							ImGui::SetCursorPos({ Cx,Cy });
							ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[2], ImVec2((float)settingSign[2].width, (float)settingSign[2].height));
						}

						ImU32 color = IM_COL32(243, 243, 243, 255);
						float rounding = 8.0f * settingGlobalScale;
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
							ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[3], ImVec2((float)settingSign[3].width, (float)settingSign[3].height));

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
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsHome1]).c_str());
							}

							{
								ImGui::SetCursorPos({ Cx + 106.0f * settingGlobalScale,Cy + 465.0f * settingGlobalScale });
								ImFontMain->Scale = 0.9f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf902");

								ImGui::SetCursorPos({ Cx + 160.0f * settingGlobalScale,Cy + 465.0f * settingGlobalScale });
								ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("2685549821");
							}

							{
								ImGui::SetCursorPos({ Cx + 106.0f * settingGlobalScale,Cy + 515.0f * settingGlobalScale });
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
								ImGui::SetCursorPos({ Cx + 468.0f * settingGlobalScale,Cy + 405.0f * settingGlobalScale });
								ImFontMain->Scale = 0.95f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf904");

								ImGui::SetCursorPos({ Cx + 520.0f * settingGlobalScale,Cy + 393.0f * settingGlobalScale });
								ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsHome2]).c_str());

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
								if (ImGui::TextLink(get<string>(i18n[i18nEnum::SettingsHome3]).c_str()))
								{
									ShellExecuteW(0, 0, L"https://space.bilibili.com/1330313497", 0, 0, SW_SHOW);
								}
							}
							{
								ImGui::SetCursorPos({ Cx + 467.0f * settingGlobalScale,Cy + 515.0f * settingGlobalScale });
								ImFontMain->Scale = 0.95f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
								ImGui::TextUnformatted("\uf906");

								ImGui::SetCursorPos({ Cx + 520.0f * settingGlobalScale,Cy + 515.0f * settingGlobalScale });
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
									ImGui::Image((ImTextureID)(intptr_t)TextureSettingSign[7], ImVec2((float)settingSign[7].width, (float)settingSign[7].height));
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

				// 软件版本
				case settingTabEnum::tab6:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
					ImGui::BeginChild("软件版本", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, false);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
						ImGui::TextUnformatted("软件版本");
					}

					if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNew)
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
						ImGui::BeginChild("软件版本#0", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("发现软件更新版本");
						}
						{
							ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 15.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
							if (ImGui::Button("手动更新", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
							{
								mandatoryUpdate = true;
								AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
							}
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					if (inconsistentArchitecture)
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
						ImGui::BeginChild("软件版本#01", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							ImGui::BeginChild("软件版本-提示2", { 670.0f * settingGlobalScale,60.0f * settingGlobalScale }, false);

							{
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextWrapped("检测到软件架构与系统架构不符，与系统架构不适应有可能会导致软件效率降低，并影响体验。建议在本页面下方“软件修复”模块中修复软件。");
								//ImGui::TextWrapped("The software architecture is detected to be incompatible with the system architecture, which may cause the software to be less efficient and affect the experience. It is recommended to repair the software in the “Software Repair” module at the bottom of this page.");
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
						ImGui::BeginChild("软件版本#1", { 750.0f * settingGlobalScale,410.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("版本信息");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("版本信息", { 750.0f * settingGlobalScale,380.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 35.0f * settingGlobalScale,20.0f * settingGlobalScale });
								ImGui::Image((void*)TextureSettingSign[1], ImVec2((float)settingSign[1].width, (float)settingSign[1].height));
							}
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY());
								wstring text;
								{
									text += L"\n程序发布版本 " + editionDate + L"(" + editionChannel + L")";
									text += L"\n程序架构和系统架构 " + programArchitecture + L" | " + targetArchitecture;
#ifdef IDT_RELEASE
									text += L"\n程序构建模式为发布版本";
#else
									text += L"\n程序构建模式为非发布调测版本";
#endif

									text += L"\n\n程序更新目标架构 " + utf8ToUtf16(setlist.updateArchitecture);
									text += L"\n程序构建时间 " + buildTime;
								}

								int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;

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
						ImGui::BeginChild("软件版本#2", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("用户信息");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("复制用户ID", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("复制用户ID");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

								ImGui::TextUnformatted(("用户ID " + utf16ToUtf8(userId)).c_str());
							}
							{
								ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								if (ImGui::Button("复制", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									OpenClipboard(NULL); // 打开剪切板
									EmptyClipboard(); // 清空剪切板
									size_t size = (userId.length() + 1) * sizeof(wchar_t); // 计算需要的内存大小（包括结尾的null字符）
									HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size); // 分配全局内存
									wchar_t* pDest = (wchar_t*)GlobalLock(hGlobal); // 锁定内存并获取指针
									wcscpy_s(pDest, userId.length() + 1, userId.c_str()); // 复制文本到全局内存
									GlobalUnlock(hGlobal); // 解锁内存
									SetClipboardData(CF_UNICODETEXT, hGlobal); // 设置剪切板数据
									CloseClipboard(); // 关闭剪切板
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
						ImGui::BeginChild("软件版本#3", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("软件修复");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("修复软件", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("修复软件");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

								ImGui::TextUnformatted("重新安装软件至所选通道的最新版本");
							}
							{
								ImGui::SetCursorPos({ 630.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								if (ImGui::Button("修复", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
								{
									if (targetArchitecture == L"win64") setlist.updateArchitecture = "win64";
									else if (targetArchitecture == L"arm64") setlist.updateArchitecture = "arm64";
									else setlist.updateArchitecture = "win32";
									WriteSetting();

									mandatoryUpdate = true;
									if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNotStarted) thread(AutomaticUpdate).detach();
									else AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
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
						ImGui::BeginChild("软件版本#4", { 750.0f * settingGlobalScale,155.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("更新偏好");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("自动更新（静默）", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("自动更新（静默）");
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!EnableAutoUpdate)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##自动更新（静默）", &EnableAutoUpdate, config);

								if (setlist.enableAutoUpdate != EnableAutoUpdate)
								{
									setlist.enableAutoUpdate = EnableAutoUpdate;
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("更新通道", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("更新通道");
							}
							{
								ImGui::SetCursorPos({ (730.0f - 280.0f) * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(280 * settingGlobalScale);

								ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

								int UpdateChannelMode;

								vector<char*> vec;
								vec.emplace_back(_strdup(("  " + string("正式通道(LTS)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("预览通道(Insider)")).c_str()));

								if (setlist.UpdateChannel == "Insider") UpdateChannelMode = 1;
								else UpdateChannelMode = 0;

								if (ImGui::Combo("##更新通道", &UpdateChannelMode, vec.data(), vec.size()))
								{
									if ((UpdateChannelMode == 0 && setlist.UpdateChannel != "LTS") ||
										(UpdateChannelMode == 1 && setlist.UpdateChannel != "Insider"))
									{
										if (UpdateChannelMode == 1) setlist.UpdateChannel = "Insider";
										else setlist.UpdateChannel = "LTS";
										WriteSetting();

										AutomaticUpdateState = AutomaticUpdateStateEnum::UpdateObtainInformation;
									}
								}

								for (char* ptr : vec) free(ptr), ptr = nullptr;
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

				// 常规
				case settingTabEnum::tab2:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
					ImGui::BeginChild("常规", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, false);

					ImGui::SetCursorPosY(20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular]).c_str());
					}

					/*
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
						ImGui::BeginChild("常规#1", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular1]).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("语言", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular1_1]).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular1_2]).c_str());
							}
							{
								ImGui::SetCursorPos({ (730.0f - 280.0f) * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(280 * settingGlobalScale);

								ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

								vector<char*> vec;
								vec.emplace_back(_strdup(("  " + string("English(en-US)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("简体中文(zh-CN)")).c_str()));
								// vec.emplace_back(_strdup(("  " + string("正體中文(zh-TW)")).c_str()));

								if (ImGui::Combo("##语言", &SelectLanguage, vec.data(), vec.size()))
								{
									if (setlist.selectLanguage != SelectLanguage)
									{
										setlist.selectLanguage = SelectLanguage;
										WriteSetting();
									}
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
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
					*/
					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
						ImGui::BeginChild("常规#2", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular2]).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("开机自动启动", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular2_1]).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular2_2]).c_str());
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!StartUp)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##开机自动启动", &StartUp, config);

								if (setlist.startUp != StartUp)
								{
									SetStartupState(StartUp, GetCurrentExePath(), L"$Inkeys");

									setlist.startUp = StartUp;
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
						ImGui::BeginChild("常规#3", { 750.0f * settingGlobalScale,165.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular3]).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("主题", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular3_1]).c_str());
							}
							{
								ImGui::SetCursorPos({ (730.0f - 280.0f) * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(280 * settingGlobalScale);

								ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

								vector<char*> vec;
								vec.emplace_back(_strdup(("  " + get<string>(i18n[i18nEnum::SettingsRegular3_3])).c_str()));
								vec.emplace_back(_strdup(("  " + get<string>(i18n[i18nEnum::SettingsRegular3_4])).c_str()));
								vec.emplace_back(_strdup(("  " + get<string>(i18n[i18nEnum::SettingsRegular3_5])).c_str()));
								vec.emplace_back(_strdup(("  " + get<string>(i18n[i18nEnum::SettingsRegular3_6])).c_str()));

								if (ImGui::Combo("##主题", &SetSkinMode, vec.data(), vec.size()))
								{
									if (setlist.SetSkinMode != SetSkinMode)
									{
										setlist.SetSkinMode = SetSkinMode;
										WriteSetting();

										if (SetSkinMode == 0) setlist.SkinMode = 1;
										else setlist.SkinMode = SetSkinMode;
									}
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
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
							ImGui::BeginChild("选项界面 UI 缩放", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular3_7]).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular3_8]).c_str());
							}
							{
								ImGui::SetCursorPos({ 435.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::PushItemWidth(300.0f * settingGlobalScale);

								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 95, 184, 230));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f * settingGlobalScale));
								ImGui::SliderFloat("##选项界面 UI 缩放", &SettingGlobalScale, 1.0f, 2.0f, "");
								SettingGlobalScale = round(SettingGlobalScale * 100) / 100;

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

									ImGui::TextUnformatted(vformat(get<string>(i18n[i18nEnum::SettingsRegular3_9]), make_format_args(SettingGlobalScale)).c_str());

									ImGui::EndTooltip();
								}
								if (!isItemActive && SettingGlobalScale != setlist.settingGlobalScale)
								{
									setlist.settingGlobalScale = SettingGlobalScale;
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
						ImGui::BeginChild("常规#4", { 750.0f * settingGlobalScale,350.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular4]).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("置顶间隔", { 750.0f * settingGlobalScale,120.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("置顶间隔");
							}
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("置顶间隔-介绍", { 410.0f * settingGlobalScale,50.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

									ImGui::TextWrapped("在非绘制模式下将按此间隔置顶窗口，落笔时暂停置顶窗口。更短的间隔可以更少地排除其他软件的窗口的干扰，但会占用更多的CPU资源。"); // 如果其他软件其他软件的窗口持续干扰，建议使用超级置顶插件。
								}

								{
									if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
									if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
									while (PushFontNum) PushFontNum--, ImGui::PopFont();
								}
								ImGui::EndChild();
							}

							cursosPosY = 0;
							{
								ImGui::SetCursorPos({ (730.0f - 280.0f) * settingGlobalScale, cursosPosY + 50.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(280 * settingGlobalScale);

								ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

								vector<char*> vec;
								vec.emplace_back(_strdup(("  " + string("非常短(100ms)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("短(500ms)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("较短(1s)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("中等(3s)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("较长(5s)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("长(10s)")).c_str()));
								vec.emplace_back(_strdup(("  " + string("非常长(30s)")).c_str()));

								if (ImGui::Combo("##置顶间隔", &TopSleepTime, vec.data(), vec.size()))
								{
									if (setlist.topSleepTime != TopSleepTime)
									{
										setlist.topSleepTime = TopSleepTime;
										WriteSetting();

										topWindowNow = true;
									}
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
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
							ImGui::BeginChild("右键点击主图标关闭程序", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular4_1]).c_str());
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular4_2]).c_str());
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!RightClickClose)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##右键点击主图标关闭程序", &RightClickClose, config);

								if (setlist.RightClickClose != RightClickClose)
								{
									setlist.RightClickClose = RightClickClose;
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("常规#41", { 750.0f * settingGlobalScale,120.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular4_3]).c_str());
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!BrushRecover)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##画笔绘制时收起主栏", &BrushRecover, config);

								if (setlist.BrushRecover != BrushRecover)
								{
									setlist.BrushRecover = BrushRecover;
									WriteSetting();
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
								ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular4_4]).c_str());
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!RubberRecover)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
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
						ImGui::BeginChild("常规#5", { 750.0f * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsRegular5]).c_str());
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("避免全屏显示", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("避免全屏显示");
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!AvoidFullScreen)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##避免全屏显示", &AvoidFullScreen, config);

								if (setlist.avoidFullScreen != AvoidFullScreen)
								{
									setlist.avoidFullScreen = AvoidFullScreen;
									WriteSetting();
								}
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("避免全屏显示-介绍", { 710.0f * settingGlobalScale,30.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

									ImGui::TextWrapped("重启软件生效。软件在非绘制模式下或穿透模式下将不会保持全屏显示。这可以保证拥有自动隐藏熟悉的任务栏可以正常升起，可能可以降低置顶窗口失败的概率，还可能可以改善任务栏无法被触摸的问题。");
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

					while (PushFontNum) PushFontNum--, ImGui::PopFont();
					ImGui::EndChild();
					break;
				}

				// 绘制
				case settingTabEnum::tab3:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
					ImGui::BeginChild("绘制", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, false);

					ImGui::SetCursorPosY(20.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 1.0f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						ImGui::TextUnformatted("绘制");
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
						ImGui::BeginChild("绘制#1", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("绘图效果优化");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("绘图设备", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("绘图设备");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted("我们将根据不同设备类型优化使用体验。");
							}
							{
								ImGui::SetCursorPos({ (730.0f - 280.0f) * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(280 * settingGlobalScale);

								ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

								vector<char*> vec;
								vec.emplace_back(_strdup(("  " + string("触控屏幕")).c_str()));
								vec.emplace_back(_strdup(("  " + string("鼠标或手写笔")).c_str()));

								if (ImGui::Combo("##绘图设备", &PaintDevice, vec.data(), vec.size()))
								{
									if (setlist.paintDevice != PaintDevice)
									{
										setlist.paintDevice = PaintDevice;
										WriteSetting();

										drawingScale = GetDrawingScale();
										stopTimingError = GetStopTimingError();
									}
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
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
						ImGui::BeginChild("绘制#2", { 750.0f * settingGlobalScale,245.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("智能绘图");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("绘制#21", { 750.0f * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("抬笔拉直直线");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted("现阶段只推荐在教学一体机上使用。");
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!WaitStraighten)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##抬笔拉直直线", &WaitStraighten, config);

								if (setlist.waitStraighten != WaitStraighten)
								{
									setlist.waitStraighten = WaitStraighten;
									WriteSetting();
								}
							}

							// Separator
							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(229, 229, 229, 255));
								ImGui::Separator();
							}

							cursosPosY = ImGui::GetCursorPosY();
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("停留拉直直线");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted("绘制直线完成后按住一秒，直线将被拉直。");
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!WaitStraighten)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##停留拉直直线", &WaitStraighten, config);

								if (setlist.waitStraighten != WaitStraighten)
								{
									setlist.waitStraighten = WaitStraighten;
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("绘制#22", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("端点吸附");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted("直线和矩形的端点将会在抬笔时吸附。");
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!PointAdsorption)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
								ImGui::Toggle("##端点吸附", &PointAdsorption, config);

								if (setlist.pointAdsorption != PointAdsorption)
								{
									setlist.pointAdsorption = PointAdsorption;
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
						ImGui::BeginChild("绘制#3", { 750.0f * settingGlobalScale,90.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("绘制行为");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("绘制#3", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("抬笔平滑笔迹");
							}
							{
								ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
								if (!SmoothWriting)
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
								}
								else
								{
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
								}
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
						ImGui::BeginChild("绘制#4", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("擦除行为");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("橡皮粗细计算方式", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("橡皮粗细计算方式");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted("如果当前计算方式不满足条件，软件会继续尝试下一个。");
							}
							{
								ImGui::SetCursorPos({ (730.0f - 280.0f) * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
								ImGui::SetNextItemWidth(280 * settingGlobalScale);

								ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

								vector<char*> vec;
								vec.emplace_back(_strdup(("  " + string("压感粗细")).c_str()));
								vec.emplace_back(_strdup(("  " + string("笔速粗细")).c_str()));
								vec.emplace_back(_strdup(("  " + string("固定粗细")).c_str()));

								if (ImGui::Combo("##橡皮粗细计算方式", &EraserMode, vec.data(), vec.size()))
								{
									if (setlist.eraserSetting.eraserMode != EraserMode)
									{
										setlist.eraserSetting.eraserMode = EraserMode;
										WriteSetting();
									}
								}
								for (char* ptr : vec) free(ptr), ptr = nullptr;
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

					ImGui::EndChild();
					break;
				}

				// 性能
				case settingTabEnum::tabPerformance:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
					ImGui::BeginChild("性能", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, false);

					ImGui::SetCursorPosY(10.0f * settingGlobalScale);
					{
						ImFontMain->Scale = 0.8f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
						ImGui::TextUnformatted("性能");
					}

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
						ImGui::BeginChild("性能#1", { 750.0f * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						{
							ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
							ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted("绘图模块");
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("落笔预备", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("落笔预备");
							}
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
								ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
								ImGui::TextUnformatted("越大则越多手指落下时更快开始绘制，但会占用更多内存。");
							}
							{
								ImGui::SetCursorPos({ 435.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImGui::PushItemWidth(300.0f * settingGlobalScale);

								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 179));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(249, 249, 249, 128));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(249, 249, 249, 77));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 95, 184, 255));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 95, 184, 230));
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f * settingGlobalScale));
								ImGui::SliderInt("##落笔预备数量", &PreparationQuantity, 0, 20, "");

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

									ImGui::TextUnformatted(format("{:d} 指", PreparationQuantity).c_str());

									ImGui::EndTooltip();
								}
								if (!isItemActive && PreparationQuantity != setlist.performanceSetting.preparationQuantity)
								{
									setlist.performanceSetting.preparationQuantity = PreparationQuantity;
									WriteSetting();

									// 落笔预备
									ResetPrepareCanvas();
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

				// 插件
				case settingTabEnum::tab4:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });
					switch (settingPlugInTab)
					{
					case settingPlugInTabEnum::tabPlug1:
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
						ImGui::BeginChild("插件", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, false);

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
									ImGui::Image((void*)TextureSettingSign[5], ImVec2((float)settingSign[5].width, (float)settingSign[5].height));
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
							ImGui::BeginChild("同类软件悬浮窗拦截助手", { 750.0f * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((void*)TextureSettingSign[8], ImVec2((float)settingSign[8].width, (float)settingSign[8].height));
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
									if (ImGui::Button("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlugDDB;
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
							ImGui::BeginChild("快捷方式保障助手", { 750.0f * settingGlobalScale,115.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImGui::Image((void*)TextureSettingSign[6], ImVec2((float)settingSign[6].width, (float)settingSign[6].height));
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
									ImGui::TextUnformatted("20241123a");
								}
								{
									ImGui::SetCursorPos({ 630.0f * settingGlobalScale, 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
									if (ImGui::Button("插件选项", { 100.0f * settingGlobalScale,30.0f * settingGlobalScale })) settingPlugInTab = settingPlugInTabEnum::tabPlug3;
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
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
						break;
					}

					case settingPlugInTabEnum::tabPlug2:
					{
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
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
						if (pptComSetlist.setAdmin)
						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
							ImGui::BeginChild("PPT演示助手#11", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
								ImGui::TextUnformatted("\ue814");
							}
							{
								ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("PPT演示助手-提示2", { 560.0f * settingGlobalScale,60.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextWrapped("检测到您的PowerPoint/WPS已经设置为始终以管理员身份打开，这意味着程序需要以管理员身份运行才能识别到放映进程并开启PPT联动。如果您不希望放映软件始终以管理员身份运行，可以参考解决方案。");
									//ImGui::TextWrapped("Detected that your PowerPoint/WPS has been set to always open as administrator, which means that the program needs to run as administrator in order to recognize the screening process and open the PPT linkage. If you do not want the screening software to always run as administrator, you can refer to the solution.");
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
									ShellExecuteW(0, 0, L"https://blog.csdn.net/alan16356/article/details/143625981?fromshare=blogdetail&sharetype=blogdetail&sharerId=143625981&sharerefer=PC&sharesource=alan16356&sharefrom=from_link", 0, 0, SW_SHOW);
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
									ImGui::PushItemWidth(300.0f * settingGlobalScale);

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
									ImGui::PushItemWidth(300.0f * settingGlobalScale);

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
									ImGui::PushItemWidth(300.0f * settingGlobalScale);

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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("PPT演示助手#6", { 750.0f * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("实验选项");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("允许关闭游离卡死的 WPP 演示进程", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("允许关闭游离卡死的 WPP 演示进程");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!AutoKillWpsProcess)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##允许关闭游离卡死的 WPP 演示进程", &AutoKillWpsProcess, config);

									if (pptComSetlist.autoKillWpsProcess != AutoKillWpsProcess)
									{
										pptComSetlist.autoKillWpsProcess = AutoKillWpsProcess;
										PptComWriteSetting();
									}
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("允许关闭游离卡死的 WPP 演示进程-介绍", { 710.0f * settingGlobalScale,30.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

										ImGui::TextWrapped("在WPS退出放映后原来的WPP演示进程并不会立即关闭，而是保持游离卡死状态。此时再次开始放映PPT则无法立即识别到放映进程并开启PPT联动。开启此选项将帮助关闭游离卡死的WPP放映进程，这不会对您的文件造成影响，推荐开启。");
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

					case settingPlugInTabEnum::tabPlug3:
					{
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(243, 243, 243, 255));
						ImGui::BeginChild("快捷方式保障助手", { 765.0f * settingGlobalScale,608.0f * settingGlobalScale }, false);

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
									ImGui::TextUnformatted("快捷方式保障助手");
								}
								{
									ImGui::SetCursorPos({ 40.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("20241123a");
								}
							}
						}

						{
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 0));
							ImGui::BeginChild("快捷方式保障助手#1", { 750.0f * settingGlobalScale,130.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("基础能力");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("修正桌面快捷方式指向和名称", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("修正桌面快捷方式指向和名称");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!CorrectLnk)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##修正桌面快捷方式指向和名称", &CorrectLnk, config);

									if (setlist.shortcutAssistant.correctLnk != CorrectLnk)
									{
										setlist.shortcutAssistant.correctLnk = CorrectLnk;
										WriteSetting();

										if (setlist.shortcutAssistant.correctLnk) shortcutAssistant.SetShortcut();
									}
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 10.0f * settingGlobalScale });

									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
									PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
									ImGui::BeginChild("修正桌面快捷方式指向和名称-介绍", { 710.0f * settingGlobalScale,30.0f * settingGlobalScale }, false);

									{
										ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));

										ImGui::TextWrapped("软件启动时将修正桌面已经存在的软件快捷方式。软件自动更新后可能会改变文件名，推荐开启。");
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
							ImGui::BeginChild("快捷方式保障助手#2", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

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
								ImGui::BeginChild("创建桌面快捷方式", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("创建桌面快捷方式");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("当桌面不存在软件的快捷方式时，则创建一个。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!CreateLnk)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##创建桌面快捷方式", &CreateLnk, config);

									if (setlist.shortcutAssistant.createLnk != CreateLnk)
									{
										setlist.shortcutAssistant.createLnk = CreateLnk;
										WriteSetting();

										if (setlist.shortcutAssistant.correctLnk) shortcutAssistant.SetShortcut();
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

					case settingPlugInTabEnum::tabPlugDDB:
					{
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
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
							PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
							ImGui::BeginChild("同类软件悬浮窗拦截助手#01", { 750.0f * settingGlobalScale,130.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							float cursosPosY = 0;
							{
								ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
								ImGui::TextUnformatted("\ue814");
							}
							{
								ImGui::SetCursorPos({ 60.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });

								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
								ImGui::BeginChild("同类软件悬浮窗拦截助手-提示1", { 670.0f * settingGlobalScale,90.0f * settingGlobalScale }, false);

								{
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);

									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextWrapped("插件仅供学习交流和研究使用，不得用于其他任何用途。\n同类软件悬浮窗拦截助手(DesktopDrawpadBlocker)是依据 GPLv3 许可协议发布的开源软件。\n并在 Github 仓库得到发布：https://github.com/Alan-CRL/DesktopDrawpadBlocker\n我们发布这款程序，希望它有用，但不承诺任何质量保证责任。\n用户在使用该插件时，需自行承担由此产生的后果和影响，使用插件则视为同意此协议。");
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
							ImGui::BeginChild("同类软件悬浮窗拦截助手#1", { 750.0f * settingGlobalScale,100.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("使用插件");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
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
									ImGui::TextUnformatted("默认模式下插件随软件开启和关闭。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.Enable)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##启用插件", &Ddb.Enable, config);

									if (ddbInteractionSetList.enable != Ddb.Enable)
									{
										ddbInteractionSetList.enable = Ddb.Enable;

										WriteSetting();

										if (ddbInteractionSetList.enable)
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
												{
													if (isProcessRunning((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
													{
														// 需要关闭旧版 DDB 并更新版本

														DdbWriteInteraction(true, true);
														for (int i = 1; i <= 20; i++)
														{
															if (!isProcessRunning((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
																break;
															this_thread::sleep_for(chrono::milliseconds(500));
														}
													}
													ExtractResource((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), L"EXE", MAKEINTRESOURCE(237));
												}
											}

											// 启动 DDB
											if (!isProcessRunning((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
											{
												DdbWriteInteraction(true, false);
												if (ddbInteractionSetList.runAsAdmin) ShellExecuteW(NULL, L"runas", (dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
												else ShellExecuteW(NULL, NULL, (dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
											}
										}
										else
										{
											DdbWriteInteraction(true, true);

											// 历史遗留问题处理
											{
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
							ImGui::BeginChild("同类软件悬浮窗拦截助手#2", { 750.0f * settingGlobalScale,175.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("常规选项");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("以管理员身份启动插件", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("以管理员身份启动插件");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("切换后软件将阻塞至多拦截间隔的秒数，以等待插件的响应。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.RunAsAdmin)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##以管理员身份启动插件", &Ddb.RunAsAdmin, config);

									if (ddbInteractionSetList.runAsAdmin != Ddb.RunAsAdmin)
									{
										ddbInteractionSetList.runAsAdmin = Ddb.RunAsAdmin;
										WriteSetting();

										if (isProcessRunning((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
										{
											// 需要关闭 DDB 并重新启动
											DdbWriteInteraction(true, true);
											for (int i = 1; i <= 20; i++)
											{
												if (!isProcessRunning((dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str()))
													break;
												this_thread::sleep_for(chrono::milliseconds(500));
											}

											DdbWriteInteraction(true, false);
											if (ddbInteractionSetList.runAsAdmin) ShellExecuteW(NULL, L"runas", (dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
											else ShellExecuteW(NULL, NULL, (dataPath + L"\\DesktopDrawpadBlocker\\DesktopDrawpadBlocker.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);
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
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("拦截间隔", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("拦截间隔");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("更短的间隔可以更快拦截出现的窗口，但会占用更多的CPU资源。");
								}
								{
									ImGui::SetCursorPos({ (730.0f - 280.0f) * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									ImGui::SetNextItemWidth(280 * settingGlobalScale);

									ImFontMain->Scale = 0.82f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));

									int SleepTime, SleenTimeRecord;
									if (ddbInteractionSetList.sleepTime == 500) SleepTime = 0;
									else if (ddbInteractionSetList.sleepTime == 1000) SleepTime = 1;
									else if (ddbInteractionSetList.sleepTime == 3000) SleepTime = 2;
									else if (ddbInteractionSetList.sleepTime == 10000) SleepTime = 4;
									else SleepTime = 3;
									SleenTimeRecord = SleepTime;

									vector<char*> vec;
									vec.emplace_back(_strdup(("  " + string("短（500ms）")).c_str()));
									vec.emplace_back(_strdup(("  " + string("较短（1s）")).c_str()));
									vec.emplace_back(_strdup(("  " + string("中等（3s）")).c_str()));
									vec.emplace_back(_strdup(("  " + string("较长（5s）")).c_str()));
									vec.emplace_back(_strdup(("  " + string("长（10s）")).c_str()));

									if (ImGui::Combo("##拦截间隔", &SleepTime, vec.data(), vec.size()))
									{
										if (SleenTimeRecord != SleepTime)
										{
											if (SleepTime == 0) ddbInteractionSetList.sleepTime = 500;
											else if (SleepTime == 1) ddbInteractionSetList.sleepTime = 1000;
											else if (SleepTime == 2) ddbInteractionSetList.sleepTime = 3000;
											else if (SleepTime == 4) ddbInteractionSetList.sleepTime = 10000;
											else ddbInteractionSetList.sleepTime = 5000;
											WriteSetting();

											DdbWriteInteraction(true, false);
										}
									}
									for (char* ptr : vec) free(ptr), ptr = nullptr;
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
							ImGui::BeginChild("同类软件悬浮窗拦截助手#3", { 750.0f * settingGlobalScale,915.0f * settingGlobalScale }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

							{
								ImGui::SetCursorPos({ 0.0f * settingGlobalScale, 0.0f * settingGlobalScale });
								ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
								ImGui::TextUnformatted("精确控制");
							}

							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * settingGlobalScale);
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
								PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
								PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
								ImGui::BeginChild("精确控制#1", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃白板3 桌面悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoWhiteboard3Floating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃白板3 桌面悬浮窗", &Ddb.intercept.SeewoWhiteboard3Floating, config);

									if (ddbInteractionSetList.intercept.seewoWhiteboard3Floating != Ddb.intercept.SeewoWhiteboard3Floating)
									{
										ddbInteractionSetList.intercept.seewoWhiteboard3Floating = Ddb.intercept.SeewoWhiteboard3Floating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#2", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃白板5 桌面悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoWhiteboard5Floating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃白板5 桌面悬浮窗", &Ddb.intercept.SeewoWhiteboard5Floating, config);

									if (ddbInteractionSetList.intercept.seewoWhiteboard5Floating != Ddb.intercept.SeewoWhiteboard5Floating)
									{
										ddbInteractionSetList.intercept.seewoWhiteboard5Floating = Ddb.intercept.SeewoWhiteboard5Floating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#3", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃白板轻白板 桌面悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoWhiteboard5CFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃白板轻白板 桌面悬浮窗", &Ddb.intercept.SeewoWhiteboard5CFloating, config);

									if (ddbInteractionSetList.intercept.seewoWhiteboard5CFloating != Ddb.intercept.SeewoWhiteboard5CFloating)
									{
										ddbInteractionSetList.intercept.seewoWhiteboard5CFloating = Ddb.intercept.SeewoWhiteboard5CFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#4", { 750.0f * settingGlobalScale,130.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃品课教师端 桌面悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoPincoSideBarFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃品课教师端 桌面悬浮窗", &Ddb.intercept.SeewoPincoSideBarFloating, config);

									if (ddbInteractionSetList.intercept.seewoPincoSideBarFloating != Ddb.intercept.SeewoPincoSideBarFloating)
									{
										ddbInteractionSetList.intercept.seewoPincoSideBarFloating = Ddb.intercept.SeewoPincoSideBarFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃品课教师端 画笔悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("包括PPT控件。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoPincoDrawingFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃品课教师端 画笔悬浮窗", &Ddb.intercept.SeewoPincoDrawingFloating, config);

									if (ddbInteractionSetList.intercept.seewoPincoDrawingFloating != Ddb.intercept.SeewoPincoDrawingFloating)
									{
										ddbInteractionSetList.intercept.seewoPincoDrawingFloating = Ddb.intercept.SeewoPincoDrawingFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#5", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃PPT小工具");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoPPTFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃PPT小工具", &Ddb.intercept.SeewoPPTFloating, config);

									if (ddbInteractionSetList.intercept.seewoPPTFloating != Ddb.intercept.SeewoPPTFloating)
									{
										ddbInteractionSetList.intercept.seewoPPTFloating = Ddb.intercept.SeewoPPTFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#6", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("AiClass 桌面悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.AiClassFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##AiClass 桌面悬浮窗", &Ddb.intercept.AiClassFloating, config);

									if (ddbInteractionSetList.intercept.aiClassFloating != Ddb.intercept.AiClassFloating)
									{
										ddbInteractionSetList.intercept.aiClassFloating = Ddb.intercept.AiClassFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#7", { 750.0f * settingGlobalScale,60.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 22.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("鸿合屏幕书写");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.HiteAnnotationFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##鸿合屏幕书写", &Ddb.intercept.HiteAnnotationFloating, config);

									if (ddbInteractionSetList.intercept.hiteAnnotationFloating != Ddb.intercept.HiteAnnotationFloating)
									{
										ddbInteractionSetList.intercept.hiteAnnotationFloating = Ddb.intercept.HiteAnnotationFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#8", { 750.0f * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("畅言智慧课堂 桌面悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("需要管理员权限。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.ChangYanFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##畅言智慧课堂 桌面悬浮窗", &Ddb.intercept.ChangYanFloating, config);

									if (ddbInteractionSetList.intercept.changYanFloating != Ddb.intercept.ChangYanFloating)
									{
										ddbInteractionSetList.intercept.changYanFloating = Ddb.intercept.ChangYanFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
									}
								}

								// Separator
								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(229, 229, 229, 255));
									ImGui::Separator();
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("畅言智慧课堂 PPT底栏");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("需要管理员权限。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.ChangYanPptFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##畅言智慧课堂 PPT底栏", &Ddb.intercept.ChangYanPptFloating, config);

									if (ddbInteractionSetList.intercept.changYanPptFloating != Ddb.intercept.ChangYanPptFloating)
									{
										ddbInteractionSetList.intercept.changYanPptFloating = Ddb.intercept.ChangYanPptFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#9", { 750.0f * settingGlobalScale,70.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("天喻教育云互动课堂 桌面悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("包括PPT控件。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.IntelligentClassFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##天喻教育云互动课堂 桌面悬浮窗", &Ddb.intercept.IntelligentClassFloating, config);

									if (ddbInteractionSetList.intercept.intelligentClassFloating != Ddb.intercept.IntelligentClassFloating)
									{
										ddbInteractionSetList.intercept.intelligentClassFloating = Ddb.intercept.IntelligentClassFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
								ImGui::BeginChild("精确控制#10", { 750.0f * settingGlobalScale,140.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

								float cursosPosY = 0;
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃桌面 画笔悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("1.0/2.0 版本通用。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoDesktopAnnotationFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃桌面 画笔悬浮窗", &Ddb.intercept.SeewoDesktopAnnotationFloating, config);

									if (ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating != Ddb.intercept.SeewoDesktopAnnotationFloating)
									{
										ddbInteractionSetList.intercept.seewoDesktopAnnotationFloating = Ddb.intercept.SeewoDesktopAnnotationFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
									}
								}

								// Separator
								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPosY(cursosPosY + 25.0f * settingGlobalScale);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(229, 229, 229, 255));
									ImGui::Separator();
								}

								cursosPosY = ImGui::GetCursorPosY();
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, cursosPosY + 20.0f * settingGlobalScale });
									ImFontMain->Scale = 0.6f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
									ImGui::TextUnformatted("希沃桌面 侧栏悬浮窗");
								}
								{
									ImGui::SetCursorPos({ 20.0f * settingGlobalScale, ImGui::GetCursorPosY() });
									ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
									ImGui::TextUnformatted("1.0/2.0 版本通用，需要管理员权限。");
								}
								{
									ImGui::SetCursorPos({ 690.0f * settingGlobalScale, cursosPosY + 25.0f * settingGlobalScale });
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 6));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 15));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 95, 184, 255));
									PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 95, 184, 230));
									if (!Ddb.intercept.SeewoDesktopSideBarFloating)
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 155));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 155));
									}
									else
									{
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
										PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 95, 184, 255));
									}
									ImGui::Toggle("##希沃桌面 侧栏悬浮窗", &Ddb.intercept.SeewoDesktopSideBarFloating, config);

									if (ddbInteractionSetList.intercept.seewoDesktopSideBarFloating != Ddb.intercept.SeewoDesktopSideBarFloating)
									{
										ddbInteractionSetList.intercept.seewoDesktopSideBarFloating = Ddb.intercept.SeewoDesktopSideBarFloating;
										WriteSetting();

										DdbWriteInteraction(true, false);
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
					}
					break;
				}

				// 快捷键
				case settingTabEnum::tab5:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					ImGui::BeginChild("快捷键", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, true);

					ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);
					{
						ImGui::SetCursorPosY(10.0f * settingGlobalScale);

						int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;

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

				// 社区名片
				case settingTabEnum::tab7:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					ImGui::BeginChild("快捷键", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, true);

					ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);
					{
						ImGui::SetCursorPosY(10.0f * settingGlobalScale);

						int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;

						std::vector<std::string> lines;
						std::wstring line, temp;
						std::wstringstream ss(L"界面还在开发中，敬请期待\n\n致谢名单（很抱歉当前界面尚未完善）\n郑子杰 Zijie Zheng ￥151.2\nbin ￥100\n路人甲 ￥100\n启幕￥66\nHettyBig ￥20\n建俊 ￥19.99\nLEON - 小清新 ￥19.99\n2,2,3-三甲基戊烷 ￥10\n[微信支付用户]*志 ￥9.99\n凌汛 ￥9.99\nKrouis ￥9.99\n爱发电用户_997e8 ￥9.99\n爱发电用户_55381 ￥9.99\n\n和所有支持智绘教的朋友们~");

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

				// 赞助我们
				case settingTabEnum::tab8:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });

					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					ImGui::BeginChild("赞助我们", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, true);

					ImGui::SetCursorPos({ 50.0f * settingGlobalScale,20.0f * settingGlobalScale });
					ImGui::Image((void*)TextureSettingSign[9], ImVec2((float)settingSign[9].width, (float)settingSign[9].height));

					{
						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 30.0f);
						wstring text = L"成功赞助后，可以联系作者修改社区名片赞助列表中的头像和昵称。";

						int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;

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

					{
						if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
						if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
						while (PushFontNum) PushFontNum--, ImGui::PopFont();
					}
					ImGui::EndChild();
					break;
				}

				// ---------------------

				// 程序调测
				case settingTabEnum::tab9:
				{
					ImGui::SetCursorPos({ 170.0f * settingGlobalScale,40.0f * settingGlobalScale });
					ImGui::BeginChild("程序调测", { (750.0f + 30.0f) * settingGlobalScale,608.0f * settingGlobalScale }, true);

					ImFontMain->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(ImFontMain);
					{
						ImGui::SetCursorPosY(10.0f);
						wstring text;
						{
							if (uRealTimeStylus == 2) text += L"\n\n输入设备消息：按下";
							else if (uRealTimeStylus == 3) text += L"\n\n输入设备消息：抬起";
							else if (uRealTimeStylus == 4) text += L"\n\n输入设备消息：移动";
							else text += L"\n\n输入设备消息：就绪";

							text += L"\n输入设备按下：";

							shared_lock<shared_mutex> locktouchNum(touchNumSm);
							text += touchDown ? L"是" : L"否";
							text += L"\n输入设备点：";
							text += to_wstring(touchNum) + L"\n";
							locktouchNum.unlock();

							for (int i = 0; i < touchNum; i++)
							{
								std::shared_lock<std::shared_mutex> lock1(touchPosSm);
								TouchMode mode = TouchPos[TouchList[i]];
								lock1.unlock();

								std::shared_lock<std::shared_mutex> lock2(touchSpeedSm);
								double speed = TouchSpeed[TouchList[i]];
								lock2.unlock();

								text += to_wstring(i + 1) + L" pid" + to_wstring(TouchList[i]) + L" 坐标" + to_wstring(mode.pt.x) + L"," + to_wstring(mode.pt.y) + L" 触摸面积" + to_wstring(mode.touchWidth) + L"*" + to_wstring(mode.touchHeight) + L" 速度" + to_wstring(speed) + L" 压力" + to_wstring(mode.pressure) + L"\n";
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

						int left_x = 10 * settingGlobalScale, right_x = 760 * settingGlobalScale;

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

				// 底栏：更新信息提示栏
				{
					using enum AutomaticUpdateStateEnum;

					if (AutomaticUpdateState == UpdateNotStarted)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip0]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateObtainInformation)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(251, 251, 251, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 95, 183, 255));
							ImGui::TextUnformatted("\uf167");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip1]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateInformationFail)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(253, 231, 233, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 43, 28, 255));
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip2]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateInformationDamage)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(253, 231, 233, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 43, 28, 255));
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip3]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateInformationUnStandardized)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(253, 231, 233, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 43, 28, 255));
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip4]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateDownloading)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip5]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateDownloadFail)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(253, 231, 233, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 43, 28, 255));
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip6]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateDownloadDamage)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(253, 231, 233, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 43, 28, 255));
							ImGui::TextUnformatted("\ueb90");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip7]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateRestart)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted(get<string>(i18n[i18nEnum::SettingsUpdateTip8]).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateLatest)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(223, 246, 221, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(15, 123, 15, 255));
							ImGui::TextUnformatted("\uec61");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							string channel = "（其他通道）";
							if (setlist.UpdateChannel == "LTS") channel = "（正式通道）";
							else if (setlist.UpdateChannel == "Insider") channel = "（预览通道）";

							ImGui::TextUnformatted((get<string>(i18n[i18nEnum::SettingsUpdateTip9]) + channel).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateNewer)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(223, 246, 221, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(15, 123, 15, 255));
							ImGui::TextUnformatted("\uec61");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

							string channel = "（其他通道）";
							if (setlist.UpdateChannel == "LTS") channel = "（正式通道）";
							else if (setlist.UpdateChannel == "Insider") channel = "（预览通道）";

							ImGui::TextUnformatted(((get<string>(i18n[i18nEnum::SettingsUpdateTip10])) + channel).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}
					else if (AutomaticUpdateState == UpdateNew)
					{
						ImGui::SetCursorPos({ 170.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						PushStyleVarNum++, ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 244, 206, 255));
						ImGui::BeginChild("更新状态-提示", { 675.0f * settingGlobalScale,30.0f * settingGlobalScale }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

						float cursosPosY = 0;
						{
							ImGui::SetCursorPos({ 10.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.55f, PushFontNum++, ImGui::PushFont(ImFontMain);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(157, 93, 0, 255));
							ImGui::TextUnformatted("\ue814");
						}
						{
							ImGui::SetCursorPos({ 36.0f * settingGlobalScale, cursosPosY + 8.0f * settingGlobalScale });
							ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);

							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
							ImGui::TextUnformatted((get<string>(i18n[i18nEnum::SettingsUpdateTip11])).c_str());
						}

						{
							if (PushStyleColorNum >= 0) ImGui::PopStyleColor(PushStyleColorNum), PushStyleColorNum = 0;
							if (PushStyleVarNum >= 0) ImGui::PopStyleVar(PushStyleVarNum), PushStyleVarNum = 0;
							while (PushFontNum) PushFontNum--, ImGui::PopFont();
						}
						ImGui::EndChild();
					}

					{
						ImGui::SetCursorPos({ 850.0f * settingGlobalScale, 660.0f * settingGlobalScale });
						ImFontMain->Scale = 0.5f, PushFontNum++, ImGui::PushFont(ImFontMain);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 179));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(249, 249, 249, 128));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(249, 249, 249, 77));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 228));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 15));
						if (ImGui::Button((get<string>(i18n[i18nEnum::SettingsUpdate0])).c_str(), { 100.0f * settingGlobalScale,30.0f * settingGlobalScale }))
						{
							if (AutomaticUpdateState == AutomaticUpdateStateEnum::UpdateNotStarted)
							{
								MessageBox(floating_window, L"The automatic update module has not been activated, which means that you are not using an official release. \nPlease go to the \"version\" page and click \"Fix Software\".\n自动更新模块尚未启动，这意味着您使用的不是官方发布版本。\n请前往“软件版本”页并点击“修复软件”。", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);
							}
							else AutomaticUpdateState = UpdateObtainInformation;
						}
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
				::ShowWindow(setting_window, SW_SHOWNOACTIVATE);
				//::SetForegroundWindow(setting_window);
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

		// 拦截任务栏关闭指令
		if ((wParam & 0xFFF0) == SC_CLOSE)
		{
			test.select = false;
			return 0;
		}

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